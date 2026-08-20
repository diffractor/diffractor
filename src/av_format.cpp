// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: FFmpeg media decoder implementation. Provides video/audio decoding, frame scaling,
// audio resampling, metadata extraction, and hardware acceleration support.

#include "pch.h"
#include "av_format.h"
#include "av_player.h"
#include "metadata_xmp.h"
#include "files.h"

// Both of these are MSVC dialect repairs. excpt.h has no equivalent elsewhere, and __restrict__ is
// a real keyword on GCC and Clang: defining it away there would strip the qualifier FFmpeg's
// headers rely on rather than supply one MSVC lacks.
#ifdef _MSC_VER
#include <excpt.h>
#define __restrict__
#endif

#define __STDC_CONSTANT_MACROS
#define FF_API_PIX_FMT 0

extern "C" {
#include "libavformat/avformat.h"
#include "libavutil/display.h"
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "libavutil/pixdesc.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libavcodec/avcodec.h"
#include "libavutil/frame.h"
#include "libavutil/hwcontext.h"
}

df_assert_movable(av_stream_info);

////////////////////////////////////////////////
////////////////////////////////////////////////
////////////////////////////////////////////////
////////////////////////////////////////////////

static void av_log(void*, const int level, const char* format, va_list argList)
{
#ifdef _DEBUG
	if (level <= AV_LOG_WARNING)
	{
		if (strstr(format, "%td") == nullptr && strstr(format, "%ti") == nullptr) // Don't handle '%td'
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
	av_log_set_level(AV_LOG_WARNING);
	av_log_set_callback(av_log);
}

std::vector<av_codec_doc> av_supported_codecs()
{
	std::vector<av_codec_doc> result;

	const AVCodec* codec = nullptr;
	void* iter = nullptr;

	while ((codec = av_codec_iterate(&iter)) != nullptr)
	{
		if (!av_codec_is_decoder(codec))
		{
			continue;
		}

		auto media_type = av_codec_media_type::other;

		switch (codec->type)
		{
		case AVMEDIA_TYPE_VIDEO:
			media_type = av_codec_media_type::video;
			break;
		case AVMEDIA_TYPE_AUDIO:
			media_type = av_codec_media_type::audio;
			break;
		default:
			continue; // only document video and audio codecs
		}

		result.emplace_back(av_codec_doc{
			codec->name ? codec->name : "",
			codec->long_name ? codec->long_name : "",
			media_type
		});
	}

	return result;
}

static double calc_duration(int64_t t, const AVRational& base, const int64_t start)
{
	if (t == AV_NOPTS_VALUE) t = 0;
	if (start != AV_NOPTS_VALUE) t -= start;
	// Scale in double: a 64-bit timestamp times a large time-base numerator overflows.
	return static_cast<double>(t) * base.num / base.den;
}

static double calc_duration(int64_t t, const int64_t& start)
{
	if (t == AV_NOPTS_VALUE) t = 0;
	if (start != AV_NOPTS_VALUE) t -= start;
	return t / static_cast<double>(AV_TIME_BASE);
}


static int hex_char_to_int(const char byte)
{
	if (byte >= '0' && byte <= '9') return byte - '0';
	if (byte >= 'a' && byte <= 'f') return byte - 'a' + 10;
	if (byte >= 'A' && byte <= 'F') return byte - 'A' + 10;
	return 0;
}

static df::blob unescape_xmp(const char* sz)
{
	std::string result;

	const auto len = strlen(sz);
	result.reserve(len);

	for (auto i = 0u; i < len; i++)
	{
		auto c = sz[i];
		if (c == '\\')
		{
			if (i + 3 < len && sz[i + 1] == 'x')
			{
				c = hex_char_to_int(sz[i + 2]) << 4 |
					hex_char_to_int(sz[i + 3]);
				i += 3;
			}
		}
		result += c;
	}

	return {result.data(), result.data() + result.size()};
}

// Decodes a 3x3 display matrix into a clockwise rotation normalised onto [0,360).
// Shared by the container-level (stream) and frame-level side data so both map
// onto the same set of orientations.
static double rotation_from_display_matrix(const uint8_t* const data, const size_t size)
{
	if (!data || size < 9 * sizeof(int32_t))
	{
		return 0.0;
	}

	const auto theta = -av_display_rotation_get(reinterpret_cast<const int32_t*>(data));
	return theta - 360.0 * floor(theta / 360.0 + 0.9 / 360.0);
}

double get_rotation(const AVStream* const st)
{
	const AVPacketSideData* side_data = av_packet_side_data_get(st->codecpar->coded_side_data,
	                                                            st->codecpar->nb_coded_side_data,
	                                                            AV_PKT_DATA_DISPLAYMATRIX);

	return side_data ? rotation_from_display_matrix(side_data->data, side_data->size) : 0.0;
}

// Locates a DV VAUX recording-date (0x62) or recording-time (0x63) pack within a
// raw DV frame. These packs live in the three VAUX DIF blocks (block index 3, 4
// and 5) of each DIF sequence and are duplicated at two pack slots within each
// block. The offsets mirror those written by the DV muxer (libavformat/dvenc.c
// dv_inject_metadata). Returns a pointer to the 5-byte pack, or null.
static const uint8_t* dv_find_vaux_pack(const uint8_t* frame, const size_t frame_size,
                                        const uint8_t pack_id, const int off_a, const int off_b)
{
	constexpr int dif_sequence_size = 12000; // 150 DIF blocks * 80 bytes
	constexpr int vaux_blocks[] = {80 * 3, 80 * 4, 80 * 5};

	for (int seq = 0; seq < 12; ++seq)
	{
		const int seq_base = seq * dif_sequence_size;
		if (static_cast<size_t>(seq_base) >= frame_size) break;

		for (const int block : vaux_blocks)
		{
			for (const int pack : {off_a, off_b})
			{
				const int offs = seq_base + block + pack;
				if (offs >= 0 && static_cast<size_t>(offs) + 5 <= frame_size &&
					frame[offs] == pack_id)
				{
					return &frame[offs];
				}
			}
		}
	}

	return nullptr;
}

df::date_t dv_extract_rec_datetime(const uint8_t* frame, const size_t frame_size)
{
	if (!frame) return {};

	const auto* const date_pack = dv_find_vaux_pack(frame, frame_size, 0x62, 13, 58);
	if (!date_pack) return {};

	// Date pack: PC2 = day, PC3 = month, PC4 = two-digit year (all BCD).
	const int day = ((date_pack[2] >> 4) & 0x03) * 10 + (date_pack[2] & 0x0f);
	const int month = ((date_pack[3] >> 4) & 0x01) * 10 + (date_pack[3] & 0x0f);
	const int year2 = ((date_pack[4] >> 4) & 0x0f) * 10 + (date_pack[4] & 0x0f);

	if (day < 1 || day > 31 || month < 1 || month > 12 || year2 > 99) return {};

	const int year = year2 < 75 ? 2000 + year2 : 1900 + year2;

	// Time pack: PC2 = seconds, PC3 = minutes, PC4 = hours (all BCD). Optional.
	int hour = 0, minute = 0, second = 0;
	const auto* const time_pack = dv_find_vaux_pack(frame, frame_size, 0x63, 18, 63);

	if (time_pack)
	{
		second = ((time_pack[2] >> 4) & 0x07) * 10 + (time_pack[2] & 0x0f);
		minute = ((time_pack[3] >> 4) & 0x07) * 10 + (time_pack[3] & 0x0f);
		hour = ((time_pack[4] >> 4) & 0x03) * 10 + (time_pack[4] & 0x0f);

		if (hour > 23 || minute > 59 || second > 59)
		{
			hour = minute = second = 0;
		}
	}

	return {year, month, day, hour, minute, second};
}

// Reads the first full DV video frame from the demuxer and extracts its embedded
// recording date/time. Used for DVCAM / DV-in-AVI files, whose creation date is
// stored inside the DV frames rather than the container.
static df::date_t read_dv_rec_datetime(AVFormatContext* fc, const int video_stream_index)
{
	if (!fc) return {};

	// Rewind so we read the first recorded frame (a thumbnail extraction may have
	// left the demuxer positioned mid-stream).
	av_seek_frame(fc, -1, 0, AVSEEK_FLAG_BACKWARD);

	df::date_t result;
	auto* pkt = av_packet_alloc();

	for (int tries = 0; tries < 64 && av_read_frame(fc, pkt) >= 0; ++tries)
	{
		// A full SD DV frame is 120000 (NTSC) / 144000 (PAL) bytes; require at
		// least one DIF sequence so the VAUX pack offsets are in range.
		if (pkt->stream_index == video_stream_index && pkt->data && pkt->size >= 12000)
		{
			result = dv_extract_rec_datetime(pkt->data, static_cast<size_t>(pkt->size));
			av_packet_unref(pkt);
			if (result.is_valid()) break;
			continue;
		}

		av_packet_unref(pkt);
	}

	av_packet_free(&pkt);
	return result;
}

static void populate_properties(const AVFormatContext* ctx, file_scan_result& result)
{
	if (ctx)
	{
		result.nb_streams = ctx->nb_streams;
		result.duration = calc_duration(ctx->duration, AV_NOPTS_VALUE);


		const AVDictionaryEntry* tag = nullptr;

		while ((tag = av_dict_get(ctx->metadata, "", tag, AV_DICT_IGNORE_SUFFIX)))
		{
			if (str::icmp(tag->key, "xmp") == 0 || str::icmp(tag->key, "id3v2_priv.XMP") == 0)
			{
				result.metadata.xmp = unescape_xmp(tag->value);
			}
			else
			{
				result.ffmpeg_metadata.emplace_back(tag->key, str::utf8_cast(tag->value));
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

// Name of the decoder that would be used for a stream, or empty when none is built in.
static str::cached decoder_name(const AVCodecParameters* const codec)
{
	const auto* const found = codec ? avcodec_find_decoder(codec->codec_id) : nullptr;
	return found ? str::cache(found->name) : str::cached{};
}

// Human-readable name of an AVPixelFormat value, or empty when unset/unknown.
static str::cached pixel_format_name(const int format)
{
	const auto* const desc = format == AV_PIX_FMT_NONE
		                         ? nullptr
		                         : av_pix_fmt_desc_get(static_cast<AVPixelFormat>(format));
	return desc ? str::cache(desc->name) : str::cached{};
}

static void populate_audio_properties(const AVStream* const s, file_scan_result& result)
{
	auto* codec = s->codecpar;

	if (codec && codec->codec_type == AVMEDIA_TYPE_AUDIO)
	{
		if (const auto name = decoder_name(codec); !str::is_empty(name)) result.audio_codec = name;

		result.audio_sample_rate = codec->sample_rate;
		result.audio_channels = codec->ch_layout.nb_channels;
		result.audio_sample_type = to_sample_type(static_cast<AVSampleFormat>(codec->format));
	}
}

static void populate_video_properties(const AVStream* const s, file_scan_result& result)
{
	const auto* codec = s->codecpar;

	if (codec && codec->codec_type == AVMEDIA_TYPE_VIDEO)
	{
		if (const auto name = decoder_name(codec); !str::is_empty(name)) result.video_codec = name;

		result.width = codec->width;
		result.height = codec->height;

		// Apply the pixel (sample) aspect ratio so anamorphic / non-square-pixel
		// video reports its display dimensions rather than the stored frame size,
		// matching the aspect used during playback (#78).
		auto sar = codec->sample_aspect_ratio;

		if (sar.num == 0 || sar.den == 0 || sar.num == sar.den)
		{
			sar = s->sample_aspect_ratio;
		}

		if (sar.num > 0 && sar.den > 0 && sar.num != sar.den && codec->width > 0 && codec->height > 0)
		{
			const auto w = static_cast<int64_t>(codec->width);
			result.height = static_cast<uint32_t>(df::mul_div(
				w, static_cast<int64_t>(sar.den) * codec->height, static_cast<int64_t>(sar.num) * w));
		}

		if (const auto name = pixel_format_name(codec->format); !str::is_empty(name))
		{
			result.pixel_format = name;
		}

		result.orientation = calc_orientation_impl(df::round(get_rotation(s)));
	}
}

int try_avcodec_send_packet(AVCodecContext* avctx, const AVPacket* avpkt)
{
	// A fault here (typically in a hardware decoder / GPU driver) is deliberately NOT caught:
	// it propagates to the global unhandled-exception handler so the app writes a minidump,
	// reports the crash, and is relaunched by Application Restart. The persisted hardware-decode
	// crash guard (see init_streams / apply_gpu_crash_guard) then disables HW video decoding on
	// the next launch. Swallowing the fault here would instead leave playback silently broken.
	return avcodec_send_packet(avctx, avpkt);
}

// Number of live decoders currently using hardware video decode. The crash guard flag is
// written on the 0->1 transition and cleared on the 1->0 transition so concurrent decoders
// (playback plus any preview) keep it set for as long as any hardware decode is in progress.
static std::atomic<int> g_hw_decode_sessions{0};

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
		return gen == other.gen ? time < other.time : gen < other.gen;
	}

	av_frame() noexcept
	{
		memset(&frm, 0, sizeof(frm));
	}

	av_frame(av_frame&& other) noexcept
	{
		memset(&frm, 0, sizeof(frm));
		av_frame_move_ref(&frm, &other.frm);
		gen = other.gen;
		time = other.time;
		orientation = other.orientation;
		eof = other.eof;
	}

	av_frame(const av_frame& other) noexcept
	{
		memset(&frm, 0, sizeof(frm));
		av_frame_ref(&frm, &other.frm);
		gen = other.gen;
		time = other.time;
		orientation = other.orientation;
		eof = other.eof;
	}

	av_frame& operator=(const av_frame& other) noexcept
	{
		if (this != &other)
		{
			av_frame_unref(&frm);
			av_frame_ref(&frm, &other.frm);
			gen = other.gen;
			time = other.time;
			orientation = other.orientation;
			eof = other.eof;
		}
		return *this;
	}

	av_frame& operator=(av_frame&& other) noexcept
	{
		if (this != &other)
		{
			av_frame_unref(&frm);
			av_frame_move_ref(&frm, &other.frm);
			gen = other.gen;
			time = other.time;
			orientation = other.orientation;
			eof = other.eof;
		}
		return *this;
	}

	~av_frame()
	{
		av_frame_unref(&frm);
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

	av_packet& operator=(const av_packet& other) noexcept
	{
		if (this != &other)
		{
			av_packet_unref(pkt);
			av_packet_ref(pkt, other.pkt);
			seek_ver = other.seek_ver;
			eof = other.eof;
		}
		return *this;
	}

	av_packet& operator=(av_packet&& other) noexcept
	{
		if (this != &other)
		{
			av_packet_unref(pkt);
			av_packet_move_ref(pkt, other.pkt);
			seek_ver = other.seek_ver;
			eof = other.eof;
		}
		return *this;
	}

	~av_packet() noexcept
	{
		av_packet_unref(pkt);
		av_packet_free(&pkt);
	}

	void copy(const AVPacket* src_avpkt) const
	{
		av_packet_ref(pkt, src_avpkt);
	}

	void move(AVPacket* src_avpkt) const
	{
		av_packet_move_ref(pkt, src_avpkt);
	}

	bool is_empty() const
	{
		return pkt->data == nullptr;
	};
};

size_t av_queued_payload_bytes(const av_packet_ptr& p)
{
	return p && p->pkt && p->pkt->size > 0 ? static_cast<size_t>(p->pkt->size) : 0;
}

size_t av_queued_payload_bytes(const av_frame_ptr& f)
{
	if (!f) return 0;

	const auto& frm = f->frm;

	// A hardware frame's own buffer is a handle, not pixels. What it costs is the pool surface it
	// keeps checked out, and that pool is allocated in full when the stream opens, so charging the
	// surface is what makes one read-ahead budget size both the queue and the pool.
	if (frm.hw_frames_ctx)
	{
		const auto* const ctx = std::bit_cast<const AVHWFramesContext*>(frm.hw_frames_ctx->data);
		const auto bytes = av_image_get_buffer_size(ctx->sw_format, ctx->width, ctx->height, 1);
		return bytes > 0 ? static_cast<size_t>(bytes) : 0;
	}

	size_t result = 0;

	for (const auto* const buf : frm.buf)
	{
		if (buf) result += buf->size;
	}

	for (auto i = 0; i < frm.nb_extended_buf; ++i)
	{
		if (frm.extended_buf[i]) result += frm.extended_buf[i]->size;
	}

	return result;
}


av_pts_correction::av_pts_correction()
{
	clear();
}

void av_pts_correction::clear()
{
	last_output = AV_NOPTS_VALUE;
	frame_interval = 0;
}

int64_t av_pts_correction::guess(const int64_t best_effort, const int64_t pts, const int64_t dts,
                                 const int64_t duration)
{
	// Step 1 - take FFmpeg's own answer. avcodec_receive_frame runs guess_correct_pts over
	// (pts, pkt_dts) and publishes the result as best_effort_timestamp, using fault counters
	// that avcodec_flush_buffers resets with the decoder. Running a second, separately reset
	// copy of that heuristic here could only diverge from the decoder's view.
	int64_t result = best_effort;

	if (result == AV_NOPTS_VALUE) result = pts;
	if (result == AV_NOPTS_VALUE) result = dts;

	// Step 2 - guarantee a usable, strictly increasing result. Some codecs and
	// containers (raw video, MJPEG sequences, damaged MPEG-TS) supply no usable
	// timestamp or one that fails to advance. Emitting a stale/duplicate value
	// makes the presenter treat the frame as "not newer" and stall, so instead we
	// extend the timeline by one frame interval - preferring the decoder-reported
	// duration and otherwise the cadence learned from earlier frames.
	if (result != AV_NOPTS_VALUE && last_output != AV_NOPTS_VALUE && result > last_output)
	{
		// Smallest positive step seen, not the most recent one: a gap in a damaged stream
		// would otherwise become the synthetic step and run the timeline away from the media.
		const auto observed = result - last_output;
		frame_interval = frame_interval > 0 ? std::min(frame_interval, observed) : observed;
	}

	const auto step = duration > 0 ? duration : (frame_interval > 0 ? frame_interval : 1);

	if (result == AV_NOPTS_VALUE)
	{
		result = last_output == AV_NOPTS_VALUE ? 0 : last_output + step;
	}
	else if (last_output != AV_NOPTS_VALUE && result <= last_output)
	{
		result = last_output + step;
	}

	last_output = result;
	return result;
}

///////////////////////////////////////////////////
////////////////////////////////////////////////
////////////////////////////////////////////////
////////////////////////////////////////////////

// Maps an AVFrame's signalled colour space + range onto the app's color_space enum.
// When the matrix is unspecified (very common) it falls back to the standard
// resolution heuristic: SD -> BT.601, HD -> BT.709, UHD -> BT.2020.
static ui::color_space av_frame_color_space(const AVFrame& frm)
{
	const bool full_range = frm.color_range == AVCOL_RANGE_JPEG;

	auto matrix = frm.colorspace;

	if (matrix == AVCOL_SPC_UNSPECIFIED)
	{
		if (frm.height >= 2000) matrix = AVCOL_SPC_BT2020_NCL;
		else if (frm.height > 576) matrix = AVCOL_SPC_BT709;
		else matrix = AVCOL_SPC_BT470BG;
	}

	switch (matrix)
	{
	case AVCOL_SPC_BT709:
		return full_range ? ui::color_space::rec709_full : ui::color_space::rec709_limited;
	case AVCOL_SPC_BT2020_NCL:
	case AVCOL_SPC_BT2020_CL:
		return full_range ? ui::color_space::rec2020_full : ui::color_space::rec2020_limited;
	case AVCOL_SPC_BT470BG:
	case AVCOL_SPC_SMPTE170M:
	case AVCOL_SPC_SMPTE240M:
	default:
		return full_range ? ui::color_space::rec601_full : ui::color_space::rec601_limited;
	}
}

av_frame_d3d av_get_d3d_info(const av_frame_ptr& frame_in)
{
	av_frame_d3d result;
	result.width = frame_in->frm.width;
	result.height = frame_in->frm.height;
	result.orientation = frame_in->orientation;
	result.color_space = av_frame_color_space(frame_in->frm);

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

double av_audio_frame_duration(const av_frame_ptr& f)
{
	if (!f || f->frm.sample_rate <= 0) return 0.0;
	return static_cast<double>(f->frm.nb_samples) / f->frm.sample_rate;
}

bool av_is_frame_empty(const av_frame_ptr& f)
{
	return f ? f->is_empty() : false;
}

static bool is_yuv_format(const AVPixelFormat f)
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
			result.aspect_ratio = {ar.num, ar.den};
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

// An empty, owning AVChannelLayout. The deleter uninitialises the layout (it can
// own a heap allocation for custom orders) before releasing it.
static channel_layout_ptr make_channel_layout()
{
	return {
		new AVChannelLayout{}, [](AVChannelLayout* layout)
		{
			av_channel_layout_uninit(layout);
			delete layout;
		}
	};
}

channel_layout_ptr av_get_def_channel_layout(const int num_channels)
{
	auto dst = make_channel_layout();
	av_channel_layout_default(dst.get(), num_channels);
	return dst;
}

channel_layout_ptr av_get_channel_layout(const uint64_t mask, const int fallback_channels)
{
	auto dst = make_channel_layout();

	if (mask == 0 || av_channel_layout_from_mask(dst.get(), mask) < 0)
	{
		av_channel_layout_default(dst.get(), fallback_channels);
	}

	return dst;
}

static channel_layout_ptr av_copy_to_ptr(const AVChannelLayout& src)
{
	auto dst = make_channel_layout();
	if (av_channel_layout_copy(dst.get(), &src) < 0) return {};
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

static int av_read(void* opaque, uint8_t* buf, const int buf_size)
{
	df::assert_true(buf_size != 0);

	const auto* const h = static_cast<platform::file*>(opaque);
	const auto read = static_cast<int>(h->read(buf, buf_size));
	if (read < 1) return AVERROR_EOF;
	return read;
}

static int64_t av_seek(void* opaque, const int64_t offset, const int whence)
{
	const auto* const h = static_cast<platform::file*>(opaque);
	int64_t result = 0;
	const auto seek_whence = whence & ~AVSEEK_FORCE;

	if (AVSEEK_SIZE == seek_whence)
	{
		result = static_cast<int64_t>(h->size());
	}
	else if (seek_whence == SEEK_SET)
	{
		result = static_cast<int64_t>(h->seek(offset, platform::file::whence::begin));
	}
	else if (seek_whence == SEEK_CUR)
	{
		result = static_cast<int64_t>(h->seek(offset, platform::file::whence::current));
	}
	else if (seek_whence == SEEK_END)
	{
		result = static_cast<int64_t>(h->seek(offset, platform::file::whence::end));
	}
	else
	{
		df::assert_true(false);
	}

	return result;
}

static int get_stream_type(const AVFormatContext* ctx, const int stream_num)
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

bool av_format_decoder::seek(const double wanted) const
{
	auto success = false;

	auto* const fc = _format_context;

	if (fc)
	{
		df::trace(std::format("av_format_decoder::seek {}", wanted));

		const auto to_ts = [](const double t) { return static_cast<int64_t>(t * AV_TIME_BASE); };

		// avformat_seek_file takes absolute container timestamps, but `wanted` is on the
		// presentation timeline, which starts at _time_origin. Files whose first PTS is not
		// zero - MPEG-TS especially - were therefore seeked short by their whole start offset.
		const auto target = to_ts(wanted) + _time_origin;
		const auto file_min = std::min(target, to_ts(_start_time) + _time_origin);
		const auto file_max = std::max(target, to_ts(_end_time) + _time_origin);

		// avformat_seek_file clears AVSEEK_FLAG_BACKWARD and derives the direction from the
		// window instead: it only searches backwards when the target sits nearer max_ts than
		// min_ts. A window centred on the target therefore always resolved to a forward seek,
		// landing on the key frame *after* the request and skipping up to a whole GOP. Asking
		// for a window that ends at the target is the only way to express "at or before".
		auto ret = avformat_seek_file(fc, -1, file_min, target, target, 0);

		if (ret < 0)
		{
			// No key frame at or before the target; take the first one that does exist rather
			// than leaving the demuxer where it was.
			ret = avformat_seek_file(fc, -1, file_min, target, file_max, 0);
		}

		_eof = ret == AVERROR_EOF;
		success = ret >= 0;

		if (_eof)
		{
			df::trace("av_format_decoder:seek end of stream");
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
	auto* const fc = _format_context;

	if (!fc || _eof)
	{
		return {};
	}

	// Read straight into the wrapper's packet: demuxing into a second AVPacket and
	// moving the reference costs an alloc/free pair on the hottest playback path.
	auto result = std::make_shared<av_packet>();
	const auto ret = av_read_frame(fc, result->pkt);

	if (ret == AVERROR_EOF)
	{
		_eof = true;
		result->eof = true;
		df::trace("av_format_decoder:read_packet end of stream");
		return result;
	}

	if (ret != 0)
	{
		return {};
	}

	return result;
}

void av_format_decoder::extract_metadata(file_scan_result& sr) const
{
	const auto* const fc = _format_context;

	if (fc)
	{
		populate_properties(fc, sr);
		const AVStream* audio_stream = nullptr;
		const AVStream* video_stream = nullptr;

		for (int i = 0; i < static_cast<int>(fc->nb_streams); ++i)
		{
			const auto* const stream = fc->streams[i];

			if (stream)
			{
				const auto is_cover_art = (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0;

				if (is_cover_art && !sr.cover_art)
				{
					const auto& packet = stream->attached_pic;
					const auto decoded = load_image_file({packet.data, static_cast<size_t>(packet.size)});
					if (decoded) sr.cover_art = decoded;
				}

				if (stream->codecpar)
				{
					const auto ct = stream->codecpar->codec_type;
					// An attached-picture (cover art) stream reports as AVMEDIA_TYPE_VIDEO but is a
					// still image, not a real video track. Never treat it as the video stream, or an
					// audio file with embedded cover art would report the image's dimensions/codec as
					// video properties. Cover art is extracted separately above.
					if (ct == AVMEDIA_TYPE_VIDEO && !is_cover_art && !video_stream) video_stream = stream;
					if (ct == AVMEDIA_TYPE_AUDIO && !audio_stream) audio_stream = stream;
				}
			}
		}

		if (audio_stream) populate_audio_properties(audio_stream, sr);
		if (video_stream) populate_video_properties(video_stream, sr);

		// DVCAM / DV-in-AVI files store their recording date/time inside the DV
		// frames rather than the container, so extract it from the first frame.
		if (video_stream && video_stream->codecpar &&
			video_stream->codecpar->codec_id == AV_CODEC_ID_DVVIDEO)
		{
			const auto dv_date = read_dv_rec_datetime(_format_context, video_stream->index);

			if (dv_date.is_valid())
			{
				// DV times are local wall-clock; store so created() round-trips it.
				sr.created_utc = dv_date.local_to_system();
			}
		}

		const auto bit_rate = fc->bit_rate;

		if (bit_rate > 0)
		{
			sr.bitrate = str::cache(prop::format_bit_rate(bit_rate));
		}

		if (fc->duration != AV_NOPTS_VALUE)
		{
			sr.duration = df::round(calc_duration(fc->duration, AV_NOPTS_VALUE));
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

void av_format_decoder::close()
{
	_is_open = false;

	// The codec contexts are about to be freed, so there is nothing to gain from
	// draining them first - and draining a hardware decoder is far from free.
	avcodec_free_context(&_video_context);
	avcodec_free_context(&_audio_context);
	av_buffer_unref(&_hw_device_ctx);

	// Release this decoder's hold on the hardware-decode crash guard. Clearing on the
	// 1->0 transition marks a clean end to HW decode so a later unrelated crash is not
	// misattributed to video decoding.
	if (_hw_decode_guard_held)
	{
		_hw_decode_guard_held = false;
		if (g_hw_decode_sessions.fetch_sub(1) == 1)
		{
			platform::set_crash_guard(platform::crash_guard::hw_video_decode, false);
		}
	}

	_pts_vid.clear();
	_pts_aud.clear();

	AVFormatContext* fc = nullptr;
	std::swap(fc, _format_context);

	if (fc)
	{
		auto* pb = fc->pb;
		avformat_close_input(&fc);

		if (pb)
		{
			// A caller-supplied AVIOContext (AVFMT_FLAG_CUSTOM_IO) is left alone by
			// avformat_close_input, and avio_context_free does not release the read
			// buffer - both must go or every opened file leaks its 256K buffer.
			av_freep(&pb->buffer);
			avio_context_free(&pb);
		}
	}

	_scaler.reset();
	_file.reset();
	_path.clear();
	_eof = false;
	_has_video = false;
	_has_audio = false;
	_has_multiple_audio_streams = false;
	_video_stream_index = -1;
	_audio_stream_index = -1;
	_bitrate = 0;
	_streams.clear();
	_cover_art.reset();
	_start_time = 0;
	_end_time = 0;
	_rotation = 0;
	_video_base = {};
	_audio_base = {};
	_video_stream_aspect_ratio = {};
	_video_start_time = 0;
	_audio_start_time = 0;
	_time_origin = 0;
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

// True when the container header alone already named every playable stream and sized every picture,
// so the demuxer has a real header rather than a raw or transport stream that must be discovered by
// reading it. Only video and audio are judged: MOV and MP4 timecode (tmcd) and Apple metadata (mebx)
// tracks legitimately reach the app with no codec id at all, and camera and phone footage almost
// always carries one, so counting them as an incomplete header disqualified most real video.
static bool has_header_codec_parameters(const AVFormatContext* fc)
{
	if (!fc || fc->nb_streams == 0) return false;
	if (fc->ctx_flags & AVFMTCTX_NOHEADER) return false;

	auto described_streams = 0;

	for (unsigned i = 0; i < fc->nb_streams; ++i)
	{
		const auto* const stream = fc->streams[i];
		if (!stream) return false;

		const auto* const codec = stream->codecpar;
		if (!codec) return false;

		if (codec->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			if (codec->codec_id == AV_CODEC_ID_NONE || codec->width <= 0 || codec->height <= 0) return false;
			++described_streams;
		}
		else if (codec->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			if (codec->codec_id == AV_CODEC_ID_NONE || codec->sample_rate <= 0 ||
				codec->ch_layout.nb_channels <= 0)
			{
				return false;
			}

			++described_streams;
		}
	}

	return described_streams > 0;
}

// True when probing found at least one stream the app could actually show, play or list. FFmpeg
// falls back to matching a demuxer on the file extension alone, so any file carrying a media
// extension - a TypeScript index.ts picked up by the MPEG-TS demuxer, say - opens cleanly and then
// describes nothing. Judged after avformat_find_stream_info, when a raw or transport stream that
// had to be discovered by reading has had its chance to name its streams.
static bool has_presentable_stream(const AVFormatContext* fc)
{
	if (!fc) return false;

	for (unsigned i = 0; i < fc->nb_streams; ++i)
	{
		const auto* const stream = fc->streams[i];
		if (!stream) continue;

		const auto* const codec = stream->codecpar;
		if (!codec || codec->codec_id == AV_CODEC_ID_NONE) continue;

		switch (codec->codec_type)
		{
		case AVMEDIA_TYPE_VIDEO:
			if (codec->width > 0 && codec->height > 0) return true;
			break;
		case AVMEDIA_TYPE_AUDIO:
			if (codec->sample_rate > 0 && codec->ch_layout.nb_channels > 0) return true;
			break;
		case AVMEDIA_TYPE_SUBTITLE:
			return true;
		default:
			break;
		}
	}

	return false;
}

bool av_format_decoder::open(const df::file_path path, const media_intent intent)
{
	const auto file = open_file(path, platform::file_open_mode::read);

	if (!file)
	{
		return false;
	}

	df::trace(std::format("av_format_decoder::open {}", path.name()));

	return open(file, path, intent);
}

bool av_format_decoder::open(const platform::file_ptr& file, const df::file_path path, const media_intent intent)
{
	close();

	// The AVIOContext assumes the stream starts at zero, and a handed-over handle has already been
	// read - by the write that produced it, or by the scan that inspected it.
	file->seek(0, platform::file::whence::begin);

	// An extension shared with a text format is settled by the header before ffmpeg probes it, so a
	// TypeScript .ts is refused outright rather than part-opening as a stream with nothing in it.
	if (files::has_media_header_rule(path.extension()))
	{
		uint8_t header[files::media_header_probe_bytes];
		const auto header_read = file->read(header, sizeof(header));
		file->seek(0, platform::file::whence::begin);

		if (!files::media_header_matches(path.extension(), {header, static_cast<size_t>(header_read)}))
		{
			df::trace(std::format("av_format_decoder::open header mismatch {}", path.name()));
			return false;
		}
	}

	static constexpr int io_buffer_size = df::two_fifty_six_k;
	auto* const io_buffer = static_cast<uint8_t*>(av_mallocz(io_buffer_size + 16));

	auto* fc = avformat_alloc_context();
	auto* pb = avio_alloc_context(io_buffer, io_buffer_size, 0, file.get(), av_read, nullptr, av_seek);
	fc->pb = pb;
	fc->flags |= AVFMT_FLAG_GENPTS;

	AVDictionary* opts = nullptr;
	av_dict_set_int(&opts, "export_xmp", 1, 0);

	if (avformat_open_input(&fc, str::utf8_to_a(path.str()).c_str(), nullptr, &opts) != 0)
	{
		// avformat_open_input frees fc on failure and sets it to NULL,
		// but pb and its buffer are not freed. We saved pb above.
		av_dict_free(&opts);
		av_freep(&pb->buffer);
		avio_context_free(&pb);
		return false;
	}

	av_dict_free(&opts);

	_format_context = fc;
	_path = path;
	_file = file;

	// avformat_find_stream_info keeps entropy-decoding H.264 until it has guessed the reorder delay -
	// 7 frames, up to 20 - and nothing a metadata scan reports uses that answer. Two separate bounds
	// hold it back, both applied AFTER open_input so container metadata is already gathered and
	// demuxers that consume probesize in their own header read (mpeg-ts) are untouched.
	AVDictionary** stream_opts = nullptr;
	const auto opts_stream_count = fc->nb_streams;

	if (intent == media_intent::metadata)
	{
		// The probe decoder is the only entropy decoding an index scan performs, and H.264 is the one
		// codec the probe keeps decoding after the stream is already characterised. AVDISCARD_ALL drops
		// each slice once its header is read; the fork applies the SPS on that path, so width, height,
		// pixel format and frame rate still land. This bounds decoding, not reading, so unlike the read
		// bound below it needs no guarantee about the header - a stream the demuxer has to discover is
		// still discovered, just without the entropy decode. find_stream_info takes one dictionary per
		// stream and hands it to that stream's probe decoder at avcodec_open2.
		stream_opts = static_cast<AVDictionary**>(av_calloc(opts_stream_count, sizeof(AVDictionary*)));

		if (stream_opts)
		{
			for (unsigned i = 0; i < opts_stream_count; ++i)
			{
				const auto* const stream = fc->streams[i];

				if (stream && stream->codecpar && stream->codecpar->codec_id == AV_CODEC_ID_H264)
				{
					av_dict_set(&stream_opts[i], "skip_frame", "all", 0);
				}
			}
		}

		// The read bound is the narrower of the two: it truncates the probe, so it is only safe for a
		// container that already named its streams. Anything that must be discovered by reading still
		// gets the full probe. Probe cost is roughly linear in bytes read, so probesize is the budget.
		if (has_header_codec_parameters(fc))
		{
			fc->probesize = df::two_fifty_six_k * 2;
			fc->max_analyze_duration = AV_TIME_BASE;
		}
	}

	avformat_find_stream_info(fc, stream_opts);

	if (stream_opts)
	{
		for (unsigned i = 0; i < opts_stream_count; ++i) av_dict_free(&stream_opts[i]);
		av_freep(&stream_opts);
	}

	if (!has_presentable_stream(fc))
	{
		df::log(__FUNCTION__, std::format("no media stream in {}", path.name()));
		close();
		return false;
	}

	// Read the bit rate only after probing: many containers (and every stream that
	// needs its rate estimating) report 0 until avformat_find_stream_info has run.
	_bitrate = fc->bit_rate;

	auto audio_stream_count = 0;

	for (int i = 0; i < static_cast<int>(fc->nb_streams); ++i)
	{
		const auto* const stream = fc->streams[i];

		if (stream)
		{
			const AVDictionaryEntry* tag = nullptr;
			av_stream_info s;
			s.index = i;

			auto* const codec = stream->codecpar;

			while (stream->metadata && (tag = av_dict_get(stream->metadata, "", tag, AV_DICT_IGNORE_SUFFIX)))
			{
				if (is_key(tag->key, "title")) s.title = str::utf8_cast(tag->value);
				if (is_key(tag->key, "codec")) s.codec = str::utf8_cast(tag->value);
				if (is_key(tag->key, "FourCC")) s.fourcc = str::utf8_cast(tag->value);
				if (is_key(tag->key, "language")) s.language = str::utf8_cast(tag->value);
				s.metadata.emplace_back(tag->key, str::utf8_cast(tag->value));
			}

			s.is_commentary = (stream->disposition & AV_DISPOSITION_COMMENT) != 0;
			s.is_audio_description = (stream->disposition & AV_DISPOSITION_VISUAL_IMPAIRED) != 0;
			s.rotation = df::round(get_rotation(stream));

			if ((stream->disposition & AV_DISPOSITION_ATTACHED_PIC) && !_cover_art)
			{
				const auto& packet = stream->attached_pic;
				const auto decoded = load_image_file({packet.data, static_cast<size_t>(packet.size)});
				if (decoded) _cover_art = decoded;
			}

			if (codec)
			{
				if (s.fourcc.empty() && stream->codecpar->codec_tag)
				{
					char name[AV_FOURCC_MAX_STRING_SIZE];
					s.fourcc = str::utf8_cast(av_fourcc_make_string(name, stream->codecpar->codec_tag));
				}

				if (codec->codec_type == AVMEDIA_TYPE_VIDEO)
				{
					s.pixel_format = pixel_format_name(codec->format);
				}

				if (codec->codec_type == AVMEDIA_TYPE_AUDIO)
				{
					s.audio_sample_rate = codec->sample_rate;
					s.audio_channels = codec->ch_layout.nb_channels;
					s.audio_sample_type = to_sample_type(static_cast<AVSampleFormat>(codec->format));
				}
				const auto codec_name = std::string(str::utf8_cast(avcodec_get_name(codec->codec_id)));
				if (s.codec.empty()) s.codec = codec_name;
				s.metadata.emplace_back("codec"_c, codec_name);

				if (codec->codec_tag)
				{
					char name[AV_FOURCC_MAX_STRING_SIZE];
					s.metadata.emplace_back("fourcc"_c,
					                        std::string(
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

static AVPixelFormat get_hw_format(AVCodecContext* ctx,
                                   const AVPixelFormat* pix_fmts)
{
	const auto wanted = av_platform_hw_decode_target().pix_fmt;

	for (const AVPixelFormat* p = pix_fmts; *p != AV_PIX_FMT_NONE; p++)
	{
		if (*p == wanted)
			return *p;
	}

	// The decoder offered no surface format the renderer can present. Returning NONE aborts this
	// decode; the caller only installs this callback once the platform's device is live, so this
	// indicates a driver/codec mismatch rather than a normal path.
	df::log(__FUNCTION__, "Failed to get hardware surface format");
	return AV_PIX_FMT_NONE;
}

// Surfaces this app checks out of the pool beyond the read-ahead queue: the frame on screen, the
// two update_for_present holds while it settles onto a sought position, and one for a frame the
// decoder emits after the queue has stopped asking for more.
static constexpr size_t hw_frames_held_outside_queue = 4;

// What one hardware surface costs in the decoder's pool. Deliberately computed from the coded
// size rather than the aligned one the pool actually rounds up to: under-reading the cost buys one
// more surface, and an over-read would buy one fewer than the queue is about to hold.
static size_t hw_surface_bytes(const AVCodecParameters* par)
{
	if (!par || par->width <= 0 || par->height <= 0) return 0;

	const auto* const desc = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(par->format));
	const size_t bytes_per_sample = desc && desc->comp[0].depth > 8 ? 2 : 1;

	// NV12, or P010 when the stream is deeper than 8 bits: one full luma plane and a half-height
	// interleaved chroma plane.
	return static_cast<size_t>(par->width) * static_cast<size_t>(par->height) * 3 * bytes_per_sample / 2;
}


void av_format_decoder::init_streams(int video_track, int audio_track, const bool can_use_hw, const bool video_only,
                                     const bool can_use_threads)
{
	auto* const fc = _format_context;

#ifdef _DEBUG
#endif


	const AVStream* video_stream = nullptr;
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
			auto* vc = avcodec_alloc_context3(video_codec);

			if (vc)
			{
				const auto is_cover_art = video_stream->disposition & AV_DISPOSITION_ATTACHED_PIC;
				const auto codec_supports_threads = video_codec->capabilities & (AV_CODEC_CAP_FRAME_THREADS |
					AV_CODEC_CAP_SLICE_THREADS |
					AV_CODEC_CAP_OTHER_THREADS);

				avcodec_parameters_to_context(vc, video_stream->codecpar);

				if (codec_supports_threads && can_use_threads && !is_cover_art)
				{
					vc->thread_count = 4;
				}
				else
				{
					vc->thread_count = 1;
				}

				vc->workaround_bugs = FF_BUG_AUTODETECT;
				vc->thread_type = FF_THREAD_FRAME;

				const auto hw_target = av_platform_hw_decode_target();

				if (can_use_hw && hw_target.is_available())
				{
					// Only the platform's own hardware path is wired into the renderer: decoded
					// frames must arrive in a surface format update() can share with the render
					// device. Scan every advertised hw config (they are not ordered, and a
					// non-matching config must not abort the search) and pick that one. Others are
					// skipped so we never install get_hw_format for a format we cannot present.
					for (int i = 0;; i++)
					{
						const auto* hw_config = avcodec_get_hw_config(video_codec, i);

						if (!hw_config)
						{
							break; // end of the config list
						}

						if ((hw_config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
							hw_config->pix_fmt == hw_target.pix_fmt &&
							static_cast<int>(hw_config->device_type) == hw_target.device_type)
						{
							const auto ret = av_hwdevice_ctx_create(&_hw_device_ctx,
							                                        hw_config->device_type,
							                                        nullptr, nullptr, 0);

							if (ret == 0)
							{
								vc->get_format = get_hw_format;

								// The whole pool is one texture array created when the stream
								// opens, so every surface is paid for whether or not it is used.
								// FFmpeg already provisions the decoder's own reference frames; these
								// are the extra surfaces this app checks out - the read-ahead queue,
								// the frame on screen, and the ones update_for_present holds while it
								// settles - so they follow the same byte budget the queue does.
								vc->extra_hw_frames = static_cast<int>(
									av_read_ahead_frames(hw_surface_bytes(video_stream->codecpar)) +
									hw_frames_held_outside_queue);

								// Frame threading costs one more pool surface per thread and buys
								// almost nothing once the GPU is doing the decoding.
								vc->thread_count = 1;

								vc->hw_device_ctx = av_buffer_ref(_hw_device_ctx);
								break;
							}
						}
					}
				}

				// Decoding-side timing inputs. Without pkt_timebase FFmpeg cannot express a
				// frame duration or a priming-sample adjustment, and leaves both unset.
				vc->pkt_timebase = video_stream->time_base;
				vc->framerate = av_guess_frame_rate(fc, fc->streams[video_stream_index], nullptr);

				if (avcodec_open2(vc, video_codec, nullptr) == 0)
				{
					_has_video = true;
					_video_base = {video_stream->time_base.num, video_stream->time_base.den};
					_video_stream_index = video_stream_index;
					_video_context = vc;
					_video_stream_aspect_ratio = {
						video_stream->sample_aspect_ratio.num, video_stream->sample_aspect_ratio.den
					};
					_rotation = df::round(get_rotation(video_stream));
				}
				else
				{
					// Publishing the index would route every packet of this stream into a queue
					// nothing drains; publishing the context would hand receive_frames a context
					// that was never opened.
					avcodec_free_context(&vc);
					video_stream = nullptr;
				}

				// Hardware decode is now live for this decoder. Raise the process-wide crash
				// guard so a fault during HW decode is detected on the next launch and only HW
				// video decoding is disabled (GPU rendering is left on).
				if (_has_video && _hw_device_ctx && !_hw_decode_guard_held)
				{
					_hw_decode_guard_held = true;
					if (g_hw_decode_sessions.fetch_add(1) == 0)
					{
						platform::set_crash_guard(platform::crash_guard::hw_video_decode, true);
					}
					df::log(__FUNCTION__, "hardware video decode active");
				}
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
					// Required for FFmpeg to shift the timestamps of gapless formats (AAC, MP3,
					// Opus) by their encoder delay. Without it the decoder still drops the priming
					// samples but leaves the PTS where it was, so the audio timeline starts early
					// by that delay and every video frame is matched against it.
					ac->pkt_timebase = audio_stream->time_base;

					if (avcodec_open2(ac, aud_decoder, nullptr) == 0)
					{
						_has_audio = true;
						_audio_base = {audio_stream->time_base.num, audio_stream->time_base.den};
						_audio_stream_index = aud_stream;
						_audio_context = ac;
					}
					else
					{
						avcodec_free_context(&ac);
						audio_stream = nullptr;
					}
				}
			}
		}
	}

	for (auto&& st : _streams)
	{
		st.is_playing = st.index == _audio_stream_index || st.index == _video_stream_index;
	}

	// One origin for the whole presentation: the earliest start among the streams actually
	// being played, expressed once and then rescaled into each stream's own time base. Taking
	// each stream's own start_time (as this used to) pulled both to zero independently and so
	// discarded the offset between them - the container-level A/V delay that MPEG-TS and
	// edit-listed MP4 rely on. The earliest playing stream, rather than fc->start_time, keeps
	// the result non-negative even when the container's own figure covers streams we ignore.
	auto origin = std::numeric_limits<int64_t>::max();

	if (video_stream && video_stream->start_time != AV_NOPTS_VALUE)
	{
		origin = std::min(origin, av_rescale_q(video_stream->start_time, video_stream->time_base, AV_TIME_BASE_Q));
	}

	if (audio_stream && audio_stream->start_time != AV_NOPTS_VALUE)
	{
		origin = std::min(origin, av_rescale_q(audio_stream->start_time, audio_stream->time_base, AV_TIME_BASE_Q));
	}

	if (origin == std::numeric_limits<int64_t>::max())
	{
		origin = fc && fc->start_time != AV_NOPTS_VALUE ? fc->start_time : 0;
	}

	_time_origin = origin;
	_video_start_time = video_stream ? av_rescale_q(origin, AV_TIME_BASE_Q, video_stream->time_base) : 0;
	_audio_start_time = audio_stream ? av_rescale_q(origin, AV_TIME_BASE_Q, audio_stream->time_base) : 0;

	double end_time_context = 0;
	double end_time_video = 0;
	double end_time_audio = 0;

	if (fc && fc->duration != AV_NOPTS_VALUE)
	{
		end_time_context = calc_duration(fc->duration, AV_NOPTS_VALUE);
	}

	if (video_stream && video_stream->duration != AV_NOPTS_VALUE)
	{
		end_time_video = calc_duration(video_stream->duration, video_stream->time_base, AV_NOPTS_VALUE);
	}

	if (audio_stream && audio_stream->duration != AV_NOPTS_VALUE)
	{
		end_time_audio = calc_duration(audio_stream->duration, audio_stream->time_base, AV_NOPTS_VALUE);
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
	result.cover_art = _cover_art;
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
			if (str::icmp(tag->key, "id3v2_priv.XMP") == 0 || str::icmp(tag->key, "xmp") == 0)
			{
				const auto packet = unescape_xmp(tag->value);
				auto xmp_kv = metadata_xmp::to_info(packet);
				const auto parsed = !xmp_kv.empty();

				// The packet is the block's real content, so it stays reachable whether or not the
				// toolkit could make a tree from it.
				constexpr size_t max_raw_bytes = 256 * 1024;
				std::string raw;
				raw.assign(std::bit_cast<const char*>(packet.data()), std::min(packet.size(), max_raw_bytes));

				result.metadata.emplace_back(metadata_standard::xmp, std::move(xmp_kv), packet.size(), parsed,
				                             std::move(raw));
			}
			else
			{
				kv.emplace_back(tag->key, str::utf8_cast(tag->value));
			}
		}

		result.metadata.emplace_back(metadata_standard::ffmpeg, kv);

		for (uint32_t i = 0; i < ctx->nb_streams; ++i)
		{
			const auto* const codec = ctx->streams[i]->codecpar;

			if (!codec)
			{
				continue;
			}

			if (codec->codec_type == AVMEDIA_TYPE_VIDEO)
			{
				if (const auto name = decoder_name(codec); !str::is_empty(name)) result.video_codec = name;
				if (const auto fmt = pixel_format_name(codec->format); !str::is_empty(fmt)) result.pixel_format = fmt;
			}
			else if (codec->codec_type == AVMEDIA_TYPE_AUDIO)
			{
				if (const auto name = decoder_name(codec); !str::is_empty(name)) result.audio_codec = name;

				result.audio_sample_rate = codec->sample_rate;
				result.audio_channels = codec->ch_layout.nb_channels;
				result.audio_sample_type = to_sample_type(static_cast<AVSampleFormat>(codec->format));
			}
		}
	}

	return result;
}


ui::orientation av_format_decoder::calc_orientation() const
{
	return calc_orientation_impl(_rotation);
}

void av_format_decoder::update_orientation(const AVFrame* const frame)
{
	if (!frame)
	{
		return;
	}

	const AVFrameSideData* side_data = av_frame_get_side_data(frame, AV_FRAME_DATA_DISPLAYMATRIX);

	if (side_data)
	{
		// Adopt the frame's rotation outright. Subtracting the whole-turn part of the
		// angle (as this used to) left _rotation at the container value and silently
		// dropped a rotation signalled only per frame.
		_rotation = df::round(rotation_from_display_matrix(side_data->data, side_data->size));
	}
}

bool av_format_decoder::decode_frame(ui::surface_ptr& dest_surface, AVCodecContext* ctx, const av_packet_ptr& packet,
                                     const sizei max_dim)
{
	auto success = false;

	if (try_avcodec_send_packet(ctx, packet->pkt) == 0)
	{
		AVFrame frame = {};

		if (avcodec_receive_frame(ctx, &frame) == 0)
		{
			const auto pts = _pts_vid.guess(frame.best_effort_timestamp, frame.pts, frame.pkt_dts, frame.duration);
			auto time = to_video_seconds(pts);

			if (frame.repeat_pict)
			{
				time += 1 / 25.0;
			}

			if (!_scaler) _scaler = std::make_unique<av_scaler>();
			success = _scaler->scale_frame(frame, dest_surface, max_dim, time, calc_orientation(),
			                               _video_stream_aspect_ratio);
		}

		av_frame_unref(&frame);
	}

	return success;
}


bool av_format_decoder::decode_nearest_frame(ui::surface_ptr& dest_surface, const sizei max_dim,
                                             const double wanted_time, const double tolerance,
                                             df::cancel_token abandon)
{
	if (!_has_video)
	{
		return false;
	}

	auto* const ctx = _video_context;

	if (!_scaler)
	{
		_scaler = std::make_unique<av_scaler>();
	}

	// A seek lands on the key frame at or before the target, so every frame between the two is
	// decoded on the way. Only a reference to the nearest one is kept: scaling each improving frame
	// in turn cost a bicubic pass and a fresh surface per frame of the GOP, and all but the last
	// were thrown away.
	av_frame best;
	auto best_time = 0.0;
	auto best_dist = std::numeric_limits<double>::max();
	auto found = false;
	auto reached = false;

	// The walk normally stops the moment it passes wanted_time; this only bounds a stream that
	// never gets there (a truncated or corrupt file). It therefore has to be wide enough to
	// span a whole GOP of interleaved video and audio packets - a ten second GOP alone is well
	// over a thousand - or the caller silently gets a frame short of the position it asked for.
	constexpr int max_packets = 8192;

	for (int i = 0; i < max_packets && !reached && !df::is_closing; i++)
	{
		const auto packet = read_packet();

		if (!packet || packet->eof)
		{
			break;
		}

		if (packet->pkt->stream_index != _video_stream_index)
		{
			continue;
		}

		if (try_avcodec_send_packet(ctx, packet->pkt) != 0)
		{
			continue;
		}

		av_frame frame;

		while (!reached && avcodec_receive_frame(ctx, &frame.frm) == 0)
		{
			const auto pts = _pts_vid.guess(frame.frm.best_effort_timestamp, frame.frm.pts, frame.frm.pkt_dts,
			                                frame.frm.duration);
			const auto time = to_video_seconds(pts);
			const auto dist = fabs(time - wanted_time);

			// Frames arrive in presentation order, so the distance to the target shrinks until we
			// pass it; the last improvement is the nearest frame.
			if (dist < best_dist)
			{
				best_dist = dist;
				best_time = time;
				found = true;
				av_frame_unref(&best.frm);
				av_frame_move_ref(&best.frm, &frame.frm);
			}
			else
			{
				av_frame_unref(&frame.frm);
			}

			// Either the walk has passed the target, or it holds a frame near enough for a caller that
			// allowed slack. Refining further would only improve a frame already accepted.
			if (time >= wanted_time || (found && best_dist <= tolerance))
			{
				reached = true;
			}
		}

		// The caller always needs something to show, so the first frame decoded is never given up -
		// only the refinement toward the exact one is. A pointer that has already moved on makes that
		// refinement worthless.
		if (found && abandon.is_cancelled())
		{
			break;
		}
	}

	return found && _scaler->scale_frame(best.frm, dest_surface, max_dim, best_time, calc_orientation(),
	                                     _video_stream_aspect_ratio);
}

bool av_format_decoder::extract_seek_frame(ui::surface_ptr& dest_surface, const sizei max_dim,
                                           const double pos_numerator,
                                           const double pos_denominator, df::cancel_token abandon)
{
	if (!_has_video)
	{
		return false;
	}

	auto* const ctx = _video_context;
	const auto start = start_time();
	const auto len = end_time() - start;

	// Use the same target time the live scrubber seeks to (media start + a
	// fraction of the duration) and then decode forward to the frame nearest that
	// time. Showing only the first (key) frame after the seek - as extract_thumbnail
	// used to - leaves the preview up to a whole GOP away from the pointed-at
	// position, which is not the frame the player jumps to.
	const auto x = std::clamp(pos_numerator, 0.0, pos_denominator);
	const auto wanted_time = start + floor(x * len / std::max(1.0, pos_denominator));

	seek(wanted_time);

	// The preview decoder is reused across hovers without going through the normal
	// flush path, so drop any buffered frames and reset the timestamp estimator -
	// otherwise a backward hover is pulled forward by guess()'s monotonic guard.
	avcodec_flush_buffers(ctx);
	_pts_vid.clear();

	return decode_nearest_frame(dest_surface, max_dim, wanted_time, 0.0, abandon);
}

bool av_format_decoder::extract_thumbnail(ui::surface_ptr& dest_surface, const sizei max_dim,
                                          const double pos_numerator,
                                          const double pos_denominator,
                                          const bool exact_frame,
                                          const double tolerance_fraction, df::cancel_token abandon)
{
	auto success = false;

	if (_has_video)
	{
		auto* const ctx = _video_context;
		const auto duration = end_time() - start_time();
		const auto time_wanted = duration * pos_numerator / pos_denominator;

		if (duration > 0)
		{
			// The preview decoder is reused across hovers, so it only sits at the start of the stream
			// when it has just been opened. Without a seek, a hover near the start answers from
			// wherever the previous hover left the decoder - and every frame from there is already
			// past the requested time, so the walk returns the first one it sees.
			auto seek_success = seek(time_wanted);

			if (seek_success)
			{
				// A container-level seek does not flush the decoder, so drop any
				// frames buffered before the seek and reset the timestamp estimator -
				// otherwise the thumbnail can come from a stale pre-seek frame (matches
				// the flush in extract_seek_frame).
				avcodec_flush_buffers(ctx);
				_pts_vid.clear();
			}
			else
			{
				// Decoding forward is only meaningful from a known position, so a stream that cannot
				// seek answers from the start and nowhere else.
				seek_success = time_wanted <= 2.0;
			}

			if (seek_success)
			{
				if (exact_frame)
				{
					success = decode_nearest_frame(dest_surface, max_dim, time_wanted,
					                               duration * std::max(0.0, tolerance_fraction), abandon);
				}
				else
				{
					for (int i = 0; i < 1024 && !success && !df::is_closing; i++)
					{
						const auto packet = read_packet();

						if (!packet || packet->eof)
						{
							break;
						}

						if (packet->pkt->stream_index == _video_stream_index)
						{
							success = decode_frame(dest_surface, ctx, packet, max_dim);
						}
					}
				}
			}
		}
	}

	return success;
}

double av_format_decoder::to_video_seconds(const int64_t vt) const
{
	return calc_duration(vt, {_video_base.num, _video_base.den}, _video_start_time);
}


file_load_result av_format_decoder::render_frame(const av_frame_ptr& frame_in) const
{
	file_load_result result;

	if (!_video_context || !frame_in)
	{
		return result;
	}

	if (!_scaler) _scaler = std::make_unique<av_scaler>();

	// scale_surface sizes and allocates the destination and downloads a hardware
	// frame itself, so doing either here would only duplicate the work (the readback
	// this used to perform was a full GPU->CPU frame copy that was then thrown away).
	const auto s = std::make_shared<ui::surface>();

	if (_scaler->scale_surface(frame_in, s))
	{
		result.s = s;
		result.success = !result.is_empty();
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
	}

	av_freep(&_out_buffer);
}

// Points `planes` at the reusable output buffer, growing it when the requested run
// of samples does not fit. Keeping one buffer for the whole session removes an
// allocate/free pair from every decoded audio frame and gives resample(), drain()
// and flush() a single, correctly sized place to convert into.
bool audio_resampler::prepare_output(uint8_t** planes, const int samples, const audio_info_t& format)
{
	const auto channels = static_cast<int>(format.channel_count());
	const auto fmt = to_AVSampleFormat(format.sample_fmt);

	if (samples <= 0 || channels <= 0 || fmt == AV_SAMPLE_FMT_NONE)
	{
		return false;
	}

	if (av_sample_fmt_is_planar(fmt) && channels > AV_NUM_DATA_POINTERS)
	{
		return false;
	}

	const auto needed = av_samples_get_buffer_size(nullptr, channels, samples, fmt, 0);

	if (needed <= 0)
	{
		return false;
	}

	if (needed > _out_buffer_size)
	{
		av_freep(&_out_buffer);
		_out_buffer = static_cast<uint8_t*>(av_malloc(needed));
		_out_buffer_size = _out_buffer ? needed : 0;
	}

	return _out_buffer &&
		av_samples_fill_arrays(planes, nullptr, _out_buffer, channels, samples, fmt, 0) >= 0;
}

void audio_resampler::flush()
{
	if (!_aud_resampler)
	{
		return;
	}

	// Discard whatever the resampler still holds. The scratch must be sized for the
	// OUTPUT format: sizing it from the source stream (as this used to) overflows
	// whenever the device rate/width exceeds the stream's.
	const auto pending = swr_get_out_samples(_aud_resampler, 0);

	if (pending > 0)
	{
		uint8_t* planes[AV_NUM_DATA_POINTERS] = {};

		if (prepare_output(planes, pending, _output_info))
		{
			swr_convert(_aud_resampler, planes, pending, nullptr, 0);
		}
	}
}

void audio_resampler::drain(audio_buffer& audio_buffer, const int gen)
{
	if (!_aud_resampler)
	{
		return;
	}

	const auto dest_format = audio_buffer.format;
	const auto out_num_channels = dest_format.channel_count();
	const auto out_sample_size = dest_format.bytes_per_sample();

	if (out_num_channels == 0 || out_sample_size == 0)
	{
		return;
	}

	for (int guard = 0; guard < 8; ++guard)
	{
		const auto pending = swr_get_out_samples(_aud_resampler, 0);

		if (pending <= 0)
		{
			break;
		}

		uint8_t* planes[AV_NUM_DATA_POINTERS] = {};

		if (!prepare_output(planes, pending, dest_format))
		{
			break;
		}

		// NULL input flushes the resampler's internal buffer.
		const auto out_samples = swr_convert(_aud_resampler, planes, pending, nullptr, 0);

		if (out_samples <= 0)
		{
			break;
		}

		audio_buffer.append(planes[0], out_samples * out_num_channels * out_sample_size,
		                    audio_buffer.end_time(), gen);
	}
}


// Scales `total_samples` interleaved samples in place by `gain`, clamping to the
// format's range so a boost above 1.0 hard-limits instead of wrapping/overflowing.
static void apply_audio_gain(uint8_t* const data, const int total_samples, const AVSampleFormat fmt,
                             const double gain)
{
	switch (fmt)
	{
	case AV_SAMPLE_FMT_FLT:
		{
			auto* const p = reinterpret_cast<float*>(data);
			const auto g = static_cast<float>(gain);

			for (int i = 0; i < total_samples; ++i)
			{
				const auto v = p[i] * g;
				p[i] = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
			}

			break;
		}
	case AV_SAMPLE_FMT_S16:
		{
			auto* const p = reinterpret_cast<int16_t*>(data);

			for (int i = 0; i < total_samples; ++i)
			{
				const auto v = static_cast<int32_t>(p[i] * gain);
				p[i] = static_cast<int16_t>(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
			}

			break;
		}
	case AV_SAMPLE_FMT_S32:
		{
			auto* const p = reinterpret_cast<int32_t*>(data);

			for (int i = 0; i < total_samples; ++i)
			{
				const auto v = p[i] * gain;
				p[i] = static_cast<int32_t>(v > 2147483647.0 ? 2147483647.0 : (v < -2147483648.0 ? -2147483648.0 : v));
			}

			break;
		}
	default:
		break;
	}
}

void audio_resampler::resample(const av_frame_ptr& frame, audio_buffer& audio_buffer)
{
	audio_info_t source_format;
	source_format.channel_layout = av_channel_layout_check(&frame->frm.ch_layout)
		                               ? av_copy_to_ptr(frame->frm.ch_layout)
		                               : _stream_info.channel_layout;

	// AV_SAMPLE_FMT_NONE is -1 and is what an undecoded frame carries; 0 is
	// AV_SAMPLE_FMT_U8, a valid format, so test for < 0 rather than == 0.
	source_format.sample_fmt = frame->frm.format < 0
		                           ? _stream_info.sample_fmt
		                           : to_sample_type(static_cast<AVSampleFormat>(frame->frm.format));

	source_format.sample_rate = frame->frm.sample_rate == 0 ? _stream_info.sample_rate : frame->frm.sample_rate;

	const auto dest_format = audio_buffer.format;
	const auto dest_sample_fmt = to_AVSampleFormat(dest_format.sample_fmt);

	if (source_format != _frame_info || dest_format != _output_info)
	{
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
		                             nullptr) && swr && swr_init(swr) == 0)
		{
			_aud_resampler = swr;
			_frame_info = source_format;
			_output_info = dest_format;
		}
		else if (swr)
		{
			// Leave the cached formats alone so a later frame retries the setup
			// rather than silently dropping every remaining sample.
			swr_free(&swr);
		}
	}

	if (!_aud_resampler)
	{
		return;
	}

	// Use the resolved source format/layout (which may fall back to the stream
	// info) so planarity and the expected plane count match what swr was set up
	// with, even when the frame left format/ch_layout unset.
	const auto is_planar = av_sample_fmt_is_planar(to_AVSampleFormat(source_format.sample_fmt));
	const int planes_expected = is_planar ? static_cast<int>(source_format.channel_count()) : 1;

	auto is_valid = frame->frm.linesize[0] != 0 && frame->frm.extended_data != nullptr;

	for (int i = 0; is_valid && i < planes_expected; ++i)
	{
		is_valid = frame->frm.extended_data[i] != nullptr;
	}

	const auto expected_out_samples = swr_get_out_samples(_aud_resampler, frame->frm.nb_samples);
	const auto out_num_channels = dest_format.channel_count();
	const auto out_sample_size = dest_format.bytes_per_sample();

	uint8_t* planes[AV_NUM_DATA_POINTERS] = {};

	if (!prepare_output(planes, expected_out_samples, dest_format))
	{
		return;
	}

	if (!is_valid)
	{
		// The frame carries no usable sample data - emit the equivalent run of
		// silence so the timeline does not jump.
		const auto silence_size = expected_out_samples * out_num_channels * out_sample_size;
		memset(planes[0], 0, silence_size);
		audio_buffer.append(planes[0], silence_size, frame->time, frame->gen);
		return;
	}

	const auto out_samples = swr_convert(_aud_resampler, planes, expected_out_samples,
	                                     frame->frm.extended_data, frame->frm.nb_samples);

	if (out_samples < 0)
	{
		df::log(__FUNCTION__, "swr_convert failed");
		return;
	}

	if (_gain != 1.0 && out_samples > 0)
	{
		apply_audio_gain(planes[0], out_samples * out_num_channels, dest_sample_fmt, _gain);
	}

	audio_buffer.append(planes[0], out_samples * out_num_channels * out_sample_size, frame->time, frame->gen);
}

av_scaler::~av_scaler()
{
	if (_scaler)
	{
		sws_freeContext(_scaler);
		_scaler = nullptr;
	}
}

namespace
{
	// A whole encoded image is already in memory here, so the AVIOContext reads from the span rather
	// than a file. ffmpeg still probes it, which is what settles the format.
	struct memory_source
	{
		const uint8_t* data = nullptr;
		int64_t size = 0;
		int64_t pos = 0;
	};

	int memory_read(void* opaque, uint8_t* buffer, int wanted)
	{
		auto* const source = static_cast<memory_source*>(opaque);
		const auto available = source->size - source->pos;

		if (available <= 0) return AVERROR_EOF;

		const auto count = std::min(static_cast<int64_t>(wanted), available);
		std::memcpy(buffer, source->data + source->pos, static_cast<size_t>(count));
		source->pos += count;
		return static_cast<int>(count);
	}

	int64_t memory_seek(void* opaque, const int64_t offset, const int whence)
	{
		auto* const source = static_cast<memory_source*>(opaque);

		// The probe asks for the size through this same callback.
		if (whence == AVSEEK_SIZE) return source->size;

		const auto base = whence == SEEK_CUR ? source->pos : whence == SEEK_END ? source->size : 0;
		const auto target = base + offset;

		if (target < 0 || target > source->size) return AVERROR(EINVAL);

		source->pos = target;
		return target;
	}
}

ui::surface_ptr av_decode_still(const df::cspan data, const sizei max_dim, const std::string_view extension_hint)
{
	if (data.data == nullptr || data.size == 0) return {};

	memory_source source{data.data, static_cast<int64_t>(data.size), 0};

	static constexpr int io_buffer_size = df::sixty_four_k;
	auto* const io_buffer = static_cast<uint8_t*>(av_mallocz(io_buffer_size + 16));
	if (!io_buffer) return {};

	auto* pb = avio_alloc_context(io_buffer, io_buffer_size, 0, &source, memory_read, nullptr, memory_seek);

	if (!pb)
	{
		av_free(io_buffer);
		return {};
	}

	auto* fc = avformat_alloc_context();

	if (!fc)
	{
		av_freep(&pb->buffer);
		avio_context_free(&pb);
		return {};
	}

	fc->pb = pb;

	// The probe reads the extension off this name. There is no file to open: pb already holds the
	// bytes, and a format with a signature is found whether or not a name is given.
	const auto probe_name = extension_hint.empty() ? std::string{} : std::format("image{}", extension_hint);

	if (avformat_open_input(&fc, probe_name.empty() ? nullptr : probe_name.c_str(), nullptr, nullptr) != 0)
	{
		// open_input frees fc itself on failure, but not the context it was given.
		av_freep(&pb->buffer);
		avio_context_free(&pb);
		return {};
	}

	const df::scope_exit close_input([&fc, &pb]
	{
		avformat_close_input(&fc);
		if (pb) av_freep(&pb->buffer);
		avio_context_free(&pb);
	});

	if (avformat_find_stream_info(fc, nullptr) < 0) return {};

	const auto stream_index = av_find_best_stream(fc, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
	if (stream_index < 0) return {};

	const auto* const params = fc->streams[stream_index]->codecpar;

	// Every still decoder with a format of its own refuses an over-budget source by returning an
	// empty surface, and an empty surface is exactly what routes a file here - so without this gate
	// the fallback re-decodes what the budget just refused. It is also the only gate the formats
	// ffmpeg alone carries (TGA, SGI, the portable pixmaps, DPX) ever get, because scan_photo reads
	// no header for them and the caller's check is skipped when the geometry is unknown.
	if (reject_over_budget_source(nullptr, {params->width, params->height}, "ffmpeg")) return {};

	const auto* const codec = avcodec_find_decoder(params->codec_id);
	if (!codec) return {};

	auto* cc = avcodec_alloc_context3(codec);
	if (!cc) return {};

	const df::scope_exit free_codec([&cc] { avcodec_free_context(&cc); });

	if (avcodec_parameters_to_context(cc, params) < 0) return {};

	// codecpar can understate what the bitstream then asks for, so the ceiling is restated where the
	// decoder itself will enforce it.
	if (df::max_decode_bytes > 0) cc->max_pixels = df::max_decode_bytes / 4;

	if (avcodec_open2(cc, codec, nullptr) != 0) return {};

	auto* frame = av_frame_alloc();
	auto* packet = av_packet_alloc();

	const df::scope_exit free_av([&frame, &packet]
	{
		if (frame) av_frame_free(&frame);
		if (packet) av_packet_free(&packet);
	});

	if (!frame || !packet) return {};

	// One frame is the whole image. An animation stops at its first, which is the frame the browser
	// and the still viewer both show.
	while (av_read_frame(fc, packet) >= 0)
	{
		const df::scope_exit unref([packet] { av_packet_unref(packet); });

		if (packet->stream_index != stream_index) continue;
		if (avcodec_send_packet(cc, packet) != 0) continue;

		if (avcodec_receive_frame(cc, frame) == 0)
		{
			ui::surface_ptr result;
			av_scaler scaler;

			if (scaler.scale_frame(*frame, result, max_dim, 0.0, ui::orientation::top_left))
			{
				return result;
			}

			return {};
		}
	}

	return {};
}

// swscale defaults to BT.601 limited range for any YUV source, which is wrong for most HD and for
// anything full range, so the signalled matrix has to be pushed into the context explicitly.
static void apply_colorspace_details(SwsContext* scaler, const ui::color_space cs)
{
	int colorspace = SWS_CS_ITU601;
	bool full_range = false;

	switch (cs)
	{
	case ui::color_space::rec709_limited: colorspace = SWS_CS_ITU709;
		break;
	case ui::color_space::rec709_full: colorspace = SWS_CS_ITU709;
		full_range = true;
		break;
	case ui::color_space::rec2020_limited: colorspace = SWS_CS_BT2020;
		break;
	case ui::color_space::rec2020_full: colorspace = SWS_CS_BT2020;
		full_range = true;
		break;
	case ui::color_space::rec601_full: full_range = true;
		break;
	case ui::color_space::rec601_limited:
	default: break;
	}

	const auto* const coefficients = sws_getCoefficients(colorspace);

	// Refused when the source is RGB and there is no matrix to set, which is not an error here.
	sws_setColorspaceDetails(scaler, coefficients, full_range, coefficients, true, 0, 1 << 16, 1 << 16);
}

bool av_scaler::scale_surface(const ui::const_surface_ptr& surface_in, ui::surface_ptr& surface_out,
                              const sizei dimensions_out, const bool high_quality)
{
	const auto source_extent = surface_in->dimensions();
	const auto fmt = surface_in->format();
	const auto source_fmt = fmt == ui::texture_format::NV12
		                        ? AV_PIX_FMT_NV12
		                        : fmt == ui::texture_format::P010
		                        ? AV_PIX_FMT_P010LE
		                        : fmt == ui::texture_format::RGB || fmt == ui::texture_format::ARGB
		                        ? AV_PIX_FMT_BGRA
		                        : AV_PIX_FMT_NONE;
	if (source_fmt == AV_PIX_FMT_NONE) return false;

	// swscale has no RGB->RGB scaler: it converts to planar YUV and back, and a BGRA source carries
	// no chroma subsampling, so libswscale forces SWS_FULL_CHR_H_INT and the scalar
	// yuv2bgra32_full_X_c output converter. Reducing a packed surface is answered directly instead.
	if (source_fmt == AV_PIX_FMT_BGRA && ui::area_downscale(surface_in, surface_out, dimensions_out))
	{
		return true;
	}

	constexpr auto output_fmt = AV_PIX_FMT_BGRA;
	_scaler = sws_getCachedContext(_scaler, source_extent.cx, source_extent.cy, source_fmt, dimensions_out.cx,
	                               dimensions_out.cy, output_fmt,
	                               high_quality ? SWS_BICUBIC : SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);

	if (_scaler)
	{
		// Without this a full-range JPEG NV12 surface would be converted as limited range here while the
		// GPU sampler converts it as full range, so the same image shifts colour between scales.
		apply_colorspace_details(_scaler, surface_in->color_space());

		surface_out = std::make_shared<ui::surface>();
		// YUV carries no alpha, so a converted frame is opaque RGB - flagging it ARGB would force the
		// software canvas down its per-pixel Porter-Duff path for every draw.
		const auto destination_format = source_fmt == AV_PIX_FMT_BGRA ? fmt : ui::texture_format::RGB;

		if (!surface_out->alloc(dimensions_out.cx, dimensions_out.cy, destination_format, surface_in->orientation()))
		{
			surface_out.reset();
			return false;
		}

		surface_out->color_space(surface_in->color_space());

		const auto stride = static_cast<int>(surface_in->stride());
		const auto* const pixels = surface_in->pixels();
		const auto is_yuv = source_fmt == AV_PIX_FMT_NV12 || source_fmt == AV_PIX_FMT_P010LE;
		const uint8_t* src_data[4] = {
			pixels,
			is_yuv ? pixels + static_cast<ptrdiff_t>(stride) * source_extent.cy : nullptr,
			nullptr,
			nullptr
		};
		const int src_stride[4] = {stride, is_yuv ? stride : 0, 0, 0};

		uint8_t* dst_data[4] = {surface_out->pixels(), nullptr, nullptr, nullptr};
		const int dst_stride[4] = {static_cast<int>(surface_out->stride()), 0, 0, 0};

		const auto scaled = sws_scale(_scaler, src_data, src_stride, 0, source_extent.cy,
		                              dst_data, dst_stride);

		return scaled == dimensions_out.cy && is_valid(surface_out);
	}

	return false;
}

bool av_scaler::convert_yuv_surface(const ui::surface& surface_in, const ui::surface_ptr& surface_out)
{
	const auto source_extent = surface_in.dimensions();
	const auto fmt = surface_in.format();
	const auto source_fmt = fmt == ui::texture_format::NV12
		                        ? AV_PIX_FMT_NV12
		                        : fmt == ui::texture_format::P010
		                        ? AV_PIX_FMT_P010LE
		                        : AV_PIX_FMT_NONE;
	if (source_fmt == AV_PIX_FMT_NONE || !surface_out) return false;

	constexpr auto render_fmt = AV_PIX_FMT_BGRA;
	_scaler = sws_getCachedContext(_scaler, source_extent.cx, source_extent.cy, source_fmt,
	                               source_extent.cx, source_extent.cy, render_fmt,
	                               SWS_POINT, nullptr, nullptr, nullptr);
	if (!_scaler) return false;

	apply_colorspace_details(_scaler, surface_in.color_space());

	if (!surface_out->alloc(source_extent, ui::texture_format::RGB, surface_in.orientation(), surface_in.time()))
	{
		return false;
	}
	surface_out->color_space(surface_in.color_space());

	const auto stride = static_cast<int>(surface_in.stride());
	const auto* const y_plane = surface_in.pixels();
	const uint8_t* src_data[4] = {
		y_plane, y_plane + static_cast<ptrdiff_t>(stride) * source_extent.cy,
		nullptr, nullptr
	};
	const int src_stride[4] = {stride, stride, 0, 0};
	uint8_t* dst_data[4] = {surface_out->pixels(), nullptr, nullptr, nullptr};
	const int dst_stride[4] = {static_cast<int>(surface_out->stride()), 0, 0, 0};

	return sws_scale(_scaler, src_data, src_stride, 0, source_extent.cy, dst_data, dst_stride) == source_extent.cy;
}

bool av_scaler::scale_surface(const av_frame_ptr& frame_in, const ui::surface_ptr& surface_out)
{
	bool success = false;
	const AVFrame* frame = &frame_in->frm;
	AVFrame* sw_frame = nullptr;

	// Read before any hardware transfer: av_hwframe_transfer_data moves pixels, not frame properties,
	// so the software copy has no matrix or range signalling of its own.
	const auto cs = av_frame_color_space(frame_in->frm);

	if (frame->hw_frames_ctx)
	{
		sw_frame = av_frame_alloc();

		if (sw_frame && av_hwframe_transfer_data(sw_frame, frame, 0) == 0)
		{
			frame = sw_frame;
		}
	}

	const sizei src_extent = {frame->width, frame->height};
	const auto source_fmt = static_cast<AVPixelFormat>(frame->format);
	constexpr auto render_fmt = AV_PIX_FMT_BGRA;

	_scaler = sws_getCachedContext(_scaler, src_extent.cx, src_extent.cy, source_fmt,
	                               src_extent.cx, src_extent.cy, render_fmt,
	                               SWS_BILINEAR, nullptr, nullptr, nullptr);

	if (_scaler)
	{
		apply_colorspace_details(_scaler, cs);

		if (surface_out->alloc(src_extent, ui::texture_format::RGB, frame_in->orientation, frame_in->time))
		{
			uint8_t* data[4] = {surface_out->pixels(), nullptr, nullptr, nullptr};
			const int linesize[4] = {static_cast<int>(surface_out->stride()), 0, 0, 0};

			const auto ret = sws_scale(_scaler, frame->data, frame->linesize, 0, src_extent.cy, data, linesize);
			success = ret > 0;
		}
	}

	av_frame_free(&sw_frame);

	return success;
}

bool av_scaler::scale_frame(const AVFrame& frame, ui::surface_ptr& surface, const sizei max_dim, const double time,
                            const ui::orientation orientation, const av_rational container_sar)
{
	bool success = false;
	const auto fmt = static_cast<AVPixelFormat>(frame.format);
	const sizei src_dims(frame.width, frame.height);

	// Correct for the pixel (sample) aspect ratio so anamorphic / non-square-pixel
	// video is scaled to its display shape rather than the stored frame shape (#78).
	auto disp_dims = src_dims;
	auto sar = frame.sample_aspect_ratio;

	// A pasp box in an MP4 reaches AVStream::sample_aspect_ratio only, so a file whose bitstream
	// VUI carries no aspect ratio hands the decoder a square-pixel frame the container contradicts.
	if (sar.num == 0 || sar.den == 0 || sar.num == sar.den)
	{
		sar = {container_sar.num, container_sar.den};
	}

	if (sar.num > 0 && sar.den > 0 && sar.num != sar.den && frame.width > 0 && frame.height > 0)
	{
		const auto w = static_cast<int64_t>(frame.width);
		disp_dims.cy = static_cast<int>(df::mul_div(
			w, static_cast<int64_t>(sar.den) * frame.height, static_cast<int64_t>(sar.num) * w));
	}

	const auto dst_dims = ui::scale_dimensions(disp_dims, max_dim);

	_scaler = sws_getCachedContext(_scaler, src_dims.cx, src_dims.cy, fmt, dst_dims.cx, dst_dims.cy,
	                               AV_PIX_FMT_BGRA, SWS_BICUBIC, nullptr, nullptr, nullptr);

	if (_scaler)
	{
		apply_colorspace_details(_scaler, av_frame_color_space(frame));

		surface = std::make_shared<ui::surface>();

		if (!surface->alloc(dst_dims.cx, dst_dims.cy, ui::texture_format::RGB, orientation, time))
		{
			surface.reset();
			return false;
		}

		uint8_t* data[4] = {(surface->pixels()), nullptr, nullptr, nullptr};
		const int linesize[4] = {static_cast<int>(surface->stride()), 0, 0, 0};

		success = sws_scale(_scaler, frame.data, frame.linesize, 0, src_dims.cy, data, linesize) == dst_dims.cy;

		if (!success)
		{
			df::log(__FUNCTION__, "sws_scale failed");
		}
	}

	return success;
}

void av_session::process_io(const platform::thread_event& video_event, const platform::thread_event& audio_event)
{
	// The open-time snapshots, not the decoder: these are read before the lock is taken.
	const auto has_audio = _has_audio.load();
	const auto has_video = _has_video.load();
	const auto video_stream = _video_stream_id.load();
	const auto audio_stream = _audio_stream_id.load();
	auto loop_iteration = 0;

	while ((has_audio && _audio_packets.should_receive()) || (has_video && _video_packets.should_receive()))
	{
		// The condition above is an OR, so a queue that never drains keeps the loop alive.
		// The ceiling is what stops the other stream buffering the rest of the file.
		if ((has_video && _video_packets.is_full()) || (has_audio && _audio_packets.is_full()))
		{
			break;
		}

		platform::shared_lock lock(_decoder_rw);
		const auto packet = _decoder.read_packet();

		if (packet)
		{
			if (packet->eof)
			{
				// Push a stream-tagged EOF marker to each queue so receive_frames can
				// flush (drain) the matching decoder's buffered tail before signalling
				// end of stream.
				if (has_video)
				{
					auto vp = std::make_shared<av_packet>();
					vp->eof = true;
					vp->seek_ver = _seek_gen;
					vp->pkt->stream_index = video_stream;
					_video_packets.push(vp);
				}

				if (has_audio)
				{
					auto ap = std::make_shared<av_packet>();
					ap->eof = true;
					ap->seek_ver = _seek_gen;
					ap->pkt->stream_index = audio_stream;
					_audio_packets.push(ap);
				}

				audio_event.set();
				video_event.set();
				break;
			}
			if (has_video && packet->pkt->stream_index == video_stream)
			{
				packet->seek_ver = _seek_gen;
				_video_packets.push(packet);
				video_event.set();
			}
			else if (has_audio && packet->pkt->stream_index == audio_stream)
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


void av_format_decoder::receive_available_frames(AVCodecContext* const ctx, av_pts_correction& pts,
                                                 const av_rational base, const int64_t start, const int seek_gen,
                                                 av_frame_queue& frames)
{
	const AVRational time_base{base.num, base.den};
	av_frame_ptr frame;

	for (;;)
	{
		if (!frame) frame = std::make_shared<av_frame>();

		if (avcodec_receive_frame(ctx, &frame->frm) != 0)
		{
			break;
		}

		update_orientation(&frame->frm);

		frame->gen = seek_gen;
		frame->time = calc_duration(pts.guess(frame->frm.best_effort_timestamp, frame->frm.pts, frame->frm.pkt_dts,
		                                      frame->frm.duration),
		                            time_base, start);
		frame->orientation = calc_orientation();

		frames.push(std::move(frame));
	}
}

void av_format_decoder::receive_frames(av_packet_queue& packets, av_frame_queue& frames)
{
	av_packet_ptr packet;

	if (!packets.pop(packet))
	{
		return;
	}

	AVCodecContext* c = nullptr;
	av_pts_correction* pts = nullptr;
	av_rational base;
	int64_t start = AV_NOPTS_VALUE;
	const auto si = packet->pkt->stream_index;
	const auto* stream_name = "unknown stream";

	if (si == _video_stream_index)
	{
		c = _video_context;
		pts = &_pts_vid;
		base = _video_base;
		start = _video_start_time;
		stream_name = "video stream";
	}
	else if (si == _audio_stream_index)
	{
		c = _audio_context;
		pts = &_pts_aud;
		base = _audio_base;
		start = _audio_start_time;
		stream_name = "audio stream";
	}

	const auto seek_gen = packet->seek_ver;

	if (packet->eof)
	{
		if (c)
		{
			// Drain the decoder so frames it still holds (codecs such as AAC delay
			// output) are emitted rather than dropped - that lost tail is what cut the
			// sound short at the end - then push an EOF marker for the output path to
			// follow with silence.
			avcodec_send_packet(c, nullptr);
			receive_available_frames(c, *pts, base, start, seek_gen, frames);
			avcodec_flush_buffers(c); // reset for a later seek / replay
		}

		auto eof_frame = std::make_shared<av_frame>();
		eof_frame->eof = true;
		eof_frame->gen = seek_gen;
		frames.push(std::move(eof_frame));
		return;
	}

	if (!c)
	{
		return;
	}

	if (packet->is_empty())
	{
		// A seek queues an empty packet as a flush marker: drop the frames decoded
		// for the old position and reset the decoder and timestamp estimator.
		frames.clear();
		pts->clear();
		avcodec_flush_buffers(c);
		df::trace(std::format("av_format_decoder::receive_frames avcodec_flush_buffers {}", stream_name));
		return;
	}

	// The decoder refuses a new packet with EAGAIN while it still has output
	// buffered, so alternate sending and draining until it takes the packet. The
	// iteration cap keeps a misbehaving decoder from spinning this thread forever.
	for (int attempt = 0; attempt < 64; ++attempt)
	{
		const auto send_res = try_avcodec_send_packet(c, packet->pkt);

		if (send_res != 0 && send_res != AVERROR(EAGAIN))
		{
			break;
		}

		receive_available_frames(c, *pts, base, start, seek_gen, frames);

		if (send_res == 0)
		{
			break;
		}
	}
}

void av_session::state(const av_play_state new_state)
{
	if (_state.exchange(new_state) != new_state)
	{
		_host.invalidate_view(view_invalid::view_layout |
			view_invalid::screen_saver |
			view_invalid::app_layout |
			view_invalid::media_elements |
			view_invalid::command_state);
	}
}

void av_session::seek(const double pos, const bool scrubbing)
{
	_scrubbing = scrubbing;

	if (fabs(_last_seek - pos) > 0.1 || pos < 0.1)
	{
		platform::shared_lock lock(_decoder_rw);

		if (_decoder.seek(pos))
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
			// Published before _pending_time_sync, which is what tells pos() to use it.
			_last_seek = pos;
			_pending_time_sync = true;
			_reset_time_offset = !_decoder.has_audio(); // && !scrubbing;
			_settling = true;
			_audio_eof_handled = false;
			_video_eof_handled = false;
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
