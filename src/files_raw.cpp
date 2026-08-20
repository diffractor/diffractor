// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Camera RAW image format support. Decodes RAW files from various cameras
// using LibRaw and Adobe DNG SDK, extracts metadata and thumbnails.

#include "pch.h"
#include "files.h"

#define LIBRAW_NODLL 1
#define LIBRAW_LIBRARY_BUILD 1
#define LIBRAW_WIN32_UNICODEPATHS 1
#define USE_DNGSDK 1

#include "dng/dng_host.h"
#include "dng/dng_tag_values.h"

// LibRaw redefines the DNG SDK feature flags (qDNGUseLibJPEG, qDNGXMPFiles, qDNGThreadSafe) for its
// own headers. Including it first would instead change the flags the SDK headers above see, which the
// prebuilt SDK library was not compiled with, so the redefinition is left in place and only silenced.
#pragma warning(push)
#pragma warning(disable : 4005) // macro redefinition
#include <libraw/libraw.h>
#pragma warning(pop)

// LibRaw hands DNG 1.7 JPEG-XL images (TIFF compression 52546) to the SDK only while qDNGSupportJXL
// is visible and the SDK is 1.7 or newer - see valid_for_dngsdk in LibRaw's dngsdk_glue.cpp. In SDK
// 1.7 that macro survives only as a deprecated-flag sentinel, so an SDK upgrade could remove it and
// silently turn every JPEG-XL DNG into an open failure with no other symptom.
#ifndef qDNGSupportJXL
#error "qDNGSupportJXL is not visible - LibRaw will reject JPEG XL DNG files"
#endif
static_assert(dngVersion_Current >= dngVersion_1_7_0_0, "DNG SDK predates JPEG XL DNG support");

// LibRaw takes the native path: the wide overloads exist only under LIBRAW_WIN32_UNICODEPATHS, and
// elsewhere it takes the byte path, which is what platform::native_path already answers.
namespace
{
	auto libraw_path(const df::file_path path)
	{
		return platform::to_file_system_path(path);
	}
}

struct id2hr_t
{
	uint64_t id;
	const char* name;
}; // id to human readable

static constexpr id2hr_t MountNames[] = {
	{LIBRAW_MOUNT_Alpa, "Alpa"},
	{LIBRAW_MOUNT_C, "C-mount"},
	{LIBRAW_MOUNT_Canon_EF_M, "Canon EF-M"},
	{LIBRAW_MOUNT_Canon_EF_S, "Canon EF-S"},
	{LIBRAW_MOUNT_Canon_EF, "Canon EF"},
	{LIBRAW_MOUNT_Canon_RF, "Canon RF"},
	{LIBRAW_MOUNT_Contax_N, "Contax N"},
	{LIBRAW_MOUNT_Contax645, "Contax 645"},
	{LIBRAW_MOUNT_FT, "4/3"},
	{LIBRAW_MOUNT_mFT, "m4/3"},
	{LIBRAW_MOUNT_Fuji_GF, "Fuji G"}, // Fujifilm G lenses, GFX cameras
	{LIBRAW_MOUNT_Fuji_GX, "Fuji GX"}, // GX680
	{LIBRAW_MOUNT_Fuji_X, "Fuji X"},
	{LIBRAW_MOUNT_Hasselblad_H, "Hasselblad H"}, // Hn cameras, HC & HCD lenses
	{LIBRAW_MOUNT_Hasselblad_V, "Hasselblad V"},
	{LIBRAW_MOUNT_Hasselblad_XCD, "Hasselblad XCD"}, // Xn cameras, XCD lenses
	{LIBRAW_MOUNT_Leica_M, "Leica M"},
	{LIBRAW_MOUNT_Leica_R, "Leica R"},
	{LIBRAW_MOUNT_Leica_S, "Leica S"},
	{LIBRAW_MOUNT_Leica_SL, "Leica SL"}, // mounts on "L" throat
	{LIBRAW_MOUNT_Leica_TL, "Leica TL"}, // mounts on "L" throat
	{LIBRAW_MOUNT_LPS_L, "LPS L-mount"}, // throat, Leica / Panasonic / Sigma
	{LIBRAW_MOUNT_Mamiya67, "Mamiya RZ/RB"}, // Mamiya RB67, RZ67
	{LIBRAW_MOUNT_Mamiya645, "Mamiya 645"},
	{LIBRAW_MOUNT_Minolta_A, "Sony/Minolta A"},
	{LIBRAW_MOUNT_Nikon_CX, "Nikkor 1"},
	{LIBRAW_MOUNT_Nikon_F, "Nikkor F"},
	{LIBRAW_MOUNT_Nikon_Z, "Nikkor Z"},
	{LIBRAW_MOUNT_Pentax_645, "Pentax 645"},
	{LIBRAW_MOUNT_Pentax_K, "Pentax K"},
	{LIBRAW_MOUNT_Pentax_Q, "Pentax Q"},
	{LIBRAW_MOUNT_RicohModule, "Ricoh module"},
	{LIBRAW_MOUNT_Rollei_bayonet, "Rollei bayonet"}, // Rollei Hy-6: Leaf AFi, Sinar Hy6- models
	{LIBRAW_MOUNT_Samsung_NX_M, "Samsung NX-M"},
	{LIBRAW_MOUNT_Samsung_NX, "Samsung NX"},
	{LIBRAW_MOUNT_Sigma_X3F, "Sigma SA/X3F"},
	{LIBRAW_MOUNT_Sony_E, "Sony E"},
	// generic formats:
	{LIBRAW_MOUNT_LF, "Large format"},
	{LIBRAW_MOUNT_DigitalBack, "Digital Back"},
	{LIBRAW_MOUNT_FixedLens, "Fixed Lens"},
	{LIBRAW_MOUNT_IL_UM, "Interchangeable lens, mount unknown"},
	{LIBRAW_MOUNT_Unknown, "Undefined Mount or Fixed Lens"},
	{LIBRAW_MOUNT_TheLastOne, "The Last One"},
};

static constexpr id2hr_t FormatNames[] = {
	{LIBRAW_FORMAT_1div2p3INCH, "1/2.3\""},
	{LIBRAW_FORMAT_1div1p7INCH, "1/1.7\""},
	{LIBRAW_FORMAT_1INCH, "1\""},
	{LIBRAW_FORMAT_FT, "4/3"},
	{LIBRAW_FORMAT_APSC, "APS-C"}, // Canon: 22.3x14.9mm; Sony et al: 23.6-23.7x15.6mm
	{LIBRAW_FORMAT_Leica_DMR, "Leica DMR"}, // 26.4x 17.6mm
	{LIBRAW_FORMAT_APSH, "APS-H"}, // Canon: 27.9x18.6mm
	{LIBRAW_FORMAT_FF, "FF 35mm"},
	{LIBRAW_FORMAT_CROP645, "645 crop 44x33mm"},
	{LIBRAW_FORMAT_LeicaS, "Leica S 45x30mm"},
	{LIBRAW_FORMAT_3648, "48x36mm"},
	{LIBRAW_FORMAT_645, "6x4.5"},
	{LIBRAW_FORMAT_66, "6x6"},
	{LIBRAW_FORMAT_67, "6x7"},
	{LIBRAW_FORMAT_68, "6x8"},
	{LIBRAW_FORMAT_69, "6x9"},
	{LIBRAW_FORMAT_SigmaAPSC, "Sigma APS-C"}, //  Sigma Foveon X3 orig: 20.7x13.8mm
	{LIBRAW_FORMAT_SigmaMerrill, "Sigma Merrill"},
	{LIBRAW_FORMAT_SigmaAPSH, "Sigma APS-H"}, // Sigma "H"26.7 x 17.9mm
	{LIBRAW_FORMAT_MF, "Medium Format"},
	{LIBRAW_FORMAT_LF, "Large format"},
	{LIBRAW_FORMAT_Unknown, "Unknown"},
	{LIBRAW_FORMAT_TheLastOne, "The Last One"},
};

static constexpr id2hr_t NikonCrops[] = {
	{0, "Uncropped"}, {1, "1.3x"}, {2, "DX"},
	{3, "5:4"}, {4, "3:2"}, {6, "16:9"},
	{8, "2.7x"}, {9, "DX Movie"}, {10, "1.3x Movie"},
	{11, "FX Uncropped"}, {12, "DX Uncropped"}, {15, "1.5x Movie"},
	{17, "1:1"},
};

static constexpr id2hr_t FujiCrops[] = {
	{0, "Uncropped"},
	{1, "GFX FF"},
	{2, "Sports Finder Mode"},
	{4, "Electronic Shutter 1.25x Crop"},
};

static constexpr id2hr_t FujiDriveModes[] = {
	{0, "Single Frame"},
	{1, "Continuous Low"},
	{2, "Continuous High"},
};

static constexpr id2hr_t CanonRecordModes[] = {
	{LIBRAW_Canon_RecordMode_JPEG, "JPEG"},
	{LIBRAW_Canon_RecordMode_CRW_THM, "CRW+THM"},
	{LIBRAW_Canon_RecordMode_AVI_THM, "AVI+THM"},
	{LIBRAW_Canon_RecordMode_TIF, "TIF"},
	{LIBRAW_Canon_RecordMode_TIF_JPEG, "TIF+JPEG"},
	{LIBRAW_Canon_RecordMode_CR2, "CR2"},
	{LIBRAW_Canon_RecordMode_CR2_JPEG, "CR2+JPEG"},
	{LIBRAW_Canon_RecordMode_UNKNOWN, "Unknown"},
	{LIBRAW_Canon_RecordMode_MOV, "MOV"},
	{LIBRAW_Canon_RecordMode_MP4, "MP4"},
	{LIBRAW_Canon_RecordMode_CRM, "CRM"},
	{LIBRAW_Canon_RecordMode_CR3, "CR3"},
	{LIBRAW_Canon_RecordMode_CR3_JPEG, "CR3+JPEG"},
	{LIBRAW_Canon_RecordMode_HEIF, "HEIF"},
	{LIBRAW_Canon_RecordMode_CR3_HEIF, "CR3+HEIF"},
};

static constexpr struct
{
	const int NumId;
	std::string_view StrId;
} CorpToStr[] = {
	{LIBRAW_CAMERAMAKER_Agfa, "Agfa"},
	{LIBRAW_CAMERAMAKER_Alcatel, "Alcatel"},
	{LIBRAW_CAMERAMAKER_Apple, "Apple"},
	{LIBRAW_CAMERAMAKER_Aptina, "Aptina"},
	{LIBRAW_CAMERAMAKER_AVT, "AVT"},
	{LIBRAW_CAMERAMAKER_Baumer, "Baumer"},
	{LIBRAW_CAMERAMAKER_Broadcom, "Broadcom"},
	{LIBRAW_CAMERAMAKER_Canon, "Canon"},
	{LIBRAW_CAMERAMAKER_Casio, "Casio"},
	{LIBRAW_CAMERAMAKER_CINE, "CINE"},
	{LIBRAW_CAMERAMAKER_Clauss, "Clauss"},
	{LIBRAW_CAMERAMAKER_Contax, "Contax"},
	{LIBRAW_CAMERAMAKER_Creative, "Creative"},
	{LIBRAW_CAMERAMAKER_DJI, "DJI"},
	{LIBRAW_CAMERAMAKER_DXO, "DXO"},
	{LIBRAW_CAMERAMAKER_Epson, "Epson"},
	{LIBRAW_CAMERAMAKER_Foculus, "Foculus"},
	{LIBRAW_CAMERAMAKER_Fujifilm, "Fujifilm"},
	{LIBRAW_CAMERAMAKER_Generic, "Generic"},
	{LIBRAW_CAMERAMAKER_Gione, "Gione"},
	{LIBRAW_CAMERAMAKER_GITUP, "GITUP"},
	{LIBRAW_CAMERAMAKER_Google, "Google"},
	{LIBRAW_CAMERAMAKER_GoPro, "GoPro"},
	{LIBRAW_CAMERAMAKER_Hasselblad, "Hasselblad"},
	{LIBRAW_CAMERAMAKER_HTC, "HTC"},
	{LIBRAW_CAMERAMAKER_I_Mobile, "I_Mobile"},
	{LIBRAW_CAMERAMAKER_Imacon, "Imacon"},
	{LIBRAW_CAMERAMAKER_Kodak, "Kodak"},
	{LIBRAW_CAMERAMAKER_Konica, "Konica"},
	{LIBRAW_CAMERAMAKER_Leaf, "Leaf"},
	{LIBRAW_CAMERAMAKER_Leica, "Leica"},
	{LIBRAW_CAMERAMAKER_Lenovo, "Lenovo"},
	{LIBRAW_CAMERAMAKER_LG, "LG"},
	{LIBRAW_CAMERAMAKER_Logitech, "Logitech"},
	{LIBRAW_CAMERAMAKER_Mamiya, "Mamiya"},
	{LIBRAW_CAMERAMAKER_Matrix, "Matrix"},
	{LIBRAW_CAMERAMAKER_Meizu, "Meizu"},
	{LIBRAW_CAMERAMAKER_Micron, "Micron"},
	{LIBRAW_CAMERAMAKER_Minolta, "Minolta"},
	{LIBRAW_CAMERAMAKER_Motorola, "Motorola"},
	{LIBRAW_CAMERAMAKER_NGM, "NGM"},
	{LIBRAW_CAMERAMAKER_Nikon, "Nikon"},
	{LIBRAW_CAMERAMAKER_Nokia, "Nokia"},
	{LIBRAW_CAMERAMAKER_Olympus, "Olympus"},
	{LIBRAW_CAMERAMAKER_OmniVison, "OmniVison"},
	{LIBRAW_CAMERAMAKER_Panasonic, "Panasonic"},
	{LIBRAW_CAMERAMAKER_Parrot, "Parrot"},
	{LIBRAW_CAMERAMAKER_Pentax, "Pentax"},
	{LIBRAW_CAMERAMAKER_PhaseOne, "PhaseOne"},
	{LIBRAW_CAMERAMAKER_PhotoControl, "PhotoControl"},
	{LIBRAW_CAMERAMAKER_Photron, "Photron"},
	{LIBRAW_CAMERAMAKER_Pixelink, "Pixelink"},
	{LIBRAW_CAMERAMAKER_Polaroid, "Polaroid"},
	{LIBRAW_CAMERAMAKER_RED, "RED"},
	{LIBRAW_CAMERAMAKER_Ricoh, "Ricoh"},
	{LIBRAW_CAMERAMAKER_Rollei, "Rollei"},
	{LIBRAW_CAMERAMAKER_RoverShot, "RoverShot"},
	{LIBRAW_CAMERAMAKER_Samsung, "Samsung"},
	{LIBRAW_CAMERAMAKER_Sigma, "Sigma"},
	{LIBRAW_CAMERAMAKER_Sinar, "Sinar"},
	{LIBRAW_CAMERAMAKER_SMaL, "SMaL"},
	{LIBRAW_CAMERAMAKER_Sony, "Sony"},
	{LIBRAW_CAMERAMAKER_ST_Micro, "ST_Micro"},
	{LIBRAW_CAMERAMAKER_THL, "THL"},
	{LIBRAW_CAMERAMAKER_Xiaomi, "Xiaomi"},
	{LIBRAW_CAMERAMAKER_XIAOYI, "XIAOYI"},
	{LIBRAW_CAMERAMAKER_YI, "YI"},
	{LIBRAW_CAMERAMAKER_Yuneec, "Yuneec"},
	{LIBRAW_CAMERAMAKER_Zeiss, "Zeiss"},
};

static constexpr struct
{
	const int NumId;
	std::string_view StrId;
} ColorSpaceToStr[] = {
	{LIBRAW_COLORSPACE_NotFound, "Not Found"},
	{LIBRAW_COLORSPACE_sRGB, "sRGB"},
	{LIBRAW_COLORSPACE_AdobeRGB, "Adobe RGB"},
	{LIBRAW_COLORSPACE_WideGamutRGB, "Wide Gamut RGB"},
	{LIBRAW_COLORSPACE_ProPhotoRGB, "ProPhoto RGB"},
	{LIBRAW_COLORSPACE_ICC, "ICC profile (embedded)"},
	{LIBRAW_COLORSPACE_Uncalibrated, "Uncalibrated"},
	{LIBRAW_COLORSPACE_CameraLinearUniWB, "Camera Linear, no WB"},
	{LIBRAW_COLORSPACE_CameraLinear, "Camera Linear"},
	{LIBRAW_COLORSPACE_CameraGammaUniWB, "Camera non-Linear, no WB"},
	{LIBRAW_COLORSPACE_CameraGamma, "Camera non-Linear"},
	{LIBRAW_COLORSPACE_MonochromeLinear, "Monochrome Linear"},
	{LIBRAW_COLORSPACE_MonochromeGamma, "Monochrome non-Linear"},
	{LIBRAW_COLORSPACE_Unknown, "Unknown"},
};

static constexpr struct
{
	const int NumId;
	const int LibRawId;
	std::string_view StrId;
} Fujifilm_WhiteBalance2Str[] = {
	{0x000, LIBRAW_WBI_Auto, "Auto"},
	{0x100, LIBRAW_WBI_Daylight, "Daylight"},
	{0x200, LIBRAW_WBI_Cloudy, "Cloudy"},
	{0x300, LIBRAW_WBI_FL_D, "Daylight Fluorescent"},
	{0x301, LIBRAW_WBI_FL_N, "Day White Fluorescent"},
	{0x302, LIBRAW_WBI_FL_W, "White Fluorescent"},
	{0x303, LIBRAW_WBI_FL_WW, "Warm White Fluorescent"},
	{0x304, LIBRAW_WBI_FL_L, "Living Room Warm White Fluorescent"},
	{0x400, LIBRAW_WBI_Tungsten, "Incandescent"},
	{0x500, LIBRAW_WBI_Flash, "Flash"},
	{0x600, LIBRAW_WBI_Underwater, "Underwater"},
	{0xf00, LIBRAW_WBI_Custom, "Custom"},
	{0xf01, LIBRAW_WBI_Custom2, "Custom2"},
	{0xf02, LIBRAW_WBI_Custom3, "Custom3"},
	{0xf03, LIBRAW_WBI_Custom4, "Custom4"},
	{0xf04, LIBRAW_WBI_Custom5, "Custom5"},
	{0xff0, LIBRAW_WBI_Kelvin, "Kelvin"},
};

static constexpr struct
{
	const int NumId;
	std::string_view StrId;
} Fujifilm_FilmModeToStr[] = {
	{0x000, "F0/Standard (Provia)"},
	{0x100, "F1/Studio Portrait"},
	{0x110, "F1a/Studio Portrait Enhanced Saturation"},
	{0x120, "F1b/Studio Portrait Smooth Skin Tone (Astia)"},
	{0x130, "F1c/Studio Portrait Increased Sharpness"},
	{0x200, "F2/Fujichrome (Velvia)"},
	{0x300, "F3/Studio Portrait Ex"},
	{0x400, "F4/Velvia"},
	{0x500, "Pro Neg. Std"},
	{0x501, "Pro Neg. Hi"},
	{0x600, "Classic Chrome"},
	{0x700, "Eterna"},
	{0x800, "Classic Negative"},
};

static constexpr struct
{
	const int NumId;
	std::string_view StrId;
} Fujifilm_DynamicRangeSettingToStr[] = {
	{0x0000, "Auto (100-400%)"},
	{0x0001, "Manual"},
	{0x0100, "Standard (100%)"},
	{0x0200, "Wide1 (230%)"},
	{0x0201, "Wide2 (400%)"},
	{0x8000, "Film Simulation"},
};

template <typename T, size_t N>
static std::string_view find_name_by_id(const T (&table)[N], const int id)
{
	for (const auto& entry : table)
		if (entry.NumId == id)
			return entry.StrId;
	return {};
}

static const id2hr_t* lookup_id2hr(const uint64_t id, const id2hr_t* table, const size_t nEntries)
{
	for (size_t k = 0; k < nEntries; k++)
		if (id == table[k].id)
			return &table[k];
	return nullptr;
}

static ui::orientation translate_libraw_orientation(const int flip)
{
	// 3 if requires 180-deg rotation; 5 if 90 deg counterclockwise, 6 if 90 deg clockwise
	auto result = flip;
	if (result == 0) result = 1;
	if (result == 5) result = 8;
	return static_cast<ui::orientation>(result);
}

static void add_metadata(metadata_kv_list& kv, const std::string_view name, const std::string_view val)
{
	if (!name.empty() && !val.empty())
	{
		auto& row = kv.emplace_back(std::string(name), std::string(val));
		row.depth = 1;
	}
}

// LibRaw reports one long flat list, so it is grouped by subject here to give the block the same
// shape the other metadata standards have.
static void add_section(metadata_kv_list& kv, const std::string_view name)
{
	auto& row = kv.emplace_back(std::string(name), std::string{});
	row.container = true;
	row.id = std::format("raw.{}", name);
}

// Most sections are conditional on the camera and format, so the ones that gathered nothing are
// removed rather than shown as empty headings. The survivors report how much they hold.
static void drop_empty_sections(metadata_kv_list& kv)
{
	metadata_kv_list kept;
	kept.reserve(kv.size());

	for (size_t i = 0; i < kv.size(); ++i)
	{
		if (kv[i].container)
		{
			size_t count = 0;
			while (i + 1 + count < kv.size() && !kv[i + 1 + count].container) ++count;
			if (count == 0) continue;

			kv[i].key = std::format("{} ({})", kv[i].key, count);
		}

		kept.emplace_back(std::move(kv[i]));
	}

	kv = std::move(kept);
}

static void add_metadata(metadata_kv_list& kv, const std::string_view name, const std::string_view val1,
                         const std::string_view val2)
{
	std::string val;
	str::join(val, val1, val2);
	add_metadata(kv, name, val);
}

// Helper: look up id2hr entry, add name if found, else add numeric value
template <size_t N>
static void add_id2hr_metadata(metadata_kv_list& kv, const std::string_view name,
                               const uint64_t id, const id2hr_t (&table)[N])
{
	if (const auto* entry = lookup_id2hr(id, table, N))
		add_metadata(kv, name, entry->name);
	else
		add_metadata(kv, name, str::to_string(id));
}

// The colour filter array LibRaw inferred, spelled from the 2x2 repeat of the `filters` mask.
static std::string cfa_pattern(const libraw_iparams_t& P1)
{
	if (P1.filters == 0) return {};
	if (P1.filters == 9) return "X-Trans (6x6)";
	if (P1.filters == 1) return "monochrome or variable";

	std::string result;

	for (auto row = 0; row < 2; ++row)
	{
		for (auto col = 0; col < 2; ++col)
		{
			result += P1.cdesc[(P1.filters >> (((row << 1 & 14) + (col & 1)) << 1)) & 3];
		}
	}

	return result;
}

// LibRaw sets gpsparsed for any non-empty GPS IFD, and many cameras (for example the Canon
// EOS 7D) write a GPS IFD containing only GPSVersionID. Without this check those files land
// at 0,0. Matches the zero rejection the EXIF path applies in exif_gps_coordinate_builder.
static bool raw_has_gps_fix(const libraw_gps_info_t& gps)
{
	if (!gps.gpsparsed) return false;

	const auto lat = gps_coordinate::dms_to_decimal(gps.latitude[0], gps.latitude[1], gps.latitude[2]);
	const auto lon = gps_coordinate::dms_to_decimal(gps.longitude[0], gps.longitude[1], gps.longitude[2]);

	return lat > 0.0 && lon > 0.0 &&
		lat <= gps_coordinate::max_valid_latitude &&
		lon < gps_coordinate::invalid_coordinate;
}

static void populate_raw_metadata(file_scan_result& result, const libraw_data_t& data, const scan_intent intent)
{
	result.width = data.sizes.width;
	result.height = data.sizes.height;

	if (data.sizes.flip)
	{
		result.orientation = translate_libraw_orientation(data.sizes.flip);
	}

	if (data.idata.xmpdata && data.idata.xmplen > 0)
	{
		result.metadata.xmp.assign(data.idata.xmpdata, data.idata.xmpdata + data.idata.xmplen);
	}

	// LibRaw builds data.other.timestamp by feeding the EXIF DateTimeOriginal (a
	// naive local time with no timezone) through mktime(), which applies the
	// process's local timezone. Interpreting that epoch as UTC would shift the
	// value by the machine's UTC offset, making the result machine-dependent (and
	// inconsistent with the JPEG/EXIF path, which stores the naive time as-is).
	// Invert the mktime() with localtime_s() to recover the original naive fields
	// so the timestamp is identical regardless of the host timezone.
	if (data.other.timestamp != 0)
	{
		tm local_tm{};

		if (localtime_s(&local_tm, &data.other.timestamp) == 0)
		{
			const auto created = df::date_t(local_tm.tm_year + 1900, local_tm.tm_mon + 1, local_tm.tm_mday,
			                                local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec);
			if (created.is_valid()) result.created_utc = created;
		}
	}

	if (!str::is_empty(data.idata.cdesc)) result.pixel_format = str::trim_and_cache(data.idata.cdesc);
	if (!str::is_empty(data.idata.make)) result.camera_manufacturer = str::trim_and_cache(data.idata.make);
	if (!str::is_empty(data.idata.model)) result.camera_model = str::trim_and_cache(data.idata.model);
	if (!str::is_empty(data.other.desc)) result.comment = str::trim_and_cache(data.other.desc);
	if (!str::is_empty(data.other.artist)) result.artist = str::trim_and_cache(data.other.artist);

	result.iso_speed = df::round(data.other.iso_speed);
	result.exposure_time = data.other.shutter;
	result.f_number = data.other.aperture;
	result.focal_length = data.other.focal_len;

	const auto& gps = data.other.parsed_gps;
	if (raw_has_gps_fix(gps))
	{
		const auto lat = gps_coordinate::dms_to_decimal(gps.latitude[0], gps.latitude[1], gps.latitude[2]);
		const auto lon = gps_coordinate::dms_to_decimal(gps.longitude[0], gps.longitude[1], gps.longitude[2]);

		result.gps._latitude = (gps.latref == 'S') ? -lat : lat;
		result.gps._longitude = (gps.longref == 'W') ? -lon : lon;
	}

	// Everything below only feeds the verbose block, so indexing stops here.
	if (intent != scan_intent::inspect) return;

	const auto& P1 = data.idata;
	const auto& P2 = data.other;
	const auto& P3 = data.makernotes.common;

	const auto& mnLens = data.lens.makernotes;
	const auto& exifLens = data.lens;
	const auto& ShootingInfo = data.shootinginfo;

	const auto& S = data.sizes;
	const auto& C = data.color;
	const auto& T = data.thumbnail;

	const auto& Canon = data.makernotes.canon;
	const auto& Hasselblad = data.makernotes.hasselblad;
	const auto& Fuji = data.makernotes.fuji;
	const auto& Nikon = data.makernotes.nikon;
	const auto& Sony = data.makernotes.sony;

	const auto CamMakerName = str::safe_string(LibRaw::cameramakeridx2maker(P1.maker_index));
	const auto ColorSpaceName = find_name_by_id(ColorSpaceToStr, P3.ColorSpace);

	metadata_kv_list kv;

	add_section(kv, "Identification");

	if (!str::is_empty(C.OriginalRawFileName))
		add_metadata(kv, "OriginalRawFileName", C.OriginalRawFileName);

	add_metadata(kv, "Timestamp", str::to_string(P2.timestamp));
	add_metadata(kv, "Camera",
	             std::format("{} {} ID: 0x{:x}", str::utf8_cast(P1.make), str::utf8_cast(P1.model), mnLens.CamID));
	add_metadata(kv, "Normalized Make/Model",
	             std::format("{}/{}", str::utf8_cast(P1.normalized_make), str::utf8_cast(P1.normalized_model)));
	add_metadata(kv, "CamMaker ID", str::to_string(P1.maker_index));

	if (!CamMakerName.empty())
		add_metadata(kv, "CameraMaker", CamMakerName);

	if (!str::is_empty(C.UniqueCameraModel))
	{
		add_metadata(kv, "UniqueCameraModel", C.UniqueCameraModel);
	}
	if (!str::is_empty(C.LocalizedCameraModel))
	{
		add_metadata(kv, "LocalizedCameraModel", C.LocalizedCameraModel);
	}
	if (!str::is_empty(C.ImageUniqueID))
	{
		add_metadata(kv, "ImageUniqueID", C.ImageUniqueID);
	}
	if (!str::is_empty(C.RawDataUniqueID))
	{
		add_metadata(kv, "RawDataUniqueID", C.RawDataUniqueID);
	}

	if (!str::is_empty(ShootingInfo.BodySerial) && strcmp(ShootingInfo.BodySerial, "0"))
	{
		add_metadata(kv, "Body#", str::trim(ShootingInfo.BodySerial));
	}
	else if (C.model2[0] && !_strnicmp(P1.normalized_make, "Kodak", 5))
	{
		add_metadata(kv, "Body#", str::trim(C.model2));
	}
	if (!str::is_empty(ShootingInfo.InternalBodySerial))
	{
		add_metadata(kv, "BodyAssy#", str::trim(ShootingInfo.InternalBodySerial));
	}
	if (!str::is_empty(exifLens.LensSerial))
	{
		add_metadata(kv, "Lens#", str::trim(exifLens.LensSerial));
	}
	if (!str::is_empty(exifLens.InternalLensSerial))
	{
		add_metadata(kv, "LensAssy#", str::trim(exifLens.InternalLensSerial));
	}
	if (!str::is_empty(P2.artist))
	{
		add_metadata(kv, "Owner", str::trim(P2.artist));
	}
	if (!str::is_empty(P1.software))
	{
		add_metadata(kv, "Software", str::trim(P1.software));
	}
	if (!str::is_empty(P3.firmware))
	{
		add_metadata(kv, "Firmware", str::trim(P3.firmware));
	}
	if (P2.shot_order)
	{
		add_metadata(kv, "Shot order", str::to_string(P2.shot_order));
	}
	if (P1.is_foveon)
	{
		add_metadata(kv, "Foveon sensor", "yes");
	}

	if (P1.dng_version)
	{
		std::string s;
		for (int i = 24; i >= 0; i -= 8)
			s += std::format("{}{}", P1.dng_version >> i & 255, i ? '.' : ' ');

		add_metadata(kv, "DNG Version", s);
	}


	add_section(kv, "Lens (Exif)");
	add_metadata(kv, "MinFocal", std::format("{:0.1} mm", exifLens.MinFocal));
	add_metadata(kv, "MaxFocal", std::format("{:0.1} mm", exifLens.MaxFocal));
	add_metadata(kv, "MaxAp @MinFocal", std::format("f/{:0.1}", exifLens.MaxAp4MinFocal));
	add_metadata(kv, "MaxAp @MaxFocal", std::format("f/{:0.1}", exifLens.MaxAp4MaxFocal));
	add_metadata(kv, "CurFocal", std::format("{:0.1} mm", P2.focal_len));
	add_metadata(kv, "MaxAperture @CurFocal", std::format("f/{:0.1}", exifLens.EXIF_MaxAp));
	add_metadata(kv, "FocalLengthIn35mmFormat", std::format("{} mm", exifLens.FocalLengthIn35mmFormat));
	add_metadata(kv, "LensMake", exifLens.LensMake);
	add_metadata(kv, "Lens", exifLens.Lens);


	add_section(kv, "Shooting");
	add_metadata(kv, "DriveMode", str::to_string(ShootingInfo.DriveMode));
	add_metadata(kv, "FocusMode", str::to_string(ShootingInfo.FocusMode));
	add_metadata(kv, "MeteringMode", str::to_string(ShootingInfo.MeteringMode));
	add_metadata(kv, "AFPoint", str::to_string(ShootingInfo.AFPoint));
	add_metadata(kv, "ExposureMode", str::to_string(ShootingInfo.ExposureMode));
	add_metadata(kv, "ExposureProgram", str::to_string(ShootingInfo.ExposureProgram));
	add_metadata(kv, "ImageStabilization", str::to_string(ShootingInfo.ImageStabilization));

	add_section(kv, "Lens (makernotes)");

	if (!str::is_empty(mnLens.body))
	{
		add_metadata(kv, "Host Body", mnLens.body);
	}

	if (!str::is_empty(Hasselblad.CaptureSequenceInitiator))
	{
		add_metadata(kv, "Initiator", Hasselblad.CaptureSequenceInitiator);
	}
	if (!str::is_empty(Hasselblad.SensorUnitConnector))
	{
		add_metadata(kv, "SU Connector", Hasselblad.SensorUnitConnector);
	}

	add_id2hr_metadata(kv, "CameraFormat", mnLens.CameraFormat, FormatNames);


	if (!_strnicmp(P1.make, "Nikon", 5) && Nikon.SensorHighSpeedCrop.cwidth)
	{
		if (const auto* Crop = lookup_id2hr(Nikon.HighSpeedCropFormat, NikonCrops, std::size(NikonCrops)))
			add_metadata(kv, "Nikon crop", std::format("{}: {}", Nikon.HighSpeedCropFormat, Crop->name));
		else
			add_metadata(kv, "Nikon crop", str::to_string(Nikon.HighSpeedCropFormat));

		add_metadata(kv, "Sensor used area", std::format(
			             "{} x {}; crop from: {} x {} at top left pixel: ({}, {})",
			             Nikon.SensorWidth, Nikon.SensorHeight,
			             Nikon.SensorHighSpeedCrop.cwidth,
			             Nikon.SensorHighSpeedCrop.cheight,
			             Nikon.SensorHighSpeedCrop.cleft,
			             Nikon.SensorHighSpeedCrop.ctop));
	}

	add_id2hr_metadata(kv, "CameraMount", mnLens.CameraMount, MountNames);

	if (mnLens.LensID != 0xffffffff)
		add_metadata(kv, "LensID", std::format("{} 0x{:x}", mnLens.LensID, mnLens.LensID));

	if (!str::is_empty(mnLens.Lens))
	{
		add_metadata(kv, "Lens", mnLens.Lens);
	}

	add_id2hr_metadata(kv, "LensFormat", mnLens.LensFormat, FormatNames);

	add_id2hr_metadata(kv, "LensMount", mnLens.LensMount, MountNames);

	switch (mnLens.FocalType)
	{
	case LIBRAW_FT_UNDEFINED:
		add_metadata(kv, "FocalType", "Undefined");
		break;
	case LIBRAW_FT_PRIME_LENS:
		add_metadata(kv, "FocalType", "Prime lens");
		break;
	case LIBRAW_FT_ZOOM_LENS:
		add_metadata(kv, "FocalType", "Zoom lens");
		break;
	default:
		add_metadata(kv, "FocalType", str::to_string(mnLens.FocalType));
		break;
	}

	add_metadata(kv, "LensFeatures_pre", mnLens.LensFeatures_pre);
	add_metadata(kv, "LensFeatures_suf", mnLens.LensFeatures_suf);
	add_metadata(kv, "MinFocal", std::format("{:0.1} mm", mnLens.MinFocal));
	add_metadata(kv, "MaxFocal", std::format("{:0.1} mm", mnLens.MaxFocal));
	add_metadata(kv, "MaxAp @MinFocal", std::format("f/{:0.1}", mnLens.MaxAp4MinFocal));
	add_metadata(kv, "MaxAp @MaxFocal", std::format("f/{:0.1}", mnLens.MaxAp4MaxFocal));
	add_metadata(kv, "MinAp @MinFocal", std::format("f/{:0.1}", mnLens.MinAp4MinFocal));
	add_metadata(kv, "MinAp @MaxFocal", std::format("f/{:0.1}", mnLens.MinAp4MaxFocal));
	add_metadata(kv, "MaxAp", std::format("f/{:0.1}", mnLens.MaxAp));
	add_metadata(kv, "MinAp", std::format("f/{:0.1}", mnLens.MinAp));
	add_metadata(kv, "CurFocal", std::format("{:0.1} mm", mnLens.CurFocal));
	add_metadata(kv, "CurAp", std::format("f/{:0.1}", mnLens.CurAp));
	add_metadata(kv, "MaxAp @CurFocal", std::format("f/{:0.1}", mnLens.MaxAp4CurFocal));
	add_metadata(kv, "MinAp @CurFocal", std::format("f/{:0.1}", mnLens.MinAp4CurFocal));

	if (exifLens.makernotes.FocalLengthIn35mmFormat > 1.0f)
		add_metadata(kv, "FocalLengthIn35mmFormat",
		             std::format("{:0.1} mm", exifLens.makernotes.FocalLengthIn35mmFormat));

	if (exifLens.nikon.EffectiveMaxAp > 0.1f)
		add_metadata(kv, "EffectiveMaxAp", std::format("f/{:0.1}", exifLens.nikon.EffectiveMaxAp));

	if (exifLens.makernotes.LensFStops > 0.1f)
		add_metadata(kv, "LensFStops @CurFocal", std::format("{:0.2}", exifLens.makernotes.LensFStops));

	add_metadata(kv, "TeleconverterID", str::to_string(mnLens.TeleconverterID));
	add_metadata(kv, "Teleconverter", mnLens.Teleconverter);
	add_metadata(kv, "AdapterID", str::to_string(mnLens.AdapterID));
	add_metadata(kv, "Adapter", mnLens.Adapter);
	add_metadata(kv, "AttachmentID", str::to_string(mnLens.AttachmentID));
	add_metadata(kv, "Attachment", mnLens.Attachment);

	if (mnLens.MinFocusDistance > 0.f)
		add_metadata(kv, "MinFocusDistance", std::format("{:0.2} m", mnLens.MinFocusDistance));
	if (mnLens.FocusRangeIndex > 0.f)
		add_metadata(kv, "FocusRangeIndex", std::format("{:0.2}", mnLens.FocusRangeIndex));
	if (mnLens.FocalUnits)
		add_metadata(kv, "FocalUnits", std::format("{} per mm", mnLens.FocalUnits));

	add_section(kv, "Exposure");
	add_metadata(kv, "ISO speed", str::to_string(static_cast<int>(P2.iso_speed)));
	if (P3.real_ISO > 0.1f)
		add_metadata(kv, "real ISO speed", str::to_string(static_cast<int>(P3.real_ISO)));

	if (P2.shutter > 0 && P2.shutter < 1)
		add_metadata(kv, "Shutter", std::format("1/{:0.1}", 1.0f / P2.shutter));
	else if (P2.shutter >= 1)
		add_metadata(kv, "Shutter", std::format("{:0.1} sec", P2.shutter));
	else /* negative*/
		add_metadata(kv, "Shutter", "negative value");

	add_metadata(kv, "Aperture", std::format("f/{:0.1}", P2.aperture));
	add_metadata(kv, "Focal length", std::format("{:0.1} mm", P2.focal_len));

	if (P3.exifExposureIndex > 0.f)
		add_metadata(kv, "Exposure index", std::format("{:0.1}", P3.exifExposureIndex));
	if (P3.ExposureCalibrationShift != 0.f)
		add_metadata(kv, "Exposure calibration shift", std::format("{:0.3} EV", P3.ExposureCalibrationShift));
	if (P3.FlashEC != 0.f)
		add_metadata(kv, "Flash exposure compensation", std::format("{:0.2} EV", P3.FlashEC));
	if (C.flash_used != 0.f)
		add_metadata(kv, "Flash used", std::format("{:0.3}", C.flash_used));
	if (C.canon_ev != 0.f)
		add_metadata(kv, "Canon EV", std::format("{:0.3}", C.canon_ev));
	if (P3.afcount > 0)
		add_metadata(kv, "Autofocus records", str::to_string(P3.afcount));

	add_section(kv, "Temperature");

	if (P3.exifAmbientTemperature > -273.15f)
		add_metadata(kv, "Ambient temperature (exif data)", std::format("{:.2f}°C", P3.exifAmbientTemperature));
	if (P3.CameraTemperature > -273.15f)
		add_metadata(kv, "Camera temperature", std::format("{:.2f}°C", P3.CameraTemperature));
	if (P3.SensorTemperature > -273.15f)
		add_metadata(kv, "Sensor temperature", std::format("{:.2f}°C", P3.SensorTemperature));
	if (P3.SensorTemperature2 > -273.15f)
		add_metadata(kv, "Sensor temperature2", std::format("{:.2f}°C", P3.SensorTemperature2));
	if (P3.LensTemperature > -273.15f)
		add_metadata(kv, "Lens temperature", std::format("{:.2f}°C", P3.LensTemperature));
	if (P3.AmbientTemperature > -273.15f)
		add_metadata(kv, "Ambient temperature", std::format("{:.2f}°C", P3.AmbientTemperature));
	if (P3.BatteryTemperature > -273.15f)
		add_metadata(kv, "Battery temperature", std::format("{:.2f}°C", P3.BatteryTemperature));
	if (P3.FlashGN > 1.0f)
		add_metadata(kv, "Flash Guide Number", std::format("{:.2f}", P3.FlashGN));

	add_section(kv, "Environment");

	if (P3.exifHumidity > 0.f)
		add_metadata(kv, "Humidity", std::format("{:.1f} %", P3.exifHumidity));
	if (P3.exifPressure > 0.f)
		add_metadata(kv, "Pressure", std::format("{:.1f} hPa", P3.exifPressure));
	if (P3.exifWaterDepth != 0.f)
		add_metadata(kv, "Water depth", std::format("{:.2f} m", P3.exifWaterDepth));
	if (P3.exifAcceleration != 0.f)
		add_metadata(kv, "Acceleration", std::format("{:.3f}", P3.exifAcceleration));
	if (P3.exifCameraElevationAngle != 0.f)
		add_metadata(kv, "Camera elevation angle", std::format("{:.2f}°", P3.exifCameraElevationAngle));

	add_section(kv, "GPS");

	if (raw_has_gps_fix(P2.parsed_gps))
	{
		const auto& G = P2.parsed_gps;
		add_metadata(kv, "Latitude", std::format("{:.0f}° {:.0f}' {:.2f}\" {}", G.latitude[0], G.latitude[1],
		                                         G.latitude[2], G.latref ? G.latref : '?'));
		add_metadata(kv, "Longitude", std::format("{:.0f}° {:.0f}' {:.2f}\" {}", G.longitude[0], G.longitude[1],
		                                          G.longitude[2], G.longref ? G.longref : '?'));
		add_metadata(kv, "Altitude", std::format("{:.2f} m{}", G.altitude, G.altref ? " (below sea level)" : ""));
		add_metadata(kv, "GPS timestamp", std::format("{:02.0f}:{:02.0f}:{:05.2f} UTC", G.gpstimestamp[0],
		                                              G.gpstimestamp[1], G.gpstimestamp[2]));

		// 'A' is an active fix; 'V' means the receiver reported the position as unreliable.
		if (G.gpsstatus)
			add_metadata(kv, "Fix status", G.gpsstatus == 'A' ? "A (active)" : std::format("{}", G.gpsstatus));
	}

	add_section(kv, "Color profile");

	if (C.profile)
		add_metadata(kv, "Embedded ICC profile", std::format("yes, {} bytes", C.profile_length));
	else
		add_metadata(kv, "Embedded ICC profile", "no");

	if (C.dng_levels.baseline_exposure > -999.f)
		add_metadata(kv, "Baseline exposure", std::format("{:.3f}", C.dng_levels.baseline_exposure));

	add_section(kv, "Raw image");
	add_metadata(kv, "Number of raw images", str::to_string(P1.raw_count));

	add_section(kv, "Fujifilm");

	if (Fuji.DriveMode != -1)
		add_id2hr_metadata(kv, "Fuji Drive Mode", Fuji.DriveMode, FujiDriveModes);

	if (Fuji.CropMode)
		add_id2hr_metadata(kv, "Fuji Crop Mode", Fuji.CropMode, FujiCrops);

	if (Fuji.WB_Preset != 0xffff)
		add_metadata(kv, "Fuji WB preset", std::format("0x{:03x}", Fuji.WB_Preset),
		             find_name_by_id(Fujifilm_WhiteBalance2Str, Fuji.WB_Preset));
	if (Fuji.ExpoMidPointShift > -999.f) // tag 0x9650
		add_metadata(kv, "Fuji Exposure shift", std::format("{:4.3}", Fuji.ExpoMidPointShift));
	if (Fuji.DynamicRange != 0xffff)
		add_metadata(kv, "Fuji Dynamic Range (0x1400)", std::format("{}", Fuji.DynamicRange),
		             Fuji.DynamicRange == 1 ? "Standard" : "Wide");
	if (Fuji.FilmMode != 0xffff)
		add_metadata(kv, "Fuji Film Mode (0x1401)", std::format("0x{:03x}", Fuji.FilmMode),
		             find_name_by_id(Fujifilm_FilmModeToStr, Fuji.FilmMode));
	if (Fuji.DynamicRangeSetting != 0xffff)
		add_metadata(kv, "Fuji Dynamic Range Setting (0x1402)",
		             std::format("0x{:04x}", Fuji.DynamicRangeSetting),
		             find_name_by_id(Fujifilm_DynamicRangeSettingToStr, Fuji.DynamicRangeSetting));
	if (Fuji.DevelopmentDynamicRange != 0xffff)
		add_metadata(kv, "Fuji Development Dynamic Range (0x1403)", str::to_string(Fuji.DevelopmentDynamicRange));
	if (Fuji.AutoDynamicRange != 0xffff)
		add_metadata(kv, "Fuji Auto Dynamic Range (0x140b)", str::to_string(Fuji.AutoDynamicRange));
	if (Fuji.DRangePriority != 0xffff)
		add_metadata(kv, "Fuji Dynamic Range priority (0x1443)", std::format("{}", Fuji.DRangePriority),
		             Fuji.DRangePriority ? "Fixed" : "Auto");
	if (Fuji.DRangePriorityAuto)
		add_metadata(kv, "Fuji Dynamic Range priority Auto (0x1444)",
		             std::format("{}", Fuji.DRangePriorityAuto),
		             Fuji.DRangePriorityAuto == 1 ? "Weak" : "Strong");
	if (Fuji.DRangePriorityFixed)
		add_metadata(kv, "Fuji Dynamic Range priority Fixed (0x1445)",
		             std::format("{}", Fuji.DRangePriorityFixed),
		             Fuji.DRangePriorityFixed == 1 ? "Weak" : "Strong");

	add_section(kv, "Sizes");

	if (S.pixel_aspect != 1)
		add_metadata(kv, "Pixel Aspect Ratio", std::format("{:0.6}", S.pixel_aspect));

	if (T.tlength)
		add_metadata(kv, "Thumb size", std::format("{:4} x {}", T.twidth, T.theight));

	add_metadata(kv, "Full size", std::format("{:4} x {}", S.raw_width, S.raw_height));

	if (S.raw_inset_crops[0].cwidth)
	{
		auto s = std::format("{:4} x {}", S.raw_inset_crops[0].cwidth, S.raw_inset_crops[0].cheight);

		if (S.raw_inset_crops[0].cleft != 0xffff)
			s += std::format(" left {}", S.raw_inset_crops[0].cleft);
		if (S.raw_inset_crops[0].ctop != 0xffff)
			s += std::format(" top {}", S.raw_inset_crops[0].ctop);

		add_metadata(kv, "Raw inset, width x height", s);
	}

	add_metadata(kv, "Image size", std::format("{:4} x {}", S.width, S.height));
	add_metadata(kv, "Output size", std::format("{:4} x {}", S.iwidth, S.iheight));
	add_metadata(kv, "Image flip", str::to_string(S.flip));

	if (S.top_margin || S.left_margin)
		add_metadata(kv, "Active area margin", std::format("left {}, top {}", S.left_margin, S.top_margin));
	if (S.raw_pitch)
		add_metadata(kv, "Raw row pitch", std::format("{} bytes", S.raw_pitch));
	if (S.raw_aspect)
		add_metadata(kv, "Raw aspect", str::to_string(S.raw_aspect));

	if (S.raw_inset_crops[1].cwidth)
	{
		add_metadata(kv, "Raw inset 2, width x height",
		             std::format("{:4} x {}", S.raw_inset_crops[1].cwidth, S.raw_inset_crops[1].cheight));
	}

	if (C.dng_levels.default_crop[2] || C.dng_levels.default_crop[3])
	{
		add_metadata(kv, "DNG default crop", std::format("{} x {} at ({}, {})", C.dng_levels.default_crop[2],
		                                                 C.dng_levels.default_crop[3], C.dng_levels.default_crop[0],
		                                                 C.dng_levels.default_crop[1]));
	}

	add_section(kv, "Thumbnails");

	if (T.tlength)
	{
		add_metadata(kv, "Preferred thumbnail", std::format("{} x {}, {} bytes, {} colors", T.twidth, T.theight,
		                                                    T.tlength, T.tcolors));
	}

	for (auto i = 0; i < data.thumbs_list.thumbcount && i < LIBRAW_THUMBNAIL_MAXCOUNT; ++i)
	{
		const auto& t = data.thumbs_list.thumblist[i];
		add_metadata(kv, std::format("Thumbnail {}", i + 1),
		             std::format("{} x {}, {} bytes at offset {}", t.twidth, t.theight, t.tlength, t.toffset));
	}

	add_section(kv, "Canon");

	if (Canon.RecordMode)
		add_id2hr_metadata(kv, "Canon record mode", Canon.RecordMode, CanonRecordModes);
	if (Canon.SensorWidth)
		add_metadata(kv, "SensorWidth", str::to_string(Canon.SensorWidth));
	if (Canon.SensorHeight)
		add_metadata(kv, "SensorHeight", str::to_string(Canon.SensorHeight));

	add_section(kv, "Hasselblad");

	if (Hasselblad.BaseISO)
		add_metadata(kv, "Hasselblad base ISO", str::to_string(Hasselblad.BaseISO));
	if (Hasselblad.Gain)
		add_metadata(kv, "Hasselblad gain", str::to_string(Hasselblad.Gain, 3));

	add_section(kv, "Raw color data");
	add_metadata(kv, "Raw colors", str::to_string(P1.colors));
	add_metadata(kv, "Color description", P1.cdesc);

	if (const auto cfa = cfa_pattern(P1); !cfa.empty())
	{
		add_metadata(kv, "CFA pattern", cfa);
	}

	if (C.raw_bps)
		add_metadata(kv, "Bits per raw pixel", str::to_string(C.raw_bps));
	if (C.maximum)
		add_metadata(kv, "White level", str::to_string(C.maximum));
	if (C.data_maximum)
		add_metadata(kv, "Highest sampled value", str::to_string(C.data_maximum));
	if (C.linear_max[0])
	{
		add_metadata(kv, "Linear maximum", std::format("{} {} {} {}", C.linear_max[0], C.linear_max[1],
		                                               C.linear_max[2], C.linear_max[3]));
	}

	if (C.cam_mul[0] > 0.f)
	{
		add_metadata(kv, "As shot white balance", std::format("{:0.4} {:0.4} {:0.4} {:0.4}", C.cam_mul[0],
		                                                      C.cam_mul[1], C.cam_mul[2], C.cam_mul[3]));
		add_metadata(kv, "White balance applied", C.as_shot_wb_applied ? "yes" : "no");
	}

	if (C.pre_mul[0] > 0.f)
	{
		add_metadata(kv, "Camera daylight multipliers", std::format("{:0.4} {:0.4} {:0.4} {:0.4}", C.pre_mul[0],
		                                                            C.pre_mul[1], C.pre_mul[2], C.pre_mul[3]));
	}

	if (C.dng_levels.asshotneutral[0] > 0.f)
	{
		add_metadata(kv, "DNG AsShotNeutral", std::format("{:0.4} {:0.4} {:0.4}", C.dng_levels.asshotneutral[0],
		                                                  C.dng_levels.asshotneutral[1],
		                                                  C.dng_levels.asshotneutral[2]));
	}

	if (C.dng_levels.LinearResponseLimit != 0.f && C.dng_levels.LinearResponseLimit != 1.f)
		add_metadata(kv, "Linear response limit", std::format("{:0.4}", C.dng_levels.LinearResponseLimit));

	if (C.ExifColorSpace != LIBRAW_COLORSPACE_Unknown)
		add_metadata(kv, "Exif color space", str::to_string(C.ExifColorSpace));

	if (Canon.ChannelBlackLevel[0])
	{
		add_metadata(kv, "Canon makernotes, ChannelBlackLevel", std::format("{} {} {} {}",
		                                                                    Canon.ChannelBlackLevel[0],
		                                                                    Canon.ChannelBlackLevel[1],
		                                                                    Canon.ChannelBlackLevel[2],
		                                                                    Canon.ChannelBlackLevel[3]));
	}

	if (C.black)
	{
		add_metadata(kv, "black", str::to_string(C.black));
	}

	add_metadata(kv, "Color space (makernotes)", std::format("{}, {}", P3.ColorSpace, ColorSpaceName));


	add_section(kv, "Sony");

	if (Sony.PixelShiftGroupID)
	{
		add_metadata(kv, "Sony PixelShiftGroupPrefix",
		             std::format("0x{:x} PixelShiftGroupID {}", Sony.PixelShiftGroupPrefix,
		                         Sony.PixelShiftGroupID));

		if (Sony.numInPixelShiftGroup)
		{
			add_metadata(kv, "shot#", std::format("{} (starts at 1) of total {}", Sony.numInPixelShiftGroup,
			                                      Sony.nShotsInPixelShiftGroup));
		}
		else
		{
			add_metadata(kv, "shots in PixelShiftGroup",
			             std::format("{}, already ARQ", Sony.nShotsInPixelShiftGroup));
		}
	}

	if (Sony.Sony0x9400_version)
	{
		add_metadata(kv, "SONY Sequence data", std::format("tag 0x9400 version '{:x}' ReleaseMode2: {}",
		                                                   Sony.Sony0x9400_version, Sony.Sony0x9400_ReleaseMode2));
		add_metadata(kv, "SequenceImageNumber",
		             std::format("{} (starts at zero)", Sony.Sony0x9400_SequenceImageNumber));
		add_metadata(kv, "SequenceLength1", std::format("{} shot(s)", Sony.Sony0x9400_SequenceLength1));
		add_metadata(kv, "SequenceFileNumber",
		             std::format("{} (starts at zero, exiftool starts at 1)", Sony.Sony0x9400_SequenceFileNumber));
		add_metadata(kv, "SequenceLength2", std::format("{} file(s)", Sony.Sony0x9400_SequenceLength2));
	}

	drop_empty_sections(kv);

	result.libraw_metadata = kv;
}

static ui::orientation calc_orientation(sizei image_extent, const ui::orientation& image_orientation,
                                        const libraw_data_t& image_data)
{
	// try detect of orientation is invalid
	auto full_image_extent = sizei(image_data.sizes.width, image_data.sizes.height);

	if (full_image_extent.cx != full_image_extent.cy)
	{
		const auto full_orientation = translate_libraw_orientation(image_data.sizes.flip);

		if (flips_xy(full_orientation))
		{
			std::swap(full_image_extent.cx, full_image_extent.cy);
		}

		if (flips_xy(image_orientation))
		{
			std::swap(image_extent.cx, image_extent.cy);
		}

		const auto dx_full = full_image_extent.is_empty() ? 0 : full_image_extent.cx * 10 / full_image_extent.cy;
		const auto dx_thumb = image_extent.is_empty() ? 0 : image_extent.cx * 10 / image_extent.cy;

		if (dx_full != dx_thumb)
		{
			return full_orientation;
		}
	}

	return image_orientation;
}


static ui::surface_ptr thumb_to_surface(const libraw_thumbnail_t& thumbnail, const ui::orientation orientation)
{
	ui::surface_ptr result;

	if (LIBRAW_THUMBNAIL_BITMAP == thumbnail.tformat)
	{
		const auto tcolors = thumbnail.tcolors;

		// Only 1 (monochrome), 3 (RGB) and 4 (RGBA) channel bitmaps are handled. Any other
		// channel count would leave the destination partially written (alloc does not zero),
		// so bail out rather than display uninitialised memory.
		if (tcolors != 1 && tcolors != 3 && tcolors != 4)
		{
			return result;
		}

		// 64-bit so the product of two file-supplied 16-bit dimensions cannot wrap and
		// defeat the bounds check below.
		const auto expected_size = static_cast<uint64_t>(thumbnail.theight) * thumbnail.twidth * tcolors;

		if (expected_size <= thumbnail.tlength)
		{
			auto temp_surface = std::make_shared<ui::surface>();

			if (temp_surface->alloc(thumbnail.twidth, thumbnail.theight, ui::texture_format::RGB, orientation))
			{
				for (auto y = 0; y < thumbnail.theight; ++y)
				{
					const auto* src = thumbnail.thumb + static_cast<size_t>(y) * thumbnail.twidth * tcolors;
					auto* dst = temp_surface->pixels_line(y);

					for (auto x = 0; x < thumbnail.twidth; ++x)
					{
						if (tcolors == 1)
						{
							const auto g = static_cast<uint8_t>(src[0]);
							*dst++ = g;
							*dst++ = g;
							*dst++ = g;
							*dst++ = 0;
							src += 1;
						}
						else if (tcolors == 3)
						{
							*dst++ = src[2];
							*dst++ = src[1];
							*dst++ = src[0];
							*dst++ = 0;
							src += 3;
						}
						else // tcolors == 4
						{
							*dst++ = src[2];
							*dst++ = src[1];
							*dst++ = src[0];
							*dst++ = src[3];
							src += 4;
						}
					}
				}

				// Only publish the surface once it is fully allocated and written.
				result = std::move(temp_surface);
			}
		}
	}

	return result;
}

// LibRaw 0.22 moved gamma_curve() and the libraw_internal_data member to a
// protected section. This thin subclass re-exposes them so the full-image
// decode path can replicate dcraw_make_mem_image's auto-brightness while
// reading imgdata.image directly (avoiding an extra full-frame copy).
class libraw_ex final : public LibRaw
{
public:
	using LibRaw::gamma_curve;
	using LibRaw::libraw_internal_data;
};

struct raw_processor
{
	// Declared first so the host outlives the LibRaw instance: ~LibRaw runs recycle(),
	// which releases the dng_negative/dng_image it allocated through this host.
	std::unique_ptr<dng_host> dng;
	std::unique_ptr<libraw_ex> processor;
};

// Full-image decode only. The Adobe SDK host belongs here and nowhere else: LibRaw reaches it from
// try_dngsdk() inside unpack(), which a header scan never calls.
static raw_processor create_decode_processor()
{
	raw_processor result;
	result.processor = std::make_unique<libraw_ex>();

	// Installing the host is what activates the Adobe SDK - LibRaw already defaults
	// rawparams.use_dngsdk to LIBRAW_DNG_DEFAULT (float, linear, deflate and 8-bit DNG), and unpack()
	// additionally routes DNG over 2GB, lossy DNG and JPEG-XL DNG through it. Everything else, notably
	// ordinary lossless-JPEG Bayer DNG, stays on LibRaw's faster native decoder: widening this to
	// LIBRAW_DNG_ALL would move the most common case onto the slower path for no accuracy gain.
	result.dng = std::make_unique<dng_host>();
	result.processor->set_dng_host(result.dng.get());

	// Apply the camera's white balance when decoding the full image, otherwise
	// the output has a strong colour cast (LibRaw defaults to no white balance).
	result.processor->imgdata.params.use_camera_wb = 1;

	return result;
}

// Borrows the calling thread's LibRaw instance for one header scan. Constructing LibRaw allocates
// and zero-fills roughly half a megabyte a scan never reads (libraw_data_t carries
// color.curve[0x10000], LibRaw_TLS carries ahd_data.cbrt[0x10000]), and indexing paid that twice
// per file - once in the constructor, once in the destructor's recycle(). recycle() restores the
// post-construction state, so one instance retained per scanning thread is equivalent and pays it
// once. The cost of that trade is the retained instance itself, on each thread that scans RAW.
class raw_scan_lease
{
public:
	raw_scan_lease() : _slot(thread_slot())
	{
		// Nested leases would silently share one instance and corrupt each other's parse state.
		df::assert_true(!_slot.in_use);
		_slot.in_use = true;
	}

	~raw_scan_lease()
	{
		// recycle() also closes the datastream, so the scanned file is not left open past the lease.
		_slot.processor.recycle();
		_slot.in_use = false;
	}

	raw_scan_lease(const raw_scan_lease&) = delete;
	raw_scan_lease& operator=(const raw_scan_lease&) = delete;

	libraw_ex* operator->() const { return &_slot.processor; }
	libraw_ex& operator*() const { return _slot.processor; }

private:
	struct slot
	{
		libraw_ex processor;
		bool in_use = false;
	};

	static slot& thread_slot()
	{
		static thread_local slot s;
		return s;
	}

	slot& _slot;
};

// Finds the index of the largest embedded JPEG thumbnail in LibRaw's thumbnail list,
// or -1 if there is none. Used as a fallback when the default (largest) thumbnail is a
// format we cannot decode - notably Canon CR3's H.265 preview, which is a proprietary
// CISZ-wrapped HEVC blob that LibRaw flags but does not decode. CR3 files also embed a
// standard JPEG thumbnail, so we pick that instead of trying to decode the HEVC preview.
// thumbs_list entries carry LIBRAW_INTERNAL_THUMBNAIL_* codes, a different enum from the
// LIBRAW_THUMBNAIL_* values in imgdata.thumbnail, so the two spellings below are deliberate.
static int find_largest_jpeg_thumb(const libraw_thumbnail_list_t& list)
{
	int best = -1;
	uint32_t best_area = 0;

	const auto count = std::min(static_cast<int>(list.thumbcount), static_cast<int>(std::size(list.thumblist)));

	for (auto i = 0; i < count; ++i)
	{
		const auto& item = list.thumblist[i];

		if (item.tformat == LIBRAW_INTERNAL_THUMBNAIL_JPEG && item.tlength > 0)
		{
			const auto area = static_cast<uint32_t>(item.twidth) * static_cast<uint32_t>(item.theight);

			if (best < 0 || area > best_area)
			{
				best = i;
				best_area = area;
			}
		}
	}

	return best;
}

// Unpacks the embedded thumbnail, preferring a format we can decode. The default (largest)
// thumbnail may be one we cannot - notably Canon CR3's H.265 preview - in which case fall
// back to the largest embedded JPEG. Returns false only when no thumbnail could be unpacked.
static bool unpack_decodable_thumb(libraw_ex& processor)
{
	if (processor.unpack_thumb() != LIBRAW_SUCCESS)
	{
		return false;
	}

	const auto format = processor.imgdata.thumbnail.tformat;

	if (format != LIBRAW_THUMBNAIL_JPEG && format != LIBRAW_THUMBNAIL_BITMAP)
	{
		const auto jpeg_index = find_largest_jpeg_thumb(processor.imgdata.thumbs_list);

		// unpack_thumb_ex overwrites the imgdata.thumbnail descriptor before it decodes, so on failure
		// tlength describes the JPEG entry while thumb still holds the old, undecodable payload.
		// Report that rather than leave the caller to read the mismatched pair.
		return jpeg_index >= 0 && processor.unpack_thumb_ex(jpeg_index) == LIBRAW_SUCCESS;
	}

	return true;
}

file_scan_result files::scan_raw(const df::file_path path, const std::string_view xmp_sidecar, const bool load_thumb,
                                 const sizei max, const scan_intent intent)
{
	file_scan_result result;
	const auto w = libraw_path(path);
	const raw_scan_lease processor;

	if (processor->open_file(w.c_str()) == LIBRAW_SUCCESS)
	{
		if (processor->adjust_sizes_info_only() == LIBRAW_SUCCESS)
		{
			populate_raw_metadata(result, processor->imgdata, intent);
			result.success = true;
		}

		if (load_thumb)
		{
			if (unpack_decodable_thumb(*processor))
			{
				const auto& t = processor->imgdata.thumbnail;

				if (LIBRAW_THUMBNAIL_JPEG == t.tformat &&
					t.tlength > 0 &&
					processor->imgdata.sizes.width > 0 &&
					processor->imgdata.sizes.height > 0)
				{
					const auto* const data = std::bit_cast<const uint8_t*>(t.thumb);
					const auto size = t.tlength;

					result.thumbnail_surface = image_to_surface(df::cspan{data, size}, max, false,
					                                           decode_intent::thumbnail);
				}
				else if (LIBRAW_THUMBNAIL_BITMAP == t.tformat && t.tlength > 0)
				{
					result.thumbnail_surface = thumb_to_surface(t, ui::orientation::top_left);
				}

				if (result.thumbnail_surface)
				{
					result.thumbnail_surface->orientation(calc_orientation(result.thumbnail_surface->dimensions(),
					                                                       result.thumbnail_surface->orientation(),
					                                                       processor->imgdata));
				}
			}
		}
	}

	if (!str::is_empty(xmp_sidecar))
	{
		result.metadata.xmp = blob_from_file(path.folder().combine_file(xmp_sidecar));
	}

	return result;
}


file_load_result load_raw(const df::file_path path, const bool can_load_preview)
{
	file_load_result result;

	const auto w = libraw_path(path);
	const auto rp = create_decode_processor();

	if (rp.processor->open_file(w.c_str()) == LIBRAW_SUCCESS)
	{
		const auto& image_data = rp.processor->imgdata;

		// The thumbnail is often large enough, so just use it
		if (can_load_preview && unpack_decodable_thumb(*rp.processor))
		{
			const auto& thumbnail = image_data.thumbnail;

			if (thumbnail.tlength > 0)
			{
				if (LIBRAW_THUMBNAIL_JPEG == thumbnail.tformat)
				{
					auto i = load_image_file(df::cspan(std::bit_cast<const uint8_t*>(thumbnail.thumb),
					                                   thumbnail.tlength));

					if (i)
					{
						i->orientation(calc_orientation(i->dimensions(), i->orientation(), rp.processor->imgdata));
						result.i = std::move(i);
					}
				}
				else if (LIBRAW_THUMBNAIL_BITMAP == thumbnail.tformat)
				{
					auto s = thumb_to_surface(thumbnail, ui::orientation::top_left);

					if (s)
					{
						s->orientation(calc_orientation(s->dimensions(), ui::orientation::top_left,
						                                rp.processor->imgdata));
						result.s = std::move(s);
					}
				}

				if (is_valid(result.i) || is_valid(result.s))
				{
					result.success = true;
					result.is_preview = true;
				}
			}
		}

		if (!result.success)
		{
			// Decode full image
			if (rp.processor->unpack() == LIBRAW_SUCCESS)
			{
				if (rp.processor->dcraw_process() == LIBRAW_SUCCESS)
				{
					const auto& libraw_internal_data = rp.processor->libraw_internal_data;
					const auto& S = image_data.sizes;
					const auto& IO = libraw_internal_data.internal_output_params;
					const auto& P1 = image_data.idata;
					const auto& O = image_data.params;

					// get_mem_image_format() reports P1.colors, so read it directly rather
					// than round-tripping through dimensions we do not use (it also swaps
					// width/height for flipped images, which we handle via orientation).
					const auto colors = P1.colors;

					if (colors != 1 && colors < 3)
					{
						df::log(__FUNCTION__,
						        std::format("unsupported RAW channel count {} for {}", colors, path.name()));
						return result;
					}

					// After dcraw_process, imgdata.image holds exactly sizes.height rows of
					// sizes.width pixels (pre_interpolate, fuji_rotate and stretch all keep
					// width/height in step with the allocation), so the raster below can walk
					// it linearly. Guard anyway: a null, empty or shrunk image (half_size,
					// aber or threshold set) would otherwise read out of bounds.
					if (!image_data.image || S.width <= 0 || S.height <= 0 ||
						S.iwidth != S.width || S.iheight != S.height)
					{
						df::log(__FUNCTION__, std::format("empty RAW image for {}", path.name()));
						return result;
					}

					// Read imgdata.image[] directly into the surface (skipping
					// dcraw_make_mem_image to avoid a second full-frame copy). This block
					// mirrors LibRaw's dcraw_make_mem_image auto-brightness (see
					// third-party/LibRaw/src/postprocessing/mem_image.cpp) and reaches into
					// libraw_internal_data internals, so keep it in sync on LibRaw upgrades.
					if (libraw_internal_data.output_data.histogram)
					{
						int val, total, t_white = 0x2000, c;
						/* 99th percentile white level */
						auto perc = df::round(
							static_cast<double>(S.width) * static_cast<double>(S.height) * O.auto_bright_thr);

						if (IO.fuji_width) perc /= 2;

						if (!(O.highlight & ~2 || O.no_auto_bright))
						{
							for (t_white = c = 0; c < P1.colors; c++)
							{
								for (val = 0x2000, total = 0; --val > 32;)
								{
									if ((total += libraw_internal_data.output_data.histogram[c][val]) > perc)
										break;
								}

								if (t_white < val) t_white = val;
							}
						}
						rp.processor->gamma_curve(O.gamm[0], O.gamm[1], 2, df::round((t_white << 3) / O.bright));
					}

					auto temp_surface = std::make_shared<ui::surface>();
					const auto orientation = translate_libraw_orientation(S.flip);
					auto* const pixel_buffer = temp_surface->alloc(S.width, S.height, ui::texture_format::RGB,
					                                               orientation);

					if (pixel_buffer)
					{
						const auto stride = temp_surface->stride();
						const auto& color_curve = image_data.color.curve;
						size_t i = 0;

						for (auto y = 0; y < S.height; y++)
						{
							auto* bufp = std::bit_cast<ui::color32*>(pixel_buffer + y * stride);

							for (auto x = 0; x < S.width; x++)
							{
								const auto* const id = image_data.image[i++];
								if (colors == 1)
								{
									const auto gray = color_curve[id[0]] >> 8;
									*bufp++ = ui::rgb(gray, gray, gray);
								}
								else
								{
									*bufp++ = color_curve[id[2]] >> 8 | 0xFF00 & color_curve[id[1]] | 0xFF0000 &
										color_curve[id[0]] << 8;
								}
							}
						}

						// Only publish the surface once it is fully allocated and written.
						result.s = std::move(temp_surface);
						result.success = true;
					}
				}
				else
				{
					df::log(__FUNCTION__, std::format("dcraw_process failed for {}", path.name()));
				}
			}
			else
			{
				df::log(__FUNCTION__, std::format("unpack failed for {}", path.name()));
			}
		}
	}
	else
	{
		df::log(__FUNCTION__, std::format("open_file failed for {}", path.name()));
	}

	return result;
}
