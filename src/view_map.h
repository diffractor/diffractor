// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Map view base class. Delegates tile rendering, pan, zoom, and
// crosshair logic to a map_engine. Subclasses add domain logic.

#pragma once

#include "model.h"
#include "ui_view.h"
#include "ui_map_common.h"

class map_view;

class map_pan_controller final : public view_controller
{
	map_view& _view;
	bool _tracking = false;

public:
	map_pan_controller(map_view& view, const view_host_ptr& host, recti bounds);

	ui::style::cursor cursor() const override
	{
		return _tracking ? ui::style::cursor::hand_up : ui::style::cursor::hand_down;
	}

	void on_mouse_left_button_down(pointi loc, ui::key_state keys) override;
	void on_mouse_move(pointi loc) override;
	void on_mouse_left_button_up(pointi loc, ui::key_state keys) override;
};

class map_view : public view_base
{
protected:
	view_state& _state;
	view_host_ptr _host;
	sizei _extent;
	map_engine _engine;

	friend class map_pan_controller;

public:
	map_view(view_state& state, view_host_ptr host)
		: _state(state), _host(std::move(host)),
		  _engine(state._async, [this] { _host->frame()->invalidate(); })
	{
	}

	void set_map_location(const gps_coordinate loc)
	{
		_engine.set_location(loc, recti(_extent));
	}

	// --- view_base overrides ---

	void activate(const sizei extent) override
	{
		_extent = extent;
		_state.stop();
		_engine.fetch_tiles_for_bounds(recti(_extent));
	}

	void deactivate() override
	{
		_engine.clear_caches();
	}

	void layout(ui::measure_context& mc, const sizei extent) override
	{
		_extent = extent;
	}

	void render(ui::draw_context& dc, view_controller_ptr controller) override
	{
		_engine.render(dc, _extent);
	}

	void mouse_wheel(const pointi loc, const int zDelta, const ui::key_state keys) override
	{
		_engine.zoom(zDelta, recti(_extent));
	}

	void pan_start(const pointi start_loc) override
	{
		_engine.pan_start();
	}

	void pan(const pointi start_loc, const pointi current_loc) override
	{
		_engine.pan(start_loc, current_loc, recti(_extent));
	}

	void pan_end(const pointi start_loc, const pointi final_loc) override
	{
		const auto gps = _engine.pan_end(start_loc, final_loc, recti(_extent));
		on_map_panned(gps);
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, const pointi loc) override
	{
		return std::make_shared<map_pan_controller>(*this, host, recti(_extent));
	}

	// Called after pan completes with the new center GPS. Override to handle reverse geocoding etc.
	virtual void on_map_panned(const gps_coordinate& new_center)
	{
	}
};

// --- map_pan_controller inline implementations (after map_view is complete) ---

inline map_pan_controller::map_pan_controller(map_view& view, const view_host_ptr& host, const recti bounds)
	: view_controller(host, bounds), _view(view)
{
}

inline void map_pan_controller::on_mouse_left_button_down(const pointi loc, const ui::key_state keys)
{
	view_controller::on_mouse_left_button_down(loc, keys);
	_tracking = true;
	_view.pan_start(loc);
}

inline void map_pan_controller::on_mouse_move(const pointi loc)
{
	if (_tracking)
	{
		_view.pan(_start_loc, loc);
	}
}

inline void map_pan_controller::on_mouse_left_button_up(const pointi loc, const ui::key_state keys)
{
	if (_tracking)
	{
		_tracking = false;
		_view.pan_end(_start_loc, loc);
	}
}
