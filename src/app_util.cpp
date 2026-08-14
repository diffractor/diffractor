// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Utility functions for file operations including import, sync, and batch
// processing. Handles file renaming, metadata updates, and folder synchronization.

#include "pch.h"
#include "util_strings.h"
#include "model.h"
#include "model_db.h"
#include "app_command_status.h"
#include "app_command_line.h"
#include "app_util.h"
#include "model_index.h"
#include "util_crash_files_db.h"

// Process-level state, declared in app_command_line.h and app.h. It is used across the model and
// file layers, so it does not belong to the application frame that used to define it.

command_line_t command_line;

crash_files_db& crash_files()
{
	static crash_files_db instance(df::probe_data_file("diffractor-files-that-crash.txt"),
	                               crash_files_db::release_tag(s_app_version));
	return instance;
}

void flush_open_files_to_crash_files_list()
{
	crash_files().flush_open_files();
}

void log_open_files_to_crash_files_list()
{
	crash_files().log_open_files();
}

void view_state::modify_items(const df::results_ptr& results, icon_index icon, const std::string_view title,
                              const df::item_elements& items_to_modify, const metadata_edits& edits,
                              const view_host_base_ptr& view)
{
	batch_edit_spec spec;
	spec.make_edits = [edits](df::file_path) { return item_edits{edits, {}}; };
	spec.process_type = df::process_items_type::can_save_metadata;
	spec.encode_params.jpeg_save_quality = setting.jpeg_save_quality;
	// This overload never applies photo edits, so orientation is the only thing that can change what
	// is drawn.
	spec.changes_presentation = edits.changes_presentation();

	modify_items(results, items_to_modify, spec, view);
}

void view_state::modify_items(const df::results_ptr& results, const df::item_elements& items_to_modify,
                              const batch_edit_spec& spec, const view_host_base_ptr& view)
{
	clear_error_items(view);
	const auto can_process = df::item_set(items_to_modify).can_process(spec.process_type, true, view);

	if (can_process.success())
	{
		// Handles must stay detached for the whole write, and the destructor touches UI-owned
		// state (display, player, selection), so the last reference is handed to the closing UI
		// callback. A stack object would reopen the files while the background write is running
		// whenever the caller's status does not block (view_command_status::wait_for_complete).
		const auto detach = std::make_shared<detach_file_handles>(*this);

		struct modify_request
		{
			index_state::item_scan_request scan;
			str::cached name;
			df::file_path path;
			str::cached xmp;
		};

		std::vector<modify_request> requests;
		requests.reserve(items_to_modify.size());
		std::vector<df::file_path> claimed;
		claimed.reserve(items_to_modify.size());
		for (const auto& item : items_to_modify)
		{
			// A write that cannot change what is drawn leaves the existing thumbnail correct, so the
			// post-write rescan must not regenerate it. Re-decoding one is slow on large video and
			// republishing it makes the tile visibly blank and reappear.
			const auto load_thumbnail = spec.changes_presentation || !item->has_thumb();
			requests.emplace_back(item_index.make_scan_request(item, load_thumbnail, false), item->name(),
			                      item->path(), item->xmp());
			claimed.emplace_back(item->path());
		}

		// Declared here, on the UI thread and before the write starts, so the modified time it
		// produces does not send the thumbnail and the displayed image back through a reload that
		// would return the same result.
		if (!spec.changes_presentation)
		{
			for (const auto& item : items_to_modify)
			{
				item->retain_thumbnail_across_next_write();
			}

			if (const auto d = display_state())
			{
				const auto is_modified = [&items_to_modify](const df::item_element_ptr& i)
				{
					if (!i) return false;
					for (const auto& item : items_to_modify) if (item == i) return true;
					return false;
				};

				if (d->_selected_texture1 && is_modified(d->_item1)) d->_selected_texture1->mark_visuals_current();
				if (d->_selected_texture2 && is_modified(d->_item2)) d->_selected_texture2->mark_visuals_current();
			}
		}

		// No read of these files may be queued while the write runs; anything asked for meanwhile is
		// deferred and re-requested on release.
		item_index.claim_for_write(claimed);

		queue_async(async_queue::work,
		            [this, requests = std::move(requests), claimed = std::move(claimed), spec, results, detach,
			            displayed_paths = spec.changes_presentation
				                              ? displayed_photo_paths()
				                              : std::vector<df::file_path>{},
			            detached_av_path = detached_display_av_path()]() mutable
		            {
			            result_scope rr(results);
			            std::string message;
			            files ff;

			            // Only the UI hop at the end releases the claim, and drain_queue swallows an
			            // exception before it is ever posted - which parks these items with no thumbnail and
			            // no metadata refresh for the rest of the session. The release is therefore owned by
			            // a guard rather than by the success path. The guard keeps its own copy of the paths
			            // because the success path moves the originals into the UI callback.
			            auto claim_handed_over = false;
			            df::scope_exit release_claim([this, &claim_handed_over, claimed]
			            {
				            if (!claim_handed_over)
				            {
					            _async.queue_ui([this, claimed] { item_index.release_write_claim(claimed); });
				            }
			            });

			            // Items the write already re-scanned for us vs. those whose scan failed and still need a
			            // forced background rescan.
			            std::vector<index_state::item_scan_request> immediate_done;
			            std::vector<index_state::item_scan_request> needs_force;
			            // The written bytes for each item on screen, handed over instead of read back. The modified
			            // time travels with them so the display stamps in the file clock domain.
			            struct written_image
			            {
				            df::file_path path;
				            file_load_result loaded;
				            df::date_t modified;
			            };
			            std::vector<written_image> written_images;
			            // The open handle over the playable item the display is about to reopen.
			            platform::file_ptr detached_av_handle;

			            for (const auto& request : requests)
			            {
				            results->start_item(request.name);

				            const auto edits = spec.make_edits(request.path);
				            file_update_result update_result;
				            const auto is_displayed = std::ranges::find(displayed_paths, request.path) !=
					            displayed_paths.end();

				            if (edits)
				            {
					            update_result = ff.update(request.path, request.path, edits->metadata, edits->image,
					                                      spec.encode_params, false, request.xmp, request.xmp,
					                                      index_state::make_rescan_spec(request.scan, request.xmp,
						                                      is_displayed,
						                                      request.path == detached_av_path));
				            }

				            results->end_item(request.name, to_status(update_result.code));

				            if (!update_result.success())
				            {
					            message = update_result.format_error();
					            break;
				            }

				            if (request.path == detached_av_path) detached_av_handle = std::move(
					            update_result.display_handle);

				            if (is_displayed)
				            {
					            written_images.emplace_back(request.path, std::move(update_result.loaded),
					                                        df::date_t(update_result.modified));
				            }

				            if (item_index.apply_write_scan(request.scan, update_result))
				            {
					            needs_force.emplace_back(request.scan);
				            }
				            else
				            {
					            immediate_done.emplace_back(request.scan);
				            }

				            if (results->is_canceled())
					            break;
			            }

			            // Background rescan: force the items whose immediate scan failed (the mtime-tie heuristic
			            // can miss a rapid successive edit whose sidecar modified time ties with the previous scan
			            // timestamp); the scanned items are already current, so queue them non-forced as a cheap
			            // no-op safety net. The detach reference is handed over here so the handles stay
			            // detached until the writes finish and the destructor runs on the UI thread.
			            _async.queue_ui(
				            [this, detach = std::move(detach), claimed = std::move(claimed), immediate_done =
					            std::move(immediate_done), needs_force = std::move(needs_force), written_images =
					            std::move(written_images), detached_av_path, detached_av_handle = std::move(
						            detached_av_handle)]() mutable
				            {
					            item_index.release_write_claim(claimed);

					            for (auto& written : written_images)
					            {
						            publish_written_image(written.path, std::move(written.loaded), written.modified);
					            }

					            // Before the guard is released below, because releasing it is what reopens the session.
					            publish_written_handle(detached_av_path, std::move(detached_av_handle));

					            df::item_set immediate_items;
					            df::item_set force_items;
					            for (const auto& request : immediate_done)
					            {
						            const auto item = request.lifetime.lock();
						            if (item && item->path() == request.path) immediate_items.add(item);
					            }
					            for (const auto& request : needs_force)
					            {
						            const auto item = request.lifetime.lock();
						            if (item && item->path() == request.path) force_items.add(item);
					            }
					            if (!force_items.empty()) item_index.queue_scan_modified_items(
						            std::move(force_items), true);
					            if (!immediate_items.empty()) item_index.queue_scan_modified_items(
						            std::move(immediate_items), false);
				            });

			            claim_handed_over = true;

			            rr.complete(message);
		            });

		results->wait_for_complete();
	}
	else
	{
		results->show_message(can_process.to_string());
	}
}


static platform::file_op_result move_or_copy(const df::file_path source_path, const df::file_path dest_path,
                                             const bool is_move, const bool fail_if_exists)
{
	return is_move
		       ? platform::move_file(source_path, dest_path, fail_if_exists)
		       : platform::copy_file(source_path, dest_path, fail_if_exists, false);
}

static df::file_path rename_destination(const rename_source& item, const std::string_view name)
{
	const auto source = item.source;
	return item.is_folder
		       ? df::file_path(source.folder().parent(), name)
		       : df::file_path(source.folder(), name, source.extension());
}

bool rename_path_exists(const df::file_path path, const bool is_folder)
{
	return is_folder ? platform::exists(path.folder().combine(path.name())) : path.exists();
}

std::string rename_path_key(const df::file_path path, const bool is_folder)
{
	return is_folder ? std::string(path.folder().combine(path.name()).text()) : path.pack();
}

std::vector<rename_source> snapshot_rename_sources(const df::item_set& items)
{
	std::vector<rename_source> result;
	result.reserve(items.size());
	for (const auto& item : items.items())
	{
		rename_source source;
		source.source = item->path();
		source.original_name = item->base_name();
		source.metadata = item->metadata();
		source.media_created = item->calc_media_created();
		source.is_folder = item->is_folder();
		if (!source.is_folder)
			for (const auto& sidecar_name : split(item->sidecars(), true))
				source.sidecars.emplace_back(source.source.folder().combine_file(sidecar_name));
		result.emplace_back(std::move(source));
	}
	return result;
}

std::vector<rename_item> calc_item_renames(const std::vector<rename_source>& items,
                                           const std::string_view template_name,
                                           const int start, const collision_policy policy)
{
	std::vector<rename_item> results;
	results.reserve(items.size());
	std::vector<char> name_is_usable;
	name_is_usable.reserve(items.size());

	// Every path this plan moves away from. A destination held by one of them is free by the time
	// the run reaches it, so renaming a set onto the names it is vacating is not a collision.
	std::set<std::string, df::iless> vacated;
	auto seq = start;

	const auto assign_sidecars = [](rename_item& rename, const rename_source& i, const df::file_path primary)
	{
		rename.sidecars.clear();
		rename.sidecar_destinations_exist.clear();
		if (i.is_folder) return;

		for (const auto sidecar_source : i.sidecars)
		{
			const auto sidecar_destination =
				primary.folder().combine_file(primary.file_name_without_extension()).extension(
					sidecar_source.extension());
			rename.sidecar_destinations_exist.emplace_back(sidecar_destination.exists());
			rename.sidecars.emplace_back(sidecar_source, sidecar_destination);
		}
	};

	for (const auto& i : items)
	{
		const auto& md = i.metadata;
		const auto& original_name = i.original_name;
		auto name = format_sequence(original_name, template_name, seq);
		name = prop::replace_tokens(name, md, original_name, i.media_created);

		rename_item rename;
		rename.source = i.source;
		rename.is_folder = i.is_folder;
		rename.policy = policy;
		rename.original_name = original_name;
		rename.new_name = name;
		rename.destination = rename_destination(i, name);

		const auto source_key = rename_path_key(rename.source, rename.is_folder);
		const auto destination_key = rename_path_key(rename.destination, rename.is_folder);
		rename.noop = source_key == destination_key;
		assign_sidecars(rename, i, rename.destination);

		// A case-only rename does not free the path it renames within, so it vacates nothing.
		if (str::icmp(source_key, destination_key) != 0)
		{
			vacated.emplace(source_key);

			for (const auto& [sidecar_source, sidecar_destination] : rename.sidecars)
				if (sidecar_source.icmp(sidecar_destination) != 0) vacated.emplace(sidecar_source.pack());
		}

		name_is_usable.emplace_back(
			platform::is_valid_file_name(name) && rename.destination.is_save_valid() ? 1 : 0);
		seq += 1;
		results.emplace_back(std::move(rename));
	}

	// Resolve the collision with the one named policy before validity is decided.
	for (auto index = 0_z; index < results.size(); ++index)
	{
		auto& rename = results[index];
		const auto& i = items[index];
		const auto source_key = rename_path_key(rename.source, rename.is_folder);

		auto group_collides = [&]
		{
			const auto destination_key = rename_path_key(rename.destination, rename.is_folder);
			rename.destination_exists = rename_path_exists(rename.destination, rename.is_folder);

			if (str::icmp(source_key, destination_key) != 0 && rename.destination_exists &&
				!vacated.contains(destination_key))
				return true;

			for (auto sidecar = 0_z; sidecar < rename.sidecars.size(); ++sidecar)
			{
				const auto& [sidecar_source, sidecar_destination] = rename.sidecars[sidecar];
				if (sidecar_source.icmp(sidecar_destination) != 0 && rename.sidecar_destinations_exist[sidecar] &&
					!vacated.contains(sidecar_destination.pack()))
					return true;
			}
			return false;
		};

		rename.collides = group_collides();

		if (rename.collides && policy == collision_policy::auto_rename)
		{
			const auto base_name = rename.new_name;

			for (auto suffix = 2; suffix < 10000; ++suffix)
			{
				const auto candidate = std::format("{} ({})", base_name, suffix);
				rename.destination = df::file_path(rename.destination.folder(), candidate,
				                                   rename.destination.extension());
				assign_sidecars(rename, i, rename.destination);

				if (!group_collides())
				{
					rename.new_name = std::string(rename.destination.file_name_without_extension());
					rename.renamed_to_avoid_collision = true;
					rename.collides = false;
					break;
				}
			}
		}
		else if (rename.collides && policy == collision_policy::skip)
		{
			rename.skipped = true;
			rename.noop = true;
			// Resolved, so the row must not keep the whole batch invalid. The duplicate-destination
			// pass below still re-sets it for collisions no policy can resolve.
			rename.collides = false;
		}

		rename.valid = name_is_usable[index] != 0 && (!rename.collides || policy == collision_policy::replace);
	}

	if (policy == collision_policy::skip)
	{
		// Skip resolves a collision by leaving the source where it is, so it no longer frees that
		// path for whatever row was cleared to take it, and settling that can cascade.
		std::map<std::string, size_t, df::iless> wanted;

		for (auto index = 0_z; index < results.size(); ++index)
		{
			wanted.emplace(rename_path_key(results[index].destination, results[index].is_folder), index);
			for (const auto& sidecar : results[index].sidecars) wanted.emplace(sidecar.second.pack(), index);
		}

		std::vector<size_t> stalled;
		for (auto index = 0_z; index < results.size(); ++index)
			if (results[index].skipped) stalled.emplace_back(index);

		while (!stalled.empty())
		{
			const auto& stalled_row = results[stalled.back()];
			stalled.pop_back();

			std::vector<std::string> released{rename_path_key(stalled_row.source, stalled_row.is_folder)};
			for (const auto& sidecar : stalled_row.sidecars) released.emplace_back(sidecar.first.pack());

			for (const auto& key : released)
			{
				if (vacated.erase(key) == 0) continue;

				const auto found = wanted.find(key);
				if (found == wanted.end()) continue;

				auto& blocked = results[found->second];
				if (blocked.noop) continue;

				blocked.skipped = true;
				blocked.noop = true;
				stalled.emplace_back(found->second);
			}
		}
	}

	// Two rows targeting the same new name is a collision the policy cannot resolve
	// safely without reordering, so it always blocks.
	std::map<std::string, int, df::iless> destination_counts;

	for (const auto& rename : results)
	{
		++destination_counts[rename_path_key(rename.destination, rename.is_folder)];
		for (const auto& sidecar : rename.sidecars) ++destination_counts[sidecar.second.pack()];
	}

	for (auto& rename : results)
	{
		const auto duplicates = destination_counts[rename_path_key(rename.destination, rename.is_folder)] > 1 ||
			std::ranges::any_of(rename.sidecars, [&](const auto& sidecar)
			{
				return destination_counts[sidecar.second.pack()] > 1;
			});
		if (duplicates)
		{
			rename.valid = false;
			rename.collides = true;
			rename.skipped = false;
			rename.renamed_to_avoid_collision = false;
		}
	}

	return results;
}

std::vector<rename_item> calc_item_renames(const df::item_set& items, const std::string_view template_name,
                                           const int start, const collision_policy policy)
{
	return calc_item_renames(snapshot_rename_sources(items), template_name, start, policy);
}

bool can_rename_items(const std::vector<rename_item>& renames)
{
	return !renames.empty() && std::ranges::all_of(renames, [](const rename_item& item) { return item.valid; }) &&
		std::ranges::any_of(renames, [](const rename_item& item) { return !item.noop; });
}

int count_rename_collisions(const std::vector<rename_item>& renames, const collision_policy policy)
{
	const auto count = [&renames](auto&& predicate)
	{
		return static_cast<int>(std::ranges::count_if(renames, predicate));
	};

	switch (policy)
	{
	case collision_policy::skip: return count([](const rename_item& item) { return item.skipped; });
	case collision_policy::auto_rename: return count([](const rename_item& item)
		{
			return item.renamed_to_avoid_collision;
		});
	case collision_policy::replace:
	case collision_policy::block_run: break;
	}

	return count([](const rename_item& item) { return item.collides; });
}

std::string format_sequence(const std::string_view original_name, const std::string_view template_name,
                            const int seq)

{
	static auto numbers = "0123456789";
	std::string result;

	if (!template_name.empty())
	{
		const auto reverse_template = std::string(template_name.rbegin(), template_name.rend());
		const auto reverse_name = std::string(original_name.rbegin(), original_name.rend());

		const auto org_len = reverse_name.size();
		auto i_div = seq;
		auto original_index = 0u;
		const auto sequence_digits = static_cast<int>(
			std::count(reverse_template.begin(), reverse_template.end(), '#'));
		auto sequence_placeholders = sequence_digits;

		for (const auto c : reverse_template)
		{
			switch (c)
			{
			case L'#':
				result.push_back(numbers[i_div % 10]);
				i_div /= 10;
				--sequence_placeholders;
				if (sequence_placeholders == 0)
				{
					while (i_div > 0)
					{
						result.push_back(numbers[i_div % 10]);
						i_div /= 10;
					}
				}
				break;
			case L'?':
				result.push_back(org_len > original_index ? reverse_name[original_index] : ' ');
				++original_index;
				break;
			default:
				result.push_back(c);
				break;
			}
		}
	}

	return std::string(result.rbegin(), result.rend());
}

bool unchanged_since_analysis(const df::file_path path, const uint64_t modified, const uint64_t size)
{
	const auto current = platform::file_attributes(path);
	return current.exists() && current.modified == modified && current.size == size;
}

import_analysis_result import_analysis(const std::vector<folder_scan_item>& src_items,
                                       const import_options& options, const item_import_set& previous_imported,
                                       const df::cancel_token& token)
{
	import_analysis_result result;
	const auto now = platform::now();

	// Destinations already claimed by earlier items in this plan. Two sources can resolve to the
	// same destination (the same file name in different source folders), which the file system
	// cannot report because nothing has been written yet. Without this, Replace would silently
	// discard one source and Auto-rename would hand both the same free name.
	df::unique_paths planned_destinations;


	for (const auto& i : src_items)
	{
		if (token.is_cancelled()) break;

		const auto md = i.item.metadata.load();

		df::date_t created;

		if (md)
		{
			created = md->created();
		}

		const auto import_folder_out = replace_tokens(options.dest_structure, md, i.item.name, i.item.created());
		const auto path_in = i.folder.combine_file(i.item.name);
		auto path_out = options.dest_folder.combine(import_folder_out).combine_file(path_in.name());
		const auto import_rec = item_import{i.item.name, i.item.file_modified, i.item.size, now};
		std::vector<df::file_path> sidecar_paths_in;
		if (md)
		{
			for (const auto& file_name : split(md->sidecars, true))
				sidecar_paths_in.emplace_back(i.folder.combine_file(file_name));
		}

		auto sidecar_destination = [](const df::file_path primary, const df::file_path sidecar)
		{
			return primary.folder().combine_file(primary.file_name_without_extension()).extension(sidecar.extension());
		};
		auto group_exists = [&](const df::file_path primary)
		{
			if (primary.exists() || planned_destinations.contains(primary)) return true;
			return std::ranges::any_of(sidecar_paths_in,
			                           [&](const auto sidecar)
			                           {
				                           const auto destination = sidecar_destination(primary, sidecar);
				                           return destination.exists() || planned_destinations.contains(destination);
			                           });
		};

		const bool already_exists = group_exists(path_out);

		// One explicit collision policy decides what happens when the destination exists.
		// Auto-rename resolves the whole media/sidecar group with one shared suffix.
		auto blocked_by_collision = false;

		if (already_exists)
		{
			switch (options.collision)
			{
			case collision_policy::auto_rename:
				for (auto suffix = 2; suffix < 10000; ++suffix)
				{
					const auto candidate = df::file_path(path_out.folder(),
					                                     std::format("{} ({})", path_out.file_name_without_extension(),
					                                                 suffix), path_out.extension());
					if (!group_exists(candidate))
					{
						path_out = candidate;
						break;
					}
				}
				break;
			case collision_policy::replace:
				break;
			case collision_policy::skip:
			case collision_policy::block_run:
				blocked_by_collision = true;
				break;
			}
		}

		if (previous_imported.contains(import_rec))
		{
			result[path_out.folder()].emplace_back(path_in, path_out, import_action::already_imported, created,
			                                       import_rec, already_exists, import_folder_out);
		}
		else if (blocked_by_collision)
		{
			result[path_out.folder()].emplace_back(path_in, path_out, import_action::already_exists, created,
			                                       import_rec, already_exists, import_folder_out);
		}
		else
		{
			std::vector<std::pair<df::file_path, df::file_path>> sidecars;
			sidecars.reserve(sidecar_paths_in.size());

			for (const auto sidecar_path_in : sidecar_paths_in)
			{
				const auto sidecar_path_out = sidecar_destination(path_out, sidecar_path_in);
				sidecars.emplace_back(sidecar_path_in, sidecar_path_out);
				planned_destinations.emplace(sidecar_path_out);
			}

			// Every other policy leaves the destination free, and the write itself proves that. Only
			// Replace needs a record of what it is about to write over.
			const auto destination_fi = options.collision == collision_policy::replace
				                            ? platform::file_attributes(path_out)
				                            : platform::file_attributes_t{};

			result[path_out.folder()].emplace_back(path_in, path_out, import_action::import, created, import_rec,
			                                       already_exists, import_folder_out,
			                                       platform::file_attributes(path_in), destination_fi,
			                                       std::move(sidecars));
			planned_destinations.emplace(path_out);
		}
	}

	return result;
}

import_result import_copy(index_state& index, df::results_ptr results, const import_analysis_result& src_items,
                          const import_options& options, df::cancel_token token)
{
	import_result result;
	result_scope rr(results);

	std::vector<folder_scan_item> existing;
	std::vector<folder_scan_item> previous;
	df::unique_folders write_folders;
	struct import_group_state
	{
		bool attempted = false;
		bool success = true;
	};
	df::hash_map<item_import, import_group_state, item_import_hash, item_import_eq> import_states;

	for (const auto& ff_dest : src_items)
	{
		for (const auto& item : ff_dest.second)
		{
			if (item.action == import_action::import) import_states.try_emplace(item.import_rec);
		}
	}

	for (const auto& ff_dest : src_items)
	{
		const auto folder_out = ff_dest.first;
		if (token.is_cancelled())
		{
			for (const auto& item : ff_dest.second)
			{
				if (item.action == import_action::import)
				{
					import_states[item.import_rec].success = false;
					results->start_item(item.source.name());
					results->end_item(item.source.name(), item_status::cancel);
				}
				else
				{
					results->end_item(item.source.name(), item_status::ignore);
				}
			}
			continue;
		}

		// Nothing is written here when every item was already imported or resolved away by the
		// collision policy, so do not create a destination folder that would stay empty.
		const auto has_imports = std::ranges::any_of(ff_dest.second,
		                                             [](const auto& item)
		                                             {
			                                             return item.action == import_action::import;
		                                             });

		if (has_imports)
		{
			const bool folder_out_exists = folder_out.exists();
			const auto create_folder_result = folder_out_exists
				                                  ? platform::file_op_result{platform::file_op_result_code::OK, {}, {}}
				                                  : platform::create_folder(folder_out);

			if (create_folder_result.failed())
			{
				results->abort(
					create_folder_result.format_error(str_format(tt.error_create_folder_failed_fmt.sv(), folder_out)));
				return result;
			}

			write_folders.emplace(folder_out);
		}

		for (const auto& i : ff_dest.second)
		{
			// Every reviewed row is accounted for in the results, so a file that was skipped or
			// already imported is stated as ignored rather than dropping out of the list.
			if (i.action != import_action::import)
			{
				results->end_item(i.source.name(), item_status::ignore);
				continue;
			}

			if (results->is_canceled())
			{
				import_states[i.import_rec].success = false;
				results->start_item(i.source.name());
				results->end_item(i.source.name(), item_status::cancel);
				continue;
			}

			results->start_item(i.source.name());

			const auto path_in = i.source;
			const auto path_out = i.destination;

			// Only the Replace policy is allowed to write over an existing destination.
			const auto fail_if_exists = options.collision != collision_policy::replace;

			// ...and only over the destination the review actually saw. A Replace row whose destination
			// was free at analysis is not a reviewed replacement, so the write itself has to prove the
			// path is still free rather than overwriting whatever appeared since.
			const auto primary_fail_if_exists = fail_if_exists || !i.destination_fi.exists();

			// The reviewed row named a specific file. A source that no longer matches would import
			// content nobody approved, and under Replace a changed destination would be written over
			// without ever having been reviewed. Neither is recoverable once the write starts.
			const auto source_unchanged = unchanged_since_analysis(path_in, i.source_fi.modified, i.source_fi.size);
			const auto destination_unchanged = fail_if_exists || !i.destination_fi.exists() ||
				unchanged_since_analysis(path_out, i.destination_fi.modified, i.destination_fi.size);

			if (!source_unchanged || !destination_unchanged)
			{
				auto& state = import_states[i.import_rec];
				state.attempted = true;
				state.success = false;
				results->end_item(i.source.name(), item_status::fail);
				++result.refused;
				continue;
			}

			// Sidecars are written before the file they describe and undone in reverse if anything in the
			// group fails, so the group either arrives whole or is left entirely at the source. Only
			// sidecars this run created are undone: under Replace the write can land on a file that
			// already existed, and deleting or moving that path back would destroy the user's original.
			auto undo_sidecar = [&](const std::pair<df::file_path, df::file_path>& moved)
			{
				if (options.is_move) move_or_copy(moved.second, moved.first, true, false);
				else platform::delete_file(moved.second);
			};

			std::vector<std::pair<df::file_path, df::file_path>> moved_sidecars;
			auto move_or_copy_result = platform::file_op_result{platform::file_op_result_code::OK, {}, {}};

			for (const auto& [sidecar_in, sidecar_out] : i.sidecars)
			{
				const auto destination_existed = !fail_if_exists && platform::file_attributes(sidecar_out).exists();
				move_or_copy_result = move_or_copy(sidecar_in, sidecar_out, options.is_move, fail_if_exists);
				if (move_or_copy_result.failed()) break;
				if (!destination_existed) moved_sidecars.emplace_back(sidecar_in, sidecar_out);
			}

			if (move_or_copy_result.success())
			{
				move_or_copy_result = move_or_copy(path_in, path_out, options.is_move, primary_fail_if_exists);
			}

			if (move_or_copy_result.failed())
			{
				for (auto s = moved_sidecars.rbegin(); s != moved_sidecars.rend(); ++s) undo_sidecar(*s);
			}

			if (move_or_copy_result.success())
			{
				import_states[i.import_rec].attempted = true;
				if (options.set_created_date && i.created_date.is_valid())
				{
					platform::created_date(path_out, i.created_date);
				}

				// An import that moves empties the folder it took from, which is only in the index
				// when importing from one collection folder to another.
				if (options.is_move) write_folders.emplace(path_in.folder());

				result.folder = path_out.folder();
			}
			else
			{
				auto& state = import_states[i.import_rec];
				state.attempted = true;
				state.success = false;
				// A destination that appeared after the review is a file that changed, not a disk error.
				if (move_or_copy_result.code == platform::file_op_result_code::ALREADY_EXISTS) ++result.refused;
			}

			results->end_item(i.source.name(), to_status(move_or_copy_result.code));
		}
	}

	for (const auto& [import_rec, state] : import_states)
	{
		if (state.attempted && state.success) result.imports.emplace(import_rec);
	}

	index.queue_scan_folders(write_folders);

	std::string result_text;

	if (!existing.empty())
	{
		result_text += format_plural_text(tt.ignored_exist_already_fmt, existing.front().item.name,
		                                  static_cast<int>(existing.size()), {},
		                                  static_cast<int>(src_items.size()));
	}

	if (!previous.empty())
	{
		if (!result_text.empty()) result_text += "\n\n";
		result_text += format_plural_text(tt.ignored_previous_fmt, previous.front().item.name,
		                                  static_cast<int>(previous.size()), {},
		                                  static_cast<int>(src_items.size()));
	}

	rr.complete(result_text);

	return result;
}

size_t count_imports(const std::vector<import_analysis_item>& items)
{
	size_t result = 0;

	for (const auto& i : items)
	{
		if (i.action == import_action::import)
		{
			++result;
		}
	}

	return result;
}

size_t count_imports(const import_analysis_result& items)
{
	size_t result = 0;

	for (const auto& i : items)
	{
		result += count_imports(i.second);
	}

	return result;
}

size_t count_import_collisions(const import_analysis_result& items)
{
	size_t result = 0;
	for (const auto& [folder, folder_items] : items)
	{
		result += std::ranges::count_if(folder_items, [](const auto& item) { return item.already_exists; });
	}
	return result;
}

size_t count_import_colliding_writes(const import_analysis_result& items)
{
	size_t result = 0;
	for (const auto& [folder, folder_items] : items)
	{
		result += std::ranges::count_if(folder_items, [](const auto& item)
		{
			return item.already_exists && item.action == import_action::import;
		});
	}
	return result;
}

std::vector<import_source> calc_import_sources(const view_state& s)
{
	std::vector<import_source> result;

	if (s.has_selection())
	{
		import_source source;
		source.text = str_format(tt.selected_items_fmt.sv(),
		                         format_plural_text(tt.title_item_count_fmt, s.selected_items()));
		source.items = s.selected_items();
		result.emplace_back(source);
	}

	const auto onedrive_camera_roll = known_path(platform::known_folder::onedrive_camera_roll);

	if (onedrive_camera_roll.exists())
	{
		result.emplace_back(std::string(onedrive_camera_roll.text()), onedrive_camera_roll);
	}

	auto drives = platform::scan_drives();
	constexpr int drive_max = 5;

	if (drives.size() > drive_max)
	{
		drives.resize(drive_max);
	}

	for (const auto& d : drives)
	{
		if (d.type == platform::drive_type::removable)
		{
			const auto text = std::format("{} {} {}", d.name, d.vol_name, d.used);
			result.emplace_back(text, df::folder_path(d.name));
		}
	}

	return result;
}


std::string relative_combine(const std::string& relative, const str::cached name)
{
	auto result = relative;

	if (!result.empty() && result.back() != '\\')
	{
		result += '\\';
	}

	result += name;
	return result;
}

sync_action calc_sync_action(const bool local_exists, const bool remote_exists,
                             const uint64_t local_modified, const uint64_t remote_modified,
                             const uint64_t local_size, const uint64_t remote_size,
                             const bool sync_local_remote, const bool sync_remote_local,
                             const bool sync_delete_local, const bool sync_delete_remote)
{
	if (local_exists && remote_exists)
	{
		if (local_modified > remote_modified && sync_local_remote) return sync_action::copy_remote;
		if (remote_modified > local_modified && sync_remote_local) return sync_action::copy_local;
		if (local_modified == remote_modified && local_size != remote_size)
		{
			if (sync_local_remote && !sync_remote_local) return sync_action::copy_remote;
			if (sync_remote_local && !sync_local_remote) return sync_action::copy_local;
		}
	}
	else if (local_exists)
	{
		if (sync_local_remote) return sync_action::copy_remote;
		if (sync_delete_local) return sync_action::delete_local;
	}
	else if (remote_exists)
	{
		if (sync_remote_local) return sync_action::copy_local;
		if (sync_delete_remote) return sync_action::delete_remote;
	}

	return sync_action::none;
}

static bool path_contains(const df::folder_path parent, const df::folder_path child)
{
	const auto parent_text = parent.text().sv();
	const auto child_text = child.text().sv();
	if (parent_text.size() >= child_text.size()) return parent == child;
	return str::icmp(parent_text, child_text.substr(0, parent_text.size())) == 0 &&
		df::is_path_sep(child_text[parent_text.size()]);
}

std::string sync_invalid_message(const sync_analysis_result& analysis)
{
	if (analysis.valid) return {};

	switch (analysis.reason)
	{
	case sync_invalid_reason::folder_unreadable:
		return str_format(tt.is_not_valid_folder_fmt.sv(), analysis.invalid_folder.text());
	case sync_invalid_reason::empty_paths:
	case sync_invalid_reason::overlapping_paths:
	case sync_invalid_reason::ambiguous_local_root:
		return std::string(tt.error_invalid_files.sv());
	case sync_invalid_reason::none:
		break;
	}

	return std::string(tt.error_cannot_continue.sv());
}

sync_analysis_result sync_analysis(const df::index_roots& local_roots, const df::folder_path remote_path,
                                   const bool sync_local_remote, const bool sync_remote_local,
                                   const bool sync_delete_local, const bool sync_delete_remote,
                                   const df::cancel_token& token)
{
	sync_analysis_result result;
	if (remote_path.is_empty() || local_roots.folders.empty())
	{
		result.valid = false;
		result.reason = sync_invalid_reason::empty_paths;
		return result;
	}

	for (const auto& local_root : local_roots.folders)
	{
		if (path_contains(local_root, remote_path) || path_contains(remote_path, local_root))
		{
			result.valid = false;
			result.reason = sync_invalid_reason::overlapping_paths;
			result.invalid_folder = remote_path;
			return result;
		}
	}

	// Which local root owns each relative folder, and the relative folders claimed by more than one
	// root. A relative folder claimed twice cannot name a local destination for a remote-only file.
	std::map<std::string, df::folder_path, df::iless> local_roots_by_relative;
	std::set<std::string, df::iless> ambiguous_relatives;

	std::vector<sync_analysis_folder> local_folders_to_scan;

	for (const auto& f : local_roots.folders)
	{
		local_folders_to_scan.emplace_back(f, f);
	}

	while (!local_folders_to_scan.empty())
	{
		if (token.is_cancelled())
		{
			result.clear();
			result.valid = false;
			return result;
		}

		const auto folder = local_folders_to_scan.back();
		local_folders_to_scan.pop_back();

		if (!is_excluded(local_roots, folder.path))
		{
			const auto contents = platform::iterate_file_items(folder.path, setting.show_hidden);
			if (!contents.success)
			{
				result.clear();
				result.valid = false;
				result.reason = sync_invalid_reason::folder_unreadable;
				result.invalid_folder = folder.path;
				return result;
			}

			for (const auto& file : contents.files)
			{
				if (token.is_cancelled()) break;

				auto& i = result[folder.relative][file.name.str()];
				if (!i.local_path.is_empty() && i.local_root != folder.root)
				{
					result.clear();
					result.valid = false;
					result.reason = sync_invalid_reason::ambiguous_local_root;
					result.invalid_folder = folder.path;
					return result;
				}
				i.local_path = folder.path.combine_file(file.name);
				i.local_fi = file;
				i.local_root = folder.root;
			}

			for (const auto& sub_folder : contents.folders)
			{
				auto relative = relative_combine(folder.relative, sub_folder.name);
				auto sub_folder_path = folder.path.combine(sub_folder.name);
				sync_analysis_folder unknown = {sub_folder_path, folder.root, relative};
				local_folders_to_scan.emplace_back(unknown);

				const auto claimed = local_roots_by_relative.find(relative);

				if (claimed == local_roots_by_relative.end())
				{
					local_roots_by_relative[relative] = folder.root;
				}
				else if (claimed->second != folder.root)
				{
					ambiguous_relatives.emplace(relative);
				}
			}
		}
	}

	std::vector<sync_analysis_folder> remote_folders_to_scan;
	remote_folders_to_scan.emplace_back(remote_path, remote_path);

	while (!remote_folders_to_scan.empty())
	{
		if (token.is_cancelled())
		{
			result.clear();
			result.valid = false;
			return result;
		}

		const auto folder = remote_folders_to_scan.back();
		remote_folders_to_scan.pop_back();

		if (!is_excluded(local_roots, folder.path))
		{
			const auto contents = platform::iterate_file_items(folder.path, setting.show_hidden);
			if (!contents.success)
			{
				result.clear();
				result.valid = false;
				result.reason = sync_invalid_reason::folder_unreadable;
				result.invalid_folder = folder.path;
				return result;
			}

			for (const auto& file : contents.files)
			{
				if (token.is_cancelled()) break;

				auto& i = result[folder.relative][file.name.str()];
				i.remote_path = folder.path.combine_file(file.name);
				i.remote_fi = file;
				i.remote_root = folder.root;
			}

			for (const auto& sub_folder : contents.folders)
			{
				sync_analysis_folder unknown = {
					folder.path.combine(sub_folder.name), remote_path,
					relative_combine(folder.relative, sub_folder.name)
				};
				remote_folders_to_scan.emplace_back(unknown);
			}
		}
	}

	for (auto&& i : result)
	{
		for (auto&& f : i.second)
		{
			if (token.is_cancelled())
			{
				result.clear();
				result.valid = false;
				return result;
			}

			const auto local_exists = !f.second.local_path.is_empty();
			const auto remote_exists = !f.second.remote_path.is_empty();

			if (f.second.local_root.is_empty())
			{
				// Keyed by the relative folder, not the file name. A remote-only file belongs to the
				// local root that owns its folder; with a single root there is only one answer.
				const auto found_relative = local_roots_by_relative.find(i.first);

				if (found_relative != local_roots_by_relative.end() && !ambiguous_relatives.contains(i.first))
				{
					f.second.local_root = found_relative->second;
				}
				else if (local_roots.folders.size() == 1)
				{
					f.second.local_root = *local_roots.folders.begin();
				}
			}

			const auto action = calc_sync_action(local_exists, remote_exists,
			                                     f.second.local_fi.attributes.modified,
			                                     f.second.remote_fi.attributes.modified,
			                                     f.second.local_fi.attributes.size,
			                                     f.second.remote_fi.attributes.size,
			                                     sync_local_remote, sync_remote_local, sync_delete_local,
			                                     sync_delete_remote);

			// Only copying a remote-only file into the collection needs a local destination. Reporting
			// a remote file the run would ignore as an invalid path fails a whole sync over nothing.
			if (f.second.local_root.is_empty() && action == sync_action::copy_local)
			{
				result.clear();
				result.valid = false;
				result.reason = sync_invalid_reason::ambiguous_local_root;
				return result;
			}

			if (f.second.remote_root.is_empty())
			{
				f.second.remote_root = remote_path;
			}

			if (f.second.local_path.is_empty() && !f.second.local_root.is_empty())
			{
				f.second.local_path = f.second.local_root.combine(i.first).combine_file(f.first);
			}

			if (f.second.remote_path.is_empty())
			{
				f.second.remote_path = f.second.remote_root.combine(i.first).combine_file(f.first);
			}

			f.second.action = action;

			if (f.second.action == sync_action::delete_local)
			{
				f.second.delete_crc = platform::file_crc32(f.second.local_path, token);
			}
			else if (f.second.action == sync_action::delete_remote)
			{
				f.second.delete_crc = platform::file_crc32(f.second.remote_path, token);
			}
		}
	}

	return result;
}

sync_run_result sync_copy(const df::results_ptr& status, const sync_analysis_result& analysis_result,
                          const df::cancel_token& token)
{
	sync_run_result result;

	for (const auto& i : analysis_result)
	{
		for (const auto& f : i.second)
		{
			const auto local_path = f.second.local_path;
			const auto remote_path = f.second.remote_path;
			const auto action = f.second.action;

			df::assert_true(!remote_path.is_empty());
			df::assert_true(!local_path.is_empty() || action == sync_action::none ||
				action == sync_action::delete_remote);

			// A remote-only file that no local root claims has no local name to report.
			const auto name = local_path.is_empty() ? remote_path.name() : local_path.name();

			// Every reviewed row is accounted for in the results, so a file the run left alone is
			// stated as ignored rather than disappearing from the list the user just approved.
			if (action == sync_action::none)
			{
				status->end_item(name, item_status::ignore);
				continue;
			}

			status->start_item(name);

			if (token.is_cancelled())
			{
				status->end_item(name, item_status::cancel);
				continue;
			}

			const auto& local_attributes = f.second.local_fi.attributes;
			const auto& remote_attributes = f.second.remote_fi.attributes;

			// The source is read immediately after this, so a source that no longer matches the
			// review would copy content nobody approved.
			const auto source_unchanged = [&]
			{
				return action == sync_action::copy_local
					       ? unchanged_since_analysis(remote_path, remote_attributes.modified, remote_attributes.size)
					       : unchanged_since_analysis(local_path, local_attributes.modified, local_attributes.size);
			};

			// A destination the review expected to be free is proved free by the write itself, which
			// leaves no window for it to appear. One that was already there has to still be the file
			// the user agreed to overwrite.
			const auto destination_unchanged = [&](const platform::file_attributes_t& reviewed,
			                                       const df::file_path path)
			{
				return !reviewed.exists() || unchanged_since_analysis(path, reviewed.modified, reviewed.size);
			};

			// Deletes were reviewed against the file's content, so that is what they are held to.
			const auto delete_content_unchanged = [&](const df::file_path path)
			{
				return platform::file_crc32(path, token) == f.second.delete_crc;
			};

			// A cancelled check proves nothing, so it is reported as the cancellation it was rather
			// than as a file that changed.
			const auto refused = [&](const bool checks_passed)
			{
				if (checks_passed) return false;
				const auto cancelled = token.is_cancelled();
				status->end_item(name, cancelled ? item_status::cancel : item_status::fail);
				if (!cancelled) ++result.refused;
				return true;
			};

			// A destination that appeared after the review is a file that changed, not a disk error.
			const auto write = [&](const platform::file_op_result& op)
			{
				if (op.code == platform::file_op_result_code::ALREADY_EXISTS) ++result.refused;
				status->end_item(name, to_status(op.code));
			};

			switch (action)
			{
			case sync_action::none:
				break;
			case sync_action::copy_local:
				if (refused(source_unchanged() && destination_unchanged(local_attributes, local_path))) break;
				write(platform::copy_file(remote_path, local_path, !local_attributes.exists(), true));
				result.folders_changed.emplace(local_path.folder());
				break;
			case sync_action::copy_remote:
				if (refused(source_unchanged() && destination_unchanged(remote_attributes, remote_path))) break;
				write(platform::copy_file(local_path, remote_path, !remote_attributes.exists(), true));
				break;
			case sync_action::delete_local:
				if (refused(unchanged_since_analysis(local_path, local_attributes.modified, local_attributes.size) &&
					delete_content_unchanged(local_path)))
					break;
				status->end_item(name, to_status(platform::delete_file(local_path).code));
				result.folders_changed.emplace(local_path.folder());
				break;
			case sync_action::delete_remote:
				if (refused(unchanged_since_analysis(remote_path, remote_attributes.modified, remote_attributes.size) &&
					delete_content_unchanged(remote_path)))
					break;
				status->end_item(name, to_status(platform::delete_file(remote_path).code));
				break;
			}
		}
	}

	return result;
}

void toggle_collection_entry(settings_t::index_t& collection_settings, const df::folder_path folder,
                             const bool is_remove)
{
	const auto local_folders = platform::local_folders();

	if (is_remove)
	{
		if (local_folders.pictures == folder) collection_settings.pictures = false;
		else if (local_folders.music == folder) collection_settings.music = false;
		else if (local_folders.video == folder) collection_settings.video = false;
		else if (local_folders.onedrive_pictures == folder) collection_settings.onedrive_pictures = false;
		else if (local_folders.onedrive_video == folder) collection_settings.onedrive_video = false;
		else if (local_folders.onedrive_music == folder) collection_settings.onedrive_music = false;
		else if (local_folders.dropbox_photos == folder) collection_settings.drop_box = false;

		std::string more_folders;

		for (const auto existing_folder_path : split_collection_folders(collection_settings.more_folders))
		{
			if (folder != df::folder_path(existing_folder_path))
			{
				if (!more_folders.empty()) more_folders += "\r\n";
				more_folders += existing_folder_path;
			}
		}

		collection_settings.more_folders = more_folders;
	}
	else
	{
		// add
		if (local_folders.pictures == folder) collection_settings.pictures = true;
		else if (local_folders.music == folder) collection_settings.music = true;
		else if (local_folders.video == folder) collection_settings.video = true;
		else if (local_folders.onedrive_pictures == folder) collection_settings.onedrive_pictures = true;
		else if (local_folders.onedrive_video == folder) collection_settings.onedrive_video = true;
		else if (local_folders.onedrive_music == folder) collection_settings.onedrive_music = true;
		else if (local_folders.dropbox_photos == folder) collection_settings.drop_box = true;
		else
		{
			if (!collection_settings.more_folders.empty()) collection_settings.more_folders += "\r\n";
			collection_settings.more_folders += folder.text();
		}
	}
}

std::vector<std::string> check_overwrite(const df::folder_path write_folder, const df::item_set& items,
                                         const std::string_view new_extension)
{
	// One enumeration of the destination rather than a query per item: the selection is unbounded and
	// this runs before a modal, where a round trip per item over a network share would stall the
	// window. A destination that cannot be read reports no collisions, which leaves the caller on its
	// non-overwriting default rather than on a guess.
	const auto contents = platform::iterate_file_items(write_folder, true);
	if (!contents.success) return {};

	std::set<std::string, df::iless> existing;
	for (const auto& f : contents.files) existing.emplace(f.name.str());
	for (const auto& f : contents.folders) existing.emplace(f.name.str());

	std::vector<std::string> result;

	for (const auto f : items.folder_paths())
	{
		if (existing.contains(std::string(f.name()))) result.emplace_back(f.name());
	}

	for (const auto f : items.file_paths())
	{
		const auto dest = write_folder.combine_file(f.name()).extension(
			new_extension.empty() ? f.extension() : new_extension);

		if (existing.contains(std::string(dest.name()))) result.emplace_back(dest.name());
	}

	return result;
}

std::string format_collision_summary(const collision_policy policy, const int count)
{
	if (count <= 0) return {};

	const auto num = str::to_string(count);

	switch (policy)
	{
	case collision_policy::skip: return str_format(tt.collision_skipped_fmt.sv(), num);
	case collision_policy::replace: return str_format(tt.collision_replaced_fmt.sv(), num);
	case collision_policy::auto_rename: return str_format(tt.collision_renamed_fmt.sv(), num);
	case collision_policy::block_run: break;
	}

	return str_format(tt.collision_blocked_fmt.sv(), num);
}

df::file_path next_free_destination(const df::file_path destination)
{
	const auto folder = destination.folder();
	const auto base_name = destination.file_name_without_extension();
	const auto extension = destination.extension();

	// Bounded: give up rather than spin if a folder is saturated with variants.
	for (auto suffix = 2; suffix < 10000; ++suffix)
	{
		auto candidate = df::file_path(folder, std::format("{} ({})", base_name, suffix), extension);
		if (!candidate.exists()) return candidate;
	}

	return destination;
}

std::vector<convert_source> snapshot_convert_sources(const df::item_set& items)
{
	std::vector<convert_source> result;
	result.reserve(items.size());

	for (const auto& item : items.items())
	{
		const auto md = item->metadata();
		result.emplace_back(item->path(), md ? md->dimensions() : sizei{}, item->xmp());
	}

	return result;
}

std::vector<convert_item_plan> plan_convert_outputs(const df::folder_path write_folder,
                                                    const std::vector<convert_source>& sources,
                                                    const std::string_view new_extension, const collision_policy policy)
{
	auto ordered = sources;
	std::ranges::sort(ordered, [](const auto& left, const auto& right)
	{
		return left.path.icmp(right.path) < 0;
	});

	df::hash_set<df::file_path, df::ihash, df::ieq> planned_paths;
	std::vector<convert_item_plan> result;
	result.reserve(ordered.size());

	for (const auto& source : ordered)
	{
		const auto base_name = source.path.file_name_without_extension();
		auto destination = df::file_path(write_folder, base_name, new_extension);
		// Two sources can share a base name, so a destination another row already claimed is taken
		// even when nothing is on disk yet. Use the " (n)" variant every other operation uses.
		for (auto suffix = 2; planned_paths.contains(destination); ++suffix)
		{
			destination = df::file_path(write_folder, std::format("{} ({})", base_name, suffix), new_extension);
		}

		convert_item_plan plan;
		plan.source = source;
		plan.collides = destination.exists();

		if (plan.collides && policy == collision_policy::auto_rename)
		{
			auto renamed = next_free_destination(destination);
			for (auto suffix = 2; renamed.exists() || planned_paths.contains(renamed); ++suffix)
			{
				renamed = df::file_path(write_folder, std::format("{} ({})", base_name, suffix), new_extension);
			}
			destination = renamed;
			plan.renamed_to_avoid_collision = true;
		}
		else if (plan.collides && policy == collision_policy::skip)
		{
			plan.skipped = true;
		}

		plan.destination = destination;
		planned_paths.emplace(destination);
		result.emplace_back(std::move(plan));
	}

	return result;
}

df::date_t adjusted_item_date(const df::date_t created, const df::date_t new_start, const df::date_t original_start)
{
	return created.is_valid() && original_start.is_valid() ? created + (new_start - original_start) : new_start;
}

icon_index drive_icon(const platform::drive_type d)
{
	switch (d)
	{
	case platform::drive_type::removable: return icon_index::usb;
	case platform::drive_type::fixed: return icon_index::hard_drive;
	case platform::drive_type::remote: return icon_index::network;
	case platform::drive_type::cdrom: return icon_index::disk;
	case platform::drive_type::device: return icon_index::disk;
	default: ;
	}

	return icon_index::hard_drive;
}

bool df::is_excluded(const index_roots& roots, const folder_path path)
{
	if (roots.excludes.contains(path)) return true;

	const auto name = path.name();

	for (const auto& exclude : roots.exclude_wildcards)
	{
		if (str::wildcard_icmp(name, exclude))
			return true;
	}

	return false;
}
