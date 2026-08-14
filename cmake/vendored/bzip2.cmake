# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: Build the vendored bzip2 as diffractor::bzip2.
#
# Imported from third-party/bzip2/bzip2.vcxproj (Release|x64) by tools/import_vcxproj.py.
# Compiler and linker flags are not restated here: DiffractorCompilerPolicy.cmake owns those
# for every target.

include_guard(GLOBAL)

add_library(diffractor_bzip2 STATIC
        "${CMAKE_SOURCE_DIR}/third-party/bzip2/blocksort.c"
        "${CMAKE_SOURCE_DIR}/third-party/bzip2/bzip2.c"
        "${CMAKE_SOURCE_DIR}/third-party/bzip2/bzlib.c"
        "${CMAKE_SOURCE_DIR}/third-party/bzip2/compress.c"
        "${CMAKE_SOURCE_DIR}/third-party/bzip2/crctable.c"
        "${CMAKE_SOURCE_DIR}/third-party/bzip2/decompress.c"
        "${CMAKE_SOURCE_DIR}/third-party/bzip2/huffman.c"
        "${CMAKE_SOURCE_DIR}/third-party/bzip2/randtable.c"
)

target_include_directories(diffractor_bzip2 PUBLIC
        "${CMAKE_SOURCE_DIR}/third-party/bzip2"
)

target_compile_definitions(diffractor_bzip2 PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:UNICODE>" "$<$<COMPILE_LANGUAGE:C,CXX>:_UNICODE>")

diffractor_apply_vendored_policy(diffractor_bzip2)

add_library(diffractor::bzip2 ALIAS diffractor_bzip2)
