/*
 * This file is part of libFirm.
 * Copyright (C) 2012 University of Karlsruhe.
 */

/**
 * @file
 * @brief       First simple copy minimization heuristics.
 * @author      Daniel Grund
 * @date        12.04.2005
 *
 * Heuristic for minimizing copies using a queue which holds 'qnodes' not yet
 * examined. A qnode has a 'target color', nodes out of the opt unit and
 * a 'conflict graph'. 'Conflict graph' = "Interference graph' + 'conflict edges'
 * A 'max indep set' is determined from these. We try to color this mis using a
 * color-exchanging mechanism. Occuring conflicts are modeled with 'conflict edges'
 * and the qnode is reinserted in the queue. The first qnode colored without
 * conflicts is the best one.
 */
#include "debug.h"
#include "bitset.h"
#include "raw_bitset.h"
#include "xmalloc.h"

#include "becopyopt_t.h"
#include "becopystat.h"
#include "beintlive_t.h"
#include "beirg.h"
#include "bemodule.h"

DEBUG_ONLY(static firm_dbg_module_t *dbg = NULL;)

/** Defines an invalid register index. */
#define NO_COLOR (-1)

#define SEARCH_FREE_COLORS

#define SLOTS_PINNED_GLOBAL 64
#define SLOTS_CONFLICTS 8
#define SLOTS_CHANGED_NODES 32

#define list_entry_queue(lh) list_entry(lh, qnode_t, queue)
#define HASH_CONFLICT(c) (hash_irn(c.n1) ^ hash_irn(c.n2))

/**
 * Modeling additional conflicts between nodes. NOT live range interference
 */
typedef struct conflict_t {
	const ir_node *n1, *n2;
} conflict_t;

/**
 * If an irn is changed, the changes first get stored in a node_stat_t,
 * to allow undo of changes (=drop new data) in case of conflicts.
 */
typedef struct node_stat_t {
	ir_node *irn;
	int      new_color;
	unsigned pinned_local :1;
} node_stat_t;

/**
 * Represents a node in the optimization queue.
 */
typedef struct qnode_t {
	struct list_head queue;            /**< chaining of unit_t->queue */
	int              color;            /**< target color */
	set              *conflicts;       /**< contains conflict_t's. All internal conflicts */
	int              mis_costs;        /**< costs of nodes/copies in the mis. */
	int              mis_size;         /**< size of the array below */
	ir_node          **mis;            /**< the nodes of unit_t->nodes[] being part of the max independent set */
	set              *changed_nodes;   /**< contains node_stat_t's. */
} qnode_t;

static pset *pinned_global;  /**< optimized nodes should not be altered any more */

static int set_cmp_conflict_t(const void *x, const void *y, size_t size)
{
	const conflict_t *xx = (const conflict_t*)x;
	const conflict_t *yy = (const conflict_t*)y;
	(void) size;

	return xx->n1 != yy->n1 || xx->n2 != yy->n2;
}

/**
 * If a local pinned conflict occurs, a new edge in the conflict graph is added.
 * The next maximum independent set build, will regard it.
 */
static inline void qnode_add_conflict(const qnode_t *qn, const ir_node *n1, const ir_node *n2)
{
	conflict_t c;
	DBG((dbg, LEVEL_4, "\t      %+F -- %+F\n", n1, n2));

	if (get_irn_idx(n1) < get_irn_idx(n2)) {
		c.n1 = n1;
		c.n2 = n2;
	} else {
		c.n1 = n2;
		c.n2 = n1;
	}
	(void)set_insert(conflict_t, qn->conflicts, &c, sizeof(c), HASH_CONFLICT(c));
}

/**
 * Checks if two nodes are in a conflict.
 */
static inline int qnode_are_conflicting(const qnode_t *qn, const ir_node *n1, const ir_node *n2)
{
	conflict_t c;
	/* search for live range interference */
	if (n1 != n2) {
		be_lv_t *const lv = be_get_irg_liveness(get_irn_irg(n1));
		if (be_values_interfere(lv, n1, n2))
			return 1;
	}
	/* search for recoloring conflicts */
	if (get_irn_idx(n1) < get_irn_idx(n2)) {
		c.n1 = n1;
		c.n2 = n2;
	} else {
		c.n1 = n2;
		c.n2 = n1;
	}
	return set_find(conflict_t, qn->conflicts, &c, sizeof(c), HASH_CONFLICT(c)) != 0;
}

static int set_cmp_node_stat_t(const void *x, const void *y, size_t size)
{
	(void) size;
	return ((const node_stat_t*)x)->irn != ((const node_stat_t*)y)->irn;
}

/**
 * Finds a node status entry of a node if existent. Otherwise return NULL
 */
static inline const node_stat_t *qnode_find_node(const qnode_t *qn, ir_node *irn)
{
	node_stat_t find;
	find.irn = irn;
	return set_find(node_stat_t, qn->changed_nodes, &find, sizeof(find), hash_irn(irn));
}

/**
 * Finds a node status entry of a node if existent. Otherwise it will return
 * an initialized new entry for this node.
 */
static inline node_stat_t *qnode_find_or_insert_node(const qnode_t *qn, ir_node *irn)
{
	node_stat_t find;
	find.irn = irn;
	find.new_color = NO_COLOR;
	find.pinned_local = 0;
	return set_insert(node_stat_t, qn->changed_nodes, &find, sizeof(find), hash_irn(irn));
}

/**
 * Returns the virtual color of a node if set before, else returns the real color.
 */
static inline int qnode_get_new_color(const qnode_t *qn, ir_node *irn)
{
	const node_stat_t *found = qnode_find_node(qn, irn);
	if (found)
		return found->new_color;
	else
		return get_irn_col(irn);
}

/**
 * Sets the virtual color of a node.
 */
static inline void qnode_set_new_color(const qnode_t *qn, ir_node *irn, int color)
{
	node_stat_t *found = qnode_find_or_insert_node(qn, irn);
	found->new_color = color;
	DBG((dbg, LEVEL_3, "\t      col(%+F) := %d\n", irn, color));
}

/**
 * Checks if a node is local pinned. A node is local pinned, iff it belongs
 * to the same optimization unit and has been optimized before the current
 * processed node.
 */
static inline int qnode_is_pinned_local(const qnode_t *qn, ir_node *irn)
{
	const node_stat_t *found = qnode_find_node(qn, irn);
	if (found)
		return found->pinned_local;
	else
		return 0;
}

/**
 * Local-pins a node, so optimizations of further nodes of the same opt unit
 * can handle situations in which a color change would undo prior optimizations.
 */
static inline void qnode_pin_local(const qnode_t *qn, ir_node *irn)
{
	node_stat_t *found = qnode_find_or_insert_node(qn, irn);
	found->pinned_local = 1;
	if (found->new_color == NO_COLOR)
		found->new_color = get_irn_col(irn);
}


/**
 * Possible return values of qnode_color_irn()
 */
#define CHANGE_SAVE NULL
#define CHANGE_IMPOSSIBLE (ir_node *)1

/**
 * Performs virtual re-coloring of node @p n to color @p col. Virtual colors of
 * other nodes are changed too, as required to preserve correctness. Function is
 * aware of local and global pinning. Recursive.
 *
 * If irn == trigger the color @p col must be used. (the first recoloring)
 * If irn != trigger an arbitrary free color may be used. If no color is free, @p col is used.
 *
 * @param  irn     The node to set the color for
 * @param  col     The color to set
 * @param  trigger The irn that caused the wish to change the color of the irn
 *                 External callers must call with trigger = irn
 *
 * @return CHANGE_SAVE iff setting the color is possible, with all transitive effects.
 *         CHANGE_IMPOSSIBLE iff conflicts with reg-constraintsis occured.
 *         Else the first conflicting ir_node encountered is returned.
 *
 */
static ir_node *qnode_color_irn(qnode_t const *const qn, ir_node *const irn, int const col, ir_node const *const trigger, bitset_t const *const allocatable_regs, be_ifg_t *const ifg)
{
	int irn_col = qnode_get_new_color(qn, irn);
	neighbours_iter_t iter;

	DBG((dbg, LEVEL_3, "\t    %+F \tcaused col(%+F) \t%2d --> %2d\n", trigger, irn, irn_col, col));

	/* If the target color is already set do nothing */
	if (irn_col == col) {
		DBG((dbg, LEVEL_3, "\t      %+F same color\n", irn));
		return CHANGE_SAVE;
	}

	/* If the irn is pinned, changing color is impossible */
	if (pset_find_ptr(pinned_global, irn) || qnode_is_pinned_local(qn, irn)) {
		DBG((dbg, LEVEL_3, "\t      %+F conflicting\n", irn));
		return irn;
	}

	arch_register_req_t   const *const req = arch_get_irn_register_req(irn);
	arch_register_class_t const *const cls = req->cls;
#ifdef SEARCH_FREE_COLORS
	/* If we resolve conflicts (recursive calls) we can use any unused color.
	 * In case of the first call @p col must be used.
	 */
	if (irn != trigger) {
		bitset_t *free_cols = bitset_alloca(cls->n_regs);
		int free_col;

		/* Get all possible colors */
		bitset_copy(free_cols, allocatable_regs);

		/* Exclude colors not assignable to the irn */
		if (arch_register_req_is(req, limited))
			rbitset_and(free_cols->data, req->limited, free_cols->size);

		/* Exclude the color of the irn, because it must _change_ its color */
		bitset_clear(free_cols, irn_col);

		/* Exclude all colors used by adjacent nodes */
		be_ifg_foreach_neighbour(ifg, &iter, irn, curr)
			bitset_clear(free_cols, qnode_get_new_color(qn, curr));

		free_col = bitset_next_set(free_cols, 0);

		if (free_col != -1) {
			qnode_set_new_color(qn, irn, free_col);
			return CHANGE_SAVE;
		}
	}
#endif /* SEARCH_FREE_COLORS */

	/* If target color is not allocatable changing color is impossible */
	if (!arch_reg_is_allocatable(req, arch_register_for_index(cls, col))) {
		DBG((dbg, LEVEL_3, "\t      %+F impossible\n", irn));
		return CHANGE_IMPOSSIBLE;
	}

	/*
	 * If we arrive here changing color may be possible, but there may be conflicts.
	 * Try to color all conflicting nodes 'curr' with the color of the irn itself.
	 */
	be_ifg_foreach_neighbour(ifg, &iter, irn, curr) {
		DBG((dbg, LEVEL_3, "\t      Confl %+F(%d)\n", curr, qnode_get_new_color(qn, curr)));
		if (qnode_get_new_color(qn, curr) == col && curr != trigger) {
			ir_node *const sub_res = qnode_color_irn(qn, curr, irn_col, irn, allocatable_regs, ifg);
			if (sub_res != CHANGE_SAVE) {
				be_ifg_neighbours_break(&iter);
				return sub_res;
			}
		}
	}

	/*
	 * If we arrive here, all conflicts were resolved.
	 * So it is save to change this irn
	 */
	qnode_set_new_color(qn, irn, col);
	return CHANGE_SAVE;
}


/**
 * Tries to set the colors for all members of this queue node;
 * to the target color qn->color
 * @returns 1 iff all members colors could be set
 *          0 else
 */
static int qnode_try_color(qnode_t const *const qn, bitset_t const *const allocatable_regs, be_ifg_t *const ifg)
{
	int i;
	for (i=0; i<qn->mis_size; ++i) {
		ir_node *test_node, *confl_node;

		test_node = qn->mis[i];
		DBG((dbg, LEVEL_3, "\t    Testing %+F\n", test_node));
		confl_node = qnode_color_irn(qn, test_node, qn->color, test_node, allocatable_regs, ifg);

		if (confl_node == CHANGE_SAVE) {
			DBG((dbg, LEVEL_3, "\t    Save --> pin local\n"));
			qnode_pin_local(qn, test_node);
		} else if (confl_node == CHANGE_IMPOSSIBLE) {
			DBG((dbg, LEVEL_3, "\t    Impossible --> remove from qnode\n"));
			qnode_add_conflict(qn, test_node, test_node);
			return 0;
		} else {
			if (qnode_is_pinned_local(qn, confl_node)) {
				/* changing test_node would change back a node of current ou */
				if (confl_node == qn->mis[0]) {
					/* Adding a conflict edge between testnode and conflnode
					 * would introduce a root -- arg interference.
					 * So remove the arg of the qn */
					DBG((dbg, LEVEL_3, "\t    Conflicting local with phi --> remove from qnode\n"));
					qnode_add_conflict(qn, test_node, test_node);
				} else {
					DBG((dbg, LEVEL_3, "\t    Conflicting local --> add conflict\n"));
					qnode_add_conflict(qn, confl_node, test_node);
				}
			}
			if (pset_find_ptr(pinned_global, confl_node)) {
				/* changing test_node would change back a node of a prior ou */
				DBG((dbg, LEVEL_3, "\t    Conflicting global --> remove from qnode\n"));
				qnode_add_conflict(qn, test_node, test_node);
			}
			return 0;
		}
	}
	return 1;
}

/**
 * Determines a maximum weighted independent set with respect to
 * the interference and conflict edges of all nodes in a qnode.
 */
static inline void qnode_max_ind_set(qnode_t *qn, const unit_t *ou)
{
	ir_node **safe, **unsafe;
	int i, o, safe_count, safe_costs, unsafe_count, *unsafe_costs;
	bitset_t *curr, *best;
	int next, curr_weight, best_weight = 0;

	/* assign the nodes into two groups.
	 * safe: node has no interference, hence it is in every max stable set.
	 * unsafe: node has an interference
	 */
	safe         = ALLOCAN(ir_node*, ou->node_count - 1);
	safe_costs   = 0;
	safe_count   = 0;
	unsafe       = ALLOCAN(ir_node*, ou->node_count - 1);
	unsafe_costs = ALLOCAN(int,      ou->node_count - 1);
	unsafe_count = 0;
	for (i=1; i<ou->node_count; ++i) {
		int is_safe = 1;
		for (o=1; o<ou->node_count; ++o) {
			if (qnode_are_conflicting(qn, ou->nodes[i], ou->nodes[o])) {
				if (i!=o) {
					unsafe_costs[unsafe_count] = ou->costs[i];
					unsafe[unsafe_count] = ou->nodes[i];
					++unsafe_count;
				}
				is_safe = 0;
				break;
			}
		}
		if (is_safe) {
			safe_costs += ou->costs[i];
			safe[safe_count++] = ou->nodes[i];
		}
	}



	/* now compute the best set out of the unsafe nodes*/
	best = bitset_alloca(unsafe_count);

	if (unsafe_count > MIS_HEUR_TRIGGER) {
		/* Heuristic: Greedy trial and error form index 0 to unsafe_count-1 */
		for (i=0; i<unsafe_count; ++i) {
			bitset_set(best, i);
			/* check if it is a stable set */
			for (o=bitset_next_set(best, 0); o!=-1 && o<=i; o=bitset_next_set(best, o+1))
				if (qnode_are_conflicting(qn, unsafe[i], unsafe[o])) {
					bitset_clear(best, i); /* clear the bit and try next one */
					break;
				}
		}
		/* compute the weight */
		bitset_foreach(best, pos)
			best_weight += unsafe_costs[pos];
	} else {
		/* Exact Algorithm: Brute force */
		curr = bitset_alloca(unsafe_count);
		bitset_set_all(curr);
		while (!bitset_is_empty(curr)) {
			/* check if curr is a stable set */
			for (i=bitset_next_set(curr, 0); i!=-1; i=bitset_next_set(curr, i+1))
				for (o=bitset_next_set(curr, i); o!=-1; o=bitset_next_set(curr, o+1)) /* !!!!! difference to ou_max_ind_set_costs(): NOT (curr, i+1) */
						if (qnode_are_conflicting(qn, unsafe[i], unsafe[o]))
							goto no_stable_set;

			/* if we arrive here, we have a stable set */
			/* compute the weight of the stable set*/
			curr_weight = 0;
			bitset_foreach(curr, pos)
				curr_weight += unsafe_costs[pos];

			/* any better ? */
			if (curr_weight > best_weight) {
				best_weight = curr_weight;
				bitset_copy(best, curr);
			}

no_stable_set:
			bitset_minus1(curr);
		}
	}

	/* transfer the best set into the qn */
	qn->mis_size = 1+safe_count+bitset_popcount(best);
	qn->mis_costs = safe_costs+best_weight;
	qn->mis[0] = ou->nodes[0]; /* the root is always in a max stable set */
	next = 1;
	for (i=0; i<safe_count; ++i)
		qn->mis[next++] = safe[i];
	bitset_foreach(best, pos)
		qn->mis[next++] = unsafe[pos];
}

/**
 * Creates a new qnode
 */
static inline qnode_t *new_qnode(const unit_t *ou, int color)
{
	qnode_t *qn = XMALLOC(qnode_t);
	qn->color         = color;
	qn->mis           = XMALLOCN(ir_node*, ou->node_count);
	qn->conflicts     = new_set(set_cmp_conflict_t, SLOTS_CONFLICTS);
	qn->changed_nodes = new_set(set_cmp_node_stat_t, SLOTS_CHANGED_NODES);
	return qn;
}

/**
 * Frees space used by a queue node
 */
static inline void free_qnode(qnode_t *qn)
{
	del_set(qn->conflicts);
	del_set(qn->changed_nodes);
	free(qn->mis);
	free(qn);
}

/**
 * Inserts a qnode in the sorted queue of the optimization unit. Queue is
 * ordered by field 'size' (the size of the mis) in decreasing order.
 */
static inline void ou_insert_qnode(unit_t *ou, qnode_t *qn)
{
	struct list_head *lh;

	if (qnode_are_conflicting(qn, ou->nodes[0], ou->nodes[0])) {
		/* root node is not in qnode */
		free_qnode(qn);
		return;
	}

	qnode_max_ind_set(qn, ou);
	/* do the insertion */
	DBG((dbg, LEVEL_4, "\t  Insert qnode color %d with cost %d\n", qn->color, qn->mis_costs));
	lh = &ou->queue;
	while (lh->next != &ou->queue) {
		qnode_t *curr = list_entry_queue(lh->next);
		if (curr->mis_costs <= qn->mis_costs)
			break;
		lh = lh->next;
	}
	list_add(&qn->queue, lh);
}

/**
 * Tries to re-allocate colors of nodes in this opt unit, to achieve lower
 * costs of copy instructions placed during SSA-destruction and lowering.
 * Works only for opt units with exactly 1 root node, which is the
 * case for approximately 80% of all phi classes and 100% of register constrained
 * nodes. (All other phi classes are reduced to this case.)
 */
static void ou_optimize(unit_t *ou, bitset_t const *const allocatable_regs, be_ifg_t *const ifg)
{
	DBG((dbg, LEVEL_1, "\tOptimizing unit:\n"));
	for (int i = 0; i < ou->node_count; ++i)
		DBG((dbg, LEVEL_1, "\t %+F\n", ou->nodes[i]));

	/* init queue */
	INIT_LIST_HEAD(&ou->queue);

	arch_register_req_t const *const req    = arch_get_irn_register_req(ou->nodes[0]);
	unsigned                   const n_regs = req->cls->n_regs;
	if (arch_register_req_is(req, limited)) {
		unsigned const* limited = req->limited;

		for (unsigned idx = 0; idx != n_regs; ++idx) {
			if (!bitset_is_set(allocatable_regs, idx))
				continue;
			if (!rbitset_is_set(limited, idx))
				continue;

			ou_insert_qnode(ou, new_qnode(ou, idx));
		}
	} else {
		for (unsigned idx = 0; idx != n_regs; ++idx) {
			if (!bitset_is_set(allocatable_regs, idx))
				continue;

			ou_insert_qnode(ou, new_qnode(ou, idx));
		}
	}

	/* search best */
	qnode_t *curr;
	for (;;) {
		assert(!list_empty(&ou->queue));
		/* get head of queue */
		curr = list_entry_queue(ou->queue.next);
		list_del(&curr->queue);
		DBG((dbg, LEVEL_2, "\t  Examine qnode color %d with cost %d\n", curr->color, curr->mis_costs));

		/* try */
		if (qnode_try_color(curr, allocatable_regs, ifg))
			break;

		/* no success, so re-insert */
		del_set(curr->changed_nodes);
		curr->changed_nodes = new_set(set_cmp_node_stat_t, SLOTS_CHANGED_NODES);
		ou_insert_qnode(ou, curr);
	}

	/* apply the best found qnode */
	if (curr->mis_size >= 2) {
		int root_col = qnode_get_new_color(curr, ou->nodes[0]);
		DBG((dbg, LEVEL_1, "\t  Best color: %d  Costs: %d << %d << %d\n", curr->color, ou->min_nodes_costs, ou->all_nodes_costs - curr->mis_costs, ou->all_nodes_costs));
		/* globally pin root and all args which have the same color */
		pset_insert_ptr(pinned_global, ou->nodes[0]);
		for (int i = 1; i < ou->node_count; ++i) {
			ir_node *irn = ou->nodes[i];
			int nc = qnode_get_new_color(curr, irn);
			if (nc != NO_COLOR && nc == root_col)
				pset_insert_ptr(pinned_global, irn);
		}

		/* set color of all changed nodes */
		foreach_set(curr->changed_nodes, node_stat_t, ns) {
			/* NO_COLOR is possible, if we had an undo */
			if (ns->new_color != NO_COLOR) {
				DBG((dbg, LEVEL_1, "\t    color(%+F) := %d\n", ns->irn, ns->new_color));
				set_irn_col(req->cls, ns->irn, ns->new_color);
			}
		}
	}

	/* free best qnode (curr) and queue */
	free_qnode(curr);
	list_for_each_entry_safe(qnode_t, curr, tmp, &ou->queue, queue)
		free_qnode(curr);
}

/**
 * Solves the problem using a heuristic approach
 * Uses the OU data structure
 */
int co_solve_heuristic(copy_opt_t *co)
{
	ASSERT_OU_AVAIL(co);

	pinned_global = pset_new_ptr(SLOTS_PINNED_GLOBAL);
	bitset_t const *const allocatable_regs = co->cenv->allocatable_regs;
	be_ifg_t       *const ifg              = co->cenv->ifg;
	list_for_each_entry(unit_t, curr, &co->units, units) {
		if (curr->node_count > 1)
			ou_optimize(curr, allocatable_regs, ifg);
	}

	del_pset(pinned_global);
	return 0;
}

BE_REGISTER_MODULE_CONSTRUCTOR(be_init_copyheur)
void be_init_copyheur(void)
{
	static co_algo_info copyheur = {
		co_solve_heuristic, 0
	};

	be_register_copyopt("heur1", &copyheur);
	FIRM_DBG_REGISTER(dbg, "ir.be.copyoptheur");
}
