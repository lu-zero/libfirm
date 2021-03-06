/*
 * This file is part of libFirm.
 * Copyright (C) 2012 University of Karlsruhe.
 */

/**
 * @file
 * @brief   Function prototypes for the assembler ir node constructors.
 * @author  Hannes Rapp, Matthias Braun
 */
#ifndef FIRM_BE_SPARC_SPARC_NEW_NODES_H
#define FIRM_BE_SPARC_SPARC_NEW_NODES_H

#include <stdbool.h>
#include "sparc_nodes_attr.h"

/**
 * Returns the attributes of an sparc node.
 */
sparc_attr_t *get_sparc_attr(ir_node *node);
const sparc_attr_t *get_sparc_attr_const(const ir_node *node);

bool sparc_has_load_store_attr(const ir_node *node);
sparc_load_store_attr_t *get_sparc_load_store_attr(ir_node *node);
const sparc_load_store_attr_t *get_sparc_load_store_attr_const(const ir_node *node);

sparc_jmp_cond_attr_t *get_sparc_jmp_cond_attr(ir_node *node);
const sparc_jmp_cond_attr_t *get_sparc_jmp_cond_attr_const(const ir_node *node);

sparc_switch_jmp_attr_t *get_sparc_switch_jmp_attr(ir_node *node);
const sparc_switch_jmp_attr_t *get_sparc_switch_jmp_attr_const(const ir_node *node);

sparc_fp_attr_t *get_sparc_fp_attr(ir_node *node);
const sparc_fp_attr_t *get_sparc_fp_attr_const(const ir_node *node);

sparc_fp_conv_attr_t *get_sparc_fp_conv_attr(ir_node *node);
const sparc_fp_conv_attr_t *get_sparc_fp_conv_attr_const(const ir_node *node);

sparc_asm_attr_t *get_sparc_asm_attr(ir_node *node);
const sparc_asm_attr_t *get_sparc_asm_attr_const(const ir_node *node);

sparc_call_attr_t *get_sparc_call_attr(ir_node *node);
const sparc_call_attr_t *get_sparc_call_attr_const(const ir_node *node);

/* Include the generated headers */
#include "gen_sparc_new_nodes.h"

#endif
