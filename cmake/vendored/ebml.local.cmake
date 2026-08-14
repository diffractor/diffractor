# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: What the libebml .vcxproj could not describe. The vendored tree carries only the Win32
# implementation of the platform IO callback; elsewhere the portable StdIOCallback is what the
# library uses, so the Win32 file is simply not built.

if (NOT WIN32)
    set_source_files_properties("${CMAKE_SOURCE_DIR}/third-party/libebml/src/platform/win32/WinIOCallback.cpp"
            PROPERTIES HEADER_FILE_ONLY ON)
endif ()
