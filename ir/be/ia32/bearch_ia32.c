/*
 * This file is part of libFirm.
 * Copyright (C) 2012 University of Karlsruhe.
 */

/**
 * @file
 * @brief       This is the main ia32 firm backend driver.
 * @author      Christian Wuerdig
 */
#include "config.h"

#include "lc_opts.h"
#include "lc_opts_enum.h"

#include <math.h>

#include "irarch.h"
#include "irgwalk.h"
#include "irprog.h"
#include "irprintf.h"
#include "iredges_t.h"
#include "ircons.h"
#include "irflag.h"
#include "irgmod.h"
#include "irgopt.h"
#include "irgopt.h"
#include "irdump.h"
#include "pdeq.h"
#include "pset.h"
#include "debug.h"
#include "error.h"
#include "xmalloc.h"
#include "irtools.h"
#include "iroptimize.h"
#include "instrument.h"
#include "iropt_t.h"
#include "lower_dw.h"
#include "lower_calls.h"
#include "lower_mode_b.h"
#include "lower_softfloat.h"
#include "firmstat_t.h"

#include "beabi.h"
#include "benode.h"
#include "belower.h"
#include "besched.h"
#include "be.h"
#include "be_t.h"
#include "beirgmod.h"
#include "beblocksched.h"
#include "bespillutil.h"
#include "bespillslots.h"
#include "bemodule.h"
#include "begnuas.h"
#include "bestate.h"
#include "beflags.h"
#include "betranshlp.h"
#include "belistsched.h"
#include "beabihelper.h"
#include "bestack.h"
#include "beutil.h"

#include "bearch_ia32_t.h"

#include "ia32_new_nodes.h"
#include "gen_ia32_regalloc_if.h"
#include "ia32_common_transform.h"
#include "ia32_transform.h"
#include "ia32_emitter.h"
#include "ia32_optimize.h"
#include "ia32_x87.h"
#include "ia32_dbg_stat.h"
#include "ia32_finish.h"
#include "ia32_fpu.h"
#include "ia32_architecture.h"

#ifdef FIRM_GRGEN_BE
#include "ia32_pbqp_transform.h"

transformer_t be_transformer = TRANSFORMER_DEFAULT;
#endif

ir_mode *ia32_mode_fpcw;
ir_mode *ia32_mode_E;
ir_type *ia32_type_E;

/** The current omit-fp state */
static ir_type *omit_fp_between_type   = NULL;
static ir_type *between_type           = NULL;
static ir_entity *old_bp_ent           = NULL;
static ir_entity *ret_addr_ent         = NULL;
static ir_entity *omit_fp_ret_addr_ent = NULL;
static int        precise_x87_spills;

/**
 * The environment for the intrinsic mapping.
 */
static ia32_intrinsic_env_t intrinsic_env = {
	NULL,    /* entity for __divdi3 library call */
	NULL,    /* entity for __moddi3 library call */
	NULL,    /* entity for __udivdi3 library call */
	NULL,    /* entity for __umoddi3 library call */
};


typedef ir_node *(*create_const_node_func) (dbg_info *dbgi, ir_node *block);

/**
 * Used to create per-graph unique pseudo nodes.
 */
static inline ir_node *create_const(ir_graph *irg, ir_node **place,
                                    create_const_node_func func,
                                    const arch_register_t* reg)
{
	ir_node *block, *res;

	if (*place != NULL)
		return *place;

	block = get_irg_start_block(irg);
	res = func(NULL, block);
	arch_set_irn_register(res, reg);
	*place = res;

	return res;
}

/* Creates the unique per irg GP NoReg node. */
ir_node *ia32_new_NoReg_gp(ir_graph *irg)
{
	ia32_irg_data_t *irg_data = ia32_get_irg_data(irg);
	return create_const(irg, &irg_data->noreg_gp, new_bd_ia32_NoReg_GP,
	                    &ia32_registers[REG_GP_NOREG]);
}

ir_node *ia32_new_NoReg_fp(ir_graph *irg)
{
	ia32_irg_data_t *irg_data = ia32_get_irg_data(irg);
	return create_const(irg, &irg_data->noreg_fp, new_bd_ia32_NoReg_FP,
	                    &ia32_registers[REG_FP_NOREG]);
}

ir_node *ia32_new_NoReg_xmm(ir_graph *irg)
{
	ia32_irg_data_t *irg_data = ia32_get_irg_data(irg);
	return create_const(irg, &irg_data->noreg_xmm, new_bd_ia32_NoReg_XMM,
	                    &ia32_registers[REG_XMM_NOREG]);
}

ir_node *ia32_new_Fpu_truncate(ir_graph *irg)
{
	ia32_irg_data_t *irg_data = ia32_get_irg_data(irg);
	return create_const(irg, &irg_data->fpu_trunc_mode, new_bd_ia32_ChangeCW,
                        &ia32_registers[REG_FPCW]);
}


/**
 * Returns the admissible noreg register node for input register pos of node irn.
 */
static ir_node *ia32_get_admissible_noreg(ir_node *irn, int pos)
{
	ir_graph                  *irg = get_irn_irg(irn);
	const arch_register_req_t *req = arch_get_irn_register_req_in(irn, pos);

	assert(req != NULL && "Missing register requirements");
	if (req->cls == &ia32_reg_classes[CLASS_ia32_gp])
		return ia32_new_NoReg_gp(irg);

	if (ia32_cg_config.use_sse2) {
		return ia32_new_NoReg_xmm(irg);
	} else {
		return ia32_new_NoReg_fp(irg);
	}
}

static ir_entity *ia32_get_frame_entity(const ir_node *irn)
{
	return is_ia32_irn(irn) ? get_ia32_frame_ent(irn) : NULL;
}

static void ia32_set_frame_entity(ir_node *node, ir_entity *entity)
{
	if (is_be_node(node))
		be_node_set_frame_entity(node, entity);
	else
		set_ia32_frame_ent(node, entity);
}

static void ia32_set_frame_offset(ir_node *irn, int bias)
{
	if (get_ia32_frame_ent(irn) == NULL)
		return;

	if (is_ia32_Pop(irn) || is_ia32_PopMem(irn)) {
		ir_graph          *irg     = get_irn_irg(irn);
		be_stack_layout_t *layout  = be_get_irg_stack_layout(irg);
		if (layout->sp_relative) {
			/* Pop nodes modify the stack pointer before calculating the
			 * destination address, so fix this here
			 */
			bias -= 4;
		}
	}
	add_ia32_am_offs_int(irn, bias);
}

static int ia32_get_sp_bias(const ir_node *node)
{
	if (is_ia32_Call(node))
		return -(int)get_ia32_call_attr_const(node)->pop;

	if (is_ia32_Push(node))
		return 4;

	if (is_ia32_Pop(node) || is_ia32_PopMem(node))
		return -4;

	if (is_ia32_Leave(node) || is_ia32_CopyEbpEsp(node)) {
		return SP_BIAS_RESET;
	}

	return 0;
}

/**
 * Build the between type and entities if not already build.
 */
static void ia32_build_between_type(void)
{
#define IDENT(s) new_id_from_chars(s, sizeof(s)-1)
	if (between_type == NULL) {
		ir_type *old_bp_type   = new_type_primitive(mode_Iu);
		ir_type *ret_addr_type = new_type_primitive(mode_Iu);

		between_type           = new_type_struct(IDENT("ia32_between_type"));
		old_bp_ent             = new_entity(between_type, IDENT("old_bp"), old_bp_type);
		ret_addr_ent           = new_entity(between_type, IDENT("ret_addr"), ret_addr_type);

		set_entity_offset(old_bp_ent, 0);
		set_entity_offset(ret_addr_ent, get_type_size_bytes(old_bp_type));
		set_type_size_bytes(between_type, get_type_size_bytes(old_bp_type) + get_type_size_bytes(ret_addr_type));
		set_type_state(between_type, layout_fixed);

		omit_fp_between_type = new_type_struct(IDENT("ia32_between_type_omit_fp"));
		omit_fp_ret_addr_ent = new_entity(omit_fp_between_type, IDENT("ret_addr"), ret_addr_type);

		set_entity_offset(omit_fp_ret_addr_ent, 0);
		set_type_size_bytes(omit_fp_between_type, get_type_size_bytes(ret_addr_type));
		set_type_state(omit_fp_between_type, layout_fixed);
	}
#undef IDENT
}

/**
 * Produces the type which sits between the stack args and the locals on the stack.
 * it will contain the return address and space to store the old base pointer.
 * @return The Firm type modeling the ABI between type.
 */
static ir_type *ia32_abi_get_between_type(ir_graph *irg)
{
	const be_stack_layout_t *layout = be_get_irg_stack_layout(irg);
	ia32_build_between_type();
	return layout->sp_relative ? omit_fp_between_type : between_type;
}

/**
 * Return the stack entity that contains the return address.
 */
ir_entity *ia32_get_return_address_entity(ir_graph *irg)
{
	const be_stack_layout_t *layout = be_get_irg_stack_layout(irg);
	ia32_build_between_type();
	return layout->sp_relative ? omit_fp_ret_addr_ent : ret_addr_ent;
}

/**
 * Return the stack entity that contains the frame address.
 */
ir_entity *ia32_get_frame_address_entity(ir_graph *irg)
{
	const be_stack_layout_t *layout = be_get_irg_stack_layout(irg);
	ia32_build_between_type();
	return layout->sp_relative ? NULL : old_bp_ent;
}

/**
 * Get the estimated cycle count for @p irn.
 *
 * @param self The this pointer.
 * @param irn  The node.
 *
 * @return     The estimated cycle count for this operation
 */
static int ia32_get_op_estimated_cost(const ir_node *irn)
{
	int            cost;
	ia32_op_type_t op_tp;

	if (is_Proj(irn))
		return 0;
	if (!is_ia32_irn(irn))
		return 0;

	assert(is_ia32_irn(irn));

	cost  = get_ia32_latency(irn);
	op_tp = get_ia32_op_type(irn);

	if (is_ia32_CopyB(irn)) {
		cost = 250;
	}
	else if (is_ia32_CopyB_i(irn)) {
		int size = get_ia32_copyb_size(irn);
		cost     = 20 + (int)ceil((4/3) * size);
	}
	/* in case of address mode operations add additional cycles */
	else if (op_tp == ia32_AddrModeD || op_tp == ia32_AddrModeS) {
		/*
			In case of stack access and access to fixed addresses add 5 cycles
			(we assume they are in cache), other memory operations cost 20
			cycles.
		*/
		if (is_ia32_use_frame(irn) || (
		    is_ia32_NoReg_GP(get_irn_n(irn, n_ia32_base)) &&
		    is_ia32_NoReg_GP(get_irn_n(irn, n_ia32_index))
		    )) {
			cost += 5;
		} else {
			cost += 20;
		}
	}

	return cost;
}

/** Get the mode that should be used for spilling value @p node */
static ir_mode *get_spill_mode(const ir_node *node)
{
	ir_mode *mode = get_irn_mode(node);
	if (mode_is_float(mode))
		return precise_x87_spills ? ia32_mode_E : mode_D;
	return mode_Iu;
}

/**
 * Check if irn can load its operand at position i from memory (source addressmode).
 * @param irn    The irn to be checked
 * @param i      The operands position
 * @return Non-Zero if operand can be loaded
 */
static int ia32_possible_memory_operand(const ir_node *irn, unsigned int i)
{
	if (!is_ia32_irn(irn)                    || /* must be an ia32 irn */
	    get_ia32_op_type(irn) != ia32_Normal || /* must not already be a addressmode irn */
	    is_ia32_use_frame(irn))                 /* must not already use frame */
		return 0;

	ir_node *op   = get_irn_n(irn, i);
	ir_node *load = get_Proj_pred(op);
	assert(is_ia32_irn(load));
	const ir_mode *mode = get_ia32_ls_mode(load);
	if (mode_is_float(mode)) {
		if (mode != mode_D && mode != mode_F)
			return 0;
	}

	switch (get_ia32_am_support(irn)) {
	case ia32_am_none:
		return 0;

	case ia32_am_unary:
		if (i != n_ia32_unary_op)
			return 0;
		break;

	case ia32_am_binary:
		switch (i) {
		case n_ia32_binary_left: {
			if (!is_ia32_commutative(irn))
				return 0;

			/* we can't swap left/right for limited registers
			 * (As this (currently) breaks constraint handling copies) */
			arch_register_req_t const *const req
				= arch_get_irn_register_req_in(irn, n_ia32_binary_left);
			if (arch_register_req_is(req, limited))
				return 0;
			break;
		}

		case n_ia32_binary_right:
			break;

		default:
			return 0;
		}
		break;

	default:
		panic("Unknown AM type");
	}

	/* HACK: must not already use "real" memory.
	 * This can happen for Call and Div */
	if (!is_NoMem(get_irn_n(irn, n_ia32_mem)))
		return 0;

	return 1;
}

static void ia32_perform_memory_operand(ir_node *irn, unsigned int i)
{
	assert(ia32_possible_memory_operand(irn, i));

	ir_node *op           = get_irn_n(irn, i);
	ir_node *load         = get_Proj_pred(op);
	ir_mode *load_mode    = get_ia32_ls_mode(load);
	ir_node *spill        = get_irn_n(load, n_ia32_mem);
	ir_mode *dest_op_mode = get_ia32_ls_mode(irn);
	if (get_mode_size_bits(load_mode) <= get_mode_size_bits(dest_op_mode)) {
		set_ia32_ls_mode(irn, load_mode);
	}
	set_ia32_op_type(irn, ia32_AddrModeS);
	set_ia32_use_frame(irn);
	set_ia32_need_stackent(irn);

	if (i == n_ia32_binary_left                    &&
	    get_ia32_am_support(irn) == ia32_am_binary &&
	    /* immediates are only allowed on the right side */
	    !is_ia32_Immediate(get_irn_n(irn, n_ia32_binary_right))) {
		ia32_swap_left_right(irn);
		i = n_ia32_binary_right;
	}

	assert(is_NoMem(get_irn_n(irn, n_ia32_mem)));

	set_irn_n(irn, n_ia32_base, get_irg_frame(get_irn_irg(irn)));
	set_irn_n(irn, n_ia32_mem,  spill);
	set_irn_n(irn, i,           ia32_get_admissible_noreg(irn, i));
	set_ia32_is_reload(irn);

	/* kill the reload */
	assert(get_irn_n_edges(op) == 0);
	assert(get_irn_n_edges(load) == 1);
	sched_remove(load);
	kill_node(op);
	kill_node(load);
}

static const be_abi_callbacks_t ia32_abi_callbacks = {
	ia32_abi_get_between_type,
};

/* register allocator interface */
static const arch_irn_ops_t ia32_irn_ops = {
	ia32_get_frame_entity,
	ia32_set_frame_offset,
	ia32_get_sp_bias,
	ia32_get_op_estimated_cost,
	ia32_possible_memory_operand,
	ia32_perform_memory_operand,
};

static int gprof = 0;

static void ia32_before_abi(ir_graph *irg)
{
	if (gprof) {
		static ir_entity *mcount = NULL;
		if (mcount == NULL) {
			ir_type *tp = new_type_method(0, 0);
			ident   *id = new_id_from_str("mcount");
			mcount = new_entity(get_glob_type(), id, tp);
			/* FIXME: enter the right ld_ident here */
			set_entity_ld_ident(mcount, get_entity_ident(mcount));
			set_entity_visibility(mcount, ir_visibility_external);
		}
		instrument_initcall(irg, mcount);
	}
}

/**
 * Transforms the standard firm graph into
 * an ia32 firm graph
 */
static void ia32_prepare_graph(ir_graph *irg)
{
	ia32_irg_data_t *irg_data = ia32_get_irg_data(irg);

#ifdef FIRM_GRGEN_BE
	switch (be_transformer) {
	case TRANSFORMER_DEFAULT:
		/* transform remaining nodes into assembler instructions */
		ia32_transform_graph(irg);
		break;

	case TRANSFORMER_PBQP:
	case TRANSFORMER_RAND:
		/* transform nodes into assembler instructions by PBQP magic */
		ia32_transform_graph_by_pbqp(irg);
		break;

	default:
		panic("invalid transformer");
	}
#else
	ia32_transform_graph(irg);
#endif

	/* do local optimizations (mainly CSE) */
	optimize_graph_df(irg);
	/* backend code expects that outedges are always enabled */
	assure_edges(irg);

	if (irg_data->dump)
		dump_ir_graph(irg, "transformed");

	/* optimize address mode */
	ia32_optimize_graph(irg);

	/* do code placement, to optimize the position of constants */
	place_code(irg);
	/* backend code expects that outedges are always enabled */
	assure_edges(irg);

	if (irg_data->dump)
		dump_ir_graph(irg, "place");
}

ir_node *ia32_turn_back_am(ir_node *node)
{
	dbg_info *dbgi  = get_irn_dbg_info(node);
	ir_graph *irg   = get_irn_irg(node);
	ir_node  *block = get_nodes_block(node);
	ir_node  *base  = get_irn_n(node, n_ia32_base);
	ir_node  *idx   = get_irn_n(node, n_ia32_index);
	ir_node  *mem   = get_irn_n(node, n_ia32_mem);
	ir_node  *noreg;

	ir_node  *load     = new_bd_ia32_Load(dbgi, block, base, idx, mem);
	ir_node  *load_res = new_rd_Proj(dbgi, load, mode_Iu, pn_ia32_Load_res);

	ia32_copy_am_attrs(load, node);
	if (is_ia32_is_reload(node))
		set_ia32_is_reload(load);
	set_irn_n(node, n_ia32_mem, get_irg_no_mem(irg));

	switch (get_ia32_am_support(node)) {
		case ia32_am_unary:
			set_irn_n(node, n_ia32_unary_op, load_res);
			break;

		case ia32_am_binary:
			if (is_ia32_Immediate(get_irn_n(node, n_ia32_binary_right))) {
				set_irn_n(node, n_ia32_binary_left, load_res);
			} else {
				set_irn_n(node, n_ia32_binary_right, load_res);
			}
			break;

		default:
			panic("Unknown AM type");
	}
	noreg = ia32_new_NoReg_gp(irg);
	set_irn_n(node, n_ia32_base,  noreg);
	set_irn_n(node, n_ia32_index, noreg);
	set_ia32_am_offs_int(node, 0);
	set_ia32_am_sc(node, NULL);
	set_ia32_am_scale(node, 0);
	clear_ia32_am_sc_sign(node);

	/* rewire mem-proj */
	if (get_irn_mode(node) == mode_T) {
		foreach_out_edge(node, edge) {
			ir_node *out = get_edge_src_irn(edge);
			if (get_irn_mode(out) == mode_M) {
				set_Proj_pred(out, load);
				set_Proj_proj(out, pn_ia32_Load_M);
				break;
			}
		}
	}

	set_ia32_op_type(node, ia32_Normal);
	if (sched_is_scheduled(node))
		sched_add_before(node, load);

	return load_res;
}

static ir_node *flags_remat(ir_node *node, ir_node *after)
{
	/* we should turn back source address mode when rematerializing nodes */
	ia32_op_type_t type;
	ir_node        *block;
	ir_node        *copy;

	if (is_Block(after)) {
		block = after;
	} else {
		block = get_nodes_block(after);
	}

	type = get_ia32_op_type(node);
	switch (type) {
		case ia32_AddrModeS:
			ia32_turn_back_am(node);
			break;

		case ia32_AddrModeD:
			/* TODO implement this later... */
			panic("found DestAM with flag user %+F this should not happen", node);

		default: assert(type == ia32_Normal); break;
	}

	copy = exact_copy(node);
	set_nodes_block(copy, block);
	sched_add_after(after, copy);

	return copy;
}

/**
 * Called before the register allocator.
 */
static void ia32_before_ra(ir_graph *irg)
{
	/* setup fpu rounding modes */
	ia32_setup_fpu_mode(irg);

	/* fixup flags */
	be_sched_fix_flags(irg, &ia32_reg_classes[CLASS_ia32_flags],
	                   &flags_remat, NULL);

	be_add_missing_keeps(irg);
}

static ir_node *ia32_new_spill(ir_node *value, ir_node *after)
{
	ir_graph *irg   = get_irn_irg(value);
	ir_node  *block = get_block(after);
	ir_node  *frame = get_irg_frame(irg);
	ir_mode  *mode  = get_spill_mode(value);
	ir_node  *noreg = ia32_new_NoReg_gp(irg);
	ir_node  *nomem = get_irg_no_mem(irg);

	ir_node *res;
	ir_node *store;
	if (mode_is_float(mode)) {
		if (ia32_cg_config.use_sse2) {
			store = new_bd_ia32_xStore(NULL, block, frame, noreg, nomem, value);
			res   = new_r_Proj(store, mode_M, pn_ia32_xStore_M);
		} else {
			store = new_bd_ia32_fst(NULL, block, frame, noreg, nomem, value, mode);
			res   = new_r_Proj(store, mode_M, pn_ia32_fst_M);
		}
	} else if (get_mode_size_bits(mode) == 128) {
		/* Spill 128 bit SSE registers */
		store = new_bd_ia32_xxStore(NULL, block, frame, noreg, nomem, value);
		res   = new_r_Proj(store, mode_M, pn_ia32_xxStore_M);
	} else {
		store = get_mode_size_bits(mode) == 8
			? new_bd_ia32_Store_8bit(NULL, block, frame, noreg, nomem, value)
			: new_bd_ia32_Store     (NULL, block, frame, noreg, nomem, value);
		res   = new_r_Proj(store, mode_M, pn_ia32_Store_M);
	}
	set_ia32_op_type(store, ia32_AddrModeD);
	set_ia32_ls_mode(store, mode);
	set_ia32_use_frame(store);
	set_ia32_is_spill(store);
	sched_add_after(after, store);

	return res;
}

static ir_node *ia32_new_reload(ir_node *value, ir_node *spill, ir_node *before)
{
	ir_graph *irg       = get_irn_irg(before);
	ir_node  *block     = get_block(before);
	ir_mode  *mode      = get_irn_mode(value);
	ir_mode  *spillmode = get_spill_mode(value);
	ir_node  *noreg     = ia32_new_NoReg_gp(irg);
	ir_node  *frame     = get_irg_frame(irg);

	ir_node  *load;
	if (mode_is_float(spillmode)) {
		if (ia32_cg_config.use_sse2)
			load = new_bd_ia32_xLoad(NULL, block, frame, noreg, spill, spillmode);
		else
			load = new_bd_ia32_fld(NULL, block, frame, noreg, spill, spillmode);
	} else if (get_mode_size_bits(spillmode) == 128) {
		/* Reload 128 bit SSE registers */
		load = new_bd_ia32_xxLoad(NULL, block, frame, noreg, spill);
	} else {
		load = new_bd_ia32_Load(NULL, block, frame, noreg, spill);
	}
	set_ia32_op_type(load, ia32_AddrModeS);
	set_ia32_ls_mode(load, spillmode);
	set_ia32_use_frame(load);
	set_ia32_is_reload(load);
	arch_add_irn_flags(load, arch_irn_flags_reload);
	sched_add_before(before, load);

	ir_node *proj = new_r_Proj(load, mode, pn_ia32_res);

	return proj;
}

static ir_node *create_push(ir_node *node, ir_node *schedpoint, ir_node *sp, ir_node *mem, ir_entity *ent)
{
	dbg_info *dbgi  = get_irn_dbg_info(node);
	ir_node  *block = get_nodes_block(node);
	ir_graph *irg   = get_irn_irg(node);
	ir_node  *noreg = ia32_new_NoReg_gp(irg);
	ir_node  *frame = get_irg_frame(irg);

	ir_node *push = new_bd_ia32_Push(dbgi, block, frame, noreg, mem, noreg, sp);

	set_ia32_frame_ent(push, ent);
	set_ia32_use_frame(push);
	set_ia32_op_type(push, ia32_AddrModeS);
	set_ia32_ls_mode(push, mode_Is);
	set_ia32_is_spill(push);

	sched_add_before(schedpoint, push);
	return push;
}

static ir_node *create_pop(ir_node *node, ir_node *schedpoint, ir_node *sp, ir_entity *ent)
{
	dbg_info *dbgi  = get_irn_dbg_info(node);
	ir_node  *block = get_nodes_block(node);
	ir_graph *irg   = get_irn_irg(node);
	ir_node  *noreg = ia32_new_NoReg_gp(irg);
	ir_node  *frame = get_irg_frame(irg);

	ir_node *pop = new_bd_ia32_PopMem(dbgi, block, frame, noreg,
	                                  get_irg_no_mem(irg), sp);

	set_ia32_frame_ent(pop, ent);
	set_ia32_use_frame(pop);
	set_ia32_op_type(pop, ia32_AddrModeD);
	set_ia32_ls_mode(pop, mode_Is);
	set_ia32_is_reload(pop);

	sched_add_before(schedpoint, pop);

	return pop;
}

static ir_node* create_spproj(ir_node *node, ir_node *pred, int pos)
{
	dbg_info *dbgi   = get_irn_dbg_info(node);
	ir_mode  *spmode = mode_Iu;
	const arch_register_t *spreg = &ia32_registers[REG_ESP];
	ir_node *sp;

	sp = new_rd_Proj(dbgi, pred, spmode, pos);
	arch_set_irn_register(sp, spreg);

	return sp;
}

/**
 * Transform MemPerm, currently we do this the ugly way and produce
 * push/pop into/from memory cascades. This is possible without using
 * any registers.
 */
static void transform_MemPerm(ir_node *node)
{
	ir_node  *block = get_nodes_block(node);
	ir_graph *irg   = get_irn_irg(node);
	ir_node  *sp    = be_get_initial_reg_value(irg, &ia32_registers[REG_ESP]);
	int       arity = be_get_MemPerm_entity_arity(node);
	ir_node **pops  = ALLOCAN(ir_node*, arity);
	ir_node  *in[1];
	ir_node  *keep;
	int       i;

	/* create Pushs */
	for (i = 0; i < arity; ++i) {
		ir_entity *inent = be_get_MemPerm_in_entity(node, i);
		ir_entity *outent = be_get_MemPerm_out_entity(node, i);
		ir_type *enttype = get_entity_type(inent);
		unsigned entsize = get_type_size_bytes(enttype);
		unsigned entsize2 = get_type_size_bytes(get_entity_type(outent));
		ir_node *mem = get_irn_n(node, i + 1);
		ir_node *push;

		/* work around cases where entities have different sizes */
		if (entsize2 < entsize)
			entsize = entsize2;
		assert( (entsize == 4 || entsize == 8) && "spillslot on x86 should be 32 or 64 bit");

		push = create_push(node, node, sp, mem, inent);
		sp = create_spproj(node, push, pn_ia32_Push_stack);
		if (entsize == 8) {
			/* add another push after the first one */
			push = create_push(node, node, sp, mem, inent);
			add_ia32_am_offs_int(push, 4);
			sp = create_spproj(node, push, pn_ia32_Push_stack);
		}

		set_irn_n(node, i, new_r_Bad(irg, mode_X));
	}

	/* create pops */
	for (i = arity - 1; i >= 0; --i) {
		ir_entity *inent = be_get_MemPerm_in_entity(node, i);
		ir_entity *outent = be_get_MemPerm_out_entity(node, i);
		ir_type *enttype = get_entity_type(outent);
		unsigned entsize = get_type_size_bytes(enttype);
		unsigned entsize2 = get_type_size_bytes(get_entity_type(inent));
		ir_node *pop;

		/* work around cases where entities have different sizes */
		if (entsize2 < entsize)
			entsize = entsize2;
		assert( (entsize == 4 || entsize == 8) && "spillslot on x86 should be 32 or 64 bit");

		pop = create_pop(node, node, sp, outent);
		sp = create_spproj(node, pop, pn_ia32_Pop_stack);
		if (entsize == 8) {
			add_ia32_am_offs_int(pop, 4);

			/* add another pop after the first one */
			pop = create_pop(node, node, sp, outent);
			sp = create_spproj(node, pop, pn_ia32_Pop_stack);
		}

		pops[i] = pop;
	}

	in[0] = sp;
	keep  = be_new_Keep(block, 1, in);
	sched_replace(node, keep);

	/* exchange memprojs */
	foreach_out_edge_safe(node, edge) {
		ir_node *proj = get_edge_src_irn(edge);
		int p = get_Proj_proj(proj);

		assert(p < arity);

		set_Proj_pred(proj, pops[p]);
		set_Proj_proj(proj, pn_ia32_Pop_M);
	}

	/* remove memperm */
	kill_node(node);
}

/**
 * Block-Walker: Calls the transform functions Spill and Reload.
 */
static void ia32_after_ra_walker(ir_node *block, void *env)
{
	(void) env;

	/* beware: the schedule is changed here */
	sched_foreach_reverse_safe(block, node) {
		if (be_is_MemPerm(node)) {
			transform_MemPerm(node);
		}
	}
}

/**
 * Collects nodes that need frame entities assigned.
 */
static void ia32_collect_frame_entity_nodes(ir_node *node, void *data)
{
	if (!is_ia32_irn(node) || get_ia32_frame_ent(node) != NULL
	 || !is_ia32_use_frame(node))
		return;
	if (is_ia32_need_stackent(node))
		goto need_stackent;

	const ir_mode *mode;
	int            align;
	switch (get_ia32_irn_opcode(node)) {
need_stackent:
		case iro_ia32_Load: {
			const ia32_attr_t *attr = get_ia32_attr_const(node);

			if (attr->data.need_32bit_stackent) {
				mode = mode_Is;
			} else if (attr->data.need_64bit_stackent) {
				mode = mode_Ls;
			} else {
				mode = get_ia32_ls_mode(node);
			}
			align = get_mode_size_bytes(mode);
			break;
		}

		case iro_ia32_fild:
		case iro_ia32_fld:
		case iro_ia32_xLoad: {
			mode  = get_ia32_ls_mode(node);
			align = get_mode_size_bytes(mode);
			break;
		}

		case iro_ia32_FldCW: {
			/* although 2 byte would be enough 4 byte performs best */
			mode  = mode_Iu;
			align = 4;
			break;
		}

		default:
#ifndef NDEBUG
			panic("unexpected frame user while collection frame entity nodes");

		case iro_ia32_FnstCW:
		case iro_ia32_Store:
		case iro_ia32_fst:
		case iro_ia32_fist:
		case iro_ia32_fisttp:
		case iro_ia32_xStore:
		case iro_ia32_xStoreSimple:
#endif
			return;
	}
	be_fec_env_t *env = (be_fec_env_t*)data;
	be_node_needs_frame_entity(env, node, mode, align);
}

static int determine_ebp_input(ir_node *ret)
{
	const arch_register_t *bp = &ia32_registers[REG_EBP];
	int   arity               = get_irn_arity(ret);
	int   i;

	for (i = 0; i < arity; ++i) {
		ir_node *input = get_irn_n(ret, i);
		if (arch_get_irn_register(input) == bp)
			return i;
	}
	panic("no ebp input found at %+F", ret);
}

static void introduce_epilog(ir_node *ret)
{
	const arch_register_t *sp         = &ia32_registers[REG_ESP];
	const arch_register_t *bp         = &ia32_registers[REG_EBP];
	ir_graph              *irg        = get_irn_irg(ret);
	ir_type               *frame_type = get_irg_frame_type(irg);
	unsigned               frame_size = get_type_size_bytes(frame_type);
	be_stack_layout_t     *layout     = be_get_irg_stack_layout(irg);
	ir_node               *block      = get_nodes_block(ret);
	ir_node               *first_sp   = get_irn_n(ret, n_be_Return_sp);
	ir_node               *curr_sp    = first_sp;
	ir_mode               *mode_gp    = ia32_reg_classes[CLASS_ia32_gp].mode;

	if (!layout->sp_relative) {
		int      n_ebp   = determine_ebp_input(ret);
		ir_node *curr_bp = get_irn_n(ret, n_ebp);
		if (ia32_cg_config.use_leave) {
			ir_node *leave = new_bd_ia32_Leave(NULL, block, curr_bp);
			curr_bp        = new_r_Proj(leave, mode_gp, pn_ia32_Leave_frame);
			curr_sp        = new_r_Proj(leave, mode_gp, pn_ia32_Leave_stack);
			arch_set_irn_register(curr_bp, bp);
			arch_set_irn_register(curr_sp, sp);
			sched_add_before(ret, leave);
		} else {
			ir_node *pop;
			ir_node *curr_mem = get_irn_n(ret, n_be_Return_mem);
			/* copy ebp to esp */
			curr_sp = new_bd_ia32_CopyEbpEsp(NULL, block, curr_bp);
			arch_set_irn_register(curr_sp, sp);
			sched_add_before(ret, curr_sp);

			/* pop ebp */
			pop      = new_bd_ia32_PopEbp(NULL, block, curr_mem, curr_sp);
			curr_bp  = new_r_Proj(pop, mode_gp, pn_ia32_PopEbp_res);
			curr_sp  = new_r_Proj(pop, mode_gp, pn_ia32_PopEbp_stack);
			curr_mem = new_r_Proj(pop, mode_M, pn_ia32_Pop_M);
			arch_set_irn_register(curr_bp, bp);
			arch_set_irn_register(curr_sp, sp);
			sched_add_before(ret, pop);

			set_irn_n(ret, n_be_Return_mem, curr_mem);
		}
		set_irn_n(ret, n_ebp, curr_bp);
	} else {
		ir_node *incsp = be_new_IncSP(sp, block, curr_sp, -(int)frame_size, 0);
		sched_add_before(ret, incsp);
		curr_sp = incsp;
	}
	set_irn_n(ret, n_be_Return_sp, curr_sp);

	/* keep verifier happy... */
	if (get_irn_n_edges(first_sp) == 0 && is_Proj(first_sp)) {
		kill_node(first_sp);
	}
}

/**
 * put the Prolog code at the beginning, epilog code before each return
 */
static void introduce_prolog_epilog(ir_graph *irg)
{
	const arch_register_t *sp         = &ia32_registers[REG_ESP];
	const arch_register_t *bp         = &ia32_registers[REG_EBP];
	ir_node               *start      = get_irg_start(irg);
	ir_node               *block      = get_nodes_block(start);
	ir_type               *frame_type = get_irg_frame_type(irg);
	unsigned               frame_size = get_type_size_bytes(frame_type);
	be_stack_layout_t     *layout     = be_get_irg_stack_layout(irg);
	ir_node               *initial_sp = be_get_initial_reg_value(irg, sp);
	ir_node               *curr_sp    = initial_sp;
	ir_mode               *mode_gp    = mode_Iu;

	if (!layout->sp_relative) {
		/* push ebp */
		ir_node *mem        = get_irg_initial_mem(irg);
		ir_node *noreg      = ia32_new_NoReg_gp(irg);
		ir_node *initial_bp = be_get_initial_reg_value(irg, bp);
		ir_node *push       = new_bd_ia32_Push(NULL, block, noreg, noreg, mem, initial_bp, initial_sp);
		ir_node *incsp;

		curr_sp = new_r_Proj(push, mode_gp, pn_ia32_Push_stack);
		arch_set_irn_register(curr_sp, sp);
		sched_add_after(start, push);

		/* move esp to ebp */
		ir_node *const curr_bp = be_new_Copy(block, curr_sp);
		sched_add_after(push, curr_bp);
		be_set_constr_single_reg_out(curr_bp, 0, bp, arch_register_req_type_ignore);
		curr_sp = be_new_CopyKeep_single(block, curr_sp, curr_bp);
		sched_add_after(curr_bp, curr_sp);
		be_set_constr_single_reg_out(curr_sp, 0, sp, arch_register_req_type_produces_sp);
		edges_reroute_except(initial_bp, curr_bp, push);

		incsp = be_new_IncSP(sp, block, curr_sp, frame_size, 0);
		edges_reroute_except(initial_sp, incsp, push);
		sched_add_after(curr_sp, incsp);

		/* make sure the initial IncSP is really used by someone */
		if (get_irn_n_edges(incsp) <= 1) {
			ir_node *in[] = { incsp };
			ir_node *keep = be_new_Keep(block, 1, in);
			sched_add_after(incsp, keep);
		}

		layout->initial_bias = -4;
	} else {
		ir_node *const incsp = be_new_IncSP(sp, block, initial_sp, frame_size, 0);
		edges_reroute_except(initial_sp, incsp, incsp);
		sched_add_after(start, incsp);
	}

	/* introduce epilog for every return node */
	{
		ir_node *end_block = get_irg_end_block(irg);
		int      arity     = get_irn_arity(end_block);
		int      i;

		for (i = 0; i < arity; ++i) {
			ir_node *ret = get_irn_n(end_block, i);
			assert(be_is_Return(ret));
			introduce_epilog(ret);
		}
	}
}

/**
 * Last touchups for the graph before emit: x87 simulation to replace the
 * virtual with real x87 instructions, creating a block schedule and peephole
 * optimisations.
 */
static void ia32_finish_graph(ir_graph *irg)
{
	ia32_irg_data_t   *irg_data     = ia32_get_irg_data(irg);
	be_stack_layout_t *stack_layout = be_get_irg_stack_layout(irg);
	bool               at_begin     = stack_layout->sp_relative ? true : false;
	be_fec_env_t      *fec_env      = be_new_frame_entity_coalescer(irg);

	/* create and coalesce frame entities */
	irg_walk_graph(irg, NULL, ia32_collect_frame_entity_nodes, fec_env);
	be_assign_entities(fec_env, ia32_set_frame_entity, at_begin);
	be_free_frame_entity_coalescer(fec_env);

	irg_block_walk_graph(irg, NULL, ia32_after_ra_walker, NULL);

	introduce_prolog_epilog(irg);

	/* fix stack entity offsets */
	be_abi_fix_stack_nodes(irg);
	be_abi_fix_stack_bias(irg);

	/* fix 2-address code constraints */
	ia32_finish_irg(irg);

	/* we might have to rewrite x87 virtual registers */
	if (irg_data->do_x87_sim) {
		ia32_x87_simulate_graph(irg);
	}

	/* do peephole optimisations */
	ia32_peephole_optimization(irg);

	be_remove_dead_nodes_from_schedule(irg);

	/* create block schedule, this also removes empty blocks which might
	 * produce critical edges */
	irg_data->blk_sched = be_create_block_schedule(irg);
}

/**
 * Emits the code, closes the output file and frees
 * the code generator interface.
 */
static void ia32_emit(ir_graph *irg)
{
	if (ia32_cg_config.emit_machcode) {
		ia32_gen_binary_routine(irg);
	} else {
		ia32_gen_routine(irg);
	}
}

/**
 * Returns the node representing the PIC base.
 */
static ir_node *ia32_get_pic_base(ir_graph *irg)
{
	ia32_irg_data_t *irg_data = ia32_get_irg_data(irg);
	ir_node         *block;
	ir_node         *get_eip = irg_data->get_eip;
	if (get_eip != NULL)
		return get_eip;

	block             = get_irg_start_block(irg);
	get_eip           = new_bd_ia32_GetEIP(NULL, block);
	irg_data->get_eip = get_eip;

	return get_eip;
}

/**
 * Initializes a IA32 code generator.
 */
static void ia32_init_graph(ir_graph *irg)
{
	struct obstack  *obst     = be_get_be_obst(irg);
	ia32_irg_data_t *irg_data = OALLOCZ(obst, ia32_irg_data_t);

	irg_data->dump = (be_options.dump_flags & DUMP_BE) ? 1 : 0;

	if (gprof) {
		/* Linux gprof implementation needs base pointer */
		be_options.omit_fp = 0;
	}

	be_birg_from_irg(irg)->isa_link = irg_data;
}

static const tarval_mode_info mo_integer = {
	TVO_HEX,
	"0x",
	NULL,
};

/*
 * set the tarval output mode of all integer modes to decimal
 */
static void set_tarval_output_modes(void)
{
	size_t i;

	for (i = ir_get_n_modes(); i > 0;) {
		ir_mode *mode = ir_get_mode(--i);

		if (mode_is_int(mode))
			set_tarval_mode_output_option(mode, &mo_integer);
	}
}

extern const arch_isa_if_t ia32_isa_if;

static void init_asm_constraints(void)
{
	be_init_default_asm_constraint_flags();

	asm_constraint_flags['a'] = ASM_CONSTRAINT_FLAG_SUPPORTS_REGISTER;
	asm_constraint_flags['b'] = ASM_CONSTRAINT_FLAG_SUPPORTS_REGISTER;
	asm_constraint_flags['c'] = ASM_CONSTRAINT_FLAG_SUPPORTS_REGISTER;
	asm_constraint_flags['d'] = ASM_CONSTRAINT_FLAG_SUPPORTS_REGISTER;
	asm_constraint_flags['D'] = ASM_CONSTRAINT_FLAG_SUPPORTS_REGISTER;
	asm_constraint_flags['S'] = ASM_CONSTRAINT_FLAG_SUPPORTS_REGISTER;
	asm_constraint_flags['Q'] = ASM_CONSTRAINT_FLAG_SUPPORTS_REGISTER;
	asm_constraint_flags['q'] = ASM_CONSTRAINT_FLAG_SUPPORTS_REGISTER;
	asm_constraint_flags['A'] = ASM_CONSTRAINT_FLAG_SUPPORTS_REGISTER;
	asm_constraint_flags['l'] = ASM_CONSTRAINT_FLAG_SUPPORTS_REGISTER;
	asm_constraint_flags['R'] = ASM_CONSTRAINT_FLAG_SUPPORTS_REGISTER;
	asm_constraint_flags['r'] = ASM_CONSTRAINT_FLAG_SUPPORTS_REGISTER;
	asm_constraint_flags['p'] = ASM_CONSTRAINT_FLAG_SUPPORTS_REGISTER;
	asm_constraint_flags['f'] = ASM_CONSTRAINT_FLAG_SUPPORTS_REGISTER;
	asm_constraint_flags['t'] = ASM_CONSTRAINT_FLAG_SUPPORTS_REGISTER;
	asm_constraint_flags['u'] = ASM_CONSTRAINT_FLAG_SUPPORTS_REGISTER;
	asm_constraint_flags['Y'] = ASM_CONSTRAINT_FLAG_SUPPORTS_REGISTER;
	asm_constraint_flags['X'] = ASM_CONSTRAINT_FLAG_SUPPORTS_REGISTER;
	asm_constraint_flags['n'] = ASM_CONSTRAINT_FLAG_SUPPORTS_IMMEDIATE;
	asm_constraint_flags['g'] = ASM_CONSTRAINT_FLAG_SUPPORTS_IMMEDIATE;

	/* no support for autodecrement/autoincrement */
	asm_constraint_flags['<'] = ASM_CONSTRAINT_FLAG_NO_SUPPORT;
	asm_constraint_flags['>'] = ASM_CONSTRAINT_FLAG_NO_SUPPORT;
	/* no float consts */
	asm_constraint_flags['E'] = ASM_CONSTRAINT_FLAG_NO_SUPPORT;
	asm_constraint_flags['F'] = ASM_CONSTRAINT_FLAG_NO_SUPPORT;
	/* makes no sense on x86 */
	asm_constraint_flags['s'] = ASM_CONSTRAINT_FLAG_NO_SUPPORT;
	/* no support for sse consts yet */
	asm_constraint_flags['C'] = ASM_CONSTRAINT_FLAG_NO_SUPPORT;
	/* no support for x87 consts yet */
	asm_constraint_flags['G'] = ASM_CONSTRAINT_FLAG_NO_SUPPORT;
	/* no support for mmx registers yet */
	asm_constraint_flags['y'] = ASM_CONSTRAINT_FLAG_NO_SUPPORT;
	/* not available in 32bit mode */
	asm_constraint_flags['Z'] = ASM_CONSTRAINT_FLAG_NO_SUPPORT;
	asm_constraint_flags['e'] = ASM_CONSTRAINT_FLAG_NO_SUPPORT;

	/* no code yet to determine register class needed... */
	asm_constraint_flags['X'] = ASM_CONSTRAINT_FLAG_NO_SUPPORT;
}

/**
 * Check if Mux(sel, mux_true, mux_false) would represent a Max or Min operation
 */
static bool mux_is_float_min_max(ir_node *sel, ir_node *mux_true,
                                 ir_node *mux_false)
{
	ir_node    *cmp_l;
	ir_node    *cmp_r;
	ir_relation relation;

	if (!is_Cmp(sel))
		return false;

	cmp_l = get_Cmp_left(sel);
	cmp_r = get_Cmp_right(sel);
	if (!mode_is_float(get_irn_mode(cmp_l)))
		return false;

	/* check for min/max. They're defined as (C-Semantik):
	 *  min(a, b) = a < b ? a : b
	 *  or min(a, b) = a <= b ? a : b
	 *  max(a, b) = a > b ? a : b
	 *  or max(a, b) = a >= b ? a : b
	 * (Note we only handle float min/max here)
	 */
	relation = get_Cmp_relation(sel);
	switch (relation) {
	case ir_relation_greater_equal:
	case ir_relation_greater:
		/* this is a max */
		if (cmp_l == mux_true && cmp_r == mux_false)
			return true;
		break;
	case ir_relation_less_equal:
	case ir_relation_less:
		/* this is a min */
		if (cmp_l == mux_true && cmp_r == mux_false)
			return true;
		break;
	case ir_relation_unordered_greater_equal:
	case ir_relation_unordered_greater:
		/* this is a min */
		if (cmp_l == mux_false && cmp_r == mux_true)
			return true;
		break;
	case ir_relation_unordered_less_equal:
	case ir_relation_unordered_less:
		/* this is a max */
		if (cmp_l == mux_false && cmp_r == mux_true)
			return true;
		break;

	default:
		break;
	}

	return false;
}

static bool mux_is_set(ir_node *sel, ir_node *mux_true, ir_node *mux_false)
{
	ir_mode *mode = get_irn_mode(mux_true);
	(void) sel;

	if (!mode_is_int(mode) && !mode_is_reference(mode)
			&& mode != mode_b)
		return false;

	if (is_Const(mux_true) && is_Const(mux_false)) {
		/* we can create a set plus up two 3 instructions for any combination
		 * of constants */
		return true;
	}

	return false;
}

static bool mux_is_float_const_const(ir_node *sel, ir_node *mux_true,
                                     ir_node *mux_false)
{
	(void) sel;

	if (!mode_is_float(get_irn_mode(mux_true)))
		return false;

	return is_Const(mux_true) && is_Const(mux_false);
}

static bool mux_is_doz(ir_node *sel, ir_node *mux_true, ir_node *mux_false)
{
	ir_node    *cmp_left;
	ir_node    *cmp_right;
	ir_node    *sub_left;
	ir_node    *sub_right;
	ir_mode    *mode;
	ir_relation relation;

	if (!is_Cmp(sel))
		return false;

	mode = get_irn_mode(mux_true);
	if (mode_is_signed(mode) || mode_is_float(mode))
		return false;

	relation  = get_Cmp_relation(sel);
	cmp_left  = get_Cmp_left(sel);
	cmp_right = get_Cmp_right(sel);

	/* "move" zero constant to false input */
	if (is_Const(mux_true) && is_Const_null(mux_true)) {
		ir_node *tmp = mux_false;
		mux_false = mux_true;
		mux_true  = tmp;
		relation = get_negated_relation(relation);
	}
	if (!is_Const(mux_false) || !is_Const_null(mux_false))
		return false;
	if (!is_Sub(mux_true))
		return false;
	sub_left  = get_Sub_left(mux_true);
	sub_right = get_Sub_right(mux_true);

	/* Mux(a >=u b, 0, a-b) */
	if ((relation & ir_relation_greater)
			&& sub_left == cmp_left && sub_right == cmp_right)
		return true;
	/* Mux(a <=u b, 0, b-a) */
	if ((relation & ir_relation_less)
			&& sub_left == cmp_right && sub_right == cmp_left)
		return true;

	return false;
}

static int ia32_is_mux_allowed(ir_node *sel, ir_node *mux_false,
                               ir_node *mux_true)
{
	ir_mode *mode;

	/* middleend can handle some things */
	if (ir_is_optimizable_mux(sel, mux_false, mux_true))
		return true;
	/* we can handle Set for all modes and compares */
	if (mux_is_set(sel, mux_true, mux_false))
		return true;
	/* SSE has own min/max operations */
	if (ia32_cg_config.use_sse2
			&& mux_is_float_min_max(sel, mux_true, mux_false))
		return true;
	/* we can handle Mux(?, Const[f], Const[f]) */
	if (mux_is_float_const_const(sel, mux_true, mux_false)) {
#ifdef FIRM_GRGEN_BE
		/* well, some code selectors can't handle it */
		if (be_transformer != TRANSFORMER_PBQP
				|| be_transformer != TRANSFORMER_RAND)
			return true;
#else
		return true;
#endif
	}

	/* no support for 64bit inputs to cmov */
	mode = get_irn_mode(mux_true);
	if (get_mode_size_bits(mode) > 32)
		return false;
	/* we can handle Abs for all modes and compares (except 64bit) */
	if (ir_mux_is_abs(sel, mux_false, mux_true) != 0)
		return true;
	/* we can't handle MuxF yet */
	if (mode_is_float(mode))
		return false;

	if (mux_is_doz(sel, mux_true, mux_false))
		return true;

	/* Check Cmp before the node */
	if (is_Cmp(sel)) {
		ir_mode *cmp_mode = get_irn_mode(get_Cmp_left(sel));

		/* we can't handle 64bit compares */
		if (get_mode_size_bits(cmp_mode) > 32)
			return false;

		/* we can't handle float compares */
		if (mode_is_float(cmp_mode))
			return false;
	}

	/* did we disable cmov generation? */
	if (!ia32_cg_config.use_cmov)
		return false;

	/* we can use a cmov */
	return true;
}

/**
 * Create the trampoline code.
 */
static ir_node *ia32_create_trampoline_fkt(ir_node *block, ir_node *mem, ir_node *trampoline, ir_node *env, ir_node *callee)
{
	ir_graph *const irg  = get_irn_irg(block);
	ir_node  *      p    = trampoline;
	ir_mode  *const mode = get_irn_mode(p);
	ir_node  *const one  = new_r_Const(irg, get_mode_one(mode_Iu));
	ir_node  *const four = new_r_Const_long(irg, mode_Iu, 4);
	ir_node  *      st;

	/* mov  ecx,<env> */
	st  = new_r_Store(block, mem, p, new_r_Const_long(irg, mode_Bu, 0xb9), cons_none);
	mem = new_r_Proj(st, mode_M, pn_Store_M);
	p   = new_r_Add(block, p, one, mode);
	st  = new_r_Store(block, mem, p, env, cons_none);
	mem = new_r_Proj(st, mode_M, pn_Store_M);
	p   = new_r_Add(block, p, four, mode);
	/* jmp  <callee> */
	st  = new_r_Store(block, mem, p, new_r_Const_long(irg, mode_Bu, 0xe9), cons_none);
	mem = new_r_Proj(st, mode_M, pn_Store_M);
	p   = new_r_Add(block, p, one, mode);
	st  = new_r_Store(block, mem, p, callee, cons_none);
	mem = new_r_Proj(st, mode_M, pn_Store_M);

	return mem;
}

static const ir_settings_arch_dep_t ia32_arch_dep = {
	1,                   /* also use subs */
	4,                   /* maximum shifts */
	63,                  /* maximum shift amount */
	ia32_evaluate_insn,  /* evaluate the instruction sequence */

	1,  /* allow Mulhs */
	1,  /* allow Mulus */
	32, /* Mulh allowed up to 32 bit */
};
static backend_params ia32_backend_params = {
	1,     /* support inline assembly */
	1,     /* support Rotl nodes */
	0,     /* little endian */
	1,     /* modulo shift efficient */
	0,     /* non-modulo shift not efficient */
	&ia32_arch_dep, /* will be set later */
	ia32_is_mux_allowed,
	32,    /* machine_size */
	NULL,  /* float arithmetic mode, will be set below */
	NULL,  /* long long type */
	NULL,  /* unsigned long long type */
	NULL,  /* long double type */
	12,    /* size of trampoline code */
	4,     /* alignment of trampoline code */
	ia32_create_trampoline_fkt,
	4      /* alignment of stack parameter */
};

/**
 * Initializes the backend ISA.
 */
static void ia32_init(void)
{
	ir_mode *mode_long_long;
	ir_mode *mode_unsigned_long_long;
	ir_type *type_long_long;
	ir_type *type_unsigned_long_long;

	ia32_setup_cg_config();

	init_asm_constraints();

	ia32_mode_fpcw = new_int_mode("Fpcw", irma_twos_complement, 16, 0, 0);

	/* note mantissa is 64bit but with explicitely encoded 1 so the really
	 * usable part as counted by firm is only 63 bits */
	ia32_mode_E = new_float_mode("E", irma_x86_extended_float, 15, 63);
	ia32_type_E = new_type_primitive(ia32_mode_E);
	set_type_size_bytes(ia32_type_E, 12);
	set_type_alignment_bytes(ia32_type_E, 4);

	mode_long_long = new_int_mode("long long", irma_twos_complement, 64, 1, 64);
	type_long_long = new_type_primitive(mode_long_long);
	mode_unsigned_long_long
		= new_int_mode("unsigned long long", irma_twos_complement, 64, 0, 64);
	type_unsigned_long_long = new_type_primitive(mode_unsigned_long_long);

	ia32_backend_params.type_long_long          = type_long_long;
	ia32_backend_params.type_unsigned_long_long = type_unsigned_long_long;

	if (ia32_cg_config.use_sse2 || ia32_cg_config.use_softfloat) {
		ia32_backend_params.mode_float_arithmetic = NULL;
		ia32_backend_params.type_long_double = NULL;
	} else {
		ia32_backend_params.mode_float_arithmetic = ia32_mode_E;
		ia32_backend_params.type_long_double      = ia32_type_E;
	}

	ia32_register_init();
	obstack_init(&opcodes_obst);
	ia32_create_opcodes(&ia32_irn_ops);
}

static void ia32_finish(void)
{
	if (between_type != NULL) {
		free_type(between_type);
		between_type = NULL;
	}
	ia32_free_opcodes();
	obstack_free(&opcodes_obst, NULL);
}

/**
 * The template that generates a new ISA object.
 * Note that this template can be changed by command line
 * arguments.
 */
static ia32_isa_t ia32_isa_template = {
	{
		&ia32_isa_if,             /* isa interface implementation */
		N_IA32_REGISTERS,
		ia32_registers,
		N_IA32_CLASSES,
		ia32_reg_classes,
		&ia32_registers[REG_ESP], /* stack pointer register */
		&ia32_registers[REG_EBP], /* base pointer register */
		2,                        /* power of two stack alignment, 2^2 == 4 */
		7,                        /* costs for a spill instruction */
		5,                        /* costs for a reload instruction */
		false,                    /* no custom abi handling */
	},
	NULL,                       /* tv_ents */
	IA32_FPU_ARCH_X87,          /* FPU architecture */
};

static arch_env_t *ia32_begin_codegeneration(void)
{
	ia32_isa_t *isa = XMALLOC(ia32_isa_t);

	set_tarval_output_modes();

	*isa        = ia32_isa_template;
	isa->tv_ent = pmap_create();

	return &isa->base;
}

/**
 * Closes the output file and frees the ISA structure.
 */
static void ia32_end_codegeneration(void *self)
{
	ia32_isa_t *isa = (ia32_isa_t*)self;
	pmap_destroy(isa->tv_ent);
	free(self);
}

/**
 * Returns the register for parameter nr.
 */
static const arch_register_t *ia32_get_RegParam_reg(unsigned cc, unsigned nr,
                                                    const ir_mode *mode)
{
	static const arch_register_t *gpreg_param_reg_fastcall[] = {
		&ia32_registers[REG_ECX],
		&ia32_registers[REG_EDX],
		NULL
	};
	static const unsigned MAXNUM_GPREG_ARGS = 3;

	static const arch_register_t *gpreg_param_reg_regparam[] = {
		&ia32_registers[REG_EAX],
		&ia32_registers[REG_EDX],
		&ia32_registers[REG_ECX]
	};

	static const arch_register_t *gpreg_param_reg_this[] = {
		&ia32_registers[REG_ECX],
		NULL,
		NULL
	};

	static const arch_register_t *fpreg_sse_param_reg_std[] = {
		&ia32_registers[REG_XMM0],
		&ia32_registers[REG_XMM1],
		&ia32_registers[REG_XMM2],
		&ia32_registers[REG_XMM3],
		&ia32_registers[REG_XMM4],
		&ia32_registers[REG_XMM5],
		&ia32_registers[REG_XMM6],
		&ia32_registers[REG_XMM7]
	};

	static const arch_register_t *fpreg_sse_param_reg_this[] = {
		NULL,  /* in case of a "this" pointer, the first parameter must not be a float */
	};
	static const unsigned MAXNUM_SSE_ARGS = 8;

	if ((cc & cc_this_call) && nr == 0)
		return gpreg_param_reg_this[0];

	if (! (cc & cc_reg_param))
		return NULL;

	if (mode_is_float(mode)) {
		if (!ia32_cg_config.use_sse2 || (cc & cc_fpreg_param) == 0)
			return NULL;
		if (nr >= MAXNUM_SSE_ARGS)
			return NULL;

		if (cc & cc_this_call) {
			return fpreg_sse_param_reg_this[nr];
		}
		return fpreg_sse_param_reg_std[nr];
	} else if (mode_is_int(mode) || mode_is_reference(mode)) {
		unsigned num_regparam;

		if (get_mode_size_bits(mode) > 32)
			return NULL;

		if (nr >= MAXNUM_GPREG_ARGS)
			return NULL;

		if (cc & cc_this_call) {
			return gpreg_param_reg_this[nr];
		}
		num_regparam = cc & ~cc_bits;
		if (num_regparam == 0) {
			/* default fastcall */
			return gpreg_param_reg_fastcall[nr];
		}
		if (nr < num_regparam)
			return gpreg_param_reg_regparam[nr];
		return NULL;
	}

	panic("unknown argument mode");
}

/**
 * Get the ABI restrictions for procedure calls.
 */
static void ia32_get_call_abi(ir_type *method_type, be_abi_call_t *abi)
{
	unsigned  cc;
	int       n, i, regnum;
	int                 pop_amount = 0;
	be_abi_call_flags_t call_flags = be_abi_call_get_flags(abi);

	/* set abi flags for calls */
	/* call_flags.try_omit_fp                 not changed: can handle both settings */
	call_flags.call_has_imm = false;  /* No call immediate, we handle this by ourselves */

	/* set parameter passing style */
	be_abi_call_set_flags(abi, call_flags, &ia32_abi_callbacks);

	cc = get_method_calling_convention(method_type);
	if (get_method_variadicity(method_type) == variadicity_variadic) {
		/* pass all parameters of a variadic function on the stack */
		cc = cc_cdecl_set | (cc & cc_this_call);
	} else {
		if (get_method_additional_properties(method_type) & mtp_property_private &&
		    ia32_cg_config.optimize_cc) {
			/* set the fast calling conventions (allowing up to 3) */
			cc = SET_FASTCALL(cc) | 3;
		}
	}

	/* we have to pop the shadow parameter ourself for compound calls */
	if ( (get_method_calling_convention(method_type) & cc_compound_ret)
			&& !(cc & cc_reg_param)) {
		pop_amount += get_mode_size_bytes(mode_P_data);
	}

	n = get_method_n_params(method_type);
	for (i = regnum = 0; i < n; i++) {
		const arch_register_t *reg  = NULL;
		ir_type               *tp   = get_method_param_type(method_type, i);
		ir_mode               *mode = get_type_mode(tp);

		if (mode != NULL) {
			reg  = ia32_get_RegParam_reg(cc, regnum, mode);
		}
		if (reg != NULL) {
			be_abi_call_param_reg(abi, i, reg, ABI_CONTEXT_BOTH);
			++regnum;
		} else {
			/* Micro optimisation: if the mode is shorter than 4 bytes, load 4 bytes.
			 * movl has a shorter opcode than mov[sz][bw]l */
			ir_mode *load_mode = mode;

			if (mode != NULL) {
				unsigned size = get_mode_size_bytes(mode);

				if (cc & cc_callee_clear_stk) {
					pop_amount += (size + 3U) & ~3U;
				}

				if (size < 4) load_mode = mode_Iu;
			}

			be_abi_call_param_stack(abi, i, load_mode, 4, 0, 0, ABI_CONTEXT_BOTH);
		}
	}

	be_abi_call_set_pop(abi, pop_amount);

	/* set return registers */
	n = get_method_n_ress(method_type);

	assert(n <= 2 && "more than two results not supported");

	/* In case of 64bit returns, we will have two 32bit values */
	if (n == 2) {
		ir_type *tp   = get_method_res_type(method_type, 0);
		ir_mode *mode = get_type_mode(tp);

		assert(!mode_is_float(mode) && "two FP results not supported");

		tp   = get_method_res_type(method_type, 1);
		mode = get_type_mode(tp);

		assert(!mode_is_float(mode) && "mixed INT, FP results not supported");

		be_abi_call_res_reg(abi, 0, &ia32_registers[REG_EAX], ABI_CONTEXT_BOTH);
		be_abi_call_res_reg(abi, 1, &ia32_registers[REG_EDX], ABI_CONTEXT_BOTH);
	}
	else if (n == 1) {
		ir_type *tp   = get_method_res_type(method_type, 0);
		ir_mode *mode = get_type_mode(tp);
		const arch_register_t *reg;
		assert(is_atomic_type(tp));

		reg = mode_is_float(mode) ? &ia32_registers[REG_ST0] : &ia32_registers[REG_EAX];

		be_abi_call_res_reg(abi, 0, reg, ABI_CONTEXT_BOTH);
	}
}

static void ia32_mark_remat(ir_node *node)
{
	if (is_ia32_irn(node)) {
		set_ia32_is_remat(node);
	}
}

static asm_constraint_flags_t ia32_parse_asm_constraint(const char **c)
{
	(void) c;

	/* we already added all our simple flags to the flags modifier list in
	 * init, so this flag we don't know. */
	return ASM_CONSTRAINT_FLAG_INVALID;
}

static int ia32_is_valid_clobber(const char *clobber)
{
	return ia32_get_clobber_register(clobber) != NULL;
}

static void ia32_lower_for_target(void)
{
	ir_mode *mode_gp = ia32_reg_classes[CLASS_ia32_gp].mode;
	size_t i, n_irgs = get_irp_n_irgs();

	/* perform doubleword lowering */
	lwrdw_param_t lower_dw_params = {
		1,  /* little endian */
		64, /* doubleword size */
		ia32_create_intrinsic_fkt,
		&intrinsic_env,
	};

	/* lower compound param handling
	 * Note: we lower compound arguments ourself, since on ia32 we don't
	 * have hidden parameters but know where to find the structs on the stack.
	 * (This also forces us to always allocate space for the compound arguments
	 *  on the callframe and we can't just use an arbitrary position on the
	 *  stackframe)
	 */
	lower_calls_with_compounds(LF_RETURN_HIDDEN | LF_DONT_LOWER_ARGUMENTS);

	/* replace floating point operations by function calls */
	if (ia32_cg_config.use_softfloat) {
		lower_floating_point();
	}

	for (i = 0; i < n_irgs; ++i) {
		ir_graph *irg = get_irp_irg(i);
		/* break up switches with wide ranges */
		lower_switch(irg, 4, 256, mode_gp);
	}

	ir_prepare_dw_lowering(&lower_dw_params);
	ir_lower_dw_ops();

	for (i = 0; i < n_irgs; ++i) {
		ir_graph *irg = get_irp_irg(i);
		/* lower for mode_b stuff */
		ir_lower_mode_b(irg, mode_Iu);
	}

	for (i = 0; i < n_irgs; ++i) {
		ir_graph *irg = get_irp_irg(i);
		/* Turn all small CopyBs into loads/stores, keep medium-sized CopyBs,
		 * so we can generate rep movs later, and turn all big CopyBs into
		 * memcpy calls. */
		lower_CopyB(irg, 64, 8193, true);
	}
}

/**
 * Returns the libFirm configuration parameter for this backend.
 */
static const backend_params *ia32_get_libfirm_params(void)
{
	return &ia32_backend_params;
}

/**
 * Check if the given register is callee or caller save.
 */
static int ia32_register_saved_by(const arch_register_t *reg, int callee)
{
	switch (reg->global_index) {
	case REG_EBX:
	case REG_EBP:
	case REG_ESI:
	case REG_EDI:
	case REG_FPCW:
		return callee;

	case REG_EAX:
	case REG_ECX:
	case REG_EDX:
	case REG_ST0:
	case REG_ST1:
	case REG_ST2:
	case REG_ST3:
	case REG_ST4:
	case REG_ST5:
	case REG_ST6:
	case REG_ST7:
	case REG_XMM0:
	case REG_XMM1:
	case REG_XMM2:
	case REG_XMM3:
	case REG_XMM4:
	case REG_XMM5:
	case REG_XMM6:
	case REG_XMM7:
		return !callee;

	default:
		return 0;
	}
}

static const lc_opt_enum_int_items_t gas_items[] = {
	{ "elf",   OBJECT_FILE_FORMAT_ELF    },
	{ "mingw", OBJECT_FILE_FORMAT_COFF   },
	{ "macho", OBJECT_FILE_FORMAT_MACH_O },
	{ NULL,    0 }
};

static lc_opt_enum_int_var_t gas_var = {
	(int*) &be_gas_object_file_format, gas_items
};

#ifdef FIRM_GRGEN_BE
static const lc_opt_enum_int_items_t transformer_items[] = {
	{ "default", TRANSFORMER_DEFAULT },
	{ "pbqp",    TRANSFORMER_PBQP    },
	{ "random",  TRANSFORMER_RAND    },
	{ NULL,      0                   }
};

static lc_opt_enum_int_var_t transformer_var = {
	(int*)&be_transformer, transformer_items
};
#endif

static const lc_opt_table_entry_t ia32_options[] = {
	LC_OPT_ENT_ENUM_INT("gasmode", "set the GAS compatibility mode", &gas_var),
#ifdef FIRM_GRGEN_BE
	LC_OPT_ENT_ENUM_INT("transformer", "the transformer used for code selection", &transformer_var),
#endif
	LC_OPT_ENT_INT ("stackalign", "set power of two stack alignment for calls",
	                &ia32_isa_template.base.stack_alignment),
	LC_OPT_ENT_BOOL("gprof",      "create gprof profiling code",                                    &gprof),
	LC_OPT_ENT_BOOL("precise_float_spill", "Spill floatingpoint values precisely (the whole 80 bits)", &precise_x87_spills),
	LC_OPT_LAST
};

const arch_isa_if_t ia32_isa_if = {
	ia32_init,
	ia32_finish,
	ia32_get_libfirm_params,
	ia32_lower_for_target,
	ia32_parse_asm_constraint,
	ia32_is_valid_clobber,

	ia32_begin_codegeneration,
	ia32_end_codegeneration,
	ia32_init_graph,
	ia32_get_call_abi,
	ia32_mark_remat,
	ia32_get_pic_base,   /* return node used as base in pic code addresses */
	ia32_new_spill,
	ia32_new_reload,
	ia32_register_saved_by,

	ia32_handle_intrinsics,
	ia32_before_abi,     /* before abi introduce hook */
	ia32_prepare_graph,
	ia32_before_ra,      /* before register allocation hook */
	ia32_finish_graph,   /* called before codegen */
	ia32_emit,           /* emit && done */
};

BE_REGISTER_MODULE_CONSTRUCTOR(be_init_arch_ia32)
void be_init_arch_ia32(void)
{
	lc_opt_entry_t *be_grp   = lc_opt_get_grp(firm_opt_get_root(), "be");
	lc_opt_entry_t *ia32_grp = lc_opt_get_grp(be_grp, "ia32");

	lc_opt_add_table(ia32_grp, ia32_options);
	be_register_isa_if("ia32", &ia32_isa_if);

	ia32_init_emitter();
	ia32_init_finish();
	ia32_init_optimize();
	ia32_init_transform();
	ia32_init_x87();
	ia32_init_architecture();
}
