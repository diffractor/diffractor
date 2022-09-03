// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

#include "util.h"

///////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////

/* CRC-32 (Ethernet, ZIP, etc.) polynomial in reversed bit order. */
// static constexpr uint32_t CRCPOLY = 0xedb88320;

/* CRC-32C (iSCSI) polynomial in reversed bit order. */
static constexpr uint32_t CRCPOLY = 0x82f63b78;

//static constexpr uint32_t CRCPOLY = 0xEDB88320u;


static std::array<std::array<uint32_t, 256>, 4> create_crc32_precalc()
{
	std::array<std::array<uint32_t, 256>, 4> result;

	for (auto i = 0u; i <= 0xFF; i++)
	{
		uint32_t x = i;

		for (uint32_t j = 0; j < 8; j++)
			x = (x >> 1) ^ (CRCPOLY & (-static_cast<int32_t>(x & 1)));

		result[0][i] = x;
	}

	for (auto i = 0u; i <= 0xFF; i++)
	{
		uint32_t c = result[0][i];

		for (auto j = 1u; j < 4; j++)
		{
			c = result[0][c & 0xFF] ^ (c >> 8);
			result[j][i] = c;
		}
	}

	return result;
}

static uint32_t calc_crc32c_c(uint32_t crc, const void* data, const size_t len)
{
	static const auto crc_precalc = create_crc32_precalc();

	const auto* p = static_cast<const uint8_t*>(data);
	const auto* const end = p + len;

	while (p < end && std::bit_cast<uintptr_t>(p) & 0x0f)
	{
		crc = crc_precalc[0][(crc ^ *p++) & 0xFF] ^ (crc >> 8);
	}

	while (p + (sizeof(uint32_t) - 1) < end)
	{
		crc ^= *std::bit_cast<const uint32_t*>(p);
		crc =
			crc_precalc[3][(crc) & 0xFF] ^
			crc_precalc[2][(crc >> 8) & 0xFF] ^
			crc_precalc[1][(crc >> 16) & 0xFF] ^
			crc_precalc[0][(crc >> 24) & 0xFF];

		p += sizeof(uint32_t);
	}

	while (p < end)
	{
		crc = crc_precalc[0][(crc ^ *p++) & 0xFF] ^ (crc >> 8);
	}

	return crc;
}

static uint32_t calc_crc32c_sse2(uint32_t crc, const void* data, const size_t len)
{
#if defined(COMPILE_SIMD_INTRINSIC)
	const auto* p = static_cast<const uint8_t*>(data);
	const auto* const end = p + len;

	while (p < end && (std::bit_cast<uintptr_t>(p) & 0x0f))
	{
		crc = _mm_crc32_u8(crc, *p++);
	}

	while (p + (sizeof(uint32_t) - 1) < end)
	{
		crc = _mm_crc32_u32(crc, *std::bit_cast<const uint32_t*>(p));
		p += sizeof(uint32_t);
	}

	while (p < end)
	{
		crc = _mm_crc32_u8(crc, *p++);
	}

#endif

	return crc;
}

static uint32_t calc_crc32c_arm(uint32_t crc, const void* data, const size_t len)
{
#if defined(COMPILE_ARM_INTRINSIC)
	const auto* p = static_cast<const uint8_t*>(data);
	const auto* const end = p + len;

	while (p < end && (std::bit_cast<uintptr_t>(p) & 0x0f))
	{
		crc = __crc32b(crc, *p++);
	}

	while (p + (sizeof(uint32_t) - 1) < end)
	{
		crc = __crc32w(crc, *std::bit_cast<const uint32_t*>(p));
		p += sizeof(uint32_t);
	}

	while (p < end)
	{
		crc = __crc32b(crc, *p++);
	}
#endif

	return crc;
}

///////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////