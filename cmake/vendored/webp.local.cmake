# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: What webp.vcxproj could not describe for the vendored webp target.

# files_webp.cpp spells its includes the way the system package installs them -- "webp/decode.h" --
# and in the vendored tree that directory is third-party/webp/src/webp. Windows answered this with
# a global include path; off Windows nothing did, so a fully vendored build (which is what CI is,
# with no libwebp-dev present) stopped at "webp/decode.h: No such file or directory".
target_include_directories(diffractor_webp PUBLIC
        "${CMAKE_SOURCE_DIR}/third-party/webp/src"
)
