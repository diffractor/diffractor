# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: What the ZLib .vcxproj could not describe.
#
# zlib-ng compiles one source per instruction set and dispatches at run time, so each has to be
# built for the set it implements. MSVC accepts the intrinsics on the baseline and the project
# therefore says nothing; GCC and Clang refuse to inline an intrinsic into a target that has not
# enabled it. The names carry the answer, so the flags are matched to them rather than listed
# twice.

if (NOT MSVC)
    # HAVE_CPUID_MS selects <intrin.h> in x86_features.c. The GNU spelling is <cpuid.h>.
    get_target_property(_zlib_defs diffractor_zlib COMPILE_DEFINITIONS)
    list(FILTER _zlib_defs EXCLUDE REGEX "HAVE_CPUID_MS")
    set_target_properties(diffractor_zlib PROPERTIES COMPILE_DEFINITIONS "${_zlib_defs}")

    # The Windows project cannot define these: MSVC has no __builtin_ctz, which is what
    # fallback_builtins.h exists to supply. Several arch functions are declared only when it does,
    # so without them functable.c refers to functions nothing declared.
    target_compile_definitions(diffractor_zlib PRIVATE
            HAVE_CPUID_GNU HAVE_BUILTIN_CTZ HAVE_BUILTIN_CTZLL)

    # AVX-512 hardware always has BMI2, but the compiler still wants to be told: chunkset_avx512.c
    # calls _bzhi_u32.
    set(_zlib_avx512 -mavx512f -mavx512dq -mavx512bw -mavx512vl -mbmi2)

    file(GLOB _zlib_x86 "${CMAKE_SOURCE_DIR}/third-party/ZLib/arch/x86/*.c")

    foreach (_source IN LISTS _zlib_x86)
        get_filename_component(_name "${_source}" NAME)
        set(_flags "")

        if (_name MATCHES "vpclmulqdq")
            set(_flags ${_zlib_avx512} -mvpclmulqdq -mpclmul)
        elseif (_name MATCHES "avx512_vnni")
            set(_flags ${_zlib_avx512} -mavx512vnni)
        elseif (_name MATCHES "avx512")
            set(_flags ${_zlib_avx512})
        elseif (_name MATCHES "avx2")
            set(_flags -mavx2)
        elseif (_name MATCHES "pclmulqdq")
            set(_flags -mpclmul -msse4.1)
        elseif (_name MATCHES "sse42")
            set(_flags -msse4.2)
        elseif (_name MATCHES "sse41")
            set(_flags -msse4.1)
        elseif (_name MATCHES "ssse3")
            set(_flags -mssse3)
        elseif (_name MATCHES "sse2")
            set(_flags -msse2)
        endif ()

        if (_flags)
            set_source_files_properties("${_source}" PROPERTIES COMPILE_OPTIONS "${_flags}")
        endif ()
    endforeach ()

    # X86_HAVE_XSAVE_INTRIN makes the feature check itself call _xgetbv.
    set_source_files_properties("${CMAKE_SOURCE_DIR}/third-party/ZLib/arch/x86/x86_features.c"
            PROPERTIES COMPILE_OPTIONS "-mxsave")
endif ()
