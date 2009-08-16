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
 * @brief     Some often needed tool-functions
 * @author    Michael Beck
 * @version   $Id$
 */
#ifndef FIRM_COMMON_IRTOOLS_H
#define FIRM_COMMON_IRTOOLS_H

#include "firm_types.h"

#include "lc_opts.h"
lc_opt_entry_t *firm_opt_get_root(void);

#include "pset.h"

#undef MIN
#undef MAX
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

/* calculate the address of the one past last element of an array whose size is
 * known statically */
#define ENDOF(x) ((x) + sizeof(x) / sizeof(*(x)))

/**
 * Three valued compare as demanded by e.g. qsort(3)
 * @param c A number.
 * @param d Another number.
 * @return 0 if c == d, -1 if c < d, 1 if c > d.
 */
#define QSORT_CMP(c, d) (((c) > (d)) - ((c) < (d)))


/**
 * convert an integer into pointer
 */
#define INT_TO_PTR(v)   ((void *)((char *)0 + (v)))

/**
 * convert a pointer into an integer
 */
#define PTR_TO_INT(v)   ((int)((char *)(v) - (char *)0))

/**
 * Dump a pset containing Firm objects.
 */
void firm_pset_dump(pset *set);

/**
 * The famous clear_link() walker-function.
 * Sets all links fields of visited nodes to NULL.
 * Do not implement it by yourself, use this one.
 */
void firm_clear_link(ir_node *n, void *env);

/**
 * The famous clear_link_and_block_lists() walker-function.
 * Sets all links fields of visited nodes to NULL.
 * Additionally, clear all Phi-lists of visited blocks.
 * Do not implement it by yourself, use this one
 */
void firm_clear_node_and_phi_links(ir_node *n, void *env);

/**
 * Copies a node to a new irg. The Ins of the new node point to
 * the predecessors on the old irg.  n->link points to the new node.
 *
 * @param n    The node to be copied
 * @param irg  the new irg
 *
 * Does NOT copy standard nodes like Start, End etc that are fixed
 * in an irg. Instead, the corresponding nodes of the new irg are returned.
 * Note further, that the new nodes have no block.
 */
void copy_irn_to_irg(ir_node *n, ir_graph *irg);

/**
 * Creates an exact copy of a node.
 * The copy resists on the same graph in the same block.
 *
 * @param n   the node to copy
 *
 * @note If the copy is not changed, the next CSE operation will
 *       replace it by the original, so beware.
 */
ir_node *exact_copy(const ir_node *n);

/**
 * Creates an ir_graph pass for running void function(ir_graph *irg).
 * Uses the default verifier and dumper.
 * The pass returns always 0.
 *
 * @param name     the name of this pass
 * @param verify   should this pass be verified?
 * @param dump     should this pass result be dumped?
 * @param params   The parameters for the if conversion.
 *
 * @return  the newly created ir_graph pass
 */
ir_graph_pass_t *def_graph_pass(
	const char *name, int verify, int dump,
	void (*function)(ir_graph *irg));

/**
 * Creates an ir_graph pass for running int function(ir_graph *irg).
 * Uses the default verifier and dumper.
 * The pass returns the return value of function.
 *
 * @param name     the name of this pass
 * @param verify   should this pass be verified?
 * @param dump     should this pass result be dumped?
 * @param params   The parameters for the if conversion.
 *
 * @return  the newly created ir_graph pass
 */
ir_graph_pass_t *def_graph_pass_ret(
	const char *name, int verify, int dump,
	int (*function)(ir_graph *irg));

#endif
