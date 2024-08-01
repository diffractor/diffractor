// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2024  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

#include "model.h"
#include "ui_view.h"
#include "ui_controllers.h"
#include "ui_elements.h"

class list_view;
class view_controls_host;
using view_controls_host_ptr = std::shared_ptr<view_controls_host>;

class list_view : public view_base
{
protected:
	using this_type = list_view;

	view_state& _state;
	view_host_ptr _host;
	sizei _extent;
	view_scroller _scroller;

	std::u8string _empty_message;

	bool _rows_clickable = false;

	static constexpr int max_col_count = 4;
	int col_count = 4;
	int coll_offset = 0;
	int col_widths[max_col_count] = { 0, 0, 0, 0 };
	std::u8string col_titles[max_col_count];

	recti _col_header_bounds[max_col_count];
	bool _header_active = false;
	bool _header_tracking = false;
	int _header_active_num = 0;

	friend class scroll_controller;
	friend class clickable_controller;
	friend class header_controller;

public:

	struct row_element final : std::enable_shared_from_this<row_element>, view_element
	{
		list_view& _view;
		std::u8string _text[max_col_count];
		ui::color _bg_row;
		ui::color32 _text_color[max_col_count] = { 0, 0, 0, 0 };
		int _order = 0;

		row_element(list_view& view) noexcept
			: view_element(view_element_style::can_invoke), _view(view)
		{
		}

		void render(ui::draw_context& dc, pointi element_offset) const override
		{
			const auto bg_alpha = dc.colors.alpha * dc.colors.bg_alpha;
			const auto dir_color = ui::color(ui::style::color::dialog_selected_background, dc.colors.alpha);
			const auto logical_bounds = bounds.offset(element_offset);

			if (dc.clip_bounds().intersects(logical_bounds))
			{
				dc.draw_rect(logical_bounds, _bg_row.a_min(bg_alpha));
				dc.draw_rect(logical_bounds, _bg_color);

				auto x = logical_bounds.left + _view.coll_offset;

				for (int i = 0; i < _view.col_count; i++)
				{
					const auto text_color = ui::color(_text_color[i] != 0 ? _text_color[i] : dc.colors.foreground, dc.colors.alpha);
					const std::u8string_view text = _text[i];
					const recti bounds(x, logical_bounds.top, x + _view.col_widths[i], logical_bounds.bottom);
					dc.draw_text(text, bounds, ui::style::font_face::dialog, ui::style::text_style::single_line, text_color, {});
					x += _view.col_widths[i] + dc.padding2;
				}
			}
		}

		sizei measure(ui::measure_context& mc, int width_limit) const override
		{
			const auto row_height = mc.text_line_height(ui::style::font_face::dialog) + mc.padding2;
			return { width_limit, row_height };
		}

		void dispatch_event(const view_element_event& event) override
		{
		}

		view_controller_ptr controller_from_location(const view_host_ptr& host, pointi loc, pointi element_offset,
			const std::vector<recti>& excluded_bounds) override
		{
			return default_controller_from_location(*this, host, loc, element_offset, excluded_bounds);
		}
	};

	using row_element_ptr = std::shared_ptr<row_element>;
	std::vector<row_element_ptr> _rows;


	list_view(view_state& state, view_host_ptr host) : _state(state), _host(std::move(host))
	{
	}

	~list_view() override
	{
		_rows.clear();
	}

	void activate(sizei extent) override
	{
		_extent = extent;
		_state.stop();
	}

	void sort(int col_num)
	{
		if (col_num == 1 || col_num == 2)
		{
			std::ranges::sort(_rows, [col_num](const auto& left, const auto& right) { return str::icmp(left->_text[col_num], right->_text[col_num]) < 0; });
		}
		else
		{
			std::ranges::sort(_rows, [](const auto& left, const auto& right) { return left->_order < right->_order; });
		}

		_state.invalidate_view(view_invalid::controller | view_invalid::view_layout);
	}

	void layout(ui::measure_context& mc, sizei extent) override
	{
		_extent = extent;

		const recti scroll_bounds{ _extent.cx - mc.scroll_width, 0, _extent.cx, _extent.cy };
		const recti client_bounds{ 0, 0, _extent.cx, _extent.cy };

		auto y_max = 0;
		std::u8string_view longest_text[max_col_count];

		bool odd_row = false;

		for (int i = 0; i < col_count; i++)
		{
			longest_text[i] = col_titles[i];
		}

		if (!_rows.empty())
		{
			const auto row_height = mc.text_line_height(ui::style::font_face::dialog) + mc.padding2;
			auto y = row_height + (mc.padding2 * 2);

			for (const auto& r : _rows)
			{
				r->bounds.set(client_bounds.left, y, client_bounds.right, y + row_height);
				y += row_height + mc.padding1;

				for (int i = 0; i < col_count; i++)
				{
					const std::u8string_view text = r->_text[i];
					if (text.size() > longest_text[i].size()) longest_text[i] = text;
				}

				r->_bg_row.a = odd_row ? 0.11f : 0.0f;
				odd_row = !odd_row;
			}

			y_max = y + mc.padding2;
		}

		auto x_max = 0;
		auto shrink_col_total = 0;

		for (int i = 0; i < col_count; i++)
		{
			col_widths[i] = mc.measure_text(longest_text[i], ui::style::font_face::dialog, ui::style::text_style::single_line, extent.cx).cx + mc.padding2;
			x_max += col_widths[i] + mc.padding2;
			if (i > 0) shrink_col_total += col_widths[i];
		}

		// Shrink if too large
		if (x_max > extent.cx)
		{
			auto cx_avail = extent.cx - ((col_count + 1) * mc.padding2);
			auto extra = x_max - cx_avail;

			for (int i = 1; i < col_count; i++)
			{
				col_widths[i] -= df::mul_div(extra, col_widths[i], shrink_col_total);
			}

			x_max = 0;

			for (int i = 0; i < col_count; i++)
			{
				x_max += col_widths[i] + mc.padding2;
			}
		}

		coll_offset = std::max(0, extent.cx - x_max) / 2;

		_scroller.layout({ client_bounds.width(), y_max }, client_bounds, scroll_bounds);
		_host->frame()->invalidate();
	}

	void deactivate() override
	{
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, pointi loc) override;

	view_element_ptr element_from_location(int y) const
	{
		view_element_ptr result;
		const auto offset = _scroller.scroll_offset();
		int distance = 10000;

		for (const auto& i : _rows)
		{
			const auto yy = (i->bounds.top + i->bounds.bottom) / 2;
			const auto d = abs(yy - y - offset.y);

			if (d < distance)
			{
				distance = d;
				result = i;
			}
		}

		return result;
	}



	void render_headers(ui::draw_context& dc)
	{
		const auto cy = dc.text_line_height(ui::style::font_face::dialog) + (dc.padding2 * 2);
		const auto bg_alpha = dc.colors.alpha * 0.77f;
		const auto text_color = ui::color(dc.colors.foreground, dc.colors.alpha);

		constexpr int y = 0;
		auto x = coll_offset;

		ui::color header_bg(ui::darken(dc.colors.background, 0.33f), bg_alpha);

		if (!_header_active)
		{
			const auto header_bounds = recti(0, y, _extent.cx, y + cy);
			dc.draw_rect(header_bounds, header_bg);
		}
		else
		{
			auto xx = x;

			for (int i = 0; i < col_count; ++i)
			{
				xx += col_widths[i] + dc.padding2;
			}

			const auto header_bounds_left = recti(0, y, x - dc.padding2, y + cy);
			const auto header_bounds_right = recti(xx - dc.padding2, y, _extent.cx, y + cy);

			dc.draw_rect(header_bounds_left, header_bg);
			dc.draw_rect(header_bounds_right, header_bg);
		}

		for (int i = 0; i < col_count; ++i)
		{
			auto cx = col_widths[i];
			const recti col_header_bounds(x - dc.padding2, y, x + cx, y + cy);
			const recti col_header_text_bounds(x, y, x + cx, y + cy);

			if (_header_active)
			{
				ui::color bg(ui::darken(_header_active_num == i ? ui::style::color::dialog_selected_background : dc.colors.background, 0.33f), bg_alpha);
				dc.draw_rect(col_header_bounds, bg);
			}

			dc.draw_text(col_titles[i], col_header_text_bounds, ui::style::font_face::dialog,
				ui::style::text_style::single_line, text_color, {});

			_col_header_bounds[i] = col_header_bounds;
			x += cx + dc.padding2;
		}
	}

	void render(ui::draw_context& rc, view_controller_ptr controller) override
	{
		const auto offset = -_scroller.scroll_offset();

		if (!_rows.empty())
		{
			for (const auto& i : _rows)
			{
				i->render(rc, offset);
			}

			render_headers(rc);

			if (!_scroller._active)
			{
				_scroller.draw_scroll(rc);
			}
		}
		else if (!_empty_message.empty())
		{
			const auto text_color = ui::color(rc.colors.foreground, rc.colors.alpha);
			rc.draw_text(_empty_message, recti(_extent), ui::style::font_face::dialog,
				ui::style::text_style::single_line_center, text_color, {});
		}
	}

	void mouse_wheel(pointi loc, int zDelta, ui::key_state keys) override
	{
		_scroller.offset(_host, 0, -zDelta);
		_state.invalidate_view(view_invalid::controller);
	}


};

class view_controls_host : public view_host, public std::enable_shared_from_this<view_controls_host>
{
public:
	view_state& _state;
	view_scroller _scroller;
	ui::control_frame_ptr _dlg;
	ui::frame_ptr _frame;
	std::vector<view_element_ptr> _controls;
	ui::color _clr = ui::style::color::dialog_text;
	long _layout_height = 0;
	long _layout_width = 0;
	ui::coll_widths _label_width;

	view_controls_host(view_state& s) : _state(s)
	{
		_scroller._scroll_child_controls = true;
	}

	~view_controls_host() override = default;

	void scroll_controls() override
	{
		if (_frame)
		{
			_frame->layout();
		}
	}

	virtual void layout_controls(ui::measure_context& mc)
	{
		if (!_controls.empty())
		{
			const auto layout_padding = mc.padding1;
			auto avail_bounds = recti(_extent);
			avail_bounds.right -= mc.scroll_width + layout_padding;

			ui::control_layouts positions;
			const auto height = stack_elements(mc, positions, avail_bounds, _controls, false,
				{ layout_padding, layout_padding });

			_layout_height = height;
			_layout_width = avail_bounds.width();
			_label_width = mc.col_widths;

			const recti scroll_bounds{ _extent.cx - mc.scroll_width, 0, _extent.cx, _extent.cy };
			const recti client_bounds{ 0, 0, _extent.cx - mc.scroll_width, _extent.cy };
			_scroller.layout({ _layout_width, _layout_height }, client_bounds, scroll_bounds);

			_dlg->apply_layout(positions, -_scroller.scroll_offset());
			_dlg->invalidate();
		}
	}

	void populate()
	{
		const view_element_event e{ view_element_event_type::populate, shared_from_this() };

		for (const auto& c : _controls)
		{
			c->dispatch_event(e);
		}
	}

	void on_window_layout(ui::measure_context& mc, sizei extent, bool is_minimized) override
	{
		_extent = extent;
		layout_controls(mc);
	}

	void on_window_paint(ui::draw_context& dc) override
	{
		dc.col_widths = _label_width;

		const auto offset = _scroller.scroll_offset();

		for (const auto& c : _controls)
		{
			if (c->is_visible())
			{
				c->render(dc, -offset);
			}
		}

		if (_active_controller)
		{
			_active_controller->draw(dc);
		}

		if (!_scroller._active)
		{
			_scroller.draw_scroll(dc);
		}
	}

	void tick() override
	{
	}

	void activate(bool is_active) override
	{
	}

	bool key_down(const int c, const ui::key_state keys) override
	{
		return false;
	}

	const ui::frame_ptr frame() override
	{
		return _frame;
	}

	const ui::control_frame_ptr owner() override
	{
		return _dlg;
	}

	void invoke(const commands cmd) override
	{
		_state.invoke(cmd);
	}

	bool is_command_checked(const commands cmd) override
	{
		return _state.is_command_checked(cmd);
	}

	void controller_changed() override
	{
		_state.invalidate_view(view_invalid::tooltip);
	}

	void invalidate_element(const view_element_ptr& e) override
	{
		if (_frame)
		{
			_frame->invalidate();
		}
	}

	view_controller_ptr controller_from_location(const pointi loc) override
	{
		if (_scroller.can_scroll() && _scroller.scroll_bounds().contains(loc))
		{
			return std::make_shared<scroll_controller>(shared_from_this(), _scroller, _scroller.scroll_bounds());
		}

		const auto offset = -_scroller.scroll_offset();

		for (const auto& e : _controls)
		{
			auto controller = e->controller_from_location(shared_from_this(), loc, offset, {});
			if (controller) return controller;
		}

		return nullptr;
	}

	void on_mouse_wheel(const pointi loc, const int delta, const ui::key_state keys, bool& was_handled) override
	{
		_scroller.offset(shared_from_this(), 0, -(delta / 2));
		update_controller(loc);
	}

	virtual void options_changed()
	{
		if (!_controls.empty())
		{
			for (const auto& c : _controls)
			{
				c->visit_controls([](const ui::control_base_ptr& cc) { cc->options_changed(); });
			}
		}
	}

	void focus_changed(bool has_focus, const ui::control_base_ptr& child) override
	{
		if (child && child->has_focus())
		{
			auto point_offset = _scroller.scroll_offset();
			const auto rc = child->window_bounds().offset(point_offset - _dlg->window_bounds().top_left());

			if (rc.top < point_offset.y)
			{
				point_offset.y = rc.top;
			}
			else if (rc.bottom > (point_offset.y + _extent.cy))
			{
				point_offset.y = rc.bottom - _extent.cy;
			}

			_scroller.scroll_offset(shared_from_this(), 0, point_offset.y);
		}
	}

	void command_hover(const ui::command_ptr& c, recti window_bounds) override
	{
		_state.command_hover(c, window_bounds);
	}

	void track_menu(const recti bounds, const std::vector<ui::command_ptr>& commands) override
	{
		_state.track_menu(_dlg, bounds, commands);
	}

	void invalidate_view(const view_invalid invalid) override
	{
		_state.invalidate_view(invalid);
	}

	void on_window_destroy() override
	{
		_active_controller.reset();
		_controls.clear();
		_dlg.reset();
		_frame.reset();
	}
};

class header_controller final : public view_controller
{
public:
	list_view& _parent;
	int _col_num;

	header_controller(const view_host_ptr& host, list_view& parent, const recti bounds, const int col_num) :
		view_controller(host, bounds), _parent(parent), _col_num(col_num)
	{
		_parent._header_active = true;
		_parent._header_active_num = _col_num;
	}

	~header_controller() override
	{
		if (_parent._header_tracking)
		{
			escape();
		}

		_parent._header_active = false;
	}

	void draw(ui::draw_context& rc) override
	{
	}

	ui::style::cursor cursor() const override
	{
		return ui::style::cursor::link;
	}

	void on_mouse_left_button_down(const pointi loc, const ui::key_state keys) override
	{
		_last_loc = loc;
		_parent._header_tracking = true;
		_host->frame()->invalidate();
	}

	void on_mouse_move(const pointi loc) override
	{
		_last_loc = loc;
	}

	void on_mouse_left_button_up(const pointi loc, const ui::key_state keys) override
	{
		_last_loc = loc;
		_parent._header_tracking = false;
		_parent.sort(_col_num);
		_host->frame()->invalidate();
	}

	void escape() override
	{
		_parent._header_tracking = false;
	}

	void popup_from_location(view_hover_element& hover) override
	{
	}
};

inline view_controller_ptr list_view::controller_from_location(const view_host_ptr& host, pointi loc)
{
	for (int i = 0; i < col_count; ++i)
	{
		if (_col_header_bounds[i].contains(loc))
		{
			return std::make_shared<header_controller>(host, *this, _col_header_bounds[i], i);
		}
	}

	if (_scroller.scroll_bounds().contains(loc))
	{
		return std::make_shared<scroll_controller>(host, _scroller, _scroller.scroll_bounds());
	}

	if (_rows_clickable)
	{
		const auto test = element_from_location(loc.y);

		if (test)
		{
			const auto test_offset = -_scroller.scroll_offset();
			return std::make_shared<clickable_controller>(host, test, test_offset);
		}
	}

	return nullptr;
}