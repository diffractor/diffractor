// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: String manipulation utilities. Provides UTF-8/UTF-16 conversion,
// string comparison, splitting, formatting, and caching.

#pragma once

class sizei;

namespace str
{
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

	constexpr bool is_empty(const std::string_view sv)
	{
		return sv.empty() || sv[0] == 0;
	}

	constexpr bool is_empty(const char* sz)
	{
		return sz == nullptr || sz[0] == 0;
	}

	inline std::string safe_string(const char* sz)
	{
		return is_empty(sz) ? std::string{} : sz;
	}

	__forceinline constexpr uint32_t pop_utf8_char(std::string_view::const_iterator& in_ptr,
	                                               const std::string_view::const_iterator& end)
	{
		const auto c1 = static_cast<uint8_t>(*in_ptr++);

		if (c1 < 0x80) // 1 octet
		{
			return c1;
		}
		if (c1 >> 5 == 0x6) // 2 octets
		{
			if (std::distance(in_ptr, end) < 1)
			{
				in_ptr = end;
				return 0;
			}

			uint32_t c = (c1 & 0x1F) << 6;
			c |= (static_cast<uint8_t>(*in_ptr++) & 0x3F) << 0;
			return c;
		}
		if (c1 >> 4 == 0xe) // 3 octets
		{
			if (std::distance(in_ptr, end) < 2)
			{
				in_ptr = end;
				return 0;
			}

			uint32_t c = (c1 & 0x0F) << 12;
			c |= (static_cast<uint8_t>(*in_ptr++) & 0x3F) << 6;
			c |= (static_cast<uint8_t>(*in_ptr++) & 0x3F) << 0;
			return c;
		}
		if (c1 >> 3 == 0x1e) // 4 octets
		{
			if (std::distance(in_ptr, end) < 3)
			{
				in_ptr = end;
				return 0;
			}

			uint32_t c = (c1 & 0x0F) << 18;
			c |= (static_cast<uint8_t>(*in_ptr++) & 0x3F) << 12;
			c |= (static_cast<uint8_t>(*in_ptr++) & 0x3F) << 6;
			c |= (static_cast<uint8_t>(*in_ptr++) & 0x3F) << 0;
			return c;
		}

		return '?';
	}

	__forceinline constexpr uint32_t peek_utf8_char(std::string_view::const_iterator in_ptr,
	                                                const std::string_view::const_iterator& end)
	{
		return pop_utf8_char(in_ptr, end);
	}

	// Byte range inside one displayable string (a name, a path, a search term), so 32 bits is far
	// more than the longest text any of these can hold. The size_t constructor narrows on purpose:
	// callers measure with string_view::size_type and every producer is bounded well below 4GB.
	struct part_t
	{
		uint32_t offset = 0;
		uint32_t length = 0;

		part_t() noexcept = default;

		constexpr part_t(const size_t o, const size_t l) noexcept
			: offset(static_cast<uint32_t>(o)), length(static_cast<uint32_t>(l))
		{
		}
	};

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// String Storage Structure for Interning
	////////////////////////////////////////////////////////////////////////////////////////////////////
	// Stores the length and UTF-8 data for an interned string. Uses the C "flexible array member"
	// pattern - actual allocation size is offsetof(chached_string_storage_t, sz) + len + 1 bytes.
	// Once allocated, the storage is immutable and persists for the application lifetime.
	////////////////////////////////////////////////////////////////////////////////////////////////////
	struct chached_string_storage_t
	{
		uint32_t len;

		// Case-insensitive hash, computed once here. Paths are interned once and then used as hash keys
		// for the life of the process, so folding UTF-8 to lower case over the whole path on every probe
		// was paying an O(length) cost to answer a question whose input never changes.
		uint32_t ihash;

		char sz[1];
	};

	namespace detail
	{
		// Records are 4-byte aligned, so a handle counts 4-byte units and reaches 16GB of pool.
		constexpr uint32_t intern_align_shift = 2;

		// FNV-1a offset basis, which is what fnv1a_i answers for an empty string. string_index_t
		// asserts the two agree rather than reaching into crypto from here.
		constexpr uint32_t empty_ihash = 2166136261u;

		// Resolves handle 0 before the pool exists. The pool reserves its own slot 0 so the table
		// never hands out a zero handle for real content.
		inline constexpr chached_string_storage_t empty_storage{0, empty_ihash, {0}};

		// Base of the single contiguous reservation holding every interned record. Assigned once,
		// before any non-zero handle can exist, so a reader that holds a handle already
		// synchronized-with the store; the relaxed load compiles to a plain move.
		extern std::atomic<const chached_string_storage_t*> intern_pool_base;

		inline const chached_string_storage_t* resolve(const uint32_t id) noexcept
		{
			const auto* const base = std::bit_cast<const uint8_t*>(intern_pool_base.load(std::memory_order_relaxed));
			return std::bit_cast<const chached_string_storage_t*>(base + (static_cast<size_t>(id) <<
				intern_align_shift));
		}
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// Interned String Handle (str::cached)
	////////////////////////////////////////////////////////////////////////////////////////////////////
	// A lightweight handle to an interned string stored in the global string pool. This type enables
	// efficient string storage and comparison throughout Diffractor's indexing engine.
	//
	// BENEFITS:
	// - Memory efficiency: Each unique string is stored exactly once, regardless of how many
	//   index_file_item or metadata objects reference it
	// - Fast equality: Handle comparison (O(1)) instead of character comparison (O(n))
	// - Thread-safe reads: The record is immutable once created
	// - Cache-friendly: Strings are allocated from a contiguous memory pool
	//
	// USAGE:
	//   str::cached name = str::cache("example.jpg");  // Intern a string
	//   str::cached trimmed = str::trim_and_cache(value); // Trim and intern
	//   if (a == b) { ... }  // O(1) handle comparison
	//   process(name.sv());  // Convert to string_view for APIs
	//
	// THREAD SAFETY:
	// - Creation via str::cache() is thread-safe (uses sharded hash map with locks)
	// - Reading from a cached instance is thread-safe (the record is immutable)
	//
	// LIFETIME:
	// - Interned strings are never deallocated - they persist for app lifetime
	// - This is acceptable because total unique strings are bounded by collection size
	// - The memory pool provides better allocation density than heap allocation
	//
	// SIZE: 4 bytes - an offset in 4-byte units from the pool's fixed base. Handle 0 is the empty
	// string and always resolves to a zero-length record, so no accessor needs a null branch.
	// COPYABLE: Trivially copyable - safe to pass by value or store in containers
	////////////////////////////////////////////////////////////////////////////////////////////////////
	struct cached
	{
		uint32_t id = 0;

		cached() noexcept = default;

		explicit cached(const uint32_t i) noexcept : id(i)
		{
		}

		cached(const cached&) noexcept = default;
		cached& operator=(const cached&) noexcept = default;
		cached(cached&&) noexcept = default;
		cached& operator=(cached&&) noexcept = default;

		void clear()
		{
			id = 0;
		}

		constexpr bool is_empty() const
		{
			return id == 0;
		}

		size_t size() const
		{
			return detail::resolve(id)->len;
		}

		// Case-insensitive, so two spellings of one path agree. Equal to crypto::fnv1a_i(sv()).
		uint32_t ihash() const
		{
			return detail::resolve(id)->ihash;
		}

		std::string_view substr(const size_t sub_pos) const
		{
			const auto* const s = detail::resolve(id);
			df::assert_true(sub_pos <= s->len);
			if (sub_pos >= s->len) return {};
			return {s->sz + sub_pos, s->len - sub_pos};
		}

		std::string_view substr(const size_t sub_pos, const size_t sub_len) const
		{
			const auto* const s = detail::resolve(id);
			df::assert_true(sub_pos <= s->len && sub_len <= s->len - sub_pos);
			if (sub_pos >= s->len || sub_pos + sub_len > s->len) return {};
			return {s->sz + sub_pos, sub_len};
		}

		char operator[](const uint32_t i) const noexcept
		{
			const auto* const s = detail::resolve(id);
			df::assert_true(s->len > i);
			if (s->len <= i) return 0;
			return s->sz[i];
		}

		operator std::string_view() const
		{
			return sv();
		}

		std::string_view sv() const
		{
			const auto* const s = detail::resolve(id);
			return {s->sz, s->len};
		}

		std::string str() const
		{
			const auto* const s = detail::resolve(id);
			return {s->sz, s->len};
		}

		bool operator==(const cached other) const
		{
			return id == other.id;
		}

		bool operator !=(const cached other) const
		{
			return id != other.id;
		}

		bool operator==(const std::string_view other) const
		{
			return other == sv();
		}

		bool operator !=(const std::string_view other) const
		{
			return other != sv();
		}

		bool operator <(const cached other) const
		{
			return id < other.id;
		}

		const char* sz() const
		{
			return detail::resolve(id)->sz;
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
			*inserter++ = static_cast<uint8_t>(ch);
		}
		else if (ch < 0x800)
		{
			*inserter++ = static_cast<uint8_t>(0xC0 | ch >> 6);
			*inserter++ = static_cast<uint8_t>(0x80 | ch >> 0 & 0x3F);
		}
		else if (ch < 0x10000)
		{
			*inserter++ = static_cast<uint8_t>(0xE0 | ch >> 12);
			*inserter++ = static_cast<uint8_t>(0x80 | ch >> 6 & 0x3F);
			*inserter++ = static_cast<uint8_t>(0x80 | ch >> 0 & 0x3F);
		}
		else
		{
			*inserter++ = static_cast<uint8_t>(ch >> 18 | 0xf0);
			*inserter++ = static_cast<uint8_t>(ch >> 12 & 0x3f | 0x80);
			*inserter++ = static_cast<uint8_t>(ch >> 6 & 0x3f | 0x80);
			*inserter++ = static_cast<uint8_t>(ch & 0x3f | 0x80);
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
		return cp >= LEAD_SURROGATE_MIN && cp <= LEAD_SURROGATE_MAX;
	}

	inline bool is_trail_surrogate(const uint32_t cp)
	{
		return cp >= TRAIL_SURROGATE_MIN && cp <= TRAIL_SURROGATE_MAX;
	}

	inline uint16_t mask16(const uint32_t oc)
	{
		return static_cast<uint16_t>(0xffff & oc);
	}

	inline std::string utf16_to_utf8(const std::wstring_view s)
	{
		std::string result;
		result.reserve(s.size() * 3);
		auto inserter = std::back_inserter(result);

		auto start = s.begin();
		const auto end = s.end();

		while (start != end)
		{
			uint32_t cp = mask16(*start++);

			if (is_lead_surrogate(cp))
			{
				if (start != end && is_trail_surrogate(mask16(*start)))
				{
					cp = (cp << 10) + mask16(*start++) + SURROGATE_OFFSET;
				}
				else
				{
					cp = 0xFFFD; // replacement character for lone lead surrogate
				}
			}
			else if (is_trail_surrogate(cp))
			{
				cp = 0xFFFD; // replacement character for lone trail surrogate
			}

			char32_to_utf8(inserter, cp);
		}

		return result;
	}

	// For UTF-16 that arrives as bytes rather than as a platform string: char16_t is two bytes
	// everywhere, where wchar_t is two on Windows and four elsewhere.
	inline std::string utf16_to_utf8(const std::u16string_view s)
	{
		std::string result;
		result.reserve(s.size() * 3);
		auto inserter = std::back_inserter(result);

		auto start = s.begin();
		const auto end = s.end();

		while (start != end)
		{
			uint32_t cp = mask16(*start++);

			if (is_lead_surrogate(cp))
			{
				if (start != end && is_trail_surrogate(mask16(*start)))
				{
					cp = (cp << 10) + mask16(*start++) + SURROGATE_OFFSET;
				}
				else
				{
					cp = 0xFFFD;
				}
			}
			else if (is_trail_surrogate(cp))
			{
				cp = 0xFFFD;
			}

			char32_to_utf8(inserter, cp);
		}

		return result;
	}

	inline std::wstring utf8_to_utf16(const std::string_view s)
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

	std::string utf8_to_a(std::string_view utf8);

	inline std::string_view utf8_cast(const std::string_view val)
	{
		return {std::bit_cast<const char*>(val.data()), val.size()};
	}

	inline std::string utf8_cast2(const std::string_view val)
	{
		return {val.begin(), val.end()};
	}

	inline _locale_t utf8_locale()
	{
		static const _locale_t loc = _create_locale(LC_CTYPE, ".UTF-8");
		return loc;
	}

	inline int to_lower(const int c)
	{
		if (c < 128) return c >= L'A' && c <= L'Z' ? c - L'A' + L'a' : c;
		if (c > USHRT_MAX) return c;
		return _towlower_l(c, utf8_locale());
	}

	inline int to_upper(const int c)
	{
		if (c < 128) return c >= L'a' && c <= L'z' ? c - L'a' + L'A' : c;
		if (c > USHRT_MAX) return c;
		return _towupper_l(c, utf8_locale());
	}

	inline std::string to_lower(const std::string_view s)
	{
		std::string result;
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

	inline std::string to_upper(const std::string_view s)
	{
		std::string result;
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

	constexpr int cmp(const std::string_view ll, const std::string_view rr)
	{
		return ll.compare(rr);
	}

	bool wildcard_icmp(std::string_view text, std::string_view wildcard);

	inline int icmp(const std::string_view ll, const std::string_view rr)
	{
		if (ll.data() == rr.data() && ll.size() == rr.size()) return 0;
		if (ll.empty() && rr.empty()) return 0;
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

	inline int icmp(const std::wstring_view ll, const std::wstring_view rr)
	{
		if (ll.data() == rr.data() && ll.size() == rr.size()) return 0;
		if (ll.empty() && rr.empty()) return 0;
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


	// Natural sort comparison (alphanum sort) - compares embedded numeric sequences as integers.
	// This ensures "file10" sorts after "file9" rather than between "file1" and "file2".
	// Case-insensitive and UTF-8 aware.
	inline int icmp_natural(const std::string_view ll, const std::string_view rr)
	{
		if (ll.data() == rr.data() && ll.size() == rr.size()) return 0;
		if (ll.empty() && rr.empty()) return 0;
		if (ll.empty()) return 1;
		if (rr.empty()) return -1;

		auto il = ll.begin();
		auto ir = rr.begin();
		const auto el = ll.end();
		const auto er = rr.end();

		while (il < el && ir < er)
		{
			auto cl = peek_utf8_char(il, el);
			auto cr = peek_utf8_char(ir, er);

			// Check if both characters are digits
			const bool l_is_digit = cl >= '0' && cl <= '9';
			const bool r_is_digit = cr >= '0' && cr <= '9';

			if (l_is_digit && r_is_digit)
			{
				// Skip leading zeros and count them
				int l_leading_zeros = 0;
				int r_leading_zeros = 0;

				while (il < el && *il == '0')
				{
					++il;
					++l_leading_zeros;
				}
				while (ir < er && *ir == '0')
				{
					++ir;
					++r_leading_zeros;
				}

				// Extract numeric values
				uint64_t l_num = 0;
				uint64_t r_num = 0;
				int l_digits = 0;
				int r_digits = 0;

				while (il < el && *il >= '0' && *il <= '9')
				{
					l_num = l_num * 10 + (*il - '0');
					++il;
					++l_digits;
				}
				while (ir < er && *ir >= '0' && *ir <= '9')
				{
					r_num = r_num * 10 + (*ir - '0');
					++ir;
					++r_digits;
				}

				// Compare numeric values
				if (l_num < r_num) return -1;
				if (l_num > r_num) return 1;

				// If equal, fewer leading zeros comes first (preserves original behavior for "007" vs "7")
				if (l_leading_zeros < r_leading_zeros) return -1;
				if (l_leading_zeros > r_leading_zeros) return 1;
			}
			else
			{
				// Compare as characters (case-insensitive)
				cl = to_lower(pop_utf8_char(il, el));
				cr = to_lower(pop_utf8_char(ir, er));

				if (cl < cr) return -1;
				if (cl > cr) return 1;
			}
		}

		// Handle remaining characters
		if (il < el) return 1; // left string is longer
		if (ir < er) return -1; // right string is longer
		return 0;
	}

	struct iless
	{
		bool operator()(const std::string_view l, const std::string_view r) const
		{
			return icmp(l, r) < 0;
		}

		bool operator()(const std::wstring_view l, const std::wstring_view r) const
		{
			return icmp(l, r) < 0;
		}
	};

	struct less
	{
		bool operator()(const std::string_view l, const std::string_view r) const
		{
			return l.compare(r) < 0;
		}

		bool operator()(const std::wstring_view l, const std::wstring_view r) const
		{
			return l.compare(r) < 0;
		}
	};


	std::string_view month(int m, bool translate);
	std::string_view short_month(int m, bool translate);
	int month(std::string_view r);

	std::string quote_if_white_space(std::string_view s);

	inline void remove(std::string& s, const std::string_view what)
	{
		const auto found = s.find(what);

		if (found != std::string::npos)
		{
			const auto begin = s.cbegin() + found;
			s.erase(begin, begin + what.size());
		}
	}

	std::string replace(std::string_view s, std::string_view find, std::string_view replacement);
	std::string_view strip(std::string_view r);
	std::string_view trim(std::string_view r);
	std::wstring_view trim(std::wstring_view r);

	bool is_quote(char c);
	bool is_num(std::string_view sv);
	bool is_probably_num(std::string_view sv);
	bool starts(std::string_view text, std::string_view sub_string);
	bool ends(std::string_view text, std::string_view sub_string);

	constexpr char last_char(const std::string_view sv)
	{
		if (sv.empty()) return 0;
		return sv.back();
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////
	// String Interning Functions
	////////////////////////////////////////////////////////////////////////////////////////////////////
	// These functions intern strings into the global string pool. If the string already exists,
	// the existing interned reference is returned. Thread-safe for concurrent calls.
	//
	// The global string pool uses:
	// - an append-only sharded open-addressing table (no deletes, so no tombstones)
	// - CRC32C hash for shard selection and probing
	// - platform::memory_pool for one contiguous, never-moving reservation
	//
	// Note: Strings whose complete storage record exceeds a memory-pool block return empty cached.
	////////////////////////////////////////////////////////////////////////////////////////////////////
	cached cache(std::wstring_view r); // Converts UTF-16 to UTF-8, then interns
	cached cache(std::string_view r); // Interns UTF-8 string directly

	// Already interned - return unchanged rather than round-tripping through the table.
	inline cached cache(const cached r)
	{
		return r;
	}

	inline cached trim_and_cache(const std::string_view r)
	{
		return cache(trim(r));
	}

	inline cached trim_and_cache(const std::wstring_view r)
	{
		return cache(trim(r));
	}

	inline cached strip_and_cache(const std::string_view r)
	{
		return cache(strip(utf8_cast(r)));
	}

	inline cached strip_and_cache(const std::wstring_view r)
	{
		return cache(trim(utf16_to_utf8(r)));
	}

	inline cached strip_and_cache(const std::u16string_view r)
	{
		return cache(trim(utf16_to_utf8(r)));
	}

	std::string print(const char* szFormat, ...);
	std::string print(std::string_view format, ...);

	// These predicates are applied to the bytes of a UTF-8 string_view, so the unit is char. A
	// continuation byte is negative and matches nothing here, which is what separating on ASCII wants.
	constexpr bool is_separator(const char c)
	{
		return c == ';' || c == ',' || c == ' ' || c == '\t' || c == '\r' || c == '\n';
	}

	constexpr bool is_artist_separator(const char c)
	{
		return c == ';' || c == ',' || c == '\t' || c == '\r' || c == '\n' || c == '\\' || c == '/';
	}

	// Genre names may contain spaces, '&' and '/' (e.g. "Action & Adventure", "R&B/Soul"),
	// so only ';' (plus control whitespace) separates multiple genre values.
	constexpr bool is_genre_separator(const char c)
	{
		return c == ';' || c == '\t' || c == '\r' || c == '\n';
	}

	constexpr bool is_white_space(const char c)
	{
		return c == ' ' || c == '\t';
	}

	constexpr bool is_slash(const char c)
	{
		return c == '\\' || c == '/';
	}

	constexpr bool is_exclude(const std::string_view r)
	{
		auto p = r.begin();
		while (p < r.end() && is_white_space(*p)) p += 1;
		return p < r.end() && *p == '-';
	}

	// True when every byte is 7-bit ASCII (< 0x80). ASCII text is always already in
	// Unicode NFC form, so callers can skip normalization on the fast path.
	constexpr bool is_ascii(const std::string_view r)
	{
		for (const auto c : r)
		{
			if (static_cast<unsigned char>(c) >= 0x80) return false;
		}
		return true;
	}

	struct find_result
	{
		std::vector<part_t> parts;
		bool found = false;
	};

	std::string_view::size_type ifind(std::string_view text, std::string_view sub_string);
	find_result ifind2(std::string_view text, std::string_view sub_string, size_t parts_offset);

	inline bool contains(const std::string_view text, const std::string_view pattern)
	{
		return ifind(text, pattern) != std::string_view::npos;
	}

	inline bool same(const std::string_view text, const std::string_view pattern)
	{
		return icmp(text, pattern) == 0;
	}


	inline bool need_quotes(const std::string_view s)
	{
		return s.find_first_of(": \t\'\"") != std::string_view::npos;
	}


	inline void join(std::string& result, const std::string_view s, const std::string_view sep = " ",
	                 const bool quote = true)
	{
		if (!s.empty())
		{
			if (!result.empty()) result += sep;

			if (quote && need_quotes(s))
			{
				const bool no_dquote = s.find(L'\"') == std::string::npos;
				const bool no_squote = s.find(L'\'') == std::string::npos;

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
	std::string combine(const T& strings, const std::string_view sep = " ", const bool quote = true)
	{
		std::string result;
		for (const auto& s : strings) str::join(result, s, sep, quote);
		return result;
	}

	inline std::string combine2(const std::string_view s1, const std::string_view s2,
	                            const std::string_view sep = " ")
	{
		std::string result;
		result.reserve(s1.size() + s2.size() + sep.size());
		result = s1;
		if (!result.empty() && !str::ends(s1, sep)) result += sep;
		result += s2;
		return result;
	}

	void split2(std::string_view text, bool detect_quotes, const std::function<void(std::string_view)>& inserter,
	            const std::function<bool(char)>& pred = is_separator);

	inline std::vector<std::string_view> split(const std::string_view text, const bool detect_quotes,
	                                           const std::function<bool(char)>& pred = is_separator)
	{
		std::vector<std::string_view> results;
		split2(text, detect_quotes, [&results](const std::string_view part) { results.emplace_back(part); }, pred);
		return results;
	}

	inline size_t split_count(const std::string_view text, const bool detect_quotes)
	{
		size_t result = 0;
		split2(text, detect_quotes, [&result](const std::string_view part) { ++result; });
		return result;
	}

	std::string to_string(bool v);
	std::string to_string(int v);
	std::string to_string(uint32_t v);
#if !DF_LONG_IS_INT64
	std::string to_string(long v);
#endif
	std::string to_string(int64_t v);
	std::string to_string(uint64_t v);
	// The mirror of the `long` case above. Under LP64 uint64_t is `unsigned long`, so `unsigned long
	// long` is a distinct type with no overload; under LLP64 the two are the same and this would
	// redeclare one.
#if DF_LONG_IS_INT64
	std::string to_string(unsigned long long v);
#endif
	std::string to_string(double v, int num_digits);
	std::string to_string(sizei v);

	std::string format_count(uint64_t total, bool show_zero = false);

	bool is_utf8(const char* sz, int len);
	bool is_utf16(const uint8_t* sz, int len);


	std::string format_seconds(int val);

	int32_t to_int(std::string_view r);
	int64_t to_int64(std::string_view r);
	uint32_t to_uint(std::string_view r);
	double to_double(std::string_view r);

	inline std::string to_hex(const uint8_t* data, const int data_length, const bool significance_order = true,
	                          const bool remove_leading_zeros_in = false)
	{
		static constexpr char hex_chars[16] = {
			'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
		};
		std::string result;
		result.reserve(data_length * 2_z);

		auto remove_leading_zeros = remove_leading_zeros_in;

		for (int i = 0; i < data_length; ++i)
		{
			const auto byte = data[significance_order ? data_length - (i + 1) : i];
			const auto c1 = (byte & 0xF0) >> 4;
			const auto c2 = (byte & 0x0F) >> 0;

			if (!remove_leading_zeros || c1) result += hex_chars[c1];
			if (!remove_leading_zeros || c2) result += hex_chars[c2];
			if (c1 || c2) remove_leading_zeros = false;
		}

		return result;
	}

	// Payloads are listed up to this length; beyond it the dump stops rather than filling the pane.
	inline constexpr size_t max_hex_dump_bytes = 4096;

	inline std::string to_hex(const uint64_t v, const bool remove_leading_zeros = true)
	{
		return to_hex(std::bit_cast<const uint8_t*>(&v), sizeof(v), true, remove_leading_zeros);
	}

	inline std::string to_hex(const uint32_t v, const bool remove_leading_zeros = true)
	{
		return to_hex(std::bit_cast<const uint8_t*>(&v), sizeof(v), true, remove_leading_zeros);
	}

	inline uint32_t hex_to_num(const std::string_view src)
	{
		uint32_t result = 0;

		for (const auto input : src)
		{
			auto n = 0;
			if (input >= '0' && input <= '9') n = input - '0';
			else if (input >= 'A' && input <= 'F') n = input - 'A' + 10;
			else if (input >= 'a' && input <= 'f') n = input - 'a' + 10;
			else break;

			result = result << 4 | n;
		}

		return result;
	}

	inline std::string replace_tokens(const std::string_view text,
	                                  const std::function<void(std::ostringstream&, std::string_view)>& substitute)
	{
		std::ostringstream result;
		std::string_view::size_type offset = 0;

		while (offset != std::string_view::npos)
		{
			const auto start_token = text.find_first_of('{', offset);

			if (start_token != std::string_view::npos)
			{
				result << text.substr(offset, start_token - offset);

				const auto end_token = text.find_first_of('}', start_token + 1);

				if (end_token != std::string_view::npos)
				{
					substitute(result, text.substr(start_token + 1, end_token - start_token - 1));
					offset = end_token + 1;
				}
				else
				{
					// unterminated token - emit the rest verbatim
					result << text.substr(start_token);
					offset = std::string_view::npos;
				}
			}
			else
			{
				result << text.substr(offset);
				offset = std::string_view::npos;
			}
		}

		return result.str();
	}

	inline bool is_wildcard(const std::string_view text)
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

inline str::cached operator"" _c(const char* str, const std::size_t len)
{
	return str::cache({str, len});
}

template <>
struct std::formatter<str::cached, char> : std::formatter<std::string_view, char>
{
	auto format(const str::cached& s, std::format_context& ctx) const
	{
		return std::formatter<std::string_view, char>::format(s.sv(), ctx);
	}
};
