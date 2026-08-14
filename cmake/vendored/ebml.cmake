# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: Build the vendored libebml as diffractor::ebml.
#
# Imported from third-party/libebml/libebml.vcxproj (Release|x64) by tools/import_vcxproj.py.
# Compiler and linker flags are not restated here: DiffractorCompilerPolicy.cmake owns those
# for every target.

include_guard(GLOBAL)

add_library(diffractor_ebml STATIC
        "${CMAKE_SOURCE_DIR}/third-party/libebml/src/Debug.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libebml/src/EbmlBinary.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libebml/src/EbmlContexts.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libebml/src/EbmlCrc32.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libebml/src/EbmlDate.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libebml/src/EbmlDummy.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libebml/src/EbmlElement.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libebml/src/EbmlFloat.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libebml/src/EbmlHead.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libebml/src/EbmlMaster.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libebml/src/EbmlSInteger.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libebml/src/EbmlStream.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libebml/src/EbmlString.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libebml/src/EbmlSubHead.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libebml/src/EbmlUInteger.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libebml/src/EbmlUnicodeString.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libebml/src/EbmlVersion.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libebml/src/EbmlVoid.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libebml/src/IOCallback.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libebml/src/MemIOCallback.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libebml/src/MemReadIOCallback.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libebml/src/platform/win32/WinIOCallback.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libebml/src/SafeReadIOCallback.cpp"
        "${CMAKE_SOURCE_DIR}/third-party/libebml/src/StdIOCallback.cpp"
)

target_include_directories(diffractor_ebml PUBLIC
        "${CMAKE_SOURCE_DIR}/third-party/libebml"
        "${CMAKE_SOURCE_DIR}/Include"
)

target_compile_definitions(diffractor_ebml PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:UNICODE>" "$<$<COMPILE_LANGUAGE:C,CXX>:_UNICODE>")

diffractor_apply_vendored_policy(diffractor_ebml)

add_library(diffractor::ebml ALIAS diffractor_ebml)
