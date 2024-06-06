// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

#include "ui_view.h"

class display_state_t;

struct item_and_group
{
	df::item_group_ptr g;
	df::item_element_ptr i;

	friend bool operator==(const item_and_group& lhs, const item_and_group& rhs)
	{
		return lhs.g == rhs.g
			&& lhs.i == rhs.i;
	}

	friend bool operator!=(const item_and_group& lhs, const item_and_group& rhs)
	{
		return !(lhs == rhs);
	}

	friend bool operator<(const item_and_group& lhs, const item_and_group& rhs)
	{
		if (lhs.g < rhs.g)
			return true;
		if (rhs.g < lhs.g)
			return false;
		return lhs.i < rhs.i;
	}

	friend bool operator<=(const item_and_group& lhs, const item_and_group& rhs)
	{
		return !(rhs < lhs);
	}

	friend bool operator>(const item_and_group& lhs, const item_and_group& rhs)
	{
		return rhs < lhs;
	}

	friend bool operator>=(const item_and_group& lhs, const item_and_group& rhs)
	{
		return !(lhs < rhs);
	}
};

class items_view final : public view_base
{
public:
	using this_type = items_view;

	view_state& _state;
	view_host_ptr _host;

	std::vector<view_element_ptr> _item_elements;
	std::vector<view_element_ptr> _media_elements;

	view_element_ptr _media_element;
	df::item_element_ptr _layout_center_item;

	view_scroller _items_scroller;
	view_scroller _media_scroller;

	pointi _pan_start_loc;
	pointi _pan_items_start_offset;
	pointi _pan_image_start_offset;
	sizei _client_extent;
	display_state_ptr _display;
	int _splitter_active = 0;
	int _scroll_width = 20;

	friend class splitter_controller;

	std::vector<item_and_group> _visible_items;

public:
	items_view(view_state& s, view_host_ptr host);
	~items_view() override = default;

	void activate(sizei extent) override;
	void deactivate() override;

	void render(ui::draw_context& dc, view_controller_ptr controller) override;

	menu_type context_menu(pointi loc) override;

	void update_visible_items_list();
	void layout(ui::measure_context& mc, sizei extent) override;
	void mouse_wheel(pointi loc, int zDelta, ui::key_state keys) override;

	void items_scroll_popup(view_hover_element& hover, pointi loc) const;
	df::item_element_ptr scroll_loc_to_item(pointi loc) const;

	bool is_over_items(const pointi loc) const override
	{
		return splitter_pos() < loc.x;
	}

	bool is_over_media(const pointi loc) const
	{
		return splitter_pos() > loc.x;
	}

	int splitter_pos() const
	{
		return df::mul_div(setting.item_splitter_pos, _client_extent.cx, settings_t::item_splitter_max);
	}

	void splitter_pos(int x, bool tracking)
	{
		if (_state.view_mode() == view_type::items)
		{
			const auto min_size = std::min(96, _client_extent.cx / 2);
			const auto s = df::mul_div(std::clamp(x, min_size, _client_extent.cx - min_size),
			                           settings_t::item_splitter_max, _client_extent.cx);

			if (s != setting.item_splitter_pos)
			{
				setting.item_splitter_pos = s;
				_state.invalidate_view(view_invalid::view_layout);
			}
		}
	}

	void pan_start(const pointi start_loc) override
	{
		/*_pan_start_loc = start_loc;
		_pan_items_start_offset = _items_scroller.scroll_offset();
		_pan_image_start_offset = _media_element->image_offset();*/
	}

	void pan(const pointi start_loc, const pointi current_loc) override
	{
		//const auto offset = start_loc - current_loc;

		//if (is_over_items(start_loc))
		//{
		//	// can only scroll vertically
		//	_items_scroller.scroll_offset(_host, _pan_items_start_offset.x, _pan_items_start_offset.y + offset.y);
		//}
		//else if (_media_element->bounds.contains(start_loc) && _media_element->can_zoom())
		//{
		//	if (!_media_element->zoom())
		//	{
		//		_media_element->zoom(true);
		//	}

		//	_media_element->scroll_image_to(_pan_image_start_offset - offset);
		//}
	}

	void pan_end(const pointi start_loc, const pointi final_loc) override
	{
		/*pan(start_loc, final_loc);
		_media_element->zoom(false);*/
	}

	recti calc_items_bounds() const
	{
		return recti(std::min(_client_extent.cx - 64, splitter_pos() + 4), 0, _client_extent.cx, _client_extent.cy);
	}

	recti calc_logical_items_bounds() const
	{
		const auto scroll_offset = _items_scroller.scroll_offset();
		return calc_items_bounds().offset(scroll_offset);
	}

	recti calc_media_bounds() const
	{
		const auto display = _state.display_state();
		if (display && (display->zoom() || display->comparing()))
			return recti(
				0, 0, _client_extent.cx, _client_extent.cy);
		return recti(0, 0, splitter_pos() - 2, _client_extent.cy);
	}

	recti calc_spliter_bounds(const int scroll_width) const;
	view_controller_ptr controller_from_location(const view_host_ptr& host, pointi loc) override;

	void broadcast_event(const view_element_event& event) const override;

	sizei client_size() const;

	void invalidate_thumb(const df::item_element_ptr& i);
	void make_visible(const df::item_element_ptr& i);
	bool is_visible(const df::item_element_ptr& i) const;
	void update_item_scroller_sections();
	void items_changed(bool path_changed) override;
	void display_changed() override;
	void update_media_elements() override;

	void draw_splitter(ui::draw_context& dc, recti bounds, bool tracking) const;

	void line_up(bool toggle_selection, bool extend_selection) const;
	void line_down(bool toggle_selection, bool extend_selection) const;
};
