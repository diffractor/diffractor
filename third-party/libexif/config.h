#pragma once

#define GETTEXT_PACKAGE "gtk20"

/* MSVC lacks the POSIX ssize_t used by exif-loader.c (0.6.26+). */
#if defined(_MSC_VER) && !defined(_SSIZE_T_DEFINED)
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#define _SSIZE_T_DEFINED
#endif