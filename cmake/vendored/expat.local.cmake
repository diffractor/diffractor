# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: What the expat .vcxproj could not describe. Windows gets its entropy from rand_s, which
# expat selects on _WIN32 by itself; every other platform has to be told which source to use, and
# expat refuses to compile rather than quietly picking a weak one.

if (NOT WIN32)
    set(_expat_src "${CMAKE_SOURCE_DIR}/third-party/expat/lib")

    # The define alone only moves the call site: each entropy source is a translation unit of its
    # own, and the .vcxproj lists rand_s because that is the only one Windows needs.
    target_compile_definitions(diffractor_expat PRIVATE HAVE_GETRANDOM)
    target_sources(diffractor_expat PRIVATE "${_expat_src}/random_getrandom.c")
    set_source_files_properties("${_expat_src}/random_rand_s.c"
            TARGET_DIRECTORY diffractor_expat PROPERTIES HEADER_FILE_ONLY ON)
endif ()
