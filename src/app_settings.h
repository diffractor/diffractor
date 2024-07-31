// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2024  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

#include "model_location.h"
#include "util_interfaces.h"

constexpr auto defaut_custom_folder_structure = u8"{year}\\{created}"sv;

namespace features
{
	const uint64_t show_photo = 1ull << 0;
	const uint64_t show_video = 1ull << 1;
	const uint64_t show_audio = 1ull << 3;
	const uint64_t show_raw = 1ull << 5;

	const uint64_t search_folder = 1ull << 8;
	const uint64_t search_text = 1ull << 9;
	const uint64_t search_property = 1ull << 10;
	const uint64_t search_type = 1ull << 11;
	const uint64_t search_flatten = 1ull << 12;
	const uint64_t search_related = 1ull << 13;
	const uint64_t search_duplicates = 1ull << 14;

	const uint64_t edit_photo_metadata = 1ull << 16;
	const uint64_t edit_photo_bitmap = 1ull << 17;
	const uint64_t edit_video_metadata = 1ull << 18;
	const uint64_t edit_audio_metadata = 1ull << 19;

	const uint64_t tag = 1ull << 32;
	const uint64_t batch_edit = 1ull << 33;
	const uint64_t slideshow = 1ull << 34;
	const uint64_t resize = 1ull << 35;
	const uint64_t convert = 1ull << 36;
	const uint64_t rotate = 1ull << 37;
	const uint64_t locate = 1ull << 38;
	const uint64_t email = 1ull << 39;
	const uint64_t adjust_date = 1ull << 40;
	const uint64_t burn_to_disk = 1ull << 41;
	const uint64_t print = 1ull << 42;
	const uint64_t scan = 1ull << 43;
	const uint64_t import = 1ull << 44;
	const uint64_t pdf = 1ull << 45;
	const uint64_t remove_metadata = 1ull << 46;
	const uint64_t sync = 1ull << 47;
}


class settings_t
{
public:
	settings_t();

	void read(const platform::setting_file_ptr& store);
	void write(const platform::setting_file_ptr& store) const;

public:
	// Constants
	static constexpr int item_splitter_max = 10000;
	static constexpr int item_scale_count = 8;
	static int item_scale_snaps[item_scale_count];

	sizei thumbnail_max_dimension;
	int resize_max_dimension = 0;
	int media_volume = 0;
	int slideshow_delay = 0;
	int item_scale = 5;
	int item_splitter_pos = 5;
	int min_show_update_day = 0;

	int jpeg_save_quality = 0;
	int webp_quality = 0;
	bool webp_lossless = false;
	uint64_t features_used_since_last_report = 0;
	uint32_t instantiations = 0;

	std::u8string write_folder;
	std::u8string write_name;
	std::u8string available_version;
	std::u8string available_test_version;
	std::u8string last_tags;
	std::u8string favorite_tags;
	std::u8string language;
	std::u8string sound_device;

	bool set_copyright_credit = false;
	std::u8string copyright_credit;
	bool set_copyright_source = false;
	std::u8string copyright_source;
	bool set_copyright_creator = false;
	std::u8string copyright_creator;
	bool set_copyright_notice = false;
	std::u8string copyright_notice;
	bool set_copyright_url = false;
	std::u8string copyright_url;

	bool set_artist = false;
	std::u8string artist;
	bool set_caption = false;
	std::u8string caption;
	bool set_album = false;
	std::u8string album;
	bool set_album_artist = false;
	std::u8string album_artist;
	bool set_genre = false;
	std::u8string genre;
	bool set_tv_show = false;
	std::u8string tv_show;

	bool is_tester = false;
	bool show_hidden = false;
	bool show_debug_info = false;
	bool confirm_deletions = false;
	bool first_run_today = false;
	bool first_run_ever = false;
	bool show_rotated = false;
	bool show_results = false;
	bool create_originals = false;
	bool check_for_updates = false;
	bool install_updates = false;
	bool can_animate = false;
	repeat_mode repeat = repeat_mode::repeat_none;
	bool auto_play = false;
	bool scale_up = false;
	bool highlight_large_items = false;
	bool sort_dates_descending = false;
	bool show_sidebar = false;
	bool use_gpu = false;
	bool use_d3d11va = false;
	bool use_yuv = false;
	bool send_crash_dumps = false;
	bool force_available_version = false;
	bool large_font = false;
	bool verbose_metadata = false;
	bool raw_preview = true;
	uint32_t detail_items = true;
	bool show_shadow = true;
	bool update_modified = true;
	bool last_played_pos = true;
	bool show_help_tooltips = true;

	struct sidebar_t
	{
		bool show_total_items = false;
		bool show_history = false;
		bool show_world_map = false;
		bool show_indexed_folders = false;
		bool show_drives = false;
		bool show_favorite_searches = false;
		bool show_tags = false;
		bool show_ratings = false;
		bool show_labels = false;
		bool show_favorite_tags_only = false;
	} sidebar;

	struct import_t
	{
		std::u8string destination_path;
		std::u8string source_path;
		std::u8string source_filter;
		std::u8string dest_folder_structure;
		bool is_move = false;
		bool set_created_date = false;
		bool ignore_previous = false;
		bool overwrite_if_newer = false;
		bool rename_different_attributes = false;
	} import;

	struct sync_t
	{
		std::u8string local_path;
		std::u8string remote_path;
		bool sync_collection = false;
		bool sync_local_remote = false;
		bool sync_remote_local = false;
		bool sync_delete_local = false;
		bool sync_delete_remote = false;
	} sync;

	struct
	{
		bool maximize = false;
	} desktop_background;

	struct email_t
	{
		std::u8string to;
		std::u8string subject;
		std::u8string message;

		bool zip = false;
		bool limit = false;
		bool convert = false;
		int max_side = 0;
	} email;

	struct convert_t
	{
		bool to_jpeg = false;
		bool to_png = false;
		bool to_webp = false;
		int jpeg_quality = 0;
		int webp_quality = 0;
		bool webp_lossless = false;
		bool limit_dimension = false;
		int max_side = 0;
	} convert;

	struct rename_t
	{
		std::u8string name_template;
		std::u8string start_seq;
	} rename;

	struct index_t
	{
		bool pictures = false;
		bool video = false;
		bool music = false;
		bool drop_box = false;
		bool onedrive_pictures = false;
		bool onedrive_video = false;
		bool onedrive_music = false;

		std::u8string more_folders;
	} collection;

	struct search_t
	{
		static constexpr int count = 10;

		std::u8string title[count];
		std::u8string path[count];
	} search;

	gps_coordinate default_location;
};


extern settings_t setting;

static void record_feature_use(const uint64_t f)
{
	setting.features_used_since_last_report |= f;
}

std::vector<std::u8string_view> split_collection_folders(const std::u8string_view text);