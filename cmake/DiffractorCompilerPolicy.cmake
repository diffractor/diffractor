# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: The compiler and linker policy for every target, application and vendored alike. It is
# stated once here rather than per target so that "are the flags right" is one file to review.
#
# The MSVC half is a transcription of src/app.vcxproj, annotated with the project setting each flag
# comes from so the two can be compared line by line for as long as both exist. Once the vcxproj
# files are deleted this file is the only record of them, so nothing here is guesswork: anything
# that could not be traced to a setting was left out rather than invented.

include_guard(GLOBAL)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 17)

if (MSVC)
    # RuntimeLibrary: MultiThreaded / MultiThreadedDebug. CMake defaults to the DLL runtime, which
    # would change what the shipped binary depends on without changing anything a test observes.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    add_compile_definitions(WIN32 _WINDOWS)

    add_compile_options(
            /bigobj         # AdditionalOptions
            /utf-8          # AdditionalOptions - source and execution charset, not a warning switch
            /FS             # AdditionalOptions - required for /MP to share one PDB writer
            /W3             # WarningLevel Level3
            /GF             # StringPooling
            /Gy             # FunctionLevelLinking
            /Oi             # IntrinsicFunctions
            /GS             # BufferSecurityCheck, on in both configurations
            /sdl            # SDLCheck
            /MP             # MultiProcessorCompilation
            /Zi             # DebugInformationFormat ProgramDatabase, in Release too
            /EHsc           # ExceptionHandling Sync
            /GR             # RuntimeTypeInfo
            /fp:fast        # FloatingPointModel Fast - changes results, not just speed
    )

    # EnableEnhancedInstructionSet StreamingSIMDExtensions2. SSE2 is unconditional on x64, where
    # the switch is rejected rather than ignored, so it is only meaningful for the 32-bit build.
    if (CMAKE_SIZEOF_VOID_P EQUAL 4)
        add_compile_options(/arch:SSE2)
    endif ()

    # Optimization, InlineFunctionExpansion, BasicRuntimeChecks, ControlFlowGuard. Guard is on in
    # Debug and off in Release, which is the reverse of the usual arrangement; it is transcribed as
    # found rather than corrected here.
    set(CMAKE_C_FLAGS_DEBUG "/Od /Ob0 /RTC1 /guard:cf /D_DEBUG")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")

    # Optimization MaxSpeed, InlineFunctionExpansion AnySuitable, FavorSizeOrSpeed Size,
    # OmitFramePointers, WholeProgramOptimization, SupportJustMyCode false.
    set(CMAKE_C_FLAGS_RELEASE "/O2 /Ob2 /Os /Oy /GL /JMC- /DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")

    # GenerateDebugInformation is true in both configurations: a release build without symbols
    # cannot be attributed to a cause when it crashes. See docs/crash.md.
    add_link_options(/DEBUG)

    # OptimizeReferences, EnableCOMDATFolding, LinkTimeCodeGeneration
    # UseFastLinkTimeCodeGeneration. Spelled out rather than using CMake's interprocedural
    # optimisation property, which emits plain /LTCG and would not be the same build.
    foreach (kind EXE SHARED MODULE)
        set(CMAKE_${kind}_LINKER_FLAGS_RELEASE "/OPT:REF /OPT:ICF /LTCG:incremental")
    endforeach ()
else ()
    add_compile_options(
            -Wall
            -Wno-unknown-pragmas
            -Wno-multichar
            # GCC 13 makes this an error by default. Accessors such as ui::surface::orientation()
            # share a name with a type in the enclosing namespace, which is ill-formed by the letter
            # of the standard and accepted by every compiler; renaming them buys nothing.
            -Wno-changes-meaning
    )
endif ()

# Diffractor's own code. Warnings are on, and the precompiled header is applied where it works.
function(diffractor_apply_app_policy target)
    if (MSVC)
        # PrecompiledHeader Use, pch.h. Not applied elsewhere: CMake's forced include reaches the
        # same header by a different path, which defeats #pragma once on a case-preserving
        # case-insensitive filesystem and reports every declaration in it as a redefinition.
        target_precompile_headers(${target} PRIVATE "${CMAKE_SOURCE_DIR}/src/pch.h")

        # SubSystem Windows.
        set_target_properties(${target} PROPERTIES WIN32_EXECUTABLE ON)
    endif ()
endfunction()

# Vendored code. It is not ours to make warning-clean, and the noise would bury our own.
function(diffractor_apply_vendored_policy target)
    if (MSVC)
        target_compile_options(${target} PRIVATE /W0)
    else ()
        # Must not reach an assembler: there -w expects an argument and would swallow the next
        # define. See cmake/vendored/LibJpeg.cmake.
        target_compile_options(${target} PRIVATE
                $<$<COMPILE_LANGUAGE:C>:-w>
                $<$<COMPILE_LANGUAGE:CXX>:-w>)
    endif ()

    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endfunction()
