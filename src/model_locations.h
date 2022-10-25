// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

#include "util_kdtree.h"
#include "model_location.h"

class database;
class db_statement;

static constexpr auto max_country_alt_names = 16;

constexpr uint32_t to_code2(const std::u8string_view s)
{
	uint32_t result = 0;
	for (const auto c : s) result = (result << 8) | str::to_upper(c);
	return result;
}

struct country_loc
{
	uint32_t code;
	str::cached name;
	gps_coordinate centroid;
};

str::cached normalize_county_abbreviation(str::cached country);
str::cached normalize_county_name(str::cached country);

class country_t
{
private:
	char8_t _code[3]{};
	str::cached _name;
	std::vector<str::cached> _alt_names;
	df::hash_map<uint32_t, str::cached> _states;
	gps_coordinate _centroid;

public:
	static country_t null;

	country_t() = default;
	~country_t() noexcept = default;
	country_t(const country_t&) = default;
	country_t& operator=(const country_t&) = default;
	country_t(country_t&&) noexcept = default;
	country_t& operator=(country_t&&) noexcept = default;

	country_t(const std::u8string_view code, const str::cached name,
	          std::vector<str::cached> alt_names) noexcept : _name(name), _alt_names(std::move(alt_names))
	{
		_code[0] = code[0];
		_code[1] = code[1];
		_code[2] = 0;
	}

	void state(const std::u8string_view code, const str::cached name)
	{
		_states[to_code2(code)] = name;
	}

	std::u8string_view code() const
	{
		return {_code, 2};
	}

	uint32_t code2() const
	{
		return to_code2(_code);
	}

	const str::cached name() const
	{
		return _name;
	}

	const std::vector<str::cached>& alt_names() const
	{
		return _alt_names;
	}

	str::cached state(const uint32_t code) const
	{
		const auto found = _states.find(code);
		if (found != _states.cend()) return found->second;
		return {};
	}

	const gps_coordinate& centroid() const
	{
		return _centroid;
	}

	bool operator<(const country_t& other) const
	{
		return icmp(_name, other._name) < 0;
	}

	friend bool operator==(const country_t& lhs, const country_t& rhs)
	{
		return icmp(lhs._name, rhs._name) == 0;
	}

	friend bool operator!=(const country_t& lhs, const country_t& rhs)
	{
		return !(lhs == rhs);
	}

	friend class location_cache;
};

struct csv_entry;

struct location_match_part
{
	str::cached text;
	std::vector<str::part_t> highlights;
};

struct location_match
{
	location_match_part city;
	location_match_part state;
	location_match_part country;
	double distance_away{};
	location_t location;
};

using location_matches = std::vector<location_match>;

class location_cache final : public df::no_copy
{
private:
	mutable platform::mutex _rw;
	_Guarded_by_(_rw)  kd_tree _tree;
	_Guarded_by_(_rw)  df::hash_map<uint32_t, country_t> _countries;
	_Guarded_by_(_rw)  const df::file_path _locations_path;

	struct location_id_and_offset
	{
		uint32_t id;
		uint32_t offset;

		bool operator<(const location_id_and_offset& other) const
		{
			return id < other.id;
		}
	};

	struct ngram_t
	{
		const static int depth = 4;
		uint8_t text[depth];

		ngram_t() noexcept = default;
		~ngram_t() noexcept = default;
		ngram_t(const ngram_t&) = default;
		ngram_t& operator=(const ngram_t&) = default;
		ngram_t(ngram_t&&) noexcept = default;
		ngram_t& operator=(ngram_t&&) noexcept = default;

		explicit ngram_t(const std::u8string_view r) noexcept
		{
			text[0] = 0;

			auto* p_out = text;
			const auto* const p_out_limit = text + depth;

			auto p_in = r.begin();
			const auto p_in_limit = r.end();

			while (p_out < p_out_limit && p_in < p_in_limit)
			{
				*p_out++ = str::normalze_for_compare(str::pop_utf8_char(p_in, p_in_limit));
			}

			while (p_out < p_out_limit)
			{
				*p_out++ = 0;
			}

			//df::assert_true(text[0] != 0);
		}

		bool is_possible_match(const ngram_t& other) const
		{
			return other.text[0] == text[0] &&
				(other.text[1] == 0 || other.text[1] == text[1]) &&
				(other.text[2] == 0 || other.text[2] == text[2]) &&
				(other.text[3] == 0 || other.text[3] == text[3]);
		}

		int cmp(const ngram_t& other) const noexcept
		{
			return memcmp(text, other.text, 4);
		}
	};

	struct location_ngram_and_offset
	{
		ngram_t ngram;
		uint32_t offset;

		location_ngram_and_offset() noexcept = default;
		~location_ngram_and_offset() noexcept = default;
		location_ngram_and_offset(const location_ngram_and_offset&) = default;
		location_ngram_and_offset& operator=(const location_ngram_and_offset&) = default;
		location_ngram_and_offset(location_ngram_and_offset&&) noexcept = default;
		location_ngram_and_offset& operator=(location_ngram_and_offset&&) noexcept = default;

		location_ngram_and_offset(const std::u8string_view r, uint32_t off) noexcept : ngram(r), offset(off)
		{
		}

		location_ngram_and_offset(const ngram_t& n, uint32_t off) noexcept : ngram(n), offset(off)
		{
		}

		bool operator<(const location_ngram_and_offset& other) const
		{
			return ngram.cmp(other.ngram) < 0;
		}
	};

	_Guarded_by_(_rw) std::vector<kd_coordinates_t> _coords;
	_Guarded_by_(_rw) std::vector<location_id_and_offset> _locations_by_id;
	_Guarded_by_(_rw) std::vector<location_ngram_and_offset> _locations_by_ngram;

	void load_countries();
	void load_states();

	static int scan_entries(std::u8string_view line, csv_entry* entries);
	int scan_entries(u8istream& file, std::u8string& line, std::streamoff offset, csv_entry* entries) const;

	location_t build_location(u8istream& file, int offset) const;
	location_t build_location(const csv_entry* entries) const;

public:
	location_cache();
	~location_cache() = default;

	void load_index();

	bool is_index_loaded() const
	{
		platform::shared_lock lock(_rw);
		return !_tree.is_empty();
	}

	country_loc find_country(double x, double y) const;
	location_t find_closest(double x, double y) const;
	location_t find_by_id(uint32_t id) const;

	location_matches auto_complete(std::u8string_view query, uint32_t max_results,
	                               gps_coordinate default_location) const;

	const country_t& find_country(const uint32_t code) const
	{
		platform::shared_lock lock(_rw);
		const auto found = _countries.find(code);
		return (found != _countries.cend()) ? found->second : country_t::null;
	}
};
