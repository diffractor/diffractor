# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: Build the vendored brotli as diffractor::brotli.
#
# Imported from third-party/brotli/brotli.vcxproj (Release|x64) by tools/import_vcxproj.py.
# Compiler and linker flags are not restated here: DiffractorCompilerPolicy.cmake owns those
# for every target.

include_guard(GLOBAL)

add_library(diffractor_brotli STATIC
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/common/dictionary.c"
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/common/transform.c"
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/dec/bit_reader.c"
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/dec/decode.c"
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/dec/huffman.c"
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/dec/prefix.c"
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/dec/state.c"
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/dec/static_init.c"
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/enc/backward_references.c"
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/enc/backward_references_hq.c"
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/enc/bit_cost.c"
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/enc/block_splitter.c"
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/enc/brotli_bit_stream.c"
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/enc/cluster.c"
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/enc/compress_fragment.c"
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/enc/compress_fragment_two_pass.c"
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/enc/dictionary_hash.c"
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/enc/encode.c"
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/enc/encoder_dict.c"
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/enc/entropy_encode.c"
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/enc/histogram.c"
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/enc/literal_cost.c"
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/enc/memory.c"
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/enc/metablock.c"
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/enc/static_dict.c"
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/enc/static_dict_lut.c"
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/enc/static_init.c"
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/enc/utf8_util.c"
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/common/constants.c"
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/common/context.c"
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/common/platform.c"
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/common/shared_dictionary.c"
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/enc/command.c"
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/enc/compound_dictionary.c"
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/enc/fast_log.c"
)

target_include_directories(diffractor_brotli PUBLIC
        "${CMAKE_SOURCE_DIR}/third-party/brotli"
        "${CMAKE_SOURCE_DIR}/third-party/brotli/c/include"
)

target_compile_definitions(diffractor_brotli PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:UNICODE>" "$<$<COMPILE_LANGUAGE:C,CXX>:_UNICODE>")

diffractor_apply_vendored_policy(diffractor_brotli)

add_library(diffractor::brotli ALIAS diffractor_brotli)
