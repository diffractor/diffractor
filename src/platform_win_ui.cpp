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
#include "platform_win_visual.h"

#include <dwmapi.h>
#include <Dbt.h>
#include <versionhelpers.h>
#include <Shellapi.h>
#include <Shobjidl.h>
#include <DbgHelp.h>
#include <winsock2.h>
#include <wtsapi32.h>
#include <shlguid.h>

#include <Shlwapi.h>

#include "app_text.h"
#include "av_format.h"
#include "files.h"
#include "ui_elements.h"
#include "util_spell.h"
#include "platform_win_res.h"
#include "platform_win_browser.h"

#pragma comment(lib, "wtsapi32" )
#pragma comment(lib, "D2d1" )
#pragma comment(lib, "dxgi")
#pragma comment(lib, "d3d11")
#pragma comment(lib, "dcomp")
#pragma comment(lib, "Dwrite")
#pragma comment(lib, "dxguid")
#pragma comment(lib, "Shlwapi")
#pragma comment(lib, "Shcore")
#pragma comment(lib, "Windowscodecs")

static constexpr auto nonclient_border_thickness = 5;
static constexpr auto base_icon_cxy = 18;
int ui::ticks_since_last_user_action = 0;

static constexpr std::u8string_view s_window_rect = u8"WindowRect";
static std::u8string restart_cmd_line;

extern bool number_format_invalid;

class button_impl;
class control_host_impl;
class win32_app;

int calc_icon_cxy(const double scale_factor)
{
	return df::round(base_icon_cxy * scale_factor);
}

/////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////

int (WINAPI* ptrGetSystemMetricsForDpi)(int, UINT) = nullptr;
BOOL (WINAPI* ptrEnableNonClientDpiScaling)(HWND) = nullptr;
UINT (WINAPI* pfnGetDpiForSystem)() = nullptr;
UINT (WINAPI* pfnGetDpiForWindow)(HWND) = nullptr;
BOOL (WINAPI* ptrAreDpiAwarenessContextsEqual)(DPI_AWARENESS_CONTEXT, DPI_AWARENESS_CONTEXT) = nullptr;
DPI_AWARENESS_CONTEXT (WINAPI* ptrGetWindowDpiAwarenessContext)(HWND) = nullptr;

// Convenient loading function, see WinMain
//  - simplified version of https://github.com/tringi/emphasize/blob/master/Windows/Windows_Symbol.hpp

template <typename P>
bool Symbol(HMODULE h, P& pointer, const char* name)
{
	if (P p = reinterpret_cast<P>(GetProcAddress(h, name)))
	{
		pointer = p;
		return true;
	}
	return false;
}

template <typename P>
bool Symbol(HMODULE h, P& pointer, USHORT index)
{
	return Symbol(h, pointer, MAKEINTRESOURCEA(index));
}

/////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////

void ui_wait_for_signal(platform::thread_event& te, uint32_t timeout_ms, const std::function<bool(LPMSG m)>& cb);

#define ABM_GETAUTOHIDEBAREX    0x0000000b // multimon aware autohide bars
//#define WS_EX_NOREDIRECTIONBITMAP 0x00200000L


#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp)                        ((int)(short)LOWORD(lp))
#endif
#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lp)                        ((int)(short)HIWORD(lp))
#endif

struct win_rect : public RECT
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

	win_rect(int l, int t, int r, int b) noexcept
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

	win_rect inflate(int xy) const noexcept
	{
		return {left - xy, top - xy, right + xy, bottom + xy};
	}

	win_rect inflate(int x, int y) const noexcept
	{
		return {left - x, top - y, right + x, bottom + y};
	}

	win_rect offset(int x, int y) const noexcept
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

static ui::key_state to_key_state(WPARAM wParam)
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


static std::wstring window_text_w(HWND h)
{
	const auto len = ::GetWindowTextLength(h);
	if (len == 0) return {};
	std::wstring result(len + 1, 0);
	GetWindowText(h, &result[0], len + 1);
	result.resize(len, 0);
	return result;
}

static std::u8string window_text(HWND h)
{
	return str::utf16_to_utf8(window_text_w(h));
}

static void fill_rect(HDC hdc, DWORD clr, const win_rect& bounds)
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
	HCURSOR move = nullptr;
	HCURSOR left_right = nullptr;
	HCURSOR up_down = nullptr;
	HCURSOR hand_up = nullptr;
	HCURSOR hand_down = nullptr;
	HCURSOR select_cur = nullptr;
	HICON diffractor_16 = nullptr;
	HICON diffractor_32 = nullptr;
	HICON diffractor_64 = nullptr;

	void init(HINSTANCE h)
	{
		link = LoadCursor(h, MAKEINTRESOURCE(IDC_HANDLINK));
		normal = LoadCursor(nullptr, IDC_ARROW);
		move = LoadCursor(h, MAKEINTRESOURCE(IDC_MOVE));
		left_right = LoadCursor(nullptr, IDC_SIZEWE);
		up_down = LoadCursor(nullptr, IDC_SIZENS);
		hand_up = LoadCursor(h, MAKEINTRESOURCE(IDC_HANDUP));
		hand_down = LoadCursor(h, MAKEINTRESOURCE(IDC_HANDDOWN));
		select_cur = LoadCursor(h, MAKEINTRESOURCE(IDC_SELECT));
		diffractor_16 = static_cast<HICON>(LoadImage(h, MAKEINTRESOURCE(IDI_DIFFRACTOR), IMAGE_ICON, 16, 16, 0));
		diffractor_32 = static_cast<HICON>(LoadImage(h, MAKEINTRESOURCE(IDI_DIFFRACTOR), IMAGE_ICON, 32, 32, 0));
		diffractor_64 = static_cast<HICON>(LoadImage(h, MAKEINTRESOURCE(IDI_DIFFRACTOR), IMAGE_ICON, 64, 64, 0));
	}
};

static resources_t resources;

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

	ui::style::color::sidebar_background = 0x00444444;
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

int keys::APPS = VK_APPS;
int keys::BACK = VK_BACK;
int keys::BROWSER_BACK = VK_BROWSER_BACK;
int keys::BROWSER_FAVORITES = VK_BROWSER_FAVORITES;
int keys::BROWSER_FORWARD = VK_BROWSER_FORWARD;
int keys::BROWSER_HOME = VK_BROWSER_HOME;
int keys::BROWSER_REFRESH = VK_BROWSER_REFRESH;
int keys::BROWSER_SEARCH = VK_BROWSER_SEARCH;
int keys::BROWSER_STOP = VK_BROWSER_STOP;
int keys::DEL = VK_DELETE;
int keys::DOWN = VK_DOWN;
int keys::ESCAPE = VK_ESCAPE;
int keys::F1 = VK_F1;
int keys::F10 = VK_F10;
int keys::F11 = VK_F11;
int keys::F2 = VK_F2;
int keys::F3 = VK_F3;
int keys::F4 = VK_F4;
int keys::F5 = VK_F5;
int keys::F6 = VK_F6;
int keys::F7 = VK_F7;
int keys::F8 = VK_F8;
int keys::F9 = VK_F9;
int keys::INSERT = VK_INSERT;
int keys::LEFT = VK_LEFT;
int keys::MEDIA_NEXT_TRACK = VK_MEDIA_NEXT_TRACK;
int keys::MEDIA_PLAY_PAUSE = VK_MEDIA_PLAY_PAUSE;
int keys::MEDIA_PREV_TRACK = VK_MEDIA_PREV_TRACK;
int keys::MEDIA_STOP = VK_MEDIA_STOP;
int keys::NEXT = VK_NEXT;
int keys::OEM_4 = VK_OEM_4;
int keys::OEM_6 = VK_OEM_6;
int keys::OEM_PLUS = VK_OEM_PLUS;
int keys::PRIOR = VK_PRIOR;
int keys::RETURN = VK_RETURN;
int keys::RIGHT = VK_RIGHT;
int keys::SPACE = VK_SPACE;
int keys::TAB = VK_TAB;
int keys::UP = VK_UP;
int keys::VOLUME_DOWN = VK_VOLUME_DOWN;
int keys::VOLUME_MUTE = VK_VOLUME_MUTE;
int keys::VOLUME_UP = VK_VOLUME_UP;
int keys::HOME = VK_HOME;
int keys::END = VK_END;

std::u8string_view keys::format(const int key)
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
	if (key == INSERT) return tt.keyboard_insert;
	if (key == LEFT) return tt.keyboard_left;
	if (key == MEDIA_NEXT_TRACK) return tt.keyboard_media_next_track;
	if (key == MEDIA_PLAY_PAUSE) return tt.keyboard_media_play_pause;
	if (key == MEDIA_PREV_TRACK) return tt.keyboard_media_prev_track;
	if (key == MEDIA_STOP) return tt.keyboard_media_stop;
	if (key == NEXT) return tt.keyboard_next;
	if (key == OEM_4) return tt.keyboard_oem_4;
	if (key == OEM_6) return tt.keyboard_oem_6;
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
	return u8"?";
}

/////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////

static DWORD ui_thread_id = 0;
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

static bool is_edit(HWND hWnd)
{
	wchar_t class_name[100];
	return IsWindow(hWnd) && ::GetClassName(hWnd, class_name, 100) && is_edit_class(class_name);
}

static bool is_button_class(HWND hWnd)
{
	if (hWnd == nullptr)
		return false;

	wchar_t szClassName[100];

	return ::GetClassName(hWnd, szClassName, 100) &&
		(_wcsicmp(szClassName, L"BUTTON") == 0);
}

static bool is_toolbar(HWND hWnd)
{
	if (hWnd == nullptr)
		return false;

	wchar_t szClassName[100];

	return ::GetClassName(hWnd, szClassName, 100) &&
		(_wcsicmp(szClassName, TOOLBARCLASSNAME) == 0);
}

static void track_mouse_leave(HWND hWnd)
{
	TRACKMOUSEEVENT tme = {0};
	tme.cbSize = sizeof(tme);
	tme.dwFlags = TME_LEAVE;
	tme.hwndTrack = hWnd;
	tme.dwHoverTime = 0;
	TrackMouseEvent(&tme);
}

static bool wants_return(HWND hWnd)
{
	if (is_edit(hWnd))
	{
		return (GetWindowLongPtr(hWnd, GWL_STYLE) & ES_WANTRETURN) && !(GetKeyState(VK_CONTROL) & 0x8000);
	}

	return false;
}

void SetFont(
	HWND hwnd,
	_In_ HFONT hFont,
	_In_ BOOL bRedraw = TRUE) noexcept
{
	df::assert_true(IsWindow(hwnd));
	::SendMessage(hwnd, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(bRedraw, 0));
}

HFONT GetFont(HWND hwnd) noexcept
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
	DWORD nFonts = 0;
	return AddFontMemResourceEx(font_data.data(), static_cast<uint32_t>(font_data.size()), nullptr, &nFonts);
}

static HFONT create_font(const ui::style::font_size type, const int base_font_size, const bool clear_type = false)
{
	static auto* icon_font = load_icon_font();
	LOGFONT lf;
	memset(&lf, 0, sizeof(lf));

	lf.lfWeight = FW_NORMAL;
	wcscpy_s(lf.lfFaceName, LF_FACESIZE, L"Calibri");
	lf.lfOutPrecision = OUT_TT_PRECIS;
	lf.lfQuality = clear_type ? CLEARTYPE_NATURAL_QUALITY : ANTIALIASED_QUALITY;

	switch (type)
	{
	case ui::style::font_size::dialog:
		lf.lfHeight = -base_font_size;
		break;
	case ui::style::font_size::code:
		lf.lfHeight = -df::mul_div(base_font_size, 4, 5);
		wcscpy_s(lf.lfFaceName, LF_FACESIZE, L"Consolas");
		break;
	case ui::style::font_size::icons:
		lf.lfHeight = -base_font_size;
		wcscpy_s(lf.lfFaceName, LF_FACESIZE, L"Segoe MDL2 Assets");
		break;
	case ui::style::font_size::small_icons:
		lf.lfHeight = - df::mul_div(base_font_size, 10, 16);
		wcscpy_s(lf.lfFaceName, LF_FACESIZE, L"Segoe MDL2 Assets");
		break;
	case ui::style::font_size::title:
		lf.lfHeight = -df::mul_div(base_font_size, 3, 2);
	//lf.lfWeight = FW_NORMAL;
		break;
	case ui::style::font_size::mega:
		lf.lfHeight = -df::mul_div(base_font_size, 9, 4);
	//lf.lfWeight = FW_BOLD;
		break;
	default:
		break;
	}

	return ::CreateFontIndirect(&lf);
}


static HWND window_from_location(HWND parent, pointi loc)
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

int gdi_text_line_height(HDC hdc, HFONT font)
{
	if (!hdc) return 0;

	auto* const old_font = SelectObject(hdc, font);
	TEXTMETRIC tm;
	ZeroMemory(&tm, sizeof(tm));
	GetTextMetrics(hdc, &tm);
	SelectObject(hdc, old_font);

	return tm.tmHeight;
}

static int gdi_text_line_height(HWND hwnd, HFONT font)
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

//int render::text_line_height(style::font_size font)
//{
//	const auto i = cached_font_heights.find(font);
//
//	if (i == cached_font_heights.cend())
//	{
//		const auto height = text_line_height(app_wnd(), gdi_font(font));
//		cached_font_heights[font] = height;
//		return height;
//	}
//
//	return i->second;
//}

static DWORD text_style_to_draw_format(ui::style::text_style style)
{
	switch (style)
	{
	case ui::style::text_style::single_line:
		return DT_SINGLELINE | DT_NOCLIP;
	case ui::style::text_style::single_line_center:
		return DT_SINGLELINE | DT_CENTER | DT_NOCLIP;
	case ui::style::text_style::single_line_far:
		return DT_SINGLELINE | DT_RIGHT | DT_NOCLIP;
	case ui::style::text_style::multiline_center:
		return DT_NOPREFIX | DT_WORDBREAK | DT_NOCLIP | DT_CENTER;
	case ui::style::text_style::multiline:
	default:
		return DT_NOPREFIX | DT_WORDBREAK | DT_NOCLIP;
	}
}

static void draw_gradient(HDC dc, const recti r, DWORD c1, DWORD c2)
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

static void streach_box(HDC dc, HDC dcmem, const recti r, int cxy, int xy, bool fillCenter)
{
	const int sbm = SetStretchBltMode(dc, HALFTONE);

	const auto cxy2 = cxy * 2;
	const auto w = xy - (cxy * 2);
	const auto l = xy - cxy;

	StretchBlt(dc, r.left + cxy, r.top, r.width() - cxy2, cxy, dcmem, cxy, 0, w, cxy, SRCCOPY);
	StretchBlt(dc, r.left, r.top + cxy, cxy, r.height() - cxy2, dcmem, 0, cxy, cxy, w, SRCCOPY);
	StretchBlt(dc, r.right - cxy, r.top + cxy, cxy, r.height() - cxy2, dcmem, l, cxy, cxy, w, SRCCOPY);
	StretchBlt(dc, r.left + cxy, r.bottom - cxy, r.width() - cxy2, cxy, dcmem, cxy, l, w, cxy, SRCCOPY);

	const auto br = xy - cxy;
	BitBlt(dc, r.left, r.top, cxy, cxy, dcmem, 0, 0, SRCCOPY);
	BitBlt(dc, r.right - cxy, r.top, cxy, cxy, dcmem, br, 0, SRCCOPY);
	BitBlt(dc, r.left, r.bottom - cxy, cxy, cxy, dcmem, 0, br, SRCCOPY);
	BitBlt(dc, r.right - cxy, r.bottom - cxy, cxy, cxy, dcmem, br, br, SRCCOPY);

	if (fillCenter)
	{
		StretchBlt(dc, r.left + cxy, r.top + cxy, r.width() - cxy2, r.height() - cxy2, dcmem, cxy, cxy, w, w, SRCCOPY);
	}

	SetStretchBltMode(dc, sbm);
}

HBITMAP create_round_rect(HDC hdc_ref, COLORREF fill_clr, COLORREF edge_clr, COLORREF bg_clr, const int radius)
{
	const HBITMAP result = CreateCompatibleBitmap(hdc_ref, 32, 32);

	if (result)
	{
		auto* const hdc_bm = CreateCompatibleDC(hdc_ref);

		if (hdc_bm)
		{
			auto* const hbm_old = SelectObject(hdc_bm, result);

			if (hbm_old)
			{
				ComPtr<ID2D1Factory> pD2DFactory;
				ComPtr<ID2D1DCRenderTarget> rt;

				HRESULT hr = D2D1CreateFactory(
					D2D1_FACTORY_TYPE_SINGLE_THREADED,
					pD2DFactory.GetAddressOf());

				if (SUCCEEDED(hr))
				{
					const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
						D2D1_RENDER_TARGET_TYPE_DEFAULT,
						D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
						0,
						0,
						D2D1_RENDER_TARGET_USAGE_NONE,
						D2D1_FEATURE_LEVEL_DEFAULT
					);

					hr = pD2DFactory->CreateDCRenderTarget(&props, &rt);
				}

				if (SUCCEEDED(hr))
				{
					const RECT rc = {0, 0, 32, 32};
					hr = rt->BindDC(hdc_bm, &rc);
				}

				if (SUCCEEDED(hr))
				{
					rt->BeginDraw();
					rt->Clear(D2D1::ColorF(ui::bgr(bg_clr), 1.0f));

					ComPtr<ID2D1SolidColorBrush> fill_brush;
					hr = rt->CreateSolidColorBrush(D2D1::ColorF(ui::bgr(fill_clr), 1.0), &fill_brush);

					D2D1_ROUNDED_RECT r;
					r.rect = D2D1::RectF(1.0f, 1.0f, 31.0f, 31.0f);
					r.radiusX = static_cast<float>(radius);
					r.radiusY = static_cast<float>(radius);

					if (SUCCEEDED(hr))
					{
						rt->FillRoundedRectangle(r, fill_brush.Get());
					}

					if (fill_clr != edge_clr)
					{
						ComPtr<ID2D1SolidColorBrush> edge_brush;
						hr = rt->CreateSolidColorBrush(D2D1::ColorF(ui::bgr(edge_clr), 1.0), &edge_brush);

						if (SUCCEEDED(hr))
						{
							rt->DrawRoundedRectangle(r, edge_brush.Get(), 2.6f);
						}
					}

					rt->EndDraw();
				}

				SelectObject(hdc_bm, hbm_old);
			}

			DeleteDC(hdc_bm);
		}
	}

	return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool is_rtl(int c)
{
	return
		(c == 0x05BE) || (c == 0x05C0) || (c == 0x05C3) || (c == 0x05C6) ||
		((c >= 0x05D0) && (c <= 0x05F4)) ||
		(c == 0x0608) || (c == 0x060B) || (c == 0x060D) ||
		((c >= 0x061B) && (c <= 0x064A)) ||
		((c >= 0x066D) && (c <= 0x066F)) ||
		((c >= 0x0671) && (c <= 0x06D5)) ||
		((c >= 0x06E5) && (c <= 0x06E6)) ||
		((c >= 0x06EE) && (c <= 0x06EF)) ||
		((c >= 0x06FA) && (c <= 0x0710)) ||
		((c >= 0x0712) && (c <= 0x072F)) ||
		((c >= 0x074D) && (c <= 0x07A5)) ||
		((c >= 0x07B1) && (c <= 0x07EA)) ||
		((c >= 0x07F4) && (c <= 0x07F5)) ||
		((c >= 0x07FA) && (c <= 0x0815)) ||
		(c == 0x081A) || (c == 0x0824) || (c == 0x0828) ||
		((c >= 0x0830) && (c <= 0x0858)) ||
		((c >= 0x085E) && (c <= 0x08AC)) ||
		(c == 0x200F) || (c == 0xFB1D) ||
		((c >= 0xFB1F) && (c <= 0xFB28)) ||
		((c >= 0xFB2A) && (c <= 0xFD3D)) ||
		((c >= 0xFD50) && (c <= 0xFDFC)) ||
		((c >= 0xFE70) && (c <= 0xFEFC)) ||
		((c >= 0x10800) && (c <= 0x1091B)) ||
		((c >= 0x10920) && (c <= 0x10A00)) ||
		((c >= 0x10A10) && (c <= 0x10A33)) ||
		((c >= 0x10A40) && (c <= 0x10B35)) ||
		((c >= 0x10B40) && (c <= 0x10C48)) ||
		((c >= 0x1EE00) && (c <= 0x1EEBB));
}

static void swap_rtl(wchar_t* begin, wchar_t* end, int* positions)
{
	auto* pos = begin;

	while (pos < end)
	{
		const auto c = *pos;

		if (is_rtl(c))
		{
			const auto* const range_begin = pos;

			while (pos < end && (is_rtl(c) || str::is_white_space(c)))
			{
				pos += 1;
			}

			auto* const xb = positions + (range_begin - begin);
			const auto* const xe = positions + (pos - begin);

			const auto xstart = *xb;
			const auto xend = *xe;
			const auto len = xe - xb;
			auto* const reversed = static_cast<int*>(alloca(sizeof(int) * len));

			if (reversed)
			{
				for (auto i = 0; i < len; i++)
				{
					reversed[i] = xend - (xb[i] - xstart);
				}

				for (auto i = 0; i < len; i++)
				{
					xb[i] = reversed[i];
				}
			}

			//std::reverse(range_begin, pos);
		}
		else
		{
			pos += 1;
		}
	}
}


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

	owner_context(const double scale_factor_in) : scale_factor(scale_factor_in)
	{
		update_fonts();
	}

	void delete_brushes()
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
		delete_brushes();
		delete_fonts();
	}

	HFONT font(const ui::style::font_size f) const
	{
		switch (f)
		{
		case ui::style::font_size::code: return code;
		case ui::style::font_size::dialog: return dialog;
		case ui::style::font_size::title: return title;
		case ui::style::font_size::mega: return mega;
		case ui::style::font_size::icons: return icons;
		case ui::style::font_size::small_icons: return small_icons;
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
			cached_gdi_brushes[c] = result;
			return result;
		}

		return i->second;
	}

	int calc_base_font_size()
	{
		return df::round(scale_factor * global_base_font_size);
	}

	void update_fonts()
	{
		delete_fonts();

		const auto bds = calc_base_font_size();
		code = create_font(ui::style::font_size::code, bds);
		dialog = create_font(ui::style::font_size::dialog, bds);
		title = create_font(ui::style::font_size::title, bds);
		mega = create_font(ui::style::font_size::mega, bds);
		icons = create_font(ui::style::font_size::icons, bds);
		small_icons = create_font(ui::style::font_size::small_icons, bds);
	}
};

using owner_context_ptr = std::shared_ptr<owner_context>;

void draw_icon(HDC hdc, const owner_context_ptr& ctx, const icon_index icon, const recti bounds, const COLORREF clr)
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
				auto* const old_font = SelectObject(bm_hdc, font);
				const auto cx = extent.cx;
				const auto cy = extent.cy;

				BITMAPINFO bmi;
				memset(&bmi.bmiHeader, 0, sizeof(BITMAPINFOHEADER));
				bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
				bmi.bmiHeader.biWidth = cx;
				bmi.bmiHeader.biHeight = -static_cast<int>(cy);
				bmi.bmiHeader.biPlanes = 1;
				bmi.bmiHeader.biCompression = BI_RGB;
				bmi.bmiHeader.biBitCount = 32;

				uint8_t* dibBits = nullptr;
				const auto hdib = CreateDIBSection(bm_hdc, &bmi, DIB_RGB_COLORS, std::bit_cast<void**>(&dibBits),
				                                   nullptr, 0);

				if (hdib)
				{
					const auto hbm_old = SelectObject(bm_hdc, hdib);
					const auto src_stride = cx * 4;

					for (auto y = 0u; y < cy; y++)
					{
						memset(dibBits + (src_stride * y), 0, src_stride);
					}

					SetTextColor(bm_hdc, 0xffffff);
					SetBkMode(bm_hdc, TRANSPARENT);
					//SetTextAlign(bm_hdc, TA_TOP);

					const RECT bounds{0, 0, cx, cy};

					if (ExtTextOut(bm_hdc, 0, 0, 0, &bounds, sz, 1, nullptr))
					{
						const auto rr = ui::get_r(clr);
						const auto gg = ui::get_g(clr);
						const auto bb = ui::get_b(clr);

						for (auto yy = 0; yy < cy; yy++)
						{
							const auto line = std::bit_cast<uint32_t*>(dibBits + (src_stride * yy));

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
								line[xx] = ui::rgba((rr * a) / 255, (gg * a) / 255, (bb * a) / 255, (a * a) / 255);
							}
						}
					}

					const BLENDFUNCTION bf = {AC_SRC_OVER, 0, 0xFF, AC_SRC_ALPHA};
					AlphaBlend(hdc, x, y, cx, cy, bm_hdc, 0, 0, cx, cy, bf);

					//BitBlt(hdc, x, y, cx, cy, bm_hdc, 0, 0, SRCCOPY);

					SelectObject(bm_hdc, hbm_old);
					SelectObject(bm_hdc, old_font);
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

static void fill_solid_rect(HDC hdc, LPCRECT lpRect, COLORREF clr)
{
	const auto clr_old = SetBkColor(hdc, clr);

	if (clr_old != CLR_INVALID)
	{
		::ExtTextOut(hdc, 0, 0, ETO_OPAQUE, lpRect, nullptr, 0, nullptr);
		SetBkColor(hdc, clr_old);
	}
}

static void fill_solid_rect(HDC hdc, int x, int y, int cx, int cy, COLORREF clr)
{
	const RECT r = {x, y, x + cx, y + cy};
	fill_solid_rect(hdc, &r, clr);
}

static void frame_rect(HDC hdc, int x, int y, int cx, int cy, COLORREF clrTopLeft, COLORREF clrBottomRight = 0,
                       int width = 1)
{
	if (clrBottomRight == 0) clrBottomRight = clrTopLeft;

	fill_solid_rect(hdc, x, y, cx - width, width, clrTopLeft);
	fill_solid_rect(hdc, x, y, width, cy - width, clrTopLeft);
	fill_solid_rect(hdc, x + cx, y, -width, cy, clrBottomRight);
	fill_solid_rect(hdc, x, y + cy, cx, -width, clrBottomRight);
}


static void frame_rect(HDC hdc, LPCRECT lpRect, COLORREF clrTopLeft, COLORREF clrBottomRight = 0, int width = 1)
{
	frame_rect(hdc, lpRect->left, lpRect->top, lpRect->right - lpRect->left,
	           lpRect->bottom - lpRect->top, clrTopLeft, clrBottomRight);
}

static int toolbar_GetButtonInfo(HWND hwnd, int nID, LPTBBUTTONINFO lptbbi)
{
	df::assert_true(IsWindow(hwnd));
	return static_cast<int>(::SendMessage(hwnd, TB_GETBUTTONINFO, nID, (LPARAM)lptbbi));
}

static int toolbar_GetButtonCount(HWND hwnd)
{
	df::assert_true(IsWindow(hwnd));
	return static_cast<int>(::SendMessage(hwnd, TB_BUTTONCOUNT, 0, 0L));
}

BOOL toolbar_GetItemRect(HWND hwnd, int nIndex, LPRECT lpRect)
{
	df::assert_true(IsWindow(hwnd));
	return static_cast<BOOL>(::SendMessage(hwnd, TB_GETITEMRECT, nIndex, (LPARAM)lpRect));
}

uint32_t toolbar_CommandToIndex(HWND hwnd, uint32_t nID)
{
	df::assert_true(IsWindow(hwnd));
	return static_cast<uint32_t>(::SendMessage(hwnd, TB_COMMANDTOINDEX, nID, 0L));
}

static void erase_toolbar_seperators(HWND tb, HDC dc, COLORREF bg_clr)
{
	const int count = toolbar_GetButtonCount(tb);

	win_rect r;
	GetClientRect(tb, &r); //get window rect of control relative to screen				

	//fill_solid_rect(pCustomDraw->hdc, &rr, _background_clr);	
	auto* const clip = CreateRectRgn(0, 0, r.width(), r.height());


	for (int i = 0; i < count; ++i)
	{
		win_rect r;
		if (toolbar_GetItemRect(tb, i, r) && r.width() > 16)
		{
			auto* const rr = CreateRectRgn(r.left, r.top, r.right, r.bottom);
			CombineRgn(clip, clip, rr, RGN_XOR);
			DeleteObject(rr);
			//fill_solid_rect(dc, r, bg_clr);
		}
	}

	auto* const bg_brush = CreateSolidBrush(bg_clr);
	FillRgn(dc, clip, bg_brush);
	DeleteObject(bg_brush);
	DeleteObject(clip);
}

static void draw_toolbar_button(const ui::command_ptr& command, const owner_context_ptr& ctx,
                                LPNMTBCUSTOMDRAW lpTBCustomDraw, const COLORREF bg_clr, const COLORREF text_clr,
                                const COLORREF selected_clr)
{
	const win_rect button_rect = lpTBCustomDraw->nmcd.rc;
	auto* const tb = lpTBCustomDraw->nmcd.hdr.hwndFrom;
	auto* const hdc = lpTBCustomDraw->nmcd.hdc;

	const int cchText = 200;
	wchar_t szText[cchText] = {0};

	TBBUTTONINFO button_info = {0};
	button_info.cbSize = sizeof(TBBUTTONINFO);
	button_info.dwMask = TBIF_TEXT | TBIF_IMAGE | TBIF_STYLE | TBIF_STATE;
	button_info.pszText = szText;
	button_info.cchText = cchText;
	toolbar_GetButtonInfo(tb, static_cast<int>(lpTBCustomDraw->nmcd.dwItemSpec), &button_info);

	const uint32_t item_state = lpTBCustomDraw->nmcd.uItemState;
	const bool is_selected = (item_state & ODS_SELECTED) != 0;
	const bool is_hotlight = (item_state & ODS_HOTLIGHT) != 0;
	const bool is_focus = is_hotlight && GetFocus() == tb;
	const bool is_checked = (button_info.fsState & TBSTATE_CHECKED) != 0;
	const bool is_pressed = (button_info.fsState & TBSTATE_PRESSED) != 0;
	const bool is_disabled = (button_info.fsState & TBSTATE_ENABLED) == 0;
	const bool is_drop_down = (button_info.fsStyle & TBSTYLE_DROPDOWN) != 0;
	const bool is_drop_whole = (button_info.fsStyle & BTNS_WHOLEDROPDOWN) != 0;

	const auto clr_checked_bg = ui::darken(bg_clr, 0.22f);
	const auto clr_selected_bg = selected_clr;
	const auto clr_hover_bg = ui::lighten(bg_clr, 0.33f);
	auto draw_clr = text_clr;
	auto icon_bg_clr = bg_clr;

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
				fill_solid_rect(mem_dc, bounds, bg_clr);
				draw_clr = text_clr;
			}

			SIZE text_extent{0, 0};
			SIZE icon_extent{0, 0};

			const auto avail_width = bounds.width();
			const auto has_text = !str::is_empty(button_info.pszText);
			const auto text_len = str::len(button_info.pszText);
			const auto has_image = button_info.iImage != I_IMAGENONE;
			//const auto x_padding = has_text && has_image ? 2 : 0;

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

			//const int total_width = text_extent.cx + icon_extent.cx + x_padding;
			int x = bounds.left + (avail_width - pos_width) / 2;

			if (has_image)
			{
				const auto x_padding = 3;
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

static void draw_menu_item(const ui::command_ptr& command, LPDRAWITEMSTRUCT lpDrawItemStruct, owner_context_ptr ctx)
{
	const int padding = 2;

	const HDC dc = lpDrawItemStruct->hDC;
	const win_rect item_bounds = lpDrawItemStruct->rcItem;
	const auto menu_background = ui::style::color::menu_background;

	if (command == nullptr)
	{
		auto sep_bounds = item_bounds;
		sep_bounds.bottom = sep_bounds.top = (sep_bounds.bottom + sep_bounds.top) / 2;

		fill_solid_rect(dc, item_bounds, menu_background);
		fill_solid_rect(dc, sep_bounds.inflate(-8, 1), ui::average(menu_background, ui::style::color::view_background));
	}
	else
	{
		const auto is_disabled = lpDrawItemStruct->itemState & (ODS_GRAYED | ODS_DISABLED);
		const auto is_selected = lpDrawItemStruct->itemState & ODS_SELECTED;
		const auto is_checked = lpDrawItemStruct->itemState & ODS_CHECKED;
		//const auto menu_background = GetSysColor(COLOR_MENU); //render::style::color::menu_background;		
		const auto menu_color = command->clr;
		const auto icon = (is_checked && command->icon == icon_index::none) ? icon_index::check : command->icon;
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


static recti get_client_rect(HWND h)
{
	win_rect r;
	GetClientRect(h, r);
	return r;
}

recti desktop_bounds_impl(HWND hwnd, const bool work_area)
{
	auto* const monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);

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


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ComPtr<ID2D1Bitmap> load_bitmap_resource(const factories_ptr& f, ID2D1RenderTarget* rt, const uint32_t res_id)
{
	ComPtr<IWICBitmapFrameDecode> pFrame;
	ComPtr<IWICFormatConverter> pConverter;
	ComPtr<ID2D1Bitmap> pBitmap;
	ComPtr<IWICBitmapDecoder> pDecoder;
	ComPtr<IWICStream> pStream;

	HGLOBAL imageResDataHandle = nullptr;
	void* pImageFile = nullptr;
	DWORD imageFileSize = 0;

	// Locate the resource.
	auto* imageResHandle = FindResourceA(get_resource_instance, MAKEINTRESOURCEA(res_id), "PNG");
	HRESULT hr = imageResHandle ? S_OK : E_FAIL;

	if (SUCCEEDED(hr))
	{
		// Load the resource.
		imageResDataHandle = LoadResource(get_resource_instance, imageResHandle);
		hr = imageResDataHandle ? S_OK : E_FAIL;
	}

	if (SUCCEEDED(hr))
	{
		// Lock it to get a system memory pointer.
		pImageFile = LockResource(imageResDataHandle);
		hr = pImageFile ? S_OK : E_FAIL;
	}
	if (SUCCEEDED(hr))
	{
		// Calculate the size.
		imageFileSize = SizeofResource(get_resource_instance, imageResHandle);
		hr = imageFileSize ? S_OK : E_FAIL;
	}

	if (SUCCEEDED(hr))
	{
		// Create a WIC stream to map onto the memory.
		hr = f->wic->CreateStream(&pStream);
	}

	if (SUCCEEDED(hr))
	{
		// Initialize the stream with the memory pointer and size.
		hr = pStream->InitializeFromMemory(
			std::bit_cast<BYTE*>(pImageFile),
			imageFileSize
		);
	}

	if (SUCCEEDED(hr))
	{
		// Create a decoder for the stream.
		hr = f->wic->CreateDecoderFromStream(
			pStream.Get(),
			nullptr,
			WICDecodeMetadataCacheOnLoad,
			&pDecoder
		);
	}

	if (SUCCEEDED(hr))
	{
		hr = pDecoder->GetFrame(0, &pFrame);
	}

	if (SUCCEEDED(hr))
	{
		// Convert the image format to 32bppPBGRA
		// (DXGI_FORMAT_B8G8R8A8_UNORM + D2D1_ALPHA_MODE_PREMULTIPLIED).
		hr = f->wic->CreateFormatConverter(&pConverter);
	}

	if (SUCCEEDED(hr))
	{
		hr = pConverter->Initialize(
			pFrame.Get(),
			GUID_WICPixelFormat32bppPBGRA,
			WICBitmapDitherTypeNone,
			nullptr,
			0.0f,
			WICBitmapPaletteTypeMedianCut
		);
	}

	if (SUCCEEDED(hr))
	{
		hr = rt->CreateBitmapFromWicBitmap(
			pConverter.Get(),
			nullptr,
			&pBitmap
		);
	}

	return pBitmap;
}


static void draw_bitmap(ID2D1RenderTarget* rt, ID2D1Bitmap* b, const float dx, const float dy, const float dcx,
                        const float dcy, const float sx, const float sy, const float scx, const float scy,
                        const float alpha)
{
	const D2D1_RECT_F dst = {dx, dy, dx + dcx, dy + dcy};
	const D2D1_RECT_F src = {sx, sy, sx + scx, sy + scy};

	rt->DrawBitmap(b, dst, alpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src);
}

static void streach_background(ID2D1RenderTarget* rt, ID2D1Bitmap* b, const recti r, const int dst_width_in,
                               const float alpha)
{
	const D2D1_SIZE_F extent = b->GetSize();
	const auto dst_width = static_cast<float>(dst_width_in);
	const auto double_dst_width = dst_width * 2.0f;

	const auto src_width = extent.width * 3.0f / 7.0f;
	const auto w = extent.width - (src_width * 2.0f);
	const auto l = extent.height - src_width;
	const auto br = extent.width - src_width;

	const auto width = static_cast<float>(r.width());
	const auto height = static_cast<float>(r.height());
	const auto top = static_cast<float>(r.top);
	const auto left = static_cast<float>(r.left);
	const auto right = static_cast<float>(r.right);
	const auto bottom = static_cast<float>(r.bottom);

	draw_bitmap(rt, b, left + dst_width, top, width - double_dst_width, dst_width, src_width, 0, w, src_width, alpha);
	draw_bitmap(rt, b, left, top + dst_width, dst_width, height - double_dst_width, 0, src_width, src_width, w, alpha);
	draw_bitmap(rt, b, right - dst_width, top + dst_width, dst_width, height - double_dst_width, l, src_width,
	            src_width, w, alpha);
	draw_bitmap(rt, b, left + dst_width, bottom - dst_width, width - double_dst_width, dst_width, src_width, l, w,
	            src_width, alpha);
	draw_bitmap(rt, b, left, top, dst_width, dst_width, 0, 0, src_width, src_width, alpha);
	draw_bitmap(rt, b, right - dst_width, top, dst_width, dst_width, br, 0, src_width, src_width, alpha);
	draw_bitmap(rt, b, left, bottom - dst_width, dst_width, dst_width, 0, br, src_width, src_width, alpha);
	draw_bitmap(rt, b, right - dst_width, bottom - dst_width, dst_width, dst_width, br, br, src_width, src_width,
	            alpha);
}

class d2d_texture : public ui::texture
{
public:
	ComPtr<ID2D1RenderTarget> _rt;
	ComPtr<ID2D1Bitmap> _bm;
	std::unique_ptr<av_scaler> _scaler;
	factories_ptr _f;

	d2d_texture(const factories_ptr& f, ID2D1RenderTarget* rt) : _rt(rt), _f(f)
	{
	}

	~d2d_texture() override
	{
		_scaler.reset();
		_bm.Reset();
		_scaler.reset();
	}

	bool is_valid() const override
	{
		return _bm != nullptr;
	}

	ui::texture_update_result update(const sizei dimensions, const ui::texture_format format,
	                                 const ui::orientation orientation, const uint8_t* pixels, const int stride,
	                                 const int buffer_size) override
	{
		auto result = ui::texture_update_result::failed;

		/*if (_bm && _dimensions == dimensions && _format == format)
		{
			D2D1_RECT_U bounds = { 0, 0, _dimensions.cx, _dimensions.cy };
			_bm->CopyFromMemory(&bounds, pixels, stride);
			result = ui::texture_update_result::tex_updated;
		}
		else*/
		{
			_bm.Reset();

			auto pixel_format = GUID_WICPixelFormat32bppBGR;

			switch (format)
			{
			case ui::texture_format::RGB:
				pixel_format = GUID_WICPixelFormat32bppBGR;
				break;
			case ui::texture_format::ARGB:
				pixel_format = GUID_WICPixelFormat32bppBGRA;
				break;
			case ui::texture_format::None:
				break;
			case ui::texture_format::NV12:
				break;
			case ui::texture_format::P010:
				break;
			default: ;
			}

			ComPtr<IWICBitmapSource> wic_source;
			ComPtr<IWICBitmap> wic_bitmap;

			auto hr = _f->wic->CreateBitmapFromMemory(dimensions.cx, dimensions.cy,
			                                          pixel_format, stride, buffer_size, (uint8_t*)pixels, &wic_bitmap);

			if (SUCCEEDED(hr))
			{
				if (format == ui::texture_format::ARGB)
				{
					ComPtr<IWICFormatConverter> wic_converter;

					if (SUCCEEDED(hr))
					{
						hr = _f->wic->CreateFormatConverter(&wic_converter);
					}

					if (SUCCEEDED(hr))
					{
						hr = wic_converter->Initialize(
							wic_bitmap.Get(), // Input bitmap to convert
							GUID_WICPixelFormat32bppPBGRA, // Destination pixel format
							WICBitmapDitherTypeNone, // Specified dither pattern
							nullptr, // Specify a particular palette 
							0.f, // Alpha threshold
							WICBitmapPaletteTypeCustom // Palette translation type
						);

						if (SUCCEEDED(hr))
						{
							wic_source = wic_converter;
						}
					}
				}
				else
				{
					wic_source = wic_bitmap;
				}
			}

			if (SUCCEEDED(hr))
			{
				hr = _rt->CreateBitmapFromWicBitmap(wic_source.Get(), nullptr, &_bm);

				if (SUCCEEDED(hr))
				{
					_dimensions = dimensions;
					_format = format;
					result = ui::texture_update_result::tex_created;
				}
			}
		}

		_orientation = orientation;

		return result;
	}

	ui::texture_update_result update(const av_frame_ptr& frame) override
	{
		if (!_scaler) _scaler = std::make_unique<av_scaler>();

		auto surface = std::make_shared<ui::surface>();
		if (_scaler->scale_surface(frame, surface))
		{
			return update(surface);
		}

		return ui::texture_update_result::failed;
	}

	ui::texture_update_result update(const ui::const_surface_ptr& s) override
	{
		df::assert_true(ui::is_ui_thread());

		if (ui::is_valid(s))
		{
			return update(s->dimensions(), s->format(), s->orientation(), s->pixels(), s->stride(), s->size());
		}

		return ui::texture_update_result::failed;
	}
};

static D2D1_RECT_F make_rectf(const int left, const int top, const int right, const int bottom)
{
	return {static_cast<float>(left), static_cast<float>(top), static_cast<float>(right), static_cast<float>(bottom)};
}

static D2D1_RECT_F make_rectf(const recti bounds)
{
	return make_rectf(bounds.left, bounds.top, bounds.right, bounds.bottom);
}

class d2d_draw_context;

using texture_gdi_ptr = std::shared_ptr<d2d_texture>;
using d2d_draw_context_ptr = std::shared_ptr<d2d_draw_context>;

class d2d_vertices final : public std::enable_shared_from_this<d2d_vertices>, public ui::vertices
{
public:
	std::vector<recti> _rects;
	std::vector<ui::color> _colors;

	void update(recti rects[], ui::color colors[], int num_bars) override
	{
		_rects.assign(rects, rects + num_bars);
		_colors.assign(colors, colors + num_bars);
	}
};

class d2d_draw_context final : public draw_context_device, public std::enable_shared_from_this<d2d_draw_context>
{
public:
	ComPtr<ID2D1RenderTarget> _rt;
	ComPtr<ID2D1Bitmap> _shadow;
	ComPtr<ID2D1Bitmap> _inverse_shadow;
	recti _bounds;
	factories_ptr _f;
	int _base_font_size = normal_font_size;

	d2d_draw_context(const factories_ptr& f, ID2D1RenderTarget* rt, const int base_font_size) : _rt(rt), _f(f),
		_base_font_size(base_font_size)
	{
		df::assert_true(rt);
	}

	~d2d_draw_context() override = default;

	void clear(const ui::color c) override
	{
		_rt->Clear(D2D1::ColorF(c.r, c.g, c.b, c.a));
	}

	void draw_rounded_rect(const recti bounds, const ui::color c, const int radius) override
	{
		df::scope_rendering_func rf(__FUNCTION__);

		if (bounds.intersects(_bounds) && _rt)
		{
			ComPtr<ID2D1SolidColorBrush> brush;

			const auto hr = _rt->CreateSolidColorBrush(
				D2D1::ColorF(c.r, c.g, c.b, c.a),
				&brush
			);

			if (SUCCEEDED(hr))
			{
				D2D1_ROUNDED_RECT r;
				r.rect = make_rectf(bounds);
				r.radiusX = static_cast<float>(radius);
				r.radiusY = static_cast<float>(radius);
				_rt->FillRoundedRectangle(r, brush.Get());
			}
		}
	}

	void draw_rect(const recti bounds, const ui::color c) override
	{
		df::scope_rendering_func rf(__FUNCTION__);

		if (bounds.intersects(_bounds) && _rt)
		{
			ComPtr<ID2D1SolidColorBrush> brush;

			const auto hr = _rt->CreateSolidColorBrush(
				D2D1::ColorF(c.r, c.g, c.b, c.a),
				&brush
			);

			if (SUCCEEDED(hr))
			{
				_rt->FillRectangle(make_rectf(bounds), brush.Get());
			}
		}
	}

	void draw_text(const std::u8string_view text, const recti bounds, const ui::style::font_size font,
	               const ui::style::text_style style, const ui::color c, const ui::color bg) override
	{
		df::scope_rendering_func rf(__FUNCTION__);

		if (_bounds.intersects(bounds))
		{
			const auto fr = _f->font_face(font, _base_font_size);

			if (fr)
			{
				fr->draw(this, _rt.Get(), str::utf8_to_utf16(text), bounds, style, c, bg, {});
			}
		}
	}

	void draw_text(const std::u8string_view text, const std::vector<ui::text_highlight_t>& highlights,
	               const recti bounds, ui::style::font_size font, ui::style::text_style style, const ui::color clr,
	               const ui::color bg) override
	{
		df::scope_rendering_func rf(__FUNCTION__);

		if (_bounds.intersects(bounds))
		{
			const auto w_highlights = highlights;

			std::wstring w;
			w.reserve(text.size());

			auto i = text.begin();
			while (i < text.end())
			{
				w += static_cast<wchar_t>(str::pop_utf8_char(i, text.end()));
			}

			const auto fr = _f->font_face(font, _base_font_size);

			if (fr)
			{
				fr->draw(this, _rt.Get(), w, bounds, style, clr, bg, w_highlights);
			}
		}
	}

	void draw_text(const ui::text_layout_ptr& tl, const recti bounds, const ui::color clr, const ui::color bg) override
	{
		df::scope_rendering_func rf(__FUNCTION__);
		df::assert_true(ui::is_ui_thread());

		if (_bounds.intersects(bounds))
		{
			const auto t = std::dynamic_pointer_cast<text_layout_impl>(tl);
			const auto fr = _f->font_face(t->_font, _base_font_size);

			if (fr)
			{
				fr->draw(this, _rt.Get(), t->_layout.Get(), bounds, clr, bg);
			}
		}
	}

	void draw_shadow(const recti bounds, const int width, const float alpha, const bool inverse) override
	{
		df::scope_rendering_func rf(__FUNCTION__);

		if (!_shadow)
		{
			_shadow = load_bitmap_resource(_f, _rt.Get(), IDB_SHADOW);
		}

		if (!_inverse_shadow)
		{
			_inverse_shadow = load_bitmap_resource(_f, _rt.Get(), IDB_INVERSE_SHADOW);
		}

		if (_shadow && _inverse_shadow)
		{
			streach_background(_rt.Get(), inverse ? _inverse_shadow.Get() : _shadow.Get(), bounds.inflate(32), 32,
			                   alpha);
		}
	}

	void draw_border(const recti inside, const recti outside, const ui::color c_inside,
	                 const ui::color c_outside) override
	{
		df::scope_rendering_func rf(__FUNCTION__);

		if (outside.intersects(_bounds) && _rt)
		{
			ComPtr<ID2D1SolidColorBrush> brush;

			const auto hr = _rt->CreateSolidColorBrush(
				D2D1::ColorF(c_outside.r, c_outside.g, c_outside.b, c_outside.a),
				&brush
			);

			if (SUCCEEDED(hr))
			{
				const auto r1 = make_rectf(outside.left, outside.top, inside.left, outside.bottom);
				const auto r2 = make_rectf(inside.right, outside.top, outside.right, outside.bottom);
				const auto r3 = make_rectf(inside.left, outside.top, inside.right, inside.top);
				const auto r4 = make_rectf(inside.left, inside.bottom, inside.right, outside.bottom);

				_rt->FillRectangle(r1, brush.Get());
				_rt->FillRectangle(r2, brush.Get());
				_rt->FillRectangle(r3, brush.Get());
				_rt->FillRectangle(r4, brush.Get());
			}
		}
	}

	void draw_texture(const ui::texture_ptr& t, const recti dst, const float alpha,
	                  const ui::texture_sampler sampler) override
	{
		df::scope_rendering_func rf(__FUNCTION__);
		draw_texture(t, dst, recti(pointi(0, 0), t->dimensions()), alpha, sampler);
	}

	void draw_texture_impl(const texture_gdi_ptr& t, const recti dst, const recti src, const float alpha,
	                       ui::texture_sampler sampler, float radius)
	{
		df::scope_rendering_func rf(__FUNCTION__);

		if (dst.intersects(_bounds) && _rt && t->_bm)
		{
			const auto dd = make_rectf(dst);
			const auto ss = make_rectf(src);
			const D2D1_BITMAP_INTERPOLATION_MODE mode = sampler == ui::texture_sampler::point
				                                            ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
				                                            : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR;

			_rt->DrawBitmap(t->_bm.Get(), &dd, alpha, mode, &ss);
		}
	}

	void draw_texture_impl(const texture_gdi_ptr& t, const quadd& qdst, const recti src, const float alpha,
	                       ui::texture_sampler sampler)
	{
		df::scope_rendering_func rf(__FUNCTION__);

		const auto dst = qdst.bounding_rect();

		if (dst.intersects(_bounds) && _rt && t->_bm)
		{
			const auto dd = make_rectf(dst.round());
			const auto ss = make_rectf(src);
			const D2D1_BITMAP_INTERPOLATION_MODE mode = sampler == ui::texture_sampler::point
				                                            ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
				                                            : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR;

			_rt->DrawBitmap(t->_bm.Get(), &dd, alpha, mode, &ss);
		}
	}

	void draw_texture(const ui::texture_ptr& t, const quadd& dst, const recti src, const float alpha,
	                  const ui::texture_sampler sampler) override
	{
		const auto tt = std::dynamic_pointer_cast<d2d_texture>(t);
		draw_texture_impl(tt, dst, src, alpha, sampler);
	}

	void draw_texture(const ui::texture_ptr& t, const recti dst, const recti src, const float alpha,
	                  const ui::texture_sampler sampler, const float radius) override
	{
		const auto tt = std::dynamic_pointer_cast<d2d_texture>(t);
		draw_texture_impl(tt, dst, src, alpha, sampler, radius);
	}

	sizei measure_text(const std::u8string_view text, const ui::style::font_size font,
	                   const ui::style::text_style style, const int width, const int height) override
	{
		df::scope_rendering_func rf(__FUNCTION__);
		const auto fr = _f->font_face(font, _base_font_size);

		if (fr)
		{
			return fr->measure(str::utf8_to_utf16(text), style, width, height);
		}

		return {};
	}

	int text_line_height(const ui::style::font_size font) override
	{
		df::scope_rendering_func rf(__FUNCTION__);
		const auto fr = _f->font_face(font, _base_font_size);

		if (fr)
		{
			return fr->calc_line_height();
		}

		return 0;
	}

	ui::texture_ptr create_texture() override
	{
		df::scope_rendering_func rf(__FUNCTION__);
		df::assert_true(ui::is_ui_thread());
		return std::make_shared<d2d_texture>(_f, _rt.Get());
	}

	ui::text_layout_ptr create_text_layout(const ui::style::font_size font) override
	{
		df::scope_rendering_func rf(__FUNCTION__);
		df::assert_true(ui::is_ui_thread());
		const auto fr = _f->font_face(font, _base_font_size);

		if (fr)
		{
			return std::make_shared<text_layout_impl>(fr, font);
		}

		return nullptr;
	}

	void draw_vertices(const ui::vertices_ptr& v) override
	{
		df::scope_rendering_func rf(__FUNCTION__);
		const auto vv = std::dynamic_pointer_cast<d2d_vertices>(v);

		for (auto i = 0u; i < vv->_rects.size(); i++)
		{
			const auto bounds = vv->_rects[i];
			const auto c2 = vv->_colors[i];
			const auto c1 = c2.emphasize();

			if (bounds.intersects(_bounds) && _rt)
			{
				ComPtr<ID2D1SolidColorBrush> brush;

				const auto hr = _rt->CreateSolidColorBrush(
					D2D1::ColorF(c1.r, c1.g, c1.b, c1.a),
					&brush
				);

				if (SUCCEEDED(hr))
				{
					const auto r = make_rectf(bounds);
					_rt->FillRectangle(r, brush.Get());
				}
			}
		}
	}

	ui::vertices_ptr create_vertices() override
	{
		df::scope_rendering_func rf(__FUNCTION__);
		return std::make_shared<d2d_vertices>();
	}

	recti clip_bounds() const override
	{
		return _bounds;
	}

	void clip_bounds(const recti r) override
	{
		df::scope_rendering_func rf(__FUNCTION__);

		if (_rt)
		{
			_rt->PushAxisAlignedClip(
				make_rectf(r),
				D2D1_ANTIALIAS_MODE_PER_PRIMITIVE
			);
		}
	}

	void restore_clip() override
	{
		df::scope_rendering_func rf(__FUNCTION__);

		if (_rt)
		{
			_rt->PopAxisAlignedClip();
		}
	}


	void draw_edge_shadows(const float alpha) override
	{
		df::scope_rendering_func rf(__FUNCTION__);

		if (!_inverse_shadow)
		{
			_inverse_shadow = load_bitmap_resource(_f, _rt.Get(), IDB_INVERSE_SHADOW);
		}

		if (_inverse_shadow)
		{
			streach_background(_rt.Get(), _inverse_shadow.Get(), _bounds, 32, alpha);
		}
	}

	bool is_valid() const override
	{
		return _rt != nullptr;
	}

	void begin_draw(const sizei client_extent, const int base_font_size) override
	{
		df::scope_rendering_func rf(__FUNCTION__);
		_bounds.set(0, 0, client_extent.cx, client_extent.cy);

		_rt->BeginDraw();
		_rt->SetTransform(D2D1::Matrix3x2F::Identity());
	}

	void render() override
	{
		df::scope_rendering_func rf(__FUNCTION__);
		_rt->EndDraw();
	}

	void resize(const sizei client_extent) override
	{
		_bounds.set(0, 0, client_extent.cx, client_extent.cy);
	}

	static ui::color32 hsv_to_rgb(const double h, const double s, const double v)
	{
		// From medit
		double f, p, q, t, hh;
		int i;
		// shift the hue to the range [0, 360] before performing calculations
		hh = ((360 + (static_cast<int>(h) % 360)) % 360) / 60.;
		i = static_cast<int>(std::floor(hh)); /* largest int <= h     */
		f = hh - i; /* fractional part of h */
		p = v * (1.0 - s);
		q = v * (1.0 - (s * f));
		t = v * (1.0 - (s * (1.0 - f)));

		double r, g, b;

		switch (i)
		{
		case 0: r = v;
			g = t;
			b = p;
			break;
		case 1: r = q;
			g = v;
			b = p;
			break;
		case 2: r = p;
			g = v;
			b = t;
			break;
		case 3: r = p;
			g = q;
			b = v;
			break;
		case 4: r = t;
			g = p;
			b = v;
			break;
		case 5: r = v;
			g = p;
			b = q;
			break;
		}

		return ui::saturate_rgba(r, g, b, 1.0);
	}

	void destroy() override
	{
		df::scope_rendering_func rf(__FUNCTION__);
		_rt.Reset();
		_shadow.Reset();
		_inverse_shadow.Reset();
	}

	void update_font_size(int base_font_size) override
	{
		df::scope_rendering_func rf(__FUNCTION__);
		_base_font_size = base_font_size;
	}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class lock_window_update : public df::no_copy
{
public:
	lock_window_update(HWND wnd)
	{
		LockWindowUpdate(wnd);
	}

	~lock_window_update()
	{
		LockWindowUpdate(nullptr);
	}
};

struct win_base
{
	HWND m_hWnd = nullptr;
};

class win_impl : public win_base
{
public:
	virtual ~win_impl()
	{
		if (IsWindow(m_hWnd))
		{
			df::log(__FUNCTION__, u8"Destroying win_base of valid window");
			SetWindowLongPtr(m_hWnd, GWLP_USERDATA, 0);
		}
	}

	virtual LRESULT on_window_message(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}

	static LRESULT CALLBACK stProcessWindowMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		if (uMsg == WM_NCCREATE)
		{
			const auto pt = std::bit_cast<win_impl*>(std::bit_cast<LPCREATESTRUCT>(lParam)->lpCreateParams);
			const auto ptr = std::bit_cast<LONG_PTR>(std::bit_cast<LPCREATESTRUCT>(lParam)->lpCreateParams);
			// get the pointer to the window from lpCreateParams which was set in CreateWindow
			SetWindowLongPtr(hwnd, GWLP_USERDATA, ptr);

			if (pt)
			{
				pt->m_hWnd = hwnd;
			}
		}

		// get the pointer to the window
		const auto ptr = std::bit_cast<win_impl*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

		if (ptr)
		{
			return ptr->on_window_message(hwnd, uMsg, wParam, lParam);
		}
		return DefWindowProc(hwnd, uMsg, wParam, lParam);
	}

	bool register_class(UINT style, HICON hIcon, HCURSOR hCursor, HBRUSH hbrBackground,
	                    LPCWSTR lpszMenuName, LPCWSTR lpszClassName, HICON hIconSm)
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

	void enable(bool enable) override { EnableWindow(hwnd(), enable); }
	std::u8string window_text() const override { return ::window_text(hwnd()); }

	void window_text(const std::u8string_view text) override
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
			&& IsIconic(wnd) != 0;
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
		SetFont(hwnd(), t->_ctx->dialog);
	}

	void show(bool show) override { ShowWindow(hwnd(), show ? SW_SHOW : SW_HIDE); };

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
	//HWND _h = nullptr;
	HCURSOR _cursor = resources.normal;

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

	void enable(bool enable) override
	{
		EnableWindow(hwnd(), enable);
	}

	std::u8string window_text() const override
	{
		return ::window_text(hwnd());
	}

	void window_text(const std::u8string_view text) override
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

	void window_bounds(const recti bounds, bool visible) override
	{
		SetWindowPos(hwnd(), nullptr, bounds.left, bounds.top, bounds.width(), bounds.height(),
		             SWP_NOACTIVATE | (visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
	}

	void show(bool show) override
	{
		ShowWindow(hwnd(), show ? SW_SHOWNOACTIVATE : SW_HIDE);
	}

	void redraw() override
	{
		InvalidateRect(hwnd(), nullptr, 0);
		//::UpdateWindow(hwnd());
		//::RedrawWindow(hwnd(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
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
		return IsWindowEnabled(hwnd()) != 0;
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
		SetFont(hwnd(), t->_gdi_ctx->dialog);
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
};

class edit_string_enum : public IEnumString
{
public:
	std::vector<std::wstring> _data;
	std::vector<std::wstring>::const_iterator _walk;

	edit_string_enum() = default;

	edit_string_enum(const std::vector<std::wstring>& data) : _data(data)
	{
		_walk = _data.begin();
	}

	void load(const std::vector<std::u8string>& data)
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
		if (IsEqualGUID(iid, IID_IEnumString))
		{
			*ppvObject = static_cast<IEnumString*>(this);
			return S_OK;
		}

		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return 1;
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		return 1;
	}

	HRESULT STDMETHODCALLTYPE Next(ULONG celt, LPOLESTR* rgelt, ULONG* pceltFetched) override
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
		while (celt > 0)
		{
			if (_walk->empty()) return S_FALSE;
			--celt;
			++_walk;
		}
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE Reset() override
	{
		_walk = _data.begin();
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE Clone(IEnumString** ppenum) override
	{
		df::assert_true(false); // memory leak?
		*ppenum = new edit_string_enum(_data);
		return S_OK;
	}
};

class edit_impl final :
	public control_base_impl<edit_impl, ui::edit, win_base>,
	public control_base2,
	public std::enable_shared_from_this<edit_impl>
{
private:
	using base_class = control_base_impl<edit_impl, edit, win_base>;

	struct unknown_word
	{
		std::u8string word;
		int pos_start = 0;
		int pos_end = 0;

		recti calc_bounds(const edit_impl& edit) const
		{
			const POINT loc_start = edit.pos_from_char(pos_start);
			const POINT loc_end = edit.pos_from_char(pos_end);
			if (loc_end.x == -1 || loc_start.x == -1) return {};

			const int line_height = gdi_text_line_height(edit.m_hWnd, GetFont(edit.m_hWnd));
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
	std::unordered_map<uint32_t, HBITMAP> _round_rec_skins;

protected:
	void add_unknown_word(std::u8string_view word_a, int word_start, int word_end);
	void update_spelling(const std::wstring& text);
	void highlight_spelling();

public:
	std::function<void(const std::u8string&)> changed;
	edit_string_enum string_enum;
	owner_context_ptr _ctx;

	explicit edit_impl(ui::edit_styles styles, control_host_impl* parent, owner_context_ptr ctx)
		: _styles(std::move(styles)), _parent(parent), _ctx(ctx)
	{
	}

	~edit_impl() override
	{
		for (const auto& b : _round_rec_skins)
		{
			DeleteObject(b.second);
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
		SetFont(m_hWnd, _ctx->dialog);
	}

	static LRESULT CALLBACK SuperProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass,
	                                  DWORD_PTR dwRefData)
	{
		const auto pt = std::bit_cast<edit_impl*>(dwRefData);

		if (uMsg == WM_NCCALCSIZE) return pt->on_window_nc_calc_size(uMsg, wParam, lParam);
		if (uMsg == WM_NCPAINT) return pt->on_window_nc_paint(uMsg, wParam, lParam);
		if (uMsg == WM_PAINT) return pt->on_window_paint(uMsg, wParam, lParam);
		if (uMsg == WM_CONTEXTMENU) return pt->on_window_context_menu(uMsg, wParam, lParam);
		if (uMsg == WM_GETDLGCODE) return pt->on_window_get_dlg_code(uMsg, wParam, lParam);
		if (uMsg == WM_IME_NOTIFY) return pt->on_window_ime_notify(uMsg, wParam, lParam);
		if (uMsg == WM_KEYDOWN) return pt->on_window_key_down(uMsg, wParam, lParam);
		if (uMsg == WM_ENABLE) return pt->on_window_enable(uMsg, wParam, lParam);
		if (uMsg == WM_MOUSEWHEEL) return pt->on_mouse_wheel(uMsg, wParam, lParam);

		// return DefWindowProc(hWnd, uMsg, wParam, lParam);
		return DefSubclassProc(hWnd, uMsg, wParam, lParam);
	}

	HWND Create(HWND hWndParent, const std::u8string_view text, const uintptr_t id)
	{
		auto style = WS_CHILD | WS_TABSTOP;
		if (_styles.xES_AUTOHSCROLL) style |= ES_AUTOHSCROLL;
		if (_styles.xES_CENTER) style |= ES_CENTER;
		if (_styles.xES_NUMBER) style |= ES_NUMBER;
		if (_styles.xES_PASSWORD) style |= ES_PASSWORD;
		if (_styles.xES_AUTOVSCROLL) style |= ES_AUTOVSCROLL;
		if (_styles.xES_MULTILINE) style |= ES_MULTILINE;
		if (_styles.xES_WANTRETURN) style |= ES_WANTRETURN;

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

		return result;
	}

	POINT pos_from_char(uint32_t nChar) const
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

	LRESULT on_mouse_wheel(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam)
	{
		SendMessage(GetParent(m_hWnd), uMsg, wParam, lParam);
		return 1;
	}

	LRESULT on_window_get_dlg_code(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam)
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
						if (_styles.xES_WANTRETURN)
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

	LRESULT on_window_key_down(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam)
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

	LRESULT on_window_nc_calc_size(uint32_t /*uMsg*/, WPARAM wParam, LPARAM lParam)
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

	void auto_completes(const std::vector<std::u8string>& texts) override
	{
		string_enum.load(texts);
	}

	bool init_auto_complete_list();

	void limit_text_len(const int max_len) override
	{
		::SendMessage(m_hWnd, EM_SETLIMITTEXT, max_len, 0L);
	}

	void replace_sel(const std::u8string_view new_text, const bool add_space_if_append) override
	{
		const auto new_text_w = str::utf8_to_utf16(new_text);
		const auto sel_res = SendMessage(m_hWnd, EM_GETSEL, 0, 0L);
		const auto is_no_selection = HIWORD(sel_res) == LOWORD(sel_res);

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

	void window_text(const std::u8string_view text) override
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
			full_invalidate();
		}
	}

	void options_changed() override
	{
		SetFont(m_hWnd, _ctx->dialog);
		full_invalidate();
	}

	void full_invalidate()
	{
		RedrawWindow(m_hWnd, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
	}

	void set_background(const ui::color32 bg) override
	{
		if (_background != bg)
		{
			_background = bg;
			full_invalidate(); // invalidate non client area
			//Invalidate(FALSE);
		}
	}

	void enable(bool enable) override
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

	LRESULT on_window_ime_notify(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam)
	{
		const LRESULT l = DefSubclassProc(m_hWnd, uMsg, wParam, lParam);

		if (IMN_SETCOMPOSITIONWINDOW == wParam)
		{
			// TODO notify parent of move
		}

		return l;
	}
};


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool is_word_break(wchar_t c)
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
	const auto wsfront = std::ranges::find_if_not(s, [](int c) { return std::iswspace(c); });
	const auto wsback = std::find_if_not(s.rbegin(), s.rend(), [](int c) { return std::iswspace(c); }).base();
	return (wsback <= wsfront ? std::wstring() : std::wstring(wsfront, wsback));
}

void edit_impl::add_unknown_word(const std::u8string_view word, int word_start, int word_end)
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
				// draw_error(word, word_start, dc);
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

void edit_impl::highlight_spelling()
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

				for (const auto& word : _unknown_words)
				{
					const auto bounds = word.calc_bounds(*this);

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


void edit_impl::on_window_nc_calc_size(LPRECT pr) const
{
	df::assert_true(pr);

	const int line_height = gdi_text_line_height(m_hWnd, GetFont(m_hWnd));
	const bool is_multi_line = _styles.xES_MULTILINE;

	const int padding = _styles.rounded_corners ? 8 : 4;
	const int h = pr->bottom - pr->top;
	const int cx = padding;
	const int cy = is_multi_line ? padding : (h - line_height) / 2;

	pr->left += cx + (_icon == icon_index::none ? 0 : 22);
	pr->right -= cx;
	pr->top += cy;
	pr->bottom -= cy;
}

LRESULT edit_impl::on_window_nc_paint(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam)
{
	const auto hdc = GetWindowDC(m_hWnd); // (HRGN)wParam, DCX_WINDOW | DCX_INTERSECTRGN);
	//if (dc.IsNull()) dc = ::GetWindowDC(m_hWnd);

	if (hdc)
	{
		const bool has_focus = GetFocus() == m_hWnd;
		const auto edit_colors = calc_colors();
		const auto edge_clr = has_focus ? ui::style::color::dialog_selected_background : _styles.bg_clr;
		const auto bg_clr = _styles.bg_clr;

		win_rect r;
		GetWindowRect(m_hWnd, r);
		const win_rect outside(0, 0, r.width(), r.height());
		win_rect inside = outside;
		on_window_nc_calc_size(inside);

		auto* const clip_rgn = CreateRectRgn(outside.left, outside.top, outside.right, outside.bottom);
		auto* const exclude_rgn = CreateRectRgn(inside.left, inside.top, inside.right, inside.bottom);
		CombineRgn(clip_rgn, clip_rgn, exclude_rgn, RGN_XOR);
		SelectClipRgn(hdc, clip_rgn);

		if (_styles.rounded_corners)
		{
			// draw_round_rect_impl(hdc, outside, edit_colors.background, edge_clr, bg_clr, true, 6);
			// static void draw_round_rect_impl(HDC dc, const recti rr, COLORREF fill_clr, COLORREF edge_clr, COLORREF bg_clr, bool fillCenter, const int radius)
			const auto fill_clr = edit_colors.background;
			const auto round_rect_key = crypto::hash_gen().append(fill_clr).append(edge_clr).append(bg_clr).result();
			const auto found_round_rect = _round_rec_skins.find(round_rect_key);
			HBITMAP round_rec_skin = nullptr;

			if (found_round_rect == _round_rec_skins.end())
			{
				round_rec_skin = _round_rec_skins[round_rect_key] = create_round_rect(
					hdc, fill_clr, edge_clr, bg_clr, 6 * _ctx->scale_factor);
			}
			else
			{
				round_rec_skin = found_round_rect->second;
			}

			const HDC mem_dc = CreateCompatibleDC(hdc);

			if (mem_dc)
			{
				auto* const old_bitmap = SelectObject(mem_dc, round_rec_skin);
				streach_box(hdc, mem_dc, outside, 8, 32, true);
				SelectObject(mem_dc, old_bitmap);
				DeleteDC(mem_dc);
			}
		}
		else if (has_focus)
		{
			const int padding = 2;

			// Verticals
			fill_rect(hdc, edge_clr, recti(outside.left, outside.top, inside.left + padding, outside.bottom));
			fill_rect(hdc, edge_clr, recti(outside.right - padding, outside.top, outside.right, outside.bottom));

			// Horizontals
			fill_rect(hdc, edge_clr,
			          recti(outside.left + padding, outside.top, outside.right - padding, outside.top + padding));
			fill_rect(hdc, edge_clr, recti(outside.left + padding, outside.bottom - padding, outside.right - padding,
			                               outside.bottom));

			fill_rect(hdc, edit_colors.background, outside.inflate(-padding));
		}
		else
		{
			fill_rect(hdc, edit_colors.background, outside);
		}

		DeleteObject(clip_rgn);
		DeleteObject(exclude_rgn);

		if (_icon != icon_index::none)
		{
			const auto icon_cxy = calc_icon_cxy(_ctx->scale_factor);
			auto rr = outside;
			rr.left = 8;
			rr.right = rr.left + icon_cxy;
			draw_icon(hdc, _ctx, _icon, rr, ui::style::color::edit_text);
		}

		ReleaseDC(m_hWnd, hdc);
	}

	//DefWindowProc(m_hWnd, uMsg, wParam, lParam);
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

	static constexpr auto button_padding = 8;

	button_impl(owner_context_ptr ctx): _ctx(ctx)
	{
	}

	HWND Create(HWND hWndParent, LPCTSTR szWindowName = nullptr,
	            DWORD dwStyle = 0, DWORD dwExStyle = 0,
	            uintptr_t id = 0U, LPVOID lpCreateParam = nullptr)
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
		SetFont(m_hWnd, _ctx->dialog);
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

	void SetCheck(int nCheck)
	{
		df::assert_true(IsWindow(m_hWnd));
		::SendMessage(m_hWnd, BM_SETCHECK, nCheck, 0L);
	}

	void draw(LPNMCUSTOMDRAW pCustomDraw) const
	{
		wchar_t text[512];
		::GetWindowText(pCustomDraw->hdr.hwndFrom, text, 512);

		win_rect button_bounds(pCustomDraw->rc);
		auto client_bounds = button_bounds.inflate(-button_padding, -button_padding);

		const auto button_style = GetButtonStyle();
		const auto button_state = pCustomDraw->uItemState;
		const auto is_selected = (button_state & CDIS_SELECTED) != 0;
		const auto is_hotlight = (button_state & CDIS_HOT) != 0;
		const auto is_checked = (button_state & CDIS_CHECKED) != 0;
		const auto is_focused = (button_state & CDIS_FOCUS) != 0;
		const auto is_disabled = (button_state & CDIS_DISABLED) != 0;
		const auto is_default = ((button_style & BS_TYPEMASK) == BS_DEFPUSHBUTTON) != 0;
		const auto is_radio_button = (button_style & BS_TYPEMASK) == BS_AUTORADIOBUTTON;
		const auto is_check_box = (button_style & BS_TYPEMASK) == BS_AUTOCHECKBOX;

		auto* const dc = pCustomDraw->hdc;
		auto clr_bg = ui::style::color::button_background;
		auto clr_text = ui::style::color::dialog_text;

		if (is_radio_button)
		{
			fill_solid_rect(dc, button_bounds,
			                is_focused
				                ? ui::style::color::dialog_selected_background
				                : ui::style::color::dialog_background);

			const auto is_checked = GetCheck() != 0;
			const sizei size(16, 16);
			const recti icon_bounds(pointi(button_bounds.left + button_padding,
			                               ((button_bounds.top + button_bounds.bottom) / 2) - (size.cy / 2)), size);

			if (is_checked)
			{
				fill_solid_rect(dc, win_rect(icon_bounds), ui::style::color::dialog_selected_background);
			}

			draw_icon(dc, _ctx, is_checked ? icon_index::radio_on : icon_index::radio_off, icon_bounds, clr_text);
			client_bounds.left += size.cx + button_padding;
		}
		else if (is_check_box)
		{
			fill_solid_rect(dc, button_bounds,
			                is_focused
				                ? ui::style::color::dialog_selected_background
				                : ui::style::color::dialog_background);

			const auto is_checked = GetCheck() != 0;
			const sizei size(16, 16);
			const recti icon_bounds(pointi(button_bounds.left + button_padding,
			                               ((button_bounds.top + button_bounds.bottom) / 2) - (size.cy / 2)), size);

			if (is_checked)
			{
				fill_solid_rect(dc, win_rect(icon_bounds), ui::style::color::dialog_selected_background);
			}

			draw_icon(dc, _ctx, is_checked ? icon_index::checkbox_on : icon_index::checkbox_off, icon_bounds, clr_text);
			client_bounds.left += size.cx + button_padding;
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
				r.right = r.left + 40;
				draw_icon(dc, _ctx, _icon, r, clr_text);
				client_bounds.left += 40;
			}

			if (is_radio_button || is_check_box)
			{
				const auto style = DT_WORDBREAK;
				auto r = client_bounds;
				DrawText(dc, text, -1, r, style | DT_CALCRECT);
				const auto yy = (client_bounds.height() - r.height()) / 2;
				DrawText(dc, text, -1, r.offset(0, yy), style);
			}
			else
			{
				const auto style = DT_WORDBREAK | DT_CENTER;
				auto r = client_bounds;
				DrawText(dc, text, -1, r, style | DT_CALCRECT);
				const auto yy = (client_bounds.height() - r.height()) / 2;
				const auto xx = (client_bounds.width() - r.width()) / 2;
				DrawText(dc, text, -1, r.offset(xx, yy), style);
			}
		}
		else
		{
			const auto icon_cxy = calc_icon_cxy(_ctx->scale_factor);
			auto title_bounds = client_bounds;
			auto details_bounds = client_bounds;
			const auto icon_width = _icon == icon_index::none ? 0 : (icon_cxy + button_padding);

			SelectObject(dc, _ctx->title);
			DrawText(dc, text, -1, title_bounds, DT_CALCRECT);

			title_bounds = title_bounds.offset((client_bounds.width() - (title_bounds.width() + icon_width)) / 2, 0);
			details_bounds.top = title_bounds.bottom + button_padding;

			if (_icon != icon_index::none)
			{
				auto r = title_bounds;
				r.right = r.left + icon_cxy;
				r.top += 3;
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

		auto cx_result = button_padding * 2;
		auto cy_result = button_padding * 2;

		const auto icon_cxy = calc_icon_cxy(_ctx->scale_factor);
		auto icon_width = 0;
		if (_icon != icon_index::none) icon_width += icon_cxy + 8;
		if (is_radio_button || is_check_box) icon_width += 16 + button_padding;

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

	sizei measure(int cx) const override
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
	trackbar_impl(std::function<void(int, bool)> changed, owner_context_ptr ctx) : _changed(std::move(changed)),
		_ctx(ctx)
	{
	}

	HWND Create(HWND hWndParent, win_rect rect = {}, LPCTSTR szWindowName = nullptr,
	            DWORD dwStyle = 0, DWORD dwExStyle = 0,
	            uintptr_t id = 0U)
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

		return m_hWnd;
	}

	void destroy() override
	{
		DestroyWindow(m_hWnd);
	}

	void set_range(int nMin, int nMax, BOOL bRedraw = TRUE)
	{
		df::assert_true(IsWindow(m_hWnd));
		::SendMessage(m_hWnd, TBM_SETRANGE, bRedraw, MAKELPARAM(nMin, nMax));
	}

	int get_pos() const override
	{
		return static_cast<int>(::SendMessage(m_hWnd, TBM_GETPOS, 0, 0L));
	}

	void SetPos(int val) override
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
		SetFont(m_hWnd, _ctx->dialog);
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
	const int _cx_min = 160;
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

	LRESULT on_changed(const UINT_PTR idCtrl, const LPNMHDR pnmh)
	{
		SYSTEMTIME std, stt;
		DateTime_GetSystemtime(_date_control, &std);

		if (_include_time)
		{
			DateTime_GetSystemtime(_time_control, &stt);

			std.wHour = stt.wHour;
			std.wMinute = stt.wMinute;
			std.wSecond = stt.wSecond;
			std.wMilliseconds = stt.wMilliseconds;
		}

		FILETIME ft;
		SystemTimeToFileTime(&std, &ft);

		if (_changed)
		{
			_changed(df::date_t(ft_to_ts(ft)));
		}

		return 0;
	}

	LRESULT on_focus(const UINT_PTR idCtrl, const LPNMHDR pnmh)
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

		//CreateWindow(DATETIMEPICK_CLASS, m_hWnd, nullptr, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_SHORTDATEFORMAT, 0, date_id);

		SetFont(_date_control, font);
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

			//CreateWindow(DATETIMEPICK_CLASS, m_hWnd, nullptr, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | DTS_TIMEFORMAT, 0, time_id);
			SetFont(_time_control, font);
			DateTime_SetSystemtime(_time_control, _val.is_valid() ? GDT_VALID : GDT_NONE, &st);
		}

		return 0;
	}

	LRESULT on_window_paint(uint32_t /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/)
	{
		PAINTSTRUCT ps;
		auto hdc = BeginPaint(m_hWnd, &ps);
		//FillRect(hdc, &ps.rcPaint, (HBRUSH)(COLOR_WINDOW + 1));
		EndPaint(m_hWnd, &ps);
		return 0;
	}

	LRESULT on_window_print_client(uint32_t /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/)
	{
		//RECT r;
		//GetClientRect(&r);
		//FillRect((HDC)wParam, &r, (HBRUSH)(COLOR_WINDOW + 1));
		return 0;
	}

	LRESULT on_window_erase_background(uint32_t /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/) const
	{
		const auto dc = std::bit_cast<HDC>(wParam);
		win_rect r;
		GetClipBox(dc, r);
		fill_solid_rect(dc, r, _colors.background);
		return 1;
	}

	LRESULT on_window_enable(uint32_t /*uMsg*/, WPARAM wParam, LPARAM lParam)
	{
		if (_include_time)
		{
			EnableWindow(_time_control, static_cast<BOOL>(wParam));
		}

		EnableWindow(_date_control, static_cast<BOOL>(wParam));

		return 0;
	}

	LRESULT on_window_layout(uint32_t /*uMsg*/, WPARAM wParam, LPARAM lParam)
	{
		const auto cx_min = _include_time ? _cx_min * 2 : _cx_min;

		_extent = {std::min(cx_min, static_cast<int>(LOWORD(lParam))), HIWORD(lParam)};

		auto date_bounds = win_rect(_extent);
		auto time_bounds = win_rect(_extent);

		if (_include_time)
		{
			const auto x = (_extent.cx) / 2;
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
		const int cx_result = _include_time ? cx : std::min(_cx_min, cx);
		return {cx_result, r.height()};
	}

	void focus() override
	{
		SetFocus(_date_control);
	}

	bool has_focus() const override
	{
		const auto* const f = GetFocus();
		return f == _date_control || f == _time_control;
	}

	void dpi_changed() override
	{
		SetFont(m_hWnd, _ctx->dialog);
		SetFont(_date_control, _ctx->dialog);
		SetFont(_time_control, _ctx->dialog);
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
		const auto dw_style = WS_CHILD;
		const auto* const class_name = L"DIFF_DATE_CTRL";

		if (register_class(CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS, nullptr, nullptr, _ctx->gdi_brush(_colors.background),
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


ComPtr<ID2D1PathGeometry> GenTriangleGeometry(ID2D1Factory* f, const D2D1_POINT_2F pt1, const D2D1_POINT_2F pt2,
                                              const D2D1_POINT_2F pt3)
{
	ComPtr<ID2D1GeometrySink> sink;
	ComPtr<ID2D1PathGeometry> pg;

	auto hr = f->CreatePathGeometry(&pg);

	if (SUCCEEDED(hr))
	{
		hr = pg->Open(&sink);
	}

	if (SUCCEEDED(hr))
	{
		sink->BeginFigure(pt1, D2D1_FIGURE_BEGIN_FILLED);
		sink->AddLine(pt2);
		sink->AddLine(pt3);
		sink->EndFigure(D2D1_FIGURE_END_CLOSED);

		hr = sink->Close();
	}

	return pg;
}


static void draw_bubble_background(const factories_ptr& f, ID2D1RenderTarget* dc, const recti bounds,
                                   const pointi focus_location, const float padding, const float shadow_xy,
                                   const float radius)
{
	dc->Clear();

	const auto shadow = load_bitmap_resource(f, dc, IDB_SHADOW);

	if (shadow)
	{
		streach_background(dc, shadow.Get(), {0, 0, bounds.width(), bounds.height()}, 32, 1.0f);
	}

	const auto l = padding;
	const auto t = padding;
	const auto r = static_cast<float>(bounds.width()) - padding;
	const auto b = static_cast<float>(bounds.height()) - padding;

	ComPtr<ID2D1RoundedRectangleGeometry> rrg;
	ComPtr<ID2D1PathGeometry> tg;
	ComPtr<ID2D1PathGeometry> g;
	ComPtr<ID2D1GeometrySink> s;

	D2D1_ROUNDED_RECT rr;
	rr.rect = {l, t, r, b};
	rr.radiusX = radius;
	rr.radiusY = radius;

	auto hr = f->d2d->CreateRoundedRectangleGeometry(rr, &rrg);

	if (SUCCEEDED(hr))
	{
		if (focus_location.y <= bounds.top)
		{
			const float center = std::clamp(static_cast<float>(focus_location.x - bounds.left), l + 24.0f, r - 24.0f);
			tg = GenTriangleGeometry(f->d2d.Get(), {center - padding, t}, {center, t - padding}, {center + padding, t});
		}
		else if (focus_location.x >= bounds.right)
		{
			const float center = std::clamp(static_cast<float>(focus_location.y - bounds.top), t + 24.0f, b - 24.0f);
			tg = GenTriangleGeometry(f->d2d.Get(), {r, center - padding}, {r + padding, center}, {r, center + padding});
		}
		else if (focus_location.y >= bounds.bottom)
		{
			const float center = std::clamp(static_cast<float>(focus_location.x - bounds.left), l + 24.0f, r - 24.0f);
			tg = GenTriangleGeometry(f->d2d.Get(), {center + padding, b}, {center, b + padding}, {center - padding, b});
		}
		else //if (focus_location.x <= bounds.left)
		{
			const float center = std::clamp(static_cast<float>(focus_location.y - bounds.top), t + 24.0f, b - 24.0f);
			tg = GenTriangleGeometry(f->d2d.Get(), {l, center + padding}, {l - padding, center}, {l, center - padding});
		}

		if (SUCCEEDED(hr))
		{
			hr = f->d2d->CreatePathGeometry(&g);
		}

		if (SUCCEEDED(hr))
		{
			hr = g->Open(&s);
		}

		if (SUCCEEDED(hr))
		{
			hr = rrg->CombineWithGeometry(tg.Get(), D2D1_COMBINE_MODE_UNION, nullptr, 0.0f, s.Get());
		}

		if (SUCCEEDED(hr))
		{
			hr = s->Close();
		}
	}

	if (SUCCEEDED(hr))
	{
		ComPtr<ID2D1SolidColorBrush> brush;
		const ui::color color = ui::abgr(ui::style::color::bubble_background);
		hr = dc->CreateSolidColorBrush(D2D1::ColorF(color.r, color.g, color.b, color.a), &brush);

		if (SUCCEEDED(hr))
		{
			dc->FillGeometry(g.Get(), brush.Get());
		}
	}
}


class frame_base : public win_impl
{
public:
	frame_base(owner_context_ptr ctx): _gdi_ctx(ctx)
	{
	}


	void create_draw_context(const factories_ptr& f, bool is_d3d, bool use_swapchain);

	LRESULT on_window_message(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;

	void reset_device();
	void handle_render();
	void handle_resize(sizei extent, bool is_minimised);
	void present();

	virtual LRESULT handle_message(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) = 0;
	virtual void on_render(const draw_context_device_ptr& ctx) = 0;
	virtual void on_resize(sizei extent, bool is_minimized) = 0;

	factories_ptr _f;
	draw_context_device_ptr _draw_ctx;

	ComPtr<ID2D1DeviceContext> _device_context;
	ComPtr<ID2D1HwndRenderTarget> _render_target;
	ComPtr<IDXGISwapChain> _swap_chain;

	mutable df::hash_map<unsigned long, HBRUSH> cached_gdi_brushes;

	sizei _extent;
	bool _is_occluded = false;
	ui::control_frame_weak_ptr _owner;
	owner_context_ptr _gdi_ctx;

	void destroy_frame_base()
	{
		_device_context.Reset();
		_render_target.Reset();
		_swap_chain.Reset();
		_draw_ctx.reset();
		_f.reset();

		if (m_hWnd != nullptr)
		{
			SetWindowLongPtr(m_hWnd, GWLP_USERDATA, 0);
			m_hWnd = nullptr;
		}
	}

	ID2D1RenderTarget* get_render_target()
	{
		if (_device_context) return _device_context.Get();
		return _render_target.Get();
	}
};

void frame_base::create_draw_context(const factories_ptr& f, const bool use_d3d, const bool use_transparency)
{
	_f = f;

	if (m_hWnd != nullptr)
	{
		HRESULT hr = S_OK;

		if (use_d3d)
		{
			ComPtr<IDXGISwapChain> sc;
			ComPtr<IDXGIFactory2> f2;

			if (SUCCEEDED(_f->dxgi.As(&f2)))
			{
				ComPtr<IDXGISwapChain1> sc1;

				DXGI_SWAP_CHAIN_DESC1 sd = {};
				sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
				sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_BACK_BUFFER;
				// (is_d3d ? DXGI_USAGE_BACK_BUFFER : 0);
				sd.Scaling = /*is_d3d ? DXGI_SCALING_NONE :*/ DXGI_SCALING_STRETCH;
				sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
				// DXGI_SWAP_EFFECT_FLIP_DISCARD; //'/*is_d3d ? (is_win_10 ? DXGI_SWAP_EFFECT_FLIP_DISCARD : DXGI_SWAP_EFFECT_DISCARD) :*/ DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
				sd.BufferCount = 2;
				sd.SampleDesc.Count = 1;
				sd.AlphaMode = use_d3d ? DXGI_ALPHA_MODE_IGNORE : DXGI_ALPHA_MODE_PREMULTIPLIED;
				sd.Width = std::max(_extent.cx, 64);
				sd.Height = std::max(_extent.cy, 64);

				hr = f2->CreateSwapChainForHwnd(_f->d3d_device.Get(), m_hWnd, &sd, nullptr, nullptr, &sc1);

				if (SUCCEEDED(hr))
				{
					sc = sc1;
				}
			}
			else
			{
				DXGI_SWAP_CHAIN_DESC sd = {};
				sd.BufferCount = 1;
				sd.BufferDesc.Width = std::max(_extent.cx, 64);
				sd.BufferDesc.Height = std::max(_extent.cy, 64);
				sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
				sd.BufferDesc.RefreshRate.Numerator = 60;
				sd.BufferDesc.RefreshRate.Denominator = 1;
				sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_BACK_BUFFER;
				sd.OutputWindow = m_hWnd;
				sd.SampleDesc.Count = 1;
				sd.SampleDesc.Quality = 0;
				sd.Windowed = TRUE;
				hr = _f->dxgi->CreateSwapChain(_f->d3d_device.Get(), &sd, &sc);
			}


			if (SUCCEEDED(hr))
			{
				_swap_chain = sc;
				_f->dxgi->MakeWindowAssociation(m_hWnd, DXGI_MWA_NO_WINDOW_CHANGES);
				_draw_ctx = d3d11_create_context(_f, _swap_chain, _gdi_ctx->calc_base_font_size());
			}
		}
		else
		{
			RECT rc;
			GetClientRect(m_hWnd, &rc);

			const D2D1_SIZE_U size = D2D1::SizeU(
				rc.right - rc.left,
				rc.bottom - rc.top
			);

			ComPtr<ID2D1HwndRenderTarget> rt;
			const auto pixel_format = D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN,
			                                            use_transparency
				                                            ? D2D1_ALPHA_MODE_PREMULTIPLIED
				                                            : D2D1_ALPHA_MODE_UNKNOWN);

			hr = _f->d2d->CreateHwndRenderTarget(
				D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, pixel_format, 96.0f, 96.0f),
				D2D1::HwndRenderTargetProperties(m_hWnd, size),
				&rt);

			if (SUCCEEDED(hr))
			{
				rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

				ComPtr<ID2D1DeviceContext> dc;

				if (SUCCEEDED(rt.As(&dc)) && dc)
				{
					dc->SetUnitMode(D2D1_UNIT_MODE_PIXELS);
				}

				_render_target = rt;
				_draw_ctx = std::make_shared<
					d2d_draw_context>(_f, get_render_target(), _gdi_ctx->calc_base_font_size());
			}
		}
	}

	if (_draw_ctx && _gdi_ctx)
	{
		_draw_ctx->scale_factor = _gdi_ctx->scale_factor;
		_draw_ctx->icon_cxy = calc_icon_cxy(_gdi_ctx->scale_factor);
	}
}

void frame_base::reset_device()
{
	// df::assert_true(false); // todo
}

void frame_base::handle_resize(const sizei extent, const bool is_minimised)
{
	if (_extent != extent)
	{
		_extent = extent;

		if (!_extent.is_empty() && !is_minimised)
		{
			if (_swap_chain)
			{
				/*DXGI_MODE_DESC mode_desc;
				mode_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
				mode_desc.Height = extent.cy;
				mode_desc.Width = extent.cx;
				_swap_chain->ResizeTarget(&mode_desc);*/

				if (_device_context)
				{
					_device_context->SetTarget(nullptr);
				}

				const UINT flags = 0;
				_swap_chain->ResizeBuffers(swap_buffer_count, extent.cx, extent.cy, back_buffer_format, flags);

				if (_device_context)
				{
					D2D1_BITMAP_PROPERTIES1 properties = {};
					properties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
					properties.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
					properties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;

					ComPtr<IDXGISurface2> surface;
					ComPtr<ID2D1Bitmap1> bitmap;

					auto hr = _swap_chain->GetBuffer(0, __uuidof(surface),
					                                 std::bit_cast<void**>(surface.GetAddressOf()));

					if (SUCCEEDED(hr))
					{
						hr = _device_context->CreateBitmapFromDxgiSurface(
							surface.Get(), properties, bitmap.GetAddressOf());

						if (SUCCEEDED(hr))
						{
							_device_context->SetTarget(bitmap.Get());
						}
					}
				}

				InvalidateRect(m_hWnd, nullptr, TRUE);
			}
			else if (_render_target)
			{
				_render_target->Resize(D2D1_SIZE_U{static_cast<uint32_t>(extent.cx), static_cast<uint32_t>(extent.cy)});
			}

			if (_draw_ctx)
			{
				_draw_ctx->resize(_extent);
			}
		}
	}

	on_resize(extent, is_minimised);
};

void frame_base::present()
{
	if (_swap_chain)
	{
		auto hr = _swap_chain->Present(0, 0);

		//if (SUCCEEDED(hr))
		//{
		//	const auto flags = _is_occluded ? DXGI_PRESENT_TEST : 0;
		//	hr = _swap_chain->Present(0, flags);

		//	if (DXGI_STATUS_OCCLUDED == hr)
		//	{
		//		_is_occluded = true;
		//	}
		//	else if (DXGI_ERROR_DEVICE_RESET == hr ||
		//		DXGI_ERROR_DEVICE_REMOVED == hr)
		//	{
		//		// reset the device
		//		df::log(__FUNCTION__, str::format(u8"D3D device reset. Present returned {:x}", hr));
		//		reset_device();
		//	}
		//	else if (SUCCEEDED(hr))
		//	{
		//		if (_is_occluded)
		//		{
		//			_is_occluded = false;
		//		}
		//	}
		//}
	}
}

void frame_base::handle_render()
{
	const auto ctx = _draw_ctx;

	if (ctx)
	{
		ctx->begin_draw(_extent, _gdi_ctx->calc_base_font_size());
		on_render(ctx);
		ctx->render();
	}

	present();
}

LRESULT frame_base::on_window_message(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
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
			handle_render();
			ValidateRect(m_hWnd, nullptr);
			return 0;
		}
		break;
	case WM_DISPLAYCHANGE:
		{
			InvalidateRect(hwnd, nullptr, FALSE);
			return 0;
		}

		break;
	default:
		return handle_message(hwnd, uMsg, wParam, lParam);
	}

	return handle_message(hwnd, uMsg, wParam, lParam);
}


class bubble_impl : public frame_base, public ui::bubble_frame
{
protected:
	UINT_PTR _timer_id = 0;
	int _alpha = 0;
	int _alpha_target = 0;
	recti _bounds;
	view_elements _elements;
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

	bool create(HWND parent, const factories_ptr& f)
	{
		const auto* const class_name = L"DIFF_BUBBLE";

		if (register_class(CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS, nullptr, nullptr,
		                   _gdi_ctx->gdi_brush(ui::style::color::bubble_background),
		                   nullptr, class_name, nullptr))
		{
			const auto style_ex = WS_EX_LAYERED; // WS_EX_NOREDIRECTIONBITMAP;
			m_hWnd = CreateWindowEx(style_ex, class_name, nullptr, WS_POPUP, 0, 0, 0, 0, parent, nullptr,
			                        get_resource_instance, this);
			df::assert_true(m_hWnd != nullptr);

			if (m_hWnd)
			{
				const MARGINS margins = {-1};
				DwmExtendFrameIntoClientArea(m_hWnd, &margins);
				create_draw_context(f, false, true);
				SetFont(m_hWnd, _gdi_ctx->dialog);
				return true;
			}
		}

		return false;
	}

	LRESULT handle_message(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override
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

			const DWORD flags = LWA_ALPHA;
			const DWORD col = RGB(255, 255, 255);
			SetLayeredWindowAttributes(m_hWnd, col, _alpha, flags);
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

	void show(const view_elements& elements, const recti button_rect, const int x_focus, const int prefered_size,
	          const bool horizontal) override
	{
		if (_draw_ctx)
		{
			const auto scale_factor = _gdi_ctx->scale_factor;
			const auto element_extent = elements.measure(*_draw_ctx, df::round(prefered_size * scale_factor));
			const auto cx = std::max(80, element_extent.cx) + (horz_padding * 2);
			const auto cy = std::max(36, element_extent.cy) + (vert_padding * 2);

			const auto xx_focus = (x_focus == -1
				                       ? ((button_rect.left + button_rect.right) / 2)
				                       : (button_rect.left + x_focus)) - (cx / 2);
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

			_draw_ctx->update_font_size(df::round(global_base_font_size * scale_factor));
			_bounds = recti(x, y, x + cx, y + cy).clamp(desktop_bounds.inflate(shadow_padding));
			_focus_loc = focus_loc;
			_elements = elements;

			ui::control_layouts positions;
			_elements.layout(*_draw_ctx, center_rect(element_extent, sizei{cx, cy}), positions);

			InvalidateRect(m_hWnd, nullptr, FALSE);
			SetWindowPos(m_hWnd, nullptr, x, y, cx, cy, SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);

			//update(bounds);
			set_alpha(255);
		}
	}

	void on_render(const draw_context_device_ptr& ctx) override
	{
		const auto rt = get_render_target();
		draw_bubble_background(_f, rt, _bounds, _focus_loc, static_cast<float>(shadow_padding),
		                       static_cast<float>(shadow_xy), radius);
		_draw_ctx->colors = {
			ui::style::color::bubble_background, ui::style::color::view_text, ui::style::color::view_selected_background
		};
		_elements.render(*ctx, {});
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

	void set_alpha(int a)
	{
		if (_alpha_target != a)
		{
			_alpha_target = a;

			if (_timer_id == 0u)
			{
				_timer_id = SetTimer(m_hWnd, 0, 1000 / 30, nullptr);
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
private:
	pointi _pan_start_loc;
	ui::frame_weak_ptr _parent_frame;
	ui::frame_host_weak_ptr _host;
	ui::frame_host_weak_ptr _parent_host;
	bool _hover = false;
	bool _tracking = false;
	bool _has_focus = false;
	ui::frame_style _style;
	UINT_PTR _timer_id = 0;


public:
	frame_impl(owner_context_ptr ctx, ui::frame_weak_ptr parent_frame, ui::frame_host_weak_ptr parent_host,
	           const ui::frame_host_weak_ptr& host, const ui::frame_style& style) :
		frame_base(ctx), _parent_frame(std::move(parent_frame)), _host(host), _parent_host(parent_host), _style(style)
	{
	}

	void destroy() override
	{
		DestroyWindow(m_hWnd);
	}

	LRESULT handle_message(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override
	{
		//if (uMsg == WM_SIZE) return on_window_layout(uMsg, wParam, lParam);
		if (uMsg == WM_DESTROY) return on_window_destroy(uMsg, wParam, lParam);
		if (uMsg == WM_ERASEBKGND) return on_window_erase_background(uMsg, wParam, lParam);
		//if (uMsg == WM_PAINT) return on_window_paint(uMsg, wParam, lParam);
		if (uMsg == WM_SETCURSOR) return on_window_set_cursor(uMsg, wParam, lParam);
		if (uMsg == WM_SETFOCUS) return on_window_set_focus(uMsg, wParam, lParam);
		if (uMsg == WM_KILLFOCUS) return on_window_kill_focus(uMsg, wParam, lParam);
		if (uMsg == WM_KEYDOWN) return on_window_key_down(uMsg, wParam, lParam);
		if (uMsg == WM_TIMER) return on_window_timer(uMsg, wParam, lParam);
		if (uMsg == WM_LBUTTONDOWN) return on_mouse_left_button_down(uMsg, wParam, lParam);
		if (uMsg == WM_LBUTTONUP) return on_mouse_left_button_up(uMsg, wParam, lParam);
		if (uMsg == WM_XBUTTONUP) return on_mouse_x_button_up(uMsg, wParam, lParam);
		if (uMsg == WM_MOUSELEAVE) return on_mouse_leave(uMsg, wParam, lParam);
		if (uMsg == WM_MOUSEMOVE) return on_mouse_move(uMsg, wParam, lParam);
		if (uMsg == WM_MOUSEWHEEL) return on_mouse_wheel(uMsg, wParam, lParam);
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
			ctx->clear(_style.colors.background);

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

	bool create(HWND parent, const ui::frame_style& style, const factories_ptr& f)
	{
		const auto* const class_name = L"DIFF_FRAME";
		auto dw_style = style.child ? WS_CHILD : WS_POPUP;
		auto dw_ex_style = 0; // style.child ? 0 : WS_EX_COMPOSITED;
		if (style.no_focus) dw_ex_style |= WS_EX_NOACTIVATE;
		if (style.can_focus) dw_style |= WS_TABSTOP;
		//if (!style.hardware_accelerated) dw_ex_style |= WS_EX_COMPOSITED;

		if (register_class(CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS, nullptr, nullptr,
		                   _gdi_ctx->gdi_brush(ui::style::color::view_background),
		                   nullptr, class_name, nullptr))
		{
			m_hWnd = CreateWindowEx(dw_ex_style, class_name, nullptr, dw_style, 0, 0, 0, 0, parent, nullptr,
			                        get_resource_instance, this);
			df::assert_true(m_hWnd != nullptr);

			if (m_hWnd)
			{
				create_draw_context(f, _style.hardware_accelerated, false);
				SetFont(m_hWnd, _gdi_ctx->dialog);

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
			/*MSG m;

			if (!PeekMessage(&m, m_hWnd, WM_SIZE, WM_SIZE, PM_NOREMOVE | PM_NOYIELD))
			{
				PostMessage(m_hWnd, WM_SIZE, 0, MAKELONG(_extent.cx, _extent.cy));
			}*/

			const auto h = _host.lock();

			if (h)
			{
				h->on_window_layout(*_draw_ctx, _extent, IsIconic(m_hWnd));
			}
		}
	}

	//LRESULT on_window_layout(uint32_t /*uMsg*/, WPARAM wParam, LPARAM lParam)
	//{
	//	_extent = { LOWORD(lParam), HIWORD(lParam) };

	//	if (is_valid_device())
	//	{
	//		_device->resize(_extent);
	//	}
	//	else
	//	{
	//		_render.resize(_extent);
	//	}

	//	auto h = _host.lock();

	//	if (h)
	//	{
	//		ui::measure_context& mc = _device ? static_cast<ui::measure_context&>(*_device) : _render;
	//		h->on_window_layout(mc, _extent, wParam == SIZE_MINIMIZED);
	//	}

	//	return 0;
	//}


	LRESULT on_window_destroy(const uint32_t /*uMsg*/, const WPARAM /*wParam*/, const LPARAM /*lParam*/)
	{
		if (_timer_id) KillTimer(m_hWnd, _timer_id);

		if (_style.can_drop)
		{
			RevokeDragDrop(m_hWnd);
		}

		const auto h = _host.lock();
		if (h) h->on_window_destroy();

		if (is_valid_device())
		{
			_draw_ctx->destroy();
		}

		destroy_frame_base();

		return 0;
	}

	LRESULT on_window_nc_hit_test(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam)
	{
		if (_style.child)
		{
			const pointi loc = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
			win_rect rc;
			GetWindowRect(GetParent(m_hWnd), &rc);

			const auto scale_factor = _draw_ctx ? _draw_ctx->scale_factor : 1.0;
			const auto border_thickness = df::round(nonclient_border_thickness * scale_factor);

			if (rc.right >= loc.x && (rc.right - border_thickness) <= loc.x)
			{
				return HTTRANSPARENT;
			}

			if (rc.left <= loc.x && (rc.left + border_thickness) >= loc.x)
			{
				return HTTRANSPARENT;
			}
		}

		return DefWindowProc(m_hWnd, uMsg, wParam, lParam);
	}

	LRESULT on_window_set_cursor(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam)
	{
		//update_cursor();
		SetCursor(_cursor);
		return 1;
	}

	LRESULT on_window_erase_background(const uint32_t /*uMsg*/, const WPARAM wParam, const LPARAM /*lParam*/)
	{
		return 1;
	}

	//LRESULT on_window_mouse_activate(uint32_t /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/)
	//{
	//	return MA_NOACTIVATE;
	//}

	void redraw() override
	{
		if (is_valid_device())
		{
			_draw_ctx->render();
			present();
		}
		else
		{
			InvalidateRect(hwnd(), nullptr, 0);
		}
	}

	LRESULT on_window_timer(const uint32_t /*uMsg*/, const WPARAM /*wParam*/, const LPARAM /*lParam*/)
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

	LRESULT on_mouse_left_button_up(uint32_t /*uMsg*/, WPARAM wParam, LPARAM lParam)
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

	LRESULT on_mouse_left_button_double_click(uint32_t /*uMsg*/, WPARAM wParam, LPARAM lParam)
	{
		const pointi loc(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		const auto h = _host.lock();
		if (h) h->on_mouse_left_button_double_click(loc, to_key_state(wParam));
		return 0;
	}

	LRESULT on_window_mouse_activate(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam)
	{
		if (_style.no_focus)
		{
			return MA_NOACTIVATE;
		}

		return DefWindowProc(m_hWnd, uMsg, wParam, lParam);
	}

	LRESULT on_mouse_leave(uint32_t /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
	{
		const pointi loc(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		_hover = false;
		const auto h = _host.lock();
		if (h) h->on_mouse_leave(loc);
		InvalidateRect(m_hWnd, nullptr, FALSE);
		return 0;
	}

	LRESULT on_mouse_wheel(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam)
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

	LRESULT on_window_gesture_notify(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam)
	{
		const auto wanted_gestures = GC_PAN_WITH_SINGLE_FINGER_VERTICALLY | GC_PAN_WITH_SINGLE_FINGER_HORIZONTALLY;
		GESTURECONFIG gc = {GID_PAN, wanted_gestures, 0};
		SetGestureConfig(m_hWnd, 0, 1, &gc, sizeof(GESTURECONFIG));

		return DefWindowProc(m_hWnd, WM_GESTURENOTIFY, wParam, lParam);
	}

	LRESULT on_window_gesture(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam)
	{
		GESTUREINFO gi;
		ZeroMemory(&gi, sizeof(GESTUREINFO));
		gi.cbSize = sizeof(GESTUREINFO);

		if (GetGestureInfo((HGESTUREINFO)lParam, &gi))
		{
			switch (gi.dwID)
			{
			case GID_BEGIN:
				_pan_start_loc = {gi.ptsLocation.x, gi.ptsLocation.y};
				break;
			case GID_END:
				break;
			case GID_PAN:
				{
					const pointi current_loc = {gi.ptsLocation.x, gi.ptsLocation.y};


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

					//CloseGestureInfoHandle(gestureHandle);
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
		if (h) h->focus_changed(false, shared_from_this());
		const auto ph = _parent_host.lock();
		if (ph) ph->focus_changed(false, shared_from_this());
		_has_focus = false;
		return 0;
	}

	LRESULT on_window_key_down(uint32_t /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/)
	{
		const auto h = _host.lock();
		if (h) return h->key_down(static_cast<int>(wParam), ui::current_key_state());
		return 0;
	}

	// dry?
	void set_cursor(ui::style::cursor cursor) override
	{
		switch (cursor)
		{
		case ui::style::cursor::none: set_cursor(nullptr);
			break;
		case ui::style::cursor::normal: set_cursor(resources.normal);
			break;
		case ui::style::cursor::link: set_cursor(resources.link);
			break;
		case ui::style::cursor::select: set_cursor(resources.select_cur);
			break;
		case ui::style::cursor::move: set_cursor(resources.move);
			break;
		case ui::style::cursor::left_right: set_cursor(resources.left_right);
			break;
		case ui::style::cursor::up_down: set_cursor(resources.up_down);
			break;
		case ui::style::cursor::hand_up: set_cursor(resources.hand_up);
			break;
		case ui::style::cursor::hand_down: set_cursor(resources.hand_down);
			break;
		default: ;
		}
	}

	void set_cursor(HICON cursor)
	{
		_cursor = cursor;
		SetCursor(cursor);
	}

	void reset_graphics() override
	{
		if (is_valid_device())
		{
			reset_device();
		}
	}

	void options_changed() override
	{
		if (is_valid_device())
		{
			_draw_ctx->update_font_size(_gdi_ctx->calc_base_font_size());
		}

		SetFont(hwnd(), _gdi_ctx->dialog);
	}

	void track_menu(const recti button_bounds, const std::vector<ui::command_ptr>& buttons) override
	{
		const auto parent = _parent_frame.lock();

		if (parent)
		{
			parent->track_menu(button_bounds, buttons);
		}
	}

	void dpi_changed()
	{
		if (_draw_ctx && _gdi_ctx)
		{
			_draw_ctx->scale_factor = _gdi_ctx->scale_factor;
			_draw_ctx->icon_cxy = calc_icon_cxy(_gdi_ctx->scale_factor);
		}

		if (is_valid_device())
		{
			_draw_ctx->update_font_size(_gdi_ctx->calc_base_font_size());
		}

		SetFont(hwnd(), _gdi_ctx->dialog);
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
		df::trace(str::format(u8"frame_impl::QueryInterface {}", win32_to_string(iid)));

		if (IsEqualGUID(iid, IID_IDropTarget))
		{
			*ppvObject = static_cast<IDropTarget*>(this);
			return S_OK;
		}

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

	void set_drop_effect(const platform::drop_effect e, DWORD* pdwEffect)
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

	STDMETHODIMP DragEnter(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override
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

	STDMETHODIMP DragOver(DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override
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

	STDMETHODIMP Drop(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) override
	{
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
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class win32_menu
{
public:
	HMENU m_hMenu;


	win32_menu(HMENU hMenu = nullptr) : m_hMenu(hMenu)
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
		return (m_hMenu != nullptr) ? TRUE : FALSE;
	}

	BOOL AppendMenu(uint32_t nFlags, UINT_PTR nIDNewItem = 0, LPCTSTR lpszNewItem = nullptr) const
	{
		df::assert_true(IsMenu(m_hMenu));
		return ::AppendMenu(m_hMenu, nFlags, nIDNewItem, lpszNewItem);
	}

	BOOL AppendMenu(uint32_t nFlags, HMENU hSubMenu, LPCTSTR lpszNewItem) const
	{
		df::assert_true(IsMenu(m_hMenu));
		df::assert_true(IsMenu(hSubMenu));
		return ::AppendMenu(m_hMenu, nFlags | MF_POPUP, (UINT_PTR)hSubMenu, lpszNewItem);
	}

	BOOL InsertMenuItem(uint32_t uItem, BOOL bByPosition, LPMENUITEMINFO lpmii) const
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

static const sizei menu_size_border = {3, 3};

class win32_app : public ui::platform_app
{
public:
	ui::app_ptr _app;
	std::vector<HANDLE> app_thread_events;
	std::vector<std::function<void()>> app_event_actions;
	HANDLE _timer_handle = nullptr;
	int _frame_delay = 0;
	std::shared_ptr<control_host_impl> _frame;
	std::vector<HANDLE> _folder_changes;
	platform::thread_event _idle_event;
	LPARAM _last_mouse_move = 0;
	bool _enable_screen_saver = true;
	DWORD _dwScreenSaverSetting = 0;
	DWORD _dwLowPowerSetting = 0;
	DWORD _dwPowerOffSetting = 0;
	DWORD _dwAppBarState = 0;
	factories_ptr _f;

	win32_app() : _idle_event(false, false)
	{
	}

	void update_event_handles();
	void tick();

	static bool is_pre_translate_message(const int message)
	{
		return message != WM_TIMER &&
			message != WM_PAINT &&
			message != WM_ERASEBKGND;
	}

	static bool is_edit_char(HWND hwnd, const char32_t c, const ui::key_state keys)
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
	void idle();
	int ui_message_loop();
	uint32_t ui_wait_for_signal(const std::vector<std::reference_wrapper<platform::thread_event>>& events,
	                            bool wait_all, uint32_t timeout_ms, const std::function<bool(LPMSG m)>& cb);
	void sys_command(ui::sys_command_type cmd) override;
	void full_screen(bool full) override;

	/*static void CALLBACK timer_cb(LPVOID lpArgToCompletionRoutine, DWORD dwTimerLowValue, DWORD dwTimerHighValue)
	{
		const auto p_this = static_cast<win32_app*>(lpArgToCompletionRoutine);
		p_this->_app->prepare_frame();
	}*/

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

	win32_app* _pa = nullptr;
	ui::weak_app_ptr _app;
	ui::frame_host_weak_ptr _host;

	platform::thread_event _close;
	ui::close_result _modal_result = ui::close_result::ok;
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

	WINDOWPLACEMENT restore_window_placement;
	DWORD restore_style = 0;
	DWORD restore_ex_style = 0;
	recti _hover_command_bounds;
	uint32_t cmd_show = SW_SHOW;

	control_host_impl(owner_context_ptr ctx, const ui::frame_host_weak_ptr& host, ui::weak_app_ptr app, win32_app* pa,
	                  const bool is_app_frame, const ui::color_style colors) :
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
		destroy_frame_base();
		clear();

		if (m_hWnd && IsWindow(m_hWnd))
		{
			DestroyWindow(m_hWnd);
		}

		m_hWnd = nullptr;
	}

	HWND Create(
		_In_opt_ HWND hWndParent,
		_In_ win_rect rect,
		_In_ DWORD dwStyle,
		_In_ DWORD dwExStyle)
	{
		const auto* const class_name = _is_app_frame ? L"DIFF_MAIN" : L"DIFF_DLG";
		auto* const icon = _is_app_frame ? resources.diffractor_32 : nullptr;

		if (register_class(CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS, icon, nullptr, _gdi_ctx->gdi_brush(_colors.background),
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

	int alloc_ids(int count = 1) const
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

	LRESULT handle_message(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override
	{
		if (uMsg == WM_CREATE) return on_window_create(uMsg, wParam, lParam);
		if (uMsg == WM_DESTROY) return on_window_destroy(uMsg, wParam, lParam);
		//if (uMsg == WM_SIZE) return on_window_layout(uMsg, wParam, lParam);
		//if (uMsg == WM_WINDOWPOSCHANGED) return on_window_pos_changed(uMsg, wParam, lParam);
		//if (uMsg == WM_PAINT) return on_window_paint(uMsg, wParam, lParam);
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
		const auto h = _host.lock();
		if (h) h->on_window_destroy();
		destroy_frame_base();
		const auto result = DefWindowProc(m_hWnd, uMsg, wParam, lParam);
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

	//LRESULT on_window_layout(uint32_t /*uMsg*/, WPARAM wParam, LPARAM lParam)
	//{
	//	_extent = { LOWORD(lParam), HIWORD(lParam) };

	//	if (_render)
	//	{
	//		_render->resize(_extent);

	//		auto h = _host.lock();
	//		if (h) h->on_window_layout(*_render, _extent, wParam == SIZE_MINIMIZED);
	//	}

	//	return 0;
	//}

	LRESULT on_window_nc_hit_test(uint32_t /*uMsg*/, WPARAM wParam, LPARAM lParam);

	LRESULT on_get_def_id(uint32_t /*uMsg*/, WPARAM wParam, LPARAM lParam)
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
			ctx->clear(_colors.background);

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

	LRESULT on_window_print_client(uint32_t /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/)
	{
		/*draw_context_gdi dc((HDC)wParam);
		dc.frame_has_focus = _has_focus;
		dc.colors = _colors;
		dc.clear(_colors.background);
		auto h = _host.lock();
		if (h) h->on_window_paint(dc);*/
		// todo
		return 0;
	}

	LRESULT on_window_erase_background(uint32_t /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/) const
	{
		//const auto dc = std::bit_cast<HDC>(wParam);
		//win_rect r;
		//GetClipBox(dc, r);
		//FillRect(dc, r, gdi_brush(_background_clr));
		return 1;
	}

	ui::color_style calc_colors(HWND child) const
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

	LRESULT on_mouse_move(uint32_t /*uMsg*/, WPARAM wParam, LPARAM lParam)
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

	LRESULT on_mouse_left_button_down(uint32_t /*uMsg*/, WPARAM wParam, LPARAM lParam)
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

	LRESULT on_mouse_left_button_up(uint32_t /*uMsg*/, WPARAM wParam, LPARAM lParam)
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

	LRESULT on_mouse_left_button_double_click(uint32_t /*uMsg*/, WPARAM wParam, LPARAM lParam)
	{
		const pointi loc(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		const auto h = _host.lock();
		if (h) h->on_mouse_left_button_double_click(loc, to_key_state(wParam));
		return 0;
	}

	LRESULT on_mouse_leave(uint32_t /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam)
	{
		const pointi loc(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		_hover = false;
		const auto h = _host.lock();
		if (h) h->on_mouse_leave(loc);
		InvalidateRect(m_hWnd, nullptr, TRUE);
		return 0;
	}

	LRESULT on_mouse_wheel(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam)
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

	static BOOL CALLBACK focus_first_callback(HWND h, LPARAM p)
	{
		const auto first_focused = std::bit_cast<bool*>(p);

		wchar_t szClassName[100];
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

	static BOOL CALLBACK focus_any_callback(HWND h, LPARAM p)
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

	void set_cursor(ui::style::cursor cursor) override
	{
		switch (cursor)
		{
		case ui::style::cursor::none: set_cursor(nullptr);
			break;
		case ui::style::cursor::normal: set_cursor(resources.normal);
			break;
		case ui::style::cursor::link: set_cursor(resources.link);
			break;
		case ui::style::cursor::select: set_cursor(resources.select_cur);
			break;
		case ui::style::cursor::move: set_cursor(resources.move);
			break;
		case ui::style::cursor::left_right: set_cursor(resources.left_right);
			break;
		case ui::style::cursor::up_down: set_cursor(resources.up_down);
			break;
		case ui::style::cursor::hand_up: set_cursor(resources.hand_up);
			break;
		case ui::style::cursor::hand_down: set_cursor(resources.hand_down);
			break;
		default: ;
		}
	}

	void set_cursor(HICON cursor)
	{
		_cursor = cursor;
		SetCursor(cursor);
	}

	ui::close_result wait_for_close(const uint32_t timeout_ms) override
	{
		_close.reset();
		ui_wait_for_signal(_close, timeout_ms, [this](LPMSG m) { return pre_translate_message(m); });
		return _modal_result;
	}

	void close(bool is_cancel) override
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
		return _modal_result == ui::close_result::cancel;
	}

	bool pre_translate_message(LPMSG m)
	{
		BOOL bHandled = 0;
		if (!m_hWnd) return false;

		//switch (m->message)
		//{
		//case WM_MOUSEWHEEL:
		//	on_mouse_wheel(m->message, m->wParam, m->lParam, bHandled);
		//	if (bHandled) return true;
		//	break;
		//case WM_KEYDOWN:
		//	//if (handle_message(WM_KEYDOWN, m->wParam, m->lParam)) return true;
		//	/*if (m->wParam == VK_ESCAPE)
		//	{
		//		close(true);
		//		return true;
		//	}*/
		//	break;
		//}

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

	void apply_layout(const ui::control_layouts& controls, const pointi scroll_offset) override
	{
		HDWP hdwp = BeginDeferWindowPos(static_cast<int>(controls.size()));

		if (hdwp)
		{
			auto* last_h = HWND_TOP;

			for (const auto& c : controls)
			{
				if (hdwp && c.control)
				{
					auto bounds = c.bounds;
					if (c.offset) bounds = bounds.offset(scroll_offset);
					const auto h = std::any_cast<HWND>(c.control->handle());
					df::assert_true(IsWindow(h));
					const auto flags = SWP_NOACTIVATE | (c.visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW);
					hdwp = DeferWindowPos(hdwp, h, last_h, bounds.left, bounds.top, bounds.width(), bounds.height(),
					                      flags);
					last_h = h;

					df::assert_true(hdwp != nullptr);
				}
			}

			if (hdwp)
			{
				EndDeferWindowPos(hdwp);
			}
		}

		if (!hdwp)
		{
			auto* last_h = HWND_TOP;

			for (const auto& c : controls)
			{
				auto bounds = c.bounds;
				if (c.offset) bounds = bounds.offset(scroll_offset);
				const auto h = std::any_cast<HWND>(c.control->handle());
				const auto flags = SWP_NOACTIVATE | (c.visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW);
				SetWindowPos(h, last_h, bounds.left, bounds.top, bounds.width(), bounds.height(), flags);
				last_h = h;
			}
		}
	}

	LRESULT on_window_context_menu(uint32_t /*uMsg*/, WPARAM wParam, LPARAM lParam)
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

	LRESULT on_window_activate(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam)
	{
		const LPARAM r = DefWindowProc(m_hWnd, uMsg, wParam, lParam);
		const auto is_active = (wParam == WA_ACTIVE || wParam == WA_CLICKACTIVE);
		const auto h = _host.lock();
		if (h) h->activate(is_active);
		return r;
	}

	LRESULT on_window_timer(uint32_t /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/);

	LRESULT on_system_device_change(uint32_t /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/);
	LRESULT on_window_erase_background(uint32_t /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/);
	LRESULT on_window_command(uint32_t uMsg, WPARAM wParam, LPARAM lParam);
	LRESULT on_window_hscroll(uint32_t uMsg, WPARAM wParam, LPARAM lParam);

	LRESULT on_window_menu_popup(uint32_t /*uMsg*/, WPARAM wParam, LPARAM lParam) const
	{
		const auto hMenu = std::bit_cast<HMENU>(wParam);

		if (hMenu)
		{
			//_menu.enable_menu(hMenu);
		}

		return 0;
	}

	LRESULT on_window_draw_item(uint32_t /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam) const
	{
		draw_item(std::bit_cast<LPDRAWITEMSTRUCT>(lParam));
		return TRUE;
	}

	LRESULT on_window_measure_item(uint32_t /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam) const
	{
		measure_item(std::bit_cast<LPMEASUREITEMSTRUCT>(lParam));
		return 1;
	}

	LRESULT on_dwm_composition_changed(uint32_t /*uMsg*/, WPARAM wParam, LPARAM lParam);
	LRESULT on_window_nc_activate(uint32_t, WPARAM, LPARAM);
	LRESULT on_window_nc_calc_size(uint32_t, WPARAM wParam, LPARAM lParam);
	LRESULT on_window_nc_paint(uint32_t, WPARAM wParam, LPARAM lParam);

	void update_region();
	void handle_composition_changed();

	void draw_item(LPDRAWITEMSTRUCT lpDrawItemStruct) const
	{
		if (lpDrawItemStruct->CtlType == ODT_MENU)
		{
			const auto found = _menu_commands.find(lpDrawItemStruct->itemID);
			draw_menu_item(found != _menu_commands.end() ? found->second : nullptr, lpDrawItemStruct, _gdi_ctx);
		}
	}

	void measure_item(LPMEASUREITEMSTRUCT lpMeasureItemStruct) const
	{
		if (lpMeasureItemStruct->CtlType == ODT_MENU)
		{
			const auto found = _menu_commands.find(lpMeasureItemStruct->itemID);

			if (found == _menu_commands.end())
			{
				lpMeasureItemStruct->itemWidth = 0;
				lpMeasureItemStruct->itemHeight = 8;
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

					lpMeasureItemStruct->itemWidth = text_bounds.width() + keyboard_accelerator_bounds.width() + 64;
					lpMeasureItemStruct->itemHeight = std::max(text_bounds.height() + 8, 20);

					ReleaseDC(m_hWnd, dc);
				}
			}
		}
	}

	void boost_thread_pri()
	{
		// _state.is_playing_media()
		//_main_thread_default_priority(GetThreadPriority(GetCurrentThread())),
		const auto priority = false ? THREAD_PRIORITY_TIME_CRITICAL : _main_thread_default_priority;

		if (_main_thread_current_priority != priority)
		{
			SetThreadPriority(GetCurrentThread(), priority);
			_main_thread_current_priority = priority;
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

				//auto bounds = desktop_bounds_impl(m_hWnd);
				//SetWindowPos(nullptr, &bounds, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED);
			}

			handle_composition_changed();
		}
	}

	LRESULT on_session_change(uint32_t /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/)
	{
		const auto app = _app.lock();

		if (app && wParam == WTS_SESSION_LOCK)
		{
			app->system_event(ui::os_event_type::screen_locked);
		}
		return 0;
	}

	void update_font_sizes()
	{
		if (_draw_ctx)
		{
			_draw_ctx->update_font_size(_gdi_ctx->calc_base_font_size());
		}

		if (_draw_ctx && _gdi_ctx)
		{
			_draw_ctx->scale_factor = _gdi_ctx->scale_factor;
			_draw_ctx->icon_cxy = calc_icon_cxy(_gdi_ctx->scale_factor);
		}

		if (_gdi_ctx)
		{
			SetFont(m_hWnd, _gdi_ctx->dialog);
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
	}

	LRESULT on_window_dpi_changed(uint32_t /*uMsg*/, WPARAM wParam, LPARAM lParam)
	{
		_gdi_ctx->update_scale_factor(HIWORD(wParam) / static_cast<double>(USER_DEFAULT_SCREEN_DPI));
		update_font_sizes();

		const auto host = _host.lock();

		if (host)
		{
			host->dpi_changed();
		}

		/*
		set_font_base_size(df::round(dpi * (setting.large_font ? large_font_size : normal_font_size)));*/

		/*auto app = _app.lock();

		if (app && wParam == WTS_SESSION_LOCK)
		{
			app->system_event(ui::os_event_type::dpi_changed);
		}*/

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

	LRESULT on_system_settings_change(uint32_t /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/)
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
		/*MSG m;

		if (!PeekMessage(&m, m_hWnd, WM_SIZE, WM_SIZE, PM_NOREMOVE | PM_NOYIELD))
		{
			PostMessage(m_hWnd, WM_SIZE, 0, MAKELONG(_extent.cx, _extent.cy));
		}*/
		if (_draw_ctx && !_extent.is_empty() && IsWindow(m_hWnd))
		{
			/*MSG m;

			if (!PeekMessage(&m, m_hWnd, WM_SIZE, WM_SIZE, PM_NOREMOVE | PM_NOYIELD))
			{
				PostMessage(m_hWnd, WM_SIZE, 0, MAKELONG(_extent.cx, _extent.cy));
			}*/

			const auto h = _host.lock();

			if (h)
			{
				h->on_window_layout(*_draw_ctx, _extent, IsIconic(m_hWnd));
			}
		}
	}

	ui::edit_ptr create_edit(const ui::edit_styles& styles, std::u8string_view text,
	                         std::function<void(const std::u8string&)> changed) override;
	ui::trackbar_ptr create_slider(int min, int max, std::function<void(int, bool)> changed) override;
	ui::button_ptr create_button(std::u8string_view text, std::function<void()> invoke,
	                             bool default_button = false) override;
	ui::button_ptr create_button(icon_index icon, std::u8string_view title, std::u8string_view details,
	                             std::function<void()> invoke, bool default_button = false) override;
	ui::button_ptr create_check_button(bool val, std::u8string_view text, bool is_radio,
	                                   std::function<void(bool)> changed) override;
	ui::web_window_ptr create_web_window(std::u8string_view start_url, ui::web_events* events) override;
	ui::date_time_control_ptr create_date_time_control(df::date_t text, std::function<void(df::date_t)> changed,
	                                                   bool include_time) override;
	ui::toolbar_ptr
	create_toolbar(const ui::toolbar_styles& styles, const std::vector<ui::command_ptr>& buttons) override;
	ui::control_frame_ptr create_dlg(ui::frame_host_weak_ptr host, bool is_popup) override;
	ui::bubble_window_ptr create_bubble() override;

	HHOOK _hMenuHook = nullptr;
	//static this_class* _current;
	//std::vector<HWND> _menuStack;
	win_rect m_rcButton;


	static LRESULT on_menu_nc_calc_size(HWND hwnd, uint32_t /*uMsg*/, WPARAM wParam, LPARAM lParam)
	{
		auto calc_valid_rects = static_cast<BOOL>(wParam);
		const auto pr = std::bit_cast<LPRECT>(lParam);

		if (pr)
		{
			pr->left += menu_size_border.cx;
			pr->right -= menu_size_border.cx;
			pr->top += menu_size_border.cy;
			pr->bottom -= menu_size_border.cy;
		}

		return 0;
	}

	static LRESULT on_menu_nc_paint(HWND hwnd, uint32_t /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/)
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

	static void draw_menu(HDC dc, const win_rect& rcWin)
	{
		fill_solid_rect(dc, rcWin, ui::lighten(ui::style::color::menu_background, 0.22f));
		fill_solid_rect(dc, rcWin.inflate(-menu_size_border.cx, -menu_size_border.cy),
		                ui::style::color::menu_background);
	}

	static LRESULT on_menu_print(HWND hwnd, uint32_t /*uMsg*/, WPARAM wParam, LPARAM lParam)
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

	static LRESULT CALLBACK MenuSuperProc(HWND hWnd, UINT uMsg, WPARAM wParam,
	                                      LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
	{
		//auto pt = std::bit_cast<control_host_impl*>(dwRefData);

		if (uMsg == WM_NCPAINT) return on_menu_nc_paint(hWnd, uMsg, wParam, lParam);
		if (uMsg == WM_PRINT) return on_menu_print(hWnd, uMsg, wParam, lParam);
		if (uMsg == WM_NCCALCSIZE) return on_menu_nc_calc_size(hWnd, uMsg, wParam, lParam);

		return DefSubclassProc(hWnd, uMsg, wParam, lParam);
	}

	static LRESULT CALLBACK menu_create_hook_proc(int nCode, WPARAM wParam, LPARAM lParam)
	{
		const LRESULT lRet = 0;
		wchar_t szClassName[7];

		//auto current = _current;

		if (nCode == HCBT_CREATEWND)
		{
			const auto hWndMenu = std::bit_cast<HWND>(wParam);
			::GetClassName(hWndMenu, szClassName, 7);

			if (::lstrcmp(L"#32768", szClassName) == 0)
			{
				//_current->_menuStack.emplace_back(hWndMenu);

				// Subclass to a flat-looking menu
				SetWindowSubclass(hWndMenu, MenuSuperProc, 0, static_cast<DWORD_PTR>(0));
				SetWindowPos(hWndMenu, HWND_TOP, 0, 0, 0, 0,
				             SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED |
				             SWP_DRAWFRAME);
				//current->m_rcButton = {};
			}
		}
		else if (nCode == HCBT_DESTROYWND)
		{
			/*const auto hWndMenu = std::bit_cast<HWND>(wParam);
			::GetClassName(hWndMenu, szClassName, 7);
			if (::lstrcmp(L"#32768", szClassName) == 0)
			{
				df::assert_true(hWndMenu == _current->_menuStack.back());
				_current->_menuStack.pop_back();
			}*/
		}
		else if (nCode < 0)
		{
			//lRet = CallNextHookEx(current->_hMenuHook, nCode, wParam, lParam);
		}
		return lRet;
	}

	int show_menu(HMENU hMenu, uint32_t menu_style, int x, int y, LPCRECT pr = nullptr)
	{
		df::scope_locked_inc sl2(df::command_active);

		//const auto prev_current = _current;
		//_current = this;
		_hMenuHook = ::SetWindowsHookEx(WH_CBT, menu_create_hook_proc, get_resource_instance, GetCurrentThreadId());

		const auto result = TrackPopupMenu(hMenu, menu_style, x, y, 0, m_hWnd, pr);

		UnhookWindowsHookEx(_hMenuHook);
		_hMenuHook = nullptr;
		//_current = prev_current;

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
			for (const auto& c : commands)
			{
				if (c)
				{
					auto text = str::utf8_to_utf16(c->text);

					if (c->menu)
					{
						const auto sub_items = c->menu();

						win32_menu sub_menu;

						if (sub_menu.CreatePopupMenu())
						{
							for (const auto& cc : sub_items)
							{
								if (cc)
								{
									const auto id = ++cmd_id;
									auto style = MF_OWNERDRAW;
									if (!cc->enable) style |= MF_DISABLED;
									if (cc->checked) style |= MF_CHECKED;
									const auto text = cc->text;
									sub_menu.AppendMenu(style, id, str::utf8_to_utf16(text).c_str());

									_menu_commands[id] = cc;
								}
								else
								{
									sub_menu.AppendMenu(MF_OWNERDRAW | MF_SEPARATOR);
								}
							}

							const auto id = ++cmd_id;
							const auto w = text;

							MENUITEMINFO mii;
							mii.cbSize = sizeof(MENUITEMINFO);
							mii.fMask = MIIM_ID | MIIM_SUBMENU | MIIM_TYPE | MIIM_DATA; // MIIM_DATA | MIIM_STATE
							mii.fType = MFT_OWNERDRAW;
							mii.fState = MFS_ENABLED;
							mii.hbmpChecked = nullptr;
							mii.hbmpUnchecked = nullptr;
							mii.dwTypeData = const_cast<LPWSTR>(w.c_str());
							mii.cch = 0;
							mii.hSubMenu = sub_menu.Detach();
							mii.dwItemData = id;
							mii.hbmpItem = nullptr;
							mii.wID = id;
							menu.InsertMenuItem(menu.GetMenuItemCount(), TRUE, &mii);
							_menu_commands[id] = c;
						}
					}
					else
					{
						const auto id = ++cmd_id;
						auto style = MF_OWNERDRAW;
						if (!c->enable) style |= MF_DISABLED;
						if (c->checked) style |= MF_CHECKED;

						menu.AppendMenu(style, id, text.c_str());

						_menu_commands[id] = c;
					}
				}
				else
				{
					menu.AppendMenu(MF_OWNERDRAW | MF_SEPARATOR);
				}
			}

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
			store->write({}, s_window_rect, {(const uint8_t*)(&wp), wp.length});
		}
	}
};


LRESULT control_host_impl::on_system_device_change(uint32_t /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/)
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

	for (const auto& unk : _unknown_words)
	{
		if (unk.calc_bounds(*this).contains({client_loc.x, client_loc.y}))
		{
			selected_word = unk;
			found_error = true;
			break;
		}
	}

	const int ID_SPELLCHECK_ADD = 1000;
	const int ID_SPELLCHECK_OPT0 = 2000;

	if (found_error)
	{
		win32_menu popup;

		if (popup.CreatePopupMenu())
		{
			// Append the suggestions, if there are any
			auto suggestions = spell.suggest(selected_word.word);
			if (suggestions.size() > 8) suggestions.resize(8);

			uint32_t item_id = ID_SPELLCHECK_OPT0;
			df::hash_map<unsigned, std::u8string> opt_map;

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
			                 str::utf8_to_utf16(str::format(tt.menu_add_fmt, selected_word.word)).c_str());
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
				for (const auto& c : _children)
				{
					if (c.second->is_radio)
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

LRESULT control_host_impl::on_window_min_max_info(uint32_t /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam) const
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
		SetWindowRgn(m_hWnd, rgn.is_empty() ? nullptr : CreateRectRgnIndirect(&_window_rgn), TRUE);
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
		//MARGINS empty_margins = { -1 };
		DwmExtendFrameIntoClientArea(m_hWnd, &empty_margins);
		const auto at = DWMNCRP_ENABLED;
		DwmSetWindowAttribute(m_hWnd, DWMWA_NCRENDERING_POLICY, &at, sizeof(DWORD));
	}

	df::log(__FUNCTION__, str::format(u8"composition_enabled {}", _composition_enabled));

	update_region();
}

LRESULT control_host_impl::on_dwm_composition_changed(uint32_t, WPARAM wParam, LPARAM lParam)
{
	handle_composition_changed();
	return 0;
}

LRESULT control_host_impl::on_window_nc_hit_test(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam)
{
	if (_is_app_frame)
	{
		POINT loc = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
		ScreenToClient(m_hWnd, &loc);

		RECT rc;
		GetClientRect(m_hWnd, &rc);

		const auto scale_factor = _draw_ctx ? _draw_ctx->scale_factor : 1.0;
		const auto border_thickness = df::round(nonclient_border_thickness * scale_factor);

		enum { left = 1, top = 2, right = 4, bottom = 8 };
		int hit = 0;
		if (loc.x < rc.left + border_thickness) hit |= left;
		if (loc.x > rc.right - border_thickness) hit |= right;
		if (loc.y < rc.top + border_thickness) hit |= top;
		if (loc.y > rc.bottom - border_thickness) hit |= bottom;

		if (hit & top && hit & left) return HTTOPLEFT;
		if (hit & top && hit & right) return HTTOPRIGHT;
		if (hit & bottom && hit & left) return HTBOTTOMLEFT;
		if (hit & bottom && hit & right) return HTBOTTOMRIGHT;
		if (hit & left) return HTLEFT;
		if (hit & top) return HTTOP;
		if (hit & right) return HTRIGHT;
		if (hit & bottom) return HTBOTTOM;

		if (loc.x >= _extent.cx - ui::cx_resize_handle && loc.y >= _extent.cy - ui::cx_resize_handle)
		{
			return HTBOTTOMRIGHT;
		}

		/*auto ht = DefWindowProc(m_hWnd, uMsg, wParam, lParam);

		if (!_is_full_screen && ht == HTCLIENT)
		{
			pointi point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			ScreenToClient(&point);

			if (point.x >= _extent.cx - 20 && point.y >= _extent.cy - 20)
			{
				ht = HTBOTTOMRIGHT;
			}
			else
			{
				ht = HTCAPTION;
			}
		}*/

		/*if (PtInRect(&_sys_button_close.close, loc))
			return HTCLOSE;
		if (PtInRect(&_sys_button_max.maximize, loc))
			return HTMAXBUTTON;
		if (PtInRect(&_sys_button_min.minimize, loc))
			return HTMINBUTTON;*/

		return HTCAPTION;
	}

	auto ht = DefWindowProc(m_hWnd, uMsg, wParam, lParam);

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

LRESULT control_host_impl::on_window_nc_activate(uint32_t, WPARAM wParam, LPARAM)
{
	// DefWindowProc won't repaint the window border if lParam (normally a HRGN) is -1.
	// This is recommended in: https://blogs.msdn.microsoft.com/wpfsdk/2008/09/08/custom-window-chrome-in-wpf/ 
	return DefWindowProcW(m_hWnd, WM_NCACTIVATE, wParam, -1);
}

static bool has_autohide_appbar(UINT edge, RECT mon)
{
	APPBARDATA ad = {sizeof(APPBARDATA), nullptr, 0, edge, mon, 0};
	return SHAppBarMessage(ABM_GETAUTOHIDEBAREX, &ad);
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

const float kDefaultDPI = 96.f;

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

float GetScalingFactorFromDPI(int dpi)
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
int GetPerMonitorDPI(HMONITOR monitor)
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


bool MonitorHasAutohideTaskbarForEdge(UINT edge, HMONITOR monitor)
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

LRESULT control_host_impl::on_window_nc_calc_size(uint32_t, WPARAM wParam, LPARAM lParam)
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
			? &(reinterpret_cast<NCCALCSIZE_PARAMS*>(l_param)->rgrc[0])
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
			if (MonitorHasAutohideTaskbarForEdge(ABE_LEFT, monitor))
				client_rect->left += kAutoHideTaskbarThicknessPx;

			if (MonitorHasAutohideTaskbarForEdge(ABE_TOP, monitor))
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
				//}
			}
			if (MonitorHasAutohideTaskbarForEdge(ABE_RIGHT, monitor))
				client_rect->right -= kAutoHideTaskbarThicknessPx;
			if (MonitorHasAutohideTaskbarForEdge(ABE_BOTTOM, monitor))
				client_rect->bottom -= kAutoHideTaskbarThicknessPx;
		}
	}

	return 0;
}

LRESULT control_host_impl::on_window_nc_paint(const uint32_t uMsg, const WPARAM wParam, const LPARAM lParam)
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

	df::hash_map<uintptr_t, std::shared_ptr<ui::command>> _commands;
	ui::toolbar_styles _styles;
	control_host_impl* _parent;
	owner_context_ptr _ctx;

	HIMAGELIST create_image_list()
	{
		const auto icon_cxy = calc_icon_cxy(_ctx->scale_factor);
		return ImageList_Create(icon_cxy, icon_cxy, ILC_COLOR, 0, 0);
	}

	void update_button_size()
	{
		const auto has_defined_button_extent = !_styles.button_extent.is_empty();

		if (has_defined_button_extent)
		{
			SetButtonSize(df::round(_styles.button_extent.cx * _ctx->scale_factor),
			              df::round(_styles.button_extent.cy * _ctx->scale_factor));
			AutoSize();
		}
		else
		{
			AutoSize();
		}
	}

	void create(HWND parent, const ui::toolbar_styles& styles, const std::vector<ui::command_ptr>& buttons,
	            uintptr_t toolbar_id)
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
					b->menu ? BTNS_DROPDOWN : 0);

				TBBUTTON bb = {image, static_cast<int>(id), TBSTATE_ENABLED, static_cast<uint8_t>(button_style), 0, 0};
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

		SetFont(m_hWnd, _ctx->dialog);
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

	void AutoSize()
	{
		df::assert_true(IsWindow(m_hWnd));
		::SendMessage(m_hWnd, TB_AUTOSIZE, 0, 0L);
	}

	BOOL SetButtonSize(int cx, int cy)
	{
		df::assert_true(IsWindow(m_hWnd));
		return static_cast<BOOL>(::SendMessage(m_hWnd, TB_SETBUTTONSIZE, 0, MAKELPARAM(cx, cy)));
	}

	BOOL SetButtonInfo(int nID, LPTBBUTTONINFO lptbbi)
	{
		df::assert_true(IsWindow(m_hWnd));
		return static_cast<BOOL>(::SendMessage(m_hWnd, TB_SETBUTTONINFO, nID, (LPARAM)lptbbi));
	}

	void SetButtonStructSize(int nSize = sizeof(TBBUTTON))
	{
		df::assert_true(IsWindow(m_hWnd));
		::SendMessage(m_hWnd, TB_BUTTONSTRUCTSIZE, nSize, 0L);
	}

	HIMAGELIST SetImageList(HIMAGELIST hImageList, int nIndex = 0)
	{
		df::assert_true(IsWindow(m_hWnd));
		return std::bit_cast<HIMAGELIST>(::SendMessage(m_hWnd, TB_SETIMAGELIST, nIndex, (LPARAM)hImageList));
	}

	BOOL AddButtons(int nNumButtons, LPTBBUTTON lpButtons)
	{
		df::assert_true(IsWindow(m_hWnd));
		return static_cast<BOOL>(::SendMessage(m_hWnd, TB_ADDBUTTONS, nNumButtons, (LPARAM)lpButtons));
	}

	void destroy() override
	{
		_commands.clear();
		DestroyWindow(m_hWnd);
	}

	sizei measure_toolbar()
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

	sizei measure_toolbar(int cx) override
	{
		SIZE result{cx, 0};

		if (_styles.xTBSTYLE_WRAPABLE)
		{
			SetWindowPos(m_hWnd, nullptr, 0, 0, cx, 500, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
			AutoSize();
			const auto extent = measure_toolbar();
			result = {extent.cx, extent.cy};
		}
		else
		{
			::SendMessage(m_hWnd, TB_GETMAXSIZE, 0, (LPARAM)&result);
		}


		/*auto extent = measure_toolbar();

		if (_styles.xTBSTYLE_WRAPABLE && extent.cx > cx)
		{
			SetWindowPos(nullptr, 0, 0, cx, 500, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
			AutoSize();
			extent = measure_toolbar();
		}*/

		return {result.cx, result.cy};
	}

	int GetButtonCount() const
	{
		df::assert_true(IsWindow(m_hWnd));
		return static_cast<int>(::SendMessage(m_hWnd, TB_BUTTONCOUNT, 0, 0L));
	}

	BOOL GetButton(int nIndex, LPTBBUTTON lpButton) const
	{
		df::assert_true(IsWindow(m_hWnd));
		return static_cast<BOOL>(::SendMessage(m_hWnd, TB_GETBUTTON, nIndex, (LPARAM)lpButton));
	}

	int GetButtonInfo(int nID, LPTBBUTTONINFO lptbbi) const
	{
		df::assert_true(IsWindow(m_hWnd));
		return static_cast<int>(::SendMessage(m_hWnd, TB_GETBUTTONINFO, nID, (LPARAM)lptbbi));
	}

	void options_changed() override
	{
		SetFont(m_hWnd, _ctx->dialog);
		update_button_state(true, true);
	}

	void dpi_changed() override
	{
		SetFont(m_hWnd, _ctx->dialog);
		DeleteObject(SetImageList(create_image_list()));
		update_button_size();
	}

	void update_button_state(const bool resize, const bool text_changed) override
	{
		std::wstring w;
		bool is_autosize = false;
		const auto count = GetButtonCount();

		for (auto i = 0; i < count; ++i)
		{
			TBBUTTON button;
			GetButton(i, &button);

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

	hr = ac->Init(m_hWnd, &string_enum, nullptr, nullptr);

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

	const DWORD opts = ACO_UPDOWNKEYDROPSLIST | ACO_AUTOSUGGEST | ACO_AUTOAPPEND;
	hr = ac2->SetOptions(opts);

	if (FAILED(hr))
	{
		df::assert_true(!"ac2->SetOptions fail!");
		return false;
	}

	return true;
}

ui::edit_ptr control_host_impl::create_edit(const ui::edit_styles& styles, const std::u8string_view text,
                                            std::function<void(const std::u8string&)> changed)
{
	const auto id = alloc_ids();
	auto result = std::make_shared<edit_impl>(styles, this, _gdi_ctx);
	result->Create(m_hWnd, text, id);
	SetFont(result->m_hWnd, _gdi_ctx->font(styles.font));
	result->changed = std::move(changed);

	if (styles.file_system_auto_complete)
	{
		SHAutoComplete(result->m_hWnd, SHACF_FILESYS_DIRS | SHACF_FILESYSTEM);
	}
	else if (!styles.auto_complete_list.empty())
	{
		result->string_enum.load(styles.auto_complete_list);
		result->init_auto_complete_list();
	}

	_children[id] = result;
	return result;
}

ui::trackbar_ptr control_host_impl::create_slider(int min, int max, std::function<void(int, bool)> changed)
{
	const auto slider_id = alloc_ids();
	auto result = std::make_shared<trackbar_impl>(std::move(changed), _gdi_ctx);
	result->Create(m_hWnd, {}, nullptr, WS_CHILD | WS_TABSTOP | WS_CLIPCHILDREN, 0, slider_id);
	SetFont(result->m_hWnd, _gdi_ctx->dialog);
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
	const auto dw_ex_style = WS_EX_CONTROLPARENT; // | WS_EX_COMPOSITED;

	if (result->Create(m_hWnd, {}, dw_style, dw_ex_style) == nullptr)
	{
		return nullptr;
	}

	result->_is_popup = is_popup;
	SetFont(result->m_hWnd, _gdi_ctx->dialog);
	return result;
}


ui::button_ptr control_host_impl::create_button(const std::u8string_view text, std::function<void()> invoke,
                                                const bool default_button)
{
	return create_button(icon_index::none, text, {}, std::move(invoke), default_button);
}

ui::button_ptr control_host_impl::create_button(icon_index icon, const std::u8string_view title,
                                                const std::u8string_view details, std::function<void()> invoke,
                                                const bool default_button)
{
	auto style = WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON | BS_TEXT | BS_NOTIFY;
	if (default_button) style |= BS_DEFPUSHBUTTON;

	const auto w = str::utf8_to_utf16(title);
	const auto details_w = str::utf8_to_utf16(details);
	auto result = std::make_shared<button_impl>(_gdi_ctx);
	const auto id = alloc_ids();

	result->Create(m_hWnd, w.c_str(), style, 0, id);
	SetFont(result->m_hWnd, _gdi_ctx->dialog);
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

ui::button_ptr control_host_impl::create_check_button(bool val, const std::u8string_view text, const bool is_radio,
                                                      std::function<void(bool)> changed)
{
	auto* const font = _gdi_ctx->dialog;
	const auto style = WS_CHILD | WS_TABSTOP | BS_NOTIFY | (is_radio ? BS_AUTORADIOBUTTON : BS_AUTOCHECKBOX);
	const auto w = str::utf8_to_utf16(text);
	auto result = std::make_shared<button_impl>(_gdi_ctx);
	const auto id = alloc_ids();

	result->Create(m_hWnd, w.c_str(), style, 0, id);
	SetFont(result->m_hWnd, font);
	result->SetCheck(val ? 1 : 0);
	result->is_radio = is_radio;

	_children[id] = result;

	if (changed)
	{
		result->_invoke = [result, changed = std::move(changed)]()
		{
			changed((result->GetCheck() & BST_CHECKED) != 0);
		};
	}

	return result;
}

ui::web_window_ptr control_host_impl::create_web_window(const std::u8string_view start_url, ui::web_events* events)
{
	auto result = std::make_shared<web_window_impl>(start_url, events);
	//const auto dw_style = WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
	//const auto id = alloc_ids();
	result->Create(m_hWnd);
	//_children[id] = result;
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
		}
	}

	return dump_successful;
}

static std::weak_ptr<ui::app> g_app;
static std::weak_ptr<win32_app> g_app_impl;

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

void ui_wait_for_signal(platform::thread_event& te, const uint32_t timeout_ms, const std::function<bool(LPMSG m)>& cb)
{
	df::assert_true(ui::is_ui_thread());

	const auto app = g_app_impl.lock();

	if (app)
	{
		app->ui_wait_for_signal({te}, false, timeout_ms, cb);
	}
}

uint32_t platform::wait_for_timeout = WAIT_TIMEOUT - WAIT_OBJECT_0;

uint32_t platform::wait_for(const std::vector<std::reference_wrapper<thread_event>>& events, const uint32_t timeout_ms,
                            const bool wait_all)
{
	const auto max_events = 64;
	df::assert_true(events.size() < max_events);

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
		HANDLE handles[max_events];
		for (auto i = 0u; i < events.size(); ++i) handles[i] = std::any_cast<HANDLE>(events[i].get()._h);
		result = WaitForMultipleObjects(static_cast<uint32_t>(events.size()), handles, wait_all,
		                                timeout_ms ? timeout_ms : INFINITE) - WAIT_OBJECT_0;
	}

	return result;
}

bool ui::is_ui_thread()
{
	return ui_thread_id == platform::current_thread_id();
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

static void show_fatal_error(const std::u8string_view message)
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


static void register_restart()
{
	const auto restart_cmd_line_w = str::utf8_to_utf16(restart_cmd_line);
	static WCHAR wsCommandLine[RESTART_MAX_CMD_LINE];
	wcscpy_s(wsCommandLine, restart_cmd_line_w.c_str());
	const auto hr = RegisterApplicationRestart(wsCommandLine, RESTART_NO_PATCH | RESTART_NO_REBOOT);
	df::assert_true(SUCCEEDED(hr));
}

static void unregister_restart()
{
	UnregisterApplicationRecoveryCallback();
	UnregisterApplicationRestart();
}

static DWORD WINAPI recover_callback(PVOID pContext)
{
	df::log(__FUNCTION__, u8"*** recover_callback");

	BOOL bCanceled = FALSE;
	ApplicationRecoveryInProgress(&bCanceled);

	if (bCanceled)
	{
		df::log(__FUNCTION__, u8"Recovery was canceled by the user.");
	}

	const auto app = g_app.lock();

	if (app)
	{
		app->save_recovery_state();
	}

	df::close_log();

	ApplicationRecoveryFinished((bCanceled) ? FALSE : TRUE);
	return 0;
}


static void setup_restart()
{
	register_restart();
	const auto hr = RegisterApplicationRecoveryCallback(recover_callback, nullptr, RECOVERY_DEFAULT_PING_INTERVAL, 0);
	df::assert_true(SUCCEEDED(hr));
}

//
//STDAPI SetProcessDpiAwareness(
//	_In_ PROCESS_DPI_AWARENESS value);

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPWSTR lpCmdLine, int nCmdShow)
{
	get_resource_instance = hInstance;

	const int result = 0;
	const auto app_impl = std::make_shared<win32_app>();
	ui::app_ptr app;

	try
	{
		//SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);

		init_color_styles();
		unhandled_exception_filter exceptions;

		app = app_impl->_app = create_app(app_impl);
		g_app = app;
		g_app_impl = app_impl;

		if (!app->pre_init())
		{
			return 0;
		}

		resources.init(get_resource_instance);

		if (const HMODULE hUser32 = GetModuleHandle(L"USER32"))
		{
			Symbol(hUser32, ptrEnableNonClientDpiScaling, "EnableNonClientDpiScaling");
			Symbol(hUser32, pfnGetDpiForSystem, "GetDpiForSystem");
			Symbol(hUser32, pfnGetDpiForWindow, "GetDpiForWindow");
			Symbol(hUser32, ptrGetSystemMetricsForDpi, "GetSystemMetricsForDpi");
			Symbol(hUser32, ptrGetWindowDpiAwarenessContext, "GetWindowDpiAwarenessContext");
			Symbol(hUser32, ptrAreDpiAwarenessContextsEqual, "AreDpiAwarenessContextsEqual");
		}

		WSADATA wsaData;

		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		{
			show_fatal_error(tt.error_winsock_failed);
			return 0;
		}

		df::start_time = platform::now();
		platform::set_thread_description(u8"main");
		ui_thread_id = platform::current_thread_id();

		auto hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

		if (FAILED(hr))
		{
			show_fatal_error(tt.error_ole_failed);
			return 0;
		}

		hr = OleInitialize(nullptr);

		if (FAILED(hr))
		{
			show_fatal_error(tt.error_ole_failed);
			return 0;
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

		const INITCOMMONCONTROLSEX iccx = {sizeof(INITCOMMONCONTROLSEX), ICC_BAR_CLASSES};

		if (!InitCommonControlsEx(&iccx))
		{
			show_fatal_error(tt.error_windows_common_controls_failed);
			return 0;
		}

		app_impl->_f = std::make_shared<factories>();

		if (!app_impl->_f->init(setting.use_gpu))
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
		OleUninitialize();
		CoUninitialize();
	}
	catch (std::exception& e)
	{
		df::log(__FUNCTION__, e.what());
		show_fatal_error(str::utf8_cast(e.what()));
	}

#ifdef _DEBUG
	//_CrtDumpMemoryLeaks(); 
#endif // _DEBUG

	WSACleanup();

	app->final_exit();

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

//control_host_impl* control_host_impl::_current = nullptr;

LRESULT control_host_impl::on_window_timer(uint32_t, WPARAM, LPARAM)
{
	ui::ticks_since_last_user_action += 1;

	if (!_hover_command_bounds.is_empty())
	{
		POINT pos;
		GetCursorPos(&pos);
		if (!_hover_command_bounds.contains({pos.x, pos.y}))
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

	for (auto& h : _folder_changes)
	{
		app_thread_events.emplace_back(h);
		app_event_actions.emplace_back([this, h]()
		{
			_app->folder_changed();
			FindNextChangeNotification(h);
		});
	}

	app_thread_events.emplace_back(std::any_cast<HANDLE>(_idle_event._h));
	app_event_actions.emplace_back([this] { idle(); });

	df::assert_true(app_thread_events.size() == app_event_actions.size());
}

void win32_app::tick()
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
	for (const auto& h : _folder_changes)
	{
		FindCloseChangeNotification(h);
	}

	_folder_changes.clear();

	for (const auto& path : folders_paths)
	{
		const auto filter = FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME
			| FILE_NOTIFY_CHANGE_DIR_NAME;
		const auto h = ::FindFirstChangeNotification(platform::to_file_system_path(path).c_str(), FALSE, filter);

		if (h != INVALID_HANDLE_VALUE && h != nullptr)
		{
			_folder_changes.emplace_back(h);
		}
	}

	update_event_handles();
}

void win32_app::destroy()
{
	for (const auto& h : _folder_changes)
	{
		FindCloseChangeNotification(h);
	}

	_folder_changes.clear();

	CloseHandle(_timer_handle);
}

void win32_app::idle()
{
	_idle_event.reset();
	_app->idle();
}

int win32_app::ui_message_loop()
{
	SystemParametersInfo(SPI_GETSCREENSAVEACTIVE, 0, &_dwScreenSaverSetting, 0);
	SystemParametersInfo(SPI_GETLOWPOWERACTIVE, 0, &_dwLowPowerSetting, 0);
	SystemParametersInfo(SPI_GETPOWEROFFACTIVE, 0, &_dwPowerOffSetting, 0);

	_timer_handle = CreateWaitableTimer(nullptr, FALSE, nullptr);

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
			app_event_actions[n]();
		}
		else
		{
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
		}

		idle();
	}

	enable_screen_saver(true);
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
		thread_events.emplace_back(std::any_cast<HANDLE>(e.get()._h));
		event_actions.emplace_back([&signal_set] { signal_set = true; });
	}

	thread_events.insert(thread_events.end(), app_thread_events.begin(), app_thread_events.end());
	event_actions.insert(event_actions.end(), app_event_actions.begin(), app_event_actions.end());

	MSG msg;
	msg.message = WM_NULL; // anything that isn't WM_QUIT

	while (!df::is_closing && !signal_set)
	{
		const auto n = MsgWaitForMultipleObjects(static_cast<uint32_t>(thread_events.size()), thread_events.data(),
		                                         wait_all, timeout_ms ? timeout_ms : INFINITE, QS_ALLINPUT);

		if (n > event_actions.size())
		{
			return result; // unexpected failure
		}
		if (n < event_actions.size())
		{
			event_actions[n]();
			result = n - static_cast<uint32_t>(app_thread_events.size() - WAIT_OBJECT_0);
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

	return result;
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
		const LARGE_INTEGER liDueTime = {0};
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
	WINDOWPLACEMENT wp;
	memset(&wp, 0, sizeof(WINDOWPLACEMENT));
	wp.length = sizeof(WINDOWPLACEMENT);
	wp.showCmd = SW_SHOW;

	{
		WINDOWPLACEMENT wp2;
		size_t dw = sizeof(wp2);

		if (store->read({}, s_window_rect, std::bit_cast<uint8_t*>(&wp2), dw))
		{
			wp2.length = static_cast<uint32_t>(dw);
			memcpy_s(&wp, sizeof(wp), &wp2, sizeof(wp2));

			// Never start minimized!
			if (wp.showCmd == SW_SHOWMINIMIZED)
				wp.showCmd = SW_SHOW;

			const HDC hdc_screen = CreateDC(L"DISPLAY", nullptr, nullptr, nullptr);
			const win_rect screenRect(0, 0, GetDeviceCaps(hdc_screen, HORZRES), GetDeviceCaps(hdc_screen, VERTRES));

			// Never start bigger than the screen?
			if (screenRect.intersects(wp.rcNormalPosition))
			{
				wp.rcNormalPosition = screenRect.intersection(wp.rcNormalPosition);
			}

			DeleteDC(hdc_screen);
		}
	}

	ui::color_style colors = {
		ui::style::color::toolbar_background, ui::style::color::view_text, ui::style::color::view_selected_background
	};

	const auto monitor = MonitorFromRect(&wp.rcNormalPosition, MONITOR_DEFAULTTONEAREST);
	const auto default_scale_fac = GetScalingFactorFromDPI(GetPerMonitorDPI(monitor));
	auto ctx = std::make_shared<owner_context>(default_scale_fac);
	auto result = std::make_shared<control_host_impl>(ctx, host, _app, this, true, colors);
	const auto window_style = WS_CAPTION | WS_OVERLAPPED | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_SYSMENU |
		WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
	const auto window_ex_style = WS_EX_WINDOWEDGE | WS_EX_APPWINDOW; // | WS_EX_LAYERED | WS_EX_COMPOSITED;

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
	SendMessage(result->m_hWnd, WM_SETICON, ICON_BIG, std::bit_cast<LPARAM>(resources.diffractor_64));
	SendMessage(result->m_hWnd, WM_SETICON, ICON_SMALL, std::bit_cast<LPARAM>(resources.diffractor_32));

	SetFont(result->m_hWnd, ctx->dialog);
	WTSRegisterSessionNotification(result->m_hWnd, NOTIFY_FOR_THIS_SESSION);
	result->cmd_show = wp.showCmd;

	_frame = result;
	ui_app_wnd = result->m_hWnd;
	return result;
}

void win32_app::enable_screen_saver(const bool enable)
{
	if (_enable_screen_saver != enable)
	{
		if (enable)
		{
			SystemParametersInfo(SPI_SETSCREENSAVEACTIVE, _dwScreenSaverSetting, nullptr, SPIF_SENDWININICHANGE);
			SystemParametersInfo(SPI_SETLOWPOWERACTIVE, _dwLowPowerSetting, nullptr, 0);
			SystemParametersInfo(SPI_SETPOWEROFFACTIVE, _dwPowerOffSetting, nullptr, 0);

			SetThreadExecutionState(ES_CONTINUOUS);
		}
		else
		{
			SystemParametersInfo(SPI_SETSCREENSAVEACTIVE, FALSE, nullptr, SPIF_SENDWININICHANGE);
			SystemParametersInfo(SPI_SETLOWPOWERACTIVE, 0, nullptr, 0);
			SystemParametersInfo(SPI_SETPOWEROFFACTIVE, 0, nullptr, 0);

			SetThreadExecutionState(ES_DISPLAY_REQUIRED | ES_CONTINUOUS);
		}
	}
}
