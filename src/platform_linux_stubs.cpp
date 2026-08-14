// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Deliberate stand-ins for the subsystems the Linux port has not reached, so the rest can
// be built and run. Nothing here pretends to work: each returns the value the caller already treats
// as failure, so no path can mistake a stub for a result.
//
// Every entry is a to-do. docs/linux.md owns the order they come off this list; the codec and XMP
// entries need their vendored libraries built, and the av_ entries need the FFmpeg fork.

#include "pch.h"

#include "av_format.h"
#include "av_player.h"
#include "files.h"
#include "metadata_xmp.h"

namespace
{
	void not_ported(const std::string_view what)
	{
		static df::hash_set<std::string> reported;

		// One line per subsystem, not per call: these sit on scan paths that run per file.
		if (reported.insert(std::string(what)).second)
		{
			df::log(__FUNCTION__, std::format("not ported to linux: {}", what));
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Codecs. Each needs its vendored library built; see docs/linux.md.
///////////////////////////////////////////////////////////////////////////////////////////////////

file_scan_result scan_heif(read_stream&, const scan_intent, const bool)
{
	not_ported("heif"sv);
	return {};
}

file_scan_result scan_jxl(read_stream&)
{
	not_ported("jxl"sv);
	return {};
}

ui::surface_ptr load_heif(read_stream&, load_diagnostic*)
{
	not_ported("heif"sv);
	return {};
}

ui::surface_ptr load_jxl(read_stream&, load_diagnostic*)
{
	not_ported("jxl"sv);
	return {};
}

file_load_result load_raw(df::file_path, bool)
{
	not_ported("raw"sv);
	return {};
}

file_scan_result files::scan_raw(df::file_path, std::string_view, bool, sizei, scan_intent)
{
	not_ported("raw"sv);
	return {};
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// XMP. Needs the vendored Adobe toolkit fork, which is not built here yet.
///////////////////////////////////////////////////////////////////////////////////////////////////

void metadata_xmp::initialise()
{
	not_ported("xmp"sv);
}

void metadata_xmp::term()
{
}

void metadata_xmp::parse(prop::item_metadata&, df::cspan)
{
	not_ported("xmp"sv);
}

void metadata_xmp::parse(prop::item_metadata&, df::file_path)
{
	not_ported("xmp"sv);
}

metadata_xmp::property_presence metadata_xmp::properties(df::cspan)
{
	not_ported("xmp"sv);
	return {};
}

bool metadata_xmp::has_embedded_xmp(df::file_path)
{
	not_ported("xmp"sv);
	return false;
}

// Reports failure rather than success-with-no-write: the write pipeline treats a false result as a
// refusal and leaves the user's file alone, which is the safe reading of "not implemented".
xmp_update_result metadata_xmp::update(df::file_path, df::file_path, const metadata_edits&, std::string_view,
                                       df::file_path)
{
	not_ported("xmp"sv);
	return {};
}

void metadata_xmp::update(std::string&, const metadata_edits&)
{
	not_ported("xmp"sv);
}

metadata_kv_list metadata_xmp::to_info(df::cspan)
{
	not_ported("xmp"sv);
	return {};
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Audio and video. Needs the vendored FFmpeg fork, which is not built here yet, so nothing
// registers a media file type and no container will open.
///////////////////////////////////////////////////////////////////////////////////////////////////

void av_initialise(file_type_by_extension&)
{
	not_ported("ffmpeg"sv);
}

av_scaler::~av_scaler() = default;

bool av_scaler::scale_surface(const ui::const_surface_ptr&, ui::surface_ptr&, sizei, bool)
{
	not_ported("ffmpeg"sv);
	return false;
}

av_pts_correction::av_pts_correction() : last_output(0), frame_interval(0)
{
}

bool av_format_decoder::open(df::file_path, media_intent)
{
	not_ported("ffmpeg"sv);
	return false;
}

bool av_format_decoder::open(const platform::file_ptr&, df::file_path, media_intent)
{
	not_ported("ffmpeg"sv);
	return false;
}

void av_format_decoder::init_streams(int, int, bool, bool, bool)
{
}

void av_format_decoder::close()
{
}

void av_format_decoder::extract_metadata(file_scan_result&) const
{
}

bool av_format_decoder::extract_thumbnail(ui::surface_ptr&, sizei, double, double, bool, double, df::cancel_token)
{
	not_ported("ffmpeg"sv);
	return false;
}

bool av_format_decoder::extract_seek_frame(ui::surface_ptr&, sizei, double, double, df::cancel_token)
{
	not_ported("ffmpeg"sv);
	return false;
}

bool av_format_decoder::seek(double) const
{
	return false;
}

ui::orientation av_format_decoder::calc_orientation() const
{
	return ui::orientation::top_left;
}

av_media_info av_format_decoder::info() const
{
	return {};
}

file_load_result av_format_decoder::render_frame(const av_frame_ptr&) const
{
	not_ported("ffmpeg"sv);
	return {};
}

std::unique_ptr<audio_resampler> av_format_decoder::make_audio_resampler() const
{
	return {};
}

audio_resampler::~audio_resampler() = default;

///////////////////////////////////////////////////////////////////////////////////////////////////
// Frame accessors. No decoder produces a frame in this build, so these are only reached with an
// empty pointer.
///////////////////////////////////////////////////////////////////////////////////////////////////

double av_time_from_frame(const av_frame_ptr&)
{
	return 0.0;
}

int av_seek_gen_from_frame(const av_frame_ptr&)
{
	return 0;
}

bool av_frame_is_eof(const av_frame_ptr&)
{
	return true;
}

bool av_is_frame_empty(const av_frame_ptr&)
{
	return true;
}

size_t av_queued_payload_bytes(const av_packet_ptr&)
{
	return 0;
}

size_t av_queued_payload_bytes(const av_frame_ptr&)
{
	return 0;
}

void av_session::seek(double, bool)
{
	not_ported("ffmpeg"sv);
}

void av_session::state(av_play_state)
{
	not_ported("ffmpeg"sv);
}
