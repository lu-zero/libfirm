/* Copyright (C) 1998 - 2000 by Universitaet Karlsruhe

* All rights reserved.
*
* Author: Goetz Lindenmaier
*
*/

/* $Id$ */

/* A datatype to treat types and entities as the same. */

# ifndef _TYPE_OR_ENTITY_H_
# define _TYPE_OR_ENTITY_H_

typedef union {
  struct type   *typ;
  struct entity *ent;
} type_or_ent;


# endif /* _TYPE_OR_ENTITY_H_ */
