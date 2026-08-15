// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: FFmpeg-based media format decoder. Handles video/audio stream parsing, codec management,
// frame extraction, seeking, and format conversion using libavformat and libavcodec.

#pragma once

#include "av_sound.h"


struct SwsContext;
struct AVFormatContext;
struct AVStream;
struct SwrContext;
struct AVCodecContext;
struct AVBufferRef;
struct AVFrame;
struct AVHWFramesContext;
struct ID3D11Texture2D;

class file_scan_result;
class audio_buffer;
class av_frame;
class av_packet;

// What the caller will do with the container once it is open. This decides how much of the file the
// demuxer may read before it answers, so every caller states it rather than inheriting a default.
// Container metadata - including an XMP packet written at the end of the file - is read by the
// demuxer's own header pass and is unaffected by the choice.
enum class media_intent
{
	// Properties only, for the index. Never decodes a picture, seeks, or displays, so the stream
	// probe is held to a byte budget instead of decoding frames to characterise the codec.
	metadata,
	// One frame at a chosen position.
	thumbnail,
	// Continuous decode with seeking and timing.
	playback
};
struct av_pts_correction;
struct audio_info_t;
struct video_info_t;
struct file_load_result;

// These cannot be forward-declared: an opaque enum declaration is an MSVC extension, and giving
// them a fixed underlying type here would contradict FFmpeg's own definition.
extern "C"
{
#include <libavutil/pixfmt.h>
#include <libavutil/samplefmt.h>
}


using av_packet_ptr = std::shared_ptr<av_packet>;
using av_frame_ptr = std::shared_ptr<av_frame>;

inline bool is_key(const std::string_view l, const std::string_view r)
{
	return str::icmp(l, r) == 0;
}

class file_type;
using file_type_by_extension = df::hash_map<std::string_view, file_type_ref, df::ihash, df::ieq>;
void av_initialise(file_type_by_extension& file_types);

// Describes a single FFmpeg codec for documentation generation.
enum class av_codec_media_type
{
	video,
	audio,
	other
};

struct av_codec_doc
{
	std::string name; // short codec name, e.g. "h264"
	std::string long_name; // human readable name, e.g. "H.264 / AVC / MPEG-4 AVC"
	av_codec_media_type media_type = av_codec_media_type::other;
};

// Enumerates the decoders compiled into the linked FFmpeg build. Used to
// generate the "Formats and Codecs" documentation page.
std::vector<av_codec_doc> av_supported_codecs();

// Extracts the camcorder recording date/time embedded in a raw DV video frame
// (DVCAM / DV-in-AVI). FFmpeg's DV demuxer does not surface this, so the
// BCD-encoded VAUX recording-date (0x62) and recording-time (0x63) packs are
// decoded here. Returns an invalid date when no usable pack is present.
df::date_t dv_extract_rec_datetime(const uint8_t* frame, size_t frame_size);

struct av_frame_d3d
{
	int width = 0;
	int height = 0;
	AVHWFramesContext* ctx = nullptr;
	ID3D11Texture2D* tex = nullptr;
	uintptr_t tex_index = 0;
	ui::orientation orientation = ui::orientation::top_left;
	ui::color_space color_space = ui::color_space::rec601_limited;
};

av_frame_d3d av_get_d3d_info(const av_frame_ptr& frame_in);
double av_time_from_frame(const av_frame_ptr& f);
int av_seek_gen_from_frame(const av_frame_ptr& f);
bool av_frame_is_eof(const av_frame_ptr& f);
// Playing time covered by an audio frame's samples; 0 when the frame is not audio.
double av_audio_frame_duration(const av_frame_ptr& f);
bool av_is_frame_empty(const av_frame_ptr& f);

// Bytes a queued item charges against the queue's read-ahead budget.
template <typename T>
size_t av_queued_payload_bytes(const std::shared_ptr<T>&) { return 0; }

size_t av_queued_payload_bytes(const av_packet_ptr& p);

// A decoded frame charges what it actually occupies: its own buffers when the decoder produced
// pixels, or the surface it pins in the hardware pool when it did not.
size_t av_queued_payload_bytes(const av_frame_ptr& f);

// Read-ahead the decoded queues may hold. A count alone made read-ahead cost whatever the
// resolution cost - 16 queued 1920x816 frames measure 63 MB, and 4K is four times that - so the
// budget is stated in bytes and the count is only the ceiling for frames small enough not to
// reach it.
inline constexpr size_t av_read_ahead_max_frames = 16;
inline constexpr size_t av_read_ahead_bytes = 24_z * 1024 * 1024;

// Below this the queue absorbs no decode jitter at all, so the byte budget never takes it lower
// however large a single frame is.
inline constexpr size_t av_read_ahead_min_frames = 3;

// Frames of `frame_bytes` each that the read-ahead budget allows. Sizes the queue and, for
// hardware decoding, the surface pool the queue draws from, so the two cannot disagree.
constexpr size_t av_read_ahead_frames(const size_t frame_bytes)
{
	if (frame_bytes == 0) return av_read_ahead_max_frames;
	return std::clamp(av_read_ahead_bytes / frame_bytes, av_read_ahead_min_frames, av_read_ahead_max_frames);
}

struct av_rational
{
	int num = 0;
	int den = 0;
};


struct video_info_t
{
	sizei display_dimensions;
	sizei render_dimensions;
	sizei aspect_ratio;
	AVPixelFormat format = {};
	bool is_yuv = false;
};


class audio_resampler final : df::no_copy
{
	SwrContext* _aud_resampler = nullptr;
	audio_info_t _frame_info;
	audio_info_t _output_info;
	audio_info_t _stream_info;
	double _gain = 1.0;

	// Reusable conversion scratch, sized for the output format and grown on demand
	// so the playback path does not allocate per decoded frame.
	uint8_t* _out_buffer = nullptr;
	int _out_buffer_size = 0;

	bool prepare_output(uint8_t** planes, int samples, const audio_info_t& format);

public:
	audio_resampler(const audio_info_t& info);
	~audio_resampler() override;

	void flush();
	// Emits any samples the resampler still has buffered (e.g. rate-conversion tail)
	// into audio_buffer at end of stream so the audio is not cut short.
	void drain(audio_buffer& audio_buffer, int gen);
	void resample(const av_frame_ptr& frame, audio_buffer& audio_buffer);

	// Software gain (>= 1.0) applied to resampled samples. Used for volume boost
	// above 100%, which the device volume (0.0-1.0) cannot provide. 1.0 = no boost.
	void set_gain(const double gain) { _gain = gain; }
};

class av_scaler final : df::no_copy
{
	SwsContext* _scaler = nullptr;

public:
	~av_scaler() override;

	bool scale_surface(const ui::const_surface_ptr& surface_in, ui::surface_ptr& surface_out, sizei dimensions_out,
	                   bool high_quality = true);
	bool convert_yuv_surface(const ui::surface& surface_in, const ui::surface_ptr& surface_out);
	bool scale_surface(const av_frame_ptr& frame, const ui::surface_ptr& surface_out);
	bool scale_frame(const AVFrame& frame, ui::surface_ptr& surface, sizei max_dim, double time,
	                 ui::orientation orientation);
};

// Decodes one still image from encoded bytes. This is the path for the formats Diffractor reads but
// has no dedicated decoder for - GIF, BMP, TIFF, TGA, SGI, the portable pixmaps, DPX - all of which
// ffmpeg carries on every platform. Returns null when the bytes are not an image it knows.
//
// TGA, SGI, the portable pixmaps and DPX have no signature worth probing, so a caller that knows the
// file name passes its extension: that is the only thing that can name the format for those.
ui::surface_ptr av_decode_still(df::cspan data, sizei max_dim, std::string_view extension_hint = {});


template <typename T>
class av_queue final : public df::no_copy
{
	// Depth at which a producer stops reading ahead. Deep enough to absorb decode
	// jitter, shallow enough that a seek discards little work - and bounded by
	// av_read_ahead_bytes as well, so the same depth does not cost four times as much
	// at 4K as it does at 1080p.
	static constexpr size_t max_queued = av_read_ahead_max_frames;

	// Hard ceiling, distinct from the read-ahead target above. The read loop demuxes
	// while *either* queue is below max_queued, so a stream whose queue never fills -
	// an attached-picture cover-art "video" track, or an audio track that ends before
	// the video - would otherwise let the other stream buffer the rest of the file.
	// Bounded by both count and bytes: 256 4K keyframes would be hundreds of megabytes.
	static constexpr size_t hard_max_queued = 256;
	static constexpr size_t hard_max_bytes = 32_z * 1024 * 1024;

	mutable platform::mutex _mutex;
	_Guarded_by_(_mutex) std::deque<std::shared_ptr<T>> _q;
	_Guarded_by_(_mutex) size_t _bytes = 0;

public:
	av_queue() = default;

	~av_queue() override
	{
		clear();
	}

	void clear()
	{
		platform::exclusive_lock lock(_mutex);
		_q.clear();
		_bytes = 0;
	}

	template <typename U>
	void push(U&& packet)
	{
		platform::exclusive_lock lock(_mutex);
		_bytes += av_queued_payload_bytes(packet);
		_q.emplace_back(std::forward<U>(packet));
	}

	double front_time() const
	{
		platform::shared_lock lock(_mutex);
		if (_q.empty() || _q.front() == nullptr) return 0.0;
		return av_time_from_frame(_q.front());
	}

	std::shared_ptr<T> front() const
	{
		platform::shared_lock lock(_mutex);
		if (_q.empty()) return nullptr;
		return _q.front();
	}

	bool pop(std::shared_ptr<T>& result)
	{
		platform::exclusive_lock lock(_mutex);
		if (_q.empty()) return false;
		result = std::move(_q.front());
		_q.pop_front();
		const auto bytes = av_queued_payload_bytes(result);
		_bytes -= std::min(_bytes, bytes);
		return true;
	}

	bool is_empty() const
	{
		platform::shared_lock lock(_mutex);
		return _q.empty();
	}

	bool should_receive() const
	{
		platform::shared_lock lock(_mutex);
		if (_q.size() >= max_queued) return false;
		return _q.size() < av_read_ahead_min_frames || _bytes < av_read_ahead_bytes;
	}

	bool is_full() const
	{
		platform::shared_lock lock(_mutex);
		return _q.size() >= hard_max_queued || _bytes >= hard_max_bytes;
	}
};

using av_packet_queue = av_queue<av_packet>;
using av_frame_queue = av_queue<av_frame>;

enum class av_stream_type
{
	video,
	audio,
	data,
	subtitle
};

struct av_stream_info
{
	int index = 0;
	bool is_playing = false;
	av_stream_type type = av_stream_type::data;
	std::string title;
	std::string codec;
	std::string fourcc;
	std::string language;
	std::string pixel_format;
	int audio_sample_rate = 0;
	int audio_channels = 0;
	prop::audio_sample_t audio_sample_type = prop::audio_sample_t::none;
	bool is_commentary = false;
	bool is_audio_description = false;
	int rotation = 0;

	metadata_kv_list metadata;
};

enum class metadata_standard
{
	media,
	exif,
	iptc,
	xmp,
	raw,
	ffmpeg,
	icc,
	// How the container itself is put together: segments, tables, and the encoder traces they leave.
	structure
};

// One embedded block as found in the file. `bytes` and `parsed` let the pane report what the block
// is and whether it was understood, so an unreadable block is distinguishable from an absent one.
struct metadata_block
{
	metadata_standard standard = metadata_standard::media;
	metadata_kv_list values;
	uint64_t bytes = 0;
	bool parsed = true;
	std::string raw;

	metadata_block() = default;

	metadata_block(const metadata_standard s, metadata_kv_list v) noexcept : standard(s), values(std::move(v))
	{
	}

	metadata_block(const metadata_standard s, metadata_kv_list v, const uint64_t b, const bool p,
	               std::string r = {}) noexcept : standard(s), values(std::move(v)), bytes(b), parsed(p),
	                                              raw(std::move(r))
	{
	}
};

class av_media_info
{
public:
	std::vector<av_stream_info> streams;
	std::vector<metadata_block> metadata;
	ui::image_ptr cover_art;

	str::cached pixel_format = {};
	str::cached video_codec = {};
	str::cached audio_codec = {};
	int audio_sample_rate = 0;
	int audio_channels = 0;

	int64_t bitrate = 0;
	double start = 0;
	double end = 0;

	sizei display_dimensions;
	sizei render_dimensions;

	ui::orientation display_orientation = ui::orientation::top_left;
	prop::audio_sample_t audio_sample_type = prop::audio_sample_t::none;

	bool has_audio = false;
	bool has_video = false;
	bool has_multiple_audio_streams = false;
};

// Turns a decoder's timestamps into a usable, strictly increasing presentation time.
// FFmpeg already resolves PTS against DTS and publishes the answer as
// AVFrame::best_effort_timestamp, so this only adds what it does not do: synthesising a
// timestamp when the stream supplies none, and repairing one that fails to advance.
struct av_pts_correction
{
	int64_t last_output; /// Last timestamp returned (keeps the output monotonic)
	int64_t frame_interval; /// Smallest positive step seen (synthesis fallback)

	av_pts_correction();
	void clear();
	// best_effort, pts and dts are AVFrame::best_effort_timestamp, ::pts and ::pkt_dts;
	// duration is AVFrame::duration (0 if unknown). Result is in stream time-base units.
	int64_t guess(int64_t best_effort, int64_t pts, int64_t dts, int64_t duration = 0);
};


class av_format_decoder final : public df::no_copy
{
	AVFormatContext* _format_context = nullptr;
	df::file_path _path;
	mutable bool _eof = false;

	av_pts_correction _pts_vid;
	av_pts_correction _pts_aud;
	platform::file_ptr _file;

public:
	av_format_decoder() = default;
	~av_format_decoder() override { close(); }

	std::unique_ptr<audio_resampler> make_audio_resampler() const;

	const df::file_path path() const
	{
		return _path;
	}

	bool open(df::file_path path, media_intent intent);
	bool open(const platform::file_ptr& file, df::file_path path, media_intent intent);
	void init_streams(int video_track, int audio_track, bool can_use_hw, bool video_only, bool can_use_threads);

	av_packet_ptr read_packet() const;

	// Lands on the key frame at or before `wanted`. Every caller decodes forward from there -
	// settling onto the sought frame, resuming where the user left off, or picking the frame
	// nearest a preview position - so a seek must never skip past what was asked for.
	bool seek(double wanted) const;
	void close();
	void extract_metadata(file_scan_result& sr) const;

	int64_t bitrate() const;

	const ui::image_ptr& cover_art() const { return _cover_art; }

private:
	mutable std::unique_ptr<av_scaler> _scaler;

	AVCodecContext* _video_context = nullptr;
	AVCodecContext* _audio_context = nullptr;

	AVBufferRef* _hw_device_ctx = nullptr;

	// True while this decoder holds a reference on the process-wide hardware-decode crash
	// guard (set when a HW decode context is opened, released on close). Ensures the guard
	// is decremented exactly once per increment even if close() runs more than once.
	bool _hw_decode_guard_held = false;

	bool _has_video = false;
	bool _has_audio = false;
	bool _is_open = false;
	bool _has_multiple_audio_streams = false;

	int _video_stream_index = -1; // _pVideoStream->index
	int _audio_stream_index = -1; // _pVideoStream->index
	int64_t _bitrate = 0;

	std::vector<av_stream_info> _streams;
	ui::image_ptr _cover_art;

	double _start_time = 0;
	double _end_time = 0;
	std::atomic<int> _rotation = 0;

	av_rational _video_base;
	av_rational _audio_base;
	av_rational _video_stream_aspect_ratio;

	int64_t _video_start_time = 0;
	int64_t _audio_start_time = 0;

	// The instant the presentation timeline starts, in AV_TIME_BASE units. Every playing
	// stream measures its timestamps from this one instant, so the offset between audio and
	// video survives; subtracting each stream's own start_time silently removed it.
	int64_t _time_origin = 0;

	void update_orientation(const AVFrame* frame);
	ui::orientation calc_orientation() const;

	bool decode_frame(ui::surface_ptr& dest_surface, AVCodecContext* ctx, const av_packet_ptr& packet, sizei max_dim);

	// Pulls every frame the decoder currently has ready onto the output queue,
	// timestamping each from the stream's time base. Shared by the normal decode
	// path and the end-of-stream drain so both stay in step.
	void receive_available_frames(AVCodecContext* ctx, av_pts_correction& pts, av_rational base, int64_t start,
	                              int seek_gen, av_frame_queue& frames);

	// Decode forward from the current (freshly seeked) position to the frame
	// nearest wanted_time, scaling that frame into dest_surface. Shared by thumbnail
	// and scrubber-preview extraction so both land on the pointed-at frame rather
	// than the key frame at the start of its GOP. Stops early at the first frame within
	// `tolerance` seconds of the target, and answers with the nearest frame reached so far
	// once `abandon` fires.
	bool decode_nearest_frame(ui::surface_ptr& dest_surface, sizei max_dim, double wanted_time, double tolerance,
	                          df::cancel_token abandon);

	friend class av_player;
	friend class av_session;
	friend class av_converter;

public:
	bool has_audio() const
	{
		return _has_audio;
	}

	bool has_video() const
	{
		return _has_video;
	}

	int video_stream_id() const
	{
		return _video_stream_index;
	}

	int audio_stream_id() const
	{
		return _audio_stream_index;
	}


	video_info_t video_information() const;
	audio_info_t audio_info() const;

	bool extract_seek_frame(ui::surface_ptr& dest_surface, sizei max_dim, double pos_numerator = 10,
	                        double pos_denominator = 100, df::cancel_token abandon = {});
	// tolerance_fraction says how near the requested position the frame has to be, as a fraction of
	// the duration. Zero decodes to the exact frame; a caller that only needs a sense of the content
	// asks for slack and gets an answer a whole GOP sooner.
	bool extract_thumbnail(ui::surface_ptr& dest_surface, sizei max_dim, double pos_numerator = 10,
	                       double pos_denominator = 100, bool exact_frame = true,
	                       double tolerance_fraction = 0.0, df::cancel_token abandon = {});
	file_load_result render_frame(const av_frame_ptr& frame_in) const;
	void receive_frames(av_packet_queue& packets, av_frame_queue& frames);

	av_media_info info() const;

	double start_time() const { return _start_time; }
	double end_time() const { return _end_time; }
	double to_video_seconds(int64_t vt) const;
};
