// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2024  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

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

std::atomic_int ui::cancel_gen;

enum class justify
{
	none,
	left,
	right
};

struct bounds_set
{
	recti first, second, third;
};

bounds_set vsplit_bounds(const recti limit, const int y1, const int y2)
{
	bounds_set result;
	result.first = limit;
	result.second = limit;
	result.first.bottom = result.second.top = limit.top + df::mul_div(limit.height(), y1, y1 + y2);
	return result;
}

static bounds_set hsplit_bounds(const recti limit, const int x1, const int x2)
{
	bounds_set result;
	result.first = limit;
	result.second = limit;
	result.first.right = result.second.left = limit.left + df::mul_div(limit.width(), x1, x1 + x2);
	return result;
}

bounds_set vsplit_bounds(const recti limit, const int y1, const int y2, const int y3)
{
	bounds_set result;
	result.first = limit;
	result.second = limit;
	result.third = limit;
	result.first.bottom = result.second.top = limit.top + df::mul_div(limit.height(), y1, y1 + y2 + y3);
	result.second.bottom = result.third.top = limit.top + df::mul_div(limit.height(), y1 + y2, y1 + y2 + y3);
	return result;
}

static bounds_set hsplit_bounds(const recti limit, const int x1, const int x2, const int x3)
{
	bounds_set result;
	result.first = limit;
	result.second = limit;
	result.third = limit;
	result.first.right = result.second.left = limit.left + df::mul_div(limit.width(), x1, x1 + x2 + x3);
	result.second.right = result.third.left = limit.left + df::mul_div(limit.width(), x1 + x2, x1 + x2 + x3);
	return result;
}

static bounds_set split_bounds(const recti limit, const sizei dims1, const sizei dims2, justify jj = justify::none)
{
	const auto vlimit = vsplit_bounds(limit, dims1.cy, dims2.cy);
	const auto hlimit = hsplit_bounds(limit, dims1.cx, dims2.cx);

	auto boundsv1 = ui::scale_dimensions(dims1, vlimit.first, true);
	auto boundsv2 = ui::scale_dimensions(dims2, vlimit.second, true);

	auto boundsh1 = ui::scale_dimensions(dims1, hlimit.first, true);
	auto boundsh2 = ui::scale_dimensions(dims2, hlimit.second, true);

	if (boundsv1.area() + boundsv2.area() > boundsh1.area() + boundsh2.area())
	{
		const auto gap = boundsv2.top - boundsv1.bottom;

		if (gap > 0)
		{
			const auto adjust = gap / 2;
			boundsv1 = boundsv1.offset(0, adjust);
			boundsv2 = boundsv2.offset(0, adjust - gap);
		}

		if (jj == justify::right)
		{
			boundsv1 = boundsv1.offset(limit.right - boundsv1.right, 0);
			boundsv2 = boundsv2.offset(limit.right - boundsv2.right, 0);
		}
		else if (jj == justify::left)
		{
			boundsv1 = boundsv1.offset(limit.left - boundsv1.left, 0);
			boundsv2 = boundsv2.offset(limit.left - boundsv2.left, 0);
		}

		return { boundsv1, boundsv2, {} };
	}
	const auto gap = boundsh2.left - boundsh1.right;

	if (gap > 0)
	{
		const auto adjust = gap / 2;
		boundsh1 = boundsh1.offset(adjust, 0);
		boundsh2 = boundsh2.offset(adjust - gap, 0);
	}

	return { boundsh1, boundsh2, {} };
}

static bounds_set split_bounds(const recti limit, const sizei dims1, const sizei dims2, const sizei dims3,
	justify jj = justify::none)
{
	const auto vlimit = vsplit_bounds(limit, dims1.cy, dims2.cy, dims3.cy);
	const auto hlimit = hsplit_bounds(limit, dims1.cx, dims2.cx, dims3.cx);

	auto boundsv1 = ui::scale_dimensions(dims1, vlimit.first, true);
	auto boundsv2 = ui::scale_dimensions(dims2, vlimit.second, true);
	auto boundsv3 = ui::scale_dimensions(dims3, vlimit.third, true);

	auto boundsh1 = ui::scale_dimensions(dims1, hlimit.first, true);
	auto boundsh2 = ui::scale_dimensions(dims2, hlimit.second, true);
	auto boundsh3 = ui::scale_dimensions(dims3, hlimit.third, true);

	if (boundsv1.area() + boundsv2.area() + boundsv3.area() > boundsh1.area() + boundsh2.area() + boundsh3.area())
	{
		boundsv1 = boundsv1.offset(0, boundsv2.top - boundsv1.bottom);
		boundsv3 = boundsv3.offset(0, boundsv2.bottom - boundsv3.top);

		if (jj == justify::right)
		{
			boundsv1 = boundsv1.offset(limit.right - boundsv1.right, 0);
			boundsv2 = boundsv2.offset(limit.right - boundsv2.right, 0);
			boundsv3 = boundsv3.offset(limit.right - boundsv3.right, 0);
		}
		else if (jj == justify::left)
		{
			boundsv1 = boundsv1.offset(limit.left - boundsv1.left, 0);
			boundsv2 = boundsv2.offset(limit.left - boundsv2.left, 0);
			boundsv3 = boundsv3.offset(limit.left - boundsv3.left, 0);
		}

		return { boundsv1, boundsv2, boundsv3 };
	}
	boundsh1 = boundsh1.offset(boundsh2.left - boundsh1.right, 0);
	boundsh3 = boundsh3.offset(boundsh2.right - boundsh3.left, 0);

	return { boundsh1, boundsh2, boundsh3 };
}

sizei stack_dimensions(const sizei d1, const sizei d2)
{
	return { std::max(d1.cx, d2.cx), d1.cy + d2.cy };
}

sizei stack_dimensions(const sizei d1, const sizei d2, const sizei d3)
{
	return { (std::max(d1.cx, std::max(d2.cx, d3.cx))), d1.cy + d2.cy + d3.cy };
}


std::vector<recti> ui::layout_images(const recti draw_bounds, const std::vector<sizei>& dims)
{
	std::vector<recti> results;
	const auto dim_count = dims.size();

	if (dim_count == 1)
	{
		results.push_back(scale_dimensions(dims[0], draw_bounds, true));
	}
	else if (dim_count == 2)
	{
		const auto split = split_bounds(draw_bounds, dims[0], dims[1]);
		results.push_back(split.first);
		results.push_back(split.second);
	}
	else if (dim_count == 3)
	{
		const auto split = split_bounds(draw_bounds, dims[0], dims[1], dims[2]);
		results.push_back(split.first);
		results.push_back(split.second);
		results.push_back(split.third);
	}
	else if (dim_count == 4)
	{
		const auto split = split_bounds(draw_bounds, stack_dimensions(dims[0], dims[1]),
			stack_dimensions(dims[2], dims[3]));
		const auto spli11 = split_bounds(split.first, dims[0], dims[1], justify::right);
		const auto split2 = split_bounds(split.second, dims[2], dims[3], justify::left);

		results.push_back(spli11.first);
		results.push_back(spli11.second);
		results.push_back(split2.first);
		results.push_back(split2.second);
	}
	else if (dim_count == 5)
	{
		const auto split = split_bounds(draw_bounds, stack_dimensions(dims[0], dims[1]),
			stack_dimensions(dims[2], dims[3], dims[4]));
		const auto spli11 = split_bounds(split.first, dims[0], dims[1], justify::right);
		const auto split2 = split_bounds(split.second, dims[2], dims[3], dims[4], justify::left);

		results.push_back(spli11.first);
		results.push_back(spli11.second);
		results.push_back(split2.first);
		results.push_back(split2.second);
		results.push_back(split2.third);
	}
	else if (dim_count >= 6)
	{
		const auto split = split_bounds(draw_bounds, stack_dimensions(dims[0], dims[1], dims[2]),
			stack_dimensions(dims[3], dims[4], dims[5]));
		const auto spli11 = split_bounds(split.first, dims[0], dims[1], dims[2], justify::right);
		const auto split2 = split_bounds(split.second, dims[3], dims[4], dims[5], justify::left);

		results.push_back(spli11.first);
		results.push_back(spli11.second);
		results.push_back(spli11.third);
		results.push_back(split2.first);
		results.push_back(split2.second);
		results.push_back(split2.third);
	}

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

		if (_tracking)
		{
			const auto split_pos = _parent->calc_compare_pos();
			auto split_bounds = _parent->bounds;
			split_bounds.left = std::max(split_bounds.left, split_pos - 1);
			split_bounds.right = std::min(split_bounds.right, split_pos + 1);
			dc.draw_rect(split_bounds, ui::color(0, hover_alpha));
		}
		else
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

	void escape() override
	{
		_tracking = false;
		_parent->compare(0, _tracking);
	}

	void popup_from_location(view_hover_element& hover) override
	{
		if (!_tracking)
		{
			hover.elements->add(make_icon_element(icon_index::compare, view_element_style::no_break));
			hover.elements->add(std::make_shared<text_element>(tt.compare));
			hover.elements->add(std::make_shared<text_element>(tt.compare_tooltip));
			hover.preferred_size = df::mul_div(hover.preferred_size, 5, 8);
			hover.window_bounds = hover.active_bounds = _bounds;
		}
	}
};

class preview_controller final : public view_controller
{
private:
	std::shared_ptr<side_by_side_control> _parent;
	bool _tracking = false;
	bool _hover = false;

public:
	preview_controller(const view_host_ptr& host, std::shared_ptr<side_by_side_control> mc,
		const recti bounds) : view_controller(host, bounds), _parent(std::move(mc))
	{
	}

	~preview_controller() override
	{
		if (_tracking)
		{
			_tracking = false;
		}

		_parent->_display->_async.invalidate_view(view_invalid::view_redraw);
	}

	void reset(const recti bounds)
	{
		_bounds = bounds;
	}

	void on_mouse_left_button_down(const pointi loc, const ui::key_state keys) override
	{
		_hover = _bounds.contains(loc);
		_tracking = true;
	}

	void on_mouse_move(const pointi loc) override
	{
		_hover = _bounds.contains(loc);

		if (_hover)
		{
			_parent->_display->load_compare_preview(std::clamp(loc.x - _bounds.left, 0, _bounds.width()),
				_bounds.width());
		}
	}

	void on_mouse_left_button_up(const pointi loc, const ui::key_state keys) override
	{
		_hover = _bounds.contains(loc);
		_tracking = false;
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

	bool _start_zoom_state = false;

	zoom_controller(const view_host_ptr& host, std::shared_ptr<photo_control> media_parent, view_state& state,
		const recti view_bounds) :
		view_controller(host, media_parent->_zoom_text_bounds),
		_parent_element(std::move(media_parent)),
		_state(state),
		_view_bounds(view_bounds),
		_start_zoom_state(_parent_element->_display->zoom())
	{
		_zoom_bounds = calc_zoom_bounds();
		_parent_element->show_zoom_box(true);
	}

	~zoom_controller() override
	{
		_tracking = false;
		_parent_element->show_zoom_box(false);
	}

	recti calc_zoom_bounds() const
	{
		const auto media_bounds = _parent_element->bounds;
		const auto zoom_dims = std::clamp(std::min(media_bounds.width(), media_bounds.height()), 80, 200);
		const auto source_orientation = _parent_element->_display->_selected_texture1->display_orientation();
		const auto source_dims = _parent_element->_display->_selected_texture1->display_dimensions();
		const auto result_dims = ui::scale_dimensions(flips_xy(source_orientation) ? source_dims.flip() : source_dims,
			zoom_dims);
		return center_rect(result_dims, _parent_element->_zoom_text_bounds).clamp(_view_bounds);
	}

	ui::style::cursor cursor() const override
	{
		return ui::style::cursor::normal;
	}

	void draw(ui::draw_context& dc) override
	{
		const auto client_bounds = _parent_element->bounds;
		const auto zoom_bounds = _zoom_bounds;
		const auto alpha = dc.colors.overlay_alpha * _alpha;
		const auto excluded = ui::color(0, alpha / 2.0f);
		const auto control_bounds = zoom_bounds.inflate(2, 2);

		try
		{
			const auto display = _parent_element->_display;

			if (display->is_one())
			{
				const auto texture_state = display->_selected_texture1;
				const auto pan_offset = _parent_element->calc_pan_offset(texture_state);
				const auto media_bounds = texture_state->display_bounds().offset(pan_offset);

				const auto l = df::mul_div(client_bounds.left - media_bounds.left, zoom_bounds.width(),
					media_bounds.width()) + zoom_bounds.left;
				const auto t = df::mul_div(client_bounds.top - media_bounds.top, zoom_bounds.height(),
					media_bounds.height()) + zoom_bounds.top;
				const auto r = df::mul_div(client_bounds.right - media_bounds.left, zoom_bounds.width(),
					media_bounds.width()) + zoom_bounds.left;
				const auto b = df::mul_div(client_bounds.bottom - media_bounds.top, zoom_bounds.height(),
					media_bounds.height()) + zoom_bounds.top;

				const auto shown_bounds = recti(l, t, r, b).crop(zoom_bounds);
				const auto zoom_texture = texture_state->zoom_texture(dc, sizei(200, 200));
				const auto sampler = calc_sampler(zoom_bounds.extent(), zoom_texture->dimensions(),
					zoom_texture->_orientation);

				const auto dst_quad = setting.show_rotated
					? quadd(zoom_bounds).transform(
						to_simple_transform(zoom_texture->_orientation))
					: quadd(zoom_bounds);

				dc.draw_rect(control_bounds, ui::color(ui::style::color::group_background, alpha));
				dc.draw_texture(zoom_texture, dst_quad, zoom_texture->dimensions(), alpha, sampler);
				dc.draw_border(shown_bounds, control_bounds, excluded, excluded);
			}
		}
		catch (std::exception& e)
		{
			df::log(__FUNCTION__, e.what());
			dc.draw_rect(control_bounds, ui::color(0x00FF00, alpha));
		}
	}

	void on_mouse_left_button_down(const pointi loc, const ui::key_state keys) override
	{
		_tracking = true;
		_parent_element->_display->zoom(_tracking);
		scroll_to(loc);
	}

	void on_mouse_move(const pointi loc) override
	{
		if (_tracking)
		{
			scroll_to(loc);
		}
	}

	void on_mouse_left_button_up(const pointi loc, const ui::key_state keys) override
	{
		_tracking = false;
		_parent_element->_display->zoom(_start_zoom_state);
	}

	void escape() override
	{
		_parent_element->_display->zoom(_start_zoom_state);
		_tracking = false;
	}

	void scroll_to(const pointi loc)
	{
		if (_last_loc != loc)
		{
			const auto dimensions = _parent_element->_display->_selected_texture1->display_dimensions();
			const auto center = _zoom_bounds.center();
			const auto x = df::mul_div(center.x - loc.x, dimensions.cx, _zoom_bounds.width());
			const auto y = df::mul_div(center.y - loc.y, dimensions.cy, _zoom_bounds.height());

			_parent_element->scroll_image_to({ x, y });
			_last_loc = loc;
		}
	}

	void popup_from_location(view_hover_element& hover) override
	{
		if (!_parent_element->_display->zoom())
		{
			hover.elements->add(make_icon_element(icon_index::zoom_in, view_element_style::no_break));
			hover.elements->add(std::make_shared<text_element>(tt.zoom_tooltip));
			hover.elements->add(std::make_shared<action_element>(tt.zoom_kb));
			hover.preferred_size = df::mul_div(hover.preferred_size, 5, 8);
			hover.window_bounds = hover.active_bounds = _zoom_bounds;
		}
	}
};

class pan_controller final : public view_controller
{
public:
	std::shared_ptr<photo_control> _parent;
	view_state& _state;
	pointi _start_scroll_loc;
	bool _tracking = false;

	pan_controller(const view_host_ptr& host, std::shared_ptr<photo_control> parent, view_state& s,
		const recti bounds) : view_controller(host, bounds), _parent(std::move(parent)), _state(s)
	{
		_parent->show_zoom_box(true);
	}

	~pan_controller() override
	{
		_tracking = false;
		_parent->show_zoom_box(false);
	}

	ui::style::cursor cursor() const override
	{
		return _tracking ? ui::style::cursor::hand_up : ui::style::cursor::hand_down;
	}

	void draw(ui::draw_context& rc) override
	{
	}

	void on_mouse_left_button_down(const pointi loc, const ui::key_state keys) override
	{
		_start_scroll_loc = _parent->image_offset();
		_start_loc = loc;
		_tracking = true;
		scroll_to(loc);
	}

	void on_mouse_move(const pointi loc) override
	{
		if (_tracking)
		{
			scroll_to(loc);
		}
	}

	void on_mouse_left_button_up(const pointi loc, const ui::key_state keys) override
	{
		_tracking = false;
	}

	void escape() override
	{
		if (_tracking)
		{
			_tracking = false;
			_parent->scroll_image_to(_start_scroll_loc);
		}
	}

	void scroll_to(const pointi loc)
	{
		if (_last_loc != loc)
		{
			const auto offset = loc - _start_loc;
			_parent->scroll_image_to(_start_scroll_loc + offset);
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

	if (bounds.contains(logical_loc) || _zoom_text_bounds.contains(logical_loc))
	{
		if (_can_pan)
		{
			if (_display->zoom())
			{
				auto pan_bounds = bounds;
				//pan_bounds.exclude(loc, zoom_bounds);
				controller = std::make_shared<pan_controller>(host, shared_from_this(), _state, pan_bounds);
			}
			else
			{
				if (_zoom_text_bounds.contains(logical_loc))
				{
					controller = std::make_shared<zoom_controller>(host, shared_from_this(), _state, _host->_extent);
				}
			}
		}
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
		result_clr = ui::style::color::important_background;
		result_clr.a = 1.0f;
	}
	else if (style && view_element_style::info)
	{
		result_clr = ui::style::color::info_background;
		result_clr.a = 1.0f;
	}
	else if (style && view_element_style::dark_background)
	{
		result_clr.a = 0.777f;
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
				//_bg_color.merge(bg);

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


	if (_display->_can_compare && _display->_compare_bounds.contains(logical_loc))
	{
		return std::make_shared<compare_controller>(host, shared_from_this(), _display->_compare_bounds);
	}

	if (_display->is_compare_video_preview(loc))
	{
		const auto tex_bounds = _display->_selected_texture1->display_bounds().make_union(
			_display->_selected_texture2->display_bounds());
		return std::make_shared<preview_controller>(host, shared_from_this(), tex_bounds);
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
	else if (event.type == view_element_event_type::dpi_changed)
	{
		_tl.reset();
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

void rate_label_control::render(ui::draw_context& dc, const pointi element_offset) const
{
	const auto view_bounds = bounds.offset(element_offset);
	_view_bounds = view_bounds;

	const auto i = _item;

	if (i)
	{
		auto icon = icon_index::flag;
		ui::color32 label_clr = 0;

		if (i->rating() == -1)
		{
			icon = icon_index::del;
		}

		const auto md = i->metadata();

		if (md)
		{
			if (!is_empty(md->label))
			{
				const auto label = md->label;

				if (icmp(label, label_select_text) == 0) label_clr = color_label_select;
				else if (icmp(label, label_second_text) == 0) label_clr = color_label_second;
				else if (icmp(label, label_approved_text) == 0) label_clr = color_label_approved;
				else if (icmp(label, label_review_text) == 0) label_clr = color_label_review;
				else if (icmp(label, label_to_do_text) == 0) label_clr = color_label_to_do;
				else label_clr = ui::average(dc.colors.foreground, dc.colors.background);
			}

			if (md->rating < 0)
			{
				label_clr = color_rate_rejected;
			}
		}

		const auto bg = calc_background_color(dc);
		const auto has_label_clr = label_clr != 0;
		const auto alpha = has_label_clr ? dc.colors.alpha : dc.colors.alpha / 4.0f;

		auto lablel_bg = bg;

		if (has_label_clr)
		{
			lablel_bg.merge(ui::color(label_clr, dc.colors.alpha));
		}

		xdraw_icon(dc, icon, view_bounds, ui::color(dc.colors.foreground, alpha), lablel_bg);
	}
}

static metadata_edits make_edit(const int r)
{
	metadata_edits edits;
	edits.rating = r;
	return edits;
}

static metadata_edits make_edit(const std::u8string_view label)
{
	metadata_edits edits;
	edits.label = label;
	return edits;
}

static std::function<void()> make_invoke(view_state& s, const ui::control_frame_ptr& parent,
	const view_host_base_ptr& view, const df::item_element_ptr& item,
	const metadata_edits& edits)
{
	return [&s, parent, view, edits, item]
		{
			const auto icon = icon_index::star;
			const auto title = tt.title_updating;

			const df::item_elements items_to_modify = { {item} };
			s.modify_items(parent, icon, title, items_to_modify, edits, view);
		};
}


static ui::command_ptr def_command(const commands id, const icon_index icon, const std::u8string_view text,
	const ui::color32 clr, std::function<void()> invoke, const bool is_checked)
{
	auto c = std::make_shared<ui::command>();
	c->icon = icon;
	c->text = text;
	c->clr = clr;
	c->invoke = std::move(invoke);
	c->checked = is_checked;
	return c;
}


bool has_label(const df::item_element_ptr& item, const std::u8string_view label_text)
{
	return str::icmp(item->label(), label_text) == 0;
}

void rate_label_control::dispatch_event(const view_element_event& event)
{
	if (event.type == view_element_event_type::invoke)
	{
		const df::item_set items = { {_item} };
		const auto parent = event.host->owner();
		const auto view = event.host;

		const std::vector<ui::command_ptr> result = {
			def_command(commands::rate_none, icon_index::none, tt.command_rate_0, 0,
						make_invoke(_state, parent, view, _item, make_edit(0)), false),
			def_command(commands::rate_rejected, icon_index::none, tt.command_rate_rejected, color_rate_rejected,
						make_invoke(_state, parent, view, _item, make_edit(-1)), _item->rating() == -1),
			nullptr,
			def_command(commands::label_approved, icon_index::none, tt.command_label_approved, color_label_approved,
						make_invoke(_state, parent, view, _item, make_edit(label_approved_text)),
						has_label(_item, label_approved_text)),
			def_command(commands::label_to_do, icon_index::none, tt.command_label_to_do, color_label_to_do,
						make_invoke(_state, parent, view, _item, make_edit(label_to_do_text)),
						has_label(_item, label_to_do_text)),
			def_command(commands::label_select, icon_index::none, tt.command_label_select, color_label_select,
						make_invoke(_state, parent, view, _item, make_edit(label_select_text)),
						has_label(_item, label_select_text)),
			def_command(commands::label_review, icon_index::none, tt.command_label_review, color_label_review,
						make_invoke(_state, parent, view, _item, make_edit(label_review_text)),
						has_label(_item, label_review_text)),
			def_command(commands::label_second, icon_index::none, tt.command_label_second, color_label_second,
						make_invoke(_state, parent, view, _item, make_edit(label_second_text)),
						has_label(_item, label_second_text)),
			def_command(commands::label_none, icon_index::none, tt.command_label_none, 0,
						make_invoke(_state, parent, view, _item, make_edit(std::u8string_view{})), false),
		};

		event.host->track_menu(_view_bounds, result);
	}
}

void rate_label_control::tooltip(view_hover_element& hover, const pointi loc, const pointi element_offset) const
{
	hover.elements->add(make_icon_element(icon_index::flag, view_element_style::no_break));
	hover.elements->add(std::make_shared<text_element>(tt.command_view_rate_label, ui::style::font_face::dialog,
		ui::style::text_style::multiline,
		view_element_style::line_break));

	const auto table = std::make_shared<ui::table_element>();
	table->add(str::format(u8"{}:"sv, tt.prop_name_label), _item->label());
	table->add(str::format(u8"{}:"sv, tt.prop_name_rating), prop::format_rating(_item->rating()));

	hover.elements->add(table);

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
			def_command(commands::show_raw_always, icon_index::none, tt.show_raw, 0, show_raw_always,
						!setting.raw_preview),
			def_command(commands::show_raw_preview, icon_index::none, tt.preview_show_preview, 0,
						show_raw_preview_always, setting.raw_preview),
			def_command(commands::show_raw_this_only, icon_index::none, tt.show_raw_now, 0, show_raw_this_only, false),
		};

		event.host->track_menu(_view_bounds, result);
	}
}

void preview_control::tooltip(view_hover_element& hover, const pointi loc, const pointi element_offset) const
{
	hover.elements->add(make_icon_element(icon_index::preview, view_element_style::no_break));

	auto text = tt.preview_rendered;
	if (_ts->is_preview()) text = tt.preview_showing;
	if (_ts->is_preview_rendering()) text = tt.preview_rendering;

	hover.elements->add(std::make_shared<text_element>(text, ui::style::font_face::dialog,
		ui::style::text_style::multiline,
		view_element_style::line_break));
	hover.active_bounds = hover.window_bounds = bounds.offset(element_offset);
}

void items_dates_control::tooltip(view_hover_element& hover, const pointi loc, const pointi element_offset) const
{
	static std::atomic_int version;
	const df::cancel_token token(version);

	const auto font = ui::style::font_face::dialog;
	const auto md = _item->metadata();
	const auto metadata_created_date = md ? md->created() : df::date_t::null;
	const auto file_created_date = _item->file_created();
	const auto file_modified_date = _item->file_modified();
	const auto created_date = _item->media_created();
	const auto st = created_date.date();
	const auto search = df::search_t().day(st.day, st.month, st.year);
	const auto matches = _state.item_index.count_matches(search, token);
	const auto e = hover.elements;

	e->add(make_icon_element(search.first_type()->icon, view_element_style::no_break));
	e->add(std::make_shared<text_element>(tt.dates_title, ui::style::font_face::title,
		ui::style::text_style::single_line, view_element_style::line_break));

	const auto table = std::make_shared<ui::table_element>(view_element_style::center);

	if (metadata_created_date.is_valid())
	{
		table->add(tt.dates_metadata_created, platform::format_date(metadata_created_date),
			platform::format_time(metadata_created_date));
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
	e->add(std::make_shared<text_element>(
		str::format(tt.items_created_on_fmt, matches.total_items().count, prop::format_date(created_date)), font,
		ui::style::text_style::multiline, view_element_style::new_line));
	e->add(std::make_shared<action_element>(tt.click_to_open));

	hover.active_bounds = hover.window_bounds = bounds.offset(element_offset);
}

void view_scroller::draw_scroll(ui::draw_context& dc) const
{
	if (can_scroll())
	{
		const auto text_line_height = dc.text_line_height(ui::style::font_face::dialog);
		const auto has_sections = !_sections.empty();
		const auto sb = _scroll_bounds.inflate(0, -2);
		const auto y = df::mul_div(_offset.y, sb.height(), _scroll_extent.cy);
		const auto cy = df::mul_div(sb.height(), _client_bounds.height(), _scroll_extent.cy);
		const auto clr = view_handle_color(false, _active, _tracking, dc.frame_has_focus, false, dc.colors.background);
		const auto bg_clr = ui::color(0x000000, dc.colors.alpha * dc.colors.bg_alpha);
		const auto y_padding = (cy >= 20) ? 1 : 10;
		const auto handle_margin = (has_sections || _active) ? 1 : df::mul_div(sb.width(), 2, 9);
		const auto left = sb.left + handle_margin;
		const auto right = sb.right - handle_margin;

		if (has_sections)
		{
			const auto padding = 2;
			auto top = 0;

			for (const auto& so : _sections)
			{
				const auto bottom = df::mul_div(so.y, _client_bounds.height(), _scroll_extent.cy);
				const auto height = bottom - top;
				const recti rr(left, sb.top + top, right, sb.top + bottom);
				dc.draw_rounded_rect(rr, bg_clr, dc.padding1);
				top = bottom + padding;

				if (height > text_line_height)
				{
					if (so.icon != icon_index::none)
					{
						const auto text_color = ui::color(dc.colors.foreground, dc.colors.alpha * (_active ? 1.0f : 0.66f));
						xdraw_icon(dc, so.icon, rr, text_color, {});
					}
					else if (!so.text.empty())
					{
						const auto text_color = ui::color(dc.colors.foreground, dc.colors.alpha * (_active ? 1.0f : 0.66f));
						dc.draw_text(so.text, rr, ui::style::font_face::dialog, ui::style::text_style::single_line_center,
							text_color, {});
					}
				}
			}
		}
		else
		{
			const recti rr(left, sb.top, right, sb.bottom);
			dc.draw_rounded_rect(rr, bg_clr, dc.padding1);
		}

		const auto top = std::clamp(sb.top + y - y_padding, sb.top, sb.bottom);
		const auto bottom = std::clamp(sb.top + y + cy + y_padding, sb.top, sb.bottom);
		const recti r(left + 1, top, right - 1, bottom);
		dc.draw_rounded_rect(r, clr, dc.padding1);
	}
}
