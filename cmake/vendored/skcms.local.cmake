# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: What the skcms .vcxproj could not describe. skcms compiles one translation unit per
# instruction set and expects each to be built for it. MSVC lets the intrinsics through on the
# baseline; GCC and Clang refuse to inline an AVX-512 intrinsic into a target that has not enabled
# it, so each file has to be told what it is for.

if (NOT MSVC)
    set_source_files_properties("${CMAKE_SOURCE_DIR}/third-party/skcms/src/skcms_TransformHsw.cc"
            PROPERTIES COMPILE_OPTIONS "-mavx2;-mf16c;-mfma")

    set_source_files_properties("${CMAKE_SOURCE_DIR}/third-party/skcms/src/skcms_TransformSkx.cc"
            PROPERTIES COMPILE_OPTIONS "-mavx512f;-mavx512dq;-mavx512cd;-mavx512bw;-mavx512vl")
endif ()
