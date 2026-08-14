# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: Build the vendored hunspell as diffractor::hunspell.
#
# Imported from third-party/hunspell/hunspell.vcxproj (Release|x64) by tools/import_vcxproj.py.
# Compiler and linker flags are not restated here: DiffractorCompilerPolicy.cmake owns those
# for every target.

include_guard(GLOBAL)

add_library(diffractor_hunspell STATIC
        "${CMAKE_SOURCE_DIR}/third-party/hunspell/affentry.cxx"
        "${CMAKE_SOURCE_DIR}/third-party/hunspell/affixmgr.cxx"
        "${CMAKE_SOURCE_DIR}/third-party/hunspell/csutil.cxx"
        "${CMAKE_SOURCE_DIR}/third-party/hunspell/filemgr.cxx"
        "${CMAKE_SOURCE_DIR}/third-party/hunspell/hashmgr.cxx"
        "${CMAKE_SOURCE_DIR}/third-party/hunspell/hunspell.cxx"
        "${CMAKE_SOURCE_DIR}/third-party/hunspell/hunzip.cxx"
        "${CMAKE_SOURCE_DIR}/third-party/hunspell/phonet.cxx"
        "${CMAKE_SOURCE_DIR}/third-party/hunspell/replist.cxx"
        "${CMAKE_SOURCE_DIR}/third-party/hunspell/suggestmgr.cxx"
)

target_include_directories(diffractor_hunspell PUBLIC
        "${CMAKE_SOURCE_DIR}/third-party/hunspell"
)

target_compile_definitions(diffractor_hunspell PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:W32>" "$<$<COMPILE_LANGUAGE:C,CXX>:HUNSPELL_STATIC>")

if (WIN32)
    target_compile_definitions(diffractor_hunspell PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:UNICODE>" "$<$<COMPILE_LANGUAGE:C,CXX>:_UNICODE>")
endif ()

diffractor_apply_vendored_policy(diffractor_hunspell)

# Anything the Windows project could not describe - a header its build generates, a flag a
# different compiler needs. Hand written, and kept out of this file so that re-importing
# does not discard it.
include("${CMAKE_CURRENT_LIST_DIR}/hunspell.local.cmake" OPTIONAL)

add_library(diffractor::hunspell ALIAS diffractor_hunspell)
