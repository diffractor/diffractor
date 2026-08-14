# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: Build the vendored highway as diffractor::highway.
#
# Imported from third-party/highway/highway.vcxproj (Release|x64) by tools/import_vcxproj.py.
# Compiler and linker flags are not restated here: DiffractorCompilerPolicy.cmake owns those
# for every target.

include_guard(GLOBAL)

add_library(diffractor_highway STATIC
        "${CMAKE_SOURCE_DIR}/third-party/highway/hwy/aligned_allocator.cc"
        "${CMAKE_SOURCE_DIR}/third-party/highway/hwy/contrib/image/image.cc"
        "${CMAKE_SOURCE_DIR}/third-party/highway/hwy/contrib/sort/print_network.cc"
        "${CMAKE_SOURCE_DIR}/third-party/highway/hwy/contrib/sort/vqsort.cc"
        "${CMAKE_SOURCE_DIR}/third-party/highway/hwy/contrib/sort/vqsort_128a.cc"
        "${CMAKE_SOURCE_DIR}/third-party/highway/hwy/contrib/sort/vqsort_128d.cc"
        "${CMAKE_SOURCE_DIR}/third-party/highway/hwy/contrib/sort/vqsort_f32a.cc"
        "${CMAKE_SOURCE_DIR}/third-party/highway/hwy/contrib/sort/vqsort_f32d.cc"
        "${CMAKE_SOURCE_DIR}/third-party/highway/hwy/contrib/sort/vqsort_f64a.cc"
        "${CMAKE_SOURCE_DIR}/third-party/highway/hwy/contrib/sort/vqsort_f64d.cc"
        "${CMAKE_SOURCE_DIR}/third-party/highway/hwy/contrib/sort/vqsort_i16a.cc"
        "${CMAKE_SOURCE_DIR}/third-party/highway/hwy/contrib/sort/vqsort_i16d.cc"
        "${CMAKE_SOURCE_DIR}/third-party/highway/hwy/contrib/sort/vqsort_i32a.cc"
        "${CMAKE_SOURCE_DIR}/third-party/highway/hwy/contrib/sort/vqsort_i32d.cc"
        "${CMAKE_SOURCE_DIR}/third-party/highway/hwy/contrib/sort/vqsort_i64a.cc"
        "${CMAKE_SOURCE_DIR}/third-party/highway/hwy/contrib/sort/vqsort_i64d.cc"
        "${CMAKE_SOURCE_DIR}/third-party/highway/hwy/contrib/sort/vqsort_u16a.cc"
        "${CMAKE_SOURCE_DIR}/third-party/highway/hwy/contrib/sort/vqsort_u16d.cc"
        "${CMAKE_SOURCE_DIR}/third-party/highway/hwy/contrib/sort/vqsort_u32a.cc"
        "${CMAKE_SOURCE_DIR}/third-party/highway/hwy/contrib/sort/vqsort_u32d.cc"
        "${CMAKE_SOURCE_DIR}/third-party/highway/hwy/contrib/sort/vqsort_u64a.cc"
        "${CMAKE_SOURCE_DIR}/third-party/highway/hwy/contrib/sort/vqsort_u64d.cc"
        "${CMAKE_SOURCE_DIR}/third-party/highway/hwy/per_target.cc"
        "${CMAKE_SOURCE_DIR}/third-party/highway/hwy/perf_counters.cc"
        "${CMAKE_SOURCE_DIR}/third-party/highway/hwy/print.cc"
        "${CMAKE_SOURCE_DIR}/third-party/highway/hwy/profiler.cc"
        "${CMAKE_SOURCE_DIR}/third-party/highway/hwy/targets.cc"
        "${CMAKE_SOURCE_DIR}/third-party/highway/hwy/timer.cc"
        "${CMAKE_SOURCE_DIR}/third-party/highway/hwy/abort.cc"
)

target_include_directories(diffractor_highway PUBLIC
        "${CMAKE_SOURCE_DIR}/third-party/highway"
)

target_compile_definitions(diffractor_highway PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:_UNICODE>" "$<$<COMPILE_LANGUAGE:C,CXX>:UNICODE>")

if (WIN32)
    target_compile_definitions(diffractor_highway PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:UNICODE>" "$<$<COMPILE_LANGUAGE:C,CXX>:_UNICODE>")
endif ()

diffractor_apply_vendored_policy(diffractor_highway)

# Anything the Windows project could not describe - a header its build generates, a flag a
# different compiler needs. Hand written, and kept out of this file so that re-importing
# does not discard it.
include("${CMAKE_CURRENT_LIST_DIR}/highway.local.cmake" OPTIONAL)

add_library(diffractor::highway ALIAS diffractor_highway)
