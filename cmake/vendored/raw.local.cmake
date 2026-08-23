# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: What the LibRaw .vcxproj could not describe. LIBRAW_WIN32_UNICODEPATHS asks for the
# datastream constructors that take a wide path, which exist only where the OS takes one.

if (NOT WIN32)
    get_target_property(_raw_defs diffractor_raw COMPILE_DEFINITIONS)
    list(FILTER _raw_defs EXCLUDE REGEX "LIBRAW_WIN32_UNICODEPATHS")
    set_target_properties(diffractor_raw PROPERTIES COMPILE_DEFINITIONS "${_raw_defs}")
endif ()
