# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: What the dav1d .vcxproj could not describe. The assembly configuration it carries only
# names the Windows output formats, so a replacement is put ahead of it on nasm's include path.
# See dav1d-linux/config.asm.
#
# It has to go on the target rather than on the sources: CMake gives nasm the target's include
# directories first, so a per-source -I lands after third-party/dav1d and never wins.

if (NOT WIN32)
    target_include_directories(diffractor_dav1d BEFORE PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/dav1d-linux")
endif ()
