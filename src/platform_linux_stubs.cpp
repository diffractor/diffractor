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
#include "util_spell.h"
#include "util_zip.h"

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

av_packet_ptr av_format_decoder::read_packet() const
{
	return {};
}

void av_format_decoder::receive_frames(av_packet_queue&, av_frame_queue&)
{
	not_ported("ffmpeg"sv);
}

bool av_scaler::convert_yuv_surface(const ui::surface&, const ui::surface_ptr&)
{
	not_ported("ffmpeg"sv);
	return false;
}

int64_t av_pts_correction::guess(const int64_t best_effort, const int64_t pts, const int64_t dts, int64_t)
{
	// AV_NOPTS_VALUE without the FFmpeg headers. Without a decoder there is no stream to correct
	// against, so prefer the timestamps as given.
	constexpr int64_t no_pts = INT64_C(0x8000000000000000);
	if (best_effort != no_pts) return best_effort;
	if (pts != no_pts) return pts;
	return dts;
}

double av_audio_frame_duration(const av_frame_ptr&)
{
	return 0.0;
}

df::date_t dv_extract_rec_datetime(const uint8_t*, size_t)
{
	return {};
}

void av_session::process_io(const platform::thread_event&, const platform::thread_event&)
{
	not_ported("ffmpeg"sv);
}

audio_resampler::audio_resampler(const audio_info_t&)
{
	not_ported("ffmpeg"sv);
}

void audio_resampler::flush()
{
}

void audio_resampler::drain(audio_buffer&, int)
{
}

void audio_resampler::resample(const av_frame_ptr&, audio_buffer&)
{
	not_ported("ffmpeg"sv);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Audio format arithmetic. These are FFmpeg-free in principle, but they live beside the decoder
// and are defined in av_format.cpp, which is not built here.
///////////////////////////////////////////////////////////////////////////////////////////////////

// The layout is a shared_ptr to an FFmpeg type, so the real count is not reachable without the
// decoder. Zero propagates as "no audio", which is the state this build is in anyway.
uint32_t audio_info_t::channel_count() const
{
	return 0;
}

uint32_t audio_info_t::bytes_per_sample() const
{
	switch (sample_fmt)
	{
	case prop::audio_sample_t::unsigned_8bit: return 1;
	case prop::audio_sample_t::signed_16bit: return 2;
	case prop::audio_sample_t::signed_32bit:
	case prop::audio_sample_t::signed_float: return 4;
	case prop::audio_sample_t::signed_64bit:
	case prop::audio_sample_t::signed_double: return 8;
	default: return 0;
	}
}

uint32_t audio_info_t::bytes_per_second() const
{
	return sample_rate * channel_count() * bytes_per_sample();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Spell checking. Needs hunspell, which is not linked here.
///////////////////////////////////////////////////////////////////////////////////////////////////

// spell_check holds a unique_ptr<Hunspell>, so its destructor needs the type complete even though
// lazy_load never creates one here. Installing libhunspell-dev and building util_spell.cpp
// replaces this whole section.
class Hunspell
{
};

void spell_check::lazy_download(df::async_i&) const
{
	not_ported("hunspell"sv);
}

void spell_check::lazy_load()
{
	not_ported("hunspell"sv);
}

// Every word is accepted: marking correct words as misspelled would be worse than not checking.
bool spell_check::is_word_valid(std::string_view) const
{
	return true;
}

std::vector<std::string> spell_check::suggest(std::string_view) const
{
	return {};
}

spell_check& spell()
{
	static spell_check instance;
	return instance;
}

spell_check::spell_check() = default;
spell_check::~spell_check() = default;

///////////////////////////////////////////////////////////////////////////////////////////////////
// Channel layouts. FFmpeg types, so these can only hand back an empty layout.
///////////////////////////////////////////////////////////////////////////////////////////////////

channel_layout_ptr av_get_channel_layout(uint64_t, int)
{
	return {};
}

channel_layout_ptr av_get_def_channel_layout(int)
{
	return {};
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Zip. Needs minizip, which is not linked here.
///////////////////////////////////////////////////////////////////////////////////////////////////

df::zip_file::~zip_file() = default;

bool df::zip_file::create(file_path)
{
	not_ported("minizip"sv);
	return false;
}

bool df::zip_file::close()
{
	return false;
}

bool df::zip_file::add(file_path, std::string_view) const
{
	return false;
}

bool df::zip_file::add(file_path)
{
	return false;
}
