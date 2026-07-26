// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Windows audio output using WASAPI. Manages audio device selection,
// buffer playback, and audio stream synchronization.

#include "pch.h"
#include "platform_win.h"

#include <mmdeviceapi.h>
#include <Audioclient.h>
#include <functiondiscoverykeys_devpkey.h>


#define __restrict__
#define __STDC_CONSTANT_MACROS
#define FF_API_PIX_FMT 0


#define REFTIMES_PER_SEC  10000000
#define REFTIMES_PER_MILLISEC  10000

const auto CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const auto IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const auto IID_IAudioClient = __uuidof(IAudioClient);
const auto IID_IAudioRenderClient = __uuidof(IAudioRenderClient);
const auto IID_ISimpleAudioVolume = __uuidof(ISimpleAudioVolume);
const auto IID_IAudioClock = __uuidof(IAudioClock);


#include "av_sound.h"

static constexpr GUID SDL_KSDATAFORMAT_SUBTYPE_PCM = {
	0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
};
static constexpr GUID SDL_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT = {
	0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
};

#define DEFINE_PROPERTYKEY2(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8, pid) EXTERN_C const PROPERTYKEY DECLSPEC_SELECTANY name = { { l, w1, w2, { b1, b2,  b3,  b4,  b5,  b6,  b7,  b8 } }, pid }

DEFINE_PROPERTYKEY2(PKEY_AudioEngine_DeviceFormat, 0xf19f064d, 0x82c, 0x4e27, 0xbc, 0x73, 0x68, 0x82, 0xa1, 0xbb, 0x8e,
                    0x4c, 0);

static prop::audio_sample_t calc_sample_fmt(const WAVEFORMATEX* waveformat)
{
	if (!waveformat)
	{
		return prop::audio_sample_t::none;
	}

	if (waveformat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT && waveformat->wBitsPerSample == 32)
	{
		return prop::audio_sample_t::signed_float;
	}
	if (waveformat->wFormatTag == WAVE_FORMAT_PCM && waveformat->wBitsPerSample == 16)
	{
		return prop::audio_sample_t::signed_16bit;
	}
	if (waveformat->wFormatTag == WAVE_FORMAT_PCM && waveformat->wBitsPerSample == 32)
	{
		return prop::audio_sample_t::signed_32bit;
	}
	if (waveformat->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
	{
		if (waveformat->cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
		{
			// Invalid extensible format
			return prop::audio_sample_t::none;
		}

		const auto* ext = std::bit_cast<const WAVEFORMATEXTENSIBLE*>(waveformat);

		if (memcmp(&ext->SubFormat, &SDL_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, sizeof(GUID)) == 0 && waveformat->
			wBitsPerSample == 32)
		{
			return prop::audio_sample_t::signed_float;
		}
		if (memcmp(&ext->SubFormat, &SDL_KSDATAFORMAT_SUBTYPE_PCM, sizeof(GUID)) == 0 && waveformat->wBitsPerSample
			== 16)
		{
			return prop::audio_sample_t::signed_16bit;
		}
		if (memcmp(&ext->SubFormat, &SDL_KSDATAFORMAT_SUBTYPE_PCM, sizeof(GUID)) == 0 && waveformat->wBitsPerSample
			== 32)
		{
			return prop::audio_sample_t::signed_32bit;
		}
	}

	return prop::audio_sample_t::none;
}

struct guid_comparer
{
	bool operator()(const GUID& Left, const GUID& Right) const
	{
		// comparison logic goes here
		return memcmp(&Left, &Right, sizeof(Right)) < 0;
	}
};

using device_map = std::vector<std::pair<_GUID, std::string>>;

class audio_device_enumerator final : public df::no_copy
{
	device_map _devices;

public:
	audio_device_enumerator()
	{
		enum_devices();
	}

	const device_map& devices() const
	{
		return _devices;
	}

private:
	static BOOL CALLBACK DSEnumProc(const LPGUID lpGUID,
	                                const LPCTSTR lpszDesc,
	                                LPCTSTR lpszDrvName,
	                                const LPVOID lpContext)
	{
		auto* const map = std::bit_cast<device_map*>(lpContext);
		const auto id = lpGUID == nullptr ? GUID_NULL : *lpGUID;
		const auto name = str::cache(lpszDesc == nullptr ? L"Default" : lpszDesc);

		map->emplace_back(id, name);
		return TRUE;
	}

	void enum_devices()
	{
		_devices.clear();
	}
};


class wasapi_sound final : public av_audio_device
{
	mutable platform::mutex _rw;
	_Guarded_by_(_rw) device_map _devices;

	_Guarded_by_(_rw) ComPtr<IMMDevice> _device;
	_Guarded_by_(_rw) ComPtr<IAudioClient> _audio;
	_Guarded_by_(_rw) ComPtr<IAudioRenderClient> _render;
	_Guarded_by_(_rw) ComPtr<ISimpleAudioVolume> _sav;
	_Guarded_by_(_rw) ComPtr<IAudioClock> _clock;
	_Guarded_by_(_rw) WAVEFORMATEX* _pwfx = nullptr;
	_Guarded_by_(_rw) uint32_t _buffer_frame_count = 0;
	platform::thread_event _data_event{false, false};
	// Read without a lock by is_device_lost()/is_stopped() and written from the device
	// operations below, so these are atomic rather than plain bools.
	std::atomic<bool> _device_lost = false;
	std::atomic<bool> _playing = false;
	double _vol = -1;

public:
	wasapi_sound() = default;

	virtual ~wasapi_sound()
	{
		clear();
	}

	void clear()
	{
		platform::exclusive_lock lock(_rw);

		if (_audio)
		{
			_audio->Stop();
		}

		_audio.Reset();
		_device.Reset();
		_render.Reset();
		_sav.Reset();
		_clock.Reset();
		_buffer_frame_count = 0;
		_data_event.reset();

		if (_pwfx)
		{
			CoTaskMemFree(_pwfx);
			_pwfx = nullptr;
		}

		_device_lost = false;
		_playing = false;
		_vol = -1;
	}

	bool is_device_lost() const override
	{
		return _device_lost;
	}

	bool activate_device(const ComPtr<IMMDevice>& device)
	{
		if (!device)
		{
			return false;
		}

		bool success = false;

		ComPtr<IAudioClient> audio;
		ComPtr<IAudioRenderClient> render;
		ComPtr<ISimpleAudioVolume> sav;
		ComPtr<IAudioClock> clock;
		WAVEFORMATEX* pwfx_temp = nullptr;
		uint32_t buffer_frame_count = 0;

		auto hr = device->Activate(
			IID_IAudioClient, CLSCTX_ALL,
			nullptr, &audio);

		if (SUCCEEDED(hr))
		{
			//hr = get_stream_format(_device, _audio, &_pwfx);
			hr = audio->GetMixFormat(&pwfx_temp);

			if (SUCCEEDED(hr) && pwfx_temp)
			{
				hr = audio->Initialize(
					AUDCLNT_SHAREMODE_SHARED,
					AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST,
					0,
					0,
					pwfx_temp,
					nullptr);

				// KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
				// WAVE_FORMAT_PCM
				// WAVE_FORMAT_EXTENSIBLE

				/*if (SUCCEEDED(hr))
						{
							hr = _audio->SetEventHandle(static_cast<HANDLE>(data_event._h));

							if (SUCCEEDED(hr))
							{
								hr = _audio->GetService(
									IID_IAudioRenderClient,
									(void**)&_render);

								success = SUCCEEDED(hr);
							}
						}*/

				if (SUCCEEDED(hr))
				{
					hr = audio->SetEventHandle(static_cast<HANDLE>(_data_event._h));
				}

				if (SUCCEEDED(hr))
				{
					success = SUCCEEDED(audio->GetBufferSize(&buffer_frame_count)) &&
						SUCCEEDED(audio->GetService(IID_IAudioRenderClient, &render)) &&
						SUCCEEDED(audio->GetService(IID_ISimpleAudioVolume, &sav)) &&
						SUCCEEDED(audio->GetService(IID_IAudioClock, &clock));
				}
			}

			if (success)
			{
				// Clean up existing format before assigning new one
				if (_pwfx)
				{
					CoTaskMemFree(_pwfx);
				}

				_device = device;
				_audio = audio;
				_render = render;
				_sav = sav;
				_clock = clock;
				_pwfx = pwfx_temp;
				_buffer_frame_count = buffer_frame_count;
				pwfx_temp = nullptr; // Ownership transferred
			}
			else if (pwfx_temp)
			{
				// Clean up on failure
				CoTaskMemFree(pwfx_temp);
			}
		}

		return success;
	}

	bool init(const std::string_view device_id)
	{
		clear();

		platform::exclusive_lock lock(_rw);
		bool success = false;
		ComPtr<IMMDeviceEnumerator> enumerator;

		auto hr = CoCreateInstance(
			CLSID_MMDeviceEnumerator, nullptr,
			CLSCTX_ALL, IID_IMMDeviceEnumerator,
			&enumerator);

		if (SUCCEEDED(hr))
		{
			ComPtr<IMMDevice> device;

			if (device_id.empty())
			{
				hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
			}
			else
			{
				const auto w = str::utf8_to_utf16(device_id);
				hr = enumerator->GetDevice(w.c_str(), &device);

				if (FAILED(hr))
				{
					hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
				}
			}

			if (SUCCEEDED(hr))
			{
				success = activate_device(device);

				if (!success)
				{
					// Try with a different device
					hr = enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);

					if (SUCCEEDED(hr))
					{
						success = activate_device(device);
					}
				}
			}
		}

		return success;
	}

	audio_info_t format() override
	{
		platform::exclusive_lock lock(_rw);
		audio_info_t result;

		if (_pwfx)
		{
			result.sample_rate = _pwfx->nSamplesPerSec;
			result.sample_fmt = calc_sample_fmt(_pwfx);
			uint64_t channel_mask = 0;

			if (_pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
				_pwfx->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
			{
				channel_mask = std::bit_cast<const WAVEFORMATEXTENSIBLE*>(_pwfx)->dwChannelMask;
			}

			result.channel_layout = av_get_channel_layout(channel_mask, _pwfx->nChannels);
		}

		return result;
	}

	double time() const override
	{
		platform::shared_lock lock(_rw);

		if (_clock)
		{
			uint64_t device_frequency = 0;
			uint64_t position = 0;

			if (SUCCEEDED(_clock->GetFrequency(&device_frequency)) &&
				SUCCEEDED(_clock->GetPosition(&position, nullptr)))
			{
				if (device_frequency == 0)
				{
					df::log("wasapi_sound::time", "Device frequency is zero, returning 0.0");
					return 0.0;
				}
				return static_cast<double>(position) / static_cast<double>(device_frequency);
			}
		}

		return 0.0;
	}

	bool is_stopped() const override
	{
		return !_playing;
	}

	void stop() override
	{
		platform::exclusive_lock lock(_rw);

		if (_audio)
		{
			const auto hr = _audio->Stop();
			_playing = false;

			if (FAILED(hr))
			{
				update_status(hr);
			}
		}
	}

	void reset() override
	{
		platform::exclusive_lock lock(_rw);

		if (_audio)
		{
			auto hr = _audio->Stop();
			if (FAILED(hr)) update_status(hr);
			hr = _audio->Reset();
			if (FAILED(hr)) update_status(hr);
			_playing = false;
			_data_event.reset();
		}
	}

	void start() override
	{
		platform::exclusive_lock lock(_rw);

		if (_audio)
		{
			const auto hr = _audio->Start();
			_playing = SUCCEEDED(hr);

			if (FAILED(hr))
			{
				update_status(hr);
			}
		}
	}

	void update_status(const HRESULT hr)
	{
		if (hr == AUDCLNT_E_DEVICE_INVALIDATED ||
			hr == AUDCLNT_E_RESOURCES_INVALIDATED ||
			hr == AUDCLNT_E_SERVICE_NOT_RUNNING ||
			hr == AUDCLNT_E_ENDPOINT_CREATE_FAILED)
		{
			_device_lost = true;
		}
	}

	void wait_for_buffer(const platform::thread_event& wake_event, const uint32_t timeout_ms) override
	{
		const HANDLE events[] = {
			static_cast<HANDLE>(_data_event._h),
			static_cast<HANDLE>(wake_event._h)
		};
		WaitForMultipleObjects(static_cast<DWORD>(std::size(events)), events, false, timeout_ms);
	}

	void write(audio_buffer& buffer) override
	{
		platform::exclusive_lock lock(_rw);

		if (!_render || !buffer.data)
		{
			return; // Early exit for invalid state
		}

		const auto bytes_per_sample = buffer.bytes_per_sample();
		const auto channel_count = buffer.format.channel_count();

		if (bytes_per_sample == 0 || channel_count == 0)
		{
			df::log("wasapi_sound::write", "Invalid audio format parameters");
			return;
		}

		const auto available_in_buffer = buffer.used_bytes() / (bytes_per_sample * channel_count);

		if (available_in_buffer > 0)
		{
			uint32_t numFramesPadding = 0;
			uint8_t* pData = nullptr;

			auto hr = _audio->GetCurrentPadding(&numFramesPadding);

			if (SUCCEEDED(hr))
			{
				const auto available_frames = numFramesPadding < _buffer_frame_count
					? _buffer_frame_count - numFramesPadding
					: 0u;
				const auto copy_samples = std::min(available_frames, available_in_buffer);

				if (copy_samples > 0)
				{
					hr = _render->GetBuffer(copy_samples, &pData);

					if (SUCCEEDED(hr))
				{
						const auto copy_bytes = copy_samples * bytes_per_sample * channel_count;

						if (copy_bytes <= buffer.used_bytes())
						{
							memcpy(pData, buffer.data + buffer.start_pos, copy_bytes);
							buffer.remove(copy_bytes);
						}
						else
						{
							df::log("wasapi_sound::write", "Copy bytes exceeds buffer size");
						}

						constexpr uint32_t flags = 0;
						hr = _render->ReleaseBuffer(copy_samples, flags);
					}
				}
			}

			if (FAILED(hr))
			{
				update_status(hr);
			}
		}
	}

	void write_silence() override
	{
		platform::exclusive_lock lock(_rw);

		if (!_render || !_audio)
		{
			return;
		}

		uint32_t numFramesPadding = 0;

		auto hr = _audio->GetCurrentPadding(&numFramesPadding);

		if (SUCCEEDED(hr))
		{
			// Fill the endpoint ring with silence so an underrun cannot replay stale
			// audio left in a driver-managed slot.
			const uint32_t target = _buffer_frame_count;

			if (numFramesPadding < target)
			{
				const auto frames = std::min<uint32_t>(target - numFramesPadding,
				                                       _buffer_frame_count - numFramesPadding);

				if (frames > 0)
				{
					uint8_t* pData = nullptr;
					hr = _render->GetBuffer(frames, &pData);

					if (SUCCEEDED(hr) && pData && _pwfx)
					{
						// Write actual zeros rather than relying on
						// AUDCLNT_BUFFERFLAGS_SILENT: GetBuffer hands back a slot in the
						// ring that still holds the last audio, and some drivers ignore
						// the SILENT flag and play it - which replays ("repeats") the last
						// sound buffer at the end of a clip, especially when the audio
						// stream ends before the video.
						memset(pData, 0, static_cast<size_t>(frames) * _pwfx->nBlockAlign);
						hr = _render->ReleaseBuffer(frames, 0);
					}
					else if (SUCCEEDED(hr))
					{
						hr = _render->ReleaseBuffer(frames, AUDCLNT_BUFFERFLAGS_SILENT);
					}
				}
			}
		}

		if (FAILED(hr))
		{
			update_status(hr);
		}
	}

	void volume(const double vol) override
	{
		platform::exclusive_lock lock(_rw);

		if (_sav && !df::equiv(_vol, vol))
		{
			const auto hr = _sav->SetMasterVolume(static_cast<float>(vol), nullptr);

			if (FAILED(hr))
			{
				update_status(hr);
			}
			else
			{
				_vol = vol;
			}
		}
	}

	std::string id() override
	{
		platform::shared_lock lock(_rw);
		std::string result;

		if (_device)
		{
			LPWSTR pwszID = nullptr;
			const auto hr = _device->GetId(&pwszID);

			if (SUCCEEDED(hr) && pwszID)
			{
				result = str::utf16_to_utf8(pwszID);
				CoTaskMemFree(pwszID);
				pwszID = nullptr;
			}
			else if (pwszID)
			{
				// Ensure memory is freed even on failure
				CoTaskMemFree(pwszID);
				pwszID = nullptr;
			}
		}

		return result;
	}
};

av_audio_device_ptr create_av_audio_device(const std::string_view device_id)
{
	auto result = std::make_shared<wasapi_sound>();

	if (result->init(device_id))
	{
		return result;
	}

	return nullptr;
}

std::vector<sound_device> list_audio_playback_devices()
{
	std::vector<sound_device> result;

	ComPtr<IMMDeviceEnumerator> pEnumerator;
	ComPtr<IMMDeviceCollection> pCollection;

	auto hr = CoCreateInstance(
		CLSID_MMDeviceEnumerator, nullptr,
		CLSCTX_ALL, IID_IMMDeviceEnumerator,
		&pEnumerator);

	if (SUCCEEDED(hr))
	{
		hr = pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pCollection);

		if (SUCCEEDED(hr))
		{
			UINT count;
			hr = pCollection->GetCount(&count);
			if (SUCCEEDED(hr))
			{
				if (count == 0)
				{
					df::log("list_audio_playback_devices", "No sound endpoints found.");
				}
				else if (count > 1000) // Sanity check to prevent excessive memory allocation
				{
					df::log("list_audio_playback_devices", "Unusually high device count, limiting to 1000");
					count = 1000;
				}

				for (ULONG i = 0; i < count; i++)
				{
					ComPtr<IMMDevice> pEndpoint;
					hr = pCollection->Item(i, &pEndpoint);

					if (SUCCEEDED(hr))
					{
						LPWSTR pwszID = nullptr;
						hr = pEndpoint->GetId(&pwszID);

						if (SUCCEEDED(hr) && pwszID)
						{
							sound_device d;
							d.id = str::utf16_to_utf8(pwszID);

							ComPtr<IPropertyStore> pProps;
							hr = pEndpoint->OpenPropertyStore(STGM_READ, &pProps);

							if (SUCCEEDED(hr))
							{
								prop_variant_t varName;
								hr = pProps->GetValue(PKEY_Device_FriendlyName, &varName.v);

								if (SUCCEEDED(hr))
								{
									d.name = str::is_empty(varName.v.pwszVal)
										         ? std::format("Audio device {}", static_cast<int>(i))
										         : str::utf16_to_utf8(varName.v.pwszVal);
								}
								else
								{
									d.name = std::format("Audio device {}", static_cast<int>(i));
								}
							}
							else
							{
								d.name = std::format("Audio device {}", static_cast<int>(i));
							}

							CoTaskMemFree(pwszID);
							pwszID = nullptr;

							result.emplace_back(d);
						}
						else if (pwszID)
						{
							// Ensure memory is freed even on failure
							CoTaskMemFree(pwszID);
							pwszID = nullptr;
						}
					}
				}
			}
		}
	}

	return result;
}
