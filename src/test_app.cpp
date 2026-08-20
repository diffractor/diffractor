// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Tests for application coordination (app*, view_* task views) -- settings persistence, and the rename, import, sync and convert planning that decides what an operation will do before it runs.

#include "pch.h"
#include "files.h"
#include "test.h"
#include "model_db.h"
#include "model_index.h"
#include "test_fixtures.h"
#include "test_runner.h"
#include "util_crash_files_db.h"
#include "app_util.h"
#include "app_settings.h"
#include "app_text.h"
#include "app_commands.h"
#include "app_search.h"
#include "ui_dialog.h"
#include "view_rename.h"
#include "view_tags.h"
#include "view_items.h"

// Rolling a release back is ordinary, and the settings store is shared with the build rolled back
// to. An order it cannot name falls through its switches to a default, so the user silently loses
// the order they chose - which is exactly the user this release's migration just moved onto one.
static void should_store_an_order_an_older_build_can_read()
{
	const auto plain = [](const group_by o) { return static_cast<uint32_t>(group_order_older_builds_understand(o)); };
	const auto ex = [](const group_by o) { return static_cast<uint32_t>(o); };

	// Every order an older build already knows is stored unchanged.
	for (const auto order : {group_by::file_type, group_by::size, group_by::date_created, group_by::date_modified,
		     group_by::camera, group_by::folder})
	{
		assert_equal(static_cast<int>(order), static_cast<int>(group_order_older_builds_understand(order)),
		             "a known order is stored as itself");
	}

	// The one it does not is stood in for by the order that meant the same thing to it.
	assert_equal(static_cast<int>(group_by::date_created),
	             static_cast<int>(group_order_older_builds_understand(group_by::date_original)),
	             "Original stands in as date created");
	assert_equal(static_cast<int>(sort_by::date_created),
	             static_cast<int>(sort_order_older_builds_understand(sort_by::date_original)),
	             "and the same for sorting");

	// A round trip through both keys returns the real order, stand-in or not.
	assert_equal(static_cast<int>(group_by::date_original),
	             static_cast<int>(resolve_stored_order<group_by>(plain(group_by::date_original),
	                                                            ex(group_by::date_original), true,
	                                                            group_order_older_builds_understand)),
	             "the true order survives its own stand-in");
	assert_equal(static_cast<int>(group_by::camera),
	             static_cast<int>(resolve_stored_order<group_by>(plain(group_by::camera), ex(group_by::camera), true,
	                                                            group_order_older_builds_understand)),
	             "and so does an order needing none");

	// An older build knows only the plain key. When it has written one, that is the newer choice and
	// the stale extended value must not overrule it.
	assert_equal(static_cast<int>(group_by::camera),
	             static_cast<int>(resolve_stored_order<group_by>(static_cast<uint32_t>(group_by::camera),
	                                                            ex(group_by::date_original), true,
	                                                            group_order_older_builds_understand)),
	             "a choice made in the older build wins");

	// Upgrading for the first time: only the plain key exists.
	assert_equal(static_cast<int>(group_by::date_created),
	             static_cast<int>(resolve_stored_order<group_by>(static_cast<uint32_t>(group_by::date_created), 0,
	                                                            false, group_order_older_builds_understand)),
	             "a store with no extended key answers with what it has");
}

static void should_persist_to_ini_file()
{
	const auto settings_folder = _temps.folder().combine("ini-settings");
	platform::create_folder(settings_folder);
	const auto settings = platform::create_ini_file_settings(settings_folder);

	// A folder without an INI is a new settings root.
	assert_equal(true, settings->root_created(), "root_created", "INI file settings");

	// Test uint32_t
	constexpr uint32_t test_uint32 = 12345;
	settings->write("test_section", "uint32_value", test_uint32);
	const auto existing_settings = platform::create_ini_file_settings(settings_folder);
	assert_equal(false, existing_settings->root_created(), "existing root", "INI file settings");
	uint32_t read_uint32 = 0;
	assert_equal(true, settings->read("test_section", "uint32_value", read_uint32), "read uint32",
	             "INI file settings");
	assert_equal(test_uint32, read_uint32, "uint32 value", "INI file settings");

	// Test uint64_t
	constexpr uint64_t test_uint64 = 0xFFFFFFFFFFFFull;
	settings->write("test_section", "uint64_value", test_uint64);
	uint64_t read_uint64 = 0;
	assert_equal(true, settings->read("test_section", "uint64_value", read_uint64), "read uint64",
	             "INI file settings");
	assert_equal(test_uint64, read_uint64, "uint64 value", "INI file settings");

	// Test string
	const auto test_string = "Hello, World! With special chars: äöü"s;
	settings->write("test_section", "string_value", test_string);
	std::string read_string;
	assert_equal(true, settings->read("test_section", "string_value", read_string), "read string",
	             "INI file settings");
	assert_equal(test_string, read_string, "string value", "INI file settings");

	// Test binary data (base64 encoded)
	const std::vector<uint8_t> test_binary = {0x00, 0x01, 0x02, 0xFF, 0xFE, 0xFD};
	settings->write("test_section", "binary_value", df::cspan{test_binary.data(), test_binary.size()});
	std::vector<uint8_t> read_buffer(test_binary.size());
	size_t read_len = read_buffer.size();
	assert_equal(true, settings->read("test_section", "binary_value", read_buffer.data(), read_len),
	             "read binary", "INI file settings");
	assert_equal(test_binary.size(), read_len, "binary length", "INI file settings");
	for (size_t i = 0; i < test_binary.size(); ++i)
	{
		assert_equal(static_cast<uint32_t>(test_binary[i]), static_cast<uint32_t>(read_buffer[i]), "binary byte",
		             "INI file settings");
	}
}

static void should_rename_with_substitutions()
{
	null_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);

	const df::file_path src_path(test_files_folder, "Test.jpg");
	const auto save_path_1 = _temps.next_path(".jpg");
	const auto save_path_2 = _temps.next_path(".jpg");

	platform::copy_file(src_path, save_path_1, false, false);

	const auto test_item = load_item(index, save_path_1, false);

	assert_equal(true, test_item->rename(index, save_path_2.file_name_without_extension()).success(), "can rename");
	assert_equal(save_path_2.name(), test_item->name(), "renamed");
	assert_equal(true, save_path_2.exists(), "renamed exists");

	// Verify item can be found at new path after rename
	const auto reloaded = load_item(index, save_path_2, false);
	assert_equal(save_path_2.name(), reloaded->name(), "index finds renamed item");
}

static void should_format_rename()
{
	assert_equal("photo-005", format_sequence("original", "photo-###", 5), "sequence format");
	assert_equal("photo-042", format_sequence("original", "photo-###", 42), "sequence zero pad");
	assert_equal("photo-1000", format_sequence("original", "photo-###", 1000), "sequence does not truncate");
	assert_equal("trip-0001", format_sequence("original", "trip-####", 1), "four digit sequence");
	assert_equal("riginal", format_sequence("original", "???????", 0), "question mark substitution");
	assert_equal("al-file", format_sequence("original", "??-file", 0), "mixed question mark substitution");
}

static void should_rename_name_token_without_extension()
{
	df::item_set items;
	const auto item = std::make_shared<df::item_element>(
		df::file_path(test_files_folder, "Test.jpg"), df::index_file_item{});
	items.add(item);

	const auto renames = calc_item_renames(items, "{name}-edited", 1, collision_policy::block_run);
	assert_equal(1ULL, static_cast<uint64_t>(renames.size()), "one rename planned");
	assert_equal("Test-edited", renames.front().new_name, "name token excludes extension");
}

static void should_reject_unusable_rename_targets()
{
	df::item_set items;
	items.add(std::make_shared<df::item_element>(df::file_path(test_files_folder, "Test.jpg"), df::index_file_item{}));

	const auto empty_template = calc_item_renames(items, "", 1, collision_policy::block_run);
	assert_equal(false, can_rename_items(empty_template), "an empty template names nothing");

#ifdef _WIN32
	// Windows drops a trailing space, so the file would not carry the name the preview shows, and a
	// device name is not a file at all. Neither is true of a filesystem that takes the name as given.
	const auto trailing_space = calc_item_renames(items, "photo ", 1, collision_policy::block_run);
	assert_equal("photo ", trailing_space.front().new_name, "the preview shows the template result");
	assert_equal(false, can_rename_items(trailing_space), "a trailing space is rejected");

	const auto device_name = calc_item_renames(items, "con", 1, collision_policy::block_run);
	assert_equal(false, can_rename_items(device_name), "a reserved device name is rejected");
#endif

	const auto usable = calc_item_renames(items, "photo", 1, collision_policy::block_run);
	assert_equal(true, can_rename_items(usable), "an ordinary name is still accepted");
}

static void should_reject_duplicate_rename_targets()
{
	df::item_set items;
	items.add(std::make_shared<df::item_element>(df::file_path(test_files_folder, "Test.jpg"), df::index_file_item{}));
	items.add(std::make_shared<df::item_element>(df::file_path(test_files_folder, "Small.jpg"), df::index_file_item{}));

	const auto renames = calc_item_renames(items, "same", 1, collision_policy::block_run);
	assert_equal(false, can_rename_items(renames), "duplicate rename targets rejected");
	assert_equal(false, renames.front().valid, "first duplicate target invalid");
	assert_equal(false, renames.back().valid, "second duplicate target invalid");
}

static std::vector<rename_source> make_rename_sources(const df::folder_path root,
                                                     const std::vector<std::string>& names)
{
	const df::blob contents = {1};
	std::vector<rename_source> sources;
	for (const auto& name : names)
	{
		const auto path = root.combine_file(name + ".jpg");
		df::blob_save_to_file(contents, path);
		rename_source source;
		source.source = path;
		source.original_name = name;
		sources.emplace_back(std::move(source));
	}
	return sources;
}

static void should_plan_the_default_rename_template()
{
	const auto root = _temps.next_folder("rename-default");
	const auto sources = make_rename_sources(root, {"alpha", "beta", "gamma"});

	const auto renames = calc_item_renames(sources, "Item ###", 1, collision_policy::block_run);
	assert_equal(3_z, renames.size(), "one row per item");
	assert_equal("Item 001", renames.front().new_name, "first row uses the start sequence");
	assert_equal(false, renames.front().collides, "a free destination does not collide");
	assert_equal(true, renames.front().valid, "a free destination is valid");
	assert_equal(true, can_rename_items(renames), "the default template can run");

	null_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);
	const auto with_sidecar = root.combine_file("with-sidecar.jpg");
	platform::copy_file(df::file_path(test_files_folder, "Test.jpg"), with_sidecar, false, false);
	platform::copy_file(df::file_path(test_files_folder, "IMG_0604.xmp"), with_sidecar.extension(".xmp"), false, false);

	df::item_set items;
	for (const auto& source : sources) items.add(load_item(index, source.source, false));
	items.add(load_item(index, with_sidecar, false));

	const auto item_renames = calc_item_renames(items, "Item ###", 1, collision_policy::block_run);
	assert_equal(4_z, item_renames.size(), "one row per indexed item");
	for (const auto& rename : item_renames)
		assert_equal(true, rename.valid, std::format("{} is valid", rename.new_name));
	assert_equal(true, can_rename_items(item_renames), "indexed items can run the default template");
}

static void should_rename_a_set_onto_names_it_is_vacating()
{
	const auto root = _temps.next_folder("rename-rotate");
	auto sources = make_rename_sources(root, {"Item 001", "Item 002", "Item 003"});
	std::ranges::reverse(sources);

	const auto renames = calc_item_renames(sources, "Item ###", 1, collision_policy::block_run);
	assert_equal(true, can_rename_items(renames), "a set may be renamed onto its own names");
	assert_equal(0, count_rename_collisions(renames, collision_policy::block_run), "no row collides");

	// A case-only rename frees nothing, so a second row cannot be cleared to take that name.
	const auto shadowed = make_rename_sources(root, {"only"});
	const auto case_only = calc_item_renames(shadowed, "ONLY", 1, collision_policy::block_run);
	assert_equal(true, can_rename_items(case_only), "a case-only rename is not a collision");
}

static void should_cascade_skipped_rename_rows()
{
	const auto root = _temps.next_folder("rename-cascade");
	// Item 001 is not part of the plan, so the row that wants it can only be skipped - which leaves
	// Item 002 where it is, so the row that was cleared to take Item 002 has to be skipped too.
	const auto occupied = make_rename_sources(root, {"Item 001"});
	const auto sources = make_rename_sources(root, {"Item 002", "beta"});

	const auto renames = calc_item_renames(sources, "Item ###", 1, collision_policy::skip);
	assert_equal(true, renames.front().skipped, "the blocked row is skipped");
	assert_equal(true, renames.back().skipped, "the row waiting on it is skipped too");
	assert_equal(false, can_rename_items(renames), "a plan that would write nothing cannot run");
}


static void should_group_rename_sidecar_collisions()
{
	const auto root = _temps.folder().combine(std::format("rename-sidecar-{}", platform::tick_count()));
	platform::create_folder(root);
	const auto source = root.combine_file("photo.jpg");
	const auto sidecar = root.combine_file("photo.xmp");
	const df::blob contents = {1};
	df::blob_save_to_file(contents, source);
	df::blob_save_to_file(contents, sidecar);
	df::blob_save_to_file(contents, root.combine_file("renamed.xmp"));

	rename_source item;
	item.source = source;
	item.original_name = "photo";
	item.sidecars.emplace_back(sidecar);

	const auto blocked = calc_item_renames(std::vector{item}, "renamed", 1, collision_policy::block_run);
	assert_equal(false, can_rename_items(blocked), "sidecar collision blocks whole rename");

	const auto renamed = calc_item_renames(std::vector{item}, "renamed", 1, collision_policy::auto_rename);
	assert_equal(true, can_rename_items(renamed), "sidecar collision can auto-rename whole group");
	assert_equal("renamed (2).jpg", renamed.front().destination.name(), "primary uses shared suffix");
	assert_equal("renamed (2).xmp", renamed.front().sidecars.front().second.name(), "sidecar uses shared suffix");
	platform::delete_items({}, {root}, false);
}

static void should_plan_unique_convert_outputs()
{
	std::vector<convert_source> sources;
	sources.emplace_back(df::file_path("c:\\one\\photo.jpg"), sizei{}, str::cached{});
	sources.emplace_back(df::file_path("c:\\two\\photo.png"), sizei{}, str::cached{});

	const auto plan = plan_convert_outputs(df::folder_path("c:\\destination"), sources, ".webp",
	                                       collision_policy::block_run);
	assert_equal(2_z, plan.size(), "all conversion sources planned");
	assert_equal("photo.webp", plan[0].destination.name(), "first conversion keeps basename");
	assert_equal("photo (2).webp", plan[1].destination.name(), "duplicate conversion basename is suffixed");
	assert_equal(false, plan[0].destination == plan[1].destination, "conversion outputs are unique");
}

static void should_adjust_item_dates_from_snapshot()
{
	constexpr df::date_t original_start(100);
	constexpr df::date_t new_start(500);
	assert_equal(df::date_t(510), adjusted_item_date(df::date_t(110), new_start, original_start),
	             "dated item preserves offset");
	assert_equal(new_start, adjusted_item_date({}, new_start, original_start),
	             "undated item uses new start");

	// The row says which tag it is about to shift, so a selection whose files are dated by different
	// things is visible before the run rather than after it.
	const auto with_shutter = std::make_shared<prop::item_metadata>();
	with_shutter->dates.add(prop::date_source::exif_original, df::date_t(2025, 8, 16, 18, 11, 56));
	with_shutter->dates.add(prop::date_source::file_modified, df::date_t(2026, 8, 19, 21, 30, 0));
	assert_equal("EXIF DateTimeOriginal", adjust_date_source_name(with_shutter), "the shutter answers first");

	const auto scanned = std::make_shared<prop::item_metadata>();
	scanned->dates.add(prop::date_source::xmp_create, df::date_t(2019, 5, 4, 9, 0, 0));
	assert_equal("XMP xmp:CreateDate", adjust_date_source_name(scanned), "the rung below it answers next");

	// Mirrors calc_media_created, which falls through to the filesystem when nothing else answers.
	assert_equal("File created", adjust_date_source_name(nullptr), "an unscanned file shifts its own stamp");
	assert_equal("File created", adjust_date_source_name(std::make_shared<prop::item_metadata>()),
	             "so does a file carrying no date at all");
}

#ifdef _WIN32
// The numbers are MAPI's own result codes, so there is nothing to classify where MAPI is absent.
static void should_classify_mapi_results()
{
	assert_equal(true, platform::classify_mapi_send_result(0) == platform::mapi_send_result::sent, "MAPI success");
	assert_equal(true, platform::classify_mapi_send_result(1) == platform::mapi_send_result::canceled,
	             "MAPI user abort");
	assert_equal(true, platform::classify_mapi_send_result(3) == platform::mapi_send_result::failed,
	             "MAPI login failure");
}
#endif

// Records what a run reported for each row so revalidation decisions can be asserted.
struct recording_status final : df::status_i
{
	std::vector<std::pair<std::string, item_status>> items;

	void start_item(std::string_view) override
	{
	}

	void end_item(const std::string_view name, const item_status status) override
	{
		items.emplace_back(name, status);
	}

	bool has_failures() const override
	{
		return std::ranges::any_of(items, [](const auto& i) { return i.second == item_status::fail; });
	}

	void abort(std::string_view) override
	{
	}

	void complete(std::string_view) override
	{
	}

	void show_errors() override
	{
	}

	void message(std::string_view, int64_t, int64_t) override
	{
	}

	void show_message(std::string_view) override
	{
	}

	bool is_canceled() const override { return false; }

	void wait_for_complete() const override
	{
	}

	item_status status_of(const std::string_view name) const
	{
		const auto found = std::ranges::find_if(items, [name](const auto& i) { return i.first == name; });
		return found == items.end() ? item_status::cancel : found->second;
	}
};

// Run holds every reviewed row to the file that was reviewed, one row at a time. A row whose file
// moved on is refused and reported; the rows that still match are still run.
static void should_revalidate_sync_rows()
{
	const auto root = _temps.next_folder("sync-revalidate");
	const auto local = root.combine("local");
	const auto remote = root.combine("remote");
	platform::create_folder(local);
	platform::create_folder(remote);

	write_test_file(local.combine_file("keep.txt"), "keep");
	write_test_file(local.combine_file("changed.txt"), "changed");
	write_test_file(local.combine_file("claimed.txt"), "claimed");

	df::index_roots roots;
	roots.folders.emplace(local);
	const auto analysis = sync_analysis(roots, remote, true, false, false, false, test_token);
	assert_equal(true, analysis.valid, "sync analysis is valid");
	assert_equal(3, static_cast<int>(count_sync_actions(analysis)), "three files to copy out");

	// The source grows after review, so copying it would send content nobody approved.
	write_test_file(local.combine_file("changed.txt"), "changed again");
	// A destination the review found free is claimed by something else before the run reaches it.
	write_test_file(remote.combine_file("claimed.txt"), "not yours");

	const auto status = std::make_shared<recording_status>();
	const auto run = sync_copy(status, analysis, test_token);

	assert_equal(true, status->status_of("keep.txt") == item_status::success, "unchanged row runs");
	assert_equal(true, status->status_of("changed.txt") == item_status::fail, "changed source is refused");
	assert_equal(true, status->status_of("claimed.txt") == item_status::fail, "claimed destination is refused");
	assert_equal(2, static_cast<int>(run.refused), "both refusals are reported so the run can say why");

	assert_equal(true, remote.combine_file("keep.txt").exists(), "unchanged row was copied");
	assert_equal(false, remote.combine_file("changed.txt").exists(), "refused row wrote nothing");
	assert_equal("not yours"s, read_test_file(remote.combine_file("claimed.txt")),
	             "claimed destination was not overwritten");
}

// A delete was reviewed against the file's content, so that is what it is held to at run time.
static void should_revalidate_sync_deletes()
{
	const auto root = _temps.next_folder("sync-delete-revalidate");
	const auto local = root.combine("local");
	const auto remote = root.combine("remote");
	platform::create_folder(local);
	platform::create_folder(remote);

	write_test_file(remote.combine_file("stale.txt"), "stale");
	write_test_file(remote.combine_file("touched.txt"), "touched");

	df::index_roots roots;
	roots.folders.emplace(local);
	const auto analysis = sync_analysis(roots, remote, false, false, false, true, test_token);
	assert_equal(true, analysis.valid, "sync analysis is valid");
	assert_equal(2, static_cast<int>(count_sync_actions(analysis, sync_action::delete_remote)),
	             "two remote files to delete");

	write_test_file(remote.combine_file("touched.txt"), "touched after review");

	const auto status = std::make_shared<recording_status>();
	sync_copy(status, analysis, test_token);

	assert_equal(true, status->status_of("stale.txt") == item_status::success, "unchanged file is deleted");
	assert_equal(true, status->status_of("touched.txt") == item_status::fail, "changed file is not deleted");
	assert_equal(false, remote.combine_file("stale.txt").exists(), "unchanged file is gone");
	assert_equal(true, remote.combine_file("touched.txt").exists(), "changed file survives");
}

static void should_detect_duplicate_import_destinations()
{
	const auto src1 = _temps.folder().combine("import-dup-1");
	const auto src2 = _temps.folder().combine("import-dup-2");
	const auto dest = _temps.folder().combine("import-dup-dest");
	platform::create_folder(src1);
	platform::create_folder(src2);
	platform::create_folder(dest);

	const auto source = df::file_path(test_files_folder, "Test.jpg");
	platform::copy_file(source, src1.combine_file("same.jpg"), false, false);
	platform::copy_file(source, src2.combine_file("same.jpg"), false, false);

	const auto make_item = [](const df::folder_path folder)
	{
		const auto path = folder.combine_file("same.jpg");
		const auto fi = platform::file_attributes(path);
		folder_scan_item item;
		item.folder = folder;
		item.item.name = path.name();
		item.item.file_modified = df::date_t(fi.modified);
		item.item.file_created = fi.created;
		item.item.ft = files::file_type_from_name(path);
		return item;
	};

	const std::vector<folder_scan_item> items{make_item(src1), make_item(src2)};
	const item_import_set no_previous;

	import_options options;
	options.dest_folder = dest;
	options.dest_structure = std::string(default_custom_folder_structure);
	options.collision = collision_policy::block_run;

	const auto blocked = import_analysis(items, options, no_previous, test_token);
	assert_equal(1, static_cast<int>(count_import_collisions(blocked)),
	             "second source claiming the same destination is a collision");
	assert_equal(1, static_cast<int>(count_imports(blocked)), "only the first source imports");

	options.collision = collision_policy::auto_rename;
	const auto renamed = import_analysis(items, options, no_previous, test_token);
	df::unique_paths destinations;

	for (const auto& folder : renamed)
	{
		for (const auto& i : folder.second) destinations.emplace(i.destination);
	}

	assert_equal(2, static_cast<int>(count_imports(renamed)), "auto-rename imports both sources");
	assert_equal(2, static_cast<int>(destinations.size()), "auto-rename gives each source its own destination");
}

// Import holds each reviewed row to the file that was reviewed, and lets the file system prove a
// destination is free rather than asking and then writing.
static void should_revalidate_import_rows(shared_test_context& stc)
{
	const auto root = _temps.next_folder("import-revalidate");
	const auto src = root.combine("source");
	const auto dest = root.combine("dest");
	platform::create_folder(src);
	platform::create_folder(dest);

	write_test_file(src.combine_file("keep.txt"), "keep");
	write_test_file(src.combine_file("changed.txt"), "changed");
	write_test_file(src.combine_file("claimed.txt"), "claimed");

	const auto make_item = [&src](const std::string_view name)
	{
		const auto path = src.combine_file(name);
		const auto fi = platform::file_attributes(path);
		folder_scan_item item;
		item.folder = src;
		item.item.name = path.name();
		item.item.file_modified = df::date_t(fi.modified);
		item.item.file_created = fi.created;
		item.item.size = df::file_size(fi.size);
		item.item.ft = files::file_type_from_name(path);
		return item;
	};

	const std::vector<folder_scan_item> items{
		make_item("keep.txt"), make_item("changed.txt"), make_item("claimed.txt")
	};

	import_options options;
	options.dest_folder = dest;
	options.dest_structure = {};
	options.collision = collision_policy::skip;

	const auto analysis = import_analysis(items, options, {}, test_token);
	assert_equal(3, static_cast<int>(count_imports(analysis)), "three files to import");

	write_test_file(src.combine_file("changed.txt"), "changed after review");
	write_test_file(dest.combine_file("claimed.txt"), "not yours");

	const auto status = std::make_shared<recording_status>();
	const auto run = import_copy(stc.empty_index, status, analysis, options, test_token);

	assert_equal(true, status->status_of("keep.txt") == item_status::success, "unchanged row imports");
	assert_equal(true, status->status_of("changed.txt") == item_status::fail, "changed source is refused");
	assert_equal(true, status->status_of("claimed.txt") == item_status::fail, "claimed destination is refused");
	assert_equal(2, static_cast<int>(run.refused), "both refusals are reported so the run can say why");

	assert_equal(true, dest.combine_file("keep.txt").exists(), "unchanged row was imported");
	assert_equal(false, dest.combine_file("changed.txt").exists(), "refused row wrote nothing");
	assert_equal("not yours"s, read_test_file(dest.combine_file("claimed.txt")),
	             "claimed destination was not overwritten");
}

// Replace is the one policy that writes over a file instead of proving the name is free, so it is
// the one that has to prove the file is still the one that was reviewed.
static void should_revalidate_replaced_import_destinations(shared_test_context& stc)
{
	const auto root = _temps.next_folder("import-replace-revalidate");
	const auto src = root.combine("source");
	const auto dest = root.combine("dest");
	platform::create_folder(src);
	platform::create_folder(dest);

	write_test_file(src.combine_file("stable.txt"), "new stable");
	write_test_file(src.combine_file("racing.txt"), "new racing");
	write_test_file(dest.combine_file("stable.txt"), "old stable");
	write_test_file(dest.combine_file("racing.txt"), "old racing");

	const auto make_item = [&src](const std::string_view name)
	{
		const auto path = src.combine_file(name);
		const auto fi = platform::file_attributes(path);
		folder_scan_item item;
		item.folder = src;
		item.item.name = path.name();
		item.item.file_modified = df::date_t(fi.modified);
		item.item.file_created = fi.created;
		item.item.size = df::file_size(fi.size);
		item.item.ft = files::file_type_from_name(path);
		return item;
	};

	const std::vector<folder_scan_item> items{make_item("stable.txt"), make_item("racing.txt")};

	import_options options;
	options.dest_folder = dest;
	options.dest_structure = {};
	options.collision = collision_policy::replace;

	const auto analysis = import_analysis(items, options, {}, test_token);
	assert_equal(2, static_cast<int>(count_imports(analysis)), "replace imports over both destinations");

	write_test_file(dest.combine_file("racing.txt"), "edited since review");

	const auto status = std::make_shared<recording_status>();
	import_copy(stc.empty_index, status, analysis, options, test_token);

	assert_equal(true, status->status_of("stable.txt") == item_status::success, "reviewed destination is replaced");
	assert_equal(true, status->status_of("racing.txt") == item_status::fail, "changed destination is refused");

	assert_equal("new stable"s, read_test_file(dest.combine_file("stable.txt")),
	             "reviewed destination took the new file");
	assert_equal("edited since review"s, read_test_file(dest.combine_file("racing.txt")),
	             "changed destination was left alone");
}

static void should_reject_missing_sync_folder()
{
	df::index_roots roots;
	roots.folders.emplace(_temps.folder().combine("missing-local"));

	const auto result = sync_analysis(roots, _temps.folder(), true, false, false, true, test_token);
	assert_equal(false, result.valid, "missing local folder invalidates sync analysis");
	assert_equal(true, result.empty(), "invalid sync analysis has no actions");
	assert_equal(true, result.reason != sync_invalid_reason::none, "records why the analysis is invalid");
	assert_equal(false, sync_invalid_message(result) == tt.error_cannot_continue.sv(),
	             "a missing folder is not reported as an internal fault");
}

static void should_reject_overlapping_sync_folders()
{
	const auto local = _temps.folder().combine("sync-overlap-local");
	platform::create_folder(local);
	df::index_roots roots;
	roots.folders.emplace(local);

	const auto result = sync_analysis(roots, local.combine("remote"), true, false, false, false, test_token);
	assert_equal(false, result.valid, "nested remote folder invalidates sync analysis");
	assert_equal(true, result.reason == sync_invalid_reason::overlapping_paths, "reports overlapping folders");
	assert_equal(false, sync_invalid_message(result) == tt.error_cannot_continue.sv(),
	             "overlapping folders are not reported as an internal fault");
}

static void should_reject_ambiguous_sync_roots()
{
	const auto local1 = _temps.folder().combine("sync-root-1");
	const auto local2 = _temps.folder().combine("sync-root-2");
	const auto remote = _temps.folder().combine("sync-remote");
	platform::create_folder(local1);
	platform::create_folder(local2);
	platform::create_folder(remote);
	const auto source = df::file_path(test_files_folder, "Test.jpg");
	platform::copy_file(source, local1.combine_file("same.jpg"), false, false);
	platform::copy_file(source, local2.combine_file("same.jpg"), false, false);

	df::index_roots roots;
	roots.folders.emplace(local1);
	roots.folders.emplace(local2);
	const auto result = sync_analysis(roots, remote, true, false, false, false, test_token);
	assert_equal(false, result.valid, "duplicate relative paths across roots invalidate sync analysis");
	assert_equal(true, result.empty(), "ambiguous sync analysis has no actions");
	assert_equal(true, result.reason == sync_invalid_reason::ambiguous_local_root, "reports ambiguous roots");
	assert_equal(false, sync_invalid_message(result) == tt.error_cannot_continue.sv(),
	             "ambiguous roots are not reported as an internal fault");
}

static void should_ignore_unclaimed_remote_files()
{
	// A collection with more than one root cannot name a local destination for a remote-only file in
	// an unknown folder. That must only fail the run when the file would actually be copied local.
	const auto local1 = _temps.folder().combine("unclaimed-root-1");
	const auto local2 = _temps.folder().combine("unclaimed-root-2");
	const auto remote = _temps.folder().combine("unclaimed-remote");
	platform::create_folder(local1);
	platform::create_folder(local2);
	platform::create_folder(remote);
	const auto source = df::file_path(test_files_folder, "Test.jpg");
	platform::copy_file(source, remote.combine_file("remote-only.jpg"), false, false);

	df::index_roots roots;
	roots.folders.emplace(local1);
	roots.folders.emplace(local2);

	const auto ignored = sync_analysis(roots, remote, true, false, false, false, test_token);
	assert_equal(true, ignored.valid, "remote-only file does not invalidate a multi-root sync analysis");
	assert_equal(1, static_cast<int>(ignored.size()), "the remote-only file is still reported");

	for (const auto& folder : ignored)
	{
		for (const auto& file : folder.second)
		{
			assert_equal(true, file.second.action == sync_action::none, "the remote-only file is ignored");
		}
	}

	const auto copied = sync_analysis(roots, remote, false, true, false, false, test_token);
	assert_equal(false, copied.valid, "copying a remote-only file needs an unambiguous local root");
	assert_equal(true, copied.reason == sync_invalid_reason::ambiguous_local_root, "reports ambiguous roots");
}

static void should_select_sync_actions()
{
	assert_equal(true, calc_sync_action(true, false, 10, 0, 100, 0, false, false, true, false) ==
	             sync_action::delete_local, "delete local is independent");
	assert_equal(true, calc_sync_action(false, true, 0, 10, 0, 100, false, false, false, true) ==
	             sync_action::delete_remote, "delete remote is independent");
	assert_equal(true, calc_sync_action(true, false, 10, 0, 100, 0, true, false, true, false) ==
	             sync_action::copy_remote, "copy local to remote takes precedence over delete local");
	assert_equal(true, calc_sync_action(false, true, 0, 10, 0, 100, false, true, false, true) ==
	             sync_action::copy_local, "copy remote to local takes precedence over delete remote");
	assert_equal(true, calc_sync_action(true, true, 20, 10, 100, 100, true, true, true, true) ==
	             sync_action::copy_remote, "newer local file copies to remote");
	assert_equal(true, calc_sync_action(true, true, 10, 20, 100, 100, true, true, true, true) ==
	             sync_action::copy_local, "newer remote file copies to local");
	assert_equal(true, calc_sync_action(true, true, 10, 10, 100, 100, true, true, true, true) ==
	             sync_action::none, "matching timestamps need no action");
	assert_equal(true, calc_sync_action(true, true, 10, 10, 101, 100, true, false, false, false) ==
	             sync_action::copy_remote, "one-way sync copies unequal sizes with matching timestamps");
	assert_equal(true, calc_sync_action(true, true, 10, 10, 100, 101, false, true, false, false) ==
	             sync_action::copy_local, "reverse one-way sync copies unequal sizes with matching timestamps");
}

static void should_offer_every_matching_tool()
{
	const auto make_tool = [](const std::string_view exe, const std::string_view group)
	{
		auto tool = std::make_shared<file_tool>();
		tool->exe = str::cache(exe);
		tool->text = str::cache(exe);
		tool->group = str::cache(group);
		tool->invoke_text = str::cache("{exe-path} {item-path}");
		return tool;
	};

	const auto first = make_tool("first", "photo");
	const auto second = make_tool("second", "photo");
	const auto player = make_tool("player", "video");

	file_tools_result result;
	result.by_extension[str::cache("jpg")] = {first, second};
	result.by_group[str::cache("photo")] = {second};
	result.by_group[str::cache("video")] = {player};
	apply_tools(std::move(result));
	// The tool table is process-global; a failing assertion below must not leave it replaced.
	const df::scope_exit restore_tools([] { apply_tools({}); });

	const auto jpeg = files::file_type_from_name("test.jpg");
	const auto tools = jpeg->all_tools();
	assert_equal(2_z, tools.size(), "every tool declaring the extension is offered");
	assert_equal_strict("first", tools[0]->text.sv(), "config order is preserved");
	assert_equal_strict("second", tools[1]->text.sv(), "a tool matched by extension and group is listed once");

	const auto mp4 = files::file_type_from_name("test.mp4");
	assert_equal(1_z, mp4->all_tools().size(), "group membership offers a tool with no extension match");

	assert_equal(false, first->invoke(df::file_path{}), "an unresolved executable is never launched");

	apply_tools({});
	assert_equal(0_z, jpeg->all_tools().size(), "reapplying replaces rather than accumulates");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Issue #175 - Sidebar history chart doesn't show the whole collection
// The date histogram must record files older than 10 years so the (now
// user-configurable) chart can display the full collection span.
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_record_history_beyond_ten_years()
{
	const auto this_year = platform::now().year();
	const location_cache locations;
	index_histograms h;

	const auto record_created = [&](const int years_ago, const int month)
	{
		df::index_file_item f;
		f.ft = files::file_type_from_name("test.jpg");
		f.file_created = df::date_t(this_year - years_ago, month, 1);
		f.file_modified = df::date_t(this_year - years_ago, month, 1);
		h.record(locations, f);
	};

	record_created(0, 3); // this year, March
	record_created(9, 6); // within the old 10-year window
	record_created(15, 6); // BEYOND the old 10-year cap - previously dropped
	record_created(40, 1); // decades back, still within max_history_years

	assert_equal(1, h._dates.dates[0 * 12 + (3 - 1)].created, "this year recorded");
	assert_equal(1, h._dates.dates[9 * 12 + (6 - 1)].created, "9-year-old recorded");
	assert_equal(1, h._dates.dates[15 * 12 + (6 - 1)].created, "15-year-old recorded (beyond old cap)");
	assert_equal(1, h._dates.dates[40 * 12 + (1 - 1)].created, "40-year-old recorded");

	// The storage capacity must exceed the old hard-coded 10-year limit.
	assert_equal(true, df::max_history_years > 10, "history capacity beyond 10 years");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// The timeline is a navigation surface: clicking a month runs a search. A chart that buckets an
// item under one date while the search answers with another is "click to open finds nothing" at the
// place a user is most likely to click. Both halves resolve through prop::item_metadata::created(),
// and only the round trip proves they still agree - neither half can be checked alone.
///////////////////////////////////////////////////////////////////////////////////////////////////
static void should_answer_a_timeline_month_with_its_own_items(shared_test_context& stc)
{
	stc.lazy_load_index();
	stc.test_index.update_summary();

	const auto histograms = stc.test_index.histograms();
	const auto year = histograms->_year;

	auto months_checked = 0;

	for (auto index = 0u; index < histograms->_dates.dates.size(); ++index)
	{
		if (histograms->_dates.dates[index].created == 0) continue;

		// The cell draws this item, so this is the item a user is clicking through to.
		const auto shown = histograms->_dates.representative_paths[index];
		if (shown.is_empty()) continue;

		const auto cell_year = year - static_cast<int>(index) / 12;
		const auto cell_month = static_cast<int>(index) % 12 + 1;

		// Day zero is the whole month, which is what the chart cell stands for. The chart buckets on
		// the capture-first ladder, so the click has to ask for that key rather than the Created one.
		const auto search = df::search_t().day(0, cell_month, cell_year, df::date_parts_prop::original);

		auto found = false;
		auto listed = 0;
		auto cb = [&](const index_state::query_item_results& items, bool)
		{
			listed += static_cast<int>(items.size());
			for (const auto& i : items) found = found || i.path == shown;
		};
		stc.test_index.query_items(search, cb, test_token);

		assert_equal(true, listed > 0, std::format("{}-{:02} lists something", cell_year, cell_month));
		assert_equal(true, found,
		             std::format("{}-{:02} lists the item its cell draws - {}", cell_year, cell_month, shown.name()));
		++months_checked;
	}

	assert_equal(true, months_checked > 0, "the fixture collection populates at least one month");
}

static void should_calculate_history_span_from_start_year()
{
	constexpr auto this_year = 2026;

	const auto typed = df::history_range_from_start_year(2010, this_year);
	assert_equal(2010, typed.first_year, "a typed start year is taken as given");
	assert_equal(this_year, typed.last_year, "and the range still ends today");

	// A start year inside the window would leave the calendar with rows the navigator cannot reach.
	const auto recent = df::history_range_from_start_year(this_year, this_year);
	assert_equal(df::history_window_years, recent.year_count(), "a start year too recent still leaves a window");

	const auto ancient = df::history_range_from_start_year(1800, this_year);
	assert_equal(df::max_history_years, ancient.year_count(), "an old start year clamps to what is stored");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// The navigator's range is worked out from what was indexed, and photographs carry wrong dates. A
// scanner that stamped 1900 on four negatives must not set the scale for twenty thousand photos,
// and a camera whose clock reset must not add a decade nobody photographed.
///////////////////////////////////////////////////////////////////////////////////////////////////
static void should_choose_a_history_range_that_survives_wrong_dates()
{
	constexpr auto this_year = 2026;

	const auto with = [](const std::vector<std::pair<int, int>>& years_and_counts)
	{
		df::date_histogram h;

		for (const auto& [years_ago, count] : years_and_counts)
		{
			h.dates[static_cast<size_t>(years_ago) * 12 + 5].created += count;
		}

		return df::history_auto_range(h, this_year);
	};

	{
		const auto empty = df::history_auto_range({}, this_year);
		assert_equal(this_year, empty.last_year, "an empty collection still ends today");
		assert_equal(df::history_window_years, empty.year_count(), "and offers exactly one window");
	}

	{
		// Fifteen real years, and four negatives a scanner stamped 1900.
		std::vector<std::pair<int, int>> years;
		for (auto y = 0; y < 15; ++y) years.emplace_back(y, 800);
		years.emplace_back(99, 4);

		const auto range = with(years);
		assert_equal(this_year - 14, range.first_year, "the range stops at the real history");
		assert_equal(this_year, range.last_year, "and ends today");
	}

	{
		// A camera whose clock reset: an island of a few dozen behind a decade of silence.
		std::vector<std::pair<int, int>> years;
		for (auto y = 0; y < 10; ++y) years.emplace_back(y, 1000);
		years.emplace_back(40, 30);
		years.emplace_back(41, 30);

		const auto range = with(years);
		assert_equal(this_year - 9, range.first_year, "an island behind a long gap is a date bug");
	}

	{
		// Scanned family photographs are a real decade, however long ago: they are too much of the
		// collection to be a clock that reset, and cutting them would hide them from the navigator.
		std::vector<std::pair<int, int>> years;
		for (auto y = 0; y < 10; ++y) years.emplace_back(y, 1000);
		for (auto y = 40; y < 50; ++y) years.emplace_back(y, 400);

		const auto range = with(years);
		assert_equal(this_year - 49, range.first_year, "a substantial older decade is kept");
	}

	{
		// One busy year is still eight years of navigator, or there is nothing to navigate.
		const auto range = with({{0, 500}});
		assert_equal(df::history_window_years, range.year_count(), "the range is never shorter than a window");
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// The media-type filter (photo/video/audio) survives a save/restore cycle. The
// filter is serialized to a stable
// comma-separated string of group names and rebuilt from it on load.
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_persist_media_filter()
{
	filter_t filter;
	filter.toggle(file_group::photo);
	filter.toggle(file_group::video);

	// Round-trip through the same serialization used to persist the setting.
	const auto restored = media_filter_from_string(media_filter_to_string(filter));

	assert_equal(true, restored.has_group(file_group::photo), "photo filter restored");
	assert_equal(true, restored.has_group(file_group::video), "video filter restored");
	assert_equal(false, restored.has_group(file_group::audio), "audio filter stays off");

	// An empty filter round-trips to an empty filter (no groups selected).
	const auto empty = media_filter_from_string(media_filter_to_string(filter_t{}));
	assert_equal(true, empty.is_empty(), "empty filter round-trips to empty");

	// Unknown group names are ignored rather than producing bogus groups.
	const auto bogus = media_filter_from_string("photo,not_a_group");
	assert_equal(true, bogus.has_group(file_group::photo), "known group parsed");
	assert_equal(1_z, bogus.groups().size(), "unknown group ignored");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Issue #228 - the tag view's action field appeared blank.
// The field is the action to apply ("what to add or remove"), not a view of the current tags,
// so it starts empty by design. What it must get right is the round-trip: a '-' prefix means
// remove, a tag containing white space is quoted, a repeated tag keeps only the last modifier,
// and matching is case-insensitive so "Beach" and "beach" are one action.
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_round_trip_tag_actions()
{
	// The field is an action list, so no text means no actions - not "all current tags".
	assert_equal(0_z, parse_tag_actions({}).size(), "an empty field is no actions");
	assert_equal(0_z, parse_tag_actions("   ").size(), "white space alone is no actions");
	assert_equal_strict("", serialize_tag_actions({}), "no actions serializes to an empty field");

	const auto actions = parse_tag_actions("beach -winter \"new york\" -\"old town\"");
	assert_equal(4_z, actions.size(), "action count");

	assert_equal_strict("beach", actions[0].first, "first tag");
	assert_equal(true, actions[0].second, "unprefixed tag is an add");
	assert_equal_strict("winter", actions[1].first, "second tag");
	assert_equal(false, actions[1].second, "'-' prefixed tag is a remove");
	assert_equal_strict("new york", actions[2].first, "quoted tag keeps its space");
	assert_equal(true, actions[2].second, "quoted tag is an add");
	assert_equal_strict("old town", actions[3].first, "quoted removal keeps its space");
	assert_equal(false, actions[3].second, "quoted '-' prefixed tag is a remove");

	// Serializing re-quotes and re-prefixes, so the field survives an edit/apply/edit cycle.
	const auto text = serialize_tag_actions(actions);
	const auto reparsed = parse_tag_actions(text);
	assert_equal(actions.size(), reparsed.size(), "round-trip action count");

	for (auto i = 0_z; i < actions.size(); ++i)
	{
		assert_equal_strict(actions[i].first, reparsed[i].first, "round-trip tag");
		assert_equal(actions[i].second, reparsed[i].second, "round-trip modifier");
	}

	// A tag typed twice is one action, matched case-insensitively, and the last modifier wins.
	const auto deduped = parse_tag_actions("Beach -beach");
	assert_equal(1_z, deduped.size(), "a repeated tag is one action");
	assert_equal_strict("Beach", deduped[0].first, "the first spelling is kept");
	assert_equal(false, deduped[0].second, "the last modifier wins");

	const auto readded = parse_tag_actions("-beach Beach");
	assert_equal(1_z, readded.size(), "a repeated tag is one action either way round");
	assert_equal(true, readded[0].second, "the last modifier wins the other way round");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Issue #229 - switching a group between thumbnails and details did not stick: any search that
// recreated the groups reverted them. The choice is held per media type as a bitmask over
// group_key_type in setting.detail_items, which is what both the group toggle and the group
// construction read, and what the settings store persists.
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_persist_detail_display_per_media_type()
{
	const auto saved = setting.detail_items;
	const df::scope_exit restore([saved] { setting.detail_items = saved; });

	setting.detail_items = 0;

	setting.set_detail_display(group_key_type::photo, true);
	assert_equal(true, setting.is_detail_display(group_key_type::photo), "photo set to detail");

	// Each media type is an independent bit: setting one must not change the others.
	assert_equal(false, setting.is_detail_display(group_key_type::video), "video unaffected");
	assert_equal(false, setting.is_detail_display(group_key_type::audio), "audio unaffected");
	assert_equal(false, setting.is_detail_display(group_key_type::folder), "folder unaffected");

	setting.set_detail_display(group_key_type::video, true);
	setting.set_detail_display(group_key_type::photo, false);
	assert_equal(false, setting.is_detail_display(group_key_type::photo), "photo back to thumbnails");
	assert_equal(true, setting.is_detail_display(group_key_type::video), "video still detail");

	// Clearing a type that was never set must not disturb the ones that were.
	setting.set_detail_display(group_key_type::audio, false);
	assert_equal(true, setting.is_detail_display(group_key_type::video), "video survives an unrelated clear");

	// Every media type has its own bit, so no two types can alias each other.
	constexpr group_key_type all[] = {
		group_key_type::folder, group_key_type::photo, group_key_type::video, group_key_type::audio,
		group_key_type::grouped_value, group_key_type::grouped_no_value, group_key_type::archive,
		group_key_type::retro, group_key_type::other,
	};

	for (const auto type : all)
	{
		setting.detail_items = 0;
		setting.set_detail_display(type, true);
		assert_equal(true, setting.is_detail_display(type), "media type set to detail");

		auto others_unset = true;

		for (const auto other : all)
		{
			if (other != type && setting.is_detail_display(other)) others_unset = false;
		}

		assert_equal(true, others_unset, "no other media type shares this bit");
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Issue #135 - Rating/labeling via NumPad not working
// The rating (0-5) and label (6-9) shortcuts are bound to the top-row digit keys
// '0'..'9'. With NumLock on, the numeric keypad sends the distinct VK_NUMPAD0..9
// virtual-key codes (0x60..0x69), which never matched the '0'..'9' bindings, so the
// keypad could not rate or label. keys::normalize_numpad maps the keypad digits onto
// the equivalent top-row digit while leaving every other key untouched.
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_map_numpad_digits_to_rating_keys()
{
	// Every keypad digit VK_NUMPAD0..VK_NUMPAD9 maps to the matching top-row digit,
	// so the rating (0-5) and label (6-9) accelerators fire from the keypad.
	for (char32_t d = 0; d <= 9; ++d)
	{
		const auto numpad_key = keys::numpad0 + d;
		assert_equal(static_cast<char32_t>(U'0' + d), keys::normalize_numpad(numpad_key),
		             "keypad digit maps to top-row digit");
	}

	// Top-row digits are already correct and must pass through unchanged.
	for (char32_t c = U'0'; c <= U'9'; ++c)
	{
		assert_equal(c, keys::normalize_numpad(c), "top-row digit unchanged");
	}

	// Non-digit keys (letters, and the keypad codes just outside the digit range) are
	// left untouched so unrelated shortcuts are not disturbed.
	assert_equal(U'A', keys::normalize_numpad(U'A'), "letter unchanged");
	assert_equal(keys::numpad0 - 1, keys::normalize_numpad(keys::numpad0 - 1), "below range unchanged");
	assert_equal(keys::numpad9 + 1, keys::normalize_numpad(keys::numpad9 + 1), "above range unchanged");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Issue #227 - Removed default sidebar tags reappear after restart
// The default favorite (sidebar) tags must be seeded only on the very first run. Because
// favorite_tags is persisted as one string, an empty saved value is otherwise treated as
// "never configured" and the defaults are re-injected on every launch, resurrecting the
// tags the user deliberately removed. The persisted favorite_tags_initialized flag records
// that favorites have been configured; after that an empty list is respected.
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_only_seed_favorite_tags_on_first_run()
{
	// First run: nothing configured yet (flag unset) and the list is empty -> seed defaults.
	assert_equal(true, should_seed_default_favorite_tags(false, true, true), "first run seeds defaults");

	// After the user configured and then cleared favorites (flag set, list empty) the empty
	// list must be respected -- the removed defaults must NOT come back.
	assert_equal(false, should_seed_default_favorite_tags(true, true, false), "cleared list stays cleared");

	// A configured, non-empty list is never overwritten.
	assert_equal(false, should_seed_default_favorite_tags(true, false, false), "configured list not seeded");

	// Upgrade case: an existing user has favorites (non-empty) but no flag yet -> do NOT
	// re-seed over their existing tags.
	assert_equal(false, should_seed_default_favorite_tags(false, false, false), "existing tags preserved on upgrade");

	// Upgrade from a version without the initialization flag: an existing empty saved list
	// means the user removed every favorite, not that this is a first run.
	assert_equal(false, should_seed_default_favorite_tags(false, true, false), "legacy empty list stays empty");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// 1.27.0 - the app crashed during startup with the sidebar hidden, before the user could act.
// A start that never reaches idle is counted; once enough consecutive starts have failed that
// way the next one reverts presentation instead of repeating the crash. The threshold has to
// tolerate a single unsettled start, which is also what a kill or power loss during launch
// looks like, without needing a third crash before it helps.
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_start_safe_only_after_repeated_failures()
{
	assert_equal(false, should_start_safe(0), "a clean history starts normally");
	assert_equal(false, should_start_safe(1), "one unsettled start could be a kill or power loss");
	assert_equal(true, should_start_safe(2), "two in a row is a reproducible startup crash");
	assert_equal(true, should_start_safe(7), "and it stays safe until one start settles");

	// The reset covers what the window puts on screen and turns the graphics path off, and must
	// leave everything the user cannot re-reach from a working window alone.
	settings_t s;
	s.show_sidebar = false;
	s.large_font = true;
	s.show_debug_info = true;
	s.sidebar.show_world_map = false;
	s.sidebar.width = 900;
	s.use_gpu = true;
	s.use_d3d11va = true;
	s.write_folder = "c:\\keep-me";
	s.favorite_tags = "keep";
	s.language = "de";

	s.reset_presentation();

	assert_equal(true, s.show_sidebar, "the sidebar comes back");
	assert_equal(false, s.large_font, "font scale returns to default");
	assert_equal(false, s.show_debug_info, "debug overlay off");
	assert_equal(true, s.sidebar.show_world_map, "sidebar contents return to default");
	assert_equal(0, s.sidebar.width, "sidebar width returns to auto");
	assert_equal(false, s.use_gpu, "hardware acceleration off, not merely defaulted");
	assert_equal(false, s.use_d3d11va, "hardware video decode off");
	assert_equal(false, s.use_yuv, "hardware yuv textures off");
	assert_equal("c:\\keep-me"s, s.write_folder, "user paths survive");
	assert_equal("keep"s, s.favorite_tags, "favorite tags survive");
	assert_equal("de"s, s.language, "language survives");
}

// Selecting several files in Explorer starts one process each, and they all reach the shared count
// before any of them can settle. Concurrency is not a crash history.
static void should_not_count_concurrent_starts()
{
	const auto owner = decide_startup(true, 1);
	assert_equal(true, owner.record, "the launch holding the startup scope records its attempt");
	assert_equal(2u, owner.next_unsettled, "and raises the count");
	assert_equal(false, owner.safe_start, "one unsettled start is not yet a crash loop");

	assert_equal(true, decide_startup(true, 2).safe_start, "two unsettled starts still escalate");

	const auto concurrent = decide_startup(false, 5);
	assert_equal(false, concurrent.record, "a concurrent launch must not raise the count");
	assert_equal(5u, concurrent.next_unsettled, "so the count is left where it was");
	assert_equal(false, concurrent.safe_start, "and it must not throw away the user's settings");
}

static void should_restore_history_selection()
{
	history_state history;
	const auto search_a = df::search_t::parse("one");
	const auto search_b = df::search_t::parse("two");
	const auto unresolved_area = df::search_t::parse("area:Munich");
	auto resolved_area = unresolved_area;
	resolved_area.resolve_area(map_location_area{.name = "Munich", .cell = {136, 29}, .cell_span = 1});
	const auto selected_a = df::file_path("c:\\one.jpg");
	const auto selected_b = df::file_path("c:\\two.jpg");

	history.history_add(search_a, {});
	history.history_add(search_b, df::paths{{selected_a}, {}});

	history_state::history_entry entry;
	assert_equal(true, history.move_history_pos(-1, df::paths{{selected_b}, {}}, entry), "browse back succeeds");
	assert_equal(true, entry.search == search_a, "back restores first search");
	assert_equal(true, entry.selected.files.size() == 1 && entry.selected.files.front() == selected_a,
	             "back restores first selection");
	history.history_add(entry.search, df::paths{{selected_b}, {}});

	assert_equal(true, history.move_history_pos(1, entry.selected, entry), "browse forward succeeds");
	assert_equal(true, entry.search == search_b, "forward restores second search");
	assert_equal(true, entry.selected.files.size() == 1 && entry.selected.files.front() == selected_b,
	             "forward restores second selection");
	history.history_add(entry.search, df::paths{{selected_a}, {}});

	assert_equal(true, history.move_history_pos(-1, entry.selected, entry), "second browse back succeeds");
	assert_equal(true, entry.selected.files.size() == 1 && entry.selected.files.front() == selected_a,
	             "opening history entries does not overwrite their selection");

	history_state area_history;
	area_history.history_add(unresolved_area, {});
	area_history.replace_current_search(unresolved_area, resolved_area);
	assert_equal(true, area_history._history.back().search == resolved_area,
	             "deferred area resolution updates the current history entry");
	assert_equal(1_z, area_history._history.size(), "deferred area resolution does not add a history entry");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Selection and command enablement
///////////////////////////////////////////////////////////////////////////////////////////////////

static df::item_element_ptr find_item_n(const view_state& s, const int n)
{
	auto count = 0;

	for (const auto& b : s.groups())
	{
		for (const auto& i : b->items())
		{
			if (count == n) return i;
			count++;
		}
	}

	return nullptr;
}

static void should_select_items()
{
	null_state_strategy ss;
	null_async_strategy as;
	const view_host_base_ptr view;

	const location_cache locations;
	index_state index(as, locations);
	view_state s(ss, as, index, make_test_player());
	s.view_mode(view_type::items);
	s.open(view, df::search_t().add_selector(test_files_folder), {});
	s.update_item_groups();
	s.update_selection();

	assert_equal(expected_cached_item_count + 1_z, s.search_items().file_paths(false).size(),
	             "items loaded into state");
	assert_equal(expected_cached_item_count + 2_z, s.search_items().file_paths().size(),
	             "items loaded into state with xmp");
	assert_equal(4_z, s.search_items().folder_paths().size(), "folders loaded into state");

	assert_equal(0_z, s.selected_count(), "invalid selection");

	s.select(view, find_item_n(s, 3), false, false, false);
	s.update_selection();
	assert_equal(1_z, s.selected_count(), "invalid selection");

	s.select(view, find_item_n(s, 1), false, false, false);
	s.update_selection();
	assert_equal(1_z, s.selected_count(), "invalid selection");

	s.select(view, find_item_n(s, 8), true, false, false);
	s.update_selection();
	assert_equal(2_z, s.selected_count(), "invalid control selection");

	s.select(view, find_item_n(s, 1), false, false, false);
	s.update_selection();
	assert_equal(1_z, s.selected_count(), "invalid selection");

	s.select(view, find_item_n(s, 4), false, true, false);
	s.update_selection();
	assert_equal(4_z, s.selected_count(), "invalid selection");

	s.select(view, find_item_n(s, 2), true, false, false);
	s.update_selection();
	assert_equal(3_z, s.selected_count(), "invalid selection");

	s.select(view, find_item_n(s, 6), true, false, false);
	s.update_selection();
	assert_equal(4_z, s.selected_count(), "invalid selection");

	const auto outside = find_item_n(s, 0);
	const auto inside = find_item_n(s, 1);
	s.select(view, df::item_elements{outside}, false);
	s.select(view, df::item_elements{inside}, true);
	s.update_selection();
	assert_equal(2_z, s.selected_count(), "control rectangle preserves outside selection");
	s.select(view, df::item_elements{inside}, true);
	s.update_selection();
	assert_equal(1_z, s.selected_count(), "control rectangle toggles inside selection");
	assert_equal(true, s.selected_items().contains(outside), "outside selection remains selected");

	s.select(view, df::item_elements{find_item_n(s, 5)}, false);
	s.select(view, find_item_n(s, 7), false, true, false);
	s.update_selection();
	assert_equal(3_z, s.selected_count(), "shift extends from the focus a rectangle set");

	s.select(view, df::item_elements{find_item_n(s, 4), find_item_n(s, 5)}, false);
	s.unselect(view, find_item_n(s, 4));
	s.select(view, find_item_n(s, 7), false, true, false);
	s.update_selection();
	assert_equal(3_z, s.selected_count(), "unselecting the anchor moves it to the focus");

	df::item_element_ptr photo;
	df::item_element_ptr other;
	for (const auto& item : s.search_items().items())
	{
		if (!item->is_folder() && item->file_type()->group == file_group::photo && !photo) photo = item;
		if (!item->is_folder() && item->file_type()->group != file_group::photo && !other) other = item;
	}
	assert_equal(true, photo != nullptr && other != nullptr, "test has photo and non-photo items");
	s.select(view, df::item_elements{photo, other}, false);
	s.update_selection();
	assert_equal(2_z, s.selected_count(), "mixed media selection");

	s.filter().add_group(file_group::photo);
	s.update_item_groups();
	s.update_selection();
	assert_equal(1_z, s.selected_count(), "filtered item removed from selection");
	assert_equal(true, s.selected_items().contains(photo), "visible selected item preserved");

	s.filter().clear();
	s.update_item_groups();
	s.update_selection();
	assert_equal(1_z, s.selected_count(), "filtered item does not reappear in selection");

	// A dropped focus keeps its last layout bounds, so item_from_location would keep answering with
	// an item select() then rejects as not displayed, silently ignoring clicks over that area.
	s.select(view, other, false, false, false);
	assert_equal(true, s.focus_item() == other, "focus follows selection");

	s.filter().add_group(file_group::photo);
	s.update_item_groups();
	s.update_selection();
	assert_equal(true, s.focus_item() == nullptr, "filtered focus is dropped");

	s.filter().clear();
	s.update_item_groups();
	s.select(view, photo, false, false, false);
	s.update_selection();
	assert_equal(1_z, s.selected_count(), "selection still works after the focus is dropped");
}

static void should_toggle_rating()
{
	const df::file_path src_path1(test_files_folder, "Test.jpg");
	const df::file_path src_path2(test_files_folder, "Gherkin.CR2");

	// A private folder: opening the shared suite temp folder would scan every file every other
	// test has left there, making this test's cost and results depend on test order.
	const auto temp_folder = _temps.next_folder("rating");
	const auto save_path_1 = _temps.next_path_in(temp_folder, ".jpg");
	const auto save_path_2 = _temps.next_path_in(temp_folder, ".cr2");

	platform::copy_file(src_path1, save_path_1, false, false);
	platform::copy_file(src_path2, save_path_2, false, false);

	null_state_strategy ss;
	null_async_strategy as;
	const view_host_base_ptr view;
	const auto results = std::make_shared<null_item_results_ui>();

	const location_cache locations;
	index_state index(as, locations);
	view_state s(ss, as, index, make_test_player());
	s.view_mode(view_type::items);
	s.open(view, df::search_t().add_selector(temp_folder), {});
	s.item_index.scan_items(s.search_items(), false, false, false, false, test_token);
	s.update_item_groups();

	s.select(view, save_path_1.name(), false);
	s.update_selection();

	assert_equal(1_z, s.selected_count(), "invalid selection");
	const auto jpeg = s.selected_items().items().front();
	s.toggle_rating(results, {jpeg}, 3, view);
	s.item_index.scan_items(s.search_items(), false, false, false, false, test_token);
	assert_equal(3, s.displayed_rating(), "invalid jpeg rating");

	s.toggle_rating(results, {jpeg}, 0, view);
	s.item_index.scan_items(s.search_items(), false, false, false, false, test_token);
	assert_equal(0, s.displayed_rating(), "invalid remove jpeg rating");

	s.select_nothing(view);
	s.update_selection();
	s.toggle_rating(results, {jpeg}, 2, view);
	s.item_index.scan_items(s.search_items(), false, false, false, false, test_token);
	assert_equal(2, jpeg->rating(), "inline rating target is independent of selection");

	s.select(view, save_path_2.name(), false);
	s.update_selection();

	assert_equal(1_z, s.selected_count(), "invalid selection");
	s.toggle_rating(results, s.selected_items().items(), 4, view);
	s.item_index.scan_items(s.search_items(), false, false, false, false, test_token);
	assert_equal(4, s.displayed_rating(), "invalid cr2 rating");

	s.item_index.scan_items(s.search_items(), true, false, false, false, test_token);
	assert_equal(4, s.displayed_rating(), "invalid cr2 rating after thumbnail load");

	s.toggle_rating(results, s.selected_items().items(), 0, view);
	s.item_index.scan_items(s.search_items(), false, false, false, false, test_token);
	assert_equal(0, s.displayed_rating(), "invalid remove cr2 rating");
}

static void assert_can_process(const view_state& s, const bool photos_only, const bool can_save_pixels,
                               const bool can_save_metadata,
                               const bool local_file, const bool local_file_or_folder, const std::string_view message)
{
	const view_host_base_ptr view;
	assert_equal(photos_only, s.can_process_selection(view, df::process_items_type::photos_only), message);
	assert_equal(can_save_pixels, s.can_process_selection(view, df::process_items_type::can_save_pixels), message);
	assert_equal(can_save_metadata, s.can_process_selection(view, df::process_items_type::can_save_metadata), message);
	assert_equal(local_file, s.can_process_selection(view, df::process_items_type::local_file), message);
	assert_equal(local_file_or_folder, s.can_process_selection(view, df::process_items_type::local_file_or_folder),
	             message);
}

static void should_enable_based_on_selection()
{
	null_state_strategy ss;
	null_async_strategy as;
	const view_host_base_ptr view;

	const location_cache locations;
	index_state index(as, locations);
	view_state s(ss, as, index, make_test_player());
	s.view_mode(view_type::items);
	s.open(view, df::search_t().add_selector(test_files_folder), {});
	s.update_item_groups();
	s.update_selection();

	assert_equal(0_z, s.selected_count(), "by default nothing selected");
	assert_can_process(s, false, false, false, false, false, "nothing selected");

	s.select(view, "test.jpg", false);
	s.update_selection();

	assert_equal(1_z, s.selected_count(), "select test.jpg");
	assert_can_process(s, true, true, true, true, true, "photo selected");

	s.select(view, "gizmo.mp4", false);
	s.update_selection();

	assert_equal(1_z, s.selected_count(), "select gizmo.mp4");
	assert_can_process(s, false, false, true, true, true, "mp4 selected");

	s.select(view, "Gherkin.xmp", false);
	s.update_selection();

	assert_equal(1_z, s.selected_count(), "Gherkin.xmp");
	assert_can_process(s, false, false, true, true, true, "xmp selected");

	s.select(view, "test.jpg", false);
	s.select(view, "gizmo.mp4", true);
	s.update_selection();

	assert_equal(2_z, s.selected_count(), "test.jpg and gizmo.mp4");
	assert_can_process(s, false, false, true, true, true, "jpg and mp4 selected");

	s.select(view, "raw", false);
	s.update_selection();

	assert_equal(1_z, s.selected_count(), "folder");
	assert_can_process(s, false, false, false, false, true, "folder selected");

	// A type that carries no edit trait must refuse metadata and say which format refused.
	// can_process must ask file_type::has_trait so a future group-level edit trait cannot
	// silently disagree with the per-extension table.
	s.select(view, "benchmarks.zip", false);
	s.update_selection();

	assert_equal(1_z, s.selected_count(), "benchmarks.zip");
	assert_can_process(s, false, false, false, true, true, "zip selected");

	const auto zip_result = s.selection_process_result(df::process_items_type::can_save_metadata);
	assert_equal(true, zip_result.fail(), "zip cannot save metadata");
	assert_equal(".zip", std::string(zip_result.first_file_extension), "reason names the extension");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Rename execution
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_rename()
{
	null_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);

	const df::file_path src_path(test_files_folder, "Test.jpg");
	const auto save_path_1 = _temps.next_path(".jpg");
	const auto save_path_2 = _temps.next_path(".jpg");

	platform::copy_file(src_path, save_path_1, false, false);

	const auto test_item = load_item(index, save_path_1, false);

	assert_equal(true, test_item->rename(index, save_path_2.file_name_without_extension()).success(), "can rename");
	assert_equal(save_path_2.name(), test_item->name(), "renamed");
	assert_equal(true, save_path_2.exists(), "renamed exists");
}

static void should_not_overwrite_during_rename()
{
	null_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);

	const df::file_path src_path(test_files_folder, "Test.jpg");
	const auto save_path_1 = _temps.next_path(".jpg");
	const auto save_path_2 = _temps.next_path(".jpg");

	platform::copy_file(src_path, save_path_1, false, false);
	platform::copy_file(src_path, save_path_2, false, false);

	const auto test_item = load_item(index, save_path_1, false);

	assert_equal(false, test_item->rename(index, save_path_2.file_name_without_extension()).success(),
	             "should not rename");
	assert_equal(save_path_1.name(), test_item->name(), "not renamed");
	assert_equal(true, test_item->path().exists(), "exists");
}

static void should_rename_file_case()
{
	null_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);

	const auto source = _temps.next_path(".jpg");
	platform::copy_file(df::file_path(test_files_folder, "Test.jpg"), source, false, false);
	const auto item = load_item(index, source, false);
	auto upper_name = str::to_upper(std::string(source.file_name_without_extension()));
	if (upper_name == source.file_name_without_extension()) upper_name = str::to_lower(upper_name);
	const auto destination = df::file_path(source.folder(), upper_name, source.extension());

	assert_equal(true, item->rename(index, upper_name).success(), "case-only rename succeeds");
	assert_equal(destination.name(), item->name(), "item keeps renamed case");
	assert_equal(true, destination.exists(), "case-only destination exists");
}

static void should_rollback_rename_when_sidecar_fails()
{
	null_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);

	const auto primary = _temps.next_path(".jpg");
	const auto sidecar = primary.extension(".xmp");
	const auto target_base = std::string(primary.file_name_without_extension()) + "-renamed";
	const auto target_sidecar = df::file_path(primary.folder(), target_base, ".xmp");
	platform::copy_file(df::file_path(test_files_folder, "Test.jpg"), primary, false, false);
	platform::copy_file(df::file_path(test_files_folder, "IMG_0604.xmp"), sidecar, false, false);
	platform::copy_file(df::file_path(test_files_folder, "IMG_0604.xmp"), target_sidecar, false, false);

	df::index_file_item info;
	info.name = primary.name();
	info.ft = files::file_type_from_name(primary);
	const auto metadata = std::make_shared<prop::item_metadata>();
	metadata->sidecars = sidecar.name();
	info.metadata.store(metadata);
	const auto item = std::make_shared<df::item_element>(primary, info);

	assert_equal(false, item->rename(index, target_base).success(), "sidecar collision fails rename");
	assert_equal(primary.name(), item->name(), "item keeps original name");
	assert_equal(true, primary.exists(), "primary remains at original path");
	assert_equal(true, sidecar.exists(), "sidecar remains at original path");
}

static void should_detect_original_path()
{
	const df::file_path path("c:\\temp\\test.original.jpg");
	assert_equal(true, path.is_original(), "detect original");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Collection membership and crash recording
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_toggle_collection_entry(shared_test_context& stc)
{
	const auto local_folders = platform::local_folders();

	settings_t::index_t settings;
	toggle_collection_entry(settings, local_folders.pictures, false);
	assert_equal(true, settings.pictures, "pictures add");

	toggle_collection_entry(settings, local_folders.pictures, true);
	toggle_collection_entry(settings, test_files_folder, false);
	assert_equal(false, settings.pictures, "pictures remove");
	assert_equal(test_files_folder.text(), settings.more_folders);

	toggle_collection_entry(settings, local_folders.video, false);
	toggle_collection_entry(settings, test_files_folder, true);
	assert_equal(true, settings.video, "pictures remove");
	assert_equal({}, settings.more_folders);
}

static void should_record_crashes()
{
	const auto db_path = _temps.next_path();
	const auto build = "test-build-1"sv;
	const auto paths = {
		test_files_folder.combine_file("Test.jpg"),
		test_files_folder.combine_file("Test90.jpg"),
		test_files_folder.combine_file("Small.jpg"),
		test_files_folder.combine_file("Lossless0.jpg")
	};

	for (auto path : paths)
	{
		{
			crash_files_db test_crash_files(db_path, build);

			assert_equal(test_crash_files.is_known_crash_file(path), false, "is_known_crash_file");

			test_crash_files.add_open(path, str::utf8_cast(__FUNCTION__));
			test_crash_files.remove_open(path);
			test_crash_files.flush_open_files();
		}

		{
			crash_files_db test_crash_files(db_path, build);

			assert_equal(test_crash_files.is_known_crash_file(path), false, "is_known_crash_file");

			test_crash_files.add_open(path, str::utf8_cast(__FUNCTION__));
			test_crash_files.flush_open_files();
		}

		{
			crash_files_db test_crash_files(db_path, build);

			assert_equal(test_crash_files.is_known_crash_file(path), true, "is_known_crash_file");
		}
	}

	{
		crash_files_db test_crash_files(db_path, build);
		assert_equal(test_crash_files.skipped_file_count(), paths.size(), "every crash is recorded once");
	}

	// A repeating crash must not append the same file again, or the list would grow with every run.
	{
		crash_files_db test_crash_files(db_path, build);

		for (auto path : paths) test_crash_files.add_open(path, str::utf8_cast(__FUNCTION__));
		test_crash_files.flush_open_files();
	}

	{
		crash_files_db test_crash_files(db_path, build);
		assert_equal(test_crash_files.skipped_file_count(), paths.size(), "a repeated crash adds no duplicates");
	}

	// A decoder fix ships in an update, so entries earned by an earlier release must not skip files
	// forever - the next release line retries them.
	{
		crash_files_db next_build(db_path, "test-build-2"sv);
		assert_equal(next_build.skipped_file_count(), 0_z, "a new release retries files an older one recorded");
	}

	// A crash is far more costly than a missing thumbnail, so the tag is coarse: neither a rebuild nor
	// a point release may discard what the list has earned.
	assert_equal(crash_files_db::release_tag("127.1"), crash_files_db::release_tag("127.9"),
	             "a point release keeps the skip list");
	assert_equal(crash_files_db::release_tag(df::format_version(true)), crash_files_db::release_tag(s_app_version),
	             "the tag carries no build number");
	assert_equal(crash_files_db::release_tag("127.1") != crash_files_db::release_tag("128.0"), true,
	             "the next release line retries");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// The ping store keeps daily aggregate totals, so whatever the ingest decodes on the day is the
// only reading of that day that will ever exist: a field misread for a month is a month gone. The
// decoder therefore lives here beside the encoder, and this covers every field at its extremes.
///////////////////////////////////////////////////////////////////////////////////////////////////
static void should_round_trip_the_environment_mask()
{
	const auto round_trip = [](const df::environment_facts& facts, const std::string_view what)
	{
		const auto packed = df::pack_environment(facts);
		const auto read_back = df::unpack_environment(packed);

		assert_equal(true, read_back.has_value(), std::format("{} decodes", what));
		assert_equal(true, read_back.value() == facts, std::format("{} reads back as itself", what));
	};

	// Nothing established: every field answers unknown, which is a reading rather than an absence.
	round_trip({}, "an unidentified machine");

	// Every field at the top of its range at once. This is what a field running out of room looks
	// like, and it has to survive being written down.
	round_trip({
		           df::os_family::other, df::os_release::other, df::machine_arch::other,
		           df::machine_arch::other, df::package_kind::other
	           }, "every field at its widest");

	// The cross-tabs the fields exist to answer, each one a real shape.
	round_trip({
		           df::os_family::windows, df::os_release::windows_11, df::machine_arch::x86,
		           df::machine_arch::x64, df::package_kind::installer
	           }, "a 32-bit process on 64-bit Windows 11");
	round_trip({
		           df::os_family::windows, df::os_release::windows_10, df::machine_arch::x64,
		           df::machine_arch::arm64, df::package_kind::microsoft_store
	           }, "an x64 build on arm64 hardware under translation");
	round_trip({
		           df::os_family::linux_, df::os_release::unknown, df::machine_arch::riscv64,
		           df::machine_arch::riscv64, df::package_kind::flatpak
	           }, "a Linux package");

	// Each field on its own, so a shift or a width that is wrong cannot hide behind a neighbour.
	for (const auto family : {df::os_family::unknown, df::os_family::windows, df::os_family::other})
		round_trip({.family = family}, "an OS family alone");
	for (const auto release : {df::os_release::unknown, df::os_release::windows_7, df::os_release::other})
		round_trip({.release = release}, "an OS version alone");
	for (const auto arch : {df::machine_arch::unknown, df::machine_arch::x86, df::machine_arch::other})
	{
		round_trip({.process = arch}, "a process architecture alone");
		round_trip({.machine = arch}, "an OS architecture alone");
	}
	for (const auto package : {df::package_kind::unknown, df::package_kind::appimage, df::package_kind::other})
		round_trip({.package = package}, "a package alone");

	// The layout version is read first and anything else fails closed. Guessing at an unrecognised
	// layout is the one mistake a daily aggregate cannot be talked out of.
	const auto packed = df::pack_environment({df::os_family::windows, df::os_release::windows_11});
	assert_equal(df::environment_layout_version, df::environment_layout::version.unpack(packed),
	             "the version is the first field");
	assert_equal(false, df::unpack_environment((packed & ~0xfull) | 0xf).has_value(),
	             "an unrecognised layout is left undecoded");

	// A reserved bit means the writer knew something this reader does not, so the same rule applies.
	assert_equal(0ull, packed & df::environment_layout::reserved, "nothing is written above the last field");
	assert_equal(false, df::unpack_environment(packed | (1ull << 27)).has_value(),
	             "a value using a reserved bit is left undecoded");

	// The value travels as hex, and the identity carries no measurement, so it stays small enough to
	// read at a glance rather than becoming an opaque 16 digits.
	assert_equal(true, str::to_hex(df::pack_environment({
		             df::os_family::windows, df::os_release::windows_11, df::machine_arch::x64,
		             df::machine_arch::x64, df::package_kind::installer
	             })).size() <= 8, "an ordinary machine packs into a short value");
}

// A view_state opened over the fixture collection, with no window. Every test below drives the same
// entry points the commands do, so what they pin is the behavior a user sees rather than a helper.
struct browsing_fixture
{
	null_state_strategy ss;
	null_async_strategy as;
	view_host_base_ptr view;
	location_cache locations;
	index_state index;
	view_state state;

	explicit browsing_fixture(const df::folder_path folder = test_files_folder)
		: index(as, locations), state(ss, as, index, make_test_player())
	{
		state.view_mode(view_type::items);
		state.open(view, df::search_t().add_selector(folder), {});
		state.update_item_groups();
		state.update_selection();
	}

	df::item_element_ptr find(const std::string_view name) const
	{
		return state.find_displayed_item_by_name(name);
	}
};

// The plan clears a destination held by a file the same run renames away, so the run has to free it
// rather than fail on it. Every row here wants the next row's current name.
static void should_run_a_rename_onto_vacated_names()
{
	const auto root = _temps.next_folder("rename-run-chain");
	for (const auto& name : {"Item 001.jpg"s, "Item 002.jpg"s, "Item 003.jpg"s})
		platform::copy_file(df::file_path(test_files_folder, "Test.jpg"), root.combine_file(name), false, false);

	browsing_fixture f(root);
	f.state.select_all(f.view);
	f.state.update_selection();
	assert_equal(3_z, f.state.selected_items().size(), "the fixture folder is selected");

	const auto saved = setting.rename;
	const df::scope_exit restore([saved] { setting.rename = saved; });
	setting.rename.name_template = "Item ###";
	setting.rename.start_seq = "2";
	setting.rename.collision = collision_policy::block_run;

	const auto view = std::make_shared<rename_view>(f.state, nullptr);
	view->activate({100, 100});
	assert_equal(true, view->can_run(), "a chained rename can run");
	view->run();

	assert_equal(false, root.combine_file("Item 001.jpg").exists(), "the vacated name is gone");
	for (const auto& name : {"Item 002.jpg"s, "Item 003.jpg"s, "Item 004.jpg"s})
		assert_equal(true, root.combine_file(name).exists(), std::format("{} was written", name));
}

// A case-only rename frees nothing, so the run must not park the file it is renaming within.
static void should_run_a_case_only_rename()
{
	const auto root = _temps.next_folder("rename-run-case");
	platform::copy_file(df::file_path(test_files_folder, "Test.jpg"), root.combine_file("photo.jpg"), false, false);

	browsing_fixture f(root);
	f.state.select_all(f.view);
	f.state.update_selection();

	const auto saved = setting.rename;
	const df::scope_exit restore([saved] { setting.rename = saved; });
	setting.rename.name_template = "PHOTO";
	setting.rename.start_seq = "1";
	setting.rename.collision = collision_policy::block_run;

	const auto view = std::make_shared<rename_view>(f.state, nullptr);
	view->activate({100, 100});
	assert_equal(true, view->can_run(), "a case-only rename can run");
	view->run();

	const auto contents = platform::iterate_file_items(root, false);
	assert_equal(1_z, contents.files.size(), "the folder still holds one file");
	assert_equal("PHOTO.jpg", std::string(contents.files.front().name), "the file carries the new case");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Issue #250 - after a move or a delete the cursor reset to the start of the listing instead of
// settling on what followed the set that left, which turns sorting a folder into a re-navigation
// after every action. Delete and move ask the same question, so they must reuse one answer rather
// than each deriving its own.
///////////////////////////////////////////////////////////////////////////////////////////////////
static void should_land_on_what_followed_a_removed_set()
{
	browsing_fixture f;

	const auto& groups = f.state.groups();
	assert_equal(true, !groups.empty(), "the fixture folder lists something");

	std::vector<df::item_element_ptr> listed;
	for (const auto& g : groups)
		for (const auto& i : g->items())
			listed.emplace_back(i);

	assert_equal(true, listed.size() > 2, "and it lists enough to have a successor");

	// A set in the middle: the cursor settles on the first item after it, not on the first item.
	f.state.select(f.view, listed[1], false, false, false);
	f.state.update_selection();
	assert_equal(true, f.state.next_unselected_item() == listed[2], "the item after the selection follows it");

	// The last item: nothing follows it, so the cursor settles on what came BEFORE the set rather
	// than on the first item of the listing. Asserting only "not null and not the item that left"
	// is satisfied by listed[0], which is the reset this exists to catch.
	f.state.select(f.view, listed.back(), false, false, false);
	f.state.update_selection();

	const auto after_last = f.state.next_unselected_item();
	assert_equal(true, after_last == listed[listed.size() - 2],
	             "a set running to the end settles on what preceded it");

	// Everything selected: nothing survives it, and the caller has to cope with that rather than
	// being handed an item that is about to disappear.
	f.state.select_all(f.view);
	f.state.update_selection();
	assert_equal(true, f.state.next_unselected_item() == nullptr, "a whole listing leaving has no successor");
}

// design.md: "Only photos, video, and audio take part, so a folder, document, or archive is stepped
// over rather than stalling the sequence". The fixture folder holds all three kinds of passenger.
static void should_step_over_items_that_cannot_play()
{	browsing_fixture f;

	const auto archive = f.find("benchmarks.zip");
	const auto document = f.find("place.json");
	assert_equal(true, archive != nullptr, "the archive fixture is present");
	assert_equal(true, document != nullptr, "the document fixture is present");
	assert_equal(false, archive->is_media(), "an archive is not media");
	assert_equal(false, document->is_media(), "a document is not media");

	auto folders = 0;

	for (const auto& g : f.state.groups())
	{
		for (const auto& i : g->items())
		{
			if (i->is_folder()) ++folders;
		}
	}

	assert_equal(true, folders > 0, "the fixture collection contains folders to step over");

	// Walk the whole collection from every starting point; the sequence must never offer a
	// passenger it cannot play or hold.
	auto visited = 0;

	for (const auto& g : f.state.groups())
	{
		for (const auto& i : g->items())
		{
			f.state.select(f.view, i, false, false, false);
			const auto next = f.state.next_media_item(true, true);

			if (next)
			{
				assert_equal(true, next->is_media(), "forward step lands on media");
				++visited;
			}

			const auto previous = f.state.next_media_item(false, true);
			if (previous) assert_equal(true, previous->is_media(), "backward step lands on media");
		}
	}

	assert_equal(true, visited > 0, "the walk actually stepped");

	// Without wrap the ends are ends, which is what stops a sequence looping when repeat is off.
	f.state.select(f.view, f.state.next_media_item(true, true), false, false, false);
	auto steps = 0;
	while (const auto next = f.state.next_media_item(true, false))
	{
		f.state.select(f.view, next, false, false, false);
		if (++steps > 1000) break;
	}

	assert_equal(true, steps > 0, "stepping forward without wrap advances");
	assert_equal(true, f.state.next_media_item(true, false) == nullptr, "the last media item has no next");
	assert_equal(true, f.state.next_media_item(true, true) != nullptr, "wrapping from the last item finds one");
}

// design.md: "a slideshow that can no longer reach a playable item stops instead of appearing to
// keep playing".
static void should_offer_a_slideshow_only_when_something_can_play()
{
	browsing_fixture f;
	assert_equal(true, f.state.can_slideshow(), "a collection holding media can run a slideshow");

	// A scope holding nothing playable cannot, however it was reached.
	browsing_fixture empty(_temps.next_folder("no-media"));
	assert_equal(false, empty.state.can_slideshow(), "an empty scope cannot run a slideshow");
}

// design.md: "Next and Previous folder move between siblings of the current folder. At the first or
// last sibling they do nothing and are unavailable; they never change level."
static void should_move_between_sibling_folders()
{
	const auto root = _temps.next_folder("siblings");
	for (const auto* const name : {"b-middle", "a-first", "c-last"})
	{
		platform::create_folder(root.combine(name));
	}

	browsing_fixture f(root.combine("b-middle"));

	assert_equal(true, f.state.has_next_path(true), "a middle sibling has a next");
	assert_equal(true, f.state.has_next_path(false), "a middle sibling has a previous");

	f.state.open_next_path(f.view, true);
	assert_equal("c-last", f.state.search().selectors().front().folder().name(), "next moves to the later sibling");
	assert_equal(false, f.state.has_next_path(true), "the last sibling has no next");
	assert_equal(true, f.state.has_next_path(false), "the last sibling still has a previous");

	// At the end the command does nothing rather than changing level.
	const auto before = f.state.search().text();
	f.state.open_next_path(f.view, true);
	assert_equal(before, f.state.search().text(), "the last sibling does not move");

	f.state.open_next_path(f.view, false);
	f.state.open_next_path(f.view, false);
	assert_equal("a-first", f.state.search().selectors().front().folder().name(), "previous walks back");
	assert_equal(false, f.state.has_next_path(false), "the first sibling has no previous");
}

// design.md: "Empty space is inert: a click that hits no item, and a drag whose rectangle covers no
// item, leave the selection unchanged rather than clearing it."
static void should_leave_the_selection_alone_over_empty_space()
{
	browsing_fixture f;

	const auto first = find_item_n(f.state, 0);
	const auto second = find_item_n(f.state, 1);
	assert_equal(true, first != nullptr && second != nullptr, "the fixture has items to select");

	// Item bounds come from layout, which no test runs, so they are placed here instead.
	first->bounds = {0, 0, 100, 100};
	second->bounds = {100, 0, 200, 100};

	for (const auto& g : f.state.groups())
	{
		g->bounds = {0, 0, 1000, 1000};
	}

	f.state.select(f.view, df::item_elements{first, second}, false);
	f.state.update_selection();
	assert_equal(2_z, f.state.selected_count(), "two items selected to start");

	// A rectangle over nothing must not clear what is selected.
	f.state.select(f.view, recti{500, 500, 600, 600}, false);
	f.state.update_selection();
	assert_equal(2_z, f.state.selected_count(), "an empty rectangle leaves the selection alone");

	// A rectangle that does cover an item still selects normally, so the guard above is not simply
	// refusing every rectangle.
	f.state.select(f.view, recti{0, 0, 50, 50}, false);
	f.state.update_selection();
	assert_equal(1_z, f.state.selected_count(), "a rectangle over an item replaces the selection");
}

// design.md: "Filters change what is visible, not what exists ... every command still targets only
// visible items".
static void should_target_only_visible_items_when_filtered()
{
	browsing_fixture f;

	f.state.select_all(f.view);
	f.state.update_selection();
	const auto unfiltered = f.state.selected_count();
	assert_equal(true, unfiltered > 0, "select all selects something");

	f.state.filter().toggle(file_group::photo);
	f.state.update_item_groups();
	f.state.update_selection();

	const auto filtered = f.state.selected_count();
	assert_equal(true, filtered > 0, "photos remain selected through the filter");
	assert_equal(true, filtered < unfiltered, "a filter removes hidden items from the command target");

	for (const auto& i : f.state.selected_items()._items)
	{
		assert_equal(true, i->file_type()->group == file_group::photo, "only visible items are targeted");
	}

	// Select All targets what is visible, not what exists.
	f.state.select_all(f.view);
	f.state.update_selection();
	assert_equal(filtered, f.state.selected_count(), "select all targets the visible items");

	f.state.clear_filters();
	f.state.update_item_groups();
	f.state.update_selection();
	assert_equal(true, f.state.selected_count() >= filtered, "clearing the filter shows the items again");
}

// design.md: "Shuffle is visibly exclusive with deterministic sorting."
static void should_keep_shuffle_exclusive_with_sorting()
{
	browsing_fixture f;

	f.state.group_order(group_by::shuffle, {});
	assert_equal(true, f.state.group_order() == group_by::shuffle, "shuffle can be chosen");

	// Choosing a deterministic sort while shuffled must leave shuffle behind rather than producing a
	// grouping that claims to be both.
	f.state.group_order({}, sort_by::name);
	assert_equal(true, f.state.sort_order() == sort_by::name, "the sort order is taken");
	assert_equal(false, f.state.group_order() == group_by::shuffle, "sorting turns shuffle off");

	f.state.group_order(group_by::shuffle, {});
	assert_equal(true, f.state.group_order() == group_by::shuffle, "shuffle can be chosen again");

	// Grouping alone is a different axis and does not need to disturb the sort.
	f.state.group_order(group_by::camera, {});
	assert_equal(true, f.state.group_order() == group_by::camera, "grouping can be changed");

	// A related search presents as related regardless of the chosen grouping.
	f.state.group_order(group_by::file_type, sort_by::def);
	assert_equal(true, f.state.effective_group_order() == group_by::file_type, "a plain search groups as chosen");
}

// Two commands claiming one key means the second is unreachable, and nothing in the running app
// reports it. Dispatch (app_frame::key_down) walks `_commands`, an UNORDERED map, and takes the
// first match whose `enable` is set - so a collision is only safe while the two can never be
// enabled at once, and is otherwise resolved arbitrarily. The pairs below qualify: browse_* is
// enabled in the media and items views, edit_item_save_and_* only in the edit and locate views.
static void should_not_claim_one_key_for_two_commands()
{
	const auto table = default_keyboard_accelerators();
	assert_equal(true, table.size() > 80, "the accelerator table is populated");

	const std::set<std::pair<commands, commands>> allowed_collisions{
		{commands::browse_back, commands::edit_item_save_and_prev},
		{commands::browse_forward, commands::edit_item_save_and_next},
	};

	std::map<std::pair<char32_t, uint32_t>, commands> claimed;

	for (const auto& [id, key] : table)
	{
		assert_equal(true, key.key != 0, "every binding names a key");
		assert_equal(true, id != commands::none, "every binding names a command");

		const auto stroke = std::make_pair(key.key, key.key_state);
		const auto found = claimed.find(stroke);

		if (found == claimed.end())
		{
			claimed.emplace(stroke, id);
			continue;
		}

		const auto pair = std::minmax(found->second, id);
		assert_equal(true, allowed_collisions.contains({pair.first, pair.second}),
		             std::format("key {:#x} state {:#x} is claimed twice", static_cast<uint32_t>(key.key),
		                         key.key_state));
	}

	// A few bindings users rely on, spot-checked so a wholesale edit of the table cannot pass unnoticed.
	const auto bound = [&table](const commands id, const char32_t key, const uint32_t state)
	{
		return std::ranges::any_of(table, [=](const command_accelerator& a)
		{
			return a.id == id && a.key.key == key && a.key.key_state == state;
		});
	};

	assert_equal(true, bound(commands::refresh, keys::F5, 0), "F5 refreshes");
	assert_equal(true, bound(commands::view_fullscreen, keys::F11, 0), "F11 is fullscreen");
	assert_equal(true, bound(commands::select_all, 'A', keyboard_accelerator_t::control), "Ctrl+A selects all");
	assert_equal(true, bound(commands::tool_delete, keys::DEL, 0), "Delete deletes");
	assert_equal(true, bound(commands::view_close, keys::ESCAPE, 0), "Escape closes");
}

// The button reads as one label or two. design.md: "Shuffle is visibly exclusive with deterministic
// sorting", so a shuffled view must never also advertise a sort order.
static void should_label_grouping_and_sorting()
{
	df::file_group_histogram summary;
	const df::file_group_histogram empty_summary;

	summary.record(files::file_type_from_name("Test.jpg"), df::file_size(4096));

	const auto says = [](const std::string& text, const std::string_view part)
	{
		return text.find(part) != std::string::npos;
	};

	// Nothing indexed yet reads as loading rather than as an empty collection.
	assert_equal(std::string(tt.loading), format_items_summary(group_by::file_type, sort_by::name, empty_summary, false),
	             "an uncounted empty collection is loading");
	assert_equal(std::string(tt.empty), format_items_summary(group_by::file_type, sort_by::name, empty_summary, true),
	             "a counted empty collection is empty");

	// Shuffle names only itself; adding a sort order would claim an order it does not have.
	const auto shuffled = format_items_summary(group_by::shuffle, sort_by::name, summary, true);
	assert_equal(true, says(shuffled, tt.sort_by_shuffle.sv()), "shuffle is named");
	assert_equal(false, says(shuffled, tt.sort_by_name.sv()), "shuffle does not also claim a sort order");

	// The default sort is not a choice worth naming either.
	const auto defaulted = format_items_summary(group_by::file_type, sort_by::def, summary, true);
	assert_equal(false, says(defaulted, tt.sort_by_def.sv()), "the default order is not named");

	// A real pairing names both.
	const auto both = format_items_summary(group_by::camera, sort_by::name, summary, true);
	assert_equal(true, says(both, tt.prop_name_camera.sv()), "the grouping is named");
	assert_equal(true, says(both, tt.sort_by_name.sv()), "the sort order is named");

	// Grouping and sorting by the same thing is said once, not twice.
	const auto same = format_items_summary(group_by::date_created, sort_by::date_created, summary, true);
	assert_equal(true, says(same, tt.prop_name_created.sv()), "the shared name is present");
	assert_equal(true, both.length() > same.length(), "a shared name is not repeated");
}

// Hands out real command objects so the menu built from them can be compared by identity. A menu
// that invented its own copies would look right and drift the moment either changed.
class command_state_strategy final : public null_state_strategy
{
public:
	mutable df::hash_map<commands, ui::command_ptr> commands_by_id;

	ui::command_ptr find_command(const commands cmd) const override
	{
		auto& found = commands_by_id[cmd];
		if (!found) found = std::make_shared<ui::command>();
		return found;
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////////
// Discussion #251 - the filter toolbar scrolls away, so changing a filter over a long listing meant
// scrolling to the top first. The control in the scroller track is always on screen and now carries
// those settings. Its click opens the menu in every scroll position: opening a menu when at the top
// and scrolling when not would be a context-dependent surprise.
///////////////////////////////////////////////////////////////////////////////////////////////////
static void should_offer_the_items_menu_at_every_scroll_position()
{
	command_state_strategy ss;
	null_async_strategy as;
	location_cache locations;
	index_state index(as, locations);
	view_state state(ss, as, index, make_test_player());

	// The toolbar's grouping button carries the Group by / Sort by list, so the menu must take it
	// from there rather than list the same commands again.
	const auto group = ss.find_command(commands::menu_group_toolbar);
	group->menu = [&ss] { return std::vector{ss.find_command(commands::group_folder)}; };

	auto scrolled = 0;
	const auto scroll = [&scrolled] { ++scrolled; };

	for (const auto at_top : {true, false})
	{
		const auto menu = items_scroll_menu(state, at_top, scroll);

		assert_equal(true, menu.size() > 1, "the menu is offered whatever the scroll position");
		assert_equal(true, menu.front() != nullptr, "and it opens with an entry of its own");
		assert_equal(std::string(tt.tooltip_scroll_to_top.sv()), std::string(menu.front()->text),
		             "scroll to top comes first, because the click no longer performs it");
		assert_equal(!at_top, menu.front()->enable, "and it says whether it has anything to do");

		const auto offers = [&menu](const ui::command_ptr& c)
		{
			return std::ranges::find(menu, c) != menu.end();
		};

		assert_equal(true, offers(ss.find_command(commands::group_folder)), "the toolbar's grouping list is carried");
		assert_equal(true, offers(ss.find_command(commands::filter_photos)), "so is the media type filter");
		assert_equal(true, offers(ss.find_command(commands::filter_videos)), "every part of it");
		assert_equal(true, offers(ss.find_command(commands::filter_audio)), "every part of it");
		assert_equal(true, offers(ss.find_command(commands::browse_recursive)),
		             "and Show items in subfolders, which was on no toolbar at all");
	}

	// Invoking the first entry is what scrolls, and only when there is somewhere to scroll to.
	const auto at_top_menu = items_scroll_menu(state, true, scroll);
	at_top_menu.front()->invoke();
	assert_equal(1, scrolled, "the entry performs the scroll the click used to");
}

// design.md "Navigation and search" specifies the address box as an editing session with a
// committed address, a typed draft and a previewed completion. The two-stage Escape is the part a
// user notices: losing it either throws away what they typed or traps them in the popup.
static void should_run_an_address_editing_session()
{
	search_edit_session session;
	session.begin("c:\\photos");

	assert_equal("c:\\photos", session.original_text(), "focusing snapshots the committed address");
	assert_equal("c:\\photos", session.typed_text(), "the draft starts at the committed address");
	assert_equal(false, session.is_previewing(), "a new session previews nothing");

	// Typing maintains a draft separate from the committed address.
	session.typed("bea");
	assert_equal("bea", session.typed_text(), "typing updates the draft");
	assert_equal("c:\\photos", session.original_text(), "typing does not disturb the committed address");

	// Up and Down preview a completion without changing the draft.
	session.preview("beach");
	assert_equal(true, session.is_previewing(), "a highlighted completion is previewing");
	assert_equal("bea", session.typed_text(), "previewing leaves the draft alone");

	// First Escape: back to the draft, popup stays open, first completion highlighted.
	const auto first_escape = session.escape();
	assert_equal(true, first_escape.handled, "escape is handled");
	assert_equal("bea", first_escape.edit_text, "the first escape restores the typed draft");
	assert_equal(true, first_escape.select_first_completion, "the first escape highlights the first completion");
	assert_equal(false, first_escape.close_popup, "the first escape keeps the popup open");
	assert_equal(false, first_escape.focus_view, "the first escape keeps address focus");
	assert_equal(false, session.is_previewing(), "the first escape ends the preview");

	// Second Escape: back to where editing began, popup closes, focus returns to the content view.
	const auto second_escape = session.escape();
	assert_equal("c:\\photos", second_escape.edit_text, "the second escape restores the committed address");
	assert_equal(true, second_escape.close_popup, "the second escape closes the popup");
	assert_equal(true, second_escape.focus_view, "the second escape returns focus to the content view");
	assert_equal(false, second_escape.select_first_completion, "the second escape highlights nothing");

	// Escape with nothing previewed goes straight to the committed address.
	session.begin("c:\\photos");
	session.typed("bea");
	const auto direct = session.escape();
	assert_equal("c:\\photos", direct.edit_text, "escape without a preview restores the committed address");
	assert_equal(true, direct.close_popup, "escape without a preview closes the popup");
}

// design.md: "Plain Tab accepts the highlighted completion into the draft without running it,
// refreshes completions, keeps the popup open, and retains address focus."
static void should_accept_a_completion_without_running_it()
{
	search_edit_session session;
	session.begin("c:\\photos");
	session.typed("bea");
	session.preview("beach");

	const auto accepted = session.accept(true, "beach ");
	assert_equal(true, accepted.handled, "tab with a highlighted completion is handled");
	assert_equal("beach ", accepted.edit_text, "tab fills in the completion");
	assert_equal(true, accepted.refresh_completions, "tab refreshes completions so the token can extend");
	assert_equal(false, accepted.close_popup, "tab keeps the popup open");
	assert_equal(false, accepted.focus_view, "tab retains address focus");

	// The accepted text becomes the draft, so a later preview-then-Escape returns here, not to "bea".
	assert_equal("beach ", session.typed_text(), "the accepted completion becomes the draft");
	assert_equal(false, session.is_previewing(), "accepting ends the preview");

	session.preview("beach huts");
	assert_equal("beach ", session.escape().edit_text, "escape after tab returns to the accepted draft");

	// With nothing highlighted, Tab is not ours to handle - it must fall through to focus traversal.
	search_edit_session untouched;
	untouched.begin("c:\\photos");
	const auto ignored = untouched.accept(false, {});
	assert_equal(false, ignored.handled, "tab without a completion is not handled");
	assert_equal(false, ignored.set_edit_text, "tab without a completion changes nothing");
}

// design.md: "Enter commits the highlighted completion when one is selected, otherwise it commits
// the visible address."
static void should_commit_the_highlighted_completion_or_the_visible_address()
{
	search_edit_session session;
	session.begin("c:\\photos");
	session.typed("bea");

	assert_equal("beach", session.commit(true, "beach", "bea"), "enter commits the highlighted completion");
	assert_equal("bea", session.commit(false, {}, "bea"), "enter commits the visible address when nothing is highlighted");

	// A preview shows in the address box, so committing with no selection still runs what is visible.
	session.preview("beach");
	assert_equal("beach", session.commit(false, {}, "beach"), "enter runs the address the user can see");
}

// design.md separates three named behaviors that all decide what happens when an item ends:
// Slideshow, Continue with the next item, and Repeat. They interact, and the interaction is the
// part a user reports as "it stopped" or "it played two at once".
static void should_decide_what_follows_an_item()
{
	// A photo in a slideshow, with a next item waiting.
	const auto photo_in_slideshow = []
	{
		playback_tick t;
		t.is_slideshow = true;
		t.is_photo = true;
		t.can_next = true;
		t.has_next = true;
		return t;
	};

	// Nothing has ended yet, so nothing happens.
	{
		auto t = photo_in_slideshow();
		assert_equal(true, calc_playback_advance(t) == playback_advance::none, "an unfinished photo is left alone");
	}

	// design.md: "a folder, document, or archive is stepped over rather than stalling the sequence",
	// and a slideshow that reaches one ends instead of appearing to keep playing.
	{
		playback_tick t;
		t.is_slideshow = true;
		t.can_next = true;
		t.has_next = true;
		assert_equal(true, calc_playback_advance(t) == playback_advance::stop,
		             "a slideshow on something that cannot play or time out stops");
	}

	// The delay elapses and the sequence moves on.
	{
		auto t = photo_in_slideshow();
		t.photo_delay_elapsed = true;
		assert_equal(true, calc_playback_advance(t) == playback_advance::advance, "an elapsed photo advances");
	}

	// design.md: "A slideshow always continues, because continuing is what the mode means" - even
	// with Continue with the next item switched off.
	{
		auto t = photo_in_slideshow();
		t.photo_delay_elapsed = true;
		t.auto_advance = false;
		assert_equal(true, calc_playback_advance(t) == playback_advance::advance,
		             "a slideshow continues regardless of the auto advance preference");
	}

	// Browsing normally, the preference decides.
	{
		playback_tick t;
		t.is_av = true;
		t.media_ended = true;
		t.can_next = true;
		t.has_next = true;
		assert_equal(true, calc_playback_advance(t) == playback_advance::stop,
		             "browsing without auto advance stops at the end of an item");

		t.auto_advance = true;
		assert_equal(true, calc_playback_advance(t) == playback_advance::advance,
		             "browsing with auto advance moves to the next item");
	}

	// design.md: "repeat one holds on the current item" - and it outranks everything below it.
	{
		auto t = photo_in_slideshow();
		t.photo_delay_elapsed = true;
		t.repeat = repeat_mode::repeat_one;
		t.has_next = true;
		assert_equal(true, calc_playback_advance(t) == playback_advance::hold, "repeat one holds on the item");

		t.has_next = false;
		assert_equal(true, calc_playback_advance(t) == playback_advance::hold,
		             "repeat one holds even with nothing to advance to");
	}

	// design.md: "repeat all returns to the first item after the last, and no repeat stops there".
	{
		auto t = photo_in_slideshow();
		t.photo_delay_elapsed = true;
		t.has_next = false;
		t.has_wrapped_next = true;

		t.repeat = repeat_mode::repeat_none;
		assert_equal(true, calc_playback_advance(t) == playback_advance::stop, "no repeat stops at the last item");

		t.repeat = repeat_mode::repeat_all;
		assert_equal(true, calc_playback_advance(t) == playback_advance::advance, "repeat all wraps to the first item");

		// Wrapping onto the only item must restart it in place; advancing would be a no-op select
		// that leaves the player paused on the first frame.
		t.wrapped_is_current = true;
		assert_equal(true, calc_playback_advance(t) == playback_advance::hold,
		             "wrapping onto the displayed item restarts it in place");
	}

	// Nothing to continue to at all.
	{
		auto t = photo_in_slideshow();
		t.photo_delay_elapsed = true;
		t.has_next = false;
		t.repeat = repeat_mode::repeat_all;
		t.has_wrapped_next = false;
		assert_equal(true, calc_playback_advance(t) == playback_advance::stop,
		             "repeat all with nothing to wrap to stops");
	}

	// A command is running, or the view cannot play: the sequence ends rather than fighting it.
	{
		auto t = photo_in_slideshow();
		t.photo_delay_elapsed = true;
		t.can_next = false;
		t.repeat = repeat_mode::repeat_one;
		assert_equal(true, calc_playback_advance(t) == playback_advance::stop,
		             "a sequence that cannot advance stops, even under repeat one");
	}
}

// index_state::auto_complete_words asserts it is not on the UI thread, because the real app reads
// the vocabulary on the auto_complete worker. The hop is modelled rather than bypassed, so these
// tests exercise the same threading the shipped code does.
static void run_completion_worker(deferred_async_strategy& as)
{
	auto ran = false;
	std::thread worker([&as, &ran] { ran = as.run_next(async_queue::auto_complete); });
	worker.join();
	assert_equal(true, ran, "a completion pass was queued");
}

// The order of what the address bar offers is the part a user judges. These rules live in
// search_auto_complete::search and had no coverage: it is reached only through a native edit
// control, so the strategy is built here through its factory instead.
static void should_rank_address_completions(shared_test_context& stc)
{
	stc.lazy_load_index();
	stc.test_index.update_summary();

	null_state_strategy ss;
	deferred_async_strategy as;
	view_state s(ss, as, stc.test_index, make_test_player());
	s.view_mode(view_type::items);

	const auto completes = make_search_auto_complete(s, [](std::string) {});
	completes->initialise([](const ui::auto_complete_results&) {});

	ui::auto_complete_results results;
	auto completions = 0;

	const auto run = [&](const std::string& query)
	{
		results.clear();
		completions = 0;
		completes->search(query, [&](const ui::auto_complete_results& r)
		{
			results = r;
			++completions;
		});
		run_completion_worker(as);
		as.drain_ui();
	};

	run("#key");

	assert_equal(1, completions, "a completion pass reports once");
	assert_equal(true, !results.empty(), "a query produces completions");

	// design.md: "Parsed input retains the user's visible spelling". Whatever else is offered, what
	// the user actually typed stays reachable at the top - Enter must never run something else.
	assert_equal("#key", results.front()->edit_text(), "the typed query is offered first");

	// The list is ranked, so a lower-weight suggestion can never appear above a higher one.
	for (auto i = 1u; i < results.size(); ++i)
	{
		assert_equal(true, results[i - 1]->weight >= results[i]->weight, "completions are ranked by weight");
	}

	// A repeated suggestion wastes a row and reads as a bug.
	df::hash_set<std::string, df::ihash, df::ieq> seen;

	for (const auto& r : results)
	{
		assert_equal(true, seen.emplace(r->edit_text()).second,
		             std::format("'{}' is offered once", r->edit_text()));
		assert_equal(true, !r->edit_text().empty(), "a completion is never blank");
	}

	assert_equal(true, results.size() <= completes->max_predictions, "the list is capped");

	// A scoped token is answered from its own vocabulary. The indexed fixture carries
	// #key1/#key2/#key3, so this cannot pass vacuously.
	const auto tag_suggested = std::ranges::any_of(results, [](const ui::auto_complete_match_ptr& r)
	{
		return str::icmp(r->edit_text(), "#key1") == 0;
	});
	assert_equal(true, tag_suggested, "an indexed tag is offered for a tag prefix");

	// That tag must also outrank the generic matches below it, or it is offered but unreachable.
	const auto tag_pos = std::ranges::find_if(results, [](const ui::auto_complete_match_ptr& r)
	{
		return str::icmp(r->edit_text(), "#key1") == 0;
	});
	assert_equal(true, tag_pos != results.end() && tag_pos - results.begin() <= 3,
	             "a scoped suggestion is ranked near the top");

	// A term the user has typed before AND that the index knows arrives from several sources at
	// once, so this is where a missing de-duplication pass shows up as a repeated row.
	s.recent_tags.add("key1");
	completes->initialise([](const ui::auto_complete_results&) {});
	run("#key1");

	assert_equal(true, !results.empty(), "a known tag produces completions");

	df::hash_set<std::string, df::ihash, df::ieq> seen_known;

	for (const auto& r : results)
	{
		assert_equal(true, seen_known.emplace(r->edit_text()).second,
		             std::format("'{}' is offered once for a query with several sources", r->edit_text()));
	}
}

// design.md: the address box "also accepts folders and offers relevant completions". With nothing
// typed there is no query to complete, so the useful offer is where the user has already been.
static void should_offer_recent_searches_for_an_empty_address()
{
	null_state_strategy ss;
	deferred_async_strategy as;
	location_cache locations;
	index_state index(as, locations);
	view_state s(ss, as, index, make_test_player());
	s.view_mode(view_type::items);

	s.recent_searches.add("@photo beach");
	s.recent_searches.add(std::string(test_files_folder.text()));

	const auto completes = make_search_auto_complete(s, [](std::string) {});
	completes->initialise([](const ui::auto_complete_results&) {});

	ui::auto_complete_results results;
	completes->search({}, [&results](const ui::auto_complete_results& r) { results = r; });
	run_completion_worker(as);
	as.drain_ui();

	const auto offered = [&results](const std::string_view text)
	{
		return std::ranges::any_of(results, [text](const ui::auto_complete_match_ptr& r)
		{
			return str::icmp(r->edit_text(), text) == 0;
		});
	};

	assert_equal(true, offered("@photo beach"), "a recent query is offered");
	assert_equal(true, offered(test_files_folder.text()), "a recent folder is offered");

	// An empty address has nothing typed, so nothing may be presented as the typed query.
	for (const auto& r : results)
	{
		assert_equal(true, !r->edit_text().empty(), "an empty address offers no blank completion");
	}
}

// Completions are computed off the UI thread while the user keeps typing. design.md requires the
// preview to follow what was typed, so an older pass must not deliver over a newer one.
static void should_discard_a_superseded_completion()
{
	null_state_strategy ss;
	deferred_async_strategy as;
	location_cache locations;
	index_state index(as, locations);
	view_state s(ss, as, index, make_test_player());
	s.view_mode(view_type::items);

	const auto completes = make_search_auto_complete(s, [](std::string) {});
	completes->initialise([](const ui::auto_complete_results&) {});

	auto first_delivered = 0;
	auto second_delivered = 0;

	completes->search("bea", [&first_delivered](const ui::auto_complete_results&) { ++first_delivered; });
	completes->search("beach", [&second_delivered](const ui::auto_complete_results&) { ++second_delivered; });

	assert_equal(2_z, as.pending_worker_count(async_queue::auto_complete), "both passes were queued");

	run_completion_worker(as);
	run_completion_worker(as);
	as.drain_ui();

	assert_equal(0, first_delivered, "the superseded pass delivers nothing");
	assert_equal(1, second_delivered, "the newest pass delivers once");

	// The same guard must hold when the older pass finishes last, which is the case that actually
	// overwrites the user's typing.
	first_delivered = 0;
	second_delivered = 0;

	completes->search("bea", [&first_delivered](const ui::auto_complete_results&) { ++first_delivered; });
	run_completion_worker(as);

	completes->search("beach", [&second_delivered](const ui::auto_complete_results&) { ++second_delivered; });
	run_completion_worker(as);

	as.drain_ui();

	assert_equal(0, first_delivered, "a late older pass is still discarded");
	assert_equal(1, second_delivered, "the newest pass is the one that lands");
}

void register_app_tests(view_state& state, test_registry& tests)
{
	tests.add("INI file settings should persist values"s, should_persist_to_ini_file);
	tests.add("Should store an order an older build can read"s, should_store_an_order_an_older_build_can_read);
	tests.add("Should Rename with substitutions"s, should_rename_with_substitutions);
	tests.add("Should rename name token without extension"s, should_rename_name_token_without_extension);
	tests.add("Should reject duplicate rename targets"s, should_reject_duplicate_rename_targets);
	tests.add("Should reject unusable rename targets"s, should_reject_unusable_rename_targets);
	tests.add("Should group Rename sidecar collisions"s, should_group_rename_sidecar_collisions);
	tests.add("Should plan the default rename template"s, should_plan_the_default_rename_template);
	tests.add("Should rename a set onto names it is vacating"s, should_rename_a_set_onto_names_it_is_vacating);
	tests.add("Should cascade skipped rename rows"s, should_cascade_skipped_rename_rows);
	tests.add("Should run a rename onto vacated names"s, should_run_a_rename_onto_vacated_names);
	tests.add("Should run a case only rename"s, should_run_a_case_only_rename);
	tests.add("Should format rename"s, should_format_rename);
	tests.add("Should plan unique convert outputs"s, should_plan_unique_convert_outputs);
	tests.add("Should adjust item dates from snapshot"s, should_adjust_item_dates_from_snapshot);
	tests.add("Should round trip the environment mask"s, should_round_trip_the_environment_mask);
#ifdef _WIN32
	tests.add("Should classify MAPI results"s, should_classify_mapi_results);
#endif
	tests.add("Should detect duplicate import destinations"s, should_detect_duplicate_import_destinations);
	tests.add("Should revalidate import rows"s, should_revalidate_import_rows);
	tests.add("Should revalidate replaced import destinations"s, should_revalidate_replaced_import_destinations);
	tests.add("Should reject missing sync folder"s, should_reject_missing_sync_folder);
	tests.add("Should reject overlapping sync folders"s, should_reject_overlapping_sync_folders);
	tests.add("Should reject ambiguous sync roots"s, should_reject_ambiguous_sync_roots);
	tests.add("Should ignore unclaimed remote sync files"s, should_ignore_unclaimed_remote_files);
	tests.add("Should select sync actions"s, should_select_sync_actions);
	tests.add("Should revalidate sync rows"s, should_revalidate_sync_rows);
	tests.add("Should revalidate sync deletes"s, should_revalidate_sync_deletes);
	tests.add("Should offer every matching external tool"s, should_offer_every_matching_tool);

	// Issue #175 - sidebar history chart span
	tests.add("Should record history beyond ten years"s, should_record_history_beyond_ten_years);
	// Issue #175 - the chart and the search it runs must agree about which month an item is in
	tests.add("Should answer a timeline month with its own items"s, should_answer_a_timeline_month_with_its_own_items);
	tests.add("Should calculate history span from start year"s, should_calculate_history_span_from_start_year);
	tests.add("Should choose a history range that survives wrong dates"s,
	          should_choose_a_history_range_that_survives_wrong_dates);

	tests.add("Should persist media filter"s, should_persist_media_filter);

	// Issue #228 - the tag view's action field
	tests.add("Should round trip tag actions"s, should_round_trip_tag_actions);

	// Issue #229 - detail display per media type
	tests.add("Should persist detail display per media type"s, should_persist_detail_display_per_media_type);

	// Issue #135 - rating/labeling via NumPad
	tests.add("Should map numpad digits to rating keys"s, should_map_numpad_digits_to_rating_keys);

	// Issue #227 - removed default sidebar tags reappear after restart
	tests.add("Should only seed favorite tags on first run"s, should_only_seed_favorite_tags_on_first_run);

	tests.add("Should start safe only after repeated failures"s, should_start_safe_only_after_repeated_failures);
	tests.add("Should not count concurrent starts"s, should_not_count_concurrent_starts);
	tests.add("Should restore history selection"s, should_restore_history_selection);

	//
	// Selection and command enablement
	//
	tests.add("Should select correctly"s, should_select_items);
	tests.add("Should Enable based on selection"s, should_enable_based_on_selection);
	tests.add("Should toggle rating"s, should_toggle_rating);
	tests.add("Should leave the selection alone over empty space"s,
	          should_leave_the_selection_alone_over_empty_space);
	tests.add("Should target only visible items when filtered"s, should_target_only_visible_items_when_filtered);
	tests.add("Should keep shuffle exclusive with sorting"s, should_keep_shuffle_exclusive_with_sorting);
	tests.add("Should not claim one key for two commands"s, should_not_claim_one_key_for_two_commands);
	tests.add("Should label grouping and sorting"s, should_label_grouping_and_sorting);
	// Discussion #251 - the filter toolbar scrolls away
	tests.add("Should offer the items menu at every scroll position"s,
	          should_offer_the_items_menu_at_every_scroll_position);

	//
	// Address box editing session
	//
	tests.add("Should run an address editing session"s, should_run_an_address_editing_session);
	tests.add("Should accept a completion without running it"s, should_accept_a_completion_without_running_it);
	tests.add("Should commit the highlighted completion or the visible address"s,
	          should_commit_the_highlighted_completion_or_the_visible_address);
	tests.add("Should rank address completions"s, should_rank_address_completions);
	tests.add("Should offer recent searches for an empty address"s,
	          should_offer_recent_searches_for_an_empty_address);
	tests.add("Should discard a superseded completion"s, should_discard_a_superseded_completion);

	//
	// Browsing sequence
	//
	tests.add("Should step over items that cannot play"s, should_step_over_items_that_cannot_play);
	// Issue #250 - the cursor reset instead of landing after the set that left
	tests.add("Should land on what followed a removed set"s, should_land_on_what_followed_a_removed_set);
	tests.add("Should offer a slideshow only when something can play"s,
	          should_offer_a_slideshow_only_when_something_can_play);
	tests.add("Should move between sibling folders"s, should_move_between_sibling_folders);
	tests.add("Should decide what follows an item"s, should_decide_what_follows_an_item);

	//
	// Rename execution
	//
	tests.add("Should Rename"s, should_rename);
	tests.add("Should not overwrite during rename"s, should_not_overwrite_during_rename);
	tests.add("Should rename file case"s, should_rename_file_case);
	tests.add("Should rollback rename when sidecar fails"s, should_rollback_rename_when_sidecar_fails);
	tests.add("Should detect original path"s, should_detect_original_path);

	//
	// Collection membership and crash recording
	//
	tests.add("Should toggle collection entry"s, should_toggle_collection_entry);
	tests.add("Should record crashes"s, should_record_crashes);
}
