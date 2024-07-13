// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once
#include "model_propery.h"
#include "model_location.h"
#include "files.h"

struct search_part;

namespace df
{
	enum class search_result_type
	{
		no_match,
		similar,
		match_prop,
		match_text,
		has_type,
		match_multiple,
		match_flag,
		match_label,
		match_file_group,
		match_date,
		match_folder,
		match_ext,
		match_location,
	};

	enum class search_term_type
	{
		empty,
		text,
		extension,
		value,
		has_type,
		media_type,
		date,
		location,
		duplicate,
	};

	struct search_result
	{
		search_result_type type = search_result_type::no_match;
		prop::key_ref key = prop::null;
		str::cached text;

		bool is_match() const
		{
			return type != search_result_type::no_match;
		}

		bool is_match(const prop::key_ref find_key) const
		{
			if (type == search_result_type::match_location)
			{
				return find_key == prop::longitude ||
					find_key == prop::latitude ||
					find_key == prop::location_country ||
					find_key == prop::location_place ||
					find_key == prop::location_state;
			}

			return find_key == key;
		}

		bool is_match(const prop::key_ref find_key, const std::u8string_view find_text) const
		{
			return find_key == key && ifind(text, find_text) != std::u8string_view::npos;
		}

		search_result_type merge_result_type(const search_result_type& existing) const
		{
			if (type == existing || existing == search_result_type::no_match)
			{
				return type;
			}

			return type == search_result_type::no_match
				       ? search_result_type::no_match
				       : search_result_type::match_multiple;
		}
	};

	inline bool is_probably_selector(const std::u8string_view text)
	{
		return item_selector::can_iterate(text) &&
			text.find(u8" #"sv) == std::u8string::npos &&
			text.find(u8" @"sv) == std::u8string::npos &&
			text.find(u8" *"sv) == std::u8string::npos &&
			text.find(u8"** "sv) == std::u8string::npos &&
			text.find(u8"**\\ "sv) == std::u8string::npos &&
			text.find(u8"**/ "sv) == std::u8string::npos &&
			text.find(u8"\\ "sv) == std::u8string::npos &&
			text.find(u8"/ "sv) == std::u8string::npos;
	}

	enum class search_term_modifier_bool
	{
		m_and,
		m_or,
		none
	};

	struct search_term_modifier
	{
		bool positive = true;
		search_term_modifier_bool logical_op = search_term_modifier_bool::none;
		bool less_than = false;
		bool greater_than = false;
		bool equals = false;
		int begin_group = 0;
		int end_group = 0;

		search_term_modifier() noexcept = default;
		search_term_modifier& operator=(const search_term_modifier&) noexcept = default;
		search_term_modifier& operator=(search_term_modifier&&) noexcept = default;
		search_term_modifier(const search_term_modifier&) noexcept = default;
		search_term_modifier(search_term_modifier&&) noexcept = default;

		search_term_modifier(bool pos) noexcept : positive(pos)
		{
		}

		search_term_modifier(bool pos, bool fuz) noexcept : positive(pos)
		{
		}

		bool is_defaults() const
		{
			return positive &&
				logical_op == search_term_modifier_bool::none &&
				!less_than &&
				!greater_than &&
				!equals &&
				begin_group == 0 &&
				end_group == 0;
		}

		friend bool operator==(const search_term_modifier& lhs, const search_term_modifier& rhs)
		{
			return lhs.positive == rhs.positive
				&& lhs.logical_op == rhs.logical_op
				&& lhs.less_than == rhs.less_than
				&& lhs.greater_than == rhs.greater_than
				&& lhs.equals == rhs.equals
				&& lhs.begin_group == rhs.begin_group
				&& lhs.end_group == rhs.end_group;
		}

		friend bool operator!=(const search_term_modifier& lhs, const search_term_modifier& rhs)
		{
			return !(lhs == rhs);
		}

		friend bool operator<(const search_term_modifier& lhs, const search_term_modifier& rhs)
		{
			if (lhs.positive < rhs.positive)
				return true;
			if (rhs.positive < lhs.positive)
				return false;
			if (lhs.logical_op < rhs.logical_op)
				return true;
			if (rhs.logical_op < lhs.logical_op)
				return false;
			if (lhs.less_than < rhs.less_than)
				return true;
			if (rhs.less_than < lhs.less_than)
				return false;
			if (lhs.greater_than < rhs.greater_than)
				return true;
			if (rhs.greater_than < lhs.greater_than)
				return false;
			if (lhs.equals < rhs.equals)
				return true;
			if (rhs.equals < lhs.equals)
				return false;
			if (lhs.begin_group < rhs.begin_group)
				return true;
			if (rhs.begin_group < lhs.begin_group)
				return false;
			return lhs.end_group < rhs.end_group;
		}

		friend bool operator<=(const search_term_modifier& lhs, const search_term_modifier& rhs)
		{
			return !(rhs < lhs);
		}

		friend bool operator>(const search_term_modifier& lhs, const search_term_modifier& rhs)
		{
			return rhs < lhs;
		}

		friend bool operator>=(const search_term_modifier& lhs, const search_term_modifier& rhs)
		{
			return !(lhs < rhs);
		}

		void clear()
		{
			positive = true;
			logical_op = search_term_modifier_bool::none;
			less_than = false;
			greater_than = false;
			equals = false;
			begin_group = 0;
			end_group = 0;
		}
	};

	struct search_parent;

	enum class date_parts_prop
	{
		any,
		created,
		modified
	};

	struct date_parts
	{
		int age = 0;
		int year = 0;
		int month = 0;
		int day = 0;

		date_parts_prop target = date_parts_prop::any;

		date_parts() noexcept = default;
		date_parts(const date_parts&) noexcept = default;
		date_parts& operator=(const date_parts&) noexcept = default;
		date_parts(date_parts&&) noexcept = default;
		date_parts& operator=(date_parts&&) noexcept = default;

		date_parts(const day_t& dd, date_parts_prop isc = date_parts_prop::any) noexcept : year(dd.year),
			month(dd.month), day(dd.day), target(isc)
		{
		}

		friend bool operator==(const date_parts& lhs, const date_parts& rhs)
		{
			return lhs.age == rhs.age
				&& lhs.year == rhs.year
				&& lhs.month == rhs.month
				&& lhs.day == rhs.day
				&& lhs.target == rhs.target;
		}

		friend bool operator!=(const date_parts& lhs, const date_parts& rhs)
		{
			return !(lhs == rhs);
		}

		friend bool operator<(const date_parts& lhs, const date_parts& rhs)
		{
			if (lhs.age < rhs.age)
				return true;
			if (rhs.age < lhs.age)
				return false;
			if (lhs.year < rhs.year)
				return true;
			if (rhs.year < lhs.year)
				return false;
			if (lhs.month < rhs.month)
				return true;
			if (rhs.month < lhs.month)
				return false;
			if (lhs.day < rhs.day)
				return true;
			if (rhs.day < lhs.day)
				return false;
			return lhs.target < rhs.target;
		}

		friend bool operator<=(const date_parts& lhs, const date_parts& rhs)
		{
			return !(rhs < lhs);
		}

		friend bool operator>(const date_parts& lhs, const date_parts& rhs)
		{
			return rhs < lhs;
		}

		friend bool operator>=(const date_parts& lhs, const date_parts& rhs)
		{
			return !(lhs < rhs);
		}

		bool is_empty() const
		{
			return age == 0 &&
				year == 0 &&
				month == 0 &&
				day == 0;
		}
	};

	struct search_term
	{
		search_term_type type = search_term_type::empty;

		search_term_modifier modifiers;

		prop::key_ref key = prop::null;
		std::u8string text;
		bool _is_wildcard = false;
		int int_val = 0;
		uint64_t int64_val = 0;
		double float_val = 0.0;
		gps_coordinate coord_val;
		xy16 xy_val = {0, 0};
		file_group_ref fg_val = nullptr;
		date_parts date_val;

		search_term() noexcept = default;
		search_term(const search_term&) = default;
		search_term& operator=(const search_term&) = default;
		search_term(search_term&&) noexcept = default;
		search_term& operator=(search_term&&) noexcept = default;

		explicit search_term(const search_term_type tt,
		                     const search_term_modifier mods) noexcept : type(tt), modifiers(mods)
		{
		}

		explicit search_term(const search_term_type tt, const std::u8string_view v,
		                     const search_term_modifier mods) noexcept :
			type(tt),
			modifiers(mods),
			text(v),
			_is_wildcard(str::is_wildcard(text))
		{
		}

		explicit search_term(const search_term_type tt, const date_parts v,
		                     const search_term_modifier mods) noexcept : type(tt), modifiers(mods), date_val(v)
		{
		}

		explicit search_term(const search_term_type tt, const gps_coordinate coord, const double v,
		                     const search_term_modifier mods) noexcept : type(tt), modifiers(mods), float_val(v),
		                                                                 coord_val(coord)
		{
		}

		explicit search_term(const prop::key_ref k, const std::u8string_view v,
		                     const search_term_modifier mods) noexcept :
			type(search_term_type::value),
			modifiers(mods),
			key(k),
			text(v),
			_is_wildcard(str::is_wildcard(text))
		{
		}

		explicit search_term(const prop::key_ref k, const int v, const search_term_modifier mods) noexcept :
			type(search_term_type::value), modifiers(mods), key(k), int_val(v)
		{
		}

		explicit search_term(const prop::key_ref k, const uint32_t v, const search_term_modifier mods) noexcept :
			type(search_term_type::value), modifiers(mods), key(k), int_val(v)
		{
		}

		explicit search_term(const prop::key_ref k, const uint64_t v, const search_term_modifier mods) noexcept :
			type(search_term_type::value), modifiers(mods), key(k), int64_val(v)
		{
		}

		explicit search_term(const prop::key_ref k, const double v, const search_term_modifier mods) noexcept :
			type(search_term_type::value), modifiers(mods), key(k), float_val(v)
		{
		}

		explicit search_term(const prop::key_ref k, const xy16 v, const search_term_modifier mods) noexcept :
			type(search_term_type::value), modifiers(mods), key(k), xy_val(v)
		{
		}

		explicit search_term(const std::u8string_view v, const search_term_modifier mods) noexcept :
			type(search_term_type::text),
			modifiers(mods),
			text(v),
			_is_wildcard(str::is_wildcard(text))
		{
		}

		explicit search_term(const prop::key_ref t, const search_term_modifier mods) noexcept :
			type(search_term_type::has_type), modifiers(mods), key(t)
		{
		}

		explicit search_term(const file_group_ref ft, const search_term_modifier mods) noexcept :
			type(search_term_type::media_type), modifiers(mods), fg_val(ft)
		{
		}

		bool is_empty() const
		{
			return type == search_term_type::empty;
		}

		bool no_modifiers() const
		{
			return modifiers.is_defaults();
		}

		bool is_date() const
		{
			return type == search_term_type::date ||
				(type == search_term_type::value && key->data_type == prop::data_type::date);
		}

		bool is_int() const
		{
			return type == search_term_type::value && key->data_type == prop::data_type::int32;
		}

		bool is_property_term() const
		{
			return type == search_term_type::date ||
				type == search_term_type::text ||
				type == search_term_type::value ||
				type == search_term_type::has_type;
		}

		bool is_property_value() const
		{
			return type == search_term_type::value;
		}

		bool is_property_type() const
		{
			return type == search_term_type::has_type;
		}

		bool is_text() const
		{
			return type == search_term_type::text;
		}

		bool is_media_type() const
		{
			return type == search_term_type::media_type;
		}

		bool needs_metadata() const
		{
			return type == search_term_type::text ||
				type == search_term_type::value ||
				type == search_term_type::has_type ||
				type == search_term_type::date ||
				type == search_term_type::location ||
				type == search_term_type::duplicate;
		}

		friend bool operator==(const search_term& lhs, const search_term& rhs)
		{
			return lhs.type == rhs.type
				&& lhs.modifiers == rhs.modifiers
				&& lhs.key == rhs.key
				&& lhs.text == rhs.text
				&& lhs.int_val == rhs.int_val
				&& lhs.int64_val == rhs.int64_val
				&& equiv(lhs.float_val, rhs.float_val)
				&& lhs.coord_val == rhs.coord_val
				&& lhs.xy_val == rhs.xy_val
				&& lhs.fg_val == rhs.fg_val
				&& lhs.date_val == rhs.date_val;
		}

		friend bool operator!=(const search_term& lhs, const search_term& rhs)
		{
			return !(lhs == rhs);
		}

		friend bool operator<(const search_term& lhs, const search_term& rhs)
		{
			if (lhs.type < rhs.type)
				return true;
			if (rhs.type < lhs.type)
				return false;
			if (lhs.modifiers < rhs.modifiers)
				return true;
			if (rhs.modifiers < lhs.modifiers)
				return false;
			if (lhs.key < rhs.key)
				return true;
			if (rhs.key < lhs.key)
				return false;
			if (lhs.text < rhs.text)
				return true;
			if (rhs.text < lhs.text)
				return false;
			if (lhs._is_wildcard < rhs._is_wildcard)
				return true;
			if (rhs._is_wildcard < lhs._is_wildcard)
				return false;
			if (lhs.int_val < rhs.int_val)
				return true;
			if (rhs.int_val < lhs.int_val)
				return false;
			if (lhs.int64_val < rhs.int64_val)
				return true;
			if (rhs.int64_val < lhs.int64_val)
				return false;
			if (lhs.float_val < rhs.float_val)
				return true;
			if (rhs.float_val < lhs.float_val)
				return false;
			if (lhs.coord_val < rhs.coord_val)
				return true;
			if (rhs.coord_val < lhs.coord_val)
				return false;
			if (lhs.xy_val < rhs.xy_val)
				return true;
			if (rhs.xy_val < lhs.xy_val)
				return false;
			if (lhs.fg_val < rhs.fg_val)
				return true;
			if (rhs.fg_val < lhs.fg_val)
				return false;
			return lhs.date_val < rhs.date_val;
		}

		friend bool operator<=(const search_term& lhs, const search_term& rhs)
		{
			return !(rhs < lhs);
		}

		friend bool operator>(const search_term& lhs, const search_term& rhs)
		{
			return rhs < lhs;
		}

		friend bool operator>=(const search_term& lhs, const search_term& rhs)
		{
			return !(lhs < rhs);
		}
	};

	std::u8string format_term(const search_term& term);


	struct related_info
	{
		file_path path;
		gps_coordinate gps;
		date_t metadata_created;
		date_t file_created;
		str::cached name;
		uint32_t crc32c = 0;
		file_size size;
		file_type_ref ft = nullptr;
		uint32_t group = 0;

		bool is_loaded = false;

		date_t created() const
		{
			return metadata_created.is_valid() ? metadata_created : file_created;
		}

		related_info() noexcept = default;
		related_info(const related_info&) noexcept = default;
		related_info& operator=(const related_info&) noexcept = default;
		related_info(related_info&&) noexcept = default;
		related_info& operator=(related_info&&) noexcept = default;

		friend bool operator==(const related_info& lhs, const related_info& rhs)
		{
			return lhs.path == rhs.path;
		}

		friend bool operator!=(const related_info& lhs, const related_info& rhs)
		{
			return !(lhs == rhs);
		}

		void load(const item_element_ptr& i);
	};

	class search_t
	{
	private:
		std::vector<item_selector> _selectors;
		std::vector<search_term> _terms;
		related_info _related;
		std::u8string _raw;

	public:
		search_t() noexcept = default;
		~search_t() noexcept = default;

		search_t(const search_t&) = default;
		search_t& operator=(const search_t&) = default;
		search_t(search_t&&) noexcept = default;
		search_t& operator=(search_t&&) noexcept = default;

		bool is_empty() const
		{
			return _selectors.empty() &&
				_related.path.is_empty() &&
				_terms.empty();
		}

		bool has_property_terms() const
		{
			return std::ranges::find_if(_terms, [](auto&& v) { return v.is_property_term(); }) != _terms.end();
		}

		const std::vector<search_term>& terms() const
		{
			return _terms;
		}

		bool has_property_filter() const
		{
			return has_property_terms();
		}

		bool is_showing_folder() const
		{
			return _selectors.size() == 1 &&
				!_selectors[0].is_recursive();
		}

		bool has_selector() const
		{
			return !_selectors.empty();
		}

		bool has_recursive_selector() const
		{
			for (const auto& s : _selectors)
			{
				if (s.is_recursive())
				{
					return true;
				}
			}

			return false;
		}

		void remove_recursive()
		{
			for (auto&& s : _selectors)
			{
				if (s.is_recursive())
				{
					s = s.parent();
				}
			}

			_raw.clear();
		}

		bool has_terms() const
		{
			return !_terms.empty();
		}

		bool can_match_folder() const
		{
			for (const auto& t : _terms)
			{
				if (t.type == search_term_type::text && str::contains(t.text, u8"**"sv))
				{
					return true;
				}
			}

			return false;
		}

		bool needs_metadata() const;

		bool has_related() const
		{
			return !_related.path.is_empty();
		}

		search_t& with(const prop::key_ref k, std::u8string_view v)
		{
			_terms.emplace_back(k, v, search_term_modifier(true));
			_raw.clear();
			return *this;
		}

		search_t& with(const search_term& term)
		{
			_terms.emplace_back(term);
			_raw.clear();
			return *this;
		}

		search_t& with(const prop::key_ref k, const int v)
		{
			_terms.emplace_back(k, v, search_term_modifier(true));
			_raw.clear();
			return *this;
		}

		search_t& with(const prop::key_ref k, float v)
		{
			_terms.emplace_back(k, v, search_term_modifier(true));
			_raw.clear();
			return *this;
		}

		search_t& with(const prop::key_ref k, xy16 v)
		{
			_terms.emplace_back(k, v, search_term_modifier(true));
			_raw.clear();
			return *this;
		}

		search_t& with(const prop::key_ref k, uint64_t v)
		{
			_terms.emplace_back(k, v, search_term_modifier(true));
			_raw.clear();
			return *this;
		}

		search_t& without(const prop::key_ref k, const std::u8string_view v)
		{
			_terms.emplace_back(k, v, search_term_modifier(false));
			_raw.clear();
			return *this;
		}

		search_t& without_extension()
		{
			_terms.emplace_back(search_term_type::extension, search_term_modifier(false));
			_raw.clear();
			return *this;
		}

		search_t& with_extension(std::u8string_view v)
		{
			_terms.emplace_back(search_term_type::extension, v, search_term_modifier(true));
			_raw.clear();
			return *this;
		}

		bool has_date() const
		{
			return std::ranges::find_if(_terms, [](auto&& v) { return v.is_date(); }) != _terms.end();
		}

		int year() const
		{
			for (const auto& t : _terms)
			{
				if (t.type == search_term_type::date && t.date_val.year != 0)
				{
					return t.date_val.year;
				}
			}

			return 0;
		}

		bool is_rating_only() const
		{
			return _terms.size() == 1 && _terms[0].type == search_term_type::value && _terms[0].key == prop::rating;
		}

		bool has_rating() const
		{
			return std::ranges::find_if(_terms, [](auto&& v) { return v.key == prop::rating; }) != _terms.end();
		}

		date_parts find_date_parts() const;
		void next_date(bool forward);

		search_t& with(const std::u8string_view a)
		{
			_terms.emplace_back(a, search_term_modifier(true));
			_raw.clear();
			return *this;
		}

		search_t& with(const std::vector<std::u8string>& a)
		{
			for (const auto& s : a) _terms.emplace_back(s, search_term_modifier(true));
			_raw.clear();
			return *this;
		}

		search_t& with(const prop::key_ref t)
		{
			_terms.emplace_back(t, search_term_modifier(true));
			_raw.clear();
			return *this;
		}

		search_t& with(const std::vector<prop::key_ref>& types)
		{
			for (const auto& t : types) _terms.emplace_back(t, search_term_modifier(true));
			_raw.clear();
			return *this;
		}

		search_t& with(const search_term_type flag)
		{
			_terms.emplace_back(flag, search_term_modifier(true));
			_raw.clear();
			return *this;
		}

		search_t& without(const prop::key_ref t)
		{
			_terms.emplace_back(t, search_term_modifier(false));
			_raw.clear();
			return *this;
		}

		search_t& without(const std::vector<prop::key_ref>& types)
		{
			for (const auto& t : types) _terms.emplace_back(t, search_term_modifier(false));
			_raw.clear();
			return *this;
		}

		search_t& fuzzy(const prop::key_ref k, const std::u8string_view v)
		{
			_terms.emplace_back(k, v, search_term_modifier(true, true));
			_raw.clear();
			return *this;
		}

		search_t& fuzzy(const prop::key_ref k, const int v)
		{
			_terms.emplace_back(k, v, search_term_modifier(true, true));
			_raw.clear();
			return *this;
		}

		search_t& fuzzy(const prop::key_ref k, const double v)
		{
			_terms.emplace_back(k, v, search_term_modifier(true, true));
			_raw.clear();
			return *this;
		}

		search_t& remove(const prop::key_ref t)
		{
			std::erase_if(_terms, [t](auto&& v) { return v.key == t; });
			_raw.clear();
			return *this;
		}

		search_t& age(const int a, const date_parts_prop target = date_parts_prop::any)
		{
			date_parts d;
			d.target = target;
			d.age = a;
			_terms.emplace_back(search_term_type::date, d, search_term_modifier(true));
			_raw.clear();
			return *this;
		}

		search_t& year(const int y, const date_parts_prop target = date_parts_prop::any)
		{
			date_parts d;
			d.target = target;
			d.year = y;
			_terms.emplace_back(search_term_type::date, d, search_term_modifier(true));
			_raw.clear();
			return *this;
		}

		search_t& month(int m, const date_parts_prop target = date_parts_prop::any)
		{
			date_parts d;
			d.target = target;
			d.month = m;
			_terms.emplace_back(search_term_type::date, d, search_term_modifier(true));
			_raw.clear();
			return *this;
		}

		search_t& day(const int dd, const int m, const int y, const date_parts_prop target = date_parts_prop::any)
		{
			date_parts d;
			d.target = target;
			d.year = y;
			d.month = m;
			d.day = dd;
			_terms.emplace_back(search_term_type::date, d, search_term_modifier(true));
			_raw.clear();
			return *this;
		}

		search_t& location(const gps_coordinate coord, const double km)
		{
			_terms.emplace_back(search_term_type::location, coord, km, search_term_modifier(true));
			_raw.clear();
			return *this;
		}

		search_t& clear_selectors()
		{
			_selectors.clear();
			_raw.clear();
			return *this;
		}

		search_t& clear_terms()
		{
			_terms.clear();
			_raw.clear();
			return *this;
		}

		search_t& add_selector(const item_selector& f)
		{
			_selectors.emplace_back(f);
			_raw.clear();
			return *this;
		}

		search_t& add_selector(const std::u8string_view sv)
		{
			_selectors.emplace_back(item_selector(sv));
			_raw.clear();
			return *this;
		}

		search_t& related(const related_info& r)
		{
			_related = r;
			_raw.clear();
			return *this;
		}

		bool has_term_type(const search_term_type& tt) const
		{
			return std::ranges::find_if(_terms, [tt](const search_term& t) { return t.type == tt; }) != _terms.end();
		}

		bool has_term_value_type(const prop::key_ref tt) const
		{
			return std::ranges::find_if(_terms, [tt](const search_term& t)
			{
				return t.is_property_value() && t.key == tt;
			}) != _terms.end();
		}

		void clear_term_type(const search_term_type& tt)
		{
			std::erase_if(_terms, [tt](auto&& t) { return t.type == tt; });
			_raw.clear();
		}

		void clear_media_type()
		{
			clear_term_type(search_term_type::media_type);
			_raw.clear();
		}

		search_t& add_media_type(const file_group_ref ft)
		{
			const auto found = std::ranges::find_if(_terms, [ft](auto&& t) { return t.is_media_type(); });

			if (found == _terms.end())
			{
				_terms.emplace_back(ft, search_term_modifier(true));
			}
			else
			{
				found->fg_val = ft;
				found->modifiers = search_term_modifier(true);
			}

			_raw.clear();
			return *this;
		}

		bool has_media_type() const { return has_term_type(search_term_type::media_type); }

		bool is_duplicates() const { return has_term_type(search_term_type::duplicate); }

		void clear_date_properties();

		const std::vector<item_selector>& selectors() const
		{
			return _selectors;
		}

		const related_info& related() const
		{
			return _related;
		}

		bool contains(const prop::key_ref k, const std::u8string_view v) const
		{
			return std::find_if(_terms.begin(), _terms.end(), [k, v](auto&& t) { return t.key == k && t.text == v; }) !=
				_terms.end();
		}

		bool is_match(const prop::key& key, date_t date) const;
		bool is_match(const prop::key& key, int val) const;

		bloom_bits calc_bloom_bits() const;

		void parse_part(const struct search_part& part);

		search_t parse_from_input(std::u8string_view text) const;
		static search_t parse_path(std::u8string_view text);
		static search_t parse(std::u8string_view text);

		void raw_text(const std::u8string_view raw) { _raw = raw; };
		const std::u8string& raw_text() const { return _raw; };

		void normalize();
		std::u8string text() const;
		std::u8string format_terms() const;

		bool has_type() const
		{
			return first_type() != prop::null;
		}

		/*date_t first_date() const
		{
			for (const auto& t : _terms)
			{
				if (t.val.t->data_type == prop::data_type::date && t.type == search_term::term_type::type)
					return date_t::from_time_stamp(t.val.n);
			}
			return date_t::null;
		}*/

		prop::key_ref first_type() const
		{
			for (const auto& t : _terms)
			{
				if (t.type == search_term_type::value || t.type == search_term_type::has_type)
				{
					return t.key;
				}
			}
			return prop::null;
		}

		friend bool operator==(const search_t& lhs, const search_t& rhs)
		{
			return lhs._selectors == rhs._selectors
				&& lhs._terms == rhs._terms
				&& lhs._related == rhs._related;
		}

		friend bool operator!=(const search_t& lhs, const search_t& rhs)
		{
			return !(lhs == rhs);
		}

		friend class search_matcher;
	};

	struct search_parent
	{
		search_t parent;
		std::u8string name;
		paths selection;
	};

	class search_matcher
	{
		const search_t& _search;
		const bloom_bits _bloom;
		const uint32_t _now_days = 0;

	public:
		search_matcher(const search_t& s, const uint32_t now_days = platform::now().to_days()) :
			_search(s),
			_bloom(s.calc_bloom_bits()),
			_now_days(now_days),
			has_terms(s.has_terms()),
			need_metadata(s.needs_metadata()),
			can_match_folder(_search.can_match_folder())
		{
		}

		const bool has_terms = false;
		const bool need_metadata = false;
		const bool can_match_folder = false;

		bool potential_match(const bloom_bits& bloom_bits) const;
		search_result match_term(const index_file_item& file, const search_term& term) const;
		search_result match_all_terms(const index_file_item& file) const;

		search_result match_item(file_path path, const index_file_item& file) const;
		search_result match_folder(str::cached name) const;
	};
}
