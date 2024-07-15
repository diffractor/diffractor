// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

// Using the excellent 'The Parallel Hashmap'
// From Gregory Popovitch
// https://github.com/greg7mdp/parallel-hashmap
#include <parallel_hashmap/phmap.h>
#include <parallel_hashmap/btree.h>

#include "crypto.h"
#include "util_path.h"

namespace prop
{
	class key;
}

namespace df
{
	template <typename K, typename V, typename H = std::hash<K>, typename E = std::equal_to<K>>
	using hash_map = std::unordered_map<K, V, H, E>;

	template <typename K, typename V, typename H = std::hash<K>, typename E = std::equal_to<K>>
	using dense_hash_map = phmap::parallel_flat_hash_map<K, V, H, E>;

	template <typename V, typename H = std::hash<V>, typename E = std::equal_to<V>>
	using hash_set = std::unordered_set<V, H, E>;

	template <typename V, typename H = std::hash<V>, typename E = std::equal_to<V>>
	using dense_hash_set = phmap::parallel_flat_hash_set<V, H, E>;

	struct ihash
	{
		size_t operator()(const file_path path) const
		{
			return crypto::fnv1a_i(path.folder().text(), path.name());
		}

		size_t operator()(const folder_path path) const
		{
			return crypto::fnv1a_i(path.text());
		}

		size_t operator()(const std::u8string_view s) const
		{
			return crypto::fnv1a_i(s);
		}

		size_t operator()(const std::string_view s) const
		{
			return crypto::fnv1a_i(s);
		}
	};

	struct iless
	{
		bool operator()(const file_path l, const file_path r) const
		{
			return l.icmp(r) < 0;
		}

		bool operator()(const std::u8string_view l, const std::u8string_view r) const
		{
			return str::icmp(l, r) < 0;
		}

		bool operator()(const folder_path l, const folder_path r) const
		{
			return l.compare(r) < 0;
		}
	};

	struct ieq
	{
		bool operator()(const file_path l, const file_path r) const
		{
			return l.icmp(r) == 0;
		}

		bool operator()(const folder_path l, const folder_path r) const
		{
			return l.compare(r) == 0;
		}

		bool operator()(const std::u8string_view l, const std::u8string_view r) const
		{
			const auto ll = l.size();
			const auto rl = r.size();
			if (ll != rl) return false;
			if (ll == 0) return true;
			return str::icmp(l, r) == 0;
		}

		bool operator()(const std::wstring_view l, const std::wstring_view r) const
		{
			return str::icmp(l, r) == 0;
		}

		bool operator()(const std::string_view l, const std::string_view r) const
		{
			return str::icmp(l, r) == 0;
		}
	};

	struct hash
	{
		uint32_t operator()(const std::u8string_view r) const
		{
			return crypto::crc32c(r.data(), r.size());
		}
	};

	struct eq
	{
		/*bool operator()(const char8_t* l, const char8_t* r) const
		{
			return strcmp(l, r) == 0;
		}*/

		bool operator()(const std::u8string_view l, const std::u8string_view r) const
		{
			return l.compare(r) == 0;
		}
	};

	using string_map = hash_map<std::u8string, std::u8string, ihash, ieq>;
	using string_counts = hash_map<std::u8string_view, int_counter, ihash, ieq>;
	using dense_string_counts = dense_hash_map<std::u8string_view, int_counter, ihash, ieq>;
	using file_path_counts = hash_map<file_path, int_counter, ihash, ieq>;
	using folder_counts = hash_map<folder_path, int_counter, ihash, ieq>;
	using unique_folders = hash_set<folder_path, ihash, ieq>;
	using unique_paths = hash_set<file_path, ihash, ieq>;
	using unique_strings = hash_set<str::cached, ihash, ieq>;
	using dense_unique_strings = dense_hash_set<str::cached, ihash, ieq>;

	struct index_roots
	{
		unique_folders folders;
		unique_paths files;
		unique_folders excludes;
		unique_strings exclude_wildcards;

		index_roots() noexcept = default;
		index_roots(const index_roots&) = default;
		index_roots& operator=(const index_roots&) = default;
		index_roots(index_roots&&) noexcept = default;
		index_roots& operator=(index_roots&&) noexcept = default;
	};

	bool is_excluded(const index_roots &roots, const df::folder_path path);
};
