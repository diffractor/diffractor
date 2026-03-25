// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Metadata scanning tests. Verifies correct parsing of metadata from
// JPEG, MP3, MP4, MOV, AVI, RAW, HEIC, AVIF, WebP, D64, archive, and MOD files.

#include "pch.h"
#include "test_utils.h"

static void should_replace_tokens()
{
	files ff;
	const auto image_md = ff_scan_file(ff, test_files_folder.combine_file(u8"Test.jpg"sv));
	const auto audio_md = ff_scan_file(ff, test_files_folder.combine_file(u8"Colorblind.mp3"sv));

	const auto md1 = image_md.to_props();
	const auto md2 = audio_md.to_props();

	assert_equal(u8"2012\\2012-09-14"sv, replace_tokens(u8"{year}\\{created}"s, md1, {}, md1->created()));
	assert_equal(u8"2012\\Test.jpg"sv, replace_tokens(u8"{year}\\{name}"s, md1, u8"Test.jpg"sv, md1->created()));
	assert_equal(u8"2012\\09\\14"sv, replace_tokens(u8"{year}\\{month}\\{day}"s, md1, {}, md1->created()));
	assert_equal(u8"2012\\september\\14"sv, replace_tokens(u8"{year}\\{month.text}\\{day}"s, md1, {}, md1->created()));
	assert_equal(u8"2012\\SEP\\14"sv, replace_tokens(u8"{year}\\{month.short}\\{day}"s, md1, {}, md1->created()));
	assert_equal(u8"Counting Crows\\This Desert Life"sv,
	             replace_tokens(u8"{artist}\\{album}"s, md2, {}, md2->created()));
}

static void should_scan_jpeg()
{
	const auto load_path = test_files_folder.combine_file(u8"Test.jpg"sv);

	prop::item_metadata expected_exif;
	expected_exif.created_exif = df::date_t(2012, 9, 14);
	expected_exif.camera_manufacturer = u8"Canon"_c;
	expected_exif.camera_model = u8"Canon EOS 7D"_c;
	expected_exif.lens = u8"EF-S15-85mm f/3.5-5.6 IS USM"_c;
	expected_exif.description = u8"Caption"_c;
	expected_exif.coordinate = gps_coordinate(50.08806, 14.42083);
	expected_exif.copyright_notice = u8"Copyright"_c;
	expected_exif.f_number = 6.3f;
	expected_exif.exposure_time = 1.0f / 100.0f;
	expected_exif.iso_speed = 100;
	expected_exif.focal_length = 15.0f;
	expected_exif.created_digitized = df::date_t(2012, 9, 14, 19, 21, 14);
	expected_exif.created_exif = df::date_t(2012, 9, 14, 19, 21, 14);
	expected_exif.rating = 0;

	assert_metadata(expected_exif, *extract_properties(load_path, metadata_type::EXIF), u8"EXIF"sv);

	prop::item_metadata expected_iptc;
	expected_iptc.title = u8"Title"_c;
	expected_iptc.description = u8"Caption"_c;
	expected_iptc.tags = u8"key1 key2 key3"_c;
	expected_iptc.location_place = u8"Prague"_c;
	expected_iptc.location_state = u8"Hlavní Mesto Praha"_c;
	expected_iptc.location_country = u8"Czech Republic"_c;
	expected_iptc.copyright_notice = u8"Copyright"_c;
	expected_iptc.rating = 0;

	assert_metadata(expected_iptc, *extract_properties(load_path, metadata_type::IPTC), u8"IPTC"sv);

	const auto actual_xmp = extract_properties(load_path, metadata_type::XMP);

	prop::item_metadata expected_xmp;
	expected_xmp.created_exif = df::date_t(2012, 9, 14);
	expected_xmp.title = u8"Title"_c;
	expected_xmp.description = u8"Caption"_c;
	expected_xmp.tags = u8"key1 key2 key3"_c;
	expected_xmp.location_place = u8"Prague"_c;
	expected_xmp.location_state = u8"Hlavní Mesto Praha"_c;
	expected_xmp.location_country = u8"Czech Republic"_c;
	expected_xmp.lens = u8"EF-S15-85mm f/3.5-5.6 IS USM"_c;
	expected_xmp.copyright_notice = u8"Copyright"_c;
	expected_xmp.rating = 4;
	expected_xmp.created_digitized = df::date_t(2012, 9, 14, 19, 21, 14);
	expected_xmp.created_exif = df::date_t(2012, 9, 14, 19, 21, 14);

	assert_metadata(expected_xmp, *actual_xmp, u8"XMP"sv);

	const auto actual_all = extract_properties(load_path, metadata_type::ALL);
	assert_metadata(*expected_test_jpg(), *actual_all, u8"all"sv);
}

static void should_parse_xmp()
{
	const auto load_path = test_files_folder.combine_file(u8"IMG_0604.xmp"sv);

	prop::item_metadata actual;
	metadata_xmp::parse(actual, load_path);

	assert_equal(u8"Flower"sv, actual.title);
	assert_equal(u8"Blomst"sv, actual.tags);
	assert_equal(u8"Følfod ( Tussilago farfara ) Lægeurt"sv, actual.description);
	assert_equal(u8"Frank Aalestrup www.fdaa.dk"sv, actual.copyright_notice);
	assert_equal(u8"\"Frank Aalestrup.\nwww.fdaa.dk\""sv, actual.copyright_creator);
	assert_equal(u8"EF100mm f/2.8L Macro IS USM"sv, actual.lens);
	assert_equal({56.19283, 9.88415}, actual.coordinate);
	assert_equal(u8"Canon"sv, actual.camera_manufacturer);
	assert_equal(u8"Canon EOS 50D"sv, actual.camera_model);
	assert_equal(u8"IMG_0604.CR2"sv, actual.raw_file_name);
	assert_equal(u8"Denmark"sv, actual.location_country);
}

static void should_merge_xmp_sidecar()
{
	const auto cr2_path = test_files_folder.combine_file(u8"Gherkin.CR2"sv);
	const auto xmp_path = test_files_folder.combine_file(u8"Gherkin.XMP"sv);

	const auto actual = extract_properties(cr2_path);

	metadata_xmp::parse(*actual, xmp_path);

	assert_equal(u8"Canon"sv, actual->camera_manufacturer);
	assert_equal(u8"United Kingdom"sv, actual->location_country);
	assert_equal(u8"\xA9 Mark Ridgwell"sv, actual->copyright_notice);
	assert_equal(u8"\"Mark Ridgwell\""sv, actual->copyright_creator);
}

static void should_scan_mp3()
{
	const auto load_path = test_files_folder.combine_file(u8"Colorblind.mp3"sv);

	files ff;
	const auto actual = ff_scan_file(ff, load_path);

	prop::item_metadata expected;
	expected.artist = u8"Counting Crows"_c;
	expected.album_artist = u8"Counting Crows"_c;
	expected.title = u8"Colorblind"_c;
	expected.album = u8"This Desert Life"_c;
	expected.comment = u8"Comments"_c;
	expected.composer = u8"Adam Duritz/Charlie Gillingham"_c;
	expected.publisher = u8"Interscope"_c;
	expected.rating = 5;
	expected.genre = u8"Rock"_c;
	expected.duration = 10;
	expected.audio_sample_rate = 22050;
	expected.audio_sample_type = 35;
	expected.audio_channels = 2;
	expected.audio_codec = u8"mp3float"_c;
	expected.track.x = 7;
	expected.year = 1999;

	assert_metadata(expected, *actual.to_props(), u8"Colorblind.mp3"sv);

	const auto load_path2 = test_files_folder.combine_file(u8"Games Without Frontiers.mp3"sv);
	const auto actual2 = ff_scan_file(ff, load_path2);

	prop::item_metadata expected2;
	expected2.artist = u8"Peter Gabriel"_c;
	expected2.album = u8"Hit"_c;
	expected2.title = u8"Games Without Frontiers"_c;
	expected2.year = 2003;
	expected2.genre = u8"Rock"_c;
	expected2.duration = 10;
	expected2.audio_sample_rate = 44100;
	expected2.audio_sample_type = 35;
	expected2.audio_channels = 2;
	expected2.track.x = 5;
	expected2.disk.x = 1;
	expected2.encoder = u8"Lavf51.12.1"_c;
	expected2.audio_codec = u8"mp3float"_c;

	assert_metadata(expected2, *actual2.to_props(), u8"Games Without Frontiers.mp3"sv);

	const auto load_path3 = test_files_folder.combine_file(u8"Is It Any Wonder.mp3"sv);
	const auto actual3 = ff_scan_file(ff, load_path3);

	prop::item_metadata expected3;
	expected3.artist = u8"Keane"_c;
	expected3.album = u8"Under The Iron Sea"_c;
	expected3.title = u8"Is It Any Wonder?"_c;
	expected3.year = 2006;
	expected3.genre = u8"Rock"_c;
	expected3.duration = 10;
	expected3.audio_sample_rate = 44100;
	expected3.audio_sample_type = 35;
	expected3.audio_channels = 2;
	expected3.track.x = 2;
	expected3.audio_codec = u8"mp3float"_c;
	expected3.created_utc = df::date_t(2006, 6, 20, 0, 0, 0);
	expected3.created_digitized = df::date_t(2006, 6, 20, 0, 0, 0);
	expected3.publisher = u8"Interscope"_c;

	assert_metadata(expected3, *actual3.to_props(), u8"Is It Any Wonder.mp3"sv);
}

static void should_scan_mp4()
{
	const auto load_path = test_files_folder.combine_file(u8"gizmo.mp4"sv);

	files ff;
	const auto actual = ff_scan_file(ff, load_path);

	prop::item_metadata expected;
	expected.title = u8"Title xxx"_c;
	expected.comment = u8"Comments xxx"_c;
	expected.tags = u8"gadget test"_c;
	expected.audio_codec = u8"aac"_c;
	expected.audio_sample_rate = 48000;
	expected.audio_sample_type = 35;
	expected.audio_channels = 1;
	expected.duration = 6;
	expected.width = 560;
	expected.height = 320;
	expected.created_utc = df::date_t(2010, 3, 20, 21, 29, 11);
	expected.created_digitized = df::date_t(2010, 3, 20, 21, 29, 11);
	expected.year = 2010;
	expected.video_codec = u8"h264"_c;
	expected.encoder = u8"HandBrake 0.9.4 2009112300"_c;
	expected.pixel_format = u8"yuv420p"_c;

	assert_metadata(expected, *actual.to_props(), u8"gizmo.mp4"sv);

	const auto load_path2 = test_files_folder.combine_file(u8"This Year's Love.m4a"sv);
	const auto actual2 = ff_scan_file(ff, load_path2);

	prop::item_metadata expected2;
	expected2.title = u8"This Year's Love"_c;
	expected2.artist = u8"David Gray"_c;
	expected2.album_artist = u8"David Gray"_c;
	expected2.composer = u8"David Gray"_c;
	expected2.album = u8"David Gray: Greatest Hits"_c;
	expected2.copyright_notice = u8"\u2117 2007 Iht Records Ltd under exclusive licence to Warner Music UK Ltd"_c;
	expected2.created_utc = df::date_t(2007, 11, 9, 8, 0, 0);
	expected2.created_digitized = df::date_t(2007, 11, 9, 8, 0, 0);
	expected2.genre = u8"Pop"_c;
	expected2.duration = 10;
	expected2.audio_codec = u8"aac"_c;
	expected2.audio_sample_rate = 44100;
	expected2.audio_sample_type = 35;
	expected2.audio_channels = 2;
	expected2.track = {7, 0};
	expected2.width = 0;
	expected2.height = 0;
	expected2.disk = {0, 0};
	expected2.year = 2007;
	expected2.encoder = u8"Lavf54.63.100"_c;

	assert_metadata(expected2, *actual2.to_props(), u8"This Year's Love.m4a"sv);
	assert_equal(true, is_empty(actual2.thumbnail_image), u8"m4a scan thumbnail"sv);

	const auto loaded = ff_scan_and_load_thumb(ff, load_path2);
	assert_equal(false, loaded.success && is_valid(loaded.thumbnail_surface), u8"m4a load thumbnail"sv);
}

static void should_scan_mov()
{
	const auto load_path = test_files_folder.combine_file(u8"ipod.mov"sv);

	files ff;
	const auto actual = ff_scan_file(ff, load_path);

	prop::item_metadata expected;
	expected.title = u8"iPad Help"_c;
	expected.tags = u8"apple ipad ipod support"_c;
	expected.comment = u8"What to do if you ipad dies"_c;
	expected.audio_codec = u8"aac"_c;
	expected.audio_sample_rate = 32000;
	expected.audio_sample_type = 35;
	expected.audio_channels = 1;
	expected.pixel_format = u8"yuv420p"_c;
	expected.duration = 86;
	expected.width = 640;
	expected.height = 480;
	expected.video_codec = u8"mpeg4"_c;
	expected.created_digitized = df::date_t(2005, 10, 17, 22, 54, 32);
	expected.created_utc = df::date_t(2005, 10, 17, 22, 54, 32);
	expected.year = 2005;

	assert_metadata(expected, *actual.to_props(), u8"ipod.mov"sv);

	const auto load_path2 = test_files_folder.combine_file(u8"StPauls.MOV"sv);
	const auto actual2 = ff_scan_file(ff, load_path2);

	prop::item_metadata expected2;
	expected2.created_utc = df::date_t(2011, 3, 13, 15, 13, 49);
	expected2.created_digitized = df::date_t(2011, 3, 13, 15, 13, 49);
	expected2.audio_codec = u8"aac"_c;
	expected2.coordinate = {51.51420, -0.09850};
	expected2.width = 640;
	expected2.height = 480;
	expected2.duration = 10;
	expected2.audio_sample_rate = 44100;
	expected2.audio_sample_type = 35;
	expected2.audio_channels = 1;
	expected2.video_codec = u8"h264"_c;
	expected2.camera_manufacturer = u8"Apple"_c;
	expected2.camera_model = u8"iPhone 3GS"_c;
	expected2.encoder = u8"4.3"_c;
	expected2.pixel_format = u8"yuv420p"_c;
	expected2.year = 2011;

	assert_metadata(expected2, *actual2.to_props(), u8"StPauls.MOV"sv);
}

static void should_scan_avi()
{
	const auto load_path = test_files_folder.combine_file(u8"Byzantium.avi"sv);

	files ff;
	const auto actual = ff_scan_file(ff, load_path);

	prop::item_metadata expected;
	expected.audio_codec = u8"wmav2"_c;
	expected.title = u8"Byzantium"_c;
	expected.comment =
		u8"John Romer recreates the glory and history of Byzantium. From the Hagia Sophia in present-day Istanbul to the looted treasures of the empire now located in St. Marks in Venice."_c;
	expected.audio_sample_rate = 48000;
	expected.audio_sample_type = 35;
	expected.audio_channels = 2;
	expected.duration = 12;
	expected.width = 854;
	expected.height = 480;
	expected.video_codec = u8"wmv3"_c;
	expected.tags = u8"Byzantium History Turkey"_c;
	expected.pixel_format = u8"yuv420p"_c;

	assert_metadata(expected, *actual.to_props(), u8"Byzantium.avi"sv);
}

static void should_scan_raw()
{
	const auto load_path = test_files_folder.combine(u8"raw"sv).combine_file(u8"Screws.CR2"sv);

	prop::item_metadata expected;
	expected.title = u8"Screws on Desk"_c;
	expected.file_name = u8"Screws.CR2"_c;
	expected.copyright_notice = u8"Copyright"_c;
	expected.tags = u8"Desk Macro Screws"_c;
	expected.title = u8"Screws on Desk"_c;
	expected.description = u8"This is a Description"_c;
	expected.comment = u8"This is a Comment"_c;
	expected.rating = 4;
	expected.width = 3522;
	expected.height = 2348;
	expected.f_number = 2.8f;
	expected.camera_manufacturer = u8"Canon"_c;
	expected.camera_model = u8"EOS-1D Mark II"_c;
	expected.exposure_time = 1 / 25.0f;
	expected.focal_length = 100;
	expected.iso_speed = 100;
	expected.pixel_format = u8"RGBG"_c;
	expected.created_utc = df::date_t(2011, 9, 23, 21, 49, 16);

	null_async_strategy as;
	location_cache locations;
	index_state index(as, locations);
	auto actual = metadata_from_cache(index, load_path);
	assert_metadata(expected, *actual, u8"Screws.CR2"sv);
}

static void should_parse_facebook_json()
{
	const auto path_status = test_files_folder.combine_file(u8"place.json"sv);
	const auto json = df::util::json::json_from_file(path_status);

	auto& result = json[u8"result"];
	auto& address_components = result[u8"address_components"];
	assert_equal(5u, address_components.Size(), u8"data"sv);
	assert_equal(u8"WC1X"sv, address_components[0][u8"long_name"].GetString(), u8"long_name"sv);
}

static void should_scan_d64()
{
	constexpr auto file_name = u8"Ace of Aces (Europe).D64"sv;
	const auto load_path = test_files_folder.combine(u8"retro"sv).combine_file(file_name);
	const auto loaded = df::blob_from_file(load_path);
	const auto contents = files::list_disk(loaded);

	assert_equal(4_z, contents.size(), u8"d64"sv, file_name);
	assert_equal(u8"147 \"      \" PRG"sv, contents[0].line, u8"d64"sv, file_name);
}

static void should_scan_archive()
{
	constexpr auto file_name = u8"benchmarks.zip"sv;
	const auto load_path = test_files_folder.combine_file(file_name);
	const auto contents = files::list_archive(load_path);

	assert_equal(2_z, contents.size(), u8"d64"sv, file_name);
	assert_equal(u8"PXL_20240404_074316577.jpg"sv, contents[0].filename, u8"d64"sv, file_name);
}

static void should_scan_mod()
{
	constexpr auto file_name = u8"giana.mod"sv;
	const auto load_path = test_files_folder.combine(u8"retro"sv).combine_file(file_name);

	files ff;
	const auto actual = ff_scan_file(ff, load_path);
	const auto props = actual.to_props();

	assert_equal(u8"giana!"_c, props->title, u8"title"sv, file_name);
	assert_equal(u8"Generic ProTracker or compatible"_c, props->encoder, u8"encoder"sv, file_name);
	assert_equal(48000, props->audio_sample_rate, u8"encoder"sv, file_name);
}

static void should_scan_heic()
{
	constexpr auto file_name = u8"melnik.heic"sv;
	const auto load_path = test_files_folder.combine_file(file_name);

	files ff;
	const auto actual = ff_scan_file(ff, load_path);
	const auto props = actual.to_props();

	assert_equal(4000, props->width, u8"width"sv, file_name);
	assert_equal(2252, props->height, u8"height"sv, file_name);
	assert_equal(u8"yuv420"sv, props->pixel_format, u8"pixel_format"sv, file_name);
}

static void should_scan_avif()
{
	constexpr auto file_name = u8"hato.profile0.10bpc.yuv420.avif"sv;
	const auto load_path = test_files_folder.combine_file(file_name);

	files ff;
	const auto actual = ff_scan_file(ff, load_path);
	const auto props = actual.to_props();

	assert_equal(3078, props->width, u8"width"sv, file_name);
	assert_equal(2048, props->height, u8"height"sv, file_name);
	assert_equal(u8"yuv444"sv, props->pixel_format, u8"pixel_format"sv, file_name);
}

static void should_scan_webp()
{
	constexpr auto file_name = u8"lake.webp"sv;
	const auto load_path = test_files_folder.combine_file(file_name);

	files ff;
	const auto actual = ff_scan_file(ff, load_path);
	const auto props = actual.to_props();

	assert_equal(550, props->width, u8"width"sv, file_name);
	assert_equal(368, props->height, u8"height"sv, file_name);
	assert_equal(u8"yuv420"sv, props->pixel_format, u8"pixel_format"sv, file_name);
}

static void should_load_po()
{
	const auto app_folder = known_path(platform::known_folder::running_app_folder);
	const auto lang_folder = app_folder.combine(u8"languages"sv);
	const auto lang_path = lang_folder.combine_file(u8"de.po"sv);

	const auto po_entries = load_po(lang_path);

	app_text_t t;
	t.load_lang(lang_path.name(), po_entries);

	assert_equal(u8"Datenbank bereinigen und neu indexieren.\nAlle Daten werden regeneriert."sv, t.reset_database,
	             u8"reset_database"sv);
}

void register_tests3(view_state& state, test_registry& tests)
{
	//
	// Scan Metadata
	//
	tests.add(u8"Should scan jpg metadata"s, should_scan_jpeg);
	tests.add(u8"Should scan avi metadata"s, should_scan_avi);
	tests.add(u8"Should scan mov metadata"s, should_scan_mov);
	tests.add(u8"Should scan mp3 metadata"s, should_scan_mp3);
	tests.add(u8"Should scan mp4 metadata"s, should_scan_mp4);
	tests.add(u8"Should scan raw metadata"s, should_scan_raw);
	tests.add(u8"Should scan mod metadata"s, should_scan_mod);
	tests.add(u8"Should scan webp metadata"s, should_scan_webp);
	tests.add(u8"Should scan heif metadata"s, should_scan_heic);
	tests.add(u8"Should scan avif metadata"s, should_scan_avif);
	tests.add(u8"Should parse Xmp"s, should_parse_xmp);
	tests.add(u8"Should merge Xmp sidecar"s, should_merge_xmp_sidecar);
	tests.add(u8"Should replace tokens"s, should_replace_tokens);

	//
	// Commodore
	//
	tests.add(u8"Should scan d64"s, should_scan_d64);

	//
	// Archive
	//
	tests.add(u8"Should scan archive"s, should_scan_archive);

	//
	// Other
	//
	tests.add(u8"Should parse facebook Json"s, should_parse_facebook_json);
	tests.add(u8"Should load po"s, should_load_po);
}
