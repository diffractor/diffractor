// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once
#include "util.h"


class gps_coordinate
{
public:
	const static int invalid_coordinate = 180;

	double _latitude = invalid_coordinate;
	double _longitude = invalid_coordinate;


	gps_coordinate(const double latitude, const double longitude) noexcept : _latitude(latitude), _longitude(longitude)
	{
	}

	gps_coordinate() noexcept = default;

	gps_coordinate& operator=(const gps_coordinate& other) noexcept = default;
	gps_coordinate& operator=(gps_coordinate&& other) noexcept = default;
	gps_coordinate(const gps_coordinate& other) noexcept = default;
	gps_coordinate(gps_coordinate&& other) noexcept = default;

	bool matches(const gps_coordinate other) const
	{
		return df::equiv(_latitude, other._latitude) && df::equiv(_longitude, other._longitude);
	}

	friend bool operator==(const gps_coordinate lhs, const gps_coordinate rhs)
	{
		return df::equiv(lhs._latitude, rhs._latitude)
			&& df::equiv(lhs._longitude, rhs._longitude);
	}

	friend bool operator!=(const gps_coordinate lhs, const gps_coordinate rhs)
	{
		return !(lhs == rhs);
	}

	bool is_empty() const
	{
		return df::equiv(_latitude, invalid_coordinate) || df::equiv(_longitude, invalid_coordinate);
	}

	void clear()
	{
		_latitude = invalid_coordinate;
		_longitude = invalid_coordinate;
	}

	bool operator<(const gps_coordinate rhs) const
	{
		if (df::equiv(latitude(), rhs.latitude()))
		{
			return longitude() < rhs.longitude();
		}
		return latitude() < rhs.latitude();
	}

	bool is_valid() const
	{
		const auto absolute_latitude = abs(_latitude);
		const auto absolute_longitude = abs(_longitude);
		return (absolute_latitude >= 0.0 && absolute_latitude < invalid_coordinate) &&
			(absolute_longitude >= 0.0 && absolute_longitude < invalid_coordinate);
	}

	double latitude() const
	{
		return _latitude;
	}

	double longitude() const
	{
		return _longitude;
	}

	void latitude(const double value)
	{
		_latitude = value;
	}

	void longitude(const double value)
	{
		_longitude = value;
	}

	std::u8string str() const;

	static void decimal_to_dms(double coord, uint32_t& deg, uint32_t& min, uint32_t& sec);

	static std::u8string decimal_to_dms_str(const double coord, const bool is_ns)
	{
		uint32_t n[3];
		decimal_to_dms(coord, n);
		return str::print(u8"%lu,%lu,%lu%c"sv, n[0], n[1], n[2],
		                  is_ns ? (coord < 0 ? 'S' : 'N') : (coord < 0 ? 'W' : 'E'));
	}

	static void decimal_to_dms(const double coord, uint32_t u[3])
	{
		decimal_to_dms(coord, u[0], u[1], u[2]);
	}

	static double dms_to_decimal(int deg, int min, int sec);
	static double dms_to_decimal(double deg, double min, double sec);

	double magnitude_between_locations(const gps_coordinate& from_loc) const
	{
		const auto d_lat = _latitude - from_loc._latitude;
		const auto d_lon = _longitude - from_loc._longitude;

		return sqrt(d_lat * d_lat + d_lon * d_lon);
	}

	static double deg2rad(const double deg)
	{
		return (deg * M_PI / 180);
	}

	static double rad2deg(const double rad)
	{
		return (rad * 180 / M_PI);
	}

	// from http://en.wikipedia.org/wiki/Haversine_formula
	double distance_in_kilometers(const gps_coordinate other) const
	{
		static constexpr auto earth_radius_km = 6371.0;

		const auto lat1r = deg2rad(_latitude);
		const auto lon1r = deg2rad(_longitude);
		const auto lat2r = deg2rad(other._latitude);
		const auto lon2r = deg2rad(other._longitude);
		const auto u = sin((lat2r - lat1r) / 2.0);
		const auto v = sin((lon2r - lon1r) / 2.0);
		const auto result = 2.0 * earth_radius_km * asin(sqrt(u * u + cos(lat1r) * cos(lat2r) * v * v));

		return result;
	}
};

struct location_and_distance_t
{
	gps_coordinate position;
	double km = 1.0;
};

struct selected_location_t
{
	uint32_t id = 0;

	std::u8string search_text;
	std::u8string place_text;
	std::u8string state_text;
	std::u8string country_text;
	double latitude = gps_coordinate::invalid_coordinate;
	double longitude = gps_coordinate::invalid_coordinate;
};

struct split_location_result
{
	bool success = false;

	double x = 0.0;
	double y = 0.0;
	double z = 0.0;
};

split_location_result split_location(std::u8string_view text);

struct location_t
{
	uint32_t id = 0;
	str::cached place;
	str::cached state;
	str::cached country;
	gps_coordinate position;

	location_t() noexcept = default;

	location_t& operator=(const location_t& other) noexcept = default;
	location_t& operator=(location_t&& other) noexcept = default;
	location_t(const location_t& other) noexcept = default;
	location_t(location_t&& other) noexcept = default;

	location_t(const uint32_t id_in, const str::cached city_in, const str::cached state_in,
	           const str::cached country_in, const gps_coordinate position_in, const double pop) noexcept
		: id(id_in), place(city_in), state(state_in), country(country_in), position(position_in)
	{
	}

	friend bool operator==(const location_t& lhs, const location_t& rhs)
	{
		return lhs.id == rhs.id;
	}

	friend bool operator!=(const location_t& lhs, const location_t& rhs)
	{
		return !(lhs == rhs);
	}

	bool operator<(const location_t& other) const
	{
		return id < other.id;
	}

	bool is_empty() const
	{
		return id == 0 && str::is_empty(place) && str::is_empty(state) & str::is_empty(country) && position.
			is_empty();
	}

	bool has_name() const
	{
		return !str::is_empty(place) || !str::is_empty(state) || !str::is_empty(country);
	}

	bool has_gps() const
	{
		return !position.is_empty();
	}

	std::u8string str() const;

	void clear();

	double estimated_distance_away(const gps_coordinate other) const
	{
		return position.magnitude_between_locations(other);
	}

	double estimated_distance_away(const location_t& other) const
	{
		return estimated_distance_away(other.position);
	}
};
