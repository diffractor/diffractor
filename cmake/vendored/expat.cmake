# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: Build the vendored expat as diffractor::expat.
#
# Imported from third-party/expat/expat.vcxproj (Release|x64) by tools/import_vcxproj.py.
# Compiler and linker flags are not restated here: DiffractorCompilerPolicy.cmake owns those
# for every target.

include_guard(GLOBAL)

add_library(diffractor_expat STATIC
        "${CMAKE_SOURCE_DIR}/third-party/expat/lib/xmlparse.c"
        "${CMAKE_SOURCE_DIR}/third-party/expat/lib/random_rand_s.c"
        "${CMAKE_SOURCE_DIR}/third-party/expat/lib/xmlrole.c"
        "${CMAKE_SOURCE_DIR}/third-party/expat/lib/xmltok.c"
)

target_include_directories(diffractor_expat PUBLIC
        "${CMAKE_SOURCE_DIR}/third-party/expat"
        "${CMAKE_SOURCE_DIR}/third-party/expat/xmlwf"
        "${CMAKE_SOURCE_DIR}/third-party/expat/lib"
)

target_compile_definitions(diffractor_expat PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:COMPILED_FROM_DSP>")

if (WIN32)
    target_compile_definitions(diffractor_expat PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:UNICODE>" "$<$<COMPILE_LANGUAGE:C,CXX>:_UNICODE>")
endif ()

diffractor_apply_vendored_policy(diffractor_expat)

# Anything the Windows project could not describe - a header its build generates, a flag a
# different compiler needs. Hand written, and kept out of this file so that re-importing
# does not discard it.
include("${CMAKE_CURRENT_LIST_DIR}/expat.local.cmake" OPTIONAL)

add_library(diffractor::expat ALIAS diffractor_expat)
