/*
 * This file is part of libFirm.
 * Copyright (C) 2012 University of Karlsruhe.
 */

/**
 * @file
 * @brief   Various irnode constructors. Automatic construction of SSA
 *          representation.
 * @author  Martin Trapp, Christian Schaefer, Goetz Lindenmaier, Boris Boesler
 *          Michael Beck, Matthias Braun
 */
#include "irprog_t.h"
#include "irgraph_t.h"
#include "irnode_t.h"
#include "irmode_t.h"
#include "ircons_t.h"
#include "irverify.h"
#include "irop_t.h"
#include "iropt_t.h"
#include "irgmod.h"
#include "irhooks.h"
#include "array_t.h"
#include "irbackedge_t.h"
#include "irflag_t.h"
#include "iredges_t.h"
#include "irflag_t.h"
#include "error.h"
#include "irgopt.h"

/**
 * Language dependent variable initialization callback.
 */
static uninitialized_local_variable_func_t *default_initialize_local_variable = NULL;

ir_node *new_rd_Const_long(dbg_info *db, ir_graph *irg, ir_mode *mode,
                           long value)
{
	return new_rd_Const(db, irg, new_tarval_from_long(value, mode));
}

ir_node *new_rd_ASM(dbg_info *db, ir_node *block, ir_node *mem,
                    int arity, ir_node *in[], ir_asm_constraint *inputs,
                    size_t n_outs, ir_asm_constraint *outputs, size_t n_clobber,
	                ident *clobber[], ident *text)
{
	ir_graph *irg = get_irn_irg(block);

	int r_arity = arity+1;
	ir_node **r_in;
	NEW_ARR_A(ir_node*, r_in, r_arity);
	r_in[0] = mem;
	memcpy(&r_in[1], in, arity*sizeof(ir_node*));

	ir_node *res = new_ir_node(db, irg, block, op_ASM, mode_T, r_arity, r_in);

	struct obstack *const obst = get_irg_obstack(irg);
	res->attr.assem.pin_state          = op_pin_state_pinned;
	res->attr.assem.input_constraints  = NEW_ARR_D(ir_asm_constraint, obst, arity);
	res->attr.assem.output_constraints = NEW_ARR_D(ir_asm_constraint, obst, n_outs);
	res->attr.assem.clobbers           = NEW_ARR_D(ident*,            obst, n_clobber);
	res->attr.assem.text               = text;

	memcpy(res->attr.assem.input_constraints,  inputs,  sizeof(inputs[0]) * arity);
	memcpy(res->attr.assem.output_constraints, outputs, sizeof(outputs[0]) * n_outs);
	memcpy(res->attr.assem.clobbers, clobber, sizeof(clobber[0]) * n_clobber);

	verify_new_node(irg, res);
	res = optimize_node(res);
	return res;
}

ir_node *new_rd_simpleSel(dbg_info *db, ir_node *block, ir_node *store,
                          ir_node *objptr, ir_entity *ent)
{
	return new_rd_Sel(db, block, store, objptr, 0, NULL, ent);
}

ir_node *new_rd_SymConst(dbg_info *db, ir_graph *irg, ir_mode *mode,
                         symconst_symbol value, symconst_kind symkind)
{
	ir_node *block = get_irg_start_block(irg);
	ir_node *res   = new_ir_node(db, irg, block, op_SymConst, mode, 0, NULL);
	res->attr.symc.kind = symkind;
	res->attr.symc.sym  = value;

	verify_new_node(irg, res);
	res = optimize_node(res);
	return res;
}

ir_node *new_rd_SymConst_addr_ent(dbg_info *db, ir_graph *irg, ir_mode *mode, ir_entity *symbol)
{
	symconst_symbol sym;
	sym.entity_p = symbol;
	return new_rd_SymConst(db, irg, mode, sym, symconst_addr_ent);
}

ir_node *new_rd_SymConst_ofs_ent(dbg_info *db, ir_graph *irg, ir_mode *mode, ir_entity *symbol)
{
	symconst_symbol sym;
	sym.entity_p = symbol;
	return new_rd_SymConst(db, irg, mode, sym, symconst_ofs_ent);
}

ir_node *new_rd_SymConst_size(dbg_info *db, ir_graph *irg, ir_mode *mode, ir_type *symbol)
{
	symconst_symbol sym;
	sym.type_p = symbol;
	return new_rd_SymConst(db, irg, mode, sym, symconst_type_size);
}

ir_node *new_rd_SymConst_align(dbg_info *db, ir_graph *irg, ir_mode *mode, ir_type *symbol)
{
	symconst_symbol sym;
	sym.type_p = symbol;
	return new_rd_SymConst(db, irg, mode, sym, symconst_type_align);
}

ir_node *new_r_Const_long(ir_graph *irg, ir_mode *mode, long value)
{
	return new_rd_Const_long(NULL, irg, mode, value);
}
ir_node *new_r_SymConst(ir_graph *irg, ir_mode *mode, symconst_symbol value,
                        symconst_kind symkind)
{
	return new_rd_SymConst(NULL, irg, mode, value, symkind);
}
ir_node *new_r_simpleSel(ir_node *block, ir_node *store, ir_node *objptr,
                         ir_entity *ent)
{
	return new_rd_Sel(NULL, block, store, objptr, 0, NULL, ent);
}
ir_node *new_r_ASM(ir_node *block, ir_node *mem,
                   int arity, ir_node *in[], ir_asm_constraint *inputs,
                   size_t n_outs, ir_asm_constraint *outputs,
                   size_t n_clobber, ident *clobber[], ident *text)
{
	return new_rd_ASM(NULL, block, mem, arity, in, inputs, n_outs, outputs, n_clobber, clobber, text);
}

/** Creates a Phi node with 0 predecessors. */
static inline ir_node *new_rd_Phi0(dbg_info *dbgi, ir_node *block,
                                   ir_mode *mode, int pos)
{
	ir_graph *irg = get_irn_irg(block);
	ir_node  *res = new_ir_node(dbgi, irg, block, op_Phi, mode, 0, NULL);
	res->attr.phi.u.pos = pos;
	verify_new_node(irg, res);
	return res;
}

static ir_node *get_r_value_internal(ir_node *block, int pos, ir_mode *mode);

static void try_remove_unnecessary_phi(ir_node *phi)
{
	ir_node *phi_value = NULL;
	int      arity     = get_irn_arity(phi);
	int      i;

	/* see if all inputs are either pointing to a single value or
	 * are self references */
	for (i = 0; i < arity; ++i) {
		ir_node *in = get_irn_n(phi, i);
		if (in == phi)
			continue;
		if (in == phi_value)
			continue;
		/** found a different value from the one we already found, can't remove
		 * the phi (yet) */
		if (phi_value != NULL)
			return;
		phi_value = in;
	}
	if (phi_value == NULL)
		return;

	/* if we're here then all phi inputs have been either phi_value
	 * or self-references, we can replace the phi by phi_value.
	 * We do this with an Id-node */
	exchange(phi, phi_value);

	/* recursively check phi_value, because it could be that we were the last
	 * phi-node in a loop-body. Then our arguments is an unnecessary phi in
	 * the loop header which can be eliminated now */
	if (is_Phi(phi_value)) {
		try_remove_unnecessary_phi(phi_value);
	}
}

/**
 * Computes the predecessors for the real phi node, and then
 * allocates and returns this node.  The routine called to allocate the
 * node might optimize it away and return a real value.
 * This function must be called with an in-array of proper size.
 */
static ir_node *set_phi_arguments(ir_node *phi, int pos)
{
	ir_node  *block        = get_nodes_block(phi);
	ir_graph *irg          = get_irn_irg(block);
	int       arity        = get_irn_arity(block);
	ir_node **in           = ALLOCAN(ir_node*, arity);
	ir_mode  *mode         = get_irn_mode(phi);
	int       i;

	/* This loop goes to all predecessor blocks of the block the Phi node
	   is in and there finds the operands of the Phi node by calling
	   get_r_value_internal.  */
	for (i = 0; i < arity; ++i) {
		ir_node *cfgpred = get_Block_cfgpred_block(block, i);
		ir_node *value;
		if (is_Bad(cfgpred)) {
			value = new_r_Bad(irg, mode);
		} else {
			value = get_r_value_internal(cfgpred, pos, mode);
		}
		in[i] = value;
	}

	phi->attr.phi.u.backedge = new_backedge_arr(get_irg_obstack(irg), arity);
	set_irn_in(phi, arity, in);

	verify_new_node(irg, phi);

	try_remove_unnecessary_phi(phi);
	return phi;
}

/**
 * This function returns the last definition of a value.  In case
 * this value was last defined in a previous block, Phi nodes are
 * inserted.  If the part of the firm graph containing the definition
 * is not yet constructed, a dummy Phi node is returned.
 *
 * @param block   the current block
 * @param pos     the value number of the value searched
 * @param mode    the mode of this value (needed for Phi construction)
 */
static ir_node *get_r_value_internal(ir_node *block, int pos, ir_mode *mode)
{
	ir_node  *res = block->attr.block.graph_arr[pos];
	ir_graph *irg = get_irn_irg(block);
	if (res != NULL)
		return res;

	/* in a matured block we can immediately determine the phi arguments */
	if (get_Block_matured(block)) {
		int arity = get_irn_arity(block);
		/* no predecessors: use unknown value */
		if (arity == 0) {
			if (block == get_irg_start_block(irg)) {
				if (default_initialize_local_variable != NULL) {
					ir_node *rem = get_r_cur_block(irg);
					set_r_cur_block(irg, block);
					res = default_initialize_local_variable(irg, mode, pos - 1);
					set_r_cur_block(irg, rem);
				} else {
					res = new_r_Unknown(irg, mode);
				}
			} else {
				/* unreachable block, use Bad */
				res = new_r_Bad(irg, mode);
			}
		/* one predecessor just use its value */
		} else if (arity == 1) {
			ir_node *cfgpred = get_Block_cfgpred(block, 0);
			if (is_Bad(cfgpred)) {
				res = new_r_Bad(irg, mode);
			} else {
				ir_node *cfgpred_block = get_nodes_block(cfgpred);
				res = get_r_value_internal(cfgpred_block, pos, mode);
			}
		/* multiple predecessors construct Phi */
		} else {
			res = new_rd_Phi0(NULL, block, mode, pos);
			/* enter phi0 into our variable value table to break cycles
			 * arising from set_phi_arguments */
			block->attr.block.graph_arr[pos] = res;
			res = set_phi_arguments(res, pos);
		}
	} else {
		/* in case of immature block we have to keep a Phi0 */
		res = new_rd_Phi0(NULL, block, mode, pos);
		/* enqueue phi so we can set arguments once the block matures */
		res->attr.phi.next     = block->attr.block.phis;
		block->attr.block.phis = res;
	}
	block->attr.block.graph_arr[pos] = res;
	return res;
}

void mature_immBlock(ir_node *block)
{
	size_t   n_preds;
	ir_node  *next;
	ir_node  *phi;
	ir_graph *irg;

	if (get_Block_matured(block))
		return;

	irg     = get_irn_irg(block);
	n_preds = ARR_LEN(block->in) - 1;
	/* Fix block parameters */
	block->attr.block.backedge = new_backedge_arr(get_irg_obstack(irg), n_preds);

	/* Traverse a chain of Phi nodes attached to this block and mature
	these, too. */
	for (phi = block->attr.block.phis; phi != NULL; phi = next) {
		ir_node *new_value;
		int      pos = phi->attr.phi.u.pos;

		next = phi->attr.phi.next;
		new_value = set_phi_arguments(phi, pos);
		if (block->attr.block.graph_arr[pos] == phi) {
			block->attr.block.graph_arr[pos] = new_value;
		}
	}

	set_Block_matured(block, 1);

	/* create final in-array for the block */
	if (block->attr.block.dynamic_ins) {
		ir_node **const new_in = DUP_ARR_D(ir_node*, get_irg_obstack(irg), block->in);
		DEL_ARR_F(block->in);
		block->in = new_in;
		block->attr.block.dynamic_ins = false;
	}

	/* Now, as the block is a finished Firm node, we can optimize it.
	   Since other nodes have been allocated since the block was created
	   we can not free the node on the obstack.  Therefore we have to call
	   optimize_in_place().
	   Unfortunately the optimization does not change a lot, as all allocated
	   nodes refer to the unoptimized node.
	   We can call optimize_in_place_2(), as global cse has no effect on blocks.
	 */
	verify_new_node(irg, block);
	optimize_in_place_2(block);
}

ir_node *new_d_Const_long(dbg_info *db, ir_mode *mode, long value)
{
	assert(irg_is_constrained(current_ir_graph, IR_GRAPH_CONSTRAINT_CONSTRUCTION));
	return new_rd_Const_long(db, current_ir_graph, mode, value);
}

ir_node *new_d_simpleSel(dbg_info *db, ir_node *store, ir_node *objptr,
                         ir_entity *ent)
{
	assert(irg_is_constrained(current_ir_graph, IR_GRAPH_CONSTRAINT_CONSTRUCTION));
	return new_rd_Sel(db, current_ir_graph->current_block,
	                  store, objptr, 0, NULL, ent);
}

ir_node *new_d_SymConst(dbg_info *db, ir_mode *mode, symconst_symbol value,
                        symconst_kind kind)
{
	assert(irg_is_constrained(current_ir_graph, IR_GRAPH_CONSTRAINT_CONSTRUCTION));
	return new_rd_SymConst(db, current_ir_graph, mode, value, kind);
}

ir_node *new_d_ASM(dbg_info *db, ir_node *mem, int arity, ir_node *in[],
                   ir_asm_constraint *inputs,
                   size_t n_outs, ir_asm_constraint *outputs,
                   size_t n_clobber, ident *clobber[], ident *text)
{
	assert(irg_is_constrained(current_ir_graph, IR_GRAPH_CONSTRAINT_CONSTRUCTION));
	return new_rd_ASM(db, current_ir_graph->current_block, mem, arity, in,
	                  inputs, n_outs, outputs, n_clobber, clobber, text);
}

ir_node *new_rd_DivRL(dbg_info *dbgi, ir_node *block, ir_node * irn_mem, ir_node * irn_left, ir_node * irn_right, ir_mode* resmode, op_pin_state pin_state)
{
	ir_node *res;
	ir_graph *irg = get_Block_irg(block);

	ir_node *in[3];
	in[0] = irn_mem;
	in[1] = irn_left;
	in[2] = irn_right;

	res = new_ir_node(dbgi, irg, block, op_Div, mode_T, 3, in);
	res->attr.div.resmode = resmode;
	res->attr.div.no_remainder = 1;
	res->attr.div.exc.pin_state = pin_state;
	verify_new_node(irg, res);
	res = optimize_node(res);
	return res;
}

ir_node *new_r_DivRL(ir_node *block, ir_node * irn_mem, ir_node * irn_left, ir_node * irn_right, ir_mode* resmode, op_pin_state pin_state)
{
	return new_rd_DivRL(NULL, block, irn_mem, irn_left, irn_right, resmode, pin_state);
}

ir_node *new_d_DivRL(dbg_info *dbgi, ir_node * irn_mem, ir_node * irn_left, ir_node * irn_right, ir_mode* resmode, op_pin_state pin_state)
{
	ir_node *res;
	assert(irg_is_constrained(current_ir_graph, IR_GRAPH_CONSTRAINT_CONSTRUCTION));
	res = new_rd_DivRL(dbgi, current_ir_graph->current_block, irn_mem, irn_left, irn_right, resmode, pin_state);
	return res;
}

ir_node *new_DivRL(ir_node * irn_mem, ir_node * irn_left, ir_node * irn_right, ir_mode* resmode, op_pin_state pin_state)
{
	return new_d_DivRL(NULL, irn_mem, irn_left, irn_right, resmode, pin_state);
}

ir_node *new_rd_immBlock(dbg_info *dbgi, ir_graph *irg)
{
	ir_node *res;

	assert(irg_is_constrained(irg, IR_GRAPH_CONSTRAINT_CONSTRUCTION));
	/* creates a new dynamic in-array as length of in is -1 */
	res = new_ir_node(dbgi, irg, NULL, op_Block, mode_BB, -1, NULL);

	set_Block_matured(res, 0);
	res->attr.block.dynamic_ins = true;
	res->attr.block.irg.irg     = irg;
	res->attr.block.backedge    = NULL;
	res->attr.block.entity      = NULL;

	set_Block_block_visited(res, 0);

	/* Create and initialize array for Phi-node construction. */
	res->attr.block.graph_arr = NEW_ARR_DZ(ir_node*, get_irg_obstack(irg), irg->n_loc);

	/* Immature block may not be optimized! */
	verify_new_node(irg, res);

	return res;
}

ir_node *new_r_immBlock(ir_graph *irg)
{
	return new_rd_immBlock(NULL, irg);
}

ir_node *new_d_immBlock(dbg_info *dbgi)
{
	return new_rd_immBlock(dbgi, current_ir_graph);
}

ir_node *new_immBlock(void)
{
	return new_rd_immBlock(NULL, current_ir_graph);
}

void add_immBlock_pred(ir_node *block, ir_node *jmp)
{
	int n = ARR_LEN(block->in) - 1;

	assert(is_Block(block) && "Error: Must be a Block");
	assert(!get_Block_matured(block) && "Error: Block already matured!\n");
	assert(is_ir_node(jmp));

	ARR_APP1(ir_node *, block->in, jmp);
	/* Call the hook */
	hook_set_irn_n(block, n, jmp, NULL);
}

void set_cur_block(ir_node *target)
{
	set_r_cur_block(current_ir_graph, target);
}

void set_r_cur_block(ir_graph *irg, ir_node *target)
{
	assert(irg_is_constrained(irg, IR_GRAPH_CONSTRAINT_CONSTRUCTION));
	assert(target == NULL || is_Block(target));
	assert(target == NULL || get_irn_irg(target) == irg);
	irg->current_block = target;
}

ir_node *get_r_cur_block(ir_graph *irg)
{
	assert(irg_is_constrained(irg, IR_GRAPH_CONSTRAINT_CONSTRUCTION));
	return irg->current_block;
}

ir_node *(get_cur_block)(void)
{
	return get_r_cur_block(current_ir_graph);
}

ir_node *get_r_value(ir_graph *irg, int pos, ir_mode *mode)
{
	assert(irg_is_constrained(irg, IR_GRAPH_CONSTRAINT_CONSTRUCTION));
	assert(pos >= 0);

	return get_r_value_internal(irg->current_block, pos + 1, mode);
}

ir_node *get_value(int pos, ir_mode *mode)
{
	return get_r_value(current_ir_graph, pos, mode);
}

/**
 * helper function for guess_mode: recursively look for a definition for
 * local variable @p pos, returns its mode if found.
 */
static ir_mode *guess_recursively(ir_node *block, int pos)
{
	ir_node *value;
	int      n_preds;
	int      i;

	if (irn_visited_else_mark(block))
		return NULL;

	/* already have a defintion -> we can simply look at its mode */
	value = block->attr.block.graph_arr[pos];
	if (value != NULL)
		return get_irn_mode(value);

	/* now we try to guess, by looking at the predecessor blocks */
	n_preds = get_irn_arity(block);
	for (i = 0; i < n_preds; ++i) {
		ir_node *pred_block = get_Block_cfgpred_block(block, i);
		ir_mode *mode       = guess_recursively(pred_block, pos);
		if (mode != NULL)
			return mode;
	}

	/* no way to guess */
	return NULL;
}

ir_mode *ir_r_guess_mode(ir_graph *irg, int pos)
{
	ir_node  *block = irg->current_block;
	ir_node  *value = block->attr.block.graph_arr[pos+1];
	ir_mode  *mode;

	/* already have a defintion -> we can simply look at its mode */
	if (value != NULL)
		return get_irn_mode(value);

	ir_reserve_resources(irg, IR_RESOURCE_IRN_VISITED);
	inc_irg_visited(irg);
	mode = guess_recursively(block, pos+1);
	ir_free_resources(irg, IR_RESOURCE_IRN_VISITED);

	return mode;
}

ir_mode *ir_guess_mode(int pos)
{
	return ir_r_guess_mode(current_ir_graph, pos);
}

void set_r_value(ir_graph *irg, int pos, ir_node *value)
{
	assert(irg_is_constrained(irg, IR_GRAPH_CONSTRAINT_CONSTRUCTION));
	assert(pos >= 0);
	assert(pos+1 < irg->n_loc);
	assert(is_ir_node(value));
	irg->current_block->attr.block.graph_arr[pos + 1] = value;
}

void set_value(int pos, ir_node *value)
{
	set_r_value(current_ir_graph, pos, value);
}

ir_node *get_r_store(ir_graph *irg)
{
	assert(irg_is_constrained(irg, IR_GRAPH_CONSTRAINT_CONSTRUCTION));
	return get_r_value_internal(irg->current_block, 0, mode_M);
}

ir_node *get_store(void)
{
	return get_r_store(current_ir_graph);
}

void set_r_store(ir_graph *irg, ir_node *store)
{
	ir_node *load, *pload, *pred, *in[2];

	assert(irg_is_constrained(irg, IR_GRAPH_CONSTRAINT_CONSTRUCTION));
	/* Beware: due to dead code elimination, a store might become a Bad node even in
	   the construction phase. */
	assert((get_irn_mode(store) == mode_M || is_Bad(store)) && "storing non-memory node");

	if (get_opt_auto_create_sync()) {
		/* handle non-volatile Load nodes by automatically creating Sync's */
		load = skip_Proj(store);
		if (is_Load(load) && get_Load_volatility(load) == volatility_non_volatile) {
			pred = get_Load_mem(load);

			if (is_Sync(pred)) {
				/* a Load after a Sync: move it up */
				ir_node *mem = skip_Proj(get_Sync_pred(pred, 0));

				set_Load_mem(load, get_memop_mem(mem));
				add_Sync_pred(pred, store);
				store = pred;
			} else {
				pload = skip_Proj(pred);
				if (is_Load(pload) && get_Load_volatility(pload) == volatility_non_volatile) {
					/* a Load after a Load: create a new Sync */
					set_Load_mem(load, get_Load_mem(pload));

					in[0] = pred;
					in[1] = store;
					store = new_r_Sync(irg->current_block, 2, in);
				}
			}
		}
	}
	irg->current_block->attr.block.graph_arr[0] = store;
}

void set_store(ir_node *store)
{
	set_r_store(current_ir_graph, store);
}

void keep_alive(ir_node *ka)
{
	ir_graph *irg = get_irn_irg(ka);
	add_End_keepalive(get_irg_end(irg), ka);
}

void ir_set_uninitialized_local_variable_func(
		uninitialized_local_variable_func_t *func)
{
	default_initialize_local_variable = func;
}

void irg_finalize_cons(ir_graph *irg)
{
	ir_node *end_block = get_irg_end_block(irg);
	mature_immBlock(end_block);

	clear_irg_constraints(irg, IR_GRAPH_CONSTRAINT_CONSTRUCTION);
}

void irp_finalize_cons(void)
{
	size_t i, n;
	for (i = 0, n = get_irp_n_irgs(); i < n; ++i) {
		irg_finalize_cons(get_irp_irg(i));
	}
}

ir_node *new_Const_long(ir_mode *mode, long value)
{
	return new_d_Const_long(NULL, mode, value);
}

ir_node *new_SymConst(ir_mode *mode, symconst_symbol value, symconst_kind kind)
{
	return new_d_SymConst(NULL, mode, value, kind);
}
ir_node *new_simpleSel(ir_node *store, ir_node *objptr, ir_entity *ent)
{
	return new_d_simpleSel(NULL, store, objptr, ent);
}
ir_node *new_ASM(ir_node *mem, int arity, ir_node *in[],
                 ir_asm_constraint *inputs, size_t n_outs,
                 ir_asm_constraint *outputs, size_t n_clobber,
                 ident *clobber[], ident *text)
{
	return new_d_ASM(NULL, mem, arity, in, inputs, n_outs, outputs, n_clobber, clobber, text);
}

ir_node *new_r_Anchor(ir_graph *irg)
{
	ir_node *in[anchor_last+1];
	ir_node *res;
	size_t   i;
	memset(in, 0, sizeof(in));
	res = new_ir_node(NULL, irg, NULL, op_Anchor, mode_ANY, 0, NULL);
	res->attr.anchor.irg.irg = irg;

	/* hack to get get_irn_irg in set_irn_in() working */
	res->in[0] = res;

	/* we can't have NULL inputs so reference ourselves for now */
	for (i = 0; i <= (size_t)anchor_last; ++i) {
		in[i] = res;
	}
	set_irn_in(res, anchor_last+1, in);

	return res;
}

ir_node *new_r_Block_noopt(ir_graph *irg, int arity, ir_node *in[])
{
	ir_node *res = new_ir_node(NULL, irg, NULL, op_Block, mode_BB, arity, in);
	res->attr.block.irg.irg = irg;
	res->attr.block.backedge = new_backedge_arr(get_irg_obstack(irg), arity);
	set_Block_matured(res, 1);
	/* Create and initialize array for Phi-node construction. */
	if (irg_is_constrained(irg, IR_GRAPH_CONSTRAINT_CONSTRUCTION)) {
		res->attr.block.graph_arr = NEW_ARR_DZ(ir_node*, get_irg_obstack(irg), irg->n_loc);
	}
	verify_new_node(irg, res);
	return res;
}
