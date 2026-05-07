// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Shared test infrastructure, helper classes, and utility functions
// used across multiple test files (tests1-6).

#pragma once

#include "model.h"
#include "model_db.h"
#include "model_index.h"
#include "model_locations.h"
#include "files.h"
#include "view_test.h"
#include "test.h"
#include "av_player.h"
#include "ui_controls.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
// Shared constants
///////////////////////////////////////////////////////////////////////////////////////////////////

inline const auto test_files_folder = known_path(platform::known_folder::test_files_folder);
inline constexpr sizei thumbnail_max_dimension = {256, 256};
inline constexpr int expected_cached_item_count = 38;

inline const auto long_text =
	"The Commodore 64, also known as the C64, C-64, C= 64, or occasionally CBM 64 or VIC-64, is an 8-bit home computer introduced in January 1982 by Commodore International. "
	"It is listed in the Guinness World Records as the highest-selling single computer model of all time, with independent estimates placing the number sold between 10 and 17 million units. "
	"Volume production started in early 1982, with machines being released on to the market in August at a price of US $595(roughly equivalent to $1, 500 in 2015)."
	"Preceded by the Commodore VIC - 20 and Commodore PET, the C64 takes its name from its 64 kilobytes(65, 536 bytes) of RAM, and has technologically superior sound and graphical specifications when compared to some earlier systems such as the Apple II and Atari 800, with multi - color sprites and a more advanced sound processor.";

///////////////////////////////////////////////////////////////////////////////////////////////////
// Shared helper classes
///////////////////////////////////////////////////////////////////////////////////////////////////

class null_item_results_ui final : public df::status_i
{
public:
	void start_item(const std::string_view name) override
	{
	}

	void end_item(const std::string_view name, const item_status status) override
	{
	}

	bool has_failures() const override { return false; }

	void abort(const std::string_view error_message) override
	{
	}

	void complete(const std::string_view message) override
	{
	}

	void show_errors() override
	{
	}

	void message(const std::string_view message, int64_t pos, int64_t total) override
	{
	}

	void show_message(const std::string_view message) override
	{
	}

	bool is_canceled() const override { return false; }

	void wait_for_complete() const override
	{
	}
};

class null_state_strategy final : public state_strategy
{
public:
	explicit null_state_strategy() = default;

	void toggle_full_screen() override
	{
	}

	bool can_open_search(const df::search_t& path) override { return true; }

	void element_broadcast(const view_element_event& event) override
	{
	}

	void item_focus_changed(const df::item_element_ptr& focus, const df::item_element_ptr& previous) override
	{
	}

	void display_changed() override
	{
	}

	void view_changed(view_type m) override
	{
	}

	void play_state_changed(const bool play) override
	{
	}

	void search_complete(const df::search_t& path, bool path_changed) override
	{
	}

	void invoke(const commands id) override
	{
	}

	void make_visible(const df::item_element_ptr& i) override
	{
	}

	void command_hover(const ui::command_ptr& c, const recti window_bounds) override
	{
	}

	bool is_command_checked(const commands cmd) override { return false; }

	void focus_view() override
	{
	}

	void delete_items(const df::item_set& items) override
	{
	}

	void invalidate_view(const view_invalid invalid) override
	{
	}

	void track_menu(const ui::frame_ptr& parent, const recti bounds,
	                const std::vector<ui::command_ptr>& commands) override
	{
	}

	void free_graphics_resources(const bool items_only, const bool offscreen_only) override
	{
	}
};

class null_async_strategy final : public async_strategy
{
public:
	location_cache locations;

	void queue_ui(const std::function<void()> f) override { f(); }

	void queue_media_preview(std::function<void(media_preview_state&)>) override
	{
	}

	void queue_database(const std::function<void(database&)> f) override
	{
	}

	void web_service_cache(std::string key, std::function<void(const std::string&)> f) override
	{
	}

	void web_service_cache(std::string key, std::string value) override
	{
	}

	void queue_async(async_queue q, const std::function<void()> f) override { f(); }

	void queue_location(const std::function<void(location_cache&)> f) override
	{
		if (!locations.is_index_loaded()) locations.load_index();
		f(locations);
	}

	void invalidate_view(const view_invalid invalid) override
	{
	}
};

class temp_files
{
	df::folder_path _folder;
	df::file_paths _paths;

public:
	temp_files() : _folder(platform::temp_folder().combine(std::format("diffractor-{}", platform::tick_count())))
	{
	}

	~temp_files()
	{
		delete_temps();
	}

	temp_files(const temp_files& other) noexcept = delete;
	temp_files(temp_files&& other) noexcept = delete;
	temp_files& operator=(const temp_files& other) = delete;
	temp_files& operator=(temp_files&& other) noexcept = delete;

	void delete_temps()
	{
		df::file_paths paths;
		std::swap(paths, _paths);

		for (const auto& path : paths)
		{
			platform::delete_file(path);
		}
	}

	df::folder_path folder() const
	{
		return _folder;
	}

	df::file_path next_path(const std::string_view ext = ".jpg")
	{
		if (!_folder.exists())
		{
			platform::create_folder(_folder);
		}

		auto result = platform::temp_file(ext, _folder);
		_paths.emplace_back(result);
		return result;
	}
};

struct metadata_type
{
	static constexpr uint32_t EXIF = 1;
	static constexpr uint32_t IPTC = 2;
	static constexpr uint32_t XMP = 4;
	static constexpr uint32_t ALL = EXIF | IPTC | XMP;
};

///////////////////////////////////////////////////////////////////////////////////////////////////
// Shared mutable globals (defined in tests1.cpp)
///////////////////////////////////////////////////////////////////////////////////////////////////

extern temp_files _temps;
extern std::atomic_int test_version;
extern df::cancel_token test_token;

///////////////////////////////////////////////////////////////////////////////////////////////////
// Shared test context
///////////////////////////////////////////////////////////////////////////////////////////////////

struct shared_test_context
{
	null_async_strategy as;
	location_cache locations;
	index_state test_index;
	index_state empty_index;
	bool loaded = false;

	shared_test_context() : test_index(as, locations), empty_index(as, locations)
	{
	}

	void lazy_load_index();
};

///////////////////////////////////////////////////////////////////////////////////////////////////
// Shared helper functions (defined in tests1.cpp)
///////////////////////////////////////////////////////////////////////////////////////////////////

file_scan_result ff_scan_file(files& ff, df::file_path path, std::string_view xmp_sidecar = {});
file_scan_result ff_scan_and_load_thumb(files& ff, df::file_path path,
                                        std::string_view xmp_sidecar = {});
void assert_metadata(const prop::item_metadata& expected, const prop::item_metadata& actual,
                     std::string_view message = {});
prop::item_metadata_ptr extract_properties(df::file_path path, uint32_t t = metadata_type::ALL);
prop::item_metadata_ptr expected_test_jpg();
prop::item_metadata_ptr metadata_from_cache(index_state& index, df::file_path path);
df::index_file_item make_index_file_info(df::date_t date);
df::item_element_ptr load_item(index_state& index, df::file_path path, bool load_thumb);
std::string_view detect_xmp_sidecar(df::file_path path);
void build_index(index_state& index, database& db);
int count_search_results(index_state& index, const df::search_t& search);
int count_search_results(index_state& index, std::string_view query);
std::shared_ptr<av_player> make_test_player();

///////////////////////////////////////////////////////////////////////////////////////////////////
// Shared prop_test helper for search matching tests
///////////////////////////////////////////////////////////////////////////////////////////////////

class prop_test
{
	df::index_file_item _f;
	uint32_t now_days = df::date_t(2000, 1, 1).to_days();

public:
	prop_test()
	{
		_f.ft = files::file_type_from_name("test.jpg");
	}

	prop_test& tag(const std::string_view s)
	{
		_f.safe_ps()->tags = str::cache(s);
		return *this;
	}

	prop_test& date(const int y, const int m, const int d)
	{
		_f.safe_ps()->created_utc = df::date_t(y, m, d);
		return *this;
	}

	prop_test& file_created_date(const int y, const int m, const int d)
	{
		_f.file_created = df::date_t(y, m, d);
		_f.file_modified = df::date_t(y, m, d);
		return *this;
	}

	prop_test& desc(const std::string_view s)
	{
		_f.safe_ps()->description = str::cache(s);
		return *this;
	}

	prop_test& rate(const int16_t n)
	{
		_f.safe_ps()->rating = n;
		return *this;
	}

	prop_test& is_match(const std::string_view query)
	{
		const auto search = df::search_t::parse(query);
		const df::search_matcher matcher(search, now_days);

		assert_equal(true, matcher.match_all_terms({}, _f).is_match(), query);
		return *this;
	}

	prop_test& is_not_match(const std::string_view query)
	{
		const auto search = df::search_t::parse(query);
		const df::search_matcher matcher(search, now_days);

		assert_equal(false, matcher.match_all_terms({}, _f).is_match(), query);
		return *this;
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////////
// Additional assert_equal overloads for test files
///////////////////////////////////////////////////////////////////////////////////////////////////

static void assert_equal(const ui::orientation expected, const ui::orientation actual,
                         const std::string_view name = {},
                         const std::string_view message = {})
{
	assert_equal(orientation_to_string(expected), orientation_to_string(actual), name, message);
}

static void assert_equal(df::search_term_modifier_bool expected, df::search_term_modifier_bool actual,
                         const std::string_view name = {}, const std::string_view message = {})
{
	assert_equal(static_cast<int>(expected), static_cast<int>(actual), name, message);
}
