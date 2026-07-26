// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Sidebar navigation panel. Contains collection overview charts, folder list,
// favorite searches, tags, ratings, labels, and drive information displays.

#pragma once
#include "ui_controls.h"
#include "app_util.h"

struct plasma
{
	uint32_t _palette[256];
	std::unique_ptr<uint8_t, df::free_delete> _pixels = nullptr;

	int _fade = 0;
	int _stride = 0;
	bool _show_color = false;
	bool _hover = false;
	int _cosinus[256];

	constexpr static int fade_max = 40;
	constexpr static int _width = 96;
	constexpr static int _height = 96;

	plasma()
	{
		for (auto i = 0; i < 256; ++i)
		{
			_cosinus[i] = static_cast<int>(127.0 * cos(i * M_PI / 64.0) + 128.0);
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


	void step_plasma() const
	{
		static uint32_t p1 = 0, p2 = 0, p3 = 0, p4 = 0;

		uint8_t t1 = p1 / 2;
		uint8_t t2 = p2 / 2;

		for (auto y = 0; y < _height; ++y)
		{
			uint8_t t3 = p3 / 2;
			uint8_t t4 = p4 / 2;

			const auto t = _cosinus[t1] + _cosinus[t2];
			auto d = std::bit_cast<uint32_t*>(_pixels.get() + y * _stride);

			for (auto x = 0; x < _width; ++x)
			{
				*d++ = _palette[(t + _cosinus[t3++] + _cosinus[t4]) >> 2 & 0x000000ff];
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
		constexpr auto cd = fade_max * 3 * 0x24;

		for (int i = 0; i < 256; ++i)
		{
			const auto r = _cosinus[i];
			const auto g = _cosinus[i + 32 & 0x0ff];
			const auto b = _cosinus[i + 64 & 0x0ff];

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

	void render(const ui::texture_ptr& tex, const sizei dims) const
	{
		df::assert_true(dims.cx <= _width);
		df::assert_true(dims.cy <= _height);

		tex->update({_height, _width}, ui::texture_format::RGB, ui::orientation::top_left, _pixels.get(), _stride,
		            _height * _stride);
	}

	bool is_active() const
	{
		return _show_color || _fade < fade_max;
	}

	static int calc_stride(const int width, const int bpp) noexcept
	{
		const int bpp_width = width * bpp;
		return (bpp_width + (bpp_width % 8 ? 8 : 0)) / 8 + 3 & ~3;
	}
};


static std::string format_total_text(const df::count_and_size total, const bool multi_line)
{
	std::string result;

	if (total.count > 0)
	{
		result = str::format_count(total.count);
	}

	if (multi_line)
	{
		if (!total.size.is_empty())
		{
			result += "\n";
			result += prop::format_size(total.size);
		}
	}

	return result;
}

struct text_layout_state
{
	ui::text_layout_ptr tf;
	std::string text;
	ui::style::font_face font = ui::style::font_face::dialog;
	ui::style::text_style style = ui::style::text_style::multiline;
	std::string _built_text; // the value of `text` that tf was last built from

	void lazy_load(ui::measure_context& mc)
	{
		// Rebuild when tf is missing OR when `text` was reassigned directly (e.g. a
		// language change re-populates a reused item and updates .text without going
		// through the text-taking overload). Without this the stale glyph layout would
		// keep rendering the previous language until restart.
		if (!tf || _built_text != text)
		{
			tf = mc.create_text_layout(font);
			tf->update(text, style);
			_built_text = text;
		}
	}

	void lazy_load(ui::measure_context& mc, const std::string& text_in, const ui::style::text_style style_in,
	               const ui::style::font_face font_in = ui::style::font_face::dialog)
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
			_built_text = text;
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

struct sidebar_tooltip_thumbnail
{
	df::file_path path;
	df::item_element_ptr item;
	bool requested = false;

	void add(view_hover_element& hover, view_state& state, const df::file_path& representative_path)
	{
		if (representative_path.is_empty()) return;
		if (path != representative_path)
		{
			path = representative_path;
			item.reset();
			requested = false;
		}

		if (!item)
		{
			const auto indexed = state.item_index.find_item(path);
			if (indexed.ft) item = std::make_shared<df::item_element>(path, indexed);
		}
		if (!item) return;

		const auto thumbnail = item->thumbnail();
		if (is_valid(thumbnail))
		{
			files file_loader;
			hover.elements->add(std::make_shared<surface_element>(
				file_loader.image_to_surface(thumbnail), 160,
				flex_item::center | flex_item::new_line, item->layout_orientation()));
		}
		else if (!requested)
		{
			requested = true;
			state.item_index.queue_load_thumbnail(item);
		}
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
	std::string tooltip_text;
	std::string key;
	df::search_t search;
	std::function<df::file_group_histogram(view_state& s, df::cancel_token token)> calc_sum;
	df::file_group_histogram summary;
	ui::color32 clr = 0;
	int icon_repeat = 1;
	mutable sidebar_tooltip_thumbnail _tooltip_thumbnail;

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
			                      ui::style::font_face::icons);
			dc.draw_text(icon_layout.tf, r, draw_clr, {});
			x += cx + dc.padding1;
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
		auto cy = mc.text_line_height(ui::style::font_face::dialog);
		auto cx = width_limit;
		const auto total_text = format_total_text(summary.total_items(), false);

		if (icon != icon_index::none)
		{
			cx -= mc.icon_cxy + mc.padding1;
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
			result.elements->add(make_icon_element(tooltip_icon, icon_repeat, flex_item::line_break));
		}
		else
		{
			result.elements->add(make_icon_element(tooltip_icon, icon_repeat, flex_item::no_break));
			result.elements->add(std::make_shared<text_element>(title_layout.text, ui::style::font_face::title,
			                                                    ui::style::text_style::multiline,
			                                                    flex_item::line_break));
		}

		_tooltip_thumbnail.add(result, _state, summary.representative_path);

		if (str::is_empty(tooltip_text))
		{
			result.elements->add(std::make_shared<text_element>(search.text(), ui::style::font_face::dialog,
			                                                    ui::style::text_style::multiline,
			                                                    flex_item::new_line));
		}
		else
		{
			result.elements->add(std::make_shared<text_element>(tooltip_text, ui::style::font_face::dialog,
			                                                    ui::style::text_style::multiline,
			                                                    flex_item::new_line));
		}

		result.elements->add(std::make_shared<summary_control>(summary, flex_item::new_line));
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
	const int _graph_height = 8;

	explicit sidebar_drive_element(view_state& state, const platform::drive_t& d) noexcept
		: view_element(view_element_style::has_tooltip | view_element_style::can_invoke), _state(state), _drive(d)
	{
		if (!str::is_empty(_drive.vol_name))
		{
			title_layout.text = std::format("{} ({})", _drive.name, _drive.vol_name);
		}
		else
		{
			title_layout.text = _drive.name;
		}

		icon = drive_icon(_drive.type);
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		const auto logical_bounds = bounds.offset(element_offset);
		render_background(dc, element_offset);

		const auto pad1 = df::round(1 * dc.scale_factor);
		const auto graph_height = df::round(_graph_height * dc.scale_factor);
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
			                      ui::style::font_face::icons);
			dc.draw_text(icon_layout.tf, r, draw_clr, {});
			x += dc.icon_cxy + dc.padding1;
		}

		auto text_bounds = logical_bounds;
		text_bounds.left = x;
		text_bounds.bottom -= graph_height + dc.padding1;

		auto graph_bounds = logical_bounds;
		graph_bounds.top = text_bounds.bottom + dc.padding1;

		dc.draw_rect(graph_bounds, ui::color(ui::style::color::sidebar_background, draw_clr.a).scale(1.22f));

		// Capacity is zero for a drive with no media inserted; there is no usage to plot.
		if (!_drive.capacity.is_empty())
		{
			auto used_bounds = graph_bounds.inflate(-pad1);
			used_bounds.right = used_bounds.left + static_cast<int>(df::mul_div(
				static_cast<int64_t>(used_bounds.width()), static_cast<int64_t>(_drive.used.to_int64()),
				static_cast<int64_t>(_drive.capacity.to_int64())));

			dc.draw_rect(used_bounds,
			             ui::color(
				             ui::average(ui::style::color::sidebar_background, ui::style::color::important_background),
				             draw_clr.a));
		}

		const auto total_text = std::format("{}|{}", prop::format_size(_drive.used),
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
		const auto cy = mc.text_line_height(ui::style::font_face::dialog);
		const auto graph_height = df::round(_graph_height * mc.scale_factor);
		return {width_limit, cy + graph_height + mc.padding1};
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, const pointi loc,
	                                             const pointi element_offset,
	                                             const std::vector<recti>& excluded_bounds) override
	{
		return default_controller_from_location(*this, host, loc, element_offset, excluded_bounds);
	}

	void tooltip(view_hover_element& result, const pointi loc, const pointi element_offset) const override
	{
		result.elements->add(make_icon_element(icon, flex_item::no_break));
		result.elements->add(std::make_shared<text_element>(_drive.name, ui::style::font_face::title,
		                                                    ui::style::text_style::multiline,
		                                                    flex_item::line_break));


		const auto table = std::make_shared<ui::table_element>(flex_item::center);

		if (!str::is_empty(_drive.vol_name))
		{
			table->add(tt.disk_label, _drive.vol_name);
		}

		table->add(tt.disk_capacity, _drive.capacity.str());
		table->add(tt_prep(tt.disk_free), _drive.free.str());
		table->add(tt.disk_used, _drive.used.str());
		table->add(tt.disk_system, _drive.file_system);

		result.elements->add(table);

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
// Values only. The sidebar is rebuilt on a worker while the UI thread is still rendering the
// current elements, so the worker must never see a pointer to a live element. It carries the
// last known summary per key so a rebuilt row can show a count before its own sum is computed.
using search_items_by_key_t = df::hash_map<std::string, df::file_group_histogram, df::ihash, df::ieq>;

class search_item_factory
{
public:
	static icon_index calc_folder_icon(const df::folder_path path)
	{
		const auto path_text = path.text();
		if (platform::is_server(path_text) || path.is_unc_path()) return icon_index::network;
		if (contains(path_text, "onedrive") || contains(path_text, tt.folder_onedrive)) return icon_index::cloud;
		if (contains(path_text, "picture") || contains(path_text, tt.folder_picture)) return icon_index::photo;
		if (contains(path_text, "video") || contains(path_text, tt.folder_video)) return icon_index::video;
		if (contains(path_text, "music") || contains(path_text, tt.folder_music)) return icon_index::audio;
		return icon_index::folder;
	}

	std::vector<search_item_ptr> create_folder_items(view_state& s, const search_items_by_key_t& existing) const
	{
		std::vector<search_item_ptr> results;

		for (auto folder : s.item_index.index_roots().folders)
		{
			auto key = std::format("f:{}", folder);
			auto i = create_or_find_item(s, existing, key);
			i->tooltip_icon = i->icon = calc_folder_icon(folder);
			i->title_layout.text = folder.name();
			i->search = df::search_t().add_selector(df::item_selector(folder));
			i->calc_sum = [folder](const view_state& s, const df::cancel_token token)
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

	static std::vector<drive_item_ptr> create_drive_items(view_state& s, const search_items_by_key_t& existing)
	{
		const auto drives = platform::scan_drives();

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
				auto item = create_or_find_item(s, existing, "s:"s + path);

				item->tooltip_icon = item->icon = search.has_selector() ? icon_index::folder : icon_index::search;
				item->title_layout.text = tt.translate_text(title);
				item->search = search;
				item->calc_sum = [search](const view_state& s, const df::cancel_token token)
				{
					return s.item_index.count_matches(search, token);
				};
				results.emplace_back(item);
			}
		}

		results.emplace_back(create_item(s, existing, "@duplicates"s, icon_index::compare, tt.duplicates,
		                                 tt.duplicates_tooltip, 0));

		return results;
	}

	void compact_summary(index_state::distinct_results& summary) const
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

		index_state::distinct_results tags;

		if (!setting.sidebar.show_favorite_tags_only)
		{
			tags = s.item_index.distinct_tags();
		}

		str::split2(setting.favorite_tags, true, [&tags](std::string_view part)
		{
			tags.emplace_back(part, df::file_group_histogram{});
		});

		compact_summary(tags);

		for (const auto& t : tags)
		{
			auto tag = std::string(t.first);
			auto search = df::search_t().with(prop::tag, tag);

			if (!str::is_empty(tag) && !search.is_empty())
			{
				auto key = std::format("t:{}", tag);
				auto i = create_or_find_item(s, existing, key);

				i->tooltip_icon = icon_index::tag;
				i->title_layout.text = tag;
				i->search = search;
				i->calc_sum = [tag](const view_state& s, df::cancel_token token)
				{
					return s.item_index.tag_summary(tag);
				};
				i->summary = t.second;
				results.emplace_back(i);
			}
		}

		return results;
	}

	search_item_ptr create_rating_item(view_state& s, const search_items_by_key_t& existing, const std::string& key,
	                                   const int rating) const
	{
		auto i = create_or_find_item(s, existing, key);
		i->tooltip_icon = i->icon = icon_index::star_solid;
		i->icon_repeat = rating;
		i->search = df::search_t::parse(key);
		i->calc_sum = [rating](const view_state& s, df::cancel_token token)
		{
			return s.item_index.rating_summary(rating);
		};
		i->summary = s.item_index.rating_summary(rating);
		return i;
	}

	search_item_ptr create_item(view_state& s, const search_items_by_key_t& existing, const std::string& key,
	                            const icon_index icon, const text_t& name, const text_t& tooltip,
	                            const ui::color32 clr = 0) const
	{
		auto i = create_or_find_item(s, existing, key);
		i->tooltip_icon = i->icon = icon;
		i->clr = clr;
		i->title_layout.text = name;
		i->tooltip_text = tooltip;
		i->search = df::search_t::parse(key);
		i->calc_sum = [search = i->search](const view_state& s, const df::cancel_token token)
		{
			return s.item_index.count_matches(search, token);
		};
		return i;
	}

	std::vector<search_item_ptr> create_ratings(view_state& s, const search_items_by_key_t& existing) const
	{
		std::vector<search_item_ptr> results;
		results.emplace_back(create_rating_item(s, existing, "rating:5"s, 5));
		results.emplace_back(create_rating_item(s, existing, "rating:4"s, 4));
		results.emplace_back(create_rating_item(s, existing, "rating:3"s, 3));
		results.emplace_back(create_rating_item(s, existing, "rating:2"s, 2));
		results.emplace_back(create_rating_item(s, existing, "rating:1"s, 1));

		results.emplace_back(create_item(s, existing, "rating:-1"s, icon_index::cancel, tt.command_rate_rejected, {},
		                                 color_rate_rejected));
		return results;
	}

	std::vector<search_item_ptr> create_labels(view_state& s, const search_items_by_key_t& existing) const
	{
		std::vector<search_item_ptr> results;

		auto labels = s.item_index.distinct_labels();
		labels.emplace_back("select", df::file_group_histogram{});
		labels.emplace_back("second", df::file_group_histogram{});
		labels.emplace_back("approved", df::file_group_histogram{});
		labels.emplace_back("review", df::file_group_histogram{});
		labels.emplace_back("to do", df::file_group_histogram{});

		compact_summary(labels);

		for (const auto& lab : labels)
		{
			auto label = std::string(lab.first);
			auto key = std::format("l:{}", label);
			auto search = df::search_t().with(prop::label, label);

			if (!str::is_empty(label) && !search.is_empty())
			{
				const auto* const def = find_rate_label_def(label);
				auto i = create_or_find_item(s, existing, key);
				i->tooltip_icon = i->icon = def ? def->icon : icon_index::flag;
				i->clr = def ? def->clr : 0;
				i->title_layout.text = prop::format_label(label);
				i->tooltip_text = {};
				i->search = search;
				i->calc_sum = [label](const view_state& s, df::cancel_token token)
				{
					return s.item_index.label_summary(label);
				};
				i->summary = lab.second;
				results.emplace_back(i);
			}
		}

		return results;
	}


	static search_item_ptr create_or_find_item(view_state& s, const search_items_by_key_t& existing,
	                                           const std::string& key)
	{
		auto result = std::make_shared<sidebar_element>(s);
		result->key = key;

		const auto found = existing.find(key);
		if (found != existing.end()) result->summary = found->second;

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
	ui::style::font_face _font = ui::style::font_face::title;
	view_state& _s;

public:
	sidebar_indexing_element(view_state& s, const view_element_options& style_in) noexcept :
		view_element(
			style_in | view_element_style::can_invoke | view_element_style::has_tooltip |
			view_element_style::important), _s(s)
	{
		update_background_color();
	}

	std::string format_text() const
	{
		return std::format("{} {}%", tt.indexing, calc_indexing_perc(_s.item_index.stats));
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
		hover.elements->add(std::make_shared<text_element>(tt.indexing_message, ui::style::font_face::dialog,
		                                                   ui::style::text_style::multiline,
		                                                   flex_item::line_break));
		hover.elements->add(std::make_shared<action_element>(tt.command_collection_options));

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

constexpr int sidebar_visualization_size = 220;

class sidebar_file_type_element final : public view_element,
                                        public std::enable_shared_from_this<sidebar_file_type_element>
{
public:
	bool _center_hover = false;
	mutable bool _pie_invalid = true;
	std::string _text;
	view_state& _state;

	struct pie_chart_entry
	{
		int id = 0;
		uint64_t count = 0;
		df::file_size size;
		file_group_ref group = file_group::other;
		bool focus = false;
		double end_rad = 0.0;
	};

	static constexpr int chart_segment_count = 64;
	std::array<pie_chart_entry, chart_segment_count> _file_type_entries;

	sidebar_file_type_element(view_state& state) noexcept : view_element(
		                                                        view_element_style::has_tooltip |
		                                                        view_element_style::can_invoke |
		                                                        flex_item::center), _state(state)
	{
	}

	void populate(const df::file_group_histogram& summary)
	{
		struct group_count
		{
			int64_t count_sr;
			int64_t count;
			df::file_size size;
			file_group_ref group;
		};

		std::vector<group_count> counts;

		for (auto i = 0; i < file_group::max_count; ++i)
		{
			const auto c = summary.counts[i];
			counts.emplace_back(df::round(std::cbrt(static_cast<double>(c.count))), c.count, c.size,
			                    file_group_from_index(i));
		}

		std::ranges::sort(counts, [](auto&& left, auto&& right) { return left.count_sr < right.count_sr; });

		auto current_segment = 0; /*

		for (auto && e : _file_type_entries)
		{
			e.group = file_group::other;
			e.focus = false;
			e.clr = 0;
		}*/

		for (auto i = 0; i < file_group::max_count; ++i)
		{
			const auto group = counts[i].group;
			const auto count_sr = counts[i].count_sr;

			if (count_sr != 0)
			{
				auto remaining_count_total = 0ll;
				const auto remaining_segments = static_cast<uint64_t>(chart_segment_count - current_segment);
				const auto is_last = i == file_group::max_count - 1;

				for (auto j = i; j < file_group::max_count; ++j)
					remaining_count_total += counts[j].count_sr;

				const auto segments = std::max(1ll, df::mul_div(remaining_segments, count_sr, remaining_count_total));

				for (int k = 0; (k < segments || is_last) && current_segment < chart_segment_count; k++)
				{
					auto&& e = _file_type_entries[current_segment];
					e.group = group;
					e.count = counts[i].count;
					e.size = counts[i].size;
					e.focus = false;
					e.id = current_segment;

					current_segment += 1;
				}
			}
		}

		/*for (auto i = 1; i < file_group::max_count; ++i)
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
		*/

		double start = -M_PI;

		for (auto&& e : _file_type_entries)
		{
			start = e.end_rad = start + 2.0 * M_PI * (1.0 / chart_segment_count);
		}

		const auto total_items = summary.total_items();
		_text = format_total_text(total_items, true);
		_pie_invalid = true;
	}

	int file_type_id_from_angle(const double rads) const
	{
		for (const auto& e : _file_type_entries)
		{
			if (rads <= e.end_rad)
			{
				return e.id;
			}
		}

		return -1;
	}

	bool hover_file_type(const int id)
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
			const auto text = str_format(tt.total_title.sv(), num, size, num_folder);

			hover.elements->add(std::make_shared<text_element>(tt.collection_title, ui::style::font_face::title,
			                                                   ui::style::text_style::multiline,
			                                                   flex_item::line_break |
			                                                   flex_item::center));
			hover.elements->add(std::make_shared<text_element>(text, ui::style::font_face::dialog,
			                                                   ui::style::text_style::multiline,
			                                                   flex_item::line_break |
			                                                   flex_item::center));

			hover.elements->add(std::make_shared<divider_element>());
			hover.elements->add(
				std::make_shared<summary_control>(
					file_types, flex_item::line_break | flex_item::center));
			hover.elements->add(std::make_shared<divider_element>());

			const auto table = std::make_shared<ui::table_element>(flex_item::center);
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

			hover.elements->add(table);
			hover.elements->add(std::make_shared<divider_element>());
			hover.elements->add(std::make_shared<action_element>(tt.command_collection_options));

			hover.preferred_size = view_hover_element::default_preferred_size + 64;
		}
		else
		{
			for (const auto& e : _file_type_entries)
			{
				if (e.focus)
				{
					const auto ft = e.group;
					const auto icon = ft->icon;
					const auto num = platform::format_number(str::to_string(e.count));
					const auto size = prop::format_size(e.size);

					hover.elements->add(make_icon_element(icon, flex_item::no_break));
					hover.elements->add(std::make_shared<text_element>(ft->display_name(e.count > 1),
					                                                   ui::style::font_face::title,
					                                                   ui::style::text_style::multiline,
					                                                   flex_item::line_break));

					const auto text = str_format(tt.collection_contains.sv(), num, ft->display_name(e.count > 1), size);
					hover.elements->add(std::make_shared<text_element>(text, ui::style::font_face::dialog,
					                                                   ui::style::text_style::multiline,
					                                                   flex_item::line_break));
				}
			}

			if (!hover.elements->is_empty())
			{
				hover.preferred_size = df::mul_div(view_hover_element::default_preferred_size, 2, 3);
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
			const auto dd = std::sqrt(dy * dy + dx * dx);
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
						_state.open(event.host, df::search_t().add_media_type(e.group), {});
						break;
					}
				}
			}
		}
	}

	mutable ui::texture_ptr _tex;

	static void draw_draw_pie_chart(const ui::texture_ptr& t, const sizei dims,
	                                const std::array<pie_chart_entry, chart_segment_count>& entries,
	                                const ui::color32& center_clr)
	{
		ui::color32 colors[chart_segment_count];

		for (int i = 0; i < chart_segment_count; ++i)
		{
			const auto rad = i * M_PI / 32.0 - M_PI;
			const auto& e = entries[i];
			const auto rgb = e.group->color;

			const auto color = ui::abgr(rgb);
			colors[i] = e.focus ? ui::lighten(color, 0.11f) : color;
		}

		const pointi center = {dims.cx / 2, dims.cy / 2};
		const int radius = std::min(dims.cx / 2, dims.cy / 2) - 1;

		const auto s = std::make_shared<ui::surface>();
		s->alloc(dims.cx, dims.cy, ui::texture_format::ARGB);
		s->fill_pie(center, radius, colors, center_clr == 0 ? 0 : ui::abgr(center_clr), 0);
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
			auto center_clr = ui::color32{};

			const auto tracking = is_style_bit_set(view_element_style::tracking);
			const auto hover = is_style_bit_set(view_element_style::hover);
			const auto selected = is_style_bit_set(view_element_style::selected);

			if (_center_hover)
			{
				center_clr = view_handle_color(selected, hover || _center_hover, tracking, dc.frame_has_focus, true).rgba();
			}

			const auto is_detecting = _state.item_index.detecting > 0;

			if (is_detecting)
			{
				center_clr = ui::style::color::important_background;
			}

			draw_draw_pie_chart(_tex, logical_bounds.extent(), _file_type_entries, center_clr);
			_pie_invalid = false;
		}

		const auto clr = ui::color(dc.colors.foreground, dc.colors.alpha);
		const auto center = logical_bounds.center();

		dc.draw_texture(_tex, center_rect(_tex->dimensions(), center));
		dc.draw_text(_text, logical_bounds, ui::style::font_face::dialog, ui::style::text_style::multiline_center, clr,
		             {});
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		const auto size = std::min(width_limit, df::round(sidebar_visualization_size * mc.scale_factor));
		return {size, size};
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

struct sidebar_history_element final : view_element, std::enable_shared_from_this<sidebar_history_element>
{
	static constexpr int col_count = 12;
	static constexpr int max_row_count = df::max_history_years;
	static constexpr int invalid_hover_month = -1;
	static constexpr int base_row_height = 10;
	static constexpr int two_year_min_width = 288;
	static constexpr int base_year_gap = 8;

	view_state& _state;
	std::array<double, col_count * max_row_count> dates{};
	std::array<df::date_counts, col_count * max_row_count> _counts{};
	std::array<df::file_path, col_count * max_row_count> _representative_paths{};
	mutable sidebar_tooltip_thumbnail _tooltip_thumbnail;

	double _min_val = 0.0;
	double _max_val = 0.0;
	int _hover_month = invalid_hover_month;
	int _current_year = 0;
	int _current_month = 0;
	// Number of years actually shown, derived from the configured start year.
	int _year_count = 10;
	mutable int row_height = base_row_height;
	mutable int _years_per_row = 1;
	mutable int _year_gap = 0;

	sidebar_history_element(view_state& state) noexcept : view_element(
		                                                      view_element_style::has_tooltip |
		                                                      view_element_style::can_invoke), _state(state)
	{
		populate({}); // Set some defaults
	}

	void populate(const df::date_histogram& summary)
	{
		const auto now = platform::now().date();
		const auto start_year = setting.sidebar.history_start_year;
		_year_count = df::history_year_count(start_year, now.year);

		_min_val = std::numeric_limits<double>::max();
		_max_val = 0.0;
		_counts = summary.dates;
		_representative_paths = summary.representative_paths;

		// Only the visible rows contribute to the min/max used for contrast.
		const auto shown = std::min(static_cast<size_t>(_year_count) * col_count, summary.dates.size());

		for (auto i = 0u; i < shown; i++)
		{
			const auto val = std::cbrt(summary.dates[i].created);
			dates[i] = val;
			if (_min_val > val) _min_val = val;
			if (_max_val < val) _max_val = val;
		}

		if (_min_val > _max_val) _min_val = _max_val; // no visible data

		_current_year = now.year;
		_current_month = now.month;
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		const auto logical_bounds = bounds.offset(element_offset);
		//render_background(dc, element_offset, clr);

		for (auto y = 0; y < _year_count; y++)
		{
			const auto display_row = y / _years_per_row;
			const auto yy1 = logical_bounds.top + display_row * row_height;
			const auto yy2 = logical_bounds.top + (display_row + 1) * row_height;

			if (yy2 < logical_bounds.bottom)
			{
				for (auto m = 0; m < col_count; m++)
				{
					const auto val = dates[y * 12 + m];
					const auto scale = std::max(df::round((val - _min_val) * 255.0 / (_max_val - _min_val)), 5);
					const auto cell = month_bounds(logical_bounds, y, m);
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
			dc.draw_rect(month_bounds(logical_bounds, y, m),
			             ui::color(ui::style::color::important_background, dc.colors.alpha));
		}
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		row_height = df::round(mc.scale_factor * base_row_height);
		_years_per_row = width_limit >= df::round(mc.scale_factor * two_year_min_width) ? 2 : 1;
		_year_gap = _years_per_row == 2 ? df::round(mc.scale_factor * base_year_gap) : 0;
		return {width_limit, df::history_row_count(_year_count, _years_per_row) * row_height + 1};
	}

	recti month_bounds(const recti logical_bounds, const int year, const int month) const
	{
		const auto display_row = year / _years_per_row;
		const auto section = year % _years_per_row;
		const auto section_width = (logical_bounds.width() - _year_gap) / _years_per_row;
		const auto section_left = logical_bounds.left + section * (section_width + _year_gap);
		return {
			section_left + df::mul_div(month, section_width, col_count),
			logical_bounds.top + display_row * row_height,
			section_left + df::mul_div(month + 1, section_width, col_count),
			logical_bounds.top + (display_row + 1) * row_height
		};
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
			const auto date_index = y * 12 + m;

			hover.elements->add(make_icon_element(icon_index::time, flex_item::no_break));
			hover.elements->add(std::make_shared<text_element>(month, ui::style::font_face::title,
			                                                   ui::style::text_style::multiline,
			                                                   flex_item::no_break));
			hover.elements->add(std::make_shared<text_element>(year, ui::style::font_face::title,
			                                                   ui::style::text_style::multiline,
			                                                   flex_item::line_break));
			_tooltip_thumbnail.add(hover, _state, _representative_paths[date_index]);
			hover.elements->add(std::make_shared<text_element>(tt.collection_contains2, ui::style::font_face::dialog,
			                                                   ui::style::text_style::multiline,
			                                                   flex_item::new_line));

			const auto table = std::make_shared<ui::table_element>(flex_item::center);
			const auto num1 = std::make_shared<text_element>(
				platform::format_number(str::to_string(date_count.created)),
				ui::style::text_style::single_line_far);
			const auto num2 = std::make_shared<text_element>(
				platform::format_number(str::to_string(date_count.modified)),
				ui::style::text_style::single_line_far);
			table->add(num1, std::make_shared<text_element>(str_format(tt.items_created_fmt.sv(), month, year)));
			table->add(num2, std::make_shared<text_element>(str_format(tt.items_modified_fmt.sv(), month, year)));
			hover.elements->add(table);

			hover.elements->add(std::make_shared<action_element>(tt.open_created_modified));
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
			const auto display_row = (ic.loc.y - logical_bounds.top) / row_height;
			const auto section_width = (logical_bounds.width() - _year_gap) / _years_per_row;
			const auto section_stride = section_width + _year_gap;
			const auto x = ic.loc.x - logical_bounds.left;
			const auto section = std::clamp(x / section_stride, 0, _years_per_row - 1);
			const auto section_x = x - section * section_stride;
			const auto year = display_row * _years_per_row + section;

			if (section_x >= 0 && section_x < section_width && year < _year_count)
			{
				const auto month = std::clamp(section_x * col_count / section_width, 0, col_count - 1);
				if (year > 0 || month < _current_month)
				{
					new_hover_month = (year << 8) + month;
				}
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
	view_state& _state;

	ui::const_surface_ptr _surface;
	ui::const_surface_ptr _surface_original;
	mutable ui::texture_ptr _tex;

	mutable bool _tex_invalid = true;
	recti _source_bounds;
	int _hover_source_left = -1;
	df::location_heat_map _summary;

	std::vector<map_location_area> _locations;
	df::hash_map<uint32_t, map_location_area> _resolved_areas;
	struct representative_item
	{
		df::file_path path;
		df::item_element_ptr item;
	};
	mutable df::hash_map<uint32_t, representative_item> _representatives;
	mutable df::unique_paths _thumbnail_requests;
	int _hover_location = -1;

	// Issue #119: the display language bit that _resolved_areas / _locations names were resolved
	// under. When the UI language changes the cached place names must be dropped and re-resolved,
	// otherwise the map keeps showing the previous language's names until the collection changes.
	int _resolved_lang_bit = -1;

	// Naming an area reads the gazetteer, so it runs on the location worker. These track the
	// requests in flight and make any answer that arrives after a language change discardable.
	df::hash_set<uint32_t> _resolving_areas;
	uint32_t _resolved_generation = 0;

	static constexpr int min_longitude_span = 90;
	static constexpr int min_latitude_span = 60;

	recti location_to_source(const map_location_area& location) const
	{
		const auto dims = _surface_original->dimensions();
		return recti(
			df::mul_div(location.cell.x, dims.cx, df::location_heat_map::map_width),
			df::mul_div(location.cell.y, dims.cy, df::location_heat_map::map_height),
			df::mul_div(location.cell.x + location.cell_span, dims.cx, df::location_heat_map::map_width),
			df::mul_div(location.cell.y + location.cell_span, dims.cy, df::location_heat_map::map_height));
	}

	void update_source_bounds()
	{
		const auto dims = _surface_original->dimensions();
		_source_bounds = recti(0, 0, dims.cx, dims.cy);
		if (_locations.empty()) return;

		auto left = dims.cx - 1;
		auto top = dims.cy - 1;
		auto right = 0;
		auto bottom = 0;

		for (const auto& location : _locations)
		{
			const auto source_loc = location_to_source(location);
			left = std::min(left, source_loc.left);
			top = std::min(top, source_loc.top);
			right = std::max(right, source_loc.right);
			bottom = std::max(bottom, source_loc.bottom);
		}

		const auto min_width = df::mul_div(dims.cx, min_longitude_span, 360);
		const auto min_height = df::mul_div(dims.cy, min_latitude_span, 180);
		auto width = std::max(right - left, min_width);
		auto height = std::max(bottom - top, min_height);
		width += std::max(width / 3, 8);
		height += std::max(height / 3, 8);
		width = std::min(width, dims.cx);
		height = std::min(height, dims.cy);

		const auto center_x = (left + right) / 2;
		const auto center_y = (top + bottom) / 2;
		left = std::clamp(center_x - width / 2, 0, dims.cx - width);
		top = std::clamp(center_y - height / 2, 0, dims.cy - height);
		_source_bounds = recti(left, top, left + width, top + height);
	}

	recti view_source_bounds() const
	{
		auto width = _source_bounds.width();
		if (bounds.height() > 0)
		{
			width = std::max(1, std::min(width,
				df::mul_div(_source_bounds.height(), bounds.width(), bounds.height())));
		}

		const auto max_left = _surface_original->dimensions().cx - width;
		const auto centered_left = std::clamp(_source_bounds.center().x - width / 2, 0, max_left);
		const auto left = _hover_source_left == -1 ? centered_left : std::clamp(_hover_source_left, 0, max_left);
		return recti(left, _source_bounds.top, left + width, _source_bounds.bottom);
	}

	recti location_to_view(const map_location_area& location, const pointi element_offset) const
	{
		const auto source_bounds = view_source_bounds();
		const auto source_loc = location_to_source(location);
		return recti(
			bounds.left + element_offset.x +
			df::mul_div(source_loc.left - source_bounds.left, bounds.width(), source_bounds.width()),
			bounds.top + element_offset.y +
			df::mul_div(source_loc.top - source_bounds.top, bounds.height(), source_bounds.height()),
			bounds.left + element_offset.x +
			df::mul_div(source_loc.right - source_bounds.left, bounds.width(), source_bounds.width()),
			bounds.top + element_offset.y +
			df::mul_div(source_loc.bottom - source_bounds.top, bounds.height(), source_bounds.height()));
	}

	static std::string display_name(const map_location_area& location)
	{
		return location.name;
	}

	static uint32_t area_key(const map_location_area& area)
	{
		return static_cast<uint32_t>(area.cell.x) |
			(static_cast<uint32_t>(area.cell.y) << 8) |
			(static_cast<uint32_t>(std::countr_zero(static_cast<unsigned>(area.cell_span))) << 16);
	}

	static uint32_t area_key(const pointi cell, const int cell_span)
	{
		return static_cast<uint32_t>(cell.x) |
			(static_cast<uint32_t>(cell.y) << 8) |
			(static_cast<uint32_t>(std::countr_zero(static_cast<unsigned>(cell_span))) << 16);
	}

	// Issue #119: drop cached place names when the UI display language changes so the map
	// re-resolves them in the new language instead of showing the previous one.
	void invalidate_stale_language()
	{
		const auto lang_bit = _state.item_index.locations().display_language_bit();
		if (lang_bit == _resolved_lang_bit) return;
		_resolved_lang_bit = lang_bit;
		_resolved_areas.clear();
		_resolving_areas.clear();
		++_resolved_generation;
		for (auto& location : _locations) location.name.clear();
	}

	// The gazetteer lookups this needs read a file, so they never run here. The answer is memoized;
	// a miss queues the work and the published result invalidates the tooltip that asked.
	void resolve_area(map_location_area& area)
	{
		df::assert_true(ui::is_ui_thread());
		invalidate_stale_language();

		if (!area.name.empty()) return;

		const auto key = area_key(area);
		if (const auto found = _resolved_areas.find(key);
			found != _resolved_areas.end() && found->second.has_same_photo_bounds(area))
		{
			const auto count = area.count;
			area = found->second;
			area.count = count;
			return;
		}

		if (!_resolving_areas.emplace(key).second) return;

		_state.queue_location([weak = std::weak_ptr(shared_from_this()), &state = _state, key,
				generation = _resolved_generation, request = area](const location_cache& locations)
			{
				auto selected = locations.find_largest(request.min_latitude, request.min_longitude,
				                                       request.max_latitude, request.max_longitude);
				if (selected.id == 0)
				{
					selected = locations.find_closest(request.position.latitude(),
					                                  request.position.longitude());
				}

				// locations.md 5.1 + 2.5: a place may only name the area from within its attribution
				// radius, so a distant city is never borrowed as the answer for an empty region.
				if (selected.id != 0 && selected.position.is_valid() && request.position.is_valid() &&
					selected.position.distance_in_kilometers(request.position) >
					location_attribution_radius_km(selected.population) * 3.0)
				{
					selected = {};
				}

				auto resolved = request;
				resolved.name = selected.id == 0
					                ? std::string(tt.location_remote.sv())
					                : qualified_name(selected);
				resolved.state = selected.state;
				resolved.country = selected.country;
				resolved.population = selected.population;
				resolved.place_position = selected.id == 0 ? gps_coordinate{} : selected.position;

				state.queue_ui([weak, key, generation, resolved = std::move(resolved)]
				{
					const auto self = weak.lock();
					if (!self) return;

					// A language change cleared the memo, which makes anything still in flight an
					// answer to a question nobody is asking now.
					if (generation != self->_resolved_generation) return;

					self->_resolving_areas.erase(key);
					self->_resolved_areas[key] = resolved;

					for (auto& location : self->_locations)
					{
						if (area_key(location) == key && location.has_same_photo_bounds(resolved))
						{
							const auto count = location.count;
							location = resolved;
							location.count = count;
						}
					}

					self->_state.map_locations(self->_locations);
					self->_state.invalidate_view(view_invalid::tooltip);
				});
			});
	}

public:
	sidebar_map_element(view_state& state, ui::const_surface_ptr s) noexcept :
		view_element(view_element_style::has_tooltip | view_element_style::can_invoke), _state(state),
		_surface_original(std::move(s))
	{
		_surface = _surface_original;
		const auto dims = _surface_original->dimensions();
		_source_bounds = recti(0, 0, dims.cx, dims.cy);
	}

	int cell_span() const
	{
		const auto dims = _surface_original->dimensions();
		const auto visible_cells = df::mul_div(view_source_bounds().width(),
			df::location_heat_map::map_width, dims.cx);
		return map_location_cell_span(visible_cells, bounds.width());
	}

	bool populate(const df::location_heat_map& summary, std::vector<map_location_area> locations)
	{
		invalidate_stale_language();

		for (auto& location : locations)
		{
			if (const auto found = _resolved_areas.find(area_key(location));
				found != _resolved_areas.end() && found->second.has_same_photo_bounds(location))
			{
				const auto count = location.count;
				location = found->second;
				location.count = count;
			}
		}
		_state.map_locations(locations);
		const auto locations_changed = _locations != locations;
		const auto summary_changed = _summary.coordinates != summary.coordinates;
		if (locations_changed || summary_changed)
		{
			if (locations_changed)
			{
				_locations = std::move(locations);
				_representatives.clear();
				_thumbnail_requests.clear();
				if (!_locations.empty())
				{
					location_matrix_params params;
					params.projection = location_matrix_projection::location_heat_map;
					params.area_cell_span = _locations.front().cell_span;
					auto matrix = _state.item_index.build_location_matrix(params);
					for (auto& cell : matrix.cells)
					{
						_representatives.emplace(area_key(cell.index, params.area_cell_span),
						                         representative_item{std::move(cell.representative_path), {}});
					}
				}
				_hover_location = -1;
				_hover_source_left = -1;
				update_source_bounds();
			}

			if (summary_changed)
			{
				_summary = summary;
				const auto dims = _surface_original->dimensions();
				df::assert_true(dims.cx * df::location_heat_map::map_height ==
					dims.cy * df::location_heat_map::map_width);

				const auto max_coord = *std::max_element(summary.coordinates.begin(), summary.coordinates.end());

				if (max_coord == 0)
				{
					_surface = _surface_original;
				}
				else
				{
					std::array<float, df::location_heat_map::map_width * df::location_heat_map::map_height>
						heat_strength{};
					const auto denominator = std::log1p(std::max(max_coord * 16u, 1u));

					for (auto heat_y = 1u; heat_y < df::location_heat_map::map_height - 1; ++heat_y)
					{
						for (auto heat_x = 1u; heat_x < df::location_heat_map::map_width - 1; ++heat_x)
						{
							const auto ym1 = (heat_y - 1) * df::location_heat_map::map_width;
							const auto y0 = heat_y * df::location_heat_map::map_width;
							const auto yp1 = (heat_y + 1) * df::location_heat_map::map_width;
							const auto value =
								summary.coordinates[ym1 + heat_x - 1] +
								summary.coordinates[ym1 + heat_x] * 2 +
								summary.coordinates[ym1 + heat_x + 1] +
								summary.coordinates[y0 + heat_x - 1] * 2 +
								summary.coordinates[y0 + heat_x] * 4 +
								summary.coordinates[y0 + heat_x + 1] * 2 +
								summary.coordinates[yp1 + heat_x - 1] +
								summary.coordinates[yp1 + heat_x] * 2 +
								summary.coordinates[yp1 + heat_x + 1];
							heat_strength[y0 + heat_x] = value == 0
								? 0.0f
								: std::clamp(static_cast<float>(std::log1p(value) / denominator), 0.0f, 1.0f);
						}
					}

					auto surface = std::make_shared<ui::surface>();
					const auto pixels = surface->alloc(dims, ui::texture_format::ARGB);
					const auto heat_color = ui::bgr(ui::style::color::important_background) | 0xFF000000;
					memset(pixels, 0, surface->stride() * dims.cy);

					for (auto y = 0; y < dims.cy; ++y)
					{
						const auto map_line = std::bit_cast<const uint32_t*>(_surface_original->pixels_line(y));
						const auto surf_line = std::bit_cast<uint32_t*>(surface->pixels_line(y));
						const auto heat_y = static_cast<size_t>(y) * df::location_heat_map::map_height / dims.cy;
						for (auto x = 0; x < dims.cx; ++x)
						{
							const auto heat_x = static_cast<size_t>(x) * df::location_heat_map::map_width / dims.cx;
							const auto strength = heat_strength[heat_y * df::location_heat_map::map_width + heat_x];
							surf_line[x] = strength == 0.0f
								? map_line[x]
								: ui::lerp(map_line[x], heat_color, strength);
						}
					}

					_surface = std::move(surface);
				}

				_tex_invalid = true;
			}
			return true;
		}

		return false;
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

			if (_tex)
			{
				dc.draw_texture(_tex, bounds.offset(element_offset), view_source_bounds());
			}

			if (_hover_location >= 0 && _hover_location < static_cast<int>(_locations.size()))
			{
				const auto hover_bounds = location_to_view(_locations[_hover_location], element_offset);
				dc.draw_rect(hover_bounds,
				             ui::color(ui::style::color::important_background, dc.colors.alpha));
			}
		}
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		const auto pie_size = std::min(width_limit, df::round(sidebar_visualization_size * mc.scale_factor));
		return {width_limit, df::mul_div(pie_size, 85, 100)};
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
			const auto previous_hover_source_left = _hover_source_left;
			int hover_location = -1;
			int hover_source_left = -1;

			if (hovering)
			{
				const auto dims = _surface_original->dimensions();
				const auto source_bounds = view_source_bounds();
				const auto source_width = source_bounds.width();
				const auto max_left = dims.cx - source_width;
				const auto centered_left = std::clamp(_source_bounds.center().x - source_width / 2, 0, max_left);
				const auto hover_x = std::clamp(ic.loc.x - logical_bounds.left, 0, logical_bounds.width());
				const auto half_width = logical_bounds.width() / 2;
				if (hover_x <= half_width)
				{
					hover_source_left = df::mul_div(centered_left, hover_x, std::max(half_width, 1));
				}
				else
				{
					hover_source_left = centered_left + df::mul_div(
						max_left - centered_left, hover_x - half_width,
						std::max(logical_bounds.width() - half_width, 1));
				}
				hover_source_left = std::clamp(hover_source_left, 0, max_left);
				_hover_source_left = hover_source_left;

				auto closest_distance = std::numeric_limits<int64_t>::max();

				// design.md targeting: the pointer has to be ON a marker. Picking the globally
				// nearest area made empty ocean hover -- and click through to -- whichever
				// cluster happened to be least far away, which is not a target the user chose.
				const auto grab = static_cast<int64_t>(std::max(bounds.height() / 8, 4));
				const auto max_distance = grab * grab;

				for (auto i = 0u; i < _locations.size(); i++)
				{
					const auto distance = distance_squared(ic.loc, location_to_view(_locations[i], ic.element_offset));
					if (distance <= max_distance && distance < closest_distance)
					{
						closest_distance = distance;
						hover_location = static_cast<int>(i);
					}
				}
			}
			else
			{
				_hover_source_left = -1;
			}

			if (_hover_location != hover_location || previous_hover_source_left != hover_source_left)
			{
				_hover_location = hover_location;
				if (_hover_location != -1) resolve_area(_locations[_hover_location]);
				_hover_source_left = hover_source_left;
				ic.invalidate_view = true;
				_state.invalidate_view(view_invalid::tooltip);
			}

		}
	}

	void tooltip(view_hover_element& hover, const pointi loc, const pointi element_offset) const override
	{
		if (_hover_location != -1)
		{
			const auto& location = _locations[_hover_location];
			const auto name = display_name(location);

			// The gazetteer answers on a worker, so the first bubble for an area often has no
			// name yet. Saying how many items are here is true either way; formatting the
			// "close to {}" and "within {} of {}" sentences around a hole is not.
			const auto named = !name.empty();

			hover.elements->add(make_icon_element(icon_index::location, flex_item::no_break));
			hover.elements->add(std::make_shared<text_element>(
				named ? name : format_plural_text(tt.map_items_here_fmt, location.count),
				ui::style::font_face::title,
				ui::style::text_style::multiline,
				flex_item::line_break));

			if (const auto found = _representatives.find(area_key(location)); found != _representatives.end())
			{
				auto& representative = found->second;
				if (!representative.item)
				{
					const auto indexed = _state.item_index.find_item(representative.path);
					if (indexed.ft)
					{
						representative.item = std::make_shared<df::item_element>(representative.path, indexed);
					}
				}

				if (representative.item)
				{
					const auto thumbnail = representative.item->thumbnail();
					if (is_valid(thumbnail))
					{
						files file_loader;
						hover.elements->add(std::make_shared<surface_element>(
							file_loader.image_to_surface(thumbnail), 160,
							flex_item::center | flex_item::new_line,
							representative.item->layout_orientation()));
					}
					else if (_thumbnail_requests.emplace(representative.path).second)
					{
						_state.item_index.queue_load_thumbnail(representative.item);
					}
				}
			}

			if (named)
			{
				hover.elements->add(std::make_shared<text_element>(
					str_format(tt.click_items_from_fmt.sv(), location.count, name), ui::style::font_face::dialog,
					ui::style::text_style::multiline, flex_item::new_line));

				// locations.md 5.4: the action line states the search it will run, including the
				// radius, so the count on the map and the count after the click are never a surprise.
				hover.elements->add(std::make_shared<action_element>(
					str_format(tt.search_within_fmt.sv(), format_distance_km(location.search_radius_km()),
					           name)));
			}
		}

		hover.active_bounds = hover.window_bounds = bounds.offset(element_offset);
		hover.horizontal = true;
	}

	void dispatch_event(const view_element_event& event) override
	{
		if (event.type == view_element_event_type::invoke && _hover_location != -1)
		{
			const auto& loc = _locations[_hover_location];
			auto search = df::search_t();

			// locations.md 5.1: an area opens a place plus a radius, never an internal `area:`
			// term. "Within 25 km of London" is a concept the user holds; "the cells GeoNames
			// happened to attribute to London" is not.
			const auto km = loc.search_radius_km();

			if (loc.place_position.is_valid() && !loc.name.empty())
			{
				search.location(loc.name, df::location_level::any).set_place_distance(km);
			}
			else if (loc.position.is_valid())
			{
				// Nothing was near enough to name, so the coordinates say exactly what was clicked.
				search.location(loc.position, km);
			}

			// Navigating from the map changes the query only. Grouping and sorting are
			// user-owned presentation and are never reassigned as a side effect (design.md).
			_state.open(event.host, search, {});
		}
	}
};

class app_logo_element final : public std::enable_shared_from_this<app_logo_element>, public view_element
{
	view_state& _state;
	std::string_view _text = s_app_name;
	ui::style::font_face _font = ui::style::font_face::title;
	ui::style::text_style _text_style = ui::style::text_style::single_line;

	mutable plasma logo_plasma;
	mutable ui::texture_ptr _plasma_tex;
	mutable ui::texture_ptr _logo_tex;

public:
	app_logo_element(view_state& state) noexcept : view_element(
		                                                   view_element_style::has_tooltip |
		                                                   view_element_style::can_invoke), _state(state)
	{
	}

	void text(const std::string_view t)
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
				auto res = platform::resource_item::logo15;
				if (logical_bounds.height() >= 40) res = platform::resource_item::logo30;
				if (logical_bounds.height() >= 60) res = platform::resource_item::logo;

				files ff;
				const auto logo_surface = ff.image_to_surface(load_resource(res));

				_logo_tex = t;
				_logo_tex->update(logo_surface);
			}
		}

		if (_logo_tex)
		{
			dc.draw_texture(_logo_tex, center_rect(_logo_tex->dimensions(), logical_plasma_bounds),
			                _logo_tex->dimensions(), dc.colors.alpha);
		}

		const auto plasma_border_clr = ui::color(0.25f, 0.25f, 0.25f, 1.0f);
		const auto pad = df::round(1 * dc.scale_factor);
		dc.draw_border(logical_plasma_bounds, logical_plasma_bounds.inflate(pad), plasma_border_clr, plasma_border_clr);

		auto text_bounds = logical_bounds;
		text_bounds.left = logical_plasma_bounds.right + dc.padding2;
		dc.draw_text(_text, text_bounds, _font, _text_style, ui::color(dc.colors.foreground, dc.colors.alpha), {});
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		const auto height = mc.text_line_height(_font) + mc.padding2;
		const auto text_width_limit = std::max(0, width_limit - height - mc.padding2);
		const auto text_extent = mc.measure_text(_text, _font, _text_style, text_width_limit);
		return {std::min(width_limit, height + mc.padding2 + text_extent.cx), std::max(height, text_extent.cy)};
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

	void step_plasma() const
	{
		logo_plasma.step();
	}

	bool plasma_is_active() const
	{
		return logo_plasma.is_active();
	}

	bool hover(const bool value)
	{
		logo_plasma._hover = value;
		if (is_style_bit_set(view_element_style::hover) == value) return false;

		const auto previous = _bg_color;
		set_style_bit(view_element_style::hover, value);
		_bg_color = previous;

		if (value)
		{
			_bg_target = view_handle_color(false, true, false, true, true,
			                               ui::color(ui::style::color::toolbar_background));

			if (ui::is_alpha_zero(_bg_color.a))
			{
				_bg_color.r = _bg_target.r;
				_bg_color.g = _bg_target.g;
				_bg_color.b = _bg_target.b;
			}
		}
		else
		{
			_bg_target = _bg_color;
			_bg_target.a = 0.0f;
		}

		return true;
	}

	bool step_background()
	{
		const auto delta = _bg_target - _bg_color;
		if (delta.abs_sum() <= ui::color::color_epsilon) return false;

		_bg_color += delta * 0.2345f;
		return true;
	}

	void free_graphics_resources() const
	{
		_plasma_tex.reset();
		_logo_tex.reset();
	}
};


class sidebar_host final : public std::enable_shared_from_this<sidebar_host>, public view_host
{
public:
	view_state& _state;
	view_scroller _scroller;
	ui::frame_ptr _frame;
	ui::control_frame_ptr _owner;
	recti _embedded_bounds;
	bool _has_focus = false;
	bool _show_indexing_control = false;
	bool _is_detecting = false;
	int _last_indexing_perc = -1;


	std::shared_ptr<sidebar_indexing_element> _indexing_elements;
	std::shared_ptr<sidebar_file_type_element> _type_chart;
	std::shared_ptr<sidebar_history_element> _history_chart;
	std::shared_ptr<sidebar_map_element> _map;
	std::vector<search_item_ptr> _items;
	std::vector<drive_item_ptr> _drives;
	std::vector<view_element_ptr> _elements;

	sidebar_host(view_state& s) : _state(s)
	{
		df::assert_true(ui::is_ui_thread());

		_indexing_elements = std::make_shared<sidebar_indexing_element>(
			s, flex_item::stretch |
			flex_item::center | view_element_style::important);
		_indexing_elements->padding(8);

		_type_chart = std::make_shared<sidebar_file_type_element>(s);
		_history_chart = std::make_shared<sidebar_history_element>(s);

		files ff;
		const auto surface = ff.image_to_surface(load_resource(platform::resource_item::map_png));
		_map = std::make_shared<sidebar_map_element>(s, surface);

		_elements.emplace_back(_type_chart);
		_elements.emplace_back(_map);
		_elements.emplace_back(_history_chart);
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

	void attach_embedded(const ui::frame_ptr& frame, const ui::control_frame_ptr& owner)
	{
		_frame = frame;
		_owner = owner;
	}

	void layout_embedded(ui::measure_context& mc, const recti bounds)
	{
		_embedded_bounds = bounds;
		_extent = bounds.extent();
		layout(mc);
	}

	void render_embedded(ui::draw_context& dc)
	{
		dc.draw_rect(_embedded_bounds, ui::color(ui::style::color::sidebar_background, dc.colors.alpha));
		dc.clip_bounds(_embedded_bounds);
		on_window_paint(dc);
		dc.restore_clip();
	}

	bool can_scroll() const
	{
		return _scroller.can_scroll();
	}

	void invalidate_file_type() const
	{
		_frame->invalidate(_type_chart->bounds.offset(-_scroller.scroll_offset()));
	}

	void populate_file_types_and_dates() const
	{
		const auto histograms = _state.item_index.histograms();
		_type_chart->populate(histograms->_file_types);
		_history_chart->populate(histograms->_dates);
		_frame->invalidate();
	}

	void hover_file_type(const int id)
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
		df::assert_true(ui::is_ui_thread());

		_items = std::move(items);
		_drives = std::move(drives);

		_elements.clear();
		if (_show_indexing_control) _elements.emplace_back(_indexing_elements);
		if (setting.sidebar.show_total_items) _elements.emplace_back(_type_chart);
		if (setting.sidebar.show_world_map) _elements.emplace_back(_map);
		if (setting.sidebar.show_history) _elements.emplace_back(_history_chart);
		_elements.insert(_elements.end(), item_elements.begin(), item_elements.end());

		update_current_search();
		queue_update_predictions();
		layout();
	}

	void populate()
	{
		df::assert_true(ui::is_ui_thread());

		const auto histograms = _state.item_index.histograms();
		_type_chart->populate(histograms->_file_types);
		_history_chart->populate(histograms->_dates);
		if (_map->populate(histograms->_locations, histograms->map_locations(_map->cell_span()))) invalidate();

		search_items_by_key_t existing;

		for (const auto& i : _items)
		{
			existing[i->key] = i->summary;
		}

		_state.queue_async(async_queue::sidebar, [t = ui_owned(_state._async, shared_from_this()), &s = _state, existing]
		{
			constexpr search_item_factory f;
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

			if (setting.sidebar.show_tags)
			{
				add_elements(f.create_tags(s, existing));
			}


			item_elements.emplace_back(std::make_shared<divider_element>());

			const auto tag_show_text = setting.sidebar.show_favorite_tags_only
				                           ? tt.command_all_tags
				                           : tt.command_favorite_tags;

			auto tags_element = std::make_shared<link_element>(tag_show_text, commands::view_favorite_tags,
			                                                   ui::style::font_face::dialog,
			                                                   ui::style::text_style::multiline_center,
			                                                   flex_item::center);

			auto customise_element = std::make_shared<link_element>(tt.command_customise, commands::options_sidebar,
			                                                        ui::style::font_face::dialog,
			                                                        ui::style::text_style::multiline_center,
			                                                        flex_item::center);
			auto favorite_tags_element = std::make_shared<link_element>(tt.customise_tags_title,
			                                                              commands::favorite_tags,
			                                                              ui::style::font_face::dialog,
			                                                              ui::style::text_style::multiline_center,
			                                                              flex_item::center);

			item_elements.emplace_back(tags_element);
			item_elements.emplace_back(favorite_tags_element);
			item_elements.emplace_back(customise_element);

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
		df::assert_true(ui::is_ui_thread());

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
			const auto pad = df::round(2 * dc.scale_factor);
			dc.draw_border(_controller_bounds, _controller_bounds.inflate(pad), c, c);
		}
	}

	void update_current_search() const
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

		// calc_sum only queries item_index, which is synchronized, so it is safe off the UI thread. The
		// elements themselves are not: they own text layouts, so the list is guarded rather than copied
		// loose into the worker, and the view is reached through a weak reference locked on the UI thread.
		auto elements = ui_owned(_state._async, std::make_shared<std::vector<search_item_ptr>>(_items));

		_state.queue_async(async_queue::sidebar, [weak = weak_from_this(), &s = _state, elements, token]
		{
			df::scope_locked_inc slc(df::jobs_running);

			int update_count = 0;

			for (const auto& i : *elements)
			{
				if (i->calc_sum)
				{
					const auto sum = i->calc_sum(s, token);

					if (!token.is_cancelled())
					{
						update_count += 1;

						//if (i->summary != sum)
						{
							s.queue_ui([weak, i, sum, token]
							{
								if (token.is_cancelled()) return;
								const auto t = weak.lock();
								if (!t) return;
								i->summary = sum;
								t->_frame->invalidate();
							});
						}
					}
				}
			}

			df::trace(std::format("Sidebar update {} predictions", update_count));
		});
	}

	view_controller_ptr controller_from_location(const pointi loc) override
	{
		df::assert_true(ui::is_ui_thread());

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

	void layout() const
	{
		if (_frame)
		{
			_frame->layout();
			_frame->invalidate();
		}
	}

	void invalidate() const
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
		df::assert_true(ui::is_ui_thread());

		constexpr auto x_padding = 4;
		const auto embedded = !_embedded_bounds.is_empty();
		const auto scroll_padding = embedded || _scroller.can_scroll() ? mc.scroll_width : x_padding;
		const auto layout_padding = sizei{0, df::round(mc.padding1 / mc.scale_factor)};
		auto avail_bounds = embedded ? _embedded_bounds : recti(_extent);
		avail_bounds.left += x_padding; // -(mc.baseline_snap / 2);
		avail_bounds.right -= scroll_padding; // -(mc.baseline_snap / 2);

		ui::control_layouts positions;
		flex_container_layout column;
		column.direction = flex_direction::column;
		column.wrap = flex_wrap::no_wrap;
		column.align_items = flex_align::start;
		column.padding = layout_padding;
		const auto content_extent = layout_flex_elements(_elements, mc, positions, avail_bounds, column);

		const auto host_bounds = embedded ? _embedded_bounds : recti(_extent);
		const recti scroll_bounds{host_bounds.right - scroll_padding,
		                          embedded ? host_bounds.top + host_bounds.height() / 2 : host_bounds.top,
		                          host_bounds.right,
		                          host_bounds.bottom};
		const recti client_bounds{host_bounds.left, host_bounds.top, host_bounds.right - scroll_padding,
		                          host_bounds.bottom};
		_scroller.layout({client_bounds.width(), content_extent.cy + mc.padding2}, client_bounds, scroll_bounds);
	}

	static int preferred_width(ui::measure_context& mc)
	{
		const auto extent = mc.measure_text("Documents", ui::style::font_face::dialog,
		                                    ui::style::text_style::single_line, 200);
		return df::mul_div(extent.cx, 5, 2);
	}

	void focus_changed(const bool has_focus, const ui::control_base_ptr& child) override
	{
		df::trace(std::format("Sidebar navigation_controls::focus {}", has_focus));

		_has_focus = has_focus;
		_frame->invalidate();
	}


	bool _is_active = true;

	void tick() override
	{
		if (_frame && _frame->is_visible())
		{
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
					_last_indexing_perc = perc;
					invalidate();
				}
			}
			else
			{
				_last_indexing_perc = -1;
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
		df::assert_true(ui::is_ui_thread());

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
