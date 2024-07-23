// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

class ui_element_render;
class ui_element_state;
class view_controller;
class view_element;
class view_elements;
class view_host;
class view_host_base;
class view_state;
enum class render_valid;
struct view_hover_element;

using view_host_ptr = std::shared_ptr<view_host>;
using view_controller_ptr = std::shared_ptr<view_controller>;
using view_element_ptr = std::shared_ptr<view_element>;
using view_host_base_ptr = std::shared_ptr<view_host_base>;

enum class menu_type
{
	view,
	media,
	items
};

enum class view_element_event_type
{
	invoke,
	click,
	double_click,
	tick,
	populate,
	dpi_changed,
	initialise,
	update_command_state,
	free_graphics_resources,
};

enum class view_invalid
{
	none = 0,

	view_layout = 1 << 1,
	group_layout = 1 << 3,
	app_layout = 1 << 4,
	view_redraw = 1 << 5,
	item_scan = 1 << 6,
	media_elements = 1 << 7,
	tooltip = 1 << 8,
	command_state = 1 << 9,
	sidebar = 1 << 10,
	sidebar_file_types_and_dates = 1 << 11,
	presence = 1 << 12,
	address = 1 << 13,
	controller = 1 << 14,
	index = 1 << 15,
	refresh_items = 1 << 16,
	focus_item_visible = 1 << 17,
	screen_saver = 1 << 18,
	options_save = 1 << 19,
	index_summary = 1 << 20,
	filters = 1 << 21,
	animations = 1 << 22,
	selection_list = 1 << 23,
	font_size = 1 << 24,
	image_compare = 1 << 25,

	options = view_layout |
	group_layout |
	app_layout |
	options_save |
	sidebar |
	command_state |
	tooltip |
	refresh_items |
	address |
	font_size,
};

constexpr view_invalid operator|(const view_invalid a, const view_invalid b)
{
	return static_cast<view_invalid>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr view_invalid operator&(const view_invalid a, const view_invalid b)
{
	return static_cast<view_invalid>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

constexpr bool operator&&(const view_invalid a, const view_invalid b)
{
	return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

constexpr view_invalid& operator|=(view_invalid& a, const view_invalid b)
{
	a = static_cast<view_invalid>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
	return a;
}

struct view_element_event
{
	view_element_event_type type;
	view_host_ptr host;
};

struct interaction_context
{
	const pointi loc;
	const pointi element_offset;
	const bool tracking;
	bool invalidate_view = false;
};

constexpr ui::color view_handle_color(const bool selected, const bool hover, const bool tracking,
                                        const bool view_has_focus, const bool text_over,
										ui::color bg_clr = ui::style::color::group_background)
{
	if (tracking)
	{
		const auto clr = selected ? ui::color(ui::style::color::view_selected_background) : bg_clr.average(ui::style::color::view_text);
		
		return clr.scale(hover ? 1.22f : 1.0f).aa(0.9f);
	}

	if (selected)
	{
		const auto clr = view_has_focus
			                 ? ui::color(ui::style::color::view_selected_background).scale(hover ? 1.22f : 1.0f)
			                 : bg_clr.average(ui::style::color::view_selected_background).scale(hover ? 1.33f : 1.0f);
		return clr.aa(0.9f);
	}

	if (hover)
	{
		const auto clr = bg_clr.average(ui::style::color::view_text);
		return text_over ? clr.scale(0.8f).aa(0.9f) : clr.aa(0.9f);
	}

	return bg_clr.scale(text_over ? 1.22f : 1.44f).aa(0.9f);
}

class view_controller
{
public:
	view_controller(view_host_ptr host, const recti bounds) : _host(std::move(host)), _bounds(bounds)
	{
	}

	virtual ~view_controller() = default;

	view_host_ptr _host;
	recti _bounds;
	pointi _last_loc;
	pointi _start_loc;
	int64_t _first_tic = 0;
	float _alpha = 1.0f;

	const recti bounds() const
	{
		return _bounds;
	}

	virtual void draw(ui::draw_context& rc)
	{
	}

	virtual ui::style::cursor cursor() const
	{
		return ui::style::cursor::normal;
	}

	virtual void on_mouse_left_button_down(const pointi loc, const ui::key_state keys)
	{
		_last_loc = _start_loc = loc;
		_first_tic = platform::tick_count();
	}

	virtual void on_mouse_move(const pointi loc)
	{
	}

	virtual void on_mouse_left_button_up(const pointi loc, const ui::key_state keys)
	{
	}

	virtual void on_mouse_left_button_double_click(const pointi loc, const ui::key_state keys)
	{
	}

	virtual void on_mouse_leave()
	{
	}

	virtual void escape()
	{
	}

	bool over_min_time() const
	{
		return (_first_tic + 200) < platform::tick_count();
	}

	virtual void popup_from_location(view_hover_element& hover)
	{
	}

	virtual void tick()
	{
	}
};

class view_host_base
{
public:
	virtual void invalidate_element(const view_element_ptr& e) = 0;
	virtual void invalidate_view(view_invalid invalid) = 0;
};

class view_host : public ui::frame_host, public view_host_base
{
public:
	sizei _extent;

	view_controller_ptr _active_controller;

	bool _show_cursor = true;
	bool _hover = false;
	bool _tracking = false;
	bool _controller_invalid = false;
	recti _controller_bounds;
	recti _tooltip_bounds;

	ui::style::cursor _cursor = ui::style::cursor::normal;

	void invalidate_view(view_invalid invalid) override = 0;
	virtual void invoke(commands cmd) = 0;
	virtual bool is_command_checked(commands cmd) = 0;
	virtual void track_menu(recti recti, const std::vector<ui::command_ptr>& commands) = 0;
	virtual void controller_changed() = 0;
	virtual const ui::frame_ptr frame() = 0;
	virtual const ui::control_frame_ptr owner() = 0;
	virtual view_controller_ptr controller_from_location(pointi loc) = 0;

	virtual void scroll_controls()
	{
	}

	void on_window_destroy() override
	{
		_active_controller.reset();
	}

	void update_controller(const pointi loc)
	{
		if (!_tooltip_bounds.is_empty() && !_tooltip_bounds.contains(loc))
		{
			_tooltip_bounds.clear();
			invalidate_view(view_invalid::tooltip);
		}

		if (!_tracking && (_controller_invalid || !_controller_bounds.contains(loc)))
		{
			_active_controller.reset();
			_active_controller = controller_from_location(loc);

			auto cursor = ui::style::cursor::normal;

			if (_active_controller)
			{
				_active_controller->on_mouse_move(loc);
				cursor = _active_controller->cursor();
			}

			_cursor = cursor;
			_controller_bounds = _active_controller ? _active_controller->bounds() : recti{};
			_controller_invalid = false;

			controller_changed();
			frame()->invalidate();
		}
	}

	void on_mouse_left_button_down(const pointi loc, const ui::key_state keys) override
	{
		update_tracking(loc, true);
		update_controller(loc);

		const auto c = _active_controller;

		if (c)
		{
			c->on_mouse_left_button_down(loc, keys);
		}

		update_cursor();
	}

	void on_mouse_left_button_up(const pointi loc, const ui::key_state keys) override
	{
		update_tracking(loc, false);

		const auto c = _active_controller;

		if (c)
		{
			c->on_mouse_left_button_up(loc, keys);
		}

		// on_mouse_left_button_up sometimes shows a dialog
		// it is best to fetch the current cursor location
		update_controller(frame()->cursor_location());
	}

	void on_mouse_left_button_double_click(const pointi loc, const ui::key_state keys) override
	{
		update_controller(loc);
		const auto c = _active_controller;

		if (c)
		{
			c->on_mouse_left_button_double_click(loc, keys);
		}
	}

	void update_tracking(const pointi loc, const bool is_tracking)
	{
		_hover = recti(_extent).contains(loc);
		_tracking = is_tracking;
	}

	void on_mouse_move(const pointi loc, const bool is_tracking) override
	{
		update_tracking(loc, is_tracking);
		update_controller(loc);

		const auto c = _active_controller;

		if (c)
		{
			c->on_mouse_move(loc);
		}

		update_cursor();
	}

	void on_mouse_leave(const pointi loc) override
	{
		update_controller({-1, -1});
		_hover = false;
	}

	void update_cursor()
	{
		if (_active_controller)
		{
			_cursor = _active_controller->cursor();
		}

		frame()->set_cursor(_show_cursor ? _cursor : ui::style::cursor::none);
	}

	void show_cursor(const bool show)
	{
		if (_show_cursor != show)
		{
			_show_cursor = show;
			update_cursor();
		}
	}

	bool show_cursor() const
	{
		return _show_cursor;
	}
};

class view_base : public df::no_copy
{
public:
	bool _view_has_focus = false;
	bool _show_render_window = true;

	view_base()
	{
	}

	virtual ~view_base()
	{
	}

	virtual void activate(sizei extent) = 0;
	virtual void deactivate() = 0;

	virtual void update_media_elements()
	{
	}

	virtual view_controller_ptr controller_from_location(const view_host_ptr& host, const pointi loc)
	{
		return nullptr;
	}

	virtual bool can_exit()
	{
		return true;
	}

	virtual bool can_scroll(const pointi loc) const
	{
		return false;
	}

	virtual menu_type context_menu(const pointi loc)
	{
		return menu_type::view;
	}

	virtual bool escape()
	{
		return false;
	}

	virtual void render(ui::draw_context& rc, view_controller_ptr controller)
	{
	}

	// Events
	virtual void layout(ui::measure_context& mc, const sizei extent)
	{
	}

	virtual void mouse_wheel(const pointi loc, const int zDelta, const ui::key_state keys)
	{
	}

	virtual void pan_start(const pointi start_loc)
	{
	}

	virtual void pan(const pointi start_loc, const pointi current_loc)
	{
	}

	virtual void pan_end(const pointi start_loc, const pointi final_loc)
	{
	}

	virtual void focus(bool has_focus)
	{
		_view_has_focus = has_focus;
	}

	virtual void items_changed(bool path_changed)
	{
	}

	virtual void display_changed()
	{
	}

	virtual bool is_over_items(const pointi loc) const
	{
		return false;
	}

	virtual void broadcast_event(const view_element_event& event) const
	{
	}
};

struct view_scroller_section
{
	std::u8string text;
	icon_index icon = icon_index::none;
	int y = 0;
};

class view_scroller
{
public:
	pointi _offset;
	sizei _scroll_extent;
	recti _client_bounds;
	recti _scroll_bounds;
	std::vector<view_scroller_section> _sections;
	std::function<void(view_hover_element&, pointi)> popup_func;
	std::function<void()> changed_func;

public:
	bool _active = false;
	bool _tracking = false;
	bool _scroll_child_controls = false;	

	const recti scroll_bounds() const
	{
		return _scroll_bounds;
	}

	void scroll_offset(const view_host_ptr& host, const int x, const int y)
	{
		const auto cx = _scroll_extent.cx - _client_bounds.width();
		const auto cy = _scroll_extent.cy - _client_bounds.height();
		const auto xx = cx > 0 ? std::clamp(x, 0, cx) : 0;
		const auto yy = cy > 0 ? std::clamp(y, 0, cy) : 0;

		if ((_offset.x != xx || _offset.y != yy) && _scroll_extent.cy > 0)
		{
			const auto delta = _offset.y - yy;
			_offset.x = xx;
			_offset.y = yy;

			if (delta != 0)
			{
				host->frame()->scroll(0, delta, _client_bounds, _scroll_child_controls);
			}

			host->frame()->invalidate(_scroll_bounds);

			if (changed_func)
			{
				changed_func();
			}

			//host->scroll_controls();
		}
	}

	void layout(const sizei scroll_extent, const recti client_bounds, const recti scroll_bounds)
	{
		_scroll_extent = scroll_extent;
		_client_bounds = client_bounds;
		_scroll_bounds = scroll_bounds;

		const auto cx = _scroll_extent.cx - _client_bounds.width();
		const auto cy = _scroll_extent.cy - _client_bounds.height();

		_offset.x = cx > 0 ? std::clamp(_offset.x, 0, cx) : 0;
		_offset.y = cy > 0 ? std::clamp(_offset.y, 0, cy) : 0;
	}

	void sections(std::vector<view_scroller_section> section_offsets)
	{
		_sections = std::move(section_offsets);
	}

	const pointi scroll_offset() const
	{
		return _offset;
	}

	const recti client_bounds() const
	{
		return _client_bounds;
	}

	void reset()
	{
		_offset.x = 0;
		_offset.y = 0;
	}

	void scrollbar_to(const view_host_ptr& host, int pos, int height)
	{
		const auto cy = df::mul_div(_client_bounds.height(), height, _scroll_extent.cy) / 2;
		scroll_offset(host, 0, df::mul_div(pos - cy, _scroll_extent.cy, height));
	}

	pointi scroll_pos() const
	{
		return {0, _scroll_extent.cy};
	}

	pointi device_to_logical(const pointi loc) const
	{
		return {loc.x, loc.y + _offset.y};
	}

	pointi logical_to_device(const pointi loc) const
	{
		return {loc.x, loc.y - _offset.y};
	}

	recti logical_to_device(const recti bounds) const
	{
		return {{bounds.left, bounds.top - _offset.y}, bounds.extent()};
	}

	void offset(const view_host_ptr& host, int x, int y)
	{
		scroll_offset(host, _offset.x + x, _offset.y + y);
	}

	void draw_scroll(ui::draw_context& dc) const;

	void scroll_popup(view_hover_element& hover, const pointi loc) const
	{
		if (popup_func)
		{
			popup_func(hover, loc);
		}
	}

	bool can_scroll() const
	{
		return _scroll_extent.cy > _client_bounds.height() && _client_bounds.height() > 0;
	}

	void clear()
	{
		_offset = {};
		_scroll_extent = {};
		_client_bounds.clear();
		_scroll_bounds.clear();
		_sections.clear();
		popup_func = {};
	}

	int logical_to_scrollbar_pos(int y) const
	{
		return df::mul_div(y, _client_bounds.height(), _scroll_extent.cy);
	}
};


class scrubber_element;

class scroll_controller final : public view_controller
{
public:
	view_scroller& _parent;
	pointi _start;


	scroll_controller(const view_host_ptr& host, view_scroller& parent, const recti bounds) :
		view_controller(host, bounds), _parent(parent)
	{
		_parent._active = true;
	}

	~scroll_controller() override
	{
		if (_parent._tracking)
		{
			escape();
		}

		_parent._active = false;
	}

	void draw(ui::draw_context& rc) override
	{
		_parent.draw_scroll(rc);
	}

	ui::style::cursor cursor() const override
	{
		return ui::style::cursor::up_down;
	}

	void update_pos(int y)
	{
		_parent.scrollbar_to(_host, y - _bounds.top, _bounds.height());
	}

	void on_mouse_left_button_down(const pointi loc, const ui::key_state keys) override
	{
		_last_loc = loc;
		_parent._tracking = true;
		_start = _parent.scroll_pos();
		update_pos(loc.y);
		_host->frame()->invalidate();
	}

	void on_mouse_move(const pointi loc) override
	{
		_last_loc = loc;

		if (_parent._tracking)
		{
			update_pos(loc.y);
		}
	}

	void on_mouse_left_button_up(const pointi loc, const ui::key_state keys) override
	{
		_last_loc = loc;
		_parent._tracking = false;
		update_pos(loc.y);
		_host->frame()->invalidate();
	}

	void escape() override
	{
		_parent.scroll_offset(_host, _start.x, _start.y);
		_parent._tracking = false;
	}

	void popup_from_location(view_hover_element& hover) override
	{
		if (_parent._active)
		{
			_parent.scroll_popup(hover, _last_loc);
		}
	}
};
