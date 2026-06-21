// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Application commands, toolbars, initialization, and lifecycle management.
// Contains command definitions, toolbar creation, app init/exit, and crash handling.

#include "pch.h"

#include "app_text.h"

#include "model_location.h"
#include "model_index.h"
#include "model_db.h"
#include "model.h"

#include "ui_dialog.h"
#include "view_test.h"
#include "view_items.h"
#include "view_sync.h"
#include "view_import.h"
#include "view_locate.h"

#include "app_sidebar.h"
#include "app_commands.h"
#include "app.h"

#include <utility>

bool toggle_details_state = false;

void app_frame::create_toolbars()
{
	const std::vector<ui::command_ptr> tbButtonsNav1 =
	{
		find_command(commands::view_show_sidebar),
#ifndef WINSTORE
		find_command(commands::info_new_version),
#endif
		find_command(commands::tool_test),
		find_command(commands::browse_back),
		find_command(commands::browse_forward),
	};

	const std::vector<ui::command_ptr> tbButtonsNav2 =
	{
		find_command(commands::favorite),
		find_command(commands::options_collection),
		find_command(commands::browse_parent),
		find_command(commands::browse_previous_folder),
		find_command(commands::browse_next_folder),
		find_command(commands::advanced_search),
		find_command(commands::menu_main),
	};

	const std::vector<ui::command_ptr> tbButtonsNav3 =
	{
		find_command(commands::view_minimize),
		find_command(commands::view_maximize),
		find_command(commands::view_restore),
		find_command(commands::exit),
	};

	const std::vector<ui::command_ptr> tool_buttons =
	{
		find_command(commands::browse_previous_item),
		find_command(commands::browse_next_item),
		nullptr,
		find_command(commands::menu_open),
		find_command(commands::tool_edit),
		find_command(commands::tool_rotate_anticlockwise),
		find_command(commands::tool_rotate_clockwise),
		find_command(commands::menu_tag_with),
		find_command(commands::menu_tools_toolbar),
		find_command(commands::menu_playback)
	};

	const std::vector<ui::command_ptr> sorting_buttons =
	{
		find_command(commands::filter_photos),
		find_command(commands::filter_videos),
		find_command(commands::filter_audio),
		nullptr,
		find_command(commands::browse_recursive),
		find_command(commands::option_toggle_details),
		find_command(commands::option_toggle_item_size),
		find_command(commands::menu_group_toolbar),
	};

	const std::vector<ui::command_ptr> media_edit_commands =
	{
		find_command(commands::edit_item_save_and_prev),
		find_command(commands::edit_item_save_and_next),
		find_command(commands::edit_item_options),
		find_command(commands::edit_item_save_as),
		find_command(commands::edit_item_save),
		find_command(commands::view_close),
	};

	const std::vector<ui::command_ptr> test_commands =
	{
		find_command(commands::test_gen_po),
		find_command(commands::test_send_crash_report),
		find_command(commands::test_crash),
		find_command(commands::test_reset_graphics),
		find_command(commands::test_new_version),
		find_command(commands::test_run_all),
		find_command(commands::view_close),
	};

	const std::vector<ui::command_ptr> sync_commands =
	{
		find_command(commands::sync_analyze),
		find_command(commands::sync_run),
		find_command(commands::view_close),
	};

	const std::vector<ui::command_ptr> rename_commands =
	{
		find_command(commands::rename_run),
		find_command(commands::view_close),
	};

	const std::vector<ui::command_ptr> import_commands =
	{
		find_command(commands::import_analyze),
		find_command(commands::import_run),
		find_command(commands::view_close),
	};

	const std::vector<ui::command_ptr> locate_commands =
	{
		find_command(commands::locate_run),
		find_command(commands::view_close),
	};

	ui::toolbar_styles tb_styles;
	tb_styles.button_extent = {30, 40};
	_navigate1 = _app_frame->create_toolbar(tb_styles, tbButtonsNav1);
	_navigate2 = _app_frame->create_toolbar(tb_styles, tbButtonsNav2);

	tb_styles.button_extent = {40, 40};
	_navigate3 = _app_frame->create_toolbar(tb_styles, tbButtonsNav3);

	tb_styles.xTBSTYLE_LIST = true;
	tb_styles.button_extent = {0, 0};

	_tools = _app_frame->create_toolbar(tb_styles, tool_buttons);
	_sorting = _app_frame->create_toolbar(tb_styles, sorting_buttons);
	_media_edit_commands = _app_frame->create_toolbar(tb_styles, media_edit_commands);
	_rename_commands = _app_frame->create_toolbar(tb_styles, rename_commands);
	_import_commands = _app_frame->create_toolbar(tb_styles, import_commands);
	_locate_commands = _app_frame->create_toolbar(tb_styles, locate_commands);
	_sync_commands = _app_frame->create_toolbar(tb_styles, sync_commands);
	_test_commands = _app_frame->create_toolbar(tb_styles, test_commands);
}

static std::string format_items_summary(const group_by grouping, const sort_by order,
                                        const df::file_group_histogram& summary, const bool is_init_complete)
{
	std::string_view group_text;
	std::string_view sort_text;

	switch (grouping)
	{
	case group_by::presence: group_text = tt.sort_by_presence;
		break;
	case group_by::file_type: group_text = tt.sort_by_file_type;
		break;
	case group_by::shuffle: group_text = tt.sort_by_shuffle;
		break;
	case group_by::size: group_text = tt.sort_by_size;
		break;
	case group_by::extension: group_text = tt.sort_by_extension;
		break;
	case group_by::location: group_text = tt.sort_by_location;
		break;
	case group_by::rating_label: group_text = tt.sort_by_rating_label;
		break;
	case group_by::date_created: group_text = tt.prop_name_created;
		break;
	case group_by::date_modified: group_text = tt.prop_name_modified;
		break;
	case group_by::camera: group_text = tt.prop_name_camera;
		break;
	case group_by::album_show: group_text = tt.sort_by_album_show;
		break;
	case group_by::resolution: group_text = tt.sort_by_resolution;
		break;
	case group_by::folder: group_text = tt.sort_by_Folder;
		break;
	}

	switch (order)
	{
	case sort_by::def: sort_text = tt.sort_by_def;
		break;
	case sort_by::name: sort_text = tt.sort_by_name;
		break;
	case sort_by::size: sort_text = tt.sort_by_size;
		break;
	case sort_by::date_modified: sort_text = tt.prop_name_modified;
		break;
	}

	const auto total_items = summary.total_items();

	if (total_items.count > 0)
	{
		const auto total_count = str::format_count(total_items.count);
		const auto total_size = prop::format_size(total_items.size);

		if (group_text == sort_text || grouping == group_by::shuffle || sort_text.empty() || order == sort_by::def)
		{
			return std::format("{}|{} - {}", total_count, total_size, group_text);
		}

		return std::format("{}|{} - {}|{}", total_count, total_size, group_text, sort_text);
	}
	const auto total_folders = summary.total_folders();

	if (total_folders.count > 0)
	{
		const auto total_count = str::format_count(total_folders.count);

		if (group_text == sort_text || grouping == group_by::shuffle || sort_text.empty() || order == sort_by::def)
		{
			return std::format("{} {} - {}", total_count, tt.folders, group_text);
		}

		return std::format("{} {} - {}|{}", total_count, tt.folders, group_text, sort_text);
	}

	return std::string(is_init_complete ? tt.empty : tt.loading);
}

icon_index volumes_icons[5] = {
	icon_index::volume3, icon_index::volume2, icon_index::volume1, icon_index::volume0, icon_index::mute
};

void app_frame::toggle_volume()
{
	auto v = setting.media_volume;

	if (v == 0)
	{
		v = media_volumes[0];
	}
	else
	{
		for (const int volume : media_volumes)
		{
			if (v > volume)
			{
				v = volume;
				break;
			}
		}
	}

	setting.media_volume = std::clamp(v, 0, media_volume_boost);
	_commands[commands::playback_volume_toggle]->icon = sound_icon();
	_tools->update_button_state(false, false);
}

icon_index app_frame::sound_icon()
{
	const auto v = setting.media_volume;

	for (auto i = 0u; i < std::size(media_volumes); i++)
	{
		if (v >= media_volumes[i])
		{
			return volumes_icons[i];
		}
	}

	return icon_index::mute;
}


icon_index app_frame::repeat_toggle_icon()
{
	if (setting.repeat == repeat_mode::repeat_one) return icon_index::repeat_one;
	if (setting.repeat == repeat_mode::repeat_all) return icon_index::repeat_all;
	return icon_index::repeat_none;
}

bool app_frame::update_toolbar_text(const commands cc, const std::string& text)
{
	const auto command = _commands[cc];
	const auto changed = text != command->toolbar_text;

	if (changed)
	{
		command->toolbar_text = text;
		invalidate_view(view_invalid::app_layout);
	}

	return changed;
}

void app_frame::update_button_state(const bool resize)
{
	static const auto can_run_tests = known_path(platform::known_folder::test_files_folder).exists();
	static const auto now_days = platform::now().to_days();
	static const auto has_burner = platform::has_burner();

	const auto view_mode = _state.view_mode();
	const auto selection_status = _state.selection_status();
	const auto is_playing = selection_status.is_playing;
	const auto can_zoom = selection_status.can_zoom;
	const auto is_maximized = _app_frame->is_maximized();
	const auto is_edit_view = df::command_active == 0 && view_mode == view_type::edit;
	const auto is_items_view = df::command_active == 0 && view_mode == view_type::items;
	const auto is_media_or_items_view = df::command_active == 0 && (view_mode == view_type::items || view_mode ==
		view_type::media);
	const auto has_selection = is_media_or_items_view && _state.has_selection();
	const auto is_single_media_selection = is_media_or_items_view && selection_status.has_single_media_selection;
#ifndef WINSTORE
	const auto new_version_avail = is_media_or_items_view && !setting.install_updates &&
		df::version(s_app_version) < df::version(setting.available_version) && static_cast<int>(now_days) >= setting.
		min_show_update_day;
	const auto show_new_version = is_media_or_items_view && (setting.force_available_version || new_version_avail);
#endif
	const auto command_item = _state.command_item();
	const auto is_displaying_item = is_media_or_items_view && command_item;
	const auto search_has_selector = _state.search().has_selector();

#ifndef WINSTORE
	_commands[commands::info_new_version]->visible = !is_edit_view && show_new_version;
#endif
	_commands[commands::view_maximize]->visible = !is_maximized;
	_commands[commands::view_restore]->visible = is_maximized;
	_commands[commands::view_show_sidebar]->visible = !is_edit_view;
	_commands[commands::tool_test]->visible = can_run_tests;
	_commands[commands::browse_forward]->visible = !is_edit_view;

	_commands[commands::advanced_search]->enable = is_media_or_items_view;
	_commands[commands::browse_back]->enable = is_media_or_items_view && _state.history.can_browse_back();
	_commands[commands::browse_forward]->enable = is_media_or_items_view && _state.history.can_browse_forward();
	_commands[commands::browse_next_folder]->enable = is_items_view;
	_commands[commands::browse_next_group]->enable = is_items_view;
	_commands[commands::browse_next_item]->enable = is_items_view;
	_commands[commands::browse_next_item_extend]->enable = is_items_view;
	_commands[commands::browse_open_containingfolder]->enable = is_items_view && is_displaying_item;
	_commands[commands::browse_open_googlemap]->enable = _state.has_gps();
	_commands[commands::browse_open_in_file_browser]->enable = is_items_view && has_selection;
	_commands[commands::browse_parent]->enable = _state.has_parent_search() && is_items_view;
	_commands[commands::browse_previous_folder]->enable = is_items_view;
	_commands[commands::browse_previous_group]->enable = is_items_view;
	_commands[commands::browse_previous_item]->enable = is_items_view;
	_commands[commands::browse_previous_item_extend]->enable = is_items_view;
	_commands[commands::browse_recursive]->enable = is_items_view && search_has_selector;
	_commands[commands::browse_search]->enable = is_items_view;
	_commands[commands::edit_copy]->enable = has_selection;
	_commands[commands::edit_cut]->enable = has_selection;
	_commands[commands::edit_item_color_reset]->enable = is_edit_view;
	_commands[commands::edit_item_options]->enable = is_edit_view;
	_commands[commands::edit_item_save]->enable = is_edit_view;
	_commands[commands::edit_item_save_and_next]->enable = is_edit_view;
	_commands[commands::edit_item_save_and_prev]->enable = is_edit_view;
	_commands[commands::edit_item_save_as]->enable = is_edit_view;
	_commands[commands::edit_paste]->enable = is_media_or_items_view && search_has_selector;
	_commands[commands::english]->enable = true;
	_commands[commands::exit]->enable = true;
	_commands[commands::favorite]->enable = is_media_or_items_view;
	_commands[commands::filter_audio]->enable = is_items_view;
	_commands[commands::filter_photos]->enable = is_items_view;
	_commands[commands::filter_videos]->enable = is_items_view;
	_commands[commands::group_album]->enable = is_items_view;
	_commands[commands::group_camera]->enable = is_items_view;
	_commands[commands::group_created]->enable = is_items_view;
	_commands[commands::group_extension]->enable = is_items_view;
	_commands[commands::group_file_type]->enable = is_items_view;
	_commands[commands::group_folder]->enable = is_items_view;
	_commands[commands::group_location]->enable = is_items_view;
	_commands[commands::group_modified]->enable = is_items_view;
	_commands[commands::group_pixels]->enable = is_items_view;
	_commands[commands::group_presence]->enable = is_items_view;
	_commands[commands::group_rating]->enable = is_items_view;
	_commands[commands::group_shuffle]->enable = is_items_view;
	_commands[commands::group_size]->enable = is_items_view;
	_commands[commands::import_analyze]->enable = view_mode == view_type::import;
	_commands[commands::import_run]->enable = view_mode == view_type::import && _view_import && _view_import->can_run();
#ifndef WINSTORE
	_commands[commands::info_new_version]->enable = is_items_view;
#endif
	_commands[commands::keyboard]->enable = is_items_view;
	_commands[commands::label_approved]->enable = has_selection;
	_commands[commands::label_none]->enable = has_selection;
	_commands[commands::label_review]->enable = has_selection;
	_commands[commands::label_second]->enable = has_selection;
	_commands[commands::label_select]->enable = has_selection;
	_commands[commands::label_to_do]->enable = has_selection;
	_commands[commands::large_font]->enable = true;
	_commands[commands::menu_display_options]->enable = true;
	_commands[commands::menu_group]->enable = true;
	_commands[commands::menu_group_toolbar]->enable = true;
	_commands[commands::menu_language]->enable = true;
	_commands[commands::menu_main]->enable = true;
	_commands[commands::menu_navigate]->enable = true;
	_commands[commands::menu_open]->enable = has_selection;
	_commands[commands::menu_playback]->enable = true;
	_commands[commands::menu_rate_or_label]->enable = true;
	_commands[commands::menu_select]->enable = true;
	_commands[commands::menu_tag_with]->enable = has_selection;
	_commands[commands::menu_tools]->enable = true;
	_commands[commands::menu_tools_toolbar]->enable = has_selection;
	_commands[commands::option_highlight_large_items]->enable = is_media_or_items_view;
	_commands[commands::option_scale_up]->enable = is_media_or_items_view;
	_commands[commands::option_show_rotated]->enable = is_media_or_items_view;
	_commands[commands::option_show_thumbnails]->enable = is_media_or_items_view;
	_commands[commands::option_toggle_details]->enable = is_media_or_items_view;
	_commands[commands::option_toggle_item_size]->enable = is_media_or_items_view;
	_commands[commands::options_collection]->enable = is_media_or_items_view;
	_commands[commands::options_general]->enable = is_media_or_items_view;
	_commands[commands::options_sidebar]->enable = is_media_or_items_view;
	_commands[commands::pin_item]->enable = has_selection;
	_commands[commands::play]->enable = is_media_or_items_view && _state.has_display_items();
	_commands[commands::playback_auto_play]->enable = is_media_or_items_view;
	_commands[commands::playback_last_played_pos]->enable = is_media_or_items_view;
	_commands[commands::playback_menu]->enable = is_media_or_items_view;
	_commands[commands::playback_repeat_all]->enable = is_media_or_items_view;
	_commands[commands::playback_repeat_none]->enable = is_media_or_items_view;
	_commands[commands::playback_repeat_one]->enable = is_media_or_items_view;
	_commands[commands::playback_volume_toggle]->enable = is_media_or_items_view;
	_commands[commands::playback_volume0]->enable = is_media_or_items_view;
	_commands[commands::playback_volume100]->enable = is_media_or_items_view;
	_commands[commands::playback_volume200]->enable = is_media_or_items_view;
	_commands[commands::playback_volume25]->enable = is_media_or_items_view;
	_commands[commands::playback_volume50]->enable = is_media_or_items_view;
	_commands[commands::playback_volume75]->enable = is_media_or_items_view;
	_commands[commands::print]->enable = has_selection;
	_commands[commands::rate_1]->enable = has_selection;
	_commands[commands::rate_2]->enable = has_selection;
	_commands[commands::rate_3]->enable = has_selection;
	_commands[commands::rate_4]->enable = has_selection;
	_commands[commands::rate_5]->enable = has_selection;
	_commands[commands::rate_none]->enable = has_selection;
	_commands[commands::rate_rejected]->enable = has_selection;
	_commands[commands::refresh]->enable = true;
	_commands[commands::rename_run]->enable = view_mode == view_type::rename;
	_commands[commands::locate_run]->enable = view_mode == view_type::locate && _view_locate && _view_locate->can_run();
	_commands[commands::repeat_toggle]->enable = is_media_or_items_view;
	_commands[commands::search_related]->enable = is_media_or_items_view && is_displaying_item;
	_commands[commands::select_all]->enable = is_items_view;
	_commands[commands::select_invert]->enable = is_items_view;
	_commands[commands::select_nothing]->enable = is_items_view && has_selection;
	//_commands[commands::show_raw_always]->enable = is_media_or_items_view;
	_commands[commands::show_raw_preview]->enable = is_media_or_items_view;
	//_commands[commands::show_raw_this_only]->enable = is_media_or_items_view;
	_commands[commands::sort_date_modified]->enable = is_media_or_items_view;
	_commands[commands::sort_dates_ascending]->enable = is_media_or_items_view;
	_commands[commands::sort_dates_descending]->enable = is_media_or_items_view;
	_commands[commands::sort_def]->enable = is_media_or_items_view;
	_commands[commands::sort_name]->enable = is_media_or_items_view;
	_commands[commands::sort_size]->enable = is_media_or_items_view;
	_commands[commands::sync_analyze]->enable = view_mode == view_type::sync;
	_commands[commands::sync_run]->enable = view_mode == view_type::sync && _view_sync && _view_sync->can_run();
	_commands[commands::test_crash]->enable = view_mode == view_type::test;
	_commands[commands::test_send_crash_report]->enable = view_mode == view_type::test;
	_commands[commands::test_gen_po]->enable = view_mode == view_type::test;
	_commands[commands::test_reset_graphics]->enable = view_mode == view_type::test;
	_commands[commands::test_new_version]->enable = view_mode == view_type::test;
	_commands[commands::test_run_all]->enable = view_mode == view_type::test;
	_commands[commands::tool_adjust_date]->enable = has_selection;
	_commands[commands::tool_burn]->enable = has_selection && has_burner;
	_commands[commands::tool_convert]->enable = is_items_view && _state.can_process_selection(
		_view_frame, df::process_items_type::photos_only);
	_commands[commands::tool_copy_to_folder]->enable = has_selection;
	_commands[commands::tool_delete]->enable = has_selection;
	_commands[commands::tool_desktop_background]->enable = is_media_or_items_view && selection_status.showing_image;
	_commands[commands::tool_edit]->enable = is_media_or_items_view && _state.can_edit_media();
	_commands[commands::tool_edit_metadata]->enable = has_selection;
	_commands[commands::tool_eject]->enable = is_items_view;
	_commands[commands::tool_email]->enable = has_selection;
	_commands[commands::tool_file_properties]->enable = has_selection;
	_commands[commands::tool_import]->enable = is_media_or_items_view;
	_commands[commands::tool_locate]->enable = has_selection;
	_commands[commands::tool_move_to_folder]->enable = has_selection;
	_commands[commands::tool_new_folder]->enable = is_items_view && search_has_selector;
	_commands[commands::tool_open_with]->enable = has_selection;
	_commands[commands::tool_remove_metadata]->enable = has_selection;
	_commands[commands::tool_rename]->enable = has_selection;
	_commands[commands::tool_rotate_anticlockwise]->enable = has_selection;
	_commands[commands::tool_rotate_clockwise]->enable = has_selection;
	_commands[commands::tool_rotate_reset]->enable = is_edit_view;
	_commands[commands::tool_save_current_video_frame]->enable = is_single_media_selection;
	_commands[commands::tool_scan]->enable = is_media_or_items_view;
	_commands[commands::tool_sync]->enable = is_media_or_items_view;
	_commands[commands::tool_tag]->enable = has_selection;
	_commands[commands::tool_test]->enable = is_media_or_items_view;
	_commands[commands::verbose_metadata]->enable = is_items_view;
	_commands[commands::view_close]->enable = !is_media_or_items_view;
	_commands[commands::view_favorite_tags]->enable = is_media_or_items_view;
	_commands[commands::view_fullscreen]->enable = is_media_or_items_view;
	_commands[commands::view_help]->enable = true;
	_commands[commands::view_items]->enable = !is_items_view;
	_commands[commands::view_maximize]->enable = true;
	_commands[commands::view_minimize]->enable = true;
	_commands[commands::view_restore]->enable = true;
	_commands[commands::view_show_sidebar]->enable = is_media_or_items_view;
	_commands[commands::view_zoom]->enable = is_media_or_items_view && can_zoom;


	_commands[commands::playback_auto_play]->checked = setting.auto_play;
	_commands[commands::playback_last_played_pos]->checked = setting.last_played_pos;
	_commands[commands::playback_repeat_one]->checked = setting.repeat == repeat_mode::repeat_one;
	_commands[commands::playback_repeat_all]->checked = setting.repeat == repeat_mode::repeat_all;
	_commands[commands::playback_repeat_none]->checked = setting.repeat == repeat_mode::repeat_none;
	_commands[commands::play]->checked = is_playing;

	_commands[commands::pin_item]->checked = _state.has_pin();
	_commands[commands::option_highlight_large_items]->checked = setting.highlight_large_items;
	_commands[commands::sort_dates_descending]->checked = setting.sort_dates_descending;
	_commands[commands::sort_dates_ascending]->checked = !setting.sort_dates_descending;
	_commands[commands::view_show_sidebar]->checked = setting.show_sidebar;
	_commands[commands::option_scale_up]->checked = setting.scale_up;
	_commands[commands::option_show_rotated]->checked = setting.show_rotated;
	_commands[commands::verbose_metadata]->checked = setting.verbose_metadata;
	_commands[commands::show_raw_preview]->checked = setting.raw_preview;
	_commands[commands::test_run_all]->checked = is_running_tests();
	_commands[commands::view_items]->checked = view_mode == view_type::items;
	_commands[commands::option_show_thumbnails]->checked = view_mode == view_type::items;
	_commands[commands::browse_recursive]->checked = _state.search().has_recursive_selector();
	_commands[commands::filter_photos]->checked = _state.filter().has_group(file_group::photo);
	_commands[commands::filter_videos]->checked = _state.filter().has_group(file_group::video);
	_commands[commands::filter_audio]->checked = _state.filter().has_group(file_group::audio);
	_commands[commands::large_font]->checked = setting.large_font;
	_commands[commands::view_fullscreen]->checked = _state.is_full_screen;
	_commands[commands::option_toggle_details]->checked = toggle_details_state;
	_commands[commands::view_favorite_tags]->checked = setting.sidebar.show_favorite_tags_only;

	_commands[commands::group_album]->checked = _state.group_order() == group_by::album_show;
	_commands[commands::group_camera]->checked = _state.group_order() == group_by::camera;
	_commands[commands::group_created]->checked = _state.group_order() == group_by::date_created;
	_commands[commands::group_presence]->checked = _state.group_order() == group_by::presence;
	_commands[commands::group_file_type]->checked = _state.group_order() == group_by::file_type;
	_commands[commands::group_location]->checked = _state.group_order() == group_by::location;
	_commands[commands::group_modified]->checked = _state.group_order() == group_by::date_modified;
	_commands[commands::group_pixels]->checked = _state.group_order() == group_by::resolution;
	_commands[commands::group_rating]->checked = _state.group_order() == group_by::rating_label;
	_commands[commands::group_shuffle]->checked = _state.group_order() == group_by::shuffle;
	_commands[commands::group_size]->checked = _state.group_order() == group_by::size;
	_commands[commands::group_extension]->checked = _state.group_order() == group_by::extension;
	_commands[commands::group_folder]->checked = _state.group_order() == group_by::folder;
	_commands[commands::sort_def]->checked = _state.sort_order() == sort_by::def;
	_commands[commands::sort_name]->checked = _state.sort_order() == sort_by::name;
	_commands[commands::sort_size]->checked = _state.sort_order() == sort_by::size;
	_commands[commands::sort_date_modified]->checked = _state.sort_order() == sort_by::date_modified;
	_commands[commands::english]->checked = setting.language == "en";


	_commands[commands::playback_volume200]->checked = setting.media_volume == media_volume_boost;
	_commands[commands::playback_volume100]->checked = setting.media_volume == media_volumes[0];
	_commands[commands::playback_volume75]->checked = setting.media_volume == media_volumes[1];
	_commands[commands::playback_volume50]->checked = setting.media_volume == media_volumes[2];
	_commands[commands::playback_volume25]->checked = setting.media_volume == media_volumes[3];
	_commands[commands::playback_volume0]->checked = setting.media_volume == media_volumes[4];

	_commands[commands::play]->icon = is_playing ? icon_index::pause : icon_index::play;
	_commands[commands::view_fullscreen]->icon = _state.is_full_screen
		                                             ? icon_index::fullscreen_exit
		                                             : icon_index::fullscreen;
	_commands[commands::playback_volume_toggle]->icon = sound_icon();
	_commands[commands::repeat_toggle]->icon = repeat_toggle_icon();
	_commands[commands::favorite]->icon = _state.search_is_favorite() ? icon_index::star_solid : icon_index::star;
	_commands[commands::options_collection]->icon = _state.search_is_in_collection()
		                                                ? icon_index::set_solid
		                                                : icon_index::set;


	const auto summary_text = format_items_summary(_state.group_order(), _state.sort_order(), _state.summary_shown(),
	                                               _state.item_index.is_init_complete());

	auto toolbar_text_changed = update_toolbar_text(commands::menu_group_toolbar, summary_text);
	toolbar_text_changed |= update_toolbar_text(commands::filter_photos,
	                                            str::format_count(_state.count_total(file_group::photo), true));
	toolbar_text_changed |= update_toolbar_text(commands::filter_videos,
	                                            str::format_count(_state.count_total(file_group::video), true));
	toolbar_text_changed |= update_toolbar_text(commands::filter_audio,
	                                            str::format_count(_state.count_total(file_group::audio), true));

	_tools->update_button_state(resize, false);
	_sorting->update_button_state(resize, toolbar_text_changed);
	_navigate1->update_button_state(resize, false);
	_navigate2->update_button_state(resize, false);
	_navigate3->update_button_state(resize, false);

	_media_edit_commands->update_button_state(resize, false);
	_import_commands->update_button_state(resize, false);
	_locate_commands->update_button_state(resize, false);
	_rename_commands->update_button_state(resize, false);
	_sync_commands->update_button_state(resize, false);
	_test_commands->update_button_state(resize, false);

	const view_element_event e{view_element_event_type::update_command_state, _view_frame};
	_view->broadcast_event(e);
}

void app_frame::update_address() const
{
	const auto& search = _state.search();
	auto icon = icon_index::search;
	std::string text;

	switch (_state.view_mode())
	{
	case view_type::test:
		text = "Testing";
		icon = icon_index::check;
		break;
	case view_type::media:
	case view_type::items:
		if (search.has_selector()) icon = icon_index::folder;
		else if (search.has_terms()) icon = search.first_type()->icon;
		text = search.text();
		break;
	case view_type::edit:
		text = _state._edit_item ? _state._edit_item->path().str() : std::string{};
		icon = _state.displayed_item_icon();
		break;
	}

	if (icon == icon_index::star)
	{
		icon = icon_index::search;
	}

	if (!_search_has_focus)
	{
		_search_edit->window_text(text);
		_search_edit->set_icon(icon);
	}
}

recti app_frame::calc_search_popup_bounds() const
{
	const auto edit_bounds = _search_edit->window_bounds();
	const auto height = _search_predictions_frame ? _search_predictions_frame->height() : 320;
	return {edit_bounds.left + 8, edit_bounds.bottom, edit_bounds.right - 8, edit_bounds.bottom + height};
}


void app_frame::update_command_text()
{
	def_command(commands::tool_adjust_date, command_group::tools, icon_index::time, tt.command_adjust_date);
	def_command(commands::tool_edit_metadata, command_group::tools, icon_index::edit_metadata,
	            tt.command_edit_metadata);
	def_command(commands::exit, command_group::help, icon_index::close, tt.command_app_exit);
	def_command(commands::playback_auto_play, command_group::media_playback, icon_index::play, tt.command_autoplay);
	def_command(commands::playback_last_played_pos, command_group::media_playback, icon_index::none,
	            tt.command_last_played_pos);
	def_command(commands::browse_back, command_group::navigation, icon_index::back, tt.command_browse_back);
	def_command(commands::browse_forward, command_group::navigation, icon_index::next, tt.command_browse_forward);
	def_command(commands::browse_next_folder, command_group::navigation, icon_index::next_folder,
	            tt.command_browse_next_folder);
	def_command(commands::browse_next_group, command_group::selection, icon_index::none, tt.command_browse_next_group);
	def_command(commands::browse_next_item, command_group::selection, icon_index::next_image,
	            tt.command_browse_next_item);
	def_command(commands::browse_next_item_extend, command_group::selection, icon_index::next_image,
	            tt.command_browse_next_item_extend);
	def_command(commands::browse_parent, command_group::navigation, icon_index::parent, tt.command_browse_parent);
	def_command(commands::browse_previous_folder, command_group::navigation, icon_index::back_folder,
	            tt.command_browse_previous_folder);
	def_command(commands::browse_previous_group, command_group::selection, icon_index::none,
	            tt.command_browse_previous_group);
	def_command(commands::browse_previous_item, command_group::selection, icon_index::back_image,
	            tt.command_browse_previous_item);
	def_command(commands::browse_previous_item_extend, command_group::selection, icon_index::back_image,
	            tt.command_browse_previous_item_extend);
	def_command(commands::tool_burn, command_group::tools, icon_index::disk, tt.command_burn);
	def_command(commands::tool_save_current_video_frame, command_group::tools, icon_index::none, tt.command_capture);
	def_command(commands::view_close, command_group::none, icon_index::close, tt.command_close);
	def_command(commands::edit_item_color_reset, command_group::none, icon_index::undo, tt.command_color_reset,
	            tt.tooltip_color_reset);
	def_command(commands::tool_convert, command_group::tools, icon_index::convert, tt.command_convert_or_resize);
	def_command(commands::tool_copy_to_folder, command_group::file_management, icon_index::copy_to_folder,
	            tt.command_copy);
	def_command(commands::tool_delete, command_group::file_management, icon_index::cancel, tt.command_delete);
	def_command(commands::tool_desktop_background, command_group::tools, icon_index::wallpaper,
	            tt.command_desktop_background);
	def_command(commands::menu_display_options, command_group::none, icon_index::none, tt.command_display_options);
	def_command(commands::tool_edit, command_group::tools, icon_index::edit, tt.command_edit,
	            std::format("{}\n{}", tt.tooltip_edit1, tt.tooltip_edit2));
	def_command(commands::edit_copy, command_group::file_management, icon_index::edit_copy, tt.command_edit_copy);
	def_command(commands::edit_cut, command_group::file_management, icon_index::edit_cut, tt.command_edit_cut);
	def_command(commands::edit_paste, command_group::file_management, icon_index::edit_paste, tt.command_edit_paste);
	def_command(commands::tool_eject, command_group::file_management, icon_index::eject, tt.command_eject);
	def_command(commands::tool_file_properties, command_group::file_management, icon_index::none,
	            tt.command_file_properties);
	def_command(commands::browse_search, command_group::navigation, icon_index::search, tt.command_file_search);
	def_command(commands::browse_recursive, command_group::navigation, icon_index::recursive, tt.command_flatten);
	def_command(commands::view_fullscreen, command_group::media_playback, icon_index::fullscreen, tt.command_fullscreen,
	            tt.tooltip_fullscreen);
	def_command(commands::option_highlight_large_items, command_group::options, icon_index::none,
	            tt.command_highlight_large_items);
	def_command(commands::tool_import, command_group::tools, icon_index::import, tt.command_import);
	def_command(commands::tool_sync, command_group::tools, icon_index::sync, tt.command_sync);
	def_command(commands::options_collection, command_group::options, icon_index::set, tt.command_collection_options);
	def_command(commands::keyboard, command_group::help, icon_index::keyboard, tt.command_keyboard);
	def_command(commands::tool_locate, command_group::tools, icon_index::location, tt.command_locate);
	def_command(commands::view_maximize, command_group::help, icon_index::maximize, tt.command_maximize);
	def_command(commands::view_minimize, command_group::help, icon_index::minimize, tt.command_minimize);
	def_command(commands::tool_move_to_folder, command_group::file_management, icon_index::move_to_folder,
	            tt.command_move);
	def_command(commands::view_show_sidebar, command_group::options, icon_index::navigation, tt.command_nav_bar,
	            tt.tooltip_nav_bar);
	def_command(commands::menu_navigate, command_group::none, icon_index::none, tt.command_navigate);
	def_command(commands::tool_new_folder, command_group::file_management, icon_index::new_folder,
	            tt.command_new_folder);
#ifndef WINSTORE
	def_command(commands::info_new_version, command_group::none, icon_index::lightbulb, tt.command_new_version);
#endif
	def_command(commands::menu_open, command_group::none, icon_index::open_one, tt.command_open, tt.tooltip_open);
	def_command(commands::browse_open_containingfolder, command_group::navigation, icon_index::none,
	            tt.command_show_in_folder);
	def_command(commands::browse_open_googlemap, command_group::open, icon_index::location, tt.command_open_google_map);
	def_command(commands::browse_open_in_file_browser, command_group::open, icon_index::none,
	            tt.command_show_in_file_browser);
	def_command(commands::tool_open_with, command_group::open, icon_index::open_one, tt.command_open_with);
	def_command(commands::options_general, command_group::options, icon_index::settings, tt.command_options);
	def_command(commands::pin_item, command_group::selection, icon_index::pin, tt.command_pin, tt.tooltip_pin);
	def_command(commands::play, command_group::media_playback, icon_index::play, tt.command_play, tt.tooltip_play);
	def_command(commands::print, command_group::tools, icon_index::print, tt.command_print);
	def_command(commands::rate_none, command_group::rate_flag, icon_index::none, tt.command_rate_0);
	def_command(commands::rate_1, command_group::rate_flag, icon_index::none, tt.command_rate_1);
	def_command(commands::rate_2, command_group::rate_flag, icon_index::none, tt.command_rate_2);
	def_command(commands::rate_3, command_group::rate_flag, icon_index::none, tt.command_rate_3);
	def_command(commands::rate_4, command_group::rate_flag, icon_index::none, tt.command_rate_4);
	def_command(commands::rate_5, command_group::rate_flag, icon_index::none, tt.command_rate_5);
	def_command(commands::rate_rejected, command_group::rate_flag, icon_index::none, tt.command_rate_rejected);
	def_command(commands::label_approved, command_group::rate_flag, icon_index::none, tt.command_label_approved);
	def_command(commands::label_to_do, command_group::rate_flag, icon_index::none, tt.command_label_to_do);
	def_command(commands::label_select, command_group::rate_flag, icon_index::none, tt.command_label_select);
	def_command(commands::label_review, command_group::rate_flag, icon_index::none, tt.command_label_review);
	def_command(commands::label_second, command_group::rate_flag, icon_index::none, tt.command_label_second);
	def_command(commands::label_none, command_group::rate_flag, icon_index::none, tt.command_label_none);
	def_command(commands::refresh, command_group::navigation, icon_index::refresh, tt.command_refresh);
	def_command(commands::search_related, command_group::tools, icon_index::compare, tt.command_related);
	def_command(commands::tool_rename, command_group::file_management, icon_index::rename, tt.command_rename);
	def_command(commands::playback_repeat_none, command_group::options, icon_index::repeat_none, tt.command_repeat_none,
	            tt.repeat_off_help);
	def_command(commands::playback_repeat_one, command_group::options, icon_index::repeat_one, tt.command_repeat_one,
	            tt.repeat_one_help);
	def_command(commands::playback_repeat_all, command_group::options, icon_index::repeat_all, tt.command_repeat_all,
	            tt.repeat_help);
	def_command(commands::playback_menu, command_group::media_playback, icon_index::media_options,
	            tt.command_playback_menu);
	def_command(commands::menu_playback, command_group::media_playback, icon_index::media_options,
	            tt.command_playback_toolbar, tt.command_playback_menu);
	def_command(commands::repeat_toggle, command_group::media_playback, icon_index::repeat_all,
	            tt.command_repeat_toggle);
	def_command(commands::view_restore, command_group::help, icon_index::restore, tt.command_restore);
	def_command(commands::tool_rotate_anticlockwise, command_group::tools, icon_index::rotate_anticlockwise,
	            tt.command_rotate_anticlockwise);
	def_command(commands::tool_rotate_clockwise, command_group::tools, icon_index::rotate_clockwise,
	            tt.command_rotate_clockwise);
	def_command(commands::tool_rotate_reset, command_group::none, icon_index::undo, tt.command_rotate_reset,
	            tt.tooltip_rotate_reset);
	def_command(commands::edit_item_save, command_group::edit_item, icon_index::save, tt.command_save);
	def_command(commands::edit_item_save_and_prev, command_group::edit_item, icon_index::back_image,
	            tt.command_save_and_back, tt.command_save_and_back_tooltip);
	def_command(commands::edit_item_save_and_next, command_group::edit_item, icon_index::next_image,
	            tt.command_save_and_next, tt.command_save_and_next_tooltip);
	def_command(commands::edit_item_save_as, command_group::edit_item, icon_index::save_copy, tt.command_save_as);
	def_command(commands::edit_item_options, command_group::edit_item, icon_index::settings, tt.command_save_options);
	def_command(commands::option_scale_up, command_group::options, icon_index::fit, tt.command_scale_up,
	            tt.tooltip_scale_up);
	def_command(commands::tool_scan, command_group::tools, icon_index::scan, tt.command_scan);
	def_command(commands::options_sidebar, command_group::options, icon_index::none, tt.command_customise);
	def_command(commands::select_all, command_group::selection, icon_index::none, tt.command_select_all);
	def_command(commands::select_invert, command_group::selection, icon_index::none, tt.command_select_invert);
	def_command(commands::select_nothing, command_group::selection, icon_index::none, tt.command_select_nothing);
	def_command(commands::tool_email, command_group::tools, icon_index::mail, tt.command_share_email);
	def_command(commands::option_show_thumbnails, command_group::options, icon_index::items,
	            tt.command_show_thumbnails);
	def_command(commands::option_show_rotated, command_group::options, icon_index::orientation, tt.item_oriented);
	def_command(commands::verbose_metadata, command_group::options, icon_index::verbose_metadata,
	            tt.show_verbose_metadata);
	def_command(commands::show_raw_preview, command_group::options, icon_index::preview, tt.preview_show_preview);
	def_command(commands::tool_tag, command_group::tools, icon_index::tag, tt.prop_name_tag);
	def_command(commands::menu_tag_with, command_group::none, icon_index::tag, tt.prop_name_tag, tt.tooltip_tag_with);
	def_command(commands::menu_language, command_group::none, icon_index::language, tt.command_language,
	            tt.tooltip_language);
	def_command(commands::english, command_group::none, icon_index::none, "English", "English language");
	def_command(commands::option_toggle_details, command_group::options, icon_index::details, tt.command_toggle_details,
	            tt.tooltip_toggle_details_all);
	def_command(commands::option_toggle_item_size, command_group::options, icon_index::zoom_in,
	            tt.command_toggle_item_size);
	def_command(commands::menu_tools_toolbar, command_group::none, icon_index::tools, tt.command_tools,
	            tt.tooltip_tools);
	def_command(commands::menu_tools, command_group::none, icon_index::tools, tt.command_tools, tt.tooltip_tools);
	def_command(commands::view_help, command_group::help, icon_index::question, tt.command_view_help);
	def_command(commands::view_items, command_group::options, icon_index::items, tt.command_view_items);
	def_command(commands::large_font, command_group::options, icon_index::none, tt.command_view_large_font);
	def_command(commands::view_favorite_tags, command_group::options, icon_index::tag, tt.command_favorite_tags);
	def_command(commands::menu_main, command_group::none, icon_index::more, tt.command_view_menu, tt.tooltip_view_menu);
	def_command(commands::menu_rate_or_label, command_group::none, icon_index::none, tt.command_view_rate_label);
	def_command(commands::menu_select, command_group::none, icon_index::none, tt.command_view_select);
	def_command(commands::menu_group_toolbar, command_group::none, icon_index::group, tt.command_view_sort);
	def_command(commands::filter_photos, command_group::selection, icon_index::photo, tt.command_filter_photos);
	def_command(commands::filter_videos, command_group::selection, icon_index::video, tt.command_filter_videos);
	def_command(commands::filter_audio, command_group::selection, icon_index::audio, tt.command_filter_audio);
	def_command(commands::menu_group, command_group::none, icon_index::group, tt.command_menu_group_sort);
	def_command(commands::playback_volume200, command_group::media_playback, icon_index::volume3, tt.command_volume200);
	def_command(commands::playback_volume100, command_group::media_playback, icon_index::volume3, tt.command_volume100);
	def_command(commands::playback_volume75, command_group::media_playback, icon_index::volume2, tt.command_volume75);
	def_command(commands::playback_volume50, command_group::media_playback, icon_index::volume1, tt.command_volume50);
	def_command(commands::playback_volume25, command_group::media_playback, icon_index::volume0, tt.command_volume25);
	def_command(commands::playback_volume0, command_group::media_playback, icon_index::mute, tt.command_volume0);
	def_command(commands::playback_volume_toggle, command_group::media_playback, icon_index::volume3,
	            tt.command_toggle_volume);
	def_command(commands::view_zoom, command_group::media_playback, icon_index::zoom_in, tt.command_zoom);
	def_command(commands::favorite, command_group::navigation, icon_index::star, tt.command_favorite);
	def_command(commands::advanced_search, command_group::navigation, icon_index::search, tt.command_advanced_search);
	def_command(commands::group_album, command_group::group_by, icon_index::none, tt.command_group_album);
	def_command(commands::group_presence, command_group::group_by, icon_index::none, tt.command_group_presence);
	def_command(commands::group_camera, command_group::group_by, icon_index::none, tt.command_group_camera);
	def_command(commands::group_created, command_group::group_by, icon_index::none, tt.command_group_created);
	def_command(commands::group_file_type, command_group::group_by, icon_index::none, tt.command_group_file_type);
	def_command(commands::group_location, command_group::group_by, icon_index::none, tt.command_group_location);
	def_command(commands::group_modified, command_group::group_by, icon_index::none, tt.command_group_modified);
	def_command(commands::group_pixels, command_group::group_by, icon_index::none, tt.command_group_resolution);
	def_command(commands::group_rating, command_group::group_by, icon_index::none, tt.command_group_rating);
	def_command(commands::group_shuffle, command_group::group_by, icon_index::none, tt.command_group_shuffle);
	def_command(commands::group_size, command_group::group_by, icon_index::none, tt.command_group_size);
	def_command(commands::group_extension, command_group::group_by, icon_index::none, tt.command_group_extension);
	def_command(commands::group_folder, command_group::group_by, icon_index::none, tt.command_group_folder);
	def_command(commands::group_toggle, command_group::group_by, icon_index::none, tt.command_toggle_group_by);
	def_command(commands::sort_dates_descending, command_group::options, icon_index::none,
	            tt.command_sort_dates_descending);
	def_command(commands::sort_dates_ascending, command_group::options, icon_index::none,
	            tt.command_sort_dates_ascending);
	def_command(commands::sort_name, command_group::sort_by, icon_index::none, tt.command_sort_name);
	def_command(commands::sort_size, command_group::sort_by, icon_index::none, tt.command_sort_size);
	def_command(commands::sort_def, command_group::sort_by, icon_index::none, tt.command_sort_def);
	def_command(commands::sort_date_modified, command_group::sort_by, icon_index::none, tt.command_sort_date_modified);
	def_command(commands::sync_analyze, command_group::none, icon_index::refresh, tt.analyze);
	def_command(commands::sync_run, command_group::none, icon_index::play, tt.command_sync);
	def_command(commands::rename_run, command_group::none, icon_index::play, tt.command_rename_files);
	def_command(commands::import_analyze, command_group::none, icon_index::refresh, tt.analyze);
	def_command(commands::import_run, command_group::none, icon_index::play, tt.command_import);
	def_command(commands::locate_run, command_group::none, icon_index::play, tt.command_locate);

	def_command(commands::test_gen_po, command_group::none, icon_index::language, "Generate language files");
	def_command(commands::test_reset_graphics, command_group::none, icon_index::screen, "Reset graphics");
	def_command(commands::test_send_crash_report, command_group::none, icon_index::download, "Send crash report");
	def_command(commands::test_crash, command_group::none, icon_index::error, "Crash!");
	def_command(commands::tool_test, command_group::none, icon_index::check, "Tests", "Show test view");
	def_command(commands::test_new_version, command_group::none, icon_index::lightbulb, "Test new version");
	def_command(commands::test_run_all, command_group::none, icon_index::play, "Run tests");

	_commands[commands::browse_previous_item]->keyboard_accelerator_text = tt.keyboard_left;
	_commands[commands::browse_next_item]->keyboard_accelerator_text = tt.keyboard_right;

	constexpr auto control = keyboard_accelerator_t::control;
	constexpr auto shift = keyboard_accelerator_t::shift;
	constexpr auto alt = keyboard_accelerator_t::alt;

	_commands[commands::rate_none]->kba.emplace_back('0');
	_commands[commands::rate_1]->kba.emplace_back('1');
	_commands[commands::rate_2]->kba.emplace_back('2');
	_commands[commands::rate_3]->kba.emplace_back('3');
	_commands[commands::rate_4]->kba.emplace_back('4');
	_commands[commands::rate_5]->kba.emplace_back('5');
	_commands[commands::rate_rejected]->kba.emplace_back(keys::DEL, alt);
	_commands[commands::label_select]->kba.emplace_back('6');
	_commands[commands::label_second]->kba.emplace_back('7');
	_commands[commands::label_approved]->kba.emplace_back('8');
	_commands[commands::label_review]->kba.emplace_back('9');

	_commands[commands::pin_item]->kba.emplace_back('A');
	_commands[commands::select_all]->kba.emplace_back('A', control);
	_commands[commands::tool_edit_metadata]->kba.emplace_back('E', control);
	_commands[commands::tool_copy_to_folder]->kba.emplace_back('C', shift | control);
	_commands[commands::edit_copy]->kba.emplace_back('C', control);
	_commands[commands::select_invert]->kba.emplace_back('D', control);
	_commands[commands::tool_adjust_date]->kba.emplace_back('D', shift | control);
	_commands[commands::tool_edit]->kba.emplace_back('E');
	_commands[commands::tool_eject]->kba.emplace_back('E', shift | control);
	_commands[commands::browse_search]->kba.emplace_back('F', control);
	_commands[commands::option_toggle_details]->kba.emplace_back('K');
	_commands[commands::tool_locate]->kba.emplace_back('L');
	_commands[commands::repeat_toggle]->kba.emplace_back('L', control);
	_commands[commands::rate_rejected]->kba.emplace_back('M');
	_commands[commands::tool_new_folder]->kba.emplace_back('N', control);
	_commands[commands::view_items]->kba.emplace_back('N', shift | control);
	_commands[commands::browse_open_containingfolder]->kba.emplace_back('O', shift | control);
	_commands[commands::label_to_do]->kba.emplace_back('P');
	_commands[commands::search_related]->kba.emplace_back('R');
	_commands[commands::tool_tag]->kba.emplace_back('T');
	_commands[commands::tool_save_current_video_frame]->kba.emplace_back('S', control);
	_commands[commands::select_nothing]->kba.emplace_back('U', control);
	_commands[commands::edit_paste]->kba.emplace_back('V', control);
	_commands[commands::edit_cut]->kba.emplace_back('X', control);
	_commands[commands::tool_move_to_folder]->kba.emplace_back('X', shift | control);
	_commands[commands::browse_back]->kba.emplace_back(keys::BACK);
	_commands[commands::tool_delete]->kba.emplace_back(keys::DEL);
	_commands[commands::edit_cut]->kba.emplace_back(keys::DEL, shift);
	_commands[commands::group_toggle]->kba.emplace_back(keys::DOWN, alt);
	_commands[commands::group_file_type]->kba.emplace_back('K', control);
	_commands[commands::group_created]->kba.emplace_back('K', control | shift);

	_commands[commands::view_help]->kba.emplace_back(keys::F1);
	_commands[commands::keyboard]->kba.emplace_back(keys::F1, control);
	_commands[commands::tool_rename]->kba.emplace_back(keys::F2);
	_commands[commands::browse_search]->kba.emplace_back(keys::F3);
	_commands[commands::browse_search]->kba.emplace_back(keys::BROWSER_SEARCH);
	_commands[commands::advanced_search]->kba.emplace_back(keys::F3, control);
	_commands[commands::view_show_sidebar]->kba.emplace_back(keys::F4);
	_commands[commands::view_show_sidebar]->kba.emplace_back(keys::BROWSER_FAVORITES);
	_commands[commands::refresh]->kba.emplace_back(keys::F5);
	_commands[commands::options_general]->kba.emplace_back(keys::F6);
	_commands[commands::options_sidebar]->kba.emplace_back(keys::F6, shift | control);
	_commands[commands::options_collection]->kba.emplace_back(keys::F6, control);
	_commands[commands::playback_auto_play]->kba.emplace_back(keys::F7, control);
	_commands[commands::playback_volume_toggle]->kba.emplace_back(keys::F7);
	_commands[commands::tool_convert]->kba.emplace_back(keys::F8);
	_commands[commands::tool_import]->kba.emplace_back(keys::F9);
	_commands[commands::browse_recursive]->kba.emplace_back(keys::F9, control);
	_commands[commands::tool_sync]->kba.emplace_back(keys::F9, shift | control);
	_commands[commands::tool_email]->kba.emplace_back(keys::F10);
	_commands[commands::option_scale_up]->kba.emplace_back(keys::F11, shift | control);
	_commands[commands::view_fullscreen]->kba.emplace_back(keys::F11);
	_commands[commands::view_fullscreen]->kba.emplace_back(keys::SPACE, shift | control);
	_commands[commands::option_show_thumbnails]->kba.emplace_back(keys::F11, shift);
	_commands[commands::edit_copy]->kba.emplace_back(keys::INSERT, control);
	_commands[commands::edit_paste]->kba.emplace_back(keys::INSERT, shift);
	_commands[commands::browse_back]->kba.emplace_back(keys::LEFT, alt);
	_commands[commands::browse_previous_folder]->kba.emplace_back(keys::LEFT, alt | control);
	_commands[commands::browse_previous_item_extend]->kba.emplace_back(keys::LEFT, control);
	_commands[commands::tool_rotate_anticlockwise]->kba.emplace_back(keys::OEM_4);
	_commands[commands::tool_rotate_clockwise]->kba.emplace_back(keys::OEM_6);
	_commands[commands::option_toggle_item_size]->kba.emplace_back(keys::OEM_PLUS, control);
	_commands[commands::large_font]->kba.emplace_back(keys::OEM_PLUS, shift | control);
	_commands[commands::tool_file_properties]->kba.emplace_back(keys::RETURN, shift | control);
	_commands[commands::browse_open_in_file_browser]->kba.emplace_back(keys::RETURN, shift);
	_commands[commands::tool_open_with]->kba.emplace_back(keys::RETURN, control);
	_commands[commands::view_zoom]->kba.emplace_back(keys::SPACE, control);
	_commands[commands::play]->kba.emplace_back(keys::SPACE);
	_commands[commands::browse_forward]->kba.emplace_back(keys::RIGHT, alt);
	_commands[commands::browse_next_folder]->kba.emplace_back(keys::RIGHT, alt | control);
	_commands[commands::browse_next_item_extend]->kba.emplace_back(keys::RIGHT, control);
	_commands[commands::browse_parent]->kba.emplace_back(keys::UP, alt);
	_commands[commands::browse_previous_group]->kba.emplace_back(keys::PRIOR);
	_commands[commands::browse_next_group]->kba.emplace_back(keys::NEXT);

	_commands[commands::edit_item_save_and_prev]->kba.emplace_back(keys::LEFT, alt);
	_commands[commands::edit_item_save_and_next]->kba.emplace_back(keys::RIGHT, alt);
	_commands[commands::edit_item_save]->kba.emplace_back(keys::RETURN, alt);
	_commands[commands::view_close]->kba.emplace_back(keys::ESCAPE);

	for (const auto& c : _commands)
	{
		c.second->keyboard_accelerator_text = format_keyboard_accelerator(c.second->kba);
	}
}


command_info_ptr app_frame::find_or_create_command_info(const commands id)
{
	const auto found = _commands.find(id);
	if (found != _commands.end()) return found->second;
	return _commands[id] = std::make_shared<ui::command>();
}

void app_frame::add_command_invoke(const commands id, std::function<void()> invoke)
{
	find_or_create_command_info(id)->invoke = std::move(invoke);
}

void app_frame::def_command(const commands id, const command_group group, const icon_index icon,
                            const std::string_view text, const std::string_view tooltip)
{
	const auto c = find_or_create_command_info(id);
	c->group = group;
	c->icon = icon;
	c->text = text;
	c->tooltip_text = tooltip;
	c->kba.clear();
	c->keyboard_accelerator_text.clear();
}

ui::command_ptr app_frame::find_command(const commands id) const
{
	const auto it = _commands.find(id);

	if (it != _commands.cend())
	{
		return it->second;
	}

	return nullptr;
}

void app_frame::tooltip(view_hover_element& hover, const commands id) const
{
	const auto command = find_command(id);

	if (command)
	{
		if (command->icon != icon_index::none)
		{
			hover.elements->add(make_icon_element(command->icon, view_element_style::no_break));
		}

		if (!command->text.empty())
		{
			hover.elements->add(std::make_shared<text_element>(command->text, ui::style::font_face::dialog,
			                                                   ui::style::text_style::multiline,
			                                                   view_element_style::line_break));
		}

		if (!command->tooltip_text.empty())
		{
			hover.elements->add(std::make_shared<text_element>(command->tooltip_text, ui::style::font_face::dialog,
			                                                   ui::style::text_style::multiline,
			                                                   view_element_style::line_break));
		}
	}

	std::string keyboard_accelerator;

	if (id == commands::browse_previous_item || id == commands::browse_next_item)
	{
		const auto forward = id == commands::browse_next_item;
		const auto i = _state.next_item(forward, false);

		if (i)
		{
			const auto& image = i->thumbnail();

			if (is_valid(image))
			{
				files ff;
				const auto surface = ff.image_to_surface(image);

				if (is_valid(surface))
				{
					hover.elements->add(
						std::make_shared<surface_element>(surface, 200,
						                                  view_element_style::center | view_element_style::new_line));
				}
			}

			hover.elements->add(std::make_shared<text_element>(i->name()));
		}

		keyboard_accelerator = forward ? tt.keyboard_right : tt.keyboard_left;
	}
	else if (id == commands::menu_group_toolbar)
	{
		hover.elements->clear();
		hover.elements->add(make_icon_element(icon_index::group, view_element_style::no_break));
		hover.elements->add(std::make_shared<text_element>(tt.group_sort_tooltip, ui::style::font_face::dialog,
		                                                   ui::style::text_style::multiline,
		                                                   view_element_style::line_break));

		hover.elements->add(std::make_shared<summary_control>(_state.summary_shown(), view_element_style::line_break));
		hover.elements->add(std::make_shared<text_element>(tt.group_sort_click, ui::style::font_face::dialog,
		                                                   ui::style::text_style::multiline,
		                                                   view_element_style::new_line));
	}
	else if (id == commands::browse_open_containingfolder)
	{
		const auto i = _state.command_item();

		if (i)
		{
			hover.elements->add(std::make_shared<text_element>(i->containing().text()));
		}
	}
	else if (id == commands::browse_next_folder || id == commands::browse_previous_folder)
	{
		hover.elements->add(std::make_shared<text_element>(_state.next_path(id == commands::browse_next_folder)));
	}
	else if (id == commands::browse_back)
	{
		if (_state.history.can_browse_back())
		{
			hover.elements->add(std::make_shared<text_element>(_state.history.back_entry().search.text()));
		}
	}
	else if (id == commands::browse_forward)
	{
		if (_state.history.can_browse_forward())
		{
			hover.elements->add(std::make_shared<text_element>(_state.history.forward_entry().search.text()));
		}
	}
	else if (id == commands::browse_parent)
	{
		const auto parent = _state.parent_search().parent;

		if (!parent.is_empty())
		{
			hover.elements->add(std::make_shared<text_element>(parent.text(), ui::style::font_face::dialog,
			                                                   ui::style::text_style::multiline,
			                                                   view_element_style::new_line));
		}
	}
	else if (id == commands::favorite)
	{
		const auto search = _state.search();

		if (!search.is_empty())
		{
			const auto is_favorite = _state.search_is_favorite();
			const auto text = str_format((is_favorite ? tt.favorite_remove_fmt : tt.favorite_add_fmt).sv(),
			                             search.text());
			hover.elements->add(std::make_shared<text_element>(text, ui::style::font_face::dialog,
			                                                   ui::style::text_style::multiline,
			                                                   view_element_style::new_line));

			hover.elements->add(std::make_shared<text_element>(tt.favorite_info, ui::style::font_face::dialog,
			                                                   ui::style::text_style::multiline,
			                                                   view_element_style::new_line));
		}
	}
	else if (id == commands::options_collection)
	{
		const auto search = _state.search();

		if (!search.is_empty())
		{
			const auto is_collection_root = _state.search_is_in_collection();
			const auto text = is_collection_root ? tt.collection_in : tt.collection_not_in;
			hover.elements->add(std::make_shared<text_element>(text, ui::style::font_face::dialog,
			                                                   ui::style::text_style::multiline,
			                                                   view_element_style::new_line));

			hover.elements->add(std::make_shared<text_element>(tt.collection_info, ui::style::font_face::dialog,
			                                                   ui::style::text_style::multiline,
			                                                   view_element_style::new_line));
		}
	}
#ifndef WINSTORE
	else if (id == commands::info_new_version)
	{
		hover.elements->add(std::make_shared<text_element>(tt.update_available, ui::style::font_face::dialog,
		                                                   ui::style::text_style::multiline,
		                                                   view_element_style::line_break));
		hover.elements->add(
			std::make_shared<text_element>(str_format(tt.update_avail_version_fmt.sv(), setting.available_version)));
		hover.elements->add(
			std::make_shared<text_element>(str_format(tt.update_current_version_fmt.sv(), s_app_version)));
	}
#endif
	else if (id == commands::option_show_rotated)
	{
		const auto i = _state.command_item();

		if (i != nullptr)
		{
			const auto md = i->metadata();

			if (md)
			{
				hover.elements->add(std::make_shared<text_element>(
					str_format(tt.item_oriented_tooltip_fmt.sv(), orientation_to_string(md->orientation))));
			}
		}

		hover.elements->add(std::make_shared<action_element>(tt.item_show_oriented));
	}

	if (keyboard_accelerator.empty())
	{
		if (command)
		{
			if (str::is_empty(keyboard_accelerator))
			{
				keyboard_accelerator = command->keyboard_accelerator_text;
			}
		}
	}

	if (!keyboard_accelerator.empty())
	{
		keyboard_accelerator = std::format("{} {}", tt.keyboard_accelerator_press, keyboard_accelerator);
		hover.elements->add(std::make_shared<action_element>(keyboard_accelerator));
	}
}

void app_frame::update_tooltip()
{
	_hover.clear();

	if (df::command_active == 0)
	{
		auto c = setting.show_help_tooltips ? _view_frame->_active_controller : view_controller_ptr{};
		std::shared_ptr<view_host> frame = _view_frame;

		if (!c)
		{
			c = _sidebar->_active_controller;
			frame = _sidebar;
		}

		if (c)
		{
			c->popup_from_location(_hover);

			if (_hover.id != commands::none)
			{
				tooltip(_hover, _hover.id);
			}

			const auto window_bounds = frame->frame()->window_bounds();
			_hover.window_bounds = _hover.window_bounds.offset(window_bounds.top_left());
			frame->_tooltip_bounds = _hover.active_bounds;
		}
		else if (setting.show_help_tooltips && _hover_command)
		{
			tooltip(_hover, std::any_cast<const commands>(_hover_command->opaque));
			_hover.window_bounds = _hover_command_bounds;
			_hover.active_bounds = _hover_command_bounds;
		}
	}

	if (!_hover.is_empty())
	{
		_bubble->show(_hover.elements, _hover.window_bounds, _hover.x_focus, _hover.preferred_size, _hover.horizontal);
	}
	else
	{
		_bubble->hide();
	}
}
