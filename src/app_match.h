// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once


static std::u8string_view strip_quotes(const std::u8string_view str)
{
	if (str.size() > 1 && str::is_quote(str[0]) && str[str.size() - 1] == str[0])
	{
		return str.substr(1, str.size() - 2);
	}

	return str;
}


inline bool find_auto_complete(const std::vector<std::u8string_view>& queries, const std::u8string_view text,
                               const bool is_path, ui::match_highlights& match)
{
	//const std::u8string_view value = text;
	std::vector<str::part_t> found_subs;

	for (const auto& q : queries)
	{
		if (!q.empty())
		{
			if (q.size() == 1) // Single char8_t?
			{
				size_t match_pos = 0;

				if (is_path)
				{
					const auto last_slash = df::find_last_slash(text);

					if (last_slash != std::u8string_view::npos && last_slash < text.size())
					{
						match_pos = last_slash + 1;
						if (match_pos >= text.size()) match_pos = 0;
					}
				}

				if (str::normalze_for_compare(text[match_pos]) == str::normalze_for_compare(q[0]))
				{
					found_subs.emplace_back(match_pos, 1);
					break;
				}
			}
			else
			{
				auto found = str::ifind(text, q);

				if (found != std::u8string_view::npos)
				{
					found_subs.emplace_back(found, q.size());
					break;
				}
			}
		}
	}

	/*if (!found_subs.empty())
	{
		for (auto i = 1u; i < queries.size(); i++)
		{
			auto q = queries[i];

			for (const auto& v : value_parts)
			{
				auto found = str::ifind(v, q);

				if (found != std::u8string_view::npos)
				{
					found_subs.emplace_back( found, q.size() });
					break;
				}
			}
		}
	}*/

	const auto is_match = queries.size() == found_subs.size();

	if (is_match)
	{
		match = std::move(found_subs);
	}

	return is_match;
}

//bool find_auto_complete_path(const std::vector<str::range> &queries, const str::range &value, ui::match_result& match)
//{
//	auto match_count = 0;
//	auto value_parts = str::split(value);
//
//	for (auto && part : value_parts)
//	{
//		auto name = platform::PathFindFileName(part.begin);
//		auto q = queries[0];
//
//		str::range v(name, part.end);
//
//		if (!q.empty() && !v.empty())
//		{
//			if (q.size() == 1) // Single char8_t?
//			{
//				if (str::normalze_for_compare(v[0]) == str::normalze_for_compare(q[0]))
//				{
//					match.text(value.str(), v.begin - value.begin, v.begin - value.begin + 1);
//					match_count += 1;
//					break;
//				}
//			}
//			else
//			{
//				auto found = str::find_sub_string(v, q);
//
//				if (!found.empty())
//				{
//					match.text(value.str(), found.begin - value.begin, found.end - value.begin);
//					match_count += 1;
//					break;
//				}
//			}
//		}
//	}
//
//	for (auto i = 1; i < queries.size(); i++)
//	{
//		auto q = queries[i];
//
//		for (auto && v : value_parts)
//		{
//			auto found = str::find_sub_string(v, q);
//
//			if (!found.empty())
//			{
//				match_count += 1;
//				break;
//			}
//		}
//	}
//
//	return queries.size() == match_count;
//
//	//if (!q.empty() && !path.empty())
//	//{
//	//	auto name = platform::PathFindFileName(path.begin);
//
//	//	if (q.size() == 1) // Single char8_t?
//	//	{
//	//		if (str::normalze_for_compare(name[0]) == str::normalze_for_compare(q[0]))
//	//		{
//	//			match.text(path.str(), 0, 1);
//	//			return true;
//	//		}
//	//	}
//	//	else
//	//	{
//	//		auto found = str::find_sub_string(name, q);
//
//	//		if (!found.empty())
//	//		{
//	//			match.text(path.str(), found.begin - path.begin, found.end - path.begin);
//	//			return true;
//	//		}
//
//	//		if (df::is_path(q.begin))
//	//		{
//	//			found = str::find_sub_string(path, q);
//
//	//			if (!found.empty())
//	//			{
//	//				match.text(path.str(), found.begin - path.begin, found.end - path.begin);
//	//				return true;
//	//			}
//	//		}
//	//	}
//	//}
//
//	//return false;
//}

static std::vector<ui::text_highlight_t> make_highlights(ui::match_highlights match, const ui::color highlight_clr)
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

class folder_match final : public ui::auto_complete_match, public std::enable_shared_from_this<folder_match>
{
public:
	ui::complete_strategy_t& _parent;
	df::folder_path folder;
	ui::match_highlights match;
	std::u8string lead;

	folder_match(ui::complete_strategy_t& parent, const df::folder_path f, ui::match_highlights m = {}, int w = 1) :
		auto_complete_match(view_element_style::can_invoke), _parent(parent), folder(f), match(std::move(m))
	{
		weight = w;
	}

	folder_match(ui::complete_strategy_t& parent, const df::folder_path f, std::u8string l, ui::match_highlights m = {},
	             int w = 1) : auto_complete_match(view_element_style::can_invoke), _parent(parent), folder(f),
	                          match(std::move(m)), lead(std::move(l))
	{
		weight = w;
	}

	std::u8string edit_text() const override
	{
		return combine2(lead, folder.text());
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		const auto logical_bounds = bounds.offset(element_offset);
		const auto bg_color = calc_background_color(dc);

		if (bg_color.a > 0.0f)
		{
			dc.draw_rounded_rect(logical_bounds.inflate(padding.cx, padding.cy), bg_color);
		}

		const auto highlight_clr = ui::color(ui::style::color::dialog_selected_text, dc.colors.alpha);
		const auto clr = ui::color(dc.colors.foreground, dc.colors.alpha);

		auto rr = logical_bounds;

		if (!str::is_empty(lead))
		{
			const auto dots = u8" ... "sv;
			const auto dots_extent = dc.measure_text(dots, ui::style::font_size::dialog,
			                                         ui::style::text_style::single_line, bounds.width());
			dc.draw_text(dots, {}, rr, ui::style::font_size::dialog, ui::style::text_style::single_line, clr, {});
			rr.left += dots_extent.cx + dc.component_snap;
		}


		const auto highlights = make_highlights(match, highlight_clr);
		dc.draw_text(folder.text(), highlights, rr, ui::style::font_size::dialog, ui::style::text_style::single_line,
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
	std::u8string lead;
	std::u8string text;
	ui::match_highlights match;

	text_match(ui::complete_strategy_t& parent, std::u8string t, std::u8string l, ui::match_highlights m = {},
	           int w = 1) : auto_complete_match(view_element_style::can_invoke), _parent(parent), lead(std::move(l)),
	                        text(std::move(t)), match(std::move(m))
	{
		weight = w;
	}

	std::u8string edit_text() const override
	{
		return str::combine2(lead, text);
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		const auto logical_bounds = bounds.offset(element_offset);
		const auto bg_color = calc_background_color(dc);

		if (bg_color.a > 0.0f)
		{
			dc.draw_rounded_rect(logical_bounds.inflate(padding.cx, padding.cy), bg_color);
		}

		const auto highlight_clr = ui::color(ui::style::color::dialog_selected_text, dc.colors.alpha);
		const auto clr = ui::color(dc.colors.foreground, dc.colors.alpha);

		auto rr = logical_bounds;

		if (!str::is_empty(lead))
		{
			const auto dots = u8" ... "sv;
			const auto dots_extent = dc.measure_text(dots, ui::style::font_size::dialog,
			                                         ui::style::text_style::single_line, bounds.width());
			dc.draw_text(dots, {}, rr, ui::style::font_size::dialog, ui::style::text_style::single_line, clr, {});
			rr.left += dots_extent.cx + dc.component_snap;
		}

		const auto highlights = make_highlights(match, highlight_clr);
		dc.draw_text(text, highlights, rr, ui::style::font_size::dialog, ui::style::text_style::single_line, clr, {});
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
