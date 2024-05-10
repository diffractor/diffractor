// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

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
struct AVStream;

class file_scan_result;
class audio_buffer;
class av_frame;
class av_packet;
struct av_pts_correction;
struct audio_info_t;
struct video_info_t;
struct file_load_result;
enum AVSampleFormat;
enum AVPixelFormat;


using av_packet_ptr = std::shared_ptr<av_packet>;
using av_frame_ptr = std::shared_ptr<av_frame>;

inline bool is_key(const std::u8string_view l, const std::u8string_view r)
{
	return str::icmp(l, r) == 0;
}

inline bool is_key(const std::string_view l, const std::u8string_view r)
{
	return str::icmp(str::utf8_cast(l), r) == 0;
}

class file_type;
using file_type_by_extension = df::hash_map<std::u8string_view, file_type_ref, df::ihash, df::ieq>;
void av_initialise(file_type_by_extension& file_types);

struct av_frame_d3d
{
	int width = 0;
	int height = 0;
	AVHWFramesContext* ctx = nullptr;
	ID3D11Texture2D* tex = nullptr;
	uintptr_t tex_index = 0;
	ui::orientation orientation = ui::orientation::top_left;
};

av_frame_d3d av_get_d3d_info(const av_frame_ptr& frame_in);
double av_time_from_frame(const av_frame_ptr& f);
int av_seek_gen_from_frame(const av_frame_ptr& f);
bool av_frame_is_eof(const av_frame_ptr& f);
bool av_is_frame_empty(const av_frame_ptr& f);

struct av_rational
{
	int num = 0;
	int den = 0;
};


struct video_info_t
{
	sizei display_dimensions;
	sizei render_dimensions;
	sizei apsect_ratio;
	AVPixelFormat format = {};
	bool is_yuv = false;
};


class audio_resampler
{
	SwrContext* _aud_resampler = nullptr;
	audio_info_t _frame_info;
	audio_info_t _stream_info;

public:
	audio_resampler(const audio_info_t& info);
	~audio_resampler();

	void flush();
	void resample(const av_frame_ptr& frame, audio_buffer& audio_buffer);
};

class av_scaler
{
	SwsContext* _scaler = nullptr;
public:
	~av_scaler();

	bool scale_surface(const ui::const_surface_ptr& surface_in, ui::surface_ptr& surface_out, sizei dimensions_out);
	bool scale_surface(const av_frame_ptr& frame, ui::surface_ptr& surface_out);
	bool scale_frame(const AVFrame& frame, ui::surface_ptr& surface, sizei max_dim, double time,
	                 ui::orientation orientation);
};


template <typename T>
class av_queue final : public df::no_copy
{
private:
	mutable platform::mutex _mutex;
	_Guarded_by_(_mutex) std::deque<std::shared_ptr<T>> _q;

public:
	av_queue()
	{
	}

	~av_queue()
	{
		clear();
	}

	void clear()
	{
		platform::exclusive_lock lock(_mutex);
		_q.clear();
	}

	template <typename T>
	void push(T&& packet)
	{
		platform::exclusive_lock lock(_mutex);
		_q.emplace_back(std::forward<T>(packet));
	}

	double front_time()
	{
		platform::shared_lock lock(_mutex);
		if (_q.empty() || _q.front() == nullptr) return 0.0;
		return av_time_from_frame(_q.front());
	}

	double back_time()
	{
		platform::shared_lock lock(_mutex);
		if (_q.empty() || _q.back() == nullptr) return 0.0;
		return av_time_from_frame(_q.back());
	}

	bool pop(std::shared_ptr<T>& result)
	{
		platform::exclusive_lock lock(_mutex);
		if (_q.empty()) return false;
		result = std::move(_q.front());
		_q.pop_front();
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
		return _q.size() < 16;
	}

	size_t size() const
	{
		platform::shared_lock lock(_mutex);
		return _q.size();
	};
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
	std::u8string title;
	std::u8string codec;
	std::u8string fourcc;
	std::u8string language;
	std::u8string pixel_format;
	int audio_sample_rate = 0;
	int audio_channels = 0;
	prop::audio_sample_t audio_sample_type = prop::audio_sample_t::none;
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
	icc
};

class av_media_info
{
public:
	std::vector<av_stream_info> streams;
	std::vector<std::pair<metadata_standard, metadata_kv_list>> metadata;

	str::cached pixel_format;
	str::cached video_codec;
	str::cached audio_codec;
	int audio_sample_rate = 0;
	int audio_channels = 0;
	prop::audio_sample_t audio_sample_type = prop::audio_sample_t::none;

	int64_t bitrate = 0;
	bool has_audio = false;
	bool has_video = false;
	bool has_multiple_audio_streams = false;
	double start = 0;
	double end = 0;

	sizei display_dimensions;
	sizei render_dimensions;
	ui::orientation display_orientation = ui::orientation::top_left;

	/*bool is_empty() const
	{
		return streams.empty();
	}

	void clear()
	{
		bitrate = 0;
		has_audio = false;
		has_video = false;
		start = 0;
		end = 0;
		display_dimensions.cx = 0;
		display_dimensions.cy = 0;
		render_dimensions.cx = 0;
		render_dimensions.cy = 0;
		pixel_format.clear();
		video_codec.clear();
		audio_codec.clear();
		audio_sample_rate = 0;
		audio_channels = 0;
		audio_sample_type = prop::audio_sample_t::none;

		streams.clear();
		metadata.clear();
	}*/
};

struct av_pts_correction
{
	int64_t num_faulty_pts; /// Number of incorrect PTS values so far
	int64_t num_faulty_dts; /// Number of incorrect DTS values so far
	int64_t last_pts; /// PTS of the last frame
	int64_t last_dts; /// DTS of the last frame

	av_pts_correction();
	void clear();
	int64_t guess(int64_t pts, int64_t dts);
};


class av_format_decoder : public df::no_copy
{
private:
	AVFormatContext* _format_context = nullptr;
	df::file_path _path;
	df::file_size _file_size;
	mutable bool _eof = false;

	av_pts_correction _pts_vid;
	av_pts_correction _pts_aud;
	platform::file_ptr _file;


	friend class av_format_decoder;

public:
	av_format_decoder() = default;
	~av_format_decoder() { close(); }

	std::unique_ptr<audio_resampler> make_audio_resampler() const;

	const df::file_path path() const
	{
		return _path;
	}

	bool open(df::file_path path);
	bool open(const platform::file_ptr& file, df::file_path path);
	void init_streams(int video_track, int audio_track, bool can_use_hw, bool video_only);

	av_packet_ptr read_packet() const;
	bool seek(double wanted, double current) const;
	void close();
	void extract_metadata(file_scan_result& sr) const;

	int64_t bitrate() const;
	const AVStream* Stream(uint32_t i) const;


private:
	std::unique_ptr<av_scaler> _scaler;

	AVCodecContext* _video_context = nullptr;
	AVCodecContext* _audio_context = nullptr;

	AVBufferRef* _hw_device_ctx = nullptr;

	bool _has_video = false;
	bool _has_audio = false;
	bool _is_open = false;
	bool _has_multiple_audio_streams = false;

	int _video_stream_index = -1; // _pVideoStream->index
	int _audio_stream_index = -1; // _pVideoStream->index
	int64_t _bitrate = 0;

	std::vector<av_stream_info> _streams;

	double _start_time = 0;
	double _end_time = 0;
	int _rotation = 0;

	av_rational _video_base;
	av_rational _audio_base;
	av_rational _video_stream_aspect_ratio;

	int64_t _video_start_time = 0;
	int64_t _audio_start_time = 0;

	void update_orientation(AVFrame* frame);
	ui::orientation calc_orientation() const;

	bool decode_frame(ui::surface_ptr& dest_surface, AVCodecContext* ctx, const av_packet_ptr& packet,
	                  double audio_time, sizei max_dim);

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

	bool is_audio_only() const
	{
		return _has_audio && !_has_video;
	}

	bool is_video_only() const
	{
		return !_has_audio && _has_video;
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
	                        double pos_denominator = 100);
	bool extract_thumbnail(ui::surface_ptr& dest_surface, sizei max_dim, double pos_numerator = 10,
	                       double pos_denominator = 100);
	file_load_result render_frame(const av_frame_ptr& frame_in) const;
	void receive_frames(av_packet_queue& packets, av_frame_queue& frames);

	void flush_audio() const;
	void flush_video() const;

	av_media_info info() const;

	double start_time() const { return _start_time; };
	double end_time() const { return _end_time; };
	double to_audio_seconds(int64_t vt) const;
	double to_video_seconds(int64_t vt) const;
};
