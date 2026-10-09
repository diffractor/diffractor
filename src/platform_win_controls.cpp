// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: The native Windows controls a dialog or panel hosts - the edit box with its spelling and
// auto-complete, push, check and radio buttons, the slider, the date and time picker, and the owner-
// drawn toolbar, which reports the window's maximize button to Windows as the caption button snap
// layouts appear over - and the factories control_host_impl in platform_win_ui.cpp builds them with.

#include "pch.h"
#include "platform_win.h"
#include "platform_win_visual.h"
#include "platform_win_ui_internal.h"
#include "app_text.h"
#include "ui_elements.h"
#include "util_spell.h"
#include "platform_win_res.h"

#include <Shlwapi.h>
#include <Shobjidl.h>
#include <ShlGuid.h>

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

static void erase_toolbar_separators(const HWND tb, const HDC dc, const COLORREF bg_clr)
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
                                const COLORREF selected_clr, const bool caption_hover, const bool caption_pressed)
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
	// A maximize caption button gets non-client mouse messages, so the control never lights or
	// presses it; the toolbar tracks both and says so here.
	const bool is_hotlight = (item_state & ODS_HOTLIGHT) != 0 || caption_hover;
	const bool is_focus = is_hotlight && GetFocus() == tb;
	const bool is_checked = (button_info.fsState & TBSTATE_CHECKED) != 0;
	const bool is_pressed = (button_info.fsState & TBSTATE_PRESSED) != 0 || caption_pressed;
	const bool is_disabled = (button_info.fsState & TBSTATE_ENABLED) == 0;

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
			return platform::probe_edit_spelling_bounds(edit.m_hWnd, pos_start, pos_end);
		}
	};

	const ui::edit_styles _styles;
	std::vector<unknown_word> _unknown_words;
	icon_index _icon = icon_index::none;
	ui::color32 _background = ui::style::color::edit_background;
	native_control_host* _parent = nullptr;
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

	explicit edit_impl(ui::edit_styles styles, native_control_host* parent, const owner_context_ptr& ctx)
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
			spell().queue_load();
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

static POINT pos_from_char(const HWND hwnd, const uint32_t nChar)
{
	df::assert_true(IsWindow(hwnd));
	const DWORD dwRet = static_cast<DWORD>(::SendMessage(hwnd, EM_POSFROMCHAR, nChar, 0));
	return {GET_X_LPARAM(dwRet), GET_Y_LPARAM(dwRet)};
}

static int char_width(const HWND hwnd, const int char_pos)
{
	const auto text = window_text_w(hwnd);
	if (char_pos < 0 || char_pos >= static_cast<int>(text.size())) return 0;

	const auto dc = GetDC(hwnd);
	if (!dc) return 0;

	const auto old_font = SelectObject(dc, GetFont(hwnd));
	SIZE extent = {};
	GetTextExtentPoint32W(dc, text.data() + char_pos, 1, &extent);
	SelectObject(dc, old_font);
	ReleaseDC(hwnd, dc);
	return extent.cx;
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

static std::vector<platform::edit_spelling_range> calc_unknown_words(
	const std::wstring& text, const std::function<bool(std::string_view)>& is_word_valid)
{
	std::vector<platform::edit_spelling_range> result;

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

				if (!is_word_valid(word_a))
				{
					result.emplace_back(word_a, word_start, word_start + static_cast<int>(word.size()));
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

		if (!is_word_valid(word_a))
		{
			result.emplace_back(word_a, word_start, word_start + static_cast<int>(word.size()));
		}
	}

	return result;
}

std::vector<platform::edit_spelling_range> platform::probe_edit_spelling_ranges(
	const std::wstring& text, const std::function<bool(std::string_view)>& is_word_valid)
{
	return calc_unknown_words(text, is_word_valid);
}

recti platform::probe_edit_spelling_bounds(const HWND hwnd, const int pos_start, const int pos_end)
{
	const auto loc_start = pos_from_char(hwnd, pos_start);
	auto loc_end = pos_from_char(hwnd, pos_end);

	if (loc_end.x == -1 && pos_end > pos_start)
	{
		const auto loc_last = pos_from_char(hwnd, pos_end - 1);
		if (loc_last.x != -1)
		{
			loc_end = loc_last;
			loc_end.x += char_width(hwnd, pos_end - 1);
		}
	}

	if (loc_end.x == -1 || loc_start.x == -1) return {};

	const auto line_height = gdi_text_line_height(hwnd, GetFont(hwnd));
	return {loc_start.x, loc_start.y, loc_end.x, loc_start.y + line_height};
}

void edit_impl::update_spelling(const std::wstring& text)
{
	_unknown_words.clear();

	for (const auto& word : calc_unknown_words(text, [](const std::string_view word)
	     {
		     return spell().is_word_valid(word);
	     }))
	{
		add_unknown_word(word.word, word.pos_start, word.pos_end);
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
			// NM_KILLFOCUS, not WM_KILLFOCUS: a notification code is never a window message, so the
			// comparison never matched and the host was told the control gained focus but never lost it.
			if (date_id == id && NM_KILLFOCUS == pnmh->code) return on_focus(id, pnmh);
			if (time_id == id && NM_KILLFOCUS == pnmh->code) return on_focus(id, pnmh);
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
		BeginPaint(m_hWnd, &ps);
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
		const auto custom_menu_created = popup.CreatePopupMenu();

		if (platform::probe_edit_context_menu_route(found_error, custom_menu_created) ==
			platform::edit_context_menu_route::custom_spelling_menu)
		{
			// Append the suggestions, if there are any
			auto suggestions = spell().suggest(selected_word.word);
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
				spell().add_word(selected_word.word, [weak = weak_from_this(), hwnd = m_hWnd](
					                  const custom_dictionary_add_result result)
				{
					if (result.status == custom_dictionary_add_status::failed)
					{
						df::log(__FUNCTION__, result.message);
					}

					const auto edit = weak.lock();
					if (edit && edit->m_hWnd == hwnd && IsWindow(hwnd)) InvalidateRect(hwnd, nullptr, TRUE);
				});
				break;

			default:
				if (opt_map.contains(cmd_id))
				{
					select(selected_word.pos_start, selected_word.pos_end);
					replace_sel(opt_map[cmd_id], false);
				}
				break;
			}

			return 0;
		}
	}

	return DefSubclassProc(m_hWnd, uMsg, wParam, lParam);
}

platform::edit_context_menu_route platform::probe_edit_context_menu_route(const bool spelling_error_under_pointer,
                                                                          const bool custom_menu_created)
{
	return spelling_error_under_pointer && custom_menu_created
		       ? edit_context_menu_route::custom_spelling_menu
		       : edit_context_menu_route::native_edit_procedure;
}

bool platform::can_present_maximize_caption_button(const df::os_release release, const uint32_t root_style)
{
	const auto has_snap_layouts = release == df::os_release::windows_11 || release == df::os_release::windows_later;
	constexpr uint32_t resizable_with_maximize = WS_THICKFRAME | WS_MAXIMIZEBOX;
	return has_snap_layouts && (root_style & resizable_with_maximize) == resizable_with_maximize;
}

bool platform::presents_maximize_caption_button(const df::os_release release, const uint32_t root_style)
{
	return can_present_maximize_caption_button(release, root_style) && (root_style & WS_MAXIMIZE) == 0;
}

bool platform::caption_button_tracker::hover(const int id)
{
	if (hover_id == id) return false;
	hover_id = id;

	// A press belongs to the button it began on; resting anywhere else abandons it.
	if (pressed_id != id) pressed_id = 0;
	return true;
}

bool platform::caption_button_tracker::press(const int id)
{
	const auto changed = hover_id != id || pressed_id != id;
	hover_id = id;
	pressed_id = id;
	return changed;
}

bool platform::caption_button_tracker::leave()
{
	const auto changed = hover_id != 0 || pressed_id != 0;
	hover_id = 0;
	pressed_id = 0;
	return changed;
}

int platform::caption_button_tracker::release(const int id)
{
	const auto invoke_id = pressed_id != 0 && pressed_id == id ? id : 0;
	pressed_id = 0;

	// Maximizing or restoring moves the frame under the pointer, so the next move finds the button
	// again rather than this keeping one that may no longer be there.
	if (invoke_id != 0) hover_id = 0;
	return invoke_id;
}

class toolbar_impl final :
	public control_base_impl<toolbar_impl, ui::toolbar, win_base>,
	public control_base2,
	public std::enable_shared_from_this<toolbar_impl>
{
public:
	toolbar_impl(native_control_host* parent, const owner_context_ptr& ctx) : _parent(parent), _ctx(ctx)
	{
	}

	// Only modal dialogs call destroy(); the view control panels are rebuilt on every view switch
	// and simply drop their controls, so without this the image list built for each toolbar is
	// orphaned and the icon bitmaps accumulate for the whole session.
	~toolbar_impl() override
	{
		release_image_list();
	}

	// Held here rather than read back from the window. A host destroys its window, and every toolbar
	// in it, before it frees the control objects, so by this destructor there was no window left to
	// ask and the list leaked with every dialog that carried a toolbar.
	void release_image_list()
	{
		if (m_hWnd && IsWindow(m_hWnd)) SetImageList(nullptr);

		if (_image_list)
		{
			ImageList_Destroy(_image_list);
			_image_list = nullptr;
		}
	}

	// Replaces the list the toolbar draws from, releasing the one it replaces.
	void attach_image_list()
	{
		const auto previous = _image_list;
		_image_list = create_image_list();
		SetImageList(_image_list);
		if (previous) ImageList_Destroy(previous);
	}

	HIMAGELIST _image_list = nullptr;
	df::hash_map<uintptr_t, std::shared_ptr<ui::command>> _commands;
	ui::toolbar_styles _styles;
	native_control_host* _parent;
	owner_context_ptr _ctx;

	// Windows 11 offers snap layouts over a window's maximize button, and only over a button the
	// window reports as one, so the toolbar reports its maximize button as that caption button
	// whenever presents_maximize_caption_button allows. Windows then sends it non-client mouse
	// messages, which the control never tracks, so its hover and press are kept here.
	platform::caption_button_tracker _caption;
	bool _caption_leave_tracked = false;

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

		attach_caption_button();
		_ctx->set_window_font(m_hWnd, ui::style::font_face::dialog);
		SetButtonStructSize();
		attach_image_list();
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

	// The subclass holds a weak reference: a panel can drop its toolbar object before the window
	// goes, and the window can go before the object.
	void attach_caption_button()
	{
		auto* const self = new std::weak_ptr<toolbar_impl>(weak_from_this());

		if (!SetWindowSubclass(m_hWnd, caption_button_proc, 0, std::bit_cast<DWORD_PTR>(self)))
		{
			delete self;
		}
	}

	static LRESULT CALLBACK caption_button_proc(const HWND h, const UINT msg, const WPARAM wparam,
	                                            const LPARAM lparam, const UINT_PTR id, const DWORD_PTR ref)
	{
		auto* const weak = std::bit_cast<std::weak_ptr<toolbar_impl>*>(ref);

		if (msg == WM_NCDESTROY)
		{
			const auto result = DefSubclassProc(h, msg, wparam, lparam);
			RemoveWindowSubclass(h, caption_button_proc, id);
			delete weak;
			return result;
		}

		if (const auto self = weak->lock())
		{
			switch (msg)
			{
			case WM_NCHITTEST:
				if (self->caption_button_at(lparam) != 0) return HTMAXBUTTON;
				break;

			case WM_NCMOUSEMOVE:
				// Windows' own handling still follows; it does not draw this button.
				self->caption_hover(wparam == HTMAXBUTTON ? self->caption_button_at(lparam) : 0);
				break;

			case WM_NCMOUSELEAVE:
				self->caption_leave();
				break;

			// Passed on, these would run Windows' tracking of a caption button it does not draw.
			case WM_NCLBUTTONDOWN:
			case WM_NCLBUTTONDBLCLK:
				if (wparam == HTMAXBUTTON)
				{
					self->caption_press(self->caption_button_at(lparam));
					return 0;
				}
				break;

			case WM_NCLBUTTONUP:
				if (wparam == HTMAXBUTTON)
				{
					self->caption_release(self->caption_button_at(lparam));
					return 0;
				}
				break;

			default:
				break;
			}
		}

		return DefSubclassProc(h, msg, wparam, lparam);
	}

	// The command id of the enabled caption maximize button under a screen point while the frame
	// presents one, or zero.
	int caption_button_at(const LPARAM screen_point) const
	{
		static const auto release = windows_release();
		const auto root = GetAncestor(m_hWnd, GA_ROOT);

		if (!root || !platform::presents_maximize_caption_button(
			release, static_cast<uint32_t>(GetWindowLong(root, GWL_STYLE))))
		{
			return 0;
		}

		POINT loc{GET_X_LPARAM(screen_point), GET_Y_LPARAM(screen_point)};
		ScreenToClient(m_hWnd, &loc);

		const auto index = static_cast<int>(::SendMessage(m_hWnd, TB_HITTEST, 0, std::bit_cast<LPARAM>(&loc)));
		if (index < 0) return 0;

		TBBUTTON button = {};
		if (!GetButton(index, &button)) return 0;
		if ((button.fsState & TBSTATE_ENABLED) == 0 || (button.fsState & TBSTATE_HIDDEN) != 0) return 0;

		const auto found = _commands.find(button.idCommand);
		return found != _commands.end() && found->second && found->second->caption_maximize ? button.idCommand : 0;
	}

	void caption_hover(const int id)
	{
		// Without this Windows never reports the pointer leaving, and the button would stay lit.
		if (id != 0 && !_caption_leave_tracked)
		{
			TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE | TME_NONCLIENT, m_hWnd, HOVER_DEFAULT};
			_caption_leave_tracked = TrackMouseEvent(&tme) != FALSE;
		}

		if (_caption.hover(id)) InvalidateRect(m_hWnd, nullptr, FALSE);
	}

	void caption_leave()
	{
		_caption_leave_tracked = false;
		if (_caption.leave()) InvalidateRect(m_hWnd, nullptr, FALSE);
	}

	void caption_press(const int id)
	{
		if (_caption.press(id)) InvalidateRect(m_hWnd, nullptr, FALSE);
	}

	void caption_release(const int id)
	{
		const auto invoke_id = _caption.release(id);
		InvalidateRect(m_hWnd, nullptr, FALSE);
		if (invoke_id != 0) invoke_command(invoke_id);
	}

	// A click and a caption button release reach the command the same way.
	void invoke_command(const int id) const
	{
		const auto found = _commands.find(id);
		if (found == _commands.end()) return;

		const auto command = found->second;
		if (command && command->invoke) command->invoke();
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

	// True when at least one button reported a rectangle. TB_GETITEMRECT skips hidden buttons, so
	// this reflects the buttons actually laid out right now.
	bool measure_buttons(sizei& result) const
	{
		result = {0, 0};
		auto measured_any = false;
		const int count = static_cast<int>(::SendMessage(m_hWnd, TB_BUTTONCOUNT, 0, 0L));

		for (int i = 0; i < count; ++i)
		{
			win_rect r;
			if (::SendMessage(m_hWnd, TB_GETITEMRECT, i, std::bit_cast<LPARAM>(static_cast<LPRECT>(r))))
			{
				measured_any = true;
				result.cx = std::max(static_cast<int>(r.right), result.cx);
				result.cy = std::max(static_cast<int>(r.bottom), result.cy);
			}
		}

		return measured_any;
	}

	sizei measure_toolbar(const int cx) override
	{
		SIZE result{cx, 0};

		if (_styles.xTBSTYLE_WRAPABLE)
		{
			::SendMessage(m_hWnd, TB_GETIDEALSIZE, TRUE, std::bit_cast<LPARAM>(&result));
			return {result.cx, result.cy};
		}

		// TB_GETMAXSIZE answers from a size the toolbar cached before the current hidden state and
		// button text were applied, so a view switch or a maximize toggle would lay the bar out at
		// its previous width and clip it against the window edge.
		if (sizei measured; measure_buttons(measured)) return measured;

		::SendMessage(m_hWnd, TB_GETMAXSIZE, 0, (LPARAM)&result);
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
		attach_image_list();
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
		invoke_command(id);
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
				erase_toolbar_separators(from, pCustomDraw->hdc, colors.background);
				return CDRF_SKIPDEFAULT;
			}
			if (pCustomDraw->dwDrawStage == CDDS_ITEMPREPAINT)
			{
				const auto tb_cd = std::bit_cast<LPNMTBCUSTOMDRAW>(pCustomDraw);
				const auto found = _commands.find(tb_cd->nmcd.dwItemSpec);

				if (found != _commands.end())
				{
					const auto id = static_cast<int>(tb_cd->nmcd.dwItemSpec);
					draw_toolbar_button(found->second, _ctx, tb_cd, colors.background, colors.foreground,
					                    colors.selected, _caption.hover_id == id, _caption.pressed_id == id);
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
			_parent->hover_command_bounds(rc);
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

native_control<ui::edit> create_native_edit(native_control_host& host, const HWND parent, const int id,
                                            const owner_context_ptr& ctx, const ui::edit_styles& styles,
                                            const std::string_view text, std::function<void(const std::string&)> changed)
{
	auto result = std::make_shared<edit_impl>(styles, &host, ctx);
	result->Create(parent, text, id);
	ctx->set_window_font(result->m_hWnd, styles.font);
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

	return {result, result};
}

native_control<ui::trackbar> create_native_slider(const HWND parent, const int id, const owner_context_ptr& ctx,
                                                  const int min, const int max,
                                                  std::function<void(int, bool)> changed)
{
	auto result = std::make_shared<trackbar_impl>(std::move(changed), ctx);
	result->Create(parent, {}, nullptr, WS_CHILD | WS_TABSTOP | WS_CLIPCHILDREN, 0, id);
	ctx->set_window_font(result->m_hWnd, ui::style::font_face::dialog);
	result->set_range(min, max);
	return {result, result};
}

native_control<ui::toolbar> create_native_toolbar(native_control_host& host, const HWND parent, const int toolbar_id,
                                                  const owner_context_ptr& ctx, const ui::toolbar_styles& styles,
                                                  const std::vector<ui::command_ptr>& buttons)
{
	auto result = std::make_shared<toolbar_impl>(&host, ctx);
	result->create(parent, styles, buttons, toolbar_id);
	return {result, result};
}

platform::caption_button_probe platform::probe_toolbar_caption_button()
{
	class quiet_host final : public native_control_host
	{
	public:
		int show_menu(HMENU, uint32_t, int, int, LPCRECT) override { return 0; }
		void show_menu(recti, const std::vector<ui::command_ptr>&) override {}
		void hover_command_bounds(recti) override {}
	};

	caption_button_probe result;

	const auto frame = CreateWindowEx(0, L"STATIC", nullptr, WS_OVERLAPPEDWINDOW, 0, 0, 400, 200, nullptr, nullptr,
	                                  get_resource_instance, nullptr);
	if (!frame) return result;

	const df::scope_exit destroy_frame([frame] { DestroyWindow(frame); });
	result.offered = presents_maximize_caption_button(windows_release(),
	                                                  static_cast<uint32_t>(GetWindowLong(frame, GWL_STYLE)));

	const auto other = std::make_shared<ui::command>();
	const auto maximize = std::make_shared<ui::command>();
	maximize->caption_maximize = true;
	maximize->invoke = [&result] { ++result.invocations; };

	quiet_host host;
	ui::toolbar_styles styles;
	styles.button_extent = {40, 40};

	constexpr int toolbar_id = 100;
	const auto toolbar = std::make_shared<toolbar_impl>(&host, std::make_shared<owner_context>(1.0));
	toolbar->create(frame, styles, {other, maximize}, toolbar_id);
	if (!toolbar->m_hWnd) return result;

	const auto tb = toolbar->m_hWnd;
	SetWindowPos(tb, nullptr, 0, 0, 200, 40, SWP_NOZORDER | SWP_NOACTIVATE);
	result.maximize_id = toolbar_id + 2;

	const auto screen_center = [tb](const int id)
	{
		win_rect bounds;
		::SendMessage(tb, TB_GETRECT, id, std::bit_cast<LPARAM>(static_cast<LPRECT>(bounds)));
		POINT loc{(bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2};
		ClientToScreen(tb, &loc);
		return MAKELPARAM(loc.x, loc.y);
	};

	const auto on_maximize = screen_center(result.maximize_id);
	result.maximize_hit = static_cast<int>(::SendMessage(tb, WM_NCHITTEST, 0, on_maximize));
	result.other_hit = static_cast<int>(::SendMessage(tb, WM_NCHITTEST, 0, screen_center(toolbar_id + 1)));

	if (result.maximize_hit == HTMAXBUTTON)
	{
		::SendMessage(tb, WM_NCMOUSEMOVE, HTMAXBUTTON, on_maximize);
		result.hovered_id = toolbar->_caption.hover_id;
		::SendMessage(tb, WM_NCLBUTTONDOWN, HTMAXBUTTON, on_maximize);
		::SendMessage(tb, WM_NCLBUTTONUP, HTMAXBUTTON, on_maximize);
		::SendMessage(tb, WM_NCMOUSEMOVE, HTMAXBUTTON, on_maximize);
		::SendMessage(tb, WM_NCMOUSELEAVE, 0, 0);
		result.hover_after_leave = toolbar->_caption.hover_id;
	}

	// What maximizing does to the frame's style.
	const auto style = GetWindowLong(frame, GWL_STYLE);
	SetWindowLong(frame, GWL_STYLE, style | WS_MAXIMIZE);
	result.maximized_hit = static_cast<int>(::SendMessage(tb, WM_NCHITTEST, 0, screen_center(result.maximize_id)));

	// What Fullscreen does to it.
	SetWindowLong(frame, GWL_STYLE, style & ~(WS_CAPTION | WS_THICKFRAME));
	result.frameless_hit = static_cast<int>(::SendMessage(tb, WM_NCHITTEST, 0, screen_center(result.maximize_id)));

	toolbar->destroy();
	return result;
}

native_control<ui::button> create_native_button(const HWND parent, const int id, const owner_context_ptr& ctx,
                                                const icon_index icon, const std::string_view title,
                                                const std::string_view details, std::function<void()> invoke,
                                                const bool default_button)
{
	auto style = WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON | BS_TEXT | BS_NOTIFY;
	if (default_button) style |= BS_DEFPUSHBUTTON;

	const auto w = str::utf8_to_utf16(title);
	auto details_w = str::utf8_to_utf16(details);
	auto result = std::make_shared<button_impl>(ctx);

	result->Create(parent, w.c_str(), style, 0, id);
	ctx->set_window_font(result->m_hWnd, ui::style::font_face::dialog);
	result->_details = std::move(details_w);
	result->_icon = icon;
	result->_invoke = std::move(invoke);
	return {result, result};
}

native_control<ui::button> create_native_check_button(const HWND parent, const int id, const owner_context_ptr& ctx,
                                                      const bool val, const std::string_view text,
                                                      const bool is_radio, const bool starts_group,
                                                      const int radio_group, std::function<void(bool)> changed)
{
	// WS_GROUP marks the first button of a radio group. Without it Windows treats every sibling
	// auto-radio button as one group, so picking a collision policy would clear the scope choice.
	const auto style = WS_CHILD | WS_TABSTOP | BS_NOTIFY | (starts_group ? WS_GROUP : 0) |
		(is_radio ? BS_AUTORADIOBUTTON : BS_AUTOCHECKBOX);
	const auto w = str::utf8_to_utf16(text);
	auto result = std::make_shared<button_impl>(ctx);

	result->Create(parent, w.c_str(), style, 0, id);
	ctx->set_window_font(result->m_hWnd, ui::style::font_face::dialog);
	result->SetCheck(val ? 1 : 0);
	result->is_radio = is_radio;
	result->radio_group = radio_group;

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

	return {result, result};
}

native_control<ui::date_time_control> create_native_date_time(const HWND parent, const int id,
                                                              const owner_context_ptr& ctx, const df::date_t val,
                                                              std::function<void(df::date_t)> changed,
                                                              const ui::color_style& colors, const bool include_time)
{
	auto result = std::make_shared<date_time_control_impl>(ctx, val, std::move(changed), colors, include_time);
	result->Create(parent, id);
	return {result, result};
}
