/*
 * Copyright (C) 1995-2011 University of Karlsruhe.  All right reserved.
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
 * @brief   Everlasting outs -- private header.
 * @author  Sebastian Hack, Andreas Schoesser
 * @date    15.01.2005
 * @version $Id$
 */
#ifndef FIRM_IR_EDGES_T_H
#define FIRM_IR_EDGES_T_H

#include "debug.h"

#include "set.h"
#include "list.h"

#include "irnode_t.h"
#include "irgraph_t.h"

#include "iredgekinds.h"
#include "iredges.h"

#define DBG_EDGES  "firm.ir.edges"

/**
 * An edge.
 */
struct ir_edge_t {
	ir_node  *src;          /**< The source node of the edge. */
	int      pos;           /**< The position of the edge at @p src. */
	unsigned invalid : 1;   /**< edges that are removed are marked invalid. */
	unsigned present : 1;   /**< Used by the verifier. Don't rely on its content. */
	unsigned kind    : 4;   /**< The kind of the edge. */
	struct list_head list;  /**< The list head to queue all out edges at a node. */
};


/** Accessor for private irn info. */
#define _get_irn_edge_info(irn, kind) (&(((irn)->edge_info)[kind]))

/** Accessor for private irg info. */
#define _get_irg_edge_info(irg, kind) (&(((irg)->edge_info)[kind]))

/**
* Convenience macro to get the outs_head from a irn_edge_info_t
* struct.
*/
#define _get_irn_outs_head(irn, kind) (&_get_irn_edge_info(irn, kind)->outs_head)

/**
* Get the first edge pointing to some node.
* @note There is no order on out edges. First in this context only
* means, that you get some starting point into the list of edges.
* @param irn The node.
* @return The first out edge that points to this node.
*/
static inline const ir_edge_t *_get_irn_out_edge_first_kind(const ir_node *irn, ir_edge_kind_t kind)
{
	const struct list_head *head;
	assert(edges_activated_kind(get_irn_irg(irn), kind));
	head = _get_irn_outs_head(irn, kind);
	return list_empty(head) ? NULL : list_entry(head->next, ir_edge_t, list);
}

/**
* Get the next edge in the out list of some node.
* @param irn The node.
* @param last The last out edge you have seen.
* @return The next out edge in @p irn 's out list after @p last.
*/
static inline const ir_edge_t *_get_irn_out_edge_next(const ir_node *irn, const ir_edge_t *last)
{
	struct list_head *next = last->list.next;
	return next == _get_irn_outs_head(irn, last->kind) ? NULL : list_entry(next, ir_edge_t, list);
}

/**
* Get the number of edges pointing to a node.
* @param irn The node.
* @return The number of edges pointing to this node.
*/
static inline int _get_irn_n_edges_kind(const ir_node *irn, int kind)
{
	return _get_irn_edge_info(irn, kind)->out_count;
}

static inline int _edges_activated_kind(const ir_graph *irg, ir_edge_kind_t kind)
{
	return _get_irg_edge_info(irg, kind)->activated;
}

/**
* Assure, that the edges information is present for a certain graph.
* @param irg The graph.
*/
static inline void _edges_assure_kind(ir_graph *irg, ir_edge_kind_t kind)
{
	if(!_edges_activated_kind(irg, kind))
		edges_activate_kind(irg, kind);
}

void edges_init_graph_kind(ir_graph *irg, ir_edge_kind_t kind);

/**
 * A node might be revivaled by CSE.
 */
void edges_node_revival(ir_node *node);

void edges_invalidate_kind(ir_node *irn, ir_edge_kind_t kind);

/**
* Register additional memory in an edge.
* This must be called before Firm is initialized.
* @param  n Number of bytes you need.
* @return A number you have to keep and to pass
*         edges_get_private_data()
*         to get a pointer to your data.
*/
size_t edges_register_private_data(size_t n);

/**
* Get a pointer to the private data you registered.
* @param  edge The edge.
* @param  ofs  The number, you obtained with
*              edges_register_private_data().
* @return A pointer to the private data.
*/
static inline void *_get_edge_private_data(const ir_edge_t *edge, int ofs)
{
	return (void *) ((char *) edge + sizeof(edge[0]) + ofs);
}

static inline ir_node *_get_edge_src_irn(const ir_edge_t *edge)
{
	return edge->src;
}

static inline int _get_edge_src_pos(const ir_edge_t *edge)
{
	return edge->pos;
}

/**
* Initialize the out edges.
* This must be called before firm is initialized.
*/
extern void init_edges(void);

void edges_invalidate_all(ir_node *irn);

/**
 * Helper function to dump the edge set of a graph,
 * unused in normal code.
 */
void edges_dump_kind(ir_graph *irg, ir_edge_kind_t kind);

/**
 * Notify normal and block edges.
 */
void edges_notify_edge(ir_node *src, int pos, ir_node *tgt,
                       ir_node *old_tgt, ir_graph *irg);

#define get_irn_n_edges_kind(irn, kind)   _get_irn_n_edges_kind(irn, kind)
#define get_edge_src_irn(edge)            _get_edge_src_irn(edge)
#define get_edge_src_pos(edge)            _get_edge_src_pos(edge)
#define get_edge_private_data(edge, ofs)  _get_edge_private_data(edge,ofs)
#define get_irn_out_edge_next(irn, last)  _get_irn_out_edge_next(irn, last)

#ifndef get_irn_n_edges
#define get_irn_n_edges(irn)              _get_irn_n_edges_kind(irn, EDGE_KIND_NORMAL)
#endif

#ifndef get_irn_out_edge_first
#define get_irn_out_edge_first(irn)       _get_irn_out_edge_first_kind(irn, EDGE_KIND_NORMAL)
#endif

#ifndef get_block_succ_first
#define get_block_succ_first(irn)         _get_irn_out_edge_first_kind(irn, EDGE_KIND_BLOCK)
#endif

#ifndef get_block_succ_next
#define get_block_succ_next(irn, last)    _get_irn_out_edge_next(irn, last)
#endif

#endif
