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
 * @brief       Register param constraints and some other register handling tools.
 * @author      Christian Wuerdig
 * @version     $Id$
 */
#include "config.h"

#include <stdlib.h>

#include "pmap.h"
#include "error.h"

#include "ia32_map_regs.h"
#include "ia32_new_nodes.h"
#include "ia32_architecture.h"
#include "gen_ia32_regalloc_if.h"
#include "bearch_ia32_t.h"

#define MAXNUM_GPREG_ARGS     3
#define MAXNUM_SSE_ARGS       8

/* this is the order of the assigned registers used for parameter passing */

static const arch_register_t *gpreg_param_reg_fastcall[] = {
	&ia32_gp_regs[REG_ECX],
	&ia32_gp_regs[REG_EDX],
	NULL
};

static const arch_register_t *gpreg_param_reg_regparam[] = {
	&ia32_gp_regs[REG_EAX],
	&ia32_gp_regs[REG_EDX],
	&ia32_gp_regs[REG_ECX]
};

static const arch_register_t *gpreg_param_reg_this[] = {
	&ia32_gp_regs[REG_ECX],
	NULL,
	NULL
};

static const arch_register_t *fpreg_sse_param_reg_std[] = {
	&ia32_xmm_regs[REG_XMM0],
	&ia32_xmm_regs[REG_XMM1],
	&ia32_xmm_regs[REG_XMM2],
	&ia32_xmm_regs[REG_XMM3],
	&ia32_xmm_regs[REG_XMM4],
	&ia32_xmm_regs[REG_XMM5],
	&ia32_xmm_regs[REG_XMM6],
	&ia32_xmm_regs[REG_XMM7]
};

static const arch_register_t *fpreg_sse_param_reg_this[] = {
	NULL,  /* in case of a "this" pointer, the first parameter must not be a float */
};


/* Mapping to store registers in firm nodes */

struct ia32_irn_reg_assoc {
	const ir_node *irn;
	const arch_register_t *reg;
};

int ia32_cmp_irn_reg_assoc(const void *a, const void *b, size_t len) {
	const struct ia32_irn_reg_assoc *x = a;
	const struct ia32_irn_reg_assoc *y = b;
	(void) len;

	return x->irn != y->irn;
}

static struct ia32_irn_reg_assoc *get_irn_reg_assoc(const ir_node *irn, set *reg_set) {
	struct ia32_irn_reg_assoc templ;
	unsigned int hash;

	templ.irn = irn;
	templ.reg = NULL;
	hash      = hash_irn(irn);

	return set_insert(reg_set, &templ, sizeof(templ), hash);
}

void ia32_set_firm_reg(ir_node *irn, const arch_register_t *reg, set *reg_set) {
	struct ia32_irn_reg_assoc *assoc = get_irn_reg_assoc(irn, reg_set);
	assoc->reg = reg;
}

const arch_register_t *ia32_get_firm_reg(const ir_node *irn, set *reg_set) {
	struct ia32_irn_reg_assoc *assoc = get_irn_reg_assoc(irn, reg_set);
	return assoc->reg;
}

void ia32_build_16bit_reg_map(pmap *reg_map) {
	pmap_insert(reg_map, &ia32_gp_regs[REG_EAX], "ax");
	pmap_insert(reg_map, &ia32_gp_regs[REG_EBX], "bx");
	pmap_insert(reg_map, &ia32_gp_regs[REG_ECX], "cx");
	pmap_insert(reg_map, &ia32_gp_regs[REG_EDX], "dx");
	pmap_insert(reg_map, &ia32_gp_regs[REG_ESI], "si");
	pmap_insert(reg_map, &ia32_gp_regs[REG_EDI], "di");
	pmap_insert(reg_map, &ia32_gp_regs[REG_EBP], "bp");
	pmap_insert(reg_map, &ia32_gp_regs[REG_ESP], "sp");
}

void ia32_build_8bit_reg_map(pmap *reg_map) {
	pmap_insert(reg_map, &ia32_gp_regs[REG_EAX], "al");
	pmap_insert(reg_map, &ia32_gp_regs[REG_EBX], "bl");
	pmap_insert(reg_map, &ia32_gp_regs[REG_ECX], "cl");
	pmap_insert(reg_map, &ia32_gp_regs[REG_EDX], "dl");
}

void ia32_build_8bit_reg_map_high(pmap *reg_map) {
	pmap_insert(reg_map, &ia32_gp_regs[REG_EAX], "ah");
	pmap_insert(reg_map, &ia32_gp_regs[REG_EBX], "bh");
	pmap_insert(reg_map, &ia32_gp_regs[REG_ECX], "ch");
	pmap_insert(reg_map, &ia32_gp_regs[REG_EDX], "dh");
}

const char *ia32_get_mapped_reg_name(pmap *reg_map, const arch_register_t *reg) {
	pmap_entry *e = pmap_find(reg_map, (void *)reg);

	//assert(e && "missing map init?");
	if (! e) {
		printf("FIXME: ia32map_regs.c:122: returning fake register name for ia32 with 32 register\n");
		return reg->name;
	}

	return e->value;
}

/**
 * Returns the register for parameter nr.
 */
const arch_register_t *ia32_get_RegParam_reg(unsigned cc, size_t nr,
                                             const ir_mode *mode)
{
	if ((cc & cc_this_call) && nr == 0)
		return gpreg_param_reg_this[0];

	if (! (cc & cc_reg_param))
		return NULL;

	if (mode_is_float(mode)) {
		if (!ia32_cg_config.use_sse2 || (cc & cc_fpreg_param) == 0)
			return NULL;
		if (nr >= MAXNUM_SSE_ARGS)
			return NULL;

		if (cc & cc_this_call) {
			return fpreg_sse_param_reg_this[nr];
		}
		return fpreg_sse_param_reg_std[nr];
	} else if (mode_is_int(mode) || mode_is_reference(mode)) {
		unsigned num_regparam;

		if (get_mode_size_bits(mode) > 32)
			return NULL;

		if (nr >= MAXNUM_GPREG_ARGS)
			return NULL;

		if (cc & cc_this_call) {
			return gpreg_param_reg_this[nr];
		}
		num_regparam = cc & ~cc_bits;
		if (num_regparam == 0) {
			/* default fastcall */
			return gpreg_param_reg_fastcall[nr];
		}
		if (nr < num_regparam)
			return gpreg_param_reg_regparam[nr];
		return NULL;
	}

	panic("unknown argument mode");
}
