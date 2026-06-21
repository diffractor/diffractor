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
#include <Shellapi.h>
#include <mapi.h>
#include <WinInet.h>
#include <ShlObj.h>


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

static constexpr CLSID CLSID_ImageThumbnailProvider = {
	0xC7657C4A, 0x9F68, 0x40fa, {0xA4, 0xDF, 0x96, 0xBC, 0x08, 0xEB, 0x35, 0x51}
};

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
				if (!value.empty())
				{
					value += L", ";
				}
				value += strings[index];
				CoTaskMemFree(strings[index]);
			}
			CoTaskMemFree(strings);
		}
	}
	else if (VT_LPWSTR == propVariant.vt)
	{
		value = propVariant.pwszVal;
	}
	else if (VT_UI4 == propVariant.vt)
	{
		//ps.store(t, static_cast<int>(propVariant.ulVal));
		df::assert_true(false);
	}

	return str::cache(str::utf16_to_utf8(value));
}

// Maximum thumbnail size
static constexpr int max_thumbnail_bytes = 0x1000000;

platform::get_cached_file_properties_response platform::get_cached_file_properties(
	const df::file_path path, prop::item_metadata& properties_out, ui::const_image_ptr& thumbnail_out)
{
	auto result = get_cached_file_properties_response::fail;

	/*ComPtr<IShellItem2> psi2;
	ComPtr<IThumbnailProvider> pThumbnailProvider;

	HRESULT hr = SHCreateItemFromParsingName(path.to_file_system_path().c_str(), nullptr, IID_PPV_ARGS(&psi2));

	if (SUCCEEDED(hr))
	{
		hr = psi2->GetPropertyStore(flags, riid, ppv);
	}

	hr = psi2->BindToHandler(nullptr, BHID_ThumbnailHandler, IID_PPV_ARGS(&pThumbProvider));

	if (SUCCEEDED(hr))
	{
	}*/

	/*render::surface result;
	ComPtr<IThumbnailProvider> pThumbnailProvider;
	ComPtr<IInitializeWithFile> pInitFile;
	auto hr = CoCreateInstance(CLSID_ImageThumbnailProvider, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pThumbnailProvider));

	if (SUCCEEDED(hr))
	{
		hr = pThumbnailProvider->QueryInterface(IID_PPV_ARGS(&pInitFile));

		if (SUCCEEDED(hr))
		{
			hr = pInitFile->Initialize(path.to_file_system_path().c_str(), STGM_READ);

			if (SUCCEEDED(hr))
			{
				WTS_ALPHATYPE at = WTSAT_UNKNOWN;
				HBITMAP hbm = nullptr;

				hr = pThumbnailProvider->GetThumbnail(256, &hbm, &at);

				if (SUCCEEDED(hr))
				{
					BITMAP bm;
					GetObject(hbm, sizeof(BITMAP), &bm);

					result.alloc(bm.bmWidth, bm.bmHeight, at == WTSAT_ARGB);

					::DeleteObject(hbm);
				}
			}
		}
	}*/

	ComPtr<IShellItem2> item;
	HRESULT hr = SHCreateItemFromParsingName(to_file_system_path(path).c_str(), nullptr /*bindContext*/,
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
							/*else if (PKEY_Audio_Format == propKey) {
								const std::wstring value = PropertyToString(propVar);
								if (!value.empty()) {
									const std::wstring version = ShellMetadata::GetAudioSubType(value);
									if (!version.empty()) {
										tags.emplace(Handler::Tags::value_type(Handler::Tag::Version, version));
									}
								}
							}*/
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
							else if (PKEY_ThumbnailStream == propKey)
							{
								if (VT_STREAM == propVar.vt)
								{
									IStream* stream = propVar.pStream;
									if (nullptr != stream)
									{
										STATSTG stats = {};
										if (SUCCEEDED(stream->Stat(&stats, STATFLAG_NONAME)))
										{
											if (stats.cbSize.QuadPart <= max_thumbnail_bytes)
											{
												ULONG bytesRead = 0;
												df::blob blob;
												blob.resize(stats.cbSize.QuadPart);

												if (SUCCEEDED(stream->Read(blob.data(), static_cast<ULONG>(stats.
													cbSize.QuadPart), &bytesRead)))
												{
													thumbnail_out = load_image_file(blob);
												}
											}
										}
									}
								}
							}
							else if (PKEY_Thumbnail == propKey)
							{
								if (VT_STREAM == propVar.vt)
								{
									IStream* stream = propVar.pStream;
									if (nullptr != stream)
									{
										STATSTG stats = {};
										if (SUCCEEDED(stream->Stat(&stats, STATFLAG_NONAME)))
										{
											if (stats.cbSize.QuadPart <= max_thumbnail_bytes)
											{
												ULONG bytesRead = 0;
												df::blob blob;
												blob.resize(stats.cbSize.QuadPart);

												if (SUCCEEDED(stream->Read(blob.data(), static_cast<ULONG>(stats.
													cbSize.QuadPart), &bytesRead)))
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
		}
	}

	return result;
}

HRESULT
QueryInterfacePropVariant(
	REFPROPVARIANT pv,
	REFIID riid,
	__out void** ppv)
{
	*ppv = nullptr;

	HRESULT hr = E_NOINTERFACE;
	if (VT_UNKNOWN == pv.vt)
	{
		hr = pv.punkVal->QueryInterface(riid, ppv);
	}
	else if (VT_STREAM == pv.vt)
	{
		hr = pv.pStream->QueryInterface(riid, ppv);
	}
	return hr;
}


STDAPI IPropertyStore_GetUnknown(__in IPropertyStore* pps, __in REFPROPERTYKEY key, __in REFIID riid,
                                 __deref_out void** ppv)
{
	*ppv = nullptr;

	PROPVARIANT propvar;
	HRESULT hr = pps->GetValue(key, &propvar);
	if (SUCCEEDED(hr))
	{
		hr = QueryInterfacePropVariant(propvar, riid, ppv);
		PropVariantClear(&propvar);
	}
	return hr;
}

MIDL_INTERFACE("4fe8a664-d045-46d8-a725-f0842f6a95ca")
	IThumbnailStreamProvider : IUnknown
{
	STDMETHOD(GetThumbnailStream)(_Outptr_result_maybenull_ IStream* * ppThumbnailStream) = 0;
};

// String to identify that the IStream bind request is for the item's thumbnail.
#define STR_BIND_THUMBNAIL_STREAM L"BindToThumbnailStream"

//THUMBNAILIDInternal
// WTS_THUMBNAILID passed in to client is opaque.  Actual content is THUMBNAILIDInternal
struct THUMBNAILIDInternal
{
	ULONGLONG ullCrc64Key;

	// This used to be ullLastModified, but that's included in the ullCrc64Key now.
	// (We already shipped IThumbnailCache with a 16 byte WTS_THUMBNAILID, and cannot change it now)
	ULONGLONG ullReserved;
};

platform::get_cached_file_properties_response platform::get_shell_thumbnail(
	const df::file_path path, ui::const_image_ptr& thumbnail_out)
{
	constexpr auto result = get_cached_file_properties_response::fail;
	auto cxy_requested_thumb_size = 96;
	// std::max(setting.thumbnail_max_dimension.cx, setting.thumbnail_max_dimension.cy);

	ComPtr<IShellItemImageFactory> spsiif;

	const auto path_w = to_file_system_path(path);

	ComPtr<IShellItem2> item;
	HRESULT hr = SHCreateItemFromParsingName(path_w.c_str(), nullptr /*bindContext*/, IID_PPV_ARGS(&item));

	//if (SUCCEEDED(hr))
	//{
	//	const GETPROPERTYSTOREFLAGS flags = GPS_DEFAULT;
	//	ComPtr<IPropertyStore> propStore;
	//	hr = item->GetPropertyStore(flags, IID_PPV_ARGS(&propStore));

	//	if (SUCCEEDED(hr))
	//	{
	//		PROPVARIANT pvThumbnailCacheId;
	//		hr = item->GetProperty(PKEY_ThumbnailCacheId, &pvThumbnailCacheId);
	//		if (SUCCEEDED(hr))
	//		{

	//			ComPtr<IThumbnailCache> spThumbCache;
	//			hr = spThumbCache.CoCreateInstance(CLSID_LocalThumbnailCache, nullptr, CLSCTX_INPROC);

	//			if (SUCCEEDED(hr))
	//			{
	//				ULONGLONG ullThumbcacheId = 0;
	//				ComPtr<ISharedBitmap> spBitmap;
	//				WTS_CACHEFLAGS OutFlags = WTS_DEFAULT;
	//				PropVariantToUInt64(pvThumbnailCacheId, &ullThumbcacheId);

	//				WTS_THUMBNAILID wtsId;
	//				THUMBNAILIDInternal* pThumbnailIDInternal = std::bit_cast<THUMBNAILIDInternal*>(&wtsId);
	//				pThumbnailIDInternal->ullCrc64Key = ullThumbcacheId;
	//				pThumbnailIDInternal->ullReserved = 0ull;
	//				
	//				hr = spThumbCache->GetThumbnailByID(wtsId, cxy_requested_thumb_size, &spBitmap, &OutFlags);

	//				if (SUCCEEDED(hr))
	//				{
	//					HBITMAP hBitmap = nullptr;
	//					WTS_ALPHATYPE alpha_type = WTSAT_UNKNOWN;

	//					if (SUCCEEDED(spBitmap->GetFormat(&alpha_type)) &&
	//						SUCCEEDED(spBitmap->GetSharedBitmap(&hBitmap)))
	//					{
	//						df::blob data;
	//						if (save_hbitmap_to_memory(hBitmap, data, alpha_type == WTSAT_ARGB))
	//						{
	//							thumbnail_out = render::image(data);
	//							result = get_cached_file_properties_response::ok;
	//						}
	//					}
	//				}
	//				else if (hr == WTS_E_EXTRACTIONPENDING || hr == E_PENDING)
	//				{
	//					result = get_cached_file_properties_response::pending;
	//				}
	//			}
	//			
	//			PropVariantClear(&pvThumbnailCacheId);
	//		}
	//		
	//		/*ComPtr<IPropertyStore> spps;
	//		hr = item->GetPropertyStoreForKeys(&PKEY_ThumbnailCacheId, 1, GPS_DEFAULT, IID_PPV_ARGS(&spps));
	//		if (SUCCEEDED(hr))
	//		{
	//			PROPVARIANT pvThumbnailCacheId;
	//			hr = spps->GetProperty(PKEY_ThumbnailCacheId, &pvThumbnailCacheId);
	//			if (SUCCEEDED(hr))
	//			{
	//				PropVariantClear(&pvThumbnailCacheId);
	//			}
	//		}*/
	//		
	//		/*DWORD cItems;
	//		hr = propStore->GetCount(&cItems);
	//		if (SUCCEEDED(hr))
	//		{
	//			for (int i = 0; i < cItems; i++)
	//			{
	//				PROPERTYKEY propKey = {};
	//				hr = propStore->GetAt(i, &propKey);

	//				if (SUCCEEDED(hr))
	//				{
	//					PROPVARIANT propVar;
	//					if (SUCCEEDED(propStore->GetValue(propKey, &propVar)))
	//					{
	//						PropVariantClear(&propVar);
	//					}
	//				}
	//			}
	//		}*/
	//	}
	//}

	//if (SUCCEEDED(hr))
	//{
	//	ComPtr<IThumbnailCache> spThumbCache;
	//	hr = spThumbCache.CoCreateInstance(CLSID_LocalThumbnailCache);
	//	//hr = CoCreateInstance(CLSID_LocalThumbnailCache, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IThumbnailCache), (void**)&spThumbCache);

	//	if (SUCCEEDED(hr))
	//	{
	//		ComPtr<ISharedBitmap> spBitmap;
	//		WTS_CACHEFLAGS OutFlags = WTS_DEFAULT;
	//		WTS_THUMBNAILID ThumbnailID = { 0 };
	//		hr = spThumbCache->GetThumbnail(item, cxy_requested_thumb_size, WTS_EXTRACT, &spBitmap, &OutFlags, &ThumbnailID);

	//		if (SUCCEEDED(hr))
	//		{
	//			HBITMAP hBitmap = nullptr;
	//			WTS_ALPHATYPE alpha_type = WTSAT_UNKNOWN;

	//			if (SUCCEEDED(spBitmap->GetFormat(&alpha_type)) &&
	//				SUCCEEDED(spBitmap->GetSharedBitmap(&hBitmap)))
	//			{
	//				df::blob data;
	//				if (save_hbitmap_to_memory(hBitmap, data, alpha_type == WTSAT_ARGB))
	//				{
	//					thumbnail_out = render::image(data);
	//					result = get_cached_file_properties_response::ok;
	//				}
	//			}
	//		}
	//		else if (hr == WTS_E_EXTRACTIONPENDING || hr == E_PENDING)
	//		{
	//			result = get_cached_file_properties_response::pending;
	//		}
	//	}
	//}


	//if (SUCCEEDED(SHCreateItemFromParsingName(path_w.c_str(), NULL, IID_PPV_ARGS(&spsiif))))
	//{
	//	SIZE size = { cxy_requested_thumb_size, cxy_requested_thumb_size };
	//	HBITMAP hBitmap = nullptr;
	//	auto hr = spsiif->GetImage(size, SIIGBF_BIGGERSIZEOK | SIIGBF_THUMBNAILONLY, &hBitmap);

	//	if (SUCCEEDED(hr))
	//	{
	//		df::blob data;
	//		if (save_hbitmap_to_memory(hBitmap, data, false))
	//		{
	//			thumbnail_out = render::image(data);
	//			result = get_cached_file_properties_response::ok;
	//		}

	//		DeleteObject(hBitmap);
	//	}
	//	else if (hr == WTS_E_EXTRACTIONPENDING || hr == E_PENDING)
	//	{
	//		result = get_cached_file_properties_response::pending;
	//	}
	//}

	// Try alternative method if no result
	//if (result == get_cached_file_properties_response::fail)
	//{
	//	ComPtr<IShellItem2> item;
	//	HRESULT hr = SHCreateItemFromParsingName(path.to_file_system_path().c_str(), nullptr /*bindContext*/, IID_PPV_ARGS(&item));

	//	if (SUCCEEDED(hr))
	//	{
	//		//ComPtr<IBindCtx> bindCtx;
	//		//hr = CreateBindCtx(0, &bindCtx);		

	//		//if (SUCCEEDED(hr))
	//		//{
	//		//	BIND_OPTS bo = { sizeof(bo) };
	//		//	hr = bindCtx->GetBindOptions(&bo);
	//		//	if (SUCCEEDED(hr))
	//		//	{
	//		//		bo.grfMode = STGM_READ | STGM_SHARE_DENY_WRITE;
	//		//		hr = bindCtx->SetBindOptions(&bo);
	//		//	}

	//		//	//hr = bindCtx->RegisterObjectParam(const_cast<PWSTR>(STR_BIND_THUMBNAIL_STREAM), punk);
	//		//	//hr = bindCtx.SetObject(STR_BIND_THUMBNAIL_STREAM, spThumbStreamInfo.Get());
	//		//	//
	//		//	if (SUCCEEDED(hr))
	//		//	{
	//		//		ComPtr<IStream> spstm;
	//		//		hr = item->BindToHandler(bindCtx, BHID_Stream, IID_PPV_ARGS(&spstm));
	//		//		if (SUCCEEDED(hr))
	//		//		{
	//		//		}
	//		//	}
	//		//}	


	//		//ComPtr<IPropertyStore> spstore;
	//		//static PROPERTYKEY const rgProps[] = {
	//		//	INIT_PKEY_Thumbnail,
	//		//	INIT_PKEY_ThumbnailStream,
	//		//	INIT_PKEY_ImageParsingName,
	//		//};
	//		//hr = item->GetPropertyStoreForKeys(rgProps, ARRAYSIZE(rgProps), GPS_DEFAULT, IID_PPV_ARGS(&spstore));
	//		//if (SUCCEEDED(hr))
	//		//{
	//		//	PROPVARIANT propvar;
	//		//	hr = spstore->GetValue(PKEY_Thumbnail, &propvar);
	//		//	if (SUCCEEDED(hr) && (propvar.vt != VT_EMPTY))
	//		//	{
	//		//		//hr = CreateThumbnailFromClipboardProperty(propvar, _rgSize, phbmp);
	//		//		PropVariantClear(&propvar);
	//		//	}
	//		//	else
	//		//	{
	//		//		ComPtr<IStream> spstm;
	//		//		hr = IPropertyStore_GetUnknown(spstore, PKEY_ThumbnailStream, IID_PPV_ARGS(&spstm));
	//		//		if (SUCCEEDED(hr))
	//		//		{
	//		//			//hr = CreateHBITMAPFromStream(spstm.Get(), _rgSize, nullptr, phbmp);
	//		//		}
	//		//		else
	//		//		{
	//		//			PROPVARIANT spropvarImageParsingName;
	//		//			hr = spstore->GetValue(PKEY_ImageParsingName, &spropvarImageParsingName);
	//		//			if (SUCCEEDED(hr))
	//		//			{
	//		//				ComPtr<IShellItem> spsiParent;
	//		//				hr = item->GetParent(&spsiParent);
	//		//				if (SUCCEEDED(hr))
	//		//				{
	//		//					//hr = CreateHBITMAPFromParsingNames(spstore.Get(), spsiParent.Get(), spropvarImageParsingName, &_rgSize, IPN_Default, phbmp, pdwAlpha);
	//		//				}

	//		//				PropVariantClear(&propvar);
	//		//			}
	//		//		}
	//		//	}
	//		//}

	//		/*auto x = PS_FULL_PRIMARY_STREAM_AVAILABLE;

	//		ULONG ulStatus = 0;
	//		hr = item->get_uint32(PKEY_FilePlaceholderStatus, &ulStatus);

	//		if (SUCCEEDED(hr))
	//		{
	//			if (ulStatus == 7)
	//			{
	//				hr = S_OK;
	//			}
	//			else
	//			{
	//				hr = S_FALSE;
	//			}
	//		}

	//		*/

	//		/*ComPtr<IThumbnailProvider> pThumbnailProvider;
	//		ComPtr<IThumbnailStreamProvider> pThumbnailStreamProvider;
	//		hr = item->BindToHandler(nullptr, BHID_ThumbnailHandler, IID_PPV_ARGS(&pThumbnailProvider));

	//		if (SUCCEEDED(hr))
	//		{
	//			WTS_ALPHATYPE at = WTSAT_UNKNOWN;
	//			HBITMAP hbm = nullptr;

	//			hr = pThumbnailProvider->GetThumbnail(cxy_requested_thumb_size, &hbm, &at);

	//			if (SUCCEEDED(hr))
	//			{
	//				BITMAP bm;
	//				GetObject(hbm, sizeof(BITMAP), &bm);

	//				df::blob data;
	//				if (save_hbitmap_to_memory(hbm, data, at == WTSAT_ARGB))
	//				{
	//					thumbnail_out = render::image(data);
	//					result = get_cached_file_properties_response::ok;
	//				}

	//				::DeleteObject(hbm);
	//			}
	//			else if (hr == WTS_E_EXTRACTIONTIMEDOUT || hr == WTS_E_EXTRACTIONPENDING || hr == E_PENDING)
	//			{
	//				result = get_cached_file_properties_response::pending;
	//			}
	//		}*/

	//		//ComPtr<IThumbnailCache> spThumbCache;
	//		//hr = spThumbCache.CoCreateInstance(CLSID_LocalThumbnailCache);
	//		////hr = CoCreateInstance(CLSID_LocalThumbnailCache, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IThumbnailCache), (void**)&spThumbCache);

	//		//if (SUCCEEDED(hr))
	//		//{
	//		//	ComPtr<ISharedBitmap> spBitmap;
	//		//	WTS_CACHEFLAGS OutFlags = WTS_DEFAULT;
	//		//	WTS_THUMBNAILID ThumbnailID = {0};
	//		//	hr = spThumbCache->GetThumbnail(item, cxy_requested_thumb_size, WTS_EXTRACT, &spBitmap, &OutFlags, &ThumbnailID);

	//		//	if (SUCCEEDED(hr))
	//		//	{
	//		//		HBITMAP hBitmap = nullptr;
	//		//		WTS_ALPHATYPE alpha_type = WTSAT_UNKNOWN;

	//		//		if (SUCCEEDED(spBitmap->GetFormat(&alpha_type)) &&
	//		//			SUCCEEDED(spBitmap->GetSharedBitmap(&hBitmap)))
	//		//		{
	//		//			df::blob data;
	//		//			if (save_hbitmap_to_memory(hBitmap, data, alpha_type == WTSAT_ARGB))
	//		//			{
	//		//				thumbnail_out = render::image(data);
	//		//				result = get_cached_file_properties_response::ok;
	//		//			}
	//		//		}
	//		//	}
	//		//	else if (hr == WTS_E_EXTRACTIONPENDING || hr == E_PENDING)
	//		//	{
	//		//		result = get_cached_file_properties_response::pending;
	//		//	}
	//		//}
	//	}
	//}

	return result;
}


platform::drives platform::scan_drives(const bool scan_contents)
{
	drives results;
	const auto drives = GetLogicalDrives();

	for (int i = 0; i < 26; ++i)
	{
		if (drives & 1 << i)
		{
			wchar_t szDrive[] = L"?:\\";
			szDrive[0] = L'A' + i;

			const auto drive_type = ::GetDriveType(szDrive);

			drive_t d;

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

				d.type = drive_type::fixed;

				switch (drive_type)
				{
				case DRIVE_REMOVABLE: d.type = drive_type::removable;
					break;
				case DRIVE_REMOTE: d.type = drive_type::remote;
					break;
				case DRIVE_CDROM: d.type = drive_type::cdrom;
					break;
				}

				ULARGE_INTEGER free_bytes_available_to_caller, total_number_of_bytes, total_number_of_free_bytes;

				const auto success = GetDiskFreeSpaceEx(szDrive, &free_bytes_available_to_caller,
				                                        &total_number_of_bytes, &total_number_of_free_bytes) != FALSE;
				df::file_size result;

				if (success)
				{
					d.used = total_number_of_bytes.QuadPart - total_number_of_free_bytes.QuadPart;
					d.free = free_bytes_available_to_caller.QuadPart;
					d.capacity = total_number_of_bytes.QuadPart;
				}

				d.name = str::utf16_to_utf8(szDrive);
				results.emplace_back(d);
			}
		}
	}

	/*if (scan_devices)
	{
		DWORD deviceCount = 0;
		ComPtr<IPortableDeviceManager> deviceManager;

		if (SUCCEEDED(deviceManager.CoCreateInstance(CLSID_PortableDeviceManager)))
		{
			if (SUCCEEDED(deviceManager->GetDevices(nullptr, &deviceCount)) && deviceCount > 0)
			{
				auto deviceIDs = new PWSTR[deviceCount];
				ZeroMemory(deviceIDs, deviceCount * sizeof(PWSTR));

				auto retrievedDeviceIDCount = deviceCount;

				if (SUCCEEDED(deviceManager->GetDevices(deviceIDs, &retrievedDeviceIDCount)))
				{
					for (auto i = 0u; i < retrievedDeviceIDCount; i++)
					{
						const auto id = deviceIDs[i];
						const auto bufLen = 200;

						WCHAR name[bufLen];
						DWORD nameLen = bufLen;

						WCHAR description[bufLen];
						DWORD descriptionLen = bufLen;

						WCHAR manufacturer[bufLen];
						DWORD manufacturerLen = bufLen;

						if (SUCCEEDED(deviceManager->GetDeviceFriendlyName(id, name, &nameLen)) &&
							SUCCEEDED(deviceManager->GetDeviceDescription(id, description, &descriptionLen)) &&
							SUCCEEDED(deviceManager->GetDeviceManufacturer(id, manufacturer, &manufacturerLen)))
						{
							if (df::folder_path::is_drive(name))
							{
								std::wstring text;

								if (wcscmp(name, description) != 0)
								{
									text = description;
									text += L"\n";
								}

								text += manufacturer;

								results.emplace_back(drive_t(drive_type::device, id, name, text, get_drive_size(id)));
							}
						}
					}
				}

				for (DWORD index = 0; index < retrievedDeviceIDCount; index++)
				{
					CoTaskMemFree(deviceIDs[index]);
					deviceIDs[index] = nullptr;
				}

				delete[] deviceIDs;
				deviceIDs = nullptr;
			}
		}
	}*/

	return results;
}


static constexpr LCID default_locale = LOCALE_USER_DEFAULT;
static char decimal_sep[10];
static char thousand_sep[10];
static NUMBERFMTA fmt;
std::atomic_bool number_format_invalid = true;
static std::mutex number_format_mutex;

void validate_number_format()
{
	if (number_format_invalid)
	{
		std::lock_guard lock(number_format_mutex);

		if (number_format_invalid)
		{
			GetLocaleInfoA(default_locale, LOCALE_SDECIMAL, decimal_sep, 6);
			GetLocaleInfoA(default_locale, LOCALE_STHOUSAND, thousand_sep, 6);

			//GetLocaleInfoW(lcid, LOCALE_RETURN_NUMBER|LOCALE_IDIGITS, (LPSTR) &fmt.NumDigits, sizeof(uint32_t));
			GetLocaleInfoA(default_locale, LOCALE_RETURN_NUMBER | LOCALE_ILZERO, std::bit_cast<LPSTR>(&fmt.LeadingZero),
			               sizeof(uint32_t));
			GetLocaleInfoA(default_locale, LOCALE_RETURN_NUMBER | LOCALE_INEGNUMBER,
			               std::bit_cast<LPSTR>(&fmt.NegativeOrder), sizeof(uint32_t));

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

	static constexpr int size = 64;
	char result[size] = {0};
	GetNumberFormatA(default_locale, 0, std::bit_cast<const char*>(num_text.c_str()), &fmt, result, size);
	return str::utf8_cast2(result);
}

std::string platform::number_dec_sep()
{
	validate_number_format();
	return str::utf8_cast2(decimal_sep);
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
		DWORD read = 0;
		if (!ReadFile(_h, buf, static_cast<uint32_t>(buf_size), &read, nullptr)) return 0;
		return read;
	}

	uint64_t write(const uint8_t* data, const uint64_t size) override
	{
		DWORD written = 0;
		if (!WriteFile(_h, data, static_cast<DWORD>(size), &written, nullptr)) return 0;
		return written;
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
		FILETIME ftm;
		GetFileTime(_h, &ftm, nullptr, nullptr);
		return df::date_t(ft_to_ts(ftm));
	}

	void set_created(const df::date_t date) override
	{
		const auto ftm = ts_to_ft(date._i);
		SetFileTime(_h, &ftm, nullptr, nullptr);
	}

	df::date_t get_modified() override
	{
		FILETIME ftm;
		GetFileTime(_h, nullptr, nullptr, nullptr);
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
			wchar_t path[MAX_PATH];
			const auto dwRet = GetFinalPathNameByHandle(_h, path, MAX_PATH, VOLUME_NAME_NT);

			if (dwRet < MAX_PATH)
			{
				return df::file_path(path);
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

uint32_t platform::file_crc32(const df::file_path path)
{
	bool success = false;
	uint32_t result = crypto::CRCINIT;

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
			return 0;
		}

		const auto size = static_cast<uint64_t>(li.QuadPart);
		constexpr uint32_t max_chunk = df::two_fifty_six_k;
		const auto buffer = df::unique_alloc<uint8_t>(max_chunk);

		DWORD dwReadChunk = 0UL;
		uint64_t total_read = 0ULL;


		do
		{
			const auto read_size = std::min(max_chunk, static_cast<uint32_t>(size));
			if (ReadFile(hFile, buffer.get(), read_size, &dwReadChunk, nullptr))
			{
				result = crypto::crc32c(result, buffer.get(), dwReadChunk);
				total_read += dwReadChunk;
			}
			else
			{
				dwReadChunk = 0;
			}
		}
		while (total_read < size && dwReadChunk > 0 && !df::is_closing);

		success = total_read == size;
		CloseHandle(hFile);
	}

	return success ? ~result : 0;
}

bool platform::eject(const df::folder_path path)
{
	ULONG returned = 0, res = 0;

	constexpr auto shareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;
	constexpr auto accessMode = GENERIC_WRITE | GENERIC_READ;
	const auto vol = "\\\\.\\"s + path.text()[0] + path.text()[1];
	const auto vol_w = str::utf8_to_utf16(vol);

	auto* hDevice = CreateFile(vol_w.c_str(), accessMode, shareMode, nullptr, OPEN_EXISTING, 0, nullptr);

	if (hDevice == INVALID_HANDLE_VALUE)
	{
		df::log(__FUNCTION__, std::format("IOCTL_STORAGE_EJECT_MEDIA: {}", last_os_error()));
		return false;
	}

	res = DeviceIoControl(hDevice, FSCTL_DISMOUNT_VOLUME, nullptr, 0, nullptr, 0, &returned, nullptr);

	if (!res)
	{
		df::log(__FUNCTION__, std::format("FSCTL_DISMOUNT_VOLUME: {}", last_os_error()));
	}

	PREVENT_MEDIA_REMOVAL PMRBuffer;
	PMRBuffer.PreventMediaRemoval = FALSE;

	res = DeviceIoControl(hDevice, IOCTL_STORAGE_MEDIA_REMOVAL, &PMRBuffer, sizeof(PREVENT_MEDIA_REMOVAL), nullptr, 0,
	                      &returned, nullptr);

	if (!res)
	{
		df::log(__FUNCTION__, std::format("IOCTL_STORAGE_MEDIA_REMOVAL: {}", last_os_error()));
	}

	res = DeviceIoControl(hDevice, IOCTL_STORAGE_EJECT_MEDIA, nullptr, 0, nullptr, 0, &returned, nullptr);

	if (!res)
	{
		df::log(__FUNCTION__, std::format("IOCTL_STORAGE_EJECT_MEDIA: {}", last_os_error()));
	}

	res = CloseHandle(hDevice);

	return res != 0;
}

bool platform::is_server(const std::string_view path)
{
	static std::regex e(R"(^[\\/]*([^\\\/]+)[\\\/]*$)");
	std::match_results<std::string_view::const_iterator> m;
	return std::regex_match(path.begin(), path.end(), m, e);
}

df::file_path platform::running_app_path()
{
	wchar_t sz[MAX_PATH];
	GetModuleFileName(get_resource_instance, sz, MAX_PATH);
	return df::file_path(sz);
}

size_t platform::calc_optimal_read_size(const df::file_path path)
{
	const auto sz = path.folder().text();
	wchar_t d = 0;

	if (!is_empty(sz) &&
		std::isalpha(sz[0]) &&
		sz[1] == ':' &&
		df::is_path_sep(sz[2]))
	{
		d = str::to_lower(sz[0]);
	}

	if (d >= 'a' && d <= 'z')
	{
		static size_t cached[26] = {0};
		const auto cached_val = cached[d - 'a'];

		if (cached_val)
		{
			return cached_val;
		}

		DWORD sectorsPerCluster = 0;
		DWORD bytesPerSector = 0;
		DWORD numberOfFreeClusters = 0;
		DWORD totalNumberOfClusters = 0;

		const wchar_t drive[] = {d, ':', '\\', 0};
		const auto success = GetDiskFreeSpace(drive, &sectorsPerCluster, &bytesPerSector, &numberOfFreeClusters,
		                                      &totalNumberOfClusters) != FALSE;

		if (success)
		{
			const auto block_size = bytesPerSector * sectorsPerCluster;
			cached[d - 'a'] = block_size;
			return block_size;
		}
	}

	return 16 * 1024; // default
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
							results.emplace(df::folder_path(spszName));
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
	return df::folder_path(sz);
}

static df::folder_path app_data()
{
	const auto folder = path_from_csidl(CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE).combine(s_app_name);
	platform::create_folder(folder);
	return folder;
}

static df::folder_path app_cache_data()
{
#ifdef WINSTORE
	// For Store apps, use the LocalCache folder as required by Microsoft
	// This uses raw COM to get the proper package-aware cache location
	// Note: LocalCacheFolder is on IApplicationData3, not IApplicationData
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
			// LocalCacheFolder is on IApplicationData2
			ComPtr<ABI::Windows::Storage::IApplicationData2> app_data3;
			hr = app_data.As(&app_data3);

			if (SUCCEEDED(hr))
			{
				ComPtr<ABI::Windows::Storage::IStorageFolder> cache_folder;
				hr = app_data3->get_LocalCacheFolder(&cache_folder);

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

	return result;
#else
	// For Desktop apps, preserve backwards compatibility with existing location
	// Existing users already have cache data at %LOCALAPPDATA%\Diffractor
	const auto folder = path_from_csidl(CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE).combine(s_app_name);
	platform::create_folder(folder);
	return folder;
#endif
}

static df::folder_path shell_known_folder(REFIID id)
{
	df::folder_path result;
	PWSTR path;

	if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &path)) && path)
	{
		result = df::folder_path(path);
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

		if (info.HasMember("personal"))
		{
			const df::folder_path personal_path(df::util::json::safe_string(info["personal"], "path"));
			if (personal_path.exists())
			{
				const auto result = personal_path.combine(sub_folder);

				if (result.exists())
				{
					return result;
				}
			}
		}

		if (info.HasMember("business"))
		{
			const df::folder_path business_path(df::util::json::safe_string(info["business"], "path"));

			if (business_path.exists())
			{
				const auto result = business_path.combine(sub_folder);

				if (result.exists())
				{
					return result;
				}
			}
		}
	}

	return dropbox;
}

static df::folder_path onedrive_root_folder()
{
	df::folder_path result;

	HKEY hKey;
	DWORD dwLen = MAX_PATH;
	wchar_t path[MAX_PATH] = {0};
	DWORD dwType = 0;
	DWORD dwRetVal = 0;

	if (ERROR_SUCCESS == (dwRetVal = RegOpenKeyEx(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\OneDrive", 0,
	                                              KEY_QUERY_VALUE, &hKey)))
	{
		if (ERROR_SUCCESS == (dwRetVal = RegQueryValueEx(hKey, L"UserFolder", nullptr, &dwType, (LPBYTE)path, &dwLen)))
		{
			if (dwType == REG_SZ)
			{
				result = df::folder_path(path);
			}
		}

		RegCloseKey(hKey);
	}
	else
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

	const auto drives = scan_drives(false);

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

	if (onedrive_pictures.exists())
	{
		result.onedrive_video = onedrive_video;
	}

	const auto onedrive_music = known_path(known_folder::onedrive_music);

	if (onedrive_pictures.exists())
	{
		result.onedrive_music = onedrive_music;
	}

	return result;
}

std::string platform::user_language()
{
	wchar_t sz[17];
	int ccBuf = GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SISO639LANGNAME, sz, 8) - 1;
	sz[ccBuf++] = '_';
	ccBuf += GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SISO3166CTRYNAME, sz + ccBuf, 8) - 1;
	return str::utf16_to_utf8({sz, static_cast<size_t>(ccBuf)});
}

bool platform::mapi_send(const std::string_view to, const std::string_view subject, const std::string_view text,
                         const attachments_t& attachments)
{
	df::assert_true(ui::is_ui_thread());

	auto* hwndParent = app_wnd();

	// some extra precautions are required to use MAPISendMail as it
	// tends to enable the parent window in between dialogs (after
	// the login dialog, but before the send note dialog).
	SetCapture(hwndParent);
	SetFocus(nullptr);

	HINSTANCE handle = LoadLibraryExA("MAPI32.DLL", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	//::LoadLibrary(L"MAPI32.DLL");
	bool success = false;

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

				fd.nPosition = 0xFFFFFFFF;
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

			auto result_code = send_mail_w(0, std::bit_cast<ULONG_PTR>(hwndParent), &message_w,
			                               MAPI_LOGON_UI | MAPI_DIALOG, 0);

			success = result_code == SUCCESS_SUCCESS ||
				result_code == MAPI_USER_ABORT ||
				result_code == MAPI_E_LOGIN_FAILURE;
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

			auto result_code = send_mail(0, std::bit_cast<ULONG_PTR>(hwndParent), &message_a,
			                             MAPI_LOGON_UI | MAPI_DIALOG, 0);

			success = result_code == SUCCESS_SUCCESS ||
				result_code == MAPI_USER_ABORT ||
				result_code == MAPI_E_LOGIN_FAILURE;
		}

		FreeLibrary(handle);
	}

	// after returning from the MAPISendMail call, the window must
	// be re-enabled and focus returned to the frame to undo the workaround
	// done before the MAPI call.
	ReleaseCapture();

	EnableWindow(hwndParent, TRUE);
	SetActiveWindow(nullptr);
	SetActiveWindow(hwndParent);
	SetFocus(hwndParent);

	if (hwndParent != nullptr)
		EnableWindow(hwndParent, TRUE);

	return success;
}

uint32_t platform::tick_count()
{
	return GetTickCount();
}

uint32_t platform::current_thread_id()
{
	return GetCurrentThreadId();
}

platform::thread_init::thread_init()
{
	// https://support.microsoft.com/en-us/help/287087/info-calling-shell-functions-and-interfaces-from-a-multithreaded-apart
	_hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
}

platform::thread_init::~thread_init()
{
	if (_hr == S_OK) CoUninitialize();
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

class CPropVariant : public PROPVARIANT
{
public:
	CPropVariant()
	{
		memset(this, 0, sizeof(this));
	}

	~CPropVariant()
	{
		const HRESULT hr = PropVariantClear(this);
		df::assert_true(hr == S_OK);
		(void)hr;
	}

	bool IsType(_In_ const VARTYPE type) const
	{
		return vt == type;
	}

	PROPVARIANT* operator &()
	{
		return this;
	}
};


static void confirm(const HRESULT hr, const std::string_view context)
{
	if (FAILED(hr))
	{
		throw app_exception(std::format("{} hr={:x}", context, hr));
	}
}

df::blob platform::from_file(const df::file_path path)
{
	df::file f;

	if (f.open_read(path, true))
	{
		const auto file_len = f.file_size();

		if (file_len > df::max_blob_size)
		{
			throw app_exception(std::format("Cannot read file into memory ({} bytes)", file_len));
		}

		return f.read_blob(file_len);
	}

	return {};
}


FILETIME ts_to_ft(const uint64_t ts)
{
	FILETIME ft;
	ft.dwHighDateTime = static_cast<uint32_t>(ts >> 32);
	ft.dwLowDateTime = static_cast<uint32_t>(ts & 0xffffffffffull);
	return ft;
}

uint64_t platform::utc_to_local(const uint64_t ts)
{
	const auto ft = ts_to_ft(ts);
	FILETIME result;
	FileTimeToLocalFileTime(&ft, &result);
	return ft_to_ts(result);
}

uint64_t platform::local_to_utc(const uint64_t ts)
{
	const auto ft = ts_to_ft(ts);
	FILETIME result;
	LocalFileTimeToFileTime(&ft, &result);
	return ft_to_ts(result);
}

df::date_t platform::dos_date_to_ts(const uint16_t dos_date, const uint16_t dos_time)
{
	FILETIME ft_local;
	FILETIME ft_utc;
	DosDateTimeToFileTime(dos_date, dos_time, &ft_local);
	LocalFileTimeToFileTime(&ft_local, &ft_utc);
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
	LARGE_INTEGER tps = {0};
	QueryPerformanceFrequency(&tps);

	LARGE_INTEGER pc = {0};
	QueryPerformanceCounter(&pc);
	return static_cast<double>(pc.QuadPart) / static_cast<double>(tps.QuadPart);
}

int64_t df::now_ms()
{
	LARGE_INTEGER tps = {0};
	QueryPerformanceFrequency(&tps);

	LARGE_INTEGER pc = {0};
	QueryPerformanceCounter(&pc);
	return pc.QuadPart * 1000 / tps.QuadPart;
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

bool platform::set_files_dates(const df::file_path path, const uint64_t dt_created, const uint64_t dt_modified)
{
	const auto w = to_file_system_path(path);

	BOOL result = FALSE;
	const HANDLE h = CreateFile(w.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
	                            FILE_ATTRIBUTE_NORMAL,
	                            nullptr);

	if (h != INVALID_HANDLE_VALUE)
	{
		const auto ft_created = ts_to_ft(dt_created);
		const auto ft_modified = ts_to_ft(dt_modified);
		result = SetFileTime(h, &ft_created, nullptr, &ft_modified);
		CloseHandle(h);
	}

	return result != FALSE;
}


platform::mutex::mutex()
{
	InitializeSRWLock(std::bit_cast<PSRWLOCK>(&_cs));
}

platform::mutex::~mutex()
{
	// ??
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

	auto* h = ::CreateEventEx(nullptr, nullptr, flags, EVENT_ALL_ACCESS);

	if (h == nullptr)
	{
		throw app_exception(last_os_error());
	}

	_h = h;
}

platform::thread_event::~thread_event()
{
	if (_h.has_value())
	{
		auto* const h = std::any_cast<HANDLE>(_h);
		_h.reset();
		CloseHandle(h);
	}
}

//
//bool platform::thread_event::create(LPSECURITY_ATTRIBUTES pSecurity, bool manual_reset, bool initial_state, LPCTSTR pszName) noexcept
//{
//	DWORD dwFlags = 0;
//
//	if (manual_reset)
//		dwFlags |= CREATE_EVENT_MANUAL_RESET;
//
//	if (initial_state)
//		dwFlags |= CREATE_EVENT_INITIAL_SET;
//
//	_h = std::bit_cast<uintptr_t>(::CreateEventEx(pSecurity, pszName, dwFlags, EVENT_ALL_ACCESS));
//
//	return (_h != 0);
//}
//
//bool platform::thread_event::open(_In_ DWORD dwAccess, _In_ BOOL bInheritHandle, _In_z_ LPCTSTR pszName) noexcept
//{
//	_h = std::bit_cast<uintptr_t>(::OpenEvent(dwAccess, bInheritHandle, pszName));
//	return (_h != 0);
//}

void platform::thread_event::reset() const noexcept
{
	ResetEvent(std::any_cast<HANDLE>(_h));
}

void platform::thread_event::set() const noexcept
{
	SetEvent(std::any_cast<HANDLE>(_h));
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


	auto i = name.begin();
	while (i < name.end())
	{
		const auto c = str::pop_utf8_char(i, name.end());

		if (c < 128 && invalid_chars.find(static_cast<char>(c)) != std::string_view::npos)
		{
			return false;
		}
	}

	for (const auto& reserved : reserved_names)
	{
		if (str::icmp(reserved, name) == 0)
		{
			return false;
		}
	}

	return true;
}
