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
#include <LibRaw/libraw.h>

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

static ui::orientation transate_libraw_orientation(const int flip)
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
		kv.emplace_back(str::cache(name), val);
	}
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

static void populate_raw_metadata(file_scan_result& result, const libraw_data_t& data)
{
	result.width = data.sizes.width;
	result.height = data.sizes.height;

	if (data.sizes.flip)
	{
		result.orientation = transate_libraw_orientation(data.sizes.flip);
	}

	if (data.idata.xmpdata && data.idata.xmplen > 0)
	{
		result.metadata.xmp.assign(data.idata.xmpdata, data.idata.xmpdata + data.idata.xmplen);
	}

	const auto created = df::date_t::from_time_t(data.other.timestamp);
	if (created.is_valid()) result.created_utc = created;

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
	if (gps.gpsparsed)
	{
		const auto lat = gps_coordinate::dms_to_decimal(gps.latitude[0], gps.latitude[1], gps.latitude[2]);
		const auto lon = gps_coordinate::dms_to_decimal(gps.longitude[0], gps.longitude[1], gps.longitude[2]);

		result.gps._latitude = (gps.latref == 'S') ? -lat : lat;
		result.gps._longitude = (gps.longref == 'W') ? -lon : lon;
	}

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

	if (P1.dng_version)
	{
		std::string s;
		for (int i = 24; i >= 0; i -= 8)
			s += std::format("{}{}", P1.dng_version >> i & 255, i ? '.' : ' ');

		add_metadata(kv, "DNG Version", s);
	}


	add_metadata(kv, "MinFocal", std::format("{:0.1} mm", exifLens.MinFocal));
	add_metadata(kv, "MaxFocal", std::format("{:0.1} mm", exifLens.MaxFocal));
	add_metadata(kv, "MaxAp @MinFocal", std::format("f/{:0.1}", exifLens.MaxAp4MinFocal));
	add_metadata(kv, "MaxAp @MaxFocal", std::format("f/{:0.1}", exifLens.MaxAp4MaxFocal));
	add_metadata(kv, "CurFocal", std::format("{:0.1} mm", P2.focal_len));
	add_metadata(kv, "MaxAperture @CurFocal", std::format("f/{:0.1}", exifLens.EXIF_MaxAp));
	add_metadata(kv, "FocalLengthIn35mmFormat", std::format("{} mm", exifLens.FocalLengthIn35mmFormat));
	add_metadata(kv, "LensMake", exifLens.LensMake);
	add_metadata(kv, "Lens", exifLens.Lens);


	add_metadata(kv, "DriveMode", str::to_string(ShootingInfo.DriveMode));
	add_metadata(kv, "FocusMode", str::to_string(ShootingInfo.FocusMode));
	add_metadata(kv, "MeteringMode", str::to_string(ShootingInfo.MeteringMode));
	add_metadata(kv, "AFPoint", str::to_string(ShootingInfo.AFPoint));
	add_metadata(kv, "ExposureMode", str::to_string(ShootingInfo.ExposureMode));
	add_metadata(kv, "ExposureProgram", str::to_string(ShootingInfo.ExposureProgram));
	add_metadata(kv, "ImageStabilization", str::to_string(ShootingInfo.ImageStabilization));

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

	if (C.profile)
		add_metadata(kv, "Embedded ICC profile", std::format("yes, {} bytes", C.profile_length));
	else
		add_metadata(kv, "Embedded ICC profile", "no");

	if (C.dng_levels.baseline_exposure > -999.f)
		add_metadata(kv, "Baseline exposure", std::format("{:.3f}", C.dng_levels.baseline_exposure));

	add_metadata(kv, "Number of raw images", str::to_string(P1.raw_count));

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

	if (Canon.RecordMode)
		add_id2hr_metadata(kv, "Canon record mode", Canon.RecordMode, CanonRecordModes);
	if (Canon.SensorWidth)
		add_metadata(kv, "SensorWidth", str::to_string(Canon.SensorWidth));
	if (Canon.SensorHeight)
		add_metadata(kv, "SensorHeight", str::to_string(Canon.SensorHeight));

	if (Hasselblad.BaseISO)
		add_metadata(kv, "Hasselblad base ISO", str::to_string(Hasselblad.BaseISO));
	if (Hasselblad.Gain)
		add_metadata(kv, "Hasselblad gain", str::to_string(Hasselblad.Gain, 3));

	add_metadata(kv, "Raw colors", str::to_string(P1.colors));

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

	result.libraw_metadata = kv;
}

static ui::orientation calc_orientation(sizei image_extent, const ui::orientation& image_orientation,
                                        const libraw_data_t& image_data)
{
	// try detect of orientation is invalid
	auto full_image_extent = sizei(image_data.sizes.width, image_data.sizes.height);

	if (full_image_extent.cx != full_image_extent.cy)
	{
		const auto full_orientation = transate_libraw_orientation(image_data.sizes.flip);

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

		const auto expected_size = thumbnail.theight * thumbnail.twidth * static_cast<uint32_t>(tcolors);

		if (expected_size <= thumbnail.tlength)
		{
			result = std::make_shared<ui::surface>();

			if (result->alloc(thumbnail.twidth, thumbnail.theight, ui::texture_format::RGB, orientation))
			{
				for (auto y = 0; y < thumbnail.theight; ++y)
				{
					const auto* src = thumbnail.thumb + y * thumbnail.twidth * tcolors;
					auto* dst = result->pixels_line(y);

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
			}
		}
	}

	return result;
}

// LibRaw 0.22 moved gamma_curve() and the libraw_internal_data member to a
// protected section. This thin subclass re-exposes them so the full-image
// decode path can replicate dcraw_make_mem_image's auto-brightness while
// reading imgdata.image directly (avoiding an extra full-frame copy).
class libraw_ex : public LibRaw
{
public:
	using LibRaw::gamma_curve;
	using LibRaw::libraw_internal_data;
};

struct raw_processor
{
	std::unique_ptr<libraw_ex> processor;
	std::unique_ptr<dng_host> dng;
};

static raw_processor create_processor()
{
	raw_processor result;
	result.processor = std::make_unique<libraw_ex>();

	//iProcessor.set_exifparser_handler(exif_callback, &context);

	result.dng = std::make_unique<dng_host>();
	result.processor->set_dng_host(result.dng.get());
	//result.processor->imgdata.params.use_dngsdk = LIBRAW_DNG_ALL;
	// LIBRAW_DNG_FLOAT | LIBRAW_DNG_LINEAR | LIBRAW_DNG_XTRANS | LIBRAW_DNG_OTHER;

	// Apply the camera's white balance when decoding the full image, otherwise
	// the output has a strong colour cast (LibRaw defaults to no white balance).
	result.processor->imgdata.params.use_camera_wb = 1;

	return result;
}

// Finds the index of the largest embedded JPEG thumbnail in LibRaw's thumbnail list,
// or -1 if there is none. Used as a fallback when the default (largest) thumbnail is a
// format we cannot decode - notably Canon CR3's H.265 preview, which is a proprietary
// CISZ-wrapped HEVC blob that LibRaw flags but does not decode. CR3 files also embed a
// standard JPEG thumbnail, so we pick that instead of trying to decode the HEVC preview.
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

file_scan_result files::scan_raw(const df::file_path path, const std::string_view xmp_sidecar, const bool load_thumb,
                                 const sizei max)
{
	file_scan_result result;
	const auto w = platform::to_file_system_path(path);
	const auto rp = create_processor();

	if (rp.processor->open_file(w.c_str()) == LIBRAW_SUCCESS)
	{
		if (rp.processor->adjust_sizes_info_only() == LIBRAW_SUCCESS)
		{
			populate_raw_metadata(result, rp.processor->imgdata);
			result.success = true;
		}

		if (load_thumb)
		{
			if (rp.processor->unpack_thumb() == LIBRAW_SUCCESS)
			{
				// The default (largest) thumbnail may be a format we cannot decode - notably
				// Canon CR3's H.265 preview. In that case fall back to the largest embedded
				// JPEG thumbnail, which reuses the standard JPEG decode path below.
				if (rp.processor->imgdata.thumbnail.tformat != LIBRAW_THUMBNAIL_JPEG &&
					rp.processor->imgdata.thumbnail.tformat != LIBRAW_THUMBNAIL_BITMAP)
				{
					const auto jpeg_index = find_largest_jpeg_thumb(rp.processor->imgdata.thumbs_list);

					if (jpeg_index >= 0)
					{
						rp.processor->unpack_thumb_ex(jpeg_index);
					}
				}

				const auto& t = rp.processor->imgdata.thumbnail;

				if (LIBRAW_THUMBNAIL_JPEG == t.tformat &&
					t.tlength > 0 &&
					rp.processor->imgdata.sizes.width > 0 &&
					rp.processor->imgdata.sizes.height > 0)
				{
					const auto* const data = std::bit_cast<const uint8_t*>(t.thumb);
					const auto size = t.tlength;

					result.thumbnail_surface = image_to_surface(df::cspan{data, size}, max);
				}
				else if (LIBRAW_THUMBNAIL_BITMAP == t.tformat && t.tlength > 0)
				{
					result.thumbnail_surface = thumb_to_surface(t, ui::orientation::top_left);
				}

				if (result.thumbnail_surface)
				{
					result.thumbnail_surface->orientation(calc_orientation(result.thumbnail_surface->dimensions(),
					                                                       result.thumbnail_surface->orientation(),
					                                                       rp.processor->imgdata));
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

	const auto w = platform::to_file_system_path(path);
	const auto rp = create_processor();

	if (rp.processor->open_file(w.c_str()) == LIBRAW_SUCCESS)
	{
		file_scan_result md;
		const auto& image_data = rp.processor->imgdata;
		populate_raw_metadata(md, image_data);

		// The thumbnail often large enough, lets just use it :)
		if (can_load_preview && rp.processor->unpack_thumb() == LIBRAW_SUCCESS)
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
					int width, height, colors, bps;
					rp.processor->get_mem_image_format(&width, &height, &colors, &bps);

					const auto& libraw_internal_data = rp.processor->libraw_internal_data;
					const auto& S = image_data.sizes;
					const auto& IO = libraw_internal_data.internal_output_params;
					const auto& P1 = image_data.idata;
					const auto& O = image_data.params;

					// Read imgdata.image[] directly into the surface (skipping
					// dcraw_make_mem_image to avoid a second full-frame copy). This block
					// mirrors LibRaw's dcraw_make_mem_image auto-brightness (see
					// third-party/LibRaw/src/postprocessing/mem_image.cpp) and reaches into
					// libraw_internal_data internals, so keep it in sync on LibRaw upgrades.
					if (libraw_internal_data.output_data.histogram)
					{
						int val, total, t_white = 0x2000, c;
						int perc = df::round(S.width * S.height * O.auto_bright_thr); /* 99th percentile white level */

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

					auto s = std::make_shared<ui::surface>();
					const auto orientation = transate_libraw_orientation(image_data.sizes.flip);
					const auto* const pixel_buffer = s->alloc(S.width, S.height, ui::texture_format::RGB, orientation);
					const auto stride = s->stride();
					result.s = std::move(s);

					if (pixel_buffer)
					{
						const auto& color_curve = image_data.color.curve;
						int i = 0;

						for (auto y = 0; y < S.height; y++)
						{
							auto* bufp = std::bit_cast<COLORREF*>(pixel_buffer + y * stride);

							for (auto x = 0; x < S.width; x++)
							{
								const auto* const id = image_data.image[i++];
								*bufp++ = color_curve[id[2]] >> 8 | 0xFF00 & color_curve[id[1]] | 0xFF0000 &
									color_curve[id[0]] << 8;
							}
						}

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
