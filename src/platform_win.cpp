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

#include <psapi.h>
#include <Shlwapi.h>
#include <VersionHelpers.h>
#include <commdlg.h>
#include <intrin.h>
#include <wia.h>
#include <Thumbcache.h>
#include <shobjidl.h>   // SHGetPropertyStoreFromParsingName, etc
#include <propkey.h>    // PKEY_Music_AlbumArtist
#include <propvarutil.h>// InitPropVariantFromString, needs shlwapi.lib
#include <lm.h>
#include <WinIoCtl.h>
#include <Shellapi.h>
#include <Softpub.h>
#include <mapi.h>
#include <WinInet.h>
#include <ShlObj.h>


#include "app_text.h"
#include "files.h"
#include "model.h"
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
//#pragma comment(lib, "SetupAPI"sv)

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

void __cdecl debug_printf(const char8_t* fmt, ...)
{
	char buffer[256];
	va_list ap;

	va_start(ap, fmt);
	_vsnprintf_s(buffer, sizeof(buffer), std::bit_cast<const char*>(fmt), ap);
	OutputDebugStringA((buffer));
	va_end(ap);
}

bool platform::clipboard_has_files_or_image()
{
	return IsClipboardFormatAvailable(CF_HDROP) || IsClipboardFormatAvailable(CF_DIB);
}

std::u8string win32_to_string(const IID& iid)
{
	LPOLESTR sz = nullptr;
	auto result = u8"?"s;

	if (SUCCEEDED(StringFromIID(iid, &sz)))
	{
		result = str::utf16_to_utf8(sz);
		CoTaskMemFree(sz);
	}

	return result;
}

class items_data_object : public IDataObject, private df::no_copy
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
	bool _has_prefered_drop = false;

public:
	items_data_object() = default;
	~items_data_object() = default;

	void cache(df::file_path path);
	void cache(const file_load_result& loaded);
	void set_for_move(bool is_move);
	void cache(const std::vector<df::file_path>& files, const std::vector<df::folder_path>& folders);

	STDMETHOD(QueryInterface)(_In_ REFIID iid, _Deref_out_ void** ppvObject) noexcept override
	{
		df::trace(str::format(u8"items_data_object::QueryInterface {}"sv, win32_to_string(iid)));

		if (IsEqualGUID(iid, IID_IDataObject))
		{
			*ppvObject = static_cast<IDataObject*>(this);
			_refs += 1;
			return S_OK;
		}

		if (IsEqualGUID(iid, IID_IUnknown))
		{
			*ppvObject = static_cast<IUnknown*>(this);
			_refs += 1;
			return S_OK;
		}

		return E_NOINTERFACE;
	}

	STDMETHOD_(ULONG, AddRef)() noexcept override
	{
		return ++_refs;
	}

	STDMETHOD_(ULONG, Release)() noexcept override
	{
		const auto n = --_refs;
		if (n <= 0)
		{
			df::trace("items_data::release - delete"sv);
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

class items_drop_source : public IDropSource, private df::no_copy
{
	std::atomic_int _refs = 0;

public:
	using DROPEFFECT = DWORD;

	STDMETHOD(QueryInterface)(_In_ REFIID iid, _Deref_out_ void** ppvObject) noexcept override
	{
		df::trace(str::format(u8"items_drop_source::QueryInterface {}"sv, win32_to_string(iid)));

		if (IsEqualGUID(iid, IID_IDropSource))
		{
			*ppvObject = static_cast<IDropSource*>(this);
			_refs += 1;
			return S_OK;
		}

		if (IsEqualGUID(iid, IID_IUnknown))
		{
			*ppvObject = static_cast<IUnknown*>(this);
			_refs += 1;
			return S_OK;
		}

		return E_NOINTERFACE;
	}

	STDMETHOD_(ULONG, AddRef)() noexcept override
	{
		return ++_refs;
	}

	STDMETHOD_(ULONG, Release)() noexcept override
	{
		const auto n = --_refs;
		if (n <= 0)
		{
			df::trace("items_drop_source::release - delete"sv);
			delete this;
		}
		return n;
	}


	STDMETHODIMP QueryContinueDrag(BOOL bEscapePressed, DWORD dwKeyState) override
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


class CEnumFORMATETCImpl final : public IEnumFORMATETC, private df::no_copy
{
private:
	FORMATETCLIST _formats;
	size_t _cur = 0;
	std::atomic_int _refs = 1;

public:
	CEnumFORMATETCImpl(FORMATETCLIST ArrFE) : _formats(std::move(ArrFE))
	{
	}

	~CEnumFORMATETCImpl()
	{
	}

	STDMETHOD(QueryInterface)(_In_ REFIID iid, _Deref_out_ void** ppvObject) noexcept override
	{
		df::trace(str::format(u8"CEnumFORMATETCImpl::QueryInterface {}"sv, win32_to_string(iid)));

		if (IsEqualGUID(iid, IID_IEnumFORMATETC))
		{
			*ppvObject = static_cast<IEnumFORMATETC*>(this);
			_refs += 1;
			return S_OK;
		}

		if (IsEqualGUID(iid, IID_IUnknown))
		{
			*ppvObject = static_cast<IUnknown*>(this);
			_refs += 1;
			return S_OK;
		}

		return E_NOINTERFACE;
	}

	STDMETHOD_(ULONG, AddRef)() noexcept override
	{
		return ++_refs;
	}

	STDMETHOD_(ULONG, Release)() noexcept override
	{
		const auto n = --_refs;
		if (n <= 0)
		{
			df::trace("CEnumFORMATETCImpl::release - delete"sv);
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


STDMETHODIMP CEnumFORMATETCImpl::Next(ULONG celt, LPFORMATETC lpFormatEtc, ULONG* pceltFetched)
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

	return (cReturn == 0) ? S_OK : S_FALSE;
}

STDMETHODIMP CEnumFORMATETCImpl::Skip(ULONG celt)
{
	df::trace(__FUNCTION__);
	if ((_cur + static_cast<int>(celt)) >= _formats.size())
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

std::u8string platform::utf16_to_utf8(const std::wstring_view text)
{
	char result[100];
	memset(result, 0, sizeof(result));
	WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<uint32_t>(text.size()), result, 100, nullptr, nullptr);
	return str::utf8_cast2(result);
}

std::wstring platform::utf8_to_utf16(const std::u8string_view text)
{
	wchar_t result[100];
	memset(result, 0, sizeof(result));
	MultiByteToWideChar(CP_UTF8, 0, std::bit_cast<const char*>(text.data()), static_cast<uint32_t>(text.size()), result,
	                    100);
	return result;
}

std::string platform::utf8_to_a(const std::u8string_view utf8)
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

static bool is_folder(DWORD attributes)
{
	return (attributes != INVALID_FILE_ATTRIBUTES &&
		(attributes & FILE_ATTRIBUTE_DIRECTORY));
}

static bool is_offline_attribute(DWORD attributes)
{
	// Onedrive and GVFS use file attributes to denote files or directories that
	// may not be locally present and are only available "online". These files are applied one of
	// the two file attributes: FILE_ATTRIBUTE_RECALL_ON_OPEN or FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS.
	// When the attribute FILE_ATTRIBUTE_RECALL_ON_OPEN is set, skip the file during enumeration because the file
	// is not locally present at all. A file with FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS may be partially present locally.
	//
	// https://stackoverflow.com/questions/49301958/how-to-detect-onedrive-online-only-files
	//
	const auto offline_mask = FILE_ATTRIBUTE_OFFLINE |
		FILE_ATTRIBUTE_RECALL_ON_OPEN |
		FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS |
		FILE_ATTRIBUTE_VIRTUAL;

	return (attributes != INVALID_FILE_ATTRIBUTES) && (attributes & offline_mask) != 0;
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

static bool can_show_file(const wchar_t* name, DWORD attributes, bool show_hidden)
{
	if (str::is_empty(name)) return false;
	if (attributes == INVALID_FILE_ATTRIBUTES) return false;
	//if (attributes & FILE_ATTRIBUTE_OFFLINE) return false;
	if (!show_hidden && (attributes & FILE_ATTRIBUTE_HIDDEN) != 0) return false;
	return !is_folder(attributes) && !is_dots(name);
}

static bool can_show_folder(const wchar_t* name, DWORD attributes, bool show_hidden)
{
	if (str::is_empty(name)) return false;
	if (attributes == INVALID_FILE_ATTRIBUTES) return false;
	//if (attributes & FILE_ATTRIBUTE_OFFLINE) return false;
	if (!show_hidden && (attributes & FILE_ATTRIBUTE_HIDDEN) != 0) return false;
	return is_folder(attributes) && !is_dots(name);
}

static bool can_show_file_or_folder(const wchar_t* name, DWORD attributes, bool show_hidden)
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
	if (result.size() >= MAX_PATH) result.insert(0, L"\\\\?\\"sv);
	return result;
};

static std::wstring parse_special_path(const std::u8string_view sv)
{
	std::wstring result;

	PIDLIST_ABSOLUTE pidl = nullptr;
	const SFGAOF stSFGAOFIn = 0;
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

	if (result.size() >= MAX_PATH) result.insert(0, L"\\\\?\\"sv);
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

void items_data_object::set_for_move(bool is_move)
{
	_is_move = is_move;
	_has_prefered_drop = true;
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

	const auto cida_len = sizeof(CIDA) + ((paths.size() + 1) * sizeof(uint32_t));
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
	const wchar_t delim = 0;

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

		if (_has_prefered_drop && pformatetcIn->cfFormat == clipboard_formats::PREFERREDDROPEFFECT)
		{
			auto* const h = GlobalAlloc(GMEM_ZEROINIT | GMEM_MOVEABLE | GMEM_DDESHARE, sizeof(DWORD));

			if (h)
			{
				auto* const p = static_cast<DWORD*>(GlobalLock(h));
				*p = _is_move ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
				GlobalUnlock(h);

				pmedium->tymed = TYMED_HGLOBAL;
				pmedium->hGlobal = h;
				pmedium->pUnkForRelease = nullptr;

				return S_OK;
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

			// must e double null terminated
			df::assert_true(paths[paths.size() - 1] == 0);
			df::assert_true(paths[paths.size() - 2] == 0);

			const auto len = paths.size();
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

STDMETHODIMP items_data_object::EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC** ppenumFormatEtc)
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

	if (_has_prefered_drop && _is_move)
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
			df::log(__FUNCTION__, str::format(u8"EnumFormatEtc "sv, str::utf16_to_utf8(szBuf)));
		}
		else
		{
			df::log(__FUNCTION__, "EnumFormatEtc (Unknown)"sv);
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

DWORD data_object_client::prefered_drop_effect() const
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

static std::u8string format_os_error(const DWORD error)
{
	wchar_t sz[1000];
	FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, g_lLangId, sz, 1000,
	               nullptr);
	auto result = str::utf16_to_utf8(sz);
	return result.empty() ? std::u8string(tt.error_unknown) : result;
}

static std::u8string last_os_error_impl()
{
	std::u8string result;
	const auto error = GetLastError();

	if (error != 0)
	{
		result = format_os_error(error);
	}

	return result;
}

static std::u8string shell_error_string(int e)
{
	switch (e)
	{
	case 0x71: return std::u8string(tt.error_same_file);
	case 0x72: return std::u8string(tt.error_many_src_1_dest);
	case 0x73: return std::u8string(tt.error_diff_dir);
	case 0x74: return std::u8string(tt.error_src_root_dir);
	case 0x75: return std::u8string(tt.error_op_cancelled);
	case 0x76: return std::u8string(tt.error_dest_subtree);
	case 0x78: return std::u8string(tt.error_access_denied_src);
	case 0x79: return std::u8string(tt.error_path_too_deep);
	case 0x7A: return std::u8string(tt.error_many_dest);
	case 0x7C: return std::u8string(tt.error_invalid_files);
	case 0x7D: return std::u8string(tt.error_dest_same_tree);
	case 0x7E: return std::u8string(tt.error_fld_dest_is_file);
	case 0x80: return std::u8string(tt.error_file_dest_is_fld);
	case 0x81: return std::u8string(tt.error_filename_too_long);
	case 0x82: return std::u8string(tt.error_dest_is_cd_rom);
	case 0x83: return std::u8string(tt.error_dest_is_dvd);
	case 0x84: return std::u8string(tt.error_dest_is_cd_record);
	case 0x85: return std::u8string(tt.error_file_too_large);
	case 0x86: return std::u8string(tt.error_src_is_cdrom);
	case 0x87: return std::u8string(tt.error_src_is_dvd);
	case 0x88: return std::u8string(tt.error_src_is_cd_record);
	case 0xB7: return std::u8string(tt.error_max);
	case 0x402: return std::u8string(tt.error_unknown);
	case 0x10000: return std::u8string(tt.error_on_dest);
	case 0x10074: return std::u8string(tt.error_dst_root_dir);
	default:
		return format_os_error(e);
	}
}

platform::file_op_result to_file_op_result(int res, const BOOL fAnyOperationsAborted = 0)
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

static df::paths dest_file_list(const df::folder_path target, const char8_t* const file_list,
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
				auto src_path = df::file_path(std::u8string_view(sz_start, sz - sz_start - 1));
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
		is_move = prefered_drop_effect() == DROPEFFECT_MOVE;
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
				result = df::file_path(std::bit_cast<const char8_t*>(std::bit_cast<uint8_t*>(pDrop) + pDrop->pFiles));
			}

			GlobalUnlock(h);
		}

		ReleaseStgMedium(&stgMedium);
	}

	return result;
}


static platform::drop_effect to_drop_effect(DWORD dwEffect)
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
				const auto* sz = std::bit_cast<LPCWSTR>(std::bit_cast<const char8_t*>(pDrop) + pDrop->pFiles);
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
				result.has_readonly |= (file_attributes(df::folder_path(std::bit_cast<const char8_t*>(sz))) &
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

	result.prefered_drop_effect = to_drop_effect(prefered_drop_effect());
	return result;
}


platform::file_op_result data_object_client::save_bitmap(const df::folder_path save_path, const std::u8string_view name,
                                                         const bool as_png)
{
	platform::file_op_result result;
	STGMEDIUM stgMedium;

	const HRESULT hr = _pData->GetData(&clipboard_formats::Bitmap, &stgMedium);

	if (SUCCEEDED(hr))
	{
		result = save_bitmap_info(save_path, name, as_png, stgMedium.hBitmap);

		GlobalUnlock(stgMedium.hBitmap);
		ReleaseStgMedium(&stgMedium);
	}

	return result;
}


void* platform::memory_pool::alloc(size_t size)
{
	static std::bad_alloc OOM;
	exclusive_lock lock(cs);

	const auto align_size = size = ((size + (alignment - 1)) / alignment) * alignment; // Align size

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

bool platform::sse2_supported = ::IsProcessorFeaturePresent(PF_XMMI64_INSTRUCTIONS_AVAILABLE) != 0;
bool platform::crc32_supported = ::IsProcessorFeaturePresent(PF_SSE4_2_INSTRUCTIONS_AVAILABLE) != 0;
bool platform::avx2_supported = ::IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE) != 0;
bool platform::avx512_supported = ::IsProcessorFeaturePresent(PF_AVX512F_INSTRUCTIONS_AVAILABLE) != 0;
bool platform::neon_supported = ::IsProcessorFeaturePresent(PF_ARM_NEON_INSTRUCTIONS_AVAILABLE) != 0;


std::u8string platform::OS()
{
#pragma warning(push)
#pragma warning(disable:4996)
	OSVERSIONINFO osvi;
	ZeroMemory(&osvi, sizeof(OSVERSIONINFO));
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

void platform::trace(const std::u8string_view message)
{
	trace(std::u8string(message));
}

void platform::trace(const std::u8string& message)
{
	OutputDebugStringA(std::bit_cast<const char*>(message.c_str()));
}

void platform::set_thread_description(const std::u8string_view name)
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


std::u8string platform::user_name()
{
	const int size = 200;
	wchar_t w[size];
	DWORD len = size;
	GetUserName(w, &len);

	return str::utf16_to_utf8(w);
}

std::u8string platform::last_os_error()
{
	return last_os_error_impl();
}

static platform::file_op_result last_op_result(BOOL res)
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
                                             bool fail_if_exists)
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
			const auto org_name = std::u8string(destination.file_name_without_extension()) + u8".original"s;
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

		return last_op_result(ReplaceFileW(
			destinationW.c_str(),
			existingW.c_str(),
			backup.is_empty() ? nullptr : backupW.c_str(),
			REPLACEFILE_IGNORE_MERGE_ERRORS | REPLACEFILE_IGNORE_ACL_ERRORS,
			nullptr,
			nullptr) != 0);
	}

	return move_file(existing, destination, true);
}

bool platform::exists(const df::folder_path path)
{
	const auto attrib = ::file_attributes(path);

	return (attrib != INVALID_FILE_ATTRIBUTES &&
		(attrib & FILE_ATTRIBUTE_DIRECTORY));
}

bool platform::exists(const df::file_path path)
{
	const auto attrib = ::file_attributes(path);

	return (attrib != INVALID_FILE_ATTRIBUTES &&
		(attrib & FILE_ATTRIBUTE_DIRECTORY) == 0);
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
	return ShellExecute(app_wnd(), L"open", w.c_str(), L"", L"", SW_SHOWNORMAL) > std::bit_cast<HINSTANCE>(
		static_cast<uintptr_t>(32));
}

bool platform::open(const std::u8string_view path)
{
	const auto w = str::utf8_to_utf16(path);
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

bool platform::run(const std::u8string_view cmd)
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

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
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

void platform::show_text_in_notepad(const std::u8string_view s)
{
	TCHAR szCmdline[] = TEXT("Notepad.exe");

	PROCESS_INFORMATION piProcInfo;
	STARTUPINFO siStartInfo;
	ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));
	ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
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
		//df::log(__FUNCTION__, piProcInfo.dwProcessId << " Notepad Process Id"sv;

		WaitForInputIdle(piProcInfo.hProcess, 1000);

		pidandhwnd pnh;
		pnh.dwProcessId = piProcInfo.dwProcessId;
		pnh.hwnd = nullptr;

		EnumDesktopWindows(nullptr, EnumWindowsProc, (LPARAM)&pnh);

		if (pnh.hwnd != nullptr)
		{
			const int ControlId = 15; // Edit control in Notepad
			auto* hEditWnd = GetDlgItem(pnh.hwnd, ControlId);

			if (!hEditWnd)
			{
				hEditWnd = FindWindowEx(pnh.hwnd, nullptr, L"scintilla", nullptr);

				if (!hEditWnd)
				{
					SendMessage(hEditWnd, WM_SETTEXT, NULL, (LPARAM)std::u8string(s).c_str());
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
	run_explorer(to_file_system_path(path));
}

void platform::show_in_file_browser(const df::folder_path path)
{
	run_explorer(to_file_system_path(path));
}

int platform::display_frequency()
{
	DEVMODE dm;
	memset(&dm, 0, sizeof(DEVMODE));
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
	OPENFILENAME ofn;
	memset(&ofn, 0, sizeof(ofn));

	wchar_t w[MAX_PATH];
	w[0] = 0;
	wcscpy_s(w, to_file_system_path(path).c_str());
	const auto extension = str::utf8_to_utf16(path.extension());

	std::u8string filter_a;
	filter_a += str::format(u8"{} (*.jpg)|*.jpg;*.jpe;*.jpeg|"sv, tt.jpeg_best);
	filter_a += str::format(u8"{} (*.png)|*.png|"sv, tt.png_best);
	filter_a += str::format(u8"{} (*.webp)|*.webp|"sv, tt.webp_best);
	filter_a += u8"|"sv;

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
	if (str::icmp(extension, L".png"sv) == 0) ofn.nFilterIndex = 2;
	if (str::icmp(extension, L".webp"sv) == 0) ofn.nFilterIndex = 3;

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
		const bstr_t name(L"scan"sv);

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
					value += L"sv, "sv;
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
		const GETPROPERTYSTOREFLAGS flags = GPS_DEFAULT;
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
	IThumbnailStreamProvider : public IUnknown
{
public:
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
	const auto result = get_cached_file_properties_response::fail;
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
		if (drives & (1 << i))
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

				if (scan_contents)
				{
					// TODO
				}

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
									text += L"\n"sv;
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

std::vector<platform::open_with_entry> platform::assoc_handlers(const std::u8string_view ext)
{
	ComPtr<IEnumAssocHandlers> handle_enum;
	df::hash_map<std::u8string, open_with_entry, df::ihash, df::ieq> handlers;
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

std::u8string platform::resource_url(const resource_item map_html)
{
	const auto app_path = running_app_path();
	return str::format(u8"res://{}/{}"sv, app_path, IDR_MAP);
}


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

	if (str::icmp(cert_name, L"Zachariah Walker"sv) != 0)
		return false;

	WINTRUST_FILE_INFO FileData = {sizeof(WINTRUST_FILE_INFO)};
	FileData.pcwszFilePath = path.c_str();

	WINTRUST_DATA WinTrustData = {sizeof(WinTrustData)};
	WinTrustData.dwUIChoice = WTD_UI_NONE;
	WinTrustData.fdwRevocationChecks = WTD_REVOKE_NONE;
	WinTrustData.dwUnionChoice = WTD_CHOICE_FILE;
	WinTrustData.dwProvFlags = WTD_SAFER_FLAG;
	WinTrustData.pFile = &FileData;

	GUID WVTPolicyGUID = WINTRUST_ACTION_GENERIC_VERIFY_V2;
	return WinVerifyTrust(app_wnd(), &WVTPolicyGUID, &WinTrustData) == ERROR_SUCCESS;
}

void platform::download_and_verify(const bool test_version, const std::function<void(df::file_path)>& complete)
{
	const auto download_path = temp_file(u8"exe"sv);

	web_request req;
	req.host = u8"diffractor.com"sv;
	req.path = test_version ? u8"diffractor-setup-test.exe"sv : u8"diffractor-setup.exe"sv;
	req.download_file_path = download_path;

	const auto response = send_request(req);

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
                                           bool silent, bool run_app_after_install)
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


df::file_path platform::temp_file(const std::u8string_view ext, const df::folder_path folder)
{
	static auto counter = GetTickCount64();
	counter += 1;

	auto name = u8"diffractor_"s;
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
		const WALLPAPEROPT options = {sizeof(WALLPAPEROPT), WPSTYLE_CENTER};
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

HRESULT GetPIDLFromPath(LPCWSTR pszPath, __out PIDLIST_ABSOLUTE* ppidl) noexcept
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
			spBurn->GetRecorderDriveLetter(szDefaultBurnerPath, static_cast<uint32_t>(std::size(szDefaultBurnerPath)))))
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

						const POINTL pt = {0};
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

		const POINTL pt = {0, 0};
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

		const POINTL pt = {0, 0};
		DWORD dwEffect = DROPEFFECT_LINK | DROPEFFECT_MOVE | DROPEFFECT_COPY;

		spDropTarget->DragEnter(data.Get(), MK_LBUTTON, pt, &dwEffect);
		spDropTarget->Drop(data.Get(), MK_LBUTTON, pt, &dwEffect);
	}
}

bool number_format_invalid = true;

static constexpr LCID default_locale = LOCALE_USER_DEFAULT;
static char decimal_sep[10];
static char thousand_sep[10];
static NUMBERFMTA fmt;

void validate_number_format()
{
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

std::u8string platform::format_number(const std::u8string& num_text)
{
	validate_number_format();

	static constexpr int size = 64;
	char result[size] = {0};
	GetNumberFormatA(default_locale, 0, std::bit_cast<const char*>(num_text.c_str()), &fmt, result, size);
	return str::utf8_cast2(result);
}

std::u8string platform::number_dec_sep()
{
	validate_number_format();
	return str::utf8_cast2(decimal_sep);
}

class file_impl : public platform::file
{
	HANDLE _h = INVALID_HANDLE_VALUE;
public:
	file_impl(HANDLE file) : _h(file)
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
		const LARGE_INTEGER offset = {0, 0};
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

	const auto desired_access = GENERIC_READ;
	const auto share_mode = FILE_SHARE_READ;
	const auto creation_disposition = OPEN_EXISTING;
	const auto flags_and_attributes = FILE_FLAG_SEQUENTIAL_SCAN;
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
		const uint32_t max_chunk = df::two_fifty_six_k;
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

	const auto shareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;
	const auto accessMode = GENERIC_WRITE | GENERIC_READ;
	const auto vol = u8"\\\\.\\"s + path.text()[0] + path.text()[1];
	const auto vol_w = str::utf8_to_utf16(vol);

	auto* hDevice = CreateFile(vol_w.c_str(), accessMode, shareMode, nullptr, OPEN_EXISTING, 0, nullptr);

	if (hDevice == INVALID_HANDLE_VALUE)
	{
		df::log(__FUNCTION__, str::format(u8"IOCTL_STORAGE_EJECT_MEDIA: {}"sv, last_os_error()));
		return false;
	}

	res = DeviceIoControl(hDevice, FSCTL_DISMOUNT_VOLUME, nullptr, 0, nullptr, 0, &returned, nullptr);

	if (!res)
	{
		df::log(__FUNCTION__, str::format(u8"FSCTL_DISMOUNT_VOLUME: {}"sv, last_os_error()));
	}

	PREVENT_MEDIA_REMOVAL PMRBuffer;
	PMRBuffer.PreventMediaRemoval = FALSE;

	res = DeviceIoControl(hDevice, IOCTL_STORAGE_MEDIA_REMOVAL, &PMRBuffer, sizeof(PREVENT_MEDIA_REMOVAL), nullptr, 0,
	                      &returned, nullptr);

	if (!res)
	{
		df::log(__FUNCTION__, str::format(u8"IOCTL_STORAGE_MEDIA_REMOVAL: {}"sv, last_os_error()));
	}

	res = DeviceIoControl(hDevice, IOCTL_STORAGE_EJECT_MEDIA, nullptr, 0, nullptr, 0, &returned, nullptr);

	if (!res)
	{
		df::log(__FUNCTION__, str::format(u8"IOCTL_STORAGE_EJECT_MEDIA: {}"sv, last_os_error()));
	}

	res = CloseHandle(hDevice);

	return res != 0;
}

bool platform::is_server(const std::u8string_view path)
{
	static std::regex e(R"(^[\\/]*([^\\\/]+)[\\\/]*$)");
	std::match_results<std::u8string_view::const_iterator> m;
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

void platform::set_clipboard(const std::u8string& text)
{
	if (OpenClipboard(app_wnd()))
	{
		EmptyClipboard();

		const auto w = str::utf8_to_utf16(text);
		const auto text_size = w.size();
		auto* const hglbCopy = GlobalAlloc(GMEM_MOVEABLE, (text_size + 1) * sizeof(wchar_t));

		if (hglbCopy)
		{
			auto* const text_copy = static_cast<wchar_t*>(GlobalLock(hglbCopy));

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

		CloseClipboard();
	}
}

platform::drop_effect platform::perform_drag(std::any frame_handle, const std::vector<df::file_path>& files,
                                             const std::vector<df::folder_path>& folders)
{
	auto* source = new items_drop_source();
	auto* data = new items_data_object();
	data->cache(files, folders);

	DWORD result_effect = DROPEFFECT_NONE;
	const auto hr = DoDragDrop(data, source, DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK, &result_effect);
	::PostMessage(std::any_cast<HWND>(frame_handle), WM_LBUTTONUP, 0, 0);
	return (DRAGDROP_S_DROP == hr) ? to_drop_effect(result_effect) : drop_effect::none;
}

std::u8string platform::file_op_result::format_error(const std::u8string_view text,
                                                     const std::u8string_view more_text) const
{
	std::u8string result;
	result = text;

	if (!more_text.empty())
	{
		str::join(result, more_text, u8"\n"sv, false);
	}

	if (error_message.empty())
	{
		if (more_text.empty())
		{
			str::join(result, tt.error_unknown, u8"\n"sv, false);
		}
	}
	else
	{
		str::join(result, error_message, u8"\n"sv, false);
	}

	return result;
}

platform::file_op_result platform::delete_items(const std::vector<df::file_path>& files,
                                                const std::vector<df::folder_path>& folders, bool allow_undo)
{
	const auto paths = all_file_system_paths(files, folders);

	SHFILEOPSTRUCT shfo;
	memset(&shfo, 0, sizeof(shfo));
	shfo.hwnd = app_wnd();
	shfo.pFrom = paths.c_str();
	shfo.wFunc = FO_DELETE;
	shfo.fFlags = FOF_NOCONFIRMATION | FOF_SILENT | (allow_undo ? FOF_ALLOWUNDO : 0);

	return to_file_op_result(SHFileOperation(&shfo), shfo.fAnyOperationsAborted);
}

platform::file_op_result platform::move_or_copy(const std::vector<df::file_path>& files,
                                                const std::vector<df::folder_path>& folders,
                                                const df::folder_path target, bool is_move)
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

static bool folder_exists(const std::u8string_view path)
{
	if (!df::is_path(path)) return false;

	const auto attrib = file_attributes(df::folder_path(path));

	return (attrib != INVALID_FILE_ATTRIBUTES &&
		(attrib & FILE_ATTRIBUTE_DIRECTORY));
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

static df::folder_path dropbox(const std::u8string_view sub_folder)
{
	const auto dropbox = path_from_csidl(CSIDL_PROFILE).combine(u8"Dropbox"sv);
	if (dropbox.exists()) return dropbox;

	const auto info_path = path_from_csidl(CSIDL_LOCAL_APPDATA).combine(u8"Dropbox"sv).combine_file(u8"info.json"sv);

	if (info_path.exists())
	{
		const auto info = df::util::json::json_from_file(info_path);

		if (info.HasMember(u8"personal"))
		{
			const df::folder_path personal_path(df::util::json::safe_string(info[u8"personal"], u8"path"));
			if (personal_path.exists())
			{
				const auto result = personal_path.combine(sub_folder);

				if (result.exists())
				{
					return result;
				}
			}
		}

		if (info.HasMember(u8"business"))
		{
			const df::folder_path business_path(df::util::json::safe_string(info[u8"business"], u8"path"));

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
		result = path_from_csidl(CSIDL_PROFILE).combine(u8"OneDrive"sv);
	}

	return result;
}

static df::folder_path onedrive(const std::u8string_view sub_folder, const std::u8string_view sub_folder2 = {})
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
	case known_folder::test_files_folder: return running_app_path().folder().combine(u8"test"sv);
	case known_folder::app_data: return app_data();
	case known_folder::downloads: return shell_known_folder(FOLDERID_Downloads);
	case known_folder::pictures: return path_from_csidl(CSIDL_MYPICTURES);
	case known_folder::video: return path_from_csidl(CSIDL_MYVIDEO);
	case known_folder::music: return path_from_csidl(CSIDL_MYMUSIC);
	case known_folder::documents: return path_from_csidl(CSIDL_MYDOCUMENTS);
	case known_folder::desktop: return path_from_csidl(CSIDL_DESKTOP);
	case known_folder::dropbox_photos: return dropbox(u8"photos"sv);
	case known_folder::onedrive_pictures: return onedrive(u8"pictures"sv);
	case known_folder::onedrive_video: return onedrive(u8"video"sv);
	case known_folder::onedrive_music: return onedrive(u8"music"sv);
	case known_folder::onedrive_camera_roll: return onedrive(u8"pictures"sv, u8"Camera Roll"sv);
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

std::u8string platform::user_language()
{
	wchar_t sz[17];
	int ccBuf = GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SISO639LANGNAME, sz, 8) - 1;
	sz[ccBuf++] = '_';
	ccBuf += GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SISO3166CTRYNAME, sz + ccBuf, 8) - 1;
	return str::utf16_to_utf8({sz, static_cast<size_t>(ccBuf)});
}

bool platform::mapi_send(const std::u8string_view to, const std::u8string_view subject, const std::u8string_view text,
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
	//::LoadLibrary(L"MAPI32.DLL"sv);
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

			MapiRecipDescW recipients;
			memset(&recipients, 0, sizeof(recipients));
			recipients.ulRecipClass = MAPI_TO;
			recipients.lpszAddress = const_cast<LPWSTR>(to_w.c_str());
			recipients.lpszName = recipients.lpszAddress;

			MapiMessageW message_w;
			memset(&message_w, 0, sizeof(message_w));

			std::vector<MapiFileDescW> files;
			std::vector<std::pair<std::wstring, std::wstring>> attachments_w;

			for (const auto& a : attachments)
			{
				attachments_w.emplace_back(str::utf8_to_utf16(a.first), str::utf8_to_utf16(a.second.str()));
			}

			for (const auto& a : attachments_w)
			{
				MapiFileDescW fd;
				memset(&fd, 0, sizeof(MapiFileDesc));

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
			auto to_s = std::u8string(to);
			auto subject_s = std::u8string(subject);
			auto text_s = std::u8string(text);

			MapiRecipDesc recipients;
			memset(&recipients, 0, sizeof(recipients));
			recipients.ulRecipClass = MAPI_TO;
			recipients.lpszAddress = const_cast<LPSTR>(std::bit_cast<const char*>(to_s.c_str()));
			recipients.lpszName = recipients.lpszAddress;

			MapiMessage message_a;
			memset(&message_a, 0, sizeof(message_a));

			std::vector<MapiFileDesc> files;
			std::vector<std::pair<std::u8string, std::u8string>> attachments_a;

			for (const auto& a : attachments)
			{
				attachments_a.emplace_back(a.first, a.second.str());
			}

			for (const auto& a : attachments_a)
			{
				MapiFileDesc fd;
				memset(&fd, 0, sizeof(MapiFileDesc));

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

	bool IsType(_In_ VARTYPE type) const
	{
		return vt == type;
	}

	PROPVARIANT* operator &()
	{
		return this;
	}
};


static void confirm(HRESULT hr, const std::u8string_view context)
{
	if (FAILED(hr))
	{
		throw app_exception(str::format(u8"{} hr={:x}"sv, context, hr));
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
			throw app_exception(str::format(u8"Cannot read file into memory ({} bytes)"sv, file_len));
		}

		return f.read_blob(file_len);
	}

	return {};
}

static uint64_t fs_to_i64(DWORD nFileSizeHigh, DWORD nFileSizeLow)
{
	return static_cast<__int64>(nFileSizeHigh) << 32 | nFileSizeLow;
}

uint64_t ft_to_ts(const FILETIME& ft)
{
	return static_cast<__int64>(ft.dwHighDateTime) << 32 | ft.dwLowDateTime;
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

std::u8string platform::format_date_time(const df::date_t d)
{
	const LCID locale = LOCALE_USER_DEFAULT;
	const int size = 128;
	wchar_t sz_date[size] = {0};
	wchar_t sz_time[size] = {0};
	SYSTEMTIME st;
	const DWORD flags = DATE_SHORTDATE;
	const auto ft = ts_to_ft(d._i);

	if (d.is_valid())
	{
		if (FileTimeToSystemTime(&ft, &st))
		{
			GetDateFormatW(locale, flags, &st, nullptr, sz_date, size);
			GetTimeFormatW(locale, 0, &st, nullptr, sz_time, size);

			return str::format(u8"{} {}"sv, str::utf16_to_utf8(sz_date), str::utf16_to_utf8(sz_time));
		}
	}

	return {};
}

std::u8string platform::format_date(const df::date_t d)
{
	const LCID locale = LOCALE_USER_DEFAULT;
	const int size = 128;
	wchar_t sz[size] = {0};
	SYSTEMTIME st;
	const DWORD flags = DATE_SHORTDATE;
	const auto ft = ts_to_ft(d._i);

	if (d.is_valid() &&
		FileTimeToSystemTime(&ft, &st) &&
		GetDateFormatW(locale, flags, &st, nullptr, sz, size))
	{
		return str::utf16_to_utf8(sz);
	}

	return {};
}

std::u8string platform::format_time(const df::date_t d)
{
	const LCID locale = LOCALE_USER_DEFAULT;
	const int size = 128;
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
	SYSTEMTIME st;
	ZeroMemory(&st, sizeof(st));
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
	return (pc.QuadPart * 1000) / tps.QuadPart;
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

bool platform::set_files_dates(df::file_path path, const uint64_t dt_created, const uint64_t dt_modified)
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
	const auto show_hidden = true;
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

std::vector<platform::folder_info> platform::select_folders(const df::item_selector& selector, bool show_hidden)
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

std::vector<platform::file_info> platform::select_files(const df::item_selector& selector, bool show_hidden)
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

class setting_file_impl : public platform::setting_file
{
	mutable df::hash_map<std::u8string, HKEY__*> _keys;
	HKEY _root_key = nullptr;
	bool _root_created = false;

public:
	setting_file_impl()
	{
		constexpr std::u8string_view s_reg_key = u8"Software\\Diffractor"sv;
		_root_key = create_key(HKEY_CURRENT_USER, s_reg_key, _root_created);
	}

	~setting_file_impl()
	{
		close();
	}

	bool root_created() const override
	{
		return _root_created;
	}


	HKEY Key(const std::u8string_view section_in) const
	{
		if (str::is_empty(section_in))
		{
			return _root_key;
		}

		const auto section = std::u8string(section_in);
		const auto found = _keys.find(section);

		if (found != _keys.end())
		{
			return found->second;
		}

		auto was_created = false;
		auto* const new_key = create_key(_root_key, section, was_created);

		if (new_key != nullptr)
		{
			_keys[section] = new_key;
			return new_key;
		}

		return nullptr;
	}

	void close()
	{
		for (const auto& k : _keys)
		{
			RegCloseKey(k.second);
		}

		_keys.clear();
		RegCloseKey(_root_key);
		_root_key = nullptr;
	}

	static HKEY create_key(HKEY parent_key, const std::u8string_view name, bool& was_created)
	{
		df::assert_true(parent_key != nullptr);

		DWORD disposition = 0;
		HKEY result_key = nullptr;
		const auto result = RegCreateKeyEx(parent_key, str::utf8_to_utf16(name).c_str(), 0, REG_NONE,
		                                   REG_OPTION_NON_VOLATILE,
		                                   KEY_ALL_ACCESS, nullptr, &result_key, &disposition);
		was_created = disposition == REG_CREATED_NEW_KEY;
		return (result == ERROR_SUCCESS) ? result_key : nullptr;
	}

	bool read(const std::u8string_view section, const std::u8string_view name, uint32_t& v) const override
	{
		DWORD dwType = 0;
		DWORD s = sizeof(uint32_t);
		const int32_t result = RegQueryValueEx(Key(section), str::utf8_to_utf16(name).c_str(), nullptr, &dwType,
		                                       std::bit_cast<uint8_t*>(&v), &s);
		df::assert_true((result != ERROR_SUCCESS) || (dwType == REG_DWORD));
		df::assert_true((result != ERROR_SUCCESS) || (s == sizeof(uint32_t)));
		return ERROR_SUCCESS == result;
	}

	bool read(const std::u8string_view section, const std::u8string_view name, uint64_t& v) const override
	{
		DWORD dwType = 0;
		DWORD s = sizeof(uint64_t);
		const int32_t result = RegQueryValueEx(Key(section), str::utf8_to_utf16(name).c_str(), nullptr, &dwType,
		                                       std::bit_cast<uint8_t*>(&v), &s);
		df::assert_true((result != ERROR_SUCCESS) || (dwType == REG_QWORD));
		df::assert_true((result != ERROR_SUCCESS) || (s == sizeof(uint64_t)));
		return ERROR_SUCCESS == result;
	}


	bool read(const std::u8string_view section, const std::u8string_view name, std::u8string& v) const override
	{
		DWORD alloc_len = 0;
		DWORD type = 0;
		const auto nameW = str::utf8_to_utf16(name);

		int32_t result = RegQueryValueEx(Key(section), nameW.c_str(), nullptr, &type, nullptr, &alloc_len);

		if (result == ERROR_SUCCESS && (type == REG_SZ || type == REG_MULTI_SZ || type == REG_EXPAND_SZ))
		{
			std::vector<uint8_t> data(alloc_len, 0);
			result = RegQueryValueEx(Key(section), nameW.c_str(), nullptr, &type, data.data(), &alloc_len);

			if (result == ERROR_SUCCESS)
			{
				const auto char_len = alloc_len >= sizeof(wchar_t) ? (alloc_len / sizeof(wchar_t)) - 1 : 0;
				v = str::utf16_to_utf8({std::bit_cast<const wchar_t*>(data.data()), char_len});
			}
		}

		return ERROR_SUCCESS == result;
	}

	bool read(const std::u8string_view section, const std::u8string_view name, uint8_t* data,
	          size_t& len) const override
	{
		DWORD dwType = REG_BINARY;
		DWORD dwSize = static_cast<uint32_t>(len);
		bool success = false;

		if (ERROR_SUCCESS == RegQueryValueEx(Key(section), str::utf8_to_utf16(name).c_str(), nullptr, &dwType, data,
		                                     &dwSize))
		{
			len = dwSize;
			success = true;
		}

		return success;
	}

	bool write(const std::u8string_view section, const std::u8string_view name, const uint32_t v) override
	{
		return ERROR_SUCCESS == RegSetValueEx(Key(section), str::utf8_to_utf16(name).c_str(), 0, REG_DWORD,
		                                      std::bit_cast<const uint8_t*>(&v), sizeof(uint32_t));
	}

	bool write(const std::u8string_view section, const std::u8string_view name, const uint64_t v) override
	{
		return ERROR_SUCCESS == RegSetValueEx(Key(section), str::utf8_to_utf16(name).c_str(), 0, REG_QWORD,
		                                      std::bit_cast<const uint8_t*>(&v), sizeof(uint64_t));
	}

	bool write(const std::u8string_view section, const std::u8string_view name, const std::u8string_view v) override
	{
		const auto w = str::utf8_to_utf16(v);
		return ERROR_SUCCESS == RegSetValueEx(Key(section), str::utf8_to_utf16(name).c_str(), 0, REG_SZ,
		                                      std::bit_cast<const uint8_t*>(w.c_str()),
		                                      static_cast<uint32_t>(w.size() * sizeof(wchar_t)));
	}

	bool write(const std::u8string_view section, const std::u8string_view name, const df::cspan data) override
	{
		return ERROR_SUCCESS == RegSetValueEx(Key(section), str::utf8_to_utf16(name).c_str(), 0, REG_BINARY, data.data,
		                                      static_cast<uint32_t>(data.size));
	}
};

platform::setting_file_ptr platform::create_registry_settings()
{
	return std::make_shared<setting_file_impl>();
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
