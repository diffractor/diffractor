// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once
#include "model_propery.h"

uint64_t av_get_def_channel_layout(int num_channels);

struct audio_info_t
{
	uint32_t sample_rate = 0;
	prop::audio_sample_t sample_fmt = prop::audio_sample_t::none;
	uint64_t channel_layout = 0;

	friend bool operator==(const audio_info_t& lhs, const audio_info_t& rhs)
	{
		return lhs.sample_rate == rhs.sample_rate
			&& lhs.sample_fmt == rhs.sample_fmt
			&& lhs.channel_layout == rhs.channel_layout;
	}

	friend bool operator!=(const audio_info_t& lhs, const audio_info_t& rhs)
	{
		return !(lhs == rhs);
	}

	uint32_t bytes_per_second() const;
	uint32_t channel_count() const;
	uint32_t bytes_per_sample() const;
};

class audio_buffer : df::no_copy
{
private:
	int gen = -1;

	uint8_t* data = nullptr;
	uint32_t size = 0;
	uint32_t end_pos = 0;

	double time = 0.0;
	const int data_alignment = 16;
	const int data_padding = 64;

	audio_info_t format;

public:
	void init(const audio_info_t& format_in)
	{
		format = format_in;
		size = 4 * format.bytes_per_second();
		data = static_cast<uint8_t*>(_aligned_realloc(data, size + data_padding, data_alignment));
		end_pos = 0;
	}

	~audio_buffer()
	{
		_aligned_free(data);
	}

	bool is_empty() const
	{
		return end_pos == 0;
	}

	/*bool has_one_second() const
	{
		return end_pos >= bytes_per_second;
	}*/

	bool should_fill() const
	{
		return end_pos <= format.bytes_per_second();
	}

	uint32_t used_bytes() const
	{
		return end_pos;
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
		end_pos = 0;
		time = 0.0;
		memset(data, 0, size);
	}

	void remove(uint32_t bytes_to_remove)
	{
		df::assert_true(bytes_to_remove <= end_pos);

		if (bytes_to_remove < end_pos)
		{
			memmove_s(data, size, data + bytes_to_remove, end_pos - bytes_to_remove);
		}

		end_pos -= bytes_to_remove;
		time += bytes_to_remove / static_cast<double>(format.bytes_per_second());
	}

	void append(const uint8_t* data_in, const uint32_t bytes_in, double time_in, int generation_in)
	{
		if (generation_in != gen)
		{
			if (bytes_in <= avail_bytes())
			{
				memcpy_s(data + end_pos, size - end_pos, data_in, bytes_in);
				end_pos = bytes_in;
				time = time_in;
				gen = generation_in;
			}
		}
		else
		{
			const auto original_end_pos = end_pos;

			/*if (bytes_in > avail_bytes())
			{
				data = static_cast<uint8_t*>(_aligned_realloc(data, size * 2 + data_padding, data_alignment));
				size = size * 2;
			}*/

			if (bytes_in <= avail_bytes())
			{
				memcpy_s(data + end_pos, size - end_pos, data_in, bytes_in);
				end_pos += bytes_in;

				time = time_in - (original_end_pos / static_cast<double>(format.bytes_per_second()));
			}
		}
	}

	void append_blank_second()
	{
		if (format.bytes_per_second() <= avail_bytes())
		{
			memset(data + end_pos, 0, format.bytes_per_second());
			end_pos += format.bytes_per_second();
		}
	}

	double end_time() const
	{
		return time + (used_bytes() / static_cast<double>(format.bytes_per_second()));
	}

	double start_time() const
	{
		return time;
	}

	double seconds() const
	{
		return static_cast<double>(end_pos) / format.bytes_per_second();
	}


	friend class wasapi_sound;
	friend class av_visualizer;
	friend class audio_resampler;
};


class av_audio_device
{
public:
	virtual std::u8string id() = 0;
	virtual audio_info_t format() = 0;
	virtual double time() const = 0;
	virtual bool is_stopped() const = 0;
	virtual bool is_device_lost() const = 0;

	virtual void reset() = 0;
	virtual void start() = 0;
	virtual void stop() = 0;
	virtual void write(audio_buffer& audio_buffer) = 0;
	virtual void volume(double x) = 0;
};

using av_audio_device_ptr = std::shared_ptr<av_audio_device>;

av_audio_device_ptr create_av_audio_device(std::u8string_view device_id);

struct sound_device
{
	std::u8string id;
	std::u8string name;
	bool is_current = false;
};

std::vector<sound_device> list_audio_playback_devices();
