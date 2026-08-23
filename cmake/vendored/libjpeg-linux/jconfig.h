// jconfig.h for Diffractor's libjpeg-turbo build on Linux.
//
// The checked-in third-party/LibJpeg/src/jconfig.h is hand-written for MSVC and is reached by a
// quoted include from jpeglib.h, so it cannot be overridden with -I. The build stages the library
// sources and drops this in instead; third-party/ is only ever read.
//
// Diffractor keeps all configuration here, and jconfigint.h just forwards to it, matching the
// arrangement the MSVC build already uses.

#define JPEG_LIB_VERSION 80
#define LIBJPEG_TURBO_VERSION 3.2.0
#define C_ARITH_CODING_SUPPORTED 1
#define D_ARITH_CODING_SUPPORTED 1
#define MEM_SRCDST_SUPPORTED 1
#define HAVE_PROTOTYPES 1
#define HAVE_UNSIGNED_CHAR 1
#define HAVE_UNSIGNED_SHORT 1
#define TRANSFORMS_SUPPORTED 1

#ifndef BITS_IN_JSAMPLE
#define BITS_IN_JSAMPLE 8
#endif

/* The jsimd routines are 8-bit only; enabling them for the 12- and 16-bit
   libraries silently substitutes 8-bit IDCT, upsample and colour convert. */
#if BITS_IN_JSAMPLE == 8
#define WITH_SIMD 1
#endif

#undef CHAR_IS_UNSIGNED
#define HAVE_STDDEF_H
#define HAVE_STDLIB_H
#undef NEED_BSD_STRINGS
#undef NEED_SYS_TYPES_H
#undef NEED_FAR_POINTERS
#undef NEED_SHORT_EXTERNAL_NAMES
#undef INCOMPLETE_TYPES_BROKEN
#undef NEON_INTRINSICS

#define PACKAGE_NAME "TurboJpeg"
#define VERSION "3.2.0"
#define BUILD __DATE__

/* Match the MSVC build's boolean width so behaviour does not differ by platform. */
typedef unsigned char boolean;
#define HAVE_BOOLEAN

#ifndef INLINE
#define INLINE __inline__ __attribute__((always_inline))
#endif

#ifndef HIDDEN
#define HIDDEN __attribute__((visibility("hidden")))
#endif

#define SIZEOF_SIZE_T 8

/* X11 headers also define INT32; XMD_H is the flag jmorecfg.h checks. */
typedef short INT16;
typedef signed int INT32;
#define XMD_H

#ifdef JPEG_INTERNALS
#undef RIGHT_SHIFT_IS_UNSIGNED
#endif

#define FALLTHROUGH __attribute__((fallthrough));

/* getenv is used only by upstream's test harness, which is not built here. */
#define NO_GETENV

#define THREAD_LOCAL __thread

/* SIMD dispatch architecture selector (values from simd/jsimdconst.h). */
#define SIMD_ARCHITECTURE X86_64
