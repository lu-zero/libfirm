/*
 * Copyright (C) 1995-2009 University of Karlsruhe.  All right reserved.
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
 * @brief	analyze graph to provide value range information
 * @author 	Jonas Fietz
 * @version	$Id$
 */
#include "config.h"

#include "config.h"
#include "irtypes.h"
#include "vrp.h"
#include "irouts.h"
#include "irgraph_t.h"
#include "irpass.h"
#include "irgwalk.h"
#include "iredges.h"
#include "tv.h"
#include "irop.h"
#include "pdeq.h"


static char v;
static void *VISITED = &v;

struct vrp_env_t {
	waitq *workqueue;
};

static int update_vrp_data(ir_node *node)
{

	tarval *new_bits_set = get_tarval_bad();
	tarval *new_bits_not_set = get_tarval_bad();
	tarval *new_range_bottom = get_tarval_bad();
	tarval *new_range_top = get_tarval_bad();
	ir_node *new_bits_node = NULL;
	ir_node *new_range_node = NULL;
	enum range_types new_range_type = VRP_UNDEFINED;
	enum range_ops new_range_op = VRP_NONE;
	int something_changed = 0;
	tarval *tmp_tv, *tv;
	ir_node *left, *right, *pred, *bound;
	int overflow_top, overflow_bottom;
	tarval *new_top, *new_bottom;

	pn_Cmp cmp;

	node->vrp.valid = 1;
	/* TODO: Check if all predecessors have valid VRP information*/


	if (!mode_is_int(get_irn_mode(node))) {
		return 0; /* we don't optimize for non-int-nodes*/
	}

	switch (get_irn_opcode(node)) {
	case iro_Const:
		tv = get_Const_tarval(node);

		new_bits_set = tv;
		new_bits_not_set = tarval_not(tv);
		new_range_bottom = tv;
		new_range_top = tv;
		new_range_type = VRP_RANGE;
		break;

	case iro_And:
		left = get_And_left(node);
		right = get_And_right(node);

		new_bits_set = tarval_and(left->vrp.bits_set, right->vrp.bits_set);
		new_bits_not_set = tarval_or(left->vrp.bits_not_set, right->vrp.bits_not_set);

		tmp_tv = tarval_not(left->vrp.bits_set);
		tmp_tv = tarval_eor(left->vrp.bits_not_set, tmp_tv);
		/*check if one of the predecessors is completely determined*/
		if (tarval_is_null(tmp_tv)) {
			new_bits_node = right;
		}

		tmp_tv = tarval_not(right->vrp.bits_set);
		tmp_tv = tarval_eor(right->vrp.bits_not_set, tmp_tv);
		if (tarval_is_null(tmp_tv)) {
			new_bits_node = left;
		}
		break;

	case iro_Add:
		left = get_Add_left(node);
		right = get_Add_right(node);


		if (left->vrp.range_type == VRP_UNDEFINED || right->vrp.range_type ==
				VRP_UNDEFINED || left->vrp.range_type == VRP_VARYING ||
				right->vrp.range_type == VRP_VARYING) {
			return 0;
		}

		new_top = tarval_add(left->vrp.range_top, right->vrp.range_top);
		overflow_top = tarval_carry();
		new_bottom = tarval_add(left->vrp.range_bottom, right->vrp.range_bottom);
		overflow_bottom = tarval_carry();

		if (!overflow_top && !overflow_bottom && left->vrp.range_type == VRP_RANGE
				&&right->vrp.range_type == VRP_RANGE) {
			new_range_bottom = new_bottom;
			new_range_top = new_top;
			new_range_type = VRP_RANGE;
		}

		if (overflow_top || overflow_bottom) {
			/* TODO Implement overflow handling*/
			new_range_type = VRP_UNDEFINED;
		}
		break;

	case iro_Sub:
		left = get_Sub_left(node);
		right = get_Sub_right(node);

		if (left->vrp.range_type == VRP_UNDEFINED || right->vrp.range_type ==
				VRP_UNDEFINED) {
			return 0;
		}

		new_top = tarval_sub(left->vrp.range_top, right->vrp.range_top, NULL);
		overflow_top = tarval_carry();
		new_bottom = tarval_sub(left->vrp.range_bottom, right->vrp.range_bottom, NULL);
		overflow_bottom = tarval_carry();

		if (!overflow_top && !overflow_bottom && left->vrp.range_type == VRP_RANGE
				&&right->vrp.range_type == VRP_RANGE) {
			new_range_bottom = new_bottom;
			new_range_top = new_top;
			new_range_type = VRP_RANGE;
		}

		if (overflow_top || overflow_bottom) {
			/* TODO Implement overflow handling*/
		}
		break;

	case iro_Or:
		left = get_Or_left(node);
		right = get_Or_right(node);

		new_bits_set = tarval_or(left->vrp.bits_set, right->vrp.bits_set);
		new_bits_not_set = tarval_and(left->vrp.bits_not_set, right->vrp.bits_not_set);

		tmp_tv = tarval_not(left->vrp.bits_set);
		tmp_tv = tarval_eor(left->vrp.bits_not_set, tmp_tv);
		/*check if one of the predecessors is completely determined*/
		if (tarval_is_null(tmp_tv)) {
			new_bits_node = right;
		}

		tmp_tv = tarval_not(right->vrp.bits_set);
		tmp_tv = tarval_eor(right->vrp.bits_not_set, tmp_tv);
		if (tarval_is_null(tmp_tv)) {
			new_bits_node = left;
		}
		break;

	case iro_Rotl:
		left = get_Rotl_left(node);
		right = get_Rotl_right(node);

		/* We can only compute this if the right value is a constant*/
		if (is_Const(right)) {
			tarval *bits_set, *bits_not_set;
			bits_set = tarval_rotl(left->vrp.bits_set, get_Const_tarval(right));
			bits_not_set = tarval_rotl(left->vrp.bits_not_set, get_Const_tarval(right));

			new_bits_set = tarval_or(bits_set, node->vrp.bits_set);
			new_bits_not_set = tarval_or(bits_not_set, node->vrp.bits_not_set);
		}
		break;

	case iro_Shl:
		left = get_Shl_left(node);
		right = get_Shl_right(node);

		/* We can only compute this if the right value is a constant*/
		if (is_Const(right)) {
			tarval *bits_set, *bits_not_set;
			ir_mode *m = get_tarval_mode(node->vrp.bits_not_set);
			bits_set = tarval_shl(left->vrp.bits_set, get_Const_tarval(right));
			bits_not_set = tarval_shl(left->vrp.bits_not_set, get_Const_tarval(right));

			new_bits_set = tarval_or(bits_set, node->vrp.bits_set);
			new_bits_not_set = tarval_or(bits_not_set, node->vrp.bits_not_set);

			bits_not_set = tarval_not( tarval_shl(
										get_mode_all_one(m),
										get_Const_tarval(right)));
			new_bits_not_set = tarval_or(bits_not_set, new_bits_not_set);

		}
		break;

	case iro_Shr:
		left = get_Shr_left(node);
		right = get_Shr_right(node);

		/* We can only compute this if the right value is a constant*/
		if (is_Const(right)) {
			tarval *bits_set, *bits_not_set;
			ir_mode *m = get_tarval_mode(node->vrp.bits_not_set);
			bits_set = tarval_shr(left->vrp.bits_set, get_Const_tarval(right));
			bits_not_set = tarval_shr(left->vrp.bits_not_set, get_Const_tarval(right));

			new_bits_set = tarval_or(bits_set, node->vrp.bits_set);
			new_bits_not_set = tarval_or(bits_not_set, node->vrp.bits_not_set);

			bits_not_set = tarval_not( tarval_shr(
										get_mode_all_one(m),
										get_Const_tarval(right)));
			new_bits_not_set = tarval_or(bits_not_set, new_bits_not_set);
		}
		break;

	case iro_Shrs:
		left = get_Shrs_left(node);
		right = get_Shrs_right(node);

		/* We can only compute this if the right value is a constant*/
		if (is_Const(right)) {
			tarval *bits_set, *bits_not_set;
			ir_mode *m = get_tarval_mode(node->vrp.bits_not_set);
			bits_set = tarval_shrs(left->vrp.bits_set, get_Const_tarval(right));
			bits_not_set = tarval_shrs(left->vrp.bits_not_set, get_Const_tarval(right));

			new_bits_set = tarval_or(bits_set, node->vrp.bits_set);
			new_bits_not_set = tarval_or(bits_not_set, node->vrp.bits_not_set);

			bits_not_set = tarval_not( tarval_shrs(
										get_mode_all_one(m),
										get_Const_tarval(right)));
			new_bits_not_set = tarval_or(bits_not_set, new_bits_not_set);
		}
		break;

	case iro_Eor:
		left = get_Eor_left(node);
		right = get_Eor_right(node);

		tarval *bits_set, *bits_not_set;
		bits_not_set = tarval_or(
							tarval_and(left->vrp.bits_set, right->vrp.bits_set),
							tarval_and(left->vrp.bits_not_set,
								right->vrp.bits_not_set));

		bits_set = tarval_or(
						tarval_and(left->vrp.bits_set, right->vrp.bits_not_set),
						tarval_and(left->vrp.bits_not_set, right->vrp.bits_set));

		new_bits_set = tarval_or(bits_set, node->vrp.bits_set);
		new_bits_not_set = tarval_or(bits_not_set, node->vrp.bits_not_set);
		break;

	case iro_Id:
		pred = get_Id_pred(node);
		new_bits_set = pred->vrp.bits_set;
		new_bits_not_set = pred->vrp.bits_not_set;
		new_range_top = pred->vrp.range_top;
		new_range_bottom = pred->vrp.range_bottom;
		new_range_type = pred->vrp.range_type;
		break;

	case iro_Not:
		pred = get_Not_op(node);
		new_bits_set = tarval_or(pred->vrp.bits_not_set, node->vrp.bits_set);
		new_bits_not_set = tarval_or(pred->vrp.bits_set, node->vrp.bits_not_set);
		break;

	case iro_Conv:
		pred = get_Conv_op(node);
		ir_mode *old_mode = get_irn_mode(pred);
		ir_mode *new_mode;

		if (!mode_is_int(old_mode))
			return 0;

		new_mode = get_irn_mode(node);

		/* The second and is needed if target type is smaller*/
		bits_not_set = tarval_not(
							tarval_convert_to(get_mode_all_one(old_mode),
								new_mode
								));
		bits_not_set = tarval_or(bits_not_set, tarval_convert_to(pred->vrp.bits_not_set, new_mode));
		new_bits_not_set = tarval_or(bits_not_set, node->vrp.bits_not_set);
		new_bits_set = tarval_and(
				tarval_not(bits_not_set), tarval_convert_to(pred->vrp.bits_set, new_mode));

		if (tarval_cmp(pred->vrp.range_top, get_mode_max(new_mode)) == pn_Cmp_Le) {
			node->vrp.range_top = pred->vrp.range_top;
		}

		if (tarval_cmp(pred->vrp.range_bottom, get_mode_min(new_mode)) == pn_Cmp_Ge) {
			node->vrp.range_bottom = pred->vrp.range_bottom;
		}
		break;

	case iro_Confirm:
		cmp = get_Confirm_cmp(node);
		bound = get_Confirm_bound(node);

		/** @todo: Handle non-Const bounds */

		if (cmp == pn_Cmp_Lg) {
			/** @todo: Is there some way to preserve the information? */
			new_range_type = VRP_ANTIRANGE;
			if (is_Const(bound)) {
				new_range_top = get_Const_tarval(bound);
				new_range_bottom = get_Const_tarval(bound);
			}
		} else if (cmp == pn_Cmp_Le) {
			if (node->vrp.range_type == VRP_UNDEFINED) {
				new_range_type = VRP_RANGE;
				if (is_Const(bound)) {
					new_range_top = get_Const_tarval(bound);
				}
				new_range_bottom = get_tarval_min(get_irn_mode(node));
			} else if (node->vrp.range_type == VRP_RANGE) {
				if (is_Const(bound)) {
					if (tarval_cmp(node->vrp.range_top,
								get_Const_tarval(bound)) == pn_Cmp_Le) {
						new_range_top = get_Const_tarval(bound);
					}
					new_range_bottom = get_tarval_min(get_irn_mode(node));

				} else if (node->vrp.range_type == VRP_ANTIRANGE) {
					/** @todo: How do we manage not to get a never ending loop? */
				}
			}
		}
		break;

	case iro_Phi:
		/* combine all ranges*/

		pred = get_Phi_pred(node,0);
		new_range_top = pred->vrp.range_top;
		new_range_bottom = pred->vrp.range_bottom;
		new_range_type = pred->vrp.range_type;
		new_bits_set = pred->vrp.bits_set;
		new_bits_not_set = pred->vrp.bits_not_set;

		int num = get_Phi_n_preds(node);
		pn_Cmp cmp;
		int i;

		assert(num > 0);


		for (i = 1; i < num; i++) {
			pred = get_Phi_pred(node, i);
			if (new_range_type == VRP_RANGE && pred->vrp.range_type ==
					VRP_RANGE) {
				cmp = tarval_cmp(new_range_top, pred->vrp.range_top);
				if (cmp == pn_Cmp_Lt) {
					new_range_top = pred->vrp.range_top;
				}
				cmp = tarval_cmp(new_range_bottom, pred->vrp.range_bottom);
				if (cmp == pn_Cmp_Gt) {
					new_range_bottom = pred->vrp.range_bottom;
				}
			} else {
				new_range_type = VRP_VARYING;
				break;
			}
		}

		break;


	default:
		/* unhandled, therefore never updated */
		break;
	}



	/* TODO: Check, if there can be information derived from any of these:
	is_Abs(node) is_Alloc(node) is_Anchor(node) is_Borrow(node) is_Bound(node)
	is_Break(node) is_Builtin(node) is_Call(node) is_CallBegin(node)
	is_Carry(node) is_Cast(node) is_Cmp(node) is_Cond(node)
	is_CopyB(node) is_Div(node) is_DivMod(node) is_Dummy(node)
	is_End(node) is_EndExcept(node) is_EndReg(node) is_Filter(node) is_Free(node)
	is_IJmp(node) is_InstOf(node) is_Jmp(node) is_Load(node) is_Minus(node)
	is_Mod(node) is_Mul(node) is_Mulh(node) is_Mux(node) is_NoMem(node)
	is_Pin(node) is_Proj(node) is_Quot(node)
	is_Raise(node) is_Return(node) is_Sel(node) is_Start(node) is_Store(node)
	is_SymConst(node) is_Sync(node) is_Tuple(node)
	*/

	/* Merge the newly calculated values with those that might already exist*/

	if (new_bits_set != tarval_bad) {
		new_bits_set = tarval_or(new_bits_set, node->vrp.bits_set);
		if (tarval_cmp(new_bits_set, node->vrp.bits_set) != pn_Cmp_Eq) {
			something_changed = 1;
			node->vrp.bits_set = new_bits_set;
		}
	}

	if (new_bits_not_set != tarval_bad) {
		new_bits_not_set = tarval_or(new_bits_not_set, node->vrp.bits_not_set);

		if (tarval_cmp(new_bits_not_set, node->vrp.bits_not_set) != pn_Cmp_Eq) {
			something_changed = 1;
			node->vrp.bits_not_set = new_bits_not_set;
		}
	}

	if (node->vrp.bits_node == NULL && new_bits_node != NULL) {
		something_changed = 1;
		node->vrp.bits_node = new_bits_node;
	}

	if (node->vrp.range_type == VRP_UNDEFINED &&
			new_range_type != VRP_UNDEFINED) {
		something_changed = 1;
		node->vrp.range_type = new_range_type;
		node->vrp.range_bottom = new_range_bottom;
		node->vrp.range_top = new_range_top;
		node->vrp.range_op = new_range_op;
		node->vrp.range_node = new_range_node;

	} else if (node->vrp.range_type == VRP_RANGE) {
		if (new_range_type == VRP_RANGE) {
			if ((new_range_node == NULL && node->vrp.range_node == NULL) ||
					(new_range_node == node->vrp.range_node &&
					 new_range_op == node->vrp.range_op)) {
				if (tarval_cmp(node->vrp.range_bottom, new_range_bottom) == pn_Cmp_Lt) {
					something_changed = 1;
					node->vrp.range_bottom = new_range_bottom;
				}
				if (tarval_cmp(node->vrp.range_top, new_range_top) == pn_Cmp_Gt) {
					something_changed = 1;
					node->vrp.range_top = new_range_top;
				}
			}

			/* prefer the absolute value*/
			if (new_range_node == NULL && node->vrp.range_node != NULL) {
				something_changed = 1;
				node->vrp.range_node = NULL;
				node->vrp.range_top = new_range_top;
				node->vrp.range_bottom = new_range_bottom;
			}
		}

		if (new_range_type == VRP_ANTIRANGE) {
			/* if they are overlapping, cut the range.*/
			/* TODO: Maybe we can preserve more information here*/
			if (new_range_node == NULL && node->vrp.range_node == NULL) {
				if (tarval_cmp(node->vrp.range_bottom, new_range_top) == pn_Cmp_Gt &&
						tarval_cmp(node->vrp.range_bottom, new_range_bottom) == pn_Cmp_Gt) {
					something_changed = 1;
					node->vrp.range_bottom = new_range_top;

				} else if (tarval_cmp(node->vrp.range_top, new_range_bottom) == pn_Cmp_Gt &&
						tarval_cmp(node->vrp.range_top, new_range_top) == pn_Cmp_Lt) {
					something_changed = 1;
					node->vrp.range_top = new_range_bottom;
				}

				/* We can not handle the case where the anti range is in the*/
				/* range*/
			}

			/* prefer the absolute value*/
			if (new_range_node == NULL && node->vrp.range_node != NULL) {
				something_changed = 1;
				node->vrp.range_node = NULL;
				node->vrp.range_top = new_range_top;
				node->vrp.range_bottom = new_range_bottom;
			}
		}
	} else if (node->vrp.range_type == VRP_ANTIRANGE) {
		if (new_range_type == VRP_ANTIRANGE) {
			if ((new_range_node == NULL && node->vrp.range_node == NULL) ||
					(new_range_node == node->vrp.range_node &&
					 new_range_op == node->vrp.range_op)) {
				if (tarval_cmp(node->vrp.range_bottom, new_range_bottom) == pn_Cmp_Gt) {
					something_changed = 1;
					node->vrp.range_bottom = new_range_bottom;
				}
				if (tarval_cmp(node->vrp.range_top, new_range_top) == pn_Cmp_Lt) {
					something_changed = 1;
					node->vrp.range_top = new_range_top;
				}
			}

			/* prefer the absolute value*/
			if (new_range_node == NULL && node->vrp.range_node != NULL) {
				something_changed = 1;
				node->vrp.range_node = NULL;
				node->vrp.range_top = new_range_top;
				node->vrp.range_bottom = new_range_bottom;
			}
		}

		if (new_range_type == VRP_RANGE) {
			if ((new_range_node == NULL && node->vrp.range_node == NULL) ||
					(new_range_node == node->vrp.range_node &&
					 new_range_op == node->vrp.range_op)) {
				if (tarval_cmp(node->vrp.range_bottom, new_range_top) == pn_Cmp_Gt) {
					something_changed = 1;
					node->vrp.range_bottom = new_range_top;
				}
				if (tarval_cmp(node->vrp.range_top, new_range_bottom) == pn_Cmp_Lt) {
					something_changed = 1;
					node->vrp.range_top = new_range_bottom;
				}
			}

			/* prefer the absolute value*/
			if (new_range_node == NULL && node->vrp.range_node != NULL) {
				something_changed = 1;
				node->vrp.range_node = NULL;
				node->vrp.range_top = new_range_top;
				node->vrp.range_bottom = new_range_bottom;
			}
		}
	}

	assert(tarval_is_null(
				tarval_and(node->vrp.bits_set, node->vrp.bits_not_set)));

	return something_changed;
}

static void vrp_first_pass(ir_node *n, void *e)
{
	ir_node *succ;
	int i;
	struct vrp_env_t *env = e;

	if (is_Block(n))
		return;

	set_irn_link(n, VISITED);

	update_vrp_data(n);

	for (i = get_irn_n_outs(n) - 1; i >=0; --i) {
		succ =  get_irn_out(n, i);
		if (get_irn_link(succ) == VISITED) {
			/* we found a loop*/
			waitq_put(env->workqueue, n);
		}
	}
}


void set_vrp_data(ir_graph *irg)
{

	ir_node *succ, *node;
	int i;
	struct vrp_env_t env;

	if (!irg) {
		/* no graph, skip */
		return;
	}

	assure_irg_outs(irg); /* ensure that out edges are consistent*/

	env.workqueue = new_waitq();
	irg_walk_graph(irg, NULL, vrp_first_pass, &env);

	/* while there are entries in the worklist, continue*/
	while (!waitq_empty(env.workqueue)) {
		node = waitq_get(env.workqueue);

		if (update_vrp_data(node)) {
			/* if something changed, add successors to worklist*/
			for (i = get_irn_n_outs(node) - 1; i >=0; --i) {
				succ =  get_irn_out(node, i);
				waitq_put(env.workqueue, node);
			}
		}
	}
	del_waitq(env.workqueue);
}


ir_graph_pass_t *set_vrp_pass(const char *name)
{
	return def_graph_pass(name ? name : "set_vrp", set_vrp_data);
}

pn_Cmp vrp_cmp(ir_node *left, ir_node *right)
{
	if (!left->vrp.valid || !right->vrp.valid) {
		return pn_Cmp_False;
	}

	if (left->vrp.range_type == VRP_RANGE && right->vrp.range_type == VRP_RANGE) {
		if (tarval_cmp(left->vrp.range_top, right->vrp.range_bottom) == pn_Cmp_Lt) {
			return pn_Cmp_Lt;
		}
		if (tarval_cmp(left->vrp.range_bottom, right->vrp.range_top) == pn_Cmp_Gt) {
			return pn_Cmp_Gt;
		}
	}

	if (!tarval_is_null(tarval_and(left->vrp.bits_set, right->vrp.bits_not_set)) ||
			!tarval_is_null(tarval_and(left->vrp.bits_not_set, right->vrp.bits_set))) {
		return pn_Cmp_Lg;
	}
	/* TODO: We can get way more information here*/

	return pn_Cmp_False;
}
