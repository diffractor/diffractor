// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: XMP (Extensible Metadata Platform) support. Reads and writes Adobe XMP
// metadata using the XMP Toolkit SDK for comprehensive metadata handling.

#include "pch.h"

#include "model_property.h"
#include "model_location.h"
#include "metadata_xmp.h"
#include "files.h"


#define TXMP_STRING_TYPE std::string
// The toolkit rejects more than one of these, and the client has to name the same one the library
// was built with or the two disagree about structure layout.
#ifdef _WIN32
#define WIN_ENV 1
#else
#define UNIX_ENV 1
#endif
#define XMP_INCLUDE_XMPFILES 1
#define XML_STATIC
#define XMP_StaticBuild 1

#include "XMP.hpp"
// ReSharper disable CppUnusedIncludeDirective
#include "metadata_exif.h"
#include "XMP.incl_cpp" // Needed otherwise undefined externs

// A malformed sidecar or a bad batch of files repeats the same failure for every scanned item, so
// each distinct message is written once and the total is capped; the session total lands in the
// exit perf summary.
static void record_xmp_error(const std::string_view context, const std::string_view message)
{
	df::bump(df::file_perf.metadata_errors);
	df::log_once(context, message);
}

static df::date_t xmp_parse_date(const std::string_view str)
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

// parse_xml_date reads the digits and drops the zone, so the caller has to recover it. Without
// this an XMP date ending in Z is filed as a local reading and lands an hour or more away from the
// container date it is meant to agree with, which then reads as two dates instead of one.
static int16_t xmp_date_offset(const std::string_view str)
{
	return prop::parse_utc_offset(str);
}

static bool xmp_decode_gps_coordinate(const std::string_view str, double& result)
{
	const auto len = str.size();
	const auto* const sz = std::bit_cast<const char*>(str.data());

	if (len < 4) return false;

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

	// Use original length for parsing (don't modify len before using it)
	const auto parse_len = len - 1;

	if (3 == _snscanf_s(sz, parse_len, "%d,%d,%d", &degrees, &mins, &seconds))
	{
		const auto coordinate = gps_coordinate::dms_to_decimal(degrees, mins, seconds);
		result = neg ? -coordinate : coordinate;
		return true;
	}

	float degrees2 = 0;
	float mins2 = 0;

	if (2 == _snscanf_s(sz, parse_len, "%f,%f", &degrees2, &mins2))
	{
		const auto coordinate = gps_coordinate::dms_to_decimal(degrees2, mins2, 0.0);
		result = neg ? -coordinate : coordinate;
		return true;
	}

	return false;
}

static bool xmp_decode_rational(const std::string_view text, metadata_exif::urational32_t& result)
{
	const auto len = text.size();
	unsigned long locNum = 0, locDenom = 0;
	char nextChar = 0; // Used to make sure sscanf consumes all of the string.
	const auto* const sz = std::bit_cast<const char*>(text.data());

	const int items = _snscanf_s(sz, len, "%lu/%lu%c", &locNum, &locDenom, &nextChar, 1);
	// AUDIT: This is safe, check the calls.

	if (items != 2)
	{
		if (items != 1) return false;
		locDenom = 1; // The XMP was just an integer, assume a denominator of 1.
	}

	// Protect against division by zero
	if (locDenom == 0)
	{
		return false;
	}

	result.numerator = locNum;
	result.denominator = locDenom;
	return true;
}

std::string microsoft_photo_prefix;

// xmp:Rating is -1 (rejected) through 5. Anything else came from a broken writer.
static int xmp_safe_rating(const int r)
{
	return std::clamp(r, -1, 5);
}

static str::cached xmp_load_array(const SXMPMeta& xmp, const char* schema_ns, const char* array_name)
{
	str::cached result = {};
	const auto count = xmp.CountArrayItems(schema_ns, array_name);

	if (count > 0)
	{
		std::vector<std::string> parts;
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

// Google's photo sphere namespace, which is what a phone panorama mode and most stitchers write.
// It is not one the toolkit knows, so it is registered at startup before any file is parsed.
constexpr auto ns_gpano = "http://ns.google.com/photos/1.0/panorama/";

// Records what the file itself declares, never what its shape suggests. `UsePanoramaViewer` and the
// cropped-area properties are read only to answer "is this a panorama" for a writer that omitted
// ProjectionType; a file carrying none of them is not one.
static void read_panorama_projection(const SXMPMeta& xmp, prop::item_metadata& md)
{
	std::string utf8;
	XMP_OptionBits flags = 0;

	if (xmp.GetProperty(ns_gpano, "ProjectionType", &utf8, &flags))
	{
		const auto text = str::trim(str::utf8_cast(utf8));

		if (str::icmp(text, "equirectangular") == 0)
		{
			md.panorama = prop::panorama_projection::equirectangular;
			return;
		}

		if (str::icmp(text, "cylindrical") == 0)
		{
			md.panorama = prop::panorama_projection::cylindrical;
			return;
		}

		if (!text.empty())
		{
			md.panorama = prop::panorama_projection::unspecified;
			return;
		}
	}

	for (const auto* const declares_panorama : {
		     "UsePanoramaViewer", "FullPanoWidthPixels", "CroppedAreaImageWidthPixels"
	     })
	{
		if (xmp.GetProperty(ns_gpano, declares_panorama, &utf8, &flags))
		{
			md.panorama = prop::panorama_projection::unspecified;
			return;
		}
	}
}

static void parse_xmp(const SXMPMeta& xmp, prop::item_metadata& md)
{
	XMP_OptionBits flags = 0;
	std::string utf8;

	if (xmp.GetProperty(kXMP_NS_Photoshop, "DateCreated", &utf8, &flags))
	{
		const auto text = str::utf8_cast(utf8);
		const auto d = xmp_parse_date(text);
		if (d.is_valid()) md.dates.add(prop::date_source::photoshop_created, d, xmp_date_offset(text));
	}

	if (xmp.GetProperty(kXMP_NS_XMP, "CreateDate", &utf8, &flags))
	{
		const auto text = str::utf8_cast(utf8);
		const auto d = xmp_parse_date(text);

		if (d.is_valid())
		{
			md.dates.add(prop::date_source::xmp_create, d, xmp_date_offset(text));
		}
		else if (str::is_num(text))
		{
			md.year = str::to_int(text);
		}
	}

	if (xmp.GetProperty(kXMP_NS_XMP, "ModifyDate", &utf8, &flags))
	{
		const auto text = str::utf8_cast(utf8);
		const auto d = xmp_parse_date(text);
		if (d.is_valid()) md.dates.add(prop::date_source::xmp_modify, d, xmp_date_offset(text));
	}

	if (xmp.GetProperty(kXMP_NS_EXIF, "DateTimeDigitized", &utf8, &flags))
	{
		const auto text = str::utf8_cast(utf8);
		const auto d = xmp_parse_date(text);
		if (d.is_valid()) md.dates.add(prop::date_source::xmp_exif_digitized, d, xmp_date_offset(text));
	}

	if (xmp.GetProperty(kXMP_NS_EXIF, "DateTimeOriginal", &utf8, &flags))
	{
		const auto text = str::utf8_cast(utf8);
		const auto d = xmp_parse_date(text);
		if (d.is_valid()) md.dates.add(prop::date_source::xmp_exif_original, d, xmp_date_offset(text));
	}

	// A coordinate that failed to decode must not be applied - a zeroed half pins the item to the
	// Gulf of Guinea instead of leaving the location unknown.
	if (xmp.GetProperty(kXMP_NS_EXIF, "GPSLatitude", &utf8, &flags))
	{
		double v = 0;
		if (xmp_decode_gps_coordinate(str::utf8_cast(utf8), v)) md.coordinate.latitude(v);
	}

	if (xmp.GetProperty(kXMP_NS_EXIF, "GPSLongitude", &utf8, &flags))
	{
		double v = 0;
		if (xmp_decode_gps_coordinate(str::utf8_cast(utf8), v)) md.coordinate.longitude(v);
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

	// Xmp is parsed last, so an absent array must not erase what Exif or Iptc supplied.
	if (const auto creator = xmp_load_array(xmp, kXMP_NS_DC, "creator"); !str::is_empty(creator))
	{
		md.copyright_creator = creator;
	}

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

	if (xmp.GetProperty(kXMP_NS_DM, "synopsis", &utf8, &flags))
	{
		md.synopsis = str::strip_and_cache(utf8);
	}

	if (xmp.GetLocalizedText(kXMP_NS_DC, "description", "", "x-default", nullptr, &utf8, &flags))
	{
		md.description = str::strip_and_cache(utf8);
	}

	if (xmp.GetProperty(kXMP_NS_XMP, "Rating", &utf8, &flags))
	{
		md.rating = xmp_safe_rating(str::to_int(str::utf8_cast(utf8)));
	}
	else if (xmp.GetProperty(kXMP_NS_MicrosoftPhoto, "Rating", &utf8, &flags))
	{
		//MicrosoftPhoto:Rating
		md.rating = xmp_safe_rating(df::round_up(str::to_int(str::utf8_cast(utf8)), 20));
	}

	if (xmp.GetProperty(kXMP_NS_XMP, "Label", &utf8, &flags))
	{
		md.label = str::strip_and_cache(utf8);
	}

	if (const auto subject = xmp_load_array(xmp, kXMP_NS_DC, "subject"); !str::is_empty(subject))
	{
		md.tags = subject;
	}

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
			md.f_number = static_cast<float>(r.to_real());
		}
	}

	if (xmp.GetProperty(kXMP_NS_EXIF, "FocalLength", &utf8, &flags))
	{
		metadata_exif::urational32_t r;

		if (xmp_decode_rational(str::utf8_cast(utf8), r))
		{
			md.focal_length = static_cast<float>(r.to_real());
		}
	}

	if (xmp.GetProperty(kXMP_NS_EXIF, "ExposureTime", &utf8, &flags))
	{
		metadata_exif::urational32_t r;

		if (xmp_decode_rational(str::utf8_cast(utf8), r))
		{
			md.exposure_time = static_cast<float>(r.to_real());
		}
	}

	if (xmp.GetProperty(kXMP_NS_TIFF, "Orientation", &utf8, &flags))
	{
		// An out of range value would index the rotation tables with an undefined enumerator.
		const auto o = str::to_int(str::utf8_cast(utf8));
		if (o >= 1 && o <= 8) md.orientation = static_cast<ui::orientation>(o);
	}

	if (xmp.GetProperty(kXMP_NS_EXIF_Aux, "Lens", &utf8, &flags))
	{
		md.lens = str::strip_and_cache(utf8);
	}

	if (xmp.GetProperty(kXMP_NS_CameraRaw, "RawFileName", &utf8, &flags))
	{
		md.raw_file_name = str::strip_and_cache(utf8);
	}

	read_panorama_projection(xmp, md);
}


void metadata_xmp::initialise()
{
	SXMPMeta::Initialize();
	// Spelled with its own type: a bare zero is also a null pointer constant, which makes the
	// option and plugin-folder overloads ambiguous where unsigned long is not unsigned int.
#ifdef _WIN32
	SXMPFiles::Initialize(XMP_OptionBits{0});
#else
	// The toolkit refuses to start on generic UNIX without this. There is no system code page to
	// reconcile legacy local-encoded text against, so such text is left exactly as stored rather
	// than transcoded by guess.
	SXMPFiles::Initialize(kXMPFiles_IgnoreLocalText);
#endif

	// https://github.com/nomacs/nomacs/blob/master/exiv2-0.25/src/xmp.cpp	
	//SXMPMeta::RegisterNamespace(kXMP_NS_MicrosoftPhoto, "MicrosoftPhoto", &microsoft_photo_prefix);

	std::string gpano_prefix;
	SXMPMeta::RegisterNamespace(ns_gpano, "GPano", &gpano_prefix);
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

		XMP_OptionBits flags = 0;
		meta.SetProperty(kXMP_NS_EXIF, "GPSLatitude",
		                 str::utf8_cast2(gps_coordinate::decimal_to_dms_str(position.latitude(), true)));
		meta.SetProperty(kXMP_NS_EXIF, "GPSLongitude",
		                 str::utf8_cast2(gps_coordinate::decimal_to_dms_str(position.longitude(), false)));
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
		meta.SetProperty(kXMP_NS_XMP_Rights, "WebStatement", str::utf8_cast2(copyright_url.value()));
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
		// dc:creator is an ordered array of names, and that is how it is read back. Writing it as
		// alt-lang text produces a packet Adobe tools will not show.
		meta.DeleteProperty(kXMP_NS_DC, "creator");

		if (str::is_empty(copyright_creator.value()))
		{
			meta.SetProperty(kXMP_NS_DC, "creator", nullptr, kXMP_PropArrayIsOrdered);
		}
		else
		{
			// Names are separated by punctuation, never by the space inside "Jane Doe".
			for (const auto& part : str::split(copyright_creator.value(), true, str::is_artist_separator))
			{
				meta.AppendArrayItem(kXMP_NS_DC, "creator", kXMP_PropArrayIsOrdered, str::utf8_cast2(part));
			}
		}
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

	if (synopsis.has_value())
	{
		meta.SetProperty(kXMP_NS_DM, "synopsis", str::utf8_cast2(synopsis.value()));
	}

	if (rating.has_value())
	{
		const int r = std::clamp(rating.value(), -1, 5);
		meta.SetProperty_Int(kXMP_NS_XMP, "Rating", r);
		meta.SetProperty_Int(kXMP_NS_MicrosoftPhoto, "Rating", rating_to_percent(r));
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

	if (date_original.has_value())
	{
		const auto text = str::utf8_cast2(date_original.value().to_xmp_date());

		// Both, because photoshop:DateCreated alone is the lowest-authority capture source and would
		// be outranked by the file's own DateTimeOriginal - the edit would appear to do nothing. The
		// toolkit reconciles exif:DateTimeOriginal back into the embedded EXIF on save.
		meta.SetProperty(kXMP_NS_Photoshop, "DateCreated", text);
		meta.SetProperty(kXMP_NS_EXIF, "DateTimeOriginal", text);
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

		tags.remove(remove_tags);
		tags.add(add_tags);
		tags.make_unique();

		meta.DeleteProperty(kXMP_NS_DC, "subject");

		if (tags.is_empty())
		{
			meta.SetProperty(kXMP_NS_DC, "subject", nullptr, kXMP_PropArrayIsUnordered);
		}
		else
			for (auto i = 0u; i < tags.size(); i++)
			{
				meta.AppendArrayItem(kXMP_NS_DC, "subject", kXMP_PropValueIsArray, str::utf8_cast2(tags[i]));
			}

		// Sync exif:XPKeywords (Windows Explorer tag) with dc:subject. Cleared tags are written
		// as an empty value rather than a deleted property: the TIFF export leaves tags that are
		// simply absent from XMP alone, so deleting here would leave the old keywords in place.
		meta.SetProperty(kXMP_NS_EXIF, "XPKeywords",
		                 tags.is_empty() ? std::string() : str::utf8_cast2(tags.to_string(";", false)));
	}

	if (remove_rating)
	{
		meta.SetProperty_Int(kXMP_NS_XMP, "Rating", 0);
		meta.SetProperty_Int(kXMP_NS_MicrosoftPhoto, "Rating", 0);
	}
}

metadata_xmp::property_presence metadata_xmp::properties(const df::cspan xmp)
{
	property_presence result;

	try
	{
		// xmp_signature already ends in the NUL that terminates the JPEG APP1 header.
		constexpr auto xmp_sig_len = xmp_signature.size();
		if (xmp.size == 0) return result;

		const auto* data = xmp.data;
		SXMPMeta meta;
		if (xmp.size > xmp_sig_len && memcmp(data, xmp_signature.data(), xmp_sig_len) == 0)
		{
			data += xmp_sig_len;
			meta.ParseFromBuffer(std::bit_cast<const char*>(data), static_cast<uint32_t>(xmp.size - xmp_sig_len));
		}
		else
		{
			meta.ParseFromBuffer(std::bit_cast<const char*>(data), static_cast<uint32_t>(xmp.size));
		}

		result.tags = meta.DoesPropertyExist(kXMP_NS_DC, "subject");
		result.rating = meta.DoesPropertyExist(kXMP_NS_XMP, "Rating") ||
			meta.DoesPropertyExist(kXMP_NS_MicrosoftPhoto, "Rating");
	}
	catch (const std::exception& e)
	{
		record_xmp_error(__FUNCTION__, e.what());
	}
	catch (const XMP_Error& e)
	{
		record_xmp_error(__FUNCTION__, e.GetErrMsg());
	}

	return result;
}

void metadata_xmp::parse(prop::item_metadata& pd, const df::cspan xmp)
{
	try
	{
		constexpr auto xmp_sig_len = xmp_signature.size();
		const auto size = xmp.size;
		const auto* const data = xmp.data;

		if (size > 0)
		{
			SXMPMeta meta;

			if (size > xmp_sig_len && memcmp(data, xmp_signature.data(), xmp_sig_len) == 0)
			{
				meta.ParseFromBuffer(std::bit_cast<const char*>(data + xmp_sig_len),
				                     static_cast<uint32_t>(size - xmp_sig_len));
			}
			else
			{
				meta.ParseFromBuffer(std::bit_cast<const char*>(data), static_cast<uint32_t>(size));
			}

			parse_xmp(meta, pd);
		}
	}
	catch (const std::exception& e)
	{
		record_xmp_error(__FUNCTION__, e.what());
	}
	catch (const XMP_Error& e)
	{
		record_xmp_error(__FUNCTION__, e.GetErrMsg());
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
			xmp.ParseFromBuffer(std::bit_cast<const char*>(f.data()), static_cast<uint32_t>(f.size()));
			parse_xmp(xmp, pd);
		}
	}
	catch (const std::exception& e)
	{
		record_xmp_error(__FUNCTION__, e.what());
	}
	catch (const XMP_Error& e)
	{
		record_xmp_error(__FUNCTION__, e.GetErrMsg());
	}
}

df::file_path probe_xmp_path(const df::file_path src_path, const std::string_view xmp_name)
{
	if (!xmp_name.empty())
	{
		return src_path.folder().combine_file(xmp_name);
	}

	return src_path.extension(".xmp");
}

// GPano writes these as plain integers. A property the file omits leaves its member zero, which
// prop::panorama_geometry reads as an incomplete declaration and resolves against the pixels.
static void read_panorama_geometry(const SXMPMeta& xmp, prop::panorama_geometry& g)
{
	const auto read = [&xmp](const char* const name, int& out)
	{
		XMP_Int32 value = 0;
		XMP_OptionBits flags = 0;
		if (xmp.GetProperty_Int(ns_gpano, name, &value, &flags)) out = value;
	};

	read("FullPanoWidthPixels", g.full_width);
	read("FullPanoHeightPixels", g.full_height);
	read("CroppedAreaLeftPixels", g.cropped_left);
	read("CroppedAreaTopPixels", g.cropped_top);
	read("CroppedAreaImageWidthPixels", g.cropped_width);
	read("CroppedAreaImageHeightPixels", g.cropped_height);
}

prop::panorama_geometry metadata_xmp::panorama(const df::file_path path)
{
	prop::panorama_geometry result;

	try
	{
		const auto* const ft = files::file_type_from_name(path);

		if (ft->has_trait(file_traits::embedded_xmp))
		{
			SXMPFiles f;

			if (f.OpenFile(str::utf8_cast2(platform::to_utf8_file_system_path(path)), kXMP_UnknownFile,
			               kXMPFiles_OpenForRead | kXMPFiles_OpenUseSmartHandler))
			{
				SXMPMeta xmp;
				const auto found = f.GetXMP(&xmp);
				f.CloseFile();

				if (found)
				{
					read_panorama_geometry(xmp, result);
					if (result.is_valid()) return result;
				}
			}
		}

		// A stitcher that wrote a sidecar rather than an embedded packet still declared the sphere. The
		// embedded reading is discarded first: read_panorama_geometry fills only what a packet declares,
		// so joining an incomplete embedded declaration to the sidecar's would describe neither file.
		result = {};

		const auto sidecar = blob_from_file(probe_xmp_path(path, {}));

		if (!sidecar.empty())
		{
			SXMPMeta xmp;
			xmp.ParseFromBuffer(std::bit_cast<const char*>(sidecar.data()), static_cast<uint32_t>(sidecar.size()));
			read_panorama_geometry(xmp, result);
		}
	}
	catch (const std::exception& e)
	{
		record_xmp_error(__FUNCTION__, e.what());
	}
	catch (const XMP_Error& e)
	{
		record_xmp_error(__FUNCTION__, e.GetErrMsg());
	}

	return result;
}

bool metadata_xmp::has_embedded_xmp(const df::file_path path)
{
	try
	{
		const auto* const ft = files::file_type_from_name(path);

		if (!ft->has_trait(file_traits::embedded_xmp))
		{
			return false;
		}

		SXMPFiles f;

		if (!f.OpenFile(str::utf8_cast2(platform::to_utf8_file_system_path(path)), kXMP_UnknownFile,
		                kXMPFiles_OpenForRead | kXMPFiles_OpenUseSmartHandler))
		{
			return false;
		}

		XMP_PacketInfo packet_info;
		const auto found = f.GetXMP(nullptr, nullptr, &packet_info) && packet_info.length > 0;
		f.CloseFile();

		return found;
	}
	catch (const std::exception& e)
	{
		record_xmp_error(__FUNCTION__, e.what());
	}
	catch (const XMP_Error& e)
	{
		record_xmp_error(__FUNCTION__, e.GetErrMsg());
	}

	return false;
}

xmp_update_result metadata_xmp::update(const df::file_path update_path, const df::file_path src_path,
                                       const metadata_edits& edits, const std::string_view src_xmp_name,
                                       const df::file_path dst_xmp_path)
{
	xmp_update_result result;

	// Which file a toolkit error refers to; the read source and the write target differ.
	auto failing_path = src_path;

	try
	{
		const auto* const src_ft = files::file_type_from_name(src_path);
		const auto* const dst_ft = files::file_type_from_name(update_path);
		const auto is_embedded_src = src_ft->has_trait(file_traits::embedded_xmp);
		const auto is_embedded_dst = dst_ft->has_trait(file_traits::embedded_xmp);

		SXMPMeta xmp;

		if (is_embedded_src)
		{
			SXMPFiles f;
			// Read only - the source is never modified here. If it cannot be read the existing
			// packet is unknown, and applying the edits to an empty one would write away every
			// property the file already holds.
			if (f.OpenFile(str::utf8_cast2(platform::to_utf8_file_system_path(src_path)), kXMP_UnknownFile,
			               kXMPFiles_OpenForRead | kXMPFiles_OpenUseSmartHandler))
			{
				f.GetXMP(&xmp);
				f.CloseFile();
			}
			else if (open_file(src_path, platform::file_open_mode::read))
			{
				// The file itself reads fine, so the toolkit simply has no handler for this
				// format. Reporting a read failure would send the user looking for a permission
				// or locking problem that does not exist.
				throw app_exception("this file format is not supported for metadata edits");
			}
			else
			{
				throw app_exception("the existing metadata could not be read");
			}
		}
		else
		{
			const auto path_xmp = probe_xmp_path(src_path, src_xmp_name);
			const auto attributes = platform::file_attributes(path_xmp);

			// Only not_found proves there is no sidecar. Denied, offline, or an unreachable share all
			// answer unknown, and treating those as "no sidecar" is the data loss this guard exists to
			// stop, moved one step earlier.
			if (!attributes.confirmed_missing())
			{
				auto f = blob_from_file(path_xmp);

				// Same rule as the embedded branch: a sidecar that cannot be read is unknown, not empty.
				// Applying the edits to a default packet and swapping it over the file would write away
				// every property the sidecar already holds. An empty read is only believable when the file
				// is known to be zero length - and that has to be known, not merely reported by an
				// attribute query that may itself have failed.
				if (f.empty() && !(attributes.exists() && attributes.size == 0))
				{
					throw app_exception("the existing metadata could not be read");
				}

				if (!f.empty())
				{
					xmp.ParseFromBuffer(std::bit_cast<const char*>(f.data()), static_cast<uint32_t>(f.size()));
				}
			}
		}

		edits.apply(xmp);

		failing_path = update_path;

		if (is_embedded_dst)
		{
			SXMPFiles xmp_dst_file;

			if (xmp_dst_file.OpenFile(str::utf8_cast2(platform::to_utf8_file_system_path(update_path)),
			                          kXMP_UnknownFile,
			                          kXMPFiles_OpenForUpdate | kXMPFiles_OpenUseSmartHandler))
			{
				const auto can_put = xmp_dst_file.CanPutXMP(xmp);

				if (can_put) xmp_dst_file.PutXMP(xmp);

				// PutXMP only stages the packet - the handler writes on close. Deliberately not
				// kXMPFiles_UpdateSafely: that always copies the whole file to a temp and swaps,
				// which is the cost an in-place caller is here to avoid. Choosing between a
				// patch and an atomic replace is the caller's decision, not this function's.
				xmp_dst_file.CloseFile();

				// OpenFile succeeded but the format cannot hold the metadata. Report it rather
				// than silently leaving the tags unwritten (#231).
				if (!can_put)
				{
					throw app_exception("this file format cannot store the metadata");
				}

				result.success = true;
			}
			else
			{
				// OpenFile returned false without throwing - common when the file is locked by
				// another process or read-only. Explain why instead of failing opaquely (#231).
				const auto reason = platform::file_write_error(update_path);
				throw app_exception(reason.empty() ? "the file could not be opened for writing" : reason);
			}
		}
		else
		{
			failing_path = dst_xmp_path;

			std::string buffer;
			xmp.SerializeToBuffer(&buffer, kXMP_OmitPacketWrapper);

			const auto path_xmp = dst_xmp_path;
			const auto f = open_file(path_xmp, platform::file_open_mode::create);

			if (f)
			{
				const auto written = f->write(std::bit_cast<const uint8_t*>(buffer.data()), buffer.size());

				// A short write leaves a truncated sidecar. Reporting success here would present
				// a full disk or a dropped network share as a saved edit.
				if (written != buffer.size())
				{
					const auto reason = platform::file_write_error(path_xmp);
					throw app_exception(reason.empty() ? "the metadata sidecar could not be written" : reason);
				}

				result.success = true;
				result.xmp_path = path_xmp;
			}
			else
			{
				const auto reason = platform::file_write_error(path_xmp);
				throw app_exception(reason.empty() ? "the metadata sidecar could not be created" : reason);
			}
		}
	}
	catch (XMP_Error& e)
	{
		df::log(__FUNCTION__, e.GetErrMsg());

		// The XMP toolkit's own message (e.g. "Open, other failure") is opaque; prepend the
		// concrete OS reason when the file simply cannot be opened for writing (#231).
		const auto reason = platform::file_write_error(failing_path);

		if (!reason.empty())
		{
			throw app_exception(std::format("{} ({})", reason, e.GetErrMsg()));
		}

		throw app_exception(e.GetErrMsg());
	}

	return result;
}

void metadata_xmp::update(std::string& buffer, const metadata_edits& edits)
{
	try
	{
		SXMPMeta meta;
		meta.ParseFromBuffer(std::bit_cast<const char*>(buffer.data()), static_cast<uint32_t>(buffer.size()));
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

// The namespace URI is what the packet carries; the short name is what makes the section legible.
static std::string_view xmp_schema_title(const std::string_view ns)
{
	if (ns == "http://purl.org/dc/elements/1.1/") return "Dublin Core";
	if (ns == "http://ns.adobe.com/xap/1.0/") return "XMP Basic";
	if (ns == "http://ns.adobe.com/xap/1.0/rights/") return "XMP Rights";
	if (ns == "http://ns.adobe.com/xap/1.0/mm/") return "XMP Media Management";
	if (ns == "http://ns.adobe.com/xap/1.0/sType/ResourceEvent#") return "Resource Event";
	if (ns == "http://ns.adobe.com/xap/1.0/sType/ResourceRef#") return "Resource Reference";
	if (ns == "http://ns.adobe.com/photoshop/1.0/") return "Photoshop";
	if (ns == "http://ns.adobe.com/camera-raw-settings/1.0/") return "Camera Raw";
	if (ns == "http://ns.adobe.com/exif/1.0/") return "Exif";
	if (ns == "http://ns.adobe.com/exif/1.0/aux/") return "Exif Auxiliary";
	if (ns == "http://ns.adobe.com/tiff/1.0/") return "TIFF";
	if (ns == "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/") return "IPTC Core";
	if (ns == "http://iptc.org/std/Iptc4xmpExt/2008-02-29/") return "IPTC Extension";
	if (ns == "http://ns.adobe.com/xmp/1.0/DynamicMedia/") return "Dynamic Media";
	if (ns == "http://ns.microsoft.com/photo/1.0/") return "Microsoft Photo";
	if (ns == "http://ns.google.com/photos/1.0/camera/") return "Google Camera";
	return {};
}

// The pane draws the packet's own tree, and the iterator reports position only through the path,
// so depth is read back from the path's structure.
static int xmp_path_depth(const std::string_view path)
{
	auto depth = 0;
	for (const auto c : path) if (c == '/' || c == '[') ++depth;
	return depth;
}

static std::string xmp_shape(const XMP_OptionBits options, const int array_count)
{
	std::string result;

	if (XMP_PropIsArray(options))
	{
		if (XMP_ArrayIsAltText(options)) result = "Alt (language)";
		else if (XMP_ArrayIsAlternate(options)) result = "Alt";
		else if (XMP_ArrayIsOrdered(options)) result = "Seq";
		else result = "Bag";

		result += std::format(" ({})", array_count);
	}
	else if (XMP_PropIsStruct(options))
	{
		result = "Struct";
	}

	if (XMP_PropIsQualifier(options))
	{
		if (!result.empty()) result += ", ";
		result += "qualifier";
	}

	return result;
}

metadata_kv_list metadata_xmp::to_info(const df::cspan xmp)
{
	metadata_kv_list result;

	try
	{
		SXMPMeta meta;

		constexpr auto xmp_sig_len = xmp_signature.size();
		const auto* data = std::bit_cast<const char*>(xmp.data);
		auto size = xmp.size;

		if (size > xmp_sig_len && memcmp(data, xmp_signature.data(), xmp_sig_len) == 0)
		{
			data = data + xmp_sig_len;
			size = size - xmp_sig_len;
		}

		meta.ParseFromBuffer(data, static_cast<uint32_t>(size));

		std::string schema_ns, prop_path, prop_val;
		XMP_OptionBits options = 0;
		SXMPIterator itr(meta);
		std::string current_schema;

		// Containers and empty properties are part of what the packet holds, so every node is
		// listed; the tree shape is carried on the row rather than used to filter it.
		while (itr.Next(&schema_ns, &prop_path, &prop_val, &options))
		{
			if (XMP_NodeIsSchema(options) || prop_path.empty()) continue;

			if (schema_ns != current_schema)
			{
				current_schema = schema_ns;
				const auto title = xmp_schema_title(schema_ns);

				auto& section = result.emplace_back(title.empty() ? schema_ns : std::string(title), std::string{});
				section.container = true;
				section.id = std::format("xmp.ns.{}", schema_ns);
				if (!title.empty()) section.shape = schema_ns;
			}

			const auto is_container = XMP_PropIsArray(options) || XMP_PropIsStruct(options);
			const auto array_count = XMP_PropIsArray(options)
				                         ? static_cast<int>(meta.CountArrayItems(
					                         schema_ns.c_str(), prop_path.c_str()))
				                         : 0;

			auto& row = result.emplace_back(prop_path, str::utf8_cast(prop_val));
			row.depth = xmp_path_depth(prop_path) + 1;
			row.container = is_container;
			row.id = std::format("xmp.{}", prop_path);

			const auto shape = xmp_shape(options, array_count);
			if (!shape.empty()) row.shape = shape;

			// A long value is kept whole behind the row rather than trimmed away.
			constexpr auto inline_value_limit = 200_z;

			if (row.value.size() > inline_value_limit)
			{
				row.detail = metadata_text_detail{row.value};
				row.value = row.value.substr(0, inline_value_limit) + "\xE2\x80\xA6";
			}
		}
	}
	catch (const std::exception& e)
	{
		record_xmp_error(__FUNCTION__, e.what());
	}
	catch (const XMP_Error& e)
	{
		record_xmp_error(__FUNCTION__, e.GetErrMsg());
	}

	return result;
}
