/*
 * Project:     libFIRM
 * File name:   ir/ir/irvrfy.c
 * Purpose:     Check irnodes for correctness.
 * Author:      Christian Schaefer
 * Modified by: Goetz Lindenmaier. Till Riedel
 * Created:
 * CVS-ID:      $Id$
 * Copyright:   (c) 1998-2003 Universitšt Karlsruhe
 * Licence:     This file protected by GPL -  GNU GENERAL PUBLIC LICENSE.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

# include "irprog.h"
# include "irgraph_t.h"
# include "irvrfy.h"
# include "irgwalk.h"
# include "irdump.h"

#ifdef NDEBUG
/*
 * in RELEASE mode, returns ret if the expression expr evaluates to zero
 * in ASSERT mode, asserts the expression expr (and the string string).
 */
#define ASSERT_AND_RET(expr, string, ret)       if (!(expr)) return (ret)

/*
 * in RELEASE mode, returns ret if the expression expr evaluates to zero
 * in ASSERT mode, executes blk if the expression expr evaluates to zero and asserts expr
 */
#define ASSERT_AND_RET_DBG(expr, string, ret, blk)      if (!(expr)) return (ret)
#else
#define ASSERT_AND_RET(expr, string, ret) \
do { \
  if (opt_do_node_verification == NODE_VERIFICATION_ON) {\
    if (!(expr) && current_ir_graph != get_const_code_irg()) \
    dump_ir_block_graph(current_ir_graph, "-assert"); \
    assert((expr) && string); } \
  if (!(expr)) { \
    if (opt_do_node_verification == NODE_VERIFICATION_REPORT) \
      fprintf(stderr, #expr " : " string "\n"); \
    bad_msg = #expr " && " string; \
    return (ret); \
  } \
} while(0)

#define ASSERT_AND_RET_DBG(expr, string, ret, blk) \
do { \
  if (!(expr)) { \
    bad_msg = #expr " && " string; \
    if (opt_do_node_verification != NODE_VERIFICATION_ERROR_ONLY) { blk; } \
    if (opt_do_node_verification == NODE_VERIFICATION_REPORT) \
      fprintf(stderr, #expr " : " string "\n"); \
    else if (opt_do_node_verification == NODE_VERIFICATION_ON) \
      assert((expr) && string); \
    return (ret); \
  } \
} while(0)

#endif

/** if this flag is set, verify entity types in Load & Store nodes */
static int vrfy_entities = 0;

/* @@@ replace use of array "in" by access functions. */
ir_node **get_irn_in(ir_node *node);

static node_verification_t opt_do_node_verification = NODE_VERIFICATION_ON;
static const char *bad_msg;

/* enable verification of Load/Store entities */
void vrfy_enable_entity_tests(int enable)
{
  vrfy_entities = enable;
}

/**
 * little helper for NULL modes
 */
static const char *get_mode_name_ex(ir_mode *mode)
{
  if (! mode)
    return "<no mode>";
  return get_mode_name(mode);
}

void do_node_verification(node_verification_t mode)
{
  opt_do_node_verification = mode;
}

/**
 * Prints a failure for a Node
 */
static void show_node_failure(ir_node *n)
{
  fprintf(stderr, "\nFIRM: irn_vrfy_irg() of node %ld %s%s\n" ,
    get_irn_node_nr(n),
    get_irn_opname(n), get_irn_modename(n)
  );
}

/**
 * Prints a failure message for a binop
 */
static void show_binop_failure(ir_node *n, const char *text)
{
  ir_node *left  = get_binop_left(n);
  ir_node *right = get_binop_right(n);

  fprintf(stderr, "\nFIRM: irn_vrfy_irg() of node %ld %s%s(%s%s, %s%s) did not match (%s)\n",
      get_irn_node_nr(n),
      get_irn_opname(n), get_irn_modename(n),
      get_irn_opname(left), get_irn_modename(left),
      get_irn_opname(right), get_irn_modename(right),
      text);
}

/**
 * Prints a failure message for an unop
 */
static void show_unop_failure(ir_node *n, const char *text)
{
  ir_node *op  = get_unop_op(n);

  fprintf(stderr, "\nFIRM: irn_vrfy_irg() of node %ld %s%s(%s%s) did not match (%s)\n",
      get_irn_node_nr(n),
      get_irn_opname(n), get_irn_modename(n),
      get_irn_opname(op), get_irn_modename(op),
      text);
}

/**
 * Prints a failure message for a proj
 */
static void show_proj_failure(ir_node *n)
{
  ir_node *op  = get_Proj_pred(n);
  int proj     = get_Proj_proj(n);

  fprintf(stderr, "\nFIRM: irn_vrfy_irg() of node %ld %s%s %d(%s%s) failed\n" ,
      get_irn_node_nr(n),
      get_irn_opname(n), get_irn_modename(n), proj,
      get_irn_opname(op), get_irn_modename(op));
}

/**
 * Prints a failure message for a proj
 */
static void show_proj_failure_ent(ir_node *n, entity *ent)
{
  ir_node *op  = get_Proj_pred(n);
  int proj     = get_Proj_proj(n);
  ir_mode *m   = get_type_mode(get_entity_type(ent));

  fprintf(stderr, "\nFIRM: irn_vrfy_irg() of node %ld %s%s %d(%s%s) entity %s(type %s mode %s)failed\n" ,
      get_irn_node_nr(n),
      get_irn_opname(n), get_irn_modename(n), proj,
      get_irn_opname(op), get_irn_modename(op),
      get_entity_name(ent), get_type_name(get_entity_type(ent)),
      get_mode_name_ex(m));
}

/**
 * Show a node and a graph
 */
static void show_node_on_graph(ir_graph *irg, ir_node *n)
{
  entity *ent = get_irg_entity(irg);

  if (ent)
    fprintf(stderr, "\nFIRM: irn_vrfy_irg() of entity %s, node %ld %s%s\n",
      get_entity_name(ent),
      get_irn_node_nr(n), get_irn_opname(n), get_irn_modename(n));
  else
    fprintf(stderr, "\nFIRM: irn_vrfy_irg() of graph %p, node %ld %s%s\n",
      (void *)irg,
      get_irn_node_nr(n), get_irn_opname(n), get_irn_modename(n));
}

/**
 * Show call params
 */
static void show_call_param(ir_node *n, type *mt)
{
  int i;

  fprintf(stderr, "\nFIRM: irn_vrfy_irg() Call type-check failed: %s(", get_type_name(mt));
  for (i = 0; i < get_method_n_params(mt); ++i) {
    fprintf(stderr, "%s ", get_mode_name_ex(get_type_mode(get_method_param_type(mt, i))));
  }
  fprintf(stderr, ") != CALL(");

  for (i = 0; i < get_Call_n_params(n); ++i) {
    fprintf(stderr, "%s ", get_mode_name_ex(get_irn_mode(get_Call_param(n, i))));
  }
  fprintf(stderr, ")\n");

}

/**
 * Show return modes
 */
static void show_return_modes(ir_graph *irg, ir_node *n, type *mt, int i)
{
  entity *ent = get_irg_entity(irg);

  fprintf(stderr, "\nFIRM: irn_vrfy_irg() Return node %ld in entity \"%s\" mode %s different from type mode %s\n",
    get_irn_node_nr(n), get_entity_name(ent),
    get_mode_name_ex(get_irn_mode(get_Return_res(n, i))),
    get_mode_name_ex(get_type_mode(get_method_res_type(mt, i)))
  );
}

/**
 * Show return number of results
 */
static void show_return_nres(ir_graph *irg, ir_node *n, type *mt)
{
  entity *ent = get_irg_entity(irg);

  fprintf(stderr, "\nFIRM: irn_vrfy_irg() Return node %ld in entity \"%s\" has %d results different from type %d\n",
    get_irn_node_nr(n), get_entity_name(ent),
    get_Return_n_ress(n), get_method_n_ress(mt));
}

/**
 * Show Phi input
 */
static void show_phi_failure(ir_node *phi, ir_node *pred, int pos)
{
  fprintf(stderr, "\nFIRM: irn_vrfy_irg() Phi node %ld has mode %s different from predeccessor node %ld mode %s\n",
    get_irn_node_nr(phi), get_mode_name_ex(get_irn_mode(phi)),
    get_irn_node_nr(pred), get_mode_name_ex(get_irn_mode(pred)));
}

/**
 * Show Phi inputs
 */
static void show_phi_inputs(ir_node *phi, ir_node *block)
{
  fprintf(stderr, "\nFIRM: irn_vrfy_irg() Phi node %ld has %d inputs, its Block %ld has %d\n",
    get_irn_node_nr(phi),   get_irn_arity(phi),
    get_irn_node_nr(block), get_irn_arity(block));
}

/* If the address is Sel or SymConst, return the entity. */
static entity *get_ptr_entity(ir_node *ptr) {
  if (get_irn_op(ptr) == op_Sel) {
    return get_Sel_entity(ptr);
  } else if ((get_irn_op(ptr) == op_SymConst) && (get_SymConst_kind(ptr) == symconst_addr_ent)) {
    return get_SymConst_entity(ptr);
  }
  return NULL;
}

/**
 * verify the Proj number
 */
static int
vrfy_Proj_proj(ir_node *p, ir_graph *irg) {
  ir_node *pred;
  ir_mode *mode;
  int proj;

  pred = skip_Id(get_Proj_pred(p));
  ASSERT_AND_RET(get_irn_mode(pred) == mode_T, "mode of a 'projed' node is not Tuple", 0);
  mode = get_irn_mode(p);
  proj = get_Proj_proj(p);

  switch (get_irn_opcode(pred)) {
    case iro_Start:
      ASSERT_AND_RET_DBG(
          (
           (proj == pn_Start_X_initial_exec && mode == mode_X) ||
           (proj == pn_Start_M         && mode == mode_M) ||
           (proj == pn_Start_P_frame_base && mode_is_reference(mode)) ||
           (proj == pn_Start_P_globals && mode_is_reference(mode)) ||
           (proj == pn_Start_T_args    && mode == mode_T) ||
           (proj == pn_Start_P_value_arg_base && mode_is_reference(mode)) ||
           (proj == pn_Start_P_value_arg_base && mode == mode_T)    /* FIXME: only one of those */
          ),
          "wrong Proj from Start", 0,
      show_proj_failure(p);
      );
      break;

    case iro_Cond:
      ASSERT_AND_RET_DBG(
	(
          (proj >= 0 && mode == mode_X && get_irn_mode(get_Cond_selector(pred)) == mode_b) ||	/* compare */
	  (mode == mode_X && mode_is_int(get_irn_mode(get_Cond_selector(pred))))		/* switch */
	),
        "wrong Proj from Cond", 0,
        show_proj_failure(p);
      );
      break;

    case iro_Raise:
      ASSERT_AND_RET_DBG(
        ((proj == pn_Raise_X && mode == mode_X) || (proj == pn_Raise_M && mode == mode_M)),
        "wrong Proj from Raise", 0,
    show_proj_failure(p);
      );
      break;

    case iro_InstOf:
      ASSERT_AND_RET_DBG(
    (proj >= 0 && mode == mode_X),
    "wrong Proj from InstOf", 0,
    show_proj_failure(p);
      );
      break;

    case iro_Call:
      ASSERT_AND_RET_DBG(
        ((proj == pn_Call_M_regular        && mode == mode_M) ||
         (proj == pn_Call_X_except         && mode == mode_X) ||
         (proj == pn_Call_T_result         && mode == mode_T) ||
         (proj == pn_Call_M_except         && mode == mode_M) ||
     (proj == pn_Call_P_value_res_base && mode == mode_P)),
        "wrong Proj from Call", 0,
        show_proj_failure(p);
      );

      break;

    case iro_Quot:
      ASSERT_AND_RET_DBG(
        ((proj == pn_Quot_M        && mode == mode_M) ||
         (proj == pn_Quot_X_except && mode == mode_X) ||
         (proj == pn_Quot_res      && mode_is_float(mode))),
        "wrong Proj from Quot", 0,
    show_proj_failure(p);
      );
      break;

    case iro_DivMod:
      ASSERT_AND_RET_DBG(
        ((proj == pn_DivMod_M        && mode == mode_M) ||
         (proj == pn_DivMod_X_except && mode == mode_X) ||
         (proj == pn_DivMod_res_div  && mode_is_int(mode)) ||
         (proj == pn_DivMod_res_mod  && mode_is_int(mode))),
        "wrong Proj from DivMod", 0,
    show_proj_failure(p);
      );
      break;

    case iro_Div:
      ASSERT_AND_RET_DBG(
        ((proj == pn_Div_M        && mode == mode_M) ||
         (proj == pn_Div_X_except && mode == mode_X) ||
         (proj == pn_Div_res      && mode_is_int(mode))),
        "wrong Proj from Div or Mod", 0,
	show_proj_failure(p);
      );
      break;

    case iro_Mod:
      ASSERT_AND_RET_DBG(
        ((proj == pn_Mod_M        && mode == mode_M) ||
         (proj == pn_Mod_X_except && mode == mode_X) ||
         (proj == pn_Mod_res      && mode_is_int(mode))),
        "wrong Proj from Div or Mod", 0,
    show_proj_failure(p);
      );
      break;

    case iro_Cmp:
      ASSERT_AND_RET_DBG(
        (proj >= 0 && proj <= 15 && mode == mode_b),
        "wrong Proj from Cmp", 0,
    show_proj_failure(p);
      );
      break;

    case iro_Load:
      if (proj == pn_Load_res) {
	ir_node *ptr = get_Load_ptr(pred);
	entity *ent = get_ptr_entity(ptr);

	if (vrfy_entities && ent && get_irg_phase_state(current_ir_graph) == phase_high) {
	  /* do NOT check this for lowered phases, see comment on Store */
	  ASSERT_AND_RET_DBG(
			     (mode == get_type_mode(get_entity_type(ent))),
			     "wrong data Proj from Load, entity type_mode failed", 0,
			     show_proj_failure_ent(p, ent);
			     );
	}
	else {
	  ASSERT_AND_RET_DBG(
			     mode_is_data(mode) && mode == get_Load_mode(pred),
			     "wrong data Proj from Load", 0,
			     show_proj_failure(p);
			     );
	}
      } else {
	ASSERT_AND_RET_DBG(
			   ((proj == pn_Load_M        && mode == mode_M) ||
			    (proj == pn_Load_X_except && mode == mode_X)),
			   "wrong Proj from Load", 0,
			   show_proj_failure(p);
			   );
      }
      break;

    case iro_Store:
      ASSERT_AND_RET_DBG(
        ((proj == pn_Store_M        && mode == mode_M) ||
         (proj == pn_Store_X_except && mode == mode_X)),
        "wrong Proj from Store", 0,
        show_proj_failure(p);
      );
      break;

    case iro_Alloc:
      ASSERT_AND_RET_DBG(
        (
         (proj == pn_Alloc_M        && mode == mode_M) ||
         (proj == pn_Alloc_X_except /* && mode == mode_X*/) ||
         (proj == pn_Alloc_res      && mode_is_reference(mode))
        ),
        "wrong Proj from Alloc", 0,
        show_proj_failure(p);
      );
      break;

    case iro_Proj:
      {
        type *mt; /* A method type */
        long nr = get_Proj_proj(pred);

        pred = skip_Id(get_Proj_pred(pred));
        ASSERT_AND_RET((get_irn_mode(pred) == mode_T), "Proj from something not a tuple", 0);
        switch (get_irn_opcode(pred))
        {
          case iro_Start:
            mt = get_entity_type(get_irg_entity(irg));

        if (nr == pn_Start_T_args) {
              ASSERT_AND_RET(
                  (proj >= 0 && mode_is_data(mode)),
                  "wrong Proj from Proj from Start", 0);
              ASSERT_AND_RET(
                (proj < get_method_n_params(mt)),
                "More Projs for args than args in type", 0
              );
              if ((mode_is_reference(mode)) && is_compound_type(get_method_param_type(mt, proj)))
                /* value argument */ break;

              ASSERT_AND_RET(
                  (mode == get_type_mode(get_method_param_type(mt, proj))),
                  "Mode of Proj from Start doesn't match mode of param type.", 0);
            }
        else if (nr == pn_Start_P_value_arg_base) {
          ASSERT_AND_RET(
                  (proj >= 0 && mode_is_reference(mode)),
                  "wrong Proj from Proj from Start", 0
              );
              ASSERT_AND_RET(
                (proj < get_method_n_params(mt)),
                "More Projs for args than args in type", 0
              );
        }
            break;

          case iro_Call:
            {
              ASSERT_AND_RET(
                  (proj >= 0 && mode_is_data(mode)),
                  "wrong Proj from Proj from Call", 0);
              mt = get_Call_type(pred);
              ASSERT_AND_RET(
                  (proj < get_method_n_ress(mt)),
                  "More Projs for results than results in type.", 0);
              if ((mode_is_reference(mode)) && is_compound_type(get_method_res_type(mt, proj)))
                /* value result */ break;

              ASSERT_AND_RET(
                  (mode == get_type_mode(get_method_res_type(mt, proj))),
                  "Mode of Proj from Call doesn't match mode of result type.", 0);

            }
            break;

          case iro_Tuple:
            /* We don't test */
            break;

          default:
            ASSERT_AND_RET(0, "Unknown opcode", 0);
        }
        break;

      }
    case iro_Tuple:
      /* We don't test */
      break;

    case iro_CallBegin:
      break;

    case iro_EndReg:
      break;

    case iro_EndExcept:
      break;

    default:
      ASSERT_AND_RET(0, "Unknown opcode", 0);
  }

  /* all went ok */
  return 1;
}

int irn_vrfy_irg(ir_node *n, ir_graph *irg)
{
  int i;
  int opcode, opcode1;
  ir_mode *mymode, *op1mode = NULL, *op2mode, *op3mode;
  int op_is_symmetric = 1;  /*  0: asymmetric
                                1: operands have identical modes
                                2: modes of operands == mode of this node */
  type *mt;                 /* A method type */
  entity *ent;

  ir_node **in;

  if (!opt_do_node_verification) return 1;

  if (! get_interprocedural_view()) {
    /*
     * do NOT check placement in interprocedural view, as we don't always know
     * the "right" graph ...
     */
    ASSERT_AND_RET_DBG(
                       node_is_in_irgs_storage(irg, n),
                       "Node is not stored on proper IR graph!", 0,
                       show_node_on_graph(irg, n);
                       );
  }

  opcode = get_irn_opcode(n);

  /* We don't want to test nodes whose predecessors are Bad,
     as we would have to special case that for each operation. */
  if (opcode != iro_Phi && opcode != iro_Block)
    for (i = 0; i < get_irn_arity(n); i++) {
      opcode1 = get_irn_opcode(get_irn_n(n, i));
      if (opcode1 == iro_Bad)
        return 1;
    }

  mymode = get_irn_mode(n);
  in = get_irn_in(n);

  switch (opcode) {

    case iro_Block:
      for (i = 0; i < get_Block_n_cfgpreds(n); ++i) {
        ir_node *pred =  get_Block_cfgpred(n, i);
        ASSERT_AND_RET(
                       (is_Bad(pred)     ||
                        is_Unknown(pred) ||
                        (get_irn_mode(pred) == mode_X)
                        ), "Block node", 0);
      }
      /*  End block may only have Return, Raise or fragile ops as preds. */
      if (n == get_irg_end_block(irg))
        for (i = 0; i < get_Block_n_cfgpreds(n); ++i) {
          ir_node *pred =  skip_Proj(get_Block_cfgpred(n, i));
          if (is_Proj(pred) || get_irn_op(pred) == op_Tuple)
            break;   /*  We can not test properly.  How many tuples are there? */
          ASSERT_AND_RET(((get_irn_op(pred) == op_Return) ||
                          is_Bad(pred)                    ||
                          (get_irn_op(pred) == op_Raise)  ||
                          is_fragile_op(pred)               ),
                         "End Block node", 0);
        }
      /*  irg attr must == graph we are in. */
      if (! get_interprocedural_view()) {
        ASSERT_AND_RET(((get_irn_irg(n) && get_irn_irg(n) == irg)), "Block node has wrong irg attribute", 0);
      }

      break;

    case iro_Start:
      ASSERT_AND_RET(
                     /* Start: BB --> X x M x ref x data1 x ... x datan x ref */
                     mymode == mode_T, "Start node", 0
                     );
      break;

    case iro_Jmp:
      ASSERT_AND_RET(
                     /* Jmp: BB --> X */
                     mymode == mode_X, "Jmp node", 0
                     );
      break;

    case iro_Break:
      ASSERT_AND_RET(
                     /* Jmp: BB --> X */
                     mymode == mode_X, "Jmp node", 0
                     );
      break;

    case iro_Cond:
      op1mode = get_irn_mode(in[1]);
      ASSERT_AND_RET(
                     /* Cond: BB x b --> X x X */
                     (op1mode == mode_b ||
                      /* Cond: BB x int --> X^n */
                      mode_is_int(op1mode) ),  "Cond node", 0
                     );
      ASSERT_AND_RET(mymode == mode_T, "Cond mode is not a tuple", 0);
      break;

    case iro_Return:
      op1mode = get_irn_mode(in[1]);
      /* Return: BB x M x data1 x ... x datan --> X */
      /* printf("mode: %s, code %s\n", ID_TO_STR(n->mode->name), ID_TO_STR(n->op->name));*/
      ASSERT_AND_RET( op1mode == mode_M, "Return node", 0 );  /* operand M */
      for (i = 2; i < get_irn_arity(n); i++) {
        ASSERT_AND_RET( mode_is_data(get_irn_mode(in[i])), "Return node", 0 );  /* operand datai */
      };
      ASSERT_AND_RET( mymode == mode_X, "Result X", 0 );   /* result X */
      /* Compare returned results with result types of method type */
      mt = get_entity_type(get_irg_entity(irg));
      ASSERT_AND_RET_DBG( get_Return_n_ress(n) == get_method_n_ress(mt),
        "Number of results for Return doesn't match number of results in type.", 0,
      show_return_nres(irg, n, mt););
      for (i = 0; i < get_Return_n_ress(n); i++) {
    type *res_type = get_method_res_type(mt, i);

        if (is_atomic_type(res_type)) {
      ASSERT_AND_RET_DBG(
        get_irn_mode(get_Return_res(n, i)) == get_type_mode(res_type),
        "Mode of result for Return doesn't match mode of result type.", 0,
        show_return_modes(irg, n, mt, i);
      );
    }
    else {
      ASSERT_AND_RET_DBG(
        mode_is_reference(get_irn_mode(get_Return_res(n, i))),
        "Mode of result for Return doesn't match mode of result type.", 0,
        show_return_modes(irg, n, mt, i);
      );
    }
      }
      break;

    case iro_Raise:
      op1mode = get_irn_mode(in[1]);
      op2mode = get_irn_mode(in[2]);
      ASSERT_AND_RET(
                     /* Sel: BB x M x ref --> X x M */
                     op1mode == mode_M && mode_is_reference(op2mode) &&
                     mymode == mode_T, "Raise node", 0
                     );
      break;

    case iro_Const: {
      ASSERT_AND_RET(
		     /* Const: BB --> data */
		     (mode_is_data (mymode) ||
		      mymode == mode_b)      /* we want boolean constants for static evaluation */
		     ,"Const node", 0        /* of Cmp. */
		     );
      } break;
    case iro_SymConst:
      if (get_SymConst_kind(n) == symconst_addr_ent) {
	entity *ent = get_SymConst_entity(n);
	if (is_method_type(get_entity_type(ent)) &&
	    get_irn_irg(n) != get_const_code_irg()) {
#if 1
	  ASSERT_AND_RET((get_entity_peculiarity(ent) != peculiarity_description),
			 "A constant must address an existing method.", 0);
#endif
	}
      }
      ASSERT_AND_RET(
                     /* SymConst: BB --> int*/
                     (mode_is_int(mymode) ||
                      /* SymConst: BB --> ref */
                      mode_is_reference(mymode))
                     ,"SymConst node", 0);
      break;

    case iro_Sel:
      op1mode = get_irn_mode(in[1]);
      op2mode = get_irn_mode(in[2]);
      ASSERT_AND_RET_DBG(
                         /* Sel: BB x M x ref x int^n --> ref */
                         (op1mode == mode_M && op2mode == mymode && mode_is_reference(mymode)),
                         "Sel node", 0, show_node_failure(n)
                         );
      for (i=3; i < get_irn_arity(n); i++)
        {
          ASSERT_AND_RET_DBG(mode_is_int(get_irn_mode(in[i])), "Sel node", 0, show_node_failure(n));
        }
      ent = get_Sel_entity(n);
      ASSERT_AND_RET_DBG(ent, "Sel node with empty entity", 0, show_node_failure(n));
      break;

    case iro_InstOf:
      ASSERT_AND_RET(mode_T == mymode, "mode of Instof is not a tuple", 0);
      ASSERT_AND_RET(mode_is_data(op1mode), "Instof not on data", 0);
      break;

    case iro_Call:
      op1mode = get_irn_mode(in[1]);
      op2mode = get_irn_mode(in[2]);
      /* Call: BB x M x ref x data1 x ... x datan
         --> M x datan+1 x ... x data n+m */
      ASSERT_AND_RET( op1mode == mode_M && mode_is_reference(op2mode), "Call node", 0 );  /* operand M x ref */

      mt = get_Call_type(n);
      if(get_unknown_type() == mt) {
	break;
      }

      for (i=3; i < get_irn_arity(n); i++) {
        ASSERT_AND_RET( mode_is_data(get_irn_mode(in[i])), "Call node", 0 );  /* operand datai */
      };
      ASSERT_AND_RET( mymode == mode_T, "Call result not a tuple", 0 );   /* result T */
      /* Compare arguments of node with those of type */

      if (get_method_variadicity(mt) == variadicity_variadic) {
        ASSERT_AND_RET_DBG(
                           get_Call_n_params(n) >= get_method_n_params(mt),
                           "Number of args for Call doesn't match number of args in variadic type.",
                           0,
                           fprintf(stderr, "Call has %d params, method %s type %d\n",
                                   get_Call_n_params(n), get_type_name(mt), get_method_n_params(mt));
                           );
      }
      else {
        ASSERT_AND_RET(
                       get_Call_n_params(n) == get_method_n_params(mt),
                       "Number of args for Call doesn't match number of args in non variadic type.",
                       0);
      }

      for (i = 0; i < get_method_n_params(mt); i++) {
	type *t = get_method_param_type(mt, i);

	if (is_atomic_type(t)) {
	  ASSERT_AND_RET_DBG(
			     get_irn_mode(get_Call_param(n, i)) == get_type_mode(t),
			     "Mode of arg for Call doesn't match mode of arg type.", 0,
			     show_call_param(n, mt);
			     );
	}
	else {
	  /* call with a compound type, mode must be reference */
	  ASSERT_AND_RET_DBG(
			     mode_is_reference(get_irn_mode(get_Call_param(n, i))),
			     "Mode of arg for Call doesn't match mode of arg type.", 0,
			     show_call_param(n, mt);
			     );
	}
      }

#if 0
      if (Call_has_callees(n)) {
	for (i = 0; i < get_Call_n_callees(n); i++) {
	  ASSERT_AND_RET(is_entity(get_Call_callee(n, i)), "callee array must contain entities.", 0);
	}
      }
#endif
      break;

    case iro_Add:
      op1mode = get_irn_mode(in[1]);
      op2mode = get_irn_mode(in[2]);
      ASSERT_AND_RET_DBG(
                         (
                          /* common Add: BB x numP x numP --> numP */
                          (op1mode == mymode && op2mode == op1mode && mode_is_numP(mymode)) ||
                          /* Pointer Add: BB x ref x int --> ref */
                          (mode_is_reference(op1mode) && mode_is_int(op2mode) && op1mode == mymode) ||
                          /* Pointer Add: BB x int x ref --> ref */
                          (mode_is_int(op1mode) && op2mode == mymode && mode_is_reference(mymode))
                          ),
                         "Add node", 0,
                         show_binop_failure(n, "/* common Add: BB x numP x numP --> numP */ |\n"
                                            "/* Pointer Add: BB x ref x int --> ref */   |\n"
                                            "/* Pointer Add: BB x int x ref --> ref */");
                         );
      if (mode_is_reference(op1mode) != mode_is_reference(op2mode)) {
        /* BB x ref x int --> ref or BB x int x ref --> ref */
        op_is_symmetric = 0;
      } else {
        /* BB x num x num --> num or BB x ref x ref */
        op_is_symmetric = 2;
      }
      break;

    case iro_Sub:
      op1mode = get_irn_mode(in[1]);
      op2mode = get_irn_mode(in[2]);
      ASSERT_AND_RET_DBG(
                         /* common Sub: BB x numP x numP --> numP */
                         ((mymode ==op1mode && mymode == op2mode && mode_is_numP(op1mode)) ||
                          /* Pointer Sub: BB x ref x int --> ref */
                          (op1mode == mymode && mode_is_int(op2mode) && mode_is_reference(mymode)) ||
                          /* Pointer Sub: BB x int x ref --> ref */
                          (mode_is_int(op1mode) && op2mode == mymode && mode_is_reference(mymode)) ||
                          /* Pointer Sub: BB x ref x ref --> int */
                          (op1mode == op2mode && mode_is_reference(op2mode) && mode_is_int(mymode))),
                         "Sub node", 0,
                         show_binop_failure(n, "/* common Sub: BB x numP x numP --> numP */ |\n"
                                            "/* Pointer Sub: BB x ref x int --> ref */   |\n"
                                            "/* Pointer Sub: BB x int x ref --> ref */   |\n"
                                            "/* Pointer Sub: BB x ref x ref --> int */" );
                         );
      if (mode_is_reference(op1mode) != mode_is_reference(op2mode)) {
        op_is_symmetric = 0;
      } else {
        op_is_symmetric = 2;
      }
      break;

    case iro_Minus:
      op1mode = get_irn_mode(in[1]);
      ASSERT_AND_RET_DBG(
                         /* Minus: BB x float --> float */
                         op1mode == mymode && mode_is_float(op1mode), "Minus node", 0,
                         show_unop_failure(n , "/* Minus: BB x float --> float */");
                         );
      op_is_symmetric = 2;
      break;

    case iro_Mul:
      op1mode = get_irn_mode(in[1]);
      op2mode = get_irn_mode(in[2]);
      ASSERT_AND_RET_DBG(
                         /* Mul: BB x int1 x int1 --> int2 */
                         ((mode_is_int(op1mode)   && op2mode == op1mode && mode_is_int(mymode)) ||
                         /* Mul: BB x float x float --> float */
                          (mode_is_float(op1mode) && op2mode == op1mode && mymode == op1mode)),
                         "Mul node",0,
                         show_binop_failure(n, "/* Mul: BB x int1 x int1 --> int2 */ |\n"
                                               "/* Mul: BB x float x float --> float */");
                         );
      op_is_symmetric = 2;
      break;

    case iro_Quot:
      op1mode = get_irn_mode(in[1]);
      op2mode = get_irn_mode(in[2]);
      op3mode = get_irn_mode(in[3]);
      ASSERT_AND_RET_DBG(
                         /* Quot: BB x M x float x float --> M x X x float */
                         op1mode == mode_M && op2mode == op3mode &&
                         get_mode_sort(op2mode) == irms_float_number &&
                         mymode == mode_T,
                         "Quot node",0,
                         show_binop_failure(n, "/* Quot: BB x M x float x float --> M x X x float */");
                         );
      op_is_symmetric = 2;
      break;

    case iro_DivMod:
      op1mode = get_irn_mode(in[1]);
      op2mode = get_irn_mode(in[2]);
      op3mode = get_irn_mode(in[3]);
      ASSERT_AND_RET(
                     /* DivMod: BB x M x int x int --> M x X x int x int */
                     op1mode == mode_M &&
                     mode_is_int(op2mode) &&
                     op3mode == op2mode &&
                     mymode == mode_T,
                     "DivMod node", 0
                     );
      op_is_symmetric = 1;
      break;

    case iro_Div:
    case iro_Mod:
      op1mode = get_irn_mode(in[1]);
      op2mode = get_irn_mode(in[2]);
      op3mode = get_irn_mode(in[3]);
      ASSERT_AND_RET(
                     /* Div or Mod: BB x M x int x int --> M x X x int */
                     op1mode == mode_M &&
                     op2mode == op3mode &&
                     mode_is_int(op2mode) &&
                     mymode == mode_T,
                     "Div or Mod node", 0
                     );
      op_is_symmetric = 1;
      break;

    case iro_Abs:
      op1mode = get_irn_mode(in[1]);
      ASSERT_AND_RET_DBG(
                         /* Abs: BB x num --> num */
                         op1mode == mymode &&
                         mode_is_num (op1mode),
                         "Abs node", 0,
                         show_unop_failure(n, "/* Abs: BB x num --> num */");
                         );
      op_is_symmetric = 2;
      break;

    case iro_And:
    case iro_Or:
    case iro_Eor:
      op1mode = get_irn_mode(in[1]);
      op2mode = get_irn_mode(in[2]);
      ASSERT_AND_RET_DBG(
                         /* And or Or or Eor: BB x int x int --> int */
                         mode_is_int(mymode) &&
                         op2mode == op1mode &&
                         mymode == op2mode,
                         "And, Or or Eor node", 0,
                         show_binop_failure(n, "/* And or Or or Eor: BB x int x int --> int */");
                         );
      op_is_symmetric = 2;
      break;

    case iro_Not:
      op1mode = get_irn_mode(in[1]);
      ASSERT_AND_RET_DBG(
                         /* Not: BB x int --> int */
                         mode_is_int(mymode) &&
                         mymode == op1mode,
                         "Not node", 0,
                         show_unop_failure(n, "/* Not: BB x int --> int */");
                         );
      op_is_symmetric = 2;
      break;


    case iro_Cmp:
      op1mode = get_irn_mode(in[1]);
      op2mode = get_irn_mode(in[2]);
      ASSERT_AND_RET_DBG(
                         /* Cmp: BB x datab x datab --> b16 */
                         mode_is_data (op1mode) &&
                         op2mode == op1mode &&
                         mymode == mode_T,
                         "Cmp node", 0,
                         show_binop_failure(n, "/* Cmp: BB x datab x datab --> b16 */");
                         );
      break;

    case iro_Shl:
    case iro_Shr:
    case iro_Shrs:
      op1mode = get_irn_mode(in[1]);
      op2mode = get_irn_mode(in[2]);
      ASSERT_AND_RET_DBG(
                         /* Shl, Shr or Shrs: BB x int x int_u --> int */
                         mode_is_int(op1mode) &&
                         mode_is_int(op2mode) &&
                         !mode_is_signed(op2mode) &&
                         mymode == op1mode,
                         "Shl, Shr, Shr or Rot node", 0,
                         show_binop_failure(n, "/* Shl, Shr or Shrs: BB x int x int_u --> int */");
                         );
      break;

    case iro_Rot:
      op1mode = get_irn_mode(in[1]);
      op2mode = get_irn_mode(in[2]);
      ASSERT_AND_RET_DBG(
                         /* Rot: BB x int x int --> int */
                         mode_is_int(op1mode) &&
                         mode_is_int(op2mode) &&
                         mymode == op1mode,
                         "Rot node", 0,
                         show_binop_failure(n, "/* Rot: BB x int x int --> int */");
                         );
      break;

    case iro_Conv:
      op1mode = get_irn_mode(in[1]);
      ASSERT_AND_RET_DBG(
                         /* Conv: BB x datab1 --> datab2 */
                         mode_is_datab(op1mode) && mode_is_data(mymode),
                         "Conv node", 0,
                         show_unop_failure(n, "/* Conv: BB x datab1 --> datab2 */");
                         );
      break;

    case iro_Cast:
      op1mode = get_irn_mode(in[1]);
      ASSERT_AND_RET_DBG(
                         /* Conv: BB x datab1 --> datab2 */
                         mode_is_data(op1mode) && op1mode == mymode,
                         "Cast node", 0,
                         show_unop_failure(n, "/* Conv: BB x datab1 --> datab2 */");
                         );
      break;

    case iro_Phi:
    {
      ir_node *block = get_nodes_block(n);

      if (! is_Bad(block) && get_irg_phase_state(get_irn_irg(n)) != phase_building) {
        /* a Phi node MUST have the same number of inputs as its block */
        ASSERT_AND_RET_DBG(
	  get_irn_arity(n) == get_irn_arity(block),
	  "wrong number of inputs in Phi node", 0,
	  show_phi_inputs(n, block);
	);
      }

      /* Phi: BB x dataM^n --> dataM */
      for (i = 1; i < get_irn_arity(n); i++) {
        if (!is_Bad(in[i]) && (get_irn_op(in[i]) != op_Unknown))
          ASSERT_AND_RET_DBG(
                             get_irn_mode(in[i]) == mymode,
                             "Phi node", 0,
                             show_phi_failure(n, in[i], i);
                             );
      };
      ASSERT_AND_RET( mode_is_dataM(mymode), "Phi node", 0 );
      break;
    }
    case iro_Load:
      op1mode = get_irn_mode(in[1]);
      op2mode = get_irn_mode(in[2]);
      ASSERT_AND_RET(
                     /* Load: BB x M x ref --> M x X x data */
                     op1mode == mode_M && mode_is_reference(op2mode),
                     "Load node", 0
                     );
      ASSERT_AND_RET( mymode == mode_T, "Load node", 0 );

      /*
       * jack's gen_add_firm_code:simpleSel seems to build Load (Load
       * (Proj (Proj))) sometimes ...

       * interprete.c:ai_eval seems to assume that this happens, too

       * obset.c:get_abstval_any can't deal with this if the load has
       * mode_T
       *
      {
        entity *ent = hunt_for_entity (get_Load_ptr (n), n);
        assert ((NULL != ent) || (mymode != mode_T));
      }
      */

      break;

    case iro_Store:
      op1mode = get_irn_mode(in[1]);
      op2mode = get_irn_mode(in[2]);
      op3mode = get_irn_mode(in[3]);
      ASSERT_AND_RET(
                     /* Load: BB x M x ref data --> M x X */
                     op1mode == mode_M && mode_is_reference(op2mode) && mode_is_data(op3mode),
                     "Store node", 0
                     );
      ASSERT_AND_RET(mymode == mode_T, "Store node", 0);

      entity *target = get_ptr_entity(in[2]);
      if (vrfy_entities && target && get_irg_phase_state(current_ir_graph) == phase_high) {
	/*
	 * If lowered code, any Sels that add 0 may be removed, causing
	 * an direct access to entities of array or compound type.
	 * Prevent this by checking the phase.
	 */
	ASSERT_AND_RET( op3mode == get_type_mode(get_entity_type(target)),
			"Store node", 0);
      }

      break;

    case iro_Alloc:
      op1mode = get_irn_mode(in[1]);
      op2mode = get_irn_mode(in[2]);
      ASSERT_AND_RET_DBG(
                         /* Alloc: BB x M x int_u --> M x X x ref */
                         op1mode == mode_M &&
                         mode_is_int(op2mode) &&
                         !mode_is_signed(op2mode) &&
                         mymode == mode_T,
                         "Alloc node", 0,
                         show_binop_failure(n, "/* Alloc: BB x M x int_u --> M x X x ref */");
                         );
      break;

    case iro_Free:
      op1mode = get_irn_mode(in[1]);
      op2mode = get_irn_mode(in[2]);
      ASSERT_AND_RET_DBG(
                         /* Free: BB x M x ref --> M */
                         op1mode == mode_M && mode_is_reference(op2mode) &&
                         mymode == mode_M,
                         "Free node", 0,
                         show_binop_failure(n, "/* Free: BB x M x ref --> M */");
                         );
      break;

    case iro_Sync:
      /* Sync: BB x M^n --> M */
      for (i=1; i < get_irn_arity(n); i++) {
        ASSERT_AND_RET( get_irn_mode(in[i]) == mode_M, "Sync node", 0 );
      };
      ASSERT_AND_RET( mymode == mode_M, "Sync node", 0 );
      break;

    case iro_Proj:
      return vrfy_Proj_proj(n, irg);
      break;

    case iro_Confirm:
      op1mode = get_irn_mode(in[1]);
      op2mode = get_irn_mode(in[2]);
      ASSERT_AND_RET_DBG(
                         /* Confirm: BB x T x T --> T */
                         op1mode == mymode &&
                         op2mode == mymode,
                         "Confirm node", 0,
                         show_binop_failure(n, "/* Confirm: BB x T x T --> T */");
                         );
      break;

    default:
      break;
    }

  /* All went ok */
  return 1;
}

int irn_vrfy(ir_node *n)
{
  int res = 1;
#ifdef DEBUG_libfirm
  res = irn_vrfy_irg(n, current_ir_graph);
#endif
  return res;
}

/*-----------------------------------------------------------------*/
/* Verify the whole graph.                                         */
/*-----------------------------------------------------------------*/

/* This *is* used, except gcc doesn't notice that */
static void vrfy_wrap(ir_node *node, void *env)
{
  int *res = env;

  *res = irn_vrfy(node);
}

int irg_vrfy(ir_graph *irg)
{
  int res = 1;
#ifdef DEBUG_libfirm
  ir_graph *rem;

  rem = current_ir_graph;
  current_ir_graph = irg;

  assert(get_irg_pinned(irg) == op_pin_state_pinned);

  irg_walk_graph(irg, vrfy_wrap, NULL, &res);

  current_ir_graph = rem;

  if (opt_do_node_verification == NODE_VERIFICATION_REPORT && ! res) {
    entity *ent = get_irg_entity(current_ir_graph);

    if (ent)
      fprintf(stderr, "irg_verify: Verifying graph %s failed\n", get_entity_name(ent));
    else
      fprintf(stderr, "irg_verify: Verifying graph %p failed\n", (void *)current_ir_graph);
  }

#endif
  return res;
}

int irn_vrfy_irg_dump(ir_node *n, ir_graph *irg, const char **bad_string)
{
  int res;
  node_verification_t old = opt_do_node_verification;

  bad_msg = NULL;
  opt_do_node_verification = NODE_VERIFICATION_ERROR_ONLY;
  res = irn_vrfy_irg(n, irg);
  opt_do_node_verification = old;
  *bad_string = bad_msg;

  return res;
}


typedef struct _vrfy_bad_env_t {
  int flags;
  int res;
} vrfy_bad_env_t;

static void check_bads(ir_node *node, void *env)
{
  vrfy_bad_env_t *venv = env;
  int i, arity = get_irn_arity(node);

  if (is_Block(node)) {
    if ((venv->flags & BAD_CF) == 0) {

      /* check for Bad Block predecessor */
      for (i = 0; i < arity; ++i) {
        ir_node *pred = get_irn_n(node, i);

	if (is_Bad(pred)) {
	  venv->res |= BAD_CF;

	  if (opt_do_node_verification == NODE_VERIFICATION_REPORT) {
	    fprintf(stderr, "irg_vrfy_bads: Block %ld has Bad predecessor\n", get_irn_node_nr(node));
	  }
	  if (opt_do_node_verification == NODE_VERIFICATION_ON) {
	    assert(0 && "Bad CF detected");
	  }
	}
      }
    }
  }
  else {
    if ((venv->flags & BAD_BLOCK) == 0) {

      /* check for Bad Block */
      if (is_Bad(get_nodes_block(node))) {
	venv->res |= BAD_BLOCK;

	if (opt_do_node_verification == NODE_VERIFICATION_REPORT) {
	  fprintf(stderr, "irg_vrfy_bads: node %ld has Bad Block\n", get_irn_node_nr(node));
	}
	if (opt_do_node_verification == NODE_VERIFICATION_ON) {
	  assert(0 && "Bad CF detected");
	}
      }
    }

    if ((venv->flags & TUPLE) == 0) {
      if (get_irn_op(node) == op_Tuple) {
	venv->res |= TUPLE;

	if (opt_do_node_verification == NODE_VERIFICATION_REPORT) {
	  fprintf(stderr, "irg_vrfy_bads: node %ld is a Tuple\n", get_irn_node_nr(node));
	}
	if (opt_do_node_verification == NODE_VERIFICATION_ON) {
	  assert(0 && "Tuple detected");
	}
      }
    }

    for (i = 0; i < arity; ++i) {
      ir_node *pred = get_irn_n(node, i);

      if (is_Bad(pred)) {
	/* check for Phi with Bad inputs */
	if (is_Phi(node) && !is_Bad(get_nodes_block(node)) && is_Bad(get_irn_n(get_nodes_block(node), i))) {
	  if (venv->flags & BAD_CF)
	    continue;
	  else {
	    venv->res |= BAD_CF;

	    if (opt_do_node_verification == NODE_VERIFICATION_REPORT) {
	      fprintf(stderr, "irg_vrfy_bads: Phi %ld has Bad Input\n", get_irn_node_nr(node));
	    }
	    if (opt_do_node_verification == NODE_VERIFICATION_ON) {
	      assert(0 && "Bad CF detected");
	    }
	  }
	}

	/* Bad node input */
	if ((venv->flags & BAD_DF) == 0) {
	  venv->res |= BAD_DF;

	  if (opt_do_node_verification == NODE_VERIFICATION_REPORT) {
	    fprintf(stderr, "irg_vrfy_bads: node %ld has Bad Input\n", get_irn_node_nr(node));
	  }
	  if (opt_do_node_verification == NODE_VERIFICATION_ON) {
	    assert(0 && "Bad NON-CF detected");
	  }
	}
      }
    }
  }
}

/*
 * verify occurance of bad nodes
 */
int irg_vrfy_bads(ir_graph *irg, int flags)
{
  vrfy_bad_env_t env;

  env.flags = flags;
  env.res   = 0;

  irg_walk_graph(irg, check_bads, NULL, &env);

  return env.res;
}
