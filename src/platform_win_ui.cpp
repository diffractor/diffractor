// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Windows UI framework. Implements window management, message handling,
// input processing, native control hosting and painting, clipboard, drag-drop, and
// system integration.

#include "pch.h"
#include "platform_win.h"
#include "platform_win_visual.h"
#include "app_command_line.h"

int run_console_tests(std::string_view test_filter);
int run_duplicate_report(std::string_view folder_text, std::string_view output_text);
int generate_wiki_docs(std::string_view output_folder);
int validate_po_files();

#include <dwmapi.h>
#include <Dbt.h>
#include <versionhelpers.h>
#include <Shellapi.h>
#include <Shobjidl.h>
#include <iostream>
#include <winsock2.h>
#include <wtsapi32.h>
#include <shlguid.h>
#include <io.h>
#include <fcntl.h>

#include <Shlwapi.h>

#ifdef _DEBUG
ui::surface_ptr platform::capture_window_surface(const std::any& window_handle)
{
	const auto hwnd = std::any_cast<HWND>(window_handle);
	RECT bounds{};
	if (!hwnd || !GetWindowRect(hwnd, &bounds)) return nullptr;

	const auto width = bounds.right - bounds.left;
	const auto height = bounds.bottom - bounds.top;
	if (width < 1 || height < 1) return nullptr;

	const auto screen_dc = GetDC(nullptr);
	const auto memory_dc = CreateCompatibleDC(screen_dc);
	const auto bitmap = CreateCompatibleBitmap(screen_dc, width, height);
	const auto previous = SelectObject(memory_dc, bitmap);
	const auto captured = BitBlt(memory_dc, 0, 0, width, height, screen_dc, bounds.left, bounds.top,
	                             SRCCOPY | CAPTUREBLT) != FALSE;

	auto surface = std::make_shared<ui::surface>();
	const auto allocated = surface->alloc(width, height, ui::texture_format::ARGB, ui::orientation::top_left);
	BITMAPINFO bitmap_info{};
	bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bitmap_info.bmiHeader.biWidth = width;
	bitmap_info.bmiHeader.biHeight = -height;
	bitmap_info.bmiHeader.biPlanes = 1;
	bitmap_info.bmiHeader.biBitCount = 32;
	bitmap_info.bmiHeader.biCompression = BI_RGB;
	const auto row_bytes = static_cast<size_t>(width) * 4;
	std::vector<uint8_t> bitmap_pixels(row_bytes * height);
	const auto copied = captured && allocated && GetDIBits(memory_dc, bitmap, 0, height, bitmap_pixels.data(),
	                                                       &bitmap_info, DIB_RGB_COLORS) == height;
	if (copied)
	{
		for (auto y = 0; y < height; ++y)
		{
			memcpy_s(surface->pixels_line(y), surface->stride(), bitmap_pixels.data() + y * row_bytes, row_bytes);
			auto* pixels = std::bit_cast<ui::color32*>(surface->pixels_line(y));
			for (auto x = 0; x < width; ++x) pixels[x] |= 0xff000000u;
		}
	}

	SelectObject(memory_dc, previous);
	DeleteObject(bitmap);
	DeleteDC(memory_dc);
	ReleaseDC(nullptr, screen_dc);

	return copied ? surface : nullptr;
}
#endif

#ifndef WINSTORE
#include <DbgHelp.h>
#endif

#include "app_text.h"
#include "files.h"
#include "ui_elements.h"
#include "util_spell.h"
#include "platform_win_res.h"

#pragma comment(lib, "wtsapi32")
#pragma comment(lib, "dxgi")
#pragma comment(lib, "d3d11")
#pragma comment(lib, "Dwrite")
#pragma comment(lib, "dxguid")
#pragma comment(lib, "Shlwapi")
#pragma comment(lib, "Shcore")
#pragma comment(lib, "Windowscodecs")

// Wire up stdout/stderr for the headless console commands (/test, /gen-docs,
// /validate-po). This app is built as a GUI subsystem executable, so it has no
// console by default. When its output is redirected to a pipe or file (e.g.
// `diffractor64.exe /test | Out-Host` or `> log.txt`, as CI does), we must bind
// the CRT stdout/stderr to the INHERITED handle so the parent process captures
// it. Reopening "CONOUT$" instead (the old behaviour) sends the output to a
// console screen buffer that a piped/redirected/headless caller never sees --
// which made CI show an empty log. Only attach/allocate a real console when the
// output is NOT redirected (i.e. a genuine interactive run).
static void setup_headless_console()
{
	const auto bind_std_stream = [](const DWORD std_handle, FILE* const stream)
	{
		const HANDLE h = GetStdHandle(std_handle);
		const DWORD type = GetFileType(h);

		if (type == FILE_TYPE_PIPE || type == FILE_TYPE_DISK)
		{
			// Redirected: point the CRT stream at the inherited pipe/file.
			const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(h), _O_TEXT);

			// Either failure leaves the stream on its original handle, so the caller still
			// needs the console fallback.
			if (fd == -1)
			{
				return false;
			}

			fflush(stream);
			const int duped = _dup2(fd, _fileno(stream));
			_close(fd);

			if (duped != 0)
			{
				return false;
			}

			setvbuf(stream, nullptr, _IONBF, 0);
			return true;
		}

		return false;
	};

	const bool out_redirected = bind_std_stream(STD_OUTPUT_HANDLE, stdout);
	const bool err_redirected = bind_std_stream(STD_ERROR_HANDLE, stderr);

	if (!out_redirected || !err_redirected)
	{
		// At least one stream is not redirected, so we need a console for it.
		if (!AttachConsole(ATTACH_PARENT_PROCESS))
		{
			AllocConsole();
		}

		FILE* fp = nullptr;
		if (!out_redirected) freopen_s(&fp, "CONOUT$", "w", stdout);
		if (!err_redirected) freopen_s(&fp, "CONOUT$", "w", stderr);
	}

	std::setlocale(LC_ALL, "en_US.UTF-8");
}

static constexpr auto ui_nonclient_border_thickness = 5;
static constexpr auto ui_base_icon_cxy = 18;
static constexpr auto ui_element_padding = 8;
static constexpr auto ui_focus_padding = 2;
static constexpr auto ui_corner_radius = 6;
static constexpr auto ui_button_padding = 8;
static constexpr auto ui_component_snap = 8;
static constexpr auto ui_baseline_snap = 4;
static constexpr auto ui_cx_resize_handle = 12;
static constexpr auto ui_scroll_width = 20;


int ui::ticks_since_last_user_action = 0;

static constexpr auto s_window_rect = "window_rect";
static std::string restart_cmd_line;

extern std::atomic_bool number_format_invalid;

class button_impl;
class control_host_impl;
class win32_app;

int calc_icon_cxy(const double scale_factor)
{
	return df::round(ui_base_icon_cxy * scale_factor);
}

/////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////

uint32_t ui_wait_for_signal(platform::thread_event& te, uint32_t timeout_ms, const std::function<bool(LPMSG m)>& cb);

#define ABM_GETAUTOHIDEBAREX    0x0000000b // multimon aware autohide bars


#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp)                        ((int)(short)LOWORD(lp))
#endif
#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lp)                        ((int)(short)HIWORD(lp))
#endif

struct win_rect : RECT
{
	win_rect() noexcept
	{
		left = 0;
		top = 0;
		right = 0;
		bottom = 0;
	}

	win_rect(const recti other) noexcept
	{
		left = other.left;
		top = other.top;
		right = other.right;
		bottom = other.bottom;
	}

	win_rect(const RECT& other) noexcept
	{
		left = other.left;
		top = other.top;
		right = other.right;
		bottom = other.bottom;
	}

	win_rect(const pointi point, const sizei size) noexcept
	{
		right = (left = point.x) + size.cx;
		bottom = (top = point.y) + size.cy;
	}

	win_rect(const int l, const int t, const int r, const int b) noexcept
	{
		left = l;
		top = t;
		right = r;
		bottom = b;
	}

	operator recti() const noexcept
	{
		return {left, top, right, bottom};
	}

	operator LPRECT() noexcept
	{
		return this;
	}

	operator LPCRECT() const noexcept
	{
		return this;
	}

	bool is_empty() const noexcept
	{
		return left >= right || top >= bottom;
	}

	int width() const noexcept
	{
		return right - left;
	}

	int height() const noexcept
	{
		return bottom - top;
	}

	win_rect inflate(const int xy) const noexcept
	{
		return {left - xy, top - xy, right + xy, bottom + xy};
	}

	win_rect inflate(const int x, const int y) const noexcept
	{
		return {left - x, top - y, right + x, bottom + y};
	}

	win_rect offset(const int x, const int y) const noexcept
	{
		return {left + x, top + y, right + x, bottom + y};
	}

	bool intersects(const win_rect& other) const noexcept
	{
		return left < other.right &&
			top < other.bottom &&
			right > other.left &&
			bottom > other.top;
	}

	win_rect intersection(const win_rect& other) const noexcept
	{
		if (!intersects(other)) return {};

		return {
			std::max(left, other.left),
			std::max(top, other.top),
			std::min(right, other.right),
			std::min(bottom, other.bottom)
		};
	}
};

static ui::key_state to_key_state(const WPARAM wParam)
{
	ui::key_state result;
	result.control = (wParam & MK_CONTROL) != 0;
	result.shift = (wParam & MK_SHIFT) != 0;
	return result;
}

ui::key_state ui::current_key_state()
{
	key_state result;
	result.control = GetAsyncKeyState(VK_CONTROL) < 0;
	result.shift = GetAsyncKeyState(VK_SHIFT) < 0;
	result.alt = GetAsyncKeyState(VK_MENU) < 0;
	return result;
}

std::any ui::focus()
{
	return GetFocus();
}

void ui::focus(const std::any& w)
{
	SetFocus(std::any_cast<HWND>(w));
}


static std::wstring window_text_w(const HWND h)
{
	const auto len = ::GetWindowTextLength(h);
	if (len <= 0) return {};
	std::wstring result(len + 1, 0);
	// GetWindowTextLength can over-report, so the copied length decides the result. Trusting
	// the reported length instead leaves embedded NULs in strings that reach settings and search.
	const auto copied = GetWindowText(h, result.data(), len + 1);
	result.resize(copied > 0 ? copied : 0, 0);
	return result;
}

static std::string window_text(const HWND h)
{
	return str::utf16_to_utf8(window_text_w(h));
}

static void fill_rect(const HDC hdc, const DWORD clr, const win_rect& bounds)
{
	if (!bounds.is_empty())
	{
		const COLORREF clr_old = SetBkColor(hdc, clr);
		if (clr_old != CLR_INVALID)
		{
			::ExtTextOut(hdc, 0, 0, ETO_OPAQUE, bounds, nullptr, 0, nullptr);
			SetBkColor(hdc, clr_old);
		}
	}
}

struct resources_t
{
	HCURSOR link = nullptr;
	HCURSOR normal = nullptr;
	HCURSOR zoom = nullptr;
	HCURSOR move = nullptr;
	HCURSOR size_all = nullptr;
	HCURSOR left_right = nullptr;
	HCURSOR up_down = nullptr;
	HCURSOR hand_up = nullptr;
	HCURSOR hand_down = nullptr;
	HCURSOR select_cur = nullptr;
	HCURSOR text_select = nullptr;
	HICON diffractor_16 = nullptr;
	HICON diffractor_32 = nullptr;
	HICON diffractor_64 = nullptr;

	void init(const HINSTANCE h)
	{
		link = LoadCursor(h, MAKEINTRESOURCE(IDC_HANDLINK));
		normal = LoadCursor(nullptr, IDC_ARROW);
		zoom = LoadCursor(h, MAKEINTRESOURCE(IDC_ZOOM));
		move = LoadCursor(h, MAKEINTRESOURCE(IDC_MOVE));
		size_all = LoadCursor(nullptr, IDC_SIZEALL);
		left_right = LoadCursor(nullptr, IDC_SIZEWE);
		up_down = LoadCursor(nullptr, IDC_SIZENS);
		hand_up = LoadCursor(h, MAKEINTRESOURCE(IDC_HANDUP));
		hand_down = LoadCursor(h, MAKEINTRESOURCE(IDC_HANDDOWN));
		select_cur = LoadCursor(h, MAKEINTRESOURCE(IDC_SELECT));
		text_select = LoadCursor(nullptr, IDC_IBEAM);
		diffractor_16 = static_cast<HICON>(LoadImage(h, MAKEINTRESOURCE(IDI_DIFFRACTOR), IMAGE_ICON, 16, 16, 0));
		diffractor_32 = static_cast<HICON>(LoadImage(h, MAKEINTRESOURCE(IDI_DIFFRACTOR), IMAGE_ICON, 32, 32, 0));
		diffractor_64 = static_cast<HICON>(LoadImage(h, MAKEINTRESOURCE(IDI_DIFFRACTOR), IMAGE_ICON, 64, 64, 0));

		// Log warnings for failed resource loads to aid debugging
		if (!normal) df::log(__FUNCTION__, "Failed to load normal cursor");
		if (!diffractor_16) df::log(__FUNCTION__, "Failed to load 16x16 icon");
		if (!diffractor_32) df::log(__FUNCTION__, "Failed to load 32x32 icon");
		if (!diffractor_64) df::log(__FUNCTION__, "Failed to load 64x64 icon");
	}
};

static resources_t resources;

// Maps the portable cursor enum onto the loaded cursor resources. Returns false for a value
// with no cursor of its own so the caller leaves the current one alone.
static bool cursor_icon(const ui::style::cursor cursor, HICON& icon)
{
	switch (cursor)
	{
	case ui::style::cursor::none: icon = nullptr;
		return true;
	case ui::style::cursor::normal: icon = resources.normal;
		return true;
	case ui::style::cursor::link: icon = resources.link;
		return true;
	case ui::style::cursor::zoom: icon = resources.zoom;
		return true;
	case ui::style::cursor::select: icon = resources.select_cur;
		return true;
	case ui::style::cursor::text_select: icon = resources.text_select;
		return true;
	case ui::style::cursor::move: icon = resources.move;
		return true;
	case ui::style::cursor::size_all: icon = resources.size_all;
		return true;
	case ui::style::cursor::left_right: icon = resources.left_right;
		return true;
	case ui::style::cursor::up_down: icon = resources.up_down;
		return true;
	case ui::style::cursor::hand_up: icon = resources.hand_up;
		return true;
	case ui::style::cursor::hand_down: icon = resources.hand_down;
		return true;
	default:
		return false;
	}
}

ui::color32 ui::style::color::dialog_text = 0;
ui::color32 ui::style::color::dialog_selected_text = 0;
ui::color32 ui::style::color::dialog_background = 0;
ui::color32 ui::style::color::dialog_selected_background = 0;
ui::color32 ui::style::color::button_background = 0;
ui::color32 ui::style::color::edit_background = 0;
ui::color32 ui::style::color::edit_text = 0;

ui::color32 ui::style::color::toolbar_background = 0;
ui::color32 ui::style::color::bubble_background = 0;
ui::color32 ui::style::color::sidebar_background = 0;
ui::color32 ui::style::color::group_background = 0;
ui::color32 ui::style::color::view_background = 0;
ui::color32 ui::style::color::view_selected_background = 0;
ui::color32 ui::style::color::view_text = 0;

ui::color32 ui::style::color::menu_background = 0;
ui::color32 ui::style::color::menu_text = 0;
ui::color32 ui::style::color::menu_shortcut_text = 0;

ui::color32 ui::style::color::important_background = 0;
ui::color32 ui::style::color::warning_background = 0;
ui::color32 ui::style::color::info_background = 0;
ui::color32 ui::style::color::desktop_background = 0;

ui::color32 ui::style::color::rank_background = 0;
ui::color32 ui::style::color::sidecar_background = 0;
ui::color32 ui::style::color::duplicate_background = 0;

static constexpr ui::color32 red = 0xaa2211;
static constexpr ui::color32 orange = 0xCC6611; // 0xCC7711; // 0x995511; 0xF57C00
static constexpr ui::color32 blue = 0x0288D1;
static constexpr ui::color32 blue2 = 0x117799;

static void init_color_styles()
{
	ui::style::color::dialog_text = 0x00eeeeee; // 0x00222222;
	ui::style::color::dialog_selected_text = 0x00ffffff; // 0x00222222;
	ui::style::color::dialog_background = 0x00555555; //  0x00BBBBBB;
	ui::style::color::dialog_selected_background = ui::bgr(0x005588EE);
	ui::style::color::button_background = 0x00444444;
	ui::style::color::edit_background = GetSysColor(COLOR_WINDOW);
	ui::style::color::edit_text = GetSysColor(COLOR_WINDOWTEXT);

	ui::style::color::sidebar_background = 0x00333333;
	ui::style::color::bubble_background = 0x00333333;
	ui::style::color::group_background = 0x00444444;
	ui::style::color::toolbar_background = 0x00666666;

	ui::style::color::important_background = ui::bgr(orange);
	ui::style::color::warning_background = ui::bgr(red);
	ui::style::color::info_background = ui::bgr(blue2);

	ui::style::color::view_background = 0x00333333;
	ui::style::color::view_selected_background = ui::bgr(blue);
	ui::style::color::view_text = 0x00eeeeee;

	ui::style::color::menu_background = 0x00444444;
	ui::style::color::menu_text = 0x00eeeeee;
	ui::style::color::menu_shortcut_text = ui::bgr(0x006699EE);

	ui::style::color::desktop_background = GetSysColor(COLOR_DESKTOP);

	ui::style::color::rank_background = ui::bgr(0x00997711);
	ui::style::color::sidecar_background = ui::bgr(0x006677CC);
	ui::style::color::duplicate_background = ui::bgr(0x007711AA);
}

char32_t keys::APPS = VK_APPS;
char32_t keys::BACK = VK_BACK;
char32_t keys::BROWSER_BACK = VK_BROWSER_BACK;
char32_t keys::BROWSER_FAVORITES = VK_BROWSER_FAVORITES;
char32_t keys::BROWSER_FORWARD = VK_BROWSER_FORWARD;
char32_t keys::BROWSER_HOME = VK_BROWSER_HOME;
char32_t keys::BROWSER_REFRESH = VK_BROWSER_REFRESH;
char32_t keys::BROWSER_SEARCH = VK_BROWSER_SEARCH;
char32_t keys::BROWSER_STOP = VK_BROWSER_STOP;
char32_t keys::DEL = VK_DELETE;
char32_t keys::DOWN = VK_DOWN;
char32_t keys::ESCAPE = VK_ESCAPE;
char32_t keys::F1 = VK_F1;
char32_t keys::F10 = VK_F10;
char32_t keys::F11 = VK_F11;
char32_t keys::F2 = VK_F2;
char32_t keys::F3 = VK_F3;
char32_t keys::F4 = VK_F4;
char32_t keys::F5 = VK_F5;
char32_t keys::F6 = VK_F6;
char32_t keys::F7 = VK_F7;
char32_t keys::F8 = VK_F8;
char32_t keys::F9 = VK_F9;
char32_t keys::INSERT = VK_INSERT;
char32_t keys::LEFT = VK_LEFT;
char32_t keys::MEDIA_NEXT_TRACK = VK_MEDIA_NEXT_TRACK;
char32_t keys::MEDIA_PLAY_PAUSE = VK_MEDIA_PLAY_PAUSE;
char32_t keys::MEDIA_PREV_TRACK = VK_MEDIA_PREV_TRACK;
char32_t keys::MEDIA_STOP = VK_MEDIA_STOP;
char32_t keys::NEXT = VK_NEXT;
char32_t keys::OEM_4 = VK_OEM_4;
char32_t keys::OEM_6 = VK_OEM_6;
char32_t keys::OEM_MINUS = VK_OEM_MINUS;
char32_t keys::OEM_PLUS = VK_OEM_PLUS;
char32_t keys::PRIOR = VK_PRIOR;
char32_t keys::RETURN = VK_RETURN;
char32_t keys::RIGHT = VK_RIGHT;
char32_t keys::SPACE = VK_SPACE;
char32_t keys::TAB = VK_TAB;
char32_t keys::UP = VK_UP;
char32_t keys::VOLUME_DOWN = VK_VOLUME_DOWN;
char32_t keys::VOLUME_MUTE = VK_VOLUME_MUTE;
char32_t keys::VOLUME_UP = VK_VOLUME_UP;
char32_t keys::HOME = VK_HOME;
char32_t keys::END = VK_END;

std::string_view keys::format(const int key)
{
	if (key == BACK) return tt.keyboard_back;
	if (key == BROWSER_BACK) return tt.keyboard_browser_back;
	if (key == BROWSER_FAVORITES) return tt.keyboard_browser_favorites;
	if (key == BROWSER_FORWARD) return tt.keyboard_browser_forward;
	if (key == BROWSER_HOME) return tt.keyboard_browser_home;
	if (key == BROWSER_REFRESH) return tt.keyboard_browser_refresh;
	if (key == BROWSER_SEARCH) return tt.keyboard_browser_search;
	if (key == BROWSER_STOP) return tt.keyboard_browser_stop;
	if (key == DEL) return tt.keyboard_del;
	if (key == DOWN) return tt.keyboard_down;
	if (key == END) return tt.keyboard_end;
	if (key == ESCAPE) return tt.keyboard_escape;
	if (key == F1) return tt.keyboard_f1;
	if (key == F10) return tt.keyboard_f10;
	if (key == F11) return tt.keyboard_f11;
	if (key == F2) return tt.keyboard_f2;
	if (key == F3) return tt.keyboard_f3;
	if (key == F4) return tt.keyboard_f4;
	if (key == F5) return tt.keyboard_f5;
	if (key == F6) return tt.keyboard_f6;
	if (key == F7) return tt.keyboard_f7;
	if (key == F8) return tt.keyboard_f8;
	if (key == F9) return tt.keyboard_f9;
	if (key == HOME) return tt.keyboard_home;
	if (key == INSERT) return tt.keyboard_insert;
	if (key == LEFT) return tt.keyboard_left;
	if (key == MEDIA_NEXT_TRACK) return tt.keyboard_media_next_track;
	if (key == MEDIA_PLAY_PAUSE) return tt.keyboard_media_play_pause;
	if (key == MEDIA_PREV_TRACK) return tt.keyboard_media_prev_track;
	if (key == MEDIA_STOP) return tt.keyboard_media_stop;
	if (key == NEXT) return tt.keyboard_next;
	if (key == OEM_4) return tt.keyboard_oem_4;
	if (key == OEM_6) return tt.keyboard_oem_6;
	if (key == OEM_MINUS) return tt.keyboard_oem_minus;
	if (key == OEM_PLUS) return tt.keyboard_oem_plus;
	if (key == PRIOR) return tt.keyboard_prior;
	if (key == RETURN) return tt.keyboard_enter;
	if (key == RIGHT) return tt.keyboard_right;
	if (key == SPACE) return tt.keyboard_space;
	if (key == TAB) return tt.keyboard_tab;
	if (key == UP) return tt.keyboard_up;
	if (key == VOLUME_DOWN) return tt.keyboard_volume_down;
	if (key == VOLUME_MUTE) return tt.keyboard_volume_mute;
	if (key == VOLUME_UP) return tt.keyboard_volume_up;
	return "?";
}

/////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////

static thread_local bool is_current_thread_ui = false;
static HWND ui_app_wnd = nullptr;
HINSTANCE get_resource_instance = nullptr;

HWND app_wnd()
{
	return ui_app_wnd;
}

static bool is_edit_class(const wchar_t* class_name)
{
	return _wcsicmp(class_name, L"DIFF_EDIT") == 0 ||
		_wcsicmp(class_name, L"Edit") == 0;
}

static bool is_edit(const HWND hWnd)
{
	wchar_t class_name[100];
	return IsWindow(hWnd) && ::GetClassName(hWnd, class_name, 100) && is_edit_class(class_name);
}

static void track_mouse_leave(const HWND hWnd)
{
	TRACKMOUSEEVENT tme = {0};
	tme.cbSize = sizeof(tme);
	tme.dwFlags = TME_LEAVE;
	tme.hwndTrack = hWnd;
	tme.dwHoverTime = 0;
	TrackMouseEvent(&tme);
}

static bool wants_return(const HWND hWnd)
{
	if (is_edit(hWnd))
	{
		return GetWindowLongPtr(hWnd, GWL_STYLE) & ES_WANTRETURN && !(GetKeyState(VK_CONTROL) & 0x8000);
	}

	return false;
}

void SetFont(
	const HWND hwnd,
	_In_ HFONT hFont,
	_In_ const BOOL bRedraw = TRUE) noexcept
{
	df::assert_true(IsWindow(hwnd));
	::SendMessage(hwnd, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(bRedraw, 0));
}

HFONT GetFont(const HWND hwnd) noexcept
{
	df::assert_true(IsWindow(hwnd));
	return (HFONT)::SendMessage(hwnd, WM_GETFONT, 0, 0);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static HANDLE load_icon_font()
{
	auto font_data = load_resource(IDF_ICONS, L"BINARY");
	if (font_data.empty())
	{
		return nullptr;
	}
	DWORD nFonts = 0;
	return AddFontMemResourceEx(font_data.data(), static_cast<uint32_t>(font_data.size()), nullptr, &nFonts);
}

static HFONT create_font(const ui::style::font_face type, const int base_font_size, const bool clear_type = false)
{
	static auto* icon_font = load_icon_font();

	LOGFONT lf = {};

	lf.lfWeight = FW_NORMAL;
	wcscpy_s(lf.lfFaceName, LF_FACESIZE, L"Calibri");
	lf.lfOutPrecision = OUT_TT_PRECIS;
	lf.lfQuality = clear_type ? CLEARTYPE_NATURAL_QUALITY : ANTIALIASED_QUALITY;

	switch (type)
	{
	case ui::style::font_face::dialog:
		lf.lfHeight = -base_font_size;
		break;
	case ui::style::font_face::code:
		lf.lfHeight = -df::mul_div(base_font_size, 4, 5);
		wcscpy_s(lf.lfFaceName, LF_FACESIZE, L"Consolas");
		break;
	case ui::style::font_face::icons:
		lf.lfHeight = -base_font_size;
		wcscpy_s(lf.lfFaceName, LF_FACESIZE, L"Segoe MDL2 Assets");
		break;
	case ui::style::font_face::small_icons:
		lf.lfHeight = -df::mul_div(base_font_size, 10, 16);
		wcscpy_s(lf.lfFaceName, LF_FACESIZE, L"Segoe MDL2 Assets");
		break;
	case ui::style::font_face::title:
		lf.lfHeight = -df::mul_div(base_font_size, 3, 2);
		break;
	case ui::style::font_face::mega:
		lf.lfHeight = -df::mul_div(base_font_size, 9, 4);
		break;
	default:
		break;
	}

	return ::CreateFontIndirect(&lf);
}


static HWND window_from_location(const HWND parent, const pointi loc)
{
	POINT client_loc = {loc.x, loc.y};
	ScreenToClient(parent, &client_loc);
	auto* w = ChildWindowFromPointEx(parent, client_loc, CWP_SKIPINVISIBLE);

	if (w && w != parent)
	{
		auto* const ww = window_from_location(w, loc);
		if (ww) w = ww;
	}

	return w;
}

int gdi_text_line_height(const HDC hdc, const HFONT font)
{
	if (!hdc) return 0;

	auto* const old_font = SelectObject(hdc, font);
	TEXTMETRIC tm = {};
	GetTextMetrics(hdc, &tm);
	SelectObject(hdc, old_font);

	return tm.tmHeight;
}

static int gdi_text_line_height(const HWND hwnd, const HFONT font)
{
	int result = 0;
	auto* const dc = GetDC(hwnd);

	if (dc)
	{
		result = gdi_text_line_height(dc, font);
		ReleaseDC(hwnd, dc);
	}

	return result;
}

static void draw_gradient(const HDC dc, const recti r, const DWORD c1, const DWORD c2)
{
	TRIVERTEX vert[2];
	GRADIENT_RECT gRect;
	vert[0].x = r.left;
	vert[0].y = r.top;
	vert[0].Red = ui::get_r(c1) << 8;
	vert[0].Green = ui::get_g(c1) << 8;
	vert[0].Blue = ui::get_b(c1) << 8;
	vert[0].Alpha = 0;

	vert[1].x = r.right;
	vert[1].y = r.bottom;
	vert[1].Red = ui::get_r(c2) << 8;
	vert[1].Green = ui::get_g(c2) << 8;
	vert[1].Blue = ui::get_b(c2) << 8;
	vert[1].Alpha = 0;

	gRect.UpperLeft = 0;
	gRect.LowerRight = 1;
	GradientFill(dc, vert, 2, &gRect, 1, GRADIENT_FILL_RECT_V);
}

// Rasterises a bordered rounded rectangle over a whole top-down 32bpp BI_RGB buffer. GDI has no
// anti-aliased equivalent, and stretching a pre-rendered RoundRect skin to fit resampled the border
// differently at every width, so the frame shimmered as the control resized.
static void fill_round_rect(uint32_t* const bits, const int w, const int h, const float radius,
                            const float border_width, const COLORREF fill_clr, const COLORREF edge_clr,
                            const COLORREF bg_clr)
{
	if (!bits || w <= 0 || h <= 0) return;

	const auto channels = [](const COLORREF c)
	{
		return std::array{
			static_cast<float>(GetRValue(c)), static_cast<float>(GetGValue(c)), static_cast<float>(GetBValue(c))
		};
	};

	const auto has_edge = fill_clr != edge_clr;
	const auto bg = channels(bg_clr);
	const auto fill = channels(fill_clr);
	const auto edge = channels(has_edge ? edge_clr : fill_clr);

	const auto half_w = w * 0.5f;
	const auto half_h = h * 0.5f;
	const auto limit = std::min(half_w, half_h);
	const auto r = std::clamp(radius, 0.0f, limit);
	const auto border = has_edge ? std::clamp(border_width, 0.0f, limit) : 0.0f;

	// Half-extents of the straight-edged core; the rounded outline is that core grown by r.
	const auto core_w = half_w - r;
	const auto core_h = half_h - r;

	for (auto y = 0; y < h; ++y)
	{
		auto* const line = bits + static_cast<size_t>(y) * static_cast<size_t>(w);
		const auto qy = std::abs(y + 0.5f - half_h) - core_h;
		const auto qy_out = std::max(qy, 0.0f);

		for (auto x = 0; x < w; ++x)
		{
			const auto qx = std::abs(x + 0.5f - half_w) - core_w;
			const auto qx_out = std::max(qx, 0.0f);

			// Signed distance to the outline: negative inside. The sqrt only contributes in the corners.
			const auto d = std::sqrt(qx_out * qx_out + qy_out * qy_out) + std::min(std::max(qx, qy), 0.0f) - r;

			const auto outer_coverage = std::clamp(0.5f - d, 0.0f, 1.0f);
			const auto inner_coverage = has_edge ? std::clamp(0.5f - (d + border), 0.0f, 1.0f) : 0.0f;

			uint32_t px = 0;

			for (auto i = 0; i < 3; ++i)
			{
				auto v = bg[i] + (edge[i] - bg[i]) * outer_coverage;
				v += (fill[i] - v) * inner_coverage;
				px = (px << 8) | static_cast<uint32_t>(std::clamp(v, 0.0f, 255.0f) + 0.5f);
			}

			line[x] = px;
		}
	}
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int global_base_font_size = normal_font_size;

class owner_context
{
public:
	HFONT small_icons = nullptr;
	HFONT icons = nullptr;
	HFONT dialog = nullptr;
	HFONT title = nullptr;
	HFONT code = nullptr;
	HFONT mega = nullptr;
	double scale_factor = 1.0;

	mutable df::hash_map<uint32_t, HBRUSH> cached_gdi_brushes;

	// Every window fonted from this context is recorded here. update_fonts() deletes all six
	// HFONTs, so a window that is not re-fonted is left holding a freed handle - GDI recycles
	// handle values, so a later WM_SETFONT or paint can select an arbitrary live object into the
	// control's DC. Recording the face as well as the window means the refresh restores the face
	// the window was created with instead of flattening everything to `dialog`.
	mutable std::vector<std::pair<HWND, ui::style::font_face>> fonted_windows;

	owner_context(const double scale_factor_in) : scale_factor(scale_factor_in)
	{
		update_fonts();
	}

	void forget_dead_windows() const
	{
		std::erase_if(fonted_windows, [](const auto& e) { return !IsWindow(e.first); });
	}

	void set_window_font(const HWND hwnd, const ui::style::font_face f) const
	{
		if (!hwnd || !IsWindow(hwnd)) return;

		forget_dead_windows();

		const auto found = std::ranges::find_if(fonted_windows, [hwnd](const auto& e) { return e.first == hwnd; });

		if (found == fonted_windows.cend()) fonted_windows.emplace_back(hwnd, f);
		else found->second = f;

		SetFont(hwnd, font(f));
	}

	void delete_brushes() const
	{
		for (const auto b : cached_gdi_brushes)
		{
			DeleteObject(b.second);
		}

		cached_gdi_brushes.clear();
	}

	void delete_fonts()
	{
		DeleteObject(small_icons);
		DeleteObject(icons);
		DeleteObject(dialog);
		DeleteObject(title);
		DeleteObject(code);
		DeleteObject(mega);

		small_icons = nullptr;
		icons = nullptr;
		dialog = nullptr;
		title = nullptr;
		code = nullptr;
		mega = nullptr;
	}

	~owner_context()
	{
		// Any window that outlives this context must stop referencing our fonts before they are
		// deleted; the system font is the safe stand-in.
		for (const auto& e : fonted_windows)
		{
			if (IsWindow(e.first)) SetFont(e.first, nullptr, FALSE);
		}

		fonted_windows.clear();
		delete_brushes();
		delete_fonts();
	}

	HFONT font(const ui::style::font_face f) const
	{
		switch (f)
		{
		case ui::style::font_face::code: return code;
		case ui::style::font_face::dialog: return dialog;
		case ui::style::font_face::title: return title;
		case ui::style::font_face::mega: return mega;
		case ui::style::font_face::icons: return icons;
		case ui::style::font_face::small_icons: return small_icons;
		default: ;
		}

		return dialog;
	}

	void update_scale_factor(const double scale_factor_in)
	{
		if (!df::equiv(scale_factor, scale_factor_in))
		{
			scale_factor = scale_factor_in;
			update_fonts();
		}
	}

	HBRUSH gdi_brush(uint32_t c) const
	{
		df::assert_true(ui::is_ui_thread());
		c = c & 0xFFFFFF;

		const auto i = cached_gdi_brushes.find(c);

		if (i == cached_gdi_brushes.cend())
		{
			auto* const result = CreateSolidBrush(c);
			// A failed creation is not cached, otherwise the null is returned for the lifetime
			// of the context and every later fill with that colour is silently skipped.
			if (result) cached_gdi_brushes[c] = result;
			return result;
		}

		return i->second;
	}

	int calc_base_font_size() const
	{
		return df::round(scale_factor * global_base_font_size);
	}

	// Combined UI scale used for layout metrics (paddings, gaps, icons). This folds the
	// large-font preference into the DPI scale so that spacing scales proportionally with
	// the text: when large fonts are enabled the whole UI looks like the normal layout
	// zoomed up rather than large text crammed into normal-sized gaps.
	double calc_ui_scale_factor() const
	{
		return scale_factor * global_base_font_size / static_cast<double>(normal_font_size);
	}

	void update_fonts()
	{
		delete_fonts();

		const auto bds = calc_base_font_size();
		code = create_font(ui::style::font_face::code, bds);
		dialog = create_font(ui::style::font_face::dialog, bds);
		title = create_font(ui::style::font_face::title, bds);
		mega = create_font(ui::style::font_face::mega, bds);
		icons = create_font(ui::style::font_face::icons, bds);
		small_icons = create_font(ui::style::font_face::small_icons, bds);

		// Hand the new generation to every window still holding one from the old generation.
		// Without this the handles just deleted stay live in child dialogs, bubbles and every
		// control inside them, none of which are reached by the layout-side refresh sweep.
		forget_dead_windows();

		for (const auto& e : fonted_windows)
		{
			SetFont(e.first, font(e.second));
		}
	}
};

using owner_context_ptr = std::shared_ptr<owner_context>;

void draw_icon(const HDC hdc, const owner_context_ptr& ctx, const icon_index icon, const recti bounds,
               const COLORREF clr)
{
	const wchar_t sz[2]{static_cast<wchar_t>(icon), 0};

	const auto smaller_icon = icon == icon_index::minimize || icon == icon_index::maximize || icon ==
		icon_index::restore || icon == icon_index::close;
	auto* const font = smaller_icon ? ctx->small_icons : ctx->icons;
	auto* const old_font = SelectObject(hdc, font);

	SIZE extent;

	if (GetTextExtentPoint32(hdc, sz, 1, &extent))
	{
		SetTextColor(hdc, clr);
		SetBkMode(hdc, TRANSPARENT);

		const auto x = (bounds.left + bounds.right - extent.cy) / 2;
		const auto y = (bounds.top + bounds.bottom - extent.cy) / 2;

		if ((static_cast<uint32_t>(icon) & 0x10000) != 0)
		{
			const HDC bm_hdc = CreateCompatibleDC(hdc);

			if (bm_hdc)
			{
				auto* const old_bm_font = SelectObject(bm_hdc, font);
				const auto cx = extent.cx;
				const auto cy = extent.cy;
				const auto src_stride = static_cast<size_t>(cx) * 4_z;

				// cy is a divisor in the stride overflow test, so reject non-positive extents first.
				const auto extents_are_usable = cx > 0 && cy > 0 && src_stride <= SIZE_MAX / static_cast<size_t>(cy);

				BITMAPINFO bmi = {};
				bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
				bmi.bmiHeader.biWidth = cx;
				bmi.bmiHeader.biHeight = -static_cast<int>(cy);
				bmi.bmiHeader.biPlanes = 1;
				bmi.bmiHeader.biCompression = BI_RGB;
				bmi.bmiHeader.biBitCount = 32;

				uint8_t* dibBits = nullptr;
				const auto hdib = extents_are_usable
					                  ? CreateDIBSection(bm_hdc, &bmi, DIB_RGB_COLORS,
					                                     std::bit_cast<void**>(&dibBits), nullptr, 0)
					                  : nullptr;

				if (hdib && dibBits)
				{
					const auto hbm_old = SelectObject(bm_hdc, hdib);

					for (auto y = 0; y < cy; y++)
					{
						memset(dibBits + src_stride * y, 0, src_stride);
					}

					SetTextColor(bm_hdc, 0xffffff);
					SetBkMode(bm_hdc, TRANSPARENT);

					const RECT bounds{0, 0, cx, cy};

					if (ExtTextOut(bm_hdc, 0, 0, 0, &bounds, sz, 1, nullptr))
					{
						const auto rr = ui::get_r(clr);
						const auto gg = ui::get_g(clr);
						const auto bb = ui::get_b(clr);

						for (auto yy = 0; yy < cy; yy++)
						{
							const auto line = std::bit_cast<uint32_t*>(dibBits + src_stride * yy);

							for (auto xx = 0; xx < cx / 2; xx++)
							{
								std::swap(line[xx], line[cx - (1 + xx)]);
							}

							for (auto xx = 0; xx < cx; xx++)
							{
								const auto cc = line[xx];
								const auto r = ui::get_r(cc);
								const auto g = ui::get_g(cc);
								const auto b = ui::get_b(cc);
								const auto a = (r + g + b) / 3;
								line[xx] = ui::rgba(rr * a / 255, gg * a / 255, bb * a / 255, a * a / 255);
							}
						}
					}

					constexpr BLENDFUNCTION bf = {AC_SRC_OVER, 0, 0xFF, AC_SRC_ALPHA};
					AlphaBlend(hdc, x, y, cx, cy, bm_hdc, 0, 0, cx, cy, bf);

					SelectObject(bm_hdc, hbm_old);
					SelectObject(bm_hdc, old_bm_font);
					DeleteObject(hdib);
				}

				DeleteDC(bm_hdc);
			}
		}
		else
		{
			ExtTextOut(hdc, x, y, 0, nullptr, sz, 1, nullptr);
		}
	}

	SelectObject(hdc, old_font);
}

static void fill_solid_rect(const HDC hdc, const LPCRECT lpRect, const COLORREF clr)
{
	const auto clr_old = SetBkColor(hdc, clr);

	if (clr_old != CLR_INVALID)
	{
		::ExtTextOut(hdc, 0, 0, ETO_OPAQUE, lpRect, nullptr, 0, nullptr);
		SetBkColor(hdc, clr_old);
	}
}

static void fill_solid_rect(const HDC hdc, const int x, const int y, const int cx, const int cy, const COLORREF clr)
{
	const RECT r = {x, y, x + cx, y + cy};
	fill_solid_rect(hdc, &r, clr);
}

static void frame_rect(const HDC hdc, const int x, const int y, const int cx, const int cy, const COLORREF clrTopLeft,
                       COLORREF clrBottomRight = 0,
                       const int width = 1)
{
	if (clrBottomRight == 0) clrBottomRight = clrTopLeft;

	fill_solid_rect(hdc, x, y, cx - width, width, clrTopLeft);
	fill_solid_rect(hdc, x, y, width, cy - width, clrTopLeft);
	fill_solid_rect(hdc, x + cx, y, -width, cy, clrBottomRight);
	fill_solid_rect(hdc, x, y + cy, cx, -width, clrBottomRight);
}


static void frame_rect(const HDC hdc, const LPCRECT lpRect, const COLORREF clrTopLeft,
                       const COLORREF clrBottomRight = 0, int width = 1)
{
	frame_rect(hdc, lpRect->left, lpRect->top, lpRect->right - lpRect->left,
	           lpRect->bottom - lpRect->top, clrTopLeft, clrBottomRight);
}

static int toolbar_GetButtonInfo(const HWND hwnd, const int nID, LPTBBUTTONINFO lptbbi)
{
	df::assert_true(IsWindow(hwnd));
	return static_cast<int>(::SendMessage(hwnd, TB_GETBUTTONINFO, nID, (LPARAM)lptbbi));
}

static int toolbar_GetButtonCount(const HWND hwnd)
{
	df::assert_true(IsWindow(hwnd));
	return static_cast<int>(::SendMessage(hwnd, TB_BUTTONCOUNT, 0, 0L));
}

BOOL toolbar_GetItemRect(const HWND hwnd, const int nIndex, LPRECT lpRect)
{
	df::assert_true(IsWindow(hwnd));
	return static_cast<BOOL>(::SendMessage(hwnd, TB_GETITEMRECT, nIndex, (LPARAM)lpRect));
}

uint32_t toolbar_CommandToIndex(const HWND hwnd, const uint32_t nID)
{
	df::assert_true(IsWindow(hwnd));
	return static_cast<uint32_t>(::SendMessage(hwnd, TB_COMMANDTOINDEX, nID, 0L));
}

static void erase_toolbar_seperators(const HWND tb, const HDC dc, const COLORREF bg_clr)
{
	const int count = toolbar_GetButtonCount(tb);

	win_rect r;
	GetClientRect(tb, &r); //get window rect of control relative to screen				

	auto* const clip = CreateRectRgn(0, 0, r.width(), r.height());

	if (!clip)
		return;

	for (int i = 0; i < count; ++i)
	{
		win_rect r;
		if (toolbar_GetItemRect(tb, i, r) && r.width() > 16)
		{
			auto* const rr = CreateRectRgn(r.left, r.top, r.right, r.bottom);

			if (rr)
			{
				CombineRgn(clip, clip, rr, RGN_XOR);
				DeleteObject(rr);
			}
		}
	}

	auto* const bg_brush = CreateSolidBrush(bg_clr);

	if (bg_brush)
	{
		FillRgn(dc, clip, bg_brush);
		DeleteObject(bg_brush);
	}

	DeleteObject(clip);
}

static void draw_toolbar_button(const ui::command_ptr& command, const owner_context_ptr& ctx,
                                const LPNMTBCUSTOMDRAW lpTBCustomDraw, const COLORREF bg_clr, const COLORREF text_clr,
                                const COLORREF selected_clr)
{
	const win_rect button_rect = lpTBCustomDraw->nmcd.rc;
	auto* const tb = lpTBCustomDraw->nmcd.hdr.hwndFrom;
	auto* const hdc = lpTBCustomDraw->nmcd.hdc;

	constexpr int cchText = 200;
	wchar_t szText[cchText] = {0};

	TBBUTTONINFO button_info = {0};
	button_info.cbSize = sizeof(TBBUTTONINFO);
	button_info.dwMask = TBIF_TEXT | TBIF_IMAGE | TBIF_STYLE | TBIF_STATE;
	button_info.pszText = szText;
	button_info.cchText = cchText;
	const auto result = toolbar_GetButtonInfo(tb, static_cast<int>(lpTBCustomDraw->nmcd.dwItemSpec), &button_info);

	// Ensure the text is properly null-terminated in case of truncation
	if (result > 0 && button_info.cchText > 0)
	{
		szText[cchText - 1] = L'\0';
	}

	const uint32_t item_state = lpTBCustomDraw->nmcd.uItemState;
	const bool is_selected = (item_state & ODS_SELECTED) != 0;
	const bool is_hotlight = (item_state & ODS_HOTLIGHT) != 0;
	const bool is_focus = is_hotlight && GetFocus() == tb;
	const bool is_checked = (button_info.fsState & TBSTATE_CHECKED) != 0;
	const bool is_pressed = (button_info.fsState & TBSTATE_PRESSED) != 0;
	const bool is_disabled = (button_info.fsState & TBSTATE_ENABLED) == 0;
	const bool is_drop_down = (button_info.fsStyle & TBSTYLE_DROPDOWN) != 0;
	const bool is_drop_whole = (button_info.fsStyle & BTNS_WHOLEDROPDOWN) != 0;

	const auto is_highlight = command && command->highlight && !is_disabled;
	const auto accent_bg = ui::style::color::important_background;
	const auto clr_normal_bg = is_highlight ? accent_bg : bg_clr;
	const auto clr_checked_bg = ui::darken(clr_normal_bg, 0.22f);
	const auto clr_selected_bg = is_highlight ? ui::darken(accent_bg, 0.22f) : selected_clr;
	const auto clr_hover_bg = ui::lighten(clr_normal_bg, 0.33f);
	auto draw_clr = text_clr;
	auto icon_bg_clr = clr_normal_bg;

	const auto icon = static_cast<icon_index>(button_info.iImage);
	const auto pos_width = button_rect.width();
	const auto pos_height = button_rect.height();

	auto* mem_dc = CreateCompatibleDC(hdc);

	if (mem_dc)
	{
		auto* mem_bm = CreateCompatibleBitmap(hdc, pos_width, pos_height);

		if (mem_bm)
		{
			auto* const old_bitmap = SelectObject(mem_dc, mem_bm);
			auto* const font_old = SelectObject(mem_dc, ctx->dialog);

			const win_rect bounds(0, 0, pos_width, pos_height);

			if (is_disabled)
			{
				fill_solid_rect(mem_dc, bounds, bg_clr);
				draw_clr = ui::average(text_clr, bg_clr);
			}
			else if (is_pressed)
			{
				fill_solid_rect(mem_dc, bounds, clr_selected_bg);
				frame_rect(mem_dc, bounds, ui::emphasize(clr_selected_bg));
				draw_clr = text_clr;
				icon_bg_clr = clr_selected_bg;
			}
			else if (is_focus)
			{
				fill_solid_rect(mem_dc, bounds, clr_selected_bg);
				frame_rect(mem_dc, bounds, ui::emphasize(clr_selected_bg));
				draw_clr = text_clr;
				icon_bg_clr = clr_selected_bg;
			}
			else if (is_hotlight)
			{
				fill_solid_rect(mem_dc, bounds, clr_hover_bg);
				frame_rect(mem_dc, bounds, ui::emphasize(clr_hover_bg));
				draw_clr = text_clr;
				icon_bg_clr = clr_hover_bg;
			}
			else if (is_checked)
			{
				fill_solid_rect(mem_dc, bounds, clr_checked_bg);
				frame_rect(mem_dc, bounds, ui::emphasize(clr_checked_bg));
				draw_clr = text_clr;
			}
			else
			{
				fill_solid_rect(mem_dc, bounds, clr_normal_bg);
				draw_clr = text_clr;
			}

			SIZE text_extent{0, 0};
			SIZE icon_extent{0, 0};

			const auto avail_width = bounds.width();
			const auto has_text = !str::is_empty(button_info.pszText);
			const auto text_len = str::len(button_info.pszText);
			const auto has_image = button_info.iImage != I_IMAGENONE;

			if (has_text)
			{
				GetTextExtentPoint(mem_dc, button_info.pszText, static_cast<int>(text_len), &text_extent);
			}

			if (has_image)
			{
				const auto icon_cxy = calc_icon_cxy(ctx->scale_factor);
				icon_extent.cx = icon_cxy;
				icon_extent.cy = icon_cxy;
			}

			int x = bounds.left + (avail_width - pos_width) / 2;

			if (has_image)
			{
				constexpr auto x_padding = 3;
				auto icon_bounds = bounds;

				if (has_text)
				{
					icon_bounds.left += x_padding;
					icon_bounds.right = icon_bounds.left + icon_extent.cx;
				}

				draw_icon(mem_dc, ctx, icon, icon_bounds, draw_clr);
				x = icon_bounds.right + x_padding;
			}

			if (has_text)
			{
				SetTextColor(mem_dc, draw_clr);
				SetBkMode(mem_dc, TRANSPARENT);

				const auto y = (bounds.top + bounds.bottom - text_extent.cy) / 2;
				const auto xx = has_image ? x : (bounds.left + bounds.right - text_extent.cx) / 2;
				ExtTextOut(mem_dc, xx, y, ETO_CLIPPED, bounds, button_info.pszText, static_cast<uint32_t>(text_len),
				           nullptr);
			}

			BitBlt(hdc, button_rect.left, button_rect.top, pos_width, pos_height, mem_dc, 0, 0, SRCCOPY);
			SelectObject(mem_dc, old_bitmap);
			SelectObject(mem_dc, font_old);
			DeleteObject(mem_bm);
		}

		DeleteDC(mem_dc);
	}
}

static void draw_menu_item(const ui::command_ptr& command, const LPDRAWITEMSTRUCT lpDrawItemStruct,
                           const owner_context_ptr& ctx)
{
	const int padding = df::round(2 * ctx->scale_factor);

	const HDC dc = lpDrawItemStruct->hDC;
	const win_rect item_bounds = lpDrawItemStruct->rcItem;
	const auto menu_background = ui::style::color::menu_background;

	if (command == nullptr)
	{
		auto sep_bounds = item_bounds;
		sep_bounds.bottom = sep_bounds.top = (sep_bounds.bottom + sep_bounds.top) / 2;

		const auto cx = df::round(8 * ctx->scale_factor);
		const auto cy = df::round(1 * ctx->scale_factor);

		fill_solid_rect(dc, item_bounds, menu_background);
		fill_solid_rect(dc, sep_bounds.inflate(-cx, cy),
		                ui::average(menu_background, ui::style::color::view_background));
	}
	else
	{
		const auto is_disabled = lpDrawItemStruct->itemState & (ODS_GRAYED | ODS_DISABLED);
		const auto is_selected = lpDrawItemStruct->itemState & ODS_SELECTED;
		const auto is_checked = lpDrawItemStruct->itemState & ODS_CHECKED;
		const auto menu_color = command->clr;
		const auto icon = is_checked && command->icon == icon_index::none ? icon_index::check : command->icon;
		const auto icon_cxy = calc_icon_cxy(ctx->scale_factor);
		const auto button_width = icon_cxy + padding * 2;
		const auto menu_text_base = menu_color
			                            ? ui::average(menu_color, ui::style::color::menu_text)
			                            : ui::style::color::menu_text;
		const auto text_clr = is_disabled ? ui::average(menu_background, menu_text_base) : menu_text_base;
		auto bg_clr = menu_background;

		if (is_selected)
		{
			bg_clr = ui::lighten(menu_background, 0.22f);
		}

		fill_solid_rect(dc, item_bounds, bg_clr);

		if (icon != icon_index::none)
		{
			const recti icon_bounds(item_bounds.left, item_bounds.top, item_bounds.left + button_width,
			                        item_bounds.bottom);

			if (is_checked)
			{
				fill_solid_rect(dc, win_rect(icon_bounds), ui::average(menu_background, menu_text_base));
			}

			draw_icon(dc, ctx, icon, icon_bounds, text_clr);
		}

		auto text_bounds = item_bounds;
		text_bounds.left += button_width + padding;
		text_bounds.right -= button_width;

		auto* const old_font = SelectObject(dc, ctx->dialog);
		SetBkMode(dc, TRANSPARENT);
		SetTextColor(dc, text_clr);

		const auto w = str::utf8_to_utf16(command->text);
		DrawText(dc, w.data(), static_cast<int>(w.size()), &text_bounds, DT_SINGLELINE | DT_LEFT | DT_VCENTER);

		if (!str::is_empty(command->keyboard_accelerator_text))
		{
			SetTextColor(dc, ui::style::color::menu_shortcut_text);
			const auto w = str::utf8_to_utf16(command->keyboard_accelerator_text);
			DrawText(dc, w.data(), static_cast<int>(w.size()), &text_bounds, DT_SINGLELINE | DT_RIGHT | DT_VCENTER);
		}

		SelectObject(dc, old_font);
	}
}


static recti get_client_rect(const HWND h)
{
	win_rect r;
	GetClientRect(h, r);
	return r;
}

recti desktop_bounds_impl(const HWND hwnd, const bool work_area)
{
	auto* const monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

	if (monitor)
	{
		MONITORINFO monitorInfo;
		monitorInfo.cbSize = sizeof(MONITORINFO);

		if (GetMonitorInfo(monitor, &monitorInfo))
		{
			return win_rect(work_area ? monitorInfo.rcWork : monitorInfo.rcMonitor);
		}
	}

	WINDOWPLACEMENT wpDesktop;
	wpDesktop.length = sizeof(wpDesktop);
	GetWindowPlacement(GetDesktopWindow(), &wpDesktop);
	return win_rect(wpDesktop.rcNormalPosition);
}

recti ui::desktop_bounds(const bool work_area)
{
	return desktop_bounds_impl(app_wnd(), work_area);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// LockWindowUpdate is deliberately absent: see docs/rendering.md, "APIs not used for resize
// flicker". It discards drawing while locked and flashes the accumulated region on unlock.

struct win_base
{
	HWND m_hWnd = nullptr;
};

class win_impl : public win_base
{
public:
	virtual ~win_impl()
	{
		if (m_hWnd && IsWindow(m_hWnd))
		{
			df::log(__FUNCTION__, "Destroying win_base of valid window");
			SetWindowLongPtr(m_hWnd, GWLP_USERDATA, 0);
			m_hWnd = nullptr; // Prevent double destruction
		}
	}

	virtual LRESULT on_window_message(const HWND hWnd, const UINT uMsg, const WPARAM wParam, const LPARAM lParam)
	{
		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}

	static LRESULT CALLBACK stProcessWindowMessage(const HWND hwnd, const UINT uMsg, const WPARAM wParam,
	                                               const LPARAM lParam)
	{
		if (uMsg == WM_NCCREATE)
		{
			// Safer pointer handling with validation
			const auto* lpCreate = reinterpret_cast<LPCREATESTRUCT>(lParam);
			if (lpCreate && lpCreate->lpCreateParams)
			{
				const auto pt = static_cast<win_impl*>(lpCreate->lpCreateParams);
				const auto ptr = reinterpret_cast<LONG_PTR>(lpCreate->lpCreateParams);
				// get the pointer to the window from lpCreateParams which was set in CreateWindow
				SetWindowLongPtr(hwnd, GWLP_USERDATA, ptr);

				if (pt)
				{
					pt->m_hWnd = hwnd;
				}
			}
		}

		// get the pointer to the window
		const auto ptr = reinterpret_cast<win_impl*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

		if (ptr)
		{
			return ptr->on_window_message(hwnd, uMsg, wParam, lParam);
		}
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}

	static bool register_class(const UINT style, const HICON hIcon, const HCURSOR hCursor, const HBRUSH hbrBackground,
	                           const LPCWSTR lpszMenuName, const LPCWSTR lpszClassName, const HICON hIconSm)
	{
		WNDCLASSEX wcx;
		wcx.cbSize = sizeof(WNDCLASSEX); // size of structure
		wcx.style = style; // redraw if size changes
		wcx.lpfnWndProc = stProcessWindowMessage; // points to window procedure
		wcx.cbClsExtra = 0; // no extra class memory
		wcx.cbWndExtra = 0; // no extra window memory
		wcx.hInstance = get_resource_instance; // handle to instance
		wcx.hIcon = hIcon; // predefined app. icon
		wcx.hCursor = hCursor; // predefined arrow
		wcx.hbrBackground = hbrBackground; // white background brush
		wcx.lpszMenuName = lpszMenuName; // name of menu resource
		wcx.lpszClassName = lpszClassName; // name of window class
		wcx.hIconSm = hIconSm;

		if (RegisterClassEx(&wcx) == 0)
		{
			if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
			{
				return false;
			}
		}

		return true;
	}
};

// Native common controls reach the screen in stages: comctl32 erases the client area with the
// parent background and only then draws the channel, thumb, separators, buttons or text over it.
// A window resize or a splitter drag moves and resizes every control in a panel, so each control
// is composited while only the erase has happened and the panel reads as flashing. Drawing the
// control into a memory bitmap and blitting once means the intermediate state never reaches the
// screen, which removes the flash rather than merely shortening it.
class buffered_control_paint
{
	HDC _dc = nullptr;
	HBITMAP _bitmap = nullptr;
	HGDIOBJ _old_bitmap = nullptr;
	sizei _extent;

	void free_buffer()
	{
		if (_dc)
		{
			if (_old_bitmap) SelectObject(_dc, _old_bitmap);
			DeleteDC(_dc);
		}

		if (_bitmap) DeleteObject(_bitmap);

		_dc = nullptr;
		_bitmap = nullptr;
		_old_bitmap = nullptr;
		_extent = {};
	}

	bool ensure_buffer(const HDC target, const sizei extent)
	{
		if (_dc && _extent == extent) return true;

		free_buffer();

		if (extent.cx < 1 || extent.cy < 1) return false;

		_dc = CreateCompatibleDC(target);
		if (!_dc) return false;

		_bitmap = CreateCompatibleBitmap(target, extent.cx, extent.cy);

		if (!_bitmap)
		{
			free_buffer();
			return false;
		}

		_old_bitmap = SelectObject(_dc, _bitmap);
		_extent = extent;
		return true;
	}

	LRESULT paint(const HWND h)
	{
		PAINTSTRUCT ps = {};
		const auto screen_dc = BeginPaint(h, &ps);
		if (!screen_dc) return 0;

		win_rect client;
		GetClientRect(h, client);

		// Without a buffer the control still has to paint, so fall back to the screen DC. That is
		// the pre-existing behaviour, only reached when GDI cannot allocate the bitmap.
		const auto buffered = ensure_buffer(screen_dc, {client.width(), client.height()});
		const auto target = buffered ? _dc : screen_dc;

		if (buffered)
		{
			// The brush and text colors the control would have been given by its own erase, so the
			// buffered result is the control's normal appearance and not a re-themed one.
			const auto brush = std::bit_cast<HBRUSH>(SendMessage(GetParent(h), WM_CTLCOLORSTATIC,
			                                                     std::bit_cast<WPARAM>(target),
			                                                     std::bit_cast<LPARAM>(h)));
			FillRect(target, client, brush ? brush : GetSysColorBrush(COLOR_BTNFACE));
		}

		DefSubclassProc(h, WM_PRINTCLIENT, std::bit_cast<WPARAM>(target), PRF_CLIENT);

		if (buffered)
		{
			const win_rect paint_bounds(ps.rcPaint);
			BitBlt(screen_dc, paint_bounds.left, paint_bounds.top, paint_bounds.width(), paint_bounds.height(),
			       _dc, paint_bounds.left, paint_bounds.top, SRCCOPY);
		}

		EndPaint(h, &ps);
		return 0;
	}

	static LRESULT CALLBACK proc(const HWND h, const UINT msg, const WPARAM wparam, const LPARAM lparam,
	                             const UINT_PTR id, const DWORD_PTR ref)
	{
		const auto self = std::bit_cast<buffered_control_paint*>(ref);

		if (msg == WM_ERASEBKGND) return 1;
		if (msg == WM_PAINT) return self->paint(h);

		if (msg == WM_NCDESTROY)
		{
			const auto result = DefSubclassProc(h, msg, wparam, lparam);
			RemoveWindowSubclass(h, proc, id);
			delete self;
			return result;
		}

		return DefSubclassProc(h, msg, wparam, lparam);
	}

public:
	// The buffer lives for the control's lifetime, so it is only released here.
	~buffered_control_paint()
	{
		free_buffer();
	}

	static void attach(const HWND h)
	{
		df::assert_true(IsWindow(h));

		auto* const self = new buffered_control_paint();

		if (!SetWindowSubclass(h, proc, 0, std::bit_cast<DWORD_PTR>(self)))
		{
			delete self;
		}
	}
};


template <class T, class ui_base, class TBase>
class control_base_impl :
	public TBase,
	public ui_base
{
public:
	HWND hwnd() const
	{
		auto t = static_cast<const T*>(this);
		auto h = t->m_hWnd;
		df::assert_true(IsWindow(h));
		return h;
	}

	std::any handle() const override
	{
		return hwnd();
	}

	void enable(const bool enable) override { EnableWindow(hwnd(), enable); }
	std::string window_text() const override { return ::window_text(hwnd()); }

	void window_text(const std::string_view text) override
	{
		const auto w = str::utf8_to_utf16(text);
		::SetWindowText(hwnd(), w.c_str());
	}

	sizei measure(int cx) const override
	{
		win_rect r;
		GetClientRect(hwnd(), &r);
		return {r.width(), r.height()};
	}

	void focus() override
	{
		SetFocus(hwnd());
	}

	bool is_visible() const override
	{
		const auto wnd = hwnd();

		return IsWindowVisible(wnd) != 0
			&& IsIconic(wnd) == 0;
	}

	bool has_focus() const override
	{
		return GetFocus() == hwnd();
	}

	recti window_bounds() const override
	{
		win_rect r;
		GetWindowRect(hwnd(), &r);
		return r;
	}

	void options_changed() override
	{
		auto t = static_cast<const T*>(this);
		t->_ctx->set_window_font(hwnd(), ui::style::font_face::dialog);
	}

	void show(const bool show) override { ShowWindow(hwnd(), show ? SW_SHOW : SW_HIDE); };

	void window_bounds(const recti bounds, const bool visible) override
	{
		SetWindowPos(hwnd(), nullptr, bounds.left, bounds.top, bounds.width(), bounds.height(),
		             SWP_NOACTIVATE | (visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
	}
};


static void scroll_impl(const HWND hwnd, const int dx, const int dy, const recti bounds,
                        const bool scroll_child_controls)
{
	auto flags = SW_INVALIDATE | SW_ERASE;
	if (scroll_child_controls) flags |= SW_SCROLLCHILDREN;

	if (bounds.is_empty())
	{
		ScrollWindowEx(hwnd, dx, dy, nullptr, nullptr, nullptr, nullptr, flags);
	}
	else
	{
		win_rect win_bounds(bounds);
		const auto* const prc_scroll = scroll_child_controls ? nullptr : static_cast<LPCRECT>(win_bounds);
		ScrollWindowEx(hwnd, dx, dy, prc_scroll, win_bounds, nullptr, nullptr, flags);
	}
}

template <class T, class TBase>
class ui_frame_window : public TBase
{
public:
	HCURSOR _cursor = resources.normal;
	uint32_t _disable_count = 0;

	HWND hwnd() const
	{
		auto t = static_cast<const T*>(this);
		auto h = t->m_hWnd;
		df::assert_true(IsWindow(h));
		return h;
	}

	pointi cursor_location() override
	{
		POINT loc = {};
		if (GetCursorPos(&loc))
		{
			ScreenToClient(hwnd(), &loc);
		}
		return {loc.x, loc.y};
	}

	std::any handle() const override
	{
		return hwnd();
	}

	void enable(const bool enable) override
	{
		if (enable)
		{
			df::assert_true(_disable_count > 0);
			if (_disable_count == 0) return;

			--_disable_count;
			if (_disable_count == 0) EnableWindow(hwnd(), true);
		}
		else
		{
			if (_disable_count++ == 0) EnableWindow(hwnd(), false);
		}
	}

	void sync_enabled()
	{
		EnableWindow(hwnd(), _disable_count == 0);
	}

	std::string window_text() const override
	{
		return ::window_text(hwnd());
	}

	void window_text(const std::string_view text) override
	{
		const auto w = str::utf8_to_utf16(text);
		::SetWindowText(hwnd(), w.c_str());
	}

	void focus() override
	{
		SetFocus(hwnd());
	}

	sizei measure(int cx) const override
	{
		return {};
	}

	bool is_visible() const override
	{
		return IsWindowVisible(hwnd()) != 0;
	}

	bool has_focus() const override
	{
		return GetFocus() == hwnd();
	}

	recti window_bounds() const override
	{
		win_rect r;
		GetWindowRect(hwnd(), &r);
		return r;
	}

	void window_bounds(const recti bounds, const bool visible) override
	{
		SetWindowPos(hwnd(), nullptr, bounds.left, bounds.top, bounds.width(), bounds.height(),
		             SWP_NOACTIVATE | (visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
	}

	void show(const bool show) override
	{
		ShowWindow(hwnd(), show ? SW_SHOWNOACTIVATE : SW_HIDE);
	}

	void redraw() override
	{
		InvalidateRect(hwnd(), nullptr, 0);
	}

	void redraw_now() override
	{
		RedrawWindow(hwnd(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
	}

	void invalidate(const recti bounds, const bool erase) override
	{
		if (bounds.is_empty())
		{
			InvalidateRect(hwnd(), nullptr, erase);
		}
		else
		{
			InvalidateRect(hwnd(), win_rect(bounds), erase);
		}
	}

	void scroll(const int dx, const int dy, const recti bounds, const bool scroll_child_controls) override
	{
		auto t = static_cast<T*>(this);
		scroll_impl(t->m_hWnd, dx, dy, bounds, scroll_child_controls);
	}

	void close(bool is_cancel) override
	{
		::PostMessage(hwnd(), WM_CLOSE, 0, 0);
	}

	bool is_enabled() const override
	{
		return _disable_count == 0 && IsWindowEnabled(hwnd()) != 0;
	}

	bool is_maximized() const override
	{
		return IsZoomed(hwnd()) != 0;
	}

	bool is_occluded() const override
	{
		return false;
	}

	void reset_graphics() override
	{
	}

	void options_changed() override
	{
		auto t = static_cast<T*>(this);
		t->_gdi_ctx->set_window_font(hwnd(), ui::style::font_face::dialog);
	}
};

class control_base2
{
public:
	virtual void on_command(const ui::frame_host_weak_ptr& host, const int id, const int code)
	{
	}

	virtual LRESULT on_notify(const ui::frame_host_weak_ptr& host, const ui::color_style& colors, const int id,
	                          const LPNMHDR pnmh)
	{
		return 0;
	}

	virtual void on_scroll(const ui::frame_host_weak_ptr& host, const int code, const int pos)
	{
	}

	virtual ui::color_style calc_colors() const
	{
		return {};
	}

	virtual void dpi_changed()
	{
	}

	bool is_radio = false;
	int radio_group = ui::radio_group_default;
};

class edit_string_enum final : public IEnumString
{
public:
	std::vector<std::wstring> _data;
	std::vector<std::wstring>::const_iterator _walk;
	std::atomic<ULONG> _ref_count = 1;
	bool _delete_on_release = false;

	edit_string_enum() = default;

	edit_string_enum(const std::vector<std::wstring>& data) : _data(data)
	{
		_walk = _data.begin();
	}

	void load(const std::vector<std::string>& data)
	{
		_data.clear();
		_data.reserve(data.size());

		for (const auto& d : data)
		{
			_data.emplace_back(str::utf8_to_utf16(d));
		}

		_walk = _data.begin();
	}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppvObject) override
	{
		if (ppvObject == nullptr) return E_POINTER;

		if (IsEqualGUID(iid, IID_IEnumString) || IsEqualGUID(iid, IID_IUnknown))
		{
			*ppvObject = static_cast<IEnumString*>(this);
			AddRef();
			return S_OK;
		}

		*ppvObject = nullptr;
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return ++_ref_count;
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		const auto refs = --_ref_count;
		if (refs == 0 && _delete_on_release) delete this;
		return refs;
	}

	HRESULT STDMETHODCALLTYPE Next(const ULONG celt, LPOLESTR* rgelt, ULONG* pceltFetched) override
	{
		if (rgelt == nullptr) return E_INVALIDARG;
		ULONG done = 0;
		while (done < celt && _walk != _data.end())
		{
			rgelt[done] = CoStrDup(_walk->c_str());
			++_walk;
			++done;
		}
		if (pceltFetched != nullptr) *pceltFetched = done;
		return done == celt ? S_OK : S_FALSE;
	}

	static wchar_t* CoStrDup(const wchar_t* in)
	{
		const auto lenBytes = (wcslen(in) + 1) * sizeof(wchar_t);
		auto* result = static_cast<wchar_t*>(CoTaskMemAlloc(lenBytes));
		if (result) memcpy(result, in, lenBytes);
		return result;
	}

	HRESULT STDMETHODCALLTYPE Skip(ULONG celt) override
	{
		while (celt > 0 && _walk != _data.end())
		{
			--celt;
			++_walk;
		}
		return celt == 0 ? S_OK : S_FALSE;
	}

	HRESULT STDMETHODCALLTYPE Reset() override
	{
		_walk = _data.begin();
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE Clone(IEnumString** ppenum) override
	{
		if (ppenum == nullptr) return E_POINTER;
		const auto offset = std::distance(_data.cbegin(), _walk);
		auto* clone = new(std::nothrow) edit_string_enum(_data);
		if (clone == nullptr)
		{
			*ppenum = nullptr;
			return E_OUTOFMEMORY;
		}
		clone->_walk = clone->_data.cbegin() + offset;
		clone->_delete_on_release = true;
		*ppenum = clone;
		return S_OK;
	}
};

class edit_impl final :
	public control_base_impl<edit_impl, ui::edit, win_base>,
	public control_base2,
	public std::enable_shared_from_this<edit_impl>
{
	using base_class = control_base_impl<edit_impl, edit, win_base>;

	struct unknown_word
	{
		std::string word;
		int pos_start = 0;
		int pos_end = 0;

		// line_height is passed in because measuring it costs a GetDC/SelectObject/GetTextMetrics/
		// ReleaseDC round trip, and the caller repeats this per misspelled word on every paint.
		recti calc_bounds(const edit_impl& edit, const int line_height) const
		{
			const POINT loc_start = edit.pos_from_char(pos_start);
			const POINT loc_end = edit.pos_from_char(pos_end);
			if (loc_end.x == -1 || loc_start.x == -1) return {};

			const auto iY = loc_start.y + line_height;
			return {loc_start.x, loc_start.y, loc_end.x, iY};
		}
	};

	const ui::edit_styles _styles;
	UINT_PTR _timerId = -1;
	std::vector<unknown_word> _unknown_words;
	icon_index _icon = icon_index::none;
	ui::color32 _background = ui::style::color::edit_background;
	control_host_impl* _parent = nullptr;
	bool _enabled = true;

protected:
	void add_unknown_word(std::string_view word_a, int word_start, int word_end);
	void update_spelling(const std::wstring& text);
	void highlight_spelling() const;

public:
	std::function<void(const std::string&)> changed;

	// Heap-allocated and ref-counted rather than a by-value member: IAutoComplete::Init keeps a
	// reference, and the autocomplete object is owned by the edit *window*, which can outlive this
	// object. A by-value member would leave the shell holding a pointer into freed memory.
	ComPtr<edit_string_enum> string_enum;
	ComPtr<IAutoComplete> _auto_complete;
	owner_context_ptr _ctx;

	explicit edit_impl(ui::edit_styles styles, control_host_impl* parent, const owner_context_ptr& ctx)
		: _styles(std::move(styles)), _parent(parent), _ctx(ctx)
	{
		auto* const e = new edit_string_enum();
		e->_delete_on_release = true;
		string_enum.Attach(e); // Attach: the constructor already starts the count at one.
	}

	~edit_impl() override
	{
		// Stop the shell's autocomplete message hook before the edit goes away.
		if (_auto_complete)
		{
			ComPtr<IAutoComplete2> ac2;
			if (SUCCEEDED(_auto_complete.As(&ac2))) ac2->Enable(FALSE);
			_auto_complete.Reset();
		}

		// SuperProc dereferences this object, so the subclass must be detached before it dies even
		// if the edit window happens to outlive it.
		if (m_hWnd && IsWindow(m_hWnd))
		{
			RemoveWindowSubclass(m_hWnd, SuperProc, 0);
		}
	}

	void on_command(const ui::frame_host_weak_ptr& host, const int id, const int code) override
	{
		if (code == EN_SETFOCUS ||
			code == EN_KILLFOCUS)
		{
			const auto has_focus = code == EN_SETFOCUS;

			if (_styles.select_all_on_focus && has_focus)
			{
				// post select_all
				::PostMessage(m_hWnd, EM_SETSEL, 0, -1);
			}

			const auto h = host.lock();
			if (h) h->focus_changed(has_focus, shared_from_this());
		}
		else if (code == EN_CHANGE)
		{
			if (changed || _styles.spelling)
			{
				const auto text = window_text_w(m_hWnd);

				if (changed)
				{
					changed(str::utf16_to_utf8(text));
				}

				if (_styles.spelling)
				{
					update_spelling(text);
				}
			}
		}
		else if (code == EN_VSCROLL)
		{
		}
	}

	void dpi_changed() override
	{
		// The configured face, not `dialog` - a code- or title-faced edit must not silently revert.
		_ctx->set_window_font(m_hWnd, _styles.font);
	}

	static LRESULT CALLBACK SuperProc(const HWND hWnd, const UINT uMsg, const WPARAM wParam, const LPARAM lParam,
	                                  UINT_PTR uIdSubclass,
	                                  const DWORD_PTR dwRefData)
	{
		const auto pt = std::bit_cast<edit_impl*>(dwRefData);

		if (uMsg == WM_NCCALCSIZE) return pt->on_window_nc_calc_size(uMsg, wParam, lParam);
		if (uMsg == WM_NCPAINT) return pt->on_window_nc_paint(uMsg, wParam, lParam);
		if (uMsg == WM_PAINT) return pt->on_window_paint(uMsg, wParam, lParam);
		if (uMsg == WM_WINDOWPOSCHANGED) return pt->on_window_pos_changed(uMsg, wParam, lParam);
		if (uMsg == WM_CONTEXTMENU) return pt->on_window_context_menu(uMsg, wParam, lParam);
		if (uMsg == WM_GETDLGCODE) return pt->on_window_get_dlg_code(uMsg, wParam, lParam);
		if (uMsg == WM_IME_NOTIFY) return pt->on_window_ime_notify(uMsg, wParam, lParam);
		if (uMsg == WM_KEYDOWN) return pt->on_window_key_down(uMsg, wParam, lParam);
		if (uMsg == WM_ENABLE) return pt->on_window_enable(uMsg, wParam, lParam);
		if (uMsg == WM_MOUSEWHEEL) return pt->on_mouse_wheel(uMsg, wParam, lParam);

		return DefSubclassProc(hWnd, uMsg, wParam, lParam);
	}

	HWND Create(const HWND hWndParent, const std::string_view text, const uintptr_t id)
	{
		auto style = WS_CHILD | WS_TABSTOP;
		if (_styles.align_center) style |= ES_CENTER;
		if (_styles.number) style |= ES_NUMBER;
		if (_styles.password) style |= ES_PASSWORD;
		if (_styles.vertical_scroll) style |= ES_AUTOVSCROLL;
		if (_styles.horizontal_scroll) style |= ES_AUTOHSCROLL;
		if (_styles.multi_line) style |= ES_MULTILINE;
		if (_styles.want_return) style |= ES_WANTRETURN;

		const auto w = str::utf8_to_utf16(text);
		const auto h = CreateWindowEx(
			0, L"EDIT", // predefined class 
			w.c_str(), // no window title 
			style,
			0, 0, 0, 0, // set size in WM_SIZE message 
			hWndParent, // parent window 
			std::bit_cast<HMENU>(id), // edit control ID 
			get_resource_instance,
			nullptr); // pointer not needed

		const auto result = m_hWnd = h;

		SetWindowSubclass(m_hWnd, SuperProc, 0, std::bit_cast<DWORD_PTR>(this));

		if (_styles.spelling)
		{
			spell.lazy_load();
		}

		if (!_styles.cue.empty())
		{
			const auto w = str::utf8_to_utf16(_styles.cue);
			SendMessage(m_hWnd, EM_SETCUEBANNER, 0, reinterpret_cast<LPARAM>(w.c_str()));
		}

		return result;
	}

	POINT pos_from_char(const uint32_t nChar) const
	{
		df::assert_true(IsWindow(m_hWnd));
		const DWORD dwRet = static_cast<DWORD>(::SendMessage(m_hWnd, EM_POSFROMCHAR, nChar, 0));
		const POINT point = {GET_X_LPARAM(dwRet), GET_Y_LPARAM(dwRet)};
		return point;
	}

	BOOL can_undo() const
	{
		df::assert_true(IsWindow(m_hWnd));
		return static_cast<BOOL>(::SendMessage(m_hWnd, EM_CANUNDO, 0, 0L));
	}

	void destroy() override
	{
		DestroyWindow(m_hWnd);
	}

	LRESULT on_window_create(uint32_t uMsg, WPARAM wParam, LPARAM lParam);
	LRESULT on_window_context_menu(uint32_t uMsg, WPARAM wParam, LPARAM lParam);

	LRESULT on_mouse_wheel(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam) const
	{
		SendMessage(GetParent(m_hWnd), uMsg, wParam, lParam);
		return 1;
	}

	LRESULT on_window_get_dlg_code(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam) const
	{
		auto lres = DefSubclassProc(m_hWnd, uMsg, wParam, lParam);
		const auto m = std::bit_cast<MSG*>(lParam);

		lres &= ~DLGC_WANTTAB;

		if (m)
		{
			if (m->message == WM_MOUSEWHEEL)
			{
				lres &= ~DLGC_WANTMESSAGE;
			}

			if (m->message == WM_KEYDOWN)
			{
				switch (m->wParam)
				{
				case VK_TAB:
					lres &= ~DLGC_WANTMESSAGE;
					break;

				case VK_RETURN:
					{
						if (_styles.want_return)
						{
							lres |= DLGC_WANTMESSAGE;
						}
						else
						{
							lres &= ~DLGC_WANTMESSAGE;
						}
					}
					break;
				}
			}
		}

		return lres;
	}

	LRESULT on_window_key_down(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam) const
	{
		if (_styles.capture_key_down)
		{
			if (_styles.capture_key_down(static_cast<int>(wParam), ui::current_key_state()))
			{
				return 0;
			}
		}

		return DefSubclassProc(m_hWnd, uMsg, wParam, lParam);
	}

	LRESULT on_window_paint(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam)
	{
		DefSubclassProc(m_hWnd, uMsg, wParam, lParam);
		if (_styles.spelling)
		{
			highlight_spelling();
		}
		return 0;
	}

	LRESULT on_window_pos_changed(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam)
	{
		const auto result = DefSubclassProc(m_hWnd, uMsg, wParam, lParam);
		const auto* const pos = std::bit_cast<const WINDOWPOS*>(lParam);

		// This control draws its whole appearance in the non-client area, which neither a move nor a
		// resize fully invalidates on its own. Marking the frame rather than painting it here is
		// deliberate: the border and the interior must reach the screen in the same update, not one
		// before the other, so the paint is left for the host to flush with the rest of the layout.
		constexpr auto geometry_unchanged = SWP_NOSIZE | SWP_NOMOVE;

		if (pos && (pos->flags & geometry_unchanged) != geometry_unchanged)
		{
			RedrawWindow(m_hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME);
		}

		return result;
	}

	LRESULT on_window_nc_calc_size(uint32_t /*uMsg*/, WPARAM wParam, const LPARAM lParam) const
	{
		on_window_nc_calc_size((LPRECT)lParam);
		return 0;
	}

	void on_window_nc_calc_size(LPRECT pr) const;
	LRESULT on_window_nc_paint(uint32_t /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/);

	LRESULT on_window_enable(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam)
	{
		_enabled = wParam != 0;
		full_invalidate();
		return DefSubclassProc(m_hWnd, uMsg, wParam, lParam);
	}

	void auto_completes(const std::vector<std::string>& texts) override
	{
		string_enum->load(texts);
	}

	bool init_auto_complete_list();

	void limit_text_len(const int max_len) override
	{
		::SendMessage(m_hWnd, EM_SETLIMITTEXT, max_len, 0L);
	}

	void replace_sel(const std::string_view new_text, const bool add_space_if_append) override
	{
		const auto new_text_w = str::utf8_to_utf16(new_text);
		DWORD selection_start = 0;
		DWORD selection_end = 0;
		SendMessage(m_hWnd, EM_GETSEL, reinterpret_cast<WPARAM>(&selection_start),
		            reinterpret_cast<LPARAM>(&selection_end));
		const auto is_no_selection = selection_start == selection_end;

		if (is_no_selection)
		{
			const auto current_text_len = GetWindowTextLength(m_hWnd);

			if (add_space_if_append && current_text_len > 0)
			{
				SendMessage(m_hWnd, EM_SETSEL, current_text_len, current_text_len);
				SendMessage(m_hWnd, EM_REPLACESEL, TRUE, std::bit_cast<LPARAM>(static_cast<const wchar_t*>(L" ")));
			}

			SendMessage(m_hWnd, EM_SETSEL, current_text_len + 1, current_text_len + 1);
		}

		::SendMessage(m_hWnd, EM_REPLACESEL, TRUE, std::bit_cast<LPARAM>(new_text_w.c_str()));
	}

	void select_all() override
	{
		select(0, -1);
	}

	void select(const int start, const int end) override
	{
		::SendMessage(m_hWnd, EM_SETSEL, start, end);
	}

	void window_text(const std::string_view text) override
	{
		const auto w = str::utf8_to_utf16(text);
		::SetWindowText(m_hWnd, w.c_str());

		const auto index = GetWindowTextLength(m_hWnd);
		SendMessage(m_hWnd, EM_SETSEL, static_cast<WPARAM>(index), index);
		SendMessage(m_hWnd, EM_SCROLLCARET, 0, 0);
	}

	void set_icon(const icon_index i) override
	{
		if (_icon != i)
		{
			_icon = i;
			SetWindowPos(m_hWnd, nullptr, 0, 0, 0, 0,
			             SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
			full_invalidate();
		}
	}

	void options_changed() override
	{
		_ctx->set_window_font(m_hWnd, _styles.font);
		full_invalidate();
	}

	void full_invalidate() const
	{
		RedrawWindow(m_hWnd, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
	}

	void set_background(const ui::color32 bg) override
	{
		if (_background != bg)
		{
			_background = bg;
			full_invalidate(); // invalidate non client area
		}
	}

	void enable(const bool enable) override
	{
		EnableWindow(m_hWnd, enable);
		full_invalidate();
	}

	ui::color_style calc_colors() const override
	{
		const auto is_window_enabled = _enabled;
		const auto ebg = _background;
		const auto efg = ui::style::color::edit_text;
		const auto bg = is_window_enabled ? ebg : ui::average(ebg, ui::style::color::dialog_background);
		const auto fg = is_window_enabled ? efg : ui::average(efg, bg);
		return {bg, fg};
	}

	LRESULT on_window_ime_notify(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam) const
	{
		return DefSubclassProc(m_hWnd, uMsg, wParam, lParam);
	}
};


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool is_word_break(const wchar_t c)
{
	auto result = false;

	if (!iswalnum(c))
	{
		result = c != L'_' && c != L'\'';
	}

	return result;
}

static std::wstring trim(const std::wstring& s)
{
	const auto wsfront = std::ranges::find_if_not(s, [](const int c) { return std::iswspace(c); });
	const auto wsback = std::find_if_not(s.rbegin(), s.rend(), [](const int c) { return std::iswspace(c); }).base();
	return wsback <= wsfront ? std::wstring() : std::wstring(wsfront, wsback);
}

void edit_impl::add_unknown_word(const std::string_view word, const int word_start, const int word_end)
{
	if (_styles.spelling)
	{
		unknown_word err;
		err.word = word;
		err.pos_start = word_start;
		err.pos_end = word_end;
		_unknown_words.emplace_back(err);
	}
}

void edit_impl::update_spelling(const std::wstring& text)
{
	_unknown_words.clear();

	const auto len = static_cast<int>(text.size());

	std::wstring word;
	auto word_start = -1;
	auto i = 0;

	// extract words from line
	while (i < len)
	{
		const auto c = text[i];

		if (is_word_break(c))
		{
			word = trim(word);

			if (!word.empty())
			{
				auto word_a = str::utf16_to_utf8(word);

				if (!spell.is_word_valid(word_a))
				{
					add_unknown_word(word_a, word_start, word_start + static_cast<int>(word.size()));
				}
			}

			word.clear();
			word_start = -1;
		}
		else
		{
			word += c;

			if (word_start == -1)
			{
				word_start = i;
			}
		}

		i++;
	}

	word = trim(word);

	if (!word.empty() && word_start != -1)
	{
		const auto word_a = str::utf16_to_utf8(word);

		if (!spell.is_word_valid(word_a))
		{
			add_unknown_word(word_a, word_start, std::min(word_start + static_cast<int>(word.size()), len - 1));
		}
	}
}

void edit_impl::highlight_spelling() const
{
	if (_styles.spelling && !_unknown_words.empty())
	{
		const auto dc = GetDC(m_hWnd);

		if (dc)
		{
			auto* pen = CreatePen(PS_ALTERNATE, 1, RGB(255, 0, 0));

			if (pen)
			{
				const auto old_pen = SelectObject(dc, pen);
				const auto line_height = gdi_text_line_height(dc, GetFont(m_hWnd));

				for (const auto& word : _unknown_words)
				{
					const auto bounds = word.calc_bounds(*this, line_height);

					if (!bounds.is_empty())
					{
						MoveToEx(dc, bounds.left, bounds.bottom, nullptr);
						LineTo(dc, bounds.right, bounds.bottom);
					}
				}

				SelectObject(dc, old_pen);
				DeleteObject(pen);
			}

			ReleaseDC(m_hWnd, dc);
		}
	}
}


void edit_impl::on_window_nc_calc_size(const LPRECT pr) const
{
	df::assert_true(pr);

	if (pr)
	{
		const auto line_height = gdi_text_line_height(m_hWnd, GetFont(m_hWnd));
		const auto is_multi_line = _styles.multi_line;
		const auto scale_factor = _ctx->scale_factor;
		const auto padding = df::round((_styles.rounded_corners ? 8 : 4) * scale_factor);
		const auto h = pr->bottom - pr->top;
		const auto cx = padding;
		const auto cy = is_multi_line ? padding : (h - line_height) / 2;
		const auto icon_cxy = calc_icon_cxy(scale_factor) + df::round(ui_element_padding * scale_factor);

		pr->left += cx + (_icon == icon_index::none ? 0 : icon_cxy);
		pr->right -= cx;
		pr->top += cy;
		pr->bottom -= cy;
	}
}

LRESULT edit_impl::on_window_nc_paint(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam)
{
	const auto hdc = GetWindowDC(m_hWnd); // (HRGN)wParam, DCX_WINDOW | DCX_INTERSECTRGN);

	if (hdc)
	{
		const bool has_focus = GetFocus() == m_hWnd;
		const auto edit_colors = calc_colors();
		const auto edge_clr = has_focus ? ui::style::color::dialog_selected_background : _styles.bg_clr;
		const auto bg_clr = _styles.bg_clr;
		const auto scale_factor = _ctx->scale_factor;

		win_rect r;
		GetWindowRect(m_hWnd, r);
		const win_rect outside(0, 0, r.width(), r.height());
		win_rect inside = outside;
		on_window_nc_calc_size(inside);

		auto* const clip_rgn = CreateRectRgn(outside.left, outside.top, outside.right, outside.bottom);
		auto* const exclude_rgn = CreateRectRgn(inside.left, inside.top, inside.right, inside.bottom);

		// Under GDI handle exhaustion either region can be null. CombineRgn/SelectClipRgn with a null
		// destination would leave the DC unclipped, so the border draw below would paint over the
		// client area instead of only the non-client frame.
		if (clip_rgn && exclude_rgn)
		{
			CombineRgn(clip_rgn, clip_rgn, exclude_rgn, RGN_XOR);
			SelectClipRgn(hdc, clip_rgn);
		}

		// The frame plus the icon takes several draws. Writing those straight to the window shows each
		// step while a resize is in flight, so compose off-screen and blit once. A DIB section rather
		// than a compatible bitmap so the rounded frame can be rasterised into the pixels directly.
		BITMAPINFO bmi = {};
		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = outside.width();
		bmi.bmiHeader.biHeight = -outside.height();
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;

		uint32_t* buffer_bits = nullptr;
		auto* const buffer_dc = outside.is_empty() ? nullptr : CreateCompatibleDC(hdc);
		auto* const buffer_bm = buffer_dc
			                        ? CreateDIBSection(buffer_dc, &bmi, DIB_RGB_COLORS,
			                                           std::bit_cast<void**>(&buffer_bits), nullptr, 0)
			                        : nullptr;
		auto* const old_buffer_bm = buffer_bm ? SelectObject(buffer_dc, buffer_bm) : nullptr;
		const auto frame_dc = buffer_bm ? buffer_dc : hdc;

		if (_styles.rounded_corners)
		{
			if (buffer_bits)
			{
				fill_round_rect(buffer_bits, outside.width(), outside.height(),
				                static_cast<float>(ui_corner_radius * scale_factor),
				                static_cast<float>(2.6 * scale_factor),
				                edit_colors.background, edge_clr, bg_clr);
			}
			else
			{
				// Without the DIB there is nothing to rasterise into; a square frame beats no frame.
				fill_rect(frame_dc, edit_colors.background, outside);
			}
		}
		else if (has_focus)
		{
			const int padding = df::round(ui_focus_padding * scale_factor);

			// Verticals
			fill_rect(frame_dc, edge_clr, recti(outside.left, outside.top, inside.left + padding, outside.bottom));
			fill_rect(frame_dc, edge_clr, recti(outside.right - padding, outside.top, outside.right, outside.bottom));

			// Horizontals
			fill_rect(frame_dc, edge_clr,
			          recti(outside.left + padding, outside.top, outside.right - padding, outside.top + padding));
			fill_rect(frame_dc, edge_clr, recti(outside.left + padding, outside.bottom - padding,
			                                    outside.right - padding,
			                                    outside.bottom));

			fill_rect(frame_dc, edit_colors.background, outside.inflate(-padding));
		}
		else
		{
			fill_rect(frame_dc, edit_colors.background, outside);
		}

		DeleteObject(clip_rgn);
		DeleteObject(exclude_rgn);

		if (_icon != icon_index::none)
		{
			const auto icon_cxy = calc_icon_cxy(scale_factor);
			auto rr = outside;
			rr.left = df::round(ui_element_padding * scale_factor);
			rr.right = rr.left + icon_cxy;
			draw_icon(frame_dc, _ctx, _icon, rr, ui::style::color::edit_text);
		}

		if (buffer_bm)
		{
			BitBlt(hdc, 0, 0, outside.width(), outside.height(), buffer_dc, 0, 0, SRCCOPY);
			SelectObject(buffer_dc, old_buffer_bm);
			DeleteObject(buffer_bm);
		}

		if (buffer_dc) DeleteDC(buffer_dc);

		ReleaseDC(m_hWnd, hdc);
	}

	return DefSubclassProc(m_hWnd, uMsg, wParam, lParam);
}


class button_impl final :
	public control_base_impl<button_impl, ui::button, win_base>,
	public control_base2,
	public std::enable_shared_from_this<button_impl>
{
public:
	icon_index _icon = icon_index::none;
	uintptr_t _id = 0;
	std::wstring _details;
	std::function<void()> _invoke;
	owner_context_ptr _ctx;

	button_impl(const owner_context_ptr& ctx) : _ctx(ctx)
	{
	}

	HWND Create(const HWND hWndParent, const LPCTSTR szWindowName = nullptr,
	            const DWORD dwStyle = 0, const DWORD dwExStyle = 0,
	            const uintptr_t id = 0U, const LPVOID lpCreateParam = nullptr)
	{
		_id = id;
		m_hWnd = CreateWindowEx(
			dwExStyle, L"BUTTON", // predefined class 
			szWindowName, // no window title 
			dwStyle,
			0, 0, 0, 0, // set size in WM_SIZE message 
			hWndParent, // parent window 
			std::bit_cast<HMENU>(id), // edit control ID 
			get_resource_instance,
			lpCreateParam);

		if (m_hWnd) buffered_control_paint::attach(m_hWnd);

		return m_hWnd;
	}

	void on_command(const ui::frame_host_weak_ptr& host, const int id, const int code) override
	{
		if (_id == id && (code == BN_SETFOCUS || code == BN_KILLFOCUS))
		{
			const auto h = host.lock();
			if (h) h->focus_changed(code == BN_SETFOCUS, shared_from_this());
		}
		else if (code == BN_CLICKED || code == BN_PUSHED || code == BN_UNPUSHED)
		{
			if (_invoke)
			{
				_invoke();
			}
		}
	}

	void dpi_changed() override
	{
		_ctx->set_window_font(m_hWnd, ui::style::font_face::dialog);
		InvalidateRect(m_hWnd, nullptr, TRUE);
	}

	void destroy() override
	{
		_invoke = nullptr;
		DestroyWindow(m_hWnd);
	}

	uint32_t GetButtonStyle() const
	{
		df::assert_true(IsWindow(m_hWnd));
		return static_cast<uint32_t>(::GetWindowLong(m_hWnd, GWL_STYLE)) & 0xFFFF;
	}

	int GetCheck() const
	{
		df::assert_true(IsWindow(m_hWnd));
		return static_cast<int>(::SendMessage(m_hWnd, BM_GETCHECK, 0, 0L));
	}

	void SetCheck(const int nCheck) const
	{
		df::assert_true(IsWindow(m_hWnd));
		::SendMessage(m_hWnd, BM_SETCHECK, nCheck, 0L);
	}

	void set_checked(const bool checked) override
	{
		if (IsWindow(m_hWnd)) SetCheck(checked ? 1 : 0);
	}

	void draw(const LPNMCUSTOMDRAW pCustomDraw) const
	{
		wchar_t text[512];
		::GetWindowText(pCustomDraw->hdr.hwndFrom, text, 512);

		const auto button_padding = df::round(ui_button_padding * _ctx->scale_factor);

		win_rect button_bounds(pCustomDraw->rc);
		auto client_bounds = button_bounds.inflate(-button_padding, -button_padding);

		const auto button_style = GetButtonStyle();
		const auto button_state = pCustomDraw->uItemState;
		const auto is_selected = (button_state & CDIS_SELECTED) != 0;
		const auto is_hotlight = (button_state & CDIS_HOT) != 0;
		const auto is_checked = (button_state & CDIS_CHECKED) != 0;
		const auto is_focused = (button_state & CDIS_FOCUS) != 0;
		const auto is_disabled = (button_state & CDIS_DISABLED) != 0;
		const auto is_default = (button_style & BS_TYPEMASK) == BS_DEFPUSHBUTTON;
		const auto is_radio_button = (button_style & BS_TYPEMASK) == BS_AUTORADIOBUTTON;
		const auto is_check_box = (button_style & BS_TYPEMASK) == BS_AUTOCHECKBOX;

		auto* const dc = pCustomDraw->hdc;
		auto clr_bg = ui::style::color::button_background;
		auto clr_text = ui::style::color::dialog_text;
		const auto icon_cxy = calc_icon_cxy(_ctx->scale_factor);

		if (is_check_box || is_radio_button)
		{
			fill_solid_rect(dc, button_bounds,
			                is_focused
				                ? ui::style::color::dialog_selected_background
				                : ui::style::color::dialog_background);

			const auto is_checked = GetCheck() != 0;
			const auto icon = is_check_box
				                  ? (is_checked ? icon_index::checkbox_on : icon_index::checkbox_off)
				                  : is_checked
				                  ? icon_index::radio_on
				                  : icon_index::radio_off;

			auto* const old_font = SelectObject(dc, _ctx->icons);
			const wchar_t sz[2]{static_cast<wchar_t>(icon), 0};
			SIZE icon_extent;

			if (GetTextExtentPoint32(dc, sz, 1, &icon_extent))
			{
				const recti icon_bounds(pointi(button_bounds.left + button_padding,
				                               (button_bounds.top + button_bounds.bottom) / 2 - icon_extent.cx / 2),
				                        sizei(icon_extent.cx, icon_extent.cy));

				if (is_checked)
				{
					fill_solid_rect(dc, win_rect(icon_bounds), ui::style::color::dialog_selected_background);
				}

				draw_icon(dc, _ctx, icon, icon_bounds, clr_text);
				client_bounds.left += icon_cxy + button_padding;
			}

			SelectObject(dc, old_font);
		}
		else if (is_disabled)
		{
			clr_bg = ui::average(ui::style::color::dialog_background, clr_bg);
			const auto c2 = ui::emphasize(clr_bg, 0.123f);
			draw_gradient(dc, button_bounds, clr_bg, c2);
			frame_rect(dc, button_bounds, ui::darken(clr_bg, 0.22f), ui::darken(c2, 0.22f));
		}
		else if (is_selected)
		{
			fill_solid_rect(dc, button_bounds, ui::style::color::dialog_selected_background);
		}
		else if (is_default)
		{
			clr_bg = ui::average(ui::style::color::dialog_selected_background, clr_bg);
			const auto c2 = ui::emphasize(clr_bg, 0.123f);
			draw_gradient(dc, button_bounds, clr_bg, c2);
			frame_rect(dc, button_bounds, ui::darken(clr_bg, 0.22f), ui::darken(c2, 0.22f));
		}
		else
		{
			const auto c2 = ui::emphasize(clr_bg, 0.123f);
			draw_gradient(dc, button_bounds, clr_bg, c2);
			frame_rect(dc, button_bounds, ui::darken(clr_bg, 0.22f), ui::darken(c2, 0.22f));
		}

		if (is_disabled)
		{
			clr_text = ui::average(clr_text, clr_bg);
		}

		SetTextColor(dc, clr_text);
		SetBkMode(dc, TRANSPARENT);

		auto* const old_font = SelectObject(dc, _ctx->dialog);

		if (str::is_empty(_details))
		{
			if (_icon != icon_index::none)
			{
				auto r = client_bounds;
				r.right = r.left + icon_cxy + button_padding;
				draw_icon(dc, _ctx, _icon, r, clr_text);
				client_bounds.left += icon_cxy + button_padding;
			}

			if (is_radio_button || is_check_box)
			{
				constexpr auto style = DT_WORDBREAK;
				auto r = client_bounds;
				DrawText(dc, text, -1, r, style | DT_CALCRECT);
				const auto yy = (client_bounds.height() - r.height()) / 2;
				DrawText(dc, text, -1, r.offset(0, yy), style);
			}
			else
			{
				constexpr auto style = DT_WORDBREAK | DT_CENTER;
				auto r = client_bounds;
				DrawText(dc, text, -1, r, style | DT_CALCRECT);
				const auto yy = (client_bounds.height() - r.height()) / 2;
				const auto xx = (client_bounds.width() - r.width()) / 2;
				DrawText(dc, text, -1, r.offset(xx, yy), style);
			}
		}
		else
		{
			auto title_bounds = client_bounds;
			auto details_bounds = client_bounds;
			const auto icon_width = _icon == icon_index::none ? 0 : icon_cxy + button_padding;

			SelectObject(dc, _ctx->title);
			DrawText(dc, text, -1, title_bounds, DT_CALCRECT);

			title_bounds = title_bounds.offset((client_bounds.width() - (title_bounds.width() + icon_width)) / 2, 0);
			details_bounds.top = title_bounds.bottom + button_padding;

			if (_icon != icon_index::none)
			{
				auto r = title_bounds;
				r.right = r.left + icon_cxy;
				r.top += button_padding / 2;
				draw_icon(dc, _ctx, _icon, r, clr_text);
				title_bounds = title_bounds.offset(icon_width, 0);
			}

			DrawText(dc, text, -1, title_bounds, DT_TOP);
			SelectObject(dc, _ctx->dialog);
			DrawText(dc, _details.c_str(), -1, details_bounds, DT_WORDBREAK | DT_CENTER);
		}

		SelectObject(dc, old_font);
	}

	sizei measure_button(const int cx) const
	{
		wchar_t text[512];
		::GetWindowText(m_hWnd, text, 512);

		const auto style = ::GetWindowLong(m_hWnd, GWL_STYLE);
		const auto is_radio_button = (style & BS_TYPEMASK) == BS_AUTORADIOBUTTON;
		const auto is_check_box = (style & BS_TYPEMASK) == BS_AUTOCHECKBOX;
		const auto scale_factor = _ctx->scale_factor;
		const auto button_padding = df::round(ui_button_padding * scale_factor);
		auto cx_result = button_padding * 2;
		auto cy_result = button_padding * 2;

		const auto icon_cxy = calc_icon_cxy(scale_factor);
		auto icon_width = 0;
		if (_icon != icon_index::none) icon_width += icon_cxy + button_padding;
		if (is_radio_button || is_check_box) icon_width += icon_cxy + button_padding;

		const auto dc = GetDC(m_hWnd);

		if (dc)
		{
			const auto old_font = SelectObject(dc, _ctx->dialog);

			if (str::is_empty(_details))
			{
				win_rect text_bounds(0, 0, cx - icon_width - button_padding - button_padding, 1000);
				DrawText(dc, text, -1, text_bounds, DT_WORDBREAK | DT_CALCRECT);
				cx_result += icon_width + text_bounds.width();
				cy_result += text_bounds.height();
			}
			else
			{
				win_rect title_bounds(0, 0, cx - icon_width - button_padding - button_padding, 1000);
				win_rect details_bounds(0, 0, cx - button_padding - button_padding, 1000);

				SelectObject(dc, _ctx->title);
				DrawText(dc, text, -1, title_bounds, DT_CALCRECT);
				SelectObject(dc, _ctx->dialog);
				DrawText(dc, _details.c_str(), -1, details_bounds, DT_WORDBREAK | DT_CALCRECT);

				cx_result += std::max(details_bounds.width(), icon_width + title_bounds.width());
				cy_result += title_bounds.height() + details_bounds.height() + button_padding;
			}

			SelectObject(dc, old_font);
			ReleaseDC(m_hWnd, dc);
		}

		return {cx_result, cy_result};
	}

	sizei measure(const int cx) const override
	{
		return measure_button(cx);
	}

	LRESULT on_notify(const ui::frame_host_weak_ptr& host, const ui::color_style& colors, const int id,
	                  const LPNMHDR pnmh) override
	{
		if (pnmh->code == NM_CUSTOMDRAW)
		{
			const auto pCustomDraw = std::bit_cast<LPNMCUSTOMDRAW>(pnmh);
			const auto from = pCustomDraw->hdr.hwndFrom;

			if (pCustomDraw->dwDrawStage == CDDS_PREERASE)
			{
				draw(pCustomDraw);
				return CDRF_SKIPDEFAULT;
			}

			return CDRF_SKIPDEFAULT;
		}

		return 0;
	}
};

class trackbar_impl final :
	public control_base_impl<trackbar_impl, ui::trackbar, win_base>,
	public control_base2,
	public std::enable_shared_from_this<trackbar_impl>
{
public:
	trackbar_impl(std::function<void(int, bool)> changed, const owner_context_ptr& ctx) : _changed(std::move(changed)),
		_ctx(ctx)
	{
	}

	HWND Create(const HWND hWndParent, win_rect rect = {}, const LPCTSTR szWindowName = nullptr,
	            const DWORD dwStyle = 0, const DWORD dwExStyle = 0,
	            const uintptr_t id = 0U)
	{
		m_hWnd = CreateWindowEx(
			dwExStyle, TRACKBAR_CLASS, // predefined class 
			szWindowName, // no window title 
			dwStyle,
			0, 0, 0, 0, // set size in WM_SIZE message 
			hWndParent, // parent window 
			std::bit_cast<HMENU>(id), // edit control ID 
			get_resource_instance,
			nullptr); // pointer not needed

		if (m_hWnd) buffered_control_paint::attach(m_hWnd);

		return m_hWnd;
	}

	void destroy() override
	{
		DestroyWindow(m_hWnd);
	}

	void set_range(const int nMin, const int nMax, const BOOL bRedraw = TRUE) const
	{
		df::assert_true(IsWindow(m_hWnd));
		::SendMessage(m_hWnd, TBM_SETRANGE, bRedraw, MAKELPARAM(nMin, nMax));
	}

	int get_pos() const override
	{
		return static_cast<int>(::SendMessage(m_hWnd, TBM_GETPOS, 0, 0L));
	}

	void SetPos(const int val) override
	{
		::SendMessage(m_hWnd, TBM_SETPOS, TRUE, val);
	}

	void buddy(const ui::edit_ptr& edit) override
	{
		::SendMessage(m_hWnd, TBM_SETBUDDY, TRUE, (LPARAM)std::any_cast<HWND>(edit->handle()));
	}

	void on_command(const ui::frame_host_weak_ptr& host, const int id, const int code) override
	{
	}

	void on_scroll(const ui::frame_host_weak_ptr& host, const int code, const int pos) override
	{
		if (_changed)
		{
			_changed(get_pos(), code == TB_THUMBTRACK);
		}
	}

	void dpi_changed() override
	{
		_ctx->set_window_font(m_hWnd, ui::style::font_face::dialog);
	}

	std::function<void(int, bool)> _changed;
	owner_context_ptr _ctx;
};

class date_time_control_impl final :
	public control_base_impl<date_time_control_impl, ui::date_time_control, win_impl>,
	public control_base2,
	public std::enable_shared_from_this<date_time_control_impl>
{
public:
	HWND _date_control = nullptr;
	HWND _time_control = nullptr;
	sizei _extent;

	const uintptr_t date_id = 1000;
	const uintptr_t time_id = 2000;
	std::function<void(df::date_t)> _changed;
	df::date_t _val;
	ui::color_style _colors;
	const bool _include_time = false;
	owner_context_ptr _ctx;

	date_time_control_impl(owner_context_ptr ctx, const df::date_t val, std::function<void(df::date_t)> changed,
	                       const ui::color_style& colors, const bool include_time) : _changed(std::move(changed)),
		_val(val), _colors(colors), _include_time(include_time), _ctx(std::move(ctx))
	{
	}

	void destroy() override
	{
		DestroyWindow(m_hWnd);
	}

	LRESULT on_window_message(const HWND hWnd, const UINT uMsg, const WPARAM wParam, const LPARAM lParam) override
	{
		if (uMsg == WM_CREATE) return on_window_create(uMsg, wParam, lParam);
		if (uMsg == WM_PAINT) return on_window_paint(uMsg, wParam, lParam);
		if (uMsg == WM_PRINTCLIENT) return on_window_print_client(uMsg, wParam, lParam);
		if (uMsg == WM_ERASEBKGND) return on_window_erase_background(uMsg, wParam, lParam);
		if (uMsg == WM_SIZE) return on_window_layout(uMsg, wParam, lParam);
		if (uMsg == WM_ENABLE) return on_window_enable(uMsg, wParam, lParam);

		if (uMsg == WM_NOTIFY)
		{
			const auto pnmh = std::bit_cast<LPNMHDR>(lParam);
			const auto id = pnmh->idFrom;

			if (date_id == id && DTN_DATETIMECHANGE == pnmh->code) return on_changed(id, pnmh);
			if (time_id == id && DTN_DATETIMECHANGE == pnmh->code) return on_changed(id, pnmh);
			if (date_id == id && NM_SETFOCUS == pnmh->code) return on_focus(id, pnmh);
			if (time_id == id && NM_SETFOCUS == pnmh->code) return on_focus(id, pnmh);
			if (date_id == id && WM_KILLFOCUS == pnmh->code) return on_focus(id, pnmh);
			if (time_id == id && WM_KILLFOCUS == pnmh->code) return on_focus(id, pnmh);
		}

		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}

	LRESULT on_changed(const UINT_PTR idCtrl, const LPNMHDR pnmh) const
	{
		// A control showing "no date" returns GDT_NONE and leaves the structure untouched, so an
		// unchecked call would convert stack garbage into a date and write it to the item.
		SYSTEMTIME std = {}, stt = {};

		if (DateTime_GetSystemtime(_date_control, &std) != GDT_VALID)
		{
			return 0;
		}

		if (_include_time)
		{
			if (DateTime_GetSystemtime(_time_control, &stt) == GDT_VALID)
			{
				std.wHour = stt.wHour;
				std.wMinute = stt.wMinute;
				std.wSecond = stt.wSecond;
				std.wMilliseconds = stt.wMilliseconds;
			}
		}

		FILETIME ft;

		if (!SystemTimeToFileTime(&std, &ft))
		{
			return 0;
		}

		if (_changed)
		{
			_changed(df::date_t(ft_to_ts(ft)));
		}

		return 0;
	}

	LRESULT on_focus(const UINT_PTR idCtrl, const LPNMHDR pnmh) const
	{
		NMHDR nmh;
		nmh.code = pnmh->code; // Message type defined by control.
		nmh.idFrom = GetDlgCtrlID(m_hWnd);
		nmh.hwndFrom = m_hWnd;
		::SendMessage(GetParent(m_hWnd), WM_NOTIFY, nmh.idFrom, (LPARAM)&nmh);
		return 0;
	}

	LRESULT on_window_create(uint32_t /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
	{
		auto* font = _ctx->dialog;

		SYSTEMTIME st;
		const auto ft = ts_to_ft(_val._i);
		FileTimeToSystemTime(&ft, &st);

		_date_control = CreateWindowEx(0,
		                               DATETIMEPICK_CLASS,
		                               nullptr,
		                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_SHORTDATEFORMAT,
		                               0, 0, 0, 0,
		                               m_hWnd,
		                               std::bit_cast<HMENU>(date_id),
		                               get_resource_instance,
		                               nullptr);

		_ctx->set_window_font(_date_control, ui::style::font_face::dialog);
		DateTime_SetSystemtime(_date_control, _val.is_valid() ? GDT_VALID : GDT_NONE, &st);

		if (_include_time)
		{
			_time_control = CreateWindowEx(0,
			                               DATETIMEPICK_CLASS,
			                               nullptr,
			                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_TIMEFORMAT,
			                               0, 0, 0, 0,
			                               m_hWnd,
			                               std::bit_cast<HMENU>(time_id),
			                               get_resource_instance,
			                               nullptr);

			_ctx->set_window_font(_time_control, ui::style::font_face::dialog);
			DateTime_SetSystemtime(_time_control, _val.is_valid() ? GDT_VALID : GDT_NONE, &st);
		}

		return 0;
	}

	LRESULT on_window_paint(uint32_t /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/) const
	{
		PAINTSTRUCT ps;
		auto hdc = BeginPaint(m_hWnd, &ps);
		EndPaint(m_hWnd, &ps);
		return 0;
	}

	static LRESULT on_window_print_client(uint32_t /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/)
	{
		return 0;
	}

	LRESULT on_window_erase_background(uint32_t /*uMsg*/, const WPARAM wParam, LPARAM /*lParam*/) const
	{
		const auto dc = std::bit_cast<HDC>(wParam);
		win_rect r;
		GetClipBox(dc, r);
		fill_solid_rect(dc, r, _colors.background);
		return 1;
	}

	LRESULT on_window_enable(uint32_t /*uMsg*/, const WPARAM wParam, LPARAM lParam) const
	{
		if (_include_time)
		{
			EnableWindow(_time_control, static_cast<BOOL>(wParam));
		}

		EnableWindow(_date_control, static_cast<BOOL>(wParam));

		return 0;
	}

	LRESULT on_window_layout(uint32_t /*uMsg*/, WPARAM wParam, const LPARAM lParam)
	{
		const auto scale_factor = _ctx->scale_factor;
		const auto cx_min = df::round(160 * scale_factor);
		const auto cx = _include_time ? cx_min * 2 : cx_min;

		_extent = {std::min(cx, static_cast<int>(LOWORD(lParam))), HIWORD(lParam)};

		auto date_bounds = win_rect(_extent);
		auto time_bounds = win_rect(_extent);

		if (_include_time)
		{
			const auto x = _extent.cx / 2;
			date_bounds.right = x - 4;
			time_bounds.left = x + 4;

			MoveWindow(_time_control, time_bounds.left, time_bounds.top, time_bounds.width(), time_bounds.height(),
			           TRUE);
		}

		MoveWindow(_date_control, date_bounds.left, date_bounds.top, date_bounds.width(), date_bounds.height(), TRUE);

		return 0;
	}

	void enable(const bool enable) override
	{
		EnableWindow(m_hWnd, enable);
	}

	sizei measure(const int cx) const override
	{
		win_rect r;
		GetClientRect(_date_control, &r);

		const auto scale_factor = _ctx->scale_factor;
		const auto cx_min = df::round(160 * scale_factor);
		const auto xx = _include_time ? cx_min * 2 : cx_min;
		const int cx_result = _include_time ? cx : std::min(xx, cx);
		return {cx_result, r.height()};
	}

	void focus() override
	{
		SetFocus(_date_control);
	}

	bool has_focus() const override
	{
		const auto* const f = GetFocus();
		// GetFocus returns null when nothing on the thread has focus, and _time_control is null
		// for a date-only control, so the null case must not be allowed to match.
		if (!f) return false;
		return f == _date_control || f == _time_control;
	}

	void dpi_changed() override
	{
		_ctx->set_window_font(m_hWnd, ui::style::font_face::dialog);
		_ctx->set_window_font(_date_control, ui::style::font_face::dialog);
		if (_time_control) _ctx->set_window_font(_time_control, ui::style::font_face::dialog);
	}

	LRESULT on_notify(const ui::frame_host_weak_ptr& host, const ui::color_style& colors, const int id,
	                  const LPNMHDR pnmh) override
	{
		if (pnmh->code == NM_SETFOCUS)
		{
			const auto h = host.lock();
			if (h) h->focus_changed(true, shared_from_this());
		}
		else if (pnmh->code == NM_KILLFOCUS)
		{
			const auto h = host.lock();
			if (h) h->focus_changed(false, shared_from_this());
		}

		return 0;
	}

	void Create(const HWND parent, const uintptr_t id)
	{
		constexpr auto dw_style = WS_CHILD;
		const auto* const class_name = L"DIFF_DATE_CTRL";

		// hbrBackground stays null: register_class treats ERROR_CLASS_ALREADY_EXISTS as success and the
		// class is never unregistered, so the first brush would be kept forever - including past the
		// death of the owner_context that owns it. on_window_erase_background paints the background.
		if (register_class(CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS, nullptr, nullptr, nullptr,
		                   nullptr, class_name, nullptr))
		{
			m_hWnd = CreateWindowEx(
				WS_EX_CONTROLPARENT,
				class_name,
				nullptr,
				dw_style,
				0, 0, 0, 0,
				parent, std::bit_cast<HMENU>(id),
				get_resource_instance,
				this);

			df::assert_true(IsWindow(m_hWnd));
		}
	}
};


class frame_base : public win_impl
{
public:
	frame_base(const owner_context_ptr& ctx) : _gdi_ctx(ctx)
	{
	}


	void create_draw_context(const factories_ptr& f, bool is_d3d, bool use_swapchain);

	// Layout metrics derived from the owner context's scale. Every window that draws has to be
	// told again whenever that scale changes, so this is the one definition of what "again" means.
	void update_dpi_metrics() const;

	// Tears down the current swap chain and draw context and builds a new one using the same
	// options. When the shared factories have been downgraded to software this transparently
	// produces a CPU software draw context instead of a Direct3D one.
	void recreate_draw_context();

	LRESULT on_window_message(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;

	void handle_render(recti damage = {});
	void handle_resize(sizei extent, bool is_minimised);
	void present() const;
	void handle_device_loss(HRESULT hr, std::string_view operation) const;

	virtual LRESULT handle_message(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) = 0;
	virtual void on_render(const draw_context_device_ptr& ctx) = 0;
	virtual void on_resize(sizei extent, bool is_minimized) = 0;

	factories_ptr _f;
	draw_context_device_ptr _draw_ctx;

	ComPtr<IDXGISwapChain> _swap_chain;
	mutable bool _device_loss_handled = false;

	// Remembered so the context can be rebuilt after a device loss.
	bool _use_d3d = false;
	bool _use_transparency = false;

	sizei _extent;
	// Allocated back buffer size, which is >= _extent and quantised. Only meaningful with a swap chain.
	sizei _buffer_extent;
	bool _is_occluded = false;
	ui::control_frame_weak_ptr _owner;
	owner_context_ptr _gdi_ctx;

	void destroy_frame_base()
	{
		_swap_chain.Reset();
		_draw_ctx.reset();
		_f.reset();

		if (m_hWnd != nullptr)
		{
			SetWindowLongPtr(m_hWnd, GWLP_USERDATA, 0);
			m_hWnd = nullptr;
		}
	}
};

// Rounds a client extent up to whole back-buffer quanta. Presenting a client-sized region from an
// oversized buffer needs no scaling call: DXGI_SCALING_NONE already pins the top-left corner and
// crops to the window, pixel-exact. Only growth past the allocation costs a real reallocation.
static sizei quantise_back_buffer_extent(const sizei extent)
{
	const auto round_up = [](const int v)
	{
		return std::max(back_buffer_quantum, (v + back_buffer_quantum - 1) / back_buffer_quantum * back_buffer_quantum);
	};

	return {round_up(extent.cx), round_up(extent.cy)};
}

void frame_base::create_draw_context(const factories_ptr& f, const bool use_d3d, const bool use_transparency)
{
	_f = f;
	_use_d3d = use_d3d;
	_use_transparency = use_transparency;

	if (m_hWnd != nullptr)
	{
		HRESULT hr = S_OK;

		if (use_d3d && _f->d3d_device)
		{
			ComPtr<IDXGISwapChain> sc;
			ComPtr<IDXGIFactory2> f2;

			// IDXGIFactory2 ships with the Direct3D 11.1 runtime, which is the minimum this build
			// targets. The legacy IDXGIFactory::CreateSwapChain path is not attempted: it cannot
			// produce a flip-model chain and its blt-model output does not match the BGRA device,
			// so a failure here falls through to the software renderer below.
			hr = _f->dxgi.As(&f2);

			if (SUCCEEDED(hr))
			{
				ComPtr<IDXGISwapChain1> sc1;

				DXGI_SWAP_CHAIN_DESC1 sd = {};
				sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
				sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_BACK_BUFFER;
				// (is_d3d ? DXGI_USAGE_BACK_BUFFER : 0);
				// Each pane owns its own swap chain, so a resize step can be composited before a pane
				// has presented its new frame. STRETCH rubber-bands that stale frame across the new
				// rectangle; NONE pins it, so the pane holds still and only the newly exposed edge is
				// briefly undrawn.
				sd.Scaling = DXGI_SCALING_NONE;
				sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
				sd.BufferCount = 2;
				sd.SampleDesc.Count = 1;
				sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
				_buffer_extent = quantise_back_buffer_extent(_extent);
				sd.Width = _buffer_extent.cx;
				sd.Height = _buffer_extent.cy;

				hr = f2->CreateSwapChainForHwnd(_f->d3d_device.Get(), m_hWnd, &sd, nullptr, nullptr, &sc1);

				if (SUCCEEDED(hr))
				{
					sc = sc1;
				}
			}


			if (SUCCEEDED(hr))
			{
				_swap_chain = sc;
				_f->dxgi->MakeWindowAssociation(m_hWnd, DXGI_MWA_NO_WINDOW_CHANGES);
				_draw_ctx = d3d11_create_context(_f, _swap_chain, _gdi_ctx->calc_base_font_size());
			}

			if (!_draw_ctx)
			{
				// The Direct3D path was requested but failed to produce a valid draw context
				// (swap-chain creation failed, or shader/device-resource setup in
				// d3d11_create_context failed). Reset any partial swap chain and fall back to
				// CPU software rendering below so the window still renders instead of staying blank.
				df::log(__FUNCTION__, "Direct3D draw context unavailable - falling back to software rendering");
				_swap_chain.Reset();
			}
		}

		if (!_draw_ctx)
		{
			// CPU software rendering: used for dialogs and bubble popups, and as the fallback
			// when Direct3D hardware is unavailable or its draw context failed to initialise.
			// use_transparency selects a layered (per-pixel alpha) window for bubbles.
			_draw_ctx = create_software_draw_context(_f, m_hWnd, use_transparency,
			                                         _gdi_ctx->calc_base_font_size());
		}
	}

	if (_draw_ctx && _gdi_ctx)
	{
		update_dpi_metrics();
	}
}

void frame_base::update_dpi_metrics() const
{
	if (_draw_ctx && _gdi_ctx)
	{
		const auto scale_factor = _gdi_ctx->calc_ui_scale_factor();
		_draw_ctx->scale_factor = scale_factor;
		_draw_ctx->icon_cxy = calc_icon_cxy(scale_factor);
		_draw_ctx->padding2 = df::round(ui_component_snap * scale_factor);
		_draw_ctx->padding1 = df::round(ui_baseline_snap * scale_factor);
		_draw_ctx->handle_cxy = df::round(ui_cx_resize_handle * scale_factor);
		_draw_ctx->scroll_width = df::round(ui_scroll_width * scale_factor);
	}
}

void frame_base::recreate_draw_context()
{
	df::assert_true(ui::is_ui_thread());

	const auto f = _f;
	const auto use_d3d = _use_d3d;
	const auto use_transparency = _use_transparency;

	if (_draw_ctx)
	{
		_draw_ctx->destroy();
		_draw_ctx.reset();
	}

	_swap_chain.Reset();
	_device_loss_handled = false;

	if (f)
	{
		// create_draw_context falls back to the CPU software backend automatically when the
		// factories no longer have a Direct3D device.
		create_draw_context(f, use_d3d, use_transparency);

		if (_draw_ctx && !_extent.is_empty())
		{
			_draw_ctx->resize(_extent);
		}
	}

	if (m_hWnd != nullptr)
	{
		InvalidateRect(m_hWnd, nullptr, FALSE);
	}
}

void frame_base::handle_resize(const sizei extent, const bool is_minimised)
{
	const auto extent_changed = _extent != extent;

	if (extent_changed)
	{
		_extent = extent;

		if (!_extent.is_empty() && !is_minimised)
		{
			if (_swap_chain)
			{
				const auto wanted = quantise_back_buffer_extent(_extent);

				// Grow as soon as the client no longer fits, but shrink only once it has halved, so
				// a drag sitting on a quantum boundary cannot reallocate on alternate steps.
				const auto must_grow = _extent.cx > _buffer_extent.cx || _extent.cy > _buffer_extent.cy;
				const auto worth_shrinking = wanted.cx * 2 <= _buffer_extent.cx && wanted.cy * 2 <= _buffer_extent.cy;

				if (must_grow || worth_shrinking)
				{
					// ResizeBuffers fails with DXGI_ERROR_INVALID_CALL while anything still holds a
					// reference to a back buffer. The draw context leaves its render target view bound
					// after each frame, so unbind it first.
					if (_draw_ctx)
					{
						_draw_ctx->release_back_buffer_references();
					}

					constexpr UINT flags = 0;
					const auto hr = _swap_chain->ResizeBuffers(swap_buffer_count, wanted.cx, wanted.cy,
					                                           back_buffer_format, flags);
					if (FAILED(hr))
					{
						df::log(__FUNCTION__, std::format("ResizeBuffers failed 0x{:08x}",
						                                  static_cast<uint32_t>(hr)));
						handle_device_loss(hr, "ResizeBuffers");
					}
					else
					{
						_buffer_extent = wanted;
					}
				}

				InvalidateRect(m_hWnd, nullptr, FALSE);
			}

			if (_draw_ctx)
			{
				_draw_ctx->resize(_extent);
			}
		}
	}

	on_resize(extent, is_minimised);

	// Resizing leaves the surface holding undefined or stale content. Painting the new layout
	// here instead of waiting for WM_PAINT stops that content reaching the screen: the WM_PAINT
	// would otherwise arrive only once the message queue drained, so every intervening
	// composition frame shows the window at its new size with the previous frame's pixels.
	if (extent_changed && !is_minimised && !_extent.is_empty() && m_hWnd != nullptr)
	{
		handle_render();
		ValidateRect(m_hWnd, nullptr);

		// Native child controls repaint from their own queued WM_PAINT, which the message loop only
		// reaches after the size step is over - so they trail the frame by one step. Flushing the
		// paints already pending here puts them in the same composition frame as this surface.
		RedrawWindow(m_hWnd, nullptr, nullptr, RDW_ALLCHILDREN | RDW_UPDATENOW);
	}
};

void frame_base::handle_device_loss(const HRESULT hr, const std::string_view operation) const
{
	if (!is_device_loss_error(hr)) return;

	if (_device_loss_handled) return;
	_device_loss_handled = true;

	ComPtr<ID3D11Device> device;
	const auto reason = _swap_chain && SUCCEEDED(_swap_chain->GetDevice(IID_PPV_ARGS(&device))) && device
		                    ? device->GetDeviceRemovedReason()
		                    : hr;
	df::log(__FUNCTION__, std::format("{} failed with HRESULT 0x{:08x}; device reason 0x{:08x}. "
	                                  "Switching to CPU software rendering.",
	                                  operation, static_cast<uint32_t>(hr), static_cast<uint32_t>(reason)));

	// Remember that the GPU path failed so the next launch starts in software mode even if
	// the in-session recovery below does not survive.
	platform::fail_crash_guard(platform::crash_guard::gpu_render);

	// Recovery must not run inside the render/resize call stack - draw contexts and the D3D
	// device are torn down, and both are live on the stack here. Post to ourselves instead.
	if (m_hWnd != nullptr)
	{
		::PostMessage(m_hWnd, WM_DIFF_DEVICE_LOST, 0, 0);
	}
}

bool is_device_loss_error(const HRESULT hr)
{
	return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET ||
		hr == DXGI_ERROR_DEVICE_HUNG || hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}

void frame_base::present() const
{
	if (_swap_chain)
	{
		const auto hr = _swap_chain->Present(0, 0);
		if (FAILED(hr))
		{
			handle_device_loss(hr, "Present");
			return;
		}

		// A successful present proves GPU device creation and rendering work. Clear the GPU
		// crash guard on the first one so an unrelated later force-kill or power loss does not
		// trigger software recovery. A later DXGI device-loss result explicitly marks it again.
		static std::atomic_bool gpu_render_confirmed{false};
		if (!gpu_render_confirmed.exchange(true))
		{
			platform::set_crash_guard(platform::crash_guard::gpu_render, false);
		}
	}
}

void frame_base::handle_render(const recti damage)
{
	df::bump(df::ui_perf.paints);
	df::perf_timer timer(df::ui_perf.paint_us, &df::ui_perf.paint_max_us);
	const auto ctx = _draw_ctx;

	if (ctx)
	{
		ctx->begin_draw(_extent, _gdi_ctx->calc_base_font_size(), damage);
		on_render(ctx);

		const auto hr = ctx->render();

		if (FAILED(hr))
		{
			// A failed render means the back buffer holds nothing worth showing, so skip the
			// present. handle_device_loss ignores non device-loss results.
			handle_device_loss(hr, "render");
			return;
		}
	}

	present();
}

LRESULT frame_base::on_window_message(const HWND hwnd, const UINT uMsg, const WPARAM wParam, const LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_CREATE:
		m_hWnd = hwnd;
		break;
	case WM_SIZE:
		handle_resize({LOWORD(lParam), HIWORD(lParam)}, wParam == SIZE_MINIMIZED);
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	case WM_PAINT:
		{
			// Read the update region before validating: it is what lets a backend repaint only the
			// part of the window that changed.
			RECT update{};
			const auto has_update = GetUpdateRect(m_hWnd, &update, FALSE) != 0;
			handle_render(has_update ? recti(update.left, update.top, update.right, update.bottom) : recti{});
			ValidateRect(m_hWnd, nullptr);
			return 0;
		}
	case WM_DIFF_DEVICE_LOST:
		{
			handle_graphics_device_lost(_f);
			return 0;
		}
	case WM_DISPLAYCHANGE:
		{
			InvalidateRect(hwnd, nullptr, FALSE);
			return 0;
		}

	default:
		return handle_message(hwnd, uMsg, wParam, lParam);
	}

	return handle_message(hwnd, uMsg, wParam, lParam);
}


class bubble_impl final : public frame_base, public ui::bubble_frame
{
protected:
	// Non-zero: SetTimer treats 0 as a valid id but this class also uses 0 as its "no timer"
	// sentinel, so a timer created with id 0 could never be stopped.
	static constexpr UINT_PTR bubble_fade_timer_id = 1;

	UINT_PTR _timer_id = 0;
	int _alpha = 0;
	int _alpha_target = 0;
	recti _bounds;
	view_elements_ptr _elements;
	bool _horizontal = false;
	pointi _focus_loc;

public:
	bubble_impl(owner_context_ptr ctx) : frame_base(std::move(ctx))
	{
	}

	~bubble_impl() override
	{
		if (m_hWnd)
		{
			DestroyWindow(m_hWnd);
			m_hWnd = nullptr;
		}
	}

	bool create(const HWND parent, const factories_ptr& f)
	{
		const auto* const class_name = L"DIFF_BUBBLE";

		// hbrBackground stays null: register_class treats ERROR_CLASS_ALREADY_EXISTS as success and the
		// class is never unregistered, so a brush handed over here would outlive the owner_context that
		// owns it and every later window of this class would erase with a deleted HBRUSH.
		if (register_class(CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS, nullptr, nullptr, nullptr,
		                   nullptr, class_name, nullptr))
		{
			constexpr auto style_ex = WS_EX_LAYERED; // WS_EX_NOREDIRECTIONBITMAP;
			m_hWnd = CreateWindowEx(style_ex, class_name, nullptr, WS_POPUP, 0, 0, 0, 0, parent, nullptr,
			                        get_resource_instance, this);
			df::assert_true(m_hWnd != nullptr);

			if (m_hWnd)
			{
				create_draw_context(f, false, true);
				_gdi_ctx->set_window_font(m_hWnd, ui::style::font_face::dialog);
				return true;
			}
		}

		return false;
	}

	LRESULT handle_message(const HWND hwnd, const UINT uMsg, const WPARAM wParam, const LPARAM lParam) override
	{
		if (uMsg == WM_DESTROY) return on_window_destroy(uMsg, wParam, lParam);
		if (uMsg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
		if (uMsg == WM_NCHITTEST) return HTTRANSPARENT;
		if (uMsg == WM_TIMER)
		{
			step_timer();
			return 0;
		}
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}

	void step_timer()
	{
		if (_alpha != _alpha_target)
		{
			_alpha = (_alpha * 5 + _alpha_target * 2) / 7;

			if (std::abs(_alpha - _alpha_target) <= 1)
			{
				_alpha = _alpha_target;
			}

			// Re-present the layered window with the updated fade alpha.
			if (_draw_ctx && _alpha > 0)
			{
				handle_render();
			}
		}
		else if (_timer_id)
		{
			KillTimer(m_hWnd, _timer_id);
			_timer_id = 0;
		}
	}

	bool is_visible() const override
	{
		return m_hWnd && _alpha > 0;
	}

	void hide() override
	{
		set_alpha(0);
	}

	const int shadow_xy = 32;
	const int shadow_padding = 16;
	const int vert_padding = shadow_padding + 6;
	const int horz_padding = shadow_padding + 10;
	const float radius = 8.0;

	void show(const view_elements_ptr& elements, const recti button_rect, const int x_focus, const int preferred_size,
	          const bool horizontal) override
	{
		if (_draw_ctx)
		{
			const auto scale_factor = _gdi_ctx->scale_factor;

			// Before measuring: measure builds and caches the element text layouts, so a stale size
			// here would lay the bubble out - and cache glyph rasters - at the previous font size.
			// A bubble shares the app's owner context and so never sees a WM_DPICHANGED of its own;
			// showing it is the only point at which it can pick up a scale change.
			_draw_ctx->update_font_size(df::round(global_base_font_size * scale_factor));
			update_dpi_metrics();

			const auto element_extent = elements->measure(*_draw_ctx, df::round(preferred_size * scale_factor));
			const auto cx = std::max(80, element_extent.cx) + horz_padding * 2;
			const auto cy = std::max(36, element_extent.cy) + vert_padding * 2;

			const auto xx_focus = (x_focus == -1
				                       ? (button_rect.left + button_rect.right) / 2
				                       : button_rect.left + x_focus) - cx / 2;
			const auto yy_focus = (button_rect.top + button_rect.bottom - cy) / 2;

			auto x = horizontal ? button_rect.left - cx : xx_focus;
			auto y = horizontal ? yy_focus : button_rect.top - cy;

			auto focus_loc = recti(x, y, x + cx, y + cy).center();
			const auto desktop_bounds = desktop_bounds_impl(GetParent(m_hWnd), true);

			if (horizontal)
			{
				if (button_rect.left - cx < desktop_bounds.left)
				{
					// on the right
					x = button_rect.right;
					focus_loc.x = button_rect.right - 5;
				}
				else
				{
					// on the left
					x = button_rect.left - cx;
					focus_loc.x = button_rect.left + 5;
				}
			}
			else
			{
				if (button_rect.top - cy < desktop_bounds.top)
				{
					// on the bottom
					y = button_rect.bottom;
					focus_loc.y = button_rect.bottom - 5;
				}
				else
				{
					// on the top
					y = button_rect.top - cy;
					focus_loc.y = button_rect.top + 5;
				}
			}

			_bounds = recti(x, y, x + cx, y + cy).clamp(desktop_bounds.inflate(shadow_padding));
			_focus_loc = focus_loc;
			_elements = elements;

			ui::control_layouts positions;
			_elements->layout(*_draw_ctx, center_rect(element_extent, sizei{cx, cy}), positions);

			InvalidateRect(m_hWnd, nullptr, FALSE);
			SetWindowPos(m_hWnd, nullptr, _bounds.left, _bounds.top, cx, cy,
			             SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);

			set_alpha(255);
		}
	}

	void on_render(const draw_context_device_ptr& ctx) override
	{
		ctx->set_layer_alpha(_alpha);
		ctx->draw_bubble_background(_bounds, _focus_loc, shadow_padding, radius);
		_draw_ctx->colors = {
			ui::style::color::bubble_background, ui::style::color::view_text, ui::style::color::view_selected_background
		};
		_elements->render(*ctx, {});
	}

	void on_resize(const sizei extent, const bool is_minimized) override
	{
	}

	LRESULT on_window_destroy(uint32_t /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
	{
		if (_timer_id) KillTimer(m_hWnd, _timer_id);
		destroy_frame_base();
		return 0;
	}

	void set_alpha(const int a)
	{
		if (_alpha_target != a)
		{
			_alpha_target = a;

			if (_timer_id == 0u)
			{
				// A non-zero nIDEvent: 0 is also the "no timer" sentinel, so passing it means the
				// 30 Hz fade timer can never be matched by KillTimer and runs for the window's life.
				_timer_id = SetTimer(m_hWnd, bubble_fade_timer_id, 1000 / 30, nullptr);
			}
		}
	}
};

class frame_impl final :
	public frame_base,
	public ui_frame_window<frame_impl, ui::frame>,
	public std::enable_shared_from_this<frame_impl>,
	public IDropTarget
{
	pointi _pan_start_loc;
	ULONGLONG _zoom_gesture_distance = 0;
	ui::frame_weak_ptr _parent_frame;
	ui::frame_host_weak_ptr _host;
	ui::frame_host_weak_ptr _parent_host;
	bool _hover = false;
	bool _tracking = false;
	bool _has_focus = false;
	ui::frame_style _style;
	UINT_PTR _timer_id = 0;

public:
	frame_impl(const owner_context_ptr& ctx, ui::frame_weak_ptr parent_frame,
	           const ui::frame_host_weak_ptr& parent_host,
	           const ui::frame_host_weak_ptr& host, const ui::frame_style& style) :
		frame_base(ctx), _parent_frame(std::move(parent_frame)), _host(host), _parent_host(parent_host), _style(style)
	{
	}

	// Nothing guarantees destroy() is reached - a frame released when its owning view or dialog goes
	// away just drops the last reference. Without this the HWND survives with a stale user-data
	// pointer, its registered IDropTarget dangles, and the draw context is never torn down.
	~frame_impl() override
	{
		if (m_hWnd && IsWindow(m_hWnd))
		{
			DestroyWindow(m_hWnd);
		}
	}

	void destroy() override
	{
		DestroyWindow(m_hWnd);
	}

	LRESULT handle_message(const HWND hwnd, const UINT uMsg, const WPARAM wParam, const LPARAM lParam) override
	{
		if (uMsg == WM_DESTROY) return on_window_destroy(uMsg, wParam, lParam);
		if (uMsg == WM_ERASEBKGND) return on_window_erase_background(uMsg, wParam, lParam);
		if (uMsg == WM_SETCURSOR) return on_window_set_cursor(uMsg, wParam, lParam);
		if (uMsg == WM_SETFOCUS) return on_window_set_focus(uMsg, wParam, lParam);
		if (uMsg == WM_KILLFOCUS) return on_window_kill_focus(uMsg, wParam, lParam);
		if (uMsg == WM_KEYDOWN) return on_window_key_down(uMsg, wParam, lParam);
		if (uMsg == WM_TIMER) return on_window_timer(uMsg, wParam, lParam);
		if (uMsg == WM_LBUTTONDOWN) return on_mouse_left_button_down(uMsg, wParam, lParam);
		if (uMsg == WM_LBUTTONUP) return on_mouse_left_button_up(uMsg, wParam, lParam);
		if (uMsg == WM_MBUTTONDOWN) return on_mouse_middle_button_down(uMsg, wParam, lParam);
		if (uMsg == WM_MBUTTONUP) return on_mouse_middle_button_up(uMsg, wParam, lParam);
		if (uMsg == WM_XBUTTONUP) return on_mouse_x_button_up(uMsg, wParam, lParam);
		if (uMsg == WM_MOUSELEAVE) return on_mouse_leave(uMsg, wParam, lParam);
		if (uMsg == WM_MOUSEMOVE) return on_mouse_move(uMsg, wParam, lParam);
		if (uMsg == WM_MOUSEWHEEL) return on_mouse_wheel(uMsg, wParam, lParam);
		constexpr auto pointer_wheel_message = 0x024e;
		constexpr auto pointer_hwheel_message = 0x024f;
		if (uMsg == WM_MOUSEHWHEEL || uMsg == pointer_hwheel_message) return on_mouse_hwheel(uMsg, wParam, lParam);
		if (uMsg == pointer_wheel_message) return on_mouse_wheel(uMsg, wParam, lParam);
		if (uMsg == WM_LBUTTONDBLCLK) return on_mouse_left_button_double_click(uMsg, wParam, lParam);
		if (uMsg == WM_MOUSEACTIVATE) return on_window_mouse_activate(uMsg, wParam, lParam);
		if (uMsg == WM_GESTURENOTIFY) return on_window_gesture_notify(uMsg, wParam, lParam);
		if (uMsg == WM_GESTURE) return on_window_gesture(uMsg, wParam, lParam);
		if (uMsg == WM_NCHITTEST) return on_window_nc_hit_test(uMsg, wParam, lParam);
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}

	void on_render(const draw_context_device_ptr& ctx_in) override
	{
		const auto ctx = _draw_ctx;

		if (ctx)
		{
			ctx->frame_has_focus = _has_focus;
			ctx->colors = _style.colors;
			ctx->clear(ui::color(_style.colors.background, 1.0f));

			const auto h = _host.lock();

			if (h)
			{
				h->on_window_paint(*ctx);
			}
		}
	}

	void on_resize(const sizei extent, const bool is_minimized) override
	{
		const auto ctx = _draw_ctx;

		if (ctx)
		{
			ctx->resize(_extent);

			const auto h = _host.lock();

			if (h)
			{
				h->on_window_layout(*ctx, extent, is_minimized);
			}
		}
	}

	bool create(const HWND parent, const ui::frame_style& style, const factories_ptr& f)
	{
		const auto* const class_name = L"DIFF_FRAME";
		auto dw_style = style.child ? WS_CHILD : WS_POPUP;
		auto dw_ex_style = 0; // style.child ? 0 : WS_EX_COMPOSITED;
		if (style.no_focus) dw_ex_style |= WS_EX_NOACTIVATE;
		if (style.can_focus) dw_style |= WS_TABSTOP;

		if (register_class(CS_DBLCLKS, nullptr, nullptr,
		                   nullptr,
		                   nullptr, class_name, nullptr))
		{
			m_hWnd = CreateWindowEx(dw_ex_style, class_name, nullptr, dw_style, 0, 0, 0, 0, parent, nullptr,
			                        get_resource_instance, this);
			df::assert_true(m_hWnd != nullptr);

			if (m_hWnd)
			{
				create_draw_context(f, _style.hardware_accelerated, false);
				_gdi_ctx->set_window_font(m_hWnd, ui::style::font_face::dialog);
				if (_style.timer_milliseconds)
				{
					_timer_id = SetTimer(m_hWnd, 0, _style.timer_milliseconds, nullptr);
				}

				if (_style.can_drop)
				{
					RegisterDragDrop(m_hWnd, this);
				}

				return true;
			}

			return false;
		}

		return false;
	}

	bool is_valid_device() const
	{
		return _draw_ctx && _draw_ctx->is_valid();
	}

	void layout() override
	{
		if (_draw_ctx && !_extent.is_empty() && IsWindow(m_hWnd))
		{
			const auto h = _host.lock();

			if (h)
			{
				h->on_window_layout(*_draw_ctx, _extent, IsIconic(m_hWnd));
			}
		}
	}

	LRESULT on_window_destroy(const uint32_t /*uMsg*/, const WPARAM /*wParam*/, const LPARAM /*lParam*/)
	{
		if (_timer_id) KillTimer(m_hWnd, _timer_id);

		if (_style.can_drop)
		{
			RevokeDragDrop(m_hWnd);
		}

		const auto h = _host.lock();
		if (h) h->on_window_destroy();

		// destroy() breaks the draw context <-> text renderer reference cycle, so it must run even when the
		// device has already been lost - otherwise the context and its whole texture atlas leak.
		if (_draw_ctx)
		{
			_draw_ctx->destroy();
		}

		destroy_frame_base();

		return 0;
	}

	LRESULT on_window_nc_hit_test(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam) const
	{
		if (_style.child)
		{
			const pointi screen_loc = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
			win_rect rc;
			GetWindowRect(GetParent(m_hWnd), &rc);

			const auto scale_factor = _draw_ctx ? _draw_ctx->scale_factor : 1.0;
			const auto border_thickness = df::round(ui_nonclient_border_thickness * scale_factor);

			if (rc.right >= screen_loc.x && rc.right - border_thickness <= screen_loc.x)
			{
				return HTTRANSPARENT;
			}

			if (rc.left <= screen_loc.x && rc.left + border_thickness >= screen_loc.x)
			{
				return HTTRANSPARENT;
			}
		}

		POINT client_loc{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
		ScreenToClient(m_hWnd, &client_loc);
		const auto h = _host.lock();
		if (h && h->is_caption_area({client_loc.x, client_loc.y}))
		{
			return _style.child ? HTTRANSPARENT : HTCAPTION;
		}

		return DefWindowProc(m_hWnd, uMsg, wParam, lParam);
	}

	LRESULT on_window_set_cursor(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam) const
	{
		SetCursor(_cursor);
		return 1;
	}

	static LRESULT on_window_erase_background(const uint32_t /*uMsg*/, const WPARAM wParam, const LPARAM /*lParam*/)
	{
		return 1;
	}

	void redraw() override
	{
		if (is_valid_device())
		{
			// No begin_draw runs here, so any damage limit from the last paint is stale - the
			// textures this re-present exists to show changed outside it.
			_draw_ctx->reset_damage();

			const auto hr = _draw_ctx->render();

			if (FAILED(hr))
			{
				handle_device_loss(hr, "redraw");
				return;
			}

			present();
		}
		else
		{
			InvalidateRect(hwnd(), nullptr, 0);
		}
	}

	void redraw_now() override
	{
		RedrawWindow(hwnd(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
	}

	LRESULT on_window_timer(const uint32_t /*uMsg*/, const WPARAM /*wParam*/, const LPARAM /*lParam*/) const
	{
		const auto h = _host.lock();
		if (h) h->tick();
		return 0;
	}

	LRESULT on_mouse_move(const uint32_t /*uMsg*/, const WPARAM wParam, const LPARAM lParam)
	{
		const pointi loc(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

		if (!_hover)
		{
			track_mouse_leave(m_hWnd);
			_hover = true;
		}

		const auto h = _host.lock();
		if (h) h->on_mouse_move(loc, _tracking);
		return 0;
	}

	LRESULT on_mouse_left_button_down(const uint32_t /*uMsg*/, const WPARAM wParam, const LPARAM lParam)
	{
		const pointi loc(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

		if (!_tracking)
		{
			_tracking = true;
			SetCapture(m_hWnd);

			if (!_style.no_focus)
			{
				SetFocus(m_hWnd);
			}
		}

		const auto h = _host.lock();
		if (h) h->on_mouse_left_button_down(loc, to_key_state(wParam));

		return 0;
	}

	LRESULT on_mouse_left_button_up(uint32_t /*uMsg*/, const WPARAM wParam, const LPARAM lParam)
	{
		const pointi loc(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

		if (_tracking)
		{
			_tracking = false;
			ReleaseCapture();
			invalidate({}, false);
		}

		const auto h = _host.lock();
		if (h) h->on_mouse_left_button_up(loc, to_key_state(wParam));
		return 0;
	}

	LRESULT on_mouse_middle_button_down(const uint32_t /*uMsg*/, const WPARAM wParam, const LPARAM lParam)
	{
		const auto h = _host.lock();
		if (h) h->on_mouse_middle_button_down({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}, to_key_state(wParam));
		return 0;
	}

	LRESULT on_mouse_middle_button_up(const uint32_t /*uMsg*/, const WPARAM wParam, const LPARAM lParam)
	{
		const auto h = _host.lock();
		if (h) h->on_mouse_middle_button_up({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}, to_key_state(wParam));
		return 0;
	}

	LRESULT on_mouse_left_button_double_click(uint32_t /*uMsg*/, const WPARAM wParam, const LPARAM lParam) const
	{
		const pointi loc(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		const auto h = _host.lock();
		constexpr ULONG_PTR pointer_signature = 0xff515700;
		constexpr ULONG_PTR pointer_signature_mask = 0xffffff00;
		constexpr ULONG_PTR touch_flag = 0x80;
		const auto extra = static_cast<ULONG_PTR>(GetMessageExtraInfo());
		const auto is_touch = (extra & pointer_signature_mask) == pointer_signature && (extra & touch_flag) != 0;
		if (h && !(is_touch && h->touch_double_tap(loc))) h->on_mouse_left_button_double_click(
			loc, to_key_state(wParam));
		return 0;
	}

	LRESULT on_window_mouse_activate(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam) const
	{
		if (_style.no_focus)
		{
			return MA_NOACTIVATE;
		}

		return DefWindowProc(m_hWnd, uMsg, wParam, lParam);
	}

	LRESULT on_mouse_leave(uint32_t /*uMsg*/, WPARAM /*wParam*/, const LPARAM lParam)
	{
		const pointi loc(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		_hover = false;
		const auto h = _host.lock();
		if (h) h->on_mouse_leave(loc);
		InvalidateRect(m_hWnd, nullptr, FALSE);
		return 0;
	}

	LRESULT on_mouse_wheel(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam) const
	{
		POINT loc = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
		ScreenToClient(m_hWnd, &loc);

		bool was_handled = false;
		const auto h = _host.lock();
		if (h) h->on_mouse_wheel({loc.x, loc.y}, static_cast<short>(HIWORD(wParam)), to_key_state(wParam), was_handled);
		if (!was_handled && IsWindow(GetParent(m_hWnd)))
			SendMessage(GetParent(m_hWnd), uMsg, wParam, lParam);
		return 0;
	}

	LRESULT on_mouse_hwheel(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam) const
	{
		POINT loc{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
		ScreenToClient(m_hWnd, &loc);
		bool was_handled = false;
		const auto h = _host.lock();
		if (h) h->on_mouse_hwheel({loc.x, loc.y}, GET_WHEEL_DELTA_WPARAM(wParam), to_key_state(wParam), was_handled);
		return was_handled ? 0 : DefWindowProc(m_hWnd, uMsg, wParam, lParam);
	}

	LRESULT on_mouse_x_button_up(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam) const
	{
		const pointi loc(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		const int fwButton = GET_XBUTTON_WPARAM(wParam);
		const auto h = _host.lock();

		switch (fwButton)
		{
		case XBUTTON1:
			if (h) h->on_mouse_other_button_up(ui::other_mouse_button::xb1, loc, to_key_state(wParam));
			break;
		case XBUTTON2:
			if (h) h->on_mouse_other_button_up(ui::other_mouse_button::xb2, loc, to_key_state(wParam));
			break;
		default: break;
		}

		return 1;
	}

	LRESULT on_window_gesture_notify(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam) const
	{
		constexpr auto wanted_gestures = GC_PAN_WITH_SINGLE_FINGER_VERTICALLY | GC_PAN_WITH_SINGLE_FINGER_HORIZONTALLY;
		GESTURECONFIG gestures[] = {
			{GID_PAN, wanted_gestures, 0},
			{GID_ZOOM, GC_ZOOM, 0}
		};
		SetGestureConfig(m_hWnd, 0, std::size(gestures), gestures, sizeof(GESTURECONFIG));

		return DefWindowProc(m_hWnd, WM_GESTURENOTIFY, wParam, lParam);
	}

	LRESULT on_window_gesture(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam)
	{
		GESTUREINFO gi = {};
		gi.cbSize = sizeof(GESTUREINFO);
		const auto gesture_handle = reinterpret_cast<HGESTUREINFO>(lParam);

		if (GetGestureInfo(gesture_handle, &gi))
		{
			switch (gi.dwID)
			{
			case GID_PAN:
				{
					POINT gesture_location{gi.ptsLocation.x, gi.ptsLocation.y};
					ScreenToClient(m_hWnd, &gesture_location);
					const pointi current_loc = {gesture_location.x, gesture_location.y};

					if (gi.dwFlags & GF_BEGIN)
					{
						_pan_start_loc = current_loc;
						const auto h = _host.lock();
						if (h) h->pan_start(_pan_start_loc);
					}
					else if (gi.dwFlags & GF_END)
					{
						const auto h = _host.lock();
						if (h) h->pan_end(_pan_start_loc, current_loc);
					}
					else
					{
						const auto h = _host.lock();
						if (h) h->pan(_pan_start_loc, current_loc);
					}

					// Handled gestures own the handle; unhandled ones are closed by DefWindowProc.
					CloseGestureInfoHandle(gesture_handle);
					return 0;
				}
			case GID_ZOOM:
				{
					const auto distance = gi.ullArguments;
					if (gi.dwFlags & GF_BEGIN)
					{
						_zoom_gesture_distance = distance;
					}
					else if (_zoom_gesture_distance > 0 && distance > 0)
					{
						POINT loc{gi.ptsLocation.x, gi.ptsLocation.y};
						ScreenToClient(m_hWnd, &loc);
						const auto delta = df::round(
							std::log(distance / static_cast<double>(_zoom_gesture_distance)) * 480.0);
						bool handled = false;
						const auto h = _host.lock();
						if (h && delta != 0) h->on_mouse_wheel({loc.x, loc.y}, delta, {true, false, false}, handled);
						_zoom_gesture_distance = distance;
					}
					if (gi.dwFlags & GF_END) _zoom_gesture_distance = 0;
					CloseGestureInfoHandle(gesture_handle);
					return 0;
				}
			default:
				break;
			}
		}

		return DefWindowProc(m_hWnd, uMsg, wParam, lParam);
	}

	LRESULT on_window_set_focus(uint32_t /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/)
	{
		const auto h = _host.lock();
		if (h) h->focus_changed(true, shared_from_this());
		const auto ph = _parent_host.lock();
		if (ph) ph->focus_changed(true, shared_from_this());
		_has_focus = true;
		return 0;
	}

	LRESULT on_window_kill_focus(uint32_t uMsg, WPARAM wParam, LPARAM /*lParam*/)
	{
		const auto h = _host.lock();
		_zoom_gesture_distance = 0;
		if (h) h->focus_changed(false, shared_from_this());
		const auto ph = _parent_host.lock();
		if (ph) ph->focus_changed(false, shared_from_this());
		_has_focus = false;
		return 0;
	}

	LRESULT on_window_key_down(uint32_t /*uMsg*/, const WPARAM wParam, LPARAM /*lParam*/) const
	{
		const auto h = _host.lock();
		if (h) return h->key_down(static_cast<int>(wParam), ui::current_key_state());
		return 0;
	}

	void set_cursor(const ui::style::cursor cursor) override
	{
		HICON icon = nullptr;
		if (cursor_icon(cursor, icon)) set_cursor(icon);
	}

	void set_cursor(const HICON cursor)
	{
		_cursor = cursor;
		SetCursor(cursor);
	}

	void reset_graphics() override
	{
		// Rebuilds the swap chain and draw context. After a Direct3D device loss the shared
		// factories have already switched to software, so this produces a CPU draw context.
		recreate_draw_context();

		const auto h = _host.lock();

		if (h && _draw_ctx && !_extent.is_empty())
		{
			h->on_window_layout(*_draw_ctx, _extent, IsIconic(m_hWnd));
		}
	}

	void options_changed() override
	{
		if (is_valid_device())
		{
			_draw_ctx->update_font_size(_gdi_ctx->calc_base_font_size());
		}

		_gdi_ctx->set_window_font(hwnd(), ui::style::font_face::dialog);
	}

	void track_menu(const recti button_bounds, const std::vector<ui::command_ptr>& buttons) override
	{
		const auto parent = _parent_frame.lock();

		if (parent)
		{
			parent->track_menu(button_bounds, buttons);
		}
	}

	void dpi_changed() const
	{
		update_dpi_metrics();

		if (is_valid_device())
		{
			_draw_ctx->update_font_size(_gdi_ctx->calc_base_font_size());
		}

		_gdi_ctx->set_window_font(hwnd(), ui::style::font_face::dialog);
	}

	ComPtr<IDataObject> _drag_data;

	pointi to_client(const POINTL& pt) const
	{
		POINT point = {pt.x, pt.y};
		ScreenToClient(m_hWnd, &point);
		return {point.x, point.y};
	}

	STDMETHOD(QueryInterface)(_In_ REFIID iid, _Deref_out_ void** ppvObject) noexcept override
	{
		df::trace(std::format("frame_impl::QueryInterface {}", win32_to_string(iid)));

		if (ppvObject == nullptr) return E_POINTER;

		if (IsEqualGUID(iid, IID_IDropTarget) || IsEqualGUID(iid, IID_IUnknown))
		{
			*ppvObject = static_cast<IDropTarget*>(this);
			return S_OK;
		}

		*ppvObject = nullptr;
		return E_NOINTERFACE;
	}

	STDMETHOD_(ULONG, AddRef)() noexcept override
	{
		return 1;
	}

	STDMETHOD_(ULONG, Release)() noexcept override
	{
		return 1;
	}

	static void set_drop_effect(const platform::drop_effect e, DWORD* pdwEffect)
	{
		if (pdwEffect)
		{
			switch (e)
			{
			case platform::drop_effect::copy:
				*pdwEffect = DROPEFFECT_COPY;
				break;
			case platform::drop_effect::move:
				*pdwEffect = DROPEFFECT_MOVE;
				break;
			case platform::drop_effect::link:
				*pdwEffect = DROPEFFECT_LINK;
				break;
			case platform::drop_effect::none:
			default:
				*pdwEffect = DROPEFFECT_NONE;
			}
		}
	}

	STDMETHODIMP DragEnter(IDataObject* pDataObj, const DWORD grfKeyState, const POINTL pt, DWORD* pdwEffect) override
	{
		_drag_data = pDataObj;
		const auto h = _host.lock();

		if (h)
		{
			const data_object_client data(_drag_data.Get());
			const auto result = h->drag_over(data, to_key_state(grfKeyState), to_client(pt));
			set_drop_effect(result, pdwEffect);
		}

		return S_OK;
	}

	STDMETHODIMP DragLeave() override
	{
		_drag_data = nullptr;
		const auto h = _host.lock();
		if (h) h->drag_leave();
		return S_OK;
	}

	STDMETHODIMP DragOver(const DWORD grfKeyState, const POINTL pt, DWORD* pdwEffect) override
	{
		const auto h = _host.lock();

		if (h)
		{
			const data_object_client data(_drag_data.Get());
			const auto result = h->drag_over(data, to_key_state(grfKeyState), to_client(pt));
			set_drop_effect(result, pdwEffect);
		}

		return S_OK;
	}

	STDMETHODIMP Drop(IDataObject* pDataObj, const DWORD grfKeyState, const POINTL pt, DWORD* pdwEffect) override
	{
		// Drop ends the drag, so the source data object must not be held past this call.
		_drag_data = nullptr;

		const auto h = _host.lock();

		if (h)
		{
			data_object_client data(pDataObj);
			const auto result = h->drag_drop(data, to_key_state(grfKeyState), to_client(pt));
			set_drop_effect(result, pdwEffect);
		}

		return S_OK;
	}
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class win32_menu
{
public:
	HMENU m_hMenu;


	win32_menu(const HMENU hMenu = nullptr) : m_hMenu(hMenu)
	{
	}

	~win32_menu()
	{
		if (m_hMenu != nullptr)
			DestroyMenu(m_hMenu);
	}

	BOOL CreatePopupMenu()
	{
		df::assert_true(m_hMenu == nullptr);
		m_hMenu = ::CreatePopupMenu();
		return m_hMenu != nullptr ? TRUE : FALSE;
	}

	BOOL AppendMenu(const uint32_t nFlags, const UINT_PTR nIDNewItem = 0, const LPCTSTR lpszNewItem = nullptr) const
	{
		df::assert_true(IsMenu(m_hMenu));
		return ::AppendMenu(m_hMenu, nFlags, nIDNewItem, lpszNewItem);
	}

	BOOL AppendMenu(const uint32_t nFlags, HMENU hSubMenu, const LPCTSTR lpszNewItem) const
	{
		df::assert_true(IsMenu(m_hMenu));
		df::assert_true(IsMenu(hSubMenu));
		return ::AppendMenu(m_hMenu, nFlags | MF_POPUP, (UINT_PTR)hSubMenu, lpszNewItem);
	}

	BOOL InsertMenuItem(const uint32_t uItem, const BOOL bByPosition, const LPMENUITEMINFO lpmii) const
	{
		df::assert_true(IsMenu(m_hMenu));
		return ::InsertMenuItem(m_hMenu, uItem, bByPosition, lpmii);
	}

	int GetMenuItemCount() const
	{
		df::assert_true(IsMenu(m_hMenu));
		return ::GetMenuItemCount(m_hMenu);
	}

	HMENU Detach()
	{
		const HMENU hMenu = m_hMenu;
		m_hMenu = nullptr;
		return hMenu;
	}

	operator HMENU() const
	{
		return m_hMenu;
	}
};

static constexpr sizei menu_size_border = {3, 3};

class win32_app final : public ui::platform_app
{
public:
	ui::app_ptr _app;
	std::vector<HANDLE> app_thread_events;
	std::vector<std::function<void()>> app_event_actions;
	HANDLE _timer_handle = nullptr;
	int _frame_delay = 0;
	std::shared_ptr<control_host_impl> _frame;

	// The notification carries no path, so the watched folder is retained here to tell the app which
	// folder to compare.
	struct folder_watch
	{
		HANDLE h = nullptr;
		df::folder_path path;
	};

	std::vector<folder_watch> _folder_changes;
	platform::thread_event _idle_event;
	LPARAM _last_mouse_move = 0;
	wchar_t _pending_high_surrogate = 0;
	bool _enable_screen_saver = true;
	DWORD _dwAppBarState = 0;
	factories_ptr _f;

	win32_app() : _idle_event(false, false)
	{
	}

	void update_event_handles();
	void tick() const;

	static bool is_pre_translate_message(const int message)
	{
		return message != WM_TIMER &&
			message != WM_PAINT &&
			message != WM_ERASEBKGND;
	}

	static bool is_edit_char(const HWND hwnd, const char32_t c, const ui::key_state keys)
	{
		if (keys.control)
		{
			if (c == 'C' ||
				c == 'Z' ||
				c == 'X' ||
				c == 'V' ||
				c == 'A')
			{
				if (c == 'A')
				{
					// Select all
					SendMessage(hwnd, EM_SETSEL, 0, -1);
				}

				return true;
			}
		}

		if (c == VK_RETURN && wants_return(hwnd))
		{
			return true;
		}

		/*if (iswcntrl())


		return
			c == VK_SPACE ||
			c == VK_LEFT ||
			c == VK_RIGHT ||
			c == VK_UP ||
			c == VK_DOWN ||
			c == VK_HOME ||
			c == VK_END ||
			c == VK_INSERT ||
			c == VK_DELETE ||
			c == VK_BACK;*/

		return c != VK_ESCAPE &&
			c != VK_RETURN &&
			c != VK_TAB;
	}

	bool pre_translate_message(MSG& m);
	void monitor_folders(const std::vector<df::folder_path>& folders_paths) override;
	void destroy();
	void idle() const;
	int ui_message_loop();
	uint32_t ui_wait_for_signal(const std::vector<std::reference_wrapper<platform::thread_event>>& events,
	                            bool wait_all, uint32_t timeout_ms, const std::function<bool(LPMSG m)>& cb);
	void sys_command(ui::sys_command_type cmd) override;
	void full_screen(bool full) override;

	void frame_delay(int delay) override;

	void queue_idle() override
	{
		_idle_event.set();
	}

	void set_font_base_size(int i) override;

	int get_font_base_size() const override
	{
		return global_base_font_size;
	}

	ui::control_frame_ptr
	create_app_frame(const platform::setting_file_ptr& store, const ui::frame_host_weak_ptr& host) override;

	void enable_screen_saver(bool enable) override;
};

class control_host_impl final :
	public frame_base,
	public ui_frame_window<control_host_impl, ui::control_frame>,
	public std::enable_shared_from_this<control_host_impl>
{
public:
	using base_class = frame_base;
	using this_class = control_host_impl;
	using this_type = control_host_impl;

	control_host_impl(const control_frame* other) = delete;
	control_host_impl& operator=(const control_frame& other) = delete;

	using control_base2_ptr = std::shared_ptr<control_base2>;

	std::unordered_map<int, control_base2_ptr> _children;
	std::unordered_map<unsigned long, std::shared_ptr<ui::command>> _menu_commands;
	std::vector<std::weak_ptr<frame_impl>> _child_frames;
	std::vector<std::weak_ptr<control_host_impl>> _child_hosts;

	// Radio group of the most recently created radio button; used to mark the first button of each
	// group with WS_GROUP so Windows scopes auto-radio exclusivity to that group.
	std::optional<int> _last_radio_group;

	win32_app* _pa = nullptr;
	ui::weak_app_ptr _app;
	ui::frame_host_weak_ptr _host;

	platform::thread_event _close;
	std::atomic<ui::close_result> _modal_result = ui::close_result::ok;
	bool _tracking = false;
	bool _is_popup = false;
	bool _hover = false;
	bool _is_first_nccalc_ = true;
	UINT_PTR _timer_id = 0;

	ui::color_style _colors;

	win_rect _window_rgn;
	win_rect _full_screen_bounds;
	bool _composition_enabled = false;
	bool _is_full_screen = false;
	bool _is_app_frame = false;
	bool _has_focus = false;
	int _main_thread_default_priority = 0;
	int _main_thread_current_priority = 0;
	mutable int _next_id = 2000;
	int _def_id = IDOK;
	int _cancel_id = IDCANCEL;

	WINDOWPLACEMENT restore_window_placement{};
	DWORD restore_style = 0;
	DWORD restore_ex_style = 0;
	recti _hover_command_bounds;
	uint32_t cmd_show = SW_SHOW;

	control_host_impl(const owner_context_ptr& ctx, const ui::frame_host_weak_ptr& host, ui::weak_app_ptr app,
	                  win32_app* pa,
	                  const bool is_app_frame, const ui::color_style& colors) :
		frame_base(ctx),
		_pa(pa),
		_app(std::move(app)),
		_host(host),
		_close(true, false),
		_colors(colors),
		_is_app_frame(is_app_frame)
	{
	}

	~control_host_impl() override
	{
		// destroy_frame_base() nulls m_hWnd, so the handle is captured first or the window leaks.
		// The window is destroyed before clear() because the child control windows hold subclass
		// and parent pointers into the control objects that clear() frees.
		const auto hwnd = m_hWnd;

		destroy_frame_base();

		if (hwnd && IsWindow(hwnd))
		{
			DestroyWindow(hwnd);
		}

		clear();
		m_hWnd = nullptr;
	}

	HWND Create(
		_In_opt_ const HWND hWndParent,
		_In_ const win_rect rect,
		_In_ const DWORD dwStyle,
		_In_ const DWORD dwExStyle)
	{
		const auto* const class_name = _is_app_frame ? L"DIFF_MAIN" : L"DIFF_DLG";
		auto* const icon = _is_app_frame ? resources.diffractor_32 : nullptr;

		if (register_class(CS_DBLCLKS, icon, nullptr, nullptr,
		                   nullptr, class_name, nullptr))
		{
			auto x = rect.left;
			auto y = rect.top;
			auto cx = rect.width();
			auto cy = rect.height();

			if (rect.is_empty())
			{
				x = CW_USEDEFAULT;
				y = CW_USEDEFAULT;
				cx = CW_USEDEFAULT;
				cy = CW_USEDEFAULT;
			}

			m_hWnd = CreateWindowEx(
				dwExStyle,
				class_name,
				nullptr,
				dwStyle,
				x,
				y,
				cx,
				cy,
				hWndParent, nullptr,
				get_resource_instance,
				this);


			create_draw_context(_pa->_f, false, false);

			df::assert_true(IsWindow(m_hWnd));
		}

		return m_hWnd;
	}

	void destroy() override
	{
		DestroyWindow(m_hWnd);
		clear();
	}

	void position(const recti bounds) override
	{
		SetWindowPos(m_hWnd, nullptr, bounds.left, bounds.top, bounds.width(), bounds.height(),
		             SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
	}

	void clear()
	{
		_children.clear();
		_menu_commands.clear();
		_modal_result = ui::close_result::ok;
	}

	int alloc_ids(const int count = 1) const
	{
		const auto result = _next_id;
		_next_id += count;
		return result;
	}

	LRESULT on_notify(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam)
	{
		const auto id = static_cast<int>(wParam);
		const auto pnmh = std::bit_cast<LPNMHDR>(lParam);
		const auto child = _children.find(id);

		if (child != _children.end())
		{
			return child->second->on_notify(_host, _colors, id, pnmh);
		}

		return DefWindowProc(m_hWnd, uMsg, wParam, lParam);
	}

	// These two messages aren't defined in winuser.h, but they are sent to windows
	// with captions. They appear to paint the window caption and frame.
	// Unfortunately if you override the standard non-client rendering as we do
	// with CustomFrameWindow, sometimes Windows (not deterministically
	// reproducibly but definitely frequently) will send these messages to the
	// window and paint the standard caption/title over the top of the custom one.
	// So we need to handle these messages in CustomFrameWindow to prevent this
	// from happening.
	const int WM_NCUAHDRAWCAPTION = 0xAE;
	const int WM_NCUAHDRAWFRAME = 0xAF;

	LRESULT handle_message(const HWND hWnd, const UINT uMsg, const WPARAM wParam, const LPARAM lParam) override
	{
		if (uMsg == WM_CREATE) return on_window_create(uMsg, wParam, lParam);
		if (uMsg == WM_DESTROY) return on_window_destroy(uMsg, wParam, lParam);
		if (uMsg == WM_PRINTCLIENT) return on_window_print_client(uMsg, wParam, lParam);
		if (uMsg == WM_ERASEBKGND) return on_window_erase_background(uMsg, wParam, lParam);
		if (uMsg == WM_ACTIVATE) return on_window_activate(uMsg, wParam, lParam);
		if (uMsg == WM_SETFOCUS) return on_window_set_focus(uMsg, wParam, lParam);
		if (uMsg == WM_KILLFOCUS) return on_window_kill_focus(uMsg, wParam, lParam);
		if (uMsg == WM_CLOSE) return on_window_close(uMsg, wParam, lParam);
		if (uMsg == WM_COMMAND) return on_window_command(uMsg, wParam, lParam);
		if (uMsg == WM_HSCROLL) return on_window_hscroll(uMsg, wParam, lParam);
		if (uMsg == WM_CONTEXTMENU) return on_window_context_menu(uMsg, wParam, lParam);
		if (uMsg == WM_CTLCOLORBTN) return on_window_color(uMsg, wParam, lParam);
		if (uMsg == WM_CTLCOLORDLG) return on_window_color(uMsg, wParam, lParam);
		if (uMsg == WM_CTLCOLOREDIT) return on_window_color(uMsg, wParam, lParam);
		if (uMsg == WM_CTLCOLORLISTBOX) return on_window_color(uMsg, wParam, lParam);
		if (uMsg == WM_CTLCOLORSTATIC) return on_window_color(uMsg, wParam, lParam);
		if (uMsg == WM_DRAWITEM) return on_window_draw_item(uMsg, wParam, lParam);
		if (uMsg == WM_MEASUREITEM) return on_window_measure_item(uMsg, wParam, lParam);
		if (uMsg == WM_TIMER) return on_window_timer(uMsg, wParam, lParam);
		if (uMsg == WM_DPICHANGED) return on_window_dpi_changed(uMsg, wParam, lParam);
		if (uMsg == WM_LBUTTONDOWN) return on_mouse_left_button_down(uMsg, wParam, lParam);
		if (uMsg == WM_LBUTTONUP) return on_mouse_left_button_up(uMsg, wParam, lParam);
		if (uMsg == WM_XBUTTONUP) return on_mouse_x_button_up(uMsg, wParam, lParam);
		if (uMsg == WM_MOUSELEAVE) return on_mouse_leave(uMsg, wParam, lParam);
		if (uMsg == WM_MOUSEMOVE) return on_mouse_move(uMsg, wParam, lParam);
		if (uMsg == WM_MOUSEWHEEL) return on_mouse_wheel(uMsg, wParam, lParam);
		if (uMsg == WM_LBUTTONDBLCLK) return on_mouse_left_button_double_click(uMsg, wParam, lParam);
		if (uMsg == WM_NCHITTEST) return on_window_nc_hit_test(uMsg, wParam, lParam);
		if (uMsg == DM_GETDEFID) return on_get_def_id(uMsg, wParam, lParam);
		if (uMsg == WM_NOTIFY) return on_notify(uMsg, wParam, lParam);

		if (_is_app_frame)
		{
			if (uMsg == WM_WTSSESSION_CHANGE) return on_session_change(uMsg, wParam, lParam);
			if (uMsg == WM_POWERBROADCAST) return on_power_broadcast(uMsg, wParam, lParam);
			if (uMsg == WM_QUERYENDSESSION) return on_query_end_session(uMsg, wParam, lParam);
			if (uMsg == WM_ENDSESSION) return on_end_session(uMsg, wParam, lParam);
			if (uMsg == WM_SETTINGCHANGE) return on_system_settings_change(uMsg, wParam, lParam);
			if (uMsg == WM_DEVICECHANGE) return on_system_device_change(uMsg, wParam, lParam);
			if (uMsg == WM_GETMINMAXINFO) return on_window_min_max_info(uMsg, wParam, lParam);
			if (uMsg == WM_INITMENUPOPUP) return on_window_menu_popup(uMsg, wParam, lParam);
			if (uMsg == WM_NCACTIVATE) return on_window_nc_activate(uMsg, wParam, lParam);
			if (uMsg == WM_NCCALCSIZE) return on_window_nc_calc_size(uMsg, wParam, lParam);
			if (uMsg == WM_NCPAINT) return on_window_nc_paint(uMsg, wParam, lParam);
			if (uMsg == WM_NCUAHDRAWCAPTION) return 0;
			if (uMsg == WM_NCUAHDRAWFRAME) return 0;
			if (uMsg == WM_DWMCOMPOSITIONCHANGED) return on_dwm_composition_changed(uMsg, wParam, lParam);
		}
		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}


	LRESULT on_window_set_focus(uint32_t /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/)
	{
		const auto h = _host.lock();
		if (h) h->focus_changed(true, nullptr);
		_has_focus = true;
		return 0;
	}

	LRESULT on_window_kill_focus(uint32_t /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/)
	{
		const auto h = _host.lock();
		if (h) h->focus_changed(false, nullptr);
		_has_focus = false;
		return 0;
	}

	LRESULT on_window_create(uint32_t /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/);

	LRESULT on_window_destroy(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam)
	{
		if (_timer_id) KillTimer(m_hWnd, _timer_id);
		if (_is_app_frame) WTSUnRegisterSessionNotification(m_hWnd);
		const auto h = _host.lock();
		if (h) h->on_window_destroy();

		// destroy_frame_base() nulls m_hWnd, so the handle has to be captured first or
		// DefWindowProc is called with nullptr and the default WM_DESTROY handling is skipped.
		auto* const hwnd = m_hWnd;
		destroy_frame_base();
		const auto result = DefWindowProc(hwnd, uMsg, wParam, lParam);
		clear();

		return result;
	}

	LRESULT on_window_close(uint32_t /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
	{
		const auto app = _app.lock();

		if (_is_app_frame && app)
		{
			if (app->can_exit())
			{
				df::is_closing = true;
				platform::event_exit.set();

				DestroyWindow(m_hWnd);
				PostQuitMessage(0);
			}
		}
		else
		{
			_modal_result = ui::close_result::cancel;
			_close.set();
		}

		return 0;
	}

	LRESULT on_window_nc_hit_test(uint32_t /*uMsg*/, WPARAM wParam, LPARAM lParam) const;

	LRESULT on_get_def_id(uint32_t /*uMsg*/, WPARAM wParam, LPARAM lParam) const
	{
		return MAKELONG(_def_id, DC_HASDEFID);
	}

	void on_render(const draw_context_device_ptr& ctx_in) override
	{
		const auto ctx = _draw_ctx;

		if (ctx)
		{
			ctx->frame_has_focus = _has_focus;
			ctx->colors = _colors;
			ctx->clear(ui::color(_colors.background, 1.0f));

			const auto h = _host.lock();

			if (h)
			{
				h->on_window_paint(*ctx);
			}
		}
	}

	void on_resize(const sizei extent, const bool is_minimized) override
	{
		const auto ctx = _draw_ctx;

		if (ctx)
		{
			update_region();
			ctx->resize(_extent);

			const auto h = _host.lock();

			if (h)
			{
				h->on_window_layout(*ctx, _extent, is_minimized);
			}
		}
	}

	static LRESULT on_window_print_client(uint32_t /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/)
	{
		return 0;
	}

	LRESULT on_window_erase_background(uint32_t /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/) const
	{
		return 1;
	}

	ui::color_style calc_colors(const HWND child) const
	{
		const auto found = _children.find(GetDlgCtrlID(child));

		if (found != _children.end())
		{
			const auto child_color = found->second->calc_colors();
			if (child_color.background != 0) return child_color;
		}

		wchar_t class_name[100] = {0};
		::GetClassName(child, class_name, 100);

		if (is_edit_class(class_name))
		{
			return {ui::style::color::edit_background, ui::style::color::edit_text};
		}

		return _colors;
	}

	LRESULT on_window_color(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam) const
	{
		const auto h = std::bit_cast<HWND>(lParam);
		const auto colors = calc_colors(h);

		if (wParam != 0)
		{
			const auto dc = std::bit_cast<HDC>(wParam);
			SetBkColor(dc, colors.background);
			SetTextColor(dc, colors.foreground);
		}

		return std::bit_cast<LRESULT>(_gdi_ctx->gdi_brush(colors.background));
	}

	LRESULT on_mouse_move(uint32_t /*uMsg*/, WPARAM wParam, const LPARAM lParam)
	{
		const pointi loc(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

		if (!_hover)
		{
			track_mouse_leave(m_hWnd);
			_hover = true;
		}

		const auto h = _host.lock();
		if (h) h->on_mouse_move(loc, _tracking);
		return 0;
	}

	LRESULT on_mouse_left_button_down(uint32_t /*uMsg*/, const WPARAM wParam, const LPARAM lParam)
	{
		const pointi loc(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

		if (!_tracking)
		{
			_tracking = true;
			SetCapture(m_hWnd);
		}

		const auto h = _host.lock();
		if (h) h->on_mouse_left_button_down(loc, to_key_state(wParam));
		return 0;
	}

	LRESULT on_mouse_left_button_up(uint32_t /*uMsg*/, const WPARAM wParam, const LPARAM lParam)
	{
		const pointi loc(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

		if (_tracking)
		{
			_tracking = false;
			ReleaseCapture();
			InvalidateRect(m_hWnd, nullptr, TRUE);
		}

		const auto h = _host.lock();
		if (h) h->on_mouse_left_button_up(loc, to_key_state(wParam));
		return 0;
	}

	LRESULT on_mouse_left_button_double_click(uint32_t /*uMsg*/, const WPARAM wParam, const LPARAM lParam) const
	{
		const pointi loc(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		const auto h = _host.lock();
		if (h) h->on_mouse_left_button_double_click(loc, to_key_state(wParam));
		return 0;
	}

	LRESULT on_mouse_leave(uint32_t /*uMsg*/, WPARAM /*wParam*/, const LPARAM lParam)
	{
		const pointi loc(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		_hover = false;
		const auto h = _host.lock();
		if (h) h->on_mouse_leave(loc);
		InvalidateRect(m_hWnd, nullptr, TRUE);
		return 0;
	}

	LRESULT on_mouse_wheel(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam) const
	{
		POINT loc = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
		ScreenToClient(m_hWnd, &loc);

		bool was_handled = false;
		const auto h = _host.lock();
		if (h) h->on_mouse_wheel({loc.x, loc.y}, static_cast<short>(HIWORD(wParam)), to_key_state(wParam), was_handled);
		if (!was_handled && IsWindow(GetParent(m_hWnd)))
			SendMessage(GetParent(m_hWnd), uMsg, wParam, lParam);
		return 0;
	}

	LRESULT on_mouse_x_button_up(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam) const
	{
		const pointi loc(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		const int fwButton = GET_XBUTTON_WPARAM(wParam);
		const auto h = _host.lock();

		switch (fwButton)
		{
		case XBUTTON1:
			if (h) h->on_mouse_other_button_up(ui::other_mouse_button::xb1, loc, to_key_state(wParam));
			break;
		case XBUTTON2:
			if (h) h->on_mouse_other_button_up(ui::other_mouse_button::xb2, loc, to_key_state(wParam));
			break;
		default: break;
		}

		return 1;
	}

	static BOOL CALLBACK focus_first_callback(const HWND h, const LPARAM p)
	{
		const auto first_focused = std::bit_cast<bool*>(p);

		wchar_t szClassName[100] = {0};
		::GetClassName(h, szClassName, 100);

		const auto is_edit_control = is_edit_class(szClassName);
		const auto is_button_control = _wcsicmp(L"BUTTON", szClassName) == 0;

		if (is_edit_control || is_button_control)
		{
			if (IsWindowEnabled(h)) // && ::IsWindowVisible(h))
			{
				*first_focused = true;
				SetFocus(h);
				if (is_edit_control)
				{
					::PostMessage(h, EM_SETSEL, 0, -1);
				}
				return false;
			}
		}

		return true;
	}

	static BOOL CALLBACK focus_any_callback(const HWND h, LPARAM p)
	{
		if (IsWindowEnabled(h)) // && ::IsWindowVisible(h))
		{
			SetFocus(h);
			return false;
		}

		return true;
	}

	double scale_factor() const override
	{
		return _gdi_ctx->scale_factor;
	}

	void focus_first() override
	{
		bool first_focused = false;

		EnumChildWindows(m_hWnd, focus_first_callback, std::bit_cast<LPARAM>(&first_focused));

		if (!first_focused)
		{
			EnumChildWindows(m_hWnd, focus_any_callback, 0);
		}
	}

	void set_cursor(const ui::style::cursor cursor) override
	{
		HICON icon = nullptr;
		if (cursor_icon(cursor, icon)) set_cursor(icon);
	}

	void set_cursor(const HICON cursor)
	{
		_cursor = cursor;
		SetCursor(cursor);
	}

	ui::close_result wait_for_close(const uint32_t timeout_ms) override
	{
		_close.reset();
		const auto waited = ui_wait_for_signal(_close, timeout_ms,
		                                       [this](const LPMSG m) { return pre_translate_message(m); });
		// _modal_result holds ok until close() overwrites it, so a wait that never saw the close
		// event must not be reported as a decision - a confirmation would read it as approval.
		if (waited != 0) return ui::close_result::cancel;
		return _modal_result.load();
	}

	void close(const bool is_cancel) override
	{
		if (_is_app_frame)
		{
			::PostMessage(m_hWnd, WM_CLOSE, 0, 0);
		}
		else
		{
			_modal_result = is_cancel ? ui::close_result::cancel : ui::close_result::ok;
			_close.set();
		}
	}

	bool is_canceled() const override
	{
		return _modal_result.load() == ui::close_result::cancel;
	}

	bool pre_translate_message(const LPMSG m) const
	{
		BOOL bHandled = 0;
		if (!m_hWnd) return false;

		return IsDialogMessage(m_hWnd, m) != 0;
	}

	void invalidate(const recti bounds, const bool erase) override
	{
		if (bounds.is_empty())
		{
			InvalidateRect(m_hWnd, nullptr, erase);
		}
		else
		{
			InvalidateRect(m_hWnd, win_rect(bounds), erase);
		}
	}

	void scroll(const int dx, const int dy, const recti bounds, const bool scroll_child_controls) override
	{
		scroll_impl(m_hWnd, dx, dy, bounds, scroll_child_controls);
	}

	struct pending_move
	{
		HWND h = nullptr;
		recti bounds;
		recti old_bounds;
		bool visible = false;
		bool size_changed = true;
		bool self_painting = false;

		bool suppress_redraw() const noexcept
		{
			return visible;
		}

		static BOOL CALLBACK invalidate_direct_child(const HWND child, const LPARAM parent_param)
		{
			const auto parent = std::bit_cast<HWND>(parent_param);

			if (GetParent(child) == parent)
			{
				RedrawWindow(child, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME);
			}

			return TRUE;
		}

		UINT flags() const noexcept
		{
			// The layout order does not define stacking, and restacking repaints every sibling it passes.
			//
			// SetWindowPos paints synchronously: it copies a moved window's pixels to their new position and
			// repaints a resized window's frame while the rest of the batch is still at its old geometry. A
			// control that draws its whole appearance in the non-client area shows that as a border arriving
			// ahead of everything around it. SWP_NOREDRAW suppresses the copy, the frame paint and the parent
			// repaint alike, so both are deferred and invalidate_after_move re-arms them once every control
			// is in place, for the host to flush in one pass.
			const auto defer_paint = suppress_redraw();

			return SWP_NOACTIVATE | SWP_NOZORDER | (size_changed ? SWP_NOCOPYBITS : 0u) |
				(defer_paint ? SWP_NOREDRAW : 0u) |
				(visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW);
		}

		void invalidate_after_move() const noexcept
		{
			if (!visible) return;

			// A child whose parent moved but whose relative bounds did not change was skipped by the
			// nested layout, so repaint direct children without rendering the parent surface again.
			if (self_painting)
			{
				EnumChildWindows(h, invalidate_direct_child, std::bit_cast<LPARAM>(h));
				return;
			}

			RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
		}
	};

	void apply_layout(const ui::control_layouts& controls, const pointi scroll_offset) override
	{
		// A layout pass runs for many state changes, so most controls are already where they belong.
		// Moving an unchanged window still repaints it, so only changed windows are touched here.
		std::vector<pending_move> moves;
		moves.reserve(controls.size());

		for (const auto& c : controls)
		{
			if (!c.control) continue;

			const auto h = std::any_cast<HWND>(c.control->handle());
			df::assert_true(IsWindow(h));
			if (!IsWindow(h)) continue;

			// The window's own style bit, not IsWindowVisible, because a hidden host must not make
			// every child look hidden and force a redundant move.
			const auto style = GetWindowLong(h, GWL_STYLE);
			const auto was_visible = (style & WS_VISIBLE) != 0;
			if (!c.visible && !was_visible) continue;

			auto bounds = c.bounds;
			if (c.offset) bounds = bounds.offset(scroll_offset);

			auto changed = true;
			auto size_changed = true;
			auto current_bounds = bounds;

			// Change detection compares against this window's client-relative position, which is
			// only what SetWindowPos means for a child. Anything else is always applied.
			if ((style & WS_CHILD) != 0)
			{
				RECT current = {};
				GetWindowRect(h, &current);
				MapWindowPoints(HWND_DESKTOP, m_hWnd, reinterpret_cast<POINT*>(&current), 2);
				current_bounds = recti(current.left, current.top, current.right, current.bottom);

				changed = current_bounds != bounds || was_visible != c.visible;
				size_changed = current_bounds.width() != bounds.width() ||
					current_bounds.height() != bounds.height();
			}

			if (!changed) continue;

			moves.push_back({
				h, bounds, was_visible ? current_bounds : recti{}, c.visible, size_changed,
				std::dynamic_pointer_cast<frame>(c.control) != nullptr
			});
		}

		if (moves.empty()) return;

		const auto flush_moves = [&moves, this]
		{
			recti vacated;

			for (const auto& m : moves)
			{
				m.invalidate_after_move();
				if (m.suppress_redraw()) vacated = vacated.make_union(m.old_bounds);
			}

			// SWP_NOREDRAW also suppresses the parent repaint a move would normally trigger, so the
			// area a control leaves behind has to be invalidated here.
			if (!vacated.is_empty())
			{
				const RECT r = {vacated.left, vacated.top, vacated.right, vacated.bottom};
				RedrawWindow(m_hWnd, &r, nullptr, RDW_INVALIDATE);
			}
		};

		HDWP hdwp = BeginDeferWindowPos(static_cast<int>(moves.size()));

		if (hdwp)
		{
			for (const auto& m : moves)
			{
				hdwp = DeferWindowPos(hdwp, m.h, nullptr, m.bounds.left, m.bounds.top, m.bounds.width(),
				                      m.bounds.height(), m.flags());
				df::assert_true(hdwp != nullptr);
				if (!hdwp) break;
			}

			if (hdwp)
			{
				EndDeferWindowPos(hdwp);
				flush_moves();
				return;
			}
		}

		for (const auto& m : moves)
		{
			SetWindowPos(m.h, nullptr, m.bounds.left, m.bounds.top, m.bounds.width(), m.bounds.height(), m.flags());
		}

		flush_moves();
	}

	LRESULT on_window_context_menu(uint32_t /*uMsg*/, WPARAM wParam, const LPARAM lParam)
	{
		const pointi loc = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
		const auto h = _host.lock();

		if (h)
		{
			const auto commands = h->menu(loc);

			if (!commands.empty())
			{
				show_menu(recti(loc, sizei()), commands);
			}
		}

		return 0;
	}

	LRESULT on_window_min_max_info(uint32_t /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/) const;

	LRESULT on_window_activate(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam) const
	{
		const LPARAM r = DefWindowProc(m_hWnd, uMsg, wParam, lParam);
		const auto is_active = wParam == WA_ACTIVE || wParam == WA_CLICKACTIVE;
		const auto h = _host.lock();
		if (h) h->activate(is_active);
		return r;
	}

	LRESULT on_window_timer(uint32_t /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/);

	LRESULT on_system_device_change(uint32_t /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/) const;
	LRESULT on_window_erase_background(uint32_t /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/);
	LRESULT on_window_command(uint32_t uMsg, WPARAM wParam, LPARAM lParam);
	LRESULT on_window_hscroll(uint32_t uMsg, WPARAM wParam, LPARAM lParam);

	static LRESULT on_window_menu_popup(uint32_t /*uMsg*/, const WPARAM wParam, LPARAM lParam)
	{
		const auto hMenu = std::bit_cast<HMENU>(wParam);

		if (hMenu)
		{
		}

		return 0;
	}

	LRESULT on_window_draw_item(uint32_t /*uMsg*/, WPARAM /*wParam*/, const LPARAM lParam) const
	{
		draw_item(std::bit_cast<LPDRAWITEMSTRUCT>(lParam));
		return TRUE;
	}

	LRESULT on_window_measure_item(uint32_t /*uMsg*/, WPARAM /*wParam*/, const LPARAM lParam) const
	{
		measure_item(std::bit_cast<LPMEASUREITEMSTRUCT>(lParam));
		return 1;
	}

	LRESULT on_dwm_composition_changed(uint32_t /*uMsg*/, WPARAM wParam, LPARAM lParam);
	LRESULT on_window_nc_activate(uint32_t, WPARAM, LPARAM) const;
	LRESULT on_window_nc_calc_size(uint32_t, WPARAM wParam, LPARAM lParam);
	LRESULT on_window_nc_paint(uint32_t, WPARAM wParam, LPARAM lParam) const;

	void update_region();
	void handle_composition_changed();

	void draw_item(const LPDRAWITEMSTRUCT lpDrawItemStruct) const
	{
		if (lpDrawItemStruct->CtlType == ODT_MENU)
		{
			const auto found = _menu_commands.find(lpDrawItemStruct->itemID);
			draw_menu_item(found != _menu_commands.end() ? found->second : nullptr, lpDrawItemStruct, _gdi_ctx);
		}
	}

	void measure_item(const LPMEASUREITEMSTRUCT lpMeasureItemStruct) const
	{
		if (lpMeasureItemStruct->CtlType == ODT_MENU)
		{
			const auto scale = scale_factor();
			const auto found = _menu_commands.find(lpMeasureItemStruct->itemID);

			if (found == _menu_commands.end())
			{
				lpMeasureItemStruct->itemWidth = 0;
				lpMeasureItemStruct->itemHeight = df::round(8 * scale);
			}
			else
			{
				const auto c = found->second;
				auto* const dc = GetDC(m_hWnd);

				if (dc)
				{
					auto* const old_font = SelectObject(dc, _gdi_ctx->dialog);
					const auto text_w = str::utf8_to_utf16(c->text);
					const auto keyboard_accelerator_w = str::utf8_to_utf16(c->keyboard_accelerator_text);

					win_rect text_bounds;
					win_rect keyboard_accelerator_bounds;
					DrawText(dc, text_w.data(), static_cast<int>(text_w.size()), &text_bounds,
					         DT_SINGLELINE | DT_LEFT | DT_CALCRECT);
					DrawText(dc, keyboard_accelerator_w.data(), static_cast<int>(keyboard_accelerator_w.size()),
					         &keyboard_accelerator_bounds, DT_SINGLELINE | DT_LEFT | DT_CALCRECT);
					SelectObject(dc, old_font);


					lpMeasureItemStruct->itemWidth = text_bounds.width() + keyboard_accelerator_bounds.width() +
						df::round(64 * scale);
					lpMeasureItemStruct->itemHeight = std::max(text_bounds.height() + df::round(8 * scale),
					                                           df::round(20 * scale));

					ReleaseDC(m_hWnd, dc);
				}
			}
		}
	}

	void full_screen(const bool full)
	{
		if (full != _is_full_screen)
		{
			_is_full_screen = full;

			if (full)
			{
				restore_style = GetWindowLong(m_hWnd, GWL_STYLE);
				restore_ex_style = GetWindowLong(m_hWnd, GWL_EXSTYLE);

				restore_window_placement.length = sizeof(restore_window_placement);
				GetWindowPlacement(m_hWnd, &restore_window_placement);

				const auto new_style = restore_style & ~(WS_CAPTION | WS_THICKFRAME);
				const auto new_ex_style = restore_ex_style & ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE
					| WS_EX_STATICEDGE);

				SetWindowLong(m_hWnd, GWL_STYLE, new_style);
				SetWindowLong(m_hWnd, GWL_EXSTYLE, new_ex_style);

				auto bounds = win_rect(desktop_bounds_impl(m_hWnd, false));
				AdjustWindowRectEx(&bounds, new_style, FALSE, new_ex_style);
				_full_screen_bounds = bounds;

				auto wp = restore_window_placement;
				wp.showCmd = SW_SHOWNORMAL;
				wp.rcNormalPosition = bounds;

				SetWindowPlacement(m_hWnd, &wp);
			}
			else
			{
				SetWindowLong(m_hWnd, GWL_STYLE, restore_style);
				SetWindowLong(m_hWnd, GWL_EXSTYLE, restore_ex_style);

				SetWindowPlacement(m_hWnd, &restore_window_placement);
			}

			handle_composition_changed();
		}
	}

	LRESULT on_session_change(uint32_t /*uMsg*/, const WPARAM wParam, LPARAM /*lParam*/) const
	{
		const auto app = _app.lock();

		if (app && wParam == WTS_SESSION_LOCK)
		{
			app->system_event(ui::os_event_type::screen_locked);
		}
		return 0;
	}

	LRESULT on_power_broadcast(uint32_t /*uMsg*/, const WPARAM wParam, LPARAM /*lParam*/) const
	{
		const auto app = _app.lock();

		if (app)
		{
			if (wParam == PBT_APMSUSPEND)
			{
				app->system_event(ui::os_event_type::system_suspending);
			}
			else if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND)
			{
				// Both can arrive for one wake; the resume handling is only invalidation, so it
				// is safe to run twice.
				app->system_event(ui::os_event_type::system_resumed);
			}
		}

		return TRUE;
	}

	// Deliberately never calls can_exit(): a shutting-down application gets no opportunity to
	// prompt, so refusing or blocking here would only earn the "this app is preventing shutdown"
	// screen. State is persisted and the session is allowed to end.
	LRESULT on_query_end_session(uint32_t /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/) const
	{
		const auto app = _app.lock();
		if (app) app->system_event(ui::os_event_type::session_ending);
		return TRUE;
	}

	LRESULT on_end_session(uint32_t /*uMsg*/, const WPARAM wParam, LPARAM /*lParam*/) const
	{
		// wParam is FALSE when another application cancelled the shutdown after we agreed to it.
		if (wParam)
		{
			const auto app = _app.lock();
			if (app) app->system_event(ui::os_event_type::session_ending);

			// WM_DESTROY and the normal teardown never run - the process is terminated once this
			// returns - so workers are released and the log is flushed here instead.
			df::is_closing = true;
			platform::event_exit.set();
			df::close_log();
		}

		return 0;
	}

	void update_font_sizes() const
	{
		if (_draw_ctx)
		{
			_draw_ctx->update_font_size(_gdi_ctx->calc_base_font_size());
		}

		update_dpi_metrics();

		if (_gdi_ctx)
		{
			// Refresh the shared GDI fonts so native child controls (edit boxes etc.)
			// pick up the new base size - update_scale_factor only rebuilds them on a
			// DPI change, not on a large-font toggle.
			_gdi_ctx->update_fonts();
			_gdi_ctx->set_window_font(m_hWnd, ui::style::font_face::dialog);
		}

		for (const auto& c : _children)
		{
			c.second->dpi_changed();
		}

		for (const auto& f : _child_frames)
		{
			auto ff = f.lock();

			if (ff)
			{
				ff->dpi_changed();
			}
		}

		// A child control host is not a control and not a frame, so it is in neither list above
		// and, being a child window, never receives WM_DPICHANGED of its own. Without this its
		// draw context keeps the scale it was created with while the controls inside it are
		// re-fonted from the shared context.
		for (const auto& h : _child_hosts)
		{
			auto hh = h.lock();

			if (hh)
			{
				hh->dpi_changed();
			}
		}
	}

	// A child host shares its parent's owner context, so the GDI fonts have already been rebuilt
	// by the parent; only this host's own draw context and contents still need the new scale.
	void dpi_changed() const
	{
		if (_draw_ctx)
		{
			_draw_ctx->update_font_size(_gdi_ctx->calc_base_font_size());
		}

		update_dpi_metrics();

		if (_gdi_ctx)
		{
			_gdi_ctx->set_window_font(m_hWnd, ui::style::font_face::dialog);
		}

		for (const auto& c : _children)
		{
			c.second->dpi_changed();
		}

		for (const auto& f : _child_frames)
		{
			auto ff = f.lock();

			if (ff)
			{
				ff->dpi_changed();
			}
		}

		const auto host = _host.lock();

		if (host)
		{
			host->dpi_changed();
		}
	}

	LRESULT on_window_dpi_changed(uint32_t /*uMsg*/, const WPARAM wParam, const LPARAM lParam)
	{
		_gdi_ctx->update_scale_factor(HIWORD(wParam) / static_cast<double>(USER_DEFAULT_SCREEN_DPI));
		update_font_sizes();

		const auto host = _host.lock();

		if (host)
		{
			host->dpi_changed();
		}

		const auto new_bounds = reinterpret_cast<RECT*>(lParam);

		if (new_bounds)
		{
			SetWindowPos(m_hWnd,
			             nullptr,
			             new_bounds->left,
			             new_bounds->top,
			             new_bounds->right - new_bounds->left,
			             new_bounds->bottom - new_bounds->top,
			             SWP_NOZORDER | SWP_NOACTIVATE);
		}

		return 0;
	}

	LRESULT on_system_settings_change(uint32_t /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/) const
	{
		const auto app = _app.lock();

		if (app)
		{
			number_format_invalid = true;
			app->system_event(ui::os_event_type::options_changed);
		}
		return 0;
	}

	void layout() override
	{
		if (_draw_ctx && IsWindow(m_hWnd))
		{
			const auto h = _host.lock();

			if (h)
			{
				h->on_window_layout(*_draw_ctx, _extent, IsIconic(m_hWnd));
			}
		}
	}

	// Rebuilds this window's draw context and every child frame's. Used after the Direct3D
	// device is lost so the whole window tree switches to the CPU software backend.
	void reset_graphics() override
	{
		recreate_draw_context();

		for (const auto& f : _child_frames)
		{
			const auto ff = f.lock();

			if (ff)
			{
				ff->reset_graphics();
			}
		}

		const auto h = _host.lock();

		if (h && _draw_ctx && !_extent.is_empty())
		{
			h->on_window_layout(*_draw_ctx, _extent, IsIconic(m_hWnd));
		}

		InvalidateRect(m_hWnd, nullptr, FALSE);
	}

	ui::edit_ptr create_edit(const ui::edit_styles& styles, std::string_view text,
	                         std::function<void(const std::string&)> changed) override;
	ui::trackbar_ptr create_slider(int min, int max, std::function<void(int, bool)> changed) override;
	ui::button_ptr create_button(std::string_view text, std::function<void()> invoke,
	                             bool default_button = false) override;
	ui::button_ptr create_button(icon_index icon, std::string_view title, std::string_view details,
	                             std::function<void()> invoke, bool default_button = false) override;
	ui::button_ptr create_check_button(bool val, std::string_view text, bool is_radio,
	                                   std::function<void(bool)> changed,
	                                   int radio_group = ui::radio_group_default) override;
	ui::date_time_control_ptr create_date_time_control(df::date_t text, std::function<void(df::date_t)> changed,
	                                                   bool include_time) override;
	ui::toolbar_ptr
	create_toolbar(const ui::toolbar_styles& styles, const std::vector<ui::command_ptr>& buttons) override;
	ui::control_frame_ptr create_dlg(ui::frame_host_weak_ptr host, bool is_popup) override;
	ui::bubble_window_ptr create_bubble() override;

	HHOOK _hMenuHook = nullptr;
	static this_class* _current;
	win_rect m_rcButton;


	LRESULT on_menu_nc_calc_size(HWND hwnd, uint32_t /*uMsg*/, const WPARAM wParam, const LPARAM lParam) const
	{
		auto calc_valid_rects = static_cast<BOOL>(wParam);
		const auto pr = std::bit_cast<LPRECT>(lParam);

		if (pr)
		{
			// Menu subclass messages can arrive while the frame is tearing down and _draw_ctx is gone.
			const auto padding = menu_size_border * (_draw_ctx ? _draw_ctx->scale_factor : 1.0);
			pr->left += padding.cx;
			pr->right -= padding.cx;
			pr->top += padding.cy;
			pr->bottom -= padding.cy;
		}

		return 0;
	}

	LRESULT on_menu_nc_paint(const HWND hwnd, uint32_t /*uMsg*/, const WPARAM wParam, LPARAM /*lParam*/)
	{
		auto dc = GetDCEx(hwnd, std::bit_cast<HRGN>(wParam), DCX_WINDOW | DCX_INTERSECTRGN | 0x10000);
		if (!dc) dc = GetWindowDC(hwnd);

		if (dc)
		{
			win_rect rcWin;
			GetWindowRect(hwnd, &rcWin);
			OffsetRect(&rcWin, -rcWin.left, -rcWin.top);
			draw_menu(dc, rcWin);
			ReleaseDC(hwnd, dc);
		}

		return 1;
	}

	void draw_menu(const HDC dc, const win_rect& rcWin) const
	{
		const auto padding = menu_size_border * (_draw_ctx ? _draw_ctx->scale_factor : 1.0);

		fill_solid_rect(dc, rcWin, ui::lighten(ui::style::color::menu_background, 0.22f));
		fill_solid_rect(dc, rcWin.inflate(-padding.cx, -padding.cy),
		                ui::style::color::menu_background);
	}

	LRESULT on_menu_print(const HWND hwnd, uint32_t /*uMsg*/, const WPARAM wParam, const LPARAM lParam)
	{
		const HDC dc = std::bit_cast<HDC>(wParam);
		win_rect rcWin;
		GetWindowRect(hwnd, rcWin);
		OffsetRect(rcWin, -rcWin.left, -rcWin.top);

		// Do the same as in on_window_nc_paint, but draw to provided DC.
		// Should there be a common method?
		if ((lParam & PRF_NONCLIENT) != 0)
		{
			draw_menu(dc, rcWin);
		}

		// Get the system to draw all the items to a memory DC and then whack it
		// on top of the background we just drew above
		if ((lParam & PRF_CLIENT) != 0)
		{
			const auto client_rect = get_client_rect(hwnd);
			const auto cx_client = client_rect.width();
			const auto cy_client = client_rect.height();
			const auto offset_x = (rcWin.right - rcWin.left - cx_client) / 2;
			const auto offset_y = (rcWin.bottom - rcWin.top - cy_client) / 2;

			auto* mem_dc = CreateCompatibleDC(dc);
			if (mem_dc)
			{
				auto* mem_bm = CreateCompatibleBitmap(dc, cx_client, cy_client);

				if (mem_bm)
				{
					auto* const old_bm = SelectObject(mem_dc, mem_bm);
					DefSubclassProc(hwnd, WM_PRINTCLIENT, std::bit_cast<WPARAM>(mem_dc), PRF_CLIENT);
					BitBlt(dc, offset_x, offset_y, cx_client, cy_client, mem_dc, 0, 0, SRCCOPY);
					SelectObject(mem_dc, old_bm);
					DeleteObject(mem_bm);
				}

				DeleteDC(mem_dc);
			}
		}

		return 0;
	}

	static LRESULT CALLBACK MenuSuperProc(const HWND hWnd, const UINT uMsg, const WPARAM wParam,
	                                      const LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR /*dwRefData*/)
	{
		// USER32 caches menu windows per thread, so this subclass can outlive the host that
		// installed it and a host captured in the reference data would be a dangling pointer.
		// The host running the current menu session is _current, which is null outside one.
		auto* const pt = _current;

		if (pt)
		{
			if (uMsg == WM_NCPAINT) return pt->on_menu_nc_paint(hWnd, uMsg, wParam, lParam);
			if (uMsg == WM_PRINT) return pt->on_menu_print(hWnd, uMsg, wParam, lParam);
			if (uMsg == WM_NCCALCSIZE) return pt->on_menu_nc_calc_size(hWnd, uMsg, wParam, lParam);
		}

		return DefSubclassProc(hWnd, uMsg, wParam, lParam);
	}

	static LRESULT CALLBACK menu_create_hook_proc(const int nCode, const WPARAM wParam, const LPARAM lParam)
	{
		constexpr LRESULT lRet = 0;
		wchar_t szClassName[7] = {0};

		const auto current = _current;

		if (nCode == HCBT_CREATEWND)
		{
			const auto hWndMenu = std::bit_cast<HWND>(wParam);
			::GetClassName(hWndMenu, szClassName, 7);

			if (::lstrcmp(L"#32768", szClassName) == 0)
			{
				// Subclass to a flat-looking menu
				// No reference data: the subclass resolves the active host through _current, so
				// nothing here can outlive the host it was installed for.
				SetWindowSubclass(hWndMenu, MenuSuperProc, 0, 0);
				SetWindowPos(hWndMenu, HWND_TOP, 0, 0, 0, 0,
				             SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED |
				             SWP_DRAWFRAME);
			}
		}
		else if (nCode == HCBT_DESTROYWND)
		{
			const auto hWndMenu = std::bit_cast<HWND>(wParam);
			::GetClassName(hWndMenu, szClassName, 7);

			if (::lstrcmp(L"#32768", szClassName) == 0)
			{
				RemoveWindowSubclass(hWndMenu, MenuSuperProc, 0);
			}
		}
		else if (nCode < 0)
		{
			// Per the CBT hook contract, when nCode < 0 the hook must pass the
			// message to CallNextHookEx and return its result without processing.
			return CallNextHookEx(current ? current->_hMenuHook : nullptr, nCode, wParam, lParam);
		}
		return CallNextHookEx(current ? current->_hMenuHook : nullptr, nCode, wParam, lParam);
	}

	int show_menu(const HMENU hMenu, const uint32_t menu_style, const int x, const int y, const LPCRECT pr = nullptr)
	{
		df::scope_locked_inc sl2(df::command_active);

		const auto prev_current = _current;
		const auto prev_hook = _hMenuHook;
		_current = this;
		_hMenuHook = ::SetWindowsHookEx(WH_CBT, menu_create_hook_proc, get_resource_instance, GetCurrentThreadId());

		const auto result = TrackPopupMenu(hMenu, menu_style, x, y, 0, m_hWnd, pr);

		// UnhookWindowsHookEx(nullptr) is an error; the hook can legitimately fail to install.
		if (_hMenuHook) UnhookWindowsHookEx(_hMenuHook);
		// The handle is restored, not cleared: a nested menu on this same host would otherwise
		// drop the outer session's hook handle and leak it.
		_hMenuHook = prev_hook;
		_current = prev_current;

		return result;
	}

	void show_menu(const recti button_bounds, const std::vector<ui::command_ptr>& commands)
	{
		const auto a = _app.lock();

		if (a)
		{
			a->track_menu(shared_from_this(), button_bounds, commands);
		}
	}

	void track_menu(const recti button_bounds, const std::vector<ui::command_ptr>& commands) override
	{
		df::scope_locked_inc sl2(df::command_active);

		auto cmd_id = 50000;
		win32_menu menu;

		if (menu.CreatePopupMenu())
		{
			// Submenus nest to a bounded depth; the depth cap stops a command whose menu reaches
			// itself from recursing forever.
			constexpr auto max_menu_depth = 4;

			const std::function<void(const win32_menu&, const std::vector<ui::command_ptr>&, int)> build_items =
				[&](const win32_menu& parent, const std::vector<ui::command_ptr>& items, const int depth)
			{
				for (const auto& c : items)
				{
					if (!c)
					{
						parent.AppendMenu(MF_OWNERDRAW | MF_SEPARATOR);
						continue;
					}

					auto text = str::utf8_to_utf16(c->text);
					const auto id = ++cmd_id;
					_menu_commands[id] = c;

					win32_menu sub_menu;

					if (c->menu && depth < max_menu_depth && sub_menu.CreatePopupMenu())
					{
						build_items(sub_menu, c->menu(), depth + 1);

						MENUITEMINFO mii = {};
						mii.cbSize = sizeof(MENUITEMINFO);
						mii.fMask = MIIM_ID | MIIM_SUBMENU | MIIM_TYPE | MIIM_DATA | MIIM_STATE;
						mii.fType = MFT_OWNERDRAW;
						mii.fState = c->enable ? MFS_ENABLED : (MFS_DISABLED | MFS_GRAYED);
						mii.dwTypeData = const_cast<LPWSTR>(text.c_str());
						mii.cch = 0;
						mii.hSubMenu = sub_menu.Detach();
						mii.dwItemData = id;
						mii.wID = id;
						parent.InsertMenuItem(parent.GetMenuItemCount(), TRUE, &mii);
					}
					else
					{
						auto style = MF_OWNERDRAW;
						if (!c->enable) style |= MF_DISABLED;
						if (c->checked) style |= MF_CHECKED;

						parent.AppendMenu(style, id, text.c_str());
					}
				}
			};

			build_items(menu, commands, 0);

			const auto win_bounds_center = window_bounds().center();
			auto style = TPM_RETURNCMD | TPM_RIGHTBUTTON;
			int x = button_bounds.left;
			int y = button_bounds.top;

			if (button_bounds.width() > 1)
			{
				if (win_bounds_center.x < button_bounds.left)
				{
					style |= TPM_RIGHTALIGN;
					x = button_bounds.right;
				}
				else
				{
				}

				if (win_bounds_center.y > button_bounds.top)
				{
					style |= TPM_TOPALIGN;
					y = button_bounds.bottom;
				}
				else
				{
					style |= TPM_BOTTOMALIGN;
				}
			}

			const auto rc = win_rect(button_bounds);
			const auto result = show_menu(menu, style, x, y, rc);
			const auto found = _menu_commands.find(result);

			if (found != _menu_commands.end())
			{
				const auto c = found->second;

				if (c && c->invoke)
				{
					df::scope_locked_inc sl2(df::command_active);
					c->invoke();
				}
			}
		}

		_menu_commands.clear();
	}

	ui::frame_ptr create_frame(ui::frame_host_weak_ptr host, const ui::frame_style& style) override;

	void save_window_position(platform::setting_file_ptr& store) override
	{
		WINDOWPLACEMENT wp;
		wp.length = sizeof(wp);
		if (GetWindowPlacement(m_hWnd, &wp))
		{
			store->write({}, s_window_rect, {(const uint8_t*)&wp, wp.length});
		}
	}
};


LRESULT control_host_impl::on_system_device_change(uint32_t /*uMsg*/, const WPARAM wParam, LPARAM /*lParam*/) const
{
	if (wParam == DBT_DEVNODES_CHANGED)
	{
		const auto app = _app.lock();

		if (app)
		{
			app->system_event(ui::os_event_type::system_device_change);
		}
	}

	return 1;
}

LRESULT control_host_impl::on_window_erase_background(uint32_t /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/)
{
	return 1;
}


LRESULT edit_impl::on_window_context_menu(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam)
{
	const POINT loc{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
	POINT client_loc(loc);
	ScreenToClient(m_hWnd, &client_loc);

	// Find out if we're over any errors
	unknown_word selected_word;
	auto found_error = false;
	const auto line_height = gdi_text_line_height(m_hWnd, GetFont(m_hWnd));

	for (const auto& unk : _unknown_words)
	{
		if (unk.calc_bounds(*this, line_height).contains({client_loc.x, client_loc.y}))
		{
			selected_word = unk;
			found_error = true;
			break;
		}
	}

	constexpr int ID_SPELLCHECK_ADD = 1000;
	constexpr int ID_SPELLCHECK_OPT0 = 2000;

	if (found_error)
	{
		win32_menu popup;

		if (popup.CreatePopupMenu())
		{
			// Append the suggestions, if there are any
			auto suggestions = spell.suggest(selected_word.word);
			if (suggestions.size() > 8) suggestions.resize(8);

			uint32_t item_id = ID_SPELLCHECK_OPT0;
			df::hash_map<unsigned, std::string> opt_map;

			for (const auto& sug : suggestions)
			{
				auto w = str::utf8_to_utf16(sug);
				popup.AppendMenu(MF_ENABLED, item_id, w.c_str());
				opt_map[item_id] = sug;
				++item_id;
			}

			if (!suggestions.empty())
			{
				popup.AppendMenu(MF_SEPARATOR);
			}

			popup.AppendMenu(MF_ENABLED, ID_SPELLCHECK_ADD,
			                 str::utf8_to_utf16(str_format(tt.menu_add_fmt.sv(), selected_word.word)).c_str());
			popup.AppendMenu(MF_SEPARATOR);

			// Now the editing commands (cut, copy, paste, undo, etc)
			if (can_undo())
			{
				popup.AppendMenu(MF_ENABLED, EM_UNDO, str::utf8_to_utf16(tt.menu_undo).c_str());
				popup.AppendMenu(MF_SEPARATOR);
			}

			popup.AppendMenu(MF_ENABLED, WM_CUT, str::utf8_to_utf16(tt.menu_cut).c_str());
			popup.AppendMenu(MF_ENABLED, WM_COPY, str::utf8_to_utf16(tt.menu_copy).c_str());
			popup.AppendMenu(MF_ENABLED, WM_PASTE, str::utf8_to_utf16(tt.menu_paste).c_str());
			popup.AppendMenu(MF_ENABLED, WM_CLEAR, str::utf8_to_utf16(tt.menu_delete).c_str());
			popup.AppendMenu(MF_SEPARATOR);
			popup.AppendMenu(MF_ENABLED, EM_SETSEL, str::utf8_to_utf16(tt.menu_select_all).c_str());

			const auto cmd_id = _parent->show_menu(popup, TPM_RIGHTBUTTON | TPM_NONOTIFY | TPM_RETURNCMD, loc.x, loc.y);

			switch (cmd_id)
			{
			case EM_UNDO:
			case WM_CUT:
			case WM_COPY:
			case WM_PASTE:
			case WM_CLEAR:
			case EM_SETSEL:
				SendMessage(m_hWnd, cmd_id, 0, -1);
				break;

			case ID_SPELLCHECK_ADD:
				spell.add_word(selected_word.word);
				InvalidateRect(m_hWnd, nullptr, TRUE);
				break;

			default:
				if (opt_map.contains(cmd_id))
				{
					select(selected_word.pos_start, selected_word.pos_end);
					replace_sel(opt_map[cmd_id], false);
				}
				break;
			}
		}
	}

	return 0;
}

LRESULT control_host_impl::on_window_command(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam)
{
	const auto id = LOWORD(wParam);
	const auto code = HIWORD(wParam);

	if (!_is_app_frame && id == IDCANCEL)
	{
		close(id == IDCANCEL);
	}
	else
	{
		const auto child = _children.find(id);

		if (child != _children.end())
		{
			if (child->second->is_radio)
			{
				// Windows clears the other auto-radio buttons in the same group; mirror that to the
				// bound values by notifying only the members of the clicked button's radio group.
				const auto group = child->second->radio_group;

				for (const auto& c : _children)
				{
					if (c.second->is_radio && c.second->radio_group == group)
					{
						c.second->on_command(_host, id, code);
					}
				}
			}
			else
			{
				child->second->on_command(_host, id, code);
			}
		}
	}

	return 0;
}

LRESULT control_host_impl::on_window_hscroll(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam)
{
	const auto code = LOWORD(wParam);
	const auto pos = HIWORD(wParam);
	const auto id = GetDlgCtrlID((HWND)lParam);
	const auto child = _children.find(id);

	if (child != _children.end())
	{
		child->second->on_scroll(_host, code, pos);
	}

	return 0;
}

LRESULT control_host_impl::on_window_min_max_info(uint32_t /*uMsg*/, WPARAM /*wParam*/, const LPARAM lParam) const
{
	const LPMINMAXINFO lpMMI = std::bit_cast<LPMINMAXINFO>(lParam);

	if (_is_full_screen)
	{
		const int width = _full_screen_bounds.right - _full_screen_bounds.left;
		const int height = _full_screen_bounds.bottom - _full_screen_bounds.top;

		lpMMI->ptMaxSize.y = height;
		lpMMI->ptMaxTrackSize.y = lpMMI->ptMaxSize.y;
		lpMMI->ptMaxSize.x = width;
		lpMMI->ptMaxTrackSize.x = lpMMI->ptMaxSize.x;
	}

	lpMMI->ptMinTrackSize.x = 640;
	lpMMI->ptMinTrackSize.y = 320;

	return 0;
}


//
// https://stackoverflow.com/questions/39731497/create-window-without-titlebar-with-resizable-border-and-without-bogus-6px-whit
// https://github.com/rossy/borderless-window/blob/master/borderless-window.c
// 
void control_host_impl::update_region()
{
	win_rect rgn;

	if (IsZoomed(m_hWnd))
	{
		WINDOWINFO wi = {sizeof(wi), 0};
		GetWindowInfo(m_hWnd, &wi);

		// For maximized windows, a region is needed to cut off the non-client
		//   borders that hang over the edge of the screen 
		rgn = recti{
			wi.rcClient.left - wi.rcWindow.left,
			wi.rcClient.top - wi.rcWindow.top,
			wi.rcClient.right - wi.rcWindow.left,
			wi.rcClient.bottom - wi.rcWindow.top,
		};
	}
	else if (!_composition_enabled)
	{
		// For ordinary themed windows when composition is disabled, a region
		// is needed to remove the rounded top corners. Make it as large as
		// possible to avoid having to change it when the window is resized. 
		rgn = recti{
			0,
			0,
			32767,
			32767,
		};
	}
	else
	{
		// Don't mess with the region when composition is enabled and the
		// window is not maximized, otherwise it will lose its shadow 
	}

	// Avoid unnecessarily updating the region to avoid unnecessary redraws 
	if (rgn != _window_rgn)
	{
		_window_rgn = rgn;
		// Treat empty regions as NULL regions 
		auto* const window_rgn = rgn.is_empty() ? nullptr : CreateRectRgnIndirect(&_window_rgn);

		// SetWindowRgn only takes ownership when it succeeds.
		if (!SetWindowRgn(m_hWnd, window_rgn, TRUE) && window_rgn)
		{
			DeleteObject(window_rgn);
		}
	}
}


void control_host_impl::handle_composition_changed()
{
	BOOL enabled = FALSE;
	DwmIsCompositionEnabled(&enabled);
	_composition_enabled = enabled != 0;

	if (enabled)
	{
		// The window needs a frame to show a shadow, so give it the smallest
		// amount of frame possible 
		const MARGINS empty_margins = {0, 0, _is_full_screen ? 0 : 1, 0};
		DwmExtendFrameIntoClientArea(m_hWnd, &empty_margins);
		constexpr auto at = DWMNCRP_ENABLED;
		DwmSetWindowAttribute(m_hWnd, DWMWA_NCRENDERING_POLICY, &at, sizeof(DWORD));
	}

	df::log(__FUNCTION__, std::format("composition_enabled {}", _composition_enabled));

	update_region();
}

LRESULT control_host_impl::on_dwm_composition_changed(uint32_t, WPARAM wParam, LPARAM lParam)
{
	df::log(__FUNCTION__, "WM_DWMCOMPOSITIONCHANGED");
	handle_composition_changed();
	return 0;
}

LRESULT control_host_impl::on_window_nc_hit_test(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam) const
{
	if (_is_app_frame)
	{
		POINT loc = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
		ScreenToClient(m_hWnd, &loc);

		RECT rc;
		GetClientRect(m_hWnd, &rc);

		const auto scale_factor = _draw_ctx ? _draw_ctx->scale_factor : 1.0;
		const auto resize_handle = _draw_ctx ? _draw_ctx->handle_cxy : df::round(ui_cx_resize_handle * scale_factor);
		const auto border_thickness = df::round(ui_nonclient_border_thickness * scale_factor);

		enum { left = 1, top = 2, right = 4, bottom = 8 };
		int hit = 0;
		if (loc.x < rc.left + border_thickness) hit |= left;
		if (loc.x > rc.right - border_thickness) hit |= right;
		if (loc.y < rc.top + border_thickness) hit |= top;
		if (loc.y > rc.bottom - resize_handle) hit |= bottom;

		if (hit & top && hit & left) return HTTOPLEFT;
		if (hit & top && hit & right) return HTTOPRIGHT;
		if (hit & bottom && loc.x < rc.left + resize_handle) return HTBOTTOMLEFT;
		if (hit & bottom && loc.x > rc.right - resize_handle) return HTBOTTOMRIGHT;
		if (hit & left) return HTLEFT;
		if (hit & top) return HTTOP;
		if (hit & right) return HTRIGHT;
		if (hit & bottom) return HTBOTTOM;

		const auto h = _host.lock();
		return h && h->is_caption_area({loc.x, loc.y}) ? HTCAPTION : HTCLIENT;
	}

	auto ht = DefWindowProc(m_hWnd, uMsg, wParam, lParam);

	if (ht == HTCLIENT)
	{
		const auto h = _host.lock();
		if (h)
		{
			POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
			ScreenToClient(m_hWnd, &point);
			if (h->is_caption_area({point.x, point.y})) return _is_app_frame ? HTCAPTION : HTTRANSPARENT;
		}
	}

	if (_is_popup && ht == HTCLIENT)
	{
		const auto h = _host.lock();

		if (h)
		{
			POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
			ScreenToClient(m_hWnd, &point);

			if (!h->is_control_area({point.x, point.y}))
			{
				ht = HTCAPTION;
			}
		}
	}

	return ht;
}

LRESULT control_host_impl::on_window_nc_activate(uint32_t, const WPARAM wParam, LPARAM) const
{
	// DefWindowProc won't repaint the window border if lParam (normally a HRGN) is -1.
	// This is recommended in: https://blogs.msdn.microsoft.com/wpfsdk/2008/09/08/custom-window-chrome-in-wpf/ 
	return DefWindowProcW(m_hWnd, WM_NCACTIVATE, wParam, -1);
}


#ifndef DPI_ENUMS_DECLARED
using PROCESS_DPI_AWARENESS = enum
{
	PROCESS_DPI_UNAWARE = 0,
	PROCESS_SYSTEM_DPI_AWARE = 1,
	PROCESS_PER_MONITOR_DPI_AWARE = 2
};

using MONITOR_DPI_TYPE = enum
{
	MDT_EFFECTIVE_DPI = 0,
	MDT_ANGULAR_DPI = 1,
	MDT_RAW_DPI = 2,
	MDT_DEFAULT = MDT_EFFECTIVE_DPI
};
#endif /*DPI_ENUMS_DECLARED*/

using funcGetProcessDpiAwareness = HRESULT(WINAPI*)(HANDLE handle, PROCESS_DPI_AWARENESS* awareness);
using funcGetDpiForMonitor = HRESULT(WINAPI*)(HMONITOR hmonitor, MONITOR_DPI_TYPE dpiType, UINT* dpiX, UINT* dpiY);


// The thickness of an auto-hide taskbar in pixels.
constexpr int kAutoHideTaskbarThicknessPx = 2;

constexpr float kDefaultDPI = 96.f;

static bool IsProcessPerMonitorDpiAware()
{
	enum class PerMonitorDpiAware
	{
		UNKNOWN = 0,
		PER_MONITOR_DPI_UNAWARE,
		PER_MONITOR_DPI_AWARE,
	};
	static auto per_monitor_dpi_aware = PerMonitorDpiAware::UNKNOWN;
	if (per_monitor_dpi_aware == PerMonitorDpiAware::UNKNOWN)
	{
		per_monitor_dpi_aware = PerMonitorDpiAware::PER_MONITOR_DPI_UNAWARE;

		static auto dll = ::LoadLibrary(L"shcore.dll");

		if (dll)
		{
			const auto get_process_dpi_awareness_func =
				reinterpret_cast<funcGetProcessDpiAwareness>(
					GetProcAddress(dll, "GetProcessDpiAwareness"));
			if (get_process_dpi_awareness_func)
			{
				PROCESS_DPI_AWARENESS awareness;
				if (SUCCEEDED(get_process_dpi_awareness_func(nullptr, &awareness)) &&
					awareness == PROCESS_PER_MONITOR_DPI_AWARE)
					per_monitor_dpi_aware = PerMonitorDpiAware::PER_MONITOR_DPI_AWARE;
			}
		}
	}
	return per_monitor_dpi_aware == PerMonitorDpiAware::PER_MONITOR_DPI_AWARE;
}

float GetScalingFactorFromDPI(const int dpi)
{
	return static_cast<float>(dpi) / kDefaultDPI;
}

int GetDefaultSystemDPI()
{
	static int dpi_x = 0;
	static int dpi_y = 0;
	static bool should_initialize = true;

	if (should_initialize)
	{
		should_initialize = false;
		const auto screen_dc = GetDC(nullptr);

		if (screen_dc)
		{
			// This value is safe to cache for the life time of the app since the
			// user must logout to change the DPI setting. This value also applies
			// to all screens.
			dpi_x = GetDeviceCaps(screen_dc, LOGPIXELSX);
			dpi_y = GetDeviceCaps(screen_dc, LOGPIXELSY);
			ReleaseDC(nullptr, screen_dc);
		}
	}
	return dpi_x;
}

// Gets the DPI for a particular monitor.
int GetPerMonitorDPI(const HMONITOR monitor)
{
	if (IsProcessPerMonitorDpiAware())
	{
		static const auto dll = ::LoadLibrary(L"shcore.dll");

		if (dll)
		{
			static const auto get_dpi_for_monitor_func = reinterpret_cast<funcGetDpiForMonitor>(GetProcAddress(
				dll, "GetDpiForMonitor"));

			if (get_dpi_for_monitor_func)
			{
				UINT dpi_x, dpi_y;

				if (SUCCEEDED(get_dpi_for_monitor_func(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y)))
				{
					return static_cast<int>(dpi_x);
				}
			}
		}
	}

	return GetDefaultSystemDPI();
}


bool MonitorHasAutohideTaskbarForEdge(const UINT edge, const HMONITOR monitor)
{
	APPBARDATA taskbar_data_for_getautohidebar = {sizeof(APPBARDATA), nullptr, 0, edge};
	taskbar_data_for_getautohidebar.hWnd = GetForegroundWindow();

	// MSDN documents an ABM_GETAUTOHIDEBAREX, which supposedly takes a monitor
	// rect and returns autohide bars on that monitor.  This sounds like a good
	// idea for multi-monitor systems.  Unfortunately, it appears to not work at
	// least some of the time (erroneously returning NULL) and there's almost no
	// online documentation or other sample code using it that suggests ways to
	// address this problem. We do the following:-
	// 1. Use the ABM_GETAUTOHIDEBAR message. If it works, i.e. returns a valid
	//    window we are done.
	// 2. If the ABM_GETAUTOHIDEBAR message does not work we query the auto hide
	//    state of the taskbar and then retrieve its position. That call returns
	//    the edge on which the taskbar is present. If it matches the edge we
	//    are looking for, we are done.
	// NOTE: This call spins a nested run loop.
	auto taskbar = reinterpret_cast<HWND>(
		SHAppBarMessage(ABM_GETAUTOHIDEBAR, &taskbar_data_for_getautohidebar));
	if (!IsWindow(taskbar))
	{
		APPBARDATA taskbar_data = {sizeof(APPBARDATA), nullptr, 0, 0};
		const auto taskbar_state = SHAppBarMessage(ABM_GETSTATE, &taskbar_data);
		if (!(taskbar_state & ABS_AUTOHIDE))
			return false;

		taskbar_data.hWnd = ::FindWindow(L"Shell_TrayWnd", nullptr);
		if (!IsWindow(taskbar_data.hWnd))
			return false;

		SHAppBarMessage(ABM_GETTASKBARPOS, &taskbar_data);
		if (taskbar_data.uEdge == edge)
			taskbar = taskbar_data.hWnd;
	}

	// There is a potential race condition here:
	// 1. A maximized chrome window is fullscreened.
	// 2. It is switched back to maximized.
	// 3. In the process the window gets a WM_NCCACLSIZE message which calls us to
	//    get the autohide state.
	// 4. The worker thread is invoked. It calls the API to get the autohide
	//    state. On Windows versions  earlier than Windows 7, taskbars could
	//    easily be always on top or not.
	//    This meant that we only want to look for taskbars which have the topmost
	//    bit set.  However this causes problems in cases where the window on the
	//    main thread is still in the process of switching away from fullscreen.
	//    In this case the taskbar might not yet have the topmost bit set.
	// 5. The main thread resumes and does not leave space for the taskbar and
	//    hence it does not pop when hovered.
	//
	// To address point 4 above, it is best to not check for the WS_EX_TOPMOST
	// window style on the taskbar, as starting from Windows 7, the topmost
	// style is always set. We don't support XP and Vista anymore.
	if (IsWindow(taskbar))
	{
		if (MonitorFromWindow(taskbar, MONITOR_DEFAULTTONEAREST) == monitor)
			return true;
		// In some cases like when the autohide taskbar is on the left of the
		// secondary monitor, the MonitorFromWindow call above fails to return the
		// correct monitor the taskbar is on. We fallback to MonitorFromPoint for
		// the cursor position in that case, which seems to work well.
		POINT cursor_pos = {0};
		GetCursorPos(&cursor_pos);
		if (MonitorFromPoint(cursor_pos, MONITOR_DEFAULTTONEAREST) == monitor)
			return true;
	}
	return false;
}

// SHAppBarMessage spins a nested run loop, and WM_NCCALCSIZE queries all four edges on every step
// of a maximized window resize or restore. The autohide state changes rarely, so a short-lived
// cache keeps that path cheap while still noticing a taskbar change within a fraction of a second.
// UI-thread only (WM_NCCALCSIZE), so plain statics are correct here.
static bool monitor_has_autohide_taskbar_cached(const UINT edge, const HMONITOR monitor)
{
	constexpr uint64_t cache_ms = 250;

	struct cache_entry
	{
		HMONITOR monitor = nullptr;
		uint64_t when = 0;
		bool value = false;
	};

	static cache_entry entries[4];
	static_assert(ABE_LEFT == 0 && ABE_TOP == 1 && ABE_RIGHT == 2 && ABE_BOTTOM == 3);

	if (edge > ABE_BOTTOM)
	{
		return MonitorHasAutohideTaskbarForEdge(edge, monitor);
	}

	auto& entry = entries[edge];
	const auto now = GetTickCount64();

	if (entry.when != 0 && entry.monitor == monitor && (now - entry.when) < cache_ms)
	{
		return entry.value;
	}

	entry.monitor = monitor;
	entry.value = MonitorHasAutohideTaskbarForEdge(edge, monitor);
	entry.when = GetTickCount64();
	return entry.value;
}

LRESULT control_host_impl::on_window_nc_calc_size(uint32_t, const WPARAM wParam, const LPARAM lParam)
{
	// Some of this code came from the chromium code base

	const auto mode = wParam != 0;
	const auto l_param = lParam;
	// We only override the default handling if we need to specify a custom
	// non-client edge width. Note that in most cases "no insets" means no
	// custom width, but in fullscreen mode or when the NonClientFrameView
	// requests it, we want a custom width of 0.

	// Let User32 handle the first nccalcsize for captioned windows
	// so it updates its internal structures (specifically caption-present)
	// Without this Tile & Cascade windows won't work.
	// See http://code.google.com/p/chromium/issues/detail?id=900
	if (_is_first_nccalc_)
	{
		_is_first_nccalc_ = false;
		return DefWindowProc(m_hWnd, WM_NCCALCSIZE, wParam, lParam);
	}

	RECT* client_rect =
		mode
			? &reinterpret_cast<NCCALCSIZE_PARAMS*>(l_param)->rgrc[0]
			: reinterpret_cast<RECT*>(l_param);

	HMONITOR monitor = MonitorFromWindow(hwnd(), MONITOR_DEFAULTTONULL);

	if (!monitor)
	{
		// We might end up here if the window was previously minimized and the
		// user clicks on the taskbar button to restore it in the previous
		// position. In that case WM_NCCALCSIZE is sent before the window
		// coordinates are restored to their previous values, so our (left,top)
		// would probably be (-32000,-32000) like all minimized windows. So the
		// above MonitorFromWindow call fails, but if we check the window rect
		// given with WM_NCCALCSIZE (which is our previous restored window
		// position) we will get the correct monitor handle.
		monitor = MonitorFromRect(client_rect, MONITOR_DEFAULTTONULL);

		if (!monitor)
		{
			// This is probably an extreme case that we won't hit, but if we don't
			// intersect any monitor, let us not adjust the client rect since our
			// window will not be visible anyway.
			return 0;
		}
	}

	if (!mode)
	{
		return DefWindowProc(m_hWnd, WM_NCCALCSIZE, wParam, lParam);
	}

	if (!_is_full_screen)
	{
		const auto is_zoomed = IsZoomed(m_hWnd) != 0;

		if (is_zoomed)
		{
			// maximised window should fill work area
			MONITORINFO monitorInfo;
			monitorInfo.cbSize = sizeof(MONITORINFO);

			if (GetMonitorInfo(monitor, &monitorInfo))
			{
				*client_rect = monitorInfo.rcWork;
			}

			// Find all auto-hide taskbars along the screen edges and adjust in by the
			// thickness of the auto-hide taskbar on each such edge, so the window isn't
			// treated as a "fullscreen app", which would cause the taskbars to
			// disappear.
			if (monitor_has_autohide_taskbar_cached(ABE_LEFT, monitor))
				client_rect->left += kAutoHideTaskbarThicknessPx;

			if (monitor_has_autohide_taskbar_cached(ABE_TOP, monitor))
			{
				//if (IsFrameSystemDrawn)
				//{
				//	// Tricky bit.  Due to a bug in DwmDefWindowProc()'s handling of
				//	// WM_NCHITTEST, having any nonclient area atop the window causes the
				//	// caption buttons to draw onscreen but not respond to mouse
				//	// hover/clicks.
				//	// So for a taskbar at the screen top, we can't push the
				//	// client_rect->top down; instead, we move the bottom up by one pixel,
				//	// which is the smallest change we can make and still get a client area
				//	// less than the screen size. This is visibly ugly, but there seems to
				//	// be no better solution.
				//	--client_rect->bottom;
				//}
				//else {
				client_rect->top += kAutoHideTaskbarThicknessPx;
			}
			if (monitor_has_autohide_taskbar_cached(ABE_RIGHT, monitor))
				client_rect->right -= kAutoHideTaskbarThicknessPx;
			if (monitor_has_autohide_taskbar_cached(ABE_BOTTOM, monitor))
				client_rect->bottom -= kAutoHideTaskbarThicknessPx;
		}
	}

	// Returning 0 lets the window manager blit the old client pixels into the new rect top-left
	// aligned. Nothing in this frame is top-left anchored, so that stale copy is the visible
	// double-step; WVR_REDRAW invalidates instead and handle_resize repaints in the same message.
	const RECT& old_client = reinterpret_cast<const NCCALCSIZE_PARAMS*>(l_param)->rgrc[2];
	const auto size_changed =
		(client_rect->right - client_rect->left) != (old_client.right - old_client.left) ||
		(client_rect->bottom - client_rect->top) != (old_client.bottom - old_client.top);

	return size_changed ? WVR_REDRAW : 0;
}

LRESULT control_host_impl::on_window_nc_paint(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam) const
{
	if (!_composition_enabled)
		return 0;

	return DefWindowProc(m_hWnd, uMsg, wParam, lParam);
}


LRESULT control_host_impl::on_window_create(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam)
{
	if (_is_app_frame)
	{
		handle_composition_changed();
	}
	_timer_id = SetTimer(m_hWnd, 0, 1000 / ui::default_ticks_per_second, nullptr);
	return DefWindowProc(m_hWnd, uMsg, wParam, lParam);
}

class toolbar_impl final :
	public control_base_impl<toolbar_impl, ui::toolbar, win_base>,
	public control_base2,
	public std::enable_shared_from_this<toolbar_impl>
{
public:
	toolbar_impl(control_host_impl* parent, const owner_context_ptr& ctx) : _parent(parent), _ctx(ctx)
	{
	}

	// Only modal dialogs call destroy(); the view control panels are rebuilt on every view switch
	// and simply drop their controls, so without this the image list built for each toolbar is
	// orphaned and the icon bitmaps accumulate for the whole session.
	~toolbar_impl() override
	{
		release_image_list();
	}

	void release_image_list() const
	{
		if (m_hWnd && IsWindow(m_hWnd))
		{
			const auto image_list = SetImageList(nullptr);
			if (image_list) ImageList_Destroy(image_list);
		}
	}

	df::hash_map<uintptr_t, std::shared_ptr<ui::command>> _commands;
	ui::toolbar_styles _styles;
	control_host_impl* _parent;
	owner_context_ptr _ctx;

	HIMAGELIST create_image_list() const
	{
		const auto icon_cxy = calc_icon_cxy(_ctx->scale_factor);
		return ImageList_Create(icon_cxy, icon_cxy, ILC_COLOR, 0, 0);
	}

	void update_button_size()
	{
		const auto has_defined_button_extent = !_styles.button_extent.is_empty();

		if (has_defined_button_extent)
		{
			const auto scale_factor = _ctx->scale_factor;
			SetButtonSize(df::round(_styles.button_extent.cx * scale_factor),
			              df::round(_styles.button_extent.cy * scale_factor));
			AutoSize();
		}
		else
		{
			AutoSize();
		}
	}

	void create(const HWND parent, const ui::toolbar_styles& styles, const std::vector<ui::command_ptr>& buttons,
	            const uintptr_t toolbar_id)
	{
		_styles = styles;

		const auto has_defined_button_extent = !styles.button_extent.is_empty();
		auto toolbar_style = WS_CHILD | WS_TABSTOP | CCS_NODIVIDER | CCS_NOPARENTALIGN | TBSTYLE_CUSTOMERASE;
		if (styles.xTBSTYLE_LIST) toolbar_style |= TBSTYLE_LIST;
		if (styles.xTBSTYLE_WRAPABLE) toolbar_style |= TBSTYLE_WRAPABLE;
		else toolbar_style |= CCS_NORESIZE;

		auto id = toolbar_id + 1;
		std::vector<TBBUTTON> toolbar_buttons;

		for (const auto& b : buttons)
		{
			if (b)
			{
				const auto icon = b->icon;
				const auto image = icon == icon_index::none ? I_IMAGENONE : static_cast<int>(icon);
				const auto button_style = BTNS_BUTTON | (has_defined_button_extent ? 0 : BTNS_AUTOSIZE) | (
					b->menu ? BTNS_DROPDOWN : 0) | (b->checkable ? BTNS_CHECK : 0);

				const auto button_state = TBSTATE_ENABLED | (b->checked ? TBSTATE_CHECKED : 0);
				TBBUTTON bb = {
					image, static_cast<int>(id), static_cast<uint8_t>(button_state),
					static_cast<uint8_t>(button_style), 0, 0
				};
				toolbar_buttons.emplace_back(bb);
				_commands[id] = b;
				id += 1;
			}
			else
			{
				TBBUTTON bb = {0, 0, 0, BTNS_SEP, 0, 0};
				toolbar_buttons.emplace_back(bb);
			}
		}

		m_hWnd = CreateWindowEx(
			0,
			TOOLBARCLASSNAME,
			nullptr,
			toolbar_style,
			0,
			0,
			0,
			0,
			parent,
			std::bit_cast<HMENU>(toolbar_id),
			get_resource_instance,
			this);

		if (m_hWnd) buffered_control_paint::attach(m_hWnd);

		if (!m_hWnd)
		{
			// Without a window the calls below message a null handle and the image list built
			// for the toolbar is never handed over, so it would leak with no owner.
			df::log(__FUNCTION__, "Toolbar window creation failed");
			return;
		}

		_ctx->set_window_font(m_hWnd, ui::style::font_face::dialog);
		SetButtonStructSize();
		SetImageList(create_image_list());
		AddButtons(static_cast<int>(toolbar_buttons.size()), toolbar_buttons.data());

		for (auto i = 0u; i < buttons.size(); i++)
		{
			const auto& b = buttons[i];

			if (b && !b->toolbar_text.empty())
			{
				auto w = str::utf8_to_utf16(b->toolbar_text);

				TBBUTTONINFO tbbi = {0};
				tbbi.cbSize = sizeof(TBBUTTONINFO);
				tbbi.dwMask = TBIF_TEXT;
				tbbi.pszText = const_cast<LPWSTR>(w.c_str());
				SetButtonInfo(toolbar_buttons[i].idCommand, &tbbi);
			}
		}

		update_button_size();
	}

	void AutoSize() const
	{
		df::assert_true(IsWindow(m_hWnd));
		::SendMessage(m_hWnd, TB_AUTOSIZE, 0, 0L);
	}

	BOOL SetButtonSize(const int cx, const int cy) const
	{
		df::assert_true(IsWindow(m_hWnd));
		return static_cast<BOOL>(::SendMessage(m_hWnd, TB_SETBUTTONSIZE, 0, MAKELPARAM(cx, cy)));
	}

	BOOL SetButtonInfo(const int nID, LPTBBUTTONINFO lptbbi) const
	{
		df::assert_true(IsWindow(m_hWnd));
		return static_cast<BOOL>(::SendMessage(m_hWnd, TB_SETBUTTONINFO, nID, (LPARAM)lptbbi));
	}

	void SetButtonStructSize(const int nSize = sizeof(TBBUTTON)) const
	{
		df::assert_true(IsWindow(m_hWnd));
		::SendMessage(m_hWnd, TB_BUTTONSTRUCTSIZE, nSize, 0L);
	}

	HIMAGELIST SetImageList(HIMAGELIST hImageList, const int nIndex = 0) const
	{
		df::assert_true(IsWindow(m_hWnd));
		return std::bit_cast<HIMAGELIST>(::SendMessage(m_hWnd, TB_SETIMAGELIST, nIndex, (LPARAM)hImageList));
	}

	BOOL AddButtons(const int nNumButtons, LPTBBUTTON lpButtons) const
	{
		df::assert_true(IsWindow(m_hWnd));
		return static_cast<BOOL>(::SendMessage(m_hWnd, TB_ADDBUTTONS, nNumButtons, (LPARAM)lpButtons));
	}

	void destroy() override
	{
		_commands.clear();

		// The toolbar does not own its image list, so it must be released here or every toolbar
		// teardown leaks the icon bitmaps.
		release_image_list();

		DestroyWindow(m_hWnd);
	}

	sizei measure_toolbar() const
	{
		sizei result(0, 0);
		const int count = static_cast<int>(::SendMessage(m_hWnd, TB_BUTTONCOUNT, 0, 0L));

		for (int i = 0; i < count; ++i)
		{
			win_rect r;
			if (::SendMessage(m_hWnd, TB_GETITEMRECT, i, std::bit_cast<LPARAM>(static_cast<LPRECT>(r))))
			{
				result.cx = std::max(static_cast<int>(r.right), result.cx);
				result.cy = std::max(static_cast<int>(r.bottom), result.cy);
			}
		}

		return result;
	}

	sizei measure_toolbar(const int cx) override
	{
		SIZE result{cx, 0};

		if (_styles.xTBSTYLE_WRAPABLE)
		{
			::SendMessage(m_hWnd, TB_GETIDEALSIZE, TRUE, std::bit_cast<LPARAM>(&result));
		}
		else
		{
			::SendMessage(m_hWnd, TB_GETMAXSIZE, 0, (LPARAM)&result);
		}


		return {result.cx, result.cy};
	}

	int GetButtonCount() const
	{
		df::assert_true(IsWindow(m_hWnd));
		return static_cast<int>(::SendMessage(m_hWnd, TB_BUTTONCOUNT, 0, 0L));
	}

	BOOL GetButton(const int nIndex, LPTBBUTTON lpButton) const
	{
		df::assert_true(IsWindow(m_hWnd));
		return static_cast<BOOL>(::SendMessage(m_hWnd, TB_GETBUTTON, nIndex, (LPARAM)lpButton));
	}

	int GetButtonInfo(const int nID, LPTBBUTTONINFO lptbbi) const
	{
		df::assert_true(IsWindow(m_hWnd));
		return static_cast<int>(::SendMessage(m_hWnd, TB_GETBUTTONINFO, nID, (LPARAM)lptbbi));
	}

	recti button_bounds(const ui::command_ptr& command) const override
	{
		for (const auto& [id, toolbar_command] : _commands)
		{
			if (toolbar_command == command)
			{
				win_rect bounds;
				if (::SendMessage(m_hWnd, TB_GETRECT, id,
				                  std::bit_cast<LPARAM>(static_cast<LPRECT>(bounds))))
				{
					return recti(bounds).offset(window_bounds().top_left());
				}
				break;
			}
		}

		return {};
	}

	void options_changed() override
	{
		_ctx->set_window_font(m_hWnd, ui::style::font_face::dialog);
		update_button_state(true, true);
	}

	void dpi_changed() override
	{
		_ctx->set_window_font(m_hWnd, ui::style::font_face::dialog);
		ImageList_Destroy(SetImageList(create_image_list()));
		update_button_size();
	}

	void update_button_state(const bool resize, const bool text_changed) override
	{
		std::wstring w;
		bool is_autosize = false;
		const auto count = GetButtonCount();

		for (auto i = 0; i < count; ++i)
		{
			TBBUTTON button = {};
			if (!GetButton(i, &button)) continue;

			const auto id = button.idCommand;
			const auto found = _commands.find(id);

			if (found != _commands.end())
			{
				const auto& c = found->second;

				TBBUTTONINFO tbbi = {0};
				tbbi.cbSize = sizeof(TBBUTTONINFO);
				tbbi.dwMask = TBIF_STATE | TBIF_STYLE;
				GetButtonInfo(id, &tbbi);

				if (tbbi.fsStyle & TBSTYLE_AUTOSIZE)
				{
					is_autosize = true;
				}

				tbbi.dwMask = TBIF_STATE;
				tbbi.fsState = tbbi.fsState & ~(TBSTATE_CHECKED | TBSTATE_ENABLED);

				if (c->checked)
				{
					tbbi.fsState |= TBSTATE_CHECKED;
				}

				if (c->enable)
				{
					tbbi.fsState |= TBSTATE_ENABLED;
				}

				if (resize || text_changed)
				{
					if (!c->toolbar_text.empty() && (c->text_can_change || text_changed))
					{
						w = str::utf8_to_utf16(c->toolbar_text);
						tbbi.dwMask |= TBIF_TEXT;
						tbbi.pszText = const_cast<LPWSTR>(w.c_str());
					}
				}

				if (c->icon_can_change || text_changed)
				{
					tbbi.dwMask |= TBIF_IMAGE;
					tbbi.iImage = static_cast<int>(c->icon);
				}

				if (c->visible)
				{
					tbbi.fsState &= ~TBSTATE_HIDDEN;
				}
				else
				{
					tbbi.fsState |= TBSTATE_HIDDEN;
				}

				SetButtonInfo(id, &tbbi);
			}
		}

		if (resize || text_changed)
		{
			if (is_autosize)
			{
				AutoSize();
			}
			else if (!_styles.button_extent.is_empty())
			{
				SetButtonSize(_styles.button_extent.cx, _styles.button_extent.cy);
			}
		}
	}

	void on_command(const ui::frame_host_weak_ptr& host, const int id, const int code) override
	{
		const auto found_toolbar = _commands.find(id);

		if (found_toolbar != _commands.end() && found_toolbar->second->invoke)
		{
			found_toolbar->second->invoke();
		}
	}

	LRESULT on_notify(const ui::frame_host_weak_ptr& host, const ui::color_style& colors, const int id,
	                  const LPNMHDR pnmh) override
	{
		if (pnmh->code == NM_CUSTOMDRAW)
		{
			const auto pCustomDraw = std::bit_cast<LPNMCUSTOMDRAW>(pnmh);
			const auto from = pCustomDraw->hdr.hwndFrom;

			if (pCustomDraw->dwDrawStage == CDDS_PREPAINT)
			{
				return CDRF_NOTIFYITEMDRAW;
			}
			if (pCustomDraw->dwDrawStage == CDDS_PREERASE)
			{
				erase_toolbar_seperators(from, pCustomDraw->hdc, colors.background);
				return CDRF_SKIPDEFAULT;
			}
			if (pCustomDraw->dwDrawStage == CDDS_ITEMPREPAINT)
			{
				const auto tb_cd = std::bit_cast<LPNMTBCUSTOMDRAW>(pCustomDraw);
				const auto found = _commands.find(tb_cd->nmcd.dwItemSpec);

				if (found != _commands.end())
				{
					draw_toolbar_button(found->second, _ctx, tb_cd, colors.background, colors.foreground,
					                    colors.selected);
				}

				return CDRF_SKIPDEFAULT;
			}
		}
		else if (pnmh->code == NM_SETFOCUS)
		{
			const auto h = host.lock();
			if (h) h->focus_changed(true, shared_from_this());
		}
		else if (pnmh->code == NM_KILLFOCUS)
		{
			const auto h = host.lock();
			if (h) h->focus_changed(false, shared_from_this());
		}
		else if (pnmh->code == TBN_DROPDOWN)
		{
			const auto ptb = std::bit_cast<NMTOOLBAR*>(pnmh);
			const auto id = ptb->iItem;
			const auto found = _commands.find(id);

			if (found != _commands.end())
			{
				auto* tb = pnmh->hwndFrom;
				win_rect rc;
				toolbar_GetItemRect(tb, toolbar_CommandToIndex(tb, id), &rc);
				MapWindowPoints(tb, HWND_DESKTOP, (LPPOINT)&rc, 2);
				_parent->show_menu(rc, found->second->menu());
			}
		}
		else if (pnmh->code == TBN_HOTITEMCHANGE)
		{
			const auto ptb = std::bit_cast<NMTBHOTITEM*>(pnmh);
			const auto id = ptb->idNew;
			const auto found = _commands.find(id);
			const auto activate_command = found != _commands.end() && (ptb->dwFlags & HICF_LEAVING) == 0;
			win_rect rc;
			::SendMessage(pnmh->hwndFrom, TB_GETRECT, id, std::bit_cast<LPARAM>(static_cast<LPRECT>(rc)));
			ClientToScreen(pnmh->hwndFrom, std::bit_cast<POINT*>(&rc.left)); // convert top-left
			ClientToScreen(pnmh->hwndFrom, std::bit_cast<POINT*>(&rc.right)); // convert bottom-right		
			const auto h = host.lock();
			if (h) h->command_hover(activate_command ? found->second : nullptr, rc);
			_parent->_hover_command_bounds = rc;
		}

		return 0;
	}
};


bool edit_impl::init_auto_complete_list()
{
	ComPtr<IAutoComplete> ac;
	auto hr = CoCreateInstance(CLSID_AutoComplete, nullptr, CLSCTX_ALL, IID_PPV_ARGS(ac.GetAddressOf()));

	if (FAILED(hr))
	{
		df::assert_true(!"CoCreateInstance/IAutoComplete fail!");
		return false;
	}

	hr = ac->Init(m_hWnd, string_enum.Get(), nullptr, nullptr);

	if (FAILED(hr))
	{
		df::assert_true(!"ac->Init fail!");
		return false;
	}

	ComPtr<IAutoComplete2> ac2;
	hr = ac.As(&ac2);
	if (FAILED(hr))
	{
		df::assert_true(!"ac->QueryInterface fail!");
		return false;
	}

	// NOTE: ACO_AUTOAPPEND is deliberately omitted. When the suggestion list holds full
	// templates (e.g. "{created}-###") and the user types a value that is a prefix of one of
	// them (e.g. "{created}"), auto-append inline-inserts the remainder and selects it. Because
	// IAutoComplete processes input through an async message hook, fast typing intermittently
	// drops/duplicates characters, and the appended-but-unwanted suffix silently changes the
	// committed text. That corrupted text is what the edit's EN_CHANGE handler stores into the
	// bound setting and later persists - which is the "template got truncated" symptom.
	// ACO_AUTOSUGGEST keeps the dropdown suggestions without mutating what the user typed.
	constexpr DWORD opts = ACO_UPDOWNKEYDROPSLIST | ACO_AUTOSUGGEST;
	hr = ac2->SetOptions(opts);

	if (FAILED(hr))
	{
		df::assert_true(!"ac2->SetOptions fail!");
		return false;
	}

	_auto_complete = ac;
	return true;
}

ui::edit_ptr control_host_impl::create_edit(const ui::edit_styles& styles, const std::string_view text,
                                            std::function<void(const std::string&)> changed)
{
	const auto id = alloc_ids();
	auto result = std::make_shared<edit_impl>(styles, this, _gdi_ctx);
	result->Create(m_hWnd, text, id);
	_gdi_ctx->set_window_font(result->m_hWnd, styles.font);
	result->changed = std::move(changed);

	if (styles.file_system_auto_complete)
	{
		SHAutoComplete(result->m_hWnd, SHACF_FILESYS_DIRS | SHACF_FILESYSTEM);
	}
	else if (!styles.auto_complete_list.empty())
	{
		result->string_enum->load(styles.auto_complete_list);
		result->init_auto_complete_list();
	}

	_children[id] = result;
	return result;
}

ui::trackbar_ptr control_host_impl::create_slider(const int min, const int max, std::function<void(int, bool)> changed)
{
	const auto slider_id = alloc_ids();
	auto result = std::make_shared<trackbar_impl>(std::move(changed), _gdi_ctx);
	result->Create(m_hWnd, {}, nullptr, WS_CHILD | WS_TABSTOP | WS_CLIPCHILDREN, 0, slider_id);
	_gdi_ctx->set_window_font(result->m_hWnd, ui::style::font_face::dialog);
	result->set_range(min, max);

	_children[slider_id] = result;

	return result;
}

ui::toolbar_ptr control_host_impl::create_toolbar(const ui::toolbar_styles& styles,
                                                  const std::vector<ui::command_ptr>& buttons)
{
	const auto toolbar_id = alloc_ids(static_cast<int>(buttons.size()) + 1);
	auto result = std::make_shared<toolbar_impl>(this, _gdi_ctx);
	result->create(m_hWnd, styles, buttons, toolbar_id);

	int id = toolbar_id + 1;
	for (const auto& b : buttons)
	{
		if (b)
		{
			_children[id] = result;
			id += 1;
		}
	}

	_children[toolbar_id] = result;

	return result;
}

ui::control_frame_ptr control_host_impl::create_dlg(ui::frame_host_weak_ptr host, const bool is_popup)
{
	ui::color_style colors = {
		ui::style::color::dialog_background, ui::style::color::dialog_text, ui::style::color::dialog_selected_background
	};
	auto ctx = is_popup ? std::make_shared<owner_context>(_gdi_ctx->scale_factor) : _gdi_ctx;
	auto result = std::make_shared<control_host_impl>(ctx, host, _app, _pa, false, colors);
	const auto dw_style = WS_CLIPCHILDREN | WS_CLIPSIBLINGS | (is_popup ? WS_POPUP : WS_CHILD);
	constexpr auto dw_ex_style = WS_EX_CONTROLPARENT;

	if (result->Create(m_hWnd, {}, dw_style, dw_ex_style) == nullptr)
	{
		return nullptr;
	}

	result->_is_popup = is_popup;

	if (!is_popup)
	{
		// A popup host owns its context and gets its own WM_DPICHANGED; a child host has neither,
		// so the parent has to hand it every scale change. Pruned like _child_frames, since a view
		// that rebuilds its control panel would otherwise grow this list for the session.
		std::erase_if(_child_hosts, [](const std::weak_ptr<control_host_impl>& h) { return h.expired(); });
		_child_hosts.push_back(result);
	}

	// The popup host owns its own context, so it must be fonted from that one - fonting it from
	// the parent's leaves it holding a handle the parent deletes on its next font refresh.
	ctx->set_window_font(result->m_hWnd, ui::style::font_face::dialog);
	return result;
}


ui::button_ptr control_host_impl::create_button(const std::string_view text, std::function<void()> invoke,
                                                const bool default_button)
{
	return create_button(icon_index::none, text, {}, std::move(invoke), default_button);
}

ui::button_ptr control_host_impl::create_button(const icon_index icon, const std::string_view title,
                                                const std::string_view details, std::function<void()> invoke,
                                                const bool default_button)
{
	auto style = WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON | BS_TEXT | BS_NOTIFY;
	if (default_button) style |= BS_DEFPUSHBUTTON;

	const auto w = str::utf8_to_utf16(title);
	auto details_w = str::utf8_to_utf16(details);
	auto result = std::make_shared<button_impl>(_gdi_ctx);
	const auto id = alloc_ids();

	result->Create(m_hWnd, w.c_str(), style, 0, id);
	_gdi_ctx->set_window_font(result->m_hWnd, ui::style::font_face::dialog);
	result->_details = std::move(details_w);
	result->_icon = icon;
	result->_invoke = std::move(invoke);

	_children[id] = result;

	if (default_button)
	{
		_def_id = id;
	}

	return result;
}

ui::button_ptr control_host_impl::create_check_button(const bool val, const std::string_view text,
                                                      const bool is_radio,
                                                      std::function<void(bool)> changed,
                                                      const int radio_group)
{
	// WS_GROUP marks the first button of a radio group. Without it Windows treats every sibling
	// auto-radio button as one group, so picking a collision policy would clear the scope choice.
	const auto starts_group = is_radio && (!_last_radio_group.has_value() || *_last_radio_group != radio_group);
	const auto style = WS_CHILD | WS_TABSTOP | BS_NOTIFY | (starts_group ? WS_GROUP : 0) |
		(is_radio ? BS_AUTORADIOBUTTON : BS_AUTOCHECKBOX);
	const auto w = str::utf8_to_utf16(text);
	auto result = std::make_shared<button_impl>(_gdi_ctx);
	const auto id = alloc_ids();

	result->Create(m_hWnd, w.c_str(), style, 0, id);
	_gdi_ctx->set_window_font(result->m_hWnd, ui::style::font_face::dialog);
	result->SetCheck(val ? 1 : 0);
	result->is_radio = is_radio;
	result->radio_group = radio_group;

	if (is_radio)
	{
		_last_radio_group = radio_group;
	}

	_children[id] = result;

	if (changed)
	{
		// Capture weakly: _invoke is a member of result, so an owning capture would make the
		// button keep itself alive and leak every checkbox and radio button in every dialog.
		result->_invoke = [weak_result = std::weak_ptr(result), changed = std::move(changed)]
		{
			const auto button = weak_result.lock();
			if (button) changed((button->GetCheck() & BST_CHECKED) != 0);
		};
	}

	return result;
}

ui::date_time_control_ptr control_host_impl::create_date_time_control(const df::date_t val,
                                                                      std::function<void(df::date_t)> changed,
                                                                      const bool include_time)
{
	auto result = std::make_shared<date_time_control_impl>(_gdi_ctx, val, std::move(changed), _colors, include_time);
	const auto id = alloc_ids();
	result->Create(m_hWnd, id);
	_children[id] = result;
	return result;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static std::weak_ptr<ui::app> g_app;
static std::weak_ptr<win32_app> g_app_impl;

void handle_graphics_device_lost(const factories_ptr& f)
{
	df::assert_true(ui::is_ui_thread());

	if (!f || f->software_mode)
	{
		// Already running on the CPU backend - another frame handled this first.
		return;
	}

	// Ask the app to drop every GPU-backed resource (display textures, player session,
	// cached surfaces) before the device goes away. Textures created on the lost device are
	// staged again from their source data on the next paint.
	const auto app = g_app.lock();

	if (app)
	{
		app->system_event(ui::os_event_type::graphics_device_lost);
	}

	// Release the device itself. From here every new draw context is a software one.
	f->downgrade_to_software();

	// Rebuild the window tree's draw contexts on the software backend.
	const auto app_impl = g_app_impl.lock();

	if (app_impl && app_impl->_frame)
	{
		app_impl->_frame->reset_graphics();
	}
}

void log_open_files_to_crash_files_list();
void flush_open_files_to_crash_files_list();

#ifndef WINSTORE

static bool create_dump(EXCEPTION_POINTERS* exception_pointers, const df::file_path dump_file_path)
{
	using MINIDUMP_WRITE_DUMP = BOOL(WINAPI*)(
		IN HANDLE hProcess,
		IN uint32_t ProcessId,
		IN HANDLE hFile,
		IN MINIDUMP_TYPE DumpType,
		IN PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam, OPTIONAL
		IN PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam, OPTIONAL
		IN PMINIDUMP_CALLBACK_INFORMATION CallbackParam OPTIONAL
	);

	auto* const dbg_help = LoadLibrary(L"DBGHELP.DLL");
	auto dump_successful = false;

	if (dbg_help)
	{
		const auto write_dump = (MINIDUMP_WRITE_DUMP)GetProcAddress(dbg_help, "MiniDumpWriteDump");

		if (write_dump)
		{
			const auto w = platform::to_file_system_path(dump_file_path);
			auto* const dump_file = CreateFile(w.c_str(), GENERIC_READ | GENERIC_WRITE,
			                                   FILE_SHARE_WRITE | FILE_SHARE_READ,
			                                   nullptr, CREATE_ALWAYS, 0, nullptr);

			if (dump_file != INVALID_HANDLE_VALUE)
			{
				MINIDUMP_EXCEPTION_INFORMATION exception_information;
				exception_information.ThreadId = GetCurrentThreadId();
				exception_information.ExceptionPointers = exception_pointers;
				exception_information.ClientPointers = TRUE;

				auto dump_flags = MiniDumpWithDataSegs | // Include DS from all loaded mocults
					MiniDumpWithHandleData | // Include high level OS handle info
					MiniDumpScanMemory | // Scan for pointer references in module list
					MiniDumpWithUnloadedModules | // Recently Unloaded modules
					MiniDumpWithThreadInfo | // Include thread state information
					MiniDumpIgnoreInaccessibleMemory |
					// Ignore memory read failures when attempting to read innaccesible regions
					MiniDumpNormal; // Normal stack trace info

				dump_successful = write_dump(GetCurrentProcess(), GetCurrentProcessId(), dump_file,
				                             static_cast<MINIDUMP_TYPE>(dump_flags), &exception_information, nullptr,
				                             nullptr);

				CloseHandle(dump_file);
			}
			else
			{
				// Log the error if file creation failed
				const auto error = GetLastError();
				// Can't use normal logging during crash handling, but store for later
				(void)error; // Suppress unused variable warning
			}
		}
	}

	if (dbg_help)
	{
		FreeLibrary(dbg_help);
	}

	return dump_successful;
}


static LONG WINAPI exception_callback(EXCEPTION_POINTERS* pExceptionPointers)
{
	const auto app = g_app.lock();

	if (app)
	{
		const auto dump_file_path = platform::temp_file();

		if (create_dump(pExceptionPointers, dump_file_path))
		{
			app->crash(dump_file_path);
		}
	}

	return EXCEPTION_CONTINUE_SEARCH;
}

struct unhandled_exception_filter
{
	PTOP_LEVEL_EXCEPTION_FILTER _original = nullptr;

	unhandled_exception_filter() : _original(SetUnhandledExceptionFilter(exception_callback))
	{
	}

	~unhandled_exception_filter()
	{
		SetUnhandledExceptionFilter(_original);
	}
};

#endif // !WINSTORE

// The application recovery/restart APIs live in kernel32 but are not implemented on every
// host. Wine, for example, terminates the process when its unimplemented
// UnregisterApplicationRecoveryCallback stub is called. Resolve them dynamically so a missing
// export degrades to a harmless no-op instead of aborting the app.
namespace
{
	template <typename T>
	T resolve_kernel32(const char* name)
	{
		const auto h = GetModuleHandleW(L"kernel32.dll");
		return h ? reinterpret_cast<T>(GetProcAddress(h, name)) : nullptr;
	}

	using register_application_restart_t = HRESULT(WINAPI*)(PCWSTR, DWORD);
	using unregister_application_restart_t = HRESULT(WINAPI*)();
	using register_application_recovery_callback_t = HRESULT(WINAPI*)(APPLICATION_RECOVERY_CALLBACK, PVOID, DWORD,
	                                                                  DWORD);
	using unregister_application_recovery_callback_t = HRESULT(WINAPI*)();
	using application_recovery_in_progress_t = HRESULT(WINAPI*)(PBOOL);
	using application_recovery_finished_t = void(WINAPI*)(BOOL);
}

#ifndef WINSTORE

// Restart is desktop-only: RegisterApplicationRestart re-launches the executable directly,
// which for a packaged build would start the process without its package identity.
static void register_restart()
{
	const auto pRegisterApplicationRestart = resolve_kernel32<register_application_restart_t>(
		"RegisterApplicationRestart");
	if (!pRegisterApplicationRestart) return;

	const auto restart_cmd_line_w = str::utf8_to_utf16(restart_cmd_line);

	// RegisterApplicationRestart rejects anything longer, and wcscpy_s would terminate the process
	// rather than truncate. Losing restart registration is preferable to either.
	if (restart_cmd_line_w.size() >= RESTART_MAX_CMD_LINE)
	{
		df::log(__FUNCTION__, "restart command line too long to register");
		return;
	}

	static WCHAR wsCommandLine[RESTART_MAX_CMD_LINE];
	wcscpy_s(wsCommandLine, restart_cmd_line_w.c_str());
	const auto hr = pRegisterApplicationRestart(wsCommandLine, RESTART_NO_PATCH | RESTART_NO_REBOOT);
	df::assert_true(SUCCEEDED(hr));
}

#endif // !WINSTORE

static void unregister_restart()
{
	const auto pUnregisterApplicationRecoveryCallback = resolve_kernel32<unregister_application_recovery_callback_t>(
		"UnregisterApplicationRecoveryCallback");
	const auto pUnregisterApplicationRestart = resolve_kernel32<unregister_application_restart_t>(
		"UnregisterApplicationRestart");
	if (pUnregisterApplicationRecoveryCallback) pUnregisterApplicationRecoveryCallback();
	if (pUnregisterApplicationRestart) pUnregisterApplicationRestart();
}

static DWORD WINAPI recover_callback(PVOID pContext)
{
	df::log(__FUNCTION__, "*** recover callback ***");
	log_open_files_to_crash_files_list();
	flush_open_files_to_crash_files_list();

	BOOL bCanceled = FALSE;
	const auto pApplicationRecoveryInProgress = resolve_kernel32<application_recovery_in_progress_t>(
		"ApplicationRecoveryInProgress");
	if (pApplicationRecoveryInProgress) pApplicationRecoveryInProgress(&bCanceled);

	if (bCanceled)
	{
		df::log(__FUNCTION__, "Recovery was canceled by the user.");
	}

	const auto app = g_app.lock();

	if (app)
	{
		app->save_recovery_state();
	}

	df::close_log();

	const auto pApplicationRecoveryFinished = resolve_kernel32<application_recovery_finished_t>(
		"ApplicationRecoveryFinished");
	if (pApplicationRecoveryFinished) pApplicationRecoveryFinished(bCanceled ? FALSE : TRUE);
	return 0;
}


static void setup_restart()
{
#ifndef WINSTORE
	register_restart();
#endif
	// The recovery callback runs in both builds: it is what persists the crashed-file skip list
	// and the session recovery state when Windows terminates a hung or crashing process.
	const auto pRegisterApplicationRecoveryCallback = resolve_kernel32<register_application_recovery_callback_t>(
		"RegisterApplicationRecoveryCallback");
	if (!pRegisterApplicationRecoveryCallback) return;
	const auto hr = pRegisterApplicationRecoveryCallback(recover_callback, nullptr, RECOVERY_DEFAULT_PING_INTERVAL, 0);
	df::assert_true(SUCCEEDED(hr));
}

// Inspect the graphics crash-guard flags left by the previous run and fall back if it
// crashed while a graphics subsystem was active. A HW-decode crash disables only hardware
// video decoding (narrowest attribution); a crash with the GPU active but no decode in
// progress disables GPU rendering. GPU device loss is handled as a one-session software
// recovery: the user's preference is preserved and retried after that session exits cleanly.
// Escalation is graceful: a decode crash drops decode first; GPU is dropped only if a later
// run still crashes with decode already off (so only gpu_render remains set).
static void apply_gpu_crash_guard()
{
	const auto decode_crashed = platform::read_crash_guard(platform::crash_guard::hw_video_decode);
	const auto gpu_crashed = platform::read_crash_guard(platform::crash_guard::gpu_render);

	if (!decode_crashed && !gpu_crashed)
	{
		return;
	}

	// Attribute to the narrowest subsystem: a hardware-decode crash disables only HW video
	// decode (never the GPU), even if HW decode happens to already be off; only a crash with
	// no decode in progress disables GPU rendering.
	if (decode_crashed)
	{
		if (setting.use_d3d11va)
		{
			setting.use_d3d11va = false;
			df::log(__FUNCTION__,
			        "Previous run crashed during hardware video decode - disabling HW video decode (GPU rendering kept)");
		}
	}
	else if (gpu_crashed)
	{
		if (setting.use_gpu)
		{
			platform::suppress_crash_guard(platform::crash_guard::gpu_render, true);
			df::log(__FUNCTION__,
			        "Previous run lost the GPU device - using software rendering for this recovery session");
		}
		return;
	}

	// Hardware-decode failures retain the existing persistent fallback policy.
	setting.write();
	platform::set_crash_guard(platform::crash_guard::hw_video_decode, false);
	platform::set_crash_guard(platform::crash_guard::gpu_render, false);
}

uint32_t ui_wait_for_signal(platform::thread_event& te, const uint32_t timeout_ms,
                            const std::function<bool(LPMSG m)>& cb)
{
	df::assert_true(ui::is_ui_thread());

	const auto app = g_app_impl.lock();

	return app ? app->ui_wait_for_signal({te}, false, timeout_ms, cb) : platform::wait_for_timeout;
}

uint32_t platform::wait_for_timeout = WAIT_TIMEOUT - WAIT_OBJECT_0;

uint32_t platform::wait_for(const std::vector<std::reference_wrapper<thread_event>>& events, const uint32_t timeout_ms,
                            const bool wait_all)
{
	constexpr auto max_events = 64;
	df::assert_true(events.size() <= max_events);

	auto result = 0;

	if (ui::is_ui_thread())
	{
		const auto app = g_app_impl.lock();

		if (app)
		{
			result = app->ui_wait_for_signal(events, wait_all, timeout_ms, {});
		}
	}
	else
	{
		// WaitForMultipleObjects cannot exceed MAXIMUM_WAIT_OBJECTS; clamp in release builds too so
		// an over-long list cannot overrun the stack array.
		const auto count = static_cast<uint32_t>(std::min<size_t>(events.size(), max_events));
		HANDLE handles[max_events];
		for (auto i = 0u; i < count; ++i) handles[i] = events[i].get()._h;
		const auto wait_result = WaitForMultipleObjects(count, handles, wait_all,
		                                                timeout_ms ? timeout_ms : INFINITE);
		result = wait_result - WAIT_OBJECT_0;
	}

	return result;
}

bool ui::is_ui_thread()
{
	return is_current_thread_ui;
}


static void show_fatal_error(const std::string_view message)
{
	df::log(__FUNCTION__, message);

	std::wstring s;
	s += str::utf8_to_utf16(tt.title_error);
	s += L"\n\n";
	s += str::utf8_to_utf16(tt.error_cannot_continue);
	s += L"\n\n";
	s += str::utf8_to_utf16(message);

	::MessageBox(nullptr, s.c_str(), s_app_name_l, MB_OK | MB_ICONHAND);
}


//
//STDAPI SetProcessDpiAwareness(
//	_In_ PROCESS_DPI_AWARENESS value);

int WINAPI wWinMain(const HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, const LPWSTR lpCmdLine, int nCmdShow)
{
	get_resource_instance = hInstance;
	is_current_thread_ui = true;

	constexpr int result = 0;
	const auto app_impl = std::make_shared<win32_app>();
	ui::app_ptr app;

	// Every startup failure below returns early, so teardown is expressed as scope guards rather
	// than repeated inline cleanup; this one also runs after the catch block closes the log.
	const df::scope_exit final_exit([&app]
	{
		if (app) app->final_exit();
	});

	try
	{
		// Done in manifest
		//SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);

		// Does not improve performance but worth testing later

		init_color_styles();

#ifndef WINSTORE
		unhandled_exception_filter exceptions;
#endif

		app = app_impl->_app = create_app(app_impl);
		g_app = app;
		g_app_impl = app_impl;

		if (!app->pre_init())
		{
			return 0;
		}

		resources.init(get_resource_instance);

		WSADATA wsaData;

		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		{
			show_fatal_error(tt.error_winsock_failed);
			return 0;
		}

		const df::scope_exit cleanup_winsock([] { WSACleanup(); });

		df::start_time = platform::now();
		platform::set_thread_description("main");

		auto hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

		if (FAILED(hr))
		{
			show_fatal_error(tt.error_ole_failed);
			return 0;
		}

		const df::scope_exit cleanup_com([] { CoUninitialize(); });

		hr = OleInitialize(nullptr);

		if (FAILED(hr))
		{
			show_fatal_error(tt.error_ole_failed);
			return 0;
		}

		const df::scope_exit cleanup_ole([] { OleUninitialize(); });

		// Handle /test command line option: run tests in console mode and exit.
		// Parse into the global command_line early so options such as -no-gpu are available
		// before the graphics factories are created below (app->init re-parses the same
		// command line later, which is idempotent).
		{
			command_line.parse(str::utf16_to_utf8(lpCmdLine));

			if (command_line.console_test)
			{
				setup_headless_console();

				df::start_time = platform::now();
				resources.init(get_resource_instance);

				const int test_result = run_console_tests(command_line.test_filter);

				return test_result;
			}

			// Handle /gen-docs command line option: regenerate the wiki
			// documentation pages in console mode and exit.
			if (command_line.gen_docs)
			{
				setup_headless_console();

				df::start_time = platform::now();
				resources.init(get_resource_instance);

				const int docs_result = generate_wiki_docs(command_line.docs_path);

				return docs_result;
			}

			// Handle /validate-po command line option: validate the translation
			// .po files against the strings registered in app_text and exit.
			if (command_line.validate_po)
			{
				setup_headless_console();

				df::start_time = platform::now();
				resources.init(get_resource_instance);

				const int validate_result = validate_po_files();

				return validate_result;
			}

			// Handle /dup-report command line option: measure duplicate grouping over a
			// real library in console mode and exit. Read-only.
			if (command_line.dup_report)
			{
				setup_headless_console();

				df::start_time = platform::now();
				resources.init(get_resource_instance);

				const int report_result = run_duplicate_report(command_line.dup_report_folder,
				                                               command_line.dup_report_output);

				return report_result;
			}
		}

#if defined(_M_IX86) || defined(_M_X64)

		if (!platform::sse2_supported)
		{
			show_fatal_error(tt.error_sse2_needed);
			return 0;
		}
#endif

		if (!IsWindows7OrGreater())
		{
			show_fatal_error(tt.error_unsupported_os);
			return 0;
		}

		constexpr INITCOMMONCONTROLSEX iccx = {sizeof(INITCOMMONCONTROLSEX), ICC_BAR_CLASSES};

		if (!InitCommonControlsEx(&iccx))
		{
			show_fatal_error(tt.error_windows_common_controls_failed);
			return 0;
		}

		app_impl->_f = std::make_shared<factories>();

		// Load persisted settings before creating the graphics factories so the
		// "use hardware acceleration" (use_gpu) preference is honoured at startup. The app is
		// handed the single shared settings store here, before the UI is created, so it can load
		// its options from the same instance the platform uses for the crash-guard fallbacks.
		app->load_settings(platform::settings());
		apply_gpu_crash_guard();

		// Honour the -no-gpu command line switch (parsed into command_line above) as well as the
		// persisted use_gpu preference, so software (WARP) rendering can be forced from launch.
		const auto recover_without_gpu = platform::crash_guard_suppressed(platform::crash_guard::gpu_render);
		if (!app_impl->_f->init(setting.use_gpu && !command_line.no_gpu && !recover_without_gpu))
		{
			show_fatal_error(tt.error_atl_direct3d);
			return 0;
		}

		srand(static_cast<uint32_t>(GetTickCount()) ^ static_cast<uint32_t>(time(nullptr)));

		if (app->init(str::utf16_to_utf8(lpCmdLine)))
		{
			restart_cmd_line = app->restart_cmd_line();
			setup_restart();
			app_impl->ui_message_loop();
		}

		app->exit();
		app_impl->_f->destroy();
		app_impl->_f.reset();

		unregister_restart();
	}
	catch (std::exception& e)
	{
		df::log(__FUNCTION__, e.what());
		show_fatal_error(str::utf8_cast(e.what()));
	}

#ifdef _DEBUG
#endif // _DEBUG

	return result;
}

df::blob load_resource(const int id, const LPCWSTR lpType)
{
	df::blob result;
	auto* const hInstance = get_resource_instance;
	auto* const hrsrc = FindResourceW(hInstance, MAKEINTRESOURCE(id), lpType);

	if (hrsrc)
	{
		auto* const hg = LoadResource(hInstance, hrsrc);

		if (hg)
		{
			const auto* const data = static_cast<const uint8_t*>(LockResource(hg));
			result.assign(data, data + SizeofResource(hInstance, hrsrc));
			FreeResource(hg);
		}
	}

	return result;
}

control_host_impl* control_host_impl::_current = nullptr;

LRESULT control_host_impl::on_window_timer(uint32_t, WPARAM, LPARAM)
{
	ui::ticks_since_last_user_action += 1;

	if (!_hover_command_bounds.is_empty())
	{
		POINT pos{};
		if (GetCursorPos(&pos) && !_hover_command_bounds.contains({pos.x, pos.y}))
		{
			const auto h = _host.lock();
			if (h) h->command_hover(nullptr, {});
			_hover_command_bounds.clear();
		}
	}

	const auto h = _host.lock();
	if (h) h->tick();
	_pa->tick();

	return 0;
}

ui::bubble_window_ptr control_host_impl::create_bubble()
{
	auto result = std::make_shared<bubble_impl>(_gdi_ctx);

	if (result->create(m_hWnd, _pa->_f))
	{
		return result;
	}

	return nullptr;
}

ui::frame_ptr control_host_impl::create_frame(ui::frame_host_weak_ptr host, const ui::frame_style& style)
{
	auto result = std::make_shared<frame_impl>(_gdi_ctx, weak_from_this(), _host, host, style);

	if (result->create(m_hWnd, style, _pa->_f))
	{
		// Expired entries are never removed anywhere else, so a host that recreates its frames -
		// the media view rebuilds them on every layout change - would grow this list without bound
		// and make every broadcast walk more dead weak_ptrs than live frames.
		std::erase_if(_child_frames, [](const std::weak_ptr<frame_impl>& f) { return f.expired(); });
		_child_frames.push_back(result);
		return result;
	}

	return nullptr;
}

void win32_app::update_event_handles()
{
	df::assert_true(ui::is_ui_thread());

	app_thread_events.clear();
	app_event_actions.clear();

	if (_timer_handle != nullptr)
	{
		app_thread_events.emplace_back(_timer_handle);
		app_event_actions.emplace_back([this] { _app->prepare_frame(); });
	}

	// MsgWaitForMultipleObjectsEx accepts at most MAXIMUM_WAIT_OBJECTS - 1 handles (it reserves one
	// slot for the input queue). Exceeding it makes the wait fail and the message loop exit, so cap
	// the total wait set defensively here. The timer and idle handles must always fit, so folder
	// watches are the ones that get truncated. monitor_folders already caps its own list, but this
	// keeps the invariant local to the one place that builds the wait set.
	constexpr size_t max_wait_handles = MAXIMUM_WAIT_OBJECTS - 1;

	// Appended before the folder watches so that a truncation - here or in ui_wait_for_signal, which
	// drops the tail - can only cost a live folder watch, never the idle action that drains the UI
	// queue.
	app_thread_events.emplace_back(static_cast<HANDLE>(_idle_event._h));
	app_event_actions.emplace_back([this] { idle(); });

	for (auto& h : _folder_changes)
	{
		if (app_thread_events.size() >= max_wait_handles)
		{
			df::log(__FUNCTION__, "Folder-watch handles exceed the wait limit - some folders will not be live-watched");
			break;
		}

		app_thread_events.emplace_back(h.h);
		app_event_actions.emplace_back([this, h = h.h, path = h.path]
		{
			_app->folder_changed(path);
			FindNextChangeNotification(h);
		});
	}

	df::assert_true(app_thread_events.size() == app_event_actions.size());
	df::assert_true(app_thread_events.size() <= max_wait_handles);
}

void win32_app::tick() const
{
	if (WAIT_OBJECT_0 == WaitForSingleObject(_timer_handle, 0))
	{
		_app->prepare_frame();
	}
}

bool win32_app::pre_translate_message(MSG& m)
{
	if (!df::is_closing && is_pre_translate_message(m.message))
	{
		const auto mm = m.message;

		if (mm >= WM_MOUSEFIRST && mm <= WM_MOUSELAST)
		{
			bool changed = true;

			if (mm == WM_MOUSEMOVE)
			{
				changed = _last_mouse_move != 0 && _last_mouse_move != m.lParam;
				_last_mouse_move = m.lParam;
			}

			if (changed)
			{
				ui::ticks_since_last_user_action = 0;
			}
		}
		else if (mm == WM_KEYDOWN)
		{
			const auto c = m.wParam;

			if (c != VK_LEFT && c != VK_RIGHT)
			{
				ui::ticks_since_last_user_action = 0;
			}
		}

		if (mm == WM_KEYDOWN || mm == WM_SYSKEYDOWN)
		{
			const auto c = static_cast<char32_t>(m.wParam);
			auto* const focus_wnd = GetFocus();
			const auto ks = ui::current_key_state();

			if (!ks.alt && is_edit(focus_wnd))
			{
				if (c == VK_ESCAPE ||
					c == VK_UP ||
					c == VK_DOWN ||
					c == VK_TAB ||
					/*c == VK_LEFT ||
						c == VK_RIGHT ||*/
					c == VK_RETURN)
				{
					if (_app->key_down(c, ks))
					{
						return true;
					}
				}
				// standard edit shortcuts - dont translate
				if (is_edit_char(focus_wnd, c, ks))
				{
					return false;
				}
			}
			else
			{
				if (_app->key_down(c, ks))
				{
					return true;
				}
			}
		}
		else if (mm == WM_CHAR && !is_edit(GetFocus()) && _app->focus_mode() == ui::focus_mode::text_edit)
		{
			const auto c = static_cast<wchar_t>(m.wParam);
			std::wstring text;

			if (str::is_lead_surrogate(c))
			{
				_pending_high_surrogate = c;
				return true;
			}

			if (str::is_trail_surrogate(c))
			{
				if (_pending_high_surrogate)
				{
					text.push_back(_pending_high_surrogate);
					text.push_back(c);
				}
				_pending_high_surrogate = 0;
			}
			else
			{
				_pending_high_surrogate = 0;
				if (c >= L' ') text.push_back(c);
			}

			if (!text.empty() && _app->text_input(str::utf16_to_utf8(text))) return true;
		}

		if (_frame)
		{
			if (IsDialogMessage(_frame->m_hWnd, &m))
				return true;
		}
	}

	return false;
}

void win32_app::monitor_folders(const std::vector<df::folder_path>& folders_paths)
{
	for (const auto& w : _folder_changes)
	{
		FindCloseChangeNotification(w.h);
	}

	_folder_changes.clear();

	// Keep the number of folder-watch handles within the wait-set budget (see update_event_handles).
	// The timer and idle handles also occupy the set, so leave headroom below MAXIMUM_WAIT_OBJECTS.
	// Watching subtrees would collapse many folders into fewer handles but changes notification
	// semantics; capping is the simple, predictable choice - excess folders are simply not
	// live-watched and refresh through the normal scan paths instead.
	constexpr size_t max_folder_watches = MAXIMUM_WAIT_OBJECTS - 4;
	auto truncated = false;

	for (const auto& path : folders_paths)
	{
		if (_folder_changes.size() >= max_folder_watches)
		{
			truncated = true;
			break;
		}

		constexpr auto filter = FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_LAST_WRITE |
			FILE_NOTIFY_CHANGE_FILE_NAME
			| FILE_NOTIFY_CHANGE_DIR_NAME;
		const auto h = ::FindFirstChangeNotification(platform::to_file_system_path(path).c_str(), FALSE, filter);

		if (h != INVALID_HANDLE_VALUE && h != nullptr)
		{
			_folder_changes.emplace_back(h, path);
		}
	}

	if (truncated)
	{
		df::log(__FUNCTION__, std::format("Watching {} of {} folders - the rest refresh via scanning",
		                                  _folder_changes.size(), folders_paths.size()));
	}

	update_event_handles();
}

void win32_app::destroy()
{
	SetThreadExecutionState(ES_CONTINUOUS);
	_enable_screen_saver = true;

	for (const auto& w : _folder_changes)
	{
		FindCloseChangeNotification(w.h);
	}

	_folder_changes.clear();

	// destroy runs even when the message loop never started, so the timer may never have been created.
	if (_timer_handle != nullptr)
	{
		CloseHandle(_timer_handle);
		_timer_handle = nullptr;
	}
}

void win32_app::idle() const
{
	// No reset: _idle_event is auto-reset, so the wait that got us here already cleared it. Resetting
	// again would discard a wake armed by a worker between that wait and this call.
	_app->idle();
}

int win32_app::ui_message_loop()
{
	// A null handle only costs the frame-pacing timer; the loop still runs on window and idle events.
	_timer_handle = CreateWaitableTimer(nullptr, FALSE, nullptr);

	if (_timer_handle == nullptr)
	{
		df::log(__FUNCTION__, platform::last_os_error());
	}

	RECT size_rect;
	GetWindowRect(_frame->m_hWnd, &size_rect);

	uint32_t show_flags = SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE;

	if (_frame->cmd_show == SW_SHOW)
	{
		show_flags |= SWP_SHOWWINDOW;
	}

	SetWindowPos(
		_frame->m_hWnd, nullptr,
		size_rect.left, size_rect.top,
		size_rect.right - size_rect.left, size_rect.bottom - size_rect.top,
		show_flags
	);

	if (_frame->cmd_show != SW_SHOW)
	{
		ShowWindow(_frame->m_hWnd, _frame->cmd_show);
	}

	frame_delay(1000 / ui::default_ticks_per_second);
	update_event_handles();

	MSG msg;

	while (true)
	{
		const auto n = MsgWaitForMultipleObjectsEx(static_cast<uint32_t>(app_thread_events.size()),
		                                           app_thread_events.data(), INFINITE, QS_ALLINPUT, MWMO_ALERTABLE);

		if (n == WAIT_IO_COMPLETION)
		{
		}
		else if (n > app_event_actions.size())
		{
			return FALSE; // unexpected failure
		}
		else if (n < app_event_actions.size())
		{
			// Copied first: the action can reach update_event_handles, which clears this vector.
			const auto action = app_event_actions[n];
			action();
		}

		// A ready waitable timer has a lower wait index than the Windows message queue. If frame
		// production approaches the timer period, it can win every wait and leave mouse input queued.
		// Drain messages after each wake, regardless of which event caused it.
		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
			{
				destroy();
				return static_cast<int>(msg.wParam);
			}

			if (!pre_translate_message(msg))
			{
				TranslateMessage(&msg);
				::DispatchMessage(&msg);
			}
		}

		idle();
	}
}

uint32_t win32_app::ui_wait_for_signal(const std::vector<std::reference_wrapper<platform::thread_event>>& events,
                                       const bool wait_all, const uint32_t timeout_ms,
                                       const std::function<bool(LPMSG m)>& cb)
{
	auto result = 0u;
	auto signal_set = false;

	std::vector<HANDLE> thread_events;
	std::vector<std::function<void()>> event_actions;

	for (const auto& e : events)
	{
		thread_events.emplace_back(static_cast<HANDLE>(e.get()._h));
		event_actions.emplace_back([&signal_set] { signal_set = true; });
	}

	// Caller events occupy the first slots of the wait set (app events are appended after), so a
	// signalled caller event's index is the caller-relative result directly.
	const auto caller_event_count = static_cast<uint32_t>(events.size());

	// MsgWaitForMultipleObjects reserves one slot for the input queue. The app set is already near
	// that limit (frame timer, idle, up to 60 folder watches), so the appended events are truncated
	// to fit rather than letting the call fail with WAIT_FAILED.
	constexpr size_t max_wait_handles = MAXIMUM_WAIT_OBJECTS - 1;
	const auto room = caller_event_count < max_wait_handles ? max_wait_handles - caller_event_count : 0_z;

	MSG msg;
	msg.message = WM_NULL; // anything that isn't WM_QUIT

	// The appended app events (frame timer, idle, folder watches) wake this wait several times a
	// second, so the timeout has to run against a fixed deadline. Passing the full timeout on every
	// iteration restarts it on each of those wakes and it can never expire.
	const auto deadline = GetTickCount64() + timeout_ms;

	while (!df::is_closing && !signal_set)
	{
		// Re-copied every iteration, never snapshotted: the idle action this loop runs can reach
		// monitor_folders, which closes the folder-watch handles and rebuilds the app set. Waiting
		// again on the old handles fails the whole wait.
		thread_events.resize(caller_event_count);
		event_actions.resize(caller_event_count);

		const auto appended = std::min(room, app_thread_events.size());
		thread_events.insert(thread_events.end(), app_thread_events.begin(), app_thread_events.begin() + appended);
		event_actions.insert(event_actions.end(), app_event_actions.begin(), app_event_actions.begin() + appended);

		auto wait_ms = INFINITE;

		if (timeout_ms)
		{
			const auto now = GetTickCount64();

			if (now >= deadline)
			{
				return platform::wait_for_timeout;
			}

			wait_ms = static_cast<DWORD>(deadline - now);
		}

		const auto n = MsgWaitForMultipleObjects(static_cast<uint32_t>(thread_events.size()), thread_events.data(),
		                                         wait_all, wait_ms, QS_ALLINPUT);

		if (n == WAIT_TIMEOUT)
		{
			// Must be distinguished from index 0, which callers read as "the first event signalled".
			return platform::wait_for_timeout;
		}
		if (n > event_actions.size())
		{
			// WAIT_FAILED arrives here. Returning result (0) would be read as "the first caller
			// event signalled", so a failed wait must be reported as one that did not complete.
			return platform::wait_for_timeout;
		}
		if (n < event_actions.size())
		{
			event_actions[n]();

			// Only a caller event (index < caller_event_count) ends the wait and defines the result.
			// App events (timer / folder / idle) run their side effects but leave the loop running.
			if (n < caller_event_count)
			{
				result = n;
			}
		}
		else
		{
			while (!df::is_closing && PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				if (cb && cb(&msg))
					continue;

				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
	}

	// Returning result here would claim the first caller event signalled. The loop also exits on
	// df::is_closing, so only signal_set proves a caller event was actually observed.
	return signal_set ? result : platform::wait_for_timeout;
}

void win32_app::sys_command(const ui::sys_command_type cmd)
{
	switch (cmd)
	{
	case ui::sys_command_type::MINIMIZE:
		::PostMessage(_frame->m_hWnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
		break;
	case ui::sys_command_type::MAXIMIZE:
		::PostMessage(_frame->m_hWnd, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
		break;
	case ui::sys_command_type::RESTORE:
		::PostMessage(_frame->m_hWnd, WM_SYSCOMMAND, SC_RESTORE, 0);
		break;
	default: ;
	}
}

void win32_app::full_screen(const bool full)
{
	_frame->full_screen(full);
	_last_mouse_move = 0;
}

void win32_app::frame_delay(const int delay)
{
	if (delay != _frame_delay && nullptr != _timer_handle)
	{
		_frame_delay = delay;
		constexpr LARGE_INTEGER liDueTime = {0};
		if (!SetWaitableTimer(_timer_handle, &liDueTime, _frame_delay, nullptr /*timer_cb*/, nullptr /*this*/, FALSE))
		{
			df::log(__FUNCTION__, platform::last_os_error());
		}
		update_event_handles();
	}
}

void win32_app::set_font_base_size(const int i)
{
	if (global_base_font_size != i)
	{
		global_base_font_size = i;

		if (_frame)
		{
			_frame->update_font_sizes();
		}
	}
}

ui::control_frame_ptr win32_app::create_app_frame(const platform::setting_file_ptr& store,
                                                  const ui::frame_host_weak_ptr& host)
{
	WINDOWPLACEMENT wp = {};
	wp.length = sizeof(WINDOWPLACEMENT);
	wp.showCmd = SW_SHOW;

	{
		WINDOWPLACEMENT wp2 = {};
		size_t dw = sizeof(wp2);

		// A short or corrupt setting would otherwise leave part of the structure uninitialised and
		// place the window at a garbage position.
		if (store->read({}, s_window_rect, std::bit_cast<uint8_t*>(&wp2), dw) && dw == sizeof(wp2))
		{
			wp2.length = static_cast<uint32_t>(dw);
			memcpy_s(&wp, sizeof(wp), &wp2, sizeof(wp2));

			// Never start minimized, and never honour a showCmd we did not write.
			if (wp.showCmd != SW_SHOW && wp.showCmd != SW_SHOWNORMAL && wp.showCmd != SW_SHOWMAXIMIZED)
			{
				wp.showCmd = SW_SHOW;
			}

			const HDC hdc_screen = CreateDC(L"DISPLAY", nullptr, nullptr, nullptr);

			if (hdc_screen)
			{
				const win_rect screenRect(0, 0, GetDeviceCaps(hdc_screen, HORZRES),
				                          GetDeviceCaps(hdc_screen, VERTRES));

				// Never start bigger than the screen?
				if (screenRect.intersects(wp.rcNormalPosition))
				{
					wp.rcNormalPosition = screenRect.intersection(wp.rcNormalPosition);
				}

				DeleteDC(hdc_screen);
			}
		}
	}

	ui::color_style colors = {
		ui::style::color::toolbar_background, ui::style::color::view_text, ui::style::color::view_selected_background
	};

	const auto monitor = MonitorFromRect(&wp.rcNormalPosition, MONITOR_DEFAULTTONEAREST);
	const auto default_scale_fac = GetScalingFactorFromDPI(GetPerMonitorDPI(monitor));
	auto ctx = std::make_shared<owner_context>(default_scale_fac);
	auto result = std::make_shared<control_host_impl>(ctx, host, _app, this, true, colors);
	constexpr auto window_style = WS_CAPTION | WS_OVERLAPPED | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_SYSMENU |
		WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
	constexpr auto window_ex_style = WS_EX_WINDOWEDGE | WS_EX_APPWINDOW; // | WS_EX_LAYERED | WS_EX_COMPOSITED;

	if (!IsRectEmpty(&wp.rcNormalPosition))
	{
		if (result->Create(nullptr, wp.rcNormalPosition, window_style, window_ex_style) == nullptr)
		{
			return nullptr;
		}
	}
	else
	{
		if (result->Create(nullptr, {}, window_style, window_ex_style) == nullptr)
		{
			return nullptr;
		}
	}

	SetWindowText(result->m_hWnd, s_app_name_l);
	// The window icon drives Alt+Tab and the title bar. The package AppList assets only cover
	// Start and the taskbar, so without this the Store build falls back to the 32px window class
	// icon upscaled. Set it in both builds.
	SendMessage(result->m_hWnd, WM_SETICON, ICON_BIG, std::bit_cast<LPARAM>(resources.diffractor_64));
	SendMessage(result->m_hWnd, WM_SETICON, ICON_SMALL, std::bit_cast<LPARAM>(resources.diffractor_32));

	ctx->set_window_font(result->m_hWnd, ui::style::font_face::dialog);
	WTSRegisterSessionNotification(result->m_hWnd, NOTIFY_FOR_THIS_SESSION);
	result->cmd_show = wp.showCmd;

	_frame = result;
	ui_app_wnd = result->m_hWnd;
	return result;
}

void platform::sync_app_window_enabled()
{
	if (!IsWindow(ui_app_wnd)) return;

	auto* const impl = reinterpret_cast<win_impl*>(GetWindowLongPtr(ui_app_wnd, GWLP_USERDATA));
	auto* const frame = dynamic_cast<control_host_impl*>(impl);
	if (frame) frame->sync_enabled();
}

namespace
{
	// Renders one off-screen control into a pre-filled top-down DIB and reports what it drew.
	// This is the exact request buffered_control_paint makes of a control during WM_PAINT.
	void probe_control_render(const HWND control, const sizei extent, int& painted_pixels, int& colors)
	{
		constexpr uint32_t fill = 0x00ff00ff; // magenta - no control theme draws it

		BITMAPINFO bi = {};
		bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bi.bmiHeader.biWidth = extent.cx;
		bi.bmiHeader.biHeight = -extent.cy;
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;

		void* bits = nullptr;
		const auto screen_dc = GetDC(nullptr);
		const auto dc = CreateCompatibleDC(screen_dc);
		const auto bitmap = CreateDIBSection(screen_dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
		ReleaseDC(nullptr, screen_dc);

		if (dc && bitmap && bits)
		{
			const auto old_bitmap = SelectObject(dc, bitmap);
			auto* const pixels = static_cast<uint32_t*>(bits);
			const auto count = static_cast<size_t>(extent.cx) * extent.cy;
			std::fill_n(pixels, count, fill);

			SendMessage(control, WM_PRINTCLIENT, std::bit_cast<WPARAM>(dc), PRF_CLIENT);
			GdiFlush();

			df::hash_set<uint32_t> distinct;

			for (auto i = 0u; i < count; ++i)
			{
				const auto pixel = pixels[i] & 0x00ffffff;
				distinct.emplace(pixel);
				if (pixel != fill) painted_pixels += 1;
			}

			colors = static_cast<int>(distinct.size());
			SelectObject(dc, old_bitmap);
		}

		if (bitmap) DeleteObject(bitmap);
		if (dc) DeleteDC(dc);
	}
}

platform::control_paint_probe platform::probe_buffered_control_paint()
{
	control_paint_probe result;

	constexpr sizei extent{200, 32};

	const auto parent = CreateWindowEx(0, L"STATIC", nullptr, WS_POPUP, 0, 0, extent.cx, extent.cy * 2,
	                                   nullptr, nullptr, get_resource_instance, nullptr);
	if (!parent) return result;

	const df::scope_exit destroy_parent([parent] { DestroyWindow(parent); });

	const auto trackbar = CreateWindowEx(0, TRACKBAR_CLASS, nullptr, WS_CHILD | WS_VISIBLE,
	                                     0, 0, extent.cx, extent.cy, parent, nullptr, get_resource_instance, nullptr);

	if (trackbar)
	{
		SendMessage(trackbar, TBM_SETRANGE, FALSE, MAKELPARAM(0, 100));
		SendMessage(trackbar, TBM_SETPOS, TRUE, 50);
		probe_control_render(trackbar, extent, result.trackbar_painted_pixels, result.trackbar_colors);
	}

	const auto toolbar = CreateWindowEx(0, TOOLBARCLASSNAME, nullptr,
	                                    WS_CHILD | WS_VISIBLE | TBSTYLE_LIST | TBSTYLE_CUSTOMERASE | CCS_NODIVIDER |
	                                    CCS_NOPARENTALIGN | CCS_NORESIZE,
	                                    0, 0, extent.cx, extent.cy, parent, nullptr, get_resource_instance, nullptr);

	if (toolbar)
	{
		SendMessage(toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
		SendMessage(toolbar, TB_SETMAXTEXTROWS, 1, 0);

		TBBUTTON buttons[2] = {};
		const auto first_text = L"Ab";
		const auto second_text = L"Cd";
		buttons[0] = {
			I_IMAGENONE, 1, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE, {}, 0,
			std::bit_cast<INT_PTR>(first_text)
		};
		buttons[1] = {
			I_IMAGENONE, 2, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE, {}, 0,
			std::bit_cast<INT_PTR>(second_text)
		};
		SendMessage(toolbar, TB_ADDBUTTONS, 2, std::bit_cast<LPARAM>(&buttons[0]));

		probe_control_render(toolbar, extent, result.toolbar_painted_pixels, result.toolbar_colors);
	}

	const auto button = CreateWindowEx(0, L"BUTTON", L"Check", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
	                                   0, 0, extent.cx, extent.cy, parent, nullptr, get_resource_instance, nullptr);

	if (button)
	{
		SendMessage(button, BM_SETCHECK, BST_CHECKED, 0);
		probe_control_render(button, extent, result.button_painted_pixels, result.button_colors);
	}

	return result;
}

// The block is expressed only through this thread's execution state, which Windows drops when
// the process ends however it ends. The SPI_SETSCREENSAVEACTIVE / SPI_SETLOWPOWERACTIVE /
// SPI_SETPOWEROFFACTIVE route this replaced changed the user's system-wide settings, so a crash,
// a kill or a shutdown left their screen saver and idle power-down switched off for good.
void win32_app::enable_screen_saver(const bool enable)
{
	df::assert_true(ui::is_ui_thread());

	if (_enable_screen_saver != enable)
	{
		// ES_SYSTEM_REQUIRED as well as ES_DISPLAY_REQUIRED: on its own the display request keeps
		// the screen lit but leaves the system idle timer free to suspend the machine mid-playback.
		const auto state = enable
			                   ? ES_CONTINUOUS
			                   : (ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);

		if (SetThreadExecutionState(state) == 0)
		{
			df::log(__FUNCTION__, platform::last_os_error());
		}

		_enable_screen_saver = enable;
	}
}
