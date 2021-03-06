/*
 * This file is part of libFirm.
 * Copyright (C) 2012 University of Karlsruhe.
 */

/**
 * @file
 * @brief    Data structure to hold type information for nodes.
 * @author   Goetz Lindenmaier
 * @date     28.8.2003
 * @brief
 *   Data structure to hold type information for nodes.
 *
 *   This module defines a field "type" of type "type *" for each ir node.
 *   It defines a flag for irgraphs to mark whether the type info of the
 *   graph is valid.  Further it defines an auxiliary type "init_type".
 */
#ifndef FIRM_ANA_IRTYPEINFO_H
#define FIRM_ANA_IRTYPEINFO_H

#include "firm_types.h"
#include "begin.h"

/* ------------ Auxiliary type. --------------------------------------- */

/** An auxiliary type used to express that a field is uninitialized.
 *
 *  This auxiliary type expresses that a field is uninitialized.  The
 *  variable is initialized by init_irtypeinfo().  The type is freed by
 *  free_irtypeinfo().
 */
FIRM_API ir_type *initial_type;



/* ------------ Initializing this module. ----------------------------- */

/** Initializes the type information module.
 *
 *  Initializes the type information module.
 *  Generates a type inititial_type and sets the type of all nodes to this type.
 *  Calling set/get_irn_typeinfo_type() is invalid before calling init. Requires memory
 *  in the order of MIN(\<calls to set_irn_typeinfo_type\>, \#irnodes).
 */
FIRM_API void init_irtypeinfo(void);
/** Frees memory used by the type information module */
FIRM_API void free_irtypeinfo(void);

/* ------------ Irgraph state handling. ------------------------------- */

/** typeinfo information state */
typedef enum {
	ir_typeinfo_none,        /**< No typeinfo computed, calls to set/get_irn_typeinfo_type()
	                              are invalid. */
	ir_typeinfo_consistent,  /**< Type info valid, calls to set/get_irn_typeinfo_type() return
	                              the proper type. */
	ir_typeinfo_inconsistent /**< Type info can be accessed, but it can be invalid
	                              because of other transformations. */
} ir_typeinfo_state;

/** Sets state of typeinfo information in graph @p irg to @p state. */
FIRM_API void set_irg_typeinfo_state(ir_graph *irg, ir_typeinfo_state state);
/** Returns state of typeinfo information in graph @p irg. */
FIRM_API ir_typeinfo_state get_irg_typeinfo_state(const ir_graph *irg);

/** Returns accumulated type information state information.
 *
 * Returns ir_typeinfo_consistent if the type information of all irgs is
 * consistent.  Returns ir_typeinfo_inconsistent if at least one irg has inconsistent
 * or no type information.  Returns ir_typeinfo_none if no irg contains type information.
 */
FIRM_API ir_typeinfo_state get_irp_typeinfo_state(void);
/** Sets state of typeinfo information for the current program to @p state */
FIRM_API void set_irp_typeinfo_state(ir_typeinfo_state state);
/** Sets state of typeinfo information for the current program to #ir_typeinfo_inconsistent */
FIRM_API void set_irp_typeinfo_inconsistent(void);

/* ------------ Irnode type information. ------------------------------ */

/** Accessing the type information.
 *
 * These routines only work properly if the ir_graph is in state
 * ir_typeinfo_consistent or ir_typeinfo_inconsistent.
 */
FIRM_API ir_type *get_irn_typeinfo_type(const ir_node *n);
/** Sets type information of procedure graph node @p node to type @p type. */
FIRM_API void set_irn_typeinfo_type(ir_node *node, ir_type *type);

#include "end.h"

#endif
