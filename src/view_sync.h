// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2024  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

#include "view_list.h"
#include "app_util.h"


class sync_view final :
	public list_view,
	public std::enable_shared_from_this<sync_view>
{

	bool _select_other_folder = false;
	std::u8string _status;

public:
	sync_view(view_state& state, view_host_ptr host) : list_view(state, std::move(host))
	{
		col_count = 3;
		col_titles[0] = tt.action;
		col_titles[1] = tt.local;
		col_titles[2] = tt.remote;
		_empty_message = tt.view_empty_message;
	}

	void run();
	void analyze();
	void refresh() override;
	void reload() override;

	std::u8string_view status() override
	{
		return _status;
	}

	void deactivate() override
	{
		_rows.clear();
		_status.clear();
	}

	view_controls_host_ptr controls(const ui::control_frame_ptr& owner);

	void exit() override
	{
		_state.view_mode(view_type::items);
	}

	std::u8string_view title() override
	{
		return s_app_name;
	}

	void update_rows(const sync_analysis_result& analysis_result);
};

