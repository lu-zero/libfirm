/*
 * Copyright (C) 1995-2008 University of Karlsruhe.  All right reserved.
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
 * @brief   Control flow optimizations.
 * @author  Goetz Lindenmaier, Michael Beck, Sebastian Hack
 * @version $Id$
 *
 * Removes Bad control flow predecessors and empty blocks.  A block is empty
 * if it contains only a Jmp node. Blocks can only be removed if they are not
 * needed for the semantics of Phi nodes. Further, we NEVER remove labeled
 * blocks (even if we could move the label).
 */
#include "config.h"

#include "iroptimize.h"

#if 1

#include <assert.h>

#include "debug.h"
#include "irprintf.h"

#include "array.h"
#include "array_t.h"
#include "bitset.h"
#include "ircons.h"
#include "iredges.h"
#include "irgmod.h"
#include "irgraph.h"
#include "irgwalk.h"
#include "irnode.h"
#include "irtools.h"


static bitset_t *block_marked;


/* Simple recursive algorithm to mark all reachable blocks beginning at the
 * start block */
static void mark_reachable(ir_node *const block)
{
	int       const  block_idx = get_irn_idx(block);
	ir_edge_t const *edge;

	if (bitset_is_set(block_marked, block_idx))
		return;
	bitset_set(block_marked, block_idx);

	//ir_fprintf(stderr, "%+F is reachable\n", block);

	foreach_block_succ(block, edge) {
		ir_node *const succ = get_edge_src_irn(edge);
		mark_reachable(succ);
	}
}


/* Set unreachable control flow predecessors to Bad */
static void remove_unreachable_preds(ir_node *const block, void *const env)
{
	int arity;
	int i;

	(void)env;

	/* There's no point in optimising unreachable blocks */
	if (!bitset_is_set(block_marked, get_irn_idx(block)))
		return;

	arity = get_Block_n_cfgpreds(block);
	for (i = 0; i < arity; ++i) {
		ir_node *const pred = get_Block_cfgpred(block, i);

		if (is_Bad(pred))
			continue;
		if (bitset_is_set(block_marked, get_irn_idx(get_nodes_block(pred))))
			continue;

		//ir_fprintf(stderr, "unreachable %+F removed\n", get_nodes_block(pred));

		set_Block_cfgpred(block, i, new_r_Bad(get_irn_irg(block), mode_X));
	}
}


/* Daisy chain all Phis to their blocks, replace Conds with only a default Proj
 * by Jmp and mark all non-empty blocks, i.e. blocks which contain anything
 * besides Phis and a Jmp */
static void collect_phis_kill_default_and_mark_nonempty(ir_node *const node, void *const env)
{
	(void)env;

	if (is_Phi(node)) {
		ir_node *const block = get_nodes_block(node);
		set_irn_link(node, get_irn_link(block));
		set_irn_link(block, node);
	} else if (!is_Jmp(node) && !is_Block(node)) {
		ir_node *const block = get_nodes_block(node);

		/* eliminate switches, which only have a default proj */
		if (is_Cond(node) && mode_is_int(get_irn_mode(get_Cond_selector(node)))) {
			ir_node         *proj0 = NULL;
			ir_edge_t const *edge;

			foreach_out_edge(node, edge) {
				ir_node *const proj = get_edge_src_irn(edge);
				assert(is_Proj(proj));
				if (proj0 != NULL)
					goto mark_non_empty;
				proj0 = proj;
			}
			assert(proj0 != NULL);
			assert(get_Cond_default_proj(node) == get_Proj_proj(proj0));
			exchange(proj0, new_r_Jmp(block));
		}

mark_non_empty:
		bitset_set(block_marked, get_irn_idx(block));
	}
}


/* Retrieve the number of non-Bad CF predecessors of block */
static int count_preds(ir_node *const block)
{
	int const arity = get_Block_n_cfgpreds(block);
	int       count = 0;
	int       i;

	for (i = 0; i < arity; ++i) {
		count += !is_Bad(get_Block_cfgpred(block, i));
	}
	return count;
}


/* Find a fan, i.e. an empty (except for Phi and Jmp) block with multiple
 * predecesors. Skip single-entry-single-exit block chains */
/* TODO remove, is overkill, the case does not exist */
static ir_node *find_fan(ir_node *const jmp)
{
	ir_node *const block = get_nodes_block(jmp);
	int            arity;
	int            i;

	if (bitset_is_set(block_marked, get_irn_idx(block)))
		return NULL;

	arity = get_Block_n_cfgpreds(block);
	for (i = 0; i < arity; ++i) {
		ir_node *const pred = get_Block_cfgpred(block, i);
		if (is_Bad(pred)) continue;
		for (++i; i < arity; ++i) {
			if (!is_Bad(get_Block_cfgpred(block, i))) return block;
		}
		return find_fan(pred);
	}
	return NULL;
}


static int merge_block_fan(ir_node *block)
{
	int arity = get_Block_n_cfgpreds(block);
	int new_arity = arity;
	int i;

	/* TODO hack to circumvent the ugly case */
	if (count_preds(block) <= 1) return arity;

	for (i = 0; i < arity; ++i) {
		ir_node *pred         = get_Block_cfgpred(block, i);
		ir_node *pred_block;
		int      pred_n_preds;

		if (!is_Jmp(pred)) continue;

		pred_block = find_fan(pred);
		if (pred_block == NULL) continue;

		/* A predecessor must have at least two predecessors to merge it, otherwise
		 * critical edges (and even incorrect control flow) would get created */
		pred_n_preds = count_preds(pred_block);
		if (pred_n_preds > 1) new_arity += pred_n_preds - 1;
	}

	assert(new_arity >= arity);
	if (new_arity > arity) {
		ir_node  *phi;
		ir_node **in;
		int       j;

		NEW_ARR_A(ir_node*, in, new_arity);

		/* Adjust Phis in this block to the new predecessors */
		for (phi = get_irn_link(block); phi != NULL; phi = get_irn_link(phi)) {
			int j = 0;

			for (i = 0; i < arity; ++i) {
				ir_node *pred       = get_Block_cfgpred(block, i);
				ir_node *pred_block;
				ir_node *phi_pred;
				int      k;

				if (is_Bad(pred)) continue;

				pred_block = find_fan(pred);
				if (pred_block == NULL) {
					in[j++] = get_Phi_pred(phi, i);
					continue;
				}

				phi_pred = get_Phi_pred(phi, i);
				if (is_Phi(phi_pred) && get_nodes_block(phi_pred) == pred_block) {
					/* Copy the predecessors, because it is a phi in the block we are
					 * merging  */
					int const pred_arity = get_Block_n_cfgpreds(pred_block);
					for (k = 0; k < pred_arity;  ++k) {
						if (is_Bad(get_Block_cfgpred(pred_block, k))) continue;
						in[j++] = get_Phi_pred(phi_pred, k);
					}
				} else {
					/* Duplicate phi_pred input pred_n_preds times */
					int const pred_n_preds = count_preds(pred_block);
					assert(get_nodes_block(phi_pred) != pred_block);
					for (k = 0; k < pred_n_preds;  ++k) {
						in[j++] = phi_pred;
					}
				}
			}

			/* It may be less, because we drop Bads */
			assert(j <= new_arity);
			set_irn_in(phi, j, in);
		}

		/* It is possible that a predecessor block dominates this block, so copy
		 * over the predecessor's Phis */
		/* TODO phis in predecessor block */
#if 0
		j = 0;
		for (i = 0; i < arity; ++i) {
			ir_node *pred         = get_Block_cfgpred(block, i);
			ir_node *pred_block;

			if (is_Bad(pred)) continue;

			pred_block = find_fan(pred);
			if (pred_block == NULL) {
				in[j++] = pred;
				continue;
			}

			for (phi = get_irn_link(pred_block); phi != NULL;) {
				ir_node *unknown = new_Unknown(get_irn_mode(phi));
				ir_node *next_phi;

				/* Kill the Phi in the predecessor block, so it is not kept */
				next_phi = get_irn_link(phi);
				exchange(phi, new_Bad(mode_X));
				phi = next_phi;
			}
		}
#endif

		/* Adjust this block's control flow predecessors */
		j = 0;
		for (i = 0; i < arity; ++i) {
			ir_node *const pred         = get_Block_cfgpred(block, i);
			ir_node *      pred_block;
			int            pred_arity;
			int            k;

			if (is_Bad(pred)) continue;

			pred_block = find_fan(pred);
			if (pred_block == NULL) {
				in[j++] = pred;
				continue;
			}

			pred_arity = get_Block_n_cfgpreds(pred_block);
			for (k = 0; k < pred_arity;  ++k) {
				ir_node *pred_pred = get_Block_cfgpred(pred_block, k);

				if (is_Bad(pred_pred)) continue;
				in[j++] = pred_pred;
			}
		}

		/* It may be less, because we drop Bads */
		assert(j <= new_arity);
		set_irn_in(block, j, in);
		return j;
	}

	return arity;
}


/* Find the top of a single-entry-single-exit block chain */
static ir_node *follow_Jmp_chain(ir_node *const jmp)
{
	ir_node* block;
	int      arity;
	int      i;

	if (!is_Jmp(jmp)) return jmp;

	block = get_nodes_block(jmp);
	arity = get_Block_n_cfgpreds(block);
	for (i = 0; i < arity; ++i) {
		ir_node * const pred = get_Block_cfgpred(block, i);
		if (is_Bad(pred)) continue;
		for (++i; i < arity; ++i) {
			if (!is_Bad(get_Block_cfgpred(block, i))) return NULL;
		}
		return follow_Jmp_chain(pred);
	}
	return NULL;
}


/* Returns true iff predecessor i and j are equal for every Phi in the chain */
static int phis_select_same(ir_node *phi, int const i, int const j)
{
	for (; phi != NULL; phi = get_irn_link(phi)) {
		if (get_Phi_pred(phi, i) != get_Phi_pred(phi, j)) {
			return 0;
		}
	}
	return 1;
}


static void remove_pointless_cond(ir_node *block, void *env)
{
#if 0 /* TODO */
	const int arity = merge_block_fan(block);
#else
	const int arity = get_Block_n_cfgpreds(block);
#endif
	int       i;

	(void)env;

restart:
	for (i = 0; i < arity; ++i) {
		ir_node *pred_i = get_Block_cfgpred(block, i);
		int j;

		pred_i = follow_Jmp_chain(pred_i);
		if (pred_i == NULL || !is_Proj(pred_i)) continue;

		pred_i = get_Proj_pred(pred_i);
		if (!is_Cond(pred_i)) continue;

		/* TODO only handle ifs, not switches for now */
		if (get_irn_mode(get_Cond_selector(pred_i)) != mode_b) continue;

		for (j = i + 1; j < arity; ++j) {
			ir_node *pred_j = get_Block_cfgpred(block, j);

			pred_j = follow_Jmp_chain(pred_j);
			if (pred_j == NULL || !is_Proj(pred_j)) continue;

			pred_j = get_Proj_pred(pred_j);

			/* if both paths end up at the same cond, check if the phis select the
			 * same on both paths */
			if (pred_i == pred_j && phis_select_same(get_irn_link(block), i, j)) {
				ir_node *const jmp = new_r_Jmp(get_nodes_block(pred_i));
				set_Block_cfgpred(block, i, jmp);
				set_Block_cfgpred(block, j, new_r_Bad(get_irn_irg(block), mode_X));

				ir_fprintf(stderr, "Found pointless Cond at %+F predecessors %d and %d\n", block, i, j);

				/* Removing a pointless Cond can reveal more of them, so restart
				 * scanning this block */
				goto restart;
			}
		}
	}
}


/* Remove all Bad and unreachable predecessors and merge single-entry-single-
 * exit block chains */
static void remove_bad_preds(ir_node *const block, void *const env)
{
	int const   arity = get_Block_n_cfgpreds(block);
	int         i;
	int         j;
	ir_node   **in;
	ir_node    *phi;
	ir_node    *pred;

	(void)env;

	NEW_ARR_A(ir_node*, in, arity);

	/* Remove phi predecessors for Bad predecessor blocks */
	pred = block;
	for (phi = get_irn_link(block); phi != NULL; phi = get_irn_link(phi)) {
		j = 0;
		for (i = 0; i < arity; ++i) {
			ir_node *const pred = get_Block_cfgpred(block, i);
			if (!is_Bad(pred)) in[j++] = get_Phi_pred(phi, i);
		}
		assert(j != 0);
		if (j == 1) {
			/* Only one phi predecessor left */
			exchange(phi, in[0]);
			/* Remove this Phi from the daisy chain */
			set_irn_link(pred, get_irn_link(phi));
		} else {
			if (j != arity) set_irn_in(phi, j, in);
			pred = phi;
		}
	}

	/* Remove all Bad predecessors from block */
	j = 0;
	for (i = 0; i < arity; ++i) {
		ir_node *const pred = get_Block_cfgpred(block, i);
		if (!is_Bad(pred)) in[j++] = pred;
	}

	if (j == 1 && is_Jmp(in[0])) {
		/* Merge block with its only predecessor, which has block as its only
		 * successor */
		exchange(block, get_nodes_block(in[0]));
	} else {
		if (j != arity) set_irn_in(block, j, in);
	}
}


/* Remove keep alive edges into unreachable blocks */
static void remove_keepalives(ir_graph *const irg)
{
	ir_node *const end = get_irg_end(irg);
	int      const n   = get_End_n_keepalives(end);
	int            i;

	for (i = 0; i != n; ++i) {
		ir_node *const kept  = get_End_keepalive(end, i);
		ir_node *const block = is_Block(kept) ? kept : get_nodes_block(kept);
		if (!bitset_is_set(block_marked, get_irn_idx(block)))
			set_End_keepalive(end, i, new_r_Bad(irg, mode_X));
	}
}


/* Optimise the control flow by
 * - removing unreachable blocks
 * - removing Bad control flow predecessors
 * - removing pointless boolean Conds (ifs which select the same on both paths)
 * - removing integer Conds with only a default Proj
 * - TODO removing pointless integer Cond Projs, i.e. cases which select the
 *   same as the default case
 * - merging chains of single-entry-single-exit blocks
 * - TODO merging fans of blocks (blocks which contain nothing but Phis and
 *   Jmp) into their sucessor
 * Note: No critical edges are created
 */
void optimize_cf(ir_graph *const irg)
{
	ir_fprintf(stderr, "cfopt on %+F\n", irg);

	normalize_one_return(irg);
#if 0 /* HACK CF successor edges of blocks seem to get stale */
	edges_assure(irg);
#else
	edges_deactivate(irg);
	edges_activate(irg);
#endif

	block_marked = bitset_malloc(get_irg_last_idx(irg));

	mark_reachable(get_irg_start_block(irg));
	irg_block_walk_graph(irg, NULL, remove_unreachable_preds, NULL);
	remove_keepalives(irg);

	bitset_clear_all(block_marked);

	irg_block_walk_graph(irg, NULL, firm_clear_link, NULL);
	inc_irg_block_visited(irg);
	irg_walk_graph(irg, NULL, collect_phis_kill_default_and_mark_nonempty, NULL);

#if 0
	/* TODO just a test */
	irg_block_walk_graph(irg, NULL, remove_bad_preds, NULL);
	irg_block_walk_graph(irg, NULL, firm_clear_link, NULL);
	inc_irg_block_visited(irg);
	irg_walk_graph(irg, NULL, collect_phis_kill_default_and_mark_nonempty, NULL);
#endif

	irg_block_walk_graph(irg, NULL, remove_pointless_cond, NULL);
	bitset_free(block_marked);

	irg_block_walk_graph(irg, NULL, remove_bad_preds, NULL);

	/* TODO only mark as inconsistent if anything was changed */
	set_irg_outs_inconsistent(irg);
	set_irg_doms_inconsistent(irg);
	set_irg_extblk_inconsistent(irg);
	set_irg_loopinfo_inconsistent(irg);
}

#else

#include <assert.h>
#include <stdbool.h>

#include "xmalloc.h"
#include "irnode_t.h"
#include "irgraph_t.h"
#include "irprog_t.h"

#include "ircons.h"
#include "iropt_t.h"
#include "irgwalk.h"
#include "irgmod.h"
#include "irdump.h"
#include "irverify.h"
#include "iredges.h"

#include "array_t.h"

#include "irouts.h"
#include "irbackedge_t.h"

#include "irflag_t.h"
#include "firmstat.h"
#include "irpass.h"
#include "irphase_t.h"

#include "iropt_dbg.h"

/** An environment for merge_blocks and collect nodes. */
typedef struct merge_env {
	bool      changed;      /**< Set if the graph was changed. */
	bool      phis_moved;   /**< Set if Phi nodes were moved. */
} merge_env;

/** set or reset the removable property of a block. */
static void set_Block_removable(ir_node *block, bool removable)
{
	set_Block_mark(block, removable);
}

/** check if a block has the removable property set. */
static bool is_Block_removable(ir_node *block)
{
	return get_Block_mark(block);
}

/** checks if a given Cond node is a switch Cond. */
static bool is_switch_Cond(ir_node *cond)
{
	ir_node *sel = get_Cond_selector(cond);
	return get_irn_mode(sel) != mode_b;
}

/** Walker: clear link fields and mark all blocks as removable. */
static void clear_link_and_mark_blocks_removable(ir_node *node, void *ctx)
{
	(void) ctx;
	set_irn_link(node, NULL);
	if (is_Block(node))
		set_Block_removable(node, true);
}

/**
 * Collects all Phi nodes in link list of Block.
 * Marks all blocks "non_removable" if they contain a node other
 * than Jmp (and Proj).
 * Links all Proj nodes to their predecessors.
 * Collects all switch-Conds in a list.
 */
static void collect_nodes(ir_node *n, void *ctx)
{
	ir_node ***switch_conds = (ir_node***)ctx;

	if (is_Phi(n)) {
		/* Collect Phi nodes to compact ins along with block's ins. */
		ir_node *block = get_nodes_block(n);
		set_irn_link(n, get_irn_link(block));
		set_irn_link(block, n);
	} else if (is_Block(n)) {
		if (has_Block_entity(n)) {
			/* block with a jump label attached cannot be removed. */
			set_Block_removable(n, false);
		}
	} else if (is_Bad(n) || is_Jmp(n)) {
		/* ignore these */
		return;
	} else {
		/* Check for non-empty block. */
		ir_node *block = get_nodes_block(n);
		if (is_Bad(block))
			return;

		set_Block_removable(block, false);

		if (is_Proj(n)) {
			/* link Proj nodes */
			ir_node *pred = get_Proj_pred(n);
			set_irn_link(n, get_irn_link(pred));
			set_irn_link(pred, n);
		} else if (is_Cond(n) && is_switch_Cond(n)) {
			/* found a switch-Cond, collect */
			ARR_APP1(ir_node*, *switch_conds, n);
		}
	}
}

/** Returns true if pred is predecessor of block b. */
static bool is_pred_of(ir_node *pred, ir_node *b)
{
	int i;

	for (i = get_Block_n_cfgpreds(b) - 1; i >= 0; --i) {
		ir_node *b_pred = get_Block_cfgpred_block(b, i);
		if (b_pred == pred)
			return true;
	}
	return false;
}

/** Test whether we can optimize away pred block pos of b.
 *
 *  @param  b    A block node.
 *  @param  pos  The position of the predecessor block to judge about.
 *
 *  @returns     The number of predecessors
 *
 *  The test is rather tricky.
 *
 *  The situation is something like the following:
 *  @verbatim
 *                 if-block
 *                  /   \
 *              then-b  else-b
 *                  \   /
 *                    b
 *  @endverbatim
 *
 *  b merges the control flow of an if-then-else.  We may not remove
 *  the 'then' _and_ the 'else' block of an 'if' if there is a Phi
 *  node in b, even if both are empty.  The destruction of this Phi
 *  requires that a copy is added before the merge.  We have to
 *  keep one of the case blocks to place the copies in.
 *
 *  To perform the test for pos, we must regard predecessors before pos
 *  as already removed.
 **/
static unsigned test_whether_dispensable(ir_node *b, int pos)
{
	ir_node *pred  = get_Block_cfgpred(b, pos);
	ir_node *predb = get_nodes_block(pred);

	if (is_Bad(pred) || !is_Block_removable(predb))
		return 1;

	/* can't remove self-loops */
	if (predb == b)
		goto non_dispensable;
	if (is_unknown_jump(pred))
		goto non_dispensable;

	/* Seems to be empty. At least we detected this in collect_nodes. */
	if (get_irn_link(b) != NULL) {
		int n_cfgpreds = get_Block_n_cfgpreds(b);
		int i;
		/* there are Phi nodes */

		/* b's pred blocks and pred's pred blocks must be pairwise disjunct.
		 * Handle all pred blocks with preds < pos as if they were already
		 * removed. */
		for (i = 0; i < pos; i++) {
			ir_node *other_pred  = get_Block_cfgpred(b, i);
			ir_node *other_predb = get_nodes_block(other_pred);
			if (is_Bad(other_pred))
				continue;
			if (is_Block_removable(other_predb)
			    && !Block_block_visited(other_predb)) {
				int j;
				for (j = get_Block_n_cfgpreds(other_predb) - 1; j >= 0; --j) {
					ir_node *other_predpred
						= get_Block_cfgpred_block(other_predb, j);
					if (is_pred_of(other_predpred, predb))
						goto non_dispensable;
				}
			} else if (is_pred_of(other_predb, predb)) {
				goto non_dispensable;
			}
		}
		for (i = pos+1; i < n_cfgpreds; i++) {
			ir_node *other_predb = get_Block_cfgpred_block(b, i);
			if (is_pred_of(other_predb, predb))
				goto non_dispensable;
		}
	}
	/* we will not dispense already visited blocks */
	if (Block_block_visited(predb))
		return 1;
	/* if we get here, the block is dispensable, count useful preds */
	return get_irn_arity(predb);

non_dispensable:
	set_Block_removable(predb, false);
	return 1;
}

/**
 * This method removes empty blocks.  A block is empty if it only contains Phi
 * and Jmp nodes.
 *
 * We first adapt Phi nodes, then Block nodes, as we need the old ins
 * of the Block to adapt the Phi nodes.  We do this by computing new
 * in arrays, and then replacing the old ones.  So far we compute new in arrays
 * for all nodes, not regarding whether there is a possibility for optimization.
 *
 * For each predecessor p of a Block b there are three cases:
 *  - The predecessor p is a Bad node: just skip it. The in array of b shrinks
 *    by one.
 *  - The predecessor p is empty. Remove p. All predecessors of p are now
 *    predecessors of b.
 *  - The predecessor p is a block containing useful code. Just keep p as is.
 *
 * For Phi nodes f we have to check the conditions at the Block of f.
 * For cases 1 and 3 we proceed as for Blocks.  For case 2 we can have two
 * cases:
 *  -2a: The old predecessor of the Phi f is a Phi pred_f IN THE BLOCK REMOVED.
 *       In this case we proceed as for blocks. We remove pred_f.  All
 *       predecessors of pred_f now are predecessors of f.
 *  -2b: The old predecessor of f is NOT in the block removed. It might be a Phi
 *       too. We have to replicate f for each predecessor of the removed block.
 *       Or, with other words, the removed predecessor block has exactly one
 *       predecessor.
 *
 * Further there is a special case for self referencing blocks:
 * @verbatim
 *
 *    then_b     else_b                              then_b  else_b
 *       \      /                                      \      /
 *        \    /                                        |    /
 *        pred_b                                        |   /
 *         |   ____                                     |  /  ____
 *         |  |    |                                    |  | |    |
 *         |  |    |       === optimized to ===>        \  | |    |
 *        loop_b   |                                     loop_b   |
 *         |  |    |                                      |  |    |
 *         |  |____|                                      |  |____|
 *         |                                              |
 * @endverbatim
 *
 * If there is a Phi in pred_b, but we remove pred_b, we have to generate a
 * Phi in loop_b, that has the ins of the Phi in pred_b and a self referencing
 * backedge.
 */
static void optimize_blocks(ir_node *b, void *ctx)
{
	int i, j, k, n, max_preds, n_preds, p_preds = -1;
	ir_node *pred, *phi, *next;
	ir_node **in;
	merge_env *env = (merge_env*)ctx;

	if (get_Block_dom_depth(b) < 0) {
		/* ignore unreachable blocks */
		return;
	}

	/* Count the number of predecessor if this block is merged with pred blocks
	   that are empty. */
	max_preds = 0;
	for (i = 0, k = get_Block_n_cfgpreds(b); i < k; ++i) {
		max_preds += test_whether_dispensable(b, i);
	}
	in = XMALLOCN(ir_node*, max_preds);

	/*- Fix the Phi nodes of the current block -*/
	for (phi = (ir_node*)get_irn_link(b); phi != NULL; phi = (ir_node*)next) {
		assert(is_Phi(phi));
		next = (ir_node*)get_irn_link(phi);

		/* Find the new predecessors for the Phi */
		p_preds = 0;
		for (i = 0, n = get_Block_n_cfgpreds(b); i < n; ++i) {
			ir_graph *irg = get_irn_irg(b);
			pred = get_Block_cfgpred_block(b, i);

			if (is_Bad(pred)) {
				/* case Phi 1: maintain Bads, as somebody else is responsible to remove them */
				in[p_preds++] = new_r_Bad(irg, get_irn_mode(phi));
			} else if (is_Block_removable(pred) && !Block_block_visited(pred)) {
				/* case Phi 2: It's an empty block and not yet visited. */
				ir_node *phi_pred = get_Phi_pred(phi, i);

				for (j = 0, k = get_Block_n_cfgpreds(pred); j < k; j++) {
					ir_node *pred_pred = get_Block_cfgpred(pred, j);

					if (is_Bad(pred_pred)) {
						in[p_preds++] = new_r_Bad(irg, get_irn_mode(phi));
						continue;
					}

					if (get_nodes_block(phi_pred) == pred) {
						/* case Phi 2a: */
						assert(is_Phi(phi_pred));  /* Block is empty!! */

						in[p_preds++] = get_Phi_pred(phi_pred, j);
					} else {
						/* case Phi 2b: */
						in[p_preds++] = phi_pred;
					}
				}
			} else {
				/* case Phi 3: */
				in[p_preds++] = get_Phi_pred(phi, i);
			}
		}
		assert(p_preds == max_preds);

		/* Fix the node */
		if (p_preds == 1)
			exchange(phi, in[0]);
		else
			set_irn_in(phi, p_preds, in);
		env->changed = true;
	}

	/*- This happens only if merge between loop backedge and single loop entry.
	    Moreover, it is only needed if predb is the direct dominator of b,
	    else there can be no uses of the Phi's in predb ... -*/
	for (k = 0, n = get_Block_n_cfgpreds(b); k < n; ++k) {
		ir_node *pred  = get_Block_cfgpred(b, k);
		ir_node *predb = get_nodes_block(pred);
		if (is_Bad(pred))
			continue;

		if (is_Block_removable(predb) && !Block_block_visited(predb)) {
			ir_node *next_phi;

			/* we found a predecessor block at position k that will be removed */
			for (phi = (ir_node*)get_irn_link(predb); phi; phi = next_phi) {
				int q_preds = 0;
				next_phi = (ir_node*)get_irn_link(phi);

				assert(is_Phi(phi));

				if (get_Block_idom(b) != predb) {
					/* predb is not the dominator. There can't be uses of pred's Phi nodes, kill them .*/
					ir_graph *irg  = get_irn_irg(b);
					ir_mode  *mode = get_irn_mode(phi);
					exchange(phi, new_r_Bad(irg, mode));
				} else {
					/* predb is the direct dominator of b. There might be uses of the Phi nodes from
					   predb in further block, so move this phi from the predecessor into the block b */
					set_nodes_block(phi, b);
					set_irn_link(phi, get_irn_link(b));
					set_irn_link(b, phi);
					env->phis_moved = true;

					/* first, copy all 0..k-1 predecessors */
					for (i = 0; i < k; i++) {
						pred = get_Block_cfgpred_block(b, i);

						if (is_Bad(pred)) {
							ir_graph *irg  = get_irn_irg(b);
							ir_mode  *mode = get_irn_mode(phi);
							in[q_preds++] = new_r_Bad(irg, mode);
						} else if (is_Block_removable(pred) && !Block_block_visited(pred)) {
							/* It's an empty block and not yet visited. */
							for (j = 0; j < get_Block_n_cfgpreds(pred); j++) {
								if (! is_Bad(get_Block_cfgpred(pred, j))) {
									in[q_preds++] = phi;
								} else {
									ir_graph *irg  = get_irn_irg(b);
									ir_mode  *mode = get_irn_mode(phi);
									in[q_preds++] = new_r_Bad(irg, mode);
								}
							}
						} else {
							in[q_preds++] = phi;
						}
					}

					/* now we are at k, copy the phi predecessors */
					pred = get_nodes_block(get_Block_cfgpred(b, k));
					for (i = 0; i < get_Phi_n_preds(phi); i++) {
						in[q_preds++] = get_Phi_pred(phi, i);
					}

					/* and now all the rest */
					for (i = k+1; i < get_Block_n_cfgpreds(b); i++) {
						pred = get_Block_cfgpred_block(b, i);

						if (is_Bad(pred)) {
							ir_graph *irg  = get_irn_irg(b);
							ir_mode  *mode = get_irn_mode(phi);
							in[q_preds++] = new_r_Bad(irg, mode);
						} else if (is_Block_removable(pred) && !Block_block_visited(pred)) {
							/* It's an empty block and not yet visited. */
							for (j = 0; j < get_Block_n_cfgpreds(pred); j++) {
								if (! is_Bad(get_Block_cfgpred(pred, j))) {
									in[q_preds++] = phi;
								} else {
									ir_graph *irg  = get_irn_irg(b);
									ir_mode  *mode = get_irn_mode(phi);
									in[q_preds++] = new_r_Bad(irg, mode);
								}
							}
						} else {
							in[q_preds++] = phi;
						}
					}

					/* Fix the node */
					if (q_preds == 1)
						exchange(phi, in[0]);
					else
						set_irn_in(phi, q_preds, in);
					env->changed = true;

					assert(q_preds <= max_preds);
					// assert(p_preds == q_preds && "Wrong Phi Fix");
				}
			}
		}
	}

	/*- Fix the block -*/
	n_preds = 0;
	for (i = 0; i < get_Block_n_cfgpreds(b); i++) {
		ir_node *pred  = get_Block_cfgpred(b, i);
		ir_node *predb = get_nodes_block(pred);
		ir_graph *irg  = get_irn_irg(pred);

		/* case 1: Bad predecessor */
		if (is_Bad(pred)) {
			in[n_preds++] = new_r_Bad(irg, mode_X);
			continue;
		}
		if (is_Block_removable(predb) && !Block_block_visited(predb)) {
			/* case 2: It's an empty block and not yet visited. */
			for (j = 0; j < get_Block_n_cfgpreds(predb); j++) {
				ir_node *predpred = get_Block_cfgpred(predb, j);

				if (is_Bad(predpred)) {
					in[n_preds++] = new_r_Bad(irg, mode_X);
					continue;
				}

				in[n_preds++] = predpred;
			}
			/* Remove block+jump as it might be kept alive. */
			exchange(pred, new_r_Bad(get_irn_irg(b), mode_X));
			exchange(predb, new_r_Bad(get_irn_irg(b), mode_BB));
		} else {
			/* case 3: */
			in[n_preds++] = pred;
		}
	}
	assert(n_preds == max_preds);

	set_irn_in(b, n_preds, in);
	env->changed = true;

	/* see if phi-fix was correct */
	assert(get_irn_link(b) == NULL || p_preds == -1 || (n_preds == p_preds));
	xfree(in);
}

/**
 * Optimize table-switch Conds.
 *
 * @param cond the switch-Cond
 * @return true if the switch-Cond was optimized
 */
static bool handle_switch_cond(ir_node *cond)
{
	ir_node *sel   = get_Cond_selector(cond);
	ir_node *proj1 = (ir_node*)get_irn_link(cond);
	ir_node *proj2 = (ir_node*)get_irn_link(proj1);
	ir_node *blk   = get_nodes_block(cond);

	/* exactly 1 Proj on the Cond node: must be the defaultProj */
	if (proj2 == NULL) {
		ir_node *jmp = new_r_Jmp(blk);
		assert(get_Cond_default_proj(cond) == get_Proj_proj(proj1));
		/* convert it into a Jmp */
		exchange(proj1, jmp);
		return true;
	}

	/* handle Cond nodes with constant argument. In this case the localopt rules
	 * should have killed all obviously impossible cases.
	 * So the only case left to handle here is 1 defaultProj + 1 case
	 * (this one case should be the one taken) */
	if (get_irn_link(proj2) == NULL) {
		ir_tarval *tv = value_of(sel);

		if (tv != tarval_bad) {
			/* we have a constant switch */
			long      num     = get_tarval_long(tv);
			long      def_num = get_Cond_default_proj(cond);
			ir_graph *irg     = get_irn_irg(cond);
			ir_node  *bad     = new_r_Bad(irg, mode_X);

			if (def_num == get_Proj_proj(proj1)) {
				/* first one is the defProj */
				if (num == get_Proj_proj(proj2)) {
					ir_node *jmp = new_r_Jmp(blk);
					exchange(proj2, jmp);
					exchange(proj1, bad);
					return true;
				}
			} else if (def_num == get_Proj_proj(proj2)) {
				/* second one is the defProj */
				if (num == get_Proj_proj(proj1)) {
					ir_node *jmp = new_r_Jmp(blk);
					exchange(proj1, jmp);
					exchange(proj2, bad);
					return true;
				}
			} else {
				/* neither: strange, Cond was not optimized so far */
				if (num == get_Proj_proj(proj1)) {
					ir_node *jmp = new_r_Jmp(blk);
					exchange(proj1, jmp);
					exchange(proj2, bad);
					return true;
				} else if (num == get_Proj_proj(proj2)) {
					ir_node *jmp = new_r_Jmp(blk);
					exchange(proj2, jmp);
					exchange(proj1, bad);
					return true;
				}
			}
		}
	}
	return false;
}

/**
 * Optimize boolean Conds, where true and false jump to the same block into a Jmp
 * Block must contain no Phi nodes.
 *
 *        Cond
 *       /    \
 *  projA      projB   =>   Jmp     Bad
 *       \    /                \   /
 *       block                 block
 */
static bool optimize_pred_cond(ir_node *block, int i, int j)
{
	ir_node *projA, *projB, *cond, *pred_block, *jmp, *bad;
	assert(i != j);

	projA = get_Block_cfgpred(block, i);
	if (!is_Proj(projA)) return false;
	projB = get_Block_cfgpred(block, j);
	if (!is_Proj(projB)) return false;
	cond  = get_Proj_pred(projA);
	if (!is_Cond(cond))  return false;

	if (cond != get_Proj_pred(projB)) return false;
	if (is_switch_Cond(cond)) return false;

	/* cond should actually be a Jmp */
	pred_block = get_nodes_block(cond);
	jmp = new_r_Jmp(pred_block);
	bad = new_r_Bad(get_irn_irg(block), mode_X);

	assert(projA != projB);
	exchange(projA, jmp);
	exchange(projB, bad);
	return true;
}

typedef enum block_flags_t {
	BF_HAS_OPERATIONS         = 1 << 0,
	BF_HAS_PHIS               = 1 << 1,
	BF_IS_UNKNOWN_JUMP_TARGET = 1 << 2,
} block_flags_t;

static bool get_phase_flag(ir_phase *block_info, ir_node *block, int flag)
{
	return PTR_TO_INT(phase_get_irn_data(block_info, block)) & flag;
}

static void set_phase_flag(ir_phase *block_info, ir_node *block,
                           block_flags_t flag)
{
	int data = PTR_TO_INT(phase_get_irn_data(block_info, block));
	data |= flag;
	phase_set_irn_data(block_info, block, INT_TO_PTR(data));
}

static void clear_phase_flag(ir_phase *block_info, ir_node *block)
{
	phase_set_irn_data(block_info, block, NULL);
}

static bool has_operations(ir_phase *block_info, ir_node *block)
{
	return get_phase_flag(block_info, block, BF_HAS_OPERATIONS);
}

static void set_has_operations(ir_phase *block_info, ir_node *block)
{
	set_phase_flag(block_info, block, BF_HAS_OPERATIONS);
}

static bool has_phis(ir_phase *block_info, ir_node *block)
{
	return get_phase_flag(block_info, block, BF_HAS_PHIS);
}

static void set_has_phis(ir_phase *block_info, ir_node *block)
{
	set_phase_flag(block_info, block, BF_HAS_PHIS);
}

static bool is_unknown_jump_target(ir_phase *block_info, ir_node *block)
{
	return get_phase_flag(block_info, block, BF_IS_UNKNOWN_JUMP_TARGET);
}

static void set_is_unknown_jump_target(ir_phase *block_info, ir_node *block)
{
	set_phase_flag(block_info, block, BF_IS_UNKNOWN_JUMP_TARGET);
}

/**
 * Pre-Walker: fill block info information.
 */
static void compute_block_info(ir_node *n, void *x)
{
	ir_phase *block_info = (ir_phase *)x;

	if (is_Block(n)) {
		int i, max = get_Block_n_cfgpreds(n);
		for (i=0; i<max; i++) {
			ir_node *pred = get_Block_cfgpred(n,i);
			if (is_unknown_jump(pred)) {
				set_is_unknown_jump_target(block_info, n);
			}
		}
	} else if (is_Phi(n)) {
		ir_node *block = get_nodes_block(n);
		set_has_phis(block_info, block);
	} else if (is_Jmp(n) || is_Cond(n) || is_Proj(n)) {
		/* ignore */
	} else {
		ir_node *block = get_nodes_block(n);
		set_has_operations(block_info, block);
	}
}

static void clear_block_info(ir_node *block, void *x)
{
	ir_phase *block_info = (ir_phase *)x;
	clear_phase_flag(block_info, block);
}

typedef struct skip_env {
	bool changed;
	ir_phase *phase;
} skip_env;

/**
 * Post-Block-walker: Optimize useless if's (boolean Cond nodes
 * with same true/false target)
 * away.
 */
static void optimize_ifs(ir_node *block, void *x)
{
	skip_env *env = (skip_env*)x;
	int i, j;
	int n_preds = get_Block_n_cfgpreds(block);

	if (has_phis(env->phase, block))
		return;

	/* optimize Cond predecessors (might produce Bad predecessors) */
	for (i = 0; i < n_preds; ++i) {
		for (j = i+1; j < n_preds; ++j) {
			optimize_pred_cond(block, i, j);
		}
	}
}

/**
 * Pre-Block walker: remove empty blocks (only contain a Jmp)
 * that are control flow predecessors of the current block.
 */
static void remove_empty_blocks(ir_node *block, void *x)
{
	skip_env *env = (skip_env*)x;
	int i;
	int n_preds = get_Block_n_cfgpreds(block);

	for (i = 0; i < n_preds; ++i) {
		ir_node *jmp, *jmp_block, *pred, *pred_block;
		int n_jpreds = 0;

		jmp = get_Block_cfgpred(block, i);
		if (!is_Jmp(jmp))
			continue;
		jmp_block = get_nodes_block(jmp);
		if (jmp_block == block)
			continue; /* this infinite loop cannot be optimized any further */
		if (is_unknown_jump_target(env->phase, jmp_block))
			continue; /* unknown jump target must not be optimized */
		if (has_operations(env->phase,jmp_block))
			continue; /* this block contains operations and cannot be skipped */
		if (has_phis(env->phase,jmp_block))
			continue; /* this block contains Phis and is not skipped */

		/* jmp_block is an empty block and can be optimized! */

		n_jpreds = get_Block_n_cfgpreds(jmp_block);
		/**
		 * If the jmp block has only one predecessor this is straightforward.
		 * However, if there are more predecessors, we only handle this,
		 * if block has no Phis.
		 */
		if (n_jpreds == 1) {
			/* skip jmp block by rerouting its predecessor to block
			 *
			 *     A              A
			 *     |              |
			 *  jmp_block   =>    |
			 *     |              |
			 *   block          block
			 */
			pred = get_Block_cfgpred(jmp_block, 0);
			exchange(jmp, pred);

			/* cleanup: jmp_block might have a Keep edge! */
			pred_block = get_nodes_block(pred);
			exchange(jmp_block, pred_block);
			env->changed = true;
		} else if (! has_phis(env->phase, block)) {
			/* all predecessors can skip the jmp block, so block gets some new predecessors
			 *
			 *  A     B                 A  B
			 *   \   /                  |  |
			 * jmp_block  C  =>  Bad  C |  |
			 *      \    /          \ | | /
			 *      block            block
			 */
			ir_node **ins = NULL;
			int j;
			NEW_ARR_A(ir_node *, ins, n_preds+n_jpreds);
			/* first copy the old predecessors, because the outer loop (i) still walks over them */
			for (j = 0; j < n_preds; ++j) {
				ins[j] = get_Block_cfgpred(block, j);
			}
			/* now append the new predecessors */
			for (j = 0; j < n_jpreds; ++j) {
				pred = get_Block_cfgpred(jmp_block, j);
				ins[n_preds+j] = pred;
			}
			set_irn_in(block, n_preds+n_jpreds, ins);
			/* convert the jmp_block to Bad */
			ir_graph *irg = get_irn_irg(block);
			exchange(jmp_block, new_r_Bad(irg, mode_BB));
			exchange(jmp, new_r_Bad(irg, mode_X));
			/* let the outer loop walk over the new predecessors as well */
			n_preds += n_jpreds;
			env->changed = true;
			// TODO What if jmp_block had a KeepAlive edge?
		} else {
			/* This would involve Phis ... */
		}
	}
}

/*
 * All cfg optimizations, which do not touch Phi nodes.
 *
 * Note that this might create critical edges.
 */
static void cfgopt_ignoring_phis(ir_graph *irg)
{
	ir_phase *block_info = new_phase(irg, NULL);
	skip_env env = { true, block_info };

	while (env.changed) {
		irg_walk_graph(irg, compute_block_info, NULL, block_info);
		env.changed = false;

		/* Remove blocks, which only consist of a Jmp */
		irg_block_walk_graph(irg, remove_empty_blocks, NULL, &env);

		/* Optimize Cond->Jmp, where then- and else-block are the same. */
		irg_block_walk_graph(irg, NULL, optimize_ifs, &env);

		if (env.changed) {
			set_irg_doms_inconsistent(irg);
			/* clear block info, because it must be recomputed */
			irg_block_walk_graph(irg, clear_block_info, NULL, block_info);
			/* Removing blocks and Conds might enable more optimizations */
			continue;
		} else {
			break;
		}
	}

	phase_free(block_info);
}

/* Optimizations of the control flow that also require changes of Phi nodes.  */
void optimize_cf(ir_graph *irg)
{
	int i, j, n;
	ir_node **in = NULL;
	ir_node *end = get_irg_end(irg);
	ir_node *new_end;
	merge_env env;

	env.changed    = false;
	env.phis_moved = false;

	assert(get_irg_phase_state(irg) != phase_building);

	/* if the graph is not pinned, we cannot determine empty blocks */
	assert(get_irg_pinned(irg) != op_pin_state_floats &&
	       "Control flow optimization need a pinned graph");

	edges_deactivate(irg);

	/* First the "simple" optimizations, which do not touch Phis */
	cfgopt_ignoring_phis(irg);

	/* we use the mark flag to mark removable blocks */
	ir_reserve_resources(irg, IR_RESOURCE_BLOCK_MARK | IR_RESOURCE_IRN_LINK);

	/* The switch Cond optimization might expose unreachable code, so we loop */
	for (;;) {
		int length;
		ir_node **switch_conds = NULL;
		bool changed = false;

		assure_doms(irg);

		/*
		 * This pass collects all Phi nodes in a link list in the block
		 * nodes.  Further it performs simple control flow optimizations.
		 * Finally it marks all blocks that do not contain useful
		 * computations, i.e., these blocks might be removed.
		 */
		switch_conds = NEW_ARR_F(ir_node*, 0);
		irg_walk(end, clear_link_and_mark_blocks_removable, collect_nodes, &switch_conds);

		/* handle all collected switch-Conds */
		length = ARR_LEN(switch_conds);
		for (i = 0; i < length; ++i) {
			ir_node *cond = switch_conds[i];
			changed |= handle_switch_cond(cond);
		}
		DEL_ARR_F(switch_conds);

		if (!changed)
			break;

		set_irg_doms_inconsistent(irg);
		set_irg_extblk_inconsistent(irg);
		set_irg_entity_usage_state(irg, ir_entity_usage_not_computed);
	}

	/* assert due to collect_nodes:
	 * 1. removable blocks are now marked as such
	 * 2. phi lists are up to date
	 */

	/* Optimize the standard code.
	 * It walks only over block nodes and adapts these and the Phi nodes in these
	 * blocks, which it finds in a linked list computed before.
	 * */
	assure_doms(irg);
	irg_block_walk_graph(irg, optimize_blocks, NULL, &env);

	new_end = optimize_in_place(end);
	if (new_end != end) {
		set_irg_end(irg, new_end);
		end = new_end;
	}
	remove_End_Bads_and_doublets(end);

	ir_free_resources(irg, IR_RESOURCE_BLOCK_MARK | IR_RESOURCE_IRN_LINK);

	if (env.phis_moved) {
		/* Bad: when we moved Phi's, we might produce dead Phi nodes
		   that are kept-alive.
		   Some other phases cannot copy with this, so kill them.
		 */
		n = get_End_n_keepalives(end);
		if (n > 0) {
			NEW_ARR_A(ir_node *, in, n);
			assure_irg_outs(irg);

			for (i = j = 0; i < n; ++i) {
				ir_node *ka = get_End_keepalive(end, i);

				if (is_Phi(ka)) {
					int k;

					for (k = get_irn_n_outs(ka) - 1; k >= 0; --k) {
						ir_node *user = get_irn_out(ka, k);

						if (user != ka && user != end) {
							/* Is it a real user or just a self loop ? */
							break;
						}
					}
					if (k >= 0)
						in[j++] = ka;
				} else
					in[j++] = ka;
			}
			if (j != n) {
				set_End_keepalives(end, j, in);
				env.changed = true;
			}
		}
	}

	if (env.changed) {
		/* Handle graph state if was changed. */
		set_irg_doms_inconsistent(irg);
		set_irg_extblk_inconsistent(irg);
		set_irg_entity_usage_state(irg, ir_entity_usage_not_computed);
	}
}

/* Creates an ir_graph pass for optimize_cf. */
ir_graph_pass_t *optimize_cf_pass(const char *name)
{
	return def_graph_pass(name ? name : "optimize_cf", optimize_cf);
}

#endif
