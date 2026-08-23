// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Utility functions for file operations. Includes rename templating, import/sync
// analysis, collection management, and file operation helpers.

#pragma once

#include "model_db.h"

struct folder_scan_item;

static constexpr auto doc_template_url = "https://www.diffractor.com/docs/template";

class command_status;

using item_results_ptr = std::shared_ptr<command_status>;
using item_import_set = df::hash_set<item_import, item_import_hash, item_import_eq>;

class result_scope
{
	const df::results_ptr _r;
	const int _uncaught_exceptions = std::uncaught_exceptions();
	bool _completed = false;

public:
	explicit result_scope(df::results_ptr r) : _r(std::move(r))
	{
	}

	~result_scope()
	{
		if (!_completed)
		{
			if (std::uncaught_exceptions() > _uncaught_exceptions)
			{
				_r->abort(tt.error_cannot_continue);
			}
			else
			{
				_r->complete();
			}
		}
	}

	void complete(const std::string_view message = {})
	{
		_r->complete(message);
		_completed = true;
	}
};

struct rename_item
{
	df::file_path source;
	df::file_path destination;
	bool is_folder = false;
	bool destination_exists = false;
	std::vector<std::pair<df::file_path, df::file_path>> sidecars;
	std::vector<bool> sidecar_destinations_exist;
	collision_policy policy = collision_policy::block_run;
	std::string original_name;
	std::string new_name;
	bool valid = false;
	bool noop = false;
	// True when the requested name already exists or duplicates another row in this plan.
	bool collides = false;
	// True when the collision policy resolved this row by leaving it alone.
	bool skipped = false;
	// True when the collision policy resolved this row by choosing a different name.
	bool renamed_to_avoid_collision = false;
};

struct rename_source
{
	df::file_path source;
	std::string original_name;
	prop::item_metadata_const_ptr metadata;
	df::date_t media_created;
	bool is_folder = false;
	std::vector<df::file_path> sidecars;
};

// A folder item's path carries an empty name and platform::exists(file_path) reports false for
// a directory, so existence for a rename row has to be tested against the right kind of path.
bool rename_path_exists(df::file_path path, bool is_folder);

// A folder source and a folder destination only compare as the same place once both are reduced to
// this form, because a folder path packs with a trailing separator and an empty name.
std::string rename_path_key(df::file_path path, bool is_folder);

// Applies the named collision policy from docs/design.md. Block Run leaves colliding rows
// invalid so can_rename_items() refuses; Skip marks them noop; Replace allows the overwrite;
// Auto-rename picks the next free " (n)" suffix. A destination held by a file the same plan renames
// away is free by the time the run reaches it, so it is not a collision.
std::vector<rename_item> calc_item_renames(const df::item_set& items, std::string_view template_name, int start,
                                           collision_policy policy);
std::vector<rename_source> snapshot_rename_sources(const df::item_set& items);
std::vector<rename_item> calc_item_renames(const std::vector<rename_source>& items,
                                           std::string_view template_name, int start, collision_policy policy);
bool can_rename_items(const std::vector<rename_item>& renames);
// Number of rows the policy had to resolve, for the Review statement.
int count_rename_collisions(const std::vector<rename_item>& renames, collision_policy policy);

std::string format_sequence(std::string_view original_name, std::string_view template_name, int seq);

struct import_options
{
	df::folder_path dest_folder;
	std::string source_filter;
	std::string dest_structure;

	bool is_move = false;
	bool set_created_date = false;
	bool rename_different_attributes = false;
	collision_policy collision = collision_policy::skip;
};

struct import_source
{
	std::string text;
	df::folder_path path;
	bool selected = false;
	df::item_set items;
};


enum class import_action
{
	import,
	already_exists,
	already_imported
};

struct import_result
{
	df::folder_path folder;
	item_import_set imports;
	// Rows whose files no longer matched the review. They wrote nothing and need a fresh analysis.
	uint32_t refused = 0;
};

struct import_analysis_item
{
	df::file_path source;
	df::file_path destination;
	import_action action;
	df::date_t created_date;
	item_import import_rec;
	bool already_exists = false;
	std::string sub_folder;
	// The source and destination as they stood at analysis, read through the same query the run uses
	// so the two are directly comparable. destination_fi is only filled for Replace, the one policy
	// that writes over a file instead of proving the name is free.
	platform::file_attributes_t source_fi;
	platform::file_attributes_t destination_fi;
	// Sidecars travel with the file they describe rather than standing as rows of their own, so a
	// partial import can never leave metadata at one end and its file at the other.
	std::vector<std::pair<df::file_path, df::file_path>> sidecars;
};

using import_analysis_result = std::map<df::folder_path, std::vector<import_analysis_item>, df::iless>;

// Confirms a path still holds the file the user reviewed. A query that fails counts as changed:
// a run must never overwrite or delete a file it could not look at.
bool unchanged_since_analysis(df::file_path path, uint64_t modified, uint64_t size);

import_analysis_result import_analysis(const std::vector<folder_scan_item>& src_items,
                                       const import_options& options, const item_import_set& previous_imported,
                                       const df::cancel_token& token);

import_result import_copy(index_state& index, df::results_ptr results, const import_analysis_result& src_items,
                          const import_options& options, df::cancel_token token);

std::vector<import_source> calc_import_sources(const view_state& s);

size_t count_imports(const std::vector<import_analysis_item>& items);
size_t count_imports(const import_analysis_result& items);

// Every row whose destination already exists, whether or not the plan will write it. This is what
// Block Run refuses on, so it must keep counting rows the policy already resolved away.
size_t count_import_collisions(const import_analysis_result& items);

// The subset of those the plan will actually write over. A previously imported row keeps its
// collision but destroys nothing, so only this number belongs in a message about overwriting.
size_t count_import_colliding_writes(const import_analysis_result& items);

enum class sync_action
{
	none,
	copy_local,
	copy_remote,
	delete_local,
	delete_remote,
};

sync_action calc_sync_action(bool local_exists, bool remote_exists,
                             uint64_t local_modified, uint64_t remote_modified,
                             uint64_t local_size, uint64_t remote_size,
                             bool sync_local_remote, bool sync_remote_local,
                             bool sync_delete_local, bool sync_delete_remote);

struct sync_analysis_item
{
	df::file_path local_path;
	df::file_path remote_path;

	platform::file_info local_fi;
	platform::file_info remote_fi;

	df::folder_path local_root;
	df::folder_path remote_root;

	sync_action action = sync_action::none;
	uint32_t delete_crc = 0;
};

struct sync_analysis_folder
{
	df::folder_path path;
	df::folder_path root;
	std::string relative;
};

using sync_analysis_items = std::map<std::string, sync_analysis_item, df::path_key_less>;

// Why an analysis could not be produced. Sync inputs are user supplied, so the common failures are
// ordinary configuration mistakes and must be reported as such rather than as an internal fault.
enum class sync_invalid_reason
{
	none,
	empty_paths,
	overlapping_paths,
	folder_unreadable,
	ambiguous_local_root,
};

struct sync_analysis_result : std::map<std::string, sync_analysis_items, df::path_key_less>
{
	bool valid = true;
	sync_invalid_reason reason = sync_invalid_reason::none;
	df::folder_path invalid_folder;
};

// Message describing why sync_analysis returned an invalid result. Empty when the result is valid.
std::string sync_invalid_message(const sync_analysis_result& analysis);

uint32_t count_sync_actions(const sync_analysis_result& analysis);
uint32_t count_sync_actions(const sync_analysis_result& analysis, sync_action action);

sync_analysis_result sync_analysis(const df::index_roots& local_roots, df::folder_path remote_path,
                                   bool sync_local_remote, bool sync_remote_local,
                                   bool sync_delete_local, bool sync_delete_remote,
                                   const df::cancel_token& token);

// Performs the reviewed sync plan. Returns the local folders it changed so the index can be
// brought back into agreement with what is now on disk.
struct sync_run_result
{
	df::unique_folders folders_changed;
	// Rows whose files no longer matched the review. They wrote nothing and need a fresh analysis.
	uint32_t refused = 0;
};

sync_run_result sync_copy(const df::results_ptr& status, const sync_analysis_result& analysis_result,
                          const df::cancel_token& token);

void toggle_collection_entry(settings_t::index_t& collection_settings, df::folder_path folder, bool is_remove);


std::vector<std::string> check_overwrite(df::folder_path write_folder, const df::item_set& items,
                                         std::string_view new_extension);

// UI-thread snapshot of one conversion source. Planning probes the filesystem, so it runs on a
// worker and must never reach through a df::item_element to do it.
struct convert_source
{
	df::file_path path;
	sizei dimensions;
	str::cached xmp;
};

std::vector<convert_source> snapshot_convert_sources(const df::item_set& items);

struct convert_item_plan
{
	convert_source source;
	df::file_path destination;
	// True when the destination existed before the policy was applied.
	bool collides = false;
	// Resolved outcome of the collision policy for this row.
	bool skipped = false;
	bool renamed_to_avoid_collision = false;
};

std::vector<convert_item_plan> plan_convert_outputs(df::folder_path write_folder,
                                                    const std::vector<convert_source>& sources,
                                                    std::string_view new_extension, collision_policy policy);

// Shared by every destination-writing operation: returns the first free " (n)" variant.
df::file_path next_free_destination(df::file_path destination);

// One vocabulary for every operation that writes to a destination.

// Review statement for a plan: names the policy and the number of rows it resolved.
std::string format_collision_summary(collision_policy policy, int count);

df::date_t adjusted_item_date(df::date_t created, df::date_t new_start, df::date_t original_start);
std::string_view adjust_date_source_name(const prop::item_metadata_const_ptr& md);

icon_index drive_icon(platform::drive_type d);
