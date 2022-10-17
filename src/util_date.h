// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

namespace df
{
	struct day_t
	{
		int year = 0, month = 0, day = 0;
		int hour = 0, minute = 0, second = 0;
		bool is_empty() const { return year == 0 && month == 0 && day == 0; };
	};

	struct day_range_t
	{
		day_t start;
		day_t end;

		bool is_empty() const
		{
			return start.is_empty() && end.is_empty();
		}
	};

	class date_t
	{
	public:
		static constexpr uint64_t intervals_per_second = 10000000LL;
		static constexpr uint64_t intervals_per_day = intervals_per_second * 24LL * 60LL * 60LL;
		static constexpr uint64_t offset_1970 = 116444736000000000LL;
		static constexpr uint64_t offset_2000 = 125911584000000000LL;

		uint64_t _i = 0;

		constexpr date_t() noexcept = default;
		constexpr date_t(const date_t&) noexcept = default;
		constexpr date_t& operator=(const date_t&) noexcept = default;
		constexpr date_t(date_t&&) noexcept = default;
		constexpr date_t& operator=(date_t&&) noexcept = default;

		date_t(const int y, const int m, const int d, const int hh = 0, const int mm = 0, const int ss = 0) noexcept
		{
			const day_t dd{y, m, d, hh, mm, ss};
			date(dd);
		}

		constexpr explicit date_t(const uint64_t n) noexcept : _i(n)
		{
		}

		constexpr int64_t operator+(const date_t other) const noexcept
		{
			return static_cast<int64_t>(_i) + static_cast<int64_t>(other._i);
		}

		constexpr int64_t operator-(const date_t other) const noexcept
		{
			return static_cast<int64_t>(_i) - static_cast<int64_t>(other._i);
		}

		constexpr date_t operator+(const int64_t& other) const noexcept
		{
			return date_t(static_cast<uint64_t>(static_cast<int64_t>(_i) + other));
		}

		constexpr date_t operator-(const int64_t& other) const noexcept
		{
			return date_t(static_cast<uint64_t>(static_cast<int64_t>(_i) - other));
		}

		static date_t null;

		constexpr date_t& operator=(int64_t n)
		{
			_i = n;
			return *this;
		}

		constexpr uint64_t to_int64() const
		{
			return _i;
		}

		friend bool operator==(const date_t lhs, const date_t rhs)
		{
			return lhs._i == rhs._i;
		}

		friend bool operator!=(const date_t lhs, const date_t rhs)
		{
			return !(lhs == rhs);
		}

		friend bool operator<(const date_t lhs, const date_t rhs)
		{
			return lhs._i < rhs._i;
		}

		friend bool operator<=(const date_t lhs, const date_t rhs)
		{
			return !(rhs < lhs);
		}

		friend bool operator>(const date_t lhs, const date_t rhs)
		{
			return rhs < lhs;
		}

		friend bool operator>=(const date_t lhs, const date_t rhs)
		{
			return !(lhs < rhs);
		}

		constexpr void clear()
		{
			_i = 0;
		}

		int year() const
		{
			const auto st = date();
			return st.year;
		}

		int month() const
		{
			const auto st = date();
			return st.month;
		}

		int day() const
		{
			const auto st = date();
			return st.day;
		}

		day_t date() const
		{
			return platform::to_date(_i);
		}

		bool date(const day_t& d)
		{
			_i = platform::from_date(d);
			return _i != 0;
		}

		int to_order() const
		{
			if (!is_valid()) return 0;
			const auto st = date();
			return (st.year * 100) + st.month;
		}

		static date_t from(std::u8string_view r);

		static date_t from_time_t(time_t t)
		{
			const uint64_t ll = static_cast<uint64_t>(t) * 10000000ll + offset_1970;
			return date_t(ll);
		}

		bool parse(std::u8string_view text);
		bool parse_exif_date(std::u8string_view r);
		bool parse_xml_date(std::u8string_view r);

		std::u8string to_iptc_date() const
		{
			const auto st = date();
			return str::format(u8"{:04}{:02}{:02}"sv, st.year, st.month, st.day);
		}

		std::u8string to_polish_date() const
		{
			const auto st = date();
			return str::format(u8"{:04}-{:02}-{:02}"sv, st.year, st.month, st.day);
		}

		std::u8string to_xmp_date() const;

		date_t system_to_local() const;

		date_t local_to_system() const;

		constexpr static date_t from_days(uint32_t days)
		{
			return date_t(days * intervals_per_day);
		}

		__forceinline constexpr uint32_t to_days() const
		{
			return static_cast<uint32_t>(_i / intervals_per_day);
		}

		constexpr date_t previous_day() const
		{
			const auto previous = (_i / intervals_per_day) - 1;
			return date_t(previous * intervals_per_day);
		}

		static constexpr bool is_leap_year(const int year)
		{
			return (year % 4 == 0) && (year % 100 != 0 || year % 400 == 0);
		}

		constexpr int to_years() const
		{
			if (_i == 0)
			{
				return 0;
			}

			if (_i < offset_2000)
			{
				return year();
			}

			auto days_after_2000 = static_cast<int>((_i - offset_2000) / intervals_per_day);
			auto year = 2000;

			while (days_after_2000 > 0)
			{
				days_after_2000 -= is_leap_year(year) ? 366 : 365;
				if (days_after_2000 <= 0) return year;
				year += 1;
			}

			return year;
		}

		constexpr date_t add_day(int d) const
		{
			return from_days(to_days() + d);
		}

		static constexpr date_t from_seconds(int64_t seconds)
		{
			return date_t(seconds * intervals_per_second);
		}

		constexpr uint64_t to_seconds() const
		{
			return _i / intervals_per_second;
		}

		constexpr bool matches(const date_t other) const
		{
			return to_days() == other.to_days();
		}

		__forceinline bool is_valid() const
		{
			if (_i == 0) return false;

			static const auto min = date_t(1800, 1, 1).to_days();
			static const auto max = platform::now().to_days();
			static const auto null_date_cpp = date_t(1970, 1, 1).to_days();
			static const auto null_date_mac = date_t(1904, 1, 1).to_days();

			const auto days = to_days();
			return days >= min && days <= max && days != null_date_cpp && days != null_date_mac;
		}

		void shift_days(int days)
		{
			_i += intervals_per_day * days;
		}
	};

	__declspec(selectany) date_t date_t::null;
};
