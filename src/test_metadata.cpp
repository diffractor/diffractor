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

// Issue #78 - Some videos ignore aspect ratio.
// anamorphic.mp4 is stored at 640x480 with a 4:3 pixel (sample) aspect ratio,
// i.e. a 16:9 display. The scanner must report the display dimensions (640x360)
// rather than the stored frame size (640x480).
static void should_apply_video_aspect_ratio()
{
	const auto load_path = test_files_folder.combine_file("anamorphic.mp4");

	files ff;
	const auto actual = ff_scan_file(ff, load_path);
	const auto md = actual.to_props();

	assert_equal(640, md->width, "anamorphic display width");
	assert_equal(360, md->height, "anamorphic display height");
}

// A container-level seek does not flush the codec, so extract_thumbnail must flush
// the decoder after seeking - otherwise, when the decoder is reused across calls, a
// later-position thumbnail can be served from a frame that was buffered before the
// seek. This reuses one decoder for an early and a late thumbnail (the exact reuse
// scenario) and asserts the two frames differ.
static void should_flush_decoder_on_thumbnail_seek()
{
	const auto load_path = test_files_folder.combine_file("gizmo.mp4");

	av_format_decoder decoder;
	assert_equal(true, decoder.open(load_path), "open gizmo.mp4");
	decoder.init_streams(-1, -1, false, false, false);
	assert_equal(true, decoder.has_video(), "gizmo.mp4 has video");

	constexpr sizei max_dim(256, 256);

	ui::surface_ptr early;
	assert_equal(true, decoder.extract_thumbnail(early, max_dim, 1, 100), "early thumbnail decoded");
	assert_equal(true, is_valid(early), "early thumbnail valid");

	// Reuse the same decoder; the seek to 95% must flush the frames buffered by the
	// early extraction above rather than replaying one of them.
	ui::surface_ptr late;
	assert_equal(true, decoder.extract_thumbnail(late, max_dim, 95, 100), "late thumbnail decoded");
	assert_equal(true, is_valid(late), "late thumbnail valid");

	const auto same_size = is_valid(early) && is_valid(late) && early->size() == late->size();
	assert_equal(true, same_size, "thumbnails allocated to the same size");

	const auto identical = same_size && memcmp(early->pixels(), late->pixels(), early->size()) == 0;
	assert_equal(false, identical, "late thumbnail differs from early (decoder flushed after seek)");
}

// A media seek can only land on a key frame, so the caller has to say which side of the
// requested time it may land on. avformat_seek_file clears AVSEEK_FLAG_BACKWARD and instead
// derives the direction from the min/max window, so the window centred on the target that
// this used to pass always resolved to the key frame *after* the request. Nothing can decode
// backwards from there, so the scrubber preview - and playback resuming at a saved position -
// silently skipped up to a whole GOP of content. indy.mp4 has ~10s between key frames, which
// is far wider than the tolerance here.
static void should_seek_to_the_frame_at_the_requested_time()
{
	const auto load_path = test_files_folder.combine_file("indy.mp4");

	av_format_decoder decoder;
	assert_equal(true, decoder.open(load_path), "open indy.mp4");
	decoder.init_streams(-1, -1, false, false, false);
	assert_equal(true, decoder.has_video(), "indy.mp4 has video");

	constexpr sizei max_dim(256, 256);
	const auto start = decoder.start_time();
	const auto len = decoder.end_time() - start;
	assert_equal(true, len > 60.0, "indy.mp4 is long enough to span several key frames");

	constexpr double numerator = 60;
	constexpr double denominator = 100;
	const auto wanted = start + floor(numerator * len / denominator);

	ui::surface_ptr s;
	assert_equal(true, decoder.extract_seek_frame(s, max_dim, numerator, denominator), "seek frame decoded");
	assert_equal(true, is_valid(s), "seek frame valid");
	assert_equal(true, fabs(s->time() - wanted) < 1.0,
	             std::format("decoded frame is at the requested time (wanted {}, got {})", wanted, s->time()));
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

static void should_extract_dv_datetime()
{
	// Build a minimal raw DV frame (one DIF sequence) carrying the VAUX
	// recording-date (0x62) and recording-time (0x63) packs at the offsets the
	// DV format places them (VAUX DIF block 3 of the sequence).
	std::vector<uint8_t> frame(12000, 0);

	auto* const date_pack = &frame[80 * 3 + 13];
	date_pack[0] = 0x62; // VAUX recording date pack id
	date_pack[1] = 0xff; // timezone unknown
	date_pack[2] = 0xc0 | (1 << 4) | 5; // day 15 (reserved bits set)
	date_pack[3] = (0 << 4) | 7; // month 07
	date_pack[4] = (0 << 4) | 3; // year 03 -> 2003

	auto* const time_pack = &frame[80 * 3 + 18];
	time_pack[0] = 0x63; // VAUX recording time pack id
	time_pack[1] = 0xff; // frames unknown
	time_pack[2] = (4 << 4) | 5; // 45 seconds
	time_pack[3] = (3 << 4) | 0; // 30 minutes
	time_pack[4] = (1 << 4) | 4; // 14 hours

	const auto actual = dv_extract_rec_datetime(frame.data(), frame.size());
	assert_equal(df::date_t(2003, 7, 15, 14, 30, 45), actual, "dv rec datetime");

	// A frame without recording packs must yield an invalid (absent) date.
	const std::vector<uint8_t> empty_frame(12000, 0);
	assert_equal(false, dv_extract_rec_datetime(empty_frame.data(), empty_frame.size()).is_valid(),
	             "dv no packs");
}

static void should_correct_pts()
{
	// av_pts_correction takes FFmpeg's AVFrame::best_effort_timestamp (falling back to pts
	// then pkt_dts) and guarantees a strictly increasing result so the presenter can always
	// order frames. AV_NOPTS_VALUE is INT64_MIN; mirror it here so the test does not need to
	// pull in the libav* headers.
	constexpr int64_t nopts = std::numeric_limits<int64_t>::min();

	// Clean, monotonic timestamps pass straight through.
	{
		av_pts_correction pc;
		assert_equal(0, static_cast<int>(pc.guess(0, 0, 0, 100)), "monotonic 0");
		assert_equal(100, static_cast<int>(pc.guess(100, 100, 100, 100)), "monotonic 100");
		assert_equal(200, static_cast<int>(pc.guess(200, 200, 200, 100)), "monotonic 200");
	}

	// best_effort_timestamp wins over pts and pkt_dts; those are only consulted when the
	// decoder had nothing to publish.
	{
		av_pts_correction pc;
		assert_equal(500, static_cast<int>(pc.guess(500, 700, 900, 100)), "best effort preferred");
		assert_equal(700, static_cast<int>(pc.guess(nopts, 700, 900, 100)), "falls back to pts");
		assert_equal(900, static_cast<int>(pc.guess(nopts, nopts, 900, 100)), "falls back to dts");
	}

	// A duplicated timestamp must not be returned verbatim - that would make the
	// presenter treat the frame as "not newer" and stall - so it is advanced by
	// one frame duration instead.
	{
		av_pts_correction pc;
		assert_equal(0, static_cast<int>(pc.guess(0, nopts, nopts, 100)), "dup first");
		assert_equal(100, static_cast<int>(pc.guess(100, nopts, nopts, 100)), "dup second");
		assert_equal(200, static_cast<int>(pc.guess(100, nopts, nopts, 100)), "dup advanced by duration");
	}

	// With no timestamps at all (raw / MJPEG streams) and no reported duration,
	// the timeline still advances using the cadence learned from earlier frames.
	{
		av_pts_correction pc;
		assert_equal(0, static_cast<int>(pc.guess(0, nopts, nopts, 0)), "nopts first");
		assert_equal(40, static_cast<int>(pc.guess(40, nopts, nopts, 0)), "nopts learn cadence");
		assert_equal(80, static_cast<int>(pc.guess(nopts, nopts, nopts, 0)), "nopts synth 1");
		assert_equal(120, static_cast<int>(pc.guess(nopts, nopts, nopts, 0)), "nopts synth 2");
	}

	// The learned cadence is the smallest step seen, not the most recent one: a gap in a
	// damaged stream must not become the synthetic step and run the timeline away.
	{
		av_pts_correction pc;
		assert_equal(0, static_cast<int>(pc.guess(0, nopts, nopts, 0)), "gap first");
		assert_equal(40, static_cast<int>(pc.guess(40, nopts, nopts, 0)), "gap learn cadence");
		assert_equal(9000, static_cast<int>(pc.guess(9000, nopts, nopts, 0)), "gap jump");
		assert_equal(9040, static_cast<int>(pc.guess(nopts, nopts, nopts, 0)), "synth uses smallest step");
	}

	// Invariant: however messy the timestamps (duplicates, backward jumps), the output is
	// always strictly increasing.
	{
		av_pts_correction pc;
		const int64_t messy[] = {0, 200, 100, 100, 400, 300, 500};
		auto prev = nopts;
		for (const auto p : messy)
		{
			const auto t = pc.guess(p, nopts, nopts, 50);
			if (prev != nopts) assert_equal(true, t > prev, "strictly increasing");
			prev = t;
		}
	}
}

// A clip with no audio track has no device clock, so it is timed off the wall clock and the
// stream's own end is the only thing that separates "played out" from "decode fell behind". That
// end-of-stream marker carries no media timestamp, so leaving it at the head of the frame queue
// made front_time() report zero and every distance comparison refuse to look past it - the marker
// was never consumed and the clip could only end on the two-second hard fallback.
static void should_end_a_silent_clip_at_the_stream_end()
{
	df::file_path silent_path;

	for (const auto* const name : {"anamorphic.mp4", "gizmo.mp4", "tagged.mkv", "tagged.webm"})
	{
		const auto candidate = test_files_folder.combine_file(name);

		av_format_decoder probe;
		if (!probe.open(candidate)) continue;
		probe.init_streams(-1, -1, false, false, false);

		if (probe.has_video() && !probe.has_audio())
		{
			silent_path = candidate;
			break;
		}
	}

	assert_equal(false, silent_path.is_empty(), "a video-only test file is available");

	const auto ses = make_test_session();
	assert_equal(true, ses->open(silent_path, files::file_type_from_name(silent_path), 0.0, true, -1, -1, false,
	                             false, false), "session opened");
	assert_equal(true, ses->is_playing(), "session auto-plays");

	const auto media_end = ses->info().end;
	assert_equal(true, media_end > 0.0, "the test clip declares a duration");

	auto now = df::now();
	assert_equal(false, ses->has_ended(now), "a freshly opened clip has not ended");

	// Drive the demux, decode and present work the player threads normally own.
	platform::thread_event video_event(false, false);
	platform::thread_event audio_event(false, false);
	platform::thread_event read_event(false, false);

	auto ended_at = -1.0;

	for (auto i = 0; i < 4000 && ended_at < 0.0; ++i)
	{
		ses->process_io(video_event, audio_event);
		ses->process_video(read_event);
		now += 0.02;
		ses->update_for_present(now);
		if (ses->has_ended(now)) ended_at = ses->pos(now);
	}

	assert_equal(true, ended_at >= 0.0, "the clip ends");
	assert_equal(true, ended_at < media_end + 1.0,
	             std::format("ends on the stream end, not the 2s fallback (end {:.2f}, ended at {:.2f})",
	                         media_end, ended_at));

	ses->close(false);
}

// Scrubbing sets the wall clock from the position the user asked for, then the audio device
// re-anchors it to the first sample it is handed. Those two have to agree: if the audio timeline
// lands somewhere other than the sought position, pos() jumps when the device starts and the view
// sits frozen until the clock catches back up to the frame already on screen.
// A seek can only land on a key sample. gizmo.mp4 carries a single video key frame, so seeking
// anywhere in it puts the demuxer back at the start. update_for_present settles the video forward
// onto the position asked for; audio has no such step, so without trimming it the buffer - and the
// device clock anchored to it - starts at the key sample and the view sits frozen on the settled
// frame until the clock catches back up. That freeze is what a scrub used to produce.
static void should_land_audio_and_video_on_the_sought_position()
{
	const auto load_path = test_files_folder.combine_file("gizmo.mp4");

	const auto ses = make_test_session();
	assert_equal(true, ses->open(load_path, files::file_type_from_name(load_path), 0.0, true, -1, -1, false,
	                             false, false), "session opened");

	const auto media_end = ses->info().end;
	assert_equal(true, ses->info().has_audio && ses->info().has_video, "gizmo.mp4 has both streams");

	constexpr auto wanted = 3.0;
	assert_equal(true, media_end > wanted + 1.0, "the clip is long enough to seek into");

	ses->seek(wanted, true);

	audio_info_t fmt;
	fmt.channel_layout = av_get_def_channel_layout(2);
	fmt.sample_fmt = prop::audio_sample_t::signed_16bit;
	fmt.sample_rate = 48000;

	audio_buffer playback_buffer;
	audio_buffer vis_buffer;
	playback_buffer.init(fmt);
	vis_buffer.init(fmt);

	platform::thread_event video_event(false, false);
	platform::thread_event audio_event(false, false);
	platform::thread_event read_event(false, false);

	auto now = df::now();

	for (auto i = 0; i < 200; ++i)
	{
		ses->process_io(video_event, audio_event);
		ses->process_video(read_event);
		ses->process_audio(playback_buffer, vis_buffer, read_event);
		now += 0.02;
		ses->update_for_present(now);
	}

	assert_equal(false, playback_buffer.is_empty(), "audio decoded after the seek");

	const auto audio_at = playback_buffer.start_time();
	const auto video_at = ses->time();

	assert_equal(true, fabs(audio_at - wanted) < 0.5,
	             std::format("audio lands on the sought position (wanted {:.2f}, got {:.2f})", wanted, audio_at));
	assert_equal(true, fabs(video_at - wanted) < 0.5,
	             std::format("video lands on the sought position (wanted {:.2f}, got {:.2f})", wanted, video_at));

	ses->close(false);
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
	assert_equal("147 \"ACE OF ACES+    \" PRG", contents[0].line, "d64", file_name);
}

static void should_detect_tiff_by_version()
{
	// The byte-order mark alone is shared with plenty of non-TIFF files, so the
	// version word decides: 42 is classic TIFF and 43 is BigTIFF.
	constexpr uint8_t little_endian_42[] = {'I', 'I', 42, 0, 8, 0, 0, 0};
	constexpr uint8_t big_endian_42[] = {'M', 'M', 0, 42, 0, 0, 0, 8};
	constexpr uint8_t big_endian_43[] = {'M', 'M', 0, 43, 0, 8, 0, 0};
	constexpr uint8_t not_tiff[] = {'I', 'I', 'B', 'M', 0, 0, 0, 0};

	assert_equal(true, files::detect_format({little_endian_42, std::size(little_endian_42)}) ==
	             detected_format::TIFF, "little endian tiff");
	assert_equal(true, files::detect_format({big_endian_42, std::size(big_endian_42)}) ==
	             detected_format::TIFF, "big endian tiff");
	assert_equal(true, files::detect_format({big_endian_43, std::size(big_endian_43)}) ==
	             detected_format::TIFF, "big endian bigtiff");
	assert_equal(true, files::detect_format({not_tiff, std::size(not_tiff)}) ==
	             detected_format::Unknown, "not tiff");
}

static void should_scan_and_load_bitmap_psd()
{
	// A minimal uncompressed 1-bit-per-pixel bitmap-mode psd. Photoshop stores
	// bitmap mode inverted, so a set bit is black and a clear bit is white.
	constexpr uint8_t bitmap_psd[] = {
		'8', 'B', 'P', 'S', 0, 1, // signature and version
		0, 0, 0, 0, 0, 0, // reserved
		0, 1, // channels
		0, 0, 0, 2, // rows
		0, 0, 0, 16, // columns
		0, 1, // depth
		0, 0, // mode - bitmap
		0, 0, 0, 0, // colour mode data length
		0, 0, 0, 0, // image resource length
		0, 0, 0, 0, // layer and mask length
		0, 0, // compression - none
		0b1010'1010, 0b0000'1111, // row 0
		0b0000'0000, 0b1111'1111, // row 1
	};

	mem_read_stream scan_stream({bitmap_psd, std::size(bitmap_psd)});
	const auto scanned = scan_photo(scan_stream);

	assert_equal(true, scanned.success, "bitmap psd scanned");
	assert_equal(16u, scanned.width, "bitmap psd width");
	assert_equal(2u, scanned.height, "bitmap psd height");
	assert_equal("mono"_c, scanned.pixel_format, "bitmap psd pixel format");

	mem_read_stream load_stream({bitmap_psd, std::size(bitmap_psd)});
	const auto surface = load_psd(load_stream);

	assert_equal(true, is_valid(surface), "bitmap psd loaded");
	assert_equal(16, static_cast<int>(surface->width()), "bitmap psd surface width");
	assert_equal(2, static_cast<int>(surface->height()), "bitmap psd surface height");

	const auto* const row0 = std::bit_cast<const uint32_t*>(surface->pixels_line(0));
	const auto* const row1 = std::bit_cast<const uint32_t*>(surface->pixels_line(1));

	constexpr uint32_t black = 0x000000;
	constexpr uint32_t white = 0xFFFFFF;
	const auto rgb = [](const uint32_t pixel) { return static_cast<uint32_t>(pixel & 0xFFFFFF); };

	assert_equal(black, rgb(row0[0]), "bitmap psd 0,0 is black");
	assert_equal(white, rgb(row0[1]), "bitmap psd 1,0 is white");
	assert_equal(white, rgb(row0[8]), "bitmap psd 8,0 is white");
	assert_equal(black, rgb(row0[12]), "bitmap psd 12,0 is black");
	assert_equal(white, rgb(row1[0]), "bitmap psd 0,1 is white");
	assert_equal(black, rgb(row1[15]), "bitmap psd 15,1 is black");
}

static void should_scan_archive()
{
	constexpr auto file_name = "benchmarks.zip";
	const auto load_path = test_files_folder.combine_file(file_name);
	const auto contents = files::list_archive(load_path);

	assert_equal(2_z, contents.size(), "archive", file_name);
	assert_equal("PXL_20240404_074316577.jpg", contents[0].filename, "archive", file_name);
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
	constexpr auto file_name = "cmyk.JPG";

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

static void should_select_slavic_plural_forms()
{
	// Czech (and Polish, Russian, Ukrainian) declare a third plural form:
	// msgstr[2]. load_po must capture it, and plural_form must select it for
	// "many" counts while keeping the binary behavior for other languages.
	const auto path = _temps.next_path(".po");

	{
		std::ofstream fs(platform::to_file_system_path(path));
		fs << "msgid \"one apple\"\n";
		fs << "msgid_plural \"{count} apples\"\n";
		fs << "msgstr[0] \"jedno jablko\"\n";
		fs << "msgstr[1] \"{count} jablka\"\n";
		fs << "msgstr[2] \"{count} jablek\"\n";
	}

	const auto po_entries = load_po(path);

	assert_equal(1, static_cast<int>(po_entries.size()), "entry count");
	assert_equal("jedno jablko", po_entries.front().str, "msgstr[0]");
	assert_equal("{count} jablka", po_entries.front().str_plural, "msgstr[1]");
	assert_equal(1, static_cast<int>(po_entries.front().str_extra.size()), "extra form count");
	assert_equal("{count} jablek", po_entries.front().str_extra.front(), "msgstr[2] captured");

	// Czech uses three forms: one (1), few (2-4), many (0, 5+, ...).
	app_text_t cs;
	cs.load_lang("cs.po", po_entries);
	assert_equal(0, cs.plural_form(1), "cs form for 1");
	assert_equal(1, cs.plural_form(2), "cs form for 2");
	assert_equal(1, cs.plural_form(4), "cs form for 4");
	assert_equal(2, cs.plural_form(5), "cs form for 5");
	assert_equal(2, cs.plural_form(11), "cs form for 11");

	// Russian shares three forms but its form 0 also covers 21, 31, ...; those
	// are clamped to the plural form so the literal-"1" singular is never reused.
	app_text_t ru;
	ru.load_lang("ru.po", po_entries);
	assert_equal(0, ru.plural_form(1), "ru form for 1");
	assert_equal(1, ru.plural_form(2), "ru form for 2");
	assert_equal(2, ru.plural_form(5), "ru form for 5");
	assert_equal(1, ru.plural_form(21), "ru form for 21 clamped");

	// Unlisted languages keep the binary one/plural behavior.
	app_text_t de;
	de.load_lang("de.po", po_entries);
	assert_equal(0, de.plural_form(1), "de form for 1");
	assert_equal(1, de.plural_form(2), "de form for 2");
	assert_equal(1, de.plural_form(5), "de form for 5");

	cs.title_item_count_fmt.extra_forms.emplace_back("{count} polozek");
	cs.clear();
	assert_equal(1, cs.plural_form(5), "clear restores binary plural rule");
	assert_equal(0, static_cast<int>(cs.title_item_count_fmt.extra_forms.size()), "clear drops extra plural forms");
}

void register_tests3(view_state& state, test_registry& tests)
{
	//
	// Scan Metadata
	//
	tests.add("Should scan jpg metadata"s, should_scan_jpeg);
	tests.add("Should scan avi metadata"s, should_scan_avi);
	tests.add("Should extract dv datetime"s, should_extract_dv_datetime);
	tests.add("Should correct pts"s, should_correct_pts);
	tests.add("Should scan mov metadata"s, should_scan_mov);
	tests.add("Should scan mkv metadata"s, [] { should_scan_matroska("tagged.mkv", "rawvideo"); });
	tests.add("Should scan webm metadata"s, [] { should_scan_matroska("tagged.webm", "vp8"); });
	tests.add("Should scan mp3 metadata"s, should_scan_mp3);
	tests.add("Should scan mp4 metadata"s, should_scan_mp4);
	tests.add("Issue #78: Should apply video aspect ratio"s, should_apply_video_aspect_ratio);
	tests.add("Should flush decoder on thumbnail seek"s, should_flush_decoder_on_thumbnail_seek);
	tests.add("Should seek to the frame at the requested time"s, should_seek_to_the_frame_at_the_requested_time);
	tests.add("Should end a silent clip at the stream end"s, should_end_a_silent_clip_at_the_stream_end);
	tests.add("Should land audio and video on the sought position"s,
	          should_land_audio_and_video_on_the_sought_position);
	tests.add("Issue #3: Should not resurrect container tags"s, should_not_resurrect_container_tags);
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

	//
	// Commodore
	//
	tests.add("Should scan d64"s, should_scan_d64);
	tests.add("Should detect tiff by version"s, should_detect_tiff_by_version);
	tests.add("Should scan and load bitmap psd"s, should_scan_and_load_bitmap_psd);

	//
	// Archive
	//
	tests.add("Should scan archive"s, should_scan_archive);

	//
	// Other
	//
	tests.add("Should parse facebook Json"s, should_parse_facebook_json);
	tests.add("Should load po"s, should_load_po);
	tests.add("Should select Slavic plural forms"s, should_select_slavic_plural_forms);
}
