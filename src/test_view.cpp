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
#include "test_fixtures.h"
#include "test_runner.h"
#include "files.h"
#include "model_zoom.h"
#include "ui_elements.h"
#include "ui_text_edit.h"
#include "view_items.h"
#include "view_list.h"
#include "view_selector.h"
#include "view_tags.h"

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
		const media_column_inputs in{&priority, &detail, &all, pane, true, true};
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
		const media_column_inputs in{&priority, &no_detail, &priority, pane, false, true};
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
	const media_column_inputs cramped_in{&cramped, &no_detail, &cramped, short_pane, false, true};
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

	const media_column_inputs in{&priority, &detail, &all, pane, true, true};
	const auto content_height = layout_media_column(in, mc, positions);

	assert_equal(pane.top, media->bounds.top, "the block is aligned to the top");
	assert_equal(true, group->bounds.bottom <= pane.bottom, "the information group is visible");
	assert_equal(group->bounds.bottom, verbose->bounds.top, "verbose metadata follows with no gap");
	assert_equal(true, content_height >= verbose->bounds.bottom - pane.top, "the scroll extent reaches it");
}

// Verbose metadata is one global setting, but a multiple selection has no metadata blocks to open, so
// its panel has nothing below it to be held clear of. It centres like a single item rather than
// sitting against the top for a reason that does not apply to it.
static void should_centre_a_block_with_no_detail_whatever_verbose_is()
{
	flex_test_measure_context mc;
	ui::control_layouts positions;
	constexpr recti pane{0, 0, 200, 300};

	const auto collage = std::make_shared<flex_test_element>(sizei{200, 100});
	const auto controls = std::make_shared<flex_test_element>(sizei{200, 40});

	const std::vector<view_element_ptr> priority{collage, controls};
	const std::vector<view_element_ptr> no_detail;

	const media_column_inputs in{&priority, &no_detail, &priority, pane, true, false};
	layout_media_column(in, mc, positions);

	const auto above = collage->bounds.top - pane.top;
	const auto below = pane.bottom - controls->bounds.bottom;

	assert_equal(true, above > 0, "a selection panel with no detail does not sit against the top");
	assert_equal(true, std::abs(above - below) <= 1, "it is centred vertically");
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

// The list view's chrome is measured in text lines, so unlike the flex stub this one has to report a
// height. Only the vertical arrangement is under test, so the column widths are nominal.
class list_test_measure_context final : public ui::measure_context
{
public:
	static constexpr int line_height = 20;

	sizei measure_text(const std::string_view text, ui::style::font_face font, ui::style::text_style style,
	                   const int cx, int cy = 0) override
	{
		return {std::min(cx, static_cast<int>(text.size()) * 8), line_height};
	}

	int text_line_height(ui::style::font_face font) override { return line_height; }
	ui::text_layout_ptr create_text_layout(ui::style::font_face font) override { return {}; }
};

class processing_test_view final : public list_view
{
public:
	std::string _text;

	processing_test_view(view_state& state, view_host_ptr host) : list_view(state, std::move(host))
	{
	}

	std::string_view status() override { return _text; }

	void refresh() override
	{
	}

	void add_rows(const int count)
	{
		for (auto i = 0; i < count; ++i)
		{
			auto row = std::make_shared<row_element>(*this);
			row->_order = i;
			row->_work_index = i;
			_rows.emplace_back(std::move(row));
		}
	}

	int active_device_top() const { return _active_row->bounds.top - _scroller.scroll_offset().y; }
	int active_device_bottom() const { return _active_row->bounds.bottom - _scroller.scroll_offset().y; }
};

// A run keeps the row it is working on visible, and visible means clear of the chrome painted over
// the list rather than merely inside the scrolling area: the column headers are drawn across the top
// of it and view_frame::draw_view_status paints the status band across its base. Scrolling the row to
// the very edge left the highlight under one or the other for the whole of a long run.
static void should_keep_the_processing_row_clear_of_the_view_chrome()
{
	null_state_strategy ss;
	null_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);
	view_state s(ss, as, index, make_test_player());

	const auto host = std::make_shared<detached_test_host>();
	const auto view = std::make_shared<processing_test_view>(s, host);

	constexpr sizei extent{400, 300};
	constexpr auto band = list_test_measure_context::line_height + 8 * 2;
	constexpr auto margin = list_test_measure_context::line_height;

	list_test_measure_context mc;
	view->add_rows(100);
	view->_text = "Processing";
	view->begin_processing(100);
	view->layout(mc, extent);

	// Far enough down that the list has to scroll to reach it, which is where the row used to come to
	// rest against the bottom edge with the status band drawn over it.
	view->processing_work_item(60);

	assert_equal(true, view->active_device_bottom() <= extent.cy - band - margin,
	             "the row stops clear of the status band");
	assert_equal(true, view->active_device_top() >= 0, "and is still inside the view");

	// Back up the list: the same rule applies to the column headers above it.
	view->processing_work_item(5);

	assert_equal(true, view->active_device_top() >= band + margin,
	             "the row stops clear of the column headers");
	assert_equal(true, view->active_device_bottom() <= extent.cy, "and is still inside the view");

	view->end_processing();
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

static void should_layout_selection_thumbnail_collage()
{
	const auto check = [](const recti draw_bounds, const std::vector<sizei>& dimensions, const size_t expected_count)
	{
		const auto bounds = ui::layout_collage(draw_bounds, dimensions);
		assert_equal(expected_count, bounds.size(), "collage bounds count");

		for (auto index = 0u; index < bounds.size(); ++index)
		{
			const auto& bound = bounds[index];
			assert_equal(true, bound.area() > 0, "collage cell has area");
			assert_equal(true, draw_bounds.contains(bound.top_left()), "collage cell top-left contained");
			assert_equal(true, draw_bounds.contains({bound.right - 1, bound.bottom - 1}),
			             "collage cell bottom-right contained");

			for (auto other = index + 1; other < bounds.size(); ++other)
			{
				const auto& other_bound = bounds[other];
				const auto overlaps = bound.left < other_bound.right && bound.right > other_bound.left &&
					bound.top < other_bound.bottom && bound.bottom > other_bound.top;
				assert_equal(false, overlaps, "collage cells do not overlap");
			}
		}
	};

	check({0, 0, 1200, 800}, std::vector<sizei>(9, sizei(256, 256)), 9);
	check({0, 0, 1600, 900}, {
		      {1200, 200}, {180, 900}, {400, 400}, {300, 800}, {1400, 350}, {640, 480},
		      {200, 1000}, {1600, 300}, {500, 500}, {900, 1600}, {1000, 250}, {300, 300}
	      }, 12);
	check({0, 0, 1920, 1080}, std::vector<sizei>(24, sizei(320, 240)), 24);
	check({0, 0, 1920, 1080}, std::vector<sizei>(30, sizei(320, 240)), 24);

	const auto aesthetic_bounds = ui::layout_collage({0, 0, 1200, 800}, std::vector<sizei>(9, sizei(256, 256)));
	const auto first_row_top = aesthetic_bounds.front().top;
	assert_equal(true, std::any_of(aesthetic_bounds.begin(), aesthetic_bounds.end(),
	                               [first_row_top](const recti& bounds) { return bounds.top != first_row_top; }),
	             "collage uses multiple rows");
	const auto [smallest, largest] = std::minmax_element(aesthetic_bounds.begin(), aesthetic_bounds.end(),
	                                                     [](const recti& left, const recti& right)
	                                                     {
		                                                     return left.area() < right.area();
	                                                     });
	assert_equal(true, largest->area() > smallest->area() * 3 / 2, "collage varies cell sizes");
	assert_equal(800, std::max_element(aesthetic_bounds.begin(), aesthetic_bounds.end(),
	                                   [](const recti& left, const recti& right)
	                                   {
		                                   return left.bottom < right.bottom;
	                                   })->bottom,
	             "collage fills available height");

	const auto portrait_bounds = ui::layout_collage({0, 0, 600, 1200}, std::vector<sizei>(9, sizei(256, 256)));
	assert_equal(1200, std::max_element(portrait_bounds.begin(), portrait_bounds.end(),
	                                    [](const recti& left, const recti& right)
	                                    {
		                                    return left.bottom < right.bottom;
	                                    })->bottom,
	             "portrait collage fills available height");
	assert_equal(true, std::any_of(portrait_bounds.begin(), portrait_bounds.end(),
	                               [](const recti& bounds) { return bounds.height() > bounds.width(); }),
	             "portrait collage has vertical cells");
}

static void should_edit_single_line_text()
{
	ui::single_line_edit_model edit;
	edit.text("Hello world");
	edit.select(6, 11);
	edit.insert("Diffractor");
	assert_equal_strict("Hello Diffractor", edit.text(), "replace selection");

	edit.move_left(true);
	assert_equal(true, edit.has_selection(), "shift-left selects");
	edit.move_left();
	assert_equal(false, edit.has_selection(), "left collapses selection");
	assert_equal(15_z, edit.caret(), "left collapses to selection start");

	edit.text("A\u00e9B");
	edit.move_left();
	edit.backspace();
	assert_equal_strict("AB", edit.text(), "backspace removes one UTF-8 code point");
	assert_equal(1_z, edit.caret(), "caret remains on UTF-8 boundary");

	edit.select_all();
	edit.insert("one\r\ntwo\nthree");
	assert_equal_strict("one two three", edit.text(), "paste is normalized to one line");

	edit.text("one two three");
	edit.move_word_left();
	assert_equal(8_z, edit.caret(), "control-left moves to previous word");
	edit.backspace_word();
	assert_equal_strict("one three", edit.text(), "control-backspace removes previous word");
	edit.undo();
	assert_equal_strict("one two three", edit.text(), "undo restores word deletion");
	edit.redo();
	assert_equal_strict("one three", edit.text(), "redo reapplies word deletion");

	edit.text("alpha beta");
	edit.select_word(7);
	assert_equal_strict("beta", edit.selected_text(), "double-click model selects a word");
	edit.erase_selection();
	assert_equal_strict("alpha ", edit.text(), "cut removes selected word");
	edit.undo();
	assert_equal_strict("alpha beta", edit.text(), "undo restores cut text");

	edit.begin_edit();
	edit.select_all();
	edit.insert("changed");
	edit.cancel_edit();
	assert_equal_strict("alpha beta", edit.text(), "cancel restores edit-start value");

	filter_t filter;
	filter.wildcard("cat");
	assert_equal_strict("cat", filter.text(), "filter preserves user input");
	assert_equal(true, filter.match_text(str::cache("bobcatfish")), "filter applies contains matching");
}

static void should_clear_detail_row_layout_metrics()
{
	df::item_row_draw_info info;
	info.title.extent = 400;
	info.title.width = 300;
	info.file_size.extent = 100;
	info.file_size.width = 80;
	info.file_size.val_min = 10;
	info.file_size.val_max = 1000;
	info.presence.extent = 40;
	info.presence.width = 40;

	info.clear_for_layout();

	assert_equal(0, info.title.extent, "title extent reset");
	assert_equal(0, info.title.width, "title width reset");
	assert_equal(0, info.file_size.extent, "size extent reset");
	assert_equal(0, info.file_size.width, "size width reset");
	assert_equal(static_cast<double>(INT64_MAX), info.file_size.val_min, "size minimum reset");
	assert_equal(static_cast<double>(INT64_MIN), info.file_size.val_max, "size maximum reset");
	assert_equal(0, info.presence.extent, "presence extent reset");
	assert_equal(0, info.presence.width, "presence width reset");
}

static void should_classify_aspect_ratio_groups()
{
	const auto assert_group = [](const sizei dimensions, const aspect_ratio_bucket expected_bucket,
	                             const bool expected_portrait, const std::string_view name)
	{
		const auto actual = calc_aspect_ratio_group(dimensions);
		assert_equal(static_cast<int>(expected_bucket), static_cast<int>(actual.bucket), name, "aspect ratio bucket");
		assert_equal(expected_portrait, actual.is_portrait, name, "aspect ratio orientation");
	};

	assert_group({1000, 1000}, aspect_ratio_bucket::square, false, "square");
	assert_group({1280, 1024}, aspect_ratio_bucket::five_four, false, "5:4");
	assert_group({4000, 3000}, aspect_ratio_bucket::four_three, false, "4:3");
	assert_group({6000, 4000}, aspect_ratio_bucket::three_two, false, "3:2");
	assert_group({1920, 1200}, aspect_ratio_bucket::sixteen_ten, false, "16:10");
	assert_group({1920, 1080}, aspect_ratio_bucket::sixteen_nine, false, "16:9");
	assert_group({2520, 1080}, aspect_ratio_bucket::twenty_one_nine, false, "21:9");
	assert_group({1080, 1920}, aspect_ratio_bucket::sixteen_nine, true, "9:16 portrait");
	assert_group({4032, 3024}, aspect_ratio_bucket::four_three, false, "cropped within tolerance");
	assert_group({1000, 700}, aspect_ratio_bucket::other, false, "other landscape");
	assert_group({700, 1000}, aspect_ratio_bucket::other, true, "other portrait");
	assert_group({}, aspect_ratio_bucket::other, false, "invalid dimensions");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Flex layout
///////////////////////////////////////////////////////////////////////////////////////////////////

// Behaves like text: content that does not fit the width limit wraps onto a second line.
class flex_wrapping_test_element final : public view_element
{
	sizei _desired;

public:
	explicit flex_wrapping_test_element(const sizei desired) : _desired(desired)
	{
		padding(0);
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		if (width_limit >= _desired.cx) return _desired;
		return {width_limit, _desired.cy * 2};
	}
};

static void should_layout_flex_elements()
{
	flex_test_measure_context mc;
	auto first = std::make_shared<flex_test_element>(sizei{40, 10});
	auto second = std::make_shared<flex_test_element>(sizei{40, 20});
	auto third = std::make_shared<flex_test_element>(sizei{40, 15});
	const std::vector<view_element_ptr> wrapping{first, second, third};
	const auto wrapped = calc_flex_layout(wrapping, mc, {100, 0}, {});
	assert_equal(5, wrapped.layout_bounds[0].top, "short item is centered on first flex line");
	assert_equal(0, wrapped.layout_bounds[1].top, "second item remains on first flex line");
	assert_equal(20, wrapped.layout_bounds[2].top, "third item wraps to second flex line");
	assert_equal(35, wrapped.extent.cy, "wrapped flex extent");

	first->flex.grow = 1.0f;
	second->flex.grow = 1.0f;
	const std::vector<view_element_ptr> growing{first, second};
	const auto grown = calc_flex_layout(growing, mc, {100, 0}, {});
	assert_equal(50, grown.layout_bounds[0].width(), "first flex item shares free space");
	assert_equal(50, grown.layout_bounds[1].width(), "second flex item shares free space");

	first->flex.grow = 0.0f;
	second->flex.grow = 0.0f;
	second->flex.main_start_auto = true;
	const auto justified = calc_flex_layout(growing, mc, {100, 0}, {});
	assert_equal(60, justified.layout_bounds[1].left, "auto margin right aligns trailing flex item");

	second->is_visible(false);
	const auto hidden = calc_flex_layout(growing, mc, {100, 0}, {});
	assert_equal(40, hidden.extent.cx, "hidden flex item consumes no space");
	assert_equal(true, hidden.layout_bounds[1] == recti{}, "hidden flex item has empty bounds");

	second->is_visible(true);
	first->flex.shrink = 1.0f;
	second->flex.shrink = 1.0f;
	first->flex.basis = 60;
	second->flex.basis = 60;
	first->flex.min_size.cx = 55;
	second->flex.min_size.cx = 40;
	flex_container_layout no_wrap;
	no_wrap.wrap = flex_wrap::no_wrap;
	const auto shrunk = calc_flex_layout(growing, mc, {100, 0}, no_wrap);
	assert_equal(55, shrunk.layout_bounds[0].width(), "flex shrink respects first minimum");
	assert_equal(45, shrunk.layout_bounds[1].width(), "flex shrink redistributes after minimum");

	auto column_first = std::make_shared<flex_test_element>(sizei{30, 10});
	auto column_second = std::make_shared<flex_test_element>(sizei{40, 20});
	const std::vector<view_element_ptr> column_elements{column_first, column_second};
	flex_container_layout column;
	column.direction = flex_direction::column;
	column.wrap = flex_wrap::no_wrap;
	const auto measured_column = calc_flex_layout(column_elements, mc, {100, -1}, column);
	assert_equal(30, measured_column.extent.cy, "unconstrained flex column measures intrinsic height");
	assert_equal(10, measured_column.layout_bounds[1].top, "unconstrained flex column stacks items");

	mc.padding2 = 8;
	auto divider = std::make_shared<divider_element>();
	divider->padding(0);
	const std::vector<view_element_ptr> divided_column{divider, column_second};
	const auto fixed_height_divider = calc_flex_layout(divided_column, mc, {100, 100}, column);
	assert_equal(8, fixed_height_divider.layout_bounds[0].height(),
	             "column divider keeps its intrinsic height instead of consuming free space");
	assert_equal(8, fixed_height_divider.layout_bounds[1].top,
	             "control after a column divider remains adjacent and visible");
	mc.padding2 = 0;

	column_first->flex.break_after = true;
	const auto broken_column = calc_flex_layout(column_elements, mc, {100, -1}, column);
	assert_equal(30, broken_column.layout_bounds[1].left,
	             "explicit break creates a new column when automatic wrapping is disabled");

	column_first->flex.break_after = false;
	column_first->flex.align_self = flex_align::stretch;
	column.padding = {5, 7};
	const auto padded_column = calc_flex_layout(column_elements, mc, {100, -1}, column);
	assert_equal(5, padded_column.layout_bounds[0].left, "column padding offsets the first item");
	assert_equal(90, padded_column.layout_bounds[0].width(), "explicit stretch fills the column cross axis");
	assert_equal(44, padded_column.extent.cy, "column padding contributes to intrinsic extent");

	mc.scale_factor = 2.0;
	const auto scaled_padded_column = calc_flex_layout(column_elements, mc, {200, -1}, column);
	assert_equal(10, scaled_padded_column.layout_bounds[0].left, "flex padding scales from logical units");
	assert_equal(58, scaled_padded_column.extent.cy, "scaled flex padding contributes once to intrinsic extent");
	mc.scale_factor = 1.0;

	ui::control_layouts positions;
	const auto applied_extent = layout_flex_elements(column_elements, mc, positions, {10, 20, 110, 80}, column);
	assert_equal(15, column_first->bounds.left, "applied flex layout offsets child bounds");
	assert_equal(padded_column.extent.cy, applied_extent.cy, "applied flex layout returns content extent");

	column.padding = {};
	column.justify = flex_justify::center;
	const auto centered_column = calc_flex_layout(column_elements, mc, {100, 100}, column);
	assert_equal(35, centered_column.layout_bounds[0].top, "flex column centers intrinsic content vertically");

	column.justify = flex_justify::start;
	column_first->flex.align_self = flex_align::automatic;
	column_first->flex = column_first->flex | flex_item::media;
	column_first->flex.basis = 100;
	const auto shrunk_column = calc_flex_layout(column_elements, mc, {100, 100}, column);
	assert_equal(80, shrunk_column.layout_bounds[0].height(), "flex column shrinks media to fit trailing controls");
	assert_equal(80, shrunk_column.layout_bounds[1].top, "trailing control remains visible after media shrink");

	auto fixed = std::make_shared<flex_test_element>(sizei{30, 10});
	auto flexible_first = std::make_shared<flex_test_element>(sizei{40, 10});
	auto flexible_second = std::make_shared<flex_test_element>(sizei{40, 10});
	fixed->flex.basis = 30;
	fixed->flex.shrink = 1.0f;
	flexible_first->flex.basis = 0;
	flexible_first->flex.grow = 1.0f;
	flexible_second->flex.basis = 0;
	flexible_second->flex.grow = 1.0f;
	flex_container_layout columns;
	columns.wrap = flex_wrap::no_wrap;
	columns.gap.cx = 5;
	const auto column_row = calc_flex_layout(
		std::vector<view_element_ptr>{fixed, flexible_first, flexible_second}, mc, {100, -1}, columns);
	assert_equal(30, column_row.layout_bounds[0].width(), "fixed flex column keeps its basis");
	assert_equal(30, column_row.layout_bounds[1].width(), "first flexible column shares remaining width");
	assert_equal(30, column_row.layout_bounds[2].width(), "second flexible column shares remaining width");

	auto capped = std::make_shared<flex_wrapping_test_element>(sizei{40, 10});
	capped->flex.max_size.cx = 25;
	capped->flex.align_self = flex_align::center;
	const std::vector<view_element_ptr> capped_elements{capped};
	const auto capped_column = calc_flex_layout(capped_elements, mc, {100, -1}, column);
	assert_equal(25, capped_column.layout_bounds[0].width(), "column item is capped by its maximum width");
	assert_equal(20, capped_column.extent.cy, "column item measures inside its maximum width");

	const auto capped_row = calc_flex_layout(capped_elements, mc, {100, -1}, {});
	assert_equal(25, capped_row.layout_bounds[0].width(), "row item is capped by its maximum width");
	assert_equal(20, capped_row.extent.cy, "row item measures inside its maximum width");

	auto padded_first = std::make_shared<flex_test_element>(sizei{20, 10});
	auto padded_second = std::make_shared<flex_test_element>(sizei{20, 10});
	flex_container_layout centered_row;
	centered_row.wrap = flex_wrap::no_wrap;
	centered_row.justify = flex_justify::center;
	centered_row.padding = {10, 0};
	const auto centered = calc_flex_layout(std::vector<view_element_ptr>{padded_first, padded_second}, mc,
	                                       {100, -1}, centered_row);
	assert_equal(30, centered.layout_bounds[0].left, "centred line keeps the container's leading padding");
	assert_equal(100, centered.extent.cx, "a justified line reports the whole main axis it positions within");

	auto capped_cross = std::make_shared<flex_test_element>(sizei{20, 10});
	capped_cross->flex.align_self = flex_align::stretch;
	capped_cross->flex.max_size.cy = 12;
	auto tall = std::make_shared<flex_test_element>(sizei{20, 40});
	flex_container_layout stretch_row;
	stretch_row.wrap = flex_wrap::no_wrap;
	const auto stretched = calc_flex_layout(std::vector<view_element_ptr>{capped_cross, tall}, mc, {100, -1},
	                                        stretch_row);
	assert_equal(12, stretched.layout_bounds[0].height(), "stretch respects the item's maximum cross size");

	auto only_child = std::make_shared<flex_test_element>(sizei{20, 10});
	only_child->is_visible(false);
	flex_container_layout hidden_column;
	hidden_column.direction = flex_direction::column;
	hidden_column.padding = {10, 10};
	const auto nothing_visible = calc_flex_layout(std::vector<view_element_ptr>{only_child}, mc, {100, -1},
	                                              hidden_column);
	assert_equal(0, nothing_visible.extent.cx, "a container with nothing visible occupies no width");
	assert_equal(0, nothing_visible.extent.cy, "a container with nothing visible occupies no height");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Item tile geometry
///////////////////////////////////////////////////////////////////////////////////////////////////

// selection-controls.md: comparison is like with like, decided from stable traits alone.
static void should_limit_comparison_to_like_pairs()
{
	const auto jpg = files::file_type_from_name("a.jpg");
	const auto png = files::file_type_from_name("b.png");
	const auto mp4 = files::file_type_from_name("a.mp4");
	const auto mp3 = files::file_type_from_name("a.mp3");
	const auto txt = files::file_type_from_name("a.txt");

	assert_equal(true, can_compare_file_types(jpg, png), "two images compare");
	assert_equal(true, can_compare_file_types(mp4, mp4), "two previewable videos compare");
	assert_equal(false, can_compare_file_types(jpg, mp4), "image and video do not compare");
	assert_equal(false, can_compare_file_types(mp3, mp3), "two audio files do not compare");
	assert_equal(false, can_compare_file_types(txt, txt), "two documents do not compare");
	assert_equal(false, can_compare_file_types(&file_type::folder, &file_type::folder),
	             "two folders do not compare");
	assert_equal(false, can_compare_file_types(nullptr, jpg), "an unknown type does not compare");
}

// A video with embedded cover art draws the cover art on its tile, so the tile has to be the shape
// of the art. Sizing it from the video's own dimensions letterboxed the art inside a frame-shaped
// tile, and the republish that follows every scan pass used to revert it to the video shape.
static void should_shape_the_tile_by_what_it_draws()
{
	df::index_file_item indexed;
	indexed.name = str::cache("clip.mp4");
	indexed.ft = files::file_type_from_name(indexed.name);

	const auto md = std::make_shared<prop::item_metadata>();
	md->width = 1920;
	md->height = 1080;
	indexed.metadata.store(md);

	const auto path = df::file_path("c:\\clip.mp4");
	const auto item = std::make_shared<df::item_element>(path, indexed);

	assert_equal(1920, item->layout_dims().cx, "video width shapes the tile before cover art arrives");
	assert_equal(1080, item->layout_dims().cy, "video height shapes the tile before cover art arrives");
	assert_equal(true, item->layout_aspect_known(), "indexed dimensions are an exact aspect");

	const auto cover_art = std::make_shared<ui::image>(df::blob(16), sizei(600, 600),
	                                                   ui::image_format::JPEG, ui::orientation::top_left);
	item->thumbnail({}, cover_art, {});

	assert_equal(600, item->layout_dims().cx, "cover art width shapes the tile that draws it");
	assert_equal(600, item->layout_dims().cy, "cover art height shapes the tile that draws it");
	assert_equal(true, item->layout_aspect_known(), "a cover art aspect is exact, so the tile may justify");

	// scan_items republishes every displayed item on every pass.
	const auto republished = item->update(path, indexed);

	assert_equal(600, item->layout_dims().cx, "a republish does not revert the tile to the video shape");
	assert_equal(false, republished, "an unchanged republish asks for no layout pass");
}

// A thumbnail is a downscaled stand-in, so it may shape a tile whose real aspect is unknown but must
// never earn the row justification that a known aspect does.
static void should_not_justify_a_tile_shaped_by_a_thumbnail()
{
	df::index_file_item indexed;
	indexed.name = str::cache("unscanned.jpg");
	indexed.ft = files::file_type_from_name(indexed.name);

	const auto path = df::file_path("c:\\unscanned.jpg");
	const auto item = std::make_shared<df::item_element>(path, indexed);

	assert_equal(false, item->layout_aspect_known(), "nothing known before a scan");

	const auto thumb = std::make_shared<ui::image>(df::blob(16), sizei(160, 120),
	                                               ui::image_format::JPEG, ui::orientation::top_left);
	item->thumbnail(thumb, {}, {});

	assert_equal(160, item->layout_dims().cx, "the thumbnail stands in for an unknown aspect");
	assert_equal(false, item->layout_aspect_known(), "a stand-in aspect is never justified");

	// The scan lands and the index now knows the real size, which outranks the thumbnail.
	const auto md = std::make_shared<prop::item_metadata>();
	md->width = 4000;
	md->height = 3000;
	indexed.metadata.store(md);

	assert_equal(true, item->update(path, indexed), "a newly scanned size asks for a layout pass");
	assert_equal(4000, item->layout_dims().cx, "the indexed size outranks the thumbnail");
	assert_equal(true, item->layout_aspect_known(), "the real aspect is now known");
}

// A tile fills its cell by cropping, so every difference between the cell's shape and the image's
// shape is hidden pixels. The row solves one height and takes each width from it, which is what stops
// a row holding one or two portraits from cutting the top and bottom off them.
static void should_keep_tile_aspect_when_laying_out_a_row()
{
	null_state_strategy ss;
	null_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);
	view_state s(ss, as, index, make_test_player());

	flex_test_measure_context mc;
	constexpr auto width_limit = 1200;
	constexpr auto crop_tolerance = 0.05;
	auto next_name = 0;

	const auto make_item = [&next_name](const int cx, const int cy)
	{
		const auto name = "item" + std::to_string(++next_name) + ".jpg";

		df::index_file_item indexed;
		indexed.name = str::cache(name);
		indexed.ft = files::file_type_from_name(indexed.name);

		const auto md = std::make_shared<prop::item_metadata>();
		md->width = static_cast<uint16_t>(cx);
		md->height = static_cast<uint16_t>(cy);
		indexed.metadata.store(md);

		return std::make_shared<df::item_element>(df::file_path("c:\\" + name), indexed);
	};

	const auto assert_row_aspects = [&](df::item_elements items, const std::string_view name)
	{
		const auto group = std::make_shared<df::item_group>(s, std::move(items), df::item_group_display::icons,
		                                                    df::group_key{});
		group->measure(mc, width_limit);

		const auto& layout_bounds = group->_layout_bounds;
		assert_equal(group->items().size(), layout_bounds.size(), name, "a cell for every item");

		for (auto i = 0u; i < layout_bounds.size(); ++i)
		{
			const auto dims = group->items()[i]->layout_dims();
			const auto image_aspect = static_cast<double>(dims.cx) / dims.cy;
			const auto cell_aspect = static_cast<double>(layout_bounds[i].width()) / layout_bounds[i].height();
			const auto hidden = 1.0 - std::min(image_aspect, cell_aspect) / std::max(image_aspect, cell_aspect);

			assert_equal(true, hidden <= crop_tolerance + 0.01, name, "tile keeps its aspect");
			assert_equal(true, layout_bounds[i].right <= width_limit, name, "tile stays inside the row");
		}
	};

	assert_row_aspects({make_item(2000, 3000)}, "one portrait"sv);
	assert_row_aspects({make_item(2000, 3000), make_item(2000, 3000)}, "two portraits"sv);
	assert_row_aspects({make_item(3000, 2000), make_item(2000, 3000), make_item(4000, 3000)}, "mixed row"sv);
	assert_row_aspects({make_item(400, 400), make_item(64, 64)}, "small squares"sv);
	assert_row_aspects({make_item(12000, 1000)}, "panorama"sv);
}

// A header sort reorders the reviewed rows while the run stays in plan order, so progress and
// results must resolve a work index by identity. Indexing by display position reports one file's
// outcome against another.
static void should_resolve_list_rows_by_work_index()
{
	const auto resolve = [](const std::vector<int>& rows, const size_t work_index)
	{
		return list_view::row_for_work_index(rows.size(), work_index, [&rows](const size_t i) { return rows[i]; });
	};

	// Unsorted, which is the common case and the one the fast path answers without scanning.
	const std::vector<int> in_order{0, 1, 2, 3};
	assert_equal(0, resolve(in_order, 0), "in order work 0"sv);
	assert_equal(2, resolve(in_order, 2), "in order work 2"sv);
	assert_equal(3, resolve(in_order, 3), "in order work 3"sv);
	assert_equal(-1, resolve(in_order, 4), "in order beyond the run"sv);

	// Plan order 0,1,2,3 shown after a sort as 2,0,3,1.
	const std::vector<int> sorted{2, 0, 3, 1};
	assert_equal(1, resolve(sorted, 0), "sorted work 0"sv);
	assert_equal(3, resolve(sorted, 1), "sorted work 1"sv);
	assert_equal(0, resolve(sorted, 2), "sorted work 2"sv);
	assert_equal(2, resolve(sorted, 3), "sorted work 3"sv);

	// Rows the run skips carry -1, so they must never be selected - including for work index 0,
	// which a position-based lookup would hand to the first row.
	const std::vector<int> with_skipped{-1, -1, 0, -1, 1};
	assert_equal(2, resolve(with_skipped, 0), "first acting row"sv);
	assert_equal(4, resolve(with_skipped, 1), "second acting row"sv);

	// No row means no highlight, rather than the nearest one.
	assert_equal(-1, resolve(with_skipped, 2), "work index beyond the run"sv);
	assert_equal(-1, resolve({}, 0), "no rows"sv);
}

// The row being processed is held, not indexed. Reordering the reviewed list mid-run used to clear
// whichever row had since moved into the stored slot, leaving the real one highlighted for good.
static void should_track_the_active_row_across_a_reorder()
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
	s.select_all(view);
	s.update_selection();

	assert_equal(true, s.selected_items().size() > 2, "test folder has a selection to review");

	const auto tags = std::make_shared<tags_view>(s, nullptr);
	tags->activate({100, 100});

	assert_equal(true, tags->_rows.size() > 2, "test folder fills the reviewed list");

	const auto highlighted = [&tags]
	{
		return std::ranges::count_if(tags->_rows, [](const auto& row) { return row->_bg_color.a > 0.0f; });
	};

	tags->begin_processing(tags->_rows.size());
	tags->processing_item(0, 1);

	const auto first = tags->_rows.front();
	assert_equal(1, static_cast<int>(highlighted()), "one row is marked in progress");
	assert_equal(true, first->_bg_color.a > 0.0f, "the row the run named is the marked one");

	// Any reorder will do; a header click during review is the reachable one.
	std::ranges::reverse(tags->_rows);
	tags->processing_item(0, 2);

	assert_equal(1, static_cast<int>(highlighted()), "the previous row is cleared even though it moved");
	assert_equal(true, tags->_rows.front()->_bg_color.a > 0.0f, "the newly named row is the marked one");

	tags->end_processing();
	assert_equal(0, static_cast<int>(highlighted()), "no row stays marked once the run ends");
}

void register_view_tests(view_state& state, test_registry& tests)
{
	tests.add("Should track the active row across a reorder"s, should_track_the_active_row_across_a_reorder);
	tests.add("Should resolve list rows by work index"s, should_resolve_list_rows_by_work_index);
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
	tests.add("Should centre a block with no detail whatever verbose is"s,
	          should_centre_a_block_with_no_detail_whatever_verbose_is);
	tests.add("Should leave full screen for task views"s, should_leave_full_screen_for_task_views);
	tests.add("Should preserve view scroller anchor across layout"s,
	          should_preserve_view_scroller_anchor_across_layout);
	tests.add("Should reserve view scroller footer"s, should_reserve_view_scroller_footer);
	tests.add("Should drag view scroller thumb from grab point"s,
	          should_drag_view_scroller_thumb_from_grab_point);
	tests.add("Should close view scroller bands over track"s, should_close_view_scroller_bands_over_track);
	tests.add("Should survive a host with no window"s, should_survive_a_host_with_no_window);
	tests.add("Should keep the processing row clear of the view chrome"s,
	          should_keep_the_processing_row_clear_of_the_view_chrome);
	tests.add("Should answer a null frame without side effects"s, should_answer_a_null_frame_without_side_effects);
	tests.add("Should layout selection thumbnail collage"s, should_layout_selection_thumbnail_collage);
	tests.add("Should edit single-line text"s, should_edit_single_line_text);
	tests.add("Should clear detail row layout metrics"s, should_clear_detail_row_layout_metrics);
	tests.add("Should classify aspect ratio groups"s, should_classify_aspect_ratio_groups);

	//
	// Flex layout
	//
	tests.add("Should layout flex elements"s, should_layout_flex_elements);

	//
	// Item tile geometry
	//
	tests.add("Should limit comparison to like pairs"s, should_limit_comparison_to_like_pairs);
	tests.add("Should shape the tile by what it draws"s, should_shape_the_tile_by_what_it_draws);
	tests.add("Should not justify a tile shaped by a thumbnail"s, should_not_justify_a_tile_shaped_by_a_thumbnail);
	tests.add("Should keep tile aspect when laying out a row"s, should_keep_tile_aspect_when_laying_out_a_row);
}
