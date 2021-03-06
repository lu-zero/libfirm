/*
 * This file is part of libFirm.
 * Copyright (C) 2012 University of Karlsruhe.
 */

/**
 * @file
 * @brief    Provides basic mathematical operations on values represented as strings.
 * @date     2003
 * @author   Mathias Heil
 * @brief
 *
 * The module uses a string to represent values, and provides operations
 * to perform calculations with these values.
 * Results are stored in an internal buffer, so you have to make a copy
 * of them if you need to store the result.
 *
 */
#ifndef FIRM_TV_STRCALC_H
#define FIRM_TV_STRCALC_H

#include <stdint.h>
#include <stdbool.h>
#include "irmode.h"

#ifdef STRCALC_DEBUG_ALL             /* switch on all debug options */
#  ifndef STRCALC_DEBUG
#    define STRCALC_DEBUG            /* switch on debug output */
#  endif
#  ifndef STRCALC_DEBUG_PRINTCOMP    /* print arguments and result of each computation */
#    define STRCALC_DEBUG_PRINTCOMP
#  endif
#  ifndef STRCALC_DEBUG_FULLPRINT
#    define STRCALC_DEBUG_FULLPRINT  /* print full length of values (e.g. 128 bit instead of 64 bit using default init) */
#  endif
#  ifndef STRCALC_DEBUG_GROUPPRINT
#    define STRCALC_DEBUG_GROUPPRINT /* print spaces after each 8 bits */
#  endif
#endif

/*
 * constants, typedefs, enums
 */

#define SC_DEFAULT_PRECISION 64

enum {
  SC_0 = 0,
  SC_1,
  SC_2,
  SC_3,
  SC_4,
  SC_5,
  SC_6,
  SC_7,
  SC_8,
  SC_9,
  SC_A,
  SC_B,
  SC_C,
  SC_D,
  SC_E,
  SC_F
};

/**
 * The output mode for integer values.
 */
enum base_t {
  SC_hex,   /**< hexadecimal output with small letters */
  SC_HEX,   /**< hexadecimal output with BIG letters */
  SC_DEC,   /**< decimal output */
  SC_OCT,   /**< octal output */
  SC_BIN    /**< binary output */
};

/**
 * buffer = value1 + value2
 */
void sc_add(const void *value1, const void *value2, void *buffer);

/**
 * buffer = value1 - value2
 */
void sc_sub(const void *value1, const void *value2, void *buffer);

/**
 * buffer = -value
 */
void sc_neg(const void *value, void *buffer);

/**
 * buffer = value1 & value2
 */
void sc_and(const void *value1, const void *value2, void *buffer);

/**
 * buffer = value1 & ~value2
 */
void sc_andnot(const void *value1, const void *value2, void *buffer);

/**
 * buffer = value1 | value2
 */
void sc_or(const void *value1, const void *value2, void *buffer);

/**
 * buffer = value1 ^ value2
 */
void sc_xor(const void *value1, const void *value2, void *buffer);

/**
 * buffer = ~value
 */
void sc_not(const void *value, void *buffer);

/**
 * buffer = value1 * value2
 */
void sc_mul(const void *value1, const void *value2, void *buffer);

/**
 * buffer = value1 / value2
 */
void sc_div(const void *value1, const void *value2, void *buffer);

/**
 * buffer = value1 % value2
 */
void sc_mod(const void *value1, const void *value2, void *buffer);

/**
 * div_buffer = value1 / value2
 * mod_buffer = value1 % value2
 */
void sc_divmod(const void *value1, const void *value2, void *div_buffer, void *mod_buffer);

/**
 * buffer = value1 << offset
 */
void sc_shlI(const void *val1, long shift_cnt, int bitsize, int sign, void *buffer);

/**
 * buffer = value1 << value2
 */
void sc_shl(const void *value1, const void *value2, int bitsize, int sign, void *buffer);

/**
 * buffer = value1 >>u offset
 */
void sc_shrI(const void *val1, long shift_cnt, int bitsize, int sign, void *buffer);

/**
 * buffer = value1 >>u value2
 */
void sc_shr(const void *value1, const void *value2, int bitsize, int sign, void *buffer);

/**
 * buffer = value1 >>s offset
 */
void sc_shrsI(const void *val1, long shift_cnt, int bitsize, int sign, void *buffer);

/**
 * buffer = value1 >>s value2
 */
void sc_shrs(const void *value1, const void *value2, int bitsize, int sign, void *buffer);

/**
 * buffer = 0
 */
void sc_zero(void *buffer);

/*
 * function declarations
 */
const void *sc_get_buffer(void);
int sc_get_buffer_length(void);

void sign_extend(void *buffer, ir_mode *mode);

/**
 * create an value form a string representation
 * @return 1 if ok, 0 in case of parse error
 */
int sc_val_from_str(char sign, unsigned base, const char *str,
                    size_t len, void *buffer);

/** create a value from a long */
void sc_val_from_long(long l, void *buffer);

/** create a value form an unsigned long */
void sc_val_from_ulong(unsigned long l, void *buffer);

/**
 * Construct a strcalc value form a sequence of bytes in two complement big
 * or little endian format.
 * @param bytes       pointer to array of bytes
 * @param n_bytes     number of bytes in the sequence
 * @param big_endian  interpret bytes as big_endian format if true, else
 *                    little endian
 * @param buffer      destination buffer (calc_buffer if used if NULL)
 */
void sc_val_from_bytes(unsigned char const *bytes, size_t n_bytes,
                       bool big_endian, void *buffer);

/** converts a value to a long */
long sc_val_to_long(const void *val);
uint64_t sc_val_to_uint64(const void *val);
void sc_min_from_bits(unsigned int num_bits, unsigned int sign, void *buffer);
void sc_max_from_bits(unsigned int num_bits, unsigned int sign, void *buffer);

/** truncates a value to lowest @p num_bits bits */
void sc_truncate(unsigned num_bits, void *buffer);

/**
 * Compares val1 and val2
 */
ir_relation sc_comp(void const *val1, void const *val2);

int sc_get_highest_set_bit(const void *value);
int sc_get_lowest_set_bit(const void *value);
int sc_is_zero(const void *value);
int sc_is_negative(const void *value);

/**
 * Return the bits of a tarval at a given byte-offset.
 *
 * @param value     the value
 * @param len       number of valid bits in the value
 * @param byte_ofs  the byte offset
 */
unsigned char sc_sub_bits(const void *value, int len, unsigned byte_ofs);

/**
 * Converts a tarval into a string.
 *
 * @param val1        the value pointer
 * @param bits        number of valid bits in this value
 * @param base        output base
 * @param signed_mode print it signed (only decimal mode supported
 */
const char *sc_print(const void *val1, unsigned bits, enum base_t base, int signed_mode);

/** Initialize the strcalc module.
 * Sets up internal data structures and constants
 * After the first call subsequent calls have no effect
 *
 * @param precision_in_bytes Specifies internal precision to be used
 *   for calculations. The reason for being multiples of 8 eludes me
 */
void init_strcalc(int precision_in_bytes);
void finish_strcalc(void);
int sc_get_precision(void);

/** Return the bit at a given position. */
int sc_get_bit_at(const void *value, unsigned pos);

/** Set the bit at the specified position. */
void sc_set_bit_at(void *value, unsigned pos);

/* Strange semantics */
int sc_had_carry(void);

#endif /* FIRM_TV_STRCALC_H */
