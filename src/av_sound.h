// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Audio output interface and buffer management. Defines audio format structures,
// sample buffering, and abstract audio device interface for playback.

#pragma once
#include "model_property.h"

namespace platform
{
	class thread_event;
}

struct AVChannelLayout;
using channel_layout_ptr = std::shared_ptr<AVChannelLayout>;

channel_layout_ptr av_get_def_channel_layout(int num_channels);
channel_layout_ptr av_get_channel_layout(uint64_t mask, int fallback_channels);

struct audio_info_t
{
	uint32_t sample_rate = 0;
	prop::audio_sample_t sample_fmt = prop::audio_sample_t::none;
	channel_layout_ptr channel_layout;

	friend bool operator==(const audio_info_t& lhs, const audio_info_t& rhs);

	friend bool operator!=(const audio_info_t& lhs, const audio_info_t& rhs)
	{
		return !(lhs == rhs);
	}

	uint32_t bytes_per_second() const;
	uint32_t channel_count() const;
	uint32_t bytes_per_sample() const;
};

class audio_buffer final : df::no_copy
{
	int gen = -1;

	uint8_t* data = nullptr;
	uint32_t size = 0;
	uint32_t start_pos = 0;
	uint32_t end_pos = 0;

	double time = 0.0;
	const uint32_t data_alignment = 16u;
	const uint32_t data_padding = 64u;

	audio_info_t format;

public:
	void init(const audio_info_t& format_in)
	{
		format = format_in;
		size = 4 * format.bytes_per_second();
		data = static_cast<uint8_t*>(_aligned_realloc(
			data, static_cast<size_t>(size) + static_cast<size_t>(data_padding), data_alignment));
		start_pos = 0;
		end_pos = 0;
	}

	~audio_buffer() override
	{
		_aligned_free(data);
	}

	bool is_empty() const
	{
		return start_pos == end_pos;
	}

	/*bool has_one_second() const
	{
		return end_pos >= bytes_per_second;
	}*/

	bool should_fill() const
	{
		return used_bytes() <= format.bytes_per_second();
	}

	uint32_t used_bytes() const
	{
		return end_pos - start_pos;
	}

	uint32_t avail_bytes() const
	{
		return size - end_pos;
	}

	uint32_t bytes_per_sample() const
	{
		return format.bytes_per_sample();
	}

	uint32_t bytes_per_second() const
	{
		return format.bytes_per_second();
	}

	int generation() const
	{
		return gen;
	}

	void clear()
	{
		start_pos = 0;
		end_pos = 0;
		time = 0.0;
		if (data) memset(data, 0, size);
	}

	void remove(const uint32_t bytes_to_remove)
	{
		df::assert_true(bytes_to_remove <= used_bytes());
		start_pos += bytes_to_remove;
		const auto bps = format.bytes_per_second();
		if (bps > 0) time += bytes_to_remove / static_cast<double>(bps);

		if (start_pos == end_pos)
		{
			start_pos = 0;
			end_pos = 0;
		}
	}

	void append(const uint8_t* data_in, const uint32_t bytes_in, const double time_in, const int generation_in)
	{
		if (generation_in != gen)
		{
			if (bytes_in <= size)
			{
				memcpy_s(data, size, data_in, bytes_in);
				start_pos = 0;
				end_pos = bytes_in;
				time = time_in;
				gen = generation_in;
			}
		}
		else
		{
			const auto original_used_bytes = used_bytes();

			if (bytes_in > avail_bytes() && start_pos > 0)
			{
				memmove_s(data, size, data + start_pos, original_used_bytes);
				start_pos = 0;
				end_pos = original_used_bytes;
			}

			if (bytes_in <= avail_bytes())
			{
				memcpy_s(data + end_pos, size - end_pos, data_in, bytes_in);
				end_pos += bytes_in;

				const auto bps = format.bytes_per_second();
				time = bps > 0 ? time_in - original_used_bytes / static_cast<double>(bps) : time_in;
			}
		}
	}

	void append_blank_second()
	{
		const auto blank_bytes = format.bytes_per_second();

		if (blank_bytes > avail_bytes() && start_pos > 0)
		{
			const auto bytes = used_bytes();
			memmove_s(data, size, data + start_pos, bytes);
			start_pos = 0;
			end_pos = bytes;
		}

		if (blank_bytes <= avail_bytes())
		{
			memset(data + end_pos, 0, blank_bytes);
			end_pos += blank_bytes;
		}
	}

	// Linearly ramps the last `seconds` of buffered audio down to silence so a clip
	// that ends on a non-zero sample does not produce an audible click when the
	// following silence (or stop) begins.
	void apply_fade_out(const double seconds)
	{
		const auto bps = format.bytes_per_second();
		const auto channels = format.channel_count();
		const auto sample_size = format.bytes_per_sample();

		if (bps == 0 || channels == 0 || sample_size == 0 || is_empty())
		{
			return;
		}

		const auto frame_bytes = channels * sample_size;
		auto fade_bytes = std::min(used_bytes(), static_cast<uint32_t>(seconds * bps));
		fade_bytes -= fade_bytes % frame_bytes;

		if (fade_bytes == 0)
		{
			return;
		}

		const auto fade_frames = fade_bytes / frame_bytes;
		auto* p = data + (end_pos - fade_bytes);

		for (uint32_t f = 0; f < fade_frames; ++f)
		{
			const auto gain = 1.0f - static_cast<float>(f + 1) / static_cast<float>(fade_frames);

			for (uint32_t ch = 0; ch < channels; ++ch)
			{
				switch (format.sample_fmt)
				{
				case prop::audio_sample_t::signed_float:
					reinterpret_cast<float*>(p)[ch] *= gain;
					break;
				case prop::audio_sample_t::signed_16bit:
					reinterpret_cast<int16_t*>(p)[ch] = static_cast<int16_t>(reinterpret_cast<int16_t*>(p)[ch] * gain);
					break;
				case prop::audio_sample_t::signed_32bit:
					reinterpret_cast<int32_t*>(p)[ch] = static_cast<int32_t>(reinterpret_cast<int32_t*>(p)[ch] * gain);
					break;
				default:
					break;
				}
			}

			p += frame_bytes;
		}
	}

	double end_time() const
	{
		const auto bps = format.bytes_per_second();
		return bps > 0 ? time + used_bytes() / static_cast<double>(bps) : time;
	}

	double start_time() const
	{
		return time;
	}

	double seconds() const
	{
		const auto bps = format.bytes_per_second();
		return bps > 0 ? static_cast<double>(used_bytes()) / bps : 0.0;
	}


	friend class wasapi_sound;
	friend class av_visualizer;
	friend class audio_resampler;
};


class av_audio_device
{
public:
	virtual std::string id() = 0;
	virtual audio_info_t format() = 0;
	virtual double time() const = 0;
	virtual bool is_stopped() const = 0;
	virtual bool is_device_lost() const = 0;

	virtual void reset() = 0;
	virtual void start() = 0;
	virtual void stop() = 0;
	virtual void wait_for_buffer(const platform::thread_event& wake_event, uint32_t timeout_ms) = 0;
	virtual void write(audio_buffer& audio_buffer) = 0;
	// Queues a short run of silence so an underrun plays silence instead of looping
	// the last buffer (used to keep the tail clean at end of stream).
	virtual void write_silence() = 0;
	virtual void volume(double x) = 0;
};

using av_audio_device_ptr = std::shared_ptr<av_audio_device>;

av_audio_device_ptr create_av_audio_device(std::string_view device_id);

struct sound_device
{
	std::string id;
	std::string name;
	bool is_current = false;
};

std::vector<sound_device> list_audio_playback_devices();
