// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once


enum class commands
{
	none = 0,

	advanced_search,
	browse_back,
	browse_forward,
	browse_next_folder,
	browse_next_group,
	browse_next_item,
	browse_next_item_extend,
	browse_open_containingfolder,
	browse_open_googlemap,
	browse_open_in_file_browser,
	browse_parent,
	browse_previous_folder,
	browse_previous_group,
	browse_previous_item,
	browse_previous_item_extend,
	browse_recursive,
	browse_search,
	edit_copy,
	edit_cut,
	edit_item_color_reset,
	view_exit,
	edit_item_options,
	edit_item_save,
	edit_item_save_and_next,
	edit_item_save_and_prev,
	edit_item_save_as,
	edit_paste,
	english,
	exit,
	favorite,
	filter_audio,
	filter_photos,
	filter_videos,
	group_album,
	group_camera,
	group_created,
	group_extension,
	group_file_type,
	group_folder,
	group_location,
	group_modified,
	group_pixels,
	group_presence,
	group_rating,
	group_shuffle,
	group_size,
	group_toggle,
	info_new_version,
	keyboard,
	label_approved,
	label_none,
	label_review,
	label_second,
	label_select,
	label_to_do,
	large_font,
	menu_display_options,
	menu_group,
	menu_group_toolbar,
	menu_language,
	menu_main,
	menu_navigate,
	menu_open,
	menu_playback,
	menu_rate_or_label,
	menu_select,
	menu_tag_with,
	menu_tools,
	menu_tools_toolbar,
	option_highlight_large_items,
	option_scale_up,
	option_show_rotated,
	option_show_thumbnails,
	option_toggle_details,
	option_toggle_item_size,
	options_collection,
	options_general,
	options_sidebar,
	pin_item,
	play,
	playback_auto_play,
	playback_last_played_pos,
	playback_menu,
	playback_repeat_all,
	playback_repeat_none,
	playback_repeat_one,
	playback_volume0,
	playback_volume100,
	playback_volume25,
	playback_volume50,
	playback_volume75,
	playback_volume_toggle,
	print,
	rate_1,
	rate_2,
	rate_3,
	rate_4,
	rate_5,
	rate_none,
	rate_rejected,
	refresh,
	repeat_toggle,	
	search_related,
	select_all,
	select_invert,
	select_nothing,
	show_raw_always,
	show_raw_preview,
	show_raw_this_only,
	sort_date_modified,
	sort_dates_ascending,
	sort_dates_descending,
	sort_def,
	sort_name,
	sort_size,	
	test_crash,
	test_boom,
	test_new_version,
	test_run_all,
	tool_adjust_date,
	tool_burn,
	tool_convert,
	tool_copy_to_folder,
	tool_delete,
	tool_desktop_background,
	tool_edit,
	tool_edit_metadata,
	tool_eject,
	tool_email,
	tool_import,
	tool_locate,
	tool_move_to_folder,
	tool_new_folder,
	tool_open_with,
	tool_file_properties,
	tool_remove_metadata,
	tool_rename,
	tool_rotate_anticlockwise,
	tool_rotate_clockwise,
	tool_rotate_reset,
	tool_save_current_video_frame,
	tool_scan,
	tool_sync,
	tool_tag,
	tool_test,
	verbose_metadata,
	view_fullscreen,
	view_help,
	view_items,
	view_maximize,
	view_minimize,
	view_restore,
	view_show_sidebar,
	view_favorite_tags,
	view_zoom,

	sync_analyze,
	sync_run,
	rename_run,
	import_analyze,
	import_run,
};
