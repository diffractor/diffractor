// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Main thumbnail grid and list view. Displays file collections
// with thumbnail rendering, selection, and navigation. Layout calculates one set of
// disjoint regions (sidebar, chrome, splitters, scroll bars, media, items) that render,
// hit testing and wheel routing all share. The commands that decide what the list contains
// are drawn at the head of the list as a command_bar_element rather than in a fixed status band.

#pragma once

#include "ui_view.h"
#include "ui_controls.h"

class display_state_t;
class sidebar_host;
class items_view;
struct metadata_block;

// Shared by the view and every metadata listing it builds. A listing can outlive the rebuild that
// replaced it -- a latched controller still holds one -- so the posture map is refcounted and the
// view drops the hook as it goes, leaving a stranded listing inert rather than dangling.
struct metadata_tree_state
{
	std::map<std::string, bool, std::less<>> expanded;
	std::function<void()> invalidate;
};

using metadata_tree_state_ptr = std::shared_ptr<metadata_tree_state>;


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
	std::vector<view_element_ptr> _media_priority_elements;
	std::vector<view_element_ptr> _media_detail_elements;

	// Which metadata rows the user has opened or closed. Keyed by block and stable row id so the
	// posture survives the element rebuild that every selection change performs.
	metadata_tree_state_ptr _metadata_tree = std::make_shared<metadata_tree_state>();

	view_element_ptr _media_element;
	df::item_element_ptr _layout_center_item;

	view_scroller _items_scroller;
	view_scroller _media_scroller;
	view_scroll_anchor _pending_items_anchor;
	view_scroll_anchor _pending_media_anchor;
	bool _reset_media_scroll = false;

	sizei _client_extent;
	display_state_ptr _display;
	int _splitter_active = 0;
	int _sidebar_splitter_active = 0;
	int _scroll_width = 20;
	int _top_chrome_height = 0;
	int _sidebar_width = 0;
	int _sidebar_splitter_width = 0;
	double _scale_factor = 1.0;

	// The address and filter boxes are the same control with different callbacks; sharing one
	// element keeps their keyboard, mouse and caret behavior identical.
	edit_element_ptr _filter_edit = std::make_shared<edit_element>();

	// locations.md 7.3: an optional control row that can shrink to a one-line header when the
	// control area would otherwise bury the items the user came to see.
	struct control_row
	{
		std::shared_ptr<view_elements> element;
		view_element_ptr header;
		std::vector<view_element_ptr> content;

		void reset()
		{
			element.reset();
			header.reset();
			content.clear();
		}

		void collapsed(const bool v) const
		{
			if (header) header->is_visible(v);
			for (const auto& c : content) c->is_visible(!v);
		}
	};

	control_row _breakdown_row;

	// An explicit expansion outranks the budget's guess and lasts the session.
	bool _breakdown_expanded = false;

	// locations.md 6.3 / 7.2: the two derived control-bar rows. Both are rebuilt from published
	// worker results, so neither ever reads the index or the gazetteer while building.

	view_element_ptr build_location_breakdown_row();
	view_element_ptr build_related_header_row() const;
	void apply_control_row_budget(ui::measure_context& mc, int width, int budget);

	view_element_ptr _items_scroll_top;
	recti _items_scroll_top_bounds;

	// Every interactive area of the view is a rectangle in this set. update_regions() is the only
	// writer and runs during layout; render, hit testing, wheel routing and context menus all read
	// it, so what is drawn, what is clicked and what is scrolled can never disagree. Regions are
	// hit tested in the fixed priority order listed here.
	struct view_regions
	{
		recti sidebar_splitter;
		recti sidebar;
		recti splitter;
		recti items_scroll;
		recti items_scroll_top;
		recti media_scroll;
		recti media;
		recti items;
	};

	view_regions _regions;

	std::shared_ptr<sidebar_host> _sidebar;

	friend class splitter_controller;
	friend class sidebar_splitter_controller;

	std::vector<item_and_group> _visible_items;
	// Reused by render so a frame over a large collection performs no allocation.
	std::vector<item_and_group> _draw_items;
	bool _visible_items_valid = false;
	double _next_thumbnail_retry = 0.0;
	double _zoom_wheel_delta = 0.0;

	items_view(view_state& s, view_host_ptr host);
	~items_view() override;
	const std::shared_ptr<sidebar_host>& sidebar() const { return _sidebar; }

	void activate(sizei extent) override;
	void deactivate() override;
	void refresh() override;

	void render(ui::draw_context& dc, view_controller_ptr controller) override;

	menu_type context_menu(pointi loc) override;

	void update_visible_items_list();
	void stage_visible_thumbnails();
	void retry_visible_thumbnails(double time_now);
	void layout(ui::measure_context& mc, sizei extent) override;
	void mouse_wheel(pointi loc, int zDelta, ui::key_state keys) override;
	void mouse_down(pointi loc) override;
	void focus(bool has_focus) override;
	bool key_down(char32_t key, ui::key_state keys) override;
	bool text_input(std::string_view text) override;

	ui::focus_mode focus_mode() const override
	{
		return _filter_edit->focused() ? ui::focus_mode::text_edit : ui::focus_mode::view;
	}

	bool is_caption_area(pointi loc) const override;

	void items_scroll_popup(view_hover_element& hover, pointi loc) const;
	group_and_item scroll_loc_to_item(pointi loc) const;

	// recti::contains is inclusive, so an empty (all-zero) region would otherwise claim the origin.
	static bool region_hit(const recti r, const pointi loc)
	{
		return !r.is_empty() && r.contains(loc);
	}

	bool is_over_items(const pointi loc) const override
	{
		return region_hit(_regions.items, loc);
	}

	bool is_over_media(const pointi loc) const
	{
		return region_hit(_regions.media, loc);
	}

	int splitter_pos() const
	{
		const auto left = content_left();
		return left + df::mul_div(setting.item_splitter_pos, std::max(0, _client_extent.cx - left),
		                          settings_t::item_splitter_max);
	}

	void splitter_pos(const int x, bool tracking) const
	{
		if (_state.view_mode() == view_type::items)
		{
			const auto left = content_left();
			const auto width = std::max(0, _client_extent.cx - left);
			const auto min_size = std::min(_scroll_width * 5, width / 2);
			const auto s = df::mul_div(std::clamp(x - left, min_size, width - min_size),
			                           settings_t::item_splitter_max, std::max(1, width));

			if (s != setting.item_splitter_pos)
			{
				setting.item_splitter_pos = s;
				_state.invalidate_view(view_invalid::view_layout);
			}
		}
	}

	bool sidebar_visible() const;
	int content_left() const;
	recti sidebar_bounds() const;
	recti sidebar_splitter_bounds() const;
	void sidebar_width(int x);

	void pan_start(const pointi start_loc) override
	{
	}

	void pan(const pointi start_loc, const pointi current_loc) override
	{
	}

	void pan_end(const pointi start_loc, const pointi final_loc) override
	{
	}

	recti calc_items_bounds() const
	{
		return _regions.items;
	}

	recti calc_logical_items_bounds() const
	{
		return _regions.items.offset(_items_scroller.scroll_offset());
	}

	recti calc_media_bounds() const
	{
		return _regions.media;
	}

	// The filter box lives in the list control bar, so its device bounds follow the item scroll.
	recti rendered_filter_bounds() const;
	void apply_rendered_filter();
	void focus_rendered_filter();
	void blur_rendered_filter();
	void blur_rendered_filter_if_scrolled_away();

	void layout_chrome(ui::measure_context& mc);
	void update_edit_caret();
	recti calc_spliter_bounds() const { return _regions.splitter; }
	void update_regions();
	view_controller_ptr media_controller_from_location(const view_host_ptr& host, pointi loc);
	view_controller_ptr items_controller_from_location(const view_host_ptr& host, pointi loc);
	view_controller_ptr controller_from_location(const view_host_ptr& host, pointi loc) override;

	void broadcast_event(const view_element_event& event) const override;

	void make_visible(const df::item_element_ptr& i);
	bool is_visible(const df::item_element_ptr& i) const;
	void update_item_scroller_sections();
	void items_changed(bool path_changed) override;
	void display_changed() override;
	void update_media_elements() override;
	void add_metadata_elements(std::vector<view_element_ptr>& elements, const metadata_block& block);
	void add_description_elements(std::vector<view_element_ptr>& elements, const df::item_element_ptr& item,
	                              const prop::item_metadata_const_ptr& md);

	void draw_splitter(ui::draw_context& dc, recti bounds, bool active, bool tracking) const;

	void line_up(bool toggle_selection, bool extend_selection) const;
	void line_down(bool toggle_selection, bool extend_selection) const;
};
