// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: The window plumbing platform_win_ui.cpp and platform_win_controls.cpp share - geometry and
// GDI helpers, the cursors and fonts owner_context hands out, the window and control base classes,
// the interface a native control uses to reach its host, and the factories the host builds controls
// with. Windows-only and internal to those two files.

#pragma once

#include "platform_win.h"
#include "platform_win_res.h"

#include <CommCtrl.h>

static constexpr auto ui_base_icon_cxy = 18;
static constexpr auto ui_element_padding = 8;
static constexpr auto ui_focus_padding = 2;
static constexpr auto ui_corner_radius = 6;
static constexpr auto ui_button_padding = 8;

inline int calc_icon_cxy(const double scale_factor)
{
	return df::round(ui_base_icon_cxy * scale_factor);
}

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

inline std::wstring window_text_w(const HWND h)
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

inline std::string window_text(const HWND h)
{
	return str::utf16_to_utf8(window_text_w(h));
}

inline void fill_rect(const HDC hdc, const DWORD clr, const win_rect& bounds)
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

inline void SetFont(
	const HWND hwnd,
	_In_ HFONT hFont,
	_In_ const BOOL bRedraw = TRUE) noexcept
{
	df::assert_true(IsWindow(hwnd));
	::SendMessage(hwnd, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(bRedraw, 0));
}

inline HFONT GetFont(const HWND hwnd) noexcept
{
	df::assert_true(IsWindow(hwnd));
	return (HFONT)::SendMessage(hwnd, WM_GETFONT, 0, 0);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline HANDLE load_icon_font()
{
	auto font_data = load_resource(IDF_ICONS, L"BINARY");
	if (font_data.empty())
	{
		return nullptr;
	}
	DWORD nFonts = 0;
	return AddFontMemResourceEx(font_data.data(), static_cast<uint32_t>(font_data.size()), nullptr, &nFonts);
}

inline HFONT create_font(const ui::style::font_face type, const int base_font_size, const bool clear_type = false)
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
		lf.lfHeight = -df::mul_div(base_font_size, icon_font_scale_num, icon_font_scale_den);
		wcscpy_s(lf.lfFaceName, LF_FACESIZE, L"FluentSystemIcons-Resizable");
		break;
	case ui::style::font_face::small_icons:
		lf.lfHeight = -df::mul_div(base_font_size, icon_font_scale_num * 10, icon_font_scale_den * 16);
		wcscpy_s(lf.lfFaceName, LF_FACESIZE, L"FluentSystemIcons-Resizable");
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



inline int gdi_text_line_height(const HDC hdc, const HFONT font)
{
	if (!hdc) return 0;

	auto* const old_font = SelectObject(hdc, font);
	TEXTMETRIC tm = {};
	GetTextMetrics(hdc, &tm);
	SelectObject(hdc, old_font);

	return tm.tmHeight;
}

inline int gdi_text_line_height(const HWND hwnd, const HFONT font)
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


inline void draw_gradient(const HDC dc, const recti r, const DWORD c1, const DWORD c2)
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
inline void fill_round_rect(uint32_t* const bits, const int w, const int h, const float radius,
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


// The base size fonts are created at, before the monitor's scale is applied.
extern int global_base_font_size;

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

inline void draw_icon(const HDC hdc, const owner_context_ptr& ctx, const icon_index icon, const recti bounds,
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

inline void fill_solid_rect(const HDC hdc, const LPCRECT lpRect, const COLORREF clr)
{
	const auto clr_old = SetBkColor(hdc, clr);

	if (clr_old != CLR_INVALID)
	{
		::ExtTextOut(hdc, 0, 0, ETO_OPAQUE, lpRect, nullptr, 0, nullptr);
		SetBkColor(hdc, clr_old);
	}
}

inline void fill_solid_rect(const HDC hdc, const int x, const int y, const int cx, const int cy, const COLORREF clr)
{
	const RECT r = {x, y, x + cx, y + cy};
	fill_solid_rect(hdc, &r, clr);
}

inline void frame_rect(const HDC hdc, const int x, const int y, const int cx, const int cy, const COLORREF clrTopLeft,
                       COLORREF clrBottomRight = 0,
                       const int width = 1)
{
	if (clrBottomRight == 0) clrBottomRight = clrTopLeft;

	fill_solid_rect(hdc, x, y, cx - width, width, clrTopLeft);
	fill_solid_rect(hdc, x, y, width, cy - width, clrTopLeft);
	fill_solid_rect(hdc, x + cx, y, -width, cy, clrBottomRight);
	fill_solid_rect(hdc, x, y + cy, cx, -width, clrBottomRight);
}


inline void frame_rect(const HDC hdc, const LPCRECT lpRect, const COLORREF clrTopLeft,
                       const COLORREF clrBottomRight = 0, int width = 1)
{
	frame_rect(hdc, lpRect->left, lpRect->top, lpRect->right - lpRect->left,
	           lpRect->bottom - lpRect->top, clrTopLeft, clrBottomRight);
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

using control_base2_ptr = std::shared_ptr<control_base2>;

// Owns a menu handle, destroying it unless detached.
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

// What a native control asks of the window hosting it.
class native_control_host
{
public:
	virtual ~native_control_host() = default;

	// Tracks a popup menu. With TPM_RETURNCMD it answers the chosen command id.
	virtual int show_menu(HMENU hMenu, uint32_t menu_style, int x, int y, LPCRECT pr = nullptr) = 0;
	virtual void show_menu(recti button_bounds, const std::vector<ui::command_ptr>& commands) = 0;

	// Where the command under the pointer is, so its tooltip is dismissed once the pointer leaves it.
	virtual void hover_command_bounds(recti bounds) = 0;
};

// A native control as what its caller asked for and as what the host dispatches commands to.
template <typename UI>
struct native_control
{
	std::shared_ptr<UI> control;
	control_base2_ptr child;
};

native_control<ui::edit> create_native_edit(native_control_host& host, HWND parent, int id,
                                            const owner_context_ptr& ctx, const ui::edit_styles& styles,
                                            std::string_view text, std::function<void(const std::string&)> changed);
native_control<ui::trackbar> create_native_slider(HWND parent, int id, const owner_context_ptr& ctx, int min,
                                                  int max, std::function<void(int, bool)> changed);
native_control<ui::toolbar> create_native_toolbar(native_control_host& host, HWND parent, int toolbar_id,
                                                  const owner_context_ptr& ctx, const ui::toolbar_styles& styles,
                                                  const std::vector<ui::command_ptr>& buttons);
native_control<ui::button> create_native_button(HWND parent, int id, const owner_context_ptr& ctx, icon_index icon,
                                                std::string_view title, std::string_view details,
                                                std::function<void()> invoke, bool default_button);
native_control<ui::button> create_native_check_button(HWND parent, int id, const owner_context_ptr& ctx, bool val,
                                                      std::string_view text, bool is_radio, bool starts_group,
                                                      int radio_group, std::function<void(bool)> changed);
native_control<ui::date_time_control> create_native_date_time(HWND parent, int id, const owner_context_ptr& ctx,
                                                              df::date_t val,
                                                              std::function<void(df::date_t)> changed,
                                                              const ui::color_style& colors, bool include_time);
