# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: What the bzip2 .vcxproj could not describe. bzlib.c calls _fdopen, which is how MSVC
# spells the POSIX fdopen it is standing in for, so the name is mapped rather than the call being
# compiled out - BZ_STRICT_ANSI would do that, but it would also make BZ2_bzdopen return NULL.

if (NOT WIN32)
    target_compile_definitions(diffractor_bzip2 PRIVATE _fdopen=fdopen)
endif ()
