// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY
//
// Based on code from LibRAW

#include "pch.h"
#include "files.h"

#define LIBRAW_NODLL 
#define LIBRAW_LIBRARY_BUILD
#define LIBRAW_WIN32_UNICODEPATHS
#define USE_DNGSDK

#include "dng/dng_host.h"
#include <LibRaw/libraw.h>

/*
table of fluorescents:
12 = FL-D; Daylight fluorescent (D 5700K – 7100K) (F1,F5)
13 = FL-N; Day white fluorescent (N 4600K – 5400K) (F7,F8)
14 = FL-W; Cool white fluorescent (W 3900K – 4500K) (F2,F6, office, store,warehouse)
15 = FL-WW; White fluorescent (WW 3200K – 3700K) (F3, residential)
16 = FL-L; Soft/Warm white fluorescent (L 2600K - 3250K) (F4, kitchen, bath)
*/

static constexpr struct
{
	const int NumId;
	std::u8string_view StrId;
	std::u8string_view hrStrId; // human-readable
	const int aux_setting;
} WBToStr[] = {
	{LIBRAW_WBI_Unknown, u8"WBI_Unknown", u8"Unknown", 0},
	{LIBRAW_WBI_Daylight, u8"WBI_Daylight", u8"Daylight", 0},
	{LIBRAW_WBI_Fluorescent, u8"WBI_Fluorescent", u8"Fluorescent", 0},
	{LIBRAW_WBI_Tungsten, u8"WBI_Tungsten", u8"Tungsten (Incandescent)", 0},
	{LIBRAW_WBI_Flash, u8"WBI_Flash", u8"Flash", 0},
	{LIBRAW_WBI_FineWeather, u8"WBI_FineWeather", u8"Fine Weather", 0},
	{LIBRAW_WBI_Cloudy, u8"WBI_Cloudy", u8"Cloudy", 0},
	{LIBRAW_WBI_Shade, u8"WBI_Shade", u8"Shade", 0},
	{LIBRAW_WBI_FL_D, u8"WBI_FL_D", u8"Daylight Fluorescent", 0},
	{LIBRAW_WBI_FL_N, u8"WBI_FL_N", u8"Day White Fluorescent", 0},
	{LIBRAW_WBI_FL_W, u8"WBI_FL_W", u8"Cool White Fluorescent", 0},
	{LIBRAW_WBI_FL_WW, u8"WBI_FL_WW", u8"White Fluorescent", 0},
	{LIBRAW_WBI_FL_L, u8"WBI_FL_L", u8"Warm White Fluorescent", 0},
	{LIBRAW_WBI_Ill_A, u8"WBI_Ill_A", u8"Illuminant A", 0},
	{LIBRAW_WBI_Ill_B, u8"WBI_Ill_B", u8"Illuminant B", 0},
	{LIBRAW_WBI_Ill_C, u8"WBI_Ill_C", u8"Illuminant C", 0},
	{LIBRAW_WBI_D55, u8"WBI_D55", u8"D55", 0},
	{LIBRAW_WBI_D65, u8"WBI_D65", u8"D65", 0},
	{LIBRAW_WBI_D75, u8"WBI_D75", u8"D75", 0},
	{LIBRAW_WBI_D50, u8"WBI_D50", u8"D50", 0},
	{LIBRAW_WBI_StudioTungsten, u8"WBI_StudioTungsten", u8"ISO Studio Tungsten", 0},
	{LIBRAW_WBI_BW, u8"WBI_BW", u8"BW", 0},
	{LIBRAW_WBI_Other, u8"WBI_Other", u8"Other", 0},
	{LIBRAW_WBI_Sunset, u8"WBI_Sunset", u8"Sunset", 1},
	{LIBRAW_WBI_Underwater, u8"WBI_Underwater", u8"Underwater", 1},
	{LIBRAW_WBI_FluorescentHigh, u8"WBI_FluorescentHigh", u8"Fluorescent High", 1},
	{LIBRAW_WBI_HT_Mercury, u8"WBI_HT_Mercury", u8"HT Mercury", 1},
	{LIBRAW_WBI_AsShot, u8"WBI_AsShot", u8"As Shot", 1},
	{LIBRAW_WBI_Measured, u8"WBI_Measured", u8"Camera Measured", 1},
	{LIBRAW_WBI_Auto, u8"WBI_Auto", u8"Camera Auto", 1},
	{LIBRAW_WBI_Auto1, u8"WBI_Auto1", u8"Camera Auto 1", 1},
	{LIBRAW_WBI_Auto2, u8"WBI_Auto2", u8"Camera Auto 2", 1},
	{LIBRAW_WBI_Auto3, u8"WBI_Auto3", u8"Camera Auto 3", 1},
	{LIBRAW_WBI_Auto4, u8"WBI_Auto4", u8"Camera Auto 4", 1},
	{LIBRAW_WBI_Custom, u8"WBI_Custom", u8"Custom", 1},
	{LIBRAW_WBI_Custom1, u8"WBI_Custom1", u8"Custom 1", 1},
	{LIBRAW_WBI_Custom2, u8"WBI_Custom2", u8"Custom 2", 1},
	{LIBRAW_WBI_Custom3, u8"WBI_Custom3", u8"Custom 3", 1},
	{LIBRAW_WBI_Custom4, u8"WBI_Custom4", u8"Custom 4", 1},
	{LIBRAW_WBI_Custom5, u8"WBI_Custom5", u8"Custom 5", 1},
	{LIBRAW_WBI_Custom6, u8"WBI_Custom6", u8"Custom 6", 1},
	{LIBRAW_WBI_PC_Set1, u8"WBI_PC_Set1", u8"PC Set 1", 1},
	{LIBRAW_WBI_PC_Set2, u8"WBI_PC_Set2", u8"PC Set 2", 1},
	{LIBRAW_WBI_PC_Set3, u8"WBI_PC_Set3", u8"PC Set 3", 1},
	{LIBRAW_WBI_PC_Set4, u8"WBI_PC_Set4", u8"PC Set 4", 1},
	{LIBRAW_WBI_PC_Set5, u8"WBI_PC_Set5", u8"PC Set 5", 1},
	{LIBRAW_WBI_Kelvin, u8"WBI_Kelvin", u8"Kelvin", 1},
	{LIBRAW_WBI_None, u8"WBI_None", u8"None", 1},
};

struct id2hr_t
{
	uint64_t id;
	const char8_t* name;
}; // id to human readable

static constexpr id2hr_t MountNames[] = {
	{LIBRAW_MOUNT_Alpa, u8"Alpa"},
	{LIBRAW_MOUNT_C, u8"C-mount"},
	{LIBRAW_MOUNT_Canon_EF_M, u8"Canon EF-M"},
	{LIBRAW_MOUNT_Canon_EF_S, u8"Canon EF-S"},
	{LIBRAW_MOUNT_Canon_EF, u8"Canon EF"},
	{LIBRAW_MOUNT_Canon_RF, u8"Canon RF"},
	{LIBRAW_MOUNT_Contax_N, u8"Contax N"},
	{LIBRAW_MOUNT_Contax645, u8"Contax 645"},
	{LIBRAW_MOUNT_FT, u8"4/3"},
	{LIBRAW_MOUNT_mFT, u8"m4/3"},
	{LIBRAW_MOUNT_Fuji_GF, u8"Fuji G"}, // Fujifilm G lenses, GFX cameras
	{LIBRAW_MOUNT_Fuji_GX, u8"Fuji GX"}, // GX680
	{LIBRAW_MOUNT_Fuji_X, u8"Fuji X"},
	{LIBRAW_MOUNT_Hasselblad_H, u8"Hasselblad H"}, // Hn cameras, HC & HCD lenses
	{LIBRAW_MOUNT_Hasselblad_V, u8"Hasselblad V"},
	{LIBRAW_MOUNT_Hasselblad_XCD, u8"Hasselblad XCD"}, // Xn cameras, XCD lenses
	{LIBRAW_MOUNT_Leica_M, u8"Leica M"},
	{LIBRAW_MOUNT_Leica_R, u8"Leica R"},
	{LIBRAW_MOUNT_Leica_S, u8"Leica S"},
	{LIBRAW_MOUNT_Leica_SL, u8"Leica SL"}, // mounts on u8"L"throat
	{LIBRAW_MOUNT_Leica_TL, u8"Leica TL"}, // mounts on u8"L"throat
	{LIBRAW_MOUNT_LPS_L, u8"LPS L-mount"}, // throat, Leica / Panasonic / Sigma
	{LIBRAW_MOUNT_Mamiya67, u8"Mamiya RZ/RB"}, // Mamiya RB67, RZ67
	{LIBRAW_MOUNT_Mamiya645, u8"Mamiya 645"},
	{LIBRAW_MOUNT_Minolta_A, u8"Sony/Minolta A"},
	{LIBRAW_MOUNT_Nikon_CX, u8"Nikkor 1"},
	{LIBRAW_MOUNT_Nikon_F, u8"Nikkor F"},
	{LIBRAW_MOUNT_Nikon_Z, u8"Nikkor Z"},
	{LIBRAW_MOUNT_Pentax_645, u8"Pentax 645"},
	{LIBRAW_MOUNT_Pentax_K, u8"Pentax K"},
	{LIBRAW_MOUNT_Pentax_Q, u8"Pentax Q"},
	{LIBRAW_MOUNT_RicohModule, u8"Ricoh module"},
	{LIBRAW_MOUNT_Rollei_bayonet, u8"Rollei bayonet"}, // Rollei Hy-6: Leaf AFi, Sinar Hy6- models
	{LIBRAW_MOUNT_Samsung_NX_M, u8"Samsung NX-M"},
	{LIBRAW_MOUNT_Samsung_NX, u8"Samsung NX"},
	{LIBRAW_MOUNT_Sigma_X3F, u8"Sigma SA/X3F"},
	{LIBRAW_MOUNT_Sony_E, u8"Sony E"},
	// generic formats:
	{LIBRAW_MOUNT_LF, u8"Large format"},
	{LIBRAW_MOUNT_DigitalBack, u8"Digital Back"},
	{LIBRAW_MOUNT_FixedLens, u8"Fixed Lens"},
	{LIBRAW_MOUNT_IL_UM, u8"Interchangeable lens, mount unknown"},
	{LIBRAW_MOUNT_Unknown, u8"Undefined Mount or Fixed Lens"},
	{LIBRAW_MOUNT_TheLastOne, u8"The Last One"},
};

static constexpr id2hr_t FormatNames[] = {
	{LIBRAW_FORMAT_1div2p3INCH, u8"1/2.3\""},
	{LIBRAW_FORMAT_1div1p7INCH, u8"1/1.7\""},
	{LIBRAW_FORMAT_1INCH, u8"1\""},
	{LIBRAW_FORMAT_FT, u8"4/3"},
	{LIBRAW_FORMAT_APSC, u8"APS-C"}, // Canon: 22.3x14.9mm; Sony et al: 23.6-23.7x15.6mm
	{LIBRAW_FORMAT_Leica_DMR, u8"Leica DMR"}, // 26.4x 17.6mm
	{LIBRAW_FORMAT_APSH, u8"APS-H"}, // Canon: 27.9x18.6mm
	{LIBRAW_FORMAT_FF, u8"FF 35mm"},
	{LIBRAW_FORMAT_CROP645, u8"645 crop 44x33mm"},
	{LIBRAW_FORMAT_LeicaS, u8"Leica S 45x30mm"},
	{LIBRAW_FORMAT_3648, u8"48x36mm"},
	{LIBRAW_FORMAT_645, u8"6x4.5"},
	{LIBRAW_FORMAT_66, u8"6x6"},
	{LIBRAW_FORMAT_67, u8"6x7"},
	{LIBRAW_FORMAT_68, u8"6x8"},
	{LIBRAW_FORMAT_69, u8"6x9"},
	{LIBRAW_FORMAT_SigmaAPSC, u8"Sigma APS-C"}, //  Sigma Foveon X3 orig: 20.7x13.8mm
	{LIBRAW_FORMAT_SigmaMerrill, u8"Sigma Merrill"},
	{LIBRAW_FORMAT_SigmaAPSH, u8"Sigma APS-H"}, // Sigma u8"H"26.7 x 17.9mm
	{LIBRAW_FORMAT_MF, u8"Medium Format"},
	{LIBRAW_FORMAT_LF, u8"Large format"},
	{LIBRAW_FORMAT_Unknown, u8"Unknown"},
	{LIBRAW_FORMAT_TheLastOne, u8"The Last One"},
};

static constexpr id2hr_t NikonCrops[] = {
	{0, u8"Uncropped"}, {1, u8"1.3x"}, {2, u8"DX"},
	{3, u8"5:4"}, {4, u8"3:2"}, {6, u8"16:9"},
	{8, u8"2.7x"}, {9, u8"DX Movie"}, {10, u8"1.3x Movie"},
	{11, u8"FX Uncropped"}, {12, u8"DX Uncropped"}, {15, u8"1.5x Movie"},
	{17, u8"1:1"},
};
#define nNikonCrops (sizeof(NikonCrops) / sizeof(id2hr_t))

static constexpr id2hr_t FujiCrops[] = {
	{0, u8"Uncropped"},
	{1, u8"GFX FF"},
	{2, u8"Sports Finder Mode"},
	{4, u8"Electronic Shutter 1.25x Crop"},
};
#define nFujiCrops (sizeof(FujiCrops) / sizeof(id2hr_t))

static constexpr id2hr_t FujiDriveModes[] = {
	{0, u8"Single Frame"},
	{1, u8"Continuous Low"},
	{2, u8"Continuous High"},
};
#define nFujiDriveModes (sizeof(FujiDriveModes) / sizeof(id2hr_t))

static constexpr id2hr_t AspectRatios[] = {
	{LIBRAW_IMAGE_ASPECT_UNKNOWN, u8"Unknown"}, {LIBRAW_IMAGE_ASPECT_3to2, u8"3:2"},
	{LIBRAW_IMAGE_ASPECT_1to1, u8"1:1"}, {LIBRAW_IMAGE_ASPECT_4to3, u8"4:3"},
	{LIBRAW_IMAGE_ASPECT_16to9, u8"16:9"}, {LIBRAW_IMAGE_ASPECT_5to4, u8"5:4"},
	{LIBRAW_IMAGE_ASPECT_OTHER, u8"Other"},
};
#define nAspectRatios (sizeof(AspectRatios) / sizeof(id2hr_t))

static constexpr id2hr_t CanonRecordModes[] = {
	{LIBRAW_Canon_RecordMode_JPEG, u8"JPEG"},
	{LIBRAW_Canon_RecordMode_CRW_THM, u8"CRW+THM"},
	{LIBRAW_Canon_RecordMode_AVI_THM, u8"AVI+THM"},
	{LIBRAW_Canon_RecordMode_TIF, u8"TIF"},
	{LIBRAW_Canon_RecordMode_TIF_JPEG, u8"TIF+JPEG"},
	{LIBRAW_Canon_RecordMode_CR2, u8"CR2"},
	{LIBRAW_Canon_RecordMode_CR2_JPEG, u8"CR2+JPEG"},
	{LIBRAW_Canon_RecordMode_UNKNOWN, u8"Unknown"},
	{LIBRAW_Canon_RecordMode_MOV, u8"MOV"},
	{LIBRAW_Canon_RecordMode_MP4, u8"MP4"},
	{LIBRAW_Canon_RecordMode_CRM, u8"CRM"},
	{LIBRAW_Canon_RecordMode_CR3, u8"CR3"},
	{LIBRAW_Canon_RecordMode_CR3_JPEG, u8"CR3+JPEG"},
	{LIBRAW_Canon_RecordMode_HEIF, u8"HEIF"},
	{LIBRAW_Canon_RecordMode_CR3_HEIF, u8"CR3+HEIF"},
};
#define nCanonRecordModes LIBRAW_Canon_RecordMode_TheLastOne

static constexpr struct
{
	const int NumId;
	std::u8string_view StrId;
} CorpToStr[] = {
	{LIBRAW_CAMERAMAKER_Agfa, u8"Agfa"},
	{LIBRAW_CAMERAMAKER_Alcatel, u8"Alcatel"},
	{LIBRAW_CAMERAMAKER_Apple, u8"Apple"},
	{LIBRAW_CAMERAMAKER_Aptina, u8"Aptina"},
	{LIBRAW_CAMERAMAKER_AVT, u8"AVT"},
	{LIBRAW_CAMERAMAKER_Baumer, u8"Baumer"},
	{LIBRAW_CAMERAMAKER_Broadcom, u8"Broadcom"},
	{LIBRAW_CAMERAMAKER_Canon, u8"Canon"},
	{LIBRAW_CAMERAMAKER_Casio, u8"Casio"},
	{LIBRAW_CAMERAMAKER_CINE, u8"CINE"},
	{LIBRAW_CAMERAMAKER_Clauss, u8"Clauss"},
	{LIBRAW_CAMERAMAKER_Contax, u8"Contax"},
	{LIBRAW_CAMERAMAKER_Creative, u8"Creative"},
	{LIBRAW_CAMERAMAKER_DJI, u8"DJI"},
	{LIBRAW_CAMERAMAKER_DXO, u8"DXO"},
	{LIBRAW_CAMERAMAKER_Epson, u8"Epson"},
	{LIBRAW_CAMERAMAKER_Foculus, u8"Foculus"},
	{LIBRAW_CAMERAMAKER_Fujifilm, u8"Fujifilm"},
	{LIBRAW_CAMERAMAKER_Generic, u8"Generic"},
	{LIBRAW_CAMERAMAKER_Gione, u8"Gione"},
	{LIBRAW_CAMERAMAKER_GITUP, u8"GITUP"},
	{LIBRAW_CAMERAMAKER_Google, u8"Google"},
	{LIBRAW_CAMERAMAKER_GoPro, u8"GoPro"},
	{LIBRAW_CAMERAMAKER_Hasselblad, u8"Hasselblad"},
	{LIBRAW_CAMERAMAKER_HTC, u8"HTC"},
	{LIBRAW_CAMERAMAKER_I_Mobile, u8"I_Mobile"},
	{LIBRAW_CAMERAMAKER_Imacon, u8"Imacon"},
	{LIBRAW_CAMERAMAKER_Kodak, u8"Kodak"},
	{LIBRAW_CAMERAMAKER_Konica, u8"Konica"},
	{LIBRAW_CAMERAMAKER_Leaf, u8"Leaf"},
	{LIBRAW_CAMERAMAKER_Leica, u8"Leica"},
	{LIBRAW_CAMERAMAKER_Lenovo, u8"Lenovo"},
	{LIBRAW_CAMERAMAKER_LG, u8"LG"},
	{LIBRAW_CAMERAMAKER_Logitech, u8"Logitech"},
	{LIBRAW_CAMERAMAKER_Mamiya, u8"Mamiya"},
	{LIBRAW_CAMERAMAKER_Matrix, u8"Matrix"},
	{LIBRAW_CAMERAMAKER_Meizu, u8"Meizu"},
	{LIBRAW_CAMERAMAKER_Micron, u8"Micron"},
	{LIBRAW_CAMERAMAKER_Minolta, u8"Minolta"},
	{LIBRAW_CAMERAMAKER_Motorola, u8"Motorola"},
	{LIBRAW_CAMERAMAKER_NGM, u8"NGM"},
	{LIBRAW_CAMERAMAKER_Nikon, u8"Nikon"},
	{LIBRAW_CAMERAMAKER_Nokia, u8"Nokia"},
	{LIBRAW_CAMERAMAKER_Olympus, u8"Olympus"},
	{LIBRAW_CAMERAMAKER_OmniVison, u8"OmniVison"},
	{LIBRAW_CAMERAMAKER_Panasonic, u8"Panasonic"},
	{LIBRAW_CAMERAMAKER_Parrot, u8"Parrot"},
	{LIBRAW_CAMERAMAKER_Pentax, u8"Pentax"},
	{LIBRAW_CAMERAMAKER_PhaseOne, u8"PhaseOne"},
	{LIBRAW_CAMERAMAKER_PhotoControl, u8"PhotoControl"},
	{LIBRAW_CAMERAMAKER_Photron, u8"Photron"},
	{LIBRAW_CAMERAMAKER_Pixelink, u8"Pixelink"},
	{LIBRAW_CAMERAMAKER_Polaroid, u8"Polaroid"},
	{LIBRAW_CAMERAMAKER_RED, u8"RED"},
	{LIBRAW_CAMERAMAKER_Ricoh, u8"Ricoh"},
	{LIBRAW_CAMERAMAKER_Rollei, u8"Rollei"},
	{LIBRAW_CAMERAMAKER_RoverShot, u8"RoverShot"},
	{LIBRAW_CAMERAMAKER_Samsung, u8"Samsung"},
	{LIBRAW_CAMERAMAKER_Sigma, u8"Sigma"},
	{LIBRAW_CAMERAMAKER_Sinar, u8"Sinar"},
	{LIBRAW_CAMERAMAKER_SMaL, u8"SMaL"},
	{LIBRAW_CAMERAMAKER_Sony, u8"Sony"},
	{LIBRAW_CAMERAMAKER_ST_Micro, u8"ST_Micro"},
	{LIBRAW_CAMERAMAKER_THL, u8"THL"},
	{LIBRAW_CAMERAMAKER_Xiaomi, u8"Xiaomi"},
	{LIBRAW_CAMERAMAKER_XIAOYI, u8"XIAOYI"},
	{LIBRAW_CAMERAMAKER_YI, u8"YI"},
	{LIBRAW_CAMERAMAKER_Yuneec, u8"Yuneec"},
	{LIBRAW_CAMERAMAKER_Zeiss, u8"Zeiss"},
};

static constexpr struct
{
	const int NumId;
	std::u8string_view StrId;
} ColorSpaceToStr[] = {
	{LIBRAW_COLORSPACE_NotFound, u8"Not Found"},
	{LIBRAW_COLORSPACE_sRGB, u8"sRGB"},
	{LIBRAW_COLORSPACE_AdobeRGB, u8"Adobe RGB"},
	{LIBRAW_COLORSPACE_WideGamutRGB, u8"Wide Gamut RGB"},
	{LIBRAW_COLORSPACE_ProPhotoRGB, u8"ProPhoto RGB"},
	{LIBRAW_COLORSPACE_ICC, u8"ICC profile (embedded)"},
	{LIBRAW_COLORSPACE_Uncalibrated, u8"Uncalibrated"},
	{LIBRAW_COLORSPACE_CameraLinearUniWB, u8"Camera Linear, no WB"},
	{LIBRAW_COLORSPACE_CameraLinear, u8"Camera Linear"},
	{LIBRAW_COLORSPACE_CameraGammaUniWB, u8"Camera non-Linear, no WB"},
	{LIBRAW_COLORSPACE_CameraGamma, u8"Camera non-Linear"},
	{LIBRAW_COLORSPACE_MonochromeLinear, u8"Monochrome Linear"},
	{LIBRAW_COLORSPACE_MonochromeGamma, u8"Monochrome non-Linear"},
	{LIBRAW_COLORSPACE_Unknown, u8"Unknown"},
};

static constexpr struct
{
	const int NumId;
	const int LibRawId;
	std::u8string_view StrId;
} Fujifilm_WhiteBalance2Str[] = {
	{0x000, LIBRAW_WBI_Auto, u8"Auto"},
	{0x100, LIBRAW_WBI_Daylight, u8"Daylight"},
	{0x200, LIBRAW_WBI_Cloudy, u8"Cloudy"},
	{0x300, LIBRAW_WBI_FL_D, u8"Daylight Fluorescent"},
	{0x301, LIBRAW_WBI_FL_N, u8"Day White Fluorescent"},
	{0x302, LIBRAW_WBI_FL_W, u8"White Fluorescent"},
	{0x303, LIBRAW_WBI_FL_WW, u8"Warm White Fluorescent"},
	{0x304, LIBRAW_WBI_FL_L, u8"Living Room Warm White Fluorescent"},
	{0x400, LIBRAW_WBI_Tungsten, u8"Incandescent"},
	{0x500, LIBRAW_WBI_Flash, u8"Flash"},
	{0x600, LIBRAW_WBI_Underwater, u8"Underwater"},
	{0xf00, LIBRAW_WBI_Custom, u8"Custom"},
	{0xf01, LIBRAW_WBI_Custom2, u8"Custom2"},
	{0xf02, LIBRAW_WBI_Custom3, u8"Custom3"},
	{0xf03, LIBRAW_WBI_Custom4, u8"Custom4"},
	{0xf04, LIBRAW_WBI_Custom5, u8"Custom5"},
	{0xff0, LIBRAW_WBI_Kelvin, u8"Kelvin"},
};

static constexpr struct
{
	const int NumId;
	std::u8string_view StrId;
} Fujifilm_FilmModeToStr[] = {
	{0x000, u8"F0/Standard (Provia)"},
	{0x100, u8"F1/Studio Portrait"},
	{0x110, u8"F1a/Studio Portrait Enhanced Saturation"},
	{0x120, u8"F1b/Studio Portrait Smooth Skin Tone (Astia)"},
	{0x130, u8"F1c/Studio Portrait Increased Sharpness"},
	{0x200, u8"F2/Fujichrome (Velvia)"},
	{0x300, u8"F3/Studio Portrait Ex"},
	{0x400, u8"F4/Velvia"},
	{0x500, u8"Pro Neg. Std"},
	{0x501, u8"Pro Neg. Hi"},
	{0x600, u8"Classic Chrome"},
	{0x700, u8"Eterna"},
	{0x800, u8"Classic Negative"},
};

constexpr struct
{
	const int NumId;
	std::u8string_view StrId;
} Fujifilm_DynamicRangeSettingToStr[] = {
	{0x0000, u8"Auto (100-400%)"},
	{0x0001, u8"Manual"},
	{0x0100, u8"Standard (100%)"},
	{0x0200, u8"Wide1 (230%)"},
	{0x0201, u8"Wide2 (400%)"},
	{0x8000, u8"Film Simulation"},
};

//clang-format on

static const id2hr_t* lookup_id2hr(const uint64_t id, const id2hr_t* table, const size_t nEntries)
{
	for (int k = 0; k < nEntries; k++)
		if (id == table[k].id)
			return &table[k];
	return nullptr;
}

std::u8string_view ColorSpace_idx2str(size_t ColorSpace)
{
	for (const auto& i : ColorSpaceToStr)
		if (i.NumId == ColorSpace)
			return i.StrId;
	return {};
}

std::u8string_view CameraMaker_idx2str(size_t maker)
{
	for (const auto& i : CorpToStr)
		if (i.NumId == static_cast<int>(maker))
			return i.StrId;
	return {};
}

std::u8string_view WB_idx2str(size_t WBi)
{
	for (const auto& i : WBToStr)
		if (i.NumId == static_cast<int>(WBi))
			return i.StrId;
	return {};
}

std::u8string_view WB_idx2hrstr(size_t WBi)
{
	for (const auto& i : WBToStr)
		if (i.NumId == static_cast<int>(WBi))
			return i.hrStrId;
	return {};
}

std::u8string_view Fujifilm_WhiteBalance_idx2str(size_t WB)
{
	for (const auto& i : Fujifilm_WhiteBalance2Str)
		if (i.NumId == WB)
			return i.StrId;
	return {};
}

std::u8string_view Fujifilm_FilmMode_idx2str(size_t FilmMode)
{
	for (const auto& i : Fujifilm_FilmModeToStr)
		if (i.NumId == FilmMode)
			return i.StrId;
	return {};
}

std::u8string_view Fujifilm_DynamicRangeSetting_idx2str(size_t DynamicRangeSetting)
{
	for (const auto& i : Fujifilm_DynamicRangeSettingToStr)
		if (i.NumId == DynamicRangeSetting)
			return i.StrId;
	return {};
}

static ui::orientation transate_libraw_orientation(int flip)
{
	// 3 if requires 180-deg rotation; 5 if 90 deg counterclockwise, 6 if 90 deg clockwise
	auto result = flip;
	if (result == 0) result = 1;
	if (result == 5) result = 8;
	return static_cast<ui::orientation>(result);
}

static void add_metadata(metadata_kv_list& kv, const std::u8string_view name, const std::u8string_view val)
{
	if (!name.empty() && !val.empty())
	{
		kv.emplace_back(str::cache(name), val);
	}
}

static void add_metadata(metadata_kv_list& kv, const std::u8string_view name, const std::string_view val)
{
	add_metadata(kv, name, str::utf8_cast(val));
}

static void add_metadata(metadata_kv_list& kv, const std::u8string_view name, const std::u8string_view val1,
                         const std::u8string_view val2)
{
	std::u8string val;
	str::join(val, val1, val2);
	add_metadata(kv, name, val);
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


	// TODO GPS
	/*data.other.parsed_gps

		exif_gps_coordinate_builder _gps_coordinate;

	const auto degrees = entry.get_urational(0);
	const auto minutes = entry.get_urational(1);
	const auto seconds = entry.get_urational(2);
	const auto latitude = gps_coordinate::dms_to_decimal(degrees.to_real(), minutes.to_real(), seconds.to_real());

	_gps_coordinate.latitude(latitude);

	const auto coord = _gps_coordinate.build();
	md.store(prop::latitude, static_cast<float>(coord.latitude()));
	md.store(prop::longitude, static_cast<float>(coord.longitude()));*/

	const auto& P1 = data.idata;
	const auto& P2 = data.other;
	const auto& P3 = data.makernotes.common;

	const auto& mnLens = data.lens.makernotes;
	const auto& exifLens = data.lens;
	const auto& ShootingInfo = data.shootinginfo;

	const auto& S = data.sizes;
	const auto& O = data.params;
	const auto& C = data.color;
	const auto& T = data.thumbnail;

	const auto& Canon = data.makernotes.canon;
	const auto& Hasselblad = data.makernotes.hasselblad;
	const auto& Fuji = data.makernotes.fuji;
	const auto& Nikon = data.makernotes.nikon;
	const auto& Oly = data.makernotes.olympus;
	const auto& Sony = data.makernotes.sony;

	const auto CamMakerName = str::safe_string(LibRaw::cameramakeridx2maker(P1.maker_index));
	const auto ColorSpaceName = ColorSpace_idx2str(P3.ColorSpace);
	int WBi;
	float denom;
	int ret;

	metadata_kv_list kv;

	if (!str::is_empty(C.OriginalRawFileName))
		add_metadata(kv, u8"OriginalRawFileName", C.OriginalRawFileName);

	char sz[64];
	ctime_s(sz, 64, &(P2.timestamp));

	add_metadata(kv, u8"Timestamp", sz);
	add_metadata(kv, u8"Camera",
	             str::format(u8"{} {} ID: 0x{:x}", str::utf8_cast(P1.make), str::utf8_cast(P1.model), mnLens.CamID));
	add_metadata(kv, u8"Normalized Make/Model",
	             str::format(u8"{}/{}", str::utf8_cast(P1.normalized_make), str::utf8_cast(P1.normalized_model)));
	add_metadata(kv, u8"CamMaker ID", str::to_string(P1.maker_index));

	if (!CamMakerName.empty())
		add_metadata(kv, u8"CameraMaker", CamMakerName);

	if (!str::is_empty(C.UniqueCameraModel))
	{
		add_metadata(kv, u8"UniqueCameraModel", C.UniqueCameraModel);
	}
	if (!str::is_empty(C.LocalizedCameraModel))
	{
		add_metadata(kv, u8"LocalizedCameraModel", C.LocalizedCameraModel);
	}
	if (!str::is_empty(C.ImageUniqueID))
	{
		add_metadata(kv, u8"ImageUniqueID", C.ImageUniqueID);
	}
	if (!str::is_empty(C.RawDataUniqueID))
	{
		add_metadata(kv, u8"RawDataUniqueID", C.RawDataUniqueID);
	}

	if (!str::is_empty(ShootingInfo.BodySerial) && strcmp(ShootingInfo.BodySerial, "0"))
	{
		add_metadata(kv, u8"Body#", str::trim(ShootingInfo.BodySerial));
	}
	else if (C.model2[0] && (!_strnicmp(P1.normalized_make, "Kodak", 5)))
	{
		add_metadata(kv, u8"Body#", str::trim(C.model2));
	}
	if (!str::is_empty(ShootingInfo.InternalBodySerial))
	{
		add_metadata(kv, u8"BodyAssy#", str::trim(ShootingInfo.InternalBodySerial));
	}
	if (!str::is_empty(exifLens.LensSerial))
	{
		add_metadata(kv, u8"Lens#", str::trim(exifLens.LensSerial));
	}
	if (!str::is_empty(exifLens.InternalLensSerial))
	{
		add_metadata(kv, u8"LensAssy#", str::trim(exifLens.InternalLensSerial));
	}
	if (!str::is_empty(P2.artist))
	{
		add_metadata(kv, u8"Owner", str::trim(P2.artist));
	}

	if (P1.dng_version)
	{
		std::u8string s;
		for (int i = 24; i >= 0; i -= 8)
			s += str::format(u8"{}{}", P1.dng_version >> i & 255, i ? '.' : ' ');

		add_metadata(kv, u8"DNG Version", s);
	}


	add_metadata(kv, u8"MinFocal", str::format(u8"{:0.1} mm", exifLens.MinFocal));
	add_metadata(kv, u8"MaxFocal", str::format(u8"{:0.1} mm", exifLens.MaxFocal));
	add_metadata(kv, u8"MaxAp @MinFocal", str::format(u8"f/{:0.1}", exifLens.MaxAp4MinFocal));
	add_metadata(kv, u8"MaxAp @MaxFocal", str::format(u8"f/{:0.1}", exifLens.MaxAp4MaxFocal));
	add_metadata(kv, u8"CurFocal", str::format(u8"{:0.1} mm", P2.focal_len));
	add_metadata(kv, u8"MaxAperture @CurFocal", str::format(u8"f/{:0.1}", exifLens.EXIF_MaxAp));
	add_metadata(kv, u8"FocalLengthIn35mmFormat", str::format(u8"{} mm", exifLens.FocalLengthIn35mmFormat));
	add_metadata(kv, u8"LensMake", exifLens.LensMake);
	add_metadata(kv, u8"Lens", exifLens.Lens);


	add_metadata(kv, u8"DriveMode", str::to_string(ShootingInfo.DriveMode));
	add_metadata(kv, u8"FocusMode", str::to_string(ShootingInfo.FocusMode));
	add_metadata(kv, u8"MeteringMode", str::to_string(ShootingInfo.MeteringMode));
	add_metadata(kv, u8"AFPoint", str::to_string(ShootingInfo.AFPoint));
	add_metadata(kv, u8"ExposureMode", str::to_string(ShootingInfo.ExposureMode));
	add_metadata(kv, u8"ExposureProgram", str::to_string(ShootingInfo.ExposureProgram));
	add_metadata(kv, u8"ImageStabilization", str::to_string(ShootingInfo.ImageStabilization));

	if (!str::is_empty(mnLens.body))
	{
		add_metadata(kv, u8"Host Body", mnLens.body);
	}

	if (!str::is_empty(Hasselblad.CaptureSequenceInitiator))
	{
		add_metadata(kv, u8"Initiator", Hasselblad.CaptureSequenceInitiator);
	}
	if (!str::is_empty(Hasselblad.SensorUnitConnector))
	{
		add_metadata(kv, u8"SU Connector", Hasselblad.SensorUnitConnector);
	}

	const auto* const FormatName = lookup_id2hr(mnLens.CameraFormat, FormatNames, LIBRAW_FORMAT_TheLastOne);

	if (FormatName)
	{
		add_metadata(kv, u8"CameraFormat", FormatName->name);
	}
	else
	{
		add_metadata(kv, u8"CameraFormat", str::to_string(mnLens.CameraFormat));
	}


	if (!_strnicmp(P1.make, "Nikon", 5) && Nikon.SensorHighSpeedCrop.cwidth)
	{
		const auto* const Crop = lookup_id2hr(Nikon.HighSpeedCropFormat, NikonCrops, nNikonCrops);

		if (Crop)
		{
			add_metadata(kv, u8"Nikon crop", str::format(u8"{}: {}", Nikon.HighSpeedCropFormat, Crop->name));
		}
		else
		{
			add_metadata(kv, u8"Nikon crop", str::to_string(Nikon.HighSpeedCropFormat));
		}

		add_metadata(kv, u8"Sensor used area", str::format(u8"{} x {}; crop from: {} x {} at top left pixel: ({}, {})",
		                                                   Nikon.SensorWidth, Nikon.SensorHeight,
		                                                   Nikon.SensorHighSpeedCrop.cwidth,
		                                                   Nikon.SensorHighSpeedCrop.cheight,
		                                                   Nikon.SensorHighSpeedCrop.cleft,
		                                                   Nikon.SensorHighSpeedCrop.ctop));
	}

	const auto* const MountName = lookup_id2hr(mnLens.CameraMount, MountNames, LIBRAW_MOUNT_TheLastOne);

	if (MountName)
	{
		add_metadata(kv, u8"CameraMount", MountName->name);
	}
	else
	{
		add_metadata(kv, u8"CameraMount", str::to_string(mnLens.CameraMount));
	}

	if (mnLens.LensID != 0xffffffff)
		add_metadata(kv, u8"LensID", str::format(u8"{} 0x{:x}", mnLens.LensID, mnLens.LensID));

	if (!str::is_empty(mnLens.Lens))
	{
		add_metadata(kv, u8"Lens", mnLens.Lens);
	}

	const auto* const LensFormatName = lookup_id2hr(mnLens.LensFormat, FormatNames, LIBRAW_FORMAT_TheLastOne);

	if (LensFormatName)
	{
		add_metadata(kv, u8"LensFormat", LensFormatName->name);
	}
	else
	{
		add_metadata(kv, u8"LensFormat", str::to_string(mnLens.LensFormat));
	}

	const auto* const LensMountName = lookup_id2hr(mnLens.LensMount, MountNames, LIBRAW_MOUNT_TheLastOne);

	if (LensMountName)
	{
		add_metadata(kv, u8"LensMount", LensMountName->name);
	}
	else
	{
		add_metadata(kv, u8"LensMount", str::to_string(mnLens.LensMount));
	}

	switch (mnLens.FocalType)
	{
	case LIBRAW_FT_UNDEFINED:
		add_metadata(kv, u8"FocalType", u8"Undefined");
		break;
	case LIBRAW_FT_PRIME_LENS:
		add_metadata(kv, u8"FocalType", u8"Prime lens");
		break;
	case LIBRAW_FT_ZOOM_LENS:
		add_metadata(kv, u8"FocalType", u8"Zoom lens");
		break;
	default:
		add_metadata(kv, u8"FocalType", str::to_string(mnLens.FocalType));
		break;
	}

	add_metadata(kv, u8"LensFeatures_pre", mnLens.LensFeatures_pre);
	add_metadata(kv, u8"LensFeatures_suf", mnLens.LensFeatures_suf);
	add_metadata(kv, u8"MinFocal", str::format(u8"{:0.1} mm", mnLens.MinFocal));
	add_metadata(kv, u8"MaxFocal", str::format(u8"{:0.1} mm", mnLens.MaxFocal));
	add_metadata(kv, u8"MaxAp @MinFocal", str::format(u8"f/{:0.1}", mnLens.MaxAp4MinFocal));
	add_metadata(kv, u8"MaxAp @MaxFocal", str::format(u8"f/{:0.1}", mnLens.MaxAp4MaxFocal));
	add_metadata(kv, u8"MinAp @MinFocal", str::format(u8"f/{:0.1}", mnLens.MinAp4MinFocal));
	add_metadata(kv, u8"MinAp @MaxFocal", str::format(u8"f/{:0.1}", mnLens.MinAp4MaxFocal));
	add_metadata(kv, u8"MaxAp", str::format(u8"f/{:0.1}", mnLens.MaxAp));
	add_metadata(kv, u8"MinAp", str::format(u8"f/{:0.1}", mnLens.MinAp));
	add_metadata(kv, u8"CurFocal", str::format(u8"{:0.1} mm", mnLens.CurFocal));
	add_metadata(kv, u8"CurAp", str::format(u8"f/{:0.1}", mnLens.CurAp));
	add_metadata(kv, u8"MaxAp @CurFocal", str::format(u8"f/{:0.1}", mnLens.MaxAp4CurFocal));
	add_metadata(kv, u8"MinAp @CurFocal", str::format(u8"f/{:0.1}", mnLens.MinAp4CurFocal));

	if (exifLens.makernotes.FocalLengthIn35mmFormat > 1.0f)
		add_metadata(kv, u8"FocalLengthIn35mmFormat",
		             str::format(u8"{:0.1} mm", exifLens.makernotes.FocalLengthIn35mmFormat));

	if (exifLens.nikon.EffectiveMaxAp > 0.1f)
		add_metadata(kv, u8"EffectiveMaxAp", str::format(u8"f/{:0.1}", exifLens.nikon.EffectiveMaxAp));

	if (exifLens.makernotes.LensFStops > 0.1f)
		add_metadata(kv, u8"LensFStops @CurFocal", str::format(u8"{:0.2}", exifLens.makernotes.LensFStops));

	add_metadata(kv, u8"TeleconverterID", str::to_string(mnLens.TeleconverterID));
	add_metadata(kv, u8"Teleconverter", mnLens.Teleconverter);
	add_metadata(kv, u8"AdapterID", str::to_string(mnLens.AdapterID));
	add_metadata(kv, u8"Adapter", mnLens.Adapter);
	add_metadata(kv, u8"AttachmentID", str::to_string(mnLens.AttachmentID));
	add_metadata(kv, u8"Attachment", mnLens.Attachment);

	add_metadata(kv, u8"ISO speed", str::to_string(static_cast<int>(P2.iso_speed)));
	if (P3.real_ISO > 0.1f)
		add_metadata(kv, u8"real ISO speed", str::to_string(static_cast<int>(P3.real_ISO)));

	if (P2.shutter > 0 && P2.shutter < 1)
		add_metadata(kv, u8"Shutter", str::format(u8"1/{:0.1}", 1.0f / P2.shutter));
	else if (P2.shutter >= 1)
		add_metadata(kv, u8"Shutter", str::format(u8"{:0.1} sec", P2.shutter));
	else /* negative*/
		add_metadata(kv, u8"Shutter", u8"negative value");

	add_metadata(kv, u8"Aperture", str::format(u8"f/{:0.1}", P2.aperture));
	add_metadata(kv, u8"Focal length", str::format(u8"{:0.1} mm", P2.focal_len));

	if (P3.exifAmbientTemperature > -273.15f)
		add_metadata(kv, u8"Ambient temperature (exif data)", str::format(u8"{:6.2}° C", P3.exifAmbientTemperature));
	if (P3.CameraTemperature > -273.15f)
		add_metadata(kv, u8"Camera temperature", str::format(u8"{:6.2}° C", P3.CameraTemperature));
	if (P3.SensorTemperature > -273.15f)
		add_metadata(kv, u8"Sensor temperature", str::format(u8"{:6.2}° C", P3.SensorTemperature));
	if (P3.SensorTemperature2 > -273.15f)
		add_metadata(kv, u8"Sensor temperature2", str::format(u8"{:6.2}° C", P3.SensorTemperature2));
	if (P3.LensTemperature > -273.15f)
		add_metadata(kv, u8"Lens temperature", str::format(u8"{:6.2}° C", P3.LensTemperature));
	if (P3.AmbientTemperature > -273.15f)
		add_metadata(kv, u8"Ambient temperature", str::format(u8"{:6.2}° C", P3.AmbientTemperature));
	if (P3.BatteryTemperature > -273.15f)
		add_metadata(kv, u8"Battery temperature", str::format(u8"{:6.2}° C", P3.BatteryTemperature));
	if (P3.FlashGN > 1.0f)
		add_metadata(kv, u8"Flash Guide Number", str::format(u8"{:6.2}", P3.FlashGN));
	add_metadata(kv, u8"Flash exposure compensation", str::format(u8"{:0.2} EV", P3.FlashEC));

	if (C.profile)
		add_metadata(kv, u8"Embedded ICC profile", str::format(u8"yes, {} bytes", C.profile_length));
	else
		add_metadata(kv, u8"Embedded ICC profile", u8"no");

	if (C.dng_levels.baseline_exposure > -999.f)
		add_metadata(kv, u8"Baseline exposure", str::format(u8"{:04.3}", C.dng_levels.baseline_exposure));

	add_metadata(kv, u8"Number of raw images", str::to_string(P1.raw_count));

	if (Fuji.DriveMode != -1)
	{
		const auto* const DriveMode = lookup_id2hr(Fuji.DriveMode, FujiDriveModes, nFujiDriveModes);

		if (DriveMode)
		{
			add_metadata(kv, u8"Fuji Drive Mode", DriveMode->name);
		}
		else
		{
			add_metadata(kv, u8"Fuji Drive Mode", str::to_string(Fuji.DriveMode));
		}
	}

	if (Fuji.CropMode)
	{
		const auto* const Crop = lookup_id2hr(Fuji.CropMode, FujiCrops, nFujiCrops);

		if (Crop)
		{
			add_metadata(kv, u8"Fuji Crop Mode", Crop->name);
		}
		else
		{
			add_metadata(kv, u8"Fuji Crop Mode", str::to_string(Fuji.CropMode));
		}
	}

	if (Fuji.WB_Preset != 0xffff)
		add_metadata(kv, u8"Fuji WB preset", str::format(u8"0x{:03x}", Fuji.WB_Preset),
		             Fujifilm_WhiteBalance_idx2str(Fuji.WB_Preset));
	if (Fuji.ExpoMidPointShift > -999.f) // tag 0x9650
		add_metadata(kv, u8"Fuji Exposure shift", str::format(u8"{:4.3}", Fuji.ExpoMidPointShift));
	if (Fuji.DynamicRange != 0xffff)
		add_metadata(kv, u8"Fuji Dynamic Range (0x1400)", str::format(u8"{}", Fuji.DynamicRange),
		             Fuji.DynamicRange == 1 ? u8"Standard" : u8"Wide");
	if (Fuji.FilmMode != 0xffff)
		add_metadata(kv, u8"Fuji Film Mode (0x1401)", str::format(u8"0x{:03x}", Fuji.FilmMode),
		             Fujifilm_FilmMode_idx2str(Fuji.FilmMode));
	if (Fuji.DynamicRangeSetting != 0xffff)
		add_metadata(kv, u8"Fuji Dynamic Range Setting (0x1402)", str::format(u8"0x{:04x}", Fuji.DynamicRangeSetting),
		             Fujifilm_DynamicRangeSetting_idx2str(Fuji.DynamicRangeSetting));
	if (Fuji.DevelopmentDynamicRange != 0xffff)
		add_metadata(kv, u8"Fuji Development Dynamic Range (0x1403)", str::to_string(Fuji.DevelopmentDynamicRange));
	if (Fuji.AutoDynamicRange != 0xffff)
		add_metadata(kv, u8"Fuji Auto Dynamic Range (0x140b)", str::to_string(Fuji.AutoDynamicRange));
	if (Fuji.DRangePriority != 0xffff)
		add_metadata(kv, u8"Fuji Dynamic Range priority (0x1443)", str::format(u8"{}", Fuji.DRangePriority),
		             Fuji.DRangePriority ? u8"Fixed" : u8"Auto");
	if (Fuji.DRangePriorityAuto)
		add_metadata(kv, u8"Fuji Dynamic Range priority Auto (0x1444)", str::format(u8"{}", Fuji.DRangePriorityAuto),
		             Fuji.DRangePriorityAuto == 1 ? u8"Weak" : u8"Strong");
	if (Fuji.DRangePriorityFixed)
		add_metadata(kv, u8"Fuji Dynamic Range priority Fixed (0x1445)", str::format(u8"{}", Fuji.DRangePriorityFixed),
		             Fuji.DRangePriorityFixed == 1 ? u8"Weak" : u8"Strong");

	if (S.pixel_aspect != 1)
		add_metadata(kv, u8"Pixel Aspect Ratio", str::format(u8"{:0.6}", S.pixel_aspect));

	if (T.tlength)
		add_metadata(kv, u8"Thumb size", str::format(u8"{:4} x {}", T.twidth, T.theight));

	add_metadata(kv, u8"Full size", str::format(u8"{:4} x {}", S.raw_width, S.raw_height));

	if (S.raw_inset_crop.cwidth)
	{
		auto s = str::format(u8"{:4} x {} u8", S.raw_inset_crop.cwidth, S.raw_inset_crop.cheight);

		if (S.raw_inset_crop.cleft != 0xffff)
			s += str::format(u8" left {}", S.raw_inset_crop.cleft);
		if (S.raw_inset_crop.ctop != 0xffff)
			s += str::format(u8" top {}", S.raw_inset_crop.ctop);

		add_metadata(kv, u8"Raw inset, width x height", s);
	}

	const auto* const Aspect = lookup_id2hr(S.raw_inset_crop.aspect, AspectRatios, nAspectRatios);

	if (Aspect)
	{
		add_metadata(kv, u8"Aspect", Aspect->name);
	}
	else
	{
		add_metadata(kv, u8"Aspect", str::format(u8"Other {}", S.raw_inset_crop.aspect));
	}

	add_metadata(kv, u8"Image size", str::format(u8"{:4} x {}", S.width, S.height));
	add_metadata(kv, u8"Output size", str::format(u8"{:4} x {}", S.iwidth, S.iheight));
	add_metadata(kv, u8"Image flip", str::to_string(S.flip));

	if (Canon.RecordMode)
	{
		const auto* const RecordMode = lookup_id2hr(Canon.RecordMode, CanonRecordModes, nCanonRecordModes);

		if (RecordMode)
		{
			add_metadata(kv, u8"Canon record mode", str::format(u8"{}: {}", Canon.RecordMode, RecordMode->name));
		}
		else
		{
			add_metadata(kv, u8"Canon record mode", str::to_string(Canon.RecordMode));
		}
	}
	if (Canon.SensorWidth)
		add_metadata(kv, u8"SensorWidth", str::to_string(Canon.SensorWidth));
	if (Canon.SensorHeight)
		add_metadata(kv, u8"SensorHeight", str::to_string(Canon.SensorHeight));
	if (Canon.SensorLeftBorder != -1)
		add_metadata(kv, u8"SensorLeftBorder", str::to_string(Canon.SensorLeftBorder));
	if (Canon.SensorTopBorder != -1)
		add_metadata(kv, u8"SensorTopBorder", str::to_string(Canon.SensorTopBorder));
	if (Canon.SensorRightBorder)
		add_metadata(kv, u8"SensorRightBorder", str::to_string(Canon.SensorRightBorder));
	if (Canon.SensorBottomBorder)
		add_metadata(kv, u8"SensorBottomBorder", str::to_string(Canon.SensorBottomBorder));
	if (Canon.BlackMaskLeftBorder)
		add_metadata(kv, u8"BlackMaskLeftBorder", str::to_string(Canon.BlackMaskLeftBorder));
	if (Canon.BlackMaskTopBorder)
		add_metadata(kv, u8"BlackMaskTopBorder", str::to_string(Canon.BlackMaskTopBorder));
	if (Canon.BlackMaskRightBorder)
		add_metadata(kv, u8"BlackMaskRightBorder", str::to_string(Canon.BlackMaskRightBorder));
	if (Canon.BlackMaskBottomBorder)
		add_metadata(kv, u8"BlackMaskBottomBorder", str::to_string(Canon.BlackMaskBottomBorder));

	if (Hasselblad.BaseISO)
		add_metadata(kv, u8"Hasselblad base ISO", str::to_string(Hasselblad.BaseISO));
	if (Hasselblad.Gain)
		add_metadata(kv, u8"Hasselblad gain", str::to_string(Hasselblad.Gain, 3));

	add_metadata(kv, u8"Raw colors", str::to_string(P1.colors));

	/*if (P1.filters)
	{
		add_metadata(kv, u8"Filter pattern", str::format(u8"");
		if (!P1.cdesc[3])
			P1.cdesc[3] = 'G';
		for (int i = 0; i < 16; i++)
			putchar(P1.cdesc[MyCoolRawProcessor.fcol(i >> 1, i & 1)]);
	}*/

	if (Canon.ChannelBlackLevel[0])
	{
		add_metadata(kv, u8"Canon makernotes, ChannelBlackLevel", str::format(u8"{} {} {} {}",
		                                                                      Canon.ChannelBlackLevel[0],
		                                                                      Canon.ChannelBlackLevel[1],
		                                                                      Canon.ChannelBlackLevel[2],
		                                                                      Canon.ChannelBlackLevel[3]));
	}

	if (C.black)
	{
		add_metadata(kv, u8"black", str::to_string(C.black));
	}
	/*if (C.cblack[0] != 0)
	{
		add_metadata(kv, u8"cblack[0 .. 3]:");
		for (int c = 0; c < 4; c++)
			add_metadata(kv, u8"{}", C.cblack[c]);
	}
	if ((C.cblack[4] * C.cblack[5]) > 0)
	{
		add_metadata(kv, u8"BlackLevelRepeatDim", str::format(u8"{} x {}", C.cblack[4], C.cblack[4]);
		int n = C.cblack[4] * C.cblack[5];
		add_metadata(kv, u8"cblack[6 .. {}]:", 6 + n - 1);
		for (int c = 6; c < 6 + n; c++)
			add_metadata(kv, u8"{}", C.cblack[c]);
	}

	if (C.linear_max[0] != 0)
	{
		add_metadata(kv, u8"Highlight linearity limits:");
		for (int c = 0; c < 4; c++)
			add_metadata(kv, u8"%ld", C.linear_max[c]);
	}

	if (P1.colors > 1)
	{
		add_metadata(kv, u8"Makernotes WB data", str::format(u8"           coeffs                  EVs");
		if ((C.cam_mul[0] > 0) && (C.cam_mul[1] > 0)) {
			add_metadata(kv, u8"  %-23s   %g %g %g %g   %5.2f %5.2f %5.2f %5.2f",
				"As shot",
				C.cam_mul[0], C.cam_mul[1], C.cam_mul[2], C.cam_mul[3],
				roundf(log2(C.cam_mul[0] / C.cam_mul[1]) * 100.0f) / 100.0f,
				0.0f,
				roundf(log2(C.cam_mul[2] / C.cam_mul[1]) * 100.0f) / 100.0f,
				C.cam_mul[3] ? roundf(log2(C.cam_mul[3] / C.cam_mul[1]) * 100.0f) / 100.0f u8", str::format(u8"0.0f);
		}

		for (int cnt = 0; cnt < int(sizeof WBToStr / sizeof * WBToStr); cnt++) {
			WBi = WBToStr[cnt].NumId;
			if ((C.WB_Coeffs[WBi][0] > 0) && (C.WB_Coeffs[WBi][1] > 0)) {
				denom = (float)C.WB_Coeffs[WBi][1];
				add_metadata(kv, u8"  %-23s   {:4} {:4} {:4} {:4}   %5.2f %5.2f %5.2f %5.2f",
					WBToStr[cnt].hrStrId,
					C.WB_Coeffs[WBi][0], C.WB_Coeffs[WBi][1], C.WB_Coeffs[WBi][2], C.WB_Coeffs[WBi][3],
					roundf(log2((float)C.WB_Coeffs[WBi][0] / denom) * 100.0f) / 100.0f,
					0.0f,
					roundf(log2((float)C.WB_Coeffs[WBi][2] / denom) * 100.0f) / 100.0f,
					C.WB_Coeffs[3] ? roundf(log2((float)C.WB_Coeffs[WBi][3] / denom) * 100.0f) / 100.0f u8", str::format(u8"0.0f);
			}
		}

		if ((Nikon.ME_WB[0] != 0.0f) && (Nikon.ME_WB[0] != 1.0f))
		{
			add_metadata(kv, u8"Nikon multi-exposure WB multipliers:");
			for (int c = 0; c < 4; c++)
				add_metadata(kv, u8"%g", Nikon.ME_WB[c]);
		}

		if (C.rgb_cam[0][0] > 0.0001)
		{
			add_metadata(kv, u8"\nCamera2RGB matrix (mode", str::format(u8"{}):", = data.params.use_camera_matrix);
			for (int i = 0; i < P1.colors; i++)
				add_metadata(kv, u8"%6.4f\t%6.4f\t%6.4f", C.rgb_cam[i][0], C.rgb_cam[i][1],
					C.rgb_cam[i][2]);
		}

		add_metadata(kv, u8"XYZ->CamRGB matrix:");
		for (int i = 0; i < P1.colors; i++)
			add_metadata(kv, u8"%6.4f\t%6.4f\t%6.4f", C.cam_xyz[i][0], C.cam_xyz[i][1],
				C.cam_xyz[i][2]);

		for (int cnt = 0; cnt < 2; cnt++) {
			if (fabsf(C.P1_color[cnt].romm_cam[0]) > 0)
			{
				add_metadata(kv, u8"PhaseOne Matrix {}:", cnt + 1);
				for (int i = 0; i < 3; i++)
					add_metadata(kv, u8"%6.4f\t%6.4f\t%6.4f", C.P1_color[cnt].romm_cam[i * 3],
						C.P1_color[cnt].romm_cam[i * 3 + 1],
						C.P1_color[cnt].romm_cam[i * 3 + 2]);
			}
		}

		if (fabsf(C.cmatrix[0][0]) > 0)
		{
			add_metadata(kv, u8"camRGB -> sRGB Matrix:");
			for (int i = 0; i < P1.colors; i++)
			{
				for (int j = 0; j < P1.colors; j++)
					add_metadata(kv, u8"%6.4f\t", C.cmatrix[i][j]);
				add_metadata(kv, u8"");
			}
		}

		if (fabsf(C.ccm[0][0]) > 0)
		{
			add_metadata(kv, u8"Color Correction Matrix:");
			for (int i = 0; i < P1.colors; i++)
			{
				for (int j = 0; j < P1.colors; j++)
					add_metadata(kv, u8"%6.4f\t", C.ccm[i][j]);
				add_metadata(kv, u8"");
			}
		}

		for (int cnt = 0; cnt < 2; cnt++) {
			if (C.dng_color[cnt].illuminant != LIBRAW_WBI_None) {
				if (C.dng_color[cnt].illuminant <= LIBRAW_WBI_StudioTungsten) {
					add_metadata(kv, u8"DNG Illuminant {}", str::format(u8"{}",
						cnt + 1, WB_idx2hrstr(C.dng_color[cnt].illuminant));
				}
				else if (C.dng_color[cnt].illuminant == LIBRAW_WBI_Other) {
					add_metadata(kv, u8"DNG Illuminant {}", str::format(u8"Other", cnt + 1);
				}
				else {
					add_metadata(kv, u8"DNG Illuminant {} is out of EXIF LightSources range [0:24, 255]", str::format(u8"{}",
						cnt + 1, C.dng_color[cnt].illuminant);
				}
			}
		}

		for (int n = 0; n < 2; n++) {
			if (fabsf(C.dng_color[n].colormatrix[0][0]) > 0)
			{
				add_metadata(kv, u8"DNG color matrix {}:", n + 1);
				for (int i = 0; i < P1.colors; i++)
					add_metadata(kv, u8"%6.4f\t%6.4f\t%6.4f", C.dng_color[n].colormatrix[i][0],
						C.dng_color[n].colormatrix[i][1],
						C.dng_color[n].colormatrix[i][2]);
			}
		}

		for (int n = 0; n < 2; n++) {
			if (fabsf(C.dng_color[n].calibration[0][0]) > 0)
			{
				add_metadata(kv, u8"DNG calibration matrix {}:", n + 1);
				for (int i = 0; i < P1.colors; i++)
				{
					for (int j = 0; j < P1.colors; j++)
						add_metadata(kv, u8"%6.4f\t", C.dng_color[n].calibration[j][i]);
					add_metadata(kv, u8"");
				}
			}
		}

		for (int n = 0; n < 2; n++) {
			if (fabsf(C.dng_color[n].forwardmatrix[0][0]) > 0)
			{
				add_metadata(kv, u8"DNG forward matrix {}:", n + 1);
				for (int i = 0; i < P1.colors; i++)
					add_metadata(kv, u8"%6.4f\t%6.4f\t%6.4f", C.dng_color[n].forwardmatrix[0][i],
						C.dng_color[n].forwardmatrix[1][i],
						C.dng_color[n].forwardmatrix[2][i]);
			}
		}

		add_metadata(kv, u8"Derived D65 multipliers:");
		for (int c = 0; c < P1.colors; c++)
			add_metadata(kv, u8"%f", C.pre_mul[c]);
	}*/

	add_metadata(kv, u8"Color space (makernotes) u8", str::format(u8"{}, {}", P3.ColorSpace, ColorSpaceName));


	if (Sony.PixelShiftGroupID)
	{
		add_metadata(kv, u8"Sony PixelShiftGroupPrefix",
		             str::format(u8"0x{:x} PixelShiftGroupID {}, u8", Sony.PixelShiftGroupPrefix,
		                         Sony.PixelShiftGroupID));

		if (Sony.numInPixelShiftGroup)
		{
			add_metadata(kv, u8"shot#", str::format(u8"{} (starts at 1) of total {}", Sony.numInPixelShiftGroup,
			                                        Sony.nShotsInPixelShiftGroup));
		}
		else
		{
			add_metadata(kv, u8"shots in PixelShiftGroup",
			             str::format(u8"{}, already ARQ", Sony.nShotsInPixelShiftGroup));
		}
	}

	if (Sony.Sony0x9400_version)
	{
		add_metadata(kv, u8"SONY Sequence data", str::format(u8"tag 0x9400 version '{:x}' ReleaseMode2: {}",
		                                                     Sony.Sony0x9400_version, Sony.Sony0x9400_ReleaseMode2));
		add_metadata(kv, u8"SequenceImageNumber",
		             str::format(u8"{} (starts at zero)", Sony.Sony0x9400_SequenceImageNumber));
		add_metadata(kv, u8"SequenceLength1", str::format(u8"{} shot(s)", Sony.Sony0x9400_SequenceLength1));
		add_metadata(kv, u8"SequenceFileNumber",
		             str::format(u8"{} (starts at zero, exiftool starts at 1)", Sony.Sony0x9400_SequenceFileNumber));
		add_metadata(kv, u8"SequenceLength2", str::format(u8"{} file(s)", Sony.Sony0x9400_SequenceLength2));
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

		const auto dx_full = full_image_extent.is_empty() ? 0 : ((full_image_extent.cx * 10) / full_image_extent.cy);
		const auto dx_thumb = image_extent.is_empty() ? 0 : ((image_extent.cx * 10) / image_extent.cy);

		if (dx_full != dx_thumb)
		{
			return full_orientation;
		}
	}

	return image_orientation;
}


static ui::surface_ptr thumb_to_surface(const libraw_thumbnail_t& thumbnail, ui::orientation orientation)
{
	ui::surface_ptr result;

	if (LIBRAW_THUMBNAIL_BITMAP == thumbnail.tformat)
	{
		const auto expected_size = thumbnail.theight * thumbnail.twidth * thumbnail.tcolors;

		if (expected_size <= thumbnail.tlength)
		{
			result = std::make_shared<ui::surface>();

			if (result->alloc(thumbnail.twidth, thumbnail.theight, ui::texture_format::RGB, orientation))
			{
				for (auto y = 0; y < thumbnail.theight; ++y)
				{
					const auto* src = thumbnail.thumb + (y * thumbnail.twidth * thumbnail.tcolors);
					auto* dst = result->pixels_line(y);

					for (auto y = 0; y < thumbnail.twidth; ++y)
					{
						if (thumbnail.tcolors == 3)
						{
							*dst++ = src[2];
							*dst++ = src[1];
							*dst++ = src[0];
							*dst++ = 0;
							src += 3;
						}
						else if (thumbnail.tcolors == 4)
						{
							*dst++ = *src++;
							*dst++ = *src++;
							*dst++ = *src++;
							*dst++ = *src++;
						}
					}
				}
			}
		}
	}

	return result;
}

struct raw_processor
{
	std::unique_ptr<LibRaw> processor;
	std::unique_ptr<dng_host> dng;
};

static raw_processor create_processor()
{
	raw_processor result;
	result.processor = std::make_unique<LibRaw>();

	//iProcessor.set_exifparser_handler(exif_callback, &context);

	result.dng = std::make_unique<dng_host>();
	result.processor->set_dng_host(result.dng.get());
	result.processor->imgdata.params.use_dngsdk = LIBRAW_DNG_ALL;
	// LIBRAW_DNG_FLOAT | LIBRAW_DNG_LINEAR | LIBRAW_DNG_XTRANS | LIBRAW_DNG_OTHER;

	return std::move(result);
}

file_scan_result files::scan_raw(const df::file_path path, const std::u8string_view xmp_sidecar, const bool load_thumb,
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
				else if (LIBRAW_THUMBNAIL_BITMAP == t.tformat)
				{
					result.thumbnail_surface = thumb_to_surface(t, ui::orientation::top_left);
				}

				result.thumbnail_surface->orientation(calc_orientation(result.thumbnail_surface->dimensions(),
				                                                       result.thumbnail_surface->orientation(),
				                                                       rp.processor->imgdata));
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

	if (rp.processor->open_file(w.c_str(), LIBRAW_OPEN_BIGFILE) == LIBRAW_SUCCESS)
	{
		file_scan_result md;
		const auto& image_data = rp.processor->imgdata;
		populate_raw_metadata(md, image_data);

		// The thumbnail often large enough, lets just use it :)
		if (can_load_preview && rp.processor->unpack_thumb() == LIBRAW_SUCCESS)
		{
			const auto& thumbnail = image_data.thumbnail;

			if (LIBRAW_THUMBNAIL_JPEG == thumbnail.tformat)
			{
				auto i = load_image_file(df::cspan(std::bit_cast<const uint8_t*>(thumbnail.thumb), thumbnail.tlength));

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
					s->orientation(calc_orientation(s->dimensions(), ui::orientation::top_left, rp.processor->imgdata));
					result.s = std::move(s);
				}
			}

			result.success = true;
			result.is_preview = true;
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

					if (libraw_internal_data.output_data.histogram)
					{
						int val, total, t_white = 0x2000, c;
						int perc = df::round(S.width * S.height * 0.01); /* 99th percentile white level */

						if (IO.fuji_width) perc /= 2;

						if (!((O.highlight & ~2) || O.no_auto_bright))
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
							auto* bufp = std::bit_cast<COLORREF*>(pixel_buffer + (y * stride));

							for (auto x = 0; x < S.width; x++)
							{
								const auto* const id = image_data.image[i++];
								*bufp++ = (color_curve[id[2]] >> 8) | (0xFF00 & color_curve[id[1]]) | (0xFF0000 &
									color_curve[id[0]] << 8);
							}
						}

						result.success = true;
					}
				}
			}
		}
	}

	return result;
}
