// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: SIMD intrinsics wrappers. Provides SSE/AVX/NEON optimized
// implementations for image processing and CRC calculations.

#pragma once

#include "util.h"

// Include proper SIMD intrinsics headers
#if defined(COMPILE_SIMD_INTRINSIC)
#include <emmintrin.h>  // For SSE2 intrinsics
#include <nmmintrin.h>  // For CRC32 intrinsics
#endif

#if defined(COMPILE_ARM_INTRINSIC)
#include <arm_acle.h>   // For ARM CRC intrinsics
#endif

///////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////

/* CRC-32 (Ethernet, ZIP, etc.) polynomial in reversed bit order. */
// static constexpr uint32_t CRCPOLY = 0xedb88320;

/* CRC-32C (iSCSI) polynomial in reversed bit order. */
static constexpr uint32_t CRCPOLY = 0x82f63b78;
//static constexpr uint32_t CRCPOLY = 0xEDB88320u;
static constexpr uint32_t CRCINIT = 0xFFFFFFFF;


inline std::array<std::array<uint32_t, 256>, 4> create_crc32_precalc()
{
	std::array<std::array<uint32_t, 256>, 4> result;

	for (auto i = 0u; i <= 0xFF; i++)
	{
		uint32_t x = i;

		for (uint32_t j = 0; j < 8; j++)
			x = x >> 1 ^ CRCPOLY & -static_cast<int32_t>(x & 1);

		result[0][i] = x;
	}

	for (auto i = 0u; i <= 0xFF; i++)
	{
		uint32_t c = result[0][i];

		for (auto j = 1u; j < 4; j++)
		{
			c = result[0][c & 0xFF] ^ c >> 8;
			result[j][i] = c;
		}
	}

	return result;
}

inline uint32_t calc_crc32c_c(uint32_t crc, const void* data, const size_t len)
{
	static const auto crc_precalc = create_crc32_precalc();

	const auto* p = static_cast<const uint8_t*>(data);
	const auto* const end = p + len;

	// Align only to the width the slice-by-4 loop reads; over-aligning pushes short
	// strings entirely through the byte loop.
	while (p < end && std::bit_cast<uintptr_t>(p) & 0x03)
	{
		crc = crc_precalc[0][(crc ^ *p++) & 0xFF] ^ crc >> 8;
	}

	while (static_cast<size_t>(end - p) >= sizeof(uint32_t))
	{
		crc ^= *std::bit_cast<const uint32_t*>(p);
		crc =
			crc_precalc[3][crc & 0xFF] ^
			crc_precalc[2][crc >> 8 & 0xFF] ^
			crc_precalc[1][crc >> 16 & 0xFF] ^
			crc_precalc[0][crc >> 24 & 0xFF];

		p += sizeof(uint32_t);
	}

	while (p < end)
	{
		crc = crc_precalc[0][(crc ^ *p++) & 0xFF] ^ crc >> 8;
	}

	return crc;
}

inline uint32_t calc_crc32c_x86(uint32_t crc, const void* data, const size_t len)
{
#if defined(COMPILE_SIMD_INTRINSIC)
	const auto* p = static_cast<const uint8_t*>(data);
	const auto* const end = p + len;

	// Align to an 8-byte boundary so the wide loop below reads aligned words.
	while (p < end && (std::bit_cast<uintptr_t>(p) & 0x07))
	{
		crc = _mm_crc32_u8(crc, *p++);
	}

#if defined(_M_X64)
	// _mm_crc32_u64 consumes 8 bytes per instruction (64-bit targets only),
	// roughly doubling throughput versus the 32-bit path on large buffers.
	uint64_t crc64 = crc;

	while (static_cast<size_t>(end - p) >= sizeof(uint64_t))
	{
		crc64 = _mm_crc32_u64(crc64, *std::bit_cast<const uint64_t*>(p));
		p += sizeof(uint64_t);
	}

	crc = static_cast<uint32_t>(crc64);
#endif

	while (static_cast<size_t>(end - p) >= sizeof(uint32_t))
	{
		crc = _mm_crc32_u32(crc, *std::bit_cast<const uint32_t*>(p));
		p += sizeof(uint32_t);
	}

	while (p < end)
	{
		crc = _mm_crc32_u8(crc, *p++);
	}

	return crc;
#else
	// Fallback to software implementation when SIMD not available
	// Mark parameters as used to avoid warnings
	(void)crc;
	(void)data;
	(void)len;
	return calc_crc32c_c(crc, data, len);
#endif
}

inline uint32_t calc_crc32c_arm(uint32_t crc, const void* data, const size_t len)
{
#if defined(COMPILE_ARM_INTRINSIC)
	const auto* p = static_cast<const uint8_t*>(data);
	const auto* const end = p + len;

	// Align to an 8-byte boundary so the wide loop below reads aligned words.
	while (p < end && (std::bit_cast<uintptr_t>(p) & 0x07))
	{
		crc = __crc32b(crc, *p++);
	}

	// __crc32d consumes 8 bytes per instruction, roughly doubling throughput
	// versus the 32-bit path on large buffers.
	while (static_cast<size_t>(end - p) >= sizeof(uint64_t))
	{
		crc = __crc32d(crc, *std::bit_cast<const uint64_t*>(p));
		p += sizeof(uint64_t);
	}

	while (static_cast<size_t>(end - p) >= sizeof(uint32_t))
	{
		crc = __crc32w(crc, *std::bit_cast<const uint32_t*>(p));
		p += sizeof(uint32_t);
	}

	while (p < end)
	{
		crc = __crc32b(crc, *p++);
	}

	return crc;
#else
	// Fallback to software implementation when ARM intrinsics not available
	// Mark parameters as used to avoid warnings
	(void)crc;
	(void)data;
	(void)len;
	return calc_crc32c_c(crc, data, len);
#endif
}

#if defined(COMPILE_SIMD_INTRINSIC)
inline __m128i pack_bgra_sse2(const __m128 p0, const __m128 p1, const __m128 p2, const __m128 p3) noexcept
{
	const auto i0 = _mm_cvttps_epi32(_mm_add_ps(p0, _mm_set1_ps(0.5f)));
	const auto i1 = _mm_cvttps_epi32(_mm_add_ps(p1, _mm_set1_ps(0.5f)));
	const auto i2 = _mm_cvttps_epi32(_mm_add_ps(p2, _mm_set1_ps(0.5f)));
	const auto i3 = _mm_cvttps_epi32(_mm_add_ps(p3, _mm_set1_ps(0.5f)));
	return _mm_packus_epi16(_mm_packs_epi32(i0, i1), _mm_packs_epi32(i2, i3));
}

inline void unpack_bgra_sse2(const __m128i packed, __m128& p0, __m128& p1, __m128& p2, __m128& p3) noexcept
{
	const auto zero = _mm_setzero_si128();
	const auto lo = _mm_unpacklo_epi8(packed, zero);
	const auto hi = _mm_unpackhi_epi8(packed, zero);
	p0 = _mm_cvtepi32_ps(_mm_unpacklo_epi16(lo, zero));
	p1 = _mm_cvtepi32_ps(_mm_unpackhi_epi16(lo, zero));
	p2 = _mm_cvtepi32_ps(_mm_unpacklo_epi16(hi, zero));
	p3 = _mm_cvtepi32_ps(_mm_unpackhi_epi16(hi, zero));
}

inline __m128 blend_bgra_sse2(const __m128 dest, const __m128 src, const __m128 alpha) noexcept
{
	const auto blended = _mm_add_ps(_mm_mul_ps(src, alpha), _mm_mul_ps(dest, _mm_sub_ps(_mm_set1_ps(1.0f), alpha)));
	const auto rgb_mask = _mm_set_epi32(0, -1, -1, -1);
	const auto opaque_alpha = _mm_castps_si128(_mm_set_ps(255.0f, 0.0f, 0.0f, 0.0f));
	return _mm_castsi128_ps(_mm_or_si128(_mm_and_si128(_mm_castps_si128(blended), rgb_mask), opaque_alpha));
}

inline size_t blend_solid_opaque_sse2(uint8_t* dest, const size_t count, const float b, const float g,
	                                  const float r, const float alpha) noexcept
{
	const auto vector_count = count & ~size_t{3};
	const auto src = _mm_set_ps(255.0f, r * 255.0f, g * 255.0f, b * 255.0f);
	const auto a = _mm_set1_ps(alpha);

	for (size_t i = 0; i < vector_count; i += 4)
	{
		__m128 d0, d1, d2, d3;
		unpack_bgra_sse2(_mm_loadu_si128(std::bit_cast<const __m128i*>(dest + i * 4)), d0, d1, d2, d3);
		_mm_storeu_si128(std::bit_cast<__m128i*>(dest + i * 4), pack_bgra_sse2(
			blend_bgra_sse2(d0, src, a), blend_bgra_sse2(d1, src, a),
			blend_bgra_sse2(d2, src, a), blend_bgra_sse2(d3, src, a)));
	}

	return vector_count;
}

inline size_t blend_glyph_opaque_sse2(uint8_t* dest, const uint8_t* coverage, const size_t count,
	                                  const float b, const float g, const float r, const float alpha) noexcept
{
	const auto vector_count = count & ~size_t{3};
	const auto src = _mm_set_ps(255.0f, r * 255.0f, g * 255.0f, b * 255.0f);
	constexpr float byte_scale = 1.0f / 255.0f;

	for (size_t i = 0; i < vector_count; i += 4)
	{
		__m128 d0, d1, d2, d3;
		unpack_bgra_sse2(_mm_loadu_si128(std::bit_cast<const __m128i*>(dest + i * 4)), d0, d1, d2, d3);
		const auto a0 = _mm_set1_ps(alpha * coverage[i] * byte_scale);
		const auto a1 = _mm_set1_ps(alpha * coverage[i + 1] * byte_scale);
		const auto a2 = _mm_set1_ps(alpha * coverage[i + 2] * byte_scale);
		const auto a3 = _mm_set1_ps(alpha * coverage[i + 3] * byte_scale);
		_mm_storeu_si128(std::bit_cast<__m128i*>(dest + i * 4), pack_bgra_sse2(
			blend_bgra_sse2(d0, src, a0), blend_bgra_sse2(d1, src, a1),
			blend_bgra_sse2(d2, src, a2), blend_bgra_sse2(d3, src, a3)));
	}

	return vector_count;
}

inline size_t blend_bgra_opaque_sse2(uint8_t* dest, const uint8_t* src_bytes, const size_t count,
	                                 const bool has_alpha, const float global_alpha) noexcept
{
	const auto vector_count = count & ~size_t{3};
	const auto rgb_mask = _mm_set_epi32(0, -1, -1, -1);
	const auto opaque_alpha = _mm_castps_si128(_mm_set_ps(255.0f, 0.0f, 0.0f, 0.0f));
	constexpr float byte_scale = 1.0f / 255.0f;

	for (size_t i = 0; i < vector_count; i += 4)
	{
		__m128 d0, d1, d2, d3;
		__m128 s0, s1, s2, s3;
		unpack_bgra_sse2(_mm_loadu_si128(std::bit_cast<const __m128i*>(dest + i * 4)), d0, d1, d2, d3);
		unpack_bgra_sse2(_mm_loadu_si128(std::bit_cast<const __m128i*>(src_bytes + i * 4)), s0, s1, s2, s3);
		const auto a0 = _mm_set1_ps((has_alpha ? src_bytes[i * 4 + 3] * byte_scale : 1.0f) * global_alpha);
		const auto a1 = _mm_set1_ps((has_alpha ? src_bytes[i * 4 + 7] * byte_scale : 1.0f) * global_alpha);
		const auto a2 = _mm_set1_ps((has_alpha ? src_bytes[i * 4 + 11] * byte_scale : 1.0f) * global_alpha);
		const auto a3 = _mm_set1_ps((has_alpha ? src_bytes[i * 4 + 15] * byte_scale : 1.0f) * global_alpha);
		s0 = _mm_castsi128_ps(_mm_or_si128(_mm_and_si128(_mm_castps_si128(s0), rgb_mask), opaque_alpha));
		s1 = _mm_castsi128_ps(_mm_or_si128(_mm_and_si128(_mm_castps_si128(s1), rgb_mask), opaque_alpha));
		s2 = _mm_castsi128_ps(_mm_or_si128(_mm_and_si128(_mm_castps_si128(s2), rgb_mask), opaque_alpha));
		s3 = _mm_castsi128_ps(_mm_or_si128(_mm_and_si128(_mm_castps_si128(s3), rgb_mask), opaque_alpha));
		_mm_storeu_si128(std::bit_cast<__m128i*>(dest + i * 4), pack_bgra_sse2(
			blend_bgra_sse2(d0, s0, a0), blend_bgra_sse2(d1, s1, a1),
			blend_bgra_sse2(d2, s2, a2), blend_bgra_sse2(d3, s3, a3)));
	}

	return vector_count;
}
#endif

///////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////
