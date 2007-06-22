/*
 * Copyright (C) 1995-2007 University of Karlsruhe.  All right reserved.
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
 * @brief   A bitset implementation.
 * @author  Sebastian Hack
 * @date    15.10.2004
 * @version $Id$
 */
#ifndef FIRM_ADT_BITSET_H
#define FIRM_ADT_BITSET_H

#include "firm_config.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "xmalloc.h"
#include "bitfiddle.h"

typedef unsigned int bitset_pos_t;

#include "bitset_std.h"

#if defined(__GNUC__) && defined(__i386__)
#include "bitset_ia32.h"
#endif

typedef struct _bitset_t {
	bitset_pos_t units;
	bitset_pos_t size;
} bitset_t;

#define BS_UNIT_SIZE         sizeof(bitset_unit_t)
#define BS_UNIT_SIZE_BITS    (BS_UNIT_SIZE * 8)
#define BS_UNIT_MASK         (BS_UNIT_SIZE_BITS - 1)

#define BS_DATA(bs)          ((bitset_unit_t *) ((char *) (bs) + sizeof(bitset_t)))
#define BS_UNITS(bits)       (round_up2(bits, BS_UNIT_SIZE_BITS) / BS_UNIT_SIZE_BITS)
#define BS_TOTAL_SIZE(bits)  (sizeof(bitset_t) + BS_UNITS(bits) * BS_UNIT_SIZE)

/**
 * Initialize a bitset.
 * This functions should not be called.
 *
 * Note that this function needs three macros which must be provided by the
 * bitfield implementor:
 * - _bitset_overall_size(size) The overall size that must be
 *   allocated for the bitfield in bytes.
 * - _bitset_units(size) The number of units that will be
 *   present in the bitfield for a given highest bit.
 * - _bitset_data_ptr(data, size) This produces as pointer to the
 *   first unit in the allocated memory area. The main reason for this
 *   macro is, that some bitset implementors want control over memory
 *   alignment.
 *
 * @param area A pointer to memory reserved for the bitset.
 * @param size The size of the bitset in bits.
 * @return A pointer to the initialized bitset.
 */
static INLINE bitset_t *_bitset_prepare(void *area, bitset_pos_t size)
{
	bitset_t *ptr = area;
	memset(ptr, 0, BS_TOTAL_SIZE(size));
	ptr->units = BS_UNITS(size);
	ptr->size  = size;
	return ptr;
}

/**
 * Mask out all bits, which are only there, because the number
 * of bits in the set didn't match a unit size boundary.
 * @param bs The bitset.
 * @return The masked bitset.
 */
static INLINE bitset_t *_bitset_mask_highest(bitset_t *bs)
{
	bitset_pos_t rest = bs->size & BS_UNIT_MASK;
	if (rest)
		BS_DATA(bs)[bs->units - 1] &= (1 << rest) - 1;
	return bs;
}

/**
 * Get the capacity of the bitset in bits.
 * @param bs The bitset.
 * @return The capacity in bits of the bitset.
 */
#define bitset_capacity(bs) ((bs)->units * BS_UNIT_SIZE_BITS)

/**
 * Get the size of the bitset in bits.
 * @note Note the difference between capacity and size.
 * @param bs The bitset.
 * @return The highest bit which can be set or cleared plus 1.
 */
#define bitset_size(bs)  ((bs)->size)

/**
 * Allocate a bitset on an obstack.
 * @param obst The obstack.
 * @param size The greatest bit that shall be stored in the set.
 * @return A pointer to an empty initialized bitset.
 */
#define bitset_obstack_alloc(obst,size) \
	_bitset_prepare(obstack_alloc(obst, BS_TOTAL_SIZE(size)), size)

/**
 * Allocate a bitset via malloc.
 * @param size The greatest bit that shall be stored in the set.
 * @return A pointer to an empty initialized bitset.
 */
#define bitset_malloc(size) \
	_bitset_prepare(xmalloc(BS_TOTAL_SIZE(size)), size)

/**
 * Free a bitset allocated with bitset_malloc().
 * @param bs The bitset.
 */
#define bitset_free(bs) free(bs)

/**
 * Allocate a bitset on the stack via alloca.
 * @param size The greatest bit that shall be stored in the set.
 * @return A pointer to an empty initialized bitset.
 */
#define bitset_alloca(size) \
	_bitset_prepare(alloca(BS_TOTAL_SIZE(size)), size)


/**
 * Get the unit which contains a specific bit.
 * This function is internal.
 * @param bs The bitset.
 * @param bit The bit.
 * @return A pointer to the unit containing the bit.
 */
static INLINE bitset_unit_t *_bitset_get_unit(const bitset_t *bs, bitset_pos_t bit)
{
	assert(bit <= bs->size && "Bit to large");
	return BS_DATA(bs) + bit / BS_UNIT_SIZE_BITS;
}

/**
 * Set a bit in the bitset.
 * @param bs The bitset.
 * @param bit The bit to set.
 */
static INLINE void bitset_set(bitset_t *bs, bitset_pos_t bit)
{
	bitset_unit_t *unit = _bitset_get_unit(bs, bit);
	_bitset_inside_set(unit, bit & BS_UNIT_MASK);
}

/**
 * Clear a bit in the bitset.
 * @param bs The bitset.
 * @param bit The bit to clear.
 */
static INLINE void bitset_clear(bitset_t *bs, bitset_pos_t bit)
{
	bitset_unit_t *unit = _bitset_get_unit(bs, bit);
	_bitset_inside_clear(unit, bit & BS_UNIT_MASK);
}

/**
 * Check, if a bit is set.
 * @param bs The bitset.
 * @param bit The bit to check for.
 * @return 1, if the bit was set, 0 if not.
 */
static INLINE int bitset_is_set(const bitset_t *bs, bitset_pos_t bit)
{
	bitset_unit_t *unit = _bitset_get_unit(bs, bit);
	return _bitset_inside_is_set(unit, bit & BS_UNIT_MASK);
}

/**
 * Flip a bit in a bitset.
 * @param bs The bitset.
 * @param bit The bit to flip.
 */
static INLINE void bitset_flip(bitset_t *bs, bitset_pos_t bit)
{
	bitset_unit_t *unit = _bitset_get_unit(bs, bit);
	_bitset_inside_flip(unit, bit & BS_UNIT_MASK);
}

/**
 * Flip the whole bitset.
 * @param bs The bitset.
 */
static INLINE void bitset_flip_all(bitset_t *bs)
{
	bitset_pos_t i;
	for(i = 0; i < bs->units; i++)
		_bitset_inside_flip_unit(&BS_DATA(bs)[i]);
	_bitset_mask_highest(bs);
}

/**
 * Copy a bitset to another.
 * @param tgt The target bitset.
 * @param src The source bitset.
 * @return The target bitset.
 */
static INLINE bitset_t *bitset_copy(bitset_t *tgt, const bitset_t *src)
{
	bitset_pos_t tu = tgt->units;
	bitset_pos_t su = src->units;
	bitset_pos_t min_units = tu < su ? tu : su;
	memcpy(BS_DATA(tgt), BS_DATA(src), min_units * BS_UNIT_SIZE);
	if(tu > min_units)
		memset(BS_DATA(tgt) + min_units, 0, BS_UNIT_SIZE * (tu - min_units));
	return _bitset_mask_highest(tgt);
}

/**
 * Find the next set bit from a given bit.
 * @note Note that if pos is set, pos is returned.
 * @param bs The bitset.
 * @param pos The bit from which to search for the next set bit.
 * @return The next set bit from pos on, or -1, if no set bit was found
 * after pos.
 */
static INLINE bitset_pos_t _bitset_next(const bitset_t *bs,
		bitset_pos_t pos, int set)
{
	bitset_pos_t unit_number = pos / BS_UNIT_SIZE_BITS;
	bitset_pos_t res;

	if(pos >= bs->size)
		return -1;

	{
		bitset_pos_t bit_in_unit = pos & BS_UNIT_MASK;
		bitset_pos_t in_unit_mask = (1 << bit_in_unit) - 1;

		/*
		 * Mask out the bits smaller than pos in the current unit.
		 * We are only interested in bits set higher than pos.
		 */
		bitset_unit_t curr_unit = BS_DATA(bs)[unit_number];

		/*
		 * Find the next bit set in the unit.
		 * Mind that this function returns 0, if the unit is -1 and
		 * counts the bits from 1 on.
		 */
		bitset_pos_t next_in_this_unit =
			_bitset_inside_ntz_value((set ? curr_unit : ~curr_unit) & ~in_unit_mask);

		/* If there is a bit set in the current unit, exit. */
		if (next_in_this_unit < BS_UNIT_SIZE_BITS) {
			res = next_in_this_unit + unit_number * BS_UNIT_SIZE_BITS;
			return res < bs->size ? res : (bitset_pos_t) -1;
		}

		/* Else search for set bits in the next units. */
		else {
			bitset_pos_t i;
			for(i = unit_number + 1; i < bs->units; ++i) {
				bitset_unit_t data = BS_DATA(bs)[i];
				bitset_pos_t first_set =
					_bitset_inside_ntz_value(set ? data : ~data);

				if (first_set < BS_UNIT_SIZE_BITS) {
					res = first_set + i * BS_UNIT_SIZE_BITS;
					return res < bs->size ? res : (bitset_pos_t) -1;
				}
			}
		}
	}

	return -1;
}

#define bitset_next_clear(bs,pos) _bitset_next((bs), (pos), 0)
#define bitset_next_set(bs,pos) _bitset_next((bs), (pos), 1)

/**
 * Convenience macro for bitset iteration.
 * @param bitset The bitset.
 * @param elm A unsigned long variable.
 */
#define bitset_foreach(bitset,elm) \
	for(elm = bitset_next_set(bitset,0); elm != (bitset_pos_t) -1; elm = bitset_next_set(bitset,elm+1))


#define bitset_foreach_clear(bitset,elm) \
	for(elm = bitset_next_clear(bitset,0); elm != (bitset_pos_t) -1; elm = bitset_next_clear(bitset,elm+1))

/**
 * Count the bits set.
 * This can also be seen as the cardinality of the set.
 * @param bs The bitset.
 * @return The number of bits set in the bitset.
 */
static INLINE unsigned bitset_popcnt(const bitset_t *bs)
{
	bitset_pos_t  i;
	bitset_unit_t *unit;
	unsigned      pop = 0;

	for (i = 0, unit = BS_DATA(bs); i < bs->units; ++i, ++unit)
		pop += _bitset_inside_pop(unit);

	return pop;
}

/**
 * Clear the bitset.
 * This sets all bits to zero.
 * @param bs The bitset.
 */
static INLINE bitset_t *bitset_clear_all(bitset_t *bs)
{
	memset(BS_DATA(bs), 0, BS_UNIT_SIZE * bs->units);
	return bs;
}

/**
 * Set the bitset.
 * This sets all bits to one.
 * @param bs The bitset.
 */
static INLINE bitset_t *bitset_set_all(bitset_t *bs)
{
	memset(BS_DATA(bs), -1, bs->units * BS_UNIT_SIZE);
	return _bitset_mask_highest(bs);
}

/**
 * Check, if one bitset is contained by another.
 * That is, each bit set in lhs is also set in rhs.
 * @param lhs A bitset.
 * @param rhs Another bitset.
 * @return 1, if all bits in lhs are also set in rhs, 0 otherwise.
 */
static INLINE int bitset_contains(const bitset_t *lhs, const bitset_t *rhs)
{
	bitset_pos_t n = lhs->units < rhs->units ? lhs->units : rhs->units;
	bitset_pos_t i;

	for(i = 0; i < n; ++i) {
		bitset_unit_t lu = BS_DATA(lhs)[i];
		bitset_unit_t ru = BS_DATA(rhs)[i];

		if((lu | ru) & ~ru)
			return 0;
	}

	/*
	 * If the left hand sinde is a larger bitset than rhs,
	 * we have to check, that all extra bits in lhs are 0
	 */
	if(lhs->units > n) {
		for(i = n; i < lhs->units; ++i) {
			if(BS_DATA(lhs)[i] != 0)
				return 0;
		}
	}

	return 1;
}

/**
 * Treat the bitset as a number and subtract 1.
 * @param bs The bitset.
 * @return The same bitset.
 */
static INLINE void bitset_minus1(bitset_t *bs)
{
#define _SH (sizeof(bitset_unit_t) * 8 - 1)

	bitset_pos_t i;

	for(i = 0; i < bs->units; ++i) {
		bitset_unit_t unit = BS_DATA(bs)[i];
		bitset_unit_t um1  = unit - 1;

		BS_DATA(bs)[i] = um1;

		if(((unit >> _SH) ^ (um1 >> _SH)) == 0)
			break;
	}
#undef _SH
}

/**
 * Check if two bitsets intersect.
 * @param a The first bitset.
 * @param b The second bitset.
 * @return 1 if they have a bit in common, 0 if not.
 */
static INLINE int bitset_intersect(const bitset_t *a, const bitset_t *b)
{
	bitset_pos_t n = a->units < b->units ? a->units : b->units;
	bitset_pos_t i;

	for (i = 0; i < n; ++i)
		if (BS_DATA(a)[i] & BS_DATA(b)[i])
			return 1;

	return 0;
}

/**
 * Check, if a bitset is empty.
 * @param a The bitset.
 * @return 1, if the bitset is empty, 0 if not.
 */
static INLINE int bitset_is_empty(const bitset_t *a)
{
	bitset_pos_t i;
	for (i = 0; i < a->units; ++i)
		if (BS_DATA(a)[i] != 0)
			return 0;
	return 1;
}

/**
 * Print a bitset to a stream.
 * The bitset is printed as a comma separated list of bits set.
 * @param file The stream.
 * @param bs The bitset.
 */
static INLINE void bitset_fprint(FILE *file, const bitset_t *bs)
{
	const char *prefix = "";
	int i;

	putc('{', file);
	for(i = bitset_next_set(bs, 0); i != -1; i = bitset_next_set(bs, i + 1)) {
		fprintf(file, "%s%u", prefix, i);
		prefix = ",";
	}
	putc('}', file);
}

static INLINE void bitset_debug_fprint(FILE *file, const bitset_t *bs)
{
	bitset_pos_t i;

	fprintf(file, "%u:", bs->units);
	for(i = 0; i < bs->units; ++i)
		fprintf(file, " " BITSET_UNIT_FMT, BS_DATA(bs)[i]);
}

/**
 * Perform tgt = tgt \ src operation.
 * @param tgt  The target bitset.
 * @param src  The source bitset.
 * @return the tgt set.
 */
static INLINE bitset_t *bitset_andnot(bitset_t *tgt, const bitset_t *src);

/**
 * Perform Union, tgt = tgt u src operation.
 * @param tgt  The target bitset.
 * @param src  The source bitset.
 * @return the tgt set.
 */
static INLINE bitset_t *bitset_or(bitset_t *tgt, const bitset_t *src);

/**
 * Perform tgt = tgt ^ ~src operation.
 * @param tgt  The target bitset.
 * @param src  The source bitset.
 * @return the tgt set.
 */
static INLINE bitset_t *bitset_xor(bitset_t *tgt, const bitset_t *src);

/*
 * Here, the binary operations follow.
 * And, Or, And Not, Xor are available.
 */
#define BINARY_OP(op) \
static INLINE bitset_t *bitset_ ## op(bitset_t *tgt, const bitset_t *src) \
{ \
	bitset_pos_t i; \
	bitset_pos_t n = tgt->units > src->units ? src->units : tgt->units; \
	for(i = 0; i < n; i += _BITSET_BINOP_UNITS_INC) \
		_bitset_inside_binop_ ## op(&BS_DATA(tgt)[i], &BS_DATA(src)[i]); \
	if(n < tgt->units) \
		_bitset_clear_rest(&BS_DATA(tgt)[i], tgt->units - i); \
	return _bitset_mask_highest(tgt); \
}

/*
 * Define the clear rest macro for the and, since it is the only case,
 * were non existed (treated as 0) units in the src must be handled.
 * For all other operations holds: x Op 0 = x for Op in { Andnot, Or, Xor }
 *
 * For and, each bitset implementer has to provide the macro
 * _bitset_clear_units(data, n), which clears n units from the pointer
 * data on.
 */
#define _bitset_clear_rest(data,n) _bitset_inside_clear_units(data, n)
BINARY_OP(and)
#undef _bitset_clear_rest
#define _bitset_clear_rest(data,n) do { } while(0)

BINARY_OP(andnot)
BINARY_OP(or)
BINARY_OP(xor)

#endif
