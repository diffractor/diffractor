# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: Linux build of Diffractor. This is stage one of the port described in docs/linux.md:
# it builds the portable core under GCC so that the Windows assumptions still sitting in that
# code fail loudly on a second toolchain. It is not yet a build of the application -- the source
# list below grows as each translation unit is made to compile, and is deliberately explicit
# rather than a glob so that "what already ports" stays readable.
#
# The Windows build remains driven by df.sln and the .vcxproj files; this does not replace them.
#
#   cmake -S . -B tmp/linux-build -DCMAKE_BUILD_TYPE=Debug
#   cmake --build tmp/linux-build -j

cmake_minimum_required(VERSION 3.22)
project(diffractor CXX)

if (WIN32)
    message(FATAL_ERROR "The Windows build is df.sln; this CMake build targets Linux only.")
endif ()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif ()

# Translation units proved to compile under GCC. Add to this list as each one is ported; the
# roadmap for what is still missing is in docs/linux.md.
set(DIFFRACTOR_PORTABLE_SOURCES
        src/util.cpp
        src/util_base64.cpp
        src/util_geometry.cpp
        src/util_strings.cpp
        src/crypto.cpp
        src/crypto_aes256.cpp
        src/crypto_md5.cpp
        src/crypto_sha.cpp
        src/app_text.cpp
        src/model_property.cpp
)

add_executable(diffractor
        ${DIFFRACTOR_PORTABLE_SOURCES}
        src/platform_linux.cpp
)

target_include_directories(diffractor PRIVATE
        src
        third-party/rapidjson/include
        third-party/FFmpeg
)

# Include/ is an MSVC shim supplying POSIX headers Windows lacks. On Linux its unistd.h shadows
# the real one and pulls in io.h, so it must never reach the include path.

# No target_precompile_headers: every translation unit includes pch.h itself, and CMake's forced
# include reaches the same file by a different path, which defeats #pragma once on this filesystem
# and reports every declaration in it as a redefinition.

target_compile_options(diffractor PRIVATE
        -Wall
        -Wno-unknown-pragmas
        -Wno-multichar
        # GCC 13 makes this an error by default. Accessors such as ui::surface::orientation() share
        # a name with a type in the enclosing namespace, which is ill-formed by the letter of the
        # standard and accepted by every compiler; renaming them buys nothing.
        -Wno-changes-meaning
)

find_package(Threads REQUIRED)
target_link_libraries(diffractor PRIVATE Threads::Threads)
