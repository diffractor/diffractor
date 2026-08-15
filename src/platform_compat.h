// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Compiler and C runtime dialect differences between MSVC and GCC/Clang, so that
// portable code can keep using one spelling. This declares no operating system facilities:
// those belong to platform.h and its platform_* implementations.

#pragma once

#include <cstdint>
#include <climits>

// `long` is distinct from both int32_t and int64_t only under LLP64, where it is 32-bit beside a
// 64-bit long long. Under LP64 `long` *is* int64_t, so a separate `long` overload redeclares an
// existing one. Overloads that exist only to catch `long` are conditioned on this.
#if LONG_MAX == INT64_MAX
#define DF_LONG_IS_INT64 1
#else
#define DF_LONG_IS_INT64 0
#endif

#ifdef _MSC_VER

#include <sal.h>

#else

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cerrno>
#include <clocale>
#include <ctime>
#include <cwctype>
#include <locale.h>
#include <malloc.h>
#include <string.h>
#include <strings.h>
#include <string>
#include <tuple>
#include <type_traits>

// SAL annotations document ownership and locking, and MSVC's analyser acts on them. They have no
// GCC or Clang equivalent, so they compile away rather than being deleted from the source.
#define _Guarded_by_(lock)
#define _Acquires_exclusive_lock_(lock)
#define _Releases_exclusive_lock_(lock)
#define _Acquires_shared_lock_(lock)
#define _Releases_shared_lock_(lock)
#define __in_z
#define __format_string

#define __forceinline inline __attribute__((always_inline))

namespace df::compat
{
	// MSVC's _itoa_s family, sharing one radix conversion. Radix 2 to 36, as the originals accept.
	template <typename UnsignedT>
	inline int to_chars_radix(const UnsignedT magnitude, const bool negative, char* buffer, const size_t size,
	                          const int radix)
	{
		if (buffer == nullptr || size == 0 || radix < 2 || radix > 36) return EINVAL;

		char digits[8 * sizeof(UnsignedT) + 1];
		size_t count = 0;
		auto value = magnitude;

		do
		{
			const auto digit = static_cast<unsigned>(value % static_cast<UnsignedT>(radix));
			digits[count++] = static_cast<char>(digit < 10 ? '0' + digit : 'a' + digit - 10);
			value /= static_cast<UnsignedT>(radix);
		}
		while (value != 0);

		const size_t needed = count + (negative ? 1u : 0u) + 1u;

		if (needed > size)
		{
			buffer[0] = 0;
			return ERANGE;
		}

		char* out = buffer;
		if (negative) *out++ = '-';
		while (count > 0) *out++ = digits[--count];
		*out = 0;
		return 0;
	}
}

inline int _itoa_s(const int value, char* buffer, const size_t size, const int radix)
{
	const bool negative = value < 0 && radix == 10;
	const auto magnitude = negative
		                       ? 0u - static_cast<unsigned int>(value)
		                       : static_cast<unsigned int>(value);
	return df::compat::to_chars_radix(magnitude, negative, buffer, size, radix);
}

inline int _ltoa_s(const long value, char* buffer, const size_t size, const int radix)
{
	const bool negative = value < 0 && radix == 10;
	const auto magnitude = negative
		                       ? 0ul - static_cast<unsigned long>(value)
		                       : static_cast<unsigned long>(value);
	return df::compat::to_chars_radix(magnitude, negative, buffer, size, radix);
}

inline int _ultoa_s(const unsigned long value, char* buffer, const size_t size, const int radix)
{
	return df::compat::to_chars_radix(value, false, buffer, size, radix);
}

inline int _i64toa_s(const int64_t value, char* buffer, const size_t size, const int radix)
{
	const bool negative = value < 0 && radix == 10;
	const auto magnitude = negative
		                       ? 0ull - static_cast<uint64_t>(value)
		                       : static_cast<uint64_t>(value);
	return df::compat::to_chars_radix(magnitude, negative, buffer, size, radix);
}

inline int _ui64toa_s(const uint64_t value, char* buffer, const size_t size, const int radix)
{
	return df::compat::to_chars_radix(value, false, buffer, size, radix);
}

inline int memcpy_s(void* dest, const size_t dest_size, const void* src, const size_t count)
{
	if (count == 0) return 0;
	if (dest == nullptr) return EINVAL;

	if (src == nullptr || dest_size < count)
	{
		std::memset(dest, 0, dest_size);
		return src == nullptr ? EINVAL : ERANGE;
	}

	std::memcpy(dest, src, count);
	return 0;
}

inline int memmove_s(void* dest, const size_t dest_size, const void* src, const size_t count)
{
	if (count == 0) return 0;
	if (dest == nullptr || src == nullptr) return EINVAL;
	if (dest_size < count) return ERANGE;

	std::memmove(dest, src, count);
	return 0;
}

inline int fopen_s(FILE** stream, const char* filename, const char* mode)
{
	if (stream == nullptr) return EINVAL;
	*stream = std::fopen(filename, mode);
	return *stream == nullptr ? errno : 0;
}

namespace compat_detail
{
	// Every value scanf actually assigns through is a pointer, so a by-value argument in the pack can
	// only be one of MSVC's buffer sizes. Dropping them is what makes the call match glibc's sscanf.
	inline std::tuple<> keep_scanf_args() { return {}; }

	template <typename FirstT, typename... RestT>
	auto keep_scanf_args(FirstT first, RestT... rest)
	{
		if constexpr (std::is_pointer_v<FirstT>)
			return std::tuple_cat(std::tuple<FirstT>(first), keep_scanf_args(rest...));
		else
			return keep_scanf_args(rest...);
	}

	// Those sizes were the only bound on %s and %[, which run until a delimiter. Once they are gone
	// the format's field width has to supply the bound, so a format without one is refused.
	inline bool scanf_conversions_bounded(const char* format)
	{
		for (auto p = format; *p != 0; ++p)
		{
			if (*p != '%') continue;

			++p;
			if (*p == 0 || *p == '%' || *p == '*') continue;

			const auto digits = p;
			while (*p >= '0' && *p <= '9') ++p;

			if ((*p == 's' || *p == '[') && p == digits) return false;
		}

		return true;
	}
}

// sscanf bounded to the first `count` characters, accepting MSVC's buffer size after every %s, %c
// and %[ conversion.
template <typename... ArgsT>
int _snscanf_s(const char* buffer, const size_t count, const char* format, ArgsT... args)
{
	if (!compat_detail::scanf_conversions_bounded(format)) return EOF;

	const std::string bounded(buffer, ::strnlen(buffer, count));
	return std::apply([&](auto... kept) { return std::sscanf(bounded.c_str(), format, kept...); },
	                  compat_detail::keep_scanf_args(args...));
}

inline int _vscprintf(const char* format, va_list args)
{
	va_list copy;
	va_copy(copy, args);
	const int result = std::vsnprintf(nullptr, 0, format, copy);
	va_end(copy);
	return result;
}

inline int vsprintf_s(char* buffer, const size_t size, const char* format, va_list args)
{
	return std::vsnprintf(buffer, size, format, args);
}

template <size_t N, typename... ArgsT>
int sprintf_s(char (&buffer)[N], const char* format, ArgsT... args)
{
	return std::snprintf(buffer, N, format, args...);
}

// MSVC returns 0 on success and writes the digit string without a decimal point, reporting the
// point position and sign separately; glibc's fcvt_r has the same contract with a different order.
inline int _fcvt_s(char* buffer, const size_t size, const double value, const int digit_count, int* decimal_point,
                   int* sign)
{
	return fcvt_r(value, digit_count, decimal_point, sign, buffer, size);
}

inline void* _aligned_malloc(const size_t size, const size_t alignment)
{
	void* result = nullptr;
	// posix_memalign requires a power-of-two multiple of sizeof(void*); _aligned_malloc does not.
	const size_t required = alignment < sizeof(void*) ? sizeof(void*) : alignment;
	if (posix_memalign(&result, required, size) != 0) return nullptr;
	return result;
}

inline void _aligned_free(void* p)
{
	std::free(p);
}

// POSIX has no aligned realloc, and malloc_usable_size is the only way to learn how much of the
// old block is safe to copy. It reports at least the requested size for a posix_memalign block.
inline void* _aligned_realloc(void* p, const size_t size, const size_t alignment)
{
	if (p == nullptr) return _aligned_malloc(size, alignment);

	if (size == 0)
	{
		std::free(p);
		return nullptr;
	}

	void* result = _aligned_malloc(size, alignment);
	if (result == nullptr) return nullptr;

	const size_t old_size = malloc_usable_size(p);
	std::memcpy(result, p, old_size < size ? old_size : size);
	std::free(p);
	return result;
}

inline uint32_t _byteswap_ulong(const uint32_t n)
{
	return __builtin_bswap32(n);
}

inline uint16_t _byteswap_ushort(const uint16_t n)
{
	return __builtin_bswap16(n);
}

using _locale_t = locale_t;

// MSVC spells the UTF-8 C locale ".UTF-8"; the POSIX name for the same thing is "C.UTF-8". The
// category argument is ignored because every caller asks for LC_CTYPE.
inline _locale_t _create_locale(int, const char*)
{
	return newlocale(LC_CTYPE_MASK, "C.UTF-8", static_cast<locale_t>(0));
}

inline int _towlower_l(const int c, const _locale_t loc)
{
	return static_cast<int>(towlower_l(static_cast<wint_t>(c), loc));
}

inline int _towupper_l(const int c, const _locale_t loc)
{
	return static_cast<int>(towupper_l(static_cast<wint_t>(c), loc));
}

inline int _strnicmp(const char* a, const char* b, const size_t count)
{
	return strncasecmp(a, b, count);
}

inline int _stricmp(const char* a, const char* b)
{
	return strcasecmp(a, b);
}

// MSVC takes the destination first and returns an errno_t; POSIX takes it last and returns a
// pointer. Only the argument order actually differs, so the failure is mapped rather than invented.
inline int localtime_s(struct tm* result, const time_t* time)
{
	return localtime_r(time, result) == nullptr ? EINVAL : 0;
}

inline int gmtime_s(struct tm* result, const time_t* time)
{
	return gmtime_r(time, result) == nullptr ? EINVAL : 0;
}

#endif // _MSC_VER
