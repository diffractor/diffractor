// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Windows movie writing through the Media Foundation sink writer -- H.264 video and AAC
// audio in MP4, using whatever hardware encoder the machine has. Diffractor ships no video encoder
// of its own; docs/movie.md#93-the-encoder-and-the-licensing-problem says why.

#include "pch.h"
#include "platform_win.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <mftransform.h>
#include <codecapi.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

namespace
{
	// Media Foundation counts in 100-nanosecond units everywhere.
	constexpr int64_t hns_per_second = 10000000;

	int64_t to_hns(const double seconds)
	{
		return static_cast<int64_t>(std::llround(seconds * hns_per_second));
	}

	std::string format_hresult(const std::string_view what, const HRESULT hr)
	{
		LPWSTR text = nullptr;

		const auto length = ::FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr, static_cast<DWORD>(hr), 0, reinterpret_cast<LPWSTR>(&text), 0, nullptr);

		auto message = length > 0 && text ? str::trim(str::utf16_to_utf8(std::wstring_view(text, length))) : std::string{};
		if (text) ::LocalFree(text);

		return message.empty()
			       ? std::format("{} failed (0x{:08x})", what, static_cast<uint32_t>(hr))
			       : std::format("{} failed (0x{:08x}): {}", what, static_cast<uint32_t>(hr), message);
	}

	// MFStartup and MFShutdown are refcounted by hand because a render and a capability probe can
	// overlap, and the second MFShutdown would pull the platform out from under the first.
	class mf_session
	{
	public:
		static bool acquire()
		{
			std::lock_guard lock(_cs);

			if (_count == 0)
			{
				const auto hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);

				if (FAILED(hr))
				{
					df::log(__FUNCTION__, format_hresult("MFStartup", hr));
					return false;
				}
			}

			++_count;
			return true;
		}

		static void release()
		{
			std::lock_guard lock(_cs);

			if (_count > 0 && --_count == 0)
			{
				MFShutdown();
			}
		}

	private:
		static std::mutex _cs;
		static int _count;
	};

	std::mutex mf_session::_cs;
	int mf_session::_count = 0;

	class win_movie_writer final : public platform::movie_writer
	{
	public:
		explicit win_movie_writer(const platform::movie_writer_request& request) : _request(request)
		{
		}

		~win_movie_writer() override
		{
			abandon();
		}

		bool open()
		{
			if (_request.extent.cx <= 0 || _request.extent.cy <= 0 || _request.frame_rate <= 0)
			{
				_error = "invalid movie geometry";
				return false;
			}

			if (!mf_session::acquire())
			{
				_error = "Media Foundation is unavailable";
				return false;
			}

			_session = true;

			ComPtr<IMFAttributes> attributes;

			if (!check(MFCreateAttributes(&attributes, 2), "MFCreateAttributes")) return false;

			// Without this the sink writer uses the software encoder even where a GPU one exists,
			// which is the difference between minutes and tens of minutes on a long movie.
			attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
			attributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);

			const auto native = platform::to_file_system_path(_request.path);

			if (!check(MFCreateSinkWriterFromURL(native.c_str(), nullptr, attributes.Get(), &_writer),
			           "MFCreateSinkWriterFromURL"))
			{
				return false;
			}

			_created_file = true;

			return open_video() && (!_request.with_audio || open_audio()) && begin();
		}

		bool write_frame(const ui::const_surface_ptr& surface, const double time) override
		{
			if (!_writing || !surface || surface->empty()) return false;

			const auto extent = surface->dimensions();

			if (extent.cx != _request.extent.cx || extent.cy != _request.extent.cy)
			{
				_error = "frame does not match the movie geometry";
				return false;
			}

			const auto row_bytes = static_cast<DWORD>(extent.cx) * 4;
			const auto total = row_bytes * static_cast<DWORD>(extent.cy);

			ComPtr<IMFMediaBuffer> buffer;
			if (!check(MFCreateMemoryBuffer(total, &buffer), "MFCreateMemoryBuffer")) return false;

			BYTE* dest = nullptr;
			if (!check(buffer->Lock(&dest, nullptr, nullptr), "IMFMediaBuffer::Lock")) return false;

			// Copied row by row because the surface stride is its own; the buffer is packed.
			for (auto y = 0; y < extent.cy; ++y)
			{
				memcpy(dest + static_cast<size_t>(y) * row_bytes, surface->pixels_line(y), row_bytes);
			}

			buffer->Unlock();

			if (!check(buffer->SetCurrentLength(total), "IMFMediaBuffer::SetCurrentLength")) return false;

			const auto duration = hns_per_second / _request.frame_rate;
			return write_sample(_video_stream, buffer.Get(), to_hns(time), duration);
		}

		bool write_audio(const int16_t* samples, const size_t sample_count, const double time) override
		{
			if (!_writing || !_request.with_audio || !samples || sample_count == 0) return false;

			const auto total = static_cast<DWORD>(sample_count * sizeof(int16_t));

			ComPtr<IMFMediaBuffer> buffer;
			if (!check(MFCreateMemoryBuffer(total, &buffer), "MFCreateMemoryBuffer")) return false;

			BYTE* dest = nullptr;
			if (!check(buffer->Lock(&dest, nullptr, nullptr), "IMFMediaBuffer::Lock")) return false;
			memcpy(dest, samples, total);
			buffer->Unlock();

			if (!check(buffer->SetCurrentLength(total), "IMFMediaBuffer::SetCurrentLength")) return false;

			const auto frames = sample_count / std::max(1, _request.channels);
			const auto duration = static_cast<int64_t>(frames) * hns_per_second /
				std::max(1, _request.sample_rate);

			return write_sample(_audio_stream, buffer.Get(), to_hns(time), duration);
		}

		bool close() override
		{
			if (!_writer || !_writing) return false;

			_writing = false;
			const auto ok = check(_writer->Finalize(), "IMFSinkWriter::Finalize");

			_writer.Reset();

			if (ok) _created_file = false; // The file is the caller's now.
			release_session();
			return ok;
		}

		void abandon() override
		{
			_writing = false;
			_writer.Reset();

			if (_created_file)
			{
				_created_file = false;
				platform::delete_file(_request.path);
			}

			release_session();
		}

		std::string last_error() const override { return _error; }

	private:
		bool open_video()
		{
			ComPtr<IMFMediaType> out;
			if (!check(MFCreateMediaType(&out), "MFCreateMediaType")) return false;

			out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
			out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
			out->SetUINT32(MF_MT_AVG_BITRATE, static_cast<UINT32>(std::max(1, _request.video_bitrate)));
			out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
			out->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High);
			MFSetAttributeSize(out.Get(), MF_MT_FRAME_SIZE, _request.extent.cx, _request.extent.cy);
			MFSetAttributeRatio(out.Get(), MF_MT_FRAME_RATE, _request.frame_rate, 1);
			MFSetAttributeRatio(out.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

			if (!check(_writer->AddStream(out.Get(), &_video_stream), "AddStream(video)")) return false;

			ComPtr<IMFMediaType> in;
			if (!check(MFCreateMediaType(&in), "MFCreateMediaType")) return false;

			in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
			in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
			in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
			// A positive stride declares top-down. RGB32 defaults to bottom-up in Media Foundation,
			// and without this every frame of the movie is upside down.
			in->SetUINT32(MF_MT_DEFAULT_STRIDE, static_cast<UINT32>(_request.extent.cx * 4));
			MFSetAttributeSize(in.Get(), MF_MT_FRAME_SIZE, _request.extent.cx, _request.extent.cy);
			MFSetAttributeRatio(in.Get(), MF_MT_FRAME_RATE, _request.frame_rate, 1);
			MFSetAttributeRatio(in.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

			return check(_writer->SetInputMediaType(_video_stream, in.Get(), nullptr),
			             "SetInputMediaType(video)");
		}

		bool open_audio()
		{
			const auto channels = static_cast<UINT32>(std::max(1, _request.channels));
			const auto rate = static_cast<UINT32>(std::max(8000, _request.sample_rate));

			ComPtr<IMFMediaType> out;
			if (!check(MFCreateMediaType(&out), "MFCreateMediaType")) return false;

			out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
			out->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
			out->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
			out->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
			out->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
			out->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
			               static_cast<UINT32>(std::max(1, _request.audio_bitrate) / 8));

			if (!check(_writer->AddStream(out.Get(), &_audio_stream), "AddStream(audio)")) return false;

			ComPtr<IMFMediaType> in;
			if (!check(MFCreateMediaType(&in), "MFCreateMediaType")) return false;

			in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
			in->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
			in->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
			in->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
			in->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
			in->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, channels * 2);
			in->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, rate * channels * 2);
			in->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);

			return check(_writer->SetInputMediaType(_audio_stream, in.Get(), nullptr),
			             "SetInputMediaType(audio)");
		}

		bool begin()
		{
			if (!check(_writer->BeginWriting(), "IMFSinkWriter::BeginWriting")) return false;
			_writing = true;
			return true;
		}

		bool write_sample(const DWORD stream, IMFMediaBuffer* buffer, const int64_t time,
		                  const int64_t duration)
		{
			ComPtr<IMFSample> sample;
			if (!check(MFCreateSample(&sample), "MFCreateSample")) return false;
			if (!check(sample->AddBuffer(buffer), "IMFSample::AddBuffer")) return false;
			if (!check(sample->SetSampleTime(time), "IMFSample::SetSampleTime")) return false;
			if (!check(sample->SetSampleDuration(duration), "IMFSample::SetSampleDuration")) return false;

			return check(_writer->WriteSample(stream, sample.Get()), "IMFSinkWriter::WriteSample");
		}

		// One place records the failure, so a render that stops has a reason to report rather than
		// just an absent file.
		bool check(const HRESULT hr, const std::string_view what)
		{
			if (SUCCEEDED(hr)) return true;

			_error = format_hresult(what, hr);
			df::log(__FUNCTION__, _error);
			return false;
		}

		void release_session()
		{
			if (!_session) return;
			_session = false;
			mf_session::release();
		}

		platform::movie_writer_request _request;
		ComPtr<IMFSinkWriter> _writer;
		DWORD _video_stream = 0;
		DWORD _audio_stream = 0;
		std::string _error;
		bool _session = false;
		bool _created_file = false;
		bool _writing = false;
	};
}

platform::movie_writer_ptr platform::create_movie_writer(const movie_writer_request& request)
{
	auto writer = std::make_shared<win_movie_writer>(request);

	if (!writer->open())
	{
		df::log(__FUNCTION__, std::format("cannot write {}: {}", request.path.name().sv(), writer->last_error()));
		writer->abandon();
		return {};
	}

	return writer;
}

bool platform::can_write_movies()
{
	// Probed rather than assumed: the H.264 encoder is a Windows component that an N edition or a
	// stripped image can be missing, and the answer belongs to the machine, not to the build.
	// Cached because the probe builds and discards a real sink writer.
	static const auto answer = []
	{
		if (!mf_session::acquire()) return false;

		ComPtr<IMFMediaType> type;
		auto ok = SUCCEEDED(MFCreateMediaType(&type));

		if (ok)
		{
			type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
			type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
			type->SetUINT32(MF_MT_AVG_BITRATE, 6000000);
			type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
			MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, 1920, 1080);
			MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, 30, 1);

			IMFActivate** activates = nullptr;
			UINT32 count = 0;

			MFT_REGISTER_TYPE_INFO output{MFMediaType_Video, MFVideoFormat_H264};

			ok = SUCCEEDED(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER,
			                         MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SYNCMFT |
			                         MFT_ENUM_FLAG_ASYNCMFT | MFT_ENUM_FLAG_SORTANDFILTER,
			                         nullptr, &output, &activates, &count)) && count > 0;

			for (UINT32 i = 0; i < count; ++i) activates[i]->Release();
			CoTaskMemFree(activates);
		}

		mf_session::release();

		if (!ok) df::log(__FUNCTION__, "no H.264 encoder on this system; Render is not offered");
		return ok;
	}();

	return answer;
}
