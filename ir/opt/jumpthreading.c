/*
 * This file is part of libFirm.
 * Copyright (C) 2012 University of Karlsruhe.
 */

/**
 * @file
 * @brief   Path-Sensitive Jump Threading
 * @date    10. Sep. 2006
 * @author  Christoph Mallon, Matthias Braun
 */
#include "iroptimize.h"

#include <assert.h>
#include <stdbool.h>
#include "array_t.h"
#include "debug.h"
#include "ircons.h"
#include "irgmod.h"
#include "irgopt.h"
#include "irgwalk.h"
#include "irnode.h"
#include "irnode_t.h"
#include "iredges.h"
#include "iredges_t.h"
#include "irtools.h"
#include "tv.h"
#include "iroptimize.h"
#include "iropt_dbg.h"
#include "vrp.h"
#include "firmstat_t.h"

#undef AVOID_PHIB

DEBUG_ONLY(static firm_dbg_module_t *dbg;)

/**
 * Add the new predecessor x to node node, which is either a Block or a Phi
 */
static void add_pred(ir_node* node, ir_node* x)
{
	ir_node** ins;

	int const n = get_Block_n_cfgpreds(node);
	NEW_ARR_A(ir_node*, ins, n + 1);
	for (int i = 0; i < n; i++)
		ins[i] = get_irn_n(node, i);
	ins[n] = x;
	set_irn_in(node, n + 1, ins);
}

static ir_node *ssa_second_def;
static ir_node *ssa_second_def_block;

static ir_node *search_def_and_create_phis(ir_node *block, ir_mode *mode,
                                           int first)
{
	int i;
	int n_cfgpreds;
	ir_graph *irg;
	ir_node *phi;
	ir_node **in;
	ir_node *dummy;

	/* In case of a bad input to a block we need to return the bad value */
	if (is_Bad(block)) {
		ir_graph *irg = get_irn_irg(block);
		return new_r_Bad(irg, mode);
	}

	/* the other defs can't be marked for cases where a user of the original
	 * value is in the same block as the alternative definition.
	 * In this case we mustn't use the alternative definition.
	 * So we keep a flag that indicated whether we walked at least 1 block
	 * away and may use the alternative definition */
	if (block == ssa_second_def_block && !first) {
		return ssa_second_def;
	}

	/* already processed this block? */
	if (irn_visited(block)) {
		ir_node *value = (ir_node*) get_irn_link(block);
		return value;
	}

	irg = get_irn_irg(block);
	assert(block != get_irg_start_block(irg));

	/* a Block with only 1 predecessor needs no Phi */
	n_cfgpreds = get_Block_n_cfgpreds(block);
	if (n_cfgpreds == 1) {
		ir_node *pred_block = get_Block_cfgpred_block(block, 0);
		ir_node *value      = search_def_and_create_phis(pred_block, mode, 0);

		set_irn_link(block, value);
		mark_irn_visited(block);
		return value;
	}

	/* create a new Phi */
	NEW_ARR_A(ir_node*, in, n_cfgpreds);
	dummy = new_r_Dummy(irg, mode);
	for (i = 0; i < n_cfgpreds; ++i)
		in[i] = dummy;

	phi = new_r_Phi(block, n_cfgpreds, in, mode);
	set_irn_link(block, phi);
	mark_irn_visited(block);

	/* set Phi predecessors */
	for (i = 0; i < n_cfgpreds; ++i) {
		ir_node *pred_block = get_Block_cfgpred_block(block, i);
		ir_node *pred_val   = search_def_and_create_phis(pred_block, mode, 0);

		set_irn_n(phi, i, pred_val);
	}

	return phi;
}

/**
 * Given a set of values this function constructs SSA-form for the users of the
 * first value (the users are determined through the out-edges of the value).
 * Uses the irn_visited flags. Works without using the dominance tree.
 */
static void construct_ssa(ir_node *orig_block, ir_node *orig_val,
                          ir_node *second_block, ir_node *second_val)
{
	ir_graph *irg;
	ir_mode *mode;

	/* no need to do anything */
	if (orig_val == second_val)
		return;

	irg = get_irn_irg(orig_val);
	inc_irg_visited(irg);

	mode = get_irn_mode(orig_val);
	set_irn_link(orig_block, orig_val);
	mark_irn_visited(orig_block);

	ssa_second_def_block = second_block;
	ssa_second_def       = second_val;

	/* Only fix the users of the first, i.e. the original node */
	foreach_out_edge_safe(orig_val, edge) {
		ir_node *user = get_edge_src_irn(edge);
		int j = get_edge_src_pos(edge);
		ir_node *user_block = get_nodes_block(user);
		ir_node *newval;

		/* ignore keeps */
		if (is_End(user))
			continue;

		DB((dbg, LEVEL_3, ">>> Fixing user %+F (pred %d == %+F)\n", user, j, get_irn_n(user, j)));

		if (is_Phi(user)) {
			ir_node *pred_block = get_Block_cfgpred_block(user_block, j);
			newval = search_def_and_create_phis(pred_block, mode, 1);
		} else {
			newval = search_def_and_create_phis(user_block, mode, 1);
		}

		/* don't fix newly created Phis from the SSA construction */
		if (newval != user) {
			DB((dbg, LEVEL_4, ">>>> Setting input %d of %+F to %+F\n", j, user, newval));
			set_irn_n(user, j, newval);
		}
	}
}

/**
 * jumpthreading produces critical edges, e.g. B-C:
 *     A         A
 *  \ /       \  |
 *   B    =>   B |
 *  / \       / \|
 *     C         C
 *
 * By splitting this critical edge more threadings might be possible.
 */
static void split_critical_edge(ir_node *block, int pos)
{
	ir_graph *irg = get_irn_irg(block);
	ir_node *in[1];
	ir_node *new_block;
	ir_node *new_jmp;

	in[0] = get_Block_cfgpred(block, pos);
	new_block = new_r_Block(irg, 1, in);
	new_jmp = new_r_Jmp(new_block);
	set_Block_cfgpred(block, pos, new_jmp);
}

typedef struct jumpthreading_env_t {
	ir_node       *true_block;
	ir_node       *cmp;        /**< The Compare node that might be partial evaluated */
	ir_relation    relation;   /**< The Compare mode of the Compare node. */
	ir_node       *cnst;
	ir_tarval     *tv;
	ir_visited_t   visited_nr;

	ir_node       *cnst_pred;   /**< the block before the constant */
	int            cnst_pos;    /**< the pos to the constant block (needed to
	                                  kill that edge later) */
} jumpthreading_env_t;

static ir_node *copy_and_fix_node(const jumpthreading_env_t *env,
                                  ir_node *block, ir_node *copy_block, int j,
                                  ir_node *node)
{
	int      i, arity;
	ir_node *copy;

	/* we can evaluate Phis right now, all other nodes get copied */
	if (is_Phi(node)) {
		copy = get_Phi_pred(node, j);
		/* we might have to evaluate a Phi-cascade */
		if (get_irn_visited(copy) >= env->visited_nr) {
			copy = (ir_node*)get_irn_link(copy);
		}
	} else {
		copy = exact_copy(node);
		set_nodes_block(copy, copy_block);

		assert(get_irn_mode(copy) != mode_X);

		arity = get_irn_arity(copy);
		for (i = 0; i < arity; ++i) {
			ir_node *pred     = get_irn_n(copy, i);
			ir_node *new_pred;

			if (get_nodes_block(pred) != block)
				continue;

			if (get_irn_visited(pred) >= env->visited_nr) {
				new_pred = (ir_node*)get_irn_link(pred);
			} else {
				new_pred = copy_and_fix_node(env, block, copy_block, j, pred);
			}
			DB((dbg, LEVEL_2, ">> Set Pred of %+F to %+F\n", copy, new_pred));
			set_irn_n(copy, i, new_pred);
		}
	}

	set_irn_link(node, copy);
	set_irn_visited(node, env->visited_nr);

	return copy;
}

static void copy_and_fix(const jumpthreading_env_t *env, ir_node *block,
                         ir_node *copy_block, int j)
{
	/* Look at all nodes in the cond_block and copy them into pred */
	foreach_out_edge(block, edge) {
		ir_node *node = get_edge_src_irn(edge);
		ir_node *copy;
		ir_mode *mode;

		if (is_End(node)) {
			/* edge is a Keep edge. If the end block is unreachable via normal
			 * control flow, we must maintain end's reachability with Keeps.
			 */
			keep_alive(copy_block);
			continue;
		}
		/* ignore control flow */
		mode = get_irn_mode(node);
		if (mode == mode_X || is_Cond(node) || is_Switch(node))
			continue;
#ifdef AVOID_PHIB
		/* we may not copy mode_b nodes, because this could produce Phi with
		 * mode_bs which can't be handled in all backends. Instead we duplicate
		 * the node and move it to its users */
		if (mode == mode_b) {
			ir_node *const pred = get_Proj_pred(node);
			long     const pn   = get_Proj_proj(node);

			foreach_out_edge_safe(node, edge) {
				ir_node *cmp_copy;
				ir_node *user       = get_edge_src_irn(edge);
				int pos             = get_edge_src_pos(edge);
				ir_node *user_block = get_nodes_block(user);

				if (user_block == block)
					continue;

				cmp_copy = exact_copy(pred);
				set_nodes_block(cmp_copy, user_block);
				copy = new_r_Proj(cmp_copy, mode_b, pn);
				set_irn_n(user, pos, copy);
			}
			continue;
		}
#endif

		copy = copy_and_fix_node(env, block, copy_block, j, node);

		/* we might hit values in blocks that have already been processed by a
		 * recursive find_phi_with_const() call */
		assert(get_irn_visited(copy) <= env->visited_nr);
		if (get_irn_visited(copy) >= env->visited_nr) {
			ir_node *prev_copy = (ir_node*)get_irn_link(copy);
			if (prev_copy != NULL)
				set_irn_link(node, prev_copy);
		}
	}

	/* fix data-flow (and reconstruct SSA if needed) */
	foreach_out_edge(block, edge) {
		ir_node *node = get_edge_src_irn(edge);
		ir_node *copy_node;
		ir_mode *mode;

		mode = get_irn_mode(node);
		if (mode == mode_X || is_Cond(node) || is_Switch(node))
			continue;
#ifdef AVOID_PHIB
		if (mode == mode_b)
			continue;
#endif

		DB((dbg, LEVEL_2, ">> Fixing users of %+F\n", node));

		copy_node = (ir_node*)get_irn_link(node);
		construct_ssa(block, node, copy_block, copy_node);
	}

	/* make sure new nodes are kept alive if old nodes were */
	ir_graph *irg = get_irn_irg(block);
	ir_node  *end = get_irg_end(irg);
	for (int i = 0, arity = get_End_n_keepalives(end); i < arity; ++i) {
		ir_node *keep = get_End_keepalive(end, i);
		if (get_irn_visited(keep) < env->visited_nr || is_Block(keep))
			continue;
		ir_node *copy = get_irn_link(keep);
		add_End_keepalive(end, copy);
	}
}

/**
 * returns whether the cmp evaluates to true or false, or can't be evaluated!
 * 1: true, 0: false, -1: can't evaluate
 *
 * @param relation  the compare mode of the Compare
 * @param tv_left   the left tarval
 * @param tv_right  the right tarval
 */
static int eval_cmp_tv(ir_relation relation, ir_tarval *tv_left,
                       ir_tarval *tv_right)
{
	ir_relation cmp_result = tarval_cmp(tv_left, tv_right);

	/* does the compare evaluate to true? */
	if (cmp_result == ir_relation_false)
		return -1;
	if ((cmp_result & relation) != 0)
		return 1;

	return 0;
}

/**
 * returns whether the cmp evaluates to true or false, or can't be evaluated!
 * 1: true, 0: false, -1: can't evaluate
 *
 * @param env      the environment
 * @param cand     the candidate node, either a Const or a Confirm
 */
static int eval_cmp(jumpthreading_env_t *env, ir_node *cand)
{
	if (is_Const(cand)) {
		ir_tarval *tv_cand = get_Const_tarval(cand);
		ir_tarval *tv_cmp  = get_Const_tarval(env->cnst);

		return eval_cmp_tv(env->relation, tv_cand, tv_cmp);
	} else { /* a Confirm */
		ir_tarval *res = computed_value_Cmp_Confirm(env->cmp, cand, env->cnst, env->relation);

		if (res == tarval_bad)
			return -1;
		return res == tarval_b_true;
	}
}

/**
 * Check for Const or Confirm with Const.
 */
static int is_Const_or_Confirm(const ir_node *node)
{
	if (is_Confirm(node))
		node = get_Confirm_bound(node);
	return is_Const(node);
}

/**
 * get the tarval of a Const or Confirm with
 */
static ir_tarval *get_Const_or_Confirm_tarval(const ir_node *node)
{
	if (is_Confirm(node)) {
		if (get_Confirm_bound(node))
			node = get_Confirm_bound(node);
	}
	return get_Const_tarval(node);
}

static ir_node *find_const_or_confirm(jumpthreading_env_t *env, ir_node *jump,
                                      ir_node *value)
{
	ir_node *block = get_nodes_block(jump);

	if (irn_visited_else_mark(value))
		return NULL;

	if (is_Const_or_Confirm(value)) {
		if (eval_cmp(env, value) <= 0)
			return NULL;

		DB((
			dbg, LEVEL_1,
			"> Found jump threading candidate %+F->%+F\n",
			block, env->true_block
		));

		/* adjust true_block to point directly towards our jump */
		add_pred(env->true_block, jump);

		split_critical_edge(env->true_block, 0);

		/* we need a bigger visited nr when going back */
		env->visited_nr++;

		return block;
	}

	if (is_Phi(value)) {
		int i, arity;

		/* the Phi has to be in the same Block as the Jmp */
		if (get_nodes_block(value) != block)
			return NULL;

		arity = get_irn_arity(value);
		for (i = 0; i < arity; ++i) {
			ir_node *copy_block;
			ir_node *phi_pred = get_Phi_pred(value, i);
			ir_node *cfgpred  = get_Block_cfgpred(block, i);

			copy_block = find_const_or_confirm(env, cfgpred, phi_pred);
			if (copy_block == NULL)
				continue;

			/* copy duplicated nodes in copy_block and fix SSA */
			copy_and_fix(env, block, copy_block, i);

			if (copy_block == get_nodes_block(cfgpred)) {
				env->cnst_pred = block;
				env->cnst_pos  = i;
			}

			/* return now as we can't process more possibilities in 1 run */
			return copy_block;
		}
	}

	return NULL;
}

static ir_node *find_candidate(jumpthreading_env_t *env, ir_node *jump,
                               ir_node *value)
{
	ir_node *block = get_nodes_block(jump);

	if (irn_visited_else_mark(value)) {
		return NULL;
	}

	if (is_Const_or_Confirm(value)) {
		ir_tarval *tv = get_Const_or_Confirm_tarval(value);

		if (tv != env->tv)
			return NULL;

		DB((
			dbg, LEVEL_1,
			"> Found jump threading candidate %+F->%+F\n",
			block, env->true_block
		));

		/* adjust true_block to point directly towards our jump */
		add_pred(env->true_block, jump);

		split_critical_edge(env->true_block, 0);

		/* we need a bigger visited nr when going back */
		env->visited_nr++;

		return block;
	}
	if (is_Phi(value)) {
		int i, arity;

		/* the Phi has to be in the same Block as the Jmp */
		if (get_nodes_block(value) != block)
			return NULL;

		arity = get_irn_arity(value);
		for (i = 0; i < arity; ++i) {
			ir_node *copy_block;
			ir_node *phi_pred = get_Phi_pred(value, i);
			ir_node *cfgpred  = get_Block_cfgpred(block, i);

			copy_block = find_candidate(env, cfgpred, phi_pred);
			if (copy_block == NULL)
				continue;

			/* copy duplicated nodes in copy_block and fix SSA */
			copy_and_fix(env, block, copy_block, i);

			if (copy_block == get_nodes_block(cfgpred)) {
				env->cnst_pred = block;
				env->cnst_pos  = i;
			}

			/* return now as we can't process more possibilities in 1 run */
			return copy_block;
		}
	}
	if (is_Cmp(value)) {
		ir_node    *cmp      = value;
		ir_node    *left     = get_Cmp_left(cmp);
		ir_node    *right    = get_Cmp_right(cmp);
		ir_relation relation = get_Cmp_relation(cmp);

		/* we assume that the constant is on the right side, swap left/right
		 * if needed */
		if (is_Const(left)) {
			ir_node *t = left;
			left       = right;
			right      = t;

			relation   = get_inversed_relation(relation);
		}

		if (!is_Const(right))
			return NULL;

		if (get_nodes_block(left) != block)
			return NULL;

		/* negate condition when we're looking for the false block */
		if (env->tv == tarval_b_false) {
			relation = get_negated_relation(relation);
		}

		/* (recursively) look if a pred of a Phi is a constant or a Confirm */
		env->cmp      = cmp;
		env->relation = relation;
		env->cnst     = right;

		return find_const_or_confirm(env, jump, left);
	}

	return NULL;
}

/**
 * Block-walker: searches for the following construct
 *
 *  Const or Phi with constants
 *           |
 *          Cmp
 *           |
 *         Cond
 *          /
 *       ProjX
 *        /
 *     Block
 */
static void thread_jumps(ir_node* block, void* data)
{
	jumpthreading_env_t env;
	bool *changed = (bool*)data;
	ir_node *selector;
	ir_node *projx;
	ir_node *cond;
	ir_node *copy_block;
	int      selector_evaluated;
	ir_graph *irg;
	ir_node *badX;
	int      cnst_pos;

	/* we do not deal with Phis, so restrict this to exactly one cfgpred */
	if (get_Block_n_cfgpreds(block) != 1)
		return;

	projx = get_Block_cfgpred(block, 0);
	if (!is_Proj(projx))
		return;
	assert(get_irn_mode(projx) == mode_X);

	cond = get_Proj_pred(projx);
	/* TODO handle switch Conds */
	if (!is_Cond(cond))
		return;

	/* handle cases that can be immediately evaluated */
	selector = get_Cond_selector(cond);
	selector_evaluated = -1;
	if (is_Cmp(selector)) {
		ir_node *left  = get_Cmp_left(selector);
		ir_node *right = get_Cmp_right(selector);
		if (is_Const(left) && is_Const(right)) {
			ir_relation relation = get_Cmp_relation(selector);
			ir_tarval  *tv_left  = get_Const_tarval(left);
			ir_tarval  *tv_right = get_Const_tarval(right);

			selector_evaluated = eval_cmp_tv(relation, tv_left, tv_right);
		}
	} else if (is_Const_or_Confirm(selector)) {
		ir_tarval *tv = get_Const_or_Confirm_tarval(selector);
		if (tv == tarval_b_true) {
			selector_evaluated = 1;
		} else {
			assert(tv == tarval_b_false);
			selector_evaluated = 0;
		}
	}

	env.cnst_pred = NULL;
	if (get_Proj_proj(projx) == pn_Cond_false) {
		env.tv = tarval_b_false;
		if (selector_evaluated >= 0)
			selector_evaluated = !selector_evaluated;
	} else {
		env.tv = tarval_b_true;
	}

	if (selector_evaluated == 0) {
		ir_graph *irg = get_irn_irg(block);
		ir_node  *bad = new_r_Bad(irg, mode_X);
		exchange(projx, bad);
		*changed = true;
		return;
	} else if (selector_evaluated == 1) {
		dbg_info *dbgi = get_irn_dbg_info(selector);
		ir_node  *jmp  = new_rd_Jmp(dbgi, get_nodes_block(projx));
		DBG_OPT_JUMPTHREADING(projx, jmp);
		exchange(projx, jmp);
		*changed = true;
		return;
	}

	/* (recursively) look if a pred of a Phi is a constant or a Confirm */
	env.true_block = block;
	irg = get_irn_irg(block);
	inc_irg_visited(irg);
	env.visited_nr = get_irg_visited(irg);

	copy_block = find_candidate(&env, projx, selector);
	if (copy_block == NULL)
		return;

	/* We might thread the condition block of an infinite loop,
	 * such that there is no path to End anymore. */
	keep_alive(block);

	/* we have to remove the edge towards the pred as the pred now
	 * jumps into the true_block. We also have to shorten Phis
	 * in our block because of this */
	badX     = new_r_Bad(irg, mode_X);
	cnst_pos = env.cnst_pos;

	/* shorten Phis */
	foreach_out_edge_safe(env.cnst_pred, edge) {
		ir_node *node = get_edge_src_irn(edge);

		if (is_Phi(node)) {
			ir_node *bad = new_r_Bad(irg, get_irn_mode(node));
			set_Phi_pred(node, cnst_pos, bad);
		}
	}

	set_Block_cfgpred(env.cnst_pred, cnst_pos, badX);

	/* the graph is changed now */
	*changed = true;
}

void opt_jumpthreading(ir_graph* irg)
{
	bool changed;
	bool rerun;

	assure_irg_properties(irg,
		IR_GRAPH_PROPERTY_NO_UNREACHABLE_CODE
		| IR_GRAPH_PROPERTY_CONSISTENT_OUT_EDGES
		| IR_GRAPH_PROPERTY_NO_CRITICAL_EDGES);

	FIRM_DBG_REGISTER(dbg, "firm.opt.jumpthreading");

	DB((dbg, LEVEL_1, "===> Performing jumpthreading on %+F\n", irg));

	ir_reserve_resources(irg, IR_RESOURCE_IRN_LINK | IR_RESOURCE_IRN_VISITED);

	changed = false;
	do {
		rerun = false;
		irg_block_walk_graph(irg, thread_jumps, NULL, &rerun);
		changed |= rerun;
	} while (rerun);

	ir_free_resources(irg, IR_RESOURCE_IRN_LINK | IR_RESOURCE_IRN_VISITED);

	confirm_irg_properties(irg,
		changed ? IR_GRAPH_PROPERTIES_NONE : IR_GRAPH_PROPERTIES_ALL);
}
