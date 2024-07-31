// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

#include "view_list.h"
#include "app_util.h"

using view_controls_host_ptr = std::shared_ptr<view_controls_host>;

class rename_view final :
	public list_view,
	public std::enable_shared_from_this<rename_view>
{
	std::vector<rename_item> _renames;
	std::u8string _status;

public:
	rename_view(view_state& state, view_host_ptr host) : list_view(state, std::move(host))
	{
		col_count = 3;
		col_titles[1] = tt.old_name;
		col_titles[2] = tt.new_name;
	}

	void exit() override
	{
		_state.view_mode(view_type::items);
	}

	void run();
	void refresh() override;

	std::u8string_view status() override
	{
		return _status;
	}

	void activate(sizei extent) override
	{
		refresh();
	}

	void deactivate() override
	{
		_rows.clear();
		_renames.clear();
		_status.clear();
	}

	std::u8string_view title() override
	{
		return s_app_name;
	}

	view_controls_host_ptr controls(const ui::control_frame_ptr& owner);
};

