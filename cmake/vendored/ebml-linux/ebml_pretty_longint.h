/*
 * This file is part of the Diffractor photo and video organizer
 * Copyright 2026  Zac Walker
 *
 * Purpose: correct libebml's EBML_PRETTYLONGINT for GCC. EbmlConfig.h appends ll to the literal
 * it is given, but EbmlSInteger.cpp:112 passes -0x80000000LL, which already carries a suffix, so
 * the token becomes -0x80000000LLll and no literal operator matches. The MSVC branch of the same
 * macro is simply (c), which is what C++ has needed since a hex literal took the first type that
 * fits it, so that spelling is correct here too rather than merely a workaround.
 *
 * EbmlConfig.h is included first on purpose: its own include guard then makes every later include
 * a no-op, so this redefinition stands for the whole translation unit. A plain -D would lose,
 * because the header defines the macro unconditionally and would be seen afterwards.
 */

#include <ebml/EbmlConfig.h>

#undef EBML_PRETTYLONGINT
#define EBML_PRETTYLONGINT(c) (c)
