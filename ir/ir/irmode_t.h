/*
 * Project:     libFIRM
 * File name:   ir/ir/irmode_t.h
 * Purpose:     Data modes of operations -- private header.
 * Author:      Martin Trapp, Christian Schaefer
 * Modified by: Goetz Lindenmaier, Mathias Heil
 * Created:
 * CVS-ID:      $Id$
 * Copyright:   (c) 1998-2003 Universitšt Karlsruhe
 * Licence:     This file protected by GPL -  GNU GENERAL PUBLIC LICENSE.
 */


/**
 * @file irmode_t.h
 */

# ifndef _IRMODE_T_H_
# define _IRMODE_T_H_

# include "irmode.h"
# include "tv.h"

/** This struct is supposed to completely define a mode. **/
struct ir_mode {
  firm_kind         kind;       /**< distinguishes this node from others */
  modecode          code;       /**< unambiguous identifier of a mode */
  ident             *name;      /**< Name ident of this mode */

  /* ----------------------------------------------------------------------- */
  /* On changing this struct you have to evaluate the mode_are_equal function!*/
  mode_sort         sort;          /**< coarse classification of this mode:
                                     int, float, reference ...
                                     (see irmode.h) */
  mode_arithmetic   arithmetic;    /**< different arithmetic operations possible with a mode */
  int               size;          /**< size of the mode in Bits. */
  int               align;         /**< byte alignment */
  unsigned          sign:1;        /**< signedness of this mode */
  unsigned int      modulo_shift;  /**< number of bits a valus of this mode will be shifted */
  unsigned          vector_elem;   /**< if this is not equal 1, this is a vector mode with
				        vector_elem number of elements, size contains the size
					of all bits and must be dividable by vector_elem */

  /* ----------------------------------------------------------------------- */
  tarval            *min;
  tarval            *max;
  tarval            *null;
  tarval            *one;
  void              *link;      /**< To store some intermediate information */
  const void        *tv_priv;   /**< tarval module will save private data here */
};

#endif /* _IRMODE_T_H_ */
