// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Application frame management, layout, options, input handling, and queuing.
// Contains UI layout, event processing, input handling, and async queue management.

#include "pch.h"

#include "app_text.h"

#include "model_location.h"
#include "model_db.h"
#include "model_index.h"
#include "model.h"

#include "ui_dialog.h"
#include "view_items.h"
#include "view_list.h"

#include "app.h"

#include <utility>

#include "util_log.h"

static std::atomic_int index_version;

platform::thread_event media_preview_event(false, false);
static platform::mutex media_preview_mutex;
static _Guarded_by_(media_preview_mutex) std::function<void(media_preview_state&)> next_media_preview;

df::index_roots index_folders()
{
	df::index_roots result;
	const auto local_folders = platform::local_folders();

	if (setting.collection.pictures && !local_folders.pictures.is_empty())
		result.folders.emplace(
			local_folders.pictures);
	if (setting.collection.video && !local_folders.video.is_empty()) result.folders.emplace(local_folders.video);
	if (setting.collection.music && !local_folders.music.is_empty()) result.folders.emplace(local_folders.music);
	if (setting.collection.drop_box && !local_folders.dropbox_photos.is_empty())
		result.folders.emplace(
			local_folders.dropbox_photos);
	if (setting.collection.onedrive_pictures && !local_folders.onedrive_pictures.is_empty())
		result.folders.emplace(
			local_folders.onedrive_pictures);
	if (setting.collection.onedrive_video && !local_folders.onedrive_video.is_empty())
		result.folders.emplace(
			local_folders.onedrive_video);
	if (setting.collection.onedrive_music && !local_folders.onedrive_music.is_empty())
		result.folders.emplace(
			local_folders.onedrive_music);

	parse_more_folders(result, setting.collection.more_folders);

	return result;
}

void app_frame::queue_media_preview(std::function<void(media_preview_state&)> f)
{
	platform::exclusive_lock media_lock(media_preview_mutex);
	next_media_preview = std::move(f);
	media_preview_event.set();
}

void app_frame::update_index()
{
	auto token = df::cancel_token(index_version);

	index_task_queue.reset_and_enqueue([this, token]
	{
		_state.item_index.index_roots(index_folders());
		_state.item_index.index_folders(token);
		invalidate_view(view_invalid::sidebar);

		_state.item_index.scan_uncached(token);
		invalidate_view(view_invalid::sidebar | view_invalid::item_scan | view_invalid::refresh_items);
	});

	_state.update_search_is_favorite_or_collection_root();
}

static void start_media_decode_video(const std::shared_ptr<av_player>& player)
{
	log_func lf(__FUNCTION__);
	platform::set_thread_description(u8"media decode_video"sv);
	platform::thread_init c;
	player->decode_video();
}

static void start_media_decode_audio(const std::shared_ptr<av_player>& player)
{
	log_func lf(__FUNCTION__);
	platform::set_thread_description(u8"media decode_audio"sv);
	platform::thread_init c;
	player->decode_audio();
}

static void start_media_reading(const std::shared_ptr<av_player>& player)
{
	log_func lf(__FUNCTION__);
	platform::set_thread_description(u8"media read"sv);
	platform::thread_init c;
	player->reading();
}


static void start_database(database& db, platform::task_queue& database_task_queue, async_strategy& async,
                           const app_frame_ptr& app, std::function<void()> index_loaded_func)
{
	log_func lf(__FUNCTION__);
	platform::set_thread_description(u8"database"sv);

	try
	{
		platform::thread_init c;
		db.open(known_path(platform::known_folder::app_cache_data), u8"diffractor-cache"sv);
		async.queue_ui(std::move(index_loaded_func));

		if (db.is_open())
		{
			async.invalidate_view(view_invalid::refresh_items);
			const std::vector<std::reference_wrapper<platform::thread_event>> events = {
				database_task_queue._event, platform::event_exit
			};

			while (!df::is_closing)
			{
				if (wait_for(events, 1000, false) == 0)
				{
					try
					{
						df::scope_locked_inc l(df::jobs_running);

						for (const auto& t : database_task_queue.dequeue_all())
						{
							t();
						}
					}
					catch (std::exception& e)
					{
						df::log(__FUNCTION__, e.what());
					}
				}
				else
				{
					df::scope_locked_inc l(df::jobs_running);
					db.perform_writes();
				}
			}

			db.perform_writes();
		}
	}
	catch (std::exception& e)
	{
		df::log(__FUNCTION__, e.what());
		app->app_fail(tt.error_index_database_failed, str::utf8_cast(e.what()));
	}

	try
	{
		if (db.is_open())
		{
			db.close();
		}
	}
	catch (std::exception& e)
	{
		df::log(__FUNCTION__, e.what());
		app->app_fail(tt.error_index_database_failed, str::utf8_cast(e.what()));
	}
}

void app_frame::queue_ui(std::function<void()> f)
{
	_ui_queue.enqueue(std::move(f));
	_pa->queue_idle();
}

void app_frame::queue_async(const async_queue q, std::function<void()> f)
{
	switch (q)
	{
	case async_queue::crc:
		crc_task_queue.enqueue(std::move(f));
		break;
	case async_queue::scan_folder:
		scan_folder_task_queue.enqueue(std::move(f));
		break;
	case async_queue::scan_modified_items:
		scan_modified_items_task_queue.enqueue(std::move(f));
		break;
	case async_queue::scan_displayed_items:
		scan_displayed_items_task_queue.enqueue(std::move(f));
		break;
	case async_queue::load:
		load_task_queue.enqueue(std::move(f));
		break;
	case async_queue::load_raw:
		load_raw_task_queue.reset_and_enqueue(std::move(f));
		break;
	case async_queue::render:
		render_task_queue.enqueue(std::move(f));
		break;
	case async_queue::query:
		query_task_queue.enqueue(std::move(f));
		break;
	case async_queue::index_predictions_single:
		predictions_task_queue.reset_and_enqueue(std::move(f));
		break;
	case async_queue::index_summary_single:
		summary_task_queue.reset_and_enqueue(std::move(f));
		break;
	case async_queue::index_presence_single:
		presence_task_queue.reset_and_enqueue(std::move(f));
		break;
	case async_queue::auto_complete:
		auto_complete_task_queue.enqueue(std::move(f));
		break;
	case async_queue::cloud:
		cloud_task_queue.enqueue(std::move(f));
		break;
	case async_queue::index:
		index_task_queue.enqueue(std::move(f));
		break;
	case async_queue::sidebar:
		sidebar_task_queue.enqueue(std::move(f));
		break;
	case async_queue::web:
		web_task_queue.enqueue(std::move(f));
		break;
	case async_queue::map_tile:
		map_tile_task_queue.enqueue(std::move(f));
		break;
	case async_queue::work:
	default:
		work_task_queue.enqueue(std::move(f));
		break;
	}
}

void app_frame::queue_location(std::function<void(location_cache&)> f)
{
	location_task_queue.enqueue([f = std::move(f), &lc = _locations] { f(lc); });
}

void app_frame::queue_database(std::function<void(database&)> f)
{
	database_task_queue.enqueue([f = std::move(f), &db = _db] { f(db); });
}

static void start_media_preview()
{
	log_func lf(__FUNCTION__);
	platform::set_thread_description(u8"media_preview"sv);

	try
	{
		platform::thread_init c;
		const std::vector<std::reference_wrapper<platform::thread_event>> events = {
			media_preview_event, platform::event_exit
		};
		media_preview_state preview_decoder;

		while (!df::is_closing)
		{
			const auto w = wait_for(events, 5000, false);

			if (0 == w)
			{
				std::function<void(media_preview_state&)> f;

				{
					platform::exclusive_lock media_lock(media_preview_mutex);
					std::swap(next_media_preview, f);
				}

				if (f)
				{
					f(preview_decoder);
				}
			}
			else if (platform::wait_for_timeout == w)
			{
				preview_decoder.close();
			}
		}
	}
	catch (std::exception& e)
	{
		df::log(__FUNCTION__, e.what());
	}
}

void start_worker(platform::task_queue& q, const std::u8string_view name)
{
	log_func lf(__FUNCTION__, name);
	platform::set_thread_description(name);

	if (!df::is_closing)
	{
		try
		{
			platform::thread_init c;
			const std::vector<std::reference_wrapper<platform::thread_event>> events = {q._event, platform::event_exit};

			while (!df::is_closing)
			{
				if (wait_for(events, 10000, false) == 0)
				{
					try
					{
						df::scope_locked_inc l(df::jobs_running);
						const auto tasks = q.dequeue_all();

						for (const auto& t : tasks)
						{
							t();
						}
					}
					catch (std::exception& e)
					{
						df::log(__FUNCTION__, e.what());
					}
				}
			}
		}
		catch (std::exception& e)
		{
			df::log(__FUNCTION__, e.what());
		}
	}
}


void app_frame::start_workers()
{
	auto app = shared_from_this();
	async_strategy& async = *this;

	_threads.start([&player = _player] { start_media_decode_video(player); });
	_threads.start([&player = _player] { start_media_decode_audio(player); });
	_threads.start([&player = _player] { start_media_reading(player); });
	_threads.start([] { start_media_preview(); });

	auto scan_uncached_func = [this]
	{
		if (!df::is_closing && _state.item_index.is_init_complete())
		{
			auto token = df::cancel_token(index_version);

			index_task_queue.enqueue([this, token]
			{
				_state.item_index.scan_uncached(token);

				invalidate_view(view_invalid::sidebar |
					view_invalid::command_state |
					view_invalid::item_scan |
					view_invalid::refresh_items |
					view_invalid::index_summary);
			});

			_threads.start([&q = crc_task_queue] { start_worker(q, u8"crc"sv); });
			_threads.start([&q = scan_folder_task_queue] { start_worker(q, u8"scan_folder"sv); });
			_threads.start([&q = scan_modified_items_task_queue] { start_worker(q, u8"scan_modified_items"sv); });
			_threads.start([&q = scan_displayed_items_task_queue] { start_worker(q, u8"scan_displayed_items"sv); });
			_threads.start([&q = predictions_task_queue] { start_worker(q, u8"predictions"sv); });
			_threads.start([&q = summary_task_queue] { start_worker(q, u8"summary"sv); });
			_threads.start([&q = presence_task_queue] { start_worker(q, u8"presence"sv); });
		}

		invalidate_view(view_invalid::sidebar |
			view_invalid::item_scan |
			view_invalid::presence |
			view_invalid::refresh_items |
			view_invalid::command_state |
			view_invalid::index_summary);
	};

	auto index_loaded_func = [this, scan_uncached_func]
	{
		if (!df::is_closing)
		{
			scan_uncached_func();
		}
	};

	_threads.start([&db = _db, &q = database_task_queue, &async, app, index_loaded_func]
	{
		start_database(db, q, async, app, index_loaded_func);
	});

	_threads.start([&q = load_task_queue] { start_worker(q, u8"load"sv); });
	_threads.start([&q = load_raw_task_queue] { start_worker(q, u8"load_raw"sv); });
	_threads.start([&q = render_task_queue] { start_worker(q, u8"render"sv); });
	_threads.start([&q = query_task_queue] { start_worker(q, u8"query"sv); });
	_threads.start([&q = auto_complete_task_queue] { start_worker(q, u8"auto_complete"sv); });
	_threads.start([&q = location_task_queue] { start_worker(q, u8"locations"sv); });
	_threads.start([&q = sidebar_task_queue] { start_worker(q, u8"sidebar"sv); });
	_threads.start([&q = web_task_queue] { start_worker(q, u8"web"sv); });
	_threads.start([&q = map_tile_task_queue] { start_worker(q, u8"map"sv); });
	_threads.start([&q = cloud_task_queue] { start_worker(q, u8"cloud"sv); });
	_threads.start([&q = index_task_queue] { start_worker(q, u8"index"sv); });

	index_task_queue.enqueue([this, scan_uncached_func]
	{
		_state.item_index.index_roots(index_folders());

		const auto token = df::cancel_token(index_version);
		_state.item_index.index_folders(token);
		queue_ui(scan_uncached_func);
		invalidate_view(view_invalid::sidebar | view_invalid::command_state | view_invalid::index_summary);
	});

	location_task_queue.enqueue([&lc = _locations] { lc.load_index(); });
}
