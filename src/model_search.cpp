// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "pch.h"
#include "model_search.h"

#include "model_index.h"
#include "model_items.h"
#include "model_tokenizer.h"

constexpr static std::u8string_view sv_duplicates = u8"duplicates"sv;

void df::search_t::clear_date_properties()
{
	std::erase_if(_terms, [](auto&& v) { return v.is_date(); });
	_raw.clear();
}

df::search_t df::search_t::parse_path(const std::u8string_view text)
{
	search_t result;
	const folder_path folder(text);

	if (folder.is_drive() || folder.exists())
	{
		result.add_selector(item_selector(folder));
	}
	else
	{
		const file_path file(text);

		if (file.exists() && file.is_valid())
		{
			result.add_selector(item_selector(file.folder())).with(file.name());
		}
	}

	return result;
}

df::search_t df::search_t::parse_from_input(const std::u8string_view text) const
{
	const auto has_selector = this->has_selector();

	if (is_path(text))
	{
		auto result = parse_path(text);

		if (!result.is_empty())
			return result;
	}

	if (has_selector && str::starts(text, u8"*."sv))
	{
		auto a = *this;
		const auto s = a.selectors().front();
		const item_selector sel(s.folder(), s.is_recursive(), text);
		return a.clear_selectors().add_selector(sel);
	}

	return parse(text);
}

df::search_t df::search_t::parse(const std::u8string_view text)
{
	const auto trimmed = str::trim(text);
	search_t result;
	result.raw_text(text);

	if (is_path(trimmed))
	{
		result = parse_path(trimmed);
	}

	if (result.is_empty())
	{
		if (is_probably_selector(trimmed))
		{
			result.add_selector(item_selector(trimmed));
		}
		else
		{
			search_tokenizer t;

			for (const auto& part : t.parse(trimmed))
			{
				if (part.scope.empty() && item_selector::can_iterate(part.term))
				{
					result.add_selector(item_selector(part.term));
				}
				else
				{
					result.parse_part(part);
				}
			}
		}
	}

	return result;
}

void df::search_t::normalize()
{
	// result remove duplicates
	std::ranges::sort(_terms);
	_terms.erase(std::ranges::unique(_terms).begin(), _terms.end());
}

static std::u8string term_quote(const std::u8string_view term_text)
{
	auto has_special_char = term_text.find_first_of(u8" \t\'\"!-#@"sv) != std::u8string::npos;

	if (!has_special_char)
	{
		// colons are allowed for folders or duration/time
		const auto colon_pos = term_text.find(':');

		if (colon_pos != std::u8string::npos)
		{
			if (colon_pos != 1) // folder
			{
				for (auto i = 0u; i < colon_pos; i++)
				{
					if (!std::iswdigit(term_text[i]))
					{
						// if not all digits, like a time ie 12:00
						has_special_char = true;
						break;
					}
				}
			}
		}
	}

	std::u8string result;

	if (has_special_char)
	{
		const char8_t quote_char = term_text.find(L'\"') == std::u8string::npos ? '\"' : '\'';
		result = quote_char;
		result += term_text;
		result += quote_char;
	}
	else
	{
		result = term_text;
	}

	return result;
}

static std::u8string format_xy(const df::xy16 xy)
{
	if (xy.y) return str::format(u8"{}/{}"sv, xy.x, xy.y);
	return str::to_string(xy.x);
}


static std::u8string format_term_value(const df::search_term& term)
{
	const auto* const t = term.key;
	const auto n = term.int_val;
	const auto d = term.float_val;
	const auto xy = term.xy_val;
	const auto n64 = term.int64_val;

	if (t->data_type == prop::data_type::string || !str::is_empty(term.text)) return term.text;
	if (t->data_type == prop::data_type::date) return prop::format_date(df::date_t::from_days(n));
	if (t == prop::f_number) return prop::format_f_num(d);
	if (t == prop::megapixels) return str::print(u8"%1.1f"sv, d);
	if (t == prop::dimensions) return prop::format_dimensions({xy.x, xy.y});
	if (t == prop::duration) return prop::format_duration(n);
	if (t == prop::exposure_time) return prop::format_exposure(d);
	if (t == prop::iso_speed) return prop::format_iso(n);
	if (t == prop::latitude) return prop::format_gps(d);
	if (t == prop::longitude) return prop::format_gps(d);
	if (t == prop::rating) return str::to_string(n);
	if (t == prop::audio_sample_rate) return prop::format_audio_sample_rate(n);
	if (t == prop::audio_channels) return prop::format_audio_channels(n);
	if (t == prop::audio_sample_type) return format_audio_sample_type(static_cast<prop::audio_sample_t>(n));
	if (t == prop::streams) return prop::format_streams(n);
	if (t == prop::track_num || t == prop::disk_num || t == prop::episode)
	{
		return format_xy(xy);
	}
	if (t == prop::orientation)
	{
		return str::to_string(n);
	}
	if (t == prop::year)
	{
		return str::to_string(n);
	}
	if (t == prop::focal_length) return prop::format_focal_length(d, 0);
	if (t == prop::file_size) return prop::format_size(df::file_size(n64));
	if (t->data_type == prop::data_type::int32)
	{
		return str::to_string(n);
	}
	if (t->data_type == prop::data_type::uint32)
	{
		return str::to_string(static_cast<uint32_t>(n));
	}

	/*else if (_t == Property::FocalLength35mmEquivalent || t == Property::FocalLength)
	{
		double fl = Find(Property::FocalLength, v) ? v.d : 0;
		int fl35 = Find(Property::FocalLength35mmEquivalent, v) ? v.n : 0;

		return Property::FormatFocalLength(fl, fl35, sz, len);
	}*/

	df::assert_true(false);
	return {};
}

std::u8string df::format_term(const search_term& term)
{
	u8ostringstream result;

	if (term.modifiers.less_than) result << u8"<"sv;
	if (term.modifiers.greater_than) result << u8">"sv;
	if (term.modifiers.equals) result << u8"="sv;
	if (term.modifiers.less_than || term.modifiers.greater_than || term.modifiers.equals) result << u8" "sv;

	if (term.type == search_term_type::has_type)
	{
		const auto scope = term.modifiers.positive ? tt.query_with : tt.query_without;
		result << scope;
		result << ':';
		result << ' ';
		result << term.key->name.sv();
	}
	else
	{
		if (!term.modifiers.positive) result << '-';

		if (term.type == search_term_type::text)
		{
			result << term_quote(term.text);
		}
		else if (term.type == search_term_type::value)
		{
			if (prop::tag == term.key)
			{
				result << '#';
				result << term_quote(format_term_value(term));
			}
			else if (prop::file_size == term.key)
			{
				// non quoted type
				result << term.key->name.sv();
				result << ':';
				result << ' ';
				result << format_term_value(term);
			}
			else
			{
				result << term.key->name.sv();
				result << ':';
				result << ' ';
				result << term_quote(format_term_value(term));
			}
		}
		else if (term.type == search_term_type::media_type)
		{
			result << '@';
			result << term.fg_val->display_name(false);
		}
		else if (term.type == search_term_type::date)
		{
			if (term.date_val.target == date_parts_prop::created)
			{
				result << tt.query_created;
				result << u8":"sv;
			}
			else if (term.date_val.target == date_parts_prop::modified)
			{
				result << tt.query_modified;
				result << u8":"sv;
			}

			if (term.date_val.age)
			{
				if (term.date_val.target == date_parts_prop::any)
				{
					result << tt.query_age;
					result << u8":"sv;
				}

				result << str::to_string(term.date_val.age);
			}
			else
			{
				const auto is_month_only = term.date_val.year == 0 && term.date_val.day == 0;
				bool is_cat = false;

				if (term.date_val.year != 0)
				{
					result << str::to_string(term.date_val.year);
					is_cat = true;
				}

				if (term.date_val.month != 0)
				{
					if (is_cat) result << u8"-"sv;
					result << (is_month_only
						           ? str::month(term.date_val.month, true)
						           : str::short_month(term.date_val.month, true));
					is_cat = true;
				}

				if (term.date_val.day != 0)
				{
					if (is_cat) result << u8"-"sv;
					result << str::to_string(term.date_val.day);
				}
			}
		}
		else if (term.type == search_term_type::location)
		{
			result << "loc:"
				<< std::showpos
				<< std::setprecision(5)
				<< std::noshowpoint
				<< term.coord_val.latitude() << term.coord_val.longitude()
				<< std::setprecision(2)
				<< term.float_val
				<< std::noshowpos;
		}
		else if (term.type == search_term_type::extension)
		{
			result << u8"ext:"sv;
			result << term.text;
		}
		else if (term.type == search_term_type::duplicate)
		{
			result << u8"@"sv;
			result << sv_duplicates;
		}
	}

	return result.str();
}

static void term_join(std::u8string& result, const df::search_term& term)
{
	if (!result.empty()) result += ' ';
	if (term.modifiers.logical_op == df::search_term_modifier_bool::m_or)
	{
		result += tt.query_or;
		result += u8" "sv;
	}
	for (auto i = 0; i < term.modifiers.begin_group; i++) result += '(';

	result += format_term(term);

	for (auto i = 0; i < term.modifiers.end_group; i++) result += ')';
}

static void term_join(std::u8string& result, const std::u8string_view term)
{
	if (!result.empty()) result += ' ';
	result += term_quote(term);
}

static void term_join(std::u8string& result, char8_t modifier, const std::u8string_view term)
{
	if (!result.empty()) result += ' ';
	result += modifier;
	result += term_quote(term);
}

static void term_join(std::u8string& result, const std::u8string_view scope, const std::u8string_view term)
{
	if (!result.empty()) result += ' ';
	result += scope;
	result += ':';
	result += ' ';
	result += term_quote(term);
}

static void term_join(std::u8string& result, char8_t modifier, const std::u8string_view scope,
                      const std::u8string_view term)
{
	if (!result.empty()) result += ' ';
	result += modifier;
	result += scope;
	result += ':';
	result += ' ';
	result += term_quote(term);
}

std::u8string df::search_t::format_terms() const
{
	const bool no_filter = _terms.empty() && !has_related();

	if (_selectors.size() == 1 && !has_media_type() && no_filter) return _selectors.front().str();

	std::u8string result;

	for (const auto& s : _selectors)
	{
		term_join(result, s.str());
	}

	if (has_related())
	{
		term_join(result, tt.query_related, _related.path.str());
	}

	for (const auto& t : _terms)
	{
		term_join(result, t);
	}

	return result;
}

std::u8string df::search_t::text() const
{
	return !str::is_empty(_raw) ? _raw : format_terms();
}

///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////

static int days_per_month(int month, int year = -1)
{
	if (month == 4 || month == 6 || month == 9 || month == 11)
	{
		return 30;
	}

	if (month == 2)
	{
		const auto leap_year = year >= 0 && (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
		return leap_year ? 29 : 28;
	}

	return 31;
}

df::date_parts df::search_t::find_date_parts() const
{
	date_parts result;

	for (const auto& t : _terms)
	{
		if (t.type == search_term_type::date && t.no_modifiers())
		{
			if (t.date_val.age) result.age = t.date_val.age;
			if (t.date_val.day) result.day = t.date_val.day;
			if (t.date_val.month) result.month = t.date_val.month;
			if (t.date_val.year) result.year = t.date_val.year;
			result.target = t.date_val.target;
		}
	}

	return result;
}

void df::search_t::next_date(bool forward)
{
	auto parts = find_date_parts();

	if (parts.day && parts.month)
	{
		if (forward)
		{
			const auto days_in_month = days_per_month(parts.month);
			parts.day += 1;

			if (parts.day > days_in_month)
			{
				parts.day = 1;
				parts.month += 1;

				if (parts.month > 12)
				{
					parts.month = 1;

					if (parts.year)
					{
						parts.year += 1;
					}
				}
			}
		}
		else
		{
			parts.day -= 1;

			if (parts.day < 1)
			{
				parts.month -= 1;

				if (parts.month < 1)
				{
					parts.month = 12;

					if (parts.year)
					{
						parts.year -= 1;
					}
				}

				parts.day = days_per_month(parts.month);
			}
		}

		clear_date_properties();
		day(parts.day, parts.month, parts.year, parts.target);
	}
	else if (parts.year && parts.month)
	{
		if (forward)
		{
			parts.month += 1;

			if (parts.month > 12)
			{
				parts.year += 1;
				parts.month = 1;
			}
		}
		else
		{
			parts.month -= 1;

			if (parts.month < 1)
			{
				parts.year -= 1;
				parts.month = 12;
			}
		}

		clear_date_properties();
		day(0, parts.month, parts.year, parts.target);
	}
	else if (parts.year)
	{
		parts.year += forward ? 1 : -1;
		clear_date_properties();
		year(parts.year, parts.target);
	}
	else if (parts.month)
	{
		if (forward)
		{
			parts.month += 1;

			if (parts.month > 12)
			{
				parts.month = 1;
			}
		}
		else
		{
			parts.month -= 1;

			if (parts.month < 1)
			{
				parts.month = 12;
			}
		}

		clear_date_properties();
		month(parts.month, parts.target);
	}
	/*else
	{
		for (const auto& v : _terms)
		{
			if (v.is_date())
			{
				auto d = date_t::from_time_stamp(v.val.n);
				d.shift_days(forward ? 1 : -1);
				v.val.n = d.to_time_stamp();
				break;
			}
		}
	}*/
}

bloom_bits df::search_t::calc_bloom_bits() const
{
	bloom_bits result;

	for (const auto& v : _terms)
	{
		switch (v.type)
		{
		case search_term_type::duplicate:
			result.types |= bloom_bits::flag;
			break;
		case search_term_type::media_type:
			result.types |= v.fg_val->bloom_bit();
			break;
		case search_term_type::value: break;
		case search_term_type::has_type: break;
		case search_term_type::date:
			result.types |= v.key->bloom_bit;
			break;
		case search_term_type::empty: break;
		case search_term_type::text: break;
		default:
			break;
		}
	}

	return result;
}

df::date_parts month_and_day(const std::u8string_view s)
{
	df::date_parts result;
	const auto parts = str::split(s, true, [](wchar_t c) { return c == '-' || c == '/' || c == '\\' || c == '.'; });

	if (parts.size() == 2)
	{
		const auto month = str::month(parts[0]);

		if (month != 0 && str::is_num(parts[1]))
		{
			const auto d = str::to_int(parts[1]);

			if (d >= 1 && d <= 31)
			{
				result.month = month;
				result.day = d;
			}
		}
	}

	return result;
}

df::date_parts year_and_month(const std::u8string_view s)
{
	df::date_parts result;
	const auto parts = str::split(s, true, [](wchar_t c) { return c == '-' || c == '/' || c == '\\' || c == '.'; });

	if (parts.size() == 2)
	{
		const auto month = str::month(parts[1]);

		if (month != 0 && str::is_num(parts[0]))
		{
			const auto y = str::to_int(parts[0]);

			if (y > 1900 && y < 2999)
			{
				result.month = month;
				result.year = y;
			}
		}
	}

	return result;
}


void df::search_t::parse_part(const search_part& part)
{
	const auto* type = prop::from_prefix(part.scope);

	if (part.scope == u8"@"sv && !part.literal)
	{
		const auto* const ft = parse_file_group(part.term);

		if (ft)
		{
			_terms.emplace_back(search_term(ft, part.modifier));
			return;
		}
	}

	if (type == prop::null)
	{
		if (str::icmp(part.scope, u8"without"sv) == 0 || str::icmp(part.scope, tt.query_without) == 0)
		{
			_terms.emplace_back(search_term(prop::from_prefix(part.term), false));
			return;
		}
		if (str::icmp(part.scope, u8"with"sv) == 0 || str::icmp(part.scope, tt.query_with) == 0)
		{
			_terms.emplace_back(search_term(prop::from_prefix(part.term), true));
			return;
		}
		if (str::icmp(part.scope, u8"related"sv) == 0 || str::icmp(part.scope, tt.query_related) == 0)
		{
			_related.path = file_path(part.term);
			return;
		}
	}

	date_t date;
	auto n = str::to_int(part.term);
	auto d = str::to_double(part.term);
	auto probably_number = str::is_probably_num(part.term);
	auto is_num = str::is_num(part.term);
	auto is_date = date.parse(part.term) && date.is_valid();
	double d1, d2;
	int n1, n2, n3;
	search_term result;

	if (part.scope == u8"@"sv)
	{
		static const hash_map<std::u8string_view, search_term_type, ihash, ieq> pre_title_stop_words
		{
			{tt.query_duplicates, search_term_type::duplicate},
			{tt.query_duplicates_alt1, search_term_type::duplicate},
			{tt.query_duplicates_alt2, search_term_type::duplicate},
			{u8"dups"sv, search_term_type::duplicate},
			{u8"duplicate"sv, search_term_type::duplicate},
			{sv_duplicates, search_term_type::duplicate},
		};

		const auto found_flag = pre_title_stop_words.find(part.term);

		if (found_flag != pre_title_stop_words.end())
		{
			result = search_term(found_flag->second, part.modifier);
		}
	}
	else if (str::icmp(part.scope, u8"loc"sv) == 0)
	{
		const auto loc = split_location(part.term);

		if (loc.success)
		{
			gps_coordinate coord(loc.x, loc.y);
			result = search_term(search_term_type::location, coord, loc.z, part.modifier);
		}
	}
	else if (str::icmp(part.scope, u8"ext"sv) == 0 ||
		str::icmp(part.scope, u8"extension"sv) == 0 ||
		str::icmp(part.scope, u8"type"sv) == 0)
	{
		result = search_term(search_term_type::extension, part.term, part.modifier);
	}
	else if (str::icmp(part.scope, u8"age"sv) == 0 || str::icmp(part.scope, tt.query_age) == 0)
	{
		date_parts dd;
		dd.age = n;
		result = search_term(search_term_type::date, dd, part.modifier);
	}
	// if no property type try to guess one
	else if (type == prop::null)
	{
		if (_snscanf_s(std::bit_cast<const char*>(part.term.data()), part.term.size(), "%d:%d", &n1, &n2) == 2)
		{
			result = search_term(prop::duration, round(n1 * 60.0 + n2), part.modifier);
		}
		else if (!month_and_day(part.term).is_empty())
		{
			auto dd = month_and_day(part.term);
			result = search_term(search_term_type::date, dd, part.modifier);
		}
		else if (!year_and_month(part.term).is_empty())
		{
			auto dd = year_and_month(part.term);
			result = search_term(search_term_type::date, dd, part.modifier);
		}
		else if (is_date)
		{
			result = search_term(search_term_type::date, date_parts(date.date()), part.modifier);
		}
		else if (is_num && n >= 1 && n <= 5)
		{
			result = search_term(prop::rating, n, part.modifier);
		}
		else if (is_num && n > 1800 && n < 2100)
		{
			date_parts dd;
			dd.year = n;
			result = search_term(search_term_type::date, dd, part.modifier);
		}
		else if (str::month(part.term) != 0)
		{
			date_parts dd;
			dd.month = str::month(part.term);
			result = search_term(search_term_type::date, dd, part.modifier);
		}
		else
		{
			_terms.emplace_back(search_term(part.term, part.modifier));
		}
	}
	else if (type == prop::tag)
	{
		result = search_term(type, part.term, part.modifier);
	}
	else if (type == prop::created_utc || type == prop::created_exif || type == prop::modified)
	{
		auto target = date_parts_prop::any;
		if (type == prop::modified) target = date_parts_prop::modified;
		if (type == prop::created_utc || type == prop::created_exif) target = date_parts_prop::created;

		if (is_num)
		{
			if (is_num && n >= 1800 && n <= 2100)
			{
				date_parts dd;
				dd.year = n;
				dd.target = target;
				result = search_term(search_term_type::date, dd, part.modifier);
			}
			else
			{
				date_parts dd;
				dd.age = n;
				dd.target = target;
				result = search_term(search_term_type::date, dd, part.modifier);
			}
		}
		else if (!month_and_day(part.term).is_empty())
		{
			auto dd = month_and_day(part.term);
			dd.target = target;
			result = search_term(search_term_type::date, dd, part.modifier);
		}
		else if (!year_and_month(part.term).is_empty())
		{
			auto dd = year_and_month(part.term);
			dd.target = target;
			result = search_term(search_term_type::date, dd, part.modifier);
		}
		else if (str::month(part.term) != 0)
		{
			date_parts dd;
			dd.year = str::month(part.term);
			dd.target = target;
			result = search_term(search_term_type::date, dd, true);
		}
		else if (is_date)
		{
			result = search_term(search_term_type::date, date_parts(date.date(), target), part.modifier);
		}
	}
	else if (type == prop::f_number)
	{
		if (_snscanf_s(std::bit_cast<const char*>(part.term.data()), part.term.size(), "f/%lf", &d1) == 1)
		{
			result = search_term(prop::f_number, d1, part.modifier);
		}
		else
		{
			result = search_term(prop::f_number, d, part.modifier);
		}
	}
	else if (type == prop::iso_speed)
	{
		if (_snscanf_s(std::bit_cast<const char*>(part.term.data()), part.term.size(), "ISO%d", &n1) == 1)
		{
			result = search_term(prop::iso_speed, n, part.modifier);
		}
		else
		{
			result = search_term(prop::iso_speed, n, part.modifier);
		}
	}
	else if (type == prop::exposure_time)
	{
		if (_snscanf_s(std::bit_cast<const char*>(part.term.data()), part.term.size(), "%lf/%lfs", &d1, &d2) == 2)
		{
			result = search_term(prop::exposure_time, d1 / d2, part.modifier);
		}
		else
		{
			result = search_term(prop::exposure_time, d, part.modifier);
		}
	}
	else if (type == prop::audio_sample_rate)
	{
		auto rate = n;

		if (str::ends(part.term, u8"khz"sv))
		{
			rate = static_cast<int>(d * 1000);
		}

		result = search_term(prop::audio_sample_rate, rate, part.modifier);
	}
	else if (type == prop::audio_sample_type)
	{
		const static hash_map<std::u8string_view, prop::audio_sample_t, ihash, ieq> audio_sample_types = {
			{u8"none"sv, prop::audio_sample_t::none},
			{u8"8bit"sv, prop::audio_sample_t::unsigned_8bit},
			{u8"16bit"sv, prop::audio_sample_t::signed_16bit},
			{u8"32bit"sv, prop::audio_sample_t::signed_32bit},
			{u8"64bit"sv, prop::audio_sample_t::signed_64bit},
			{u8"float"sv, prop::audio_sample_t::signed_float},
			{u8"double"sv, prop::audio_sample_t::signed_double},
			{u8"8bit planar"sv, prop::audio_sample_t::unsigned_planar_8bit},
			{u8"16bit planar"sv, prop::audio_sample_t::signed_planar_16bit},
			{u8"32bit planar"sv, prop::audio_sample_t::signed_planar_32bit},
			{u8"64bit planar"sv, prop::audio_sample_t::signed_planar_64bit},
			{u8"float planar"sv, prop::audio_sample_t::planar_float},
			{u8"double planar"sv, prop::audio_sample_t::planar_double}
		};

		const auto found = audio_sample_types.find(part.term);

		if (found != audio_sample_types.end())
		{
			result = search_term(prop::audio_sample_type, static_cast<int>(found->second), part.modifier);
		}
		else
		{
			result = search_term(prop::audio_sample_type, n, part.modifier);
		}
	}
	else if (type == prop::audio_channels)
	{
		const static hash_map<std::u8string_view, int, ihash, ieq> audio_channels = {
			{u8"mono"sv, 1},
			{u8"stereo"sv, 2},
			{u8"3.0 surround"sv, 3},
			{u8"quad"sv, 4},
			{u8"5.0 surround"sv, 5},
			{u8"5.1 surround"sv, 6},
			{u8"7.1 surround"sv, 8}
		};

		const auto found = audio_channels.find(part.term);

		if (found != audio_channels.end())
		{
			result = search_term(prop::audio_channels, found->second, part.modifier);
		}
		else
		{
			result = search_term(prop::audio_channels, n, part.modifier);
		}
	}
	else if (type == prop::duration)
	{
		auto mods = part.modifier;
		auto duration = 0;

		if (_snscanf_s(std::bit_cast<const char*>(part.term.data()), part.term.size(), "%d:%d", &n1, &n2) == 2)
		{
			duration = n1 * 60 + n2;
		}
		else if (_snscanf_s(std::bit_cast<const char*>(part.term.data()), part.term.size(), "%d:%d:%d", &n1, &n2,
		                    &n3) == 3)
		{
			duration = n1 * 60 * 60 + n2 * 60 + n3;
		}
		else
		{
			if (str::ends(part.term, u8"m"sv))
			{
				duration = n * 60;
			}
			else if (str::ends(part.term, u8"h"sv))
			{
				duration = n * 60 * 60;
			}
			else if (str::ends(part.term, u8"d"sv))
			{
				duration = n * 60 * 60 * 24;
			}
			else
			{
				duration = n;
			}
		}

		result = search_term(prop::duration, duration, mods);
	}
	else if (type == prop::file_size)
	{
		auto size = 0ull;

		if (str::ends(part.term, u8"gb"sv))
		{
			size = df::round(d * 1024ull * 1024ull * 1024ull);
		}
		else if (str::ends(part.term, u8"mb"sv))
		{
			size = df::round(d * 1024ull * 1024ull);
		}
		else if (str::ends(part.term, u8"kb"sv))
		{
			size = df::round(d * 1024ull);
		}
		else
		{
			size = n;
		}

		result = search_term(type, size, part.modifier);
	}
	else
	{
		switch (type->data_type)
		{
		case prop::data_type::int32:
			result = search_term(type, n, part.modifier);
			break;
		case prop::data_type::date:
			result = search_term(type, date.to_days(), part.modifier);
			break;
		case prop::data_type::float32:
			result = search_term(type, d, part.modifier);
			break;
		case prop::data_type::size:
			result = search_term(type, static_cast<uint64_t>(n), part.modifier);
			break;
		case prop::data_type::string:
			result = search_term(type, part.term, part.modifier);
			break;
		case prop::data_type::int_pair:
			break;
		case prop::data_type::uint32:
			result = search_term(type, n, part.modifier);
			break;
		}
	}

	if (!result.is_empty())
	{
		_terms.emplace_back(result);
	}
}

void df::related_info::load(const file_item_ptr& i)
{
	const auto md = i->metadata();

	path = i->path();
	name = i->name();
	size = i->file_size();
	file_created = i->file_created();
	ft = i->file_type();
	crc32c = i->crc32c();
	group = i->duplicates().group;

	if (md)
	{
		gps = md->coordinate;
		metadata_created = md->created();
	}

	is_loaded = true;
}

bool df::search_t::needs_metadata() const
{
	for (const auto& t : _terms)
	{
		if (t.needs_metadata())
		{
			return true;
		}
	}

	return false;
}

bool df::search_matcher::potential_match(const bloom_bits& bloom_bits) const
{
	return _bloom.potential_match(bloom_bits);
}

struct compare_result
{
	bool match = false;
	int com = 0;
	str::cached val_matched = {};
};

static bool modifier_match(compare_result cmp, const df::search_term_modifier& modifiers)
{
	if (modifiers.equals && cmp.com == 0 && cmp.match)
	{
		return true;
	}

	if (modifiers.greater_than && cmp.com < 0 && cmp.match)
	{
		return true;
	}

	if (modifiers.less_than && cmp.com > 0 && cmp.match)
	{
		return true;
	}

	if (!modifiers.greater_than && !modifiers.less_than && cmp.match)
	{
		return cmp.com == 0;
	}

	return false;
}


inline unsigned msb_64(uint64_t mask)
{
	unsigned index = 0;
	while (mask)
	{
		++index;
		mask >>= 1;
	}
	return index;
}

static compare_result compare_term(const df::search_term& term, const int rr)
{
	const auto ll = term.int_val;
	const auto res = (ll < rr) ? -1 : (ll > rr) ? 1 : 0;
	return {true, res};
}


static compare_result compare_file_size(const df::search_term& term, const uint64_t r)
{
	const auto ll = prop::round_size(term.int64_val).total();
	const auto rr = prop::round_size(r).total();

	const auto res = (ll < rr) ? -1 : (ll > rr) ? 1 : 0;
	return {true, res};
}

static compare_result compare_term(const df::search_term& term, const df::date_t r)
{
	df::assert_true(false);
	return {};
}

static compare_result compare_term(const df::search_term& term, const str::cached r)
{
	if (term._is_wildcard)
	{
		const auto cmp = wildcard_icmp(r, term.text);
		return {cmp, 0, r};
	}

	const auto cmp = icmp(term.text, r);
	return {cmp == 0, 0, r};
}

static compare_result compare_aperture(const df::search_term& term, double f_number)
{
	auto ll = 0.0;
	auto rr = 0.0;

	if (term.key == prop::f_number)
	{
		ll = prop::closest_fstop(term.float_val);
	}

	if (f_number != 0.0)
	{
		rr = prop::closest_fstop(f_number);
	}

	if (df::equiv(ll, rr)) return {true, 0};
	const auto res = (ll < rr) ? -1 : (ll > rr) ? 1 : 0;
	return {true, res};
}

static double mp_round(const double mp)
{
	if (mp < 2.0)
	{
		return std::round(mp * 10.0) / 10.0;
	}

	return std::round(mp);
}

static compare_result compare_megapixels(const df::search_term& term, int w, int h)
{
	const auto ll = mp_round(term.float_val);
	const auto rr = mp_round(ui::calc_mega_pixels(w, h));

	if (df::equiv(ll, rr)) return {true, 0};
	const auto res = (ll < rr) ? -1 : (ll > rr) ? 1 : 0;
	return {true, res};
}

static compare_result compare_term(const df::search_term& term, const double rr)
{
	const auto ll = term.float_val;
	const auto res = (ll < rr) ? -1 : (ll > rr) ? 1 : 0;
	return {true, res};
}

static compare_result compare_exposure_time(const df::search_term& term, const double r)
{
	const double ll = (term.float_val < 1.0) ? -1.0 / term.float_val : term.float_val;
	const double rr = (r < 1.0) ? -1.0 / r : r;

	if (df::equiv(ll, rr, 0.00001)) return {true, 0};
	const auto res = (ll > rr) ? -1 : (ll < rr) ? 1 : 0;
	return {true, res};
}

static compare_result compare_term(const df::search_term& term, const df::xy8 r)
{
	const auto l = term.xy_val;

	int res = 0;
	if (l.x > r.x) res = -1;
	else if (l.x < r.x) res = 1;
	else if (l.y > r.y) res = -1;
	else if (l.y < r.y) res = 1;
	return {true, res};
}

static compare_result compare_term(const df::search_term& term, const df::xy16 r)
{
	const auto l = term.xy_val;

	int res = 0;
	if (l.x > r.x) res = -1;
	else if (l.x < r.x) res = 1;
	else if (l.y > r.y) res = -1;
	else if (l.y < r.y) res = 1;
	return {true, res};
}

//static compare_result compare_term(const df::search_term& term, const df::file_size& r)
//{
//	const auto ll = term.int_val;
//	const auto rr = r.to_int64();
//
//	const auto res = (ll < rr) ? -1 : (ll > rr) ? 1 : 0;
//	return { true, res };
//}

df::search_result compare_text(const df::search_term& term, const df::index_file_item& file)
{
	const auto& text = term.text;

	if (contains(file.name, text)) return {df::search_result_type::match_prop, prop::file_name};
	if (str::same(prop::format_size(file.size), text)) return {df::search_result_type::match_prop, prop::file_size};
	//if (str::contains(prop::format_date(file.file_modified, false), text)) return true;
	//if (str::contains(prop::format_date(file.file_created, false), text)) return true;

	const auto md = file.metadata.load();;

	if (md)
	{
		if (contains(md->album, text)) return {df::search_result_type::match_prop, prop::album};
		//if (contains(md->album_artist, text)) return {df::search_result_type::match_prop, prop::album_artist};
		//if (contains(md->artist, text)) return {df::search_result_type::match_prop, prop::artist};
		if (contains(md->audio_codec, text)) return {df::search_result_type::match_prop, prop::audio_codec};
		if (contains(md->bitrate, text)) return {df::search_result_type::match_prop, prop::bitrate};
		if (contains(md->camera_manufacturer, text))
			return {
				df::search_result_type::match_prop, prop::camera_manufacturer
			};
		if (contains(md->camera_model, text)) return {df::search_result_type::match_prop, prop::camera_model};
		if (contains(md->comment, text)) return {df::search_result_type::match_prop, prop::comment};
		if (contains(md->composer, text)) return {df::search_result_type::match_prop, prop::composer};
		if (contains(md->copyright_creator, text)) return {df::search_result_type::match_prop, prop::copyright_creator};
		if (contains(md->copyright_credit, text)) return {df::search_result_type::match_prop, prop::copyright_credit};
		if (contains(md->copyright_notice, text)) return {df::search_result_type::match_prop, prop::copyright_notice};
		if (contains(md->copyright_source, text)) return {df::search_result_type::match_prop, prop::copyright_source};
		if (contains(md->copyright_url, text)) return {df::search_result_type::match_prop, prop::copyright_url};
		if (contains(md->description, text)) return {df::search_result_type::match_prop, prop::description};
		if (contains(md->encoder, text)) return {df::search_result_type::match_prop, prop::encoder};
		if (contains(md->file_name, text)) return {df::search_result_type::match_prop, prop::file_name};
		if (contains(md->genre, text)) return {df::search_result_type::match_prop, prop::genre};
		if (contains(md->lens, text)) return {df::search_result_type::match_prop, prop::lens};
		if (contains(md->location_place, text)) return {df::search_result_type::match_prop, prop::location_place};
		if (contains(md->location_country, text)) return {df::search_result_type::match_prop, prop::location_country};
		if (contains(md->location_state, text)) return {df::search_result_type::match_prop, prop::location_state};
		if (contains(md->performer, text)) return {df::search_result_type::match_prop, prop::performer};
		if (contains(md->pixel_format, text)) return {df::search_result_type::match_prop, prop::pixel_format};
		if (contains(md->publisher, text)) return {df::search_result_type::match_prop, prop::publisher};
		if (contains(md->show, text)) return {df::search_result_type::match_prop, prop::show};
		if (contains(md->synopsis, text)) return {df::search_result_type::match_prop, prop::synopsis};
		if (contains(md->title, text)) return {df::search_result_type::match_prop, prop::title};
		if (contains(md->label, text)) return {df::search_result_type::match_prop, prop::label};
		if (contains(md->video_codec, text)) return {df::search_result_type::match_prop, prop::video_codec};
		if (contains(md->raw_file_name, text)) return {df::search_result_type::match_prop, prop::raw_file_name};
		//if (contains(md->tags, text)) return {df::search_result_type::match_prop, prop::tag, str::cache(text)};

		if (str::same(prop::format_dimensions(md->dimensions()), text))
			return {
				df::search_result_type::match_prop, prop::dimensions
			};
		if (str::same(prop::format_pixels(md->dimensions(), file.ft), text))
			return {
				df::search_result_type::match_prop, prop::dimensions
			};
		if (str::same(prop::format_exposure(md->exposure_time), text))
			return {
				df::search_result_type::match_prop, prop::exposure_time
			};
		if (str::same(prop::format_f_num(md->f_number), text))
			return {
				df::search_result_type::match_prop, prop::f_number
			};
		if (str::same(prop::format_focal_length(md->focal_length, md->focal_length_35mm_equivalent), text))
			return {
				df::search_result_type::match_prop, prop::focal_length
			};

		/*if (str::contains(prop::format_date(md->created_digitized, false), text)) return true;
		if (str::contains(prop::format_date(md->created_exif, false), text)) return true;
		if (str::contains(prop::format_date(md->created_utc, false), text)) return true;*/
		if (str::same(prop::format_duration(md->duration), text))
			return {
				df::search_result_type::match_prop, prop::duration
			};
		//if (str::contains(str::to_string(md->height), text)) return df::search_result::match_x;
		//if (str::contains(str::to_string(md->width), text)) return df::search_result::match_x;
		if (str::same(prop::format_iso(md->iso_speed), text))
			return {
				df::search_result_type::match_prop, prop::iso_speed
			};
		//if (str::same(prop::format_rating(md->rating), text)) return { df::search_result_type::match_prop, prop::rating };
		if (str::same(prop::format_audio_channels(md->audio_channels), text))
			return {
				df::search_result_type::match_prop, prop::audio_channels
			};
		if (str::same(prop::format_audio_sample_rate(md->audio_sample_rate), text))
			return {
				df::search_result_type::match_prop, prop::audio_sample_rate
			};
		if (str::same(format_audio_sample_type(static_cast<prop::audio_sample_t>(md->audio_sample_type)), text))
			return
				{df::search_result_type::match_prop, prop::audio_sample_type};
		if (str::same(str::to_string(md->year), text)) return {df::search_result_type::match_prop, prop::year};

		//if (str::contains(prop::format_season(md->season), text)) return true;
		//if (str::contains(prop::format_orientation(md->orientation), text)) return true;
		//if (str::contains(prop::format_disk(md->disk), text)) return true;
		//if (str::contains(prop::format_episode(md->episode), text)) return true;
		//if (str::contains(prop::format_track(md->track), text)) return true;

		//if (str::contains(prop::format_gps(md->coordinate), text)) return true;

		const auto md = file.metadata.load();

		compare_result comp_result;
		prop::key_ref key = prop::null;

		auto cmp = [&comp_result, &term](const std::u8string_view part)
		{
			if (!comp_result.match)
			{
				comp_result = compare_term(term, str::cache(part));
			}
		};

		if (!is_empty(md->tags)) split2(md->tags, true, cmp);

		if (comp_result.match)
		{
			key = prop::tag;
		}
		else
		{
			if (!is_empty(md->artist)) split2(md->artist, true, cmp, str::is_artist_separator);

			if (comp_result.match)
			{
				key = prop::artist;
			}
			else
			{
				if (!is_empty(md->album_artist)) split2(md->album_artist, true, cmp, str::is_artist_separator);

				if (comp_result.match)
				{
					key = prop::album_artist;
				}
			}
		}

		if (comp_result.match)
		{
			return {df::search_result_type::match_prop, key, comp_result.val_matched};
		}
	}

	return {df::search_result_type::no_match};
}

static compare_result compare_val(const df::search_term& term, const df::index_file_item& file)
{
	auto&& t = term.key;

	if (t == prop::modified && !prop::is_null(file.file_modified)) return compare_term(term, file.file_modified);
	if (t == prop::file_size && !file.size.is_empty()) return compare_file_size(term, file.size.to_int64());

	const auto md = file.metadata.load();

	if (md)
	{
		if (term.key == prop::tag || term.key == prop::artist || term.key == prop::album_artist)
		{
			compare_result comp_result;
			auto cmp = [&comp_result, &term](const std::u8string_view part)
			{
				if (!comp_result.match)
				{
					comp_result = compare_term(term, str::cache(part));
				}
			};

			if (term.key == prop::tag) split2(md->tags, true, cmp);
			else if (term.key == prop::artist) split2(md->artist, true, cmp, str::is_artist_separator);
			else if (term.key == prop::album_artist) split2(md->album_artist, true, cmp, str::is_artist_separator);

			return comp_result;
		}

		if (t == prop::title && !prop::is_null(md->title)) return compare_term(term, md->title);
		if (t == prop::description && !prop::is_null(md->description)) return compare_term(term, md->description);
		if (t == prop::comment && !prop::is_null(md->comment)) return compare_term(term, md->comment);
		if (t == prop::synopsis && !prop::is_null(md->synopsis)) return compare_term(term, md->synopsis);
		if (t == prop::composer && !prop::is_null(md->composer)) return compare_term(term, md->composer);
		if (t == prop::encoder && !prop::is_null(md->encoder)) return compare_term(term, md->encoder);
		if (t == prop::publisher && !prop::is_null(md->publisher)) return compare_term(term, md->publisher);
		if (t == prop::performer && !prop::is_null(md->performer)) return compare_term(term, md->performer);
		if (t == prop::genre && !prop::is_null(md->genre)) return compare_term(term, md->genre);
		if (t == prop::copyright_credit && !prop::is_null(md->copyright_credit))
			return compare_term(
				term, md->copyright_credit);
		if (t == prop::copyright_notice && !prop::is_null(md->copyright_notice))
			return compare_term(
				term, md->copyright_notice);
		if (t == prop::copyright_creator && !prop::is_null(md->copyright_creator))
			return compare_term(
				term, md->copyright_creator);
		if (t == prop::copyright_source && !prop::is_null(md->copyright_source))
			return compare_term(
				term, md->copyright_source);
		if (t == prop::copyright_url && !prop::is_null(md->copyright_url)) return compare_term(term, md->copyright_url);
		if (t == prop::file_name && !prop::is_null(md->file_name)) return compare_term(term, md->file_name);
		if (t == prop::raw_file_name && !prop::is_null(md->raw_file_name)) return compare_term(term, md->raw_file_name);
		if (t == prop::pixel_format && !prop::is_null(md->pixel_format)) return compare_term(term, md->pixel_format);
		if (t == prop::bitrate && !prop::is_null(md->bitrate)) return compare_term(term, md->bitrate);
		if (t == prop::orientation) return compare_term(term, static_cast<int>(md->orientation));
		if (t == prop::dimensions) return compare_term(term, df::xy16::make(md->width, md->height));
		if (t == prop::megapixels) return compare_megapixels(term, md->width, md->height);
		if (t == prop::year && !prop::is_null(md->year)) return compare_term(term, md->year);
		if (t == prop::rating && !prop::is_null(md->rating)) return compare_term(term, md->rating);
		if (t == prop::season && !prop::is_null(md->season)) return compare_term(term, md->season);
		if (t == prop::episode && !prop::is_null(md->episode)) return compare_term(term, md->episode);
		if (t == prop::disk_num && !prop::is_null(md->disk)) return compare_term(term, md->disk);
		if (t == prop::track_num && !prop::is_null(md->track)) return compare_term(term, md->track);
		if (t == prop::duration && !prop::is_null(md->duration)) return compare_term(term, md->duration);
		if (t == prop::created_utc && !prop::is_null(md->created_utc)) return compare_term(term, md->created_utc);
		if (t == prop::created_exif && !prop::is_null(md->created_exif)) return compare_term(term, md->created_exif);
		if (t == prop::created_digitized && !prop::is_null(md->created_digitized))
			return compare_term(
				term, md->created_digitized);
		if (t == prop::exposure_time && !prop::is_null(md->exposure_time))
			return compare_exposure_time(
				term, md->exposure_time);
		if (t == prop::f_number && !prop::is_null(md->f_number)) return compare_aperture(term, md->f_number);
		if (t == prop::focal_length && !prop::is_null(md->focal_length)) return compare_term(term, md->focal_length);
		if (t == prop::focal_length_35mm_equivalent && !prop::is_null(md->focal_length_35mm_equivalent))
			return
				compare_term(term, md->focal_length_35mm_equivalent);
		if (t == prop::iso_speed && !prop::is_null(md->iso_speed)) return compare_term(term, md->iso_speed);
		if (t == prop::latitude && !prop::is_null(md->coordinate.is_valid()))
			return compare_term(
				term, md->coordinate.latitude());
		if (t == prop::longitude && !prop::is_null(md->coordinate.is_valid()))
			return compare_term(
				term, md->coordinate.longitude());
		if (t == prop::location_country && !prop::is_null(md->location_country))
			return compare_term(
				term, md->location_country);
		if (t == prop::location_state && !prop::is_null(md->location_state))
			return compare_term(
				term, md->location_state);
		if (t == prop::location_place && !prop::is_null(md->location_place))
			return compare_term(
				term, md->location_place);
		if (t == prop::camera_manufacturer && !prop::is_null(md->camera_manufacturer))
			return compare_term(
				term, md->camera_manufacturer);
		if (t == prop::camera_model && !prop::is_null(md->camera_model)) return compare_term(term, md->camera_model);
		if (t == prop::lens && !prop::is_null(md->lens)) return compare_term(term, md->lens);
		if (t == prop::video_codec && !prop::is_null(md->video_codec)) return compare_term(term, md->video_codec);
		if (t == prop::audio_sample_type && !prop::is_null(md->audio_sample_type))
			return compare_term(
				term, md->audio_sample_type);
		if (t == prop::audio_sample_rate && !prop::is_null(md->audio_sample_rate))
			return compare_term(
				term, md->audio_sample_rate);
		if (t == prop::audio_channels && !prop::is_null(md->audio_channels))
			return compare_term(
				term, md->audio_channels);
		if (t == prop::audio_codec && !prop::is_null(md->audio_codec)) return compare_term(term, md->audio_codec);
		if (t == prop::album_artist && !prop::is_null(md->album_artist)) return compare_term(term, md->album_artist);
		if (t == prop::artist && !prop::is_null(md->artist)) return compare_term(term, md->artist);
		if (t == prop::album && !prop::is_null(md->album)) return compare_term(term, md->album);
		if (t == prop::show && !prop::is_null(md->show)) return compare_term(term, md->show);
		if (t == prop::game && !prop::is_null(md->game)) return compare_term(term, md->game);
		if (t == prop::system && !prop::is_null(md->system)) return compare_term(term, md->system);
		if (t == prop::label && !prop::is_null(md->label)) return compare_term(term, md->label);
		if (t == prop::doc_id && !prop::is_null(md->doc_id)) return compare_term(term, md->doc_id);
	}

	return {};
}

bool has_type(const prop::key_ref t, const df::index_file_item& file)
{
	if (t == prop::modified) return !prop::is_null(file.file_modified);
	if (t == prop::file_size) return !file.size.is_empty();

	const auto md = file.metadata.load();

	if (md)
	{
		if (t == prop::title) return !prop::is_null(md->title);
		if (t == prop::description) return !prop::is_null(md->description);
		if (t == prop::comment) return !prop::is_null(md->comment);
		if (t == prop::synopsis) return !prop::is_null(md->synopsis);
		if (t == prop::composer) return !prop::is_null(md->composer);
		if (t == prop::encoder) return !prop::is_null(md->encoder);
		if (t == prop::publisher) return !prop::is_null(md->publisher);
		if (t == prop::performer) return !prop::is_null(md->performer);
		if (t == prop::genre) return !prop::is_null(md->genre);
		if (t == prop::copyright_credit) return !prop::is_null(md->copyright_credit);
		if (t == prop::copyright_notice) return !prop::is_null(md->copyright_notice);
		if (t == prop::copyright_creator) return !prop::is_null(md->copyright_creator);
		if (t == prop::copyright_source) return !prop::is_null(md->copyright_source);
		if (t == prop::copyright_url) return !prop::is_null(md->copyright_url);
		if (t == prop::file_name) return !prop::is_null(md->file_name);
		if (t == prop::raw_file_name) return !prop::is_null(md->raw_file_name);
		if (t == prop::pixel_format) return !prop::is_null(md->pixel_format);
		if (t == prop::bitrate) return !prop::is_null(md->bitrate);
		if (t == prop::orientation) return md->orientation != ui::orientation::left_top;
		if (t == prop::dimensions) return !prop::is_null(md->width);
		if (t == prop::year) return !prop::is_null(md->year);
		if (t == prop::rating) return !prop::is_null(md->rating);
		if (t == prop::season) return !prop::is_null(md->season);
		if (t == prop::episode) return !prop::is_null(md->episode);
		if (t == prop::disk_num) return !prop::is_null(md->disk);
		if (t == prop::track_num) return !prop::is_null(md->track);
		if (t == prop::duration) return !prop::is_null(md->duration);
		if (t == prop::created_utc) return !prop::is_null(md->created_utc);
		if (t == prop::created_exif) return !prop::is_null(md->created_exif);
		if (t == prop::created_digitized) return !prop::is_null(md->created_digitized);
		if (t == prop::exposure_time) return !prop::is_null(md->exposure_time);
		if (t == prop::f_number) return !prop::is_null(md->f_number);
		if (t == prop::focal_length) return !prop::is_null(md->focal_length);
		if (t == prop::focal_length_35mm_equivalent) return !prop::is_null(md->focal_length_35mm_equivalent);
		if (t == prop::iso_speed) return !prop::is_null(md->iso_speed);
		if (t == prop::latitude) return md->coordinate.is_valid();
		if (t == prop::longitude) return md->coordinate.is_valid();
		if (t == prop::location_country) return !prop::is_null(md->location_country);
		if (t == prop::location_state) return !prop::is_null(md->location_state);
		if (t == prop::location_place) return !prop::is_null(md->location_place);
		if (t == prop::camera_manufacturer) return !prop::is_null(md->camera_manufacturer);
		if (t == prop::camera_model) return !prop::is_null(md->camera_model);
		if (t == prop::lens) return !prop::is_null(md->lens);
		if (t == prop::video_codec) return !prop::is_null(md->video_codec);
		if (t == prop::audio_sample_rate) return !prop::is_null(md->audio_sample_rate);
		if (t == prop::audio_sample_type) return !prop::is_null(md->audio_sample_type);
		if (t == prop::audio_codec) return !prop::is_null(md->audio_codec);
		if (t == prop::album_artist) return !prop::is_null(md->album_artist);
		if (t == prop::artist) return !prop::is_null(md->artist);
		if (t == prop::album) return !prop::is_null(md->album);
		if (t == prop::show) return !prop::is_null(md->show);
		if (t == prop::game) return !prop::is_null(md->game);
		if (t == prop::system) return !prop::is_null(md->system);
		if (t == prop::tag) return !prop::is_null(md->tags);
		if (t == prop::label) return !prop::is_null(md->label);
	}

	return false;
}

static bool is_date_match(const df::date_parts& term, const df::date_t d, const df::search_term_modifier& modifiers,
                          const uint32_t now_days)
{
	if (!d.is_valid())
		return false;

	if (term.age != 0)
	{
		const auto then = d.to_days();
		const auto age = static_cast<int>(now_days) - static_cast<int>(then);
		return age <= term.age;
	}

	const auto parts = d.date();
	int tt = 0;
	int vv = 0;

	if (term.year != 0)
	{
		tt += term.year * 12 * 31;
		vv += parts.year * 12 * 31;
	}

	if (term.month != 0)
	{
		tt += term.month * 31;
		vv += parts.month * 31;
	}

	if (term.day != 0)
	{
		tt += term.day;
		vv += parts.day;
	}

	const auto cc = tt - vv;
	const auto can_match = tt != 0 && vv != 0;

	if (can_match)
	{
		if (modifiers.equals && cc == 0)
		{
			return true;
		}

		if (modifiers.greater_than && cc < 0)
		{
			return true;
		}

		if (modifiers.less_than && cc > 0)
		{
			return true;
		}

		if (!modifiers.greater_than && !modifiers.less_than)
		{
			return cc == 0;
		}
	}

	return false;
}

static bool is_date_match(const df::search_term& term, const df::index_file_item& file, const uint32_t now_days)
{
	const bool is_any = term.date_val.target == df::date_parts_prop::any;

	if (term.date_val.target == df::date_parts_prop::created || is_any)
	{
		const auto md = file.metadata.load();

		if (md)
		{
			if (md->created_exif.is_valid())
			{
				return is_date_match(term.date_val, md->created_exif, term.modifiers, now_days);
			}
			if (md->created_utc.is_valid())
			{
				return is_date_match(term.date_val, md->created_utc.system_to_local(), term.modifiers, now_days);
			}
			if (md->created_digitized.is_valid())
			{
				return is_date_match(term.date_val, md->created_digitized, term.modifiers, now_days);
			}
			if (is_date_match(term.date_val, file.file_created, term.modifiers, now_days))
			{
				return true;
			}
		}
		else
		{
			if (is_date_match(term.date_val, file.file_created, term.modifiers, now_days))
			{
				return true;
			}
		}
	}

	if (term.date_val.target == df::date_parts_prop::modified || is_any)
	{
		if (is_date_match(term.date_val, file.file_modified, term.modifiers, now_days)) return true;
	}

	return false;
}


bool df::search_t::is_match(const prop::key& key, const date_t date) const
{
	auto date_term_count = 0;
	auto date_term_match = 0;

	for (const auto& term : _terms)
	{
		if (term.is_date())
		{
			static const search_term_modifier mod;
			const auto now_days = platform::now().to_days();

			date_term_count += 1;

			if (term.date_val.target == date_parts_prop::created &&
				(key == prop::created_utc || key == prop::created_digitized || key == prop::created_exif))
			{
				if (is_date_match(term.date_val, date, mod, now_days)) ++date_term_match;
			}
			else if (term.date_val.target == date_parts_prop::modified &&
				key == prop::modified)
			{
				if (is_date_match(term.date_val, date, mod, now_days)) ++date_term_match;
			}
			else if (term.date_val.target == date_parts_prop::any)
			{
				if (is_date_match(term.date_val, date, mod, now_days)) ++date_term_match;
			}
		}
	}

	return date_term_match > 0 && date_term_match >= date_term_count;
}

bool df::search_t::is_match(const prop::key& key, const int val) const
{
	auto term_count = 0;
	auto term_match = 0;

	for (const auto& term : _terms)
	{
		if (term.is_int())
		{
			term_count += 1;

			if (term.key == key)
			{
				const auto cmp = compare_term(term, val);
				const bool match_value = modifier_match(cmp, term.modifiers);
				if (match_value == term.modifiers.positive) ++term_match;
			}
		}
	}

	return term_match > 0 && term_match >= term_count;
}


static bool eq_ext(std::u8string_view ext1, std::u8string_view ext2)
{
	if (!ext1.empty() && ext1[0] == '.') ext1 = ext1.substr(1);
	if (!ext2.empty() && ext2[0] == '.') ext2 = ext2.substr(1);
	return str::icmp(ext1, ext2) == 0;
}

df::search_result df::search_matcher::match_term(const index_file_item& file, const search_term& term) const
{
	search_result result;

	if (term.type == search_term_type::media_type)
	{
		const bool match_file_group = term.fg_val == file.ft->group;

		if (match_file_group == term.modifiers.positive)
		{
			result.type = search_result_type::match_file_group;
		}
	}
	else if (term.type == search_term_type::extension)
	{
		const auto name = file.name.sv();
		const auto extension_pos = find_ext(name);
		const auto ext = name.substr(extension_pos);
		const bool match_ext = eq_ext(term.text, ext);

		if (match_ext == term.modifiers.positive)
		{
			result.type = search_result_type::match_ext;
		}
	}
	else if (term.type == search_term_type::location)
	{
		const auto md = file.metadata.load();
		const auto match_location =
			term.coord_val.is_valid() &&
			md &&
			md->has_gps() &&
			term.coord_val.distance_in_kilometers(md->coordinate) < term.float_val;


		if (match_location == term.modifiers.positive)
		{
			result.type = search_result_type::match_location;
		}
	}
	else if (term.type == search_term_type::duplicate)
	{
		const bool has_dups = file.duplicates.count > 1;

		if (has_dups == term.modifiers.positive)
		{
			result.type = search_result_type::similar;
		}
	}
	else if (term.type == search_term_type::text)
	{
		const auto match_text = compare_text(term, file);

		if (match_text.is_match() == term.modifiers.positive)
		{
			result = match_text;
		}
	}
	else if (term.is_date())
	{
		const bool match_date = is_date_match(term, file, _now_days);

		if (match_date == term.modifiers.positive)
		{
			result.type = search_result_type::match_date;
		}
	}
	else if (term.type == search_term_type::has_type)
	{
		if (file.ft != file_type::folder)
		{
			const bool match_has_type = has_type(term.key, file);

			if (match_has_type == term.modifiers.positive)
			{
				result.type = search_result_type::has_type;
			}
		}
	}
	else if (term.type == search_term_type::value)
	{
		const auto cmp = compare_val(term, file);
		const bool match_value = modifier_match(cmp, term.modifiers);

		if (match_value == term.modifiers.positive)
		{
			result.type = search_result_type::match_prop;
			result.key = term.key;
			result.text = cmp.val_matched;
		}
	}

	return result;
}

df::search_result df::search_matcher::match_item(const file_path path, const index_file_item& file) const
{
	//auto result = search_result::is_match;
	//const auto file_name = ps.read_string(prop::file_name);

	/*if (_search._terms.empty())
	{
		return _search.has_selector() ? search_result::match_folder : search_result::match_value;
	}*/

	search_result result;

	if (_search.has_related())
	{
		//auto same_signature = false;
		//auto same_album = false;

		const auto& related = _search.related();

		const auto same_signature = path == related.path ||
			is_dup_match(related, file);

		if (same_signature)
		{
			result.type = search_result_type::similar;
		}
		else if (related.crc32c != 0 && file.crc32c == related.crc32c)
		{
			result.type = search_result_type::similar;
		}
		else
		{
			return result;
		}

		/*const auto same_location =
			related.gps.is_valid() &&
			has_gps(ps) &&
			related.gps.distance_in_kilometers(gps_coordinate(ps.read_float(prop::latitude), ps.read_float(prop::longitude))) < 0.2;

		const auto same_date = related.created.is_valid() &&
			related.created.to_days() == ps.created().to_days();

		const auto similarity = same_signature || same_location;
		if (!similarity) return match_result::no_match;

		if (same_signature)
			result = match_result::same_signature;
		else if (same_date)
			result = match_result::similar_time;
		else if (same_location)
			result = match_result::similar_location;
		else if (same_album)
			result = match_result::same_album;*/
	}
	else if (_search.has_selector() && _search._terms.empty())
	{
		result.type = search_result_type::match_folder;
	}

	if (can_match_folder)
	{
		for (const auto& t : _search._terms)
		{
			if (t.type == search_term_type::text &&
				str::contains(t.text, u8"**"sv) &&
				wildcard_icmp(path.folder().text(), t.text))
			{
				result.type = search_result_type::match_folder;
				result.text = path.folder().text();
				return result;
			}
		}
	}

	if (_search._terms.empty())
	{
		return result;
	}

	if (!potential_match(file.bloom))
	{
		return {search_result_type::no_match};
	}

	return match_all_terms(file);
}

df::search_result df::search_matcher::match_all_terms(const index_file_item& file) const
{
	if (_search._terms.size() == 1)
	{
		// optimisation for single term
		return match_term(file, _search._terms[0]);
	}

	struct level
	{
		bool logical_and = true;
		bool state = true;
	};

	const auto max_levels = 32;
	level level_results[max_levels];
	auto current_level = 0;
	auto result_type = search_result_type::no_match;

	for (const auto& term : _search._terms)
	{
		const auto match_type = match_term(file, term);
		const auto is_match = match_type.is_match();
		result_type = match_type.merge_result_type(result_type);

		if (term.modifiers.begin_group > 0)
		{
			for (auto i = 0; i < term.modifiers.begin_group; i++)
			{
				if (current_level < max_levels - 1)
				{
					current_level += 1;
					level_results[current_level].logical_and = term.modifiers.logical_op !=
						search_term_modifier_bool::m_or;
					level_results[current_level].state = is_match;
				}
			}
		}
		else if (term.modifiers.logical_op != search_term_modifier_bool::m_or)
		{
			level_results[current_level].state &= is_match;
		}
		else
		{
			level_results[current_level].state |= is_match;
		}

		for (auto i = 0; i < term.modifiers.end_group; i++)
		{
			if (current_level > 0)
			{
				if (level_results[current_level].logical_and)
				{
					level_results[current_level - 1].state &= level_results[current_level].state;
				}
				else
				{
					level_results[current_level - 1].state |= level_results[current_level].state;
				}

				current_level -= 1;
			}
		}
	}

	//return { level_results[0].state ? result_type : search_result_type::no_match };
	return {level_results[0].state ? search_result_type::match_multiple : search_result_type::no_match};
}

df::search_result df::search_matcher::match_folder(const str::cached name) const
{
	if (_search.has_related())
	{
		return {};
	}

	index_file_item file;
	file.name = name;
	file.ft = file_type::folder;

	return match_all_terms(file);
}
