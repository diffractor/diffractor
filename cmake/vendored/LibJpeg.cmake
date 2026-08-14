# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: Build the vendored libjpeg-turbo. The system package is not a substitute -- Ubuntu
# 24.04 ships 2.1.5, which predates the 12- and 16-bit APIs files_jpeg.cpp uses.
#
# Upstream's own CMakeLists cannot drive this: the vendored copy is a source subset with
# cmakescripts/, md5/ and the test inputs removed, so it fails to configure. The source lists
# below are lifted from the three .vcxproj files, which are the record of what Diffractor
# actually compiles.
#
# third-party/ is only ever read. The sources are staged into the build tree because
# src/jconfig.h is hand-written for MSVC and is reached by a quoted include from jpeglib.h, so it
# cannot be overridden with -I.

include_guard(GLOBAL)

set(_jpeg_upstream "${CMAKE_SOURCE_DIR}/third-party/LibJpeg")
set(_jpeg_stage "${CMAKE_BINARY_DIR}/vendored/LibJpeg")

file(COPY "${_jpeg_upstream}/src" "${_jpeg_upstream}/simd"
        DESTINATION "${_jpeg_stage}"
        PATTERN "jconfig.h" EXCLUDE
        PATTERN "jconfigint.h" EXCLUDE
        PATTERN "intermediate" EXCLUDE
)

file(COPY "${CMAKE_SOURCE_DIR}/cmake/vendored/libjpeg-linux/jconfig.h"
        "${CMAKE_SOURCE_DIR}/cmake/vendored/libjpeg-linux/jconfigint.h"
        DESTINATION "${_jpeg_stage}/src")

set(_jpeg_src "${_jpeg_stage}/src")

# 8-bit library, from LibJpeg.vcxproj minus the NEON, TurboJPEG and test sources.
set(_jpeg_sources_8
        jaricom.c jcapimin.c jcapistd.c jcarith.c jccoefct.c jccolor.c jcdctmgr.c jcdiffct.c
        jchuff.c jcinit.c jclhuff.c jclossls.c jcmainct.c jcmarker.c jcmaster.c jcomapi.c
        jcparam.c jcphuff.c jcprepct.c jcsample.c jctrans.c jdapimin.c jdapistd.c jdarith.c
        jdatadst.c jdatasrc.c jdcoefct.c jdcolor.c jddctmgr.c jddiffct.c jdhuff.c jdicc.c
        jdinput.c jdlhuff.c jdlossls.c jdmainct.c jdmarker.c jdmaster.c jdmerge.c jdphuff.c
        jdpostct.c jdsample.c jdtrans.c jerror.c jfdctflt.c jfdctfst.c jfdctint.c jidctflt.c
        jidctfst.c jidctint.c jidctred.c jmemmgr.c jmemnobs.c jquant1.c jquant2.c jutils.c
        transupp.c)

# Compiled again at higher precision, from jpeg12.vcxproj and jpeg16.vcxproj.
set(_jpeg_sources_12
        jcapistd.c jccoefct.c jccolor.c jcdctmgr.c jcdiffct.c jclossls.c jcmainct.c jcprepct.c
        jcsample.c jdapistd.c jdcoefct.c jdcolor.c jddctmgr.c jddiffct.c jdlossls.c jdmainct.c
        jdmerge.c jdpostct.c jdsample.c jfdctfst.c jfdctint.c jidctflt.c jidctfst.c jidctint.c
        jidctred.c jquant1.c jquant2.c jutils.c)

set(_jpeg_sources_16
        jcapistd.c jccolor.c jcdiffct.c jclossls.c jcmainct.c jcprepct.c jcsample.c jdapistd.c
        jdcolor.c jddiffct.c jdlossls.c jdmainct.c jdpostct.c jdsample.c jutils.c)

list(TRANSFORM _jpeg_sources_8 PREPEND "${_jpeg_src}/")
list(TRANSFORM _jpeg_sources_12 PREPEND "${_jpeg_src}/")
list(TRANSFORM _jpeg_sources_16 PREPEND "${_jpeg_src}/")

add_library(diffractor_jpeg_12 OBJECT ${_jpeg_sources_12})
target_compile_definitions(diffractor_jpeg_12 PRIVATE BITS_IN_JSAMPLE=12)
target_include_directories(diffractor_jpeg_12 PRIVATE "${_jpeg_src}")

add_library(diffractor_jpeg_16 OBJECT ${_jpeg_sources_16})
target_compile_definitions(diffractor_jpeg_16 PRIVATE BITS_IN_JSAMPLE=16)
target_include_directories(diffractor_jpeg_16 PRIVATE "${_jpeg_src}")

# SIMD. simd/nasm/jsimdcfg.inc is checked in already generated, so nasm needs no preprocessing
# step. Only x86-64 is wired up; anything else falls back to the C paths.
set(_jpeg_simd_sources "")

if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    file(GLOB _jpeg_asm "${_jpeg_stage}/simd/x86_64/*.asm")

    # The *ext-*.asm files are %include fragments assembled once per pixel format by their parent
    # (jccolor-avx2.asm and friends), which define RGB_RED and the rest first. Assembling them
    # directly fails on undefined symbols.
    list(FILTER _jpeg_asm EXCLUDE REGEX "ext-(avx2|sse2)\\.asm$")
    set(_jpeg_simd_sources ${_jpeg_asm} "${_jpeg_stage}/simd/jsimd.c")

    # -DPIC selects the i386 GOT paths in jsimdext.inc; x86-64 is position independent anyway.
    set_source_files_properties(${_jpeg_asm} PROPERTIES
            COMPILE_FLAGS
            "-D__x86_64__ -DELF -I${_jpeg_stage}/simd/nasm/ -I${_jpeg_stage}/simd/x86_64/")
endif ()

add_library(diffractor_jpeg STATIC
        ${_jpeg_sources_8}
        ${_jpeg_simd_sources}
        $<TARGET_OBJECTS:diffractor_jpeg_12>
        $<TARGET_OBJECTS:diffractor_jpeg_16>
)

target_include_directories(diffractor_jpeg
        PUBLIC "${_jpeg_src}"
        PRIVATE "${_jpeg_stage}/simd" "${_jpeg_stage}/simd/x86_64")

set_target_properties(diffractor_jpeg diffractor_jpeg_12 diffractor_jpeg_16
        PROPERTIES POSITION_INDEPENDENT_CODE ON C_STANDARD 11)

# Upstream is not warning-clean under -Wall and it is not ours to fix. This must not reach nasm:
# there -w expects an argument and would swallow the next define.
target_compile_options(diffractor_jpeg PRIVATE $<$<COMPILE_LANGUAGE:C>:-w>)
target_compile_options(diffractor_jpeg_12 PRIVATE $<$<COMPILE_LANGUAGE:C>:-w>)
target_compile_options(diffractor_jpeg_16 PRIVATE $<$<COMPILE_LANGUAGE:C>:-w>)

add_library(diffractor::jpeg ALIAS diffractor_jpeg)
