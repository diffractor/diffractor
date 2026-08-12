// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Hash map and set templates. Provides custom hash containers
// optimized for file paths, strings, and other common key types.

#pragma once

#include <algorithm>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

	// 'dense_*' historically used a flat/parallel hash map. The interning pool (the one place
	// that genuinely needed a dense, cache-friendly table for a very large number of strings)
	// now has its own purpose-built append-only table, so the general containers use the
	// standard library. The distinct alias is kept so intent is documented and a specialised
	// implementation can be reintroduced in one place if a specific case ever needs it.
	template <typename K, typename V, typename H = std::hash<K>, typename E = std::equal_to<K>>
	using dense_hash_map = std::unordered_map<K, V, H, E>;

	template <typename V, typename H = std::hash<V>, typename E = std::equal_to<V>>
	using hash_set = std::unordered_set<V, H, E>;

	template <typename V, typename H = std::hash<V>, typename E = std::equal_to<V>>
	using dense_hash_set = std::unordered_set<V, H, E>;

	struct ihash
	{
		// Both parts are interned, so their case-insensitive hashes are already known.
		size_t operator()(const file_path path) const
		{
			const auto folder = path.folder().text().ihash();
			const auto name = path.name().ihash();
			return folder ^ (name + 0x9e3779b9u + (folder << 6) + (folder >> 2));
		}

		size_t operator()(const folder_path path) const
		{
			return path.text().ihash();
		}

		size_t operator()(const str::cached s) const
		{
			return s.ihash();
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

		bool operator()(const std::string_view l, const std::string_view r) const
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

		bool operator()(const std::string_view l, const std::string_view r) const
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
	};

	struct hash
	{
		uint32_t operator()(const std::string_view r) const
		{
			return crypto::crc32c(r.data(), r.size());
		}
	};

	struct eq
	{
		bool operator()(const std::string_view l, const std::string_view r) const
		{
			return l.compare(r) == 0;
		}
	};

	using string_map = hash_map<std::string, std::string, ihash, ieq>;
	using string_counts = hash_map<std::string_view, int_counter, ihash, ieq>;
	using dense_string_counts = dense_hash_map<std::string_view, int_counter, ihash, ieq>;
	using file_path_counts = hash_map<file_path, int_counter, ihash, ieq>;
	using folder_counts = hash_map<folder_path, int_counter, ihash, ieq>;
	using unique_folders = hash_set<folder_path, ihash, ieq>;
	using unique_paths = hash_set<file_path, ihash, ieq>;
	using unique_strings = hash_set<str::cached, ihash, ieq>;
	using dense_unique_strings = dense_hash_set<str::cached, ihash, ieq>;

	// Groups a flat sequence by the folder each element belongs to, replacing
	// hash_map<folder_path, std::vector<T>>.
	//
	// That map costs a node plus a separately grown vector per folder and copies every element into
	// its bucket. This holds two contiguous arrays - one index per element and one record per folder -
	// so nothing is copied and nothing is allocated per group.
	//
	// Groups appear in first-seen order and elements keep their input order within a group.
	class folder_groups
	{
	public:
		struct group
		{
			folder_path folder;
			uint32_t first = 0;
			uint32_t count = 0;
		};

		struct include_all
		{
			template <typename T>
			bool operator()(const T&) const { return true; }
		};

		template <typename Range, typename FolderOf, typename Include = include_all>
		void build(const Range& elements, FolderOf folder_of, Include include = {})
		{
			clear();

			const auto n = static_cast<uint32_t>(std::size(elements));
			_group_of.assign(n, 0);

			uint32_t i = 0;

			for (const auto& element : elements)
			{
				if (include(element))
				{
					const auto g = find_or_add(folder_of(element));
					_group_of[i] = g + 1;
					_groups[g].count += 1;
				}

				++i;
			}

			uint32_t next = 0;

			for (auto& g : _groups)
			{
				g.first = next;
				next += g.count;
				g.count = 0; // refilled by the scatter below
			}

			_order.resize(next);

			for (i = 0; i < n; ++i)
			{
				const auto g = _group_of[i];

				if (g != 0)
				{
					auto& rec = _groups[g - 1];
					_order[rec.first + rec.count] = i;
					rec.count += 1;
				}
			}
		}

		const std::vector<group>& groups() const { return _groups; }
		bool empty() const { return _groups.empty(); }

		// Indices into the sequence build() was given. Valid until the next build().
		std::span<const uint32_t> elements(const group& g) const
		{
			return {_order.data() + g.first, g.count};
		}

		void clear()
		{
			_groups.clear();
			_order.clear();
			_group_of.clear();
			std::fill(_table.begin(), _table.end(), slot{});
			_table_count = 0;
		}

	private:
		struct slot
		{
			uint32_t key = 0; // 0 marks an empty slot
			uint32_t group = 0;
		};

		std::vector<group> _groups;
		std::vector<uint32_t> _order;
		std::vector<uint32_t> _group_of; // per element, group + 1; 0 when excluded
		std::vector<slot> _table;
		uint32_t _table_count = 0;

		// FNV-1a avalanches poorly in the low bits the mask selects.
		static constexpr uint32_t mix(uint32_t x)
		{
			x ^= x >> 16;
			x *= 0x7feb352du;
			x ^= x >> 15;
			x *= 0x846ca68bu;
			x ^= x >> 16;
			return x;
		}

		uint32_t find_or_add(const folder_path folder)
		{
			auto key = static_cast<uint32_t>(ihash{}(folder));
			if (key == 0) key = 1;

			if (!_table.empty())
			{
				const auto mask = static_cast<uint32_t>(_table.size()) - 1;
				auto i = mix(key) & mask;

				while (_table[i].key != 0)
				{
					// Interning is case sensitive, so a second spelling of one folder is a distinct
					// handle with the same case-insensitive hash, and has to compare equal here.
					if (_table[i].key == key && ieq{}(_groups[_table[i].group].folder, folder))
					{
						return _table[i].group;
					}

					i = (i + 1) & mask;
				}
			}

			const auto g = static_cast<uint32_t>(_groups.size());
			_groups.push_back({folder, 0, 0});
			insert(key, g);
			return g;
		}

		void insert(const uint32_t key, const uint32_t group)
		{
			if (_table.empty() || (static_cast<size_t>(_table_count) + 1) * 4 >= _table.size() * 3) grow();

			const auto mask = static_cast<uint32_t>(_table.size()) - 1;
			auto i = mix(key) & mask;
			while (_table[i].key != 0) i = (i + 1) & mask;
			_table[i] = {key, group};
			++_table_count;
		}

		void grow()
		{
			std::vector<slot> next(_table.empty() ? 16u : _table.size() * 2);
			const auto mask = static_cast<uint32_t>(next.size()) - 1;

			for (const auto& s : _table)
			{
				if (s.key == 0) continue;
				auto i = mix(s.key) & mask;
				while (next[i].key != 0) i = (i + 1) & mask;
				next[i] = s;
			}

			_table.swap(next);
		}
	};

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

	bool is_excluded(const index_roots& roots, folder_path path);
};
