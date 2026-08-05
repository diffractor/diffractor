// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Shared preview and processing view for batch conversion, metadata, and date tools.

#pragma once

#include "view_list.h"
#include "app_util.h"

enum class batch_tool_mode
{
	convert,
	metadata,
	adjust_date,
};

class batch_tool_view final : public list_view, public std::enable_shared_from_this<batch_tool_view>
{
	batch_tool_mode _mode = batch_tool_mode::convert;
	std::string _title;
	std::string _status;
	std::vector<convert_item_plan> _convert_plan;
	df::date_t _original_start;
	df::date_t _new_start;
	bool _new_start_seeded = false;
	std::string _metadata_title;
	std::string _metadata_comment;
	std::string _metadata_synopsis;
	int _metadata_rating = 0;
	int _metadata_year = 0;
	df::date_t _metadata_created;
	df::xy8 _metadata_episode = {0, 0};
	int _metadata_season = 0;
	df::xy8 _metadata_track = {0, 0};
	df::xy8 _metadata_disk = {0, 0};
	metadata_edits metadata_changes() const;
	void update_date_start();
	void refresh_convert();
	void refresh_metadata();
	void refresh_dates();
	void run_convert();
	void run_metadata();
	void run_dates();
	// Called from the worker: hands a finished run back to the UI thread.
	static void queue_run_result(const view_state& s, const std::shared_ptr<batch_tool_view>& view,
	                             platform::file_op_result result, bool canceled, std::string title,
	                             std::shared_ptr<detach_file_handles> detach, std::string error);

public:
	batch_tool_view(view_state& state, view_host_ptr host) : list_view(state, std::move(host)) { col_count = 3; }

	void mode(const batch_tool_mode value) { _mode = value; }
	batch_tool_mode mode() const { return _mode; }
	void run();
	text_t run_text() const;
	void refresh() override;
	void activate(sizei extent) override;
	void deactivate() override;
	bool can_run() const;
	view_controls_host_ptr controls(const ui::control_frame_ptr& owner);
	std::string_view status() override { return _status; }
	std::string_view title() override;
	text_t empty_message() override { return tt.view_empty_message; }
	std::array<text_t, max_col_count> col_titles() override;
	std::string_view operation_name() const override;

	void exit() override
	{
		if (!confirm_exit_while_processing(operation_name())) return;
		_state.view_mode(view_type::items);
	}
};
