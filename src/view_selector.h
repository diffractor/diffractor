// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Compact horizontal item selector used by edit and locate workflows.

#pragma once

#include "ui_view.h"

class selector_view final : public view_base, public std::enable_shared_from_this<selector_view>
{
public:
	using select_item_fn = std::function<void(const df::item_element_ptr&, ui::key_state)>;
	using item_filter_fn = std::function<bool(const df::item_element_ptr&)>;

private:
	struct selector_item
	{
		df::item_element_ptr item;
		recti bounds;
		ui::const_image_ptr image;
		ui::texture_ptr texture;
		// Decoded on a worker and published here; the texture is created on the next paint because a
		// device resource may only be made on the UI thread.
		ui::surface_ptr surface;
		bool decode_pending = false;
	};

	view_state& _state;
	view_host_ptr _host;
	select_item_fn _select_item;
	item_filter_fn _item_filter;
	std::vector<selector_item> _items;
	df::item_element_ptr _selection_anchor;
	sizei _extent;
	int _scroll_x = 0;
	int _content_width = 0;
	int _gap = 0;
	int _scrollbar_height = 0;
	bool _active = false;
	// Item bounds only exist once layout has run, so activation defers the scroll that reveals focus.
	bool _scroll_to_focus = false;

	void rebuild_items();
	void clamp_scroll();
	void update_visible_items();
	selector_item* item_from_location(pointi loc);
	const selector_item* item_from_location(pointi loc) const;

public:
	selector_view(view_state& state, view_host_ptr host, select_item_fn select_item);
	void filter(item_filter_fn item_filter);
	void reset_selection_anchor() { _selection_anchor.reset(); }
	void selection_anchor(df::item_element_ptr item) { _selection_anchor = std::move(item); }
	df::item_elements selection_range(const df::item_element_ptr& item) const;
	static quadd thumbnail_destination(sizei texture_dimensions, recti image_bounds, ui::orientation orientation,
	                                   bool show_rotated);

	void activate(sizei extent) override;
	void deactivate() override;
	void refresh() override;
	void items_changed(bool path_changed) override;
	void display_changed() override;
	void layout(ui::measure_context& mc, sizei extent) override;
	void render(ui::draw_context& dc, view_controller_ptr controller) override;
	void mouse_wheel(pointi loc, int z_delta, ui::key_state keys) override;
	void mouse_hwheel(pointi loc, int z_delta, ui::key_state keys) override;
	view_controller_ptr controller_from_location(const view_host_ptr& host, pointi loc) override;
	void broadcast_event(const view_element_event& event) const override;

	void make_visible(const df::item_element_ptr& item);
	bool can_scroll() const;
	recti scrollbar_bounds() const;
	recti scrollbar_thumb_bounds() const;
	void scrollbar_to(int x);
	void scroll_by(int delta_x);
};
