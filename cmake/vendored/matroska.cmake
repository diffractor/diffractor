# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: Build the vendored libmatroska as diffractor::matroska.
#
# Imported from third-party/libmatroska/libmatroska.vcxproj (Release|x64) by tools/import_vcxproj.py.
# Compiler and linker flags are not restated here: DiffractorCompilerPolicy.cmake owns those
# for every target.

include_guard(GLOBAL)

add_library(diffractor_matroska STATIC
        "${CMAKE_SOURCE_DIR}/third-party/libmatroska/src/FileKax.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libmatroska/src/KaxAttached.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libmatroska/src/KaxAttachments.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libmatroska/src/KaxBlock.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libmatroska/src/KaxBlockData.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libmatroska/src/KaxCluster.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libmatroska/src/KaxContexts.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libmatroska/src/KaxCues.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libmatroska/src/KaxCuesData.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libmatroska/src/KaxInfoData.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libmatroska/src/KaxSeekHead.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libmatroska/src/KaxSegment.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libmatroska/src/KaxSemantic.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libmatroska/src/KaxTracks.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libmatroska/src/KaxVersion.cpp"
)

target_include_directories(diffractor_matroska PUBLIC
        "${CMAKE_SOURCE_DIR}/third-party/libmatroska"
        "${CMAKE_SOURCE_DIR}/third-party/libebml"
)

if (WIN32)
    target_compile_definitions(diffractor_matroska PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:UNICODE>" "$<$<COMPILE_LANGUAGE:C,CXX>:_UNICODE>")
endif ()

diffractor_apply_vendored_policy(diffractor_matroska)

# Anything the Windows project could not describe - a header its build generates, a flag a
# different compiler needs. Hand written, and kept out of this file so that re-importing
# does not discard it.
include("${CMAKE_CURRENT_LIST_DIR}/matroska.local.cmake" OPTIONAL)

add_library(diffractor::matroska ALIAS diffractor_matroska)
