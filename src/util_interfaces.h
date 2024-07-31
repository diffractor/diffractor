// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

class LoadJob;

enum class view_type
{
	none,
	items,
	media,
	edit,
	test,
	rename,
	import,
	sync
};

enum class repeat_mode
{
	repeat_none = 0,
	repeat_all,
	repeat_one
};

enum class item_status
{
	success = 0,
	fail,
	ignore,
	cancel
};


enum class async_queue
{
	crc,
	scan_folder,
	scan_modified_items,
	scan_displayed_items,
	work,
	auto_complete,
	cloud,
	load,
	load_raw,
	render,
	query,
	sidebar,
	index,
	index_predictions_single,
	index_summary_single,
	index_presence_single,
	web,
};


const auto thumbnail_quality = 85;


using metadata_kv = std::pair<str::cached, std::u8string>;
using metadata_kv_list = std::vector<metadata_kv>;

namespace df
{
	class search_t;
	class item_element;
	struct index_file_item;

	using item_element_ptr = std::shared_ptr<item_element>;
		
	struct status_i
	{
		virtual ~status_i() = default;
		virtual void start_item(std::u8string_view name) = 0;
		virtual void end_item(std::u8string_view name, item_status status) = 0;

		virtual bool has_failures() const = 0;
		virtual void abort(std::u8string_view error_message) = 0;
		virtual void complete(std::u8string_view message = {}) = 0;
		virtual void show_errors() = 0;

		virtual void message(std::u8string_view message, int64_t pos, int64_t total) = 0;

		virtual void show_message(std::u8string_view message) = 0;
		virtual bool is_canceled() const = 0;
		virtual void wait_for_complete() const = 0;
	};

	using results_ptr = std::shared_ptr<status_i>;

	struct async_i
	{
		virtual void queue_ui(std::function<void()> f) = 0;
		virtual void queue_async(async_queue q, std::function<void()> f) = 0;
	};
};
