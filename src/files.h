// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

#include "files_jpeg.h"

#include "model_tags.h"
#include "model_location.h"
#include "model_propery.h"

enum class group_key_type : uint32_t
{
	folder = 1 << 0,
	photo = 1 << 1,
	video = 1 << 2,
	audio = 1 << 3,
	grouped_value = 1 << 4,
	grouped_no_value = 1 << 5,
	archive = 1 << 6,
	retro = 1 << 7,
	other = 1 << 8,
};

template <class tStringObj>
class TXMPMeta;
using SXMPMeta = TXMPMeta<std::string>;

class read_stream;
class av_media_info;
class av_scaler;
class file_scan_result;
class file_group;
struct file_tool;

struct media_name_props
{
	std::u8string show;
	std::u8string title;

	int season = 0;
	int episode = 0;
	int episode_of = 0;
	int year = 0;
};

using file_group_by_name = df::hash_map<std::u8string_view, file_group_ref, df::ihash, df::ieq>;
using file_type_by_extension = df::hash_map<std::u8string_view, file_type_ref, df::ihash, df::ieq>;
using file_tool_by_extension = df::hash_map<std::u8string_view, file_tool*, df::ihash, df::ieq>;
using file_tool_by_name = df::hash_map<std::u8string_view, file_tool*, df::ihash, df::ieq>;

enum class file_traits : uint32_t
{
	none = 0,
	raw = 1 << 0,
	embedded_xmp = 1 << 1,
	no_metadata_grouping = 1 << 3,
	disk_image = 1 << 4,
	text = 1 << 5,
	visualize_audio = 1 << 6,
	preview_video = 1 << 7,
	cache_metadata = 1 << 8,
	zoom = 1 << 9,
	hide_overlays = 1 << 12,
	thumbnail = 1 << 13,
	file_name_metadata = 1 << 14,
	music_metadata = 1 << 15,
	video_metadata = 1 << 16,
	photo_metadata = 1 << 17,
	edit = 1 << 18,

	av = 1 << 20,
	bitmap = 1 << 21,
	archive = 1 << 22,
	commodore = 1 << 23,
};


constexpr file_traits operator|(file_traits a, file_traits b)
{
	return static_cast<file_traits>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr file_traits operator&(file_traits a, file_traits b)
{
	return static_cast<file_traits>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

constexpr bool operator&&(file_traits a, file_traits b)
{
	return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

enum class detected_format
{
	Unknown = 0,
	JPEG,
	PNG,
	WEBP,
	GIF,
	PSD,
	BMP,
	TIFF,
	HEIF,
};

class file_group
{
public:
	file_group(
		const std::u8string_view name,
		const std::u8string_view plural_name,
		const ui::color32 color,
		const icon_index icon,
		const file_traits traits,
		const group_key_type key,
		std::vector<std::u8string_view> sidecars)
		: name(name),
		  plural_name(plural_name),
		  color(color),
		  icon(icon),
		  traits(traits),
		  key(key),
		  sidecars(std::move(sidecars))
	{
	}

	std::u8string_view name;
	std::u8string_view plural_name;
	group_key_type key;
	ui::color32 color;
	icon_index icon = icon_index::document;
	file_traits traits = file_traits::none;
	std::vector<std::u8string_view> sidecars;
	std::vector<file_tool*> tools;

	mutable int id = -1;

	static file_group other;
	static file_group folder;
	static file_group photo;
	static file_group video;
	static file_group audio;
	static file_group archive;
	static file_group commodore;

	static constexpr size_t max_count = 7;

	std::u8string display_name(bool is_plural) const;

	uint32_t bloom_bit() const
	{
		return 1 << (id % 8);
	}

	bool has_trait(const file_traits t) const
	{
		return traits && t;
	}

	ui::color32 text_color(const ui::color32 default_text_color) const
	{
		//if (ft == file_type::folder) return lighten(render::style::color::important_background, 111);
		//if (ft == file_type::archive) return lighten(render::bgr(blue), 222);
		//return ::is_media(ft) ? render::style::color::view_text : darken(render::style::color::view_text, 66);
		return ui::average(default_text_color, color);
	}


	operator file_group_ref() const
	{
		return this;
	}
};


class file_type
{
public:
	file_group_ref group = nullptr;
	std::u8string_view extension;
	std::u8string_view text;
	file_traits traits = file_traits::none;
	icon_index icon = icon_index::document;
	std::vector<std::u8string_view> sidecars;
	std::vector<file_tool*> tools;

	static file_type folder;
	static file_type other;

	file_type(const file_group_ref group, const std::u8string_view extension, const std::u8string_view text,
	          const file_traits traits)
		: group(group),
		  extension(extension),
		  text(text),
		  traits(traits)
	{
	}

	bool has_trait(const file_traits t) const
	{
		return (traits && t) || group->has_trait(t);
	}

	bool is_media() const
	{
		return has_trait(file_traits::bitmap) || has_trait(file_traits::av);
	}

	bool can_cache() const
	{
		return has_trait(file_traits::cache_metadata);
	}

	bool is_playable() const
	{
		return has_trait(file_traits::av);
	}

	bool can_edit() const
	{
		return has_trait(file_traits::edit);
	}

	std::u8string display_name(const bool is_plural) const { return group->display_name(is_plural); }

	ui::color32 text_color(const ui::color32 default_text_color) const
	{
		return group->text_color(default_text_color);
	}

	ui::const_surface_ptr default_thumbnail() const;

	std::vector<file_tool*> all_tools() const
	{
		std::unordered_set<file_tool*> result;
		result.insert(tools.begin(), tools.end());
		result.insert(group->tools.begin(), group->tools.end());
		return {result.begin(), result.end()};
	}

	operator file_type_ref() const
	{
		return this;
	}
};

struct file_tool
{
	str::cached exe;
	str::cached invoke_text;
	str::cached text;
	str::cached extensions;
	df::file_path exe_path;

	bool invoke(df::file_path path);

	bool exists() const
	{
		return !exe_path.is_empty();
	}
};



file_group_ref file_group_from_index(int from_id);
file_group_ref parse_file_group(const std::u8string& text);
void load_file_types();
void load_tools();
std::vector<file_group_ref> all_file_groups();

inline std::u8string safe_file_type_name(const file_group_ref fg, const bool is_plural = false)
{
	return fg ? fg->display_name(is_plural) : std::u8string{};
}

inline std::u8string safe_file_type_name(const file_type_ref ft, const bool is_plural = false)
{
	return ft ? ft->group->display_name(is_plural) : std::u8string{};
}


//
//constexpr bool is_playable(file_type t)
//{
//	return t == file_type::video || t == file_type::audio;
//}
//
//constexpr bool is_photo(file_type t)
//{
//	return t == file_type::photo;
//}
//
//constexpr bool is_video(file_type t)
//{
//	return t == file_type::video;
//}
//
//constexpr bool is_media(file_type t)
//{
//	return t == file_type::photo || t == file_type::video || t == file_type::audio;
//}
//
//constexpr bool is_bitmap_hash_matchable(file_type t, const sizei dims)
//{
//	return t == file_type::photo && dims.cx >= 8 && dims.cy >= 8;
//}
//
//constexpr uint32_t media_type_to_bloom_bit(const file_type& mt)
//{
//	switch (mt)
//	{
//	case file_type::photo: return bloom_bits::photo;
//	case file_type::video: return bloom_bits::video;
//	case file_type::audio: return bloom_bits::audio;
//	}
//
//	return 0;
//}

//file_type parse_media_type(const std::u8string_view text);
//std::u8string_view format_file_type(file_type mt, bool plural = false);

struct metadata_parts
{
	df::blob xmp;
	df::blob exif;
	df::blob iptc;
	df::blob icc;
};

class file_scan_result
{
private:
	void parse_metadata_ffmpeg_kv(prop::item_metadata& result) const;
	void parse_metadata_moov(prop::item_metadata& result) const;
	void id3v2_metadata_id3v2(prop::item_metadata& result) const;
	void parse_metadata_id3v1(prop::item_metadata& result) const;

public:
	detected_format format = detected_format::Unknown;

	uint32_t width = 0;
	uint32_t height = 0;

	str::cached pixel_format;
	ui::orientation orientation = ui::orientation::top_left;

	mutable ui::const_image_ptr thumbnail_image;
	ui::surface_ptr thumbnail_surface;
	mutable ui::const_image_ptr cover_art;

	metadata_parts metadata;

	df::blob moov;
	df::blob id3v2;
	df::blob id3v1;

	metadata_kv_list ffmpeg_metadata;
	metadata_kv_list libraw_metadata;

	uint64_t exif_file_offset = 0;
	mutable uint32_t popm_offset = 0;

	df::date_t created_utc;
	int iso_speed = 0;
	float exposure_time = 0;
	float f_number = 0;
	float focal_length = 0;

	str::cached comment;
	str::cached artist;
	str::cached camera_model;
	str::cached camera_manufacturer;
	str::cached video_codec;
	str::cached copyright_notice;
	str::cached title;
	mutable std::vector<str::cached> keywords;

	uint32_t nb_streams = 0;
	double duration = 0.0;

	str::cached audio_codec;
	int audio_sample_rate = 0;
	int audio_channels = 0;
	prop::audio_sample_t audio_sample_type = prop::audio_sample_t::none;
	str::cached bitrate;
	int id3v2_version_major = 0;
	mutable gps_coordinate gps;
	uint32_t crc32c = 0;
	bool success = false;	

	sizei dimensions() const
	{
		return {static_cast<int>(width), static_cast<int>(height)};
	}

	metadata_parts save_metadata() const;
	prop::item_metadata_ptr to_props() const;
	av_media_info to_info() const;
};

struct file_load_result
{
	bool success = false;
	bool is_preview = false;

	ui::const_surface_ptr s;
	ui::const_image_ptr i;

	file_load_result() noexcept = default;
	file_load_result(const file_load_result&) = default;
	file_load_result& operator=(const file_load_result&) = default;	
	file_load_result(file_load_result&&) noexcept = default;
	file_load_result& operator=(file_load_result&&) noexcept = default;

	sizei dimensions() const;
	ui::const_surface_ptr to_surface(sizei scale_hint = {}, bool can_use_yuv = false) const;
	ui::pixel_difference_result calc_pixel_difference(const file_load_result& other) const;

	void clear()
	{
		s.reset();
		i.reset();
		success = false;
	}

	bool is_empty() const
	{
		return ui::is_empty(s) && ui::is_empty(i);
	}

	ui::orientation orientation() const
	{
		if (success)
		{
			if (is_valid(i)) return i->orientation();
			if (is_valid(s)) return s->orientation();
		}
		return {};
	}

	bool is_jpeg() const
	{
		return success && ui::is_jpeg(i);
	}
};


struct png_parts
{
	uint32_t width = 0;
	uint32_t height = 0;
	df::blob idat;
	df::blob plte;

	metadata_parts metadata;
};

struct webp_parts
{
	int width = 0;
	int height = 0;

	metadata_parts metadata;
	std::vector<ui::surface_ptr> frames;
};

struct file_encode_params
{
	int jpeg_save_quality = 85;
	bool webp_lossless = false;
	int webp_quality = 70;
	bool webp_lossy_alpha = false;
};

file_scan_result scan_png(read_stream& s);
file_scan_result scan_xmp(df::file_path path);
file_scan_result scan_jpg(read_stream& s);
file_scan_result scan_psd(read_stream& s);
file_scan_result scan_heif(read_stream& s);
webp_parts scan_webp(df::cspan data, bool decode_surface);

png_parts split_png(read_stream& s);
media_name_props scan_info_from_title(std::u8string_view name);

ui::surface_ptr load_psd(read_stream& s);
file_load_result load_raw(df::file_path path, bool can_load_preview);
ui::surface_ptr load_png(df::cspan data);
ui::surface_ptr load_webp(df::cspan data);
ui::surface_ptr load_heif(read_stream& s);

ui::image_ptr save_png(const ui::const_surface_ptr& surface_in, const metadata_parts& metadata);
ui::image_ptr save_webp(const ui::const_surface_ptr& surface_in, const metadata_parts& metadata,
                        const file_encode_params& params);
ui::image_ptr save_jpeg(const ui::const_surface_ptr& surface_in, const metadata_parts& metadata,
                        const file_encode_params& encoder_params);
ui::image_ptr save_surface(const ui::image_format& format, const ui::const_surface_ptr& surface,
                           const metadata_parts& metadata, const file_encode_params& params);
ui::image_format extension_to_format(std::u8string_view ext);

struct pack128
{
	uint32_t u1;
	uint32_t u2;
	uint32_t u3;
	uint32_t u4;

	operator df::cspan() const
	{
		return {std::bit_cast<const uint8_t*>(this), 16};
	}
};

class read_stream
{
protected:
	uint64_t _file_size = 0;

public:
	virtual ~read_stream() = default;

	virtual uint8_t peek8(uint64_t pos) = 0;
	virtual uint16_t peek16(uint64_t pos) = 0;
	virtual uint32_t peek32(uint64_t pos) = 0;
	virtual uint64_t peek64(uint64_t pos) = 0;
	virtual pack128 peek128(uint64_t pos) = 0;

	virtual df::blob read_all() = 0;
	virtual df::blob read(uint64_t pos, size_t len) = 0;
	virtual void read(uint64_t pos, uint8_t* buffer, size_t len) = 0;

	const uint64_t size() const
	{
		return _file_size;
	}
};


class mem_read_stream : public read_stream
{
	const uint8_t* const _data = nullptr;

public:
	mem_read_stream(df::cspan cs) : _data(cs.data)
	{
		_file_size = cs.size;
	}

	template <typename T>
	T peek(uint64_t pos)
	{
		if (pos + sizeof(T) > _file_size) throw app_exception(u8"invalid peek"s);
		return *std::bit_cast<const T*>(_data + pos);
	}

	uint8_t peek8(uint64_t pos) override
	{
		return peek<uint8_t>(pos);
	}

	uint16_t peek16(uint64_t pos) override
	{
		return peek<uint16_t>(pos);
	}

	uint32_t peek32(uint64_t pos) override
	{
		return peek<uint32_t>(pos);
	}

	uint64_t peek64(uint64_t pos) override
	{
		return peek<uint64_t>(pos);
	}

	pack128 peek128(uint64_t pos) override
	{
		return peek<pack128>(pos);
	}

	void read(uint64_t pos, uint8_t* buffer, size_t len) override
	{
		if (pos + len > _file_size) throw app_exception(u8"invalid read"s);
		memcpy(buffer, _data + pos, len);
	}

	df::blob read(uint64_t pos, size_t len) override
	{
		if (pos + len > _file_size) throw app_exception(u8"invalid read"s);
		df::blob result;
		result.resize(len);
		memcpy(result.data(), _data + pos, len);
		return result;
	}

	df::blob read_all() override
	{
		df::blob result;
		result.resize(_file_size);
		memcpy(result.data(), _data, _file_size);
		return result;
	}
};

class file_read_stream : public read_stream
{
private:
	platform::file_ptr _h;
	uint8_t* _buffer = nullptr;
	size_t _loaded_start_pos = 0;
	size_t _loaded_end_pos = 0;
	size_t _buffer_size = 0;
	size_t _block_size = 0;

	void load_buffer(uint64_t pos, size_t len);

public:
	bool open(df::file_path path);
	bool open(platform::file_ptr h);
	void close();

	~file_read_stream() override;

	template <typename T>
	T peek(uint64_t pos)
	{
		load_buffer(pos, sizeof(T));
		return *std::bit_cast<const T*>(_buffer + pos - _loaded_start_pos);
	}

	uint8_t peek8(uint64_t pos) override
	{
		return peek<uint8_t>(pos);
	}

	uint16_t peek16(uint64_t pos) override
	{
		return peek<uint16_t>(pos);
	}

	uint32_t peek32(uint64_t pos) override
	{
		return peek<uint32_t>(pos);
	}

	uint64_t peek64(uint64_t pos) override
	{
		return peek<uint64_t>(pos);
	}

	pack128 peek128(uint64_t pos) override
	{
		return peek<pack128>(pos);
	}

	void read(uint64_t pos, uint8_t* buffer, size_t len) override;

	df::blob read(uint64_t pos, size_t len) override
	{
		df::blob result;
		result.resize(len);
		read(pos, result.data(), len);
		return result;
	}

	df::blob read_all() override
	{
		df::blob result;
		result.resize(_file_size);
		read(0, result.data(), _file_size);
		return result;
	}
};

struct codec_info : public df::no_copy
{
	bool item_only = false;
	std::u8string title;
	std::u8string key;
	std::u8string extension_default;
};

namespace photo_edits_default
{
	const double Brightness = 0.0;
	const double Contrast = 0.0;
	const double Darks = 0.0;
	const double Vibrance = 0.0;
	const double Lights = 0.0;
	const double Midtones = 0.0;
	const double Rotation = 0.0;
	const double Saturation = 0.0;
	const double Straighten = 0.0;
};

class image_edits
{
	quadd _crop;
	sizei _scale;

	double _brightness;
	double _contrast;
	double _darks;
	double _vibrance;
	double _lights;
	double _midtones;
	double _saturation;

public:
	image_edits() : _scale(0, 0),
	                _brightness(photo_edits_default::Brightness),
	                _contrast(photo_edits_default::Contrast),
	                _darks(photo_edits_default::Darks),
	                _vibrance(photo_edits_default::Vibrance),
	                _lights(photo_edits_default::Lights),
	                _midtones(photo_edits_default::Midtones),
	                _saturation(photo_edits_default::Saturation)
	{
	}

	image_edits(int s) : _scale(s, s),
	                     _brightness(photo_edits_default::Brightness),
	                     _contrast(photo_edits_default::Contrast),
	                     _darks(photo_edits_default::Darks),
	                     _vibrance(photo_edits_default::Vibrance),
	                     _lights(photo_edits_default::Lights),
	                     _midtones(photo_edits_default::Midtones),
	                     _saturation(photo_edits_default::Saturation)
	{
	}

	image_edits(const sizei s) : _scale(s),
	                             _brightness(photo_edits_default::Brightness),
	                             _contrast(photo_edits_default::Contrast),
	                             _darks(photo_edits_default::Darks),
	                             _vibrance(photo_edits_default::Vibrance),
	                             _lights(photo_edits_default::Lights),
	                             _midtones(photo_edits_default::Midtones),
	                             _saturation(photo_edits_default::Saturation)
	{
	}

	image_edits(const image_edits& other) : _crop(other._crop),
	                                        _scale(other._scale),
	                                        _brightness(other._brightness),
	                                        _contrast(other._contrast),
	                                        _darks(other._darks),
	                                        _vibrance(other._vibrance),
	                                        _lights(other._lights),
	                                        _midtones(other._midtones),
	                                        _saturation(other._saturation)
	{
	}

	image_edits& operator=(const image_edits& other)
	{
		_scale = other._scale;
		_crop = other._crop;
		_brightness = other._brightness;
		_contrast = other._contrast;
		_darks = other._darks;
		_vibrance = other._vibrance;
		_lights = other._lights;
		_midtones = other._midtones;
		_saturation = other._saturation;
		return *this;
	}

	friend bool operator==(const image_edits& lhs, const image_edits& rhs)
	{
		return lhs._crop == rhs._crop
			&& lhs._scale == rhs._scale
			&& lhs._brightness == rhs._brightness
			&& lhs._contrast == rhs._contrast
			&& lhs._darks == rhs._darks
			&& lhs._vibrance == rhs._vibrance
			&& lhs._lights == rhs._lights
			&& lhs._midtones == rhs._midtones
			&& lhs._saturation == rhs._saturation;
	}

	friend bool operator!=(const image_edits& lhs, const image_edits& rhs)
	{
		return !(lhs == rhs);
	}

	void clear()
	{
		_crop.clear();
		_scale.cx = 0;
		_scale.cy = 0;
		_brightness = photo_edits_default::Brightness;
		_contrast = photo_edits_default::Contrast;
		_darks = photo_edits_default::Darks;
		_vibrance = photo_edits_default::Vibrance;
		_lights = photo_edits_default::Lights;
		_midtones = photo_edits_default::Midtones;
		_saturation = photo_edits_default::Saturation;
	}

	bool has_scale() const
	{
		return _scale.cx != 0 && _scale.cy != 0;
	}

	void scale(const sizei s)
	{
		_scale = s;
	}

	void scale(int s)
	{
		_scale.cx = s;
		_scale.cy = s;
	}

	sizei scale() const
	{
		return _scale;
	}

	const quadd& crop_bounds() const
	{
		return _crop;
	}

	const quadd crop_bounds(const sizei size) const
	{
		if (_crop.is_empty()) return quadd(size);
		return _crop;
	}

	void crop_bounds(const quadd& v)
	{
		_crop = v;
	}

	double contrast() const
	{
		return _contrast;
	}

	void contrast(const double v)
	{
		_contrast = v;
	}

	double brightness() const
	{
		return _brightness;
	}

	void brightness(const double v)
	{
		_brightness = v;
	}

	double saturation() const
	{
		return _saturation;
	}

	void saturation(const double v)
	{
		_saturation = v;
	}

	double vibrance() const
	{
		return _vibrance;
	}

	void vibrance(const double v)
	{
		_vibrance = v;
	}

	double darks() const
	{
		return _darks;
	}

	void darks(const double v)
	{
		_darks = v;
	}

	double lights() const
	{
		return _lights;
	}

	void lights(const double v)
	{
		_lights = v;
	}

	double midtones() const
	{
		return _midtones;
	}

	void midtones(const double v)
	{
		_midtones = v;
	}

	bool has_color_changes() const
	{
		return _brightness != photo_edits_default::Brightness || _contrast != photo_edits_default::Contrast ||
			_saturation != photo_edits_default::Saturation || _vibrance != photo_edits_default::Vibrance ||
			_darks != photo_edits_default::Darks || _midtones != photo_edits_default::Midtones ||
			_lights != photo_edits_default::Lights;
	}

	bool has_changes(sizei image_extent) const;
	bool has_crop(sizei image_extent) const;
	bool is_no_loss(sizei image_extent) const;

	bool has_rotation() const
	{
		return rotation_angle() != 0.0;
	};

	double rotation_angle() const;

	sizei result_extent() const
	{
		return has_scale() ? scale() : sizei(df::round(_crop[0].dist(_crop[1])), df::round(_crop[0].dist(_crop[3])));
	}
};

class metadata_edits
{
public:
	std::optional<std::u8string> title;
	std::optional<std::u8string> copyright_notice;
	std::optional<std::u8string> copyright_creator;
	std::optional<std::u8string> copyright_source;
	std::optional<std::u8string> copyright_credit;
	std::optional<std::u8string> copyright_url;
	std::optional<std::u8string> description;
	std::optional<std::u8string> comment;
	std::optional<std::u8string> artist;
	std::optional<std::u8string> album;
	std::optional<std::u8string> album_artist;
	std::optional<std::u8string> genre;
	std::optional<std::u8string> show;
	std::optional<df::date_t> created;

	std::optional<int> season;
	std::optional<df::xy8> episode;
	std::optional<int> year;
	std::optional<int> rating;
	std::optional<std::u8string> label;
	std::optional<ui::orientation> orientation;

	std::optional<df::xy8> track_num;
	std::optional<df::xy8> disk_num;

	tag_set add_tags;
	tag_set remove_tags;

	bool remove_rating = false;

	std::optional<gps_coordinate> location_coordinate;
	std::optional<std::u8string> location_place;
	std::optional<std::u8string> location_state;
	std::optional<std::u8string> location_country;

public:
	metadata_edits() noexcept = default;
	~metadata_edits() = default;
	metadata_edits(const metadata_edits&) = default;
	metadata_edits& operator=(const metadata_edits&) = default;
	metadata_edits(metadata_edits&&) noexcept = default;
	metadata_edits& operator=(metadata_edits&&) noexcept = default;

	void apply(SXMPMeta& meta) const;

	bool has_changes() const
	{
		return
			title.has_value() ||
			copyright_notice.has_value() ||
			copyright_creator.has_value() ||
			copyright_source.has_value() ||
			copyright_credit.has_value() ||
			copyright_url.has_value() ||
			description.has_value() ||
			comment.has_value() ||
			artist.has_value() ||
			album.has_value() ||
			album_artist.has_value() ||
			genre.has_value() ||
			show.has_value() ||
			created.has_value() ||
			season.has_value() ||
			episode.has_value() ||
			year.has_value() ||
			rating.has_value() ||
			label.has_value() ||
			track_num.has_value() ||
			disk_num.has_value() ||
			location_coordinate.has_value() ||
			location_place.has_value() ||
			location_state.has_value() ||
			location_country.has_value() ||
			orientation.has_value() ||
			!add_tags.is_empty() ||
			!remove_tags.is_empty() ||
			remove_rating;
	}

	friend class files;
};

struct archive_item
{
	std::u8string filename;
	df::file_size uncompressed_size;
	df::file_size compressed_size;
	df::date_t created;
};

inline bool is_image_format(const detected_format& format)
{
	return format == detected_format::JPEG ||
		format == detected_format::PNG ||
		format == detected_format::WEBP;
}


ui::image_ptr load_image_file(df::cspan data);
file_scan_result scan_photo(read_stream& s);


class files : private df::no_copy
{
private:
	jpeg_decoder_x _jpeg_decoder;
	jpeg_encoder _jpeg_encoder;

	std::unique_ptr<av_scaler> _scaler{};

	mutable platform::mutex _mutex;

	file_scan_result scan_raw(df::file_path path, std::u8string_view xmp_sidecar, bool load_thumb, sizei max);

public:
	files();
	~files();

	ui::const_image_ptr surface_to_image(const ui::const_surface_ptr& surface_in, const metadata_parts& metadata,
	                                     const file_encode_params& params, ui::image_format format);
	ui::surface_ptr image_to_surface(const ui::const_image_ptr& image, sizei scale_hint = {}, bool can_use_yuv = false);
	ui::surface_ptr image_to_surface(df::cspan data, sizei scale_hint = {}, bool can_use_yuv = false);
	ui::surface_ptr scale_if_needed(ui::surface_ptr surface_in, sizei target_extent);
	ui::const_surface_ptr scale_if_needed(ui::const_surface_ptr surface_in, sizei target_extent);
	ui::pixel_difference_result pixel_difference(const ui::const_image_ptr& expected, const ui::const_image_ptr& actual);

	bool save(df::file_path path, const file_load_result& loaded);
	

	static std::u8string_view to_string(ui::image_format f)
	{
		switch (f)
		{
		case ui::image_format::JPEG: return u8"JPEG"sv;
		case ui::image_format::PNG: return u8"PNG"sv;
		case ui::image_format::WEBP: return u8"WEBP"sv;
		case ui::image_format::Unknown: break;
		default: ;
		}

		return u8"Unknown"sv;
	}

	static detected_format detect_format(df::cspan image_buffer_in);
	static file_type_ref file_type_from_name(df::file_path path);
	static file_type_ref file_type_from_name(std::u8string_view name);

	static bool can_save(df::file_path path);
	static bool can_save_extension(std::u8string_view ext);
	static bool is_jpeg(df::file_path path);
	static bool is_jpeg(std::u8string_view name);
	static bool is_raw(df::file_path path);
	static bool is_raw(std::u8string_view name);
	static bool is_jpeg(uint32_t header);

	file_scan_result scan_file(df::file_path path, bool load_thumb, file_type_ref ft,
	                           std::u8string_view xmp_sidecar = {}, sizei max_thumb_size = {});

	file_load_result load(df::file_path path, bool can_load_preview);

	platform::file_op_result update(df::file_path path_src, df::file_path path_dst,
	                                const metadata_edits& metadata_edits, const image_edits& photo_edits,
	                                const file_encode_params& params, bool create_original,
	                                std::u8string_view xmp_name);

	platform::file_op_result update(const df::file_path path, const metadata_edits& metadata_edits,
	                                const image_edits& photo_edits, const file_encode_params& params,
	                                const bool create_original, const std::u8string_view xmp_name)
	{
		return update(path, path, metadata_edits, photo_edits, params, create_original, xmp_name);
	}

	static std::vector<archive_item> list_archive(df::file_path zip_file_path);

	struct d64_item
	{
		std::u8string line;
	};

	static std::vector<d64_item> list_disk(const df::blob& selected_item_data);
};
