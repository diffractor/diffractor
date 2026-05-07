// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Map control dialog widget for location picking in metadata edit dialogs.

#pragma once

#include "ui_map_common.h"

class map_control final : public view_element, public std::enable_shared_from_this<map_control>, public ui::frame_host
{
public:
	ui::frame_ptr _frame;
	sizei _extent;
	async_strategy& _async;
	std::function<void(gps_coordinate)> _cb;

	std::unique_ptr<map_engine> _engine;

	bool _hover = false;
	pointi _start_loc;

	map_control(async_strategy& async, std::function<void(gps_coordinate)> cb) : _async(async), _cb(
			std::move(cb))
	{
	}

	void init(const ui::control_frame_ptr& owner)
	{
		_frame = owner->create_frame(weak_from_this(), {});
		_engine = std::make_unique<map_engine>(_async, [this] { if (_frame) _frame->invalidate(); });
	}

	sizei measure(ui::measure_context& mc, const int cx) const override
	{
		return {cx, cx};
	}

	void visit_controls(const std::function<void(const ui::control_base_ptr&)>& handler) override
	{
		handler(_frame);
	}

	void layout(ui::measure_context& mc, const recti bounds_in, ui::control_layouts& positions) override
	{
		bounds = bounds_in;
		positions.emplace_back(_frame, bounds, is_visible());
	}

	void on_window_layout(ui::measure_context& mc, const sizei extent, const bool is_minimized) override
	{
		_extent = extent;
	}

	void on_mouse_move(const pointi loc, const bool is_tracking) override
	{
		if (!_hover)
		{
			_hover = true;
			_frame->invalidate();
		}

		if (is_tracking && _engine)
		{
			_engine->pan(_start_loc, loc, calc_bounds());
		}
	}

	void on_mouse_left_button_down(const pointi loc, const ui::key_state keys) override
	{
		_start_loc = loc;
		if (_engine)
			_engine->pan_start();
	}

	void on_mouse_left_button_up(const pointi loc, const ui::key_state keys) override
	{
		if (_engine)
		{
			const auto gps = _engine->pan_end(_start_loc, loc, calc_bounds());
			send_location_changed_event(gps);
		}
		_start_loc = {0, 0};
	}

	void on_mouse_leave(const pointi loc) override
	{
		if (_hover)
		{
			_hover = false;
			_frame->invalidate();
		}
	}

	void send_location_changed_event(const gps_coordinate& loc) const
	{
		if (_cb)
		{
			_cb(loc);
		}
	}

	void on_mouse_wheel(const pointi loc, const int delta, const ui::key_state keys, bool& was_handled) override
	{
		if (_engine)
		{
			was_handled = _engine->zoom(delta, calc_bounds());
		}
	}

	void tick() override
	{
	}

	void activate(bool is_active) override
	{
	}

	bool key_down(const int c, const ui::key_state keys) override
	{
		return false;
	}

	void on_window_paint(ui::draw_context& dc) override
	{
		if (_engine)
		{
			_engine->render(dc, _extent);
		}
	}

	recti calc_bounds() const
	{
		return recti(_extent);
	}

	void set_location_marker(const gps_coordinate loc)
	{
		if (_engine)
		{
			_engine->set_location(loc, calc_bounds());
		}
	}
};

using map_control_ptr = std::shared_ptr<map_control>;
