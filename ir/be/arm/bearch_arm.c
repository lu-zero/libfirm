/*
 * This file is part of libFirm.
 * Copyright (C) 2012 University of Karlsruhe.
 */

/**
 * @file
 * @brief   The main arm backend driver file.
 * @author  Matthias Braun, Oliver Richter, Tobias Gneist
 */
#include "lc_opts.h"
#include "lc_opts_enum.h"

#include "irgwalk.h"
#include "irprog.h"
#include "ircons.h"
#include "irgmod.h"
#include "irgopt.h"
#include "iroptimize.h"
#include "irdump.h"
#include "lower_calls.h"
#include "error.h"
#include "debug.h"
#include "array_t.h"
#include "irtools.h"

#include "bearch.h"
#include "benode.h"
#include "belower.h"
#include "besched.h"
#include "be.h"
#include "bemodule.h"
#include "beirg.h"
#include "bespillslots.h"
#include "bespillutil.h"
#include "begnuas.h"
#include "belistsched.h"
#include "beflags.h"
#include "bestack.h"
#include "betranshlp.h"

#include "bearch_arm_t.h"

#include "arm_new_nodes.h"
#include "gen_arm_regalloc_if.h"
#include "arm_transform.h"
#include "arm_optimize.h"
#include "arm_emitter.h"
#include "arm_map_regs.h"

static ir_entity *arm_get_frame_entity(const ir_node *irn)
{
	const arm_attr_t *attr = get_arm_attr_const(irn);

	if (is_arm_FrameAddr(irn)) {
		const arm_SymConst_attr_t *frame_attr = get_arm_SymConst_attr_const(irn);
		return frame_attr->entity;
	}
	if (attr->is_load_store) {
		const arm_load_store_attr_t *load_store_attr
			= get_arm_load_store_attr_const(irn);
		if (load_store_attr->is_frame_entity) {
			return load_store_attr->entity;
		}
	}
	return NULL;
}

/**
 * This function is called by the generic backend to correct offsets for
 * nodes accessing the stack.
 */
static void arm_set_stack_bias(ir_node *irn, int bias)
{
	if (is_arm_FrameAddr(irn)) {
		arm_SymConst_attr_t *attr = get_arm_SymConst_attr(irn);
		attr->fp_offset += bias;
	} else {
		arm_load_store_attr_t *attr = get_arm_load_store_attr(irn);
		assert(attr->base.is_load_store);
		attr->offset += bias;
	}
}

static int arm_get_sp_bias(const ir_node *irn)
{
	/* We don't have any nodes changing the stack pointer.
	   We probably want to support post-/pre increment/decrement later */
	(void) irn;
	return 0;
}

/* fill register allocator interface */

static const arch_irn_ops_t arm_irn_ops = {
	arm_get_frame_entity,
	arm_set_stack_bias,
	arm_get_sp_bias,
	NULL,    /* get_op_estimated_cost   */
	NULL,    /* possible_memory_operand */
	NULL,    /* perform_memory_operand  */
};

/**
 * Transforms the standard Firm graph into an ARM firm graph.
 */
static void arm_prepare_graph(ir_graph *irg)
{
	/* transform nodes into assembler instructions */
	be_timer_push(T_CODEGEN);
	arm_transform_graph(irg);
	be_timer_pop(T_CODEGEN);
	be_dump(DUMP_BE, irg, "code-selection");

	/* do local optimizations (mainly CSE) */
	local_optimize_graph(irg);

	/* do code placement, to optimize the position of constants */
	place_code(irg);
}

static void arm_collect_frame_entity_nodes(ir_node *node, void *data)
{
	be_fec_env_t  *env = (be_fec_env_t*)data;
	const ir_mode *mode;
	int            align;
	ir_entity     *entity;
	const arm_load_store_attr_t *attr;

	if (be_is_Reload(node) && be_get_frame_entity(node) == NULL) {
		mode  = get_irn_mode(node);
		align = get_mode_size_bytes(mode);
		be_node_needs_frame_entity(env, node, mode, align);
		return;
	}

	if (!is_arm_Ldf(node) && !is_arm_Ldr(node))
		return;

	attr   = get_arm_load_store_attr_const(node);
	entity = attr->entity;
	mode   = attr->load_store_mode;
	align  = get_mode_size_bytes(mode);
	if (entity != NULL)
		return;
	if (!attr->is_frame_entity)
		return;
	be_node_needs_frame_entity(env, node, mode, align);
}

static void arm_set_frame_entity(ir_node *node, ir_entity *entity)
{
	if (is_be_node(node)) {
		be_node_set_frame_entity(node, entity);
	} else {
		arm_load_store_attr_t *attr = get_arm_load_store_attr(node);
		attr->entity = entity;
	}
}

static void transform_Reload(ir_node *node)
{
	ir_node   *block  = get_nodes_block(node);
	dbg_info  *dbgi   = get_irn_dbg_info(node);
	ir_node   *ptr    = get_irn_n(node, n_be_Reload_frame);
	ir_node   *mem    = get_irn_n(node, n_be_Reload_mem);
	ir_mode   *mode   = get_irn_mode(node);
	ir_entity *entity = be_get_frame_entity(node);
	const arch_register_t *reg;
	ir_node   *proj;
	ir_node   *load;

	load = new_bd_arm_Ldr(dbgi, block, ptr, mem, mode, entity, false, 0, true);
	sched_replace(node, load);

	proj = new_rd_Proj(dbgi, load, mode, pn_arm_Ldr_res);

	reg = arch_get_irn_register(node);
	arch_set_irn_register(proj, reg);

	exchange(node, proj);
}

static void transform_Spill(ir_node *node)
{
	ir_node   *block  = get_nodes_block(node);
	dbg_info  *dbgi   = get_irn_dbg_info(node);
	ir_node   *ptr    = get_irn_n(node, n_be_Spill_frame);
	ir_graph  *irg    = get_irn_irg(node);
	ir_node   *mem    = get_irg_no_mem(irg);
	ir_node   *val    = get_irn_n(node, n_be_Spill_val);
	ir_mode   *mode   = get_irn_mode(val);
	ir_entity *entity = be_get_frame_entity(node);
	ir_node   *store;

	store = new_bd_arm_Str(dbgi, block, ptr, val, mem, mode, entity, false, 0,
	                       true);
	sched_replace(node, store);

	exchange(node, store);
}

static void arm_after_ra_walker(ir_node *block, void *data)
{
	(void) data;

	sched_foreach_reverse_safe(block, node) {
		if (be_is_Reload(node)) {
			transform_Reload(node);
		} else if (be_is_Spill(node)) {
			transform_Spill(node);
		}
	}
}

static void arm_emit(ir_graph *irg)
{
	be_stack_layout_t *stack_layout = be_get_irg_stack_layout(irg);
	bool               at_begin     = stack_layout->sp_relative ? true : false;
	be_fec_env_t      *fec_env      = be_new_frame_entity_coalescer(irg);

	irg_walk_graph(irg, NULL, arm_collect_frame_entity_nodes, fec_env);
	be_assign_entities(fec_env, arm_set_frame_entity, at_begin);
	be_free_frame_entity_coalescer(fec_env);

	irg_block_walk_graph(irg, NULL, arm_after_ra_walker, NULL);

	/* fix stack entity offsets */
	be_abi_fix_stack_nodes(irg);
	be_abi_fix_stack_bias(irg);

	/* do peephole optimizations and fix stack offsets */
	arm_peephole_optimization(irg);

	/* emit code */
	arm_emit_function(irg);
}

static void arm_before_ra(ir_graph *irg)
{
	be_sched_fix_flags(irg, &arm_reg_classes[CLASS_arm_flags], NULL, NULL);
}

static ir_entity *divsi3;
static ir_entity *udivsi3;
static ir_entity *modsi3;
static ir_entity *umodsi3;

static void handle_intrinsic(ir_node *node, void *data)
{
	(void)data;
	if (is_Div(node)) {
		ir_mode *mode = get_Div_resmode(node);
		if (get_mode_arithmetic(mode) == irma_twos_complement) {
			ir_entity *entity = mode_is_signed(mode) ? divsi3 : udivsi3;
			be_map_exc_node_to_runtime_call(node, mode, entity, pn_Div_M,
			                                pn_Div_X_regular, pn_Div_X_except,
			                                pn_Div_res);
		}
	}
	if (is_Mod(node)) {
		ir_mode *mode = get_Mod_resmode(node);
		assert(get_mode_arithmetic(mode) == irma_twos_complement);
		ir_entity *entity = mode_is_signed(mode) ? modsi3 : umodsi3;
		be_map_exc_node_to_runtime_call(node, mode, entity, pn_Mod_M,
		                                pn_Mod_X_regular, pn_Mod_X_except,
		                                pn_Mod_res);
	}
}

static void arm_create_runtime_entities(void)
{
	if (divsi3 != NULL)
		return;

	ir_type *int_tp  = get_type_for_mode(mode_Is);
	ir_type *uint_tp = get_type_for_mode(mode_Iu);

	ir_type *tp_divsi3 = new_type_method(2, 1);
	set_method_param_type(tp_divsi3, 0, int_tp);
	set_method_param_type(tp_divsi3, 1, int_tp);
	set_method_res_type(tp_divsi3, 0, int_tp);
	divsi3 = create_compilerlib_entity(new_id_from_str("__divsi3"), tp_divsi3);

	ir_type *tp_udivsi3 = new_type_method(2, 1);
	set_method_param_type(tp_udivsi3, 0, uint_tp);
	set_method_param_type(tp_udivsi3, 1, uint_tp);
	set_method_res_type(tp_udivsi3, 0, uint_tp);
	udivsi3 = create_compilerlib_entity(new_id_from_str("__udivsi3"), tp_udivsi3);

	ir_type *tp_modsi3 = new_type_method(2, 1);
	set_method_param_type(tp_modsi3, 0, int_tp);
	set_method_param_type(tp_modsi3, 1, int_tp);
	set_method_res_type(tp_modsi3, 0, int_tp);
	modsi3 = create_compilerlib_entity(new_id_from_str("__modsi3"), tp_modsi3);

	ir_type *tp_umodsi3 = new_type_method(2, 1);
	set_method_param_type(tp_umodsi3, 0, uint_tp);
	set_method_param_type(tp_umodsi3, 1, uint_tp);
	set_method_res_type(tp_umodsi3, 0, uint_tp);
	umodsi3 = create_compilerlib_entity(new_id_from_str("__umodsi3"), tp_umodsi3);
}

/**
 * Maps all intrinsic calls that the backend support
 * and map all instructions the backend did not support
 * to runtime calls.
 */
static void arm_handle_intrinsics(ir_graph *irg)
{
	arm_create_runtime_entities();
	irg_walk_graph(irg, handle_intrinsic, NULL, NULL);
}

extern const arch_isa_if_t arm_isa_if;
static arm_isa_t arm_isa_template = {
	{
		&arm_isa_if,             /* isa interface */
		N_ARM_REGISTERS,
		arm_registers,
		N_ARM_CLASSES,
		arm_reg_classes,
		&arm_registers[REG_SP],  /* stack pointer */
		&arm_registers[REG_R11], /* base pointer */
		2,                       /* power of two stack alignment for calls, 2^2 == 4 */
		7,                       /* spill costs */
		5,                       /* reload costs */
	},
	ARM_FPU_ARCH_FPE,          /* FPU architecture */
};

static void arm_init(void)
{
	arm_register_init();

	arm_create_opcodes(&arm_irn_ops);
}

static void arm_finish(void)
{
	arm_free_opcodes();
}

static arch_env_t *arm_begin_codegeneration(void)
{
	arm_isa_t *isa = XMALLOC(arm_isa_t);
	*isa = arm_isa_template;

	be_gas_emit_types = false;

	return &isa->base;
}

/**
 * Closes the output file and frees the ISA structure.
 */
static void arm_end_codegeneration(void *self)
{
	free(self);
}

/**
 * Allows or disallows the creation of Psi nodes for the given Phi nodes.
 * @return 1 if allowed, 0 otherwise
 */
static int arm_is_mux_allowed(ir_node *sel, ir_node *mux_false,
                              ir_node *mux_true)
{
	(void) sel;
	(void) mux_false;
	(void) mux_true;
	return false;
}

static int arm_is_valid_clobber(const char *clobber)
{
	(void) clobber;
	return 0;
}

static void arm_lower_for_target(void)
{
	ir_mode *mode_gp = arm_reg_classes[CLASS_arm_gp].mode;
	size_t i, n_irgs = get_irp_n_irgs();

	/* lower compound param handling */
	lower_calls_with_compounds(LF_RETURN_HIDDEN);

	for (i = 0; i < n_irgs; ++i) {
		ir_graph *irg = get_irp_irg(i);
		lower_switch(irg, 4, 256, mode_gp);
	}

	for (i = 0; i < n_irgs; ++i) {
		ir_graph *irg = get_irp_irg(i);
		/* Turn all small CopyBs into loads/stores and all bigger CopyBs into
		 * memcpy calls.
		 * TODO:  These constants need arm-specific tuning. */
		lower_CopyB(irg, 31, 32, false);
	}
}

/**
 * Returns the libFirm configuration parameter for this backend.
 */
static const backend_params *arm_get_libfirm_params(void)
{
	static ir_settings_arch_dep_t ad = {
		1,    /* allow subs */
		1,    /* Muls are fast enough on ARM but ... */
		31,   /* ... one shift would be possible better */
		NULL, /* no evaluator function */
		0,    /* SMUL is needed, only in Arch M */
		0,    /* UMUL is needed, only in Arch M */
		32,   /* SMUL & UMUL available for 32 bit */
	};
	static backend_params p = {
		1,     /* big endian */
		1,     /* modulo shift efficient */
		0,     /* non-modulo shift not efficient */
		0,     /* PIC code not supported */
		&ad,   /* will be set later */
		arm_is_mux_allowed, /* allow_ifconv function */
		32,    /* machine size */
		NULL,  /* float arithmetic mode (TODO) */
		NULL,  /* long long type */
		NULL,  /* unsigned long long type */
		NULL,  /* long double type */
		0,     /* no trampoline support: size 0 */
		0,     /* no trampoline support: align 0 */
		NULL,  /* no trampoline support: no trampoline builder */
		4      /* alignment of stack parameter */
	};

	return &p;
}

/* fpu set architectures. */
static const lc_opt_enum_int_items_t arm_fpu_items[] = {
	{ "softfloat", ARM_FPU_ARCH_SOFTFLOAT },
	{ "fpe",       ARM_FPU_ARCH_FPE },
	{ "fpa",       ARM_FPU_ARCH_FPA },
	{ "vfp1xd",    ARM_FPU_ARCH_VFP_V1xD },
	{ "vfp1",      ARM_FPU_ARCH_VFP_V1 },
	{ "vfp2",      ARM_FPU_ARCH_VFP_V2 },
	{ NULL,        0 }
};

static lc_opt_enum_int_var_t arch_fpu_var = {
	&arm_isa_template.fpu_arch, arm_fpu_items
};

static const lc_opt_table_entry_t arm_options[] = {
	LC_OPT_ENT_ENUM_INT("fpunit",    "select the floating point unit", &arch_fpu_var),
	LC_OPT_LAST
};

const arch_isa_if_t arm_isa_if = {
	arm_init,
	arm_finish,
	arm_get_libfirm_params,
	arm_lower_for_target,
	arm_is_valid_clobber,

	arm_begin_codegeneration,
	arm_end_codegeneration,
	NULL,  /* get call abi */
	NULL,  /* mark remat */
	be_new_spill,
	be_new_reload,
	NULL,  /* register_saved_by */

	arm_handle_intrinsics, /* handle_intrinsics */
	arm_prepare_graph,
	arm_before_ra,
	arm_emit,
};

BE_REGISTER_MODULE_CONSTRUCTOR(be_init_arch_arm)
void be_init_arch_arm(void)
{
	lc_opt_entry_t *be_grp = lc_opt_get_grp(firm_opt_get_root(), "be");
	lc_opt_entry_t *arm_grp = lc_opt_get_grp(be_grp, "arm");

	lc_opt_add_table(arm_grp, arm_options);

	be_register_isa_if("arm", &arm_isa_if);

	arm_init_transform();
	arm_init_emitter();
}
