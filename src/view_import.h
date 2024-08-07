// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2024  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

#include "app_util.h"
#include "view_list.h"

class import_view final :
	public list_view,
	public std::enable_shared_from_this<import_view>
{
	bool _select_other_folder = false;
	std::vector<import_source> _sources;
	std::u8string _status;

public:
	import_view(view_state& state, view_host_ptr host) : list_view(state, std::move(host))
	{
		col_count = 3;
	}

	std::array<text_t, max_col_count> col_titles() override
	{
		return std::array<text_t, max_col_count>
		{
			tt.action,
			tt.source,
			tt.destination,
			{}
		};
	};

	text_t empty_message() override { return tt.view_empty_message; }

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

	void update_rows(const import_analysis_result& analysis_result);
};

