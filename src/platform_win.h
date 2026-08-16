// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Windows platform implementation. Implements file system, shell integration,
// registry access, and Windows-specific functionality.

#pragma once

#define WINVER _WIN32_WINNT_WIN7
#define _WIN32_WINNT _WIN32_WINNT_WIN7
#define _WIN32_WINDOWS _WIN32_WINNT_WIN7
#define _WIN32_IE _WIN32_IE_IE110
#define NTDDI_VERSION   NTDDI_WIN7

#ifndef WIN32_LEAN_AND_MEAN
// WIN32_LEAN_AND_MEAN implies NOCRYPT and NOGDI.
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX 
#endif
#ifndef NOKERNEL
#define NOKERNEL
#endif
#ifndef NOSERVICE
#define NOSERVICE
#endif
#ifndef NOSOUND
#define NOSOUND
#endif
#ifndef NOMCX
#define NOMCX
#endif

#ifndef STRICT
#define STRICT
#endif
#define _WINSOCKAPI_    // stops windows.h including winsock.h

#include <dxgi.h>
#include <dxgi1_4.h> // IDXGIAdapter3, for the video-memory gauge in the session perf summary
#include <windows.h>
#include <wrl.h>
using namespace Microsoft::WRL;


struct factories;
struct file_load_result;
using factories_ptr = std::shared_ptr<factories>;

constexpr int max_vert_count = 4 * 512;
constexpr int max_index_count = 6 * 512;
constexpr int max_text_len = 2000;

// Colour every render target is cleared to before the scene is drawn. Both the Direct3D and the
// software backend must use this, otherwise anything the scene does not explicitly paint shows
// through as black on one backend and as this grey on the other.
constexpr float scene_clear_shade = 0.222f;

HWND app_wnd();
bool is_device_loss_error(HRESULT hr);

// Private window message posted by a frame when a Direct3D device-loss result is seen. It
// is posted (never sent) so recovery runs after the render or resize call stack has
// unwound and no draw-context or device object is still live on the stack.
constexpr UINT WM_DIFF_DEVICE_LOST = WM_APP + 0x3d1;

// Switches the whole process to CPU software rendering after the Direct3D device is lost,
// then asks the app to release GPU resources and rebuild every frame's draw context.
void handle_graphics_device_lost(const factories_ptr& f);

extern HINSTANCE get_resource_instance;
FILETIME ts_to_ft(uint64_t ts);
uint64_t ft_to_ts(const FILETIME& ft);

// Wrap an already-open Win32 file HANDLE in a platform::file. Takes ownership of the
// handle (closed when the returned file_ptr is released). Used by replace_file to hand
// back the still-open, cache-coherent handle it renamed through, so the file can be
// read back immediately without a stale by-name reopen over SMB. See platform_win_files.cpp.
namespace platform
{
	file_ptr make_file_from_handle(HANDLE h);

	// What a shell drag source actually advertises. Windows-only by nature: the formats it names
	// are clipboard format ids, and the paths come back in the shell's own UTF-16.
	struct data_object_probe
	{
		std::vector<uint32_t> enum_formats; // cfFormat ids, in EnumFormatEtc (source-preference) order
		int hdrop_enum_index = -1; // position of CF_HDROP within enum_formats (-1 = absent)
		int shell_id_list_enum_index = -1; // position of CFSTR_SHELLIDLIST within enum_formats (-1 = absent)
		bool advertises_hdrop = false; // QueryGetData(CF_HDROP)
		bool advertises_shell_id_list = false; // QueryGetData(CFSTR_SHELLIDLIST)
		int hdrop_count = -1; // files parsed from CF_HDROP (-1 = no data returned)
		int shell_id_list_count = -1; // CIDA cidl from CFSTR_SHELLIDLIST (-1 = no data returned)
		std::vector<std::wstring> hdrop_paths;
		std::vector<std::wstring> shell_id_list_paths;
	};

	data_object_probe probe_drag_data_object(const std::vector<df::file_path>& files,
	                                         const std::vector<df::folder_path>& folders);

	// Shell and common-dialog APIs reject the \\?\ prefix that to_file_system_path adds for long
	// paths, so they take the plain form and accept the MAX_PATH limit those APIs already impose.
	// There is no cross-platform notion of a shell path, so this is Windows-private.
	std::wstring to_shell_path(df::file_path path);
	std::wstring to_shell_path(df::folder_path path);

	// Brings a saved window rect back onto a display. Size is preserved where it fits and clamped to
	// the work area where it does not; the position is nudged inside.
	recti fit_window_to_work_area(recti saved, recti work_area);

	// True when a saved rect cannot be restored as it stands: it reaches no display at all, it is
	// larger than the work area it would land in, or so little of it overlaps that there is nothing
	// left to grab. A window deliberately straddling two displays is none of those and is left alone.
	bool window_needs_refit(recti saved, recti work_area, bool reaches_a_display);
}

std::string win32_to_string(const IID& iid);

struct char_pos_width
{
	float x = 0.0f;
	float y = 0.0f;
	float cx = 0.0f;
};

struct calc_text_extent_result
{
	std::vector<char_pos_width> pos;
	int cy = 0;
	int cx = 0;
};

struct text_line
{
	int begin = 0;
	int end = 0;
	int pixel_width = 0;
};

class draw_context_device : public ui::draw_context
{
public:
	virtual void destroy() = 0;
	virtual void update_font_size(int base_font_size) = 0;

	// damage is the region the window layer knows needs repainting; empty means the whole client.
	// It is an optimisation hint only - a backend may redraw more, but the resulting pixels inside
	// damage must not depend on how much was redrawn.
	virtual void begin_draw(sizei client_extent, int base_font_size, recti damage = {}) = 0;

	// Flushes the accumulated scene. Returns the underlying graphics result so the window
	// layer can detect device loss and downgrade to the CPU backend; the software backend
	// always returns S_OK.
	virtual HRESULT render() = 0;

	// Discards any damage limit, so the next render covers the whole client. Needed by callers
	// that re-present an existing scene whose textures changed underneath it.
	virtual void reset_damage()
	{
	}
	virtual void resize(sizei size) = 0;
	virtual bool is_valid() const = 0;

	// Releases every reference this context holds on the swap-chain back buffer. Must be
	// called before IDXGISwapChain::ResizeBuffers - a still-bound render target view makes
	// ResizeBuffers fail with DXGI_ERROR_INVALID_CALL. No-op on the software backend.
	virtual void release_back_buffer_references()
	{
	}

	// Software-rendering extensions (only implemented by the software backend used for
	// layered bubble popups; no-ops on the hardware backend).
	virtual void set_layer_alpha(int alpha)
	{
	}

	virtual void draw_bubble_background(recti bounds, pointi focus_location, int padding, float radius)
	{
	}
};

using draw_context_device_ptr = std::shared_ptr<draw_context_device>;

class data_object_client : public platform::clipboard_data
{
	ComPtr<IDataObject> _pData;

public:
	data_object_client(IDataObject* pData);

	bool has_data(FORMATETC* pf) const;
	bool has_drop_files() const override;
	bool has_bitmap() const override;
	platform::file_op_result drop_files(df::folder_path target, platform::drop_effect effect) override;
	platform::file_op_result save_bitmap(df::folder_path save_path, std::string_view name, bool as_png) override;
	DWORD preferred_drop_effect() const;
	description files_description() const override;
	df::file_path first_path() const override;
};

draw_context_device_ptr d3d11_create_context(const factories_ptr& f, const ComPtr<IDXGISwapChain>& swap_chain,
                                             int base_font_size);
draw_context_device_ptr create_software_draw_context(const factories_ptr& f, HWND hwnd, bool layered,
                                                     int base_font_size);
df::blob load_resource(int id, LPCWSTR lpType);

HGLOBAL image_to_handle(const file_load_result& image);
platform::file_op_result save_bitmap_info(df::folder_path save_path, std::string_view name, bool as_png,
                                          HBITMAP image_buffer_in);
void draw_surface(HDC hdc, sizei dimensions, ui::texture_format format, int stride, const uint8_t* pixels);


struct variant_t
{
	VARIANT v = {};

	variant_t() noexcept
	{
		VariantInit(&v);
	}

	~variant_t() noexcept
	{
		VariantClear(&v);
	}

	variant_t(const variant_t& other) = delete;
	variant_t& operator=(const variant_t& other) = delete;
};

struct prop_variant_t
{
	PROPVARIANT v = {};

	prop_variant_t() noexcept
	{
		PropVariantInit(&v);
	}

	~prop_variant_t() noexcept
	{
		PropVariantClear(&v);
	}

	prop_variant_t(const variant_t& other) = delete;
	prop_variant_t& operator=(const variant_t& other) = delete;
};


struct bstr_t
{
	BSTR m_str = nullptr;

	bstr_t() = default;

	explicit bstr_t(const std::wstring_view sv) : m_str(SysAllocStringLen(sv.data(), static_cast<uint32_t>(sv.size())))
	{
	}

	explicit bstr_t(const std::string_view sv) : m_str(SysAllocString(str::utf8_to_utf16(sv).c_str()))
	{
	}

	bstr_t(const bstr_t& other) = delete;
	bstr_t& operator=(const bstr_t& other) = delete;

	operator BSTR() const noexcept
	{
		return m_str;
	}

	~bstr_t() noexcept
	{
		if (m_str)
		{
			SysFreeString(m_str);
		}
	}

	BSTR* operator&() noexcept
	{
		df::assert_true(m_str == nullptr);
		return &m_str;
	}
};
