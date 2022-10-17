// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

#include <intrin.h>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#define COMPILE_SIMD_INTRINSIC
#endif

#if defined(_MSC_VER) && defined(_M_ARM64)
#define COMPILE_ARM_INTRINSIC
#endif


#if defined(_MSC_VER)
#  if defined(_WIN64)
typedef __int64 ssize_t;
#  else
typedef long ssize_t;
#  endif
#endif

using u8ostringstream = std::basic_ostringstream<char8_t, std::char_traits<char8_t>, std::allocator<char8_t>>;
using u8istringstream = std::basic_istringstream<char8_t, std::char_traits<char8_t>, std::allocator<char8_t>>;

using u8istream = std::basic_ifstream<char8_t, std::char_traits<char8_t>>;
using u8ostream = std::basic_ofstream<char8_t, std::char_traits<char8_t>>;

constexpr std::size_t operator "" _z(unsigned long long n)
{
	return n;
}

class app_exception : public std::exception
{
public:
	using my_base = exception;

	explicit app_exception(const std::string& message) : my_base(message.c_str())
	{
	}

	explicit app_exception(const std::u8string& message) : my_base(std::bit_cast<const char*>(message.c_str()))
	{
	}

	explicit app_exception(const char* message) : my_base(message)
	{
	}
};

namespace df
{
	constexpr uint32_t sixty_four_k = 1024 * 64;
	constexpr uint32_t two_fifty_six_k = 1024 * 256;
	constexpr uint32_t one_meg = 1024 * 1024;
	constexpr uint32_t one_mega_pixel = 1024 * 1024;
	constexpr double golden_ratio = 1.61803399;
	constexpr uint64_t max_file_load_size = 1024ull * 1024ull * 16ull;
	constexpr uint32_t max_thumbnails_to_display = 5u;

	class folder_path;
	class file_path;
	class date_t;
	class file_size;

	constexpr void assert_true(const bool should_be_true)
	{
#ifdef _DEBUG
		if (!should_be_true)
		{
			__debugbreak();
		}
#endif //_DEBUG
	}

	struct free_delete
	{
		void operator()(void* x) { _aligned_free(x); }
	};

	template <typename T>
	using unique_alloc_ptr = std::unique_ptr<T, free_delete>;

	template <typename T>
	unique_alloc_ptr<T> unique_alloc(const size_t alloc_size)
	{
		return std::unique_ptr<T, free_delete>(static_cast<T*>(_aligned_malloc(alloc_size + 16, 16)));
	}

	extern std::atomic_bool is_closing;
	extern std::atomic_int file_handles_detached;
	extern std::atomic_int jobs_running;
	extern std::atomic_int loading_media;
	extern std::atomic_int command_active;
	extern std::atomic_int dragging_items;
	extern std::atomic_int handling_crash;
	extern const char* rendering_func;
	extern std::u8string gpu_desc;
	extern std::u8string gpu_id;
	extern std::u8string d3d_info;
	extern date_t start_time;
	extern file_path last_loaded_path;
	extern file_path previous_log_path;
	extern file_path log_path;

	void log(std::u8string_view context, std::u8string_view message);
	void log(std::string_view context, std::u8string_view message);
	void log(std::string_view context, std::string_view message);
	void trace(std::u8string_view message);
	void trace(std::string_view message);
	file_path close_log();
	std::u8string format_version(bool short_text);

	int isqrt(int x);
	double now();
	int64_t now_ms();

	class measure_ms
	{
		int& _ms;
		int64_t _start = 0;

	public:
		measure_ms(int& ms) : _ms(ms), _start(now_ms())
		{
		}

		~measure_ms()
		{
			const auto t = now_ms() - _start;
			// average if existing value
			_ms = static_cast<int>(_ms == 0 ? t : (_ms + t) / 2);
		}
	};

	constexpr uint32_t byte_clamp(const int n)
	{
		return (n > 255) ? 255u : (n < 0) ? 0u : static_cast<uint32_t>(n);
	}

	constexpr uint32_t byte_clamp(const uint32_t n)
	{
		return (n > 255) ? 255u : n;
	}

	constexpr int round_up(const int i, const int d)
	{
		return (i % d) ? (i / d) + 1 : (i / d);
	}

	inline int round_up(const float d)
	{
		if (!isnormal(d)) return 0;
		return static_cast<int>(d < 0.0f ? floor(d) : ceil(d));
	}

	inline int round(const double d)
	{
		if (!isnormal(d)) return 0;

		const auto f = floor(d);

		if ((d - f) >= 0.5)
		{
			return static_cast<int>(d >= 0.0 ? ceil(d) : f);
		}
		return static_cast<int>(d < 0.0 ? ceil(d) : f);
	}

	inline int round(const float d)
	{
		if (!isnormal(d)) return 0;

		const auto f = floor(d);

		if ((d - f) >= 0.5f)
		{
			return static_cast<int>(d >= 0.0f ? ceil(d) : f);
		}
		return static_cast<int>(d < 0.0f ? ceil(d) : f);
	}

	constexpr int round(const int i, const int d)
	{
		return (i + (d / 2)) / d;
	}

	constexpr uint64_t round64(const uint64_t i, const uint64_t d)
	{
		return (i + (d / 2)) / d;
	}

	constexpr bool in_range(const uint8_t* section, const size_t section_size, const uint8_t* limit,
		const size_t limit_size)
	{
		return (section >= limit) && ((section + section_size) <= (limit + limit_size));
	}

	// Returns true if x is in range [low..high], else false 
	constexpr bool in_range(const int low, const int high, const int x)
	{
		return low <= x && high >= x;
	}

	constexpr int64_t mul_div(const int64_t n64, const int64_t num64, const int64_t den64)
	{
		return den64 ? ((n64 * num64) + (den64 / 2)) / den64 : -1;
	}

	constexpr int mul_div(const int n, const int num, const int den)
	{
		if (in_range(INT16_MIN, INT16_MAX, n) &&
			in_range(INT16_MIN, INT16_MAX, num))
		{
			return den ? ((n * num) + (den / 2)) / den : -1;
		}

		const int64_t n64 = n;
		const int64_t num64 = num;
		const int64_t den64 = den;

		return static_cast<int>(mul_div(n64, num64, den64));
	}

	template <typename FloatingPoint>
	constexpr FloatingPoint fabs(FloatingPoint x)
	{
		return x >= 0 ? x : x < 0 ? -x : 0;
	}

	constexpr bool equiv(const double x, const double y, const double epsilon = std::numeric_limits<double>::epsilon())
	{
		return fabs(x - y) <= epsilon;
	}

	constexpr bool equiv(const float x, const float y)
	{
		return fabs(x - y) <= std::numeric_limits<float>::epsilon();
	}

	constexpr bool is_zero(const float x)
	{
		return fabs(x) <= std::numeric_limits<float>::epsilon();
	}

	constexpr bool is_zero(const double x)
	{
		return fabs(x) <= std::numeric_limits<double>::epsilon();
	}

	struct no_copy
	{
		no_copy(const no_copy&) = delete;
		no_copy& operator=(const no_copy&) = delete;
		no_copy() = default;
	};

	static constexpr int max_blob_size = 1024 * 1024 * 100;
	using blob = std::vector<uint8_t>;

	struct cspan
	{
		const uint8_t* data = nullptr;
		size_t size = 0;

		cspan() noexcept = default;
		~cspan() noexcept = default;
		cspan(const cspan&) noexcept = default;
		cspan& operator=(const cspan&) noexcept = default;
		cspan(cspan&&) noexcept = default;
		cspan& operator=(cspan&&) noexcept = default;

		cspan(const uint8_t* d, const size_t s) noexcept : data(d), size(s)
		{
		}

		cspan(const blob& b) noexcept : data(b.data()), size(b.size())
		{
		}

		bool operator>(const size_t other) const
		{
			return size > other;
		}

		bool operator>(const int other) const
		{
			return static_cast<int>(size) > other;
		}

		const uint8_t* begin() const
		{
			return data;
		}

		const uint8_t* end() const
		{
			return data + size;
		}

		cspan sub(const size_t pos) const
		{
			cspan result;
			result.data = data + pos;
			result.size = size - pos;
			return result;
		}

		bool empty() const
		{
			return size == 0 || data == nullptr;
		}
	};

	blob blob_from_file(file_path path, size_t max_load = max_blob_size);
	bool blob_save_to_file(cspan data, file_path path);

	struct span
	{
		uint8_t* data = nullptr;
		size_t size = 0;

		span() noexcept = default;
		~span() noexcept = default;
		span(const span&) noexcept = default;
		span& operator=(const span&) noexcept = default;
		span(span&&) noexcept = default;
		span& operator=(span&&) noexcept = default;

		span(uint8_t* d, size_t s) noexcept : data(d), size(s)
		{
		}

		span(blob& b) noexcept : data(b.data()), size(b.size())
		{
		}

		bool operator>(const size_t other) const
		{
			return size > other;
		}

		bool operator>(const int other) const
		{
			return static_cast<int>(size) > other;
		}

		span sub(const size_t pos) const
		{
			span result;
			result.data = data + pos;
			result.size = size - pos;
			return result;
		}

		operator bool() const
		{
			return size > 0 && data;
		}

		operator cspan() const
		{
			return { data, size };
		}

	private:
		span operator+(int) const;
	};

#pragma pack(push, 1)

	struct xy8
	{
		int16_t x, y;

		xy8() noexcept = default;
		constexpr xy8& operator=(const xy8&) noexcept = default;
		constexpr xy8& operator=(xy8&&) noexcept = default;
		constexpr xy8(const xy8&) noexcept = default;
		constexpr xy8(xy8&&) noexcept = default;

		constexpr xy8(const uint8_t xx, const uint8_t yy) : x(xx), y(yy)
		{
		}

		constexpr static xy8 make(const uint8_t xx, const uint8_t yy)
		{
			const xy8 result = { xx, yy };
			return result;
		}

		constexpr bool operator ==(const xy8 other) const
		{
			return x == other.x && y == other.y;
		}

		constexpr bool operator !=(const xy8 other) const
		{
			return x != other.x || y != other.y;
		}

		static xy8 parse(std::u8string_view r);
		std::u8string str() const;
	};


#pragma pack(pop)

	struct xy16
	{
		int16_t x, y;

		xy16() noexcept = default;
		constexpr xy16& operator=(const xy16&) noexcept = default;
		constexpr xy16& operator=(xy16&&) noexcept = default;
		constexpr xy16(const xy16&) noexcept = default;
		constexpr xy16(xy16&&) noexcept = default;

		constexpr xy16(const int16_t xx, const int16_t yy) : x(xx), y(yy)
		{
		}

		constexpr static xy16 make(const int16_t xx, const int16_t yy) noexcept
		{
			const xy16 result = { xx, yy };
			return result;
		}

		friend bool operator==(const xy16& lhs, const xy16& rhs)
		{
			return lhs.x == rhs.x
				&& lhs.y == rhs.y;
		}

		friend bool operator!=(const xy16& lhs, const xy16& rhs)
		{
			return !(lhs == rhs);
		}

		friend bool operator<(const xy16& lhs, const xy16& rhs)
		{
			if (lhs.x < rhs.x)
				return true;
			if (rhs.x < lhs.x)
				return false;
			return lhs.y < rhs.y;
		}

		friend bool operator<=(const xy16& lhs, const xy16& rhs)
		{
			return !(rhs < lhs);
		}

		friend bool operator>(const xy16& lhs, const xy16& rhs)
		{
			return rhs < lhs;
		}

		friend bool operator>=(const xy16& lhs, const xy16& rhs)
		{
			return !(lhs < rhs);
		}
	};

	struct xy32
	{
		int32_t x, y;
		static xy32 null;

		xy32() noexcept = default;
		constexpr xy32& operator=(const xy32&) noexcept = default;
		constexpr xy32& operator=(xy32&&) noexcept = default;
		constexpr xy32(const xy32&) noexcept = default;
		constexpr xy32(xy32&&) noexcept = default;

		constexpr xy32(const int32_t xx, const int32_t yy) : x(xx), y(yy)
		{
		}

		constexpr static xy32 make(const int32_t xx, const int32_t yy)
		{
			const xy32 result = { xx, yy };
			return result;
		}

		constexpr static xy32 make(const uint32_t xx, const uint32_t yy)
		{
			const xy32 result = { static_cast<int32_t>(xx), static_cast<int32_t>(yy) };
			return result;
		}

		static xy32 parse(std::u8string_view r);

		constexpr operator xy16() const
		{
			return xy16::make(static_cast<int16_t>(x), static_cast<int16_t>(y));
		}

		constexpr bool is_empty() const { return x == 0 && y == 0; }

		std::u8string str() const;

		friend bool operator==(const xy32& lhs, const xy32& rhs)
		{
			return lhs.x == rhs.x
				&& lhs.y == rhs.y;
		}

		friend bool operator!=(const xy32& lhs, const xy32& rhs)
		{
			return !(lhs == rhs);
		}

		friend bool operator<(const xy32& lhs, const xy32& rhs)
		{
			if (lhs.x < rhs.x)
				return true;
			if (rhs.x < lhs.x)
				return false;
			return lhs.y < rhs.y;
		}

		friend bool operator<=(const xy32& lhs, const xy32& rhs)
		{
			return !(rhs < lhs);
		}

		friend bool operator>(const xy32& lhs, const xy32& rhs)
		{
			return rhs < lhs;
		}

		friend bool operator>=(const xy32& lhs, const xy32& rhs)
		{
			return !(lhs < rhs);
		}
	};

	class file_size
	{
	public:
		uint64_t _i = 0;

		constexpr file_size() noexcept = default;

		constexpr explicit file_size(const uint32_t d) noexcept : _i(d)
		{
		}

		constexpr explicit file_size(const uint64_t i) noexcept : _i(i)
		{
		}

		constexpr explicit file_size(const int64_t i) noexcept : _i(i)
		{
		}

		constexpr explicit file_size(const int32_t i) noexcept : _i(static_cast<uint64_t>(i))
		{
		}

		constexpr explicit file_size(const float f) noexcept : _i(static_cast<uint64_t>(f))
		{
		}

		constexpr explicit file_size(const double d) noexcept : _i(static_cast<uint64_t>(d))
		{
		}

		constexpr explicit file_size(const long i) noexcept : _i(static_cast<uint64_t>(i))
		{
		}

		constexpr explicit file_size(const unsigned long i) noexcept : _i(static_cast<uint64_t>(i))
		{
		}

		constexpr file_size(const file_size&) noexcept = default;
		constexpr file_size& operator=(const file_size&) noexcept = default;
		constexpr file_size(file_size&&) noexcept = default;
		constexpr file_size& operator=(file_size&&) noexcept = default;

		constexpr file_size& operator=(const uint32_t s) noexcept
		{
			_i = s;
			return *this;
		}

		constexpr file_size& operator=(const uint64_t s) noexcept
		{
			_i = s;
			return *this;
		}

		constexpr file_size operator+(const file_size other) const noexcept
		{
			return file_size(_i + other._i);
		}

		constexpr file_size operator-(const file_size other) const noexcept
		{
			return file_size(_i - other._i);
		}

		constexpr file_size operator/(const size_t other) const noexcept
		{
			return file_size(_i / other);
		}

		constexpr file_size& operator+=(const file_size other) noexcept
		{
			_i += other._i;
			return *this;
		}

		constexpr void clear()
		{
			_i = 0;
		}

		constexpr bool is_empty() const
		{
			return _i == 0;
		}

		constexpr bool is_valid() const
		{
			return _i != 0;
		}

		friend bool operator==(const file_size& lhs, const file_size& rhs)
		{
			return lhs._i == rhs._i;
		}

		friend bool operator!=(const file_size& lhs, const file_size& rhs)
		{
			return !(lhs == rhs);
		}

		friend bool operator<(const file_size& lhs, const file_size& rhs)
		{
			return lhs._i < rhs._i;
		}

		friend bool operator<=(const file_size& lhs, const file_size& rhs)
		{
			return !(rhs < lhs);
		}

		friend bool operator>(const file_size& lhs, const file_size& rhs)
		{
			return rhs < lhs;
		}

		friend bool operator>=(const file_size& lhs, const file_size& rhs)
		{
			return !(lhs < rhs);
		}

		constexpr int to_kb() const
		{
			return static_cast<int>(_i / 1024);
		}

		constexpr int to_int() const
		{
			return static_cast<int>(_i);
		}

		constexpr uint64_t to_int64() const
		{
			return _i;
		}

		constexpr float to_float() const
		{
			return static_cast<float>(_i);
		}

		std::u8string str() const;

		static file_size null;
	};

	struct count_and_size
	{
		uint32_t count = 0;
		file_size size;

		count_and_size operator+(const count_and_size other) const
		{
			return { count + other.count, size + other.size };
		}

		count_and_size operator+=(const count_and_size other)
		{
			count += other.count;
			size += other.size;
			return *this;
		}

		void add(const file_size& s)
		{
			++count;
			size += s;
		}

		friend bool operator==(const count_and_size& lhs, const count_and_size& rhs)
		{
			return lhs.count == rhs.count
				&& lhs.size == rhs.size;
		}

		friend bool operator!=(const count_and_size& lhs, const count_and_size& rhs)
		{
			return !(lhs == rhs);
		}
	};

	class scope_locked_inc : public no_copy
	{
	private:
		std::atomic_int& _i;
		const long _current;

	public:
		scope_locked_inc(std::atomic_int& i) : _i(i), _current(++i)
		{
		}

		~scope_locked_inc()
		{
			--_i;
		}
	};

	class scope_rendering_func : public no_copy
	{
	private:
		const char* _prev = "";

	public:
		scope_rendering_func(const char* f) : _prev(rendering_func)
		{
			rendering_func = f;
		}

		~scope_rendering_func()
		{
			rendering_func = _prev;
		}
	};

	struct version
	{
		int major = 0;
		int minor = 0;

		version(std::u8string_view version);

		friend bool operator==(const version& lhs, const version& rhs)
		{
			return lhs.major == rhs.major
				&& lhs.minor == rhs.minor;
		}

		friend bool operator!=(const version& lhs, const version& rhs)
		{
			return !(lhs == rhs);
		}

		friend bool operator<(const version& lhs, const version& rhs)
		{
			if (lhs.major < rhs.major)
				return true;
			if (rhs.major < lhs.major)
				return false;
			return lhs.minor < rhs.minor;
		}

		friend bool operator<=(const version& lhs, const version& rhs)
		{
			return !(rhs < lhs);
		}

		friend bool operator>(const version& lhs, const version& rhs)
		{
			return rhs < lhs;
		}

		friend bool operator>=(const version& lhs, const version& rhs)
		{
			return !(lhs < rhs);
		}

		version& operator +(const int i)
		{
			major += 1;
			return *this;
		}

		friend u8ostringstream& operator <<(u8ostringstream& stream, const version& ver)
		{
			stream << ver.major;
			stream << '.';
			stream << ver.minor;
			return stream;
		}

		std::u8string to_string() const
		{
			u8ostringstream s;
			s << *this;
			return s.str();
		}
	};

	struct int_counter
	{
		int i = 0;

		void operator++()
		{
			i++;
		}

		void operator+=(const int n)
		{
			i += n;
		}

		int_counter& operator=(const int n)
		{
			i = n;
			return *this;
		}

		operator int() const
		{
			return i;
		}
	};

	class cancel_token
	{
	private:
		static std::atomic_int empty;
		std::atomic_int& version;
		int job_version = 0;

	public:
		bool is_cancelled() const
		{
			return is_closing || job_version != version.load(std::memory_order_relaxed);
		}


		~cancel_token() noexcept = default;
		cancel_token& operator=(const cancel_token& other) noexcept = delete;
		cancel_token& operator=(cancel_token&& other) noexcept = delete;
		cancel_token(const cancel_token& other) noexcept = default;
		cancel_token(cancel_token&& other) noexcept = default;

		cancel_token() noexcept : version(empty)
		{
			// empty version
		}

		cancel_token(std::atomic_int& v) : version(v)
		{
			++v;
			job_version = version.load(std::memory_order_relaxed);
		}
	};

	inline uint64_t byteswap64(const uint64_t n)
	{
		return _byteswap_uint64(n);
	}

	inline uint64_t byteswap64(const uint8_t* addr)
	{
		return _byteswap_uint64(*std::bit_cast<const uint64_t*>(addr));
	}

	inline uint32_t byteswap32(const uint32_t n)
	{
		return _byteswap_ulong(n);
	}

	inline uint32_t byteswap32(const uint8_t* addr)
	{
		return _byteswap_ulong(*std::bit_cast<const uint32_t*>(addr));
	}

	inline uint16_t byteswap16(const uint16_t n)
	{
		return _byteswap_ushort(n);
	}

	inline uint16_t byteswap16(const uint8_t* addr)
	{
		return _byteswap_ushort(*std::bit_cast<const uint16_t*>(addr));
	}

	inline std::u8string url_encode(const std::u8string_view url)
	{
		std::u8string result;

		for (const auto c : url)
		{
			if ((48 <= c && c <= 57) || //0-9
				(65 <= c && c <= 90) || //ABC...XYZ
				(97 <= c && c <= 122) || //abc...xyz
				(c == '~' || c == '-' || c == '_' || c == '.')
				)
			{
				result += c;
			}
			else
			{
				static constexpr std::u8string_view chars = u8"0123456789ABCDEF"sv;

				result += '%';
				result += chars[(c & 0xF0) >> 4];
				result += chars[c & 0x0F];
			}
		}

		return result;
	}
} // namespace
