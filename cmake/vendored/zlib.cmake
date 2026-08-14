# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: Build the vendored ZLib as diffractor::zlib.
#
# Imported from third-party/ZLib/ZLib.vcxproj (Release|x64) by tools/import_vcxproj.py.
# Compiler and linker flags are not restated here: DiffractorCompilerPolicy.cmake owns those
# for every target.

include_guard(GLOBAL)

add_library(diffractor_zlib STATIC
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/adler32.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/arch/generic/adler32_c.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/arch/generic/adler32_fold_c.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/arch/generic/chunkset_c.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/arch/generic/compare256_c.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/arch/generic/crc32_braid_c.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/arch/generic/crc32_chorba_c.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/arch/generic/crc32_fold_c.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/arch/generic/slide_hash_c.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/arch/x86/adler32_avx2.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/arch/x86/adler32_avx512.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/arch/x86/adler32_avx512_vnni.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/arch/x86/adler32_sse42.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/arch/x86/adler32_ssse3.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/arch/x86/chorba_sse2.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/arch/x86/chorba_sse41.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/arch/x86/chunkset_avx2.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/arch/x86/chunkset_avx512.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/arch/x86/chunkset_sse2.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/arch/x86/chunkset_ssse3.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/arch/x86/compare256_avx2.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/arch/x86/compare256_avx512.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/arch/x86/compare256_sse2.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/arch/x86/crc32_pclmulqdq.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/arch/x86/crc32_vpclmulqdq.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/arch/x86/slide_hash_avx2.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/arch/x86/slide_hash_sse2.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/arch/x86/x86_features.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/compress.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/cpu_features.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/crc32.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/crc32_braid_comb.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/deflate.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/deflate_fast.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/deflate_huff.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/deflate_medium.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/deflate_quick.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/deflate_rle.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/deflate_slow.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/deflate_stored.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/functable.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/gzlib.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/gzwrite.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/infback.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/inflate.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/inftrees.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/insert_string.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/insert_string_roll.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/trees.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/uncompr.c"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib/zutil.c"
)

target_include_directories(diffractor_zlib PUBLIC
        "${CMAKE_SOURCE_DIR}/third-party/ZLib"
)

target_compile_definitions(diffractor_zlib PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:ZLIB_COMPAT>" "$<$<COMPILE_LANGUAGE:C,CXX>:WITH_ALL_FALLBACKS>" "$<$<COMPILE_LANGUAGE:C,CXX>:WITH_GZFILEOP>" "$<$<COMPILE_LANGUAGE:C,CXX>:NO_FSEEKO>" "$<$<COMPILE_LANGUAGE:C,CXX>:HAVE_CPUID_MS>" "$<$<COMPILE_LANGUAGE:C,CXX>:HAVE_BUILTIN_ASSUME_ALIGNED>" "$<$<COMPILE_LANGUAGE:C,CXX>:WITH_OPTIM>" "$<$<COMPILE_LANGUAGE:C,CXX>:X86_FEATURES>" "$<$<COMPILE_LANGUAGE:C,CXX>:X86_HAVE_XSAVE_INTRIN>" "$<$<COMPILE_LANGUAGE:C,CXX>:X86_SSE2>" "$<$<COMPILE_LANGUAGE:C,CXX>:X86_SSSE3>" "$<$<COMPILE_LANGUAGE:C,CXX>:X86_SSE41>" "$<$<COMPILE_LANGUAGE:C,CXX>:X86_SSE42>" "$<$<COMPILE_LANGUAGE:C,CXX>:X86_PCLMULQDQ_CRC>" "$<$<COMPILE_LANGUAGE:C,CXX>:X86_AVX2>" "$<$<COMPILE_LANGUAGE:C,CXX>:X86_AVX512>" "$<$<COMPILE_LANGUAGE:C,CXX>:X86_AVX512VNNI>" "$<$<COMPILE_LANGUAGE:C,CXX>:X86_VPCLMULQDQ_CRC>" "$<$<COMPILE_LANGUAGE:C,CXX>:UNICODE>" "$<$<COMPILE_LANGUAGE:C,CXX>:_UNICODE>")

diffractor_apply_vendored_policy(diffractor_zlib)

add_library(diffractor::zlib ALIAS diffractor_zlib)
