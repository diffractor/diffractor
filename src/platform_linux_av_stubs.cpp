// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Stand-ins for everything av_format.cpp defines, for a build configured without the
// FFmpeg fork. Built only when diffractor::ffmpeg did not resolve, and it is av_format.cpp that is
// built instead: the two are alternatives, never both.
//
// Nothing here pretends to work. Each entry returns the value the caller already treats as
// failure, so no path can mistake a stub for a result: no media file type registers, no container
// opens, and every surface scale fails.

#include "pch.h"

#include "av_format.h"
#include "av_player.h"
#include "files.h"

namespace
{
	void not_ported()
	{
		static bool reported = false;

		// One line for the subsystem, not one per call: these sit on scan paths that run per file.
		if (!std::exchange(reported, true))
		{
			df::log(__FUNCTION__, "not ported to linux: ffmpeg");
		}
	}
}

void av_initialise(file_type_by_extension&)
{
	not_ported();
}

av_scaler::~av_scaler() = default;

bool av_scaler::scale_surface(const ui::const_surface_ptr&, ui::surface_ptr&, sizei, bool)
{
	not_ported();
	return false;
}

av_pts_correction::av_pts_correction() : last_output(0), frame_interval(0)
{
}

bool av_format_decoder::open(df::file_path, media_intent)
{
	not_ported();
	return false;
}

bool av_format_decoder::open(const platform::file_ptr&, df::file_path, media_intent)
{
	not_ported();
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
	not_ported();
	return false;
}

bool av_format_decoder::extract_seek_frame(ui::surface_ptr&, sizei, double, double, df::cancel_token)
{
	not_ported();
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
	not_ported();
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
	not_ported();
}

void av_session::state(av_play_state)
{
	not_ported();
}

av_packet_ptr av_format_decoder::read_packet() const
{
	return {};
}

void av_format_decoder::receive_frames(av_packet_queue&, av_frame_queue&)
{
	not_ported();
}

bool av_scaler::convert_yuv_surface(const ui::surface&, const ui::surface_ptr&)
{
	not_ported();
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
	not_ported();
}

audio_resampler::audio_resampler(const audio_info_t&)
{
	not_ported();
}

void audio_resampler::flush()
{
}

void audio_resampler::drain(audio_buffer&, int)
{
}

void audio_resampler::resample(const av_frame_ptr&, audio_buffer&)
{
	not_ported();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Audio format arithmetic. FFmpeg-free in principle, but av_format.cpp is where they are defined,
// so a build without it still has to supply them.
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
