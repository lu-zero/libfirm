/*
 * This file is part of libFirm.
 * Copyright (C) 2012 University of Karlsruhe.
 */

/**
 * @file
 * @brief       Interblock liveness analysis.
 * @author      Sebastian Hack
 * @date        06.12.2004
 */
/* statev is expensive here, only enable when needed */
#define DISABLE_STATEV

#include "iredges_t.h"
#include "irgwalk.h"
#include "irprintf.h"
#include "irdump_t.h"
#include "irnodeset.h"

#include "absgraph.h"
#include "statev_t.h"
#include "be_t.h"
#include "bearch.h"
#include "beutil.h"
#include "belive_t.h"
#include "besched.h"
#include "bemodule.h"

DEBUG_ONLY(static firm_dbg_module_t *dbg = NULL;)

#define LV_STD_SIZE             64

int (be_is_live_in)(const be_lv_t *lv, const ir_node *block, const ir_node *irn)
{
	return _be_is_live_xxx(lv, block, irn, be_lv_state_in);
}

int (be_is_live_out)(const be_lv_t *lv, const ir_node *block, const ir_node *irn)
{
	return _be_is_live_xxx(lv, block, irn, be_lv_state_out);
}

int (be_is_live_end)(const be_lv_t *lv, const ir_node *block, const ir_node *irn)
{
	return _be_is_live_xxx(lv, block, irn, be_lv_state_end);
}

static inline unsigned _be_liveness_bsearch(be_lv_info_t *arr, const ir_node *node)
{
	be_lv_info_t *payload = arr + 1;

	unsigned n   = arr[0].head.n_members;
	unsigned res = 0;
	int lo       = 0;
	int hi       = n;

	if (n == 0)
		return 0;

	do {
		int md           = lo + ((hi - lo) >> 1);
		ir_node *md_node = payload[md].node.node;

		if (node > md_node)
			lo = md + 1;
		else if (node < md_node)
			hi = md;
		else {
			res = md;
			break;
		}

		res = lo;
	} while (lo < hi);

	return res;
}

be_lv_info_node_t *be_lv_get(const be_lv_t *li, const ir_node *bl,
                             const ir_node *irn)
{
	be_lv_info_t *irn_live;
	be_lv_info_node_t *res = NULL;

	stat_ev_tim_push();
	irn_live = ir_nodehashmap_get(be_lv_info_t, &li->map, bl);
	if (irn_live != NULL) {
		/* Get the position of the index in the array. */
		int pos = _be_liveness_bsearch(irn_live, irn);

		/* Get the record in question. 1 must be added, since the first record contains information about the array and must be skipped. */
		be_lv_info_node_t *rec = &irn_live[pos + 1].node;

		/* Check, if the irn is in deed in the array. */
		if (rec->node == irn)
			res = rec;
	}
	stat_ev_tim_pop("be_lv_get");

	return res;
}

static be_lv_info_node_t *be_lv_get_or_set(be_lv_t *li, ir_node *bl,
                                           ir_node *irn)
{
	be_lv_info_t *irn_live = ir_nodehashmap_get(be_lv_info_t, &li->map, bl);
	if (irn_live == NULL) {
		irn_live = OALLOCNZ(&li->obst, be_lv_info_t, LV_STD_SIZE);
		irn_live[0].head.n_size = LV_STD_SIZE-1;
		ir_nodehashmap_insert(&li->map, bl, irn_live);
	}

	/* Get the position of the index in the array. */
	unsigned pos = _be_liveness_bsearch(irn_live, irn);

	/* Get the record in question. 1 must be added, since the first record contains information about the array and must be skipped. */
	be_lv_info_node_t *res = &irn_live[pos + 1].node;

	/* Check, if the irn is in deed in the array. */
	if (res->node != irn) {
		be_lv_info_t *payload;
		unsigned n_members = irn_live[0].head.n_members;
		unsigned n_size    = irn_live[0].head.n_size;
		unsigned i;

		if (n_members + 1 >= n_size) {
			/* double the array size. Remember that the first entry is
			 * metadata about the array and not a real array element */
			unsigned old_size_bytes  = (n_size + 1) * sizeof(irn_live[0]);
			unsigned new_size        = (2 * n_size) + 1;
			size_t   new_size_bytes  = new_size * sizeof(irn_live[0]);
			be_lv_info_t *nw = OALLOCN(&li->obst, be_lv_info_t, new_size);
			memcpy(nw, irn_live, old_size_bytes);
			memset(((char*) nw) + old_size_bytes, 0,
			       new_size_bytes - old_size_bytes);
			nw[0].head.n_size = new_size - 1;
			irn_live = nw;
			ir_nodehashmap_insert(&li->map, bl, nw);
		}

		payload = &irn_live[1];
		for (i = n_members; i > pos; --i) {
			payload[i] = payload[i - 1];
		}

		++irn_live[0].head.n_members;

		res        = &payload[pos].node;
		res->node  = irn;
		res->flags = 0;
	}

	return res;
}

typedef struct lv_remove_walker_t {
	be_lv_t       *lv;
	ir_node const *irn;
} lv_remove_walker_t;

/**
 * Removes a node from the list of live variables of a block.
 */
static void lv_remove_irn_walker(ir_node *const bl, void *const data)
{
	lv_remove_walker_t *const w        = (lv_remove_walker_t*)data;
	ir_node      const *const irn      = w->irn;
	be_lv_info_t       *const irn_live = ir_nodehashmap_get(be_lv_info_t, &w->lv->map, bl);
	if (irn_live != NULL) {
		unsigned n   = irn_live[0].head.n_members;
		unsigned pos = _be_liveness_bsearch(irn_live, irn);
		be_lv_info_t *payload  = irn_live + 1;
		be_lv_info_node_t *res = &payload[pos].node;

		/* The node is in deed in the block's array. Let's remove it. */
		if (res->node == irn) {
			unsigned i;

			for (i = pos + 1; i < n; ++i)
				payload[i - 1] = payload[i];

			payload[n - 1].node.node  = NULL;
			payload[n - 1].node.flags = 0;

			--irn_live[0].head.n_members;
			DBG((dbg, LEVEL_3, "\tdeleting %+F from %+F at pos %d\n", irn, bl, pos));
		}
	}
}

static struct {
	be_lv_t  *lv;         /**< The liveness object. */
	ir_node  *def;        /**< The node (value). */
	ir_node  *def_block;  /**< The block of def. */
} re;

/**
 * Mark a node (value) live out at a certain block. Do this also
 * transitively, i.e. if the block is not the block of the value's
 * definition, all predecessors are also marked live.
 * @param block The block to mark the value live out of.
 * @param state The liveness bits to set, either end or end+out.
 */
static void live_end_at_block(ir_node *const block, be_lv_state_t const state)
{
	be_lv_info_node_t *const n      = be_lv_get_or_set(re.lv, block, re.def);
	be_lv_state_t      const before = n->flags;

	assert(state == be_lv_state_end || state == (be_lv_state_end | be_lv_state_out));
	DBG((dbg, LEVEL_2, "marking %+F live %s at %+F\n", re.def, state & be_lv_state_out ? "end+out" : "end", block));
	n->flags |= state;

	/* There is no need to recurse further, if we where here before (i.e., any
	 * live state bits were set before). */
	if (before != be_lv_state_none)
		return;

	/* Stop going up further, if this is the block of the definition. */
	if (re.def_block == block)
		return;

	DBG((dbg, LEVEL_2, "marking %+F live in at %+F\n", re.def, block));
	n->flags |= be_lv_state_in;

	for (int i = get_Block_n_cfgpreds(block); i-- != 0;) {
		ir_node *const pred_block = get_Block_cfgpred_block(block, i);
		live_end_at_block(pred_block, be_lv_state_end | be_lv_state_out);
	}
}

/**
 * Liveness analysis for a value.
 * Compute the set of all blocks a value is live in.
 * @param irn     The node (value).
 */
static void liveness_for_node(ir_node *irn)
{
	ir_node *const def_block = get_nodes_block(irn);

	re.def       = irn;
	re.def_block = def_block;

	/* Go over all uses of the value */
	foreach_out_edge(irn, edge) {
		ir_node *use = edge->src;
		ir_node *use_block;

		DBG((dbg, LEVEL_4, "%+F: use at %+F, pos %d in %+F\n", irn, use, edge->pos, get_block(use)));
		assert(get_irn_n(use, edge->pos) == irn);

		/*
		 * If the usage is no data node, skip this use, since it does not
		 * affect the liveness of the node.
		 */
		if (!is_liveness_node(use))
			continue;

		/* Get the block where the usage is in. */
		use_block = get_nodes_block(use);

		/*
		 * If the use is a phi function, determine the corresponding block
		 * through which the value reaches the phi function and mark the
		 * value as live out of that block.
		 */
		if (is_Phi(use)) {
			ir_node *pred_block = get_Block_cfgpred_block(use_block, edge->pos);
			live_end_at_block(pred_block, be_lv_state_end);
		}

		/*
		 * Else, the value is live in at this block. Mark it and call live
		 * out on the predecessors.
		 */
		else if (def_block != use_block) {
			int i;

			be_lv_info_node_t *const n = be_lv_get_or_set(re.lv, use_block, irn);
			DBG((dbg, LEVEL_2, "marking %+F live in at %+F\n", irn, use_block));
			n->flags |= be_lv_state_in;

			for (i = get_Block_n_cfgpreds(use_block) - 1; i >= 0; --i) {
				ir_node *pred_block = get_Block_cfgpred_block(use_block, i);
				live_end_at_block(pred_block, be_lv_state_end | be_lv_state_out);
			}
		}
	}
}

/**
 * Walker, collect all nodes for which we want calculate liveness info
 * on an obstack.
 */
static void collect_liveness_nodes(ir_node *irn, void *data)
{
	ir_node **nodes = (ir_node**)data;
	if (is_liveness_node(irn))
		nodes[get_irn_idx(irn)] = irn;
}

void be_liveness_compute_sets(be_lv_t *lv)
{
	int       i;
	int       n;

	if (lv->sets_valid)
		return;

	be_timer_push(T_LIVE);
	ir_nodehashmap_init(&lv->map);
	obstack_init(&lv->obst);

	n = get_irg_last_idx(lv->irg);
	ir_node **const nodes = NEW_ARR_FZ(ir_node*, n);

	/* inserting the variables sorted by their ID is probably
	 * more efficient since the binary sorted set insertion
	 * will not need to move around the data. */
	irg_walk_graph(lv->irg, NULL, collect_liveness_nodes, nodes);

	re.lv = lv;

	for (i = 0; i < n; ++i) {
		if (nodes[i] != NULL)
			liveness_for_node(nodes[i]);
	}

	DEL_ARR_F(nodes);

	be_timer_pop(T_LIVE);

	lv->sets_valid = true;
}

void be_liveness_compute_chk(be_lv_t *lv)
{
	if (lv->lvc != NULL)
		return;
	lv->lvc = lv_chk_new(lv->irg);
}

void be_liveness_invalidate_sets(be_lv_t *lv)
{
	if (!lv->sets_valid)
		return;
	obstack_free(&lv->obst, NULL);
	ir_nodehashmap_destroy(&lv->map);
	lv->sets_valid = false;
}

void be_liveness_invalidate_chk(be_lv_t *lv)
{
	be_liveness_invalidate_sets(lv);

	if (lv->lvc == NULL)
		return;
	lv_chk_free(lv->lvc);
	lv->lvc = NULL;
}

be_lv_t *be_liveness_new(ir_graph *irg)
{
	be_lv_t *lv = XMALLOCZ(be_lv_t);

	lv->irg = irg;

	return lv;
}

void be_liveness_free(be_lv_t *lv)
{
	be_liveness_invalidate_sets(lv);
	be_liveness_invalidate_chk(lv);

	free(lv);
}

void be_liveness_remove(be_lv_t *lv, const ir_node *irn)
{
	if (lv->sets_valid) {
		lv_remove_walker_t w;

		/*
		 * Removes a single irn from the liveness information.
		 * Since an irn can only be live at blocks dominated by the block of its
		 * definition, we only have to process that dominance subtree.
		 */
		w.lv  = lv;
		w.irn = irn;
		dom_tree_walk(get_nodes_block(irn), lv_remove_irn_walker, NULL, &w);
	}
}

void be_liveness_introduce(be_lv_t *lv, ir_node *irn)
{
	/* Don't compute liveness information for non-data nodes. */
	if (lv->sets_valid && is_liveness_node(irn)) {
		re.lv = lv;
		liveness_for_node(irn);
	}
}

void be_liveness_update(be_lv_t *lv, ir_node *irn)
{
	be_liveness_remove(lv, irn);
	be_liveness_introduce(lv, irn);
}

void be_liveness_transfer(const arch_register_class_t *cls,
                          ir_node *node, ir_nodeset_t *nodeset)
{
	/* You should better break out of your loop when hitting the first phi
	 * function. */
	assert(!is_Phi(node) && "liveness_transfer produces invalid results for phi nodes");

	be_foreach_definition(node, cls, value, req,
		ir_nodeset_remove(nodeset, value);
	);

	be_foreach_use(node, cls, in_req, op, op_req,
		ir_nodeset_insert(nodeset, op);
	);
}



void be_liveness_end_of_block(const be_lv_t *lv,
                              const arch_register_class_t *cls,
                              const ir_node *block, ir_nodeset_t *live)
{
	assert(lv->sets_valid && "live sets must be computed");
	be_lv_foreach_cls(lv, block, be_lv_state_end, cls, node) {
		ir_nodeset_insert(live, node);
	}
}



void be_liveness_nodes_live_before(be_lv_t const *const lv, arch_register_class_t const *const cls, ir_node const *const pos, ir_nodeset_t *const live)
{
	ir_node *const bl = get_nodes_block(pos);
	be_liveness_end_of_block(lv, cls, bl, live);
	sched_foreach_reverse(bl, irn) {
		be_liveness_transfer(cls, irn, live);
		if (irn == pos)
			return;
	}
}

static void collect_node(ir_node *irn, void *data)
{
	struct obstack *obst = (struct obstack*)data;
	obstack_ptr_grow(obst, irn);
}

static void be_live_chk_compare(be_lv_t *lv, lv_chk_t *lvc)
{
	ir_graph *irg    = lv->irg;

	struct obstack obst;
	ir_node **nodes;
	ir_node **blocks;
	int i, j;

	obstack_init(&obst);

	irg_block_walk_graph(irg, collect_node, NULL, &obst);
	obstack_ptr_grow(&obst, NULL);
	blocks = (ir_node**)obstack_finish(&obst);

	irg_walk_graph(irg, collect_node, NULL, &obst);
	obstack_ptr_grow(&obst, NULL);
	nodes = (ir_node**)obstack_finish(&obst);

	stat_ev_ctx_push("be_lv_chk_compare");
	for (j = 0; nodes[j]; ++j) {
		ir_node *irn = nodes[j];
		if (is_Block(irn))
			continue;

		for (i = 0; blocks[i]; ++i) {
			ir_node *bl = blocks[i];
			int lvr_in  = be_is_live_in (lv, bl, irn);
			int lvr_out = be_is_live_out(lv, bl, irn);
			int lvr_end = be_is_live_end(lv, bl, irn);

			int lvc_in  = lv_chk_bl_in (lvc, bl, irn);
			int lvc_out = lv_chk_bl_out(lvc, bl, irn);
			int lvc_end = lv_chk_bl_end(lvc, bl, irn);

			if (lvr_in - lvc_in != 0)
				ir_fprintf(stderr, "live in  info for %+F at %+F differs: nml: %d, chk: %d\n", irn, bl, lvr_in, lvc_in);

			if (lvr_end - lvc_end != 0)
				ir_fprintf(stderr, "live end info for %+F at %+F differs: nml: %d, chk: %d\n", irn, bl, lvr_end, lvc_end);

			if (lvr_out - lvc_out != 0)
				ir_fprintf(stderr, "live out info for %+F at %+F differs: nml: %d, chk: %d\n", irn, bl, lvr_out, lvc_out);
		}
	}
	stat_ev_ctx_pop("be_lv_chk_compare");

	obstack_free(&obst, NULL);
}

BE_REGISTER_MODULE_CONSTRUCTOR(be_init_live)
void be_init_live(void)
{
	(void)be_live_chk_compare;
	FIRM_DBG_REGISTER(dbg, "firm.be.liveness");
}
