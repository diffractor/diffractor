// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2025  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Folder synchronization view. Compares source and destination folders,
// shows differences, and performs bidirectional sync operations.

#include "pch.h"
#include "model.h"
#include "model_db.h"
#include "model_index.h"
#include "ui_dialog.h"
#include "view_sync.h"
#include "app.h"
#include "app_command_status.h"
#include "app_util.h"
#include "ui_controls.h"

static uint32_t count_sync(const sync_analysis_items& analysis, const sync_action sync_action)
{
	uint32_t result = 0;

	for (const auto& f : analysis)
	{
		if (f.second.action == sync_action)
		{
			++result;
		}
	}

	return result;
}

static uint32_t count_sync_actions(const sync_analysis_result& analysis)
{
	uint32_t result = 0;

	for (const auto& i : analysis)
	{
		for (const auto& f : i.second)
		{
			if (f.second.action != sync_action::none)
			{
				++result;
			}
		}
	}

	return result;
}

static auto calc_sync_source(const df::index_roots& roots, const bool select_other_folder)
{
	df::index_roots result;

	if (select_other_folder)
	{
		result.folders.emplace(df::folder_path(setting.sync.local_path));
	}
	else
	{
		result = roots;
	}

	return result;
};

view_controls_host_ptr sync_view::controls(const ui::control_frame_ptr& owner)
{
	auto result = std::make_shared<view_controls_host>(_state);
	auto frame = owner->create_dlg(result, false);

	const auto& items = _state.selected_items();

	std::vector<view_element_ptr> controls;

	controls.emplace_back(std::make_shared<text_element>(tt.command_sync, ui::style::font_face::title));
	controls.emplace_back(std::make_shared<text_element>(tt.sync_info_1));
	controls.emplace_back(std::make_shared<text_element>(tt.for_example));
	controls.emplace_back(std::make_shared<text_element>(tt.sync_info_2));
	controls.emplace_back(std::make_shared<divider_element>());
	controls.emplace_back(std::make_shared<text_element>(tt.sync_local));

	bool select_collection = setting.sync.sync_collection;
	_select_other_folder = !select_collection;

	auto collection_check = std::make_shared<ui::check_control>(frame, tt.sync_collection, select_collection, true);
	controls.emplace_back(collection_check);

	auto other_folder_check = std::make_shared<ui::check_control>(frame, tt.sync_other_folder, _select_other_folder,
	                                                              true);
	other_folder_check->child(std::make_shared<ui::folder_picker_control>(frame, setting.sync.local_path));
	controls.emplace_back(other_folder_check);

	controls.emplace_back(std::make_shared<divider_element>());
	controls.emplace_back(std::make_shared<text_element>(tt.sync_remote));
	controls.emplace_back(std::make_shared<ui::folder_picker_control>(frame, setting.sync.remote_path));
	controls.emplace_back(
		std::make_shared<ui::check_control>(frame, tt.sync_local_remote, setting.sync.sync_local_remote));
	controls.emplace_back(
		std::make_shared<ui::check_control>(frame, tt.sync_remote_local, setting.sync.sync_remote_local));
	controls.emplace_back(
		std::make_shared<ui::check_control>(frame, tt.sync_delete_remote, setting.sync.sync_delete_remote));
	controls.emplace_back(
		std::make_shared<ui::check_control>(frame, tt.sync_delete_local, setting.sync.sync_delete_local));

	for (const auto& c : controls)
	{
		c->margin.cx = 8;
		c->margin.cy = 4;
	}

	result->_controls = controls;
	result->_frame = result->_dlg = frame;

	return result;
}

void sync_view::update_rows(const sync_analysis_result& analysis_result)
{
	const auto blue_text_color = ui::lighten(ui::style::color::dialog_selected_background, 0.55f);
	const auto gray_text_color = ui::darken(ui::style::color::view_text, 0.22f);
	const auto orange_text_color = ui::lighten(ui::style::color::important_background, 0.55f);

	std::vector<row_element_ptr> rows;

	int ignore = 0;
	int copy_local = 0;
	int copy_remote = 0;
	int delete_local = 0;
	int delete_remote = 0;

	for (const auto& a : analysis_result)
	{
		for (const auto& i : a.second)
		{
			auto row = std::make_shared<row_element>(*this);

			switch (i.second.action)
			{
			case sync_action::none:
				row->_text_color[0] = gray_text_color;
				row->_text_color[1] = gray_text_color;
				row->_text_color[2] = gray_text_color;
				row->_text[1] = i.second.local_path.pack();
				row->_text[2] = i.second.remote_path.pack();
				row->_text[0] = tt.ignore;
				row->_order = 100;
				ignore += 1;
				break;
			case sync_action::copy_local:
				row->_text_color[0] = blue_text_color;
				row->_text_color[1] = blue_text_color;
				row->_text[0] = tt.sync_copy_local_action;
				row->_text[1] = i.second.local_path.pack();
				row->_text[2] = i.second.remote_path.pack();
				row->_order = 1;
				copy_local += 1;
				break;
			case sync_action::copy_remote:
				row->_text_color[0] = blue_text_color;
				row->_text_color[2] = blue_text_color;
				row->_text[0] = tt.sync_copy_remote_action;
				row->_text[1] = i.second.local_path.pack();
				row->_text[2] = i.second.remote_path.pack();
				row->_order = 2;
				copy_remote += 1;
				break;
			case sync_action::delete_local:
				row->_text_color[0] = orange_text_color;
				row->_text_color[1] = orange_text_color;
				row->_text[0] = tt.sync_delete_local_action;
				row->_text[1] = i.second.local_path.pack();
				row->_order = 3;
				delete_local += 1;
				break;
			case sync_action::delete_remote:
				row->_text_color[0] = orange_text_color;
				row->_text_color[2] = orange_text_color;
				row->_text[0] = tt.sync_delete_remote_action;
				row->_text[2] = i.second.remote_path.pack();
				row->_order = 4;
				delete_remote += 1;
				break;
			}

			rows.emplace_back(row);
		}
	}

	_rows = std::move(rows);
	_status = str::format(u8"{} {}   {} {}   {} {}   {} {}   {} {}"sv,
	                      copy_local, tt.sync_copy_local_action,
	                      copy_remote, tt.sync_copy_remote_action,
	                      delete_local, tt.sync_delete_local_action,
	                      delete_remote, tt.sync_delete_remote_action,
	                      ignore, tt.ignore);

	_state.invalidate_view(view_invalid::view_layout | view_invalid::controller | view_invalid::status);
}


void sync_view::run() const
{
	const auto title = tt.command_sync;
	constexpr auto icon = icon_index::sync;

	record_feature_use(features::sync);
	detach_file_handles detach(_state);

	const auto sync_source = calc_sync_source(_state.item_index.index_roots(), _select_other_folder);
	_state.recent_folders.add(setting.import.destination_path);

	const auto dlg = make_dlg(_host->owner());
	dlg->show_status(icon, tt.processing);

	auto token = df::cancel_token(ui::cancel_gen);
	const auto results = std::make_shared<command_status>(_state._async, dlg, icon, title, 0);

	_state.queue_async(async_queue::work, [&s = _state, results, sync_source, token]
	{
		result_scope rr(results);
		const df::folder_path remote_path(setting.sync.remote_path);
		const auto analysis_result = sync_analysis(sync_source, remote_path,
		                                           setting.sync.sync_local_remote,
		                                           setting.sync.sync_remote_local,
		                                           setting.sync.sync_delete_local,
		                                           setting.sync.sync_delete_remote, token);

		results->total(count_sync_actions(analysis_result));
		sync_copy(results, analysis_result, token);

		rr.complete();
	});

	results->wait_for_complete();
	dlg->_frame->destroy();
}

void sync_view::analyze()
{
	const auto title = tt.command_sync;
	constexpr auto icon = icon_index::sync;

	platform::thread_event event_analyze(true, false);
	const auto sync_source = calc_sync_source(_state.item_index.index_roots(), _select_other_folder);
	auto token = df::cancel_token(ui::cancel_gen);

	const auto status_dlg = make_dlg(_host->owner());
	status_dlg->show_cancel_status(icon, tt.analyzing, [] { ++ui::cancel_gen; });

	_state.queue_async(async_queue::work, [&s = _state, &event_analyze, sync_source, t = shared_from_this(), token]
	{
		const df::folder_path remote_path(setting.sync.remote_path);
		const auto analysis_result = sync_analysis(sync_source, remote_path,
		                                           setting.sync.sync_local_remote,
		                                           setting.sync.sync_remote_local,
		                                           setting.sync.sync_delete_local,
		                                           setting.sync.sync_delete_remote, token);

		if (!token.is_cancelled())
		{
			s.queue_ui([analysis_result, t]
			{
				t->update_rows(analysis_result);
			});
			event_analyze.set();
		}
	});

	platform::wait_for({event_analyze}, 100000, false);
	status_dlg->_frame->destroy();
}

void sync_view::refresh()
{
	_state.invalidate_view(view_invalid::view_layout | view_invalid::controller);
}

void sync_view::reload()
{
	analyze();
}
