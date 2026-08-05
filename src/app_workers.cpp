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

#include "app.h"

#include <utility>

#include "util_log.h"

static std::atomic_int index_version;

platform::thread_event media_preview_event(false, false);
static platform::mutex media_preview_mutex;
static _Guarded_by_(media_preview_mutex) std::function<void(media_preview_state&)> next_media_preview;
static _Guarded_by_(media_preview_mutex) std::deque<std::function<void(media_preview_state&)>> media_preview_must_run;

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

void app_frame::queue_media_preview(std::function<void(media_preview_state&)> f, const bool must_run)
{
	// A superseded preview owns decoded surfaces and item references, so it is released after the
	// lock rather than destroyed inside it.
	std::function<void(media_preview_state&)> superseded;

	{
		platform::exclusive_lock media_lock(media_preview_mutex);

		// Only speculative preview work may be superseded. Teardown must still run: a caller waiting on
		// a dropped close blocks for its whole timeout, and any pending preview is stale once teardown
		// has been requested.
		std::swap(superseded, next_media_preview);

		if (must_run) media_preview_must_run.emplace_back(std::move(f));
		else next_media_preview = std::move(f);
	}

	media_preview_event.set();
}

void app_frame::update_index()
{
	queue_index_update(false);
}

void app_frame::rebuild_index()
{
	queue_index_update(true);
}

void app_frame::queue_index_update(const bool forget_cached_metadata)
{
	auto token = df::cancel_token(index_version);

	index_task_queue.reset_and_enqueue([this, token, forget_cached_metadata]
	{
		// Forgetting runs on this queue, not the caller's, so it cannot interleave with the
		// scan_uncached it is meant to feed.
		if (forget_cached_metadata)
		{
			_state.item_index.forget_cached_metadata();
		}

		_state.item_index.index_roots(index_folders());
		_state.item_index.index_folders(token);
		invalidate_view(view_invalid::sidebar | view_invalid::presence);

		_state.item_index.scan_uncached(token);
		invalidate_view(view_invalid::sidebar | view_invalid::item_scan | view_invalid::presence |
			view_invalid::refresh_items);
	});

	_state.update_search_is_favorite_or_collection_root();
}

static void start_media_decode_video(const std::shared_ptr<av_player>& player)
{
	log_func lf(__FUNCTION__);
	platform::set_thread_description("media decode_video");
	platform::thread_init c;
	player->decode_video();
}

static void start_media_decode_audio(const std::shared_ptr<av_player>& player)
{
	log_func lf(__FUNCTION__);
	platform::set_thread_description("media decode_audio");
	platform::thread_init c;
	platform::media_thread_priority audio_priority;
	player->decode_audio();
}

static void start_media_reading(const std::shared_ptr<av_player>& player)
{
	log_func lf(__FUNCTION__);
	platform::set_thread_description("media read");
	platform::thread_init c;
	player->reading();
}


static void start_database(database& db, platform::task_queue& database_task_queue, async_strategy& async,
                           const app_frame_ptr& app, std::function<void()> index_loaded_func)
{
	log_func lf(__FUNCTION__);
	platform::set_thread_description("database");
	auto* const perf = df::register_queue("database");

	try
	{
		platform::thread_init c;
		db.open(known_path(platform::known_folder::app_cache_data), "diffractor-cache");
		async.queue_ui(std::move(index_loaded_func));

		// The loop runs whether or not the database opened. Its tasks carry the continuations that
		// close dialogs, release thumbnail queries, and answer web-cache lookups, and it is what
		// drains the write queue - which holds encoded thumbnails and would otherwise grow for the
		// whole session against a database that can never accept it.
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
					const auto tasks = database_task_queue.dequeue_all();

					if (!tasks.empty())
					{
						df::bump(perf->batches);
						df::bump(perf->tasks, tasks.size());
						df::record_peak(perf->batch_peak, static_cast<uint32_t>(tasks.size()));
					}

					for (const auto& t : tasks)
					{
						try
						{
							df::perf_timer timer(perf->busy_us, &perf->task_max_us);
							t();
						}
						catch (const std::exception& e)
						{
							df::log(__FUNCTION__, e.what());
						}
					}
				}
				catch (std::exception& e)
				{
					df::log(__FUNCTION__, e.what());
				}
			}

			try
			{
				df::scope_locked_inc l(df::jobs_running);
				db.perform_writes();
			}
			catch (std::exception& e)
			{
				df::log(__FUNCTION__, e.what());
			}
		}

		db.perform_writes();
	}
	catch (std::exception& e)
	{
		df::log(__FUNCTION__, e.what());
		app->app_fail(tt.error_index_database_failed, str::utf8_cast(e.what()));
	}

	// Truncate any tasks still queued at shutdown so their captured references
	// are released while owning objects are still alive.
	try
	{
		(void)database_task_queue.dequeue_all();
	}
	catch (...)
	{
		// Shutdown path: nothing left to report to, and the queue is discarded either way.
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
	// One idle wake drains the queued UI batch, so additional callbacks need no extra context switch.
	if (_ui_queue.enqueue(std::move(f)))
	{
		_pa->queue_idle();
	}
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
		// Not reset_and_enqueue: each batch releases the loading claim its items were marked with on
		// enqueue, so a dropped batch would leave them claimed forever. The cancel token retires
		// superseded batches instead, and measurement says that is cheap - a retired batch breaks on
		// its first token check, so it costs a queue hop rather than a decode. See thumbnail_state.
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
	case async_queue::render_display:
		render_display_task_queue.enqueue(std::move(f));
		break;
	case async_queue::query:
		// Not reset_and_enqueue: this queue carries two unrelated producers. Dropping a pending task
		// would either lose a search's only publication hop (view_state::open) or strand a day in
		// _counting_days forever, so it would never be counted again. Superseded work retires itself
		// instead - a stale search breaks on its cancel token and a stale count on its generation.
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
		auto_complete_task_queue.reset_and_enqueue(std::move(f));
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

void app_frame::queue_tile_db(std::function<void(tile_cache_db&)> f)
{
	tile_db_task_queue.enqueue([f = std::move(f), &db = _tile_db] { f(db); });
}

static void start_media_preview()
{
	log_func lf(__FUNCTION__);
	platform::set_thread_description("media_preview");

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
				std::deque<std::function<void(media_preview_state&)>> must_run;
				std::function<void(media_preview_state&)> f;

				{
					platform::exclusive_lock media_lock(media_preview_mutex);
					std::swap(media_preview_must_run, must_run);
					std::swap(next_media_preview, f);
				}

				// Teardown first: it releases the file handles a waiting caller needs before it can
				// rename, replace or delete.
				// Each task is isolated: this is the only worker draining these queues, so letting one
				// throw past the loop would strand every later teardown with no consumer.
				for (const auto& r : must_run)
				{
					try
					{
						r(preview_decoder);
					}
					catch (const std::exception& e)
					{
						df::log(__FUNCTION__, e.what());
					}
				}

				if (f)
				{
					try
					{
						f(preview_decoder);
					}
					catch (const std::exception& e)
					{
						df::log(__FUNCTION__, e.what());
					}
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

void start_worker(platform::task_queue& q, const std::string_view name)
{
	log_func lf(__FUNCTION__, name);
	platform::set_thread_description(name);

	// One slot per worker thread, so the shared loop can account every queue without knowing which.
	auto* const perf = df::register_queue(name);

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

						if (!tasks.empty())
						{
							df::bump(perf->batches);
							df::bump(perf->tasks, tasks.size());
							df::record_peak(perf->batch_peak, static_cast<uint32_t>(tasks.size()));
						}

						for (const auto& t : tasks)
						{
							try
							{
								df::perf_timer timer(perf->busy_us, &perf->task_max_us);
								t();
							}
							catch (const std::exception& e)
							{
								df::log(__FUNCTION__, e.what());
							}
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

	// On shutdown: truncate any remaining queued tasks. Lambdas hold references
	// (this, view_state, etc.) that may become invalid as teardown progresses;
	// destroying them now while the owning objects are still alive is safe.
	// Tasks holding the last reference to UI-owned state do not die here - ui_owned_ptr hands those
	// back to _ui_queue, which ~app_frame drains on the UI thread after joining this thread.
	try
	{
		(void)q.dequeue_all();
	}
	catch (...)
	{
		// Shutdown path: nothing left to report to, and the queue is discarded either way.
	}
}

void start_map_worker(platform::task_queue& q)
{
	log_func lf(__FUNCTION__, "map");
	platform::set_thread_description("map");
	auto* const perf = df::register_queue("map_tile");

	try
	{
		platform::thread_init c;
		const std::vector<std::reference_wrapper<platform::thread_event>> events = {q._event, platform::event_exit};

		while (!df::is_closing)
		{
			if (wait_for(events, 10000, false) == 0)
			{
				platform::task_queue::task_t task;

				if (q.dequeue(task))
				{
					// Wake another map worker for the next queued tile before this worker blocks on I/O.
					q._event.set();

					try
					{
						df::scope_locked_inc l(df::jobs_running);
						df::bump(perf->batches);
						df::bump(perf->tasks);
						df::perf_timer timer(perf->busy_us, &perf->task_max_us);
						task();
					}
					catch (std::exception& e)
					{
						df::log(__FUNCTION__, e.what());
					}
				}
			}
		}
	}
	catch (std::exception& e)
	{
		df::log(__FUNCTION__, e.what());
	}
}

// The tile store gets its own thread because the SQLite build serialises nothing for us: one
// connection, one thread, and every statement issued from here. Decoding and downloading stay on the
// map workers, so a slow disk delays the next lookup rather than the picture on screen.
static void start_tile_db_worker(tile_cache_db& db, platform::task_queue& queue)
{
	log_func lf(__FUNCTION__, "map_tile_db");
	platform::set_thread_description("map tile database");
	auto* const perf = df::register_queue("map_tile_db");

	try
	{
		platform::thread_init c;
		db.open(resolve_tile_cache_db_path());

		// A cache carried in from earlier sessions is bounded before it is served, not once a few
		// hundred writes have already grown it further.
		db.prune();

		const std::vector<std::reference_wrapper<platform::thread_event>> events = {
			queue._event, platform::event_exit
		};

		while (!df::is_closing)
		{
			if (wait_for(events, 1000, false) == 0)
			{
				df::scope_locked_inc l(df::jobs_running);
				const auto tasks = queue.dequeue_all();

				if (!tasks.empty())
				{
					df::bump(perf->batches);
					df::bump(perf->tasks, tasks.size());
					df::record_peak(perf->batch_peak, static_cast<uint32_t>(tasks.size()));
				}

				for (const auto& t : tasks)
				{
					try
					{
						df::perf_timer timer(perf->busy_us, &perf->task_max_us);
						t();
					}
					catch (std::exception& e)
					{
						df::log(__FUNCTION__, e.what());
					}
				}
			}

			// One commit per drained batch: the accessed stamps a single pan earns are worth one
			// transaction between them, never one each. Runs on the idle tick too, so a batch that
			// is not followed by another is still committed.
			db.flush();
		}
	}
	catch (std::exception& e)
	{
		df::log(__FUNCTION__, e.what());
	}

	try
	{
		(void)queue.dequeue_all();
		db.close();
	}
	catch (std::exception& e)
	{
		df::log(__FUNCTION__, e.what());
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
		// Runs on the UI thread from two completions - the database open and the folder discovery -
		// whichever observes init as complete first. Starting the workers twice would put two
		// consumers on every index queue: concurrent reads against the one disk this pipeline is
		// deliberately serialised on, and concurrent mutation of the shared index by two prediction,
		// summary and presence passes.
		if (!df::is_closing && !_index_workers_started && _state.item_index.is_init_complete())
		{
			_index_workers_started = true;

			auto token = df::cancel_token(index_version);

			index_task_queue.enqueue([this, token]
			{
				_state.item_index.scan_uncached(token);

				invalidate_view(view_invalid::sidebar |
					view_invalid::command_state |
					view_invalid::item_scan |
					view_invalid::presence |
					view_invalid::refresh_items |
					view_invalid::index_summary);
			});

			_threads.start([&q = crc_task_queue] { start_worker(q, "crc"); });
			_threads.start([&q = scan_folder_task_queue] { start_worker(q, "scan_folder"); });
			_threads.start([&q = scan_modified_items_task_queue] { start_worker(q, "scan_modified_items"); });
			_threads.start([&q = scan_displayed_items_task_queue] { start_worker(q, "scan_displayed_items"); });
			_threads.start([&q = predictions_task_queue] { start_worker(q, "predictions"); });
			_threads.start([&q = summary_task_queue] { start_worker(q, "summary"); });
			_threads.start([&q = presence_task_queue] { start_worker(q, "presence"); });
		}

		invalidate_view(view_invalid::sidebar |
			view_invalid::item_scan |
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

	_threads.start([&q = load_task_queue] { start_worker(q, "load"); });
	_threads.start([&q = load_raw_task_queue] { start_worker(q, "load_raw"); });
	_threads.start([&q = render_task_queue] { start_worker(q, "render"); });
	_threads.start([&q = render_display_task_queue] { start_worker(q, "render_display"); });
	_threads.start([&q = query_task_queue] { start_worker(q, "query"); });
	_threads.start([&q = auto_complete_task_queue] { start_worker(q, "auto_complete"); });
	_threads.start([&q = location_task_queue] { start_worker(q, "locations"); });
	_threads.start([&q = sidebar_task_queue] { start_worker(q, "sidebar"); });
	_threads.start([&q = web_task_queue] { start_worker(q, "web"); });
	for (auto i = 0; i < 2; ++i)
	{
		_threads.start([&q = map_tile_task_queue] { start_map_worker(q); });
	}
	_threads.start([&db = _tile_db, &q = tile_db_task_queue] { start_tile_db_worker(db, q); });
	_threads.start([&q = cloud_task_queue] { start_worker(q, "cloud"); });
	_threads.start([&q = index_task_queue] { start_worker(q, "index"); });

	index_task_queue.enqueue([this, scan_uncached_func]
	{
		// Wait for the database cache to finish loading before validating folders. The cache
		// loader (merge_folder) and folder validation (validate_folder) both mutate the same
		// folder nodes; if validation of a large folder races ahead of the loader it writes back
		// nodes with metadata_scanned=0, causing that whole folder to be re-scanned on every
		// startup. Bounded so a failed/absent database cannot hang indexing.
		for (auto i = 0; i < 3000 && !_state.item_index.is_cache_loaded() && !df::is_closing; ++i)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}

		_state.item_index.index_roots(index_folders());

		const auto token = df::cancel_token(index_version);
		_state.item_index.index_folders(token);
		queue_ui(scan_uncached_func);
		invalidate_view(view_invalid::sidebar | view_invalid::command_state | view_invalid::presence |
			view_invalid::index_summary);
	});

	// Issue #119: pick the location display language from the UI language before loading.
	_locations.set_display_language(setting.language);
	location_task_queue.enqueue([this]
	{
		_locations.load_index();
		invalidate_view(view_invalid::sidebar);
	});
}
