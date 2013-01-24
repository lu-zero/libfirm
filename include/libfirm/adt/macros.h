/*
 * This file is part of libFirm.
 * Copyright (C) 2013 Karlsruhe Institute of Technology
 */

#ifndef FIRM_ADT_MACROS_H
#define FIRM_ADT_MACROS_H

/* define a NORETURN attribute */
#ifndef NORETURN
# if defined(__GNUC__)
#  if __GNUC__ >= 3 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 70)
#   define NORETURN void __attribute__ ((noreturn))
#  endif /* __GNUC__ >= 3 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 70) */
# endif /* defined(__GNUC__) */

# if defined(_MSC_VER)
#  define NORETURN void __declspec(noreturn)
# endif /* defined(_MSC_VER) */

/* If not set above, use "void" for DOES_NOT_RETURN. */
# ifndef NORETURN
# define NORETURN void
# endif /* ifndef NORETURN */
#endif /* ifndef NORETURN */

#endif /* FIRM_ADT_MACROS_H */
