// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Image surface operations. Implements pixel-level transformations
// including RGB/BGR swapping and pixel difference calculations.

#include "pch.h"
#include "util.h"
#include "util_simd.h"

#ifdef COMPILE_SIMD_INTRINSIC
#include <emmintrin.h>
#include <tmmintrin.h>

// Separated so SSSE3 can be named for this loop alone. Naming it on swap_rb would let the scalar
// fallback below use SSSE3 as well - on a processor that reached the fallback by not having it.
DF_TARGET_SSSE3 static void swap_rb_ssse3(uint8_t* const pixels, const int stride, const int height)
{
	const auto mask = _mm_setr_epi8(
		0x02, 0x01, 0x00, 0x03,
		0x06, 0x05, 0x04, 0x07,
		0x0a, 0x09, 0x08, 0x0b,
		0x0e, 0x0d, 0x0c, 0x0f);

	for (auto y = 0; y < height; y++)
	{
		const auto dst = pixels + y * stride;
		const auto* end = dst + stride;

		for (auto p = dst; p < end; p += 16)
		{
			const __m128i pixel = _mm_load_si128(std::bit_cast<__m128i*>(p));
			_mm_store_si128(std::bit_cast<__m128i*>(p), _mm_shuffle_epi8(pixel, mask));
		}
	}
}
#endif

void ui::surface::swap_rb()
{
#ifdef COMPILE_SIMD_INTRINSIC
	// _mm_shuffle_epi8 is an SSSE3 instruction, so gate on SSSE3 support (not
	// just SSE2 which is the x64 baseline) to avoid an illegal-instruction fault
	// on early x64 CPUs that have SSE2 but lack SSSE3.
	if (platform::ssse3_supported)
	{
		const auto dest_stride = _stride;

		if (std::bit_cast<uintptr_t>(_pixels.get()) % 16 == 0 && dest_stride % 16 == 0)
		{
			swap_rb_ssse3(_pixels.get(), dest_stride, _dimensions.cy);
			return;
		}
	}
#endif

	for (auto y = 0; y < _dimensions.cy; y++)
	{
		const auto dst = std::bit_cast<uint32_t*>(pixels_line(y));
		const auto* end = dst + _dimensions.cx;

		for (auto p = dst; p < end; p += 1)
		{
			*p = bgr(*p);
		}
	}
}


ui::pixel_difference_result ui::surface::pixel_difference(const const_surface_ptr& other) const
{
	if (_dimensions != other->dimensions())
	{
		return pixel_difference_result::not_equal;
	}

	for (auto y = 0; y < _dimensions.cy; y++)
	{
		const auto this_line = std::bit_cast<const uint32_t*>(pixels_line(y));
		const auto other_line = std::bit_cast<const uint32_t*>(other->pixels_line(y));

		for (auto x = 0; x < _dimensions.cx; x++)
		{
			if (this_line[x] != other_line[x])
			{
				return pixel_difference_result::not_equal;
			}
		}
	}

	return pixel_difference_result::equal;
}
