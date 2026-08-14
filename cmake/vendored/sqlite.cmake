# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: Build the vendored sqlite as diffractor::sqlite.
#
# Imported from third-party/sqlite/sqlite.vcxproj (Release|x64) by tools/import_vcxproj.py.
# Compiler and linker flags are not restated here: DiffractorCompilerPolicy.cmake owns those
# for every target.

include_guard(GLOBAL)

add_library(diffractor_sqlite STATIC
        "${CMAKE_SOURCE_DIR}/third-party/sqlite/sqlite3.c"
)

target_include_directories(diffractor_sqlite PUBLIC
        "${CMAKE_SOURCE_DIR}/third-party/sqlite"
)

target_compile_definitions(diffractor_sqlite PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:SQLITE_DEFAULT_CACHE_SIZE=500>" "$<$<COMPILE_LANGUAGE:C,CXX>:SQLITE_DEFAULT_PAGE_SIZE=(1024*16)>" "$<$<COMPILE_LANGUAGE:C,CXX>:SQLITE_WIN32_MALLOC>" "$<$<COMPILE_LANGUAGE:C,CXX>:SQLITE_WIN32_HEAP_CREATE=0>" "$<$<COMPILE_LANGUAGE:C,CXX>:SQLITE_DQS=0>" "$<$<COMPILE_LANGUAGE:C,CXX>:SQLITE_THREADSAFE=2>" "$<$<COMPILE_LANGUAGE:C,CXX>:SQLITE_DEFAULT_MEMSTATUS=0>" "$<$<COMPILE_LANGUAGE:C,CXX>:SQLITE_DEFAULT_WAL_SYNCHRONOUS=1>" "$<$<COMPILE_LANGUAGE:C,CXX>:SQLITE_LIKE_DOESNT_MATCH_BLOBS>" "$<$<COMPILE_LANGUAGE:C,CXX>:SQLITE_MAX_EXPR_DEPTH=0>" "$<$<COMPILE_LANGUAGE:C,CXX>:SQLITE_OMIT_DECLTYPE>" "$<$<COMPILE_LANGUAGE:C,CXX>:SQLITE_OMIT_DEPRECATED>" "$<$<COMPILE_LANGUAGE:C,CXX>:SQLITE_OMIT_PROGRESS_CALLBACK>" "$<$<COMPILE_LANGUAGE:C,CXX>:SQLITE_OMIT_SHARED_CACHE>" "$<$<COMPILE_LANGUAGE:C,CXX>:SQLITE_USE_ALLOCA>" "$<$<COMPILE_LANGUAGE:C,CXX>:SQLITE_OMIT_AUTOINIT>" "$<$<COMPILE_LANGUAGE:C,CXX>:SQLITE_OMIT_LOAD_EXTENSION>" "$<$<COMPILE_LANGUAGE:C,CXX>:UNICODE>" "$<$<COMPILE_LANGUAGE:C,CXX>:_UNICODE>")

diffractor_apply_vendored_policy(diffractor_sqlite)

add_library(diffractor::sqlite ALIAS diffractor_sqlite)
