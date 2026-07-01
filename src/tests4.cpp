// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Media modification tests. Verifies correct updating of metadata,
// ratings, tags, locations, and image transformations (rotate, resize, save).

#include "pch.h"
#include "test_utils.h"

static str::cached make_unique_tags(tag_set tags1, const tag_set& tags2)
{
	tags1.add(tags2);
	tags1.make_unique();
	return str::cache(tags1.to_string());
}

static void should_save(const std::string_view ext, const bool should_support_metadata)
{
	const auto save_path = _temps.next_path(ext);
	const auto load_path = test_files_folder.combine_file("Test.jpg");

	files ff;
	const image_edits color;
	ff.update(load_path, save_path, {}, color, {}, false, {});

	const auto expected = extract_properties(load_path);
	const auto actual = extract_properties(save_path);

	assert_equal(expected->width, actual->width);
	assert_equal(expected->height, actual->height);

	if (should_support_metadata)
	{
		assert_metadata(*expected, *actual, save_path.name());
	}
}

static void should_update_rating(const std::string_view name)
{
	files ff;

	const auto ext = name.substr(df::find_ext(name));
	const auto save_path = _temps.next_path(ext);
	const auto load_path = test_files_folder.combine_file(name);

	metadata_edits edits1;
	edits1.rating = 3;

	ff.update(load_path, save_path, edits1, {}, {}, false, {});

	{
		const auto actual_scanned = ff_scan_file(ff, save_path, detect_xmp_sidecar(save_path));
		const auto ps = actual_scanned.to_props();
		assert_equal(3, ps->rating, "rating");
	}

	metadata_edits edits2;
	edits2.remove_rating = true;
	ff.update(save_path, edits2, {}, {}, false, {});

	{
		const auto actual_scanned = ff_scan_file(ff, save_path, detect_xmp_sidecar(save_path));
		const auto ps = actual_scanned.to_props();
		assert_equal(0, ps->rating, "rating");
	}
}

static void should_update_exif_rating()
{
	const auto load_path = test_files_folder.combine_file("exif-rating.jpg");
	const auto save_path = _temps.next_path(".jpg");

	files ff;
	metadata_edits edits1;
	edits1.rating = 3;

	ff.update(load_path, save_path, edits1, {}, {}, false, {});

	{
		const auto actual_scanned = ff_scan_file(ff, save_path);
		const auto ps = actual_scanned.to_props();
		assert_equal(3, ps->rating, "to_props");

		const auto actual_exif = extract_properties(save_path, metadata_type::EXIF);
		const auto actual_iptc = extract_properties(save_path, metadata_type::IPTC);
		const auto actual_xmp = extract_properties(save_path, metadata_type::XMP);

		assert_equal(3, actual_xmp->rating, "XMP");
		assert_equal(3, actual_exif->rating, "exif");
	}

	metadata_edits edits2;
	edits2.remove_rating = true;
	ff.update(save_path, edits2, {}, {}, false, {});

	{
		const auto actual_scanned = ff_scan_file(ff, save_path);
		assert_equal(0, actual_scanned.to_props()->rating);

		const auto actual_exif = extract_properties(save_path, metadata_type::EXIF);
		const auto actual_iptc = extract_properties(save_path, metadata_type::IPTC);
		const auto actual_xmp = extract_properties(save_path, metadata_type::XMP);

		assert_equal(0, actual_xmp->rating, "XMP");
		assert_equal(0, actual_exif->rating, "exif");
		assert_equal(0, actual_iptc->rating, "IPC");
	}
}

static void should_update_formatted_text()
{
	const auto load_path = test_files_folder.combine_file("exif-rating.jpg");
	const auto save_path = _temps.next_path(".jpg");
	constexpr auto desc_text = "a\tb\nc";

	files ff;
	metadata_edits edits1;
	edits1.description = desc_text;

	ff.update(load_path, save_path, edits1, {}, {}, false, {});

	{
		const auto actual_scanned = ff_scan_file(ff, save_path);
		const auto ps = actual_scanned.to_props();
		assert_equal(desc_text, ps->description, "to_props");

		const auto actual_exif = extract_properties(save_path, metadata_type::EXIF);
		const auto actual_iptc = extract_properties(save_path, metadata_type::IPTC);
		const auto actual_xmp = extract_properties(save_path, metadata_type::XMP);

		assert_equal(desc_text, actual_xmp->description, "XMP");
		assert_equal(desc_text, actual_exif->description, "exif");
		assert_equal(desc_text, actual_iptc->description, "IPC");
	}
}

static void should_update_metadata(const std::string_view name)
{
	files ff;

	const auto ext = name.substr(df::find_ext(name));
	const auto save_path = _temps.next_path(ext);
	const auto load_path = test_files_folder.combine_file(name);

	const auto tags_to_add = tag_set("key1 key2 key3");

	metadata_edits metadata_edits;
	metadata_edits.add_tags = tags_to_add;
	metadata_edits.copyright_notice = "Copyright xx";
	metadata_edits.rating = 3;
	metadata_edits.title = "Title xx";
	metadata_edits.description = "Description xx";

	ff.update(load_path, save_path, metadata_edits, {}, {}, false, {});

	const auto sr_expected = ff_scan_file(ff, load_path, detect_xmp_sidecar(load_path));
	const auto sr_actual = ff_scan_file(ff, save_path, detect_xmp_sidecar(save_path));

	const auto expected = sr_expected.to_props();
	const auto actual = sr_actual.to_props();

	expected->title = "Title xx"_c;
	expected->copyright_notice = "Copyright xx"_c;
	expected->description = "Description xx"_c;
	expected->rating = 3;
	expected->tags = make_unique_tags(tag_set(expected->tags), tags_to_add);

	assert_metadata(*expected, *actual);
}

static void should_add_remove_tags(const std::string_view name)
{
	const auto ext = name.substr(df::find_ext(name));
	const auto save_path = _temps.next_path(ext);
	const auto load_path = test_files_folder.combine_file(name);

	const auto tags_to_add = tag_set("Максим yyyy zzzz");
	const auto tags_to_remove = tag_set("Максим zzzz");

	metadata_edits edits1;
	edits1.add_tags = tags_to_add;

	files ff;
	ff.update(load_path, save_path, edits1, {}, {}, false, {});

	const auto sr_expected = ff_scan_file(ff, load_path, detect_xmp_sidecar(load_path));
	const auto sr_actual = ff_scan_file(ff, save_path, detect_xmp_sidecar(save_path));

	const auto expected = sr_expected.to_props();
	const auto actual = sr_actual.to_props();

	expected->tags = make_unique_tags(tag_set(expected->tags), tags_to_add);

	assert_metadata(*expected, *actual, "added");

	metadata_edits edits2;
	edits2.remove_tags = tags_to_remove;
	ff.update(save_path, edits2, {}, {}, false, {});

	tag_set expected_tags(expected->tags);
	expected_tags.remove(tags_to_remove);
	expected->tags = make_unique_tags(expected_tags, {});

	const auto sr_actual2 = ff_scan_file(ff, save_path, detect_xmp_sidecar(save_path));
	const auto actual2 = sr_actual2.to_props();
	assert_metadata(*expected, *actual2, "removed");
}

static void should_remove_shell_written_tags()
{
	// Copy a test JPEG to temp so we can modify it
	const auto load_path = test_files_folder.combine_file("Test.jpg");
	const auto save_path = _temps.next_path(".jpg");

	files ff;
	ff.update(load_path, save_path, {}, {}, {}, false, {});

	// Write tags via Windows shell properties (simulates Windows Explorer tag editing)
	const std::vector<std::string> shell_tags = {"ShellTag1", "ShellTag2", "ShellTag3"};
	const auto write_ok = platform::write_shell_tags(save_path, shell_tags);
	assert_equal(true, write_ok, "write_shell_tags");

	// Verify shell tags are readable via shell
	{
		const auto shell_meta = platform::read_shell_metadata(save_path);
		assert_equal(3u, static_cast<unsigned>(shell_meta.tags.size()), "shell read count");
	}

	// Verify Diffractor can read the shell-written tags
	{
		const auto sr = ff_scan_file(ff, save_path);
		const auto ps = sr.to_props();
		const tag_set scanned_tags(ps->tags);
		assert_equal(true, scanned_tags.size() >= 3u, "diffractor read shell tags");
	}

	// Remove one tag via Diffractor
	{
		metadata_edits edits;
		edits.remove_tags = tag_set("ShellTag2");
		ff.update(save_path, edits, {}, {}, false, {});
	}

	// Verify the tag is removed from Diffractor's perspective
	{
		const auto sr = ff_scan_file(ff, save_path);
		const auto ps = sr.to_props();
		const tag_set remaining(ps->tags);

		// ShellTag2 should be gone
		tag_set check_removed("ShellTag2");
		tag_set test_set = remaining;
		const auto size_before = test_set.size();
		test_set.remove(check_removed);
		assert_equal(size_before, test_set.size(), "ShellTag2 should not be present");
	}

	// Verify tags are also removed from shell perspective (IPTC/EXIF)
	{
		const auto shell_meta = platform::read_shell_metadata(save_path);
		bool found_removed = false;
		for (const auto& t : shell_meta.tags)
		{
			if (str::icmp(t, "ShellTag2") == 0) found_removed = true;
		}
		assert_equal(false, found_removed, "ShellTag2 removed from shell");
	}

	// Remove all remaining tags
	{
		metadata_edits edits;
		edits.remove_tags = tag_set("ShellTag1 ShellTag3");
		ff.update(save_path, edits, {}, {}, false, {});
	}

	// Verify all tags are gone
	{
		const auto sr = ff_scan_file(ff, save_path);
		const auto ps = sr.to_props();
		// Only original tags from Test.jpg should remain (if any)
		const auto shell_meta = platform::read_shell_metadata(save_path);
		bool found_any_shell = false;
		for (const auto& t : shell_meta.tags)
		{
			if (str::icmp(t, "ShellTag1") == 0 || str::icmp(t, "ShellTag2") == 0 || str::icmp(t, "ShellTag3") ==
				0)
				found_any_shell = true;
		}
		assert_equal(false, found_any_shell, "all shell tags removed");
	}
}

static void should_update_location(const std::string_view name)
{
	files ff;

	const auto ext = name.substr(df::find_ext(name));
	const auto save_path = _temps.next_path(ext);
	const auto load_path = test_files_folder.combine_file(name);
	const auto coordinate = gps_coordinate(40.71417, -74.00611);

	metadata_edits metadata_edits;
	metadata_edits.location_coordinate = coordinate;
	metadata_edits.location_place = "Big Apple"_c;
	metadata_edits.location_state = "New York"_c;
	metadata_edits.location_country = "USA"_c;

	ff.update(load_path, save_path, metadata_edits, {}, {}, false, {});

	const auto sr_expected = ff_scan_file(ff, load_path, detect_xmp_sidecar(load_path));
	const auto sr_actual = ff_scan_file(ff, save_path, detect_xmp_sidecar(save_path));

	const auto expected = sr_expected.to_props();
	const auto actual = sr_actual.to_props();

	expected->coordinate = coordinate;
	expected->location_place = "Big Apple"_c;
	expected->location_state = "New York"_c;
	expected->location_country = "USA"_c;

	assert_metadata(*expected, *actual);
}

static void should_update_gps_in_exif()
{
	const auto save_path = _temps.next_path(".jpg");
	const auto load_path = test_files_folder.combine_file("IMG_9340.jpg");
	const auto coordinate = gps_coordinate(40.71417, -74.00611);

	metadata_edits metadata_edits;
	metadata_edits.location_coordinate = coordinate;
	metadata_edits.location_place = "Big Apple"_c;
	metadata_edits.location_state = "New York"_c;
	metadata_edits.location_country = "USA"_c;

	files ff;
	ff.update(load_path, save_path, metadata_edits, {}, {}, false, {});

	const auto actual_exif = extract_properties(save_path, metadata_type::EXIF);
	const auto actual_iptc = extract_properties(save_path, metadata_type::IPTC);
	const auto actual_xmp = extract_properties(save_path, metadata_type::XMP);

	assert_equal("Big Apple", actual_iptc->location_place, "IPTC");
	assert_equal("New York", actual_iptc->location_state, "IPTC");
	assert_equal("USA", actual_iptc->location_country, "IPTC");
	assert_equal({40.71417, -74.00611}, actual_exif->coordinate, "EXIF");

	assert_equal("Big Apple", actual_xmp->location_place, "XMP");
	assert_equal("New York", actual_xmp->location_state, "XMP");
	assert_equal("USA", actual_xmp->location_country, "XMP");
}

static void should_handle_international_characters()
{
	const auto save_path = _temps.next_path(".jpg");
	const auto load_path = test_files_folder.combine_file("test.jpg");
	constexpr auto description = "In vollen Zügen genießen";

	tag_set tags;
	tags.add_one("In vollen Zügen genießen");
	tags.add_one("Nældens takvinge");
	tags.add_one("Žižkov");

	const auto test = str::utf16_to_utf8(str::utf8_to_utf16(description));
	assert_equal(description, test);

	metadata_edits edits;
	edits.description = description;
	edits.remove_tags = tag_set("key1 key2 key3");
	edits.add_tags = tags;

	files ff;
	ff.update(load_path, save_path, edits, {}, {}, false, {});

	const auto actual_exif = extract_properties(save_path, metadata_type::EXIF);
	const auto actual_iptc = extract_properties(save_path, metadata_type::IPTC);
	const auto actual_xmp = extract_properties(save_path, metadata_type::XMP);

	assert_equal(description, actual_xmp->description, "XMP");
	assert_equal(description, actual_exif->description, "EXIF");
	assert_equal(description, actual_iptc->description, "IPC");

	const auto expected_tags = tags.to_string();
	assert_equal(expected_tags, actual_xmp->tags, "XMP Tags");
	assert_equal(expected_tags, actual_iptc->tags, "IPTC Tags");

	const auto sr = ff_scan_file(ff, save_path);
	const auto ps = sr.to_props();
	assert_equal(description, ps->description);
	assert_equal(expected_tags, ps->tags, "Tags");
}

// Issue #219 - Korean (Hangul) metadata must survive a write/read round-trip
// through IPTC and XMP the same as other non-Latin scripts. Uses \u escapes so
// the source file stays ASCII-clean.
static void should_handle_korean_characters()
{
	const auto save_path = _temps.next_path(".jpg");
	const auto load_path = test_files_folder.combine_file("test.jpg");
	// "서울에서 찍은 사진" (a photo taken in Seoul)
	constexpr auto description = "\uC11C\uC6B8\uC5D0\uC11C \uCC0D\uC740 \uC0AC\uC9C4";

	tag_set tags;
	tags.add_one("\uAC00\uC871"); // 가족  family
	tags.add_one("\uC5EC\uD589"); // 여행  travel
	tags.add_one("\uC11C\uC6B8"); // 서울  Seoul
	tags.make_unique(); // tags are stored/read back in sorted order

	// UTF-8 <-> UTF-16 round-trip of the Korean text must be lossless.
	const auto test = str::utf16_to_utf8(str::utf8_to_utf16(description));
	assert_equal(description, test);

	metadata_edits edits;
	edits.description = description;
	edits.remove_tags = tag_set("key1 key2 key3"); // test.jpg ships with these
	edits.add_tags = tags;

	files ff;
	ff.update(load_path, save_path, edits, {}, {}, false, {});

	const auto actual_xmp = extract_properties(save_path, metadata_type::XMP);
	const auto actual_iptc = extract_properties(save_path, metadata_type::IPTC);

	assert_equal(description, actual_xmp->description, "XMP");
	assert_equal(description, actual_iptc->description, "IPTC");

	const auto expected_tags = tags.to_string();
	assert_equal(expected_tags, actual_xmp->tags, "XMP Tags");
	assert_equal(expected_tags, actual_iptc->tags, "IPTC Tags");

	// Full scan (the path used by the index) must also read the Korean values.
	const auto sr = ff_scan_file(ff, save_path);
	const auto ps = sr.to_props();
	assert_equal(description, ps->description, "scan description");
	assert_equal(expected_tags, ps->tags, "scan tags");
}

static void should_convert_raw_to_jpeg()
{
	const auto load_path = test_files_folder.combine("raw").combine_file("Screws.CR2");
	const auto save_path = _temps.next_path(".jpg");

	files ff;
	ff.update(load_path, save_path, {}, {}, {}, false, {});

	const auto sr_expected = ff_scan_file(ff, load_path, detect_xmp_sidecar(load_path));
	const auto sr_actual = ff_scan_file(ff, save_path);

	const auto expected = sr_expected.to_props();
	const auto actual = sr_actual.to_props();

	assert_equal(expected->tags, actual->tags, "tags");
	assert_equal(expected->title, actual->title, "title");
	assert_equal(expected->description, actual->description, "description");
	assert_equal(expected->width, actual->width, "width");
	assert_equal(expected->height, actual->height, "height");
}

static void should_rotate()
{
	files ff;

	{
		const auto save_path = _temps.next_path();
		const auto load_path = test_files_folder.combine_file("Test.jpg");
		const auto loaded = ff.load(load_path, false);

		image_edits edits;
		const quadd crop(loaded.i->dimensions());
		edits.crop_bounds(crop.transform(simple_transform::rot_90));

		metadata_edits md_edits;
		md_edits.orientation = ui::orientation::top_left;

		ff.update(load_path, save_path, md_edits, edits, {}, false, {});

		const auto expected = extract_properties(test_files_folder.combine_file("Test90.jpg"));
		const auto actual = extract_properties(save_path);

		assert_equal(expected->width, actual->width, "width");
		assert_equal(expected->height, actual->height, "height");

		assert_metadata(*expected, *actual);
	}

	{
		const auto save_path = _temps.next_path();
		const auto load_path = test_files_folder.combine_file("exif-rotated.jpg");
		const auto loaded = ff.load(load_path, false);

		image_edits edits;
		const quadd crop(loaded.i->dimensions());
		edits.crop_bounds(crop.transform(simple_transform::rot_90));

		metadata_edits md_edits;
		md_edits.orientation = ui::orientation::top_left;

		ff.update(load_path, save_path, md_edits, edits, {}, false, {});

		const auto actual_exif = extract_properties(save_path, metadata_type::EXIF);
		assert_equal(ui::orientation::top_left, actual_exif->orientation, "orientation");
	}

	{
		// PNG
		const auto save_path = _temps.next_path(".png");
		const auto load_path = test_files_folder.combine_file("engine.png");
		const auto loaded = ff.load(load_path, false);

		image_edits edits;
		const quadd crop(loaded.i->dimensions());
		edits.crop_bounds(crop.transform(simple_transform::rot_90));
		ff.update(load_path, save_path, {}, edits, {}, false, {});

		const auto updated = ff.load(save_path, false);
		assert_equal(loaded.i->height(), updated.i->width(), "png width");
		assert_equal(loaded.i->width(), updated.i->height(), "png width");
	}

	{
		// WEBP
		const auto save_path = _temps.next_path(".webp");
		const auto load_path = test_files_folder.combine_file("lake.webp");
		const auto loaded = ff.load(load_path, false);

		image_edits edits;
		const quadd crop(loaded.i->dimensions());
		edits.crop_bounds(crop.transform(simple_transform::rot_90));
		ff.update(load_path, save_path, {}, edits, {}, false, {});

		const auto updated = ff.load(save_path, false);
		assert_equal(false, updated.is_empty(), "webp result empty");
		assert_equal(loaded.i->height(), updated.i->width(), "webp width");
		assert_equal(loaded.i->width(), updated.i->height(), "webp width");
	}
}

static void should_rotate133()
{
	const auto save_path = _temps.next_path();
	const auto load_path = test_files_folder.combine_file("Test.jpg");

	files ff;
	const auto loaded = ff.load(load_path, false);

	const quadd crop(loaded.i->dimensions());
	image_edits edits;
	edits.crop_bounds(crop.rotate(133, crop.center_point()));

	ff.update(load_path, save_path, {}, edits, {}, false, {});

	const auto actual = extract_properties(save_path);
	const auto expected = expected_test_jpg();
	expected->width = 576;
	expected->height = 384;
	assert_metadata(*expected, *actual);
}

static void should_rotate_lossless()
{
	const auto save_path = _temps.next_path();
	const auto load_path = test_files_folder.combine_file("Lossless0.jpg");

	image_edits edits;
	const quadd crop(sizei(640, 480));
	edits.crop_bounds(crop.transform(simple_transform::rot_90));

	files ff;
	ff.update(load_path, save_path, {}, edits, {}, false, {});

	const auto expected = extract_properties(test_files_folder.combine_file("Lossless90.jpg"));
	const auto actual = extract_properties(save_path);

	assert_equal(expected->width, actual->width);
	assert_equal(expected->height, actual->height);
}

static void should_resize()
{
	const auto save_path = _temps.next_path();
	const auto load_path = test_files_folder.combine_file("Test.jpg");

	image_edits edits;
	edits.scale(sizei(200, 150));

	files ff;
	ff.update(load_path, save_path, {}, edits, {}, false, {});

	const auto actual = extract_properties(save_path);

	if (!actual)
	{
		throw test_assert_exception(std::format("Should resize: could not extract properties from {}", save_path.str()));
	}

	assert_equal(200, actual->width);
	assert_equal(133, actual->height);
}

static void should_preserve_orientation()
{
	const auto load_path = test_files_folder.combine_file("exif-rotated.jpg");

	files ff;
	const auto loaded = ff.load(load_path, false);
	const auto surface = loaded.to_surface();
	const auto loaded_orientation = loaded.i->orientation();

	const auto encoded_jpg = ff.surface_to_image(surface, {}, {}, ui::image_format::JPEG);
	assert_equal(loaded_orientation, encoded_jpg->orientation(), "orientation for created jpg");

	const auto encoded_png = ff.surface_to_image(surface, {}, {}, ui::image_format::PNG);
	assert_equal(loaded_orientation, encoded_png->orientation(), "orientation for created png");

	const auto encoded_webp = ff.surface_to_image(surface, {}, {}, ui::image_format::WEBP);
	assert_equal(loaded_orientation, encoded_webp->orientation(), "orientation for created webp");

	const auto loaded_jpg = load_image_file(encoded_jpg->data());
	assert_equal(loaded_orientation, loaded_jpg->orientation(), "orientation for re-loaded jpg");

	const auto loaded_png = load_image_file(encoded_png->data());
	assert_equal(loaded_orientation, loaded_png->orientation(), "orientation for re-loaded png");

	const auto loaded_webp = load_image_file(encoded_webp->data());
	assert_equal(loaded_orientation, loaded_webp->orientation(), "orientation for re-loaded webp");
}

void register_tests4(view_state& state, test_registry& tests)
{
	//
	// Modify media
	//
	constexpr std::string_view common_files[] = {
		"Byzantium.avi",
		"cherrys.psd",
		"Colorblind.mp3",
		"engine.png",
		"Gherkin.CR2",
		"gizmo.mp4",
		"IMG_0096.JPG",
		"cmyk.JPG",
		"ipod.mov",
		"jello.tif",
		"StPauls.MOV",
		"Test.jpg",
		"tuesday.gif",
		"lake.webp",
	};

	for (auto name : common_files)
	{
		tests.add(std::format("Should update metadata {}", name), [name] { should_update_metadata(name); });
		tests.add(std::format("Should update location {}", name), [name] { should_update_location(name); });
		tests.add(std::format("Should update rating {}", name), [name] { should_update_rating(name); });
		tests.add(std::format("Should update tags {}", name), [name] { should_add_remove_tags(name); });
	}

	tests.add("Should update gps in exif"s, should_update_gps_in_exif);
	tests.add("Should handle international characters"s, should_handle_international_characters);
	tests.add("Should handle korean characters"s, should_handle_korean_characters);
	tests.add("Should update exif rating"s, should_update_exif_rating);
	tests.add("Should update formatted description"s, should_update_formatted_text);
	tests.add("Should remove shell written tags"s, should_remove_shell_written_tags);

	//
	// Bitmap Edit
	//
	tests.add("Should preserve orientation"s, should_preserve_orientation);
	tests.add("Should resize"s, should_resize);
	tests.add("Should rotate"s, should_rotate);
	tests.add("Should rotate 133"s, should_rotate133);
	tests.add("Should rotate lossless"s, should_rotate_lossless);
	tests.add("Should save .png"s, [] { should_save(".png", true); });
	tests.add("Should save .jpg"s, [] { should_save(".jpg", true); });
	tests.add("Should save .webp"s, [] { should_save(".webp", true); });
	tests.add("Should convert raw to jpeg"s, should_convert_raw_to_jpeg);
}
