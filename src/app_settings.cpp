// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "pch.h"

static constexpr std::u8string_view s_dest_path = u8"DestPath";
static constexpr std::u8string_view s_move = u8"Move";
static constexpr std::u8string_view s_import = u8"Import";
static constexpr std::u8string_view s_limit = u8"Limit";
static constexpr std::u8string_view s_jpg = u8"jpg";
static constexpr std::u8string_view s_png = u8"png";
static constexpr std::u8string_view s_webp = u8"webp";
static constexpr std::u8string_view s_limit_dimension = u8"LimitDimension";
static constexpr std::u8string_view s_max = u8"Max";
static constexpr std::u8string_view s_start = u8"Start";
static constexpr std::u8string_view s_album = u8"Album";
static constexpr std::u8string_view s_maximize = u8"Maximize";
static constexpr std::u8string_view s_favourite_tags = u8"FavouriteTags";
static constexpr std::u8string_view s_out_folder = u8"OutFolder";
static constexpr std::u8string_view s_resize_size = u8"ResizeSize";
static constexpr std::u8string_view s_volume = u8"Volume";
static constexpr std::u8string_view s_create_originals = u8"CreateOriginals";
static constexpr std::u8string_view s_show_rotated = u8"ShowRotated";
static constexpr std::u8string_view s_show_results = u8"ShowResults";
static constexpr std::u8string_view s_check_for_updates = u8"CheckForUpdates";
static constexpr std::u8string_view s_last_run = u8"LastRun";
static constexpr std::u8string_view s_pictures = u8"Pictures";
static constexpr std::u8string_view s_index = u8"Index";
static constexpr std::u8string_view s_favourite_search = u8"FavouriteSearch";
static constexpr std::u8string_view s_video = u8"Video";
static constexpr std::u8string_view s_music = u8"Music";
static constexpr std::u8string_view s_drop_box = u8"DropBox";
static constexpr std::u8string_view s_title = u8"Title";
static constexpr std::u8string_view s_path = u8"Path";
static constexpr std::u8string_view s_more = u8"More";
static constexpr std::u8string_view s_out_name = u8"OutName";
static constexpr std::u8string_view s_convert = u8"Convert";
static constexpr std::u8string_view s_available_version = u8"AvailableVersion";
static constexpr std::u8string_view s_available_test_version = u8"AvailableTestVersion";
static constexpr std::u8string_view s_tags = u8"Tags";
static constexpr std::u8string_view s_is_sponsor = u8"Awesome";
static constexpr std::u8string_view s_is_tester = u8"IsTester";
static constexpr std::u8string_view s_first_time = u8"First";
static constexpr std::u8string_view s_quality = u8"Quality";
static constexpr std::u8string_view s_webp_quality = u8"webp_quality";
static constexpr std::u8string_view s_webp_lossless = u8"webp_lossless";
static constexpr std::u8string_view s_slideshow_delay = u8"SlideshowDelay";
static constexpr std::u8string_view s_copyright = u8"Copyright";
static constexpr std::u8string_view s_creator = u8"Creator";
static constexpr std::u8string_view s_album_artist = u8"AlbumArtist";
static constexpr std::u8string_view s_genre = u8"Genre";
static constexpr std::u8string_view s_tv_show = u8"TvShow";
static constexpr std::u8string_view s_to = u8"To";
static constexpr std::u8string_view s_subject = u8"Subject";
static constexpr std::u8string_view s_message = u8"Message";
static constexpr std::u8string_view s_items_scale = u8"ItemsScale";
static constexpr std::u8string_view s_item_splitter = u8"ItemSplitter";
static constexpr std::u8string_view s_auto_play = u8"AutoPlay";
static constexpr std::u8string_view s_scale_up = u8"ScaleUp";
static constexpr std::u8string_view s_features = u8"Features";
static constexpr std::u8string_view s_update_min = u8"UpdateMin";
static constexpr std::u8string_view s_instantiations = u8"Instantiations";
static constexpr std::u8string_view s_highlight_large_items = u8"HighlightLargeItems";
static constexpr std::u8string_view s_sort_dates_descending = u8"SortDatesDescending";
static constexpr std::u8string_view g_show_navigation_bar = u8"show_nav_bar";
static constexpr std::u8string_view g_detail_folder_items = u8"detail_folder_items";
static constexpr std::u8string_view g_detail_file_items = u8"detail_file_items";
static constexpr std::u8string_view s_large_font = u8"large_font";
static constexpr std::u8string_view s_use_gpu = u8"UseGpu";
static constexpr std::u8string_view s_use_d3d11_va = u8"use_d3d11va";
static constexpr std::u8string_view s_use_yuv = u8"use_yuv";
static constexpr std::u8string_view s_send_crash_dumps = u8"SendCrashDumps";
static constexpr std::u8string_view s_install_updates = u8"InstallUpdates";
static constexpr std::u8string_view s_show_performance_timings = u8"ShowPerformanceTimings";
static constexpr std::u8string_view s_source = u8"Source";
static constexpr std::u8string_view s_credit = u8"Credit";
static constexpr std::u8string_view s_artist = u8"Artist";
static constexpr std::u8string_view s_caption = u8"caption";
static constexpr std::u8string_view s_url = u8"url";
static constexpr std::u8string_view s_set_url = u8"set_url";
static constexpr std::u8string_view s_set_copyright = u8"SetCopyright";
static constexpr std::u8string_view s_set_creator = u8"SetCreator";
static constexpr std::u8string_view s_set_source = u8"SetSource";
static constexpr std::u8string_view s_set_credit = u8"SetCredit";
static constexpr std::u8string_view s_set_artist = u8"SetArtist";
static constexpr std::u8string_view s_set_caption = u8"set_caption";
static constexpr std::u8string_view s_set_album = u8"SetAlbum";
static constexpr std::u8string_view s_set_album_artist = u8"SetAlbumArtist";
static constexpr std::u8string_view s_set_genre = u8"SetGenre";
static constexpr std::u8string_view s_set_tv_show = u8"SetTvShow";
static constexpr std::u8string_view s_ignore_previous = u8"IgnorePrevious";
static constexpr std::u8string_view s_overwrite_if_newer = u8"OverwriteIfNewer";
static constexpr std::u8string_view s_hidden = u8"Hidden";
static constexpr std::u8string_view s_email = u8"Email";
static constexpr std::u8string_view s_rename = u8"Rename";
static constexpr std::u8string_view s_template = u8"Template";
static constexpr std::u8string_view s_wall_paper = u8"WallPaper";
static constexpr std::u8string_view s_confirm = u8"Confirm";
static constexpr std::u8string_view s_repeat = u8"repeat";
static constexpr std::u8string_view s_zip = u8"Zip";
static constexpr std::u8string_view s_sidebar = u8"sidebar";
static constexpr std::u8string_view s_show_total_items = u8"show_total_items";
static constexpr std::u8string_view s_show_history = u8"show_history";
static constexpr std::u8string_view s_show_world_map = u8"show_world_map";
static constexpr std::u8string_view s_show_indexed_folders = u8"show_indexed_folders";
static constexpr std::u8string_view s_show_drives = u8"show_drives";
static constexpr std::u8string_view s_show_favorite_searches = u8"show_favorite_searches";
static constexpr std::u8string_view s_show_favorite_tags = u8"show_favorite_tags";
static constexpr std::u8string_view s_show_ratings = u8"show_ratings";
static constexpr std::u8string_view s_show_labels = u8"show_labels";
static constexpr std::u8string_view s_lang = u8"lang";
static constexpr std::u8string_view s_verbose_metadata = u8"verbose_metadata";
static constexpr std::u8string_view s_location_latitude = u8"location_latitude";
static constexpr std::u8string_view s_location_longitude = u8"location_longitude";
static constexpr std::u8string_view s_raw_preview = u8"raw_preview";
static constexpr std::u8string_view s_onedrive_pictures = u8"onedrive_pictures";
static constexpr std::u8string_view s_onedrive_video = u8"onedrive_video";
static constexpr std::u8string_view s_onedrive_music = u8"onedrive_music";
static constexpr std::u8string_view s_set_created_date = u8"set_created_date";
static constexpr std::u8string_view s_sync = u8"sync";
static constexpr std::u8string_view s_local_path = u8"local_path";
static constexpr std::u8string_view s_remote_path = u8"remote_path";
static constexpr std::u8string_view s_sync_collection = u8"sync_collection";
static constexpr std::u8string_view s_sync_local_remoteh = u8"sync_local_remoteh";
static constexpr std::u8string_view s_sync_remote_local = u8"sync_remote_local";
static constexpr std::u8string_view s_sync_delete_local = u8"sync_delete_local";
static constexpr std::u8string_view s_sync_delete_remote = u8"sync_delete_remote";
static constexpr std::u8string_view s_custom_folder_structure = u8"dest_folder_structure";
static constexpr std::u8string_view s_rename_different_attributes = u8"rename_different_attributes";
static constexpr std::u8string_view s_source_path = u8"source_path";
static constexpr std::u8string_view s_source_filter = u8"source_filter";
static constexpr std::u8string_view s_show_shadow = u8"show_shadow";
static constexpr std::u8string_view s_update_modified = u8"update_modified";
static constexpr std::u8string_view s_last_played_pos = u8"last_played_pos";
static constexpr std::u8string_view s_show_help_tooltips = u8"show_help_tooltips";

settings_t setting;

int settings_t::item_scale_snaps[item_scale_count];

static void init_item_scale_snaps()
{
	auto prime = 5059.0;

	for (auto i = 0; i < settings_t::item_scale_count; i++)
	{
		settings_t::item_scale_snaps[i] = df::round(std::sqrt(prime));
		prime *= 1.3333;
	}
}

settings_t::settings_t()
{
	init_item_scale_snaps();

	language = u8"en";
	write_name = u8"results";
	show_rotated = true;
	show_results = true;
	create_originals = true;
	check_for_updates = true;
	install_updates = true;
	show_hidden = false;
	show_debug_info = false;
	use_gpu = true;
	use_d3d11va = true;
	use_yuv = true;
	send_crash_dumps = true;
	confirm_deletions = true;
	first_run_today = true;
	first_run_ever = true;
	is_tester = false;
	can_animate = false;
	repeat = repeat_mode::repeat_none;
	auto_play = true;
	scale_up = true;
	highlight_large_items = true;
	sort_dates_descending = true;
	show_sidebar = true;
	large_font = false;
	verbose_metadata = true;
	raw_preview = true;
	default_location = gps_coordinate(51.5255317687988, -0.116743430495262); // London

	features_used_since_last_report = 0;
	instantiations = 0;

	copyright_notice = str::format(u8"\xC2\xA9 {} {} - All Rights Reserved", platform::user_name(), platform::now().year());
	copyright_creator = platform::user_name();
	artist = platform::user_name();

	thumbnail_max_dimension = { 320, 256 };
	resize_max_dimension = 2048;
	media_volume = 1000;
	jpeg_save_quality = 90;
	webp_quality = 70;
	webp_lossless = false;
	slideshow_delay = 3;
	item_splitter_pos = item_splitter_max / 2;

	write_folder = known_path(platform::known_folder::pictures).text();
	import.source_path = known_path(platform::known_folder::onedrive_camera_roll).text();
	import.destination_path = known_path(platform::known_folder::pictures).text();
	import.dest_folder_structure = defaut_custom_folder_structure;
	import.is_move = false;
	import.set_created_date = true;
	import.rename_different_attributes = true;

	sync.local_path = known_path(platform::known_folder::pictures).text();
	sync.remote_path = known_path(platform::known_folder::onedrive_pictures).combine(u8"backup").text();
	sync.sync_collection = true;
	sync.sync_local_remote = true;
	sync.sync_remote_local = false;
	sync.sync_delete_local = false;
	sync.sync_delete_remote = true;


	desktop_background.maximize = true;

	email.zip = false;
	email.convert = true;
	email.limit = true;
	email.max_side = 1024;

	convert.to_jpeg = true;
	convert.to_png = false;
	convert.to_webp = false;
	convert.limit_dimension = false;
	convert.max_side = 1024;
	convert.jpeg_quality = 90;
	convert.webp_quality = 70;
	convert.webp_lossless = false;

	sidebar.show_total_items = true;
	sidebar.show_history = true;
	sidebar.show_world_map = true;
	sidebar.show_indexed_folders = true;
	sidebar.show_drives = true;
	sidebar.show_favorite_searches = true;
	sidebar.show_favorite_tags = true;
	sidebar.show_ratings = true;
	sidebar.show_labels = true;

	index.pictures = true;
	index.video = true;
	index.music = false;
	index.drop_box = false;
	index.onedrive_pictures = false;
	index.onedrive_video = false;
	index.onedrive_music = false;

	search.title[0] = u8"Last 7 days";
	search.path[0] = u8"age: 7";
	search.title[1] = u8"Christmas";
	search.path[1] = u8"created: dec.25";
	search.title[2] = u8"Desktop";
	search.path[2] = known_path(platform::known_folder::desktop).text();
	search.title[3] = u8"Downloads";
	search.path[3] = known_path(platform::known_folder::downloads).text();
	search.title[4] = u8"Pictures";
	search.path[4] = known_path(platform::known_folder::pictures).text();
	search.title[5] = u8"Videos";
	search.path[5] = known_path(platform::known_folder::video).text();

	rename.name_template = u8"Item ###";
	rename.start_index = u8"1";

	set_copyright_credit = false;
	set_copyright_source = false;
	set_copyright_creator = false;
	set_copyright_notice = false;
	set_copyright_url = false;
	set_artist = false;
	set_caption = false;
	set_album = false;
	set_album_artist = false;
	set_genre = false;
	set_tv_show = false;

	available_version = s_app_version;
};

class setting_formatter
{
	platform::setting_file_ptr _file;

public:
	setting_formatter(const platform::setting_file_ptr& f) : _file(f)
	{
	}

	bool write(const std::u8string_view section, const std::u8string_view name, const uint32_t v)
	{
		return _file->write(section, name, v);
	}

	bool read(const std::u8string_view section, const std::u8string_view name, uint32_t& v) const
	{
		return _file->read(section, name, v);
	}

	bool write(const std::u8string_view section, const std::u8string_view name, const uint64_t v)
	{
		return _file->write(section, name, v);
	}

	bool read(const std::u8string_view section, const std::u8string_view name, uint64_t& v) const
	{
		return _file->read(section, name, v);
	}

	bool write(const std::u8string_view section, const std::u8string_view name, const int v)
	{
		return _file->write(section, name, std::bit_cast<uint32_t>(v));
	}

	bool read(const std::u8string_view section, const std::u8string_view name, int& v) const
	{
		uint32_t vv{};
		if (_file->read(section, name, vv))
		{
			v = vv;
			return true;
		}
		return false;
	}

	bool write(const std::u8string_view section, const std::u8string_view name, const long v)
	{
		return _file->write(section, name, std::bit_cast<uint32_t>(v));
	}

	bool read(const std::u8string_view section, const std::u8string_view name, int64_t& v) const
	{
		uint64_t vv{};
		if (_file->read(section, name, vv))
		{
			v = vv;
			return true;
		}
		return false;
	}

	bool read(const std::u8string_view section, const std::u8string_view name, repeat_mode& v) const
	{
		uint32_t vv{};
		if (_file->read(section, name, vv))
		{
			v = std::bit_cast<repeat_mode>(vv);
			return true;
		}
		return false;
	}

	bool write(const std::u8string_view section, const std::u8string_view name, const int64_t v)
	{
		return _file->write(section, name, std::bit_cast<uint64_t>(v));
	}

	bool read(const std::u8string_view section, const std::u8string_view name, double& v) const
	{
		std::u8string str;
		const bool success = _file->read(section, name, str);
		v = str::to_double(str);
		return success;
	}

	bool write(const std::u8string_view section, const std::u8string_view name, const double v)
	{
		return _file->write(section, name, str::to_string(v, 5));
	}

	bool read(const std::u8string_view section, const std::u8string_view name, long& v) const
	{
		uint32_t vv{};
		if (_file->read(section, name, vv))
		{
			v = vv;
			return true;
		}
		return false;
	}

	bool write(const std::u8string_view section, const std::u8string_view name, const bool v)
	{
		const uint32_t dw = v ? 1 : 0;
		return _file->write(section, name, dw);
	}

	bool read(const std::u8string_view section, const std::u8string_view name, bool& v) const
	{
		uint32_t dw = 0;

		if (!_file->read(section, name, dw))
			return false;

		v = dw != 0;
		return true;
	}

	bool read(const std::u8string_view section, const std::u8string_view name, std::u8string& str) const
	{
		return _file->read(section, name, str);
	}

	bool write(const std::u8string_view section, const std::u8string_view name, const std::u8string_view v)
	{
		return _file->write(section, name, v);
	}

	bool read(const std::u8string_view section, const std::u8string_view name, std::u8string* v, int count) const
	{
		auto success = true;

		for (auto i = 0; i < count; i++)
		{
			success &= read(section, str::format(u8"{}{}", name, i), v[i]);
		}

		return success;
	}

	bool write(const std::u8string_view section, const std::u8string_view name, const std::u8string* v, int count)
	{
		auto success = true;

		for (auto i = 0; i < count; i++)
		{
			success &= write(section, str::format(u8"{}{}", name, i), v[i]);
		}

		return success;
	}

	bool read(const std::u8string_view section, const std::u8string_view name, std::vector<uint8_t>& v) const
	{
		const size_t buffer_size = 1024 * 8;
		uint8_t buffer[buffer_size];
		size_t n = buffer_size;
		const auto success = read(section, name, buffer, n);

		if (success)
		{
			v.assign(buffer, buffer + n);
		}

		return success;
	}

	bool write(const std::u8string_view section, const std::u8string_view name, const std::vector<uint8_t>& v)
	{
		return write(section, name, { v.data(), v.size() });
	}

	bool read(const std::u8string_view section, const std::u8string_view name,
		df::hash_map<std::u8string, std::u8string, df::ihash, df::ieq>& v) const
	{
		std::u8string combined;
		const auto success = read(section, name, combined);

		if (success)
		{
			const auto parts = str::split(combined, true, str::is_white_space);

			for (const auto& part : parts)
			{
				const auto split = part.find(':');

				if (split != std::u8string_view::npos)
				{
					v[std::u8string(part.substr(0, split))] = part.substr(split + 1);
				}
			}
		}

		return success;
	}

	bool write(const std::u8string_view section, const std::u8string_view name,
		const df::hash_map<std::u8string, std::u8string, df::ihash, df::ieq>& v)
	{
		std::u8string combined;
		for (const auto& s : v) str::join(combined, s.first + u8":" + s.second);
		return write(section, name, combined);
	}

	bool read(const std::u8string_view section, const std::u8string_view name, uint8_t* pValue, size_t& n) const
	{
		return _file->read(section, name, pValue, n);
	}

	bool write(const std::u8string_view section, const std::u8string_view name, df::cspan cs) const
	{
		return _file->write(section, name, cs);
	}
};

void settings_t::read(const platform::setting_file_ptr& store_in)
{
	const setting_formatter store(store_in);

	store.read({}, s_is_tester, is_tester);
	store.read({}, s_hidden, show_hidden);
	store.read({}, s_show_shadow, show_shadow);
	store.read({}, s_update_modified, update_modified);
	store.read({}, s_last_played_pos, last_played_pos);
	store.read({}, s_show_help_tooltips, show_help_tooltips);
	store.read({}, s_show_performance_timings, show_debug_info);
	store.read({}, s_use_gpu, use_gpu);
	store.read({}, s_use_d3d11_va, use_d3d11va);
	store.read({}, s_use_yuv, use_yuv);
	store.read({}, s_send_crash_dumps, send_crash_dumps);
	store.read({}, s_confirm, confirm_deletions);
	store.read({}, s_repeat, repeat);
	store.read({}, s_auto_play, auto_play);
	store.read({}, s_scale_up, scale_up);
	store.read({}, s_highlight_large_items, highlight_large_items);
	store.read({}, s_sort_dates_descending, sort_dates_descending);
	store.read({}, g_show_navigation_bar, show_sidebar);
	store.read({}, g_detail_folder_items, detail_folder_items);
	store.read({}, g_detail_file_items, detail_file_items);
	store.read({}, s_resize_size, resize_max_dimension);
	store.read({}, s_volume, media_volume);
	store.read({}, s_quality, jpeg_save_quality);
	store.read({}, s_webp_quality, webp_quality);
	store.read({}, s_webp_lossless, webp_lossless);
	store.read({}, s_slideshow_delay, slideshow_delay);
	store.read({}, s_items_scale, item_scale);
	store.read({}, s_item_splitter, item_splitter_pos);
	store.read({}, s_update_min, min_show_update_day);
	store.read({}, s_large_font, large_font);
	store.read({}, s_verbose_metadata, verbose_metadata);
	store.read({}, s_raw_preview, raw_preview);
	store.read({}, s_lang, language);
	store.read({}, s_out_name, write_name);
	store.read({}, s_out_folder, write_folder);
	store.read({}, s_show_rotated, show_rotated);
	store.read({}, s_show_results, show_results);
	store.read({}, s_create_originals, create_originals);
	store.read({}, s_check_for_updates, check_for_updates);
	store.read({}, s_install_updates, install_updates);

	auto lat = default_location.latitude();
	auto lng = default_location.longitude();
	store.read({}, s_location_latitude, lat);
	store.read({}, s_location_longitude, lng);
	default_location.latitude(lat);
	default_location.longitude(lng);

	store.read({}, s_available_version, available_version);
	store.read({}, s_available_test_version, available_test_version);
	store.read({}, s_tags, last_tags);
	store.read({}, s_favourite_tags, favourite_tags);

	store.read({}, s_copyright, copyright_notice);
	store.read({}, s_creator, copyright_creator);
	store.read({}, s_source, copyright_source);
	store.read({}, s_credit, copyright_credit);
	store.read({}, s_url, copyright_url);
	store.read({}, s_artist, artist);
	store.read({}, s_caption, caption);
	store.read({}, s_album, album);
	store.read({}, s_album_artist, album_artist);
	store.read({}, s_genre, genre);
	store.read({}, s_tv_show, tv_show);

	store.read({}, s_set_copyright, set_copyright_credit);
	store.read({}, s_set_creator, set_copyright_source);
	store.read({}, s_set_source, set_copyright_creator);
	store.read({}, s_set_credit, set_copyright_notice);
	store.read({}, s_set_url, set_copyright_url);
	store.read({}, s_set_artist, set_artist);
	store.read({}, s_set_caption, set_caption);
	store.read({}, s_set_album, set_album);
	store.read({}, s_set_album_artist, set_album_artist);
	store.read({}, s_set_genre, set_genre);
	store.read({}, s_set_tv_show, set_tv_show);

	store.read({}, s_features, features_used_since_last_report);
	store.read({}, s_instantiations, instantiations);

	int last_run = 0;
	store.read({}, s_last_run, last_run);
	first_run_today = last_run != platform::now().to_days();
	store.read({}, s_first_time, first_run_ever);

	store.read(s_rename, s_template, rename.name_template);
	store.read(s_rename, s_start, rename.start_index);

	store.read(s_import, s_source_path, import.source_path);
	store.read(s_import, s_source_filter, import.source_filter);
	store.read(s_import, s_dest_path, import.destination_path);
	store.read(s_import, s_move, import.is_move);
	store.read(s_import, s_set_created_date, import.set_created_date);
	store.read(s_import, s_ignore_previous, import.ignore_previous);
	store.read(s_import, s_overwrite_if_newer, import.overwrite_if_newer);
	store.read(s_import, s_custom_folder_structure, import.dest_folder_structure);
	store.read(s_import, s_rename_different_attributes, import.rename_different_attributes);

	store.read(s_sync, s_local_path, sync.local_path);
	store.read(s_sync, s_remote_path, sync.remote_path);
	store.read(s_sync, s_sync_collection, sync.sync_collection);
	store.read(s_sync, s_sync_local_remoteh, sync.sync_local_remote);
	store.read(s_sync, s_sync_remote_local, sync.sync_remote_local);
	store.read(s_sync, s_sync_delete_local, sync.sync_delete_local);
	store.read(s_sync, s_sync_delete_remote, sync.sync_delete_remote);

	store.read(s_wall_paper, s_maximize, desktop_background.maximize);

	store.read(s_convert, s_jpg, convert.to_jpeg);
	store.read(s_convert, s_png, convert.to_png);
	store.read(s_convert, s_webp, convert.to_webp);
	store.read(s_convert, s_limit_dimension, convert.limit_dimension);
	store.read(s_convert, s_max, convert.max_side);
	store.read(s_convert, s_quality, convert.jpeg_quality);
	store.read(s_convert, s_webp_quality, convert.webp_quality);
	store.read(s_convert, s_webp_lossless, convert.webp_lossless);

	store.read(s_email, s_to, email.to);
	store.read(s_email, s_subject, email.subject);
	store.read(s_email, s_message, email.message);
	store.read(s_email, s_zip, email.zip);
	store.read(s_email, s_convert, email.convert);
	store.read(s_email, s_limit, email.limit);
	store.read(s_email, s_max, email.max_side);

	store.read(s_index, s_pictures, index.pictures);
	store.read(s_index, s_video, index.video);
	store.read(s_index, s_music, index.music);
	store.read(s_index, s_drop_box, index.drop_box);
	store.read(s_index, s_onedrive_pictures, index.onedrive_pictures);
	store.read(s_index, s_onedrive_video, index.onedrive_video);
	store.read(s_index, s_onedrive_music, index.onedrive_music);
	store.read(s_index, s_more, index.more_folders);

	store.read(s_favourite_search, s_title, search.title, search.count);
	store.read(s_favourite_search, s_path, search.path, search.count);

	store.read(s_sidebar, s_show_total_items, sidebar.show_total_items);
	store.read(s_sidebar, s_show_history, sidebar.show_history);
	store.read(s_sidebar, s_show_world_map, sidebar.show_world_map);
	store.read(s_sidebar, s_show_indexed_folders, sidebar.show_indexed_folders);
	store.read(s_sidebar, s_show_drives, sidebar.show_drives);
	store.read(s_sidebar, s_show_favorite_searches, sidebar.show_favorite_searches);
	store.read(s_sidebar, s_show_favorite_tags, sidebar.show_favorite_tags);
	store.read(s_sidebar, s_show_ratings, sidebar.show_ratings);
	store.read(s_sidebar, s_show_labels, sidebar.show_labels);
}

void settings_t::write(const platform::setting_file_ptr& store_in) const
{
	setting_formatter store(store_in);

	store.write({}, s_is_tester, is_tester);
	store.write({}, s_hidden, show_hidden);
	store.write({}, s_show_shadow, show_shadow);
	store.write({}, s_update_modified, update_modified);
	store.write({}, s_last_played_pos, last_played_pos);
	store.write({}, s_show_help_tooltips, show_help_tooltips);
	store.write({}, s_show_performance_timings, show_debug_info);
	store.write({}, s_use_gpu, use_gpu);
	store.write({}, s_use_d3d11_va, use_d3d11va);
	store.write({}, s_use_yuv, use_yuv);
	store.write({}, s_send_crash_dumps, send_crash_dumps);
	store.write({}, s_confirm, confirm_deletions);
	store.write({}, s_repeat, static_cast<int>(repeat));
	store.write({}, s_auto_play, auto_play);
	store.write({}, s_scale_up, scale_up);
	store.write({}, s_highlight_large_items, highlight_large_items);
	store.write({}, s_sort_dates_descending, sort_dates_descending);
	store.write({}, g_show_navigation_bar, show_sidebar);
	store.write({}, g_detail_folder_items, detail_folder_items);
	store.write({}, g_detail_file_items, detail_file_items);
	store.write({}, s_resize_size, resize_max_dimension);
	store.write({}, s_volume, media_volume);
	store.write({}, s_quality, jpeg_save_quality);
	store.write({}, s_webp_quality, webp_quality);
	store.write({}, s_webp_lossless, webp_lossless);
	store.write({}, s_slideshow_delay, slideshow_delay);
	store.write({}, s_items_scale, item_scale);
	store.write({}, s_item_splitter, item_splitter_pos);
	store.write({}, s_update_min, min_show_update_day);
	store.write({}, s_large_font, large_font);
	store.write({}, s_verbose_metadata, verbose_metadata);
	store.write({}, s_raw_preview, raw_preview);
	store.write({}, s_lang, language);
	store.write({}, s_out_name, write_name);
	store.write({}, s_out_folder, write_folder);
	store.write({}, s_show_rotated, show_rotated);
	store.write({}, s_show_results, show_results);
	store.write({}, s_create_originals, create_originals);
	store.write({}, s_check_for_updates, check_for_updates);
	store.write({}, s_install_updates, install_updates);
	store.write({}, s_last_run, platform::now().to_days());
	store.write({}, s_location_latitude, default_location.latitude());
	store.write({}, s_location_longitude, default_location.longitude());
	store.write({}, s_available_version, available_version);
	store.write({}, s_available_test_version, available_test_version);
	store.write({}, s_tags, last_tags);
	store.write({}, s_favourite_tags, favourite_tags);
	store.write({}, s_first_time, first_run_ever);
	store.write({}, s_copyright, copyright_notice);
	store.write({}, s_creator, copyright_creator);
	store.write({}, s_source, copyright_source);
	store.write({}, s_credit, copyright_credit);
	store.write({}, s_url, copyright_url);
	store.write({}, s_artist, artist);
	store.write({}, s_caption, caption);
	store.write({}, s_album, album);
	store.write({}, s_album_artist, album_artist);
	store.write({}, s_genre, genre);
	store.write({}, s_tv_show, tv_show);

	store.write({}, s_set_copyright, set_copyright_credit);
	store.write({}, s_set_creator, set_copyright_source);
	store.write({}, s_set_source, set_copyright_creator);
	store.write({}, s_set_credit, set_copyright_notice);
	store.write({}, s_set_url, set_copyright_url);
	store.write({}, s_set_artist, set_artist);
	store.write({}, s_set_caption, set_caption);
	store.write({}, s_set_album, set_album);
	store.write({}, s_set_album_artist, set_album_artist);
	store.write({}, s_set_genre, set_genre);
	store.write({}, s_set_tv_show, set_tv_show);

	store.write({}, s_features, features_used_since_last_report);
	store.write({}, s_instantiations, instantiations);

	store.write(s_rename, s_template, rename.name_template);
	store.write(s_rename, s_start, rename.start_index);

	store.write(s_import, s_source_path, import.source_path);
	store.write(s_import, s_source_filter, import.source_filter);
	store.write(s_import, s_dest_path, import.destination_path);
	store.write(s_import, s_move, import.is_move);
	store.write(s_import, s_set_created_date, import.set_created_date);
	store.write(s_import, s_ignore_previous, import.ignore_previous);
	store.write(s_import, s_overwrite_if_newer, import.overwrite_if_newer);
	store.write(s_import, s_custom_folder_structure, import.dest_folder_structure);
	store.write(s_import, s_rename_different_attributes, import.rename_different_attributes);

	store.write(s_sync, s_local_path, sync.local_path);
	store.write(s_sync, s_remote_path, sync.remote_path);
	store.write(s_sync, s_sync_collection, sync.sync_collection);
	store.write(s_sync, s_sync_local_remoteh, sync.sync_local_remote);
	store.write(s_sync, s_sync_remote_local, sync.sync_remote_local);
	store.write(s_sync, s_sync_delete_local, sync.sync_delete_local);
	store.write(s_sync, s_sync_delete_remote, sync.sync_delete_remote);

	store.write(s_wall_paper, s_maximize, desktop_background.maximize);

	store.write(s_convert, s_jpg, convert.to_jpeg);
	store.write(s_convert, s_png, convert.to_png);
	store.write(s_convert, s_webp, convert.to_webp);
	store.write(s_convert, s_limit_dimension, convert.limit_dimension);
	store.write(s_convert, s_max, convert.max_side);
	store.write(s_convert, s_quality, convert.jpeg_quality);
	store.write(s_convert, s_webp_quality, convert.webp_quality);
	store.write(s_convert, s_webp_lossless, convert.webp_lossless);

	store.write(s_email, s_to, email.to);
	store.write(s_email, s_subject, email.subject);
	store.write(s_email, s_message, email.message);
	store.write(s_email, s_zip, email.zip);
	store.write(s_email, s_convert, email.convert);
	store.write(s_email, s_limit, email.limit);
	store.write(s_email, s_max, email.max_side);

	store.write(s_index, s_pictures, index.pictures);
	store.write(s_index, s_video, index.video);
	store.write(s_index, s_music, index.music);
	store.write(s_index, s_drop_box, index.drop_box);
	store.write(s_index, s_onedrive_pictures, index.onedrive_pictures);
	store.write(s_index, s_onedrive_video, index.onedrive_video);
	store.write(s_index, s_onedrive_music, index.onedrive_music);
	store.write(s_index, s_more, index.more_folders);

	store.write(s_favourite_search, s_title, search.title, search.count);
	store.write(s_favourite_search, s_path, search.path, search.count);

	store.write(s_sidebar, s_show_total_items, sidebar.show_total_items);
	store.write(s_sidebar, s_show_history, sidebar.show_history);
	store.write(s_sidebar, s_show_world_map, sidebar.show_world_map);
	store.write(s_sidebar, s_show_indexed_folders, sidebar.show_indexed_folders);
	store.write(s_sidebar, s_show_drives, sidebar.show_drives);
	store.write(s_sidebar, s_show_favorite_searches, sidebar.show_favorite_searches);
	store.write(s_sidebar, s_show_favorite_tags, sidebar.show_favorite_tags);
	store.write(s_sidebar, s_show_ratings, sidebar.show_ratings);
	store.write(s_sidebar, s_show_labels, sidebar.show_labels);
}

std::vector<std::u8string> settings_t::index_t::collection_folders() const
{
	std::vector<std::u8string> result;
	const auto more = str::split(more_folders, true, [](wchar_t c) { return c == '\n' || c == '\r'; });
	result.reserve(more.size() + 1);
	for (auto m : more) result.emplace_back(m);
	return result;
}
