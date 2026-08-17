# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: What the jxl .vcxproj could not describe. See highway.local.cmake: both build
# unoptimised in Release under MSBuild, and optimising them here makes a JPEG XL decode to an empty
# image. "Should scan jxl metadata" is the check.

if (MSVC)
    target_compile_options(diffractor_jxl PRIVATE $<$<CONFIG:Release>:/Od> $<$<CONFIG:Release>:/GL->)
endif ()
