// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2024  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "pch.h"
#include "util.h"
#include "util_strings.h"

#include <cstdarg>


#include "app_text.h"
#include "util_text.h"
#include "model_propery.h"

static_assert(std::is_trivially_copyable_v<str::part_t>);
static_assert(std::is_trivially_copyable_v<str::cached>);

bool str::is_utf8(const char8_t* sz, const int len)
{
	static constexpr int byte_class_table[256] = {
		/*       00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F  */
		/* 00 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* 10 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* 20 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* 30 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* 40 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* 50 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* 60 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* 70 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		/* 80 */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		/* 90 */ 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
		/* A0 */ 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
		/* B0 */ 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
		/* C0 */ 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
		/* D0 */ 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
		/* E0 */ 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 8, 7, 7,
		/* F0 */ 9, 10, 10, 10, 11, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4
		/*       00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F  */
	};

	/* state table */
	enum utf8_state
	{
		kSTART = 0,
		kA,
		kB,
		kC,
		kD,
		kE,
		kF,
		kG,
		kERROR,
		kNumOfStates
	};

	static constexpr utf8_state state_table[] = {
		/*                            kSTART, kA,     kB,     kC,     kD,     kE,     kF,     kG,     kERROR */
		/* 0x00-0x7F: 0            */ kSTART, kERROR, kERROR, kERROR, kERROR, kERROR, kERROR, kERROR, kERROR,
		/* 0x80-0x8F: 1            */ kERROR, kSTART, kA, kERROR, kA, kB, kERROR, kB, kERROR,
		/* 0x90-0x9f: 2            */ kERROR, kSTART, kA, kERROR, kA, kB, kB, kERROR, kERROR,
		/* 0xa0-0xbf: 3            */ kERROR, kSTART, kA, kA, kERROR, kB, kB, kERROR, kERROR,
		/* 0xc0-0xc1, 0xf5-0xff: 4 */ kERROR, kERROR, kERROR, kERROR, kERROR, kERROR, kERROR, kERROR, kERROR,
		/* 0xc2-0xdf: 5            */ kA, kERROR, kERROR, kERROR, kERROR, kERROR, kERROR, kERROR, kERROR,
		/* 0xe0: 6                 */ kC, kERROR, kERROR, kERROR, kERROR, kERROR, kERROR, kERROR, kERROR,
		/* 0xe1-0xec, 0xee-0xef: 7 */ kB, kERROR, kERROR, kERROR, kERROR, kERROR, kERROR, kERROR, kERROR,
		/* 0xed: 8                 */ kD, kERROR, kERROR, kERROR, kERROR, kERROR, kERROR, kERROR, kERROR,
		/* 0xf0: 9                 */ kF, kERROR, kERROR, kERROR, kERROR, kERROR, kERROR, kERROR, kERROR,
		/* 0xf1-0xf3: 10           */ kE, kERROR, kERROR, kERROR, kERROR, kERROR, kERROR, kERROR, kERROR,
		/* 0xf4: 11                */ kG, kERROR, kERROR, kERROR, kERROR, kERROR, kERROR, kERROR, kERROR
	};

	auto current = kSTART;
	const auto* p = std::bit_cast<const uint8_t*>(sz);
	const auto* const limit = p + len;

	while (p < limit&& kERROR != current)
	{
		current = state_table[(byte_class_table[*p++] * kNumOfStates) + current];
	}

	return current == kSTART;
}

static constexpr uint8_t BOM_UTF8[] = { 0xef, 0xbb, 0xbf }; // UTF-8
static constexpr uint8_t BOM_UTF16_LE[] = { 0xff, 0xfe }; // UTF-16, little-endian
static constexpr uint8_t BOM_UTF16_BE[] = { 0xfe, 0xff }; // UTF-16, big-endian
static constexpr uint8_t BOM_UTF32_LE[] = { 0xff, 0xfe, 0x00, 0x00 }; // UTF-32, little-endian
static constexpr uint8_t BOM_UTF32_BE[] = { 0x00, 0x00, 0xfe, 0xff }; // UTF-32, big-endian

bool str::is_utf16(const uint8_t* sz, const int len)
{
	return len > 4 && sz[0] == 0 && sz[2] == 0; // if every other char8_t is zero then likely utf16
}

static bool is_utf7(const char8_t* p, const int len)
{
	const auto* const limit = p + len;

	while (p < limit)
	{
		if (*p & 0x80 || !*p) return false;
		p++;
	}

	return true;
}

//static uint32_t DetectEncoding(const char8_t * string, int len)
//{
//	if (string && len)
//	{
//		if (Ips(string, len)) return CP_UTF8;
//		if (IsUTF7(string, len)) return CP_UTF7;
//	}
//
//	return CP_ACP;
//}

//static std::u8string format_args(const char8_t* szFormat, const va_list& argList)
//{
//	const auto length = _vscprintf(szFormat, argList);
//	std::u8string result(length + 1, 0);
//	vstr::format2(&result[0], length + 1, szFormat, argList);
//	return result;
//}

//std::u8string str::format(const char8_t* szFormat, ...)
//{
//	va_list argList;
//	va_start(argList, szFormat);
//	auto result = format_args(szFormat, argList);
//	va_end(argList);
//	return result;
//}


str::format_arg::format_arg(const df::file_path value) : type(FILE_PATH), text_value(value.folder().text()),
text_value2(value.name())
{
}

str::format_arg::format_arg(const df::folder_path value) : type(FOLDER_PATH), text_value(value.text())
{
}

str::format_arg::format_arg(const df::date_t value) : type(DATE)
{
	u.int64_value = value.to_int64();
}

static int dec_places(const std::u8string_view fmt)
{
	const auto dot_pos = fmt.find('.');
	if (dot_pos != std::u8string_view::npos) return str::to_int(fmt.substr(dot_pos + 1));
	return -1;
}

template <typename inserter>
static void copy_sv(inserter&& result, const std::u8string_view sv, const uint32_t result_width = 0,
	const bool is_zero_padded = false)
{
	if (result_width > sv.size())
	{
		std::fill_n(result, result_width - sv.size(), is_zero_padded ? '0' : ' ');
	}

	std::copy(sv.begin(), sv.end(), result);
}

template <typename inserter>
static void format_render_arg(inserter&& result, const str::format_arg& arg, const std::u8string_view fmt)
{
	const auto is_hex = !fmt.empty() && (fmt.back() == 'x' || fmt.back() == 'X');
	const auto is_zero_padded = !fmt.empty() && fmt.front() == '0';
	const auto result_width = fmt.empty() ? 0 : str::to_int(fmt);

	if (is_hex)
	{
		switch (arg.type)
		{
		case str::format_arg::INT64:
		case str::format_arg::UINT64:
		case str::format_arg::FILE_SIZE:
		case str::format_arg::DATE:
			copy_sv(result, str::to_hex(std::bit_cast<const uint8_t*>(&arg.u.uint64_value), sizeof(arg.u.uint64_value),
				true, true), result_width, is_zero_padded);
			break;
		case str::format_arg::INT32:
		case str::format_arg::UINT32:
			copy_sv(result, str::to_hex(std::bit_cast<const uint8_t*>(&arg.u.uint32_value), sizeof(arg.u.uint32_value),
				true, true), result_width, is_zero_padded);
			break;
		case str::format_arg::UINT16:
			copy_sv(result, str::to_hex(std::bit_cast<const uint8_t*>(&arg.u.uint16_value), sizeof(arg.u.uint16_value),
				true, true), result_width, is_zero_padded);
			break;
		default:
			df::assert_true(false);
			break;
		}
	}
	else
	{
		switch (arg.type)
		{
		case str::format_arg::INT64:
			copy_sv(result, str::to_string(arg.u.int64_value), result_width, is_zero_padded);
			break;
		case str::format_arg::UINT64:
			copy_sv(result, str::to_string(arg.u.uint64_value), result_width, is_zero_padded);
			break;
		case str::format_arg::INT32:
			copy_sv(result, str::to_string(arg.u.int32_value), result_width, is_zero_padded);
			break;
		case str::format_arg::UINT32:
			copy_sv(result, str::to_string(arg.u.uint32_value), result_width, is_zero_padded);
			break;
		case str::format_arg::UINT16:
			copy_sv(result, str::to_string(arg.u.uint16_value), result_width, is_zero_padded);
			break;
		case str::format_arg::DOUBLE:
			copy_sv(result, str::to_string(arg.u.dbl_value, dec_places(fmt)), result_width, is_zero_padded);
			break;
		case str::format_arg::TEXT:
		case str::format_arg::FOLDER_PATH:
			copy_sv(result, arg.text_value, result_width, is_zero_padded);
			break;
		case str::format_arg::FILE_PATH:
			copy_sv(result, arg.text_value);
			copy_sv(result, u8"\\"sv);
			copy_sv(result, arg.text_value2);
			break;
		case str::format_arg::FILE_SIZE:
		{
			const df::file_size s(arg.u.int64_value);
			copy_sv(result, prop::format_size(s), result_width, is_zero_padded);
		}
		break;
		case str::format_arg::DATE:
		{
			const df::date_t d(arg.u.int64_value);
			copy_sv(result, platform::format_date(d));
			*result++ = ' ';
			copy_sv(result, platform::format_time(d));
		}
		break;
		default:
			df::assert_true(false);
			break;
		}
	}
}

std::u8string str::format_impl(const std::u8string_view fmt, const format_arg* args, const size_t num_args)
{
	std::u8string result;
	result.reserve(fmt.size());
	size_t arg_i = 0;

	auto i = fmt.begin();
	const auto end = fmt.end();
	auto inserter = std::back_inserter(result);

	while (i < end)
	{
		auto c = pop_utf8_char(i, end);

		if (c == '{')
		{
			if (i >= end)
			{
				break;
			}

			c = pop_utf8_char(i, end);

			if (c == '{')
			{
				char32_to_utf8(inserter, c);
			}
			else
			{
				bool has_arg_index = false;
				int arg_index = 0;

				bool has_format = false;
				size_t format_start = 0;
				size_t format_len = 0;

				while (isdigit(c) && i < end)
				{
					arg_index = (arg_index * 10) + (c - '0');
					has_arg_index = true;
					c = pop_utf8_char(i, end);
				}

				if (c == ':' && i < end)
				{
					has_format = true;
					format_start = std::distance(fmt.begin(), i);
					c = pop_utf8_char(i, end);

					while (c != '}' && i < end)
					{
						format_len += 1;
						c = pop_utf8_char(i, end);
					}
				}

				if (c == '}')
				{
					const auto aa = has_arg_index ? arg_index : arg_i;
					format_render_arg(inserter, args[aa], fmt.substr(format_start, format_len));
					if (!has_arg_index) arg_i += 1;
				}
			}
		}
		else
		{
			if (c == '}')
			{
				if (i >= end)
				{
					break;
				}

				c = pop_utf8_char(i, end);

				if (c != '}')
				{
					char32_to_utf8(inserter, c);
				}
			}

			char32_to_utf8(inserter, c);
		}
	}

	return result;
}

std::u8string str::print(const std::u8string_view svformat, ...)
{
	const std::string szFormat = str::utf8_cast2(svformat);
	va_list argList;
	va_start(argList, svformat);
	const auto* const format = std::bit_cast<const char*>(szFormat.c_str());
	const auto result_length = _vscprintf(szFormat.c_str(), argList);
	std::u8string result;
	result.resize(result_length + 1, 0);
	vsprintf_s(std::bit_cast<char*>(result.data()), result_length + 1, format, argList);
	result.resize(result_length);
	va_end(argList);
	return result;
}

std::u8string str::print(__in_z __format_string const char8_t* szFormat, ...)
{
	va_list argList;
	va_start(argList, szFormat);
	const auto* const format = std::bit_cast<const char*>(szFormat);
	const auto result_length = _vscprintf(format, argList);
	std::u8string result;
	result.resize(result_length + 1, 0);
	vsprintf_s(std::bit_cast<char*>(result.data()), result_length + 1, format, argList);
	result.resize(result_length);
	va_end(argList);
	return result;
}

struct string_index_hash
{
	uint32_t operator()(const std::u8string_view sv) const
	{
		return crypto::crc32c(sv.data(), sv.size());
	}
};

struct string_index_eq
{
	bool operator()(const std::u8string_view l, const std::u8string_view r) const
	{
		return l.compare(r) == 0;
	}
};


//struct string_index_less
//{
//	bool operator()(const str::chached_string_storage_t* l, const str::chached_string_storage_t* r) const
//	{
//		if (l->hash == r->hash) return std::u8string_view(l->sz, l->len).compare(std::u8string_view(r->sz, r->len)) < 0;
//		return l->hash < r->hash;
//	}
//
//	bool operator()(const str::chached_string_storage_t* l, const string_index_key&r) const
//	{
//		if (l->hash == r.hash) return std::u8string_view(l->sz, l->len).compare(r.sv) < 0;
//		return l->hash < r.hash;
//	}
//
//	bool operator()(const string_index_key&l, const str::chached_string_storage_t* r) const
//	{
//		if (l.hash == r->hash) return l.sv.compare(std::u8string_view(r->sz, r->len)) < 0;
//		return l.hash < r->hash;
//	}
//};


struct string_index_t
{
	using K = std::u8string_view;
	using V = str::chached_string_storage_t*;
	using storage_t = phmap::parallel_flat_hash_map<K, V, string_index_hash, string_index_eq, std::allocator<std::pair<const K, V>>, 4, platform::mutex>;
	storage_t _storage;
	platform::memory_pool _pool;

	str::chached_string_storage_t* make_entry(const std::u8string_view sv)
	{
		const auto len = sv.size();
		const auto allocation = sizeof(str::chached_string_storage_t) + (len + 1) * sizeof(char8_t);
		auto* const copy = static_cast<str::chached_string_storage_t*>(_pool.alloc(allocation));

		copy->len = static_cast<uint32_t>(len);
		memcpy_s(copy->sz, allocation, sv.data(), len * sizeof(char8_t));
		copy->sz[len] = 0;

		return copy;
	}

	str::cached find_or_insert(const std::u8string_view sv)
	{
		if (sv.empty() || sv.size() > platform::memory_pool::block_size) return {};
		str::chached_string_storage_t* result = nullptr;

		const auto exists = [&result](const std::pair<const K, V>& kv) { result = kv.second; };
		const auto emplace = [this, sv, &result](const storage_t::constructor& ctor)
			{
				result = make_entry(sv);
				ctor(std::u8string_view(result->sz, result->len), result);
			};

		_storage.lazy_emplace_l(sv, exists, emplace);

		return { result };
	}
};

static string_index_t& string_index()
{
	static string_index_t index;
	return index;
}

str::cached str::cache(const std::u8string_view sr)
{
	if (sr.empty()) return {};
	return string_index().find_or_insert(sr);
}

str::cached str::cache(const std::string_view sr)
{
	if (sr.empty()) return {};
	return string_index().find_or_insert(utf8_cast(sr));
}

str::cached str::cache(const std::wstring_view sr)
{
	if (sr.empty()) return {};
	return string_index().find_or_insert(utf16_to_utf8(sr));
}

std::u8string_view str::strip(const std::u8string_view s)
{
	if (s.empty()) return {};

	constexpr auto WHITESPACE = u8"\0"sv;
	const auto start = s.find_first_not_of(WHITESPACE);
	if (start == std::u8string::npos) return {};

	const auto end = s.find_last_not_of(WHITESPACE);
	if (end == std::u8string::npos) return {};

	return s.substr(start, end - start + 1);
}

std::u8string_view str::trim(const std::u8string_view s)
{
	if (s.empty()) return {};

	constexpr auto WHITESPACE = u8" \n\r\t\f\v\0"sv;
	const auto start = s.find_first_not_of(WHITESPACE);
	if (start == std::u8string::npos) return {};

	const auto end = s.find_last_not_of(WHITESPACE);
	if (end == std::u8string::npos) return {};

	return s.substr(start, end - start + 1);
}

std::wstring_view str::trim(const std::wstring_view s)
{
	if (s.empty()) return {};

	constexpr auto WHITESPACE = L" \n\r\t\f\v\0"sv;
	const auto start = s.find_first_not_of(WHITESPACE);
	if (start == std::u8string::npos) return {};

	const auto end = s.find_last_not_of(WHITESPACE);
	if (end == std::u8string::npos) return {};

	return s.substr(start, end - start + 1);
}

std::string_view str::trim(const std::string_view s)
{
	if (s.empty()) return {};

	constexpr auto WHITESPACE = " \n\r\t\f\v\0"sv;
	const auto start = s.find_first_not_of(WHITESPACE);
	if (start == std::u8string::npos) return {};

	const auto end = s.find_last_not_of(WHITESPACE);
	if (end == std::u8string::npos) return {};

	return s.substr(start, end - start + 1);
}

bool str::is_num(const std::u8string_view sv)
{
	for (const auto& c : sv)
	{
		if (!iswdigit(c) && c != '.' && c != ',')
			return false;
	}

	return true;
}


bool str::is_probably_num(const std::u8string_view sv)
{
	int digits = 0;

	for (const auto& c : sv)
	{
		if (!iswdigit(c) && c != '.' && c != ',' && c != '-')
			break;

		digits += 1;
	}

	return digits > 0;
}

bool str::ends(const std::u8string_view text, const std::u8string_view sub_string)
{
	auto text_len = text.size();
	auto sub_len = sub_string.size();

	while (text_len > 0 && sub_len > 0 && to_lower(text[text_len - 1]) == to_lower(sub_string[sub_len - 1]))
	{
		text_len -= 1;
		sub_len -= 1;
	}

	return sub_len == 0;
}

bool str::starts(const std::u8string_view text, const std::u8string_view sub_string)
{
	if (text == sub_string) return true;
	if (text.size() < sub_string.size()) return false;

	auto text_p = text.begin();
	auto sub_p = sub_string.begin();
	const auto text_end = text.end();
	const auto sub_end = sub_string.end();

	while (text_p < text_end && sub_p < sub_end)
	{
		const auto text_match_char = normalze_for_compare(pop_utf8_char(text_p, text_end));
		const auto sub_match_char = normalze_for_compare(pop_utf8_char(sub_p, sub_end));

		if (text_match_char != sub_match_char)
			return false;
	}

	return true;
}

std::u8string_view::size_type str::ifind(const std::u8string_view text, const std::u8string_view sub_string)
{
	if (!text.empty() && !sub_string.empty())
	{
		auto text_p = text.begin();
		auto sub_p = sub_string.begin();
		const auto text_end = text.end();
		const auto sub_end = sub_string.end();
		const auto first_sub_char = normalze_for_compare(pop_utf8_char(sub_p, sub_end));

		while (text_p < text_end && sub_p <= sub_end)
		{
			const auto text_start = text_p;
			const auto text_char = normalze_for_compare(pop_utf8_char(text_p, text_end));

			if (text_char == first_sub_char) // Is matching?
			{
				auto text_match = text_p;
				auto sub_match = sub_p;
				auto matching = true;

				while (matching && sub_match < sub_end && text_match < text_end)
				{
					const auto text_match_char = normalze_for_compare(pop_utf8_char(text_match, text_end));
					const auto sub_match_char = normalze_for_compare(pop_utf8_char(sub_match, sub_end));

					matching = text_match_char == sub_match_char;
				}

				if (matching && sub_match == sub_end)
					return text_start - text.begin();
			}
		}
	}

	return std::u8string_view::npos;
}

str::find_result str::ifind2(const std::u8string_view text, const std::u8string_view sub_string,
	const size_t parts_offset)
{
	find_result result;
	std::vector<part_t> parts;

	if (!text.empty() && !sub_string.empty())
	{
		auto text_p = text.begin();
		auto sub_p = sub_string.begin();
		const auto text_end = text.end();
		const auto sub_end = sub_string.end();
		auto first_sub_char = normalze_for_compare(pop_utf8_char(sub_p, sub_end));

		while (first_sub_char == 0x20 && sub_p < sub_end)
		{
			first_sub_char = normalze_for_compare(pop_utf8_char(sub_p, sub_end));
		}

		while (text_p < text_end && sub_p <= sub_end)
		{
			const auto text_char = normalze_for_compare(pop_utf8_char(text_p, text_end));

			if (text_char == first_sub_char) // Is matching?
			{
				const auto match_start = static_cast<size_t>(std::distance(text.begin(), text_p)) - 1u;
				auto match_len = 0u;

				auto text_match = text_p;
				auto sub_match = sub_p;
				auto matching = true;
				auto sub_match_char = first_sub_char;

				while (matching && sub_match < sub_end && text_match < text_end)
				{
					const auto text_match_char = normalze_for_compare(pop_utf8_char(text_match, text_end));
					sub_match_char = normalze_for_compare(pop_utf8_char(sub_match, sub_end));

					matching = text_match_char == sub_match_char && sub_match_char != 0x20;
					match_len += 1;
				}

				if (matching && sub_match == sub_end)
				{
					parts.emplace_back(match_start + parts_offset, match_len + 1);

					result.found = true;
					result.parts = std::move(parts);
					break;
				}

				// word match scan for remaining
				if (sub_match_char == 0x20 && match_len > 0 && sub_match < sub_end && text_match < text_end)
				{
					while (sub_match_char == 0x20 && sub_match < sub_end)
					{
						sub_match_char = normalze_for_compare(pop_utf8_char(sub_match, sub_end));
					}

					parts.emplace_back(match_start + parts_offset, match_len);

					first_sub_char = sub_match_char;
					text_p = text_match;
					sub_p = sub_match;
				}
			}
		}
	}

	return result;
}

bool str::is_quote(const wchar_t c)
{
	return c == L'\"' || c == L'\'';
}

void str::split2(const std::u8string_view text, const bool detect_quotes,
	const std::function<void(std::u8string_view)>& inserter, const std::function<bool(wchar_t)>& pred)
{
	if (!text.empty())
	{
		auto in_quotes = false;
		char8_t quote_char = 0;
		auto i = 0u;
		auto split_start = std::u8string_view::npos;
		auto split_len = 0;

		while (i < text.size())
		{
			const auto c = text[i];

			if (is_quote(c) && (quote_char == 0 || quote_char == c) && detect_quotes)
			{
				in_quotes = !in_quotes;
				quote_char = in_quotes ? c : 0;
			}
			else if (in_quotes || !pred(c))
			{
				if (split_start == std::u8string_view::npos)
				{
					split_start = i;
				}

				split_len += 1;
			}
			else
			{
				if (split_start != std::u8string_view::npos && split_len > 0)
				{
					const auto part = text.substr(split_start, split_len);

					if (!part.empty())
					{
						inserter(part);
					}
				}

				split_start = std::u8string_view::npos;
				split_len = 0;
			}

			++i;
		}

		if (split_start != std::u8string_view::npos && split_len > 0)
		{
			const auto last = text.substr(split_start, split_len);

			if (!last.empty())
			{
				inserter(last);
			}
		}
	}
};


df::string_map str::split_url_params(const std::u8string_view params)
{
	const auto copy = std::u8string(params);
	df::string_map results;
	u8istringstream ss(copy);
	std::u8string item;

	while (std::getline(ss, item, u8'&'))
	{
		const auto split = item.find(u8'=');

		if (split != std::wstring::npos)
		{
			results[item.substr(0, split)] = item.substr(split + 1);
		}
	}

	return results;
}

df::string_map str::extract_url_params(const std::u8string_view url)
{
	df::string_map results;
	auto found = url.find(u8'?');

	if (found != std::wstring::npos)
	{
		results = split_url_params(url.substr(found + 1));
	}
	else
	{
		found = url.find(u8'#');

		if (found != std::wstring::npos)
		{
			results = split_url_params(url.substr(found + 1));
		}
	}

	return results;
}

std::u8string str::replace(const std::u8string_view s, const std::u8string_view find,
	const std::u8string_view replacement)
{
	auto result = std::u8string(s);
	size_t pos = 0;
	const auto find_length = find.size();
	const auto replacement_length = replacement.size();

	while ((pos = result.find(find, pos)) != std::u8string::npos)
	{
		result.replace(pos, find_length, replacement);
		pos += replacement_length;
	}

	return result;
}

static constexpr std::u8string_view months[] =
{
	{},
	u8"January"sv,
	u8"February"sv,
	u8"March"sv,
	u8"April"sv,
	u8"May"sv,
	u8"June"sv,
	u8"July"sv,
	u8"August"sv,
	u8"September"sv,
	u8"October"sv,
	u8"November"sv,
	u8"December"sv,
};

static constexpr std::u8string_view short_months[] =
{
	{},
	u8"jan"sv,
	u8"feb"sv,
	u8"mar"sv,
	u8"apr"sv,
	u8"may"sv,
	u8"jun"sv,
	u8"jul"sv,
	u8"aug"sv,
	u8"sep"sv,
	u8"oct"sv,
	u8"nov"sv,
	u8"dec"sv,
};

static const std::u8string_view tt_months(const int m)
{
	const std::u8string_view result[] =
	{
		{},
		tt.month_january,
		tt.month_february,
		tt.month_march,
		tt.month_april,
		tt.month_may,
		tt.month_june,
		tt.month_july,
		tt.month_august,
		tt.month_september,
		tt.month_october,
		tt.month_november,
		tt.month_december,
	};

	return result[std::clamp(m, 1, 12)];
}

static const std::u8string_view tt_short_months(const int m)
{
	const std::u8string_view result[] =
	{
		{},
		tt.month_short_jan,
		tt.month_short_feb,
		tt.month_short_mar,
		tt.month_short_apr,
		tt.month_short_may,
		tt.month_short_jun,
		tt.month_short_jul,
		tt.month_short_aug,
		tt.month_short_sep,
		tt.month_short_oct,
		tt.month_short_nov,
		tt.month_short_dec,
	};

	return result[std::clamp(m, 1, 12)];
}

std::u8string_view str::month(const int m, const bool translate)
{
	const auto n = std::clamp(m, 1, 12);
	return translate ? tt_months(n) : months[n];
}

std::u8string_view str::short_month(const int m, const bool translate)
{
	const auto n = std::clamp(m, 1, 12);
	return translate ? tt_short_months(n) : short_months[n];
}

int str::month(const std::u8string_view r)
{
	for (int i = 1; i <= 12; i++)
		if (icmp(months[i], r) == 0)
			return i;

	if (r.size() == 3)
	{
		for (int i = 1; i <= 12; i++)
			if (icmp(short_months[i], r) == 0)
				return i;
	}

	for (int i = 1; i <= 12; i++)
		if (icmp(tt_months(i), r) == 0)
			return i;

	if (r.size() == 3)
	{
		for (int i = 1; i <= 12; i++)
			if (icmp(tt_short_months(i), r) == 0)
				return i;
	}

	return 0;
}

std::u8string str::quote_if_white_space(const std::u8string_view s)
{
	if (need_quotes(s))
	{
		const auto q = s.find(L'\"') == std::u8string::npos ? '"' : '\'';

		std::u8string result;
		result.reserve(s.size() + 2);
		result = q;
		result += s;
		result += q;
		return result;
	}

	return std::u8string(s);
}

std::u8string str::to_string(const bool v)
{
	return std::u8string(v ? tt.text_true : tt.text_false);
}

std::u8string str::to_string(const int v)
{
	static constexpr int size = 64;
	char8_t result[size];
	result[0] = 0;
	_itoa_s(v, std::bit_cast<char*>(static_cast<char8_t*>(result)), size, 10);
	return result;
}

std::u8string str::to_string(const uint32_t v)
{
	static constexpr int size = 64;
	char8_t result[size];
	result[0] = 0;
	_ultoa_s(v, std::bit_cast<char*>(static_cast<char8_t*>(result)), size, 10);
	return result;
}

std::u8string str::to_string(const long v)
{
	static constexpr int size = 64;
	char8_t result[size];
	result[0] = 0;
	_ltoa_s(v, std::bit_cast<char*>(static_cast<char8_t*>(result)), size, 10);
	return result;
}

std::u8string str::to_string(const int64_t v)
{
	static constexpr int size = 128;
	char8_t result[size];
	result[0] = 0;
	_i64toa_s(v, std::bit_cast<char*>(static_cast<char8_t*>(result)), size, 10);
	return result;
}

std::u8string str::to_string(const uint64_t v)
{
	static constexpr int size = 128;
	char8_t result[size];
	result[0] = 0;
	_ui64toa_s(v, std::bit_cast<char*>(static_cast<char8_t*>(result)), size, 10);
	return result;
}

std::u8string str::to_string(const double v, int num_digits)
{
	std::u8string result;
	static constexpr int size = 128;
	char8_t text[size];
	text[0] = 0;
	int decimal = 0;
	int sign = 0;
	bool strip_trailing_zeros = false;
	const auto sep = std::use_facet<std::numpunct<char8_t>>(std::locale()).decimal_point();

	if (num_digits == -1)
	{
		num_digits = 6;
		strip_trailing_zeros = true;
	}

	if (0 == _fcvt_s(std::bit_cast<char*>(static_cast<char8_t*>(text)), size, v, num_digits, &decimal, &sign))
	{
		if (v < 0.0)
		{
			result += '-';
		}

		if (num_digits > 0 && decimal >= 1.0)
		{
			result.append(text, text + decimal);
			result += sep;
			result.append(text + decimal);
		}
		else if (decimal < 1.0)
		{
			result += '0';
			result += sep;
			for (auto i = 0; i > decimal; i--) result += '0';
			result += text;
		}
		else
		{
			result = text;
			strip_trailing_zeros = false;
		}

		if (strip_trailing_zeros)
		{
			const auto last_non_zero = result.find_last_not_of('0');

			if (last_non_zero != std::u8string::npos && result[last_non_zero] != '.')
			{
				result.resize(last_non_zero + 1);
			}
		}
	}

	return result;
}

std::u8string str::to_string(const sizei v)
{
	return format(u8"{}x{}"sv, v.cx, v.cy);
}

std::u8string str::format_count(const uint64_t total, const bool show_zero)
{
	std::u8string result;

	if (show_zero || total > 0)
	{
		if (total > 900000)
		{
			result = format(u8"{}m"sv, df::round64(total, 1000000ull));
		}
		else if (total >= 900)
		{
			result = format(u8"{}k"sv, df::round64(total, 1000ull));
		}
		else
		{
			result = to_string(total);
		}
	}

	return result;
}

std::u8string str::format_seconds(const int val)
{
	char sz[64] = { 0 };

	const auto secs = (val % 60);
	const auto mins = ((val / 60) % 60);
	const auto hours = ((val / (60 * 60)) % 60);

	if (hours)
	{
		sprintf_s(sz, "%d:%02d:%02d", hours, mins, secs);
	}
	else if (mins)
	{
		sprintf_s(sz, "%d:%02d", mins, secs);
	}
	else
	{
		_itoa_s(val, sz, 64, 10);
	}

	return utf8_cast2(sz);
}

int32_t str::to_int(const std::u8string_view sv)
{
	int result = 0;
	auto from_result = std::from_chars(std::bit_cast<const char*>(sv.data()),
		std::bit_cast<const char*>(sv.data() + sv.size()), result);
	// todo
	return result;
}

int64_t str::to_int64(const std::u8string_view r)
{
	int64_t result = 0;

	for (const auto c : r)
	{
		if (iswdigit(c))
		{
			result = (result * 10) + (c - '0');
		}
	}

	return result;
}

uint32_t str::to_uint(const std::u8string_view r)
{
	uint32_t result = 0;

	for (const auto c : r)
	{
		if (iswdigit(c))
		{
			result = (result * 10) + (c - '0');
		}
	}

	return result;
}

double str::to_double(const std::u8string_view r)
{
	auto a = 0.0;
	auto e = 0;
	auto c = r.begin();
	const auto end = r.end();
	auto top_sign = 1;

	while (c < end && is_white_space(*c))
	{
		++c;
	}

	if (c < end)
	{
		if (*c == '+')
		{
			++c;
		}
		else if (*c == '-')
		{
			top_sign = -1;
			++c;
		}
	}

	while (c < end && is_white_space(*c))
	{
		++c;
	}

	while (c < end && iswdigit(*c))
	{
		a = a * 10.0 + (*c - '0');
		++c;
	}

	if (c < end && *c == '.')
	{
		++c;

		while (c < end && iswdigit(*c))
		{
			a = a * 10.0 + (*c - '0');
			e = e - 1;
			++c;
		}
	}

	if (c < end && (*c == 'e' || *c == 'E'))
	{
		int sign = 1;
		int i = 0;

		++c;

		if (c < end)
		{
			if (*c == '+')
			{
				++c;
			}
			else if (*c == '-')
			{
				sign = -1;
				++c;
			}

			++c;

			while (c < end && iswdigit(*c))
			{
				i = i * 10 + (*c - '0');
				++c;
			}
			e += i * sign;
		}
	}

	while (e > 0)
	{
		a *= 10.0;
		e--;
	}

	while (e < 0)
	{
		a *= 0.1;
		e++;
	}

	return a * top_sign;
}

using word_counts_t = df::hash_map<std::u8string_view, int, df::ihash, df::ieq>;

df::string_counts top_totals(const word_counts_t& counts, int limit)
{
	std::vector<std::pair<int, std::u8string_view>> all;
	all.reserve(all.size());

	for (const auto& i : counts)
	{
		all.emplace_back(i.second, i.first);
	}

	std::ranges::sort(all, [](auto&& l, auto&& r) { return l.first > r.first; });

	df::string_counts results;

	for (const auto& i : all)
	{
		if (limit-- > 0)
		{
			results[i.second] = i.first;
		}
	}

	return results;
}

static const df::hash_set<std::u8string_view, df::ihash, df::ieq> unwanted_english_word = {
	u8"and"sv,
};

static bool is_range_separator(const wchar_t c)
{
	return iswpunct(c) || iswspace(c);
}

static bool is_num(const std::u8string_view text)
{
	return !text.empty() && std::find_if(text.begin(),
		text.end(), [](int c) { return !std::isdigit(c); }) == text.end();
}

static bool is_num_with_prefix(const std::u8string_view text, const std::u8string_view prefix)
{
	return text.rfind(prefix, 0) == 0 && is_num(text.substr(prefix.size()));
}

static bool is_wanted_word(const std::u8string_view text)
{
	if (text.size() <= 2) return false;
	if (is_num(text)) return false;
	if (is_num_with_prefix(text, u8"DSC"sv)) return false;
	if (is_num_with_prefix(text, u8"DSCF"sv)) return false;
	if (is_num_with_prefix(text, u8"Image"sv)) return false;
	if (is_num_with_prefix(text, u8"IMG_"sv)) return false;
	if (is_num_with_prefix(text, u8"IMGP"sv)) return false;
	if (is_num_with_prefix(text, u8"MVI_"sv)) return false;
	if (is_num_with_prefix(text, u8"PIC_"sv)) return false;
	if (is_num_with_prefix(text, u8"SN"sv)) return false;
	return !unwanted_english_word.contains(text);
}

void str::count_ranges(df::dense_string_counts& totals, const std::u8string_view text)
{
	if (!text.empty())
	{
		auto i = 0u;
		auto split_start = std::u8string_view::npos;
		auto split_len = 0;

		while (i < text.size())
		{
			const auto c = text[i];

			if (!is_range_separator(c))
			{
				if (split_start == std::u8string_view::npos)
				{
					split_start = i;
				}

				split_len += 1;
			}
			else
			{
				if (split_start != std::u8string_view::npos && split_len > 0)
				{
					const auto part = trim(text.substr(split_start, split_len));

					if (!part.empty() && is_wanted_word(part))
					{
						++totals[part];
					}
				}

				split_start = std::u8string_view::npos;
				split_len = 0;
			}

			++i;
		}

		if (split_start != std::u8string_view::npos && split_len > 0)
		{
			const auto last = trim(text.substr(split_start, split_len));

			if (!last.empty() && is_wanted_word(last))
			{
				++totals[last];
			}
		}
	}
}

df::string_counts str::guess_word(df::string_counts& counts, const std::u8string_view pattern)
{
	const auto max_results = 16;

	word_counts_t totals;

	for (const auto& entry : counts)
	{
		if (contains(entry.first, pattern))
		{
			totals[entry.first] += entry.second;
		}
	}

	return top_totals(totals, max_results);
}

static df::dense_hash_map<int, char32_t> make_normalizations()
{
	df::dense_hash_map<int, char32_t> normalizations = {
		{0x20, ' '}, // space 
		{0x0c, ' '}, // form feed 
		{0x0a, ' '}, // line feed 
		{0x0d, ' '}, // carriage return 
		{0x09, ' '}, // horizontal tab 
		{0x0b, ' '}, // vertical tab 
		{0x0041, 'a'},
		{0x0042, 'b'},
		{0x0043, 'c'},
		{0x0044, 'd'},
		{0x0045, 'e'},
		{0x0046, 'f'},
		{0x0047, 'g'},
		{0x0048, 'h'},
		{0x0049, 'i'},
		{0x004a, 'j'},
		{0x004b, 'k'},
		{0x004c, 'l'},
		{0x004d, 'm'},
		{0x004e, 'n'},
		{0x004f, 'o'},
		{0x0050, 'p'},
		{0x0051, 'q'},
		{0x0052, 'r'},
		{0x0053, 's'},
		{0x0054, 't'},
		{0x0055, 'u'},
		{0x0056, 'v'},
		{0x0057, 'w'},
		{0x0058, 'x'},
		{0x0059, 'y'},
		{0x005a, 'z'},
		{0x0061, 'a'},
		{0x0062, 'b'},
		{0x0063, 'c'},
		{0x0064, 'd'},
		{0x0065, 'e'},
		{0x0066, 'f'},
		{0x0067, 'g'},
		{0x0068, 'h'},
		{0x0069, 'i'},
		{0x006a, 'j'},
		{0x006b, 'k'},
		{0x006c, 'l'},
		{0x006d, 'm'},
		{0x006e, 'n'},
		{0x006f, 'o'},
		{0x0070, 'p'},
		{0x0071, 'q'},
		{0x0072, 'r'},
		{0x0073, 's'},
		{0x0074, 't'},
		{0x0075, 'u'},
		{0x0076, 'v'},
		{0x0077, 'w'},
		{0x0078, 'x'},
		{0x0079, 'y'},
		{0x007a, 'z'},
		{0x00aa, 'a'}, // feminine ordinal indicator
		{0x00ba, 'o'}, // masculine ordinal indicator
		{0x00c0, 'a'}, // latin capital letter a with grave
		{0x00c1, 'a'}, // latin capital letter a with acute
		{0x00c2, 'a'}, // latin capital letter a with circumflex
		{0x00c3, 'a'}, // latin capital letter a with tilde
		{0x00c4, 'a'}, // latin capital letter a with diaeresis
		{0x00c5, 'a'}, // latin capital letter a with ring above
		{0x00c7, 'c'}, // latin capital letter c with cedilla
		{0x00c8, 'e'}, // latin capital letter e with grave
		{0x00c9, 'e'}, // latin capital letter e with acute
		{0x00ca, 'e'}, // latin capital letter e with circumflex
		{0x00cb, 'e'}, // latin capital letter e with diaeresis
		{0x00cc, 'i'}, // latin capital letter i with grave
		{0x00cd, 'i'}, // latin capital letter i with acute
		{0x00ce, 'i'}, // latin capital letter i with circumflex
		{0x00cf, 'i'}, // latin capital letter i with diaeresis
		{0x00d0, 'd'}, // latin capital letter eth -- no decomposition  	// eth [d for vietnamese]
		{0x00d1, 'n'}, // latin capital letter n with tilde
		{0x00d2, 'o'}, // latin capital letter o with grave
		{0x00d3, 'o'}, // latin capital letter o with acute
		{0x00d4, 'o'}, // latin capital letter o with circumflex
		{0x00d5, 'o'}, // latin capital letter o with tilde
		{0x00d6, 'o'}, // latin capital letter o with diaeresis
		{0x00d8, 'o'}, // latin capital letter o with stroke -- no decom
		{0x00d9, 'u'}, // latin capital letter u with grave
		{0x00da, 'u'}, // latin capital letter u with acute
		{0x00db, 'u'}, // latin capital letter u with circumflex
		{0x00dc, 'u'}, // latin capital letter u with diaeresis
		{0x00dd, 'y'}, // latin capital letter y with acute
		{0x00df, 's'}, // latin small letter sharp s -- no decomposition
		{0x00e0, 'a'}, // latin small letter a with grave
		{0x00e1, 'a'}, // latin small letter a with acute
		{0x00e2, 'a'}, // latin small letter a with circumflex
		{0x00e3, 'a'}, // latin small letter a with tilde
		{0x00e4, 'a'}, // latin small letter a with diaeresis
		{0x00e5, 'a'}, // latin small letter a with ring above
		{0x00e7, 'c'}, // latin small letter c with cedilla
		{0x00e8, 'e'}, // latin small letter e with grave
		{0x00e9, 'e'}, // latin small letter e with acute
		{0x00ea, 'e'}, // latin small letter e with circumflex
		{0x00eb, 'e'}, // latin small letter e with diaeresis
		{0x00ec, 'i'}, // latin small letter i with grave
		{0x00ed, 'i'}, // latin small letter i with acute
		{0x00ee, 'i'}, // latin small letter i with circumflex
		{0x00ef, 'i'}, // latin small letter i with diaeresis
		{0x00f0, 'd'},
		// latin small letter eth -- no decomposition         // small eth, "d" for benefit of vietnamese
		{0x00f1, 'n'}, // latin small letter n with tilde
		{0x00f2, 'o'}, // latin small letter o with grave
		{0x00f3, 'o'}, // latin small letter o with acute
		{0x00f4, 'o'}, // latin small letter o with circumflex
		{0x00f5, 'o'}, // latin small letter o with tilde
		{0x00f6, 'o'}, // latin small letter o with diaeresis
		{0x00f8, 'o'}, // latin small letter o with stroke -- no decompo
		{0x00f9, 'u'}, // latin small letter u with grave
		{0x00fa, 'u'}, // latin small letter u with acute
		{0x00fb, 'u'}, // latin small letter u with circumflex
		{0x00fc, 'u'}, // latin small letter u with diaeresis
		{0x00fd, 'y'}, // latin small letter y with acute
		{0x00ff, 'y'}, // latin small letter y with diaeresis
		{0x0100, 'a'}, // latin capital letter a with macron
		{0x0101, 'a'}, // latin small letter a with macron
		{0x0102, 'a'}, // latin capital letter a with breve
		{0x0103, 'a'}, // latin small letter a with breve
		{0x0104, 'a'}, // latin capital letter a with ogonek
		{0x0105, 'a'}, // latin small letter a with ogonek
		{0x0106, 'c'}, // latin capital letter c with acute
		{0x0107, 'c'}, // latin small letter c with acute
		{0x0108, 'c'}, // latin capital letter c with circumflex
		{0x0109, 'c'}, // latin small letter c with circumflex
		{0x010a, 'c'}, // latin capital letter c with dot above
		{0x010b, 'c'}, // latin small letter c with dot above
		{0x010c, 'c'}, // latin capital letter c with caron
		{0x010d, 'c'}, // latin small letter c with caron
		{0x010e, 'd'}, // latin capital letter d with caron
		{0x010f, 'd'}, // latin small letter d with caron
		{0x0110, 'd'},
		// latin capital letter d with stroke -- no decomposition                     // capital d with stroke
		{0x0111, 'd'},
		// latin small letter d with stroke -- no decomposition                       // small d with stroke
		{0x0112, 'e'}, // latin capital letter e with macron
		{0x0113, 'e'}, // latin small letter e with macron
		{0x0114, 'e'}, // latin capital letter e with breve
		{0x0115, 'e'}, // latin small letter e with breve
		{0x0116, 'e'}, // latin capital letter e with dot above
		{0x0117, 'e'}, // latin small letter e with dot above
		{0x0118, 'e'}, // latin capital letter e with ogonek
		{0x0119, 'e'}, // latin small letter e with ogonek
		{0x011a, 'e'}, // latin capital letter e with caron
		{0x011b, 'e'}, // latin small letter e with caron
		{0x011c, 'g'}, // latin capital letter g with circumflex
		{0x011d, 'g'}, // latin small letter g with circumflex
		{0x011e, 'g'}, // latin capital letter g with breve
		{0x011f, 'g'}, // latin small letter g with breve
		{0x0120, 'g'}, // latin capital letter g with dot above
		{0x0121, 'g'}, // latin small letter g with dot above
		{0x0122, 'g'}, // latin capital letter g with cedilla
		{0x0123, 'g'}, // latin small letter g with cedilla
		{0x0124, 'h'}, // latin capital letter h with circumflex
		{0x0125, 'h'}, // latin small letter h with circumflex
		{0x0126, 'h'}, // latin capital letter h with stroke -- no decomposition
		{0x0127, 'h'}, // latin small letter h with stroke -- no decomposition
		{0x0128, 'i'}, // latin capital letter i with tilde
		{0x0129, 'i'}, // latin small letter i with tilde
		{0x012a, 'i'}, // latin capital letter i with macron
		{0x012b, 'i'}, // latin small letter i with macron
		{0x012c, 'i'}, // latin capital letter i with breve
		{0x012d, 'i'}, // latin small letter i with breve
		{0x012e, 'i'}, // latin capital letter i with ogonek
		{0x012f, 'i'}, // latin small letter i with ogonek
		{0x0130, 'i'}, // latin capital letter i with dot above
		{0x0131, 'i'}, // latin small letter dotless i -- no decomposition
		{0x0132, 'i'}, // latin capital ligature ij    
		{0x0133, 'i'}, // latin small ligature ij      
		{0x0134, 'j'}, // latin capital letter j with circumflex
		{0x0135, 'j'}, // latin small letter j with circumflex
		{0x0136, 'k'}, // latin capital letter k with cedilla
		{0x0137, 'k'}, // latin small letter k with cedilla
		{0x0138, 'k'}, // latin small letter kra -- no decomposition
		{0x0139, 'l'}, // latin capital letter l with acute
		{0x013a, 'l'}, // latin small letter l with acute
		{0x013b, 'l'}, // latin capital letter l with cedilla
		{0x013c, 'l'}, // latin small letter l with cedilla
		{0x013d, 'l'}, // latin capital letter l with caron
		{0x013e, 'l'}, // latin small letter l with caron
		{0x013f, 'l'}, // latin capital letter l with middle dot
		{0x0140, 'l'}, // latin small letter l with middle dot
		{0x0141, 'l'}, // latin capital letter l with stroke -- no decomposition
		{0x0142, 'l'}, // latin small letter l with stroke -- no decomposition
		{0x0143, 'n'}, // latin capital letter n with acute
		{0x0144, 'n'}, // latin small letter n with acute
		{0x0145, 'n'}, // latin capital letter n with cedilla
		{0x0146, 'n'}, // latin small letter n with cedilla
		{0x0147, 'n'}, // latin capital letter n with caron
		{0x0148, 'n'}, // latin small letter n with caron
		{0x014c, 'o'}, // latin capital letter o with macron
		{0x014d, 'o'}, // latin small letter o with macron
		{0x014e, 'o'}, // latin capital letter o with breve
		{0x014f, 'o'}, // latin small letter o with breve
		{0x0150, 'o'}, // latin capital letter o with double acute
		{0x0151, 'o'}, // latin small letter o with double acute
		{0x0154, 'r'}, // latin capital letter r with acute
		{0x0155, 'r'}, // latin small letter r with acute
		{0x0156, 'r'}, // latin capital letter r with cedilla
		{0x0157, 'r'}, // latin small letter r with cedilla
		{0x0158, 'r'}, // latin capital letter r with caron
		{0x0159, 'r'}, // latin small letter r with caron
		{0x015a, 's'}, // latin capital letter s with acute
		{0x015b, 's'}, // latin small letter s with acute
		{0x015c, 's'}, // latin capital letter s with circumflex
		{0x015d, 's'}, // latin small letter s with circumflex
		{0x015e, 's'}, // latin capital letter s with cedilla
		{0x015f, 's'}, // latin small letter s with cedilla
		{0x0160, 's'}, // latin capital letter s with caron
		{0x0161, 's'}, // latin small letter s with caron
		{0x0162, 't'}, // latin capital letter t with cedilla
		{0x0163, 't'}, // latin small letter t with cedilla
		{0x0164, 't'}, // latin capital letter t with caron
		{0x0165, 't'}, // latin small letter t with caron
		{0x0166, 't'}, // latin capital letter t with stroke -- no decomposition
		{0x0167, 't'}, // latin small letter t with stroke -- no decomposition
		{0x0168, 'u'}, // latin capital letter u with tilde
		{0x0169, 'u'}, // latin small letter u with tilde
		{0x016a, 'u'}, // latin capital letter u with macron
		{0x016b, 'u'}, // latin small letter u with macron
		{0x016c, 'u'}, // latin capital letter u with breve
		{0x016d, 'u'}, // latin small letter u with breve
		{0x016e, 'u'}, // latin capital letter u with ring above
		{0x016f, 'u'}, // latin small letter u with ring above
		{0x0170, 'u'}, // latin capital letter u with double acute
		{0x0171, 'u'}, // latin small letter u with double acute
		{0x0172, 'u'}, // latin capital letter u with ogonek
		{0x0173, 'u'}, // latin small letter u with ogonek
		{0x0174, 'w'}, // latin capital letter w with circumflex
		{0x0175, 'w'}, // latin small letter w with circumflex
		{0x0176, 'y'}, // latin capital letter y with circumflex
		{0x0177, 'y'}, // latin small letter y with circumflex
		{0x0178, 'y'}, // latin capital letter y with diaeresis
		{0x0179, 'z'}, // latin capital letter z with acute
		{0x017a, 'z'}, // latin small letter z with acute
		{0x017b, 'z'}, // latin capital letter z with dot above
		{0x017c, 'z'}, // latin small letter z with dot above
		{0x017d, 'z'}, // latin capital letter z with caron
		{0x017e, 'z'}, // latin small letter z with caron
		{0x017f, 's'}, // latin small letter long s    
		{0x0180, 'b'}, // latin small letter b with stroke -- no decomposition
		{0x0181, 'b'}, // latin capital letter b with hook -- no decomposition
		{0x0182, 'b'}, // latin capital letter b with topbar -- no decomposition
		{0x0183, 'b'}, // latin small letter b with topbar -- no decomposition
		{0x0184, '6'}, // latin capital letter tone six -- no decomposition
		{0x0185, '6'}, // latin small letter tone six -- no decomposition
		{0x0186, 'o'}, // latin capital letter open o -- no decomposition
		{0x0187, 'c'}, // latin capital letter c with hook -- no decomposition
		{0x0188, 'c'}, // latin small letter c with hook -- no decomposition
		{0x0189, 'd'}, // latin capital letter african d -- no decomposition
		{0x018a, 'd'}, // latin capital letter d with hook -- no decomposition
		{0x018b, 'd'}, // latin capital letter d with topbar -- no decomposition
		{0x018c, 'd'}, // latin small letter d with topbar -- no decomposition
		{0x018d, 'd'}, // latin small letter turned delta -- no decomposition
		{0x018e, 'e'}, // latin capital letter reversed e -- no decomposition
		{0x018f, 'e'}, // latin capital letter schwa -- no decomposition
		{0x0190, 'e'}, // latin capital letter open e -- no decomposition
		{0x0191, 'f'}, // latin capital letter f with hook -- no decomposition
		{0x0192, 'f'}, // latin small letter f with hook -- no decomposition
		{0x0193, 'g'}, // latin capital letter g with hook -- no decomposition
		{0x0194, 'g'}, // latin capital letter gamma -- no decomposition
		{0x0196, 'i'}, // latin capital letter iota -- no decomposition
		{0x0197, 'i'}, // latin capital letter i with stroke -- no decomposition
		{0x0198, 'k'}, // latin capital letter k with hook -- no decomposition
		{0x0199, 'k'}, // latin small letter k with hook -- no decomposition
		{0x019a, 'l'}, // latin small letter l with bar -- no decomposition
		{0x019b, 'l'}, // latin small letter lambda with stroke -- no decomposition
		{0x019c, 'm'}, // latin capital letter turned m -- no decomposition
		{0x019d, 'n'}, // latin capital letter n with left hook -- no decomposition
		{0x019e, 'n'}, // latin small letter n with long right leg -- no decomposition
		{0x019f, 'o'}, // latin capital letter o with middle tilde -- no decomposition
		{0x01a0, 'o'}, // latin capital letter o with horn
		{0x01a1, 'o'}, // latin small letter o with horn
		{0x01a4, 'p'}, // latin capital letter p with hook -- no decomposition
		{0x01a5, 'p'}, // latin small letter p with hook -- no decomposition
		{0x01a7, '2'}, // latin capital letter tone two -- no decomposition
		{0x01a8, '2'}, // latin small letter tone two -- no decomposition
		{0x01a9, 's'}, // latin capital letter esh -- no decomposition
		{0x01aa, 's'}, // latin letter reversed esh loop -- no decomposition
		{0x01ab, 't'}, // latin small letter t with palatal hook -- no decomposition
		{0x01ac, 't'}, // latin capital letter t with hook -- no decomposition
		{0x01ad, 't'}, // latin small letter t with hook -- no decomposition
		{0x01ae, 't'}, // latin capital letter t with retroflex hook -- no decomposition
		{0x01af, 'u'}, // latin capital letter u with horn
		{0x01b0, 'u'}, // latin small letter u with horn
		{0x01b1, 'u'}, // latin capital letter upsilon -- no decomposition
		{0x01b2, 'v'}, // latin capital letter v with hook -- no decomposition
		{0x01b3, 'y'}, // latin capital letter y with hook -- no decomposition
		{0x01b4, 'y'}, // latin small letter y with hook -- no decomposition
		{0x01b5, 'z'}, // latin capital letter z with stroke -- no decomposition
		{0x01b6, 'z'}, // latin small letter z with stroke -- no decomposition
		{0x01b7, 'z'}, // latin capital letter ezh -- no decomposition
		{0x01b8, 'z'}, // latin capital letter ezh reversed -- no decomposition
		{0x01b9, 'z'}, // latin small letter ezh reversed -- no decomposition
		{0x01ba, 'z'}, // latin small letter ezh with tail -- no decomposition
		{0x01bb, '2'}, // latin letter two with stroke -- no decomposition
		{0x01bc, '5'}, // latin capital letter tone five -- no decomposition
		{0x01bd, '5'}, // latin small letter tone five -- no decomposition
		{0x01bf, 'w'}, // latin letter wynn -- no decomposition
		{0x01c6, 'd'}, // latin small letter dz with caron
		{0x01cd, 'a'}, // latin capital letter a with caron
		{0x01ce, 'a'}, // latin small letter a with caron
		{0x01cf, 'i'}, // latin capital letter i with caron
		{0x01d0, 'i'}, // latin small letter i with caron
		{0x01d1, 'o'}, // latin capital letter o with caron
		{0x01d2, 'o'}, // latin small letter o with caron
		{0x01d3, 'u'}, // latin capital letter u with caron
		{0x01d4, 'u'}, // latin small letter u with caron
		{0x01d5, 'u'}, // latin capital letter u with diaeresis and macron
		{0x01d6, 'u'}, // latin small letter u with diaeresis and macron
		{0x01d7, 'u'}, // latin capital letter u with diaeresis and acute
		{0x01d8, 'u'}, // latin small letter u with diaeresis and acute
		{0x01d9, 'u'}, // latin capital letter u with diaeresis and caron
		{0x01da, 'u'}, // latin small letter u with diaeresis and caron
		{0x01db, 'u'}, // latin capital letter u with diaeresis and grave
		{0x01dc, 'u'}, // latin small letter u with diaeresis and grave
		{0x01dd, 'e'}, // latin small letter turned e -- no decomposition
		{0x01de, 'a'}, // latin capital letter a with diaeresis and macron
		{0x01df, 'a'}, // latin small letter a with diaeresis and macron
		{0x01e0, 'a'}, // latin capital letter a with dot above and macron
		{0x01e1, 'a'}, // latin small letter a with dot above and macron
		{0x01e4, 'g'}, // latin capital letter g with stroke -- no decomposition
		{0x01e5, 'g'}, // latin small letter g with stroke -- no decomposition
		{0x01e6, 'g'}, // latin capital letter g with caron
		{0x01e7, 'g'}, // latin small letter g with caron
		{0x01e8, 'k'}, // latin capital letter k with caron
		{0x01e9, 'k'}, // latin small letter k with caron
		{0x01ea, 'o'}, // latin capital letter o with ogonek
		{0x01eb, 'o'}, // latin small letter o with ogonek
		{0x01ec, 'o'}, // latin capital letter o with ogonek and macron
		{0x01ed, 'o'}, // latin small letter o with ogonek and macron
		{0x01ee, 'z'}, // latin capital letter ezh with caron
		{0x01ef, 'z'}, // latin small letter ezh with caron
		{0x01f0, 'j'}, // latin small letter j with caron
		{0x01f4, 'g'}, // latin capital letter g with acute
		{0x01f5, 'g'}, // latin small letter g with acute
		{0x01f7, 'w'}, // latin capital letter wynn -- no decomposition
		{0x01f8, 'n'}, // latin capital letter n with grave
		{0x01f9, 'n'}, // latin small letter n with grave
		{0x01fa, 'a'}, // latin capital letter a with ring above and acute
		{0x01fb, 'a'}, // latin small letter a with ring above and acute
		{0x01fe, 'o'}, // latin capital letter o with stroke and acute
		{0x01ff, 'o'}, // latin small letter o with stroke and acute
		{0x0200, 'a'}, // latin capital letter a with double grave
		{0x0201, 'a'}, // latin small letter a with double grave
		{0x0202, 'a'}, // latin capital letter a with inverted breve
		{0x0203, 'a'}, // latin small letter a with inverted breve
		{0x0204, 'e'}, // latin capital letter e with double grave
		{0x0205, 'e'}, // latin small letter e with double grave
		{0x0206, 'e'}, // latin capital letter e with inverted breve
		{0x0207, 'e'}, // latin small letter e with inverted breve
		{0x0208, 'i'}, // latin capital letter i with double grave
		{0x0209, 'i'}, // latin small letter i with double grave
		{0x020a, 'i'}, // latin capital letter i with inverted breve
		{0x020b, 'i'}, // latin small letter i with inverted breve
		{0x020c, 'o'}, // latin capital letter o with double grave
		{0x020d, 'o'}, // latin small letter o with double grave
		{0x020e, 'o'}, // latin capital letter o with inverted breve
		{0x020f, 'o'}, // latin small letter o with inverted breve
		{0x0210, 'r'}, // latin capital letter r with double grave
		{0x0211, 'r'}, // latin small letter r with double grave
		{0x0212, 'r'}, // latin capital letter r with inverted breve
		{0x0213, 'r'}, // latin small letter r with inverted breve
		{0x0214, 'u'}, // latin capital letter u with double grave
		{0x0215, 'u'}, // latin small letter u with double grave
		{0x0216, 'u'}, // latin capital letter u with inverted breve
		{0x0217, 'u'}, // latin small letter u with inverted breve
		{0x0218, 's'}, // latin capital letter s with comma below
		{0x0219, 's'}, // latin small letter s with comma below
		{0x021a, 't'}, // latin capital letter t with comma below
		{0x021b, 't'}, // latin small letter t with comma below
		{0x021c, 'z'}, // latin capital letter yogh -- no decomposition
		{0x021d, 'z'}, // latin small letter yogh -- no decomposition
		{0x021e, 'h'}, // latin capital letter h with caron
		{0x021f, 'h'}, // latin small letter h with caron
		{0x0220, 'n'}, // latin capital letter n with long right leg -- no decomposition
		{0x0221, 'd'}, // latin small letter d with curl -- no decomposition
		{0x0224, 'z'}, // latin capital letter z with hook -- no decomposition
		{0x0225, 'z'}, // latin small letter z with hook -- no decomposition
		{0x0226, 'a'}, // latin capital letter a with dot above
		{0x0227, 'a'}, // latin small letter a with dot above
		{0x0228, 'e'}, // latin capital letter e with cedilla
		{0x0229, 'e'}, // latin small letter e with cedilla
		{0x022a, 'o'}, // latin capital letter o with diaeresis and macron
		{0x022b, 'o'}, // latin small letter o with diaeresis and macron
		{0x022c, 'o'}, // latin capital letter o with tilde and macron
		{0x022d, 'o'}, // latin small letter o with tilde and macron
		{0x022e, 'o'}, // latin capital letter o with dot above
		{0x022f, 'o'}, // latin small letter o with dot above
		{0x0230, 'o'}, // latin capital letter o with dot above and macron
		{0x0231, 'o'}, // latin small letter o with dot above and macron
		{0x0232, 'y'}, // latin capital letter y with macron
		{0x0233, 'y'}, // latin small letter y with macron
		{0x0234, 'l'}, // latin small letter l with curl -- no decomposition
		{0x0235, 'n'}, // latin small letter n with curl -- no decomposition
		{0x0236, 't'}, // latin small letter t with curl -- no decomposition
		{0x0250, 'a'}, // latin small letter turned a -- no decomposition
		{0x0251, 'a'}, // latin small letter alpha -- no decomposition
		{0x0252, 'a'}, // latin small letter turned alpha -- no decomposition
		{0x0253, 'b'}, // latin small letter b with hook -- no decomposition
		{0x0254, 'o'}, // latin small letter open o -- no decomposition
		{0x0255, 'c'}, // latin small letter c with curl -- no decomposition
		{0x0256, 'd'}, // latin small letter d with tail -- no decomposition
		{0x0257, 'd'}, // latin small letter d with hook -- no decomposition
		{0x0258, 'e'}, // latin small letter reversed e -- no decomposition
		{0x0259, 'e'}, // latin small letter schwa -- no decomposition
		{0x025a, 'e'}, // latin small letter schwa with hook -- no decomposition
		{0x025b, 'e'}, // latin small letter open e -- no decomposition
		{0x025c, 'e'}, // latin small letter reversed open e -- no decomposition
		{0x025d, 'e'}, // latin small letter reversed open e with hook -- no decomposition
		{0x025e, 'e'}, // latin small letter closed reversed open e -- no decomposition
		{0x025f, 'j'}, // latin small letter dotless j with stroke -- no decomposition
		{0x0260, 'g'}, // latin small letter g with hook -- no decomposition
		{0x0261, 'g'}, // latin small letter script g -- no decomposition
		{0x0262, 'g'}, // latin letter small capital g -- no decomposition
		{0x0263, 'g'}, // latin small letter gamma -- no decomposition
		{0x0264, 'y'}, // latin small letter rams horn -- no decomposition
		{0x0265, 'h'}, // latin small letter turned h -- no decomposition
		{0x0266, 'h'}, // latin small letter h with hook -- no decomposition
		{0x0267, 'h'}, // latin small letter heng with hook -- no decomposition
		{0x0268, 'i'}, // latin small letter i with stroke -- no decomposition
		{0x0269, 'i'}, // latin small letter iota -- no decomposition
		{0x026a, 'i'}, // latin letter small capital i -- no decomposition
		{0x026b, 'l'}, // latin small letter l with middle tilde -- no decomposition
		{0x026c, 'l'}, // latin small letter l with belt -- no decomposition
		{0x026d, 'l'}, // latin small letter l with retroflex hook -- no decomposition
		{0x026f, 'm'}, // latin small letter turned m -- no decomposition
		{0x0270, 'm'}, // latin small letter turned m with long leg -- no decomposition
		{0x0271, 'm'}, // latin small letter m with hook -- no decomposition
		{0x0272, 'n'}, // latin small letter n with left hook -- no decomposition
		{0x0273, 'n'}, // latin small letter n with retroflex hook -- no decomposition
		{0x0274, 'n'}, // latin letter small capital n -- no decomposition
		{0x0275, 'o'}, // latin small letter barred o -- no decomposition
		{0x0276, 'oe'}, // latin letter small capital oe -- no decomposition
		{0x0277, 'o'}, // latin small letter closed omega -- no decomposition
		{0x0279, 'r'}, // latin small letter turned r -- no decomposition
		{0x027a, 'r'}, // latin small letter turned r with long leg -- no decomposition
		{0x027b, 'r'}, // latin small letter turned r with hook -- no decomposition
		{0x027c, 'r'}, // latin small letter r with long leg -- no decomposition
		{0x027d, 'r'}, // latin small letter r with tail -- no decomposition
		{0x027e, 'r'}, // latin small letter r with fishhook -- no decomposition
		{0x027f, 'r'}, // latin small letter reversed r with fishhook -- no decomposition
		{0x0280, 'r'}, // latin letter small capital r -- no decomposition
		{0x0281, 'r'}, // latin letter small capital inverted r -- no decomposition
		{0x0282, 's'}, // latin small letter s with hook -- no decomposition
		{0x0283, 's'}, // latin small letter esh -- no decomposition
		{0x0284, 'j'}, // latin small letter dotless j with stroke and hook -- no decomposition
		{0x0285, 's'}, // latin small letter squat reversed esh -- no decomposition
		{0x0286, 's'}, // latin small letter esh with curl -- no decomposition
		{0x0287, 'y'}, // latin small letter turned t -- no decomposition
		{0x0288, 't'}, // latin small letter t with retroflex hook -- no decomposition
		{0x0289, 'u'}, // latin small letter u bar -- no decomposition
		{0x028a, 'u'}, // latin small letter upsilon -- no decomposition
		{0x028b, 'u'}, // latin small letter v with hook -- no decomposition
		{0x028c, 'v'}, // latin small letter turned v -- no decomposition
		{0x028d, 'w'}, // latin small letter turned w -- no decomposition
		{0x028e, 'y'}, // latin small letter turned y -- no decomposition
		{0x028f, 'y'}, // latin letter small capital y -- no decomposition
		{0x0290, 'z'}, // latin small letter z with retroflex hook -- no decomposition
		{0x0291, 'z'}, // latin small letter z with curl -- no decomposition
		{0x0292, 'z'}, // latin small letter ezh -- no decomposition
		{0x0293, 'z'}, // latin small letter ezh with curl -- no decomposition
		{0x0297, 'c'}, // latin letter stretched c -- no decomposition
		{0x0299, 'b'}, // latin letter small capital b -- no decomposition
		{0x029a, 'e'}, // latin small letter closed open e -- no decomposition
		{0x029b, 'g'}, // latin letter small capital g with hook -- no decomposition
		{0x029c, 'h'}, // latin letter small capital h -- no decomposition
		{0x029d, 'j'}, // latin small letter j with crossed-tail -- no decomposition
		{0x029e, 'k'}, // latin small letter turned k -- no decomposition
		{0x029f, 'l'}, // latin letter small capital l -- no decomposition
		{0x02a0, 'q'}, // latin small letter q with hook -- no decomposition
		{0x02ac, 'w'}, // latin letter bilabial percussive -- no decomposition
		{0x02ad, 't'}, // latin letter bidental percussive -- no decomposition
		{0x02ae, 'h'}, // latin small letter turned h with fishhook -- no decomposition
		{0x02af, 'h'}, // latin small letter turned h with fishhook and tail -- no decomposition
		{0x02b0, 'h'}, // modifier letter small h
		{0x02b1, 'h'}, // modifier letter small h with hook
		{0x02b2, 'j'}, // modifier letter small j
		{0x02b3, 'r'}, // modifier letter small r
		{0x02b4, 'r'}, // modifier letter small turned r
		{0x02b5, 'r'}, // modifier letter small turned r with hook
		{0x02b6, 'r'}, // modifier letter small capital inverted r
		{0x02b7, 'w'}, // modifier letter small w
		{0x02b8, 'y'}, // modifier letter small y
		{0x02e1, 'l'}, // modifier letter small l
		{0x02e2, 's'}, // modifier letter small s
		{0x02e3, 'x'}, // modifier letter small x
		{0x0380, 'a'}, //	LATIN CAPITAL LETTER A WITH GRAVE
		{0x0381, 'a'}, //	LATIN CAPITAL LETTER A WITH ACUTE
		{0x0382, 'a'}, //	LATIN CAPITAL LETTER A WITH CIRCUMFLEX
		{0x0383, 'a'}, //	LATIN CAPITAL LETTER A WITH TILDE
		{0x0384, 'a'}, //	LATIN CAPITAL LETTER A WITH DIAERESIS
		{0x0385, 'a'}, //	LATIN CAPITAL LETTER A WITH RING ABOVE
		{0x0387, 'c'}, //	LATIN CAPITAL LETTER C WITH CEDILLA
		{0x0388, 'e'}, //	LATIN CAPITAL LETTER E WITH GRAVE
		{0x0389, 'e'}, //	LATIN CAPITAL LETTER E WITH ACUTE
		{0x038a, 'e'}, //	LATIN CAPITAL LETTER E WITH CIRCUMFLEX
		{0x038b, 'e'}, //	LATIN CAPITAL LETTER E WITH DIAERESIS
		{0x038c, 'i'}, //	LATIN CAPITAL LETTER I WITH GRAVE
		{0x038d, 'i'}, //	LATIN CAPITAL LETTER I WITH ACUTE
		{0x038e, 'i'}, //	LATIN CAPITAL LETTER I WITH CIRCUMFLEX
		{0x038f, 'i'}, //	LATIN CAPITAL LETTER I WITH DIAERESIS
		{0x0391, 'n'}, //	LATIN CAPITAL LETTER N WITH TILDE
		{0x0392, 'o'}, //	LATIN CAPITAL LETTER O WITH GRAVE
		{0x0393, 'o'}, //	LATIN CAPITAL LETTER O WITH ACUTE
		{0x0394, 'o'}, //	LATIN CAPITAL LETTER O WITH CIRCUMFLEX
		{0x0395, 'o'}, //	LATIN CAPITAL LETTER O WITH TILDE
		{0x0396, 'o'}, //	LATIN CAPITAL LETTER O WITH DIAERESIS
		{0x0398, 'o'}, //	LATIN CAPITAL LETTER O WITH STROKE
		{0x0399, 'u'}, //	LATIN CAPITAL LETTER U WITH GRAVE
		{0x039a, 'u'}, //	LATIN CAPITAL LETTER U WITH ACUTE
		{0x039b, 'u'}, //	LATIN CAPITAL LETTER U WITH CIRCUMFLEX
		{0x039c, 'u'}, //	LATIN CAPITAL LETTER U WITH DIAERESIS
		{0x039d, 'y'}, //	LATIN CAPITAL LETTER Y WITH ACUTE
		{0x039f, 's'}, //	LATIN SMALL LETTER SHARP S
		{0x03a0, 'a'}, //	LATIN SMALL LETTER A WITH GRAVE
		{0x03a1, 'a'}, //	LATIN SMALL LETTER A WITH ACUTE
		{0x03a2, 'a'}, //	LATIN SMALL LETTER A WITH CIRCUMFLEX
		{0x03a3, 'a'}, //	LATIN SMALL LETTER A WITH TILDE
		{0x03a4, 'a'}, //	LATIN SMALL LETTER A WITH DIAERESIS
		{0x03a5, 'a'}, //	LATIN SMALL LETTER A WITH RING ABOVE
		{0x03a7, 'c'}, //	LATIN SMALL LETTER C WITH CEDILLA
		{0x03a8, 'e'}, //	LATIN SMALL LETTER E WITH GRAVE
		{0x03a9, 'e'}, //	LATIN SMALL LETTER E WITH ACUTE
		{0x03aa, 'e'}, //	LATIN SMALL LETTER E WITH CIRCUMFLEX
		{0x03ab, 'e'}, //	LATIN SMALL LETTER E WITH DIAERESIS
		{0x03ac, 'i'}, //	LATIN SMALL LETTER I WITH GRAVE
		{0x03ad, 'i'}, //	LATIN SMALL LETTER I WITH ACUTE
		{0x03ae, 'i'}, //	LATIN SMALL LETTER I WITH CIRCUMFLEX
		{0x03af, 'i'}, //	LATIN SMALL LETTER I WITH DIAERESIS
		{0x03b1, 'n'}, //	LATIN SMALL LETTER N WITH TILDE
		{0x03b2, 'o'}, //	LATIN SMALL LETTER O WITH GRAVE
		{0x03b3, 'o'}, //	LATIN SMALL LETTER O WITH ACUTE
		{0x03b4, 'o'}, //	LATIN SMALL LETTER O WITH CIRCUMFLEX
		{0x03b5, 'o'}, //	LATIN SMALL LETTER O WITH TILDE
		{0x03b6, 'o'}, //	LATIN SMALL LETTER O WITH DIAERESIS
		{0x03b8, 'o'}, //	LATIN SMALL LETTER O WITH STROKE
		{0x03b9, 'u'}, //	LATIN SMALL LETTER U WITH GRAVE
		{0x03ba, 'u'}, //	LATIN SMALL LETTER U WITH ACUTE
		{0x03bb, 'u'}, //	LATIN SMALL LETTER U WITH CIRCUMFLEX
		{0x03bc, 'u'}, //	LATIN SMALL LETTER U WITH DIAERESIS
		{0x03bd, 'y'}, //	LATIN SMALL LETTER Y WITH ACUTE
		{0x03bf, 'y'}, //	LATIN SMALL LETTER Y WITH DIAERESIS
		{0x0480, 'a'}, //	LATIN CAPITAL LETTER A WITH MACRON
		{0x0481, 'a'}, //	LATIN SMALL LETTER A WITH MACRON
		{0x0482, 'a'}, //	LATIN CAPITAL LETTER A WITH BREVE
		{0x0483, 'a'}, //	LATIN SMALL LETTER A WITH BREVE
		{0x0484, 'a'}, //	LATIN CAPITAL LETTER A WITH OGONEK
		{0x0485, 'a'}, //	LATIN SMALL LETTER A WITH OGONEK
		{0x0486, 'c'}, //	LATIN CAPITAL LETTER C WITH ACUTE
		{0x0487, 'c'}, //	LATIN SMALL LETTER C WITH ACUTE
		{0x0488, 'c'}, //	LATIN CAPITAL LETTER C WITH CIRCUMFLEX
		{0x0489, 'c'}, //	LATIN SMALL LETTER C WITH CIRCUMFLEX
		{0x048a, 'c'}, //	LATIN CAPITAL LETTER C WITH DOT ABOVE
		{0x048b, 'c'}, //	LATIN SMALL LETTER C WITH DOT ABOVE
		{0x048c, 'c'}, //	LATIN CAPITAL LETTER C WITH CARON
		{0x048d, 'c'}, //	LATIN SMALL LETTER C WITH CARON
		{0x048e, 'd'}, //	LATIN CAPITAL LETTER D WITH CARON
		{0x048f, 'd'}, //	LATIN SMALL LETTER D WITH CARON
		{0x0490, 'd'}, //	LATIN CAPITAL LETTER D WITH STROKE
		{0x0491, 'd'}, //	LATIN SMALL LETTER D WITH STROKE
		{0x0492, 'e'}, //	LATIN CAPITAL LETTER E WITH MACRON
		{0x0493, 'e'}, //	LATIN SMALL LETTER E WITH MACRON
		{0x0494, 'e'}, //	LATIN CAPITAL LETTER E WITH BREVE
		{0x0495, 'e'}, //	LATIN SMALL LETTER E WITH BREVE
		{0x0496, 'e'}, //	LATIN CAPITAL LETTER E WITH DOT ABOVE
		{0x0497, 'e'}, //	LATIN SMALL LETTER E WITH DOT ABOVE
		{0x0498, 'e'}, //	LATIN CAPITAL LETTER E WITH OGONEK
		{0x0499, 'e'}, //	LATIN SMALL LETTER E WITH OGONEK
		{0x049a, 'e'}, //	LATIN CAPITAL LETTER E WITH CARON
		{0x049b, 'e'}, //	LATIN SMALL LETTER E WITH CARON
		{0x049c, 'g'}, //	LATIN CAPITAL LETTER G WITH CIRCUMFLEX
		{0x049d, 'g'}, //	LATIN SMALL LETTER G WITH CIRCUMFLEX
		{0x049e, 'g'}, //	LATIN CAPITAL LETTER G WITH BREVE
		{0x049f, 'g'}, //	LATIN SMALL LETTER G WITH BREVE
		{0x04a0, 'g'}, //	LATIN CAPITAL LETTER G WITH DOT ABOVE
		{0x04a1, 'g'}, //	LATIN SMALL LETTER G WITH DOT ABOVE
		{0x04a2, 'g'}, //	LATIN CAPITAL LETTER G WITH CEDILLA
		{0x04a3, 'g'}, //	LATIN SMALL LETTER G WITH CEDILLA
		{0x04a4, 'h'}, //	LATIN CAPITAL LETTER H WITH CIRCUMFLEX
		{0x04a5, 'h'}, //	LATIN SMALL LETTER H WITH CIRCUMFLEX
		{0x04a6, 'h'}, //	LATIN CAPITAL LETTER H WITH STROKE
		{0x04a7, 'h'}, //	LATIN SMALL LETTER H WITH STROKE
		{0x04a8, 'i'}, //	LATIN CAPITAL LETTER I WITH TILDE
		{0x04a9, 'i'}, //	LATIN SMALL LETTER I WITH TILDE
		{0x04aa, 'i'}, //	LATIN CAPITAL LETTER I WITH MACRON
		{0x04ab, 'i'}, //	LATIN SMALL LETTER I WITH MACRON
		{0x04ac, 'i'}, //	LATIN CAPITAL LETTER I WITH BREVE
		{0x04ad, 'i'}, //	LATIN SMALL LETTER I WITH BREVE
		{0x04ae, 'i'}, //	LATIN CAPITAL LETTER I WITH OGONEK
		{0x04af, 'i'}, //	LATIN SMALL LETTER I WITH OGONEK
		{0x04b0, 'i'}, //	LATIN CAPITAL LETTER I WITH DOT ABOVE
		{0x04b1, 'i'}, //	LATIN SMALL LETTER DOTLESS I
		{0x04b4, 'j'}, //	LATIN CAPITAL LETTER J WITH CIRCUMFLEX
		{0x04b5, 'j'}, //	LATIN SMALL LETTER J WITH CIRCUMFLEX
		{0x04b6, 'k'}, //	LATIN CAPITAL LETTER K WITH CEDILLA
		{0x04b7, 'k'}, //	LATIN SMALL LETTER K WITH CEDILLA
		{0x04b8, 'k'}, //	LATIN SMALL LETTER KRA
		{0x04b9, 'l'}, //	LATIN CAPITAL LETTER L WITH ACUTE
		{0x04ba, 'l'}, //	LATIN SMALL LETTER L WITH ACUTE
		{0x04bb, 'l'}, //	LATIN CAPITAL LETTER L WITH CEDILLA
		{0x04bc, 'l'}, //	LATIN SMALL LETTER L WITH CEDILLA
		{0x04bd, 'l'}, //	LATIN CAPITAL LETTER L WITH CARON
		{0x04be, 'l'}, //	LATIN SMALL LETTER L WITH CARON
		{0x04bf, 'l'}, //	LATIN CAPITAL LETTER L WITH MIDDLE DOT
		{0x0580, 'l'}, //	LATIN SMALL LETTER L WITH MIDDLE DOT
		{0x0581, 'l'}, //	LATIN CAPITAL LETTER L WITH STROKE
		{0x0582, 'l'}, //	LATIN SMALL LETTER L WITH STROKE
		{0x0583, 'n'}, //	LATIN CAPITAL LETTER N WITH ACUTE
		{0x0584, 'n'}, //	LATIN SMALL LETTER N WITH ACUTE
		{0x0585, 'n'}, //	LATIN CAPITAL LETTER N WITH CEDILLA
		{0x0586, 'n'}, //	LATIN SMALL LETTER N WITH CEDILLA
		{0x0587, 'n'}, //	LATIN CAPITAL LETTER N WITH CARON
		{0x0588, 'n'}, //	LATIN SMALL LETTER N WITH CARON
		{0x0589, 'n'}, //	LATIN SMALL LETTER N PRECEDED BY APOSTROPHE
		{0x058a, 'n'}, //	LATIN CAPITAL LETTER ENG
		{0x058b, 'n'}, //	LATIN SMALL LETTER ENG
		{0x058c, 'o'}, //	LATIN CAPITAL LETTER O WITH MACRON
		{0x058d, 'o'}, //	LATIN SMALL LETTER O WITH MACRON
		{0x058e, 'o'}, //	LATIN CAPITAL LETTER O WITH BREVE
		{0x058f, 'o'}, //	LATIN SMALL LETTER O WITH BREVE
		{0x0590, 'o'}, //	LATIN CAPITAL LETTER O WITH DOUBLE ACUTE
		{0x0591, 'o'}, //	LATIN SMALL LETTER O WITH DOUBLE ACUTE
		{0x0594, 'r'}, //	LATIN CAPITAL LETTER R WITH ACUTE
		{0x0595, 'r'}, //	LATIN SMALL LETTER R WITH ACUTE
		{0x0596, 'r'}, //	LATIN CAPITAL LETTER R WITH CEDILLA
		{0x0597, 'r'}, //	LATIN SMALL LETTER R WITH CEDILLA
		{0x0598, 'r'}, //	LATIN CAPITAL LETTER R WITH CARON
		{0x0599, 'r'}, //	LATIN SMALL LETTER R WITH CARON
		{0x059a, 's'}, //	LATIN CAPITAL LETTER S WITH ACUTE
		{0x059b, 's'}, //	LATIN SMALL LETTER S WITH ACUTE
		{0x059c, 's'}, //	LATIN CAPITAL LETTER S WITH CIRCUMFLEX
		{0x059d, 's'}, //	LATIN SMALL LETTER S WITH CIRCUMFLEX
		{0x059e, 's'}, //	LATIN CAPITAL LETTER S WITH CEDILLA
		{0x059f, 's'}, //	LATIN SMALL LETTER S WITH CEDILLA
		{0x05a0, 's'}, //	LATIN CAPITAL LETTER S WITH CARON
		{0x05a1, 's'}, //	LATIN SMALL LETTER S WITH CARON
		{0x05a2, 't'}, //	LATIN CAPITAL LETTER T WITH CEDILLA
		{0x05a3, 't'}, //	LATIN SMALL LETTER T WITH CEDILLA
		{0x05a4, 't'}, //	LATIN CAPITAL LETTER T WITH CARON
		{0x05a5, 't'}, //	LATIN SMALL LETTER T WITH CARON
		{0x05a6, 't'}, //	LATIN CAPITAL LETTER T WITH STROKE
		{0x05a7, 't'}, //	LATIN SMALL LETTER T WITH STROKE
		{0x05a8, 'u'}, //	LATIN CAPITAL LETTER U WITH TILDE
		{0x05a9, 'u'}, //	LATIN SMALL LETTER U WITH TILDE
		{0x05aa, 'u'}, //	LATIN CAPITAL LETTER U WITH MACRON
		{0x05ab, 'u'}, //	LATIN SMALL LETTER U WITH MACRON
		{0x05ac, 'u'}, //	LATIN CAPITAL LETTER U WITH BREVE
		{0x05ad, 'u'}, //	LATIN SMALL LETTER U WITH BREVE
		{0x05ae, 'u'}, //	LATIN CAPITAL LETTER U WITH RING ABOVE
		{0x05af, 'u'}, //	LATIN SMALL LETTER U WITH RING ABOVE
		{0x05b0, 'u'}, //	LATIN CAPITAL LETTER U WITH DOUBLE ACUTE
		{0x05b1, 'u'}, //	LATIN SMALL LETTER U WITH DOUBLE ACUTE
		{0x05b2, 'u'}, //	LATIN CAPITAL LETTER U WITH OGONEK
		{0x05b3, 'u'}, //	LATIN SMALL LETTER U WITH OGONEK
		{0x05b4, 'w'}, //	LATIN CAPITAL LETTER W WITH CIRCUMFLEX
		{0x05b5, 'w'}, //	LATIN SMALL LETTER W WITH CIRCUMFLEX
		{0x05b6, 'y'}, //	LATIN CAPITAL LETTER Y WITH CIRCUMFLEX
		{0x05b7, 'y'}, //	LATIN SMALL LETTER Y WITH CIRCUMFLEX
		{0x05b8, 'y'}, //	LATIN CAPITAL LETTER Y WITH DIAERESIS
		{0x05b9, 'z'}, //	LATIN CAPITAL LETTER Z WITH ACUTE
		{0x05ba, 'z'}, //	LATIN SMALL LETTER Z WITH ACUTE
		{0x05bb, 'z'}, //	LATIN CAPITAL LETTER Z WITH DOT ABOVE
		{0x05bc, 'z'}, //	LATIN SMALL LETTER Z WITH DOT ABOVE
		{0x05bd, 'z'}, //	LATIN CAPITAL LETTER Z WITH CARON
		{0x05be, 'z'}, //	LATIN SMALL LETTER Z WITH CARON
		{0x05bf, 's'}, //	LATIN SMALL LETTER LONG S
		{0x0680, 'b'}, //	LATIN SMALL LETTER B WITH STROKE
		{0x0681, 'b'}, //	LATIN CAPITAL LETTER B WITH HOOK
		{0x0682, 'b'}, //	LATIN CAPITAL LETTER B WITH TOPBAR
		{0x0683, 'b'}, //	LATIN SMALL LETTER B WITH TOPBAR
		{0x0684, 'b'}, //	LATIN CAPITAL LETTER TONE SIX
		{0x0685, 'b'}, //	LATIN SMALL LETTER TONE SIX
		{0x0686, 'o'}, //	LATIN CAPITAL LETTER OPEN O
		{0x0687, 'c'}, //	LATIN CAPITAL LETTER C WITH HOOK
		{0x0688, 'c'}, //	LATIN SMALL LETTER C WITH HOOK
		{0x0689, 'd'}, //	LATIN CAPITAL LETTER AFRICAN D
		{0x068a, 'd'}, //	LATIN CAPITAL LETTER D WITH HOOK
		{0x068b, 'd'}, //	LATIN CAPITAL LETTER D WITH TOPBAR
		{0x068c, 'd'}, //	LATIN SMALL LETTER D WITH TOPBAR
		{0x068d, 'd'}, //	LATIN SMALL LETTER TURNED DELTA
		{0x068e, 'e'}, //	LATIN CAPITAL LETTER REVERSED E
		{0x068f, 'e'}, //	LATIN CAPITAL LETTER SCHWA
		{0x0690, 'e'}, //	LATIN CAPITAL LETTER OPEN E
		{0x0691, 'f'}, //	LATIN CAPITAL LETTER F WITH HOOK
		// TODO {0x0692, 'ƒ'}, //	LATIN SMALL LETTER F WITH HOOK
		{0x0693, 'g'}, //	LATIN CAPITAL LETTER G WITH HOOK
		{0x0694, 'g'}, //	LATIN CAPITAL LETTER GAMMA
		{0x0696, 'i'}, //	LATIN CAPITAL LETTER IOTA
		{0x0697, 'i'}, //	LATIN CAPITAL LETTER I WITH STROKE
		{0x0698, 'k'}, //	LATIN CAPITAL LETTER K WITH HOOK
		{0x0699, 'k'}, //	LATIN SMALL LETTER K WITH HOOK
		{0x069a, 'l'}, //	LATIN SMALL LETTER L WITH BAR
		{0x069b, 'l'}, //	LATIN SMALL LETTER LAMBDA WITH STROKE
		{0x069c, 'm'}, //	LATIN CAPITAL LETTER TURNED M
		{0x069d, 'n'}, //	LATIN CAPITAL LETTER N WITH LEFT HOOK
		{0x069e, 'n'}, //	LATIN SMALL LETTER N WITH LONG RIGHT LEG
		{0x069f, 'o'}, //	LATIN CAPITAL LETTER O WITH MIDDLE TILDE
		{0x06a0, 'o'}, //	LATIN CAPITAL LETTER O WITH HORN
		{0x06a1, 'o'}, //	LATIN SMALL LETTER O WITH HORN
		{0x06a4, 'p'}, //	LATIN CAPITAL LETTER P WITH HOOK
		{0x06a5, 'p'}, //	LATIN SMALL LETTER P WITH HOOK
		{0x06a7, '2'}, //	LATIN CAPITAL LETTER TONE TWO
		{0x06a8, '2'}, //	LATIN SMALL LETTER TONE TWO
		{0x06ab, 't'}, //	LATIN SMALL LETTER T WITH PALATAL HOOK
		{0x06ac, 't'}, //	LATIN CAPITAL LETTER T WITH HOOK
		{0x06ad, 't'}, //	LATIN SMALL LETTER T WITH HOOK
		{0x06ae, 't'}, //	LATIN CAPITAL LETTER T WITH RETROFLEX HOOK
		{0x06af, 'u'}, //	LATIN CAPITAL LETTER U WITH HORN
		{0x06b0, 'u'}, //	LATIN SMALL LETTER U WITH HORN
		{0x06b1, 'u'}, //	LATIN CAPITAL LETTER UPSILON
		{0x06b2, 'v'}, //	LATIN CAPITAL LETTER V WITH HOOK
		{0x06b3, 'y'}, //	LATIN CAPITAL LETTER Y WITH HOOK
		{0x06b4, 'y'}, //	LATIN SMALL LETTER Y WITH HOOK
		{0x06b5, 'z'}, //	LATIN CAPITAL LETTER Z WITH STROKE
		{0x06b6, 'z'}, //	LATIN SMALL LETTER Z WITH STROKE
		{0x06bb, '2'}, //	LATIN LETTER TWO WITH STROKE
		{0x06bc, '5'}, //	LATIN CAPITAL LETTER TONE FIVE
		{0x06bd, '5'}, //	LATIN SMALL LETTER TONE FIVE
		{0x078d, 'a'}, //	LATIN CAPITAL LETTER A WITH CARON
		{0x078e, 'a'}, //	LATIN SMALL LETTER A WITH CARON
		{0x078f, 'i'}, //	LATIN CAPITAL LETTER I WITH CARON
		{0x0790, 'i'}, //	LATIN SMALL LETTER I WITH CARON
		{0x0791, 'o'}, //	LATIN CAPITAL LETTER O WITH CARON
		{0x0792, 'o'}, //	LATIN SMALL LETTER O WITH CARON
		{0x0793, 'u'}, //	LATIN CAPITAL LETTER U WITH CARON
		{0x0794, 'u'}, //	LATIN SMALL LETTER U WITH CARON
		{0x0795, 'u'}, //	LATIN CAPITAL LETTER U WITH DIAERESIS AND MACRON
		{0x0796, 'u'}, //	LATIN SMALL LETTER U WITH DIAERESIS AND MACRON
		{0x0797, 'u'}, //	LATIN CAPITAL LETTER U WITH DIAERESIS AND ACUTE
		{0x0798, 'u'}, //	LATIN SMALL LETTER U WITH DIAERESIS AND ACUTE
		{0x0799, 'u'}, //	LATIN CAPITAL LETTER U WITH DIAERESIS AND CARON
		{0x079a, 'u'}, //	LATIN SMALL LETTER U WITH DIAERESIS AND CARON
		{0x079b, 'u'}, //	LATIN CAPITAL LETTER U WITH DIAERESIS AND GRAVE
		{0x079c, 'u'}, //	LATIN SMALL LETTER U WITH DIAERESIS AND GRAVE
		{0x079d, 'e'}, //	LATIN SMALL LETTER TURNED E
		{0x079e, 'a'}, //	LATIN CAPITAL LETTER A WITH DIAERESIS AND MACRON
		{0x079f, 'a'}, //	LATIN SMALL LETTER A WITH DIAERESIS AND MACRON
		{0x07a0, 'a'}, //	LATIN CAPITAL LETTER A WITH DOT ABOVE AND MACRON
		{0x07a1, 'a'}, //	LATIN SMALL LETTER A WITH DOT ABOVE AND MACRON
		{0x07a4, 'g'}, //	LATIN CAPITAL LETTER G WITH STROKE
		{0x07a5, 'g'}, //	LATIN SMALL LETTER G WITH STROKE
		{0x07a6, 'g'}, //	LATIN CAPITAL LETTER G WITH CARON
		{0x07a7, 'g'}, //	LATIN SMALL LETTER G WITH CARON
		{0x07a8, 'k'}, //	LATIN CAPITAL LETTER K WITH CARON
		{0x07a9, 'k'}, //	LATIN SMALL LETTER K WITH CARON
		{0x07aa, 'o'}, //	LATIN CAPITAL LETTER O WITH OGONEK
		{0x07ab, 'o'}, //	LATIN SMALL LETTER O WITH OGONEK
		{0x07ac, 'o'}, //	LATIN CAPITAL LETTER O WITH OGONEK AND MACRON
		{0x07ad, 'o'}, //	LATIN SMALL LETTER O WITH OGONEK AND MACRON
		{0x07b0, 'j'}, //	LATIN SMALL LETTER J WITH CARON
		{0x07b4, 'g'}, //	LATIN CAPITAL LETTER G WITH ACUTE
		{0x07b5, 'g'}, //	LATIN SMALL LETTER G WITH ACUTE
		{0x07b8, 'n'}, //	LATIN CAPITAL LETTER N WITH GRAVE
		{0x07b9, 'n'}, //	LATIN SMALL LETTER N WITH GRAVE
		{0x07ba, 'a'}, //	LATIN CAPITAL LETTER A WITH RING ABOVE AND ACUTE
		{0x07bb, 'a'}, //	LATIN SMALL LETTER A WITH RING ABOVE AND ACUTE
		{0x07be, 'o'}, //	LATIN CAPITAL LETTER O WITH STROKE AND ACUTE
		{0x07bf, 'o'}, //	LATIN SMALL LETTER O WITH STROKE AND ACUTE
		{0x1d00, 'a'}, // latin letter small capital a -- no decomposition
		{0x1d03, 'b'}, // latin letter small capital barred b -- no decomposition
		{0x1d04, 'c'}, // latin letter small capital c -- no decomposition
		{0x1d05, 'd'}, // latin letter small capital d -- no decomposition
		{0x1d07, 'e'}, // latin letter small capital e -- no decomposition
		{0x1d08, 'e'}, // latin small letter turned open e -- no decomposition
		{0x1d09, 'i'}, // latin small letter turned i -- no decomposition
		{0x1d0a, 'j'}, // latin letter small capital j -- no decomposition
		{0x1d0b, 'k'}, // latin letter small capital k -- no decomposition
		{0x1d0c, 'l'}, // latin letter small capital l with stroke -- no decomposition
		{0x1d0d, 'm'}, // latin letter small capital m -- no decomposition
		{0x1d0e, 'n'}, // latin letter small capital reversed n -- no decomposition
		{0x1d0f, 'o'}, // latin letter small capital o -- no decomposition
		{0x1d10, 'o'}, // latin letter small capital open o -- no decomposition
		{0x1d11, 'o'}, // latin small letter sideways o -- no decomposition
		{0x1d12, 'o'}, // latin small letter sideways open o -- no decomposition
		{0x1d13, 'o'}, // latin small letter sideways o with stroke -- no decomposition
		{0x1d16, 'o'}, // latin small letter top half o -- no decomposition
		{0x1d17, 'o'}, // latin small letter bottom half o -- no decomposition
		{0x1d18, 'p'}, // latin letter small capital p -- no decomposition
		{0x1d19, 'r'}, // latin letter small capital reversed r -- no decomposition
		{0x1d1a, 'r'}, // latin letter small capital turned r -- no decomposition
		{0x1d1b, 't'}, // latin letter small capital t -- no decomposition
		{0x1d1c, 'u'}, // latin letter small capital u -- no decomposition
		{0x1d1d, 'u'}, // latin small letter sideways u -- no decomposition
		{0x1d1e, 'u'}, // latin small letter sideways diaeresized u -- no decomposition
		{0x1d1f, 'm'}, // latin small letter sideways turned m -- no decomposition
		{0x1d20, 'v'}, // latin letter small capital v -- no decomposition
		{0x1d21, 'w'}, // latin letter small capital w -- no decomposition
		{0x1d22, 'z'}, // latin letter small capital z -- no decomposition
		{0x1d25, 'l'}, // latin letter ain -- no decomposition
		{0x1d2c, 'a'}, // modifier letter capital a
		{0x1d2e, 'b'}, // modifier letter capital b
		{0x1d2f, 'b'}, // modifier letter capital barred b -- no decomposition
		{0x1d30, 'd'}, // modifier letter capital d
		{0x1d31, 'e'}, // modifier letter capital e
		{0x1d32, 'e'}, // modifier letter capital reversed e
		{0x1d33, 'g'}, // modifier letter capital g
		{0x1d34, 'h'}, // modifier letter capital h
		{0x1d35, 'i'}, // modifier letter capital i
		{0x1d36, 'j'}, // modifier letter capital j
		{0x1d37, 'k'}, // modifier letter capital k
		{0x1d38, 'l'}, // modifier letter capital l
		{0x1d39, 'm'}, // modifier letter capital m
		{0x1d3a, 'n'}, // modifier letter capital n
		{0x1d3b, 'n'}, // modifier letter capital reversed n -- no decomposition
		{0x1d3c, 'o'}, // modifier letter capital o
		{0x1d3d, 'ou'}, // modifier letter capital ou
		{0x1d3e, 'p'}, // modifier letter capital p
		{0x1d3f, 'r'}, // modifier letter capital r
		{0x1d40, 't'}, // modifier letter capital t
		{0x1d41, 'u'}, // modifier letter capital u
		{0x1d42, 'w'}, // modifier letter capital w
		{0x1d43, 'a'}, // modifier letter small a
		{0x1d44, 'a'}, // modifier letter small turned a
		{0x1d46, 'ae'}, // modifier letter small turned ae
		{0x1d47, 'b'}, // modifier letter small b
		{0x1d48, 'd'}, // modifier letter small d
		{0x1d49, 'e'}, // modifier letter small e
		{0x1d4a, 'e'}, // modifier letter small schwa
		{0x1d4b, 'e'}, // modifier letter small open e
		{0x1d4c, 'e'}, // modifier letter small turned open e
		{0x1d4d, 'g'}, // modifier letter small g
		{0x1d4e, 'i'}, // modifier letter small turned i -- no decomposition
		{0x1d4f, 'k'}, // modifier letter small k
		{0x1d50, 'm'}, // modifier letter small m
		{0x1d51, 'g'}, // modifier letter small eng
		{0x1d52, 'o'}, // modifier letter small o
		{0x1d53, 'o'}, // modifier letter small open o
		{0x1d54, 'o'}, // modifier letter small top half o
		{0x1d55, 'o'}, // modifier letter small bottom half o
		{0x1d56, 'p'}, // modifier letter small p
		{0x1d57, 't'}, // modifier letter small t
		{0x1d58, 'u'}, // modifier letter small u
		{0x1d59, 'u'}, // modifier letter small sideways u
		{0x1d5a, 'm'}, // modifier letter small turned m
		{0x1d5b, 'v'}, // modifier letter small v
		{0x1d62, 'i'}, // latin subscript small letter i
		{0x1d63, 'r'}, // latin subscript small letter r
		{0x1d64, 'u'}, // latin subscript small letter u
		{0x1d65, 'v'}, // latin subscript small letter v
		{0x1e00, 'a'}, // latin capital letter a with ring below
		{0x1e01, 'a'}, // latin small letter a with ring below
		{0x1e02, 'b'}, // latin capital letter b with dot above
		{0x1e03, 'b'}, // latin small letter b with dot above
		{0x1e04, 'b'}, // latin capital letter b with dot below
		{0x1e05, 'b'}, // latin small letter b with dot below
		{0x1e06, 'b'}, // latin capital letter b with line below
		{0x1e07, 'b'}, // latin small letter b with line below
		{0x1e08, 'c'}, // latin capital letter c with cedilla and acute
		{0x1e09, 'c'}, // latin small letter c with cedilla and acute
		{0x1e0a, 'd'}, // latin capital letter d with dot above
		{0x1e0b, 'd'}, // latin small letter d with dot above
		{0x1e0c, 'd'}, // latin capital letter d with dot below
		{0x1e0d, 'd'}, // latin small letter d with dot below
		{0x1e0e, 'd'}, // latin capital letter d with line below
		{0x1e0f, 'd'}, // latin small letter d with line below
		{0x1e10, 'd'}, // latin capital letter d with cedilla
		{0x1e11, 'd'}, // latin small letter d with cedilla
		{0x1e12, 'd'}, // latin capital letter d with circumflex below
		{0x1e13, 'd'}, // latin small letter d with circumflex below
		{0x1e14, 'e'}, // latin capital letter e with macron and grave
		{0x1e15, 'e'}, // latin small letter e with macron and grave
		{0x1e16, 'e'}, // latin capital letter e with macron and acute
		{0x1e17, 'e'}, // latin small letter e with macron and acute
		{0x1e18, 'e'}, // latin capital letter e with circumflex below
		{0x1e19, 'e'}, // latin small letter e with circumflex below
		{0x1e1a, 'e'}, // latin capital letter e with tilde below
		{0x1e1b, 'e'}, // latin small letter e with tilde below
		{0x1e1c, 'e'}, // latin capital letter e with cedilla and breve
		{0x1e1d, 'e'}, // latin small letter e with cedilla and breve
		{0x1e1e, 'f'}, // latin capital letter f with dot above
		{0x1e1f, 'f'}, // latin small letter f with dot above
		{0x1e20, 'g'}, // latin capital letter g with macron
		{0x1e21, 'g'}, // latin small letter g with macron
		{0x1e22, 'h'}, // latin capital letter h with dot above
		{0x1e23, 'h'}, // latin small letter h with dot above
		{0x1e24, 'h'}, // latin capital letter h with dot below
		{0x1e25, 'h'}, // latin small letter h with dot below
		{0x1e26, 'h'}, // latin capital letter h with diaeresis
		{0x1e27, 'h'}, // latin small letter h with diaeresis
		{0x1e28, 'h'}, // latin capital letter h with cedilla
		{0x1e29, 'h'}, // latin small letter h with cedilla
		{0x1e2a, 'h'}, // latin capital letter h with breve below
		{0x1e2b, 'h'}, // latin small letter h with breve below
		{0x1e2c, 'i'}, // latin capital letter i with tilde below
		{0x1e2d, 'i'}, // latin small letter i with tilde below
		{0x1e2e, 'i'}, // latin capital letter i with diaeresis and acute
		{0x1e2f, 'i'}, // latin small letter i with diaeresis and acute
		{0x1e30, 'k'}, // latin capital letter k with acute
		{0x1e31, 'k'}, // latin small letter k with acute
		{0x1e32, 'k'}, // latin capital letter k with dot below
		{0x1e33, 'k'}, // latin small letter k with dot below
		{0x1e34, 'k'}, // latin capital letter k with line below
		{0x1e35, 'k'}, // latin small letter k with line below
		{0x1e36, 'l'}, // latin capital letter l with dot below
		{0x1e37, 'l'}, // latin small letter l with dot below
		{0x1e38, 'l'}, // latin capital letter l with dot below and macron
		{0x1e39, 'l'}, // latin small letter l with dot below and macron
		{0x1e3a, 'l'}, // latin capital letter l with line below
		{0x1e3b, 'l'}, // latin small letter l with line below
		{0x1e3c, 'l'}, // latin capital letter l with circumflex below
		{0x1e3d, 'l'}, // latin small letter l with circumflex below
		{0x1e3e, 'm'}, // latin capital letter m with acute
		{0x1e3f, 'm'}, // latin small letter m with acute
		{0x1e40, 'm'}, // latin capital letter m with dot above
		{0x1e41, 'm'}, // latin small letter m with dot above
		{0x1e42, 'm'}, // latin capital letter m with dot below
		{0x1e43, 'm'}, // latin small letter m with dot below
		{0x1e44, 'n'}, // latin capital letter n with dot above
		{0x1e45, 'n'}, // latin small letter n with dot above
		{0x1e46, 'n'}, // latin capital letter n with dot below
		{0x1e47, 'n'}, // latin small letter n with dot below
		{0x1e48, 'n'}, // latin capital letter n with line below
		{0x1e49, 'n'}, // latin small letter n with line below
		{0x1e4a, 'n'}, // latin capital letter n with circumflex below
		{0x1e4b, 'n'}, // latin small letter n with circumflex below
		{0x1e4c, 'o'}, // latin capital letter o with tilde and acute
		{0x1e4d, 'o'}, // latin small letter o with tilde and acute
		{0x1e4e, 'o'}, // latin capital letter o with tilde and diaeresis
		{0x1e4f, 'o'}, // latin small letter o with tilde and diaeresis
		{0x1e50, 'o'}, // latin capital letter o with macron and grave
		{0x1e51, 'o'}, // latin small letter o with macron and grave
		{0x1e52, 'o'}, // latin capital letter o with macron and acute
		{0x1e53, 'o'}, // latin small letter o with macron and acute
		{0x1e54, 'p'}, // latin capital letter p with acute
		{0x1e55, 'p'}, // latin small letter p with acute
		{0x1e56, 'p'}, // latin capital letter p with dot above
		{0x1e57, 'p'}, // latin small letter p with dot above
		{0x1e58, 'r'}, // latin capital letter r with dot above
		{0x1e59, 'r'}, // latin small letter r with dot above
		{0x1e5a, 'r'}, // latin capital letter r with dot below
		{0x1e5b, 'r'}, // latin small letter r with dot below
		{0x1e5c, 'r'}, // latin capital letter r with dot below and macron
		{0x1e5d, 'r'}, // latin small letter r with dot below and macron
		{0x1e5e, 'r'}, // latin capital letter r with line below
		{0x1e5f, 'r'}, // latin small letter r with line below
		{0x1e60, 's'}, // latin capital letter s with dot above
		{0x1e61, 's'}, // latin small letter s with dot above
		{0x1e62, 's'}, // latin capital letter s with dot below
		{0x1e63, 's'}, // latin small letter s with dot below
		{0x1e64, 's'}, // latin capital letter s with acute and dot above
		{0x1e65, 's'}, // latin small letter s with acute and dot above
		{0x1e66, 's'}, // latin capital letter s with caron and dot above
		{0x1e67, 's'}, // latin small letter s with caron and dot above
		{0x1e68, 's'}, // latin capital letter s with dot below and dot above
		{0x1e69, 's'}, // latin small letter s with dot below and dot above
		{0x1e6a, 't'}, // latin capital letter t with dot above
		{0x1e6b, 't'}, // latin small letter t with dot above
		{0x1e6c, 't'}, // latin capital letter t with dot below
		{0x1e6d, 't'}, // latin small letter t with dot below
		{0x1e6e, 't'}, // latin capital letter t with line below
		{0x1e6f, 't'}, // latin small letter t with line below
		{0x1e70, 't'}, // latin capital letter t with circumflex below
		{0x1e71, 't'}, // latin small letter t with circumflex below
		{0x1e72, 'u'}, // latin capital letter u with diaeresis below
		{0x1e73, 'u'}, // latin small letter u with diaeresis below
		{0x1e74, 'u'}, // latin capital letter u with tilde below
		{0x1e75, 'u'}, // latin small letter u with tilde below
		{0x1e76, 'u'}, // latin capital letter u with circumflex below
		{0x1e77, 'u'}, // latin small letter u with circumflex below
		{0x1e78, 'u'}, // latin capital letter u with tilde and acute
		{0x1e79, 'u'}, // latin small letter u with tilde and acute
		{0x1e7a, 'u'}, // latin capital letter u with macron and diaeresis
		{0x1e7b, 'u'}, // latin small letter u with macron and diaeresis
		{0x1e7c, 'v'}, // latin capital letter v with tilde
		{0x1e7d, 'v'}, // latin small letter v with tilde
		{0x1e7e, 'v'}, // latin capital letter v with dot below
		{0x1e7f, 'v'}, // latin small letter v with dot below
		{0x1e80, 'w'}, // latin capital letter w with grave
		{0x1e81, 'w'}, // latin small letter w with grave
		{0x1e82, 'w'}, // latin capital letter w with acute
		{0x1e83, 'w'}, // latin small letter w with acute
		{0x1e84, 'w'}, // latin capital letter w with diaeresis
		{0x1e85, 'w'}, // latin small letter w with diaeresis
		{0x1e86, 'w'}, // latin capital letter w with dot above
		{0x1e87, 'w'}, // latin small letter w with dot above
		{0x1e88, 'w'}, // latin capital letter w with dot below
		{0x1e89, 'w'}, // latin small letter w with dot below
		{0x1e8a, 'x'}, // latin capital letter x with dot above
		{0x1e8b, 'x'}, // latin small letter x with dot above
		{0x1e8c, 'x'}, // latin capital letter x with diaeresis
		{0x1e8d, 'x'}, // latin small letter x with diaeresis
		{0x1e8e, 'y'}, // latin capital letter y with dot above
		{0x1e8f, 'y'}, // latin small letter y with dot above
		{0x1e90, 'z'}, // latin capital letter z with circumflex
		{0x1e91, 'z'}, // latin small letter z with circumflex
		{0x1e92, 'z'}, // latin capital letter z with dot below
		{0x1e93, 'z'}, // latin small letter z with dot below
		{0x1e94, 'z'}, // latin capital letter z with line below
		{0x1e95, 'z'}, // latin small letter z with line below
		{0x1e96, 'h'}, // latin small letter h with line below
		{0x1e97, 't'}, // latin small letter t with diaeresis
		{0x1e98, 'w'}, // latin small letter w with ring above
		{0x1e99, 'y'}, // latin small letter y with ring above
		{0x1e9a, 'a'}, // latin small letter a with right half ring
		{0x1e9b, 's'}, // latin small letter long s with dot above
		{0x1ea0, 'a'}, // latin capital letter a with dot below
		{0x1ea1, 'a'}, // latin small letter a with dot below
		{0x1ea2, 'a'}, // latin capital letter a with hook above
		{0x1ea3, 'a'}, // latin small letter a with hook above
		{0x1ea4, 'a'}, // latin capital letter a with circumflex and acute
		{0x1ea5, 'a'}, // latin small letter a with circumflex and acute
		{0x1ea6, 'a'}, // latin capital letter a with circumflex and grave
		{0x1ea7, 'a'}, // latin small letter a with circumflex and grave
		{0x1ea8, 'a'}, // latin capital letter a with circumflex and hook above
		{0x1ea9, 'a'}, // latin small letter a with circumflex and hook above
		{0x1eaa, 'a'}, // latin capital letter a with circumflex and tilde
		{0x1eab, 'a'}, // latin small letter a with circumflex and tilde
		{0x1eac, 'a'}, // latin capital letter a with circumflex and dot below
		{0x1ead, 'a'}, // latin small letter a with circumflex and dot below
		{0x1eae, 'a'}, // latin capital letter a with breve and acute
		{0x1eaf, 'a'}, // latin small letter a with breve and acute
		{0x1eb0, 'a'}, // latin capital letter a with breve and grave
		{0x1eb1, 'a'}, // latin small letter a with breve and grave
		{0x1eb2, 'a'}, // latin capital letter a with breve and hook above
		{0x1eb3, 'a'}, // latin small letter a with breve and hook above
		{0x1eb4, 'a'}, // latin capital letter a with breve and tilde
		{0x1eb5, 'a'}, // latin small letter a with breve and tilde
		{0x1eb6, 'a'}, // latin capital letter a with breve and dot below
		{0x1eb7, 'a'}, // latin small letter a with breve and dot below
		{0x1eb8, 'e'}, // latin capital letter e with dot below
		{0x1eb9, 'e'}, // latin small letter e with dot below
		{0x1eba, 'e'}, // latin capital letter e with hook above
		{0x1ebb, 'e'}, // latin small letter e with hook above
		{0x1ebc, 'e'}, // latin capital letter e with tilde
		{0x1ebd, 'e'}, // latin small letter e with tilde
		{0x1ebe, 'e'}, // latin capital letter e with circumflex and acute
		{0x1ebf, 'e'}, // latin small letter e with circumflex and acute
		{0x1ec0, 'e'}, // latin capital letter e with circumflex and grave
		{0x1ec1, 'e'}, // latin small letter e with circumflex and grave
		{0x1ec2, 'e'}, // latin capital letter e with circumflex and hook above
		{0x1ec3, 'e'}, // latin small letter e with circumflex and hook above
		{0x1ec4, 'e'}, // latin capital letter e with circumflex and tilde
		{0x1ec5, 'e'}, // latin small letter e with circumflex and tilde
		{0x1ec6, 'e'}, // latin capital letter e with circumflex and dot below
		{0x1ec7, 'e'}, // latin small letter e with circumflex and dot below
		{0x1ec8, 'i'}, // latin capital letter i with hook above
		{0x1ec9, 'i'}, // latin small letter i with hook above
		{0x1eca, 'i'}, // latin capital letter i with dot below
		{0x1ecb, 'i'}, // latin small letter i with dot below
		{0x1ecc, 'o'}, // latin capital letter o with dot below
		{0x1ecd, 'o'}, // latin small letter o with dot below
		{0x1ece, 'o'}, // latin capital letter o with hook above
		{0x1ecf, 'o'}, // latin small letter o with hook above
		{0x1ed0, 'o'}, // latin capital letter o with circumflex and acute
		{0x1ed1, 'o'}, // latin small letter o with circumflex and acute
		{0x1ed2, 'o'}, // latin capital letter o with circumflex and grave
		{0x1ed3, 'o'}, // latin small letter o with circumflex and grave
		{0x1ed4, 'o'}, // latin capital letter o with circumflex and hook above
		{0x1ed5, 'o'}, // latin small letter o with circumflex and hook above
		{0x1ed6, 'o'}, // latin capital letter o with circumflex and tilde
		{0x1ed7, 'o'}, // latin small letter o with circumflex and tilde
		{0x1ed8, 'o'}, // latin capital letter o with circumflex and dot below
		{0x1ed9, 'o'}, // latin small letter o with circumflex and dot below
		{0x1eda, 'o'}, // latin capital letter o with horn and acute
		{0x1edb, 'o'}, // latin small letter o with horn and acute
		{0x1edc, 'o'}, // latin capital letter o with horn and grave
		{0x1edd, 'o'}, // latin small letter o with horn and grave
		{0x1ede, 'o'}, // latin capital letter o with horn and hook above
		{0x1edf, 'o'}, // latin small letter o with horn and hook above
		{0x1ee0, 'o'}, // latin capital letter o with horn and tilde
		{0x1ee1, 'o'}, // latin small letter o with horn and tilde
		{0x1ee2, 'o'}, // latin capital letter o with horn and dot below
		{0x1ee3, 'o'}, // latin small letter o with horn and dot below
		{0x1ee4, 'u'}, // latin capital letter u with dot below
		{0x1ee5, 'u'}, // latin small letter u with dot below
		{0x1ee6, 'u'}, // latin capital letter u with hook above
		{0x1ee7, 'u'}, // latin small letter u with hook above
		{0x1ee8, 'u'}, // latin capital letter u with horn and acute
		{0x1ee9, 'u'}, // latin small letter u with horn and acute
		{0x1eea, 'u'}, // latin capital letter u with horn and grave
		{0x1eeb, 'u'}, // latin small letter u with horn and grave
		{0x1eec, 'u'}, // latin capital letter u with horn and hook above
		{0x1eed, 'u'}, // latin small letter u with horn and hook above
		{0x1eee, 'u'}, // latin capital letter u with horn and tilde
		{0x1eef, 'u'}, // latin small letter u with horn and tilde
		{0x1ef0, 'u'}, // latin capital letter u with horn and dot below
		{0x1ef1, 'u'}, // latin small letter u with horn and dot below		
		{0x1ef2, 'y'}, // latin capital letter y with grave
		{0x1ef3, 'y'}, // latin small letter y with grave
		{0x1ef4, 'y'}, // latin capital letter y with dot below
		{0x1ef5, 'y'}, // latin small letter y with dot below
		{0x1ef6, 'y'}, // latin capital letter y with hook above
		{0x1ef7, 'y'}, // latin small letter y with hook above
		{0x1ef8, 'y'}, // latin capital letter y with tilde
		{0x1ef9, 'y'}, // latin small letter y with tilde
		{0x2071, 'i'}, // superscript latin small letter i
		{0x207f, 'n'}, // superscript latin small letter n
		{0x212a, 'k'}, // kelvin sign
		{0x212b, 'a'}, // angstrom sign
		{0x212c, 'b'}, // script capital b
		{0x212d, 'c'}, // black-letter capital c
		{0x212f, 'e'}, // script small e
		{0x2130, 'e'}, // script capital e
		{0x2131, 'f'}, // script capital f
		{0x2132, 'f'}, // turned capital f -- no decomposition
		{0x2133, 'm'}, // script capital m
		{0x2134, '0'}, // script small o
		{0x213a, '0'}, // rotated capital q -- no decomposition
		{0x2141, 'g'}, // turned sans-serif capital g -- no decomposition
		{0x2142, 'l'}, // turned sans-serif capital l -- no decomposition
		{0x2143, 'l'}, // reversed sans-serif capital l -- no decomposition
		{0x2144, 'y'}, // turned sans-serif capital y -- no decomposition
		{0x2145, 'd'}, // double-struck italic capital d
		{0x2146, 'd'}, // double-struck italic small d
		{0x2147, 'e'}, // double-struck italic small e
		{0x2148, 'i'}, // double-struck italic small i
		{0x2149, 'j'}, // double-struck italic small j
		{0xff21, 'a'}, // fullwidth latin capital letter b
		{0xff22, 'b'}, // fullwidth latin capital letter b
		{0xff23, 'c'}, // fullwidth latin capital letter c
		{0xff24, 'd'}, // fullwidth latin capital letter d
		{0xff25, 'e'}, // fullwidth latin capital letter e
		{0xff26, 'f'}, // fullwidth latin capital letter f
		{0xff27, 'g'}, // fullwidth latin capital letter g
		{0xff28, 'h'}, // fullwidth latin capital letter h
		{0xff29, 'i'}, // fullwidth latin capital letter i
		{0xff2a, 'j'}, // fullwidth latin capital letter j
		{0xff2b, 'k'}, // fullwidth latin capital letter k
		{0xff2c, 'l'}, // fullwidth latin capital letter l
		{0xff2d, 'm'}, // fullwidth latin capital letter m
		{0xff2e, 'n'}, // fullwidth latin capital letter n
		{0xff2f, 'o'}, // fullwidth latin capital letter o
		{0xff30, 'p'}, // fullwidth latin capital letter p
		{0xff31, 'q'}, // fullwidth latin capital letter q
		{0xff32, 'r'}, // fullwidth latin capital letter r
		{0xff33, 's'}, // fullwidth latin capital letter s
		{0xff34, 't'}, // fullwidth latin capital letter t
		{0xff35, 'u'}, // fullwidth latin capital letter u
		{0xff36, 'v'}, // fullwidth latin capital letter v
		{0xff37, 'w'}, // fullwidth latin capital letter w
		{0xff38, 'x'}, // fullwidth latin capital letter x
		{0xff39, 'y'}, // fullwidth latin capital letter y
		{0xff3a, 'z'}, // fullwidth latin capital letter z
		{0xff41, 'a'}, // fullwidth latin small letter a
		{0xff42, 'b'}, // fullwidth latin small letter b
		{0xff43, 'c'}, // fullwidth latin small letter c
		{0xff44, 'd'}, // fullwidth latin small letter d
		{0xff45, 'e'}, // fullwidth latin small letter e
		{0xff46, 'f'}, // fullwidth latin small letter f
		{0xff47, 'g'}, // fullwidth latin small letter g
		{0xff48, 'h'}, // fullwidth latin small letter h
		{0xff49, 'i'}, // fullwidth latin small letter i
		{0xff4a, 'j'}, // fullwidth latin small letter j
		{0xff4b, 'k'}, // fullwidth latin small letter k
		{0xff4c, 'l'}, // fullwidth latin small letter l
		{0xff4d, 'm'}, // fullwidth latin small letter m
		{0xff4e, 'n'}, // fullwidth latin small letter n
		{0xff4f, 'o'}, // fullwidth latin small letter o
		{0xff50, 'p'}, // fullwidth latin small letter p
		{0xff51, 'q'}, // fullwidth latin small letter q
		{0xff52, 'r'}, // fullwidth latin small letter r
		{0xff53, 's'}, // fullwidth latin small letter s
		{0xff54, 't'}, // fullwidth latin small letter t
		{0xff55, 'u'}, // fullwidth latin small letter u
		{0xff56, 'v'}, // fullwidth latin small letter v
		{0xff57, 'w'}, // fullwidth latin small letter w
		{0xff58, 'x'}, // fullwidth latin small letter x
		{0xff59, 'y'}, // fullwidth latin small letter y
		{0xff5a, 'z'}, // fullwidth latin small letter z
	};

	return normalizations;
}

std::string str::utf8_to_a(const std::u8string_view utf8)
{
	return platform::utf8_to_a(utf8);
}

int str::normalze_for_compare(const int c)
{
	if (c < 128)
	{
		constexpr int ascii_mapping[] = {
			0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
			0x8, 0x20, 0x20, 0x20, 0x20, 0x20, 0xe, 0xf,
			0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
			0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
			0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
			0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
			0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
			0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
			0x40, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
			0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
			0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
			0x78, 0x79, 0x7a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
			0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
			0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
			0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
			0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
		};

		return ascii_mapping[c];
	}

	static auto normalizations = make_normalizations();
	const auto found = normalizations.find(c);
	if (found != normalizations.cend()) return found->second;
	return to_lower(c);
}


bool str::wildcard_icmp(const std::u8string_view text_in, const std::u8string_view wildcard_in)
{
	auto is_match = true;

	// The location in the tame string, from which we started after last wildcard
	auto text = text_in.begin();
	auto wildcard = wildcard_in.begin();
	const auto wildcard_end = wildcard_in.end();
	const auto text_end = text_in.end();
	auto post_last_wildcard = wildcard_end; // The location after the last '*', if we’ve encountered one
	auto pos_last_text = text_end;

	// Walk the text strings one character at a time.
	while (true)
	{
		//const auto t = *text;
		//const auto w = *wildcard;

		// How do you match a unique text string?
		if (text == text_end)
		{
			// Easy: unique up on it!
			if (wildcard == wildcard_end)
			{
				break; // "x" matches "x"
			}
			if (peek_utf8_char(wildcard, wildcard_end) == '*')
			{
				pop_utf8_char(wildcard, wildcard_end);
				continue; // "x*" matches "x"sv or "xy"
			}
			if (pos_last_text != text_end)
			{
				if (pos_last_text != text_end)
				{
					is_match = false;
					break;
				}
				pop_utf8_char(pos_last_text, text_end);
				text = pos_last_text;
				wildcard = post_last_wildcard;
				continue;
			}

			is_match = false;
			break; // "x" doesn't match "xy"
		}

		if (wildcard == wildcard_end && text != text_end)
		{
			is_match = false;
			break;
		}

		const auto t = normalze_for_compare(peek_utf8_char(text, text_end));
		const auto w = normalze_for_compare(peek_utf8_char(wildcard, wildcard_end));

		// How do you match a tame text string?
		if (t != w)
		{
			// The tame way: unique up on it!
			if (w == '*')
			{
				pop_utf8_char(wildcard, wildcard_end);
				post_last_wildcard = wildcard;
				pos_last_text = text;

				if (wildcard == wildcard_end)
				{
					break; // "*" matches "x"
				}
				continue; // "*y" matches "xy"
			}
			if (post_last_wildcard != wildcard_end)
			{
				if (post_last_wildcard != wildcard)
				{
					wildcard = post_last_wildcard;

					if (t == normalze_for_compare(peek_utf8_char(wildcard, wildcard_end)))
					{
						pop_utf8_char(wildcard, wildcard_end);
					}
				}
				pop_utf8_char(text, text_end);
				continue; // "*sip*" matches "mississippi"
			}
			is_match = false;
			break; // "x" doesn't match "y"
		}

		pop_utf8_char(text, text_end);
		pop_utf8_char(wildcard, wildcard_end);
	}

	return is_match;
}
