
/* $Id$ */

# ifndef _TYPE_T_H_
# define _TYPE_T_H_

# include "type.h"

/****h* libfirm/type_t.h
 *
 * NAME
 *   file type_t.h
 * COPYRIGHT
 *   (C) 2001 by Universitaet Karlsruhe
 * AUTHORS
 *   Goetz Lindenmaier
 * NOTES
 *   This file contains the datatypes hidden in type.h.
 * SEE ALSO
 *   type.h tpop_t.h tpop.h
 *****
 */

typedef struct {
  entity **members;    /* fields and methods of this class */
  type   **subtypes;   /* direct subtypes */
  type   **supertypes; /* direct supertypes */
  peculiarity peculiarity;
} cls_attr;

typedef struct {
  entity **members;    /* fields of this struct. No method entities
			  allowed. */
} stc_attr;

typedef struct {
  int n_params;        /* number of parameters */
  type **param_type;   /* code generation needs this information.
                          @@@ Should it be generated by the frontend,
                          or does this impose unnecessary work for
                          optimizations that change the parameters of
                          methods? */
  int n_res;           /* number of results */
  type **res_type;     /* array with result types */
} mtd_attr;

typedef struct {
  int     n_types;
  /* type  **unioned_type; * a list of unioned types. */
  /* ident **delim_names;  * names of the union delimiters. */
  entity **members;    /* fields of this union. No method entities
			  allowed.  */

} uni_attr;

typedef struct {
  int   n_dimensions;  /* Number of array dimensions.  */
  ir_node **lower_bound;   /* Lower bounds of dimensions.  Usually all 0. */
  ir_node **upper_bound;   /* Upper bounds or dimensions. */
  int *order;              /* Ordering of dimensions. */
  type *element_type;  /* The type of the array elements. */
  entity *element_ent; /* Entity for the array elements, to be used for
			  element selection with Sel. */
} arr_attr;

typedef struct {
  int      n_enums;    /* Number of enumerators. */
  tarval **enumer;     /* Contains all constants that represent a member
                          of the enum -- enumerators. */
  ident  **enum_nameid;/* Contains the names of the enum fields as specified by
                          the source program */
} enm_attr;

typedef struct {
  type *points_to;     /* The type of the enitity the pointer points to. */
} ptr_attr;

/*
typedef struct {   * No private attr yet! *
} pri_attr; */


/*
typedef struct {        * No private attr, must be smaller than others! *
} id_attr;
*/


typedef union {
  cls_attr ca;
  stc_attr sa;
  mtd_attr ma;
  uni_attr ua;
  arr_attr aa;
  enm_attr ea;
  ptr_attr pa;
} tp_attr;

struct type {
  firm_kind kind;
  tp_op *type_op;
  ident *name;
  type_state state;        /* Represents the types state: layout undefined or
			      fixed. */
  int size;                /* Size of an entity of this type.  This is determined
			      when fixing the layout of this class.  Size must be
			      given in bytes. */
  ir_mode *mode;           /* The mode for atomic types */
  unsigned long visit;     /* visited counter for walks of the type information */
  void *link;              /* holds temporary data - like in irnode_t.h */
  tp_attr attr;            /* type kind specific fields. This must be the last
			      entry in this struct!  Varying size! */
};

/****f* type_t.h/new_type
 *
 * NAME
 *   new_type - creates a new type representation
 * SYNOPSIS
 *  type *new_type(tp_op *type_op, ir_mode *mode, ident* name);
 * INPUTS
 *   type_op - the kind of this type.  May not be type_id.
 *   mode    - the mode to be used for this type, may be NULL
 *   name    - an ident for the name of this type.
 * RESULT
 *   a new type of the given type.  The remaining private attributes are not
 *   initalized.  The type is in state layout_undefined.
 ***
 */
inline type *
new_type(tp_op *type_op,
	 ir_mode *mode,
	 ident* name);
void free_type_attrs       (type *tp);

inline void free_class_attrs      (type *clss);
inline void free_struct_attrs     (type *strct);
inline void free_method_attrs     (type *method);
inline void free_union_attrs      (type *uni);
inline void free_array_attrs      (type *array);
inline void free_enumeration_attrs(type *enumeration);
inline void free_pointer_attrs    (type *pointer);
inline void free_primitive_attrs  (type *primitive);


# endif /* _TYPE_T_H_ */
