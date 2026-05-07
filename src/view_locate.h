// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Location assignment view. Provides a map-based interface for setting
// GPS coordinates on selected items, with search and reverse geocoding.
// The map fills the main view area (left panel) and search/properties are in
// the right panel (view_controls).

#pragma once

#include "view_map.h"

class view_controls_host;
using view_controls_host_ptr = std::shared_ptr<view_controls_host>;

class locate_view final :
	public map_view,
	public std::enable_shared_from_this<locate_view>
{
	std::string _status;
	std::shared_ptr<selected_location_t> _location;
	std::function<void()> _populate_controls;
	// Monotonic request id used to discard stale reverse-geocode results when the
	// user pans the map quickly and multiple background requests are in flight.
	uint64_t _geocode_request_id = 0;

public:
	locate_view(view_state& state, view_host_ptr host) : map_view(state, std::move(host))
	{
		_location = std::make_shared<selected_location_t>();
	}

	void run();
	void refresh() override;

	bool can_run() const
	{
		return gps_coordinate(_location->latitude, _location->longitude).is_valid();
	}

	std::string_view status() override
	{
		return _status;
	}

	void activate(sizei extent) override;
	void deactivate() override;

	void on_map_panned(const gps_coordinate& new_center) override;

	view_controls_host_ptr controls(const ui::control_frame_ptr& owner);

	void exit() override
	{
		_state.view_mode(view_type::items);
	}

	std::string_view title() override
	{
		return s_app_name;
	}

	const std::shared_ptr<selected_location_t>& location() const { return _location; }
	void update_location(const location_t& loc);

	void broadcast_event(const view_element_event& event) const override
	{
	}

private:
	// Queue an async reverse-geocode lookup for the given GPS position. Tags the
	// request with a monotonic id so stale results from earlier requests are
	// discarded when the user pans rapidly or makes an explicit selection.
	void request_reverse_geocode(const gps_coordinate& gps);
};
