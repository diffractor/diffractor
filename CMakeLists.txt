# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: The build of Diffractor, for every platform. This is becoming the single source of
# truth: df.sln and the .vcxproj files are being replaced by it, and the Visual Studio projects
# that remain useful for debugging and profiling are generated from here on demand.
#
# The source list is deliberately explicit rather than a glob, so that what is built is reviewable
# and a file cannot join the build by being dropped in a folder.
#
# Drive this through the gateway rather than by hand:
#
#   dd setup       install the build tooling and system packages
#   dd build       configure and build
#   dd run         run what has been built

cmake_minimum_required(VERSION 3.22)

# Selecting the MSVC runtime by property rather than by editing flag strings needs this policy.
cmake_policy(SET CMP0091 NEW)

project(diffractor CXX C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug)
endif ()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(DiffractorCompilerPolicy)
include(DiffractorDependency)

# Vendored libjpeg-turbo assembles its SIMD with nasm. enable_language() only takes effect at
# directory scope, and the vendored modules are included from inside a function, so it happens here.
if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    enable_language(ASM_NASM)

    if (WIN32)
        set(CMAKE_ASM_NASM_OBJECT_FORMAT win64)
    else ()
        set(CMAKE_ASM_NASM_OBJECT_FORMAT elf64)
    endif ()
endif ()

find_program(CCACHE_PROGRAM ccache)
if (CCACHE_PROGRAM AND NOT MSVC)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif ()

# ---------------------------------------------------------------------------------------------
# Dependencies
#
# System packages are a bring-up convenience, not the shipped configuration: turn each off with
# -DDIFFRACTOR_SYSTEM_<NAME>=OFF once the vendored copy has a CMakeLists.txt. Everything here is
# OPTIONAL while the translation units that need it are still out of the build, so a missing
# package is reported rather than fatal.
# ---------------------------------------------------------------------------------------------

diffractor_dependency(zlib PKG_CONFIG zlib VENDORED_DIR third-party/ZLib OPTIONAL)
diffractor_dependency(png PKG_CONFIG libpng16 VENDORED_DIR third-party/libpng OPTIONAL)

# The system libjpeg-turbo is too old to be a substitute: files_jpeg.cpp uses the 12- and 16-bit
# APIs added in 3.0, and Ubuntu 24.04 ships 2.1.5. Declared before the call so the option below
# defaults to the vendored build; -DDIFFRACTOR_SYSTEM_JPEG=ON still forces the system copy.
option(DIFFRACTOR_SYSTEM_JPEG "Use the system jpeg rather than the vendored copy" OFF)
diffractor_dependency(jpeg PKG_CONFIG libjpeg VENDORED_MODULE vendored/LibJpeg OPTIONAL)

diffractor_dependency(sqlite PKG_CONFIG sqlite3 VENDORED_DIR third-party/sqlite OPTIONAL)
diffractor_dependency(expat PKG_CONFIG expat VENDORED_DIR third-party/expat OPTIONAL)
# webp splits the muxer and demuxer into their own pkg-config modules; files_webp.cpp uses all three.
diffractor_dependency(webp PKG_CONFIG libwebp libwebpmux libwebpdemux VENDORED_DIR third-party/webp OPTIONAL)
diffractor_dependency(archive PKG_CONFIG libarchive VENDORED_DIR third-party/libarchive OPTIONAL)
diffractor_dependency(exif PKG_CONFIG libexif VENDORED_DIR third-party/libexif OPTIONAL)

# Forks. Never resolvable from the system: docs/third-party.md records the patches a distribution
# copy would silently discard.
diffractor_dependency(xmp FORK VENDORED_MODULE vendored/Xmp VENDORED_DIR third-party/xmp OPTIONAL)
diffractor_dependency(ffmpeg FORK VENDORED_MODULE vendored/FFmpeg VENDORED_DIR third-party/FFmpeg OPTIONAL)

diffractor_report_dependencies()

# files_core.cpp includes libarchive by the shape of the vendored tree
# ("libarchive/libarchive/archive.h"), which the system package does not provide. Forwarding
# headers cost nothing and avoid changing the source, which would also mean changing the include
# path in app.vcxproj on Windows.
if (TARGET diffractor::archive)
    set(DIFFRACTOR_COMPAT_INCLUDE "${CMAKE_BINARY_DIR}/compat")
    file(WRITE "${DIFFRACTOR_COMPAT_INCLUDE}/libarchive/libarchive/archive.h" "#include <archive.h>\n")
    file(WRITE "${DIFFRACTOR_COMPAT_INCLUDE}/libarchive/libarchive/archive_entry.h" "#include <archive_entry.h>\n")
endif ()

# ---------------------------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------------------------

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
        src/app_settings.cpp
        src/app_util.cpp
        src/model_property.cpp
        src/model_tags.cpp
        src/model_visits.cpp
        src/model_locations.cpp
        src/metadata_icc.cpp
        src/metadata_iptc.cpp
        src/files_commodore.cpp
        src/files_core.cpp
        src/files_psd.cpp
        src/files_scan_photo.cpp
        src/files_jpeg.cpp
        src/files_png.cpp
        src/files_webp.cpp
        src/metadata_exif.cpp
        src/model.cpp
        src/model_db.cpp
        src/model_index.cpp
        src/model_items.cpp
        src/model_search.cpp
        src/model_tile_cache.cpp
        src/render_color.cpp
        src/render_image.cpp
        src/render_surface.cpp
        src/ui.cpp
        src/ui_controls.cpp
        src/ui_flex.cpp
        src/app_commands.cpp
        src/app.cpp
        src/app_dup_report.cpp
        src/app_search.cpp
        src/app_toolbar.cpp
        src/app_workers.cpp
        src/view_batch.cpp
        src/view_edit.cpp
        src/view_import.cpp
        src/view_items.cpp
        src/view_locate.cpp
        src/view_rename.cpp
        src/view_selector.cpp
        src/view_sync.cpp
        src/view_tags.cpp
)

# av_format.cpp and platform_linux_av_stubs.cpp define the same symbols and are alternatives: one
# drives the FFmpeg fork, the other reports every media operation as failed. metadata_xmp.cpp and
# platform_linux_xmp_stubs.cpp stand in the same relation over the Adobe toolkit fork.
if (TARGET diffractor::ffmpeg)
    list(APPEND DIFFRACTOR_PORTABLE_SOURCES src/av_format.cpp)
else ()
    list(APPEND DIFFRACTOR_PORTABLE_SOURCES src/platform_linux_av_stubs.cpp)
endif ()

if (TARGET diffractor::xmp)
    list(APPEND DIFFRACTOR_PORTABLE_SOURCES src/metadata_xmp.cpp)
else ()
    list(APPEND DIFFRACTOR_PORTABLE_SOURCES src/platform_linux_xmp_stubs.cpp)
endif ()

# Tools rather than tests: neither has a library dependency, so neither is conditional.
list(APPEND DIFFRACTOR_PORTABLE_SOURCES
        src/app_gen_docs.cpp
        src/app_validate_po.cpp
)

# Codec and utility translation units, each gated on the library it needs. Where the library is
# absent, platform_linux_stubs.cpp supplies the same symbols -- so when one of these libraries is
# first resolved on Linux, the matching block must come out of that file or the two will collide.
if (TARGET diffractor::heif)
    list(APPEND DIFFRACTOR_PORTABLE_SOURCES src/files_heif.cpp)
endif ()

if (TARGET diffractor::jxl)
    list(APPEND DIFFRACTOR_PORTABLE_SOURCES src/files_jxl.cpp)
endif ()

if (TARGET diffractor::raw)
    list(APPEND DIFFRACTOR_PORTABLE_SOURCES src/files_raw.cpp)
endif ()

if (TARGET diffractor::hunspell)
    list(APPEND DIFFRACTOR_PORTABLE_SOURCES src/util_spell.cpp)
endif ()

if (TARGET diffractor::minizip)
    list(APPEND DIFFRACTOR_PORTABLE_SOURCES src/util_zip.cpp)
endif ()

# The suite itself. run_console_tests builds a view_state and registers every subject file, so this
# is all or nothing.
set(DIFFRACTOR_TEST_SOURCES
        src/test_app.cpp
        src/test_av.cpp
        src/test_files.cpp
        src/test_fixtures.cpp
        src/test_index.cpp
        src/test_locations.cpp
        src/test_media_edit.cpp
        src/test_metadata.cpp
        src/test_render.cpp
        src/test_runner.cpp
        src/test_search.cpp
        src/test_text.cpp
        src/test_util.cpp
        src/test_view.cpp
)

if (WIN32)
    list(APPEND DIFFRACTOR_TEST_SOURCES src/test_platform_win.cpp)

    set(DIFFRACTOR_PLATFORM_SOURCES
            src/platform_win.cpp
            src/platform_win_d3d11.cpp
            src/platform_win_files.cpp
            src/platform_win_font.cpp
            src/platform_win_settings.cpp
            src/platform_win_software.cpp
            src/platform_win_sound.cpp
            src/platform_win_ui.cpp
            src/platform_win_web.cpp
            src/platform_win_wic.cpp
            src/platform_win_res.rc
    )
else ()
    set(DIFFRACTOR_PLATFORM_SOURCES
            src/platform_linux.cpp
            src/platform_linux_desktop.cpp
            src/platform_linux_files.cpp
            src/platform_linux_settings.cpp
            src/platform_linux_stubs.cpp
            src/platform_linux_ui.cpp
    )
endif ()

add_executable(diffractor
        ${DIFFRACTOR_PORTABLE_SOURCES}
        ${DIFFRACTOR_TEST_SOURCES}
        ${DIFFRACTOR_PLATFORM_SOURCES}
)

target_include_directories(diffractor PRIVATE
        src
        third-party/rapidjson/include
        ${DIFFRACTOR_COMPAT_INCLUDE}
)

# Include/ is an MSVC shim supplying POSIX headers Windows lacks. It belongs on the include path
# there and nowhere else: on Linux its unistd.h shadows the real one and pulls in io.h.
if (WIN32)
    target_include_directories(diffractor PRIVATE Include)
endif ()

foreach (dep IN ITEMS zlib png jpeg sqlite expat webp archive exif ffmpeg xmp heif jxl raw hunspell minizip)
    if (TARGET diffractor::${dep})
        target_link_libraries(diffractor PRIVATE diffractor::${dep})
    endif ()
endforeach ()

diffractor_apply_app_policy(diffractor)

find_package(Threads REQUIRED)
target_link_libraries(diffractor PRIVATE Threads::Threads)

# A dependency built as an external project is not on the target's link line as a CMake target, so
# the ordering has to be stated.
get_property(DIFFRACTOR_EXTERNAL_TARGETS GLOBAL PROPERTY DIFFRACTOR_EXTERNAL_TARGETS)
if (DIFFRACTOR_EXTERNAL_TARGETS)
    add_dependencies(diffractor ${DIFFRACTOR_EXTERNAL_TARGETS})
endif ()
