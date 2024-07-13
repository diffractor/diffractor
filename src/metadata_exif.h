// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once


namespace prop
{
	struct item_metadata;
	using item_metadata_ptr = std::shared_ptr<item_metadata>;
}

enum exif_format
{
	NUM_FORMATS = 12,
	FMT_BYTE = 1,
	FMT_STRING = 2,
	FMT_USHORT = 3,
	FMT_ULONG = 4,
	FMT_URATIONAL = 5,
	FMT_SBYTE = 6,
	FMT_UNDEFINED = 7,
	FMT_SSHORT = 8,
	FMT_SLONG = 9,
	FMT_SRATIONAL = 10,
	FMT_SINGLE = 11,
	FMT_DOUBLE = 12
};


inline std::vector<uint8_t> make_orientation_exif(ui::orientation orientation)
{
	std::vector<uint8_t> exif_block = {
		0x49, 0x49,
		0x2A, 0x00,
		0x08, 0x00, 0x00, 0x00, // ifd0 offset
		0x01, 0x00, // ifd0 entrycount
		0x12, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, static_cast<uint8_t>(orientation), 0x00, 0x00, 0x00,
		// orientation
		0x1a, 0x00, 0x00, 0x00, // ifd1 offset
		0x00, 0x00, // ifd1 entry count
		0x00, 0x00,
	};

	return exif_block;
}

constexpr bool first_char_is(const std::u8string_view sv, const char8_t c)
{
	return !sv.empty() && sv[0] == c;
}

class exif_gps_coordinate_builder
{
public:
	enum class NorthSouth
	{
		North,
		South
	};

	enum class EastWest
	{
		East,
		West
	};

private:
	static constexpr int invalid_coordinate = 180;

	NorthSouth _south;
	EastWest _west;
	double _latitude;
	double _longitude;

public:
	exif_gps_coordinate_builder();

	bool is_valid() const;

	void latitude_north_south(NorthSouth value)
	{
		_south = value;
	}

	void longitude_east_west(EastWest value)
	{
		_west = value;
	}

	void latitude(double value)
	{
		_latitude = value;
	}

	void longitude(double value)
	{
		_longitude = value;
	}


	gps_coordinate build() const;
};


namespace metadata_exif
{
	template <typename T>
	class rational_t
	{
	public:
		T numerator = 0;
		T denominator = 0;

		rational_t() noexcept = default;
		~rational_t() noexcept = default;
		rational_t(const rational_t&) = default;
		rational_t& operator=(const rational_t&) = default;
		rational_t(rational_t&&) noexcept = default;
		rational_t& operator=(rational_t&&) noexcept = default;

		rational_t(const T n, const T d) noexcept : numerator(n), denominator(d)
		{
		}

		void clear()
		{
			numerator = 0;
			denominator = 0;
		}

		rational_t& operator=(const double other)
		{
			numerator = static_cast<T>(other * 1000.0f);
			denominator = 1000;

			return *this;
		}

		friend bool operator==(const rational_t& lhs, const rational_t& rhs)
		{
			return lhs.numerator == rhs.numerator
				&& lhs.denominator == rhs.denominator;
		}

		friend bool operator!=(const rational_t& lhs, const rational_t& rhs)
		{
			return !(lhs == rhs);
		}

		bool operator<(const rational_t& other) const
		{
			return to_real() < other.to_real();
		}

		double to_real() const
		{
			if (denominator == 0) return 0.0;
			return static_cast<double>(numerator) / static_cast<double>(denominator);
		}

		T mul_div(T m) const
		{
			if (denominator == 0) return 0;
			return df::mul_div(m, numerator, denominator);
		}

		bool is_empty() const
		{
			return numerator == 0 || denominator == 0;
		}

		int round() const
		{
			return df::round(to_real());
		}

		int64_t to_int64() const
		{
			int64_t i = static_cast<uint32_t>(numerator);
			i <<= 32;
			i |= static_cast<uint32_t>(denominator);
			return i;
		}

		static rational_t<T> from_int64(int64_t i)
		{
			rational_t result;
			result.numerator = static_cast<T>(i >> 32);
			result.denominator = static_cast<T>(i & 0xffffffff);
			return result;
		}
	};

	using srational32_t = rational_t<uint32_t>;
	using urational32_t = rational_t<int32_t>;

	void parse(prop::item_metadata& pd, df::cspan cs);
	void fix_exif_dimensions(df::span data, sizei dimensions);
	void fix_exif_rating(df::span data, int rating);
	metadata_kv_list to_info(df::cspan data);
	df::blob fix_dims(df::span cs, int image_width, int image_height);
	df::blob make_exif(const prop::item_metadata_ptr& md);
};
