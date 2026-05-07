// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Core file handling operations. Manages file loading, saving, and format detection
// for images and media files. Coordinates metadata extraction across different file types.

#include "pch.h"

#include "app_text.h"
#include "files.h"
#include "metadata_exif.h"
#include "metadata_xmp.h"
#include "av_format.h"
#include "metadata_icc.h"
#include "metadata_iptc.h"

#define LIBARCHIVE_STATIC
#include "libarchive/libarchive/archive.h"
#include "libarchive/libarchive/archive_entry.h"

#undef GetObject
#undef min


static_assert(std::is_move_constructible_v<file_scan_result>);
static_assert(std::is_move_assignable_v<file_scan_result>);
static_assert(std::is_move_assignable_v<file_load_result>);


std::string file_group::display_name(const bool is_plural) const
{
	const auto result = is_plural && !str::is_empty(plural_name) ? plural_name : name;
	return tt.translate_text(std::string(result));
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

bool file_tool::invoke(const df::file_path path) const
{
	auto substitute = [&](std::ostringstream& result, const std::string_view token)
	{
		if (token == "item-path") result << str::quote_if_white_space(path.str());
		else if (token == "exe-path") result << str::quote_if_white_space(exe_path.str());
	};

	return platform::run(replace_tokens(invoke_text, substitute));
}

file_group_ref file_group_from_index(const int from_id)
{
	return s_config.groups[from_id];
}

file_group_ref parse_file_group(const std::string& text)
{
	const auto found = s_config.groups_by_name.find(text);

	if (found != s_config.groups_by_name.end())
	{
		return found->second;
	}

	return nullptr;
}

static constexpr file_traits photo_traits = file_traits::bitmap | file_traits::cache_metadata |
	file_traits::zoom | file_traits::hide_overlays |
	file_traits::thumbnail | file_traits::photo_metadata;
static constexpr file_traits video_traits = file_traits::av | file_traits::preview_video |
	file_traits::cache_metadata | file_traits::hide_overlays |
	file_traits::thumbnail | file_traits::video_metadata;
static constexpr file_traits audio_traits = file_traits::av | file_traits::visualize_audio |
	file_traits::cache_metadata | file_traits::music_metadata;

static constexpr file_traits commodore_traits = file_traits::commodore | file_traits::no_metadata_grouping;
static constexpr file_traits archive_traits = file_traits::archive | file_traits::no_metadata_grouping;

file_group file_group::other("other", "others", 0x5E5E5E, icon_index::document,
                             file_traits::no_metadata_grouping, group_key_type::other, {});
file_group file_group::folder("folder", "folders", 0x18A59C, icon_index::folder,
                              file_traits::no_metadata_grouping, group_key_type::folder, {});
file_group file_group::photo("photo", "photos", 0x18A549, icon_index::photo, photo_traits,
                             group_key_type::photo, {"xmp"});
file_group file_group::video("video", "videos", 0xA55018, icon_index::video, video_traits,
                             group_key_type::video,
                             {"srt", "smi", "vtt", "mpl2", "thm", "xmp"});
file_group file_group::audio("audio", {}, 0xA5184B, icon_index::audio, audio_traits, group_key_type::audio, {});

file_group file_group::archive("archive", "archives", 0x5588DD, icon_index::archive, archive_traits,
                               group_key_type::archive, {});
file_group file_group::commodore("commodore", {}, 0xFF8811, icon_index::retro, commodore_traits,
                                 group_key_type::retro, {});


file_type file_type::folder(file_group::folder, {}, {}, {});
file_type file_type::other(file_group::other, {}, {}, {});

void load_file_types()
{
	s_config.groups = {
		file_group::folder,
		file_group::photo,
		file_group::video,
		file_group::audio,
		file_group::archive,
		file_group::commodore,
		file_group::other
	};

	s_config.types = {
		{file_group::video, "264", {}, {}},
		{file_group::video, "265", {}, {}},
		{file_group::video, "302", {}, {}},
		{file_group::video, "3fr", "Hasselblad Raw", file_traits::raw | file_traits::edit},
		{file_group::audio, "669", {}, {}},
		{file_group::video, "722", {}, {}},
		{file_group::video, "A64", {}, {}},
		{file_group::video, "aa", {}, {}},
		{file_group::video, "aa3", {}, {}},
		{file_group::audio, "aac,adt,adts", "Advanced Audio Coding", {}},
		{file_group::video, "ac3", {}, {}},
		{file_group::video, "acm", {}, {}},
		{file_group::video, "adf", {}, {}},
		{file_group::video, "adp", {}, {}},
		{file_group::video, "ads", {}, {}},
		{file_group::video, "adx", {}, {}},
		{file_group::video, "aea", {}, {}},
		{file_group::audio, "afc", {}, {}},
		{file_group::audio, "aif, aifc, aiff", "Audio Interchange File Format", file_traits::embedded_xmp},
		{file_group::audio, "au,snd", "Sun Microsystems and NeXT audio", {}},
		{file_group::video, "aix", {}, {}},
		{file_group::audio, "amf", {}, {}},
		{file_group::video, "amr", "GSM and UMTS mobile phone video", {}},
		{file_group::audio, "ams", {}, {}},
		{file_group::audio, "ape", "Monkey's Audio (APE)", {}},
		{file_group::video, "apl", {}, {}},
		{file_group::video, "apng", {}, {}},
		{file_group::video, "aptx", {}, {}},
		{file_group::video, "aptxhd", {}, {}},
		{file_group::video, "aqt", {}, {}},
		{file_group::photo, "arw,sr2,srf", "Sony Raw", file_traits::raw | file_traits::edit},
		{file_group::photo, "bay", "Casio Raw", file_traits::raw | file_traits::edit},
		{file_group::photo, "bmq", "NuCore Raw", file_traits::raw | file_traits::edit},
		{file_group::photo, "cap", "Phase One Raw", file_traits::raw | file_traits::edit},
		{file_group::photo, "cine", "Phantom Raw", file_traits::raw | file_traits::edit},
		{file_group::video, "asf", "Windows Media", file_traits::embedded_xmp | file_traits::edit},
		{file_group::video, "ass", {}, {}},
		{file_group::video, "ast", "Audio Stream", {}},
		{
			file_group::video, "avi", "Audio Visual Interleave",
			file_traits::embedded_xmp | file_traits::edit
		},
		{file_group::video, "avr", {}, {}},
		{file_group::video, "avs", {}, {}},
		{file_group::video, "avs2", {}, {}},
		{file_group::video, "bcstm", {}, {}},
		{file_group::video, "bfstm", {}, {}},
		{file_group::video, "bit", {}, {}},
		{file_group::photo, "bmp", "Microsoft Windows Bitmap", {}},
		{file_group::video, "bmv", {}, {}},
		{file_group::video, "brstm", "Binary Revolution Stream", {}},
		{file_group::video, "c2", {}, {}},
		{file_group::video, "caf", {}, {}},
		{file_group::video, "cavs", {}, {}},
		{file_group::video, "cdata", {}, {}},
		{file_group::video, "cdg", {}, {}},
		{file_group::video, "cgi", {}, {}},
		{file_group::video, "chk", {}, {}},
		{file_group::video, "cif", {}, {}},
		{file_group::video, "cpk", {}, {}},
		{file_group::photo, "crw,cr2,cr3", "Canon raw", file_traits::raw | file_traits::edit},
		{file_group::photo, "cs1,sti,ia", "Sinar raw", file_traits::raw | file_traits::edit},
		{file_group::video, "cdxl", "Commodore CDXL video", {}},
		{file_group::video, "daud", {}, {}},
		{file_group::video, "dav", {}, {}},
		{file_group::audio, "dbm", {}, {}},
		{file_group::audio, "cda", "CD Audio Track", {}},
		{file_group::photo, "dc2,dcr,drf,dsc,k25,kc2,kdc", "Kodak raw", {}},
		{file_group::audio, "dff", {}, {}},
		{file_group::audio, "digi", {}, {}},
		{file_group::video, "divx", {}, {}},
		{file_group::audio, "dmf", {}, {}},
		{
			file_group::photo, "dng", "Adobe Digital Negative",
			file_traits::raw | file_traits::edit | file_traits::embedded_xmp | file_traits::edit
		},
		{file_group::video, "dnxhd", {}, {}},
		{file_group::video, "dnxhr", {}, {}},
		{file_group::video, "drc", {}, {}},
		{file_group::audio, "dsf", {}, {}},
		{file_group::audio, "dsm", {}, {}},
		{file_group::video, "dss", {}, {}},
		{file_group::video, "dtk", {}, {}},
		{file_group::audio, "dtm", {}, {}},
		{file_group::video, "dts", "DTS (sound system)", {}},
		{file_group::video, "dtshd", "DTS (sound system)", {}},
		{file_group::video, "dv", {}, {}},
		{file_group::video, "dvd", {}, {}},
		{file_group::video, "eac3", {}, {}},
		{file_group::photo, "erf", "Epson Raw", file_traits::raw | file_traits::edit},
		{file_group::photo, "mfw", "Mamiya raw", file_traits::raw | file_traits::edit},
		{file_group::photo, "raw", "Panasonic raw", file_traits::raw | file_traits::edit},
		{file_group::photo, "exr", {}, {}},
		{file_group::video, "fap", {}, {}},
		{file_group::audio, "far", {}, {}},
		{file_group::photo, "fff", "Imacon raw", file_traits::raw | file_traits::edit},
		{file_group::video, "fits", {}, {}},
		{file_group::audio, "flac", "Free Lossless Audio", {}},
		{file_group::video, "flm", {}, {}},
		{file_group::video, "flv", "Flash video", file_traits::embedded_xmp | file_traits::edit},
		{file_group::video, "fsb", {}, {}},
		{file_group::video, "fwse", {}, {}},
		{file_group::video, "g722", {}, {}},
		{file_group::video, "g723_1", {}, {}},
		{file_group::video, "g729", {}, {}},
		{file_group::audio, "gdm", {}, {}},
		{file_group::video, "genh", {}, {}},
		{
			file_group::photo, "gif,giff", "CompuServe's Graphics Interchange Format",
			file_traits::embedded_xmp | file_traits::edit
		},
		{file_group::video, "gsm", "GSM Full Rate", {}},
		{file_group::video, "gxf", {}, {}},
		{file_group::video, "h261", {}, {}},
		{file_group::video, "h263", {}, {}},
		{file_group::video, "h264", {}, {}},
		{file_group::video, "h265", {}, {}},
		{file_group::video, "hca", {}, {}},
		{file_group::photo, "hdr", {}, {}},
		{file_group::video, "hevc", {}, {}},
		{file_group::audio, "ice", {}, {}},
		{file_group::photo, "ico", "Microsoft Windows icon", {}},
		{file_group::audio, "id3", {}, {}},
		{file_group::video, "idf", {}, {}},
		{file_group::video, "idx", {}, {}},
		{file_group::photo, "iff", "ILBM", {}},
		{file_group::video, "ifv", {}, {}},
		{file_group::photo, "iiq", "Phase One raw", file_traits::raw | file_traits::edit},
		{file_group::audio, "imf", {}, {}},
		{file_group::video, "ircam", {}, {}},
		{file_group::video, "ism", {}, {}},
		{file_group::video, "isma", {}, {}},
		{file_group::video, "ismv", {}, {}},
		{file_group::audio, "it", {}, {}},
		{file_group::video, "ivf", "Indeo Video Technology", {}},
		{file_group::video, "ivr", {}, {}},
		{file_group::audio, "j2b", {}, {}},
		{file_group::photo, "j2c", {}, {}},
		{file_group::photo, "j2k", {}, {}},
		{file_group::photo, "jif", {}, {}},
		{file_group::video, "jls", {}, {}},
		{file_group::photo, "jp2", "JPEG2000", {}},
		{
			file_group::photo, "jpeg,jpg,jpe,jfif", "Joint Photographic Experts Group",
			file_traits::embedded_xmp | file_traits::edit
		},
		{file_group::photo, "jpx", "Jpeg 2000", file_traits::embedded_xmp | file_traits::edit},
		{file_group::video, "jss", {}, {}},
		{file_group::photo, "koa", {}, {}},
		{file_group::video, "kux", {}, {}},
		{file_group::video, "latm", {}, {}},
		{file_group::video, "lbc", {}, {}},
		{file_group::photo, "lbm", "ILBM", {}},
		{file_group::video, "ljpg", {}, {}},
		{file_group::video, "loas", {}, {}},
		{file_group::video, "lrc", {}, {}},
		{file_group::video, "lrv", {}, {}},
		{file_group::video, "lvf", {}, {}},
		{file_group::audio, "m15", {}, {}},
		{file_group::audio, "mid,midi,rmi", "Musical Instrument Digital Interface", {}},
		{file_group::video, "m2p", {}, {}},
		{file_group::video, "m2t", {}, {}},
		{file_group::video, "m2ts", "MPEG-2 TS video", {}},
		{file_group::video, "mac", {}, {}},
		{file_group::photo, "mdc,mrw", "Minolta raw", file_traits::raw | file_traits::edit},
		{file_group::audio, "mdl", {}, {}},
		{file_group::audio, "med", {}, {}},
		{file_group::photo, "mef", "Mamiya raw", file_traits::raw | file_traits::edit},
		{file_group::video, "mj2", {}, {}},
		{file_group::video, "mjpeg", {}, {}},
		{file_group::video, "mjpg", {}, {}},
		{file_group::video, "mk3d", {}, {}},
		{file_group::audio, "mka", {}, {}},
		{file_group::video, "mks", {}, {}},
		{file_group::video, "mkv", "Matroska video", {}},
		{file_group::video, "mlp", {}, {}},
		{file_group::audio, "mmcmp", {}, {}},
		{file_group::video, "mmf", {}, {}},
		{file_group::audio, "mms", {}, {}},
		{file_group::photo, "mng", "Multiple Network Graphics animation", {}},
		{file_group::audio, "mo3", {}, {}},
		{file_group::audio, "mod", {}, {}},
		{file_group::photo, "mos", "Leaf raw", file_traits::raw | file_traits::edit},
		{
			file_group::audio, "mp3", "MPEG Layer 3",
			file_traits::embedded_xmp | file_traits::edit | file_traits::thumbnail
		},
		{
			file_group::video, "mov", {},
			file_traits::embedded_xmp | file_traits::edit
		},
		{
			file_group::video, "mp4,mp4a,mp4v,m4v,m4b,f4v,3g2,3gp2,3gp,3gpp,crm", "MPEG-4",
			file_traits::embedded_xmp | file_traits::edit
		},
		{
			file_group::video, "crm", "Canon Cinema RAW Light",
			file_traits::embedded_xmp | file_traits::edit
		},
		{
			file_group::audio, "m4a,mp4a,m4r", "MPEG-4 Audio",
			file_traits::embedded_xmp | file_traits::edit | file_traits::thumbnail
		},
		{
			file_group::audio, "m4p", "MPEG-4 (DRM)",
			file_traits::embedded_xmp | file_traits::edit | file_traits::thumbnail
		},
		{file_group::audio, "mpc", "Musepack", {}},
		{file_group::video, "mpd", {}, {}},
		{
			file_group::video, "mpeg,mpg,mpe,m1v,m2v,mp2,mpv,m2p,m2t,mpe,vob,ms-pvr,dvr-ms", "MPEG",
			file_traits::embedded_xmp | file_traits::edit
		},
		{file_group::audio, "mpa,m2a", "MPEG", file_traits::embedded_xmp | file_traits::edit},
		{
			file_group::photo, "heif, heifs, heic, heics, avci, avcs, avif, avifs",
			"High Efficiency Image File Format", file_traits::embedded_xmp
		},
		{file_group::video, "avc1", "Advanced Video Coding", {}},
		{file_group::audio, "mptm", {}, {}},
		{file_group::video, "msbc", {}, {}},
		{file_group::video, "msf", {}, {}},
		{file_group::audio, "mt2", {}, {}},
		{file_group::video, "mtaf", {}, {}},
		{file_group::audio, "mtm", {}, {}},
		{file_group::video, "mts", {}, {}},
		{file_group::video, "musx", {}, {}},
		{file_group::video, "mvi", {}, {}},
		{file_group::video, "mxf", "SMPTE Material Exchange Format", {}},
		{file_group::video, "mxg", {}, {}},
		{file_group::photo, "nef,nrw", "Nikon  raw", file_traits::raw | file_traits::edit},
		{file_group::video, "nist", {}, {}},
		{file_group::photo, "nrw", {}, {}},
		{file_group::video, "nsp", {}, {}},
		{file_group::audio, "nst", {}, {}},
		{file_group::video, "nut", {}, {}},
		{file_group::video, "obu", {}, {}},
		{file_group::audio, "oga", {}, {}},
		{file_group::audio, "ogg", "container, multimedia", {}},
		{file_group::video, "ogm", {}, {}},
		{file_group::video, "ogv", {}, {}},
		{file_group::video, "ogx", {}, {}},
		{file_group::audio, "okt", {}, {}},
		{file_group::video, "oma", {}, {}},
		{file_group::video, "omg", {}, {}},
		{file_group::audio, "opus", {}, {}},
		{file_group::photo, "orf", "Olympus raw", file_traits::raw | file_traits::edit},
		{file_group::video, "paf", {}, {}},
		{file_group::video, "pam", {}, {}},
		{file_group::photo, "pbm", "Portable bitmap", {}},
		{file_group::photo, "pcd", {}, {}},
		{file_group::photo, "pct", "Apple Macintosh PICT image", {}},
		{file_group::photo, "pcx", "ZSoft's PC Paint image", {}},
		{file_group::photo, "pef,ptx", "Pentax raw", file_traits::raw | file_traits::edit},
		{file_group::photo, "pfm", {}, {}},
		{file_group::photo, "pgm", "Portable graymap", {}},
		{file_group::video, "pgmyuv", {}, {}},
		{file_group::photo, "pic", {}, {}},
		{file_group::photo, "pict", "Apple Macintosh PICT image", {}},
		{file_group::video, "pjs", {}, {}},
		{file_group::audio, "plm", {}, {}},
		{
			file_group::photo, "png", "Portable Network Graphic",
			file_traits::embedded_xmp | file_traits::edit
		},
		{file_group::photo, "ppm", "Portable Pixmap", {}},
		{
			file_group::photo, "psd", "Adobe Photoshop Drawing",
			file_traits::embedded_xmp | file_traits::edit
		},
		{file_group::audio, "psm", {}, {}},
		{file_group::video, "psp", "Paint Shop Pro image", {}},
		{file_group::audio, "pt36", {}, {}},
		{file_group::audio, "ptm", {}, {}},
		{file_group::video, "pvf", {}, {}},
		{file_group::photo, "pxn", "Logitech raw", file_traits::raw | file_traits::edit},
		{file_group::video, "qcif", {}, {}},
		{file_group::photo, "qtk", "Apple Quicktake raw", file_traits::raw | file_traits::edit},
		{file_group::audio, "ra, rm", "RealAudio", {}},
		{file_group::photo, "raf", "Fuji raw", file_traits::raw | file_traits::edit},
		{file_group::photo, "ras", {}, {}},
		{file_group::video, "rco", {}, {}},
		{file_group::video, "rcv", {}, {}},
		{file_group::photo, "rdc", "Digital Foto Maker raw", file_traits::raw | file_traits::edit},
		{file_group::video, "rmd,r3d", "RED", {}},
		{file_group::video, "rgb", "Silicon Graphics Image", {}},
		{file_group::video, "rm", "RealAudio (RA, RM)", {}},
		{file_group::video, "roq", "Quake 3 video", {}},
		{file_group::video, "rsd", {}, {}},
		{file_group::video, "rso", {}, {}},
		{file_group::video, "rt", {}, {}},
		{file_group::photo, "rw", {}, file_traits::raw | file_traits::edit},
		{file_group::photo, "rw2", "Panasonic raw", file_traits::raw | file_traits::edit},
		{file_group::photo, "rwl", "Leica raw", file_traits::raw | file_traits::edit},
		{file_group::photo, "rwz", "Rawzor raw", file_traits::raw | file_traits::edit},
		{file_group::audio, "s3m", {}, {}},
		{file_group::video, "sami", {}, {}},
		{file_group::video, "sbc", {}, {}},
		{file_group::video, "sbg", {}, {}},
		{file_group::video, "scc", {}, {}},
		{file_group::video, "sdr2", {}, {}},
		{file_group::video, "sds", {}, {}},
		{file_group::video, "sdx", {}, {}},
		{file_group::video, "ser", {}, {}},
		{file_group::video, "sf", {}, {}},
		{file_group::audio, "sfx", {}, {}},
		{file_group::audio, "sfx2", {}, {}},
		{file_group::photo, "sgi", "Silicon Graphics Image", {}},
		{file_group::video, "shn", "Shorten (SHN)", {}},
		{file_group::video, "son", {}, {}},
		{file_group::video, "sox", {}, {}},
		{file_group::video, "spdif", {}, {}},
		{file_group::video, "sph", {}, {}},
		{file_group::audio, "spx", "Speex low bitrate audio", {}},
		{file_group::photo, "srw", "Samsung raw", file_traits::raw | file_traits::edit},
		{file_group::video, "ss2", {}, {}},
		{file_group::video, "ssa", {}, {}},
		{file_group::audio, "st26", {}, {}},
		{file_group::audio, "stk", {}, {}},
		{file_group::video, "stl", {}, {}},
		{file_group::audio, "stm", {}, {}},
		{file_group::audio, "stp", {}, {}},
		{file_group::video, "str", {}, {}},
		{file_group::video, "sub", {}, {}},
		{file_group::video, "sup", {}, {}},
		{file_group::video, "ag", {}, {}},
		{file_group::video, "swf", "Macromedia Flash", file_traits::embedded_xmp},
		{file_group::video, "tak", "Tom's Lossless Audio Kompressor", {}},
		{file_group::photo, "tga,targa", "Truevision TGA (Targa) image", {}},
		{file_group::video, "tco", {}, {}},
		{file_group::video, "thd", {}, {}},
		{
			file_group::photo, "tiff, tif", "Tagged Image File Format",
			file_traits::embedded_xmp | file_traits::edit
		},
		{file_group::photo, "cin", "Kodak Cineon Image", {}},
		{file_group::photo, "dpx", "Digital Picture Exchange", {}},
		{file_group::video, "tod", {}, {}},
		{file_group::video, "ts", {}, {}},
		{file_group::audio, "tta", "True Audio", {}},
		{file_group::video, "ty", {}, {}},
		{file_group::video, "ty+", {}, {}},
		{file_group::audio, "ult", {}, {}},
		{file_group::audio, "umx", {}, {}},
		{file_group::video, "v210", {}, {}},
		{file_group::video, "vag", {}, {}},
		{file_group::video, "vb", {}, {}},
		{file_group::video, "vc1", {}, {}},
		{file_group::video, "vc2", {}, {}},
		{file_group::video, "viv", {}, {}},
		{file_group::video, "voc", "Creative Labs Soundblaster", {}},
		{file_group::video, "vpk", {}, {}},
		{file_group::video, "vqe", {}, {}},
		{file_group::video, "vqf", "Yamaha TwinVQ", {}},
		{file_group::video, "vql", {}, {}},
		{file_group::video, "w64", {}, {}},
		{file_group::photo, "wap", {}, {}},
		{file_group::audio, "wav", "Microsoft Wave", file_traits::embedded_xmp | file_traits::edit},
		{file_group::photo, "wbm", {}, {}},
		{file_group::photo, "wbmp", {}, file_traits::embedded_xmp | file_traits::edit},
		{file_group::video, "web", {}, {}},
		{file_group::video, "webm", {}, {}},
		{file_group::photo, "webp", {}, file_traits::embedded_xmp | file_traits::edit},
		{
			file_group::audio, "wma", "Windows Media Audio 9",
			file_traits::embedded_xmp | file_traits::edit
		},
		{
			file_group::video, "wmv,wm", "Windows Media video",
			file_traits::embedded_xmp | file_traits::edit
		},
		{file_group::audio, "wow", {}, {}},
		{file_group::video, "wsd", {}, {}},
		{file_group::video, "wtv", {}, {}},
		{file_group::audio, "wv", "WavPack", {}},
		{file_group::photo, "x3f", "Sigma raw", file_traits::raw | file_traits::edit},
		{file_group::photo, "xbm", "X Window System Bitmap", {}},
		{file_group::video, "xl", {}, {}},
		{file_group::audio, "xm", {}, {}},
		{file_group::video, "xmv", {}, {}},
		{file_group::audio, "xpk", {}, {}},
		{file_group::photo, "xpm", "X Window System Pixmap", {}},
		{file_group::video, "xvag", {}, {}},
		{file_group::audio, "xx", {}, {}},
		{file_group::video, "y4m", {}, {}},
		{file_group::video, "yop", {}, {}},
		{file_group::video, "yuv", "Raw YUV video format", {}},
		{file_group::video, "yuv10", {}, {}},

		{file_group::archive, "zip", {}, {file_traits::archive}},
		{file_group::archive, "rar", {}, {file_traits::archive}},
		{file_group::archive, "7z", {}, {file_traits::archive}},
		{file_group::archive, "gz", {}, {file_traits::archive}},
		{file_group::archive, "tgz", {}, {file_traits::archive}},
		{file_group::archive, "cpio", {}, {file_traits::archive}},
		{file_group::archive, "iso", {}, {file_traits::archive}},
		{file_group::archive, "cab", {}, {file_traits::archive}},
		{file_group::archive, "pax", {}, {file_traits::archive}},
		{file_group::archive, "lzip", {}, {file_traits::archive}},
		{file_group::archive, "lza", {}, {file_traits::archive}},
		{file_group::archive, "bzip2,bz2", {}, {file_traits::archive}},
		{file_group::archive, "tar", {}, {file_traits::archive}},
		{file_group::archive, "lha", {}, {file_traits::archive}},
		{file_group::archive, "a,ar", {}, {file_traits::archive}},

		{file_group::commodore, "d64", {}, {file_traits::disk_image | file_traits::commodore}},
		{file_group::commodore, "d81", {}, {file_traits::disk_image | file_traits::commodore}},
		{file_group::commodore, "t64", {}, {file_traits::commodore}},
		{file_group::commodore, "crt", {}, {file_traits::commodore}},
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
			str::split2(ft.extension, true, [pft = &ft](const std::string_view ext)
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
		const auto json = blob_from_file(df::probe_data_file("diffractor-tools.json"));

		if (!json.empty())
		{
			df::util::json::json_doc document;
			document.Parse(std::bit_cast<const char*>(json.data()), json.size());

			const auto& tools = document["tools"];

			if (tools.IsObject())
			{
				for (const auto& m : tools.GetObject())
				{
					if (str::icmp(m.name.GetString(), "folders") == 0)
					{
						if (m.value.IsArray())
						{
							for (const auto& folder : m.value.GetArray())
							{
								tool_paths.emplace_back(df::folder_path(folder.GetString()));
							}
						}
					}

					if (str::icmp(m.name.GetString(), "apps") == 0)
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
										if (str::icmp(a.name.GetString(), "exe") == 0)
											tool->exe = str::cache(
												a.value.GetString());
										if (str::icmp(a.name.GetString(), "invoke") == 0)
											tool->invoke_text =
												str::cache(a.value.GetString());
										if (str::icmp(a.name.GetString(), "text") == 0)
											tool->text = str::cache(
												a.value.GetString());
										if (str::icmp(a.name.GetString(), "extensions") == 0)
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

	df::hash_map<std::string, df::file_path, df::ihash, df::ieq> exe_by_name;

	for (const auto& folder : tool_paths)
	{
		const auto exe_selector = df::item_selector(folder, true, "*.exe");
		const auto files = platform::select_files(exe_selector, true);

		for (const auto& f : files)
		{
			auto path = f.folder.combine_file(f.name);
			exe_by_name[std::string(path.file_name_without_extension())] = path;
		}
	}

	for (const auto& tool : tools_by_name)
	{
		const auto found = exe_by_name.find(std::string(tool.second->exe));

		if (found != exe_by_name.end())
		{
			tool.second->exe_path = found->second;
		}
	}

	for (auto& ft : s_config.types)
	{
		if (!ft.extension.empty())
		{
			str::split2(ft.extension, true, [&ft, &tools_by_ext](const std::string_view ext)
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
	constexpr sizei scale_hint = {1024 * 4, 1024 * 4};
	const auto s1 = to_surface(scale_hint);
	const auto s2 = other.to_surface(scale_hint);

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

file_type_ref files::file_type_from_name(const std::string_view name)
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

bool files::can_save_extension(const std::string_view ext)
{
	static const df::hash_set<std::string_view, df::ihash, df::ieq> save_extensions = {
		{".jpg"},
		{".jpeg"},
		{".jpe"},
		{".png"},
		{".webp"},
		{"jpg"},
		{"jpeg"},
		{"jpe"},
		{"png"},
		{"webp"},
	};

	return save_extensions.contains(ext);
}

bool files::is_raw(const df::file_path path)
{
	const auto* const mt = file_type_from_name(path);
	return (mt->traits & file_traits::raw) != file_traits::none;
}

bool files::is_raw(const std::string_view name)
{
	const auto* const mt = file_type_from_name(name);
	return (mt->traits & file_traits::raw) != file_traits::none;
}

bool files::is_jpeg(const df::file_path path)
{
	const auto ext = path.extension();
	return str::icmp(ext, ".jpg") == 0 || str::icmp(ext, ".jpeg") == 0 || str::icmp(ext, ".jpe") == 0;
}

bool files::is_jpeg(const std::string_view name)
{
	const auto ext = name.substr(df::find_ext(name));
	return str::icmp(ext, ".jpg") == 0 || str::icmp(ext, ".jpeg") == 0 || str::icmp(ext, ".jpe") == 0;
}

static bool is_heif(const df::cspan image_buffer_in)
{
	if (image_buffer_in.size < 12u)
	{
		return false;
	}

	constexpr std::array<uint8_t, 4> ftyp_header = {'f', 't', 'y', 'p'};
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

	constexpr std::array<unsigned char, 4> ftyp_header = {'f', 't', 'y', 'p'};
	constexpr std::array<std::array<unsigned char, 4>, 2> brand = {
		{
			{'a', 'v', 'i', 'f'},
			{'a', 'v', 'i', 's'},
		}
	};

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
				_jpeg_encoder.encode(dimensions.cx, dimensions.cy, surface_in->pixels(),
				                     static_cast<uint32_t>(surface_in->stride()),
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

ui::pixel_difference_result files::pixel_difference(const ui::const_image_ptr& expected,
                                                    const ui::const_image_ptr& actual)
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
							success = _jpeg_decoder.read_rgb(temp_surface->pixels(),
							                                 static_cast<int>(temp_surface->stride()),
							                                 static_cast<int>(temp_surface->size()));
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

ui::surface_ptr files::image_to_surface(const df::cspan image_buffer_in, const sizei target_extent,
                                        const bool can_use_yuv)
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
							success = _jpeg_decoder.read_rgb(temp_surface->pixels(),
							                                 static_cast<int>(temp_surface->stride()),
							                                 static_cast<int>(temp_surface->size()));
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

static uint64_t round_up_to_multiple(const uint64_t n, const uint64_t multiple)
{
	if (multiple == 0)
		return n;

	const auto remainder = n % multiple;

	if (remainder == 0)
		return n;

	return n + multiple - remainder;
}

void file_read_stream::read(const uint64_t pos, uint8_t* buffer, const size_t len)
{
	load_buffer(pos, len);
	memcpy(buffer, _buffer + pos - _loaded_start_pos, len);
}

void file_read_stream::load_buffer(const uint64_t pos, const size_t len)
{
	const auto wanted_end_pos = pos + len;

	if (wanted_end_pos > _loaded_end_pos || pos < _loaded_start_pos)
	{
		if (wanted_end_pos > _file_size)
		{
			const auto message = std::format("invalid read past end of file: {}", _h->path());
			df::log(__FUNCTION__, message);
			throw app_exception(message);
		}

		const auto new_start_pos = pos > _block_size ? round_up_to_multiple(pos - _block_size, _block_size) : 0;
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
			const auto message = std::format("buffer alloc failed: {}", _h->path());
			df::log(__FUNCTION__, message);
			throw std::bad_alloc();
		}

		/*if (_buffer && new_start_pos != _loaded_start_pos)
		{
			std::memmove(_buffer, )
		}*/

		if (_h->seek(new_start_pos, platform::file::whence::begin) != new_start_pos)
		{
			const auto message = std::format("invalid load_buffer seek: {} {}", new_start_pos, _h->path());
			df::log(__FUNCTION__, message);
			throw app_exception(message);
		}

		const auto wanted = new_end_pos - new_start_pos;

		if (_h->read(_buffer, wanted) != wanted)
		{
			const auto message = std::format("invalid load_buffer read: {} {}", wanted, _h->path());
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

std::string normalize_string_trailing_null(std::string operand)
{
	if (!operand.empty() && operand.back() != '\0')
	{
		operand.append(1, '\0');
	}

	return operand;
}

file_scan_result files::scan_file(const df::file_path path, const bool load_thumb, const file_type_ref ft,
                                  const std::string_view xmp_sidecar, const sizei max_thumb_size)
{
	file_scan_result result;

	try
	{
		const auto f = open_file(path, platform::file_open_mode::read);

		if (f)
		{
			const auto file_len = f->size();
			const bool is_bitmap = ft->has_trait(file_traits::bitmap);
			const auto is_raw = ft->has_trait(file_traits::raw);
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
			else if (ft->has_trait(file_traits::av))
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

						if (!ft->has_trait(file_traits::thumbnail))
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


file_load_result files::load(const df::file_path path, const bool can_load_preview)
{
	df::last_loaded_path = path;

	file_load_result result;
	const auto* const mt = file_type_from_name(path);

	if (mt->has_trait(file_traits::bitmap))
	{
		if (mt->has_trait(file_traits::raw))
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
	else if (mt->has_trait(file_traits::av))
	{
		// Not supported
	}

	return result;
}

static void patch_file(const df::file_path path, const uint64_t offset, const uint8_t* data, const size_t len)
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

static simple_transform angle_to_transform(const int a)
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


ui::image_format extension_to_format(const std::string_view ext)
{
	static const df::hash_map<std::string_view, ui::image_format, df::ihash, df::ieq> extensions =
	{
		{".jpg", ui::image_format::JPEG},
		{".jpeg", ui::image_format::JPEG},
		{".jpe", ui::image_format::JPEG},
		{".png", ui::image_format::PNG},
		{".webp", ui::image_format::WEBP},
		{"jpg", ui::image_format::JPEG},
		{"jpeg", ui::image_format::JPEG},
		{"jpe", ui::image_format::JPEG},
		{"png", ui::image_format::PNG},
		{"webp", ui::image_format::WEBP},
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
                                       const std::string_view xmp_name)
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

		if (mt->has_trait(file_traits::bitmap))
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
			constexpr auto ten_megabytes = 10ull * 1024ull * 1024ull;

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
					                          ? path_dst.extension(".xmp")
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

std::vector<archive_item> files::list_archive(const df::file_path zip_file_path)
{
	std::vector<archive_item> results;
	archive_entry* entry;
	int r;
	const auto a = archive_read_new();

	archive_read_support_filter_all(a);
	archive_read_support_format_all(a);

	const auto w = platform::to_file_system_path(zip_file_path);

	if ((r = archive_read_open_filename_w(a, w.c_str(), 10240)) == ARCHIVE_OK)
	{
		for (;;)
		{
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
	}

	archive_read_free(a);

	return results;
}

void file_scan_result::parse_metadata_ffmpeg_kv(prop::item_metadata& result) const
{
	for (const auto& kv : ffmpeg_metadata)
	{
		if (is_key(kv.first, "album")) result.album = str::strip_and_cache(kv.second);
		else if (is_key(kv.first, "show")) result.show = str::strip_and_cache(kv.second);
		else if (is_key(kv.first, "programme")) result.show = str::strip_and_cache(kv.second);
		else if (is_key(kv.first, "album_artist")) result.album_artist = str::strip_and_cache(kv.second);
		else if (is_key(kv.first, "artist")) result.artist = str::strip_and_cache(kv.second);
		else if (is_key(kv.first, "comment")) result.comment = str::strip_and_cache(kv.second);
		else if (is_key(kv.first, "description")) result.description = str::strip_and_cache(kv.second);
		else if (is_key(kv.first, "composer")) result.composer = str::strip_and_cache(kv.second);
		else if (is_key(kv.first, "copyright")) result.copyright_notice = str::strip_and_cache(kv.second);
		else if (is_key(kv.first, "creation_time") || is_key(kv.first, "date") || is_key(
			kv.first, "com.apple.quicktime.creationdate"))
		{
			if (kv.second.size() == 4 || str::ends(kv.second, "00-00"))
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
		else if (is_key(kv.first, "date-eng") || is_key(kv.first, "Rip date"))
		{
			const auto date = df::date_t::from(kv.second);
			if (date.is_valid())
			{
				result.created_digitized = date;
			}
		}
		else if (is_key(kv.first, "id3v2_priv.Windows Media Player 9 Series"))
		{
			if (kv.second.size() >= 4)
			{
				const auto hex_part = kv.second.substr(2, 4);
				const auto rating = str::hex_to_num(hex_part);

				if (rating)
				{
					result.rating = 1 + rating / 52;
				}
			}
		}
		else if (is_key(kv.first, "encoder") || is_key(kv.first, "encoded_by"))
			result.encoder =
				str::strip_and_cache(kv.second);
		else if (is_key(kv.first, "genre")) result.genre = str::strip_and_cache(kv.second);
		else if (is_key(kv.first, "publisher")) result.publisher = str::strip_and_cache(kv.second);
		else if (is_key(kv.first, "synopsis")) result.synopsis = str::strip_and_cache(kv.second);
		else if (is_key(kv.first, "title")) result.title = str::strip_and_cache(kv.second);
		else if (is_key(kv.first, "maker") || is_key(kv.first, "com.apple.quicktime.make"))
		{
			result.camera_manufacturer = str::strip_and_cache(kv.second);
		}
		else if (is_key(kv.first, "model") || is_key(kv.first, "com.apple.quicktime.model") || is_key(
			kv.first, "model-eng"))
		{
			result.camera_model = str::strip_and_cache(kv.second);
		}
		else if (is_key(kv.first, "performer")) result.performer = str::strip_and_cache(kv.second);
		else if (is_key(kv.first, "year")) result.year = str::to_int(kv.second);
		else if (is_key(kv.first, "disk") || is_key(kv.first, "disc")) result.disk = df::xy8::parse(kv.second);
		else if (is_key(kv.first, "track")) result.track = df::xy8::parse(kv.second);
		else if (is_key(kv.first, "variant_bitrate")) result.bitrate = str::strip_and_cache(kv.second);
		else if (is_key(kv.first, "episode_sort")) result.episode = df::xy8::parse(kv.second);
		else if (is_key(kv.first, "season_number")) result.season = str::to_int(kv.second);
		else if (is_key(kv.first, "system")) result.system = str::strip_and_cache(kv.second);
		else if (is_key(kv.first, "game")) result.game = str::strip_and_cache(kv.second);
		else if (is_key(kv.first, "song") && prop::is_null(result.title))
			result.title =
				str::strip_and_cache(kv.second);
		else if (is_key(kv.first, "compatible_brands") || is_key(kv.first, "minor_version"))
		{
			// compatible_brands: 3gp4, avc1isom, isomavc1, isomiso2avc1mp41, isomiso2mp41, isommp42, M4A mp42isom, mp41isom, mp42mp41isomavc1, qt
			// minor_version: 3gp4, avc1isom, isomavc1, isomiso2avc1mp41, isomiso2mp41, isommp42, M4A mp42isom, mp41isom, mp42mp41isomavc1, qt
		}
		else if (is_key(kv.first, "rating"))
		{
			result.rating = str::to_int(kv.second);
		}
		else if (is_key(kv.first, "keywords"))
		{
			str::split2(kv.second, true, [this](const std::string_view text)
			{
				keywords.emplace_back(str::cache(text));
			});
		}
		else if (is_key(kv.first, "location-eng") || is_key(kv.first, "location") || is_key(
			kv.first, "com.apple.quicktime.location.ISO6709"))
		{
			const auto loc = split_location(kv.second);

			if (loc.success)
			{
				gps = gps_coordinate(loc.x, loc.y);
			}
		}
		else
		{
			//df::log(__FUNCTION__, std::format("Unknown tag: {} = {}", kv.first, kv.second));
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
			std::string text;
			text.assign(std::bit_cast<const char*>(metadata.xmp.data()), metadata.xmp.size());
			metadata_kv_list text_kv;
			text_kv.emplace_back(""_c, text);
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
