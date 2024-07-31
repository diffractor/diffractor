// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2024  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

inline view_element_ptr make_icon_link_element2(const icon_index i, commands cmd, const view_element_style style_in)
{
	const wchar_t text[2] = { static_cast<wchar_t>(i), 0 };
	auto element = std::make_shared<link_element>(str::utf16_to_utf8(text), cmd, ui::style::font_face::icons,
		ui::style::text_style::single_line_center, style_in, true);
	return element;
}

class media_view final : public view_base
{
	using this_type = media_view;

	view_state& _state;
	view_host_ptr _host;

	view_element_ptr _left_arrow_element;
	view_element_ptr _right_arrow_element;
	view_element_ptr _media_element;
	view_elements_ptr _controls_element;
	display_state_ptr _display;

	sizei _client_extent;

public:
	float overlay_alpha = 1.0f;
	float overlay_alpha_target = 1.0f;

	media_view(view_state& state, view_host_ptr host) :
		_state(state),
		_host(std::move(host)),
		_left_arrow_element(make_icon_link_element2(icon_index::back_image, commands::browse_previous_item,
			view_element_style::none)),
		_right_arrow_element(make_icon_link_element2(icon_index::next_image, commands::browse_next_item,
			view_element_style::none))
	{
	}

	~media_view() override
	{
		df::log(__FUNCTION__, "destruct"sv);
	}

	recti calc_media_bounds() const
	{
		return { 0, 0, _client_extent.cx, _client_extent.cy };
	}

	void activate(const sizei extent) override
	{
		_client_extent = extent;
		update_media_elements();
	}

	void deactivate() override
	{
	}

	void refresh() override
	{
		_state.open(_host, _state.search(), {});
	}

	menu_type context_menu(const pointi loc) override
	{
		return menu_type::media;
	}

	void mouse_wheel(const pointi loc, const int zDelta, const ui::key_state keys) override
	{
		_state.select_next(_host, zDelta <= 0, keys.control, keys.shift);
	}

	void render(ui::draw_context& dc, view_controller_ptr controller) override
	{
		const auto original_colors = dc.colors;
		dc.colors.overlay_alpha = overlay_alpha;
		dc.colors.bg_alpha = overlay_alpha * 0.55f;

		if (_media_element)
		{
			_media_element->render(dc, { 0, 0 });
		}

		if (!_display->zoom() && !_display->comparing())
		{
			dc.colors.alpha = dc.colors.alpha * overlay_alpha;

			if (_controls_element)
			{
				_controls_element->render(dc, { 0, 0 });
			}

			_left_arrow_element->render(dc, { 0, 0 });
			_right_arrow_element->render(dc, { 0, 0 });

			dc.colors.alpha = original_colors.alpha;

			const auto is_slide_show = _display->is_playing_slideshow();

			if (is_slide_show)
			{
				const auto alpha = dc.colors.alpha * (1.0f - dc.colors.overlay_alpha);

				auto progress_bounds = calc_media_bounds().inflate(-20);
				progress_bounds.top = progress_bounds.bottom - 16;
				progress_bounds.right = progress_bounds.left + 64;

				dc.draw_rect(progress_bounds, ui::color(ui::style::color::group_background, alpha * 0.7f));

				auto r = progress_bounds.inflate(-2);
				r.right = r.left + df::mul_div(std::min(_display->slideshow_pos(), 1000), r.width(), 1000);
				dc.draw_rect(r, ui::color(ui::style::color::view_selected_background, alpha * 0.7f));
			}
		}

		dc.colors = original_colors;
	}

	void layout(ui::measure_context& mc, const sizei extent) override
	{
		_client_extent = extent;

		ui::control_layouts positions;
		auto avail_bounds = calc_media_bounds();
		const auto scale_factor = mc.scale_factor;

		if (!_state.is_full_screen || !_display->is_one())
		{
			avail_bounds = avail_bounds.inflate(df::round(-4 * scale_factor));
		}

		const int minumum_media_control_width = df::round(32 * 7 * scale_factor);
		const auto overlay_media_control = (_display->is_one() && _display->display_item_has_trait(
			file_traits::hide_overlays)) || _display->comparing();
		auto media_bounds_avail = avail_bounds;

		if (avail_bounds.width() > minumum_media_control_width && _controls_element)
		{
			const auto control_limit = avail_bounds.inflate(-mc.padding2);
			const auto center_limit = df::mul_div(control_limit.width(), 5, 11);
			const auto limit = center_limit;
			const auto elements_extent = _controls_element->measure(mc, limit);
			const auto control_avail_bounds = recti(control_limit.left, control_limit.bottom - elements_extent.cy,
				control_limit.right, control_limit.bottom);
			const auto control_bounds = center_rect(sizei{ center_limit, elements_extent.cy }, control_avail_bounds);

			_controls_element->layout(mc, control_bounds, positions);

			if (!overlay_media_control)
			{
				media_bounds_avail.bottom = control_bounds.top - mc.padding2;
			}
		}

		const auto arrow_cx = avail_bounds.width() / 17;
		const auto arrow_cy = avail_bounds.height() / 7;
		const auto left_top = (avail_bounds.top + avail_bounds.bottom - arrow_cy) / 2;
		const auto right_top = (avail_bounds.top + avail_bounds.bottom - arrow_cy) / 2;

		_left_arrow_element->layout(
			mc, recti(avail_bounds.left, left_top, avail_bounds.left + arrow_cx, left_top + arrow_cy), positions);
		_right_arrow_element->layout(mc, recti(avail_bounds.right - arrow_cx, right_top, avail_bounds.right,
			right_top + arrow_cy), positions);

		if (!overlay_media_control)
		{
			media_bounds_avail.left += arrow_cx + mc.padding2;
			media_bounds_avail.right -= arrow_cx + mc.padding2;
		}

		if (_media_element)
		{
			const auto media_extent = _media_element->measure(mc, media_bounds_avail.width());
			auto media_bounds = recti(media_bounds_avail.top_left(), media_extent);

			if (media_bounds.height() < media_bounds_avail.height() || media_bounds.width() < media_bounds_avail.
				width())
			{
				media_bounds = center_rect(media_bounds, media_bounds_avail);
			}
			else if (_media_element->is_style_bit_set(view_element_style::shrink) && media_bounds.height() >
				media_bounds_avail.height() || media_bounds.width() > media_bounds_avail.width())
			{
				media_bounds = media_bounds_avail;
			}

			_media_element->layout(mc, media_bounds, positions);
		}

		/*const auto zoom_control_extent = _zoom_element->measure(mc, avail_bounds.width());
		_zoom_element->layout(mc, recti(pointi(mc.component_snap, mc.component_snap), zoom_control_extent), positions);
		_zoom_element->_view_bounds = avail_bounds;		*/

		_host->frame()->invalidate();
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, const pointi loc) override
	{
		const pointi media_offset{};
		view_controller_ptr controller;

		if (_display->zoom() || _display->comparing())
		{
			if (_media_element)
			{
				controller = _media_element->controller_from_location(host, loc, media_offset, {});
			}
		}
		else
		{
			if (!controller)
			{
				controller = _left_arrow_element->controller_from_location(host, loc, media_offset, {});
			}

			if (!controller)
			{
				controller = _right_arrow_element->controller_from_location(host, loc, media_offset, {});
			}

			if (!controller && _controls_element && _media_element)
			{
				controller = _controls_element->controller_from_location(host, loc, media_offset, {});

				if (!controller && loc.y < _controls_element->bounds.top)
				{
					const std::vector<recti> excluded_bounds = {
						_left_arrow_element->bounds,
						_right_arrow_element->bounds,
						_controls_element->bounds,
					};

					controller = _media_element->controller_from_location(host, loc, media_offset, excluded_bounds);
				}
			}
		}

		return controller;
	}

	void display_changed() override
	{
		_state.invalidate_view(view_invalid::view_layout);
	}

	void update_media_elements() override;

	void broadcast_event(const view_element_event& event) const override
	{
		if (_left_arrow_element) _left_arrow_element->dispatch_event(event);
		if (_right_arrow_element) _right_arrow_element->dispatch_event(event);
		if (_controls_element) _controls_element->dispatch_event(event);
		if (_media_element) _media_element->dispatch_event(event);
	}
};
