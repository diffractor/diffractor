# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: What the expat .vcxproj could not describe. Windows gets its entropy from rand_s, which
# expat selects on _WIN32 by itself; every other platform has to be told which source to use, and
# expat refuses to compile rather than quietly picking a weak one.

if (NOT WIN32)
    target_compile_definitions(diffractor_expat PRIVATE HAVE_GETRANDOM)
endif ()
