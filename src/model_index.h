// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

#include "model_items.h"

struct search_part;
class location_cache;
class database;
class async_strategy;

struct item_db_write
{
	df::file_path path;

	std::optional<prop::item_metadata_ptr> md;
	std::optional<df::date_t> modified;
	std::optional<double> media_position;
	std::optional<ui::const_image_ptr> thumb;
	std::optional<ui::const_image_ptr> cover_art;
	std::optional<df::date_t> thumb_scanned;
	std::optional<df::date_t> metadata_scanned;
	std::optional<uint32_t> crc32c;

	item_db_write() noexcept = default;
	item_db_write(const item_db_write&) = delete;
	item_db_write& operator=(const item_db_write&) = delete;
	item_db_write(item_db_write&&) noexcept = default;
	item_db_write& operator=(item_db_write&&) noexcept = default;
};

struct db_item_t
{
	str::cached path;
	df::date_t metadata_scanned;
	prop::item_metadata_ptr metadata;
	uint32_t crc32c = 0;

	db_item_t() noexcept = default;
	db_item_t(const db_item_t&) = delete;
	db_item_t& operator=(const db_item_t&) = delete;
	db_item_t(db_item_t&&) noexcept = default;
	db_item_t& operator=(db_item_t&&) noexcept = default;
};

struct key_val
{
	str::cached key;
	str::cached val;

	key_val() noexcept = default;

	key_val(const str::cached k, const str::cached v) noexcept : key(k), val(v)
	{
	}

	bool operator<(const key_val& other) const
	{
		return compare(other) < 0;
	}

	bool operator==(const key_val& other) const
	{
		return compare(other) == 0;
	}

	bool operator!=(const key_val& other) const
	{
		return compare(other) != 0;
	}

	int compare(const key_val& other) const
	{
		const auto f = icmp(key, other.key);
		return f == 0 ? icmp(val, other.val) : f;
	}
};

struct phash
{
	size_t operator()(const key_val& r) const
	{
		crypto::hash_gen h;
		h.append(r.key);
		h.append(r.val);
		return h.result();
	}
};

struct peq
{
	bool operator()(const key_val& l, const key_val& r) const
	{
		return l.compare(r) == 0;
	}
};

struct index_statistic
{
	int index_folder_count = 0;
	int index_item_remaining = 0;
	int items_saved = 0;
	int thumbs_saved = 0;

	int media_item_count = 0;
	int index_item_count = 0;
	int indexed_dup_folder_count = 0;
	int indexed_max_compare_count = 0;
	int indexed_crc_count = 0;

	int index_load_ms = 0;
	int predictions_ms = 0;
	int count_matches_ms = 0;

	int scan_items_ms = 0;
	int update_presence_ms = 0;

	df::file_size database_size;
	df::file_path database_path;
};


using unique_key_vals = df::hash_map<key_val, df::int_counter, phash, peq>;
using db_items_t = std::vector<db_item_t>;
using items_by_folder_t = df::dense_hash_map<df::folder_path, df::index_folder_item_ptr, df::ihash, df::ieq>;

using item_writes_t = platform::queue<item_db_write>;
using index_folders_t = std::vector<std::pair<df::folder_path, df::index_folder_item_ptr>>;

class index_items
{
private:
	mutable platform::mutex _rw;
	_Guarded_by_(_rw) items_by_folder_t _index;	

public:
	df::index_folder_item_ptr find(const df::folder_path folder) const
	{
		platform::shared_lock lock(_rw);
		const auto found_in_index = _index.find(folder);
		return found_in_index != _index.end() ? found_in_index->second : nullptr;
	}

	void replace(const df::folder_path folder_path, df::index_folder_item_ptr i)
	{
		platform::exclusive_lock lock(_rw);

		_index[folder_path] = i;

		// update parent to point to this
		if (!folder_path.is_root())
		{
			const auto parent_folder = folder_path.parent();
			const auto found_in_index = _index.find(parent_folder);

			if (found_in_index != _index.end())
			{
				const auto parent_node = found_in_index->second;
				const auto folder_name = i->name;
				const auto lb = std::lower_bound(parent_node->folders.begin(), parent_node->folders.end(), folder_name,
				                                 [](const df::index_folder_item_ptr& l, const std::u8string_view r)
				                                 {
					                                 return icmp(l->name, r) < 0;
				                                 });

				if (lb != parent_node->folders.end() && icmp((*lb)->name, folder_name) == 0)
				{
					std::swap(*lb, i);
				}
			}
		}
	}

	void erase(const std::vector<df::folder_path>& folders)
	{
		platform::exclusive_lock lock(_rw);

		for (const auto& g : folders)
		{
			const auto found_in_index = _index.find(g);

			if (found_in_index != _index.end())
			{
				_index.erase(found_in_index);
			}
		}
	}

	index_folders_t all_folders() const
	{
		platform::shared_lock lock(_rw);
		index_folders_t result(_index.begin(), _index.end());
		return result;
	}

	df::index_folder_item_ptr find_or_create(const df::folder_path folder_path)
	{
		{
			platform::shared_lock lock(_rw);
			const auto found_in_index = _index.find(folder_path);
			if (found_in_index != _index.end())
			{
				return found_in_index->second;
			}
		}

		{
			platform::exclusive_lock lock(_rw);
			auto result = std::make_shared<df::index_folder_item>();
			_index[folder_path] = result;
			return result;
		}
	}

	void clear()
	{
		platform::exclusive_lock lock(_rw);
		_index.clear();
	}
};

__forceinline bool is_dup_match(const df::index_file_item* file, const df::index_file_item* other_file)
{
	if (file->crc32c != 0 && file->crc32c == other_file->crc32c)
	{
		return true;
	}

	const auto name_match = icmp(file->name, other_file->name) == 0;

	if (name_match && file->ft->has_trait(file_traits::av))
	{
		if (file->size == other_file->size)
		{
			return true;
		}
	}

	return name_match && file->created() == other_file->created();
}

__forceinline bool is_dup_match(const df::index_file_item& file, const df::item_element_ptr& other_file)
{
	if (file.crc32c != 0 && file.crc32c == other_file->crc32c())
	{
		return true;
	}

	const auto name_match = icmp(file.name, other_file->path().name()) == 0;

	if (file.ft->has_trait(file_traits::av))
	{
		if (name_match && file.size == other_file->file_size())
		{
			return true;
		}
	}

	return name_match && file.created() == other_file->media_created();
}

__forceinline bool is_dup_match(const df::related_info& file, const df::index_file_item& other_file)
{
	if (file.group != 0 && file.group == other_file.duplicates.group)
	{
		return true;
	}

	if (file.crc32c != 0 && file.crc32c == other_file.crc32c)
	{
		return true;
	}

	const auto name_match = icmp(file.path.name(), other_file.name) == 0;

	if (file.ft->has_trait(file_traits::av))
	{
		if (name_match && file.size == other_file.size)
		{
			return true;
		}
	}

	return name_match && file.created() == other_file.created();
}

struct location_group
{
	str::cached name;
	uint32_t count = 0;
	pointi loc;

	bool operator==(const location_group& other) const
	{
		return name == other.name &&
			count == other.count &&
			loc == other.loc;
	}

	bool operator !=(const location_group& other) const
	{
		return name != other.name ||
			count != other.count ||
			loc != other.loc;
	}
};

struct index_histograms
{
	index_histograms() noexcept = default;
	~index_histograms() = default;
	index_histograms(const index_histograms&) = default;
	index_histograms& operator=(const index_histograms&) = default;
	index_histograms(index_histograms&&) noexcept = default;
	index_histograms& operator=(index_histograms&&) noexcept = default;

	df::file_group_histogram _file_types;
	df::date_histogram _dates;
	df::location_heat_map _locations;
	df::hash_map<uint32_t, location_group> _location_groups;

	void record(const location_cache& locations, const df::index_file_item& file);
};

using strings_by_prop = df::hash_map<prop::key_ref, df::dense_unique_strings>;
using prop_text_summary = df::hash_map<std::u8string_view, df::file_group_histogram, df::ihash, df::ieq>;
using prop_num_summary = std::array<df::file_group_histogram, 6>;

struct index_summary
{
	index_summary() noexcept = default;
	~index_summary() = default;
	index_summary(const index_summary&) = delete;
	index_summary& operator=(const index_summary&) = delete;
	index_summary(index_summary&&) noexcept = delete;
	index_summary& operator=(index_summary&&) noexcept = delete;

	df::index_roots _roots;
	strings_by_prop _distinct_text;
	df::dense_string_counts _distinct_words;
	df::unique_folders _distinct_folders;
	df::unique_folders _distinct_other_folders;
	df::unique_folders _distinct_prime_folders;

	prop_text_summary _distinct_tags;
	prop_text_summary _distinct_labels;
	prop_num_summary _distinct_ratings;

	index_histograms _histograms;
};

struct folder_scan_item
{
	df::folder_path folder;
	df::index_file_item item;
};

class index_state : public df::no_copy
{
private:
	async_strategy& _async;

	platform::mutex _summary_rw;

	index_items _items;
	_Guarded_by_(_summary_rw) index_summary _summary;
	item_writes_t _db_writes;

	bool _cache_items_loaded = false;
	bool _folders_indexed = false;
	const location_cache& _locations;
	bool _fully_loaded = false;

	void calc_folder_summary(const df::index_folder_info_const_ptr& folder, df::file_group_histogram& result,
	                         df::cancel_token token) const;
	bool is_collection_search(const df::search_t& search) const;

public:
	explicit index_state(async_strategy& as, const location_cache& locations);

	void init_item_index();
	void reset();

	void invalidate_view(view_invalid invalid) const;

	std::vector<std::pair<df::file_path, df::index_file_item>> duplicate_list(uint32_t group) const;

	std::atomic_int scanning_items = 0;
	std::atomic_int thumbnailing_items = 0;
	std::atomic_int detecting = 0;
	std::atomic_int indexing = 0;
	std::atomic_int searching = 0;

	index_statistic stats;

	void cache_load_complete()
	{
		_cache_items_loaded = true;
	}

	void merge_folder(df::folder_path folder_path, const db_items_t& items);

	void save_media_position(df::file_path id, double media_position);
	void save_crc(df::file_path id, uint32_t crc);
	void save_location(df::file_path id, const location_t& loc);
	void save_thumbnail(df::file_path id, const ui::const_image_ptr& thumbnail_image, const ui::const_image_ptr& cover_art, df::date_t scan_timestamp);

	df::index_file_item find_item(df::file_path id) const;

	df::file_group_histogram calc_folder_summary(df::folder_path path, df::cancel_token token) const;
	df::file_group_histogram count_matches(const df::search_t& a, df::cancel_token token);

	df::file_group_histogram label_summary(const std::u8string_view label) const
	{
		platform::shared_lock lock(_summary_rw);
		const auto found = _summary._distinct_labels.find(label);
		return found != _summary._distinct_labels.end() ? found->second : df::file_group_histogram{};
	}

	df::file_group_histogram tag_summary(const std::u8string_view tag) const
	{
		platform::shared_lock lock(_summary_rw);
		const auto found = _summary._distinct_tags.find(tag);
		return found != _summary._distinct_tags.end() ? found->second : df::file_group_histogram{};
	}

	df::file_group_histogram rating_summary(int r) const
	{
		platform::shared_lock lock(_summary_rw);
		if (r == 0 || r > 5) return {};
		if (r == -1) r = 0;
		return _summary._distinct_ratings[r];
	}

	void update_predictions();
	void update_summary();
	void update_presence(const df::item_set& items);

	void query_items(const df::search_t& search, const df::unique_items& existing,
	                 const std::function<void(df::item_set, bool)>& found_callback, df::cancel_token token);

	struct validate_folder_result
	{
		df::index_folder_item_ptr folder;
		bool was_updated = false;
	};

	validate_folder_result validate_folder(df::folder_path folder_path,
	                                          bool refresh_from_file_system, df::date_t timestamp);
	void scan_item(const df::index_folder_item_ptr& folder, df::file_path file_path, bool load_thumbnails,
	               bool scan_if_offline, const df::item_element_ptr& i, file_type_ref ft);
	void scan_item(const df::item_element_ptr& i, bool load_thumb, bool scan_if_offline);
	bool needs_scan(const df::item_element_ptr&) const;
	bool is_indexed(df::folder_path folder) const;

	void index_folders(df::cancel_token token);
	void index_roots(df::index_roots roots);
	void scan_uncached(df::cancel_token token);
	std::vector<folder_scan_item> scan_items(const df::index_roots& roots, bool recursive, bool scan_if_offline,
	                                         df::cancel_token token);
	void scan_items(const df::item_set& items, bool load_thumbs, bool refresh_from_file_system, bool only_if_needed,
	                bool
	                scan_if_offline, df::cancel_token token);
	void scan_folder(df::folder_path folder_path, const df::index_folder_item_ptr& folder);
	void scan_folder(df::folder_path folder_path, bool mark_is_indexed, df::date_t timestamp);

	void queue_scan_folder(df::folder_path path);
	void queue_scan_folders(df::unique_folders paths);
	void queue_scan_listed_items(df::item_set listed_items);
	void queue_scan_modified_items(df::item_set items_to_scan);
	void queue_scan_displayed_items(df::item_set visible);
	void queue_update_presence(df::item_set items);
	void queue_update_predictions();
	void queue_update_summary();

	std::vector<df::file_path> all_indexed_items() const
	{
		const auto folders = _items.all_folders();
		std::vector<df::file_path> results;
		results.reserve(10000);

		for (const auto& ff : folders)
		{
			for (const auto& file : ff.second->files)
			{
				results.emplace_back(ff.first, file.name);
			}
		}

		return results;
	}

	void save_all_cached_items()
	{
		const auto folders = _items.all_folders();

		for (const auto& folder : folders)
		{
			if (folder.second->is_indexed)
			{
				for (const auto& file : folder.second->files)
				{
					const auto md = file.metadata.load();
					if (file.ft->can_cache() && md)
					{
						item_db_write write;
						write.path = {folder.first, file.name};
						write.md = md;
						write.metadata_scanned = file.metadata_scanned;
						_db_writes.enqueue(std::move(write));
					}
				}
			}
		}
	}

	item_writes_t& db_writes()
	{
		return _db_writes;
	}

	df::unique_folders distinct_folders() const
	{
		platform::shared_lock lock(_summary_rw);
		return _summary._distinct_folders;
	}

	index_histograms histograms() const
	{
		platform::shared_lock lock(_summary_rw);
		return _summary._histograms;
	}

	df::file_group_histogram file_types() const
	{
		platform::shared_lock lock(_summary_rw);
		return _summary._histograms._file_types;
	}

	std::vector<str::cached> distinct_genres() const;

	df::dense_string_counts distinct_words() const
	{
		platform::shared_lock lock(_summary_rw);
		return _summary._distinct_words;
	}

	using distinct_results = std::vector<std::pair<std::u8string_view, df::file_group_histogram>>;

	distinct_results distinct_tags() const
	{
		platform::shared_lock lock(_summary_rw);
		return {_summary._distinct_tags.begin(), _summary._distinct_tags.end()};
	}

	distinct_results distinct_labels() const
	{
		platform::shared_lock lock(_summary_rw);
		return {_summary._distinct_labels.begin(), _summary._distinct_labels.end()};
	}

	prop_num_summary distinct_ratings() const
	{
		platform::shared_lock lock(_summary_rw);
		return _summary._distinct_ratings;
	}

	df::index_roots index_roots() const
	{
		platform::shared_lock lock(_summary_rw);
		return _summary._roots;
	}

	struct folder_total
	{
		df::folder_path folder;
		uint32_t count = 0;
		df::file_size size;
	};

	std::vector<folder_total> includes_with_totals() const;

	bool is_init_complete() const
	{
		return _cache_items_loaded && _folders_indexed;
	}

	struct auto_complete_word
	{
		std::u8string text;
		std::vector<str::part_t> highlights;
	};

	struct auto_complete_folder
	{
		df::folder_path path;
		std::vector<str::part_t> highlights;
	};

	std::vector<auto_complete_word> auto_complete_words(std::u8string_view query, size_t max_results);
	std::vector<auto_complete_folder> auto_complete_folders(std::u8string_view query, size_t max_results) const;
	std::vector<std::u8string> auto_complete_text(prop::key_ref key);
};
