# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: Build the vendored dav1d as diffractor::dav1d.
#
# Imported from third-party/dav1d/dav1d.vcxproj (Release|x64) by tools/import_vcxproj.py.
# Compiler and linker flags are not restated here: DiffractorCompilerPolicy.cmake owns those
# for every target.

include_guard(GLOBAL)

add_library(diffractor_dav1d STATIC
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/cdf.c"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/cpu.c"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/ctx.c"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/data.c"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/decode.c"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/dequant_tables.c"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/getbits.c"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/intra_edge.c"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/itx_1d.c"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/lf_mask.c"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/lib.c"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/log.c"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/mem.c"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/msac.c"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/obu.c"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/pal.c"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/picture.c"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/qm.c"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/ref.c"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/refmvs.c"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/scan.c"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/tables.c"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/thread_task.c"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/tmpl16.c"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/tmpl16b.c"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/tmpl8.c"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/tmpl8b.c"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/warpmv.c"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/wedge.c"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/win32/thread.c"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/cpu.c"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/cdef16_avx2.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/cdef16_avx512.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/cdef16_sse.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/cdef_avx2.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/cdef_avx512.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/cdef_sse.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/cpuid.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/filmgrain16_avx2.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/filmgrain16_avx512.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/filmgrain16_sse.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/filmgrain_avx2.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/filmgrain_avx512.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/filmgrain_sse.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/ipred16_avx2.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/ipred16_avx512.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/ipred16_sse.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/ipred_avx2.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/ipred_avx512.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/ipred_sse.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/itx16_avx2.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/itx16_avx512.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/itx16_sse.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/itx_avx2.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/itx_avx512.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/itx_sse.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/loopfilter16_avx2.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/loopfilter16_avx512.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/loopfilter16_sse.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/loopfilter_avx2.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/loopfilter_avx512.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/loopfilter_sse.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/looprestoration16_avx2.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/looprestoration16_avx512.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/looprestoration16_sse.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/looprestoration_avx2.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/looprestoration_avx512.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/looprestoration_sse.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/mc16_avx2.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/mc16_avx512.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/mc16_sse.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/mc_avx2.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/mc_avx512.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/mc_sse.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/msac.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/pal.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/refmvs.asm"
)

target_include_directories(diffractor_dav1d PUBLIC
        "${CMAKE_SOURCE_DIR}/third-party/dav1d"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/include"
)

if (WIN32)
    target_compile_definitions(diffractor_dav1d PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:UNICODE>" "$<$<COMPILE_LANGUAGE:C,CXX>:_UNICODE>")
endif ()

# Assembler include paths and definitions. These are not compiler settings and reach
# nasm no other way. Grouping is by the source's own directory, because that is what
# MSBuild supplied per item as %(RootDir)%(Directory) and every %include resolves against.
set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/cdef16_avx2.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/cdef16_avx512.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/cdef16_sse.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/cdef_avx2.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/cdef_avx512.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/cdef_sse.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/cpuid.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/filmgrain16_avx2.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/filmgrain16_avx512.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/filmgrain16_sse.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/filmgrain_avx2.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/filmgrain_avx512.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/filmgrain_sse.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/ipred16_avx2.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/ipred16_avx512.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/ipred16_sse.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/ipred_avx2.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/ipred_avx512.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/ipred_sse.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/itx16_avx2.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/itx16_avx512.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/itx16_sse.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/itx_avx2.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/itx_avx512.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/itx_sse.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/loopfilter16_avx2.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/loopfilter16_avx512.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/loopfilter16_sse.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/loopfilter_avx2.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/loopfilter_avx512.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/loopfilter_sse.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/looprestoration16_avx2.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/looprestoration16_avx512.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/looprestoration16_sse.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/looprestoration_avx2.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/looprestoration_avx512.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/looprestoration_sse.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/mc16_avx2.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/mc16_avx512.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/mc16_sse.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/mc_avx2.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/mc_avx512.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/mc_sse.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/msac.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/pal.asm"
        "${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/refmvs.asm"
        PROPERTIES COMPILE_FLAGS "-I${CMAKE_SOURCE_DIR}/third-party/dav1d/src/x86/ -I${CMAKE_SOURCE_DIR}/third-party/dav1d/ -I${CMAKE_SOURCE_DIR}/third-party/dav1d/src/")

if (MSVC)
    target_compile_options(diffractor_dav1d PRIVATE $<$<COMPILE_LANGUAGE:C,CXX>:/experimental:c11atomics>)
endif ()

diffractor_apply_vendored_policy(diffractor_dav1d)

# Anything the Windows project could not describe - a header its build generates, a flag a
# different compiler needs. Hand written, and kept out of this file so that re-importing
# does not discard it.
include("${CMAKE_CURRENT_LIST_DIR}/dav1d.local.cmake" OPTIONAL)

add_library(diffractor::dav1d ALIAS diffractor_dav1d)
