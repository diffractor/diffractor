// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

namespace crypto
{
	constexpr uint32_t CRCINIT = 0xFFFFFFFFu;

	std::u8string hmac_sha1(std::u8string_view key_bytes, std::u8string_view data);
	uint32_t crc32c(const void* data, size_t len);
	uint32_t crc32c(uint32_t crc, const void* data, size_t len);
	uint32_t crc32c(std::u8string_view sv);
	uint32_t fnv1a(const void* data, size_t len);
	uint32_t fnv1a_i(std::u8string_view sv);
	uint32_t fnv1a_i(std::string_view sv);
	uint32_t fnv1a_i(std::u8string_view sv1, std::u8string_view sv2);

	std::vector<uint8_t> encrypt(df::cspan cs, std::u8string_view password);
	std::vector<uint8_t> decrypt(df::cspan cs, std::u8string_view password);

	std::vector<uint8_t> encrypt(const std::vector<uint8_t>& text, const std::vector<uint8_t>& key);

	inline std::vector<uint8_t> encrypt(const std::vector<uint8_t>& s, const std::u8string_view password)
	{
		return encrypt({s.data(), s.size()}, password);
	}

	inline std::vector<uint8_t> encrypt(const std::u8string_view s, const std::u8string_view password)
	{
		return encrypt({std::bit_cast<const uint8_t*>(s.data()), s.size()}, password);
	}

	inline std::vector<uint8_t> decrypt(const std::vector<uint8_t>& data, const std::u8string_view password)
	{
		return decrypt({(data.data()), data.size()}, password);
	}

	class hash_gen
	{
		const uint32_t FNV_32_PRIME = 0x01000193u;
		const uint32_t FNV_32_INIT = 0x811c9dc5u;
		uint32_t h = FNV_32_INIT;

	public:
		hash_gen()
		{
		}

		hash_gen(const std::u8string_view sv)
		{
			append(sv);
		}

		hash_gen& append(const std::u8string_view sv)
		{
			auto p = sv.begin();

			while (p < sv.end())
			{
				h ^= str::to_lower(str::pop_utf8_char(p, sv.end()));
				h *= FNV_32_PRIME;
			}

			return *this;
		}

		hash_gen& append(const int n)
		{
			h ^= n;
			h *= FNV_32_PRIME;
			return *this;
		}

		hash_gen& append(const uint32_t n)
		{
			h ^= n;
			h *= FNV_32_PRIME;
			return *this;
		}

		hash_gen& append(const uint64_t n)
		{
			h ^= n;
			h *= FNV_32_PRIME;
			return *this;
		}

		uint32_t result() const
		{
			return h;
		}
	};
}
