/**
 * @file irreflect.c
 * @date 9.9.2004
 * @author Sebastian Hack
 * @brief Reflection for Firm operands.
 *
 * $Id$
 */

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define obstack_chunk_alloc malloc
#define obstack_chunk_free free
#include <obstack.h>

#include "irmode.h"
#include "irreflect.h"

#define obstack_grow_str(obst,s) obstack_grow((obst), (s), strlen((s)))
#define obstack_grow_str_const(obst,s) obstack_grow((obst), (s), sizeof((s)))

extern int obstack_printf(struct obstack *obst, const char *fmt, ...);

/**
 * Get the number of bits set in a word.
 */
static inline int pop(int i) {
  unsigned x = (unsigned) i;
  x = ((x >> 1) & 0x55555555);
  x = (x & 0x33333333) + ((x >> 2) & 0x33333333);
  x = (x + (x >> 4)) & 0x0f0f0f0f;
  x = x + (x >> 8);
  x = x + (x >> 16);
  return (int) (x & 0x3f);
}

/**
 * Get the number of bits differing in two variables.
 */
static inline int dist(int x, int y) {
  return pop(x ^ y);
}


#define MAX_SIG_COUNT 8
#define MAX_ARG_COUNT 10

typedef struct {
  int num;    /**< A sequential number (one opcode can have multiple signatures. */
  rflct_arg_t args[]; /**< The signature. */
} rflct_args_t;

typedef struct {
  opcode opc;
  const char *name;
  bool commutative;
  int sig_count;
  const rflct_arg_t *sigs[MAX_SIG_COUNT];
} rflct_opcode_t;

static struct obstack obst;

static const rflct_opcode_t *opcodes[iro_MaxOpcode];

#define OPCODES_COUNT (sizeof(opcodes) / sizeof(opcodes[0]))

// #define MODE_IS(mode,mask) (((mode) & (mask)) != 0)

rflct_mode_class_t rflct_get_mode_class(const ir_mode *mode) {
  mode_sort sort = get_mode_sort(mode);

  switch(sort) {
  case irms_auxiliary:
  case irms_control_flow:
    if(mode == mode_BB)
      return RFLCT_MC(BB);
    else if(mode == mode_X)
      return RFLCT_MC(X);
  case irms_memory:
    return RFLCT_MC(Mem);
  case irms_internal_boolean:
    return RFLCT_MC(Bool);
  case irms_int_number:
    return mode_is_signed(mode) ? RFLCT_MC(IntS) : RFLCT_MC(IntU);
  case irms_float_number:
    return RFLCT_MC(Float);
  case irms_reference:
    return RFLCT_MC(Ref);
  case irms_character:
    return RFLCT_MC(Char);
  }

  return RFLCT_MC(None);
}

static inline const rflct_opcode_t *get_opcode(opcode opc) {
  assert(opc >= 0 && opc < OPCODES_COUNT && "Invalid opcode");
  return opcodes[opc];
}

static inline const rflct_arg_t *get_args(opcode opc, int sig) {
  const rflct_opcode_t *opcode = get_opcode(opc);
  assert(sig >= 0 && sig < opcode->sig_count
	 && "Invalid signature");
  return opcode->sigs[sig];
}

#define GET_OPCODE(opc) get_opcode(opc)
#define GET_ARGS(opc,args) get_args(opc, args)

int rflct_get_signature_count(opcode opc) {
  const rflct_opcode_t *opcode = GET_OPCODE(opc);
  return opcode->sig_count;
}

int rflct_get_in_args_count(opcode opc, int sig) {
  const rflct_arg_t *args = GET_ARGS(opc, sig);
  int res = 0, i = 0;

  for(i = 0; args[i].name != NULL; i++);
  for(res = 0, i += 1; args[i].name != NULL; res++, i++);
  return res;
}

int rflct_get_out_args_count(opcode opc, int sig) {
  const rflct_arg_t *args = GET_ARGS(opc, sig);
  int i = 0;
  for(i = 0; args[i].name != NULL; i++);
  return i;
}


const rflct_arg_t *rflct_get_in_args(opcode opc, int sig) {
  const rflct_arg_t *args = GET_ARGS(opc, sig);
  int i;

  for(i = 0; args[i].name != NULL; i++);
  return &args[i + 1];
}

const rflct_arg_t *rflct_get_out_args(opcode opc, int sig) {
  return GET_ARGS(opc, sig);
}

int rflct_signature_match(ir_node *irn, int sig) {
  opcode op = get_irn_opcode(irn);
  const rflct_arg_t *args = rflct_get_in_args(op, sig);
  int dst = 0;
  int i, j;

  for(i = 0, j = -1; RFLCT_ARG_VALID(&args[i])
	&& j < get_irn_arity(irn); j++) {

    ir_node *child = get_irn_n(irn, j);
    const rflct_arg_t *arg = &args[i];
    rflct_mode_class_t mc = rflct_get_mode_class(get_irn_mode(child));

    if(arg->accepted_modes & mc)
      dst += dist(arg->accepted_modes, mc);
    else
      return INT_MAX;

    if(!arg->is_variadic)
      i++;
  }

  return dst;
}

int rflct_get_signature(ir_node *irn) {
  const rflct_opcode_t *opc = GET_OPCODE(get_irn_opcode(irn));
  int min_dist = INT_MAX;
  int min_sig = INT_MAX;
  int i;

  for(i = 0; i < opc->sig_count; i++) {
    int dist = rflct_signature_match(irn, i);
    if(dist < min_dist) {
      min_dist = dist;
      min_sig = i;
    }
  }

  return min_sig;
}

static const char *rflct_mode_class_atomic_name(rflct_mode_class_t mc) {
#define XXX(_mc) case RFLCT_MC(_mc): return #_mc
  switch(mc) {
    XXX(None);
    XXX(Mem);
    XXX(Bool);
    XXX(IntS);
    XXX(IntU);
    XXX(Float);
    XXX(Ref);
    XXX(Char);
    XXX(X);
    XXX(BB);
    XXX(Int);
    XXX(Intb);
    XXX(Num);
    XXX(NumP);
    XXX(Data);
    XXX(Datab);
    XXX(DataM);
    XXX(DataMX);
    XXX(Lh);
  default:
    return "";
  }
#undef XXX
}

static void rflct_mode_class_comb_name_obst(struct obstack *obst,
					    rflct_mode_class_t mc) {
  const char *res = rflct_mode_class_atomic_name(mc);

  if(strlen(res) == 0) {
    const char *prefix = "";
    int mask;

    obstack_1grow(obst, '{');
    for(mask = 1; mask != 0; mask <<= 1) {
      if(mask & mc) {
	const char *s = rflct_mode_class_atomic_name(mask);
	obstack_grow_str(obst, s);
	obstack_grow_str(obst, prefix);
	prefix = "|";
      }
    }
    obstack_1grow(obst, '}');

  } else
    obstack_grow(obst, res, strlen(res));
}

char *rflct_mode_class_name(char *str, int n, rflct_mode_class_t mc) {
  struct obstack obst;
  const char *res;

  obstack_init(&obst);

  rflct_mode_class_comb_name_obst(&obst, mc);
  obstack_1grow(&obst, 0);
  res = obstack_finish(&obst);

  strncpy(str, res, n);

  obstack_free(&obst, NULL);

  return str;
}

static void rflct_obstack_grow_args(struct obstack *obst,
				    const rflct_arg_t *args) {
  const rflct_arg_t *arg;
  const char *prefix = "";

  for(arg = args; RFLCT_ARG_VALID(arg); arg++) {
    obstack_grow_str(obst, prefix);
    obstack_grow_str(obst, arg->name);
    if(arg->is_variadic)
      obstack_1grow(obst, '*');
    obstack_1grow(obst, ':');
    rflct_mode_class_comb_name_obst(obst, arg->accepted_modes);
    prefix = ", ";
  }

}

char *rflct_to_string(char *buf, int n, opcode opc, int sig) {
  struct obstack obst;
  char *s;
  const rflct_opcode_t *opcode = GET_OPCODE(opc);

  obstack_init(&obst);

  obstack_1grow(&obst, '(');
  rflct_obstack_grow_args(&obst, rflct_get_out_args(opc, sig));

  obstack_grow_str(&obst, ") = ");
  obstack_grow_str(&obst, opcode->name);
  obstack_1grow(&obst, '(');

  rflct_obstack_grow_args(&obst, rflct_get_in_args(opc, sig));

  obstack_1grow(&obst, ')');
  obstack_1grow(&obst, 0);
  s = obstack_finish(&obst);
  strncpy(buf, s, n);
  obstack_free(&obst, NULL);

  return buf;
}

#define ARG(name,modes) \
  _ARG(name, modes, false, -1)

#define ARG_SAME(name,modes,mode_same) \
  _ARG(name, modes, false, mode_same)

#define VARG(name,modes) \
  _ARG(name, modes, true, 0)

#define VARG_SAME(name,modes) \
  _ARG(name, modes, true, 1)

#define MARK \
  _ARG(NULL, None, false, -1)

#define BLOCK ARG("Block", BB)

static void init_ops(void) {

  int curr_sig;
  rflct_opcode_t *opcode;

  obstack_init(&obst);


#define BEGIN_OP(op)  \
    curr_sig = 0; \
    opcode = obstack_alloc(&obst, sizeof(*opcode)); \
    opcode->opc = iro_ ## op; \
    opcode->name = #op; \
    opcodes[opcode->opc] = opcode;


#define BEGIN_ARGS

#define _ARG(_name,_modes,_variadic,_mode_equals) \
  { \
    rflct_arg_t args; \
    args.name = _name; \
    args.accepted_modes = RFLCT_MC(_modes); \
    args.is_variadic = _variadic; \
    args.mode_equals = _mode_equals; \
    obstack_grow(&obst, &args, sizeof(args)); \
  }

#define END_ARGS \
  { \
    _ARG(NULL, None, false, 0) \
    assert(curr_sig < MAX_SIG_COUNT && "Mind the maximum number of signatures"); \
    opcode->sigs[curr_sig++] = obstack_finish(&obst); \
    opcode->sig_count = curr_sig; \
  }

#define END_OP

#include "irreflect.def"

#undef BEGIN_ARGS
#undef END_ARGS
#undef BEGIN_OP
#undef END_OP
}




void init_rflct(void) {
  init_ops();
}
