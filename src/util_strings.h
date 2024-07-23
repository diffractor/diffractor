// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

class sizei;

namespace str
{
	constexpr size_t len(const std::u8string_view sv)
	{
		return sv.size();
	}

	constexpr size_t len(const std::string_view sv)
	{
		return sv.size();
	}

	constexpr size_t len(const std::wstring_view sv)
	{
		return sv.size();
	}

	constexpr bool is_empty(const std::wstring_view sv)
	{
		return sv.empty() || sv[0] == 0;
	}

	constexpr bool is_empty(const wchar_t* sz)
	{
		return sz == nullptr || sz[0] == 0;
	}

	constexpr bool is_empty(const std::u8string_view sv)
	{
		return sv.empty() || sv[0] == 0;
	}

	constexpr bool is_empty(const char8_t* sz)
	{
		return sz == nullptr || sz[0] == 0;
	}

	constexpr bool is_empty(const char* sz)
	{
		return sz == nullptr || sz[0] == 0;
	}

	inline std::u8string safe_string(const char8_t* sz)
	{
		return is_empty(sz) ? std::u8string{} : sz;
	}

	inline std::string safe_string(const char* sz)
	{
		return is_empty(sz) ? std::string{} : sz;
	}

	__forceinline constexpr uint32_t pop_utf8_char(std::u8string_view::const_iterator& in_ptr,
	                                               const std::u8string_view::const_iterator& end)
	{
		const auto c1 = *in_ptr++;

		if (c1 < 0x80) // 1 octet
		{
			return c1;
		}
		if ((c1 >> 5) == 0x6) // 2 octets
		{
			if (std::distance(in_ptr, end) < 1)
			{
				in_ptr = end;
				return 0;
			}

			uint32_t c = (c1 & 0x1F) << 6;
			c |= ((*in_ptr++ & 0x3F) << 0);
			return c;
		}
		if ((c1 >> 4) == 0xe)  // 3 octets
		{
			if (std::distance(in_ptr, end) < 2)
			{
				in_ptr = end;
				return 0;
			}

			uint32_t c = ((c1 & 0x0F) << 12);
			c |= ((*in_ptr++ & 0x3F) << 6);
			c |= ((*in_ptr++ & 0x3F) << 0);
			return c;
		}
		if ((c1 >> 3) == 0x1e)  // 4 octets
		{
			if (std::distance(in_ptr, end) < 3)
			{
				in_ptr = end;
				return 0;
			}

			uint32_t c = ((c1 & 0x0F) << 18);
			c |= ((*in_ptr++ & 0x3F) << 12);
			c |= ((*in_ptr++ & 0x3F) << 6);
			c |= ((*in_ptr++ & 0x3F) << 0);
			return c;
		}

		return '?';
	}

	__forceinline constexpr uint32_t peek_utf8_char(std::u8string_view::const_iterator in_ptr,
		const std::u8string_view::const_iterator& end)
	{
		return pop_utf8_char(in_ptr, end);
	}

	struct part_t
	{
		size_t offset = 0;
		size_t length = 0;
	};

	struct chached_string_storage_t
	{
		uint32_t len;
		char8_t sz[1];
	};

	struct cached
	{
		const chached_string_storage_t* storage = nullptr;

		cached() noexcept = default;

		cached(const chached_string_storage_t* s) noexcept : storage(s)
		{
		}

		cached(const cached&) noexcept = default;
		cached& operator=(const cached&) noexcept = default;
		cached(cached&&) noexcept = default;
		cached& operator=(cached&&) noexcept = default;

		void clear()
		{
			storage = nullptr;
		}

		constexpr bool is_empty() const
		{
			// capture storage for better thread safety
			const auto s = storage;
			return s == nullptr || s->len == 0;
		}

		constexpr size_t size() const
		{
			// capture storage for better thread safety
			const auto s = storage;
			return s ? s->len : 0;
		}

		constexpr std::u8string_view substr(const size_t sub_pos) const
		{
			// capture storage for better thread safety
			const auto s = storage;
			const auto len = s ? s->len : 0;
			df::assert_true(sub_pos <= len);
			if (s == nullptr || s->len == 0 || sub_pos >= len) return {};
			return {s->sz + sub_pos, s->len - sub_pos};
		}

		constexpr std::u8string_view substr(const size_t sub_pos, const size_t sub_len) const
		{
			// capture storage for better thread safety
			const auto s = storage;
			const auto len = s ? s->len : 0;
			df::assert_true(sub_pos <= len && sub_len <= len - sub_pos);
			if (s == nullptr || s->len == 0 || sub_pos >= len || (sub_pos + sub_len) > len) return {};
			return {s->sz + sub_pos, sub_len};
		}

		char8_t operator[](const uint32_t i) const noexcept
		{
			// capture storage for better thread safety
			const auto s = storage;
			df::assert_true(s != nullptr && s->len > i);
			if (s == nullptr || s->len >= i) return 0;
			return s->sz[i];
		}

		operator std::u8string_view() const
		{
			return sv();
		}

		std::u8string_view sv() const
		{
			// capture storage for better thread safety
			const auto s = storage;
			return s ? std::u8string_view(s->sz, s->len) : std::u8string_view{};
		}

		std::u8string str() const
		{
			// capture storage for better thread safety
			const auto s = storage;
			return s ? std::u8string(s->sz, s->len) : std::u8string{};
		}

		bool operator==(const cached other) const
		{
			return storage == other.storage;
		}

		bool operator !=(const cached other) const
		{
			return storage != other.storage;
		}

		bool operator==(const std::u8string_view other) const
		{
			return other == sv();
		}

		bool operator !=(const std::u8string_view other) const
		{
			return other != sv();
		}

		bool operator <(const cached other) const
		{
			return storage < other.storage;
		}

		const char8_t* sz() const
		{
			// capture storage for better thread safety
			const auto s = storage;
			return s ? s->sz : u8"";
		}
	};

	constexpr bool is_empty(const cached c)
	{
		return c.is_empty();
	}

	template <class output_it>
	void char32_to_utf8(output_it&& inserter, const uint32_t ch)
	{
		if (ch < 0x80)
		{
			*(inserter++) = static_cast<uint8_t>(ch);
		}
		else if (ch < 0x800)
		{
			*(inserter++) = static_cast<uint8_t>(0xC0 | (ch >> 6));
			*(inserter++) = static_cast<uint8_t>(0x80 | ((ch >> 0) & 0x3F));
		}
		else if (ch < 0x10000)
		{
			*(inserter++) = static_cast<uint8_t>(0xE0 | (ch >> 12));
			*(inserter++) = static_cast<uint8_t>(0x80 | ((ch >> 6) & 0x3F));
			*(inserter++) = static_cast<uint8_t>(0x80 | ((ch >> 0) & 0x3F));
		}
		else
		{
			*(inserter++) = static_cast<uint8_t>((ch >> 18) | 0xf0);
			*(inserter++) = static_cast<uint8_t>(((ch >> 12) & 0x3f) | 0x80);
			*(inserter++) = static_cast<uint8_t>(((ch >> 6) & 0x3f) | 0x80);
			*(inserter++) = static_cast<uint8_t>((ch & 0x3f) | 0x80);
		}
	}

	constexpr uint32_t LEAD_SURROGATE_MIN = 0xd800u;
	constexpr uint32_t LEAD_SURROGATE_MAX = 0xdbffu;
	constexpr uint32_t TRAIL_SURROGATE_MIN = 0xdc00u;
	constexpr uint32_t TRAIL_SURROGATE_MAX = 0xdfffu;
	constexpr uint32_t LEAD_OFFSET = LEAD_SURROGATE_MIN - (0x10000 >> 10);
	constexpr uint32_t SURROGATE_OFFSET = 0xfca02400u; //  0x10000u - (LEAD_SURROGATE_MIN << 10) - TRAIL_SURROGATE_MIN;

	inline bool is_lead_surrogate(const uint32_t cp)
	{
		return (cp >= LEAD_SURROGATE_MIN && cp <= LEAD_SURROGATE_MAX);
	}

	inline bool is_trail_surrogate(const uint32_t cp)
	{
		return (cp >= TRAIL_SURROGATE_MIN && cp <= TRAIL_SURROGATE_MAX);
	}

	inline uint16_t mask16(const uint32_t oc)
	{
		return static_cast<uint16_t>(0xffff & oc);
	}

	inline std::u8string utf16_to_utf8(const std::wstring_view s)
	{
		std::u8string result;
		result.reserve(s.size() * 3);
		auto inserter = std::back_inserter(result);

		auto start = s.begin();
		const auto end = s.end();

		while (start != end)
		{
			uint32_t cp = mask16(*start++);

			if (is_lead_surrogate(cp)) 
			{
				if (start != end) 
				{
					const uint32_t trail_surrogate = mask16(*start++);

					if (is_trail_surrogate(trail_surrogate))
					{
						cp = (cp << 10) + trail_surrogate + SURROGATE_OFFSET;
					}
					else
					{
						throw std::invalid_argument("Invalid input string"s);
					}
				}
				else
				{
					throw std::invalid_argument("Invalid input string"s);
				}

			}
			// Lone trail surrogate
			else if (is_trail_surrogate(cp))
			{
				throw std::invalid_argument("Invalid input string"s);
			}

			char32_to_utf8(inserter, cp);
		}

		return result;
	};

	inline std::wstring utf8_to_utf16(const std::u8string_view s)
	{
		std::wstring result;
		result.reserve(s.size());
		auto i = s.begin();
		while (i < s.end())
		{
			const auto cp = pop_utf8_char(i, s.end());

			if (cp > 0xffff) 
			{
				result += static_cast<uint16_t>((cp >> 10) + LEAD_OFFSET);
				result += static_cast<uint16_t>((cp & 0x3ff) + TRAIL_SURROGATE_MIN);
			}
			else
			{
				result += static_cast<uint16_t>(cp);
			}
		}
		return result;
	}

	inline std::u32string utf8_to_utf32(const std::u8string_view s)
	{
		std::u32string result;
		result.reserve(s.size());
		auto i = s.begin();
		while (i < s.end()) result += pop_utf8_char(i, s.end());
		return result;
	}

	std::string utf8_to_a(std::u8string_view utf8);

	inline std::string_view utf8_cast(const std::u8string_view val)
	{
		return {std::bit_cast<const char*>(val.data()), val.size()};
	}

	inline std::string utf8_cast2(const std::u8string_view val)
	{
		return {val.begin(), val.end()};
	}

	inline std::u8string_view utf8_cast(const std::string_view val)
	{
		return {std::bit_cast<const char8_t*>(val.data()), val.size()};
	}

	inline std::u8string utf8_cast2(const std::string_view val)
	{
		return {val.begin(), val.end()};
	}

	constexpr int to_lower(const int c)
	{
		if (c < 128) return ((c >= L'A') && (c <= L'Z')) ? c - L'A' + L'a' : c;
		if (c > USHRT_MAX) return c;
		return towlower(c);
	}

	constexpr int to_upper(const int c)
	{
		if (c < 128) return ((c >= L'a') && (c <= L'z')) ? c - L'a' + L'A' : c;
		if (c > USHRT_MAX) return c;
		return towupper(c);
	}

	inline std::u8string to_lower(const std::u8string_view s)
	{
		std::u8string result;
		result.reserve(s.size());
		auto inserter = std::back_inserter(result);

		auto i = s.begin();
		while (i < s.end())
		{
			const auto cp = pop_utf8_char(i, s.end());
			char32_to_utf8(inserter, to_lower(cp));
		}

		return result;
	}

	inline std::u8string to_upper(const std::u8string_view s)
	{
		std::u8string result;
		result.reserve(s.size());
		auto inserter = std::back_inserter(result);

		auto i = s.begin();
		while (i < s.end())
		{
			const auto cp = pop_utf8_char(i, s.end());
			char32_to_utf8(inserter, to_upper(cp));
		}

		return result;
	}

	int normalze_for_compare(int c);

	constexpr int cmp(const std::u8string_view ll, const std::u8string_view rr)
	{
		return ll.compare(rr);
	}

	bool wildcard_icmp(std::u8string_view text, std::u8string_view wildcard);

	constexpr int icmp(const std::u8string_view ll, const std::u8string_view rr)
	{
		if (ll.data() == rr.data() || (ll.empty() && rr.empty())) return 0;
		if (ll.empty()) return 1;
		if (rr.empty()) return -1;

		auto cl = 0;
		auto cr = 0;

		auto il = ll.begin();
		auto ir = rr.begin();
		const auto el = ll.end();
		const auto er = rr.end();

		while (il < el && ir < er)
		{
			cl = to_lower(pop_utf8_char(il, el));
			cr = to_lower(pop_utf8_char(ir, er));
			if (cl < cr) return -1;
			if (cl > cr) return 1;
		}

		if (il == el) cl = 0;
		if (ir == er) cr = 0;
		return cl - cr;
	}

	constexpr int icmp(const std::wstring_view ll, const std::wstring_view rr)
	{
		if (ll.data() == rr.data() || (ll.empty() && rr.empty())) return 0;
		if (ll.empty()) return 1;
		if (rr.empty()) return -1;

		auto cl = 0;
		auto cr = 0;

		auto il = ll.begin();
		auto ir = rr.begin();
		const auto el = ll.end();
		const auto er = rr.end();

		while (il < el && ir < er)
		{
			cl = to_lower(*il++);
			cr = to_lower(*ir++);
			if (cl < cr) return -1;
			if (cl > cr) return 1;
		}

		if (il == el) cl = 0;
		if (ir == er) cr = 0;
		return cl - cr;
	}

	constexpr int icmp(const std::string_view ll, const std::string_view rr)
	{
		if (ll.data() == rr.data() || (ll.empty() && rr.empty())) return 0;
		if (ll.empty()) return 1;
		if (rr.empty()) return -1;

		auto cl = 0;
		auto cr = 0;

		auto il = ll.begin();
		auto ir = rr.begin();
		const auto el = ll.end();
		const auto er = rr.end();

		while (il < el && ir < er)
		{
			cl = to_lower(*il++);
			cr = to_lower(*ir++);
			if (cl < cr) return -1;
			if (cl > cr) return 1;
		}

		if (il == el) cl = 0;
		if (ir == er) cr = 0;
		return cl - cr;
	}

	struct iless
	{
		bool operator()(const std::u8string_view l, const std::u8string_view r) const
		{
			return icmp(l, r) < 0;
		}

		bool operator()(const std::wstring_view l, const std::wstring_view r) const
		{
			return icmp(l, r) < 0;
		}

		bool operator()(const std::string_view l, const std::string_view r) const
		{
			return icmp(l, r) < 0;
		}
	};

	struct less
	{
		bool operator()(const std::u8string_view l, const std::u8string_view r) const
		{
			return l.compare(r) < 0;
		}

		bool operator()(const std::wstring_view l, const std::wstring_view r) const
		{
			return l.compare(r) < 0;
		}

		bool operator()(const std::string_view l, const std::string_view r) const
		{
			return l.compare(r) < 0;
		}
	};


	std::u8string_view month(int m, bool translate);
	std::u8string_view short_month(int m, bool translate);
	int month(std::u8string_view r);

	std::u8string quote_if_white_space(std::u8string_view s);

	inline void remove(std::u8string& s, const std::u8string_view what)
	{
		const auto found = s.find(what);

		if (found != std::u8string::npos)
		{
			const auto begin = s.cbegin() + found;
			s.erase(begin, begin + what.size());
		}
	}

	std::u8string replace(std::u8string_view s, std::u8string_view find, std::u8string_view replacement);
	std::u8string_view strip(std::u8string_view r);
	std::u8string_view trim(std::u8string_view r);
	std::wstring_view trim(std::wstring_view r);
	std::string_view trim(std::string_view r);

	bool is_quote(wchar_t c);
	bool is_num(std::u8string_view sv);
	bool is_probably_num(std::u8string_view sv);
	bool starts(std::u8string_view text, std::u8string_view sub_string);
	bool ends(std::u8string_view text, std::u8string_view sub_string);

	constexpr int last_char(const std::u8string_view sv)
	{
		if (sv.empty()) return 0;
		return sv.back();
	}

	cached cache(std::wstring_view r);
	cached cache(std::u8string_view r);
	cached cache(std::string_view r);
	cached cache(cached r);

	inline cached trim_and_cache(const std::u8string_view r)
	{
		return cache(trim(r));
	}

	inline cached trim_and_cache(const std::wstring_view r)
	{
		return cache(trim(r));
	}

	inline cached trim_and_cache(const std::string_view r)
	{
		return cache(trim(utf8_cast(r)));
	}

	inline cached strip_and_cache(const std::string_view r)
	{
		return cache(strip(utf8_cast(r)));
	}

	inline cached strip_and_cache(const std::u8string_view r)
	{
		return cache(strip(r));
	}

	inline cached strip_and_cache(const std::wstring_view r)
	{
		return cache(trim(utf16_to_utf8(r)));
	}

	inline std::u8string cp_to_utf8(const std::u8string_view sv)
	{
		return std::u8string(sv);
	}


	struct format_arg
	{
		enum type { INT64, UINT64, INT32, UINT32, UINT16, DOUBLE, TEXT, FILE_PATH, FOLDER_PATH, FILE_SIZE, DATE };

		type type;

		union
		{
			int32_t int32_value;
			uint32_t uint32_value;
			int64_t int64_value;
			uint64_t uint64_value;
			uint16_t uint16_value;
			double dbl_value;
		} u;

		const std::u8string_view text_value;
		const std::u8string_view text_value2;

		format_arg(const int32_t value) : type(INT32) { u.int32_value = value; }
		format_arg(const uint32_t value) : type(UINT32) { u.uint32_value = value; }
		format_arg(const long value) : type(INT32) { u.uint32_value = value; }
		format_arg(const uint16_t value) : type(UINT16) { u.uint16_value = value; }
		format_arg(const int64_t value) : type(INT64) { u.int64_value = value; }
		format_arg(const uint64_t value) : type(UINT64) { u.uint64_value = value; }
		format_arg(const double value) : type(DOUBLE) { u.dbl_value = value; }

		format_arg(const std::u8string_view value) : type(TEXT), text_value(value)
		{
			u.int32_value = 0;
		}

		format_arg(const char8_t* value) : type(TEXT), text_value(value)
		{
			u.int32_value = 0;
		}

		format_arg(const std::u8string& value) : type(TEXT), text_value(value)
		{
			u.int32_value = 0;
		}

		format_arg(const cached value) : type(TEXT), text_value(value)
		{
			u.int32_value = 0;
		}

		format_arg(const df::file_size& value) : type(FILE_SIZE) { u.int64_value = value.to_int64(); }
		format_arg(df::file_path value);
		format_arg(df::folder_path value);
		format_arg(df::date_t value);
	};

	std::u8string format_impl(std::u8string_view fmt, const format_arg* args, size_t num_args);

	template <typename... Args>
	std::u8string format(const std::u8string_view fmt, const Args&... args)
	{
		const format_arg arg_array[] = {args...};
		return format_impl(fmt, arg_array, sizeof...(Args));
	}

	std::u8string print(const char8_t* szFormat, ...);
	std::u8string print(const std::u8string_view format, ...);

	constexpr bool is_cr_or_lf(const wchar_t c)
	{
		return c == L'\r' || c == L'\n';
	}

	constexpr bool is_separator(const wchar_t c)
	{
		return c == L';' || c == L',' || c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
	}

	constexpr bool is_artist_separator(const wchar_t c)
	{
		return c == L';' || c == L',' || c == L'\t' || c == L'\r' || c == L'\n' || c == L'\\' || c == L'/';
	}

	constexpr bool is_white_space(const wchar_t c)
	{
		return c == L' ' || c == L'\t';
	}

	constexpr bool is_comma(const wchar_t c)
	{
		return c == L';' || c == L',';
	}

	constexpr bool is_slash(const wchar_t c)
	{
		return c == L'\\' || c == L'/';
	}

	constexpr bool is_colon(const wchar_t c)
	{
		return c == L':';
	}

	constexpr bool is_exclude(const std::u8string_view r)
	{
		auto p = r.begin();
		while (p < r.end() && is_white_space(*p)) p += 1;
		return p < r.end() && *p == '-';
	}

	struct find_result
	{		
		std::vector<part_t> parts;
		bool found = false;
	};

	std::u8string_view::size_type ifind(std::u8string_view text, std::u8string_view sub_string);
	find_result ifind2(std::u8string_view text, std::u8string_view sub_string, size_t parts_offset);

	inline bool contains(const std::u8string_view text, const std::u8string_view pattern)
	{
		return ifind(text, pattern) != std::u8string_view::npos;
	}

	inline bool same(const std::u8string_view text, const std::u8string_view pattern)
	{
		return icmp(text, pattern) == 0;
	}


	inline bool need_quotes(const std::u8string_view s)
	{
		return s.find_first_of(u8": \t\'\""sv) != std::u8string_view::npos;
	}


	inline void join(std::u8string& result, const std::u8string_view s, const std::u8string_view sep = u8" "sv,
	                 const bool quote = true)
	{
		//trim(s);

		if (!s.empty())
		{
			if (!result.empty()) result += sep;

			if (quote && need_quotes(s))
			{
				const bool no_dquote = s.find(L'\"') == std::u8string::npos;
				const bool no_squote = s.find(L'\'') == std::u8string::npos;

				if (no_dquote)
				{
					result += '"';
					result += s;
					result += '"';
				}
				else if (no_squote)
				{
					result += '\'';
					result += s;
					result += '\'';
				}
				else
				{
					result += s;
				}
			}
			else
			{
				result += s;
			}
		}
	}

	template <class T>
	std::u8string combine(const T& strings, const std::u8string_view sep = u8" "sv, const bool quote = true)
	{
		std::u8string result;
		for (const auto& s : strings) str::join(result, s, sep, quote);
		return result;
	}

	inline std::u8string combine2(const std::u8string_view s1, const std::u8string_view s2,
	                              const std::u8string_view sep = u8" "sv)
	{
		std::u8string result;
		result.reserve(s1.size() + s2.size() + sep.size());
		result = s1;
		if (!result.empty() && !str::ends(s1, sep)) result += sep;
		result += s2;
		return result;
	}

	void split2(std::u8string_view text, bool detect_quotes, const std::function<void(std::u8string_view)>& inserter,
	            const std::function<bool(wchar_t)>& pred = is_separator);

	inline std::vector<std::u8string_view> split(const std::u8string_view text, const bool detect_quotes,
	                                             const std::function<bool(wchar_t)>& pred = is_separator)
	{
		std::vector<std::u8string_view> results;
		split2(text, detect_quotes, [&results](const std::u8string_view part) { results.emplace_back(part); }, pred);
		return results;
	}

	inline size_t split_count(const std::u8string_view text, const bool detect_quotes)
	{
		size_t result = 0;
		split2(text, detect_quotes, [&result](const std::u8string_view part) { ++result; });
		return result;
	}

	std::u8string to_string(bool v);
	std::u8string to_string(int v);
	std::u8string to_string(uint32_t v);
	std::u8string to_string(long v);
	std::u8string to_string(int64_t v);
	std::u8string to_string(uint64_t v);
	std::u8string to_string(double v, int num_digits);
	std::u8string to_string(sizei v);

	std::u8string format_count(uint64_t total, bool show_zero = false);

	bool is_utf8(const char8_t* sz, int len);
	bool is_utf16(const uint8_t* sz, int len);


	std::u8string format_seconds(int val);

	int32_t to_int(std::u8string_view r);
	int64_t to_int64(std::u8string_view r);
	uint32_t to_uint(std::u8string_view r);
	double to_double(std::u8string_view r);

	inline std::u8string to_hex(const uint8_t* data, const int data_length, const bool significance_order = true,
	                            const bool remove_leading_zeros_in = false)
	{
		static constexpr char8_t hex_chars[16] = {
			'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
		};
		std::u8string result;
		result.reserve(data_length * 2_z);

		auto remove_leading_zeros = remove_leading_zeros_in;

		for (int i = 0; i < data_length; ++i)
		{
			const auto byte = data[significance_order ? (data_length - (i + 1)) : i];
			const auto c1 = (byte & 0xF0) >> 4;
			const auto c2 = (byte & 0x0F) >> 0;

			if (!remove_leading_zeros || c1) result += hex_chars[c1];
			if (!remove_leading_zeros || c2) result += hex_chars[c2];
			if (c1 || c2) remove_leading_zeros = false;
		}

		return result;
	}

	inline std::u8string to_hex(const uint64_t v, const bool remove_leading_zeros = true)
	{
		return to_hex(std::bit_cast<const uint8_t*>(&v), sizeof(v), true, remove_leading_zeros);
	}

	inline std::u8string to_hex(const uint32_t v, const bool remove_leading_zeros = true)
	{
		return to_hex(std::bit_cast<const uint8_t*>(&v), sizeof(v), true, remove_leading_zeros);
	}

	inline uint32_t hex_char_to_num(const char8_t input)
	{
		if (input >= '0' && input <= '9')
			return input - '0';
		if (input >= 'A' && input <= 'F')
			return input - 'A' + 10;
		if (input >= 'a' && input <= 'f')
			return input - 'a' + 10;
		throw std::invalid_argument("Invalid input string"s);
	}

	inline void hex_to_data(const std::u8string_view src, uint8_t* dst, const size_t dst_len)
	{
		const auto limit = std::min(src.size() / 2, src.size());

		for (auto i = 0u; i < limit; i++)
		{
			*(dst++) = (hex_char_to_num(src[i * 2_z]) << 4) + hex_char_to_num(src[(i * 2_z) + 1_z]);
		}
	}

	inline uint32_t hex_to_num(const std::u8string_view src)
	{
		uint32_t result = 0;

		for (const auto input : src)
		{
			auto n = 0;
			if (input >= '0' && input <= '9') n = input - '0';
			else if (input >= 'A' && input <= 'F') n = input - 'A' + 10;
			else if (input >= 'a' && input <= 'f') n = input - 'a' + 10;
			else break;

			result = (result << 4) | n;
		}

		return result;
	}

	inline std::u8string replace_tokens(const std::u8string_view text,
	                                    const std::function<void(u8ostringstream&, std::u8string_view)>& substitute)
	{
		u8ostringstream result;
		std::u8string_view::size_type offset = 0;

		while (offset != std::u8string_view::npos)
		{
			const auto start_token = text.find_first_of('{', offset);

			if (start_token != std::u8string_view::npos)
			{
				result << text.substr(offset, start_token - offset);

				const auto end_token = text.find_first_of('}', start_token + 1);

				if (end_token != std::u8string_view::npos)
				{
					substitute(result, text.substr(start_token + 1, end_token - start_token - 1));
					offset = end_token + 1;
				}
				else
				{
					result << text.substr(offset);
					offset = std::u8string_view::npos;
				}
			}
			else
			{
				result << text.substr(offset);
				offset = std::u8string_view::npos;
			}
		}

		return result.str();
	}

	inline bool is_wildcard(const std::u8string_view text)
	{
		for (auto i = text.begin(); i < text.end(); ++i)
		{
			const auto c = *i;
			if (c == '*') return true;
			if (c == '\\') ++i;
		}

		return false;
	}
}

inline str::cached operator"" _c(const char8_t* str, const std::size_t len)
{
	return str::cache({str, len});
}
