// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "pch.h"

#include "model_items.h"

#include "ui_dialog.h"
#include "model.h"
#include "ui_controllers.h"
#include "ui_controls.h"

static constexpr auto today_order = 100;

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////


static_assert(std::is_trivially_copyable_v<df::group_key>);
static_assert(std::is_trivially_copyable_v<df::file_group_histogram>);
static_assert(std::is_trivially_copyable_v<df::file_group_histogram>);

//////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////

static df::group_key media_type_index(const df::item_element_ptr& i)
{
	df::group_key result;
	const auto* const ft = i->file_type();
	if (ft->has_trait(file_type_traits::folder)) return {df::group_key_type::folder};
	result.order1 = ft->group->id;
	result.type = (ft->group == file_group::other) ? df::group_key_type::other : df::group_key_type::grouped_value;
	result.text1 = str::cache(ft->display_name(true));
	result.group = ft->group;
	return result;
}

static df::group_key shuffle_index_key(const df::item_element_ptr& i)
{
	df::group_key result;
	result.type = df::group_key_type::grouped_value;
	return result;
}

static df::group_key extension_key(const df::item_element_ptr& i)
{
	df::group_key result;
	auto ext = i->extension();

	if (!ext.empty())
	{
		result.type = df::group_key_type::grouped_value;
		if (ext.front() == L'.') ext = ext.substr(1);
		result.text1 = str::cache(ext);
	}

	return result;
}

static df::group_key folder_key(const df::file_item_ptr& i)
{
	df::group_key result;
	result.type = df::group_key_type::grouped_value;
	result.text1 = i->folder().text();
	return result;
}

static df::group_key folder_key(const df::folder_item_ptr& i)
{
	df::group_key result;
	result.type = df::group_key_type::grouped_value;
	result.text1 = i->path().parent().text();
	return result;
}

static df::group_key location_key(const df::item_element_ptr& i)
{
	df::group_key result;
	const auto* const ft = i->file_type();
	if (ft->has_trait(file_type_traits::folder)) return {df::group_key_type::folder};

	const auto md = i->metadata();

	if (md && !is_empty(md->location_country))
	{
		result.type = df::group_key_type::grouped_value;
		result.text1 = normalize_county_name(md->location_country);
		result.text2 = md->location_state;
		result.text3 = md->location_place;
	}

	return result;
}

static df::group_key camera_key(const df::item_element_ptr& i)
{
	df::group_key result;
	const auto* const ft = i->file_type();
	if (ft->has_trait(file_type_traits::folder)) return {df::group_key_type::folder};

	const auto md = i->metadata();

	if (md && !is_empty(md->camera_manufacturer))
	{
		result.type = df::group_key_type::grouped_value;
		result.text1 = md->camera_manufacturer;
		result.text2 = md->camera_model;
	}

	return result;
}

static df::group_key album_show_key(const df::item_element_ptr& i)
{
	df::group_key result;
	const auto* const ft = i->file_type();
	if (ft->has_trait(file_type_traits::folder)) return {df::group_key_type::folder};

	const auto md = i->metadata();

	if (md)
	{
		if (!is_empty(md->show))
		{
			result.type = df::group_key_type::grouped_value;
			result.text1 = md->show;
			result.text1_prop_type = prop::show;

			if (md->season != 0)
			{
				result.order3 = md->season;
			}
		}
		else if (!is_empty(md->album_artist) || !is_empty(md->artist) || !is_empty(md->album))
		{
			result.type = df::group_key_type::grouped_value;

			if (!is_empty(md->album_artist))
			{
				result.text1 = md->album_artist;
				result.text1_prop_type = prop::album_artist;
			}
			else if (!is_empty(md->artist))
			{
				result.text1 = md->artist;
				result.text1_prop_type = prop::artist;
			}

			if (!is_empty(md->album))
			{
				result.text2 = md->album;
			}
		}
	}

	return result;
}

static df::group_key size_key(const df::item_element_ptr& i)
{
	df::group_key result;
	const auto size_bucket = prop::size_bucket(i->file_size().to_int64());
	if (size_bucket == 0) return {df::group_key_type::grouped_no_value};
	result.order1 = df::isqrt(size_bucket);
	result.type = df::group_key_type::grouped_value;
	return result;
}

static const auto rating_base_num = 6;
static const auto rating_reject_num = 7;

static df::group_key rating_key(const df::item_element_ptr& i)
{
	df::group_key result;
	const auto* const ft = i->file_type();
	if (ft->has_trait(file_type_traits::folder)) return {df::group_key_type::folder};

	const auto md = i->metadata();

	if (md)
	{
		if (md->rating != 0)
		{
			result.type = df::group_key_type::grouped_value;

			if (md->rating == -1)
			{
				result.order1 = rating_reject_num;
			}
			else
			{
				result.order1 = rating_base_num - md->rating;
			}
		}

		if (!is_empty(md->label))
		{
			result.text1 = md->label;
			result.type = df::group_key_type::grouped_value;
		}
	}

	return result;
}

static df::group_key date_key(const prop::key_ref prop, const df::date_t when, const df::item_element_ptr& i)
{
	df::group_key result;
	const auto* const ft = i->file_type();
	if (ft->has_trait(file_type_traits::folder)) return {df::group_key_type::folder};
	result.type = df::group_key_type::grouped_value;

	if (when.is_valid())
	{
		const auto day = when.to_days();
		const auto today = platform::now().to_days();

		if (day == today)
		{
			result.order1 = 0;
		}
		else if (day == today - 1)
		{
			result.order1 = 1;
		}
		else
		{
			const auto date = when.date();
			result.order1 = 3000 - date.year;
			result.order2 = 100 - date.month;
		}
	}

	return result;
}

static df::group_key resolution_key(const df::item_element_ptr& i)
{
	df::group_key result;
	const auto* const ft = i->file_type();
	if (ft->has_trait(file_type_traits::folder)) return {df::group_key_type::folder};

	const auto md = i->metadata();

	if (md && md->width > 0 && md->height > 0)
	{
		const auto extent = sizei{md->width, md->height};
		const auto display_name = ft->group->display_name(true);

		if (ft->has_trait(file_type_traits::av))
		{
			const auto video_res = prop::format_video_resolution(extent);

			if (!video_res.empty())
			{
				result.type = df::group_key_type::grouped_value;
				result.order1 = ft->group->id;
				result.order2 = 1 + df::isqrt(extent.cx * extent.cy);
				result.text1 = str::cache(display_name);
				result.text2 = str::cache(video_res);
				return result;
			}

			result.type = df::group_key_type::grouped_value;
			result.order1 = ft->group->id;
			result.order2 = 1 + df::isqrt(extent.cx * extent.cy);
			result.text1 = str::cache(display_name);
			result.text2 = str::cache(str::format(u8"{}x{}"sv, extent.cx, extent.cy));
		}
		else
		{
			if (extent.cx <= 128 && extent.cy <= 128)
			{
				result.type = df::group_key_type::grouped_value;
				result.order1 = 1;
				result.order2 = 0;
				result.text1 = str::cache(display_name);
				result.text2 = u8"icon size"_c;
			}
			else
			{
				const auto mp = ui::calc_mega_pixels(extent.cx, extent.cy);

				if (mp < 0.9)
				{
					result.type = df::group_key_type::grouped_value;
					result.order1 = 1;
					result.order2 = 1;
					result.text1 = str::cache(display_name);
					result.text2 = u8"small size"_c;
				}
				else
				{
					const auto n = prop::exp_round(mp);
					result.type = df::group_key_type::grouped_value;
					result.order1 = 1;
					result.order2 = 1 + df::isqrt(n);
					result.text1 = str::cache(display_name);
					result.text2 = str::cache(str::format(u8"{} megapixels"sv, n));
				}
			}
		}
	}

	return result;
}

class toggle_item_display_element : public std::enable_shared_from_this<toggle_item_display_element>,
                                    public view_element
{
private:
	df::weak_item_group_ptr _parent;
	const icon_index _icon = icon_index::details;

public:
	toggle_item_display_element(df::weak_item_group_ptr parent) noexcept : view_element(
		                                                                       view_element_style::right_justified |
		                                                                       view_element_style::has_tooltip |
		                                                                       view_element_style::can_invoke),
	                                                                       _parent(std::move(parent))
	{
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		const auto bg = calc_background_color(dc);
		const auto clr = ui::color(dc.colors.foreground, dc.colors.alpha);
		xdraw_icon(dc, _icon, bounds.offset(element_offset), clr, bg);
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
			const auto p = _parent.lock();
			if (p) p->toggle_display();
		}
	}

	void tooltip(view_hover_element& hover, const pointi loc, const pointi element_offset) const override
	{
		hover.elements.add(std::make_shared<text_element>(tt.tooltip_toggle_details_selected));
		hover.window_bounds = hover.active_bounds = bounds.offset(element_offset);
	}
};

static void sort_items(df::item_elements& items, const group_by group_mode, const sort_by sort_order,
                       const bool group_by_dups)
{
	constexpr auto size_sorter = [](const df::item_element_ptr& l, const df::item_element_ptr& r) -> bool
	{
		const auto ll = l->file_size();
		const auto rr = r->file_size();
		return (ll == rr) ? icmp(l->name(), r->name()) < 0 : ll > rr;
	};

	constexpr auto modified_sorter = [](const df::item_element_ptr& l, const df::item_element_ptr& r)
	{
		if (l->file_modified() == r->file_modified()) return icmp(l->name(), r->name()) < 0;
		return l->file_modified() > r->file_modified();
	};

	constexpr auto created_sorter = [](const df::item_element_ptr& l, const df::item_element_ptr& r)
	{
		if (l->media_created() == r->media_created()) return icmp(l->name(), r->name()) < 0;
		return l->media_created() > r->media_created();
	};

	constexpr auto pixel_sorter = [](const df::item_element_ptr& l, const df::item_element_ptr& r) -> bool
	{
		const auto lmd = l->metadata();
		const auto rmd = r->metadata();
		const auto ll = lmd ? ui::calc_mega_pixels(lmd->width, lmd->height) : 0;
		const auto rr = rmd ? ui::calc_mega_pixels(rmd->width, rmd->height) : 0;
		return (ll == rr) ? icmp(l->name(), r->name()) < 0 : ll > rr;
	};

	constexpr auto group_sorter = [](const df::item_element_ptr& l, const df::item_element_ptr& r)
	{
		const auto ll = l->duplicates().group;
		const auto rr = r->duplicates().group;
		if (ll != rr) return ll < rr;
		return icmp(l->name(), r->name()) < 0;
	};

	constexpr auto name_sorter = [](const df::item_element_ptr& l, const df::item_element_ptr& r)
	{
		return icmp(l->name(), r->name()) < 0;
	};

	if (group_mode == group_by::shuffle)
	{
		std::ranges::sort(items, [](const df::item_element_ptr& l, const df::item_element_ptr& r)
		{
			return l->random() < r->random();
		});
	}
	else if (sort_order == sort_by::size)
	{
		std::ranges::sort(items, size_sorter);
	}
	else if (sort_order == sort_by::date_modified)
	{
		std::ranges::sort(items, modified_sorter);
	}
	else if (sort_order == sort_by::name)
	{
		std::ranges::sort(items, name_sorter);
	}
	else if (group_mode == group_by::size)
	{
		std::ranges::sort(items, size_sorter);
	}
	else if (group_mode == group_by::date_modified)
	{
		std::ranges::sort(items, modified_sorter);
	}
	else if (group_mode == group_by::date_created)
	{
		std::ranges::sort(items, created_sorter);
	}
	else if (group_mode == group_by::resolution)
	{
		std::ranges::sort(items, pixel_sorter);
	}
	else if (group_mode == group_by::folder)
	{
		std::ranges::sort(items, name_sorter);
	}
	else if (group_by_dups)
	{
		std::ranges::sort(items, group_sorter);
	}
	else
	{
		std::ranges::sort(items, name_sorter);
	}

	if (!setting.sort_dates_descending && (group_mode == group_by::date_created || group_mode ==
		group_by::date_modified))
	{
		std::ranges::reverse(items);
	}
}


void df::item_group::sort(const group_by group_mode, const sort_by sort_order, const bool group_by_dups)
{
	sort_items(_items, group_mode, sort_order, group_by_dups);

	auto last_dup_group = 0u;
	auto alt_background = false;

	for (const auto& i : _items)
	{
		const auto dup_group = i->duplicates().group;
		if (group_by_dups && last_dup_group != dup_group) alt_background = !alt_background;
		i->alt_background = alt_background;
		last_dup_group = dup_group;
	}

	_show_folder = group_by_dups || _state.search().has_related();
}

//////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////

df::file_group_histogram df::item_set::summary() const
{
	file_group_histogram result;

	for (const auto& i : _folders)
	{
		result.record(i->info());
	}

	for (const auto& i : _items)
	{
		result.record(i->file_type(), i->file_size());
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////

df::item_set_info df::item_set::info() const
{
	item_set_info result;

	for (const auto& i : _items)
	{
		const auto md = i->metadata();

		if (md)
		{
			if (i->sidecars_count() > 0) result.sidecars += 1;

			if (i->is_media())
			{
				if (md->tags.is_empty()) result.untagged += 1;
				if (str::is_empty(md->location_country)) result.unlocated += 1;
				if (md->rating == 0) result.unrated += 1;
				if (str::is_empty(md->copyright_credit)) result.uncredited += 1;
			}
		}

		if (i->duplicates().count > 1)
		{
			result.duplicates += 1;
		}

		if (i->rating() == -1) result.rejected += 1;
	}

	return result;
}


std::u8string_view item_presence_text(const item_presence v, const bool long_text)
{
	if (v == item_presence::this_in)
	{
		return long_text ? tt.presence_this_in_long : tt.presence_this_in;
	}
	if (v == item_presence::similar_in)
	{
		return long_text ? tt.presence_similar_in_long : tt.presence_similar_in;
	}
	if (v == item_presence::not_in)
	{
		return long_text ? tt.presence_not_in_long : tt.presence_not_in;
	}
	if (v == item_presence::newer_in)
	{
		return long_text ? tt.presence_newer_in_long : tt.presence_newer_in;
	}
	if (v == item_presence::older_in)
	{
		return long_text ? tt.presence_older_in_long : tt.presence_older_in;
	}
	return tt.presence_loading;
}

static df::group_key presence_key(const item_presence& pr)
{
	df::group_key result;
	result.order1 = static_cast<int>(pr);
	result.type = df::group_key_type::grouped_value;

	switch (pr)
	{
	case item_presence::this_in:
		result.text1 = str::cache(tt.presence_this_in);
		result.icon = icon_index::check;
		break;
	case item_presence::similar_in:
		result.text1 = str::cache(tt.presence_similar_in);
		result.icon = icon_index::compare;
		break;
	case item_presence::not_in:
		result.text1 = str::cache(tt.presence_not_in);
		result.icon = icon_index::del;
		break;
	case item_presence::newer_in:
		result.text1 = str::cache(tt.presence_newer_in);
		result.icon = icon_index::add2;
		break;
	case item_presence::older_in:
		result.text1 = str::cache(tt.presence_older_in);
		result.icon = icon_index::remove2;
		break;
	default:
		result.text1 = str::cache(tt.presence_loading);
		result.icon = icon_index::unknown;
		break;
	}

	return result;
}

static int calc_item_line_height()
{
	return settings_t::item_scale_snaps[std::max(0, setting.item_scale) % settings_t::item_scale_count];
}

std::shared_ptr<group_title_control> df::build_group_title(view_state& s, const view_host_base_ptr& view,
                                                           const item_group_ptr& g)
{
	assert_true(ui::is_ui_thread());

	auto result = std::make_shared<group_title_control>();
	const auto font = ui::style::font_face::title;

	g->_scroll_tooltip_text.clear();

	const auto add_title_link = [g, &s, view, result, font](const std::u8string_view title, const search_t& search)
	{
		if (search.is_empty())
		{
			const auto link = std::make_shared<text_element>(title, font, ui::style::text_style::multiline,
			                                                 view_element_style::none);
			link->margin(4);
			link->padding(4);
			result->elements.emplace_back(link);
		}
		else
		{
			const auto command = [&s, view, search]() { s.open(view, search, {}); };

			auto tooltip = [search](view_hover_element& popup)
			{
				popup.elements.add(make_icon_element(search.first_type()->icon, view_element_style::no_break));
				popup.elements.add(std::make_shared<text_element>(search.text()));
				popup.elements.add(std::make_shared<action_element>(tt.click_to_search_similar));
			};

			const auto link = std::make_shared<link_element>(title, command, tooltip, font,
			                                                 ui::style::text_style::multiline,
			                                                 view_element_style::none);
			link->margin(4);
			link->padding(4);
			result->elements.emplace_back(link);
		}

		if constexpr (font == ui::style::font_face::title)
		{
			g->_scroll_tooltip_text.emplace_back(title);
		}
	};

	const auto order = s.group_order();
	const auto current_search = s.search();
	const auto& key = g->_key;

	if (key.type == group_key_type::grouped_no_value)
	{
		if (order == group_by::extension)
		{
			add_title_link(tt.group_title_no_extension, search_t(current_search).without_extension());
		}
		else if (order == group_by::location)
		{
			add_title_link(tt.group_title_no_location, search_t(current_search).without(prop::location_country));
		}
		else if (order == group_by::rating_label)
		{
			add_title_link(tt.group_title_no_rating, search_t(current_search).without(prop::rating));
		}
		else if (order == group_by::resolution)
		{
			add_title_link(tt.group_title_no_resolution, search_t(current_search));
		}
		else if (order == group_by::camera)
		{
			add_title_link(tt.group_title_no_camera, search_t(current_search).without(prop::camera_model));
		}
		else if (order == group_by::album_show)
		{
			add_title_link(tt.group_title_no_album_or_show, {});
		}
		else if (order == group_by::folder)
		{
			add_title_link(tt.folder_title, {});
		}
		else
		{
			add_title_link(tt.group_title_no_value, search_t(current_search).without(prop::album));
		}
	}
	else if (key.type == group_key_type::grouped_value)
	{
		if (order == group_by::shuffle)
		{
			add_title_link(tt.group_title_shuffle, {});
		}
		else if (order == group_by::size)
		{
			file_size min_size;
			file_size max_size;

			if (!g->_items.empty())
			{
				min_size = g->_items[0]->file_size();
				max_size = g->_items[0]->file_size();

				for (const auto& i : g->_items)
				{
					const auto fs = i->file_size();
					if (min_size > fs) min_size = fs;
					if (max_size < fs) max_size = fs;
				}

				if (min_size == max_size)
				{
					add_title_link(prop::format_size(min_size), {});
				}
				else
				{
					const auto text = str::format(tt.group_title_size_range_fmt, prop::format_size(min_size),
					                              prop::format_size(max_size));
					add_title_link(text, {});
				}
			}
		}
		else if (order == group_by::extension)
		{
			if (!is_empty(key.text1))
			{
				add_title_link(key.text1, search_t(current_search).with_extension(key.text1));
			}
			else
			{
				add_title_link(tt.group_title_no_extension, search_t(current_search).without_extension());
			}
		}
		else if (order == group_by::location)
		{
			if (!is_empty(key.text1))
			{
				add_title_link(key.text1, search_t(current_search).with(prop::location_country, key.text1));
			}

			if (!is_empty(key.text2))
			{
				add_title_link(key.text2, search_t(current_search).with(prop::location_state, key.text2));
			}

			if (!is_empty(key.text3))
			{
				add_title_link(key.text3, search_t(current_search).with(prop::location_place, key.text3));
			}
		}
		else if (order == group_by::rating_label)
		{
			if (rating_reject_num == key.order1)
			{
				add_title_link(tt.command_rate_rejected, search_t(current_search).with(prop::rating, -1));
			}
			else if (key.order1 != 0)
			{
				const int32_t rating = rating_base_num - key.order1;

				if (rating != 0)
				{
					const auto search = search_t(current_search).with(prop::rating, rating);
					const auto command = [&s, view, search]() { s.open(view, search, {}); };

					auto tooltip = [search, rating](view_hover_element& popup)
					{
						popup.elements.add(
							make_icon_element(icon_index::star_solid, rating, view_element_style::line_break));
						popup.elements.add(std::make_shared<action_element>(tt.click_to_search_similar));
					};

					const auto link = make_icon_link_element(icon_index::star_solid, rating, command, tooltip);
					link->margin(4);
					link->padding(4);
					result->elements.emplace_back(link);

					g->_scroll_tooltip_rating = rating;
				}
			}

			if (!is_empty(key.text1))
			{
				add_title_link(tt.translate_text(key.text1.sz()),
				               search_t(current_search).with(prop::label, key.text1));
			}
		}
		else if (order == group_by::date_created || order == group_by::date_modified)
		{
			auto target = date_parts_prop::any;
			if (order == group_by::date_modified) target = date_parts_prop::modified;
			if (order == group_by::date_created) target = date_parts_prop::created;

			if (key.order1 == 0)
			{
				const auto today = platform::now().date();
				add_title_link(tt.group_title_today,
				               search_t(current_search).day(today.day, today.month, today.year, target));
			}
			else if (key.order1 == 1)
			{
				const auto yesterday = platform::now().previous_day().date();
				add_title_link(tt.group_title_yesterday,
				               search_t(current_search).day(yesterday.day, yesterday.month, yesterday.year, target));
			}
			else
			{
				const auto month = 100 - key.order2;
				const auto year = 3000 - key.order1;

				add_title_link(str::format(u8"{} {}"sv, str::month(month, true), year),
				               search_t(current_search).day(0, month, year, target));
			}
		}
		else if (order == group_by::resolution)
		{
			if (!is_empty(key.text1) && !is_empty(key.text2))
			{
				add_title_link(format(u8"{}:{}"sv, key.text1, key.text2), {});
			}
		}
		else if (order == group_by::camera)
		{
			if (!is_empty(key.text1))
			{
				add_title_link(key.text1, search_t(current_search).with(prop::camera_manufacturer, key.text1));
			}

			if (!is_empty(key.text2))
			{
				add_title_link(key.text2, search_t(current_search).with(prop::camera_model, key.text2));
			}
		}
		else if (order == group_by::album_show)
		{
			if (!is_empty(key.text1))
			{
				add_title_link(key.text1, search_t(current_search).with(key.text1_prop_type, key.text1));
			}

			if (key.order3 != 0)
			{
				add_title_link(str::format(u8"{} {}"sv, prop::season.text(), key.order3),
				               search_t(current_search).with(prop::season, key.order3));
			}

			if (!is_empty(key.text2))
			{
				add_title_link(key.text2, search_t(current_search).with(prop::album, key.text2));
			}
		}
		else if (order == group_by::file_type)
		{
			if (!is_empty(key.text1))
			{
				add_title_link(key.text1, search_t(current_search).add_media_type(key.group));
			}
		}
		else if (order == group_by::presence)
		{
			if (!is_empty(key.text1))
			{
				add_title_link(key.text1, {});
			}
		}
		else if (order == group_by::folder)
		{
			if (!is_empty(key.text1))
			{
				add_title_link(key.text1, search_t().add_selector(key.text1));
			}
		}
	}
	else if (key.type == group_key_type::other)
	{
		add_title_link(file_group::other.display_name(true), {});
	}
	else if (key.type == group_key_type::folder)
	{
		add_title_link(file_group::folder.display_name(true), {});
	}
	else
	{
		add_title_link(tt.group_title_items, {});
	}

	result->elements.emplace_back(std::make_shared<padding_element>());

	if (key.type == group_key_type::folder)
	{
		result->elements.emplace_back(make_icon_link_element(icon_index::recursive, commands::browse_recursive,
		                                                     view_element_style::right_justified));
	}


	result->elements.emplace_back(std::make_shared<toggle_item_display_element>(g));
	result->padding(8);
	result->margin(4, 8);
	result->set_style_bit(view_element_style::background, true);

	return result;
}

void df::item_group::update_scroll_info(const group_by order)
{
	scroll_text.clear();
	icon = icon_index::none;

	if (_key.type == group_key_type::grouped_value)
	{
		if (order == group_by::shuffle)
		{
			icon = icon_index::shuffle;
		}
		else if (order == group_by::size)
		{
			if (!_items.empty())
			{
				file_size total;

				for (const auto& ii : _items)
				{
					total += ii->file_size();
				}

				scroll_text = prop::format_magnitude(total / _items.size());
			}
		}
		else if (order == group_by::extension)
		{
			scroll_text = to_upper(_key.text1);
		}
		else if (order == group_by::location)
		{
			scroll_text = normalize_county_abbreviation(_key.text1);
		}
		else if (order == group_by::rating_label)
		{
			if (rating_reject_num == _key.order1)
			{
				icon = icon_index::del; // reject
			}
			else if (_key.order1 != 0)
			{
				const int32_t rating = rating_base_num - _key.order1;

				if (rating != 0)
				{
					scroll_text = str::to_string(rating);
				}
			}
		}
		else if (order == group_by::date_created || order == group_by::date_modified)
		{
			if (_key.order1 == 0)
			{
				icon = icon_index::today;
			}
			else if (_key.order1 == 1)
			{
				icon = icon_index::yesterday;
			}
			else
			{
				scroll_text = str::to_string(3000 - _key.order1); // year
			}
		}
		else if (order == group_by::resolution)
		{
			scroll_text = _key.text2;
		}
		else if (order == group_by::camera)
		{
			scroll_text = _key.text1;
		}
		else if (order == group_by::album_show)
		{
			scroll_text = _key.text1;
		}
		else if (order == group_by::file_type)
		{
			icon = _key.group->icon;
		}
		else if (order == group_by::presence)
		{
			icon = _key.icon;
		}
		else if (order == group_by::folder)
		{
			icon = icon_index::folder;
		}
	}
	else if (_key.type == group_key_type::other)
	{
		icon = icon_index::document;
	}
	else if (_key.type == group_key_type::folder)
	{
		icon = icon_index::folder;
	}
}

void view_state::update_item_groups()
{
	//df::assert_true(ui::is_ui_thread());

	std::map<df::group_key, df::item_group_ptr> existing_groups;
	for (const auto& g : _item_groups)
	{
		existing_groups[g->_key] = g;
	}

	df::item_set new_display_items;
	const auto is_duplicates = _search.is_duplicates();
	std::map<df::group_key, df::item_elements> groups;

	for (const auto& i : _search_items._folders)
	{
		i->row_layout_valid = false;

		if (_filter.match(i))
		{
			new_display_items.add(i);

			switch (_group_order)
			{
			case group_by::size:
				groups[size_key(i)].emplace_back(i);
				break;

			case group_by::folder:
				groups[folder_key(i)].emplace_back(i);
				break;

			case group_by::date_created:
				groups[date_key(prop::created_utc, i->media_created(), i)].emplace_back(i);
				break;

			case group_by::date_modified:
				groups[date_key(prop::modified, i->file_modified().system_to_local(), i)].emplace_back(i);
				break;

			case group_by::shuffle:
				groups[shuffle_index_key(i)].emplace_back(i);
				break;

			default:
				groups[media_type_index(i)].emplace_back(i);
				break;
			}
		}
	}

	for (const auto& i : _search_items._items)
	{
		i->row_layout_valid = false;

		if (_filter.match(i))
		{
			new_display_items.add(i);

			switch (_group_order)
			{
			case group_by::size:
				groups[size_key(i)].emplace_back(i);
				break;

			case group_by::extension:
				groups[extension_key(i)].emplace_back(i);
				break;

			case group_by::folder:
				groups[folder_key(i)].emplace_back(i);
				break;

			case group_by::location:
				groups[location_key(i)].emplace_back(i);
				break;

			case group_by::rating_label:
				groups[rating_key(i)].emplace_back(i);
				break;

			case group_by::date_created:
				groups[date_key(prop::created_utc, i->media_created(), i)].emplace_back(i);
				break;

			case group_by::date_modified:
				groups[date_key(prop::modified, i->file_modified().system_to_local(), i)].emplace_back(i);
				break;

			case group_by::resolution:
				groups[resolution_key(i)].emplace_back(i);
				break;

			case group_by::camera:
				groups[camera_key(i)].emplace_back(i);
				break;

			case group_by::album_show:
				groups[album_show_key(i)].emplace_back(i);
				break;

			case group_by::shuffle:
				groups[shuffle_index_key(i)].emplace_back(i);
				break;

			case group_by::presence:
				groups[presence_key(i->presence())].emplace_back(i);
				break;

			default:
				groups[media_type_index(i)].emplace_back(i);
				break;
			}
		}
	}

	df::item_groups new_item_groups;
	new_item_groups.reserve(groups.size());

	for (auto&& i : groups)
	{
		df::item_group_ptr b;
		auto found_group = existing_groups.find(i.first);

		if (found_group != existing_groups.end())
		{
			b = found_group->second;
			b->items(std::move(i.second));
		}
		else
		{
			const auto is_detail_display = i.first.type == df::group_key_type::folder
				                               ? setting.detail_folder_items
				                               : setting.detail_file_items;
			b = std::make_shared<df::item_group>(*this, std::move(i.second),
			                                     is_detail_display
				                                     ? df::item_group_display::detail
				                                     : df::item_group_display::icons, i.first);
			b->padding(0);
			b->margin(8, 0);
		}

		b->update_scroll_info(_group_order);
		b->sort(_group_order, _sort_order, is_duplicates);

		new_item_groups.emplace_back(b);
	}

	groups.clear();

	if (!setting.sort_dates_descending && (_group_order == group_by::date_created || _group_order ==
		group_by::date_modified))
	{
		std::ranges::reverse(new_item_groups);
	}

	const auto new_summary_shown = new_display_items.summary();

	if (_summary_shown != new_summary_shown)
	{
		_summary_shown = new_summary_shown;
		invalidate_view(view_invalid::app_layout);
	}

	_display_items = std::move(new_display_items);
	_item_groups = std::move(new_item_groups);
}

sizei df::item_group::measure(ui::measure_context& mc, const int width_limit) const
{
	_layout_bounds.resize(_items.size());

	const auto scale_factor = mc.scale_factor;
	// maeasure and save calculated layout information
	const double base_line_height = calc_item_line_height() * scale_factor;
	const auto base_adjust = base_line_height * 1.11;

	const double cy = base_line_height + mc.padding1;
	double y = 0;
	double y_max = 0;

	if (!_items.empty())
	{
		const auto item_count = static_cast<int>(_items.size());

		if (_display == item_group_display::detail)
		{
			const auto line_height = mc.text_line_height(ui::style::font_face::dialog);

			for (auto i = 0; i < item_count; ++i)
			{
				const auto& item = _items[i];
				const auto element_padding = item->porch() * scale_factor;
				const auto has_title = item->has_title();
				const auto line_count = 1 + (_show_folder ? 1 : 0) + (has_title ? 1 : 0);

				const auto height = (element_padding.cy * 2) + (line_height * line_count);
				_layout_bounds[i] = recti(element_padding.cx, round(y + element_padding.cy),
				                          width_limit - element_padding.cx, round(y + height - element_padding.cy));
				y_max = y + height;
				y += height;
			}
		}
		else
		{
			const auto max_cols = 100;
			const auto gap = 2.0 * scale_factor;
			double dst_widths[max_cols];
			double src_widths[max_cols];

			auto total_adjust_cx_ratio = 0.0;
			auto count_adjust_cx_ratio = 0;
			auto prev_col_count = 0;
			const auto default_cols = std::max(1.0, width_limit / (base_line_height + (mc.padding1 * 2.0)));
			const double x_limit = width_limit;
			auto n = 0;

			while (n < item_count)
			{
				double x = 0;
				int col_count = 0;				
				const int line_start = n;
				double total_src_width = 0.0;

				// calc line
				while (n < item_count && col_count < max_cols)
				{
					const auto& item = _items[n];
					double cx = (width_limit - (mc.padding1 * 3)) / default_cols;

					src_widths[col_count] = cx;
					dst_widths[col_count] = cx;

					auto dims = item->thumbnail_dims();
					const auto orientation = item->thumbnail_orientation();

					if (!dims.is_empty())
					{
						if (setting.show_rotated && flips_xy(orientation))
						{
							std::swap(dims.cx, dims.cy);
						}

						cx = std::max(20.0, dims.cx * std::min(cy, static_cast<double>(dims.cy)) / dims.cy);
						if (cx > dims.cx) cx = dims.cx;
						src_widths[col_count] = dims.cx;
						dst_widths[col_count] = cx;
					}

					if (col_count < 1 || (x + (cx / 1.5)) <= x_limit)
					{
						total_src_width += src_widths[col_count];
						col_count += 1;
						n += 1;
						x += cx;
					}
					else
					{
						break;
					}
				}

				// post processing on line
				if (col_count > 0)
				{					
					const auto is_end_break = n == item_count;
					const auto avail_cx = (width_limit - mc.padding1);
					const double extra_cx = avail_cx - (gap * (col_count - 1)) - x;
					auto cy_av = 0.0;
					auto total_width = 0.0;
					auto x_gap = gap;

					for (int nn = 0; nn < col_count; nn++)
					{
						const auto& item = _items[line_start + nn];						
						auto adjust_cx = std::min(base_adjust, extra_cx * (src_widths[nn] / total_src_width));
						// dont adjust cover art
						if (item->has_cover_art()) adjust_cx = 0;

						if (is_end_break)
						{
							// Limit maximum size of adjust for last line of group
							// Unless we are reducing in size already
							if (adjust_cx > 0.0)
							{
								if (prev_col_count > 1 && count_adjust_cx_ratio > 0)
								{
									// use average of previous lines
									const auto av_adjust_cx = total_adjust_cx_ratio / count_adjust_cx_ratio;
									adjust_cx = std::min(av_adjust_cx, adjust_cx);
								}
							}
						}
						else
						{
							total_adjust_cx_ratio += adjust_cx;
							count_adjust_cx_ratio += 1;
						}
						
						const auto cx = std::max(20.0 * scale_factor, dst_widths[nn] + adjust_cx);						
						const auto orientation = item->thumbnail_orientation();
						auto dims = item->thumbnail_dims();

						if (!dims.is_empty())
						{
							if (setting.show_rotated && flips_xy(orientation))
							{
								std::swap(dims.cx, dims.cy);
							}

							cy_av += mul_div(cx, dims.cy, dims.cx);
						}
						else
						{
							cy_av += cy;
						}

						dst_widths[nn] = cx;
						total_width += cx;
					}

					if (!is_end_break)
					{
						x_gap = std::clamp((avail_cx - total_width) / (col_count - 1), gap, gap * 5.5);
					}

					const double cy_line = std::clamp(cy_av / col_count, cy * 0.77, cy * 1.33);

					double xx = 0;

					for (int nn = 0; nn < col_count; nn++)
					{
						const auto cx = dst_widths[nn];
						_layout_bounds[line_start + nn] = recti(round(xx), round(y), round(xx + cx), round(y + cy_line));
						xx += cx + x_gap;
					}

					y += cy_line + gap;
					y_max = std::max(y_max, y + mc.padding1);
				}

				prev_col_count = col_count;
			}
		}
	}

	return {width_limit, round(y_max)};
}

static bool can_show_flag(const df::item_display_info& info)
{
	return !is_empty(info.label) || info.rating == -1;
}

static void draw_flag(ui::draw_context& dc, const df::item_display_info& info, const recti logical_bounds,
                      const float a)
{
	const auto label = info.label;
	const auto rating = info.rating;

	auto icon = icon_index::flag;
	const ui::color32 flag_clr = 0;
	ui::color32 label_clr = 0;

	if (rating == -1)
	{
		icon = icon_index::del;
	}

	if (!is_empty(label))
	{
		if (icmp(label, label_select_text) == 0) label_clr = color_label_select;
		else if (icmp(label, label_second_text) == 0) label_clr = color_label_second;
		else if (icmp(label, label_approved_text) == 0) label_clr = color_label_approved;
		else if (icmp(label, label_review_text) == 0) label_clr = color_label_review;
		else if (icmp(label, label_to_do_text) == 0) label_clr = color_label_to_do;
		else label_clr = ui::average(dc.colors.foreground, dc.colors.background);
	}

	if (rating < 0)
	{
		label_clr = color_rate_rejected;
	}

	const bool has_clr = label_clr || flag_clr;
	const auto alpha = has_clr ? a : a / 4.0f;
	ui::color bg;

	if (has_clr)
	{
		/*if (label_clr && flag_clr)
		{
			auto logical_bounds1 = logical_bounds;
			auto logical_bounds2 = logical_bounds;
			logical_bounds1.right = logical_bounds2.left = (logical_bounds.left + logical_bounds.right) / 2;

			dc.draw_rounded_rect(logical_bounds1, ui::color(flag_clr, dc.colors.alpha));
			dc.draw_rounded_rect(logical_bounds2, ui::color(label_clr, dc.colors.alpha));
		}
		else*/
		if (flag_clr)
		{
			bg = ui::color(flag_clr, a);
		}
		else
		{
			bg = ui::color(label_clr, a);
		}
	}

	xdraw_icon(dc, icon, logical_bounds, ui::color(dc.colors.foreground, alpha), bg);
}

void df::item_group::layout(ui::measure_context& mc, const recti bounds_in, ui::control_layouts& positions)
{
	bounds = bounds_in;

	if (_layout_bounds.size() == _items.size())
	{
		for (auto i = 0u; i < _items.size(); ++i)
		{
			_items[i]->layout(mc, _layout_bounds[i].offset(bounds.left, bounds.top), positions);
		}
	}

	if (_display == item_group_display::detail)
	{
		update_row_layout(mc);
	}
}

void df::item_group::display(const item_group_display d)
{
	if (_display != d)
	{
		_display = d;
		_state.invalidate_view(view_invalid::view_layout);
	}
}


void df::item_group::toggle_display()
{
	display(_display == item_group_display::icons ? item_group_display::detail : item_group_display::icons);

	if (_key.type == group_key_type::folder)
	{
		setting.detail_folder_items = _display == item_group_display::detail;
	}
}


void df::item_group::update_row_layout(ui::measure_context& mc) const
{
	_row_draw_info.icon.width = _row_draw_info.icon.extent;
	_row_draw_info.disk.width = _row_draw_info.disk.val_max > 1 ? _row_draw_info.disk.extent : 0;
	_row_draw_info.track.width = _row_draw_info.track.extent;
	_row_draw_info.title.width = _row_draw_info.title.extent;
	_row_draw_info.flag.width = _row_draw_info.flag.extent;
	_row_draw_info.presence.width = _row_draw_info.presence.extent;
	_row_draw_info.sidecars.width = _row_draw_info.sidecars.extent;
	_row_draw_info.items.width = _row_draw_info.items.extent;
	_row_draw_info.info.width = _row_draw_info.info.extent;
	_row_draw_info.duration.width = _row_draw_info.duration.extent;
	_row_draw_info.file_size.width = _row_draw_info.file_size.extent;
	_row_draw_info.bitrate.width = _row_draw_info.bitrate.extent;
	_row_draw_info.pixel_format.width = _row_draw_info.pixel_format.extent;
	_row_draw_info.dimensions.width = _row_draw_info.dimensions.extent;
	_row_draw_info.audio_sample_rate.width = _row_draw_info.audio_sample_rate.extent;
	_row_draw_info.modified.width = _row_draw_info.modified.extent;
	_row_draw_info.created.width = _row_draw_info.created.extent;

	const auto text_padding = df::round(item_draw_info::_text_padding * mc.scale_factor);
	const auto width_avail = (bounds.width() - mc.padding1 * 5);
	const auto half_width_avail = width_avail / 2;
	const auto expendable_title_width = (_row_draw_info.title.width > half_width_avail)
		                                    ? _row_draw_info.title.width - half_width_avail
		                                    : 0;

	if (width_avail < _row_draw_info.total(text_padding)) _row_draw_info.title.width -= expendable_title_width;
	if (width_avail < _row_draw_info.total(text_padding)) _row_draw_info.info.width = _row_draw_info.disk.width = 0;
	if (width_avail < _row_draw_info.total(text_padding)) _row_draw_info.file_size.width = 0;
	if (width_avail < _row_draw_info.total(text_padding)) _row_draw_info.duration.width = 0;
	if (width_avail < _row_draw_info.total(text_padding)) _row_draw_info.track.width = 0;
	if (width_avail < _row_draw_info.total(text_padding)) _row_draw_info.audio_sample_rate.width = 0;
	if (width_avail < _row_draw_info.total(text_padding)) _row_draw_info.bitrate.width = 0;
	if (width_avail < _row_draw_info.total(text_padding)) _row_draw_info.dimensions.width = 0;
	if (width_avail < _row_draw_info.total(text_padding)) _row_draw_info.pixel_format.width = 0;
	if (width_avail < _row_draw_info.total(text_padding)) _row_draw_info.title.width = width_avail;
	if (width_avail < _row_draw_info.total(text_padding)) _row_draw_info.items.width = 0;
	if (width_avail < _row_draw_info.total(text_padding)) _row_draw_info.modified.width = 0;
	if (width_avail < _row_draw_info.total(text_padding)) _row_draw_info.created.width = 0;
	if (width_avail < _row_draw_info.total(text_padding)) _row_draw_info.sidecars.width = 0;
	if (width_avail < _row_draw_info.total(text_padding)) _row_draw_info.presence.width = 0;
	if (width_avail < _row_draw_info.total(text_padding)) _row_draw_info.flag.width = 0;

	if (_row_draw_info.total(text_padding) < width_avail)
	{
		_row_draw_info.title.width += width_avail - _row_draw_info.total(text_padding);
	}
}

void df::item_group::update_detail_row_layout(ui::draw_context& dc, const item_element_ptr& i, bool has_related) const
{
	const auto info = i->populate_info();

	_row_draw_info.icon.extent = std::max(_row_draw_info.icon.extent, dc.icon_cxy);

	if (info.disk != 0)
	{
		_row_draw_info.disk.update_extent(dc, str::to_string(info.disk));
	}

	if (info.track != 0)
	{
		_row_draw_info.track.update_extent(dc, str::to_string(info.track), info.track);
	}

	_row_draw_info.title.update_extent(dc, combine2(info.title, info.name));

	if (can_show_flag(info))
	{
		_row_draw_info.flag.extent = std::max(_row_draw_info.flag.extent, dc.icon_cxy);
	}

	if (!has_related && info.presence != item_presence::unknown)
	{
		_row_draw_info.presence.update_extent(dc, str::format_count(info.duplicates, true));
	}

	if (info.sidecars > 0)
	{
		_row_draw_info.sidecars.update_extent(dc, str::format_count(info.sidecars));
	}

	if (!str::is_empty(info.info))
	{
		_row_draw_info.info.update_extent(dc, info.info);
	}

	if (info.items > 0)
	{
		_row_draw_info.items.update_extent(dc, str::format_count(info.items), info.items);
	}

	if (info.duration > 0)
	{
		_row_draw_info.duration.update_extent(dc, prop::format_duration(info.duration), info.duration);
	}

	if (!info.size.is_empty())
	{
		_row_draw_info.file_size.update_extent(dc, prop::format_size(info.size), info.size.to_int64());
	}

	if (!is_empty(info.bitrate))
	{
		_row_draw_info.bitrate.update_extent(dc, info.bitrate);
	}

	if (!is_empty(info.pixel_format))
	{
		_row_draw_info.pixel_format.update_extent(dc, info.pixel_format);
	}

	if (!prop::is_null(info.dimensions))
	{
		_row_draw_info.dimensions.update_extent(dc, prop::format_pixels(info.dimensions, i->file_type()));
	}

	if (!prop::is_null(info.audio_sample_rate))
	{
		_row_draw_info.audio_sample_rate.update_extent(dc, prop::format_audio_sample_rate(info.audio_sample_rate));
	}

	if (_state.group_order() == group_by::date_modified)
	{
		_row_draw_info.modified.update_extent(dc, platform::format_date_time(info.modified));
	}

	if (_state.group_order() == group_by::date_created)
	{
		_row_draw_info.created.update_extent(dc, platform::format_date_time(info.created));
	}

	i->row_layout_valid = true;
}

void df::item_group::render(ui::draw_context& dc, const pointi element_offset) const
{
}

void df::item_group::scroll_tooltip(const ui::const_image_ptr& thumbnail, view_elements& elements) const
{
	const auto max_thumb_dim = 80;
	files ff;

	if (is_valid(thumbnail))
	{
		elements.add(std::make_shared<surface_element>(ff.image_to_surface(thumbnail), max_thumb_dim,
		                                               view_element_style::center));
	}
	else
	{
		for (const auto& i : _items)
		{
			if (i->has_thumb())
			{
				elements.add(std::make_shared<surface_element>(ff.image_to_surface(i->thumbnail()), max_thumb_dim,
				                                               view_element_style::center));
				break;
			}
		}
	}

	if (_scroll_tooltip_rating != 0)
	{
		elements.add(make_icon_element(icon_index::star_solid, _scroll_tooltip_rating, view_element_style::line_break));
	}

	for (const auto& t : _scroll_tooltip_text)
	{
		elements.add(std::make_shared<text_element>(t, ui::style::font_face::dialog, ui::style::text_style::multiline,
		                                            view_element_style::center | view_element_style::new_line));
	}
}

void df::item_group::tooltip(view_hover_element& hover, const pointi loc, const pointi element_offset) const
{
	for (const auto& i : _items)
	{
		if (i->bounds.offset(element_offset).contains(loc))
		{
			i->tooltip(hover, loc, element_offset);
		}
	}
}

view_controller_ptr df::item_group::controller_from_location(const view_host_ptr& host, const pointi loc,
                                                             const pointi element_offset,
                                                             const std::vector<recti>& excluded_bounds)
{
	view_controller_ptr result;

	if (!result)
	{
		for (const auto& i : _items)
		{
			if (i->bounds.offset(element_offset).contains(loc))
			{
				result = i->controller_from_location(host, loc, element_offset, {});
				if (result) break;
			}
		}
	}

	return result;
}

df::item_element_ptr df::item_group::drawable_from_layout_location(const pointi loc) const
{
	for (const auto& i : _items)
	{
		if (i->bounds.contains(loc))
		{
			return i;
		}
	}

	return nullptr;
}


void df::index_file_item::update_duplicates(const index_folder_item_ptr& f, const duplicate_info dup_info) const
{
	if (duplicates.count != dup_info.count || duplicates.group != dup_info.group)
	{
		duplicates.count = dup_info.count;
		duplicates.group = dup_info.group;

		if (dup_info.count > 1)
		{
			bloom.types |= bloom_bits::flag;
		}
		else
		{
			bloom.types &= ~bloom_bits::flag;
		}

		f->update_bloom_bits(*this);
	}
}

void df::index_file_item::calc_bloom_bits() const
{
	bloom = {};
	bloom.types |= ft->group->bloom_bit();

	const auto md = metadata.load();

	if (md)
	{
		bloom |= md->calc_bloom_bits();
	}

	if (duplicates.group != 0)
	{
		bloom.types |= bloom_bits::flag;
	}
}

void df::item_element::render_bg(ui::draw_context& dc, const item_group& group, const pointi element_offset) const
{
	const auto& s = group._state;
	const auto bg_color = calc_background_color(dc);
	const auto display = group.display();
	const auto device_bounds = bounds.offset(element_offset);
	const auto is_focus = s.focus_item().get() == this;
	const auto is_hover = is_style_bit_set(view_element_style::hover);
	const auto extra_padding = df::round((is_focus || is_hover ? 4 : 0) * dc.scale_factor);
	const auto pad = padding * dc.scale_factor;
	const auto bg_padding = sizei(pad.cx + extra_padding, pad.cy + extra_padding);
	const auto bg_bounds = device_bounds.inflate(bg_padding.cx, bg_padding.cy);

	if (display == item_group_display::detail)
	{
		if (bg_color.a > 0.0f)
		{
			dc.draw_rounded_rect(bg_bounds, bg_color, dc.padding1);
		}
	}
	else
	{
		if (bg_color.a > 0.0f)
		{
			dc.draw_rounded_rect(bg_bounds, bg_color, dc.padding1);
		}
	}
}

void df::item_element::render(ui::draw_context& dc, const item_group& group, const pointi element_offset) const
{
	const auto& s = group._state;
	const auto display = group.display();
	const auto group_order = s.group_order();
	const auto sort_order = s.sort_order();
	const auto info = populate_info();
	const auto has_related = s.search().has_related();

	const auto text_padding = df::round(item_draw_info::_text_padding * dc.scale_factor);
	const auto device_bounds = bounds.offset(element_offset);

	const auto* const ft = file_type();
	const auto is_highlight = is_style_bit_set(view_element_style::highlight);
	const auto is_hover = is_style_bit_set(view_element_style::hover);
	const auto is_folder = ft->has_trait(file_type_traits::folder);
	const auto is_focus = s.focus_item().get() == this;
	const auto is_selected = this->is_selected();

	const auto is_error = this->is_error();
	const auto show_folder = group._show_folder;

	const auto text_font = ui::style::font_face::dialog;
	const auto text_line_height = dc.text_line_height(text_font);
	const auto cxy_flag = std::max(dc.icon_cxy + text_padding, text_line_height + text_padding);

	const auto background_is_highlighted = is_focus || is_highlight || is_hover || is_selected || is_error;
	const auto bg_color = calc_background_color(dc);
	const auto pad = padding * dc.scale_factor;

	if (alt_background)
	{
		dc.draw_rounded_rect(device_bounds.inflate(pad.cx, pad.cy), ui::color(0, 0.1f), dc.padding1);
	}

	if (display == item_group_display::detail)
	{
		const auto alpha = dc.colors.alpha;
		const auto text_color = ui::color(dc.colors.foreground, alpha);
		const auto group_text_color = ui::color(ft->text_color(dc.colors.foreground), alpha).emphasize(background_is_highlighted);
		const auto bg_disk = ui::color(ui::style::color::view_selected_background, dc.colors.alpha * 0.77f);
		const auto bg_dups = ui::color(ui::style::color::duplicate_background, dc.colors.alpha * 0.77f);
		const auto bg_sidecars = ui::color(ui::style::color::sidecar_background, dc.colors.alpha * 0.77f);

		if (device_bounds.width() > 100)
		{
			const auto& widths = group.row_widths();

			recti row_bounds = device_bounds;
			// (device_bounds.left + 6, device_bounds.top + thumb_padding, device_bounds.right - 6, device_bounds.bottom - thumb_padding);

			auto image_rect = row_bounds;
			image_rect.right = image_rect.left + widths.icon.extent + text_padding;

			/*if (has_thumb && image_rect.height() > 32)
			{
				this->draw_image(dc, image_rect, 0);
			}
			else*/
			{
				xdraw_icon(dc, info.icon, image_rect, text_color, {});
			}

			row_bounds.left = image_rect.right + text_padding;

			auto text_rect = row_bounds;
			auto folder_rect = row_bounds;

			if (show_folder)
			{
				const auto line_count = 2 + (info.title.is_empty() ? 0 : 1);
				const auto line_height = text_rect.height() / line_count;
				folder_rect.top = text_rect.bottom -= line_height;
			}

			auto text_style = ui::style::text_style::single_line;
			auto text_style_far = ui::style::text_style::single_line_far;
			auto text_style_center = ui::style::text_style::single_line_center;
			auto text_x = text_rect.left;
			auto text_y = pad.cy;

			if (widths.disk.width > 0)
			{
				if (info.disk != 0)
				{
					const auto bb = widths.disk.calc_bounds(text_rect, text_x, text_y, text_padding);
					widths.disk.draw(dc, str::to_string(info.disk), ui::color{}, bb, text_font, text_style_far,
					                 text_color);
				}
				text_x += widths.disk.width + text_padding;
			}

			if (widths.track.width > 0)
			{
				if (info.track != 0)
				{
					const auto bb = widths.track.calc_bounds(text_rect, text_x, text_y, text_padding);
					widths.track.draw(dc, str::to_string(info.track), ui::color{}, bb, text_font, text_style_far,
					                  text_color);
				}
				text_x += widths.track.width + text_padding;
			}

			if (widths.title.width > 0)
			{
				auto title_rect = text_rect;
				title_rect.left = text_x;
				title_rect.right = text_x + widths.title.width + text_padding;

				if (info.online_status != item_online_status::disk)
				{
					const auto bb = center_rect(sizei{cxy_flag, cxy_flag},
					                            recti(title_rect.right - cxy_flag, title_rect.top, title_rect.right,
					                                  title_rect.bottom));
					xdraw_icon(dc, icon_index::cloud, bb, text_color, bg_disk);
					title_rect.right -= cxy_flag;
				}

				if (!info.title.is_empty() && title_rect.height() >= (2 * text_line_height))
				{
					auto name_bounds = title_rect;
					auto title_bounds = title_rect;
					name_bounds.top = title_bounds.bottom = (title_rect.top + title_rect.bottom) / 2;
					name_bounds.left = title_bounds.left += text_padding / 2;
					dc.draw_text(info.title, title_bounds, text_font, text_style, text_color, {});
					dc.draw_text(info.name, name_bounds, text_font, text_style, group_text_color, {});
				}
				else
				{
					dc.draw_text(info.name, title_rect, text_font, text_style, group_text_color, {});
				}

				text_x += widths.title.width + text_padding;
			}

			if (widths.flag.width > 0)
			{
				if (can_show_flag(info))
				{
					const auto bb = widths.flag.calc_bounds(text_rect, text_x, text_y, text_padding);
					draw_flag(dc, info, bb, dc.colors.alpha);
				}
				text_x += widths.flag.width + text_padding;
			}


			if (widths.presence.width > 0)
			{
				const auto bb = widths.presence.calc_bounds(text_rect, text_x, text_y, text_padding);
				widths.presence.draw(dc, str::format_count(info.duplicates, true), bg_dups, bb, text_font,
				                     text_style_center, text_color);
				text_x += widths.presence.width + text_padding;
			}

			if (widths.sidecars.width > 0)
			{
				if (info.sidecars > 0)
				{
					const auto bb = widths.sidecars.calc_bounds(text_rect, text_x, text_y, text_padding);
					widths.sidecars.draw(dc, str::format_count(info.sidecars), bg_sidecars, bb, text_font,
					                     text_style_center, text_color);
				}
				text_x += widths.sidecars.width + text_padding;
			}

			if (widths.items.width > 0)
			{
				if (info.items > 0)
				{
					const auto bb = widths.items.calc_bounds(text_rect, text_x, text_y, text_padding);
					widths.items.draw(dc, str::format_count(info.items), info.items, bb, text_font, text_style_far,
					                  text_color);
				}
				text_x += widths.items.width + text_padding;
			}

			if (widths.duration.width > 0)
			{
				if (info.duration > 0)
				{
					const auto bb = widths.duration.calc_bounds(text_rect, text_x, text_y, text_padding);
					const auto text = prop::format_duration(info.duration);
					widths.duration.draw(dc, text, info.duration, bb, text_font, text_style_far, text_color);
				}
				text_x += widths.duration.width + text_padding;
			}

			if (widths.file_size.width > 0)
			{
				if (!info.size.is_empty())
				{
					const auto bb = widths.file_size.calc_bounds(text_rect, text_x, text_y, text_padding);
					const auto text = prop::format_size(info.size);
					widths.file_size.draw(dc, text, info.size.to_int64(), bb, text_font, text_style_far, text_color);
				}
				text_x += widths.file_size.width + text_padding;
			}

			if (widths.bitrate.width > 0)
			{
				if (!prop::is_null(info.bitrate))
				{
					const auto bb = widths.bitrate.calc_bounds(text_rect, text_x, text_y, text_padding);
					widths.bitrate.draw(dc, info.bitrate, ui::color{}, bb, text_font, text_style_far, text_color);
				}
				text_x += widths.info.width + text_padding;
			}

			if (widths.info.width > 0)
			{
				if (!prop::is_null(info.info))
				{
					const auto bb = widths.info.calc_bounds(text_rect, text_x, text_y, text_padding);
					widths.info.draw(dc, info.info, ui::color{}, bb, text_font, text_style_far, text_color);
				}
				text_x += widths.info.width + text_padding;
			}

			if (widths.dimensions.width > 0)
			{
				if (!prop::is_null(info.dimensions))
				{
					const auto bb = widths.dimensions.calc_bounds(text_rect, text_x, text_y, text_padding);
					const auto text = prop::format_pixels(info.dimensions, file_type());
					widths.dimensions.draw(dc, text, ui::color{}, bb, text_font, text_style_far, text_color);
				}
				text_x += widths.dimensions.width + text_padding;
			}

			if (widths.pixel_format.width > 0)
			{
				if (!is_empty(info.pixel_format))
				{
					const auto bb = widths.pixel_format.calc_bounds(text_rect, text_x, text_y, text_padding);
					widths.pixel_format.draw(dc, info.pixel_format, ui::color{}, bb, text_font, text_style, text_color);
				}
				text_x += widths.pixel_format.width + text_padding;
			}

			if (widths.audio_sample_rate.width > 0)
			{
				if (!prop::is_null(info.audio_sample_rate))
				{
					const auto bb = widths.audio_sample_rate.calc_bounds(text_rect, text_x, text_y, text_padding);
					const auto text = prop::format_audio_sample_rate(info.audio_sample_rate);
					widths.audio_sample_rate.draw(dc, text, ui::color{}, bb, text_font, text_style_far, text_color);
				}
				text_x += widths.audio_sample_rate.width + text_padding;
			}

			if (widths.created.width > 0)
			{
				if (!prop::is_null(info.created))
				{
					const auto bb = widths.created.calc_bounds(text_rect, text_x, text_y, text_padding);
					const auto text = platform::format_date_time(info.created);
					widths.created.draw(dc, text, ui::color{}, bb, text_font, text_style_far, text_color);
				}
				text_x += widths.created.width + text_padding;
			}

			if (widths.modified.width > 0)
			{
				if (!prop::is_null(info.modified))
				{
					const auto bb = widths.modified.calc_bounds(text_rect, text_x, text_y, text_padding);
					const auto text = platform::format_date_time(info.modified);
					widths.modified.draw(dc, text, ui::color{}, bb, text_font, text_style_far, text_color);
				}
				text_x += widths.modified.width + text_padding;
			}

			if (show_folder)
			{
				folder_rect.left += text_padding / 2;
				dc.draw_text(info.folder, folder_rect, text_font, text_style, text_color, {});
			}
		}
	}
	else if (display == item_group_display::icons)
	{
		const auto thumbnail = _thumbnail;
		const auto cover_art = _cover_art;
		const auto thumb_is_valid = is_valid(thumbnail) || is_valid(cover_art);
		const auto show_text = is_hover || !thumb_is_valid || is_folder || is_focus;
		const auto expand_text = (is_hover || is_focus) && thumb_is_valid;
		const auto show_stars = show_text && group_order != group_by::rating_label;
		const auto show_info = (is_folder || is_hover) && !str::is_empty(info.info);
		const auto show_size = show_text && !is_folder && (group_order == group_by::size || sort_order ==
			sort_by::size);
		const auto show_created = show_text && !is_folder && group_order == group_by::date_created;
		const auto show_modified = show_text && !is_folder && (group_order == group_by::date_modified || sort_order ==
			sort_by::date_modified);
		const auto show_selected = !(is_focus || is_hover) && (is_selected || is_highlight);
		const auto text_style = ui::style::text_style::single_line_center;
		const auto stars_line_height = std::max(text_line_height, dc.icon_cxy) + dc.padding2;
		const auto extra_padding = df::round((is_focus || is_hover ? 4 : 0) * dc.scale_factor);
		const auto bg_padding = sizei(pad.cx + extra_padding, pad.cy + extra_padding);
		const auto bg_bounds = device_bounds.inflate(bg_padding.cx, bg_padding.cy);
		const auto text_alpha = thumb_is_valid && !show_text ? bg_color.a * dc.colors.alpha : dc.colors.alpha;
		const auto text_color = ui::color(dc.colors.foreground, text_alpha);
		const auto group_text_color = ui::color(ft->text_color(dc.colors.foreground), text_alpha).emphasize(background_is_highlighted);
		const auto bg_disk = ui::color(ui::style::color::view_selected_background, text_alpha * 0.77f);
		const auto bg_dups = ui::color(ui::style::color::duplicate_background, text_alpha * 0.77f);
		const auto bg_sidecars = ui::color(ui::style::color::sidecar_background, text_alpha * 0.77f);
		const auto thumb_padding = df::round(item_draw_info::_thumb_padding * dc.scale_factor);
		auto title_style = ui::style::text_style::single_line_center;
		auto title_line_height2 = dc.text_line_height(info.title_font);
		auto title_line_height1 = is_empty(info.title) ? 0 : title_line_height2;
		auto title_text_extra_width = 0;

		if (is_focus || is_hover)
		{
			title_style = ui::style::text_style::multiline_center;
			auto avail_width = is_hover ? mul_div(bg_bounds.width(), 7, 4) : bg_bounds.width();
			const auto title_extent1 = is_empty(info.title)
				                           ? sizei{}
				                           : dc.measure_text(info.title, info.title_font, title_style, avail_width);
			const auto title_extent2 = dc.measure_text(info.name, info.title_font, title_style, avail_width);
			const auto extra1 = title_extent1.cx > bg_bounds.width() ? title_extent1.cx - device_bounds.width() : 0;
			const auto extra2 = title_extent2.cx > bg_bounds.width() ? title_extent2.cx - device_bounds.width() : 0;
			title_text_extra_width = std::max(extra1, extra2);
			title_line_height1 = title_extent1.cy;
			title_line_height2 = title_extent2.cy;
		}

		auto size_avail = device_bounds.extent();
		size_avail.cy -= thumb_padding * 2;

		int text_height = 0;

		if (show_text)
		{
			text_height += title_line_height1;
			text_height += title_line_height2;
		}

		if (show_info)
		{
			text_height += text_line_height;
		}

		if (show_size || show_created || show_modified)
		{
			text_height += text_line_height;
		}

		if (show_stars && info.rating != 0)
		{
			text_height += stars_line_height;
		}

		auto text_rect = device_bounds;
		text_rect.top = expand_text ? bg_bounds.bottom : (bg_bounds.bottom - text_height);
		text_rect.bottom = text_rect.top + text_height;
		text_rect.left -= (title_text_extra_width / 2) + (is_hover ? text_padding : 0);
		text_rect.right += (title_text_extra_width / 2) + (is_hover ? text_padding : 0);

		if (text_rect.right > group.bounds.right)
		{
			text_rect.offset(group.bounds.right - text_rect.right, 0);
		}

		auto icon_rect = device_bounds.inflate(-thumb_padding, -thumb_padding);

		if (icon_rect.height() > text_height)
		{
			icon_rect.bottom = icon_rect.bottom - text_height;
		}

		if (thumb_is_valid)
		{
			if (!_texture)
			{
				const auto t = dc.create_texture();
				files ff;

				const auto image = !is_valid(cover_art) || is_hover ? thumbnail : cover_art;

				if (t && t->update(ff.image_to_surface(image)) != ui::texture_update_result::failed)
				{
					_texture = t;
				}
			}

			const auto tex = _texture;

			if (tex)
			{
				const auto texture_dimensions = tex->dimensions();
				const auto orientation = _thumbnail_orientation;
				const auto pad3 = df::round(3 * dc.scale_factor);
				const auto flip = setting.show_rotated && flips_xy(orientation);
				const auto rect_draw = rectd(device_bounds.left, device_bounds.top, device_bounds.width(),
				                             device_bounds.height()).inflate(show_selected ? -pad3 : 0);

				const double ww = flip ? rect_draw.Height : rect_draw.Width;
				const double hh = flip ? rect_draw.Width : rect_draw.Height;

				const double sx = ww / static_cast<double>(texture_dimensions.cx);
				const double sy = hh / static_cast<double>(texture_dimensions.cy);

				const auto texture_scale = std::max(sx, sy);
				const auto cx_tex = ww / texture_scale;
				const auto cy_tex = hh / texture_scale;
				const auto x_tex = (texture_dimensions.cx - cx_tex) / 2;
				const auto y_tex = (texture_dimensions.cy - cy_tex) / 3; // Bias to show top of image

				const rectd rect_tex(x_tex, y_tex, cx_tex, cy_tex);

				const auto dst_quad = setting.show_rotated
					                      ? quadd(rect_draw).transform(to_simple_transform(orientation))
					                      : quadd(rect_draw);

				const auto sampler = calc_sampler(rect_draw.extent().round(), rect_tex.extent().round(), orientation);

				dc.draw_texture(tex, dst_quad, rect_tex.round(), dc.colors.alpha, sampler);

				draw_texture_info(dc, rect_draw.round(), tex, orientation, sampler, dc.colors.alpha);
			}
		}
		else if (is_folder)
		{
			xdraw_icon(dc, info.icon, icon_rect, group_text_color, {});
		}
		else
		{
			auto ext = _name.substr(find_ext(_name));
			if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);

			if (str::is_empty(ext))
			{
				xdraw_icon(dc, info.icon, icon_rect, text_color, {});
			}
			else
			{
				dc.draw_text(str::to_lower(ext), icon_rect, ui::style::font_face::title,
				             ui::style::text_style::single_line_center, text_color, {});
			}
		}

		auto y = text_rect.top;

		if (show_text)
		{
			auto text_bg_bounds = text_rect;
			text_bg_bounds.left = std::min(bg_bounds.left, text_rect.left);
			text_bg_bounds.right = std::max(bg_bounds.right, text_rect.right);

			//text_bg_bounds.top = text_rect.top;
			dc.draw_rounded_rect(text_bg_bounds, ui::color(ui::style::color::view_background, text_alpha * 0.888f), dc.padding1);

			recti title_rect(text_rect.left, y, text_rect.right, y + title_line_height1 + title_line_height2);

			if (!is_empty(info.title))
			{
				auto bounds = title_rect;
				bounds.bottom = title_rect.top + title_line_height1;
				dc.draw_text(info.title, bounds, info.title_font, title_style, group_text_color, {});
				y += title_line_height1;
			}

			if (!is_empty(info.name))
			{
				auto bounds = title_rect;
				bounds.top = title_rect.top + title_line_height1;
				dc.draw_text(info.name, bounds, info.title_font, title_style, group_text_color, {});
				y += title_line_height2;
			}
		}

		if (show_info)
		{
			recti info_rect(text_rect.left, y, text_rect.right, y + text_line_height);
			dc.draw_text(info.info, info_rect, text_font, text_style, text_color, {});
			y += text_line_height;
		}

		if (show_size || show_created || show_modified)
		{
			recti order_text_rect(text_rect.left, y, text_rect.right, y + text_line_height);
			std::u8string text;

			if (show_size)
			{
				text = prop::format_size(info.size);
			}
			else if (show_created)
			{
				text = prop::format_date(info.created);
			}
			else if (show_modified)
			{
				text = prop::format_date(info.modified);
			}

			dc.draw_text(text, order_text_rect, text_font, ui::style::text_style::single_line_center, text_color, {});
			y += text_line_height;
		}

		if (show_stars)
		{
			const auto rating = info.rating;

			if (rating >= 1 && rating <= 5)
			{
				auto x = (text_rect.left + text_rect.right - (dc.icon_cxy * rating)) / 2;
				y += 2;

				for (auto ii = 0; ii < rating; ii++)
				{
					recti r(x, y, x + dc.icon_cxy, y + stars_line_height);
					xdraw_icon(dc, icon_index::star_solid, r, text_color, {});
					x += dc.icon_cxy;
				}
			}
			else if (rating < 0)
			{
				recti label_bounds(text_rect.left, y, text_rect.right, y + stars_line_height);
				dc.draw_text(tt.command_rate_rejected, label_bounds, text_font,
				             ui::style::text_style::single_line_center, ui::color(color_rate_rejected, text_alpha), {});
			}

			y += stars_line_height;
		}

		const auto y_flag = device_bounds.top - (expand_text ? cxy_flag : 0);
		auto x_flag = device_bounds.left;

		if (can_show_flag(info))
		{
			const recti bb(x_flag, y_flag, x_flag + cxy_flag, y_flag + cxy_flag);
			draw_flag(dc, info, bb, text_alpha);
			x_flag += bb.width();
		}

		if (show_text)
		{
			if (!has_related && info.presence != item_presence::unknown)
			{
				const auto text = str::format_count(info.duplicates, true);
				const auto cx = std::max(cxy_flag, dc.measure_text(text, ui::style::font_face::dialog,
				                                                   ui::style::text_style::single_line_center, 100).cx);
				const recti bb(x_flag, y_flag, x_flag + cx, y_flag + cxy_flag);
				dc.draw_text(text, bb, ui::style::font_face::dialog, ui::style::text_style::single_line_center,
				             text_color, bg_dups);
				x_flag += cx;
			}

			if (info.sidecars > 0)
			{
				const auto text = str::format_count(info.sidecars);
				const auto cx = std::max(cxy_flag, dc.measure_text(text, ui::style::font_face::dialog,
				                                                   ui::style::text_style::single_line_center, 100).cx);
				const recti bb(x_flag, y_flag, x_flag + cx, y_flag + cxy_flag);

				dc.draw_text(text, bb, ui::style::font_face::dialog, ui::style::text_style::single_line_center,
				             text_color, bg_sidecars);
				x_flag += cx;
			}

			if (info.online_status != item_online_status::disk)
			{
				const recti bb(x_flag, y_flag, x_flag + cxy_flag, y_flag + cxy_flag);
				xdraw_icon(dc, icon_index::cloud, bb, text_color, bg_disk);
				x_flag += bb.width();
			}
		}
	}
}

sizei df::item_element::measure(ui::measure_context& mc, const int width_limit) const
{
	assert_true(false);
	return {0, 0};
}

void df::item_element::layout(ui::measure_context& mc, const recti bounds_in, ui::control_layouts& positions)
{
	bounds = bounds_in;
}

view_controller_ptr df::item_element::controller_from_location(const view_host_ptr& host, const pointi loc,
                                                               const pointi element_offset,
                                                               const std::vector<recti>& excluded_bounds)
{
	return nullptr;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////


std::vector<ui::const_image_ptr> df::item_set::thumbs(const size_t max, const item_element_ptr& skip_this) const
{
	std::vector<ui::const_image_ptr> results;

	for (const auto& i : _items)
	{
		if (results.size() > max)
			break;

		if (i->has_thumb() && i != skip_this)
		{
			results.emplace_back(i->thumbnail());
		}
	}

	return results;
}

size_t df::item_set::thumb_count() const
{
	auto result = 0;

	for (const auto& i : _items)
	{
		if (i->has_thumb())
		{
			++result;
		}
	}

	return result;
}

df::process_result df::item_set::can_process(process_items_type file_types, bool mark_errors,
                                             const view_host_base_ptr& view) const
{
	process_result result;

	if (file_types != process_items_type::local_file_or_folder)
	{
		if (!_folders.empty())
		{
			for (const auto& i : _folders)
			{
				result.record_error(i, process_result_code::folder, mark_errors, view);
			}
		}
	}

	for (const auto& i : _items)
	{
		const auto* const ft = i->file_type();

		if (file_types == process_items_type::photos_only && !ft->has_trait(file_type_traits::bitmap))
		{
			result.record_error(i, process_result_code::not_photo, mark_errors, view);
		}

		if (file_types == process_items_type::can_save_pixels)
		{
			if (i->is_read_only())
			{
				result.record_error(i, process_result_code::read_only, mark_errors, view);
			}
			else if (!files::can_save(i->path()))
			{
				result.record_error(i, process_result_code::cannot_save_pixels, mark_errors, view);
			}
		}

		if (file_types == process_items_type::can_save_metadata)
		{
			if (ft->traits && file_type_traits::embedded_xmp && i->is_read_only())
			{
				result.record_error(i, process_result_code::cannot_embed_xmp, mark_errors, view);
			}

			if (!(ft->traits && file_type_traits::edit))
			{
				result.record_error(i, process_result_code::cannot_edit, mark_errors, view);
			}
		}
	}

	return result;
}

void df::folder_item::add_to(item_set& results)
{
	results.add(shared_from_this());
}

void df::folder_item::add_to(paths& results)
{
	results.folders.emplace_back(_path);
}

void df::folder_item::add_to(unique_paths& paths)
{
	paths.emplace(_path);
}

df::item_display_info df::folder_item::populate_info() const
{
	item_display_info result;
	result.info.clear();

	result.icon = _path.is_drive() ? icon_index::disk : icon_index::folder;
	result.name = _name;
	result.title_font = ui::style::font_face::title;

	result.title = _info->volume;
	result.items = _total_count;
	result.size = _size;
	result.modified = _modified.system_to_local();
	result.created = media_created();
	return result;
}

void df::file_item::add_to(item_set& results)
{
	results.add(shared_from_this());
}

void df::file_item::add_to(paths& results)
{
	results.files.emplace_back(_path);
}

void df::file_item::add_to(unique_paths& paths)
{
	paths.emplace(_path);
}

df::item_display_info df::file_item::populate_info() const
{
	const auto* const mt = file_type();

	item_display_info result;
	result.icon = icon_index::document;
	result.folder = _path.folder().text();
	result.online_status = _online_status;
	result.presence = _presence;
	result.name = _name;
	result.info.clear();

	if (!is_link())
	{
		result.size = _size;
		result.duplicates = _duplicates.count;

		const auto md = _metadata.load();

		if (md)
		{
			result.title = md->title;
			result.track = md->track.x;
			result.disk = md->disk.x;
			result.duration = md->duration;
			result.rating = md->rating;
			result.label = md->label;
			result.sidecars = static_cast<int>(sidecars_count());
			result.bitrate = md->bitrate;
			result.pixel_format = md->pixel_format;
			result.dimensions = md->dimensions();
			result.audio_channels = md->audio_channels;
			result.audio_sample_rate = md->audio_sample_rate;
			result.audio_sample_type = md->audio_sample_type;
		}

		result.icon = mt->icon;
		result.modified = _modified.system_to_local();
		result.created = media_created();
	}

	return result;
}

void df::folder_item::open(view_state& s, const view_host_base_ptr& view) const
{
	s.open(view, search_t().add_selector(_path), {});
}

void df::file_item::open(view_state& s, const view_host_base_ptr& view) const
{
	if (!is_media())
	{
		platform::open(path());
	}
}


////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void df::file_item::update(const file_path path, const index_file_item& info) noexcept
{
	const auto md = info.metadata.load();

	_name = path.name();
	_online_status = info.calc_online_status();
	_is_read_only = (info.flags && index_item_flags::is_read_only);
	_path = path;
	_ft = info.ft;
	_size = info.size;
	_modified = info.file_modified;
	_created = info.file_created;
	_duplicates = info.duplicates;
	_metadata = md;
	_crc32c = info.crc32c;
	_media_created = calc_media_created();
	row_layout_valid = false;

	if (!_ft && !is_empty(_name))
	{
		_ft = files::file_type_from_name(_name);
	}

	if (md && !ui::is_valid(_thumbnail) && !ui::is_valid(_cover_art))
	{
		_thumbnail_dims = md->dimensions();
		_thumbnail_orientation = md->orientation;
	}
}

void df::folder_item::calc_folder_summary(cancel_token token)
{
	const auto total = platform::calc_folder_summary(_path, setting.show_hidden, token);
	_size = total.size;
	_total_count = total.count;
}

platform::file_op_result df::folder_item::rename(index_state& index, const std::u8string_view new_name)
{
	const auto path_src = path();
	const auto path_dst = path_src.parent().combine(new_name);

	if (path_src == path_dst)
	{
		// no-op
		platform::file_op_result result;
		result.code = platform::file_op_result_code::OK;
		return result;
	}

	if (!path_dst.is_save_valid())
	{
		platform::file_op_result result;
		result.error_message = format_invalid_name_message(new_name);
		return result;
	}

	auto result = platform::move_file(path_src, path_dst);

	if (result.success())
	{
		_path = path_dst;
		_name = path_dst.name();
	}

	return result;
}

platform::file_op_result df::file_item::rename(index_state& index, const std::u8string_view new_name)
{
	const auto path_src = path();
	const auto path_dst = file_path(path_src.folder(), new_name, path_src.extension());

	if (path_src == path_dst)
	{
		// no-op
		platform::file_op_result result;
		result.code = platform::file_op_result_code::OK;
		return result;
	}

	if (!path_dst.is_save_valid())
	{
		platform::file_op_result result;
		result.error_message = format_invalid_name_message(new_name);
		return result;
	}

	auto result = platform::move_file(path_src, path_dst, true);

	if (result.success())
	{
		const auto sidecar_parts = split(sidecars(), true);

		for (const auto& file_name : sidecar_parts)
		{
			const auto folder_path = path_src.folder();
			const auto sidecar_path_src = folder_path.combine_file(file_name);
			const auto sidecar_path_dst = folder_path.combine_file(new_name).extension(sidecar_path_src.extension());

			auto sidecar_result = platform::move_file(sidecar_path_src, sidecar_path_dst, true);

			if (sidecar_result.failed())
			{
				return sidecar_result;
			}
		}

		_path = file_path(_path.folder(), _name = path_dst.name());
	}

	return result;
}


/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
