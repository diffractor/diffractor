# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: What the dng .vcxproj could not describe. dng_xmp_sdk.cpp includes XMP.hpp, which
# rejects a build that has not named its platform. The dependency is real, so it is stated as one
# rather than by repeating the environment macro here.

if (TARGET diffractor::xmp)
    target_link_libraries(diffractor_dng PRIVATE diffractor::xmp)
endif ()
