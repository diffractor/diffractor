// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: File indexing engine. Scans folders, extracts metadata, builds searchable
// index, detects duplicates, and manages the in-memory item collection.

#include "pch.h"

#include "model_index.h"
#include "model_db.h"
#include "model_locations.h"
#include "model_property.h"
#include "model.h"
#include "metadata_xmp.h"
#include "util_crash_files_db.h"
#include "util_text.h"

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

constexpr auto max_folders_to_index = 100000;

// Hamming distance across 63 meaningful bits. Every hash sets exactly 31 of them, so a distance is
// always even and only 0, 2, 4 and 6 are distinct settings; 6 is the loosest of the four.
constexpr auto max_duplicate_phash_distance = 6;

// Capture time is recorded to the second and cameras shoot faster than that. A picture that matches
// this many others in the SAME orientation under one timestamp is a burst frame rather than a
// re-save: continuous shooting produces frames that match each other at any threshold. A turned
// match is never a burst frame, so it is not counted here (docs/collections.md section 7.3).
constexpr size_t max_similar_pictures_at_one_capture_time = 2;

// A work bound, not a judgement: past this a capture time is unambiguously continuous shooting, and
// hashing every frame of it would decode a great deal to reach a refusal. Duplicate search and
// presence share it, so neither can report a set the other cannot see.
constexpr size_t max_photos_sharing_capture_time = 8;

// Hashing reads and decodes, so a pass asks for a bounded batch and the pass that follows asks for
// the next. A large collection converges over several rounds rather than stalling on the first.
constexpr size_t max_phash_requests_per_pass = 256;

// A picture worth this much I/O to identify. Beyond it the read costs more than the answer is worth.
constexpr uint64_t max_phash_file_bytes = 128ull * 1024ull * 1024ull;

// Shared by duplicate search and presence, so neither can claim a copy the other denies. Compared by
// aspect rather than extent, so a resize still counts. The tolerance is an absolute block rather than
// a percentage because lossless JPEG rotation trims to the MCU grid: a 1024x683 photograph turns into
// 672x1024, not 683x1024, and a percentage tight enough to be useful on a large picture would reject
// that. A quarter turn transposes the stored extent, so a transposed shape counts when rotations are
// allowed. An unknown shape is not a different shape, so a picture with no stored extent is never
// refused on this ground.
static bool same_picture_shape(const sizei a, const sizei b, const bool allow_swap = true)
{
	if (a.is_empty() || b.is_empty()) return true;

	const auto close = [](const sizei left, const sizei right)
	{
		constexpr double mcu_block = 16.0;
		const auto left_aspect = static_cast<double>(left.cx) / left.cy;
		const auto right_aspect = static_cast<double>(right.cx) / right.cy;
		const auto shortest = std::min({left.cx, left.cy, right.cx, right.cy});
		const auto tolerance = mcu_block / shortest + 0.01;
		return std::abs(left_aspect - right_aspect) <= std::max(left_aspect, right_aspect) * tolerance;
	};

	if (close(a, b)) return true;

	return allow_swap && close(a, {b.cy, b.cx});
}

// A row written before the quarter turns were stored carries the first hash alone. Reporting it as
// unhashed asks for the other three, rather than leaving a rotated copy permanently unrecognisable.
static df::picture_hashes_ptr picture_hashes_from_db(const crypto::phash_rotations& rotations)
{
	if (rotations[0] == 0) return nullptr;
	if (rotations[0] == crypto::phash_declined) return df::make_picture_hashes(rotations);

	const auto complete = std::ranges::all_of(rotations, [](const uint64_t h) { return crypto::phash_is_usable(h); });
	return complete ? df::make_picture_hashes(rotations) : nullptr;
}

df_assert_pod(df::file_path);df_assert_pod(df::file_group_histogram);
df_assert_pod(search_presence_mask);
df_assert_pod(key_val);
df_assert_movable(df::index_file_item);
df_assert_movable(folder_scan_item);
df_assert_movable(index_state::query_item);
df_assert_movable(index_state::item_scan_request);
df_assert_movable(index_state::thumbnail_result);
df_assert_movable(index_state::validate_folder_result);

static_assert(sizeof(search_presence_mask) == 4);
static_assert(sizeof(df::file_path) == sizeof(str::cached) * 2);
static_assert(sizeof(key_val) == sizeof(str::cached) * 2);

// Every query walks these fields once per candidate. A type that outgrows a machine word makes
// std::atomic fall back to a lock, which x64 hides for 16 bytes and ARM64 does not, so a widened
// field would turn the hot index record into a contended mutex rather than fail here.
static_assert(std::atomic<df::date_t>::is_always_lock_free);
static_assert(std::atomic<df::duplicate_info>::is_always_lock_free);
static_assert(std::atomic<search_presence_mask>::is_always_lock_free);

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

struct dup_key
{
	str::cached name = {};
	uint64_t created = 0;

	int compare(const dup_key& other) const
	{
		const auto name_diff = icmp(name, other.name);
		if (name_diff != 0) return name_diff;
		if (created < other.created) return -1;
		if (created > other.created) return 1;
		return 0;
	}

	uint32_t calc_hash() const
	{
		return crypto::hash_gen(name).append(created).result();
	}
};

struct dup_index_hash
{
	size_t operator()(const dup_key& i) const
	{
		return i.calc_hash();
	}

	bool operator()(const uint32_t i) const
	{
		return i;
	}
};

struct dup_index_eq
{
	bool operator()(const dup_key& l, const dup_key& r) const
	{
		return l.compare(r) == 0;
	}

	bool operator()(const uint32_t l, const uint32_t r) const
	{
		return l == r;
	}
};

static auto next_dup_group = 1000u;

static __forceinline uint32_t x64to32(const uint64_t n)
{
	constexpr uint64_t fnv_prime = 1099511628211u;
	constexpr uint64_t fnv_offset_basis = 14695981039346656037u;
	const auto result = (fnv_offset_basis ^ n) * fnv_prime;
	return static_cast<uint32_t>(result);
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

struct query_items_result
{
	index_state::query_item_results results;

	void match_item(const df::file_path id, const df::index_file_item& file, const df::search_result& match)
	{
		results.push_back({index_state::query_item_kind::file, id, file, {}, {}, match});
	}

	void match_folder(const df::folder_path folder_path, const df::index_folder_item_ptr& folder)
	{
		results.push_back({index_state::query_item_kind::folder, {}, {}, folder_path, folder, {}});
	}
};

struct count_items_result
{
	df::file_group_histogram summary;

	void match_item(const df::file_path id, const df::index_file_item& file, const df::search_result& match)
	{
		summary.record(file, id);
	}

	static void match_folder(const df::folder_path folder_path, const df::index_folder_item_ptr& folder)
	{
	}
};

// A related search written as text - typed, or restored from a favorite or saved search -
// carries only the path, so every field the relation axes compare is recovered from the
// index before any matching happens. Files outside the collection fall back to the path.
static const df::search_t& resolve_related(const df::search_t& search, const index_state& state,
                                           df::search_t& storage)
{
	if (!search.has_related())
	{
		return search;
	}

	const auto path = search.related().path;

	if (search.related().is_loaded)
	{
		// The snapshot was taken when the command ran, but duplicate grouping is recomputed behind it
		// and is what the strongest relation is decided by. Only that one field is refreshed, and
		// only from the index, which owns it.
		const auto found = state.find_item(path);
		const auto group = found.ft ? found.duplicates.load().group : 0;

		if (group == 0 || group == search.related().group)
		{
			return search;
		}

		df::related_info r = search.related();
		r.group = group;

		storage = search;
		storage.related(r);
		return storage;
	}

	const auto found = state.find_item(path);

	df::related_info r;
	r.path = path;

	if (found.ft)
	{
		r.name = found.name;
		r.size = found.size;
		r.file_created = found.file_created;
		r.ft = found.ft;
		r.crc32c = found.crc32c.load();
		r.group = found.duplicates.load().group;

		const auto md = found.metadata.load();

		if (md)
		{
			r.gps = md->coordinate;
			r.metadata_created = md->created();
			r.album = md->album;
			r.album_artist = md->album_artist;
			r.show = md->show;
			r.season = md->season;
			r.episode = md->episode;
			r.disk = md->disk;
			r.track = md->track;
		}
	}
	else
	{
		r.name = str::cache(path.name());
		r.ft = files::file_type_from_name(path);
	}

	r.is_loaded = true;

	storage = search;
	storage.related(r);
	return storage;
}

template <typename T>
static void iterate_items(const df::search_t& search_in,
                          T& results,
                          index_state& state,
                          df::cancel_token token,
                          index_items& index,
                          bool refresh_from_file_system,
                          bool show_sidecars)
{
	df::search_t resolved_related;
	const auto& search = resolve_related(search_in, state, resolved_related);

	df::search_matcher matcher(search, platform::now().to_days(), &state.locations());

	const auto now = platform::now();
	const auto& selectors = search.selectors();
	const auto has_selector = !selectors.empty();
	const auto has_related = search.has_related();

	// A related search answers with the closest matches on each axis, so its results are collected
	// and ranked here rather than reported as they are found. Counting shares this path so a count
	// can never disagree with the set the same search displays.
	struct related_payload
	{
		df::index_file_item file;
		df::search_result match;
	};

	df::related_collector<related_payload> related_slots;

	// Two selectors can name overlapping trees - `folders_scanned` only stops one selector walking a
	// folder twice - and a file matched by both is still one file. The set is only paid for when
	// there is more than one selector to overlap, and folders cannot repeat within a single walk.
	df::unique_paths emitted;
	df::unique_folders emitted_folders;
	const auto selectors_can_overlap = selectors.size() > 1;

	const auto report = [&results, &related_slots, &emitted, has_related, selectors_can_overlap](
		const df::file_path id,
		const df::index_file_item& file,
		const df::search_result& match)
	{
		if (selectors_can_overlap && !emitted.emplace(id).second)
		{
			return;
		}

		if (has_related)
		{
			related_slots.offer({df::related_axis_of(match.type), match.distance}, id, {file, match});
		}
		else
		{
			results.match_item(id, file, match);
		}
	};

	const auto report_folder = [&results, &emitted_folders, selectors_can_overlap](
		const df::folder_path path, const df::index_folder_item_ptr& folder)
	{
		if (selectors_can_overlap && !emitted_folders.emplace(path).second)
		{
			return;
		}

		results.match_folder(path, folder);
	};

	if (has_selector)
	{
		for (const auto& selector : selectors)
		{
			if (token.is_cancelled())
				break;

			const auto recursive = selector.is_recursive();
			const auto wildcard = selector.wildcard();

			files ff;
			std::vector<df::folder_path> folders = {selector.folder()};
			df::unique_folders folders_scanned;

			while (!folders.empty())
			{
				if (token.is_cancelled()) break;

				const auto current_folder = folders.back();
				folders.pop_back();

				if (!folders_scanned.contains(current_folder))
				{
					folders_scanned.emplace(current_folder);

					const auto found_node = state.validate_folder(current_folder, refresh_from_file_system, now);

					if (found_node.folder)
					{
						for (const auto& folder_entry : *found_node.folder->folders_snapshot())
						{
							if (token.is_cancelled()) break;

							const auto folder_path = current_folder.combine(folder_entry->name);

							if (!selector.has_wildcard() || wildcard_icmp(folder_entry->name, wildcard))
							{
								if (!folder_entry->is_excluded)
								{
									if (recursive)
									{
										folders.emplace_back(folder_path);
									}

									if ((!recursive && !matcher.has_terms) || matcher.match_folder(
										folder_path.text(), folder_path.name()).is_match())
									{
										report_folder(folder_path, folder_entry);
									}
								}
							}
						}

						if (matcher.need_metadata)
						{
							for (const auto& f : found_node.folder->files)
							{
								if (token.is_cancelled())
									break;

								state.scan_item(found_node.folder, {current_folder, f.name}, false, false, false, false,
								                {}, false, f.ft);
							}
						}

						if (matcher.can_contain(found_node.folder->search_presence_summary))
						{
							for (const auto& file_node : found_node.folder->files)
							{
								if (token.is_cancelled())
									break;

								const auto& file = file_node;

								if (!selector.has_wildcard() || wildcard_icmp(file_node.name, wildcard))
								{
									if (!(file.flags && df::index_item_flags::is_sidecar) || show_sidecars)
									{
										const auto id = current_folder.combine_file(file_node.name);
										const auto match = matcher.match_item(id, file);

										if (match.is_match())
										{
											report(id, file, match);
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}
	else
	{
		const auto folders = index.all_folders();

		for (const auto& folder_node : folders)
		{
			if (token.is_cancelled()) break;

			if (folder_node.second->is_in_collection)
			{
				// Folder masks are an early rejection only; each surviving item is checked
				// against its own mask and then by the authoritative exact matcher.
				if (has_related ||
					matcher.can_match_folder ||
					matcher.can_contain(folder_node.second->search_presence_summary))
				{
					for (const auto& file_node : folder_node.second->files)
					{
						if (token.is_cancelled()) break;

						if (!(file_node.flags && df::index_item_flags::is_sidecar) || show_sidecars)
						{
							const auto path = folder_node.first.combine_file(file_node.name);
							const auto match = matcher.match_item(path, file_node);

							if (match.is_match())
							{
								report(path, file_node, match);
							}
						}
					}
				}

				const auto match = matcher.match_folder(folder_node.first.text(), folder_node.first.name());

				if (match.is_match())
				{
					results.match_folder(folder_node.first, folder_node.second);
				}
			}
		}
	}

	if (has_related && !token.is_cancelled())
	{
		related_slots.drain([&results](const df::file_path id, related_payload&& payload)
		{
			results.match_item(id, payload.file, payload.match);
		});
	}
}

void index_state::query_items(const df::search_t& search,
                              const std::function<void(query_item_results, bool)>& found_callback,
                              const df::cancel_token& token)
{
	df::scope_locked_inc l(searching);

	_async.invalidate_view(view_invalid::view_layout);

	if (search.has_related())
	{
		record_feature_use(features::search_related);
	}

	if (search.is_duplicates())
	{
		record_feature_use(features::search_duplicates);
	}

	const auto selectors = search.selectors();
	const auto has_selector = !selectors.empty();

	if (has_selector)
	{
		record_feature_use(features::search_folder);

		for (const auto& selector : selectors)
		{
			if (selector.is_recursive())
			{
				record_feature_use(features::search_flatten);
			}
		}
	}

	query_items_result results;
	{
		df::bump(df::query_perf.queries);
		df::perf_timer timer(df::query_perf.query_us, &df::query_perf.query_max_us);
		iterate_items(search, results, *this, token, _items, true, search.has_related());
	}

	df::bump(df::query_perf.query_items, results.results.size());

	if (search.has_related())
	{
		const auto id = search.related().path;
		const auto folder = _items.find(id.folder());
		if (!(folder && folder->is_in_collection))
		{
			// The item the search started at is outside the collection, so the scan never saw it. It
			// still leads its own answer, which is what the negative distance orders it to.
			df::search_result anchor(df::search_result_type::similar);
			anchor.distance = -1;

			results.results.push_back({query_item_kind::existing, id, {}, {}, {}, anchor});
		}
	}

	if (search.has_term_type(df::search_term_type::text))
	{
		record_feature_use(features::search_text);
	}

	if (search.has_term_type(df::search_term_type::value))
	{
		record_feature_use(features::search_property);
	}

	if (search.has_term_type(df::search_term_type::has_type))
	{
		record_feature_use(features::search_type);
	}

	found_callback(std::move(results.results), true);
}

df::item_set index_state::materialize_query_items(query_item_results items, const df::unique_items& existing) const
{
	df::assert_true(ui::is_ui_thread());
	df::bump(df::query_perf.materializations);
	df::bump(df::query_perf.materialize_items, items.size());
	df::perf_timer timer(df::query_perf.materialize_us, &df::query_perf.materialize_max_us);
	df::item_set results;
	results.reserve(items.size());

	for (auto& result : items)
	{
		df::item_element_ptr item;
		if (result.kind == query_item_kind::file)
		{
			item = existing.find(result.path);
			if (item)
			{
				item->update(result.path, result.file);
			}
			else
			{
				item = std::make_shared<df::item_element>(result.path, result.file);
			}
			item->search(result.match);
		}
		else if (result.kind == query_item_kind::folder)
		{
			item = existing.find(result.folder_path);
			if (!item)
			{
				item = std::make_shared<df::item_element>(result.folder_path, std::move(result.folder));
			}
		}
		else
		{
			item = existing.find(result.path);
			if (item) item->search(result.match);
		}

		if (item) results.add(item);
	}

	return results;
}

df::file_group_histogram index_state::count_matches(const df::search_t& search, const df::cancel_token& token)
{
	count_items_result result;

	if (is_collection_search(search))
	{
		df::measure_ms ms(stats.count_matches_ms);
		df::bump(df::query_perf.counts);
		df::perf_timer timer(df::query_perf.count_us);
		iterate_items(search, result, *this, token, _items, false, search.has_related());
	}

	return result.summary;
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

index_state::index_state(async_strategy& as, const location_cache& locations) : _async(as), _locations(locations)
{
}

void index_state::enqueue_db_write(item_db_write write)
{
	if (_db_writes.enqueue(std::move(write)))
	{
		// The database worker flushes writes after every task batch. One no-op task on the
		// empty-to-nonempty transition gives the write queue an explicit wake without flooding it.
		_async.queue_database([](database&)
		{
		});
	}
}

void index_state::enqueue_db_writes(std::vector<item_db_write> writes)
{
	if (writes.empty()) return;

	// The worker drains the write queue on every pass, so a producer that enqueues one row at a time
	// always finds it empty and wakes the database thread once per row - and each of those wakes opens
	// its own transaction. Handing over the whole group keeps it to one wake and one transaction.
	if (_db_writes.enqueue_all(std::move(writes)))
	{
		_async.queue_database([](database&)
		{
		});
	}
}

void index_state::init_item_index()
{
	auto summary = std::make_shared<index_metadata_summary>();

	for (const auto& f : all_file_groups())
	{
		++summary->_distinct_words[str::cache(std::format("@{}", f->name))];
		++summary->_distinct_words[str::cache(std::format("@{}", f->plural_name))];
	}

	summary->_distinct_text[prop::genre] = df::dense_unique_strings
	{
		"Abstract"_c,
		"Action & Adventure"_c,
		"Action"_c,
		"Aerial"_c,
		"Alternative"_c,
		"Analog"_c,
		"Animation"_c,
		"Anime"_c,
		"Architectural"_c,
		"Aviation"_c,
		"Blues"_c,
		"Brazilian"_c,
		"Candid"_c,
		"Children's"_c,
		"Christian & Gospel"_c,
		"Classic"_c,
		"Classical"_c,
		"Close-up"_c,
		"Cloudscape"_c,
		"Comedy"_c,
		"Conceptual"_c,
		"Concert Films"_c,
		"Concert"_c,
		"Conservation"_c,
		"Country"_c,
		"Dance"_c,
		"Documentary"_c,
		"Drama"_c,
		"Easy Listening"_c,
		"Electronic"_c,
		"Family"_c,
		"Fashion"_c,
		"Film Noir"_c,
		"Film still"_c,
		"Fine-art"_c,
		"Fire"_c,
		"Fireworks"_c,
		"Fitness & Workout"_c,
		"Food"_c,
		"Foreign"_c,
		"Forensic"_c,
		"Geophotography"_c,
		"Glamour"_c,
		"High key"_c,
		"High-speed"_c,
		"Hip-Hop/Rap"_c,
		"Holiday"_c,
		"Horror"_c,
		"Independent"_c,
		"Instrumental"_c,
		"Jazz"_c,
		"Kids & Family"_c,
		"Kids"_c,
		"Kirlian"_c,
		"Landscape"_c,
		"Latin"_c,
		"Lifestyle"_c,
		"Lo-fi"_c,
		"Lomography"_c,
		"Long-exposure"_c,
		"Low key"_c,
		"Macro"_c,
		"Medical"_c,
		"Monochrome"_c,
		"Music Documentaries"_c,
		"Music Feature Films"_c,
		"Musicals"_c,
		"Narrative"_c,
		"New Age"_c,
		"Night"_c,
		"Nonfiction"_c,
		"Opera"_c,
		"Panorama"_c,
		"Panoramic"_c,
		"Photo op"_c,
		"Photobiography"_c,
		"Photojournalism"_c,
		"Photowalking"_c,
		"Podcast"_c,
		"Polaroid"_c,
		"Pop"_c,
		"Portrait"_c,
		"R&B"_c,
		"Reality TV"_c,
		"Reggae"_c,
		"Rock"_c,
		"Romance"_c,
		"Satellite"_c,
		"Sci-Fi & Fantasy"_c,
		"Short Films"_c,
		"Singer/Songwriter"_c,
		"Social"_c,
		"Soft focus"_c,
		"Soul"_c,
		"Soundtrack"_c,
		"Special Interest"_c,
		"Sports"_c,
		"Star trail"_c,
		"Still life"_c,
		"Stock"_c,
		"Street"_c,
		"Subminiature"_c,
		"Teens"_c,
		"Thriller"_c,
		"Time-lapse"_c,
		"Travel"_c,
		"Ultraviolet"_c,
		"Underwater"_c,
		"Urban"_c,
		"Vernacular"_c,
		"Vintage"_c,
		"Vocal"_c,
		"War"_c,
		"Western"_c,
		"World"_c
	};

	rebuild_sorted_words(*summary);

	index_metadata_summary_const_ptr published = std::move(summary);
	{
		platform::exclusive_lock lock(_summary_rw);
		_summary._metadata.swap(published);
	}
}

void index_state::reset()
{
	_items.clear();
}

void index_state::add_distinct_other_folders(df::unique_folders folders)
{
	for (;;)
	{
		std::shared_ptr<const df::unique_folders> current;
		{
			platform::shared_lock lock(_summary_rw);
			current = _summary._distinct_other_folders;
		}

		if (std::ranges::all_of(folders, [&](const auto& folder) { return current->contains(folder); }))
		{
			return;
		}

		// Build the immutable replacement outside the write lock, then publish only if current is unchanged.
		auto next = std::make_shared<df::unique_folders>(folders);
		next->insert(current->begin(), current->end());

		{
			std::shared_ptr<const df::unique_folders> published = next;
			platform::exclusive_lock lock(_summary_rw);
			if (_summary._distinct_other_folders == current)
			{
				_summary._distinct_other_folders.swap(published);
				return;
			}
		}
	}
}

void index_state::invalidate_view(const view_invalid invalid) const
{
	_async.invalidate_view(invalid);
}


bool index_state::is_in_collection(const df::folder_path folder) const
{
	const auto parent = folder.parent();

	{
		platform::shared_lock lock(_summary_rw);

		if (_summary._roots.folders.contains(folder) ||
			_summary._roots.folders.contains(parent))
		{
			return true;
		}
	}

	const auto found_folder = _items.find(folder);

	if (found_folder)
	{
		return found_folder->is_in_collection;
	}

	const auto found_parent = _items.find(parent);

	if (found_parent)
	{
		return found_parent->is_in_collection;
	}

	return false;
}

static df::index_item_infos::iterator find_file(df::index_item_infos& files, const std::string_view name)
{
	const auto lb = std::lower_bound(files.begin(), files.end(), name);
	if (lb != files.end() && *lb == name) return lb;
	return files.end();
}

static df::index_item_infos::const_iterator find_file(const df::index_item_infos& files, const std::string_view name)
{
	const auto lb = std::lower_bound(files.begin(), files.end(), name);
	if (lb != files.end() && *lb == name) return lb;
	return files.end();
}

static df::index_folder_item_ptr find_or_create_folder(index_items& items, const df::folder_path path,
                                                       const platform::folder_info& fd)
{
	df::assert_true(!is_empty(fd.name));

	auto candidate = std::make_shared<df::index_folder_item>();
	candidate->name = fd.name;
	candidate->modified = fd.attributes.modified;
	candidate->created = fd.attributes.created;
	candidate->is_read_only = fd.attributes.is_readonly;
	return items.find_or_create(path, std::move(candidate));
}

void populate_file_info(df::index_file_item& file_node, const platform::file_info& fd, const bool cache_items_loaded)
{
	df::assert_true(!is_empty(fd.name));

	const auto name = fd.name;
	file_node.file_modified = df::date_t(fd.attributes.modified);
	file_node.file_created = fd.attributes.created;
	// Set or CLEAR these flags to reflect the current filesystem state. Clearing matters
	// for cloud-only placeholders: when OneDrive hydrates a file (offline -> online) the
	// is_offline flag must drop so the file is re-scanned via the normal (thumbnail) path.
	if (fd.attributes.is_readonly) file_node.flags |= df::index_item_flags::is_read_only;
	else file_node.flags &= ~df::index_item_flags::is_read_only;
	if (fd.attributes.is_offline) file_node.flags |= df::index_item_flags::is_offline;
	else file_node.flags &= ~df::index_item_flags::is_offline;
	file_node.size = fd.attributes.size;

	auto md = file_node.metadata.load();

	if (file_node.name != name)
	{
		file_node.name = name;
		file_node.ft = files::file_type_from_name(name);

		if (cache_items_loaded &&
			md == nullptr &&
			file_node.ft->has_trait(file_traits::file_name_metadata))
		{
			const auto ext_pos = df::find_ext(name);

			if (ext_pos != std::string_view::npos)
			{
				md = std::make_shared<prop::item_metadata>();
				const auto name_props = scan_info_from_title(name.substr(0, ext_pos));

				if (!str::is_empty(name_props.show)) md->show = str::cache(name_props.show);
				if (!str::is_empty(name_props.title)) md->title = str::cache(name_props.title);
				if (name_props.year != 0) md->year = name_props.year;
				if (name_props.episode != 0) md->episode = df::xy8::make(name_props.episode, name_props.episode_of);
				if (name_props.season != 0) md->season = name_props.season;
				md->file_name = name;
				file_node.metadata.store(md);
			}
		}
	}

	if (md && md->file_name.sv() != name)
	{
		auto updated = std::make_shared<prop::item_metadata>(*md);
		updated->file_name = name;
		file_node.metadata.store(std::move(updated));
	}
}

index_state::validate_folder_result index_state::validate_folder(const df::folder_path folder_path,
                                                                 const bool refresh_from_file_system,
                                                                 const df::date_t timestamp)
{
	auto existing_folder = _items.find(folder_path);

	if (refresh_from_file_system || !existing_folder)
	{
		bool changes_detected = false;

		df::index_item_infos updated_files;
		df::index_folder_infos updated_folders;
		std::vector<df::folder_path> removed_folders;
		std::unordered_multimap<std::string_view, str::cached, df::ihash, df::ieq> sidecars;
		df::hash_set<std::string_view, df::ihash, df::ieq> sidecar_extensions;

		auto less_ptr_name = [](const auto& a, const auto& b) { return str::icmp(a->name, b->name) < 0; };
		auto less_name = [](const auto& a, const auto& b) { return str::icmp(a.name, b.name) < 0; };
		auto less_id = [](const auto& a, const auto& b) { return str::icmp(a.name, b.name) < 0; };

		auto contents = platform::iterate_file_items(folder_path, setting.show_hidden);

		if (!contents.success)
		{
			// Enumeration failed (offline volume, denied access, network drop). Rebuilding from
			// the empty listing would erase this branch of the index and expire its database rows.
			add_distinct_other_folders({folder_path});
			return {existing_folder, false};
		}

		updated_folders.reserve(contents.folders.size());
		updated_files.reserve(contents.files.size());

		std::ranges::sort(contents.folders, less_name);
		std::ranges::sort(contents.files, less_name);

		auto folder_first = contents.folders.begin();
		const auto folder_last = contents.folders.end();

		if (existing_folder)
		{
			const auto existing_folders = existing_folder->folders_snapshot();
			df::assert_true(std::ranges::is_sorted(contents.folders, less_name));
			df::assert_true(std::ranges::is_sorted(*existing_folders, less_ptr_name));

			if (contents.files.size() != existing_folder->files.size() ||
				contents.folders.size() != existing_folders->size())
			{
				changes_detected = true;
			}

			auto old_first = existing_folders->begin();
			const auto old_last = existing_folders->end();

			while (folder_first != folder_last && old_first != old_last)
			{
				const auto d = icmp(folder_first->name, (*old_first)->name);

				if (d < 0)
				{
					// create: only in new
					updated_folders.emplace_back(
						find_or_create_folder(_items, folder_path.combine(folder_first->name), *folder_first));
					changes_detected = true;
					++folder_first;
				}
				else if (d > 0)
				{
					// remove: only in old
					removed_folders.emplace_back(folder_path.combine((*old_first)->name));
					changes_detected = true;
					++old_first;
				}
				else
				{
					// copy: in both
					auto index_folder_item = find_or_create_folder(_items, folder_path.combine(folder_first->name),
					                                               *folder_first);
					updated_folders.emplace_back(index_folder_item);
					if (*old_first != index_folder_item) changes_detected = true;
					++folder_first;
					++old_first;
				}
			}
		}
		else
		{
			changes_detected = true;
		}

		while (folder_first != folder_last)
		{
			// only in new
			updated_folders.emplace_back(
				find_or_create_folder(_items, folder_path.combine(folder_first->name), *folder_first));
			changes_detected = true;
			++folder_first;
		}

		auto file_first = contents.files.begin();
		const auto file_last = contents.files.end();

		if (existing_folder)
		{
			auto old_first = existing_folder->files.begin();
			const auto old_last = existing_folder->files.end();

			while (file_first != file_last && old_first != old_last)
			{
				const auto d = icmp(file_first->name, old_first->name);

				if (d < 0)
				{
					// create: only in new
					df::index_file_item info;
					populate_file_info(info, *file_first, _cache_items_loaded);
					updated_files.emplace_back(info);
					changes_detected = true;
					++file_first;
				}
				else if (d > 0)
				{
					// remove: only in old
					changes_detected = true;
					++old_first;
				}
				else
				{
					if (old_first->file_modified != df::date_t(file_first->attributes.modified))
					{
						changes_detected = true;
					}

					// copy: in both
					df::index_file_item info = *old_first;
					info.metadata.store(old_first->metadata);
					info.metadata_scanned = old_first->metadata_scanned.load();
					info.crc32c = old_first->crc32c.load();
					info.phash = old_first->phash.load();

					if (old_first->file_modified != df::date_t(file_first->attributes.modified) ||
						old_first->size.to_int64() != static_cast<int64_t>(file_first->attributes.size))
					{
						// The bytes changed, so a hash of the previous bytes is no longer a description of
						// this file. Clearing it is what asks the displayed-item worker to compute it again;
						// carrying it forward would report an edited file and an untouched copy as identical.
						info.crc32c = 0;
					}

					const auto was_offline = old_first->flags && df::index_item_flags::is_offline;
					populate_file_info(info, *file_first, _cache_items_loaded);
					const auto now_offline = info.flags && df::index_item_flags::is_offline;

					if (was_offline != now_offline)
					{
						// The cloud (offline) status flipped. OneDrive hydration preserves the
						// file's modified time, so this is often the ONLY change -- it must mark the
						// folder dirty, otherwise the updated node (with the cleared is_offline flag)
						// is discarded and the file is never re-scanned online.
						changes_detected = true;
					}

					if (was_offline && !now_offline)
					{
						// A cloud-only placeholder was just hydrated (downloaded). Force a full
						// re-scan so it gets a real thumbnail, content hash and full metadata (tags,
						// camera, etc.) that the offline shell path could not provide.
						info.metadata_scanned = df::date_t{};
						info.crc32c = 0;
						info.phash = nullptr;
					}

					updated_files.emplace_back(info);
					++file_first;
					++old_first;
				}
			}
		}

		while (file_first != file_last)
		{
			// only in new
			df::index_file_item info;
			populate_file_info(info, *file_first, _cache_items_loaded);
			updated_files.emplace_back(info);
			changes_detected = true;
			++file_first;
		}

		for (const auto& f : updated_files)
		{
			for (const auto& sc : f.ft->sidecars)
			{
				sidecar_extensions.emplace(sc);
			}
		}

		for (const auto& f : updated_files)
		{
			if (!(f.flags && df::index_item_flags::is_offline) && f.name[0] != '.')
			{
				const auto name = f.name;
				const auto extension_pos = df::find_ext(name);
				auto ext = name.substr(extension_pos);
				if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);

				if (sidecar_extensions.contains(ext))
				{
					const auto path = folder_path.combine_file(name);
					const auto without_extension = path.file_name_without_extension();

					sidecars.emplace(without_extension, name);

					if (str::icmp(ext, "xmp") == 0)
					{
						auto ps = f.metadata.load();

						if (f.metadata_scanned < f.file_modified)
						{
							auto updated = ps
								               ? std::make_shared<prop::item_metadata>(*ps)
								               : std::make_shared<prop::item_metadata>();
							metadata_xmp::parse(*updated, path);
							f.metadata.store(updated);
							ps = std::move(updated);
							f.metadata_scanned = timestamp;
							changes_detected = true;
						}

						const auto raw_file = ps ? ps->raw_file_name : str::cached{};

						if (!is_empty(raw_file))
						{
							sidecars.emplace(raw_file, name);
						}
					}
				}
			}
		}

		if (!sidecars.empty())
		{
			for (const auto& file : updated_files)
			{
				const auto name = file.name;
				const auto path = folder_path.combine_file(name);

				if (!file.ft->sidecars.empty())
				{
					const auto without_extension = path.file_name_without_extension();

					std::set<std::string_view> updated_sidecars;
					const auto found_with_extension = sidecars.equal_range(name);

					for (auto it = found_with_extension.first; it != found_with_extension.second; ++it)
					{
						updated_sidecars.emplace(it->second);
					}

					const auto found_without_extension = sidecars.equal_range(without_extension);

					for (auto it = found_without_extension.first; it != found_without_extension.second; ++it)
					{
						updated_sidecars.emplace(it->second);
					}

					const auto ps = file.metadata.load();
					const auto combined = str::combine(updated_sidecars);
					str::cached xmp;

					for (const auto& sc_name : updated_sidecars)
					{
						const auto ext = sc_name.substr(df::find_ext(sc_name));

						if (str::icmp(ext, ".xmp") == 0)
						{
							xmp = str::cache(sc_name);
						}

						const auto found = find_file(updated_files, sc_name);

						if (found != updated_files.end())
						{
							found->flags |= df::index_item_flags::is_sidecar;
						}
					}

					if (!ps || icmp(ps->sidecars, combined) != 0 || icmp(ps->xmp, xmp) != 0)
					{
						auto updated = ps
							               ? std::make_shared<prop::item_metadata>(*ps)
							               : std::make_shared<prop::item_metadata>();
						updated->sidecars = str::cache(combined);
						updated->xmp = xmp;
						file.metadata.store(std::move(updated));
						changes_detected = true;
					}
				}
			}
		}

		for (const auto& f : updated_files)
		{
			f.calc_search_presence();
		}

		if (changes_detected)
		{
			df::assert_true(std::ranges::is_sorted(updated_files, less_id));
			df::assert_true(std::ranges::is_sorted(updated_folders, less_ptr_name));

			auto folder_node = std::make_shared<df::index_folder_item>(std::move(updated_files),
			                                                           std::move(updated_folders));

			if (existing_folder)
			{
				folder_node->name = existing_folder->name;
				folder_node->is_read_only = existing_folder->is_read_only;
				folder_node->is_excluded = existing_folder->is_excluded.load();
				folder_node->is_in_collection = existing_folder->is_in_collection.load();
				folder_node->volume = existing_folder->volume;
				folder_node->created = existing_folder->created;
				folder_node->modified = existing_folder->modified;
			}
			else
			{
				folder_node->name = folder_path.name();
			}

			df::assert_true(!is_empty(folder_node->name));
			folder_node->reset_search_presence();

			_items.replace(folder_path, folder_node);
			_items.erase(removed_folders);

			return {folder_node, changes_detected};
		}
	}

	add_distinct_other_folders({folder_path});

	return {existing_folder, false};
}

std::vector<std::pair<df::file_path, df::index_file_item>> index_state::duplicate_list(const uint32_t group) const
{
	std::vector<std::pair<df::file_path, df::index_file_item>> result;
	const auto folders = _items.all_folders();

	for (const auto& ifn : folders)
	{
		if (ifn.second->is_in_collection)
		{
			for (const auto& file : ifn.second->files)
			{
				if (file.duplicates.load().group == group)
				{
					result.emplace_back(df::file_path(ifn.first, file.name), file);
				}
			}
		}
	}

	return result;
}

void index_state::update_predictions()
{
	const auto folders = _items.all_folders();
	const auto start_ms = df::now_ms();
	const auto folder_count = folders.size();
	// dup on:
	// filename
	// metadata created
	// file created and size
	// crc32c
	// the same picture at the same capture time, by perceptual hash


	struct duplicate_candidate
	{
		const df::index_folder_item_ptr& folder;
		const df::index_file_item* file;
		df::folder_path path;
	};

	std::vector<duplicate_candidate> files;
	std::vector<std::pair<uint32_t, size_t>> dups;

	{
		// four candidate keys per file - reserve from the real count rather than a fixed guess
		size_t indexed_file_count = 0;

		for (const auto& ifn : folders)
		{
			if (ifn.second->is_in_collection) indexed_file_count += ifn.second->files.size();
		}

		dups.reserve(indexed_file_count * 4);
		files.reserve(indexed_file_count);
	}

	int indexed_crc_count = 0;

	for (const auto& ifn : folders)
	{
		if (ifn.second->is_in_collection)
		{
			for (const auto& file : ifn.second->files)
			{
				if (df::is_closing) return;
				const auto file_index = files.size();
				files.push_back({ifn.second, &file, ifn.first});
				const auto md = file.metadata.load(); // important to hold ref

				if (md)
				{
					const auto cd = md->created();

					if (cd.is_valid())
					{
						dups.emplace_back(x64to32(cd.to_int64()), file_index);
					}
				}

				dups.emplace_back(file.name.ihash(), file_index);
				dups.emplace_back(x64to32(file.file_created.to_int64()), file_index);

				if (file.crc32c)
				{
					dups.emplace_back(file.crc32c, file_index);
					++indexed_crc_count;
				}
				else
				{
					dups.emplace_back(x64to32(file.size.to_int64()), file_index);
				}
			}
		}
	}

	std::ranges::sort(dups, [](auto&& left, auto&& right) { return left.first < right.first; });

	std::vector<size_t> parents(files.size());
	std::vector<uint8_t> ranks(files.size());
	std::iota(parents.begin(), parents.end(), 0);

	const auto find_root = [&parents](size_t item)
	{
		while (parents[item] != item)
		{
			parents[item] = parents[parents[item]];
			item = parents[item];
		}
		return item;
	};

	const auto unite = [&parents, &ranks, &find_root](const size_t left, const size_t right)
	{
		auto left_root = find_root(left);
		auto right_root = find_root(right);
		if (left_root == right_root) return;

		if (ranks[left_root] < ranks[right_root]) std::swap(left_root, right_root);
		parents[right_root] = left_root;
		if (ranks[left_root] == ranks[right_root]) ++ranks[left_root];
	};

	const auto& cdups = dups;

	int max_compare_count = 0;
	const auto dup_size = cdups.size();

	// How each file joined its set. Only the strongest evidence for a file is kept, so a file that is
	// byte-identical to one member and merely a possible copy of another reports the stronger claim.
	std::vector<df::copy_grade> grades(files.size(), df::copy_grade::none);

	const auto record_grade = [&grades](const size_t item, const df::copy_grade grade)
	{
		grades[item] = df::strongest(grades[item], grade);
	};

	for (auto i = 0u; i < dup_size; ++i)
	{
		if (df::is_closing) return;

		int compare_count = 0;
		const auto it = cdups[i];

		for (auto hi = i; hi < dup_size; ++hi)
		{
			const auto hit = cdups[hi];

			if (hit.first != it.first)
			{
				max_compare_count = std::max(max_compare_count, compare_count);
				break;
			}

			// a hash bucket can be arbitrarily large, so shutdown must be observed here too
			if (df::is_closing) return;

			compare_count += 1;

			const auto* const file = files[it.second].file;
			const auto* const other_file = files[hit.second].file;

			if (file == other_file) continue;

			const auto grade = dup_match_grade(file, other_file);

			if (grade != df::copy_grade::none)
			{
				unite(it.second, hit.second);
				record_grade(it.second, grade);
				record_grade(hit.second, grade);
			}
		}
	}

	// The perceptual stage. It is deliberately not part of the walk above: a picture match is a
	// tolerance, not an equality, so it may not be closed over transitively. Every candidate is
	// compared against one anchor chosen for the capture time and never against another candidate,
	// which makes each set a star rather than a chain (docs/collections.md section 7.2).
	std::vector<df::file_path> phash_wanted;

	{
		df::hash_map<uint64_t, std::vector<size_t>> capture_times;
		uint32_t dated_pictures = 0;

		for (size_t i = 0; i < files.size(); ++i)
		{
			const auto* const file = files[i].file;
			if (!file->ft->has_trait(file_traits::bitmap)) continue;

			const auto md = file->metadata.load(); // important to hold ref
			if (!md) continue;

			const auto created = md->created();
			if (!created.is_valid()) continue;

			capture_times[created.to_int64()].push_back(i);
			++dated_pictures;
		}

		// A hash is only earned by a picture that shares a capture time with another, so marking the
		// candidates is what makes an excess measurable rather than merely suspected.
		std::vector<uint8_t> is_candidate(files.size(), 0);
		uint32_t candidate_count = 0;
		uint32_t wanted_count = 0;
		uint32_t matched_count = 0;
		uint32_t crowded_count = 0;

		// The shape narrowing the gate applies, and what a gate blind to rotation would have refused.
		uint32_t dims_unknown = 0;
		uint32_t aspect_solo = 0;
		uint32_t aspect_solo_swap = 0;
		uint32_t matched_cross_aspect = 0;

		const auto dims_of = [&files](const size_t i)
		{
			const auto md = files[i].file->metadata.load(); // important to hold ref
			return md ? md->dimensions() : sizei{};
		};

		for (const auto& [created, members] : capture_times)
		{
			if (df::is_closing) return;

			// One photograph proves nothing, and past this a capture time is unambiguously continuous
			// shooting. Presence applies the same bound, so neither surface can see a set the other
			// cannot (docs/collections.md section 7).
			if (members.size() < 2 || members.size() > max_photos_sharing_capture_time) continue;

			// Sharing a capture second is a weak claim on its own. A picture whose neighbours are all
			// a different shape cannot be a copy of any of them, and refusing it here is a decode
			// saved rather than a judgement made. A quarter turn transposes the stored extent, so a
			// transposed neighbour still counts (docs/collections.md section 7.2).
			std::vector<size_t> shaped;
			shaped.reserve(members.size());

			for (const auto member : members)
			{
				const auto member_dims = dims_of(member);

				if (member_dims.is_empty())
				{
					++dims_unknown;
					// Shape is unknown rather than different, so the picture keeps its place.
					shaped.push_back(member);
					continue;
				}

				auto has_peer = false;
				auto has_peer_with_swap = false;

				for (const auto other : members)
				{
					if (other == member) continue;

					const auto other_dims = dims_of(other);

					if (same_picture_shape(member_dims, other_dims, false)) has_peer = true;
					if (same_picture_shape(member_dims, other_dims, true)) has_peer_with_swap = true;
					if (has_peer && has_peer_with_swap) break;
				}

				if (!has_peer) ++aspect_solo;
				if (!has_peer_with_swap) ++aspect_solo_swap;
				if (has_peer_with_swap) shaped.push_back(member);
			}

			if (shaped.size() < 2) continue;

			candidate_count += static_cast<uint32_t>(shaped.size());

			for (const auto member : shaped) is_candidate[member] = 1;

			// Every picture here has to be compared before any of them is reported: the crowd rule
			// counts matches, so judging a half-hashed capture time could let a burst through it.
			auto evidence_complete = true;

			for (const auto member : shaped)
			{
				if (files[member].file->phash.load() != nullptr) continue;

				evidence_complete = false;
				++wanted_count;

				// Hashing needs the file and this walk holds the index lock, so the work is only
				// noted here. The pass that follows the hashes will see them and compare.
				if (phash_wanted.size() < max_phash_requests_per_pass)
				{
					phash_wanted.emplace_back(files[member].path, files[member].file->name);
				}
			}

			if (!evidence_complete) continue;

			// Lowest path, so which item anchors the set never depends on the order the index
			// happened to be walked in. A picture that declined to be identified cannot anchor, and
			// skipping it here stops one blank frame from suppressing the whole capture time.
			// Sentinel is files.size(): shaped holds indices into files, so files.size() is not a
			// value a real member can take.
			auto anchor = files.size();

			for (const auto member : shaped)
			{
				const auto held = files[member].file->phash.load();
				if (!held || !held->is_usable()) continue;

				if (anchor == files.size() ||
					df::file_path(files[member].path, files[member].file->name) <
					df::file_path(files[anchor].path, files[anchor].file->name))
				{
					anchor = member;
				}
			}

			if (anchor == files.size()) continue;

			const auto anchor_hashes = files[anchor].file->phash.load();
			const auto anchor_hash = anchor_hashes->stored();

			// Collected rather than applied, because how many match decides whether any of them are
			// reported: a crowd around one anchor is a burst.
			std::vector<size_t> matched;

			// Continuous shooting produces frames in one orientation; it never produces a turned one.
			// So only an untuned match is evidence of a burst, and the crowd rule counts those alone.
			size_t same_orientation_matches = 0;

			for (const auto member : shaped)
			{
				if (member == anchor) continue;

				const auto member_hashes = files[member].file->phash.load();

				if (!member_hashes || !member_hashes->is_usable()) continue;

				// The member's four turns are the complete orbit, so this covers every relative
				// rotation without the anchor needing its own.
				if (crypto::phash_distance(anchor_hash, member_hashes->rotations) > max_duplicate_phash_distance)
				{
					continue;
				}

				matched.push_back(member);

				if (crypto::phash_distance(anchor_hash, member_hashes->stored()) <= max_duplicate_phash_distance)
				{
					++same_orientation_matches;
				}
			}

			if (same_orientation_matches > max_similar_pictures_at_one_capture_time)
			{
				++crowded_count;
				continue;
			}

			const auto anchor_dims = dims_of(anchor);

			for (const auto member : matched)
			{
				unite(anchor, member);
				record_grade(anchor, df::copy_grade::same_picture);
				record_grade(member, df::copy_grade::same_picture);

				if (!same_picture_shape(anchor_dims, dims_of(member), true)) ++matched_cross_aspect;
			}

			matched_count += static_cast<uint32_t>(matched.size());
		}

		// Held against invited. A picture keeps its hash once computed, so a large uninvited count is
		// not drift - it is hashing that was asked for by something other than the candidate rule.
		uint32_t usable_held = 0;
		uint32_t declined_held = 0;
		uint32_t uninvited = 0;

		for (size_t i = 0; i < files.size(); ++i)
		{
			const auto held = files[i].file->phash.load();
			if (!held) continue;

			if (held->is_usable()) ++usable_held;
			else ++declined_held;

			if (!is_candidate[i]) ++uninvited;
		}

		df::set_gauge(df::index_perf.pass_pictures, dated_pictures);
		df::set_gauge(df::index_perf.pass_buckets, static_cast<uint32_t>(capture_times.size()));
		df::set_gauge(df::index_perf.pass_candidates, candidate_count);
		df::set_gauge(df::index_perf.pass_wanted, wanted_count);
		df::set_gauge(df::index_perf.pass_matched, matched_count);
		df::set_gauge(df::index_perf.pass_crowded, crowded_count);
		df::set_gauge(df::index_perf.pass_usable_held, usable_held);
		df::set_gauge(df::index_perf.pass_declined_held, declined_held);
		df::set_gauge(df::index_perf.pass_uninvited, uninvited);
		df::set_gauge(df::index_perf.pass_dims_unknown, dims_unknown);
		df::set_gauge(df::index_perf.pass_aspect_solo, aspect_solo);
		df::set_gauge(df::index_perf.pass_aspect_solo_swap, aspect_solo_swap);
		df::set_gauge(df::index_perf.pass_matched_cross_aspect, matched_cross_aspect);

		stats.indexed_phash_count = static_cast<int>(usable_held);
		stats.indexed_phash_declined_count = static_cast<int>(declined_held);
		stats.indexed_phash_uninvited_count = static_cast<int>(uninvited);
	}

	df::hash_map<size_t, df::int_counter> component_counts;
	for (auto i = 0u; i < files.size(); ++i)
	{
		++component_counts[find_root(i)];
	}

	df::hash_map<size_t, uint32_t> component_groups;
	for (const auto& [root, count] : component_counts)
	{
		if (count > 1) component_groups[root] = ++next_dup_group;
	}

	for (auto i = 0u; i < files.size(); ++i)
	{
		if (df::is_closing) return;
		const auto root = find_root(i);
		const auto count = static_cast<uint32_t>(component_counts[root]);
		const auto found_group = component_groups.find(root);
		const auto group = found_group == component_groups.end() ? 0u : found_group->second;
		const auto grade = group == 0 ? df::copy_grade::none : grades[i];
		files[i].file->update_duplicates(files[i].folder, df::duplicate_info{group, count, grade});
	}

	stats.indexed_dup_folder_count = static_cast<int>(component_groups.size());
	stats.indexed_crc_count = indexed_crc_count;
	stats.indexed_max_compare_count = max_compare_count;
	stats.predictions_ms = static_cast<int>(df::now_ms() - start_ms);

	df::set_gauge(df::index_perf.pass_files, static_cast<uint32_t>(files.size()));
	df::set_gauge(df::index_perf.pass_crc_held, static_cast<uint32_t>(indexed_crc_count));
	df::set_gauge(df::index_perf.pass_dup_groups, static_cast<uint32_t>(component_groups.size()));

	df::trace(std::format("Index update predictions: {} folders in {} ms", folder_count, stats.predictions_ms));

	if (!phash_wanted.empty() && !df::is_closing)
	{
		queue_calc_perceptual_hashes(std::move(phash_wanted));
	}
}

// Decoding cannot happen on the predictions walk, so the pairs it could not judge are hashed here
// and the pass is asked for again. Each pass narrows the work, so a large collection converges over
// several rounds instead of stalling on the first.
void index_state::queue_calc_perceptual_hashes(std::vector<df::file_path> paths)
{
	_async.queue_async(async_queue::crc, [this, paths = std::move(paths)]
	{
		auto usable = 0;

		// Results are published in groups. Hashing a file takes milliseconds, so publishing each one on
		// its own found both the work queue and the write queue empty every time and woke two threads
		// per file for a few microseconds of work each.
		constexpr size_t publish_group = 32;
		std::vector<std::pair<df::file_path, crypto::phash_rotations>> hashed;
		hashed.reserve(publish_group);

		for (const auto& path : paths)
		{
			if (df::is_closing) break;

			crypto::phash_rotations hash{};
			auto readable = false;

			{
				df::scope_locked_inc loading(df::loading_media);
				df::perf_timer timer(df::index_perf.phash_us, &df::index_perf.phash_max_us);
				df::bump(df::index_perf.phash_computed);
				file_read_stream stream;

				if (stream.open(path) && stream.size() <= max_phash_file_bytes)
				{
					files ff;
					df::blob owner;
					readable = true;
					df::bump(df::index_perf.phash_bytes, stream.size());
					hash = ff.calc_perceptual_hash_rotations(stream.view_all(owner));
				}
				else
				{
					df::bump(df::index_perf.phash_unreadable);
				}
			}

			// Every attempt is recorded, including a refusal and a file that could not be read or
			// decoded. Without that the next pass asks for the same file again, forever.
			if (crypto::phash_is_usable(hash[0]))
			{
				hashed.emplace_back(path, hash);
				df::bump(df::index_perf.phash_usable);
				++usable;
			}
			else
			{
				hashed.emplace_back(path, crypto::phash_rotations{crypto::phash_declined, 0, 0, 0});
				if (readable) df::bump(df::index_perf.phash_declined);
			}

			if (hashed.size() >= publish_group)
			{
				save_phashes(std::move(hashed));
				hashed.clear();
				hashed.reserve(publish_group);
			}
		}

		// Published even when shutdown cut the loop short, so attempts already made are not repeated.
		save_phashes(std::move(hashed));

		// Only a hash that can actually match is worth another pass.
		if (usable > 0 && !df::is_closing)
		{
			queue_update_predictions();
		}
	});
}


static void add_words(df::dense_string_counts& distinct_words, strings_by_prop& distinct_text, const str::cached text,
                      const prop::key_ref key)
{
	count_ranges(distinct_words, text);
	distinct_text[key].emplace(text);
}


bool location_matrix_params::contains(const gps_coordinate coordinate) const
{
	return coordinate.is_valid() &&
		coordinate.latitude() >= min_latitude && coordinate.latitude() <= max_latitude &&
		coordinate.longitude() >= min_longitude && coordinate.longitude() <= max_longitude;
}

pointi location_matrix_params::cell(const gps_coordinate coordinate) const
{
	if (projection == location_matrix_projection::location_heat_map)
	{
		const auto location = df::location_heat_map::calc_map_loc(coordinate);
		const auto span = std::max(1, area_cell_span);
		return {location.x / span * span, location.y / span * span};
	}

	constexpr auto max_mercator_latitude = 85.05112878;
	const auto scale = std::pow(2.0, zoom) * 256.0 / std::max(1, cell_size);
	const auto latitude = std::clamp(coordinate.latitude(), -max_mercator_latitude, max_mercator_latitude);
	const auto latitude_radians = latitude * M_PI / 180.0;
	return {
		static_cast<int>(std::floor((coordinate.longitude() + 180.0) / 360.0 * scale)),
		static_cast<int>(std::floor((1.0 - std::asinh(std::tan(latitude_radians)) / M_PI) / 2.0 * scale))
	};
}

void location_matrix::add(df::file_path path, const gps_coordinate coordinate, const bool can_thumbnail,
                          const int rating)
{
	if (!params.contains(coordinate)) return;
	const auto index = params.cell(coordinate);
	const auto key = static_cast<uint64_t>(static_cast<uint32_t>(index.x)) << 32 | static_cast<uint32_t>(index.y);
	const uint8_t representative_rank = can_thumbnail ? (rating >= 4 ? 2 : 1) : 0;
	const auto found = _cell_lookup.find(key);
	if (found == _cell_lookup.end())
	{
		_cell_lookup.emplace(key, static_cast<uint32_t>(cells.size()));
		_representative_ranks.push_back(representative_rank);
		cells.push_back({
			index, std::move(path), {}, 1,
			coordinate.latitude(), coordinate.longitude(),
			coordinate.latitude(), coordinate.longitude(), coordinate.latitude(), coordinate.longitude()
		});
	}
	else
	{
		const auto cell_index = found->second;
		auto& cell = cells[cell_index];
		++cell.count;
		cell.latitude_sum += coordinate.latitude();
		cell.longitude_sum += coordinate.longitude();
		cell.min_latitude = std::min(cell.min_latitude, coordinate.latitude());
		cell.min_longitude = std::min(cell.min_longitude, coordinate.longitude());
		cell.max_latitude = std::max(cell.max_latitude, coordinate.latitude());
		cell.max_longitude = std::max(cell.max_longitude, coordinate.longitude());
		if (representative_rank > _representative_ranks[cell_index] ||
			(representative_rank == _representative_ranks[cell_index] && path.icmp(cell.representative_path) < 0))
		{
			_representative_ranks[cell_index] = representative_rank;
			cell.representative_path = std::move(path);
		}
	}
}

void location_matrix::finalize()
{
	for (auto& cell : cells)
	{
		cell.centroid = {cell.latitude_sum / cell.count, cell.longitude_sum / cell.count};
	}
	std::ranges::sort(cells, [](const cell& left, const cell& right)
	{
		return left.index.y == right.index.y ? left.index.x < right.index.x : left.index.y < right.index.y;
	});
	_cell_lookup.clear();
	_representative_ranks.clear();
}

location_matrix index_state::build_location_matrix(const location_matrix_params& params,
                                                   const df::unique_paths& excluded) const
{
	location_matrix result{params};

	for (const auto& [folder, folder_item] : _items.all_folders())
	{
		if (!folder_item->is_in_collection.load()) continue;

		for (const auto& file : folder_item->files)
		{
			const auto metadata = file.metadata.load();
			if (!metadata || !metadata->coordinate.is_valid()) continue;

			auto path = df::file_path(folder, file.name);
			const auto visual_media = file.ft &&
				(file.ft->has_trait(file_traits::bitmap) || file.ft->has_trait(file_traits::video_metadata));
			const auto can_thumbnail = visual_media && file.ft->has_trait(file_traits::thumbnail);
			if (!excluded.contains(path))
			{
				result.add(std::move(path), metadata->coordinate, can_thumbnail, metadata->rating);
			}
		}
	}

	result.finalize();
	return result;
}


void index_state::update_summary(const uint64_t generation)
{
	const auto start_ms = df::now_ms();

	{
		platform::shared_lock lock(_summary_rw);
		if (generation != 0 && generation != _summary_generation) return;
	}

	prop_text_summary distinct_labels;
	prop_num_summary distinct_ratings;
	prop_text_summary distinct_tags;
	tag_companion_counts tag_companions;

	df::dense_string_counts distinct_words;
	strings_by_prop distinct_text;
	df::unique_folders distinct_other_folders;

	index_histograms histograms;

	auto& distinct_tag_texts = distinct_text[prop::tag];
	const auto folders = _items.all_folders();
	auto files_until_currency_check = 256u;

	for (const auto& ifn : folders)
	{
		if (generation != 0)
		{
			platform::shared_lock lock(_summary_rw);
			if (generation != _summary_generation) return;
		}

		const auto is_indexed = ifn.second->is_in_collection.load();

		if (is_indexed)
		{
			for (const auto& file : ifn.second->files)
			{
				if (df::is_closing) return;
				if (generation != 0 && --files_until_currency_check == 0)
				{
					files_until_currency_check = 256;
					platform::shared_lock lock(_summary_rw);
					if (generation != _summary_generation) return;
				}

				const auto path = df::file_path(ifn.first, file.name);
				histograms.record(_locations, file, path);
				const auto md = file.metadata.load();

				if (md)
				{
					std::vector<std::string> item_tags;
					split2(md->tags, true,
					       [&distinct_words, &distinct_tags, &distinct_tag_texts, &item_tags, &file, &path](
					       const std::string_view part)
					       {
						       const auto cached_tag = str::cache(part);
						       item_tags.emplace_back(part);
						       distinct_tag_texts.emplace(cached_tag);
						       distinct_words[str::cache(std::format("#{}", part))] += 1;
						       distinct_tags[cached_tag].record(file, path);
					       });

					for (const auto& tag : item_tags)
					{
						for (const auto& companion : item_tags)
						{
							if (str::icmp(tag, companion) != 0)
							{
								++tag_companions[tag][str::cache(companion)];
							}
						}
					}

					if (!prop::is_null(md->album)) add_words(distinct_words, distinct_text, md->album, prop::album);
					if (!prop::is_null(md->album_artist))
						add_words(distinct_words, distinct_text, md->album_artist,
						          prop::album_artist);
					if (!prop::is_null(md->artist)) add_words(distinct_words, distinct_text, md->artist, prop::artist);
					if (!prop::is_null(md->audio_codec))
						add_words(distinct_words, distinct_text, md->audio_codec,
						          prop::audio_codec);
					if (!prop::is_null(md->bitrate))
						add_words(distinct_words, distinct_text, md->bitrate,
						          prop::bitrate);
					if (!prop::is_null(md->camera_manufacturer))
						add_words(
							distinct_words, distinct_text, md->camera_manufacturer, prop::camera_manufacturer);
					if (!prop::is_null(md->camera_model))
						add_words(distinct_words, distinct_text, md->camera_model,
						          prop::camera_model);
					if (!prop::is_null(md->comment))
						add_words(distinct_words, distinct_text, md->comment,
						          prop::comment);
					if (!prop::is_null(md->composer))
						add_words(distinct_words, distinct_text, md->composer,
						          prop::composer);
					if (!prop::is_null(md->copyright_creator))
						add_words(
							distinct_words, distinct_text, md->copyright_creator, prop::copyright_creator);
					if (!prop::is_null(md->copyright_credit))
						add_words(distinct_words, distinct_text,
						          md->copyright_credit, prop::copyright_credit);
					if (!prop::is_null(md->copyright_notice))
						add_words(distinct_words, distinct_text,
						          md->copyright_notice, prop::copyright_notice);
					if (!prop::is_null(md->copyright_source))
						add_words(distinct_words, distinct_text,
						          md->copyright_source, prop::copyright_source);
					if (!prop::is_null(md->copyright_url))
						add_words(distinct_words, distinct_text, md->copyright_url,
						          prop::copyright_url);
					if (!prop::is_null(md->description))
						add_words(distinct_words, distinct_text, md->description,
						          prop::description);
					if (!prop::is_null(md->encoder))
						add_words(distinct_words, distinct_text, md->encoder,
						          prop::encoder);
					if (!prop::is_null(md->file_name))
						add_words(distinct_words, distinct_text, md->file_name,
						          prop::file_name);
					if (!prop::is_null(md->genre))
					{
						// Genre is a single ';'-separated field; index each value
						// separately so the sidebar and autocomplete list them individually.
						count_ranges(distinct_words, md->genre);
						split2(md->genre, false, [&distinct_text](const std::string_view part)
						{
							const auto g = str::trim(part);
							if (!g.empty()) distinct_text[prop::genre].emplace(str::cache(g));
						}, str::is_genre_separator);
					}
					if (!prop::is_null(md->lens)) add_words(distinct_words, distinct_text, md->lens, prop::lens);
					if (!prop::is_null(md->location_place))
						add_words(distinct_words, distinct_text, md->location_place,
						          prop::location_place);
					if (!prop::is_null(md->location_country))
						add_words(distinct_words, distinct_text,
						          md->location_country, prop::location_country);
					if (!prop::is_null(md->location_state))
						add_words(distinct_words, distinct_text, md->location_state,
						          prop::location_state);
					if (!prop::is_null(md->performer))
						add_words(distinct_words, distinct_text, md->performer,
						          prop::performer);
					if (!prop::is_null(md->pixel_format))
						add_words(distinct_words, distinct_text, md->pixel_format,
						          prop::pixel_format);
					if (!prop::is_null(md->publisher))
						add_words(distinct_words, distinct_text, md->publisher,
						          prop::publisher);
					if (!prop::is_null(md->show)) add_words(distinct_words, distinct_text, md->show, prop::show);
					if (!prop::is_null(md->synopsis))
						add_words(distinct_words, distinct_text, md->synopsis,
						          prop::synopsis);
					if (!prop::is_null(md->title)) add_words(distinct_words, distinct_text, md->title, prop::title);
					if (!prop::is_null(md->video_codec))
						add_words(distinct_words, distinct_text, md->video_codec,
						          prop::video_codec);
					if (!prop::is_null(md->raw_file_name))
						add_words(distinct_words, distinct_text, md->raw_file_name,
						          prop::raw_file_name);

					if (!prop::is_null(md->label)) distinct_labels[md->label].record(file, path);

					auto r = md->rating;

					if (r != 0 && r < 6)
					{
						if (r == -1) r = 0;
						distinct_ratings[r].record(file, path);
					}
				}
			}
		}

		if (!is_indexed)
		{
			distinct_other_folders.emplace(ifn.first);
		}
	}

	auto summary = std::make_shared<index_metadata_summary>();

	// This walk visits every indexed folder, so it is authoritative. Seeding from the previous
	// snapshot and only inserting would keep words from items that have since been deleted or
	// renamed, and grow without bound for as long as the session lasts.
	summary->_distinct_text = std::move(distinct_text);
	summary->_distinct_words = std::move(distinct_words);

	summary->_distinct_labels = std::move(distinct_labels);
	summary->_distinct_ratings = distinct_ratings;
	summary->_distinct_tags = std::move(distinct_tags);
	summary->_tag_companions = std::move(tag_companions);
	if (generation != 0)
	{
		platform::shared_lock lock(_summary_rw);
		if (generation != _summary_generation) return;
	}
	if (!rebuild_sorted_words(*summary, generation)) return;

	index_metadata_summary_const_ptr published_summary = std::move(summary);
	index_histograms_const_ptr histogram_snapshot = std::make_shared<index_histograms>(std::move(histograms));

	{
		platform::exclusive_lock lock(_summary_rw);
		if (generation != 0 && generation != _summary_generation) return;
		distinct_other_folders.insert(_summary._distinct_other_folders->begin(),
		                              _summary._distinct_other_folders->end());
		std::shared_ptr<const df::unique_folders> other_folders_snapshot =
			std::make_shared<df::unique_folders>(std::move(distinct_other_folders));
		_summary._metadata.swap(published_summary);
		_summary._histograms.swap(histogram_snapshot);
		_summary._distinct_other_folders.swap(other_folders_snapshot);
	}

	_async.invalidate_view(view_invalid::sidebar | view_invalid::tooltip);

	df::trace(std::format("Index update summary in {} ms", df::now_ms() - start_ms));
}

static bool needs_scan_impl(const df::index_folder_item_ptr& f, const df::index_file_item& file,
                            const bool thumbnail_needed, const bool scan_if_offline)
{
	// Offline (cloud-only) files are also eligible for scanning: scan_item routes them
	// through the Windows Shell property store, which reads cached metadata WITHOUT
	// hydrating (downloading) the file. scan_if_offline is retained for API stability.
	(void)scan_if_offline;

	if (file.ft->is_media())
	{
		if (thumbnail_needed)
		{
			return true;
		}

		const auto xmp_file_name = file.xmp();

		if (!is_empty(xmp_file_name))
		{
			const auto& xmp_file = find_file(f->files, xmp_file_name);

			if (xmp_file != f->files.end())
			{
				// xmp metadata is scanned during validate folders
				// here return true if scan is newer then parent folder scan
				if (file.metadata_scanned < xmp_file->file_modified)
				{
					return true;
				}
			}
		}

		return file.metadata_scanned < file.file_modified;
	}

	return false;
}


void index_state::scan_uncached(const df::cancel_token& token)
{
	df::scope_locked_inc l(indexing);
	_fully_loaded = false;
	std::vector<df::file_path> uncached;

	size_t items_in_index = 0;

	{
		// create list of uncached
		const auto folders = _items.all_folders();

		for (const auto& folder : folders)
		{
			if (folder.second->is_in_collection)
			{
				for (const auto& file : folder.second->files)
				{
					if (file.ft->is_media())
					{
						items_in_index += 1;

						if (needs_scan_impl(folder.second, file, false, false))
						{
							uncached.emplace_back(folder.first, file.name);
						}
					}
				}
			}
		}
	}

	stats.index_item_count = static_cast<int>(items_in_index);
	stats.index_item_remaining = static_cast<int>(uncached.size());

	_async.invalidate_view(view_invalid::view_layout);

	// A first index walks the whole collection here, so the database hand-off is grouped: one row at
	// a time woke the database thread and opened a transaction per file.
	db_write_batch writes(*this);

	for (const auto& id : uncached)
	{
		if (token.is_cancelled()) break;

		const auto f = _items.find(id.folder());

		if (f)
		{
			scan_item(f, id, false, false, false, false, {}, false, files::file_type_from_name(id.name()), false,
			          true, true, &writes);
		}

		--stats.index_item_remaining;
	}

	writes.flush();
	stats.index_item_remaining = 0;

	_async.invalidate_view(view_invalid::view_layout | view_invalid::group_layout);
	_fully_loaded = !token.is_cancelled();
}

std::vector<folder_scan_item> index_state::scan_items(const df::index_roots& roots, const bool recursive,
                                                      const bool scan_if_offline, const df::cancel_token& token)
{
	const auto now = platform::now();

	std::vector<folder_scan_item> results;
	std::vector<df::folder_path> folders_to_scan = {roots.folders.begin(), roots.folders.end()};

	auto update_index_summary = false;
	db_write_batch writes(*this);

	while (!folders_to_scan.empty())
	{
		if (token.is_cancelled()) break;

		const auto folder_path = folders_to_scan.back();
		folders_to_scan.pop_back();

		if (!is_excluded(roots, folder_path))
		{
			const auto node = validate_folder(folder_path, true, now);

			// Null when enumeration failed for a folder that was never indexed - an offline volume,
			// a dropped share, or a directory that grants write but not list.
			if (!node.folder) continue;

			for (const auto& file : node.folder->files)
			{
				if (token.is_cancelled()) break;
				scan_item(node.folder, folder_path.combine_file(file.name), false, false, false, scan_if_offline, {},
				          false, file.ft, false, true, true, &writes);
				results.emplace_back(folder_path, file);
			}

			// Per folder, so a long walk neither holds the rows nor loses more than one folder's work.
			writes.flush();

			if (recursive)
			{
				for (const auto& sub_folder : *node.folder->folders_snapshot())
				{
					folders_to_scan.emplace_back(folder_path.combine(sub_folder->name));
				}
			}

			update_index_summary = update_index_summary || (node.folder->is_in_collection && node.was_updated);
		}
	}

	for (const auto& file_path : roots.files)
	{
		const auto node = validate_folder(file_path.folder(), true, now);

		if (token.is_cancelled()) break;
		if (!node.folder) continue;
		const auto found_file = find_file(node.folder->files, file_path.name());

		if (found_file != node.folder->files.end())
		{
			scan_item(node.folder, file_path, false, false, false, scan_if_offline, {}, false, found_file->ft, false,
			          true, true, &writes);
			results.emplace_back(file_path.folder(), *found_file);
		}
	}

	if (update_index_summary)
	{
		_async.invalidate_view(view_invalid::index_summary);
	}

	return results;
}

void index_state::scan_offline_item(const df::index_folder_item_ptr& folder,
                                    const df::file_path file_path,
                                    const bool thumbnail_needed,
                                    const std::weak_ptr<df::item_element>& item,
                                    const bool publish_to_item,
                                    const df::index_file_item& file,
                                    const df::date_t now,
                                    const bool invalidate_summary)
{
	// Cloud-only placeholder (OneDrive Files On-Demand, GVFS, etc.). Read cached metadata
	// (and, when the shell has one, a cached thumbnail) via the Windows Shell property
	// store WITHOUT hydrating (downloading) the file. Verified empirically that this does
	// not trigger a download for online-only files. We never compute a content hash
	// (crc32c) for these items, so they are excluded from hash-based duplicate matching.
	// Full hydration only happens when the user explicitly opens/accesses the file.
	const auto want_thumb = thumbnail_needed && publish_to_item;

	auto metadata = std::make_shared<prop::item_metadata>();
	ui::const_image_ptr shell_thumbnail;
	const auto resp = platform::get_cached_file_properties(file_path, *metadata, shell_thumbnail);

	item_db_write write;
	write.path = file_path;
	write.metadata_scanned = now;
	write.modified = file.file_modified;

	if (resp == platform::get_cached_file_properties_response::ok)
	{
		df::scope_locked_inc l(scanning_items);

		metadata->file_name = file_path.name();

		ui::const_image_ptr thumbnail_image;

		if (is_valid(shell_thumbnail))
		{
			files ff;

			thumbnail_image = shell_thumbnail;
			const auto max_extent = setting.thumbnail_max_dimension;
			const auto thumb_extent = thumbnail_image->dimensions();

			if (max_extent.cx < thumb_extent.cx || max_extent.cy < thumb_extent.cy)
			{
				const auto surf = ff.image_to_surface(thumbnail_image, max_extent, false, {},
				                                      decode_intent::thumbnail);
				thumbnail_image = ff.surface_to_thumbnail(surf);
			}

			if (is_valid(thumbnail_image) && thumbnail_image->data().size() < df::two_fifty_six_k)
			{
				write.thumb = thumbnail_image;
				write.thumb_scanned = file.file_modified;
			}
			else
			{
				thumbnail_image.reset();
			}
		}
		else if (want_thumb)
		{
			// Metadata came back but no cached thumbnail is available offline.
			publish_thumbnail_failure(item, file_path);
		}

		const auto existing_metadata = file.metadata.load();

		if (existing_metadata)
		{
			metadata->sidecars = existing_metadata->sidecars;
			metadata->xmp = existing_metadata->xmp;
			metadata->media_position = existing_metadata->media_position;
		}

		file.metadata_scanned = now;
		file.metadata.store(metadata);
		write.md = metadata;

		if (publish_to_item && is_valid(thumbnail_image))
		{
			publish_thumbnail(item, file_path, thumbnail_image, {}, file.file_modified, false, true);
		}
	}
	else
	{
		// Shell had no cached data. Still persist a properties row (with metadata_scanned)
		// so we do not re-scan this file on every startup. We write the (empty) metadata via
		// write.md so perform_writes performs an insert-or-replace that creates the row -- a
		// bare metadata_scanned update would affect zero rows when no row exists yet.
		metadata->file_name = file_path.name();
		file.metadata_scanned = now;
		file.metadata.store(metadata);
		write.md = metadata;

		if (want_thumb)
		{
			publish_thumbnail_failure(item, file_path);
		}
	}

	enqueue_db_write(std::move(write));

	if (invalidate_summary && folder->is_in_collection)
	{
		_async.invalidate_view(view_invalid::index_summary);
	}
}

void index_state::apply_scan_result(const df::index_folder_item_ptr& folder,
                                    const df::file_path file_path,
                                    const file_scan_result& sr,
                                    const df::date_t now,
                                    const df::date_t thumbnail_version,
                                    const bool load_thumb,
                                    const bool thumbnail_needed,
                                    const bool had_thumbnail,
                                    const std::weak_ptr<df::item_element>& item,
                                    const bool publish_to_item,
                                    const bool publish_item_update_immediately,
                                    const bool invalidate_summary,
                                    db_write_batch* writes)
{
	const auto queue_write = [this, writes](item_db_write w)
	{
		if (writes) writes->add(std::move(w));
		else enqueue_db_write(std::move(w));
	};

	const auto found_file = find_file(folder->files, file_path.name());

	if (found_file == folder->files.end())
	{
		return;
	}

	files ff;

	if (sr.success)
	{
		df::scope_locked_inc l(scanning_items);
		const auto* const mt = files::file_type_from_name(file_path);
		const auto metadata = sr.to_props();
		const auto thumbnail_was_loaded = is_valid(sr.thumbnail_surface) ||
			is_valid(sr.thumbnail_image);

		if (mt->has_trait(file_traits::video_metadata))
		{
			const auto name_props = scan_info_from_title(file_path.file_name_without_extension());

			if (is_empty(metadata->show) && !str::is_empty(name_props.show))
				metadata->show = str::cache(
					name_props.show);
			if (is_empty(metadata->title) && !str::is_empty(name_props.title))
				metadata->title = str::cache(
					name_props.title);
			if (metadata->year == 0 && name_props.year != 0) metadata->year = name_props.year;
			if (metadata->episode == df::xy8::make(0, 0) && name_props.episode != 0)
				metadata->episode =
					df::xy8::make(name_props.episode, name_props.episode_of);
			if (metadata->season == 0 && name_props.season != 0) metadata->season = name_props.season;
		}

		item_db_write write;
		write.path = file_path;
		write.md = metadata;
		write.metadata_scanned = now;

		ui::const_image_ptr cover_art;
		ui::const_image_ptr thumbnail_image;
		ui::const_surface_ptr thumbnail_surface;

		// Indexing is metadata only. A thumbnail produced here was provisional anyway - it is stored
		// with no scan timestamp, so the visible-item pass replaced it on first display - and paying
		// to decode, scale and re-encode every item in the collection to get one made indexing far
		// and away the most expensive thing the application does. Visuals are acquired on demand.
		if (load_thumb && is_valid(sr.cover_art))
		{
			cover_art = sr.cover_art;
			const auto max_extent = setting.thumbnail_max_dimension;
			const auto cover_art_extent = cover_art->dimensions();

			if (max_extent.cx < cover_art_extent.cx || max_extent.cy < cover_art_extent.cy)
			{
				auto surf = ff.image_to_surface(cover_art, max_extent, false, {}, decode_intent::thumbnail);
				cover_art = ff.surface_to_thumbnail(surf);
			}

			if (is_valid(cover_art))
			{
				df::assert_true(cover_art->data().size() < df::two_fifty_six_k);
				write.cover_art = cover_art;
			}
		}

		if (load_thumb && is_valid(sr.thumbnail_surface))
		{
			const auto max_extent = setting.thumbnail_max_dimension;
			const auto thumb_extent = sr.thumbnail_surface->dimensions();

			if (max_extent.cx < thumb_extent.cx || max_extent.cy < thumb_extent.cy)
			{
				av_scaler scaler;
				const auto dims = ui::scale_dimensions(thumb_extent, max_extent, true);
				auto surf = std::make_shared<ui::surface>();
				scaler.scale_surface(sr.thumbnail_surface, surf, dims);
				thumbnail_image = ff.surface_to_thumbnail(surf);
				thumbnail_surface = surf;
			}
			else
			{
				auto surf = sr.thumbnail_surface;
				thumbnail_image = ff.surface_to_thumbnail(surf);
				thumbnail_surface = surf;
			}

			if (is_valid(thumbnail_image))
			{
				df::assert_true(thumbnail_image->data().size() < df::two_fifty_six_k);
				write.thumb = thumbnail_image;
			}
		}
		else if (load_thumb && is_valid(sr.thumbnail_image))
		{
			thumbnail_image = sr.thumbnail_image;

			if (is_valid(thumbnail_image))
			{
				const auto max_extent = setting.thumbnail_max_dimension;
				const auto thumb_extent = thumbnail_image->dimensions();

				if (max_extent.cx < thumb_extent.cx || max_extent.cy < thumb_extent.cy)
				{
					auto surf = ff.image_to_surface(thumbnail_image, max_extent, false, {},
					                                decode_intent::thumbnail);
					thumbnail_image = ff.surface_to_thumbnail(surf);
					thumbnail_surface = surf;
				}

				if (is_valid(thumbnail_image))
				{
					df::assert_true(thumbnail_image->data().size() < df::two_fifty_six_k);
					write.thumb = thumbnail_image;
				}
			}
			else
			{
				// Thumbnail decode produced an invalid image (corrupt file, unsupported format, etc.)
				thumbnail_image.reset();

				if (publish_to_item)
				{
					publish_thumbnail_failure(item, file_path);
				}
			}
		}
		else if (load_thumb)
		{
			if (publish_to_item)
			{
				publish_thumbnail_failure(item, file_path);
			}
		}

		if (sr.crc32c)
		{
			write.crc32c = sr.crc32c;
		}

		if (load_thumb && thumbnail_was_loaded)
		{
			write.thumb_scanned = thumbnail_version;
		}

		const auto existing_metadata = found_file->metadata.load();

		if (existing_metadata && metadata)
		{
			metadata->sidecars = existing_metadata->sidecars;
			metadata->xmp = existing_metadata->xmp;
			// Playback position lives only in the index and database, so a rescan that
			// did not read it must carry it or the resume point is lost.
			metadata->media_position = existing_metadata->media_position;
		}

		metadata->file_name = file_path.name();
		found_file->metadata_scanned = now;
		found_file->metadata.store(metadata);

		// Same guard as the database write above: scan_file only computes a CRC when it read the whole
		// file, so an ordinary thumbnail scan of a video answers zero. Storing that would clear a value
		// the database still holds and silently drop the item out of duplicate detection.
		if (sr.crc32c)
		{
			found_file->crc32c = sr.crc32c;
		}

		// search_presence is a hard rejection filter, so it must be refreshed with every
		// published metadata snapshot or a newly matching item stays invisible to search
		found_file->calc_search_presence();
		folder->update_search_presence(*found_file);

		// For the immediate post-edit scan, metadata_scanned is stamped with the file's own
		// (handle-read) modified time. That makes needs_scan_impl (metadata_scanned <
		// file_modified) false both now - the cached file_modified is the older pre-edit mtime -
		// and after the background validate_folder refreshes file_modified to the same post-edit
		// mtime, so the queued background rescan is a no-op and never reopens the file BY NAME
		// (which could read stale SMB-cached bytes).

		write.modified = found_file->file_modified;

		if (publish_to_item)
		{
			if ((thumbnail_was_loaded || !had_thumbnail) && (is_valid(thumbnail_image) || is_valid(
				cover_art)))
			{
				df::assert_true(
					!is_valid(thumbnail_image) || thumbnail_image->data().size() < df::two_fifty_six_k);
				df::assert_true(!is_valid(cover_art) || cover_art->data().size() < df::two_fifty_six_k);

				// Stage as soon as the item that asked for this thumbnail has it: an unstaged
				// thumbnail cannot draw, so deferring to the end of the batch made a whole
				// screenful appear at once instead of filling in as each one decoded.
				publish_thumbnail(item, file_path, thumbnail_image, cover_art,
				                  thumbnail_was_loaded ? thumbnail_version : df::date_t::null,
				                  thumbnail_was_loaded,
				                  thumbnail_needed || publish_item_update_immediately);
			}
		}

		queue_write(std::move(write));

		if (invalidate_summary && folder->is_in_collection)
		{
			_async.invalidate_view(view_invalid::index_summary);
		}
	}
	else
	{
		item_db_write write;
		write.path = file_path;
		write.metadata_scanned = now;
		write.modified = found_file->file_modified;
		queue_write(std::move(write));

		if (load_thumb && publish_to_item)
		{
			publish_thumbnail_failure(item, file_path);
		}
	}
}

void index_state::scan_item(const df::index_folder_item_ptr& folder,
                            const df::file_path file_path,
                            const bool load_thumb,
                            const bool thumbnail_needed,
                            const bool had_thumbnail,
                            const bool scan_if_offline,
                            const std::weak_ptr<df::item_element>& item,
                            const bool publish_to_item,
                            const file_type_ref ft,
                            const bool force,
                            const bool publish_item_update_immediately,
                            const bool invalidate_summary,
                            db_write_batch* writes)
{
	const auto now = platform::now();
	const auto found_file = find_file(folder->files, file_path.name());

	if (found_file != folder->files.end())
	{
		const auto& file = *found_file;
		const auto thumbnail_version = file.file_modified.load();

		if (force || needs_scan_impl(folder, file, thumbnail_needed, scan_if_offline))
		{
			if (!crash_files().is_known_crash_file(file_path))
			{
				df::assert_true(ft->is_media());

				record_open_path record(crash_files(), file_path, str::utf8_cast(__FUNCTION__));

				if (file.flags && df::index_item_flags::is_offline)
				{
					// Cloud-only placeholder: never hydrate during indexing. Read cached
					// shell metadata/thumbnail instead. See scan_offline_item().
					scan_offline_item(folder, file_path, thumbnail_needed, item, publish_to_item, *found_file, now,
					                  invalidate_summary);
				}
				else
				{
					files ff;
					const auto sr = ff.scan_file(file_path, load_thumb, ft, file.xmp(),
					                             setting.thumbnail_max_dimension);

					apply_scan_result(folder, file_path, sr, now, thumbnail_version, load_thumb, thumbnail_needed,
					                  had_thumbnail, item, publish_to_item, publish_item_update_immediately,
					                  invalidate_summary, writes);
				}
			}
			else if (load_thumb && publish_to_item)
			{
				// Skipping silently would leave the tile blank, which reads as still loading rather
				// than as a file the app refuses to open. See docs/design.md system states.
				publish_thumbnail_failure(item, file_path);
			}
		}

		if (publish_to_item && publish_item_update_immediately)
		{
			publish_item_update(item, file_path);
		}
	}
}

void index_state::scan_item(const df::item_element_ptr& i, const bool load_thumb, const bool scan_if_offline)
{
	const auto node = validate_folder(i->folder(), true, platform::now());
	scan_item(node.folder, i->path(), load_thumb, load_thumb && i->should_load_thumbnail(), i->has_thumb(),
	          scan_if_offline, i, true, i->file_type());
}

rescan_spec index_state::make_rescan_spec(const item_scan_request& request, const std::string_view xmp_sidecar,
                                          const bool want_image, const bool want_handle)
{
	rescan_spec spec;
	spec.wanted = true;
	spec.load_thumbnail = request.load_thumbnail;
	spec.want_image = want_image;
	spec.want_handle = want_handle;
	spec.file_type = request.file_type;
	spec.xmp_sidecar = xmp_sidecar;
	spec.max_thumb_size = setting.thumbnail_max_dimension;
	return spec;
}

void index_state::apply_scan_now(const item_scan_request& request, const file_scan_result& sr, const bool coherent,
                                 const df::date_t known_modified)
{
	// Reuse the cached folder node - do NOT refresh from the filesystem here. A by-name refresh is
	// exactly the stale-read window we are avoiding; the scan already carries the authoritative
	// post-edit content.
	const auto node = validate_folder(request.folder, false, platform::now());
	if (!node.folder) return;
	const auto found_file = find_file(node.folder->files, request.path.name());

	if (found_file == node.folder->files.end())
	{
		return;
	}

	// A coherent scan came through the post-swap handle, so stamping both times from the file's own
	// modified time makes the later background rescan a no-op. The record's own modified time has to
	// advance with them: leaving it at the pre-edit value makes thumbnail_timestamp != file_modified,
	// which is what should_load_thumbnail() tests, so the "no-op" background rescan would re-read and
	// re-decode the file we just wrote.
	if (coherent) found_file->file_modified = known_modified;

	const auto now = coherent ? known_modified : platform::now();
	const auto thumbnail_version = coherent ? known_modified : found_file->file_modified.load();

	apply_scan_result(node.folder, request.path, sr, now, thumbnail_version, request.load_thumbnail,
	                  request.thumbnail_needed, request.had_thumbnail, request.lifetime, true, true, true);

	publish_item_update(request.lifetime, request.path);
}

bool index_state::apply_write_scan(const item_scan_request& request, const file_update_result& result)
{
	if (!result.success() || !result.scanned)
	{
		return true;
	}

	apply_scan_now(request, result.scan, result.coherent, df::date_t(result.modified));
	return false;
}

bool index_state::needs_scan(const df::item_element_ptr& item) const
{
	const auto id = item->path();
	const auto found_folder = _items.find(id.folder());

	if (found_folder)
	{
		const auto found_file = find_file(found_folder->files, id.name());

		if (found_file != found_folder->files.end())
		{
			return needs_scan_impl(found_folder, *found_file, false, false);
		}
	}

	return true;
}

df::index_file_item index_state::find_item(const df::file_path id) const
{
	const auto found_folder = _items.find(id.folder());

	if (found_folder)
	{
		const auto found_file = find_file(found_folder->files, id.name());

		if (found_file != found_folder->files.end())
		{
			return *found_file;
		}
	}

	return {};
}

void index_histograms::record(const location_cache&, const df::index_file_item& file, const df::file_path& path)
{
	const auto year = _year;
	constexpr auto map_width = static_cast<int>(df::location_heat_map::map_width);
	constexpr auto map_height = static_cast<int>(df::location_heat_map::map_height);

	const auto md = file.metadata.load();
	auto created = file.file_created;

	if (md)
	{
		if (md->created_exif.is_valid())
		{
			created = md->created_exif;
		}
		else if (md->created_utc.is_valid())
		{
			created = md->created_utc.system_to_local();
		}
		else if (md->created_digitized.is_valid())
		{
			created = md->created_digitized;
		}

		const auto coord = md->coordinate;

		if (coord.is_valid())
		{
			const auto map_loc = df::location_heat_map::calc_map_loc(coord);
			const auto map_index = map_loc.y * map_width + map_loc.x;
			_locations.coordinates[map_index] += 1;
			_location_latitude_sums[map_index] += coord.latitude();
			_location_longitude_sums[map_index] += coord.longitude();
			_location_min_latitudes[map_index] = std::min(_location_min_latitudes[map_index], coord.latitude());
			_location_min_longitudes[map_index] = std::min(_location_min_longitudes[map_index], coord.longitude());
			_location_max_latitudes[map_index] = std::max(_location_max_latitudes[map_index], coord.latitude());
			_location_max_longitudes[map_index] = std::max(_location_max_longitudes[map_index], coord.longitude());

			const auto place_name = md->location_place;
			const auto state_name = md->location_state;
			const auto country_name = md->location_country;
			if (!str::is_empty(place_name) || !str::is_empty(state_name) || !str::is_empty(country_name))
			{
				const auto group_id = crypto::hash_gen(country_name.sv()).append(state_name.sv()).append(
					place_name.sv()).result();
				const auto found = _location_groups.find(group_id);

				if (found != _location_groups.end())
				{
					found->second.count += 1;
					found->second.latitude_sum += coord.latitude();
					found->second.longitude_sum += coord.longitude();
					found->second.loc = df::location_heat_map::calc_map_loc(found->second.centroid());
				}
				else
				{
					const auto name = !str::is_empty(place_name) ? place_name : country_name;
					_location_groups[group_id] = {
						name, state_name, country_name, 1, map_loc, coord.latitude(), coord.longitude()
					};
				}
			}
		}
	}

	auto& file_type = _file_types.counts[file.ft->group->id];
	file_type.count += 1;
	file_type.size += file.size;

	const auto created_date_parts = created.date();
	const auto created_year_offset = year - created_date_parts.year;

	if (created_year_offset >= 0 && created_year_offset < df::max_history_years)
	{
		const auto date_index = created_year_offset * 12 + created_date_parts.month - 1;
		_dates.dates[date_index].created += 1;
		_dates.record_representative(date_index, file, path);
	}

	const auto modified_date_parts = file.file_modified.load().date();
	const auto modified_date_parts_year_offset = year - modified_date_parts.year;

	if (modified_date_parts_year_offset >= 0 && modified_date_parts_year_offset < df::max_history_years)
	{
		const auto date_index = modified_date_parts_year_offset * 12 + modified_date_parts.month - 1;
		_dates.dates[date_index].modified += 1;
		_dates.record_representative(date_index, file, path);
	}
}

std::vector<map_location_area> index_histograms::map_locations(const int requested_cell_span) const
{
	constexpr auto map_width = static_cast<int>(df::location_heat_map::map_width);
	constexpr auto map_height = static_cast<int>(df::location_heat_map::map_height);
	const auto cell_span = std::clamp(std::bit_ceil(static_cast<unsigned>(std::max(requested_cell_span, 1))), 1u, 64u);

	struct area_build
	{
		uint32_t count = 0;
		double latitude_sum = 0.0;
		double longitude_sum = 0.0;
		double min_latitude = std::numeric_limits<double>::max();
		double min_longitude = std::numeric_limits<double>::max();
		double max_latitude = std::numeric_limits<double>::lowest();
		double max_longitude = std::numeric_limits<double>::lowest();
	};

	df::hash_map<uint32_t, area_build> builds;
	for (auto cell_index = 0u; cell_index < _locations.coordinates.size(); ++cell_index)
	{
		const auto count = _locations.coordinates[cell_index];
		if (count == 0) continue;
		const auto x = static_cast<int>(cell_index % map_width);
		const auto y = static_cast<int>(cell_index / map_width);
		const auto area_x = x / static_cast<int>(cell_span) * static_cast<int>(cell_span);
		const auto area_y = y / static_cast<int>(cell_span) * static_cast<int>(cell_span);
		const auto area_key = static_cast<uint32_t>(area_y * map_width + area_x);
		auto& area = builds[area_key];
		area.count += count;
		area.latitude_sum += _location_latitude_sums[cell_index];
		area.longitude_sum += _location_longitude_sums[cell_index];
		area.min_latitude = std::min(area.min_latitude, _location_min_latitudes[cell_index]);
		area.min_longitude = std::min(area.min_longitude, _location_min_longitudes[cell_index]);
		area.max_latitude = std::max(area.max_latitude, _location_max_latitudes[cell_index]);
		area.max_longitude = std::max(area.max_longitude, _location_max_longitudes[cell_index]);
	}

	std::vector<map_location_area> result;
	result.reserve(builds.size());
	for (const auto& [area_key, build] : builds)
	{
		const auto area_x = static_cast<int>(area_key % map_width);
		const auto area_y = static_cast<int>(area_key / map_width);
		result.push_back({
			.count = build.count,
			.cell = {area_x, area_y},
			.cell_span = static_cast<int>(cell_span),
			.position = {build.latitude_sum / build.count, build.longitude_sum / build.count},
			.min_latitude = build.min_latitude,
			.min_longitude = build.min_longitude,
			.max_latitude = build.max_latitude,
			.max_longitude = build.max_longitude
		});
	}

	std::ranges::sort(result, [](const auto& left, const auto& right)
	{
		return left.cell.y == right.cell.y ? left.cell.x < right.cell.x : left.cell.y < right.cell.y;
	});

	return result;
}

std::optional<map_location_area> index_histograms::find_map_location(const std::string_view name,
                                                                     const location_cache& locations,
                                                                     const gps_coordinate default_location) const
{
	std::vector<location_t> named_locations;
	for (const auto& match : locations.auto_complete(name, 32, default_location))
	{
		if (str::icmp(match.location.place, name) == 0)
		{
			named_locations.emplace_back(match.location);
		}
	}

	for (const auto cell_span : {1, 2, 4, 8, 16, 32, 64})
	{
		const auto areas = map_locations(cell_span);
		for (const auto& named_location : named_locations)
		{
			const auto map_cell = df::location_heat_map::calc_map_loc(named_location.position);
			const auto found = std::ranges::find_if(areas, [map_cell](const map_location_area& area)
			{
				return area.contains(map_cell);
			});
			if (found == areas.end()) continue;

			auto selected = locations.find_largest(found->min_latitude, found->min_longitude,
			                                       found->max_latitude, found->max_longitude);
			if (selected.id == 0)
			{
				selected = locations.find_closest(found->position.latitude(), found->position.longitude());
			}
			if (str::icmp(selected.place, name) == 0)
			{
				auto result = *found;
				result.name = selected.place;
				result.state = selected.state;
				result.country = selected.country;
				result.population = selected.population;
				return result;
			}
		}
	}

	return {};
}

inline bool index_state::is_collection_search(const df::search_t& search) const
{
	for (const auto& sel : search.selectors())
	{
		if (!is_in_collection(sel.folder()))
		{
			return false;
		}
	}

	return true;
}

void index_state::calc_folder_summary(const df::folder_path& path, const df::index_folder_info_const_ptr& folder,
                                      df::file_group_histogram& result, const df::cancel_token& token)
{
	const auto child_folders = folder->folders_snapshot();
	for (const auto& sub_folder : *child_folders)
	{
		calc_folder_summary(path.combine(sub_folder->name), sub_folder, result, token);
	}

	for (const auto& file : folder->files)
	{
		result.record(file, df::file_path(path, file.name));
	}
}

df::file_group_histogram index_state::calc_folder_summary(const df::folder_path path,
                                                          const df::cancel_token& token) const
{
	df::file_group_histogram result;
	const auto folder = _items.find(path);

	if (folder)
	{
		const auto child_folders = folder->folders_snapshot();
		for (const auto& sub_folder : *child_folders)
		{
			calc_folder_summary(path.combine(sub_folder->name), sub_folder, result, token);
		}

		for (const auto& file : folder->files)
		{
			result.record(file, df::file_path(path, file.name));
		}
	}

	return result;
}

void index_state::save_media_position(const df::file_path id, const double media_position)
{
	item_db_write write;
	write.path = id;
	write.media_position = media_position;
	enqueue_db_write(std::move(write));
}

void index_state::save_crc(const df::file_path id, const uint32_t crc)
{
	_async.queue_async(async_queue::work, [this, id, crc]
	{
		const auto f = _items.find(id.folder());

		if (f)
		{
			const auto found_file = find_file(f->files, id.name());

			if (found_file != f->files.end())
			{
				found_file->crc32c = crc;
				found_file->calc_search_presence();
				f->update_search_presence(*found_file);
			}
		}
	});

	item_db_write write;
	write.path = id;
	write.crc32c = crc;
	enqueue_db_write(std::move(write));
}

void index_state::save_phash(const df::file_path id, const crypto::phash_rotations& phash)
{
	_async.queue_async(async_queue::work, [this, id, published = df::make_picture_hashes(phash)]
	{
		const auto f = _items.find(id.folder());
		auto found = false;

		if (f)
		{
			const auto found_file = find_file(f->files, id.name());

			if (found_file != f->files.end())
			{
				found_file->phash = published;
				found = true;
			}
		}

		// The database write is an update keyed on an existing row, so a path the collection does not
		// hold keeps no hash and will be decoded again next time it is asked about.
		if (!found) df::bump(df::index_perf.phash_unpersisted);
	});

	item_db_write write;
	write.path = id;
	write.phash = phash;
	enqueue_db_write(std::move(write));
}

void index_state::save_phashes(std::vector<std::pair<df::file_path, crypto::phash_rotations>> hashes)
{
	if (hashes.empty()) return;

	std::vector<item_db_write> writes;
	writes.reserve(hashes.size());

	for (const auto& [path, phash] : hashes)
	{
		item_db_write write;
		write.path = path;
		write.phash = phash;
		writes.emplace_back(std::move(write));
	}

	enqueue_db_writes(std::move(writes));

	// Published as complete sets, so the walk never sees a picture with some orientations filled in.
	std::vector<std::pair<df::file_path, df::picture_hashes_ptr>> published;
	published.reserve(hashes.size());

	for (const auto& [path, phash] : hashes)
	{
		published.emplace_back(path, df::make_picture_hashes(phash));
	}

	_async.queue_async(async_queue::work, [this, published = std::move(published)]
	{
		for (const auto& [path, hashes_ptr] : published)
		{
			const auto f = _items.find(path.folder());
			auto found = false;

			if (f)
			{
				const auto found_file = find_file(f->files, path.name());

				if (found_file != f->files.end())
				{
					found_file->phash = hashes_ptr;
					found = true;
				}
			}

			if (!found) df::bump(df::index_perf.phash_unpersisted);
		}
	});
}

void index_state::save_thumbnail(const df::file_path id, const ui::const_image_ptr& thumbnail_image,
                                 const ui::const_image_ptr& cover_art, const df::date_t scan_timestamp)
{
	item_db_write write;
	write.path = id;

	if (is_valid(thumbnail_image))
	{
		write.thumb = thumbnail_image;
	}

	if (is_valid(cover_art))
	{
		write.cover_art = cover_art;
	}

	write.thumb_scanned = scan_timestamp;
	enqueue_db_write(std::move(write));
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

void index_state::index_roots(df::index_roots roots)
{
	_fully_loaded = false;
	{
		platform::exclusive_lock lock(_summary_rw);
		std::swap(_summary._roots, roots);
	}
}

void index_state::index_folders(df::cancel_token token)
{
	df::scope_locked_inc l(detecting);
	df::index_roots roots;

	{
		platform::shared_lock lock(_summary_rw);
		roots = _summary._roots;
	}

	const auto now = platform::now();
	std::vector<df::folder_path> folders(roots.folders.begin(), roots.folders.end());
	df::unique_folders unique_folder_paths(folders.cbegin(), folders.cend());

	index_histograms histograms;
	int count = 0;
	stats.index_folder_count = 0;
	auto next_histogram_publish_ms = df::now_ms();

	for (const auto& f : _items.all_folders())
	{
		f.second->is_in_collection = false;
		f.second->is_excluded = false;
	}

	while (!folders.empty() && !token.is_cancelled())
	{
		if (token.is_cancelled())
		{
			break;
		}

		auto folder_path = folders.back();
		folders.pop_back();

		if (!is_excluded(roots, folder_path))
		{
			const auto node = validate_folder(folder_path, true, now);
			if (!node.folder) continue;
			node.folder->is_in_collection = true;

			for (const auto& file : node.folder->files)
			{
				histograms.record(_locations, file, df::file_path(folder_path, file.name));

				if (file.ft->is_media())
				{
					count += 1;
				}
			}

			for (const auto& sub_folder : *node.folder->folders_snapshot())
			{
				const auto sub_folder_path = folder_path.combine(sub_folder->name);
				const auto is_excluded = df::is_excluded(roots, sub_folder_path);

				if (!unique_folder_paths.contains(sub_folder_path) &&
					folders.size() < max_folders_to_index &&
					!is_excluded)
				{
					folders.emplace_back(sub_folder_path);
					unique_folder_paths.emplace(sub_folder_path);
					++stats.index_folder_count;
				}

				sub_folder->is_excluded = is_excluded;
			}
		}

		// Histogram snapshots are large; rate-limit progress copies while always publishing the final state below.
		const auto current_ms = df::now_ms();
		if (current_ms >= next_histogram_publish_ms)
		{
			index_histograms_const_ptr histogram_snapshot = std::make_shared<index_histograms>(histograms);
			{
				platform::exclusive_lock lock(_summary_rw);
				_summary._histograms.swap(histogram_snapshot);
			}
			_async.invalidate_view(view_invalid::sidebar_file_types_and_dates | view_invalid::tooltip);
			next_histogram_publish_ms = current_ms + 100;
		}
	}

	if (!token.is_cancelled())
	{
		stats.index_item_count = stats.media_item_count = count;

		_async.queue_database([cached_items = all_indexed_items()](const database& db)
		{
			db.clean(cached_items);
		});
	}

	{
		stats.index_folder_count = static_cast<int>(unique_folder_paths.size());
		std::shared_ptr<const df::unique_folders> folders_snapshot =
			std::make_shared<df::unique_folders>(std::move(unique_folder_paths));
		platform::exclusive_lock lock(_summary_rw);
		_summary._distinct_folders.swap(folders_snapshot);
	}

	df::unique_folders distinct_prime_folders;

	for (const auto& r : roots.folders)
	{
		distinct_prime_folders.emplace(r);
	}

	for (const auto& d : platform::drives())
	{
		distinct_prime_folders.emplace(df::folder_path(d.name));
	}

	std::shared_ptr<const df::unique_folders> prime_folders_snapshot =
		std::make_shared<df::unique_folders>(std::move(distinct_prime_folders));
	index_histograms_const_ptr histogram_snapshot = std::make_shared<index_histograms>(std::move(histograms));

	{
		platform::exclusive_lock lock(_summary_rw);
		_summary._distinct_prime_folders.swap(prime_folders_snapshot);
		_summary._histograms.swap(histogram_snapshot);
	}

	_folders_indexed = true;
}


static void scan_trim(std::string& s)
{
	const auto pred = [](const int ch)
	{
		return !std::iswspace(ch) && ch != '-';
	};

	s.erase(s.begin(), std::ranges::find_if(s, pred));
	s.erase(std::find_if(s.rbegin(), s.rend(), pred).base(), s.end());
}

std::string build_result_string(const std::vector<std::string>& tokens,
                                const std::vector<std::string>::const_iterator& begin,
                                const std::vector<std::string>::const_iterator& end)
{
	std::string result;
	bool brackets = false;
	auto i = begin;

	while (i < end)
	{
		if (*i == "[")
		{
			brackets = true;
		}
		else if (brackets)
		{
			brackets = *i != "]";
		}
		else
		{
			if (!result.empty()) result += " ";
			result += *i;
		}

		++i;
	}

	return std::string(str::utf8_cast(result));
}

media_name_props scan_info_from_title(const std::string_view name8)
{
	media_name_props result;

	static const df::hash_set<std::string_view, df::ihash, df::ieq> stop_words
	{
		"480p",
		"720p",
		"720",
		"1080p",
		"1080",
		"1440p",
		"1440",
		"2160p",
		"2160",
		"4320p",
		"4320",
		"4k",
		"8k",
		"uhd",
		"hdtv",
		"x264",
		"x265",
		"h264",
		"h265",
		"hevc",
		"av1",
		"ac3",
		"eac3",
		"ddp",
		"dts",
		"aac",
		"brrip",
		"bdrip",
		"bluray",
		"hdrip",
		"dvdrip",
		"web",
		"webdl",
		"webrip",
		"pdtv",
		"dvdscr",
		"xvid",
		"hdr",
		"hdr10",
		"hdr10plus",
		"dv",
		"dolbyvision",
		"remux",
		"proper",
		"repack",
		"internal",
		"unrated",
		"10bit",
		"extended",
		"5.1",
		"7.1"
	};

	static const df::hash_set<std::string_view, df::ihash, df::ieq> pre_title_stop_words
	{
		"(",
		"[",
		"-",
	};

	static const df::hash_set<std::string_view, df::ihash, df::ieq> pre_show_stop_words
	{
		"-",
	};

	static const auto split_rx = std::regex{R"([\s]+|_|\-|\.|\(|\[|\]|\))"s};

	const auto name = std::string_view(std::bit_cast<const char*>(name8.data()), name8.size());

	auto tokens = std::vector<std::string>(
		std::regex_token_iterator<std::string_view::const_iterator>{name.begin(), name.end(), split_rx, {-1, 0}},
		std::regex_token_iterator<std::string_view::const_iterator>{}
	);

	static const auto separator_rx = std::regex{R"([\s]+|_|\.)"s};

	std::erase_if(tokens, [](auto&& i) { return i.empty() || std::regex_match(i, separator_rx); });

	static const auto episode_rx = std::regex{"s([0-9]{1,2})e([0-9]{1,3})"s, std::regex_constants::icase};
	static const auto episode_of_rx = std::regex{"([0-9]{1,3})of([0-9]{1,3})"s, std::regex_constants::icase};
	static const auto episode_x_rx = std::regex{"([0-9]{1,2})x([0-9]{1,3})"s, std::regex_constants::icase};
	static const auto year_rx = std::regex{"(19|20)[0-9]{2}"s};
	static const auto dimensions_rx = std::regex{"([0-9]{3,5})x([0-9]{3,5})"s, std::regex_constants::icase};

	struct episode_info
	{
		int season = 0;
		int episode = 0;
		int episode_of = 0;
	};

	const auto parse_episode = [&](const std::string& token) -> std::optional<episode_info>
	{
		std::smatch match;

		if (std::regex_match(token, match, episode_rx))
		{
			const auto season = str::to_int(match[1].str());
			const auto episode = str::to_int(match[2].str());
			if (season <= 99 && episode > 0 && episode <= 255) return episode_info{season, episode, 0};
		}
		else if (std::regex_match(token, match, episode_of_rx))
		{
			const auto episode = str::to_int(match[1].str());
			const auto episode_of = str::to_int(match[2].str());
			if (episode > 0 && episode <= episode_of && episode_of <= 255)
				return episode_info{0, episode, episode_of};
		}
		else if (std::regex_match(token, match, episode_x_rx))
		{
			const auto season = str::to_int(match[1].str());
			const auto episode = str::to_int(match[2].str());
			if (season <= 99 && episode > 0 && episode <= 255) return episode_info{season, episode, 0};
		}

		return std::nullopt;
	};

	const auto found_episode_num = std::ranges::find_if(tokens, [&](const auto& token)
	{
		return parse_episode(token).has_value();
	});

	auto end_show = found_episode_num;

	while (end_show != tokens.begin() &&
		end_show != tokens.end() &&
		pre_show_stop_words.contains(*(end_show - 1)))
	{
		--end_show;
	}

	if (end_show != tokens.begin() &&
		end_show != tokens.end() &&
		*(end_show - 1) == ")")
	{
		--end_show;

		if (end_show != tokens.begin() &&
			end_show != tokens.end() &&
			std::regex_match(*(end_show - 1), year_rx))
		{
			--end_show;

			if (end_show != tokens.begin() &&
				end_show != tokens.end() &&
				*(end_show - 1) == "(")
			{
				result.year = str::to_int(*end_show);
				--end_show;
			}
		}
	}

	auto end_title = std::ranges::find_if(tokens, [&](const auto& token)
	{
		if (stop_words.contains(token)) return true;

		std::smatch match;
		if (!std::regex_match(token, match, dimensions_rx)) return false;

		const auto width = str::to_int(match[1].str());
		const auto height = str::to_int(match[2].str());
		return width >= 320 && height >= 200;
	});

	while (end_title != tokens.begin() &&
		end_title != tokens.end() &&
		pre_title_stop_words.contains(*(end_title - 1)))
	{
		--end_title;
	}

	if (found_episode_num != tokens.end() && found_episode_num != tokens.begin())
	{
		result.show = build_result_string(tokens, tokens.begin(), end_show);

		const auto episode = parse_episode(*found_episode_num);
		if (episode)
		{
			result.season = episode->season;
			result.episode = episode->episode;
			result.episode_of = episode->episode_of;
		}

		result.title = build_result_string(tokens, found_episode_num + 1, end_title);
	}
	else
	{
		auto movie_title_end = end_title;
		auto found_year = false;

		if (movie_title_end != tokens.begin() && std::regex_match(*(movie_title_end - 1), year_rx))
		{
			result.year = str::to_int(*(movie_title_end - 1));
			--movie_title_end;
			found_year = true;
		}
		else if (movie_title_end - tokens.begin() >= 3 && *(movie_title_end - 1) == ")" &&
			std::regex_match(*(movie_title_end - 2), year_rx) && *(movie_title_end - 3) == "(")
		{
			result.year = str::to_int(*(movie_title_end - 2));
			movie_title_end -= 3;
			found_year = true;
		}

		if ((end_title != tokens.end() || found_year) && movie_title_end != tokens.begin())
		{
			result.title = build_result_string(tokens, tokens.begin(), movie_title_end);
		}
	}

	scan_trim(result.title);
	scan_trim(result.show);

	return result;
}

struct presence_match
{
	item_presence state = item_presence::unknown;
	df::duplicate_info duplicates = {};
};

struct presence_request
{
	std::weak_ptr<df::item_element> lifetime;
	df::file_path path;
	df::file_size size;
	df::date_t file_modified;
	df::date_t media_created;
	uint32_t crc32c = 0;
	bool is_folder = false;
	bool is_bitmap = false;
	sizei dimensions;
};

struct presence_result
{
	presence_request source;
	presence_match match;
};

static int presence_rank(const item_presence presence)
{
	switch (presence)
	{
	case item_presence::newer_in: return 3;
	case item_presence::similar_in: return 2;
	case item_presence::older_in: return 1;
	default: return 0;
	}
}

static bool prefer_duplicate_info(const df::duplicate_info candidate, const df::duplicate_info current)
{
	if (current.group == 0) return candidate.group != 0;
	if (candidate.group == 0) return false;
	if (candidate.group != current.group) return candidate.group < current.group;
	return candidate.count > current.count;
}

static df::copy_grade dup_match_grade(const df::index_file_item& file, const presence_request& other)
{
	if (file.crc32c != 0 && file.size == other.size && file.crc32c == other.crc32c)
	{
		return df::copy_grade::identical;
	}

	const auto name_match = icmp(file.name, other.path.name()) == 0;

	if (name_match && file.ft->has_trait(file_traits::av) && file.size == other.size)
	{
		return df::copy_grade::same_file;
	}

	return name_match && file.created() == other.media_created
		       ? df::copy_grade::same_file
		       : df::copy_grade::none;
}

// An outside file the cheap grades could not place, held until the walk is over because deciding it
// means decoding a picture and the walk is reading the index.
struct presence_similar_candidate
{
	size_t request_index = 0;
	df::file_path path;
	crypto::phash_rotations phash{};
	df::date_t file_modified;
	df::duplicate_info duplicates;
	sizei dimensions;
};


static void items_possible_hashes_contains(std::vector<presence_match>& matches,
                                           const std::vector<std::pair<unsigned, size_t>>& possible,
                                           const std::vector<presence_request>& requests,
                                           const df::index_file_item& indexed_file, const uint32_t hash)
{
	auto lb = std::lower_bound(possible.begin(), possible.end(), hash, [](auto&& l, auto&& r) { return l.first < r; });

	while (lb != possible.end() && lb->first == hash)
	{
		const auto request_index = lb->second;
		const auto& request = requests[request_index];
		const auto grade = dup_match_grade(indexed_file, request);

		if (grade != df::copy_grade::none)
		{
			auto candidate = item_presence::unknown;

			if (indexed_file.file_modified == request.file_modified ||
				(indexed_file.crc32c != 0 && indexed_file.crc32c == request.crc32c))
			{
				candidate = item_presence::similar_in;
			}
			else if (indexed_file.file_modified < request.file_modified)
			{
				candidate = item_presence::older_in;
			}
			else if (indexed_file.file_modified > request.file_modified)
			{
				candidate = item_presence::newer_in;
			}

			auto duplicates = indexed_file.duplicates.load();
			duplicates.grade = grade;
			auto& current = matches[request_index];
			if (presence_rank(candidate) > presence_rank(current.state) ||
				(candidate == current.state && prefer_duplicate_info(duplicates, current.duplicates)))
			{
				current.state = candidate;
				current.duplicates = duplicates;
			}
		}

		++lb;
	}
}

// Decides the outside photographs the cheap grades could not place. Reading and decoding happens
// here, after the index walk, and only for a capture time the collection does not crowd: the same
// refusal duplicate search makes, so both surfaces answer alike (docs/collections.md section 7.3).
static void resolve_similar_presence(index_state& index, const std::vector<presence_request>& requests,
                                     std::vector<presence_match>& matches,
                                     std::vector<presence_similar_candidate>& candidates)
{
	if (candidates.empty()) return;

	std::ranges::sort(candidates, [](auto&& left, auto&& right)
	{
		return left.request_index < right.request_index;
	});

	files decoder;

	// A member is only hashed by the predictions pass when another member shares its capture time, so
	// the picture an outside file is being compared against often has no hash yet. It is computed
	// here and saved, because answering "checking" forever would be an absence in all but name.
	const auto hash_of = [&decoder, &index](const df::file_path path,
	                                        const crypto::phash_rotations& known) -> crypto::phash_rotations
	{
		if (known[0] != 0) return known;

		crypto::phash_rotations hash{};

		{
			df::scope_locked_inc loading(df::loading_media);
			df::perf_timer timer(df::index_perf.phash_us, &df::index_perf.phash_max_us);
			df::bump(df::index_perf.phash_computed);
			df::bump(df::index_perf.phash_presence);
			file_read_stream stream;

			if (stream.open(path) && stream.size() <= max_phash_file_bytes)
			{
				df::blob owner;
				df::bump(df::index_perf.phash_bytes, stream.size());
				hash = decoder.calc_perceptual_hash_rotations(stream.view_all(owner));
				df::bump(crypto::phash_is_usable(hash[0])
					         ? df::index_perf.phash_usable
					         : df::index_perf.phash_declined);
			}
			else
			{
				df::bump(df::index_perf.phash_unreadable);
			}
		}

		if (!crypto::phash_is_usable(hash[0])) hash = {crypto::phash_declined, 0, 0, 0};

		index.save_phash(path, hash);
		return hash;
	};

	for (auto i = candidates.begin(); i != candidates.end();)
	{
		if (df::is_closing) return;

		auto group_end = i;
		while (group_end != candidates.end() && group_end->request_index == i->request_index) ++group_end;

		const auto member_count = static_cast<size_t>(std::distance(i, group_end));
		const auto request_index = i->request_index;
		const auto& request = requests[request_index];

		// A cheaper grade already answered this file; a picture cannot make that claim stronger.
		const auto already_matched = matches[request_index].state != item_presence::unknown;

		if (!already_matched && member_count <= max_photos_sharing_capture_time)
		{
			const auto probe_hash = hash_of(request.path, {});

			if (crypto::phash_is_usable(probe_hash[0]))
			{
				// The outside file is the anchor here, so the same crowd rule applies: many members
				// matching one picture in one orientation at one capture time is a burst, not a set
				// of copies. A turned match is never burst evidence, exactly as duplicate search
				// counts it (docs/collections.md section 7.3).
				std::vector<const presence_similar_candidate*> matched;
				size_t same_orientation_matches = 0;

				for (auto candidate = i; candidate != group_end; ++candidate)
				{
					// Shape narrows before the picture is decoded, exactly as duplicate search does.
					if (!same_picture_shape(request.dimensions, candidate->dimensions)) continue;

					const auto candidate_hash = hash_of(candidate->path, candidate->phash);

					if (!crypto::phash_is_usable(candidate_hash[0])) continue;
					if (crypto::phash_distance(probe_hash[0], candidate_hash) > max_duplicate_phash_distance)
					{
						continue;
					}

					matched.push_back(&*candidate);

					if (crypto::phash_distance(probe_hash[0], candidate_hash[0]) <= max_duplicate_phash_distance)
					{
						++same_orientation_matches;
					}
				}

				if (same_orientation_matches <= max_similar_pictures_at_one_capture_time)
				{
					for (const auto* const candidate : matched)
					{
						auto state = item_presence::similar_in;

						if (candidate->file_modified < request.file_modified) state = item_presence::older_in;
						else if (candidate->file_modified > request.file_modified)
							state = item_presence::newer_in;

						auto duplicates = candidate->duplicates;
						duplicates.grade = df::copy_grade::same_picture;
						auto& current = matches[request_index];

						if (presence_rank(state) > presence_rank(current.state))
						{
							current.state = state;
							current.duplicates = duplicates;
						}
					}
				}
			}
		}

		i = group_end;
	}
}

void index_state::queue_update_presence(const df::item_set& items)
{
	df::assert_true(ui::is_ui_thread());
	if (items.empty()) return;

	std::vector<presence_request> requests;
	requests.reserve(items.size());
	for (const auto& item : items.items())
	{
		const auto ft = item->file_type();
		const auto md = item->metadata();
		requests.emplace_back(item, item->path(), item->file_size(), item->file_modified(),
		                      item->media_created(), item->crc32c(), item->is_folder(),
		                      ft && ft->has_trait(file_traits::bitmap),
		                      md ? md->dimensions() : sizei{});
	}

	_async.queue_async(async_queue::index_presence_single, [this, requests = std::move(requests)]() mutable
	{
		df::measure_ms ms(stats.update_presence_ms);
		df::index_folder_info_map indexed_folders;
		std::vector<presence_match> matches(requests.size());

		for (const auto& request : requests)
		{
			if (!request.is_folder)
			{
				const auto folder = _items.find(request.path.folder());
				if (folder && folder->is_in_collection)
				{
					indexed_folders[request.path.folder()] = folder;
				}
			}
		}

		std::vector<std::pair<uint32_t, size_t>> items_possible_hashes;
		// Outside photographs whose capture time a member might share, keyed exactly rather than by the
		// folded hash above, because a picture comparison is too expensive to run on a fold collision.
		df::hash_map<uint64_t, std::vector<size_t>> requests_by_capture_time;

		for (size_t index = 0; index < requests.size(); ++index)
		{
			const auto& request = requests[index];
			if (request.is_folder) continue;

			const auto is_indexed_folder = indexed_folders.contains(request.path.folder());

			if (is_indexed_folder)
			{
				auto& match = matches[index];
				match.state = item_presence::this_in;

				const auto& folder = indexed_folders.at(request.path.folder());
				const auto file = find_file(folder->files, request.path.name());
				if (file != folder->files.end()) match.duplicates = file->duplicates.load();
			}
			else
			{
				if (request.crc32c)
				{
					items_possible_hashes.emplace_back(request.crc32c, index);
				}

				if (request.media_created.is_valid())
				{
					items_possible_hashes.emplace_back(x64to32(request.media_created.to_int64()), index);

					if (request.is_bitmap)
					{
						requests_by_capture_time[request.media_created.to_int64()].push_back(index);
					}
				}

				items_possible_hashes.emplace_back(request.path.name().ihash(), index);
			}
		}

		std::vector<presence_similar_candidate> similar_candidates;

		if (!items_possible_hashes.empty())
		{
			std::ranges::sort(items_possible_hashes, [](auto&& left, auto&& right)
			{
				return left.first < right.first;
			});
			const auto folders = _items.all_folders();

			for (const auto& ifn : folders)
			{
				if (ifn.second->is_in_collection)
				{
					for (const auto& file : ifn.second->files)
					{
						if (file.crc32c)
						{
							items_possible_hashes_contains(matches, items_possible_hashes, requests, file,
							                               file.crc32c);
						}

						const auto created_date = file.created();

						if (created_date.is_valid())
						{
							items_possible_hashes_contains(matches, items_possible_hashes, requests, file,
							                               x64to32(created_date.to_int64()));

							const auto shares_time = requests_by_capture_time.find(created_date.to_int64());

							if (shares_time != requests_by_capture_time.end() &&
								file.ft->has_trait(file_traits::bitmap))
							{
								const auto held = file.phash.load();
								const auto rotations = held ? held->rotations : crypto::phash_rotations{};
								const auto file_md = file.metadata.load(); // important to hold ref
								const auto file_dims = file_md ? file_md->dimensions() : sizei{};

								for (const auto request_index : shares_time->second)
								{
									similar_candidates.emplace_back(request_index,
									                                df::file_path(ifn.first, file.name),
									                                rotations,
									                                file.file_modified.load(),
									                                file.duplicates.load(),
									                                file_dims);
								}
							}
						}

						items_possible_hashes_contains(matches, items_possible_hashes, requests, file,
						                               file.name.ihash());
					}
				}
			}
		}

		resolve_similar_presence(*this, requests, matches, similar_candidates);

		for (size_t index = 0; index < matches.size(); ++index)
		{
			auto& match = matches[index];
			const auto& request = requests[index];
			if (request.is_folder)
			{
				match = {};
				continue;
			}

			if (match.state == item_presence::unknown)
			{
				auto evidence_complete = false;
				if (_fully_loaded)
				{
					const auto folder = _items.find(request.path.folder());
					if (folder)
					{
						const auto file = find_file(folder->files, request.path.name());
						evidence_complete = file != folder->files.end() &&
							!needs_scan_impl(folder, *file, false, false);
					}
				}
				match.state = evidence_complete ? item_presence::not_in : item_presence::unknown;
				match.duplicates = df::duplicate_info{};
			}
		}

		const auto result_count = requests.size();
		std::vector<presence_result> results;
		results.reserve(result_count);
		for (size_t index = 0; index < result_count; ++index)
		{
			results.emplace_back(std::move(requests[index]), matches[index]);
		}

		_async.queue_ui([this, results = std::move(results)]
		{
			auto changed = false;
			for (const auto& result : results)
			{
				const auto item = result.source.lifetime.lock();
				if (!item || item->path() != result.source.path || item->file_size() != result.source.size ||
					item->file_modified() != result.source.file_modified ||
					item->media_created() != result.source.media_created || item->crc32c() != result.source.crc32c)
				{
					continue;
				}

				if (item->presence() == result.match.state && item->duplicates() == result.match.duplicates)
				{
					continue;
				}

				item->presence(result.match.state);
				item->duplicates(result.match.duplicates);
				changed = true;
			}

			if (changed)
			{
				_async.invalidate_view(view_invalid::view_layout | view_invalid::group_layout);
			}
		});

		df::trace(std::format("Index update presence {} items in {} ms", result_count, stats.update_presence_ms));
	});
}

index_state::item_scan_request index_state::make_scan_request(const df::item_element_ptr& item,
                                                              const bool load_thumbnail, const bool claim_loading)
{
	df::assert_true(ui::is_ui_thread());
	const auto is_folder = item->is_folder();
	const auto thumbnail_needed = load_thumbnail && !is_folder && item->should_load_thumbnail();
	if (thumbnail_needed && claim_loading) item->is_loading_thumbnail(true);
	return {
		item, item->path(), item->folder(), item->file_type(), is_folder, load_thumbnail && !is_folder,
		thumbnail_needed, item->has_thumb()
	};
}

index_state::item_scan_requests index_state::make_scan_requests(const df::item_set& items,
                                                                const bool load_thumbnails)
{
	df::assert_true(ui::is_ui_thread());
	item_scan_requests requests;
	requests.reserve(items.size());

	for (const auto& item : items.items())
	{
		requests.emplace_back(make_scan_request(item, load_thumbnails));
	}

	return requests;
}

bool index_state::scan_items(const df::item_set& items_to_scan, const bool load_thumbs,
                             const bool refresh_from_file_system, const bool only_if_needed,
                             const bool scan_if_offline, const df::cancel_token& token, const bool force)
{
	return scan_items(make_scan_requests(items_to_scan, load_thumbs), refresh_from_file_system, only_if_needed,
	                  scan_if_offline, token, force);
}

bool index_state::scan_items(const item_scan_requests& requests,
                             const bool refresh_from_file_system,
                             const bool only_if_needed,
                             const bool scan_if_offline,
                             const df::cancel_token& token,
                             const bool force,
                             scan_batch_stats* stats_out)
{
	auto metadata_refresh_needed = false;
	uint64_t thumbs_scanned = 0;
	std::vector<std::pair<std::weak_ptr<df::item_element>, df::file_path>> updated_items;
	updated_items.reserve(requests.size());
	{
		df::measure_ms ms(stats.scan_items_ms);
		df::scope_locked_inc l(scanning_items);
		df::folder_groups items_by_folder;
		items_by_folder.build(requests,
		                      [](const item_scan_request& r) { return r.folder; },
		                      [](const item_scan_request& r) { return !r.is_folder; });

		const auto now = platform::now();
		db_write_batch writes(*this);

		for (const auto& ff : items_by_folder.groups())
		{
			if (token.is_cancelled()) break;

			const auto node = validate_folder(ff.folder, refresh_from_file_system, now);
			if (!node.folder) continue;

			for (const auto request_index : items_by_folder.elements(ff))
			{
				const auto& request = requests[request_index];

				if (token.is_cancelled()) break;

				if (!only_if_needed || request.thumbnail_needed)
				{
					const auto found_file = find_file(node.folder->files, request.path.name());
					const auto is_new = found_file == node.folder->files.end();
					// Thumbnail generation on its own is not a metadata refresh, so it must not force a regroup.
					const auto metadata_scan_wanted = is_new || force ||
						needs_scan_impl(node.folder, *found_file, false, scan_if_offline);
					const auto scan_possible = metadata_scan_wanted || request.thumbnail_needed;
					const auto scanned_before = is_new ? df::date_t{} : found_file->metadata_scanned.load();

					scan_item(node.folder, request.path, request.load_thumbnail, request.thumbnail_needed,
					          request.had_thumbnail, scan_if_offline, request.lifetime, true, request.file_type, force,
					          false, false, &writes);

					if (request.thumbnail_needed) ++thumbs_scanned;

					if (metadata_scan_wanted)
					{
						// A file that cannot be scanned never advances metadata_scanned, so it reports
						// needs_scan on every pass - summarise on effect, not on intent.
						const auto found_after = find_file(node.folder->files, request.path.name());
						metadata_refresh_needed |= found_after != node.folder->files.end() &&
							!(found_after->metadata_scanned.load() == scanned_before);
					}

					// Items already carry the index record the query materialised them from, so a republish
					// is only worth a UI hop when a scan could run or validate_folder refreshed the folder.
					if (scan_possible || node.was_updated)
					{
						updated_items.emplace_back(request.lifetime, request.path);
					}
				}
			}

			// Per folder, so a long batch neither holds the rows nor loses more than one folder's work.
			writes.flush();
		}

		struct folder_update
		{
			std::weak_ptr<df::item_element> lifetime;
			df::folder_path folder;
			df::index_folder_item_ptr info;
			df::count_and_size total;
		};

		std::vector<folder_update> folder_updates;

		for (const auto& request : requests)
		{
			if (token.is_cancelled()) break;

			if (request.is_folder)
			{
				const auto folder_path = request.folder;
				const auto node = validate_folder(folder_path, refresh_from_file_system, now);
				// Re-summarising a folder is not a metadata refresh. Reporting one unconditionally made
				// every scan of a listing that contains a folder re-invalidate index_summary (and
				// group_layout via queue_scan_displayed_items), which never settled.
				metadata_refresh_needed |= node.was_updated;
				folder_updates.emplace_back(request.lifetime, folder_path, node.folder,
				                            platform::calc_folder_summary(folder_path, setting.show_hidden, token));
			}
		}

		// One publication for the whole batch rather than a UI hop per folder.
		if (!folder_updates.empty())
		{
			_async.queue_ui([folder_updates = std::move(folder_updates)]
			{
				for (const auto& update : folder_updates)
				{
					const auto item = update.lifetime.lock();
					if (item && item->is_folder() && item->folder() == update.folder)
					{
						item->update_folder(update.info, update.total);
					}
				}
			});
		}
	}

	std::vector<std::pair<std::weak_ptr<df::item_element>, df::file_path>> completed;
	completed.reserve(requests.size());
	for (const auto& request : requests)
	{
		if (request.thumbnail_needed) completed.emplace_back(request.lifetime, request.path);
	}

	if (stats_out)
	{
		stats_out->thumbs_requested = completed.size();
		stats_out->thumbs_scanned = thumbs_scanned;
	}

	if (!completed.empty() || !updated_items.empty())
	{
		_async.queue_ui([this, completed = std::move(completed), updated_items = std::move(updated_items), token]
		{
			for (const auto& [item, path] : completed)
			{
				const auto current_item = item.lock();
				if (current_item && current_item->path() == path)
				{
					current_item->is_loading_thumbnail(false);
					// Staging must run even when the batch was cancelled: this path publishes with
					// stage_surface false, so anything loaded before the cancel would stay unstaged and
					// the item would never ask for its thumbnail again.
					current_item->stage_thumbnail_surface(_async);
				}
				else
				{
					df::bump(df::thumbnail_perf.scan_completions_stale);
				}
			}

			auto layout_changed = false;

			for (const auto& [item, path] : updated_items)
			{
				const auto current_item = item.lock();
				if (!current_item || current_item->path() != path) continue;

				const auto current_info = find_item(path);
				if (current_info.name == path.name()) layout_changed |= current_item->update(path, current_info);
			}

			// A republished item can change its tile geometry - a newly scanned size, or cover art the
			// tile is now shaped by - and nothing else in this hop asks for the layout it needs.
			if (layout_changed) _async.invalidate_view(view_invalid::view_layout | view_invalid::view_redraw);
		});
	}

	const auto load_thumbs = std::ranges::any_of(requests, [](const auto& request)
	{
		return request.load_thumbnail;
	});
	df::trace(std::format("Index scan {} items (thumbs={} refresh-fs={}) in {} ms", requests.size(),
	                      load_thumbs,
	                      refresh_from_file_system, stats.scan_items_ms));
	if (metadata_refresh_needed)
	{
		_async.invalidate_view(view_invalid::index_summary);
	}
	return metadata_refresh_needed;
}

void index_state::scan_folder(const df::folder_path folder_path, const df::index_folder_item_ptr& folder)
{
	for (const auto& f : folder->files)
	{
		scan_item(folder, {folder_path, f.name}, false, false, false, false, {}, false, f.ft);
	}

	for (const auto& f : *folder->folders_snapshot())
	{
		queue_scan_folder(folder_path.combine(f->name));
	}
}

bool index_state::scan_folder(const df::folder_path folder_path, const bool mark_is_indexed, const df::date_t timestamp)
{
	df::scope_locked_inc l(scanning_items);
	const auto node = validate_folder(folder_path, true, timestamp);
	if (!node.folder) return false;
	node.folder->is_in_collection = mark_is_indexed;
	scan_folder(folder_path, node.folder);

	if (node.folder->is_in_collection && node.was_updated)
	{
		_async.invalidate_view(view_invalid::index_summary);
	}

	return node.was_updated;
}

void index_state::queue_scan_listed_items(const df::item_set& listed_items)
{
	static std::atomic_int version;
	df::cancel_token token(version);
	auto requests = make_scan_requests(listed_items, false);

	_async.queue_async(async_queue::scan_folder, [this, requests = std::move(requests), token]
	{
		// Presence matching walks every file in every indexed folder for every listed item, so it is only
		// worth doing when this scan actually moved an index record.
		if (scan_items(requests, false, false, false, token))
		{
			_async.invalidate_view(view_invalid::presence);
		}
	});
}

void index_state::claim_for_write(const std::vector<df::file_path>& paths)
{
	df::assert_true(ui::is_ui_thread());

	for (const auto& path : paths)
	{
		++_write_claims[path];
	}
}

void index_state::release_write_claim(const std::vector<df::file_path>& paths)
{
	df::assert_true(ui::is_ui_thread());

	for (const auto& path : paths)
	{
		const auto found = _write_claims.find(path);
		if (found == _write_claims.end()) continue;
		if (--found->second <= 0) _write_claims.erase(found);
	}

	if (_deferred_modified_scans.empty()) return;

	df::item_set still_claimed;
	df::item_set ready;

	for (const auto& i : _deferred_modified_scans.items())
	{
		if (_write_claims.contains(i->path())) still_claimed.add(i);
		else ready.add(i);
	}

	_deferred_modified_scans = std::move(still_claimed);

	// Deferred because a write was running, so the index record is stale by construction.
	if (!ready.empty()) queue_scan_modified_items(std::move(ready), true);
}

void index_state::queue_scan_modified_items(df::item_set items_to_scan, const bool force)
{
	df::assert_true(ui::is_ui_thread());

	if (!_write_claims.empty())
	{
		df::item_set unclaimed;

		for (const auto& i : items_to_scan.items())
		{
			if (_write_claims.contains(i->path())) _deferred_modified_scans.add(i);
			else unclaimed.add(i);
		}

		if (unclaimed.empty()) return;
		items_to_scan = std::move(unclaimed);
	}

	auto requests = make_scan_requests(items_to_scan, true);
	_async.queue_async(async_queue::scan_modified_items, [this, requests = std::move(requests), force]
	{
		const auto start_ms = df::now_ms();
		scan_items(requests, true, false, false, {}, force);
		_async.invalidate_view(view_invalid::index_summary | view_invalid::media_elements |
			view_invalid::presence);
		df::trace(std::format("Index scan modified {} items in {} ms", requests.size(),
		                      df::now_ms() - start_ms));
	});
}

void index_state::queue_scan_displayed_items(df::item_set visible)
{
	df::assert_true(ui::is_ui_thread());

	if (!_write_claims.empty())
	{
		df::item_set unclaimed;

		// No loading claim has been made yet, so a skipped item is simply re-offered by
		// items_view::retry_visible_thumbnails once the write releases it.
		for (const auto& i : visible.items())
		{
			if (!_write_claims.contains(i->path())) unclaimed.add(i);
		}

		if (unclaimed.empty()) return;
		visible = std::move(unclaimed);
	}

	// Visible-thumbnail cancellation invariant (see also queue_scan_offline_thumbnails): a batch may
	// EITHER use a cancel token OR mark items with a batch-wide "pending" flag up front, never both --
	// a cancelled batch that pre-marked a batch flag would leave unprocessed items stuck forever.
	// This (local) path uses a cancel token: a fresh token per call cancels the previous in-flight
	// batch so scrolling abandons work for items scrolled past. It marks NO batch-wide flag; the only
	// per-item loading claim is made on UI while building the immutable request batch and is cleared
	// by path-checked UI completion even under cancellation. That release is why the batch must still
	// reach the worker; items_view::retry_visible_thumbnails re-requests whatever a cancelled batch
	// abandoned, since the visible set stops changing once scrolling stops.
	static std::atomic_int version;
	df::cancel_token token(version);
	auto requests = make_scan_requests(visible, true);

	// Depth is the gauge that shows whether scroll enqueues batches faster than the worker drains
	// them - the cost this queue pays for using enqueue rather than reset_and_enqueue.
	df::bump(df::thumbnail_perf.scan_batches_queued);
	df::record_peak(df::thumbnail_perf.scan_batches_pending_peak,
	                df::thumbnail_perf.scan_batches_pending.fetch_add(1, std::memory_order_relaxed) + 1);

	_async.queue_async(async_queue::scan_displayed_items, [this, requests = std::move(requests), token]
	{
		df::scope_locked_inc thumbnailing(thumbnailing_items);
		df::thumbnail_perf.scan_batches_pending.fetch_sub(1, std::memory_order_relaxed);

		if (!requests.empty())
		{
			scan_batch_stats stats;
			const auto metadata_refresh_needed = scan_items(requests, false, true, false, token, false, &stats);

			df::bump(df::thumbnail_perf.scan_batches);
			if (token.is_cancelled()) df::bump(df::thumbnail_perf.scan_batches_cancelled);
			df::bump(df::thumbnail_perf.scan_thumbs_requested, stats.thumbs_requested);
			df::bump(df::thumbnail_perf.scan_thumbs_scanned, stats.thumbs_scanned);

			_async.queue_ui([this, token, metadata_refresh_needed]
			{
				if (token.is_cancelled()) return;
				_async.invalidate_view(metadata_refresh_needed
					                       ? view_invalid::view_layout | view_invalid::group_layout
					                       : view_invalid::view_redraw);
			});
		}
	});
}

void index_state::queue_stage_thumbnails(const df::item_elements& items)
{
	if (items.empty()) return;

	df::assert_true(ui::is_ui_thread());
	for (const auto& item : items)
	{
		item->stage_thumbnail_surface(_async, true);
	}
}

void index_state::publish_thumbnail(std::weak_ptr<df::item_element> item, df::file_path path,
                                    ui::const_image_ptr thumbnail, ui::const_image_ptr cover_art,
                                    const df::date_t timestamp,
                                    const bool fade_in, const bool stage_surface) const
{
	_async.queue_ui([this, item = std::move(item), path = std::move(path), thumbnail = std::move(thumbnail),
			cover_art = std::move(cover_art), timestamp, fade_in, stage_surface]() mutable
		{
			const auto current_item = item.lock();
			if (!current_item || current_item->path() != path) return;

			const auto previous_dims = current_item->layout_dims();
			const auto previous_orientation = current_item->layout_orientation();
			current_item->thumbnail(std::move(thumbnail), std::move(cover_art), timestamp, fade_in);
			const auto geometry_changed = previous_dims != current_item->layout_dims() ||
				previous_orientation != current_item->layout_orientation();
			if (stage_surface)
			{
				current_item->stage_thumbnail_surface(_async, true);
			}
			else
			{
				_async.invalidate_view(view_invalid::view_redraw);
			}
			if (geometry_changed) _async.invalidate_view(view_invalid::view_layout);
		});
}

void index_state::publish_thumbnails(thumbnail_results results, const bool invalidate_group_layout) const
{
	if (results.empty() && !invalidate_group_layout) return;

	_async.queue_ui([this, results = std::move(results), invalidate_group_layout]() mutable
	{
		auto geometry_changed = false;
		df::bump(df::thumbnail_perf.published_db, results.size());

		for (auto& result : results)
		{
			const auto item = result.lifetime.lock();
			if (!item || item->path() != result.path) continue;

			const auto previous_dims = item->layout_dims();
			const auto previous_orientation = item->layout_orientation();
			item->thumbnail(std::move(result.thumbnail), std::move(result.cover_art), result.timestamp);
			geometry_changed = geometry_changed || previous_dims != item->layout_dims() ||
				previous_orientation != item->layout_orientation();
		}

		_async.invalidate_view(invalidate_group_layout || geometry_changed
			                       ? view_invalid::view_redraw | view_invalid::view_layout
			                       : view_invalid::view_redraw);
	});
}

void index_state::publish_item_update(std::weak_ptr<df::item_element> item, df::file_path path) const
{
	_async.queue_ui([this, item = std::move(item), path = std::move(path)]
	{
		const auto current_item = item.lock();
		if (!current_item || current_item->path() != path) return;

		const auto current_info = find_item(path);
		if (current_info.name != path.name()) return;

		if (current_item->update(path, current_info))
		{
			_async.invalidate_view(view_invalid::view_layout | view_invalid::view_redraw);
		}
	});
}

void index_state::publish_thumbnail_failure(std::weak_ptr<df::item_element> item, df::file_path path) const
{
	_async.queue_ui([item = std::move(item), path = std::move(path)]
	{
		const auto current_item = item.lock();
		if (!current_item || current_item->path() != path) return;

		df::bump(df::thumbnail_perf.load_failures);
		current_item->failed_loading_thumbnail(true);
	});
}

void index_state::publish_crc(std::weak_ptr<df::item_element> item, df::file_path path, const df::file_size size,
                              const df::item_online_status online_status, const uint32_t existing_crc,
                              const uint32_t crc)
{
	_async.queue_ui([this, item = std::move(item), path = std::move(path), size, online_status, existing_crc, crc]
	{
		const auto current_item = item.lock();
		if (!current_item || current_item->path() != path || current_item->file_size() != size ||
			current_item->online_status() != online_status || current_item->crc32c() != existing_crc)
		{
			return;
		}

		save_crc(path, crc);
		current_item->crc32c(crc);
		// A checksum changes no tile geometry, so a redraw is enough; relayout here re-wrapped the
		// grid under the pointer while duplicate detection worked through a folder.
		_async.invalidate_view(view_invalid::view_redraw | view_invalid::presence);
	});
}

void index_state::queue_load_visible_thumbnails(const df::item_elements& visible)
{
	df::assert_true(ui::is_ui_thread());
	if (visible.empty()) return;

	queue_stage_thumbnails(visible);

	auto queue_sources = [this](const df::item_elements& items)
	{
		df::item_elements local;
		df::item_elements offline;
		for (const auto& item : items)
		{
			item->add_if_thumbnail_load_needed(local);
			item->add_if_shell_thumbnail_needed(offline);
		}
		if (!local.empty()) queue_scan_displayed_items(std::move(local));
		if (!offline.empty()) queue_scan_offline_thumbnails(std::move(offline));
	};

	database::thumbnail_requests requests;
	df::item_elements resolved;
	for (const auto& item : visible)
	{
		if (item->begin_db_thumbnail_query())
		{
			requests.emplace_back(item, item->path(), item->folder(), item->is_folder(), item->has_thumb());
		}
		else
		{
			resolved.emplace_back(item);
		}
	}

	if (!requests.empty())
	{
		_async.queue_database([this, requests = std::move(requests)](const database& db)
		{
			db.load_thumbnails(*this, requests);
			_async.queue_ui([this, requests = std::move(requests)]
			{
				df::item_elements current_visible;
				current_visible.reserve(requests.size());
				for (const auto& request : requests)
				{
					auto item = request.lifetime.lock();
					if (item && item->path() == request.path && item->is_visible())
						current_visible.emplace_back(std::move(item));
				}
				queue_load_visible_thumbnails(std::move(current_visible));
			});
		});
	}

	queue_sources(resolved);
}

void index_state::queue_load_thumbnail(df::item_element_ptr item)
{
	if (!item || item->has_thumb()) return;

	// The database hop copies this lambda, so the item crosses a worker queue: ui_owned_ptr hands
	// the final reference back to the UI thread if a truncated queue drops it there.
	auto load_from_source = [this, item = ui_owned(_async, item)]
	{
		if (item->has_thumb())
		{
			queue_stage_thumbnails({item.shared()});
			_async.invalidate_view(view_invalid::tooltip | view_invalid::view_redraw);
			return;
		}

		df::item_set items;
		item->add_to(items);

		if (item->online_status() == df::item_online_status::offline)
		{
			queue_scan_offline_thumbnails(std::move(items), false);
		}
		else if (item->should_load_thumbnail())
		{
			auto requests = make_scan_requests(items, true);
			_async.queue_async(async_queue::scan_folder, [this, requests = std::move(requests)]
			{
				df::scope_locked_inc thumbnailing(thumbnailing_items);
				scan_items(requests, false, true, false, {});
				_async.invalidate_view(view_invalid::tooltip | view_invalid::view_redraw);
			});
		}
	};

	if (item->begin_db_thumbnail_query())
	{
		df::item_set items;
		item->add_to(items);
		auto requests = database::make_thumbnail_requests(items);
		_async.queue_database([this, requests = std::move(requests), load_from_source](const database& db)
		{
			db.load_thumbnails(*this, requests);
			_async.queue_ui(load_from_source);
		});
	}
	else
	{
		load_from_source();
	}
}

void index_state::queue_scan_offline_thumbnails(const df::item_set& items, const bool visible_only)
{
	// Cloud (OneDrive) thumbnail fetch for VISIBLE offline placeholders. Unlike the local
	// displayed-items path (queue_scan_displayed_items), this does NOT use a cancel token: it is
	// re-enqueued on every layout to drive the view_state::tick retry of icon-only items, and a
	// cancel token would let each re-enqueue cancel the in-flight batch and strand its pre-marked
	// items. Instead, staying on the visible set is done PER ITEM: each item carries an is_visible()
	// flag maintained by items_view::update_visible_items_list, and the batch skips (and re-arms) any
	// item that has scrolled out of view since it was queued -- so scrolling around a large folder
	// abandons stale screenfuls instead of grinding through them. The worker cannot read that
	// UI-owned flag, so a batch counter tells it when a newer visible set has been queued and the
	// rest of its own items are stale. Items are marked shell_thumbnail_pending up front to
	// de-duplicate concurrent batches; that flag is cleared on every exit path (fetched, skipped or
	// failed) so nothing is ever left stuck pending.

	df::assert_true(ui::is_ui_thread());

	struct request
	{
		std::weak_ptr<df::item_element> lifetime;
		df::file_path path;
		df::date_t modified;
		uint64_t generation = 0;
		bool eligible = false;
		bool abandoned = false;
	};

	struct result
	{
		request source;
		platform::get_cached_file_properties_response response =
			platform::get_cached_file_properties_response::fail;
		ui::const_image_ptr thumbnail;
	};

	std::vector<request> requests;
	requests.reserve(items.size());
	for (const auto& i : items.items())
	{
		const auto eligible = (!visible_only || i->is_visible()) && !i->is_folder() &&
			i->online_status() == df::item_online_status::offline;
		requests.emplace_back(i, i->path(), i->file_modified(), i->begin_thumbnail_request(), eligible);
	}

	// The single-item hover path does not supersede a visible batch.
	const auto batch = visible_only ? ++_offline_thumbnail_batch : _offline_thumbnail_batch.load();

	_async.queue_async(async_queue::cloud, [this, requests = std::move(requests), visible_only, batch]() mutable
	{
		df::scope_locked_inc thumbnailing(thumbnailing_items);
		std::vector<result> results;
		results.reserve(requests.size());

		for (auto& request : requests)
		{
			// On shutdown the UI publication pass will not run at all, so there is nothing left to
			// clear and the remaining requests are simply dropped.
			if (df::is_closing) break;

			// A newer visible set has been queued, so the rest of this batch is stale. Report it as
			// abandoned rather than breaking out: the UI pass must still clear every pending claim, and
			// items that are still visible are re-armed for the next retry pass.
			if (visible_only && batch != _offline_thumbnail_batch.load(std::memory_order_relaxed))
			{
				request.abandoned = true;
			}

			if (!request.eligible || request.abandoned)
			{
				results.emplace_back(request, platform::get_cached_file_properties_response::pending, nullptr);
				continue;
			}

			ui::const_image_ptr shell_thumb;
			const auto response = platform::get_shell_thumbnail(
				request.path, setting.thumbnail_max_dimension, true, shell_thumb);
			result completed{request, response, {}};

			if (response == platform::get_cached_file_properties_response::ok && is_valid(shell_thumb))
			{
				// Downscale/re-encode to the thumbnail size budget, matching scan_offline_item.
				files ff;

				auto thumbnail_image = shell_thumb;
				const auto max_extent = setting.thumbnail_max_dimension;
				const auto thumb_extent = thumbnail_image->dimensions();

				if (max_extent.cx < thumb_extent.cx || max_extent.cy < thumb_extent.cy)
				{
					auto surf = ff.image_to_surface(thumbnail_image, max_extent, false, {},
					                                decode_intent::thumbnail);
					thumbnail_image = ff.surface_to_thumbnail(surf);
				}

				if (is_valid(thumbnail_image) && thumbnail_image->data().size() < df::two_fifty_six_k)
				{
					completed.thumbnail = thumbnail_image;
					save_thumbnail(request.path, thumbnail_image, {}, request.modified);
				}
			}

			results.emplace_back(std::move(completed));
		}

		_async.queue_ui([this, results = std::move(results), visible_only]() mutable
		{
			auto geometry_changed = false;
			for (auto& completed : results)
			{
				const auto item = completed.source.lifetime.lock();
				if (!item || !item->is_current_thumbnail_request(completed.source.generation)) continue;

				item->shell_thumbnail_pending(false);
				if (!completed.source.eligible || (visible_only && !item->is_visible()) || item->is_folder() ||
					item->online_status() != df::item_online_status::offline)
				{
					continue;
				}

				if (completed.source.abandoned)
				{
					// Superseded by a newer visible set before it was fetched - re-arm it for the retry
					// pass without spending one of its bounded provider attempts.
					item->shell_thumbnail_retry_pending(true, false);
					continue;
				}

				if (is_valid(completed.thumbnail))
				{
					df::bump(df::thumbnail_perf.published_shell);
					const auto previous_dims = item->layout_dims();
					const auto previous_orientation = item->layout_orientation();
					item->thumbnail(std::move(completed.thumbnail), {}, completed.source.modified, true);
					geometry_changed = geometry_changed || previous_dims != item->layout_dims() ||
						previous_orientation != item->layout_orientation();
					item->stage_thumbnail_surface(_async);
				}
				else if (completed.response == platform::get_cached_file_properties_response::pending)
				{
					df::bump(df::thumbnail_perf.shell_retries);
					item->shell_thumbnail_retry_pending(true);
				}
				else
				{
					df::bump(df::thumbnail_perf.load_failures);
					item->failed_loading_thumbnail(true);
				}
			}

			_async.invalidate_view(view_invalid::tooltip | view_invalid::view_redraw |
				(geometry_changed ? view_invalid::view_layout : view_invalid::none));
		});
	});
}

// Both passes walk the whole index, and apply_scan_result raises view_invalid::index_summary per
// scanned item, so a request arrives on every UI drain while a scan is running. The delay collapses
// that burst into one pass; the queue holds it, so no worker thread is spent waiting it out.
constexpr uint32_t summary_debounce_ms = 333;

void index_state::queue_update_predictions()
{
	const auto generation = ++_predictions_generation;

	_async.queue_async_after(async_queue::index_predictions_single, summary_debounce_ms, [this, generation]
	{
		if (df::is_closing) return;
		if (generation != _predictions_generation.load()) return;

		update_predictions();
		if (generation != _predictions_generation.load()) return;

		// Predictions only feed the sidebar; asking for refresh_items here closed a cycle that never
		// reached a steady state. Counts only: nothing about which rows exist has changed, so rebuilding
		// them would discard every sidebar text layout to publish a number.
		_async.invalidate_view(view_invalid::sidebar_counts);
	});
}

void index_state::queue_update_summary()
{
	uint64_t generation;
	{
		platform::exclusive_lock lock(_summary_rw);
		generation = ++_summary_generation;
	}

	_async.queue_async_after(async_queue::index_summary_single, summary_debounce_ms, [this, generation]
	{
		if (df::is_closing) return;
		{
			platform::shared_lock lock(_summary_rw);
			if (generation != _summary_generation) return;
		}

		update_summary(generation);
		{
			platform::shared_lock lock(_summary_rw);
			if (generation != _summary_generation) return;
		}
		// update_summary already asked for the rebuild its new vocabulary needs; this only has to
		// re-earn the counts that vocabulary changed.
		_async.invalidate_view(view_invalid::sidebar_counts);
	});
}

std::vector<str::cached> index_state::distinct_genres() const
{
	index_metadata_summary_const_ptr summary;
	{
		platform::shared_lock lock(_summary_rw);
		summary = _summary._metadata;
	}
	const auto found = summary->_distinct_text.find(prop::genre);
	return found != summary->_distinct_text.end()
		       ? std::vector<str::cached>{found->second.begin(), found->second.end()}
		       : std::vector<str::cached>{};
}

std::vector<std::string> index_state::auto_complete_text(const prop::key_ref key)
{
	index_metadata_summary_const_ptr summary;
	{
		platform::shared_lock lock(_summary_rw);
		summary = _summary._metadata;
	}
	const auto found = summary->_distinct_text.find(key);
	return found != summary->_distinct_text.end()
		       ? std::vector<std::string>{found->second.begin(), found->second.end()}
		       : std::vector<std::string>{};
}

void index_state::queue_validate_changed_folders(df::unique_folders paths)
{
	_async.queue_async(async_queue::scan_folder, [this, paths = std::move(paths)]
	{
		const auto now = platform::now();
		auto changed = false;

		for (const auto& path : paths)
		{
			// A large set is still a bounded walk, but nothing here is worth holding a worker open
			// for while the application is closing.
			if (df::is_closing) return;
			changed |= validate_folder(path, true, now).was_updated;
		}

		if (changed)
		{
			_async.invalidate_view(view_invalid::refresh_items);
		}
	});
}

void index_state::queue_scan_folder(const df::folder_path path)
{
	_async.queue_async(async_queue::scan_folder, [this, path]
	{
		const auto now = platform::now();
		scan_folder(path, is_in_collection(path), now);
	});
}

void index_state::queue_scan_folders(df::unique_folders paths)
{
	_async.queue_async(async_queue::scan_folder, [this, paths = std::move(paths)]
	{
		const auto now = platform::now();
		auto changed = false;

		for (const auto& path : paths)
		{
			if (df::is_closing) return;
			changed |= scan_folder(path, is_in_collection(path), now);
		}

		// Only a folder that differs needs the search re-run, and only the batch asks for it: the
		// recursive per-folder path would re-open the search once per folder it walked.
		_async.invalidate_view(changed
			                       ? view_invalid::view_layout | view_invalid::refresh_items
			                       : view_invalid::view_layout);
	});
}

void index_state::merge_folder(const df::folder_path folder_path, const db_items_t& items)
{
	const auto found_in_index = _items.find(folder_path);
	df::index_folder_item_ptr folder_node;

	if (found_in_index && !found_in_index->files.empty())
	{
		folder_node = found_in_index;

		df::assert_true(std::is_sorted(folder_node->files.begin(), folder_node->files.end()));

		auto file_first = items.begin();
		const auto file_last = items.end();
		auto old_first = folder_node->files.begin();
		const auto old_last = folder_node->files.end();

		while (file_first != file_last && old_first != old_last)
		{
			const auto d = icmp(file_first->path, old_first->name);

			if (d < 0)
			{
				// skip: only in new					
				++file_first;
			}
			else if (d > 0)
			{
				// skip: only in old
				++old_first;
			}
			else
			{
				// merge: in both
				old_first->metadata = file_first->metadata;
				old_first->metadata_scanned = file_first->metadata_scanned;
				old_first->crc32c = file_first->crc32c;
				old_first->phash = picture_hashes_from_db(file_first->phash);
				old_first->calc_search_presence();
				++file_first;
				++old_first;
			}
		}

		// the merged metadata can both add and remove bits, so rebuild the summary rather than
		// OR-ing into the stale one
		folder_node->reset_search_presence();
	}
	else
	{
		df::index_item_infos files;
		files.resize(std::distance(items.begin(), items.end()));
		auto node = files.begin();

		for (auto i = items.begin(); i != items.end(); ++i)
		{
			const auto metadata = i->metadata;
			const auto id = i->path;
			const auto* const mt = files::file_type_from_name(i->path);

			auto& file_node = *node;
			file_node.name = id;
			file_node.ft = mt;
			file_node.metadata = metadata;
			file_node.crc32c = i->crc32c;
			file_node.phash = picture_hashes_from_db(i->phash);
			file_node.metadata_scanned = i->metadata_scanned;

			file_node.calc_search_presence();
			++node;
		}

		df::assert_true(std::is_sorted(files.begin(), files.end()));

		folder_node = std::make_shared<df::index_folder_item>(std::move(files));
		folder_node->name = folder_path.name();

		if (found_in_index)
		{
			// the replacement stands in for the same folder, so it must keep what the scan learned
			folder_node->is_in_collection = found_in_index->is_in_collection.load();
			folder_node->is_excluded = found_in_index->is_excluded.load();
			folder_node->is_read_only = found_in_index->is_read_only;
			folder_node->volume = found_in_index->volume;
			folder_node->created = found_in_index->created;
			folder_node->modified = found_in_index->modified;
			folder_node->child_folders = found_in_index->folders_snapshot();
		}

		folder_node->reset_search_presence();
		_items.replace(folder_path, folder_node);
	}
}

static df::count_and_size sum_items(const df::index_folder_info_const_ptr& folder)
{
	df::count_and_size result;

	for (const auto& sub_folder : *folder->folders_snapshot())
	{
		result += sum_items(sub_folder);
	}

	for (const auto& file : folder->files)
	{
		result.size += file.size;
		++result.count;
	}

	return result;
}

std::vector<index_state::folder_total> index_state::includes_with_totals() const
{
	df::unique_folders includes;

	{
		platform::shared_lock lock(_summary_rw);
		includes = _summary._roots.folders;
	}

	std::vector<folder_total> results;
	results.reserve(includes.size());

	for (const auto& f : includes)
	{
		df::file_size size;
		uint64_t count = 0;
		const auto existing_folder = _items.find(f);

		if (existing_folder)
		{
			const auto sum_result = sum_items(existing_folder);
			size = sum_result.size;
			count = sum_result.count;
		}

		results.emplace_back(f, count, size);
	}

	return results;
}

// Rebuilds the case-insensitively-sorted vocabulary lookup before its immutable publication.
bool index_state::rebuild_sorted_words(index_metadata_summary& summary, const uint64_t generation) const
{
	auto& sorted = summary._sorted_words;
	sorted.clear();
	sorted.reserve(summary._distinct_words.size());

	for (const auto& kv : summary._distinct_words) sorted.emplace_back(kv.first);

	std::ranges::sort(sorted, [](const std::string_view a, const std::string_view b) { return str::icmp(a, b) < 0; });
	if (generation != 0)
	{
		platform::shared_lock lock(_summary_rw);
		if (generation != _summary_generation) return false;
	}

	// Trigram index over the vocabulary (term-id == index into _sorted_words). This accelerates
	// the per-keystroke *substring* prediction path, which would otherwise scan every term.
	summary._word_trigrams = df::trigram_index{};

	for (uint32_t i = 0; i < sorted.size(); ++i)
	{
		if (generation != 0 && (i & 255u) == 0)
		{
			platform::shared_lock lock(_summary_rw);
			if (generation != _summary_generation) return false;
		}
		summary._word_trigrams.add(i, sorted[i]);
	}

	summary._word_trigrams.freeze();

	return true;
}

std::vector<index_state::auto_complete_word> index_state::auto_complete_words(
	const std::string_view query, const size_t max_results)
{
	df::assert_true(!ui::is_ui_thread());

	std::vector<auto_complete_word> result;
	index_metadata_summary_const_ptr summary;
	{
		platform::shared_lock lock(_summary_rw);
		summary = _summary._metadata;
	}

	// Prefix matches first: a binary search over the sorted snapshot (O(log N + k)) rather
	// than a full scan of the vocabulary. Prefix hits are the strongest predictions and are
	// returned ahead of interior substring matches.
	const auto [prefix_first, prefix_last] = word_prefix_range(summary->_sorted_words, query);

	for (auto it = prefix_first; it != prefix_last && result.size() < max_results; ++it)
	{
		const auto count = summary->_distinct_words.find(*it);
		result.emplace_back(std::string(*it), std::vector<str::part_t>{{0, query.size()}},
		                    count == summary->_distinct_words.end() ? 0 : count->second.i);
	}

	// For longer queries, top up with interior substring matches the prefix pass cannot find,
	// skipping words already added as prefix matches. A single-token query (no spaces) matches
	// as a contiguous substring, so the vocabulary trigram index can supply the candidates and
	// we verify each with str::ifind2 - identical results to a full scan, but far fewer checks.
	// Multi-word queries use ifind2's word-gap semantics, which trigrams cannot bound, so those
	// (and queries too short for a trigram) fall back to a full scan.
	if (query.size() > 2 && result.size() < max_results)
	{
		// str::normalze_for_compare folds every whitespace form to a word gap, not just a space,
		// so any of them means the query has to take the full-scan path.
		const auto candidates = query.find_first_of(" \t\n\v\f\r") == std::string_view::npos
			                        ? summary->_word_trigrams.candidates(query)
			                        : std::nullopt;

		if (candidates)
		{
			for (const auto id : *candidates)
			{
				if (result.size() >= max_results) break;
				if (id >= summary->_sorted_words.size()) continue;

				const auto word = summary->_sorted_words[id];
				if (str::starts(word, query)) continue; // already added by the prefix pass

				const auto found = str::ifind2(word, query, 0);
				if (found.found)
				{
					const auto count = summary->_distinct_words.find(word);
					result.emplace_back(std::string(word), found.parts,
					                    count == summary->_distinct_words.end() ? 0 : count->second.i);
				}
			}
		}
		else
		{
			for (const auto& word : summary->_distinct_words)
			{
				if (result.size() >= max_results) break;
				if (str::starts(word.first, query)) continue; // already added by the prefix pass

				const auto found = str::ifind2(word.first, query, 0);
				if (found.found) result.emplace_back(std::string(word.first), found.parts, word.second.i);
			}
		}
	}

	return result;
}

std::vector<index_state::auto_complete_word> index_state::auto_complete_tag_companions(
	const std::vector<std::string>& tags, const std::string_view prefix, const size_t max_results) const
{
	std::vector<auto_complete_word> result;
	df::hash_map<std::string, int, df::ihash, df::ieq> scores;
	index_metadata_summary_const_ptr summary;
	{
		platform::shared_lock lock(_summary_rw);
		summary = _summary._metadata;
	}

	for (const auto& tag : tags)
	{
		const auto found = summary->_tag_companions.find(tag);
		if (found == summary->_tag_companions.end()) continue;

		for (const auto& [companion, count] : found->second)
		{
			if ((prefix.empty() || str::starts(companion, prefix)) &&
				std::ranges::none_of(tags, [&companion](const auto& existing)
				{
					return str::icmp(existing, companion) == 0;
				}))
			{
				scores[std::string(companion)] += count.i;
			}
		}
	}

	std::vector<std::pair<std::string, int>> ranked(scores.begin(), scores.end());
	std::ranges::sort(ranked, [](const auto& left, const auto& right)
	{
		return left.second == right.second
			       ? str::icmp(left.first, right.first) < 0
			       : left.second > right.second;
	});

	for (const auto& [tag, count] : ranked)
	{
		if (result.size() >= max_results) break;
		result.emplace_back(std::format("#{}", tag),
		                    prefix.empty() ? std::vector<str::part_t>{} : std::vector<str::part_t>{{1, prefix.size()}},
		                    count);
	}

	return result;
}

// locations.md 3.4: a completion the collection has no photos anywhere near is noise. The heat
// map is coordinate-based, so unlike the location groups it also counts GPS-only items that
// carry no place text -- which is the majority of a camera roll. One cell plus its neighbours is
// about the coarsest area a single place name can plausibly stand for.
static uint32_t collection_items_near(const df::location_heat_map& heat_map, const gps_coordinate coord)
{
	if (!coord.is_valid()) return 0;

	constexpr auto map_width = static_cast<int>(df::location_heat_map::map_width);
	constexpr auto map_height = static_cast<int>(df::location_heat_map::map_height);

	const auto cell = df::location_heat_map::calc_map_loc(coord);
	uint32_t total = 0;

	for (auto dy = -1; dy <= 1; ++dy)
	{
		const auto y = cell.y + dy;
		if (y < 0 || y >= map_height) continue;

		for (auto dx = -1; dx <= 1; ++dx)
		{
			// Longitude wraps, latitude does not.
			const auto x = ((cell.x + dx) % map_width + map_width) % map_width;
			total += heat_map.coordinates[y * map_width + x];
		}
	}

	return total;
}

// locations.md 3.4 + 3.5: a place completion commits the canonical location term, never the bare
// name. A bare name would run a text search over stored fields, which is exactly the empty result
// the one-location-vocabulary rule exists to remove.
std::vector<index_state::auto_complete_word> index_state::auto_complete_locations(
	const std::string_view query, const size_t max_results, const df::location_level level) const
{
	std::vector<auto_complete_word> result;
	if (query.empty()) return result;

	const auto summary = histograms();
	df::hash_set<std::string, df::ihash, df::ieq> seen;

	for (const auto& match : _locations.auto_complete(query, static_cast<uint32_t>(max_results * 4),
	                                                  setting.default_location))
	{
		std::string name;

		switch (level)
		{
		case df::location_level::state: name = std::string(match.location.state.sv());
			break;
		case df::location_level::country: name = std::string(match.location.country.sv());
			break;
		default: name = qualified_name(match.location);
			break;
		}

		if (name.empty() || !seen.emplace(name).second) continue;

		// A level-qualified scope collapses many gazetteer rows onto one label, so the row has to
		// have matched THAT label: "Frankfurt" is a fine hit for `loc:franc` and a wrong one for
		// `country:franc`, where the answer is France.
		if ((level == df::location_level::state || level == df::location_level::country) &&
			str::ifind(name, query) == std::string_view::npos)
		{
			continue;
		}

		auto text = df::search_t().location(name, level).text();
		std::vector<str::part_t> highlights;

		if (const auto found = str::ifind(text, query); found != std::string_view::npos)
		{
			highlights.emplace_back(found, query.size());
		}

		result.emplace_back(std::move(text), std::move(highlights),
		                    static_cast<int>(collection_items_near(summary->_locations, match.location.position)));
	}

	// A place the collection actually holds photos near is the one the user meant; the gazetteer
	// order (exact spelling, then population) decides the rest.
	std::ranges::stable_sort(result, [](const auto& left, const auto& right)
	{
		return left.occurrences > right.occurrences;
	});

	if (result.size() > max_results) result.resize(max_results);
	return result;
}

std::vector<index_state::auto_complete_folder> index_state::auto_complete_folders(
	const std::string_view query, const size_t max_results) const
{
	df::assert_true(!ui::is_ui_thread());

	std::vector<auto_complete_folder> result;

	// Retain the immutable folder snapshots under a shared lock, then match unlocked.
	std::array<std::shared_ptr<const df::unique_folders>, 3> folder_sets;

	{
		platform::shared_lock lock(_summary_rw);
		folder_sets[0] = _summary._distinct_prime_folders;
		folder_sets[1] = _summary._distinct_folders;
		folder_sets[2] = _summary._distinct_other_folders;
	}

	for (const auto& folders : folder_sets)
	{
		if (query.size() > 2)
		{
			for (const auto& folder : *folders)
			{
				auto name_pos = folder.is_root() ? 0u : folder.find_last_slash() + 1;
				if (name_pos == std::string_view::npos) name_pos = 0;
				auto found = str::ifind2(folder.text().substr(name_pos), query, name_pos);

				if (!found.found && name_pos != 0)
				{
					found = ifind2(folder.text(), query, 0);
				}

				if (found.found)
				{
					result.emplace_back(folder, found.parts);
					if (result.size() > max_results) break;
				}
			}
		}
		else
		{
			for (const auto& folder : *folders)
			{
				const auto name_pos = folder.is_root() ? 0u : folder.find_last_slash() + 1;

				if (name_pos != std::string_view::npos && str::starts(folder.text().substr(name_pos), query))
				{
					result.emplace_back(folder, std::vector<str::part_t>{{name_pos, query.size()}});
					if (result.size() > max_results) break;
				}
				else if (starts(folder.text(), query))
				{
					result.emplace_back(folder, std::vector<str::part_t>{{0, query.size()}});
					if (result.size() > max_results) break;
				}
			}
		}
	}

	return result;
}
