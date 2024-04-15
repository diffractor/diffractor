// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "pch.h"
#include "av_format.h"
#include "av_player.h"
#include "metadata_xmp.h"
#include "files.h"

#include <excpt.h>

#define __restrict__
#define __STDC_CONSTANT_MACROS
#define FF_API_PIX_FMT 0

extern "C" {
#include "libavformat/avformat.h"
#include "libavutil/display.h"
#include "libavutil/eval.h"
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libavcodec/avcodec.h"
#include "libavutil/frame.h"
}

static_assert(std::is_move_constructible_v<av_stream_info>);

////////////////////////////////////////////////
////////////////////////////////////////////////
////////////////////////////////////////////////
////////////////////////////////////////////////

static void av_log(void*, int level, const char* format, va_list argList)
{
#ifdef _DEBUG
	if (level <= AV_LOG_WARNING)
	{
		if (strstr(format, "%td") == nullptr && strstr(format, "%ti") == nullptr) // Dont handle '%td'
		{
			const auto length = _vscprintf(format, argList);
			std::string result(length + 1u, 0);
			vsprintf_s(result.data(), length + 1u, format, argList);
			platform::trace(result);
		}
	}
#endif
}

void av_initialise(file_type_by_extension& file_types)
{
	//#ifdef _DEBUG
	av_log_set_level(AV_LOG_WARNING);
	av_log_set_callback(av_log);
	//#endif


}

static double calc_duration(int64_t t, const AVRational& base, const int64_t start)
{
	if (t == AV_NOPTS_VALUE) t = 0;
	if (start != AV_NOPTS_VALUE) t -= start;
	return (t * base.num) / static_cast<double>(base.den);
}

static double calc_duration(int64_t t, const int64_t& start)
{
	if (t == AV_NOPTS_VALUE) t = 0;
	if (start != AV_NOPTS_VALUE) t -= start;
	return (t) / static_cast<double>(AV_TIME_BASE);
}


static int hex_char_to_int(char8_t byte)
{
	if (byte >= '0' && byte <= '9') return byte - '0';
	if (byte >= 'a' && byte <= 'f') return byte - 'a' + 10;
	if (byte >= 'A' && byte <= 'F') return byte - 'A' + 10;
	return 0;
}

static df::blob unescape_xmp(const char* sz)
{
	std::u8string result;

	const auto len = strlen(sz);
	result.reserve(len);

	for (auto i = 0u; i < len; i++)
	{
		auto c = sz[i];
		if (c == '\\')
		{
			if ((i + 3) < len && sz[i + 1] == 'x')
			{
				c = (hex_char_to_int(sz[i + 2]) << 4) |
					hex_char_to_int(sz[i + 3]);
				i += 3;
			}
		}
		result += c;
	}

	return {result.data(), result.data() + result.size()};
}

double get_rotation(AVStream* st)
{
	const AVDictionaryEntry* rotate_tag = av_dict_get(st->metadata, "rotate", nullptr, 0);
	//uint8_t* displaymatrix = av_stream_get_side_data(st,
	//                                                 AV_PKT_DATA_DISPLAYMATRIX, nullptr);

	const AVPacketSideData* side_data = av_packet_side_data_get(st->codecpar->coded_side_data,
		st->codecpar->nb_coded_side_data,
		AV_PKT_DATA_DISPLAYMATRIX);

	double theta = 0.0;

	if (side_data)
	{
		const auto *display_matrix = reinterpret_cast<const int32_t*>(side_data->data);

		if (rotate_tag && *rotate_tag->value && strcmp(rotate_tag->value, "0"))
		{
			char* tail;
			theta = av_strtod(rotate_tag->value, &tail);
			if (*tail)
				theta = 0.0;
		}
		if (display_matrix && abs(theta) > 0.0)
		{
			theta = -av_display_rotation_get(std::bit_cast<int32_t*>(display_matrix));
		}

		theta -= 360 * floor(theta / 360 + 0.9 / 360);
	}

	return theta;
}

static void populate_properties(AVFormatContext* ctx, file_scan_result& result)
{
	if (ctx)
	{
		result.nb_streams = ctx->nb_streams;
		result.duration = calc_duration(ctx->duration, ctx->start_time);


		const AVDictionaryEntry* tag = nullptr;

		while ((tag = av_dict_get(ctx->metadata, "", tag, AV_DICT_IGNORE_SUFFIX)))
		{
			if (str::icmp(tag->key, "xmp"sv) == 0 || str::icmp(tag->key, "id3v2_priv.XMP"sv) == 0)
			{
				result.metadata.xmp = unescape_xmp(tag->value);
			}
			else
			{
				result.ffmpeg_metadata.emplace_back(str::cache(tag->key), str::utf8_cast(tag->value));
			}
		}
	}
}

static AVSampleFormat to_AVSampleFormat(const prop::audio_sample_t sample_fmt)
{
	switch (sample_fmt)
	{
	case prop::audio_sample_t::none: return AV_SAMPLE_FMT_NONE;
	case prop::audio_sample_t::unsigned_8bit: return AV_SAMPLE_FMT_U8;
	case prop::audio_sample_t::signed_16bit: return AV_SAMPLE_FMT_S16;
	case prop::audio_sample_t::signed_32bit: return AV_SAMPLE_FMT_S32;
	case prop::audio_sample_t::signed_64bit: return AV_SAMPLE_FMT_S64;
	case prop::audio_sample_t::signed_float: return AV_SAMPLE_FMT_FLT;
	case prop::audio_sample_t::signed_double: return AV_SAMPLE_FMT_DBL;
	case prop::audio_sample_t::unsigned_planar_8bit: return AV_SAMPLE_FMT_U8P;
	case prop::audio_sample_t::signed_planar_16bit: return AV_SAMPLE_FMT_S16P;
	case prop::audio_sample_t::signed_planar_32bit: return AV_SAMPLE_FMT_S32P;
	case prop::audio_sample_t::signed_planar_64bit: return AV_SAMPLE_FMT_S64P;
	case prop::audio_sample_t::planar_float: return AV_SAMPLE_FMT_FLTP;
	case prop::audio_sample_t::planar_double: return AV_SAMPLE_FMT_DBLP;
	default: ;
	}

	return AV_SAMPLE_FMT_NONE;
}

static prop::audio_sample_t to_sample_type(const AVSampleFormat format)
{
	switch (format)
	{
	case AV_SAMPLE_FMT_U8: return prop::audio_sample_t::unsigned_8bit;
	case AV_SAMPLE_FMT_S16: return prop::audio_sample_t::signed_16bit;
	case AV_SAMPLE_FMT_S32: return prop::audio_sample_t::signed_32bit;
	case AV_SAMPLE_FMT_FLT: return prop::audio_sample_t::signed_float;
	case AV_SAMPLE_FMT_DBL: return prop::audio_sample_t::signed_double;
	case AV_SAMPLE_FMT_U8P: return prop::audio_sample_t::unsigned_planar_8bit;
	case AV_SAMPLE_FMT_S16P: return prop::audio_sample_t::signed_planar_16bit;
	case AV_SAMPLE_FMT_S32P: return prop::audio_sample_t::signed_planar_32bit;
	case AV_SAMPLE_FMT_FLTP: return prop::audio_sample_t::planar_float;
	case AV_SAMPLE_FMT_DBLP: return prop::audio_sample_t::planar_double;
	case AV_SAMPLE_FMT_S64: return prop::audio_sample_t::signed_64bit;
	case AV_SAMPLE_FMT_S64P: return prop::audio_sample_t::signed_planar_64bit;
	default:
	case AV_SAMPLE_FMT_NONE:
		break;
	}

	return prop::audio_sample_t::none;
}

static ui::orientation calc_orientation_impl(const int rr)
{
	if (rr == 90) return ui::orientation::right_top;
	if (rr == 180) return ui::orientation::bottom_right;
	if (rr == 270) return ui::orientation::left_bottom;
	return ui::orientation::top_left;
}

static void populate_properties(AVStream* s, file_scan_result& result)
{
	auto* codec = s->codecpar;

	if (codec)
	{
		if (codec->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			auto* const found = avcodec_find_decoder(codec->codec_id);
			if (found)result.video_codec = str::cache(found->name);

			result.width = codec->width;
			result.height = codec->height;

			if (codec->format != AV_PIX_FMT_NONE)
			{
				const auto* const desc = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(codec->format));

				if (desc)
				{
					result.pixel_format = str::cache(desc->name);
				}
			}
		}

		if (codec->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			auto* const found = avcodec_find_decoder(codec->codec_id);
			if (found) result.audio_codec = str::cache(found->name);

			result.audio_sample_rate = codec->sample_rate;
			result.audio_channels = codec->ch_layout.nb_channels;
			result.audio_sample_type = to_sample_type(static_cast<AVSampleFormat>(codec->format));
		}

		result.orientation = calc_orientation_impl(df::round(get_rotation(s)));
	}
}

int try_avcodec_send_packet(AVCodecContext* avctx, const AVPacket* avpkt)
{
	const bool is_hw = avctx->hw_device_ctx != nullptr;

	__try
	{
		return avcodec_send_packet(avctx, avpkt);
	}
	__except (is_hw ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
	{
		setting.use_d3d11va = false;
	}

	return AVERROR(EINVAL);
}

////////////////////////////////////////////////
////////////////////////////////////////////////
////////////////////////////////////////////////
////////////////////////////////////////////////

class av_frame
{
public:
	double time = 0.0;
	int gen = 0;
	ui::orientation orientation = ui::orientation::top_left;
	bool eof = false;
	AVFrame frm;

	bool operator<(const av_frame& other) const
	{
		return (gen == other.gen) ? time < other.time : gen < other.gen;
	}

	av_frame() noexcept
	{
		memset(&frm, 0, sizeof(frm));
		//av_init_frame(&avpkt);
	}

	av_frame(av_frame&& other) noexcept
	{
		memset(&frm, 0, sizeof(frm));
		//av_init_frame(&avpkt);
		av_frame_move_ref(&frm, &other.frm);
		gen = other.gen;
		time = other.time;
		orientation = other.orientation;
		eof = other.eof;
	}

	av_frame(const av_frame& other) noexcept
	{
		memset(&frm, 0, sizeof(frm));
		//av_init_frame(&avpkt);
		av_frame_ref(&frm, &other.frm);
		gen = other.gen;
		time = other.time;
		orientation = other.orientation;
		eof = other.eof;
	}

	const av_frame& operator=(const av_frame& other) noexcept
	{
		av_frame_unref(&frm);
		av_frame_ref(&frm, &other.frm);
		gen = other.gen;
		time = other.time;
		orientation = other.orientation;
		eof = other.eof;
		return *this;
	}

	const av_frame& operator=(av_frame&& other) noexcept
	{
		av_frame_unref(&frm);
		av_frame_move_ref(&frm, &other.frm);
		gen = other.gen;
		time = other.time;
		orientation = other.orientation;
		eof = other.eof;
		return *this;
	}

	~av_frame()
	{
		av_frame_unref(&frm);
	}

	void unref()
	{
		av_frame_unref(&frm);
		memset(&frm, 0, sizeof(frm));
		time = 0.0;
	}

	AVPixelFormat pix_fmt() const
	{
		return static_cast<AVPixelFormat>(frm.format);
	}

	bool is_yuv() const
	{
		const auto f = pix_fmt();
		return f == AV_PIX_FMT_YUV420P || f == AV_PIX_FMT_YUVJ420P;
	}

	bool is_empty() const
	{
		return frm.width == 0 || frm.height == 0 || frm.data[0] == nullptr;
	}
};

class av_packet
{
public:
	AVPacket* pkt = nullptr;
	int seek_ver = 0;
	bool eof = false;

	av_packet() noexcept : pkt(av_packet_alloc())
	{
	}

	av_packet(av_packet&& other) noexcept : pkt(av_packet_alloc()), seek_ver(other.seek_ver), eof(other.eof)
	{
		av_packet_move_ref(pkt, other.pkt);
	}

	av_packet(const av_packet& other) noexcept : pkt(av_packet_alloc()), seek_ver(other.seek_ver), eof(other.eof)
	{
		av_packet_ref(pkt, other.pkt);
	}

	const av_packet& operator=(const av_packet& other) noexcept
	{
		av_packet_unref(pkt);
		av_packet_ref(pkt, other.pkt);
		seek_ver = other.seek_ver;
		eof = other.eof;
		return *this;
	}

	const av_packet& operator=(av_packet&& other) noexcept
	{
		av_packet_unref(pkt);
		av_packet_move_ref(pkt, other.pkt);
		seek_ver = other.seek_ver;
		eof = other.eof;
		return *this;
	}

	~av_packet() noexcept
	{
		av_packet_unref(pkt);
		av_packet_free(&pkt);
	}

	void copy(const AVPacket* src_avpkt)
	{
		av_packet_ref(pkt, src_avpkt);
	}

	void move(AVPacket* src_avpkt)
	{
		av_packet_move_ref(pkt, src_avpkt);
	}

	bool is_empty() const
	{
		return pkt->data == nullptr;
	};
};


av_pts_correction::av_pts_correction()
{
	clear();
}

void av_pts_correction::clear()
{
	num_faulty_pts = num_faulty_dts = 0;
	last_pts = last_dts = AV_NOPTS_VALUE;
}

int64_t av_pts_correction::guess(int64_t pts, int64_t dts)
{
	auto result = AV_NOPTS_VALUE;

	if (dts != AV_NOPTS_VALUE)
	{
		if (dts <= last_dts) num_faulty_dts += 1;
		last_dts = dts;
	}

	if (pts != AV_NOPTS_VALUE)
	{
		if (pts <= last_pts) num_faulty_pts += 1;
		last_pts = pts;
	}

	if ((num_faulty_pts <= num_faulty_dts || dts == AV_NOPTS_VALUE) && pts != AV_NOPTS_VALUE)
	{
		result = pts;
	}
	else if (dts != AV_NOPTS_VALUE)
	{
		result = dts;
	}
	else if (last_pts != AV_NOPTS_VALUE)
	{
		result = last_pts;
	}
	else if (last_dts != AV_NOPTS_VALUE)
	{
		result = last_dts;
	}

	return result;
}

///////////////////////////////////////////////////
////////////////////////////////////////////////
////////////////////////////////////////////////
////////////////////////////////////////////////

av_frame_d3d av_get_d3d_info(const av_frame_ptr& frame_in)
{
	av_frame_d3d result;
	result.width = frame_in->frm.width;
	result.height = frame_in->frm.height;
	result.orientation = frame_in->orientation;

	if (frame_in->frm.format == AV_PIX_FMT_D3D11)
	{
		result.ctx = std::bit_cast<AVHWFramesContext*>(frame_in->frm.hw_frames_ctx->data);
		result.tex = std::bit_cast<ID3D11Texture2D*>(frame_in->frm.data[0]);
		result.tex_index = std::bit_cast<uintptr_t>(frame_in->frm.data[1]);
	}

	return result;
}

double av_time_from_frame(const av_frame_ptr& f)
{
	return f ? f->time : 0.0;
}

int av_seek_gen_from_frame(const av_frame_ptr& f)
{
	return f ? f->gen : -1;
}

bool av_frame_is_eof(const av_frame_ptr& f)
{
	return f ? f->eof : false;
}

bool av_is_frame_empty(const av_frame_ptr& f)
{
	return f ? f->is_empty() : false;
}

static bool is_yuv_format(AVPixelFormat f)
{
	return f == AV_PIX_FMT_YUV420P || f == AV_PIX_FMT_YUVJ420P;
}

video_info_t av_format_decoder::video_information() const
{
	video_info_t result;

	if (_video_context)
	{
		auto ar = _video_context->sample_aspect_ratio;

		if (ar.num == 0 || ar.den == 0 || ar.den == ar.num)
		{
			ar = {_video_stream_aspect_ratio.num, _video_stream_aspect_ratio.den};
		}

		if (ar.num != 0 && ar.den != 0)
		{
			result.apsect_ratio = {ar.num, ar.den};
		}

		if (ar.num == 0 || ar.den == 0 || ar.den == ar.num)
		{
			result.display_dimensions = {_video_context->width, _video_context->height};
		}
		else
		{
			const auto width = static_cast<int64_t>(_video_context->width);
			const auto height = df::mul_div(width, ar.den * static_cast<int64_t>(_video_context->height),
			                                ar.num * width);
			result.display_dimensions = {static_cast<int>(width), static_cast<int>(height)};
		}

		result.render_dimensions = {_video_context->width, _video_context->height};
		result.format = _video_context->pix_fmt;
		result.is_yuv = is_yuv_format(_video_context->pix_fmt);
	}

	return result;
}

channel_layout_ptr av_get_def_channel_layout(const int num_channels)
{
	auto dst = std::make_shared<AVChannelLayout>();
	av_channel_layout_default(dst.get(), num_channels);
	return dst;
}

static channel_layout_ptr av_copy_to_ptr(const AVChannelLayout& src)
{
	auto dst = std::make_shared<AVChannelLayout>();
	av_channel_layout_copy(dst.get(), &src);	
	return dst;
}

audio_info_t av_format_decoder::audio_info() const
{
	audio_info_t result;

	if (_audio_context)
	{
		result.channel_layout = av_copy_to_ptr(_audio_context->ch_layout);
		result.sample_rate = _audio_context->sample_rate;
		result.sample_fmt = to_sample_type(_audio_context->sample_fmt);
	}

	return result;
};

static int av_read(void* opaque, uint8_t* buf, int buf_size)
{
	df::assert_true(buf_size != 0);

	const auto* const h = static_cast<platform::file*>(opaque);
	const auto read = static_cast<int>(h->read(buf, buf_size));
	if (read < 1) return AVERROR_EOF;
	return read;
}

static int64_t av_seek(void* opaque, int64_t offset, int whence)
{
	const auto* const h = static_cast<platform::file*>(opaque);
	int64_t result = 0;

	if (AVSEEK_SIZE == whence)
	{
		result = static_cast<int64_t>(h->size());
	}
	else if (whence == SEEK_SET)
	{
		result = static_cast<int64_t>(h->seek(offset, platform::file::whence::begin));
	}
	else if (whence == SEEK_CUR)
	{
		result = static_cast<int64_t>(h->seek(offset, platform::file::whence::current));
	}
	else if (whence == SEEK_END)
	{
		result = static_cast<int64_t>(h->seek(offset, platform::file::whence::end));
	}
	else
	{
		df::assert_true(false);
	}

	return result;
}

static int get_stream_type(AVFormatContext* ctx, int stream_num)
{
	if (stream_num >= 0 && stream_num < static_cast<int>(ctx->nb_streams))
	{
		const auto* const stream = ctx->streams[stream_num];

		if (stream && stream->codecpar)
		{
			return stream->codecpar->codec_type;
		}
	}

	return AVMEDIA_TYPE_UNKNOWN;
}

bool av_format_decoder::seek(const double wanted, const double current) const
{
	auto success = false;

	auto* const fc = _format_context;

	if (fc)
	{
		df::trace(str::format(u8"av_format_decoder::seek {}"sv, wanted));

		const auto target = static_cast<int64_t>(wanted * AV_TIME_BASE);
		const auto target_min = static_cast<int64_t>(std::max(_start_time, wanted - 1.0) * AV_TIME_BASE);
		const auto target_max = static_cast<int64_t>(std::min(_end_time, wanted + 1.0) * AV_TIME_BASE);
		const auto back = wanted < current;
		auto flags = back ? AVSEEK_FLAG_BACKWARD : 0;

		//auto ret = av_seek_frame(fc, -1, target, flags);
		auto ret = avformat_seek_file(fc, -1, target_min, target, target_max, flags);

		//df::log(__FUNCTION__, "av_format_decoder.seek " , wanted);

		if (ret < 0 && (flags & AVSEEK_FLAG_BACKWARD))
		{
			flags &= ~AVSEEK_FLAG_BACKWARD;
			//ret = av_seek_frame(fc, -1, target, flags);
			ret = avformat_seek_file(fc, -1, target_min, target, target_max, flags);
		}

		_eof = ret == AVERROR_EOF;
		success = ret >= 0;

		if (_eof)
		{
			df::trace("av_format_decoder:seek end of stream"sv);
		}

		if (success)
		{
			// Breaks MP3 seeking
			//avformat_flush(fc);
		}
	}
	return success;
}

av_packet_ptr av_format_decoder::read_packet() const
{
	av_packet_ptr result;

	auto* raw_packet = av_packet_alloc();
	auto* const fc = _format_context;

	if (fc && !_eof)
	{
		const auto ret = av_read_frame(fc, raw_packet);

		if (ret == AVERROR_EOF)
		{
			_eof = true;
			result = std::make_shared<av_packet>();
			result->eof = true;
			df::trace("av_format_decoder:read_packet end of stream"sv);
		}
		else if (0 == ret)
		{
			result = std::make_shared<av_packet>();
			result->move(raw_packet);
			av_packet_unref(raw_packet);
		}
	}

	av_packet_free(&raw_packet);
	return result;
}

void av_format_decoder::extract_metadata(file_scan_result& sr) const
{
	auto* const fc = _format_context;

	if (fc)
	{
		populate_properties(fc, sr);

		for (int i = 0; i < static_cast<int>(fc->nb_streams); ++i)
		{
			auto* const stream = fc->streams[i];

			if (stream && stream->codecpar)
			{
				/*AVDictionaryEntry* tag = nullptr;

				while (stream->metadata && (tag = av_dict_get(stream->metadata, {}, tag, AV_DICT_IGNORE_SUFFIX)))
				{
					if (is_key(tag->key, "title"sv)) md.store(prop::title, str::cache(tag->value));
				}*/

				const auto ct = stream->codecpar->codec_type;

				if (ct == AVMEDIA_TYPE_VIDEO ||
					ct == AVMEDIA_TYPE_AUDIO)
				{
					populate_properties(stream, sr);
				}
			}
		}

		const auto bit_rate = fc->bit_rate;

		if (bit_rate > 0)
		{
			sr.bitrate = str::cache(prop::format_bit_rate(bit_rate));
		}

		if (fc->duration != AV_NOPTS_VALUE)
		{
			sr.duration = df::round(calc_duration(fc->duration, fc->start_time));
		}

		sr.orientation = calc_orientation();
	}
}

int64_t av_format_decoder::bitrate() const
{
	const auto* const fc = _format_context;

	if (fc)
	{
		return fc->bit_rate;
	}

	return 0;
}

const AVStream* av_format_decoder::Stream(uint32_t i) const
{
	const auto* const fc = _format_context;

	if (fc && fc->nb_streams > i)
	{
		return fc->streams[i];
	}

	return nullptr;
}


void av_format_decoder::close()
{
	const av_packet emty_packet;

	_is_open = false;

	if (_video_context)
	{
		try_avcodec_send_packet(_video_context, emty_packet.pkt);
	}

	if (_audio_context)
	{
		try_avcodec_send_packet(_audio_context, emty_packet.pkt);
	}

	if (_video_context)
	{
		avcodec_free_context(&_video_context);
		_video_context = nullptr;
	}

	if (_audio_context)
	{
		avcodec_free_context(&_audio_context);
		_audio_context = nullptr;
	}

	if (_hw_device_ctx)
	{
		av_buffer_unref(&_hw_device_ctx);
		_hw_device_ctx = nullptr;
	}

	_pts_vid.clear();
	_pts_aud.clear();

	AVFormatContext* fc = nullptr;
	std::swap(fc, _format_context);

	if (fc)
	{
		av_freep(&fc->pb->buffer);
		//avio_context_free(&fc->pb);
		avformat_close_input(&fc);
	}

	_scaler.reset();
	_file.reset();
	_path.clear();
}


///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<audio_resampler> av_format_decoder::make_audio_resampler() const
{
	return std::make_unique<audio_resampler>(audio_info());
}

bool av_format_decoder::open(const df::file_path path)
{
	const auto file = open_file(path, platform::file_open_mode::read);

	if (!file)
	{
		return false;
	}

	df::trace(format(u8"av_format_decoder::open {}"sv, path.name()));

	return open(file, path);
}

bool av_format_decoder::open(const platform::file_ptr& file, const df::file_path path)
{
	close();

	static constexpr int io_buffer_size = df::two_fifty_six_k;
	auto* const io_buffer = static_cast<uint8_t*>(av_mallocz(io_buffer_size + 16));

	auto* fc = avformat_alloc_context();
	fc->pb = avio_alloc_context(io_buffer, io_buffer_size, 0, file.get(), av_read, nullptr, av_seek);
	fc->flags |= AVFMT_FLAG_GENPTS;

	/*if (thumbnail) // slow for gopro videos
	{
		fc->flags |= AVFMT_FLAG_NOBUFFER;
	}*/

	AVDictionary* opts = nullptr;
	av_dict_set_int(&opts, "export_xmp", 1, 0);
	// av_dict_set_int(&opts, "analyzeduration"sv, 100, 0);
	// av_dict_set_int(&opts, "probesize"sv, 10000000, 0);
	// Consider increasing the value for the 'analyzeduration' (0) and 'probesize' (5000000) options

	if (avformat_open_input(&fc, str::utf8_to_a(path.str()).c_str(), nullptr, &opts) != 0)
	{
		avformat_close_input(&fc);
		return false;
	}

	_format_context = fc;
	_path = path;
	_file_size = file->size();
	_file = file;
	_bitrate = bitrate();

	avformat_find_stream_info(fc, nullptr);

	auto audio_stream_count = 0;

	for (int i = 0; i < static_cast<int>(fc->nb_streams); ++i)
	{
		auto* const stream = fc->streams[i];

		if (stream)
		{
			AVDictionaryEntry* tag = nullptr;
			av_stream_info s;
			s.index = i;

			auto* const codec = stream->codecpar;

			while (stream->metadata && (tag = av_dict_get(stream->metadata, "", tag, AV_DICT_IGNORE_SUFFIX)))
			{
				if (is_key(tag->key, u8"title"sv)) s.title = str::utf8_cast(tag->value);
				if (is_key(tag->key, u8"codec"sv)) s.codec = str::utf8_cast(tag->value);
				if (is_key(tag->key, u8"FourCC"sv)) s.fourcc = str::utf8_cast(tag->value);
				if (is_key(tag->key, u8"language"sv)) s.language = str::utf8_cast(tag->value);
				s.metadata.emplace_back(str::cache(tag->key), str::utf8_cast(tag->value));
			}

			s.rotation = df::round(get_rotation(stream));


			if (codec)
			{
				if (s.fourcc.empty() && stream->codecpar->codec_tag)
				{
					char name[AV_FOURCC_MAX_STRING_SIZE];
					s.fourcc = str::utf8_cast(av_fourcc_make_string(name, stream->codecpar->codec_tag));
				}

				if (codec->codec_type == AVMEDIA_TYPE_VIDEO)
				{
					if (codec->format != AV_PIX_FMT_NONE)
					{
						const auto* const desc = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(codec->format));

						if (desc)
						{
							s.pixel_format = str::cache(desc->name);
						}
					}
				}

				if (codec->codec_type == AVMEDIA_TYPE_AUDIO)
				{
					s.audio_sample_rate = codec->sample_rate;
					s.audio_channels = codec->ch_layout.nb_channels;
					s.audio_sample_type = to_sample_type(static_cast<AVSampleFormat>(codec->format));
				}
				s.metadata.emplace_back(u8"codec"_c,
				                        std::u8string(str::utf8_cast(avcodec_get_name(codec->codec_id))));
				//s.metadata.emplace_back( "profile"sv, av_get_profile_name(avcodec_find_decoder(stream->codecpar->codec_id), stream->codecpar->profile) });

				if (codec->codec_tag)
				{
					char name[AV_FOURCC_MAX_STRING_SIZE];
					s.metadata.emplace_back(u8"fourcc"_c,
					                        std::u8string(
						                        str::utf8_cast(av_fourcc_make_string(name, codec->codec_tag))));
				}

				switch (codec->codec_type)
				{
				case AVMEDIA_TYPE_SUBTITLE:
					s.type = av_stream_type::subtitle;
					break;
				case AVMEDIA_TYPE_VIDEO:
					s.type = av_stream_type::video;
					break;
				case AVMEDIA_TYPE_AUDIO:
					s.type = av_stream_type::audio;
					audio_stream_count += 1;
					break;
				case AVMEDIA_TYPE_UNKNOWN:
				case AVMEDIA_TYPE_DATA:
				case AVMEDIA_TYPE_ATTACHMENT:
				case AVMEDIA_TYPE_NB:
				default:
					s.type = av_stream_type::data;
					break;
				}
			}

			_streams.emplace_back(s);
		}
	}

	_has_multiple_audio_streams = audio_stream_count > 1;
	_is_open = true;

	return true;
}

static enum AVPixelFormat get_hw_format(AVCodecContext* ctx,
	const enum AVPixelFormat* pix_fmts)
{
	const enum AVPixelFormat* p;

	for (p = pix_fmts; *p != -1; p++) {
		if (*p == AV_PIX_FMT_D3D11)
			return *p;
	}

	fprintf(stderr, "Failed to get HW surface format.\n");
	return AV_PIX_FMT_NONE;
}



void av_format_decoder::init_streams(int video_track, int audio_track, bool can_use_hw, bool video_only)
{
	auto* const fc = _format_context;
	//str::format2(fc->url, sizeof(fc->url), "{}"sv, path.str());

#ifdef _DEBUG
	//av_dump_format(fc, 0, fc->url, 0);
#endif


	AVStream* video_stream = nullptr;
	const AVStream* audio_stream = nullptr;

	// validate stream selection
	if (get_stream_type(fc, video_track) != AVMEDIA_TYPE_VIDEO) video_track = -1;
	if (get_stream_type(fc, audio_track) != AVMEDIA_TYPE_AUDIO) audio_track = -1;

	const AVCodec* video_codec = nullptr;
	const auto video_stream_index = av_find_best_stream(fc, AVMEDIA_TYPE_VIDEO, video_track, -1, &video_codec, 0);

	if (video_stream_index >= 0)
	{
		video_stream = fc->streams[video_stream_index];

		if (video_stream && video_stream->codecpar && video_codec)
		{
			auto* const vc = avcodec_alloc_context3(video_codec);

			if (vc)
			{
				avcodec_parameters_to_context(vc, video_stream->codecpar);

				vc->workaround_bugs = FF_BUG_AUTODETECT;

				if (can_use_hw)
				{
					for (int i = 0;; i++) {

						const auto *hw_config = avcodec_get_hw_config(video_codec, i);

						if (hw_config && hw_config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)
						{
							vc->get_format = get_hw_format;

							const auto ret = av_hwdevice_ctx_create(&_hw_device_ctx, 
								hw_config->device_type,
								nullptr, nullptr, 0);

							if (ret == 0)
							{
								vc->extra_hw_frames = 16;
								vc->hw_device_ctx = av_buffer_ref(_hw_device_ctx);
								break;
							}
						}
						else
						{
							break;
						}
					}
				}

				_has_video = avcodec_open2(vc, video_codec, nullptr) == 0;
				_video_base = {video_stream->time_base.num, video_stream->time_base.den};
				_video_start_time = video_stream->start_time;
				_video_stream_index = video_stream_index;
				_video_context = vc;
				_video_stream_aspect_ratio = {
					video_stream->sample_aspect_ratio.num, video_stream->sample_aspect_ratio.den
				};
				_rotation = df::round(get_rotation(video_stream));
			}
		}
	}

	if (!video_only)
	{
		const AVCodec* aud_decoder = nullptr;
		const auto aud_stream = av_find_best_stream(fc, AVMEDIA_TYPE_AUDIO, audio_track, video_stream_index,
		                                            &aud_decoder, 0);

		if (aud_stream >= 0)
		{
			audio_stream = fc->streams[aud_stream];

			if (audio_stream && audio_stream->codecpar && aud_decoder)
			{
				auto* ac = avcodec_alloc_context3(aud_decoder);

				if (ac)
				{
					avcodec_parameters_to_context(ac, audio_stream->codecpar);
					ac->workaround_bugs = FF_BUG_AUTODETECT;
					ac->request_sample_fmt = AV_SAMPLE_FMT_S16;
					// TODO investigate if it is worth using
					// "downmix" codec private option
					//ac->request_channel_layout = AV_CH_LAYOUT_STEREO;

					_has_audio = avcodec_open2(ac, aud_decoder, nullptr) == 0;
					_audio_base = {audio_stream->time_base.num, audio_stream->time_base.den};
					_audio_start_time = audio_stream->start_time;
					_audio_stream_index = aud_stream;
					_audio_context = ac;
				}
			}
		}
	}

	for (auto&& st : _streams)
	{
		st.is_playing = st.index == _audio_stream_index || st.index == _video_stream_index;
	}

	double end_time_context = 0;
	double end_time_video = 0;
	double end_time_audio = 0;

	if (fc && fc->duration != AV_NOPTS_VALUE)
	{
		end_time_context = calc_duration(fc->duration, fc->start_time);
	}

	if (video_stream && video_stream->duration != AV_NOPTS_VALUE)
	{
		end_time_video = calc_duration(video_stream->duration, video_stream->time_base, video_stream->start_time);
	}

	if (audio_stream && audio_stream->duration != AV_NOPTS_VALUE)
	{
		end_time_audio = calc_duration(audio_stream->duration, audio_stream->time_base, audio_stream->start_time);
	}

	_start_time = 0.0; // std::min(start_time_context, start_time_video, start_time_audio);
	_end_time = _start_time + std::max(std::max(end_time_context, end_time_video), end_time_audio);
}

av_media_info av_format_decoder::info() const
{
	const auto vid_info = video_information();

	av_media_info result;
	result.streams = _streams;
	result.has_multiple_audio_streams = _has_multiple_audio_streams;
	result.has_audio = _has_audio;
	result.has_video = _has_video;
	result.bitrate = _bitrate;
	result.start = _start_time;
	result.end = _end_time;
	result.render_dimensions = vid_info.render_dimensions;
	result.display_dimensions = vid_info.display_dimensions;
	result.display_orientation = calc_orientation();

	const auto* ctx = _format_context;

	if (ctx)
	{
		metadata_kv_list kv;
		const AVDictionaryEntry* tag = nullptr;

		while ((tag = av_dict_get(ctx->metadata, "", tag, AV_DICT_IGNORE_SUFFIX)))
		{
			if (str::icmp(tag->key, "id3v2_priv.XMP"sv) == 0 || str::icmp(tag->key, "xmp"sv) == 0)
			{
				const auto xmp_kv = metadata_xmp::to_info(unescape_xmp(tag->value));
				result.metadata.emplace_back(metadata_standard::xmp, xmp_kv);
			}
			else
			{
				kv.emplace_back(str::cache(tag->key), str::utf8_cast(tag->value));
			}
		}

		result.metadata.emplace_back(metadata_standard::ffmpeg, kv);

		for (uint32_t i = 0; i < ctx->nb_streams; ++i)
		{
			auto* codec = ctx->streams[i]->codecpar;

			if (codec)
			{
				if (codec->codec_type == AVMEDIA_TYPE_VIDEO)
				{
					auto* const found = avcodec_find_decoder(codec->codec_id);
					if (found)
					{
						result.video_codec = str::cache(found->name);
					}

					if (codec->format != AV_PIX_FMT_NONE)
					{
						const auto* const desc = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(codec->format));

						if (desc)
						{
							result.pixel_format = str::cache(desc->name);
						}
					}
				}

				if (codec->codec_type == AVMEDIA_TYPE_AUDIO)
				{
					auto* const found = avcodec_find_decoder(codec->codec_id);

					if (found)
					{
						result.audio_codec = str::cache(found->name);
					}

					result.audio_sample_rate = codec->sample_rate;
					result.audio_channels = codec->ch_layout.nb_channels;
					result.audio_sample_type = to_sample_type(static_cast<AVSampleFormat>(codec->format));
				}
			}
		}
	}

	return result;
}


void av_format_decoder::flush_audio() const
{
	if (_audio_context)
	{
		avcodec_flush_buffers(_audio_context);
	}
}

void av_format_decoder::flush_video() const
{
	if (_video_context)
	{
		avcodec_flush_buffers(_video_context);
	}
}

//void av_format_decoder::clear()
//{
//	_pts.clear();
//
//	if (_video_context)
//	{
//		avcodec_flush_buffers(_video_context);
//	}
//
//	if (_audio_context)
//	{
//		avcodec_flush_buffers(_audio_context);
//	}
//}


ui::orientation av_format_decoder::calc_orientation() const
{
	return calc_orientation_impl(_rotation);
}

bool av_format_decoder::decode_frame(ui::surface_ptr& dest_surface, AVCodecContext* ctx, const av_packet_ptr& packet,
                                     double audio_time, const sizei max_dim)
{
	auto success = false;
	const auto send_res = try_avcodec_send_packet(ctx, packet->pkt);

	if (send_res == 0)
	{
		AVFrame frame;
		memset(&frame, 0, sizeof(frame));

		const auto rec_res = avcodec_receive_frame(ctx, &frame);

		if (rec_res == 0)
		{
			const auto pts = _pts_vid.guess(frame.pts, frame.pkt_dts);
			auto time = to_video_seconds(pts);

			if (frame.repeat_pict)
			{
				time += 1 / 25.0;
			}

			if (df::equiv(audio_time, 0.0) ||
				(time >= (audio_time - 0.1) && time <= (audio_time + 1.0)))
			{
				if (!_scaler) _scaler = std::make_unique<av_scaler>();
				success = _scaler->scale_frame(frame, dest_surface, max_dim, time, calc_orientation());
			}
		}

		av_frame_unref(&frame);
	}

	return success;
}


bool av_format_decoder::extract_seek_frame(ui::surface_ptr& dest_surface, const sizei max_dim, double pos_numerator,
                                           double pos_denominator)
{
	auto success = false;

	if (_has_video)
	{
		if (_has_audio)
		{
			auto* const ctx = _video_context;
			const auto start = start_time();
			const auto end = end_time();
			const auto len = end - start;
			const auto x = std::clamp(pos_numerator, 0.0, pos_denominator);

			if (x > 2.0)
			{
				const auto wanted_time = start + floor(((x * len)) / std::max(1.0, pos_denominator));

				if (!seek(wanted_time, 0))
				{
					return false;
				}
			}

			av_packet_queue video_packets;
			double audio_time = -1;
			bool has_audio_time = false;

			for (int i = 0; i < 1024 && !success; i++)
			{
				const auto packet = read_packet();

				if (packet)
				{
					if (packet->pkt->stream_index == _video_stream_index)
					{
						if (has_audio_time)
						{
							success = decode_frame(dest_surface, ctx, packet, audio_time, max_dim);
						}
						else
						{
							video_packets.push(packet);
						}
					}
					else if (packet->pkt->stream_index == _audio_stream_index)
					{
						const auto time = to_audio_seconds(packet->pkt->pts);

						if (audio_time < 0)
						{
							audio_time = time;
							has_audio_time = true;
						}
					}

					if (has_audio_time && video_packets.size() > 0)
					{
						av_packet_ptr queued_packet;

						while (!success && video_packets.pop(queued_packet))
						{
							success = decode_frame(dest_surface, ctx, queued_packet, audio_time, max_dim);
						}
					}
				}
			}
		}
		else
		{
			success = extract_thumbnail(dest_surface, max_dim, pos_numerator, pos_denominator);
		}
	}

	return success;
}

bool av_format_decoder::extract_thumbnail(ui::surface_ptr& dest_surface, const sizei max_dim, double pos_numerator,
                                          double pos_denominator)
{
	auto success = false;

	if (_has_video)
	{
		auto* const ctx = _video_context;
		const auto duration = end_time() - start_time();
		const auto time_wanted = (duration * pos_numerator) / pos_denominator;

		if (duration > 0)
		{
			auto seek_success = true;

			if (time_wanted > 2.0)
			{
				seek_success = seek(time_wanted, 0);
			}

			if (seek_success)
			{
				for (int i = 0; i < 1024 && !success && !df::is_closing; i++)
				{
					const auto packet = read_packet();

					if (packet)
					{
						if (packet->pkt->stream_index == _video_stream_index)
						{
							success = decode_frame(dest_surface, ctx, packet, 0.0, max_dim);
						}
					}
				}
			}
		}
	}

	return success;
}

double av_format_decoder::to_video_seconds(int64_t vt) const
{
	return calc_duration(vt, {_video_base.num, _video_base.den}, _video_start_time);
}

double av_format_decoder::to_audio_seconds(int64_t vt) const
{
	return calc_duration(vt, {_audio_base.num, _audio_base.den}, _audio_start_time);
}


file_load_result av_format_decoder::render_frame(const av_frame_ptr& frame_in) const
{
	file_load_result result;
	const auto* const video_codec_context = _video_context;
	const auto h = video_codec_context->height;
	const auto w = video_codec_context->width;

	AVFrame* sw_frame = nullptr;
	auto* frame = &frame_in->frm;

	if (frame->format == AV_PIX_FMT_D3D11)
	{
		sw_frame = av_frame_alloc();

		if (sw_frame && av_hwframe_transfer_data(sw_frame, frame, 0) == 0)
		{
			frame = sw_frame;
		}
	}

	const sizei src_extent = {frame->width, frame->height};
	const auto source_fmt = static_cast<AVPixelFormat>(frame->format);

	auto* const scaler = sws_getContext(src_extent.cx, src_extent.cy, source_fmt, w, h, AV_PIX_FMT_BGRA, SWS_BICUBIC,
	                                    nullptr, nullptr, nullptr);

	if (scaler)
	{
		const auto s = std::make_shared<ui::surface>();
		auto* const buffer = s->alloc(w, h, ui::texture_format::RGB, calc_orientation());
		const auto pitch = s->stride();

		uint8_t* data[4] = {buffer, nullptr, nullptr, nullptr};
		const int linesize[4] = {pitch, 0, 0, 0};

		sws_scale(scaler, frame->data, frame->linesize, 0, src_extent.cy, data, linesize);

		result.s = s;
		result.success = !result.is_empty();

		sws_freeContext(scaler);
	}

	if (sw_frame)
	{
		av_frame_free(&sw_frame);
		sw_frame = nullptr;
	}

	return result;
}


audio_resampler::audio_resampler(const audio_info_t& info) : _stream_info(info)
{
}

audio_resampler::~audio_resampler()
{
	if (_aud_resampler)
	{
		swr_close(_aud_resampler);
		swr_free(&_aud_resampler);
		_aud_resampler = nullptr;
	}
}

void audio_resampler::flush()
{
	if (_aud_resampler)
	{
		const auto buffer_len = _stream_info.bytes_per_second();
		const auto buffer = df::unique_alloc<uint8_t>(buffer_len);

		if (buffer)
		{
			uint8_t* output[AV_NUM_DATA_POINTERS]{buffer.get(), nullptr, nullptr, nullptr, nullptr, nullptr};
			swr_convert(_aud_resampler, output, buffer_len / 4u, nullptr, 0);
		}
	}
}


void audio_resampler::resample(const av_frame_ptr& frame, audio_buffer& audio_buffer)
{
	audio_info_t source_format;
	source_format.channel_layout = frame->frm.ch_layout.nb_channels == 0
		                               ? _stream_info.channel_layout
		                               : av_copy_to_ptr(frame->frm.ch_layout);

	source_format.sample_fmt = frame->frm.format == 0
		                           ? _stream_info.sample_fmt
		                           : to_sample_type(static_cast<AVSampleFormat>(frame->frm.format));

	source_format.sample_rate = frame->frm.sample_rate == 0 ? _stream_info.sample_rate : frame->frm.sample_rate;

	const auto dest_format = audio_buffer.format;
	const auto dest_sample_fmt = to_AVSampleFormat(dest_format.sample_fmt);

	if (source_format != _frame_info)
	{
		_frame_info = source_format;

		SwrContext* swr = nullptr;
		std::swap(swr, _aud_resampler);

		if (0 == swr_alloc_set_opts2(&swr,
			dest_format.channel_layout.get(),
			dest_sample_fmt,
			dest_format.sample_rate,
			source_format.channel_layout.get(),
			to_AVSampleFormat(source_format.sample_fmt),
			source_format.sample_rate,
			0,
			nullptr))
		{

			if (swr)
			{
				if (swr_init(swr) == 0)
				{
					_aud_resampler = swr;
				}
			}
		}
	}

	if (_aud_resampler)
	{
		//uint8_t* output = nullptr;

		const int in_samples = frame->frm.nb_samples;
		const auto expected_out_samples = swr_get_out_samples(_aud_resampler, in_samples);
		const auto is_planar = av_sample_fmt_is_planar(static_cast<AVSampleFormat>(frame->frm.format));
		const auto planes_expected = is_planar ? frame->frm.ch_layout.nb_channels : 1;

		bool is_valid = true;

		if (frame->frm.linesize[0] == 0 ||
			planes_expected > 6)
		{
			is_valid = false;
		}

		for (int i = 0; i < planes_expected; ++i)
		{
			if (frame->frm.data[i] == nullptr)
			{
				is_valid = false;
			}
		}

		const auto out_num_channels = dest_format.channel_count();
		const auto out_sample_size = dest_format.bytes_per_sample();

		uint8_t* buffer[AV_NUM_DATA_POINTERS]{nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
		av_samples_alloc(buffer, nullptr, out_num_channels, expected_out_samples, dest_sample_fmt, 0);

		if (buffer[0])
		{
			if (is_valid)
			{
				const auto out_samples = swr_convert(_aud_resampler, buffer, expected_out_samples,
				                                     const_cast<const uint8_t**>(frame->frm.data),
				                                     frame->frm.nb_samples);
				audio_buffer.append(buffer[0], out_samples * out_num_channels * out_sample_size, frame->time,
				                    frame->gen);
			}
			else
			{
				audio_buffer.append(buffer[0], expected_out_samples * out_num_channels * out_sample_size, frame->time,
				                    frame->gen);
			}

			av_freep(buffer);
		}
	}
}

av_scaler::~av_scaler()
{
	if (_scaler)
	{
		sws_freeContext(_scaler);
		_scaler = nullptr;
	}
}

bool av_scaler::scale_surface(const ui::const_surface_ptr& surface_in, ui::surface_ptr& surface_out,
                              sizei dimensions_out)
{
	const auto source_extent = surface_in->dimensions();
	const auto fmt = surface_in->format() == ui::texture_format::ARGB ? AV_PIX_FMT_BGRA : AV_PIX_FMT_BGRA;

	_scaler = sws_getCachedContext(_scaler, source_extent.cx, source_extent.cy, fmt, dimensions_out.cx,
	                               dimensions_out.cy, fmt, SWS_BICUBIC, nullptr, nullptr,
	                               nullptr);

	if (_scaler)
	{
		surface_out = std::make_shared<ui::surface>();
		surface_out->alloc(dimensions_out.cx, dimensions_out.cy, surface_in->format(), surface_in->orientation());

		const uint8_t* src_data[4] = {surface_in->pixels(), nullptr, nullptr, nullptr};
		const int src_stride[4] = {surface_in->stride(), 0, 0, 0};

		uint8_t* dst_data[4] = {surface_out->pixels(), nullptr, nullptr, nullptr};
		const int dst_stride[4] = {surface_out->stride(), 0, 0, 0};

		sws_scale(_scaler, src_data, src_stride, 0, source_extent.cy, dst_data, dst_stride);
	}

	return is_valid(surface_out);
}

bool av_scaler::scale_surface(const av_frame_ptr& frame_in2, ui::surface_ptr& surface_out)
{
	bool success = false;
	const AVFrame* frame = &frame_in2->frm;
	AVFrame* sw_frame = nullptr;

	if (frame->format == AV_PIX_FMT_D3D11)
	{
		sw_frame = av_frame_alloc();

		if (av_hwframe_transfer_data(sw_frame, frame, 0) == 0)
		{
			frame = sw_frame;
		}
	}

	const sizei src_extent = {static_cast<int>(frame->width), static_cast<int>(frame->height)};
	const auto source_fmt = static_cast<AVPixelFormat>(frame->format);
	const auto render_fmt = AV_PIX_FMT_BGRA;

	_scaler = sws_getCachedContext(_scaler, src_extent.cx, src_extent.cy, source_fmt,
	                               src_extent.cx, src_extent.cy, render_fmt,
	                               SWS_POINT, nullptr, nullptr, nullptr);

	if (_scaler)
	{
		if (surface_out->alloc(src_extent, ui::texture_format::RGB, frame_in2->orientation, frame_in2->time))
		{
			uint8_t* data[4] = {surface_out->pixels(), nullptr, nullptr, nullptr};
			const int linesize[4] = {surface_out->stride(), 0, 0, 0};

			const auto ret = sws_scale(_scaler, frame->data, frame->linesize, 0, src_extent.cy, data, linesize);
			success = ret > 0;
		}
	}

	if (sw_frame)
	{
		av_frame_free(&sw_frame);
		sw_frame = nullptr;
	}

	return success;
}

bool av_scaler::scale_frame(const AVFrame& frame, ui::surface_ptr& surface, const sizei max_dim, double time,
                            ui::orientation orientation)
{
	bool success = false;
	const auto fmt = static_cast<AVPixelFormat>(frame.format);
	const sizei src_dims(frame.width, frame.height);
	const auto dst_dims = ui::scale_dimensions(src_dims, max_dim);

	_scaler = sws_getCachedContext(_scaler, src_dims.cx, src_dims.cy, fmt, dst_dims.cx, dst_dims.cy,
	                               AV_PIX_FMT_BGRA, SWS_BICUBIC, nullptr, nullptr, nullptr);

	if (_scaler)
	{
		surface = std::make_shared<ui::surface>();
		surface->alloc(dst_dims.cx, dst_dims.cy, ui::texture_format::RGB, orientation, time);

		uint8_t* data[4] = {(surface->pixels()), nullptr, nullptr, nullptr};
		const int linesize[4] = {surface->stride(), 0, 0, 0};

		success = sws_scale(_scaler, frame.data, frame.linesize, 0, src_dims.cy, data, linesize) == dst_dims.cy;

		if (!success)
		{
			df::log(__FUNCTION__, "sws_scale failed"sv);
		}
	}

	return success;
}

void av_session::process_io(platform::thread_event& video_event, platform::thread_event& audio_event)
{
	const auto has_audio = _decoder.has_audio();
	const auto has_video = _decoder.has_video();
	const auto video_stream = _decoder.video_stream_id();
	const auto audio_stream = _decoder.audio_stream_id();
	auto loop_iteration = 0;

	while ((has_audio && _audio_packets.should_receive()) || (has_video && _video_packets.should_receive()))
	{
		platform::shared_lock lock(_decoder_rw);
		const auto packet = _decoder.read_packet();

		if (packet)
		{
			if (packet->eof)
			{
				_video_packets.push(packet);
				_audio_packets.push(packet);
				audio_event.set();
				video_event.set();
				break;
			}
			if (packet->pkt->stream_index == video_stream)
			{
				packet->seek_ver = _seek_gen;
				_video_packets.push(packet);
				video_event.set();
			}
			else if (packet->pkt->stream_index == audio_stream)
			{
				packet->seek_ver = _seek_gen;
				_audio_packets.push(packet);
				audio_event.set();
			}
		}
		else
		{
			break;
		}

		if (_state == av_play_state::closed || df::is_closing || ++loop_iteration > max_loop_iteration)
		{
			break;
		}
	}
}


void av_format_decoder::receive_frames(av_packet_queue& packets, av_frame_queue& frames)
{
	av_packet_ptr packet;

	if (packets.pop(packet))
	{
		AVCodecContext* c = nullptr;
		av_pts_correction* pts_c = nullptr;
		const auto seek_ver = packet->seek_ver;
		AVRational base;
		int64_t start = AV_NOPTS_VALUE;
		const auto si = packet->pkt->stream_index;
		auto stream_name = u8"unknown stream"sv;

		if (packet->eof)
		{
			auto frame = std::make_shared<av_frame>();
			frame->eof = true;
			frame->gen = seek_ver;
			frames.push(frame);
		}
		else if (si == _video_stream_index)
		{
			c = _video_context;
			pts_c = &_pts_vid;
			base = {_video_base.num, _video_base.den};
			start = _video_start_time;
			stream_name = u8"video stream"sv;
		}
		else if (si == _audio_stream_index)
		{
			c = _audio_context;
			pts_c = &_pts_aud;
			base = {_audio_base.num, _audio_base.den};
			start = _audio_start_time;
			stream_name = u8"audio stream"sv;
		}

		if (c != nullptr)
		{
			if (packet->is_empty())
			{
				frames.clear();
				pts_c->clear();

				avcodec_flush_buffers(c);
				df::trace(str::format(u8"av_format_decoder::receive_frames avcodec_flush_buffers {}"sv, stream_name));

				//try_avcodec_send_packet(c, &packet.pkt);

				//auto frame = std::make_shared<av_frame>();
				//frame->seek_ver = seek_ver;
				//frames.push(frame);
			}
			else
			{
				auto send_res = try_avcodec_send_packet(c, packet->pkt);
				//df::trace(str::format(u8"[{}] try_avcodec_send_packet 111 {}"sv, si, send_res));

				if (send_res == 0)
				{
					auto frame = std::make_shared<av_frame>();
					auto rec_res = avcodec_receive_frame(c, &frame->frm);
					//df::trace(str::format(u8"[{}] avcodec_receive_frame 111 {}"sv, si, rec_res));

					while (rec_res == 0)
					{
						frame->gen = seek_ver;
						frame->time = calc_duration(pts_c->guess(frame->frm.pts, frame->frm.pkt_dts), base, start);
						frame->orientation = calc_orientation();
						frames.push(frame);

						frame = std::make_shared<av_frame>();
						rec_res = avcodec_receive_frame(c, &frame->frm);
						//df::trace(str::format(u8"[{}] avcodec_receive_frame 222 {} {}"sv, si, rec_res, frame->time));
					}
				}

				while (AVERROR(EAGAIN) == send_res)
				{
					send_res = try_avcodec_send_packet(c, packet->pkt);
					//df::trace(str::format(u8"[{}] try_avcodec_send_packet 333 {}"sv, si, send_res));

					if (send_res == 0 || AVERROR(EAGAIN) == send_res)
					{
						auto frame = std::make_shared<av_frame>();
						auto rec_res = avcodec_receive_frame(c, &frame->frm);
						//df::trace(str::format(u8"[{}] avcodec_receive_frame 333 {}"sv, si, rec_res));

						while (rec_res == 0)
						{
							frame->gen = seek_ver;
							frame->time = calc_duration(pts_c->guess(frame->frm.pts, frame->frm.pkt_dts), base, start);
							frame->orientation = calc_orientation();
							frames.push(frame);

							frame = std::make_shared<av_frame>();
							rec_res = avcodec_receive_frame(c, &frame->frm);
							//df::trace(str::format(u8"[{}] avcodec_receive_frame 444 {} {}"sv, si, rec_res, frame->time));
						}
					}
				}
			}
		}
	}
}

void av_session::state(av_play_state new_state)
{
	if (_state != new_state)
	{
		_state = new_state;
		_host.invalidate_view(view_invalid::view_layout |
			view_invalid::screen_saver |
			view_invalid::app_layout |
			view_invalid::media_elements |
			view_invalid::command_state);
	}
}

void av_session::seek(const double pos, bool scrubbing)
{
	_scrubbing = scrubbing;

	if (fabs(_last_seek - pos) > 0.1 || pos < 0.1)
	{
		platform::shared_lock lock(_decoder_rw);

		if (_decoder.seek(pos, _last_frame_decoded))
		{
			_video_packets.clear();
			_audio_packets.clear();
			_video_frames.clear();
			_audio_frames.clear();

			if (_decoder.has_video())
			{
				auto packet = std::make_shared<av_packet>();
				packet->pkt->stream_index = _decoder._video_stream_index;
				_video_packets.push(packet);
			}

			if (_decoder.has_audio())
			{
				auto packet = std::make_shared<av_packet>();
				packet->pkt->stream_index = _decoder._audio_stream_index;
				_audio_packets.push(packet);
			}

			_seek_gen += 1;
			_pending_time_sync = true;
			_reset_time_offset = !_decoder.has_audio(); // && !scrubbing;
			_last_seek = pos;
		}
	}
}

uint32_t audio_info_t::bytes_per_second() const
{
	return bytes_per_sample() * channel_count() * sample_rate;
}

uint32_t audio_info_t::channel_count() const
{
	if (!channel_layout) return 0;
	return channel_layout->nb_channels;
}

uint32_t audio_info_t::bytes_per_sample() const
{
	return av_get_bytes_per_sample(to_AVSampleFormat(sample_fmt));
}

bool operator==(const audio_info_t& lhs, const audio_info_t& rhs)
{
	if (lhs.sample_rate != rhs.sample_rate ||
		lhs.sample_fmt != rhs.sample_fmt)
	{
		return false;
	}

	if (lhs.channel_layout == nullptr && rhs.channel_layout == nullptr)
	{
		return true;
	}

	if (lhs.channel_layout == nullptr || rhs.channel_layout == nullptr)
	{
		return false;
	}

	return av_channel_layout_compare(lhs.channel_layout.get(), rhs.channel_layout.get()) == 0;
}