/*
 * Project:     libFIRM
 * File name:   ir/opt/ldstopt.c
 * Purpose:     load store optimizations
 * Author:
 * Created:
 * CVS-ID:      $Id$
 * Copyright:   (c) 1998-2004 Universit��t Karlsruhe
 * Licence:     This file protected by GPL -  GNU GENERAL PUBLIC LICENSE.
 */
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif
#ifdef HAVE_STRING_H
# include <string.h>
#endif

# include "irnode_t.h"
# include "irgraph_t.h"
# include "irmode_t.h"
# include "iropt_t.h"
# include "ircons_t.h"
# include "irgmod.h"
# include "irgwalk.h"
# include "irvrfy.h"
# include "tv_t.h"
# include "dbginfo_t.h"
# include "iropt_dbg.h"
# include "irflag_t.h"
# include "array.h"
# include "irhooks.h"
# include "irtools.h"
# include "opt_polymorphy.h"

#ifdef DO_CACHEOPT
#include "cacheopt/cachesim.h"
#endif

#undef IMAX
#define IMAX(a,b)	((a) > (b) ? (a) : (b))

#define MAX_PROJ	IMAX(pn_Load_max, pn_Store_max)

enum changes_t {
  DF_CHANGED = 1,       /**< data flow changed */
  CF_CHANGED = 2,       /**< control flow changed */
};

/**
 * walker environment
 */
typedef struct _walk_env_t {
  struct obstack obst;		/**< list of all stores */
  unsigned changes;             /**< a bitmask of graph changes */
} walk_env_t;

/**
 * flags for Load/Store
 */
enum ldst_flags_t {
  LDST_VISITED = 1              /**< if set, this Load/Store is already visited */
};

/**
 * a Load/Store info
 */
typedef struct _ldst_info_t {
  ir_node  *projs[MAX_PROJ];    /**< list of Proj's of this node */
  ir_node  *exc_block;          /**< the exception block if available */
  int      exc_idx;             /**< predecessor index in the exception block */
  unsigned flags;               /**< flags */
  unsigned visited;             /**< visited counter for breaking loops */
} ldst_info_t;

/**
 * flags for control flow
 */
enum block_flags_t {
  BLOCK_HAS_COND = 1,      /**< Block has conditional control flow */
  BLOCK_HAS_EXC  = 2       /**< Block has exceptional control flow */
};

/**
 * a Block info
 */
typedef struct _block_info_t {
  unsigned flags;               /**< flags for the block */
} block_info_t;

/** the master visited flag for loop detection. */
static unsigned master_visited = 0;

#define INC_MASTER()       ++master_visited
#define MARK_NODE(info)    (info)->visited = master_visited
#define NODE_VISITED(info) (info)->visited >= master_visited

/**
 * get the Load/Store info of a node
 */
static ldst_info_t *get_ldst_info(ir_node *node, walk_env_t *env)
{
  ldst_info_t *info = get_irn_link(node);

  if (! info) {
    info = obstack_alloc(&env->obst, sizeof(*info));

    memset(info, 0, sizeof(*info));

    set_irn_link(node, info);
  }
  return info;
}

/**
 * get the Block info of a node
 */
static block_info_t *get_block_info(ir_node *node, walk_env_t *env)
{
  block_info_t *info = get_irn_link(node);

  if (! info) {
    info = obstack_alloc(&env->obst, sizeof(*info));

    memset(info, 0, sizeof(*info));

    set_irn_link(node, info);
  }
  return info;
}

/**
 * update the projection info for a Load/Store
 */
static unsigned update_projs(ldst_info_t *info, ir_node *proj)
{
  long nr = get_Proj_proj(proj);

  assert(0 <= nr && nr <= MAX_PROJ && "Wrong proj from LoadStore");

  if (info->projs[nr]) {
    /* there is already one, do CSE */
    exchange(proj, info->projs[nr]);
    return DF_CHANGED;
  }
  else {
    info->projs[nr] = proj;
    return 0;
  }
}

/**
 * update the exception block info for a Load/Store node.
 *
 * @param info   the load/store info struct
 * @param block  the exception handler block for this load/store
 * @param pos    the control flow input of the block
 */
static unsigned update_exc(ldst_info_t *info, ir_node *block, int pos)
{
  assert(info->exc_block == NULL && "more than one exception block found");

  info->exc_block = block;
  info->exc_idx   = pos;
  return 0;
}

#define get_irn_out_n(node)     (unsigned)PTR_TO_INT(get_irn_link(node))
#define set_irn_out_n(node, n)  set_irn_link(adr, INT_TO_PTR(n))

/**
 * walker, collects all Load/Store/Proj nodes
 *
 * walks from Start -> End
 */
static void collect_nodes(ir_node *node, void *env)
{
  ir_op       *op = get_irn_op(node);
  ir_node     *pred, *blk, *pred_blk;
  ldst_info_t *ldst_info;
  walk_env_t  *wenv = env;

  if (op == op_Proj) {
    ir_node *adr;
    ir_op *op;

    pred = get_Proj_pred(node);
    op   = get_irn_op(pred);

    if (op == op_Load) {
      ldst_info = get_ldst_info(pred, wenv);

      wenv->changes |= update_projs(ldst_info, node);

      if ((ldst_info->flags & LDST_VISITED) == 0) {
        adr = get_Load_ptr(pred);
        set_irn_out_n(adr, get_irn_out_n(adr) + 1);

        ldst_info->flags |= LDST_VISITED;
      }

      /*
       * Place the Proj's to the same block as the
       * predecessor Load. This is always ok and prevents
       * "non-SSA" form after optimizations if the Proj
       * is in a wrong block.
       */
      blk      = get_nodes_block(node);
      pred_blk = get_nodes_block(pred);
      if (blk != pred_blk) {
        wenv->changes |= DF_CHANGED;
        set_nodes_block(node, pred_blk);
      }
    }
    else if (op == op_Store) {
      ldst_info = get_ldst_info(pred, wenv);

      wenv->changes |= update_projs(ldst_info, node);

      if ((ldst_info->flags & LDST_VISITED) == 0) {
        adr = get_Store_ptr(pred);
        set_irn_out_n(adr, get_irn_out_n(adr) + 1);

        ldst_info->flags |= LDST_VISITED;
      }

      /*
       * Place the Proj's to the same block as the
       * predecessor Store. This is always ok and prevents
       * "non-SSA" form after optimizations if the Proj
       * is in a wrong block.
       */
      blk      = get_nodes_block(node);
      pred_blk = get_nodes_block(pred);
      if (blk != pred_blk) {
        wenv->changes |= DF_CHANGED;
        set_nodes_block(node, pred_blk);
      }
    }
  }
  else if (op == op_Block) { /* check, if it's an exception block */
    int i;

    for (i = get_Block_n_cfgpreds(node) - 1; i >= 0; --i) {
      ir_node      *pred_block;
      block_info_t *bl_info;

      pred = skip_Proj(get_Block_cfgpred(node, i));

      /* ignore Bad predecessors, they will be removed later */
      if (is_Bad(pred))
        continue;

      pred_block = get_nodes_block(pred);
      bl_info    = get_block_info(pred_block, wenv);

      if (is_fragile_op(pred))
	      bl_info->flags |= BLOCK_HAS_EXC;
      else if (is_irn_forking(pred))
        bl_info->flags |= BLOCK_HAS_COND;

      if (get_irn_op(pred) == op_Load || get_irn_op(pred) == op_Store) {
        ldst_info = get_ldst_info(pred, wenv);

        wenv->changes |= update_exc(ldst_info, node, i);
      }
    }
  }
}

/**
 * Returns an entity if the address ptr points to a constant one.
 */
static entity *find_constant_entity(ir_node *ptr)
{
  for (;;) {
    ir_op *op = get_irn_op(ptr);

    if (op == op_SymConst && (get_SymConst_kind(ptr) == symconst_addr_ent)) {
      return get_SymConst_entity(ptr);
    }
    else if (op == op_Sel) {
      entity *ent = get_Sel_entity(ptr);
      type *tp    = get_entity_owner(ent);

      /* Do not fiddle with polymorphism. */
      if (is_Class_type(get_entity_owner(ent)) &&
          ((get_entity_n_overwrites(ent)    != 0) ||
           (get_entity_n_overwrittenby(ent) != 0)   ) )
        return NULL;

      if (variability_constant == get_entity_variability(ent))
        return ent;

      if (is_Array_type(tp)) {
        /* check bounds */
        int i, n;

        for (i = 0, n = get_Sel_n_indexs(ptr); i < n; ++i) {
          ir_node *bound;
          tarval *tlower, *tupper;
          ir_node *index = get_Sel_index(ptr, i);
          tarval *tv     = computed_value(index);

          /* check if the index is constant */
          if (tv == tarval_bad)
            return NULL;

          bound  = get_array_lower_bound(tp, i);
          tlower = computed_value(bound);
          bound  = get_array_upper_bound(tp, i);
          tupper = computed_value(bound);

          if (tlower == tarval_bad || tupper == tarval_bad)
            return NULL;

          if (tarval_cmp(tv, tlower) & pn_Cmp_Lt)
            return NULL;
          if (tarval_cmp(tupper, tv) & pn_Cmp_Lt)
            return NULL;

          /* ok, bounds check finished */
        }
      }

      /* try next */
      ptr = get_Sel_ptr(ptr);
    }
    else
      return NULL;
  }
}

/**
 * Return the Selection index of a Sel node from dimension n
 */
static long get_Sel_array_index_long(ir_node *n, int dim) {
  ir_node *index = get_Sel_index(n, dim);
  assert(get_irn_op(index) == op_Const);
  return get_tarval_long(get_Const_tarval(index));
}

/**
 * Returns the accessed component graph path for an
 * node computing an address.
 *
 * @param ptr    the node computing the address
 * @param depth  current depth in steps upward from the root
 *               of the address
 */
static compound_graph_path *rec_get_accessed_path(ir_node *ptr, int depth) {
  compound_graph_path *res = NULL;
  entity              *root, *field;
  int                 path_len, pos;

  if (get_irn_op(ptr) == op_SymConst) {
    /* a SymConst. If the depth is 0, this is an access to a global
     * entity and we don't need a component path, else we know
     * at least it's length.
     */
    assert(get_SymConst_kind(ptr) == symconst_addr_ent);
    root = get_SymConst_entity(ptr);
    res = (depth == 0) ? NULL : new_compound_graph_path(get_entity_type(root), depth);
  }
  else {
    assert(get_irn_op(ptr) == op_Sel);
    /* it's a Sel, go up until we find the root */
    res = rec_get_accessed_path(get_Sel_ptr(ptr), depth+1);

    /* fill up the step in the path at the current position */
    field    = get_Sel_entity(ptr);
    path_len = get_compound_graph_path_length(res);
    pos      = path_len - depth - 1;
    set_compound_graph_path_node(res, pos, field);

    if (is_Array_type(get_entity_owner(field))) {
      assert(get_Sel_n_indexs(ptr) == 1 && "multi dim arrays not implemented");
      set_compound_graph_path_array_index(res, pos, get_Sel_array_index_long(ptr, 0));
    }
  }
  return res;
}

/** Returns an access path or NULL.  The access path is only
 *  valid, if the graph is in phase_high and _no_ address computation is used.
 */
static compound_graph_path *get_accessed_path(ir_node *ptr) {
  return rec_get_accessed_path(ptr, 0);
}

/**
 * optimize a Load
 */
static unsigned optimize_load(ir_node *load)
{
  ldst_info_t *info = get_irn_link(load);
  ir_mode *load_mode = get_Load_mode(load);
  ir_node *pred, *mem, *ptr, *new_node;
  entity *ent;
  unsigned res = 0;

  /* do NOT touch volatile loads for now */
  if (get_Load_volatility(load) == volatility_is_volatile)
    return 0;

  /* the address of the load to be optimized */
  ptr = get_Load_ptr(load);

  /*
   * Check if we can remove the exception from a Load:
   * This can be done, if the address is from an Sel(Alloc) and
   * the Sel type is a subtype of the allocated type.
   *
   * This optimizes some often used OO constructs,
   * like x = new O; x->t;
   */
  if (info->projs[pn_Load_X_except]) {
    if (get_irn_op(ptr) == op_Sel) {
      ir_node *mem = get_Sel_mem(ptr);

      if (get_irn_op(skip_Proj(mem)) == op_Alloc) {
        /* ok, check the types */
        entity *ent  = get_Sel_entity(ptr);
        type *s_type = get_entity_type(ent);
        type *a_type = get_Alloc_type(mem);

        if (is_subclass_of(s_type, a_type)) {
          /* ok, condition met: there can't be an exception because
           * Alloc guarantees that enough memory was allocated */

          exchange(info->projs[pn_Load_X_except], new_Bad());
          info->projs[pn_Load_X_except] = NULL;
          res |= CF_CHANGED;
        }
      }
    }
    else if ((get_irn_op(skip_Proj(ptr)) == op_Alloc) ||
	     ((get_irn_op(ptr) == op_Cast) && (get_irn_op(skip_Proj(get_Cast_op(ptr))) == op_Alloc))) {
      /* simple case: a direct load after an Alloc. Firm Alloc throw
       * an exception in case of out-of-memory. So, there is no way for an
       * exception in this load.
       * This code is constructed by the "exception lowering" in the Jack compiler.
       */
       exchange(info->projs[pn_Load_X_except], new_Bad());
       info->projs[pn_Load_X_except] = NULL;
       res |= CF_CHANGED;
    }
  }

  /* the mem of the Load. Must still be returned after optimization */
  mem  = get_Load_mem(load);

  if (! info->projs[pn_Load_res] && ! info->projs[pn_Load_X_except]) {
    /* a Load which value is neither used nor exception checked, remove it */
    exchange(info->projs[pn_Load_M], mem);

    return res | DF_CHANGED;
  }

  /* Load from a constant polymorphic field, where we can resolve
     polymorphism. */
  new_node = transform_node_Load(load);
  if (new_node != load) {
    if (info->projs[pn_Load_M]) {
      exchange(info->projs[pn_Load_M], mem);
      info->projs[pn_Load_M] = NULL;
    }
    if (info->projs[pn_Load_X_except]) {
      exchange(info->projs[pn_Load_X_except], new_Bad());
      info->projs[pn_Load_X_except] = NULL;
    }
    if (info->projs[pn_Load_res])
      exchange(info->projs[pn_Load_res], new_node);
    return res | DF_CHANGED;
  }

  /* check if we can determine the entity that will be loaded */
  ent = find_constant_entity(ptr);
  if (ent) {
    if ((allocation_static == get_entity_allocation(ent)) &&
        (visibility_external_allocated != get_entity_visibility(ent))) {
      /* a static allocation that is not external: there should be NO exception
       * when loading. */

      /* no exception, clear the info field as it might be checked later again */
      if (info->projs[pn_Load_X_except]) {
        exchange(info->projs[pn_Load_X_except], new_Bad());
        info->projs[pn_Load_X_except] = NULL;
        res |= CF_CHANGED;
      }

      if (variability_constant == get_entity_variability(ent)
	        && is_atomic_entity(ent)) {
        /* Might not be atomic after
           lowering of Sels.  In this
           case we could also load, but
           it's more complicated. */
        /* more simpler case: we load the content of a constant value:
         * replace it by the constant itself
         */

        /* no memory */
        if (info->projs[pn_Load_M]) {
          exchange(info->projs[pn_Load_M], mem);
          res |= DF_CHANGED;
        }

        /* no result :-) */
        if (info->projs[pn_Load_res]) {
          if (is_atomic_entity(ent)) {
            ir_node *c = copy_const_value(get_irn_dbg_info(load), get_atomic_ent_value(ent));

            DBG_OPT_RC(load, c);
            exchange(info->projs[pn_Load_res], c);
            return DF_CHANGED | res;
          }
        }
      }
      else if (variability_constant == get_entity_variability(ent)) {
        compound_graph_path *path = get_accessed_path(ptr);

        if (path) {
          ir_node *c;

          assert(is_proper_compound_graph_path(path, get_compound_graph_path_length(path)-1));
          /*
          {
            int j;
            for (j = 0; j < get_compound_graph_path_length(path); ++j) {
              entity *node = get_compound_graph_path_node(path, j);
              fprintf(stdout, ".%s", get_entity_name(node));
              if (is_Array_type(get_entity_owner(node)))
                      fprintf(stdout, "[%d]", get_compound_graph_path_array_index(path, j));
            }
            printf("\n");
          }
          */

          c = get_compound_ent_value_by_path(ent, path);
          free_compound_graph_path(path);

          /* printf("  cons: "); DDMN(c); */

          if (info->projs[pn_Load_M]) {
            exchange(info->projs[pn_Load_M], mem);
            res |= DF_CHANGED;
          }
          if (info->projs[pn_Load_res]) {
            exchange(info->projs[pn_Load_res], copy_const_value(get_irn_dbg_info(load), c));
            return res | DF_CHANGED;
          }
        }
        else {
          /*  We can not determine a correct access path.  E.g., in jack, we load
	            a byte from an object to generate an exception.   Happens in test program
	            Reflectiontest.
          printf(">>>>>>>>>>>>> Found access to constant entity %s in function %s\n", get_entity_name(ent),
           get_entity_name(get_irg_entity(current_ir_graph)));
          printf("  load: "); DDMN(load);
          printf("  ptr:  "); DDMN(ptr);
          */
        }
      }
    }
  }

  /* Check, if the address of this load is used more than once.
   * If not, this load cannot be removed in any case. */
  if (get_irn_out_n(ptr) <= 1)
    return res;

  /*
   * follow the memory chain as long as there are only Loads
   * and try to replace current Load or Store by a previous one.
   * Note that in unreachable loops it might happen that we reach
   * load again, as well as we can fall into a cycle.
   * We break such cycles using a special visited flag.
   */
  INC_MASTER();
  for (pred = skip_Proj(mem); load != pred; pred = skip_Proj(get_Load_mem(pred))) {
    ldst_info_t *pred_info = get_irn_link(pred);

    /*
     * BEWARE: one might think that checking the modes is useless, because
     * if the pointers are identical, they refer to the same object.
     * This is only true in strong typed languages, not in C were the following
     * is possible a = *(type1 *)p; b = *(type2 *)p ...
     */

    if (get_irn_op(pred) == op_Store && get_Store_ptr(pred) == ptr &&
        get_irn_mode(get_Store_value(pred)) == load_mode) {
      /*
       * a Load immediately after a Store -- a read after write.
       * We may remove the Load, if both Load & Store does not have an exception handler
       * OR they are in the same block. In the latter case the Load cannot
       * throw an exception when the previous Store was quiet.
       *
       * Why we need to check for Store Exception? If the Store cannot
       * be executed (ROM) the exception handler might simply jump into
       * the load block :-(
       * We could make it a little bit better if we would know that the exception
       * handler of the Store jumps directly to the end...
       */
      if ((!pred_info->projs[pn_Store_X_except] && !info->projs[pn_Load_X_except]) ||
          get_nodes_block(load) == get_nodes_block(pred)) {
        ir_node *value = get_Store_value(pred);

        DBG_OPT_RAW(load, value);
        if (info->projs[pn_Load_M])
          exchange(info->projs[pn_Load_M], mem);

        /* no exception */
        if (info->projs[pn_Load_X_except]) {
          exchange( info->projs[pn_Load_X_except], new_Bad());
          res |= CF_CHANGED;
        }

        if (info->projs[pn_Load_res])
          exchange(info->projs[pn_Load_res], value);

        return res | DF_CHANGED;
      }
    }
    else if (get_irn_op(pred) == op_Load && get_Load_ptr(pred) == ptr &&
             get_Load_mode(pred) == load_mode) {
      /*
       * a Load after a Load -- a read after read.
       * We may remove the second Load, if it does not have an exception handler
       * OR they are in the same block. In the later case the Load cannot
       * throw an exception when the previous Load was quiet.
       *
       * Here, there is no need to check if the previous Load has an exception
       * hander because they would have exact the same exception...
       */
      if (! info->projs[pn_Load_X_except] || get_nodes_block(load) == get_nodes_block(pred)) {
        DBG_OPT_RAR(load, pred);

        if (pred_info->projs[pn_Load_res]) {
          /* we need a data proj from the previous load for this optimization */
          if (info->projs[pn_Load_res])
            exchange(info->projs[pn_Load_res], pred_info->projs[pn_Load_res]);

          if (info->projs[pn_Load_M])
            exchange(info->projs[pn_Load_M], mem);
        }
        else {
          if (info->projs[pn_Load_res]) {
            set_Proj_pred(info->projs[pn_Load_res], pred);
            set_nodes_block(info->projs[pn_Load_res], get_nodes_block(pred));
            pred_info->projs[pn_Load_res] = info->projs[pn_Load_res];
          }
          if (info->projs[pn_Load_M]) {
            /* Actually, this if should not be necessary.  Construct the Loads
               properly!!! */
            exchange(info->projs[pn_Load_M], mem);
          }
        }

        /* no exception */
        if (info->projs[pn_Load_X_except]) {
          exchange(info->projs[pn_Load_X_except], new_Bad());
          res |= CF_CHANGED;
        }

        return res |= DF_CHANGED;
      }
    }

    /* follow only Load chains */
    if (get_irn_op(pred) != op_Load)
      break;

    /* check for cycles */
    if (NODE_VISITED(pred_info))
      break;
    MARK_NODE(pred_info);
  }
  return res;
}

/**
 * optimize a Store
 */
static unsigned optimize_store(ir_node *store)
{
  ldst_info_t *info = get_irn_link(store);
  ir_node *pred, *mem, *ptr, *value, *block;
  ir_mode *mode;
  unsigned res = 0;

  if (get_Store_volatility(store) == volatility_is_volatile)
    return 0;

  /*
   * BEWARE: one might think that checking the modes is useless, because
   * if the pointers are identical, they refer to the same object.
   * This is only true in strong typed languages, not is C were the following
   * is possible *(type1 *)p = a; *(type2 *)p = b ...
   */

  ptr   = get_Store_ptr(store);

  /* Check, if the address of this load is used more than once.
   * If not, this load cannot be removed in any case. */
  if (get_irn_out_n(ptr) <= 1)
    return 0;

  block = get_nodes_block(store);
  mem   = get_Store_mem(store);
  value = get_Store_value(store);
  mode  = get_irn_mode(value);

  /* follow the memory chain as long as there are only Loads */
  INC_MASTER();
  for (pred = skip_Proj(mem); pred != store; pred = skip_Proj(get_Load_mem(pred))) {
    ldst_info_t *pred_info = get_irn_link(pred);

    if (get_irn_op(pred) == op_Store && get_Store_ptr(pred) == ptr &&
        get_nodes_block(pred) == block && get_irn_mode(get_Store_value(pred)) == mode) {
      /*
       * a Store after a Store in the same block -- a write after write.
       * We may remove the first Store, if it does not have an exception handler.
       *
       * TODO: What, if both have the same exception handler ???
       */
      if (get_Store_volatility(pred) != volatility_is_volatile && !pred_info->projs[pn_Store_X_except]) {
        DBG_OPT_WAW(pred, store);
        exchange( pred_info->projs[pn_Store_M], get_Store_mem(pred) );
        return DF_CHANGED;
      }
    }
    else if (get_irn_op(pred) == op_Load && get_Load_ptr(pred) == ptr &&
             value == pred_info->projs[pn_Load_res]) {
      /*
       * a Store of a value after a Load -- a write after read.
       * We may remove the second Store, if it does not have an exception handler.
       */
      if (! info->projs[pn_Store_X_except]) {
        DBG_OPT_WAR(store, pred);
        exchange( info->projs[pn_Store_M], mem );
        return DF_CHANGED;
      }
    }

    /* follow only Load chains */
    if (get_irn_op(pred) != op_Load)
      break;

    /* check for cycles */
    if (NODE_VISITED(pred_info))
      break;
    MARK_NODE(pred_info);
  }
  return res;
}

/**
 * walker, optimizes Phi after Stores to identical places:
 * Does the following optimization:
 * @verbatim
 *
 *   val1   val2   val3          val1  val2  val3
 *    |      |      |               \    |    /
 *   Str    Str    Str               \   |   /
 *      \    |    /                   PhiData
 *       \   |   /                       |
 *        \  |  /                       Str
 *          PhiM
 *
 * @endverbatim
 * This reduces the number of stores and allows for predicated execution.
 * Moves Stores back to the end of a function which may be bad.
 *
 * This is only possible if the predecessor blocks have only one successor.
 */
static unsigned optimize_phi(ir_node *phi, void *env)
{
  walk_env_t *wenv = env;
  int i, n;
  ir_node *store, *old_store, *ptr, *block, *phiM, *phiD, *exc, *projM;
  ir_mode *mode;
  ir_node **inM, **inD;
  int *idx;
  dbg_info *db = NULL;
  ldst_info_t *info;
  block_info_t *bl_info;
  unsigned res = 0;

  /* Must be a memory Phi */
  if (get_irn_mode(phi) != mode_M)
    return 0;

  n = get_Phi_n_preds(phi);
  if (n <= 0)
    return 0;

  store = skip_Proj(get_Phi_pred(phi, 0));
  old_store = store;
  if (get_irn_op(store) != op_Store)
    return 0;

  /* abort on dead blocks */
  if (is_Block_dead(get_nodes_block(store)))
    return 0;

  /* check if the block has only one successor */
  bl_info = get_irn_link(get_nodes_block(store));
  if (bl_info->flags)
    return 0;

  /* this is the address of the store */
  ptr  = get_Store_ptr(store);
  mode = get_irn_mode(get_Store_value(store));
  info = get_irn_link(store);
  exc  = info->exc_block;

  for (i = 1; i < n; ++i) {
    ir_node *pred = skip_Proj(get_Phi_pred(phi, i));

    if (get_irn_op(pred) != op_Store)
      return 0;

    if (ptr != get_Store_ptr(pred) || mode != get_irn_mode(get_Store_value(pred)))
      return 0;

    info = get_irn_link(pred);

    /* check, if all stores have the same exception flow */
    if (exc != info->exc_block)
      return 0;

    /* abort on dead blocks */
    if (is_Block_dead(get_nodes_block(store)))
      return 0;

    /* check if the block has only one successor */
    bl_info = get_irn_link(get_nodes_block(store));
    if (bl_info->flags)
      return 0;
  }

  /*
   * ok, when we are here, we found all predecessors of a Phi that
   * are Stores to the same address and size. That means whatever
   * we do before we enter the block of the Phi, we do a Store.
   * So, we can move the Store to the current block:
   *
   *   val1    val2    val3          val1  val2  val3
   *    |       |       |               \    |    /
   * | Str | | Str | | Str |             \   |   /
   *      \     |     /                   PhiData
   *       \    |    /                       |
   *        \   |   /                       Str
   *           PhiM
   *
   * Is only allowed if the predecessor blocks have only one successor.
   */

  /* first step: collect all inputs */
  NEW_ARR_A(ir_node *, inM, n);
  NEW_ARR_A(ir_node *, inD, n);
  NEW_ARR_A(int, idx, n);

  for (i = 0; i < n; ++i) {
    ir_node *pred = skip_Proj(get_Phi_pred(phi, i));
    info = get_irn_link(pred);

    inM[i] = get_Store_mem(pred);
    inD[i] = get_Store_value(pred);
    idx[i] = info->exc_idx;
  }
  block = get_nodes_block(phi);

  /* second step: create a new memory Phi */
  phiM = new_rd_Phi(get_irn_dbg_info(phi), current_ir_graph, block, n, inM, mode_M);

  /* third step: create a new data Phi */
  phiD = new_rd_Phi(get_irn_dbg_info(phi), current_ir_graph, block, n, inD, mode);

  /* fourth step: create the Store */
  store = new_rd_Store(db, current_ir_graph, block, phiM, ptr, phiD);
#ifdef DO_CACHEOPT
  co_set_irn_name(store, co_get_irn_ident(old_store));
#endif

  projM = new_rd_Proj(NULL, current_ir_graph, block, store, mode_M, pn_Store_M);

  info = get_ldst_info(store, wenv);
  info->projs[pn_Store_M] = projM;

  /* fifths step: repair exception flow */
  if (exc) {
    ir_node *projX = new_rd_Proj(NULL, current_ir_graph, block, store, mode_X, pn_Store_X_except);

    info->projs[pn_Store_X_except] = projX;
    info->exc_block                = exc;
    info->exc_idx                  = idx[0];

    for (i = 0; i < n; ++i) {
      set_Block_cfgpred(exc, idx[i], projX);
    }

    if (n > 1) {
      /* the exception block should be optimized as some inputs are identical now */
    }

    res |= CF_CHANGED;
  }

  /* sixth step: replace old Phi */
  exchange(phi, projM);

  return res | DF_CHANGED;
}

/**
 * walker, do the optimizations
 */
static void do_load_store_optimize(ir_node *n, void *env)
{
  walk_env_t *wenv = env;

  switch (get_irn_opcode(n)) {

  case iro_Load:
    wenv->changes |= optimize_load(n);
    break;

  case iro_Store:
    wenv->changes |= optimize_store(n);
    break;

  case iro_Phi:
    wenv->changes |= optimize_phi(n, env);

  default:
    ;
  }
}

/*
 * do the load store optimization
 */
void optimize_load_store(ir_graph *irg)
{
  walk_env_t env;

  assert(get_irg_phase_state(irg) != phase_building);
  assert(get_irg_pinned(irg) != op_pin_state_floats &&
    "LoadStore optimization needs pinned graph");

  if (! get_opt_redundant_loadstore())
    return;

  obstack_init(&env.obst);
  env.changes = 0;

  /* init the links, then collect Loads/Stores/Proj's in lists */
  master_visited = 0;
  irg_walk_graph(irg, firm_clear_link, collect_nodes, &env);

  /* now we have collected enough information, optimize */
  irg_walk_graph(irg, NULL, do_load_store_optimize, &env);

  obstack_free(&env.obst, NULL);

  /* Handle graph state */
  if (env.changes) {
    if (get_irg_outs_state(current_ir_graph) == outs_consistent)
      set_irg_outs_inconsistent(current_ir_graph);
  }

  if (env.changes & CF_CHANGED) {
    /* is this really needed: Yes, control flow changed, block might get Bad. */
    if (get_irg_dom_state(current_ir_graph) == dom_consistent)
      set_irg_dom_inconsistent(current_ir_graph);
  }
}
