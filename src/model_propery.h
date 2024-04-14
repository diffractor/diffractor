// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

#include "model_location.h"

enum class icon_index : int;
class file_type;
class file_group;

using file_type_ref = const file_type*;
using file_group_ref = const file_group*;

namespace ui
{
	class surface;
	class image;
	enum class orientation : uint8_t;
}

struct bloom_bits
{
	static constexpr uint32_t flag = 1 << 30u;
	static constexpr uint32_t hash = 1 << 29u;

	static constexpr uint32_t album = 1 << 8u;
	static constexpr uint32_t artist = 1 << 9u;
	static constexpr uint32_t camera = 1 << 10u;
	static constexpr uint32_t camera_settings = 1 << 11u;
	static constexpr uint32_t codec = 1 << 12u;
	static constexpr uint32_t audio_codec = 1 << 13u;
	static constexpr uint32_t created = 1 << 14u;
	static constexpr uint32_t credit = 1 << 15u;
	static constexpr uint32_t duration = 1 << 16u;
	static constexpr uint32_t general = 1 << 17u;
	static constexpr uint32_t genre = 1 << 18u;
	static constexpr uint32_t location = 1 << 19u;
	static constexpr uint32_t rating_label = 1 << 20u;
	static constexpr uint32_t game = 1 << 21u;
	static constexpr uint32_t season = 1 << 22u;
	static constexpr uint32_t tag = 1 << 23u;
	static constexpr uint32_t text = 1 << 24u;
	static constexpr uint32_t year = 1 << 25u;
	static constexpr uint32_t doc_id = 1 << 26u;

	uint32_t types = 0;

	bloom_bits() noexcept = default;
	~bloom_bits() noexcept = default;
	bloom_bits(const bloom_bits&) noexcept = default;
	bloom_bits& operator=(const bloom_bits&) noexcept = default;
	bloom_bits(bloom_bits&&) noexcept = default;
	bloom_bits& operator=(bloom_bits&&) noexcept = default;

	const bloom_bits& operator|=(const bloom_bits& other)
	{
		types |= other.types;
		return *this;
	}

	bool potential_match(const bloom_bits& other) const
	{
		return (types & other.types) == types;
	}
};

constexpr std::u8string_view label_select_text = u8"select"sv;
constexpr std::u8string_view label_second_text = u8"second"sv;
constexpr std::u8string_view label_approved_text = u8"approved"sv;
constexpr std::u8string_view label_review_text = u8"review"sv;
constexpr std::u8string_view label_to_do_text = u8"to do"sv;

namespace df
{
	struct search_term;
};

// Metadata Constants
namespace prop
{
	struct item_metadata;
	class key;

	enum class data_type
	{
		int32 = 1,
		size = 2,
		date = 3,
		float32 = 4,
		string = 5,
		int_pair = 7,
		uint32 = 8,
		string2 = string,
	};

	enum class audio_sample_t
	{
		none = 0,
		unsigned_8bit = 8,
		signed_16bit = 16,
		signed_32bit = 32,
		signed_64bit = 64,
		signed_float = 32 | 1,
		signed_double = 64 | 1,
		unsigned_planar_8bit = 8 | 2,
		signed_planar_16bit = 16 | 2,
		signed_planar_32bit = 32 | 2,
		signed_planar_64bit = 64 | 2,
		planar_float = 32 | 1 | 2,
		planar_double = 64 | 1 | 2,
	};

	namespace style
	{
		enum
		{
			none = 0,
			groupable = 1 << 1,
			sortable = 1 << 2,
			multi_value = 1 << 3,
			fuzzy_search = 1 << 4,
			auto_complete = 1 << 5,
		};
	};

	class key;
	using key_ref = const key*;

	class key
	{
	private:
		key(const key& other) = delete;
		key& operator=(const key& other) = delete;
		key(key&& other) noexcept = delete;
		key& operator=(key&& other) = delete;

	public:
		key(uint16_t id, std::u8string_view sn, std::u8string_view n, std::u8string_view& tx, icon_index i, data_type t,
		    uint32_t f, uint32_t bit);

		uint16_t id = 0;
		icon_index icon;
		str::cached short_name;
		str::cached name;
		std::u8string_view& text_key;
		data_type data_type;
		uint32_t flags = 0;
		uint32_t bloom_bit = 0;

		friend bool operator==(const key& lhs, const key& rhs)
		{
			return lhs.id == rhs.id;
		}

		friend bool operator!=(const key& lhs, const key& rhs)
		{
			return !(lhs == rhs);
		}

		constexpr bool operator<(const key& other) const
		{
			return id < other.id;
		}

		constexpr bool is_multi_value() const
		{
			return (flags & style::multi_value) != 0;
		}

		constexpr bool is_fuzzy_search() const
		{
			return (flags & style::fuzzy_search) != 0;
		}

		constexpr bool is_groupable() const
		{
			return (flags & style::groupable) != 0;
		}

		constexpr bool is_auto_complete() const
		{
			return (flags & style::auto_complete) != 0;
		}

		constexpr operator key_ref() const
		{
			return this;
		}

		std::u8string text() const;
	};


	extern key album;
	extern key artist;
	extern key album_artist;
	extern key audio_codec;
	extern key audio_sample_rate;
	extern key audio_sample_type;
	extern key bitrate;
	extern key camera_manufacturer;
	extern key camera_model;
	extern key audio_channels;
	extern key location_place;
	extern key comment;
	extern key description;
	extern key disk_num;
	extern key composer;
	extern key copyright_credit;
	extern key copyright_source;
	extern key copyright_creator;
	extern key copyright_notice;
	extern key copyright_url;
	extern key location_country;
	extern key created_exif;
	extern key created_digitized;
	extern key created_utc;
	extern key dimensions;
	extern key duration;
	extern key encoder;
	extern key encoding_tool;
	extern key episode;
	extern key exposure_time;
	extern key f_number;
	extern key focal_length_35mm_equivalent;
	extern key focal_length;
	extern key pixel_format;
	extern key genre;
	extern key iso_speed;
	extern key latitude;
	extern key lens;
	extern key longitude;
	extern key media_category;
	extern key megapixels;
	extern key modified;
	extern key null;
	extern key orientation;
	extern key publisher;
	extern key performer;
	extern key rating;
	extern key season;
	extern key show;
	extern key file_size;
	extern key location_state;
	extern key streams;
	extern key synopsis;
	extern key tag;
	extern key track_num;
	extern key title;
	extern key video_codec;
	extern key year;
	extern key unique_id;
	extern key file_name;
	extern key raw_file_name;
	extern key system;
	extern key game;
	extern key crc32c;
	extern key label;
	extern key doc_id;

	constexpr bool is_null(const std::u8string_view s)
	{
		return str::is_empty(s);
	}

	constexpr bool is_null(const float n)
	{
		return df::is_zero(n);
	}

	constexpr bool is_null(const int n)
	{
		return n == 0;
	}

	constexpr bool is_null(const uint32_t n)
	{
		return n == 0;
	}

	inline bool is_null(const df::date_t d)
	{
		return !d.is_valid();
	}

	constexpr bool is_null(const sizei s)
	{
		return s.is_empty();
	}

	constexpr bool is_null(const df::xy32 xy)
	{
		return xy.is_empty();
	}

	constexpr bool is_null(const df::xy8 xy)
	{
		return xy.x == 0 || xy.y == 0;
	}

#pragma pack(push, 1)

	struct item_metadata
	{
		item_metadata() noexcept = default;

		item_metadata& operator=(const item_metadata& other) noexcept = default;
		item_metadata& operator=(item_metadata&& other) noexcept = default;

		item_metadata(const item_metadata& other) = default;
		item_metadata(item_metadata&& other) = default;

		sizei dimensions() const { return {width, height}; }

		str::cached album;
		str::cached album_artist;
		str::cached artist;
		str::cached audio_codec;
		str::cached bitrate;
		str::cached camera_manufacturer;
		str::cached camera_model;
		str::cached comment;
		str::cached composer;
		str::cached copyright_creator;
		str::cached copyright_credit;
		str::cached copyright_url;
		str::cached copyright_notice;
		str::cached copyright_source;
		str::cached description;
		str::cached encoder;
		str::cached file_name;
		str::cached genre;
		str::cached lens;
		str::cached location_place;
		str::cached location_country;
		str::cached location_state;
		str::cached performer;
		str::cached pixel_format;
		str::cached publisher;
		str::cached show;
		str::cached synopsis;
		str::cached title;
		str::cached video_codec;
		str::cached raw_file_name;
		str::cached tags;
		str::cached game;
		str::cached system;
		str::cached label;
		str::cached doc_id;

		float exposure_time = 0.0f;
		float f_number = 0.0f;
		float focal_length = 0.0f;
		gps_coordinate coordinate;

		df::date_t created_digitized;
		df::date_t created_exif;
		df::date_t created_utc;

		uint16_t duration = 0;
		uint16_t focal_length_35mm_equivalent = 0;
		uint16_t height = 0;
		uint16_t iso_speed = 0;
		int16_t rating = 0;
		uint16_t audio_channels = 0;
		uint16_t audio_sample_rate = 0;
		uint16_t audio_sample_type = 0;
		uint16_t width = 0;
		uint16_t year = 0;
		uint8_t season = 0;
		ui::orientation orientation = ui::orientation::top_left;
		df::xy8 disk = {0, 0};
		df::xy8 episode = {0, 0};
		df::xy8 track = {0, 0};

		double media_position = 0.0;

		str::cached sidecars;
		str::cached xmp;

		df::date_t created() const
		{
			auto d = created_utc.system_to_local();

			if (!d.is_valid())
			{
				d = created_exif;
			}

			return d;
		}

		bloom_bits calc_bloom_bits() const;

		bool has_gps() const
		{
			return coordinate.is_valid();
		}

		std::u8string format(std::u8string_view name) const;
	};

#pragma pack(pop)

	using item_metadata_ptr = std::shared_ptr<item_metadata>;
	using item_metadata_aptr = std::atomic<std::shared_ptr<item_metadata>>;
	using item_metadata_const_ptr = std::shared_ptr<const item_metadata>;

	const key* from_id(uint16_t id);
	const key* from_prefix(std::u8string_view scope);

	struct prop_scope
	{
		std::u8string scope;
		const key* type;
	};

	std::vector<prop_scope> search_scopes();
	std::vector<prop_scope> key_scopes();

	int64_t size_bucket(int64_t n);
	double closest_fstop(double fs);
	int megapixels_order(double val);
	int exp_round(double d);

	struct size_rounded
	{
		std::u8string_view unit;
		std::u8string_view short_unit;
		uint64_t n = 0;
		uint64_t dec = 0;
		uint64_t div = 0;
		int rounded = 0;

		uint64_t total() const
		{
			return (n * div) + ((dec * div) / 10);
		}
	};

	size_rounded round_size(uint64_t s);

	std::u8string format_f_num(double d);
	std::u8string format_fstop(double d);
	std::u8string format_date(df::date_t d);
	std::u8string format_polish_date(df::date_t ft);
	std::u8string format_duration(int d);
	std::u8string format_exposure(double d);
	std::u8string format_focal_length(double d, int filmEquivalent);
	std::u8string format_four_cc(uint32_t f);
	std::u8string format_gps(double d);
	std::u8string format_gps(double lat, double lon);
	std::u8string format_iso(int i);
	std::u8string format_audio_sample_rate(int v);
	std::u8string format_audio_sample_rate(uint16_t v);
	std::u8string format_audio_sample_type(audio_sample_t v);
	std::u8string format_audio_channels(int v);
	std::u8string format_size(const df::file_size& s);
	std::u8string format_magnitude(const df::file_size& s);
	std::u8string format_streams(int s);
	std::u8string format_white_balance(int i);
	std::u8string format_rating(int i);
	std::u8string format_bit_rate(int64_t i);
	std::u8string format_dimensions(sizei v);
	std::u8string format_pixels(sizei v, file_type_ref ft);
	std::u8string format_video_resolution(sizei v);

	std::u8string replace_tokens(std::u8string_view text, const item_metadata_const_ptr& md);

	inline double to_apex_val(const double d)
	{
		return std::log(d) * 2.0 / std::log(2.0);
	}

	inline double aperture_to_fstop(const double d)
	{
		return std::exp(std::log(2.0) * d / 2.0);
	}
};
