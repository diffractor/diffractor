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
#include <VersionHelpers.h>
#include <commdlg.h>
#include <wia.h>
#include <Thumbcache.h>
#include <shobjidl.h>   // SHGetPropertyStoreFromParsingName, etc
#include <propkey.h>    // PKEY_Keywords, PKEY_Title, PKEY_Rating
#include <propvarutil.h>// InitPropVariantFromStringVector
#include <lm.h>
#include <Shellapi.h>
#include <Softpub.h>
#include <WinInet.h>
#include <ShlObj.h>


#include "app_text.h"
#include "files.h"
#include "util.h"
#include "util_strings.h"
#include "platform_win_res.h"

#pragma comment(lib, "Wininet")
#pragma comment(lib, "mfuuid")
#pragma comment(lib, "dsound")
#pragma comment(lib, "comctl32")
#pragma comment(lib, "Psapi")
#pragma comment(lib, "Msimg32")
#pragma comment(lib, "UxTheme")
#pragma comment(lib, "PortableDeviceGUIDs")
#pragma comment(lib, "Crypt32")
#pragma comment(lib, "Bcrypt")
#pragma comment(lib, "Wintrust")
#pragma comment(lib, "Imm32")
#pragma comment(lib, "wiaguid")
#pragma comment(lib, "Propsys")
#pragma comment(lib, "Dwmapi")
#pragma comment(lib, "Netapi32")
#pragma comment(lib, "Advapi32")

#ifdef WINSTORE
#include <roapi.h>
#include <windows.storage.h>
#include <windows.system.h>
#pragma comment(lib, "runtimeobject")
#endif

//#pragma comment(lib, "SetupAPI")

size_t platform::static_memory_usage = 0;
platform::thread_event platform::event_exit(true, false);


static_assert(std::is_trivially_copyable_v<platform::file_info>);
static_assert(std::is_trivially_copyable_v<platform::folder_info>);
static_assert(std::is_move_constructible_v<platform::folder_contents>);

struct clipboard_formats
{
	static uint32_t PREFERREDDROPEFFECT;
	static uint32_t SHELLIDLIST;

	static FORMATETC Bitmap;
	static FORMATETC PDE;
	static FORMATETC Drop;
	static FORMATETC DropShellItems;
};

void __cdecl debug_printf(const char* fmt, ...)
{
	char buffer[256];
	va_list ap;

	va_start(ap, fmt);
	_vsnprintf_s(buffer, sizeof(buffer), std::bit_cast<const char*>(fmt), ap);
	OutputDebugStringA(buffer);
	va_end(ap);
}

bool platform::clipboard_has_files_or_image()
{
	return IsClipboardFormatAvailable(CF_HDROP) || IsClipboardFormatAvailable(CF_DIB);
}

std::string win32_to_string(const IID& iid)
{
	LPOLESTR sz = nullptr;
	auto result = "?"s;

	if (SUCCEEDED(StringFromIID(iid, &sz)))
	{
		result = str::utf16_to_utf8(sz);
		CoTaskMemFree(sz);
	}

	return result;
}

class items_data_object final : public IDataObject, df::no_copy
{
protected:
	// Information remembered for dataobject
	df::file_path _path;
	std::vector<df::file_path> _files;
	std::vector<df::folder_path> _folders;

	std::atomic_int _refs = 0;

	bool _has_image = false;
	bool _is_move = false;
	file_load_result _loaded;
	bool _has_preferred_drop = false;

public:
	items_data_object() = default;
	~items_data_object() override = default;

	void cache(df::file_path path);
	void cache(const file_load_result& loaded);
	void set_for_move(bool is_move);
	void cache(const std::vector<df::file_path>& files, const std::vector<df::folder_path>& folders);

	STDMETHOD(QueryInterface)(_In_ REFIID iid, _Deref_out_ void** ppvObject) noexcept override
	{
		df::trace(std::format("items_data_object::QueryInterface {}", win32_to_string(iid)));

		if (IsEqualGUID(iid, IID_IDataObject))
		{
			*ppvObject = static_cast<IDataObject*>(this);
			AddRef();
			return S_OK;
		}

		if (IsEqualGUID(iid, IID_IUnknown))
		{
			*ppvObject = static_cast<IUnknown*>(this);
			AddRef();
			return S_OK;
		}

		return E_NOINTERFACE;
	}

	STDMETHOD_(ULONG, AddRef)() noexcept override
	{
		return _refs.fetch_add(1) + 1;
	}

	STDMETHOD_(ULONG, Release)() noexcept override
	{
		const auto n = _refs.fetch_sub(1) - 1;
		if (n <= 0)
		{
			df::trace("items_data::release - delete");
			delete this;
		}
		return n;
	}


	// IDataObject
	STDMETHOD(GetData)(FORMATETC* pformatetcIn, STGMEDIUM* pmedium) override;
	STDMETHOD(GetDataHere)(FORMATETC* /* pformatetc */, STGMEDIUM* /* pmedium */) override;
	STDMETHOD(QueryGetData)(FORMATETC* /* pformatetc */) override;
	STDMETHOD(GetCanonicalFormatEtc)(FORMATETC* /* pformatectIn */, FORMATETC* /* pformatetcOut */) override;
	STDMETHOD(SetData)(FORMATETC* /* pformatetc */, STGMEDIUM* /* pmedium */, BOOL /* fRelease */) override;
	STDMETHOD(EnumFormatEtc)(DWORD /* dwDirection */, IEnumFORMATETC** /* ppenumFormatEtc */) override;
	STDMETHOD(DAdvise)(FORMATETC* pformatetc, DWORD advf, IAdviseSink* pAdvSink, DWORD* pdwConnection) override;
	STDMETHOD(DUnadvise)(DWORD dwConnection) override;
	STDMETHOD(EnumDAdvise)(IEnumSTATDATA** ppenumAdvise) override;
};

class items_drop_source final : public IDropSource, df::no_copy
{
	std::atomic_int _refs = 0;

public:
	using DROPEFFECT = DWORD;

	STDMETHOD(QueryInterface)(_In_ REFIID iid, _Deref_out_ void** ppvObject) noexcept override
	{
		df::trace(std::format("items_drop_source::QueryInterface {}", win32_to_string(iid)));

		if (IsEqualGUID(iid, IID_IDropSource))
		{
			*ppvObject = static_cast<IDropSource*>(this);
			AddRef();
			return S_OK;
		}

		if (IsEqualGUID(iid, IID_IUnknown))
		{
			*ppvObject = static_cast<IUnknown*>(this);
			AddRef();
			return S_OK;
		}

		return E_NOINTERFACE;
	}

	STDMETHOD_(ULONG, AddRef)() noexcept override
	{
		return _refs.fetch_add(1) + 1;
	}

	STDMETHOD_(ULONG, Release)() noexcept override
	{
		const auto n = _refs.fetch_sub(1) - 1;
		if (n <= 0)
		{
			df::trace("items_drop_source::release - delete");
			delete this;
		}
		return n;
	}


	STDMETHODIMP QueryContinueDrag(const BOOL bEscapePressed, const DWORD dwKeyState) override
	{
		if (bEscapePressed || (dwKeyState & MK_RBUTTON) != 0)
		{
			return DRAGDROP_S_CANCEL;
		}

		if ((dwKeyState & MK_LBUTTON) == 0)
		{
			return DRAGDROP_S_DROP;
		}

		return S_OK;
	}

	STDMETHODIMP GiveFeedback(DROPEFFECT /*dropEffect*/) override
	{
		return DRAGDROP_S_USEDEFAULTCURSORS;
	}
};


//////////////////////////////////////////////////////////////////////


using FORMATETCLIST = std::vector<FORMATETC>;

uint32_t clipboard_formats::PREFERREDDROPEFFECT = RegisterClipboardFormat(CFSTR_PREFERREDDROPEFFECT);
uint32_t clipboard_formats::SHELLIDLIST = RegisterClipboardFormat(CFSTR_SHELLIDLIST);

FORMATETC clipboard_formats::Bitmap = {CF_BITMAP, nullptr, DVASPECT_CONTENT, -1, TYMED_GDI};
FORMATETC clipboard_formats::PDE = {
	static_cast<CLIPFORMAT>(PREFERREDDROPEFFECT), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL
};
FORMATETC clipboard_formats::Drop = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
FORMATETC clipboard_formats::DropShellItems = {
	static_cast<CLIPFORMAT>(SHELLIDLIST), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL
};


class CEnumFORMATETCImpl final : public IEnumFORMATETC, df::no_copy
{
	FORMATETCLIST _formats;
	size_t _cur = 0;
	std::atomic_int _refs = 1;

public:
	CEnumFORMATETCImpl(FORMATETCLIST ArrFE) : _formats(std::move(ArrFE))
	{
	}

	~CEnumFORMATETCImpl() override
	{
	}

	STDMETHOD(QueryInterface)(_In_ REFIID iid, _Deref_out_ void** ppvObject) noexcept override
	{
		df::trace(std::format("CEnumFORMATETCImpl::QueryInterface {}", win32_to_string(iid)));

		if (IsEqualGUID(iid, IID_IEnumFORMATETC))
		{
			*ppvObject = static_cast<IEnumFORMATETC*>(this);
			AddRef();
			return S_OK;
		}

		if (IsEqualGUID(iid, IID_IUnknown))
		{
			*ppvObject = static_cast<IUnknown*>(this);
			AddRef();
			return S_OK;
		}

		return E_NOINTERFACE;
	}

	STDMETHOD_(ULONG, AddRef)() noexcept override
	{
		return _refs.fetch_add(1) + 1;
	}

	STDMETHOD_(ULONG, Release)() noexcept override
	{
		const auto n = _refs.fetch_sub(1) - 1;
		if (n <= 0)
		{
			df::trace("CEnumFORMATETCImpl::release - delete");
			delete this;
		}
		return n;
	}

	void Add(const FORMATETC& fmtetc)
	{
		_formats.emplace_back(fmtetc);
	}

	//IEnumFORMATETC members
	STDMETHOD(Next)(ULONG, LPFORMATETC, ULONG*) override;
	STDMETHOD(Skip)(ULONG) override;
	STDMETHOD(Reset)() override;
	STDMETHOD(Clone)(IEnumFORMATETC**) override;
};


STDMETHODIMP CEnumFORMATETCImpl::Next(const ULONG celt, LPFORMATETC lpFormatEtc, ULONG* pceltFetched)
{
	df::trace(__FUNCTION__);
	if (pceltFetched != nullptr)
		*pceltFetched = 0;

	auto cReturn = celt;

	if (celt <= 0 || lpFormatEtc == nullptr || _cur >= _formats.size())
		return S_FALSE;

	if (pceltFetched == nullptr && celt != 1) // pceltFetched can be nullptr only for 1 item request
		return S_FALSE;

	while (_cur < _formats.size() && cReturn > 0)
	{
		*lpFormatEtc++ = _formats[_cur++];
		--cReturn;
	}
	if (pceltFetched != nullptr)
		*pceltFetched = celt - cReturn;

	return cReturn == 0 ? S_OK : S_FALSE;
}

STDMETHODIMP CEnumFORMATETCImpl::Skip(const ULONG celt)
{
	df::trace(__FUNCTION__);
	if (_cur + static_cast<int>(celt) >= _formats.size())
		return S_FALSE;

	_cur += celt;
	return S_OK;
}

STDMETHODIMP CEnumFORMATETCImpl::Reset()
{
	df::trace(__FUNCTION__);
	_cur = 0;
	return S_OK;
}


STDMETHODIMP CEnumFORMATETCImpl::Clone(IEnumFORMATETC FAR* FAR* ppCloneEnumFormatEtc)
{
	df::trace(__FUNCTION__);
	if (ppCloneEnumFormatEtc == nullptr)
		return E_POINTER;

	*ppCloneEnumFormatEtc = new CEnumFORMATETCImpl(_formats);
	return S_OK;
}

//////////////////////////////////////////////////////////////////////

std::string platform::utf16_to_utf8(const std::wstring_view text)
{
	if (text.empty()) return {};

	const auto len = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr,
	                                     nullptr);
	if (len <= 0) return {};

	std::string result(len, '\0');
	WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), len, nullptr, nullptr);
	return str::utf8_cast2(result);
}

std::wstring platform::utf8_to_utf16(const std::string_view text)
{
	if (text.empty()) return {};

	const auto len = MultiByteToWideChar(CP_UTF8, 0, std::bit_cast<const char*>(text.data()),
	                                     static_cast<int>(text.size()), nullptr, 0);
	if (len <= 0) return {};

	std::wstring result(len, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, std::bit_cast<const char*>(text.data()), static_cast<int>(text.size()),
	                    result.data(), len);
	return result;
}

std::string platform::normalize_nfc(const std::string_view text)
{
	// ASCII is already NFC - avoid the conversion round-trip entirely.
	if (str::is_ascii(text)) return std::string(text);

	const auto w = platform::utf8_to_utf16(text);
	if (w.empty()) return std::string(text);

	// First call estimates the required buffer (documented to be an upper bound).
	const auto est = NormalizeString(NormalizationC, w.c_str(), static_cast<int>(w.size()), nullptr, 0);
	if (est <= 0) return std::string(text); // error - leave text unchanged

	std::wstring out(static_cast<size_t>(est), L'\0');
	const auto len = NormalizeString(NormalizationC, w.c_str(), static_cast<int>(w.size()), out.data(), est);
	if (len <= 0) return std::string(text);

	out.resize(static_cast<size_t>(len));
	return platform::utf16_to_utf8(out);
}

std::string platform::utf8_to_a(const std::string_view utf8)
{
	std::string result;
	const auto length = MultiByteToWideChar(CP_UTF8, 0, std::bit_cast<LPCSTR>(utf8.data()),
	                                        static_cast<uint32_t>(utf8.size()), nullptr, 0);

	if (length > 0)
	{
		std::vector<wchar_t> wide;
		wide.resize(length + 1);

		MultiByteToWideChar(CP_UTF8, 0, std::bit_cast<LPCSTR>(utf8.data()), -1, wide.data(), length);

		size_t convertedChars = 0;
		result.resize(length + 1);
		wcstombs_s(&convertedChars, result.data(), length + 1, wide.data(), _TRUNCATE);
	}

	return result;
}

static bool is_folder(const DWORD attributes)
{
	return attributes != INVALID_FILE_ATTRIBUTES &&
		attributes & FILE_ATTRIBUTE_DIRECTORY;
}

static bool is_offline_attribute(const DWORD attributes)
{
	// Onedrive and GVFS use file attributes to denote files or directories that
	// may not be locally present and are only available "online". These files are applied one of
	// the two file attributes: FILE_ATTRIBUTE_RECALL_ON_OPEN or FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS.
	// When the attribute FILE_ATTRIBUTE_RECALL_ON_OPEN is set, skip the file during enumeration because the file
	// is not locally present at all. A file with FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS may be partially present locally.
	//
	// https://stackoverflow.com/questions/49301958/how-to-detect-onedrive-online-only-files
	//
	constexpr auto offline_mask = FILE_ATTRIBUTE_OFFLINE |
		FILE_ATTRIBUTE_RECALL_ON_OPEN |
		FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS |
		FILE_ATTRIBUTE_VIRTUAL;

	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & offline_mask) != 0;
}

static bool is_dots(const wchar_t* name)
{
	const auto* p = name;
	while (*p)
	{
		if (*p != '.') return false;
		p += 1;
	}

	return !str::is_empty(name);
}

static bool can_show_file(const wchar_t* name, const DWORD attributes, const bool show_hidden)
{
	if (str::is_empty(name)) return false;
	if (attributes == INVALID_FILE_ATTRIBUTES) return false;
	//if (attributes & FILE_ATTRIBUTE_OFFLINE) return false;
	if (!show_hidden && (attributes & FILE_ATTRIBUTE_HIDDEN) != 0) return false;
	return !is_folder(attributes) && !is_dots(name);
}

static bool can_show_folder(const wchar_t* name, const DWORD attributes, const bool show_hidden)
{
	if (str::is_empty(name)) return false;
	if (attributes == INVALID_FILE_ATTRIBUTES) return false;
	//if (attributes & FILE_ATTRIBUTE_OFFLINE) return false;
	if (!show_hidden && (attributes & FILE_ATTRIBUTE_HIDDEN) != 0) return false;
	return is_folder(attributes) && !is_dots(name);
}

static bool can_show_file_or_folder(const wchar_t* name, const DWORD attributes, const bool show_hidden)
{
	if (is_folder(attributes))
	{
		return can_show_folder(name, attributes, show_hidden);
	}
	return can_show_file(name, attributes, show_hidden);
}

static uint32_t file_attributes(const df::file_path path)
{
	const auto w = platform::to_file_system_path(path);
	return ::GetFileAttributes(w.c_str());
}

static uint32_t file_attributes(const df::folder_path path)
{
	const auto w = platform::to_file_system_path(path);
	return ::GetFileAttributes(w.c_str());
}

std::wstring platform::to_file_system_path(const df::file_path path)
{
	auto result = str::utf8_to_utf16(path.pack());
	if (result.size() >= MAX_PATH) result.insert(0, L"\\\\?\\");
	return result;
};

static std::wstring parse_special_path(const std::string_view sv)
{
	std::wstring result;

	PIDLIST_ABSOLUTE pidl = nullptr;
	constexpr SFGAOF stSFGAOFIn = 0;
	SFGAOF stSFGAOFOut = 0;

	const auto guid = str::utf8_to_utf16(sv);
	wchar_t path[MAX_PATH];

	if (SUCCEEDED(SHParseDisplayName(guid.c_str(), nullptr, &pidl, stSFGAOFIn, &stSFGAOFOut)))
	{
		if (SHGetPathFromIDList(pidl, path))
		{
			result = path;
		}

		CoTaskMemFree(pidl);
	}

	return result;
}

std::wstring platform::to_file_system_path(const df::folder_path path)
{
	std::wstring result;

	if (df::folder_path::is_guid_path(path.text()))
	{
		result = parse_special_path(path.text());
	}
	else
	{
		result = str::utf8_to_utf16(path.text());
	}

	if (result.size() >= MAX_PATH) result.insert(0, L"\\\\?\\");
	return result;
}


void items_data_object::cache(const df::file_path path)
{
	_path = path;
	_has_image = true;
}

void items_data_object::cache(const file_load_result& loaded)
{
	_loaded = loaded;
}

void items_data_object::cache(const std::vector<df::file_path>& files, const std::vector<df::folder_path>& folders)
{
	_files = files;
	_folders = folders;
}

void items_data_object::set_for_move(const bool is_move)
{
	_is_move = is_move;
	_has_preferred_drop = true;
}

static HGLOBAL create_shell_id_list(const std::vector<df::file_path>& files,
                                    const std::vector<df::folder_path>& folders)
{
	std::vector<PUIDLIST_RELATIVE> paths;
	size_t total_pidl_len = 0;

	for (const auto& path : files)
	{
		paths.emplace_back(ILCreateFromPath(platform::to_file_system_path(path).c_str()));
	}

	for (const auto& path : folders)
	{
		paths.emplace_back(ILCreateFromPath(platform::to_file_system_path(path).c_str()));
	}

	for (const auto& i : paths)
	{
		total_pidl_len += ILGetSize(i);
	}

	const auto cida_len = sizeof(CIDA) + (paths.size() + 1) * sizeof(uint32_t);
	auto* const hGlobal = GlobalAlloc(GPTR | GMEM_SHARE,
	                                  static_cast<DWORD>(cida_len + total_pidl_len + sizeof(uint32_t) + 1));

	if (!hGlobal)
		return nullptr;

	if (auto* pData = static_cast<LPIDA>(GlobalLock(hGlobal)))
	{
		auto index = 0;
		auto pos = static_cast<uint32_t>(cida_len);

		pData->cidl = static_cast<uint32_t>(paths.size());
		pData->aoffset[index] = pos;

		// parent folder
		*std::bit_cast<uint32_t*>(std::bit_cast<uint8_t*>(pData) + pos) = 0;

		pos += static_cast<uint32_t>(sizeof(uint32_t));
		index += 1;

		for (const auto& i : paths)
		{
			const auto cbPidl = ILGetSize(i);
			pData->aoffset[index] = pos;
			CopyMemory(std::bit_cast<uint8_t*>(pData) + pos, i, cbPidl);
			pos += cbPidl;
			index += 1;
		}

		GlobalUnlock(hGlobal);
	}

	for (const auto& i : paths)
	{
		ILFree(i);
	}

	return hGlobal;
}

static std::wstring all_file_system_paths(const std::vector<df::file_path>& files,
                                          const std::vector<df::folder_path>& folders)
{
	std::wstring result;
	constexpr wchar_t delim = 0;

	for (const auto& path : folders)
	{
		result += platform::to_file_system_path(path);
		result += delim;
	}

	for (const auto& path : files)
	{
		result += platform::to_file_system_path(path);
		result += delim;
	}

	result += delim;
	return result;
}


STDMETHODIMP items_data_object::GetData(FORMATETC* pformatetcIn, STGMEDIUM* pmedium)
{
	df::trace(__FUNCTION__);

	if (!pformatetcIn || !pmedium)
	{
		return E_INVALIDARG;
	}

	try
	{
		const auto has_paths = !_files.empty() || !_folders.empty();

		if (pformatetcIn->cfFormat == CF_DIB)
		{
			if (_has_image && !_loaded.success)
			{
				files ff;
				_loaded = ff.load(_path, false);
			}

			if (_loaded.success)
			{
				pmedium->tymed = TYMED_HGLOBAL;
				pmedium->hGlobal = image_to_handle(_loaded);
				pmedium->pUnkForRelease = nullptr;
				return S_OK;
			}
		}

		if (_has_preferred_drop && pformatetcIn->cfFormat == clipboard_formats::PREFERREDDROPEFFECT)
		{
			auto* const h = GlobalAlloc(GMEM_ZEROINIT | GMEM_MOVEABLE | GMEM_DDESHARE, sizeof(DWORD));

			if (h)
			{
				auto* const p = static_cast<DWORD*>(GlobalLock(h));
				if (p)
				{
					*p = _is_move ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
					GlobalUnlock(h);

					pmedium->tymed = TYMED_HGLOBAL;
					pmedium->hGlobal = h;
					pmedium->pUnkForRelease = nullptr;

					return S_OK;
				}
				GlobalFree(h);
			}
			else
			{
				return E_OUTOFMEMORY;
			}
		}

		if (pformatetcIn->cfFormat == clipboard_formats::SHELLIDLIST && has_paths)
		{
			pmedium->hGlobal = create_shell_id_list(_files, _folders);
			pmedium->tymed = TYMED_HGLOBAL;
			pmedium->pUnkForRelease = nullptr;
			return S_OK;
		}

		if (pformatetcIn->cfFormat == CF_HDROP && has_paths)
		{
			const auto paths = all_file_system_paths(_files, _folders);

			// must be double null terminated
			df::assert_true(paths[paths.size() - 1] == 0);
			df::assert_true(paths[paths.size() - 2] == 0);

			const auto len = paths.size();

			// Check for potential integer overflow
			if (len > SIZE_MAX / sizeof(wchar_t) ||
				len * sizeof(wchar_t) > SIZE_MAX - sizeof(DROPFILES))
			{
				df::log(__FUNCTION__, "Path data too large, potential overflow");
				return E_OUTOFMEMORY;
			}

			const auto text_alloc_len = len * sizeof(wchar_t);
			const auto allocLen = sizeof(DROPFILES) + text_alloc_len;

			auto* const h = GlobalAlloc(GMEM_ZEROINIT | GMEM_MOVEABLE | GMEM_DDESHARE, allocLen);

			if (h)
			{
				auto* const p = static_cast<DROPFILES*>(GlobalLock(h));

				if (p)
				{
					p->pFiles = sizeof(DROPFILES);
					p->fWide = 1;

					auto* const fileData = std::bit_cast<wchar_t*>(std::bit_cast<uint8_t*>(p) + sizeof(DROPFILES));
					memcpy_s(fileData, text_alloc_len, paths.data(), text_alloc_len);
					GlobalUnlock(h);

					pmedium->tymed = TYMED_HGLOBAL;
					pmedium->hGlobal = h;
					pmedium->pUnkForRelease = nullptr;
				}
			}

			return S_OK;
		}
	}
	catch (const std::exception& e)
	{
		df::log(__FUNCTION__, e.what());
		return E_UNEXPECTED;
	}

	return DV_E_FORMATETC;
}

STDMETHODIMP items_data_object::GetDataHere(FORMATETC* pformatetc, STGMEDIUM* pmedium)
{
	df::trace(__FUNCTION__);
	return E_NOTIMPL;
}

STDMETHODIMP items_data_object::QueryGetData(FORMATETC* pformatetc)
{
	df::trace(__FUNCTION__);

	const auto has_paths = !_files.empty() || !_folders.empty();

	if (pformatetc->cfFormat == CF_DIB && _has_image)
	{
		return S_OK;
	}

	if (pformatetc->cfFormat == CF_HDROP && has_paths)
	{
		return S_OK;
	}

	if (pformatetc->cfFormat == clipboard_formats::SHELLIDLIST && has_paths)
	{
		return S_OK;
	}

	return E_NOTIMPL;
}

STDMETHODIMP items_data_object::GetCanonicalFormatEtc(FORMATETC* pformatectIn, FORMATETC* pformatetcOut)
{
	df::trace(__FUNCTION__);
	return E_NOTIMPL;
}

STDMETHODIMP items_data_object::SetData(FORMATETC* pformatetc, STGMEDIUM* pmedium, BOOL fRelease)
{
	df::trace(__FUNCTION__);
	return E_NOTIMPL;
}

STDMETHODIMP items_data_object::EnumFormatEtc(const DWORD dwDirection, IEnumFORMATETC** ppenumFormatEtc)
{
	df::trace(__FUNCTION__);
	const auto has_paths = !_files.empty() || !_folders.empty();
	std::vector<FORMATETC> vfmtetc;

	if (ppenumFormatEtc == nullptr)
		return E_POINTER;

	vfmtetc.clear();

	if (_has_image || _loaded.success)
	{
		vfmtetc.emplace_back(clipboard_formats::Bitmap);
	}

	if (_has_preferred_drop && _is_move)
	{
		vfmtetc.emplace_back(clipboard_formats::PDE);
	}

	if (has_paths)
	{
		vfmtetc.emplace_back(clipboard_formats::Drop);
		vfmtetc.emplace_back(clipboard_formats::DropShellItems);
	}

#ifdef _DEBUG

	for (auto it = vfmtetc.cbegin(); it != vfmtetc.cend(); ++it)
	{
		wchar_t szBuf[MAX_PATH];

		if (GetClipboardFormatNameW(it->cfFormat, szBuf, MAX_PATH))
		{
			// remaining entries read from "fmt" members
			df::log(__FUNCTION__, std::format("EnumFormatEtc ", str::utf16_to_utf8(szBuf)));
		}
		else
		{
			df::log(__FUNCTION__, "EnumFormatEtc (Unknown)");
		}
	}

#endif //_DEBUG


	*ppenumFormatEtc = nullptr;
	switch (dwDirection)
	{
	case DATADIR_GET:
		*ppenumFormatEtc = new CEnumFORMATETCImpl(vfmtetc);
		break;

	case DATADIR_SET:
	default:
		return E_NOTIMPL;
	}

	return S_OK;
}

STDMETHODIMP items_data_object::DAdvise(FORMATETC* pformatetc, DWORD advf, IAdviseSink* pAdvSink, DWORD* pdwConnection)
{
	df::trace(__FUNCTION__);
	return E_NOTIMPL;
}

STDMETHODIMP items_data_object::DUnadvise(DWORD dwConnection)
{
	df::trace(__FUNCTION__);
	return E_NOTIMPL;
}

STDMETHODIMP items_data_object::EnumDAdvise(IEnumSTATDATA** ppenumAdvise)
{
	df::trace(__FUNCTION__);
	return E_NOTIMPL;
}

data_object_client::data_object_client(IDataObject* pData) : _pData(pData)
{
}

bool data_object_client::has_data(FORMATETC* pf) const
{
	return _pData && _pData->QueryGetData(pf) == S_OK;
}

bool data_object_client::has_drop_files() const
{
	return has_data(&clipboard_formats::Drop);
}

bool data_object_client::has_bitmap() const
{
	return has_data(&clipboard_formats::Bitmap);
}

DWORD data_object_client::preferred_drop_effect() const
{
	STGMEDIUM stgMedium;
	DWORD result = DROPEFFECT_COPY;

	if (SUCCEEDED(_pData->GetData(&clipboard_formats::PDE, &stgMedium)))
	{
		auto* const h = stgMedium.hGlobal;
		const auto* const p = static_cast<DWORD*>(GlobalLock(h));

		if (p)
		{
			result = *p;
			GlobalUnlock(h);
		}
	}

	return result;
}

static LCID g_lLangId = MAKELCID(LANG_NEUTRAL, SORT_DEFAULT);

static std::string format_os_error(const DWORD error)
{
	wchar_t sz[1000];
	FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, g_lLangId, sz, 1000,
	               nullptr);
	auto result = str::utf16_to_utf8(sz);
	return result.empty() ? std::string(tt.error_unknown) : result;
}

static std::string last_os_error_impl()
{
	std::string result;
	const auto error = GetLastError();

	if (error != 0)
	{
		result = format_os_error(error);
	}

	return result;
}

static std::string shell_error_string(const int e)
{
	switch (e)
	{
	case 0x71: return std::string(tt.error_same_file);
	case 0x72: return std::string(tt.error_many_src_1_dest);
	case 0x73: return std::string(tt.error_diff_dir);
	case 0x74: return std::string(tt.error_src_root_dir);
	case 0x75: return std::string(tt.error_op_cancelled);
	case 0x76: return std::string(tt.error_dest_subtree);
	case 0x78: return std::string(tt.error_access_denied_src);
	case 0x79: return std::string(tt.error_path_too_deep);
	case 0x7A: return std::string(tt.error_many_dest);
	case 0x7C: return std::string(tt.error_invalid_files);
	case 0x7D: return std::string(tt.error_dest_same_tree);
	case 0x7E: return std::string(tt.error_fld_dest_is_file);
	case 0x80: return std::string(tt.error_file_dest_is_fld);
	case 0x81: return std::string(tt.error_filename_too_long);
	case 0x82: return std::string(tt.error_dest_is_cd_rom);
	case 0x83: return std::string(tt.error_dest_is_dvd);
	case 0x84: return std::string(tt.error_dest_is_cd_record);
	case 0x85: return std::string(tt.error_file_too_large);
	case 0x86: return std::string(tt.error_src_is_cdrom);
	case 0x87: return std::string(tt.error_src_is_dvd);
	case 0x88: return std::string(tt.error_src_is_cd_record);
	case 0xB7: return std::string(tt.error_max);
	case 0x402: return std::string(tt.error_unknown);
	case 0x10000: return std::string(tt.error_on_dest);
	case 0x10074: return std::string(tt.error_dst_root_dir);
	default:
		return format_os_error(e);
	}
}

platform::file_op_result to_file_op_result(const int res, const BOOL fAnyOperationsAborted = 0)
{
	static constexpr int FO_CANCELLED = 0x04c7;

	platform::file_op_result result;
	result.code = res == 0 ? platform::file_op_result_code::OK : platform::file_op_result_code::FAILED;
	if (res == FO_CANCELLED || (res != 0 && fAnyOperationsAborted != 0))
		result.code =
			platform::file_op_result_code::CANCELLED;
	if (res != 0) result.error_message = shell_error_string(res);
	return result;
}

using name_mapping_t = std::unordered_map<df::file_path, df::file_path, df::ihash, df::ieq>;

static df::paths dest_file_list(const df::folder_path target, const wchar_t* const file_list,
                                const name_mapping_t& name_mapping)
{
	df::paths result;

	if (file_list)
	{
		const auto* sz = file_list;
		const auto* sz_start = sz;

		while (true)
		{
			if (*sz++ == 0)
			{
				auto src_path = df::file_path(std::wstring_view(sz_start, sz - sz_start - 1));
				auto src_id = target.combine_file(src_path.name());
				const auto found = name_mapping.find(src_id);
				result.files.emplace_back(found == name_mapping.end() ? src_id : found->second);
				sz_start = sz;

				if (*sz == 0) break;
			}
		}
	}

	return result;
}

static df::paths dest_file_list(const df::folder_path target, const char* const file_list,
                                const name_mapping_t& name_mapping)
{
	df::paths result;

	if (file_list)
	{
		const auto* sz = file_list;
		const auto* sz_start = sz;

		while (true)
		{
			if (*sz++ == 0)
			{
				auto src_path = df::file_path(std::string_view(sz_start, sz - sz_start - 1));
				auto src_id = target.combine_file(src_path.name());
				const auto found = name_mapping.find(src_id);
				result.files.emplace_back(found == name_mapping.end() ? src_id : found->second);
				sz_start = sz;

				if (*sz == 0) break;
			}
		}
	}

	return result;
}

struct HANDLETOMAPPINGSW
{
	UINT uNumberOfMappings; // Number of mappings in the array.
	LPSHNAMEMAPPINGW lpSHNameMapping; // Pointer to the array of mappings.
};

struct HANDLETOMAPPINGSA
{
	UINT uNumberOfMappings; // Number of mappings in the array.
	LPSHNAMEMAPPINGA lpSHNameMapping; // Pointer to the array of mappings.
};

static platform::file_op_result perform_hdrop2(HANDLE h, const df::folder_path target, bool is_move)
{
	auto* const pDrop = static_cast<DROPFILES*>(GlobalLock(h));
	platform::file_op_result result;

	if (pDrop)
	{
		const auto op_code = static_cast<UINT>(is_move ? FO_MOVE : FO_COPY);

		if (pDrop->fWide)
		{
			const auto targetW = platform::to_file_system_path(target);
			const auto* const file_list = std::bit_cast<LPCWSTR>(std::bit_cast<uint8_t*>(pDrop) + pDrop->pFiles);

			SHFILEOPSTRUCTW shfo = {
				app_wnd(),
				op_code,
				file_list,
				targetW.c_str(),
				FOF_RENAMEONCOLLISION | FOF_WANTMAPPINGHANDLE,
				0, nullptr, nullptr
			};

			result = to_file_op_result(SHFileOperationW(&shfo), shfo.fAnyOperationsAborted);

			name_mapping_t name_mapping;
			auto* const s = std::bit_cast<HANDLETOMAPPINGSW*>(shfo.hNameMappings);

			if (s)
			{
				for (auto i = 0u; i < s->uNumberOfMappings; i++)
				{
					const auto& nm = s->lpSHNameMapping[i];
					name_mapping[df::file_path(std::wstring_view{nm.pszOldPath, static_cast<size_t>(nm.cchOldPath)})] =
						df::file_path(std::wstring_view{nm.pszNewPath, static_cast<size_t>(nm.cchNewPath)});
				}
			}

			SHFreeNameMappings(shfo.hNameMappings);

			result.created_files = dest_file_list(target, file_list, name_mapping);
		}
		else
		{
			const auto targetA = utf8_cast2(target.text());
			const auto* const file_list = std::bit_cast<LPCSTR>(std::bit_cast<uint8_t*>(pDrop) + pDrop->pFiles);

			SHFILEOPSTRUCTA shfo = {
				app_wnd(),
				op_code,
				file_list,
				targetA.c_str(),
				FOF_RENAMEONCOLLISION | FOF_WANTMAPPINGHANDLE,
				0, nullptr, nullptr
			};

			result = to_file_op_result(SHFileOperationA(&shfo), shfo.fAnyOperationsAborted);

			name_mapping_t name_mapping;
			auto* const s = std::bit_cast<HANDLETOMAPPINGSW*>(shfo.hNameMappings);

			if (s)
			{
				for (auto i = 0u; i < s->uNumberOfMappings; i++)
				{
					const auto& nm = s->lpSHNameMapping[i];
					name_mapping[df::file_path(std::wstring_view{nm.pszOldPath, static_cast<size_t>(nm.cchOldPath)})] =
						df::file_path(std::wstring_view{nm.pszNewPath, static_cast<size_t>(nm.cchNewPath)});
				}
			}

			SHFreeNameMappings(shfo.hNameMappings);

			result.created_files = dest_file_list(target, str::utf8_cast2(file_list).c_str(), name_mapping);
		}

		GlobalUnlock(h);
	}

	return result;
}

platform::file_op_result data_object_client::drop_files(const df::folder_path target,
                                                        const platform::drop_effect effect)
{
	bool is_move = effect == platform::drop_effect::move;

	if (effect == platform::drop_effect::none)
	{
		is_move = preferred_drop_effect() == DROPEFFECT_MOVE;
	}

	STGMEDIUM stgMedium;
	platform::file_op_result result;

	if (SUCCEEDED(_pData->GetData(&clipboard_formats::Drop, &stgMedium)))
	{
		auto* const h = stgMedium.hGlobal;
		result = perform_hdrop2(h, target, is_move);
		ReleaseStgMedium(&stgMedium);
	}

	return result;
}

df::file_path data_object_client::first_path() const
{
	df::file_path result;
	STGMEDIUM stgMedium;

	if (SUCCEEDED(_pData->GetData(&clipboard_formats::Drop, &stgMedium)))
	{
		auto* const h = stgMedium.hGlobal;
		auto* const pDrop = static_cast<DROPFILES*>(GlobalLock(h));

		if (pDrop)
		{
			if (pDrop->fWide)
			{
				result = df::file_path(std::bit_cast<LPCWSTR>(std::bit_cast<uint8_t*>(pDrop) + pDrop->pFiles));
			}
			else
			{
				result = df::file_path(std::bit_cast<const char*>(std::bit_cast<uint8_t*>(pDrop) + pDrop->pFiles));
			}

			GlobalUnlock(h);
		}

		ReleaseStgMedium(&stgMedium);
	}

	return result;
}


static platform::drop_effect to_drop_effect(const DWORD dwEffect)
{
	if (dwEffect == DROPEFFECT_COPY)
	{
		return platform::drop_effect::copy;
	}
	if (dwEffect == DROPEFFECT_MOVE)
	{
		return platform::drop_effect::move;
	}
	if (dwEffect == DROPEFFECT_LINK)
	{
		return platform::drop_effect::link;
	}
	return platform::drop_effect::none;
}

data_object_client::description data_object_client::files_description() const
{
	description result;
	STGMEDIUM stgMedium;

	if (SUCCEEDED(_pData->GetData(&clipboard_formats::Drop, &stgMedium)))
	{
		auto* const h = stgMedium.hGlobal;
		auto* const pDrop = static_cast<DROPFILES*>(GlobalLock(h));

		if (pDrop)
		{
			result.count = 1;

			if (pDrop->fWide)
			{
				const auto* sz = std::bit_cast<LPCWSTR>(std::bit_cast<const char*>(pDrop) + pDrop->pFiles);
				result.first_name = str::utf16_to_utf8(sz);
				result.has_readonly |= (file_attributes(df::folder_path(sz)) & FILE_ATTRIBUTE_READONLY) != 0;

				while (sz[0] != 0 || sz[1] != 0)
				{
					if (*sz++ == 0)
					{
						++result.count;
					}
				}
			}
			else
			{
				const auto* sz = std::bit_cast<LPCSTR>(std::bit_cast<uint8_t*>(pDrop) + pDrop->pFiles);
				result.first_name = str::utf8_cast(sz);
				result.has_readonly |= (file_attributes(df::folder_path(std::bit_cast<const char*>(sz))) &
					FILE_ATTRIBUTE_READONLY) != 0;

				while (sz[0] != 0 || sz[1] != 0)
				{
					if (*sz++ == 0)
					{
						++result.count;
					}
				}
			}

			GlobalUnlock(h);
		}

		ReleaseStgMedium(&stgMedium);
	}

	result.preferred_drop_effect = to_drop_effect(preferred_drop_effect());
	return result;
}


platform::file_op_result data_object_client::save_bitmap(const df::folder_path save_path, const std::string_view name,
                                                         const bool as_png)
{
	platform::file_op_result result;
	STGMEDIUM stgMedium;

	const HRESULT hr = _pData->GetData(&clipboard_formats::Bitmap, &stgMedium);

	if (SUCCEEDED(hr))
	{
		result = save_bitmap_info(save_path, name, as_png, stgMedium.hBitmap);

		// Don't unlock bitmap handle - ReleaseStgMedium will handle cleanup
		ReleaseStgMedium(&stgMedium);
	}

	return result;
}


void* platform::memory_pool::alloc(size_t size)
{
	static std::bad_alloc OOM;
	exclusive_lock lock(cs);

	const auto align_size = size = (size + (alignment - 1)) / alignment * alignment; // Align size

	if (align_size > block_size) throw OOM;

	if (next_free + align_size > block_limit)
	{
		next_free = std::bit_cast<uint8_t*>(VirtualAlloc(nullptr, block_size, MEM_COMMIT, PAGE_READWRITE));
		if (!next_free) throw OOM;
		block_limit = next_free + block_size;
		static_memory_usage += block_size;
	}

	auto* const result = next_free;
	next_free += align_size;
	return result;
}

df::file_path platform::resolve_link(const df::file_path path)
{
	ComPtr<IShellLink> psl;
	df::file_path result;

	if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(psl.GetAddressOf()))))
	{
		ComPtr<IPersistFile> ppf;

		if (SUCCEEDED(psl.As(&ppf)))
		{
			const auto link = to_file_system_path(path);

			if (ppf && SUCCEEDED(ppf->Load(link.c_str(), STGM_WRITE)))
			{
				wchar_t result_path[MAX_PATH];
				const auto success = SUCCEEDED(psl->GetPath(result_path, MAX_PATH, nullptr, 0));
				if (success) result = df::file_path(result_path);
			}
		}
	}

	return result;
}

bool platform::sse2_supported = IsProcessorFeaturePresent(PF_XMMI64_INSTRUCTIONS_AVAILABLE) != 0;
bool platform::crc32_supported = IsProcessorFeaturePresent(PF_SSE4_2_INSTRUCTIONS_AVAILABLE) != 0;
bool platform::avx2_supported = IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE) != 0;
bool platform::avx512_supported = IsProcessorFeaturePresent(PF_AVX512F_INSTRUCTIONS_AVAILABLE) != 0;
bool platform::neon_supported = IsProcessorFeaturePresent(PF_ARM_NEON_INSTRUCTIONS_AVAILABLE) != 0;

#include <bcrypt.h>
#pragma comment(lib, "bcrypt")

void platform::secure_zero(void* ptr, const size_t len)
{
	SecureZeroMemory(ptr, len);
}

void platform::generate_random_bytes(uint8_t* buffer, const size_t len)
{
	BCryptGenRandom(nullptr, buffer, static_cast<ULONG>(len), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
}


std::string platform::OS()
{
#pragma warning(push)
#pragma warning(disable:4996)
	OSVERSIONINFO osvi = {};
	osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	GetVersionEx(&osvi);
#pragma warning(pop)

	// make up major for windows 11
	if (osvi.dwMajorVersion == 10 && osvi.dwBuildNumber >= 22000)
	{
		osvi.dwMajorVersion = 11;
	}

	char result[64];
	sprintf_s(result, "%d.%d", osvi.dwMajorVersion, osvi.dwMinorVersion);
	return str::utf8_cast2(result);
}

void platform::trace(const std::string_view message)
{
	trace(std::string(message));
}

void platform::trace(const std::string& message)
{
	OutputDebugStringA(std::bit_cast<const char*>(message.c_str()));
}

void platform::set_thread_description(const std::string_view name)
{
	if (IsWindows10OrGreater())
	{
		using pfnSetThreadDescription = HRESULT(WINAPI*)(HANDLE, PCWSTR);

		if (!str::is_empty(name))
		{
			static pfnSetThreadDescription set_thread_description_proc = nullptr;
			static HMODULE kernel32 = nullptr;

			if (!kernel32)
			{
				kernel32 = LoadLibraryW(L"kernel32.dll");
			}

			if (kernel32 && set_thread_description_proc == nullptr)
			{
				set_thread_description_proc = std::bit_cast<pfnSetThreadDescription>(
					GetProcAddress(kernel32, "SetThreadDescription"));
			}

			if (set_thread_description_proc != nullptr)
			{
				set_thread_description_proc(GetCurrentThread(), str::utf8_to_utf16(name).c_str());
			}
		}
	}
}


std::string platform::user_name()
{
	constexpr int size = 200;
	wchar_t w[size];
	DWORD len = size;
	GetUserName(w, &len);

	return str::utf16_to_utf8(w);
}

std::string platform::last_os_error()
{
	return last_os_error_impl();
}

static platform::file_op_result last_op_result(const BOOL res)
{
	platform::file_op_result result;

	if (res == 0)
	{
		result.code = platform::file_op_result_code::FAILED;
		result.error_message = last_os_error_impl();
	}
	else
	{
		result.code = platform::file_op_result_code::OK;
	}

	return result;
}

platform::file_op_result platform::delete_file(const df::file_path path)
{
	const auto w = to_file_system_path(path);
	return last_op_result(::DeleteFile(w.c_str()));
}


//bool Platform::FileAttributes(const Core::file_path& path, WIN32_FILE_ATTRIBUTE_DATA& fi)
//{
//    auto w = path.ToFileSystemPath();
//    memset(&fi, 0, sizeof(fi));
//    return GetFileAttributesEx(w.c_str(), GetFileExInfoStandard, &fi) != 0;
//}

platform::file_op_result platform::copy_file(const df::file_path existing, const df::file_path destination,
                                             const bool fail_if_exists, const bool can_create_folder)
{
	if (can_create_folder && !destination.folder().exists())
	{
		const auto cf_res = create_folder(destination.folder());

		if (!cf_res.success())
		{
			return cf_res;
		}
	}

	const auto existingW = to_file_system_path(existing);
	const auto destinationW = to_file_system_path(destination);
	return last_op_result(::CopyFile(existingW.c_str(), destinationW.c_str(), fail_if_exists));
}

platform::file_op_result platform::move_file(const df::file_path existing, const df::file_path destination,
                                             const bool fail_if_exists)
{
	const auto existingW = to_file_system_path(existing);
	const auto destinationW = to_file_system_path(destination);
	return last_op_result(::MoveFileEx(existingW.c_str(), destinationW.c_str(),
	                                   MOVEFILE_COPY_ALLOWED | (fail_if_exists ? 0 : MOVEFILE_REPLACE_EXISTING)));
}

platform::file_op_result platform::move_file(const df::folder_path existing, const df::folder_path destination)
{
	const auto existingW = to_file_system_path(existing);
	const auto destinationW = to_file_system_path(destination);
	return last_op_result(::MoveFileEx(existingW.c_str(),
	                                   destinationW.c_str(),
	                                   MOVEFILE_COPY_ALLOWED));
}

platform::file_op_result platform::replace_file(const df::file_path destination, const df::file_path existing,
                                                const bool create_originals)
{
	df::file_path backup;

	if (destination.exists())
	{
		if (create_originals)
		{
			// Create original
			const auto org_name = std::string(destination.file_name_without_extension()) + ".original"s;
			const auto original_path = df::file_path(existing.folder(), org_name, destination.extension());

			if (!original_path.exists())
			{
				backup = original_path;
				//platform::move_file(path_src, original_path, true);
			}
		}

		const auto existingW = to_file_system_path(existing);
		const auto destinationW = to_file_system_path(destination);
		const auto backupW = to_file_system_path(backup);

		// Try ReplaceFileW first - this is the preferred atomic operation
		const auto replace_success = ReplaceFileW(
			destinationW.c_str(),
			existingW.c_str(),
			backup.is_empty() ? nullptr : backupW.c_str(),
			REPLACEFILE_IGNORE_MERGE_ERRORS | REPLACEFILE_IGNORE_ACL_ERRORS,
			nullptr,
			nullptr);

		if (replace_success)
		{
			return last_op_result(TRUE);
		}

		// ReplaceFileW failed - this commonly happens on network drives (SMB shares)
		// Fall back to move with overwrite (MOVEFILE_REPLACE_EXISTING)
		const auto last_error = GetLastError();
		df::log(__FUNCTION__, std::format("ReplaceFileW failed with error {}, falling back to move with overwrite",
		                                  static_cast<uint32_t>(last_error)));

		// If backup was requested, try to create it first by copying destination
		if (!backup.is_empty())
		{
			// Use copy instead of move so we don't lose the destination if the final move fails
			const auto backup_result = copy_file(destination, backup, true, false);
			if (backup_result.failed())
			{
				df::log(__FUNCTION__, "Failed to create backup, proceeding without backup");
			}
		}

		// Move existing to destination, overwriting if it exists
		return move_file(existing, destination, false);
	}

	return move_file(existing, destination, true);
}

bool platform::exists(const df::folder_path path)
{
	const auto attrib = ::file_attributes(path);

	return attrib != INVALID_FILE_ATTRIBUTES &&
		attrib & FILE_ATTRIBUTE_DIRECTORY;
}

bool platform::exists(const df::file_path path)
{
	const auto attrib = ::file_attributes(path);

	return attrib != INVALID_FILE_ATTRIBUTES &&
		(attrib & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

platform::file_op_result platform::create_folder(const df::folder_path path)
{
	file_op_result result;

	if (path.exists())
	{
		result.code = file_op_result_code::OK;
		return result;
	}

	const auto parent = path.parent();

	if (!parent.is_root() && !parent.exists())
	{
		auto parent_result = create_folder(parent);

		if (parent_result.failed())
		{
			return parent_result;
		}
	}

	const auto w = to_file_system_path(path);
	const auto res = SHCreateDirectoryExW(app_wnd(), w.c_str(), nullptr);

	if (res == 0)
	{
		SHChangeNotify(SHCNE_MKDIR, SHCNF_PATH, w.c_str(), nullptr);
		result.code = file_op_result_code::OK;
	}
	else
	{
		result.code = file_op_result_code::FAILED;
		result.error_message = format_os_error(res);
	}

	return result;
}

bool platform::open(const df::file_path path)
{
	const auto w = to_file_system_path(path);
#ifdef WINSTORE
	// Use WinRT Launcher for Store apps - more reliable in sandbox
	try
	{
		ComPtr<ABI::Windows::System::ILauncherStatics> launcher;
		HRESULT hr = RoGetActivationFactory(
			Microsoft::WRL::Wrappers::HStringReference(RuntimeClass_Windows_System_Launcher).Get(),
			IID_PPV_ARGS(&launcher));

		if (SUCCEEDED(hr))
		{
			// Get StorageFile from path
			ComPtr<ABI::Windows::Storage::IStorageFileStatics> file_statics;
			hr = RoGetActivationFactory(
				Microsoft::WRL::Wrappers::HStringReference(RuntimeClass_Windows_Storage_StorageFile).Get(),
				IID_PPV_ARGS(&file_statics));

			if (SUCCEEDED(hr))
			{
				HSTRING path_hstring = nullptr;
				WindowsCreateString(w.c_str(), static_cast<UINT32>(w.size()), &path_hstring);

				ComPtr<ABI::Windows::Foundation::IAsyncOperation<ABI::Windows::Storage::StorageFile*>> file_op;
				hr = file_statics->GetFileFromPathAsync(path_hstring, &file_op);
				WindowsDeleteString(path_hstring);

				if (SUCCEEDED(hr))
				{
					ComPtr<ABI::Windows::Storage::IStorageFile> storage_file;
					// Wait for async operation (simplified - in production consider proper async handling)
					ABI::Windows::Foundation::AsyncStatus status;
					ComPtr<ABI::Windows::Foundation::IAsyncInfo> async_info;
					if (SUCCEEDED(file_op.As(&async_info)))
					{
						for (int i = 0; i < 100; ++i)
						{
							async_info->get_Status(&status);
							if (status != ABI::Windows::Foundation::AsyncStatus::Started)
								break;
							Sleep(10);
						}

						if (status == ABI::Windows::Foundation::AsyncStatus::Completed)
						{
							hr = file_op->GetResults(&storage_file);
							if (SUCCEEDED(hr) && storage_file)
							{
								ComPtr<ABI::Windows::Foundation::IAsyncOperation<bool>> launch_op;
								hr = launcher->LaunchFileAsync(storage_file.Get(), &launch_op);
								return SUCCEEDED(hr);
							}
						}
					}
				}
			}
		}
	}
	catch (...)
	{
		df::log(__FUNCTION__, "WinRT Launcher failed, falling back to ShellExecute");
	}
	// Fallback to ShellExecute
#endif
	return ShellExecute(app_wnd(), L"open", w.c_str(), L"", L"", SW_SHOWNORMAL) > std::bit_cast<HINSTANCE>(
		static_cast<uintptr_t>(32));
}

bool platform::open(const std::string_view path)
{
	const auto w = str::utf8_to_utf16(path);
#ifdef WINSTORE
	// Use WinRT Launcher for Store apps - required for URLs and more reliable overall
	try
	{
		ComPtr<ABI::Windows::System::ILauncherStatics> launcher;
		HRESULT hr = RoGetActivationFactory(
			Microsoft::WRL::Wrappers::HStringReference(RuntimeClass_Windows_System_Launcher).Get(),
			IID_PPV_ARGS(&launcher));

		if (SUCCEEDED(hr))
		{
			// Create URI from path
			ComPtr<ABI::Windows::Foundation::IUriRuntimeClassFactory> uri_factory;
			hr = RoGetActivationFactory(
				Microsoft::WRL::Wrappers::HStringReference(RuntimeClass_Windows_Foundation_Uri).Get(),
				IID_PPV_ARGS(&uri_factory));

			if (SUCCEEDED(hr))
			{
				HSTRING uri_hstring = nullptr;
				WindowsCreateString(w.c_str(), static_cast<UINT32>(w.size()), &uri_hstring);

				ComPtr<ABI::Windows::Foundation::IUriRuntimeClass> uri;
				hr = uri_factory->CreateUri(uri_hstring, &uri);
				WindowsDeleteString(uri_hstring);

				if (SUCCEEDED(hr) && uri)
				{
					ComPtr<ABI::Windows::Foundation::IAsyncOperation<bool>> launch_op;
					hr = launcher->LaunchUriAsync(uri.Get(), &launch_op);
					return SUCCEEDED(hr);
				}
			}
		}
	}
	catch (...)
	{
		df::log(__FUNCTION__, "WinRT Launcher failed, falling back to ShellExecute");
	}
	// Fallback to ShellExecute
#endif
	return ShellExecute(app_wnd(), L"open", w.c_str(), L"", L"", SW_SHOWNORMAL) > std::bit_cast<HINSTANCE>(
		static_cast<uintptr_t>(32));
}

static bool run_command_line(const std::wstring& command_line)
{
	PROCESS_INFORMATION pi;
	STARTUPINFO si = {0};
	si.cb = sizeof(si);
	si.wShowWindow = SW_SHOWNORMAL;

	if (CreateProcess(nullptr, const_cast<LPWSTR>(command_line.c_str()), nullptr, nullptr, FALSE,
	                  CREATE_DEFAULT_ERROR_MODE, nullptr, nullptr, &si, &pi))
	{
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		return true;
	}

	return false;
}

bool platform::run(const std::string_view cmd)
{
	return run_command_line(str::utf8_to_utf16(cmd));
}


static bool run_explorer(const std::wstring& path)
{
	const auto command_line = L"explorer.exe /select,\""s + path + L"\""s;
	return run_command_line(command_line);
}

struct pidandhwnd
{
	DWORD dwProcessId;
	HWND hwnd;
};

static BOOL CALLBACK EnumWindowsProc(const HWND hwnd, const LPARAM lParam)
{
	auto* ppnh = (pidandhwnd*)lParam;
	DWORD dwProcessId;
	GetWindowThreadProcessId(hwnd, &dwProcessId);
	if (ppnh->dwProcessId == dwProcessId)
	{
		ppnh->hwnd = hwnd;
		return FALSE;
	}
	return TRUE;
}

// Could use a simple notepad like this -> https://github.com/estout82/Notepad/blob/master/Src/Main.cpp

void platform::show_text_in_notepad(const std::string_view s)
{
	TCHAR szCmdline[] = TEXT("Notepad.exe");

	PROCESS_INFORMATION piProcInfo = {};
	STARTUPINFO siStartInfo = {};
	siStartInfo.cb = sizeof(STARTUPINFO);
	siStartInfo.hStdError = nullptr;
	siStartInfo.hStdOutput = nullptr;
	siStartInfo.hStdInput = nullptr;

	const auto started = CreateProcess(nullptr,
	                                   szCmdline, // command line 
	                                   nullptr, // process security attributes 
	                                   nullptr, // primary thread security attributes 
	                                   TRUE, // handles are inherited 
	                                   0, // creation flags 
	                                   nullptr, // use parent's environment 
	                                   nullptr, // use parent's current directory 
	                                   &siStartInfo, // STARTUPINFO pointer 
	                                   &piProcInfo); // receives PROCESS_INFORMATION 

	if (started)
	{
		//df::log(__FUNCTION__, piProcInfo.dwProcessId << " Notepad Process Id";

		WaitForInputIdle(piProcInfo.hProcess, 1000);

		pidandhwnd pnh;
		pnh.dwProcessId = piProcInfo.dwProcessId;
		pnh.hwnd = nullptr;

		EnumDesktopWindows(nullptr, EnumWindowsProc, (LPARAM)&pnh);

		if (pnh.hwnd != nullptr)
		{
			constexpr int ControlId = 15; // Edit control in Notepad
			auto* hEditWnd = GetDlgItem(pnh.hwnd, ControlId);

			if (!hEditWnd)
			{
				hEditWnd = FindWindowEx(pnh.hwnd, nullptr, L"scintilla", nullptr);

				if (!hEditWnd)
				{
					SendMessage(hEditWnd, WM_SETTEXT, NULL, (LPARAM)std::string(s).c_str());
				}
			}
			else
			{
				const auto w = str::utf8_to_utf16(s);
				SendMessage(hEditWnd, WM_SETTEXT, NULL, (LPARAM)w.c_str());
			}
		}
	}
}

void platform::show_in_file_browser(const df::file_path path)
{
#ifdef WINSTORE
	// For Store apps, use WinRT Launcher to open the containing folder
	// We open the parent folder since we can't do "select" like explorer.exe
	try
	{
		ComPtr<ABI::Windows::System::ILauncherStatics> launcher;
		HRESULT hr = RoGetActivationFactory(
			Microsoft::WRL::Wrappers::HStringReference(RuntimeClass_Windows_System_Launcher).Get(),
			IID_PPV_ARGS(&launcher));

		if (SUCCEEDED(hr))
		{
			// Get the folder path
			const auto folder_path = path.folder();
			const auto w = to_file_system_path(folder_path);

			ComPtr<ABI::Windows::Storage::IStorageFolderStatics> folder_statics;
			hr = RoGetActivationFactory(
				Microsoft::WRL::Wrappers::HStringReference(RuntimeClass_Windows_Storage_StorageFolder).Get(),
				IID_PPV_ARGS(&folder_statics));

			if (SUCCEEDED(hr))
			{
				HSTRING path_hstring = nullptr;
				WindowsCreateString(w.c_str(), static_cast<UINT32>(w.size()), &path_hstring);

				ComPtr<ABI::Windows::Foundation::IAsyncOperation<ABI::Windows::Storage::StorageFolder*>> folder_op;
				hr = folder_statics->GetFolderFromPathAsync(path_hstring, &folder_op);
				WindowsDeleteString(path_hstring);

				if (SUCCEEDED(hr))
				{
					// Wait for async operation
					ComPtr<ABI::Windows::Foundation::IAsyncInfo> async_info;
					if (SUCCEEDED(folder_op.As(&async_info)))
					{
						ABI::Windows::Foundation::AsyncStatus status;
						for (int i = 0; i < 100; ++i)
						{
							async_info->get_Status(&status);
							if (status != ABI::Windows::Foundation::AsyncStatus::Started)
								break;
							Sleep(10);
						}

						if (status == ABI::Windows::Foundation::AsyncStatus::Completed)
						{
							ComPtr<ABI::Windows::Storage::IStorageFolder> storage_folder;
							hr = folder_op->GetResults(&storage_folder);

							if (SUCCEEDED(hr) && storage_folder)
							{
								// Use LaunchFolderAsync from ILauncherStatics3
								ComPtr<ABI::Windows::System::ILauncherStatics3> launcher3;
								if (SUCCEEDED(launcher.As(&launcher3)))
								{
									ComPtr<ABI::Windows::Foundation::IAsyncOperation<bool>> launch_op;
									launcher3->LaunchFolderAsync(storage_folder.Get(), &launch_op);
									return;
								}
							}
						}
					}
				}
			}
		}
	}
	catch (...)
	{
		df::log(__FUNCTION__, "WinRT Launcher failed");
	}
	// Fallback - shouldn't normally reach here for Store builds
#endif
	run_explorer(to_file_system_path(path));
}

void platform::show_in_file_browser(const df::folder_path path)
{
#ifdef WINSTORE
	// For Store apps, use WinRT Launcher to open the folder
	try
	{
		ComPtr<ABI::Windows::System::ILauncherStatics> launcher;
		HRESULT hr = RoGetActivationFactory(
			Microsoft::WRL::Wrappers::HStringReference(RuntimeClass_Windows_System_Launcher).Get(),
			IID_PPV_ARGS(&launcher));

		if (SUCCEEDED(hr))
		{
			const auto w = to_file_system_path(path);

			ComPtr<ABI::Windows::Storage::IStorageFolderStatics> folder_statics;
			hr = RoGetActivationFactory(
				Microsoft::WRL::Wrappers::HStringReference(RuntimeClass_Windows_Storage_StorageFolder).Get(),
				IID_PPV_ARGS(&folder_statics));

			if (SUCCEEDED(hr))
			{
				HSTRING path_hstring = nullptr;
				WindowsCreateString(w.c_str(), static_cast<UINT32>(w.size()), &path_hstring);

				ComPtr<ABI::Windows::Foundation::IAsyncOperation<ABI::Windows::Storage::StorageFolder*>> folder_op;
				hr = folder_statics->GetFolderFromPathAsync(path_hstring, &folder_op);
				WindowsDeleteString(path_hstring);

				if (SUCCEEDED(hr))
				{
					// Wait for async operation
					ComPtr<ABI::Windows::Foundation::IAsyncInfo> async_info;
					if (SUCCEEDED(folder_op.As(&async_info)))
					{
						ABI::Windows::Foundation::AsyncStatus status;
						for (int i = 0; i < 100; ++i)
						{
							async_info->get_Status(&status);
							if (status != ABI::Windows::Foundation::AsyncStatus::Started)
								break;
							Sleep(10);
						}

						if (status == ABI::Windows::Foundation::AsyncStatus::Completed)
						{
							ComPtr<ABI::Windows::Storage::IStorageFolder> storage_folder;
							hr = folder_op->GetResults(&storage_folder);

							if (SUCCEEDED(hr) && storage_folder)
							{
								// Use LaunchFolderAsync from ILauncherStatics2
								ComPtr<ABI::Windows::System::ILauncherStatics3> launcher3;
								if (SUCCEEDED(launcher.As(&launcher3)))
								{
									ComPtr<ABI::Windows::Foundation::IAsyncOperation<bool>> launch_op;
									launcher3->LaunchFolderAsync(storage_folder.Get(), &launch_op);
									return;
								}
							}
						}
					}
				}
			}
		}
	}
	catch (...)
	{
		df::log(__FUNCTION__, "WinRT Launcher failed");
	}
	// Fallback - shouldn't normally reach here for Store builds
#endif
	run_explorer(to_file_system_path(path));
}

int platform::display_frequency()
{
	DEVMODE dm = {};
	dm.dmSize = sizeof(DEVMODE);
	dm.dmDriverExtra = 0;

	if (EnumDisplaySettings(nullptr, ENUM_CURRENT_SETTINGS, &dm))
	{
		if (dm.dmDisplayFrequency > 0)
		{
			return dm.dmDisplayFrequency;
		}
	}

	return 30; // Guess
}

bool platform::working_set(int64_t& current, int64_t& peak)
{
	PROCESS_MEMORY_COUNTERS mem;

	if (GetProcessMemoryInfo(GetCurrentProcess(), &mem, sizeof(mem)))
	{
		current = mem.WorkingSetSize;
		peak = mem.PeakWorkingSetSize;
		return true;
	}

	return false;
}

df::folder_path platform::temp_folder()
{
	wchar_t path[MAX_PATH + 1];
	::GetTempPath(MAX_PATH, path);
	return df::folder_path(path);
}

static int CALLBACK browse_callback_proc(const HWND hwnd, const uint32_t uMsg, LPARAM, const LPARAM pData)
{
	switch (uMsg)
	{
	case BFFM_INITIALIZED:
		// WParam is TRUE since you are passing a path.
		SendMessage(hwnd, BFFM_SETSELECTION, TRUE, pData);
		return 1;

	default:
		break;
	}
	return 0;
}

bool platform::browse_for_folder(df::folder_path& path)
{
	const auto title = str::utf8_to_utf16(tt.select_folder);

	wchar_t path_result[MAX_PATH];
	wchar_t path_root[MAX_PATH];
	wcscpy_s(path_root, to_file_system_path(path).c_str());

	BROWSEINFO bi;
	bi.hwndOwner = GetActiveWindow();
	bi.pidlRoot = nullptr;
	bi.pszDisplayName = path_result;
	bi.lpszTitle = title.c_str();
	bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
	// Needed to set default folder
	// Use reinterpret_cast over bit_cast
	bi.lpfn = browse_callback_proc;
	bi.lParam = reinterpret_cast<LPARAM>(static_cast<wchar_t*>(path_root));
	bi.iImage = 0;

	auto* const pidl_result = SHBrowseForFolder(&bi);

	if (pidl_result)
	{
		wchar_t sz[MAX_PATH];
		SHGetPathFromIDListW(pidl_result, sz);
		CoTaskMemFree(pidl_result);
		path = df::folder_path(str::utf16_to_utf8(sz));
	}

	return pidl_result != nullptr;
}


bool platform::prompt_for_save_path(df::file_path& path)
{
	OPENFILENAME ofn = {};

	wchar_t w[MAX_PATH];
	w[0] = 0;
	wcscpy_s(w, to_file_system_path(path).c_str());
	const auto extension = str::utf8_to_utf16(path.extension());

	std::string filter_a;
	filter_a += std::format("{} (*.jpg)|*.jpg;*.jpe;*.jpeg|", tt.jpeg_best);
	filter_a += std::format("{} (*.png)|*.png|", tt.png_best);
	filter_a += std::format("{} (*.webp)|*.webp|", tt.webp_best);
	filter_a += "|";

	auto filter = str::utf8_to_utf16(filter_a);

	for (auto&& c : filter)
	{
		if (c == '|') c = 0;
	}

	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = app_wnd();
	ofn.lpstrFilter = filter.c_str();
	ofn.lpstrFile = w;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT;
	ofn.lpstrDefExt = extension.c_str();
	ofn.nFilterIndex = 1;
	if (str::icmp(extension, L".png") == 0) ofn.nFilterIndex = 2;
	if (str::icmp(extension, L".webp") == 0) ofn.nFilterIndex = 3;

	const auto success = GetSaveFileName(&ofn) != 0;
	path = df::file_path(w);
	return success;
}


//#define STRSAFE_NO_DEPRECATE
//#include <strsafe.h>

// autocrop
// https://github.com/rajbot/autocrop

// Scanning example 
// https://microsoft.visualstudio.com/OS/_git/os?path=%2Fprintscan%2Fui%2FDocCenter%2FScan%2Fmainfrmscan.cpp&version=GBofficial%2Frsmaster&_a=contents


platform::scan_result platform::scan(const df::folder_path save_path)
{
	scan_result result;

	LONG num_files = 0;
	BSTR* file_paths = nullptr;
	ComPtr<IWiaItem2> pItem;

	// Create the device manager
	ComPtr<IWiaDevMgr2> pWiaDevMgr2;
	auto hr = CoCreateInstance(CLSID_WiaDevMgr2, nullptr, CLSCTX_LOCAL_SERVER,
	                           IID_PPV_ARGS(pWiaDevMgr2.GetAddressOf()));

	if (SUCCEEDED(hr))
	{
		//create a scan dialog and a select device UI
		const bstr_t folder(to_file_system_path(save_path).c_str());
		const bstr_t name(L"scan");

		hr = pWiaDevMgr2->GetImageDlg(0, nullptr, app_wnd(), folder, name, &num_files, &file_paths, &pItem);

		if (SUCCEEDED(hr) && num_files > 0 && file_paths)
		{
			result.saved_file_path = df::file_path(file_paths[0]);
			result.success = true;
		}

		if (SUCCEEDED(hr) && num_files && file_paths)
		{
			for (auto i = 0; i < num_files; i++)
			{
				SysFreeString(file_paths[i]);
			}

			CoTaskMemFree(file_paths);
		}
	}

	if (FAILED(hr))
	{
		if (WIA_S_NO_DEVICE_AVAILABLE == hr)
		{
			result.error_message = tt.error_connect_scanner;
		}
		else
		{
			result.error_message = tt.error_scanner;
		}
	}

	return result;
}

static bool invoke_assoc(const ComPtr<IAssocHandler>& handler, const std::vector<df::file_path>& files,
                         const std::vector<df::folder_path>& folders)
{
	bool success = true;
	const ComPtr<items_data_object> data = new items_data_object();
	data->cache(files, folders);

	ComPtr<IAssocHandlerInvoker> invoker;

	if (SUCCEEDED(handler->CreateInvoker(data.Get(), &invoker)))
	{
		if (FAILED(invoker->Invoke()))
		{
			if (FAILED(handler->Invoke(data.Get())))
			{
				success = false;
			}
		}
	}

	return success;
}

std::vector<platform::open_with_entry> platform::assoc_handlers(const std::string_view ext)
{
	ComPtr<IEnumAssocHandlers> handle_enum;
	df::hash_map<std::string, open_with_entry, df::ihash, df::ieq> handlers;
	const auto w = str::utf8_to_utf16(ext);

	if (SUCCEEDED(SHAssocEnumHandlers(w.c_str(), ASSOC_FILTER_RECOMMENDED, &handle_enum)))
	{
		ComPtr<IAssocHandler> handler;
		ULONG received = 0;

		while (S_OK == handle_enum->Next(1, &handler, &received))
		{
			if (received == 0) break;
			LPWSTR name = nullptr;

			if (SUCCEEDED(handler->GetUIName(&name)))
			{
				auto&& h = handlers[str::utf16_to_utf8(name)];
				h.invoke = [handler](const std::vector<df::file_path>& files,
				                     const std::vector<df::folder_path>& folders)
				{
					return invoke_assoc(handler, files, folders);
				};
				h.weight += 1;
			}

			handler = nullptr;
		}
	}

	std::vector<open_with_entry> result;

	for (const auto& h : handlers)
	{
		open_with_entry e;
		e.name = h.first;
		e.invoke = h.second.invoke;
		e.weight = h.second.weight;
		result.emplace_back(e);
	}

	return result;
}

df::blob platform::load_resource(const resource_item i)
{
	switch (i)
	{
	case resource_item::logo:
		return ::load_resource(IDB_LOGO, L"PNG");
	case resource_item::logo30:
		return ::load_resource(IDB_LOGO30, L"PNG");
	case resource_item::logo15:
		return ::load_resource(IDB_LOGO15, L"PNG");
	case resource_item::title:
		return ::load_resource(IDB_TITLE, L"PNG");
	case resource_item::map_png:
		return ::load_resource(IDB_MAP, L"PNG");
	case resource_item::sql:
		return ::load_resource(IDR_CREATE_SQL, L"SQL");
	default: ;
	}

	return {};
}

#ifndef WINSTORE

static std::wstring read_cert_name(const std::wstring& path)
{
	std::wstring result;

	HCERTSTORE store = nullptr;
	HCRYPTMSG msg = nullptr;

	if (CryptQueryObject(CERT_QUERY_OBJECT_FILE, path.c_str(), CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
	                     CERT_QUERY_FORMAT_FLAG_BINARY, 0, nullptr, nullptr, nullptr, &store, &msg, nullptr))
	{
		DWORD infoSize = 0;
		if (CryptMsgGetParam(msg, CMSG_SIGNER_CERT_INFO_PARAM, 0, nullptr, &infoSize))
		{
			const auto info = df::unique_alloc<CERT_INFO>(infoSize);

			if (info)
			{
				if (CryptMsgGetParam(msg, CMSG_SIGNER_CERT_INFO_PARAM, 0, info.get(), &infoSize))
				{
					const auto* const cert_context = CertFindCertificateInStore(store, X509_ASN_ENCODING, 0,
						CERT_FIND_SUBJECT_CERT, info.get(), nullptr);

					if (cert_context)
					{
						wchar_t sz[1024];
						CertGetNameString(cert_context, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, sz, 1024);
						result = sz;

						CertFreeCertificateContext(cert_context);
					}
				}
			}
		}

		if (store) CertCloseStore(store, 0);
		if (msg) CryptMsgClose(msg);
	}

	return result;
}

// CRYPT_E_SECURITY_SETTINGS

static bool verify_package(const df::file_path path_in)
{
	const auto path = platform::to_file_system_path(path_in);
	const auto cert_name = read_cert_name(path);

	// Pin updates to the author. Use a substring match so certificate renewals
	// (which may carry a prefixed subject such as "Open Source Developer
	// Zachariah Walker") continue to validate without a code change.
	if (!str::contains(str::utf16_to_utf8(cert_name), "Zachariah Walker"))
		return false;

	WINTRUST_FILE_INFO FileData = {sizeof(WINTRUST_FILE_INFO)};
	FileData.pcwszFilePath = path.c_str();

	WINTRUST_DATA WinTrustData = {sizeof(WinTrustData)};
	WinTrustData.dwUIChoice = WTD_UI_NONE;
	WinTrustData.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;
	WinTrustData.dwUnionChoice = WTD_CHOICE_FILE;
	WinTrustData.dwProvFlags = WTD_SAFER_FLAG | WTD_REVOCATION_CHECK_CHAIN;
	WinTrustData.pFile = &FileData;

	GUID WVTPolicyGUID = WINTRUST_ACTION_GENERIC_VERIFY_V2;
	const auto status = WinVerifyTrust(app_wnd(), &WVTPolicyGUID, &WinTrustData);

	// Accept a valid signature even when the revocation status can't be reached
	// (offline or the CA endpoint is down), but reject a certificate that is
	// actually revoked.
	return status == ERROR_SUCCESS
		|| status == CRYPT_E_REVOCATION_OFFLINE
		|| status == CRYPT_E_NO_REVOCATION_CHECK;
}

void platform::download_and_verify(const std::function<void(df::file_path)>& complete)
{
	const auto download_path = known_path(known_folder::downloads).combine_file("diffractor-setup.exe");

	web_request req;
	req.path = "diffractor-setup.exe";
	req.download_file_path = download_path;

	const auto con = connect_to_host("diffractor.com");
	const auto response = send_request(con, req);

	if (response.status_code == 200 && verify_package(download_path))
	{
		complete(download_path);
	}
	else
	{
		complete({});
	}
}

platform::file_op_result platform::install(const df::file_path installer_path, const df::folder_path destination_folder,
                                           const bool silent, const bool run_app_after_install)
{
	auto command_line = L"\""s + to_file_system_path(installer_path) + L"\""s;
	if (silent) command_line += L" /S"s;
	if (run_app_after_install) command_line += L" /RR";
	command_line += L" /D="s + to_file_system_path(destination_folder);

	const auto success = verify_package(installer_path) &&
		run_command_line(command_line);

	file_op_result result;

	if (success)
	{
		result.code = file_op_result_code::OK;
	}
	else
	{
		result.error_message = last_os_error();
	}

	return result;
}

#endif


df::file_path platform::temp_file(const std::string_view ext, const df::folder_path folder)
{
	static auto counter = GetTickCount64();
	counter += 1;

	auto name = "diffractor_"s;
	name += str::to_hex(std::bit_cast<const uint8_t*>(&counter), 8);

	if (!str::is_empty(ext))
	{
		if (ext[0] != '.') name += '.';
		name += ext;
	}

	return {folder.is_empty() ? temp_folder() : folder, name};
}

void platform::set_desktop_wallpaper(const df::file_path file_path)
{
	const auto path = to_file_system_path(file_path);

	// IDesktopWallpaper new windows 10 API
	ComPtr<IActiveDesktop> sAD;
	const auto hr = CoCreateInstance(CLSID_ActiveDesktop, nullptr, CLSCTX_ALL, IID_PPV_ARGS(sAD.GetAddressOf()));

	if (SUCCEEDED(hr))
	{
		constexpr WALLPAPEROPT options = {sizeof(WALLPAPEROPT), WPSTYLE_CENTER};
		sAD->SetWallpaper(path.c_str(), 0);
		sAD->SetWallpaperOptions(&options, 0);
		sAD->ApplyChanges(AD_APPLY_ALL);
	}

	SystemParametersInfo(SPI_SETDESKWALLPAPER, 0, (LPVOID)path.c_str(), SPIF_SENDCHANGE);
}

void platform::show_file_properties(const std::vector<df::file_path>& files,
                                    const std::vector<df::folder_path>& folders)
{
	auto* p = new items_data_object();
	p->cache(files, folders);
	SHMultiFileProperties(p, 0);
}

HRESULT GetPIDLFromPath(const LPCWSTR pszPath, __out PIDLIST_ABSOLUTE* ppidl) noexcept
{
	return SHParseDisplayName(pszPath, nullptr, ppidl, 0, nullptr);
}

bool platform::has_burner()
{
	bool result = false;

	ComPtr<ICDBurn> spBurn;
	const auto hr = CoCreateInstance(CLSID_CDBurn, nullptr, CLSCTX_ALL, IID_PPV_ARGS(spBurn.GetAddressOf()));

	if (SUCCEEDED(hr))
	{
		BOOL has_default_burner = FALSE;

		if (SUCCEEDED(spBurn->HasRecordableDrive(&has_default_burner)))
		{
			result = has_default_burner != FALSE;
		}
	}

	return result;
}

void platform::burn_to_cd(const std::vector<df::file_path>& files, const std::vector<df::folder_path>& folders)
{
	record_feature_use(features::burn_to_disk);

	ComPtr<ICDBurn> spBurn;
	const auto hr = CoCreateInstance(CLSID_CDBurn, nullptr, CLSCTX_ALL, IID_PPV_ARGS(spBurn.GetAddressOf()));

	if (SUCCEEDED(hr))
	{
		WCHAR szDefaultBurnerPath[4];
		if (SUCCEEDED(
			spBurn->GetRecorderDriveLetter(szDefaultBurnerPath, std::size(szDefaultBurnerPath))))
		{
			PIDLIST_ABSOLUTE pidlBurner = nullptr;

			// Convert the path to a PIDL
			if (SUCCEEDED(GetPIDLFromPath(szDefaultBurnerPath, &pidlBurner)))
			{
				// Get the IShellFolder for that PIDL
				ComPtr<IShellFolder> spShellFolder;
				if (SUCCEEDED(SHBindToObject(nullptr, pidlBurner, nullptr, IID_PPV_ARGS(&spShellFolder))))
				{
					// Get the IDropTarget for that IShellFolder
					ComPtr<IDropTarget> spDropTarget;
					if (SUCCEEDED(spShellFolder->CreateViewObject(app_wnd(), IID_PPV_ARGS(&spDropTarget))))
					{
						const ComPtr<items_data_object> data = new items_data_object();
						data->cache(files, folders);

						constexpr POINTL pt = {0};
						DWORD dwEffect = DROPEFFECT_LINK | DROPEFFECT_MOVE | DROPEFFECT_COPY;

						spDropTarget->DragEnter(data.Get(), MK_LBUTTON, pt, &dwEffect);
						spDropTarget->Drop(data.Get(), MK_LBUTTON, pt, &dwEffect);
					}
				}
			}
		}
	}
}

void platform::print(const std::vector<df::file_path>& files, const std::vector<df::folder_path>& folders)
{
	static constexpr CLSID CLSID_PrintPhotosDropTarget = {
		0x60fd46de, 0xf830, 0x4894, {0xa6, 0x28, 0x6f, 0xa8, 0x1b, 0xc0, 0x19, 0x0d}
	};

	ComPtr<IDropTarget> drop_target;

	if (SUCCEEDED(
		CoCreateInstance(CLSID_PrintPhotosDropTarget, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(drop_target.
			GetAddressOf()))))
	{
		const ComPtr<items_data_object> data = new items_data_object();
		data->cache(files, folders);

		constexpr POINTL pt = {0, 0};
		DWORD drop_effect = DROPEFFECT_LINK | DROPEFFECT_MOVE | DROPEFFECT_COPY;

		drop_target->DragEnter(data.Get(), MK_LBUTTON, pt, &drop_effect);
		drop_target->Drop(data.Get(), MK_LBUTTON, pt, &drop_effect);
	}
}

void platform::remove_metadata(const std::vector<df::file_path>& files, const std::vector<df::folder_path>& folders)
{
	static constexpr CLSID CLSID_RemovePropertiesDropTarget = {
		0x09a28848, 0x0e97, 0x4cef, {0xb9, 0x50, 0xce, 0xa0, 0x37, 0x16, 0x11, 0x55}
	};

	ComPtr<IDropTarget> spDropTarget;

	if (SUCCEEDED(
		CoCreateInstance(CLSID_RemovePropertiesDropTarget, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&spDropTarget))))
	{
		const ComPtr<items_data_object> data = new items_data_object();
		data->cache(files, folders);

		constexpr POINTL pt = {0, 0};
		DWORD dwEffect = DROPEFFECT_LINK | DROPEFFECT_MOVE | DROPEFFECT_COPY;

		spDropTarget->DragEnter(data.Get(), MK_LBUTTON, pt, &dwEffect);
		spDropTarget->Drop(data.Get(), MK_LBUTTON, pt, &dwEffect);
	}
}

platform::clipboard_data_ptr platform::clipboard()
{
	ComPtr<IDataObject> pdo;
	OleGetClipboard(&pdo);
	return std::make_shared<data_object_client>(pdo.Get());
}

void platform::set_clipboard(const std::vector<df::file_path>& files, const std::vector<df::folder_path>& folders,
                             const file_load_result& loaded, const bool is_move)
{
	auto* p = new items_data_object();
	p->set_for_move(is_move);
	p->cache(files, folders);
	p->cache(loaded);
	OleSetClipboard(p);
}

void platform::set_clipboard(const std::string_view text)
{
	if (OpenClipboard(app_wnd()))
	{
		EmptyClipboard();

		const auto w = str::utf8_to_utf16(text);
		const auto text_size = w.size();

		// Check for potential overflow
		if (text_size > SIZE_MAX / sizeof(wchar_t) - 1)
		{
			df::log(__FUNCTION__, "Text too large for clipboard");
			CloseClipboard();
			return;
		}

		auto* const hglbCopy = GlobalAlloc(GMEM_MOVEABLE, (text_size + 1) * sizeof(wchar_t));

		if (hglbCopy)
		{
			auto* const text_copy = static_cast<wchar_t*>(GlobalLock(hglbCopy));

			if (text_copy)
			{
				memcpy(text_copy, w.data(), text_size * sizeof(wchar_t));

				for (auto i = 0u; i < text_size; ++i)
				{
					const auto c = text_copy[i];

					// poor man's escape
					if (c < 32 && c != 10 && c != 13)
					{
						text_copy[i] = '.';
					}
				}

				text_copy[text_size] = static_cast<wchar_t>(0); // null character 
				GlobalUnlock(hglbCopy);

				SetClipboardData(CF_UNICODETEXT, hglbCopy);
			}
			else
			{
				GlobalFree(hglbCopy);
			}
		}

		CloseClipboard();
	}
}

platform::drop_effect platform::perform_drag(const std::any& frame_handle, const std::vector<df::file_path>& files,
                                             const std::vector<df::folder_path>& folders)
{
	auto* source = new items_drop_source();
	auto* data = new items_data_object();
	data->cache(files, folders);

	DWORD result_effect = DROPEFFECT_NONE;
	const auto hr = DoDragDrop(data, source, DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK, &result_effect);
	::PostMessage(std::any_cast<HWND>(frame_handle), WM_LBUTTONUP, 0, 0);
	return DRAGDROP_S_DROP == hr ? to_drop_effect(result_effect) : drop_effect::none;
}

platform::data_object_probe platform::probe_drag_data_object(const std::vector<df::file_path>& files,
                                                             const std::vector<df::folder_path>& folders)
{
	data_object_probe result;

	const ComPtr<items_data_object> data = new items_data_object();
	data->cache(files, folders);

	// 1. Enumerate advertised formats, preserving the source's order of preference.
	ComPtr<IEnumFORMATETC> en;
	if (SUCCEEDED(data->EnumFormatEtc(DATADIR_GET, &en)) && en)
	{
		FORMATETC fmt{};
		while (en->Next(1, &fmt, nullptr) == S_OK)
		{
			const auto index = static_cast<int>(result.enum_formats.size());

			if (fmt.cfFormat == CF_HDROP) result.hdrop_enum_index = index;
			if (fmt.cfFormat == clipboard_formats::SHELLIDLIST) result.shell_id_list_enum_index = index;

			result.enum_formats.emplace_back(fmt.cfFormat);
		}
	}

	// 2. Which file-bearing formats does the object claim to support.
	FORMATETC fmt_drop = clipboard_formats::Drop;
	FORMATETC fmt_ids = clipboard_formats::DropShellItems;
	result.advertises_hdrop = data->QueryGetData(&fmt_drop) == S_OK;
	result.advertises_shell_id_list = data->QueryGetData(&fmt_ids) == S_OK;

	// 3. CF_HDROP -> parse DROPFILES and count the files it resolves to.
	{
		STGMEDIUM medium{};
		FORMATETC fmt = clipboard_formats::Drop;
		if (data->GetData(&fmt, &medium) == S_OK && medium.hGlobal)
		{
			auto* const hdrop = static_cast<HDROP>(medium.hGlobal);
			const auto count = DragQueryFileW(hdrop, 0xFFFFFFFF, nullptr, 0);
			result.hdrop_count = static_cast<int>(count);

			for (UINT i = 0; i < count; ++i)
			{
				wchar_t path[MAX_PATH * 4] = {};
				if (DragQueryFileW(hdrop, i, path, static_cast<UINT>(std::size(path))))
				{
					result.hdrop_paths.emplace_back(path);
				}
			}

			ReleaseStgMedium(&medium);
		}
	}

	// 4. CFSTR_SHELLIDLIST -> parse the CIDA and resolve each PIDL back to a path.
	{
		STGMEDIUM medium{};
		FORMATETC fmt = clipboard_formats::DropShellItems;
		if (data->GetData(&fmt, &medium) == S_OK && medium.hGlobal)
		{
			if (auto* const pida = static_cast<LPIDA>(GlobalLock(medium.hGlobal)))
			{
				result.shell_id_list_count = static_cast<int>(pida->cidl);

				auto* const base = std::bit_cast<uint8_t*>(pida);
				auto* const folder = std::bit_cast<PCIDLIST_ABSOLUTE>(base + pida->aoffset[0]);

				for (uint32_t i = 0; i < pida->cidl; ++i)
				{
					auto* const child = std::bit_cast<PCUIDLIST_RELATIVE>(base + pida->aoffset[i + 1]);

					if (auto* const full = ILCombine(folder, child))
					{
						wchar_t path[MAX_PATH * 4] = {};
						if (SHGetPathFromIDListW(full, path))
						{
							result.shell_id_list_paths.emplace_back(path);
						}
						ILFree(full);
					}
				}

				GlobalUnlock(medium.hGlobal);
			}

			ReleaseStgMedium(&medium);
		}
	}

	return result;
}

std::string platform::file_op_result::format_error(const std::string_view text,
                                                   const std::string_view more_text) const
{
	std::string result;
	result = text;

	if (!more_text.empty())
	{
		str::join(result, more_text, "\n", false);
	}

	if (error_message.empty())
	{
		if (more_text.empty())
		{
			str::join(result, tt.error_unknown, "\n", false);
		}
	}
	else
	{
		str::join(result, error_message, "\n", false);
	}

	return result;
}

platform::file_op_result platform::delete_items(const std::vector<df::file_path>& files,
                                                const std::vector<df::folder_path>& folders, const bool allow_undo)
{
	const auto paths = all_file_system_paths(files, folders);

	SHFILEOPSTRUCT shfo = {};
	shfo.hwnd = app_wnd();
	shfo.pFrom = paths.c_str();
	shfo.wFunc = FO_DELETE;
	shfo.fFlags = FOF_NOCONFIRMATION | FOF_SILENT | (allow_undo ? FOF_ALLOWUNDO : 0);

	return to_file_op_result(SHFileOperation(&shfo), shfo.fAnyOperationsAborted);
}

platform::file_op_result platform::move_or_copy(const std::vector<df::file_path>& files,
                                                const std::vector<df::folder_path>& folders,
                                                const df::folder_path target, const bool is_move)
{
	const auto paths = all_file_system_paths(files, folders);
	const auto to = to_file_system_path(target);

	SHFILEOPSTRUCT shfo = {
		app_wnd(),
		static_cast<uint32_t>(is_move ? FO_MOVE : FO_COPY),
		paths.c_str(),
		to.c_str(),
		FOF_RENAMEONCOLLISION,
		0, nullptr, nullptr
	};

	return to_file_op_result(SHFileOperation(&shfo), shfo.fAnyOperationsAborted);
}

static bool folder_exists(const std::string_view path)
{
	if (!df::is_path(path)) return false;

	const auto attrib = file_attributes(df::folder_path(path));

	return attrib != INVALID_FILE_ATTRIBUTES &&
		attrib & FILE_ATTRIBUTE_DIRECTORY;
}

static uint64_t fs_to_i64(const DWORD nFileSizeHigh, const DWORD nFileSizeLow)
{
	return static_cast<__int64>(nFileSizeHigh) << 32 | nFileSizeLow;
}

uint64_t ft_to_ts(const FILETIME& ft)
{
	return static_cast<__int64>(ft.dwHighDateTime) << 32 | ft.dwLowDateTime;
}

static __forceinline void populate_file_attributes(platform::file_attributes_t& fi,
                                                   const WIN32_FILE_ATTRIBUTE_DATA& fad)
{
	fi.created = ft_to_ts(fad.ftCreationTime);
	fi.modified = ft_to_ts(fad.ftLastWriteTime);
	fi.size = fs_to_i64(fad.nFileSizeHigh, fad.nFileSizeLow);
	fi.is_readonly = 0 != (fad.dwFileAttributes & FILE_ATTRIBUTE_READONLY);
	fi.is_hidden = 0 != (fad.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN);
	fi.is_offline = 0 != is_offline_attribute(fad.dwFileAttributes);
}

static __forceinline void populate_file_attributes(platform::file_attributes_t& fi, const WIN32_FIND_DATA& fad)
{
	fi.created = ft_to_ts(fad.ftCreationTime);
	fi.modified = ft_to_ts(fad.ftLastWriteTime);
	fi.size = fs_to_i64(fad.nFileSizeHigh, fad.nFileSizeLow);
	fi.is_readonly = 0 != (fad.dwFileAttributes & FILE_ATTRIBUTE_READONLY);
	fi.is_hidden = 0 != (fad.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN);
	fi.is_offline = 0 != is_offline_attribute(fad.dwFileAttributes);
}

platform::file_attributes_t platform::file_attributes(const df::file_path path)
{
	file_attributes_t result;
	WIN32_FILE_ATTRIBUTE_DATA fad;
	const auto w = to_file_system_path(path);
	if (GetFileAttributesEx(w.c_str(), GetFileExInfoStandard, &fad) != 0)
	{
		populate_file_attributes(result, fad);
	}
	return result;
}

platform::file_attributes_t platform::file_attributes(const df::folder_path path)
{
	file_attributes_t result;
	WIN32_FILE_ATTRIBUTE_DATA fad;
	const auto w = to_file_system_path(path);
	if (GetFileAttributesEx(w.c_str(), GetFileExInfoStandard, &fad) != 0)
	{
		populate_file_attributes(result, fad);
	}
	return result;
}


bool df::item_selector::has_media() const
{
	auto result = false;
	constexpr auto show_hidden = true;
	const auto path = _root.combine_file(_wildcard);
	const auto w = platform::to_file_system_path(path);

	WIN32_FIND_DATA fd;
	auto* const hh = FindFirstFileEx(w.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
	                                 FIND_FIRST_EX_LARGE_FETCH);

	if (hh != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (is_folder(fd.dwFileAttributes))
			{
				// recursive?
			}
			else
			{
				const auto name = str::utf16_to_utf8(fd.cFileName); // optimize

				if (can_show_file_or_folder(fd.cFileName, fd.dwFileAttributes, show_hidden))
				{
					if (files::file_type_from_name(name)->is_media())
					{
						result = true;
						break;
					}
				}
			}
		}
		while (FindNextFile(hh, &fd) != 0);

		FindClose(hh);
	}

	return result;
}

std::vector<platform::folder_info> platform::select_folders(const df::item_selector& selector, const bool show_hidden)
{
	std::vector<folder_info> results;

	if (!selector.is_recursive())
	{
		WIN32_FIND_DATA fd;

		const auto root_folder = selector.folder();
		const auto root_path = to_file_system_path(root_folder);
		const auto file_search_path = root_path + L"\\*.*"s;
		auto* const files = FindFirstFileEx(file_search_path.c_str(), FindExInfoBasic, &fd,
		                                    FindExSearchLimitToDirectories,
		                                    nullptr, FIND_FIRST_EX_LARGE_FETCH);

		if (files != INVALID_HANDLE_VALUE)
		{
			do
			{
				if (is_folder(fd.dwFileAttributes))
				{
					if (can_show_file_or_folder(fd.cFileName, fd.dwFileAttributes, show_hidden))
					{
						folder_info i;
						i.name = str::cache(str::utf16_to_utf8(fd.cFileName));
						populate_file_attributes(i.attributes, fd);
						results.emplace_back(i);
					}
				}
			}
			while (FindNextFile(files, &fd) != 0);

			FindClose(files);
		}
	}

	return results;
}


static df::count_and_size calc_folder_summary_impl(const std::wstring& root_path, const bool show_hidden,
                                                   const df::cancel_token& token)
{
	df::count_and_size result;
	WIN32_FIND_DATA fd;

	std::vector<std::wstring> folder_paths = {root_path};

	while (!folder_paths.empty())
	{
		const auto path = folder_paths.back();
		folder_paths.pop_back();

		const auto find_path = path + L"\\*.*"s;
		auto* const files = FindFirstFileEx(find_path.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
		                                    FIND_FIRST_EX_LARGE_FETCH);

		if (files != INVALID_HANDLE_VALUE)
		{
			do
			{
				if (token.is_cancelled())
				{
					FindClose(files);
					return {};
				}

				if (is_folder(fd.dwFileAttributes))
				{
					if (can_show_file_or_folder(fd.cFileName, fd.dwFileAttributes, show_hidden))
					{
						auto child_path = path;
						child_path += '\\';
						child_path += fd.cFileName;
						folder_paths.emplace_back(child_path);
					}
				}
				else
				{
					if (can_show_file(fd.cFileName, fd.dwFileAttributes, show_hidden))
					{
						++result.count;
						result.size += df::file_size(fs_to_i64(fd.nFileSizeHigh, fd.nFileSizeLow));
					}
				}
			}
			while (FindNextFile(files, &fd) != 0 && !token.is_cancelled());

			FindClose(files);
		}
	}

	return result;
}

df::count_and_size platform::calc_folder_summary(const df::folder_path folder, const bool show_hidden,
                                                 const df::cancel_token& token)
{
	const auto root_path = to_file_system_path(folder);
	return calc_folder_summary_impl(root_path, show_hidden, token);
}

platform::folder_contents platform::iterate_file_items(const df::folder_path folder, bool show_hidden)
{
	folder_contents results;
	WIN32_FIND_DATA fd;

	const auto root_path = to_file_system_path(folder);
	const auto file_search_path = root_path + L"\\*.*"s;
	auto* const files = FindFirstFileEx(file_search_path.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
	                                    FIND_FIRST_EX_LARGE_FETCH);

	results.files.reserve(256);
	results.folders.reserve(64);

	if (files != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (is_folder(fd.dwFileAttributes))
			{
				if (can_show_file_or_folder(fd.cFileName, fd.dwFileAttributes, show_hidden))
				{
					folder_info i;
					i.name = str::cache(str::utf16_to_utf8(fd.cFileName));
					populate_file_attributes(i.attributes, fd);
					results.folders.emplace_back(i);
				}
			}
			else
			{
				if (can_show_file(fd.cFileName, fd.dwFileAttributes, show_hidden))
				{
					file_info i;
					i.folder = folder;
					i.name = str::cache(str::utf16_to_utf8(fd.cFileName));
					populate_file_attributes(i.attributes, fd);
					results.files.emplace_back(i);
				}
			}
		}
		while (FindNextFile(files, &fd) != 0);

		FindClose(files);
	}
	else if (is_server(folder.text()))
	{
		NET_API_STATUS res;
		auto path = to_file_system_path(folder);

		do // begin do
		{
			PSHARE_INFO_502 BufPtr = nullptr;
			DWORD er = 0, tr = 0, resume = 0;
			res = NetShareEnum(const_cast<wchar_t*>(path.c_str()), 502, std::bit_cast<LPBYTE*>(&BufPtr),
			                   MAX_PREFERRED_LENGTH, &er, &tr, &resume);

			if (res == ERROR_SUCCESS || res == ERROR_MORE_DATA)
			{
				auto* p = BufPtr;

				for (auto i = 1u; i <= er; i++)
				{
					if (STYPE_DISKTREE == p->shi502_type &&
						IsValidSecurityDescriptor(p->shi502_security_descriptor))
					{
						folder_info i;
						i.name = str::cache(str::utf16_to_utf8(p->shi502_netname));
						i.attributes.is_readonly = true;
						results.folders.emplace_back(i);
					}

					p++;
				}

				NetApiBufferFree(BufPtr);
			}
		}
		while (res == ERROR_MORE_DATA);
	}

	return results;
}

std::vector<platform::file_info> platform::select_files(const df::item_selector& selector, const bool show_hidden)
{
	std::vector<file_info> results;

	const auto recursive = selector.is_recursive();
	const auto has_wild = selector.has_wildcard();

	WIN32_FIND_DATA fd;

	const auto root_folder = selector.folder();
	std::deque<df::folder_path> folders = {root_folder};

	while (!folders.empty())
	{
		const auto current_folder = folders.back();
		folders.pop_back();

		auto root_path = to_file_system_path(current_folder);
		auto file_search_path = root_path + L"\\*.*"s;
		auto* const files = FindFirstFileEx(file_search_path.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch,
		                                    nullptr, FIND_FIRST_EX_LARGE_FETCH);

		if (files != INVALID_HANDLE_VALUE)
		{
			do
			{
				if (is_folder(fd.dwFileAttributes))
				{
					if (recursive && can_show_file_or_folder(fd.cFileName, fd.dwFileAttributes, show_hidden))
					{
						folders.emplace_back(current_folder.combine(str::utf16_to_utf8(fd.cFileName)));
					}
				}
				else
				{
					if (can_show_file_or_folder(fd.cFileName, fd.dwFileAttributes, show_hidden))
					{
						const auto name = str::utf16_to_utf8(fd.cFileName);

						if (!has_wild || str::wildcard_icmp(name, selector.wildcard()))
						{
							file_info i;
							i.folder = current_folder;
							i.name = str::cache(name);
							populate_file_attributes(i.attributes, fd);
							results.emplace_back(i);
						}
					}
				}
			}
			while (FindNextFile(files, &fd) != 0);

			FindClose(files);
		}
	}

	return results;
}

bool platform::write_shell_tags(const df::file_path path, const std::vector<std::string>& tags)
{
	ComPtr<IPropertyStore> propStore;
	const auto w = to_file_system_path(path);
	HRESULT hr = SHGetPropertyStoreFromParsingName(w.c_str(), nullptr, GPS_READWRITE, IID_PPV_ARGS(&propStore));

	if (FAILED(hr))
		return false;

	PROPVARIANT propVar;
	PropVariantInit(&propVar);

	if (tags.empty())
	{
		propVar.vt = VT_EMPTY;
	}
	else
	{
		std::vector<std::wstring> wide_tags;
		wide_tags.reserve(tags.size());
		for (const auto& t : tags)
			wide_tags.emplace_back(str::utf8_to_utf16(t));

		std::vector<PCWSTR> ptrs;
		ptrs.reserve(wide_tags.size());
		for (const auto& wt : wide_tags)
			ptrs.push_back(wt.c_str());

		hr = InitPropVariantFromStringVector(ptrs.data(), static_cast<ULONG>(ptrs.size()), &propVar);

		if (FAILED(hr))
			return false;
	}

	hr = propStore->SetValue(PKEY_Keywords, propVar);
	PropVariantClear(&propVar);

	if (FAILED(hr))
		return false;

	hr = propStore->Commit();
	return SUCCEEDED(hr);
}

platform::metadata_result platform::read_shell_metadata(const df::file_path path)
{
	metadata_result result;

	ComPtr<IPropertyStore> propStore;
	const auto w = to_file_system_path(path);
	HRESULT hr = SHGetPropertyStoreFromParsingName(w.c_str(), nullptr, GPS_DEFAULT, IID_PPV_ARGS(&propStore));

	if (FAILED(hr))
		return result;

	PROPVARIANT propVar;
	PropVariantInit(&propVar);

	if (SUCCEEDED(propStore->GetValue(PKEY_Keywords, &propVar)))
	{
		if (propVar.vt == (VT_VECTOR | VT_LPWSTR))
		{
			for (ULONG i = 0; i < propVar.calpwstr.cElems; ++i)
			{
				result.tags.emplace_back(str::utf16_to_utf8(propVar.calpwstr.pElems[i]));
			}
		}
		PropVariantClear(&propVar);
	}

	PropVariantInit(&propVar);
	if (SUCCEEDED(propStore->GetValue(PKEY_Title, &propVar)))
	{
		if (propVar.vt == VT_LPWSTR && propVar.pwszVal)
		{
			result.title = str::utf16_to_utf8(propVar.pwszVal);
		}
		PropVariantClear(&propVar);
	}

	PropVariantInit(&propVar);
	if (SUCCEEDED(propStore->GetValue(PKEY_Rating, &propVar)))
	{
		if (propVar.vt == VT_UI4)
		{
			result.rating = static_cast<int>(propVar.ulVal);
		}
		PropVariantClear(&propVar);
	}

	return result;
}
