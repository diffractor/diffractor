// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Tests for the audio and video layer (av*) -- stream naming, audio buffering and
// visualization, container probing, seeking, playback timing and the hover preview decoder.

#include "pch.h"
#include "test.h"
#include "av_format.h"
#include "av_player.h"
#include "av_sound.h"
#include "av_visualizer.h"
#include "files.h"
#include "model.h"
#include "test_fixtures.h"
#include "test_runner.h"
#include "app_text.h"

static void should_format_audio_stream_names()
{
	av_stream_info stream;
	stream.type = av_stream_type::audio;
	stream.language = "eng";
	stream.audio_channels = 2;
	assert_equal("English - stereo", format_audio_stream_name(stream, 1), "language and channels");

	stream.language.clear();
	stream.title = "Director";
	stream.is_commentary = true;
	stream.audio_channels = 6;
	assert_equal("Director - commentary - 5.1 surround", format_audio_stream_name(stream, 1),
	             "title, role and channels");

	stream = {};
	stream.type = av_stream_type::audio;
	stream.audio_channels = 2;
	assert_equal("Audio track 2 - stereo", format_audio_stream_name(stream, 2), "numbered fallback");

	stream.audio_channels = 0;
	stream.codec = "aac";
	assert_equal("Audio track 3 - aac", format_audio_stream_name(stream, 3), "codec fallback");
}

static void should_compact_consumed_audio_only_when_needed()
{
	audio_info_t format;
	format.channel_layout = av_get_def_channel_layout(2);
	format.sample_fmt = prop::audio_sample_t::signed_16bit;
	format.sample_rate = 10;

	audio_buffer buffer;
	buffer.init(format);

	const std::array<uint8_t, 60> first{};
	const std::array<uint8_t, 40> second{};
	buffer.append(first.data(), static_cast<uint32_t>(first.size()), 1.0, 1);
	buffer.remove(40);
	buffer.append(second.data(), static_cast<uint32_t>(second.size()), 2.0, 1);

	assert_equal(60u, buffer.used_bytes(), "audio bytes retained after cursor compaction");
	assert_equal(1.5, buffer.seconds(), "audio duration retained after cursor compaction");
	assert_equal(1.5, buffer.start_time(), "audio start time follows appended frame timing");
	assert_equal(3.0, buffer.end_time(), "audio end time follows appended frame timing");

	audio_info_t visualizer_format;
	visualizer_format.channel_layout = av_get_def_channel_layout(2);
	visualizer_format.sample_fmt = prop::audio_sample_t::signed_16bit;
	visualizer_format.sample_rate = 48000;

	audio_buffer visualizer_buffer;
	visualizer_buffer.init(visualizer_format);
	std::array<int16_t, FFT_BUFFER_SIZE * 4> samples{};
	std::fill_n(samples.begin(), FFT_BUFFER_SIZE * 2, 100);
	for (size_t frame = 0; frame < FFT_BUFFER_SIZE; ++frame)
	{
		const auto sample = static_cast<int16_t>(12000.0 * sin(2.0 * M_PI * 16.0 * frame / FFT_BUFFER_SIZE));
		const auto i = FFT_BUFFER_SIZE * 2 + frame * 2;
		samples[i] = sample;
		samples[i + 1] = sample;
	}
	visualizer_buffer.append(std::bit_cast<const uint8_t*>(samples.data()),
	                         static_cast<uint32_t>(samples.size() * sizeof(int16_t)), 0.0, 1);
	visualizer_buffer.remove(FFT_BUFFER_SIZE * 4);

	av_visualizer visualizer;
	visualizer.update(visualizer_buffer);
	assert_equal(true, visualizer.step(1.0),
	             "visualizer consumes the live audio window");
	assert_equal(true, std::any_of(std::begin(visualizer._frame._data[0]),
	                               std::end(visualizer._frame._data[0]), [](const int bar) { return bar > 0; }),
	             "visualizer ignores consumed samples before the cursor");

	audio_info_t masked;
	masked.channel_layout = av_get_channel_layout(3, 1);
	assert_equal(2u, masked.channel_count(), "speaker mask defines channel count");
	audio_info_t fallback;
	fallback.channel_layout = av_get_channel_layout(0, 6);
	assert_equal(6u, fallback.channel_count(), "missing speaker mask uses endpoint channel count");
}

// audio_buffer keeps its samples private; this declared friend lets the test read them.
struct audio_ramp_probe
{
	static int16_t sample(const audio_buffer& buffer, const size_t i)
	{
		return std::bit_cast<const int16_t*>(buffer.data + buffer.start_pos)[i];
	}
};

static void should_ramp_audio_at_buffer_edges()
{
	audio_info_t format;
	format.channel_layout = av_get_def_channel_layout(2);
	format.sample_fmt = prop::audio_sample_t::signed_16bit;
	format.sample_rate = 1000;

	audio_buffer buffer;
	buffer.init(format);

	std::array<int16_t, 2000> samples{};
	samples.fill(1000);
	buffer.append(std::bit_cast<const uint8_t*>(samples.data()),
	              static_cast<uint32_t>(samples.size() * sizeof(int16_t)), 0.0, 1);

	buffer.apply_fade_in(0.010);
	buffer.apply_fade_out(0.010);

	// 1000 frames of stereo audio at 1kHz, so a 10ms ramp covers 10 frames = 20 samples.
	const auto sample = [&buffer](const size_t i) { return audio_ramp_probe::sample(buffer, i); };

	assert_equal(true, sample(0) < 200, "playback starts near silence");
	assert_equal(1000, static_cast<int>(sample(19)), "the fade in reaches full level after 10ms");
	assert_equal(1000, static_cast<int>(sample(1000)), "audio between the ramps is untouched");
	assert_equal(0, static_cast<int>(sample(1999)), "playback ends at silence");
	assert_equal(true, sample(1979) == 1000, "the fade out only covers the last 10ms");
	assert_equal(4000u, buffer.used_bytes(), "ramping does not consume buffered audio");
}

static void should_time_visualizer_independently_of_refresh_rate()
{
	auto animate = [](const double frame_seconds)
	{
		av_visualizer visualizer;
		av_visualizer::frame peak(0.0);
		peak._data[0][8] = 1000;
		visualizer._frames.push(peak);

		for (auto time = 0.0; time <= 0.1; time += frame_seconds)
		{
			visualizer.step(time);
		}

		return visualizer._frame._data[0][8];
	};

	const auto level_30hz = animate(1.0 / 30.0);
	const auto level_120hz = animate(1.0 / 120.0);
	assert_equal(true, std::abs(level_30hz - level_120hz) < 30,
	             "visualizer attack is independent of display refresh rate");

	av_visualizer visualizer;
	av_visualizer::frame loud(0.010);
	loud._data[0][8] = 1000;
	av_visualizer::frame quiet(0.020);
	visualizer._frames.push(loud);
	visualizer._frames.push(quiet);
	visualizer.step(0.0);
	assert_equal(true, visualizer._frame._data[0][8] > 0,
	             "visualizer preserves a transient between presentation frames");
}

static void should_extract_dv_datetime()
{
	// Build a minimal raw DV frame (one DIF sequence) carrying the VAUX
	// recording-date (0x62) and recording-time (0x63) packs at the offsets the
	// DV format places them (VAUX DIF block 3 of the sequence).
	std::vector<uint8_t> frame(12000, 0);

	auto* const date_pack = &frame[80 * 3 + 13];
	date_pack[0] = 0x62; // VAUX recording date pack id
	date_pack[1] = 0xff; // timezone unknown
	date_pack[2] = 0xc0 | (1 << 4) | 5; // day 15 (reserved bits set)
	date_pack[3] = (0 << 4) | 7; // month 07
	date_pack[4] = (0 << 4) | 3; // year 03 -> 2003

	auto* const time_pack = &frame[80 * 3 + 18];
	time_pack[0] = 0x63; // VAUX recording time pack id
	time_pack[1] = 0xff; // frames unknown
	time_pack[2] = (4 << 4) | 5; // 45 seconds
	time_pack[3] = (3 << 4) | 0; // 30 minutes
	time_pack[4] = (1 << 4) | 4; // 14 hours

	const auto actual = dv_extract_rec_datetime(frame.data(), frame.size());
	assert_equal(df::date_t(2003, 7, 15, 14, 30, 45), actual, "dv rec datetime");

	// A frame without recording packs must yield an invalid (absent) date.
	const std::vector<uint8_t> empty_frame(12000, 0);
	assert_equal(false, dv_extract_rec_datetime(empty_frame.data(), empty_frame.size()).is_valid(),
	             "dv no packs");
}

static void should_correct_pts()
{
	// av_pts_correction takes FFmpeg's AVFrame::best_effort_timestamp (falling back to pts
	// then pkt_dts) and guarantees a strictly increasing result so the presenter can always
	// order frames. AV_NOPTS_VALUE is INT64_MIN; mirror it here so the test does not need to
	// pull in the libav* headers.
	constexpr int64_t nopts = std::numeric_limits<int64_t>::min();

	// Clean, monotonic timestamps pass straight through.
	{
		av_pts_correction pc;
		assert_equal(0, static_cast<int>(pc.guess(0, 0, 0, 100)), "monotonic 0");
		assert_equal(100, static_cast<int>(pc.guess(100, 100, 100, 100)), "monotonic 100");
		assert_equal(200, static_cast<int>(pc.guess(200, 200, 200, 100)), "monotonic 200");
	}

	// best_effort_timestamp wins over pts and pkt_dts; those are only consulted when the
	// decoder had nothing to publish.
	{
		av_pts_correction pc;
		assert_equal(500, static_cast<int>(pc.guess(500, 700, 900, 100)), "best effort preferred");
		assert_equal(700, static_cast<int>(pc.guess(nopts, 700, 900, 100)), "falls back to pts");
		assert_equal(900, static_cast<int>(pc.guess(nopts, nopts, 900, 100)), "falls back to dts");
	}

	// A duplicated timestamp must not be returned verbatim - that would make the
	// presenter treat the frame as "not newer" and stall - so it is advanced by
	// one frame duration instead.
	{
		av_pts_correction pc;
		assert_equal(0, static_cast<int>(pc.guess(0, nopts, nopts, 100)), "dup first");
		assert_equal(100, static_cast<int>(pc.guess(100, nopts, nopts, 100)), "dup second");
		assert_equal(200, static_cast<int>(pc.guess(100, nopts, nopts, 100)), "dup advanced by duration");
	}

	// With no timestamps at all (raw / MJPEG streams) and no reported duration,
	// the timeline still advances using the cadence learned from earlier frames.
	{
		av_pts_correction pc;
		assert_equal(0, static_cast<int>(pc.guess(0, nopts, nopts, 0)), "nopts first");
		assert_equal(40, static_cast<int>(pc.guess(40, nopts, nopts, 0)), "nopts learn cadence");
		assert_equal(80, static_cast<int>(pc.guess(nopts, nopts, nopts, 0)), "nopts synth 1");
		assert_equal(120, static_cast<int>(pc.guess(nopts, nopts, nopts, 0)), "nopts synth 2");
	}

	// The learned cadence is the smallest step seen, not the most recent one: a gap in a
	// damaged stream must not become the synthetic step and run the timeline away.
	{
		av_pts_correction pc;
		assert_equal(0, static_cast<int>(pc.guess(0, nopts, nopts, 0)), "gap first");
		assert_equal(40, static_cast<int>(pc.guess(40, nopts, nopts, 0)), "gap learn cadence");
		assert_equal(9000, static_cast<int>(pc.guess(9000, nopts, nopts, 0)), "gap jump");
		assert_equal(9040, static_cast<int>(pc.guess(nopts, nopts, nopts, 0)), "synth uses smallest step");
	}

	// Invariant: however messy the timestamps (duplicates, backward jumps), the output is
	// always strictly increasing.
	{
		av_pts_correction pc;
		const int64_t messy[] = {0, 200, 100, 100, 400, 300, 500};
		auto prev = nopts;
		for (const auto p : messy)
		{
			const auto t = pc.guess(p, nopts, nopts, 50);
			if (prev != nopts) assert_equal(true, t > prev, "strictly increasing");
			prev = t;
		}
	}
}

// The index scan bounds FFmpeg's stream probe; the inspect scan does not. Every property the index
// records has to survive that bound, so the two intents are compared across the AV containers.
static void should_scan_av_metadata_with_a_bounded_probe()
{
	auto fixtures_carrying_xmp = 0;

	for (const auto* const name : {
		     "gizmo.mp4", "indy.mp4", "anamorphic.mp4", "ipod.mov", "StPauls.MOV", "tagged.mkv",
		     "Byzantium.avi", "Colorblind.mp3"
	     })
	{
		const auto path = test_files_folder.combine_file(name);
		const auto* const ft = files::file_type_from_name(path);

		files ff;
		const auto inspected = ff.scan_file(path, false, ft, {}, {}, scan_intent::inspect);
		const auto indexed = ff.scan_file(path, false, ft, {}, {}, scan_intent::index);

		assert_equal(true, indexed.success, name, "index scan succeeded");
		assert_equal(inspected.width, indexed.width, name, "width");
		assert_equal(inspected.height, indexed.height, name, "height");
		assert_equal(inspected.duration, indexed.duration, name, "duration");
		assert_equal(inspected.video_codec.sv(), indexed.video_codec.sv(), name, "video codec");
		assert_equal(inspected.pixel_format.sv(), indexed.pixel_format.sv(), name, "pixel format");
		assert_equal(inspected.audio_codec.sv(), indexed.audio_codec.sv(), name, "audio codec");
		assert_equal(inspected.audio_sample_rate, indexed.audio_sample_rate, name, "audio sample rate");
		assert_equal(inspected.audio_channels, indexed.audio_channels, name, "audio channels");
		assert_equal(static_cast<int>(inspected.audio_sample_type), static_cast<int>(indexed.audio_sample_type),
		             name, "audio sample type");
		assert_equal(inspected.bitrate.sv(), indexed.bitrate.sv(), name, "bit rate");
		assert_equal(inspected.orientation, indexed.orientation, name, "orientation");
		assert_equal(inspected.created_utc, indexed.created_utc, name, "created");

		// gizmo.mp4, ipod.mov and Byzantium.avi carry their XMP packet in the last 1% of the file,
		// megabytes past the probe budget. It survives because container metadata is read by the
		// demuxer's read_header, inside avformat_open_input, which the bound is applied after.
		assert_equal(inspected.metadata.xmp.size(), indexed.metadata.xmp.size(), name, "xmp size");
		assert_equal(true, std::ranges::equal(inspected.metadata.xmp, indexed.metadata.xmp), name, "xmp bytes");

		if (!inspected.metadata.xmp.empty()) ++fixtures_carrying_xmp;
	}

	// Without this the XMP assertions above would pass on an empty packet and prove nothing.
	assert_equal(3, fixtures_carrying_xmp, "fixtures carrying a trailing xmp packet");
}

// FFmpeg falls back to matching a demuxer on the file extension alone, so a TypeScript source file
// named index.ts is handed to the MPEG-TS demuxer, opens without any error and then describes no
// stream whatsoever. The scan must report that as a failure rather than publish an empty video.
static void should_reject_a_non_media_file()
{
	const auto path = test_files_folder.combine("excluded1").combine_file("not-media.ts");
	const auto* const ft = files::file_type_from_name(path);

	// The extension alone still says video; only the header settles it.
	assert_equal(true, path.exists(), "fixture is present");
	assert_equal(true, ft->has_trait(file_traits::av), "ts is an av extension");
	assert_equal(false, files::media_header_matches(path.extension(), df::blob_from_file(path, 1024)),
	             "the header rule refuses it before the demuxer sees it");

	av_format_decoder decoder;
	assert_equal(false, decoder.open(path, media_intent::metadata), "decoder rejects the file");

	files ff;
	assert_equal(false, ff.scan_file(path, false, ft, {}, {}, scan_intent::inspect).success, "inspect scan fails");
	assert_equal(false, ff.scan_file(path, false, ft, {}, {}, scan_intent::index).success, "index scan fails");
}

// Issue #78 - Some videos ignore aspect ratio.
// anamorphic.mp4 is stored at 640x480 with a 4:3 pixel (sample) aspect ratio,
// i.e. a 16:9 display. The scanner must report the display dimensions (640x360)
// rather than the stored frame size (640x480).
static void should_apply_video_aspect_ratio()
{
	const auto load_path = test_files_folder.combine_file("anamorphic.mp4");

	files ff;
	const auto actual = ff_scan_file(ff, load_path);
	const auto md = actual.to_props();

	assert_equal(640, md->width, "anamorphic display width");
	assert_equal(360, md->height, "anamorphic display height");
}

// A container-level seek does not flush the codec, so extract_thumbnail must flush
// the decoder after seeking - otherwise, when the decoder is reused across calls, a
// later-position thumbnail can be served from a frame that was buffered before the
// seek. This reuses one decoder for an early and a late thumbnail (the exact reuse
// scenario) and asserts the two frames differ.
static void should_flush_decoder_on_thumbnail_seek()
{
	const auto load_path = test_files_folder.combine_file("gizmo.mp4");

	av_format_decoder decoder;
	assert_equal(true, decoder.open(load_path, media_intent::thumbnail), "open gizmo.mp4");
	decoder.init_streams(-1, -1, false, false, false);
	assert_equal(true, decoder.has_video(), "gizmo.mp4 has video");

	constexpr sizei max_dim(256, 256);

	ui::surface_ptr early;
	assert_equal(true, decoder.extract_thumbnail(early, max_dim, 1, 100), "early thumbnail decoded");
	assert_equal(true, is_valid(early), "early thumbnail valid");

	// Reuse the same decoder; the seek to 95% must flush the frames buffered by the
	// early extraction above rather than replaying one of them.
	ui::surface_ptr late;
	assert_equal(true, decoder.extract_thumbnail(late, max_dim, 95, 100), "late thumbnail decoded");
	assert_equal(true, is_valid(late), "late thumbnail valid");

	const auto same_size = is_valid(early) && is_valid(late) && early->size() == late->size();
	assert_equal(true, same_size, "thumbnails allocated to the same size");

	const auto identical = same_size && memcmp(early->pixels(), late->pixels(), early->size()) == 0;
	assert_equal(false, identical, "late thumbnail differs from early (decoder flushed after seek)");
}

// A media seek can only land on a key frame, so the caller has to say which side of the
// requested time it may land on. avformat_seek_file clears AVSEEK_FLAG_BACKWARD and instead
// derives the direction from the min/max window, so the window centred on the target that
// this used to pass always resolved to the key frame *after* the request. Nothing can decode
// backwards from there, so the scrubber preview - and playback resuming at a saved position -
// silently skipped up to a whole GOP of content. indy.mp4 has ~10s between key frames, which
// is far wider than the tolerance here.
static void should_seek_to_the_frame_at_the_requested_time()
{
	const auto load_path = test_files_folder.combine_file("indy.mp4");

	av_format_decoder decoder;
	assert_equal(true, decoder.open(load_path, media_intent::thumbnail), "open indy.mp4");
	decoder.init_streams(-1, -1, false, false, false);
	assert_equal(true, decoder.has_video(), "indy.mp4 has video");

	constexpr sizei max_dim(256, 256);
	const auto start = decoder.start_time();
	const auto len = decoder.end_time() - start;
	assert_equal(true, len > 60.0, "indy.mp4 is long enough to span several key frames");

	constexpr double numerator = 60;
	constexpr double denominator = 100;
	const auto wanted = start + floor(numerator * len / denominator);

	ui::surface_ptr s;
	assert_equal(true, decoder.extract_seek_frame(s, max_dim, numerator, denominator), "seek frame decoded");
	assert_equal(true, is_valid(s), "seek frame valid");
	assert_equal(true, fabs(s->time() - wanted) < 1.0,
	             std::format("decoded frame is at the requested time (wanted {}, got {})", wanted, s->time()));
}

// The library thumbnail is the key frame at or before a tenth of the way in, decoded once rather
// than walked to. Skipping the seek for any position under two seconds - which is every clip
// shorter than twenty - silently thumbnailed those from frame zero instead of the tenth asked for.
static void should_seek_short_video_thumbnails()
{
	const auto path = test_files_folder.combine_file("StPauls.MOV");

	av_format_decoder dec;
	assert_equal(true, dec.open(path, media_intent::thumbnail), "decoder opened");
	dec.init_streams(-1, -1, false, true, false);
	assert_equal(true, dec.has_video(), "StPauls.MOV has video");

	const auto duration = dec.end_time() - dec.start_time();
	assert_equal(true, duration > 2.0 && duration < 20.0, "the clip is short enough to have skipped the seek");

	ui::surface_ptr thumbnail;
	assert_equal(true, dec.extract_thumbnail(thumbnail, {256, 256}, 10, 100, false), "thumbnail decoded");
	assert_equal(true, is_valid(thumbnail), "thumbnail surface");

	const auto wanted = duration / 10.0;

	assert_equal(true, thumbnail->time() > 0.0,
	             std::format("a short clip thumbnails from {:.2f}s, not frame zero", thumbnail->time()));
	assert_equal(true, thumbnail->time() <= wanted + 0.5,
	             std::format("the key frame is at or before {:.2f}s (got {:.2f}s)", wanted, thumbnail->time()));

	dec.close();
}

// A clip with no audio track has no device clock, so it is timed off the wall clock and the
// stream's own end is the only thing that separates "played out" from "decode fell behind". That
// end-of-stream marker carries no media timestamp, so leaving it at the head of the frame queue
// made front_time() report zero and every distance comparison refuse to look past it - the marker
// was never consumed and the clip could only end on the two-second hard fallback.
static void should_end_a_silent_clip_at_the_stream_end()
{
	df::file_path silent_path;

	for (const auto* const name : {"anamorphic.mp4", "gizmo.mp4", "tagged.mkv", "tagged.webm"})
	{
		const auto candidate = test_files_folder.combine_file(name);

		av_format_decoder probe;
		if (!probe.open(candidate, media_intent::playback)) continue;
		probe.init_streams(-1, -1, false, false, false);

		if (probe.has_video() && !probe.has_audio())
		{
			silent_path = candidate;
			break;
		}
	}

	assert_equal(false, silent_path.is_empty(), "a video-only test file is available");

	const auto ses = make_test_session();
	assert_equal(true, ses->open(silent_path, files::file_type_from_name(silent_path), 0.0, true, -1, -1, false,
	                             false, false), "session opened");
	assert_equal(true, ses->is_playing(), "session auto-plays");

	const auto media_end = ses->info().end;
	assert_equal(true, media_end > 0.0, "the test clip declares a duration");

	auto now = df::now();
	assert_equal(false, ses->has_ended(now), "a freshly opened clip has not ended");

	// Drive the demux, decode and present work the player threads normally own.
	const platform::thread_event video_event(false, false);
	const platform::thread_event audio_event(false, false);
	const platform::thread_event read_event(false, false);

	auto ended_at = -1.0;

	for (auto i = 0; i < 4000 && ended_at < 0.0; ++i)
	{
		ses->process_io(video_event, audio_event);
		ses->process_video(read_event);
		now += 0.02;
		ses->update_for_present(now);
		if (ses->has_ended(now)) ended_at = ses->pos(now);
	}

	assert_equal(true, ended_at >= 0.0, "the clip ends");
	assert_equal(true, ended_at < media_end + 1.0,
	             std::format("ends on the stream end, not the 2s fallback (end {:.2f}, ended at {:.2f})",
	                         media_end, ended_at));

	ses->close(false);
}

// Read-ahead used to be counted in frames alone, so what it cost depended entirely on the
// resolution: sixteen queued 1920x816 frames measured 63 MB of process commit, and 4K is four
// times the frame. The budget is stated in bytes now, and this holds the queue to it.
static void should_bound_video_read_ahead_by_bytes()
{
	const auto path = test_files_folder.combine_file("indy.mp4");

	av_format_decoder dec;
	assert_equal(true, dec.open(path, media_intent::playback), "decoder opened");
	dec.init_streams(-1, -1, false, false, true);
	assert_equal(true, dec.has_video(), "indy.mp4 has video");

	av_packet_queue packets;
	av_frame_queue decoded;
	av_frame_queue video;
	auto at_end = false;

	for (auto i = 0; i < 3000 && !at_end && video.should_receive(); ++i)
	{
		auto p = dec.read_packet();
		if (!p) break;

		packets.push(p);
		dec.receive_frames(packets, decoded);

		// The decoder queue carries both streams and the end-of-stream marker; only the video
		// frames are under test.
		for (av_frame_ptr f; decoded.pop(f); f.reset())
		{
			if (av_frame_is_eof(f)) at_end = true;
			else if (!av_is_frame_empty(f)) video.push(f);
		}
	}

	// Otherwise the queue stopped because the clip ran out, and the budget was never tested.
	assert_equal(false, at_end, "the clip is long enough to fill the read-ahead budget");

	size_t count = 0;
	size_t bytes = 0;
	size_t frame_bytes = 0;

	for (av_frame_ptr f; video.pop(f); f.reset())
	{
		frame_bytes = av_queued_payload_bytes(f);
		bytes += frame_bytes;
		++count;
	}

	assert_equal(true, frame_bytes > 0, "a decoded frame charges what its buffers cost");
	assert_equal(true, count >= av_read_ahead_min_frames, "read-ahead keeps enough frames to absorb decode jitter");
	assert_equal(true, count < av_read_ahead_max_frames,
	             std::format("the byte budget is reached before the frame count cap ({} frames)", count));

	// The queue only stops asking once the budget is met, so it can overshoot by the frames the
	// packet in flight produced - but by no more than that.
	assert_equal(true, bytes <= av_read_ahead_bytes + frame_bytes,
	             std::format("read-ahead stays inside its budget ({} frames, {} bytes)", count, bytes));

	dec.close();
}

// Scrubbing sets the wall clock from the position the user asked for, then the audio device
// re-anchors it to the first sample it is handed. Those two have to agree: if the audio timeline
// lands somewhere other than the sought position, pos() jumps when the device starts and the view
// sits frozen until the clock catches back up to the frame already on screen.
// A seek can only land on a key sample. gizmo.mp4 carries a single video key frame, so seeking
// anywhere in it puts the demuxer back at the start. update_for_present settles the video forward
// onto the position asked for; audio has no such step, so without trimming it the buffer - and the
// device clock anchored to it - starts at the key sample and the view sits frozen on the settled
// frame until the clock catches back up. That freeze is what a scrub used to produce.
static void should_land_audio_and_video_on_the_sought_position()
{
	const auto load_path = test_files_folder.combine_file("gizmo.mp4");

	const auto ses = make_test_session();
	assert_equal(true, ses->open(load_path, files::file_type_from_name(load_path), 0.0, true, -1, -1, false,
	                             false, false), "session opened");

	const auto media_end = ses->info().end;
	assert_equal(true, ses->info().has_audio && ses->info().has_video, "gizmo.mp4 has both streams");

	constexpr auto wanted = 3.0;
	assert_equal(true, media_end > wanted + 1.0, "the clip is long enough to seek into");

	ses->seek(wanted, true);

	audio_info_t fmt;
	fmt.channel_layout = av_get_def_channel_layout(2);
	fmt.sample_fmt = prop::audio_sample_t::signed_16bit;
	fmt.sample_rate = 48000;

	audio_buffer playback_buffer;
	audio_buffer vis_buffer;
	playback_buffer.init(fmt);
	vis_buffer.init(fmt);

	const platform::thread_event video_event(false, false);
	const platform::thread_event audio_event(false, false);
	const platform::thread_event read_event(false, false);

	auto now = df::now();

	for (auto i = 0; i < 200; ++i)
	{
		ses->process_io(video_event, audio_event);
		ses->process_video(read_event);
		ses->process_audio(playback_buffer, vis_buffer, read_event);
		now += 0.02;
		ses->update_for_present(now);
	}

	assert_equal(false, playback_buffer.is_empty(), "audio decoded after the seek");

	const auto audio_at = playback_buffer.start_time();
	const auto video_at = ses->time();

	assert_equal(true, fabs(audio_at - wanted) < 0.5,
	             std::format("audio lands on the sought position (wanted {:.2f}, got {:.2f})", wanted, audio_at));
	assert_equal(true, fabs(video_at - wanted) < 0.5,
	             std::format("video lands on the sought position (wanted {:.2f}, got {:.2f})", wanted, video_at));

	ses->close(false);
}

// The scrubber tooltip and the hovered item thumbnail both scrub through a video by asking the
// preview decoder - a second FFmpeg instance, separate from playback - for the frame nearest a
// position. Each position must answer with its own frame; a decoder that returns the same key
// frame everywhere looks exactly like a preview that has stopped working.
static void should_preview_video_frames_at_hover_positions()
{
	const auto path = test_files_folder.combine_file("indy.mp4");

	media_preview_state preview;
	assert_equal(true, preview.open1(path), "preview decoder opened");

	const auto duration = preview.decoder1->end_time() - preview.decoder1->start_time();
	assert_equal(true, duration > 1.0, "the clip is long enough to scrub");

	double previous_time = -1.0;

	for (const auto pos : {10, 45, 80})
	{
		auto surface = std::make_shared<ui::surface>();

		assert_equal(true, preview.decoder1->extract_seek_frame(surface, {256, 256}, pos, 100),
		             std::format("seek preview decoded at {}%", pos));
		assert_equal(true, is_valid(surface), std::format("seek preview surface at {}%", pos));

		const auto expected = duration * pos / 100.0;
		assert_equal(true, std::abs(surface->time() - expected) < 2.0,
		             std::format("preview at {}% lands near {:.2f}s, not {:.2f}s", pos, expected, surface->time()));
		assert_equal(true, surface->time() > previous_time,
		             std::format("preview at {}% is later than the one before it", pos));

		previous_time = surface->time();
	}

	// The hovered-thumbnail path re-enters the same open decoder. Asking it for a position near the
	// start after it has been left near the end is what proves it seeks: every frame where it was
	// left is already past the requested time, so a decoder that walks forward without seeking
	// answers with the frame it happens to be sitting on.
	auto thumbnail = std::make_shared<ui::surface>();
	assert_equal(true, preview.decoder1->extract_thumbnail(thumbnail, {256, 256}, 1, 100),
	             "hover thumbnail decoded from the reused decoder");
	assert_equal(true, is_valid(thumbnail), "hover thumbnail surface");
	assert_equal(true, thumbnail->time() < previous_time,
	             std::format("a backward hover rewinds the reused decoder ({:.2f}s, was {:.2f}s)",
	                         thumbnail->time(), previous_time));

	preview.close();
}

// A hover queues a preview for every pixel the pointer moves, so the decoder behind them has to
// survive the sequence: reopening the container costs a probe that decodes frames of its own, far
// more than the preview itself. It is also what makes an abandoned walk safe to retry.
static void should_reuse_the_preview_decoder_across_hovers()
{
	const auto path = test_files_folder.combine_file("indy.mp4");

	media_preview_state preview;
	assert_equal(true, preview.open1(path), "preview decoder opened");

	const auto* const first = preview.decoder1.get();

	for (const auto pos : {10, 20, 30, 20, 10})
	{
		assert_equal(true, preview.open1(path), std::format("preview decoder available at {}%", pos));
		assert_equal(true, first == preview.decoder1.get(),
		             std::format("hover at {}% reused the open decoder rather than reopening", pos));

		auto surface = std::make_shared<ui::surface>();
		assert_equal(true, preview.decoder1->extract_seek_frame(surface, {256, 256}, pos, 100),
		             std::format("preview decoded at {}%", pos));
	}

	// A newer hover is already waiting, so the walk toward the exact frame stops at the nearest
	// frame it has reached instead of finishing a result nobody will see.
	std::atomic_bool superseded = true;
	preview.superseded = &superseded;

	auto abandoned = std::make_shared<ui::surface>();
	assert_equal(true,
	             preview.decoder1->extract_seek_frame(abandoned, {256, 256}, 60, 100, preview.abandon_token()),
	             "an abandoned walk still answers with the frame it reached");
	assert_equal(true, is_valid(abandoned), "abandoned preview surface");

	preview.close();
}

// A hovered thumbnail says what the video contains, not where in it the pointer sits, so it accepts
// any frame within a fraction of the duration. That slack is the whole point: the key frame the
// seek already landed on qualifies, so the walk stops there instead of decoding the rest of the GOP.
static void should_allow_tolerance_for_hover_thumbnails()
{
	const auto path = test_files_folder.combine_file("indy.mp4");

	av_format_decoder dec;
	assert_equal(true, dec.open(path, media_intent::thumbnail), "decoder opened");
	dec.init_streams(-1, -1, false, true, true);
	assert_equal(true, dec.has_video(), "indy.mp4 has video");

	const auto duration = dec.end_time() - dec.start_time();
	constexpr auto pos = 10.0;
	const auto wanted = duration * pos / 100.0;

	ui::surface_ptr key_frame;
	assert_equal(true, dec.extract_thumbnail(key_frame, {256, 256}, pos, 100, false), "key frame decoded");

	ui::surface_ptr exact;
	assert_equal(true, dec.extract_thumbnail(exact, {256, 256}, pos, 100, true, 0.0), "exact frame decoded");

	ui::surface_ptr toleranced;
	assert_equal(true, dec.extract_thumbnail(toleranced, {256, 256}, pos, 100, true, 0.02),
	             "toleranced frame decoded");

	// Unless the exact frame is a real walk past the key frame, the tolerance proves nothing.
	assert_equal(true, exact->time() > key_frame->time(), "the exact frame is a walk past the key frame");
	assert_equal(true, std::abs(key_frame->time() - wanted) <= duration * 0.02,
	             "the key frame is inside the tolerance this test asks for");

	assert_equal(true, df::equiv(toleranced->time(), key_frame->time()),
	             std::format("the walk stopped at the key frame ({:.2f}s, key {:.2f}s, exact {:.2f}s)",
	                         toleranced->time(), key_frame->time(), exact->time()));

	dec.close();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Session lifetime
///////////////////////////////////////////////////////////////////////////////////////////////////

// A reopen queued by detach_file_handles lands on the UI thread long after the teardown that queued
// it. If a second teardown or a navigation has taken the display since, publishing would reopen the
// file the caller is renaming, replacing or deleting -- the handle the detach exists to release. The
// generation is what makes that visible to the callback, which then closes rather than publishes.
static void should_reject_superseded_av_session()
{
	null_async_strategy as;
	common_display_state_t common;
	const auto d = std::make_shared<display_state_t>(as, common);

	const auto first = ++d->_av_generation;
	const auto opened = make_test_session();

	assert_equal(true, d->publish_av_session(opened, first), "current generation publishes");
	assert_equal(true, d->_session == opened, "session installed on the display");

	// A teardown supersedes it and clears the display, exactly as detach_file_handles does.
	const auto second = ++d->_av_generation;
	d->_session.reset();

	const auto late = make_test_session();
	assert_equal(false, d->publish_av_session(late, first), "superseded generation rejected");
	assert_equal(true, d->_session == nullptr, "display left detached, caller owns closing the session");

	assert_equal(true, d->publish_av_session(late, second), "current generation publishes again");
	assert_equal(true, d->_session == late, "newest session installed");
}

// design.md fixes these three thresholds, and they are the difference between a helpful resume and
// a video that will not start from the beginning. Nothing else defends the numbers.
static void should_resume_only_in_the_middle_of_long_media()
{
	// A clip at or under ten seconds never resumes, however far in the saved position is.
	assert_equal(false, should_resume_at(0.0, 10.0, 5.0), "ten seconds is not long enough to resume");
	assert_equal(false, should_resume_at(0.0, 9.0, 5.0), "a short clip never resumes");
	assert_equal(true, should_resume_at(0.0, 10.5, 5.0), "just over ten seconds can resume");

	// Barely started: inside two seconds of the start is a restart, not a resume.
	assert_equal(false, should_resume_at(0.0, 60.0, 0.0), "a zero position is not a resume");
	assert_equal(false, should_resume_at(0.0, 60.0, 2.0), "exactly two seconds in is still the start");
	assert_equal(true, should_resume_at(0.0, 60.0, 2.5), "past two seconds is a resume");

	// Effectively finished: inside five seconds of the end would resume onto the credits.
	assert_equal(false, should_resume_at(0.0, 60.0, 55.0), "exactly five seconds from the end is the end");
	assert_equal(false, should_resume_at(0.0, 60.0, 59.9), "a position at the end is not a resume");
	assert_equal(true, should_resume_at(0.0, 60.0, 54.5), "before the last five seconds is a resume");

	// The window is measured from the stream's own start, not from zero, so a container whose first
	// timestamp is not zero gets the same two-second grace.
	assert_equal(false, should_resume_at(100.0, 160.0, 101.0), "the start grace follows the stream start");
	assert_equal(true, should_resume_at(100.0, 160.0, 103.0), "past the start grace on an offset stream");
	assert_equal(false, should_resume_at(100.0, 160.0, 156.0), "the end grace follows the stream end");

	// A saved position outside the media entirely cannot resume.
	assert_equal(false, should_resume_at(0.0, 60.0, -5.0), "a negative position is refused");
	assert_equal(false, should_resume_at(0.0, 60.0, 120.0), "a position past the end is refused");
}

// design.md: closing while a seek or resume is still synchronizing must save the accepted target.
// Saving the presented time instead writes zero over the position the user just resumed to.
static void should_save_the_accepted_target_while_synchronizing()
{
	assert_equal(42.0, position_to_save(false, 17.0, 42.0), "a settled session saves the presented time");

	// The frame time is still 0 because no resumed frame has arrived yet; saving it would lose the
	// position. This is the case the branch exists for.
	assert_equal(17.0, position_to_save(true, 17.0, 0.0), "a synchronizing session saves the seek target");
	assert_equal(17.0, position_to_save(true, 17.0, 42.0), "synchronizing wins over a stale presented time");
}

void register_av_tests(view_state& state, test_registry& tests)
{
	//
	// Resume
	//
	tests.add("Should resume only in the middle of long media"s, should_resume_only_in_the_middle_of_long_media);
	tests.add("Should save the accepted target while synchronizing"s,
	          should_save_the_accepted_target_while_synchronizing);

	//
	// Audio
	//
	tests.add("Should format audio stream names"s, should_format_audio_stream_names);
	tests.add("Should retain audio buffer timing across cursor compaction"s,
	          should_compact_consumed_audio_only_when_needed);
	tests.add("Should ramp audio at buffer edges"s, should_ramp_audio_at_buffer_edges);
	tests.add("Should time visualizer independently of refresh rate"s,
	          should_time_visualizer_independently_of_refresh_rate);

	//
	// Probe
	//
	tests.add("Should extract dv datetime"s, should_extract_dv_datetime);
	tests.add("Should correct pts"s, should_correct_pts);
	tests.add("Should scan av metadata with a bounded probe"s, should_scan_av_metadata_with_a_bounded_probe);
	tests.add("Should reject a non media file"s, should_reject_a_non_media_file);

	// Issue #78 - video aspect ratio
	tests.add("Should apply video aspect ratio"s, should_apply_video_aspect_ratio);

	//
	// Seeking
	//
	tests.add("Should flush decoder on thumbnail seek"s, should_flush_decoder_on_thumbnail_seek);
	tests.add("Should seek to the frame at the requested time"s, should_seek_to_the_frame_at_the_requested_time);
	tests.add("Should seek short video thumbnails"s, should_seek_short_video_thumbnails);

	//
	// Playback
	//
	tests.add("Should end a silent clip at the stream end"s, should_end_a_silent_clip_at_the_stream_end);
	tests.add("Should bound video read ahead by bytes"s, should_bound_video_read_ahead_by_bytes);
	tests.add("Should land audio and video on the sought position"s,
	          should_land_audio_and_video_on_the_sought_position);

	//
	// Hover preview
	//
	tests.add("Should preview video frames at hover positions"s, should_preview_video_frames_at_hover_positions);
	tests.add("Should reuse the preview decoder across hovers"s, should_reuse_the_preview_decoder_across_hovers);
	tests.add("Should allow tolerance for hover thumbnails"s, should_allow_tolerance_for_hover_thumbnails);

	//
	// Session lifetime
	//
	tests.add("Should reject superseded av session"s, should_reject_superseded_av_session);
}
