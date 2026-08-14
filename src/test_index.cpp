// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Index and database tests. Verifies the inverted/trigram/postings index, folder scanning
// and roots parsing, the SQLite cache schema and storage, thumbnail publication, staging and
// trimming, item property and metadata caching, duplicate and presence reporting, and cloud
// placeholder hydration.

#include "pch.h"

#include <sqlite3.h>

#include "test_fixtures.h"
#include "model_db_pack.h"
#include "model_postings.h"
#include "util_crash_files_db.h"
#include "app_util.h"
#include "metadata_exif.h"
#include "metadata_iptc.h"
#include "metadata_xmp.h"
#include "ui_elements.h"
#include "ui_map_common.h"

static void should_create_database_schema()
{
	sqlite3_initialize();

	const auto database_path = _temps.next_path(".db");
	sqlite3* database_handle = nullptr;
	const auto open_result = sqlite3_open(database_path.str().c_str(), &database_handle);
	const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> database(database_handle, sqlite3_close);

	if (open_result != SQLITE_OK)
	{
		throw test_assert_exception(std::format("Failed to create schema test database: {}",
		                                        database ? sqlite3_errmsg(database.get()) : "out of memory"));
	}

	const auto resource = load_resource(platform::resource_item::sql);
	const std::string schema(reinterpret_cast<const char*>(resource.data()), resource.size());
	if (sqlite3_exec(database.get(), schema.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK)
	{
		throw test_assert_exception(
			std::format("Failed to execute database schema: {}", sqlite3_errmsg(database.get())));
	}

	const auto query_text = [&database](const std::string_view sql)
	{
		sqlite3_stmt* statement_handle = nullptr;
		const auto prepare_result = sqlite3_prepare_v2(database.get(), sql.data(), static_cast<int>(sql.size()),
		                                               &statement_handle, nullptr);
		const std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(statement_handle, sqlite3_finalize);

		if (prepare_result != SQLITE_OK || sqlite3_step(statement.get()) != SQLITE_ROW)
		{
			throw test_assert_exception(
				std::format("Failed schema query '{}': {}", sql, sqlite3_errmsg(database.get())));
		}

		const auto* text = sqlite3_column_text(statement.get(), 0);
		return text == nullptr ? std::string{} : std::string(reinterpret_cast<const char*>(text));
	};

	assert_equal("wal", query_text("PRAGMA journal_mode"), "schema journal mode");
	assert_equal("1", query_text("PRAGMA synchronous"), "schema synchronous mode");
	assert_equal("item_imports,item_properties,item_thumbnails,web_service_cache",
	             query_text("SELECT group_concat(name, ',') FROM (SELECT name FROM sqlite_schema "
		             "WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name)"),
	             "schema tables");
	assert_equal("folder,name,properties,hash,media_position,flag,crc,last_scanned,last_indexed",
	             query_text("SELECT group_concat(name, ',') FROM pragma_table_info('item_properties')"),
	             "item_properties columns");
	assert_equal("folder,name,bitmap,cover_art,last_scanned",
	             query_text("SELECT group_concat(name, ',') FROM pragma_table_info('item_thumbnails')"),
	             "item_thumbnails columns");
	assert_equal("key,created_date,value",
	             query_text("SELECT group_concat(name, ',') FROM pragma_table_info('web_service_cache')"),
	             "web_service_cache columns");
	assert_equal("name,modified,size,imported",
	             query_text("SELECT group_concat(name, ',') FROM pragma_table_info('item_imports')"),
	             "item_imports columns");
	assert_equal("folder,name",
	             query_text("SELECT group_concat(name, ',') FROM "
		             "(SELECT name FROM pragma_table_info('item_properties') WHERE pk > 0 ORDER BY pk)"),
	             "item_properties primary key");
	assert_equal("folder,name",
	             query_text("SELECT group_concat(name, ',') FROM "
		             "(SELECT name FROM pragma_table_info('item_thumbnails') WHERE pk > 0 ORDER BY pk)"),
	             "item_thumbnails primary key");
	assert_equal("key",
	             query_text("SELECT group_concat(name, ',') FROM "
		             "(SELECT name FROM pragma_table_info('web_service_cache') WHERE pk > 0 ORDER BY pk)"),
	             "web_service_cache primary key");
	assert_equal("name,modified,size",
	             query_text("SELECT group_concat(name, ',') FROM "
		             "(SELECT name FROM pragma_table_info('item_imports') WHERE pk > 0 ORDER BY pk)"),
	             "item_imports primary key");
	assert_equal("ok", query_text("PRAGMA integrity_check"), "schema integrity");
}

static void should_store_thumbnails()
{
	const auto index_path = _temps.next_path();
	const auto file_path = test_files_folder.combine_file("Test.jpg");

	null_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);

	database db(index);
	db.open(index_path.folder(), index_path.file_name_without_extension());

	const auto i = load_item(index, file_path, true);
	db.perform_writes();

	const auto thumb = db.load_thumbnail(i->path());
	assert_equal(i->thumbnail(), thumb.thumb, "local loaded thumb");
}

static void should_store_cover_art()
{
	const auto index_path = _temps.next_path();
	const auto file_path = test_files_folder.combine_file("indy.mp4");

	null_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);

	database db(index);
	db.open(index_path.folder(), index_path.file_name_without_extension());

	const auto i = load_item(index, file_path, true);
	db.perform_writes();

	const auto thumb = db.load_thumbnail(i->path());
	assert_equal(i->cover_art(), thumb.cover_art, "local loaded cover art");
}

static void should_store_item_properties()
{
	const auto index_path = _temps.next_path();
	const auto file_path = test_files_folder.combine_file("Test.jpg");

	null_async_strategy as;
	location_cache locations;
	index_state index(as, locations);

	database db(index);
	db.open(index_path.folder(), index_path.file_name_without_extension());

	auto data = blob_from_file(file_path);
	const auto crc32c_expected = crypto::crc32c(data.data(), data.size());

	auto md = std::make_shared<prop::item_metadata>();
	md->album = "test"_c;
	md->orientation = ui::orientation::bottom_right;

	std::deque<item_db_write> writes;

	constexpr auto media_pos = 111.1;

	{
		item_db_write w;
		w.crc32c = crc32c_expected;
		w.md = md;
		w.media_position = media_pos;
		w.path = file_path;
		writes.emplace_back(std::move(w));
	}

	db.perform_writes(std::move(writes));
	db.load_index_values();

	auto item = index.find_item(file_path);
	auto item_md = item.metadata.load();

	assert_metadata(*md, *item_md, "index");
	assert_equal(crc32c_expected, item.crc32c, "index crc32");
	assert_equal(static_cast<int>(media_pos), static_cast<int>(item_md->media_position),
	             "index media position");
	assert_equal(md->orientation, item_md->orientation, "index orientation");

	const auto reloaded_crc = platform::file_crc32(file_path);
	assert_equal(reloaded_crc, item.crc32c, "platform::file_crc32 crc32");
}

// A database written before db_metadata_version records place text that cannot be told apart from
// text the file itself carried, so opening it must drop the cached metadata and force a rescan.
// Everything a rescan does not replace is kept, or the upgrade would cost a full thumbnail rebuild.
static void should_invalidate_cached_metadata_written_by_an_older_build()
{
	const auto index_path = _temps.next_path();
	const auto db_path = df::file_path(index_path.folder(), index_path.file_name_without_extension(), ".db");
	const auto file_path = test_files_folder.combine_file("Test.jpg");

	null_async_strategy as;
	location_cache locations;

	{
		index_state index(as, locations);
		database db(index);
		db.open(index_path.folder(), index_path.file_name_without_extension());

		auto md = std::make_shared<prop::item_metadata>();
		md->album = "test"_c;
		md->location_place = "Somewhere"_c;

		std::deque<item_db_write> writes;
		item_db_write w;
		w.path = file_path;
		w.md = md;
		w.crc32c = 0x1234u;
		w.media_position = 111.1;
		w.metadata_scanned = df::date_t(2020, 1, 1, 0, 0, 0);
		writes.emplace_back(std::move(w));

		db.perform_writes(std::move(writes));
		db.close();
	}

	// Stamp the file as one an older build left behind.
	{
		sqlite3* handle = nullptr;
		const auto open_result = sqlite3_open(db_path.str().c_str(), &handle);
		const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> raw(handle, sqlite3_close);

		if (open_result != SQLITE_OK ||
			sqlite3_exec(raw.get(), "PRAGMA user_version = 0;", nullptr, nullptr, nullptr) != SQLITE_OK)
		{
			throw test_assert_exception("Failed to reset the database metadata version"s);
		}
	}

	index_state index(as, locations);
	database db(index);
	db.open(index_path.folder(), index_path.file_name_without_extension());

	const auto item = index.find_item(file_path);
	const auto item_md = item.metadata.load();

	assert_equal(0ll, item.metadata_scanned.load().to_int64(), "scan state cleared");
	assert_equal(true, item_md == nullptr || str::is_empty(item_md->location_place), "stale place cleared");
	assert_equal(0x1234u, item.crc32c, "crc retained");
	assert_equal(true, item_md != nullptr && static_cast<int>(item_md->media_position) == 111,
	             "media position retained");
}

// A cache file this build cannot read must be replaced, not refused. Everything it holds can be
// rebuilt by re-indexing, while failing to open it closes the app before the user can reach the
// reset that would repair it.
static void should_replace_an_unreadable_database()
{
	const auto index_path = _temps.next_path();
	const auto db_path = df::file_path(index_path.folder(), index_path.file_name_without_extension(), ".db");
	const auto file_path = test_files_folder.combine_file("Test.jpg");

	{
		std::ofstream corrupt(platform::to_file_system_path(db_path), std::ios::binary | std::ios::trunc);
		corrupt << "SQLite format 3\0not a database at all";
	}

	null_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);

	database db(index);
	db.open(index_path.folder(), index_path.file_name_without_extension());

	assert_equal(true, db.is_open(), "replaced database open");
	assert_equal(false, db.has_errors(), "faults from the replaced file cleared");

	const auto md = std::make_shared<prop::item_metadata>();
	md->album = "replaced"_c;

	std::deque<item_db_write> writes;
	item_db_write w;
	w.path = file_path;
	w.md = md;
	w.crc32c = 0x4321u;
	writes.emplace_back(std::move(w));

	db.perform_writes(std::move(writes));
	db.load_index_values();

	assert_equal(0x4321u, index.find_item(file_path).crc32c, "replaced database usable");
}

// Without a database the app still has to run. Every operation must complete rather than strand
// its caller, and the write queue must keep draining or the index would hoard encoded thumbnails
// for a whole session against a database that can never accept them.
static void should_run_without_a_database()
{
	const auto missing_folder = _temps.next_path().folder().combine("missing-cache-folder");
	const auto file_path = test_files_folder.combine_file("Test.jpg");

	null_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);

	database db(index);
	db.open(missing_folder, "diffractor-cache");

	assert_equal(false, db.is_open(), "database not open");

	item_db_write w;
	w.path = file_path;
	w.md = std::make_shared<prop::item_metadata>();
	index.db_writes().enqueue(std::move(w));

	db.perform_writes();
	assert_equal(true, index.db_writes().dequeue_all().empty(), "write queue drained");

	assert_equal(true, db.load_item_imports().empty(), "no import history");
	db.writes_item_imports({});
	db.web_service_cache("key", "value");
	assert_equal(true, db.web_service_cache("key").empty(), "no web cache");
	db.clean({file_path});
	assert_equal(false, ui::is_valid(db.load_thumbnail(file_path).thumb), "no thumbnail");
}

// The scan loops hand their rows to the database in groups. One row at a time found the write queue
// empty every time, so it woke the database thread - and opened a transaction - once per file.
static void should_hand_scan_results_to_the_database_in_groups()
{
	// Stands in for the database worker, which drains the whole write queue on every pass. That eager
	// drain is what makes a per-row producer wake it again for the very next row.
	class draining_async_strategy final : public null_async_strategy
	{
	public:
		index_state* index = nullptr;
		int drains = 0;
		size_t rows = 0;

		void queue_database(std::function<void(database&)> f) override
		{
			++drains;
			rows += index->db_writes().dequeue_all().size();
		}
	};

	draining_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);
	as.index = &index;

	df::index_roots paths;
	paths.folders.emplace(test_files_folder);
	paths.excludes.emplace(test_files_folder.combine("excluded1"));
	paths.exclude_wildcards.emplace("exclud*2"_c);

	index.index_roots(paths);
	index.index_folders(test_token);
	index.scan_uncached(test_token);

	as.rows += index.db_writes().dequeue_all().size();

	// Without the fixtures actually being scanned the comparison below would pass vacuously.
	assert_equal(expected_cached_item_count, index.stats.media_item_count, "cached item count");
	assert_equal(true, as.rows >= 40, std::format("rows written: {}", as.rows));

	// One drain per row is the defect. Grouping cannot need more than one per 64-row group.
	assert_equal(true, as.drains <= static_cast<int>(as.rows / 8),
	             std::format("database drains {} for {} rows", as.drains, as.rows));
}

static void should_pack_item_properties()
{
	const auto file_path = test_files_folder.combine_file("Test.jpg");
	const auto md = extract_properties(file_path);
	md->album = "test"_c;
	md->orientation = ui::orientation::bottom_right;

	metadata_packer packer;
	packer.pack(md);

	const auto unpacked = std::make_shared<prop::item_metadata>();

	metadata_unpacker unpacker(packer.cdata());
	unpacker.unpack(unpacked);

	assert_metadata(*md, *unpacked, "index");
	assert_equal(md->orientation, unpacked->orientation, "index orientation");
}

static void should_store_webservice_results()
{
	const auto index_path = _temps.next_path();

	null_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);
	database db(index);
	db.open(index_path.folder(), index_path.file_name_without_extension());

	constexpr auto key = "key   xxxxxxxxxx";
	const auto value = long_text;
	db.web_service_cache(key, value);
	const auto result = db.web_service_cache(key);

	assert_equal(result, value, "web_service_cache");
}

static void should_bound_webservice_cache()
{
	const auto index_path = _temps.next_path();

	null_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);
	database db(index);
	db.open(index_path.folder(), index_path.file_name_without_extension());

	for (int i = 0; i <= 1000; ++i)
	{
		db.web_service_cache(std::format("key{}", i), "value");
	}

	assert_equal(true, db.web_service_cache("key0").empty(), "oldest web cache entry evicted");
	assert_equal("value", db.web_service_cache("key1000"), "newest web cache entry retained");
}

static void should_index(shared_test_context& stc)
{
	stc.lazy_load_index();

	assert_equal(expected_cached_item_count, stc.test_index.stats.media_item_count, "cached item count");

	const auto expected_md = expected_test_jpg();
	expected_md->file_name = "Test.jpg"_c;
	assert_metadata(*expected_md, *metadata_from_cache(stc.test_index, test_files_folder.combine_file("Test.jpg")),
	                "Test.jpg");

	const auto actual = metadata_from_cache(stc.test_index, test_files_folder.combine_file("Gherkin.CR2"));
	assert_equal("Canon", actual->camera_manufacturer, "camera_manufacturer");
	assert_equal("United Kingdom", actual->location_country, "location_country");
	assert_equal("© Mark Ridgwell", actual->copyright_notice, "copyright_notice");
	assert_equal("\"Mark Ridgwell\"", actual->copyright_creator, "copyright_creator");
}

static void should_parse_roots(shared_test_context& stc)
{
	df::index_roots roots1;
	parse_more_folders(roots1, test_files_folder.text());

	assert_equal(0_z, roots1.files.size(), "parsed files");
	assert_equal(0_z, roots1.excludes.size(), "parsed excludes");
	assert_equal(1_z, roots1.folders.size(), "parsed folder");
	assert_equal(test_files_folder.text(), roots1.folders.begin()->text(), "parsed folder");

	df::index_roots roots2;
	const auto exclude_files_folder = test_files_folder.combine("excluded1");
	parse_more_folders(roots2, std::format(" - {}\n{}", exclude_files_folder.text(), test_files_folder.text()));

	assert_equal(0_z, roots2.files.size(), "parsed files");
	assert_equal(1_z, roots2.excludes.size(), "parsed excludes");
	assert_equal(1_z, roots2.folders.size(), "parsed folder");
	assert_equal(test_files_folder.text(), roots2.folders.begin()->text(), "parsed folder");
	assert_equal(exclude_files_folder.text(), roots2.excludes.begin()->text(), "parsed exclude");

	df::index_roots roots3;
	parse_more_folders(roots3, std::format("- secret\n{}\n -exclude*", test_files_folder.text()));

	assert_equal(0_z, roots3.files.size(), "parsed files");
	assert_equal(0_z, roots3.excludes.size(), "parsed excludes");
	assert_equal(1_z, roots3.folders.size(), "parsed folder");
	assert_equal(2_z, roots3.exclude_wildcards.size(), "parsed exclude wildcards");

	const std::vector<str::cached> exclude_wildcards(roots3.exclude_wildcards.begin(), roots3.exclude_wildcards.end());

	assert_equal(test_files_folder.text(), roots3.folders.begin()->text(), "parsed folder");
	assert_equal("exclude*", exclude_wildcards[0], "parsed exclude");
	assert_equal("secret", exclude_wildcards[1], "parsed exclude");
}

static void should_parse_drive_label_roots(shared_test_context& stc)
{
	// A device label (volume name) entered in the collection list should resolve
	// to the matching drive by its volume label - not by its drive letter.
	platform::drives drives;

	platform::drive_t d;
	d.name = "X:\\";
	d.vol_name = "DiffractorTestLabel";
	drives.emplace_back(d);

	// A label matching the drive's volume name resolves to that drive's path
	// (rather than being stored as the bare label text).
	df::index_roots roots;
	parse_more_folders(roots, "DiffractorTestLabel", drives);
	assert_equal(1_z, roots.folders.size(), "matching label count");
	assert_equal("X:\\", roots.folders.begin()->text(), "device label resolved to drive path");
}

// Verifies the item-level reload predicate independently of the scanner and database. Thumbnail
// loading is deferred until the DB lookup completes; after that, a missing/unstamped thumbnail is
// eligible, a thumbnail stamped at the file modification time is current, and a later file change
// makes that same thumbnail stale.
static void should_not_reload_thumb_when_valid()
{
	const auto load_path = test_files_folder.combine_file("test.jpg");

	const df::date_t date(1972, 5, 25);
	const df::date_t date2(1972, 5, 26);

	files ff;
	const auto loaded = ff.load(load_path, false);

	const auto i_local = std::make_shared<df::item_element>(load_path, make_index_file_info(date));
	assert_equal(false, i_local->should_load_thumbnail(), "should not load by default");

	i_local->begin_db_thumbnail_query();
	assert_equal(true, i_local->should_load_thumbnail(), "should load after db load");

	i_local->thumbnail(loaded.i, nullptr);
	assert_equal(true, i_local->should_load_thumbnail(), "should load without timestamp");

	i_local->thumbnail(loaded.i, nullptr, date);
	assert_equal(false, i_local->should_load_thumbnail(), "should not load a current timestamped thumbnail");

	i_local->update(load_path, make_index_file_info(date2));
	assert_equal(true, i_local->should_load_thumbnail(), "should if date changes");

	i_local->update(load_path, make_index_file_info(date.add_day(-1)));
	assert_equal(true, i_local->should_load_thumbnail(), "should if date changes backward");
}

static void should_reuse_persisted_hover_thumbnail_until_video_changes()
{
	const auto index_path = _temps.next_path();
	const auto image_path = test_files_folder.combine_file("test.jpg");
	const df::file_path video_path(test_files_folder, "hover-preview.mp4");
	const df::date_t modified(2026, 7, 31);

	files ff;
	const auto loaded = ff.load(image_path, false);
	assert_equal(true, is_valid(loaded.i), "hover thumbnail test image");

	null_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);
	database db(index);
	db.open(index_path.folder(), index_path.file_name_without_extension());

	index.save_thumbnail(video_path, loaded.i, {}, modified);
	db.perform_writes();

	const auto reloaded_item = std::make_shared<df::item_element>(video_path, make_index_file_info(modified));
	df::item_set items;
	reloaded_item->add_to(items);
	reloaded_item->begin_db_thumbnail_query();
	db.load_thumbnails(index, database::make_thumbnail_requests(items));

	assert_equal(true, reloaded_item->has_thumb(), "hover thumbnail survives database round trip");
	assert_equal(false, reloaded_item->should_load_thumbnail(), "unchanged video reuses hovered thumbnail");

	reloaded_item->update(video_path, make_index_file_info(modified.add_day(1)));
	assert_equal(true, reloaded_item->should_load_thumbnail(), "modified video invalidates hovered thumbnail");

	reloaded_item->update(video_path, make_index_file_info(modified.add_day(-1)));
	assert_equal(true, reloaded_item->should_load_thumbnail(), "older modified date invalidates hovered thumbnail");
}

// Exercises the complete two-stage thumbnail lifecycle. The metadata scan may cache a provisional
// embedded thumbnail without a current thumbnail timestamp. The visible-item scan then generates
// the full thumbnail, stores it with its scan timestamp, and a fresh item must reuse that DB row
// without opening the source again. The final stale control proves the test detects a required
// regeneration when the source modification time advances.
static void should_reload_thumb_after_scan()
{
	files ff;
	const auto index_path = _temps.next_path();
	null_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);

	database db(index);
	db.open(index_path.folder(), index_path.file_name_without_extension());
	build_index(index, db);

	auto path_test = df::file_path(test_files_folder, "Test.jpg");
	const auto path_sony = df::file_path(test_files_folder, "Sony.jpg");

	const auto test_item = load_item(index, path_test, false);
	const auto sony_item = load_item(index, path_sony, false);

	assert_equal(false, test_item->should_load_thumbnail(), "should_load_thumbnail for test.jpg");
	assert_equal(false, sony_item->should_load_thumbnail(), "should_load_thumbnail for sony.jpg");

	df::item_set items;
	items._items = {test_item, sony_item};

	// Stage 1: a metadata refresh persists no thumbnail at all, so the item is still waiting on the
	// visible-item pass for one.
	const auto metadata_refreshed_initially = index.scan_items(items, false, false, false, false, test_token, true);
	db.perform_writes();
	assert_equal(true, metadata_refreshed_initially, "forced metadata refresh should require regrouping after scan");

	assert_equal(false, test_item->should_load_thumbnail(), "should_load_thumbnail for test.jpg");
	assert_equal(false, sony_item->should_load_thumbnail(), "should_load_thumbnail for sony.jpg");

	items.for_all([](const auto& item) { item->begin_db_thumbnail_query(); });
	items.for_all([](const auto& item) { item->begin_db_thumbnail_query(); });
	db.load_thumbnails(index, database::make_thumbnail_requests(items));

	assert_equal(true, test_item->should_load_thumbnail(),
	             "should_load_thumbnail for test.jpg after db load_thumbnails");
	assert_equal(true, sony_item->should_load_thumbnail(),
	             "should_load_thumbnail for sony.jpg after db load_thumbnails");

	// Stage 2: loading thumbnails from the full files is what generates and stores the images, with
	// scan timestamps that are current relative to the source modification times.
	const auto metadata_refreshed_for_thumbnails = index.scan_items(items, true, false, false, false, test_token);
	db.perform_writes();
	assert_equal(false, metadata_refreshed_for_thumbnails,
	             "current metadata should not force regrouping after thumbnail generation");

	assert_equal(false, test_item->should_load_thumbnail(),
	             "should_load_thumbnail for test.jpg after index load_thumbnails");
	assert_equal(false, sony_item->should_load_thumbnail(),
	             "should_load_thumbnail for sony.jpg after index load_thumbnails");

	const auto reloaded_item = std::make_shared<df::item_element>(path_test,
	                                                              make_index_file_info(test_item->file_modified()));
	df::item_set reloaded_items;
	reloaded_item->add_to(reloaded_items);
	reloaded_items.for_all([](const auto& item) { item->begin_db_thumbnail_query(); });
	db.load_thumbnails(index, database::make_thumbnail_requests(reloaded_items));

	// Simulate a later application session: DB hydration alone must make the generated thumbnail
	// current, and the real conditional scan path must perform no thumbnail write.
	assert_equal(true, reloaded_item->has_thumb(), "generated thumbnail should survive database round trip");
	assert_equal(false, reloaded_item->should_load_thumbnail(),
	             "generated database thumbnail should not reload from full file");

	const auto thumbs_saved_before_valid_scan = index.stats.thumbs_saved;
	index.scan_items(reloaded_items, true, false, true, false, test_token);
	db.perform_writes();
	assert_equal(thumbs_saved_before_valid_scan, index.stats.thumbs_saved,
	             "valid database thumbnail should skip full-file thumbnail generation");

	// Sensitivity control: advancing the source timestamp must make the cache stale and produce
	// exactly one replacement thumbnail through the same conditional scan path.
	reloaded_item->update(path_test, make_index_file_info(reloaded_item->thumbnail_timestamp().add_day(1)));
	assert_equal(true, reloaded_item->should_load_thumbnail(), "modified file should make database thumbnail stale");
	const auto metadata_refreshed_after_modify =
		index.scan_items(reloaded_items, true, false, true, false, test_token);
	db.perform_writes();
	assert_equal(false, metadata_refreshed_after_modify,
	             "thumbnail staleness alone should not force regrouping after regeneration");
	assert_equal(thumbs_saved_before_valid_scan + 1, index.stats.thumbs_saved,
	             "stale database thumbnail should regenerate from the full file");
}

// The cost assertion behind the write-suppression design: a metadata-only edit must read the file
// exactly once, through the write's own coherent handle, and must leave the cached index record
// current. Asserting on the cached record matters more than asserting on the item: the background
// safety net refreshes the folder from the filesystem, so a stale record is masked whenever that
// refresh happens to run, and costs a re-read and a re-decode whenever it does not.
static void should_not_reread_after_metadata_write()
{
	// A private folder: the shared suite temp folder would drag every other test's files into the
	// scan and make this test's cost depend on test order.
	const auto temp_folder = _temps.next_folder("write-suppression");
	const auto file_path = _temps.next_path_in(temp_folder, ".jpg");
	platform::copy_file(test_files_folder.combine_file("Test.jpg"), file_path, false, false);

	null_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);

	const auto item = load_item(index, file_path, true);
	assert_equal(false, item->should_load_thumbnail(), "thumbnail is current before the write");
	assert_equal(false, index.needs_scan(item), "record is current before the write");

	const auto modified_before = item->file_modified();
	item->retain_thumbnail_across_next_write();

	const auto request = index_state::make_scan_request(item, true, false);
	const auto xmp = detect_xmp_sidecar(file_path);

	metadata_edits edits;
	edits.rating = 3;

	files ff;
	const auto scans_before = df::file_perf.scans.load();
	const auto result = ff.update(file_path, edits, {}, {}, false, xmp,
	                              index_state::make_rescan_spec(request, xmp, false, false));

	assert_equal(true, result.success(), "the write succeeds");
	assert_equal(true, result.scanned, "the write scans back through its own handle");
	assert_equal(true, result.coherent, "the write-back scan is coherent");
	assert_equal(scans_before + 1, df::file_perf.scans.load(), "the write reads the file exactly once");

	const auto force = index.apply_write_scan(request, result);
	assert_equal(false, force, "a coherent write does not force a rescan");

	const auto written_modified = df::date_t(result.modified);
	assert_equal(true, written_modified == index.find_item(file_path).file_modified.load(),
	             "the cached index record carries the written modified time");
	assert_equal(true, modified_before < item->file_modified(), "the item carries the written modified time");
	assert_equal(false, item->should_load_thumbnail(), "the written item does not re-request its thumbnail");
	assert_equal(false, index.needs_scan(item), "the cached record does not report needing a scan");
	assert_equal(scans_before + 1, df::file_perf.scans.load(), "publishing the write costs no extra read");
}

// Two batches can hold one path at once, so a claim is counted rather than a set membership. The
// first release must not open the file up while the second batch's write is still queued.
static void should_count_overlapping_write_claims()
{
	const auto temp_folder = _temps.next_folder("write-claims");
	const auto file_path = _temps.next_path_in(temp_folder, ".jpg");
	platform::copy_file(test_files_folder.combine_file("Test.jpg"), file_path, false, false);

	null_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);

	const auto item = load_item(index, file_path, true);
	df::item_set items;
	items.add(item);

	const std::vector claimed{file_path};
	index.claim_for_write(claimed);
	index.claim_for_write(claimed);

	index.release_write_claim(claimed);

	const auto scans_after_first_release = df::file_perf.scans.load();
	index.queue_scan_modified_items(items, true);
	assert_equal(scans_after_first_release, df::file_perf.scans.load(),
	             "a still-claimed path must stay deferred after an overlapping batch releases");

	index.release_write_claim(claimed);
	assert_equal(true, scans_after_first_release < df::file_perf.scans.load(),
	             "the deferred scan runs once the last claim is released");
}

// Only selector folders are live-watched, so a search that names no folder - related items,
// duplicates, a tag, a date - has nothing watching it. Deleting from one of those views has to tell
// the index itself, or the view keeps listing a file that is gone.
// The index is a cache, so an in-app change has to both correct it and ask for the search to be run
// again. Asking unconditionally would re-open the search on every folder walked, so the request is
// tied to a folder actually differing.
static void should_request_a_re_query_only_when_a_folder_changed(shared_test_context& stc)
{
	const df::file_path source(test_files_folder, "Test.jpg");
	const auto temp_folder = _temps.next_folder("requery");
	const auto removed = _temps.next_path_in(temp_folder, ".jpg");
	platform::copy_file(source, removed, false, false);

	deferred_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);

	df::index_roots roots;
	roots.folders.emplace(temp_folder);
	index.index_roots(roots);
	index.index_folders(test_token);

	df::unique_folders touched;
	touched.emplace(temp_folder);

	// Nothing has changed on disk, so nothing needs re-running.
	index.queue_validate_changed_folders(touched);
	assert_equal(true, as.run_next(async_queue::scan_folder), "validate ran");
	assert_equal(false, as.was_invalidated(view_invalid::refresh_items), "an unchanged folder asks for nothing");

	assert_equal(true, platform::delete_items({removed}, {}, false).success(), "delete");

	index.queue_validate_changed_folders(std::move(touched));
	assert_equal(true, as.run_next(async_queue::scan_folder), "validate ran");
	assert_equal(true, as.was_invalidated(view_invalid::refresh_items), "a changed folder asks for the re-query");
}

static void should_drop_deleted_items_from_a_search_with_no_folder(shared_test_context& stc)
{
	const df::file_path source(test_files_folder, "Test.jpg");
	const auto temp_folder = _temps.next_folder("deleted");
	const auto kept = _temps.next_path_in(temp_folder, ".jpg");
	const auto removed = _temps.next_path_in(temp_folder, ".jpg");

	platform::copy_file(source, kept, false, false);
	platform::copy_file(source, removed, false, false);

	null_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);
	auto cache_path = _temps.next_path();
	database db(index);
	db.open(cache_path.folder(), cache_path.file_name_without_extension());

	df::index_roots roots;
	roots.folders.emplace(temp_folder);
	index.index_roots(roots);
	index.index_folders(test_token);
	index.scan_uncached(test_token);

	// No selector: this is the shape every relation, duplicate and tag search has.
	const auto search = df::search_t::parse("@photo");
	assert_equal(2, count_search_results(index, search), "both copies are listed");

	assert_equal(true, platform::delete_items({removed}, {}, false).success(), "delete");

	df::unique_folders touched;
	touched.emplace(removed.folder());
	index.queue_validate_changed_folders(std::move(touched));

	assert_equal(1, count_search_results(index, search), "the deleted copy is gone from the list");
}

static void should_detect_duplicates(shared_test_context& stc)
{
	files ff;
	null_async_strategy as;
	location_cache locations;
	index_state index(as, locations);
	auto cache_path = _temps.next_path();
	database db(index);
	db.open(cache_path.folder(), cache_path.file_name_without_extension());
	build_index(index, db);

	const auto path1 = df::file_path(test_files_folder, "Test.jpg");
	const auto path2 = df::file_path(test_files_folder, "Test90.jpg");
	const auto path3 = df::file_path(test_files_folder, "Test180.jpg");
	const auto path4 = df::file_path(test_files_folder, "Test270.jpg");
	const auto path5 = df::file_path(test_files_folder, "Small.jpg");
	const auto path_sony = df::file_path(test_files_folder, "Sony.jpg");

	const auto test_item1 = std::make_shared<df::item_element>(path1, index.find_item(path1));
	const auto test_item2 = std::make_shared<df::item_element>(path2, index.find_item(path2));
	const auto test_item3 = std::make_shared<df::item_element>(path3, index.find_item(path3));
	const auto test_item4 = std::make_shared<df::item_element>(path4, index.find_item(path4));
	const auto test_item5 = std::make_shared<df::item_element>(path5, index.find_item(path5));
	const auto sony_item = std::make_shared<df::item_element>(path_sony, index.find_item(path_sony));

	df::item_set items({test_item1, test_item2, test_item3, test_item4, test_item5, sony_item});
	items.for_all([](const auto& item) { item->begin_db_thumbnail_query(); });
	db.load_thumbnails(index, database::make_thumbnail_requests(items));

	index.scan_item(test_item1, true, false);
	index.scan_item(test_item2, true, false);
	index.scan_item(test_item3, true, false);
	index.scan_item(test_item4, true, false);
	index.scan_item(test_item5, true, false);
	index.scan_item(sony_item, true, false);

	index.update_predictions();

	// Small.jpg is Test.jpg resized, and Test90/180/270 are the same picture turned. None share a
	// name, size or CRC with the original, so the perceptual stage is the only thing that can see
	// them. A quarter turn is a common grading step, so a turned copy is claimed as a copy.
	const auto test_group = index.find_item(test_item1->path()).duplicates.load();
	const auto small_group = index.find_item(test_item5->path()).duplicates.load();

	assert_equal(5u, test_group.count, "a resized copy and three turns are duplicates");
	assert_equal(5u, small_group.count, "and so is the original");
	assert_equal(true, test_group.group != 0 && test_group.group == small_group.group, "one duplicate group");

	// The grade is the claim. A re-encode is only ever "possible", and saying so is what separates it
	// from a byte-identical copy on a surface the user deletes from.
	assert_equal(static_cast<int>(df::copy_grade::same_picture), static_cast<int>(test_group.grade),
	             "a resized copy is graded as the same picture");
	assert_equal(static_cast<int>(df::copy_grade::same_picture), static_cast<int>(small_group.grade),
	             "and so is the original");

	// A turned copy joins the set; an unrelated photo taken at the same second still does not.
	assert_equal(5u, index.find_item(test_item2->path()).duplicates.load().count, "a quarter turn is a copy");
	assert_equal(5u, index.find_item(test_item3->path()).duplicates.load().count, "a half turn is a copy");
	assert_equal(1u, index.find_item(sony_item->path()).duplicates.load().count, "an unrelated photo is not");

	// Parity: `@duplicates` and a related search read the one duplicate group, so a picture found by
	// the perceptual stage is reported by both rather than only by the feature that computed it.
	assert_equal(5, count_search_results(index, "@duplicates"), "@duplicates lists the set");

	df::related_info r;
	r.load(test_item1);

	std::string related_summary;

	index.query_items(df::search_t().related(r), [&related_summary, &test_item5](
		                  const index_state::query_item_results& items, bool)
	                  {
		                  for (const auto& i : items)
		                  {
			                  if (i.path != test_item5->path()) continue;
			                  related_summary = df::related_axis_of(i.match.type) == df::related_axis::duplicate
				                                    ? "duplicate"
				                                    : "other";
		                  }
	                  }, test_token);

	assert_equal("duplicate", related_summary,
	             "related reports the resized copy as a possible copy, not a coincidence of time");
}

// Presence, duplicate search and related items are one relation asked at three scales, so none of
// them may see a copy the others deny. The perceptual grade is the one that was visible to duplicate
// search and related items while presence was blind to it.
static void should_report_a_re_encoded_copy_to_presence()
{
	null_async_strategy as;
	location_cache locations;
	index_state index(as, locations);
	const auto cache_path = _temps.next_path();
	database db(index);
	db.open(cache_path.folder(), cache_path.file_name_without_extension());

	// A collection holding the full-size picture only, so an outside copy of the resized one cannot
	// match any member by name, size or checksum. The picture is the only evidence left.
	const auto collection_folder = _temps.next_path().folder().combine("re-encode-collection");
	const auto outside_folder = _temps.next_path().folder().combine("re-encode-outside");
	platform::create_folder(collection_folder);
	platform::create_folder(outside_folder);

	const auto member_path = collection_folder.combine_file("original.jpg");
	const auto outside_path = outside_folder.combine_file("resized-elsewhere.jpg");
	platform::copy_file(test_files_folder.combine_file("Test.jpg"), member_path, false, false);
	platform::copy_file(test_files_folder.combine_file("Small.jpg"), outside_path, false, false);

	df::index_roots roots;
	roots.folders.emplace(collection_folder);
	index.index_roots(roots);
	index.index_folders(test_token);
	index.scan_uncached(test_token);
	index.update_predictions();

	const auto outside_item = std::make_shared<df::item_element>(outside_path, index.find_item(outside_path));
	index.scan_item(outside_item, true, false);
	index.queue_update_presence(df::item_set({outside_item}));

	const auto presence = outside_item->presence();

	assert_equal(true,
	             presence == item_presence::similar_in || presence == item_presence::newer_in ||
	             presence == item_presence::older_in,
	             "presence reports a possible copy of a re-encode rather than an absence");
	assert_equal(static_cast<int>(df::copy_grade::same_picture),
	             static_cast<int>(outside_item->duplicates().grade),
	             "presence grades it the same way duplicate search would");
}

// Presence and duplicate search are one relation asked at two scales, so a quarter turn has to read
// the same from both. This is the surface that was blind to the perceptual grade before.
static void should_report_a_rotated_copy_to_presence()
{
	null_async_strategy as;
	location_cache locations;
	index_state index(as, locations);
	const auto cache_path = _temps.next_path();
	database db(index);
	db.open(cache_path.folder(), cache_path.file_name_without_extension());

	const auto collection_folder = _temps.next_path().folder().combine("rotate-collection");
	const auto outside_folder = _temps.next_path().folder().combine("rotate-outside");
	platform::create_folder(collection_folder);
	platform::create_folder(outside_folder);

	// The collection holds the upright picture; the outside file is the same picture turned, so it
	// shares no name, size or checksum with the member and its stored extent is transposed. The
	// fixtures are 1024x683 and 672x1024: lossless JPEG rotation trims to the MCU grid, so a turned
	// copy is not an exact transpose, and the shape narrowing has to survive that.
	const auto member_path = collection_folder.combine_file("upright.jpg");
	const auto outside_path = outside_folder.combine_file("turned-elsewhere.jpg");
	platform::copy_file(test_files_folder.combine_file("Test.jpg"), member_path, false, false);
	platform::copy_file(test_files_folder.combine_file("Test90.jpg"), outside_path, false, false);

	df::index_roots roots;
	roots.folders.emplace(collection_folder);
	index.index_roots(roots);
	index.index_folders(test_token);
	index.scan_uncached(test_token);
	index.update_predictions();

	const auto outside_item = std::make_shared<df::item_element>(outside_path, index.find_item(outside_path));
	index.scan_item(outside_item, true, false);
	index.queue_update_presence(df::item_set({outside_item}));

	const auto presence = outside_item->presence();

	assert_equal(true,
	             presence == item_presence::similar_in || presence == item_presence::newer_in ||
	             presence == item_presence::older_in,
	             "presence reports a possible copy of a rotation rather than an absence");
	assert_equal(static_cast<int>(df::copy_grade::same_picture),
	             static_cast<int>(outside_item->duplicates().grade),
	             "presence grades a turned copy the same way duplicate search would");
}

static void should_require_equal_size_for_duplicate_crc(){
	df::index_file_item first;
	first.ft = files::file_type_from_name("first.jpg");
	first.name = str::cache("first.jpg");
	first.size = df::file_size(100);
	first.crc32c = 1234;

	df::index_file_item second;
	second.ft = files::file_type_from_name("second.jpg");
	second.name = str::cache("second.jpg");
	second.size = df::file_size(200);
	second.crc32c = 1234;

	assert_equal(false, is_dup_match(&first, &second), "same CRC with different sizes");
	second.size = first.size;
	assert_equal(true, is_dup_match(&first, &second), "same CRC and size");
}

static void should_update_collection_presence(shared_test_context& stc)
{
	stc.lazy_load_index();

	const auto source_path = test_files_folder.combine_file("Test.jpg");
	const auto source_info = stc.test_index.find_item(source_path);
	const auto external_root = _temps.next_path().folder().combine("presence-test");

	auto make_external = [&](const std::string_view folder_name, df::index_file_item info,
	                         const std::string_view file_name = "Test.jpg")
	{
		return std::make_shared<df::item_element>(
			df::file_path(external_root.combine(folder_name), file_name), info);
	};

	const auto in_collection = std::make_shared<df::item_element>(source_path, source_info);
	const auto possible_copy = make_external("same", source_info);

	auto newer_info = source_info;
	newer_info.crc32c = 0;
	newer_info.file_modified = df::date_t(2099, 1, 1);
	const auto possible_older_copy = make_external("newer-outside", newer_info);

	auto older_info = source_info;
	older_info.crc32c = 0;
	older_info.file_modified = df::date_t(1990, 1, 1);
	const auto possible_newer_copy = make_external("older-outside", older_info);

	const auto absent = load_item(stc.test_index,
	                              test_files_folder.combine("excluded1").combine_file("document.png"), false);
	auto incomplete_info = source_info;
	incomplete_info.crc32c = 0;
	const auto incomplete = make_external("incomplete", incomplete_info, "not-present.jpg");

	const auto folder_info = std::make_shared<df::index_folder_item>();
	const auto folder = std::make_shared<df::item_element>(external_root.combine("folder"), folder_info);
	folder->presence(item_presence::similar_in);
	folder->duplicates({42, 2});

	stc.test_index.queue_update_presence(df::item_set({
		in_collection, possible_copy, possible_older_copy, possible_newer_copy, absent, incomplete, folder
	}));

	assert_equal(static_cast<int>(item_presence::this_in), static_cast<int>(in_collection->presence()),
	             "collection member presence");
	assert_equal(source_info.duplicates.load().count, in_collection->duplicates().count,
	             "collection member duplicate summary");
	assert_equal(static_cast<int>(item_presence::similar_in), static_cast<int>(possible_copy->presence()),
	             "possible copy presence");
	assert_equal(static_cast<int>(item_presence::older_in), static_cast<int>(possible_older_copy->presence()),
	             "possible older collection copy presence");
	assert_equal(static_cast<int>(item_presence::newer_in), static_cast<int>(possible_newer_copy->presence()),
	             "possible newer collection copy presence");
	assert_equal(static_cast<int>(item_presence::not_in), static_cast<int>(absent->presence()),
	             "no possible collection copy presence");
	assert_equal(static_cast<int>(item_presence::unknown), static_cast<int>(incomplete->presence()),
	             "incomplete presence remains provisional");
	assert_equal(static_cast<int>(item_presence::unknown), static_cast<int>(folder->presence()),
	             "folder has no file presence");
	assert_equal(0u, folder->duplicates().count, "folder has no duplicate summary");

	std::atomic_int scan_version = 0;
	const df::cancel_token canceled_scan(scan_version);
	const df::cancel_token current_scan(scan_version);
	stc.test_index.scan_uncached(canceled_scan);
	stc.test_index.queue_update_presence(df::item_set({absent}));
	assert_equal(static_cast<int>(item_presence::unknown), static_cast<int>(absent->presence()),
	             "absence remains provisional after an incomplete collection scan");

	stc.test_index.scan_uncached(current_scan);
	stc.test_index.queue_update_presence(df::item_set({absent}));
	assert_equal(static_cast<int>(item_presence::not_in), static_cast<int>(absent->presence()),
	             "absence is published after the collection scan completes");
}

static void should_discard_stale_presence_result()
{
	deferred_async_strategy async;
	location_cache locations;
	index_state index(async, locations);

	df::index_file_item initial;
	initial.name = str::cache("presence.jpg");
	initial.ft = files::file_type_from_name(initial.name);
	initial.size = df::file_size(100);
	initial.file_modified = df::date_t(2020, 1, 1);
	const auto item = std::make_shared<df::item_element>(df::file_path("c:\\presence.jpg"), initial);
	item->presence(item_presence::similar_in);

	index.queue_update_presence(df::item_set({item}));
	auto changed = initial;
	changed.size = df::file_size(200);
	changed.file_modified = df::date_t(2021, 1, 1);
	item->update(item->path(), changed);

	assert_equal(true, async.run_next(async_queue::index_presence_single), "presence work was queued");
	async.drain_ui();

	assert_equal(static_cast<int>(item_presence::similar_in), static_cast<int>(item->presence()),
	             "stale presence result was discarded");
}

static void should_discard_stale_scan_item_update()
{
	const auto file_path = _temps.next_path(".jpg");
	platform::copy_file(test_files_folder.combine_file("Test.jpg"), file_path, false, true);

	deferred_async_strategy async;
	const location_cache locations;
	index_state index(async, locations);
	index.validate_folder(file_path.folder(), true, platform::now());

	const auto indexed = index.find_item(file_path);
	const auto item = std::make_shared<df::item_element>(file_path, indexed);
	index.scan_item(item, false, false);

	const auto replacement_path = file_path.extension(".changed.jpg");
	item->update(replacement_path, indexed);
	async.drain_ui();

	assert_equal(true, item->path() == replacement_path, "stale scan item update was discarded");
}

static void should_discard_stale_crc_result()
{
	deferred_async_strategy async;
	location_cache locations;
	index_state index(async, locations);

	df::index_file_item initial;
	initial.name = str::cache("crc.jpg");
	initial.ft = files::file_type_from_name(initial.name);
	initial.size = df::file_size(100);
	const auto path = df::file_path("c:\\crc.jpg");
	const auto item = std::make_shared<df::item_element>(path, initial);

	index.publish_crc(item, path, initial.size, df::item_online_status::disk, 0, 123);
	auto changed = initial;
	changed.size = df::file_size(200);
	item->update(path, changed);
	async.drain_ui();

	assert_equal(0u, item->crc32c(), "stale CRC result was discarded");
}

static void should_detect_rotation(shared_test_context& stc)
{
	files ff;
	const auto index_path = _temps.next_path();
	null_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);

	database db(index);
	db.open(index_path.folder(), index_path.file_name_without_extension());

	df::index_roots paths;
	paths.folders.emplace(test_files_folder);

	index.index_roots(paths);
	index.index_folders(test_token);

	const auto path_test = df::file_path(test_files_folder, "exif-rotated.jpg");
	const auto test_item = std::make_shared<df::item_element>(path_test, index.find_item(path_test));

	assert_equal(ui::orientation::top_left, test_item->layout_orientation());

	df::item_set items;
	items._items = {test_item};

	index.scan_items(items, false, false, false, false, test_token);
	db.perform_writes();

	assert_equal(ui::orientation::right_top, test_item->layout_orientation());

	// Indexing stores no thumbnail, so the item only asks for one once it is visible - which is what
	// clears db_query_pending. Without this the metadata scan above is the last thing that runs and
	// the thumbnail assertion below is testing nothing.
	items.for_all([](const auto& item) { item->begin_db_thumbnail_query(); });
	db.load_thumbnails(index, database::make_thumbnail_requests(items));

	assert_equal(ui::orientation::right_top, test_item->layout_orientation());

	index.scan_items(items, true, false, false, false, test_token);
	db.perform_writes();

	assert_equal(ui::orientation::right_top, test_item->layout_orientation());
	assert_equal(true, is_valid(test_item->thumbnail()), "the visible-item scan produced a thumbnail");
	assert_equal(ui::orientation::right_top, test_item->thumbnail()->orientation());
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Index concurrency (thread-synchronisation regression)
// The indexing thread rebuilds the folder index (index_folders) - replacing the summary folder
// sets and flipping the per-folder is_in_collection/is_excluded flags under the summary/map locks -
// while UI and worker threads read the same state through the const query methods. Before the sync
// review, auto_complete_folders read _summary without the lock and the folder flags were plain
// bools, so this pattern was a data race. This soak test drives that exact reader/writer overlap;
// it must finish without crashing, deadlocking, or corrupting the index.
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_index_concurrently()
{
	const auto index_path = _temps.next_path();

	null_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);

	database db(index);
	db.open(index_path.folder(), index_path.file_name_without_extension());

	df::index_roots paths;
	paths.folders.emplace(test_files_folder);
	index.index_roots(paths);
	index.index_folders(test_token);

	std::atomic<bool> stop = false;
	std::atomic<int> reader_iterations = 0;
	std::vector<std::thread> readers;

	// Readers hammer the const query methods that share state with index_folders.
	for (auto i = 0; i < 3; ++i)
	{
		readers.emplace_back([&]
		{
			while (!stop.load(std::memory_order_relaxed))
			{
				index.auto_complete_folders("test", 32);
				(void)index.is_in_collection(test_files_folder);
				(void)index.distinct_folders();
				(void)index.duplicate_list(0);
				reader_iterations.fetch_add(1, std::memory_order_relaxed);
			}
		});
	}

	// A single writer, matching the real design (one indexing thread), repeatedly rebuilds
	// the folder index under the readers.
	for (auto i = 0; i < 8 && !df::is_closing; ++i)
	{
		index.index_roots(paths);
		index.index_folders(test_token);
	}

	stop.store(true, std::memory_order_relaxed);
	for (auto& t : readers) t.join();

	assert_equal(true, reader_iterations.load() > 0, "concurrent readers ran");
	assert_equal(true, index.is_in_collection(test_files_folder), "collection intact after concurrent indexing");
}

// Verifies the pure prefix-range lookup that powers fast typeahead prediction over the
// case-insensitively sorted vocabulary snapshot.
static void should_find_word_prefix_range()
{
	// Sorted by str::icmp: '#' (0x23) < '@' (0x40) < letters (case-folded).
	const std::vector<std::string_view> terms = {
		"#dog", "@video", "Amsterdam", "index", "indigo", "industry", "windmill", "window"
	};

	const auto collect = [&terms](const std::string_view q)
	{
		const auto [lo, hi] = word_prefix_range(terms, q);
		return std::vector<std::string_view>(lo, hi);
	};

	assert_equal(3_z, collect("ind").size(), "three words start with 'ind'");
	assert_equal("index"s, std::string(collect("ind").front()), "first 'ind' match");
	assert_equal("industry"s, std::string(collect("ind").back()), "last 'ind' match");
	assert_equal(3_z, collect("IND").size(), "prefix match is case-insensitive");
	assert_equal(0_z, collect("dust").size(), "an interior substring is not a prefix");
	assert_equal(1_z, collect("@vid").size(), "scoped '@vid' prefix");
	assert_equal("@video"s, std::string(collect("@vid").front()), "'@video' matched");
	assert_equal(1_z, collect("#do").size(), "scoped '#do' prefix");
	assert_equal(0_z, collect("zzz").size(), "no match returns an empty range");
	assert_equal(1_z, collect("windmill").size(), "exact word matches itself");
	assert_equal(terms.size(), collect("").size(), "empty query matches all terms");
}

// Verifies the compressed posting lists (delta + VByte) and the boolean set operations that
// will compose term queries in the search inverted index.
static void should_encode_postings()
{
	// Round-trips across single-byte, multi-byte and max-width varints and large gaps.
	const std::vector<uint32_t> ids = {0, 1, 5, 127, 128, 300, 16383, 16384, 1000000, 4000000000u};
	const auto pl = df::posting_list::from_sorted(ids);
	assert_equal(true, pl.to_vector() == ids, "postings round-trip");
	assert_equal(static_cast<int>(ids.size()), static_cast<int>(pl.count()), "posting count");

	std::vector<uint32_t> via_each;
	pl.for_each([&via_each](const uint32_t id) { via_each.push_back(id); });
	assert_equal(true, via_each == ids, "for_each matches to_vector");

	// Duplicates and out-of-order ids are ignored (a term twice in one item = one posting).
	df::posting_list dedup;
	dedup.add(10);
	dedup.add(10);
	dedup.add(4);
	dedup.add(11);
	assert_equal(true, (dedup.to_vector() == std::vector<uint32_t>{10, 11}), "dedup / out-of-order ignored");

	// Empty list.
	const df::posting_list empty;
	assert_equal(true, empty.empty(), "empty posting list");
	assert_equal(true, empty.to_vector().empty(), "empty decodes to empty");

	// Compression: a dense contiguous run is ~1 byte per id (all deltas == 1).
	std::vector<uint32_t> dense(1000);
	for (uint32_t i = 0; i < dense.size(); ++i) dense[i] = i;
	const auto dense_pl = df::posting_list::from_sorted(dense);
	assert_equal(true, dense_pl.byte_size() <= dense.size() + 4, "dense run compresses to ~1 byte/id");

	// Boolean set operations (AND / OR / AND-NOT) that compose term queries.
	const std::vector<uint32_t> a = {1, 3, 5, 7, 9};
	const std::vector<uint32_t> b = {2, 3, 4, 7, 8};
	assert_equal(true, (df::postings_intersect(a, b) == std::vector<uint32_t>{3, 7}), "intersect (AND)");
	assert_equal(true, (df::postings_union(a, b) == std::vector<uint32_t>{1, 2, 3, 4, 5, 7, 8, 9}), "union (OR)");
	assert_equal(true, (df::postings_difference(a, b) == std::vector<uint32_t>{1, 5, 9}), "difference (AND-NOT)");
}

// Builds a tiny inverted index and cross-checks its term lookups and boolean composition
// against a brute-force scan of the same corpus - the correctness strategy for replacing the
// per-item search scan with a reverse index.
static void should_query_inverted_index()
{
	const std::vector<std::pair<uint32_t, std::vector<std::string_view>>> docs = {
		{0, {"beach", "sunset", "hawaii"}},
		{1, {"beach", "surf"}},
		{2, {"mountain", "sunset"}},
		{3, {"beach", "sunset", "surf"}},
		{4, {"portrait"}},
	};

	df::inverted_index index;
	for (const auto& [id, terms] : docs) index.add_document(id, terms);

	const auto brute = [&docs](const std::string_view term)
	{
		std::vector<uint32_t> out;
		for (const auto& [id, terms] : docs)
			if (std::ranges::any_of(terms, [term](const std::string_view t) { return str::icmp(t, term) == 0; }))
				out.push_back(id);
		return out;
	};

	for (const auto* const term : {"beach", "sunset", "surf", "portrait", "mountain", "missing"})
	{
		assert_equal(true, index.find(term) == brute(term), std::format("term '{}' matches brute force", term));
	}

	assert_equal(true, index.find("BEACH") == brute("beach"), "case-insensitive term lookup");

	// Boolean composition maps to AND / AND-NOT / OR query semantics.
	assert_equal(
		true, (df::postings_intersect(index.find("beach"), index.find("sunset")) == std::vector<uint32_t>{0, 3}),
		"beach AND sunset");
	assert_equal(true, (df::postings_difference(index.find("beach"), index.find("surf")) == std::vector<uint32_t>{0}),
	             "beach AND NOT surf");
	assert_equal(true,
	             (df::postings_union(index.find("sunset"), index.find("mountain")) == std::vector<uint32_t>{0, 2, 3}),
	             "sunset OR mountain");
}

// Cross-checks the trigram substring accelerator: its candidates must be a superset of the
// true (case-insensitive) substring matches, and verifying those candidates must reproduce the
// brute-force result exactly - the safe "candidate + verify" path for indexed substring search.
static void should_query_trigram_index()
{
	const std::vector<std::string_view> corpus = {
		"beach sunset", // 0
		"bulldog puppy", // 1
		"my dog photo", // 2
		"hotdog stand", // 3
		"mountain lake", // 4
		"DOGMA", // 5 - different case
	};

	df::trigram_index index;
	for (uint32_t i = 0; i < corpus.size(); ++i) index.add(i, corpus[i]);
	index.freeze();

	const auto brute = [&corpus](const std::string_view q)
	{
		std::vector<uint32_t> out;
		for (uint32_t i = 0; i < corpus.size(); ++i)
			if (str::contains(corpus[i], q)) out.push_back(i);
		return out;
	};

	for (const auto* const q : {"dog", "sunset", "mountain", "og p", "xyz", "DOG"})
	{
		const auto cand = index.candidates(q);
		assert_equal(true, cand.has_value(), std::format("'{}' produces trigram candidates", q));

		const auto truth = brute(q);

		// No false negatives: every true match is among the candidates.
		for (const auto id : truth)
			assert_equal(true, std::ranges::find(*cand, id) != cand->end(),
			             std::format("'{}' candidate superset", q));

		// Verifying candidates reproduces the brute-force substring result exactly.
		std::vector<uint32_t> verified;
		for (const auto id : *cand)
			if (str::contains(corpus[id], q)) verified.push_back(id);
		std::ranges::sort(verified);
		assert_equal(true, verified == truth, std::format("'{}' verified candidates == brute force", q));
	}

	// Queries shorter than a trigram cannot use the index.
	assert_equal(false, index.candidates("do").has_value(), "short query requires a scan");
}

static void should_materialize_detached_query_item()
{
	null_async_strategy async;
	const location_cache locations;
	const index_state index(async, locations);

	df::index_file_item file;
	file.name = "detached.jpg"_c;
	file.ft = files::file_type_from_name(file.name);
	file.size = df::file_size(1234);
	const auto metadata = std::make_shared<prop::item_metadata>();
	metadata->title = "Detached snapshot"_c;
	file.metadata.store(metadata);

	const auto path = df::file_path("c:\\detached.jpg");
	index_state::query_item_results query_items;
	query_items.push_back({index_state::query_item_kind::file, path, file, {}, {}, {}});

	const auto items = index.materialize_query_items(std::move(query_items), {});
	assert_equal(1_z, items.size(), "detached query result materialized without an index lookup");
	assert_equal("Detached snapshot", items.items().front()->title(), "detached metadata snapshot retained");
	assert_equal(1234, static_cast<int>(items.items().front()->file_size().to_int64()),
	             "detached file facts retained");
}

static void should_batch_thumbnail_publication()
{
	deferred_async_strategy async;
	location_cache locations;
	index_state index(async, locations);

	df::index_file_item first_file;
	first_file.name = "first.jpg"_c;
	first_file.ft = files::file_type_from_name(first_file.name);
	const auto first = std::make_shared<df::item_element>(df::file_path("c:\\first.jpg"), first_file);

	df::index_file_item second_file;
	second_file.name = "second.jpg"_c;
	second_file.ft = files::file_type_from_name(second_file.name);
	const auto second = std::make_shared<df::item_element>(df::file_path("c:\\second.jpg"), second_file);

	const auto timestamp = df::date_t(2026, 7, 28);
	index_state::thumbnail_results results;
	results.emplace_back(first, first->path(), nullptr, nullptr, timestamp);
	results.emplace_back(second, second->path(), nullptr, nullptr, timestamp);
	index.publish_thumbnails(std::move(results), false);

	assert_equal(1_z, async.pending_ui_count(), "thumbnail result batch should queue one UI callback");
	async.drain_ui();
	assert_equal(timestamp, first->thumbnail_timestamp(), "first batched thumbnail result applied");
	assert_equal(timestamp, second->thumbnail_timestamp(), "second batched thumbnail result applied");
	assert_equal(true, async.was_invalidated(view_invalid::view_redraw), "thumbnail publication requests redraw");
	assert_equal(false, async.was_invalidated(view_invalid::view_layout),
	             "unchanged thumbnail geometry does not request full layout");
}

static void should_skip_unneeded_thumbnail_staging()
{
	deferred_async_strategy async;
	df::index_file_item file;
	file.name = "staging.jpg"_c;
	file.ft = files::file_type_from_name(file.name);
	const auto item = std::make_shared<df::item_element>(df::file_path("c:\\staging.jpg"), file);

	item->stage_thumbnail_surface(async);
	assert_equal(0_z, async.pending_worker_count(async_queue::render),
	             "missing thumbnail should not queue render work");
}

static void should_reuse_cached_thumbnail_surface()
{
	const auto file_path = _temps.next_path(".jpg");
	platform::copy_file(test_files_folder.combine_file("Test.jpg"), file_path, false, true);

	null_async_strategy scan_async;
	const location_cache locations;
	index_state index(scan_async, locations);
	const auto item = load_item(index, file_path, true);
	assert_equal(true, item->has_cached_surface(), "scan fixture populated the thumbnail surface cache");

	deferred_async_strategy deferred;
	item->stage_thumbnail_surface(deferred);
	assert_equal(0_z, deferred.pending_worker_count(async_queue::render),
	             "cached thumbnail surface should not be staged again");
}

// Simulates a OneDrive Files On-Demand online-only file using platform::test_offline_predicate,
// which forces a real local file to be reported as an offline placeholder during folder
// enumeration. Verifies: (1) offline placeholders are indexed for metadata via the shell path
// with no content hash, (2) metadata_scanned persists so they are NOT re-indexed on restart,
// and (3) once hydrated (online) they are re-scanned to get a content hash and thumbnail.
static void should_index_offline_placeholder()
{
	const auto index_path = _temps.next_path();
	const auto file_path = _temps.next_path(".jpg");
	platform::copy_file(test_files_folder.combine_file("Test.jpg"), file_path, false, true);

	null_async_strategy as;
	location_cache locations;
	index_state index(as, locations);

	database db(index);
	db.open(index_path.folder(), index_path.file_name_without_extension());

	// --- Phase 1: the file is an online-only cloud placeholder ---
	platform::test_offline_predicate = [file_path](const df::file_path& p) { return p == file_path; };
	const df::scope_exit clear_offline([] { platform::test_offline_predicate = nullptr; });

	const auto offline_item = load_item(index, file_path, true);

	const auto offline_status = offline_item->online_status();
	const auto offline_md = offline_item->metadata();
	const auto offline_crc = offline_item->crc32c();
	// The shell property store also surfaces keywords and GPS for a placeholder (no hydration), which
	// the offline scan reads for free to help index non-downloaded files.
	const std::string offline_tags(offline_md ? offline_md->tags.sv() : std::string_view{});
	const auto offline_has_gps = offline_md && offline_md->coordinate.is_valid();
	const auto offline_gps_ok = offline_has_gps &&
		std::abs(offline_md->coordinate.latitude() - 50.08806) < 0.01 &&
		std::abs(offline_md->coordinate.longitude() - 14.42083) < 0.01;
	// Guards against re-indexing every startup: a scanned placeholder must not report that it
	// still needs a metadata scan, nor keep requesting a (shell) thumbnail every session.
	const auto offline_needs_scan = index.needs_scan(offline_item);
	const auto offline_wants_thumb = offline_item->should_load_thumbnail();

	db.perform_writes();

	// Reload into a fresh index from the same database to prove persistence, i.e. that the
	// item would NOT be re-indexed on the next application start.
	uint32_t reloaded_crc = 0xffffffffu;
	bool reloaded_scanned_valid = false;
	{
		index_state index2(as, locations);
		database db2(index2);
		db2.open(index_path.folder(), index_path.file_name_without_extension());
		db2.load_index_values();
		const auto reloaded = index2.find_item(file_path);
		reloaded_crc = reloaded.crc32c;
		reloaded_scanned_valid = reloaded.metadata_scanned.load().is_valid();
	}

	// --- Phase 2: the file has been hydrated (downloaded) and is now online ---
	platform::test_offline_predicate = nullptr;

	const auto online_item = load_item(index, file_path, true);

	const auto online_status = online_item->online_status();
	const auto online_crc = online_item->crc32c();
	const auto online_thumb_valid = ui::is_valid(online_item->thumbnail());
	const auto online_md = online_item->metadata();
	const std::string online_tags(online_md ? online_md->tags.sv() : std::string_view{});

	assert_equal(static_cast<int>(df::item_online_status::offline), static_cast<int>(offline_status),
	             "item reported offline while a placeholder");
	assert_equal(true, offline_md != nullptr, "offline item has shell metadata");
	assert_equal("key1 key2 key3", offline_tags, "shell keywords extracted for offline placeholder");
	assert_equal(true, offline_gps_ok, "shell GPS coordinate extracted for offline placeholder");
	assert_equal(0u, offline_crc, "offline item has no content hash");
	assert_equal(false, offline_needs_scan, "scanned placeholder does not need re-scan (no re-index on restart)");
	assert_equal(false, offline_wants_thumb, "placeholder does not repeatedly request a thumbnail");

	assert_equal(true, reloaded_scanned_valid, "metadata_scanned persisted (no re-index on restart)");
	assert_equal(0u, reloaded_crc, "no content hash persisted for offline item");

	assert_equal("key1 key2 key3", online_tags, "embedded tags extracted into item metadata after hydration");

	assert_equal(static_cast<int>(df::item_online_status::disk), static_cast<int>(online_status),
	             "item reported online after hydration");
	assert_equal(true, online_crc != 0, "content hash computed after hydration");
	assert_equal(true, online_thumb_valid, "thumbnail loaded after hydration");
}

// Verifies the item-level half of hydration recovery: an item whose thumbnail could not be
// loaded while it was a cloud placeholder is allowed to load it again once the file transitions
// from offline to online (item_element::update).
static void should_clear_failed_thumbnail_on_hydration()
{
	const auto file_path = test_files_folder.combine_file("Test.jpg");

	null_async_strategy as;
	location_cache locations;
	index_state index(as, locations);

	index.validate_folder(file_path.folder(), true, platform::now());

	auto info = index.find_item(file_path);
	info.ft = files::file_type_from_name(file_path.name());

	const auto item = std::make_shared<df::item_element>(file_path, info);

	// Offline placeholder whose thumbnail could not be loaded from the shell cache.
	auto offline_info = info;
	offline_info.flags |= df::index_item_flags::is_offline;
	item->update(file_path, offline_info);
	item->failed_loading_thumbnail(true);

	assert_equal(static_cast<int>(df::item_online_status::offline), static_cast<int>(item->online_status()),
	             "offline before hydration");
	assert_equal(true, item->failed_loading_thumbnail(), "thumbnail marked failed while offline");

	// Hydrated: the same item is now reported online.
	auto online_info = info;
	online_info.flags &= ~df::index_item_flags::is_offline;
	item->update(file_path, online_info);

	assert_equal(static_cast<int>(df::item_online_status::disk), static_cast<int>(item->online_status()),
	             "online after hydration");
	assert_equal(false, item->failed_loading_thumbnail(), "thumbnail failure cleared after hydration");
}

// Reproduces the "summary not refreshed after download" report: the SAME displayed item element
// (not a freshly created one) must have its metadata refreshed after the file is hydrated and
// re-scanned, so the first summary section (camera/tags read from item->metadata()) updates
// without re-selecting the photo.
static void should_refresh_same_item_metadata_after_hydration()
{
	const auto index_path = _temps.next_path();
	const auto file_path = _temps.next_path(".jpg");
	platform::copy_file(test_files_folder.combine_file("Test.jpg"), file_path, false, true);

	null_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);

	database db(index);
	db.open(index_path.folder(), index_path.file_name_without_extension());

	// Phase 1: the file is an online-only placeholder; scan it and keep the displayed item.
	platform::test_offline_predicate = [file_path](const df::file_path& p) { return p == file_path; };
	const df::scope_exit clear_offline([] { platform::test_offline_predicate = nullptr; });

	const auto item = load_item(index, file_path, true);
	const auto offline_md = item->metadata();
	const std::string offline_camera(offline_md ? offline_md->camera_model.sv() : std::string_view{});

	// Phase 2: the file is hydrated. Re-scan THE SAME item element (as queue_scan_modified_items
	// does for the currently displayed item).
	platform::test_offline_predicate = nullptr;

	df::item_set to_rescan;
	item->add_to(to_rescan);
	index.scan_items(to_rescan, true, true, false, false, {});

	const auto online_md = item->metadata();
	const std::string online_tags(online_md ? online_md->tags.sv() : std::string_view{});
	const std::string online_camera(online_md ? online_md->camera_model.sv() : std::string_view{});

	// Camera is the discriminator: the offline shell property scan never provides a camera model
	// (unlike tags, which the shell can supply via System.Keywords), so it must appear only after
	// the full online re-scan of the hydrated file.
	assert_equal(true, offline_camera.empty(), "offline shell scan provides no camera model before hydration");
	assert_equal("key1 key2 key3", online_tags,
	             "same displayed item metadata refreshed with tags after hydration re-scan");
	assert_equal("Canon EOS 7D", online_camera,
	             "same displayed item metadata refreshed with camera after hydration re-scan");
}

// Verifies the display-trigger timing fix (view_state::rescan_hydrated_display_item): a cloud
// placeholder being viewed must not be re-indexed until the FULL-file metadata scan has completed
// (display_state_t::_full_metadata_loaded). Triggering earlier -- e.g. off a partial preview texture
// load, which is enough to show the image but only partially hydrates the placeholder -- would
// re-scan the file while it is still offline and miss its tags (the bug that required an F5). Once
// the full metadata has loaded (the file is fully hydrated) the same view frame triggers the online
// re-index and the item gains its tags.
static void should_trigger_rescan_only_after_full_metadata_load()
{
	const auto file_path = _temps.next_path(".jpg");
	platform::copy_file(test_files_folder.combine_file("Test.jpg"), file_path, false, true);

	null_state_strategy ss;
	null_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);
	common_display_state_t common;
	view_state s(ss, as, index, make_test_player());

	// Phase 1: index the file as an online-only placeholder.
	platform::test_offline_predicate = [file_path](const df::file_path& p) { return p == file_path; };
	const df::scope_exit clear_offline([] { platform::test_offline_predicate = nullptr; });
	const auto item = load_item(index, file_path, true);

	// The file has since been hydrated (predicate cleared): its bytes are fully present on disk.
	platform::test_offline_predicate = nullptr;

	// Build a display for the item with a loaded texture, mimicking the big-view render state.
	const auto d = std::make_shared<display_state_t>(as, common);
	d->_item1 = item;
	d->_selected_texture1 = std::make_shared<texture_state>(as, item);
	d->_selected_texture1->load_image(item); // synchronous under null_async_strategy
	s._display = d;

	const auto texture_loaded = !d->_selected_texture1->loaded().is_empty();

	// Phase 2a: the full-file metadata scan has NOT finished. The rescan must NOT fire, so the item
	// stays offline (this is the bug that left the tag list missing until F5).
	d->_full_metadata_loaded = false;
	s.rescan_hydrated_display_item();
	const auto status_before = item->online_status();
	const auto md_before = item->metadata();
	// Camera is the "not yet re-indexed" signal: the offline shell scan can supply tags (via
	// System.Keywords) but never a camera model, so that only appears after the full online re-scan.
	const std::string camera_before(md_before ? md_before->camera_model.sv() : std::string_view{});

	// Phase 2b: the full-file metadata scan has completed (the placeholder is fully hydrated). The
	// same view frame now triggers the online re-index; the item flips online and gains its tags.
	d->_full_metadata_loaded = true;
	s.rescan_hydrated_display_item();
	const auto status_after = item->online_status();
	const auto md_after = item->metadata();
	const std::string tags_after(md_after ? md_after->tags.sv() : std::string_view{});
	const std::string camera_after(md_after ? md_after->camera_model.sv() : std::string_view{});

	// Phase 3: the trigger is one-shot -- calling again must not change anything.
	s.rescan_hydrated_display_item();
	const auto status_repeat = item->online_status();

	s._display.reset();

	assert_equal(true, texture_loaded, "texture image loaded for display");
	assert_equal(static_cast<int>(df::item_online_status::offline), static_cast<int>(status_before),
	             "no premature re-index before full metadata load");
	assert_equal(true, camera_before.empty(),
	             "no camera model before full metadata load (offline shell scan lacks it)");
	assert_equal(static_cast<int>(df::item_online_status::disk), static_cast<int>(status_after),
	             "item re-indexed online after full metadata load");
	assert_equal("key1 key2 key3", tags_after, "tags picked up once full metadata load triggers re-index");
	assert_equal("Canon EOS 7D", camera_after, "camera picked up once full metadata load triggers re-index");
	assert_equal(static_cast<int>(df::item_online_status::disk), static_cast<int>(status_repeat),
	             "trigger remains one-shot after completing");
}

// A load claims _photo_loaded before it runs. If the load then fails and the claim stands, the
// texture is retained under its path and reused when the user navigates back onto it -- and both the
// paths that would load it (display_state_t::populate and texture_state::refresh) skip a texture that
// reports itself loaded. The image would then never load and never report a problem, leaving the
// thumbnail standing in for it for as long as the texture stayed cached.
static void should_retry_after_failed_load()
{
	const auto good_path = _temps.next_path(".jpg");
	const auto missing_path = _temps.next_path(".jpg");
	platform::copy_file(test_files_folder.combine_file("Test.jpg"), good_path, false, true);
	platform::copy_file(test_files_folder.combine_file("Test.jpg"), missing_path, false, true);

	null_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);

	const auto good_item = load_item(index, good_path, true);
	const auto missing_item = load_item(index, missing_path, true);

	// Indexed while present, then removed, so the load is guaranteed to fail.
	platform::delete_file(missing_path);

	const auto failed = std::make_shared<texture_state>(as, missing_item);
	failed->load_image(missing_item); // synchronous under null_async_strategy

	assert_equal(false, failed->_photo_loaded, "failed load does not leave the image claimed as loaded");
	assert_equal(true, failed->_load_retry_pending, "failed load arms the retry that arriving on the item uses");
	assert_equal(true, failed->_is_placeholder, "failed load still shows the thumbnail standing in");

	// Arriving on the item is what must recover. refresh() is the per-tick path the display runs, and
	// it loads only when the texture does not claim to be loaded -- so this fails if the claim stands.
	platform::copy_file(test_files_folder.combine_file("Test.jpg"), missing_path, false, true);
	failed->refresh(missing_item);

	assert_equal(true, failed->_photo_loaded, "arriving on the item after a failed load loads it");
	assert_equal(false, failed->_is_placeholder, "recovered load replaces the thumbnail with the image");
	assert_equal(false, failed->loaded().is_empty(), "recovered load holds the decoded image");

	// The success path must keep working: the claim stands and the decoded image replaces the thumb.
	const auto loaded = std::make_shared<texture_state>(as, good_item);
	loaded->load_image(good_item);

	assert_equal(true, loaded->_photo_loaded, "successful load claims the image so arriving does not reload");
	assert_equal(false, loaded->_is_placeholder, "successful load replaces the thumbnail with the image");
	assert_equal(false, loaded->loaded().is_empty(), "successful load holds the decoded image");
	assert_equal(false, loaded->_load_retry_pending, "successful load arms no retry");
}

// With no item-to-item fade there is no outgoing image to cover phase 1's latency, so anything the
// display cannot draw at once is a visible flash of the shaped grey rectangle. The browser has
// already decoded a thumbnail surface for every visible item, and adopting it is what makes phase 1
// immediate rather than a render-worker round trip. Demotion off the display gives up every decoded
// representation, so returning to a cached item needs the same seed or it waits out a full decode.
static void should_seed_placeholder_from_staged_thumbnail()
{
	const auto file_path = _temps.next_path(".jpg");
	platform::copy_file(test_files_folder.combine_file("Test.jpg"), file_path, false, true);

	null_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);

	const auto item = load_item(index, file_path, true);
	assert_equal(true, item->has_cached_surface(), "fixture staged the item's thumbnail surface");

	const auto tex = std::make_shared<texture_state>(as, item);
	assert_equal(false, tex->has_visual(), "a fresh texture state has nothing to draw before it is seeded");

	tex->seed_placeholder(item);
	assert_equal(true, tex->has_visual(), "selection draws the staged thumbnail without waiting on a decode");
	assert_equal(true, tex->is_provisional(), "the thumbnail standing in is still marked provisional");

	// Seeding must never step backwards onto something better.
	tex->load_image(item); // synchronous under null_async_strategy
	assert_equal(false, tex->_is_placeholder, "load replaced the thumbnail with the image");
	tex->seed_placeholder(item);
	assert_equal(false, tex->_is_placeholder, "seeding does not reinstate the thumbnail over a loaded image");

	tex->release_decoded_surfaces();
	assert_equal(false, tex->has_visual(), "demotion gives up every representation the entry decoded");
	assert_equal(false, tex->loaded().is_empty(), "demotion keeps the loaded image so no file is re-read");

	tex->seed_placeholder(item);
	assert_equal(true, tex->has_visual(), "returning to a demoted item draws the thumbnail at once");
}

// A thumbnail budget that evicted without re-arming the database query would leave the item blank for
// as long as the search stayed open: update_visible_items_list only asks the database for items whose
// query is still pending, and every other route to a thumbnail is a file scan.
static void should_trim_thumbnail_blobs_by_distance()
{
	null_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);

	df::item_elements items;

	for (auto i = 0; i < 4; ++i)
	{
		const auto path = _temps.next_path(".jpg");
		platform::copy_file(test_files_folder.combine_file("Test.jpg"), path, false, true);
		items.emplace_back(load_item(index, path, true));
	}

	// Two items on screen, one a viewport below, one far below.
	constexpr recti viewport{0, 0, 400, 300};
	items[0]->bounds = {0, 0, 200, 150};
	items[1]->bounds = {200, 0, 400, 150};
	items[2]->bounds = {0, 700, 200, 850};
	items[3]->bounds = {0, 3000, 200, 3150};

	size_t total = 0;

	for (const auto& i : items)
	{
		assert_equal(true, i->has_thumb(), "fixture loaded a thumbnail", "trim thumbnails");
		assert_equal(true, i->begin_db_thumbnail_query(), "database query starts pending", "trim thumbnails");
		total += i->thumbnail_blob_bytes();
	}

	assert_equal(0, static_cast<int>(df::trim_thumbnail_blobs(items, viewport, total)),
	             "nothing released while inside the budget", "trim thumbnails");
	assert_equal(true, items[3]->has_thumb(), "the furthest item keeps its thumbnail inside the budget",
	             "trim thumbnails");

	// Items is not always the laid-out view, and ranking by distance from nothing would dump the set.
	assert_equal(0, static_cast<int>(df::trim_thumbnail_blobs(items, recti{}, 1)),
	             "an empty viewport releases nothing", "trim thumbnails");
	assert_equal(true, items[3]->has_thumb(), "an empty viewport keeps every thumbnail", "trim thumbnails");

	const auto dims_before = items[3]->layout_dims();

	assert_equal(true, df::trim_thumbnail_blobs(items, viewport, 1) > 0, "over budget releases something",
	             "trim thumbnails");
	assert_equal(true, items[0]->has_thumb(), "an item in the viewport keeps its thumbnail", "trim thumbnails");
	assert_equal(true, items[1]->has_thumb(), "an item in the viewport keeps its thumbnail", "trim thumbnails");
	assert_equal(false, items[2]->has_thumb(), "an item a viewport away gives its thumbnail up", "trim thumbnails");
	assert_equal(false, items[3]->has_thumb(), "the furthest item gives its thumbnail up", "trim thumbnails");

	assert_equal(true, items[3]->begin_db_thumbnail_query(), "an evicted item re-asks the database",
	             "trim thumbnails");
	assert_equal(false, items[0]->begin_db_thumbnail_query(), "a retained item does not re-ask", "trim thumbnails");
	assert_equal(dims_before.cx, items[3]->layout_dims().cx, "eviction does not reflow the row", "trim thumbnails");
	assert_equal(dims_before.cy, items[3]->layout_dims().cy, "eviction does not reflow the row", "trim thumbnails");
}

// The recent-texture cache retains by form, not by recency. An entry that is no longer displayed
// gives up everything it decoded, because re-decoding an encoded file at display size costs less than
// the surface costs to hold -- but a format that decoded straight to a surface has no encoded form to
// fall back on, and dropping its pixels would send it back to the file for a full native-size decode.
static void should_retain_undisplayed_images_by_form()
{
	null_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);

	const auto jpeg_path = _temps.next_path(".jpg");
	const auto heif_path = _temps.next_path(".heic");
	platform::copy_file(test_files_folder.combine_file("Test.jpg"), jpeg_path, false, true);
	platform::copy_file(test_files_folder.combine("excluded1").combine_file("melnik-rotated.heic"), heif_path, false,
	                    true);

	const auto encoded = std::make_shared<texture_state>(as, load_item(index, jpeg_path, true));
	const auto surface_only = std::make_shared<texture_state>(as, load_item(index, heif_path, true));

	encoded->load_image(load_item(index, jpeg_path, false)); // synchronous under null_async_strategy
	surface_only->load_image(load_item(index, heif_path, false));

	assert_equal(false, encoded->loaded().is_empty(), "jpeg loaded", "retain by form");
	assert_equal(false, surface_only->loaded().is_empty(), "heif loaded", "retain by form");
	assert_equal(0, static_cast<int>(encoded->retained_decoded_bytes()),
	             "a jpeg holds no decoded pixels before it is drawn", "retain by form");
	assert_equal(true, surface_only->retained_decoded_bytes() > 0,
	             "a heif decodes straight to a surface, so its pixels are its only representation",
	             "retain by form");

	encoded->release_decoded_surfaces();
	surface_only->release_decoded_surfaces();

	assert_equal(0, static_cast<int>(encoded->retained_decoded_bytes()), "the jpeg keeps nothing decoded",
	             "retain by form");
	assert_equal(true, surface_only->retained_decoded_bytes() > 0,
	             "the heif keeps the surface it cannot cheaply rebuild", "retain by form");

	// Both must still be loaded, or the next draw returns to the file and the cache achieved nothing.
	assert_equal(false, encoded->loaded().is_empty(), "release keeps the jpeg loaded", "retain by form");
	assert_equal(false, surface_only->loaded().is_empty(), "release keeps the heif loaded", "retain by form");
	assert_equal(true, encoded->_photo_loaded, "release does not re-arm a file load", "retain by form");
	assert_equal(true, surface_only->_photo_loaded, "release does not re-arm a file load", "retain by form");
}

// Reverse permutation of hydration: a file that was indexed online (tags + content hash) is
// dehydrated back into a cloud-only placeholder (OneDrive "free up space"). Its modified time is
// unchanged, so it must NOT be re-scanned; the item flips back to offline but keeps the previously
// indexed metadata (tags) and content hash rather than losing them to the shell-only offline scan.
static void should_preserve_metadata_when_dehydrated()
{
	const auto file_path = _temps.next_path(".jpg");
	platform::copy_file(test_files_folder.combine_file("Test.jpg"), file_path, false, true);

	null_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);

	// Online: a full scan produces tags and a content hash.
	const auto item = load_item(index, file_path, true);
	const auto online_md = item->metadata();
	const std::string online_tags(online_md ? online_md->tags.sv() : std::string_view{});
	const auto online_status = item->online_status();
	const auto online_has_crc = item->crc32c() != 0;

	// Dehydrated: the file becomes a cloud placeholder again. Re-scan the same item element.
	platform::test_offline_predicate = [file_path](const df::file_path& p) { return p == file_path; };
	const df::scope_exit clear_offline([] { platform::test_offline_predicate = nullptr; });
	df::item_set to_rescan;
	item->add_to(to_rescan);
	index.scan_items(to_rescan, true, true, false, false, {});
	platform::test_offline_predicate = nullptr;

	const auto offline_md = item->metadata();
	const std::string offline_tags(offline_md ? offline_md->tags.sv() : std::string_view{});
	const auto offline_status = item->online_status();
	const auto offline_has_crc = item->crc32c() != 0;

	assert_equal(static_cast<int>(df::item_online_status::disk), static_cast<int>(online_status),
	             "online after full scan");
	assert_equal("key1 key2 key3", online_tags, "tags read while online");
	assert_equal(true, online_has_crc, "content hash computed while online");

	assert_equal(static_cast<int>(df::item_online_status::offline), static_cast<int>(offline_status),
	             "offline after dehydration");
	assert_equal("key1 key2 key3", offline_tags, "tags preserved after dehydration (no re-scan)");
	assert_equal(true, offline_has_crc, "content hash preserved after dehydration");
}

// Verifies the on-demand shell-thumbnail path for cloud-only placeholders: the thumbnail is fetched
// only for items the user is actually viewing (never during indexing / for the hydrating scan, and
// only for items still on screen -- an off-screen item is abandoned, not stuck), and a hydrated
// (online) item does not use this path at all. Uses test_offline_predicate to mark local files as
// placeholders; under null_async_strategy queue_scan_offline_thumbnails runs synchronously.
static void should_fetch_shell_thumbnail_only_for_offline_visible()
{
	const auto file_path = _temps.next_path(".jpg");
	platform::copy_file(test_files_folder.combine_file("Test.jpg"), file_path, false, true);
	const auto hidden_path = _temps.next_path(".jpg");
	platform::copy_file(test_files_folder.combine_file("Test.jpg"), hidden_path, false, true);

	null_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);

	// Online-only placeholders: indexed with metadata only, no thumbnail.
	platform::test_offline_predicate = [file_path, hidden_path](const df::file_path& p)
	{
		return p == file_path || p == hidden_path;
	};
	const df::scope_exit clear_offline([] { platform::test_offline_predicate = nullptr; });
	const auto item = load_item(index, file_path, true);
	const auto hidden = load_item(index, hidden_path, true);
	item->begin_db_thumbnail_query(); // the db-thumbnail query has run; nothing cached yet
	hidden->begin_db_thumbnail_query();

	const auto offline_wants_normal = item->should_load_thumbnail();
	const auto offline_wants_shell = item->should_load_shell_thumbnail();

	// Only the on-screen item is marked visible; the fetcher must skip the off-screen one.
	item->is_visible(true);
	hidden->is_visible(false);

	// Fetch on-demand, exactly as the items view does for visible offline items.
	df::item_set to_load;
	item->add_to(to_load);
	hidden->add_to(to_load);
	index.queue_scan_offline_thumbnails(to_load);

	const auto pending_after = item->shell_thumbnail_pending();
	const auto wants_shell_after = item->should_load_shell_thumbnail();
	// The off-screen item was skipped: its pending flag is cleared (not stuck) and it stays eligible
	// so it is fetched if it later scrolls into view.
	const auto hidden_pending_after = hidden->shell_thumbnail_pending();
	const auto hidden_still_eligible = hidden->should_load_shell_thumbnail();
	const auto hidden_has_thumb = ui::is_valid(hidden->thumbnail());

	// Once hydrated (online), the shell-thumbnail path is not used (the normal scan handles it).
	platform::test_offline_predicate = nullptr;
	const auto online_item = load_item(index, file_path, true);
	online_item->begin_db_thumbnail_query();
	const auto online_wants_shell = online_item->should_load_shell_thumbnail();

	assert_equal(false, offline_wants_normal, "offline item never uses the hydrating thumbnail scan");
	assert_equal(true, offline_wants_shell, "offline visible item requests a shell thumbnail");
	assert_equal(false, pending_after, "shell-thumbnail pending flag cleared after the fetch attempt");
	assert_equal(false, wants_shell_after, "visible offline item does not re-request after a single fetch attempt");
	assert_equal(false, hidden_pending_after, "off-screen item pending flag cleared (abandoned, not stuck)");
	assert_equal(true, hidden_still_eligible,
	             "off-screen item skipped by the fetcher stays eligible for when it scrolls in");
	assert_equal(false, hidden_has_thumb, "off-screen item is not fetched");
	assert_equal(false, online_wants_shell, "hydrated (online) item does not use the shell thumbnail path");
}

// A cloud provider that has not generated a thumbnail returns its generic icon, which the fetcher
// reports as "pending" and the items view retries on a timer. Some files (video is common) never get
// one, so the retry is bounded: after the last attempt the item stops asking and keeps its file-type
// placeholder, and hydration restores its budget. Re-arming a batch abandoned for a newer visible set
// must not spend an attempt.
static void should_bound_shell_thumbnail_retries()
{
	const auto file_path = _temps.next_path(".jpg");
	platform::copy_file(test_files_folder.combine_file("Test.jpg"), file_path, false, true);

	null_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);

	platform::test_offline_predicate = [file_path](const df::file_path& p) { return p == file_path; };
	const df::scope_exit clear_offline([] { platform::test_offline_predicate = nullptr; });
	const auto item = load_item(index, file_path, true);
	item->begin_db_thumbnail_query();
	item->is_visible(true);

	// Abandoned batches re-arm without consuming the budget.
	for (auto i = 0; i < static_cast<int>(df::max_shell_thumbnail_retries) * 2; ++i)
	{
		item->shell_thumbnail_retry_pending(true, false);
		item->shell_thumbnail_retry_pending(false);
	}

	const auto eligible_after_abandons = item->should_load_shell_thumbnail();

	auto armed = 0;
	auto responses = 0;
	while (item->should_load_shell_thumbnail() && responses < static_cast<int>(df::max_shell_thumbnail_retries) * 4)
	{
		++responses;
		item->shell_thumbnail_retry_pending(true); // provider returned its generic icon again
		if (item->shell_thumbnail_retry_pending()) ++armed;
		item->shell_thumbnail_retry_pending(false); // retry pass re-requests it
	}

	const auto eligible_after_cap = item->should_load_shell_thumbnail();
	const auto failed_after_cap = item->failed_loading_thumbnail();
	const auto has_thumb = ui::is_valid(item->thumbnail());

	assert_equal(true, eligible_after_abandons, "abandoned batches re-arm without spending an attempt");
	assert_equal(static_cast<int>(df::max_shell_thumbnail_retries), armed, "provider retries are bounded");
	assert_equal(false, eligible_after_cap, "item stops asking the provider after the last attempt");
	assert_equal(true, failed_after_cap, "item settles on its file-type placeholder");
	assert_equal(false, has_thumb, "no thumbnail was invented for the item");
}

static void should_discard_stale_thumbnail_surface()
{
	const auto file_path = _temps.next_path(".jpg");
	platform::copy_file(test_files_folder.combine_file("Test.jpg"), file_path, false, true);

	null_async_strategy scan_async;
	const location_cache locations;
	index_state index(scan_async, locations);
	const auto item = load_item(index, file_path, true);
	assert_equal(true, item->has_thumb(), "scan produced encoded thumbnail");
	item->thumbnail(item->thumbnail(), {});
	assert_equal(false, item->has_cached_surface(), "test reset the decoded surface cache");

	deferred_async_strategy deferred;
	item->stage_thumbnail_surface(deferred);
	deferred.drain_ui();

	item->thumbnail({}, {});
	assert_equal(true, deferred.run_next(async_queue::render), "render work was queued");
	deferred.drain_ui();

	assert_equal(false, item->has_cached_surface(), "stale surface was discarded");
}

static void should_rescan_collection_after_forgetting_cache()
{
	const auto folder = _temps.next_folder("rebuild");
	const auto file_path = _temps.next_path_in(folder, ".jpg");
	platform::copy_file(test_files_folder.combine_file("Test.jpg"), file_path, false, true);

	null_async_strategy as;
	location_cache locations;
	index_state index(as, locations);

	df::index_roots roots;
	roots.folders.emplace(folder);
	index.index_roots(roots);
	index.index_folders(test_token);
	index.scan_uncached(test_token);

	const auto scanned = index.find_item(file_path);
	assert_equal(true, scanned.metadata_scanned.load().is_valid(), "first scan stamped the item");
	assert_equal(true, scanned.metadata.load() != nullptr, "first scan cached metadata");

	index.forget_cached_metadata();

	const auto forgotten = index.find_item(file_path);
	assert_equal(false, forgotten.metadata_scanned.load().is_valid(), "reset clears the scan timestamp");
	assert_equal(true, forgotten.metadata.load() != nullptr,
	             "reset keeps the metadata payload so index-only values survive");

	index.scan_uncached(test_token);

	const auto rescanned = index.find_item(file_path);
	assert_equal(true, rescanned.metadata_scanned.load().is_valid(), "the collection is re-read after a reset");
}

void register_index_tests(view_state& state, test_registry& tests)
{
	//
	// Index
	//
	tests.add("Should find word prefix range"s, should_find_word_prefix_range);
	tests.add("Should encode postings"s, should_encode_postings);
	tests.add("Should query inverted index"s, should_query_inverted_index);
	tests.add("Should query trigram index"s, should_query_trigram_index);
	tests.add("Should materialize detached query item"s, should_materialize_detached_query_item);
	tests.add("Should batch thumbnail publication"s, should_batch_thumbnail_publication);
	tests.add("Should skip unneeded thumbnail staging"s, should_skip_unneeded_thumbnail_staging);
	tests.add("Should reuse cached thumbnail surface"s, should_reuse_cached_thumbnail_surface);
	tests.add("Should index"s, should_index);
	tests.add("Should create database schema"s, should_create_database_schema);
	tests.add("Should store thumbnails"s, should_store_thumbnails);
	tests.add("Should store cover art"s, should_store_cover_art);
	tests.add("Should store item properties"s, should_store_item_properties);
	tests.add("Should invalidate cached metadata from an older build"s,
	          should_invalidate_cached_metadata_written_by_an_older_build);
	tests.add("Should replace an unreadable database"s, should_replace_an_unreadable_database);
	tests.add("Should run without a database"s, should_run_without_a_database);
	tests.add("Should hand scan results to the database in groups"s,
	          should_hand_scan_results_to_the_database_in_groups);
	tests.add("Should store pack properties"s, should_pack_item_properties);
	tests.add("Should store webservice results"s, should_store_webservice_results);
	tests.add("Should bound webservice cache"s, should_bound_webservice_cache);
	tests.add("Should detect duplicates"s, should_detect_duplicates);
	tests.add("Should drop deleted items from a search with no folder"s,
	          should_drop_deleted_items_from_a_search_with_no_folder);
	tests.add("Should request a re-query only when a folder changed"s,
	          should_request_a_re_query_only_when_a_folder_changed);
	tests.add("Should require equal size for duplicate CRC"s, should_require_equal_size_for_duplicate_crc);
	tests.add("Should report a re-encoded copy to presence"s, should_report_a_re_encoded_copy_to_presence);
	tests.add("Should report a rotated copy to presence"s, should_report_a_rotated_copy_to_presence);
	tests.add("Should update collection presence"s, should_update_collection_presence);
	tests.add("Should discard stale presence result"s, should_discard_stale_presence_result);
	tests.add("Should discard stale scan item update"s, should_discard_stale_scan_item_update);
	tests.add("Should discard stale CRC result"s, should_discard_stale_crc_result);
	tests.add("Should not reload thumb when valid"s, should_not_reload_thumb_when_valid);
	tests.add("Should reuse persisted hover thumbnail until video changes"s,
	          should_reuse_persisted_hover_thumbnail_until_video_changes);
	tests.add("Should reload thumb after scan"s, should_reload_thumb_after_scan);
	tests.add("Should not reread after metadata write"s, should_not_reread_after_metadata_write);
	tests.add("Should count overlapping write claims"s, should_count_overlapping_write_claims);
	tests.add("Should detect rotation"s, should_detect_rotation);
	tests.add("Should parse roots"s, should_parse_roots);
	tests.add("Should parse drive label roots"s, should_parse_drive_label_roots);
	tests.add("Should index concurrently"s, should_index_concurrently);
	tests.add("Should index offline OneDrive placeholder"s, should_index_offline_placeholder);
	tests.add("Should clear failed thumbnail on hydration"s, should_clear_failed_thumbnail_on_hydration);
	tests.add("Should refresh same item metadata after hydration"s, should_refresh_same_item_metadata_after_hydration);
	tests.add("Should trigger rescan only after full metadata load"s,
	          should_trigger_rescan_only_after_full_metadata_load);
	tests.add("Should retry after failed load"s, should_retry_after_failed_load);
	tests.add("Should seed placeholder from staged thumbnail"s, should_seed_placeholder_from_staged_thumbnail);
	tests.add("Should trim thumbnail blobs by distance"s, should_trim_thumbnail_blobs_by_distance);
	tests.add("Should retain undisplayed images by form"s, should_retain_undisplayed_images_by_form);
	tests.add("Should preserve metadata when dehydrated"s, should_preserve_metadata_when_dehydrated);
	tests.add("Should fetch shell thumbnail only for offline visible"s,
	          should_fetch_shell_thumbnail_only_for_offline_visible);
	tests.add("Should bound shell thumbnail retries"s, should_bound_shell_thumbnail_retries);
	tests.add("Should discard stale thumbnail surface"s, should_discard_stale_thumbnail_surface);
	tests.add("Should rescan collection after forgetting cache"s, should_rescan_collection_after_forgetting_cache);
}
