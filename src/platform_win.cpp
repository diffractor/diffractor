// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Windows platform abstraction layer. Provides OS-specific implementations for
// file system, threading, networking, and shell integration.

#include "pch.h"

#include "platform_win.h"

#include <psapi.h>
#include <Shlwapi.h>
#include <commdlg.h>
#include <Thumbcache.h>
#include <shobjidl.h>   // SHGetPropertyStoreFromParsingName, etc
#include <propkey.h>    // PKEY_Music_AlbumArtist
#include <propvarutil.h>// InitPropVariantFromString, needs shlwapi.lib
#include <lm.h>
#include <WinIoCtl.h>
#include <SetupAPI.h>
#include <cfgmgr32.h>
#include <Shellapi.h>
#include <mapi.h>
#include <WinInet.h>
#include <ShlObj.h>
#include <wincodec.h> // WIC: HBITMAP -> surface conversion for shell thumbnails

#pragma comment(lib, "SetupAPI")
#pragma comment(lib, "Cfgmgr32")


#include "files.h"
#include "model.h"
#include "util.h"
#include "util_strings.h"

#ifdef WINSTORE
#include <roapi.h>
#include <windows.storage.h>
#include <windows.system.h>
#pragma comment(lib, "runtimeobject")
#endif

#include <avrt.h>
#pragma comment(lib, "avrt.lib")

// Values here come from third-party shell property handlers, so an unexpected type or a null
// string must yield an empty value rather than fault or assert.
str::cached cache_string_var(const PROPVARIANT& propVariant)
{
	std::wstring value;
	if (VT_VECTOR & propVariant.vt)
	{
		LPWSTR* strings = nullptr;
		ULONG numStrings = 0;
		if (SUCCEEDED(PropVariantToStringVectorAlloc(propVariant, &strings, &numStrings)))
		{
			for (ULONG index = 0; index < numStrings; index++)
			{
				if (strings[index])
				{
					if (!value.empty())
					{
						value += L", ";
					}
					value += strings[index];
					CoTaskMemFree(strings[index]);
				}
			}
			CoTaskMemFree(strings);
		}
	}
	else if (VT_LPWSTR == propVariant.vt && propVariant.pwszVal)
	{
		value = propVariant.pwszVal;
	}

	return str::cache(str::utf16_to_utf8(value));
}

// Maximum thumbnail size
static constexpr int max_thumbnail_bytes = 0x1000000;

platform::get_cached_file_properties_response platform::get_cached_file_properties(
	const df::file_path path, prop::item_metadata& properties_out, ui::const_image_ptr& thumbnail_out)
{
	auto result = get_cached_file_properties_response::fail;


	ComPtr<IShellItem2> item;
	HRESULT hr = SHCreateItemFromParsingName(to_shell_path(path).c_str(), nullptr /*bindContext*/,
	                                         IID_PPV_ARGS(&item));

	if (SUCCEEDED(hr))
	{
		constexpr GETPROPERTYSTOREFLAGS flags = GPS_DEFAULT;
		ComPtr<IPropertyStore> propStore;
		hr = item->GetPropertyStore(flags, IID_PPV_ARGS(&propStore));

		if (SUCCEEDED(hr))
		{
			result = get_cached_file_properties_response::ok;
			DWORD propCount = 0;
			hr = propStore->GetCount(&propCount);

			// GPS arrives as separate latitude/longitude values plus an N/S/E/W hemisphere ref;
			// collect them here and combine into a coordinate after the loop.
			bool has_gps_lat = false;
			bool has_gps_lon = false;
			double gps_lat = 0.0;
			double gps_lon = 0.0;
			std::wstring gps_lat_ref;
			std::wstring gps_lon_ref;

			if (SUCCEEDED(hr))
			{
				for (DWORD i = 0; i < propCount; i++)
				{
					PROPERTYKEY propKey = {0};
					if (SUCCEEDED(propStore->GetAt(i, &propKey)))
					{
						PROPVARIANT propVar;
						if (SUCCEEDED(propStore->GetValue(propKey, &propVar)))
						{
							if (PKEY_Music_AlbumArtist == propKey)
							{
								properties_out.album_artist = cache_string_var(propVar);
							}
							else if (PKEY_Music_Artist == propKey)
							{
								properties_out.artist = cache_string_var(propVar);
							}
							else if (PKEY_Title == propKey)
							{
								properties_out.title = cache_string_var(propVar);
							}
							else if (PKEY_Music_Genre == propKey)
							{
								properties_out.genre = cache_string_var(propVar);
							}
							else if (PKEY_Music_AlbumTitle == propKey)
							{
								properties_out.album = cache_string_var(propVar);
							}
							else if (PKEY_Comment == propKey)
							{
								properties_out.comment = cache_string_var(propVar);
							}
							else if (PKEY_Media_Year == propKey)
							{
								if (VT_UI4 == propVar.vt)
								{
									properties_out.year = static_cast<uint16_t>(propVar.ulVal);
								}
							}
							else if (PKEY_Music_TrackNumber == propKey)
							{
								if (VT_UI4 == propVar.vt)
								{
									properties_out.track.x = static_cast<int16_t>(propVar.ulVal);
								}
							}
							else if (PKEY_Image_HorizontalSize == propKey)
							{
								if (VT_UI4 == propVar.vt)
								{
									properties_out.width = static_cast<uint16_t>(std::min<ULONG>(
										propVar.ulVal, 0xffff));
								}
							}
							else if (PKEY_Image_VerticalSize == propKey)
							{
								if (VT_UI4 == propVar.vt)
								{
									properties_out.height = static_cast<uint16_t>(std::min<ULONG>(
										propVar.ulVal, 0xffff));
								}
							}
							else if (PKEY_Photo_Orientation == propKey)
							{
								if (VT_UI2 == propVar.vt && propVar.uiVal >= 1 && propVar.uiVal <= 8)
								{
									properties_out.orientation = static_cast<ui::orientation>(propVar.uiVal);
								}
							}
							else if (PKEY_Photo_DateTaken == propKey)
							{
								// PKEY_Photo_DateTaken is delivered as a UTC FILETIME. Store it as
								// created_utc so item_metadata::created() converts it to local time.
								if (VT_FILETIME == propVar.vt)
								{
									properties_out.created_utc = df::date_t(ft_to_ts(propVar.filetime));
								}
							}
							else if (PKEY_Keywords == propKey)
							{
								// Tags/keywords. OneDrive does not expose System.Keywords on placeholders
								// today, but read it for future support / other cloud providers, joined into
								// Diffractor's space-separated (quoted) tag form.
								LPWSTR* strings = nullptr;
								ULONG numStrings = 0;
								if (SUCCEEDED(PropVariantToStringVectorAlloc(propVar, &strings, &numStrings)))
								{
									std::vector<std::string> keywords;
									keywords.reserve(numStrings);

									for (ULONG s = 0; s < numStrings; ++s)
									{
										keywords.emplace_back(str::utf16_to_utf8(strings[s]));
										CoTaskMemFree(strings[s]);
									}

									CoTaskMemFree(strings);

									if (!keywords.empty() && is_empty(properties_out.tags))
									{
										properties_out.tags = str::cache(str::combine(keywords));
									}
								}
							}
							else if (PKEY_GPS_LatitudeDecimal == propKey)
							{
								if (VT_R8 == propVar.vt)
								{
									gps_lat = propVar.dblVal;
									has_gps_lat = true;
								}
							}
							else if (PKEY_GPS_LongitudeDecimal == propKey)
							{
								if (VT_R8 == propVar.vt)
								{
									gps_lon = propVar.dblVal;
									has_gps_lon = true;
								}
							}
							else if (PKEY_GPS_LatitudeRef == propKey)
							{
								if (VT_LPWSTR == propVar.vt && propVar.pwszVal)
								{
									gps_lat_ref = propVar.pwszVal;
								}
							}
							else if (PKEY_GPS_LongitudeRef == propKey)
							{
								if (VT_LPWSTR == propVar.vt && propVar.pwszVal)
								{
									gps_lon_ref = propVar.pwszVal;
								}
							}
							else if (PKEY_ThumbnailStream == propKey || PKEY_Thumbnail == propKey)
							{
								if (VT_STREAM == propVar.vt)
								{
									IStream* stream = propVar.pStream;
									if (nullptr != stream)
									{
										STATSTG stats = {};
										if (SUCCEEDED(stream->Stat(&stats, STATFLAG_NONAME)))
										{
											const auto thumbnail_size = stats.cbSize.QuadPart;

											if (thumbnail_size > 0 && thumbnail_size <= max_thumbnail_bytes)
											{
												df::blob blob;
												blob.resize(static_cast<size_t>(thumbnail_size));
												size_t total_read = 0;

												// Read is allowed to return short without failing, so keep
												// asking until it stops making progress.
												while (total_read < blob.size())
												{
													ULONG bytes_read = 0;

													if (FAILED(stream->Read(blob.data() + total_read,
													                        static_cast<ULONG>(blob.size() - total_read),
													                        &bytes_read)) || bytes_read == 0)
													{
														break;
													}

													total_read += bytes_read;
												}

												// Never decode bytes the stream did not supply.
												if (total_read == blob.size())
												{
													thumbnail_out = load_image_file(blob);
												}
											}
										}
									}
								}
							}
							PropVariantClear(&propVar);
						}
					}
				}
			}

			// Combine any GPS latitude/longitude read above into a coordinate. The N/S/E/W ref decides
			// the hemisphere; if it is absent we fall back to the sign already on the decimal value.
			if (has_gps_lat && has_gps_lon)
			{
				auto lat = std::abs(gps_lat);
				auto lon = std::abs(gps_lon);
				if (gps_lat_ref.empty()) lat = gps_lat;
				else if (gps_lat_ref == L"S" || gps_lat_ref == L"s") lat = -lat;
				if (gps_lon_ref.empty()) lon = gps_lon;
				else if (gps_lon_ref == L"W" || gps_lon_ref == L"w") lon = -lon;
				properties_out.coordinate = gps_coordinate(lat, lon);
			}
		}
	}

	return result;
}

platform::get_cached_file_properties_response platform::get_shell_thumbnail(
	const df::file_path path, const sizei requested_extent, const bool allow_network,
	ui::const_image_ptr& thumbnail_out)
{
	// Fetch a thumbnail via IShellItemImageFactory -- the same mechanism Windows Explorer uses.
	// For a OneDrive (Files On-Demand) placeholder this returns the cloud provider's thumbnail
	// WITHOUT hydrating (downloading) the full file. Verified empirically that on-disk bytes stay 0.
	// allow_network=false adds SIIGBF_INCACHEONLY (local thumbnail cache only, instant, never hits
	// the network); with allow_network=true a cache miss may fetch the thumbnail from the cloud.
	auto result = get_cached_file_properties_response::fail;

	// SHCreateItemFromParsingName rejects the \\?\ prefix, so this is a shell path even when it is long.
	const auto path_w = to_shell_path(path);

	ComPtr<IShellItemImageFactory> factory;
	auto hr = SHCreateItemFromParsingName(path_w.c_str(), nullptr, IID_PPV_ARGS(&factory));

	if (FAILED(hr))
	{
		return result;
	}

	const auto extent = std::max({requested_extent.cx, requested_extent.cy, 32});
	const SIZE size{extent, extent};

	// NOTE: do NOT use SIIGBF_THUMBNAILONLY here. That flag only returns an already-cached
	// thumbnail and fails (HR 0x8004B205) for a not-yet-cached cloud placeholder, so almost
	// nothing would load. Letting the shell ask the cloud provider's thumbnail handler returns the
	// real thumbnail and -- verified empirically on OneDrive -- does NOT hydrate the file.
	// SIIGBF_INCACHEONLY (when the network is not allowed) keeps it to the instant local cache.
	int flags = SIIGBF_RESIZETOFIT;
	if (!allow_network) flags |= SIIGBF_INCACHEONLY;

	HBITMAP hbitmap = nullptr;
	hr = factory->GetImage(size, flags, &hbitmap);

	const df::scope_exit free_bitmap([&hbitmap] { if (hbitmap) DeleteObject(hbitmap); });

	if (hr == WTS_E_EXTRACTIONPENDING || hr == E_PENDING)
	{
		// Not in the local cache yet (and either network not allowed, or the fetch is in flight).
		return get_cached_file_properties_response::pending;
	}

	if (FAILED(hr) || hbitmap == nullptr)
	{
		return result;
	}

	// Convert the HBITMAP to a 32bpp BGRA surface via WIC, then encode it so the caller receives the
	// same ui::image type as the property-store thumbnail path.
	ComPtr<IWICImagingFactory> wic;
	hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));

	ComPtr<IWICBitmap> wic_bitmap;
	if (SUCCEEDED(hr))
	{
		hr = wic->CreateBitmapFromHBITMAP(hbitmap, nullptr, WICBitmapUseAlpha, &wic_bitmap);
	}

	ComPtr<IWICFormatConverter> converter;
	if (SUCCEEDED(hr))
	{
		hr = wic->CreateFormatConverter(&converter);
	}

	if (SUCCEEDED(hr))
	{
		hr = converter->Initialize(wic_bitmap.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
		                           nullptr, 0.0f, WICBitmapPaletteTypeMedianCut);
	}

	uint32_t w = 0;
	uint32_t h = 0;
	if (SUCCEEDED(hr))
	{
		hr = converter->GetSize(&w, &h);
	}

	if (SUCCEEDED(hr) && w > 0 && h > 0)
	{
		const auto surface = std::make_shared<ui::surface>();

		if (surface->alloc(static_cast<int>(w), static_cast<int>(h), ui::texture_format::ARGB,
		                   ui::orientation::top_left) &&
			SUCCEEDED(converter->CopyPixels(nullptr, static_cast<uint32_t>(surface->stride()),
				static_cast<uint32_t>(surface->size()), surface->pixels())))
		{
			// Reject the shell's generic file-type icon (returned while OneDrive has not yet
			// produced a cloud thumbnail). A real photo/video thumbnail is fully opaque; the generic
			// icon is drawn on a transparent background. Measuring the fraction of fully transparent
			// pixels cleanly separates them (empirically: thumbnail ~0.00, generic icon ~0.45). When
			// it is only an icon we return 'pending' so the item keeps its own placeholder and can be
			// retried later, once the cloud thumbnail becomes available.
			const auto* const px = surface->pixels();
			const auto stride = surface->stride();
			size_t transparent = 0;
			size_t sampled = 0;

			for (uint32_t y = 0; y < h; y += 2)
			{
				const auto* const row = px + static_cast<size_t>(y) * stride;

				for (uint32_t x = 0; x < w; x += 2)
				{
					if (row[static_cast<size_t>(x) * 4 + 3] == 0) ++transparent;
					++sampled;
				}
			}

			const auto transparent_fraction = sampled ? static_cast<double>(transparent) / sampled : 1.0;

			if (transparent_fraction > 0.10)
			{
				return get_cached_file_properties_response::pending;
			}

			files ff;
			auto image = ff.surface_to_thumbnail(surface);

			if (is_valid(image))
			{
				thumbnail_out = std::move(image);
				result = get_cached_file_properties_response::ok;
			}
		}
	}

	return result;
}


platform::drives platform::scan_drives()
{
	drives results;
	const auto drives = GetLogicalDrives();

	// Probing a drive with no media raises a system modal ("There is no disk in the drive") unless
	// hard-error reporting is suppressed for this thread.
	DWORD previous_error_mode = 0;
	const auto error_mode_set = SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX,
	                                               &previous_error_mode) != FALSE;
	const df::scope_exit restore_error_mode([error_mode_set, previous_error_mode]
	{
		if (error_mode_set) SetThreadErrorMode(previous_error_mode, nullptr);
	});

	for (int i = 0; i < 26; ++i)
	{
		if (drives & 1 << i)
		{
			wchar_t szDrive[] = L"?:\\";
			szDrive[0] = static_cast<wchar_t>(L'A' + i);

			drive_t d;
			d.name = str::utf16_to_utf8(szDrive);

			switch (::GetDriveType(szDrive))
			{
			case DRIVE_REMOVABLE: d.type = drive_type::removable;
				break;
			case DRIVE_REMOTE: d.type = drive_type::remote;
				break;
			case DRIVE_CDROM: d.type = drive_type::cdrom;
				break;
			default: d.type = drive_type::fixed;
				break;
			}

			// Volume and size details are unavailable for an empty card reader or optical drive; the
			// drive is still reported so it can be browsed once media is inserted.
			wchar_t szFileSys[MAX_PATH];
			wchar_t szVolNameBuff[MAX_PATH];
			DWORD dwSerial = 0;
			DWORD dwMFL = 0;
			DWORD dwSysFlags = 0;

			if (GetVolumeInformation(szDrive, szVolNameBuff, MAX_PATH, &dwSerial, &dwMFL, &dwSysFlags, szFileSys,
			                         MAX_PATH))
			{
				if (!str::is_empty(szVolNameBuff))
				{
					d.vol_name = str::utf16_to_utf8(szVolNameBuff);
				}

				if (!str::is_empty(szFileSys))
				{
					d.file_system = str::utf16_to_utf8(szFileSys);
				}
			}

			ULARGE_INTEGER free_bytes_available_to_caller, total_number_of_bytes, total_number_of_free_bytes;

			if (GetDiskFreeSpaceEx(szDrive, &free_bytes_available_to_caller, &total_number_of_bytes,
			                       &total_number_of_free_bytes))
			{
				d.used = total_number_of_bytes.QuadPart - total_number_of_free_bytes.QuadPart;
				d.free = free_bytes_available_to_caller.QuadPart;
				d.capacity = total_number_of_bytes.QuadPart;
			}

			results.emplace_back(d);
		}
	}

	return results;
}


static constexpr LCID default_locale = LOCALE_USER_DEFAULT;
static wchar_t decimal_sep[8];
static wchar_t thousand_sep[8];
static NUMBERFMTW fmt;
std::atomic_bool number_format_invalid = true;
static std::mutex number_format_mutex;

void validate_number_format()
{
	if (number_format_invalid)
	{
		std::lock_guard lock(number_format_mutex);

		if (number_format_invalid)
		{
			// The ANSI variants return code-page bytes that the callers treat as UTF-8; separators
			// such as the French no-break space only survive the round trip through UTF-16.
			if (GetLocaleInfoW(default_locale, LOCALE_SDECIMAL, decimal_sep,
			                   std::size(decimal_sep)) == 0)
			{
				wcscpy_s(decimal_sep, L".");
			}

			if (GetLocaleInfoW(default_locale, LOCALE_STHOUSAND, thousand_sep,
			                   std::size(thousand_sep)) == 0)
			{
				wcscpy_s(thousand_sep, L",");
			}

			// LOCALE_RETURN_NUMBER writes a DWORD, and cchData counts wchar_t.
			GetLocaleInfoW(default_locale, LOCALE_RETURN_NUMBER | LOCALE_ILZERO,
			               std::bit_cast<LPWSTR>(&fmt.LeadingZero), sizeof(uint32_t) / sizeof(wchar_t));
			GetLocaleInfoW(default_locale, LOCALE_RETURN_NUMBER | LOCALE_INEGNUMBER,
			               std::bit_cast<LPWSTR>(&fmt.NegativeOrder), sizeof(uint32_t) / sizeof(wchar_t));

			fmt.NumDigits = 0;
			fmt.Grouping = 3;
			fmt.lpDecimalSep = decimal_sep;
			fmt.lpThousandSep = thousand_sep;

			number_format_invalid = false;
		}
	}
}

std::string platform::format_number(const std::string& num_text)
{
	validate_number_format();

	wchar_t result[64];
	const auto w = str::utf8_to_utf16(num_text);

	if (GetNumberFormatW(default_locale, 0, w.c_str(), &fmt, result, std::size(result)) == 0)
	{
		return num_text;
	}

	return str::utf16_to_utf8(result);
}

std::string platform::number_dec_sep()
{
	validate_number_format();
	return str::utf16_to_utf8(decimal_sep);
}

class file_impl final : public platform::file
{
	HANDLE _h = INVALID_HANDLE_VALUE;

public:
	file_impl(const HANDLE file) : _h(file)
	{
	}

	~file_impl() override
	{
		if (_h != INVALID_HANDLE_VALUE)
		{
			CloseHandle(_h);
		}
	}

	uint64_t size() const override
	{
		LARGE_INTEGER li;
		li.QuadPart = 0;
		if (!GetFileSizeEx(_h, &li))
		{
			return 0;
		}

		return static_cast<uint64_t>(li.QuadPart);
	}

	uint64_t read(uint8_t* buf, const uint64_t buf_size) const override
	{
		uint64_t total_read = 0;
		while (total_read < buf_size)
		{
			const auto chunk_size = static_cast<DWORD>(std::min<uint64_t>(buf_size - total_read, MAXDWORD));
			DWORD chunk_read = 0;

			// A short read is normal at end of file, but a failure is not - callers only see the byte
			// count, so the reason is logged here rather than being reported as a clean truncation.
			if (!ReadFile(_h, buf + total_read, chunk_size, &chunk_read, nullptr))
			{
				df::log(__FUNCTION__, platform::last_os_error());
				break;
			}

			total_read += chunk_read;
			if (chunk_read < chunk_size) break;
		}
		return total_read;
	}

	uint64_t write(const uint8_t* data, const uint64_t size) override
	{
		uint64_t total_written = 0;
		while (total_written < size)
		{
			const auto chunk_size = static_cast<DWORD>(std::min<uint64_t>(size - total_written, MAXDWORD));
			DWORD chunk_written = 0;

			if (!WriteFile(_h, data + total_written, chunk_size, &chunk_written, nullptr))
			{
				df::log(__FUNCTION__, platform::last_os_error());
				break;
			}

			total_written += chunk_written;
			if (chunk_written == 0) break;
		}
		return total_written;
	}

	uint64_t seek(const uint64_t pos, const whence w) const override
	{
		LARGE_INTEGER result = {0, 0};
		LARGE_INTEGER offset;
		offset.QuadPart = pos;

		auto method = FILE_BEGIN;

		switch (w)
		{
		case whence::begin:
			break;
		case whence::current:
			method = FILE_CURRENT;
			break;
		case whence::end:
			method = FILE_END;
			break;
		default: ;
		}

		if (!SetFilePointerEx(_h, offset, &result, method)) return -1;
		return result.QuadPart;
	}

	uint64_t pos() const override
	{
		LARGE_INTEGER result = {0, 0};
		constexpr LARGE_INTEGER offset = {0, 0};
		if (!SetFilePointerEx(_h, offset, &result, FILE_CURRENT)) return -1;
		return result.QuadPart;
	}

	bool trunc(const uint64_t pos) const override
	{
		LARGE_INTEGER liOff;
		liOff.QuadPart = pos;
		if (!SetFilePointerEx(_h, liOff, nullptr, FILE_BEGIN)) return false;
		return SetEndOfFile(_h) != 0;
	}

	df::date_t get_created() override
	{
		FILETIME ftm{};
		if (!GetFileTime(_h, &ftm, nullptr, nullptr)) return {};
		return df::date_t(ft_to_ts(ftm));
	}

	void set_created(const df::date_t date) override
	{
		const auto ftm = ts_to_ft(date._i);
		SetFileTime(_h, &ftm, nullptr, nullptr);
	}

	df::date_t get_modified() override
	{
		FILETIME ftm{};
		if (!GetFileTime(_h, nullptr, nullptr, &ftm)) return {};
		return df::date_t(ft_to_ts(ftm));
	}

	void set_modified(const df::date_t date) override
	{
		const auto ftm = ts_to_ft(date._i);
		SetFileTime(_h, nullptr, nullptr, &ftm);
	}

	df::file_path path() const override
	{
		if (_h != INVALID_HANDLE_VALUE)
		{
			std::vector<wchar_t> path(MAX_PATH);
			auto len = GetFinalPathNameByHandle(_h, path.data(), static_cast<DWORD>(path.size()), VOLUME_NAME_DOS);
			if (len >= path.size())
			{
				path.resize(static_cast<size_t>(len) + 1);
				len = GetFinalPathNameByHandle(_h, path.data(), static_cast<DWORD>(path.size()), VOLUME_NAME_DOS);
			}

			if (len > 0 && len < path.size())
			{
				return df::file_path(str::utf16_to_utf8(std::wstring_view(path.data(), len)));
			}
		}

		return {};
	}
};

platform::file_ptr platform::open_file(const df::file_path path, const file_open_mode mode)
{
	auto desired_access = GENERIC_READ;
	auto share_mode = FILE_SHARE_READ;
	auto creation_disposition = OPEN_EXISTING;
	auto flags_and_attributes = FILE_ATTRIBUTE_NORMAL;

	switch (mode)
	{
	case file_open_mode::read:
		break;
	case file_open_mode::write:
		desired_access = GENERIC_WRITE;
		share_mode = FILE_SHARE_WRITE;
		creation_disposition = OPEN_EXISTING;
		break;
	case file_open_mode::read_write:
		desired_access = GENERIC_READ | GENERIC_WRITE;
		share_mode = 0;
		creation_disposition = OPEN_EXISTING;
		flags_and_attributes = FILE_FLAG_RANDOM_ACCESS;
		break;

	case file_open_mode::create:
		desired_access = GENERIC_WRITE;
		share_mode = FILE_SHARE_WRITE;
		creation_disposition = CREATE_ALWAYS;
		break;
	case file_open_mode::sequential_scan:
		flags_and_attributes = FILE_FLAG_SEQUENTIAL_SCAN;
		break;
	default: ;
	}

	const auto path_w = to_file_system_path(path);
	auto* const file = CreateFile(path_w.c_str(), desired_access, share_mode, nullptr, creation_disposition,
	                              flags_and_attributes, nullptr);

	if (INVALID_HANDLE_VALUE == file)
	{
		return {};
	}

	return std::make_shared<file_impl>(file);
}

platform::file_ptr platform::make_file_from_handle(const HANDLE h)
{
	if (h == nullptr || h == INVALID_HANDLE_VALUE)
	{
		return {};
	}

	return std::make_shared<file_impl>(h);
}

class mapped_file_impl final : public platform::mapped_file
{
	static DWORD allocation_granularity()
	{
		static const DWORD value = []
		{
			SYSTEM_INFO si{};
			GetSystemInfo(&si);
			return si.dwAllocationGranularity;
		}();

		return value;
	}

	HANDLE _file = INVALID_HANDLE_VALUE;
	HANDLE _section = nullptr;
	const uint8_t* _view = nullptr; // granularity-aligned base of the current view
	size_t _view_size = 0;
	df::cspan _span; // the bytes the caller asked for, inside _view

	uint64_t _file_size = 0;

	void unmap_view()
	{
		if (_view)
		{
			UnmapViewOfFile(_view);
			_view = nullptr;
			_view_size = 0;
			_span = {};
		}
	}

public:
	mapped_file_impl(const HANDLE file, const HANDLE section, const uint64_t file_size) noexcept
		: _file(file), _section(section), _file_size(file_size)
	{
	}

	~mapped_file_impl() override
	{
		unmap_view();
		if (_section) CloseHandle(_section);
		if (_file != INVALID_HANDLE_VALUE) CloseHandle(_file);
	}

	uint64_t file_size() const override
	{
		return _file_size;
	}

	df::cspan data() const override
	{
		return _span;
	}

	df::cspan set_window(const uint64_t offset, const uint64_t len) override
	{
		unmap_view();

		if (offset >= _file_size) return {};

		const auto available = _file_size - offset;
		const auto want = std::min(len, available);
		if (want == 0) return {};

		const uint64_t granularity = allocation_granularity();

		// A view must start on an allocation-granularity boundary, so map from the boundary at or
		// below the requested offset and hand back a span that starts at the offset itself.
		const auto aligned = (offset / granularity) * granularity;
		const auto skew = offset - aligned;
		const auto map_len = skew + want;

		if (map_len > std::numeric_limits<size_t>::max()) return {};

		_view = static_cast<const uint8_t*>(MapViewOfFile(_section, FILE_MAP_READ,
		                                                  static_cast<DWORD>(aligned >> 32),
		                                                  static_cast<DWORD>(aligned & 0xFFFFFFFFull),
		                                                  static_cast<SIZE_T>(map_len)));

		if (!_view)
		{
			df::log(__FUNCTION__, platform::last_os_error());
			return {};
		}

		_view_size = static_cast<size_t>(map_len);
		_span = {_view + skew, static_cast<size_t>(want)};
		return _span;
	}

	void release_working_set() override
	{
		// VirtualUnlock on pages that were never locked removes them from the working set and
		// reports ERROR_NOT_LOCKED. That is the documented way to trim just this mapping rather
		// than the whole process, so the failure is expected and deliberately ignored.
		if (_view) VirtualUnlock(const_cast<uint8_t*>(_view), _view_size);
	}
};

platform::mapped_file_ptr platform::map_file(const df::file_path path, const map_mode mode)
{
	const auto path_w = to_file_system_path(path);
	auto* const file = CreateFileW(path_w.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
	                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

	if (file == INVALID_HANDLE_VALUE)
	{
		return {};
	}

	LARGE_INTEGER li{};

	if (!GetFileSizeEx(file, &li) || li.QuadPart <= 0)
	{
		// An empty file cannot be mapped at all, so this is a normal answer rather than a fault.
		CloseHandle(file);
		return {};
	}

	auto* const section = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);

	if (!section)
	{
		df::log(__FUNCTION__, platform::last_os_error());
		CloseHandle(file);
		return {};
	}

	auto result = std::make_shared<mapped_file_impl>(file, section, static_cast<uint64_t>(li.QuadPart));

	if (mode == map_mode::whole_file && result->set_window(0, static_cast<uint64_t>(li.QuadPart)).empty())
	{
		return {};
	}

	return result;
}

uint32_t platform::file_crc32(const df::file_path path)

{
	return file_crc32(path, {});
}

uint32_t platform::file_crc32(const df::file_path path, const df::cancel_token& token)
{
	bool success = false;
	uint32_t result = crypto::CRCINIT;
	df::perf_timer timer(df::index_perf.crc_us, &df::index_perf.crc_max_us);
	df::bump(df::index_perf.crc_computed);

	constexpr auto desired_access = GENERIC_READ;
	constexpr auto share_mode = FILE_SHARE_READ;
	constexpr auto creation_disposition = OPEN_EXISTING;
	constexpr auto flags_and_attributes = FILE_FLAG_SEQUENTIAL_SCAN;
	const auto path_w = to_file_system_path(path);
	auto* const hFile = CreateFile(path_w.c_str(), desired_access, share_mode, nullptr, creation_disposition,
	                               flags_and_attributes, nullptr);

	if (INVALID_HANDLE_VALUE != hFile)
	{
		LARGE_INTEGER li;
		li.QuadPart = 0;
		if (!GetFileSizeEx(hFile, &li))
		{
			CloseHandle(hFile);
			df::bump(df::index_perf.crc_failed);
			return 0;
		}

		const auto size = static_cast<uint64_t>(li.QuadPart);
		constexpr uint32_t max_chunk = df::two_fifty_six_k;
		const auto buffer = df::unique_alloc<uint8_t>(max_chunk);

		DWORD dwReadChunk = 0UL;
		uint64_t total_read = 0ULL;


		while (total_read < size && !token.is_cancelled())
		{
			const auto read_size = static_cast<DWORD>(std::min<uint64_t>(max_chunk, size - total_read));
			if (ReadFile(hFile, buffer.get(), read_size, &dwReadChunk, nullptr))
			{
				// A file truncated by another process reads zero bytes without failing; without this the
				// loop would never reach the size taken when the file was opened.
				if (dwReadChunk == 0) break;

				result = crypto::crc32c(result, buffer.get(), dwReadChunk);
				total_read += dwReadChunk;
			}
			else
			{
				dwReadChunk = 0;
				break;
			}
		}

		success = total_read == size;
		df::bump(df::index_perf.crc_bytes, total_read);
		CloseHandle(hFile);
	}

	if (!success) df::bump(df::index_perf.crc_failed);

	return success ? ~result : 0;
}

// GUID_DEVINTERFACE_DISK. Defined here because the SDK only declares it and which import library
// supplies the symbol varies between SDK versions.
static constexpr GUID guid_devinterface_disk = {
	0x53f56307, 0xb6bf, 0x11d0, {0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b}
};

static bool volume_device_number(const HANDLE device, STORAGE_DEVICE_NUMBER& result)
{
	DWORD returned = 0;
	return DeviceIoControl(device, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &result, sizeof(result), &returned,
	                       nullptr) != 0;
}

// Finds the device node of the disk a volume lives on, matched by storage device number because
// that is the only identifier both the volume and the disk interface report.
static bool find_disk_devinst(const STORAGE_DEVICE_NUMBER& volume_device, DEVINST& result)
{
	auto* const devices = SetupDiGetClassDevs(&guid_devinterface_disk, nullptr, nullptr,
	                                          DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

	if (devices == INVALID_HANDLE_VALUE)
	{
		df::log(__FUNCTION__, std::format("SetupDiGetClassDevs: {}", platform::last_os_error()));
		return false;
	}

	const df::scope_exit close_devices([devices] { SetupDiDestroyDeviceInfoList(devices); });

	SP_DEVICE_INTERFACE_DATA interface_data = {sizeof(interface_data)};

	for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devices, nullptr, &guid_devinterface_disk, i, &interface_data); ++i)
	{
		// Inline path storage keeps the detail buffer correctly aligned; a device path that does not
		// fit simply fails the call and is skipped.
		struct
		{
			SP_DEVICE_INTERFACE_DETAIL_DATA detail;
			wchar_t path[MAX_PATH];
		} storage = {};

		storage.detail.cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

		SP_DEVINFO_DATA info_data = {sizeof(info_data)};

		if (!SetupDiGetDeviceInterfaceDetail(devices, &interface_data, &storage.detail, sizeof(storage), nullptr,
		                                     &info_data))
		{
			continue;
		}

		auto* const disk = CreateFile(storage.detail.DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
		                              OPEN_EXISTING, 0, nullptr);

		if (disk == INVALID_HANDLE_VALUE)
		{
			continue;
		}

		STORAGE_DEVICE_NUMBER disk_device = {};
		const auto matched = volume_device_number(disk, disk_device) &&
			disk_device.DeviceType == volume_device.DeviceType &&
			disk_device.DeviceNumber == volume_device.DeviceNumber;

		CloseHandle(disk);

		if (matched)
		{
			result = info_data.DevInst;
			return true;
		}
	}

	return false;
}

// Removing a USB drive is a PnP operation on the device node the volume sits on. The storage
// eject IOCTL only ejects media from a drive that has removable media, so it fails for the USB
// disks and external drives this command exists to remove.
static bool request_device_eject(const wchar_t drive_letter)
{
	const std::wstring volume_path = std::wstring(L"\\\\.\\") + drive_letter + L':';

	// Query access only: the device cannot be removed while a handle asks for more than that.
	auto* const volume = CreateFile(volume_path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
	                                0, nullptr);

	if (volume == INVALID_HANDLE_VALUE)
	{
		df::log(__FUNCTION__, std::format("CreateFile: {}", platform::last_os_error()));
		return false;
	}

	STORAGE_DEVICE_NUMBER volume_device = {};
	const auto have_device = volume_device_number(volume, volume_device);
	const std::string device_number_error = have_device ? std::string{} : platform::last_os_error();
	CloseHandle(volume);

	if (!have_device)
	{
		df::log(__FUNCTION__, std::format("IOCTL_STORAGE_GET_DEVICE_NUMBER: {}", device_number_error));
		return false;
	}

	DEVINST disk_devinst = 0;

	if (!find_disk_devinst(volume_device, disk_devinst))
	{
		df::log(__FUNCTION__, std::format("no disk device for {}:", str::utf16_to_utf8({&drive_letter, 1})));
		return false;
	}

	// The disk node itself is not removable; the parent is the device that can be unplugged.
	DEVINST parent_devinst = 0;

	if (CM_Get_Parent(&parent_devinst, disk_devinst, 0) != CR_SUCCESS)
	{
		df::log(__FUNCTION__, "CM_Get_Parent failed");
		return false;
	}

	constexpr auto attempts = 3;

	for (auto attempt = 0; attempt < attempts; ++attempt)
	{
		auto veto_type = PNP_VetoTypeUnknown;
		wchar_t veto_name[MAX_PATH] = {};

		if (CM_Request_Device_Eject(parent_devinst, &veto_type, veto_name, MAX_PATH, 0) == CR_SUCCESS)
		{
			return true;
		}

		if (attempt + 1 == attempts)
		{
			df::log(__FUNCTION__, std::format("CM_Request_Device_Eject vetoed by {} ({})",
			                                  str::utf16_to_utf8(veto_name), static_cast<int>(veto_type)));
		}
		else
		{
			// A veto is usually a handle that is about to close, so give it a moment.
			Sleep(200 * (attempt + 1));
		}
	}

	return false;
}

// Ejects the volume at path. Runs on a worker: it locks, dismounts and waits on the device.
bool platform::eject(const df::folder_path path)
{
	// The device path is built from the drive letter and colon, so anything else is not a volume.
	const auto text = path.text();

	if (text.size() < 2 || !std::isalpha(static_cast<unsigned char>(text[0])) || text[1] != ':')
	{
		return false;
	}

	const auto drive_letter = static_cast<wchar_t>(text[0]);
	const std::wstring vol_w = std::wstring(L"\\\\.\\") + drive_letter + L':';

	constexpr auto share_mode = FILE_SHARE_READ | FILE_SHARE_WRITE;
	constexpr auto access_mode = GENERIC_READ | GENERIC_WRITE;

	auto* const device = CreateFile(vol_w.c_str(), access_mode, share_mode, nullptr, OPEN_EXISTING, 0, nullptr);

	if (device == INVALID_HANDLE_VALUE)
	{
		df::log(__FUNCTION__, std::format("CreateFile {}: {}", str::utf16_to_utf8(vol_w), last_os_error()));
		return false;
	}

	auto media_ejected = false;
	auto locked = false;

	{
		const df::scope_exit close_device([device] { CloseHandle(device); });

		DWORD returned = 0;
		constexpr auto lock_attempts = 5;

		// Locking is what makes the dismount safe: it fails while another handle is open instead of
		// tearing a volume away from whoever is writing to it. Retries cover a handle mid-close.
		for (auto attempt = 0; attempt < lock_attempts && !locked; ++attempt)
		{
			locked = DeviceIoControl(device, FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0, &returned, nullptr) != 0;

			if (!locked)
			{
				if (attempt + 1 == lock_attempts)
				{
					df::log(__FUNCTION__, std::format("FSCTL_LOCK_VOLUME: {}", last_os_error()));
				}
				else
				{
					Sleep(100 * (attempt + 1));
				}
			}
		}

		if (locked)
		{
			if (DeviceIoControl(device, FSCTL_DISMOUNT_VOLUME, nullptr, 0, nullptr, 0, &returned, nullptr))
			{
				PREVENT_MEDIA_REMOVAL allow_removal = {};
				allow_removal.PreventMediaRemoval = FALSE;

				// Drives without removable media reject this; that is not a failure to eject.
				if (!DeviceIoControl(device, IOCTL_STORAGE_MEDIA_REMOVAL, &allow_removal, sizeof(allow_removal),
				                     nullptr, 0, &returned, nullptr))
				{
					df::log(__FUNCTION__, std::format("IOCTL_STORAGE_MEDIA_REMOVAL: {}", last_os_error()));
				}

				media_ejected = DeviceIoControl(device, IOCTL_STORAGE_EJECT_MEDIA, nullptr, 0, nullptr, 0, &returned,
				                                nullptr) != 0;

				if (!media_ejected)
				{
					df::log(__FUNCTION__, std::format("IOCTL_STORAGE_EJECT_MEDIA: {}", last_os_error()));
				}
			}
			else
			{
				df::log(__FUNCTION__, std::format("FSCTL_DISMOUNT_VOLUME: {}", last_os_error()));
			}
		}
	}

	// Media eject covers optical drives and card readers. Everything else - including a volume that
	// could not be locked - is removed through its device node, which lets the system flush and
	// dismount in order and report what is holding the drive instead of forcing it away.
	return media_ejected || request_device_eject(drive_letter);
}

bool platform::is_server(const std::string_view path)
{
	// A drive root such as "C:\" is also a single path segment, but it is never a server name.
	// Treating one as a server sends drive enumeration failures down the NetShareEnum path.
	if (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':')
	{
		return false;
	}

	static std::regex e(R"(^[\\/]*([^\\\/]+)[\\\/]*$)");
	std::match_results<std::string_view::const_iterator> m;
	return std::regex_match(path.begin(), path.end(), m, e);
}

df::file_path platform::running_app_path()
{
	// The buffer is grown until the name fits; GetModuleFileName truncates without failing on
	// Windows Vista and later, so a full buffer is treated as truncation.
	std::wstring result(MAX_PATH, 0);

	for (;;)
	{
		const auto len = GetModuleFileName(get_resource_instance, result.data(), static_cast<DWORD>(result.size()));

		if (len == 0)
		{
			df::log(__FUNCTION__, last_os_error());
			return {};
		}

		if (len < result.size())
		{
			result.resize(len);
			return df::file_path(str::utf16_to_utf8(result));
		}

		if (result.size() >= 32768) return {};
		result.resize(result.size() * 2);
	}
}

static void add_library_paths(REFIID libraryId, df::unique_folders& results)
{
	ComPtr<IShellLibrary> plib;
	const auto hr = CoCreateInstance(CLSID_ShellLibrary, nullptr, CLSCTX_ALL, IID_PPV_ARGS(plib.GetAddressOf()));

	if (SUCCEEDED(hr))
	{
		if (plib && SUCCEEDED(plib->LoadLibraryFromKnownFolder(libraryId, STGM_READ)))
		{
			ComPtr<IShellItemArray> pFolders;

			if (SUCCEEDED(plib->GetFolders(LFF_FORCEFILESYSTEM, IID_PPV_ARGS(&pFolders))))
			{
				ComPtr<IEnumShellItems> spEnum;

				if (SUCCEEDED(pFolders->EnumItems(&spEnum)))
				{
					for (ComPtr<IShellItem> spPrinter;
					     spEnum->Next(1, &spPrinter, nullptr) == S_OK;
					     spPrinter.Reset())
					{
						wchar_t* spszName = nullptr;

						if (SUCCEEDED(spPrinter->GetDisplayName(SIGDN_FILESYSPATH, &spszName)))
						{
							results.emplace(df::folder_path(str::utf16_to_utf8(spszName)));
							CoTaskMemFree(spszName);
						}
					}
				}
			}
		}
	}
}

static df::folder_path path_from_csidl(const int csidl)
{
	wchar_t sz[MAX_PATH] = {0};
	SHGetFolderPath(app_wnd(), csidl, nullptr, SHGFP_TYPE_CURRENT, sz);
	return df::folder_path(str::utf16_to_utf8(sz));
}

static df::folder_path app_data()
{
	const auto folder = path_from_csidl(CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE).combine(s_app_name);
	platform::create_folder(folder);
	return folder;
}

static df::folder_path app_cache_data()
{
	// Existing users already have cache data at %LOCALAPPDATA%\Diffractor; this stays the
	// location for desktop builds and the fallback when the Store lookup below fails.
	const auto default_folder = path_from_csidl(CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE).combine(s_app_name);

#ifdef WINSTORE
	// Store packages are expected to keep their index in the package LocalCache folder.
	df::folder_path result;

	ComPtr<ABI::Windows::Storage::IApplicationDataStatics> app_data_statics;
	HRESULT hr = RoGetActivationFactory(
		Microsoft::WRL::Wrappers::HStringReference(RuntimeClass_Windows_Storage_ApplicationData).Get(),
		IID_PPV_ARGS(&app_data_statics));

	if (SUCCEEDED(hr))
	{
		ComPtr<ABI::Windows::Storage::IApplicationData> app_data;
		hr = app_data_statics->get_Current(&app_data);

		if (SUCCEEDED(hr))
		{
			ComPtr<ABI::Windows::Storage::IApplicationData2> app_data2;
			hr = app_data.As(&app_data2);

			if (SUCCEEDED(hr))
			{
				ComPtr<ABI::Windows::Storage::IStorageFolder> cache_folder;
				hr = app_data2->get_LocalCacheFolder(&cache_folder);

				if (SUCCEEDED(hr))
				{
					ComPtr<ABI::Windows::Storage::IStorageItem> storage_item;
					hr = cache_folder.As(&storage_item);

					if (SUCCEEDED(hr))
					{
						HSTRING path_hstring = nullptr;
						hr = storage_item->get_Path(&path_hstring);

						if (SUCCEEDED(hr) && path_hstring)
						{
							UINT32 path_length = 0;
							const wchar_t* path_buffer = WindowsGetStringRawBuffer(path_hstring, &path_length);
							if (path_buffer && path_length > 0)
							{
								result = df::folder_path(std::wstring_view(path_buffer, path_length));
							}
							WindowsDeleteString(path_hstring);
						}
					}
				}
			}
		}
	}

	if (result.is_empty())
	{
		df::log(__FUNCTION__, "LocalCacheFolder unavailable; using local app data");
	}
	else
	{
		return result;
	}
#endif

	platform::create_folder(default_folder);
	return default_folder;
}

static df::folder_path shell_known_folder(REFIID id)
{
	df::folder_path result;
	PWSTR path;

	if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &path)) && path)
	{
		result = df::folder_path(str::utf16_to_utf8(path));
		CoTaskMemFree(path);
	}

	return result;
}

static df::folder_path dropbox(const std::string_view sub_folder)
{
	const auto dropbox = path_from_csidl(CSIDL_PROFILE).combine("Dropbox");
	if (dropbox.exists()) return dropbox;

	const auto info_path = path_from_csidl(CSIDL_LOCAL_APPDATA).combine("Dropbox").combine_file("info.json");

	if (info_path.exists())
	{
		const auto info = df::util::json::json_from_file(info_path);

		const df::folder_path personal_path(
			df::util::json::safe_string(df::util::json::safe_object(info, "personal"), "path"));
		if (personal_path.exists())
		{
			const auto result = personal_path.combine(sub_folder);

			if (result.exists())
			{
				return result;
			}
		}

		const df::folder_path business_path(
			df::util::json::safe_string(df::util::json::safe_object(info, "business"), "path"));

		if (business_path.exists())
		{
			const auto result = business_path.combine(sub_folder);

			if (result.exists())
			{
				return result;
			}
		}
	}

	return dropbox;
}

static df::folder_path onedrive_root_folder()
{
	df::folder_path result;

	HKEY hKey;
	wchar_t path[MAX_PATH] = {0};
	DWORD dwLen = sizeof(path) - sizeof(wchar_t); // leave room for a terminator the value may omit
	DWORD dwType = 0;

	if (ERROR_SUCCESS == RegOpenKeyEx(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\OneDrive", 0,
	                                  KEY_QUERY_VALUE, &hKey))
	{
		if (ERROR_SUCCESS == RegQueryValueEx(hKey, L"UserFolder", nullptr, &dwType, std::bit_cast<LPBYTE>(&path[0]),
		                                     &dwLen)
			&& dwType == REG_SZ)
		{
			result = df::folder_path(str::utf16_to_utf8(path));
		}

		RegCloseKey(hKey);
	}

	// The key exists before the value does on a fresh install, so the profile fallback covers both.
	if (result.is_empty())
	{
		result = path_from_csidl(CSIDL_PROFILE).combine("OneDrive");
	}

	return result;
}

static df::folder_path onedrive(const std::string_view sub_folder, const std::string_view sub_folder2 = {})
{
	const auto root = onedrive_root_folder();

	if (root.exists())
	{
		auto result = root.combine(sub_folder);

		if (!sub_folder2.empty())
		{
			result = result.combine(sub_folder2);
		}

		if (result.exists())
		{
			return result;
		}
	}

	return {};
}

df::folder_path platform::known_path(const known_folder f)
{
	switch (f)
	{
	case known_folder::running_app_folder: return running_app_path().folder();
	case known_folder::test_files_folder: return running_app_path().folder().combine("test");
	case known_folder::app_data: return app_data();
	case known_folder::app_cache_data: return app_cache_data();
	case known_folder::downloads: return shell_known_folder(FOLDERID_Downloads);
	case known_folder::pictures: return path_from_csidl(CSIDL_MYPICTURES);
	case known_folder::video: return path_from_csidl(CSIDL_MYVIDEO);
	case known_folder::music: return path_from_csidl(CSIDL_MYMUSIC);
	case known_folder::documents: return path_from_csidl(CSIDL_MYDOCUMENTS);
	case known_folder::desktop: return path_from_csidl(CSIDL_DESKTOP);
	case known_folder::dropbox_photos: return dropbox("photos");
	case known_folder::onedrive_pictures: return onedrive("pictures");
	case known_folder::onedrive_video: return onedrive("video");
	case known_folder::onedrive_music: return onedrive("music");
	case known_folder::onedrive_camera_roll: return onedrive("pictures", "Camera Roll");
	default: ;
	}

	return {};
}

df::unique_folders platform::known_folders()
{
	df::unique_folders result;

	result.emplace(known_path(known_folder::pictures));
	result.emplace(path_from_csidl(CSIDL_COMMON_PICTURES));
	add_library_paths(FOLDERID_PicturesLibrary, result);
	result.emplace(known_path(known_folder::video));

	result.emplace(path_from_csidl(CSIDL_COMMON_VIDEO));
	add_library_paths(FOLDERID_VideosLibrary, result);

	result.emplace(known_path(known_folder::music));
	result.emplace(path_from_csidl(CSIDL_COMMON_MUSIC));
	add_library_paths(FOLDERID_MusicLibrary, result);

	result.emplace(known_path(known_folder::documents));
	result.emplace(path_from_csidl(CSIDL_COMMON_DOCUMENTS));
	add_library_paths(FOLDERID_DocumentsLibrary, result);

	result.emplace(known_path(known_folder::desktop));


	const auto folder = known_path(known_folder::downloads);
	if (!folder.is_empty()) result.emplace(folder);

	if (known_path(known_folder::dropbox_photos).exists()) result.emplace(known_path(known_folder::dropbox_photos));
	if (known_path(known_folder::onedrive_pictures).exists())
		result.emplace(
			known_path(known_folder::onedrive_pictures));
	if (known_path(known_folder::onedrive_video).exists()) result.emplace(known_path(known_folder::onedrive_video));
	if (known_path(known_folder::onedrive_music).exists()) result.emplace(known_path(known_folder::onedrive_music));

	const auto drives = scan_drives();

	for (const auto& d : drives)
	{
		result.emplace(df::folder_path(d.name));
	}

	return result;
}

local_folders_result platform::local_folders()
{
	local_folders_result result;

	result.pictures = known_path(known_folder::pictures);

	result.video = known_path(known_folder::video);

	result.music = known_path(known_folder::music);

	result.desktop = known_path(known_folder::desktop);

	const auto downloads = known_path(known_folder::downloads);

	if (!downloads.is_empty())
	{
		result.downloads = downloads;
	}

	const auto dropbox = known_path(known_folder::dropbox_photos);

	if (dropbox.exists())
	{
		result.dropbox_photos = dropbox;
	}

	const auto onedrive_pictures = known_path(known_folder::onedrive_pictures);

	if (onedrive_pictures.exists())
	{
		result.onedrive_pictures = onedrive_pictures;
	}

	const auto onedrive_video = known_path(known_folder::onedrive_video);

	if (onedrive_video.exists())
	{
		result.onedrive_video = onedrive_video;
	}

	const auto onedrive_music = known_path(known_folder::onedrive_music);

	if (onedrive_music.exists())
	{
		result.onedrive_music = onedrive_music;
	}

	return result;
}

std::string platform::user_language()
{
	wchar_t language[16]{};
	wchar_t country[16]{};

	// Both calls return 0 on failure, otherwise the character count including the terminating null.
	const auto language_len = GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SISO639LANGNAME, language,
	                                        std::size(language));

	if (language_len < 2) return {};

	std::wstring result(language, static_cast<size_t>(language_len) - 1);

	const auto country_len = GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SISO3166CTRYNAME, country,
	                                       std::size(country));

	if (country_len >= 2)
	{
		result += L'_';
		result.append(country, static_cast<size_t>(country_len) - 1);
	}

	return str::utf16_to_utf8(result);
}

platform::mapi_send_result platform::classify_mapi_send_result(const uint32_t result_code)
{
	if (result_code == SUCCESS_SUCCESS) return mapi_send_result::sent;
	if (result_code == MAPI_USER_ABORT) return mapi_send_result::canceled;
	return mapi_send_result::failed;
}

platform::mapi_send_result platform::mapi_send(const std::string_view to, const std::string_view subject,
                                               const std::string_view text, const attachments_t& attachments)
{
	df::assert_true(ui::is_ui_thread());

	auto* hwndParent = app_wnd();

	// some extra precautions are required to use MAPISendMail as it
	// tends to enable the parent window in between dialogs (after
	// the login dialog, but before the send note dialog).
	SetCapture(hwndParent);
	SetFocus(nullptr);

	HINSTANCE handle = LoadLibraryExA("MAPI32.DLL", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	auto result = mapi_send_result::failed;

	if (handle)
	{
		auto send_mail = std::bit_cast<LPMAPISENDMAIL>(GetProcAddress(handle, "MAPISendMail"));
		auto send_mail_w = std::bit_cast<LPMAPISENDMAILW>(GetProcAddress(handle, "MAPISendMailW"));

		if (send_mail_w)
		{
			auto to_w = str::utf8_to_utf16(to);
			auto subject_w = str::utf8_to_utf16(subject);
			auto text_w = str::utf8_to_utf16(text);

			MapiRecipDescW recipients = {};
			recipients.ulRecipClass = MAPI_TO;
			recipients.lpszAddress = const_cast<LPWSTR>(to_w.c_str());
			recipients.lpszName = recipients.lpszAddress;

			MapiMessageW message_w = {};

			std::vector<MapiFileDescW> files;
			std::vector<std::pair<std::wstring, std::wstring>> attachments_w;

			for (const auto& a : attachments)
			{
				attachments_w.emplace_back(str::utf8_to_utf16(a.first), str::utf8_to_utf16(a.second.str()));
			}

			for (const auto& a : attachments_w)
			{
				MapiFileDescW fd = {};

				fd.lpszPathName = const_cast<wchar_t*>(a.second.c_str());
				fd.lpszFileName = const_cast<wchar_t*>(a.first.c_str());
				fd.nPosition = -1;

				files.emplace_back(fd);
			}

			if (!files.empty())
			{
				message_w.nFileCount = static_cast<uint32_t>(files.size());
				message_w.lpFiles = files.data();
			}

			if (!to_w.empty())
			{
				message_w.lpRecips = &recipients;
				message_w.nRecipCount = 1;
			}

			message_w.lpszSubject = const_cast<LPWSTR>(subject_w.c_str());
			message_w.lpszNoteText = const_cast<LPWSTR>(text_w.c_str());

			const auto result_code = send_mail_w(0, std::bit_cast<ULONG_PTR>(hwndParent), &message_w,
			                                     MAPI_LOGON_UI | MAPI_DIALOG, 0);
			result = classify_mapi_send_result(result_code);
		}
		else if (send_mail)
		{
			auto to_s = std::string(to);
			auto subject_s = std::string(subject);
			auto text_s = std::string(text);

			MapiRecipDesc recipients = {};
			recipients.ulRecipClass = MAPI_TO;
			recipients.lpszAddress = const_cast<LPSTR>(std::bit_cast<const char*>(to_s.c_str()));
			recipients.lpszName = recipients.lpszAddress;

			MapiMessage message_a = {};

			std::vector<MapiFileDesc> files;
			std::vector<std::pair<std::string, std::string>> attachments_a;

			for (const auto& a : attachments)
			{
				attachments_a.emplace_back(a.first, a.second.str());
			}

			for (const auto& a : attachments_a)
			{
				MapiFileDesc fd = {};

				fd.nPosition = 0xFFFFFFFF;
				fd.lpszPathName = const_cast<char*>(std::bit_cast<const char*>(a.second.c_str()));
				fd.lpszFileName = const_cast<char*>(std::bit_cast<const char*>(a.first.c_str()));

				files.emplace_back(fd);
			}

			if (!files.empty())
			{
				message_a.nFileCount = static_cast<uint32_t>(files.size());
				message_a.lpFiles = files.data();
			}

			if (!to.empty())
			{
				message_a.lpRecips = &recipients;
				message_a.nRecipCount = 1;
			}

			message_a.lpszSubject = const_cast<LPSTR>(std::bit_cast<const char*>(subject_s.c_str()));
			message_a.lpszNoteText = const_cast<LPSTR>(std::bit_cast<const char*>(text_s.c_str()));

			const auto result_code = send_mail(0, std::bit_cast<ULONG_PTR>(hwndParent), &message_a,
			                                   MAPI_LOGON_UI | MAPI_DIALOG, 0);
			result = classify_mapi_send_result(result_code);
		}

		FreeLibrary(handle);
	}

	// after returning from the MAPISendMail call, the window must
	// have its logical enabled state restored and focus returned to the frame.
	ReleaseCapture();

	sync_app_window_enabled();
	SetActiveWindow(nullptr);
	SetActiveWindow(hwndParent);
	SetFocus(hwndParent);

	return result;
}

uint32_t platform::tick_count()
{
	return GetTickCount();
}

uint32_t platform::caret_blink_time()
{
	const auto milliseconds = GetCaretBlinkTime();
	return milliseconds == INFINITE ? 0 : milliseconds;
}

uint32_t platform::current_thread_id()
{
	return GetCurrentThreadId();
}

static HANDLE g_startup_scope = nullptr;

bool platform::claim_startup_scope()
{
	if (g_startup_scope) return true;

	// Names the window between this process starting and its first idle frame, per user session. Only
	// one process holds it at a time, so overlapping launches can tell themselves apart from a repeat
	// of a launch that failed. A crash releases it with the process, which is the case that matters.
	g_startup_scope = ::CreateMutexW(nullptr, TRUE, L"Local\\DiffractorStartupScope");

	if (!g_startup_scope) return false;

	if (::GetLastError() == ERROR_ALREADY_EXISTS)
	{
		::CloseHandle(g_startup_scope);
		g_startup_scope = nullptr;
		return false;
	}

	return true;
}

void platform::release_startup_scope()
{
	if (g_startup_scope)
	{
		::ReleaseMutex(g_startup_scope);
		::CloseHandle(g_startup_scope);
		g_startup_scope = nullptr;
	}
}

platform::thread_init::thread_init()
{
	// https://support.microsoft.com/en-us/help/287087/info-calling-shell-functions-and-interfaces-from-a-multithreaded-apart
	_hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
}

platform::thread_init::~thread_init()
{
	if (SUCCEEDED(_hr)) CoUninitialize();
}


platform::media_thread_priority::media_thread_priority()
{
	// Join the MMCSS "Pro Audio" class so the OS scheduler keeps this audio thread
	// running promptly even while the video-decode thread is busy. Failure is
	// non-fatal (the thread simply runs at normal priority).
	DWORD task_index = 0;
	_task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_index);
}

platform::media_thread_priority::~media_thread_priority()
{
	if (_task)
	{
		AvRevertMmThreadCharacteristics(_task);
		_task = nullptr;
	}
}


FILETIME ts_to_ft(const uint64_t ts)
{
	FILETIME ft;
	ft.dwHighDateTime = static_cast<uint32_t>(ts >> 32);
	ft.dwLowDateTime = static_cast<uint32_t>(ts & 0xffffffffull);
	return ft;
}

uint64_t platform::utc_to_local(const uint64_t ts)
{
	const auto ft = ts_to_ft(ts);
	FILETIME result = {};
	// The conversion fails for timestamps outside the representable range. Returning `result`
	// regardless would hand back an uninitialised stack value as a date.
	if (!FileTimeToLocalFileTime(&ft, &result)) return ts;
	return ft_to_ts(result);
}

uint64_t platform::local_to_utc(const uint64_t ts)
{
	const auto ft = ts_to_ft(ts);
	FILETIME result = {};
	if (!LocalFileTimeToFileTime(&ft, &result)) return ts;
	return ft_to_ts(result);
}

df::date_t platform::dos_date_to_ts(const uint16_t dos_date, const uint16_t dos_time)
{
	FILETIME ft_local = {};
	FILETIME ft_utc = {};
	// A zip/archive entry can carry an out-of-range DOS date; treat that as "no date" rather than
	// reading whatever happened to be on the stack.
	if (!DosDateTimeToFileTime(dos_date, dos_time, &ft_local)) return {};
	if (!LocalFileTimeToFileTime(&ft_local, &ft_utc)) return {};
	return df::date_t(ft_to_ts(ft_utc));
}

std::string platform::format_date_time(const df::date_t d)
{
	constexpr LCID locale = LOCALE_USER_DEFAULT;
	constexpr int size = 128;
	wchar_t sz_date[size] = {0};
	wchar_t sz_time[size] = {0};
	SYSTEMTIME st;
	constexpr DWORD flags = DATE_SHORTDATE;
	const auto ft = ts_to_ft(d._i);

	if (d.is_valid())
	{
		if (FileTimeToSystemTime(&ft, &st))
		{
			GetDateFormatW(locale, flags, &st, nullptr, sz_date, size);
			GetTimeFormatW(locale, 0, &st, nullptr, sz_time, size);

			return std::format("{} {}", str::utf16_to_utf8(sz_date), str::utf16_to_utf8(sz_time));
		}
	}

	return {};
}

std::string platform::format_date(const df::date_t d)
{
	constexpr LCID locale = LOCALE_USER_DEFAULT;
	constexpr int size = 128;
	wchar_t sz[size] = {0};
	SYSTEMTIME st;
	constexpr DWORD flags = DATE_SHORTDATE;
	const auto ft = ts_to_ft(d._i);

	if (d.is_valid() &&
		FileTimeToSystemTime(&ft, &st) &&
		GetDateFormatW(locale, flags, &st, nullptr, sz, size))
	{
		return str::utf16_to_utf8(sz);
	}

	return {};
}

std::string platform::format_time(const df::date_t d)
{
	constexpr LCID locale = LOCALE_USER_DEFAULT;
	constexpr int size = 128;
	wchar_t sz[size] = {0};
	SYSTEMTIME st;
	const auto ft = ts_to_ft(d._i);

	if (FileTimeToSystemTime(&ft, &st) &&
		GetTimeFormatW(locale, 0, &st, nullptr, sz, size))
	{
		return str::utf16_to_utf8(sz);
	}

	return {};
}

df::date_t platform::now()
{
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	return df::date_t(ft_to_ts(ft));
}

df::day_t platform::to_date(const uint64_t ts)
{
	// perf https://stackoverflow.com/questions/15957805/extract-year-month-day-etc-from-stdchronotime-point-in-c
	const auto ft = ts_to_ft(ts);
	SYSTEMTIME st = {};
	FileTimeToSystemTime(&ft, &st);
	return {st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond};
}

uint64_t platform::from_date(const df::day_t& day)
{
	SYSTEMTIME st = {0};

	st.wYear = day.year;
	st.wMonth = day.month;
	st.wDay = day.day;
	st.wHour = day.hour;
	st.wMinute = day.minute;
	st.wSecond = day.second;

	if (st.wMonth == 0) st.wMonth = 1;
	if (st.wDay == 0) st.wDay = 1;

	FILETIME ft;
	return SystemTimeToFileTime(&st, &ft) != 0 ? ft_to_ts(ft) : 0;
}

double df::now()
{
	// Reciprocal rather than a divide: this is read several times a frame by animation, A/V sync and
	// the zoom timer, and the rounding sits far below the counter's own resolution.
	static const double seconds_per_tick = []
	{
		LARGE_INTEGER tps = {0};
		QueryPerformanceFrequency(&tps);
		return tps.QuadPart == 0 ? 0.0 : 1.0 / static_cast<double>(tps.QuadPart);
	}();

	LARGE_INTEGER pc = {0};
	QueryPerformanceCounter(&pc);
	return static_cast<double>(pc.QuadPart) * seconds_per_tick;
}

int64_t df::now_ms()
{
	static const int64_t ticks_per_second = []
	{
		LARGE_INTEGER tps = {0};
		QueryPerformanceFrequency(&tps);
		return tps.QuadPart == 0 ? 1 : tps.QuadPart;
	}();

	LARGE_INTEGER pc = {0};
	QueryPerformanceCounter(&pc);
	return pc.QuadPart * 1000 / ticks_per_second;
}

int64_t df::now_us()
{
	// Called per measured operation, so the frequency (fixed for the life of the process) is
	// cached and the counter is scaled before dividing to keep the multiply inside 64 bits.
	static const int64_t ticks_per_second = []
	{
		LARGE_INTEGER tps = {0};
		QueryPerformanceFrequency(&tps);
		return tps.QuadPart == 0 ? 1 : tps.QuadPart;
	}();

	LARGE_INTEGER pc = {0};
	QueryPerformanceCounter(&pc);
	return (pc.QuadPart / ticks_per_second) * 1'000'000 + (pc.QuadPart % ticks_per_second) * 1'000'000 /
		ticks_per_second;
}

bool platform::created_date(const df::file_path path, const df::date_t dt)
{
	const auto w = to_file_system_path(path);

	BOOL result = FALSE;
	const HANDLE h = CreateFile(w.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
	                            FILE_ATTRIBUTE_NORMAL,
	                            nullptr);

	if (h != INVALID_HANDLE_VALUE)
	{
		const auto ft = ts_to_ft(dt._i);
		result = SetFileTime(h, &ft, nullptr, nullptr);
		CloseHandle(h);
	}

	return result != FALSE;
}


_Acquires_exclusive_lock_(this)

void platform::mutex::ex_lock() const
{
	AcquireSRWLockExclusive(std::bit_cast<PSRWLOCK>(&_cs));
}

_Releases_exclusive_lock_(this)

void platform::mutex::ex_unlock() const
{
	ReleaseSRWLockExclusive(std::bit_cast<PSRWLOCK>(&_cs));
}

_Acquires_shared_lock_(this)

void platform::mutex::sh_lock() const
{
	AcquireSRWLockShared(std::bit_cast<PSRWLOCK>(&_cs));
}

_Releases_shared_lock_(this)

void platform::mutex::sh_unlock() const
{
	ReleaseSRWLockShared(std::bit_cast<PSRWLOCK>(&_cs));
}


platform::thread_event::thread_event(const bool manual_reset, const bool initial_state)
{
	create(manual_reset, initial_state);
}

void platform::thread_event::create(const bool manual_reset, const bool initial_state)
{
	DWORD flags = 0;

	if (manual_reset)
		flags |= CREATE_EVENT_MANUAL_RESET;

	if (initial_state)
		flags |= CREATE_EVENT_INITIAL_SET;

	auto* const h = ::CreateEventEx(nullptr, nullptr, flags, EVENT_ALL_ACCESS);

	if (h == nullptr)
	{
		throw app_exception(last_os_error());
	}

	if (_h) CloseHandle(_h);
	_h = h;
}

platform::thread_event::~thread_event()
{
	if (_h)
	{
		CloseHandle(_h);
		_h = nullptr;
	}
}

//

void platform::thread_event::reset() const noexcept
{
	if (_h) ResetEvent(_h);
}

void platform::thread_event::set() const noexcept
{
	if (_h) SetEvent(_h);
}


bool platform::is_valid_file_name(const std::string_view name)
{
	static constexpr std::string_view invalid_chars = "\\/:*?\"<>|"; // Common invalid characters
	static constexpr std::string_view reserved_names[] = {
		"CON", "PRN", "AUX", "NUL",
		"COM1", "COM2", "COM3", "COM4",
		"COM5", "COM6", "COM7", "COM8", "COM9",
		"LPT1", "LPT2", "LPT3", "LPT4", "LPT5",
		"LPT6", "LPT7", "LPT8", "LPT9"
	};

	if (name.empty())
	{
		return false;
	}

	// Windows silently drops a trailing dot or space, so the file would not carry the name shown.
	if (name.back() == '.' || name.back() == ' ')
	{
		return false;
	}

	auto i = name.begin();
	while (i < name.end())
	{
		const auto c = str::pop_utf8_char(i, name.end());

		if (c < 128 && (c < 32 || invalid_chars.find(static_cast<char>(c)) != std::string_view::npos))
		{
			return false;
		}
	}

	// A device name stays reserved when it carries an extension: CON.txt is still the console.
	const auto stem = name.substr(0, name.find('.'));

	for (const auto& reserved : reserved_names)
	{
		if (str::icmp(reserved, stem) == 0)
		{
			return false;
		}
	}

	return true;
}
