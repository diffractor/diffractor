# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: Build the vendored minizip as diffractor::minizip.
#
# Imported from third-party/minizip/minizip.vcxproj (Release|x64) by tools/import_vcxproj.py.
# Compiler and linker flags are not restated here: DiffractorCompilerPolicy.cmake owns those
# for every target.

include_guard(GLOBAL)

add_library(diffractor_minizip STATIC
        "${CMAKE_SOURCE_DIR}/third-party/minizip/compat/ioapi.c"
        "${CMAKE_SOURCE_DIR}/third-party/minizip/compat/unzip.c"
        "${CMAKE_SOURCE_DIR}/third-party/minizip/compat/zip.c"
        "${CMAKE_SOURCE_DIR}/third-party/minizip/mz_crypt.c"
        "${CMAKE_SOURCE_DIR}/third-party/minizip/mz_crypt_winvista.c"
        "${CMAKE_SOURCE_DIR}/third-party/minizip/mz_os.c"
        "${CMAKE_SOURCE_DIR}/third-party/minizip/mz_os_win32.c"
        "${CMAKE_SOURCE_DIR}/third-party/minizip/mz_strm.c"
        "${CMAKE_SOURCE_DIR}/third-party/minizip/mz_strm_buf.c"
        "${CMAKE_SOURCE_DIR}/third-party/minizip/mz_strm_bzip.c"
        "${CMAKE_SOURCE_DIR}/third-party/minizip/mz_strm_lzma.c"
        "${CMAKE_SOURCE_DIR}/third-party/minizip/mz_strm_mem.c"
        "${CMAKE_SOURCE_DIR}/third-party/minizip/mz_strm_os_win32.c"
        "${CMAKE_SOURCE_DIR}/third-party/minizip/mz_strm_pkcrypt.c"
        "${CMAKE_SOURCE_DIR}/third-party/minizip/mz_strm_split.c"
        "${CMAKE_SOURCE_DIR}/third-party/minizip/mz_strm_wzaes.c"
        "${CMAKE_SOURCE_DIR}/third-party/minizip/mz_strm_zlib.c"
        "${CMAKE_SOURCE_DIR}/third-party/minizip/mz_zip.c"
        "${CMAKE_SOURCE_DIR}/third-party/minizip/mz_zip_rw.c"
)

target_include_directories(diffractor_minizip PUBLIC
        "${CMAKE_SOURCE_DIR}/third-party/minizip"
        "${CMAKE_SOURCE_DIR}/third-party"
        "${CMAKE_SOURCE_DIR}/third-party/ZLib"
        "${CMAKE_SOURCE_DIR}/third-party/bzip2"
        "${CMAKE_SOURCE_DIR}/third-party/liblzma/src/liblzma/api"
)

target_compile_definitions(diffractor_minizip PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:ZLIB_COMPAT>" "$<$<COMPILE_LANGUAGE:C,CXX>:LZMA_API_STATIC>" "$<$<COMPILE_LANGUAGE:C,CXX>:HAVE_BZIP2>" "$<$<COMPILE_LANGUAGE:C,CXX>:HAVE_ZLIB>" "$<$<COMPILE_LANGUAGE:C,CXX>:HAVE_LZMA>" "$<$<COMPILE_LANGUAGE:C,CXX>:_CRT_NONSTDC_NO_DEPRECATE>" "$<$<COMPILE_LANGUAGE:C,CXX>:WIN64>" "$<$<COMPILE_LANGUAGE:C,CXX>:UNICODE>" "$<$<COMPILE_LANGUAGE:C,CXX>:_UNICODE>")

diffractor_apply_vendored_policy(diffractor_minizip)

add_library(diffractor::minizip ALIAS diffractor_minizip)
