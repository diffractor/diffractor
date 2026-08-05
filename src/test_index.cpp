// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Index, database, and UI tests. Verifies indexing, thumbnail storage,
// item properties, duplicate detection, renaming, selection, and command state.

#include "pch.h"

#include <Sqlite3.h>

#include "test_utils.h"
#include "model_db_pack.h"
#include "model_postings.h"
#include "util_crash_files_db.h"
#include "app_util.h"
#include "metadata_exif.h"
#include "metadata_iptc.h"
#include "metadata_xmp.h"
#include "ui_elements.h"
#include "ui_map_common.h"

class flex_test_measure_context final : public ui::measure_context
{
public:
	sizei measure_text(std::string_view text, ui::style::font_face font, ui::style::text_style style, int cx,
	                   int cy = 0) override
	{
		return {};
	}

	int text_line_height(ui::style::font_face font) override
	{
		return 0;
	}

	ui::text_layout_ptr create_text_layout(ui::style::font_face font) override
	{
		return {};
	}
};

class flex_test_element final : public view_element
{
	sizei _desired;

public:
	explicit flex_test_element(const sizei desired) : _desired(desired)
	{
		padding(0);
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		return {std::min(_desired.cx, width_limit), _desired.cy};
	}
};

// Behaves like text: content that does not fit the width limit wraps onto a second line.
class flex_wrapping_test_element final : public view_element
{
	sizei _desired;

public:
	explicit flex_wrapping_test_element(const sizei desired) : _desired(desired)
	{
		padding(0);
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		if (width_limit >= _desired.cx) return _desired;
		return {width_limit, _desired.cy * 2};
	}
};

static void should_layout_flex_elements()
{
	flex_test_measure_context mc;
	auto first = std::make_shared<flex_test_element>(sizei{40, 10});
	auto second = std::make_shared<flex_test_element>(sizei{40, 20});
	auto third = std::make_shared<flex_test_element>(sizei{40, 15});
	const std::vector<view_element_ptr> wrapping{first, second, third};
	const auto wrapped = calc_flex_layout(wrapping, mc, {100, 0}, {});
	assert_equal(5, wrapped.layout_bounds[0].top, "short item is centered on first flex line");
	assert_equal(0, wrapped.layout_bounds[1].top, "second item remains on first flex line");
	assert_equal(20, wrapped.layout_bounds[2].top, "third item wraps to second flex line");
	assert_equal(35, wrapped.extent.cy, "wrapped flex extent");

	first->flex.grow = 1.0f;
	second->flex.grow = 1.0f;
	const std::vector<view_element_ptr> growing{first, second};
	const auto grown = calc_flex_layout(growing, mc, {100, 0}, {});
	assert_equal(50, grown.layout_bounds[0].width(), "first flex item shares free space");
	assert_equal(50, grown.layout_bounds[1].width(), "second flex item shares free space");

	first->flex.grow = 0.0f;
	second->flex.grow = 0.0f;
	second->flex.main_start_auto = true;
	const auto justified = calc_flex_layout(growing, mc, {100, 0}, {});
	assert_equal(60, justified.layout_bounds[1].left, "auto margin right aligns trailing flex item");

	second->is_visible(false);
	const auto hidden = calc_flex_layout(growing, mc, {100, 0}, {});
	assert_equal(40, hidden.extent.cx, "hidden flex item consumes no space");
	assert_equal(true, hidden.layout_bounds[1] == recti{}, "hidden flex item has empty bounds");

	second->is_visible(true);
	first->flex.shrink = 1.0f;
	second->flex.shrink = 1.0f;
	first->flex.basis = 60;
	second->flex.basis = 60;
	first->flex.min_size.cx = 55;
	second->flex.min_size.cx = 40;
	flex_container_layout no_wrap;
	no_wrap.wrap = flex_wrap::no_wrap;
	const auto shrunk = calc_flex_layout(growing, mc, {100, 0}, no_wrap);
	assert_equal(55, shrunk.layout_bounds[0].width(), "flex shrink respects first minimum");
	assert_equal(45, shrunk.layout_bounds[1].width(), "flex shrink redistributes after minimum");

	auto column_first = std::make_shared<flex_test_element>(sizei{30, 10});
	auto column_second = std::make_shared<flex_test_element>(sizei{40, 20});
	const std::vector<view_element_ptr> column_elements{column_first, column_second};
	flex_container_layout column;
	column.direction = flex_direction::column;
	column.wrap = flex_wrap::no_wrap;
	const auto measured_column = calc_flex_layout(column_elements, mc, {100, -1}, column);
	assert_equal(30, measured_column.extent.cy, "unconstrained flex column measures intrinsic height");
	assert_equal(10, measured_column.layout_bounds[1].top, "unconstrained flex column stacks items");

	mc.padding2 = 8;
	auto divider = std::make_shared<divider_element>();
	divider->padding(0);
	const std::vector<view_element_ptr> divided_column{divider, column_second};
	const auto fixed_height_divider = calc_flex_layout(divided_column, mc, {100, 100}, column);
	assert_equal(8, fixed_height_divider.layout_bounds[0].height(),
	             "column divider keeps its intrinsic height instead of consuming free space");
	assert_equal(8, fixed_height_divider.layout_bounds[1].top,
	             "control after a column divider remains adjacent and visible");
	mc.padding2 = 0;

	column_first->flex.break_after = true;
	const auto broken_column = calc_flex_layout(column_elements, mc, {100, -1}, column);
	assert_equal(30, broken_column.layout_bounds[1].left,
	             "explicit break creates a new column when automatic wrapping is disabled");

	column_first->flex.break_after = false;
	column_first->flex.align_self = flex_align::stretch;
	column.padding = {5, 7};
	const auto padded_column = calc_flex_layout(column_elements, mc, {100, -1}, column);
	assert_equal(5, padded_column.layout_bounds[0].left, "column padding offsets the first item");
	assert_equal(90, padded_column.layout_bounds[0].width(), "explicit stretch fills the column cross axis");
	assert_equal(44, padded_column.extent.cy, "column padding contributes to intrinsic extent");

	mc.scale_factor = 2.0;
	const auto scaled_padded_column = calc_flex_layout(column_elements, mc, {200, -1}, column);
	assert_equal(10, scaled_padded_column.layout_bounds[0].left, "flex padding scales from logical units");
	assert_equal(58, scaled_padded_column.extent.cy, "scaled flex padding contributes once to intrinsic extent");
	mc.scale_factor = 1.0;

	ui::control_layouts positions;
	const auto applied_extent = layout_flex_elements(column_elements, mc, positions, {10, 20, 110, 80}, column);
	assert_equal(15, column_first->bounds.left, "applied flex layout offsets child bounds");
	assert_equal(padded_column.extent.cy, applied_extent.cy, "applied flex layout returns content extent");

	column.padding = {};
	column.justify = flex_justify::center;
	const auto centered_column = calc_flex_layout(column_elements, mc, {100, 100}, column);
	assert_equal(35, centered_column.layout_bounds[0].top, "flex column centers intrinsic content vertically");

	column.justify = flex_justify::start;
	column_first->flex.align_self = flex_align::automatic;
	column_first->flex = column_first->flex | flex_item::media;
	column_first->flex.basis = 100;
	const auto shrunk_column = calc_flex_layout(column_elements, mc, {100, 100}, column);
	assert_equal(80, shrunk_column.layout_bounds[0].height(), "flex column shrinks media to fit trailing controls");
	assert_equal(80, shrunk_column.layout_bounds[1].top, "trailing control remains visible after media shrink");

	auto fixed = std::make_shared<flex_test_element>(sizei{30, 10});
	auto flexible_first = std::make_shared<flex_test_element>(sizei{40, 10});
	auto flexible_second = std::make_shared<flex_test_element>(sizei{40, 10});
	fixed->flex.basis = 30;
	fixed->flex.shrink = 1.0f;
	flexible_first->flex.basis = 0;
	flexible_first->flex.grow = 1.0f;
	flexible_second->flex.basis = 0;
	flexible_second->flex.grow = 1.0f;
	flex_container_layout columns;
	columns.wrap = flex_wrap::no_wrap;
	columns.gap.cx = 5;
	const auto column_row = calc_flex_layout(
		std::vector<view_element_ptr>{fixed, flexible_first, flexible_second}, mc, {100, -1}, columns);
	assert_equal(30, column_row.layout_bounds[0].width(), "fixed flex column keeps its basis");
	assert_equal(30, column_row.layout_bounds[1].width(), "first flexible column shares remaining width");
	assert_equal(30, column_row.layout_bounds[2].width(), "second flexible column shares remaining width");

	auto capped = std::make_shared<flex_wrapping_test_element>(sizei{40, 10});
	capped->flex.max_size.cx = 25;
	capped->flex.align_self = flex_align::center;
	const std::vector<view_element_ptr> capped_elements{capped};
	const auto capped_column = calc_flex_layout(capped_elements, mc, {100, -1}, column);
	assert_equal(25, capped_column.layout_bounds[0].width(), "column item is capped by its maximum width");
	assert_equal(20, capped_column.extent.cy, "column item measures inside its maximum width");

	const auto capped_row = calc_flex_layout(capped_elements, mc, {100, -1}, {});
	assert_equal(25, capped_row.layout_bounds[0].width(), "row item is capped by its maximum width");
	assert_equal(20, capped_row.extent.cy, "row item measures inside its maximum width");

	auto padded_first = std::make_shared<flex_test_element>(sizei{20, 10});
	auto padded_second = std::make_shared<flex_test_element>(sizei{20, 10});
	flex_container_layout centered_row;
	centered_row.wrap = flex_wrap::no_wrap;
	centered_row.justify = flex_justify::center;
	centered_row.padding = {10, 0};
	const auto centered = calc_flex_layout(std::vector<view_element_ptr>{padded_first, padded_second}, mc,
	                                       {100, -1}, centered_row);
	assert_equal(30, centered.layout_bounds[0].left, "centred line keeps the container's leading padding");
	assert_equal(100, centered.extent.cx, "a justified line reports the whole main axis it positions within");

	auto capped_cross = std::make_shared<flex_test_element>(sizei{20, 10});
	capped_cross->flex.align_self = flex_align::stretch;
	capped_cross->flex.max_size.cy = 12;
	auto tall = std::make_shared<flex_test_element>(sizei{20, 40});
	flex_container_layout stretch_row;
	stretch_row.wrap = flex_wrap::no_wrap;
	const auto stretched = calc_flex_layout(std::vector<view_element_ptr>{capped_cross, tall}, mc, {100, -1},
	                                        stretch_row);
	assert_equal(12, stretched.layout_bounds[0].height(), "stretch respects the item's maximum cross size");

	auto only_child = std::make_shared<flex_test_element>(sizei{20, 10});
	only_child->is_visible(false);
	flex_container_layout hidden_column;
	hidden_column.direction = flex_direction::column;
	hidden_column.padding = {10, 10};
	const auto nothing_visible = calc_flex_layout(std::vector<view_element_ptr>{only_child}, mc, {100, -1},
	                                              hidden_column);
	assert_equal(0, nothing_visible.extent.cx, "a container with nothing visible occupies no width");
	assert_equal(0, nothing_visible.extent.cy, "a container with nothing visible occupies no height");
}

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

static void should_detect_original_path()
{
	const df::file_path path("c:\\temp\\test.original.jpg");
	assert_equal(true, path.is_original(), "detect original");
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

	// Small.jpg is Test.jpg resized: no shared name, size or CRC, but the same capture time and the
	// same picture. Recognising that is the whole point of the perceptual stage.
	const auto test_group = index.find_item(test_item1->path()).duplicates.load();
	const auto small_group = index.find_item(test_item5->path()).duplicates.load();

	assert_equal(2u, test_group.count, "a resized copy is a duplicate");
	assert_equal(2u, small_group.count, "and so is the original");
	assert_equal(true, test_group.group != 0 && test_group.group == small_group.group, "one duplicate group");

	// The grade is the claim. A re-encode is only ever "possible", and saying so is what separates it
	// from a byte-identical copy on a surface the user deletes from.
	assert_equal(static_cast<int>(df::copy_grade::same_picture), static_cast<int>(test_group.grade),
	             "a resized copy is graded as the same picture");
	assert_equal(static_cast<int>(df::copy_grade::same_picture), static_cast<int>(small_group.grade),
	             "and so is the original");

	// A rotation is a different bitmap and must not be swept in with them.
	assert_equal(1u, index.find_item(test_item2->path()).duplicates.load().count, "duplicates");
	assert_equal(1u, index.find_item(test_item3->path()).duplicates.load().count, "duplicates");
	assert_equal(1u, index.find_item(sony_item->path()).duplicates.load().count, "duplicates");

	// Parity: `@duplicates` and a related search read the one duplicate group, so a picture found by
	// the perceptual stage is reported by both rather than only by the feature that computed it.
	assert_equal(2, count_search_results(index, "@duplicates"), "@duplicates lists the pair");

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

static void should_require_equal_size_for_duplicate_crc()
{
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

// A video with embedded cover art draws the cover art on its tile, so the tile has to be the shape
// of the art. Sizing it from the video's own dimensions letterboxed the art inside a frame-shaped
// tile, and the republish that follows every scan pass used to revert it to the video shape.
static void should_shape_the_tile_by_what_it_draws()
{
	df::index_file_item indexed;
	indexed.name = str::cache("clip.mp4");
	indexed.ft = files::file_type_from_name(indexed.name);

	const auto md = std::make_shared<prop::item_metadata>();
	md->width = 1920;
	md->height = 1080;
	indexed.metadata.store(md);

	const auto path = df::file_path("c:\\clip.mp4");
	const auto item = std::make_shared<df::item_element>(path, indexed);

	assert_equal(1920, item->layout_dims().cx, "video width shapes the tile before cover art arrives");
	assert_equal(1080, item->layout_dims().cy, "video height shapes the tile before cover art arrives");
	assert_equal(true, item->layout_aspect_known(), "indexed dimensions are an exact aspect");

	const auto cover_art = std::make_shared<ui::image>(df::blob(16), sizei(600, 600),
	                                                   ui::image_format::JPEG, ui::orientation::top_left);
	item->thumbnail({}, cover_art, {});

	assert_equal(600, item->layout_dims().cx, "cover art width shapes the tile that draws it");
	assert_equal(600, item->layout_dims().cy, "cover art height shapes the tile that draws it");
	assert_equal(true, item->layout_aspect_known(), "a cover art aspect is exact, so the tile may justify");

	// scan_items republishes every displayed item on every pass.
	const auto republished = item->update(path, indexed);

	assert_equal(600, item->layout_dims().cx, "a republish does not revert the tile to the video shape");
	assert_equal(false, republished, "an unchanged republish asks for no layout pass");
}

// A thumbnail is a downscaled stand-in, so it may shape a tile whose real aspect is unknown but must
// never earn the row justification that a known aspect does.
static void should_not_justify_a_tile_shaped_by_a_thumbnail()
{
	df::index_file_item indexed;
	indexed.name = str::cache("unscanned.jpg");
	indexed.ft = files::file_type_from_name(indexed.name);

	const auto path = df::file_path("c:\\unscanned.jpg");
	const auto item = std::make_shared<df::item_element>(path, indexed);

	assert_equal(false, item->layout_aspect_known(), "nothing known before a scan");

	const auto thumb = std::make_shared<ui::image>(df::blob(16), sizei(160, 120),
	                                               ui::image_format::JPEG, ui::orientation::top_left);
	item->thumbnail(thumb, {}, {});

	assert_equal(160, item->layout_dims().cx, "the thumbnail stands in for an unknown aspect");
	assert_equal(false, item->layout_aspect_known(), "a stand-in aspect is never justified");

	// The scan lands and the index now knows the real size, which outranks the thumbnail.
	const auto md = std::make_shared<prop::item_metadata>();
	md->width = 4000;
	md->height = 3000;
	indexed.metadata.store(md);

	assert_equal(true, item->update(path, indexed), "a newly scanned size asks for a layout pass");
	assert_equal(4000, item->layout_dims().cx, "the indexed size outranks the thumbnail");
	assert_equal(true, item->layout_aspect_known(), "the real aspect is now known");
}

// A tile fills its cell by cropping, so every difference between the cell's shape and the image's
// shape is hidden pixels. The row solves one height and takes each width from it, which is what stops
// a row holding one or two portraits from cutting the top and bottom off them.
static void should_keep_tile_aspect_when_laying_out_a_row()
{
	null_state_strategy ss;
	null_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);
	view_state s(ss, as, index, make_test_player());

	flex_test_measure_context mc;
	constexpr auto width_limit = 1200;
	constexpr auto crop_tolerance = 0.05;
	auto next_name = 0;

	const auto make_item = [&next_name](const int cx, const int cy)
	{
		const auto name = "item" + std::to_string(++next_name) + ".jpg";

		df::index_file_item indexed;
		indexed.name = str::cache(name);
		indexed.ft = files::file_type_from_name(indexed.name);

		const auto md = std::make_shared<prop::item_metadata>();
		md->width = static_cast<uint16_t>(cx);
		md->height = static_cast<uint16_t>(cy);
		indexed.metadata.store(md);

		return std::make_shared<df::item_element>(df::file_path("c:\\" + name), indexed);
	};

	const auto assert_row_aspects = [&](df::item_elements items, const std::string_view name)
	{
		const auto group = std::make_shared<df::item_group>(s, std::move(items), df::item_group_display::icons,
		                                                    df::group_key{});
		group->measure(mc, width_limit);

		const auto& layout_bounds = group->_layout_bounds;
		assert_equal(group->items().size(), layout_bounds.size(), name, "a cell for every item");

		for (auto i = 0u; i < layout_bounds.size(); ++i)
		{
			const auto dims = group->items()[i]->layout_dims();
			const auto image_aspect = static_cast<double>(dims.cx) / dims.cy;
			const auto cell_aspect = static_cast<double>(layout_bounds[i].width()) / layout_bounds[i].height();
			const auto hidden = 1.0 - std::min(image_aspect, cell_aspect) / std::max(image_aspect, cell_aspect);

			assert_equal(true, hidden <= crop_tolerance + 0.01, name, "tile keeps its aspect");
			assert_equal(true, layout_bounds[i].right <= width_limit, name, "tile stays inside the row");
		}
	};

	assert_row_aspects({make_item(2000, 3000)}, "one portrait"sv);
	assert_row_aspects({make_item(2000, 3000), make_item(2000, 3000)}, "two portraits"sv);
	assert_row_aspects({make_item(3000, 2000), make_item(2000, 3000), make_item(4000, 3000)}, "mixed row"sv);
	assert_row_aspects({make_item(400, 400), make_item(64, 64)}, "small squares"sv);
	assert_row_aspects({make_item(12000, 1000)}, "panorama"sv);
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

	// A decoder fix ships in an update, so entries earned by an earlier build must not skip files
	// forever - the next build retries them.
	{
		crash_files_db next_build(db_path, "test-build-2"sv);
		assert_equal(next_build.skipped_file_count(), 0_z, "a new build retries files an older build recorded");
	}
}

static void write_binary_file(const df::file_path path, const uint8_t* const data, const int size)
{
	const auto f = open_file(path, platform::file_open_mode::create);

	if (f)
	{
		f->write(data, size);
	}
}

static void should_not_crash(const std::string_view name)
{
	const auto path = test_files_folder.combine_file(name);
	auto blob = blob_from_file(path);
	const auto ext = name.substr(df::find_ext(name));

	// Without this the whole test degrades silently: a missing or empty fixture makes both
	// loops below no-ops, and "did not crash" would be reported for work that never ran.
	assert_equal(true, blob.size() > 16u, std::format("{} fixture is readable", name));

	const auto original = std::vector<uint8_t>(blob.begin(), blob.end());

	files ff;

	auto* const data = blob.data();
	const auto size = blob.size();

	auto corrupted = 0u;

	for (auto i = 0u; i < size && !df::is_closing; i++)
	{
		const auto v = data[i];
		data[i] = 0xff;

		prop::item_metadata md;
		mem_read_stream stream(blob);
		const auto info = scan_photo(stream);

		metadata_exif::parse(md, info.metadata.exif);
		metadata_iptc::parse(md, info.metadata.iptc);
		metadata_xmp::parse(md, info.metadata.xmp);

		data[i] = v;
		++corrupted;
	}

	constexpr auto truncation_steps = 32;
	auto truncated = 0u;

	for (auto i = 0u; i < truncation_steps && !df::is_closing; i++)
	{
		const auto save_path = _temps.next_path(ext);
		write_binary_file(save_path, data, df::mul_div(static_cast<int>(size), i, truncation_steps));
		ff_scan_and_load_thumb(ff, save_path);
		++truncated;
	}

	if (df::is_closing)
	{
		assert_equal(true, corrupted > 0u, std::format("{} sweep started before shutdown", name));
		return;
	}

	assert_equal(static_cast<int>(size), static_cast<int>(corrupted),
	             std::format("{} every byte was corrupted and scanned", name));
	assert_equal(truncation_steps, static_cast<int>(truncated),
	             std::format("{} every truncation was scanned", name));
	assert_equal(true, std::equal(original.begin(), original.end(), blob.begin(), blob.end()),
	             std::format("{} bytes restored after the sweep", name));
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

// Mirrors the vocabulary substring-prediction path in auto_complete_words: for single-token
// queries, trigram candidates verified with str::ifind2 must reproduce a full ifind2 scan of
// the vocabulary exactly (the correctness guarantee behind the per-keystroke acceleration).
static void should_accelerate_substring_prediction()
{
	const std::vector<std::string_view> words = {
		"amsterdam", "index", "indigo", "industry", "reindex", "window", "windmill", "grind", "din"
	};

	df::trigram_index tri;
	for (uint32_t i = 0; i < words.size(); ++i) tri.add(i, words[i]);

	const auto scan = [&words](const std::string_view q)
	{
		std::vector<uint32_t> out;
		for (uint32_t i = 0; i < words.size(); ++i)
			if (str::ifind2(words[i], q, 0).found) out.push_back(i);
		return out;
	};

	for (const auto* const q : {"ind", "win", "dex", "ndu", "zzz"})
	{
		const auto cand = tri.candidates(q);
		assert_equal(true, cand.has_value(), std::format("'{}' has trigram candidates", q));

		std::vector<uint32_t> via_index;
		for (const auto id : *cand)
			if (str::ifind2(words[id], q, 0).found) via_index.push_back(id);
		std::ranges::sort(via_index);

		assert_equal(true, via_index == scan(q), std::format("'{}' trigram+verify == full scan", q));
	}
}

static void should_signal_and_replace_pending_queue_work()
{
	platform::queue<int> values;
	assert_equal(true, values.enqueue(1), "first enqueue signals empty transition");
	assert_equal(false, values.enqueue(2), "additional queued work does not signal again");

	int value = 0;
	assert_equal(true, values.dequeue(value), "first value dequeued");
	assert_equal(false, values.enqueue(3), "queue remains nonempty until the final value is dequeued");
	assert_equal(true, values.dequeue(value), "second value dequeued");
	assert_equal(true, values.dequeue(value), "third value dequeued");
	assert_equal(true, values.enqueue(4), "enqueue signals after queue becomes empty again");

	platform::task_queue tasks;
	auto executed = 0;
	tasks.enqueue([&executed] { executed = 1; });
	tasks.reset_and_enqueue([&executed] { executed = 2; });
	for (const auto& task : tasks.dequeue_all()) task();
	assert_equal(2, executed, "reset_and_enqueue retains only the latest pending task");
}

static void should_predict_search_completions(shared_test_context& stc)
{
	stc.lazy_load_index();
	stc.test_index.update_summary();
	auto& index = stc.test_index;

	std::vector<index_state::auto_complete_word> words;
	std::vector<index_state::auto_complete_word> companions;
	std::vector<index_state::auto_complete_word> filtered;
	std::thread prediction_worker([&]
	{
		words = index.auto_complete_words("#key", 10);
		companions = index.auto_complete_tag_companions({"key1"}, {}, 10);
		filtered = index.auto_complete_tag_companions({"key1"}, "key3", 10);
	});
	prediction_worker.join();

	const auto key1 = std::ranges::find(words, "#key1", &index_state::auto_complete_word::text);
	assert_equal(true, key1 != words.end(), "indexed tag is suggested");
	assert_equal(true, key1 != words.end() && key1->occurrences > 0, "tag occurrence count is returned");

	assert_equal(true,
	             std::ranges::find(companions, "#key2", &index_state::auto_complete_word::text) != companions.end(),
	             "co-occurring tag is suggested");
	assert_equal(true,
	             std::ranges::find(companions, "#key1", &index_state::auto_complete_word::text) == companions.end(),
	             "existing tag is not suggested again");

	assert_equal(1_z, filtered.size(), "companion prefix filters suggestions");
	assert_equal("#key3"s, filtered.front().text, "matching companion is retained");
	assert_equal(true, filtered.front().occurrences > 0, "companion co-occurrence count is returned");
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

// A neighbour prefetch claims _photo_loaded before the load runs. If the load then fails and the
// claim stands, the texture is retained under its path and reused when the user navigates onto it --
// and both the paths that would load it (display_state_t::populate and texture_state::refresh) skip
// a texture that reports itself loaded. The image would then never load and never report a problem,
// leaving the thumbnail standing in for it for as long as the texture stayed cached.
static void should_retry_after_failed_prefetch()
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

	// Indexed while present, then removed, so the prefetch load is guaranteed to fail.
	platform::delete_file(missing_path);

	const auto failed = std::make_shared<texture_state>(as, missing_item);
	failed->prefetch(missing_item); // synchronous under null_async_strategy

	assert_equal(false, failed->_photo_loaded, "failed prefetch does not leave the image claimed as loaded");
	assert_equal(true, failed->_load_retry_pending, "failed prefetch arms the retry that arriving on the item uses");
	assert_equal(true, failed->_is_placeholder, "failed prefetch still shows the thumbnail standing in");

	// Arriving on the item is what must recover. refresh() is the per-tick path the display runs, and
	// it loads only when the texture does not claim to be loaded -- so this fails if the claim stands.
	platform::copy_file(test_files_folder.combine_file("Test.jpg"), missing_path, false, true);
	failed->refresh(missing_item);

	assert_equal(true, failed->_photo_loaded, "arriving on the item after a failed prefetch loads it");
	assert_equal(false, failed->_is_placeholder, "recovered load replaces the thumbnail with the image");
	assert_equal(false, failed->loaded().is_empty(), "recovered load holds the decoded image");

	// The success path must keep working: the claim stands and the decoded image replaces the thumb.
	const auto loaded = std::make_shared<texture_state>(as, good_item);
	loaded->prefetch(good_item);

	assert_equal(true, loaded->_photo_loaded, "successful prefetch claims the image so arriving does not reload");
	assert_equal(false, loaded->_is_placeholder, "successful prefetch replaces the thumbnail with the image");
	assert_equal(false, loaded->loaded().is_empty(), "successful prefetch holds the decoded image");
	assert_equal(false, loaded->_load_retry_pending, "successful prefetch arms no retry");
}

// A reopen queued by detach_file_handles lands on the UI thread long after the teardown that queued
// it. If a second teardown or a navigation has taken the display since, publishing would reopen the
// file the caller is renaming, replacing or deleting -- the handle the detach exists to release. The
// generation is what makes that visible to the callback, which then closes rather than publishes.
static void should_reject_superseded_av_session()
{
	null_async_strategy as;
	common_display_state_t common;
	const auto d = std::make_shared<display_state_t>(as, common);

	const auto first = ++d->_av_generation;
	const auto opened = make_test_session();

	assert_equal(true, d->publish_av_session(opened, first), "current generation publishes");
	assert_equal(true, d->_session == opened, "session installed on the display");

	// A teardown supersedes it and clears the display, exactly as detach_file_handles does.
	const auto second = ++d->_av_generation;
	d->_session.reset();

	const auto late = make_test_session();
	assert_equal(false, d->publish_av_session(late, first), "superseded generation rejected");
	assert_equal(true, d->_session == nullptr, "display left detached, caller owns closing the session");

	assert_equal(true, d->publish_av_session(late, second), "current generation publishes again");
	assert_equal(true, d->_session == late, "newest session installed");
}

static void should_keep_file_handles_detached_until_last_operation()
{
	null_state_strategy state_strategy;
	deferred_async_strategy async;
	const location_cache locations;
	index_state index(async, locations);
	view_state state(state_strategy, async, index, make_test_player());
	const auto initial_detach_count = df::file_handles_detached.load();

	auto first = std::make_shared<detach_file_handles>(state);
	auto second = std::make_shared<detach_file_handles>(state);
	assert_equal(initial_detach_count + 2, df::file_handles_detached.load(), "both operations detached");

	first.reset();
	assert_equal(initial_detach_count + 1, df::file_handles_detached.load(), "later operation remains detached");
	assert_equal(0ull, static_cast<uint64_t>(async.pending_worker_count(async_queue::scan_modified_items)),
	             "no intermediate rescan");

	second.reset();
	assert_equal(initial_detach_count, df::file_handles_detached.load(), "final operation restores handles");
	assert_equal(1ull, static_cast<uint64_t>(async.pending_worker_count(async_queue::scan_modified_items)),
	             "one final rescan");
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

//
// Map tiles (OpenStreetMap tile usage policy compliance)
//

static void should_build_tile_user_agent()
{
	// The OSM tile usage policy requires a User-Agent that names the app + version
	// and provides a contact URL; a generic/library-default agent is blocked.
	const auto ua = tile_user_agent();
	const auto expected = std::format("Diffractor/{} (+https://diffractor.com)", s_app_version);

	assert_equal(expected, ua, "stable product, version and contact url", "tile user agent");
	assert_equal(std::string::npos, ua.find_first_of("\r\n\t"), "contains no invalid header characters",
	             "tile user agent");
}

static void should_pack_tile_database_keys()
{
	// Zoom caps at 18, so x and y stay below 2^20 and the whole address fits a positive int64 -
	// which is what lets the store key its rows on the rowid.
	assert_equal(true, map_tile_db_key(0, 0, 0) == 0, "origin", "tile key");

	constexpr auto widest = 1 << map_max_zoom;
	assert_equal(true, map_tile_db_key(map_max_zoom, widest - 1, widest - 1) > 0, "widest address stays positive",
	             "tile key");

	std::set<int64_t> seen;

	for (auto z = map_min_zoom; z <= map_max_zoom; ++z)
	{
		const auto span = 1 << z;

		for (const auto x : {0, 1, span / 2, span - 1})
		{
			for (const auto y : {0, 1, span / 2, span - 1})
			{
				assert_equal(true, seen.insert(map_tile_db_key(z, x, y)).second, "address is unique", "tile key");
			}
		}
	}
}

static df::date_t tile_days_ago(const uint32_t days)
{
	return df::date_t(platform::now().to_int64() - days * df::date_t::intervals_per_day);
}

static void should_cache_tiles_in_a_database()
{
	tile_cache_db db;
	db.open(_temps.next_path(".db"));

	assert_equal(true, db.is_open(), "database opened", "tile cache");

	constexpr auto key = map_tile_db_key(3, 1, 2);
	assert_equal(true, db.load(key).empty(), "load absent returns empty", "tile cache");

	const df::blob bytes = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a, 1, 2, 3, 4};
	db.store(key, df::cspan(bytes));
	db.flush();

	const auto loaded = db.load(key);
	assert_equal(static_cast<int>(bytes.size()), static_cast<int>(loaded.size()), "size round-trips", "tile cache");
	assert_equal(true, loaded == bytes, "bytes round-trip", "tile cache");
	assert_equal(1, static_cast<int>(db.count()), "one row stored", "tile cache");

	const auto path = db.path();
	db.close();

	// A fresh connection over the same file still finds it (persists between sessions).
	tile_cache_db reopened;
	reopened.open(path);
	assert_equal(true, reopened.load(key) == bytes, "persists across sessions", "tile cache");
	reopened.close();
}

static void should_prune_unused_tiles()
{
	tile_cache_db db;
	db.open(_temps.next_path(".db"));

	const df::blob bytes = {1, 2, 3, 4};
	const auto long_ago = tile_days_ago(60);

	for (auto i = 0; i < 3; ++i)
	{
		db.store(map_tile_db_key(5, i, i), df::cspan(bytes), long_ago);
	}

	db.flush();
	assert_equal(3, static_cast<int>(db.count()), "stored before prune", "tile cache");

	db.prune(30, tile_cache_db::max_bytes);
	assert_equal(0, static_cast<int>(db.count()), "tiles unused for longer than the window are dropped",
	             "tile cache");

	db.close();
}

static void should_bound_tile_cache_by_size()
{
	tile_cache_db db;
	db.open(_temps.next_path(".db"));

	const df::blob bytes(8192, 0x5a);
	const auto long_ago = tile_days_ago(60);

	for (auto i = 0; i < 8; ++i)
	{
		db.store(map_tile_db_key(6, i, i), df::cspan(bytes), long_ago);
	}

	db.flush();
	assert_equal(8, static_cast<int>(db.count()), "stored before prune", "tile cache");

	// A window wide enough that the age pass matches nothing, so only the size pass can evict.
	db.prune(365, 1);
	assert_equal(0, static_cast<int>(db.count()), "size cap evicts least recently used", "tile cache");

	db.close();
}

static void should_keep_tiles_inside_the_retention_window()
{
	// The OSM tile usage policy asks clients to keep what they download for at least a week, so a
	// prune that has run out of anything older must give up rather than evict a fresh tile.
	tile_cache_db db;
	db.open(_temps.next_path(".db"));

	const df::blob bytes(8192, 0x5a);

	for (auto i = 0; i < 4; ++i)
	{
		db.store(map_tile_db_key(7, i, i), df::cspan(bytes));
	}

	db.flush();
	db.prune(0, 1);

	assert_equal(4, static_cast<int>(db.count()), "recently fetched tiles survive any cap", "tile cache");

	db.close();
}

static void should_replace_an_unreadable_tile_cache()
{
	const auto path = _temps.next_path(".db");
	const df::blob junk(4096, 0x7f);
	df::blob_save_to_file(df::cspan(junk), path);

	tile_cache_db db;
	db.open(path);

	assert_equal(true, db.is_open(), "a file this build cannot read is replaced", "tile cache");

	constexpr auto key = map_tile_db_key(4, 5, 6);
	const df::blob bytes = {9, 8, 7};
	db.store(key, df::cspan(bytes));
	db.flush();

	assert_equal(true, db.load(key) == bytes, "usable after replacement", "tile cache");

	db.close();
}

static void should_resolve_tile_cache_db_beside_index_db()
{
	// The tile database shares the folder the index database (diffractor-cache.db) is opened from.
	const auto db_path = resolve_tile_cache_db_path();
	const auto base = known_path(platform::known_folder::app_cache_data);

	assert_equal(true, db_path == base.combine_file("map-tiles-cache.db"sv), "database beside index database",
	             "tile cache location");
	assert_equal(true, base.exists(), "base folder exists", "tile cache location");
}

static void should_query_kdtree_bounds()
{
	// A 10x10 integer grid of points; offset encodes the original index.
	std::vector<kd_coordinates_t> pts;
	for (int gx = 0; gx < 10; ++gx)
	{
		for (int gy = 0; gy < 10; ++gy)
		{
			kd_coordinates_t c;
			c.x = static_cast<float>(gx);
			c.y = static_cast<float>(gy);
			c.offset = static_cast<uint32_t>(gx * 10 + gy);
			c.country = 0;
			pts.push_back(c);
		}
	}

	kd_tree tree;
	tree.build(pts); // reorders pts, but offset travels with each point

	// x in {3,4,5} and y in {3,4,5} => 9 points.
	std::vector<kd_coordinates_t> found;
	tree.find_in_bounds(pts, 2.5f, 2.5f, 5.5f, 5.5f, found);
	assert_equal(9, static_cast<int>(found.size()), "window count", "kd bounds");

	for (const auto& c : found)
	{
		assert_equal(true, c.x >= 2.5f && c.x <= 5.5f && c.y >= 2.5f && c.y <= 5.5f, "point in range", "kd bounds");
	}

	// A window far from every point returns nothing.
	std::vector<kd_coordinates_t> none;
	tree.find_in_bounds(pts, 100.0f, 100.0f, 200.0f, 200.0f, none);
	assert_equal(0, static_cast<int>(none.size()), "empty window", "kd bounds");

	// A window covering everything returns all points exactly once.
	std::vector<kd_coordinates_t> all;
	tree.find_in_bounds(pts, -1.0f, -1.0f, 100.0f, 100.0f, all);
	assert_equal(100, static_cast<int>(all.size()), "full window", "kd bounds");
}

static void should_anchor_map_marker_cells_to_world()
{
	const gps_coordinate first(50.0755, 14.4378);
	const gps_coordinate nearby(50.07551, 14.43781);

	const auto first_cell = map_marker_world_cell(first, 16);
	const auto nearby_cell = map_marker_world_cell(nearby, 16);

	assert_equal(true, first_cell == map_marker_world_cell(first, 16), "cell is independent of viewport center",
	             "map markers");
	assert_equal(true, first_cell == nearby_cell, "nearby markers aggregate in the same world cell", "map markers");
	assert_equal(false, first_cell == map_marker_world_cell(first, 17), "zoom recalculates world cells", "map markers");
}

static void should_measure_distance_to_map_cells()
{
	constexpr recti cell(10, 20, 30, 40);
	assert_equal(0, static_cast<int>(distance_squared({20, 30}, cell)), "cursor inside cell has zero distance",
	             "map cells");
	assert_equal(25, static_cast<int>(distance_squared({35, 30}, cell)), "horizontal gap is measured", "map cells");
	assert_equal(50, static_cast<int>(distance_squared({35, 45}, cell)), "diagonal gap is measured", "map cells");
	const auto left_distance = distance_squared({50, 30}, cell);
	constexpr auto right_distance = distance_squared({50, 30}, recti(60, 20, 80, 40));
	assert_equal(true, right_distance < left_distance, "nearest occupied cell wins between cells", "map cells");
}

static void should_frame_map_on_the_box_that_holds_items()
{
	constexpr sizei extent(800, 600);

	// locations.md 5.5: the gesture is to see a hot spot and click it, so a map that opens has
	// to show the region that holds items rather than an arbitrary coordinate inside it.
	const map_box empty;
	assert_equal(false, empty.valid, "an empty box frames nothing", "map framing");
	assert_equal(false, empty.centre().is_valid(), "an empty box has no centre", "map framing");

	map_box city;
	city.add(gps_coordinate(51.4, -0.3));
	city.add(gps_coordinate(51.6, 0.1));

	const auto centre = city.centre();
	assert_equal(true, std::abs(centre.latitude() - 51.5) < 0.0001, "centre latitude", "map framing");
	assert_equal(true, std::abs(centre.longitude() - -0.1) < 0.0001, "centre longitude", "map framing");

	map_box world;
	world.add(gps_coordinate(-60.0, -170.0));
	world.add(gps_coordinate(60.0, 170.0));

	const auto city_zoom = map_fit_zoom(city, extent);
	const auto world_zoom = map_fit_zoom(world, extent);

	assert_equal(true, city_zoom > world_zoom, "a smaller box frames closer", "map framing");
	assert_equal(true, world_zoom >= map_min_zoom && city_zoom <= map_max_zoom, "framing stays on served tiles",
	             "map framing");

	// The fitted zoom shows the whole box; one step closer would cut it off.
	const auto span_px = [](const map_box& box, const int zoom)
	{
		return std::max(
			(lon_to_tile_x(box.max_longitude, zoom) - lon_to_tile_x(box.min_longitude, zoom)) * TILE_SIZE,
			(lat_to_tile_y(box.min_latitude, zoom) - lat_to_tile_y(box.max_latitude, zoom)) * TILE_SIZE);
	};

	assert_equal(true, span_px(city, city_zoom) <= extent.cx, "the framed box fits", "map framing");
	assert_equal(true, span_px(city, city_zoom + 1) > extent.cx - TILE_SIZE, "framing is not needlessly wide",
	             "map framing");

	map_box point;
	point.add(gps_coordinate(51.5, -0.1));
	assert_equal(map_max_zoom, map_fit_zoom(point, extent), "a single place frames as close as the tiles go",
	             "map framing");

	assert_equal(map_min_zoom, map_fit_zoom(city, sizei(0, 0)), "an unlaid-out map cannot frame", "map framing");
}

static void should_build_aggregate_location_matrix()
{
	location_matrix matrix({16, 44, -60.0, -30.0, 60.0, 170.0});
	const gps_coordinate prague1(50.0755, 14.4378);
	const gps_coordinate prague2(50.0756, 14.4379);
	const gps_coordinate sydney(-33.8688, 151.2093);
	const gps_coordinate excluded_by_bounds(64.1466, -21.9426);
	matrix.add(df::file_path("c:\\b.jpg"), prague2, true, 5);
	matrix.add(df::file_path("c:\\sydney.jpg"), sydney, true, 0);
	matrix.add(df::file_path("c:\\a.jpg"), prague1, true, 3);
	matrix.add(df::file_path("c:\\reykjavik.jpg"), excluded_by_bounds, true, 5);
	matrix.finalize();

	assert_equal(2_z, matrix.cells.size(), "only occupied in-bounds cells are stored", "location matrix");
	const auto prague = std::ranges::find(matrix.cells, matrix.params.cell(prague1), &location_matrix::cell::index);
	assert_equal(true, prague != matrix.cells.end(), "nearby items share a cell", "location matrix");
	assert_equal(2, static_cast<int>(prague->count), "cell retains the item count", "location matrix");
	assert_equal("b.jpg", prague->representative_path.name(), "high-rated media is representative", "location matrix");
	assert_equal(50.07555, prague->centroid.latitude(), "cell coordinate is the centroid", "location matrix");
	assert_equal(50.0755, prague->min_latitude, "cell retains minimum latitude", "location matrix");
	assert_equal(14.4379, prague->max_longitude, "cell retains maximum longitude", "location matrix");

	location_matrix ranked;
	ranked.add(df::file_path("c:\\0.txt"), prague1, false, 5);
	ranked.add(df::file_path("c:\\a.jpg"), prague1, true, 3);
	ranked.add(df::file_path("c:\\b.jpg"), prague1, true, 5);
	ranked.finalize();
	assert_equal("b.jpg", ranked.cells.front().representative_path.name(),
	             "rated visual media outranks non-visual and lower-rated items", "location matrix");

	location_matrix_params sidebar_params;
	sidebar_params.projection = location_matrix_projection::location_heat_map;
	sidebar_params.area_cell_span = 4;
	const auto heat_cell = df::location_heat_map::calc_map_loc(prague1);
	const auto sidebar_cell = sidebar_params.cell(prague1);
	assert_equal(heat_cell.x / 4 * 4, sidebar_cell.x, "fixed projection aligns horizontally", "location matrix");
	assert_equal(heat_cell.y / 4 * 4, sidebar_cell.y, "fixed projection aligns vertically", "location matrix");
}

static void should_select_thumbnail_representatives_while_counting()
{
	auto make_file = [](const std::string_view name, const int rating)
	{
		df::index_file_item result;
		result.name = str::cache(name);
		result.ft = files::file_type_from_name(name);
		const auto metadata = std::make_shared<prop::item_metadata>();
		metadata->rating = rating;
		result.metadata.store(metadata);
		return result;
	};

	const auto text = make_file("0.txt", 5);
	const auto ordinary_photo = make_file("a.jpg", 3);
	const auto rated_photo = make_file("b.jpg", 5);
	df::file_group_histogram first;
	first.record(text, df::file_path("c:\\0.txt"));
	first.record(ordinary_photo, df::file_path("c:\\a.jpg"));
	assert_equal("a.jpg", first.representative_path.name(), "visual media outranks non-thumbnail files",
	             "summary thumbnail");

	df::file_group_histogram second;
	second.record(rated_photo, df::file_path("c:\\b.jpg"));
	first.add(second);
	assert_equal("b.jpg", first.representative_path.name(), "rated media survives histogram merge",
	             "summary thumbnail");

	df::date_histogram dates;
	dates.record_representative(0, ordinary_photo, df::file_path("c:\\a.jpg"));
	dates.record_representative(0, rated_photo, df::file_path("c:\\b.jpg"));
	assert_equal("b.jpg", dates.representative_paths[0].name(), "date bucket prioritises rated media",
	             "summary thumbnail");
}

// selection-controls.md: comparison is like with like, decided from stable traits alone.
static void should_limit_comparison_to_like_pairs()
{
	const auto jpg = files::file_type_from_name("a.jpg");
	const auto png = files::file_type_from_name("b.png");
	const auto mp4 = files::file_type_from_name("a.mp4");
	const auto mp3 = files::file_type_from_name("a.mp3");
	const auto txt = files::file_type_from_name("a.txt");

	assert_equal(true, can_compare_file_types(jpg, png), "two images compare");
	assert_equal(true, can_compare_file_types(mp4, mp4), "two previewable videos compare");
	assert_equal(false, can_compare_file_types(jpg, mp4), "image and video do not compare");
	assert_equal(false, can_compare_file_types(mp3, mp3), "two audio files do not compare");
	assert_equal(false, can_compare_file_types(txt, txt), "two documents do not compare");
	assert_equal(false, can_compare_file_types(&file_type::folder, &file_type::folder),
	             "two folders do not compare");
	assert_equal(false, can_compare_file_types(nullptr, jpg), "an unknown type does not compare");
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

void register_tests6(view_state& state, test_registry& tests)
{
	tests.add("Should layout flex elements"s, should_layout_flex_elements);
	tests.add("Should limit comparison to like pairs"s, should_limit_comparison_to_like_pairs);
	//
	// Index
	//
	tests.add("Should find word prefix range"s, should_find_word_prefix_range);
	tests.add("Should encode postings"s, should_encode_postings);
	tests.add("Should query inverted index"s, should_query_inverted_index);
	tests.add("Should query trigram index"s, should_query_trigram_index);
	tests.add("Should materialize detached query item"s, should_materialize_detached_query_item);
	tests.add("Should reject superseded av session"s, should_reject_superseded_av_session);
	tests.add("Should keep file handles detached until last operation"s,
	          should_keep_file_handles_detached_until_last_operation);
	tests.add("Should batch thumbnail publication"s, should_batch_thumbnail_publication);
	tests.add("Should skip unneeded thumbnail staging"s, should_skip_unneeded_thumbnail_staging);
	tests.add("Should reuse cached thumbnail surface"s, should_reuse_cached_thumbnail_surface);
	tests.add("Should accelerate substring prediction"s, should_accelerate_substring_prediction);
	tests.add("Should signal and replace pending queue work"s, should_signal_and_replace_pending_queue_work);
	tests.add("Should predict search completions"s, should_predict_search_completions);
	tests.add("Should index"s, should_index);
	tests.add("Should create database schema"s, should_create_database_schema);
	tests.add("Should store thumbnails"s, should_store_thumbnails);
	tests.add("Should store cover art"s, should_store_cover_art);
	tests.add("Should store item properties"s, should_store_item_properties);
	tests.add("Should invalidate cached metadata from an older build"s,
	          should_invalidate_cached_metadata_written_by_an_older_build);
	tests.add("Should replace an unreadable database"s, should_replace_an_unreadable_database);
	tests.add("Should run without a database"s, should_run_without_a_database);
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
	tests.add("Should update collection presence"s, should_update_collection_presence);
	tests.add("Should discard stale presence result"s, should_discard_stale_presence_result);
	tests.add("Should discard stale scan item update"s, should_discard_stale_scan_item_update);
	tests.add("Should discard stale CRC result"s, should_discard_stale_crc_result);
	tests.add("Should shape the tile by what it draws"s, should_shape_the_tile_by_what_it_draws);
	tests.add("Should not justify a tile shaped by a thumbnail"s, should_not_justify_a_tile_shaped_by_a_thumbnail);
	tests.add("Should keep tile aspect when laying out a row"s, should_keep_tile_aspect_when_laying_out_a_row);
	tests.add("Should Rename"s, should_rename);
	tests.add("Should not overwrite during rename"s, should_not_overwrite_during_rename);
	tests.add("Should rename file case"s, should_rename_file_case);
	tests.add("Should rollback rename when sidecar fails"s, should_rollback_rename_when_sidecar_fails);
	tests.add("Should detect original path"s, should_detect_original_path);
	tests.add("Should not reload thumb when valid"s, should_not_reload_thumb_when_valid);
	tests.add("Should reuse persisted hover thumbnail until video changes"s,
	          should_reuse_persisted_hover_thumbnail_until_video_changes);
	tests.add("Should reload thumb after scan"s, should_reload_thumb_after_scan);
	tests.add("Should not reread after metadata write"s, should_not_reread_after_metadata_write);
	tests.add("Should count overlapping write claims"s, should_count_overlapping_write_claims);
	tests.add("Should detect rotation"s, should_detect_rotation);
	tests.add("Should parse roots"s, should_parse_roots);
	tests.add("Should parse drive label roots"s, should_parse_drive_label_roots);
	tests.add("Should toggle collection entry"s, should_toggle_collection_entry);
	tests.add("Should record crashes"s, should_record_crashes);
	tests.add("Should index concurrently"s, should_index_concurrently);
	tests.add("Should index offline OneDrive placeholder"s, should_index_offline_placeholder);
	tests.add("Should clear failed thumbnail on hydration"s, should_clear_failed_thumbnail_on_hydration);
	tests.add("Should refresh same item metadata after hydration"s, should_refresh_same_item_metadata_after_hydration);
	tests.add("Should trigger rescan only after full metadata load"s,
	          should_trigger_rescan_only_after_full_metadata_load);
	tests.add("Should retry after failed prefetch"s, should_retry_after_failed_prefetch);
	tests.add("Should preserve metadata when dehydrated"s, should_preserve_metadata_when_dehydrated);
	tests.add("Should fetch shell thumbnail only for offline visible"s,
	          should_fetch_shell_thumbnail_only_for_offline_visible);
	tests.add("Should bound shell thumbnail retries"s, should_bound_shell_thumbnail_retries);
	tests.add("Should discard stale thumbnail surface"s, should_discard_stale_thumbnail_surface);
	tests.add("Should rescan collection after forgetting cache"s, should_rescan_collection_after_forgetting_cache);

	//
	// Map tiles
	//
	tests.add("Should build tile user agent"s, should_build_tile_user_agent);
	tests.add("Should pack tile database keys"s, should_pack_tile_database_keys);
	tests.add("Should cache tiles in a database"s, should_cache_tiles_in_a_database);
	tests.add("Should prune unused tiles"s, should_prune_unused_tiles);
	tests.add("Should bound tile cache by size"s, should_bound_tile_cache_by_size);
	tests.add("Should keep tiles inside the retention window"s, should_keep_tiles_inside_the_retention_window);
	tests.add("Should replace an unreadable tile cache"s, should_replace_an_unreadable_tile_cache);
	tests.add("Should resolve tile cache db beside index db"s, should_resolve_tile_cache_db_beside_index_db);
	tests.add("Should query kd-tree bounds"s, should_query_kdtree_bounds);
	tests.add("Should anchor map marker cells to world"s, should_anchor_map_marker_cells_to_world);
	tests.add("Should measure distance to map cells"s, should_measure_distance_to_map_cells);
	tests.add("Should frame map on the box that holds items"s, should_frame_map_on_the_box_that_holds_items);
	tests.add("Should build aggregate location matrix"s, should_build_aggregate_location_matrix);
	tests.add("Should select thumbnail representatives while counting"s,
	          should_select_thumbnail_representatives_while_counting);

	//
	// UI
	//
	tests.add("Should select correctly"s, should_select_items);
	tests.add("Should Enable based on selection"s, should_enable_based_on_selection);
	tests.add("Should toggle rating"s, should_toggle_rating);

#ifndef _DEBUG
	tests.add("Should not crash on JPEG"s, [] { should_not_crash("small.jpg"); });
	tests.add("Should not crash on GIF"s, [] { should_not_crash("tuesday.gif"); });
	tests.add("Should not crash on TIFF"s, [] { should_not_crash("small.tif"); });
	tests.add("Should not crash on PNG"s, [] { should_not_crash("cube.png"); });
	tests.add("Should not crash on WEBP"s, [] { should_not_crash("lake.webp"); });
#endif
}
