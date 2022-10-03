// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "pch.h"

#include "model_index.h"
#include "model_db.h"
#include "model_locations.h"
#include "model_propery.h"
#include "model.h"
#include "metadata_xmp.h"
#include "util_crash_files_db.h"
#include "util_text.h"

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

constexpr auto max_folders_to_index = 100000;

static_assert(std::is_trivially_copyable_v<df::file_path>);
static_assert(std::is_trivially_copyable_v<df::file_group_histogram>);
static_assert(std::is_trivially_copyable_v<bloom_bits>);
static_assert(std::is_trivially_copyable_v<key_val>);
static_assert(std::is_move_constructible_v<df::index_file_item>);

static_assert(sizeof(bloom_bits) == 4);
static_assert(sizeof(df::file_path) == sizeof(void*) * 2);
//static_assert(sizeof(df::index_file_info) == 48);
//static_assert(sizeof(df::index_folder_info) == 88);
//static_assert(sizeof(prop::item_metadata) == 250);

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

struct dup_key
{
	str::cached name;
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

static dup_key calc_dup_key(const df::index_file_item& i, const df::file_path id)
{
	const auto md = i.metadata;

	if (md)
	{
		const auto cd = md->created();
		const auto normalised_created = cd.is_valid() ? cd : i.file_created.system_to_local();
		return {id.name(), normalised_created.to_int64()};
	}

	return {id.name(), i.file_created.system_to_local().to_int64()};
}

static dup_key calc_dup_key(const df::file_item_ptr& i)
{
	const auto md = i->metadata();

	if (md)
	{
		const auto cd = md->created();
		const auto normalised_created = cd.is_valid() ? cd : i->file_created().system_to_local();
		return {i->name(), normalised_created.to_int64()};
	}

	return {i->name(), i->file_created().system_to_local().to_int64()};
}

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
	const df::unique_items& existing;
	df::item_set results;

	void match_item(const df::file_path id, const df::index_file_item& file, const df::search_result& match,
	                const bool is_indexed_location)
	{
		auto found = existing.find(id);

		if (found)
		{
			found->update(id, file, is_indexed_location);
		}
		else
		{
			found = std::make_shared<df::file_item>(id, file, is_indexed_location);
		}

		found->search(match);
		results.add(found);
	}

	void match_folder(const df::folder_path folder_path, const df::index_folder_item_ptr& folder)
	{
		const auto path = folder_path;
		const auto found = existing.find(path);
		const auto ii = found ? found : std::make_shared<df::folder_item>(folder_path, folder);
		results.add(ii);
	}
};

struct count_items_result
{
	df::file_group_histogram summary;

	void match_item(const df::file_path id, const df::index_file_item& file, const df::search_result& match,
	                const bool is_indexed_location)
	{
		summary.record(file);
	}

	void match_folder(const df::folder_path folder_path, const df::index_folder_item_ptr& folder)
	{
	}
};

template <typename T>
static void iterate_items(const df::search_t& search,
                          T& results,
                          index_state& state,
                          df::cancel_token token,
                          index_items& index,
                          bool refresh_from_file_system,
                          bool show_sidecars)
{
	df::search_matcher matcher(search);

	const auto now = platform::now();
	const auto& selectors = search.selectors();
	const auto has_selector = !selectors.empty();

	if (has_selector)
	{
		for (const auto& selector : selectors)
		{
			if (token.is_cancelled())
				break;

			//df::log(__FUNCTION__, "query " << selector.str();

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

					const auto found_folder = state.validate_folder(current_folder, false, refresh_from_file_system,
					                                                now);

					if (found_folder)
					{
						for (const auto& folder_entry : found_folder->folders)
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
									else if (!matcher.has_terms || matcher.match_folder(folder_path.name()).is_match())
									{
										results.match_folder(folder_path, folder_entry);
									}
								}
							}
						}

						if (matcher.need_metadata)
						{
							for (const auto& f : found_folder->files)
							{
								if (token.is_cancelled())
									break;

								state.scan_item(found_folder, {current_folder, f.name}, false, false, nullptr, f.ft);
							}
						}

						if (matcher.potential_match(found_folder->bloom_filter))
						{
							const auto is_indexed_location = found_folder->is_indexed;

							for (const auto& file_node : found_folder->files)
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
											results.match_item(id, file, match, is_indexed_location);
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
		const auto has_related = search.has_related();
		const auto folders = index.all_folders();

		for (const auto& folder_node : folders)
		{
			if (token.is_cancelled()) break;

			if (folder_node.second->is_indexed)
			{
				if (has_related ||
					matcher.can_match_folder ||
					matcher.potential_match(folder_node.second->bloom_filter))
				{
					const auto is_indexed_location = folder_node.second->is_indexed;

					for (const auto& file_node : folder_node.second->files)
					{
						if (token.is_cancelled()) break;
						const auto& file = file_node;

						if (!(file.flags && df::index_item_flags::is_sidecar) || show_sidecars)
						{
							const auto path = folder_node.first.combine_file(file_node.name);
							const auto match = matcher.match_item(path, file);

							if (match.is_match())
							{
								results.match_item(path, file, match, is_indexed_location);
							}
						}
					}
				}

				const auto match = matcher.match_folder(folder_node.first.name());

				if (match.is_match())
				{
					results.match_folder(folder_node.first, folder_node.second);
				}
			}
		}
	}
}

void index_state::query_items(const df::search_t& search, const df::unique_items& existing,
                              const std::function<void(df::item_set, bool)>& found_callback, df::cancel_token token)
{
	df::scope_locked_inc l(searching);

	_async.invalidate_view(view_invalid::view_layout);

	if (search.has_related())
	{
		df::assert_true(search.related().is_loaded);
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

	query_items_result results{existing, {}};
	iterate_items(search, results, *this, token, _items, true, search.has_related());

	if (search.has_related())
	{
		const auto id = search.related().path;
		const auto found_related = existing.find(id);

		if (found_related && !found_related->is_indexed_location())
		{
			found_related->search({df::search_result_type::similar});
			results.results.add(found_related);
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

df::file_group_histogram index_state::count_matches(const df::search_t& search, df::cancel_token token)
{
	count_items_result result;

	if (is_collection_search(search))
	{
		df::measure_ms ms(stats.count_matches_ms);
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

void index_state::init_item_index()
{
	platform::exclusive_lock lock(_summary_rw);

	for (const auto& f : all_file_groups())
	{
		++_summary._distinct_words[str::cache(str::format(u8"@{}", f->name))];
		++_summary._distinct_words[str::cache(str::format(u8"@{}", f->plural_name))];
	}

	_summary._distinct_text[prop::genre] = df::dense_unique_strings
	{
		u8"Abstract"_c,
		u8"Action & Adventure"_c,
		u8"Action"_c,
		u8"Aerial"_c,
		u8"Alternative"_c,
		u8"Analog"_c,
		u8"Animation"_c,
		u8"Anime"_c,
		u8"Architectural"_c,
		u8"Aviation"_c,
		u8"Blues"_c,
		u8"Brazilian"_c,
		u8"Candid"_c,
		u8"Children's"_c,
		u8"Christian & Gospel"_c,
		u8"Classic"_c,
		u8"Classical"_c,
		u8"Close-up"_c,
		u8"Cloudscape"_c,
		u8"Comedy"_c,
		u8"Conceptual"_c,
		u8"Concert Films"_c,
		u8"Concert"_c,
		u8"Conservation"_c,
		u8"Country"_c,
		u8"Dance"_c,
		u8"Documentary"_c,
		u8"Drama"_c,
		u8"Easy Listening"_c,
		u8"Electronic"_c,
		u8"Family"_c,
		u8"Fashion"_c,
		u8"Film still"_c,
		u8"Fine-art"_c,
		u8"Fire"_c,
		u8"Fireworks"_c,
		u8"Fitness & Workout"_c,
		u8"Food"_c,
		u8"Foreign"_c,
		u8"Forensic"_c,
		u8"Geophotography"_c,
		u8"Glamour"_c,
		u8"High key"_c,
		u8"High-speed"_c,
		u8"Hip-Hop/Rap"_c,
		u8"Holiday"_c,
		u8"Horror"_c,
		u8"Independent"_c,
		u8"Instrumental"_c,
		u8"Jazz"_c,
		u8"Kids & Family"_c,
		u8"Kids"_c,
		u8"Kirlian"_c,
		u8"Landscape"_c,
		u8"Latin"_c,
		u8"Lifestyle"_c,
		u8"Lo-fi"_c,
		u8"Lomography"_c,
		u8"Long-exposure"_c,
		u8"Low key"_c,
		u8"Macro"_c,
		u8"Medical"_c,
		u8"Monochrome"_c,
		u8"Music Documentaries"_c,
		u8"Music Feature Films"_c,
		u8"Musicals"_c,
		u8"Narrative"_c,
		u8"New Age"_c,
		u8"Night"_c,
		u8"Nonfiction"_c,
		u8"Opera"_c,
		u8"Panorama"_c,
		u8"Panoramic"_c,
		u8"Pellier Noir"_c,
		u8"Photo op"_c,
		u8"Photobiography"_c,
		u8"Photojournalism"_c,
		u8"Photowalking"_c,
		u8"Podcast"_c,
		u8"Polaroid"_c,
		u8"Pop"_c,
		u8"Portrait"_c,
		u8"R&B"_c,
		u8"Reality TV"_c,
		u8"Reggae"_c,
		u8"Rock"_c,
		u8"Romance"_c,
		u8"Satellite"_c,
		u8"Sci-Fi & Fantasy"_c,
		u8"Short Films"_c,
		u8"Singer/Songwriter"_c,
		u8"Social"_c,
		u8"Soft focus"_c,
		u8"Soul"_c,
		u8"Soundtrack"_c,
		u8"Special Interest"_c,
		u8"Sports"_c,
		u8"Star trail"_c,
		u8"Still life"_c,
		u8"Stock"_c,
		u8"Street"_c,
		u8"Subminiature"_c,
		u8"Teens"_c,
		u8"Thriller"_c,
		u8"Time-lapse"_c,
		u8"Travel"_c,
		u8"Ultraviolet"_c,
		u8"Underwater"_c,
		u8"Urban"_c,
		u8"Vernacular"_c,
		u8"Vintage"_c,
		u8"Vocal"_c,
		u8"War"_c,
		u8"Western"_c,
		u8"World"_c
	};
}

void index_state::reset()
{
	_items.clear();
}


bool index_state::is_indexed(const df::folder_path folder) const
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
		return found_folder->is_indexed;
	}

	const auto found_parent = _items.find(parent);

	if (found_parent)
	{
		return found_parent->is_indexed;
	}

	return false;
}

static df::index_item_infos::iterator find_file(df::index_item_infos& files, const std::u8string_view name)
{
	const auto lb = std::lower_bound(files.begin(), files.end(), name);
	// , [](auto&& l, auto&& r) { return l._time < r._time; });
	if (lb != files.end() && *lb == name) return lb;
	return files.end();
}

static df::index_item_infos::const_iterator find_file(const df::index_item_infos& files, const std::u8string_view name)
{
	const auto lb = std::lower_bound(files.begin(), files.end(), name);
	// , [](auto&& l, auto&& r) { return l._time < r._time; });
	if (lb != files.end() && *lb == name) return lb;
	return files.end();
}

static df::index_folder_item_ptr find_or_create_folder(index_items& items, const df::folder_path path,
                                                       const platform::folder_info& fd)
{
	df::assert_true(!is_empty(fd.name));

	auto entry = items.find_or_create(path);
	entry->name = fd.name;
	entry->modified = fd.attributes.modified;
	entry->created = fd.attributes.created;
	entry->is_read_only = fd.attributes.is_readonly;
	return entry;
}

void populate_file_info(df::index_file_item& file_node, const platform::file_info& fd, const bool cache_items_loaded)
{
	df::assert_true(!is_empty(fd.name));

	const auto name = fd.name;
	file_node.file_modified = fd.attributes.modified;
	file_node.file_created = fd.attributes.created;
	if (fd.attributes.is_readonly) file_node.flags |= df::index_item_flags::is_read_only;
	if (fd.attributes.is_offline) file_node.flags |= df::index_item_flags::is_offline;
	file_node.size = fd.attributes.size;

	auto md = file_node.metadata;

	if (file_node.name != name)
	{
		file_node.name = name;
		file_node.ft = files::file_type_from_name(name);

		if (cache_items_loaded &&
			md == nullptr &&
			file_node.ft->has_trait(file_type_traits::file_name_metadata))
		{
			const auto ext_pos = df::find_ext(name);

			if (ext_pos != std::u8string_view::npos)
			{
				md = file_node.safe_ps();
				const auto name_props = scan_info_from_title(name.substr(0, ext_pos));

				if (!str::is_empty(name_props.show)) md->show = str::cache(name_props.show);
				if (!str::is_empty(name_props.title)) md->title = str::cache(name_props.title);
				if (name_props.year != 0) md->year = name_props.year;
				if (name_props.episode != 0) md->episode = df::xy8::make(name_props.episode, name_props.episode_of);
				if (name_props.season != 0) md->season = name_props.season;
			}
		}
	}

	if (md)
	{
		md->file_name = name;
	}
}

df::index_folder_item_ptr index_state::validate_folder(const df::folder_path folder_path, const bool mark_is_indexed,
                                                       const bool refresh_from_file_system, const df::date_t timestamp)
{
	auto existing_folder = _items.find(folder_path);

	if (refresh_from_file_system || !existing_folder)
	{
		bool changes_detected = false;

		df::index_item_infos updated_files;
		df::index_folder_infos updated_folders;
		std::vector<df::folder_path> removed_folders;
		std::unordered_multimap<std::u8string_view, str::cached, df::ihash, df::ieq> sidecars;
		df::hash_set<std::u8string_view, df::ihash, df::ieq> sidecar_extensions;

		auto less_ptr_name = [](const auto& a, const auto& b) { return str::icmp(a->name, b->name) < 0; };
		auto less_name = [](const auto& a, const auto& b) { return str::icmp(a.name, b.name) < 0; };
		auto less_id = [](const auto& a, const auto& b) { return str::icmp(a.name, b.name) < 0; };

		auto contents = platform::iterate_file_items(folder_path, setting.show_hidden);

		updated_folders.reserve(contents.folders.size());
		updated_files.reserve(contents.files.size());

		std::ranges::sort(contents.folders, less_name);
		std::ranges::sort(contents.files, less_name);

		auto folder_first = contents.folders.begin();
		const auto folder_last = contents.folders.end();

		if (existing_folder)
		{
			df::assert_true(std::ranges::is_sorted(contents.folders, less_name));
			df::assert_true(std::ranges::is_sorted(existing_folder->folders, less_ptr_name));

			if (contents.files.size() != existing_folder->files.size() ||
				contents.folders.size() != existing_folder->folders.size())
			{
				changes_detected = true;
			}

			auto old_first = existing_folder->folders.begin();
			const auto old_last = existing_folder->folders.end();

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
					if ((*old_first) != index_folder_item) changes_detected = true;
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
					info.metadata = old_first->metadata;
					info.metadata_scanned = old_first->metadata_scanned;
					info.crc32c = old_first->crc32c;
					populate_file_info(info, *file_first, _cache_items_loaded);
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

					if (str::icmp(ext, u8"xmp") == 0)
					{
						auto ps = f.safe_ps();

						if (f.metadata_scanned < f.file_modified)
						{
							metadata_xmp::parse(*ps, path);
							f.metadata_scanned = timestamp;
							changes_detected = true;
						}

						const auto raw_file = ps->raw_file_name;

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

					std::set<std::u8string_view> updated_sidecars;
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

					auto ps = file.safe_ps();
					auto combined = str::combine(updated_sidecars);

					if (icmp(ps->sidecars, combined) != 0)
					{
						ps->sidecars = str::cache(combined);
						changes_detected = true;
					}


					for (const auto& sc_name : updated_sidecars)
					{
						const auto ext = sc_name.substr(df::find_ext(sc_name));

						if (str::icmp(ext, u8".xmp") == 0)
						{
							ps->xmp = str::cache(sc_name);
						}

						const auto found = find_file(updated_files, sc_name);

						if (found != updated_files.end())
						{
							found->flags |= df::index_item_flags::is_sidecar;
						}
					}
				}
			}
		}

		for (const auto& f : updated_files)
		{
			f.calc_bloom_bits();
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
				folder_node->is_excluded = existing_folder->is_excluded;
				folder_node->is_indexed = mark_is_indexed || existing_folder->is_indexed;
				folder_node->volume = existing_folder->volume;
				folder_node->bloom_filter = existing_folder->bloom_filter;
				folder_node->created = existing_folder->created;
				folder_node->modified = existing_folder->modified;
			}
			else
			{
				folder_node->name = folder_path.name();
				folder_node->is_indexed = mark_is_indexed;
			}

			df::assert_true(!is_empty(folder_node->name));
			folder_node->reset_bloom_bits();

			_items.replace(folder_path, folder_node);
			_items.erase(removed_folders);

			if (folder_node->is_indexed)
			{
				_async.invalidate_view(view_invalid::index_summary);
			}

			return folder_node;
		}
	}

	{
		platform::exclusive_lock lock(_summary_rw);
		_summary._distinct_other_folders.emplace(folder_path);
	}

	return existing_folder;
}

std::vector<std::pair<df::file_path, df::index_file_item>> index_state::duplicate_list(const uint32_t group) const
{
	std::vector<std::pair<df::file_path, df::index_file_item>> result;
	const auto folders = _items.all_folders();

	for (const auto& ifn : folders)
	{
		if (ifn.second->is_indexed)
		{
			for (const auto& file : ifn.second->files)
			{
				if (file.duplicates.group == group)
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
	// bitmap hash
	// crc32c


	//std::unordered_multimap<uint32_t, const df::index_file_item*, dup_index_hash, dup_index_eq> dups;
	std::vector<std::pair<uint32_t, const df::index_file_item*>> dups;
	dups.reserve(1000000);

	int indexed_crc_count = 0;

	for (const auto& ifn : folders)
	{
		if (ifn.second->is_indexed)
		{
			for (const auto& file : ifn.second->files)
			{
				if (df::is_closing) return;
				const auto file_item_p = &file;
				const auto md = file.metadata; // important to hold ref

				if (md)
				{
					const auto cd = md->created();

					if (cd.is_valid())
					{
						dups.emplace_back(x64to32(cd.to_int64()), file_item_p);
					}
				}

				dups.emplace_back(crypto::fnv1a_i(file.name), file_item_p);
				dups.emplace_back(x64to32(file.file_created.to_int64()), file_item_p);

				if (file.crc32c)
				{
					dups.emplace_back(file.crc32c, file_item_p);
					++indexed_crc_count;
				}
				else
				{
					dups.emplace_back(x64to32(file.size.to_int64()), file_item_p);
				}
			}
		}
	}

	std::ranges::sort(dups, [](auto&& left, auto&& right) { return left.first < right.first; });

	df::hash_map<uint32_t, df::int_counter> group_counts;
	df::hash_map<uint32_t, uint32_t> group_coalesce;
	const auto& cdups = dups;

	int max_compare_count = 0;
	const auto dup_size = cdups.size();

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

			compare_count += 1;

			const auto* const file = it.second;
			const auto* const other_file = hit.second;

			if (file != other_file && is_dup_match(file, other_file))
			{
				if (file->duplicates.group == other_file->duplicates.group)
				{
					if (file->duplicates.group == 0)
					{
						other_file->duplicates.group = file->duplicates.group = ++next_dup_group;
					}
				}
				else
				{
					if (file->duplicates.group == 0)
					{
						file->duplicates.group = other_file->duplicates.group;
					}
					else if (other_file->duplicates.group == 0)
					{
						other_file->duplicates.group = file->duplicates.group;
					}
					else
					{
						if (!group_coalesce.contains(other_file->duplicates.group))
						{
							group_coalesce[file->duplicates.group] = other_file->duplicates.group;
						}
						else if (!group_coalesce.contains(file->duplicates.group))
						{
							group_coalesce[other_file->duplicates.group] = file->duplicates.group;
						}
					}
				}
			}
		}
	}

	for (const auto& ifn : folders)
	{
		if (ifn.second->is_indexed)
		{
			for (const auto& file : ifn.second->files)
			{
				if (df::is_closing) return;

				if (file.duplicates.group)
				{
					const auto found = group_coalesce.find(file.duplicates.group);

					if (found != group_coalesce.end())
					{
						file.duplicates.group = found->second;
					}
				}

				++group_counts[file.duplicates.group];
			}
		}
	}

	group_counts[0] = 1;

	for (const auto& ifn : folders)
	{
		if (ifn.second->is_indexed)
		{
			for (const auto& file : ifn.second->files)
			{
				if (df::is_closing) return;
				const auto count = static_cast<uint32_t>(group_counts[file.duplicates.group]);
				file.update_duplicates(ifn.second, {file.duplicates.group, count});
			}
		}
	}

	stats.indexed_dup_folder_count = static_cast<int>(group_counts.size()) - 1;
	stats.indexed_crc_count = indexed_crc_count;
	stats.indexed_max_compare_count = max_compare_count;
	stats.update_presence_ms = static_cast<int>(df::now_ms() - start_ms);

	df::trace(str::format(u8"Index update predictions: {} folders in {} ms", folder_count, stats.update_presence_ms));
}


static void add_words(df::dense_string_counts& distinct_words, strings_by_prop& distinct_text, const str::cached text,
                      const prop::key_ref key)
{
	count_ranges(distinct_words, text);
	distinct_text[key].emplace(text);
}


void index_state::update_summary()
{
	const auto start_ms = df::now_ms();

	prop_text_summary distinct_labels;
	prop_num_summary distinct_ratings;
	prop_text_summary distinct_tags;

	df::dense_string_counts distinct_words;
	strings_by_prop distinct_text;
	df::unique_folders distinct_other_folders;

	index_histograms histograms;

	auto& distinct_tag_texts = distinct_text[prop::tag];

	const auto folders = _items.all_folders();

	for (const auto& ifn : folders)
	{
		const auto is_indexed = ifn.second->is_indexed;

		if (is_indexed)
		{
			for (const auto& file : ifn.second->files)
			{
				if (df::is_closing) return;
				const auto md = file.metadata;

				if (md)
				{
					split2(md->tags, true,
					       [&distinct_words, &distinct_tags, &distinct_tag_texts, &file](const std::u8string_view part)
					       {
						       const auto cached_tag = str::cache(part);
						       distinct_tag_texts.emplace(cached_tag);
						       distinct_words[str::cache(str::format(u8"#{}", part))] += 1;
						       distinct_tags[cached_tag].record(file);
					       });

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
					if (!prop::is_null(md->genre)) add_words(distinct_words, distinct_text, md->genre, prop::genre);
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

					histograms.record(_locations, file);

					if (!prop::is_null(md->label)) distinct_labels[md->label].record(file);

					auto r = md->rating;

					if (r != 0 && r < 6)
					{
						if (r == -1) r = 0;
						distinct_ratings[r].record(file);
					}
				}
			}
		}

		if (!is_indexed)
		{
			distinct_other_folders.emplace(ifn.first);
		}
	}

	{
		platform::exclusive_lock lock(_summary_rw);

		for (const auto& v : distinct_other_folders)
		{
			_summary._distinct_other_folders.insert(v);
		}

		for (const auto& kv : distinct_text)
		{
			auto& dest = _summary._distinct_text[kv.first];

			for (const auto& v : kv.second)
			{
				dest.insert(v);
			}
		}

		for (const auto& kv : distinct_words)
		{
			_summary._distinct_words[kv.first] = kv.second;
		}

		_summary._distinct_labels = std::move(distinct_labels);
		_summary._distinct_ratings = distinct_ratings;
		_summary._distinct_tags = std::move(distinct_tags);
		_summary._histograms = std::move(histograms);
	}

	_async.invalidate_view(view_invalid::sidebar | view_invalid::tooltip);

	df::trace(str::format(u8"Index update summary in {} ms", df::now_ms() - start_ms));
}

static bool needs_scan_impl(const df::index_folder_item_ptr& f, const df::index_file_item& file, const bool load_thumb,
                            const bool scan_if_offline, const df::file_item_ptr& item)
{
	const auto is_offline = file.flags && df::index_item_flags::is_offline;

	if ((scan_if_offline || !is_offline) && file.ft->is_media())
	{
		if (item && load_thumb && item->should_load_thumbnail())
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


void index_state::scan_uncached(df::cancel_token token)
{
	df::scope_locked_inc l(indexing);
	std::vector<df::file_path> uncached;

	size_t items_in_index = 0;

	{
		// create list of uncached
		const auto folders = _items.all_folders();

		for (const auto& folder : folders)
		{
			if (folder.second->is_indexed)
			{
				for (const auto& file : folder.second->files)
				{
					if (file.ft->is_media())
					{
						items_in_index += 1;

						if (needs_scan_impl(folder.second, file, false, false, nullptr))
						{
							uncached.emplace_back(folder.first, file.name);
						}
					}
				}
			}
		}
	}

	stats.index_item_count = items_in_index;
	stats.index_item_remaining = uncached.size();

	_async.invalidate_view(view_invalid::view_layout);

	for (const auto& id : uncached)
	{
		if (token.is_cancelled()) break;

		const auto f = _items.find(id.folder());

		if (f)
		{
			scan_item(f, id, false, false, nullptr, files::file_type_from_name(id.name()));
		}

		--stats.index_item_remaining;
	}

	stats.index_item_remaining = 0;

	_async.invalidate_view(view_invalid::view_layout | view_invalid::group_layout);
	_fully_loaded = true;
}

std::vector<folder_scan_item> index_state::scan_items(const df::index_roots& roots, const bool recursive,
                                                      const bool scan_if_offline, df::cancel_token token)
{
	const auto now = platform::now();

	std::vector<folder_scan_item> results;
	std::vector<df::folder_path> folders_to_scan = {roots.folders.begin(), roots.folders.end()};

	while (!folders_to_scan.empty())
	{
		if (token.is_cancelled()) break;

		const auto folder_path = folders_to_scan.back();
		folders_to_scan.pop_back();

		if (!roots.excludes.contains(folder_path))
		{
			const auto node = validate_folder(folder_path, false, true, now);

			for (const auto& file : node->files)
			{
				if (token.is_cancelled()) break;
				scan_item(node, folder_path.combine_file(file.name), false, scan_if_offline, nullptr, file.ft);
				results.emplace_back(folder_path, file);
			}

			if (recursive)
			{
				for (const auto& sub_folder : node->folders)
				{
					folders_to_scan.emplace_back(folder_path.combine(sub_folder->name));
				}
			}
		}
	}

	for (const auto& file_path : roots.files)
	{
		const auto node = validate_folder(file_path.folder(), false, true, now);

		if (token.is_cancelled()) break;
		const auto found_file = find_file(node->files, file_path.name());

		if (found_file != node->files.end())
		{
			scan_item(node, file_path, false, scan_if_offline, nullptr, found_file->ft);
			results.emplace_back(file_path.folder(), *found_file);
		}
	}

	return results;
}

struct scope_locked_loading_thumbnail
{
	df::file_item_ptr _i;

	scope_locked_loading_thumbnail(df::file_item_ptr i) : _i(std::move(i))
	{
		_i->is_loading_thumbnail(true);
	}

	~scope_locked_loading_thumbnail()
	{
		_i->is_loading_thumbnail(false);
	}
};

void index_state::scan_item(const df::index_folder_item_ptr& folder,
                            const df::file_path file_path,
                            const bool load_thumb,
                            const bool scan_if_offline,
                            const df::file_item_ptr& item,
                            const file_type_ref ft)
{
	const auto now = platform::now();
	const auto found_file = find_file(folder->files, file_path.name());

	if (found_file != folder->files.end())
	{
		const auto& file = *found_file;

		if (needs_scan_impl(folder, file, load_thumb, scan_if_offline, item))
		{
			if (!crash_files.is_known_crash_file(file_path))
			{
				df::assert_true(ft->is_media());

				record_open_path record(crash_files, file_path, str::utf8_cast(__FUNCTION__));
				std::shared_ptr<scope_locked_loading_thumbnail> loading_lock;

				if (load_thumb && item && item->should_load_thumbnail())
				{
					loading_lock = std::make_shared<scope_locked_loading_thumbnail>(item);
				}

				files ff;
				const auto sr = ff.scan_file(file_path, load_thumb, ft, file.xmp(), setting.thumbnail_max_dimension);

				if (sr.success)
				{
					df::scope_locked_inc l(scanning_items);
					const auto* const mt = files::file_type_from_name(file_path);
					const auto metadata = sr.to_props();
					const auto thumbnail_was_loaded = is_valid(sr.thumbnail_surface) || is_valid(sr.thumbnail_image);

					if (mt->has_trait(file_type_traits::video_metadata))
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

					file_encode_params encode_params;
					encode_params.jpeg_save_quality = thumbnail_quality;

					ui::const_image_ptr thumbnail_image;
					ui::const_surface_ptr thumbnail_surface;

					if (is_valid(sr.thumbnail_surface))
					{
						const auto max_extent = setting.thumbnail_max_dimension;
						const auto thumb_extent = sr.thumbnail_surface->dimensions();

						if (max_extent.cx < thumb_extent.cx || max_extent.cy < thumb_extent.cy)
						{
							av_scaler scaler;
							const auto dims = ui::scale_dimensions(thumb_extent, max_extent, true);
							auto surf = std::make_shared<ui::surface>();
							scaler.scale_surface(sr.thumbnail_surface, surf, dims);
							thumbnail_image = ff.surface_to_image(surf, {}, encode_params, ui::image_format::Unknown);
							thumbnail_surface = surf;
						}
						else
						{
							auto surf = sr.thumbnail_surface;
							thumbnail_image = ff.surface_to_image(surf, {}, encode_params, ui::image_format::Unknown);
							thumbnail_surface = surf;
						}

						df::assert_true(thumbnail_image->data().size() < df::two_fifty_six_k);

						if (is_valid(thumbnail_image))
						{
							write.thumb = thumbnail_image;
						}
					}
					else if (is_valid(sr.thumbnail_image))
					{
						thumbnail_image = sr.thumbnail_image;

						if (is_valid(thumbnail_image))
						{
							const auto max_extent = setting.thumbnail_max_dimension;
							const auto thumb_extent = thumbnail_image->dimensions();

							if (max_extent.cx < thumb_extent.cx || max_extent.cy < thumb_extent.cy)
							{
								auto surf = ff.image_to_surface(thumbnail_image, max_extent);
								thumbnail_image = ff.surface_to_image(surf, {}, encode_params,
								                                      ui::image_format::Unknown);
								thumbnail_surface = surf;
							}

							df::assert_true(thumbnail_image->data().size() < df::two_fifty_six_k);

							if (is_valid(thumbnail_image))
							{
								write.thumb = thumbnail_image;
							}
						}
						else
						{
							// TODO - why
							// df::assert_true(false);
							thumbnail_image.reset();

							if (item)
							{
								item->failed_loading_thumbnail(true);
							}
						}
					}
					else if (load_thumb)
					{
						if (item)
						{
							item->failed_loading_thumbnail(true);
						}
					}

					if (sr.crc32c)
					{
						write.crc32c = sr.crc32c;
					}

					if (load_thumb && thumbnail_was_loaded)
					{
						write.thumb_scanned = now;
					}

					const auto item_has_no_thumb = item && !item->has_thumb();
					const auto scan_has_thumbnail = is_valid(thumbnail_surface) || is_valid(thumbnail_image);					

					if (found_file->metadata && metadata)
					{
						metadata->sidecars = found_file->metadata->sidecars;
						metadata->xmp = found_file->metadata->xmp;
					}

					found_file->metadata_scanned = now;
					found_file->metadata = metadata;
					found_file->crc32c = sr.crc32c;
					metadata->file_name = file_path.name();

					write.modified = found_file->file_modified;

					if (item)
					{
						if ((thumbnail_was_loaded || item_has_no_thumb) && is_valid(thumbnail_image))
						{
							df::assert_true(thumbnail_image->data().size() < df::two_fifty_six_k);
							item->thumbnail(thumbnail_image, thumbnail_was_loaded ? now : df::date_t::null);
							_async.invalidate_view(view_invalid::view_redraw);
						}
					}

					_db_writes.enqueue(std::move(write));

					if (metadata->coordinate.is_valid() &&
						prop::is_null(metadata->location_country) &&
						prop::is_null(metadata->location_state) &&
						prop::is_null(metadata->location_place))
					{
						if (folder->is_indexed)
							_async.queue_location(
								[this, folder, file_path, coord = metadata->coordinate](location_cache& locations)
								{
									save_location(
										file_path, locations.find_closest(coord.latitude(), coord.longitude()));

									if (folder->is_indexed)
									{
										_async.invalidate_view(view_invalid::index_summary);
									}
								});
					}

					if (folder->is_indexed)
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
					_db_writes.enqueue(std::move(write));

					if (load_thumb && item)
					{
						item->failed_loading_thumbnail(true);
					}
				}
			}
		}

		if (item)
		{
			item->update(file_path, file, folder->is_indexed);
		}
	}
}

void index_state::scan_item(const df::file_item_ptr& i, const bool load_thumb, const bool scan_if_offline)
{
	const auto folder = validate_folder(i->folder(), false, true, platform::now());
	scan_item(folder, i->path(), load_thumb, scan_if_offline, i, i->file_type());
}

bool index_state::needs_scan(const df::file_item_ptr& item) const
{
	const auto id = item->path();
	const auto found_folder = _items.find(id.folder());

	if (found_folder)
	{
		const auto found_file = find_file(found_folder->files, id.name());

		if (found_file != found_folder->files.end())
		{
			return needs_scan_impl(found_folder, *found_file, false, false, item);
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

void index_histograms::record(const location_cache& locations, const df::index_file_item& file)
{
	static auto year = platform::now().year();
	constexpr auto map_width = static_cast<int>(df::location_heat_map::map_width);
	constexpr auto map_height = static_cast<int>(df::location_heat_map::map_height);

	const auto md = file.metadata;
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
			_locations.coordinates[(map_loc.y * map_width) + map_loc.x] = 1;

			const auto country = locations.find_country(coord.latitude(), coord.longitude());
			const auto country_code = country.code;
			const auto found = _location_groups.find(country_code);

			if (found != _location_groups.end())
			{
				found->second.count += 1;
			}
			else
			{
				_location_groups[country_code] = {
					country.name, 1, df::location_heat_map::calc_map_loc(country.centroid)
				};
			}
		}
	}

	auto& file_type = _file_types.counts[file.ft->group->id];
	file_type.count += 1;
	file_type.size += file.size;

	const auto created_date_parts = created.date();
	const auto created_year_offset = year - created_date_parts.year;

	if (created_year_offset >= 0 && created_year_offset < 10)
	{
		_dates.dates[created_year_offset * 12 + created_date_parts.month - 1].created += 1;
	}

	const auto modified_date_parts = file.file_modified.date();
	const auto modified_date_parts_year_offset = year - modified_date_parts.year;

	if (modified_date_parts_year_offset >= 0 && modified_date_parts_year_offset < 10)
	{
		_dates.dates[modified_date_parts_year_offset * 12 + modified_date_parts.month - 1].modified += 1;
	}
}

inline bool index_state::is_collection_search(const df::search_t& search) const
{
	for (const auto& sel : search.selectors())
	{
		if (!is_indexed(sel.folder()))
		{
			return false;
		}
	}

	return true;
}

void index_state::calc_folder_summary(const df::index_folder_info_const_ptr& folder, df::file_group_histogram& result,
                                      df::cancel_token token) const
{
	for (const auto& sub_folder : folder->folders)
	{
		calc_folder_summary(sub_folder, result, token);
	}

	for (const auto& file : folder->files)
	{
		result.record(file);
	}
}

df::file_group_histogram index_state::calc_folder_summary(const df::folder_path path, df::cancel_token token) const
{
	df::file_group_histogram result;
	const auto folder = _items.find(path);

	if (folder)
	{
		for (const auto& sub_folder : folder->folders)
		{
			calc_folder_summary(sub_folder, result, token);
		}

		for (const auto& file : folder->files)
		{
			result.record(file);
		}
	}

	return result;
}

void index_state::save_media_position(const df::file_path id, const double media_position)
{
	item_db_write write;
	write.path = id;
	write.media_position = media_position;
	_db_writes.enqueue(std::move(write));
}

void index_state::save_crc(const df::file_path id, const uint32_t crc)
{
	_async.queue_async(async_queue::work, [this, id, crc]()
	{
		const auto f = _items.find(id.folder());

		if (f)
		{
			const auto found_file = find_file(f->files, id.name());

			if (found_file != f->files.end())
			{
				found_file->crc32c = crc;
				found_file->calc_bloom_bits();
				f->update_bloom_bits(*found_file);
			}
		}
	});

	item_db_write write;
	write.path = id;
	write.crc32c = crc;
	_db_writes.enqueue(std::move(write));
}

void index_state::save_location(const df::file_path id, const location_t& loc)
{
	_async.queue_async(async_queue::work, [this, id, loc]()
	{
		const auto found_folder = _items.find(id.folder());

		if (found_folder)
		{
			const auto found_file = find_file(found_folder->files, id.name());

			if (found_file != found_folder->files.end())
			{
				auto md = found_file->safe_ps();

				if (!md->coordinate.is_valid())
				{
					md->coordinate = loc.position;
				}

				md->location_place = loc.place;
				md->location_state = loc.state;
				md->location_country = loc.country;

				item_db_write write;
				write.path = id;
				write.md = md;
				write.metadata_scanned = found_file->metadata_scanned;
				_db_writes.enqueue(std::move(write));
			}
		}
	});
}

void index_state::save_thumbnail(const df::file_path id, const ui::const_image_ptr& thumbnail_image, const df::date_t scan_timestamp)
{
	item_db_write write;
	write.path = id;

	if (is_valid(thumbnail_image))
	{
		write.thumb = thumbnail_image;
	}

	write.thumb_scanned = scan_timestamp;
	_db_writes.enqueue(std::move(write));
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
	platform::exclusive_lock lock(_summary_rw);
	_summary._roots = std::move(roots);
}

void index_state::index_folders(df::cancel_token token)
{
	df::scope_locked_inc l(detecting);
	df::index_roots roots;

	{
		platform::exclusive_lock lock(_summary_rw);
		roots = _summary._roots;
	}

	const auto now = platform::now();
	std::vector<df::folder_path> folders(roots.folders.begin(), roots.folders.end());
	df::unique_folders unique_folder_paths(folders.cbegin(), folders.cend());

	index_histograms histograms;
	items_by_folder_t indexed;
	int count = 0;
	stats.index_folder_count = 0;

	for (const auto& f : _items.all_folders())
	{
		f.second->is_indexed = false;
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

		if (!roots.excludes.contains(folder_path))
		{
			const auto group_node = validate_folder(folder_path, true, true, now);
			group_node->is_indexed = true;

			for (const auto& file : group_node->files)
			{
				histograms.record(_locations, file);

				if (file.ft->is_media())
				{
					count += 1;
				}
			}

			for (const auto& sub_folder : group_node->folders)
			{
				const auto sub_folder_path = folder_path.combine(sub_folder->name);
				const auto is_excluded = roots.excludes.contains(sub_folder_path);

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

		{
			platform::exclusive_lock lock(_summary_rw);
			_summary._histograms._file_types = histograms._file_types;
			_summary._histograms._dates = histograms._dates;
			_async.invalidate_view(view_invalid::sidebar_file_types_and_dates | view_invalid::tooltip);
		}
	}

	if (!token.is_cancelled())
	{
		stats.index_item_count = stats.media_item_count = count;

		_async.queue_database([cached_items = all_indexed_items()](database& db)
		{
			db.clean(cached_items);
		});
	}

	{
		platform::exclusive_lock lock(_summary_rw);
		_summary._distinct_folders = std::move(unique_folder_paths);
		stats.index_folder_count = _summary._distinct_folders.size();
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

	{
		platform::exclusive_lock lock(_summary_rw);
		_summary._distinct_prime_folders = std::move(distinct_prime_folders);
		_summary._histograms = std::move(histograms);
	}

	_folders_indexed = true;
}


static void scan_trim(std::u8string& s)
{
	const auto pred = [](int ch)
	{
		return !std::iswspace(ch) && ch != '-';
	};

	s.erase(s.begin(), std::ranges::find_if(s, pred));
	s.erase(std::find_if(s.rbegin(), s.rend(), pred).base(), s.end());
}

std::u8string build_result_string(const std::vector<std::string>& tokens,
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

	return std::u8string(str::utf8_cast(result));
}

media_name_props scan_info_from_title(const std::u8string_view name8)
{
	media_name_props result;

	static const df::hash_set<std::string, df::ihash, df::ieq> stop_words
	{
		"480p",
		"720p",
		"720",
		"1080p",
		"1080",
		"hdtv",
		"x264",
		"x265",
		"h264",
		"h265",
		"ac3",
		"dts",
		"aac",
		"brrip",
		"bdrip",
		"bluray",
		"hdrip",
		"hdtv",
		"dvdrip",
		"webrip",
		"xvid",
		"extended",
		"5.1",
		"7.1"
	};

	static const df::hash_set<std::string, df::ihash, df::ieq> pre_title_stop_words
	{
		"internal",
		"web",
		"(",
		"[",
		"-",
		"PDTV",
		"DVDScr",
		"10bit",
		"UNRATED",
	};

	static const df::hash_set<std::string, df::ihash, df::ieq> pre_show_stop_words
	{
		"-",
	};

	static const auto split_rx = std::regex{R"([\s]+|\-|\.|\(|\[|\]|\))", std::regex_constants::icase};

	const auto name = std::string_view(std::bit_cast<const char*>(name8.data()), name8.size());

	auto tokens = std::vector<std::string>(
		std::regex_token_iterator<std::string_view::const_iterator>{name.begin(), name.end(), split_rx, {-1, 0}},
		std::regex_token_iterator<std::string_view::const_iterator>{}
	);

	static const auto space_rx = std::regex{R"([\s]+|\.)", std::regex_constants::icase};

	std::erase_if(tokens, [](auto&& i) { return i.empty() || std::regex_match(i, space_rx); });

	static std::regex episode_rx("s([0-9]+)e([0-9]+)", std::regex_constants::icase);
	static std::regex episode_of_rx("([0-9]+)of([0-9]+)", std::regex_constants::icase);
	static std::regex episode_x_rx("([0-9]+)x([0-9]+)", std::regex_constants::icase);
	static std::regex year_rx("(19|20)[0-9][0-9]", std::regex_constants::icase);

	auto found_episode_num = std::ranges::find_if(tokens, [](auto&& i)
	{
		return std::regex_match(i, episode_rx) || std::regex_match(i, episode_of_rx) || std::regex_match(
			i, episode_x_rx);
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
				result.year = std::stoi(*end_show);
				--end_show;
			}
		}
	}

	auto end_title = std::ranges::find_if(tokens, [](auto&& i)
	{
		return stop_words.contains(i);
	});

	/*for (auto t : tokens)
	{
		std::cout << "'" << t << "'" << std::endl;
	}*/

	while (end_title != tokens.begin() &&
		end_title != tokens.end() &&
		pre_title_stop_words.contains(*(end_title - 1)))
	{
		--end_title;
	}

	if ((end_title != tokens.end() && end_title != tokens.begin()) ||
		found_episode_num != tokens.end() && found_episode_num != tokens.begin())
	{
		if (found_episode_num != tokens.end())
		{
			result.show = build_result_string(tokens, tokens.begin(), end_show);

			std::smatch match;
			auto i = found_episode_num;

			if ((std::regex_search(*i, match, episode_rx) && match.size() == 3))
			{
				result.season = std::stoi(match[1]);
				result.episode = std::stoi(match[2]);
			}
			else if ((std::regex_search(*i, match, episode_of_rx) && match.size() == 3))
			{
				result.episode = std::stoi(match[1]);
				result.episode_of = std::stoi(match[2]);
			}
			else if ((std::regex_search(*i, match, episode_x_rx) && match.size() == 3))
			{
				result.season = std::stoi(match[1]);
				result.episode = std::stoi(match[2]);
			}

			++i;

			result.title = build_result_string(tokens, i, end_title);
		}
		else if (end_title != tokens.end())
		{
			// ok it does have movie info
			auto i_year = end_title;
			--i_year;

			if (std::regex_match(*i_year, year_rx))
			{
				result.year = std::stoi(*i_year);
				--end_title;
			}

			result.title = build_result_string(tokens, tokens.begin(), end_title);
		}
	}

	scan_trim(result.title);
	scan_trim(result.show);

	return result;
}

static void items_possible_hashes_contains(df::hash_map<df::file_item_ptr, item_presence>& item_presence,
                                           const std::vector<std::pair<unsigned, df::file_item_ptr>>& possible,
                                           const df::index_file_item& indexed_file, const uint32_t hash)
{
	auto lb = std::lower_bound(possible.begin(), possible.end(), hash, [](auto&& l, auto&& r) { return l.first < r; });

	while (lb != possible.end() && lb->first == hash)
	{
		const auto i = lb->second;

		if (is_dup_match(indexed_file, i))
		{
			i->duplicates(indexed_file.duplicates);

			if (indexed_file.file_modified == i->file_modified() ||
				(indexed_file.crc32c != 0 && indexed_file.crc32c == i->crc32c()))
			{
				if (item_presence[i] != item_presence::newer_in)
				{
					item_presence[i] = item_presence::similar_in;
				}
			}
			else if (indexed_file.file_modified < i->file_modified())
			{
				if (item_presence[i] == item_presence::unknown)
				{
					item_presence[i] = item_presence::older_in;
				}
			}
			else if (indexed_file.file_modified > i->file_modified())
			{
				item_presence[i] = item_presence::newer_in;
			}
		}

		++lb;
	}
}

void index_state::update_presence(const df::item_set& items)
{
	{
		df::measure_ms ms(stats.update_presence_ms);
		df::file_items_by_folder items_by_folder;

		for (const auto& i : items.items())
		{
			items_by_folder[i->folder()].emplace_back(i);
		}

		for (const auto& ff : items_by_folder)
		{
			const auto folder = _items.find(ff.first);

			if (folder)
			{
				for (const auto& i : ff.second)
				{
					const auto file = find_file(folder->files, i->path().name());

					if (file != folder->files.end())
					{
						i->update(i->path(), *file, folder->is_indexed);
					}
				}
			}
		}

		std::vector<std::pair<uint32_t, df::file_item_ptr>> items_possible_hashes;
		df::hash_map<df::file_item_ptr, item_presence> item_presence;

		for (const auto& i : items.items())
		{
			const auto is_indexed = i->is_indexed_location();

			if (is_indexed)
			{
				item_presence[i] = item_presence::this_in;
			}
			else
			{
				if (i->crc32c())
				{
					items_possible_hashes.emplace_back(i->crc32c(), i);
				}

				const auto md = i->metadata();

				if (md)
				{
					const auto created_date = md->created();

					if (created_date.is_valid())
					{
						items_possible_hashes.emplace_back(x64to32(created_date.to_int64()), i);
					}
				}
				else
				{
					auto d = i->file_created().system_to_local();
					items_possible_hashes.emplace_back(x64to32(d.to_int64()), i);
				}

				items_possible_hashes.emplace_back(crypto::fnv1a_i(i->path().name()), i);
				item_presence[i] = item_presence::unknown;
			}
		}

		if (!items_possible_hashes.empty())
		{
			std::ranges::sort(items_possible_hashes, [](auto&& left, auto&& right)
			{
				return left.first < right.first;
			});
			const auto folders = _items.all_folders();

			for (const auto& ifn : folders)
			{
				if (ifn.second->is_indexed)
				{
					for (const auto& file : ifn.second->files)
					{
						if (file.crc32c)
						{
							items_possible_hashes_contains(item_presence, items_possible_hashes, file, file.crc32c);
						}

						const auto created_date = file.created();

						if (created_date.is_valid())
						{
							items_possible_hashes_contains(item_presence, items_possible_hashes, file,
							                               x64to32(created_date.to_int64()));
						}

						items_possible_hashes_contains(item_presence, items_possible_hashes, file, crypto::fnv1a_i(file.name));
					}
				}
			}
		}

		for (const auto& i : item_presence)
		{
			const auto pres = i.second;

			if (pres == item_presence::unknown)
			{
				i.first->presence(item_presence::not_in);
				i.first->duplicates({0, 0});
			}
			else
			{
				i.first->presence(pres);
			}
		}
	}

	df::trace(str::format(u8"Index update presence {} items in {} ms", items.size(), stats.update_presence_ms));
}

void index_state::scan_items(const df::item_set& items_to_scan,
                             const bool load_thumbs,
                             const bool refresh_from_file_system,
                             const bool only_if_needed,
                             const bool scan_if_offline,
                             df::cancel_token token)
{
	{
		df::measure_ms ms(stats.scan_items_ms);
		df::scope_locked_inc l(scanning_items);
		df::file_items_by_folder items_by_folder;

		for (const auto& i : items_to_scan.items())
		{
			items_by_folder[i->folder()].emplace_back(i);
		}

		const auto now = platform::now();

		for (const auto& ff : items_by_folder)
		{
			if (token.is_cancelled()) break;

			const auto folder = validate_folder(ff.first, false, refresh_from_file_system, now);

			for (const auto& i : ff.second)
			{
				if (token.is_cancelled()) break;

				if (!only_if_needed || i->should_load_thumbnail())
				{
					scan_item(folder, i->path(), load_thumbs, scan_if_offline, i, i->file_type());
				}
			}
		}

		for (const auto& folder : items_to_scan.folders())
		{
			if (token.is_cancelled()) break;

			folder->info(validate_folder(folder->path(), false, refresh_from_file_system, now));
			folder->calc_folder_summary(token);
		}
	}

	df::trace(str::format(u8"Index scan {} items (thumbs={} refresh-fs={}) in {} ms", items_to_scan.size(), load_thumbs,
	                      refresh_from_file_system, stats.scan_items_ms));
}

void index_state::scan_folder(const df::folder_path folder_path, const df::index_folder_item_ptr& folder)
{
	for (const auto& f : folder->files)
	{
		scan_item(folder, {folder_path, f.name}, false, false, nullptr, f.ft);
	}

	for (const auto& f : folder->folders)
	{
		queue_scan_folder(folder_path.combine(f->name));
	}
}

void index_state::scan_folder(const df::folder_path folder_path, const bool mark_is_indexed, const df::date_t timestamp)
{
	df::scope_locked_inc l(scanning_items);
	const auto folder = validate_folder(folder_path, mark_is_indexed, true, timestamp);
	scan_folder(folder_path, folder);
}

void index_state::queue_scan_listed_items(df::item_set listed_items)
{
	static std::atomic_int version;
	df::cancel_token token(version);

	_async.queue_async(async_queue::scan_folder, [this, listed_items = std::move(listed_items), token]()
	{
		scan_items(listed_items, false, false, false, false, token);
	});
}

void index_state::queue_scan_modified_items(df::item_set items_to_scan)
{
	_async.queue_async(async_queue::scan_modified_items, [this, items_to_scan = std::move(items_to_scan)]()
	{
		const auto start_ms = df::now_ms();
		scan_items(items_to_scan, true, true, false, false, {});
		_async.invalidate_view(view_invalid::index_summary | view_invalid::media_elements);
		df::trace(str::format(u8"Index scan modified {} items in {} ms", items_to_scan.size(),
		                      df::now_ms() - start_ms));
	});
}

void index_state::queue_scan_displayed_items(df::item_set visible)
{
	static std::atomic_int version;
	df::cancel_token token(version);

	_async.queue_async(async_queue::scan_displayed_items, [this, visible = std::move(visible), token]()
	{
		df::scope_locked_inc thumbnailing(thumbnailing_items);

		if (!visible.empty())
		{
			scan_items(visible, true, false, true, false, token);
			_async.invalidate_view(view_invalid::view_layout | view_invalid::group_layout);
		}
	});
}

void index_state::queue_update_presence(df::item_set items)
{
	if (!items.empty())
	{
		_async.queue_async(async_queue::index_presence_single, [this, items = std::move(items)]()
		{
			const auto start_ms = df::now_ms();
			update_presence(items);
			_async.invalidate_view(view_invalid::view_layout | view_invalid::group_layout);
			df::trace(str::format(u8"Index update presence in {} ms", df::now_ms() - start_ms));
		});
	}
}

void index_state::queue_update_predictions()
{
	_async.queue_async(async_queue::index_predictions_single, [this]()
	{
		update_predictions();
		_async.invalidate_view(view_invalid::sidebar);
	});
}

void index_state::queue_update_summary()
{
	_async.queue_async(async_queue::index_summary_single, [this]()
	{
		update_summary();
		_async.invalidate_view(view_invalid::sidebar);
		std::this_thread::sleep_for(333ms);
	});
}

std::vector<str::cached> index_state::distinct_genres() const
{
	platform::shared_lock lock(_summary_rw);
	const auto found = _summary._distinct_text.find(prop::genre);
	return found != _summary._distinct_text.end()
		       ? std::vector<str::cached>{found->second.begin(), found->second.end()}
		       : std::vector<str::cached>{};
}

std::vector<std::u8string> index_state::auto_complete_text(const prop::key_ref key)
{
	platform::shared_lock lock(_summary_rw);
	const auto found = _summary._distinct_text.find(key);
	return found != _summary._distinct_text.end()
		       ? std::vector<std::u8string>{found->second.begin(), found->second.end()}
		       : std::vector<std::u8string>{};
}

void index_state::queue_scan_folder(const df::folder_path path)
{
	_async.queue_async(async_queue::scan_folder, [this, path]()
	{
		const auto now = platform::now();
		scan_folder(path, is_indexed(path), now);
	});
}

void index_state::queue_scan_folders(df::unique_folders paths)
{
	_async.queue_async(async_queue::scan_folder, [this, paths = std::move(paths)]()
	{
		const auto now = platform::now();

		for (const auto& path : paths)
		{
			scan_folder(path, is_indexed(path), now);
		}

		_async.invalidate_view(view_invalid::view_layout);
	});
}

static void count_distinct_values(unique_key_vals& vals, const prop::item_metadata_ptr& md)
{
	if (!prop::is_null(md->album)) ++vals[{prop::album.name, md->album}];
	if (!prop::is_null(md->album_artist)) ++vals[{prop::album_artist.name, md->album_artist}];
	if (!prop::is_null(md->artist)) ++vals[{prop::artist.name, md->artist}];
	if (!prop::is_null(md->audio_codec)) ++vals[{prop::audio_codec.name, md->audio_codec}];
	if (!prop::is_null(md->bitrate)) ++vals[{prop::bitrate.name, md->bitrate}];
	if (!prop::is_null(md->camera_manufacturer)) ++vals[{prop::camera_manufacturer.name, md->camera_manufacturer}];
	if (!prop::is_null(md->camera_model)) ++vals[{prop::camera_model.name, md->camera_model}];
	if (!prop::is_null(md->comment)) ++vals[{prop::comment.name, md->comment}];
	if (!prop::is_null(md->composer)) ++vals[{prop::composer.name, md->composer}];
	if (!prop::is_null(md->copyright_creator)) ++vals[{prop::copyright_creator.name, md->copyright_creator}];
	if (!prop::is_null(md->copyright_credit)) ++vals[{prop::copyright_credit.name, md->copyright_credit}];
	if (!prop::is_null(md->copyright_notice)) ++vals[{prop::copyright_notice.name, md->copyright_notice}];
	if (!prop::is_null(md->copyright_source)) ++vals[{prop::copyright_source.name, md->copyright_source}];
	if (!prop::is_null(md->copyright_url)) ++vals[{prop::copyright_url.name, md->copyright_url}];
	if (!prop::is_null(md->description)) ++vals[{prop::description.name, md->description}];
	if (!prop::is_null(md->encoder)) ++vals[{prop::encoder.name, md->encoder}];
	if (!prop::is_null(md->file_name)) ++vals[{prop::file_name.name, md->file_name}];
	if (!prop::is_null(md->genre)) ++vals[{prop::genre.name, md->genre}];
	if (!prop::is_null(md->lens)) ++vals[{prop::lens.name, md->lens}];
	if (!prop::is_null(md->location_place)) ++vals[{prop::location_place.name, md->location_place}];
	if (!prop::is_null(md->location_country)) ++vals[{prop::location_country.name, md->location_country}];
	if (!prop::is_null(md->location_state)) ++vals[{prop::location_state.name, md->location_state}];
	if (!prop::is_null(md->performer)) ++vals[{prop::performer.name, md->performer}];
	if (!prop::is_null(md->pixel_format)) ++vals[{prop::pixel_format.name, md->pixel_format}];
	if (!prop::is_null(md->publisher)) ++vals[{prop::publisher.name, md->publisher}];
	if (!prop::is_null(md->show)) ++vals[{prop::show.name, md->show}];
	if (!prop::is_null(md->synopsis)) ++vals[{prop::synopsis.name, md->synopsis}];
	if (!prop::is_null(md->title)) ++vals[{prop::title.name, md->title}];
	if (!prop::is_null(md->video_codec)) ++vals[{prop::video_codec.name, md->video_codec}];
	if (!prop::is_null(md->game)) ++vals[{prop::game.name, md->game}];
	if (!prop::is_null(md->label)) ++vals[{prop::label.name, md->label}];
	if (!prop::is_null(md->system)) ++vals[{prop::system.name, md->system}];
	if (!prop::is_null(md->raw_file_name)) ++vals[{prop::raw_file_name.name, md->raw_file_name}];
	if (!prop::is_null(md->tags)) ++vals[{prop::tag.name, md->tags}];

	/*if (!prop::is_null(md->exposure_time)) ++vals[{prop::exposure_time.name, md->exposure_time}];
	if (!prop::is_null(md->f_number)) ++vals[{prop::f_number.name, md->f_number}];
	if (!prop::is_null(md->focal_length)) ++vals[{prop::focal_length.name, md->focal_length}];
	if (!prop::is_null(md->created_digitized)) ++vals[{prop::created_digitized.name, md->created_digitized}];
	if (!prop::is_null(md->created_exif)) ++vals[{prop::created_exif.name, md->created_exif}];
	if (!prop::is_null(md->created_utc)) ++vals[{prop::created_utc.name, md->created_utc}];
	if (!prop::is_null(md->duration)) ++vals[{prop::duration.name, md->duration}];
	if (!prop::is_null(md->focal_length_35mm_equivalent)) ++vals[{prop::focal_length_35mm_equivalent.name, md->focal_length_35mm_equivalent}];
	if (!prop::is_null(md->iso_speed)) ++vals[{prop::iso_speed.name, md->iso_speed}];
	if (!prop::is_null(md->rating)) ++vals[{prop::rating.name, md->rating}];
	if (!prop::is_null(md->audio_channels)) ++vals[{prop::audio_channels.name, md->audio_channels}];
	if (!prop::is_null(md->audio_sample_rate)) ++vals[{prop::audio_sample_rate.name, md->audio_sample_rate}];
	if (!prop::is_null(md->audio_sample_type)) ++vals[{prop::audio_sample_type.name, md->audio_sample_type}];
	if (!prop::is_null(md->year)) ++vals[{prop::year.name, md->year}];
	if (!prop::is_null(md->season)) ++vals[{prop::season.name, md->season}];
	if (!prop::is_null(md->disk)) ++vals[{prop::disk_num.name, md->disk}];
	if (!prop::is_null(md->episode)) ++vals[{prop::episode.name, md->episode}];
	if (!prop::is_null(md->track)) ++vals[{prop::track_num.name, md->track}];*/

	//if (!prop::is_null(md->orientation)) ++vals[{prop::orientation.name, md->orientation}];
	//if (!prop::is_null(md->coordinate)) ++vals[{prop::coordinate.name, md->coordinate}];
	//if (!prop::is_null(md->height)) ++vals[{prop::height.name, md->height}];
	//if (!prop::is_null(md->width)) ++vals[{prop::width.name, md->width}];
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
				++file_first;
				++old_first;
			}
		}
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
			file_node.metadata_scanned = i->metadata_scanned;

			file_node.calc_bloom_bits();
			++node;
		}

		df::assert_true(std::is_sorted(files.begin(), files.end()));

		folder_node = std::make_shared<df::index_folder_item>(files);
		folder_node->name = folder_path.name();
		folder_node->reset_bloom_bits();
		_items.replace(folder_path, folder_node);
	}

	_cache_items_loaded = true;
}

static df::count_and_size sum_items(const df::index_folder_info_const_ptr& folder)
{
	df::count_and_size result;

	for (const auto& sub_folder : folder->folders)
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
		uint32_t count = 0;
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

std::vector<index_state::auto_complete_word> index_state::auto_complete_words(
	const std::u8string_view query, const size_t max_results)
{
	df::assert_true(!ui::is_ui_thread());

	std::vector<auto_complete_word> result;
	platform::shared_lock lock(_summary_rw);

	if (query.size() > 2)
	{
		for (const auto& word : _summary._distinct_words)
		{
			const auto found = str::ifind2(word.first, query, 0);

			if (found.found)
			{
				result.emplace_back(std::u8string(word.first), found.parts);
				if (result.size() > max_results) break;
			}
		}
	}
	else
	{
		for (const auto& word : _summary._distinct_words)
		{
			if (str::starts(word.first, query))
			{
				result.emplace_back(std::u8string(word.first), std::vector<str::part_t>{{0, query.size()}});
				if (result.size() > max_results) break;
			}
		}
	}

	return result;
}

std::vector<index_state::auto_complete_folder> index_state::auto_complete_folders(
	const std::u8string_view query, const size_t max_results) const
{
	df::assert_true(!ui::is_ui_thread());

	std::vector<auto_complete_folder> result;

	for (const auto& folders : {
		     _summary._distinct_prime_folders, _summary._distinct_folders, _summary._distinct_other_folders
	     })
	{
		if (query.size() > 2)
		{
			for (const auto& folder : folders)
			{
				auto name_pos = folder.is_root() ? 0u : (folder.find_last_slash() + 1);
				if (name_pos == std::u8string_view::npos) name_pos = 0;
				auto found = str::ifind2(folder.text().substr(name_pos), query, name_pos);

				if (!found.found && name_pos != 0)
				{
					found = ifind2(folder.text(), query, 0);
					name_pos = 0;
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
			for (const auto& folder : folders)
			{
				const auto name_pos = folder.is_root() ? 0u : (folder.find_last_slash() + 1);

				if (name_pos != std::u8string_view::npos && str::starts(folder.text().substr(name_pos), query))
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
