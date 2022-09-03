// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
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
	const static int item_splitter_max = 10000;
	const static int item_scale_count = 8;
	static int item_scale_snaps[item_scale_count];

	sizei thumbnail_max_dimension;
	int resize_max_dimension;
	int media_volume;
	int slideshow_delay;
	int item_scale = 5;
	int item_splitter_pos = 5;
	int min_show_update_day = 0;

	int jpeg_save_quality;
	int webp_quality;
	bool webp_lossless;

	uint64_t features_used_since_last_report = 0;
	uint32_t instantiations = 0;

	std::u8string write_folder;
	std::u8string write_name;
	std::u8string available_version;
	std::u8string available_test_version;
	std::u8string last_tags;
	std::u8string favourite_tags;
	std::u8string language;

	bool set_copyright_credit;
	std::u8string copyright_credit;
	bool set_copyright_source;
	std::u8string copyright_source;
	bool set_copyright_creator;
	std::u8string copyright_creator;
	bool set_copyright_notice;
	std::u8string copyright_notice;
	bool set_copyright_url;
	std::u8string copyright_url;

	bool set_artist;
	std::u8string artist;
	bool set_caption;
	std::u8string caption;
	bool set_album;
	std::u8string album;
	bool set_album_artist;
	std::u8string album_artist;
	bool set_genre;
	std::u8string genre;
	bool set_tv_show;
	std::u8string tv_show;

	bool is_tester;
	bool show_hidden;
	bool show_debug_info;
	bool confirm_deletions;
	bool first_run_today;
	bool first_run_ever;
	bool show_rotated;
	bool show_results;
	bool create_originals;
	bool check_for_updates;
	bool install_updates;
	bool can_animate;
	repeat_mode repeat;
	bool auto_play;
	bool scale_up;
	bool highlight_large_items;
	bool sort_dates_descending;
	bool show_sidebar;
	bool use_gpu;
	bool use_d3d11va;
	bool use_yuv;
	bool send_crash_dumps;
	bool force_available_version = false;
	bool large_font;
	bool verbose_metadata = false;
	bool raw_preview = true;
	bool detail_folder_items = true;
	bool detail_file_items = false;
	bool show_shadow = true;
	bool update_modified = true;
	bool last_played_pos = true;
	bool show_help_tooltips = true;

	struct sidebar_t
	{
		bool show_total_items;
		bool show_history;
		bool show_world_map;
		bool show_indexed_folders;
		bool show_drives;
		bool show_favorite_searches;
		bool show_favorite_tags;
		bool show_ratings;
		bool show_labels;
	} sidebar;

	struct import_t
	{
		std::u8string destination_path;
		std::u8string source_path;
		std::u8string source_filter;
		std::u8string dest_folder_structure;
		bool is_move;
		bool set_created_date;
		bool ignore_previous;
		bool overwrite_if_newer;
		bool rename_different_attributes;
	} import;

	struct sync_t
	{
		std::u8string local_path;
		std::u8string remote_path;
		bool sync_collection;
		bool sync_local_remote;
		bool sync_remote_local;
		bool sync_delete_local;
		bool sync_delete_remote;
	} sync;

	struct
	{
		bool maximize;
	} desktop_background;

	struct email_t
	{
		std::u8string to;
		std::u8string subject;
		std::u8string message;

		bool zip;
		bool limit;
		bool convert;
		int max_side;
	} email;

	struct convert_t
	{
		bool to_jpeg;
		bool to_png;
		bool to_webp;
		int jpeg_quality;
		int webp_quality;
		bool webp_lossless;
		bool limit_dimension;
		int max_side;
	} convert;

	struct rename_t
	{
		std::u8string name_template;
		std::u8string start_index;
	} rename;

	struct index_t
	{
		bool pictures;
		bool video;
		bool music;
		bool drop_box;
		bool onedrive_pictures;
		bool onedrive_video;
		bool onedrive_music;

		std::u8string more_folders;

		std::vector<std::u8string> collection_folders() const;
	} index;

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
