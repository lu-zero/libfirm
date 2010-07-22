/*
 * Copyright (C) 1995-2010 University of Karlsruhe.  All right reserved.
 *
 * This file is part of libFirm.
 *
 * This file may be distributed and/or modified under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation and appearing in the file LICENSE.GPL included in the
 * packaging of this file.
 *
 * Licensees holding valid libFirm Professional Edition licenses may use
 * this file in accordance with the libFirm Commercial License.
 * Agreement provided with the Software.
 *
 * This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
 * WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE.
 */

/**
 * @file
 * @brief   code selection (transform FIRM into SPARC FIRM)
 * @version $Id$
 */
#include "config.h"

#include "irnode_t.h"
#include "irgraph_t.h"
#include "irmode_t.h"
#include "irgmod.h"
#include "iredges.h"
#include "irvrfy.h"
#include "ircons.h"
#include "irprintf.h"
#include "dbginfo.h"
#include "iropt_t.h"
#include "debug.h"
#include "error.h"

#include "../benode.h"
#include "../beirg.h"
#include "../beutil.h"
#include "../betranshlp.h"
#include "../beabihelper.h"
#include "bearch_sparc_t.h"

#include "sparc_nodes_attr.h"
#include "sparc_transform.h"
#include "sparc_new_nodes.h"
#include "gen_sparc_new_nodes.h"

#include "gen_sparc_regalloc_if.h"
#include "sparc_cconv.h"

#include <limits.h>

DEBUG_ONLY(static firm_dbg_module_t *dbg = NULL;)

static sparc_code_gen_t      *env_cg;
static beabi_helper_env_t    *abihelper;
static const arch_register_t *sp_reg = &sparc_gp_regs[REG_SP];
static const arch_register_t *fp_reg = &sparc_gp_regs[REG_FRAME_POINTER];
static calling_convention_t  *cconv  = NULL;
static ir_mode               *mode_gp;
static ir_mode               *mode_fp;
static pmap                  *node_to_stack;

static ir_node *gen_SymConst(ir_node *node);


static inline int mode_needs_gp_reg(ir_mode *mode)
{
	return mode_is_int(mode) || mode_is_reference(mode);
}

/**
 * Create an And that will zero out upper bits.
 *
 * @param dbgi     debug info
 * @param block    the basic block
 * @param op       the original node
 * @param src_bits  number of lower bits that will remain
 */
static ir_node *gen_zero_extension(dbg_info *dbgi, ir_node *block, ir_node *op,
                                   int src_bits)
{
	if (src_bits == 8) {
		return new_bd_sparc_And_imm(dbgi, block, op, 0xFF);
	} else if (src_bits == 16) {
		ir_node *lshift = new_bd_sparc_Sll_imm(dbgi, block, op, 16);
		ir_node *rshift = new_bd_sparc_Slr_imm(dbgi, block, lshift, 16);
		return rshift;
	} else {
		panic("zero extension only supported for 8 and 16 bits");
	}
}

/**
 * Generate code for a sign extension.
 */
static ir_node *gen_sign_extension(dbg_info *dbgi, ir_node *block, ir_node *op,
                                   int src_bits)
{
	int shift_width = 32 - src_bits;
	ir_node *lshift_node = new_bd_sparc_Sll_imm(dbgi, block, op, shift_width);
	ir_node *rshift_node = new_bd_sparc_Sra_imm(dbgi, block, lshift_node, shift_width);
	return rshift_node;
}

/**
 * returns true if it is assured, that the upper bits of a node are "clean"
 * which means for a 16 or 8 bit value, that the upper bits in the register
 * are 0 for unsigned and a copy of the last significant bit for signed
 * numbers.
 */
static bool upper_bits_clean(ir_node *transformed_node, ir_mode *mode)
{
	(void) transformed_node;
	(void) mode;
	/* TODO */
	return false;
}

static ir_node *gen_extension(dbg_info *dbgi, ir_node *block, ir_node *op,
                              ir_mode *orig_mode)
{
	int bits = get_mode_size_bits(orig_mode);
	if (bits == 32)
		return op;

	if (mode_is_signed(orig_mode)) {
		return gen_sign_extension(dbgi, block, op, bits);
	} else {
		return gen_zero_extension(dbgi, block, op, bits);
	}
}


/**
 * Creates a possible DAG for a constant.
 */
static ir_node *create_const_graph_value(dbg_info *dbgi, ir_node *block,
				long value)
{
	ir_node *result;

	// we need to load hi & lo separately
	if (value < -4096 || value > 4095) {
		ir_node *hi = new_bd_sparc_HiImm(dbgi, block, (int) value);
		result = new_bd_sparc_LoImm(dbgi, block, hi, value);
		be_dep_on_frame(hi);
	} else {
		result = new_bd_sparc_Mov_imm(dbgi, block, (int) value);
		be_dep_on_frame(result);
	}

	return result;
}


/**
 * Create a DAG constructing a given Const.
 *
 * @param irn  a Firm const
 */
static ir_node *create_const_graph(ir_node *irn, ir_node *block)
{
	tarval  *tv = get_Const_tarval(irn);
	ir_mode *mode = get_tarval_mode(tv);
	dbg_info *dbgi = get_irn_dbg_info(irn);
	long value;


	if (mode_is_reference(mode)) {
		/* SPARC V8 is 32bit, so we can safely convert a reference tarval into Iu */
		assert(get_mode_size_bits(mode) == get_mode_size_bits(mode_gp));
		tv = tarval_convert_to(tv, mode_gp);
	}

	value = get_tarval_long(tv);
	return create_const_graph_value(dbgi, block, value);
}

typedef enum {
	MATCH_NONE         = 0,
	MATCH_COMMUTATIVE  = 1 << 0,
	MATCH_SIZE_NEUTRAL = 1 << 1,
} match_flags_t;

typedef ir_node* (*new_binop_reg_func) (dbg_info *dbgi, ir_node *block, ir_node *op1, ir_node *op2);
typedef ir_node* (*new_binop_fp_func) (dbg_info *dbgi, ir_node *block, ir_node *op1, ir_node *op2, ir_mode *mode);
typedef ir_node* (*new_binop_imm_func) (dbg_info *dbgi, ir_node *block, ir_node *op1, int simm13);

/**
 * checks if a node's value can be encoded as a immediate
 *
 */
static bool is_imm_encodeable(const ir_node *node)
{
	long val;

	//assert(mode_is_float_vector(get_irn_mode(node)));

	if (!is_Const(node))
		return false;

	val = get_tarval_long(get_Const_tarval(node));

	return !(val < -4096 || val > 4095);
}

/**
 * helper function for binop operations
 *
 * @param new_binop_reg_func register generation function ptr
 * @param new_binop_imm_func immediate generation function ptr
 */
static ir_node *gen_helper_binop(ir_node *node, match_flags_t flags,
				new_binop_reg_func new_reg, new_binop_imm_func new_imm)
{
	ir_node  *block   = be_transform_node(get_nodes_block(node));
	ir_node  *op1     = get_binop_left(node);
	ir_node  *new_op1;
	ir_node  *op2     = get_binop_right(node);
	ir_node  *new_op2;
	dbg_info *dbgi    = get_irn_dbg_info(node);

	if (is_imm_encodeable(op2)) {
		ir_node *new_op1 = be_transform_node(op1);
		return new_imm(dbgi, block, new_op1, get_tarval_long(get_Const_tarval(op2)));
	}

	new_op2 = be_transform_node(op2);

	if ((flags & MATCH_COMMUTATIVE) && is_imm_encodeable(op1)) {
		return new_imm(dbgi, block, new_op2, get_tarval_long(get_Const_tarval(op1)) );
	}

	new_op1 = be_transform_node(op1);

	return new_reg(dbgi, block, new_op1, new_op2);
}

/**
 * helper function for FP binop operations
 */
static ir_node *gen_helper_binfpop(ir_node *node, new_binop_fp_func new_reg)
{
	ir_node  *block   = be_transform_node(get_nodes_block(node));
	ir_node  *op1     = get_binop_left(node);
	ir_node  *new_op1;
	ir_node  *op2     = get_binop_right(node);
	ir_node  *new_op2;
	dbg_info *dbgi    = get_irn_dbg_info(node);

	new_op2 = be_transform_node(op2);
	new_op1 = be_transform_node(op1);
	return new_reg(dbgi, block, new_op1, new_op2, get_irn_mode(node));
}

/**
 * Creates an sparc Add.
 *
 * @param node   FIRM node
 * @return the created sparc Add node
 */
static ir_node *gen_Add(ir_node *node)
{
	ir_mode  *mode    = get_irn_mode(node);
	ir_node  *block   = be_transform_node(get_nodes_block(node));
	dbg_info *dbgi    = get_irn_dbg_info(node);

	(void) block;
	(void) dbgi;

	if (mode_is_float(mode))
		panic("FP not implemented yet");

	return gen_helper_binop(node, MATCH_COMMUTATIVE | MATCH_SIZE_NEUTRAL, new_bd_sparc_Add_reg, new_bd_sparc_Add_imm);
}


/**
 * Creates an sparc Sub.
 *
 * @param node       FIRM node
 * @return the created sparc Sub node
 */
static ir_node *gen_Sub(ir_node *node)
{
	ir_mode  *mode    = get_irn_mode(node);
	ir_node  *block   = be_transform_node(get_nodes_block(node));
	dbg_info *dbgi    = get_irn_dbg_info(node);

	(void) block;
	(void) dbgi;

	if (mode_is_float(mode))
		panic("FP not implemented yet");

	return gen_helper_binop(node, MATCH_SIZE_NEUTRAL, new_bd_sparc_Sub_reg, new_bd_sparc_Sub_imm);
}


/**
 * Transforms a Load.
 *
 * @param node    the ir Load node
 * @return the created sparc Load node
 */
static ir_node *gen_Load(ir_node *node)
{
	ir_mode  *mode     = get_Load_mode(node);
	ir_node  *block    = be_transform_node(get_nodes_block(node));
	ir_node  *ptr      = get_Load_ptr(node);
	ir_node  *new_ptr  = be_transform_node(ptr);
	ir_node  *mem      = get_Load_mem(node);
	ir_node  *new_mem  = be_transform_node(mem);
	dbg_info *dbgi      = get_irn_dbg_info(node);
	ir_node  *new_load = NULL;

	if (mode_is_float(mode))
		panic("SPARC: no fp implementation yet");

	new_load = new_bd_sparc_Ld(dbgi, block, new_ptr, new_mem, mode, NULL, 0, 0, false);
	set_irn_pinned(new_load, get_irn_pinned(node));

	return new_load;
}



/**
 * Transforms a Store.
 *
 * @param node    the ir Store node
 * @return the created sparc Store node
 */
static ir_node *gen_Store(ir_node *node)
{
	ir_node  *block    = be_transform_node(get_nodes_block(node));
	ir_node  *ptr      = get_Store_ptr(node);
	ir_node  *new_ptr  = be_transform_node(ptr);
	ir_node  *mem      = get_Store_mem(node);
	ir_node  *new_mem  = be_transform_node(mem);
	ir_node  *val      = get_Store_value(node);
	ir_node  *new_val  = be_transform_node(val);
	ir_mode  *mode     = get_irn_mode(val);
	dbg_info *dbgi     = get_irn_dbg_info(node);
	ir_node *new_store = NULL;

	if (mode_is_float(mode))
		panic("SPARC: no fp implementation yet");

	new_store = new_bd_sparc_St(dbgi, block, new_ptr, new_val, new_mem, mode, NULL, 0, 0, false);

	return new_store;
}

/**
 * Creates an sparc Mul.
 * returns the lower 32bits of the 64bit multiply result
 *
 * @return the created sparc Mul node
 */
static ir_node *gen_Mul(ir_node *node) {
	ir_mode  *mode    = get_irn_mode(node);
	dbg_info *dbgi     = get_irn_dbg_info(node);

	ir_node *mul;
	ir_node *proj_res_low;

	if (mode_is_float(mode)) {
		mul = gen_helper_binfpop(node, new_bd_sparc_fMul);
		return mul;
	}

	assert(mode_is_data(mode));
	mul = gen_helper_binop(node, MATCH_COMMUTATIVE | MATCH_SIZE_NEUTRAL, new_bd_sparc_Mul_reg, new_bd_sparc_Mul_imm);
	arch_irn_add_flags(mul, arch_irn_flags_modify_flags);

	proj_res_low = new_rd_Proj(dbgi, mul, mode_gp, pn_sparc_Mul_low);
	return proj_res_low;
}

/**
 * Creates an sparc Mulh.
 * Mulh returns the upper 32bits of a mul instruction
 *
 * @return the created sparc Mulh node
 */
static ir_node *gen_Mulh(ir_node *node) {
	ir_mode  *mode    = get_irn_mode(node);
	dbg_info *dbgi     = get_irn_dbg_info(node);

	ir_node *mul;
	ir_node *proj_res_hi;

	if (mode_is_float(mode))
		panic("FP not supported yet");


	assert(mode_is_data(mode));
	mul = gen_helper_binop(node, MATCH_COMMUTATIVE | MATCH_SIZE_NEUTRAL, new_bd_sparc_Mulh_reg, new_bd_sparc_Mulh_imm);
	//arch_irn_add_flags(mul, arch_irn_flags_modify_flags);
	proj_res_hi = new_rd_Proj(dbgi, mul, mode_gp, pn_sparc_Mulh_low);
	return proj_res_hi;
}

/**
 * Creates an sparc Div.
 *
 * @return the created sparc Div node
 */
static ir_node *gen_Div(ir_node *node) {

	ir_mode  *mode    = get_irn_mode(node);

	ir_node *div;

	if (mode_is_float(mode))
		panic("FP not supported yet");

	//assert(mode_is_data(mode));
	div = gen_helper_binop(node, MATCH_SIZE_NEUTRAL, new_bd_sparc_Div_reg, new_bd_sparc_Div_imm);
	return div;
}


/**
 * transform abs node:
 * mov a, b
 * sra b, 31, b
 * xor a, b
 * sub a, b
 *
 * @return
 */
static ir_node *gen_Abs(ir_node *node) {
	ir_node  *block   = be_transform_node(get_nodes_block(node));
	ir_mode  *mode    = get_irn_mode(node);
	dbg_info *dbgi    = get_irn_dbg_info(node);
	ir_node   *op     = get_Abs_op(node);

	ir_node *mov, *sra, *xor, *sub, *new_op;

	if (mode_is_float(mode))
		panic("FP not supported yet");

	new_op = be_transform_node(op);

	mov = new_bd_sparc_Mov_reg(dbgi, block, new_op);
	sra = new_bd_sparc_Sra_imm(dbgi, block, mov, 31);
	xor = new_bd_sparc_Xor_reg(dbgi, block, new_op, sra);
	sub = new_bd_sparc_Sub_reg(dbgi, block, sra, xor);

	return sub;
}

/**
 * Transforms a Not node.
 *
 * @return the created ARM Not node
 */
static ir_node *gen_Not(ir_node *node)
{
	ir_node  *block   = be_transform_node(get_nodes_block(node));
	ir_node  *op      = get_Not_op(node);
	ir_node  *new_op  = be_transform_node(op);
	dbg_info *dbgi    = get_irn_dbg_info(node);

	return new_bd_sparc_Not(dbgi, block, new_op);
}

static ir_node *gen_And(ir_node *node)
{
	ir_mode  *mode    = get_irn_mode(node);
	ir_node  *block   = be_transform_node(get_nodes_block(node));
	dbg_info *dbgi    = get_irn_dbg_info(node);

	(void) block;
	(void) dbgi;

	if (mode_is_float(mode))
		panic("FP not implemented yet");

	return gen_helper_binop(node, MATCH_COMMUTATIVE, new_bd_sparc_And_reg, new_bd_sparc_And_imm);
}

static ir_node *gen_Or(ir_node *node)
{
	ir_mode  *mode    = get_irn_mode(node);
	ir_node  *block   = be_transform_node(get_nodes_block(node));
	dbg_info *dbgi    = get_irn_dbg_info(node);

	(void) block;
	(void) dbgi;

	if (mode_is_float(mode))
		panic("FP not implemented yet");

	return gen_helper_binop(node, MATCH_COMMUTATIVE, new_bd_sparc_Or_reg, new_bd_sparc_Or_imm);
}

static ir_node *gen_Eor(ir_node *node)
{
	ir_mode  *mode    = get_irn_mode(node);
	ir_node  *block   = be_transform_node(get_nodes_block(node));
	dbg_info *dbgi    = get_irn_dbg_info(node);

	(void) block;
	(void) dbgi;

	if (mode_is_float(mode))
		panic("FP not implemented yet");

	return gen_helper_binop(node, MATCH_COMMUTATIVE, new_bd_sparc_Xor_reg, new_bd_sparc_Xor_imm);
}

static ir_node *gen_Shl(ir_node *node)
{
	return gen_helper_binop(node, MATCH_SIZE_NEUTRAL, new_bd_sparc_Sll_reg, new_bd_sparc_Sll_imm);
}

static ir_node *gen_Shr(ir_node *node)
{
	return gen_helper_binop(node, MATCH_SIZE_NEUTRAL, new_bd_sparc_Slr_reg, new_bd_sparc_Slr_imm);
}

static ir_node *gen_Shrs(ir_node *node)
{
	return gen_helper_binop(node, MATCH_SIZE_NEUTRAL, new_bd_sparc_Sra_reg, new_bd_sparc_Sra_imm);
}

/****** TRANSFORM GENERAL BACKEND NODES ********/

/**
 * Transforms a Minus node.
 *
 */
static ir_node *gen_Minus(ir_node *node)
{
	ir_node  *block   = be_transform_node(get_nodes_block(node));
	ir_node  *op      = get_Minus_op(node);
	ir_node  *new_op  = be_transform_node(op);
	dbg_info *dbgi    = get_irn_dbg_info(node);
	ir_mode  *mode    = get_irn_mode(node);

	if (mode_is_float(mode)) {
		panic("FP not implemented yet");
	}

	assert(mode_is_data(mode));
	return new_bd_sparc_Minus(dbgi, block, new_op);
}

static ir_node *make_addr(dbg_info *dbgi, ir_entity *entity)
{
	ir_node *block = get_irg_start_block(current_ir_graph);
	ir_node *node  = new_bd_sparc_SymConst(dbgi, block, entity);
	be_dep_on_frame(node);
	return node;
}

/**
 * Create an entity for a given (floatingpoint) tarval
 */
static ir_entity *create_float_const_entity(tarval *tv)
{
	ir_entity        *entity = (ir_entity*) pmap_get(env_cg->constants, tv);
	ir_initializer_t *initializer;
	ir_mode          *mode;
	ir_type          *type;
	ir_type          *glob;

	if (entity != NULL)
		return entity;

	mode   = get_tarval_mode(tv);
	type   = get_type_for_mode(mode);
	glob   = get_glob_type();
	entity = new_entity(glob, id_unique("C%u"), type);
	set_entity_visibility(entity, ir_visibility_private);
	add_entity_linkage(entity, IR_LINKAGE_CONSTANT);

	initializer = create_initializer_tarval(tv);
	set_entity_initializer(entity, initializer);

	pmap_insert(env_cg->constants, tv, entity);
	return entity;
}

/**
 * Transforms a Const node.
 *
 * @param node    the ir Const node
 * @return The transformed sparc node.
 */
static ir_node *gen_Const(ir_node *node)
{
	ir_node *block = be_transform_node(get_nodes_block(node));
	ir_mode *mode  = get_irn_mode(node);

	if (mode_is_float(mode)) {
		dbg_info  *dbgi   = get_irn_dbg_info(node);
		tarval    *tv     = get_Const_tarval(node);
		ir_entity *entity = create_float_const_entity(tv);
		ir_node   *addr   = make_addr(dbgi, entity);
		ir_node   *mem    = new_NoMem();
		ir_node   *new_op
			= new_bd_sparc_Ldf(dbgi, block, addr, mem, mode, NULL, 0, 0, false);

		ir_node   *proj   = new_Proj(new_op, mode, pn_sparc_Ldf_res);
		return proj;
	}

	return create_const_graph(node, block);
}

/**
 * AddSP
 * @param node the ir AddSP node
 * @return transformed sparc SAVE node
 */
static ir_node *gen_be_AddSP(ir_node *node)
{
	ir_node  *block  = be_transform_node(get_nodes_block(node));
	ir_node  *sz     = get_irn_n(node, be_pos_AddSP_size);
	ir_node  *new_sz = be_transform_node(sz);
	ir_node  *sp     = get_irn_n(node, be_pos_AddSP_old_sp);
	ir_node  *new_sp = be_transform_node(sp);
	dbg_info *dbgi   = get_irn_dbg_info(node);
	ir_node  *nomem  = new_NoMem();
	ir_node  *new_op;

	/* SPARC stack grows in reverse direction */
	new_op = new_bd_sparc_SubSP(dbgi, block, new_sp, new_sz, nomem);

	return new_op;
}


/**
 * SubSP
 * @param node the ir SubSP node
 * @return transformed sparc SAVE node
 */
static ir_node *gen_be_SubSP(ir_node *node)
{
	ir_node  *block  = be_transform_node(get_nodes_block(node));
	ir_node  *sz     = get_irn_n(node, be_pos_SubSP_size);
	ir_node  *new_sz = be_transform_node(sz);
	ir_node  *sp     = get_irn_n(node, be_pos_SubSP_old_sp);
	ir_node  *new_sp = be_transform_node(sp);
	dbg_info *dbgi   = get_irn_dbg_info(node);
	ir_node  *nomem  = new_NoMem();
	ir_node  *new_op;

	/* SPARC stack grows in reverse direction */
	new_op = new_bd_sparc_AddSP(dbgi, block, new_sp, new_sz, nomem);
	return new_op;
}

/**
 * transform FrameAddr
 */
static ir_node *gen_be_FrameAddr(ir_node *node)
{
	ir_node   *block  = be_transform_node(get_nodes_block(node));
	ir_entity *ent    = be_get_frame_entity(node);
	ir_node   *fp     = be_get_FrameAddr_frame(node);
	ir_node   *new_fp = be_transform_node(fp);
	dbg_info  *dbgi   = get_irn_dbg_info(node);
	ir_node   *new_node;
	new_node = new_bd_sparc_FrameAddr(dbgi, block, new_fp, ent);
	return new_node;
}

/**
 * Transform a be_Copy.
 */
static ir_node *gen_be_Copy(ir_node *node)
{
	ir_node *result = be_duplicate_node(node);
	ir_mode *mode   = get_irn_mode(result);

	if (mode_needs_gp_reg(mode)) {
		set_irn_mode(node, mode_gp);
	}

	return result;
}

/**
 * Transform a Call
 */
static ir_node *gen_be_Call(ir_node *node)
{
	ir_node *res = be_duplicate_node(node);
	arch_irn_add_flags(res, arch_irn_flags_modify_flags);
	return res;
}

/**
 * Transforms a Switch.
 *
 */
static ir_node *gen_SwitchJmp(ir_node *node)
{
	ir_node  *block    = be_transform_node(get_nodes_block(node));
	ir_node  *selector = get_Cond_selector(node);
	dbg_info *dbgi     = get_irn_dbg_info(node);
	ir_node *new_op = be_transform_node(selector);
	ir_node *const_graph;
	ir_node *sub;

	ir_node *proj;
	const ir_edge_t *edge;
	int min = INT_MAX;
	int max = INT_MIN;
	int translation;
	int pn;
	int n_projs;

	foreach_out_edge(node, edge) {
		proj = get_edge_src_irn(edge);
		assert(is_Proj(proj) && "Only proj allowed at SwitchJmp");

		pn = get_Proj_proj(proj);

		min = pn<min ? pn : min;
		max = pn>max ? pn : max;
	}

	translation = min;
	n_projs = max - translation + 1;

	foreach_out_edge(node, edge) {
		proj = get_edge_src_irn(edge);
		assert(is_Proj(proj) && "Only proj allowed at SwitchJmp");

		pn = get_Proj_proj(proj) - translation;
		set_Proj_proj(proj, pn);
	}

	const_graph = create_const_graph_value(dbgi, block, translation);
	sub = new_bd_sparc_Sub_reg(dbgi, block, new_op, const_graph);
	return new_bd_sparc_SwitchJmp(dbgi, block, sub, n_projs, get_Cond_default_proj(node) - translation);
}

static bool is_cmp_unsigned(ir_node *b_value)
{
	ir_node *pred;
	ir_node *op;

	if (!is_Proj(b_value))
		panic("can't determine cond signednes");
	pred = get_Proj_pred(b_value);
	if (!is_Cmp(pred))
		panic("can't determine cond signednes (no cmp)");
	op = get_Cmp_left(pred);
	return !mode_is_signed(get_irn_mode(op));
}

/**
 * Transform Cond nodes
 */
static ir_node *gen_Cond(ir_node *node)
{
	ir_node  *selector = get_Cond_selector(node);
	ir_mode  *mode     = get_irn_mode(selector);
	ir_node  *block;
	ir_node  *flag_node;
	bool      is_unsigned;
	pn_Cmp    pnc;
	dbg_info *dbgi;

	// switch/case jumps
	if (mode != mode_b) {
		return gen_SwitchJmp(node);
	}

	// regular if/else jumps
	assert(is_Proj(selector));
	assert(is_Cmp(get_Proj_pred(selector)));

	block       = be_transform_node(get_nodes_block(node));
	dbgi        = get_irn_dbg_info(node);
	flag_node   = be_transform_node(get_Proj_pred(selector));
	pnc         = get_Proj_proj(selector);
	is_unsigned = is_cmp_unsigned(selector);
	return new_bd_sparc_BXX(dbgi, block, flag_node, pnc, is_unsigned);
}

/**
 * transform Cmp
 */
static ir_node *gen_Cmp(ir_node *node)
{
	ir_node  *block    = be_transform_node(get_nodes_block(node));
	ir_node  *op1      = get_Cmp_left(node);
	ir_node  *op2      = get_Cmp_right(node);
	ir_mode  *cmp_mode = get_irn_mode(op1);
	dbg_info *dbgi     = get_irn_dbg_info(node);
	ir_node  *new_op1;
	ir_node  *new_op2;

	if (mode_is_float(cmp_mode)) {
		panic("FloatCmp not implemented");
	}

	/*
	if (get_mode_size_bits(cmp_mode) != 32) {
		panic("CmpMode != 32bit not supported yet");
	}
	*/

	assert(get_irn_mode(op2) == cmp_mode);

	/* compare with 0 can be done with Tst */
	/*
	if (is_Const(op2) && tarval_is_null(get_Const_tarval(op2))) {
		new_op1 = be_transform_node(op1);
		return new_bd_sparc_Tst(dbgi, block, new_op1, false,
		                          is_unsigned);
	}

	if (is_Const(op1) && tarval_is_null(get_Const_tarval(op1))) {
		new_op2 = be_transform_node(op2);
		return new_bd_sparc_Tst(dbgi, block, new_op2, true,
		                          is_unsigned);
	}
	*/

	/* integer compare */
	new_op1 = be_transform_node(op1);
	new_op1 = gen_extension(dbgi, block, new_op1, cmp_mode);
	new_op2 = be_transform_node(op2);
	new_op2 = gen_extension(dbgi, block, new_op2, cmp_mode);
	return new_bd_sparc_Cmp_reg(dbgi, block, new_op1, new_op2);
}

/**
 * Transforms a SymConst node.
 */
static ir_node *gen_SymConst(ir_node *node)
{
	ir_entity *entity = get_SymConst_entity(node);
	dbg_info  *dbgi   = get_irn_dbg_info(node);

	return make_addr(dbgi, entity);
}

/**
 * Transforms a Conv node.
 *
 */
static ir_node *gen_Conv(ir_node *node)
{
	ir_node  *block    = be_transform_node(get_nodes_block(node));
	ir_node  *op       = get_Conv_op(node);
	ir_node  *new_op   = be_transform_node(op);
	ir_mode  *src_mode = get_irn_mode(op);
	ir_mode  *dst_mode = get_irn_mode(node);
	dbg_info *dbg      = get_irn_dbg_info(node);

	int src_bits = get_mode_size_bits(src_mode);
	int dst_bits = get_mode_size_bits(dst_mode);

	if (src_mode == dst_mode)
		return new_op;

	if (mode_is_float(src_mode) || mode_is_float(dst_mode)) {
		assert((src_bits <= 64 && dst_bits <= 64) && "quad FP not implemented");

		if (mode_is_float(src_mode)) {
			if (mode_is_float(dst_mode)) {
				// float -> float conv
				if (src_bits > dst_bits) {
					return new_bd_sparc_FsTOd(dbg, block, new_op, dst_mode);
				} else {
					return new_bd_sparc_FdTOs(dbg, block, new_op, dst_mode);
				}
			} else {
				// float -> int conv
				switch (dst_bits) {
					case 32:
						return new_bd_sparc_FsTOi(dbg, block, new_op, dst_mode);
					case 64:
						return new_bd_sparc_FdTOi(dbg, block, new_op, dst_mode);
					default:
						panic("quad FP not implemented");
				}
			}
		} else {
			// int -> float conv
			switch (dst_bits) {
				case 32:
					return new_bd_sparc_FiTOs(dbg, block, new_op, src_mode);
				case 64:
					return new_bd_sparc_FiTOd(dbg, block, new_op, src_mode);
				default:
					panic("quad FP not implemented");
			}
		}
	} else { /* complete in gp registers */
		int min_bits;
		ir_mode *min_mode;

		if (src_bits == dst_bits) {
			/* kill unneccessary conv */
			return new_op;
		}

		if (src_bits < dst_bits) {
			min_bits = src_bits;
			min_mode = src_mode;
		} else {
			min_bits = dst_bits;
			min_mode = dst_mode;
		}

		if (upper_bits_clean(new_op, min_mode)) {
			return new_op;
		}

		if (mode_is_signed(min_mode)) {
			return gen_sign_extension(dbg, block, new_op, min_bits);
		} else {
			return gen_zero_extension(dbg, block, new_op, min_bits);
		}
	}
}

static ir_node *gen_Unknown(ir_node *node)
{
	ir_node  *block     = get_nodes_block(node);
	ir_node  *new_block = be_transform_node(block);
	dbg_info *dbgi      = get_irn_dbg_info(node);

	/* just produce a 0 */
	ir_mode *mode = get_irn_mode(node);
	if (mode_is_float(mode)) {
		panic("FP not implemented");
		be_dep_on_frame(node);
		return node;
	} else if (mode_needs_gp_reg(mode)) {
		return create_const_graph_value(dbgi, new_block, 0);
	}

	panic("Unexpected Unknown mode");
}

/**
 * Produces the type which sits between the stack args and the locals on the
 * stack.
 */
static ir_type *sparc_get_between_type(void)
{
	static ir_type *between_type = NULL;

	if (between_type == NULL) {
		between_type = new_type_class(new_id_from_str("sparc_between_type"));
		set_type_size_bytes(between_type, SPARC_MIN_STACKSIZE);
	}

	return between_type;
}

static void create_stacklayout(ir_graph *irg)
{
	ir_entity         *entity        = get_irg_entity(irg);
	ir_type           *function_type = get_entity_type(entity);
	be_stack_layout_t *layout        = be_get_irg_stack_layout(irg);
	ir_type           *arg_type;
	int                p;
	int                n_params;

	/* calling conventions must be decided by now */
	assert(cconv != NULL);

	/* construct argument type */
	arg_type = new_type_struct(id_mangle_u(get_entity_ident(entity), new_id_from_chars("arg_type", 8)));
	n_params = get_method_n_params(function_type);
	for (p = 0; p < n_params; ++p) {
		reg_or_stackslot_t *param = &cconv->parameters[p];
		char                buf[128];
		ident              *id;

		if (param->type == NULL)
			continue;

		snprintf(buf, sizeof(buf), "param_%d", p);
		id            = new_id_from_str(buf);
		param->entity = new_entity(arg_type, id, param->type);
		set_entity_offset(param->entity, param->offset);
	}

	memset(layout, 0, sizeof(*layout));

	layout->frame_type     = get_irg_frame_type(irg);
	layout->between_type   = sparc_get_between_type();
	layout->arg_type       = arg_type;
	layout->initial_offset = 0;
	layout->initial_bias   = 0;
	layout->stack_dir      = -1;
	layout->sp_relative    = false;

	assert(N_FRAME_TYPES == 3);
	layout->order[0] = layout->frame_type;
	layout->order[1] = layout->between_type;
	layout->order[2] = layout->arg_type;
}

/**
 * transform the start node to the prolog code + initial barrier
 */
static ir_node *gen_Start(ir_node *node)
{
	ir_graph  *irg           = get_irn_irg(node);
	ir_entity *entity        = get_irg_entity(irg);
	ir_type   *function_type = get_entity_type(entity);
	ir_node   *block         = get_nodes_block(node);
	ir_node   *new_block     = be_transform_node(block);
	dbg_info  *dbgi          = get_irn_dbg_info(node);
	ir_node   *mem;
	ir_node   *start;
	ir_node   *sp;
	ir_node   *fp;
	ir_node   *barrier;
	ir_node   *save;
	int        i;

	/* stackpointer is important at function prolog */
	be_prolog_add_reg(abihelper, sp_reg,
			arch_register_req_type_produces_sp | arch_register_req_type_ignore);
	/* function parameters in registers */
	for (i = 0; i < get_method_n_params(function_type); ++i) {
		const reg_or_stackslot_t *param = &cconv->parameters[i];
		if (param->reg0 != NULL)
			be_prolog_add_reg(abihelper, param->reg0, 0);
		if (param->reg1 != NULL)
			be_prolog_add_reg(abihelper, param->reg1, 0);
	}

	start = be_prolog_create_start(abihelper, dbgi, new_block);

	mem  = be_prolog_get_memory(abihelper);
	sp   = be_prolog_get_reg_value(abihelper, sp_reg);
	save = new_bd_sparc_Save(NULL, block, sp, mem, SPARC_MIN_STACKSIZE);
	fp   = new_r_Proj(save, mode_gp, pn_sparc_Save_frame);
	sp   = new_r_Proj(save, mode_gp, pn_sparc_Save_stack);
	mem  = new_r_Proj(save, mode_M, pn_sparc_Save_mem);
	arch_set_irn_register(fp, fp_reg);
	arch_set_irn_register(sp, sp_reg);

	be_prolog_add_reg(abihelper, fp_reg, arch_register_req_type_ignore);
	be_prolog_set_reg_value(abihelper, fp_reg, fp);

	sp = be_new_IncSP(sp_reg, new_block, sp, BE_STACK_FRAME_SIZE_EXPAND, 0);
	be_prolog_set_reg_value(abihelper, sp_reg, sp);
	be_prolog_set_memory(abihelper, mem);

	barrier = be_prolog_create_barrier(abihelper, new_block);

	return barrier;
}

static ir_node *get_stack_pointer_for(ir_node *node)
{
	/* get predecessor in stack_order list */
	ir_node *stack_pred = be_get_stack_pred(abihelper, node);
	ir_node *stack_pred_transformed;
	ir_node *stack;

	if (stack_pred == NULL) {
		/* first stack user in the current block. We can simply use the
		 * initial sp_proj for it */
		ir_node *sp_proj = be_prolog_get_reg_value(abihelper, sp_reg);
		return sp_proj;
	}

	stack_pred_transformed = be_transform_node(stack_pred);
	stack                  = pmap_get(node_to_stack, stack_pred);
	if (stack == NULL) {
		return get_stack_pointer_for(stack_pred);
	}

	return stack;
}

/**
 * transform a Return node into epilogue code + return statement
 */
static ir_node *gen_Return(ir_node *node)
{
	ir_node  *block          = get_nodes_block(node);
	ir_node  *new_block      = be_transform_node(block);
	dbg_info *dbgi           = get_irn_dbg_info(node);
	ir_node  *mem            = get_Return_mem(node);
	ir_node  *new_mem        = be_transform_node(mem);
	ir_node  *sp_proj        = get_stack_pointer_for(node);
	int       n_res          = get_Return_n_ress(node);
	ir_node  *bereturn;
	ir_node  *incsp;
	int       i;

	be_epilog_begin(abihelper);
	be_epilog_set_memory(abihelper, new_mem);
	/* connect stack pointer with initial stack pointer. fix_stack phase
	   will later serialize all stack pointer adjusting nodes */
	be_epilog_add_reg(abihelper, sp_reg,
			arch_register_req_type_produces_sp | arch_register_req_type_ignore,
			sp_proj);

	/* result values */
	for (i = 0; i < n_res; ++i) {
		ir_node                  *res_value     = get_Return_res(node, i);
		ir_node                  *new_res_value = be_transform_node(res_value);
		const reg_or_stackslot_t *slot          = &cconv->results[i];
		const arch_register_t    *reg           = slot->reg0;
		assert(slot->reg1 == NULL);
		be_epilog_add_reg(abihelper, reg, 0, new_res_value);
	}

	/* create the barrier before the epilog code */
	be_epilog_create_barrier(abihelper, new_block);

	/* epilog code: an incsp */
	sp_proj = be_epilog_get_reg_value(abihelper, sp_reg);
	incsp   = be_new_IncSP(sp_reg, new_block, sp_proj,
	                       BE_STACK_FRAME_SIZE_SHRINK, 0);
	be_epilog_set_reg_value(abihelper, sp_reg, incsp);

	bereturn = be_epilog_create_return(abihelper, dbgi, new_block);

	return bereturn;
}

static ir_node *bitcast_int_to_float(dbg_info *dbgi, ir_node *block,
                                     ir_node *node)
{
	ir_graph *irg   = current_ir_graph;
	ir_node  *stack = get_irg_frame(irg);
	ir_node  *nomem = new_NoMem();
	ir_node  *st    = new_bd_sparc_St(dbgi, block, stack, node, nomem, mode_gp,
	                                  NULL, 0, 0, true);
	ir_node  *ldf;
	set_irn_pinned(st, op_pin_state_floats);

	ldf = new_bd_sparc_Ldf(dbgi, block, stack, st, mode_fp, NULL, 0, 0, true);
	set_irn_pinned(ldf, op_pin_state_floats);

	return new_Proj(ldf, mode_fp, pn_sparc_Ldf_res);
}

static ir_node *bitcast_float_to_int(dbg_info *dbgi, ir_node *block,
                                     ir_node *node)
{
	ir_graph *irg   = current_ir_graph;
	ir_node  *stack = get_irg_frame(irg);
	ir_node  *nomem = new_NoMem();
	ir_node  *stf   = new_bd_sparc_Stf(dbgi, block, stack, node, nomem, mode_fp,
	                                   NULL, 0, 0, true);
	ir_node  *ld;
	set_irn_pinned(stf, op_pin_state_floats);

	ld = new_bd_sparc_Ld(dbgi, block, stack, stf, mode_gp, NULL, 0, 0, true);
	set_irn_pinned(ld, op_pin_state_floats);

	return new_Proj(ld, mode_fp, pn_sparc_Ld_res);
}

static ir_node *gen_Call(ir_node *node)
{
	ir_graph        *irg          = get_irn_irg(node);
	ir_node         *callee       = get_Call_ptr(node);
	ir_node         *block        = get_nodes_block(node);
	ir_node         *new_block    = be_transform_node(block);
	ir_node         *mem          = get_Call_mem(node);
	ir_node         *new_mem      = be_transform_node(mem);
	dbg_info        *dbgi         = get_irn_dbg_info(node);
	ir_type         *type         = get_Call_type(node);
	int              n_params     = get_Call_n_params(node);
	int              n_param_regs = sizeof(param_regs)/sizeof(param_regs[0]);
	/* max inputs: memory, callee, register arguments */
	int              max_inputs   = 2 + n_param_regs;
	ir_node        **in           = ALLOCAN(ir_node*, max_inputs);
	ir_node        **sync_ins     = ALLOCAN(ir_node*, max_inputs);
	struct obstack  *obst         = be_get_be_obst(irg);
	const arch_register_req_t **in_req
		= OALLOCNZ(obst, const arch_register_req_t*, max_inputs);
	calling_convention_t *cconv
		= sparc_decide_calling_convention(type, true);
	int              in_arity     = 0;
	int              sync_arity   = 0;
	int              n_caller_saves
		= sizeof(caller_saves)/sizeof(caller_saves[0]);
	ir_entity       *entity       = NULL;
	ir_node         *new_frame    = get_stack_pointer_for(node);
	ir_node         *incsp;
	int              mem_pos;
	ir_node         *res;
	int              p;
	int              i;
	int              o;
	int              out_arity;

	assert(n_params == get_method_n_params(type));

	/* construct arguments */

	/* memory input */
	in_req[in_arity] = arch_no_register_req;
	mem_pos          = in_arity;
	++in_arity;

	/* stack pointer input */
	/* construct an IncSP -> we have to always be sure that the stack is
	 * aligned even if we don't push arguments on it */
	incsp = be_new_IncSP(sp_reg, new_block, new_frame,
	                     cconv->param_stack_size, 1);
	in_req[in_arity] = sp_reg->single_req;
	in[in_arity]     = incsp;
	++in_arity;

	/* parameters */
	for (p = 0; p < n_params; ++p) {
		ir_node                  *value      = get_Call_param(node, p);
		ir_node                  *new_value  = be_transform_node(value);
		ir_node                  *new_value1 = NULL;
		const reg_or_stackslot_t *param      = &cconv->parameters[p];
		ir_type                  *param_type = get_method_param_type(type, p);
		ir_mode                  *mode       = get_type_mode(param_type);
		ir_node                  *str;

		if (mode_is_float(mode) && param->reg0 != NULL) {
			unsigned size_bits = get_mode_size_bits(mode);
			assert(size_bits == 32);
			new_value = bitcast_float_to_int(dbgi, new_block, new_value);
		}

		/* put value into registers */
		if (param->reg0 != NULL) {
			in[in_arity]     = new_value;
			in_req[in_arity] = param->reg0->single_req;
			++in_arity;
			if (new_value1 == NULL)
				continue;
		}
		if (param->reg1 != NULL) {
			assert(new_value1 != NULL);
			in[in_arity]     = new_value1;
			in_req[in_arity] = param->reg1->single_req;
			++in_arity;
			continue;
		}

		/* we need a store if we're here */
		if (new_value1 != NULL) {
			new_value = new_value1;
			mode      = mode_gp;
		}

		/* create a parameter frame if necessary */
		if (mode_is_float(mode)) {
			str = new_bd_sparc_Stf(dbgi, new_block, incsp, new_value, new_mem,
			                       mode, NULL, 0, param->offset, true);
		} else {
			str = new_bd_sparc_St(dbgi, new_block, incsp, new_value, new_mem,
								  mode, NULL, 0, param->offset, true);
		}
		sync_ins[sync_arity++] = str;
	}
	assert(in_arity <= max_inputs);

	/* construct memory input */
	if (sync_arity == 0) {
		in[mem_pos] = new_mem;
	} else if (sync_arity == 1) {
		in[mem_pos] = sync_ins[0];
	} else {
		in[mem_pos] = new_rd_Sync(NULL, new_block, sync_arity, sync_ins);
	}

	if (is_SymConst(callee)) {
		entity = get_SymConst_entity(callee);
	} else {
		in[in_arity]     = be_transform_node(callee);
		in_req[in_arity] = sparc_reg_classes[CLASS_sparc_gp].class_req;
		++in_arity;
	}

	/* outputs:
	 *  - memory
	 *  - caller saves
	 */
	out_arity = 1 + n_caller_saves;

	/* create call node */
	if (entity != NULL) {
		res = new_bd_sparc_Call_imm(dbgi, new_block, in_arity, in, out_arity,
		                            entity, 0);
	} else {
		res = new_bd_sparc_Call_reg(dbgi, new_block, in_arity, in, out_arity);
	}
	set_sparc_in_req_all(res, in_req);

	/* create output register reqs */
	o = 0;
	arch_set_out_register_req(res, o++, arch_no_register_req);
	for (i = 0; i < n_caller_saves; ++i) {
		const arch_register_t *reg = caller_saves[i];
		arch_set_out_register_req(res, o++, reg->single_req);
	}
	assert(o == out_arity);

	/* copy pinned attribute */
	set_irn_pinned(res, get_irn_pinned(node));

	/* IncSP to destroy the call stackframe */
	incsp = be_new_IncSP(sp_reg, new_block, incsp, -cconv->param_stack_size, 0);
	/* if we are the last IncSP producer in a block then we have to keep
	 * the stack value.
	 * Note: This here keeps all producers which is more than necessary */
	add_irn_dep(incsp, res);
	keep_alive(incsp);

	pmap_insert(node_to_stack, node, incsp);

	sparc_free_calling_convention(cconv);
	return res;
}

static ir_node *gen_Sel(ir_node *node)
{
	dbg_info  *dbgi      = get_irn_dbg_info(node);
	ir_node   *block     = get_nodes_block(node);
	ir_node   *new_block = be_transform_node(block);
	ir_node   *ptr       = get_Sel_ptr(node);
	ir_node   *new_ptr   = be_transform_node(ptr);
	ir_entity *entity    = get_Sel_entity(node);

	/* must be the frame pointer all other sels must have been lowered
	 * already */
	assert(is_Proj(ptr) && is_Start(get_Proj_pred(ptr)));
	/* we should not have value types from parameters anymore - they should be
	   lowered */
	assert(get_entity_owner(entity) !=
			get_method_value_param_type(get_entity_type(get_irg_entity(get_irn_irg(node)))));

	return new_bd_sparc_FrameAddr(dbgi, new_block, new_ptr, entity);
}

/**
 * Transform some Phi nodes
 */
static ir_node *gen_Phi(ir_node *node)
{
	const arch_register_req_t *req;
	ir_node  *block = be_transform_node(get_nodes_block(node));
	ir_graph *irg   = current_ir_graph;
	dbg_info *dbgi  = get_irn_dbg_info(node);
	ir_mode  *mode  = get_irn_mode(node);
	ir_node  *phi;

	if (mode_needs_gp_reg(mode)) {
		/* we shouldn't have any 64bit stuff around anymore */
		assert(get_mode_size_bits(mode) <= 32);
		/* all integer operations are on 32bit registers now */
		mode = mode_gp;
		req  = sparc_reg_classes[CLASS_sparc_gp].class_req;
	} else {
		req = arch_no_register_req;
	}

	/* phi nodes allow loops, so we use the old arguments for now
	 * and fix this later */
	phi = new_ir_node(dbgi, irg, block, op_Phi, mode, get_irn_arity(node), get_irn_in(node) + 1);
	copy_node_attr(irg, node, phi);
	be_duplicate_deps(node, phi);
	arch_set_out_register_req(phi, 0, req);
	be_enqueue_preds(node);
	return phi;
}


/**
 * Transform a Proj from a Load.
 */
static ir_node *gen_Proj_Load(ir_node *node)
{
	ir_node  *load     = get_Proj_pred(node);
	ir_node  *new_load = be_transform_node(load);
	dbg_info *dbgi     = get_irn_dbg_info(node);
	long     proj      = get_Proj_proj(node);

	/* renumber the proj */
	switch (get_sparc_irn_opcode(new_load)) {
		case iro_sparc_Ld:
			/* handle all gp loads equal: they have the same proj numbers. */
			if (proj == pn_Load_res) {
				return new_rd_Proj(dbgi, new_load, mode_gp, pn_sparc_Ld_res);
			} else if (proj == pn_Load_M) {
				return new_rd_Proj(dbgi, new_load, mode_M, pn_sparc_Ld_M);
			}
			break;
		default:
			panic("Unsupported Proj from Load");
	}

	return be_duplicate_node(node);
}

/**
 * Transform the Projs from a Cmp.
 */
static ir_node *gen_Proj_Cmp(ir_node *node)
{
	(void) node;
	panic("not implemented");
}

/**
 * transform Projs from a Div
 */
static ir_node *gen_Proj_Div(ir_node *node)
{
	ir_node  *pred     = get_Proj_pred(node);
	ir_node  *new_pred = be_transform_node(pred);
	dbg_info *dbgi     = get_irn_dbg_info(node);
	long     proj      = get_Proj_proj(node);

	switch (proj) {
	case pn_Div_res:
		if (is_sparc_Div(new_pred)) {
			return new_rd_Proj(dbgi, new_pred, mode_gp, pn_sparc_Div_res);
		}
		break;
	default:
		break;
	}
	panic("Unsupported Proj from Div");
}

static ir_node *gen_Proj_Start(ir_node *node)
{
	ir_node *block     = get_nodes_block(node);
	ir_node *new_block = be_transform_node(block);
	ir_node *barrier   = be_transform_node(get_Proj_pred(node));
	long     pn        = get_Proj_proj(node);

	switch ((pn_Start) pn) {
	case pn_Start_X_initial_exec:
		/* excahnge ProjX with a jump */
		return new_bd_sparc_Ba(NULL, new_block);
	case pn_Start_M:
		return new_r_Proj(barrier, mode_M, 0);
	case pn_Start_T_args:
		return barrier;
	case pn_Start_P_frame_base:
		return be_prolog_get_reg_value(abihelper, fp_reg);
	case pn_Start_P_tls:
		return new_Bad();
	case pn_Start_max:
		break;
	}
	panic("Unexpected start proj: %ld\n", pn);
}

static ir_node *gen_Proj_Proj_Start(ir_node *node)
{
	long       pn          = get_Proj_proj(node);
	ir_node   *block       = get_nodes_block(node);
	ir_node   *new_block   = be_transform_node(block);
	ir_entity *entity      = get_irg_entity(current_ir_graph);
	ir_type   *method_type = get_entity_type(entity);
	ir_type   *param_type  = get_method_param_type(method_type, pn);
	const reg_or_stackslot_t *param;

	/* Proj->Proj->Start must be a method argument */
	assert(get_Proj_proj(get_Proj_pred(node)) == pn_Start_T_args);

	param = &cconv->parameters[pn];

	if (param->reg0 != NULL) {
		/* argument transmitted in register */
		ir_mode               *mode  = get_type_mode(param_type);
		const arch_register_t *reg   = param->reg0;
		ir_node               *value = be_prolog_get_reg_value(abihelper, reg);

		if (mode_is_float(mode)) {
			/* convert integer value to float */
			value = bitcast_int_to_float(NULL, new_block, value);
		}
		return value;
	} else {
		/* argument transmitted on stack */
		ir_node  *fp   = be_prolog_get_reg_value(abihelper, fp_reg);
		ir_node  *mem  = be_prolog_get_memory(abihelper);
		ir_mode  *mode = get_type_mode(param->type);
		ir_node  *load;
		ir_node  *value;

		if (mode_is_float(mode)) {
			load  = new_bd_sparc_Ldf(NULL, new_block, fp, mem, mode,
			                         param->entity, 0, 0, true);
			value = new_r_Proj(load, mode_fp, pn_sparc_Ldf_res);
		} else {
			load  = new_bd_sparc_Ld(NULL, new_block, fp, mem, mode,
			                        param->entity, 0, 0, true);
			value = new_r_Proj(load, mode_gp, pn_sparc_Ld_res);
		}
		set_irn_pinned(load, op_pin_state_floats);

		return value;
	}
}

static ir_node *gen_Proj_Call(ir_node *node)
{
	long     pn        = get_Proj_proj(node);
	ir_node *call      = get_Proj_pred(node);
	ir_node *new_call  = be_transform_node(call);

	switch ((pn_Call) pn) {
	case pn_Call_M:
		return new_r_Proj(new_call, mode_M, 0);
	case pn_Call_X_regular:
	case pn_Call_X_except:
	case pn_Call_T_result:
	case pn_Call_P_value_res_base:
	case pn_Call_max:
		break;
	}
	panic("Unexpected Call proj %ld\n", pn);
}

/**
 * Finds number of output value of a mode_T node which is constrained to
 * a single specific register.
 */
static int find_out_for_reg(ir_node *node, const arch_register_t *reg)
{
	int n_outs = arch_irn_get_n_outs(node);
	int o;

	for (o = 0; o < n_outs; ++o) {
		const arch_register_req_t *req = arch_get_out_register_req(node, o);
		if (req == reg->single_req)
			return o;
	}
	return -1;
}

static ir_node *gen_Proj_Proj_Call(ir_node *node)
{
	long                  pn            = get_Proj_proj(node);
	ir_node              *call          = get_Proj_pred(get_Proj_pred(node));
	ir_node              *new_call      = be_transform_node(call);
	ir_type              *function_type = get_Call_type(call);
	calling_convention_t *cconv
		= sparc_decide_calling_convention(function_type, true);
	const reg_or_stackslot_t *res = &cconv->results[pn];
	const arch_register_t    *reg = res->reg0;
	ir_mode                  *mode;
	int                       regn;

	assert(res->reg0 != NULL && res->reg1 == NULL);
	regn = find_out_for_reg(new_call, reg);
	if (regn < 0) {
		panic("Internal error in calling convention for return %+F", node);
	}
	mode = res->reg0->reg_class->mode;

	sparc_free_calling_convention(cconv);

	return new_r_Proj(new_call, mode, regn);
}

/**
 * Transform a Proj node.
 */
static ir_node *gen_Proj(ir_node *node)
{
	ir_node *pred = get_Proj_pred(node);
	long     pn   = get_Proj_proj(node);

	switch (get_irn_opcode(pred)) {
	case iro_Store:
		if (pn == pn_Store_M) {
			return be_transform_node(pred);
		} else {
			panic("Unsupported Proj from Store");
		}
		break;
	case iro_Load:
		return gen_Proj_Load(node);
	case iro_Call:
		return gen_Proj_Call(node);
	case iro_Cmp:
		return gen_Proj_Cmp(node);
	case iro_Cond:
		return be_duplicate_node(node);
	case iro_Div:
		return gen_Proj_Div(node);
	case iro_Start:
		return gen_Proj_Start(node);
	case iro_Proj: {
		ir_node *pred_pred = get_Proj_pred(pred);
		if (is_Call(pred_pred)) {
			return gen_Proj_Proj_Call(node);
		} else if (is_Start(pred_pred)) {
			return gen_Proj_Proj_Start(node);
		}
		/* FALLTHROUGH */
	}
	default:
		panic("code selection didn't expect Proj after %+F\n", pred);
	}
}


/**
 * transform a Jmp
 */
static ir_node *gen_Jmp(ir_node *node)
{
	ir_node  *block     = get_nodes_block(node);
	ir_node  *new_block = be_transform_node(block);
	dbg_info *dbgi      = get_irn_dbg_info(node);

	return new_bd_sparc_Ba(dbgi, new_block);
}

/**
 * configure transformation callbacks
 */
void sparc_register_transformers(void)
{
	be_start_transform_setup();

	be_set_transform_function(op_Abs,          gen_Abs);
	be_set_transform_function(op_Add,          gen_Add);
	be_set_transform_function(op_And,          gen_And);
	be_set_transform_function(op_be_AddSP,     gen_be_AddSP);
	be_set_transform_function(op_be_Call,      gen_be_Call);
	be_set_transform_function(op_be_Copy,      gen_be_Copy);
	be_set_transform_function(op_be_FrameAddr, gen_be_FrameAddr);
	be_set_transform_function(op_be_SubSP,     gen_be_SubSP);
	be_set_transform_function(op_Call,         gen_Call);
	be_set_transform_function(op_Cmp,          gen_Cmp);
	be_set_transform_function(op_Cond,         gen_Cond);
	be_set_transform_function(op_Const,        gen_Const);
	be_set_transform_function(op_Conv,         gen_Conv);
	be_set_transform_function(op_Div,          gen_Div);
	be_set_transform_function(op_Eor,          gen_Eor);
	be_set_transform_function(op_Jmp,          gen_Jmp);
	be_set_transform_function(op_Load,         gen_Load);
	be_set_transform_function(op_Minus,        gen_Minus);
	be_set_transform_function(op_Mul,          gen_Mul);
	be_set_transform_function(op_Mulh,         gen_Mulh);
	be_set_transform_function(op_Not,          gen_Not);
	be_set_transform_function(op_Or,           gen_Or);
	be_set_transform_function(op_Phi,          gen_Phi);
	be_set_transform_function(op_Proj,         gen_Proj);
	be_set_transform_function(op_Return,       gen_Return);
	be_set_transform_function(op_Sel,          gen_Sel);
	be_set_transform_function(op_Shl,          gen_Shl);
	be_set_transform_function(op_Shr,          gen_Shr);
	be_set_transform_function(op_Shrs,         gen_Shrs);
	be_set_transform_function(op_Start,        gen_Start);
	be_set_transform_function(op_Store,        gen_Store);
	be_set_transform_function(op_Sub,          gen_Sub);
	be_set_transform_function(op_SymConst,     gen_SymConst);
	be_set_transform_function(op_Unknown,      gen_Unknown);

	be_set_transform_function(op_sparc_Save,   be_duplicate_node);
}

/* hack to avoid unused fp proj at start barrier */
static void assure_fp_keep(void)
{
	unsigned         n_users = 0;
	const ir_edge_t *edge;
	ir_node         *fp_proj = be_prolog_get_reg_value(abihelper, fp_reg);

	foreach_out_edge(fp_proj, edge) {
		ir_node *succ = get_edge_src_irn(edge);
		if (is_End(succ) || is_Anchor(succ))
			continue;
		++n_users;
	}

	if (n_users == 0) {
		ir_node *block = get_nodes_block(fp_proj);
		ir_node *in[1] = { fp_proj };
		be_new_Keep(block, 1, in);
	}
}

/**
 * Transform a Firm graph into a SPARC graph.
 */
void sparc_transform_graph(sparc_code_gen_t *cg)
{
	ir_graph  *irg    = cg->irg;
	ir_entity *entity = get_irg_entity(irg);
	ir_type   *frame_type;

	sparc_register_transformers();
	env_cg = cg;

	node_to_stack = pmap_create();

	mode_gp = mode_Iu;
	mode_fp = mode_F;

	abihelper = be_abihelper_prepare(irg);
	be_collect_stacknodes(abihelper);
	cconv = sparc_decide_calling_convention(get_entity_type(entity), false);
	create_stacklayout(irg);

	be_transform_graph(cg->irg, NULL);
	assure_fp_keep();

	be_abihelper_finish(abihelper);
	sparc_free_calling_convention(cconv);

	frame_type = get_irg_frame_type(irg);
	if (get_type_state(frame_type) == layout_undefined)
		default_layout_compound_type(frame_type);

	pmap_destroy(node_to_stack);
	node_to_stack = NULL;

	be_add_missing_keeps(irg);
}

void sparc_init_transform(void)
{
	FIRM_DBG_REGISTER(dbg, "firm.be.sparc.transform");
}
