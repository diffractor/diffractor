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

// Test seam definition (see platform.h). Default (empty) means real cloud detection.
std::function<bool(const df::file_path&)> platform::test_offline_predicate;

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

std::atomic<size_t> platform::static_memory_usage = 0;
platform::thread_event platform::event_exit(true, false);


df_assert_pod(platform::file_info);
df_assert_pod(platform::folder_info);
df_assert_move_only(platform::folder_contents);

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

	const auto w = utf8_to_utf16(text);
	if (w.empty()) return std::string(text);

	// First call estimates the required buffer (documented to be an upper bound).
	const auto est = NormalizeString(NormalizationC, w.c_str(), static_cast<int>(w.size()), nullptr, 0);
	if (est <= 0) return std::string(text); // error - leave text unchanged

	std::wstring out(static_cast<size_t>(est), L'\0');
	const auto len = NormalizeString(NormalizationC, w.c_str(), static_cast<int>(w.size()), out.data(), est);
	if (len <= 0) return std::string(text);

	out.resize(static_cast<size_t>(len));
	return utf16_to_utf8(out);
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
	// Onedrive and GVFS use file attributes to denote files or directories that
	// may not be locally present and are only available "online". These files are applied one of
	// the two file attributes: FILE_ATTRIBUTE_RECALL_ON_OPEN or FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS.
	// FILE_ATTRIBUTE_RECALL_ON_OPEN means nothing is present locally; FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS
	// may be partially present. Both are still enumerated and indexed, then read through the shell
	// rather than opened, because opening one hydrates (downloads) it.
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
	if (!show_hidden && (attributes & FILE_ATTRIBUTE_HIDDEN) != 0) return false;
	return !is_folder(attributes) && !is_dots(name);
}

static bool can_show_folder(const wchar_t* name, const DWORD attributes, const bool show_hidden)
{
	if (str::is_empty(name)) return false;
	if (attributes == INVALID_FILE_ATTRIBUTES) return false;
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

// Junctions and symlinks are still listed, but descending into one can point back at an ancestor and
// turn a recursive scan into an unbounded walk of the disk.
static bool can_descend_into_folder(const wchar_t* name, const DWORD attributes, const bool show_hidden)
{
	return can_show_folder(name, attributes, show_hidden) && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
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

static void make_extended_path(std::wstring& path)
{
	if (path.size() < MAX_PATH || path.starts_with(L"\\\\?\\")) return;

	if (path.starts_with(L"\\\\"))
	{
		path.replace(0, 2, L"\\\\?\\UNC\\");
	}
	else
	{
		path.insert(0, L"\\\\?\\");
	}
}

std::wstring platform::to_file_system_path(const df::file_path path)
{
	auto result = str::utf8_to_utf16(path.pack());
	make_extended_path(result);
	return result;
};

std::string platform::to_utf8_file_system_path(const df::file_path path)
{
	return str::utf16_to_utf8(to_file_system_path(path));
}

std::wstring platform::to_shell_path(const df::file_path path)
{
	return str::utf8_to_utf16(path.pack());
}

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
	auto result = to_shell_path(path);
	make_extended_path(result);
	return result;
}

std::filesystem::path platform::to_stream_path(const df::file_path path)
{
	return {to_file_system_path(path)};
}

std::filesystem::path platform::to_stream_path(const df::folder_path path)
{
	return {to_file_system_path(path)};
}

std::wstring platform::to_shell_path(const df::folder_path path)
{
	if (df::folder_path::is_guid_path(path.text()))
	{
		return parse_special_path(path.text());
	}

	return str::utf8_to_utf16(path.text());
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
		paths.emplace_back(ILCreateFromPath(platform::to_shell_path(path).c_str()));
	}

	for (const auto& path : folders)
	{
		paths.emplace_back(ILCreateFromPath(platform::to_shell_path(path).c_str()));
	}

	for (const auto& i : paths)
	{
		total_pidl_len += ILGetSize(i);
	}

	const auto cida_len = sizeof(CIDA) + (paths.size() + 1) * sizeof(uint32_t);

	// TYMED_HGLOBAL requires moveable memory, matching the CF_HDROP medium built below.
	auto* const hGlobal = GlobalAlloc(GHND,
	                                  static_cast<DWORD>(cida_len + total_pidl_len + sizeof(uint32_t) + 1));

	if (!hGlobal)
	{
		for (const auto& i : paths)
		{
			ILFree(i);
		}

		return nullptr;
	}

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
		result += platform::to_shell_path(path);
		result += delim;
	}

	for (const auto& path : files)
	{
		result += platform::to_shell_path(path);
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
			if (pmedium->hGlobal == nullptr) return E_OUTOFMEMORY;
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
					return S_OK;
				}

				GlobalFree(h);
			}

			return E_OUTOFMEMORY;
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

// CF_HDROP blocks come from another process, so pFiles and the double-NUL terminator are validated
// against the actual allocation before anything walks the list.
class locked_drop_files
{
public:
	explicit locked_drop_files(const HGLOBAL h) : _h(h)
	{
		if (!_h) return;

		const auto cb = GlobalSize(_h);
		_drop = static_cast<const DROPFILES*>(GlobalLock(_h));

		if (!_drop) return;

		_locked = true;

		if (cb < sizeof(DROPFILES) || _drop->pFiles < sizeof(DROPFILES) || _drop->pFiles >= cb) return;

		const auto* const list = std::bit_cast<const uint8_t*>(_drop) + _drop->pFiles;
		const auto char_size = _drop->fWide ? sizeof(wchar_t) : sizeof(char);
		const auto count = (cb - _drop->pFiles) / char_size;
		auto zeros = 0;

		for (size_t i = 0; i < count; ++i)
		{
			const auto is_zero = _drop->fWide
				                     ? std::bit_cast<const wchar_t*>(list)[i] == 0
				                     : list[i] == 0;

			if (!is_zero)
			{
				zeros = 0;
			}
			else if (++zeros == 2)
			{
				_list = list;
				return;
			}
		}
	}

	~locked_drop_files()
	{
		if (_locked) GlobalUnlock(_h);
	}

	locked_drop_files(const locked_drop_files&) = delete;
	locked_drop_files& operator=(const locked_drop_files&) = delete;

	bool is_valid() const { return _list != nullptr; }
	bool is_wide() const { return _drop->fWide != 0; }
	const wchar_t* wide_list() const { return std::bit_cast<const wchar_t*>(_list); }
	const char* narrow_list() const { return std::bit_cast<const char*>(_list); }

private:
	HGLOBAL _h = nullptr;
	const DROPFILES* _drop = nullptr;
	const uint8_t* _list = nullptr;
	bool _locked = false;
};

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

	if (_pData && SUCCEEDED(_pData->GetData(&clipboard_formats::PDE, &stgMedium)))
	{
		auto* const h = stgMedium.hGlobal;
		const auto cb = GlobalSize(h);
		const auto* const p = static_cast<DWORD*>(GlobalLock(h));

		if (p)
		{
			if (cb >= sizeof(DWORD)) result = *p;
			GlobalUnlock(h);
		}

		ReleaseStgMedium(&stgMedium);
	}

	return result;
}

static LCID g_lLangId = MAKELCID(LANG_NEUTRAL, SORT_DEFAULT);

static std::string format_os_error(const DWORD error)
{
	// FormatMessageW leaves the buffer untouched for codes with no system message, which is common
	// for the HRESULT-shaped values passed in here.
	wchar_t sz[1000]{};
	const auto len = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error,
	                                g_lLangId, sz, std::size(sz), nullptr);
	auto result = len == 0 ? std::string{} : str::utf16_to_utf8(sz);
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
	if (res == FO_CANCELLED || fAnyOperationsAborted != 0)
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
				auto src_path = df::file_path(str::utf16_to_utf8(std::wstring_view(sz_start, sz - sz_start - 1)));
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
	const locked_drop_files drop(h);
	platform::file_op_result result;

	if (drop.is_valid())
	{
		const auto op_code = static_cast<UINT>(is_move ? FO_MOVE : FO_COPY);

		if (drop.is_wide())
		{
			const auto targetW = platform::to_shell_path(target);
			const auto* const file_list = drop.wide_list();

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
					name_mapping[to_file_path(std::wstring_view{nm.pszOldPath, static_cast<size_t>(nm.cchOldPath)})] =
						to_file_path(std::wstring_view{nm.pszNewPath, static_cast<size_t>(nm.cchNewPath)});
				}
			}

			SHFreeNameMappings(shfo.hNameMappings);

			result.created_files = dest_file_list(target, file_list, name_mapping);
		}
		else
		{
			const auto targetA = utf8_cast2(target.text());
			const auto* const file_list = drop.narrow_list();

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
			auto* const s = std::bit_cast<HANDLETOMAPPINGSA*>(shfo.hNameMappings);

			if (s)
			{
				for (auto i = 0u; i < s->uNumberOfMappings; i++)
				{
					const auto& nm = s->lpSHNameMapping[i];
					const auto old_path = str::utf8_cast2(
						std::string_view(nm.pszOldPath, static_cast<size_t>(nm.cchOldPath)));
					const auto new_path = str::utf8_cast2(
						std::string_view(nm.pszNewPath, static_cast<size_t>(nm.cchNewPath)));
					name_mapping[df::file_path(old_path)] = df::file_path(new_path);
				}
			}

			SHFreeNameMappings(shfo.hNameMappings);

			result.created_files = dest_file_list(target, str::utf8_cast2(file_list).c_str(), name_mapping);
		}
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

	if (_pData && SUCCEEDED(_pData->GetData(&clipboard_formats::Drop, &stgMedium)))
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

	if (_pData && SUCCEEDED(_pData->GetData(&clipboard_formats::Drop, &stgMedium)))
	{
		if (const locked_drop_files drop(stgMedium.hGlobal); drop.is_valid())
		{
			result = drop.is_wide() ? to_file_path(drop.wide_list()) : df::file_path(drop.narrow_list());
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

	if (_pData && SUCCEEDED(_pData->GetData(&clipboard_formats::Drop, &stgMedium)))
	{
		if (const locked_drop_files drop(stgMedium.hGlobal); drop.is_valid())
		{
			result.count = 1;

			if (drop.is_wide())
			{
				const auto* sz = drop.wide_list();
				result.first_name = str::utf16_to_utf8(sz);
				result.has_readonly |= (file_attributes(to_folder_path(sz)) & FILE_ATTRIBUTE_READONLY) != 0;

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
				const auto* sz = drop.narrow_list();
				result.first_name = str::utf8_cast(sz);
				result.has_readonly |= (file_attributes(df::folder_path(sz)) &
					FILE_ATTRIBUTE_READONLY) != 0;

				while (sz[0] != 0 || sz[1] != 0)
				{
					if (*sz++ == 0)
					{
						++result.count;
					}
				}
			}
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

	const HRESULT hr = _pData ? _pData->GetData(&clipboard_formats::Bitmap, &stgMedium) : E_POINTER;

	if (SUCCEEDED(hr))
	{
		result = save_bitmap_info(save_path, name, as_png, stgMedium.hBitmap);

		// Don't unlock bitmap handle - ReleaseStgMedium will handle cleanup
		ReleaseStgMedium(&stgMedium);
	}

	return result;
}


void* platform::memory_pool::alloc(const size_t size)
{
	static std::bad_alloc OOM;
	exclusive_lock lock(cs);

	if (size > block_size) throw OOM;

	if (base == nullptr)
	{
		// Reserve once and never move: handles are offsets from this address for the process lifetime.
		// A 32-bit address space may not have the whole range free in one piece, so settle for a smaller
		// arena rather than failing to start - the size only bounds how many strings can be interned.
		for (auto attempt = reserve_size; attempt >= block_size; attempt /= 2)
		{
			base = std::bit_cast<uint8_t*>(VirtualAlloc(nullptr, attempt, MEM_RESERVE, PAGE_NOACCESS));

			if (base)
			{
				reserved = attempt;
				break;
			}
		}

		if (!base) throw OOM;
	}

	const auto align_size = (size + (alignment - 1)) / alignment * alignment;

	if (align_size > committed - used)
	{
		const auto shortfall = align_size - (committed - used);
		const auto grow = (shortfall + block_size - 1) / block_size * block_size;

		if (grow > reserved - committed) throw OOM;
		if (!VirtualAlloc(base + committed, grow, MEM_COMMIT, PAGE_READWRITE)) throw OOM;

		committed += grow;
		static_memory_usage.fetch_add(grow, std::memory_order_relaxed);
	}

	auto* const result = base + used;
	used += align_size;
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
			// IPersistFile is a Shell binding and rejects the \\?\ prefix.
			const auto link = to_shell_path(path);

			if (ppf && SUCCEEDED(ppf->Load(link.c_str(), STGM_READ)))
			{
				wchar_t result_path[MAX_PATH];
				const auto success = SUCCEEDED(psl->GetPath(result_path, MAX_PATH, nullptr, 0));
				if (success) result = to_file_path(result_path);
			}
		}
	}

	return result;
}

bool platform::sse2_supported = IsProcessorFeaturePresent(PF_XMMI64_INSTRUCTIONS_AVAILABLE) != 0;
bool platform::ssse3_supported = IsProcessorFeaturePresent(PF_SSSE3_INSTRUCTIONS_AVAILABLE) != 0;
bool platform::crc32_supported = IsProcessorFeaturePresent(PF_SSE4_2_INSTRUCTIONS_AVAILABLE) != 0;
bool platform::avx2_supported = IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE) != 0;
bool platform::avx512_supported = IsProcessorFeaturePresent(PF_AVX512F_INSTRUCTIONS_AVAILABLE) != 0;
bool platform::neon_supported = IsProcessorFeaturePresent(PF_ARM_NEON_INSTRUCTIONS_AVAILABLE) != 0;
bool platform::arm_crc32_supported = IsProcessorFeaturePresent(PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE) != 0;

#include <bcrypt.h>
#pragma comment(lib, "bcrypt")

void platform::secure_zero(void* ptr, const size_t len)
{
	SecureZeroMemory(ptr, len);
}

bool platform::generate_random_bytes(uint8_t* buffer, const size_t len)
{
	const auto status = BCryptGenRandom(nullptr, buffer, static_cast<ULONG>(len), BCRYPT_USE_SYSTEM_PREFERRED_RNG);

	if (!BCRYPT_SUCCESS(status))
	{
		// Leaving the buffer untouched would hand the caller stale bytes that look random.
		SecureZeroMemory(buffer, len);
		df::log(__FUNCTION__, std::format("BCryptGenRandom failed with status {:#x}", static_cast<uint32_t>(status)));
		return false;
	}

	return true;
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
	sprintf_s(result, "%u.%u", osvi.dwMajorVersion, osvi.dwMinorVersion);
	return str::utf8_cast2(result);
}

static df::machine_arch to_machine_arch(const USHORT image_file_machine)
{
	switch (image_file_machine)
	{
	case IMAGE_FILE_MACHINE_I386: return df::machine_arch::x86;
	case IMAGE_FILE_MACHINE_AMD64: return df::machine_arch::x64;
	case IMAGE_FILE_MACHINE_ARMNT: return df::machine_arch::arm32;
	case IMAGE_FILE_MACHINE_ARM64: return df::machine_arch::arm64;
	case IMAGE_FILE_MACHINE_UNKNOWN: return df::machine_arch::unknown;
	default: return df::machine_arch::other;
	}
}

// The build's own target, which is a compile-time fact and cannot be got wrong at runtime.
static df::machine_arch process_arch()
{
#if defined(_M_ARM64) || defined(__aarch64__)
	return df::machine_arch::arm64;
#elif defined(_M_ARM) || defined(__arm__)
	return df::machine_arch::arm32;
#elif defined(_M_X64) || defined(__x86_64__)
	return df::machine_arch::x64;
#elif defined(_M_IX86) || defined(__i386__)
	return df::machine_arch::x86;
#else
	return df::machine_arch::other;
#endif
}

// The *native* machine, which is the whole point of the field: a 32-bit process on a 64-bit machine
// is the number that decides whether the 32-bit build is still earning its place, and asking the
// process is what makes that question unanswerable. IsWow64Process2 is Windows 10 1511 and later, so
// an older system answers unknown rather than a guess.
static df::machine_arch native_machine_arch()
{
	using pfnIsWow64Process2 = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);

	if (const auto kernel = GetModuleHandleW(L"kernel32.dll"))
	{
		if (const auto fn = std::bit_cast<pfnIsWow64Process2>(GetProcAddress(kernel, "IsWow64Process2")))
		{
			USHORT process_machine = IMAGE_FILE_MACHINE_UNKNOWN;
			USHORT native_machine = IMAGE_FILE_MACHINE_UNKNOWN;

			if (fn(GetCurrentProcess(), &process_machine, &native_machine))
			{
				return to_machine_arch(native_machine);
			}
		}
	}

	// Without IsWow64Process2 the native machine still has an honest source: GetNativeSystemInfo
	// reports the machine rather than the process, and it is correct under emulation, which is the
	// case worth knowing about. It exists on every supported Windows, so this is a fallback in
	// availability only.
	SYSTEM_INFO info = {};
	GetNativeSystemInfo(&info);

	switch (info.wProcessorArchitecture)
	{
	case PROCESSOR_ARCHITECTURE_INTEL: return df::machine_arch::x86;
	case PROCESSOR_ARCHITECTURE_AMD64: return df::machine_arch::x64;
	case PROCESSOR_ARCHITECTURE_ARM: return df::machine_arch::arm32;
	case PROCESSOR_ARCHITECTURE_ARM64: return df::machine_arch::arm64;
	case PROCESSOR_ARCHITECTURE_UNKNOWN: return df::machine_arch::unknown;
	// The call answered with a machine this field has no member for, which is precisely what other
	// is for. Booking it as unknown would hide it among the readings that were never taken.
	default: return df::machine_arch::other;
	}
}

static df::os_release windows_release()
{
#pragma warning(push)
#pragma warning(disable:4996)
	OSVERSIONINFO osvi = {};
	osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	const auto read = GetVersionEx(&osvi) != 0;
#pragma warning(pop)

	// A reading we could not take is unknown, not other: other is the signal that the field has run
	// out of room, and a failed call booked there would look like one.
	if (!read || osvi.dwMajorVersion == 0) return df::os_release::unknown;

	if (osvi.dwMajorVersion > 10) return df::os_release::windows_later;

	if (osvi.dwMajorVersion == 10)
	{
		return osvi.dwBuildNumber >= 22000 ? df::os_release::windows_11 : df::os_release::windows_10;
	}

	// 6.0 is Vista and 6.2 is Windows 8.0. Neither has a value, and folding them into a neighbour
	// would fix a wrong meaning permanently - a value's meaning is fixed once assigned.
	if (osvi.dwMajorVersion == 6)
	{
		if (osvi.dwMinorVersion >= 3) return df::os_release::windows_8_1;
		if (osvi.dwMinorVersion == 1) return df::os_release::windows_7;
	}

	return df::os_release::other;
}

// Where the running binary came from. The Store package is a full-trust desktop-bridge app, so
// nothing about its behaviour marks it out - only its install location does.
static df::package_kind installed_package()
{
#ifdef WINSTORE
	return df::package_kind::microsoft_store;
#else
	const auto app_folder = platform::known_path(platform::known_folder::running_app_folder);

	if (app_folder.is_empty()) return df::package_kind::unknown;

	// The desktop installer puts the application in %LOCALAPPDATA%\Diffractor; anything running
	// from anywhere else was unpacked by hand. The boundary matters: a plain prefix compare reads
	// %LOCALAPPDATA%\DiffractorPortable as an install.
	const auto installed_root = platform::known_path(platform::known_folder::app_data);

	// A root we could not read is unknown, not portable. path_from_csidl fabricates a relative path
	// when the shell lookup fails, so an unqualified root means the comparison never happened -
	// booking that as portable would report every install on such a machine as unpacked by hand.
	if (!installed_root.is_qualified()) return df::package_kind::unknown;

	const std::string_view app_text = app_folder.text();
	const std::string_view root_text = installed_root.text();

	if (df::path_text_starts(app_text, root_text) &&
		(app_text.size() == root_text.size() || df::is_path_sep(app_text[root_text.size()])))
	{
		return df::package_kind::installer;
	}

	return df::package_kind::portable;
#endif
}

df::environment_facts platform::environment()
{
	df::environment_facts result;
	result.family = df::os_family::windows;
	result.release = windows_release();
	result.process = process_arch();
	result.machine = native_machine_arch();
	result.package = installed_package();
	return result;
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
			// Worker threads name themselves as they start, so the lookup is resolved exactly once.
			static const pfnSetThreadDescription set_thread_description_proc = []
			{
				auto* const kernel32 = GetModuleHandleW(L"kernel32.dll");
				return kernel32
					       ? std::bit_cast<pfnSetThreadDescription>(GetProcAddress(kernel32, "SetThreadDescription"))
					       : nullptr;
			}();

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
	wchar_t w[size]{};
	DWORD len = size;

	if (!GetUserName(w, &len))
	{
		df::log(__FUNCTION__, last_os_error());
		return {};
	}

	return str::utf16_to_utf8(w);
}

std::string platform::last_os_error()
{
	return last_os_error_impl();
}

std::string platform::file_write_error(const df::file_path path)
{
	// Probe write access the same way a metadata writer would: open the existing file for
	// writing with no sharing. This surfaces the concrete, OS-localised reason - most often a
	// sharing violation ("being used by another process") from an AV scanner, search indexer,
	// cloud sync, or the app's own handle - instead of an opaque toolkit error (#231).
	const auto w = to_file_system_path(path);
	const auto h = CreateFileW(w.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

	if (h == INVALID_HANDLE_VALUE)
	{
		return format_os_error(GetLastError());
	}

	CloseHandle(h);
	return {};
}

bool platform::wait_for_unlocked_write(const df::file_path path)
{
	// A reader started by an earlier edit can still hold the file when the next write arrives. Only a
	// sharing or lock violation is worth waiting on; anything else will not clear by waiting.
	const auto w = to_file_system_path(path);

	for (auto attempt = 0; attempt < 5; ++attempt)
	{
		const auto h = CreateFileW(w.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
		                           nullptr);

		if (h != INVALID_HANDLE_VALUE)
		{
			CloseHandle(h);
			return true;
		}

		const auto last_error = GetLastError();

		if (last_error != ERROR_SHARING_VIOLATION && last_error != ERROR_LOCK_VIOLATION)
		{
			return false;
		}

		Sleep(50 * (attempt + 1));
	}

	return false;
}

static platform::file_op_result last_op_result(const BOOL res)
{
	platform::file_op_result result;

	if (res == 0)
	{
		const auto error = GetLastError();
		result.code = error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS
			              ? platform::file_op_result_code::ALREADY_EXISTS
			              : platform::file_op_result_code::FAILED;
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

// Commit any buffered writes for a file to the underlying volume. On network
// drives (SMB) writes are held in a write-behind cache; if we swap the file in
// before those bytes reach the server the destination can look unchanged even
// though every call "succeeded". Flushing here makes the replace durable.
// See issue #207 (updates to files on network drives do not always update).
static platform::file_op_result flush_file_to_disk(const df::file_path path)
{
	const auto w = platform::to_file_system_path(path);
	auto* const h = CreateFileW(w.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
	                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

	if (h == INVALID_HANDLE_VALUE)
	{
		return platform::replacement_flush_result(false, format_os_error(GetLastError()));
	}

	const auto flushed = FlushFileBuffers(h) != 0;
	const auto flush_error = flushed ? ERROR_SUCCESS : GetLastError();
	CloseHandle(h);
	return platform::replacement_flush_result(flushed,
	                                          flushed ? std::string{} : format_os_error(flush_error));
}

platform::file_op_result platform::replacement_flush_result(const bool flushed, std::string error_message)
{
	file_op_result result;
	result.code = flushed ? file_op_result_code::OK : file_op_result_code::FAILED;
	result.error_message = std::move(error_message);
	return result;
}

// Build the extended-length form of a path ("\\?\C:\..." for a drive, "\\?\UNC\server\share\..."
// for a UNC path). SetFileInformationByHandle with FILE_RENAME_INFO wants a fully-qualified target
// in this form; a plain "\\server\share\..." UNC target is rejected with ERROR_INVALID_NAME (123).
// Verified on Synology SMB3 (tmp/nas_rename_probe.py). Idempotent if already extended.
static std::wstring to_extended(const std::wstring& p)
{
	if (p.rfind(LR"(\\?\)", 0) == 0)
	{
		return p; // already extended
	}

	if (p.size() >= 2 && p[0] == L'\\' && p[1] == L'\\')
	{
		return LR"(\\?\UNC\)" + p.substr(2); // \\server\share\... -> \\?\UNC\server\share\...
	}

	return LR"(\\?\)" + p; // C:\... -> \\?\C:\...
}

// Rename an open handle's file to targetW via SetFileInformationByHandle(FileRenameInfo), retrying
// the transient oplock/lease-break errors seen on a just-written SMB destination. Returns true on
// success; on failure last_error holds the final GetLastError().
static bool rename_by_handle(const HANDLE h, const std::wstring& targetW, const bool replace_if_exists,
                             DWORD& last_error)
{
	const auto name_bytes = targetW.size() * sizeof(wchar_t);
	std::vector<uint8_t> buffer(sizeof(FILE_RENAME_INFO) + name_bytes);
	auto* const info = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
	info->ReplaceIfExists = replace_if_exists ? TRUE : FALSE;
	info->RootDirectory = nullptr;
	info->FileNameLength = static_cast<DWORD>(name_bytes);
	memcpy(info->FileName, targetW.c_str(), name_bytes);

	last_error = ERROR_SUCCESS;

	for (auto attempt = 0; attempt < 5; ++attempt)
	{
		if (SetFileInformationByHandle(h, FileRenameInfo, info, static_cast<DWORD>(buffer.size())) != 0)
		{
			return true;
		}

		last_error = GetLastError();

		if (last_error != ERROR_SHARING_VIOLATION && last_error != ERROR_LOCK_VIOLATION &&
			last_error != ERROR_ACCESS_DENIED)
		{
			break;
		}

		Sleep(50 * (attempt + 1));
	}

	return false;
}

// Replace `destination` with `existing` (a freshly-written temp file in the same folder). This is a
// clean-room reimplementation of ReplaceFileW's swap, used uniformly for local and network paths.
//
// Rather than call the Win32 ReplaceFileW (which cannot hand back an open handle and, on some SMB
// servers, leaves the caller reading a stale by-name data cache after the swap), we rename the
// replacement into place THROUGH a retained handle and return that same still-open, cache-coherent
// handle. The caller re-scans the edited file through this handle instead of a fresh by-name open,
// which is what fixes the SMB read-after-write staleness (proven in tmp/nas_handle_rename_test.py).
//
// Like ReplaceFileW we preserve ONLY the destination's creation time, set on the replacement BEFORE
// the rename. If the handle path cannot run (e.g. a filesystem that rejects rename-by-handle, or a
// read-only / reparse-point target) we fall back to MoveFileEx (which returns no coherent handle).
platform::file_op_result platform::replace_file(const df::file_path destination, const df::file_path existing,
                                                const bool create_originals)
{
	// Make sure the replacement's contents are actually on the volume before we swap it in
	// (network write-behind cache); this is what makes updates stick on network drives (issue #207).
	if (const auto flush_result = flush_file_to_disk(existing); flush_result.failed())
	{
		return flush_result;
	}

	const auto destination_exists = destination.exists();

	df::file_path backup;

	if (destination_exists && create_originals)
	{
		const auto base_name = std::string(destination.file_name_without_extension()) + ".original"s;
		const auto extension = destination.extension();

		// A requested backup is part of the contract, so keep uniquifying instead of silently replacing
		// the destination with no new recovery point when an earlier .original is already there.
		for (auto attempt = 0; attempt < 1000 && backup.is_empty(); ++attempt)
		{
			const auto name = attempt == 0 ? base_name : std::format("{}.{}", base_name, attempt);
			const auto candidate = df::file_path(existing.folder(), name, extension);

			if (!candidate.exists())
			{
				backup = candidate;
			}
		}

		if (backup.is_empty())
		{
			file_op_result result;
			result.code = file_op_result_code::FAILED;
			result.error_message = std::format("Could not create a backup of {}", destination.str());
			df::log(__FUNCTION__, result.error_message);
			return result;
		}
	}

	// A requested backup is part of the operation's contract. Create it before opening or renaming
	// the replacement so failure leaves both the destination and edited temporary file untouched.
	if (!backup.is_empty())
	{
		if (const auto backup_result = copy_file(destination, backup, true, false); backup_result.failed())
		{
			return backup_result;
		}
	}

	const auto existingW = to_extended(to_file_system_path(existing));
	const auto destinationW = to_extended(to_file_system_path(destination));

	// Preserve the destination's creation time (ReplaceFileW semantics). Also decide whether the
	// handle-rename path is applicable: skip it for read-only or reparse-point (symlink/junction)
	// targets, which ReplaceFileW itself refuses - let the move fallback surface the same outcome.
	FILETIME dst_creation{};
	bool have_creation = false;
	bool can_handle_rename = true;

	if (destination_exists)
	{
		const auto dst_attr = GetFileAttributesW(destinationW.c_str());

		if (dst_attr == INVALID_FILE_ATTRIBUTES ||
			(dst_attr & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
		{
			can_handle_rename = false;
		}
		else
		{
			auto* const dh = CreateFileW(destinationW.c_str(), FILE_READ_ATTRIBUTES,
			                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
			                             OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
			if (dh != INVALID_HANDLE_VALUE)
			{
				have_creation = GetFileTime(dh, &dst_creation, nullptr, nullptr) != 0;
				CloseHandle(dh);
			}
		}
	}

	if (can_handle_rename)
	{
		// Open the replacement with DELETE (to rename it via SetFileInformationByHandle),
		// GENERIC_READ (so the caller can read it back through this handle), and
		// FILE_WRITE_ATTRIBUTES (to stamp the preserved creation time). Share every mode so a
		// concurrent oplock/lease break can proceed.
		auto* h = CreateFileW(existingW.c_str(), GENERIC_READ | DELETE | FILE_WRITE_ATTRIBUTES,
		                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
		                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

		if (h != INVALID_HANDLE_VALUE)
		{
			if (have_creation)
			{
				SetFileTime(h, &dst_creation, nullptr, nullptr);
			}

			DWORD rename_error = ERROR_SUCCESS;

			if (rename_by_handle(h, destinationW, true, rename_error))
			{
				file_op_result result;
				result.code = file_op_result_code::OK;

				// Authoritative modified time read back through the (renamed) handle. The caller
				// uses this as both file_modified and metadata_scanned so a later background
				// rescan is a no-op.
				FILETIME modified{};
				if (GetFileTime(h, nullptr, nullptr, &modified))
				{
					result.modified = ft_to_ts(modified);
				}

				// The rename needed DELETE access, and holding it blocks any later by-path reader
				// that does not itself share DELETE - LibRaw's RAW open is one. ReOpenFile drops
				// the access without a by-name reopen, so the handle stays cache-coherent.
				auto* const read_only = ReOpenFile(h, GENERIC_READ,
				                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0);

				if (read_only != INVALID_HANDLE_VALUE)
				{
					CloseHandle(h);
					h = read_only;
				}

				result.coherent_handle = make_file_from_handle(h); // takes ownership of h
				return result;
			}

			CloseHandle(h);
			df::log(__FUNCTION__, std::format("rename-by-handle failed with error {}, falling back to move",
			                                  static_cast<uint32_t>(rename_error)));
		}
	}

	// Fallback: MoveFileEx (no coherent handle). Move the replacement into place with write-through
	// so the new file is committed on network drives.
	const auto move_result = last_op_result(MoveFileExW(existingW.c_str(), destinationW.c_str(),
	                                                    MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH |
	                                                    (destination_exists ? MOVEFILE_REPLACE_EXISTING : 0)));

	// Preserve the replaced file's creation time on the fallback path too (ReplaceFileW semantics).
	// MoveFileEx gives the destination the replacement's creation time, so restore the captured one.
	if (move_result.success() && have_creation)
	{
		auto* const rh = CreateFileW(destinationW.c_str(), FILE_WRITE_ATTRIBUTES,
		                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
		                             OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
		if (rh != INVALID_HANDLE_VALUE)
		{
			SetFileTime(rh, &dst_creation, nullptr, nullptr);
			CloseHandle(rh);
		}
	}

	return move_result;
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

	const auto w = to_shell_path(path);
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

bool platform::is_writable(const df::folder_path path)
{
	if (path.is_empty() || !exists(path))
	{
		return false;
	}

	// Directory attributes do not reflect ACL-based denial (a Store package folder looks normal),
	// so the only reliable probe is creating a file. The handle deletes itself when closed.
	const auto name = std::format("df-write-probe-{:08x}.tmp", static_cast<uint32_t>(GetCurrentProcessId()));
	const auto w = to_file_system_path(path.combine_file(name));

	const auto h = CreateFileW(w.c_str(), GENERIC_WRITE, FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS,
	                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);

	if (h == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	CloseHandle(h);
	return true;
}

bool platform::open(const df::file_path path)
{
	const auto w = to_file_system_path(path);
	return ShellExecute(app_wnd(), L"open", w.c_str(), L"", L"", SW_SHOWNORMAL) > std::bit_cast<HINSTANCE>(
		static_cast<uintptr_t>(32));
}

bool platform::open(const std::string_view path)
{
	const auto w = str::utf8_to_utf16(path);
	return ShellExecute(app_wnd(), L"open", w.c_str(), L"", L"", SW_SHOWNORMAL) > std::bit_cast<HINSTANCE>(
		static_cast<uintptr_t>(32));
}

// Resolves an executable in the Windows directory to an absolute path so CreateProcess never falls
// back to searching the current directory for it (untrusted search path, CWE-426).
static std::wstring windows_dir_executable_path(const std::wstring_view name)
{
	wchar_t dir[MAX_PATH]{};
	const auto len = GetWindowsDirectoryW(dir, std::size(dir));
	if (len == 0 || len >= std::size(dir)) return {};
	return std::wstring(dir, len) + L'\\' + std::wstring(name);
}

// application_path may be empty only for a caller-supplied command line that names its own
// executable; every in-app launch passes an absolute path.
static bool run_command_line(const std::wstring& application_path, const std::wstring& command_line)
{
	PROCESS_INFORMATION pi = {};
	STARTUPINFO si = {};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_SHOWNORMAL;

	// CreateProcess is documented to modify lpCommandLine, so hand it a mutable copy.
	std::vector<wchar_t> mutable_command_line(command_line.cbegin(), command_line.cend());
	mutable_command_line.push_back(0);

	if (CreateProcess(application_path.empty() ? nullptr : application_path.c_str(), mutable_command_line.data(),
	                  nullptr, nullptr, FALSE, CREATE_DEFAULT_ERROR_MODE, nullptr, nullptr, &si, &pi))
	{
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		return true;
	}

	df::log(__FUNCTION__, platform::last_os_error());
	return false;
}

bool platform::run(const std::string_view cmd)
{
	return run_command_line({}, str::utf8_to_utf16(cmd));
}

bool platform::run(const df::file_path exe, const std::string_view cmd)
{
	if (exe.is_empty()) return false;
	return run_command_line(to_file_system_path(exe), str::utf8_to_utf16(cmd));
}


static bool run_explorer(const std::wstring& path)
{
	const auto explorer = windows_dir_executable_path(L"explorer.exe");
	if (explorer.empty()) return false;
	return run_command_line(explorer, L"\""s + explorer + L"\" /select,\""s + path + L"\""s);
}

void platform::show_in_file_browser(const df::file_path path)
{
	run_explorer(to_shell_path(path));
}

void platform::show_in_file_browser(const df::folder_path path)
{
	run_explorer(to_shell_path(path));
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

bool platform::has_avx2()
{
	static const bool result = []
	{
#if defined(_M_X64) || defined(_M_IX86)
		int regs[4] = {};
		__cpuid(regs, 0);
		if (regs[0] < 7) return false;

		__cpuid(regs, 1);
		constexpr auto osxsave_bit = 1 << 27;
		constexpr auto avx_bit = 1 << 28;
		if ((regs[2] & osxsave_bit) == 0 || (regs[2] & avx_bit) == 0) return false;

		// XCR0 bits 1 and 2: without the OS saving XMM and YMM state the YMM registers are not
		// usable however the CPU reports itself.
		if ((_xgetbv(0) & 0x6) != 0x6) return false;

		__cpuidex(regs, 7, 0);
		constexpr auto avx2_bit = 1 << 5;
		return (regs[1] & avx2_bit) != 0;
#else
		return false;
#endif
	}();

	return result;
}

bool platform::memory_usage(memory_usage_t& result)
{
	PROCESS_MEMORY_COUNTERS_EX mem{};
	mem.cb = sizeof(mem);

	if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&mem), sizeof(mem)))
	{
		return false;
	}

	result.working_set = mem.WorkingSetSize;
	result.peak_working_set = mem.PeakWorkingSetSize;
	result.commit = mem.PrivateUsage;

	// Only QueryWorkingSet reports the per-page share state that separates private from shared.
	// The working set can grow between sizing the buffer and filling it, so the retry is bounded
	// rather than a loop that a busy process could keep alive.
	SYSTEM_INFO si{};
	GetSystemInfo(&si);
	const int64_t page_size = si.dwPageSize;
	std::vector<ULONG_PTR> pages(1u << 16);

	for (auto attempt = 0; attempt < 8; ++attempt)
	{
		if (QueryWorkingSet(GetCurrentProcess(), pages.data(),
		                    static_cast<DWORD>(pages.size() * sizeof(ULONG_PTR))))
		{
			const auto count = std::min(static_cast<size_t>(pages[0]), pages.size() - 1u);

			for (auto i = 1u; i <= count; ++i)
			{
				// PSAPI_WORKING_SET_BLOCK bit 8 is Shared.
				if (pages[i] & (1ull << 8)) result.shared_working_set += page_size;
				else result.private_working_set += page_size;
			}

			return true;
		}

		if (GetLastError() != ERROR_BAD_LENGTH) break;
		pages.resize(pages.size() * 2);
	}

	// The totals are still good even when the private/shared split is not.
	return true;
}

df::folder_path platform::temp_folder()
{
	wchar_t path[MAX_PATH + 1]{};
	const auto len = ::GetTempPath(MAX_PATH, path);

	// Zero means failure; a value above the buffer size means the path was not written at all.
	if (len == 0 || len > MAX_PATH)
	{
		return known_path(known_folder::app_cache_data);
	}

	return to_folder_path(path);
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
	wchar_t path_root[MAX_PATH]{};

	// SHBrowseForFolder cannot address a path longer than MAX_PATH, so an over-long initial
	// selection is dropped rather than overflowing the buffer (wcscpy_s would terminate the process).
	const auto initial_root = to_shell_path(path);

	if (initial_root.size() < std::size(path_root))
	{
		wcscpy_s(path_root, initial_root.c_str());
	}

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
		wchar_t sz[MAX_PATH]{};

		if (SHGetPathFromIDListW(pidl_result, sz))
		{
			path = df::folder_path(str::utf16_to_utf8(sz));
		}

		CoTaskMemFree(pidl_result);
	}

	return pidl_result != nullptr;
}


bool platform::prompt_for_save_path(df::file_path& path)
{
	OPENFILENAME ofn = {};

	wchar_t w[MAX_PATH];
	w[0] = 0;

	// GetSaveFileName is limited to MAX_PATH, so an over-long suggestion is dropped rather than
	// overflowing the buffer (wcscpy_s would terminate the process).
	const auto initial_name = to_shell_path(path);

	if (initial_name.size() < std::size(w))
	{
		wcscpy_s(w, initial_name.c_str());
	}
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
	path = df::file_path(str::utf16_to_utf8(w));
	return success;
}

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
		const bstr_t folder(to_shell_path(save_path).c_str());
		const bstr_t name(L"scan");

		hr = pWiaDevMgr2->GetImageDlg(0, nullptr, app_wnd(), folder, name, &num_files, &file_paths, &pItem);

		if (SUCCEEDED(hr) && num_files > 0 && file_paths)
		{
			result.saved_file_path = to_file_path(file_paths[0]);
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
				CoTaskMemFree(name);
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
						wchar_t sz[1024] = {};
						if (CertGetNameString(cert_context, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, sz, 1024) > 1)
						{
							result = sz;
						}

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
	constexpr std::string_view expected_signer = "Zachariah Walker";

	const auto path = platform::to_file_system_path(path_in);
	const auto cert_name = str::trim(str::utf16_to_utf8(read_cert_name(path)));

	// Pin updates to the author. A suffix match keeps certificate renewals working when the subject
	// carries a prefix such as "Open Source Developer Zachariah Walker", while a plain substring
	// match would also accept an unrelated signer whose name merely contains the author's name.
	const auto signer_matches = cert_name == expected_signer ||
		str::ends(cert_name, std::string(" ").append(expected_signer));

	if (!signer_matches)
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
	auto command_line = L"\""s + to_shell_path(installer_path) + L"\""s;
	if (silent) command_line += L" /S"s;
	if (run_app_after_install) command_line += L" /RR";
	command_line += L" /D="s + to_shell_path(destination_folder);

	const auto is_verified = verify_package(installer_path);
	const auto success = is_verified && run_command_line(to_shell_path(installer_path), command_line);

	file_op_result result;

	if (success)
	{
		result.code = file_op_result_code::OK;
	}
	else if (!is_verified)
	{
		// last_os_error() would report an unrelated stale error - nothing failed at the OS level.
		result.error_message = std::format("{} is not correctly signed", installer_path.name());
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
	// Any thread can stage a temp file, and files are staged beside the user's own files, so the seed
	// is randomised per process (a tick-based seed repeats across instances started in the same tick)
	// and the counter is atomic so two callers are never handed the same name.
	static const uint64_t seed = std::bit_cast<uint64_t>(df::now()) ^ (static_cast<uint64_t>(GetCurrentProcessId()) <<
		32) ^ GetTickCount64();
	static std::atomic<uint64_t> counter = seed;

	const auto base = folder.is_empty() ? temp_folder() : folder;
	df::file_path result;

	for (auto attempt = 0; attempt < 100; ++attempt)
	{
		const auto value = counter.fetch_add(1) + 1;

		auto name = "diffractor_"s;
		name += str::to_hex(std::bit_cast<const uint8_t*>(&value), 8);

		if (!str::is_empty(ext))
		{
			if (ext[0] != '.') name += '.';
			name += ext;
		}

		result = df::file_path(base, name);

		if (!result.exists())
		{
			return result;
		}
	}

	df::log(__FUNCTION__, std::format("could not find an unused temp name in {}", base.text()));
	return result;
}

void platform::set_desktop_wallpaper(const df::file_path file_path)
{
	const auto path = to_shell_path(file_path);

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
	const ComPtr<items_data_object> p = new items_data_object();
	p->cache(files, folders);
	SHMultiFileProperties(p.Get(), 0);
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

bool platform::burn_to_cd(const std::vector<df::file_path>& files, const std::vector<df::folder_path>& folders)
{
	record_feature_use(features::burn_to_disk);
	bool result = false;

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

						if (SUCCEEDED(spDropTarget->DragEnter(data.Get(), MK_LBUTTON, pt, &dwEffect)))
						{
							result = SUCCEEDED(spDropTarget->Drop(data.Get(), MK_LBUTTON, pt, &dwEffect));
						}
					}
				}
			}
		}
	}

	return result;
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

platform::clipboard_data_ptr platform::clipboard()
{
	ComPtr<IDataObject> pdo;

	// Fails while another process holds the clipboard; every accessor tolerates the resulting null object.
	if (const auto hr = OleGetClipboard(&pdo); FAILED(hr))
	{
		df::log(__FUNCTION__, std::format("OleGetClipboard failed: {:x}", static_cast<uint32_t>(hr)));
	}

	return std::make_shared<data_object_client>(pdo.Get());
}

void platform::set_clipboard(const std::vector<df::file_path>& files, const std::vector<df::folder_path>& folders,
                             const file_load_result& loaded, const bool is_move)
{
	const ComPtr<items_data_object> p = new items_data_object();
	p->set_for_move(is_move);
	p->cache(files, folders);
	p->cache(loaded);
	OleSetClipboard(p.Get());
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

std::string platform::clipboard_text()
{
	std::string result;
	if (!OpenClipboard(app_wnd())) return result;
	if (const auto data = GetClipboardData(CF_UNICODETEXT))
	{
		if (const auto text = static_cast<const wchar_t*>(GlobalLock(data)))
		{
			result = str::utf16_to_utf8(text);
			GlobalUnlock(data);
		}
	}
	CloseClipboard();
	return result;
}

platform::drop_effect platform::perform_drag(const std::any& frame_handle, const std::vector<df::file_path>& files,
                                             const std::vector<df::folder_path>& folders)
{
	const ComPtr<items_drop_source> source = new items_drop_source();
	const ComPtr<items_data_object> data = new items_data_object();
	data->cache(files, folders);

	DWORD result_effect = DROPEFFECT_NONE;
	const auto hr = DoDragDrop(data.Get(), source.Get(), DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK,
	                           &result_effect);
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
				if (DragQueryFileW(hdrop, i, path, std::size(path)))
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
				// The CIDA is a foreign buffer, so cidl and every offset are validated against the allocation.
				const auto cb = GlobalSize(medium.hGlobal);
				constexpr auto header = sizeof(UINT) * 2;

				if (cb >= header && pida->cidl <= (cb - header) / sizeof(UINT))
				{
					result.shell_id_list_count = static_cast<int>(pida->cidl);

					auto* const base = std::bit_cast<uint8_t*>(pida);

					if (pida->aoffset[0] < cb)
					{
						auto* const folder = std::bit_cast<PCIDLIST_ABSOLUTE>(base + pida->aoffset[0]);

						for (uint32_t i = 0; i < pida->cidl; ++i)
						{
							if (pida->aoffset[i + 1] >= cb) continue;

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

// Files deleted from network locations bypass the Recycle Bin, including when
// the share is exposed through a mapped drive letter.
static bool fs_path_can_recycle(std::wstring_view w)
{
	if (w.starts_with(L"\\\\?\\UNC\\")) return false; // extended-length UNC prefix
	if (w.starts_with(L"\\\\?\\")) w.remove_prefix(4); // strip extended-length prefix

	// A UNC path begins with two path separators (\\server\share).
	if (w.size() >= 2 && (w[0] == L'\\' || w[0] == L'/') && (w[1] == L'\\' || w[1] == L'/'))
	{
		return false;
	}

	if (w.size() >= 3 && w[1] == L':' && (w[2] == L'\\' || w[2] == L'/'))
	{
		const wchar_t root[]{w[0], L':', L'\\', L'\0'};
		if (GetDriveTypeW(root) == DRIVE_REMOTE) return false;
	}

	return true;
}

bool platform::can_recycle(const std::vector<df::file_path>& files, const std::vector<df::folder_path>& folders)
{
	for (const auto& f : files)
	{
		if (!fs_path_can_recycle(to_file_system_path(f))) return false;
	}

	for (const auto& f : folders)
	{
		if (!fs_path_can_recycle(to_file_system_path(f))) return false;
	}

	return true;
}

platform::file_op_result platform::move_or_copy(const std::vector<df::file_path>& files,
                                                const std::vector<df::folder_path>& folders,
                                                const df::folder_path target, const bool is_move,
                                                const bool replace_existing)
{
	const auto paths = all_file_system_paths(files, folders);
	const auto to = to_shell_path(target);

	// Auto-rename is the default because it cannot destroy anything. Replace is only reached when the
	// caller has already named the colliding files and had the overwrite confirmed, so the shell must
	// not ask a second time in its own vocabulary.
	const FILEOP_FLAGS flags = replace_existing
		                           ? static_cast<FILEOP_FLAGS>(FOF_NOCONFIRMATION)
		                           : static_cast<FILEOP_FLAGS>(FOF_RENAMEONCOLLISION | FOF_WANTMAPPINGHANDLE);

	SHFILEOPSTRUCT shfo = {
		app_wnd(),
		static_cast<uint32_t>(is_move ? FO_MOVE : FO_COPY),
		paths.c_str(),
		to.c_str(),
		flags,
		0, nullptr, nullptr
	};

	auto result = to_file_op_result(SHFileOperation(&shfo), shfo.fAnyOperationsAborted);
	std::vector<std::pair<std::wstring, std::wstring>> name_mappings;
	const auto* mappings = std::bit_cast<HANDLETOMAPPINGSW*>(shfo.hNameMappings);

	if (mappings)
	{
		name_mappings.reserve(mappings->uNumberOfMappings);
		for (auto i = 0u; i < mappings->uNumberOfMappings; ++i)
		{
			const auto& mapping = mappings->lpSHNameMapping[i];
			name_mappings.emplace_back(mapping.pszOldPath, mapping.pszNewPath);
		}
	}

	SHFreeNameMappings(shfo.hNameMappings);

	if (result.success())
	{
		const auto mapped_name = [&name_mappings](const std::wstring& source_path)
		{
			auto found = std::ranges::find_if(name_mappings, [&source_path](const auto& mapping)
			{
				return _wcsicmp(mapping.first.c_str(), source_path.c_str()) == 0;
			});

			if (found == name_mappings.end())
			{
				const auto separator = source_path.find_last_of(L"\\/");
				const auto source_name = source_path.c_str() +
					(separator == std::wstring::npos ? std::wstring::size_type{} : separator + 1);
				found = std::ranges::find_if(name_mappings, [source_name](const auto& mapping)
				{
					const auto mapping_separator = mapping.first.find_last_of(L"\\/");
					const auto mapping_name = mapping.first.c_str() +
						(mapping_separator == std::wstring::npos ? std::wstring::size_type{} : mapping_separator + 1);
					return _wcsicmp(mapping_name, source_name) == 0;
				});
			}

			if (found == name_mappings.end()) return std::wstring{};
			auto result = std::move(found->second);
			name_mappings.erase(found);
			return result;
		};
		const auto mapped_path = [&mapped_name](const df::file_path source, const df::file_path requested)
		{
			auto mapped = mapped_name(str::utf8_to_utf16(source.pack()));
			if (mapped.empty()) mapped = mapped_name(str::utf8_to_utf16(requested.pack()));
			return mapped.empty() ? requested : df::file_path(str::utf16_to_utf8(mapped));
		};
		const auto mapped_folder = [&mapped_name](const df::folder_path source, const df::folder_path requested)
		{
			auto mapped = mapped_name(str::utf8_to_utf16(source.text()));
			if (mapped.empty()) mapped = mapped_name(str::utf8_to_utf16(requested.text()));
			return mapped.empty() ? requested : df::folder_path(str::utf16_to_utf8(mapped));
		};

		for (const auto& file : files)
		{
			const auto requested = target.combine_file(file.name());
			result.created_files.files.emplace_back(mapped_path(file, requested));
		}

		for (const auto& folder : folders)
		{
			const auto requested = target.combine(folder.name());
			result.created_files.folders.emplace_back(mapped_folder(folder, requested));
		}
	}

	return result;
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
	fi.presence = platform::file_presence::found;
	fi.created = ft_to_ts(fad.ftCreationTime);
	fi.modified = ft_to_ts(fad.ftLastWriteTime);
	fi.size = fs_to_i64(fad.nFileSizeHigh, fad.nFileSizeLow);
	fi.is_readonly = 0 != (fad.dwFileAttributes & FILE_ATTRIBUTE_READONLY);
	fi.is_hidden = 0 != (fad.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN);
	fi.is_offline = 0 != is_offline_attribute(fad.dwFileAttributes);
}

static __forceinline void populate_file_attributes(platform::file_attributes_t& fi, const WIN32_FIND_DATA& fad)
{
	fi.presence = platform::file_presence::found;
	fi.created = ft_to_ts(fad.ftCreationTime);
	fi.modified = ft_to_ts(fad.ftLastWriteTime);
	fi.size = fs_to_i64(fad.nFileSizeHigh, fad.nFileSizeLow);
	fi.is_readonly = 0 != (fad.dwFileAttributes & FILE_ATTRIBUTE_READONLY);
	fi.is_hidden = 0 != (fad.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN);
	fi.is_offline = 0 != is_offline_attribute(fad.dwFileAttributes);
}

// Only these two codes prove the path is gone. Everything else - denied, offline, share unreachable,
// name too long - means the query failed, which is not the same claim.
static platform::file_presence presence_from_query_error(const DWORD error)
{
	return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
		       ? platform::file_presence::not_found
		       : platform::file_presence::unknown;
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
	else
	{
		result.presence = presence_from_query_error(GetLastError());
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
	else
	{
		result.presence = presence_from_query_error(GetLastError());
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
					if (can_descend_into_folder(fd.cFileName, fd.dwFileAttributes, show_hidden))
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

// ERROR_PATH_NOT_FOUND is also what an ejected volume or a dropped network mapping reports, so a
// folder only counts as deleted while its parent is still enumerable.
static bool parent_folder_is_available(const df::folder_path folder)
{
	if (folder.is_root())
	{
		return false;
	}

	const auto attributes = GetFileAttributesW(platform::to_file_system_path(folder.parent()).c_str());
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

platform::folder_contents platform::iterate_file_items(const df::folder_path folder, bool show_hidden)
{
	folder_contents results;
	WIN32_FIND_DATA fd;

	const auto root_path = to_file_system_path(folder);
	const auto file_search_path = root_path + L"\\*.*"s;
	auto* const files = FindFirstFileEx(file_search_path.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
	                                    FIND_FIRST_EX_LARGE_FETCH);

	// Read before the allocations below, which are not required to preserve the thread last error.
	const auto enumerate_error = GetLastError();

	results.files.reserve(256);
	results.folders.reserve(64);

	if (files != INVALID_HANDLE_VALUE)
	{
		results.success = true;
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

					if (test_offline_predicate &&
						test_offline_predicate(folder.combine_file(i.name)))
					{
						i.attributes.is_offline = true;
					}

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
		DWORD resume = 0;

		do // begin do
		{
			PSHARE_INFO_502 BufPtr = nullptr;
			DWORD er = 0, tr = 0;
			res = NetShareEnum(const_cast<wchar_t*>(path.c_str()), 502, std::bit_cast<LPBYTE*>(&BufPtr),
			                   MAX_PREFERRED_LENGTH, &er, &tr, &resume);

			if (res == ERROR_SUCCESS || res == ERROR_MORE_DATA)
			{
				results.success = true;
				auto* p = BufPtr;

				for (auto i = 1u; i <= er; i++)
				{
					if (STYPE_DISKTREE == p->shi502_type &&
						IsValidSecurityDescriptor(p->shi502_security_descriptor))
					{
						folder_info i;
						i.name = str::cache(str::utf16_to_utf8(p->shi502_netname));
						i.attributes.presence = file_presence::found;
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
	else if (enumerate_error == ERROR_FILE_NOT_FOUND ||
		(enumerate_error == ERROR_PATH_NOT_FOUND && parent_folder_is_available(folder)))
	{
		// The folder is genuinely gone, so an empty listing is the truth. Every other failure
		// (offline volume, denied access, network drop) leaves success false so callers keep
		// what they already know instead of treating the folder as emptied.
		results.success = true;
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
					if (recursive && can_descend_into_folder(fd.cFileName, fd.dwFileAttributes, show_hidden))
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

platform::file_op_result platform::write_shell_tags(const df::file_path path, const std::vector<std::string>& tags)
{
	const auto fail = [](const std::string_view step, const HRESULT hr)
	{
		return file_op_result{
			file_op_result_code::FAILED,
			std::format("{} failed: {}", step, format_os_error(static_cast<DWORD>(hr)))
		};
	};

	ComPtr<IPropertyStore> propStore;
	const auto w = to_shell_path(path);
	HRESULT hr = SHGetPropertyStoreFromParsingName(w.c_str(), nullptr, GPS_READWRITE, IID_PPV_ARGS(&propStore));

	if (FAILED(hr))
		return fail("SHGetPropertyStoreFromParsingName"sv, hr);

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
			return fail("InitPropVariantFromStringVector"sv, hr);
	}

	hr = propStore->SetValue(PKEY_Keywords, propVar);
	PropVariantClear(&propVar);

	if (FAILED(hr))
		return fail("IPropertyStore::SetValue"sv, hr);

	hr = propStore->Commit();

	if (FAILED(hr))
		return fail("IPropertyStore::Commit"sv, hr);

	return {file_op_result_code::OK};
}

platform::metadata_result platform::read_shell_metadata(const df::file_path path)
{
	metadata_result result;

	ComPtr<IPropertyStore> propStore;
	const auto w = to_shell_path(path);
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
