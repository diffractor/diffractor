// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

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

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient = __uuidof(IAudioClient);
const IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);
const IID IID_ISimpleAudioVolume = __uuidof(ISimpleAudioVolume);
const IID IID_IAudioClock = __uuidof(IAudioClock);


#include "av_sound.h"

static const GUID SDL_KSDATAFORMAT_SUBTYPE_PCM = {
	0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
};
static const GUID SDL_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT = {
	0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
};

#define DEFINE_PROPERTYKEY2(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8, pid) EXTERN_C const PROPERTYKEY DECLSPEC_SELECTANY name = { { l, w1, w2, { b1, b2,  b3,  b4,  b5,  b6,  b7,  b8 } }, pid }

DEFINE_PROPERTYKEY2(PKEY_AudioEngine_DeviceFormat, 0xf19f064d, 0x82c, 0x4e27, 0xbc, 0x73, 0x68, 0x82, 0xa1, 0xbb, 0x8e,
                    0x4c, 0);

static prop::audio_sample_t calc_sample_fmt(const WAVEFORMATEX* waveformat)
{
	if ((waveformat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) && (waveformat->wBitsPerSample == 32))
	{
		return prop::audio_sample_t::signed_float;
	}
	if ((waveformat->wFormatTag == WAVE_FORMAT_PCM) && (waveformat->wBitsPerSample == 16))
	{
		return prop::audio_sample_t::signed_16bit;
	}
	if ((waveformat->wFormatTag == WAVE_FORMAT_PCM) && (waveformat->wBitsPerSample == 32))
	{
		return prop::audio_sample_t::signed_32bit;
	}
	if (waveformat->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
	{
		const auto* ext = std::bit_cast<const WAVEFORMATEXTENSIBLE*>(waveformat);

		if ((memcmp(&ext->SubFormat, &SDL_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, sizeof(GUID)) == 0) && (waveformat->
			wBitsPerSample == 32))
		{
			return prop::audio_sample_t::signed_float;
		}
		if ((memcmp(&ext->SubFormat, &SDL_KSDATAFORMAT_SUBTYPE_PCM, sizeof(GUID)) == 0) && (waveformat->wBitsPerSample
			== 16))
		{
			return prop::audio_sample_t::signed_16bit;
		}
		if ((memcmp(&ext->SubFormat, &SDL_KSDATAFORMAT_SUBTYPE_PCM, sizeof(GUID)) == 0) && (waveformat->wBitsPerSample
			== 32))
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

using device_map = std::vector<std::pair<_GUID, std::u8string>>;

class audio_device_enumerator : public df::no_copy
{
private:
	device_map _devices;

public:
	audio_device_enumerator()
	{
		enum_devices();
	}

	bool has_multiple_audio_devices() const
	{
		return _devices.size() > 1;
	}

	const device_map& devices() const
	{
		return _devices;
	}

private:
	static BOOL CALLBACK DSEnumProc(LPGUID lpGUID,
	                                LPCTSTR lpszDesc,
	                                LPCTSTR lpszDrvName,
	                                LPVOID lpContext)
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
		//DirectSoundEnumerate(DSEnumProc, &_devices);
	}
};


class wasapi_sound : public av_audio_device
{
private:
	mutable platform::mutex _mutex;
	device_map _devices;

	ComPtr<IMMDevice> _device;
	ComPtr<IAudioClient> _audio;
	ComPtr<IAudioRenderClient> _render;
	ComPtr<ISimpleAudioVolume> _sav;
	ComPtr<IAudioClock> _clock;

	//ComPtr<IAudioRenderClient> _render;
	//ComPtr<IMMDeviceEnumerator> _enumerator;

	WAVEFORMATEX* _pwfx = nullptr;
	bool _device_lost = false;
	bool _playing = false;
	double _vol = -1;

public:
	wasapi_sound()
	{
		platform::exclusive_lock lock(_mutex);
		//data_event.create(false, false);
	}

	virtual ~wasapi_sound()
	{
		clear();
	}

	void clear()
	{
		platform::exclusive_lock lock(_mutex);

		if (_audio)
		{
			_audio->Stop();
		}

		_audio.Reset();
		_device.Reset();
		_render.Reset();
		_sav.Reset();
		_clock.Reset();

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
		bool success = false;

		ComPtr<IAudioClient> audio;
		ComPtr<IAudioRenderClient> render;
		ComPtr<ISimpleAudioVolume> sav;
		ComPtr<IAudioClock> clock;

		auto hr = device->Activate(
			IID_IAudioClient, CLSCTX_ALL,
			nullptr, &audio);


		if (SUCCEEDED(hr))
		{
			//hr = get_stream_format(_device, _audio, &_pwfx);
			hr = audio->GetMixFormat(&_pwfx);

			if (SUCCEEDED(hr))
			{
				hr = audio->Initialize(
					AUDCLNT_SHAREMODE_SHARED,
					0, //AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST,
					REFTIMES_PER_SEC,
					0, //REFTIMES_PER_SEC,
					_pwfx,
					nullptr);

				// KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
				// WAVE_FORMAT_PCM
				// WAVE_FORMAT_EXTENSIBLE

				/*if (SUCCEEDED(hr))
						{
							hr = _audio->SetEventHandle(std::any_cast<HANDLE>(data_event._h));

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
					success = SUCCEEDED(audio->GetService(IID_IAudioRenderClient, &render)) &&
						SUCCEEDED(audio->GetService(IID_ISimpleAudioVolume, &sav)) &&
						SUCCEEDED(audio->GetService(IID_IAudioClock, &clock));
				}
			}

			if (success)
			{
				_device = device;
				_audio = audio;
				_render = render;
				_sav = sav;
				_clock = clock;
			}
		}

		return success;
	}

	bool init(const std::u8string_view device_id)
	{
		clear();

		platform::exclusive_lock lock(_mutex);
		bool success = false;
		//LPCGUID p_device_id = nullptr;
		//GUID device_guid;

		//if (!device_id.empty())
		//{
		//	str::hex_to_data(device_id, std::bit_cast<uint8_t*>(&device_guid), sizeof(GUID));
		//	p_device_id = &device_guid;
		//}

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
			}
		}

		return success;
	}

	audio_info_t format() override
	{
		platform::exclusive_lock lock(_mutex);
		audio_info_t result;

		if (_pwfx)
		{
			result.sample_rate = _pwfx->nSamplesPerSec;
			result.sample_fmt = calc_sample_fmt(_pwfx);
			result.channel_layout = av_get_def_channel_layout(_pwfx->nChannels);
		}

		return result;
	}

	double time() const override
	{
		platform::shared_lock lock(_mutex);

		if (_clock)
		{
			uint64_t device_frequency = 0;
			uint64_t position = 0;

			if (SUCCEEDED(_clock->GetFrequency(&device_frequency)) &&
				SUCCEEDED(_clock->GetPosition(&position, nullptr)))
			{
				return device_frequency > 0 ? (static_cast<double>(position) / static_cast<double>(device_frequency)) : 0.0;
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
		platform::shared_lock lock(_mutex);

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
		platform::shared_lock lock(_mutex);

		if (_audio)
		{
			_audio->Stop();
			_audio->Reset();
			_playing = false;
		}
	}

	void start() override
	{
		platform::shared_lock lock(_mutex);

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

	void update_status(HRESULT hr)
	{
		if (hr == AUDCLNT_E_DEVICE_INVALIDATED)
		{
			_device_lost = true;
		}
	}

	void write(audio_buffer& buffer) override
	{
		platform::shared_lock lock(_mutex);

		if (_render)
		{
			const auto bytes_per_sample = buffer.bytes_per_sample();
			const auto channel_count = buffer.format.channel_count();
			const auto available_in_buffer = buffer.used_bytes() / (bytes_per_sample * channel_count);

			if (available_in_buffer > 0)
			{
				uint32_t bufferFrameCount = 0;
				uint32_t numFramesPadding = 0;
				uint8_t* pData = nullptr;

				auto hr = _audio->GetBufferSize(&bufferFrameCount);

				if (SUCCEEDED(hr))
				{
					hr = _audio->GetCurrentPadding(&numFramesPadding);

					if (SUCCEEDED(hr))
					{
						const auto copy_samples = std::min(bufferFrameCount - numFramesPadding, available_in_buffer);

						hr = _render->GetBuffer(copy_samples, &pData);

						if (SUCCEEDED(hr))
						{
							const auto copy_bytes = copy_samples * bytes_per_sample * channel_count;

							memcpy(pData, buffer.data, copy_bytes);
							buffer.remove(copy_bytes);

							const uint32_t flags = 0;
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
	}

	void volume(double vol) override
	{
		if (_sav && !df::equiv(_vol, vol))
		{
			const auto hr = _sav->SetMasterVolume(static_cast<float>(vol), nullptr);
			_vol = vol;

			if (FAILED(hr))
			{
				update_status(hr);
			}
		}
	}

	std::u8string id() override
	{
		std::u8string result;

		if (_device)
		{
			LPWSTR pwszID = nullptr;
			const auto hr = _device->GetId(&pwszID);

			if (SUCCEEDED(hr))
			{
				result = str::utf16_to_utf8(pwszID);
				CoTaskMemFree(pwszID);
				pwszID = nullptr;
			}
		}

		return result;
	}
};

av_audio_device_ptr create_av_audio_device(const std::u8string_view device_id)
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
					printf("No endpoints found.\n");
				}

				// Each loop prints the name of an endpoint device.
				for (ULONG i = 0; i < count; i++)
				{
					ComPtr<IMMDevice> pEndpoint;
					// Get pointer to endpoint number i.
					hr = pCollection->Item(i, &pEndpoint);

					if (SUCCEEDED(hr))
					{
						LPWSTR pwszID = nullptr;
						hr = pEndpoint->GetId(&pwszID);

						if (SUCCEEDED(hr))
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
									d.name = str::is_empty(varName.v.pwszVal) ? str::format(u8"Audio device {}", static_cast<int>(i)) : str::utf16_to_utf8(varName.v.pwszVal);
								}
							}

							CoTaskMemFree(pwszID);
							pwszID = nullptr;

							result.emplace_back(d);
						}
					}
				}
			}
		}
	}

	return result;
}
