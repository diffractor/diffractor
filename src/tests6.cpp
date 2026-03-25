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

static void should_store_thumbnails()
{
	const auto index_path = _temps.next_path();
	const auto file_path = test_files_folder.combine_file(u8"Test.jpg"sv);

	null_async_strategy as;
	location_cache locations;
	index_state index(as, locations);

	database db(index);
	db.open(index_path.folder(), index_path.file_name_without_extension());

	auto i = load_item(index, file_path, true);
	db.perform_writes();

	auto thumb = db.load_thumbnail(i->path());
	assert_equal(i->thumbnail(), thumb.thumb, u8"local loaded thumb"sv);
}

static void should_store_cover_art()
{
	const auto index_path = _temps.next_path();
	const auto file_path = test_files_folder.combine_file(u8"indy.mp4"sv);

	null_async_strategy as;
	location_cache locations;
	index_state index(as, locations);

	database db(index);
	db.open(index_path.folder(), index_path.file_name_without_extension());

	auto i = load_item(index, file_path, true);
	db.perform_writes();

	auto thumb = db.load_thumbnail(i->path());
	assert_equal(i->cover_art(), thumb.cover_art, u8"local loaded cover art"sv);
}

static void should_store_item_properties()
{
	const auto index_path = _temps.next_path();
	const auto file_path = test_files_folder.combine_file(u8"Test.jpg"sv);

	null_async_strategy as;
	location_cache locations;
	index_state index(as, locations);

	database db(index);
	db.open(index_path.folder(), index_path.file_name_without_extension());

	auto data = blob_from_file(file_path);
	const auto crc32c_expected = crypto::crc32c(data.data(), data.size());

	auto md = std::make_shared<prop::item_metadata>();
	md->album = u8"test"_c;
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

	assert_metadata(*md, *item_md, u8"index"sv);
	assert_equal(crc32c_expected, item.crc32c, u8"index crc32"sv);
	assert_equal(static_cast<int>(media_pos), static_cast<int>(item_md->media_position),
	             u8"index media position"sv);
	assert_equal(md->orientation, item_md->orientation, u8"index orientation"sv);

	const auto reloaded_crc = platform::file_crc32(file_path);
	assert_equal(reloaded_crc, item.crc32c, u8"platform::file_crc32 crc32"sv);
}

static void should_pack_item_properties()
{
	const auto file_path = test_files_folder.combine_file(u8"Test.jpg"sv);
	const auto md = extract_properties(file_path);
	md->album = u8"test"_c;
	md->orientation = ui::orientation::bottom_right;

	metadata_packer packer;
	packer.pack(md);

	const auto unpacked = std::make_shared<prop::item_metadata>();

	metadata_unpacker unpacker(packer.cdata());
	unpacker.unpack(unpacked);

	assert_metadata(*md, *unpacked, u8"index"sv);
	assert_equal(md->orientation, unpacked->orientation, u8"index orientation"sv);
}

static void should_store_webservice_results()
{
	const auto index_path = _temps.next_path();

	null_async_strategy as;
	location_cache locations;
	index_state index(as, locations);
	database db(index);
	db.open(index_path.folder(), index_path.file_name_without_extension());

	constexpr auto key = u8"key   xxxxxxxxxx"sv;
	const auto value = long_text;
	db.web_service_cache(key, value);
	const auto result = db.web_service_cache(key);

	assert_equal(result, value, u8"web_service_cache"sv);
}

static void should_index(shared_test_context& stc)
{
	stc.lazy_load_index();

	assert_equal(expected_cached_item_count, stc.test_index.stats.media_item_count, u8"cached item count"sv);

	const auto expected_md = expected_test_jpg();
	expected_md->file_name = u8"Test.jpg"_c;
	assert_metadata(*expected_md, *metadata_from_cache(stc.test_index, test_files_folder.combine_file(u8"Test.jpg"sv)),
	                u8"Test.jpg"sv);

	const auto actual = metadata_from_cache(stc.test_index, test_files_folder.combine_file(u8"Gherkin.CR2"sv));
	assert_equal(u8"Canon"sv, actual->camera_manufacturer, u8"camera_manufacturer"sv);
	assert_equal(u8"United Kingdom"sv, actual->location_country, u8"location_country"sv);
	assert_equal(u8"\xA9 Mark Ridgwell"sv, actual->copyright_notice, u8"copyright_notice"sv);
	assert_equal(u8"\"Mark Ridgwell\""sv, actual->copyright_creator, u8"copyright_creator"sv);
}

static void should_toggle_collection_entry(shared_test_context& stc)
{
	const auto local_folders = platform::local_folders();

	settings_t::index_t settings;
	toggle_collection_entry(settings, local_folders.pictures, false);
	assert_equal(true, settings.pictures, u8"pictures add"sv);

	toggle_collection_entry(settings, local_folders.pictures, true);
	toggle_collection_entry(settings, test_files_folder, false);
	assert_equal(false, settings.pictures, u8"pictures remove"sv);
	assert_equal(test_files_folder.text(), settings.more_folders);

	toggle_collection_entry(settings, local_folders.video, false);
	toggle_collection_entry(settings, test_files_folder, true);
	assert_equal(true, settings.video, u8"pictures remove"sv);
	assert_equal({}, settings.more_folders);
}

static void should_parse_roots(shared_test_context& stc)
{
	df::index_roots roots1;
	parse_more_folders(roots1, test_files_folder.text());

	assert_equal(0_z, roots1.files.size(), u8"parsed files"sv);
	assert_equal(0_z, roots1.excludes.size(), u8"parsed excludes"sv);
	assert_equal(1_z, roots1.folders.size(), u8"parsed folder"sv);
	assert_equal(test_files_folder.text(), roots1.folders.begin()->text(), u8"parsed folder"sv);

	df::index_roots roots2;
	const auto exclude_files_folder = test_files_folder.combine(u8"excluded1"sv);
	parse_more_folders(roots2, str::format(u8" - {}\n{}"sv, exclude_files_folder.text(), test_files_folder.text()));

	assert_equal(0_z, roots2.files.size(), u8"parsed files"sv);
	assert_equal(1_z, roots2.excludes.size(), u8"parsed excludes"sv);
	assert_equal(1_z, roots2.folders.size(), u8"parsed folder"sv);
	assert_equal(test_files_folder.text(), roots2.folders.begin()->text(), u8"parsed folder"sv);
	assert_equal(exclude_files_folder.text(), roots2.excludes.begin()->text(), u8"parsed exclude"sv);

	df::index_roots roots3;
	parse_more_folders(roots3, str::format(u8"- secret\n{}\n -exclude*"sv, test_files_folder.text()));

	assert_equal(0_z, roots3.files.size(), u8"parsed files"sv);
	assert_equal(0_z, roots3.excludes.size(), u8"parsed excludes"sv);
	assert_equal(1_z, roots3.folders.size(), u8"parsed folder"sv);
	assert_equal(2_z, roots3.exclude_wildcards.size(), u8"parsed exclude wildcards"sv);

	const std::vector<str::cached> exclude_wildcards(roots3.exclude_wildcards.begin(), roots3.exclude_wildcards.end());

	assert_equal(test_files_folder.text(), roots3.folders.begin()->text(), u8"parsed folder"sv);
	assert_equal(u8"exclude*", exclude_wildcards[0], u8"parsed exclude"sv);
	assert_equal(u8"secret", exclude_wildcards[1], u8"parsed exclude"sv);
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
	             u8"items loaded into state"sv);
	assert_equal(expected_cached_item_count + 2_z, s.search_items().file_paths().size(),
	             u8"items loaded into state with xmp"sv);
	assert_equal(4_z, s.search_items().folder_paths().size(), u8"folders loaded into state"sv);

	assert_equal(0_z, s.selected_count(), u8"invalid selection"sv);

	s.select(view, find_item_n(s, 3), false, false, false);
	s.update_selection();
	assert_equal(1_z, s.selected_count(), u8"invalid selection"sv);

	s.select(view, find_item_n(s, 1), false, false, false);
	s.update_selection();
	assert_equal(1_z, s.selected_count(), u8"invalid selection"sv);

	s.select(view, find_item_n(s, 8), true, false, false);
	s.update_selection();
	assert_equal(2_z, s.selected_count(), u8"invalid control selection"sv);

	s.select(view, find_item_n(s, 1), false, false, false);
	s.update_selection();
	assert_equal(1_z, s.selected_count(), u8"invalid selection"sv);

	s.select(view, find_item_n(s, 4), false, true, false);
	s.update_selection();
	assert_equal(4_z, s.selected_count(), u8"invalid selection"sv);

	s.select(view, find_item_n(s, 2), true, false, false);
	s.update_selection();
	assert_equal(3_z, s.selected_count(), u8"invalid selection"sv);

	s.select(view, find_item_n(s, 6), true, false, false);
	s.update_selection();
	assert_equal(4_z, s.selected_count(), u8"invalid selection"sv);
}

static void should_toggle_rating()
{
	const df::file_path src_path1(test_files_folder, u8"Test.jpg"sv);
	const df::file_path src_path2(test_files_folder, u8"Gherkin.CR2"sv);

	const auto save_path_1 = _temps.next_path(u8".jpg"sv);
	const auto save_path_2 = _temps.next_path(u8".cr2"sv);

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

	assert_equal(1_z, s.selected_count(), u8"invalid selection"sv);
	s.toggle_rating(results, s.selected_items().items(), 3, view);
	s.item_index.scan_items(s.search_items(), false, false, false, false, test_token);
	assert_equal(3, s.displayed_rating(), u8"invalid jpeg rating"sv);

	s.toggle_rating(results, s.selected_items().items(), 0, view);
	s.item_index.scan_items(s.search_items(), false, false, false, false, test_token);
	assert_equal(0, s.displayed_rating(), u8"invalid remove jpeg rating"sv);

	s.select(view, save_path_2.name(), false);
	s.update_selection();

	assert_equal(1_z, s.selected_count(), u8"invalid selection"sv);
	s.toggle_rating(results, s.selected_items().items(), 4, view);
	s.item_index.scan_items(s.search_items(), false, false, false, false, test_token);
	assert_equal(4, s.displayed_rating(), u8"invalid cr2 rating"sv);

	s.item_index.scan_items(s.search_items(), true, false, false, false, test_token);
	assert_equal(4, s.displayed_rating(), u8"invalid cr2 rating after thumbnail load"sv);

	s.toggle_rating(results, s.selected_items().items(), 0, view);
	s.item_index.scan_items(s.search_items(), false, false, false, false, test_token);
	assert_equal(0, s.displayed_rating(), u8"invalid remove cr2 rating"sv);
}

static void assert_can_process(const view_state& s, const bool photos_only, const bool can_save_pixels,
                               const bool can_save_metadata,
                               const bool local_file, const bool local_file_or_folder, const std::u8string_view message)
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

	assert_equal(0_z, s.selected_count(), u8"by default nothing selected"sv);
	assert_can_process(s, false, false, false, false, false, u8"nothing selected"sv);

	s.select(view, u8"test.jpg"sv, false);
	s.update_selection();

	assert_equal(1_z, s.selected_count(), u8"select test.jpg"sv);
	assert_can_process(s, true, true, true, true, true, u8"photo selected"sv);

	s.select(view, u8"gizmo.mp4"sv, false);
	s.update_selection();

	assert_equal(1_z, s.selected_count(), u8"select gizmo.mp4"sv);
	assert_can_process(s, false, false, true, true, true, u8"mp4 selected"sv);

	s.select(view, u8"Gherkin.xmp"sv, false);
	s.update_selection();

	assert_equal(1_z, s.selected_count(), u8"Gherkin.xmp"sv);
	assert_can_process(s, false, false, true, true, true, u8"xmp selected"sv);

	s.select(view, u8"test.jpg"sv, false);
	s.select(view, u8"gizmo.mp4"sv, true);
	s.update_selection();

	assert_equal(2_z, s.selected_count(), u8"test.jpg and gizmo.mp4"sv);
	assert_can_process(s, false, false, true, true, true, u8"jpg and mp4 selected"sv);

	s.select(view, u8"raw"sv, false);
	s.update_selection();

	assert_equal(1_z, s.selected_count(), u8"folder"sv);
	assert_can_process(s, false, false, false, false, true, u8"folder selected"sv);
}

static void should_detect_original_path()
{
	const df::file_path path(u8"c:\\temp\\test.original.jpg"sv);
	assert_equal(true, path.is_original(), u8"detect original"sv);
}

static void should_not_reload_thumb_when_valid()
{
	const auto load_path = test_files_folder.combine_file(u8"test.jpg"sv);

	const df::date_t date(1972, 5, 25);
	const df::date_t date2(1972, 5, 26);

	files ff;
	const auto loaded = ff.load(load_path, false);

	const auto i_local = std::make_shared<df::item_element>(load_path, make_index_file_info(date));
	assert_equal(false, i_local->should_load_thumbnail(), u8"should not load by default"sv);

	i_local->db_thumb_query_complete();
	assert_equal(true, i_local->should_load_thumbnail(), u8"should load after db load"sv);

	i_local->thumbnail(loaded.i, nullptr);
	assert_equal(true, i_local->should_load_thumbnail(), u8"should load without timestamp"sv);

	i_local->thumbnail(loaded.i, nullptr, date);
	assert_equal(false, i_local->should_load_thumbnail(), u8"should load if thumb but not hash"sv);

	i_local->update(load_path, make_index_file_info(date2));
	assert_equal(true, i_local->should_load_thumbnail(), u8"should if date changes"sv);
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

	auto path_test = df::file_path(test_files_folder, u8"Test.jpg"sv);
	auto path_sony = df::file_path(test_files_folder, u8"Sony.jpg"sv);

	const auto test_item = load_item(index, path_test, false);
	const auto sony_item = load_item(index, path_sony, false);

	assert_equal(false, test_item->should_load_thumbnail(), u8"should_load_thumbnail for test.jpg"sv);
	assert_equal(false, sony_item->should_load_thumbnail(), u8"should_load_thumbnail for sony.jpg"sv);

	df::item_set items;
	items._items = {test_item, sony_item};

	index.scan_items(items, false, false, false, false, test_token);
	db.perform_writes();

	assert_equal(false, test_item->should_load_thumbnail(), u8"should_load_thumbnail for test.jpg"sv);
	assert_equal(false, sony_item->should_load_thumbnail(), u8"should_load_thumbnail for sony.jpg"sv);

	db.load_thumbnails(index, items);

	assert_equal(true, test_item->should_load_thumbnail(),
	             u8"should_load_thumbnail for test.jpg after db load_thumbnails"sv);
	assert_equal(true, sony_item->should_load_thumbnail(),
	             u8"should_load_thumbnail for sony.jpg after db load_thumbnails"sv);

	index.scan_items(items, true, false, false, false, test_token);
	db.perform_writes();

	assert_equal(false, test_item->should_load_thumbnail(),
	             u8"should_load_thumbnail for test.jpg after index load_thumbnails"sv);
	assert_equal(false, sony_item->should_load_thumbnail(),
	             u8"should_load_thumbnail for sony.jpg after index load_thumbnails"sv);
}

static void should_rename()
{
	null_async_strategy as;
	location_cache locations;
	index_state index(as, locations);

	const df::file_path src_path(test_files_folder, u8"Test.jpg"sv);
	const auto save_path_1 = _temps.next_path(u8".jpg"sv);
	const auto save_path_2 = _temps.next_path(u8".jpg"sv);

	platform::copy_file(src_path, save_path_1, false, false);

	auto test_item = load_item(index, save_path_1, false);

	assert_equal(true, test_item->rename(index, save_path_2.file_name_without_extension()).success(), u8"can rename"sv);
	assert_equal(save_path_2.name(), test_item->name(), u8"renamed"sv);
	assert_equal(true, save_path_2.exists(), u8"renamed exists"sv);
}

static void should_not_overwrite_during_rename()
{
	null_async_strategy as;
	location_cache locations;
	index_state index(as, locations);

	const df::file_path src_path(test_files_folder, u8"Test.jpg"sv);
	const auto save_path_1 = _temps.next_path(u8".jpg"sv);
	const auto save_path_2 = _temps.next_path(u8".jpg"sv);

	platform::copy_file(src_path, save_path_1, false, false);
	platform::copy_file(src_path, save_path_2, false, false);

	auto test_item = load_item(index, save_path_1, false);

	assert_equal(false, test_item->rename(index, save_path_2.file_name_without_extension()).success(),
	             u8"should not rename"sv);
	assert_equal(save_path_1.name(), test_item->name(), u8"not renamed"sv);
	assert_equal(true, test_item->path().exists(), u8"exists"sv);
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

	const auto path1 = df::file_path(test_files_folder, u8"Test.jpg"sv);
	const auto path2 = df::file_path(test_files_folder, u8"Test90.jpg"sv);
	const auto path3 = df::file_path(test_files_folder, u8"Test180.jpg"sv);
	const auto path4 = df::file_path(test_files_folder, u8"Test270.jpg"sv);
	const auto path5 = df::file_path(test_files_folder, u8"Small.jpg"sv);
	const auto path_sony = df::file_path(test_files_folder, u8"Sony.jpg"sv);

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

	assert_equal(1u, index.find_item(test_item1->path()).duplicates.count, u8"duplicates"sv);
	assert_equal(1u, index.find_item(test_item2->path()).duplicates.count, u8"duplicates"sv);
	assert_equal(1u, index.find_item(test_item3->path()).duplicates.count, u8"duplicates"sv);
	assert_equal(1u, index.find_item(sony_item->path()).duplicates.count, u8"duplicates"sv);
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

	const auto path_test = df::file_path(test_files_folder, u8"exif-rotated.jpg"sv);
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
		test_files_folder.combine_file(u8"Test.jpg"sv),
		test_files_folder.combine_file(u8"Test90.jpg"sv),
		test_files_folder.combine_file(u8"Small.jpg"sv),
		test_files_folder.combine_file(u8"Lossless0.jpg"sv)
	};

	for (auto path : paths)
	{
		{
			crash_files_db test_crash_files(db_path);

			assert_equal(test_crash_files.is_known_crash_file(path), false, u8"is_known_crash_file"sv);

			test_crash_files.add_open(path, str::utf8_cast(__FUNCTION__));
			test_crash_files.remove_open(path);
			test_crash_files.flush_open_files();
		}

		{
			crash_files_db test_crash_files(db_path);

			assert_equal(test_crash_files.is_known_crash_file(path), false, u8"is_known_crash_file"sv);

			test_crash_files.add_open(path, str::utf8_cast(__FUNCTION__));
			test_crash_files.flush_open_files();
		}

		{
			crash_files_db test_crash_files(db_path);

			assert_equal(test_crash_files.is_known_crash_file(path), true, u8"is_known_crash_file"sv);
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

static void should_not_crash(const std::u8string_view name)
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
	tests.add(u8"Should index"s, should_index);
	tests.add(u8"Should store thumbnails"s, should_store_thumbnails);
	tests.add(u8"Should store cover art"s, should_store_cover_art);
	tests.add(u8"Should store item properties"s, should_store_item_properties);
	tests.add(u8"Should store pack properties"s, should_pack_item_properties);
	tests.add(u8"Should store webservice results"s, should_store_webservice_results);
	tests.add(u8"Should detect duplicates"s, should_detect_duplicates);
	tests.add(u8"Should Rename"s, should_rename);
	tests.add(u8"Should not overwrite during rename"s, should_not_overwrite_during_rename);
	tests.add(u8"Should detect original path"s, should_detect_original_path);
	tests.add(u8"Should not reload thumb when valid"s, should_not_reload_thumb_when_valid);
	tests.add(u8"Should reload thumb after scan"s, should_reload_thumb_after_scan);
	tests.add(u8"Should detect rotation"s, should_detect_rotation);
	tests.add(u8"Should parse roots"s, should_parse_roots);
	tests.add(u8"Should toggle collection entry"s, should_toggle_collection_entry);
	tests.add(u8"Should record crashes"s, should_record_crashes);

	//
	// UI
	//
	tests.add(u8"Should select correctly"s, should_select_items);
	tests.add(u8"Should Enable based on selection"s, should_enable_based_on_selection);
	tests.add(u8"Should toggle rating"s, should_toggle_rating);

#ifndef _DEBUG
	tests.add(u8"Should not crash on JPEG"s, [] { should_not_crash(u8"small.jpg"sv); });
	tests.add(u8"Should not crash on GIF"s, [] { should_not_crash(u8"tuesday.gif"sv); });
	tests.add(u8"Should not crash on TIFF"s, [] { should_not_crash(u8"small.tif"sv); });
	tests.add(u8"Should not crash on PNG"s, [] { should_not_crash(u8"cube.png"sv); });
	tests.add(u8"Should not crash on WEBP"s, [] { should_not_crash(u8"lake.webp"sv); });
#endif
}
