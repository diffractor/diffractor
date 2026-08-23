# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: What the dng .vcxproj could not describe. dng_xmp_sdk.cpp includes XMP.hpp, which
# rejects a build that has not named its platform. The dependency is real, so it is stated as one
# rather than by repeating the environment macro here.

if (TARGET diffractor::xmp)
    target_link_libraries(diffractor_dng PRIVATE diffractor::xmp)
endif ()

# SXMPDocOps is declared nowhere in this tree, so the DNG SDK's document-history support cannot be
# built at all. RawEnvironment.h already knows that and switches it off - but only in its _WIN32
# branch, so the __linux__ branch right above it walks into four uses of a class that does not
# exist. dng_flags.h guards the flag with #ifndef, so naming it here is enough. Windows is left
# alone because RawEnvironment.h defines it unguarded there and would warn about the redefinition.
if (NOT WIN32)
    target_compile_definitions(diffractor_dng PRIVATE qDNGXMPDocOps=0)
endif ()
