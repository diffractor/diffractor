// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: View-geometry tests. Covers the zoom model (fit, stepping, anchoring, panning and the
// navigator) and the item selector / view scroller widgets. Pure geometry: no file, codec or
// database access, so these run in microseconds and stay apart from the media-edit tests.

#include "pch.h"
#include "test_utils.h"
#include "model_zoom.h"
#include "view_items.h"
#include "view_selector.h"

static void assert_zoom_near(const double expected, const double actual, const std::string_view message)
{
	assert_equal(true, std::abs(expected - actual) < 0.000001, message);
}

static void should_select_settled_zoom_sampler()
{
	assert_equal(true, calc_sampler({1000, 500}, {1000, 500}, ui::orientation::top_left) ==
	             ui::texture_sampler::point, "one-to-one is exact");
	assert_equal(true, calc_sampler({3000, 1500}, {1000, 500}, ui::orientation::top_left) ==
	             ui::texture_sampler::bicubic, "three times remains smooth");
	assert_equal(true, calc_sampler({3001, 1500}, {1000, 500}, ui::orientation::top_left) ==
	             ui::texture_sampler::point, "above three times keeps source pixels exact");
	assert_equal(true, calc_sampler({3001, 1500}, {1000, 500}, ui::orientation::top_left, false, true) ==
	             ui::texture_sampler::bicubic, "a magnified stand-in stays smooth rather than blocky");
	assert_equal(true, calc_sampler({1000, 500}, {1000, 500}, ui::orientation::top_left, false, true) ==
	             ui::texture_sampler::point, "a stand-in at one-to-one is still exact");
	assert_equal(true, calc_sampler({1500, 750}, {1000, 500}, ui::orientation::top_left, true) ==
	             ui::texture_sampler::bilinear, "interactive magnification uses fast sampler");
	assert_equal(true, calc_sampler({1500, 750}, {1000, 500}, ui::orientation::top_left) ==
	             ui::texture_sampler::bicubic, "settled magnification uses quality sampler");
}

static void should_accumulate_precision_zoom_wheel_deltas()
{
	auto pending = 0.0;
	assert_equal(0, df::zoom_view_state::accumulate_wheel_steps(pending, 20.0), "first partial detent");
	assert_equal(0, df::zoom_view_state::accumulate_wheel_steps(pending, 20.0), "second partial detent");
	assert_equal(0, df::zoom_view_state::accumulate_wheel_steps(pending, 19.0), "detent not rounded early");
	assert_equal(1, df::zoom_view_state::accumulate_wheel_steps(pending, 1.0), "exact detent steps once");
	assert_zoom_near(0.0, pending, "positive detent consumed exactly");
	assert_equal(-2, df::zoom_view_state::accumulate_wheel_steps(pending, -120.0), "multiple reverse detents");
	assert_zoom_near(0.0, pending, "negative detents consumed exactly");
}

static void should_validate_zoom_navigator_mode()
{
	assert_equal(true, is_valid_zoom_navigator_mode(static_cast<uint32_t>(zoom_navigator_mode::auto_hide)),
	             "auto-hide is valid");
	assert_equal(true, is_valid_zoom_navigator_mode(static_cast<uint32_t>(zoom_navigator_mode::pinned)),
	             "pinned is valid");
	assert_equal(true, is_valid_zoom_navigator_mode(static_cast<uint32_t>(zoom_navigator_mode::off)),
	             "off is valid");
	assert_equal(false, is_valid_zoom_navigator_mode(3), "unknown mode is rejected");
}

static void should_keep_comparison_zoom_panes_matched()
{
	df::comparison_zoom_state zoom;
	zoom.mutate([](df::zoom_view_state& state) { state.set_explicit(2.0, {0.25, 0.75}); });
	assert_zoom_near(2.0, zoom.state(df::zoom_pane::primary).explicit_scale(), "primary scale changes");
	assert_zoom_near(2.0, zoom.state(df::zoom_pane::secondary).explicit_scale(), "secondary follows");
	assert_zoom_near(0.25, zoom.state(df::zoom_pane::secondary).center().X, "secondary adopts center");

	zoom.active(df::zoom_pane::secondary);
	assert_equal(true, zoom.active() == df::zoom_pane::secondary, "active pane switches");
	zoom.mutate([](df::zoom_view_state& state) { state.set_explicit(4.0, {0.75, 0.25}); });
	assert_zoom_near(4.0, zoom.state(df::zoom_pane::primary).explicit_scale(), "primary follows the active pane");
	assert_zoom_near(0.75, zoom.state(df::zoom_pane::primary).center().X, "primary adopts center");

	assert_equal(true, df::comparison_zoom_state::other(df::zoom_pane::primary) == df::zoom_pane::secondary,
	             "flip target from primary");
	assert_equal(true, df::comparison_zoom_state::other(df::zoom_pane::secondary) == df::zoom_pane::primary,
	             "flip target from secondary");
}

static void should_calculate_zoom_fit_without_enlarging()
{
	assert_zoom_near(0.5, df::zoom_view_state::fit_scale({4000, 2000}, {2000, 1200}), "large image fit");
	assert_zoom_near(1.0, df::zoom_view_state::fit_scale({200, 100}, {2000, 1200}), "small image fit");
	assert_zoom_near(10.0, df::zoom_view_state::fit_scale({200, 100}, {2000, 1200}, true), "fit and enlarge");
}

static void should_recalculate_zoom_fit_variants()
{
	df::zoom_view_state zoom;
	constexpr sized source{4000, 3000};
	zoom.fit_width(source, {1000, 500});
	assert_equal(true, zoom.mode() == df::zoom_scale_mode::fit_width, "fit width mode");
	assert_zoom_near(0.25, zoom.effective_scale(0.125), "fit width scale");
	zoom.update_fit_variant(source, {2000, 500});
	assert_zoom_near(0.5, zoom.effective_scale(0.1666666667), "fit width follows viewport");

	zoom.fill(source, {1000, 1000});
	assert_equal(true, zoom.mode() == df::zoom_scale_mode::fill, "fill mode");
	assert_zoom_near(1.0 / 3.0, zoom.effective_scale(0.25), "fill covers viewport");
	const auto geometry = zoom.geometry(source, {1000, 1000}, 0.25);
	assert_zoom_near(1000.0, geometry.destination.Height, "fill height");
	assert_zoom_near(4000.0 / 3.0, geometry.destination.Width, "fill crops width");
}

static void should_step_zoom_reversibly_through_fit()
{
	df::zoom_view_state zoom;
	constexpr sized source{4000, 2000};
	constexpr sized viewport{1600, 1000};
	const auto fit = zoom.fit_scale(source, viewport);
	constexpr pointd anchor{800, 500};

	zoom.step(1, fit, source, viewport, anchor);
	assert_zoom_near(0.5, zoom.effective_scale(fit), "first step above inserted fit");
	zoom.step(1, fit, source, viewport, anchor);
	assert_zoom_near(0.67, zoom.effective_scale(fit), "second ladder step");
	zoom.step(-1, fit, source, viewport, anchor);
	zoom.step(-1, fit, source, viewport, anchor);
	assert_equal(true, zoom.is_fit(), "step out returns to fit");
	assert_zoom_near(fit, zoom.effective_scale(fit), "fit scale restored");
}

static void should_keep_zoom_anchor_still()
{
	df::zoom_view_state zoom;
	constexpr sized source{4000, 3000};
	constexpr sized viewport{1000, 800};
	constexpr pointd anchor{750, 600};
	const auto fit = zoom.fit_scale(source, viewport);
	constexpr pointd old_center{0.5, 0.5};
	const pointd source_anchor{
		old_center.X * source.Width + (anchor.X - viewport.Width / 2.0) / fit,
		old_center.Y * source.Height + (anchor.Y - viewport.Height / 2.0) / fit
	};

	zoom.set_anchored(1.0, fit, source, viewport, anchor);
	const auto center = zoom.center();
	const pointd projected{
		viewport.Width / 2.0 + (source_anchor.X - center.X * source.Width),
		viewport.Height / 2.0 + (source_anchor.Y - center.Y * source.Height)
	};
	assert_zoom_near(anchor.X, projected.X, "anchor x");
	assert_zoom_near(anchor.Y, projected.Y, "anchor y");
}

static void should_step_zoom_at_pointer_anchor()
{
	df::zoom_view_state zoom;
	constexpr sized source{4000, 3000};
	constexpr sized viewport{1000, 800};
	constexpr pointd anchor{750, 600};
	const auto fit = zoom.fit_scale(source, viewport);
	const pointd source_anchor{
		zoom.center().X * source.Width + (anchor.X - viewport.Width / 2.0) / fit,
		zoom.center().Y * source.Height + (anchor.Y - viewport.Height / 2.0) / fit
	};

	zoom.step(1, fit, source, viewport, anchor);
	const auto scale = zoom.effective_scale(fit);
	const auto center = zoom.center();
	const pointd projected{
		viewport.Width / 2.0 + (source_anchor.X - center.X * source.Width) * scale,
		viewport.Height / 2.0 + (source_anchor.Y - center.Y * source.Height) * scale
	};
	assert_zoom_near(anchor.X, projected.X, "stepped anchor x");
	assert_zoom_near(anchor.Y, projected.Y, "stepped anchor y");
}

static void should_preserve_zoom_center_across_dimensions()
{
	df::zoom_view_state zoom;
	zoom.set_explicit(4.0, {0.25, 0.75});
	const auto first = zoom.geometry({6000, 4000}, {1200, 800}, 0.2);
	const auto second = zoom.geometry({3000, 2000}, {1200, 800}, 0.4);

	assert_zoom_near(first.center.X, second.center.X, "normalized center x");
	assert_zoom_near(first.center.Y, second.center.Y, "normalized center y");
}

static void should_suspend_carried_zoom_below_fit()
{
	df::zoom_view_state zoom;
	zoom.update_source({6000, 4000}, 0.2);
	zoom.set_explicit(0.5, {0.25, 0.75});

	zoom.update_source({1000, 800}, 1.0);
	assert_equal(true, zoom.is_carried_fit(), "smaller image is temporarily fitted");
	assert_equal(false, zoom.is_magnified(1.0), "carried fit does not permit pan");
	assert_equal(true, zoom.mode() == df::zoom_scale_mode::explicit_scale, "carried fit remains durable Zoom mode");
	assert_zoom_near(1.0, zoom.effective_scale(1.0), "fit is displayed");
	assert_zoom_near(0.5, zoom.explicit_scale(), "carried scale is retained");

	zoom.update_source({6000, 4000}, 0.2);
	assert_equal(false, zoom.is_carried_fit(), "carried scale resumes on a large image");
	assert_zoom_near(0.5, zoom.effective_scale(0.2), "carried scale is restored");
	assert_zoom_near(0.25, zoom.center().X, "source center x is retained");
	assert_zoom_near(0.75, zoom.center().Y, "source center y is retained");
}

static void should_clamp_zoom_model_pan_to_edges()
{
	df::zoom_view_state zoom;
	constexpr sized source{4000, 3000};
	constexpr sized viewport{1000, 800};
	zoom.set_explicit(2.0);
	zoom.pan_source({-source.Width, -source.Height}, source, viewport, 0.25);
	assert_zoom_near(0.0, zoom.center().X, "stored source center reaches left edge");
	assert_zoom_near(0.0, zoom.center().Y, "stored source center reaches top edge");
	const auto first = zoom.geometry(source, viewport, 0.25);
	assert_zoom_near(viewport.Width / (2.0 * source.Width * 2.0), first.center.X, "draw center clamps left");
	assert_zoom_near(viewport.Height / (2.0 * source.Height * 2.0), first.center.Y, "draw center clamps top");

	zoom.pan_source({source.Width * 2.0, source.Height * 2.0}, source, viewport, 0.25);
	assert_zoom_near(1.0, zoom.center().X, "stored source center reaches right edge");
	assert_zoom_near(1.0, zoom.center().Y, "stored source center reaches bottom edge");
	const auto second = zoom.geometry(source, viewport, 0.25);
	assert_zoom_near(1.0 - viewport.Width / (2.0 * source.Width * 2.0), second.center.X, "draw center clamps right");
	assert_zoom_near(1.0 - viewport.Height / (2.0 * source.Height * 2.0), second.center.Y, "draw center clamps bottom");
}

static void should_toggle_fit_to_last_explicit_zoom()
{
	df::zoom_view_state zoom;
	zoom.set_explicit(4.0, {0.25, 0.75});
	zoom.toggle_fit();
	assert_equal(true, zoom.is_fit(), "first toggle fits");
	zoom.toggle_fit();
	assert_equal(false, zoom.is_fit(), "second toggle restores explicit mode");
	assert_zoom_near(4.0, zoom.explicit_scale(), "last explicit scale restored");
	assert_zoom_near(0.25, zoom.center().X, "last explicit center x restored");
	assert_zoom_near(0.75, zoom.center().Y, "last explicit center y restored");
}

static void should_keep_zoom_anchor_through_layout_change()
{
	df::zoom_view_state zoom;
	constexpr sized source{4000, 3000};
	constexpr sized old_viewport{1000, 800};
	constexpr sized new_viewport{1800, 1000};
	constexpr pointd old_origin{700, 100};
	constexpr pointd new_origin{100, 100};
	constexpr pointd old_anchor{750, 600};
	const auto absolute_anchor = old_origin + old_anchor;
	const auto old_fit = zoom.fit_scale(source, old_viewport);
	const auto source_anchor = zoom.source_point_at(source, old_viewport, old_fit, old_anchor);

	zoom.set_anchored(1.0, old_fit, source, old_viewport, old_anchor);
	zoom.center_source_point_at(source_anchor, source, new_viewport, zoom.fit_scale(source, new_viewport),
	                            absolute_anchor - new_origin);
	const auto scale = zoom.effective_scale(zoom.fit_scale(source, new_viewport));
	const auto center = zoom.center();
	const pointd projected{
		new_origin.X + new_viewport.Width / 2.0 + (source_anchor.X - center.X * source.Width) * scale,
		new_origin.Y + new_viewport.Height / 2.0 + (source_anchor.Y - center.Y * source.Height) * scale
	};
	assert_zoom_near(absolute_anchor.X, projected.X, "relayout anchor x");
	assert_zoom_near(absolute_anchor.Y, projected.Y, "relayout anchor y");
}

static void should_zoom_model_region_to_viewport()
{
	df::zoom_view_state zoom;
	constexpr sized source{4000, 3000};
	constexpr sized viewport{1000, 800};
	const auto fit = zoom.fit_scale(source, viewport);
	zoom.zoom_region({250, 200, 500, 400}, source, viewport, fit);

	assert_zoom_near(0.5, zoom.center().X, "region center x");
	assert_zoom_near(0.5, zoom.center().Y, "region center y");
	assert_zoom_near(0.5, zoom.effective_scale(fit), "region scale fills viewport");
}

static void should_accelerate_pan_from_drag_origin()
{
	const auto accelerated = df::zoom_view_state::accelerate_pan({3.0, 4.0}, 10.0);
	assert_zoom_near(4.5, accelerated.X, "accelerated pan x");
	assert_zoom_near(6.0, accelerated.Y, "accelerated pan y");

	const auto dpi_scaled = df::zoom_view_state::accelerate_pan({3.0, 4.0}, 20.0);
	assert_zoom_near(3.75, dpi_scaled.X, "DPI-scaled ramp x");
	assert_zoom_near(5.0, dpi_scaled.Y, "DPI-scaled ramp y");
}

static void should_bound_auto_pan_velocity()
{
	const auto stopped = df::zoom_view_state::auto_pan_velocity({6.0, 8.0});
	assert_zoom_near(0.0, stopped.X, "auto-pan dead zone x");
	assert_zoom_near(0.0, stopped.Y, "auto-pan dead zone y");
	const auto moving = df::zoom_view_state::auto_pan_velocity({15.0, 20.0});
	assert_zoom_near(62.4, moving.X, "auto-pan velocity x");
	assert_zoom_near(83.2, moving.Y, "auto-pan velocity y");
	const auto capped = df::zoom_view_state::auto_pan_velocity({300.0, 400.0});
	assert_zoom_near(720.0, capped.X, "auto-pan capped x");
	assert_zoom_near(960.0, capped.Y, "auto-pan capped y");
}

static void should_map_zoom_navigator_to_source_center()
{
	const auto center = df::zoom_view_state::navigator_center({50.0, 25.0}, {200.0, 100.0});
	assert_zoom_near(0.25, center.X, "navigator center x");
	assert_zoom_near(0.25, center.Y, "navigator center y");

	const auto clamped = df::zoom_view_state::navigator_center({-10.0, 120.0}, {200.0, 100.0});
	assert_zoom_near(0.0, clamped.X, "navigator clamps left");
	assert_zoom_near(1.0, clamped.Y, "navigator clamps bottom");
}

static void should_orient_selector_thumbnails()
{
	constexpr recti image_bounds(0, 0, 100, 100);
	const auto rotated = selector_view::thumbnail_destination({4, 3}, image_bounds,
	                                                          ui::orientation::right_top, true);
	const auto unrotated = selector_view::thumbnail_destination({4, 3}, image_bounds,
	                                                            ui::orientation::right_top, false);

	assert_equal(90.0, rotated.angle(), "selector applies right-top orientation");
	assert_equal(true, rotated.bounding_rect().extent().round() == sizei(75, 100),
	             "oriented selector thumbnail fits as portrait");
	assert_equal(0.0, unrotated.angle(), "selector preserves orientation when rotation is disabled");
	assert_equal(true, unrotated.bounding_rect().extent().round() == sizei(100, 75),
	             "unrotated selector thumbnail fits as landscape");
}

static void should_range_select_across_selector()
{
	null_state_strategy ss;
	null_async_strategy as;
	const view_host_base_ptr view;

	const location_cache locations;
	index_state index(as, locations);
	view_state s(ss, as, index, make_test_player());
	s.view_mode(view_type::items);
	s.open(view, df::search_t().add_selector(test_files_folder), {});
	s.update_item_groups();
	s.update_selection();

	const auto is_file = [](const df::item_element_ptr& i) { return !i->is_folder(); };

	df::item_elements shown;
	df::item_element_ptr hidden;

	for (const auto& group : s.groups())
	{
		for (const auto& i : group->items())
		{
			if (is_file(i)) shown.emplace_back(i);
			else hidden = i;
		}
	}

	assert_equal(true, shown.size() > 4, "test folder has enough items for a range");

	const auto selector = std::make_shared<selector_view>(s, nullptr, nullptr);
	selector->filter(is_file);
	selector->activate({0, 0});

	selector->selection_anchor(shown[1]);
	const auto forward = selector->selection_range(shown[4]);
	assert_equal(4_z, forward.size(), "range spans the anchor to the clicked item");
	assert_equal(true, forward.front() == shown[4], "clicked item leads so focus follows the pointer");

	// A second shift click measures from the same anchor rather than pivoting on the previous click.
	const auto shortened = selector->selection_range(shown[2]);
	assert_equal(2_z, shortened.size(), "a shift click does not move the anchor");
	assert_equal(true, shortened.front() == shown[2], "clicked item still leads");

	const auto backward = selector->selection_range(shown[0]);
	assert_equal(2_z, backward.size(), "range is measured backwards from the anchor");
	assert_equal(true, backward.front() == shown[0], "clicked item leads when the range runs backwards");

	// An item the strip filtered out cannot be reached, so a task is never handed one it cannot write.
	if (hidden) assert_equal(0_z, selector->selection_range(hidden).size(), "filtered items are not selectable");

	// A strip that is not on screen holds no items, so it neither claims item visibility nor queues
	// thumbnails for the view that is.
	selector->deactivate();
	assert_equal(0_z, selector->selection_range(shown[0]).size(), "an inactive strip selects nothing");
}

// Selecting an item the browser has already drawn must fill the panel from what the item already
// holds - its staged thumbnail surface and its indexed dimensions - rather than decoding again. No
// draw runs here, and nothing else in the selection path produces a surface, so a panel that has
// something to show can only have taken it from the item on the selecting thread.
static void should_show_selected_thumbnail_without_waiting()
{
	null_state_strategy ss;
	null_async_strategy as;
	const view_host_base_ptr view;

	const location_cache locations;
	index_state index(as, locations);
	view_state s(ss, as, index, make_test_player());
	s.view_mode(view_type::items);
	s.open(view, df::search_t().add_selector(test_files_folder), {});
	s.update_item_groups();
	s.update_selection();

	df::item_elements photos;

	for (const auto& group : s.groups())
	{
		for (const auto& i : group->items())
		{
			if (!i->is_folder() && i->file_type()->has_trait(file_traits::bitmap)) photos.emplace_back(i);
		}
	}

	assert_equal(true, photos.size() >= 2_z, "test folder has photos to step between");

	// What the browser leaves on an item whose tile is on screen.
	const auto& target = photos.front();
	index.scan_item(target, true, false);
	target->stage_thumbnail_surface(as);

	assert_equal(true, target->has_thumb(), "the tile has its encoded thumbnail");
	assert_equal(true, target->has_cached_surface(), "the tile has its decoded thumbnail surface");
	assert_equal(true, target->metadata() != nullptr, "the tile has its indexed metadata");

	s.select(view, target, false, false, false);
	s.update_selection();

	const auto d = s.display_state();

	assert_equal(true, d && d->_selected_texture1 != nullptr, "selecting built a display texture");
	assert_equal(true, d->_selected_texture1->has_visual(), "the panel can draw the tile's thumbnail at once");
	assert_equal(false, d->_selected_texture1->display_dimensions().is_empty(),
	             "the panel knows the image's shape at once");
	assert_equal(true, d->_selected_texture1->is_provisional(), "what it draws is still marked provisional");
}

static void should_stage_neighbour_stand_ins()
{
	null_state_strategy ss;
	null_async_strategy as;
	const view_host_base_ptr view;

	const location_cache locations;
	index_state index(as, locations);
	view_state s(ss, as, index, make_test_player());
	s.view_mode(view_type::media);
	s.open(view, df::search_t().add_selector(test_files_folder), {});
	s.update_item_groups();
	s.update_selection();

	df::item_elements ordered;

	for (const auto& group : s.groups())
	{
		for (const auto& i : group->items()) ordered.emplace_back(i);
	}

	size_t middle = 0;

	for (size_t n = 1; n + 1 < ordered.size(); ++n)
	{
		if (!ordered[n - 1]->is_folder() && !ordered[n]->is_folder() && !ordered[n + 1]->is_folder())
		{
			middle = n;
			break;
		}
	}

	assert_equal(true, middle != 0_z, "test folder has a file with file neighbours");

	// Nothing here has a thumbnail: the browser's batched database query is what normally supplies
	// them, and there is no browser in this view. Retiring the query leaves the display facing the
	// case that matters - an item with no encoded thumbnail to stand in for it.
	for (const auto& i : ordered) i->begin_db_thumbnail_query();

	s.select(view, ordered[middle], false, false, false);
	s.update_selection();

	const auto previous = s.next_item(false, false);
	const auto next = s.next_item(true, false);

	assert_equal(true, previous && next, "there are items to reach either side");
	assert_equal(true, previous->has_thumb(), "the previous item's thumbnail was requested");
	assert_equal(true, next->has_thumb(), "the next item's thumbnail was requested");
	assert_equal(true, previous->has_cached_surface(), "the previous item's stand-in is staged");
	assert_equal(true, next->has_cached_surface(), "the next item's stand-in is staged");
}

// The items preview fills in as the file is read: the metadata blocks, and any property whose value
// was not in the index, are emitted long after the panel is laid out. None of that may move or resize
// what is already on screen. This is the shape of every jump reported against 1.27.1 - the media pane
// was the column's shrink target, so each late row was paid for by shrinking the picture, and the
// column centred on the total, so each late row moved everything above it.
static void should_hold_media_column_still_as_detail_arrives()
{
	flex_test_measure_context mc;
	ui::control_layouts positions;

	const auto media = std::make_shared<flex_test_element>(sizei{200, 400});
	media->flex = media->flex | flex_item::media;

	const std::vector<view_element_ptr> priority{media, std::make_shared<flex_test_element>(sizei{200, 20})};
	const auto all = priority;

	constexpr recti pane{0, 0, 200, 600};

	const auto arrange = [&](const std::vector<view_element_ptr>& detail)
	{
		const media_column_inputs in{&priority, &detail, &all, pane, true};
		return layout_media_column(in, mc, positions);
	};

	const std::vector<view_element_ptr> no_detail;
	arrange(no_detail);
	const auto first_bounds = media->bounds;

	assert_equal(false, first_bounds.is_empty(), "the media pane was laid out");

	// One metadata block lands. It is below the primary content and it is not the media.
	const std::vector<view_element_ptr> one_row{std::make_shared<flex_test_element>(sizei{200, 40})};
	const auto height_with_row = arrange(one_row);

	assert_equal(true, media->bounds == first_bounds, "a detail row neither moves nor resizes the media");

	// Enough detail to take the column past the pane, which is where the arrangement used to change.
	const std::vector<view_element_ptr> many_rows{
		std::make_shared<flex_test_element>(sizei{200, 300}),
		std::make_shared<flex_test_element>(sizei{200, 300}),
		std::make_shared<flex_test_element>(sizei{200, 300})
	};
	const auto height_with_many = arrange(many_rows);

	assert_equal(true, media->bounds == first_bounds, "detail past the pane height still does not move the media");
	assert_equal(true, height_with_many > height_with_row, "detail grows the scrollable height instead");
}

// Verbose metadata closed: the media, the first information group and the toggle own the pane and
// centre in it. The media shrinks to keep all three visible - a portrait image that took the whole
// pane pushed the information group off the bottom, where nothing said it was there.
static void should_fit_the_whole_primary_block_when_verbose_is_closed()
{
	flex_test_measure_context mc;
	ui::control_layouts positions;
	constexpr recti pane{0, 0, 200, 300};
	const std::vector<view_element_ptr> no_detail;

	const auto arrange = [&](const sizei media_extent, const view_element_ptr& toggle)
	{
		const auto media = std::make_shared<flex_test_element>(media_extent);
		media->flex = media->flex | flex_item::media;
		const std::vector<view_element_ptr> priority{media, std::make_shared<flex_test_element>(sizei{200, 20}), toggle};
		const media_column_inputs in{&priority, &no_detail, &priority, pane, false};
		layout_media_column(in, mc, positions);
		return media;
	};

	// Portrait: taller than the pane on its own, so the media must give way.
	auto tall_toggle = std::make_shared<flex_test_element>(sizei{200, 20});
	const auto tall_media = arrange(sizei{200, 400}, tall_toggle);

	assert_equal(true, tall_media->bounds.top >= pane.top, "a portrait image starts inside the pane");
	assert_equal(true, tall_toggle->bounds.bottom <= pane.bottom, "the verbose toggle stays inside the pane");

	// Landscape: everything fits, so the block centres rather than sitting at the top.
	auto short_toggle = std::make_shared<flex_test_element>(sizei{200, 20});
	const auto short_media = arrange(sizei{200, 100}, short_toggle);
	const auto above = short_media->bounds.top - pane.top;
	const auto below = pane.bottom - short_toggle->bounds.bottom;

	assert_equal(true, above > 0, "a block that fits does not sit against the top");
	assert_equal(true, std::abs(above - below) <= 1, "it is centred vertically");

	// Shorter than the block can be made even with the media at its floor. Centring the overflow would
	// push the top of the image above the pane, where the scroller cannot reach it.
	constexpr recti short_pane{0, 0, 200, 150};
	const auto squeezed = std::make_shared<flex_test_element>(sizei{200, 400});
	squeezed->flex = squeezed->flex | flex_item::media;
	const auto big_group = std::make_shared<flex_test_element>(sizei{200, 120});
	const std::vector<view_element_ptr> cramped{squeezed, big_group};
	const media_column_inputs cramped_in{&cramped, &no_detail, &cramped, short_pane, false};
	layout_media_column(cramped_in, mc, positions);

	assert_equal(true, squeezed->bounds.top >= short_pane.top, "an overflowing block is never clipped off the top");
}

// Verbose metadata open: the media and the first information group are held at the top, both visible,
// and the metadata blocks follow immediately. A centred line reports the container height rather than
// the content height, which left a gap the size of the pane's free space between the two.
static void should_follow_the_primary_block_when_verbose_is_open()
{
	flex_test_measure_context mc;
	ui::control_layouts positions;
	constexpr recti pane{0, 0, 200, 300};

	const auto media = std::make_shared<flex_test_element>(sizei{200, 100});
	media->flex = media->flex | flex_item::media;
	const auto group = std::make_shared<flex_test_element>(sizei{200, 20});
	const auto verbose = std::make_shared<flex_test_element>(sizei{200, 50});

	const std::vector<view_element_ptr> priority{media, group};
	const std::vector<view_element_ptr> detail{verbose};
	const std::vector<view_element_ptr> all{media, group, verbose};

	const media_column_inputs in{&priority, &detail, &all, pane, true};
	const auto content_height = layout_media_column(in, mc, positions);

	assert_equal(pane.top, media->bounds.top, "the block is aligned to the top");
	assert_equal(true, group->bounds.bottom <= pane.bottom, "the information group is visible");
	assert_equal(group->bounds.bottom, verbose->bounds.top, "verbose metadata follows with no gap");
	assert_equal(true, content_height >= verbose->bounds.bottom - pane.top, "the scroll extent reaches it");
}

static void should_leave_full_screen_for_task_views()
{
	null_state_strategy ss;
	null_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);
	view_state s(ss, as, index, make_test_player());

	s.is_full_screen = true;
	s.view_mode(view_type::media);
	assert_equal(0, ss.toggle_full_screen_count, "media is the full screen view");

	s.view_mode(view_type::edit);
	assert_equal(1, ss.toggle_full_screen_count, "a task view entered from full screen leaves it");

	// The stub records the request without applying it, so the windowed state is set here.
	s.is_full_screen = false;
	s.view_mode(view_type::tags);
	assert_equal(1, ss.toggle_full_screen_count, "a windowed task view does not toggle full screen");
}

static void should_preserve_view_scroller_anchor_across_layout()
{
	view_scroller scroller;
	constexpr recti client_bounds(0, 0, 100, 200);
	constexpr recti scroll_bounds(90, 0, 100, 200);
	const auto element = std::make_shared<text_element>("anchor");
	element->bounds = {0, 350, 100, 450};

	scroller.layout({100, 1000}, client_bounds, scroll_bounds);
	scroller._offset.y = 300;
	const auto anchor = scroller.capture_anchor(element);
	assert_equal(50, anchor.device_top, "scroll anchor captures device position");

	element->bounds = {0, 500, 100, 600};
	scroller.layout({100, 1200}, client_bounds, scroll_bounds);
	assert_equal(450, scroller.anchor_offset(anchor, true), "current element preserves device position");
	assert_equal(375, scroller.anchor_offset(anchor, false), "replaced element preserves scroll ratio");

	scroller._offset.y = 0;
	const auto top_anchor = scroller.capture_anchor(element);
	element->bounds = {0, 700, 100, 800};
	assert_equal(0, scroller.anchor_offset(top_anchor, true), "top remains top after layout");
}

static void should_reserve_view_scroller_footer()
{
	view_scroller scroller;
	constexpr recti client_bounds(0, 0, 100, 200);
	constexpr recti full_scroll_bounds(90, 0, 100, 200);
	const auto footer = scroller.layout_with_footer(
		{100, 1000}, client_bounds, full_scroll_bounds, 10, 2);

	assert_equal(true, footer == recti(90, 190, 100, 200), "scroll footer occupies requested base");
	assert_equal(true, scroller.scroll_bounds() == recti(90, 0, 100, 188),
	             "scroll track ends before footer");

	// The section bands are placed with logical_to_scrollbar_pos, so it must land on the track the
	// thumb actually travels rather than on the client height the footer no longer covers.
	const auto track = scroller.track_bounds();
	assert_equal(true, track == recti(90, 2, 100, 186), "scroll track excludes its own edge inset");
	assert_equal(track.height(), scroller.logical_to_scrollbar_pos(1000), "list end maps to track end");
	assert_equal(1000, scroller.scrollbar_pos_to_logical(track.bottom), "track end maps to list end");
	assert_equal(0, scroller.scrollbar_pos_to_logical(track.top), "track start maps to list start");
}

static void should_drag_view_scroller_thumb_from_grab_point()
{
	view_scroller scroller;
	scroller.layout({100, 1000}, recti(0, 0, 100, 200), recti(90, 0, 100, 200));
	scroller._offset.y = 400;

	const auto track = scroller.track_bounds();
	const auto thumb = scroller.thumb_bounds();

	assert_equal(track.top + scroller.logical_to_scrollbar_pos(400), scroller.thumb_origin(),
	             "thumb origin follows the scroll position");
	assert_equal(true, thumb.top <= scroller.thumb_origin() && thumb.bottom > scroller.thumb_origin(),
	             "painted thumb covers its origin");

	// A drag holds the point it grabbed: releasing without moving must leave the thumb where it
	// was, rather than re-centring it on the cursor as an unheld press on bare track does.
	const auto grab = scroller.thumb_origin() + 3;
	const auto held = scroller.scrollbar_pos_to_logical(grab - 3);
	assert_equal(scroller.logical_to_scrollbar_pos(400), scroller.logical_to_scrollbar_pos(held),
	             "grabbing the thumb does not move it");
	assert_equal(scroller.scrollbar_pos_to_logical(scroller.thumb_origin() + 10),
	             scroller.scrollbar_pos_to_logical(grab + 10 - 3),
	             "grab offset cancels out of the drag");
}

static void should_close_view_scroller_bands_over_track()
{
	view_scroller scroller;
	scroller.layout({100, 1000}, recti(0, 0, 100, 200), recti(90, 0, 100, 200));
	scroller.sections({{"a", icon_index::none, 400}, {"b", icon_index::none, 700}});

	const auto track_height = scroller.track_bounds().height();
	std::vector<std::pair<int, int>> bands;
	auto labelled = 0;

	scroller.for_each_band([&](const int top, const int bottom, const view_scroller_section* so)
	{
		bands.emplace_back(top, bottom);
		if (so) ++labelled;
	});

	assert_equal(3u, static_cast<unsigned>(bands.size()), "sections plus a closing band");
	assert_equal(2, labelled, "only the sections carry labels");
	assert_equal(0, bands.front().first, "bands start at the track");
	assert_equal(track_height, bands.back().second, "bands close off the track");
}

// A host that has never been attached, or whose window has already been destroyed. Diffractor
// 1.27.0 shipped a startup crash of exactly this shape: the sidebar borrowed the items view's
// frame only when it was visible, but kept populating, counting and invalidating while hidden.
class detached_test_host final : public std::enable_shared_from_this<detached_test_host>, public view_host
{
public:
	int controller_requests = 0;
	int controller_changes = 0;
	int invalidations = 0;

	// The whole point: no window, ever.
	const ui::frame_ptr frame() const override { return ui::no_frame(); }
	const ui::control_frame_ptr owner() override { return nullptr; }

	void on_window_layout(ui::measure_context& mc, const sizei extent, bool is_minimized) override {}
	void on_window_paint(ui::draw_context& dc) override {}
	void tick() override {}
	void activate(bool is_active) override {}
	bool key_down(const int c, const ui::key_state keys) override { return false; }
	void invoke(const commands cmd) override {}
	bool is_command_checked(const commands cmd) override { return false; }
	void track_menu(const recti bounds, const std::vector<ui::command_ptr>& commands) override {}
	void controller_changed() override { ++controller_changes; }
	void invalidate_element(const view_element_ptr& e) override { ++invalidations; }
	void invalidate_view(const view_invalid invalid) override { ++invalidations; }

	view_controller_ptr controller_from_location(const pointi loc) override
	{
		++controller_requests;
		return nullptr;
	}
};

static void should_survive_a_host_with_no_window()
{
	const auto host = std::make_shared<detached_test_host>();
	host->_extent = {200, 400};

	// Every entry point view_host offers, in the order a real pointer session reaches them. Each
	// one dereferences frame() unconditionally, so a null there is an access violation.
	host->on_mouse_move({10, 10}, false);
	host->on_mouse_left_button_down({10, 10}, {});
	host->on_mouse_left_button_up({10, 10}, {});
	host->on_mouse_middle_button_down({10, 10}, {});
	host->on_mouse_middle_button_up({10, 10}, {});
	host->on_mouse_left_button_double_click({10, 10}, {});
	host->on_mouse_leave({10, 10});
	host->update_cursor();
	host->show_cursor(false);
	host->invalidate_element(nullptr);

	assert_equal(false, host->escape_controller(), "nothing to escape without a controller");
	assert_equal(false, host->key_down_controller(U'a', {}), "no controller claims a key");
	assert_equal(true, host->controller_requests > 0, "hit testing still ran");

	// Scrolling asks the host for its window twice: once to shift the pixels, once to repaint.
	view_scroller scroller;
	scroller.layout({200, 4000}, recti(0, 0, 190, 400), recti(190, 0, 200, 400));
	scroller.scroll_offset(host, 0, 500);
	scroller.offset(host, 0, 120);
	assert_equal(true, scroller.scroll_offset().y > 0, "the scroller still tracks its position");
}

static void should_answer_a_null_frame_without_side_effects()
{
	const auto f = ui::no_frame();
	assert_equal(true, f != nullptr, "the stand-in is a real object");
	assert_equal(true, f == ui::no_frame(), "one shared instance, so it costs nothing to ask");

	// Answers chosen so a caller that acts on them does less, never more: an absent window is
	// occluded (skip drawing), invisible, unfocused, and its cursor is outside every client rect.
	assert_equal(true, f->is_occluded(), "nothing drawn into it could be seen");
	assert_equal(false, f->is_visible(), "not visible");
	assert_equal(false, f->has_focus(), "cannot hold focus");
	assert_equal(false, f->is_enabled(), "cannot be interacted with");
	assert_equal(true, f->window_bounds().is_empty(), "occupies nothing");
	assert_equal(true, f->cursor_location() == pointi(-1, -1), "cursor is outside every client rect");
}

void register_tests7(view_state& state, test_registry& tests)
{
	tests.add("Should select settled zoom sampler"s, should_select_settled_zoom_sampler);
	tests.add("Should accumulate precision zoom wheel deltas"s, should_accumulate_precision_zoom_wheel_deltas);
	tests.add("Should validate zoom navigator mode"s, should_validate_zoom_navigator_mode);
	tests.add("Should keep comparison zoom panes matched"s, should_keep_comparison_zoom_panes_matched);
	tests.add("Should calculate zoom model fit without enlarging"s, should_calculate_zoom_fit_without_enlarging);
	tests.add("Should recalculate zoom fit variants"s, should_recalculate_zoom_fit_variants);
	tests.add("Should step zoom model reversibly through fit"s, should_step_zoom_reversibly_through_fit);
	tests.add("Should keep zoom model anchor still"s, should_keep_zoom_anchor_still);
	tests.add("Should step zoom model at pointer anchor"s, should_step_zoom_at_pointer_anchor);
	tests.add("Should preserve zoom model center across dimensions"s,
	          should_preserve_zoom_center_across_dimensions);
	tests.add("Should suspend carried zoom model below fit"s, should_suspend_carried_zoom_below_fit);
	tests.add("Should clamp zoom model pan to edges"s, should_clamp_zoom_model_pan_to_edges);
	tests.add("Should toggle zoom model fit to last explicit view"s, should_toggle_fit_to_last_explicit_zoom);
	tests.add("Should keep zoom model anchor through layout change"s,
	          should_keep_zoom_anchor_through_layout_change);
	tests.add("Should zoom model region to viewport"s, should_zoom_model_region_to_viewport);
	tests.add("Should accelerate zoom pan from drag origin"s, should_accelerate_pan_from_drag_origin);
	tests.add("Should bound zoom auto-pan velocity"s, should_bound_auto_pan_velocity);
	tests.add("Should map zoom navigator to source center"s, should_map_zoom_navigator_to_source_center);
	tests.add("Should orient selector thumbnails"s, should_orient_selector_thumbnails);
	tests.add("Should range select across selector"s, should_range_select_across_selector);
	tests.add("Should stage neighbour stand ins"s, should_stage_neighbour_stand_ins);
	tests.add("Should show selected thumbnail without waiting"s, should_show_selected_thumbnail_without_waiting);
	tests.add("Should hold media column still as detail arrives"s, should_hold_media_column_still_as_detail_arrives);
	tests.add("Should fit the whole primary block when verbose is closed"s,
	          should_fit_the_whole_primary_block_when_verbose_is_closed);
	tests.add("Should follow the primary block when verbose is open"s,
	          should_follow_the_primary_block_when_verbose_is_open);
	tests.add("Should leave full screen for task views"s, should_leave_full_screen_for_task_views);
	tests.add("Should preserve view scroller anchor across layout"s,
	          should_preserve_view_scroller_anchor_across_layout);
	tests.add("Should reserve view scroller footer"s, should_reserve_view_scroller_footer);
	tests.add("Should drag view scroller thumb from grab point"s,
	          should_drag_view_scroller_thumb_from_grab_point);
	tests.add("Should close view scroller bands over track"s, should_close_view_scroller_bands_over_track);
	tests.add("Should survive a host with no window"s, should_survive_a_host_with_no_window);
	tests.add("Should answer a null frame without side effects"s, should_answer_a_null_frame_without_side_effects);
}
