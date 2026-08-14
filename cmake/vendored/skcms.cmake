# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: Build the vendored skcms as diffractor::skcms.
#
# Imported from third-party/skcms/skcms.vcxproj (Release|x64) by tools/import_vcxproj.py.
# Compiler and linker flags are not restated here: DiffractorCompilerPolicy.cmake owns those
# for every target.

include_guard(GLOBAL)

add_library(diffractor_skcms STATIC
        "${CMAKE_SOURCE_DIR}/third-party/skcms/skcms.cc"
        "${CMAKE_SOURCE_DIR}/third-party/skcms/src/skcms_TransformBaseline.cc"
        "${CMAKE_SOURCE_DIR}/third-party/skcms/src/skcms_TransformHsw.cc"
        "${CMAKE_SOURCE_DIR}/third-party/skcms/src/skcms_TransformSkx.cc"
)

target_include_directories(diffractor_skcms PUBLIC
        "${CMAKE_SOURCE_DIR}/third-party/skcms"
)

target_compile_definitions(diffractor_skcms PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:UNICODE>" "$<$<COMPILE_LANGUAGE:C,CXX>:_UNICODE>")

diffractor_apply_vendored_policy(diffractor_skcms)

add_library(diffractor::skcms ALIAS diffractor_skcms)
