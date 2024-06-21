// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "pch.h"
#include "model.h"
#include "model_index.h"
#include "model_db.h"
#include "ui_view.h"
#include "ui_dialog.h"
#include "ui_controls.h"
#include "view_items.h"

class item_select_controller final : public view_controller
{
private:
	items_view& _parent;
	view_scroller& _scroller;
	view_state& _state;
	bool _tracking = false;
	bool _selecting = false;
	bool _cancel = false;
	pointi _last_logical_loc;
	pointi _start_logical_loc;
	df::item_element_ptr _hover_item;

public:
	item_select_controller(const view_host_ptr& host, items_view& parent, view_state& s, view_scroller& scroller,
		const recti bounds, const pointi loc) : view_controller(host, bounds), _parent(parent),
		_scroller(scroller), _state(s),
		_start_logical_loc(loc)
	{
		if (_hover_item)
		{
			_hover_item->set_style_bit(view_element_style::hover, true, _host, _hover_item);
		}
	}

	item_select_controller(const view_host_ptr& host, items_view& parent, view_state& s, view_scroller& scroller,
		df::item_element_ptr i, const pointi loc) : view_controller(host, {}), _parent(parent),
		_scroller(scroller), _state(s),
		_start_logical_loc(loc),
		_hover_item(std::move(i))
	{
		if (_hover_item)
		{
			_hover_item->set_style_bit(view_element_style::hover, true, _host, _hover_item);
			_bounds = _scroller.logical_to_device(_hover_item->bounds);
		}
	}

	~item_select_controller() override
	{
		if (_tracking)
		{
			_cancel = true;
			update_state();
		}

		if (_hover_item)
		{
			_hover_item->set_style_bit(view_element_style::hover, false, _host, _hover_item);
		}

		for (const auto& group : _state.groups())
		{
			for (const auto& item : group->items())
			{
				item->set_style_bit(view_element_style::highlight, false, _host, item);
			}
		}
	}

	ui::style::cursor cursor() const override
	{
		return _selecting ? ui::style::cursor::select : ui::style::cursor::normal;
	}

	recti calc_selection_bounds() const
	{
		return recti(_start_loc, _last_loc).normalise();
	}

	recti calc_logical_selection_bounds() const
	{
		return _selecting ? recti(_start_logical_loc, _last_logical_loc).normalise() : recti();
	}

	void draw(ui::draw_context& rc) override
	{
		if (_selecting)
		{
			const auto selection = calc_selection_bounds();
			rc.draw_rect(selection, ui::color(ui::style::color::dialog_selected_background, 0.5));
		}
	}

	void on_mouse_left_button_down(const pointi loc, const ui::key_state keys) override
	{
		_tracking = true;
		_cancel = false;
		_first_tic = platform::tick_count();
		_last_loc = _start_loc = loc;
		_last_logical_loc = _start_logical_loc = _scroller.device_to_logical(loc);
		update_state();
	}

	void on_mouse_move(const pointi loc) override
	{
		_last_loc = loc;
		update_state();

		if (!_tracking && _hover_item && _hover_item->file_type()->has_trait(file_traits::preview_video))
		{
			const auto logical_loc = _scroller.device_to_logical(loc);
			const auto item_bounds = _hover_item->bounds;

			if (item_bounds.contains(logical_loc))
			{
				_state.load_hover_thumb(_hover_item, loc.x - item_bounds.left, item_bounds.width());
			}
		}
	}

	void update_state()
	{
		const auto previous_sel_bounds = calc_logical_selection_bounds();

		const auto start_area = center_rect({ 8, 8 }, _start_loc);
		_selecting = _tracking && !_cancel && !start_area.contains(_last_loc);
		_last_logical_loc = _scroller.device_to_logical(_last_loc);
		const auto logical_selection_bounds = calc_logical_selection_bounds();
		const auto changed_bounds = logical_selection_bounds.make_union(previous_sel_bounds);

		for (const auto& group : _state.groups())
		{
			if (group->bounds.intersects(changed_bounds))
			{
				for (const auto& i : group->items())
				{
					i->set_style_bit(view_element_style::highlight, i->bounds.intersects(logical_selection_bounds),
						_host, i);
				}
			}
		}

		_host->frame()->invalidate();
	}

	void invoke_select(const ui::key_state keys, bool perform_open)
	{
		if (!_cancel)
		{
			if (_selecting)
			{
				_state.select(_parent._host, calc_logical_selection_bounds(), keys.control);
				_state.stop_slideshow();
			}
			else
			{
				const auto i = _state.item_from_location(_last_logical_loc);

				if (i)
				{
					_state.select(_parent._host, i, keys.control, keys.shift, false);
					_state.stop_slideshow();

					if (perform_open)
					{
						_state.open(_parent._host, i);

						if (i->file_type()->is_media() && !_state.is_full_screen)
						{
							_state.toggle_full_screen();
						}

						_cancel = true;
					}
				}
			}
		}
	}

	void on_mouse_left_button_up(const pointi loc, const ui::key_state keys) override
	{
		_last_loc = loc;
		update_state();

		invoke_select(keys, false);

		_selecting = false;
		_tracking = false;
	}

	void on_mouse_left_button_double_click(const pointi loc, const ui::key_state keys) override
	{
		_last_loc = loc;
		update_state();
		invoke_select(keys, true);
	}

	void escape() override
	{
		_cancel = true;
		update_state();
	}

	void tick() override
	{
		if (_selecting)
		{
			const auto offset = 32;
			const auto client_bounds = _scroller.client_bounds();
			const auto scroll_zone = 64;

			if (_last_loc.y < client_bounds.top + scroll_zone)
			{
				_scroller.offset(_host, 0, -offset);
			}
			else if (_last_loc.y > client_bounds.bottom - scroll_zone)
			{
				_scroller.offset(_host, 0, offset);
			}
		}
	}
};

class item_drag_controller final : public view_controller
{
private:
	items_view& _parent;
	view_scroller& _scroller;
	view_state& _state;
	bool _tracking = false;
	bool _cancel = false;
	bool _drag_started = false;
	bool _dropped = false;
	pointi _last_logical_loc;
	pointi _start_logical_loc;
	df::item_element_ptr _hover_item;

public:
	item_drag_controller(const view_host_ptr& host, items_view& parent, view_state& s, view_scroller& scroller,
		df::item_element_ptr i, const pointi loc) : view_controller(host, {}), _parent(parent),
		_scroller(scroller), _state(s),
		_start_logical_loc(loc), _hover_item(std::move(i))
	{
		if (_hover_item)
		{
			_hover_item->set_style_bit(view_element_style::hover, true, _host, _hover_item);
			_bounds = _scroller.logical_to_device(_hover_item->bounds);
		}
	}

	~item_drag_controller() override
	{
		if (_tracking)
		{
			_cancel = true;
		}

		if (_hover_item)
		{
			_hover_item->set_style_bit(view_element_style::hover, false, _host, _hover_item);
		}
	}

	ui::style::cursor cursor() const override
	{
		return ui::style::cursor::normal;
	}

	recti calc_selection_bounds() const
	{
		return recti(_start_loc, _last_loc).normalise();
	}

	void draw(ui::draw_context& rc) override
	{
	}

	void on_mouse_left_button_down(const pointi loc, const ui::key_state keys) override
	{
		_tracking = true;
		_cancel = false;
		_first_tic = platform::tick_count();
		_last_loc = _start_loc = loc;
		_last_logical_loc = _start_logical_loc = _scroller.device_to_logical(loc);
		_drag_started = false;
	}

	void on_mouse_move(const pointi loc) override
	{
		_last_loc = loc;

		if (_tracking && !_cancel && !_bounds.contains(loc))
		{
			start_drag();
			//_parent->invalidate();
		}
		else if (!_tracking && _hover_item && _hover_item->file_type()->has_trait(file_traits::preview_video))
		{
			const auto logical_loc = _scroller.device_to_logical(loc);
			const auto item_bounds = _hover_item->bounds;

			if (item_bounds.contains(logical_loc))
			{
				_state.load_hover_thumb(_hover_item, loc.x - item_bounds.left, item_bounds.width());
			}
		}
	}

	void on_mouse_left_button_up(const pointi loc, const ui::key_state keys) override
	{
		_last_loc = loc;
		_tracking = false;

		if (!_drag_started && !_cancel)
		{
			if (_hover_item)
			{
				_state.select(_parent._host, _hover_item, keys.control, keys.shift, false);
				_state.stop_slideshow();
			}
		}
	}

	void on_mouse_left_button_double_click(const pointi loc, const ui::key_state keys) override
	{
		_last_loc = loc;
		_state.open(_parent._host, _hover_item);

		if (_hover_item->file_type()->is_media() && !_state.is_full_screen)
		{
			_state.toggle_full_screen();
		}

		_state.stop_slideshow();
		_cancel = true;
	}

	void start_drag()
	{
		if (_hover_item)
		{
			df::scope_locked_inc l(df::dragging_items);
			detach_file_handles detach(_state);
			_drag_started = true;
			_dropped = platform::perform_drag(_host->frame()->handle(), _state.selected_items().file_paths(),
				_state.selected_items().folder_paths()) != platform::drop_effect::none;
			_tracking = false;
			_state.invalidate_view(view_invalid::controller);
		}
	}

	void escape() override
	{
		_cancel = true;
	}
};

void items_view::activate(const sizei extent)
{
	_client_extent = extent;
	update_media_elements();
}

void items_view::deactivate()
{
}

items_view::items_view(view_state& s, view_host_ptr host) :
	_state(s),
	_host(std::move(host))
{
	_items_scroller.popup_func = [this](view_hover_element& hover, const pointi loc)
		{
			items_scroll_popup(hover, loc);
		};
	_items_scroller.changed_func = [this]() { update_visible_items_list(); };
	_media_scroller.changed_func = [this]() { _state.invalidate_view(view_invalid::view_redraw); };
}

void items_view::items_changed(const bool path_changed)
{
	if (path_changed)
	{
		_items_scroller.reset();
		_media_scroller.reset();

		/*const auto i = _state.focus_item();

		if (!i)
		{
			const auto shown = _state.first_selected();

			if (shown)
			{
				_state.select(shown, false, false);
			}
		}*/
	}

	std::vector<view_element_ptr> elements;
	elements.clear();
	elements.reserve(_state.groups().size() * 3);

	for (const auto& g : _state.groups())
	{
		//elements.emplace_back(std::make_shared<padding_element>(8));
		//if (!elements.empty()) elements.emplace_back(std::make_shared<divider_element>());
		elements.emplace_back(build_group_title(_state, _host, g));
		//elements.emplace_back(std::make_shared<padding_element>(4));
		elements.emplace_back(g);
	}

	const auto has_items = _state.has_display_items();

	if (has_items)
	{
		if (_state.group_order() == group_by::date_created || _state.group_order() == group_by::date_modified)
		{
			const auto text = setting.sort_dates_descending
				? tt.command_sort_dates_ascending
				: tt.command_sort_dates_descending;
			const auto command = setting.sort_dates_descending
				? commands::sort_dates_ascending
				: commands::sort_dates_descending;

			elements.emplace_back(std::make_shared<divider_element>());

			auto element = std::make_shared<link_element>(text, command, ui::style::font_face::dialog,
				ui::style::text_style::multiline_center,
				view_element_style::new_line | view_element_style::center);
			element->padding = { 8, 8 };
			element->margin = { 8, 8 };
			elements.emplace_back(element);
		}
	}

	if (_state.item_index.searching > 0)
	{
		if (has_items)
		{
			elements.emplace_back(std::make_shared<divider_element>());
		}
		else
		{
			elements.emplace_back(std::make_shared<padding_element>(32));
		}

		elements.emplace_back(std::make_shared<text_element>(tt.searching_text, ui::style::font_face::title,
			ui::style::text_style::single_line_center,
			view_element_style::new_line |
			view_element_style::center));
	}
	else if (_state.all_items_filtered_out())
	{
		elements.emplace_back(std::make_shared<padding_element>(32));
		auto element = std::make_shared<link_element>(tt.all_items_filtered, [&s = _state]() { s.clear_filters(); },
			ui::style::font_face::dialog,
			ui::style::text_style::multiline_center,
			view_element_style::new_line | view_element_style::center);
		element->padding = { 8, 8 };
		element->margin = { 8, 8 };
		elements.emplace_back(element);
	}

	elements.emplace_back(std::make_shared<padding_element>(32));

	std::swap(_item_elements, elements);

	_state.invalidate_view(view_invalid::view_layout);
}

void items_view::invalidate_thumb(const df::item_element_ptr& i)
{
	_state.invalidate_view(view_invalid::view_redraw);
}

sizei items_view::client_size() const
{
	return _client_extent;
}

void items_view::draw_splitter(ui::draw_context& dc, const recti bounds, const bool tracking) const
{
	const auto scale_factor = dc.scale_factor;
	const bool active = _splitter_active != 0;
	const auto scale1 = df::round(1 * scale_factor);
	const auto handle_margin = (tracking || active) ? scale1 : df::mul_div(bounds.width(), 2, 9);
	const auto left = bounds.left + handle_margin;
	const auto right = bounds.right - handle_margin;

	recti draw_bounds;
	draw_bounds.left = left;
	draw_bounds.right = right;
	draw_bounds.top = bounds.top + dc.handle_cxy;
	draw_bounds.bottom = bounds.bottom - dc.handle_cxy;

	if (draw_bounds.height() > 8)
	{
		dc.draw_rounded_rect(draw_bounds, ui::color(0x000000, dc.colors.alpha * dc.colors.bg_alpha), dc.padding1);
	}

	if (active)
	{
		const auto clr = view_handle_color(false, _splitter_active, tracking, dc.frame_has_focus, false).aa(dc.colors.alpha);
		dc.draw_rounded_rect(draw_bounds.inflate(-scale1), clr, dc.padding1);
	}
}

class splitter_controller final : public view_controller
{
public:
	items_view& _view;
	int _start = 0;
	pointi _start_loc;
	bool _tracking = false;

	splitter_controller(const view_host_ptr& host, items_view& view,
		const recti bounds) : view_controller(host, bounds), _view(view)
	{
		_view._splitter_active++;
	}

	~splitter_controller() override
	{
		if (_tracking)
		{
			escape();
		}

		_view._splitter_active--;
	}

	void draw(ui::draw_context& rc) override
	{
		_view.draw_splitter(rc, _view.calc_spliter_bounds(rc.scroll_width), _tracking);
	}

	ui::style::cursor cursor() const override
	{
		return ui::style::cursor::left_right;
	}

	void update_pos(const pointi loc)
	{
		_view.splitter_pos(loc.x, _tracking);
	}

	void on_mouse_left_button_down(const pointi loc, const ui::key_state keys) override
	{
		_tracking = true;
		_start = _view.splitter_pos();
		_start_loc = loc;
		update_pos(loc);
	}

	void on_mouse_move(const pointi loc) override
	{
		if (_tracking)
		{
			update_pos(loc);
		}
	}

	void on_mouse_left_button_up(const pointi loc, const ui::key_state keys) override
	{
		if (_tracking)
		{
			_tracking = false;
			update_pos(loc);
		}
	}

	void escape() override
	{
		_tracking = false;
		_view.splitter_pos(_start, false);
	}
};

recti items_view::calc_spliter_bounds(const int scroll_width) const
{
	const auto media_can_scroll = _media_scroller.can_scroll();
	const auto split_x = splitter_pos();
	const auto control_padding = scroll_width / 2;

	return {
		split_x - control_padding, 0, split_x + control_padding,
		media_can_scroll ? _client_extent.cy / 2 : _client_extent.cy
	};
}

view_controller_ptr items_view::controller_from_location(const view_host_ptr& host, const pointi loc)
{
	view_controller_ptr controller;

	const recti split_bounds = calc_spliter_bounds(_scroll_width);

	if (split_bounds.contains(loc))
	{
		controller = std::make_shared<splitter_controller>(host, *this, split_bounds);
	}
	else
	{
		const auto media_can_scroll = _media_scroller.can_scroll();
		const auto media_bounds = calc_media_bounds();

		if (media_can_scroll && _media_scroller.scroll_bounds().contains(loc))
		{
			controller = std::make_shared<scroll_controller>(host, _media_scroller, _media_scroller.scroll_bounds());
		}

		if (media_bounds.contains(loc))
		{
			const auto media_offset = -_media_scroller.scroll_offset();

			if (!controller)
			{
				for (const auto& e : _media_elements)
				{
					controller = e->controller_from_location(host, loc, media_offset, {});
					if (controller) break;
				}
			}
		}
		else
		{
			if (_items_scroller.can_scroll() && _items_scroller.scroll_bounds().contains(loc))
			{
				controller = std::make_shared<
					scroll_controller>(host, _items_scroller, _items_scroller.scroll_bounds());
			}

			if (!controller)
			{
				const auto items_offset = -_items_scroller.scroll_offset();
				const auto log_loc = _items_scroller.device_to_logical(loc);

				for (const auto& e : _item_elements)
				{
					controller = e->controller_from_location(host, loc, items_offset, {});
					if (controller) break;
				}

				if (!controller)
				{
					for (const auto& group : _state.groups())
					{
						if (group->bounds.contains(log_loc))
						{
							const auto i = group->drawable_from_layout_location(log_loc);

							if (i && i->is_selected() && _state.can_process_selection(
								host, df::process_items_type::local_file_or_folder))
							{
								controller = std::make_shared<item_drag_controller>(
									host, *this, _state, _items_scroller, i, loc);
							}
							else
							{
								controller = std::make_shared<item_select_controller>(
									host, *this, _state, _items_scroller, i, loc);
							}

							break;
						}
					}
				}
			}
		}
	}

	if (!controller && recti(_client_extent).contains(loc))
	{
		controller = std::make_shared<item_select_controller>(host, *this, _state, _items_scroller, nullptr, loc);
	}

	return controller;
}

menu_type items_view::context_menu(const pointi loc)
{
	if (is_over_items(loc))
	{
		const auto i = _state.item_from_location(loc + _items_scroller.scroll_offset());

		if (i && !i->is_selected())
		{
			_state.select(_host, i, false, false, false);
		}

		return menu_type::items;
	}

	return menu_type::media;
}

void items_view::mouse_wheel(const pointi loc, const int zDelta, const ui::key_state keys)
{
	if (keys.control)
	{
		setting.item_scale = std::clamp(setting.item_scale + (zDelta > 0 ? 1 : -1), 0,
			settings_t::item_scale_count - 1);
		_state.invalidate_view(view_invalid::view_layout);
	}
	else if (_media_scroller.can_scroll() && is_over_media(loc))
	{
		_media_scroller.offset(_host, 0, -zDelta);
	}
	else if (is_over_items(loc))
	{
		_items_scroller.offset(_host, 0, -zDelta);
	}

	_state.invalidate_view(view_invalid::controller);
}

void items_view::update_visible_items_list()
{
	std::vector<item_and_group> new_visible_items;
	new_visible_items.reserve(_visible_items.size());

	const auto logical_items_bounds = calc_logical_items_bounds();
	const auto extended_logical_items_bounds = logical_items_bounds.inflate(0, logical_items_bounds.height() / 2);

	for (const auto& group : _state.groups())
	{
		if (group->bounds.intersects(extended_logical_items_bounds))
		{
			for (const auto& i : group->items())
			{
				if (i->bounds.intersects(extended_logical_items_bounds))
				{
					new_visible_items.emplace_back(group, i);
				}
			}
		}
	}

	std::ranges::sort(new_visible_items);

	if (_visible_items != new_visible_items)
	{
		_visible_items = std::move(new_visible_items);
		df::trace("items_view::update_visible_items_list changed"sv);

		const auto visible_center_loc = logical_items_bounds.center();
		auto visible_center_distance = INT_MAX;
		df::item_element_ptr center_element;

		df::item_set db_thumbnail_pending;

		for (const auto& i : _visible_items)
		{
			if (i.i->bounds.intersects(extended_logical_items_bounds))
			{
				const auto distance_to_center = i.i->bounds.center().dist_sqrd(visible_center_loc);

				if (distance_to_center < visible_center_distance)
				{
					visible_center_distance = distance_to_center;
					center_element = i.i;
				}

				if (i.i->db_thumbnail_pending())
				{
					i.i->add_to(db_thumbnail_pending);
				}
			}
		}


		_layout_center_item = center_element;

		if (!db_thumbnail_pending.empty())
		{
			_state._async.queue_database(
				[&s = _state, db_thumbnail_pending = std::move(db_thumbnail_pending), visible_items = _visible_items](
					database& db)
				{
					db.load_thumbnails(s.item_index, db_thumbnail_pending);

					df::file_items load;

					for (const auto& i : visible_items)
					{
						i.i->add_if_thumbnail_load_needed(load);
					}

					if (!load.empty())
					{
						s.item_index.queue_scan_displayed_items(std::move(load));
					}
				});
		}
		else
		{
			df::file_items load;

			for (const auto& i : _visible_items)
			{
				i.i->add_if_thumbnail_load_needed(load);
			}

			if (!load.empty())
			{
				_state.item_index.queue_scan_displayed_items(std::move(load));
			}
		}
	}
}


void items_view::render(ui::draw_context& dc, const view_controller_ptr controller)
{
	const auto media_offset = -_media_scroller.scroll_offset();

	_display->media_offset = media_offset;

	if (_display->zoom() || _display->comparing())
	{
		if (_media_element)
		{
			_media_element->render(dc, -media_offset);
		}
	}
	else
	{
		const auto items_offset = -_items_scroller.scroll_offset();
		const auto logical_view_bounds = calc_logical_items_bounds();

		if (!_items_scroller._active)
		{
			_items_scroller.draw_scroll(dc);
		}

		if (!_media_scroller._active)
		{
			_media_scroller.draw_scroll(dc);
		}

		if (_splitter_active == 0)
		{
			draw_splitter(dc, calc_spliter_bounds(dc.scroll_width), false);
		}

		for (const auto& e : _item_elements)
		{
			if (e->bounds.intersects(logical_view_bounds))
			{
				e->render(dc, items_offset);
			}
		}

		for (const auto& e : _media_elements)
		{
			e->render(dc, media_offset);
		}

		const auto& state_focus = _state.focus_item();
		const auto has_related = _state.search().has_related();

		item_and_group focus;
		item_and_group hover;
		std::unordered_set<df::item_group_ptr> update_row_layout_groups;

		for (const auto& ii : _visible_items)
		{
			const bool is_detail_group = ii.g->_display == df::item_group_display::detail;

			if (ii.i->bounds.intersects(logical_view_bounds))
			{
				if (is_detail_group && !ii.i->row_layout_valid)
				{
					ii.g->update_detail_row_layout(dc, ii.i, has_related);
					update_row_layout_groups.emplace(ii.g);
				}
			}

			if (ii.i->is_style_bit_set(view_element_style::hover))
			{
				hover = { ii.g, ii.i };
			}

			if (ii.i == state_focus)
			{
				focus = { ii.g, ii.i };
			}
		}

		for (const auto& g : update_row_layout_groups)
		{
			g->update_row_layout(dc);
		}

		for (const auto& i : _visible_items)
		{
			if (i.i != focus.i && i.i != hover.i)
			{
				i.i->render_bg(dc, *i.g, items_offset);
			}
		}

		for (const auto& i : _visible_items)
		{
			if (i.i != focus.i && i.i != hover.i)
			{
				i.i->render(dc, *i.g, items_offset);
			}
		}

		if (focus.i && focus.i != hover.i)
		{
			focus.i->render_bg(dc, *focus.g, items_offset);
			focus.i->render(dc, *focus.g, items_offset);
		}

		if (hover.i)
		{
			hover.i->render_bg(dc, *hover.g, items_offset);
			hover.i->render(dc, *hover.g, items_offset);
		}
	}
}

void items_view::make_visible(const df::item_element_ptr& i)
{
	if (i)
	{
		const auto scroll_offset = _items_scroller.scroll_offset();
		const auto bounds = i->bounds;
		auto point_offset = scroll_offset;
		const auto size_client = client_size();

		if (bounds.top < scroll_offset.y)
		{
			point_offset.y = bounds.top;
		}
		else if (bounds.bottom > (scroll_offset.y + size_client.cy))
		{
			point_offset.y = bounds.bottom - size_client.cy;
		}

		_items_scroller.scroll_offset(_host, point_offset.x, point_offset.y);
	}
}

bool items_view::is_visible(const df::item_element_ptr& i) const
{
	if (i)
	{
		const auto logical_bounds = calc_logical_items_bounds();
		return i->bounds.intersects(logical_bounds);
	}

	return false;
}

void items_view::update_item_scroller_sections()
{
	std::vector<view_scroller_section> sections;

	if (!_state.groups().empty())
	{
		const auto min_section_height = 8;
		auto last_text = _state.groups().front()->scroll_text;
		auto last_icon = _state.groups().front()->icon;
		auto last_y = 0;

		for (const auto& group : _state.groups())
		{
			const auto r = group->bounds;
			const auto text = group->scroll_text;
			const auto icon = group->icon;
			const auto y = r.top;

			if (str::icmp(last_text, text) != 0 ||
				last_icon != icon)
			{
				const auto cy = _items_scroller.logical_to_scrollbar_pos(y - last_y);

				if (cy > min_section_height)
				{
					sections.emplace_back(last_text, last_icon, y);
				}

				last_text = text;
				last_icon = icon;
				last_y = y;
			}
		}

		const auto yy = _state.groups().back()->bounds.bottom;
		const auto cy = _items_scroller.logical_to_scrollbar_pos(yy - last_y);

		if (cy > min_section_height)
		{
			sections.emplace_back(last_text, last_icon, yy);
		}
	}

	_items_scroller.sections(std::move(sections));
}

void items_view::layout(ui::measure_context& mc, const sizei extent)
{
	_client_extent = extent;
	_scroll_width = mc.scroll_width;

	df::assert_true(ui::is_ui_thread());

	ui::control_layouts positions;

	const auto is_zoom = _display->zoom() || _display->comparing();
	auto avail_media_bounds = calc_media_bounds();

	if (is_zoom)
	{
		if (_media_element)
		{
			_media_element->layout(mc, avail_media_bounds, positions);
			_media_scroller.layout({}, avail_media_bounds, {});
		}
	}
	else
	{
		const auto image_padding = df::round(8 * mc.scale_factor);
		avail_media_bounds.left += image_padding;
		avail_media_bounds.right -= image_padding / 2;
		//avail_media_bounds.top += image_padding;
		//avail_bounds.bottom -= image_padding;

		const auto logical_items_bounds = calc_logical_items_bounds();
		const auto& focus = _state.focus_item();
		const auto& anchor_item = (focus && focus->bounds.intersects(logical_items_bounds))
			? focus
			: _layout_center_item;

		const auto media_height = stack_elements(mc, positions, avail_media_bounds, _media_elements, true);
		const auto split_x = splitter_pos();
		const auto control_padding = mc.scroll_width / 2;
		const recti media_scroll_bounds{
			split_x - control_padding, _client_extent.cy / 2, split_x + control_padding, _client_extent.cy
		};

		_media_scroller.layout({ avail_media_bounds.width(), media_height }, avail_media_bounds, media_scroll_bounds);

		const auto scroll_text_width = mc.measure_text(u8"88888"sv, ui::style::font_face::dialog,
			ui::style::text_style::single_line, _client_extent.cx / 5).cx;
		const auto items_scroll_offset = _items_scroller.scroll_offset();
		const auto anchor_item_y = anchor_item ? anchor_item->bounds.top - items_scroll_offset.y : 0;
		auto item_layout_iteration_count = 1;

		for (auto i = 0; i < item_layout_iteration_count; i++)
		{
			const auto show_scroll_items = _items_scroller.can_scroll();
			const auto scroll_padding = show_scroll_items ? scroll_text_width : mc.padding1;
			auto avail_item_bounds = calc_items_bounds();
			avail_item_bounds.right -= scroll_padding;
			const auto items_height = stack_elements(mc, positions, avail_item_bounds, _item_elements, false,
				{ 0, mc.padding2 });

			const auto item_scroll_bounds = recti{
				_client_extent.cx - scroll_padding, 0, _client_extent.cx, _client_extent.cy
			};
			const sizei scroll_extent = { avail_item_bounds.width(), items_height };

			_items_scroller.layout(scroll_extent, avail_item_bounds, item_scroll_bounds);

			if (show_scroll_items != _items_scroller.can_scroll())
			{
				// can scroll change - redo layout
				item_layout_iteration_count = 2;
			}
		}

		update_item_scroller_sections();

		if (anchor_item)
		{
			_items_scroller.scroll_offset(_host, 0, anchor_item->bounds.top - anchor_item_y);
		}
	}

	update_visible_items_list();

	_host->frame()->invalidate();
}

void items_view::broadcast_event(const view_element_event& event) const
{
	for (const auto& i : _media_elements)
	{
		i->dispatch_event(event);
	}

	for (const auto& i : _item_elements)
	{
		i->dispatch_event(event);
	}
}

class copy_clip_element final : public std::enable_shared_from_this<copy_clip_element>, public view_element
{
	metadata_kv_list _kv;

public:
	copy_clip_element(metadata_kv_list kv) noexcept : view_element(
		view_element_style::right_justified |
		view_element_style::has_tooltip |
		view_element_style::can_invoke), _kv(std::move(kv))
	{
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		const auto logical_bounds = bounds.offset(element_offset);
		const auto bg = calc_background_color(dc);
		xdraw_icon(dc, icon_index::edit_copy, logical_bounds, ui::color(dc.colors.foreground, dc.colors.alpha), bg);
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		return { mc.icon_cxy, mc.icon_cxy };
	}

	void dispatch_event(const view_element_event& event) override
	{
		if (event.type == view_element_event_type::invoke)
		{
			u8ostringstream s;

			for (const auto& m : _kv)
			{
				s << m.first.sv();
				s << u8": "sv;
				s << m.second;
				s << std::endl;
			}

			platform::set_clipboard(s.str());
		}
	}

	void tooltip(view_hover_element& hover, const pointi loc, const pointi element_offset) const override
	{
		hover.elements.add(make_icon_element(icon_index::edit_copy, view_element_style::no_break));
		hover.elements.add(std::make_shared<text_element>(tt.copy_to_clipboard));
		hover.active_bounds = hover.window_bounds = bounds.offset(element_offset);
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, const pointi loc,
		const pointi element_offset,
		const std::vector<recti>& excluded_bounds) override
	{
		return default_controller_from_location(*this, host, loc, element_offset, excluded_bounds);
	}
};

inline view_element_ptr title_style(view_element_ptr e)
{
	e->padding(8);
	e->margin(4, 8);
	e->set_style_bit(view_element_style::background, true);
	return e;
}

inline view_element_ptr media_control_style(view_element_ptr e)
{
	e->padding(4);
	e->margin(4, 0);
	e->set_style_bit(view_element_style::background, true);
	return e;
}

inline view_element_ptr margin8(view_element_ptr e)
{
	e->margin.cx = 8;
	e->margin.cy = 0;
	return e;
}

inline view_element_ptr margin16(view_element_ptr e)
{
	e->margin.cx = 16;
	e->margin.cy = 0;
	return e;
}

static std::u8string_view format_metadata_standard(const metadata_standard ms)
{
	switch (ms)
	{
	case metadata_standard::media: return tt.media_metadata_title;
	case metadata_standard::exif: return tt.exif_metadata_title;
	case metadata_standard::iptc: return tt.iptc_metadata_title;
	case metadata_standard::xmp: return tt.xmp_metadata_title;
	case metadata_standard::raw: return tt.raw_metadata_title;
	case metadata_standard::ffmpeg: return tt.media_metadata_title;
	case metadata_standard::icc: return tt.icc_metadata_title;
	default:;
	}

	return {};
}

class cover_art_control final : public view_element, public std::enable_shared_from_this<cover_art_control>
{
public:

	ui::const_surface_ptr _surface;
	mutable ui::texture_ptr _tex;
	mutable int _cx_surface = 0;

	std::shared_ptr<ui::group_control> _controls = std::make_shared<ui::group_control>();

	cover_art_control() : view_element(view_element_style::has_tooltip | view_element_style::can_invoke)
	{
	}

	void add(const view_element_ptr& p)
	{
		_controls->add(p);
	}

	void add(const ui::surface_ptr& s)
	{
		_surface = s;
	}

	bool is_control_area(const pointi loc, const pointi element_offset) const override
	{
		return _controls->is_control_area(loc, element_offset);
	}

	void dispatch_event(const view_element_event& event) override
	{
		_controls->dispatch_event(event);
	}

	void hover(interaction_context& ic) override
	{
		_controls->hover(ic);
	}

	sizei measure(ui::measure_context& mc, const int cx) const override
	{
		auto avail = cx;
		auto cy = 0;

		_cx_surface = 0;

		if (is_valid(_surface))
		{
			const auto surf_cx = _surface->width() + mc.padding2;
			const auto show_surface = surf_cx < (cx / 2);

			if (show_surface)
			{
				_cx_surface = surf_cx;
				cy = _surface->height() + (mc.padding2 * 2);
				avail -= surf_cx;
			}
		}

		const auto controls_extent = _controls->measure(mc, avail);
		cy = std::max(cy, controls_extent.cy);
		return { cx, cy };
	}

	void layout(ui::measure_context& mc, const recti bounds, ui::control_layouts& positions) override
	{
		view_element::layout(mc, bounds, positions);

		auto contol_bounds = bounds;
		contol_bounds.left += _cx_surface;
		_controls->layout(mc, contol_bounds, positions);
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		if (_cx_surface > 0 && is_valid(_surface))
		{
			if (!_tex)
			{
				const auto t = dc.create_texture();

				if (t && t->update(_surface) != ui::texture_update_result::failed)
				{
					_tex = t;
				}
			}

			if (_tex)
			{
				const auto extent = _tex->dimensions();
				auto top_left = bounds.top_left();
				top_left.x += dc.padding2 / 2;
				top_left.y += dc.padding2;

				const recti surface_bounds = { top_left, extent };
				dc.draw_texture(_tex, surface_bounds.offset(element_offset));
			}
		}

		_controls->render(dc, element_offset);
	}

	void visit_controls(const std::function<void(const ui::control_base_ptr&)>& handler) override
	{
		_controls->visit_controls(handler);
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, const pointi loc,
		const pointi element_offset,
		const std::vector<recti>& excluded_bounds) override
	{
		return _controls->controller_from_location(host, loc, element_offset, excluded_bounds);
	}
};

void items_view::update_media_elements()
{
	df::assert_true(ui::is_ui_thread());

	const auto display = _state.display_state();
	_display = display;

	std::vector<view_element_ptr> elements;

	if (display)
	{
		const auto media_control_element = _state.create_selection_controls();

		if (display->is_one())
		{
			const auto& item = display->_item1;
			const auto* const file_type = item->file_type();
			const auto md = item->metadata();

			if (file_type->has_trait(file_traits::bitmap))
			{
				_media_element = std::make_shared<photo_control>(_state, display, _host, false);

				elements.emplace_back(_media_element);
				elements.emplace_back(std::make_shared<padding_element>(4));
				elements.emplace_back(media_control_style(media_control_element));
			}
			else if (file_type->has_trait(file_traits::visualize_audio))
			{
				_media_element = std::make_shared<audio_control>(_state, display, _host);

				elements.emplace_back(_media_element);
				elements.emplace_back(std::make_shared<padding_element>(4));
				elements.emplace_back(media_control_style(media_control_element));
			}
			else if (file_type->has_trait(file_traits::av))
			{
				_media_element = std::make_shared<video_control>(_state, display, _host);

				elements.emplace_back(_media_element);
				elements.emplace_back(std::make_shared<padding_element>(4));
				elements.emplace_back(media_control_style(media_control_element));
			}
			else if (file_type->has_trait(file_traits::archive))
			{
				elements.emplace_back(media_control_style(media_control_element));
				elements.emplace_back(std::make_shared<padding_element>(4));
				elements.emplace_back(std::make_shared<file_list_control>(display, view_element_style::center));
			}
			else if (file_type->has_trait(file_traits::commodore))
			{
				elements.emplace_back(media_control_style(media_control_element));

				if (display && !display->_selected_item_data.empty())
				{
					elements.emplace_back(std::make_shared<padding_element>(4));
					elements.emplace_back(std::make_shared<comodore_disk_control>(display, view_element_style::center));
				}
			}
			else
			{
				elements.emplace_back(media_control_style(media_control_element));

				if (display && !display->_selected_item_data.empty())
				{
					elements.emplace_back(std::make_shared<hex_control>(display, view_element_style::center));

					if (item->file_size().to_int64() > df::one_meg)
					{
						elements.emplace_back(std::make_shared<divider_element>());
						auto element = std::make_shared<text_element>(tt.truncated_at_one_mb,
							ui::style::font_face::title,
							ui::style::text_style::multiline_center,
							view_element_style::new_line |
							view_element_style::center |
							view_element_style::important);
						element->padding = { 8, 8 };
						element->margin = { 8, 8 };
						elements.emplace_back(element);
					}
					else
					{
						elements.emplace_back(std::make_shared<padding_element>(32));
					}
				}
			}

			if (md)
			{
				const auto has_description = !is_empty(md->comment) || !is_empty(md->description) || !is_empty(md->synopsis);

				if (item->has_cover_art() && has_description)
				{
					auto description = std::make_shared<cover_art_control>();

					if (item->has_cover_art())
					{
						files ff;
						auto surface = ff.image_to_surface(item->cover_art());
						description->add(surface);
					}

					if (!is_empty(md->comment))
					{
						description->add(title_style(std::make_shared<group_title_control>(tt.prop_name_comment)));
						description->add(margin16(std::make_shared<text_element>(md->comment)));
					}

					if (!is_empty(md->description))
					{
						description->add(title_style(std::make_shared<group_title_control>(tt.prop_name_description)));
						description->add(margin16(std::make_shared<text_element>(md->description)));
					}

					if (!is_empty(md->synopsis))
					{
						description->add(title_style(std::make_shared<group_title_control>(tt.prop_name_synopsis)));
						description->add(margin16(std::make_shared<text_element>(md->synopsis)));
					}
					elements.emplace_back(description);
				}
			}

			if (setting.verbose_metadata)
			{
				if (display && !display->_player_media_info.streams.empty())
				{
					elements.emplace_back(title_style(std::make_shared<group_title_control>(tt.prop_name_streams)));

					const auto table = std::make_shared<ui::table_element>(view_element_style::grow);
					table->no_shrink_col[0] = true;
					table->no_shrink_col[1] = true;
					table->no_shrink_col[2] = false;
					table->no_shrink_col[3] = true;
					table->no_shrink_col[4] = true;
					table->no_shrink_col[5] = true;
					table->no_shrink_col[6] = true;

					for (const auto& st : display->_player_media_info.streams)
					{
						std::u8string type;

						switch (st.type)
						{
						case av_stream_type::video: type = tt.video;
							break;
						case av_stream_type::audio: type = tt.audio;
							break;
						case av_stream_type::data: type = tt.data;
							break;
						case av_stream_type::subtitle: type = tt.subtitle;
							break;
						default:;
						}

						auto format = st.pixel_format;

						if (st.type == av_stream_type::audio)
						{
							format = prop::format_audio_sample_rate(st.audio_sample_rate);

							if (st.audio_channels != 0)
							{
								format += u8"  "sv;
								format += prop::format_audio_channels(st.audio_channels);
							}

							if (st.audio_sample_type != prop::audio_sample_t::none)
							{
								format += u8"  "sv;
								format += format_audio_sample_type(st.audio_sample_type);
							}
						}

						auto stream = std::make_shared<stream_element>(_state, item, st);

						std::vector<view_element_ptr> row = {
							std::make_shared<text_element>(str::to_string(st.index)),
							std::make_shared<text_element>(type),
							stream,
							std::make_shared<text_element>(st.codec),
							std::make_shared<text_element>(st.fourcc),
							//std::make_shared<text_element>(st.language),								
							std::make_shared<text_element>(format),
							std::make_shared<text_element>(st.rotation == 0.0
															   ? std::u8string{}
															   : str::format(u8"rotation={}"sv, st.rotation))
						};

						table->add(row);
					}

					elements.emplace_back(margin16(table));
				}

				if (display && !display->_player_media_info.metadata.empty())
				{
					for (const auto& m : display->_player_media_info.metadata)
					{
						if (!m.second.empty())
						{
							elements.emplace_back(title_style(std::make_shared<group_title_control>(
								format_metadata_standard(m.first), std::vector<view_element_ptr>{
								std::make_shared<copy_clip_element>(m.second)
							})));

							auto table = std::make_shared<ui::table_element>(view_element_style::grow);

							for (const auto& r : m.second)
							{
								table->add(r.first, r.second);
							}

							elements.emplace_back(margin16(table));
						}
					}
				}
			}

			elements.emplace_back(std::make_shared<divider_element>());

			auto verbose_element = std::make_shared<link_element>(
				setting.verbose_metadata ? tt.hide_verbose_metadata : tt.show_verbose_metadata,
				commands::verbose_metadata, ui::style::font_face::dialog, ui::style::text_style::multiline_center,
				view_element_style::new_line | view_element_style::center);
			elements.emplace_back(verbose_element);

			elements.emplace_back(std::make_shared<padding_element>(8));
		}
		else if (display->is_two())
		{
			_media_element = std::make_shared<side_by_side_control>(display);
			elements.emplace_back(_media_element);
			elements.emplace_back(media_control_style(media_control_element));
		}
		else if (display->is_multi())
		{
			_media_element = std::make_shared<images_control2>(_state, display);
			elements.emplace_back(_media_element);
			elements.emplace_back(media_control_style(media_control_element));
		}
		else
		{
			elements.emplace_back(media_control_style(media_control_element));
		}
	}

	std::swap(_media_elements, elements);
}

void items_view::display_changed()
{
	_media_scroller.scroll_offset(_host, 0, 0);
	_state.invalidate_view(view_invalid::view_layout);
};

void items_view::line_up(const bool toggle_selection, const bool extend_selection) const
{
	const auto focus = _state.focus_item();

	if (focus)
	{
		auto top = focus->bounds.top_center();

		// scan up looking for something above
		for (int i = 0; i < 100; i++)
		{
			top.y -= 8;

			auto found = _state.item_from_location(top);

			if (found)
			{
				_state.stop_slideshow();
				_state.select(_host, found, toggle_selection, extend_selection, false);
				return;
			}
		}
	}

	_state.select_next(_host, false, toggle_selection, extend_selection);
}

void items_view::line_down(const bool toggle_selection, const bool extend_selection) const
{
	const auto focus = _state.focus_item();

	if (focus)
	{
		auto bottom = focus->bounds.bottom_center();

		// scan looking for something below
		for (int i = 0; i < 100; i++)
		{
			bottom.y += 8;

			auto found = _state.item_from_location(bottom);

			if (found)
			{
				_state.stop_slideshow();
				_state.select(_host, found, toggle_selection, extend_selection, false);
				return;
			}
		}
	}

	_state.select_next(_host, true, toggle_selection, extend_selection);
}

void items_view::items_scroll_popup(view_hover_element& hover, const pointi loc) const
{
	const auto i = scroll_loc_to_item(loc);

	if (i)
	{
		view_elements elements;

		for (const auto& group : _state.groups())
		{
			for (const auto& ii : group->items())
			{
				if (i == ii)
				{
					group->scroll_tooltip(i->thumbnail(), elements);

					if (!elements.is_empty())
					{
						const auto scroll_bounds = _items_scroller.scroll_bounds();
						const auto top = loc.y;
						const auto left = scroll_bounds.left;
						const auto right = scroll_bounds.right;

						hover.elements = elements;
						hover.window_bounds = recti(left, top, right, top + 16);
						hover.active_bounds = recti(left, loc.y - 1, right, loc.y + 1);
						hover.active_delay = 50;
						hover.horizontal = true;
						hover.prefered_size = 200;
					}
				}
			}
		}
	}
}

df::item_element_ptr items_view::scroll_loc_to_item(const pointi loc) const
{
	const auto scroll_bounds = _items_scroller.scroll_bounds();
	const auto y = df::mul_div(loc.y, _items_scroller._scroll_extent.cy, scroll_bounds.height());

	df::item_group_ptr group;
	df::item_element_ptr item;

	{
		const auto item_groups = _state.groups();
		auto distance = 0;

		for (const auto& g : item_groups)
		{
			const auto bounds = g->bounds;
			const auto pos = (bounds.top + bounds.bottom) / 2;
			const auto d = abs(y - pos);

			if (!group || distance > d)
			{
				group = g;
				distance = d;
			}
		}
	}

	if (group)
	{
		auto distance = 0;

		for (const auto& i : group->items())
		{
			const auto bounds = i->bounds;
			const auto pos = (bounds.top + bounds.bottom) / 2;
			const auto d = abs(y - pos);

			if (!item || distance > d || (distance == d && i->has_thumb() && !item->has_thumb()))
			{
				item = i;
				distance = d;
			}
		}
	}

	return item;
}
