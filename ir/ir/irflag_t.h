/*
 * Project:     libFIRM
 * File name:   ir/ir/irgraph.c
 * Purpose:     Flags to control optimizations, inline implementation.
 * Author:      Michael Beck
 * Created:
 * CVS-ID:      $Id$
 * Copyright:   (c) 1998-2004 Universitšt Karlsruhe
 * Licence:     This file protected by GPL -  GNU GENERAL PUBLIC LICENSE.
 */

/**
 * @file irflag_t.h
 *
 * Inline implementation of Optimization flags.
 *
 * @author Michael Beck
 */
#ifndef _IRFLAG_T_H_
#define _IRFLAG_T_H_

#include "irflag.h"

/**
 * current libFIRM optimizations
 */
typedef enum {
#define E_FLAG(name, value, def)	irf_##name = (1 << value),
#define I_FLAG(name, value, def)	irf_##name = (1 << value),

#include "irflag_t.def"
	irf_last
#undef I_FLAG
#undef E_FLAG
} libfirm_opts_t;

extern optimization_state_t libFIRM_opt;
extern optimization_state_t libFIRM_verb;
extern firm_verification_t opt_do_node_verification;

extern int firm_verbosity_level;

/** initialises the flags */
void firm_init_flags(void);

/* generate the getter functions for external access */
#define E_FLAG(name, value, def)                    \
static INLINE int _get_opt_##name(void) {           \
  return libFIRM_opt & irf_##name;                  \
}                                                   \
static INLINE int get_opt_##name##_verbose(void) {  \
  return libFIRM_verb & irf_##name;                 \
}

/* generate the getter functions for internal access */
#define I_FLAG(name, value, def)                   \
static INLINE int get_opt_##name(void) {           \
  return libFIRM_opt & irf_##name;                 \
}                                                  \
static INLINE int get_opt_##name##_verbose(void) { \
  return libFIRM_verb & irf_##name;                \
}

#include "irflag_t.def"

#undef I_FLAG
#undef E_FLAG

static INLINE int _get_firm_verbosity (void) {
	return firm_verbosity_level;
}

static INLINE int _get_optimize (void) {
  return get_opt_optimize();
}

static INLINE firm_verification_t
get_node_verification_mode(void) {
  return opt_do_node_verification;
}

#define get_optimize()                           _get_optimize()
#define get_opt_cse()                            _get_opt_cse()
#define get_firm_verbosity()                     _get_firm_verbosity()
#define get_opt_dyn_meth_dispatch()              _get_opt_dyn_meth_dispatch()
#define get_opt_optimize_class_casts()           _get_opt_optimize_class_casts()
#define get_opt_suppress_downcast_optimization() _get_opt_suppress_downcast_optimization()

#endif /* _IRFLAG_T_H_ */
