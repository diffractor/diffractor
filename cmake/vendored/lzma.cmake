# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: Build the vendored liblzma as diffractor::lzma.
#
# Imported from third-party/liblzma/liblzma.vcxproj (Release|x64) by tools/import_vcxproj.py.
# Compiler and linker flags are not restated here: DiffractorCompilerPolicy.cmake owns those
# for every target.

include_guard(GLOBAL)

add_library(diffractor_lzma STATIC
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/common/tuklib_cpucores.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/common/tuklib_exit.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/common/tuklib_mbstr_fw.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/common/tuklib_mbstr_width.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/common/tuklib_open_stdxxx.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/common/tuklib_physmem.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/common/tuklib_progname.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/check/check.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/check/crc32_fast.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/check/crc64_fast.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/check/sha256.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/alone_decoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/alone_encoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/auto_decoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/block_buffer_decoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/block_buffer_encoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/block_decoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/block_encoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/block_header_decoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/block_header_encoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/block_util.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/common.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/easy_buffer_encoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/easy_decoder_memusage.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/easy_encoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/easy_encoder_memusage.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/easy_preset.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/filter_buffer_decoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/filter_buffer_encoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/filter_common.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/filter_decoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/filter_encoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/filter_flags_decoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/filter_flags_encoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/hardware_cputhreads.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/hardware_physmem.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/index.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/index_decoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/index_encoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/index_hash.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/outqueue.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/stream_buffer_decoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/stream_buffer_encoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/stream_decoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/stream_encoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/stream_encoder_mt.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/stream_flags_common.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/stream_flags_decoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/stream_flags_encoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/vli_decoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/vli_encoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common/vli_size.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/delta/delta_common.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/delta/delta_decoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/delta/delta_encoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/lzma/fastpos_table.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/lzma/lzma2_decoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/lzma/lzma2_encoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/lzma/lzma_decoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/lzma/lzma_encoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/lzma/lzma_encoder_optimum_fast.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/lzma/lzma_encoder_optimum_normal.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/lzma/lzma_encoder_presets.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/lz/lz_decoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/lz/lz_encoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/lz/lz_encoder_mf.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/rangecoder/price_table.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/simple/arm.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/simple/armthumb.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/simple/ia64.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/simple/powerpc.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/simple/simple_coder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/simple/simple_decoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/simple/simple_encoder.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/simple/sparc.c"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/simple/x86.c"
)

target_include_directories(diffractor_lzma PUBLIC
        "${CMAKE_SOURCE_DIR}/third-party/liblzma"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/common"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/common"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/api"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/check"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/delta"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/lz"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/lzma"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/rangecoder"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/simple"
)

target_compile_definitions(diffractor_lzma PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:HAVE_CONFIG_H>" "$<$<COMPILE_LANGUAGE:C,CXX>:LZMA_API_STATIC>")

if (WIN32)
    target_compile_definitions(diffractor_lzma PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:UNICODE>" "$<$<COMPILE_LANGUAGE:C,CXX>:_UNICODE>")
endif ()

diffractor_apply_vendored_policy(diffractor_lzma)

# Anything the Windows project could not describe - a header its build generates, a flag a
# different compiler needs. Hand written, and kept out of this file so that re-importing
# does not discard it.
include("${CMAKE_CURRENT_LIST_DIR}/lzma.local.cmake" OPTIONAL)

add_library(diffractor::lzma ALIAS diffractor_lzma)
