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

static void should_save(const std::u8string_view ext, const bool should_support_metadata)
{
	const auto save_path = _temps.next_path(ext);
	const auto load_path = test_files_folder.combine_file(u8"Test.jpg"sv);

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

static void should_update_rating(const std::u8string_view name)
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
		assert_equal(3, ps->rating, u8"rating"sv);
	}

	metadata_edits edits2;
	edits2.remove_rating = true;
	ff.update(save_path, edits2, {}, {}, false, {});

	{
		const auto actual_scanned = ff_scan_file(ff, save_path, detect_xmp_sidecar(save_path));
		const auto ps = actual_scanned.to_props();
		assert_equal(0, ps->rating, u8"rating"sv);
	}
}

static void should_update_exif_rating()
{
	const auto load_path = test_files_folder.combine_file(u8"exif-rating.jpg"sv);
	const auto save_path = _temps.next_path(u8".jpg"sv);

	files ff;
	metadata_edits edits1;
	edits1.rating = 3;

	ff.update(load_path, save_path, edits1, {}, {}, false, {});

	{
		const auto actual_scanned = ff_scan_file(ff, save_path);
		const auto ps = actual_scanned.to_props();
		assert_equal(3, ps->rating, u8"to_props"sv);

		const auto actual_exif = extract_properties(save_path, metadata_type::EXIF);
		const auto actual_iptc = extract_properties(save_path, metadata_type::IPTC);
		const auto actual_xmp = extract_properties(save_path, metadata_type::XMP);

		assert_equal(3, actual_xmp->rating, u8"XMP"sv);
		assert_equal(3, actual_exif->rating, u8"exif"sv);
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

		assert_equal(0, actual_xmp->rating, u8"XMP"sv);
		assert_equal(0, actual_exif->rating, u8"exif"sv);
		assert_equal(0, actual_iptc->rating, u8"IPC"sv);
	}
}

static void should_update_formatted_text()
{
	const auto load_path = test_files_folder.combine_file(u8"exif-rating.jpg"sv);
	const auto save_path = _temps.next_path(u8".jpg"sv);
	constexpr auto desc_text = u8"a\tb\nc"sv;

	files ff;
	metadata_edits edits1;
	edits1.description = desc_text;

	ff.update(load_path, save_path, edits1, {}, {}, false, {});

	{
		const auto actual_scanned = ff_scan_file(ff, save_path);
		const auto ps = actual_scanned.to_props();
		assert_equal(desc_text, ps->description, u8"to_props"sv);

		const auto actual_exif = extract_properties(save_path, metadata_type::EXIF);
		const auto actual_iptc = extract_properties(save_path, metadata_type::IPTC);
		const auto actual_xmp = extract_properties(save_path, metadata_type::XMP);

		assert_equal(desc_text, actual_xmp->description, u8"XMP"sv);
		assert_equal(desc_text, actual_exif->description, u8"exif"sv);
		assert_equal(desc_text, actual_iptc->description, u8"IPC"sv);
	}
}

static void should_update_metadata(const std::u8string_view name)
{
	files ff;

	const auto ext = name.substr(df::find_ext(name));
	const auto save_path = _temps.next_path(ext);
	const auto load_path = test_files_folder.combine_file(name);

	const auto tags_to_add = tag_set(u8"key1 key2 key3"sv);

	metadata_edits metadata_edits;
	metadata_edits.add_tags = tags_to_add;
	metadata_edits.copyright_notice = u8"Copyright xx"sv;
	metadata_edits.rating = 3;
	metadata_edits.title = u8"Title xx"sv;
	metadata_edits.description = u8"Description xx"sv;

	ff.update(load_path, save_path, metadata_edits, {}, {}, false, {});

	const auto sr_expected = ff_scan_file(ff, load_path, detect_xmp_sidecar(load_path));
	const auto sr_actual = ff_scan_file(ff, save_path, detect_xmp_sidecar(save_path));

	const auto expected = sr_expected.to_props();
	const auto actual = sr_actual.to_props();

	expected->title = u8"Title xx"_c;
	expected->copyright_notice = u8"Copyright xx"_c;
	expected->description = u8"Description xx"_c;
	expected->rating = 3;
	expected->tags = make_unique_tags(tag_set(expected->tags), tags_to_add);

	assert_metadata(*expected, *actual);
}

static void should_add_remove_tags(const std::u8string_view name)
{
	const auto ext = name.substr(df::find_ext(name));
	const auto save_path = _temps.next_path(ext);
	const auto load_path = test_files_folder.combine_file(name);

	const auto tags_to_add = tag_set(u8"Максим yyyy zzzz"sv);
	const auto tags_to_remove = tag_set(u8"Максим zzzz"sv);

	metadata_edits edits1;
	edits1.add_tags = tags_to_add;

	files ff;
	ff.update(load_path, save_path, edits1, {}, {}, false, {});

	const auto sr_expected = ff_scan_file(ff, load_path, detect_xmp_sidecar(load_path));
	const auto sr_actual = ff_scan_file(ff, save_path, detect_xmp_sidecar(save_path));

	const auto expected = sr_expected.to_props();
	const auto actual = sr_actual.to_props();

	expected->tags = make_unique_tags(tag_set(expected->tags), tags_to_add);

	assert_metadata(*expected, *actual, u8"added"sv);

	metadata_edits edits2;
	edits2.remove_tags = tags_to_remove;
	ff.update(save_path, edits2, {}, {}, false, {});

	tag_set expected_tags(expected->tags);
	expected_tags.remove(tags_to_remove);
	expected->tags = make_unique_tags(expected_tags, {});

	const auto sr_actual2 = ff_scan_file(ff, save_path, detect_xmp_sidecar(save_path));
	const auto actual2 = sr_actual2.to_props();
	assert_metadata(*expected, *actual2, u8"removed"sv);
}

static void should_remove_shell_written_tags()
{
	// Copy a test JPEG to temp so we can modify it
	const auto load_path = test_files_folder.combine_file(u8"Test.jpg"sv);
	const auto save_path = _temps.next_path(u8".jpg"sv);

	files ff;
	ff.update(load_path, save_path, {}, {}, {}, false, {});

	// Write tags via Windows shell properties (simulates Windows Explorer tag editing)
	const std::vector<std::u8string> shell_tags = {u8"ShellTag1", u8"ShellTag2", u8"ShellTag3"};
	const auto write_ok = platform::write_shell_tags(save_path, shell_tags);
	assert_equal(true, write_ok, u8"write_shell_tags"sv);

	// Verify shell tags are readable via shell
	{
		const auto shell_meta = platform::read_shell_metadata(save_path);
		assert_equal(3u, static_cast<unsigned>(shell_meta.tags.size()), u8"shell read count"sv);
	}

	// Verify Diffractor can read the shell-written tags
	{
		const auto sr = ff_scan_file(ff, save_path);
		const auto ps = sr.to_props();
		const tag_set scanned_tags(ps->tags);
		assert_equal(true, scanned_tags.size() >= 3u, u8"diffractor read shell tags"sv);
	}

	// Remove one tag via Diffractor
	{
		metadata_edits edits;
		edits.remove_tags = tag_set(u8"ShellTag2"sv);
		ff.update(save_path, edits, {}, {}, false, {});
	}

	// Verify the tag is removed from Diffractor's perspective
	{
		const auto sr = ff_scan_file(ff, save_path);
		const auto ps = sr.to_props();
		const tag_set remaining(ps->tags);

		// ShellTag2 should be gone
		tag_set check_removed(u8"ShellTag2"sv);
		tag_set test_set = remaining;
		const auto size_before = test_set.size();
		test_set.remove(check_removed);
		assert_equal(size_before, test_set.size(), u8"ShellTag2 should not be present"sv);
	}

	// Verify tags are also removed from shell perspective (IPTC/EXIF)
	{
		const auto shell_meta = platform::read_shell_metadata(save_path);
		bool found_removed = false;
		for (const auto& t : shell_meta.tags)
		{
			if (str::icmp(t, u8"ShellTag2") == 0) found_removed = true;
		}
		assert_equal(false, found_removed, u8"ShellTag2 removed from shell"sv);
	}

	// Remove all remaining tags
	{
		metadata_edits edits;
		edits.remove_tags = tag_set(u8"ShellTag1 ShellTag3"sv);
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
			if (str::icmp(t, u8"ShellTag1") == 0 || str::icmp(t, u8"ShellTag2") == 0 || str::icmp(t, u8"ShellTag3") ==
				0)
				found_any_shell = true;
		}
		assert_equal(false, found_any_shell, u8"all shell tags removed"sv);
	}
}

static void should_update_location(const std::u8string_view name)
{
	files ff;

	const auto ext = name.substr(df::find_ext(name));
	const auto save_path = _temps.next_path(ext);
	const auto load_path = test_files_folder.combine_file(name);
	const auto coordinate = gps_coordinate(40.71417, -74.00611);

	metadata_edits metadata_edits;
	metadata_edits.location_coordinate = coordinate;
	metadata_edits.location_place = u8"Big Apple"_c;
	metadata_edits.location_state = u8"New York"_c;
	metadata_edits.location_country = u8"USA"_c;

	ff.update(load_path, save_path, metadata_edits, {}, {}, false, {});

	const auto sr_expected = ff_scan_file(ff, load_path, detect_xmp_sidecar(load_path));
	const auto sr_actual = ff_scan_file(ff, save_path, detect_xmp_sidecar(save_path));

	const auto expected = sr_expected.to_props();
	const auto actual = sr_actual.to_props();

	expected->coordinate = coordinate;
	expected->location_place = u8"Big Apple"_c;
	expected->location_state = u8"New York"_c;
	expected->location_country = u8"USA"_c;

	assert_metadata(*expected, *actual);
}

static void should_update_gps_in_exif()
{
	const auto save_path = _temps.next_path(u8".jpg"sv);
	const auto load_path = test_files_folder.combine_file(u8"IMG_9340.jpg"sv);
	const auto coordinate = gps_coordinate(40.71417, -74.00611);

	metadata_edits metadata_edits;
	metadata_edits.location_coordinate = coordinate;
	metadata_edits.location_place = u8"Big Apple"_c;
	metadata_edits.location_state = u8"New York"_c;
	metadata_edits.location_country = u8"USA"_c;

	files ff;
	ff.update(load_path, save_path, metadata_edits, {}, {}, false, {});

	const auto actual_exif = extract_properties(save_path, metadata_type::EXIF);
	const auto actual_iptc = extract_properties(save_path, metadata_type::IPTC);
	const auto actual_xmp = extract_properties(save_path, metadata_type::XMP);

	assert_equal(u8"Big Apple"sv, actual_iptc->location_place, u8"IPTC"sv);
	assert_equal(u8"New York"sv, actual_iptc->location_state, u8"IPTC"sv);
	assert_equal(u8"USA"sv, actual_iptc->location_country, u8"IPTC"sv);
	assert_equal({40.71417, -74.00611}, actual_exif->coordinate, u8"EXIF"sv);

	assert_equal(u8"Big Apple"sv, actual_xmp->location_place, u8"XMP"sv);
	assert_equal(u8"New York"sv, actual_xmp->location_state, u8"XMP"sv);
	assert_equal(u8"USA"sv, actual_xmp->location_country, u8"XMP"sv);
}

static void should_handle_international_characters()
{
	const auto save_path = _temps.next_path(u8".jpg"sv);
	const auto load_path = test_files_folder.combine_file(u8"test.jpg"sv);
	constexpr auto description = u8"In vollen Zügen genießen"sv;

	tag_set tags;
	tags.add_one(u8"In vollen Zügen genießen"sv);
	tags.add_one(u8"Nældens takvinge"sv);
	tags.add_one(u8"Žižkov"sv);

	const auto test = str::utf16_to_utf8(str::utf8_to_utf16(description));
	assert_equal(description, test);

	metadata_edits edits;
	edits.description = description;
	edits.remove_tags = tag_set(u8"key1 key2 key3"sv);
	edits.add_tags = tags;

	files ff;
	ff.update(load_path, save_path, edits, {}, {}, false, {});

	const auto actual_exif = extract_properties(save_path, metadata_type::EXIF);
	const auto actual_iptc = extract_properties(save_path, metadata_type::IPTC);
	const auto actual_xmp = extract_properties(save_path, metadata_type::XMP);

	assert_equal(description, actual_xmp->description, u8"XMP"sv);
	assert_equal(description, actual_exif->description, u8"EXIF"sv);
	assert_equal(description, actual_iptc->description, u8"IPC"sv);

	const auto expected_tags = tags.to_string();
	assert_equal(expected_tags, actual_xmp->tags, u8"XMP Tags"sv);
	assert_equal(expected_tags, actual_iptc->tags, u8"IPTC Tags"sv);

	const auto sr = ff_scan_file(ff, save_path);
	const auto ps = sr.to_props();
	assert_equal(description, ps->description);
	assert_equal(expected_tags, ps->tags, u8"Tags"sv);
}

static void should_convert_raw_to_jpeg()
{
	const auto load_path = test_files_folder.combine(u8"raw"sv).combine_file(u8"Screws.CR2"sv);
	const auto save_path = _temps.next_path(u8".jpg"sv);

	files ff;
	ff.update(load_path, save_path, {}, {}, {}, false, {});

	const auto sr_expected = ff_scan_file(ff, load_path, detect_xmp_sidecar(load_path));
	const auto sr_actual = ff_scan_file(ff, save_path);

	const auto expected = sr_expected.to_props();
	const auto actual = sr_actual.to_props();

	assert_equal(expected->tags, actual->tags, u8"tags"sv);
	assert_equal(expected->title, actual->title, u8"title"sv);
	assert_equal(expected->description, actual->description, u8"description"sv);
	assert_equal(expected->width, actual->width, u8"width"sv);
	assert_equal(expected->height, actual->height, u8"height"sv);
}

static void should_rotate()
{
	files ff;

	{
		const auto save_path = _temps.next_path();
		const auto load_path = test_files_folder.combine_file(u8"Test.jpg"sv);
		const auto loaded = ff.load(load_path, false);

		image_edits edits;
		const quadd crop(loaded.i->dimensions());
		edits.crop_bounds(crop.transform(simple_transform::rot_90));

		metadata_edits md_edits;
		md_edits.orientation = ui::orientation::top_left;

		ff.update(load_path, save_path, md_edits, edits, {}, false, {});

		const auto expected = extract_properties(test_files_folder.combine_file(u8"Test90.jpg"sv));
		const auto actual = extract_properties(save_path);

		assert_equal(expected->width, actual->width, u8"width"sv);
		assert_equal(expected->height, actual->height, u8"height"sv);

		assert_metadata(*expected, *actual);
	}

	{
		const auto save_path = _temps.next_path();
		const auto load_path = test_files_folder.combine_file(u8"exif-rotated.jpg"sv);
		const auto loaded = ff.load(load_path, false);

		image_edits edits;
		const quadd crop(loaded.i->dimensions());
		edits.crop_bounds(crop.transform(simple_transform::rot_90));

		metadata_edits md_edits;
		md_edits.orientation = ui::orientation::top_left;

		ff.update(load_path, save_path, md_edits, edits, {}, false, {});

		const auto actual_exif = extract_properties(save_path, metadata_type::EXIF);
		assert_equal(ui::orientation::top_left, actual_exif->orientation, u8"orientation"sv);
	}

	{
		// PNG
		const auto save_path = _temps.next_path(u8".png"sv);
		const auto load_path = test_files_folder.combine_file(u8"engine.png"sv);
		const auto loaded = ff.load(load_path, false);

		image_edits edits;
		const quadd crop(loaded.i->dimensions());
		edits.crop_bounds(crop.transform(simple_transform::rot_90));
		ff.update(load_path, save_path, {}, edits, {}, false, {});

		const auto updated = ff.load(save_path, false);
		assert_equal(loaded.i->height(), updated.i->width(), u8"png width"sv);
		assert_equal(loaded.i->width(), updated.i->height(), u8"png width"sv);
	}

	{
		// WEBP
		const auto save_path = _temps.next_path(u8".webp"sv);
		const auto load_path = test_files_folder.combine_file(u8"lake.webp"sv);
		const auto loaded = ff.load(load_path, false);

		image_edits edits;
		const quadd crop(loaded.i->dimensions());
		edits.crop_bounds(crop.transform(simple_transform::rot_90));
		ff.update(load_path, save_path, {}, edits, {}, false, {});

		const auto updated = ff.load(save_path, false);
		assert_equal(false, updated.is_empty(), u8"webp result empty"sv);
		assert_equal(loaded.i->height(), updated.i->width(), u8"webp width"sv);
		assert_equal(loaded.i->width(), updated.i->height(), u8"webp width"sv);
	}
}

static void should_rotate133()
{
	const auto save_path = _temps.next_path();
	const auto load_path = test_files_folder.combine_file(u8"Test.jpg"sv);

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
	const auto load_path = test_files_folder.combine_file(u8"Lossless0.jpg"sv);

	image_edits edits;
	const quadd crop(sizei(640, 480));
	edits.crop_bounds(crop.transform(simple_transform::rot_90));

	files ff;
	ff.update(load_path, save_path, {}, edits, {}, false, {});

	const auto expected = extract_properties(test_files_folder.combine_file(u8"Lossless90.jpg"sv));
	const auto actual = extract_properties(save_path);

	assert_equal(expected->width, actual->width);
	assert_equal(expected->height, actual->height);
}

static void should_resize()
{
	const auto save_path = _temps.next_path();
	const auto load_path = test_files_folder.combine_file(u8"Test.jpg"sv);

	image_edits edits;
	edits.scale(sizei(200, 150));

	files ff;
	ff.update(load_path, save_path, {}, edits, {}, false, {});

	const auto actual = extract_properties(save_path);
	assert_equal(200, actual->width);
	assert_equal(133, actual->height);
}

static void should_preserve_orientation()
{
	const auto load_path = test_files_folder.combine_file(u8"exif-rotated.jpg"sv);

	files ff;
	const auto loaded = ff.load(load_path, false);
	const auto surface = loaded.to_surface();
	const auto loaded_orientation = loaded.i->orientation();

	const auto encoded_jpg = ff.surface_to_image(surface, {}, {}, ui::image_format::JPEG);
	assert_equal(loaded_orientation, encoded_jpg->orientation(), u8"orientation for created jpg"sv);

	const auto encoded_png = ff.surface_to_image(surface, {}, {}, ui::image_format::PNG);
	assert_equal(loaded_orientation, encoded_png->orientation(), u8"orientation for created png"sv);

	const auto encoded_webp = ff.surface_to_image(surface, {}, {}, ui::image_format::WEBP);
	assert_equal(loaded_orientation, encoded_webp->orientation(), u8"orientation for created webp"sv);

	const auto loaded_jpg = load_image_file(encoded_jpg->data());
	assert_equal(loaded_orientation, loaded_jpg->orientation(), u8"orientation for re-loaded jpg"sv);

	const auto loaded_png = load_image_file(encoded_png->data());
	assert_equal(loaded_orientation, loaded_png->orientation(), u8"orientation for re-loaded png"sv);

	const auto loaded_webp = load_image_file(encoded_webp->data());
	assert_equal(loaded_orientation, loaded_webp->orientation(), u8"orientation for re-loaded webp"sv);
}

void register_tests4(view_state& state, test_registry& tests)
{
	//
	// Modify media
	//
	constexpr std::u8string_view common_files[] = {
		u8"Byzantium.avi"sv,
		u8"cherrys.psd"sv,
		u8"Colorblind.mp3"sv,
		u8"engine.png"sv,
		u8"Gherkin.CR2"sv,
		u8"gizmo.mp4"sv,
		u8"IMG_0096.JPG"sv,
		u8"cmyk.JPG"sv,
		u8"ipod.mov"sv,
		u8"jello.tif"sv,
		u8"StPauls.MOV"sv,
		u8"Test.jpg"sv,
		u8"tuesday.gif"sv,
		u8"lake.webp"sv,
	};

	for (auto name : common_files)
	{
		tests.add(str::format(u8"Should update metadata {}"sv, name), [name] { should_update_metadata(name); });
		tests.add(str::format(u8"Should update location {}"sv, name), [name] { should_update_location(name); });
		tests.add(str::format(u8"Should update rating {}"sv, name), [name] { should_update_rating(name); });
		tests.add(str::format(u8"Should update tags {}"sv, name), [name] { should_add_remove_tags(name); });
	}

	tests.add(u8"Should update gps in exif"s, should_update_gps_in_exif);
	tests.add(u8"Should handle international characters"s, should_handle_international_characters);
	tests.add(u8"Should update exif rating"s, should_update_exif_rating);
	tests.add(u8"Should update formatted description"s, should_update_formatted_text);
	tests.add(u8"Should remove shell written tags"s, should_remove_shell_written_tags);

	//
	// Bitmap Edit
	//
	tests.add(u8"Should preserve orientation"s, should_preserve_orientation);
	tests.add(u8"Should resize"s, should_resize);
	tests.add(u8"Should rotate"s, should_rotate);
	tests.add(u8"Should rotate 133"s, should_rotate133);
	tests.add(u8"Should rotate lossless"s, should_rotate_lossless);
	tests.add(u8"Should save .png"s, [] { should_save(u8".png"sv, true); });
	tests.add(u8"Should save .jpg"s, [] { should_save(u8".jpg"sv, true); });
	tests.add(u8"Should save .webp"s, [] { should_save(u8".webp"sv, true); });
	tests.add(u8"Should convert raw to jpeg"s, should_convert_raw_to_jpeg);
}
