// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "pch.h"

#include "util_base64.h"
#include "util_simd.h"
#include "crypto.h"
#include "crypto_sha.h"
#include "crypto_aes256.h"

///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////

static constexpr int SHA1_DIGEST_LENGTH = 20;
static constexpr int SHA1_BLOCK_SIZE = 64;

std::u8string crypto::hmac_sha1(const std::u8string_view key, const std::u8string_view data)
{
	sha1 sha1;

	uint8_t hash[SHA1_BLOCK_SIZE] = {};

	if (key.size() > SHA1_BLOCK_SIZE)
	{
		sha1.update({std::bit_cast<const uint8_t*>(key.data()), key.size()});
		sha1.final(hash);
	}
	else
	{
		memcpy_s(hash, SHA1_BLOCK_SIZE, key.data(), key.size());
	}

	uint8_t ipad[SHA1_BLOCK_SIZE];
	uint8_t opad[SHA1_BLOCK_SIZE];

	for (int i = 0; i < SHA1_BLOCK_SIZE; i++)
	{
		ipad[i] = 0x36 ^ hash[i];
		opad[i] = 0x5c ^ hash[i];
	}

	sha1.update({ipad, SHA1_BLOCK_SIZE});
	sha1.update({std::bit_cast<const uint8_t*>(data.data()), data.size()});
	sha1.final(hash);

	sha1.update({opad, SHA1_BLOCK_SIZE});
	sha1.update({hash, SHA1_DIGEST_LENGTH});
	sha1.final(hash);

	return base64_encode(hash, SHA1_DIGEST_LENGTH);
}

///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////


std::vector<uint8_t> crypto::encrypt(df::cspan input, const std::u8string_view password)
{
	sha1 hash;
	hash.update({std::bit_cast<const uint8_t*>(password.data()), password.size()});

	uint8_t digest[sha1::DIGEST_SIZE];
	hash.final(digest);

	const std::vector<uint8_t> key(digest, digest + sha1::DIGEST_SIZE);

	std::vector<uint8_t> result;
	aes256::encrypt(key, input, result);
	return result;
}

std::vector<uint8_t> crypto::encrypt(const std::vector<uint8_t>& input, const std::vector<uint8_t>& key)
{
	std::vector<uint8_t> result;
	aes256::encrypt(key, input, result);
	return result;
}

std::vector<uint8_t> crypto::decrypt(df::cspan input, const std::u8string_view password)
{
	sha1 hash;
	hash.update({std::bit_cast<const uint8_t*>(password.data()), password.size()});

	uint8_t digest[sha1::DIGEST_SIZE];
	hash.final(digest);

	const std::vector<uint8_t> key(digest, digest + sha1::DIGEST_SIZE);

	std::vector<uint8_t> result;
	aes256::decrypt(key, input, result);
	return result;
}



///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////
// CRC-32



uint32_t crypto::crc32c(const void* data, const size_t len)
{
	return ~crc32c(CRCINIT, data, len);
}

uint32_t crypto::crc32c(uint32_t crc, const void* data, size_t len)
{
	if (platform::crc32_supported)
	{
		return calc_crc32c_sse2(crc, data, len);
	}

	if (platform::neon_supported)
	{
		return calc_crc32c_arm(crc, data, len);
	}

	return calc_crc32c_c(crc, data, len);
}

uint32_t crypto::crc32c(const std::u8string_view sv)
{
	return crc32c(sv.data(), sv.size());
}

static constexpr uint32_t FNV_PRIME_32 = 16777619u;
static constexpr uint32_t OFFSET_BASIS_32 = 2166136261u;

uint32_t crypto::fnv1a(const void* data, const size_t len)
{
	const auto* p = static_cast<const uint8_t*>(data);
	uint32_t result = OFFSET_BASIS_32;

	for (size_t i = 0; i < len; ++i)
	{
		result ^= p[i];
		result *= FNV_PRIME_32;
	}
	return result;
}

uint32_t crypto::fnv1a_i(const std::u8string_view sv)
{
	auto p = sv.begin();
	uint32_t result = OFFSET_BASIS_32;

	while (p < sv.end())
	{
		result ^= str::to_lower(str::pop_utf8_char(p, sv.end()));
		result *= FNV_PRIME_32;
	}

	return result;
}

uint32_t crypto::fnv1a_i(const std::string_view sv)
{
	uint32_t result = OFFSET_BASIS_32;

	for (const auto c : sv)
	{
		result ^= str::to_lower(c);
		result *= FNV_PRIME_32;
	}

	return result;
}

uint32_t crypto::fnv1a_i(const std::u8string_view sv1, const std::u8string_view sv2)
{
	auto p = sv1.begin();
	uint32_t result = OFFSET_BASIS_32;

	while (p < sv1.end())
	{
		result ^= str::to_lower(str::pop_utf8_char(p, sv1.end()));
		result *= FNV_PRIME_32;
	}

	p = sv2.begin();

	while (p < sv2.end())
	{
		result ^= str::to_lower(str::pop_utf8_char(p, sv2.end()));
		result *= FNV_PRIME_32;
	}

	return result;
}
