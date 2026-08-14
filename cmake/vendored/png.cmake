# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: Build the vendored libpng as diffractor::png.
#
# Imported from third-party/libpng/libpng.vcxproj (Release|x64) by tools/import_vcxproj.py.
# Compiler and linker flags are not restated here: DiffractorCompilerPolicy.cmake owns those
# for every target.

include_guard(GLOBAL)

add_library(diffractor_png STATIC
        "${CMAKE_SOURCE_DIR}/third-party/libpng/intel/filter_sse2_intrinsics.c"
        "${CMAKE_SOURCE_DIR}/third-party/libpng/intel/intel_init.c"
        "${CMAKE_SOURCE_DIR}/third-party/libpng/png.c"
        "${CMAKE_SOURCE_DIR}/third-party/libpng/pngerror.c"
        "${CMAKE_SOURCE_DIR}/third-party/libpng/pngget.c"
        "${CMAKE_SOURCE_DIR}/third-party/libpng/pngmem.c"
        "${CMAKE_SOURCE_DIR}/third-party/libpng/pngpread.c"
        "${CMAKE_SOURCE_DIR}/third-party/libpng/pngread.c"
        "${CMAKE_SOURCE_DIR}/third-party/libpng/pngrio.c"
        "${CMAKE_SOURCE_DIR}/third-party/libpng/pngrtran.c"
        "${CMAKE_SOURCE_DIR}/third-party/libpng/pngrutil.c"
        "${CMAKE_SOURCE_DIR}/third-party/libpng/pngset.c"
        "${CMAKE_SOURCE_DIR}/third-party/libpng/pngtest.c"
        "${CMAKE_SOURCE_DIR}/third-party/libpng/pngtrans.c"
        "${CMAKE_SOURCE_DIR}/third-party/libpng/pngwio.c"
        "${CMAKE_SOURCE_DIR}/third-party/libpng/pngwrite.c"
        "${CMAKE_SOURCE_DIR}/third-party/libpng/pngwtran.c"
        "${CMAKE_SOURCE_DIR}/third-party/libpng/pngwutil.c"
)

target_include_directories(diffractor_png PUBLIC
        "${CMAKE_SOURCE_DIR}/third-party/libpng"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib"
)

target_compile_definitions(diffractor_png PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:UNICODE>" "$<$<COMPILE_LANGUAGE:C,CXX>:_UNICODE>")

diffractor_apply_vendored_policy(diffractor_png)

add_library(diffractor::png ALIAS diffractor_png)
