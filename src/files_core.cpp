// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "pch.h"

#include "app_text.h"
#include "files.h"
#include "metadata_exif.h"
#include "metadata_xmp.h"
#include "av_format.h"
#include "metadata_icc.h"
#include "metadata_iptc.h"

#include "rapidjson/istreamwrapper.h"

#define LIBARCHIVE_STATIC
#include "libarchive/libarchive/archive.h"
#include "libarchive/libarchive/archive_entry.h"

#undef GetObject
#undef min


static_assert(std::is_move_constructible_v<file_scan_result>);
static_assert(std::is_move_assignable_v<file_scan_result>);
static_assert(std::is_move_assignable_v<file_load_result>);


std::u8string file_group::display_name(const bool is_plural) const
{
	const auto result = is_plural && !str::is_empty(plural_name) ? plural_name : name;
	return tt.translate_text(std::u8string(result));
}

ui::const_surface_ptr file_type::default_thumbnail() const
{
	return platform::create_segoe_md2_icon(static_cast<wchar_t>(group->icon));
}

struct file_type_config
{
	file_group_by_name groups_by_name;
	file_type_by_extension types_by_name;

	std::vector<file_group_ref> groups;
	std::vector<file_type> types;
};

static file_type_config s_config;

bool file_tool::invoke(const df::file_path path)
{
	auto substitute = [&](u8ostringstream& result, const std::u8string_view token)
	{
		if (token == u8"item-path"sv) result << str::quote_if_white_space(path.str());
		else if (token == u8"exe-path"sv) result << str::quote_if_white_space(exe_path.str());
	};

	return platform::run(replace_tokens(invoke_text, substitute));
}

file_group_ref file_group_from_index(const int from_id)
{
	return s_config.groups[from_id];
}

file_group_ref parse_file_group(const std::u8string& text)
{
	const auto found = s_config.groups_by_name.find(text);

	if (found != s_config.groups_by_name.end())
	{
		return found->second;
	}

	return nullptr;
}

static constexpr file_type_traits photo_traits = file_type_traits::bitmap | file_type_traits::cache_metadata |
	file_type_traits::zoom | file_type_traits::hide_overlays |
	file_type_traits::thumbnail | file_type_traits::photo_metadata;
static constexpr file_type_traits video_traits = file_type_traits::av | file_type_traits::preview_video |
	file_type_traits::cache_metadata | file_type_traits::hide_overlays | file_type_traits::thumbnail |
	file_type_traits::thumbnail | file_type_traits::video_metadata;
static constexpr file_type_traits audio_traits = file_type_traits::av | file_type_traits::visualize_audio |
	file_type_traits::cache_metadata | file_type_traits::music_metadata;

static constexpr file_type_traits commodore_traits = file_type_traits::commodore;
static constexpr file_type_traits archive_traits = file_type_traits::archive;

file_group file_group::other(u8"other"sv, u8"others"sv, 0x5E5E5E, icon_index::document, file_type_traits::other, {});
file_group file_group::folder(u8"folder"sv, u8"folders"sv, 0x18A59C, icon_index::folder, file_type_traits::folder, {});
file_group file_group::photo(u8"photo"sv, u8"photos"sv, 0x18A549, icon_index::photo, photo_traits, {u8"xmp"});
file_group file_group::video(u8"video"sv, u8"videos"sv, 0xA55018, icon_index::video, video_traits,
                             {u8"srt"sv, u8"smi"sv, u8"vtt"sv, u8"mpl2"sv, u8"thm"sv, u8"xmp"});
file_group file_group::audio(u8"audio"sv, {}, 0xA5184B, icon_index::audio, audio_traits, {});

file_group file_group::archive(u8"archive"sv, u8"archives"sv, 0x5588DD, icon_index::archive, archive_traits, {});
file_group file_group::commodore(u8"commodore"sv, {}, 0xFF8811, icon_index::retro, commodore_traits, {});


file_type file_type::folder(file_group::folder, {}, {}, {});
file_type file_type::other(file_group::other, {}, {}, {});;

void load_file_types()
{
	s_config.groups = {
		file_group::folder,
		file_group::photo,
		file_group::video,
		file_group::audio,
		file_group::archive,
		file_group::commodore,
		file_group::other};

	s_config.types = {
		{file_group::video, u8"264"sv, {}, {}},
		{file_group::video, u8"265"sv, {}, {}},
		{file_group::video, u8"302"sv, {}, {}},
		{file_group::video, u8"3fr"sv, u8"Hasselblad Raw"sv, file_type_traits::raw | file_type_traits::edit},
		{file_group::audio, u8"669"sv, {}, {}},
		{file_group::video, u8"722"sv, {}, {}},
		{file_group::video, u8"A64"sv, {}, {}},
		{file_group::video, u8"aa"sv, {}, {}},
		{file_group::video, u8"aa3"sv, {}, {}},
		{file_group::audio, u8"aac,adt,adts"sv, u8"Advanced Audio Coding"sv, {}},
		{file_group::video, u8"ac3"sv, {}, {}},
		{file_group::video, u8"acm"sv, {}, {}},
		{file_group::video, u8"adf"sv, {}, {}},
		{file_group::video, u8"adp"sv, {}, {}},
		{file_group::video, u8"ads"sv, {}, {}},
		{file_group::video, u8"adx"sv, {}, {}},
		{file_group::video, u8"aea"sv, {}, {}},
		{file_group::audio, u8"afc"sv, {}, {}},
		{file_group::audio, u8"aif, aifc, aiff"sv, u8"Audio Interchange File Format"sv, file_type_traits::embedded_xmp},
		{file_group::audio, u8"au,snd"sv, u8"Sun Microsystems and NeXT audio"sv, {}},
		{file_group::video, u8"aix"sv, {}, {}},
		{file_group::audio, u8"amf"sv, {}, {}},
		{file_group::video, u8"amr"sv, u8"GSM and UMTS mobile phone video"sv, {}},
		{file_group::audio, u8"ams"sv, {}, {}},
		{file_group::audio, u8"ape"sv, u8"Monkey's Audio (APE)"sv, {}},
		{file_group::video, u8"apl"sv, {}, {}},
		{file_group::video, u8"apng"sv, {}, {}},
		{file_group::video, u8"aptx"sv, {}, {}},
		{file_group::video, u8"aptxhd"sv, {}, {}},
		{file_group::video, u8"aqt"sv, {}, {}},
		{file_group::photo, u8"arw,sr2,srf"sv, u8"Sony Raw"sv, file_type_traits::raw | file_type_traits::edit},
		{file_group::photo, u8"bay"sv, u8"Casio Raw"sv, file_type_traits::raw | file_type_traits::edit},
		{file_group::photo, u8"bmq"sv, u8"NuCore Raw"sv, file_type_traits::raw | file_type_traits::edit},
		{file_group::photo, u8"cap"sv, u8"Phase One Raw"sv, file_type_traits::raw | file_type_traits::edit},
		{file_group::photo, u8"cine"sv, u8"Phantom Raw"sv, file_type_traits::raw | file_type_traits::edit},
		{file_group::video, u8"asf"sv, u8"Windows Media"sv, file_type_traits::embedded_xmp | file_type_traits::edit},
		{file_group::video, u8"ass"sv, {}, {}},
		{file_group::video, u8"ast"sv, u8"Audio Stream"sv, {}},
		{
			file_group::video, u8"avi"sv, u8"Audio Visual Interleave"sv,
			file_type_traits::embedded_xmp | file_type_traits::edit
		},
		{file_group::video, u8"avr"sv, {}, {}},
		{file_group::video, u8"avs"sv, {}, {}},
		{file_group::video, u8"avs2"sv, {}, {}},
		{file_group::video, u8"bcstm"sv, {}, {}},
		{file_group::video, u8"bfstm"sv, {}, {}},
		{file_group::video, u8"bit"sv, {}, {}},
		{file_group::photo, u8"bmp"sv, u8"Microsoft Windows Bitmap"sv, {}},
		{file_group::video, u8"bmv"sv, {}, {}},
		{file_group::video, u8"brstm"sv, u8"Binary Revolution Stream"sv, {}},
		{file_group::video, u8"c2"sv, {}, {}},
		{file_group::video, u8"caf"sv, {}, {}},
		{file_group::video, u8"cavs"sv, {}, {}},
		{file_group::video, u8"cdata"sv, {}, {}},
		{file_group::video, u8"cdg"sv, {}, {}},
		{file_group::video, u8"cdxl"sv, {}, {}},
		{file_group::video, u8"cgi"sv, {}, {}},
		{file_group::video, u8"chk"sv, {}, {}},
		{file_group::video, u8"cif"sv, {}, {}},
		{file_group::video, u8"cpk"sv, {}, {}},
		{file_group::photo, u8"crw,cr2,cr3"sv, u8"Canon raw"sv, file_type_traits::raw | file_type_traits::edit},
		{file_group::photo, u8"cs1,sti,ia"sv, u8"Sinar raw"sv, file_type_traits::raw | file_type_traits::edit},
		{file_group::video, u8"cdxl"sv, u8"Commodore CDXL video"sv, {}},
		{file_group::video, u8"daud"sv, {}, {}},
		{file_group::video, u8"dav"sv, {}, {}},
		{file_group::audio, u8"dbm"sv, {}, {}},
		{file_group::audio, u8"cda"sv, u8"CD Audio Track"sv, {}},
		{file_group::photo, u8"dc2,dcr,drf,dsc,k25,kc2,kdc"sv, u8"Kodak raw"sv, {}},
		{file_group::audio, u8"dff"sv, {}, {}},
		{file_group::audio, u8"digi"sv, {}, {}},
		{file_group::video, u8"divx"sv, {}, {}},
		{file_group::audio, u8"dmf"sv, {}, {}},
		{
			file_group::photo, u8"dng"sv, u8"Adobe Digital Negative"sv,
			file_type_traits::raw | file_type_traits::edit | file_type_traits::embedded_xmp | file_type_traits::edit
		},
		{file_group::video, u8"dnxhd"sv, {}, {}},
		{file_group::video, u8"dnxhr"sv, {}, {}},
		{file_group::video, u8"dpx"sv, {}, {}},
		{file_group::video, u8"drc"sv, {}, {}},
		{file_group::audio, u8"dsf"sv, {}, {}},
		{file_group::audio, u8"dsm"sv, {}, {}},
		{file_group::video, u8"dss"sv, {}, {}},
		{file_group::video, u8"dtk"sv, {}, {}},
		{file_group::audio, u8"dtm"sv, {}, {}},
		{file_group::video, u8"dts"sv, u8"DTS (sound system)"sv, {}},
		{file_group::video, u8"dtshd"sv, u8"DTS (sound system)"sv, {}},
		{file_group::video, u8"dv"sv, {}, {}},
		{file_group::video, u8"dvd"sv, {}, {}},
		{file_group::video, u8"eac3"sv, {}, {}},
		{file_group::photo, u8"erf"sv, u8"Epson Raw"sv, file_type_traits::raw | file_type_traits::edit},
		{file_group::photo, u8"mfw"sv, u8"Mamiya raw"sv, file_type_traits::raw | file_type_traits::edit},
		{file_group::photo, u8"raw"sv, u8"Panasonic raw"sv, file_type_traits::raw | file_type_traits::edit},
		{file_group::photo, u8"exr"sv, {}, {}},
		{file_group::video, u8"fap"sv, {}, {}},
		{file_group::audio, u8"far"sv, {}, {}},
		{file_group::photo, u8"fff"sv, u8"Imacon raw"sv, file_type_traits::raw | file_type_traits::edit},
		{file_group::video, u8"fits"sv, {}, {}},
		{file_group::audio, u8"flac"sv, u8"ree Lossless Audio"sv, {}},
		{file_group::video, u8"flm"sv, {}, {}},
		{file_group::video, u8"flv"sv, u8"Flash video"sv, file_type_traits::embedded_xmp | file_type_traits::edit},
		{file_group::video, u8"fsb"sv, {}, {}},
		{file_group::video, u8"fwse"sv, {}, {}},
		{file_group::video, u8"g722"sv, {}, {}},
		{file_group::video, u8"g723_1"sv, {}, {}},
		{file_group::video, u8"g729"sv, {}, {}},
		{file_group::audio, u8"gdm"sv, {}, {}},
		{file_group::video, u8"genh"sv, {}, {}},
		{
			file_group::photo, u8"gif,giff"sv, u8"CompuServe's Graphics Interchange Format"sv,
			file_type_traits::embedded_xmp | file_type_traits::edit
		},
		{file_group::video, u8"gsm"sv, u8"GSM Full Rate"sv, {}},
		{file_group::video, u8"gxf"sv, {}, {}},
		{file_group::video, u8"h261"sv, {}, {}},
		{file_group::video, u8"h263"sv, {}, {}},
		{file_group::video, u8"h264"sv, {}, {}},
		{file_group::video, u8"h265"sv, {}, {}},
		{file_group::video, u8"hca"sv, {}, {}},
		{file_group::photo, u8"hdr"sv, {}, {}},
		{file_group::video, u8"hevc"sv, {}, {}},
		{file_group::audio, u8"ice"sv, {}, {}},
		{file_group::photo, u8"ico"sv, u8"Microsoft Windows icon"sv, {}},
		{file_group::audio, u8"id3"sv, {}, {}},
		{file_group::video, u8"idf"sv, {}, {}},
		{file_group::video, u8"idx"sv, {}, {}},
		{file_group::photo, u8"iff"sv, u8"ILBM"sv, {}},
		{file_group::video, u8"ifv"sv, {}, {}},
		{file_group::photo, u8"iiq"sv, u8"Phase One raw"sv, file_type_traits::raw | file_type_traits::edit},
		{file_group::audio, u8"imf"sv, {}, {}},
		{file_group::video, u8"ircam"sv, {}, {}},
		{file_group::video, u8"ism"sv, {}, {}},
		{file_group::video, u8"isma"sv, {}, {}},
		{file_group::video, u8"ismv"sv, {}, {}},
		{file_group::audio, u8"it"sv, {}, {}},
		{file_group::video, u8"ivf"sv, u8"Indeo Video Technology"sv, {}},
		{file_group::video, u8"ivr"sv, {}, {}},
		{file_group::audio, u8"j2b"sv, {}, {}},
		{file_group::photo, u8"j2c"sv, {}, {}},
		{file_group::photo, u8"j2k"sv, {}, {}},
		{file_group::photo, u8"jif"sv, {}, {}},
		{file_group::video, u8"jls"sv, {}, {}},
		{file_group::photo, u8"jp2"sv, u8"JPEG2000"sv, {}},
		{
			file_group::photo, u8"jpeg,jpg,jpe,jfif"sv, u8"Joint Photographic Experts Group"sv,
			file_type_traits::embedded_xmp | file_type_traits::edit
		},
		{file_group::photo, u8"jpx"sv, u8"Jpeg 2000"sv, file_type_traits::embedded_xmp | file_type_traits::edit},
		{file_group::video, u8"jss"sv, {}, {}},
		{file_group::photo, u8"koa"sv, {}, {}},
		{file_group::video, u8"kux"sv, {}, {}},
		{file_group::video, u8"latm"sv, {}, {}},
		{file_group::video, u8"lbc"sv, {}, {}},
		{file_group::photo, u8"lbm"sv, u8"ILBM"sv, {}},
		{file_group::video, u8"ljpg"sv, {}, {}},
		{file_group::video, u8"loas"sv, {}, {}},
		{file_group::video, u8"lrc"sv, {}, {}},
		{file_group::video, u8"lrv"sv, {}, {}},
		{file_group::video, u8"lvf"sv, {}, {}},
		{file_group::audio, u8"m15"sv, {}, {}},
		{file_group::audio, u8"mid,midi,rmi"sv, u8"Musical Instrument Digital Interface"sv, {}},
		{file_group::video, u8"m2p"sv, {}, {}},
		{file_group::video, u8"m2t"sv, {}, {}},
		{file_group::video, u8"m2ts"sv, u8"MPEG-2 TS video"sv, {}},
		{file_group::video, u8"mac"sv, {}, {}},
		{file_group::photo, u8"mdc,mrw"sv, u8"Minolta  raw"sv, file_type_traits::raw | file_type_traits::edit},
		{file_group::audio, u8"mdl"sv, {}, {}},
		{file_group::audio, u8"med"sv, {}, {}},
		{file_group::photo, u8"mef"sv, u8"Mamiya raw"sv, file_type_traits::raw | file_type_traits::edit},
		{file_group::video, u8"mj2"sv, {}, {}},
		{file_group::video, u8"mjpeg"sv, {}, {}},
		{file_group::video, u8"mjpg"sv, {}, {}},
		{file_group::video, u8"mk3d"sv, {}, {}},
		{file_group::audio, u8"mka"sv, {}, {}},
		{file_group::video, u8"mks"sv, {}, {}},
		{file_group::video, u8"mkv"sv, u8"Matroska video"sv, {}},
		{file_group::video, u8"mlp"sv, {}, {}},
		{file_group::audio, u8"mmcmp"sv, {}, {}},
		{file_group::video, u8"mmf"sv, {}, {}},
		{file_group::audio, u8"mms"sv, {}, {}},
		{file_group::photo, u8"mng"sv, u8"Multiple Network Graphics animation"sv, {}},
		{file_group::audio, u8"mo3"sv, {}, {}},
		{file_group::audio, u8"mod"sv, {}, {}},
		{file_group::photo, u8"mos"sv, u8"Leaf raw"sv, file_type_traits::raw | file_type_traits::edit},
		{
			file_group::audio, u8"mp3"sv, u8"MPEG Layer 3"sv,
			file_type_traits::embedded_xmp | file_type_traits::edit | file_type_traits::thumbnail
		},
		{
			file_group::video, u8"mov"sv, {},
			file_type_traits::embedded_xmp | file_type_traits::edit | file_type_traits::mp4
		},
		{
			file_group::video, u8"mp4,mp4a,mp4v,m4v,m4b,f4v,3g2,3gp2,3gp,3gpp,crm"sv, u8"MPEG-4"sv,
			file_type_traits::embedded_xmp | file_type_traits::edit | file_type_traits::mp4
		},
		{
			file_group::video, u8"crm"sv, u8"Canon Cinema RAW Light"sv,
			file_type_traits::embedded_xmp | file_type_traits::edit | file_type_traits::mp4
		},
		{
			file_group::audio, u8"m4a,mp4a,m4r"sv, u8"MPEG-4 Audio"sv,
			file_type_traits::embedded_xmp | file_type_traits::edit | file_type_traits::mp4 |
			file_type_traits::thumbnail
		},
		{
			file_group::audio, u8"m4p"sv, u8"MPEG-4 (DRM)"sv,
			file_type_traits::embedded_xmp | file_type_traits::edit | file_type_traits::mp4 |
			file_type_traits::thumbnail
		},
		{file_group::audio, u8"mpc"sv, u8"Musepack"sv, {}},
		{file_group::video, u8"mpd"sv, {}, {}},
		{
			file_group::video, u8"mpeg,mpg,mpe,m1v,m2v,mp2,mpv,m2p,m2t,mpe,vob,ms-pvr,dvr-ms"sv, u8"MPEG"sv,
			file_type_traits::embedded_xmp | file_type_traits::edit
		},
		{file_group::audio, u8"mpa,m2a"sv, u8"MPEG"sv, file_type_traits::embedded_xmp | file_type_traits::edit},
		{
			file_group::photo, u8"heif, heifs, heic, heics, avci, avcs, avif, avifs"sv,
			u8"High Efficiency Image File Format"sv, file_type_traits::embedded_xmp
		},
		{file_group::video, u8"avc1"sv, u8"Advanced Video Coding"sv, {}},
		{file_group::audio, u8"mptm"sv, {}, {}},
		{file_group::photo, u8"mrw"sv, {}, file_type_traits::raw | file_type_traits::edit},
		{file_group::video, u8"msbc"sv, {}, {}},
		{file_group::video, u8"msf"sv, {}, {}},
		{file_group::audio, u8"mt2"sv, {}, {}},
		{file_group::video, u8"mtaf"sv, {}, {}},
		{file_group::audio, u8"mtm"sv, {}, {}},
		{file_group::video, u8"mts"sv, {}, {}},
		{file_group::video, u8"musx"sv, {}, {}},
		{file_group::video, u8"mvi"sv, {}, {}},
		{file_group::video, u8"mxf"sv, u8"SMPTE Material Exchange Format"sv, {}},
		{file_group::video, u8"mxg"sv, {}, {}},
		{file_group::photo, u8"nef,nrw"sv, u8"Nikon  raw"sv, file_type_traits::raw | file_type_traits::edit},
		{file_group::video, u8"nist"sv, {}, {}},
		{file_group::photo, u8"nrw"sv, {}, {}},
		{file_group::video, u8"nsp"sv, {}, {}},
		{file_group::audio, u8"nst"sv, {}, {}},
		{file_group::video, u8"nut"sv, {}, {}},
		{file_group::video, u8"obu"sv, {}, {}},
		{file_group::audio, u8"oga"sv, {}, {}},
		{file_group::audio, u8"ogg"sv, u8"container, multimedia"sv, {}},
		{file_group::video, u8"ogm"sv, {}, {}},
		{file_group::video, u8"ogv"sv, {}, {}},
		{file_group::video, u8"ogx"sv, {}, {}},
		{file_group::audio, u8"okt"sv, {}, {}},
		{file_group::video, u8"oma"sv, {}, {}},
		{file_group::video, u8"omg"sv, {}, {}},
		{file_group::audio, u8"opus"sv, {}, {}},
		{file_group::photo, u8"orf"sv, u8"Olympus raw"sv, file_type_traits::raw | file_type_traits::edit},
		{file_group::video, u8"paf"sv, {}, {}},
		{file_group::video, u8"pam"sv, {}, {}},
		{file_group::photo, u8"pbm"sv, u8"Portable bitmap"sv, {}},
		{file_group::photo, u8"pcd"sv, {}, {}},
		{file_group::photo, u8"pct"sv, u8"Apple Macintosh PICT image"sv, {}},
		{file_group::photo, u8"pcx"sv, u8"ZSoft's PC Paint image"sv, {}},
		{file_group::photo, u8"pef,ptx"sv, u8"Pentax raw"sv, file_type_traits::raw | file_type_traits::edit},
		{file_group::photo, u8"pfm"sv, {}, {}},
		{file_group::photo, u8"pgm"sv, u8"Portable graymap"sv, {}},
		{file_group::video, u8"pgmyuv"sv, {}, {}},
		{file_group::photo, u8"pic"sv, {}, {}},
		{file_group::photo, u8"pict"sv, u8"Apple Macintosh PICT image"sv, {}},
		{file_group::video, u8"pjs"sv, {}, {}},
		{file_group::audio, u8"plm"sv, {}, {}},
		{
			file_group::photo, u8"png"sv, u8"Portable Network Graphic"sv,
			file_type_traits::embedded_xmp | file_type_traits::edit
		},
		{file_group::audio, u8"ppm"sv, u8"Portable Pixmap"sv, {}},
		{
			file_group::photo, u8"psd"sv, u8"Adobe Photoshop Drawing"sv,
			file_type_traits::embedded_xmp | file_type_traits::edit
		},
		{file_group::audio, u8"psm"sv, {}, {}},
		{file_group::video, u8"psp"sv, u8"Paint Shop Pro image"sv, {}},
		{file_group::audio, u8"pt36"sv, {}, {}},
		{file_group::audio, u8"ptm"sv, {}, {}},
		{file_group::video, u8"pvf"sv, {}, {}},
		{file_group::photo, u8"pxn"sv, u8"Logitech raw"sv, file_type_traits::raw | file_type_traits::edit},
		{file_group::video, u8"qcif"sv, {}, {}},
		{file_group::photo, u8"qtk"sv, u8"Apple Quicktake raw"sv, file_type_traits::raw | file_type_traits::edit},
		{file_group::audio, u8"ra, rm"sv, u8"RealAudio"sv, {}},
		{file_group::photo, u8"raf"sv, u8"Fuji raw"sv, file_type_traits::raw | file_type_traits::edit},
		{file_group::photo, u8"ras"sv, {}, {}},
		{file_group::video, u8"rco"sv, {}, {}},
		{file_group::video, u8"rcv"sv, {}, {}},
		{file_group::photo, u8"rdc"sv, u8"Digital Foto Maker raw"sv, file_type_traits::raw | file_type_traits::edit},
		{file_group::video, u8"rmd,r3d"sv, u8"RED"sv, {}},
		{file_group::video, u8"rgb"sv, u8"Silicon Graphics Image"sv, {}},
		{file_group::video, u8"rm"sv, u8"RealAudio (RA, RM)"sv, {}},
		{file_group::video, u8"roq"sv, u8"Quake 3 video"sv, {}},
		{file_group::video, u8"rsd"sv, {}, {}},
		{file_group::video, u8"rso"sv, {}, {}},
		{file_group::video, u8"rt"sv, {}, {}},
		{file_group::photo, u8"rw"sv, {}, file_type_traits::raw | file_type_traits::edit},
		{file_group::photo, u8"rw2"sv, u8"Panasonic raw"sv, file_type_traits::raw | file_type_traits::edit},
		{file_group::photo, u8"rwl"sv, u8"Leica raw"sv, file_type_traits::raw | file_type_traits::edit},
		{file_group::photo, u8"rwz"sv, u8"Rawzor raw"sv, file_type_traits::raw | file_type_traits::edit},
		{file_group::audio, u8"s3m"sv, {}, {}},
		{file_group::video, u8"sami"sv, {}, {}},
		{file_group::video, u8"sbc"sv, {}, {}},
		{file_group::video, u8"sbg"sv, {}, {}},
		{file_group::video, u8"scc"sv, {}, {}},
		{file_group::video, u8"sdr2"sv, {}, {}},
		{file_group::video, u8"sds"sv, {}, {}},
		{file_group::video, u8"sdx"sv, {}, {}},
		{file_group::video, u8"ser"sv, {}, {}},
		{file_group::video, u8"sf"sv, {}, {}},
		{file_group::audio, u8"sfx"sv, {}, {}},
		{file_group::audio, u8"sfx2"sv, {}, {}},
		{file_group::photo, u8"sgi"sv, u8"Silicon Graphics Image"sv, {}},
		{file_group::video, u8"shn"sv, u8"Shorten (SHN)"sv, {}},
		{file_group::video, u8"son"sv, {}, {}},
		{file_group::video, u8"sox"sv, {}, {}},
		{file_group::video, u8"spdif"sv, {}, {}},
		{file_group::video, u8"sph"sv, {}, {}},
		{file_group::audio, u8"spx"sv, u8"Speex low bitrate audio"sv, {}},
		{file_group::photo, u8"srw"sv, u8"Samsung raw"sv, file_type_traits::raw | file_type_traits::edit},
		{file_group::video, u8"ss2"sv, {}, {}},
		{file_group::video, u8"ssa"sv, {}, {}},
		{file_group::audio, u8"st26"sv, {}, {}},
		{file_group::audio, u8"stk"sv, {}, {}},
		{file_group::video, u8"stl"sv, {}, {}},
		{file_group::audio, u8"stm"sv, {}, {}},
		{file_group::audio, u8"stp"sv, {}, {}},
		{file_group::video, u8"str"sv, {}, {}},
		{file_group::video, u8"sub"sv, {}, {}},
		{file_group::video, u8"sup"sv, {}, {}},
		{file_group::video, u8"svag"sv, {}, {}},
		{file_group::video, u8"swf"sv, u8"Macromedia Flash"sv, file_type_traits::embedded_xmp},
		{file_group::video, u8"tak"sv, u8"Tom's Lossless Audio Kompressor"sv, {}},
		{file_group::photo, u8"tga,targa"sv, u8"Truevision TGA (Targa) image"sv, {}},
		{file_group::video, u8"tco"sv, {}, {}},
		{file_group::video, u8"thd"sv, {}, {}},
		{
			file_group::photo, u8"tiff, tif"sv, u8"Tagged Image File Format"sv,
			file_type_traits::embedded_xmp | file_type_traits::edit
		},
		{file_group::photo, u8"cin"sv, u8"Kodak Cineon Image"sv, {}},
		{file_group::photo, u8"dpx"sv, u8"Digital Picture Exchange"sv, {}},
		{file_group::video, u8"tod"sv, {}, {}},
		{file_group::video, u8"ts"sv, {}, {}},
		{file_group::audio, u8"tta"sv, u8"True Audio"sv, {}},
		{file_group::video, u8"ty"sv, {}, {}},
		{file_group::video, u8"ty+"sv, {}, {}},
		{file_group::audio, u8"ult"sv, {}, {}},
		{file_group::audio, u8"umx"sv, {}, {}},
		{file_group::video, u8"v210"sv, {}, {}},
		{file_group::video, u8"vag"sv, {}, {}},
		{file_group::video, u8"vb"sv, {}, {}},
		{file_group::video, u8"vc1"sv, {}, {}},
		{file_group::video, u8"vc2"sv, {}, {}},
		{file_group::video, u8"viv"sv, {}, {}},
		{file_group::video, u8"voc"sv, u8"Creative Labs Soundblaster"sv, {}},
		{file_group::video, u8"vpk"sv, {}, {}},
		{file_group::video, u8"vqe"sv, {}, {}},
		{file_group::video, u8"vqf"sv, u8"Yamaha TwinVQ"sv, {}},
		{file_group::video, u8"vql"sv, {}, {}},
		{file_group::video, u8"w64"sv, {}, {}},
		{file_group::photo, u8"wap"sv, {}, {}},
		{file_group::audio, u8"wav"sv, u8"Microsoft Wave"sv, file_type_traits::embedded_xmp | file_type_traits::edit},
		{file_group::photo, u8"wbm"sv, {}, {}},
		{file_group::photo, u8"wbmp"sv, {}, file_type_traits::embedded_xmp | file_type_traits::edit},
		{file_group::video, u8"web"sv, {}, {}},
		{file_group::video, u8"webm"sv, {}, {}},
		{file_group::photo, u8"webp"sv, {}, file_type_traits::embedded_xmp | file_type_traits::edit},
		{
			file_group::audio, u8"wma"sv, u8"Windows Media Audio 9"sv,
			file_type_traits::embedded_xmp | file_type_traits::edit
		},
		{
			file_group::video, u8"wmv,wm"sv, u8"Windows Media video"sv,
			file_type_traits::embedded_xmp | file_type_traits::edit
		},
		{file_group::audio, u8"wow"sv, {}, {}},
		{file_group::video, u8"wsd"sv, {}, {}},
		{file_group::video, u8"wtv"sv, {}, {}},
		{file_group::audio, u8"wv"sv, u8"WavPack"sv, {}},
		{file_group::photo, u8"x3f"sv, u8"Sigma raw"sv, file_type_traits::raw | file_type_traits::edit},
		{file_group::photo, u8"xbm"sv, u8"X Window System Bitmap"sv, {}},
		{file_group::video, u8"xl"sv, {}, {}},
		{file_group::audio, u8"xm"sv, {}, {}},
		{file_group::video, u8"xmv"sv, {}, {}},
		{file_group::audio, u8"xpk"sv, {}, {}},
		{file_group::photo, u8"xpm"sv, u8"X Window System Pixmap"sv, {}},
		{file_group::video, u8"xvag"sv, {}, {}},
		{file_group::audio, u8"xx"sv, {}, {}},
		{file_group::video, u8"y4m"sv, {}, {}},
		{file_group::video, u8"yop"sv, {}, {}},
		{file_group::video, u8"yuv"sv, u8"Raw YUV video format"sv, {}},
		{file_group::video, u8"yuv10"sv, {}, {}},

		{ file_group::archive, u8"zip"sv, {}, { file_type_traits::archive } },
		{ file_group::archive, u8"rar"sv, {}, { file_type_traits::archive } },
		{ file_group::archive, u8"7z"sv, {}, { file_type_traits::archive } },
		{ file_group::archive, u8"gz"sv, {}, { file_type_traits::archive } },
		{ file_group::archive, u8"tgz"sv, {}, { file_type_traits::archive } },
		{ file_group::archive, u8"cpio"sv, {}, { file_type_traits::archive } },
		{ file_group::archive, u8"iso"sv, {}, { file_type_traits::archive } },
		{ file_group::archive, u8"cab"sv, {}, { file_type_traits::archive } },
		{ file_group::archive, u8"pax"sv, {}, { file_type_traits::archive } },
		{ file_group::archive, u8"lzip"sv, {}, { file_type_traits::archive } },
		{ file_group::archive, u8"lza"sv, {}, { file_type_traits::archive } },
		{ file_group::archive, u8"bzip2,bz2"sv, {}, { file_type_traits::archive } },
		{ file_group::archive, u8"tar"sv, {}, { file_type_traits::archive } },
		{ file_group::archive, u8"lha"sv, {}, { file_type_traits::archive } },
		{ file_group::archive, u8"lza"sv, {}, { file_type_traits::archive } },
		{ file_group::archive, u8"a,ar"sv, {}, { file_type_traits::archive } },

		{ file_group::commodore, u8"d64"sv, {}, { file_type_traits::disk_image | file_type_traits::commodore } },
		{ file_group::commodore, u8"d81"sv, {}, { file_type_traits::disk_image | file_type_traits::commodore } },
		{ file_group::commodore, u8"t64"sv, {}, { file_type_traits::commodore } },
		{ file_group::commodore, u8"crt"sv, {}, { file_type_traits::commodore } },
	};

	int next_id = 0;

	for (const auto g : s_config.groups)
	{
		s_config.groups_by_name[g->name] = g;
		g->id = next_id;
		next_id += 1;
	}

	for (auto& ft : s_config.types)
	{
		auto& sidecars = ft.sidecars;
		sidecars.insert(sidecars.end(), ft.group->sidecars.begin(), ft.group->sidecars.end());
		std::ranges::sort(sidecars);
		sidecars.erase(std::ranges::unique(sidecars).begin(), sidecars.end());

		if (ft.icon == icon_index::document)
		{
			ft.icon = ft.group->icon;
		}

		if (!ft.extension.empty())
		{
			str::split2(ft.extension, true, [pft = &ft](const std::u8string_view ext)
			{
				s_config.types_by_name.insert_or_assign(str::cache(ext), pft);
			});
		}
	}

	av_initialise(s_config.types_by_name);
}

//static file_tool_by_name s_tools_by_name;
//static std::vector<df::folder_path> s_tool_paths;

void load_tools()
{
	file_tool_by_name tools_by_name;
	std::vector<df::folder_path> tool_paths;
	file_tool_by_extension tools_by_ext;

	try
	{
		const auto json = blob_from_file(df::probe_data_file(u8"diffractor-tools.json"sv));

		if (!json.empty())
		{
			df::util::json::json_doc document;
			document.Parse(std::bit_cast<const char8_t*>(json.data()), json.size());

			const auto& tools = document[u8"tools"];

			if (tools.IsObject())
			{
				for (const auto& m : tools.GetObject())
				{
					if (str::icmp(m.name.GetString(), u8"folders"sv) == 0)
					{
						if (m.value.IsArray())
						{
							for (const auto& folder : m.value.GetArray())
							{
								tool_paths.emplace_back(df::folder_path(folder.GetString()));
							}
						}
					}

					if (str::icmp(m.name.GetString(), u8"apps"sv) == 0)
					{
						if (m.value.IsArray())
						{
							for (const auto& app : m.value.GetArray())
							{
								if (app.IsObject())
								{
									auto* tool = new file_tool();

									for (const auto& a : app.GetObject())
									{
										if (str::icmp(a.name.GetString(), u8"exe"sv) == 0)
											tool->exe = str::cache(
												a.value.GetString());
										if (str::icmp(a.name.GetString(), u8"invoke"sv) == 0)
											tool->invoke_text =
												str::cache(a.value.GetString());
										if (str::icmp(a.name.GetString(), u8"text"sv) == 0)
											tool->text = str::cache(
												a.value.GetString());
										if (str::icmp(a.name.GetString(), u8"extensions"sv) == 0)
											tool->extensions =
												str::cache(a.value.GetString());
									}

									for (const auto& ext : split(tool->extensions, true))
									{
										tools_by_ext[str::cache(ext)] = tool;
									}

									tools_by_name[tool->exe] = tool;
								}
							}
						}
					}
				}
			}
		}
	}
	catch (std::exception& e)
	{
		df::log(__FUNCTION__, e.what());
	}

	df::hash_map<std::u8string, df::file_path, df::ihash, df::ieq> exe_by_name;

	for (const auto& folder : tool_paths)
	{
		const auto exe_selector = df::item_selector(folder, true, u8"*.exe"sv);
		const auto files = platform::select_files(exe_selector, true);

		for (const auto& f : files)
		{
			auto path = f.folder.combine_file(f.name);
			exe_by_name[std::u8string(path.file_name_without_extension())] = path;
		}
	}

	for (const auto& tool : tools_by_name)
	{
		const auto found = exe_by_name.find(std::u8string(tool.second->exe));

		if (found != exe_by_name.end())
		{
			tool.second->exe_path = found->second;
		}
	}

	for (auto& ft : s_config.types)
	{
		if (!ft.extension.empty())
		{
			str::split2(ft.extension, true, [&ft, &tools_by_ext](const std::u8string_view ext)
			{
				const auto found = tools_by_ext.find(ext);

				if (found != tools_by_ext.end())
				{
					ft.tools.emplace_back(found->second);
				}
			});
		}
	}
}

std::vector<file_group_ref> all_file_groups()
{
	std::vector<file_group_ref> result;
	result.reserve(s_config.groups.size());

	for (auto* const g : s_config.groups)
	{
		result.emplace_back(g);
	}

	return result;
}

sizei file_load_result::dimensions() const
{
	if (success)
	{
		if (is_valid(i)) return i->dimensions();
		if (is_valid(s)) return s->dimensions();
	}
	return {};
}

ui::const_surface_ptr file_load_result::to_surface(const sizei scale_hint, const bool can_use_yuv) const
{
	if (success)
	{
		files ff;

		if (is_valid(s))
		{
			return ff.scale_if_needed(s, scale_hint);
		}

		if (is_valid(i))
		{
			return ff.image_to_surface(i, scale_hint, can_use_yuv);
		}
	}

	return {};
}

ui::pixel_difference_result file_load_result::calc_pixel_difference(const file_load_result& other) const
{
	const sizei scale_hint = { 1024 * 4, 1024 * 4 };
	auto s1 = to_surface(scale_hint);
	auto s2 = other.to_surface(scale_hint);

	if (s1 && s2)
	{
		return s1->pixel_difference(s2);
	}

	return ui::pixel_difference_result::unknown;
}


file_type_ref files::file_type_from_name(const df::file_path path)
{
	return file_type_from_name(path.name());
}

file_type_ref files::file_type_from_name(const std::u8string_view name)
{
	//if (df::file_path::is_original(name)) return file_type::sidecar;

	if (!str::is_empty(name))
	{
		auto ext = name.substr(df::find_ext(name));

		if (!ext.empty())
		{
			if (ext[0] == L'.') ext = ext.substr(1);
			auto found = s_config.types_by_name.find(ext);
			if (found == s_config.types_by_name.cend()) found = s_config.types_by_name.find(name); // Try whole name?
			if (found != s_config.types_by_name.cend()) return found->second;
		}
	}

	return file_type::other;
}

bool image_edits::has_changes(const sizei image_extent) const
{
	return _scale.cx != 0 || _scale.cy != 0 || has_crop(image_extent) || has_rotation() || has_color_changes();
}

bool image_edits::has_crop(const sizei image_extent) const
{
	if (_crop.is_empty()) return false;
	quadd bounds(image_extent);
	return !_crop.has_point(bounds[0]) || !_crop.has_point(bounds[1]) || !_crop.has_point(bounds[2]) || !_crop.
		has_point(bounds[3]);
}

bool image_edits::is_no_loss(const sizei image_extent) const
{
	return !has_crop(image_extent) && !has_scale() && !has_color_changes();
}

double image_edits::rotation_angle() const
{
	return _crop.angle();
}

bool files::can_save(const df::file_path path)
{
	const auto ext = path.extension();
	return can_save_extension(ext);
}

bool files::can_save_extension(const std::u8string_view ext)
{
	static const df::hash_set<std::u8string_view, df::ihash, df::ieq> save_extensions = {
		{u8".jpg"},
		{u8".jpeg"},
		{u8".jpe"},
		{u8".png"},
		{u8".webp"},
		{u8"jpg"},
		{u8"jpeg"},
		{u8"jpe"},
		{u8"png"},
		{u8"webp"},
	};

	return save_extensions.contains(ext);
}

bool files::is_raw(const df::file_path path)
{
	const auto* const mt = file_type_from_name(path);
	return (mt->traits & file_type_traits::raw) != file_type_traits::none;
}

bool files::is_raw(const std::u8string_view name)
{
	const auto* const mt = file_type_from_name(name);
	return (mt->traits & file_type_traits::raw) != file_type_traits::none;
}

bool files::is_jpeg(const df::file_path path)
{
	const auto ext = path.extension();
	return str::icmp(ext, u8".jpg"sv) == 0 || str::icmp(ext, u8".jpeg"sv) == 0 || str::icmp(ext, u8".jpe"sv) == 0;
}

bool files::is_jpeg(const std::u8string_view name)
{
	const auto ext = name.substr(df::find_ext(name));
	return str::icmp(ext, u8".jpg"sv) == 0 || str::icmp(ext, u8".jpeg"sv) == 0 || str::icmp(ext, u8".jpe"sv) == 0;
}

static bool is_heif(const df::cspan image_buffer_in)
{
	if (image_buffer_in.size < 12u)
	{
		return false;
	}

	const std::array<uint8_t, 4> ftyp_header = {'f', 't', 'y', 'p'};
	const std::array<std::array<uint8_t, 4>, 10> brand = {
		{
			{'h', 'e', 'i', 'c'},
			{'h', 'e', 'i', 'x'},
			{'h', 'e', 'v', 'c'},
			{'h', 'e', 'v', 'x'},
			{'h', 'e', 'i', 'm'},
			{'h', 'e', 'i', 's'},
			{'h', 'e', 'v', 'm'},
			{'h', 'e', 'v', 's'},
			{'m', 'i', 'f', '1'},
			{'m', 's', 'f', '1'},
		}
	};

	if (!std::equal(std::begin(ftyp_header), std::end(ftyp_header), image_buffer_in.data + 4))
		return false;

	return std::any_of(std::begin(brand), std::end(brand), [image_buffer_in](const auto& b)
	{
		return std::equal(std::begin(b), std::end(b), image_buffer_in.data + 8);
	});
}

inline bool is_avif(const df::cspan image_buffer_in)
{
	if (image_buffer_in.size < 12u)
	{
		return false;
	}

	const std::array<unsigned char, 4> ftyp_header = { 'f', 't', 'y', 'p' };
	const std::array<std::array<unsigned char, 4>, 2> brand = { {
		{'a', 'v', 'i', 'f'},
		{'a', 'v', 'i', 's'},
	} };

	if (!std::equal(std::begin(ftyp_header), std::end(ftyp_header), image_buffer_in.data + 4))
		return false;

	return std::any_of(std::begin(brand), std::end(brand), [image_buffer_in](const auto& b)
		{
			return std::equal(std::begin(b), std::end(b), image_buffer_in.data + 8);
		});
}

detected_format files::detect_format(const df::cspan image_buffer_in)
{
	// https://en.wikipedia.org/wiki/List_of_file_signatures

	if (image_buffer_in.size >= 4)
	{
		const auto header32 = *std::bit_cast<const uint32_t*>(image_buffer_in.data);

		if (header32 == 0x53504238)
		{
			return detected_format::PSD;
		}

		// 47 49 46 38 
		if (header32 == 0x38464947)
		{
			return detected_format::GIF;
		}

		const auto header16 = *std::bit_cast<const uint16_t*>(&header32);

		switch (header16)
		{
		case 0xD8FF: return detected_format::JPEG;
		case 0x4D42: return detected_format::BMP;
		case 0x5089: return detected_format::PNG;
		case 0x4949: return detected_format::TIFF;
		case 0x4d4d: return detected_format::TIFF;
		//case 0x4947: return file_format2::GIF;
		case 0x4952: return detected_format::WEBP;
		}
	}

	if (is_heif(image_buffer_in) || is_avif(image_buffer_in))
	{
		return detected_format::HEIF;
	}

	return detected_format::Unknown;
}

files::files()
{
}

files::~files()
{
	_scaler.reset();
}

ui::const_image_ptr files::surface_to_image(const ui::const_surface_ptr& surface_in, const metadata_parts& metadata,
                                            const file_encode_params& params, const ui::image_format format)
{
	ui::const_image_ptr result;

	if (is_valid(surface_in))
	{
		const auto dimensions = surface_in->dimensions();
		const auto has_alpha = surface_in->format() == ui::texture_format::ARGB;
		const auto orientation = surface_in->orientation();

		if (format == ui::image_format::PNG ||
			(format == ui::image_format::Unknown && has_alpha))
		{
			result = save_png(surface_in, metadata);
		}
		else if (format == ui::image_format::WEBP)
		{
			result = save_webp(surface_in, metadata, params);
		}
		else
		{
			result = std::make_shared<ui::image>(
				_jpeg_encoder.encode(dimensions.cx, dimensions.cy, surface_in->pixels(), surface_in->stride(),
				                     orientation, metadata, params), dimensions, ui::image_format::JPEG, orientation);
		}
	}

	return result;
}

ui::surface_ptr files::scale_if_needed(ui::surface_ptr surface_in, const sizei target_extent)
{
	ui::surface_ptr result;

	if (is_valid(surface_in))
	{
		const auto dimensions_out = ui::scale_dimensions(surface_in->dimensions(), target_extent);

		if (surface_in->dimensions() == dimensions_out || target_extent.is_empty())
		{
			std::swap(surface_in, result);
		}
		else
		{
			if (!_scaler)
			{
				_scaler = std::make_unique<av_scaler>();
			}

			auto surface = std::make_shared<ui::surface>();
			_scaler->scale_surface(surface_in, surface, dimensions_out);
			result = surface;
		}
	}

	return result;
}

ui::const_surface_ptr files::scale_if_needed(ui::const_surface_ptr surface_in, const sizei target_extent)
{
	ui::const_surface_ptr result;

	if (is_valid(surface_in))
	{
		const auto dimensions_out = ui::scale_dimensions(surface_in->dimensions(), target_extent);

		if (surface_in->dimensions() == dimensions_out || target_extent.is_empty())
		{
			std::swap(surface_in, result);
		}
		else
		{
			if (!_scaler)
			{
				_scaler = std::make_unique<av_scaler>();
			}

			auto surface = std::make_shared<ui::surface>();
			_scaler->scale_surface(surface_in, surface, dimensions_out);
			result = surface;
		}
	}

	return result;
}

ui::pixel_difference_result files::pixel_difference(const ui::const_image_ptr& expected, const ui::const_image_ptr& actual)
{
	if (!is_empty(expected) || !is_empty(actual))
	{
		const auto expected_surface = image_to_surface(expected);
		const auto actual_surface = image_to_surface(actual);

		if (!is_empty(expected_surface) && !is_empty(actual_surface))
		{
			return expected_surface->pixel_difference(actual_surface);
		}
	}

	return ui::pixel_difference_result::unknown;
}


ui::surface_ptr files::image_to_surface(const ui::const_image_ptr& image, const sizei target_extent,
                                        const bool can_use_yuv)
{
	ui::surface_ptr surface_result;

	try
	{
		if (is_valid(image))
		{
			const auto& image_buffer_in = image->data();
			const auto format = image->format();

			if (!_scaler)
			{
				_scaler = std::make_unique<av_scaler>();
			}

			if (format == ui::image_format::JPEG)
			{
				ui::surface_ptr temp_surface;
				bool success = false;

				try
				{
					if (_jpeg_decoder.read_header(image_buffer_in))
					{
						const auto scale_hint = ui::calc_scale_down_factor(_jpeg_decoder.dimensions(), target_extent);

						if (_jpeg_decoder.start_decompress(scale_hint, false))
						{
							const auto dimensions = _jpeg_decoder.dimensions_out();
							temp_surface = std::make_shared<ui::surface>();
							temp_surface->alloc(dimensions, ui::texture_format::RGB, image->orientation());
							success = _jpeg_decoder.read_rgb(temp_surface->pixels(), temp_surface->stride(),
							                                 temp_surface->size());
							_jpeg_decoder.close();
						}
					}
				}
				catch (std::exception& e)
				{
					df::log(__FUNCTION__, e.what());
					_jpeg_decoder.close();
				}

				if (success)
				{
					surface_result = scale_if_needed(std::move(temp_surface), target_extent);
				}
			}
			else if (format == ui::image_format::PNG)
			{
				try
				{
					auto loaded = load_png(image->data());

					if (is_valid(loaded))
					{
						surface_result = scale_if_needed(std::move(loaded), target_extent);
					}
				}
				catch (std::exception& e)
				{
					df::log(__FUNCTION__, e.what());
				}
			}
			else if (format == ui::image_format::WEBP)
			{
				try
				{
					auto loaded = load_webp(image->data());

					if (is_valid(loaded))
					{
						surface_result = scale_if_needed(std::move(loaded), target_extent);
					}
				}
				catch (std::exception& e)
				{
					df::log(__FUNCTION__, e.what());
				}
			}
		}
	}
	catch (std::exception& e)
	{
		df::log(__FUNCTION__, e.what());
	}

	return surface_result;
}

ui::surface_ptr files::image_to_surface(df::cspan image_buffer_in, const sizei target_extent, const bool can_use_yuv)
{
	ui::surface_ptr surface_result;

	try
	{
		if (!image_buffer_in.empty())
		{
			const auto format = detect_format(image_buffer_in);

			if (!_scaler)
			{
				_scaler = std::make_unique<av_scaler>();
			}

			if (format == detected_format::JPEG)
			{
				auto temp_surface = std::make_shared<ui::surface>();
				bool success = false;

				try
				{
					if (_jpeg_decoder.read_header(image_buffer_in))
					{
						const auto scale_hint = ui::calc_scale_down_factor(_jpeg_decoder.dimensions(), target_extent);

						if (_jpeg_decoder.start_decompress(scale_hint, false))
						{
							const auto dimensions = _jpeg_decoder.dimensions_out();
							temp_surface->alloc(dimensions, ui::texture_format::RGB, _jpeg_decoder._orientation_out);
							success = _jpeg_decoder.read_rgb(temp_surface->pixels(), temp_surface->stride(),
							                                 temp_surface->size());
							_jpeg_decoder.close();
						}
					}
				}
				catch (std::exception& e)
				{
					df::log(__FUNCTION__, e.what());
					_jpeg_decoder.close();
				}

				if (success)
				{
					surface_result = scale_if_needed(std::move(temp_surface), target_extent);
				}
			}
			else if (format == detected_format::PSD)
			{
				mem_read_stream stream(image_buffer_in);
				auto loaded = load_psd(stream);

				if (is_valid(loaded))
				{
					surface_result = scale_if_needed(std::move(loaded), target_extent);
				}
			}
			else if (format == detected_format::PNG)
			{
				try
				{
					auto loaded = load_png(image_buffer_in);

					if (is_valid(loaded))
					{
						surface_result = scale_if_needed(std::move(loaded), target_extent);
					}
				}
				catch (std::exception& e)
				{
					df::log(__FUNCTION__, e.what());
				}
			}
			else if (format == detected_format::WEBP)
			{
				try
				{
					auto loaded = load_webp(image_buffer_in);

					if (is_valid(loaded))
					{
						surface_result = scale_if_needed(std::move(loaded), target_extent);
					}
				}
				catch (std::exception& e)
				{
					df::log(__FUNCTION__, e.what());
				}
			}
			else if (format == detected_format::HEIF)
			{
				try
				{
					mem_read_stream stream(image_buffer_in);
					auto loaded = load_heif(stream);

					if (is_valid(loaded))
					{
						surface_result = scale_if_needed(std::move(loaded), target_extent);
					}
				}
				catch (std::exception& e)
				{
					df::log(__FUNCTION__, e.what());
				}
			}

			if (is_empty(surface_result))
			{
				surface_result = platform::image_to_surface(image_buffer_in, target_extent);
			}
		}
	}
	catch (std::exception& e)
	{
		df::log(__FUNCTION__, e.what());
	}

	return surface_result;
}

static uint64_t round_up_to_multiple(uint64_t n, uint64_t multiple)
{
	if (multiple == 0)
		return n;

	const auto remainder = n % multiple;

	if (remainder == 0)
		return n;

	return n + multiple - remainder;
}

void file_read_stream::read(uint64_t pos, uint8_t* buffer, size_t len)
{
	load_buffer(pos, len);
	memcpy(buffer, _buffer + pos - _loaded_start_pos, len);
}

void file_read_stream::load_buffer(uint64_t pos, size_t len)
{
	const auto wanted_end_pos = pos + len;

	if (wanted_end_pos > _loaded_end_pos || pos < _loaded_start_pos)
	{
		if (wanted_end_pos > _file_size)
		{
			const auto message = str::format(u8"invalid read past end of file: {}"sv, _h->path());
			df::log(__FUNCTION__, message);
			throw app_exception(message);
		}

		const auto new_start_pos = (pos > _block_size) ? round_up_to_multiple(pos - _block_size, _block_size) : 0;
		const auto new_end_pos = std::min(round_up_to_multiple(wanted_end_pos, _block_size), _file_size);
		const auto existing_buffer_size = _loaded_end_pos - _loaded_start_pos;
		const auto new_buffer_size = new_end_pos - new_start_pos;

		if (existing_buffer_size != new_buffer_size)
		{
			if (_buffer == nullptr)
			{
				_buffer = static_cast<uint8_t*>(_aligned_malloc(new_buffer_size, 16));
			}
			else
			{
				_buffer = static_cast<uint8_t*>(_aligned_realloc(_buffer, new_buffer_size, 16));
			}

			_buffer_size = new_buffer_size;
		}

		if (!_buffer)
		{
			const auto message = str::format(u8"buffer alloc failed: {}"sv, _h->path());
			df::log(__FUNCTION__, message);
			throw std::bad_alloc();
		}

		/*if (_buffer && new_start_pos != _loaded_start_pos)
		{
			std::memmove(_buffer, )
		}*/

		if (_h->seek(new_start_pos, platform::file::whence::begin) != new_start_pos)
		{
			const auto message = str::format(u8"invalid load_buffer seek: {} {}"sv, new_start_pos, _h->path());
			df::log(__FUNCTION__, message);
			throw app_exception(message);
		}

		const auto wanted = new_end_pos - new_start_pos;

		if (_h->read(_buffer, wanted) != wanted)
		{
			const auto message = str::format(u8"invalid load_buffer read: {} {}"sv, wanted, _h->path());
			df::log(__FUNCTION__, message);
			throw app_exception(message);
		}

		_loaded_start_pos = new_start_pos;
		_loaded_end_pos = new_end_pos;
	}
}

bool file_read_stream::open(const df::file_path path)
{
	return open(open_file(path, platform::file_open_mode::sequential_scan));
}

bool file_read_stream::open(platform::file_ptr h)
{
	_h = std::move(h);

	if (_h)
	{
		_file_size = _h->size();
		_block_size = df::sixty_four_k; // platform::calc_optimal_read_size(path);
		return true;
	}

	return false;
}

void file_read_stream::close()
{
	_h.reset();

	if (_buffer)
	{
		_aligned_free(_buffer);
		_buffer = nullptr;
	}
}

file_read_stream::~file_read_stream()
{
	close();
}

bool files::save(const df::file_path path, const file_load_result& loaded)
{
	const auto save_format = extension_to_format(path.extension());
	ui::const_image_ptr saved;

	if (is_valid(loaded.i) && loaded.i->format() == save_format)
	{
		saved = loaded.i;
	}
	else
	{
		file_encode_params encode_params;
		encode_params.jpeg_save_quality = setting.convert.jpeg_quality;
		encode_params.webp_quality = setting.convert.webp_quality;
		encode_params.webp_lossless = setting.convert.webp_lossless;
		saved = surface_to_image(loaded.to_surface(), {}, encode_params, save_format);
	}

	return is_valid(saved) && blob_save_to_file(saved->data(), path);
}

std::u8string normalize_string_trailing_null(std::u8string operand)
{
	if (!operand.empty() && (operand.back() != '\0'))
	{
		operand.append(1, '\0');
	}

	return operand;
}

file_scan_result files::scan_file(const df::file_path path, const bool load_thumb, const file_type_ref ft,
                                  const std::u8string_view xmp_sidecar, const sizei max_thumb_size)
{
	file_scan_result result;

	try
	{
		const auto f = open_file(path, platform::file_open_mode::read);

		if (f)
		{
			const auto file_len = f->size();
			const bool is_bitmap = ft->has_trait(file_type_traits::bitmap);
			const auto is_raw = ft->has_trait(file_type_traits::raw);
			const auto is_small_file = file_len < df::two_fifty_six_k;
			const auto load_from_mem = load_thumb && !is_raw && is_bitmap;

			df::blob data;

			if (is_small_file || load_from_mem)
			{
				data.resize(file_len);
				const auto read = f->read(data.data(), file_len);
				if (read != file_len) return result;
				f->seek(0, platform::file::whence::begin);
			}

			if (is_bitmap)
			{
				if (is_raw)
				{
					result = scan_raw(path, xmp_sidecar, load_thumb, max_thumb_size);
				}
				else if (!data.empty())
				{
					mem_read_stream stream(data);
					result = scan_photo(stream);

					if (is_image_format(detect_format(stream.peek128(0))))
					{
						result.thumbnail_image = load_image_file(data);
					}
					else
					{
						if (load_thumb)
						{
							auto s = image_to_surface(data, max_thumb_size);

							if (is_valid(s))
							{
								file_encode_params params;
								auto i = surface_to_image(s, {}, params, ui::image_format::Unknown);

								if (is_valid(i))
								{
									result.thumbnail_image = std::move(i);
								}
							}
						}
					}
				}
				else
				{
					file_read_stream stream;

					if (stream.open(f))
					{
						result = scan_photo(stream);
					}
				}
			}
			else if (ft->has_trait(file_type_traits::av))
			{
				/*if (ft->traits && file_type_traits::embedded_xmp)
				{
					result = scan_xmp(path);
				}*/

				av_format_decoder decoder;

				if (decoder.open(f, path))
				{
					result.cover_art = decoder.cover_art();

					if (load_thumb)
					{
						decoder.init_streams(-1, -1, false, true, false);

						int pos_numerator = 10;
						int pos_denominator = 100;

						if (!ft->has_trait(file_type_traits::thumbnail))
						{
							pos_numerator = 0;
						}

						ui::surface_ptr thumbnail_surface;

						if (decoder.extract_thumbnail(thumbnail_surface, max_thumb_size, pos_numerator,
							pos_denominator))
						{
							result.thumbnail_surface = std::move(thumbnail_surface);
						}
					}

					decoder.extract_metadata(result);
					result.success = true;
				}

				/*if (!success)
				{
					file_scanner result;

					if (result.parse(path, {}))
					{
						if (!result.thumbnail.is_empty())
						{
							surface_out = image_to_surface(ui::const_image_ptr(result.thumbnail), target_extent);
							success = !surface_out.is_empty();
						}
					}
				}*/
			}

			if (!data.empty())
			{
				result.crc32c = crypto::crc32c(data.data(), data.size());
			}
		}
	}
	catch (std::exception& e)
	{
		df::log(__FUNCTION__, e.what());
	}

	return result;
}

ui::image_ptr load_image_file(df::cspan file)
{
	ui::image_ptr result;
	mem_read_stream stream(file);

	if (stream.size() > 16)
	{
		const auto detected = files::detect_format(stream.peek128(0));

		if (is_image_format(detected))
		{
			const auto info = scan_photo(stream);
			auto format = ui::image_format::Unknown;

			switch (info.format)
			{
			case detected_format::JPEG:
				format = ui::image_format::JPEG;
				break;
			case detected_format::PNG:
				format = ui::image_format::PNG;
				break;
			case detected_format::WEBP:
				format = ui::image_format::WEBP;
				break;
			default: ;
			}

			result = std::make_shared<ui::image>(file, info.dimensions(), format, info.orientation);
		}
		else if (detected != detected_format::Unknown)
		{
			files ff;
			result = save_png(ff.image_to_surface(file, {}), {});
		}
	}

	return result;
}


file_load_result files::load(const df::file_path path, bool can_load_preview)
{
	df::last_loaded_path = path;

	file_load_result result;
	const auto* const mt = file_type_from_name(path);

	if (mt->has_trait(file_type_traits::bitmap))
	{
		if (mt->has_trait(file_type_traits::raw))
		{
			result = load_raw(path, can_load_preview);
		}
		else
		{
			const auto file = blob_from_file(path);

			if (!file.empty())
			{
				mem_read_stream stream(file);

				if (stream.size() > 16)
				{
					const auto detected = detect_format(stream.peek128(0));

					if (is_image_format(detected))
					{
						result.i = load_image_file(file);
						result.success = is_valid(result.i);
					}
					else if (detected != detected_format::Unknown)
					{
						switch (detected)
						{
						case detected_format::PSD:
							result.s = load_psd(stream);
							result.success = is_valid(result.s);
							break;

						case detected_format::HEIF:
							result.s = load_heif(stream);
							result.success = is_valid(result.s);
							break;

						case detected_format::GIF:
						case detected_format::BMP:
						case detected_format::TIFF:
							result.s = image_to_surface(file, {});
							result.success = is_valid(result.s);
							break;

						default: ;
						}
					}
				}
			}
		}
	}
	else if (mt->has_trait(file_type_traits::av))
	{
		// Not supported
	}

	return result;
}

static void patch_file(const df::file_path path, uint64_t offset, const uint8_t* data, size_t len)
{
	const auto f = open_file(path, platform::file_open_mode::write);

	if (f)
	{
		if (f->seek(offset, platform::file::whence::begin) == offset)
		{
			f->write(data, len);
		}
	}
}

static simple_transform angle_to_transform(int a)
{
	if (a == -90 || a == 270)
	{
		return simple_transform::rot_90;
	}

	if (a == 90 || a == -270)
	{
		return simple_transform::rot_270;
	}

	if (a == 180 || a == -180)
	{
		return simple_transform::rot_180;
	}

	return simple_transform::none;
}


ui::image_format extension_to_format(const std::u8string_view ext)
{
	static const df::hash_map<std::u8string_view, ui::image_format, df::ihash, df::ieq> extensions =
	{
		{u8".jpg"sv, ui::image_format::JPEG},
		{u8".jpeg"sv, ui::image_format::JPEG},
		{u8".jpe"sv, ui::image_format::JPEG},
		{u8".png"sv, ui::image_format::PNG},
		{u8".webp"sv, ui::image_format::WEBP},
		{u8"jpg"sv, ui::image_format::JPEG},
		{u8"jpeg"sv, ui::image_format::JPEG},
		{u8"jpe"sv, ui::image_format::JPEG},
		{u8"png"sv, ui::image_format::PNG},
		{u8"webp"sv, ui::image_format::WEBP},
	};

	const auto found = extensions.find(ext);

	if (found != extensions.end())
	{
		return found->second;
	}

	return ui::image_format::Unknown;
}

ui::image_ptr save_surface(const ui::image_format& format, const ui::const_surface_ptr& surface,
                           const metadata_parts& metadata, const file_encode_params& params)
{
	if (format == ui::image_format::JPEG)
	{
		return save_jpeg(surface, metadata, params);
	}
	if (format == ui::image_format::PNG)
	{
		return save_png(surface, metadata);
	}
	if (format == ui::image_format::WEBP)
	{
		return save_webp(surface, metadata, params);
	}

	return {};
}


platform::file_op_result files::update(const df::file_path path_src, const df::file_path path_dst,
                                       const metadata_edits& metadata_edits, const image_edits& photo_edits,
                                       const file_encode_params& params, const bool create_original,
                                       const std::u8string_view xmp_name)
{
	platform::file_op_result result = {platform::file_op_result_code::OK};

	bool temp_file_created = false;
	const auto path_temp = platform::temp_file(path_dst.extension(), path_dst.folder());

	try
	{
		//bool success = true;
		bool has_photo_edits = false;

		const auto* const mt = file_type_from_name(path_src);
		const auto file_attributes = platform::file_attributes(path_src);
		const auto file_size = file_attributes.size;
		const auto file_modified = file_attributes.modified;
		const auto file_created = file_attributes.created;
		const auto extension_change = str::icmp(path_dst.extension(), path_src.extension()) != 0;
		const auto path_change = path_dst != path_src;

		file_scan_result scan_result;

		if (mt->has_trait(file_type_traits::bitmap))
		{
			file_read_stream stream;

			if (stream.open(path_src))
			{
				scan_result = scan_photo(stream);
				has_photo_edits = photo_edits.has_changes(scan_result.dimensions()) || extension_change;
			}
		}

		if (has_photo_edits)
		{
			const auto loaded = load(path_src, false);
			temp_file_created = true;

			if (loaded.success)
			{
				const auto dimensions_in = loaded.dimensions();
				const auto dst_path_is_jpeg = is_jpeg(path_dst);

				if (ui::is_jpeg(loaded.i) && dst_path_is_jpeg && photo_edits.is_no_loss(dimensions_in) && params.
					jpeg_save_quality >= 75)
				{
					const auto saved = _jpeg_decoder.transform(loaded.i->data(), _jpeg_encoder,
					                                           angle_to_transform(
						                                           df::round(photo_edits.rotation_angle())));

					if (saved.empty() || !blob_save_to_file(saved, path_temp))
					{
						result.code = platform::file_op_result_code::FAILED;
					}
				}
				else
				{
					const auto temp_surface = loaded.to_surface()->transform(photo_edits);

					if (is_empty(temp_surface))
					{
						result.code = platform::file_op_result_code::FAILED;
					}
					else
					{
						const auto saved = save_surface(extension_to_format(path_temp.extension()), temp_surface,
						                                scan_result.save_metadata(), params);

						if (is_empty(saved) || !blob_save_to_file(saved->data(), path_temp))
						{
							result.code = platform::file_op_result_code::FAILED;
						}
					}
				}
			}
		}
		else
		{
			// use temp file for anything under 5 meg
			const auto ten_megabytes = 10ull * 1024ull * 1024ull;

			if (file_size <= ten_megabytes || path_change)
			{
				result = platform::copy_file(path_src, path_temp, true, false);
				temp_file_created = result.success();
			}
		}

		xmp_update_result xmp_result;

		if (result.success())
		{
			const auto has_metadata_changes = metadata_edits.has_changes();

			if (has_metadata_changes || path_change || has_photo_edits)
			{
				const auto metadata_update_path = temp_file_created ? path_temp : path_dst;
				xmp_result = metadata_xmp::update(metadata_update_path, path_src, metadata_edits, xmp_name);
			}
		}

		if (result.success() && temp_file_created)
		{
			result = platform::replace_file(path_dst, path_temp, create_original);

			if (!xmp_result.xmp_path.is_empty())
			{
				const auto path_dst_xmp = xmp_name.empty()
					                          ? path_dst.extension(u8".xmp"sv)
					                          : path_dst.folder().combine_file(xmp_result.xmp_path.name());
				const auto path_temp_xmp = xmp_result.xmp_path;
				result = platform::replace_file(path_dst_xmp, path_temp_xmp, create_original);
			}
		}

		if (result.success() && !setting.update_modified)
		{
			platform::set_files_dates(path_dst, file_created, file_modified);
		}
	}
	catch (std::exception& e)
	{
		df::log(__FUNCTION__, e.what());

		result.code = platform::file_op_result_code::FAILED;
		result.error_message = str::utf8_cast(e.what());

		if (temp_file_created)
		{
			platform::delete_file(path_temp);
		}
	}

	return result;
}

std::vector<archive_item> files::list_archive(df::file_path zip_file_path)
{
	std::vector<archive_item> results;
	struct archive_entry* entry;
	int r;
	const auto a = archive_read_new();
	const auto ext = archive_write_disk_new();

	archive_read_support_filter_all(a);
	archive_read_support_format_all(a);

	const auto w = platform::to_file_system_path(zip_file_path);

	if ((r = archive_read_open_filename_w(a, w.c_str(), 10240)) == ARCHIVE_OK)
	{
		for (;;) {
			r = archive_read_next_header(a, &entry);
			if (r == ARCHIVE_OK)
			{
				archive_item result_info;
				result_info.filename = str::utf8_cast(archive_entry_pathname_utf8(entry));
				result_info.uncompressed_size = df::file_size(archive_entry_size(entry));
				//result_info.compressed_size = df::file_size(archive_filter_bytes(a, -1));
				result_info.created = df::date_t(archive_entry_ctime(entry));
				results.emplace_back(result_info);
			}
			else
			{
				break;
			}

			//archive_read_data_skip(a);
		}
		archive_read_close(a);
		archive_read_free(a);
	}

	return results;
}

void file_scan_result::parse_metadata_ffmpeg_kv(prop::item_metadata& result) const
{
	for (const auto& kv : ffmpeg_metadata)
	{
		if (is_key(kv.first, u8"album"sv)) result.album = str::strip_and_cache(kv.second);
		else if (is_key(kv.first, u8"show"sv)) result.show = str::strip_and_cache(kv.second);
		else if (is_key(kv.first, u8"programme"sv)) result.show = str::strip_and_cache(kv.second);
		else if (is_key(kv.first, u8"album_artist"sv)) result.album_artist = str::strip_and_cache(kv.second);
		else if (is_key(kv.first, u8"artist"sv)) result.artist = str::strip_and_cache(kv.second);
		else if (is_key(kv.first, u8"comment"sv)) result.comment = str::strip_and_cache(kv.second);
		else if (is_key(kv.first, u8"description"sv)) result.description = str::strip_and_cache(kv.second);
		else if (is_key(kv.first, u8"composer"sv)) result.composer = str::strip_and_cache(kv.second);
		else if (is_key(kv.first, u8"copyright"sv)) result.copyright_notice = str::strip_and_cache(kv.second);
		else if (is_key(kv.first, u8"creation_time"sv) || is_key(kv.first, u8"date"sv) || is_key(
			kv.first, u8"com.apple.quicktime.creationdate"sv))
		{
			if (kv.second.size() == 4 || str::ends(kv.second, u8"00-00"sv))
			{
				const auto year = str::to_int(kv.second);

				if (year > 1800 && year < 2100)
				{
					result.year = year;
				}
			}
			else
			{
				const auto date = df::date_t::from(kv.second);

				if (date.is_valid())
				{
					if (!result.created_digitized.is_valid())
					{
						result.created_digitized = date;
					}

					result.created_utc = date;
					result.year = date.year();
				}
			}
		}
		else if (is_key(kv.first, u8"date-eng"sv) || is_key(kv.first, u8"Rip date"sv))
		{
			const auto date = df::date_t::from(kv.second);
			if (date.is_valid())
			{
				result.created_digitized = date;
			}
		}
		else if (is_key(kv.first, u8"id3v2_priv.Windows Media Player 9 Series"sv))
		{
			if (kv.second.size() >= 4)
			{
				const auto hex_part = kv.second.substr(2, 4);
				const auto rating = str::hex_to_num(hex_part);

				if (rating)
				{
					result.rating = 1 + (rating / 52);
				}
			}
		}
		else if (is_key(kv.first, u8"encoder"sv) || is_key(kv.first, u8"encoded_by"sv))
			result.encoder =
				str::strip_and_cache(kv.second);
		else if (is_key(kv.first, u8"genre"sv)) result.genre = str::strip_and_cache(kv.second);
		else if (is_key(kv.first, u8"publisher"sv)) result.publisher = str::strip_and_cache(kv.second);
		else if (is_key(kv.first, u8"synopsis"sv)) result.synopsis = str::strip_and_cache(kv.second);
		else if (is_key(kv.first, u8"title"sv)) result.title = str::strip_and_cache(kv.second);
		else if (is_key(kv.first, u8"maker"sv) || is_key(kv.first, u8"com.apple.quicktime.make"sv))
		{
			result.camera_manufacturer = str::strip_and_cache(kv.second);
		}
		else if (is_key(kv.first, u8"model"sv) || is_key(kv.first, u8"com.apple.quicktime.model"sv) || is_key(
			kv.first, u8"model-eng"sv))
		{
			result.camera_model = str::strip_and_cache(kv.second);
		}
		else if (is_key(kv.first, u8"performer"sv)) result.performer = str::strip_and_cache(kv.second);
		else if (is_key(kv.first, u8"year"sv)) result.year = str::to_int(kv.second);
		else if (is_key(kv.first, u8"disk"sv) || is_key(kv.first, u8"disc"sv)) result.disk = df::xy8::parse(kv.second);
		else if (is_key(kv.first, u8"track"sv)) result.track = df::xy8::parse(kv.second);
		else if (is_key(kv.first, u8"variant_bitrate"sv)) result.bitrate = str::strip_and_cache(kv.second);
		else if (is_key(kv.first, u8"episode_sort"sv)) result.episode, df::xy32::parse(kv.second);
		else if (is_key(kv.first, u8"season_number"sv)) result.season = str::to_int(kv.second);
		else if (is_key(kv.first, u8"system"sv)) result.system = str::strip_and_cache(kv.second);
		else if (is_key(kv.first, u8"game"sv)) result.game = str::strip_and_cache(kv.second);
		else if (is_key(kv.first, u8"song"sv) && prop::is_null(result.title))
			result.title =
				str::strip_and_cache(kv.second);
		else if (is_key(kv.first, u8"compatible_brands"sv) || is_key(kv.first, u8"minor_version"sv))
		{
			// compatible_brands: 3gp4, avc1isom, isomavc1, isomiso2avc1mp41, isomiso2mp41, isommp42, M4A mp42isom, mp41isom, mp42mp41isomavc1, qt
			// minor_version: 3gp4, avc1isom, isomavc1, isomiso2avc1mp41, isomiso2mp41, isommp42, M4A mp42isom, mp41isom, mp42mp41isomavc1, qt
		}
		else if (is_key(kv.first, u8"rating"sv))
		{
			result.rating = str::to_int(kv.second);
		}
		else if (is_key(kv.first, u8"keywords"sv))
		{
			str::split2(kv.second, true, [this](const std::u8string_view text)
			{
				keywords.emplace_back(str::cache(text));
			});
		}
		else if (is_key(kv.first, u8"location-eng"sv) || is_key(kv.first, u8"location"sv) || is_key(
			kv.first, u8"com.apple.quicktime.location.ISO6709"sv))
		{
			const auto loc = split_location(kv.second);

			if (loc.success)
			{
				gps = gps_coordinate(loc.x, loc.y);
			}
		}
		else
		{
			//df::log(__FUNCTION__, str::format(u8"Unknown tag: {} = {}"sv, kv.first, kv.second));
		}
	}
}

metadata_parts file_scan_result::save_metadata() const
{
	if (metadata.exif.empty() &&
		metadata.iptc.empty() &&
		metadata.xmp.empty())
	{
		metadata_parts result;
		result.exif = metadata_exif::make_exif(to_props());
		return result;
	}

	return metadata;
}

prop::item_metadata_ptr file_scan_result::to_props() const
{
	auto result = std::make_shared<prop::item_metadata>();

	if (!metadata.exif.empty())
	{
		metadata_exif::parse(*result, metadata.exif);
	}

	if (!metadata.iptc.empty())
	{
		metadata_iptc::parse(*result, metadata.iptc);
	}

	/*if (!moov.is_empty())
	{
		parse_metadata_moov(*result);
	}

	if (!id3v2.is_empty())
	{
		id3v2_metadata_id3v2(*result);
	}

	if (!id3v1.is_empty())
	{
		parse_metadata_id3v1(*result);
	}*/

	if (!ffmpeg_metadata.empty())
	{
		parse_metadata_ffmpeg_kv(*result);
	}

	if (!metadata.xmp.empty())
	{
		metadata_xmp::parse(*result, metadata.xmp);
	}

	if (prop::is_null(result->created_utc)) result->created_utc = created_utc;
	if (prop::is_null(result->iso_speed)) result->iso_speed = iso_speed;
	if (prop::is_null(result->exposure_time)) result->exposure_time = exposure_time;
	if (prop::is_null(result->f_number)) result->f_number = f_number;
	if (prop::is_null(result->focal_length)) result->focal_length = focal_length;
	if (prop::is_null(result->comment)) result->comment = comment;
	if (prop::is_null(result->artist)) result->artist = artist;
	if (prop::is_null(result->camera_model)) result->camera_model = camera_model;
	if (prop::is_null(result->camera_manufacturer)) result->camera_manufacturer = camera_manufacturer;
	if (prop::is_null(result->video_codec)) result->video_codec = video_codec;
	if (prop::is_null(result->copyright_notice)) result->copyright_notice = copyright_notice;
	if (prop::is_null(result->title)) result->title = title;
	if (prop::is_null(result->duration)) result->duration = df::round(duration);
	if (prop::is_null(result->audio_codec)) result->audio_codec = audio_codec;
	if (prop::is_null(result->audio_channels)) result->audio_channels = audio_channels;
	if (prop::is_null(result->audio_sample_type)) result->audio_sample_type = static_cast<uint16_t>(audio_sample_type);
	if (prop::is_null(result->audio_sample_rate)) result->audio_sample_rate = audio_sample_rate;

	if (!result->coordinate.is_valid())
	{
		result->coordinate = gps;
	}

	if (!result->tags.is_empty())
	{
		auto tags = split(result->tags, true);
		tags.insert(tags.end(), keywords.begin(), keywords.end());
		std::ranges::sort(tags, str::iless());
		tags.erase(std::ranges::unique(tags, df::ieq()).begin(), tags.end());
		result->tags = str::cache(str::combine(tags));
	}

	if (!prop::is_null(width)) result->width = width;
	if (!prop::is_null(height)) result->height = height;
	if (!prop::is_null(pixel_format)) result->pixel_format = pixel_format;
	if (orientation != ui::orientation::top_left) result->orientation = orientation;

	return result;
}

av_media_info file_scan_result::to_info() const
{
	av_media_info result;

	if (!ffmpeg_metadata.empty())
	{
		result.metadata.emplace_back(metadata_standard::media, ffmpeg_metadata);
	}

	if (!libraw_metadata.empty())
	{
		result.metadata.emplace_back(metadata_standard::raw, libraw_metadata);
	}

	if (!metadata.exif.empty())
	{
		const auto kv = metadata_exif::to_info(metadata.exif);
		result.metadata.emplace_back(metadata_standard::exif, kv);
	}

	if (!metadata.iptc.empty())
	{
		const auto kv = metadata_iptc::to_info(metadata.iptc);
		result.metadata.emplace_back(metadata_standard::iptc, kv);
	}

	if (!metadata.xmp.empty())
	{
		const auto kv = metadata_xmp::to_info(metadata.xmp);

		if (kv.empty())
		{
			std::u8string text;
			text.assign(std::bit_cast<const char8_t*>(metadata.xmp.data()), metadata.xmp.size());
			metadata_kv_list text_kv;
			text_kv.emplace_back(u8""_c, text);
			result.metadata.emplace_back(metadata_standard::xmp, text_kv);
		}
		else
		{
			result.metadata.emplace_back(metadata_standard::xmp, kv);
		}
	}

	if (!metadata.icc.empty())
	{
		const auto kv = metadata_icc::to_info(metadata.icc);
		result.metadata.emplace_back(metadata_standard::icc, kv);
	}

	return result;
}
