// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once
#include "ui_controls.h"


struct plasma
{
	uint32_t _palette[256];
	std::unique_ptr<uint8_t, df::free_delete> _pixels = nullptr;

	int _fade = 0;
	int _stride = 0;
	bool _show_color = false;
	bool _hover = false;
	int _cosinus[256];

	const static int fade_max = 40;
	const static int _width = 64;
	const static int _height = 64;

	plasma()
	{
		for (auto i = 0; i < 256; ++i)
		{
			_cosinus[i] = static_cast<int>((127.0 * (cos(i * M_PI / 64.0))) + 128.0);
		}

		_stride = calc_stride(_width, 32);
		_pixels = df::unique_alloc<uint8_t>(_height * _stride);

		init_plasma();
		step_plasma();
	}

	~plasma()
	{
		_pixels.reset();
	}


	void step_plasma()
	{
		static uint32_t p1 = 0, p2 = 0, p3 = 0, p4 = 0;

		uint8_t t1 = p1 / 2;
		uint8_t t2 = p2 / 2;

		for (auto y = 0; y < _height; ++y)
		{
			uint8_t t3 = p3 / 2;
			uint8_t t4 = p4 / 2;

			const auto t = _cosinus[t1] + _cosinus[t2];
			auto d = std::bit_cast<uint32_t*>(_pixels.get() + (y * _stride));

			for (auto x = 0; x < _width; ++x)
			{
				*d++ = _palette[((t + _cosinus[t3++] + _cosinus[t4]) >> 2) & 0x000000ff];
				t4 += 2;
			}

			t1 += 2;
			t2 += 1;
		}

		p1 += 1;
		p2 -= 1;
		p3 += 2;
		p4 -= 2;
	}


	void init_plasma()
	{
		const auto fc = fade_max - _fade;

		// Optimize using XMScalarSinCosEst?

		// tone the color to be blue like the app 
		const auto fr = _fade * 0x24;
		const auto fg = _fade * 0x22;
		const auto fb = _fade * 0x20;
		const auto cd = fade_max * 3 * 0x24;

		for (int i = 0; i < 256; ++i)
		{
			const auto r = _cosinus[i];
			const auto g = _cosinus[(i + 32) & 0x0ff];
			const auto b = _cosinus[(i + 64) & 0x0ff];

			const auto c = b + g + r;
			const auto rr = df::mul_div(r, fc, fade_max) + df::mul_div(c, fr, cd);
			const auto gg = df::mul_div(g, fc, fade_max) + df::mul_div(c, fg, cd);
			const auto bb = df::mul_div(b, fc, fade_max) + df::mul_div(c, fb, cd);

			_palette[i] = ui::average(ui::style::color::sidebar_background, ui::rgb(rr, gg, bb));
		}
	}

	void step()
	{
		_show_color = df::jobs_running > 0 || _hover;

		if (_show_color && _fade > 0)
		{
			--_fade;
			init_plasma();
		}
		else if (!_show_color && _fade < fade_max)
		{
			++_fade;
			init_plasma();
		}

		step_plasma();
	}

	void render(const ui::texture_ptr& tex, const sizei dims)
	{
		df::assert_true(dims.cx <= _width);
		df::assert_true(dims.cy <= _height);

		tex->update({_height, _width}, ui::texture_format::RGB, ui::orientation::top_left, _pixels.get(), _stride,
		            _height * _stride);
	}

	bool is_active() const
	{
		return _show_color || (_fade < fade_max);
	}

	static int calc_stride(const int width, const int bpp) noexcept
	{
		const int bpp_width = width * bpp;
		return ((bpp_width + ((bpp_width % 8) ? 8 : 0)) / 8 + 3) & ~3;
	}
};


static std::u8string format_total_text(const df::count_and_size total, const bool multi_line)
{
	std::u8string result;

	if (total.count > 0)
	{
		result = str::format_count(total.count);
	}

	if (multi_line)
	{
		if (!total.size.is_empty())
		{
			result += u8"\n";
			result += prop::format_size(total.size);
		}
	}

	return result;
}

struct text_layout_state
{
	ui::text_layout_ptr tf;
	std::u8string text;
	ui::style::font_size font = ui::style::font_size::dialog;
	ui::style::text_style style = ui::style::text_style::multiline;

	void lazy_load(ui::measure_context& mc)
	{
		if (!tf)
		{
			tf = mc.create_text_layout(font);
			tf->update(text, style);
		}
	}

	void lazy_load(ui::measure_context& mc, const std::u8string& text_in, const ui::style::text_style style_in,
	               const ui::style::font_size font_in = ui::style::font_size::dialog)
	{
		const auto need_create = tf == nullptr ||
			text != text_in ||
			font != font_in ||
			style != style_in;

		if (need_create)
		{
			text = text_in;
			style = style_in;
			font = font_in;
			tf = mc.create_text_layout(font);
			tf->update(text, style);
		}
	}

	bool is_empty() const
	{
		return text.empty();
	}

	void dpi_changed()
	{
		tf.reset();
	}
};

class sidebar_element final : public view_element, public std::enable_shared_from_this<sidebar_element>
{
public:
	view_state& _state;

	mutable text_layout_state icon_layout;
	mutable text_layout_state title_layout;
	mutable text_layout_state total_layout;

	icon_index icon = icon_index::none;
	icon_index tooltip_icon = icon_index::none;
	std::u8string tooltip_text;
	std::u8string key;
	df::search_t search;
	std::function<df::file_group_histogram(view_state& s, df::cancel_token token)> calc_sum;
	df::file_group_histogram summary;
	ui::color32 clr = 0;
	int icon_repeat = 1;

	explicit sidebar_element(view_state& state) noexcept
		: view_element(view_element_style::has_tooltip | view_element_style::can_invoke), _state(state)
	{
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		const auto logical_bounds = bounds.offset(element_offset);
		render_background(dc, element_offset);

		const auto draw_clr = clr
			                      ? ui::color(ui::average(clr, dc.colors.foreground), dc.colors.alpha)
			                      : ui::color(dc.colors.foreground, dc.colors.alpha);
		const auto has_text = !title_layout.is_empty();
		const auto has_image = icon != icon_index::none;

		auto x = logical_bounds.left;

		if (has_image)
		{
			const auto cx = dc.icon_cxy * icon_repeat;

			auto r = logical_bounds;
			r.left = x;
			r.right = x + cx;

			const std::wstring text(icon_repeat, static_cast<wchar_t>(icon));
			icon_layout.lazy_load(dc, str::utf16_to_utf8(text), ui::style::text_style::single_line_center,
			                      ui::style::font_size::icons);
			dc.draw_text(icon_layout.tf, r, draw_clr, {});
			x += cx + dc.baseline_snap;
		}

		auto text_bounds = logical_bounds;
		text_bounds.left = x;

		const auto total_items = summary.total_items();
		const auto total_text = format_total_text(total_items, false);

		if (!str::is_empty(total_text))
		{
			total_layout.lazy_load(dc, total_text, ui::style::text_style::single_line_far);
			const auto sum_text_extent = total_layout.tf->measure_text(text_bounds.width());
			dc.draw_text(total_layout.tf, text_bounds, draw_clr, {});
			text_bounds.right = text_bounds.right - sum_text_extent.cx;
		}

		if (has_text)
		{
			title_layout.lazy_load(dc);
			dc.draw_text(title_layout.tf, text_bounds, draw_clr, {});
		}
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		auto cy = mc.text_line_height(ui::style::font_size::dialog);
		auto cx = width_limit;
		const auto total_text = format_total_text(summary.total_items(), false);

		if (icon != icon_index::none)
		{
			cx -= mc.icon_cxy + mc.baseline_snap;
		}

		if (!str::is_empty(total_text))
		{
			total_layout.lazy_load(mc, total_text, ui::style::text_style::single_line_far);
			const auto sum_text_extent = total_layout.tf->measure_text(cx);
			cx -= sum_text_extent.cx;
		}

		if (!title_layout.is_empty())
		{
			title_layout.lazy_load(mc);
			const auto text_extent = title_layout.tf->measure_text(cx);
			cy = std::max(cy, text_extent.cy);
		}

		return {width_limit, cy};
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, const pointi loc,
	                                             const pointi element_offset,
	                                             const std::vector<recti>& excluded_bounds) override
	{
		return default_controller_from_location(*this, host, loc, element_offset, excluded_bounds);
	}

	void tooltip(view_hover_element& result, const pointi loc, const pointi element_offset) const override
	{
		if (title_layout.is_empty())
		{
			result.elements.add(make_icon_element(tooltip_icon, icon_repeat, view_element_style::line_break));
		}
		else
		{
			result.elements.add(make_icon_element(tooltip_icon, icon_repeat, view_element_style::no_break));
			result.elements.add(std::make_shared<text_element>(title_layout.text, ui::style::font_size::title,
			                                                   ui::style::text_style::multiline,
			                                                   view_element_style::line_break));
		}

		if (str::is_empty(tooltip_text))
		{
			result.elements.add(std::make_shared<text_element>(search.text(), ui::style::font_size::dialog,
			                                                   ui::style::text_style::multiline,
			                                                   view_element_style::line_break));
		}
		else
		{
			result.elements.add(std::make_shared<text_element>(tooltip_text, ui::style::font_size::dialog,
			                                                   ui::style::text_style::multiline,
			                                                   view_element_style::line_break));
		}

		const auto has_folder = search.has_selector();

		result.elements.add(std::make_shared<summary_control>(summary, view_element_style::line_break));
		result.elements.add(std::make_shared<action_element>(has_folder ? tt.click_to_open : tt.click_to_search));
		result.active_bounds = result.window_bounds = bounds.offset(element_offset);
		result.horizontal = true;
	}

	void dispatch_event(const view_element_event& event) override
	{
		if (event.type == view_element_event_type::invoke)
		{
			_state.open(event.host, search, {});
		}
		else if (event.type == view_element_event_type::dpi_changed)
		{
			icon_layout.dpi_changed();
			title_layout.dpi_changed();
			total_layout.dpi_changed();
		}
	}
};

class sidebar_drive_element final : public view_element, public std::enable_shared_from_this<sidebar_drive_element>
{
public:
	mutable text_layout_state icon_layout;
	mutable text_layout_state title_layout;
	mutable text_layout_state total_layout;

	view_state& _state;
	icon_index icon = icon_index::none;
	platform::drive_t _drive;
	const int graph_height = 8;

	explicit sidebar_drive_element(view_state& state, platform::drive_t& d) noexcept
		: view_element(view_element_style::has_tooltip | view_element_style::can_invoke), _state(state), _drive(d)
	{
		if (!str::is_empty(_drive.vol_name))
		{
			title_layout.text = str::format(u8"{} ({})", _drive.name, _drive.vol_name);
		}
		else
		{
			title_layout.text = _drive.name;
		}

		icon = drive_icon(_drive.type);
	}

	static icon_index drive_icon(platform::drive_type d)
	{
		switch (d)
		{
		case platform::drive_type::removable: return icon_index::usb;
		case platform::drive_type::fixed: return icon_index::hard_drive;
		case platform::drive_type::remote: return icon_index::network;
		case platform::drive_type::cdrom: return icon_index::disk;
		case platform::drive_type::device: return icon_index::disk;
		default: ;
		}

		return icon_index::hard_drive;
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		const auto logical_bounds = bounds.offset(element_offset);
		render_background(dc, element_offset);

		const auto has_image = icon != icon_index::none;
		const auto draw_clr = ui::color(dc.colors.foreground, dc.colors.alpha);

		auto x = logical_bounds.left;

		if (has_image)
		{
			auto r = logical_bounds;
			r.right = r.left + dc.icon_cxy;
			r.bottom -= graph_height;
			const std::wstring text(1, static_cast<wchar_t>(icon));
			icon_layout.lazy_load(dc, str::utf16_to_utf8(text), ui::style::text_style::single_line_center,
			                      ui::style::font_size::icons);
			dc.draw_text(icon_layout.tf, r, draw_clr, {});
			x += dc.icon_cxy + dc.baseline_snap;
		}

		auto text_bounds = logical_bounds;

		text_bounds.left = x;
		text_bounds.bottom -= graph_height;

		auto graph_bounds = logical_bounds;
		//graph_bounds.left += 8;
		//graph_bounds.right -= 8;
		graph_bounds.top = text_bounds.bottom + 1;

		auto used_bounds = graph_bounds.inflate(-1);
		used_bounds.right = used_bounds.left + static_cast<int>(df::mul_div(
			static_cast<int64_t>(used_bounds.width()), static_cast<int64_t>(_drive.used.to_int64()),
			static_cast<int64_t>(_drive.capacity.to_int64())));

		dc.draw_rect(graph_bounds, ui::color(ui::style::color::sidebar_background, draw_clr.a).scale(1.22f));
		dc.draw_rect(used_bounds,
		             ui::color(
			             ui::average(ui::style::color::sidebar_background, ui::style::color::important_background),
			             draw_clr.a));

		const auto total_text = str::format(u8"{}|{}", prop::format_size(_drive.used),
		                                    prop::format_size(_drive.capacity));

		if (!str::is_empty(total_text))
		{
			total_layout.lazy_load(dc, total_text, ui::style::text_style::single_line_far);
			const auto sum_text_extent = total_layout.tf->measure_text(text_bounds.width());
			dc.draw_text(total_layout.tf, text_bounds, draw_clr, {});
			text_bounds.right = text_bounds.right - sum_text_extent.cx;
		}

		title_layout.lazy_load(dc, _drive.name, ui::style::text_style::single_line);
		dc.draw_text(title_layout.tf, text_bounds, draw_clr, {});
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		const auto cy = mc.text_line_height(ui::style::font_size::dialog);
		return {width_limit, cy + graph_height};
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, const pointi loc,
	                                             const pointi element_offset,
	                                             const std::vector<recti>& excluded_bounds) override
	{
		return default_controller_from_location(*this, host, loc, element_offset, excluded_bounds);
	}

	void tooltip(view_hover_element& result, const pointi loc, const pointi element_offset) const override
	{
		result.elements.add(make_icon_element(icon, view_element_style::no_break));
		result.elements.add(std::make_shared<text_element>(_drive.name, ui::style::font_size::title,
		                                                   ui::style::text_style::multiline,
		                                                   view_element_style::line_break));


		const auto table = std::make_shared<ui::table_element>(view_element_style::center);

		if (!str::is_empty(_drive.vol_name))
		{
			table->add(tt.disk_label, _drive.vol_name);
		}

		table->add(tt.disk_capacity, _drive.capacity.str());
		table->add(tt.disk_free, _drive.free.str());
		table->add(tt.disk_used, _drive.used.str());
		table->add(tt.disk_system, _drive.file_system);

		result.elements.add(table);
		result.elements.add(std::make_shared<action_element>(tt.click_to_open));

		result.active_bounds = result.window_bounds = bounds.offset(element_offset);
		result.horizontal = true;
	}

	void dispatch_event(const view_element_event& event) override
	{
		if (event.type == view_element_event_type::invoke)
		{
			_state.open(event.host, _drive.name);
		}
		else if (event.type == view_element_event_type::dpi_changed)
		{
			icon_layout.dpi_changed();
			title_layout.dpi_changed();
			total_layout.dpi_changed();
		}
	}
};

using search_item_ptr = std::shared_ptr<sidebar_element>;
using drive_item_ptr = std::shared_ptr<sidebar_drive_element>;
using search_items_by_key_t = df::hash_map<std::u8string, std::shared_ptr<sidebar_element>, df::ihash, df::ieq>;

class search_item_factory
{
public:
	icon_index calc_folder_icon(const df::folder_path path) const
	{
		const auto path_text = path.text();
		if (platform::is_server(path_text) || path.is_unc_path()) return icon_index::network;
		if (contains(path_text, u8"onedrive") || contains(path_text, tt.folder_onedrive)) return icon_index::cloud;
		if (contains(path_text, u8"picture") || contains(path_text, tt.folder_picture)) return icon_index::photo;
		if (contains(path_text, u8"video") || contains(path_text, tt.folder_video)) return icon_index::video;
		if (contains(path_text, u8"music") || contains(path_text, tt.folder_music)) return icon_index::audio;
		return icon_index::folder;
	}

	std::vector<search_item_ptr> create_folder_items(view_state& s, const search_items_by_key_t& existing) const
	{
		std::vector<search_item_ptr> results;

		for (auto folder : s.item_index.index_roots().folders)
		{
			auto key = str::format(u8"f:{}", folder);
			auto i = create_or_find_item(s, existing, key);
			i->tooltip_icon = i->icon = calc_folder_icon(folder);
			i->title_layout.text = folder.name();
			i->search = df::search_t().add_selector(df::item_selector(folder));
			i->calc_sum = [folder](view_state& s, df::cancel_token token)
			{
				return s.item_index.calc_folder_summary(folder, token);
			};
			results.emplace_back(i);
		}

		std::ranges::sort(results, [](auto&& l, auto&& r)
		{
			return str::icmp(l->title_layout.text, r->title_layout.text) < 0;
		});

		return results;
	}

	std::vector<drive_item_ptr> create_drive_items(view_state& s, const search_items_by_key_t& existing) const
	{
		const auto drives = platform::scan_drives(false);

		std::vector<drive_item_ptr> results;

		for (auto d : drives)
		{
			results.emplace_back(std::make_shared<sidebar_drive_element>(s, d));
		}

		return results;
	}

	std::vector<search_item_ptr> create_search_items(view_state& s, const search_items_by_key_t& existing) const
	{
		std::vector<search_item_ptr> results;

		for (auto i = 0; i < setting.search.count; i++)
		{
			auto&& title = setting.search.title[i];
			auto&& path = setting.search.path[i];

			if (!title.empty() && !path.empty())
			{
				auto search = df::search_t::parse(path);
				auto item = create_or_find_item(s, existing, u8"s:" + path);

				item->tooltip_icon = item->icon = search.has_selector() ? icon_index::folder : icon_index::search;
				item->title_layout.text = tt.translate_text(title);
				item->search = search;
				item->calc_sum = [search](view_state& s, df::cancel_token token)
				{
					return s.item_index.count_matches(search, token);
				};
				results.emplace_back(item);
			}
		}

		results.emplace_back(create_item(s, existing, u8"@duplicates", icon_index::compare, tt.duplicates,
		                                 tt.duplicates_tooltip, 0));

		return results;
	}

	void compact_summary(index_state::distinct_result_result& summary) const
	{
		std::ranges::sort(summary, [](auto&& left, auto&& right) { return str::icmp(left.first, right.first) < 0; });

		auto dst = summary.begin();
		const auto last = summary.end();

		if (dst != last)
		{
			auto i = dst;
			++i;

			while (i != last)
			{
				if (str::icmp(i->first, dst->first) == 0)
				{
					dst->second.add(i->second);
				}
				else
				{
					++dst;
					*dst = std::move(*i);
				}

				++i;
			}

			++dst;

			if (dst != last)
			{
				summary.erase(dst, last);
			}
		}
	}

	std::vector<search_item_ptr> create_tags(view_state& s, const search_items_by_key_t& existing) const
	{
		std::vector<search_item_ptr> results;

		auto tags = s.item_index.distinct_tags();
		str::split2(setting.favourite_tags, true, [&tags](std::u8string_view part)
		{
			tags.emplace_back(part, df::file_group_histogram{});
		});

		compact_summary(tags);

		for (const auto& t : tags)
		{
			auto tag = std::u8string(t.first);
			auto search = df::search_t().with(prop::tag, tag);

			if (!str::is_empty(tag) && !search.is_empty())
			{
				auto key = str::format(u8"t:{}", tag);
				auto i = create_or_find_item(s, existing, key);

				i->tooltip_icon = icon_index::tag;
				i->title_layout.text = tag;
				i->search = search;
				i->calc_sum = [tag](view_state& s, df::cancel_token token) { return s.item_index.tag_summary(tag); };
				i->summary = t.second;
				results.emplace_back(i);
			}
		}

		return results;
	}

	search_item_ptr create_rating_item(view_state& s, const search_items_by_key_t& existing, const std::u8string& key,
	                                   const int rating) const
	{
		auto i = create_or_find_item(s, existing, key);
		i->tooltip_icon = i->icon = icon_index::star_solid;
		i->icon_repeat = rating;
		i->search = df::search_t::parse(key);
		i->calc_sum = [rating](view_state& s, df::cancel_token token) { return s.item_index.rating_summary(rating); };
		i->summary = s.item_index.rating_summary(rating);
		return i;
	}

	search_item_ptr create_item(view_state& s, const search_items_by_key_t& existing, const std::u8string& key,
	                            icon_index icon, app_text_t::text_t name, app_text_t::text_t tooltip,
	                            const ui::color32 clr = 0) const
	{
		auto i = create_or_find_item(s, existing, key);
		i->tooltip_icon = i->icon = icon;
		i->clr = clr;
		i->title_layout.text = name;
		i->tooltip_text = tooltip;
		i->search = df::search_t::parse(key);
		i->calc_sum = [search = i->search](view_state& s, df::cancel_token token)
		{
			return s.item_index.count_matches(search, token);
		};
		return i;
	}

	std::vector<search_item_ptr> create_ratings(view_state& s, const search_items_by_key_t& existing) const
	{
		std::vector<search_item_ptr> results;
		results.emplace_back(create_rating_item(s, existing, u8"rating:5", 5));
		results.emplace_back(create_rating_item(s, existing, u8"rating:4", 4));
		results.emplace_back(create_rating_item(s, existing, u8"rating:3", 3));
		results.emplace_back(create_rating_item(s, existing, u8"rating:2", 2));
		results.emplace_back(create_rating_item(s, existing, u8"rating:1", 1));

		results.emplace_back(create_item(s, existing, u8"rating:-1", icon_index::cancel, tt.command_rate_rejected, {},
		                                 color_rate_rejected));
		return results;
	}

	std::vector<search_item_ptr> create_labels(view_state& s, const search_items_by_key_t& existing) const
	{
		static df::hash_map<std::u8string_view, ui::color32, df::ihash, df::ieq> colors =
		{
			{u8"select", color_label_select},
			{u8"second", color_label_second},
			{u8"approved", color_label_approved},
			{u8"review", color_label_review},
			{u8"to do", color_label_to_do},
		};

		std::vector<search_item_ptr> results;

		auto labels = s.item_index.distinct_labels();
		labels.emplace_back(u8"select", df::file_group_histogram{});
		labels.emplace_back(u8"second", df::file_group_histogram{});
		labels.emplace_back(u8"approved", df::file_group_histogram{});
		labels.emplace_back(u8"review", df::file_group_histogram{});
		labels.emplace_back(u8"to do", df::file_group_histogram{});

		compact_summary(labels);

		for (const auto& lab : labels)
		{
			auto label = std::u8string(lab.first);
			auto key = str::format(u8"l:{}", label);
			auto search = df::search_t().with(prop::label, label);

			if (!str::is_empty(label) && !search.is_empty())
			{
				auto i = create_or_find_item(s, existing, key);
				i->tooltip_icon = i->icon = icon_index::flag;
				i->clr = colors[lab.first];
				i->title_layout.text = label;
				i->tooltip_text = {};
				i->search = search;
				i->calc_sum = [label](view_state& s, df::cancel_token token)
				{
					return s.item_index.label_summary(label);
				};
				i->summary = lab.second;
				results.emplace_back(i);
			}
		}

		return results;
	}


	search_item_ptr create_or_find_item(view_state& s, const search_items_by_key_t& existing,
	                                    const std::u8string& key) const
	{
		const auto found = existing.find(key);

		if (found != existing.end())
		{
			return found->second;
		}

		auto result = std::make_shared<sidebar_element>(s);
		result->key = key;
		return result;
	}
};

static int calc_indexing_perc(const index_statistic& stats)
{
	const auto total = stats.index_item_count;
	const auto processed = stats.index_item_count - stats.index_item_remaining;
	const auto result = df::mul_div(std::clamp(processed, 0, total), 100, total);
	return std::clamp(result, 1, 100);
}

class sidebar_indexing_element final : public view_element,
                                       public std::enable_shared_from_this<sidebar_indexing_element>
{
private:
	ui::style::font_size _font = ui::style::font_size::title;
	view_state& _s;

public:
	sidebar_indexing_element(view_state& s, const view_element_style style_in) noexcept :
		view_element(
			style_in | view_element_style::can_invoke | view_element_style::has_tooltip |
			view_element_style::important), _s(s)
	{
		update_background_color();
	}

	std::u8string format_text() const
	{
		return str::format(u8"{} {}%", tt.indexing, calc_indexing_perc(_s.item_index.stats));
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		const auto logical_bounds = bounds.offset(element_offset);
		render_background(dc, element_offset);
		const auto clr = ui::color(dc.colors.foreground, dc.colors.alpha);
		dc.draw_text(format_text(), logical_bounds, _font, ui::style::text_style::multiline_center, clr, {});
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		const auto extent = mc.measure_text(format_text(), _font, ui::style::text_style::multiline_center, width_limit);
		return {width_limit, extent.cy};
	}

	void tooltip(view_hover_element& hover, const pointi loc, const pointi element_offset) const override
	{
		hover.elements.add(std::make_shared<text_element>(tt.indexing_message, ui::style::font_size::dialog,
		                                                  ui::style::text_style::multiline,
		                                                  view_element_style::line_break));
		hover.elements.add(std::make_shared<action_element>(tt.click_collection_options));

		hover.active_bounds = hover.window_bounds = bounds.offset(element_offset);
		hover.horizontal = true;
	}

	void dispatch_event(const view_element_event& event) override
	{
		if (event.type == view_element_event_type::invoke)
		{
			event.host->invoke(commands::options_collection);
		}
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, const pointi loc,
	                                             const pointi element_offset,
	                                             const std::vector<recti>& excluded_bounds) override
	{
		return default_controller_from_location(*this, host, loc, element_offset, excluded_bounds);
	}
};

class sidebar_file_type_element final : public view_element,
                                        public std::enable_shared_from_this<sidebar_file_type_element>
{
public:
	bool _center_hover = false;
	mutable bool _pie_invalid = true;
	std::u8string _text;
	view_state& _state;

	struct pie_chart_entry
	{
		double amount = 0.0;
		int id = 0;
		double start_rad = 0.0;
		double end_rad = 0.0;
		bool focus = false;
		ui::color32 clr;
	};

	using file_type_entries_t = std::vector<pie_chart_entry>;
	file_type_entries_t _file_type_entries;

	sidebar_file_type_element(view_state& state) noexcept : view_element(
		                                                        view_element_style::has_tooltip |
		                                                        view_element_style::can_invoke), _state(state)
	{
	}

	void populate(const df::file_group_histogram& summary)
	{
		_file_type_entries.clear();
		_file_type_entries.reserve(max_file_type_count);

		auto total = 0.0;

		for (auto i = 1; i < max_file_type_count; ++i)
		{
			const auto& c = summary.counts[i];

			if (c.count > 0)
			{
				const auto ft = file_group_from_index(i);
				const auto count = std::cbrt(static_cast<double>(c.size.to_int64()));

				pie_chart_entry e;
				e.id = i;
				e.amount = count;
				e.clr = ft->color;
				_file_type_entries.emplace_back(e);

				total += count;
			}
		}

		double start = -M_PI;

		for (auto&& e : _file_type_entries)
		{
			const auto tt = e.amount / total;

			e.start_rad = start;
			start = e.end_rad = start + (2.0 * M_PI * tt);
		}

		const auto total_items = summary.total_items();
		_text = format_total_text(total_items, true);
		_pie_invalid = true;
	}

	int file_type_id_from_angle(const double rads)
	{
		for (const auto& e : _file_type_entries)
		{
			if (rads >= e.start_rad && rads <= e.end_rad)
			{
				return e.id;
			}
		}

		return -1;
	}

	bool hover_file_type(int id)
	{
		auto changed = false;

		for (auto&& e : _file_type_entries)
		{
			const auto focus = e.id == id;

			if (e.focus != focus)
			{
				changed = true;
				e.focus = focus;
			}
		}

		return changed;
	}

	void tooltip(view_hover_element& hover, const pointi loc, const pointi element_offset) const override
	{
		if (_center_hover)
		{
			const auto file_types = _state.item_index.file_types();
			const auto index_roots = _state.item_index.includes_with_totals();
			const auto total = file_types.total_items();
			const auto num = platform::format_number(str::to_string(total.count));
			const auto num_folder = platform::format_number(str::to_string(_state.item_index.stats.index_folder_count));
			const auto size = prop::format_size(total.size);
			const auto text = str::format(tt.total_title, num, size, num_folder);

			hover.elements.add(std::make_shared<text_element>(tt.collection_title, ui::style::font_size::title,
			                                                  ui::style::text_style::multiline,
			                                                  view_element_style::line_break |
			                                                  view_element_style::center));
			hover.elements.add(std::make_shared<text_element>(text, ui::style::font_size::dialog,
			                                                  ui::style::text_style::multiline,
			                                                  view_element_style::line_break |
			                                                  view_element_style::center));

			hover.elements.add(std::make_shared<divider_element>());
			hover.elements.add(
				std::make_shared<summary_control>(
					file_types, view_element_style::line_break | view_element_style::center));
			hover.elements.add(std::make_shared<divider_element>());

			const auto table = std::make_shared<ui::table_element>(view_element_style::center);
			table->no_shrink_col[1] = true;
			table->no_shrink_col[2] = true;

			for (const auto& f : index_roots)
			{
				const auto folder_text = std::make_shared<text_element>(f.folder.text());
				folder_text->foreground_color(ui::emphasize(ui::style::color::view_text));
				const auto count_text = std::make_shared<text_element>(str::format_count(f.count),
				                                                       ui::style::text_style::single_line_far);
				const auto size_text = std::make_shared<text_element>(prop::format_size(f.size),
				                                                      ui::style::text_style::single_line_far);
				table->add(folder_text, count_text, size_text);
			}

			hover.elements.add(table);
			hover.elements.add(std::make_shared<divider_element>());
			hover.elements.add(std::make_shared<action_element>(tt.click_collection_options));

			hover.prefered_size = view_hover_element::default_prefered_size + 64;
		}
		else
		{
			for (const auto& e : _file_type_entries)
			{
				if (e.focus)
				{
					const auto file_types = _state.item_index.file_types();
					const auto& c = file_types.counts[e.id];
					const auto ft = file_group_from_index(e.id);
					const auto icon = ft->icon;
					const auto num = platform::format_number(str::to_string(c.count));
					const auto size = prop::format_size(c.size);

					hover.elements.add(make_icon_element(icon, view_element_style::no_break));
					hover.elements.add(std::make_shared<text_element>(ft->display_name(c.count > 1),
					                                                  ui::style::font_size::title,
					                                                  ui::style::text_style::multiline,
					                                                  view_element_style::line_break));

					const auto text = str::format(tt.collection_contains, num, ft->display_name(c.count > 1), size);
					hover.elements.add(std::make_shared<text_element>(text, ui::style::font_size::dialog,
					                                                  ui::style::text_style::multiline,
					                                                  view_element_style::line_break));
				}
			}

			if (!hover.elements.is_empty())
			{
				hover.elements.add(std::make_shared<action_element>(tt.click_to_open));
				hover.prefered_size = df::mul_div(view_hover_element::default_prefered_size, 2, 3);
			}
		}

		hover.active_bounds = hover.window_bounds = bounds.offset(element_offset);
		hover.horizontal = true;
	}

	void hover(interaction_context& ic) override
	{
		bool changed = false;
		const auto logical_bounds = bounds.offset(ic.element_offset);
		const auto hovering = logical_bounds.contains(ic.loc);

		if (hovering)
		{
			const auto center = logical_bounds.center();
			const auto dx = ic.loc.x - center.x;
			const auto dy = ic.loc.y - center.y;
			const auto dd = df::isqrt(dy * dy + dx * dx);
			const auto center_hover = dd < logical_bounds.width() / 4;

			if (center_hover)
			{
				changed |= hover_file_type(-1);
			}
			else
			{
				changed |= hover_file_type(file_type_id_from_angle(atan2(dy, dx)));
			}

			if (_center_hover != center_hover)
			{
				_center_hover = center_hover;
				changed = true;
			}
		}
		else
		{
			changed |= hover_file_type(-1);
		}

		if (changed)
		{
			ic.invalidate_view = true;
			_pie_invalid = true;
			_state.invalidate_view(view_invalid::tooltip);
		}
	}

	void dispatch_event(const view_element_event& event) override
	{
		if (event.type == view_element_event_type::invoke)
		{
			if (_center_hover)
			{
				_state.invoke(commands::options_collection);
			}
			else
			{
				for (const auto& e : _file_type_entries)
				{
					if (e.focus)
					{
						const auto ft = file_group_from_index(e.id);
						_state.open(event.host, df::search_t().add_media_type(ft), {});
						break;
					}
				}
			}
		}
	}

	mutable ui::texture_ptr _tex;

	static void draw_draw_pie_chart(const ui::texture_ptr& t, const sizei dims,
	                                const std::vector<pie_chart_entry>& entries, const ui::color32& bg_clr,
	                                const ui::color32& center_clr)
	{
		ui::color32 colors[64];

		for (int i = 0; i < 64; ++i)
		{
			const auto rad = (i * M_PI / 32.0) - M_PI;
			auto c = ui::bgr(bg_clr);

			for (const auto& e : entries)
			{
				if (rad >= e.start_rad && rad <= e.end_rad)
				{
					c = e.focus ? ui::lighten(ui::bgr(e.clr), 0.11f) : ui::bgr(e.clr);
					break;
				}
			}

			colors[i] = c;
		}

		const pointi center = {dims.cx / 2, dims.cy / 2};
		const int radius = std::min(dims.cx / 2, dims.cy / 2) - 1;

		const auto s = std::make_shared<ui::surface>();
		s->alloc(dims.cx, dims.cy, ui::texture_format::RGB);
		s->fill_pie(center, radius, colors, ui::bgr(center_clr), ui::bgr(bg_clr));
		t->update(s);
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		const auto logical_bounds = bounds.offset(element_offset);

		if (!_tex)
		{
			_tex = dc.create_texture();
		}

		if (_pie_invalid)
		{
			//render_background(rc, element_offset, clr);
			const auto bg_clr = ui::style::color::sidebar_background;
			auto center_clr = ui::color(ui::style::color::sidebar_background);

			const auto tracking = is_style_bit_set(view_element_style::tracking);
			const auto hover = is_style_bit_set(view_element_style::hover);
			const auto selected = is_style_bit_set(view_element_style::selected);

			if (_center_hover)
			{
				center_clr = view_handle_color(selected, hover || _center_hover, tracking, dc.frame_has_focus, true);
			}

			const auto is_detecting = _state.item_index.detecting > 0;

			if (is_detecting)
			{
				center_clr = ui::style::color::important_background;
			}

			draw_draw_pie_chart(_tex, logical_bounds.extent(), _file_type_entries, bg_clr, center_clr.rgba());
			_pie_invalid = false;
		}

		const auto clr = ui::color(dc.colors.foreground, dc.colors.alpha);
		const auto center = logical_bounds.center();

		dc.draw_texture(_tex, center_rect(_tex->dimensions(), center));
		dc.draw_text(_text, logical_bounds, ui::style::font_size::dialog, ui::style::text_style::multiline_center, clr,
		             {});
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		return {width_limit, width_limit};
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, const pointi loc,
	                                             const pointi element_offset,
	                                             const std::vector<recti>& excluded_bounds) override
	{
		return default_controller_from_location(*this, host, loc, element_offset, excluded_bounds);
	}

	void layout(ui::measure_context& mc, const recti bounds_in, ui::control_layouts& positions) override
	{
		if (bounds != bounds_in)
		{
			_pie_invalid = true;
			bounds = bounds_in;
		}
	}
};

struct sidebar_history_element final : public view_element, public std::enable_shared_from_this<sidebar_history_element>
{
	static const int col_count = 12;
	static const int row_count = 10;
	static const int invalid_hover_month = -1;
	static const int base_row_height = 10;

	view_state& _state;
	std::array<double, col_count * row_count> dates{};
	std::array<df::date_counts, col_count * row_count> _counts;

	int _min_val = 0;
	int _max_val = 0;
	int _hover_month = invalid_hover_month;
	int _current_year = 0;
	int _current_month = 0;
	mutable int row_height = base_row_height;

	sidebar_history_element(view_state& state) noexcept : view_element(
		                                                      view_element_style::has_tooltip |
		                                                      view_element_style::can_invoke), _state(state)
	{
		populate({}); // Set some defaults
	}

	void populate(const df::date_histogram& summary)
	{
		_min_val = INT_MAX;
		_max_val = 0;
		_counts = summary.dates;

		for (auto i = 0u; i < summary.dates.size(); i++)
		{
			const auto val = std::cbrt(summary.dates[i].created);
			dates[i] = val;
			if (_min_val > val) _min_val = val;
			if (_max_val < val) _max_val = val;
		}

		const auto now = platform::now().date();
		_current_year = now.year;
		_current_month = now.month;
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		const auto logical_bounds = bounds.offset(element_offset);
		//render_background(dc, element_offset, clr);

		for (auto y = 0; y < row_count; y++)
		{
			const auto yy1 = logical_bounds.top + (y * row_height);
			const auto yy2 = logical_bounds.top + ((y + 1) * row_height);

			if (yy2 < logical_bounds.bottom)
			{
				for (auto m = 0; m < col_count; m++)
				{
					if (y == _current_year && m > _current_month) break;

					const auto xx1 = logical_bounds.left + df::mul_div(m, logical_bounds.width(), col_count);
					const auto xx2 = logical_bounds.left + df::mul_div(m + 1, logical_bounds.width(), col_count);

					const auto val = dates[y * 12 + m];
					const auto scale = std::max(df::round((val - _min_val) * 255 / (_max_val - _min_val)), 5);
					const recti cell = {xx1, yy1, xx2, yy2};
					dc.draw_rect(cell.inflate(-1),
					             ui::color(ui::lerp(ui::style::color::sidebar_background, dc.colors.foreground, scale),
					                       dc.colors.alpha));
				}
			}
		}

		if (hover_month_is_valid())
		{
			const auto m = _hover_month & 0x0F;
			const auto y = _hover_month >> 8;
			const auto yy1 = logical_bounds.top + (y * row_height);
			const auto yy2 = logical_bounds.top + ((y + 1) * row_height);
			const auto xx1 = logical_bounds.left + df::mul_div(m, logical_bounds.width(), col_count);
			const auto xx2 = logical_bounds.left + df::mul_div(m + 1, logical_bounds.width(), col_count);
			const recti cell = {xx1, yy1, xx2, yy2};

			dc.draw_rect(cell, ui::color(ui::style::color::important_background, dc.colors.alpha));
		}
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		row_height = df::round(mc.scale_factor * base_row_height);
		return {width_limit, row_count * row_height + 1};
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, const pointi loc,
	                                             const pointi element_offset,
	                                             const std::vector<recti>& excluded_bounds) override
	{
		return default_controller_from_location(*this, host, loc, element_offset, excluded_bounds);
	}

	bool hover_month_is_valid() const
	{
		return _hover_month != invalid_hover_month;
	}

	void tooltip(view_hover_element& hover, const pointi loc, const pointi element_offset) const override
	{
		if (hover_month_is_valid())
		{
			const auto m = _hover_month & 0x0F;
			const auto y = _hover_month >> 8;
			const auto date_count = _counts[y * 12 + m];
			const auto month = str::month(m + 1, true);
			const auto year = str::to_string(_current_year - y);

			hover.elements.add(make_icon_element(icon_index::time, view_element_style::no_break));
			hover.elements.add(std::make_shared<text_element>(month, ui::style::font_size::title,
			                                                  ui::style::text_style::multiline,
			                                                  view_element_style::no_break));
			hover.elements.add(std::make_shared<text_element>(year, ui::style::font_size::title,
			                                                  ui::style::text_style::multiline,
			                                                  view_element_style::line_break));
			hover.elements.add(std::make_shared<text_element>(tt.collection_contains2, ui::style::font_size::dialog,
			                                                  ui::style::text_style::multiline,
			                                                  view_element_style::line_break));

			const auto table = std::make_shared<ui::table_element>(view_element_style::center);
			const auto num1 = std::make_shared<text_element>(
				platform::format_number(str::to_string(date_count.created)),
				ui::style::text_style::single_line_far);
			const auto num2 = std::make_shared<text_element>(
				platform::format_number(str::to_string(date_count.modified)),
				ui::style::text_style::single_line_far);
			table->add(num1, std::make_shared<text_element>(str::format(tt.items_created_fmt, month, year)));
			table->add(num2, std::make_shared<text_element>(str::format(tt.items_modified_fmt, month, year)));
			hover.elements.add(table);

			hover.elements.add(std::make_shared<action_element>(tt.click_to_open_created_modified));
		}

		hover.active_bounds = hover.window_bounds = bounds.offset(element_offset);
		hover.horizontal = true;
	}

	void hover(interaction_context& ic) override
	{
		const auto logical_bounds = bounds.offset(ic.element_offset);
		const auto hovering = logical_bounds.contains(ic.loc);
		int new_hover_month = invalid_hover_month;

		if (hovering)
		{
			const auto month = std::clamp((ic.loc.x - logical_bounds.left) * col_count / logical_bounds.width(), 0,
			                              col_count - 1);
			const auto year = std::clamp((ic.loc.y - logical_bounds.top) / row_height, 0, row_count - 1);

			if (year > 0 || month < _current_month)
			{
				new_hover_month = (year << 8) + month;
			}
		}

		if (new_hover_month != _hover_month)
		{
			_hover_month = new_hover_month;
			ic.invalidate_view = true;
			_state.invalidate_view(view_invalid::tooltip);
		}
	}

	void dispatch_event(const view_element_event& event) override
	{
		if (event.type == view_element_event_type::invoke && hover_month_is_valid())
		{
			const auto m = _hover_month & 0x0F;
			const auto y = _hover_month >> 8;

			const auto ks = ui::current_key_state();
			const auto type = ks.control ? df::date_parts_prop::modified : df::date_parts_prop::created;
			const auto search = df::search_t().day(0, m + 1, _current_year - y, type);

			_state.open(event.host, search, {});
		}
	}
};

class sidebar_map_element final : public view_element, public std::enable_shared_from_this<sidebar_map_element>
{
private:
	view_state& _state;

	ui::const_surface_ptr _surface;
	ui::const_surface_ptr _surface_original;
	mutable ui::texture_ptr _tex;
	sizei _hover_offset;

	int default_y_offset = 0;
	int default_height = 110;
	mutable bool _tex_invalid = true;

	std::vector<location_group> _locations;
	int _hover_location = -1;

public:
	sidebar_map_element(view_state& state, ui::const_surface_ptr s) noexcept :
		view_element(view_element_style::has_tooltip | view_element_style::can_invoke), _state(state),
		_surface_original(std::move(s))
	{
		const auto dims = _surface_original->dimensions();
		_hover_offset.cy = default_y_offset = (default_height - dims.cy) / 2;
		_surface = _surface_original;
	}

	void populate(const df::location_heat_map& summary, const df::hash_map<uint32_t, location_group>& locations)
	{
		std::vector<location_group> new_locations;
		new_locations.reserve(locations.size());

		for (const auto& loc : locations)
		{
			new_locations.emplace_back(loc.second);
		}

		if (_locations != new_locations)
		{
			_locations = std::move(new_locations);
			_hover_location = -1;

			const auto dims = _surface_original->dimensions();
			df::assert_true(dims.cx == df::location_heat_map::map_width);
			df::assert_true(dims.cy == df::location_heat_map::map_height);

			const auto max_coord = *std::max_element(summary.coordinates.begin(), summary.coordinates.end());

			if (max_coord == 0)
			{
				_surface = _surface_original;
			}
			else
			{
				auto surface = std::make_shared<ui::surface>();
				const auto pixels = surface->alloc(
					{df::location_heat_map::map_width, df::location_heat_map::map_height}, ui::texture_format::ARGB);
				const auto cc = ui::bgr(ui::style::color::important_background) | 0xFF000000;

				memset(pixels, 0, surface->stride() * df::location_heat_map::map_height);

				for (auto y = 1u; y < df::location_heat_map::map_height - 1; ++y)
				{
					const auto map_line = std::bit_cast<const uint32_t*>(_surface_original->pixels_line(y));
					const auto surf_line = std::bit_cast<uint32_t*>(surface->pixels_line(y));

					for (auto x = 1u; x < df::location_heat_map::map_width - 1; ++x)
					{
						const auto ym1 = ((y - 1) * df::location_heat_map::map_width);
						const auto y0 = (y * df::location_heat_map::map_width);
						const auto yp1 = ((y + 1) * df::location_heat_map::map_width);

						const auto v =
							summary.coordinates[ym1 + x - 1] +
							(summary.coordinates[ym1 + x] * 2) +
							summary.coordinates[ym1 + x + 1] +

							(summary.coordinates[y0 + x - 1] * 2) +
							(summary.coordinates[y0 + x] * 4) +
							(summary.coordinates[y0 + x + 1] * 2) +

							summary.coordinates[yp1 + x - 1] +
							(summary.coordinates[yp1 + x] * 2) +
							summary.coordinates[yp1 + x + 1];

						if (v)
						{
							const auto s = std::min(v, 4u) / 4.0f;
							const auto m = map_line[x];
							const auto c = ui::lerp(m, cc, s);
							surf_line[x] = c;
						}
						else
						{
							surf_line[x] = map_line[x];
						}
					}
				}

				_surface = std::move(surface);
			}

			_tex_invalid = true;
		}
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		if (is_valid(_surface))
		{
			if (!_tex)
			{
				const auto t = dc.create_texture();

				if (t)
				{
					_tex = t;
					_tex_invalid = true;
				}
			}

			if (_tex_invalid)
			{
				_tex->update(_surface);
				_tex_invalid = false;
			}

			const auto dims = _surface->dimensions();
			const auto cx = bounds.width();
			const auto cy = bounds.height();
			const auto xx = _hover_offset.cx + (dims.cx - cx) / 2;
			const auto yy = _hover_offset.cy + (dims.cy - cy) / 2;

			if (_tex)
			{
				dc.draw_texture(_tex, bounds.offset(element_offset), recti(xx, yy, xx + cx, yy + cy));
			}

			if (_hover_location >= 0 && _hover_location < static_cast<int>(_locations.size()))
			{
				const auto hover_bounds = center_rect(
					{8, 8}, bounds.top_left() + element_offset - pointi(xx, yy) + _locations[_hover_location].loc);
				dc.draw_rect(hover_bounds, ui::color(ui::style::color::important_background));
			}
		}
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		return {width_limit, default_height};
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, const pointi loc,
	                                             const pointi element_offset,
	                                             const std::vector<recti>& excluded_bounds) override
	{
		return default_controller_from_location(*this, host, loc, element_offset, excluded_bounds);
	}

	void hover(interaction_context& ic) override
	{
		if (_surface)
		{
			const auto logical_bounds = bounds.offset(ic.element_offset);
			const auto hovering = logical_bounds.contains(ic.loc);
			sizei new_hover_offset = {0, default_y_offset};
			int hover_location = -1;

			if (hovering)
			{
				const auto center = logical_bounds.center();
				const auto dims = _surface->dimensions();
				const auto cx = bounds.width();
				const auto cy = bounds.height();

				new_hover_offset.cx = df::mul_div(ic.loc.x - center.x, dims.cx - cx, cx);
				//new_hover_offset.cy = df::mul_div(ic.loc.y - center.y, dims.cy - cy, cy);

				const auto xx = new_hover_offset.cx + (dims.cx - cx) / 2;
				const auto yy = new_hover_offset.cy + (dims.cy - cy) / 2;
				const auto logical_loc = ic.loc + ic.element_offset - bounds.top_left() + pointi(xx, yy);
				//const auto less_location = [logical_loc](const location_group& left, const location_group& right) { return left.loc.dist_sqrd(logical_loc) < right.loc.dist_sqrd(logical_loc); };
				//const auto closest_location = std::min_element(_locations.begin(), _locations.end(), less_location);

				auto distance = INT_MAX;

				for (auto i = 0u; i < _locations.size(); i++)
				{
					const auto dd = _locations[i].loc.dist_sqrd(logical_loc);

					if (dd < distance)
					{
						distance = dd;
						hover_location = i;
					}
				}

				//hover_location = closest_location == _locations.end() ? -1 : std::distance(_locations.begin(), closest_location);
			}

			if (_hover_location != hover_location)
			{
				_hover_location = hover_location;
				ic.invalidate_view = true;
				_state.invalidate_view(view_invalid::tooltip);
			}

			if (new_hover_offset != _hover_offset)
			{
				_hover_offset = new_hover_offset;
				ic.invalidate_view = true;
			}
		}
	}

	void tooltip(view_hover_element& hover, const pointi loc, const pointi element_offset) const override
	{
		if (_hover_location != -1)
		{
			const auto& location = _locations[_hover_location];

			hover.elements.add(make_icon_element(icon_index::location, view_element_style::no_break));
			hover.elements.add(std::make_shared<text_element>(location.name, ui::style::font_size::title,
			                                                  ui::style::text_style::multiline,
			                                                  view_element_style::line_break));
			hover.elements.add(std::make_shared<text_element>(
				format(tt.click_items_from_fmt, location.count, location.name), ui::style::font_size::dialog,
				ui::style::text_style::multiline, view_element_style::line_break));
			hover.elements.add(std::make_shared<action_element>(tt.click_to_search));
		}

		hover.active_bounds = hover.window_bounds = bounds.offset(element_offset);
		hover.horizontal = true;
	}

	void dispatch_event(const view_element_event& event) override
	{
		if (event.type == view_element_event_type::invoke && _hover_location != -1)
		{
			const auto& loc = _locations[_hover_location];
			_state.open(event.host, df::search_t().with(loc.name), {});
		}
	}
};

class sidebar_logo_element final : public std::enable_shared_from_this<sidebar_logo_element>, public view_element
{
private:
	view_state& _state;
	std::u8string_view _text = s_app_name;
	ui::style::font_size _font = ui::style::font_size::title;
	ui::style::text_style _text_style = ui::style::text_style::single_line;

	mutable plasma logo_plasma;
	mutable ui::texture_ptr _plasma_tex;

	ui::const_surface_ptr _logo_surface;
	mutable ui::texture_ptr _logo_tex;

public:
	sidebar_logo_element(view_state& state) noexcept : view_element(
		                                                   view_element_style::has_tooltip |
		                                                   view_element_style::can_invoke), _state(state)
	{
		files ff;
		_logo_surface = ff.image_to_surface(load_resource(platform::resource_item::logo15));
	}

	void text(const std::u8string_view t)
	{
		_text = t;
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		const auto logical_bounds = bounds.offset(element_offset);
		render_background(dc, element_offset);

		auto logical_plasma_bounds = logical_bounds;
		logical_plasma_bounds.right = logical_plasma_bounds.left + logical_plasma_bounds.height();

		if (!_plasma_tex)
		{
			_plasma_tex = dc.create_texture();
		}

		if (_plasma_tex)
		{
			logo_plasma.render(_plasma_tex, logical_plasma_bounds.extent());
			dc.draw_texture(_plasma_tex, logical_plasma_bounds, logical_plasma_bounds.extent());
		}

		if (!_logo_tex)
		{
			const auto t = dc.create_texture();

			if (t)
			{
				_logo_tex = t;
				_logo_tex->update(_logo_surface);
			}
		}

		if (_logo_tex)
		{
			dc.draw_texture(_logo_tex, center_rect(_logo_tex->dimensions(), logical_plasma_bounds),
			                _logo_tex->dimensions(), dc.colors.alpha);
		}

		const auto plasma_border_clr = ui::color(0.25f, 0.25f, 0.25f, 1.0f);
		dc.draw_border(logical_plasma_bounds, logical_plasma_bounds.inflate(1), plasma_border_clr, plasma_border_clr);

		auto text_bounds = logical_bounds;
		text_bounds.left = logical_plasma_bounds.right + dc.component_snap;
		dc.draw_text(_text, text_bounds, _font, _text_style, ui::color(dc.colors.foreground, dc.colors.alpha), {});
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		return {width_limit, mc.text_line_height(_font) + mc.component_snap};
	}

	void dispatch_event(const view_element_event& event) override
	{
		if (event.type == view_element_event_type::invoke)
		{
			_state.invoke(commands::view_help);
		}
	}

	void tooltip(view_hover_element& hover, pointi loc, pointi element_offset) const override;

	view_controller_ptr controller_from_location(const view_host_ptr& host, const pointi loc,
	                                             const pointi element_offset,
	                                             const std::vector<recti>& excluded_bounds) override
	{
		return default_controller_from_location(*this, host, loc, element_offset, excluded_bounds);
	}

	void step_plasma()
	{
		logo_plasma.step();
	}

	bool plasma_is_active() const
	{
		return logo_plasma.is_active();
	}
};


class sidebar_host final : public std::enable_shared_from_this<sidebar_host>, public view_host
{
public:
	view_state& _state;
	view_scroller _scroller;
	ui::frame_ptr _frame;
	ui::control_frame_ptr _owner;
	bool _has_focus = false;
	bool _show_indexing_control = false;
	bool _is_detecting = false;
	int _last_indexing_perc = -1;


	std::shared_ptr<sidebar_logo_element> _title;
	std::shared_ptr<sidebar_indexing_element> _indexing_elements;
	std::shared_ptr<sidebar_file_type_element> _type_chart;
	std::shared_ptr<sidebar_history_element> _history_chart;
	std::shared_ptr<sidebar_map_element> _map;
	std::vector<search_item_ptr> _items;
	std::vector<drive_item_ptr> _drives;
	std::vector<view_element_ptr> _elements;

	sidebar_host(view_state& s) : _state(s)
	{
		_title = std::make_shared<sidebar_logo_element>(_state);

		_indexing_elements = std::make_shared<sidebar_indexing_element>(
			s, view_element_style::grow | view_element_style::no_vert_center | view_element_style::line_break |
			view_element_style::center | view_element_style::important);
		_indexing_elements->padding(8);

		_type_chart = std::make_shared<sidebar_file_type_element>(s);
		_history_chart = std::make_shared<sidebar_history_element>(s);

		files ff;
		const auto surface = ff.image_to_surface(load_resource(platform::resource_item::map_png));
		_map = std::make_shared<sidebar_map_element>(s, surface);

		_elements.emplace_back(_title);
		_elements.emplace_back(_type_chart);
		_elements.emplace_back(_history_chart);
		_elements.emplace_back(_map);
	}

	void init(const ui::control_frame_ptr& owner)
	{
		ui::frame_style fs;
		fs.can_focus = true;
		fs.colors = {
			ui::style::color::sidebar_background, ui::style::color::view_text,
			ui::style::color::view_selected_background
		};
		fs.timer_milliseconds = 1000 / 15;
		_frame = owner->create_frame(weak_from_this(), fs);
		_owner = owner;
	}

	void invalidate_file_type()
	{
		_frame->invalidate(_type_chart->bounds.offset(-_scroller.scroll_offset()));
	}

	void populate_file_types_and_dates()
	{
		const auto histograms = _state.item_index.histograms();
		_type_chart->populate(histograms._file_types);
		_history_chart->populate(histograms._dates);
		_frame->invalidate();
	}

	void hover_file_type(int id)
	{
		if (_type_chart->hover_file_type(id))
		{
			invalidate_file_type();
			_state.invalidate_view(view_invalid::tooltip);
		}
	}

	void update_content(std::vector<search_item_ptr> items, std::vector<drive_item_ptr> drives,
	                    std::vector<view_element_ptr> item_elements)
	{
		_items = std::move(items);
		_drives = std::move(drives);

		_elements.clear();
		_elements.emplace_back(_title);
		if (_show_indexing_control) _elements.emplace_back(_indexing_elements);
		if (setting.sidebar.show_total_items) _elements.emplace_back(_type_chart);
		if (setting.sidebar.show_history) _elements.emplace_back(_history_chart);
		if (setting.sidebar.show_world_map) _elements.emplace_back(_map);
		_elements.insert(_elements.end(), item_elements.begin(), item_elements.end());

		update_current_search();
		queue_update_predictions();
		layout();
	}

	void populate()
	{
		df::assert_true(ui::is_ui_thread());

		const auto histograms = _state.item_index.histograms();
		_type_chart->populate(histograms._file_types);
		_history_chart->populate(histograms._dates);
		_map->populate(histograms._locations, histograms._location_groups);

		search_items_by_key_t existing;

		for (const auto& i : _items)
		{
			existing[i->key] = i;
		}

		_state.queue_async(async_queue::sidebar, [t = shared_from_this(), &s = _state, existing]()
		{
			const search_item_factory f;
			std::vector<search_item_ptr> items;
			std::vector<drive_item_ptr> drives;
			std::vector<view_element_ptr> item_elements;
			std::unordered_set<search_item_ptr> added_elements;

			auto add_elements = [&items, &item_elements, &added_elements](
				const std::vector<search_item_ptr>& items_to_add)
			{
				if (!items_to_add.empty() && !item_elements.empty())
					item_elements.emplace_back(
						std::make_shared<divider_element>());

				for (const auto i : items_to_add)
				{
					if (!added_elements.contains(i))
					{
						added_elements.insert(i);
						items.push_back(i);
						item_elements.push_back(i);
					}
				}
			};

			if (setting.sidebar.show_indexed_folders)
			{
				add_elements(f.create_folder_items(s, existing));
			}

			if (setting.sidebar.show_favorite_searches)
			{
				add_elements(f.create_search_items(s, existing));
			}

			if (setting.sidebar.show_drives)
			{
				drives = f.create_drive_items(s, existing);
				if (!drives.empty() && !item_elements.empty())
					item_elements.emplace_back(
						std::make_shared<divider_element>());
				item_elements.insert(item_elements.end(), drives.begin(), drives.end());
			}

			if (setting.sidebar.show_ratings)
			{
				add_elements(f.create_ratings(s, existing));
			}

			if (setting.sidebar.show_labels)
			{
				add_elements(f.create_labels(s, existing));
			}

			if (setting.sidebar.show_favorite_tags)
			{
				add_elements(f.create_tags(s, existing));
			}

			df::trace(str::format(u8"Sidebar populate {} elements", item_elements.size()));

			s.queue_ui(
				[t, items = std::move(items), drives = std::move(drives), item_elements = std::move(item_elements)]
				{
					t->update_content(std::move(items), std::move(drives), std::move(item_elements));
				});
		});
	}

	void on_window_layout(ui::measure_context& mc, const sizei extent, const bool is_minimized) override
	{
		if (!is_minimized)
		{
			_extent = extent;
			layout(mc);
		}
	}

	void on_window_paint(ui::draw_context& dc) override
	{
		const auto offset = _scroller.scroll_offset();
		const auto logical_clip_bounds = dc.clip_bounds().offset(offset);

		for (const auto& e : _elements)
		{
			if (e->bounds.intersects(logical_clip_bounds))
			{
				e->render(dc, -offset);
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

		if (setting.show_debug_info && _active_controller)
		{
			const auto c = ui::color(1.0f, 0.0f, 0.0f, 1.0f);
			dc.draw_border(_controller_bounds, _controller_bounds.inflate(2), c, c);
		}
	}

	void update_current_search()
	{
		const auto current_search = _state.search();
		const auto current_search_text = current_search.text();

		for (const auto& i : _items)
		{
			const auto checked = i->search == current_search;

			if (i->is_style_bit_set(view_element_style::checked) != checked)
			{
				i->set_style_bit(view_element_style::checked, checked);
				_frame->invalidate();
			}
		}

		for (const auto& i : _drives)
		{
			const auto checked = i->_drive.name == current_search_text;

			if (i->is_style_bit_set(view_element_style::checked) != checked)
			{
				i->set_style_bit(view_element_style::checked, checked);
				_frame->invalidate();
			}
		}
	}

	void queue_update_predictions()
	{
		static std::atomic_int version;
		df::cancel_token token(version);

		_state.queue_async(async_queue::sidebar, [this, &s = _state, item_elements = _items, token]()
		{
			df::scope_locked_inc slc(df::jobs_running);

			int update_count = 0;

			for (const auto& i : item_elements)
			{
				if (i->calc_sum)
				{
					const auto sum = i->calc_sum(s, token);

					if (!token.is_cancelled())
					{
						update_count += 1;

						//if (i->summary != sum)
						{
							s.queue_ui([this, i, sum]()
							{
								i->summary = sum;
								_frame->invalidate();
							});
						}
					}
				}
			}

			df::trace(str::format(u8"Sidebar update {} predictions", update_count));
		});
	}

	view_controller_ptr controller_from_location(const pointi loc) override
	{
		if (_scroller.can_scroll() && _scroller.scroll_bounds().contains(loc))
		{
			return std::make_shared<scroll_controller>(shared_from_this(), _scroller, _scroller.scroll_bounds());
		}

		const auto offset = -_scroller.scroll_offset();

		for (const auto& e : _elements)
		{
			auto controller = e->controller_from_location(shared_from_this(), loc, offset, {});
			if (controller) return controller;
		}

		return nullptr;
	}

	void on_mouse_wheel(const pointi loc, const int delta, const ui::key_state keys, bool& was_handled) override
	{
		_scroller.offset(shared_from_this(), 0, -(delta / 2));
		was_handled = _scroller.can_scroll();
		update_controller(loc);
	}

	void layout()
	{
		if (_frame)
		{
			_frame->layout();
			_frame->invalidate();
		}
	}

	void invalidate()
	{
		if (_frame)
		{
			_frame->invalidate();
		}
	}

	void invalidate_element(const view_element_ptr& e) override
	{
		if (_frame)
		{
			_frame->invalidate();
		}
	}

	void layout(ui::measure_context& mc)
	{
		const auto x_padding = 4;
		const auto scroll_padding = _scroller.can_scroll() ? view_scroller::def_width : x_padding;
		const auto layout_padding = sizei{0, mc.baseline_snap};
		auto avail_bounds = recti(_extent);
		avail_bounds.left += x_padding; // -(mc.baseline_snap / 2);
		avail_bounds.right -= scroll_padding; // -(mc.baseline_snap / 2);

		ui::control_layouts positions;
		const auto y = stack_elements(mc, positions, avail_bounds, _elements, false, layout_padding);

		const recti scroll_bounds{_extent.cx - scroll_padding, 0, _extent.cx, _extent.cy};
		const recti client_bounds{0, 0, _extent.cx - scroll_padding, _extent.cy};
		_scroller.layout({client_bounds.width(), y + mc.component_snap}, client_bounds, scroll_bounds);
	}

	int prefered_width(ui::measure_context& mc)
	{
		const auto extent = mc.measure_text(u8"Documents", ui::style::font_size::dialog,
		                                    ui::style::text_style::single_line, 200);
		return df::mul_div(extent.cx, 5, 2);
	}

	void focus_changed(bool has_focus, const ui::control_base_ptr& child) override
	{
		df::trace(str::format(u8"Sidebar navigation_controls::focus {}", has_focus));

		_has_focus = has_focus;
		_frame->invalidate();
	}


	bool _is_active = true;

	void tick() override
	{
		if (_frame && _frame->is_visible())
		{
			if (_is_active || _title->plasma_is_active())
			{
				_frame->invalidate(_title->bounds.offset(-_scroller.scroll_offset()), false);
				_title->step_plasma();
			}

			const auto is_indexing = _state.item_index.indexing > 0;
			const auto is_detecting = _state.item_index.detecting > 0;

			if (_show_indexing_control != is_indexing)
			{
				_show_indexing_control = is_indexing;
				populate();
			}

			if (_is_detecting != is_detecting)
			{
				_is_detecting = is_detecting;
				_frame->invalidate(_indexing_elements->bounds);
			}

			if (_show_indexing_control)
			{
				const auto perc = calc_indexing_perc(_state.item_index.stats);

				if (_last_indexing_perc != perc)
				{
					_frame->invalidate(_indexing_elements->bounds);
				}
			}
		}
	}

	void activate(bool is_active) override
	{
	}

	bool key_down(const int c, const ui::key_state keys) override
	{
		return false;
	};

	const ui::frame_ptr frame() override
	{
		return _frame;
	}

	const ui::control_frame_ptr owner() override
	{
		return _owner;
	}

	void controller_changed() override
	{
		_state.invalidate_view(view_invalid::tooltip);
	}

	void invoke(const commands cmd) override
	{
		_state.invoke(cmd);
	}

	bool is_command_checked(const commands cmd) override
	{
		return _state.is_command_checked(cmd);
	}

	void track_menu(const recti bounds, const std::vector<ui::command_ptr>& commands) override
	{
		_state.track_menu(_frame, bounds, commands);
	}

	void invalidate_view(const view_invalid invalid) override
	{
		_state.invalidate_view(invalid);
	}

	void dpi_changed() override
	{
		const view_element_event ev{view_element_event_type::dpi_changed, shared_from_this()};

		for (const auto& e : _elements)
		{
			e->dispatch_event(ev);
		}

		if (_frame)
		{
			_frame->layout();
			_frame->invalidate();
		}
	}
};
