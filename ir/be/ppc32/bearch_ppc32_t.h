#ifndef _BEARCH_PPC32_T_H_
#define _BEARCH_PPC32_T_H_

#include "debug.h"
#include "bearch_ppc32.h"
#include "ppc32_nodes_attr.h"
#include "../be.h"
#include "set.h"

typedef struct _ppc32_code_gen_t {
	const arch_code_generator_if_t *impl;             /**< implementation */
	ir_graph                       *irg;              /**< current irg */
	FILE                           *out;              /**< output file */
	const arch_env_t               *arch_env;         /**< the arch env */
	set                            *reg_set;          /**< set to memorize registers for FIRM nodes (e.g. phi) */
	firm_dbg_module_t              *mod;              /**< debugging module */
	int                             emit_decls;       /**< flag indicating if decls were already emitted */
	const be_irg_t                 *birg;             /**< The be-irg (contains additional information about the irg) */
	unsigned                        area_size;        /**< size of call area for the current irg */
	entity                         *area;             /**< the entity representing the call area or NULL for leaf functions */
	ir_node                        *start_succ_block; /**< the block succeeding the start block in the cfg */
	ir_node                        **blk_sched;       /**< an array containing the scheduled blocks */
} ppc32_code_gen_t;


typedef struct _ppc32_isa_t {
	const arch_isa_if_t   *impl;
	const arch_register_t *sp;            /**< The stack pointer register. */
	const arch_register_t *bp;            /**< The base pointer register. */
	const int              stack_dir;     /**< -1 for decreasing, 1 for increasing. */
	int                  num_codegens;
} ppc32_isa_t;


typedef struct _ppc32_irn_ops_t {
	const arch_irn_ops_if_t *impl;
	ppc32_code_gen_t     *cg;
} ppc32_irn_ops_t;


/** this is a struct to minimize the number of parameters
   for transformation walker */
typedef struct _ppc32_transform_env_t {
	firm_dbg_module_t *mod;      /**< The firm debugger */
	dbg_info          *dbg;      /**< The node debug info */
	ir_graph          *irg;      /**< The irg, the node should be created in */
	ir_node           *block;    /**< The block, the node should belong to */
	ir_node           *irn;      /**< The irn, to be transformed */
	ir_mode           *mode;     /**< The mode of the irn */
} ppc32_transform_env_t;


#endif /* _BEARCH_PPC32_T_H_ */
