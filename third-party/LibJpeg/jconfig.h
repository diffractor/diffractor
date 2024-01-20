/* jconfig.vc --- jconfig.h for Microsoft Visual C++ on Windows 95 or NT. */
/* see jconfig.txt for explanations */

#define JPEG_LIB_VERSION 80
#define LIBJPEG_TURBO_VERSION 3.0.0
#define C_ARITH_CODING_SUPPORTED 1
#define D_ARITH_CODING_SUPPORTED 1
#define MEM_SRCDST_SUPPORTED 1
#define WITH_SIMD 1
#define HAVE_PROTOTYPES 1
#define HAVE_UNSIGNED_CHAR 1
#define HAVE_UNSIGNED_SHORT 1
#define TRANSFORMS_SUPPORTED 1

#ifndef BITS_IN_JSAMPLE
#define BITS_IN_JSAMPLE 8
#endif


/* #define void char */
/* #define const */
#undef CHAR_IS_UNSIGNED
#define HAVE_STDDEF_H
#define HAVE_STDLIB_H
#undef NEED_BSD_STRINGS
#undef NEED_SYS_TYPES_H
#undef NEED_FAR_POINTERS	/* we presume a 32-bit flat memory model */
#undef NEED_SHORT_EXTERNAL_NAMES
#undef INCOMPLETE_TYPES_BROKEN
#undef NO_GETENV
#undef NEON_INTRINSICS

#define PACKAGE_NAME "TurboJpeg"
#define VERSION "3.0.0"
#define BUILD __DATE__ 



/* Define "boolean" as unsigned char, not int, per Windows custom */
#ifndef __RPCNDR_H__		/* don't conflict if rpcndr.h already read */
typedef unsigned char boolean;
#endif

#ifndef INLINE
#define INLINE __inline
#endif

#define HAVE_BOOLEAN		/* prevent jmorecfg.h from redefining it */

#ifdef _WIN64
#define SIZEOF_SIZE_T 8
#else
#define SIZEOF_SIZE_T 4
#endif


/* Define "INT32" as int, not long, per Windows custom */
#if !(defined(_BASETSD_H_) || defined(_BASETSD_H))   /* don't conflict if basetsd.h already read */
typedef short INT16;
typedef signed int INT32;
#endif
#define XMD_H                   /* prevent jmorecfg.h from redefining it */

#ifdef JPEG_INTERNALS

#undef RIGHT_SHIFT_IS_UNSIGNED

#endif /* JPEG_INTERNALS */

#define HAVE_INTRIN_H 1
#define FALLTHROUGH
#define NO_GETENV

#ifdef _M_ARM64
#define NEON_INTRINSICS
#endif
