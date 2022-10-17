// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "pch.h"

#include "model_propery.h"
#include "model_location.h"
#include "metadata_xmp.h"
#include "files.h"


#define TXMP_STRING_TYPE std::string
#define WIN_ENV 1
#define XMP_INCLUDE_XMPFILES 1
#define XML_STATIC
#define XMP_StaticBuild 1

#include "XMP.hpp"
// ReSharper disable CppUnusedIncludeDirective
#include "metadata_exif.h"
#include "XMP.incl_cpp" // Needed otherwise undefined externs

static df::date_t xmp_parse_date(const std::u8string_view str)
{
	if (!str.empty())
	{
		df::date_t ft;
		ft.parse_xml_date(str);

		if (ft.is_valid())
		{
			return ft;
		}
	}

	return {};
}

static double xmp_decode_gps_coordinate(const std::u8string_view str)
{
	auto len = str.size();
	const auto* const sz = std::bit_cast<const char*>(str.data());

	if (len < 4) return 0;

	// DDD,MM,SSk
	// DDD,MM.mmk

	// Key:
	// DDD = number of degrees
	// MM = number of minutes
	// SS = number of seconds
	// mm = fraction of minutes
	// k = {N/S/E/W)

	int degrees = 0;
	int mins = 0;
	int seconds = 0;

	const auto last = sz[len - 1];
	const auto neg = last == 'S' || last == 'W' || last == 's' || last == 'w';

	len -= 1;

	if (3 == _snscanf_s(sz, len, "%d,%d,%d", &degrees, &mins, &seconds))
	{
		const auto coordinate = gps_coordinate::dms_to_decimal(degrees, mins, seconds);
		return neg ? -coordinate : coordinate;
	}

	float degrees2 = 0;
	float mins2 = 0;

	if (2 == _snscanf_s(sz, len, "%f,%f", &degrees2, &mins2))
	{
		const auto coordinate = gps_coordinate::dms_to_decimal(degrees2, mins2, 0.0);
		return neg ? -coordinate : coordinate;
	}

	return 0;
}

static bool xmp_decode_rational(const std::u8string_view text, metadata_exif::urational32_t& result)
{
	const auto len = text.size();
	unsigned long locNum = 0, locDenom = 0;
	char8_t nextChar = 0; // Used to make sure sscanf consumes all of the string.
	const auto* const sz = std::bit_cast<const char*>(text.data());

	const int items = _snscanf_s(sz, len, "%lu/%lu%c", &locNum, &locDenom, &nextChar);
	// AUDIT: This is safe, check the calls.

	if (items != 2)
	{
		if (items != 1) return false;
		locDenom = 1; // The XMP was just an integer, assume a denominator of 1.
	}

	result.numerator = locNum;
	result.denominator = locDenom;
	return true;
}

std::u8string microsoft_photo_prefix;

static str::cached xmp_load_array(const SXMPMeta& xmp, const char* schema_ns, const char* array_name)
{
	str::cached result;
	const auto count = xmp.CountArrayItems(schema_ns, array_name);

	if (count > 0)
	{
		std::vector<std::u8string> parts;
		parts.reserve(count);

		for (auto i = 0; i < count; i++)
		{
			std::string str;

			if (xmp.GetArrayItem(schema_ns, array_name, i + 1, &str, nullptr))
			{
				parts.emplace_back(str::utf8_cast(str));
			}
		}

		if (!parts.empty())
		{
			result = str::cache(str::combine(parts));
		}
	}

	return result;
}

static void parse_xmp(const SXMPMeta& xmp, prop::item_metadata& md)
{
	XMP_OptionBits flags = 0;
	std::string utf8;

	if (xmp.GetProperty(kXMP_NS_Photoshop, "DateCreated", &utf8, &flags))
	{
		const auto d = xmp_parse_date(str::utf8_cast(utf8));
		if (d.is_valid()) md.created_exif = d;
	}

	if (xmp.GetProperty(kXMP_NS_XMP, "CreateDate", &utf8, &flags))
	{
		const auto d = xmp_parse_date(str::utf8_cast(utf8));

		if (d.is_valid())
		{
			md.created_digitized = d;
		}
		else if (str::is_num(str::utf8_cast(utf8)))
		{
			md.year = str::to_int(str::utf8_cast(utf8));
		}
	}

	if (xmp.GetProperty(kXMP_NS_EXIF, "DateTimeDigitized", &utf8, &flags))
	{
		const auto d = xmp_parse_date(str::utf8_cast(utf8));
		if (d.is_valid()) md.created_digitized = d;
	}

	if (xmp.GetProperty(kXMP_NS_EXIF, "DateTimeOriginal", &utf8, &flags))
	{
		const auto d = xmp_parse_date(str::utf8_cast(utf8));
		if (d.is_valid()) md.created_exif = d;
	}

	if (xmp.GetProperty(kXMP_NS_EXIF, "GPSLatitude", &utf8, &flags))
	{
		md.coordinate.latitude(xmp_decode_gps_coordinate(str::utf8_cast(utf8)));
	}

	if (xmp.GetProperty(kXMP_NS_EXIF, "GPSLongitude", &utf8, &flags))
	{
		md.coordinate.longitude(xmp_decode_gps_coordinate(str::utf8_cast(utf8)));
	}

	if (xmp.GetProperty(kXMP_NS_Photoshop, "City", &utf8, &flags))
	{
		md.location_place = str::strip_and_cache(utf8);
	}

	if (xmp.GetProperty(kXMP_NS_Photoshop, "State", &utf8, &flags))
	{
		md.location_state = str::strip_and_cache(utf8);
	}

	if (xmp.GetProperty(kXMP_NS_Photoshop, "Country", &utf8, &flags))
	{
		md.location_country = str::strip_and_cache(utf8);
	}

	if (xmp.GetProperty(kXMP_NS_Photoshop, "Credit", &utf8, &flags))
	{
		md.copyright_credit = str::strip_and_cache(utf8);
	}

	if (xmp.GetProperty(kXMP_NS_XMP_Rights, "WebStatement", &utf8, &flags))
	{
		md.copyright_url = str::strip_and_cache(utf8);
	}

	if (xmp.GetProperty(kXMP_NS_Photoshop, "Source", &utf8, &flags))
	{
		md.copyright_source = str::strip_and_cache(utf8);
	}

	md.copyright_creator = xmp_load_array(xmp, kXMP_NS_DC, "creator");

	if (xmp.GetLocalizedText(kXMP_NS_DC, "rights", "", "x-default", nullptr, &utf8, &flags))
	{
		md.copyright_notice = str::strip_and_cache(utf8);
	}

	if (xmp.GetLocalizedText(kXMP_NS_DC, "title", "", "x-default", nullptr, &utf8, &flags))
	{
		md.title = str::strip_and_cache(utf8);
	}
	
	if (xmp.GetLocalizedText(kXMP_NS_EXIF, "UserComment", "", "x-default", nullptr, &utf8, &flags))
	{
		md.comment = str::strip_and_cache(utf8);
	}

	if (xmp.GetProperty(kXMP_NS_DM, "logComment", &utf8, &flags))
	{
		md.comment = str::strip_and_cache(utf8);
	}

	if (xmp.GetLocalizedText(kXMP_NS_DC, "description", "", "x-default", nullptr, &utf8, &flags))
	{
		md.description = str::strip_and_cache(utf8);
	}
	
	if (xmp.GetProperty(kXMP_NS_XMP, "Rating", &utf8, &flags))
	{
		md.rating = str::to_int(str::utf8_cast(utf8));
	}
	else if (xmp.GetProperty(kXMP_NS_MicrosoftPhoto, "Rating", &utf8, &flags))
	{
		//MicrosoftPhoto:Rating
		md.rating = df::round_up(str::to_int(str::utf8_cast(utf8)), 20);
	}

	if (xmp.GetProperty(kXMP_NS_XMP, "Label", &utf8, &flags))
	{
		md.label = str::strip_and_cache(utf8);
	}

	md.tags = xmp_load_array(xmp, kXMP_NS_DC, "subject");

	if (xmp.GetProperty(kXMP_NS_TIFF, "Make", &utf8, &flags))
	{
		md.camera_manufacturer = str::strip_and_cache(utf8);
	}

	if (xmp.GetProperty(kXMP_NS_TIFF, "Model", &utf8, &flags))
	{
		md.camera_model = str::strip_and_cache(utf8);
	}

	if (xmp.GetProperty(kXMP_NS_DM, "album", &utf8, &flags))
	{
		md.album = str::strip_and_cache(utf8);
	}

	if (xmp.GetProperty(kXMP_NS_DM, "albumArtist", &utf8, &flags))
	{
		md.album_artist = str::strip_and_cache(utf8);
	}

	if (xmp.GetProperty(kXMP_NS_DM, "artist", &utf8, &flags))
	{
		md.artist = str::strip_and_cache(utf8);
	}

	if (xmp.GetProperty(kXMP_NS_DM, "genre", &utf8, &flags))
	{
		md.genre = str::strip_and_cache(utf8);
	}

	if (xmp.GetProperty(kXMP_NS_DM, "show", &utf8, &flags))
	{
		md.show = str::strip_and_cache(utf8);
	}

	if (xmp.GetProperty(kXMP_NS_DM, "trackNumber", &utf8, &flags))
	{
		md.track = df::xy8::parse(str::utf8_cast(utf8));
	}

	if (xmp.GetProperty(kXMP_NS_DM, "episode", &utf8, &flags))
	{
		md.episode = df::xy8::parse(str::utf8_cast(utf8));
	}

	if (xmp.GetProperty(kXMP_NS_DM, "season", &utf8, &flags))
	{
		md.season = str::to_int(str::utf8_cast(utf8));
	}

	// <exif:FNumber>33/10</exif:FNumber>
	// <exif:FocalLength>600/10</exif:FocalLength>
	// <exif:ExposureTime>10/100</exif:ExposureTime>

	if (xmp.GetProperty(kXMP_NS_EXIF, "FocalLengthIn35mmFilm", &utf8, &flags))
	{
		metadata_exif::urational32_t r;

		if (xmp_decode_rational(str::utf8_cast(utf8), r))
		{
			md.focal_length_35mm_equivalent = r.round();
		}
	}

	if (xmp.GetProperty(kXMP_NS_EXIF, "FNumber", &utf8, &flags))
	{
		metadata_exif::urational32_t r;

		if (xmp_decode_rational(str::utf8_cast(utf8), r))
		{
			md.f_number = r.to_real();
		}
	}

	if (xmp.GetProperty(kXMP_NS_EXIF, "FocalLength", &utf8, &flags))
	{
		metadata_exif::urational32_t r;

		if (xmp_decode_rational(str::utf8_cast(utf8), r))
		{
			md.focal_length = r.to_real();
		}
	}

	if (xmp.GetProperty(kXMP_NS_EXIF, "ExposureTime", &utf8, &flags))
	{
		metadata_exif::urational32_t r;

		if (xmp_decode_rational(str::utf8_cast(utf8), r))
		{
			md.exposure_time = r.to_real();
		}
	}

	if (xmp.GetProperty(kXMP_NS_TIFF, "Orientation", &utf8, &flags))
	{
		md.orientation = static_cast<ui::orientation>(str::to_int(str::utf8_cast(utf8)));
	}

	if (xmp.GetProperty(kXMP_NS_EXIF_Aux, "Lens", &utf8, &flags))
	{
		md.lens = str::strip_and_cache(utf8);
	}

	if (xmp.GetProperty(kXMP_NS_CameraRaw, "RawFileName", &utf8, &flags))
	{
		md.raw_file_name = str::strip_and_cache(utf8);
	}
}


void metadata_xmp::initialise()
{
	SXMPMeta::Initialize();
	SXMPFiles::Initialize(0UL);

	// https://github.com/nomacs/nomacs/blob/master/exiv2-0.25/src/xmp.cpp	
	//SXMPMeta::RegisterNamespace(kXMP_NS_MicrosoftPhoto, "MicrosoftPhoto"sv, &microsoft_photo_prefix);
}

void metadata_xmp::term()
{
	SXMPFiles::Terminate();
	SXMPMeta::Terminate();
}

void metadata_edits::apply(SXMPMeta& meta) const
{
	if (location_coordinate.has_value())
	{
		const auto position = location_coordinate.value();

		//std::u8string v;
		XMP_OptionBits flags = 0;
		meta.SetProperty(kXMP_NS_EXIF, "GPSLatitude",
		                 str::utf8_cast2(gps_coordinate::decimal_to_dms_str(position.latitude(), true)));
		meta.SetProperty(kXMP_NS_EXIF, "GPSLongitude",
		                 str::utf8_cast2(gps_coordinate::decimal_to_dms_str(position.longitude(), false)));

		// meta.SetProperty_Float(kXMP_NS_EXIF, "GPSLatitude"sv, position.Latitude());
		// meta.SetProperty_Float(kXMP_NS_EXIF, "GPSLongitude"sv, position.Longitude());
	}


	if (location_place.has_value())
	{
		if (!str::is_empty(location_place.value()))
		{
			meta.SetProperty(kXMP_NS_Photoshop, "City", str::utf8_cast2(location_place.value()));
		}
		else
		{
			meta.DeleteProperty(kXMP_NS_Photoshop, "City");
		}
	}

	if (location_state.has_value())
	{
		if (!str::is_empty(location_state.value()))
		{
			meta.SetProperty(kXMP_NS_Photoshop, "State", str::utf8_cast2(location_state.value()));
		}
		else
		{
			meta.DeleteProperty(kXMP_NS_Photoshop, "State");
		}
	}

	if (location_country.has_value())
	{
		if (!str::is_empty(location_country.value()))
		{
			meta.SetProperty(kXMP_NS_Photoshop, "Country", str::utf8_cast2(location_country.value()));
		}
		else
		{
			meta.DeleteProperty(kXMP_NS_Photoshop, "Country");
		}
	}

	if (copyright_credit.has_value())
	{
		meta.SetProperty(kXMP_NS_Photoshop, "Credit", str::utf8_cast2(copyright_credit.value()));
	}

	if (copyright_url.has_value())
	{
		meta.SetProperty(kXMP_NS_XMP_Rights, "WebStatement", str::utf8_cast2(copyright_credit.value()));
	}

	if (copyright_source.has_value())
	{
		meta.SetProperty(kXMP_NS_Photoshop, "Source", str::utf8_cast2(copyright_source.value()));
	}

	if (copyright_notice.has_value())
	{
		meta.SetLocalizedText(kXMP_NS_DC, "rights", "", "x-default", str::utf8_cast2(copyright_notice.value()));
	}

	if (copyright_creator.has_value())
	{
		meta.SetLocalizedText(kXMP_NS_DC, "creator", "", "x-default", str::utf8_cast2(copyright_creator.value()));
	}

	if (title.has_value())
	{
		meta.SetLocalizedText(kXMP_NS_DC, "title", "", "x-default", str::utf8_cast2(title.value()));
	}

	if (description.has_value())
	{
		meta.SetLocalizedText(kXMP_NS_DC, "description", "", "x-default", str::utf8_cast2(description.value()));
	}

	if (comment.has_value())
	{
		meta.SetProperty(kXMP_NS_DM, "logComment", str::utf8_cast2(comment.value()));
	}

	if (rating.has_value())
	{
		const int r = std::clamp(rating.value(), -1, 5);
		meta.SetProperty_Int(kXMP_NS_XMP, "Rating", r);
		meta.SetProperty_Int(kXMP_NS_MicrosoftPhoto, "Rating", std::clamp(r * 20, 0, 99));
	}

	if (label.has_value())
	{
		if (!str::is_empty(label.value()))
		{
			meta.SetProperty(kXMP_NS_XMP, "Label", str::utf8_cast2(label.value()));
		}
		else
		{
			meta.DeleteProperty(kXMP_NS_XMP, "Label");
		}
	}

	if (album.has_value())
	{
		meta.SetProperty(kXMP_NS_DM, "album", str::utf8_cast2(album.value()));
	}

	if (album_artist.has_value())
	{
		meta.SetProperty(kXMP_NS_DM, "albumArtist", str::utf8_cast2(album_artist.value()));
	}

	if (genre.has_value())
	{
		meta.SetProperty(kXMP_NS_DM, "genre", str::utf8_cast2(genre.value()));
	}

	if (show.has_value())
	{
		meta.SetProperty(kXMP_NS_DM, "show", str::utf8_cast2(show.value()));
	}

	if (year.has_value())
	{
		meta.SetProperty(kXMP_NS_XMP, "CreateDate", str::utf8_cast2(str::to_string(year.value())));
	}

	if (created.has_value())
	{
		meta.SetProperty(kXMP_NS_Photoshop, "DateCreated", str::utf8_cast2(created.value().to_xmp_date()));
	}

	if (episode.has_value())
	{
		meta.SetProperty(kXMP_NS_DM, "episode", str::utf8_cast2(episode.value().str()));
	}

	if (season.has_value())
	{
		meta.SetProperty(kXMP_NS_DM, "season", str::utf8_cast2(str::to_string(season.value())));
	}

	if (track_num.has_value())
	{
		meta.SetProperty(kXMP_NS_DM, "trackNumber", str::utf8_cast2(track_num.value().str()));
	}

	if (disk_num.has_value())
	{
		meta.SetProperty(kXMP_NS_DM, "discNumber", str::utf8_cast2(disk_num.value().str()));
	}

	if (artist.has_value())
	{
		meta.SetProperty(kXMP_NS_DM, "artist", str::utf8_cast2(artist.value()));
	}

	if (orientation.has_value())
	{
		meta.SetProperty(kXMP_NS_TIFF, "Orientation",
		                 str::utf8_cast2(str::to_string(static_cast<int>(orientation.value()))), kXMP_DeleteExisting);
	}

	if (!remove_tags.is_empty() || !add_tags.is_empty())
	{
		tag_set tags;
		const auto count = meta.CountArrayItems(kXMP_NS_DC, "subject");

		for (auto i = 0; i < count; i++)
		{
			std::string str;
			if (meta.GetArrayItem(kXMP_NS_DC, "subject", i + 1, &str, nullptr))
			{
				tags.add_one(str::cache(str));
			}
		}

		/*while(meta.CountArrayItems(kXMP_NS_DC, "subject"sv) > 0)
		{
			meta.DeleteArrayItem(kXMP_NS_DC, "subject"sv, 1);
		}*/

		tags.remove(remove_tags);
		tags.add(add_tags);
		tags.make_unique();

		meta.DeleteProperty(kXMP_NS_DC, "subject");

		for (auto i = 0; i < tags.size(); i++)
		{
			meta.AppendArrayItem(kXMP_NS_DC, "subject", kXMP_PropValueIsArray, str::utf8_cast2(tags[i]));
		}
	}

	if (remove_rating)
	{
		meta.DeleteProperty(kXMP_NS_XMP, "Rating");
		meta.DeleteProperty(kXMP_NS_MicrosoftPhoto, "Rating");
	}
}

void metadata_xmp::parse(prop::item_metadata& pd, df::cspan xmp)
{
	try
	{
		const auto xmp_sig_len = xmp_signature.size() + 1;
		const auto size = xmp.size;
		const auto* const data = xmp.data;

		if (size > xmp_sig_len)
		{
			SXMPMeta meta;

			if (memcmp(data, xmp_signature.data(), xmp_sig_len) == 0)
			{
				meta.ParseFromBuffer(std::bit_cast<const char*>(data + xmp_sig_len), size - xmp_sig_len);
			}
			else
			{
				meta.ParseFromBuffer(std::bit_cast<const char*>(data), size);
			}

			parse_xmp(meta, pd);
		}
	}
	catch (std::exception& e)
	{
		df::log(__FUNCTION__, e.what());
	}
	catch (const XMP_Error& e)
	{
		df::log(__FUNCTION__, e.GetErrMsg());
	}
}


void metadata_xmp::parse(prop::item_metadata& pd, const df::file_path path)
{
	try
	{
		SXMPMeta xmp;
		auto f = blob_from_file(path);

		if (!f.empty())
		{
			xmp.ParseFromBuffer(std::bit_cast<const char*>(f.data()), f.size());
			parse_xmp(xmp, pd);
		}
	}
	catch (std::exception& e)
	{
		df::log(__FUNCTION__, e.what());
	}
	catch (const XMP_Error& e)
	{
		df::log(__FUNCTION__, e.GetErrMsg());
	}
}

file_scan_result scan_xmp(const df::file_path path)
{
	file_scan_result result;

	try
	{
		SXMPFiles f;

		if (f.OpenFile(str::utf8_to_a(path.str()), kXMP_UnknownFile,
		               kXMPFiles_OpenForRead | kXMPFiles_OpenOnlyXMP | kXMPFiles_OpenUseSmartHandler))
		{
			SXMPMeta meta;
			std::string packet;

			result.success = f.GetXMP(&meta, &packet);
			result.metadata.xmp.assign(packet.data(), packet.data() + packet.size());
			f.CloseFile();
		}
	}
	catch (XMP_Error& e)
	{
		df::log(__FUNCTION__, e.GetErrMsg());
		result.success = false;
	}

	return result;
}

df::file_path probe_xmp_path(const df::file_path src_path, const std::u8string_view xmp_name)
{
	if (!xmp_name.empty())
	{
		return src_path.folder().combine_file(xmp_name);
	}

	return src_path.extension(u8".xmp"sv);
}

xmp_update_result metadata_xmp::update(const df::file_path update_path, const df::file_path src_path,
                                       const metadata_edits& edits, const std::u8string_view xmp_name)
{
	xmp_update_result result;

	try
	{
		const auto* const src_ft = files::file_type_from_name(src_path);
		const auto* const dst_ft = files::file_type_from_name(update_path);
		const auto is_embedded_src = src_ft->traits && file_type_traits::embedded_xmp;
		const auto is_embedded_dst = dst_ft->traits && file_type_traits::embedded_xmp;

		SXMPMeta xmp;

		if (is_embedded_src)
		{
			SXMPFiles f;
			const auto w = platform::to_file_system_path(src_path);
			if (f.OpenFile(str::utf8_cast2(str::utf16_to_utf8(w)), kXMP_UnknownFile,
			               kXMPFiles_OpenForUpdate | kXMPFiles_OpenUseSmartHandler))
			{
				f.GetXMP(&xmp);
				f.CloseFile();
			}
		}
		else
		{
			const auto path_xmp = probe_xmp_path(src_path, xmp_name);

			if (path_xmp.exists())
			{
				auto f = blob_from_file(path_xmp);

				if (!f.empty())
				{
					xmp.ParseFromBuffer(std::bit_cast<const char*>(f.data()), f.size());
				}
			}
		}

		edits.apply(xmp);

		if (is_embedded_dst)
		{
			SXMPFiles xmp_dst_file;
			const auto w = platform::to_file_system_path(update_path);

			if (xmp_dst_file.OpenFile(str::utf8_cast2(str::utf16_to_utf8(w)), kXMP_UnknownFile,
			                          kXMPFiles_OpenForUpdate | kXMPFiles_OpenUseSmartHandler))
			{
				if (xmp_dst_file.CanPutXMP(xmp))
				{
					xmp_dst_file.PutXMP(xmp);
					result.success = true;
				}

				xmp_dst_file.CloseFile();
			}
		}
		else
		{
			std::string buffer;
			xmp.SerializeToBuffer(&buffer, kXMP_OmitPacketWrapper);

			const auto path_xmp = probe_xmp_path(update_path, xmp_name);
			const auto f = open_file(path_xmp, platform::file_open_mode::create);

			if (f)
			{
				f->write(std::bit_cast<const uint8_t*>(buffer.data()), buffer.size());
				result.success = true;
				result.xmp_path = path_xmp;
			}
		}
	}
	catch (XMP_Error& e)
	{
		df::log(__FUNCTION__, e.GetErrMsg());
		throw app_exception(e.GetErrMsg());
	}

	return result;
}

void metadata_xmp::update(std::u8string& buffer, const metadata_edits& edits)
{
	try
	{
		SXMPMeta meta;
		meta.ParseFromBuffer(std::bit_cast<const char*>(buffer.data()), buffer.size());
		edits.apply(meta);

		std::string temp;
		meta.SerializeToBuffer(&temp, kXMP_EncodeUTF8);
		buffer = str::utf8_cast(temp);
	}
	catch (const XMP_Error& e)
	{
		df::log(__FUNCTION__, e.GetErrMsg());
		throw app_exception(e.GetErrMsg());
	}
}

metadata_kv_list metadata_xmp::to_info(df::cspan xmp)
{
	metadata_kv_list result;

	try
	{
		SXMPMeta meta;

		const auto xmp_sig_len = xmp_signature.size() + 1;
		const auto* data = std::bit_cast<const char*>(xmp.data);
		auto size = xmp.size;

		if (memcmp(data, xmp_signature.data(), xmp_sig_len) == 0)
		{
			data = data + xmp_sig_len;
			size = size - xmp_sig_len;
		}

		meta.ParseFromBuffer(data, size);

		std::string schema_ns, prop_path, prop_val;
		SXMPIterator itr(meta);

		while (itr.Next(&schema_ns, &prop_path, &prop_val))
		{
			result.emplace_back(str::cache(prop_path), str::utf8_cast(prop_val));
		}
	}
	catch (std::exception& e)
	{
		df::log(__FUNCTION__, e.what());
	}
	catch (const XMP_Error& e)
	{
		df::log(__FUNCTION__, e.GetErrMsg());
	}

	return result;
}
