# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: What the highway .vcxproj could not describe. It builds unoptimised in Release as well
# as in Debug -- Optimization is Disabled in every configuration -- and libjxl, which is highway's
# only consumer here, does the same. The importer does not carry that setting, because optimisation
# is otherwise the build type's business rather than a per-library one.
#
# This is not cosmetic. Letting the Release policy optimise these two makes zoltan-tasi.jxl decode
# to nothing, which "Should scan jxl metadata" catches. Restore the setting rather than
# investigating the miscompile: the shipped Windows binary has been built this way for years, so
# matching it is what "the same product from a different build" means.

if (MSVC)
    target_compile_options(diffractor_highway PRIVATE $<$<CONFIG:Release>:/Od> $<$<CONFIG:Release>:/GL->)
endif ()
