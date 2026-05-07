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
#include "test_utils.h"
#include "model_db_pack.h"
#include "util_crash_files_db.h"
#include "app_util.h"
#include "metadata_exif.h"
#include "metadata_iptc.h"
#include "metadata_xmp.h"

static void should_store_thumbnails()
{
	const auto index_path = _temps.next_path();
	const auto file_path = test_files_folder.combine_file("Test.jpg");

	null_async_strategy as;
	location_cache locations;
	index_state index(as, locations);

	database db(index);
	db.open(index_path.folder(), index_path.file_name_without_extension());

	auto i = load_item(index, file_path, true);
	db.perform_writes();

	auto thumb = db.load_thumbnail(i->path());
	assert_equal(i->thumbnail(), thumb.thumb, "local loaded thumb");
}

static void should_store_cover_art()
{
	const auto index_path = _temps.next_path();
	const auto file_path = test_files_folder.combine_file("indy.mp4");

	null_async_strategy as;
	location_cache locations;
	index_state index(as, locations);

	database db(index);
	db.open(index_path.folder(), index_path.file_name_without_extension());

	auto i = load_item(index, file_path, true);
	db.perform_writes();

	auto thumb = db.load_thumbnail(i->path());
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
	location_cache locations;
	index_state index(as, locations);
	database db(index);
	db.open(index_path.folder(), index_path.file_name_without_extension());

	constexpr auto key = "key   xxxxxxxxxx";
	const auto value = long_text;
	db.web_service_cache(key, value);
	const auto result = db.web_service_cache(key);

	assert_equal(result, value, "web_service_cache");
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
	view_host_base_ptr view;

	location_cache locations;
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
}

static void should_toggle_rating()
{
	const df::file_path src_path1(test_files_folder, "Test.jpg");
	const df::file_path src_path2(test_files_folder, "Gherkin.CR2");

	const auto save_path_1 = _temps.next_path(".jpg");
	const auto save_path_2 = _temps.next_path(".cr2");

	platform::copy_file(src_path1, save_path_1, false, false);
	platform::copy_file(src_path2, save_path_2, false, false);

	const auto temp_folder = _temps.folder();

	null_state_strategy ss;
	null_async_strategy as;
	view_host_base_ptr view;
	auto results = std::make_shared<null_item_results_ui>();

	location_cache locations;
	index_state index(as, locations);
	view_state s(ss, as, index, make_test_player());
	s.view_mode(view_type::items);
	s.open(view, df::search_t().add_selector(temp_folder), {});
	s.item_index.scan_items(s.search_items(), false, false, false, false, test_token);
	s.update_item_groups();

	s.select(view, save_path_1.name(), false);
	s.update_selection();

	assert_equal(1_z, s.selected_count(), "invalid selection");
	s.toggle_rating(results, s.selected_items().items(), 3, view);
	s.item_index.scan_items(s.search_items(), false, false, false, false, test_token);
	assert_equal(3, s.displayed_rating(), "invalid jpeg rating");

	s.toggle_rating(results, s.selected_items().items(), 0, view);
	s.item_index.scan_items(s.search_items(), false, false, false, false, test_token);
	assert_equal(0, s.displayed_rating(), "invalid remove jpeg rating");

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
	view_host_base_ptr view;

	location_cache locations;
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
}

static void should_detect_original_path()
{
	const df::file_path path("c:\\temp\\test.original.jpg");
	assert_equal(true, path.is_original(), "detect original");
}

static void should_not_reload_thumb_when_valid()
{
	const auto load_path = test_files_folder.combine_file("test.jpg");

	const df::date_t date(1972, 5, 25);
	const df::date_t date2(1972, 5, 26);

	files ff;
	const auto loaded = ff.load(load_path, false);

	const auto i_local = std::make_shared<df::item_element>(load_path, make_index_file_info(date));
	assert_equal(false, i_local->should_load_thumbnail(), "should not load by default");

	i_local->db_thumb_query_complete();
	assert_equal(true, i_local->should_load_thumbnail(), "should load after db load");

	i_local->thumbnail(loaded.i, nullptr);
	assert_equal(true, i_local->should_load_thumbnail(), "should load without timestamp");

	i_local->thumbnail(loaded.i, nullptr, date);
	assert_equal(false, i_local->should_load_thumbnail(), "should load if thumb but not hash");

	i_local->update(load_path, make_index_file_info(date2));
	assert_equal(true, i_local->should_load_thumbnail(), "should if date changes");
}

static void should_reload_thumb_after_scan()
{
	files ff;
	const auto index_path = _temps.next_path();
	null_async_strategy as;
	location_cache locations;
	index_state index(as, locations);

	database db(index);
	db.open(index_path.folder(), index_path.file_name_without_extension());
	build_index(index, db);

	auto path_test = df::file_path(test_files_folder, "Test.jpg");
	auto path_sony = df::file_path(test_files_folder, "Sony.jpg");

	const auto test_item = load_item(index, path_test, false);
	const auto sony_item = load_item(index, path_sony, false);

	assert_equal(false, test_item->should_load_thumbnail(), "should_load_thumbnail for test.jpg");
	assert_equal(false, sony_item->should_load_thumbnail(), "should_load_thumbnail for sony.jpg");

	df::item_set items;
	items._items = {test_item, sony_item};

	index.scan_items(items, false, false, false, false, test_token);
	db.perform_writes();

	assert_equal(false, test_item->should_load_thumbnail(), "should_load_thumbnail for test.jpg");
	assert_equal(false, sony_item->should_load_thumbnail(), "should_load_thumbnail for sony.jpg");

	db.load_thumbnails(index, items);

	assert_equal(true, test_item->should_load_thumbnail(),
	             "should_load_thumbnail for test.jpg after db load_thumbnails");
	assert_equal(true, sony_item->should_load_thumbnail(),
	             "should_load_thumbnail for sony.jpg after db load_thumbnails");

	index.scan_items(items, true, false, false, false, test_token);
	db.perform_writes();

	assert_equal(false, test_item->should_load_thumbnail(),
	             "should_load_thumbnail for test.jpg after index load_thumbnails");
	assert_equal(false, sony_item->should_load_thumbnail(),
	             "should_load_thumbnail for sony.jpg after index load_thumbnails");
}

static void should_rename()
{
	null_async_strategy as;
	location_cache locations;
	index_state index(as, locations);

	const df::file_path src_path(test_files_folder, "Test.jpg");
	const auto save_path_1 = _temps.next_path(".jpg");
	const auto save_path_2 = _temps.next_path(".jpg");

	platform::copy_file(src_path, save_path_1, false, false);

	auto test_item = load_item(index, save_path_1, false);

	assert_equal(true, test_item->rename(index, save_path_2.file_name_without_extension()).success(), "can rename");
	assert_equal(save_path_2.name(), test_item->name(), "renamed");
	assert_equal(true, save_path_2.exists(), "renamed exists");
}

static void should_not_overwrite_during_rename()
{
	null_async_strategy as;
	location_cache locations;
	index_state index(as, locations);

	const df::file_path src_path(test_files_folder, "Test.jpg");
	const auto save_path_1 = _temps.next_path(".jpg");
	const auto save_path_2 = _temps.next_path(".jpg");

	platform::copy_file(src_path, save_path_1, false, false);
	platform::copy_file(src_path, save_path_2, false, false);

	auto test_item = load_item(index, save_path_1, false);

	assert_equal(false, test_item->rename(index, save_path_2.file_name_without_extension()).success(),
	             "should not rename");
	assert_equal(save_path_1.name(), test_item->name(), "not renamed");
	assert_equal(true, test_item->path().exists(), "exists");
}

ui::const_image_ptr transform_jpeg(const ui::const_image_ptr& image, const simple_transform transform)
{
	df::assert_true(is_jpeg(image));

	const jpeg_decoder_x decoder;
	jpeg_encoder encoder;
	return load_image_file(decoder.transform(image->data(), encoder, transform));
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
	db.load_thumbnails(index, items);

	index.scan_item(test_item1, true, false);
	index.scan_item(test_item2, true, false);
	index.scan_item(test_item3, true, false);
	index.scan_item(test_item4, true, false);
	index.scan_item(test_item5, true, false);
	index.scan_item(sony_item, true, false);

	index.update_predictions();

	assert_equal(1u, index.find_item(test_item1->path()).duplicates.count, "duplicates");
	assert_equal(1u, index.find_item(test_item2->path()).duplicates.count, "duplicates");
	assert_equal(1u, index.find_item(test_item3->path()).duplicates.count, "duplicates");
	assert_equal(1u, index.find_item(sony_item->path()).duplicates.count, "duplicates");
}

static void should_detect_rotation(shared_test_context& stc)
{
	files ff;
	const auto index_path = _temps.next_path();
	null_async_strategy as;
	location_cache locations;
	index_state index(as, locations);

	database db(index);
	db.open(index_path.folder(), index_path.file_name_without_extension());

	df::index_roots paths;
	paths.folders.emplace(test_files_folder);

	index.index_roots(paths);
	index.index_folders(test_token);

	const auto path_test = df::file_path(test_files_folder, "exif-rotated.jpg");
	const auto test_item = std::make_shared<df::item_element>(path_test, index.find_item(path_test));

	assert_equal(ui::orientation::top_left, test_item->thumbnail_orientation());

	df::item_set items;
	items._items = {test_item};

	index.scan_items(items, false, false, false, false, test_token);
	db.perform_writes();

	assert_equal(ui::orientation::right_top, test_item->thumbnail_orientation());

	db.load_thumbnails(index, items);

	assert_equal(ui::orientation::right_top, test_item->thumbnail_orientation());

	index.scan_items(items, true, false, false, false, test_token);
	db.perform_writes();

	assert_equal(ui::orientation::right_top, test_item->thumbnail_orientation());
	assert_equal(ui::orientation::right_top, test_item->thumbnail()->orientation());
}

static void should_record_crashes()
{
	const auto db_path = _temps.next_path();
	const auto paths = {
		test_files_folder.combine_file("Test.jpg"),
		test_files_folder.combine_file("Test90.jpg"),
		test_files_folder.combine_file("Small.jpg"),
		test_files_folder.combine_file("Lossless0.jpg")
	};

	for (auto path : paths)
	{
		{
			crash_files_db test_crash_files(db_path);

			assert_equal(test_crash_files.is_known_crash_file(path), false, "is_known_crash_file");

			test_crash_files.add_open(path, str::utf8_cast(__FUNCTION__));
			test_crash_files.remove_open(path);
			test_crash_files.flush_open_files();
		}

		{
			crash_files_db test_crash_files(db_path);

			assert_equal(test_crash_files.is_known_crash_file(path), false, "is_known_crash_file");

			test_crash_files.add_open(path, str::utf8_cast(__FUNCTION__));
			test_crash_files.flush_open_files();
		}

		{
			crash_files_db test_crash_files(db_path);

			assert_equal(test_crash_files.is_known_crash_file(path), true, "is_known_crash_file");
		}
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

	files ff;

	auto* const data = blob.data();
	const auto size = blob.size();

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
	}

	constexpr auto truncation_steps = 32;

	for (auto i = 0u; i < truncation_steps && !df::is_closing; i++)
	{
		const auto save_path = _temps.next_path(ext);
		write_binary_file(save_path, data, df::mul_div(static_cast<int>(size), i, truncation_steps));
		ff_scan_and_load_thumb(ff, save_path);
	}
}

void register_tests6(view_state& state, test_registry& tests)
{
	//
	// Index
	//
	tests.add("Should index"s, should_index);
	tests.add("Should store thumbnails"s, should_store_thumbnails);
	tests.add("Should store cover art"s, should_store_cover_art);
	tests.add("Should store item properties"s, should_store_item_properties);
	tests.add("Should store pack properties"s, should_pack_item_properties);
	tests.add("Should store webservice results"s, should_store_webservice_results);
	tests.add("Should detect duplicates"s, should_detect_duplicates);
	tests.add("Should Rename"s, should_rename);
	tests.add("Should not overwrite during rename"s, should_not_overwrite_during_rename);
	tests.add("Should detect original path"s, should_detect_original_path);
	tests.add("Should not reload thumb when valid"s, should_not_reload_thumb_when_valid);
	tests.add("Should reload thumb after scan"s, should_reload_thumb_after_scan);
	tests.add("Should detect rotation"s, should_detect_rotation);
	tests.add("Should parse roots"s, should_parse_roots);
	tests.add("Should toggle collection entry"s, should_toggle_collection_entry);
	tests.add("Should record crashes"s, should_record_crashes);

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
