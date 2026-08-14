# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: What the libde265 .vcxproj could not describe.
#
# Everything in extra/ stands in for something Win32 lacked - condition variables, and getopt - and
# each is native here. The x86 sources use SSSE3 and SSE4.1 intrinsics: MSVC lets those through on
# the baseline, GCC and Clang refuse to inline one into a target that has not enabled it.

if (NOT WIN32)
    foreach (windows_only extra/win32cond.c extra/getopt.c extra/getopt_long.c)
        set_source_files_properties("${CMAKE_SOURCE_DIR}/third-party/libde265/${windows_only}"
                PROPERTIES HEADER_FILE_ONLY ON)
    endforeach ()
endif ()

if (NOT MSVC)
    file(GLOB _de265_x86 "${CMAKE_SOURCE_DIR}/third-party/libde265/libde265/x86/*.cc")

    set_source_files_properties(${_de265_x86} PROPERTIES COMPILE_OPTIONS "-mssse3;-msse4.1")

    # image.cc picks its aligned allocator from MinGW, MSVC, posix_memalign, then plain memalign.
    # Only the last is left here, and glibc stopped declaring it.
    target_compile_definitions(diffractor_de265 PRIVATE HAVE_POSIX_MEMALIGN)
endif ()
