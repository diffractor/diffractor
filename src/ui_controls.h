// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

#include "app_commands.h"
#include "ui_controllers.h"
#include "ui_elements.h"

class display_state_t;

static std::u8string_view orientation_to_string(const ui::orientation& o) noexcept
{
	switch (o)
	{
	case ui::orientation::top_left: return tt.orientation_top_left;
	case ui::orientation::top_right: return tt.orientation_top_right;
	case ui::orientation::bottom_right: return tt.orientation_bottom_right;
	case ui::orientation::bottom_left: return tt.orientation_bottom_left;
	case ui::orientation::left_top: return tt.orientation_left_top;
	case ui::orientation::right_top: return tt.orientation_right_top;
	case ui::orientation::right_bottom: return tt.orientation_right_bottom;
	case ui::orientation::left_bottom: return tt.orientation_left_bottom;
	default: break;
	}

	return {};
}

namespace ui
{
	std::vector<recti> layout_images(recti draw_bounds, const std::vector<sizei>& dims);
}

class rating_control final : public std::enable_shared_from_this<rating_control>, public view_element
{
	view_state& _state;

	mutable int _icon_cxy = 0;
	int _hover_rating = 0;
	int last_hover_rating = 0;
	const bool show_accelerator = false;
	const df::item_element_ptr _item;

public:
	rating_control(view_state& s, df::item_element_ptr i, const bool show_accelerator,
	               const view_element_style style_in) noexcept : _state(s), show_accelerator(show_accelerator),
	                                                             _item(std::move(i))
	{
		style |= style_in | view_element_style::has_tooltip | view_element_style::can_invoke;

		if (s.search().is_match(prop::rating, _item->rating()))
		{
			style |= view_element_style::important;
		}

		update_background_color();
	}

	int displayed_rating() const
	{
		if (_item)
		{
			const auto md = _item->metadata();

			if (md && md->rating != 0)
			{
				return md->rating;
			}
		}

		return 0;
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		const auto logical_bounds = bounds.offset(element_offset);
		const auto bg = calc_background_color(dc);
		const auto is_hover = is_style_bit_set(view_element_style::hover);
		const auto rating = is_hover ? last_hover_rating : displayed_rating();
		const auto clr = ui::color(dc.colors.foreground, dc.colors.alpha);

		wchar_t text[6] = {};

		for (auto i = 0; i < 5; i++)
		{
			text[i] = static_cast<wchar_t>(i < rating ? icon_index::star_solid : icon_index::star);
		}

		dc.draw_text(str::utf16_to_utf8(text), logical_bounds, ui::style::font_face::icons,
		             ui::style::text_style::single_line_center, clr, bg);
	}

	void tooltip(view_hover_element& hover, const pointi loc, const pointi element_offset) const override
	{
		hover.elements->add(make_icon_element(icon_index::star, view_element_style::no_break));

		const auto current_rating = displayed_rating();

		if (current_rating == last_hover_rating && current_rating != 0)
		{
			hover.elements->add(std::make_shared<text_element>(str::format(tt.rating_remove_fmt, current_rating)));
		}
		else
		{
			hover.elements->
			      add(std::make_shared<text_element>(format_plural_text(tt.rating_set_fmt, last_hover_rating)));

			if (current_rating != 0)
			{
				hover.elements->add(std::make_shared<text_element>(str::format(tt.rating_remove_fmt, current_rating)));
			}
		}

		auto rating_bounds = bounds.offset(element_offset);
		rating_bounds.right = rating_bounds.left + last_hover_rating * _icon_cxy;
		rating_bounds.left = rating_bounds.right - _icon_cxy;

		hover.window_bounds = rating_bounds;
		hover.active_bounds = rating_bounds;

		if (show_accelerator)
		{
			hover.elements->add(std::make_shared<action_element>(tt.rating_keys));
		}
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		_icon_cxy = mc.icon_cxy;
		return {mc.icon_cxy * 5, mc.icon_cxy};
	}

	void hover(interaction_context& ic) override
	{
		const auto logical_bounds = bounds.offset(ic.element_offset);
		const auto hovering = logical_bounds.contains(ic.loc);
		const auto rating = hovering ? to_rating(logical_bounds, ic.loc) : 0;

		if (_hover_rating != rating)
		{
			_hover_rating = rating;
			ic.invalidate_view = true;
		}

		last_hover_rating = _hover_rating;
	}

	static int to_rating(const recti rating_bounds, const pointi loc)
	{
		// fix - divide by zero!
		return ((loc.x - rating_bounds.left) / std::max(1, rating_bounds.width() / 5)) + 1;
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, const pointi loc,
	                                             const pointi element_offset,
	                                             const std::vector<recti>& excluded_bounds) override
	{
		return default_controller_from_location(*this, host, loc, element_offset, excluded_bounds);
	}

	void dispatch_event(const view_element_event& event) override;
};

class rate_label_control final : public std::enable_shared_from_this<rate_label_control>, public view_element
{
	view_state& _state;
	const df::item_element_ptr _item;
	const bool show_accelerator = false;
	mutable recti _view_bounds;

public:
	rate_label_control(view_state& s, df::item_element_ptr i, const bool show_accelerator,
	                   const view_element_style style_in) noexcept : view_element(
		                                                                 style_in | view_element_style::has_tooltip |
		                                                                 view_element_style::can_invoke), _state(s),
	                                                                 _item(std::move(i)),
	                                                                 show_accelerator(show_accelerator)
	{
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		return {mc.icon_cxy, mc.icon_cxy};
	}

	void render(ui::draw_context& dc, pointi element_offset) const override;
	void dispatch_event(const view_element_event& event) override;
	void tooltip(view_hover_element& hover, pointi loc, pointi element_offset) const override;

	view_controller_ptr controller_from_location(const view_host_ptr& host, const pointi loc,
	                                             const pointi element_offset,
	                                             const std::vector<recti>& excluded_bounds) override
	{
		return default_controller_from_location(*this, host, loc, element_offset, excluded_bounds);
	}
};

class preview_control final : public std::enable_shared_from_this<preview_control>, public view_element
{
	view_state& _state;
	const texture_state_ptr _ts;
	const bool show_accelerator = false;
	mutable recti _view_bounds;

public:
	preview_control(view_state& s, texture_state_ptr ts, const bool show_accelerator,
	                const view_element_style style_in) noexcept : _state(s), _ts(std::move(ts)),
	                                                              show_accelerator(show_accelerator)
	{
		style |= style_in | view_element_style::has_tooltip | view_element_style::can_invoke;
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		return {mc.icon_cxy, mc.icon_cxy};
	}

	void render(ui::draw_context& dc, pointi element_offset) const override;
	void dispatch_event(const view_element_event& event) override;
	void tooltip(view_hover_element& hover, pointi loc, pointi element_offset) const override;

	view_controller_ptr controller_from_location(const view_host_ptr& host, const pointi loc,
	                                             const pointi element_offset,
	                                             const std::vector<recti>& excluded_bounds) override
	{
		return default_controller_from_location(*this, host, loc, element_offset, excluded_bounds);
	}
};


class items_dates_control final : public std::enable_shared_from_this<items_dates_control>, public view_element
{
private:
	view_state& _state;
	const df::item_element_ptr _item;
	std::u8string _text;
	ui::style::font_face _font = ui::style::font_face::dialog;
	ui::style::text_style _text_style = ui::style::text_style::multiline;

public:
	items_dates_control(view_state& s, df::item_element_ptr i) noexcept : view_element(
		                                                                   view_element_style::has_tooltip |
		                                                                   view_element_style::can_invoke), _state(s),
	                                                                   _item(std::move(i))
	{
		const auto& search = s.search();

		if (search.is_match(prop::created_utc, _item->file_created()))
		{
			style |= view_element_style::important;
			_text = platform::format_date(_item->file_created());
		}
		else if (search.is_match(prop::modified, _item->file_modified()))
		{
			style |= view_element_style::important;
			_text = platform::format_date(_item->file_modified());
		}
		else
		{
			const auto created_date = _item->media_created();

			if (search.is_match(prop::created_utc, created_date))
			{
				style |= view_element_style::important;
			}

			if (created_date.is_valid())
			{
				_text = platform::format_date(created_date);
			}
		}

		if (_text.empty())
		{
			_text = tt.invalid;
		}

		update_background_color();
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		const auto bg = calc_background_color(dc);
		const auto logical_bounds = bounds.offset(element_offset);
		dc.draw_text(_text, logical_bounds, _font, _text_style, ui::color(dc.colors.foreground, dc.colors.alpha), bg);
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		return mc.measure_text(_text, _font, _text_style, width_limit);
	}

	void tooltip(view_hover_element& hover, pointi loc, pointi element_offset) const override;

	void dispatch_event(const view_element_event& event) override
	{
		if (event.type == view_element_event_type::invoke)
		{
			const auto created_date = _item->media_created();

			if (created_date.is_valid())
			{
				const auto st = created_date.date();
				const auto search = df::search_t().day(st.day, st.month, st.year);
				_state.open(event.host, search, {});
			}
		}
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, const pointi loc,
	                                             const pointi element_offset,
	                                             const std::vector<recti>& excluded_bounds) override
	{
		return default_controller_from_location(*this, host, loc, element_offset, excluded_bounds);
	}
};

class pin_control final : public std::enable_shared_from_this<pin_control>, public view_element
{
	view_state& _state;
	const df::item_element_ptr _item;
	const bool show_accelerator = false;

public:
	pin_control(view_state& s, df::item_element_ptr i, const bool show_accelerator,
	            const view_element_style style_in) noexcept : _state(s), _item(std::move(i)),
	                                                          show_accelerator(show_accelerator)
	{
		style |= style_in | view_element_style::has_tooltip | view_element_style::can_invoke;
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		const auto i = _item;

		if (i)
		{
			const auto logical_bounds = bounds.offset(element_offset);
			const auto is_pin = _state._pin_item == _item;
			const auto icon = is_pin ? icon_index::pinned : icon_index::pin;
			const auto alpha = dc.colors.alpha;
			auto bg = calc_background_color(dc);

			if (is_pin)
			{
				bg.merge(ui::color(ui::style::color::important_background, dc.colors.alpha));
			}

			xdraw_icon(dc, icon, logical_bounds, ui::color(dc.colors.foreground, alpha), bg);
		}
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		return {mc.icon_cxy, mc.icon_cxy};
	}

	void dispatch_event(const view_element_event& event) override
	{
		if (event.type == view_element_event_type::invoke)
		{
			const auto i = _item;

			if (i)
			{
				if (_state._pin_item == i)
				{
					_state._pin_item.reset();
				}
				else
				{
					_state._pin_item = i;
				}

				_state.invalidate_view(view_invalid::view_redraw);
			}
		}
	}

	void tooltip(view_hover_element& hover, const pointi loc, const pointi element_offset) const override
	{
		hover.id = commands::pin_item;
		hover.active_bounds = hover.window_bounds = bounds.offset(element_offset);
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, const pointi loc,
	                                             const pointi element_offset,
	                                             const std::vector<recti>& excluded_bounds) override
	{
		return default_controller_from_location(*this, host, loc, element_offset, excluded_bounds);
	}
};

class unselect_element final : public std::enable_shared_from_this<unselect_element>, public view_element
{
private:
	view_state& _state;
	const df::item_element_ptr _item;
	const icon_index _icon = icon_index::close;

public:
	unselect_element(view_state& s, df::item_element_ptr i,
	                 const view_element_style style_in) noexcept : _state(s), _item(std::move(i))
	{
		style |= style_in | view_element_style::has_tooltip | view_element_style::can_invoke;
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		const auto bg = calc_background_color(dc);
		xdraw_icon(dc, _icon, bounds.offset(element_offset), ui::color(dc.colors.foreground, dc.colors.alpha), bg);
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		return {mc.icon_cxy, mc.icon_cxy};
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, const pointi loc,
	                                             const pointi element_offset,
	                                             const std::vector<recti>& excluded_bounds) override
	{
		return default_controller_from_location(*this, host, loc, element_offset, excluded_bounds);
	}

	void dispatch_event(const view_element_event& event) override
	{
		if (event.type == view_element_event_type::invoke)
		{
			_state.unselect(event.host, _item);
		}
	}

	void tooltip(view_hover_element& hover, const pointi loc, const pointi element_offset) const override
	{
		hover.elements->add(make_icon_element(_icon, view_element_style::no_break));
		hover.elements->add(std::make_shared<text_element>(format(tt.unselect_fmt, _item->name())));
		hover.active_bounds = hover.window_bounds = bounds.offset(element_offset);
	}
};

class delete_element final : public std::enable_shared_from_this<delete_element>, public view_element
{
private:
	view_state& _state;
	const df::item_element_ptr _item;
	const icon_index _icon = icon_index::del;

public:
	delete_element(view_state& s, df::item_element_ptr i,
	               const view_element_style style_in) noexcept : _state(s), _item(std::move(i))
	{
		style |= style_in | view_element_style::has_tooltip | view_element_style::can_invoke;
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		const auto bg = calc_background_color(dc);
		xdraw_icon(dc, _icon, bounds.offset(element_offset), ui::color(dc.colors.foreground, dc.colors.alpha), bg);
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		return {mc.icon_cxy, mc.icon_cxy};
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, const pointi loc,
	                                             const pointi element_offset,
	                                             const std::vector<recti>& excluded_bounds) override
	{
		return default_controller_from_location(*this, host, loc, element_offset, excluded_bounds);
	}

	void dispatch_event(const view_element_event& event) override
	{
		if (event.type == view_element_event_type::invoke)
		{
			df::item_set items;
			items.add(_item);
			_state._events.delete_items(items);
		}
	}

	void tooltip(view_hover_element& hover, const pointi loc, const pointi element_offset) const override
	{
		hover.elements->add(make_icon_element(_icon, view_element_style::no_break));
		hover.elements->add(std::make_shared<text_element>(format(tt.delete_fmt, _item->name())));
		hover.active_bounds = hover.window_bounds = bounds.offset(element_offset);
	}
};

class stream_element final : public std::enable_shared_from_this<stream_element>, public view_element
{
private:
	view_state& _state;
	const df::item_element_ptr _item;
	const av_stream_info _stream;
	ui::style::font_face _font = ui::style::font_face::dialog;
	ui::style::text_style _text_style = ui::style::text_style::multiline;
	std::u8string _text;

public:
	stream_element(view_state& state, df::item_element_ptr i, av_stream_info stream) noexcept : _state(state),
		_item(std::move(i)), _stream(std::move(stream))
	{
		if (stream.type == av_stream_type::audio)
		{
			style |= view_element_style::has_tooltip | view_element_style::can_invoke;
		}

		_text = _stream.title;
		if (_text.empty()) _text = str::format(tt.stream_name_fmt, _stream.index);
		set_style_bit(view_element_style::checked, _stream.is_playing);
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		const auto logical_bounds = bounds.offset(element_offset);
		const auto bg = calc_background_color(dc);
		dc.draw_text(_text, logical_bounds, _font, _text_style, ui::color(dc.colors.foreground, dc.colors.alpha), bg);
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		sizei result;

		if (!_text.empty())
		{
			result = mc.measure_text(_text, _font, _text_style, width_limit);
		}

		return result;
	}

	void dispatch_event(const view_element_event& event) override
	{
		if (event.type == view_element_event_type::invoke)
		{
			if (_stream.type == av_stream_type::audio)
			{
				_state.change_tracks(-1, _stream.index);
			}
			else if (_stream.type == av_stream_type::video)
			{
				_state.change_tracks(_stream.index, -1);
			}
		}
	}

	void tooltip(view_hover_element& hover, const pointi loc, const pointi element_offset) const override
	{
		hover.elements->add(make_icon_element(icon_index::audio, view_element_style::no_break));
		hover.elements->add(std::make_shared<text_element>(str::format(tt.stream_select_fmt, _text)));
		hover.active_bounds = hover.window_bounds = bounds.offset(element_offset);
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, const pointi loc,
	                                             const pointi element_offset,
	                                             const std::vector<recti>& excluded_bounds) override
	{
		return default_controller_from_location(*this, host, loc, element_offset, excluded_bounds);
	}
};

class summary_control final : public std::enable_shared_from_this<summary_control>, public view_element
{
	const df::file_group_histogram _summary;

	mutable int _line_height = 0;
	mutable int _col_1_width = 0;
	mutable int _col_2_width = 0;
	mutable int _col_3_width = 0;

	struct entry
	{
		icon_index icon;
		std::u8string name;
		std::u8string count;
		std::u8string size;
		file_group_ref ft;
	};

	std::vector<entry> _lines;

	ui::style::font_face _font = ui::style::font_face::dialog;

public:
	summary_control(const df::file_group_histogram& summary,
	                const view_element_style style_in) noexcept : view_element(style_in), _summary(summary)
	{
		populate_lines();
	}

	void populate_lines()
	{
		for (auto i = 0; i < file_group::max_count; ++i)
		{
			const auto& c = _summary.counts[i];

			if (c.count > 0)
			{
				const auto* const ft = file_group_from_index(i);
				const auto size = c.size.is_empty() ? std::u8string{} : prop::format_size(c.size);
				const auto text = ft->display_name(c.count > 1);
				const auto num = platform::format_number(str::to_string(c.count));

				_lines.emplace_back(ft->icon, text, num, size, ft);
			}
		}
	}

	void dispatch_event(const view_element_event& event) override
	{
		if (event.type == view_element_event_type::populate)
		{
			_lines.clear();
			populate_lines();
		}
	}

	const int icon_width = 24;
	const int col_padding = 4;

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		const auto logical_bounds = bounds.offset(element_offset);
		const auto cy = logical_bounds.height() - static_cast<int>(_line_height * _lines.size());
		auto y = logical_bounds.top + cy;

		const auto text_style = ui::style::text_style::single_line;
		const auto num_style = ui::style::text_style::single_line_far;
		const auto clr = ui::color(dc.colors.foreground, dc.colors.alpha);

		for (const auto& line : _lines)
		{
			const auto clr_text = ui::color(line.ft->text_color(dc.colors.foreground), dc.colors.alpha);

			auto x = logical_bounds.left;
			xdraw_icon(dc, line.icon, recti(x, y, x + icon_width, y + _line_height), clr, {});
			dc.draw_text(line.count, recti(x + icon_width, y, x + _col_1_width, y + _line_height), _font, num_style,
			             clr, {});
			x += _col_1_width + col_padding;
			dc.draw_text(line.name, recti(x, y, x + _col_2_width - col_padding, y + _line_height), _font, text_style,
			             clr_text.emphasize(), {});
			x += _col_2_width + col_padding;
			dc.draw_text(line.size, recti(x, y, x + _col_3_width - col_padding, y + _line_height), _font, num_style,
			             clr, {});
			y += _line_height;
		}
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		_line_height = mc.text_line_height(ui::style::font_face::dialog) + 2;

		_col_1_width = 0;
		_col_2_width = 0;
		_col_3_width = 0;

		for (const auto& line : _lines)
		{
			auto extent = mc.measure_text(line.count, ui::style::font_face::dialog, ui::style::text_style::single_line,
			                              100, _line_height);
			_col_1_width = std::max(extent.cx + icon_width, _col_1_width);

			extent = mc.measure_text(line.name, ui::style::font_face::dialog, ui::style::text_style::single_line, 100,
			                         _line_height);
			_col_2_width = std::max(extent.cx + col_padding, _col_2_width);

			extent = mc.measure_text(line.size, ui::style::font_face::dialog, ui::style::text_style::single_line, 100,
			                         _line_height);
			_col_3_width = std::max(extent.cx + col_padding, _col_3_width);
		}

		return {_col_1_width + _col_2_width + _col_3_width, static_cast<int>(_line_height * _lines.size())};
	}
};

class file_list_control final : public std::enable_shared_from_this<file_list_control>, public view_element
{
	static constexpr int icon_width = 24;
	static constexpr int col_padding = 8;
	static constexpr int col_count = 4;

	mutable int _line_height = 0;
	mutable int _col_widths[col_count] = {};
	ui::style::text_style _text_style[col_count];

	struct entry
	{
		icon_index icon;
		ui::color32 color;
		std::array<std::u8string, col_count> text;
		mutable std::array<sizei, col_count> extents;
	};

	display_state_ptr _display;
	std::vector<entry> _lines;

	ui::style::font_face _font = ui::style::font_face::dialog;

public:
	file_list_control(display_state_ptr display, const view_element_style style_in) noexcept : view_element(style_in),
		_display(std::move(display))
	{
		populate();
	}

	void populate()
	{
		_lines.clear();
		_lines.reserve(_display->_archive_items.size());

		for (const auto& i : _display->_archive_items)
		{
			const auto is_empty = i.uncompressed_size.is_empty();
			const auto* const ft = is_empty ? file_type::folder : files::file_type_from_name(i.filename);
			const auto color = ft->text_color(ui::style::color::view_text);
			const auto text = i.filename;
			const auto created = prop::format_date(i.created);
			const auto compressed_size = is_empty ? std::u8string{} : prop::format_size(i.compressed_size);
			const auto uncompressed_size = i.uncompressed_size.is_empty()
				                               ? std::u8string{}
				                               : prop::format_size(i.uncompressed_size);

			_lines.emplace_back(ft->icon, color,
			                    std::array<std::u8string, col_count>{text, created, uncompressed_size, compressed_size},
			                    std::array<sizei, col_count>{});
		}

		_text_style[0] = ui::style::text_style::single_line;
		_text_style[1] = ui::style::text_style::single_line;
		_text_style[2] = ui::style::text_style::single_line_far;
		_text_style[3] = ui::style::text_style::single_line_far;
	}

	void dispatch_event(const view_element_event& event) override
	{
		if (event.type == view_element_event_type::populate)
		{
			populate();
		}
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		const auto logical_bounds = bounds.offset(element_offset);
		const auto min_top = logical_bounds.top - _line_height;
		const auto cy = logical_bounds.height() - static_cast<int>(_line_height * _lines.size());
		auto y = logical_bounds.top + cy;
		const auto clr = ui::color(dc.colors.foreground, dc.colors.alpha);

		for (const auto& line : _lines)
		{
			auto x = logical_bounds.left;
			const auto clr_title = ui::color(line.color, dc.colors.alpha);

			xdraw_icon(dc, line.icon, recti(x, y, x + icon_width, y + _line_height), clr, {});
			x += icon_width;

			if (y > min_top && y < logical_bounds.bottom)
			{
				for (auto i = 0; i < col_count; i++)
				{
					const auto cc = i == 0 ? clr_title : clr;
					const auto width = _col_widths[i];
					auto text_bounds = recti(x, y, x + width - col_padding, y + _line_height);

					if (text_bounds.left < logical_bounds.right)
					{
						if (text_bounds.right > logical_bounds.right) text_bounds.right = logical_bounds.right;
						dc.draw_text(line.text[i], text_bounds, _font, _text_style[i], cc, {});
					}

					x += width;
				}
			}

			y += _line_height;
		}
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		_line_height = mc.text_line_height(ui::style::font_face::dialog) + 2;

		for (int& _col_width : _col_widths)
		{
			_col_width = 0;
		}

		for (const auto& line : _lines)
		{
			for (auto i = 0; i < col_count; i++)
			{
				if (line.extents[i].is_empty())
				{
					line.extents[i] = mc.measure_text(line.text[i], ui::style::font_face::dialog,
					                                  ui::style::text_style::single_line, 1000, _line_height);
				}

				const auto width = line.extents[i].cx + col_padding;
				if (width > _col_widths[i]) _col_widths[i] = width;
			}
		}

		auto total_col_width = icon_width;

		for (const int _col_width : _col_widths)
		{
			total_col_width += _col_width;
		}

		if (total_col_width > width_limit)
		{
			const auto diff = total_col_width - width_limit;

			if ((_col_widths[0] - diff) > (width_limit / col_count))
			{
				_col_widths[0] -= diff;
				total_col_width -= diff;
			}
		}

		return {std::min(width_limit, total_col_width), static_cast<int>(_line_height * _lines.size())};
	}
};


class hex_control final : public std::enable_shared_from_this<hex_control>, public view_element
{
	const int padding = 4;
	display_state_ptr _display;

	mutable int _x_data = 0;
	mutable int _x_text = 0;
	mutable int _bytes_per_line = 0;
	mutable int _chars_per_line = 0;
	mutable int _line_height = 0;
	mutable int _line_width = 0;

	ui::style::font_face _font = ui::style::font_face::code;

public:
	hex_control(display_state_ptr display, const view_element_style style_in) noexcept : view_element(style_in),
		_display(std::move(display))
	{
	}

	struct char_entry
	{
		char8_t c;
		ui::color32 clr;
	};

	static std::array<char_entry, 256> make_char_map()
	{
		std::array<char_entry, 256> result;

		const auto cl = ui::lighten(ui::style::color::view_selected_background, 0.22f);
		const auto ch = ui::lighten(ui::style::color::duplicate_background, 0.66f);

		for (auto i = 0; i < 256; i++)
		{
			auto c = static_cast<char8_t>(i);
			auto clr = ui::style::color::view_text;

			if (c == 32u || c == u8'\t')
			{
				c = u8' ';
				//clr = ui::style::color::view_text;
			}
			else if (c < 32)
			{
				c = u8'.';
				clr = cl;
			}
			else if (c >= 127)
			{
				c = u8'.';
				clr = ch;
			}

			//else if (std::ispunct(c)) {}
			//else if (std::isalnum(c)) {}
			//else { c = "."sv; };

			result[i] = {c, clr};
		}

		return result;
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		static const auto char_map = make_char_map();
		static constexpr char8_t hex_chars[16] = {
			'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
		};

		const auto line_count = calc_line_count();
		const auto clr = ui::color(dc.colors.foreground, dc.colors.alpha);

		if (line_count > 0)
		{
			const auto logical_bounds = bounds.offset(element_offset);
			const auto clip_bounds = dc.clip_bounds().intersection(logical_bounds);
			const auto first_line = (clip_bounds.top - logical_bounds.top) / _line_height;
			const auto last_line = first_line + ((clip_bounds.height() + _line_height) / _line_height);
			auto x_ascii = 0u;
			//const auto clr = ui::color(dc.colors.foreground, dc.colors.alpha);

			std::u8string line(_chars_per_line, ' ');
			std::vector<ui::text_highlight_t> highlights;

			const auto left = (logical_bounds.left + logical_bounds.right - _line_width) / 2;

			for (auto i = first_line; i < last_line; ++i)
			{
				const auto start_address = i * _bytes_per_line;

				if (start_address < _display->_selected_item_data.size())
				{
					line.assign(line.size(), ' ');

					const auto limit = std::min(_bytes_per_line,
					                            static_cast<int>(_display->_selected_item_data.size()) - start_address);
					const auto* const data = _display->_selected_item_data.data() + start_address;

					auto x = 0u;
					const auto address = static_cast<uint32_t>(start_address);

					highlights.emplace_back(x, 8, ui::color(dc.colors.foreground, dc.colors.alpha * 0.77f));

					line[x++] = hex_chars[(address >> 28) & 0xF];
					line[x++] = hex_chars[(address >> 24) & 0xF];
					line[x++] = hex_chars[(address >> 20) & 0xF];
					line[x++] = hex_chars[(address >> 16) & 0xF];
					line[x++] = hex_chars[(address >> 12) & 0xF];
					line[x++] = hex_chars[(address >> 8) & 0xF];
					line[x++] = hex_chars[(address >> 4) & 0xF];
					line[x++] = hex_chars[(address >> 0) & 0xF];

					x += 2;

					for (auto j = 0; j < limit; ++j)
					{
						const auto byte = data[j];
						const auto cc = char_map[byte & 0xff].clr;

						if (cc != dc.colors.foreground)
						{
							highlights.emplace_back(x, 2, ui::color(cc, dc.colors.alpha));
						}

						line[x] = hex_chars[(byte & 0xF0) >> 4];
						++x;
						line[x] = hex_chars[(byte & 0x0F) >> 0];
						x += ((j % 8) == 7) ? 3 : 2;
					}

					// always start ascii part on same column
					if (x > x_ascii) x_ascii = x;
					x = x_ascii;

					for (auto j = 0; j < limit; ++j)
					{
						const auto& ce = char_map[data[j] & 0xff];
						const auto cc = ce.clr;

						if (cc != dc.colors.foreground)
						{
							highlights.emplace_back(x, 1, ui::color(cc, dc.colors.alpha));
						}

						line[x] = ce.c;
						x += ((j % 8) == 7) ? 2 : 1;
					}

					const auto y = logical_bounds.top + (i * _line_height);
					dc.draw_text(line, highlights, recti(left, y, logical_bounds.right, y + _line_height), _font,
					             ui::style::text_style::single_line, clr, {});
					highlights.clear();
				}
			}
		}
	}

	static uint32_t calc_chars_per_line(int bytes_per_line)
	{
		return (static_cast<int>(sizeof(uint32_t)) * 2) + 4 + (bytes_per_line * 4) + ((bytes_per_line / 8) * 2);
	}

	int calc_line_count() const
	{
		return _bytes_per_line > 0 ? df::round_up(static_cast<int>(_display->_selected_item_data.size()), _bytes_per_line) : 0;
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		_line_width = 0;
		_bytes_per_line = 0;
		_chars_per_line = 0;

		while (true)
		{
			const auto line = std::u8string(calc_chars_per_line(_bytes_per_line + 8), u8'0');
			const auto extent = mc.measure_text(line, _font, ui::style::text_style::single_line, width_limit + 200);

			if (extent.cx > width_limit)
			{
				break;
			}

			_bytes_per_line += 8;
			_chars_per_line = calc_chars_per_line(_bytes_per_line);
			_line_width = extent.cx;
			_line_height = extent.cy + padding;
		}

		const auto line_count = calc_line_count();
		return {width_limit, _line_height * line_count};
	}
};


class comodore_disk_control final : public std::enable_shared_from_this<comodore_disk_control>, public view_element
{
	display_state_ptr _display;

	mutable int _line_height = 0;
	mutable int _line_width = 0;

	ui::style::font_face _font = ui::style::font_face::petscii;

	std::vector<files::d64_item> _lines;

	uint32_t c64_blue = ui::rgb(33, 27, 174);
	uint32_t c64_light_blue = ui::rgb(95, 83, 254);

public:
	comodore_disk_control(display_state_ptr display, const view_element_style style_in) noexcept : view_element(style_in),
		_display(std::move(display))
	{
		_lines = files::list_disk(_display->_selected_item_data);
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		if (!_lines.empty())
		{

			const auto clr = ui::color(c64_light_blue, dc.colors.alpha);
			const auto bg_clr = ui::color(c64_blue, dc.colors.alpha);
			const auto borger_clr = ui::color(c64_light_blue, dc.colors.alpha);
			const auto logical_bounds = bounds.offset(element_offset);
			const auto left = std::max(logical_bounds.left + dc.padding2, (logical_bounds.left + logical_bounds.right - _line_width) / 2);
			const auto right = std::min(logical_bounds.right - dc.padding2, left + _line_width + dc.padding2);
			auto y = logical_bounds.top + dc.padding2 * 2;
			const auto bg_bounds = recti(left - dc.padding2, y - dc.padding2, right, logical_bounds.bottom - dc.padding2);

			dc.draw_rect(bg_bounds, bg_clr);
			dc.draw_border(bg_bounds, bg_bounds.inflate(dc.padding2), borger_clr, borger_clr);

			for (const auto& line : _lines)
			{
				dc.draw_text(line.line, recti(left, y, logical_bounds.right, y + _line_height), _font,
				             ui::style::text_style::single_line, clr, {});
				y += _line_height;
			}
		}
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		_line_width = 0;
		_line_height = 0;

		for (const auto& line : _lines)
		{
			const auto extent = mc.measure_text(line.line, _font, ui::style::text_style::single_line, width_limit);
			_line_width = std::max(_line_width, extent.cx);
			_line_height = std::max(_line_height, extent.cy + mc.padding1);
		}

		return { width_limit, static_cast<int>(_line_height * _lines.size() + (mc.padding2 * 4)) };
	}
};

class play_control final : public std::enable_shared_from_this<play_control>, public view_element
{
public:
	view_state& _state;
	display_state_ptr _display;

	play_control(view_state& s, const view_element_style style_in) noexcept : _state(s), _display(s.display_state())
	{
		style |= style_in | view_element_style::has_tooltip | view_element_style::can_invoke;
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		const auto logical_bounds = bounds.offset(element_offset);
		const auto alpha = dc.colors.alpha;
		const auto is_slide_show = _display->is_playing_slideshow();

		render_background(dc, element_offset);

		if (is_slide_show)
		{
			auto progress_bounds = logical_bounds;
			progress_bounds.top = progress_bounds.bottom - 16;

			dc.draw_rect(progress_bounds, ui::color(dc.colors.background, alpha * 0.7f));

			auto r = progress_bounds.inflate(-2);
			r.right = r.left + df::mul_div(std::min(_display->slideshow_pos(), 1000), r.width(), 1000);
			dc.draw_rect(r, ui::color(ui::style::color::dialog_selected_background, alpha * 0.7f));
		}

		const auto icon = _display->is_playing() ? icon_index::pause : icon_index::play;
		const auto clr = ui::color(dc.colors.foreground, dc.colors.alpha);

		xdraw_icon(dc, icon, logical_bounds, clr, {});
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		return {32, 32};
	}

	void dispatch_event(const view_element_event& event) override
	{
		if (event.type == view_element_event_type::invoke)
		{
			// commands::ID_PLAY
			_state.play(event.host);
		}
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, const pointi loc,
	                                             const pointi element_offset,
	                                             const std::vector<recti>& excluded_bounds) override
	{
		return default_controller_from_location(*this, host, loc, element_offset, excluded_bounds);
	}

	void tooltip(view_hover_element& hover, const pointi loc, const pointi element_offset) const override
	{
		hover.id = commands::play;
		hover.active_bounds = hover.window_bounds = bounds.offset(element_offset);
	}
};

class scrubber_element final : public std::enable_shared_from_this<scrubber_element>, public view_element
{
	display_state_ptr _display;
	std::shared_ptr<av_player> _player;

public:
	scrubber_element(std::shared_ptr<av_player> player, display_state_ptr display) noexcept :
		view_element(view_element_style::has_tooltip | view_element_style::can_invoke), _display(std::move(display)),
		_player(std::move(player))
	{
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		const auto logical_bounds = bounds.offset(element_offset);
		render_background(dc, element_offset);

		if (logical_bounds.width() > _display->_time_width * 3)
		{			
			const auto is_tracking = is_style_bit_set(view_element_style::tracking);
			const auto is_hover = is_style_bit_set(view_element_style::hover);
			const auto scrub_bg_clr = ui::color(0, dc.colors.alpha / 3.33f);
			const auto scale1 = df::round(1 * dc.scale_factor);

			auto scrub_bounds = _display->_scrubber_bounds = logical_bounds.inflate(-_display->_time_width, 0);

			if (!is_hover && !is_tracking)
			{
				scrub_bounds.top += dc.padding1;
				scrub_bounds.bottom -= dc.padding1;
			}
						
			dc.draw_rounded_rect(scrub_bounds, scrub_bg_clr, dc.padding1);

			_display->_loading_alpha_animation.target(_display->_session ? 0.0f : 1.0f);

			const auto max_scrubber_width = scrub_bounds.width() - (scale1 * 2);
			const auto media_pos = _display->media_pos() - _display->media_start();
			const auto media_len = std::max(1.0, _display->media_end() - _display->media_start());
			const auto pos = df::round(media_pos * max_scrubber_width / media_len);

			scrub_bounds.left += scale1;
			scrub_bounds.right = scrub_bounds.left + std::clamp(pos, scale1 * 2, max_scrubber_width);
			scrub_bounds.top += scale1;
			scrub_bounds.bottom -= scale1;

			dc.draw_rounded_rect(scrub_bounds, view_handle_color(false, is_hover, is_tracking, dc.frame_has_focus, true).aa(dc.colors.alpha), dc.padding1);

			auto time_bounds = logical_bounds;
			time_bounds.right = _display->_scrubber_bounds.left;

			auto duration_bounds = logical_bounds;
			duration_bounds.left = _display->_scrubber_bounds.right;

			const auto clr = ui::color(dc.colors.foreground, dc.colors.alpha);

			dc.draw_text(_display->_time, time_bounds, ui::style::font_face::dialog,
			             ui::style::text_style::single_line_center, clr, {});
			dc.draw_text(_display->_duration, duration_bounds, ui::style::font_face::dialog,
			             ui::style::text_style::single_line_center, clr, {});
		}
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		const auto extent = mc.measure_text(u8"00:00:00"sv, ui::style::font_face::dialog, ui::style::text_style::single_line, 200);
		_display->_time_width = extent.cx + mc.padding2;
		return {width_limit, std::max(extent.cy, mc.scroll_width)};
	}

	void hover(interaction_context& ic) override
	{
		const auto scrubber_width = _display->_scrubber_bounds.width();
		const auto media_start = _display->media_start();
		const auto media_end = _display->media_end();
		const auto media_len = media_end - media_start;
		const auto scrub_left = _display->_scrubber_bounds.left;
		const auto scrub_right = _display->_scrubber_bounds.right;
		const auto scrubber_pos = std::clamp(ic.loc.x, scrub_left, scrub_right) - scrub_left;

		if (_display->_hover_scrubber_pos != scrubber_pos)
		{
			_display->_hover_scrubber_pos = scrubber_pos;
			_display->load_seek_preview(scrubber_pos, scrubber_width, [d = _display] { d->preview_loaded(); });
		}

		const auto is_tracking = is_style_bit_set(view_element_style::tracking);

		if (is_tracking || ic.tracking || is_tracking != ic.tracking)
		{
			const auto time_pos = media_start + floor((scrubber_pos * media_len) / std::max(1, scrubber_width));
			set_style_bit(view_element_style::tracking, ic.tracking);

			if (_display->_session)
			{
				_player->seek(_display->_session, time_pos, ic.tracking);
			}
		}
	}

	void dispatch_event(const view_element_event& event) override
	{
	}

	void tooltip(view_hover_element& hover, const pointi loc, const pointi element_offset) const override
	{
		const auto& surface = _display->_hover_surface;

		if (is_valid(surface))
		{
			const auto x = std::clamp(loc.x, _display->_scrubber_bounds.left, _display->_scrubber_bounds.right);

			hover.elements->add(std::make_shared<surface_element>(surface, 200, view_element_style::none));
			hover.elements->add(std::make_shared<text_element>(str::format_seconds(df::round(surface->time())),
			                                                  ui::style::font_face::dialog,
			                                                  ui::style::text_style::single_line,
			                                                  view_element_style::center |
			                                                  view_element_style::new_line));

			hover.window_bounds = _display->_scrubber_bounds;
			hover.active_bounds = recti(x, _display->_scrubber_bounds.top, x + 1, _display->_scrubber_bounds.bottom);
			hover.x_focus = _display->_hover_scrubber_pos;
		}
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, const pointi loc,
	                                             const pointi element_offset,
	                                             const std::vector<recti>& excluded_bounds) override
	{
		return default_controller_from_location(*this, host, loc, element_offset, excluded_bounds);
	}
};

class group_title_control : public view_element
{
public:
	struct title_element
	{
		view_element_ptr v;
		mutable sizei extent;
		mutable bool visible = true;

		title_element() = default;

		title_element(view_element_ptr vv) : v(std::move(vv))
		{
		}
	};

	std::vector<title_element> elements;

	group_title_control() noexcept = default;

	group_title_control(const std::u8string_view title,
	                    const std::vector<view_element_ptr>& other_controls = {}) noexcept
	{
		elements.emplace_back(std::make_shared<text_element>(title, ui::style::font_face::title,
		                                                     ui::style::text_style::multiline,
		                                                     view_element_style::grow));
		for (const auto& e : other_controls) elements.emplace_back(e);
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		const auto bg_color = calc_background_color(dc);

		if (bg_color.a > 0.0f)
		{
			const auto pad = padding * dc.scale_factor;
			dc.draw_rounded_rect(bounds.offset(element_offset).inflate(pad), bg_color, dc.padding1);
		}

		for (const auto& e : elements)
		{
			if (e.visible)
			{
				e.v->render(dc, element_offset);
			}
		}
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		auto cx = 0;
		auto cy = 0;
		auto grow_count = 0;

		for (const auto& e : elements)
		{
			if (cx != 0) cx += mc.padding1;
			e.extent = e.v->measure(mc, width_limit);
			cx += e.extent.cx;
			if (e.extent.cy > cy) cy = e.extent.cy;

			if (e.v->is_style_bit_set(view_element_style::grow))
			{
				++grow_count;
			}
		}

		const auto overflow = width_limit - cx;

		for (const auto& element : elements)
		{
			const auto& e = element;

			if (e.v->is_style_bit_set(view_element_style::grow))
			{
				const auto xx = std::max(element.extent.cx + (overflow / grow_count), 64);
				e.extent.cx = xx;
				e.extent.cy = e.v->measure(mc, xx).cy;
			}
		}

		return {width_limit, cy};
	}

	void layout(ui::measure_context& mc, const recti bounds_in, ui::control_layouts& positions) override
	{
		bounds = bounds_in;
		//recti bounds_play{ bounds.left, bounds.top, bounds.left + elements[0].extent.cx, bounds.top + top_row_height, };
		//elements[0].v->layout(mc, bounds_play, positions);
		//const auto title_left = bounds_play.right + mc.baseline_snap;

		auto x = bounds.left;
		const auto y = bounds.top;

		for (const auto& element : elements)
		{
			const auto cx = element.extent.cx;
			if ((x + cx) <= bounds.right)
			{
				const recti bb{x, y, x + cx, bounds.bottom};
				element.v->layout(mc, bb, positions);
				x += cx + mc.padding1;
				element.visible = true;
			}
			else
			{
				element.visible = false;
			}
		}

		/*if (title_below)
		{
			const auto avail_inline = x - title_left;
			const auto extent = elements[1].v->measure(mc, avail_inline);
			recti bounds_title{ bounds.left, bounds.top + top_row_height, bounds.left + elements[1].extent.cx, bounds.top + top_row_height + elements[1].extent.cy };

			if (extent.cy <= bounds.height())
			{
				bounds_title.left = title_left;
				bounds_title.right = x;
				bounds_title.top = bounds.top;
				bounds_title.bottom = bounds.top + extent.cy;
			}

			elements[1].v->layout(mc, bounds_title, positions);
		}
		else
		{
			const recti bounds_title{ title_left, bounds.top, title_left + elements[1].extent.cx, bounds.top + top_row_height };
			elements[1].v->layout(mc, bounds_title, positions);
		}*/
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, const pointi loc,
	                                             const pointi element_offset,
	                                             const std::vector<recti>& excluded_bounds) override
	{
		view_controller_ptr result;
		for (const auto& e : elements)
		{
			if (e.visible)
			{
				result = e.v->controller_from_location(host, loc, element_offset, {});
				if (result) break;
			}
		}
		return result;
	}

	void dispatch_event(const view_element_event& event) override
	{
		for (const auto& i : elements)
		{
			i.v->dispatch_event(event);
		}
	}
};

class photo_control final : public view_element, public std::enable_shared_from_this<photo_control>
{
public:
	view_state& _state;
	display_state_ptr _display;
	view_host_ptr _host;

	bool _zoom_active = false;
	bool _can_pan = false;
	mutable int _scale_percent = 0;
	recti _zoom_text_bounds;
	recti _view_bounds;
	bool _fill = false;

	void show_zoom_box(bool zoom)
	{
		_zoom_active = zoom;
	}

	photo_control(view_state& state, display_state_ptr display, view_host_ptr host, bool fill) noexcept :
		view_element(view_element_style::can_invoke | view_element_style::shrink), _state(state),
		_display(std::move(display)), _host(std::move(host)), _fill(fill)
	{
	}

	const pointi image_offset() const
	{
		return _display->_image_offset;
	}

	void scroll_image_to(pointi loc)
	{
		if (_display->_selected_texture1)
		{
			const auto st = _display->_selected_texture1;
			const auto display_dims = st->calc_display_dimensions();
			const auto client_extent = bounds.extent();
			const auto xx = std::max(display_dims.cx - client_extent.cx, 0) / 2;
			const auto yy = std::max(display_dims.cy - client_extent.cy, 0) / 2;

			const recti limit(-xx, -yy, xx, yy);
			const auto pt = loc.clamp(limit);

			if (_display->_image_offset != pt)
			{
				_display->_image_offset = pt;
				_state.invalidate_view(view_invalid::view_redraw);
			}
		}
	}

	pointi calc_pan_offset(const texture_state_ptr& tex) const
	{
		const auto tex_bounds = tex->display_bounds();
		const auto display_dims = tex_bounds.extent();
		const auto xx = std::max(display_dims.cx - bounds.width(), 0) / 2;
		const auto yy = std::max(display_dims.cy - bounds.height(), 0) / 2;

		const recti limit(-xx, -yy, xx, yy);
		return _display->_image_offset.clamp(limit);
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		if (_display->_selected_texture1)
		{
			const auto st = _display->_selected_texture1;

			_display->media_offset = element_offset;
			_display->update_for_present(dc.time_now);

			if (_can_pan && _display->zoom())
			{
				const auto pan_offset = calc_pan_offset(st);
				st->draw(dc, element_offset + pan_offset, 0, true);
			}
			else
			{
				st->draw(dc, element_offset, 0, true);
			}

			render_text(dc, element_offset);
		}
	}

	sizei calc_tex_extent(int width_limit, int height_limit) const
	{
		if (_display->_selected_texture1)
		{
			const auto st = _display->_selected_texture1;
			auto image_scale = 0.0;
			const auto dimensions = sized(st->calc_display_dimensions());

			if (_display->zoom())
			{
				image_scale = 1.0;
			}
			else
			{
				image_scale = calc_thumb_scale(dimensions, sized(width_limit, height_limit), false);
				if (!setting.scale_up && image_scale > 1.0) image_scale = 1.0;
			}

			const auto cx = std::max(1.0, dimensions.Width * image_scale);
			const auto cy = std::max(1.0, dimensions.Height * image_scale);
			const auto result = sizei(df::round(cx), df::round(cy));
			_scale_percent = df::round(image_scale * 100);

			return result;
		}

		return {};
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		const auto tex_extent = calc_tex_extent(width_limit, width_limit * 3);
		const auto text_line_height = _fill ? 0 : mc.text_line_height(ui::style::font_face::dialog) + mc.padding2;
		return {width_limit, tex_extent.cy + text_line_height};
	}

	void layout(ui::measure_context& mc)
	{
		if (_display->_selected_texture1)
		{
			const auto st = _display->_selected_texture1;
			const auto i = _display->_item1;

			auto text_line_height = _fill ? 0 : mc.text_line_height(ui::style::font_face::dialog) + mc.padding2;
			auto layout_extent = calc_tex_extent(bounds.width(), bounds.height() - text_line_height);

			if (text_line_height > 0 && (bounds.width() - layout_extent.cx) > 100)
			{
				layout_extent = calc_tex_extent(bounds.width(), bounds.height());
				text_line_height = 0;
			}

			const auto center = bounds.center();
			const auto x = center.x - (layout_extent.cx / 2);
			const auto y = (center.y + (text_line_height / 2)) - (layout_extent.cy / 2);

			st->layout(mc, recti(x, y, x + layout_extent.cx, y + layout_extent.cy), i);

			bool can_pan = false;

			if (i && i->file_type()->has_trait(file_traits::zoom))
			{
				const auto dim_src = st->calc_display_dimensions();
				can_pan = bounds.width() < dim_src.cx || bounds.height() < dim_src.cy;
			}

			_can_pan = can_pan;
			_zoom_text_bounds = {bounds.top_left(), measure_zoom(mc)};
		}
	}

	void layout(ui::measure_context& mc, const recti bounds_in, ui::control_layouts&) override
	{
		bounds = bounds_in;
		layout(mc);
	}

	void dispatch_event(const view_element_event& event) override
	{
	}

	sizei measure_zoom(ui::measure_context& mc) const
	{
		const auto text_extent = mc.measure_text(u8"000%"sv, ui::style::font_face::dialog,
		                                         ui::style::text_style::single_line, 100);
		const auto pad = padding * mc.scale_factor;
		return {text_extent.cx + (pad.cx * 3) + mc.icon_cxy, text_extent.cy + (pad.cy * 2)};
	}

	void render_text(ui::draw_context& dc, const pointi element_offset) const
	{
		if (!_zoom_active)
		{
			const auto alpha = dc.colors.overlay_alpha;
			const auto clr = ui::color(dc.colors.foreground, alpha);
			const auto pad = padding * dc.scale_factor;

			if (_scale_percent > 0)
			{
				const auto text = str::format(u8"{}%"sv, _scale_percent);
				const auto logical_bounds = _zoom_text_bounds.offset(element_offset);
				const auto inner_bounds = logical_bounds.inflate(-pad.cx, -pad.cy);
				const recti icon_bounds(inner_bounds.left, inner_bounds.top, inner_bounds.left + dc.icon_cxy,
				                        inner_bounds.bottom);
				const recti text_bounds(inner_bounds.left + dc.icon_cxy + pad.cx, inner_bounds.top,
				                        inner_bounds.right, inner_bounds.bottom);

				dc.draw_rect(logical_bounds, ui::color(ui::style::color::group_background, alpha * 0.5f));
				xdraw_icon(dc, icon_index::zoom_in, icon_bounds, clr, {});
				dc.draw_text(text.c_str(), text_bounds, ui::style::font_face::dialog,
				             ui::style::text_style::single_line, clr, {});
			}

			if (_display->_item_pos > 0)
			{
				const auto pos_text = str::format(
					_display->_break_count == _display->_total_count ? u8"{}|{}"sv : u8"{}|{}|{}"sv, _display->_item_pos,
					_display->_break_count, _display->_total_count);
				const auto pos_text_extent = dc.measure_text(pos_text, ui::style::font_face::dialog,
				                                             ui::style::text_style::single_line, 100);
				const auto pos_text_bounds = recti{
					bounds.right - pos_text_extent.cx - pad.cx - pad.cx, bounds.top, bounds.right,
					bounds.top + pos_text_extent.cy + pad.cy + pad.cy
				};
				const auto logical_bounds = pos_text_bounds.offset(element_offset);

				dc.draw_rect(logical_bounds, ui::color(ui::style::color::group_background, alpha * 0.5f));
				dc.draw_text(pos_text.c_str(), logical_bounds.inflate(-pad.cx, 0), ui::style::font_face::dialog,
				             ui::style::text_style::single_line_far, clr, {});
			}
		}
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, pointi loc, pointi element_offset,
	                                             const std::vector<recti>& excluded_bounds) override;

	friend class zoom_controller;
	friend class pan_controller;
};

class video_control final : public view_element, public std::enable_shared_from_this<video_control>
{
public:
	view_state& _state;
	display_state_ptr _display;
	view_host_ptr _host;

	video_control(view_state& s, display_state_ptr display, view_host_ptr host) noexcept :
		view_element(view_element_style::can_invoke | view_element_style::shrink), _state(s),
		_display(std::move(display)), _host(std::move(host))
	{
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		try
		{
			if (_display->_selected_texture1)
			{
				const auto st = _display->_selected_texture1;

				_display->media_offset = element_offset;
				_display->update_for_present(dc.time_now);

				if (_display->player_has_video() && !st->_vid_tex)
				{
					st->_vid_tex = dc.create_texture();
				}

				st->draw(dc, element_offset, 0, false);
			}
		}
		catch (std::exception& e)
		{
			df::trace(e.what());
		}
	}

	sizei calc_tex_extent(int width_limit, int height_limit) const
	{
		sizei result;

		if (_display->_selected_texture1)
		{
			const auto st = _display->_selected_texture1;
			const auto ww = width_limit;
			const auto dimensions = sized(st->calc_display_dimensions());
			auto image_scale = calc_thumb_scale(dimensions, sized(ww, height_limit), false);
			if (!setting.scale_up && image_scale > 1.0) image_scale = 1.0;

			const auto cx = std::max(1.0, dimensions.Width * image_scale);
			const auto cy = std::max(1.0, dimensions.Height * image_scale);
			result = sizei(df::round(cx), df::round(cy));
		}

		return result;
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		const auto tex_extent = calc_tex_extent(width_limit, width_limit * 3);
		return {width_limit, tex_extent.cy};
	}

	void layout(ui::measure_context& mc, const recti bounds_in, ui::control_layouts&) override
	{
		bounds = bounds_in;

		if (_display->_selected_texture1)
		{
			const auto st = _display->_selected_texture1;
			const auto i = _display->_item1;

			const auto layout_extent = calc_tex_extent(bounds.width(), bounds.height());
			st->layout(mc, center_rect(layout_extent, bounds), i);
		}
	}

	void dispatch_event(const view_element_event& event) override
	{
		if (event.type == view_element_event_type::invoke)
		{
			_state.play(event.host);
		}
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, pointi loc, pointi element_offset,
	                                             const std::vector<recti>& excluded_bounds) override;

	friend class zoom_controller;
	friend class pan_controller;
};


class audio_control final : public view_element, public std::enable_shared_from_this<audio_control>
{
public:
	view_state& _state;
	view_host_ptr _host;
	display_state_ptr _display;

	audio_control(view_state& s, display_state_ptr display, view_host_ptr host) noexcept :
		view_element(view_element_style::can_invoke | view_element_style::shrink), _state(s), _host(std::move(host)),
		_display(std::move(display))
	{
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		return {width_limit, std::max(width_limit, 500)};
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		_display->_audio_element_offset = element_offset;
		_display->_audio_element_alpha = dc.colors.alpha;

		if (_display->_selected_texture1)
		{
			const auto st = _display->_selected_texture1;

			_display->media_offset = element_offset;
			_display->update_for_present(dc.time_now);

			if (_display->player_has_video() && !st->_vid_tex)
			{
				st->_vid_tex = dc.create_texture();
			}

			if (_display->player_has_video() && st->_vid_tex)
			{
				st->draw(dc, element_offset, 0, false);
			}
		}

		if (!_display->_audio_verts)
		{
			_display->_audio_verts = dc.create_vertices();
		}

		if (_display->_audio_verts)
		{
			_display->_audio_element_bounds = bounds;
			_display->update_for_present(dc.time_now);
			dc.draw_vertices(_display->_audio_verts);
		}
	}

	void layout(ui::measure_context& mc, const recti bounds_in, ui::control_layouts&) override
	{
		bounds = bounds_in;

		if (bounds.width() > 100)
		{
			if (_display->_selected_texture1)
			{
				const auto st = _display->_selected_texture1;
				const auto i = _display->_item1;

				auto limit_bounds = bounds;
				limit_bounds.bottom = bounds.top + df::mul_div(bounds.height(), 3, 5);

				const auto tex_bounds = ui::scale_dimensions(st->calc_display_dimensions(), limit_bounds);
				st->layout(mc, center_rect(tex_bounds, limit_bounds), i);
			}
		}
	}

	void dispatch_event(const view_element_event& event) override
	{
		if (event.type == view_element_event_type::invoke)
		{
			_state.play(event.host);
		}
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, pointi loc, pointi element_offset,
	                                             const std::vector<recti>& excluded_bounds) override;

	friend class zoom_controller;
	friend class pan_controller;
};

class side_by_side_control final : public view_element, public std::enable_shared_from_this<side_by_side_control>
{
public:
	display_state_ptr _display;

	side_by_side_control(display_state_ptr display) noexcept : view_element(
		                                                           view_element_style::can_invoke |
		                                                           view_element_style::shrink),
	                                                           _display(std::move(display))
	{
	}

	const pointi image_offset() const
	{
		return _display->_image_offset;
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		const auto compare_pos = calc_compare_pos();

		if (_display->is_two())
		{
			_display->media_offset = element_offset;
			_display->update_for_present(dc.time_now);

			_display->_selected_texture1->draw(dc, element_offset, compare_pos, true);
			_display->_selected_texture2->draw(dc, element_offset, compare_pos, false);

			if (!_display->_comparing)
			{
				auto rr = _display->_compare_bounds.offset(element_offset);
				rr.left = (rr.left + rr.right) / 2;
				rr.right = rr.left + 1;
				dc.draw_rect(rr, ui::color(ui::average(dc.colors.background, 0), dc.colors.alpha));
			}
		}
	}

	std::vector<sizei> calc_normal_extents(int width_limit, int height_limit) const
	{
		std::vector<sizei> result;
		if (_display->is_two())
		{
			const auto ww = _display->_comparing ? width_limit : ((width_limit - 10) / 2);
			const auto selected_textures = {_display->_selected_texture1, _display->_selected_texture2};

			for (const auto& st : selected_textures)
			{
				const auto dimensions = sized(st->calc_display_dimensions());
				auto image_scale = calc_thumb_scale(dimensions, sized(ww, height_limit), false);
				if (!setting.scale_up && image_scale > 1.0) image_scale = 1.0;
				const auto cx = std::max(1.0, dimensions.Width * image_scale);
				const auto cy = std::max(1.0, dimensions.Height * image_scale);
				result.emplace_back(df::round(cx), df::round(cy));
			}
		}

		return result;
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		if (_display->is_two())
		{
			const auto layout_bounds = calc_normal_extents(width_limit, width_limit);

			auto cy = 0;

			for (const auto& extent : layout_bounds)
			{
				if (cy < extent.cy) cy = extent.cy;
			}

			return {width_limit, cy};
		}

		return {};
	}

	void layout(ui::measure_context& mc)
	{
		if (_display->is_two())
		{
			const auto tex_count = 2;
			const auto layout_extent = calc_normal_extents(bounds.width(), bounds.height());
			df::assert_true(layout_extent.size() == tex_count);

			const std::array<texture_state_ptr, 2> selected_textures{
				_display->_selected_texture1, _display->_selected_texture2
			};
			const std::array<df::item_element_ptr, 2> selected_items{_display->_item1, _display->_item2};

			for (auto i = 0u; i < tex_count; i++)
			{
				auto tex_bounds = bounds;

				if (!_display->_comparing)
				{
					tex_bounds.left = bounds.left + df::mul_div(i, bounds.width(), tex_count);
					tex_bounds.right = bounds.left + df::mul_div(i + 1, bounds.width(), tex_count);

					if (i == 0) tex_bounds = tex_bounds.offset(-1, 0);
					if (i == 1) tex_bounds = tex_bounds.offset(1, 0);
				}

				selected_textures[i]->layout(mc, center_rect(layout_extent[i], tex_bounds), selected_items[i]);
			}

			_display->_compare_bounds = center_rect(sizei{mc.scroll_width, df::mul_div(bounds.height(), 5, 7)},
			                                        bounds);

			//if (_is_comparing)
			{
				_display->_compare_limits = bounds;

				for (const auto& tex : selected_textures)
				{
					const auto bounds = tex->display_bounds();
					if (_display->_compare_limits.left < bounds.left) _display->_compare_limits.left = bounds.left;
					if (_display->_compare_limits.right > bounds.right) _display->_compare_limits.right = bounds.right;
				}
			}
		}
	}

	void layout(ui::measure_context& mc, const recti bounds_in, ui::control_layouts&) override
	{
		bounds = bounds_in;
		layout(mc);
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, pointi loc, pointi element_offset,
	                                             const std::vector<recti>& excluded_bounds) override;

	void compare(const int x, bool tracking)
	{
		const auto compare = tracking;

		if (_display->_comparing != compare)
		{
			_display->_comparing = compare;
			_display->stop_slideshow();
			_display->_async.invalidate_view(view_invalid::view_layout | view_invalid::tooltip);
		}

		if (_display->_compare_hover_loc != x)
		{
			_display->_compare_hover_loc = x;
			_display->_async.invalidate_view(view_invalid::view_redraw);
		}
	}

	int calc_compare_pos() const
	{
		if (!_display || !_display->_comparing) return 0;
		if (_display->_compare_limits.is_empty())
			return (_display->_compare_limits.left + _display->_compare_limits.
			                                                   right) / 2;
		return std::clamp(_display->_compare_hover_loc, _display->_compare_limits.left + 4,
		                  _display->_compare_limits.right - 4);
	}

	friend class compare_controller;
};

class images_control2 final : public view_element, public std::enable_shared_from_this<images_control2>
{
	view_state& _state;
	display_state_ptr _display;

public:
	images_control2(view_state& state, display_state_ptr display) noexcept : _state(state), _display(std::move(display))
	{
	}

	sizei measure(ui::measure_context& mc, const int cx) const override
	{
		const auto image_count = _display->_images.size();
		const auto cx_control = std::min(360, df::mul_div(cx, 5, 11));

		if (image_count > 0)
		{
			return {cx, std::max(180, cx_control)};
		}

		return {cx, 0};
	}

	std::vector<sizei> surface_dims() const
	{
		std::vector<sizei> results;
		results.reserve(_display->_surfaces.size());

		for (const auto& s : _display->_surfaces)
		{
			results.emplace_back(s->dimensions());
		}

		return results;
	}

	void layout(ui::measure_context& mc, const recti bounds_in, ui::control_layouts& positions) override
	{
		bounds = bounds_in;
		_display->_surface_bounds = ui::layout_images(bounds_in, surface_dims());
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		if (_display->_textures.empty())
		{
			for (const auto& s : _display->_surfaces)
			{
				auto t = dc.create_texture();

				if (t && t->update(s) != ui::texture_update_result::failed)
				{
					_display->_textures.emplace_back(std::move(t));
				}
			}
		}

		for (auto i = 0u; i < std::min(_display->_textures.size(), _display->_surface_bounds.size()); ++i)
		{
			dc.draw_texture(_display->_textures[i], _display->_surface_bounds[i].offset(element_offset));
		}
	}
};

class split_element final : public view_element, public std::enable_shared_from_this<split_element>
{
private:
	const ui::const_surface_ptr _surface;
	const view_element_ptr _child;
	mutable ui::texture_ptr _tex;
	mutable sizei _surface_extent;
	mutable sizei _child_extent;
	recti _surface_bounds;
	recti _child_bounds;

public:
	split_element(ui::const_surface_ptr s, view_element_ptr child, const view_element_style style_in) noexcept :
		view_element(style_in), _surface(std::move(s)), _child(std::move(child))
	{
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		if (is_valid(_surface))
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
				dc.draw_texture(_tex, _surface_bounds.offset(element_offset));
			}
		}

		if (_child)
		{
			_child->render(dc, element_offset);
		}
	}


	void layout(ui::measure_context& mc, const recti bounds_in, ui::control_layouts& positions) override
	{
		bounds = bounds_in;

		const auto surface_cy_extra = (bounds_in.height() - _surface_extent.cy) / 2;
		const auto child_cy_extra = (bounds_in.height() - _child_extent.cy) / 2;

		_surface_bounds = recti({bounds_in.left, bounds_in.top + surface_cy_extra}, _surface_extent);
		_child_bounds = recti({bounds_in.left + _surface_extent.cx, bounds_in.top + child_cy_extra}, _child_extent);
		_child->layout(mc, _child_bounds, positions);
	}

	void tooltip(view_hover_element& hover, const pointi loc, const pointi element_offset) const override
	{
		_child->tooltip(hover, loc, element_offset);
	}

	void hover(interaction_context& ic) override
	{
		_child->hover(ic);
	}

	void dispatch_event(const view_element_event& event) override
	{
		_child->dispatch_event(event);

		if (event.type == view_element_event_type::free_graphics_resources)
		{
			_tex.reset();
		}
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, const pointi loc,
	                                             const pointi element_offset,
	                                             const std::vector<recti>& excluded_bounds) override
	{
		return _child->controller_from_location(host, loc, element_offset, excluded_bounds);
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		_surface_extent = ui::scale_dimensions(_surface->dimensions(), width_limit / 2, true);
		_child_extent = _child->measure(mc, width_limit - _surface_extent.cx);
		return {_child_extent.cx + _surface_extent.cx, std::max(_surface_extent.cy, _child_extent.cy)};
	}
};


class bullet_element final : public view_element, public std::enable_shared_from_this<bullet_element>
{
private:
	const int _icon_padding = 16;
	const icon_index _icon;
	const view_element_ptr _child;
	mutable sizei _child_extent;
	recti _child_bounds;

public:
	bullet_element(const icon_index i, view_element_ptr child,
	               const view_element_style style_in = view_element_style::none) noexcept : view_element(style_in),
		_icon(i), _child(std::move(child))
	{
	}

	bullet_element(const icon_index i, const std::u8string_view text) : _icon(i),
	                                                                    _child(std::make_shared<text_element>(text))
	{
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		auto icon_bounds = bounds.offset(element_offset);
		icon_bounds.right = icon_bounds.left + dc.icon_cxy + _icon_padding;
		icon_bounds.left += 8;

		xdraw_icon(dc, _icon, icon_bounds, ui::color(dc.colors.foreground, dc.colors.alpha), {});

		if (_child)
		{
			_child->render(dc, element_offset);
		}
	}


	void layout(ui::measure_context& mc, const recti bounds_in, ui::control_layouts& positions) override
	{
		bounds = bounds_in;

		const auto child_cy_extra = (bounds_in.height() - _child_extent.cy) / 2;

		_child_bounds = recti({bounds_in.left + mc.icon_cxy + _icon_padding, bounds_in.top + child_cy_extra},
		                      _child_extent);
		_child->layout(mc, _child_bounds, positions);
	}

	void tooltip(view_hover_element& hover, const pointi loc, const pointi element_offset) const override
	{
		_child->tooltip(hover, loc, element_offset);
	}

	void hover(interaction_context& ic) override
	{
		_child->hover(ic);
	}

	void dispatch_event(const view_element_event& event) override
	{
		_child->dispatch_event(event);
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, const pointi loc,
	                                             const pointi element_offset,
	                                             const std::vector<recti>& excluded_bounds) override
	{
		return _child->controller_from_location(host, loc, element_offset, excluded_bounds);
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		const auto w = mc.icon_cxy + _icon_padding;
		_child_extent = _child->measure(mc, width_limit - w);
		return {_child_extent.cx + w, std::max(mc.icon_cxy, _child_extent.cy)};
	}
};
