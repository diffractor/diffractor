// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Metadata tests (metadata_exif, metadata_iptc, metadata_xmp, metadata_icc) -- scanning
// properties from JPEG, MP3, MP4, MOV, MKV, AVI, RAW, HEIF, AVIF, WebP, JXL and MOD files, tag
// editing and removal, sidecar merging, metadata presentation blocks and filename token replacement.

#include "pch.h"

#include "metadata_xmp.h"
#include "metadata_exif.h"
#include "test_fixtures.h"
#include "test_runner.h"
#include "av_format.h"
#include "av_player.h"

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
	const auto xmp_path = test_files_folder.combine_file("Gherkin.xmp");

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
	expected.rating = 4;
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

// Issue #3 - Cannot remove MP4 tags added in an old version.
// Old versions left tags in the native MP4 'KEYW' atom (surfaced by FFmpeg as the
// "keywords" tag). When a file also carries embedded XMP, dc:subject is the
// authoritative tag list, so a stale container keyword must not resurrect a tag
// that was removed via XMP.
static void should_not_resurrect_container_tags()
{
	// Serialise an XMP packet whose dc:subject holds only the kept tag - this
	// represents the state right after the stuck tag was removed via XMP.
	metadata_edits edits;
	edits.add_tags = tag_set("keeptag");
	std::string xmp_buffer;
	metadata_xmp::update(xmp_buffer, edits);

	// A scan result carrying the stale container keyword AND the embedded XMP.
	file_scan_result sr;
	sr.ffmpeg_metadata.emplace_back(str::cache("keywords"), "stucktag");
	sr.metadata.xmp.assign(xmp_buffer.begin(), xmp_buffer.end());

	const auto md = sr.to_props();

	// Only the XMP tag survives; the stale KEYW keyword is not merged back in.
	assert_equal("keeptag"_c, md->tags, "xmp authoritative over stale container keyword");
}

static void should_apply_property_level_windows_metadata_precedence()
{
	metadata_edits unrelated_edits;
	unrelated_edits.title = "unrelated xmp";
	std::string unrelated_xmp;
	metadata_xmp::update(unrelated_xmp, unrelated_edits);

	file_scan_result fallback;
	fallback.ffmpeg_metadata.emplace_back(str::cache("WM/Category"), "WindowsTag");
	fallback.ffmpeg_metadata.emplace_back(str::cache("WM/SharedUserRating"), "75");
	fallback.metadata.xmp.assign(unrelated_xmp.begin(), unrelated_xmp.end());
	const auto fallback_metadata = fallback.to_props();
	assert_equal("WindowsTag"_c, fallback_metadata->tags, "unrelated xmp allows windows tags");
	assert_equal(4, fallback_metadata->rating, "unrelated xmp allows windows rating");

	metadata_edits cleared_edits;
	cleared_edits.add_tags = tag_set("temporary");
	cleared_edits.rating = 5;
	std::string cleared_xmp;
	metadata_xmp::update(cleared_xmp, cleared_edits);
	metadata_edits remove_edits;
	remove_edits.remove_tags = tag_set("temporary");
	remove_edits.remove_rating = true;
	metadata_xmp::update(cleared_xmp, remove_edits);

	file_scan_result cleared;
	cleared.ffmpeg_metadata.emplace_back(str::cache("WM/Category"), "StaleTag");
	cleared.ffmpeg_metadata.emplace_back(str::cache("WM/SharedUserRating"), "99");
	cleared.metadata.xmp.assign(cleared_xmp.begin(), cleared_xmp.end());
	const auto cleared_metadata = cleared.to_props();
	assert_equal(true, cleared_metadata->tags.is_empty(), "cleared xmp tags suppress stale windows tags");
	assert_equal(0, cleared_metadata->rating, "cleared xmp rating suppresses stale windows rating");
}

// Matroska stores metadata as EBML SimpleTags, which FFmpeg surfaces verbatim - so the
// upper-case names Matroska specifies (TITLE, RATING, ...) only reach the UI because our
// key matching is case-insensitive. WebM is the same container with a different DocType,
// so it must behave identically. Nothing else in the corpus is EBML.
static void should_scan_matroska(const std::string_view name, const std::string_view video_codec)
{
	files ff;
	const auto actual = ff_scan_file(ff, test_files_folder.combine_file(name));

	prop::item_metadata expected;
	expected.title = "Matroska Test Title"_c;
	expected.artist = "Matroska Test Artist"_c;
	expected.genre = "TestGenre"_c;
	expected.comment = "matroska test comment"_c;
	expected.tags = "alpha beta"_c;
	expected.rating = 4;
	expected.width = 16;
	expected.height = 16;
	expected.duration = 1;
	expected.encoder = "diffractor-test"_c; // Matroska MuxingApp
	expected.video_codec = str::cache(video_codec);

	assert_metadata(expected, *actual.to_props(), name);
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
	expected.rating = 4;
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
	// Issue #61: the QuickTime ISO6709 location tag must reach the coordinate, or the world map
	// cannot plot the video.
	expected2.coordinate = {51.51420, -0.09850};
	// StPauls.MOV carries a Windows Explorer 'London' tag (WM/Category, no XMP),
	// now surfaced by the FFmpeg 'Xtra' reader. (#123)
	expected2.tags = "London"_c;
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
	expected.created_utc = df::date_t(2011, 9, 23, 23, 49, 16);

	null_async_strategy as;
	const location_cache locations;
	index_state index(as, locations);
	const auto actual = metadata_from_cache(index, load_path);
	assert_metadata(expected, *actual, "Screws.CR2");
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
	assert_equal(48000, props->audio_sample_rate, "audio_sample_rate", file_name);
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

static void should_not_double_apply_heif_rotation()
{
	// This asset carries an 'irot' transform property (270 degrees CCW) alongside an Exif
	// Orientation tag (6) describing the same quarter turn, which is what phone cameras
	// write. libheif applies 'irot' itself: it swaps the dimensions reported by
	// heif_image_handle_get_width/height and rotates the decoded pixels. Applying the
	// Exif orientation on top of that would rotate the image a second time.
	constexpr auto file_name = "melnik-rotated.heic";
	const auto load_path = test_files_folder.combine("excluded1").combine_file(file_name);

	files ff;
	const auto actual = ff_scan_file(ff, load_path);
	const auto props = actual.to_props();

	// Guard the premise: the file really does carry an Exif block with Orientation 6.
	assert_equal(false, actual.metadata.exif.empty(), "exif present", file_name);

	// The stored image is 4000x2252, so the upright image is 2252x4000.
	assert_equal(2252, props->width, "width", file_name);
	assert_equal(4000, props->height, "height", file_name);

	// Already upright, so no further rotation may be requested.
	assert_equal(static_cast<int>(ui::orientation::top_left), static_cast<int>(props->orientation),
	             "orientation", file_name);

	// The display path decodes the same item, so it must agree with the scan rather than
	// rotating a second time - a mismatch here shows as a thumbnail facing the wrong way.
	const auto loaded = ff.load(load_path, false);
	assert_equal(true, is_valid(loaded.s), "loaded", file_name);
	assert_equal(static_cast<int>(ui::orientation::top_left), static_cast<int>(loaded.orientation()),
	             "loaded orientation", file_name);
	assert_equal(2252, loaded.dimensions().cx, "loaded width", file_name);
	assert_equal(4000, loaded.dimensions().cy, "loaded height", file_name);

	// The stored thumbnail is a separate item that need not carry the same 'irot', so its
	// orientation is resolved from its own properties. The upright image is portrait, so the
	// thumbnail must also be portrait once its own orientation is applied.
	const auto* const ft = files::file_type_from_name(load_path);
	const auto with_thumb = ff.scan_file(load_path, true, ft, {}, {}, scan_intent::index);
	assert_equal(true, ui::is_valid(with_thumb.thumbnail_surface), "thumbnail", file_name);

	const auto thumb_extent = with_thumb.thumbnail_surface->dimensions();
	const auto thumb_upright = ui::flips_xy(with_thumb.thumbnail_surface->orientation())
		                           ? sizei{thumb_extent.cy, thumb_extent.cx}
		                           : thumb_extent;
	assert_equal(true, thumb_upright.cy > thumb_upright.cx, "thumbnail is upright portrait", file_name);
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
	// libheif 1.23 reports the file's native chroma (yuv420, per AV1 profile0) via
	// heif_image_handle_get_preferred_decoding_colorspace; 1.18 returned yuv444.
	assert_equal("yuv420", props->pixel_format, "pixel_format", file_name);
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

static void should_scan_jxl()
{
	constexpr auto file_name = "zoltan-tasi.jxl";
	const auto load_path = test_files_folder.combine_file(file_name);

	files ff;
	const auto actual = ff_scan_file(ff, load_path);
	const auto props = actual.to_props();

	assert_equal(true, actual.success, "success", file_name);
	assert_equal(true, props->width > 0 && props->height > 0, "dimensions", file_name);

	// The codestream decodes to a surface matching the scanned dimensions.
	const auto loaded = ff.load(load_path, false);
	assert_equal(false, loaded.is_empty(), "jxl load empty", file_name);
	assert_equal(props->width, loaded.dimensions().cx, "jxl load width", file_name);
	assert_equal(props->height, loaded.dimensions().cy, "jxl load height", file_name);
}

static const metadata_block* find_block(const av_media_info& info, const metadata_standard standard)
{
	for (const auto& b : info.metadata)
	{
		if (b.standard == standard) return &b;
	}

	return nullptr;
}

static const metadata_kv* find_row(const metadata_block& block, const std::string_view key)
{
	for (const auto& r : block.values)
	{
		if (r.key == key) return &r;
	}

	return nullptr;
}

// The verbose pane is a block inspector: each embedded block must describe its own extent and
// present its own shape, so a reader can tell what the file actually contains.
static void should_present_exif_block_by_ifd()
{
	constexpr auto file_name = "Test.jpg";

	files ff;
	const auto sr = ff_scan_file(ff, test_files_folder.combine_file(file_name));
	const auto info = sr.to_info();
	const auto block = find_block(info, metadata_standard::exif);

	assert_equal(true, block != nullptr, "exif block", file_name);
	assert_equal(true, block->bytes > 0, "exif bytes", file_name);
	assert_equal(true, block->parsed, "exif parsed", file_name);

	// Entries are grouped under the IFD they were read from rather than flattened.
	auto containers = 0;
	auto children = 0;
	auto shaped = 0;

	for (const auto& r : block->values)
	{
		if (r.container)
		{
			++containers;
			assert_equal(0, r.depth, "ifd depth", file_name);
		}
		else
		{
			++children;
			if (!r.shape.empty()) ++shaped;
		}
	}

	assert_equal(true, containers > 0, "exif ifd sections", file_name);
	assert_equal(true, children > 0, "exif entries", file_name);
	assert_equal(children, shaped, "exif entry shapes", file_name);

	// The Exif directory carries the capture settings, so it asks to be open whatever its size.
	const auto* exif_ifd = static_cast<const metadata_kv*>(nullptr);

	for (const auto& r : block->values)
	{
		if (r.id == "exif.ifd.2") exif_ifd = &r;
	}

	assert_equal(true, exif_ifd != nullptr, "exif ifd section", file_name);
	assert_equal(true, exif_ifd->open_by_default, "exif ifd open by default", file_name);
}

// A raw file carries the same kind of detail as Exif, so LibRaw's flat report is grouped into
// sections and any xmp sidecar is presented as the file's xmp block.
static void should_present_raw_block_sections()
{
	constexpr auto file_name = "Gherkin.CR2";

	files ff;
	const auto sr = ff_scan_file(ff, test_files_folder.combine_file(file_name), "Gherkin.xmp");
	const auto info = sr.to_info();
	const auto block = find_block(info, metadata_standard::raw);

	assert_equal(true, block != nullptr, "raw block", file_name);

	auto containers = 0;
	auto children = 0;
	auto orphans = 0;

	for (const auto& r : block->values)
	{
		if (r.container)
		{
			++containers;
			assert_equal(0, r.depth, "raw section depth", file_name);
			assert_equal(false, r.id.empty(), "raw section id", file_name);
		}
		else
		{
			++children;
			if (r.depth != 1) ++orphans;
		}
	}

	assert_equal(true, containers > 1, "raw sections", file_name);
	assert_equal(true, children > 0, "raw values", file_name);
	assert_equal(0, orphans, "raw value depth", file_name);

	// No section is left as an empty heading.
	for (size_t i = 0; i < block->values.size(); ++i)
	{
		if (block->values[i].container)
		{
			const auto has_child = (i + 1) < block->values.size() && !block->values[i + 1].container;
			assert_equal(true, has_child, "raw section has values", file_name);
		}
	}

	const auto xmp = find_block(info, metadata_standard::xmp);

	assert_equal(true, xmp != nullptr, "raw xmp sidecar block", file_name);
	assert_equal(true, xmp->bytes > 0, "raw xmp sidecar bytes", file_name);
	assert_equal(false, xmp->values.empty(), "raw xmp sidecar values", file_name);
}

static const metadata_kv* find_row_prefix(const metadata_block& block, const std::string_view prefix)
{
	for (const auto& r : block.values)
	{
		if (r.key.starts_with(prefix)) return &r;
	}

	return nullptr;
}

// The structure block reports how the file is built rather than what it says about itself, so it
// survives metadata stripping and shows the traces the encoder left.
static void should_present_jpeg_structure_block()
{
	constexpr auto file_name = "Test.jpg";

	files ff;
	const auto sr = ff_scan_file(ff, test_files_folder.combine_file(file_name));
	const auto info = sr.to_info();
	const auto block = find_block(info, metadata_standard::structure);

	assert_equal(true, block != nullptr, "structure block", file_name);

	const auto summary = find_row_prefix(*block, "Summary");
	const auto segments = find_row_prefix(*block, "Segments");
	const auto quant = find_row_prefix(*block, "Quantisation tables");
	const auto huffman = find_row_prefix(*block, "Huffman tables (");

	assert_equal(true, summary != nullptr, "structure summary", file_name);
	assert_equal(true, summary->open_by_default, "structure summary open", file_name);
	assert_equal(true, segments != nullptr, "structure segments", file_name);
	assert_equal(true, quant != nullptr, "structure quantisation tables", file_name);
	assert_equal(true, huffman != nullptr, "structure huffman tables", file_name);

	// The frame is described, and the trailer is accounted for either way.
	assert_equal(true, find_row(*block, "Encoding") != nullptr, "structure encoding", file_name);
	assert_equal(true, find_row(*block, "Chroma subsampling") != nullptr, "structure subsampling", file_name);
	assert_equal(true, find_row(*block, "End of image marker") != nullptr, "structure eoi", file_name);

	// Quantisation tables carry the coefficient grid rather than being summarised away.
	auto tables_with_detail = 0;

	for (const auto& r : block->values)
	{
		const auto detail = std::get_if<metadata_numeric_detail>(&r.detail);
		if (r.key.starts_with("Table ") && detail && detail->values.size() == 64 && detail->columns == 8)
		{
			++tables_with_detail;
		}
	}

	assert_equal(true, tables_with_detail > 0, "structure table detail", file_name);
}

static void should_present_webp_structure_block()
{
	constexpr auto file_name = "lake.webp";

	files ff;
	const auto sr = ff_scan_file(ff, test_files_folder.combine_file(file_name));
	const auto info = sr.to_info();
	const auto block = find_block(info, metadata_standard::structure);

	assert_equal(true, block != nullptr, "structure block", file_name);
	assert_equal(true, find_row_prefix(*block, "Chunks") != nullptr, "webp chunks", file_name);
	assert_equal(true, find_row(*block, "Compression") != nullptr, "webp compression", file_name);
}

static void should_present_heif_structure_block()
{
	constexpr auto file_name = "melnik.heic";

	files ff;
	const auto sr = ff_scan_file(ff, test_files_folder.combine_file(file_name));
	const auto info = sr.to_info();
	const auto block = find_block(info, metadata_standard::structure);

	assert_equal(true, block != nullptr, "structure block", file_name);
	assert_equal(true, find_row(*block, "Major brand") != nullptr, "heif brand", file_name);
	assert_equal(true, find_row(*block, "Primary item id") != nullptr, "heif primary item", file_name);
	assert_equal(true, find_row(*block, "Stored size") != nullptr, "heif stored size", file_name);
}

static void should_present_icc_block_sections()
{
	constexpr auto file_name = "cmyk.jpg";

	files ff;
	const auto sr = ff_scan_file(ff, test_files_folder.combine_file(file_name));
	const auto info = sr.to_info();
	const auto block = find_block(info, metadata_standard::icc);

	assert_equal(true, block != nullptr, "icc block", file_name);
	assert_equal(true, block->bytes > 0, "icc bytes", file_name);

	// Summary, header and tag directory are all present and deliberately labelled, so decoded
	// values never replace the raw tag list they were derived from.
	const auto summary = find_row(*block, "Summary");
	const auto header = find_row(*block, "Header");

	assert_equal(true, summary != nullptr, "icc summary", file_name);
	assert_equal(true, header != nullptr, "icc header", file_name);
	assert_equal(true, summary->container, "icc summary container", file_name);
	assert_equal(true, header->container, "icc header container", file_name);

	auto tags = 0;

	for (const auto& r : block->values)
	{
		if (r.id.starts_with("icc.tag.")) ++tags;
	}

	assert_equal(true, tags > 0, "icc tag rows", file_name);
}

// The description panel presents one section for every prose field, so the field list drives its
// header name, its ordering, and which entries collapse as repeats.
static void should_collect_descriptive_fields()
{
	prop::item_metadata none;
	assert_equal(size_t{0}, prop::descriptive_fields(none).size(), "no prose fields");

	prop::item_metadata one;
	one.comment = "A note"_c;
	const auto only_comment = prop::descriptive_fields(one);
	assert_equal(size_t{1}, only_comment.size(), "single prose field");
	assert_equal("comment", only_comment[0].id, "lone field keeps its own identity");
	assert_equal(false, only_comment[0].duplicate, "a lone field is never a repeat");

	prop::item_metadata all;
	all.comment = "Same text"_c;
	all.description = "Same text"_c;
	all.synopsis = "Different text"_c;
	const auto ordered = prop::descriptive_fields(all);
	assert_equal(size_t{3}, ordered.size(), "every populated field listed");
	assert_equal("description", ordered[0].id, "description leads");
	assert_equal("synopsis", ordered[1].id, "synopsis follows");
	assert_equal("comment", ordered[2].id, "comment last");
	assert_equal(false, ordered[0].duplicate, "leading field is the original");
	assert_equal(false, ordered[1].duplicate, "distinct text is not a repeat");
	assert_equal(true, ordered[2].duplicate, "text already shown is marked a repeat");
}

static void should_scan_info_from_title()
{
	const auto assert_name = [](const std::string_view name, const media_name_props& expected)
	{
		const auto actual = scan_info_from_title(name);
		assert_equal(expected.show, actual.show, "show", name);
		assert_equal(expected.title, actual.title, "title", name);
		assert_equal(expected.season, actual.season, "season", name);
		assert_equal(expected.episode, actual.episode, "episode", name);
		assert_equal(expected.episode_of, actual.episode_of, "episode_of", name);
		assert_equal(expected.year, actual.year, "year", name);
	};

	assert_name("Game.of.Thrones.S02E06.HDTV.x264 - 2HD.mp4",
	            {.show = "Game of Thrones", .season = 2, .episode = 6});
	assert_name("It's.a.Wonderful.Life.1946.720p.BluRay.x264.YIFY.mp4",
	            {.title = "It's a Wonderful Life", .year = 1946});
	assert_name("The.Show.S01E02.The.Beginning.2160p.WEB-DL.HEVC.HDR",
	            {.show = "The Show", .title = "The Beginning", .season = 1, .episode = 2});
	assert_name("The.Show.S01E02.The.Beginning.DV.HEVC",
	            {.show = "The Show", .title = "The Beginning", .season = 1, .episode = 2});
	assert_name("Show_Name_S02E06_1080p", {.show = "Show Name", .season = 2, .episode = 6});
	assert_name("Show.Name.1x02.720p", {.show = "Show Name", .season = 1, .episode = 2});
	assert_name("Documentary.Part.02of10.1080p", {.show = "Documentary Part", .episode = 2, .episode_of = 10});
	assert_name("Holiday.Video.1920x1080", {.title = "Holiday Video"});
	assert_name("Movie.Title.2024", {.title = "Movie Title", .year = 2024});
	assert_name("Movie.Title.(2024).2160p", {.title = "Movie Title", .year = 2024});
	assert_name("The.Show.(2020).S01E02.1080p",
	            {.show = "The Show", .season = 1, .episode = 2, .year = 2020});
	assert_name("The.Show.S01E02.[GROUP].Episode.Title.1080p",
	            {.show = "The Show", .title = "Episode Title", .season = 1, .episode = 2});
	assert_name("Show.S999999999999E1.1080p", {.title = "Show S999999999999E1"});
	assert_name("Series.10of02.1080p", {.title = "Series 10of02"});
	assert_name("Family.Holiday.Video", {});
}

static void should_parse_exif_tags()
{
	// Builds a minimal little-endian TIFF/EXIF block that exercises the tags added to
	// the EXIF parser: Software (-> encoder), Artist (-> artist), the Windows XP string
	// tags (XPTitle/XPAuthor/XPKeywords/XPSubject) and the APEX MaxApertureValue /
	// ShutterSpeedValue fallbacks. XP values use ASCII payloads so decoding is
	// deterministic; the point of this test is the tag-dispatch and fallback logic.
	struct exif_entry
	{
		uint16_t tag;
		uint16_t fmt;
		uint32_t count;
		std::vector<uint8_t> data;
	};

	const auto ascii = [](const std::string_view s) { return std::vector<uint8_t>(s.begin(), s.end()); };
	const auto rational_bytes = [](const int32_t n, const int32_t d)
	{
		std::vector<uint8_t> v(8);
		memcpy(v.data(), &n, 4);
		memcpy(v.data() + 4, &d, 4);
		return v;
	};

	// Entries must be in ascending tag order (ARTIST before XPAuthor so the XPAuthor
	// guard is proven not to overwrite the already-set artist).
	std::vector<exif_entry> entries = {
		{0x0131, FMT_STRING, 0, ascii("DiffractorApp")}, // Software      -> encoder
		{0x013b, FMT_STRING, 0, ascii("Ansel Adams")}, //   Artist        -> artist
		{0x9201, FMT_SRATIONAL, 1, rational_bytes(6, 1)}, // ShutterSpeed  -> 2^-6 = 1/64s
		{0x9205, FMT_URATIONAL, 1, rational_bytes(4, 1)}, // MaxAperture   -> 2^(4/2) = f/4
		{0x9c9b, FMT_STRING, 0, ascii("Winter")}, //         XPTitle       -> title
		{0x9c9d, FMT_STRING, 0, ascii("Ignored Author")}, // XPAuthor      -> artist (guarded)
		{0x9c9e, FMT_STRING, 0, ascii("alpha beta")}, //     XPKeywords    -> tags
		{0x9c9f, FMT_STRING, 0, ascii("A subject")}, //      XPSubject     -> description
	};

	for (auto& e : entries)
	{
		if (e.count == 0) e.count = static_cast<uint32_t>(e.data.size());
	}

	std::vector<uint8_t> buf;
	const auto put16 = [&buf](const uint16_t v)
	{
		buf.push_back(static_cast<uint8_t>(v));
		buf.push_back(static_cast<uint8_t>(v >> 8));
	};
	const auto put32 = [&buf](const uint32_t v)
	{
		buf.push_back(static_cast<uint8_t>(v));
		buf.push_back(static_cast<uint8_t>(v >> 8));
		buf.push_back(static_cast<uint8_t>(v >> 16));
		buf.push_back(static_cast<uint8_t>(v >> 24));
	};

	const auto entry_count = static_cast<uint32_t>(entries.size());
	constexpr uint32_t ifd0_offset = 8;
	const uint32_t data_start = ifd0_offset + 2 + entry_count * 12 + 4;

	// Reserve word-aligned out-of-line offsets for payloads that don't fit in 4 bytes.
	std::vector<uint32_t> data_offset(entries.size(), 0);
	uint32_t cursor = data_start;
	for (size_t i = 0; i < entries.size(); ++i)
	{
		const auto size = static_cast<uint32_t>(entries[i].data.size());
		if (size > 4)
		{
			data_offset[i] = cursor;
			cursor += size;
			if (cursor & 1) ++cursor;
		}
	}

	// TIFF header (little-endian).
	put16(0x4949);
	put16(0x002a);
	put32(ifd0_offset);

	// IFD0.
	put16(static_cast<uint16_t>(entry_count));
	for (size_t i = 0; i < entries.size(); ++i)
	{
		const auto& e = entries[i];
		put16(e.tag);
		put16(e.fmt);
		put32(e.count);

		const auto size = static_cast<uint32_t>(e.data.size());
		if (size > 4)
		{
			put32(data_offset[i]);
		}
		else
		{
			uint32_t inline_value = 0;
			memcpy(&inline_value, e.data.data(), size);
			put32(inline_value);
		}
	}
	put32(0); // No IFD1.

	// Out-of-line payloads (word aligned, matching the reserved offsets above).
	for (const auto& e : entries)
	{
		if (e.data.size() > 4)
		{
			buf.insert(buf.end(), e.data.begin(), e.data.end());
			if (buf.size() & 1) buf.push_back(0);
		}
	}

	prop::item_metadata md;
	metadata_exif::parse(md, df::cspan{buf.data(), buf.size()});

	assert_equal("DiffractorApp", md.encoder, "Software -> encoder");
	assert_equal("Ansel Adams", md.artist, "Artist -> artist (XPAuthor must not override)");
	assert_equal("Winter", md.title, "XPTitle -> title");
	assert_equal("alpha beta", md.tags, "XPKeywords -> tags");
	assert_equal("A subject", md.description, "XPSubject -> description");
	assert_equal(prop::format_f_num(4.0f), prop::format_f_num(md.f_number), "MaxApertureValue -> f_number");
	assert_equal(prop::format_exposure(1.0f / 64.0f), prop::format_exposure(md.exposure_time),
	             "ShutterSpeedValue -> exposure_time");
}

// locations.md 2.8: GPS altitude and speed live in a sub-IFD reached through tag 0x8825, and
// the altitude reference that decides the sign may arrive either side of the value.
static void should_parse_exif_gps_height()
{
	struct gps_entry
	{
		uint16_t tag;
		uint16_t fmt;
		uint32_t count;
		std::vector<uint8_t> data;
	};

	const auto build_gps_exif = [](const std::vector<gps_entry>& entries)
	{
		std::vector<uint8_t> buf;
		const auto put16 = [&buf](const uint16_t v)
		{
			buf.push_back(static_cast<uint8_t>(v));
			buf.push_back(static_cast<uint8_t>(v >> 8));
		};
		const auto put32 = [&buf](const uint32_t v)
		{
			buf.push_back(static_cast<uint8_t>(v));
			buf.push_back(static_cast<uint8_t>(v >> 8));
			buf.push_back(static_cast<uint8_t>(v >> 16));
			buf.push_back(static_cast<uint8_t>(v >> 24));
		};

		constexpr uint32_t ifd0_offset = 8;
		constexpr uint32_t gps_ifd_offset = ifd0_offset + 2 + 12 + 4; // IFD0 holds one entry
		const auto count = static_cast<uint32_t>(entries.size());
		const uint32_t gps_data_start = gps_ifd_offset + 2 + count * 12 + 4;

		std::vector<uint32_t> data_offset(entries.size(), 0);
		uint32_t cursor = gps_data_start;
		for (size_t i = 0; i < entries.size(); ++i)
		{
			if (entries[i].data.size() > 4)
			{
				data_offset[i] = cursor;
				cursor += static_cast<uint32_t>(entries[i].data.size());
			}
		}

		put16(0x4949);
		put16(0x002a);
		put32(ifd0_offset);

		put16(1);
		put16(0x8825); // GPS IFD pointer
		put16(4); // FMT_ULONG
		put32(1);
		put32(gps_ifd_offset);
		put32(0);

		put16(static_cast<uint16_t>(count));
		for (size_t i = 0; i < entries.size(); ++i)
		{
			const auto& e = entries[i];
			put16(e.tag);
			put16(e.fmt);
			put32(e.count);

			const auto size = static_cast<uint32_t>(e.data.size());
			if (size > 4)
			{
				put32(data_offset[i]);
			}
			else
			{
				uint32_t inline_value = 0;
				memcpy(&inline_value, e.data.data(), size);
				put32(inline_value);
			}
		}
		put32(0);

		for (const auto& e : entries)
		{
			if (e.data.size() > 4) buf.insert(buf.end(), e.data.begin(), e.data.end());
		}

		return buf;
	};

	const auto urational = [](const uint32_t n, const uint32_t d)
	{
		std::vector<uint8_t> v(8);
		memcpy(v.data(), &n, 4);
		memcpy(v.data() + 4, &d, 4);
		return v;
	};

	constexpr uint16_t FMT_BYTE = 1;
	constexpr uint16_t FMT_STRING = 2;
	constexpr uint16_t FMT_URATIONAL = 5;

	// A cruising airliner: 10,668 m above sea level at 900 km/h.
	prop::item_metadata cruising;
	const auto cruising_exif = build_gps_exif({
		{0x0005, FMT_BYTE, 1, {0}},
		{0x0006, FMT_URATIONAL, 1, urational(10668, 1)},
		{0x000c, FMT_STRING, 2, {'K', 0}},
		{0x000d, FMT_URATIONAL, 1, urational(900, 1)},
	});
	metadata_exif::parse(cruising, df::cspan{cruising_exif.data(), cruising_exif.size()});
	assert_equal(10668.0f, cruising.altitude, "GPSAltitude -> altitude");
	assert_equal(900.0f, cruising.gps_speed, "GPSSpeed in km/h -> gps_speed");

	// A dive: reference 1 means below sea level, so the stored positive magnitude is negated.
	prop::item_metadata dive;
	const auto dive_exif = build_gps_exif({
		{0x0005, FMT_BYTE, 1, {1}},
		{0x0006, FMT_URATIONAL, 1, urational(180, 10)},
	});
	metadata_exif::parse(dive, df::cspan{dive_exif.data(), dive_exif.size()});
	assert_equal(-18.0f, dive.altitude, "below-sea-level reference negates the altitude");

	// Knots are the other common speed reference.
	prop::item_metadata knots;
	const auto knots_exif = build_gps_exif({
		{0x000c, FMT_STRING, 2, {'N', 0}},
		{0x000d, FMT_URATIONAL, 1, urational(100, 1)},
	});
	metadata_exif::parse(knots, df::cspan{knots_exif.data(), knots_exif.size()});
	assert_equal(185.2f, knots.gps_speed, "GPSSpeed in knots -> km/h");
}

// Issue #65 - Binary text in JPEG comment
// Some Samsung phones (e.g. SM-G900F) store a binary blob in the EXIF UserComment
// tag, either raw or behind a valid "ASCII\0\0\0" character code. The parser must
// recognise the junk and drop it, while still preserving valid comments.
static void should_drop_binary_exif_comment()
{
	constexpr uint16_t TAG_USER_COMMENT = 0x9286;
	constexpr uint16_t FMT_UNDEFINED = 7;

	// Builds a minimal little-endian TIFF/EXIF block containing a single entry.
	const auto build_exif = [](const uint16_t tag, const uint16_t fmt, const std::vector<uint8_t>& data)
	{
		std::vector<uint8_t> buf;
		const auto put16 = [&buf](const uint16_t v)
		{
			buf.push_back(static_cast<uint8_t>(v));
			buf.push_back(static_cast<uint8_t>(v >> 8));
		};
		const auto put32 = [&buf](const uint32_t v)
		{
			buf.push_back(static_cast<uint8_t>(v));
			buf.push_back(static_cast<uint8_t>(v >> 8));
			buf.push_back(static_cast<uint8_t>(v >> 16));
			buf.push_back(static_cast<uint8_t>(v >> 24));
		};

		constexpr uint32_t ifd0_offset = 8;
		constexpr uint32_t data_start = ifd0_offset + 2 + 1 * 12 + 4; // one entry
		const auto size = static_cast<uint32_t>(data.size());

		put16(0x4949); // little-endian TIFF header
		put16(0x002a);
		put32(ifd0_offset);

		put16(1); // entry count
		put16(tag);
		put16(fmt);
		put32(size);
		if (size > 4)
		{
			put32(data_start);
		}
		else
		{
			uint32_t inline_value = 0;
			memcpy(&inline_value, data.data(), size);
			put32(inline_value);
		}
		put32(0); // no IFD1

		if (size > 4) buf.insert(buf.end(), data.begin(), data.end());
		return buf;
	};

	// The Samsung junk blob begins with the fixed marker {0x12, 0xf8, 0x0f, 0x3b}.
	const std::vector<uint8_t> junk = {
		0x12, 0xf8, 0x0f, 0x3b, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77
	};
	prop::item_metadata md_junk;
	const auto exif_junk = build_exif(TAG_USER_COMMENT, FMT_UNDEFINED, junk);
	metadata_exif::parse(md_junk, df::cspan{exif_junk.data(), exif_junk.size()});
	assert_equal(true, prop::is_null(md_junk.comment), "binary Samsung comment dropped");

	// SM-G900F hides its blob behind a valid "ASCII\0\0\0" character code, so the marker
	// test alone never fires; the payload is still binary and must be dropped.
	std::vector<uint8_t> samsung = {'A', 'S', 'C', 'I', 'I', 0, 0, 0, 0x0a, 0, 0, 0};
	for (const auto c : {'J', 'K', 'J', 'K'}) samsung.push_back(static_cast<uint8_t>(c));
	for (const uint8_t c : {0x27, 0x03, 0xab, 0x5c, 0x46, 0x0b, 0x01, 0x00}) samsung.push_back(c);
	prop::item_metadata md_samsung;
	const auto exif_samsung = build_exif(TAG_USER_COMMENT, FMT_UNDEFINED, samsung);
	metadata_exif::parse(md_samsung, df::cspan{exif_samsung.data(), exif_samsung.size()});
	assert_equal(true, prop::is_null(md_samsung.comment), "ASCII-prefixed binary comment dropped");

	// Control: a well-formed ASCII UserComment ("ASCII\0\0\0" prefix) is preserved.
	const std::vector<uint8_t> good = {
		'A', 'S', 'C', 'I', 'I', 0, 0, 0, 'H', 'e', 'l', 'l', 'o'
	};
	prop::item_metadata md_good;
	const auto exif_good = build_exif(TAG_USER_COMMENT, FMT_UNDEFINED, good);
	metadata_exif::parse(md_good, df::cspan{exif_good.data(), exif_good.size()});
	assert_equal("Hello", md_good.comment, "valid ASCII comment preserved");

	// Control: multi-line text with trailing nul padding survives the binary scan.
	const std::vector<uint8_t> padded = {
		'A', 'S', 'C', 'I', 'I', 0, 0, 0, 'L', 'i', 'n', 'e', '\n', '2', 0, 0
	};
	prop::item_metadata md_padded;
	const auto exif_padded = build_exif(TAG_USER_COMMENT, FMT_UNDEFINED, padded);
	metadata_exif::parse(md_padded, df::cspan{exif_padded.data(), exif_padded.size()});
	assert_equal("Line\n2", md_padded.comment, "multi-line padded comment preserved");
}

static void should_copy_preserve_properties()
{
	prop::item_metadata src;
	src.title = "Test Title"_c;
	src.rating = 4;
	src.media_position = 19.5;
	src.iso_speed = 400;
	src.artist = "Test Artist"_c;

	prop::item_metadata dst;
	dst = src;

	assert_equal(src.title, dst.title, "should copy title");
	assert_equal(src.rating, dst.rating, "should copy rating");
	assert_equal(src.iso_speed, dst.iso_speed, "should copy iso_speed");
	assert_equal(src.artist, dst.artist, "should copy artist");
}

static void should_replace_item_metadata_without_resetting_playback_position()
{
	df::index_file_item initial;
	initial.name = "position.mp4"_c;
	initial.ft = files::file_type_from_name(initial.name);
	const auto initial_metadata = std::make_shared<prop::item_metadata>();
	initial_metadata->title = "Initial"_c;
	initial_metadata->media_position = 12.0;
	initial.metadata.store(initial_metadata);

	const auto path = df::file_path("c:\\position.mp4");
	const auto item = std::make_shared<df::item_element>(path, initial);
	const auto old_snapshot = item->metadata();
	item->media_position(24.0);

	const auto refreshed = initial;
	const auto refreshed_metadata = std::make_shared<prop::item_metadata>(*initial_metadata);
	refreshed_metadata->title = "Refreshed"_c;
	refreshed_metadata->media_position = 4.0;
	refreshed.metadata.store(refreshed_metadata);
	item->update(path, refreshed);

	assert_equal("Initial", old_snapshot->title, "old metadata snapshot remains unchanged");
	assert_equal("Refreshed", item->metadata()->title, "item receives refreshed metadata snapshot");
	assert_equal(24, static_cast<int>(item->media_position()), "live playback position survives metadata refresh");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Issue #209 - Unable to remove tags by any means
// Tags removed via tag_set::remove() should actually be deleted.
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_remove_tags()
{
	tag_set tags("cat dog bird fish");
	assert_equal(4, static_cast<int>(tags.size()), "initial tag count");

	// Remove a single tag
	const tag_set to_remove("dog");
	tags.remove(to_remove);
	assert_equal(3, static_cast<int>(tags.size()), "after removing dog");

	// Verify removed tag is gone and others remain
	const auto result = tags.to_string();
	assert_equal(true, result.find("dog") == std::string::npos, "dog should be gone");
	assert_equal(true, result.find("cat") != std::string::npos, "cat should remain");
	assert_equal(true, result.find("bird") != std::string::npos, "bird should remain");
	assert_equal(true, result.find("fish") != std::string::npos, "fish should remain");
}

static void should_remove_tags_case_insensitive()
{
	tag_set tags("Cat Dog Bird");
	const tag_set to_remove("DOG");
	tags.remove(to_remove);

	assert_equal(2, static_cast<int>(tags.size()), "case insensitive remove");
	assert_equal(true, tags.to_string().find("Dog") == std::string::npos, "Dog gone");
}

static void should_remove_multiple_tags()
{
	tag_set tags("cat dog bird fish");
	const tag_set to_remove("dog fish");
	tags.remove(to_remove);

	assert_equal(2, static_cast<int>(tags.size()), "multi-remove count");
}

static void should_remove_all_tags()
{
	tag_set tags("cat dog");
	const tag_set to_remove("cat dog");
	tags.remove(to_remove);

	assert_equal(true, tags.is_empty(), "all tags removed");
}

static void should_remove_nonexistent_tag()
{
	tag_set tags("cat dog");
	const tag_set to_remove("elephant");
	tags.remove(to_remove);

	// Should not crash and should leave existing tags untouched
	assert_equal(2, static_cast<int>(tags.size()), "remove nonexistent");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Issue #219 - Korean tags are not working
// Korean (Hangul) tags must round-trip through tag parsing.
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_parse_korean_tags()
{
	constexpr auto family = "\uAC00\uC871"; // 가족
	constexpr auto travel = "\uC5EC\uD589"; // 여행
	constexpr auto photo = "\uC0AC\uC9C4"; // 사진

	// Space-separated Korean tags split into individual tags.
	tag_set tags(std::format("{} {} {}", family, travel, photo));
	assert_equal(3, static_cast<int>(tags.size()), "korean tag count");

	// Removing one Korean tag leaves the others intact (case-insensitive path).
	tags.remove(tag_set(travel));
	assert_equal(2, static_cast<int>(tags.size()), "korean tag count after remove");
	assert_equal(true, tags.to_string().find(travel) == std::string::npos, "travel tag removed");
	assert_equal(true, tags.to_string().find(family) != std::string::npos, "family tag remains");
	assert_equal(true, tags.to_string().find(photo) != std::string::npos, "photo tag remains");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Issue #184 - 'Group by date created' uses the wrong date (ignores DateTimeOriginal)
// The media "created" date (used for group-by/sort-by date created and the
// displayed creation date) must prefer the EXIF DateTimeOriginal capture time
// over the container/file creation time, falling back to the latter only when
// no capture time is present.
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_prefer_datetimeoriginal_for_created()
{
	// DateTimeOriginal (created_exif) wins over the file/container date (created_utc).
	prop::item_metadata md;
	md.created_utc = df::date_t(2020, 1, 1, 12, 0, 0); // e.g. date the file was written
	md.created_exif = df::date_t(2005, 6, 15, 9, 30, 0); // capture time
	assert_equal(df::date_t(2005, 6, 15, 9, 30, 0), md.created(), "prefers DateTimeOriginal");

	// With no capture time, fall back to the container/file creation date.
	prop::item_metadata md2;
	md2.created_utc = df::date_t(2020, 1, 1, 12, 0, 0);
	assert_equal(md2.created_utc.system_to_local(), md2.created(), "falls back to created_utc");

	// With neither set the date is invalid (the item then uses the file date).
	constexpr prop::item_metadata md3;
	assert_equal(false, md3.created().is_valid(), "no dates -> invalid");
}

// tag_set is what every keyword edit passes through, and it is the reason a written file keeps its
// tags in a stable order. Ordering, case-insensitive de-duplication and the quoting round trip are
// each load-bearing: a set that reorders makes an unchanged file look edited to the write path.
static void should_combine_and_deduplicate_tag_sets()
{
	tag_set tags("holiday beach");
	assert_equal(2, static_cast<int>(tags.size()), "parsed tag count");

	tags.add(tag_set("Beach sunset"));
	tags.make_unique();

	// Case-insensitive: "Beach" must not join "beach" as a second tag.
	assert_equal(3, static_cast<int>(tags.size()), "a differently cased duplicate is not a new tag");
	assert_equal("beach holiday sunset", tags.to_string(" ", false), "tags are held in name order");

	tags.remove(tag_set("HOLIDAY"));
	assert_equal("beach sunset", tags.to_string(" ", false), "removal ignores case");

	tags.remove(tag_set("absent"));
	assert_equal("beach sunset", tags.to_string(" ", false), "removing an absent tag changes nothing");

	// A tag containing a space has to survive the string round trip, which is what quoting is for.
	tag_set quoted("\"new york\" paris");
	assert_equal(2, static_cast<int>(quoted.size()), "a quoted multi-word tag is one tag");
	assert_equal(true, tag_set(quoted.to_string()) == quoted, "a quoted tag set round trips through text");

	tags.clear();
	assert_equal(true, tags.is_empty(), "a cleared set is empty");
}

void register_metadata_tests(view_state& state, test_registry& tests)
{
	tests.add("Should collect descriptive fields"s, should_collect_descriptive_fields);
	tests.add("Should scan info from title"s, should_scan_info_from_title);
	tests.add("Should combine and deduplicate tag sets"s, should_combine_and_deduplicate_tag_sets);
	tests.add("Should copy preserve properties"s, should_copy_preserve_properties);
	tests.add("Should replace item metadata without resetting playback position"s,
	          should_replace_item_metadata_without_resetting_playback_position);
	tests.add("Should parse exif tags"s, should_parse_exif_tags);
	tests.add("Should parse exif gps height"s, should_parse_exif_gps_height);

	// Issue #65 - binary exif comment
	tests.add("Should drop binary exif comment"s, should_drop_binary_exif_comment);

	//
	// Tags
	//
	// Issue #209 - tag removal
	tests.add("Should remove tags"s, should_remove_tags);
	tests.add("Should remove tags case insensitive"s, should_remove_tags_case_insensitive);
	tests.add("Should remove multiple tags"s, should_remove_multiple_tags);
	tests.add("Should remove all tags"s, should_remove_all_tags);
	tests.add("Should remove nonexistent tag"s, should_remove_nonexistent_tag);

	// Issue #219 - Korean tags
	tests.add("Should parse Korean tags"s, should_parse_korean_tags);

	// Issue #184 - group by date created uses DateTimeOriginal
	tests.add("Should prefer DateTimeOriginal for created"s, should_prefer_datetimeoriginal_for_created);

	//
	// Scan Metadata
	//
	tests.add("Should scan jpg metadata"s, should_scan_jpeg);
	tests.add("Should scan avi metadata"s, should_scan_avi);
	tests.add("Should scan mov metadata"s, should_scan_mov);
	tests.add("Should scan mkv metadata"s, [] { should_scan_matroska("tagged.mkv", "rawvideo"); });
	tests.add("Should scan webm metadata"s, [] { should_scan_matroska("tagged.webm", "vp8"); });
	tests.add("Should scan mp3 metadata"s, should_scan_mp3);
	tests.add("Should scan mp4 metadata"s, should_scan_mp4);

	// Issue #3 - container tags added by an old version cannot be removed
	tests.add("Should not resurrect container tags"s, should_not_resurrect_container_tags);
	tests.add("Should apply property level Windows metadata precedence"s,
	          should_apply_property_level_windows_metadata_precedence);
	tests.add("Should scan raw metadata"s, should_scan_raw);
	tests.add("Should scan mod metadata"s, should_scan_mod);
	tests.add("Should scan webp metadata"s, should_scan_webp);
	tests.add("Should scan jxl metadata"s, should_scan_jxl);
	tests.add("Should scan heif metadata"s, should_scan_heic);
	tests.add("Should not double apply heif rotation"s, should_not_double_apply_heif_rotation);
	tests.add("Should scan avif metadata"s, should_scan_avif);
	tests.add("Should parse Xmp"s, should_parse_xmp);
	tests.add("Should present exif metadata by ifd"s, should_present_exif_block_by_ifd);
	tests.add("Should present raw metadata sections"s, should_present_raw_block_sections);
	tests.add("Should present icc metadata sections"s, should_present_icc_block_sections);
	tests.add("Should present jpeg structure"s, should_present_jpeg_structure_block);
	tests.add("Should present webp structure"s, should_present_webp_structure_block);
	tests.add("Should present heif structure"s, should_present_heif_structure_block);
	tests.add("Should merge Xmp sidecar"s, should_merge_xmp_sidecar);
	tests.add("Should replace tokens"s, should_replace_tokens);
}
