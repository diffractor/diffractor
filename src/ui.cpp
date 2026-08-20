// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: UI element layout and rendering. Implements view controllers, image
// layout algorithms, and comparison view controls.

#include "pch.h"
#include "util_geometry.h"
#include "model.h"
#include "model_index.h"
#include "ui_elements.h"
#include "ui_dialog.h"
#include "ui_controls.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::unordered_map<void*, std::function<bool()>> ui::animations;
bool ui::animations_enabled = true;
// Matches alpha_fade_rate at 60Hz until the first frame recomputes it.
float ui::animation_step_factor = 0.333f;

std::vector<recti> ui::layout_collage(const recti draw_bounds, const std::vector<sizei>& dimensions)
{
	constexpr size_t max_cells = 24;
	constexpr double golden_ratio = 1.6180339887498948482;
	const auto cell_count = std::min(dimensions.size(), max_cells);
	std::vector<recti> results(cell_count);

	if (cell_count == 0 || draw_bounds.is_empty()) return {};
	if (cell_count == 1)
	{
		results.front() = draw_bounds;
		return results;
	}

	std::vector<double> aspect_ratios;
	aspect_ratios.reserve(cell_count);
	for (auto index = 0u; index < cell_count; ++index)
	{
		const auto dimensions_safe = dimensions[index];
		aspect_ratios.emplace_back(dimensions_safe.cx > 0 && dimensions_safe.cy > 0
			                           ? static_cast<double>(dimensions_safe.cx) / dimensions_safe.cy
			                           : 1.0);
	}

	const auto canvas_aspect = static_cast<double>(draw_bounds.width()) / draw_bounds.height();
	const auto feature = static_cast<size_t>(std::min_element(aspect_ratios.begin(), aspect_ratios.end(),
	                                                          [canvas_aspect](const double left, const double right)
	                                                          {
		                                                          return std::abs(std::log(left / canvas_aspect)) <
			                                                          std::abs(std::log(right / canvas_aspect));
	                                                          }) - aspect_ratios.begin());

	std::vector<double> weights(cell_count, 1.0);
	weights[feature] = golden_ratio * golden_ratio;
	if (cell_count >= 6)
	{
		weights[(feature + cell_count / 2) % cell_count] = golden_ratio;
	}

	const auto weight_sum = [&weights](const size_t start, const size_t end)
	{
		return std::accumulate(weights.begin() + start, weights.begin() + end, 0.0);
	};

	const auto layout = [&](const auto& self, const size_t start, const size_t end, const recti bounds) -> void
	{
		if (end - start == 1)
		{
			results[start] = bounds;
			return;
		}

		const auto total_weight = weight_sum(start, end);
		auto split = start + 1;
		auto best_balance = std::numeric_limits<double>::max();
		for (auto candidate = start + 1; candidate < end; ++candidate)
		{
			const auto balance = std::abs(weight_sum(start, candidate) / total_weight - 0.5);
			if (balance < best_balance)
			{
				best_balance = balance;
				split = candidate;
			}
		}

		const auto first_fraction = weight_sum(start, split) / total_weight;
		if (bounds.width() > 1 && (bounds.width() >= bounds.height() || bounds.height() <= 1))
		{
			const auto split_x = std::clamp(bounds.left + df::round(bounds.width() * first_fraction),
			                                bounds.left + 1, bounds.right - 1);
			self(self, start, split, {bounds.left, bounds.top, split_x, bounds.bottom});
			self(self, split, end, {split_x, bounds.top, bounds.right, bounds.bottom});
		}
		else if (bounds.height() > 1)
		{
			const auto split_y = std::clamp(bounds.top + df::round(bounds.height() * first_fraction),
			                                bounds.top + 1, bounds.bottom - 1);
			self(self, start, split, {bounds.left, bounds.top, bounds.right, split_y});
			self(self, split, end, {bounds.left, split_y, bounds.right, bounds.bottom});
		}
		else
		{
			for (auto i = start; i < end; ++i) results[i] = bounds;
		}
	};

	layout(layout, 0, cell_count, draw_bounds);

	return results;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class compare_controller final : public view_controller
{
public:
	std::shared_ptr<side_by_side_control> _parent;
	bool _tracking = false;

	compare_controller(const view_host_ptr& host, std::shared_ptr<side_by_side_control> mc,
	                   const recti bounds) : view_controller(host, bounds), _parent(std::move(mc))
	{
	}

	~compare_controller() override
	{
		_tracking = false;
		_parent->compare(0, _tracking);
	}

	ui::style::cursor cursor() const override
	{
		return ui::style::cursor::left_right;
	}

	void draw(ui::draw_context& dc) override
	{
		const auto handle_color = view_handle_color(false, true, _tracking, dc.frame_has_focus, true);
		const auto hover_alpha = _alpha * 0.77f;
		const auto hover_color = handle_color.aa(hover_alpha);

		if (!_tracking)
		{
			dc.draw_rounded_rect(_bounds, hover_color, dc.padding1);
		}
	}

	void on_mouse_left_button_down(const pointi loc, const ui::key_state keys) override
	{
		_tracking = true;
		_parent->compare(loc.x, _tracking);
		_parent->_display->_async.invalidate_view(view_invalid::tooltip);
	}

	void on_mouse_move(const pointi loc) override
	{
		_parent->compare(loc.x, _tracking);
	}

	void on_mouse_left_button_up(const pointi loc, const ui::key_state keys) override
	{
		_tracking = false;
		_parent->compare(loc.x, _tracking);
	}

	bool escape() override
	{
		if (!_tracking) return false;
		_tracking = false;
		_parent->compare(0, _tracking);
		return true;
	}

	void popup_from_location(view_hover_element& hover) override
	{
		if (!_tracking)
		{
			hover.elements->add(make_icon_element(icon_index::compare, flex_item::no_break));
			hover.elements->add(std::make_shared<text_element>(tt.compare));
			hover.elements->add(std::make_shared<text_element>(tt.compare_tooltip));
			hover.preferred_size = df::mul_div(hover.preferred_size, 5, 8);
			hover.window_bounds = hover.active_bounds = _bounds;
		}
	}
};

class preview_controller final : public view_controller
{
	std::shared_ptr<side_by_side_control> _parent;
	recti _scrubber_bounds;
	bool _tracking = false;
	bool _hover = false;

public:
	preview_controller(const view_host_ptr& host, std::shared_ptr<side_by_side_control> mc,
	                   const recti control_bounds, const recti scrubber_bounds) :
		view_controller(host, control_bounds), _parent(std::move(mc)), _scrubber_bounds(scrubber_bounds)
	{
	}

	~preview_controller() override
	{
		_tracking = false;
		_hover = false;
		_parent->set_style_bit(view_element_style::tracking, false);
		_parent->set_style_bit(view_element_style::hover, false);

		_parent->_display->_async.invalidate_view(view_invalid::view_redraw);
	}

	void reset(const recti bounds)
	{
		_bounds = bounds;
	}

	ui::style::cursor cursor() const override
	{
		return ui::style::cursor::link;
	}

	void on_mouse_left_button_down(const pointi loc, const ui::key_state keys) override
	{
		_hover = _bounds.contains(loc);
		_tracking = true;
		_parent->set_style_bit(view_element_style::tracking, true);
		_parent->set_style_bit(view_element_style::hover, _hover);
		on_mouse_move(loc);
	}

	void on_mouse_move(const pointi loc) override
	{
		_hover = _bounds.contains(loc);
		_parent->set_style_bit(view_element_style::hover, _hover);

		if (_tracking)
		{
			const auto pos = std::clamp(loc.x - _scrubber_bounds.left, 0, _scrubber_bounds.width());
			_parent->_display->_compare_video_pos = pos;
			_parent->_display->load_compare_preview(pos, _scrubber_bounds.width());
			_parent->_display->_async.invalidate_view(view_invalid::view_redraw);
		}
	}

	void on_mouse_left_button_up(const pointi loc, const ui::key_state keys) override
	{
		_hover = _bounds.contains(loc);
		_tracking = false;
		_parent->set_style_bit(view_element_style::tracking, false);
		_parent->set_style_bit(view_element_style::hover, _hover);
	}
};

class zoom_controller final : public view_controller
{
public:
	std::shared_ptr<photo_control> _parent_element;

	view_state& _state;
	recti _view_bounds;
	recti _zoom_bounds;
	bool _tracking = false;
	bool _region_select = false;
	bool _inspect_active = false;
	df::zoom_view_state _start_zoom_state;
	bool _committed = false;

	zoom_controller(const view_host_ptr& host, std::shared_ptr<photo_control> media_parent, view_state& state,
	                const recti interaction_bounds, const recti view_bounds) :
		view_controller(host, interaction_bounds),
		_parent_element(std::move(media_parent)),
		_state(state),
		_view_bounds(view_bounds),
		_start_zoom_state(_parent_element->_display->zoom_state())
	{
		_zoom_bounds = calc_zoom_bounds();
	}

	~zoom_controller() override
	{
		if (_tracking && !_committed) _parent_element->_display->restore_zoom_state(_start_zoom_state);
		_tracking = false;
	}

	recti calc_zoom_bounds() const
	{
		const auto media_bounds = _parent_element->bounds;
		const auto zoom_dims = std::clamp(std::min(media_bounds.width(), media_bounds.height()), 80, 200);
		const auto source_orientation = _parent_element->_display->_selected_texture1->display_orientation();
		const auto source_dims = _parent_element->_display->_selected_texture1->display_dimensions();
		const auto result_dims = ui::scale_dimensions(flips_xy(source_orientation) ? source_dims.flip() : source_dims,
		                                              zoom_dims);
		return recti(_view_bounds.top_left(), result_dims).clamp(_view_bounds);
	}

	ui::style::cursor cursor() const override
	{
		if (_region_select) return ui::style::cursor::select;
		return _inspect_active ? ui::style::cursor::size_all : ui::style::cursor::zoom;
	}

	void draw(ui::draw_context& dc) override
	{
		if (_region_select && _tracking)
		{
			const auto selection = recti(_start_loc, _last_loc).normalise().crop(_view_bounds);
			dc.draw_rect(selection, ui::color(ui::style::color::dialog_selected_background, 0.5));
		}
	}

	void on_mouse_left_button_down(const pointi loc, const ui::key_state keys) override
	{
		_tracking = true;
		_last_loc = _start_loc = loc;
		_region_select = keys.control;
		_inspect_active = false;
		_committed = false;
		if (_region_select) return;
		const auto local = loc - _view_bounds.top_left();
		_parent_element->_display->inspect_at_100(pointd(local));
		_inspect_active = true;
	}

	void on_mouse_move(const pointi loc) override
	{
		if (_tracking)
		{
			_last_loc = loc;
			if (_region_select) return;
			const auto local = loc - _view_bounds.top_left();
			const auto extent = _view_bounds.extent();
			_parent_element->_display->zoom_center({
				std::clamp(local.x / static_cast<double>(std::max(1, extent.cx)), 0.0, 1.0),
				std::clamp(local.y / static_cast<double>(std::max(1, extent.cy)), 0.0, 1.0)
			});
		}
	}

	void on_mouse_left_button_up(const pointi loc, const ui::key_state keys) override
	{
		_tracking = false;
		if (_region_select)
		{
			const auto selection = recti(_start_loc, loc).normalise().crop(_view_bounds);
			_region_select = false;
			if (selection.width() >= 8 && selection.height() >= 8)
			{
				const auto local = selection.offset(-_view_bounds.top_left());
				_parent_element->_display->zoom_region(rectd(local));
			}
			return;
		}
		if (_committed)
		{
			_committed = false;
		}
		else
		{
			_parent_element->_display->restore_zoom_state(_start_zoom_state);
		}
		_inspect_active = false;
	}

	bool key_down(const char32_t key, const ui::key_state keys) override
	{
		if (_tracking && _inspect_active && key == keys::SPACE)
		{
			_parent_element->_display->commit_inspect();
			_committed = true;
			return true;
		}
		return false;
	}

	bool escape() override
	{
		if (!_tracking && !_region_select && !_inspect_active) return false;
		_parent_element->_display->restore_zoom_state(_start_zoom_state);
		_region_select = false;
		_tracking = false;
		_inspect_active = false;
		return true;
	}
};

static void render_zoom_overlay(ui::draw_context& dc, const display_state_ptr& display,
                                const texture_state_ptr& texture_state, const recti bounds, const pointi element_offset,
                                recti& navigator_bounds, recti& fit_bounds, recti& out_bounds, recti& actual_bounds,
                                recti& in_bounds, recti& options_bounds)
{
	navigator_bounds.clear();
	fit_bounds.clear();
	out_bounds.clear();
	actual_bounds.clear();
	in_bounds.clear();
	options_bounds.clear();
	try
	{
		if (setting.zoom_navigator == zoom_navigator_mode::off) return;
		if (!texture_state) return;

		const auto client_bounds = bounds.offset(element_offset);
		const auto inspect = display->is_temporary_zoom();
		const auto text = !inspect && texture_state->is_provisional()
			                  ? std::format("{}% - Preview", display->zoom_scale_percent())
			                  : std::format("{}%", display->zoom_scale_percent());
		const auto text_extent = dc.measure_text(text, ui::style::font_face::dialog,
		                                         ui::style::text_style::single_line, 100);
		const auto label_padding = dc.padding1;
		const auto label_height = text_extent.cy + label_padding * 2;
		const auto available_height = std::max(1, client_bounds.height() - label_height);
		const auto navigator_limit = df::round(df::zoom_view_state::navigator_dip * dc.scale_factor);
		const auto zoom_dim = std::max(32, std::min({navigator_limit, client_bounds.width(), available_height}));
		const auto source_orientation = texture_state->display_orientation();
		const auto source_dims = texture_state->display_dimensions();
		const auto zoom_dims = ui::scale_dimensions(flips_xy(source_orientation) ? source_dims.flip() : source_dims,
		                                            zoom_dim);
		const auto panel_width = inspect ? zoom_dims.cx : zoom_dim;
		const auto panel_bounds = recti(client_bounds.left, client_bounds.top + label_height,
		                                client_bounds.left + panel_width,
		                                client_bounds.top + label_height + zoom_dims.cy)
			.crop(client_bounds);
		const auto zoom_left = panel_bounds.left + (panel_bounds.width() - zoom_dims.cx) / 2;
		const auto zoom_bounds = recti({zoom_left, panel_bounds.top}, zoom_dims).crop(panel_bounds);
		navigator_bounds = zoom_bounds;
		const auto media_bounds = texture_state->display_bounds().offset(element_offset);
		if (media_bounds.is_empty()) return;

		const auto l = df::mul_div(client_bounds.left - media_bounds.left, zoom_bounds.width(), media_bounds.width()) +
			zoom_bounds.left;
		const auto t = df::mul_div(client_bounds.top - media_bounds.top, zoom_bounds.height(), media_bounds.height()) +
			zoom_bounds.top;
		const auto r = df::mul_div(client_bounds.right - media_bounds.left, zoom_bounds.width(), media_bounds.width()) +
			zoom_bounds.left;
		const auto b = df::mul_div(client_bounds.bottom - media_bounds.top, zoom_bounds.height(),
		                           media_bounds.height()) +
			zoom_bounds.top;
		const auto shown_bounds = recti(l, t, r, b).crop(zoom_bounds);
		const auto zoom_texture = texture_state->zoom_texture(dc, df::zoom_view_state::navigator_surface_extent);
		if (!zoom_texture) return;

		const auto alpha = std::min(dc.colors.overlay_alpha, display->_zoom_overlay_alpha);
		const auto thumb_alpha = alpha;
		const auto control_bounds = panel_bounds;
		const auto label_width = inspect
			                         ? std::min(panel_bounds.width(), text_extent.cx + label_padding * 2)
			                         : panel_bounds.width();
		const auto label_bounds = recti(panel_bounds.left, client_bounds.top, panel_bounds.left + label_width,
		                                client_bounds.top + label_height).crop(client_bounds);
		const auto button_width = std::min(label_height, label_bounds.width() / 5);
		if (!inspect)
		{
			fit_bounds = recti(label_bounds.left, label_bounds.top,
			                   std::min(label_bounds.right, label_bounds.left + button_width), label_bounds.bottom);
			out_bounds = recti(fit_bounds.right, label_bounds.top,
			                   std::min(label_bounds.right, fit_bounds.right + button_width), label_bounds.bottom);
			options_bounds = recti(std::max(label_bounds.left, label_bounds.right - button_width), label_bounds.top,
			                       label_bounds.right, label_bounds.bottom);
			in_bounds = recti(std::max(label_bounds.left, options_bounds.left - button_width), label_bounds.top,
			                  options_bounds.left, label_bounds.bottom);
			actual_bounds = recti(out_bounds.right, label_bounds.top, in_bounds.left, label_bounds.bottom);
		}
		const auto excluded = ui::color(0, thumb_alpha / 2.0f);
		const auto sampler = calc_sampler(zoom_bounds.extent(), zoom_texture->dimensions(),
		                                  zoom_texture->_orientation);
		const auto dst_quad = setting.show_rotated
			                      ? quadd(zoom_bounds).transform(to_simple_transform(zoom_texture->_orientation))
			                      : quadd(zoom_bounds);

		dc.draw_rect(label_bounds, ui::color(0, alpha * 0.5f));
		const auto foreground = ui::color(dc.colors.foreground, alpha);
		if (inspect)
		{
			dc.draw_text(text, label_bounds, ui::style::font_face::dialog,
			             ui::style::text_style::single_line_center, foreground, {});
		}
		else
		{
			dc.draw_text(icon_to_utf8(icon_index::fit), fit_bounds, ui::style::font_face::icons,
			             ui::style::text_style::single_line_center, foreground, {});
			dc.draw_text(icon_to_utf8(icon_index::zoom_out), out_bounds, ui::style::font_face::icons,
			             ui::style::text_style::single_line_center, foreground, {});
			dc.draw_text(text, actual_bounds, ui::style::font_face::dialog,
			             ui::style::text_style::single_line_center, foreground, {});
			dc.draw_text(icon_to_utf8(icon_index::zoom_in), in_bounds, ui::style::font_face::icons,
			             ui::style::text_style::single_line_center, foreground, {});
			dc.draw_text(icon_to_utf8(icon_index::more), options_bounds, ui::style::font_face::icons,
			             ui::style::text_style::single_line_center, foreground, {});
		}

		if (thumb_alpha > 0.0f)
		{
			dc.draw_rect(control_bounds, ui::color(ui::style::color::group_background, thumb_alpha));
			dc.draw_texture(zoom_texture, dst_quad, zoom_texture->dimensions(), thumb_alpha, sampler);
			dc.draw_border(shown_bounds, zoom_bounds, excluded, excluded);
		}
	}
	catch (const std::exception& e)
	{
		df::log(__FUNCTION__, e.what());
	}
}

void photo_control::render_zoom_thumb(ui::draw_context& dc, const pointi element_offset) const
{
	render_zoom_overlay(dc, _display, _display->_selected_texture1, bounds, element_offset,
	                    _zoom_navigator_bounds, _zoom_fit_bounds, _zoom_out_bounds, _zoom_100_bounds,
	                    _zoom_in_bounds, _zoom_options_bounds);
}

void side_by_side_control::render_zoom_thumb(ui::draw_context& dc, const pointi element_offset) const
{
	const auto texture = _display->active_zoom_pane() == df::zoom_pane::primary
		                     ? _display->_selected_texture1
		                     : _display->_selected_texture2;
	render_zoom_overlay(dc, _display, texture, bounds, element_offset, _zoom_navigator_bounds,
	                    _zoom_fit_bounds, _zoom_out_bounds, _zoom_100_bounds, _zoom_in_bounds,
	                    _zoom_options_bounds);
}

class zoom_command_controller final : public view_controller
{
	commands _command;
	bool _tracking = false;

public:
	zoom_command_controller(const view_host_ptr& host, const recti bounds, const commands command) :
		view_controller(host, bounds), _command(command)
	{
	}

	ui::style::cursor cursor() const override
	{
		return ui::style::cursor::link;
	}

	void on_mouse_left_button_down(const pointi loc, const ui::key_state keys) override
	{
		_tracking = true;
	}

	void on_mouse_left_button_up(const pointi loc, const ui::key_state keys) override
	{
		const auto invoke = _tracking && _bounds.contains(loc);
		_tracking = false;
		if (invoke) _host->invoke(_command);
	}

	bool escape() override
	{
		if (!_tracking) return false;
		_tracking = false;
		return true;
	}
};

class zoom_options_controller final : public view_controller
{
	view_state& _state;
	bool _tracking = false;

public:
	zoom_options_controller(const view_host_ptr& host, view_state& state, const recti bounds) :
		view_controller(host, bounds), _state(state)
	{
	}

	ui::style::cursor cursor() const override
	{
		return ui::style::cursor::link;
	}

	void on_mouse_left_button_down(const pointi loc, const ui::key_state keys) override
	{
		_tracking = true;
	}

	void on_mouse_left_button_up(const pointi loc, const ui::key_state keys) override
	{
		const auto show = _tracking && _bounds.contains(loc);
		_tracking = false;
		if (!show) return;
		const auto command = _state.find_command(commands::menu_zoom);
		const auto menu = command && command->menu ? command->menu() : std::vector<ui::command_ptr>{};
		if (!menu.empty()) _host->track_menu(_bounds, menu);
	}

	bool escape() override
	{
		if (!_tracking) return false;
		_tracking = false;
		return true;
	}
};

class zoom_navigator_controller final : public view_controller
{
	display_state_ptr _display;
	bool _tracking = false;

public:
	zoom_navigator_controller(const view_host_ptr& host, display_state_ptr display, const recti bounds) :
		view_controller(host, bounds), _display(std::move(display))
	{
	}

	ui::style::cursor cursor() const override
	{
		return ui::style::cursor::size_all;
	}

	void on_mouse_left_button_down(const pointi loc, const ui::key_state keys) override
	{
		_tracking = true;
		move_to(loc);
	}

	void on_mouse_move(const pointi loc) override
	{
		if (_tracking) move_to(loc);
	}

	void on_mouse_left_button_up(const pointi loc, const ui::key_state keys) override
	{
		if (_tracking) move_to(loc);
		_tracking = false;
	}

	bool escape() override
	{
		if (!_tracking) return false;
		_tracking = false;
		return true;
	}

	void move_to(const pointi loc) const
	{
		const auto local = pointd(loc - _bounds.top_left());
		_display->zoom_center(df::zoom_view_state::navigator_center(local, sized(_bounds.extent())));
	}
};

class comparison_zoom_controller final : public view_controller
{
	display_state_ptr _display;
	df::zoom_pane _pane;
	// Hit bounds are clipped away from the overlays; the pane frame stays whole so zoom positions map correctly.
	recti _view_bounds;
	df::zoom_view_state _start_zoom_state;
	bool _tracking = false;
	bool _region_select = false;
	bool _inspect_active = false;
	bool _pan_active = false;
	bool _committed = false;

public:
	comparison_zoom_controller(const view_host_ptr& host, display_state_ptr display, const df::zoom_pane pane,
	                           const recti interaction_bounds, const recti view_bounds) :
		view_controller(host, interaction_bounds), _display(std::move(display)), _pane(pane),
		_view_bounds(view_bounds)
	{
		_display->active_zoom_pane(_pane);
		_start_zoom_state = _display->zoom_state();
	}

	~comparison_zoom_controller() override
	{
		if (_tracking && !_committed) _display->restore_zoom_state(_start_zoom_state);
	}

	ui::style::cursor cursor() const override
	{
		if (_region_select) return ui::style::cursor::select;
		return _display->zoom() || _inspect_active ? ui::style::cursor::size_all : ui::style::cursor::zoom;
	}

	void draw(ui::draw_context& dc) override
	{
		if (_region_select && _tracking)
		{
			const auto selection = recti(_start_loc, _last_loc).normalise().crop(_view_bounds);
			dc.draw_rect(selection, ui::color(ui::style::color::dialog_selected_background, 0.5));
		}
	}

	void on_mouse_left_button_down(const pointi loc, const ui::key_state keys) override
	{
		_display->active_zoom_pane(_pane);
		_start_zoom_state = _display->zoom_state();
		_tracking = true;
		_last_loc = _start_loc = loc;
		_region_select = keys.control;
		_inspect_active = false;
		_pan_active = false;
		_committed = false;
		if (_region_select) return;
		if (_display->zoom())
		{
			_pan_active = true;
		}
		else
		{
			_display->inspect_at_100(pointd(loc - _view_bounds.top_left()));
			_inspect_active = true;
		}
	}

	void on_mouse_move(const pointi loc) override
	{
		if (!_tracking) return;
		_last_loc = loc;
		if (_region_select) return;
		if (_pan_active)
		{
			const auto delta = pointd(loc - _start_loc);
			const auto ramp = 120.0 * _host->owner()->scale_factor();
			_display->pan_zoom(df::zoom_view_state::accelerate_pan(delta, ramp), _start_zoom_state);
		}
		else if (_inspect_active)
		{
			const auto local = loc - _view_bounds.top_left();
			const auto extent = _view_bounds.extent();
			_display->zoom_center({
				std::clamp(local.x / static_cast<double>(std::max(1, extent.cx)), 0.0, 1.0),
				std::clamp(local.y / static_cast<double>(std::max(1, extent.cy)), 0.0, 1.0)
			});
		}
	}

	void on_mouse_left_button_up(const pointi loc, const ui::key_state keys) override
	{
		_tracking = false;
		if (_region_select)
		{
			const auto selection = recti(_start_loc, loc).normalise().crop(_view_bounds);
			_region_select = false;
			if (selection.width() >= 8 && selection.height() >= 8)
				_display->zoom_region(rectd(selection.offset(-_view_bounds.top_left())));
		}
		else if (_inspect_active && !_committed)
		{
			_display->restore_zoom_state(_start_zoom_state);
		}
		_inspect_active = false;
		_pan_active = false;
		_committed = false;
	}

	bool key_down(const char32_t key, const ui::key_state keys) override
	{
		if (_tracking && _inspect_active && key == keys::SPACE)
		{
			_display->commit_inspect();
			_committed = true;
			return true;
		}
		return false;
	}

	bool escape() override
	{
		if (!_tracking && !_region_select && !_inspect_active && !_pan_active) return false;
		if (_tracking) _display->restore_zoom_state(_start_zoom_state);
		_tracking = false;
		_region_select = false;
		_inspect_active = false;
		_pan_active = false;
		return true;
	}
};

class comparison_pane_controller final : public view_controller
{
	display_state_ptr _display;
	df::zoom_pane _pane;
	bool _tracking = false;

public:
	comparison_pane_controller(const view_host_ptr& host, display_state_ptr display, const df::zoom_pane pane,
	                           const recti bounds) :
		view_controller(host, bounds), _display(std::move(display)), _pane(pane)
	{
	}

	ui::style::cursor cursor() const override { return ui::style::cursor::link; }

	void on_mouse_left_button_down(const pointi loc, const ui::key_state keys) override { _tracking = true; }

	void on_mouse_left_button_up(const pointi loc, const ui::key_state keys) override
	{
		const auto invoke = _tracking && _bounds.contains(loc);
		_tracking = false;
		if (invoke) _display->active_zoom_pane(_pane);
	}

	bool escape() override
	{
		if (!_tracking) return false;
		_tracking = false;
		return true;
	}
};

class pan_controller final : public view_controller, public std::enable_shared_from_this<pan_controller>
{
public:
	std::shared_ptr<photo_control> _parent;
	view_state& _state;
	df::zoom_view_state _start_zoom_state;
	bool _tracking = false;
	bool _region_select = false;
	bool _auto_pan = false;
	pointd _auto_velocity;
	pointd _auto_offset;
	double _auto_last_time = 0.0;

	pan_controller(const view_host_ptr& host, std::shared_ptr<photo_control> parent, view_state& s,
	               const recti bounds) : view_controller(host, bounds), _parent(std::move(parent)), _state(s)
	{
	}

	~pan_controller() override
	{
		ui::animations.erase(this);
		if (_tracking && !_region_select) _parent->_display->restore_zoom_state(_start_zoom_state);
	}

	ui::style::cursor cursor() const override
	{
		if (_region_select) return ui::style::cursor::select;
		return ui::style::cursor::size_all;
	}

	void draw(ui::draw_context& rc) override
	{
		if (_region_select && _tracking)
		{
			const auto selection = recti(_start_loc, _last_loc).normalise().crop(_bounds);
			rc.draw_rect(selection, ui::color(ui::style::color::dialog_selected_background, 0.5));
		}
	}

	void on_mouse_left_button_down(const pointi loc, const ui::key_state keys) override
	{
		if (_auto_pan)
		{
			stop_auto_pan();
			return;
		}
		_start_zoom_state = _parent->_display->zoom_state();
		_last_loc = _start_loc = loc;
		_tracking = true;
		_region_select = keys.control;
		if (_region_select) return;
		scroll_to(loc);
	}

	void on_mouse_move(const pointi loc) override
	{
		if (_auto_pan)
		{
			_auto_velocity = df::zoom_view_state::auto_pan_velocity(pointd(loc - _start_loc),
			                                                        12.0 * _host->owner()->scale_factor());
			return;
		}
		if (_tracking)
		{
			if (_region_select)
			{
				_last_loc = loc;
				return;
			}
			scroll_to(loc);
		}
	}

	void on_mouse_left_button_up(const pointi loc, const ui::key_state keys) override
	{
		_tracking = false;
		if (_region_select)
		{
			const auto selection = recti(_start_loc, loc).normalise().crop(_bounds);
			_region_select = false;
			if (selection.width() >= 8 && selection.height() >= 8)
				_parent->_display->zoom_region(rectd(selection.offset(-_bounds.top_left())));
		}
	}

	void on_mouse_middle_button_down(const pointi loc, const ui::key_state keys) override
	{
		if (_auto_pan)
		{
			stop_auto_pan();
			return;
		}
		_start_zoom_state = _parent->_display->zoom_state();
		_start_loc = loc;
		_auto_velocity = {};
		_auto_offset = {};
		_auto_last_time = df::now();
		_auto_pan = true;
		const auto weak = weak_from_this();
		ui::animations[this] = [weak]
		{
			const auto controller = weak.lock();
			if (!controller || !controller->_auto_pan) return false;
			const auto now = df::now();
			const auto elapsed = std::clamp(now - controller->_auto_last_time, 0.0, 0.05);
			controller->_auto_last_time = now;
			controller->_auto_offset = controller->_auto_offset + controller->_auto_velocity * elapsed;
			controller->_parent->_display->pan_zoom(controller->_auto_offset, controller->_start_zoom_state);
			controller->_host->invalidate_view(view_invalid::view_redraw);
			return true;
		};
		_host->invalidate_view(view_invalid::animations);
	}

	bool escape() override
	{
		if (_auto_pan)
		{
			stop_auto_pan();
			return true;
		}
		if (_tracking)
		{
			_tracking = false;
			if (!_region_select) _parent->_display->restore_zoom_state(_start_zoom_state);
			_region_select = false;
			return true;
		}
		return false;
	}

	void stop_auto_pan()
	{
		_auto_pan = false;
		_auto_velocity = {};
		ui::animations.erase(this);
		_host->invalidate_view(view_invalid::controller | view_invalid::view_redraw);
	}

	void scroll_to(const pointi loc)
	{
		if (_last_loc != loc)
		{
			const auto delta = pointd(loc - _start_loc);
			const auto ramp = 120.0 * _host->owner()->scale_factor();
			const auto offset = df::zoom_view_state::accelerate_pan(delta, ramp);
			_parent->_display->pan_zoom(pointd(offset), _start_zoom_state);
			_last_loc = loc;
		}
	}
};

view_controller_ptr view_elements::controller_from_location(const view_host_ptr& host, const pointi loc,
                                                            const pointi element_offset,
                                                            const std::vector<recti>& excluded_bounds)
{
	view_controller_ptr result;

	if (bounds.contains(loc - element_offset))
	{
		for (const auto& c : _children)
		{
			if (!c->is_visible()) continue;
			result = c->controller_from_location(host, loc, element_offset, excluded_bounds);
			if (result) break;
		}
	}

	return result;
}


view_controller_ptr photo_control::controller_from_location(const view_host_ptr& host, const pointi loc,
                                                            const pointi element_offset,
                                                            const std::vector<recti>& excluded_bounds)
{
	view_controller_ptr controller;
	const auto logical_loc = loc + element_offset;
	const auto image_bounds = _display->_selected_texture1
		                          ? _display->_selected_texture1->display_bounds().offset(element_offset)
		                          : recti{};
	const auto durable_zoom = _display->zoom() && !_display->is_temporary_zoom();
	if (durable_zoom && _zoom_grading_element && !_zoom_grading_element->bounds.is_empty() &&
		_zoom_grading_element->bounds.offset(element_offset).contains(loc))
	{
		return _zoom_grading_element->controller_from_location(host, loc, element_offset, {});
	}

	if (durable_zoom)
	{
		if (!_zoom_options_bounds.is_empty() && _zoom_options_bounds.contains(loc))
		{
			return std::make_shared<zoom_options_controller>(host, _state, _zoom_options_bounds);
		}
		const std::array zoom_commands{
			std::pair{_zoom_fit_bounds, commands::view_zoom_fit},
			std::pair{_zoom_out_bounds, commands::view_zoom_out},
			std::pair{_zoom_100_bounds, commands::view_zoom_100},
			std::pair{_zoom_in_bounds, commands::view_zoom_in}
		};
		for (const auto& [command_bounds, command] : zoom_commands)
		{
			if (!command_bounds.is_empty() && command_bounds.contains(loc))
			{
				return std::make_shared<zoom_command_controller>(host, command_bounds, command);
			}
		}
	}

	if (durable_zoom && !_zoom_navigator_bounds.is_empty() &&
		_zoom_navigator_bounds.contains(loc))
	{
		return std::make_shared<zoom_navigator_controller>(host, _display, _zoom_navigator_bounds);
	}

	if (!_display->zoom() && !image_bounds.is_empty() && image_bounds.contains(loc) &&
		_display->can_zoom())
	{
		auto interaction_bounds = image_bounds;
		for (const auto& excluded_logical : excluded_bounds)
		{
			if (excluded_logical.is_empty()) continue;
			const auto excluded = excluded_logical.offset(element_offset);
			if (excluded.contains(loc)) return nullptr;
			if (!interaction_bounds.intersects(excluded)) continue;

			if (excluded.right < loc.x) interaction_bounds.left = std::max(interaction_bounds.left, excluded.right + 1);
			if (excluded.left > loc.x) interaction_bounds.right = std::min(interaction_bounds.right, excluded.left - 1);
			if (excluded.bottom < loc.y) interaction_bounds.top = std::max(interaction_bounds.top, excluded.bottom + 1);
			if (excluded.top > loc.y) interaction_bounds.bottom = std::min(interaction_bounds.bottom, excluded.top - 1);
		}
		const auto view_bounds = bounds.offset(element_offset);
		return std::make_shared<zoom_controller>(host, shared_from_this(), _state, interaction_bounds, view_bounds);
	}

	if (bounds.contains(logical_loc) && _can_pan && _display->zoom())
	{
		auto interaction_bounds = bounds.offset(element_offset);
		if (durable_zoom)
		{
			const std::array tool_bounds{
				_zoom_fit_bounds,
				_zoom_out_bounds,
				_zoom_100_bounds,
				_zoom_in_bounds,
				_zoom_options_bounds,
				_zoom_navigator_bounds,
				_zoom_grading_element ? _zoom_grading_element->bounds.offset(element_offset) : recti{}
			};
			for (const auto tool_bounds_item : tool_bounds)
			{
				if (!tool_bounds_item.is_empty()) interaction_bounds.exclude(loc, tool_bounds_item);
			}
		}
		for (const auto excluded_logical : excluded_bounds)
		{
			if (!excluded_logical.is_empty()) interaction_bounds.exclude(loc, excluded_logical.offset(element_offset));
		}
		controller = std::make_shared<pan_controller>(host, shared_from_this(), _state, interaction_bounds);
	}

	return controller;
}

view_controller_ptr view_element::controller_from_location(const view_host_ptr& host, const pointi loc,
                                                           const pointi element_offset,
                                                           const std::vector<recti>& excluded_bounds)
{
	return nullptr;
}


static ui::color calc_clr(const view_element_style& style)
{
	auto result_clr = ui::color();

	const auto tracking = style && view_element_style::tracking;
	const auto hover = style && view_element_style::hover;
	const auto selected = style && view_element_style::selected;
	const auto highlight = style && view_element_style::highlight;

	if (tracking || hover || selected || highlight)
	{
		result_clr = view_handle_color(selected || highlight, hover, tracking, true, true);
		result_clr.a = 1.0f;
	}
	else if (style && view_element_style::checked)
	{
		result_clr.a = 0.22f;
	}
	else if (style && view_element_style::important)
	{
		result_clr = ui::color(ui::style::color::important_background);
		result_clr.a = 1.0f;
	}
	else if (style && view_element_style::info)
	{
		result_clr = ui::color(ui::style::color::info_background);
		result_clr.a = 1.0f;
	}
	else if (style && view_element_style::dark_background)
	{
		result_clr.a = 0.777f;
	}
	else if (style && view_element_style::shaded_background)
	{
		result_clr.a = 0.44f;
	}
	else if (style && view_element_style::background)
	{
		result_clr.a = 0.222f;
	}

	return result_clr;
}

void view_element::update_background_color()
{
	_bg_target = _bg_color = calc_clr(style);
}

void view_element::set_style_bit(const view_element_style mask, const bool state)
{
	auto new_style = style;

	if (state)
	{
		new_style |= mask;
	}
	else
	{
		new_style &= ~mask;
	}

	if (new_style != style)
	{
		style = new_style;
		update_background_color();
	}
}


void view_element::set_style_bit(const view_element_style mask, const bool state, const view_host_base_ptr& view,
                                 const view_element_ptr& e)
{
	auto new_style = style;

	if (state)
	{
		new_style |= mask;
	}
	else
	{
		new_style &= ~mask;
	}

	if (new_style != style)
	{
		style = new_style;
		const auto bg = calc_clr(style);

		if (view)
		{
			if (_bg_target != bg && e)
			{
				if (ui::is_alpha_zero(_bg_color.a))
				{
					_bg_color.r = bg.r;
					_bg_color.g = bg.g;
					_bg_color.b = bg.b;
					_bg_target = bg;
				}
				else if (ui::is_alpha_zero(bg.a))
				{
					_bg_target.a = 0.0f;
				}
				else
				{
					_bg_target = bg;
				}

				ui::animations[this] = [view, e]
				{
					const auto dd = e->_bg_target - e->_bg_color;
					bool invalidate = false;

					if (dd.abs_sum() > ui::color::color_epsilon)
					{
						e->_bg_color += dd * 0.2345f;
						view->invalidate_element(e);
						invalidate = true;
					}

					return invalidate;
				};

				view->invalidate_view(view_invalid::animations);
			}
		}
		else
		{
			_bg_target = _bg_color = calc_clr(style);
		}
	}
}

view_controller_ptr video_control::controller_from_location(const view_host_ptr& host, const pointi loc,
                                                            const pointi element_offset,
                                                            const std::vector<recti>& excluded_bounds)
{
	return default_controller_from_location(*this, host, loc, element_offset, excluded_bounds);
}

view_controller_ptr audio_control::controller_from_location(const view_host_ptr& host, const pointi loc,
                                                            const pointi element_offset,
                                                            const std::vector<recti>& excluded_bounds)
{
	return default_controller_from_location(*this, host, loc, element_offset, excluded_bounds);
}

view_controller_ptr side_by_side_control::controller_from_location(const view_host_ptr& host, const pointi loc,
                                                                   const pointi element_offset,
                                                                   const std::vector<recti>& excluded_bounds)
{
	const auto logical_loc = loc + element_offset;
	const auto durable_zoom = _display->zoom() && !_display->is_temporary_zoom();
	if (durable_zoom)
	{
		const auto index = _display->active_zoom_pane() == df::zoom_pane::primary ? 0u : 1u;
		const auto& grading = _zoom_grading_elements[index];
		if (grading && !grading->bounds.is_empty() && grading->bounds.offset(element_offset).contains(loc))
		{
			return grading->controller_from_location(host, loc, element_offset, {});
		}
		if (!_zoom_options_bounds.is_empty() && _zoom_options_bounds.contains(loc))
		{
			return std::make_shared<zoom_options_controller>(host, _state, _zoom_options_bounds);
		}
		const std::array zoom_commands{
			std::pair{_zoom_fit_bounds, commands::view_zoom_fit},
			std::pair{_zoom_out_bounds, commands::view_zoom_out},
			std::pair{_zoom_100_bounds, commands::view_zoom_100},
			std::pair{_zoom_in_bounds, commands::view_zoom_in}
		};
		for (const auto& [command_bounds, command] : zoom_commands)
		{
			if (!command_bounds.is_empty() && command_bounds.contains(loc))
			{
				return std::make_shared<zoom_command_controller>(host, command_bounds, command);
			}
		}
		if (!_zoom_navigator_bounds.is_empty() && _zoom_navigator_bounds.contains(loc))
		{
			return std::make_shared<zoom_navigator_controller>(host, _display, _zoom_navigator_bounds);
		}
	}

	const auto video_control_bounds = _display->_compare_video_control_bounds.offset(element_offset);
	if (_display->_is_compare_video && video_control_bounds.contains(loc))
	{
		return std::make_shared<preview_controller>(host, shared_from_this(), video_control_bounds,
		                                            _display->_compare_video_scrubber_bounds.offset(element_offset));
	}

	for (auto i = 0u; i < _display->_pane_marker_bounds.size(); ++i)
	{
		const auto marker = _display->_pane_marker_bounds[i];
		if (marker.is_empty() || !marker.contains(loc)) continue;

		// Magnified there is only one marker and it names the pane you are already in, so it flips.
		const auto pane = _display->is_zoom_mode()
			                  ? df::comparison_zoom_state::other(_display->active_zoom_pane())
			                  : i == 0
			                  ? df::zoom_pane::primary
			                  : df::zoom_pane::secondary;
		return std::make_shared<comparison_pane_controller>(host, _display, pane, marker);
	}

	if (!_display->is_zoom_mode() && _display->_can_compare && _display->_compare_bounds.contains(logical_loc))
	{
		return std::make_shared<compare_controller>(host, shared_from_this(), _display->_compare_bounds);
	}

	if (!_display->_comparing && _display->can_zoom())
	{
		auto pane_bounds = bounds.offset(element_offset);
		const auto zoomed = _display->is_zoom_mode();
		const auto pane = zoomed
			                  ? _display->active_zoom_pane()
			                  : loc.x < pane_bounds.center().x
			                  ? df::zoom_pane::primary
			                  : df::zoom_pane::secondary;
		if (!zoomed)
		{
			if (pane == df::zoom_pane::primary) pane_bounds.right = pane_bounds.center().x;
			else pane_bounds.left = pane_bounds.center().x;
		}
		if (pane_bounds.contains(loc))
		{
			// The pane covers every overlay tested above and view_host will not rebuild the controller
			// while the pointer stays inside its bounds, so the hit bounds are clipped away from them.
			auto interaction_bounds = pane_bounds;

			for (const auto& marker : _display->_pane_marker_bounds)
			{
				if (!marker.is_empty()) interaction_bounds.exclude(loc, marker);
			}

			if (durable_zoom)
			{
				const auto index = _display->active_zoom_pane() == df::zoom_pane::primary ? 0u : 1u;
				const auto& grading = _zoom_grading_elements[index];
				const std::array tool_bounds{
					_zoom_fit_bounds,
					_zoom_out_bounds,
					_zoom_100_bounds,
					_zoom_in_bounds,
					_zoom_options_bounds,
					_zoom_navigator_bounds,
					grading && !grading->bounds.is_empty() ? grading->bounds.offset(element_offset) : recti{}
				};

				for (const auto tool : tool_bounds)
				{
					if (!tool.is_empty()) interaction_bounds.exclude(loc, tool);
				}
			}

			if (_display->_is_compare_video && !video_control_bounds.is_empty())
			{
				interaction_bounds.exclude(loc, video_control_bounds);
			}

			return std::make_shared<comparison_zoom_controller>(host, _display, pane, interaction_bounds, pane_bounds);
		}
	}

	auto excluded_bounds2 = excluded_bounds;
	excluded_bounds2.emplace_back(_display->_compare_bounds);
	return default_controller_from_location(*this, host, loc, element_offset, excluded_bounds2);
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void link_element::update_check(const view_element_event& event)
{
	const bool is_checked = _cmd != commands::none && event.host->is_command_checked(_cmd);

	if (is_checked != is_style_bit_set(view_element_style::checked))
	{
		set_style_bit(view_element_style::checked, is_checked);
		event.host->invalidate_view(view_invalid::view_redraw);
	}
}

void link_element::dispatch_event(const view_element_event& event)
{
	if (event.type == view_element_event_type::update_command_state)
	{
		update_check(event);
	}
	else if (event.type == view_element_event_type::invoke)
	{
		if (_cmd != commands::none)
		{
			event.host->invoke(_cmd);
			update_check(event);
		}
		else if (_invoke)
		{
			_invoke();
		}
	}
	else
	{
		text_element_base::dispatch_event(event);
	}
}

void link_element::tooltip(view_hover_element& hover, const pointi loc, const pointi element_offset) const
{
	if (_tooltip)
	{
		_tooltip(hover);
	}
	else if (_cmd != commands::none)
	{
		hover.id = _cmd;
	}

	hover.active_bounds = hover.window_bounds = bounds.offset(element_offset);
}

view_controller_ptr link_element::controller_from_location(const view_host_ptr& host, const pointi loc,
                                                           const pointi element_offset,
                                                           const std::vector<recti>& excluded_bounds)
{
	return default_controller_from_location(*this, host, loc, element_offset, excluded_bounds);
}

namespace
{
	static_assert(std::size(rate_label_defs) == rate_label_control::entry_count);

	// Row order matches the sidebar filters and the keyboard keys: Reject, 6, 7, 8, 9, P.
	constexpr commands rate_label_commands[] = {
		commands::rate_rejected,
		commands::label_select,
		commands::label_second,
		commands::label_approved,
		commands::label_review,
		commands::label_to_do,
	};

	static_assert(std::size(rate_label_commands) == rate_label_control::entry_count);

	bool is_entry_set(const df::item_element_ptr& i, const rate_label_def& e)
	{
		return e.key.empty() ? i->rating() == -1 : str::icmp(i->label(), e.key) == 0;
	}

	std::string entry_text(const rate_label_def& e)
	{
		return e.key.empty() ? std::string(tt.command_rate_rejected) : prop::format_label(e.key);
	}
}

void rating_control::tooltip(view_hover_element& hover, const pointi loc, const pointi element_offset) const
{
	hover.elements->add(make_icon_element(icon_index::star, flex_item::no_break));

	const auto current_rating = displayed_rating();
	const auto is_rejected = current_rating == -1;
	const auto has_stars = current_rating >= 1;

	if (_can_edit)
	{
		if (has_stars && current_rating == last_hover_rating)
		{
			hover.elements->add(std::make_shared<text_element>(str_format(tt.rating_remove_fmt.sv(), current_rating)));
		}
		else
		{
			hover.elements->
			      add(std::make_shared<text_element>(format_plural_text(tt.rating_set_fmt, last_hover_rating)));

			if (has_stars)
			{
				hover.elements->add(
					std::make_shared<text_element>(str_format(tt.rating_remove_fmt.sv(), current_rating)));
			}
		}
	}

	const auto table = std::make_shared<ui::table_element>();
	table->add(std::format("{}:", tt.prop_name_label), prop::format_label(_item->label()));
	table->add(std::format("{}:", tt.prop_name_rating), prop::format_rating(current_rating));
	hover.elements->add(table);

	if (_can_edit && is_rejected)
	{
		hover.elements->add(std::make_shared<text_element>(tt.rating_replaces_reject, ui::style::font_face::dialog,
		                                                   ui::style::text_style::multiline, flex_item::line_break));
	}

	auto rating_bounds = bounds.offset(element_offset);
	rating_bounds.right = rating_bounds.left + std::max(1, last_hover_rating) * _icon_cxy;
	rating_bounds.left = rating_bounds.right - _icon_cxy;

	hover.window_bounds = rating_bounds;
	hover.active_bounds = rating_bounds;

	if (_can_edit && show_accelerator)
	{
		hover.elements->add(std::make_shared<action_element>(tt.rating_keys));
	}

	if (!_can_edit)
	{
		const df::item_set items = {{_item}};
		const auto can_process = items.can_process(df::process_items_type::can_save_metadata, false, {});
		hover.elements->add(std::make_shared<text_element>(can_process.to_string(), ui::style::font_face::dialog,
		                                                   ui::style::text_style::multiline,
		                                                   flex_item::line_break));
	}
}

// The bubble names the value the click under the pointer would store, because the batch field shows
// no item state to infer it from.
void rating_edit_control::tooltip(view_hover_element& hover, const pointi loc, const pointi element_offset) const
{
	const auto rating = cell_to_rating(std::max(0, _hover_cell));

	hover.elements->add(make_icon_element(rating < 0 ? rate_label_reject.icon : icon_index::star,
	                                      flex_item::no_break));

	if (rating < 0)
	{
		hover.elements->add(std::make_shared<text_element>(tt.command_rate_rejected));
	}
	else if (_val == rating)
	{
		hover.elements->add(std::make_shared<text_element>(str_format(tt.rating_remove_fmt.sv(), rating)));
	}
	else
	{
		hover.elements->add(std::make_shared<text_element>(format_plural_text(tt.rating_set_fmt, rating)));
	}

	hover.window_bounds = hover.active_bounds = bounds.offset(element_offset);
}

void rate_label_control::render(ui::draw_context& dc, const pointi element_offset) const
{
	const auto i = _item;
	if (!i) return;

	render_background(dc, element_offset);

	const auto view_bounds = bounds.offset(element_offset);
	_view_bounds = view_bounds;
	const auto alpha = _can_edit ? dc.colors.alpha : dc.colors.alpha * 0.25f;

	draw_rate_label_badge(dc, i->label(), i->rating(), view_bounds, alpha);
}

static ui::command_ptr def_menu_command(const icon_index icon, const std::string_view text, const ui::color32 clr,
                                        std::function<void()> invoke, const bool is_checked,
                                        const bool is_enabled = true)
{
	auto c = std::make_shared<ui::command>();
	c->icon = icon;
	c->text = text;
	c->clr = clr;
	c->invoke = std::move(invoke);
	c->checked = is_checked;
	c->enable = is_enabled;
	return c;
}


// The whole grading vocabulary lives in the menu, so the badge stays one icon wide while every mark
// remains one click away with its own colour, glyph, and accelerator.
void rate_label_control::dispatch_event(const view_element_event& event)
{
	if (event.type == view_element_event_type::invoke && _can_edit)
	{
		std::vector<ui::command_ptr> menu;
		menu.reserve(entry_count);

		for (auto n = 0; n < entry_count; ++n)
		{
			const auto& e = rate_label_defs[n];
			const auto is_set = is_entry_set(_item, e);

			auto apply = [self = shared_from_this(), &e, is_set, host = event.host]
			{
				metadata_edits edits;

				if (e.key.empty())
				{
					edits.rating = self->_item->rating() == -1 ? 0 : -1;
				}
				else
				{
					edits.label = is_set ? std::string_view{} : e.key;
				}

				const df::item_elements items_to_modify = {{self->_item}};
				self->_state.modify_items(host->owner(), e.icon, tt.title_updating, items_to_modify, edits, host);
			};

			auto c = def_menu_command(e.icon, entry_text(e), e.clr, std::move(apply), is_set);

			if (show_accelerator)
			{
				const auto command = _state.find_command(rate_label_commands[n]);
				if (command) c->keyboard_accelerator_text = command->keyboard_accelerator_text;
			}

			menu.emplace_back(std::move(c));
		}

		event.host->track_menu(_view_bounds, menu);
	}
}

// The bubble is the only place the grading rules are stated, so it names the current mark, reports
// both values, and warns about the exclusive value a menu choice would replace.
void rate_label_control::tooltip(view_hover_element& hover, const pointi loc, const pointi element_offset) const
{
	const auto current_label = _item->label();
	const auto current_rating = _item->rating();
	const auto* const def = find_rate_label_def(current_label);
	const auto icon = current_rating < 0 ? rate_label_reject.icon : def ? def->icon : icon_index::flag;

	hover.elements->add(make_icon_element(icon, flex_item::no_break));
	hover.elements->add(std::make_shared<text_element>(tt.command_view_rate_label, ui::style::font_face::dialog,
	                                                   ui::style::text_style::multiline,
	                                                   flex_item::line_break));

	const auto table = std::make_shared<ui::table_element>();
	table->add(std::format("{}:", tt.prop_name_label), prop::format_label(current_label));
	table->add(std::format("{}:", tt.prop_name_rating), prop::format_rating(current_rating));

	hover.elements->add(table);

	// Only the exclusive values a menu choice would overwrite are worth stating.
	if (_can_edit)
	{
		if (!current_label.empty())
		{
			hover.elements->add(std::make_shared<text_element>(
				str_format(tt.label_replaces_fmt.sv(), prop::format_label(current_label)),
				ui::style::font_face::dialog, ui::style::text_style::multiline, flex_item::line_break));
		}

		if (current_rating > 0)
		{
			hover.elements->add(std::make_shared<text_element>(tt.label_reject_replaces_rating,
			                                                   ui::style::font_face::dialog,
			                                                   ui::style::text_style::multiline,
			                                                   flex_item::line_break));
		}
	}
	else
	{
		const df::item_set items = {{_item}};
		const auto can_edit = items.can_process(df::process_items_type::can_save_metadata, false, {});
		hover.elements->add(std::make_shared<text_element>(can_edit.to_string(), ui::style::font_face::dialog,
		                                                   ui::style::text_style::multiline,
		                                                   flex_item::line_break));
	}

	hover.active_bounds = hover.window_bounds = bounds.offset(element_offset);
}

void preview_control::render(ui::draw_context& dc, const pointi element_offset) const
{
	const auto view_bounds = bounds.offset(element_offset);
	_view_bounds = view_bounds;
	auto bg = calc_background_color(dc);

	if (_ts->is_preview_rendering())
	{
		bg.merge(ui::color(ui::style::color::important_background, dc.colors.alpha));
	}

	xdraw_icon(dc, icon_index::preview, view_bounds, ui::color(dc.colors.foreground, dc.colors.alpha), bg);
}

void preview_control::dispatch_event(const view_element_event& event)
{
	if (event.type == view_element_event_type::invoke)
	{
		const auto parent = event.host->owner();

		auto show_raw_always = [ts = _ts]
		{
			setting.raw_preview = false;
			ts->load_raw();
		};

		auto show_raw_preview_always = []
		{
			setting.raw_preview = true;
		};

		auto show_raw_this_only = [ts = _ts]
		{
			ts->load_raw();
		};

		const std::vector<ui::command_ptr> result = {
			def_menu_command(icon_index::none, tt.show_raw, 0, show_raw_always, !setting.raw_preview),
			def_menu_command(icon_index::none, tt.preview_show_preview, 0, show_raw_preview_always,
			                 setting.raw_preview),
			def_menu_command(icon_index::none, tt.show_raw_now, 0, show_raw_this_only, false),
		};

		event.host->track_menu(_view_bounds, result);
	}
}

void preview_control::tooltip(view_hover_element& hover, const pointi loc, const pointi element_offset) const
{
	hover.elements->add(make_icon_element(icon_index::preview, flex_item::no_break));

	auto text = tt.preview_rendered;
	if (_ts->is_preview()) text = tt.preview_showing;
	if (_ts->is_preview_rendering()) text = tt.preview_rendering;

	hover.elements->add(std::make_shared<text_element>(text, ui::style::font_face::dialog,
	                                                   ui::style::text_style::multiline,
	                                                   flex_item::line_break));
	hover.active_bounds = hover.window_bounds = bounds.offset(element_offset);
}

void items_dates_control::tooltip(view_hover_element& hover, const pointi loc, const pointi element_offset) const
{
	constexpr auto font = ui::style::font_face::dialog;
	const auto md = _item->metadata();
	const auto file_created_date = _item->file_created();
	const auto file_modified_date = _item->file_modified();
	const auto created_date = _item->media_created();
	const auto st = created_date.date();
	const auto search = df::search_t().day(st.day, st.month, st.year);

	// Counting the day scans every indexed folder, so it is answered by the query worker. Until it
	// answers, the tooltip shows the dates it already holds and leaves the total out.
	const auto matches = _state.day_item_count(created_date);
	const auto e = hover.elements;

	e->add(make_icon_element(search.first_type()->icon, flex_item::no_break));
	e->add(std::make_shared<text_element>(tt.dates_title, ui::style::font_face::title,
	                                      ui::style::text_style::single_line, flex_item::line_break));

	const auto table = std::make_shared<ui::table_element>(flex_item::center);

	// Naming the tag each date came from is what turns "the date is wrong" into a question with a
	// visible answer, which is the whole difficulty of #184.
	if (md)
	{
		const auto add_date_row = [&table, md](const std::string_view label, const prop::date_concept kind)
		{
			const auto d = md->dates.resolve(kind);
			if (!d.is_valid()) return;

			const auto source = prop::date_source_name(md->dates.resolved_source(kind));
			table->add(label, platform::format_date(d),
			           std::format("{} - {}", platform::format_time(d), source));
		};

		add_date_row(tt.prop_name_original.sv(), prop::date_concept::original);
		add_date_row(tt.prop_name_created.sv(), prop::date_concept::created);
		add_date_row(tt.prop_name_modified.sv(), prop::date_concept::modified);
	}

	if (file_created_date.is_valid())
	{
		table->add(tt.dates_file_created, platform::format_date(file_created_date),
		           platform::format_time(file_created_date));
	}

	if (file_modified_date.is_valid())
	{
		table->add(tt.dates_file_modified, platform::format_date(file_modified_date),
		           platform::format_time(file_modified_date));
	}

	e->add(table);

	if (matches)
	{
		e->add(std::make_shared<text_element>(
			str_format(tt.items_created_on_fmt.sv(), *matches, prop::format_date(created_date)), font,
			ui::style::text_style::multiline, flex_item::new_line));
	}

	hover.active_bounds = hover.window_bounds = bounds.offset(element_offset);
}

void view_scroller::draw_scroll(ui::draw_context& dc) const
{
	if (can_scroll())
	{
		const auto text_line_height = dc.text_line_height(ui::style::font_face::dialog);
		const auto sb = track_bounds();
		const auto clr = view_handle_color(false, _active, _tracking, dc.frame_has_focus, false,
		                                   ui::color(dc.colors.background));
		const auto bg_clr = ui::color(0x000000, dc.colors.alpha * dc.colors.bg_alpha);
		const auto handle_margin = track_inset();
		const auto left = sb.left + handle_margin;
		const auto right = sb.right - handle_margin;
		// The bands are a fixed map of the list, so they read the same whether or not the pointer
		// is over the column. Only the thumb answers the pointer.
		const auto label_color = ui::color(dc.colors.foreground, dc.colors.alpha);

		// The thumb is the backdrop of the column, not an overlay on it. It grows with the share of
		// the list on screen, so a short list puts it over most of the sections; drawn last it wiped
		// them out, worst of all under the pointer where it is at its brightest.
		dc.draw_rounded_rect(thumb_bounds(), clr, dc.padding1);

		for_each_band([&](const int band_top, const int band_bottom, const view_scroller_section* so)
		{
			const recti rr(left, sb.top + band_top, right, sb.top + band_bottom);

			dc.draw_rounded_rect(rr, bg_clr, dc.padding1);

			if (!so) return;

			// An icon needs less room than a text line, so gate each on what it actually costs.
			if (so->icon != icon_index::none)
			{
				if (band_bottom - band_top > dc.icon_cxy) xdraw_icon(dc, so->icon, rr, label_color, {});
			}
			else if (!so->text.empty() && band_bottom - band_top > text_line_height)
			{
				dc.draw_text(so->text, rr, ui::style::font_face::dialog,
				             ui::style::text_style::single_line_center, label_color, {});
			}
		});
	}
}

view_scroll_anchor view_scroller::capture_anchor(const view_element_ptr& element) const
{
	view_scroll_anchor result;
	result.valid = !_client_bounds.is_empty();
	result.was_at_start = _offset.y == 0;

	const auto max_scroll = std::max(0, _scroll_extent.cy - _client_bounds.height());
	result.scroll_ratio = max_scroll > 0 ? static_cast<double>(_offset.y) / max_scroll : 0.0;

	if (element)
	{
		const auto logical_bounds = _client_bounds.offset(0, _offset.y);
		if (element->bounds.intersects(logical_bounds))
		{
			result.element = element;
			result.device_top = element->bounds.top - _offset.y;
		}
	}

	return result;
}

recti view_scroller::layout_with_footer(const sizei scroll_extent, const recti client_bounds,
                                        recti scroll_bounds, const int footer_extent, const int gap)
{
	recti footer_bounds;
	if (footer_extent > 0 && !scroll_bounds.is_empty())
	{
		const auto height = std::min(footer_extent, scroll_bounds.height());
		footer_bounds = {
			scroll_bounds.left, scroll_bounds.bottom - height,
			scroll_bounds.right, scroll_bounds.bottom
		};
		scroll_bounds.bottom = std::max(scroll_bounds.top, footer_bounds.top - std::max(0, gap));
	}

	layout(scroll_extent, client_bounds, scroll_bounds);
	_footer_bounds = footer_bounds;
	return footer_bounds;
}

int view_scroller::anchor_offset(const view_scroll_anchor& anchor, const bool element_is_current) const
{
	if (!anchor.valid || anchor.was_at_start) return 0;
	if (anchor.element && element_is_current) return anchor.element->bounds.top - anchor.device_top;

	const auto max_scroll = std::max(0, _scroll_extent.cy - _client_bounds.height());
	return df::round(anchor.scroll_ratio * max_scroll);
}

void view_scroller::restore_anchor(const view_host_ptr& host, const view_scroll_anchor& anchor,
                                   const bool element_is_current)
{
	if (!anchor.valid) return;
	scroll_offset(host, _offset.x, anchor_offset(anchor, element_is_current));
}
