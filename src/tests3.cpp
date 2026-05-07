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

#include "metadata_xmp.h"
#include "test_utils.h"

static void should_replace_tokens()
{
	files ff;
	const auto image_md = ff_scan_file(ff, test_files_folder.combine_file("Test.jpg"));
	const auto audio_md = ff_scan_file(ff, test_files_folder.combine_file("Colorblind.mp3"));

	const auto md1 = image_md.to_props();
	const auto md2 = audio_md.to_props();

	assert_equal("2012\\2012-09-14", replace_tokens("{year}\\{created}"s, md1, {}, md1->created()));
	assert_equal("2012\\Test.jpg", replace_tokens("{year}\\{name}"s, md1, "Test.jpg", md1->created()));
	assert_equal("2012\\09\\14", replace_tokens("{year}\\{month}\\{day}"s, md1, {}, md1->created()));
	assert_equal("2012\\september\\14", replace_tokens("{year}\\{month.text}\\{day}"s, md1, {}, md1->created()));
	assert_equal("2012\\SEP\\14", replace_tokens("{year}\\{month.short}\\{day}"s, md1, {}, md1->created()));
	assert_equal("Counting Crows\\This Desert Life",
	             replace_tokens("{artist}\\{album}"s, md2, {}, md2->created()));
}

static void should_scan_jpeg()
{
	const auto load_path = test_files_folder.combine_file("Test.jpg");

	prop::item_metadata expected_exif;
	expected_exif.created_exif = df::date_t(2012, 9, 14);
	expected_exif.camera_manufacturer = "Canon"_c;
	expected_exif.camera_model = "Canon EOS 7D"_c;
	expected_exif.lens = "EF-S15-85mm f/3.5-5.6 IS USM"_c;
	expected_exif.description = "Caption"_c;
	expected_exif.coordinate = gps_coordinate(50.08806, 14.42083);
	expected_exif.copyright_notice = "Copyright"_c;
	expected_exif.f_number = 6.3f;
	expected_exif.exposure_time = 1.0f / 100.0f;
	expected_exif.iso_speed = 100;
	expected_exif.focal_length = 15.0f;
	expected_exif.created_digitized = df::date_t(2012, 9, 14, 19, 21, 14);
	expected_exif.created_exif = df::date_t(2012, 9, 14, 19, 21, 14);
	expected_exif.rating = 0;

	assert_metadata(expected_exif, *extract_properties(load_path, metadata_type::EXIF), "EXIF");

	prop::item_metadata expected_iptc;
	expected_iptc.title = "Title"_c;
	expected_iptc.description = "Caption"_c;
	expected_iptc.tags = "key1 key2 key3"_c;
	expected_iptc.location_place = "Prague"_c;
	expected_iptc.location_state = "Hlavní Mesto Praha"_c;
	expected_iptc.location_country = "Czech Republic"_c;
	expected_iptc.copyright_notice = "Copyright"_c;
	expected_iptc.rating = 0;

	assert_metadata(expected_iptc, *extract_properties(load_path, metadata_type::IPTC), "IPTC");

	const auto actual_xmp = extract_properties(load_path, metadata_type::XMP);

	prop::item_metadata expected_xmp;
	expected_xmp.created_exif = df::date_t(2012, 9, 14);
	expected_xmp.title = "Title"_c;
	expected_xmp.description = "Caption"_c;
	expected_xmp.tags = "key1 key2 key3"_c;
	expected_xmp.location_place = "Prague"_c;
	expected_xmp.location_state = "Hlavní Mesto Praha"_c;
	expected_xmp.location_country = "Czech Republic"_c;
	expected_xmp.lens = "EF-S15-85mm f/3.5-5.6 IS USM"_c;
	expected_xmp.copyright_notice = "Copyright"_c;
	expected_xmp.rating = 4;
	expected_xmp.created_digitized = df::date_t(2012, 9, 14, 19, 21, 14);
	expected_xmp.created_exif = df::date_t(2012, 9, 14, 19, 21, 14);

	assert_metadata(expected_xmp, *actual_xmp, "XMP");

	const auto actual_all = extract_properties(load_path, metadata_type::ALL);
	assert_metadata(*expected_test_jpg(), *actual_all, "all");
}

static void should_parse_xmp()
{
	const auto load_path = test_files_folder.combine_file("IMG_0604.xmp");

	prop::item_metadata actual;
	metadata_xmp::parse(actual, load_path);

	assert_equal("Flower", actual.title);
	assert_equal("Blomst", actual.tags);
	assert_equal("Følfod ( Tussilago farfara ) Lægeurt", actual.description);
	assert_equal("Frank Aalestrup www.fdaa.dk", actual.copyright_notice);
	assert_equal("\"Frank Aalestrup.\nwww.fdaa.dk\"", actual.copyright_creator);
	assert_equal("EF100mm f/2.8L Macro IS USM", actual.lens);
	assert_equal({56.19283, 9.88415}, actual.coordinate);
	assert_equal("Canon", actual.camera_manufacturer);
	assert_equal("Canon EOS 50D", actual.camera_model);
	assert_equal("IMG_0604.CR2", actual.raw_file_name);
	assert_equal("Denmark", actual.location_country);
}

static void should_merge_xmp_sidecar()
{
	const auto cr2_path = test_files_folder.combine_file("Gherkin.CR2");
	const auto xmp_path = test_files_folder.combine_file("Gherkin.XMP");

	const auto actual = extract_properties(cr2_path);

	metadata_xmp::parse(*actual, xmp_path);

	assert_equal("Canon", actual->camera_manufacturer);
	assert_equal("United Kingdom", actual->location_country);
	assert_equal("© Mark Ridgwell", actual->copyright_notice);
	assert_equal("\"Mark Ridgwell\"", actual->copyright_creator);
}

static void should_scan_mp3()
{
	const auto load_path = test_files_folder.combine_file("Colorblind.mp3");

	files ff;
	const auto actual = ff_scan_file(ff, load_path);

	prop::item_metadata expected;
	expected.artist = "Counting Crows"_c;
	expected.album_artist = "Counting Crows"_c;
	expected.title = "Colorblind"_c;
	expected.album = "This Desert Life"_c;
	expected.comment = "Comments"_c;
	expected.composer = "Adam Duritz/Charlie Gillingham"_c;
	expected.publisher = "Interscope"_c;
	expected.rating = 5;
	expected.genre = "Rock"_c;
	expected.duration = 10;
	expected.audio_sample_rate = 22050;
	expected.audio_sample_type = 35;
	expected.audio_channels = 2;
	expected.audio_codec = "mp3float"_c;
	expected.track.x = 7;
	expected.year = 1999;

	assert_metadata(expected, *actual.to_props(), "Colorblind.mp3");

	const auto load_path2 = test_files_folder.combine_file("Games Without Frontiers.mp3");
	const auto actual2 = ff_scan_file(ff, load_path2);

	prop::item_metadata expected2;
	expected2.artist = "Peter Gabriel"_c;
	expected2.album = "Hit"_c;
	expected2.title = "Games Without Frontiers"_c;
	expected2.year = 2003;
	expected2.genre = "Rock"_c;
	expected2.duration = 10;
	expected2.audio_sample_rate = 44100;
	expected2.audio_sample_type = 35;
	expected2.audio_channels = 2;
	expected2.track.x = 5;
	expected2.disk.x = 1;
	expected2.encoder = "Lavf51.12.1"_c;
	expected2.audio_codec = "mp3float"_c;

	assert_metadata(expected2, *actual2.to_props(), "Games Without Frontiers.mp3");

	const auto load_path3 = test_files_folder.combine_file("Is It Any Wonder.mp3");
	const auto actual3 = ff_scan_file(ff, load_path3);

	prop::item_metadata expected3;
	expected3.artist = "Keane"_c;
	expected3.album = "Under The Iron Sea"_c;
	expected3.title = "Is It Any Wonder?"_c;
	expected3.year = 2006;
	expected3.genre = "Rock"_c;
	expected3.duration = 10;
	expected3.audio_sample_rate = 44100;
	expected3.audio_sample_type = 35;
	expected3.audio_channels = 2;
	expected3.track.x = 2;
	expected3.audio_codec = "mp3float"_c;
	expected3.created_utc = df::date_t(2006, 6, 20, 0, 0, 0);
	expected3.created_digitized = df::date_t(2006, 6, 20, 0, 0, 0);
	expected3.publisher = "Interscope"_c;

	assert_metadata(expected3, *actual3.to_props(), "Is It Any Wonder.mp3");
}

static void should_scan_mp4()
{
	const auto load_path = test_files_folder.combine_file("gizmo.mp4");

	files ff;
	const auto actual = ff_scan_file(ff, load_path);

	prop::item_metadata expected;
	expected.title = "Title xxx"_c;
	expected.comment = "Comments xxx"_c;
	expected.tags = "gadget test"_c;
	expected.audio_codec = "aac"_c;
	expected.audio_sample_rate = 48000;
	expected.audio_sample_type = 35;
	expected.audio_channels = 1;
	expected.duration = 6;
	expected.width = 560;
	expected.height = 320;
	expected.created_utc = df::date_t(2010, 3, 20, 21, 29, 11);
	expected.created_digitized = df::date_t(2010, 3, 20, 21, 29, 11);
	expected.year = 2010;
	expected.video_codec = "h264"_c;
	expected.encoder = "HandBrake 0.9.4 2009112300"_c;
	expected.pixel_format = "yuv420p"_c;

	assert_metadata(expected, *actual.to_props(), "gizmo.mp4");

	const auto load_path2 = test_files_folder.combine_file("This Year's Love.m4a");
	const auto actual2 = ff_scan_file(ff, load_path2);

	prop::item_metadata expected2;
	expected2.title = "This Year's Love"_c;
	expected2.artist = "David Gray"_c;
	expected2.album_artist = "David Gray"_c;
	expected2.composer = "David Gray"_c;
	expected2.album = "David Gray: Greatest Hits"_c;
	expected2.copyright_notice = "\u2117 2007 Iht Records Ltd under exclusive licence to Warner Music UK Ltd"_c;
	expected2.created_utc = df::date_t(2007, 11, 9, 8, 0, 0);
	expected2.created_digitized = df::date_t(2007, 11, 9, 8, 0, 0);
	expected2.genre = "Pop"_c;
	expected2.duration = 10;
	expected2.audio_codec = "aac"_c;
	expected2.audio_sample_rate = 44100;
	expected2.audio_sample_type = 35;
	expected2.audio_channels = 2;
	expected2.track = {7, 0};
	expected2.width = 0;
	expected2.height = 0;
	expected2.disk = {0, 0};
	expected2.year = 2007;
	expected2.encoder = "Lavf54.63.100"_c;

	assert_metadata(expected2, *actual2.to_props(), "This Year's Love.m4a");
	assert_equal(true, is_empty(actual2.thumbnail_image), "m4a scan thumbnail");

	const auto loaded = ff_scan_and_load_thumb(ff, load_path2);
	assert_equal(false, loaded.success && is_valid(loaded.thumbnail_surface), "m4a load thumbnail");
}

static void should_scan_mov()
{
	const auto load_path = test_files_folder.combine_file("ipod.mov");

	files ff;
	const auto actual = ff_scan_file(ff, load_path);

	prop::item_metadata expected;
	expected.title = "iPad Help"_c;
	expected.tags = "apple ipad ipod support"_c;
	expected.comment = "What to do if you ipad dies"_c;
	expected.audio_codec = "aac"_c;
	expected.audio_sample_rate = 32000;
	expected.audio_sample_type = 35;
	expected.audio_channels = 1;
	expected.pixel_format = "yuv420p"_c;
	expected.duration = 86;
	expected.width = 640;
	expected.height = 480;
	expected.video_codec = "mpeg4"_c;
	expected.created_digitized = df::date_t(2005, 10, 17, 22, 54, 32);
	expected.created_utc = df::date_t(2005, 10, 17, 22, 54, 32);
	expected.year = 2005;

	assert_metadata(expected, *actual.to_props(), "ipod.mov");

	const auto load_path2 = test_files_folder.combine_file("StPauls.MOV");
	const auto actual2 = ff_scan_file(ff, load_path2);

	prop::item_metadata expected2;
	expected2.created_utc = df::date_t(2011, 3, 13, 15, 13, 49);
	expected2.created_digitized = df::date_t(2011, 3, 13, 15, 13, 49);
	expected2.audio_codec = "aac"_c;
	expected2.coordinate = {51.51420, -0.09850};
	expected2.width = 640;
	expected2.height = 480;
	expected2.duration = 10;
	expected2.audio_sample_rate = 44100;
	expected2.audio_sample_type = 35;
	expected2.audio_channels = 1;
	expected2.video_codec = "h264"_c;
	expected2.camera_manufacturer = "Apple"_c;
	expected2.camera_model = "iPhone 3GS"_c;
	expected2.encoder = "4.3"_c;
	expected2.pixel_format = "yuv420p"_c;
	expected2.year = 2011;

	assert_metadata(expected2, *actual2.to_props(), "StPauls.MOV");
}

static void should_scan_avi()
{
	const auto load_path = test_files_folder.combine_file("Byzantium.avi");

	files ff;
	const auto actual = ff_scan_file(ff, load_path);

	prop::item_metadata expected;
	expected.audio_codec = "wmav2"_c;
	expected.title = "Byzantium"_c;
	expected.comment =
		"John Romer recreates the glory and history of Byzantium. From the Hagia Sophia in present-day Istanbul to the looted treasures of the empire now located in St. Marks in Venice."_c;
	expected.audio_sample_rate = 48000;
	expected.audio_sample_type = 35;
	expected.audio_channels = 2;
	expected.duration = 12;
	expected.width = 854;
	expected.height = 480;
	expected.video_codec = "wmv3"_c;
	expected.tags = "Byzantium History Turkey"_c;
	expected.pixel_format = "yuv420p"_c;

	assert_metadata(expected, *actual.to_props(), "Byzantium.avi");
}

static void should_scan_raw()
{
	const auto load_path = test_files_folder.combine("raw").combine_file("Screws.CR2");

	prop::item_metadata expected;
	expected.title = "Screws on Desk"_c;
	expected.file_name = "Screws.CR2"_c;
	expected.copyright_notice = "Copyright"_c;
	expected.tags = "Desk Macro Screws"_c;
	expected.title = "Screws on Desk"_c;
	expected.description = "This is a Description"_c;
	expected.comment = "This is a Comment"_c;
	expected.rating = 4;
	expected.width = 3522;
	expected.height = 2348;
	expected.f_number = 2.8f;
	expected.camera_manufacturer = "Canon"_c;
	expected.camera_model = "EOS-1D Mark II"_c;
	expected.exposure_time = 1 / 25.0f;
	expected.focal_length = 100;
	expected.iso_speed = 100;
	expected.pixel_format = "RGBG"_c;
	expected.created_utc = df::date_t(2011, 9, 23, 21, 49, 16);

	null_async_strategy as;
	location_cache locations;
	index_state index(as, locations);
	auto actual = metadata_from_cache(index, load_path);
	assert_metadata(expected, *actual, "Screws.CR2");
}

static void should_parse_facebook_json()
{
	const auto path_status = test_files_folder.combine_file("place.json");
	const auto json = df::util::json::json_from_file(path_status);

	auto& result = json["result"];
	auto& address_components = result["address_components"];
	assert_equal(5u, address_components.Size(), "data");
	assert_equal("WC1X", address_components[0]["long_name"].GetString(), "long_name");
}

static void should_scan_d64()
{
	constexpr auto file_name = "Ace of Aces (Europe).D64";
	const auto load_path = test_files_folder.combine("retro").combine_file(file_name);
	const auto loaded = df::blob_from_file(load_path);
	const auto contents = files::list_disk(loaded);

	assert_equal(4_z, contents.size(), "d64", file_name);
	assert_equal("147 \"      \" PRG", contents[0].line, "d64", file_name);
}

static void should_scan_archive()
{
	constexpr auto file_name = "benchmarks.zip";
	const auto load_path = test_files_folder.combine_file(file_name);
	const auto contents = files::list_archive(load_path);

	assert_equal(2_z, contents.size(), "d64", file_name);
	assert_equal("PXL_20240404_074316577.jpg", contents[0].filename, "d64", file_name);
}

static void should_scan_mod()
{
	constexpr auto file_name = "giana.mod";
	const auto load_path = test_files_folder.combine("retro").combine_file(file_name);

	files ff;
	const auto actual = ff_scan_file(ff, load_path);
	const auto props = actual.to_props();

	assert_equal("giana!"_c, props->title, "title", file_name);
	assert_equal("Generic ProTracker or compatible"_c, props->encoder, "encoder", file_name);
	assert_equal(48000, props->audio_sample_rate, "encoder", file_name);
}

static void should_scan_heic()
{
	constexpr auto file_name = "melnik.heic";
	const auto load_path = test_files_folder.combine_file(file_name);

	files ff;
	const auto actual = ff_scan_file(ff, load_path);
	const auto props = actual.to_props();

	assert_equal(4000, props->width, "width", file_name);
	assert_equal(2252, props->height, "height", file_name);
	assert_equal("yuv420", props->pixel_format, "pixel_format", file_name);
}

static void should_scan_avif()
{
	constexpr auto file_name = "hato.profile0.10bpc.yuv420.avif";
	const auto load_path = test_files_folder.combine_file(file_name);

	files ff;
	const auto actual = ff_scan_file(ff, load_path);
	const auto props = actual.to_props();

	assert_equal(3078, props->width, "width", file_name);
	assert_equal(2048, props->height, "height", file_name);
	assert_equal("yuv444", props->pixel_format, "pixel_format", file_name);
}

static void should_scan_webp()
{
	constexpr auto file_name = "lake.webp";
	const auto load_path = test_files_folder.combine_file(file_name);

	files ff;
	const auto actual = ff_scan_file(ff, load_path);
	const auto props = actual.to_props();

	assert_equal(550, props->width, "width", file_name);
	assert_equal(368, props->height, "height", file_name);
	assert_equal("yuv420", props->pixel_format, "pixel_format", file_name);
}

static void should_load_po()
{
	const auto app_folder = known_path(platform::known_folder::running_app_folder);
	const auto lang_folder = app_folder.combine("languages");
	const auto lang_path = lang_folder.combine_file("de.po");

	const auto po_entries = load_po(lang_path);

	app_text_t t;
	t.load_lang(lang_path.name(), po_entries);

	assert_equal("Datenbank bereinigen und neu indexieren.\nAlle Daten werden regeneriert.", t.reset_database,
	             "reset_database");
}

void register_tests3(view_state& state, test_registry& tests)
{
	//
	// Scan Metadata
	//
	tests.add("Should scan jpg metadata"s, should_scan_jpeg);
	tests.add("Should scan avi metadata"s, should_scan_avi);
	tests.add("Should scan mov metadata"s, should_scan_mov);
	tests.add("Should scan mp3 metadata"s, should_scan_mp3);
	tests.add("Should scan mp4 metadata"s, should_scan_mp4);
	tests.add("Should scan raw metadata"s, should_scan_raw);
	tests.add("Should scan mod metadata"s, should_scan_mod);
	tests.add("Should scan webp metadata"s, should_scan_webp);
	tests.add("Should scan heif metadata"s, should_scan_heic);
	tests.add("Should scan avif metadata"s, should_scan_avif);
	tests.add("Should parse Xmp"s, should_parse_xmp);
	tests.add("Should merge Xmp sidecar"s, should_merge_xmp_sidecar);
	tests.add("Should replace tokens"s, should_replace_tokens);

	//
	// Commodore
	//
	tests.add("Should scan d64"s, should_scan_d64);

	//
	// Archive
	//
	tests.add("Should scan archive"s, should_scan_archive);

	//
	// Other
	//
	tests.add("Should parse facebook Json"s, should_parse_facebook_json);
	tests.add("Should load po"s, should_load_po);
}
