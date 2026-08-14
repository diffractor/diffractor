# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: Build the vendored libde265 as diffractor::de265.
#
# Imported from third-party/libde265/libde265.vcxproj (Release|x64) by tools/import_vcxproj.py.
# Compiler and linker flags are not restated here: DiffractorCompilerPolicy.cmake owns those
# for every target.

include_guard(GLOBAL)

add_library(diffractor_de265 STATIC
        "${CMAKE_SOURCE_DIR}/third-party/libde265/extra/getopt.c"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/extra/getopt_long.c"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/extra/win32cond.c"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/alloc_pool.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/bitstream.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/cabac.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/contextmodel.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/de265.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/deblock.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/decctx.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/dpb.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/fallback-dct.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/fallback-deblk.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/fallback-intrapred.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/fallback-motion.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/fallback.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/image-io.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/image.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/intrapred.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/md5.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/motion.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/nal-parser.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/nal.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/pps.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/quality.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/refpic.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/sao.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/scan.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/sei.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/slice.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/sps.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/threads.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/transform.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/util.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/visualize.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/vps.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/vui.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/x86/sse-dct.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/x86/sse-deblk.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/x86/sse-intrapred.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/x86/sse-motion.cc"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/x86/sse.cc"
)

target_include_directories(diffractor_de265 PUBLIC
        "${CMAKE_SOURCE_DIR}/third-party/libde265"
        "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265"
)

target_compile_definitions(diffractor_de265 PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:HAVE_CONFIG_H>" "$<$<COMPILE_LANGUAGE:C,CXX>:LIBDE265_STATIC_BUILD>" "$<$<COMPILE_LANGUAGE:C,CXX>:UNICODE>" "$<$<COMPILE_LANGUAGE:C,CXX>:_UNICODE>")

diffractor_apply_vendored_policy(diffractor_de265)

add_library(diffractor::de265 ALIAS diffractor_de265)
