/*
 * Project:     libFIRM
 * File name:   ir/ana/callgraph.h
 * Purpose:     Representation and computation of the callgraph.
 * Author:      Goetz Lindenmaier
 * Modified by:
 * Created:     21.7.2004
 * CVS-ID:      $Id$
 * Copyright:   (c) 2004 Universitšt Karlsruhe
 * Licence:     This file protected by GPL -  GNU GENERAL PUBLIC LICENSE.
 */

#ifndef _CALLGRAPH_H_
#define _CALLGRAPH_H_

/**
 * @file callgraph.h
 *
 *  This file contains the representation of the callgraph.
 *  The nodes of the call graph are ir_graphs.  The edges between
 *  the nodes are calling relations.  I.e., if method a calls method
 *  b at some point, there is an edge between a and b.
 *
 *  Further this file contains an algorithm to construct the call
 *  graph.  The construction of the callgraph uses the callee
 *  information in Call nodes to determine which methods are called.
 *
 *  Finally this file contains an algorithm that computes backedges
 *  in the callgraph, i.e., the algorithm finds possibly recursive calls.
 *  The algorithm computes an upper bound of all recursive calls.
 *
 */

#include "irgraph.h"

/** Flag to indicate state of callgraph. */
typedef enum {
  irp_callgraph_none,
  irp_callgraph_consistent,   /* calltree is inconsistent */
  irp_callgraph_inconsistent,
  irp_callgraph_and_calltree_consistent
} irp_callgraph_state;
irp_callgraph_state get_irp_callgraph_state(void);
void                set_irp_callgraph_state(irp_callgraph_state s);

/** The functions that call irg. */
int       get_irg_n_callers(ir_graph *irg);
ir_graph *get_irg_caller(ir_graph *irg, int pos);

int       is_irg_caller_backedge(ir_graph *irg, int pos);
int       has_irg_caller_backedge(ir_graph *irg);

/** maximal loop depth of call nodes that call along this edge. */
int       get_irg_caller_loop_depth(ir_graph *irg, int pos);

/** The functions called by irg. */
int       get_irg_n_callees(ir_graph *irg);
ir_graph *get_irg_callee(ir_graph *irg, int pos);

int       is_irg_callee_backedge(ir_graph *irg, int pos);
int       has_irg_callee_backedge(ir_graph *irg);

/** maximal loop depth of call nodes that call along this edge. */
int       get_irg_callee_loop_depth(ir_graph *irg, int pos);

/** Maximal loop depth of all paths from an external visible method to
    this irg. */
int       get_irg_loop_depth(ir_graph *irg);
/** Maximal recursion depth of all paths from an external visible method to
    this irg. */
int       get_irg_recursion_depth(ir_graph *irg);

double get_irg_method_execution_frequency (ir_graph *irg);

/** Construct the callgraph. Expects callee information, i.e.,
    irg_callee_info_consistent must be set.  This can be computed with
    cgana(). */
void compute_callgraph(void);
/** Destruct the callgraph. */
void free_callgraph(void);


/** A function type for fuctions passed to the callgraph walker. */
typedef void callgraph_walk_func(ir_graph *g, void *env);

void callgraph_walk(callgraph_walk_func *pre, callgraph_walk_func *post, void *env);

/** Compute the backedges that represent recursions and a looptree.
 */
void find_callgraph_recursions(void);

/** Compute interprocedural performance estimates.
 *
 *  Computes
 *   - the loop depth of the method.
 *     The loop depth of an edge between two methods is the
 *     maximal loop depth of the Call nodes that call along this edge.
 *     The loop depth of the method is the loop depth of the most expensive
 *     path from main().
 *   - The recursion depth.  The maximal number of recursions passed
 *     on all paths reaching this method.
 *   - The execution freqency.  As loop depth, but the edge weight is the sum
 *     of the execution freqencies of all Calls along the edge.
 **/
void compute_performance_estimates(void);

/** Computes the loop nesting information.
 *
 * Computes callee info and the callgraph if
 * this information is not available.
 */
void analyse_loop_nesting_depth(void);


#endif /* _CALLGRAPH_H_ */
