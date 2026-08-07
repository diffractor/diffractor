// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Auto-complete matching logic for search. Implements folder and text matching
// with highlight support for the search box dropdown suggestions.

#pragma once
#include "ui_controllers.h"
#include "ui_dialog.h"

static std::string_view strip_quotes(const std::string_view str)
{
	if (str.size() > 1 && str::is_quote(str[0]) && str[str.size() - 1] == str[0])
	{
		return str.substr(1, str.size() - 2);
	}

	return str;
}


inline bool find_auto_complete(const std::vector<std::string_view>& queries, const std::string_view text,
                               const bool is_path, ui::match_highlights& match)
{
	std::vector<str::part_t> found_subs;

	for (const auto& q : queries)
	{
		if (!q.empty())
		{
			if (q.size() == 1) // Single char?
			{
				size_t match_pos = 0;

				if (is_path)
				{
					const auto last_slash = df::find_last_slash(text);

					if (last_slash != std::string_view::npos && last_slash < text.size())
					{
						match_pos = last_slash + 1;
						if (match_pos >= text.size()) match_pos = 0;
					}
				}

				if (match_pos < text.size() &&
					str::normalze_for_compare(text[match_pos]) == str::normalze_for_compare(q[0]))
				{
					found_subs.emplace_back(match_pos, 1);
					break;
				}
			}
			else
			{
				auto found = str::ifind(text, q);

				if (found != std::string_view::npos)
				{
					found_subs.emplace_back(found, q.size());
					break;
				}
			}
		}
	}

	const auto is_match = queries.size() == found_subs.size();

	if (is_match)
	{
		match = std::move(found_subs);
	}

	return is_match;
}

static std::vector<ui::text_highlight_t> make_highlights(const ui::match_highlights& match,
                                                         const ui::color highlight_clr)
{
	std::vector<ui::text_highlight_t> highlights(match.size());

	for (auto i = 0u; i < match.size(); i++)
	{
		highlights[i].offset = static_cast<uint32_t>(match[i].offset);
		highlights[i].length = static_cast<uint32_t>(match[i].length);
		highlights[i].clr = highlight_clr;
	}

	return highlights;
}

static std::string auto_complete_lead(std::string lead)
{
	if (!lead.empty() && !str::is_white_space(lead.back())) lead.push_back(' ');
	return lead;
}

static icon_index search_icon(const std::string_view text)
{
	if (df::is_path(str::trim(text))) return icon_index::folder;

	const auto search = df::search_t::parse(text);
	if (search.has_recursive_selector()) return icon_index::recursive;
	if (search.has_selector()) return icon_index::folder;

	for (const auto& term : search.terms())
	{
		if ((term.type == df::search_term_type::value || term.type == df::search_term_type::has_type) &&
			term.key != prop::null)
		{
			return term.key->icon;
		}
		if (term.type == df::search_term_type::date) return icon_index::time;
		if (term.type == df::search_term_type::location || term.type == df::search_term_type::area)
			return icon_index::location;
		if (term.type == df::search_term_type::media_type && term.fg_val) return term.fg_val->icon;
	}

	return icon_index::search;
}

class folder_match final : public ui::auto_complete_match, public std::enable_shared_from_this<folder_match>
{
public:
	ui::complete_strategy_t& _parent;
	df::folder_path folder;
	ui::match_highlights match;
	std::string lead;

	folder_match(ui::complete_strategy_t& parent, const df::folder_path f, ui::match_highlights m = {},
	             const int w = 1) :
		auto_complete_match(view_element_style::can_invoke), _parent(parent), folder(f), match(std::move(m))
	{
		weight = w;
	}

	folder_match(ui::complete_strategy_t& parent, const df::folder_path f, std::string l, ui::match_highlights m = {},
	             const int w = 1) : auto_complete_match(view_element_style::can_invoke), _parent(parent), folder(f),
	                                match(std::move(m)), lead(std::move(l))
	{
		weight = w;
	}

	std::string edit_text() const override
	{
		return combine2(auto_complete_lead(lead), folder.text());
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		const auto logical_bounds = bounds.offset(element_offset);
		const auto bg_color = calc_background_color(dc);

		if (bg_color.a > 0.0f)
		{
			const auto pad = padding * dc.scale_factor;
			dc.draw_rounded_rect(logical_bounds.inflate(pad.cx, pad.cy), bg_color, dc.padding1);
		}

		const auto highlight_clr = ui::color(ui::style::color::dialog_selected_text, dc.colors.alpha);
		const auto clr = ui::color(dc.colors.foreground, dc.colors.alpha);

		auto rr = logical_bounds;
		const auto icon_width = dc.measure_text("X", ui::style::font_face::dialog,
		                                        ui::style::text_style::single_line, bounds.width()).cy;
		auto icon_bounds = rr;
		icon_bounds.right = icon_bounds.left + icon_width;
		xdraw_icon(dc, icon_index::folder, icon_bounds, clr, {});
		rr.left = icon_bounds.right + dc.padding2;

		if (!str::is_empty(lead))
		{
			const auto lead_text = auto_complete_lead(lead);
			const auto lead_extent = dc.measure_text(lead_text, ui::style::font_face::dialog,
			                                         ui::style::text_style::single_line, bounds.width());
			dc.draw_text(lead_text, {}, rr, ui::style::font_face::dialog, ui::style::text_style::single_line, clr, {});
			rr.left += lead_extent.cx;
		}


		const auto highlights = make_highlights(match, highlight_clr);
		dc.draw_text(folder.text(), highlights, rr, ui::style::font_face::dialog, ui::style::text_style::single_line,
		             clr, {});
	}

	void dispatch_event(const view_element_event& event) override
	{
		if (event.type == view_element_event_type::click)
		{
			_parent.selected(shared_from_this(), ui::complete_strategy_t::select_type::click);
		}
		else if (event.type == view_element_event_type::double_click)
		{
			_parent.selected(shared_from_this(), ui::complete_strategy_t::select_type::double_click);
		}
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, const pointi loc,
	                                             const pointi element_offset,
	                                             const std::vector<recti>& excluded_bounds) override
	{
		return default_controller_from_location(*this, host, loc, element_offset, excluded_bounds);
	}
};

class text_match final : public ui::auto_complete_match, public std::enable_shared_from_this<text_match>
{
public:
	ui::complete_strategy_t& _parent;
	std::string lead;
	std::string text;
	ui::match_highlights match;
	icon_index icon;

	text_match(ui::complete_strategy_t& parent, std::string t, std::string l, ui::match_highlights m = {},
	           const int w = 1, const icon_index icon_in = icon_index::search) :
		auto_complete_match(view_element_style::can_invoke), _parent(parent),
		lead(std::move(l)),
		text(std::move(t)), match(std::move(m)), icon(icon_in)
	{
		weight = w;
	}

	std::string edit_text() const override
	{
		return str::combine2(auto_complete_lead(lead), text);
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		const auto logical_bounds = bounds.offset(element_offset);
		const auto bg_color = calc_background_color(dc);

		if (bg_color.a > 0.0f)
		{
			const auto pad = padding * dc.scale_factor;
			dc.draw_rounded_rect(logical_bounds.inflate(pad.cx, pad.cy), bg_color, dc.padding1);
		}

		const auto highlight_clr = ui::color(ui::style::color::dialog_selected_text, dc.colors.alpha);
		const auto clr = ui::color(dc.colors.foreground, dc.colors.alpha);

		auto rr = logical_bounds;
		const auto icon_width = dc.measure_text("X", ui::style::font_face::dialog,
		                                        ui::style::text_style::single_line, bounds.width()).cy;
		auto icon_bounds = rr;
		icon_bounds.right = icon_bounds.left + icon_width;
		xdraw_icon(dc, icon, icon_bounds, clr, {});
		rr.left = icon_bounds.right + dc.padding2;

		if (!str::is_empty(lead))
		{
			const auto lead_text = auto_complete_lead(lead);
			const auto lead_extent = dc.measure_text(lead_text, ui::style::font_face::dialog,
			                                         ui::style::text_style::single_line, bounds.width());
			dc.draw_text(lead_text, {}, rr, ui::style::font_face::dialog, ui::style::text_style::single_line, clr, {});
			rr.left += lead_extent.cx;
		}

		const auto highlights = make_highlights(match, highlight_clr);
		dc.draw_text(text, highlights, rr, ui::style::font_face::dialog, ui::style::text_style::single_line, clr, {});
	}

	void dispatch_event(const view_element_event& event) override
	{
		if (event.type == view_element_event_type::click)
		{
			_parent.selected(shared_from_this(), ui::complete_strategy_t::select_type::click);
		}
		else if (event.type == view_element_event_type::double_click)
		{
			_parent.selected(shared_from_this(), ui::complete_strategy_t::select_type::double_click);
		}
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, const pointi loc,
	                                             const pointi element_offset,
	                                             const std::vector<recti>& excluded_bounds) override
	{
		return default_controller_from_location(*this, host, loc, element_offset, excluded_bounds);
	}
};
