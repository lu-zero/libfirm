/*
 * This file is part of libFirm.
 * Copyright (C) 2012 University of Karlsruhe.
 */

/**
 * @file
 * @brief       This file contains functions for matching firm graphs for
 *              nodes that can be used as address mode for x86 instructions
 * @author      Matthias Braun
 */
#ifndef IA32_ADDRESS_MODE_H
#define IA32_ADDRESS_MODE_H

#include <stdbool.h>
#include "irtypes.h"

/**
 * The address mode data: Used to construct (memory) address modes.
 */
typedef struct ia32_address_t ia32_address_t;
struct ia32_address_t {
	ir_node   *base;          /**< The base register (if any) */
	ir_node   *index;         /**< The index register (if any). */
	ir_node   *mem;           /**< The memory value (if any). */
	int        offset;        /**< An integer offset. */
	int        scale;         /**< An integer scale. {0,1,2,3} */
	ir_entity *symconst_ent;  /**< A SynConst entity if any. */
	bool       use_frame;     /**< Set, if the frame is accessed */
	bool       tls_segment;   /**< Set if AM is relative to TLS */
	ir_entity *frame_entity;  /**< The accessed frame entity if any. */
	bool       symconst_sign; /**< The "sign" of the symconst. */
};

/**
 * Additional flags for the address mode creation.
 */
typedef enum ia32_create_am_flags_t {
	ia32_create_am_normal     = 0,       /**< Normal operation. */
	ia32_create_am_force      = 1U << 0, /**< Ignore the marking of node as a
	                                          non-address-mode node. */
	ia32_create_am_double_use = 1U << 1  /**< Fold AM, even if the root of
	                                          address calculation has two users.
	                                          This is useful for dest AM. */
} ia32_create_am_flags_t;

/**
 * Create an address mode for a given node.
 */
void ia32_create_address_mode(ia32_address_t *addr, ir_node *node, ia32_create_am_flags_t);

/**
 * Mark those nodes of the given graph that cannot be used inside an
 * address mode because there values must be materialized in registers.
 */
void ia32_calculate_non_address_mode_nodes(ir_graph *irg);

/**
 * Free the non_address_mode information.
 */
void ia32_free_non_address_mode_nodes(void);

/**
 * Tells whether the given node is a non address mode node.
 */
int ia32_is_non_address_mode_node(ir_node const *node);

/**
 * mark a node so it will not be used as part of address modes
 */
void ia32_mark_non_am(ir_node *node);

#endif
