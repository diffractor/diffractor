# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: Build the vendored libexif as diffractor::exif.
#
# Imported from third-party/libexif/libexif.vcxproj (Release|x64) by tools/import_vcxproj.py.
# Compiler and linker flags are not restated here: DiffractorCompilerPolicy.cmake owns those
# for every target.

include_guard(GLOBAL)

add_library(diffractor_exif STATIC
        "${CMAKE_SOURCE_DIR}/third-party/libexif/libexif/apple/exif-mnote-data-apple.c"
        "${CMAKE_SOURCE_DIR}/third-party/libexif/libexif/apple/mnote-apple-entry.c"
        "${CMAKE_SOURCE_DIR}/third-party/libexif/libexif/apple/mnote-apple-tag.c"
        "${CMAKE_SOURCE_DIR}/third-party/libexif/libexif/canon/exif-mnote-data-canon.c"
        "${CMAKE_SOURCE_DIR}/third-party/libexif/libexif/canon/mnote-canon-entry.c"
        "${CMAKE_SOURCE_DIR}/third-party/libexif/libexif/canon/mnote-canon-tag.c"
        "${CMAKE_SOURCE_DIR}/third-party/libexif/libexif/exif-byte-order.c"
        "${CMAKE_SOURCE_DIR}/third-party/libexif/libexif/exif-content.c"
        "${CMAKE_SOURCE_DIR}/third-party/libexif/libexif/exif-data.c"
        "${CMAKE_SOURCE_DIR}/third-party/libexif/libexif/exif-entry.c"
        "${CMAKE_SOURCE_DIR}/third-party/libexif/libexif/exif-format.c"
        "${CMAKE_SOURCE_DIR}/third-party/libexif/libexif/exif-gps-ifd.c"
        "${CMAKE_SOURCE_DIR}/third-party/libexif/libexif/exif-ifd.c"
        "${CMAKE_SOURCE_DIR}/third-party/libexif/libexif/exif-loader.c"
        "${CMAKE_SOURCE_DIR}/third-party/libexif/libexif/exif-log.c"
        "${CMAKE_SOURCE_DIR}/third-party/libexif/libexif/exif-mem.c"
        "${CMAKE_SOURCE_DIR}/third-party/libexif/libexif/exif-mnote-data.c"
        "${CMAKE_SOURCE_DIR}/third-party/libexif/libexif/exif-tag.c"
        "${CMAKE_SOURCE_DIR}/third-party/libexif/libexif/exif-utils.c"
        "${CMAKE_SOURCE_DIR}/third-party/libexif/libexif/fuji/exif-mnote-data-fuji.c"
        "${CMAKE_SOURCE_DIR}/third-party/libexif/libexif/fuji/mnote-fuji-entry.c"
        "${CMAKE_SOURCE_DIR}/third-party/libexif/libexif/fuji/mnote-fuji-tag.c"
        "${CMAKE_SOURCE_DIR}/third-party/libexif/libexif/olympus/exif-mnote-data-olympus.c"
        "${CMAKE_SOURCE_DIR}/third-party/libexif/libexif/olympus/mnote-olympus-entry.c"
        "${CMAKE_SOURCE_DIR}/third-party/libexif/libexif/olympus/mnote-olympus-tag.c"
        "${CMAKE_SOURCE_DIR}/third-party/libexif/libexif/pentax/exif-mnote-data-pentax.c"
        "${CMAKE_SOURCE_DIR}/third-party/libexif/libexif/pentax/mnote-pentax-entry.c"
        "${CMAKE_SOURCE_DIR}/third-party/libexif/libexif/pentax/mnote-pentax-tag.c"
)

target_include_directories(diffractor_exif PUBLIC
        "${CMAKE_SOURCE_DIR}/third-party/libexif"
)

if (WIN32)
    target_compile_definitions(diffractor_exif PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:UNICODE>" "$<$<COMPILE_LANGUAGE:C,CXX>:_UNICODE>")
endif ()

diffractor_apply_vendored_policy(diffractor_exif)

# Anything the Windows project could not describe - a header its build generates, a flag a
# different compiler needs. Hand written, and kept out of this file so that re-importing
# does not discard it.
include("${CMAKE_CURRENT_LIST_DIR}/exif.local.cmake" OPTIONAL)

add_library(diffractor::exif ALIAS diffractor_exif)
