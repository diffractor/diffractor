// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Cryptographic utilities including HMAC-SHA1, AES encryption/decryption,
// CRC32 checksums, and FNV-1a hashing for file integrity and authentication.

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

std::string crypto::hmac_sha1(const std::string_view key, const std::string_view data)
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

static constexpr size_t KDF_ITERATIONS = 10000;
static constexpr size_t HMAC_SIZE = crypto::sha256::DIGEST_SIZE; // 32 bytes

static std::vector<uint8_t> hmac_sha256_raw(const uint8_t* key, const size_t key_len,
                                            const uint8_t* data, const size_t data_len)
{
	static constexpr int HMAC_BLOCK_SIZE = 64;
	crypto::sha256 sha;

	uint8_t adjusted_key[HMAC_BLOCK_SIZE] = {};

	if (key_len > HMAC_BLOCK_SIZE)
	{
		sha.update({key, key_len});
		sha.final(adjusted_key);
	}
	else
	{
		memcpy(adjusted_key, key, key_len);
	}

	uint8_t ipad[HMAC_BLOCK_SIZE];
	uint8_t opad[HMAC_BLOCK_SIZE];

	for (int i = 0; i < HMAC_BLOCK_SIZE; i++)
	{
		ipad[i] = 0x36 ^ adjusted_key[i];
		opad[i] = 0x5c ^ adjusted_key[i];
	}

	platform::secure_zero(adjusted_key, sizeof(adjusted_key));

	uint8_t inner_hash[crypto::sha256::DIGEST_SIZE];
	sha.update({ipad, HMAC_BLOCK_SIZE});
	sha.update({data, data_len});
	sha.final(inner_hash);

	uint8_t result[crypto::sha256::DIGEST_SIZE];
	sha.update({opad, HMAC_BLOCK_SIZE});
	sha.update({inner_hash, crypto::sha256::DIGEST_SIZE});
	sha.final(result);

	return std::vector<uint8_t>(result, result + crypto::sha256::DIGEST_SIZE);
}

static void derive_keys(const std::string_view password, uint8_t enc_key[32], uint8_t mac_key[32])
{
	const auto* pw = std::bit_cast<const uint8_t*>(password.data());
	const auto pw_len = password.size();

	// Initial hash with SHA-256 (produces full 32-byte key for AES-256)
	crypto::sha256 sha;
	sha.update({pw, pw_len});
	uint8_t derived[crypto::sha256::DIGEST_SIZE];
	sha.final(derived);

	// Key stretching: iterate SHA-256(previous || password)
	for (size_t i = 0; i < KDF_ITERATIONS; i++)
	{
		sha.update({derived, crypto::sha256::DIGEST_SIZE});
		sha.update({pw, pw_len});
		sha.final(derived);
	}

	memcpy(enc_key, derived, 32);

	// Derive separate MAC key
	constexpr uint8_t mac_label[] = "hmac-key";
	sha.update({derived, crypto::sha256::DIGEST_SIZE});
	sha.update({mac_label, sizeof(mac_label)});
	sha.final(mac_key);

	platform::secure_zero(derived, sizeof(derived));
}


std::vector<uint8_t> crypto::encrypt(const df::cspan input, const std::string_view password)
{
	uint8_t enc_key[sha256::DIGEST_SIZE];
	uint8_t mac_key[sha256::DIGEST_SIZE];
	derive_keys(password, enc_key, mac_key);

	const std::vector<uint8_t> key(enc_key, enc_key + sha256::DIGEST_SIZE);
	platform::secure_zero(enc_key, sizeof(enc_key));

	std::vector<uint8_t> result;

	if (aes256::encrypt(key, input, result) == 0)
	{
		platform::secure_zero(mac_key, sizeof(mac_key));
		return {};
	}

	// Encrypt-then-MAC: compute HMAC-SHA256 over ciphertext
	auto hmac = hmac_sha256_raw(mac_key, sizeof(mac_key), result.data(), result.size());
	platform::secure_zero(mac_key, sizeof(mac_key));
	result.insert(result.end(), hmac.begin(), hmac.end());

	return result;
}

std::vector<uint8_t> crypto::encrypt(const std::vector<uint8_t>& input, const std::vector<uint8_t>& key)
{
	std::vector<uint8_t> result;
	aes256::encrypt(key, input, result);
	return result;
}

std::vector<uint8_t> crypto::decrypt(const df::cspan input, const std::string_view password)
{
	if (input.size < HMAC_SIZE)
		return {};

	uint8_t enc_key[sha256::DIGEST_SIZE];
	uint8_t mac_key[sha256::DIGEST_SIZE];
	derive_keys(password, enc_key, mac_key);

	// Verify HMAC before decrypting (Encrypt-then-MAC)
	const size_t ciphertext_len = input.size - HMAC_SIZE;
	const auto expected_hmac = hmac_sha256_raw(mac_key, sizeof(mac_key), input.data, ciphertext_len);
	platform::secure_zero(mac_key, sizeof(mac_key));

	const uint8_t* actual_hmac = input.data + ciphertext_len;
	uint8_t diff = 0;
	for (size_t i = 0; i < HMAC_SIZE; ++i)
		diff |= expected_hmac[i] ^ actual_hmac[i];

	if (diff != 0)
	{
		platform::secure_zero(enc_key, sizeof(enc_key));
		return {}; // Authentication failed
	}

	const std::vector<uint8_t> key(enc_key, enc_key + sha256::DIGEST_SIZE);
	platform::secure_zero(enc_key, sizeof(enc_key));

	std::vector<uint8_t> result;
	aes256::decrypt(key, {input.data, ciphertext_len}, result);
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

uint32_t crypto::crc32c(const uint32_t crc, const void* data, const size_t len)
{
	if (platform::crc32_supported)
	{
		return calc_crc32c_x86(crc, data, len);
	}

	if (platform::arm_crc32_supported)
	{
		return calc_crc32c_arm(crc, data, len);
	}

	return calc_crc32c_c(crc, data, len);
}

uint32_t crypto::crc32c(const std::string_view sv)
{
	return crc32c(sv.data(), sv.size());
}

///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////
// Perceptual hash

namespace
{
	// Side of the low-frequency DCT block kept as the hash. 8x8 is 64 coefficients, one of which is
	// the DC term the median threshold discards, so the hash is 63 meaningful bits in a 64-bit word.
	constexpr size_t phash_block = 8;

	// Below this spread across the kept coefficients the picture has no structure to identify it,
	// only noise around a flat field, and any hash of it collides with every other flat field.
	constexpr double phash_min_deviation = 1.0;

	// The DCT-II basis is separable and the input extent is fixed, so the cosine table is built once
	// rather than per image: this is called on candidate pairs, not on the whole collection.
	const auto& phash_basis()
	{
		static const auto table = []
		{
			auto result = std::make_unique<std::array<double, crypto::phash_extent * phash_block>>();

			for (size_t u = 0; u < phash_block; ++u)
			{
				for (size_t x = 0; x < crypto::phash_extent; ++x)
				{
					(*result)[u * crypto::phash_extent + x] =
						std::cos((2.0 * x + 1.0) * u * M_PI / (2.0 * crypto::phash_extent));
				}
			}

			return result;
		}();

		return *table;
	}
}

uint64_t crypto::perceptual_hash(const uint8_t* gray, const size_t len)
{
	if (gray == nullptr || len < phash_pixels)
	{
		return 0;
	}

	const auto& basis = phash_basis();

	// Rows first, then columns: separating the 2D transform turns 32*32*8*8 products into 2*32*32*8.
	std::array<double, phash_extent * phash_block> rows{};

	for (size_t y = 0; y < phash_extent; ++y)
	{
		for (size_t u = 0; u < phash_block; ++u)
		{
			double sum = 0.0;
			for (size_t x = 0; x < phash_extent; ++x)
			{
				sum += gray[y * phash_extent + x] * basis[u * phash_extent + x];
			}
			rows[y * phash_block + u] = sum;
		}
	}

	std::array<double, phash_block * phash_block> coefficients{};

	for (size_t u = 0; u < phash_block; ++u)
	{
		for (size_t v = 0; v < phash_block; ++v)
		{
			double sum = 0.0;
			for (size_t y = 0; y < phash_extent; ++y)
			{
				sum += rows[y * phash_block + v] * basis[u * phash_extent + y];
			}
			coefficients[u * phash_block + v] = sum;
		}
	}

	// The DC term carries overall brightness, which is exactly what a re-encode or an exposure tweak
	// changes, so it is excluded from the threshold rather than allowed to dominate it.
	std::array<double, phash_block * phash_block - 1> ranked{};
	std::copy(coefficients.begin() + 1, coefficients.end(), ranked.begin());

	const auto middle = ranked.begin() + ranked.size() / 2;
	std::nth_element(ranked.begin(), middle, ranked.end());
	const auto median = *middle;

	double deviation = 0.0;
	for (size_t i = 1; i < coefficients.size(); ++i)
	{
		deviation += std::abs(coefficients[i] - median);
	}

	if ((deviation / (coefficients.size() - 1)) < phash_min_deviation)
	{
		return 0;
	}

	uint64_t result = 0;

	for (size_t i = 1; i < coefficients.size(); ++i)
	{
		if (coefficients[i] > median)
		{
			result |= 1ull << i;
		}
	}

	// A structured image can still land on an all-zero pattern. Bit 0 is reserved, so 2 is the
	// smallest value that reads as a real hash.
	return result == 0 ? 2ull : result;
}

namespace
{
	// A quarter turn clockwise: the value at row r, column c moves to row c, column N-1-r.
	void rotate_quarter_turn(std::array<uint8_t, crypto::phash_pixels>& grid)
	{
		constexpr auto n = crypto::phash_extent;
		std::array<uint8_t, crypto::phash_pixels> rotated{};

		for (size_t r = 0; r < n; ++r)
		{
			for (size_t c = 0; c < n; ++c)
			{
				rotated[c * n + (n - 1 - r)] = grid[r * n + c];
			}
		}

		grid = rotated;
	}
}

crypto::phash_rotations crypto::perceptual_hash_rotations(const uint8_t* gray, const size_t len)
{
	phash_rotations result{};

	if (gray == nullptr || len < phash_pixels)
	{
		return result;
	}

	std::array<uint8_t, phash_pixels> grid{};
	std::copy_n(gray, phash_pixels, grid.begin());

	for (auto& hash : result)
	{
		hash = perceptual_hash(grid.data(), grid.size());
		rotate_quarter_turn(grid);
	}

	// The detail test is applied per orientation, so a picture near the floor could speak in one
	// turn and decline in another. Answering only when every orientation agrees keeps the result
	// from depending on which way round the file happened to be saved.
	if (std::ranges::any_of(result, [](const uint64_t h) { return !phash_is_usable(h); }))
	{
		result.fill(0);
	}

	return result;
}

static constexpr uint32_t FNV_PRIME_32 = 16777619u;
static constexpr uint32_t OFFSET_BASIS_32 = 2166136261u;

uint32_t crypto::fnv1a_i(const std::string_view sv)
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
