// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Core utility functions and data types. Implements file paths, dates, blobs,
// logging, and other foundational types used throughout the application.

#include "pch.h"
#include "util.h"
#include "model_property.h"

#include "rapidjson/istreamwrapper.h"

df_assert_pod(df::file_size);
df_assert_pod(df::day_range_t);
df_assert_pod(df::day_t);
df_assert_pod(df::date_t);
df_assert_pod(sizei);
df_assert_pod(pointi);
df_assert_pod(recti);
df_assert_pod(sized);
df_assert_pod(pointd);
df_assert_pod(rectd);
df_assert_pod(affined);
df_assert_pod(quadd);
df_assert_pod(df::xy8);
df_assert_pod(df::xy16);
df_assert_pod(df::xy32);
df_assert_pod(df::int_counter);
df_assert_pod(df::file_path);
df_assert_pod(df::folder_path);
df_assert_pod(std::string_view);

df_assert_pod(ui::color);
df_assert_movable(ui::const_image_ptr);
df_assert_movable(ui::const_surface_ptr);

df_assert_pod(df::cspan);
df_assert_pod(df::span);
df_assert_move_only(df::blob);


std::atomic_bool df::is_closing = false;
std::atomic_int df::file_handles_detached = 0;
std::atomic_int df::jobs_running = 0;
std::atomic_int df::loading_media = 0;
std::atomic_int df::command_active = 0;
std::atomic_int df::dragging_items = 0;
std::atomic_int df::handling_crash = 0;
std::atomic<const char*> df::rendering_func = "";

auto df::gpu_desc = "unknown"s;
auto df::gpu_id = "unknown"s;
auto df::d3d_info = "unknown"s;

int df::max_texture_dimension = 16384;
int64_t df::max_texture_bytes = 128ll * 1024ll * 1024ll;
int64_t df::max_decode_bytes = 1024ll * 1024ll * 1024ll;

df::date_t df::start_time;
std::atomic_int df::cancel_token::empty;
df::file_path df::last_loaded_path;

// Portable and per-user installs keep the log beside the executable. Store packages and
// per-machine installs have a read-only install folder, so those fall back to app data.
static df::folder_path resolve_log_folder()
{
	const auto module_folder = known_path(platform::known_folder::running_app_folder);
	return platform::is_writable(module_folder) ? module_folder : known_path(platform::known_folder::app_data);
}

static const df::folder_path s_log_folder = resolve_log_folder();
df::file_path df::log_path = s_log_folder.combine_file("diffractor.log");
df::file_path df::previous_log_path = s_log_folder.combine_file("diffractor.previous.log");

static platform::mutex log_mutex;
_Guarded_by_(log_mutex) static std::ofstream log_file;
_Guarded_by_(log_mutex) static bool log_truncated = false;

// A repeating per-item failure logs once per file, so a long session over a large collection would
// otherwise grow the log without bound. One rotation is kept, so the worst case on disk is twice this.
constexpr std::streamoff max_log_bytes = 4ll * 1024 * 1024;

namespace
{
	size_t ifind_from(const std::string_view text, const std::string_view sub, const size_t from)
	{
		if (from >= text.size()) return std::string::npos;
		const auto found = str::ifind(text.substr(from), sub);
		return found == std::string_view::npos ? std::string::npos : found + from;
	}

	// The log is attached to support reports and crash uploads, so it must not carry the account
	// name. Everything else about a path is kept - the folder shape is what makes a report
	// reproducible. Deliberately pattern-based rather than a platform::user_name() comparison:
	// that call logs on failure, and this runs holding the log lock. It also catches the 8.3 short
	// form (ZACWAL~1) and names embedded in OS and third-party error strings.
	std::string scrub_user_identity(const std::string_view message)
	{
		std::string result(message);

		for (const auto users : {"\\users\\"sv, "/users/"sv})
		{
			for (size_t at = 0; (at = ifind_from(result, users, at)) != std::string::npos;)
			{
				const auto begin = at + users.size();
				auto end = begin;
				while (end < result.size() && result[end] != '\\' && result[end] != '/') ++end;

				if (end == begin)
				{
					at = begin;
					continue;
				}

				constexpr auto token = "%user%"sv;
				result.replace(begin, end - begin, token);
				at = begin + token.size();
			}
		}

		return result;
	}

	// The end-of-session summaries are the highest-value lines in the file and are written last, so
	// they stay exempt from the cap. Each is emitted once per session, so the exemption is bounded.
	bool is_session_summary(const std::string_view context)
	{
		return context.starts_with("perf") || context == "main";
	}
}

void df::log(const std::string_view context, const std::string_view message)
{
	platform::exclusive_lock ll(log_mutex);
	static auto start_time = platform::tick_count();
	static bool did_try_open = false;

#ifdef _DEBUG

	platform::trace(std::format("{}:{}\n", context, message));

#endif

	if (!did_try_open)
	{
		did_try_open = true;

		if (log_path.exists())
		{
			platform::move_file(log_path, previous_log_path, false);
		}

		log_file.open(platform::to_file_system_path(log_path), std::ios::out | std::ios::trunc);
	}

	if (log_file.is_open() && (!log_truncated || is_session_summary(context)))
	{
		const auto time = platform::tick_count() - start_time;
		const auto thread_id = platform::current_thread_id();

		log_file << std::right << std::setfill('0') << std::setw(8) << thread_id
			<< " "
			<< std::setw(8) << time
			<< " "
			<< std::setfill(' ')
			<< std::left << std::setw(33) << context
			<< scrub_user_identity(message) << '\n';

		if (!log_truncated && log_file.tellp() >= max_log_bytes)
		{
			log_truncated = true;
			log_file << "*** log truncated at " << max_log_bytes
				<< " bytes - only end-of-session summaries follow ***\n";
		}
	}
}

void df::trace(const std::string_view message)
{
#ifdef _DEBUG
	platform::trace(std::format("{}\n", message));
#endif
}

// One malformed batch repeats the same codec message for every item scanned. The distinct-message
// ceiling bounds the other direction, where a library emits a message that varies every time.
void df::log_once(const std::string_view context, const std::string_view message)
{
	constexpr size_t max_distinct = 128;

	static platform::mutex mutex;
	_Guarded_by_(mutex) static hash_set<std::string> seen;

	{
		platform::exclusive_lock lock(mutex);
		if (seen.size() >= max_distinct) return;
		if (!seen.emplace(std::format("{}|{}", context, message)).second) return;
	}

	log(context, message);
}

df::thumbnail_counters df::thumbnail_perf;
df::ui_counters df::ui_perf;
df::db_counters df::db_perf;
df::query_counters df::query_perf;
df::file_counters df::file_perf;
df::index_counters df::index_perf;

namespace
{
	constexpr size_t max_perf_queues = 32;
	std::array<df::queue_counters, max_perf_queues> perf_queues;
	std::atomic_size_t perf_queue_count;
	platform::mutex perf_queue_mutex;
	const int64_t perf_start_us = df::now_us();

	std::string format_us(const uint64_t us)
	{
		if (us >= 10'000'000) return std::format("{}s", us / 1'000'000);
		if (us >= 1'000'000) return std::format("{:.1f}s", static_cast<double>(us) / 1'000'000.0);
		if (us >= 1'000) return std::format("{}ms", us / 1'000);
		return std::format("{}us", us);
	}

	// Grouped rather than rounded: cross-checking one counter against another (skipped + coalesced +
	// decodes == requests, for example) is how a measurement defect gets caught, and rounding hides it.
	std::string format_count(const uint64_t n)
	{
		auto digits = std::format("{}", n);
		for (auto i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3)
		{
			digits.insert(i, 1, ',');
		}
		return digits;
	}

	uint64_t load(const std::atomic_uint64_t& v) { return v.load(std::memory_order_relaxed); }
	uint32_t load(const std::atomic_uint32_t& v) { return v.load(std::memory_order_relaxed); }
}

df::queue_counters* df::register_queue(const std::string_view name)
{
	// Runs once per worker thread at startup. Threads that share a queue share its slot, so the
	// summary reports one row per queue rather than one per thread.
	platform::exclusive_lock lock(perf_queue_mutex);
	const auto count = perf_queue_count.load(std::memory_order_relaxed);

	for (size_t i = 0; i < count; ++i)
	{
		if (perf_queues[i].name == name) return &perf_queues[i];
	}

	if (count >= max_perf_queues - 1)
	{
		auto& overflow = perf_queues[max_perf_queues - 1];
		overflow.name = std::string_view("overflow");
		return &overflow;
	}

	perf_queues[count].name = name;
	perf_queue_count.store(count + 1, std::memory_order_relaxed);
	return &perf_queues[count];
}

void df::log_perf_summary()
{
	const auto& t = thumbnail_perf;
	const auto& u = ui_perf;
	const auto& d = db_perf;
	const auto& q = query_perf;
	const auto& f = file_perf;
	const auto& x = index_perf;

	if (load(u.idle_drains) == 0 && load(u.paints) == 0 && load(f.scans) == 0 && load(d.read_batches) == 0)
	{
		return;
	}

	log("perf session", std::format("uptime={} version={}",
	                                format_us(static_cast<uint64_t>(now_us() - perf_start_us)),
	                                format_version(true)));

	log("perf ui", std::format(
		    "idle drains={} tasks={} busy={} max={} peak-batch={} | "
		    "paints={} busy={} max={} | texture-uploads={}",
		    format_count(load(u.idle_drains)), format_count(load(u.idle_tasks)),
		    format_us(load(u.idle_us)), format_us(load(u.idle_max_us)), load(u.idle_batch_peak),
		    format_count(load(u.paints)), format_us(load(u.paint_us)), format_us(load(u.paint_max_us)),
		    format_count(load(u.texture_uploads))));

	const auto queue_count = std::min(perf_queue_count.load(std::memory_order_relaxed), max_perf_queues);

	for (size_t i = 0; i < queue_count; ++i)
	{
		const auto& queue = perf_queues[i];
		const auto tasks = load(queue.tasks);
		if (tasks == 0) continue;

		log("perf queue", std::format("{:<22} tasks={:<8} busy={:<8} max={:<8} batches={} peak-batch={}",
		                              queue.name, format_count(tasks), format_us(load(queue.busy_us)),
		                              format_us(load(queue.task_max_us)), load(queue.batches),
		                              load(queue.batch_peak)));
	}

	if (load(d.read_batches) != 0 || load(d.write_batches) != 0)
	{
		log("perf database", std::format(
			    "reads batches={} thumbs={} in={} max={} | writes batches={} items={} thumbs={} in={} max={}",
			    format_count(load(d.read_batches)), format_count(load(d.thumbnails_read)),
			    format_us(load(d.read_us)), format_us(load(d.read_max_us)),
			    format_count(load(d.write_batches)), format_count(load(d.items_written)),
			    format_count(load(d.thumbs_written)), format_us(load(d.write_us)),
			    format_us(load(d.write_max_us))));
	}

	if (load(q.queries) != 0 || load(q.counts) != 0)
	{
		log("perf query", std::format(
			    "match runs={} in={} max={} items={} | materialize runs={} in={} max={} items={} | "
			    "count runs={} in={}",
			    format_count(load(q.queries)), format_us(load(q.query_us)), format_us(load(q.query_max_us)),
			    format_count(load(q.query_items)),
			    format_count(load(q.materializations)), format_us(load(q.materialize_us)),
			    format_us(load(q.materialize_max_us)), format_count(load(q.materialize_items)),
			    format_count(load(q.counts)), format_us(load(q.count_us))));
	}

	if (load(f.scans) != 0 || load(f.decodes) != 0 || load(f.metadata_errors) != 0)
	{
		log("perf files", std::format(
			    "scans={} in={} max={} | loads={} in={} max={} | decodes={} in={} max={} bytes={} | metadata-errors={}",
			    format_count(load(f.scans)), format_us(load(f.scan_us)), format_us(load(f.scan_max_us)),
			    format_count(load(f.loads)), format_us(load(f.load_us)), format_us(load(f.load_max_us)),
			    format_count(load(f.decodes)), format_us(load(f.decode_us)),
			    format_us(load(f.decode_max_us)), file_size(load(f.decode_bytes)).str(),
			    format_count(load(f.metadata_errors))));
	}

	if (load(x.crc_computed) != 0 || load(x.crc_failed) != 0 || load(x.pass_files) != 0)
	{
		const auto files_walked = static_cast<uint64_t>(load(x.pass_files));
		const auto crc_held = static_cast<uint64_t>(load(x.pass_crc_held));
		const auto crc_pct = files_walked == 0 ? 0 : static_cast<int>((crc_held * 100) / files_walked);

		log("perf crc", std::format(
			    "computed={} failed={} bytes={} in={} max={} | last pass files={} held={} ({}%) dup-groups={}",
			    format_count(load(x.crc_computed)), format_count(load(x.crc_failed)),
			    file_size(load(x.crc_bytes)).str(), format_us(load(x.crc_us)), format_us(load(x.crc_max_us)),
			    format_count(files_walked), format_count(crc_held), crc_pct,
			    format_count(load(x.pass_dup_groups))));
	}

	if (load(x.phash_computed) != 0 || load(x.pass_pictures) != 0)
	{
		// A picture is only supposed to be hashed when another picture shares its capture time, so
		// unpersisted and uninvited are the two numbers that say whether that rule is actually holding.
		log("perf phash", std::format(
			    "computed={} usable={} declined={} unreadable={} | unpersisted={} unwritten={} presence={} | "
			    "bytes={} in={} max={}",
			    format_count(load(x.phash_computed)), format_count(load(x.phash_usable)),
			    format_count(load(x.phash_declined)), format_count(load(x.phash_unreadable)),
			    format_count(load(x.phash_unpersisted)), format_count(load(x.phash_unwritten)),
			    format_count(load(x.phash_presence)),
			    file_size(load(x.phash_bytes)).str(), format_us(load(x.phash_us)),
			    format_us(load(x.phash_max_us))));

		const auto candidates = static_cast<uint64_t>(load(x.pass_candidates));
		const auto uninvited = static_cast<uint64_t>(load(x.pass_uninvited));
		const auto held = static_cast<uint64_t>(load(x.pass_usable_held)) + load(x.pass_declined_held);
		const auto excess_pct = candidates == 0 ? 0 : static_cast<int>((uninvited * 100) / candidates);

		log("perf phash", std::format(
			    "last pass pictures={} capture-times={} candidates={} wanted={} crowded={} matched={} | "
			    "held={} usable={} declined={} uninvited={} ({}% of candidates)",
			    format_count(load(x.pass_pictures)), format_count(load(x.pass_buckets)),
			    format_count(candidates), format_count(load(x.pass_wanted)), format_count(load(x.pass_crowded)),
			    format_count(load(x.pass_matched)), format_count(held), format_count(load(x.pass_usable_held)),
			    format_count(load(x.pass_declined_held)), format_count(uninvited), excess_pct));

		// solo-with-swap is what the gate refused; solo is what a gate blind to rotation would have
		// refused, and the gap between them is what supporting a quarter turn costs.
		log("perf phash", std::format(
			    "shape refused={} (strict would be {}) cross-shape-matches={} dimensions-unknown={}",
			    format_count(load(x.pass_aspect_solo_swap)), format_count(load(x.pass_aspect_solo)),
			    format_count(load(x.pass_matched_cross_aspect)),
			    format_count(load(x.pass_dims_unknown))));
	}

	const auto thumbs_requested = load(t.scan_thumbs_requested);
	const auto thumbs_scanned = load(t.scan_thumbs_scanned);
	const auto decodes = load(t.stage_decodes);
	const auto discarded = load(t.stage_discarded);

	if (load(t.stage_requests) == 0 && thumbs_requested == 0) return;

	// The two ratios the pipeline is tuned against: how much thumbnail work a batch abandons to
	// cancellation, and how many decoded surfaces are thrown away as stale.
	const auto abandoned_pct = thumbs_requested == 0
		                           ? 0
		                           : static_cast<int>(((thumbs_requested - thumbs_scanned) * 100) / thumbs_requested);
	const auto discarded_pct = decodes == 0 ? 0 : static_cast<int>((discarded * 100) / decodes);

	log("perf thumbnails", std::format(
		    "scan batches queued={} run={} cancelled={} peak-pending={} | "
		    "scan thumbs-requested={} scanned={} abandoned={}% stale={}",
		    load(t.scan_batches_queued), load(t.scan_batches), load(t.scan_batches_cancelled),
		    load(t.scan_batches_pending_peak),
		    format_count(thumbs_requested), format_count(thumbs_scanned), abandoned_pct,
		    load(t.scan_completions_stale)));

	log("perf thumbnails", std::format(
		    "stage requests={} skipped={} coalesced={} decodes={} discarded={} ({}%) | "
		    "published db={} shell={} retries={} failures={}",
		    format_count(load(t.stage_requests)), format_count(load(t.stage_skipped)),
		    format_count(load(t.stage_coalesced)), format_count(decodes), format_count(discarded),
		    discarded_pct,
		    format_count(load(t.published_db)), load(t.published_shell), load(t.shell_retries),
		    load(t.load_failures)));
}

df::file_path df::close_log()
{
	platform::exclusive_lock ll(log_mutex);

	if (log_file.is_open())
	{
		log_file.close();
	}

	return log_path;
}

std::string df::file_size::str() const
{
	return prop::format_size(*this);
}

df::version::version(const std::string_view version)
{
	const auto parts = str::split(version, true, [](const wchar_t c) { return c == '.'; });

	if (!parts.empty())
	{
		major = str::to_int(parts[0]);
	}

	if (parts.size() > 1)
	{
		minor = str::to_int(parts[1]);
	}
}

std::string df::url_extract(const std::string_view text)
{
	static const std::regex url_regex(R"((https?:\/\/[^\s\"\']+))");
	std::match_results<std::string_view::const_iterator> match;

	if (std::regex_search(text.begin(), text.end(), match, url_regex))
	{
		return match[1].str();
	}

	return {};
}

std::vector<std::string> df::url_extract_all(const std::string_view text)
{
	static const std::regex url_regex(R"((https?:\/\/[^\s\"\']+))");
	std::vector<std::string> result;

	for (std::regex_iterator<std::string_view::const_iterator> i(text.begin(), text.end(), url_regex), end;
	     i != end; ++i)
	{
		auto found = (*i)[1].str();
		if (std::ranges::find(result, found) == result.end()) result.emplace_back(std::move(found));
	}

	return result;
}

std::string df::date_t::to_xmp_date() const
{
	const auto st = date();
	return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}", st.year, st.month, st.day, st.hour, st.minute,
	                   st.second);
}

df::date_t df::date_t::system_to_local() const
{
	return date_t(platform::utc_to_local(_i));
}

df::date_t df::date_t::local_to_system() const
{
	return date_t(platform::local_to_utc(_i));
}

df::date_t df::date_t::from(const std::string_view r)
{
	date_t ft;
	ft.parse(r);
	return ft;
}

static bool parse_iso_8601_like(const std::string_view r, df::day_t& result)
{
	int yyyy = 0, mm = 0, dd = 0, hh = 0, ss = 0, min = 0;
	constexpr int mmm_len = 4;
	char mmm[mmm_len] = {0};
	double sec = 0.0;
	bool success = false;

	if (!r.empty())
	{
		// Samsung 2005:08:17 11:42:43
		// Canon    THU OCT 26 16:46:04 2006
		//          TUE MAY 08 10:00:1: 2007
		// Fujifilm Mon Mar 3 09:44:56 2008

		char day[4] = {0};
		char month[4] = {0};

		const auto* const source_data = std::bit_cast<const char*>(r.data());
		const auto source_len = r.size();

		if (7 == _snscanf_s(source_data, source_len, "%3s %3s %d %d:%d:%d %d", day, 4, month, 4, &dd, &hh, &min, &ss,
		                    &yyyy) ||
			7 == _snscanf_s(source_data, source_len, "%3s %3s %d %d:%d:%d: %d", day, 4, month, 4, &dd, &hh, &min, &ss,
			                &yyyy))
		{
			mm = str::month(std::bit_cast<const char*>(static_cast<char*>(month)));
			sec = ss;
			success = mm != 0;
		}
		else if (_snscanf_s(source_data, source_len, "%4d-%2d-%2d %2d:%2d:%lg",
		                    &yyyy,
		                    &mm,
		                    &dd,
		                    &hh,
		                    &min,
		                    &sec) == 6)
		{
			success = true;
		}
		else if (_snscanf_s(source_data, source_len, "%4d:%2d:%2d %2d:%2d:%lg",
		                    &yyyy,
		                    &mm,
		                    &dd,
		                    &hh,
		                    &min,
		                    &sec) == 6)
		{
			success = true;
		}
		else if (_snscanf_s(source_data, source_len, "%4d-%2d-%2dT%2d:%2d:%lg",
		                    &yyyy,
		                    &mm,
		                    &dd,
		                    &hh,
		                    &min,
		                    &sec) == 6)
		{
			success = true;
		}
		else if (_snscanf_s(source_data, source_len, "%4d%2d%2dT%2d%2d%lg",
		                    &yyyy,
		                    &mm,
		                    &dd,
		                    &hh,
		                    &min,
		                    &sec) == 6)
		{
			success = true;
		}
		else if (_snscanf_s(source_data, source_len, "%4d%2d%2d%2d%2d%lg",
		                    &yyyy,
		                    &mm,
		                    &dd,
		                    &hh,
		                    &min,
		                    &sec) == 6)
		{
			success = true;
		}
		else if (_snscanf_s(source_data, source_len, "%4d-%2d-%2d", // "2006-01-14"
		                    &yyyy,
		                    &mm,
		                    &dd) == 3)
		{
			hh = 0, ss = 0, min = 0;
			success = true;
		}
		else if (_snscanf_s(source_data, source_len, "%4d-%3s-%2d", // "2006-jan-14"
		                    &yyyy,
		                    mmm,
		                    mmm_len,
		                    &dd) == 3)
		{
			mm = str::month(std::bit_cast<const char*>(static_cast<const char*>(mmm)));
			hh = 0, ss = 0, min = 0;
			success = true;
		}
	}

	if (success)
	{
		result.year = yyyy;
		result.month = mm;
		result.day = dd;
		result.hour = hh;
		result.minute = min;
		result.second = static_cast<int>(floor(sec + 0.1));
	}

	return success;
}

bool df::date_t::parse(const std::string_view text)
{
	day_t d = {0};

	if (parse_iso_8601_like(text, d))
	{
		date(d);
		return true;
	}

	return false;
}

bool df::date_t::parse_exif_date(const std::string_view r)
{
	// "2006:01:14 15:51:31"
	day_t d = {0};
	return parse_iso_8601_like(r, d) && date(d);
}

bool df::date_t::parse_xml_date(const std::string_view r)
{
	// "2006:01:14 15:51:31"
	// "2011-10-03T02:59:13.000000Z"
	// "2019-09-23T18:45:46.309-07:00"
	// "2007-11-09T08:00:00Z"
	// "2006-01-14T15:51:31"
	// "2006-01-14"
	day_t d = {0};
	return parse_iso_8601_like(r, d) && date(d);
}

df::file_size df::file_size::null;


df::file_path df::probe_data_file(const std::string_view file_name)
{
	const auto module_folder = known_path(platform::known_folder::running_app_folder);
	const auto app_data_folder = known_path(platform::known_folder::app_data);
	const auto module_path = module_folder.combine_file(file_name);

	if (module_path.exists())
	{
		return module_path;
	}

	return app_data_folder.combine_file(file_name);
}

df::blob df::blob_from_file(const file_path path, const size_t max_load)
{
	file f;

	if (f.open_read(path, true))
	{
		const auto file_len = f.file_size();
		auto load_len = file_len;

		if (max_load != 0 && load_len > max_load)
		{
			load_len = max_load;
		}

		if (load_len > max_blob_size)
		{
			const auto message = std::format("Cannot read file into memory ({} bytes)", file_len);
			df::log(__FUNCTION__, message);
			throw app_exception(message);
		}

		return f.read_blob(static_cast<size_t>(load_len));
	}

	return {};
}

bool df::blob_save_to_file(const cspan data, const file_path path)
{
	size_t written = 0;
	const auto len = data.size;
	const auto file = open_file(path, platform::file_open_mode::create);

	if (file)
	{
		written = static_cast<size_t>(file->write(data.data, len));
	}

	return written == len;
}

df::util::json::json_doc df::util::json::json_from_file(const file_path path)
{
	std::ifstream ifs(str::utf8_to_utf16(path.str()));
	rapidjson::BasicIStreamWrapper<std::ifstream> isw(ifs);
	json_doc d;
	d.ParseStream(isw);
	return d;
}
