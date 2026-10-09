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
#include <Shellapi.h>
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

// The release the running system reports. Reads no files, so the UI thread may ask.
df::os_release windows_release();

// The Microsoft Basic Render Driver: WARP presented as a hardware adapter, which is what Windows
// offers when no GPU driver is working. Drawn through, every frame is a full-window CPU redraw.
bool is_software_adapter(uint32_t vendor_id, uint32_t device_id, uint32_t flags);

// Whether a device answering this CheckFormatSupport mask can draw a texture of the format, which for
// NV12 and P010 means sampling views of their planes - holding the format is not enough.
bool can_sample_texture_format(UINT support);

// The memory a GPU allocates textures from. An integrated part shares system memory, and the
// "dedicated" figure it reports is only a carve-out made at boot (128MB on Intel parts).
uint64_t gpu_texture_memory(uint64_t dedicated, uint64_t shared, bool unified_memory);

// The bytes one displayed texture may cost, given that memory (0 when there is no GPU to ask) and
// the machine's physical memory (0 when unknown).
int64_t calc_texture_budget(uint64_t gpu_bytes, int64_t total_phys);

// The adapter the renderer draws with, which a hardware decode device is made on so the two can share
// pictures. Published once the render device exists; a zero LUID while there is none, and on the CPU
// backend.
void publish_render_adapter(LUID luid);
LUID render_adapter();

// The adapter with this LUID as Windows lists them now, or null once it is gone - or for a zero LUID.
ComPtr<IDXGIAdapter1> find_adapter_by_luid(LUID luid);

// Private window message posted by a frame when a Direct3D device-loss result is seen, or when a
// hardware window cannot build a Direct3D context at all. It is posted (never sent) so recovery runs
// after the render or resize call stack has unwound and no draw-context or device object is still
// live on the stack.
constexpr UINT WM_DIFF_DEVICE_LOST = WM_APP + 0x3d1;

// Switches the whole process to CPU software rendering once the Direct3D device is lost or cannot
// present, then asks the app to release GPU resources and rebuild every frame's draw context.
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

	using shell_file_operation_w_fn = decltype(&SHFileOperationW);
	using shell_file_operation_a_fn = decltype(&SHFileOperationA);

	struct shell_operation_probe
	{
		file_op_result result;
		std::wstring destination_w;
		std::string destination_a;
		FILEOP_FLAGS flags = {};
	};

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
		std::vector<uint32_t> image_formats;
		std::vector<uint32_t> image_tymed;
		bool advertises_bitmap = false;
		bool advertises_dib = false;
		bool bitmap_retrieved = false;
		bool dib_retrieved = false;
		sizei bitmap_dimensions;
		sizei dib_dimensions;
	};

	data_object_probe probe_drag_data_object(const std::vector<df::file_path>& files,
	                                         const std::vector<df::folder_path>& folders);
	data_object_probe probe_drag_data_object(const file_load_result& loaded);

	shell_operation_probe probe_shell_file_operation_w(shell_file_operation_w_fn op);
	shell_operation_probe probe_shell_file_operation_a(shell_file_operation_a_fn op);
	shell_operation_probe probe_shell_delete_operation(shell_file_operation_w_fn op, bool allow_undo);
	std::wstring probe_shell_destination_w(df::folder_path target);
	std::string probe_shell_destination_a(df::folder_path target);
	FILEOP_FLAGS probe_shell_delete_flags(bool allow_undo);

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

	struct edit_spelling_range
	{
		std::string word;
		int pos_start = 0;
		int pos_end = 0;
	};

	std::vector<edit_spelling_range> probe_edit_spelling_ranges(
		const std::wstring& text, const std::function<bool(std::string_view)>& is_word_valid);
	recti probe_edit_spelling_bounds(HWND hwnd, int pos_start, int pos_end);

	enum class edit_context_menu_route
	{
		native_edit_procedure,
		custom_spelling_menu,
	};

	edit_context_menu_route probe_edit_context_menu_route(bool spelling_error_under_pointer, bool custom_menu_created);

	// Windows 11 offers snap layouts over a window's maximize button, and only over a button the
	// window reports as one. Only a resizable frame with a maximize box has one to report: Fullscreen
	// strips the resizing frame, and dialogs have no maximize box.
	bool can_present_maximize_caption_button(df::os_release release, uint32_t root_style);

	// Whether a toolbar reports its maximize button as that caption button now. Not while the frame is
	// maximized: Windows then opens the flyout at a standard caption's depth, over the middle of the
	// taller restore button the top bar draws, and the flyout takes the click meant for the button.
	bool presents_maximize_caption_button(df::os_release release, uint32_t root_style);

	// The hover and press of a toolbar button reported as the maximize caption button. Windows sends
	// that button non-client mouse messages, which the toolbar control never tracks. Ids are toolbar
	// command ids; zero is none.
	struct caption_button_tracker
	{
		int hover_id = 0;
		int pressed_id = 0;

		// Each answers whether what the toolbar draws changed.
		bool hover(int id);
		bool press(int id);
		bool leave();

		// The id to invoke: a release over the button its press began on, otherwise zero.
		int release(int id);
	};

	struct caption_button_probe
	{
		bool offered = false;
		int maximize_id = 0;
		int maximize_hit = HTNOWHERE;
		int other_hit = HTNOWHERE;
		int maximized_hit = HTNOWHERE;
		int frameless_hit = HTNOWHERE;
		int hovered_id = 0;
		int hover_after_leave = 0;
		int invocations = 0;
	};

	// Builds a toolbar holding an ordinary button and a maximize button in a hidden frame, and sends
	// it the messages Windows sends while the pointer rests on, presses and leaves the maximize button.
	caption_button_probe probe_toolbar_caption_button();

	struct number_format_probe_snapshot
	{
		std::wstring decimal_sep;
		std::wstring thousand_sep;
		uint32_t leading_zero = 1;
		uint32_t negative_order = 1;
	};

	void set_number_format_probe_snapshots(std::vector<number_format_probe_snapshot> snapshots,
	                                       bool invalidate_during_refresh);
	void clear_number_format_probe_snapshots();
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

// The backend interface is ui's, not Windows'. Imported here so the Windows layer can keep
// spelling it unqualified, which is how it reads in every backend and in the window layer.
using ui::draw_context_device;
using ui::draw_context_device_ptr;

// A path arriving from a Win32 API is UTF-16; df::file_path and df::folder_path are UTF-8 and know
// nothing of wide strings. These name that conversion once, on the side of the boundary that has a
// reason to know about it.
inline df::file_path to_file_path(const std::wstring_view w)
{
	return df::file_path(str::utf16_to_utf8(w));
}

inline df::folder_path to_folder_path(const std::wstring_view w)
{
	return df::folder_path(str::utf16_to_utf8(w));
}

class data_object_client : public platform::clipboard_data
{
	ComPtr<IDataObject> _pData;

public:
	data_object_client(IDataObject* pData);

	bool has_data(FORMATETC* pf) const;
	bool has_drop_files() const override;
	bool has_bitmap() const override;
	platform::file_op_result drop_files(df::folder_path target, platform::drop_effect effect) override;
	platform::clipboard_bitmap_ptr capture_bitmap() override;
	platform::file_op_result save_bitmap(df::folder_path save_path, std::string_view name, bool as_png) override;
	DWORD preferred_drop_effect() const;
	description files_description() const override;
	df::file_path first_path() const override;
	std::vector<df::file_path> drop_paths() const override;
};

draw_context_device_ptr d3d11_create_context(const factories_ptr& f, const ComPtr<IDXGISwapChain>& swap_chain,
                                             int base_font_size);
// A texture on the factories' Direct3D device, as every hardware draw context makes them.
ui::texture_ptr d3d11_create_texture(const factories_ptr& f);
draw_context_device_ptr create_software_draw_context(const factories_ptr& f, HWND hwnd, bool layered,
                                                     int base_font_size);
df::blob load_resource(int id, LPCWSTR lpType);

HGLOBAL image_to_handle(const file_load_result& image);
platform::file_op_result save_bitmap_info(df::folder_path save_path, std::string_view name, bool as_png,
                                          HBITMAP image_buffer_in);


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

	// PropVariantClear releases what v points to, so a copy would release it twice. The deleted pair
	// used to name variant_t, which left this type's own copy operations generated.
	prop_variant_t(const prop_variant_t& other) = delete;
	prop_variant_t& operator=(const prop_variant_t& other) = delete;
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
