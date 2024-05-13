#pragma once


#if defined(_M_IX86) || defined(_M_X64)

#define X86_AVX_CHUNKSET 1
#define X86_AVX2 1
#define X86_AVX2_ADLER32 1
#define X86_AVX512 1
#define X86_AVX512VNNI 1
#define X86_FEATURES 1
#define X86_SSE2 1
#define X86_SSE2_CHUNKSET 1
#define X86_SSSE3 1
#define X86_SSE42 1
#define X86_SSE42_CMP_STR 1
#define X86_SSE42_CRC_HASH 1
#define X86_SSE42_CRC_INTRIN 1
#define X86_SSSE3_ADLER32 1
#define HAVE_BITSCANFORWARD64 1
#define HAVE_BITSCANFORWARD 1


#endif

#if defined(_M_ARM64)

#define ARM_FEATURES 1

#endif