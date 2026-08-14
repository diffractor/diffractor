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

# Vendored libjpeg-turbo and dav1d assemble their SIMD with nasm. enable_language() only takes
# effect at directory scope, and the vendored modules are included from inside a function, so it
# happens here. Windows carries its own copy in tools/, which is what the .vcxproj files used.
find_program(NASM_EXECUTABLE
        NAMES nasm
        HINTS "${CMAKE_SOURCE_DIR}/tools"
        DOC "The nasm assembler")

if (NASM_EXECUTABLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(CMAKE_ASM_NASM_COMPILER "${NASM_EXECUTABLE}")
    enable_language(ASM_NASM)

    if (WIN32)
        set(CMAKE_ASM_NASM_OBJECT_FORMAT win64)
    else ()
        set(CMAKE_ASM_NASM_OBJECT_FORMAT elf64)
    endif ()
else ()
    message(STATUS "nasm not found: the libraries that assemble their SIMD will not build")
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

# Every module below marked MSVC_ONLY was imported from a .vcxproj and carries that project's
# Windows configuration. Linux resolves what it can from system packages and stubs the rest, and
# each import earns its way onto another toolchain by being made to build there.
diffractor_dependency(zlib PKG_CONFIG zlib VENDORED_MODULE vendored/zlib MSVC_ONLY VENDORED_DIR third-party/ZLib OPTIONAL)
diffractor_dependency(png PKG_CONFIG libpng16 VENDORED_MODULE vendored/png MSVC_ONLY VENDORED_DIR third-party/libpng OPTIONAL)

# The system libjpeg-turbo is too old to be a substitute: files_jpeg.cpp uses the 12- and 16-bit
# APIs added in 3.0, and Ubuntu 24.04 ships 2.1.5. Declared before the call so the option below
# defaults to the vendored build; -DDIFFRACTOR_SYSTEM_JPEG=ON still forces the system copy.
option(DIFFRACTOR_SYSTEM_JPEG "Use the system jpeg rather than the vendored copy" OFF)
diffractor_dependency(jpeg PKG_CONFIG libjpeg VENDORED_MODULE vendored/LibJpeg OPTIONAL)

diffractor_dependency(sqlite PKG_CONFIG sqlite3 VENDORED_MODULE vendored/sqlite MSVC_ONLY VENDORED_DIR third-party/sqlite OPTIONAL)
diffractor_dependency(expat PKG_CONFIG expat VENDORED_MODULE vendored/expat MSVC_ONLY VENDORED_DIR third-party/expat OPTIONAL)
# webp splits the muxer and demuxer into their own pkg-config modules; files_webp.cpp uses all three.
diffractor_dependency(webp PKG_CONFIG libwebp libwebpmux libwebpdemux VENDORED_MODULE vendored/webp MSVC_ONLY VENDORED_DIR third-party/webp OPTIONAL)
diffractor_dependency(archive PKG_CONFIG libarchive VENDORED_MODULE vendored/archive MSVC_ONLY VENDORED_DIR third-party/libarchive OPTIONAL)
diffractor_dependency(exif PKG_CONFIG libexif VENDORED_MODULE vendored/exif MSVC_ONLY VENDORED_DIR third-party/libexif OPTIONAL)

# Compression and support libraries. Nothing in src/ includes these directly: they are here because
# the codecs above and below link them.
diffractor_dependency(bzip2 VENDORED_MODULE vendored/bzip2 MSVC_ONLY VENDORED_DIR third-party/bzip2 OPTIONAL)
diffractor_dependency(lzma VENDORED_MODULE vendored/lzma MSVC_ONLY VENDORED_DIR third-party/liblzma OPTIONAL)
diffractor_dependency(brotli VENDORED_MODULE vendored/brotli MSVC_ONLY VENDORED_DIR third-party/brotli OPTIONAL)
diffractor_dependency(highway VENDORED_MODULE vendored/highway MSVC_ONLY VENDORED_DIR third-party/highway OPTIONAL)
# Forks. Never resolvable from the system: docs/third-party.md records the patches a distribution
# copy would silently discard. Declared before the codecs because dng compiles against the XMP
# toolkit, and a dependency can only be stated on a target that already exists.
diffractor_dependency(xmp FORK VENDORED_MODULE vendored/Xmp VENDORED_DIR third-party/xmp OPTIONAL)
diffractor_dependency(ffmpeg FORK VENDORED_MODULE vendored/FFmpeg VENDORED_DIR third-party/FFmpeg OPTIONAL)

diffractor_dependency(skcms VENDORED_MODULE vendored/skcms MSVC_ONLY VENDORED_DIR third-party/skcms OPTIONAL)
diffractor_dependency(de265 VENDORED_MODULE vendored/de265 MSVC_ONLY VENDORED_DIR third-party/libde265 OPTIONAL)
diffractor_dependency(dav1d VENDORED_MODULE vendored/dav1d MSVC_ONLY VENDORED_DIR third-party/dav1d OPTIONAL)
diffractor_dependency(dng VENDORED_MODULE vendored/dng MSVC_ONLY VENDORED_DIR third-party/dng OPTIONAL)
diffractor_dependency(ebml VENDORED_MODULE vendored/ebml MSVC_ONLY VENDORED_DIR third-party/libebml OPTIONAL)
diffractor_dependency(matroska VENDORED_MODULE vendored/matroska MSVC_ONLY VENDORED_DIR third-party/libmatroska OPTIONAL)
diffractor_dependency(openmpt VENDORED_MODULE vendored/openmpt MSVC_ONLY VENDORED_DIR third-party/libopenmpt OPTIONAL)

# Codecs and utilities that gate a translation unit of their own.
diffractor_dependency(heif VENDORED_MODULE vendored/heif MSVC_ONLY VENDORED_DIR third-party/libheif OPTIONAL)
diffractor_dependency(jxl VENDORED_MODULE vendored/jxl MSVC_ONLY VENDORED_DIR third-party/libjx OPTIONAL)
diffractor_dependency(raw VENDORED_MODULE vendored/raw MSVC_ONLY VENDORED_DIR third-party/LibRaw OPTIONAL)
diffractor_dependency(hunspell VENDORED_MODULE vendored/hunspell MSVC_ONLY VENDORED_DIR third-party/hunspell OPTIONAL)
diffractor_dependency(minizip VENDORED_MODULE vendored/minizip MSVC_ONLY VENDORED_DIR third-party/minizip OPTIONAL)

diffractor_report_dependencies()

# files_core.cpp includes libarchive by the shape of the vendored tree
# ("libarchive/libarchive/archive.h"), which the system package does not provide. Forwarding
# headers cost nothing and avoid changing the source. Not needed where the vendored tree itself is
# on the include path, which is what Windows does.
if (TARGET diffractor::archive AND NOT WIN32)
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
#
# The rest are the vendored roots app.vcxproj listed. Several headers are reached by their position
# in the tree rather than through a library target, libarchive/libarchive/archive.h among them.
if (WIN32)
    target_include_directories(diffractor PRIVATE
            Include
            third-party
            third-party/webp/src
            third-party/LibJpeg/src
            third-party/libheif/api
            third-party/hunspell
            third-party/libpng
            third-party/sqlite
            third-party/dng
            third-party/libexif
            third-party/bzip2
            third-party/zlib
            third-party/xmp/public/include
            third-party/libraw
            third-party/FFmpeg
    )
endif ()

# The resource script embeds the Direct3D shaders as binary resources, so they have to be compiled
# before it is. app.vcxproj expressed this as FxCompile items writing .cso into $(IntDir), which the
# resource compiler then had on its include path.
if (WIN32)
    # The SDK installs one fxc per version, and none of them is on the path.
    file(GLOB _fxc_candidates
            "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin/*/x64/fxc.exe"
            "$ENV{ProgramFiles}/Windows Kits/10/bin/*/x64/fxc.exe")

    set(_fxc_dirs "")
    foreach (_candidate IN LISTS _fxc_candidates)
        get_filename_component(_dir "${_candidate}" DIRECTORY)
        list(APPEND _fxc_dirs "${_dir}")
    endforeach ()

    list(SORT _fxc_dirs)
    list(REVERSE _fxc_dirs)

    find_program(FXC_EXECUTABLE NAMES fxc HINTS ${_fxc_dirs} DOC "The Direct3D shader compiler")

    if (NOT FXC_EXECUTABLE)
        message(FATAL_ERROR "fxc was not found; the shaders the resource script embeds cannot be built")
    endif ()

    set(_shader_dir "${CMAKE_BINARY_DIR}/shaders")
    file(MAKE_DIRECTORY "${_shader_dir}")

    # ShaderModel 4.0 throughout; vs.hlsl is the only vertex shader.
    set(_pixel_shaders circle_ps font_ps rgb_bicubic_ps rgb_ps solid_ps yuv_bicubic_ps yuv_ps)
    set(_vertex_shaders vs)
    set(_shader_outputs "")

    foreach (_shader IN LISTS _pixel_shaders _vertex_shaders)
        if (_shader IN_LIST _vertex_shaders)
            set(_profile vs_4_0)
        else ()
            set(_profile ps_4_0)
        endif ()

        set(_out "${_shader_dir}/${_shader}.cso")

        add_custom_command(
                OUTPUT "${_out}"
                COMMAND "${FXC_EXECUTABLE}" /nologo /T ${_profile} /E main
                /Fo "${_out}" "${CMAKE_SOURCE_DIR}/src/shaders/${_shader}.hlsl"
                MAIN_DEPENDENCY "${CMAKE_SOURCE_DIR}/src/shaders/${_shader}.hlsl"
                DEPENDS "${CMAKE_SOURCE_DIR}/src/shaders/common.hlsli"
                COMMENT "Compiling ${_shader}.hlsl (${_profile})"
                VERBATIM)

        list(APPEND _shader_outputs "${_out}")
    endforeach ()

    add_custom_target(diffractor_shaders DEPENDS ${_shader_outputs})
    add_dependencies(diffractor diffractor_shaders)

    # The script names them without a path, so the directory holding them is what makes them found.
    target_include_directories(diffractor PRIVATE "${_shader_dir}")
    set_source_files_properties("${CMAKE_SOURCE_DIR}/src/platform_win_res.rc"
            PROPERTIES OBJECT_DEPENDS "${_shader_outputs}")
endif ()

# Every resolved dependency is named here rather than relying on one library to pull in another.
# These are static archives, and the support libraries have no translation unit of their own that
# would otherwise put them on the line.
foreach (dep IN ITEMS
        zlib png jpeg sqlite expat webp archive exif ffmpeg xmp
        heif jxl raw hunspell minizip
        de265 dav1d dng ebml matroska openmpt
        bzip2 lzma brotli highway skcms)
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
