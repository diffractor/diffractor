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

static void derive_keys(const std::u8string_view password, uint8_t enc_key[32], uint8_t mac_key[32])
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


std::vector<uint8_t> crypto::encrypt(const df::cspan input, const std::u8string_view password)
{
	uint8_t enc_key[sha256::DIGEST_SIZE];
	uint8_t mac_key[sha256::DIGEST_SIZE];
	derive_keys(password, enc_key, mac_key);

	const std::vector<uint8_t> key(enc_key, enc_key + sha256::DIGEST_SIZE);
	platform::secure_zero(enc_key, sizeof(enc_key));

	std::vector<uint8_t> result;
	aes256::encrypt(key, input, result);

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

std::vector<uint8_t> crypto::decrypt(const df::cspan input, const std::u8string_view password)
{
	if (input.size < HMAC_SIZE)
		return {};

	uint8_t enc_key[sha256::DIGEST_SIZE];
	uint8_t mac_key[sha256::DIGEST_SIZE];
	derive_keys(password, enc_key, mac_key);

	// Verify HMAC before decrypting (Encrypt-then-MAC)
	const size_t ciphertext_len = input.size - HMAC_SIZE;
	auto expected_hmac = hmac_sha256_raw(mac_key, sizeof(mac_key), input.data, ciphertext_len);
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
