/* WIN32 - version */
/* always debug heap support */
#include "crtdbg.h"

/* Define to 1 if you have the <malloc.h> header file. */
#define HAVE_MALLOC_H 1

/* Define to 1 if you have the <inttypes.h> header file. */
/* #undef HAVE_INTTYPES_H */

/* Define to 1 if you have the <jni.h> header file. */
/* #undef HAVE_JNI_H */

/* Define to 1 if you have the <math.h> header file. */
#define HAVE_MATH_H 1

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the <obstack.h> header file. */
#define HAVE_OBSTACK_H 1

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <strings.h> header file. */
/* #undef HAVE_STRINGS_H */

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define to 1 if you have the <io.h> header file. */
#define HAVE_IO_H 1

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "firm@ipd.info.uni-karlsruhe.de"

/* Define to the full name of this package. */
#define PACKAGE_NAME "libFIRM"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "libFIRM 1.18.1"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "libFIRM"

/* Define to the version of this package. */
#define PACKAGE_VERSION "1.18.1"

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

#define libfirm_VERSION_MAJOR 1
#define libfirm_VERSION_MINOR 18
#define libfirm_VERSION_MICRO 1

/* ---------------snip, snip ---------------------- */

/* define to enable debugging stuff. */
#define DEBUG_libfirm 1

/* define to 1 to use the ILP solver */
/* #undef WITH_ILP */

/* Define to disable assertion checking.  */
/* #undef NDEBUG */

/* Define to 1 if your processor stores words with the most significant byte
   first (like Motorola and SPARC, unlike Intel and VAX). */
/* #undef WORDS_BIGENDIAN */

/* Define the right volatile token */
/* #undef volatile */

/* Define the right const token */
/* #undef const */

#ifndef inline
#define inline __inline
#endif

/* map some non-POSIX names for Win32 */
#define snprintf    _snprintf
#define __attribute__(x)

typedef unsigned __int16 uint16_t;
typedef unsigned __int32 uint32_t;
typedef __int64 int64_t;
