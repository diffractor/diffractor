// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Media modification tests. Verifies metadata updates, ratings, tags, locations and
// image transformations (rotate, resize, convert, ICC/XMP round-trip).

#include "pch.h"
#include "test_utils.h"
#include "metadata_xmp.h"
#include "view_edit.h"

#include "webp/decode.h"
#include "webp/encode.h"
#include "webp/mux.h"


static uint32_t read_be32(const uint8_t* p)
{
	return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 |
		static_cast<uint32_t>(p[2]) << 8 | p[3];
}

static df::blob find_xtra_entry(const df::file_path path, const std::string_view wanted_name)
{
	const auto data = df::blob_from_file(path);
	for (size_t pos = 0; pos + 12 <= data.size(); ++pos)
	{
		const auto entry_size = read_be32(data.data() + pos);
		const auto name_size = read_be32(data.data() + pos + 4);
		if (entry_size < 12 || entry_size > data.size() - pos || name_size > entry_size - 12) continue;
		const std::string_view name(reinterpret_cast<const char*>(data.data() + pos + 8), name_size);
		if (name == wanted_name)
		{
			return {
				data.begin() + static_cast<ptrdiff_t>(pos),
				data.begin() + static_cast<ptrdiff_t>(pos + entry_size)
			};
		}
	}
	return {};
}

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
	constexpr image_edits color;
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

static void should_apply_perspective_correction()
{
	const auto source = std::make_shared<ui::surface>();
	source->alloc(16, 12, ui::texture_format::RGB);

	for (auto y = 0; y < 12; ++y)
	{
		for (auto x = 0; x < 16; ++x)
		{
			source->set_pixel(x, y, ui::rgba(x * 16, y * 20, 64));
		}
	}

	image_edits edits;
	edits.perspective_horizontal(0.4);
	edits.perspective_vertical(-0.25);
	const auto corrected = source->transform(edits);

	assert_equal(true, source->dimensions() == corrected->dimensions(), "perspective dimensions");
	assert_equal(true, source->pixel_difference(corrected) == ui::pixel_difference_result::not_equal,
	             "perspective changes pixels");

	edits.crop_bounds(quadd(source->dimensions()));
	const auto valid = edits.perspective_bounds(source->dimensions());
	const auto crop = edits.effective_crop_bounds(source->dimensions());
	const auto inside = [](const quadd& polygon, const pointd point)
	{
		auto sign = 0.0;
		for (auto edge = 0; edge < 4; ++edge)
		{
			const auto start = polygon[edge];
			const auto end = polygon[(edge + 1) % 4];
			const auto cross = (end.X - start.X) * (point.Y - start.Y) -
				(end.Y - start.Y) * (point.X - start.X);
			if (edge == 0) sign = cross;
			else if (sign * cross < -0.0001) return false;
		}
		return true;
	};
	assert_equal(true, crop.actual_extent().Width < source->width(), "perspective crop narrows invalid edges");
	for (auto corner = 0; corner < 4; ++corner)
	{
		assert_equal(true, inside(valid, crop[corner]), "perspective crop corner is valid");
	}

	const auto rotated_bounds = quadd(source->dimensions()).rotate(20, quadd(source->dimensions()).center_point());
	edits.crop_bounds(rotated_bounds);
	const auto rotated_valid = edits.perspective_bounds(source->dimensions());
	const auto rotated_crop = edits.effective_crop_bounds(source->dimensions());
	for (auto corner = 0; corner < 4; ++corner)
	{
		assert_equal(true, inside(rotated_valid, rotated_crop[corner]), "rotated perspective crop corner is valid");
	}

	edits.perspective_horizontal(0);
	edits.perspective_vertical(0);
	edits.crop_bounds({});
	assert_equal(true, source->transform(edits).get() == source.get(), "zero perspective preserves surface");
}

static void should_fit_document_correction_to_controls()
{
	constexpr sizei extent(400, 300);
	constexpr auto horizontal = 0.30;
	constexpr auto vertical = -0.18;

	// A page filling this rectangle once corrected must have been photographed at these corners.
	const quadd page_in_output(60.0, 50.0, 340.0, 250.0);
	std::array<pointd, 4> corners{};

	for (auto index = 0; index < 4; ++index)
	{
		const auto nx = page_in_output[index].X / extent.cx - 0.5;
		const auto ny = page_in_output[index].Y / extent.cy - 0.5;
		const auto denominator = 1.0 + horizontal * nx + vertical * ny;
		corners[index] = {(0.5 + nx / denominator) * extent.cx, (0.5 + ny / denominator) * extent.cy};
	}

	const auto correction = fit_document_correction(corners, extent, ui::orientation::top_left);
	assert_equal(true, static_cast<bool>(correction), "document correction fits");
	assert_equal(true, std::abs(correction.perspective_horizontal - horizontal) < 0.01,
	             "recovers horizontal perspective");
	assert_equal(true, std::abs(correction.perspective_vertical - vertical) < 0.01, "recovers vertical perspective");
	assert_equal(true, std::abs(correction.straighten) < 0.5, "rectified page needs no straighten");

	// The crop stays a rectangle the user can adjust, and needs no clipping because it is already
	// inside the warped image.
	image_edits edits;
	edits.perspective_horizontal(correction.perspective_horizontal);
	edits.perspective_vertical(correction.perspective_vertical);
	edits.crop_bounds(correction.crop);
	assert_equal(true, std::abs(edits.crop_bounds().angle() - correction.straighten) < 0.5,
	             "crop angle matches straighten");

	const auto effective = edits.effective_crop_bounds(extent);

	for (auto corner = 0; corner < 4; ++corner)
	{
		assert_equal(true, correction.crop[corner].dist(page_in_output[corner]) < 2.0, "crop matches the page");
		assert_equal(true, correction.crop[corner].dist(effective[corner]) < 0.5, "crop needs no clipping");
	}
}

static void should_detect_only_clear_document_regions()
{
	const auto document = std::make_shared<ui::surface>();
	document->alloc(160, 120, ui::texture_format::RGB);
	constexpr std::array<pointd, 4> corners = {pointd(24, 18), pointd(140, 12), pointd(132, 108), pointd(18, 100)};
	for (auto y = 0; y < 120; ++y)
	{
		for (auto x = 0; x < 160; ++x)
		{
			bool inside = true;
			for (auto edge = 0; edge < 4; ++edge)
			{
				const auto& a = corners[edge];
				const auto& b = corners[(edge + 1) % 4];
				if ((b.X - a.X) * (y + 0.5 - a.Y) - (b.Y - a.Y) * (x + 0.5 - a.X) < 0)
				{
					inside = false;
					break;
				}
			}
			document->set_pixel(x, y, inside ? ui::rgba(238, 236, 232) : ui::rgba(40, 55, 70));
		}
	}

	const auto detected = detect_document(document, document->dimensions());
	assert_equal(true, static_cast<bool>(detected), "detects clear document");
	assert_equal(true, detected.extent.cx > 100 && detected.extent.cy > 80, "document extent is plausible");

	const auto ambiguous = std::make_shared<ui::surface>();
	ambiguous->alloc(160, 120, ui::texture_format::RGB);
	for (auto y = 0; y < 120; ++y)
		for (auto x = 0; x < 160; ++x)
			ambiguous->set_pixel(x, y, ui::rgba(220, 220, 220));
	assert_equal(true, !detect_document(ambiguous, ambiguous->dimensions()), "rejects borderless bright image");
}

static void should_detect_photographed_document()
{
	files loader;
	const auto loaded = loader.load(test_files_folder.combine("excluded1").combine_file("document.png"), false);
	const auto surface = loaded.to_surface({768, 768});
	const auto detected = detect_document(surface, loaded.dimensions());

	assert_equal(true, static_cast<bool>(detected), "detects photographed document");
	assert_equal(true, detected.extent.cx > loaded.dimensions().cx / 2 &&
	             detected.extent.cy > loaded.dimensions().cy / 2 &&
	             detected.extent.cx <= loaded.dimensions().cx &&
	             detected.extent.cy <= loaded.dimensions().cy,
	             "photographed document extent is plausible");
	assert_equal(true, detected.extent.cy > detected.extent.cx, "photographed document remains portrait");
}

static void should_detect_document_at_image_edge()
{
	const auto document = std::make_shared<ui::surface>();
	document->alloc(160, 120, ui::texture_format::RGB);
	for (auto y = 0; y < 120; ++y)
	{
		for (auto x = 0; x < 160; ++x)
		{
			const auto inside = x < 130 && y < 100;
			document->set_pixel(x, y, inside ? ui::rgba(238, 236, 232) : ui::rgba(40, 55, 70));
		}
	}

	const auto detected = detect_document(document, document->dimensions());
	assert_equal(true, static_cast<bool>(detected), "detects document touching image edge");
}

static void should_detect_low_contrast_document()
{
	const auto document = std::make_shared<ui::surface>();
	document->alloc(240, 180, ui::texture_format::RGB);
	constexpr std::array<pointd, 4> corners = {pointd(34, 22), pointd(210, 30), pointd(202, 160), pointd(26, 152)};

	for (auto y = 0; y < 180; ++y)
	{
		for (auto x = 0; x < 240; ++x)
		{
			bool inside = true;
			for (auto edge = 0; edge < 4; ++edge)
			{
				const auto& a = corners[edge];
				const auto& b = corners[(edge + 1) % 4];
				if ((b.X - a.X) * (y + 0.5 - a.Y) - (b.Y - a.Y) * (x + 0.5 - a.X) < 0)
				{
					inside = false;
					break;
				}
			}
			// Lighting ramp overlaps page and surround tones, so no global threshold separates them.
			const auto level = df::round((inside ? 148.0 : 120.0) + 70.0 * x / 240.0);
			document->set_pixel(x, y, ui::rgba(level, level, level));
		}
	}

	const auto detected = detect_document(document, document->dimensions());
	assert_equal(true, static_cast<bool>(detected), "detects low contrast document");
	assert_equal(true, detected.extent.cx > 140 && detected.extent.cx < 210, "low contrast document width");
	assert_equal(true, detected.extent.cy > 100 && detected.extent.cy < 160, "low contrast document height");
}

static void should_crop_without_resampling()
{
	const auto source = std::make_shared<ui::surface>();
	source->alloc(16, 12, ui::texture_format::RGB);

	for (auto y = 0; y < 12; ++y)
	{
		for (auto x = 0; x < 16; ++x)
		{
			// Alternating extremes, so any interpolation shows up immediately as a mid tone.
			source->set_pixel(x, y, (x + y) & 1 ? ui::rgba(255, 255, 255) : ui::rgba(0, 0, 0));
		}
	}

	// A crop dragged in the view lands on fractional source pixels.
	image_edits edits;
	edits.crop_bounds(quadd(rectd(3.4, 2.6, 8.0, 6.0)));

	const auto cropped = source->transform(edits);
	assert_equal(true, cropped->dimensions() == sizei(8, 6), "crop extent");

	for (auto y = 0u; y < cropped->height(); ++y)
	{
		for (auto x = 0u; x < cropped->width(); ++x)
		{
			assert_equal(source->get_pixel(3 + x, 3 + y), cropped->get_pixel(x, y), "crop copies source pixels");
		}
	}
}

static void should_apply_temperature_and_tint()
{
	const auto source = std::make_shared<ui::surface>();
	source->alloc(1, 1, ui::texture_format::RGB);
	source->set_pixel(0, 0, ui::rgba(128, 128, 128));

	image_edits edits;
	edits.temperature(0.5);
	edits.tint(0.25);
	const auto adjusted = source->transform(edits)->get_pixel(0, 0);

	assert_equal(true, ui::get_r(adjusted) > ui::get_b(adjusted), "temperature warms neutral pixel");
	assert_equal(true, ui::get_g(adjusted) < ui::get_r(adjusted), "positive tint reduces green");
}

static void should_initialize_edit_rotation_from_orientation()
{
	constexpr sizei landscape(120, 80);
	const auto top_left = edit_view_state::initial_crop(landscape, ui::orientation::top_left);
	const auto right_top = edit_view_state::initial_crop(landscape, ui::orientation::right_top);
	const auto bottom_right = edit_view_state::initial_crop(landscape, ui::orientation::bottom_right);
	const auto left_bottom = edit_view_state::initial_crop(landscape, ui::orientation::left_bottom);

	assert_equal(0.0, top_left.angle(), "top-left rotation");
	assert_equal(-90.0, right_top.angle(), "right-top rotation");
	assert_equal(180.0, std::abs(bottom_right.angle()), "bottom-right rotation");
	assert_equal(90.0, left_bottom.angle(), "left-bottom rotation");
	assert_equal(true, right_top.actual_extent().Height > right_top.actual_extent().Width,
	             "right-top orientation is portrait");
	assert_equal(true, left_bottom.actual_extent().Height > left_bottom.actual_extent().Width,
	             "left-bottom orientation is portrait");

	const auto source = std::make_shared<ui::surface>();
	source->alloc(3, 2, ui::texture_format::RGB);
	for (auto y = 0; y < 2; ++y)
	{
		for (auto x = 0; x < 3; ++x)
		{
			source->set_pixel(x, y, ui::rgba(10 + x + y * 3, 0, 0));
		}
	}

	image_edits edits;
	edits.crop_bounds(edit_view_state::initial_crop(source->dimensions(), ui::orientation::right_top));
	const auto edited = source->transform(edits);
	const auto displayed = source->transform(to_simple_transform(ui::orientation::right_top));
	assert_equal(true, displayed->dimensions() == edited->dimensions(),
	             "right-top edit dimensions match image view");
	for (auto y = 0u; y < edited->height(); ++y)
	{
		for (auto x = 0u; x < edited->width(); ++x)
		{
			assert_equal(displayed->get_pixel(x, y), edited->get_pixel(x, y),
			             "right-top edit pixels match image view");
		}
	}
}

static void should_build_rotated_edit_preview_surface()
{
	files ff;
	const auto loaded = ff.load(test_files_folder.combine_file("Test.jpg"), false);
	const auto source_dimensions = loaded.dimensions();
	const auto source = loaded.to_surface(ui::scale_dimensions(source_dimensions, 192));

	image_edits edits;
	edits.crop_bounds(quadd(source_dimensions).transform(simple_transform::rot_90));
	const auto preview = edit_view::build_preview_surface(source, source_dimensions, edits);
	const auto expected_dimensions = ui::scale_dimensions(sizei(source->height(), source->width()), 128);

	assert_equal(true, is_valid(preview), "rotated edit preview is valid");
	assert_equal(expected_dimensions.cx, static_cast<int>(preview->width()), "rotated edit preview width");
	assert_equal(expected_dimensions.cy, static_cast<int>(preview->height()), "rotated edit preview height");
}

static void should_build_straightened_edit_preview_without_black_corners()
{
	constexpr sizei loaded_dimensions(4000, 3000);
	const auto source = std::make_shared<ui::surface>();
	source->alloc(192, 144, ui::texture_format::RGB);
	source->clear(ui::rgba(255, 255, 255));

	image_edits edits;
	const quadd image_bounds(loaded_dimensions);
	edits.crop_bounds(image_bounds.rotate(5.0, image_bounds.center_point()));
	const auto preview = edit_view::build_preview_surface(source, loaded_dimensions, edits);

	assert_equal(true, is_valid(preview), "straightened edit preview is valid");
	assert_equal(true, preview->width() <= 128 && preview->height() <= 128,
	             "straightened edit preview is bounded");
	constexpr auto rgb_mask = 0x00ffffffu;
	constexpr auto white = 0x00ffffffu;
	assert_equal(white, preview->get_pixel(preview->width() / 2, preview->height() / 2) & rgb_mask,
	             "straightened edit preview center");
	assert_equal(white, preview->get_pixel(0, 0) & rgb_mask, "straightened edit preview top-left corner");
	assert_equal(white, preview->get_pixel(preview->width() - 1, 0) & rgb_mask,
	             "straightened edit preview top-right corner");
	assert_equal(white, preview->get_pixel(0, preview->height() - 1) & rgb_mask,
	             "straightened edit preview bottom-left corner");
	assert_equal(white, preview->get_pixel(preview->width() - 1, preview->height() - 1) & rgb_mask,
	             "straightened edit preview bottom-right corner");
}


static void should_not_treat_initial_orientation_as_an_edit()
{
	image_edits original_edits;
	original_edits.crop_bounds(edit_view_state::initial_crop(sizei(120, 80), ui::orientation::right_top));
	auto current_edits = original_edits;

	assert_equal(false, current_edits != original_edits, "initial EXIF orientation is baseline");

	current_edits.crop_bounds(current_edits.crop_bounds().transform(simple_transform::rot_90));
	assert_equal(true, current_edits != original_edits, "user rotation changes baseline");
}

static void should_update_rating(const std::string_view name)
{
	files ff;

	const auto ext = name.substr(df::find_ext(name));
	const auto save_path = _temps.next_path(ext);
	const auto load_path = test_files_folder.combine_file(name);

	metadata_edits edits1;
	edits1.rating = 3;

	auto set_rating = ff.update(load_path, save_path, edits1, {}, {}, false, {}, {}, ff_inspect_rescan(save_path));

	{
		const auto actual_scanned = ff_scan_after_update(ff, set_rating, save_path, detect_xmp_sidecar(save_path));
		const auto ps = actual_scanned.to_props();
		assert_equal(3, ps->rating, "rating");
	}

	metadata_edits edits2;
	edits2.remove_rating = true;
	auto clear_rating = ff.update(save_path, edits2, {}, {}, false, {}, ff_inspect_rescan(save_path));

	{
		const auto actual_scanned = ff_scan_after_update(ff, clear_rating, save_path, detect_xmp_sidecar(save_path));
		const auto ps = actual_scanned.to_props();
		assert_equal(0, ps->rating, "rating");
	}
}

static void should_update_label(const std::string_view name)
{
	files ff;

	const auto ext = name.substr(df::find_ext(name));
	const auto save_path = _temps.next_path(ext);
	const auto load_path = test_files_folder.combine_file(name);

	metadata_edits set_label;
	set_label.label = label_approved_text;
	auto label_written = ff.update(load_path, save_path, set_label, {}, {}, false, {}, {},
	                               ff_inspect_rescan(save_path));

	{
		const auto actual_scanned = ff_scan_after_update(ff, label_written, save_path, detect_xmp_sidecar(save_path));
		assert_equal(label_approved_text, actual_scanned.to_props()->label, "label");
	}

	metadata_edits clear_label;
	clear_label.label = std::string{};
	auto label_cleared = ff.update(save_path, clear_label, {}, {}, false, {}, ff_inspect_rescan(save_path));

	{
		const auto actual_scanned = ff_scan_after_update(ff, label_cleared, save_path, detect_xmp_sidecar(save_path));
		assert_equal(std::string_view{}, actual_scanned.to_props()->label, "cleared label");
	}
}

// Issue #134 - ratings and labels could not be applied to files whose name contains emoji.
// Emoji are non-BMP: surrogate pairs in the UTF-16 filesystem API and 4-byte sequences in the
// UTF-8 path the model carries. The in-place update path is the risky one - it derives a
// temporary name from the original, writes it, then replaces the original - so the name has to
// survive creation, a rescan, the replacement, and a second rescan without being mangled.
static void should_update_rating_and_label_for_emoji_filename()
{
	files ff;

	constexpr std::string_view emoji_name = "test-\U0001F389\U0001F4A3.jpg"; // test-🎉💣.jpg

	const auto folder = _temps.next_folder("emoji-names");
	const auto save_path = folder.combine_file(emoji_name);
	const auto load_path = test_files_folder.combine_file("Test.jpg");

	metadata_edits create_edits;
	create_edits.rating = 3;
	auto created = ff.update(load_path, save_path, create_edits, {}, {}, false, {}, {},
	                         ff_inspect_rescan(save_path));

	assert_equal(true, save_path.exists(), "emoji-named file created");
	assert_equal(emoji_name, save_path.name().sv(), "emoji name is not mangled on write");

	{
		const auto scanned = ff_scan_after_update(ff, created, save_path, detect_xmp_sidecar(save_path));
		assert_equal(3, scanned.to_props()->rating, "rating written to emoji-named file");
	}

	// In-place: this is the temp-file-then-replace path, and the only one the report exercises.
	metadata_edits label_edits;
	label_edits.label = label_approved_text;
	auto labelled = ff.update(save_path, label_edits, {}, {}, false, {}, ff_inspect_rescan(save_path));

	{
		const auto scanned = ff_scan_after_update(ff, labelled, save_path, detect_xmp_sidecar(save_path));
		const auto ps = scanned.to_props();
		assert_equal(label_approved_text, ps->label, "label written in place to emoji-named file");
		assert_equal(3, ps->rating, "existing rating survives the in-place replacement");
	}

	metadata_edits clear_edits;
	clear_edits.remove_rating = true;
	clear_edits.label = std::string{};
	auto cleared = ff.update(save_path, clear_edits, {}, {}, false, {}, ff_inspect_rescan(save_path));

	{
		const auto scanned = ff_scan_after_update(ff, cleared, save_path, detect_xmp_sidecar(save_path));
		const auto ps = scanned.to_props();
		assert_equal(0, ps->rating, "rating cleared from emoji-named file");
		assert_equal(std::string_view{}, ps->label, "label cleared from emoji-named file");
	}

	// A failed replacement leaves the working file behind, which is how the reporter saw the
	// original name "disappear"; only the emoji-named file may remain.
	assert_equal(true, save_path.exists(), "emoji-named file survives every update");
	assert_equal(1, static_cast<int>(platform::iterate_file_items(folder, false).files.size()),
	             "no temporary files left beside the emoji-named file");
}

static void should_update_exif_rating()
{
	const auto load_path = test_files_folder.combine_file("exif-rating.jpg");
	const auto save_path = _temps.next_path(".jpg");

	files ff;
	metadata_edits edits1;
	edits1.rating = 3;

	auto set_rating = ff.update(load_path, save_path, edits1, {}, {}, false, {}, {}, ff_inspect_rescan(save_path));

	{
		const auto actual_scanned = ff_scan_after_update(ff, set_rating, save_path);
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
	auto clear_rating = ff.update(save_path, edits2, {}, {}, false, {}, ff_inspect_rescan(save_path));

	{
		const auto actual_scanned = ff_scan_after_update(ff, clear_rating, save_path);
		assert_equal(0, actual_scanned.to_props()->rating);

		const auto actual_exif = extract_properties(save_path, metadata_type::EXIF);
		const auto actual_iptc = extract_properties(save_path, metadata_type::IPTC);
		const auto actual_xmp = extract_properties(save_path, metadata_type::XMP);

		assert_equal(0, actual_xmp->rating, "XMP");
		assert_equal(0, actual_exif->rating, "exif");
		assert_equal(0, actual_iptc->rating, "IPTC");
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

	auto described = ff.update(load_path, save_path, edits1, {}, {}, false, {}, {}, ff_inspect_rescan(save_path));

	{
		const auto actual_scanned = ff_scan_after_update(ff, described, save_path);
		const auto ps = actual_scanned.to_props();
		assert_equal(desc_text, ps->description, "to_props");

		const auto actual_exif = extract_properties(save_path, metadata_type::EXIF);
		const auto actual_iptc = extract_properties(save_path, metadata_type::IPTC);
		const auto actual_xmp = extract_properties(save_path, metadata_type::XMP);

		assert_equal(desc_text, actual_xmp->description, "XMP");
		assert_equal(desc_text, actual_exif->description, "exif");
		assert_equal(desc_text, actual_iptc->description, "IPTC");
	}
}

static void should_update_synopsis()
{
	const auto load_path = test_files_folder.combine_file("exif-rating.jpg");
	const auto save_path = _temps.next_path(".jpg");
	constexpr auto synopsis_text = "A long description of what happens";

	files ff;
	metadata_edits edits;
	edits.synopsis = synopsis_text;
	assert_equal(true, edits.has_changes(), "synopsis counts as a change");

	auto written = ff.update(load_path, save_path, edits, {}, {}, false, {}, {}, ff_inspect_rescan(save_path));
	assert_equal(true, written.success(), std::format("synopsis written ({})", written.format_error()));

	const auto actual_scanned = ff_scan_after_update(ff, written, save_path);
	assert_equal(synopsis_text, actual_scanned.to_props()->synopsis, "to_props");

	const auto actual_xmp = extract_properties(save_path, metadata_type::XMP);
	assert_equal(synopsis_text, actual_xmp->synopsis, "XMP");
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

	auto written = ff.update(load_path, save_path, metadata_edits, {}, {}, false, {}, {},
	                         ff_inspect_rescan(save_path));

	const auto sr_expected = ff_scan_file(ff, load_path, detect_xmp_sidecar(load_path));
	const auto sr_actual = ff_scan_after_update(ff, written, save_path, detect_xmp_sidecar(save_path));

	const auto expected = sr_expected.to_props();
	const auto actual = sr_actual.to_props();

	expected->title = "Title xx"_c;
	expected->copyright_notice = "Copyright xx"_c;
	expected->description = "Description xx"_c;
	expected->rating = 3;
	expected->tags = make_unique_tags(tag_set(expected->tags), tags_to_add);

	// The Adobe XMP SDK legacy-reconciles dc:description into the native ASF
	// Content-Description "Description" field, which FFmpeg surfaces as "comment".
	// So for ASF/WMV the description round-trips as both description and comment.
	if (str::icmp(ext, ".asf") == 0 || str::icmp(ext, ".wmv") == 0 || str::icmp(ext, ".wm") == 0)
	{
		expected->comment = "Description xx"_c;
	}

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
	auto add_result = ff.update(load_path, save_path, edits1, {}, {}, false, {}, {}, ff_inspect_rescan(save_path));
	assert_equal(true, add_result.success(), std::format("tags added ({})", add_result.format_error()));

	const auto sr_expected = ff_scan_file(ff, load_path, detect_xmp_sidecar(load_path));
	const auto sr_actual = ff_scan_after_update(ff, add_result, save_path, detect_xmp_sidecar(save_path));

	const auto expected = sr_expected.to_props();
	const auto actual = sr_actual.to_props();

	expected->tags = make_unique_tags(tag_set(expected->tags), tags_to_add);

	assert_metadata(*expected, *actual, "added");

	metadata_edits edits2;
	edits2.remove_tags = tags_to_remove;
	auto remove_result = ff.update(save_path, edits2, {}, {}, false, {}, ff_inspect_rescan(save_path));
	assert_equal(true, remove_result.success(), std::format("tags removed ({})", remove_result.format_error()));

	tag_set expected_tags(expected->tags);
	expected_tags.remove(tags_to_remove);
	expected->tags = make_unique_tags(expected_tags, {});

	const auto sr_actual2 = ff_scan_after_update(ff, remove_result, save_path, detect_xmp_sidecar(save_path));
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
	const auto write_result = platform::write_shell_tags(save_path, shell_tags);
	assert_equal(true, write_result.success(), std::format("write_shell_tags ({})", write_result.format_error()));

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

// The FFmpeg 'Xtra' box reader surfaces Windows Explorer / Media Player tags
// (WM/Category) as "keywords" and the user rating (WM/SharedUserRating). The
// committed fixtures carry these in-file, so we can read them without the Shell
// API — proving the FFmpeg-only read path for Windows-tagged MP4/MOV. (#123)
static void should_read_mp4_xtra_metadata(const std::string_view name, const std::string_view expect_tag)
{
	files ff;
	const auto sr = ff_scan_file(ff, test_files_folder.combine_file(name));

	bool found_keywords = false;
	bool found_rating = false;
	for (const auto& kv : sr.ffmpeg_metadata)
	{
		if (str::icmp(kv.key, "WM/Category") == 0 &&
			kv.value.find(expect_tag) != std::string_view::npos)
			found_keywords = true;
		if (str::icmp(kv.key, "WM/SharedUserRating") == 0)
			found_rating = true;
	}

	assert_equal(true, found_keywords, std::format("xtra keywords {}", name));
	assert_equal(true, found_rating, std::format("xtra rating {}", name));
}

// FFmpeg's ASF demuxer must surface ALL Windows 'WM/Category' keywords, not just
// the first. Windows stores them as separate descriptors across the Extended-
// Content-Description and Metadata-Library objects; the demuxer now appends them
// (';'-separated). Tag a WMV via the Shell, then read the raw metadata. (#123)
static void should_read_asf_wm_categories()
{
	const auto load_path = test_files_folder.combine_file("gen.wmv");
	const auto save_path = _temps.next_path(".wmv");

	files ff;
	ff.update(load_path, save_path, {}, {}, {}, false, {});

	const std::vector<std::string> shell_tags = {"AsfTagA", "AsfTagB", "AsfTagC"};
	if (platform::write_shell_tags(save_path, shell_tags).failed())
		return; // Windows Media Foundation unavailable; skip on this host

	const auto sr = ff_scan_file(ff, save_path);

	std::string_view categories;
	for (const auto& kv : sr.ffmpeg_metadata)
	{
		if (str::icmp(kv.key, "WM/Category") == 0) categories = kv.value;
	}

	assert_equal(true, categories.find("AsfTagA") != std::string_view::npos, "asf WM/Category A");
	assert_equal(true, categories.find("AsfTagB") != std::string_view::npos, "asf WM/Category B");
	assert_equal(true, categories.find("AsfTagC") != std::string_view::npos, "asf WM/Category C");
}

// Windows compatibility (write): Diffractor writes tags & rating via its normal
// XMP update; the XMP SDK reconciles them into the Windows 'Xtra' atom, so the
// Windows Shell (Explorer / Media Player) reads them back. Validates the SDK
// writes Microsoft's binary format byte-exactly. (#123)
static void should_write_windows_tags_via_xmp(const std::string_view name)
{
	const auto ext = name.substr(df::find_ext(name));
	const auto load_path = test_files_folder.combine_file(name);
	const auto save_path = _temps.next_path(ext);
	const auto original_subtitle = find_xtra_entry(load_path, "WM/SubTitle");

	metadata_edits edits;
	edits.add_tags = tag_set("WinWriteA WinWriteB");
	edits.rating = 4;

	files ff;
	ff.update(load_path, save_path, edits, {}, {}, false, {});

	const auto shell = platform::read_shell_metadata(save_path);

	bool found_a = false, found_b = false;
	for (const auto& t : shell.tags)
	{
		if (str::icmp(t, "WinWriteA") == 0) found_a = true;
		if (str::icmp(t, "WinWriteB") == 0) found_b = true;
	}
	assert_equal(true, found_a && found_b, std::format("shell reads xmp-written tags {}", name));
	assert_equal(true, shell.rating.has_value() && *shell.rating >= 63,
	             std::format("shell reads xmp-written rating {}", name)); // 4 stars => 75

	// The file must remain valid (not corrupted by the native-metadata write).
	const auto sr = ff_scan_file(ff, save_path);
	assert_equal(true, sr.success, std::format("file still scans after write {}", name));
	if (!original_subtitle.empty())
	{
		assert_equal(true, original_subtitle == find_xtra_entry(save_path, "WM/SubTitle"),
		             std::format("unrelated Xtra subtitle preserved {}", name));
	}
}

// Windows compatibility: the tags Windows Explorer / Media Player write live in
// the Shell property system (MP4 'Xtra' atom, ASF 'WM/*' descriptors), which
// FFmpeg does not fully surface. Verify Diffractor's Shell wrappers can write and
// read them back the same way Windows does, for the formats Windows can tag. (#123)
static void should_roundtrip_windows_shell_tags(const std::string_view name)
{
	const auto ext = name.substr(df::find_ext(name));
	const auto load_path = test_files_folder.combine_file(name);
	const auto save_path = _temps.next_path(ext);

	files ff;
	ff.update(load_path, save_path, {}, {}, {}, false, {}); // copy to temp

	const std::vector<std::string> shell_tags = {"WinTagA", "WinTagB", "Максим"};
	const auto write_result = platform::write_shell_tags(save_path, shell_tags);
	assert_equal(true, write_result.success(),
	             std::format("write_shell_tags {} ({})", name, write_result.format_error()));

	// The same Shell property store Explorer uses reads the tags back verbatim.
	const auto shell_meta = platform::read_shell_metadata(save_path);
	assert_equal(3u, static_cast<unsigned>(shell_meta.tags.size()), std::format("shell tag count {}", name));

	bool found_a = false, found_b = false, found_c = false;
	for (const auto& t : shell_meta.tags)
	{
		if (str::icmp(t, "WinTagA") == 0) found_a = true;
		else if (str::icmp(t, "WinTagB") == 0) found_b = true;
		else if (str::icmp(t, "Максим") == 0) found_c = true;
	}
	assert_equal(true, found_a && found_b && found_c, std::format("shell tags round-trip {}", name));

	// Diffractor's scan must still succeed on the Windows-tagged file.
	const auto sr = ff_scan_file(ff, save_path);
	assert_equal(true, sr.success, std::format("scan ok {}", name));
}

// Containers Windows cannot tag (no property handler — AVI, MPEG program stream):
// the Shell write must fail gracefully and the Diffractor scan must still succeed.
static void should_handle_unsupported_shell_tags(const std::string_view name)
{
	const auto ext = name.substr(df::find_ext(name));
	const auto load_path = test_files_folder.combine_file(name);
	const auto save_path = _temps.next_path(ext);

	files ff;
	ff.update(load_path, save_path, {}, {}, {}, false, {});

	const auto write_result = platform::write_shell_tags(save_path, {"X", "Y"});
	assert_equal(false, write_result.success(), std::format("shell write unsupported {}", name));

	const auto sr = ff_scan_file(ff, save_path);
	assert_equal(true, sr.success, std::format("scan still ok {}", name));
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

	auto written = ff.update(load_path, save_path, metadata_edits, {}, {}, false, {}, {},
	                         ff_inspect_rescan(save_path));

	const auto sr_expected = ff_scan_file(ff, load_path, detect_xmp_sidecar(load_path));
	const auto sr_actual = ff_scan_after_update(ff, written, save_path, detect_xmp_sidecar(save_path));

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
	assert_equal(description, actual_iptc->description, "IPTC");

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

static void should_edit_video_metadata_in_place(const std::string_view name)
{
	const auto source = test_files_folder.combine_file(name);
	const auto path = _temps.next_path(source.extension());
	platform::copy_file(source, path, false, false);

	files ff;

	metadata_edits first;
	first.rating = 3;
	assert_equal(true, ff.update(path, first, {}, {}, false, {}).success(), "first rating saved");
	assert_equal(true, metadata_xmp::has_embedded_xmp(path), "packet embedded");

	const auto size_before = platform::file_attributes(path).size;

	// With a packet in place the toolkit patches the existing bytes, so the file is never
	// staged and swapped, which proves the whole media file was not copied.
	metadata_edits second;
	second.rating = 5;
	const auto result = ff.update(path, second, {}, {}, false, {});

	assert_equal(true, result.success(), "second rating saved");
	assert_equal(false, result.staged, "second rating edited in place");
	assert_equal(true, result.created_files.files.empty(), "no staged files left behind");

	const auto sr = ff_scan_file(ff, path);
	assert_equal(5, sr.to_props()->rating, "rating round trips");
	assert_equal(size_before, platform::file_attributes(path).size, "file not rewritten");
}

// ISO base media absorbs a first packet with a bounded box write, so the opening rating of an
// import must not copy the whole video.
static void should_inject_video_metadata_in_place(const std::string_view name)
{
	const auto source = test_files_folder.combine_file(name);
	const auto path = _temps.next_path(source.extension());
	platform::copy_file(source, path, false, false);

	assert_equal(false, metadata_xmp::has_embedded_xmp(path), "starts with no packet");

	files ff;
	metadata_edits edits;
	edits.rating = 4;
	const auto result = ff.update(path, edits, {}, {}, false, {});

	assert_equal(true, result.success(), "first rating saved");
	assert_equal(false, result.staged, "first rating injected in place");
	assert_equal(true, result.created_files.files.empty(), "no staged files left behind");

	const auto sr = ff_scan_file(ff, path);
	assert_equal(4, sr.to_props()->rating, "rating round trips");
}

// MP3 and RIFF handlers shift the whole payload inside the live file with no temp, and JPEG
// rewrites whenever a rating dirties EXIF. All must stage, so the original survives a failure.
static void should_stage_metadata_edit(const std::string_view name)
{
	const auto source = test_files_folder.combine_file(name);
	const auto path = _temps.next_path(source.extension());
	platform::copy_file(source, path, false, false);

	files ff;

	metadata_edits first;
	first.rating = 3;
	const auto first_result = ff.update(path, first, {}, {}, false, {});
	assert_equal(true, first_result.success(), std::format("first rating saved ({})", first_result.format_error()));

	metadata_edits second;
	second.rating = 5;
	const auto result = ff.update(path, second, {}, {}, false, {});

	assert_equal(true, result.success(), std::format("second rating saved ({})", result.format_error()));
	assert_equal(true, result.staged, "edit staged and swapped");
	assert_equal(true, result.created_files.files.empty(), "no staged files left behind");

	const auto sr = ff_scan_file(ff, path, detect_xmp_sidecar(path));
	assert_equal(5, sr.to_props()->rating, "rating round trips");
}

// The three readers of what a write produced are all served from one scan taken behind the write.
// This is the display's share of it: the bytes come back as an image, so nothing reads the file
// again to draw what was just saved.
static void should_return_written_image()
{
	const auto path = _temps.next_path(".jpg");
	platform::copy_file(test_files_folder.combine_file("Test.jpg"), path, false, false);

	files ff;
	const auto before = ff.load(path, false);

	rescan_spec rescan;
	rescan.wanted = true;
	rescan.load_thumbnail = true;
	rescan.want_image = true;
	rescan.file_type = files::file_type_from_name(path);

	metadata_edits edits;
	edits.rating = 3;
	const auto result = ff.update(path, edits, {}, {}, false, {}, rescan);

	assert_equal(true, result.success(), std::format("rating saved ({})", result.format_error()));
	assert_equal(true, result.scanned, "write scanned what it wrote");
	assert_equal(true, result.loaded.success, "write returned the image it wrote");
	assert_equal(before.i->width(), result.loaded.i->width(), "written image width");
	assert_equal(before.i->height(), result.loaded.i->height(), "written image height");

	// The scan only wraps the written bytes when a caller asked for them, and asking for a thumbnail
	// is a separate request. Without this the assertions above pass on the thumbnail's half of the
	// gate and say nothing about want_image.
	rescan_spec image_only;
	image_only.wanted = true;
	image_only.load_thumbnail = false;
	image_only.want_image = true;
	image_only.file_type = files::file_type_from_name(path);

	metadata_edits more_edits;
	more_edits.rating = 4;
	const auto image_only_result = ff.update(path, more_edits, {}, {}, false, {}, image_only);

	assert_equal(true, image_only_result.success(),
	             std::format("rating saved ({})", image_only_result.format_error()));
	assert_equal(true, image_only_result.loaded.success, "want_image alone returns the written image");
	assert_equal(before.i->width(), image_only_result.loaded.i->width(), "image-only written width");
	assert_equal(before.i->height(), image_only_result.loaded.i->height(), "image-only written height");
}

// The AV display's share. A container can be gigabytes, so it takes the handle rather than the
// bytes; reading it back is what proves the handle refers to the swapped-in file and not the stage.
static void should_hand_over_written_handle()
{
	const auto path = _temps.next_path(".mp3");
	platform::copy_file(test_files_folder.combine_file("Colorblind.mp3"), path, false, false);

	files ff;

	rescan_spec rescan;
	rescan.want_handle = true;

	metadata_edits edits;
	edits.rating = 3;
	const auto result = ff.update(path, edits, {}, {}, false, {}, rescan);

	assert_equal(true, result.success(), std::format("rating saved ({})", result.format_error()));
	assert_equal(true, result.staged, "edit staged and swapped");
	assert_equal(true, result.display_handle != nullptr, "write handed over its open handle");

	const auto expected = df::blob_from_file(path);
	df::blob actual;
	actual.resize(expected.size());
	result.display_handle->seek(0, platform::file::whence::begin);
	actual.resize(static_cast<size_t>(result.display_handle->read(actual.data(), actual.size())));

	assert_equal(true, expected == actual, "handle reads back the written bytes");
}

static void should_not_rewrite_unchanged_file()
{
	const auto path = _temps.next_path(".jpg");
	platform::copy_file(test_files_folder.combine_file("Test.jpg"), path, false, false);
	const auto modified_before = platform::file_attributes(path).modified;

	files ff;
	const auto result = ff.update(path, {}, {}, {}, false, {});

	assert_equal(true, result.success(), "unchanged save succeeds");
	assert_equal(modified_before, platform::file_attributes(path).modified, "unchanged save preserves modified time");
}

// A raw file's metadata lives in its sidecar, so rating one must not cost the size of the raw.
// The media file is the assertion: an untouched size, modified time and byte count prove no copy
// or swap ran, and a sidecar-only write proves replace_file never saw the raw at all.
static void should_edit_raw_sidecar_only()
{
	const auto path = _temps.next_path(".CR2");
	const auto path_xmp = path.extension(".xmp");
	const auto raw_folder = test_files_folder.combine("raw");

	platform::copy_file(raw_folder.combine_file("Screws.CR2"), path, false, false);
	platform::copy_file(raw_folder.combine_file("Screws.xmp"), path_xmp, false, false);

	const auto attributes_before = platform::file_attributes(path);
	const auto raw_before = df::blob_from_file(path);

	files ff;
	metadata_edits edits;
	edits.rating = 5;
	const auto result = ff.update(path, edits, {}, {}, false, {});

	assert_equal(true, result.success(), "rating saved");
	assert_equal(false, result.staged, "raw not staged and swapped");
	assert_equal(true, result.created_files.files.empty(), "no staged files left behind");

	const auto attributes_after = platform::file_attributes(path);
	assert_equal(attributes_before.size, attributes_after.size, "raw size unchanged");
	assert_equal(attributes_before.modified, attributes_after.modified, "raw modified time unchanged");
	assert_equal(true, raw_before == df::blob_from_file(path), "raw bytes unchanged");

	prop::item_metadata sidecar;
	metadata_xmp::parse(sidecar, path_xmp);
	assert_equal(5, sidecar.rating, "sidecar rating");

	const auto sr = ff_scan_file(ff, path, detect_xmp_sidecar(path));
	assert_equal(5, sr.to_props()->rating, "rating round trips");
}

static void should_save_as_with_distinct_xmp_sidecar()
{
	const auto source_path = _temps.next_path(".CR2");
	const auto source_xmp_path = source_path.extension(".xmp");
	const auto destination_path = _temps.next_path(".CR2");
	const auto destination_xmp_path = destination_path.extension(".xmp");
	const auto raw_folder = test_files_folder.combine("raw");

	platform::copy_file(raw_folder.combine_file("Screws.CR2"), source_path, false, false);
	platform::copy_file(raw_folder.combine_file("Screws.xmp"), source_xmp_path, false, false);
	const auto source_xmp_before = df::blob_from_file(source_xmp_path);

	metadata_edits edits;
	edits.title = "Saved as copy";

	files ff;
	const auto result = ff.update(source_path, destination_path, edits, {}, {}, false, source_xmp_path.name());

	assert_equal(true, result.success(), "save as succeeds");
	assert_equal(true, source_xmp_before == df::blob_from_file(source_xmp_path), "source sidecar unchanged");
	assert_equal(true, destination_xmp_path.exists(), "destination sidecar created");
	prop::item_metadata destination_xmp;
	metadata_xmp::parse(destination_xmp, destination_xmp_path);
	assert_equal("Saved as copy", destination_xmp.title, "destination sidecar metadata");
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

// Issue #102 - Rotating an image (the transform behind the [ and ] shortcuts and
// the rotate_clockwise/anticlockwise commands). These tests validate the 90-degree
// rotation pipeline across JPEG (incl. lossless + EXIF-oriented), PNG and WebP;
// the keyboard-shortcut dispatch reported in #102 is a UI-level concern.
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

		// Test.jpg is 683 pixels tall, which is not a whole number of MCUs, so the lossless path
		// declines it. The rotate must still keep every row rather than trimming to the MCU grid,
		// which the Test90.jpg fixture does not - it was captured from the old trimming path.
		expected->width = static_cast<int>(loaded.i->height());
		expected->height = static_cast<int>(loaded.i->width());

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
		assert_equal(loaded.i->width(), updated.i->height(), "png height");
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
		assert_equal(loaded.i->width(), updated.i->height(), "webp height");
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
		throw test_assert_exception(std::format("Should resize: could not extract properties from {}",
		                                        save_path.str()));
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

// A small (single APP2 segment) ICC profile must round-trip unchanged through a
// JPEG encode followed by a metadata scan.
static void should_roundtrip_small_icc()
{
	files ff;
	const auto load_path = test_files_folder.combine_file("Test.jpg");
	const auto loaded = ff.load(load_path, false);
	const auto surface = loaded.to_surface();

	metadata_parts metadata;
	metadata.icc.resize(3u * 1024u);
	for (size_t i = 0; i < metadata.icc.size(); ++i)
	{
		metadata.icc[i] = static_cast<uint8_t>((i * 17u + 3u) & 0xffu);
	}

	const auto encoded = ff.surface_to_image(surface, metadata, {}, ui::image_format::JPEG);

	mem_read_stream stream(encoded->data());
	const auto scanned = scan_jpg(stream);

	assert_equal(metadata.icc.size(), scanned.metadata.icc.size(), "small icc size round-trips");
	assert_equal(true, metadata.icc == scanned.metadata.icc, "small icc bytes round-trip");
}

// Regression: a large ICC profile exceeds the 65533-byte JPEG marker limit, so
// it must be split across multiple APP2 segments on write and reassembled in
// sequence order on read without any loss or reordering.
static void should_roundtrip_large_icc()
{
	files ff;
	const auto load_path = test_files_folder.combine_file("Test.jpg");
	const auto loaded = ff.load(load_path, false);
	const auto surface = loaded.to_surface();

	// 200 KB => 4 APP2 segments (each holds ~65519 profile bytes).
	metadata_parts metadata;
	metadata.icc.resize(200u * 1024u);
	for (size_t i = 0; i < metadata.icc.size(); ++i)
	{
		metadata.icc[i] = static_cast<uint8_t>((i * 31u + 7u) & 0xffu);
	}

	const auto encoded = ff.surface_to_image(surface, metadata, {}, ui::image_format::JPEG);

	mem_read_stream stream(encoded->data());
	const auto scanned = scan_jpg(stream);

	assert_equal(metadata.icc.size(), scanned.metadata.icc.size(), "large icc size round-trips");
	assert_equal(true, metadata.icc == scanned.metadata.icc, "large icc bytes round-trip");
}

// An XMP packet carrying a long description, padded so its total size is exact and the JPEG
// marker boundary can be hit rather than approached.
static df::blob make_xmp_packet(const size_t total_size)
{
	const std::string prefix =
		R"(<?xpacket begin="" id="W5M0MpCehiHzreSzNTczkc9d"?><x:xmpmeta xmlns:x="adobe:ns:meta/"><rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#"><rdf:Description xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:description><rdf:Alt><rdf:li xml:lang="x-default">)";
	const std::string suffix =
		R"(</rdf:li></rdf:Alt></dc:description></rdf:Description></rdf:RDF></x:xmpmeta><?xpacket end="w"?>)";

	auto packet = prefix;
	packet.append(total_size - prefix.size() - suffix.size(), 'D');
	packet += suffix;

	return {packet.begin(), packet.end()};
}

// The largest XMP that still fits one APP1 segment must round-trip, so rejecting oversized blocks
// cannot cost a description that was always writable.
static void should_roundtrip_largest_xmp()
{
	files ff;
	const auto load_path = test_files_folder.combine_file("Test.jpg");
	const auto loaded = ff.load(load_path, false);
	const auto surface = loaded.to_surface();

	metadata_parts metadata;
	metadata.xmp = make_xmp_packet(65533u - xmp_signature_len);

	const auto encoded = ff.surface_to_image(surface, metadata, {}, ui::image_format::JPEG);

	mem_read_stream stream(encoded->data());
	const auto scanned = scan_jpg(stream);

	assert_equal(metadata.xmp.size(), scanned.metadata.xmp.size(), "largest xmp size round-trips");
	assert_equal(true, metadata.xmp == scanned.metadata.xmp, "largest xmp bytes round-trip");
}

// Regression: a description that pushes XMP past the 65533-byte marker limit used to be dropped,
// writing an image stripped of its tags with nothing said about it. It must be reported instead,
// and the encoder must stay usable for the next file in the run.
static void should_report_oversized_xmp()
{
	files ff;
	const auto load_path = test_files_folder.combine_file("Test.jpg");
	const auto loaded = ff.load(load_path, false);
	const auto surface = loaded.to_surface();

	metadata_parts oversized;
	oversized.xmp = make_xmp_packet(65534u - xmp_signature_len);

	auto reported = false;

	try
	{
		ff.surface_to_image(surface, oversized, {}, ui::image_format::JPEG);
	}
	catch (const std::exception&)
	{
		reported = true;
	}

	assert_equal(true, reported, "oversized xmp is reported rather than dropped");

	metadata_parts writable;
	writable.xmp = make_xmp_packet(1024u);

	const auto encoded = ff.surface_to_image(surface, writable, {}, ui::image_format::JPEG);
	assert_equal(true, is_valid(encoded), "encoder still usable after a rejected file");

	mem_read_stream stream(encoded->data());
	const auto scanned = scan_jpg(stream);
	assert_equal(true, writable.xmp == scanned.metadata.xmp, "next file keeps its xmp");
}

// Regression: the JPEG decoder must call jpeg_save_markers so read_header can
// recover the embedded EXIF orientation from the APP1 marker.
static void should_read_jpeg_orientation()
{
	const auto load_path = test_files_folder.combine_file("exif-rotated.jpg");
	const auto data = df::blob_from_file(load_path);

	jpeg_decoder_x decoder;
	assert_equal(true, decoder.read_header(data), "read jpeg header");
	assert_equal(ui::orientation::right_top, decoder._orientation_out, "decoder recovers EXIF orientation");
}

// The payload of the first DQT segment, which is the table the encoder quantized with.
static df::blob first_dqt(const df::cspan jpeg)
{
	for (size_t i = 2; i + 4 < jpeg.size;)
	{
		if (jpeg.data[i] != 0xFF) break;

		const auto marker = jpeg.data[i + 1];

		if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7))
		{
			i += 2;
			continue;
		}

		const size_t len = (static_cast<size_t>(jpeg.data[i + 2]) << 8) | jpeg.data[i + 3];
		if (marker == 0xDB) return {jpeg.data + i + 4, jpeg.data + i + 2 + len};
		if (marker == 0xDA) break; // start of scan - no table declarations follow
		i += 2 + len;
	}

	return {};
}

static ui::surface_ptr make_gradient_surface(const int cx, const int cy)
{
	auto surface = std::make_shared<ui::surface>();
	surface->alloc(cx, cy, ui::texture_format::RGB);

	for (auto y = 0; y < cy; ++y)
		for (auto x = 0; x < cx; ++x)
			surface->set_pixel(x, y, ui::rgba((x * 4) & 0xFF, (y * 4) & 0xFF, ((x ^ y) * 4) & 0xFF));

	return surface;
}

// Editing a JPEG must re-encode against the source's own quantization tables, so an untouched block
// quantizes back to itself instead of being re-quantized to whatever the quality slider says.
static void should_reuse_source_jpeg_tables()
{
	files ff;
	const auto surface = make_gradient_surface(64, 64);

	file_encode_params coarse;
	coarse.jpeg_save_quality = 40;
	const auto source = ff.surface_to_image(surface, {}, coarse, ui::image_format::JPEG);
	const auto source_dqt = first_dqt(source->data());
	assert_equal(false, source_dqt.empty(), "source declares a quantization table");

	file_encode_params matched;
	matched.jpeg_save_quality = 95;
	matched.jpeg_source = source->data();
	const auto re_encoded = ff.surface_to_image(surface, {}, matched, ui::image_format::JPEG);
	assert_equal(true, first_dqt(re_encoded->data()) == source_dqt, "re-encode adopts the source tables");

	file_encode_params unmatched;
	unmatched.jpeg_save_quality = 95;
	const auto control = ff.surface_to_image(surface, {}, unmatched, ui::image_format::JPEG);
	assert_equal(false, first_dqt(control->data()) == source_dqt, "quality still applies without a source");
}

// Lossless rotation must refuse rather than trim. Trimming silently drops up to a whole MCU of edge
// pixels the user saw in the preview; refusing sends the save down the re-encode path instead.
static void should_refuse_imperfect_lossless_rotate()
{
	files ff;

	// 4:2:0 chroma puts the MCU grid on 16 pixels, so 20 rows cannot rotate losslessly.
	const auto aligned = ff.surface_to_image(make_gradient_surface(32, 32), {}, {}, ui::image_format::JPEG);
	const auto unaligned = ff.surface_to_image(make_gradient_surface(32, 20), {}, {}, ui::image_format::JPEG);

	jpeg_encoder aligned_encoder;
	const jpeg_decoder_x aligned_decoder;
	assert_equal(false, aligned_decoder.transform(aligned->data(), aligned_encoder, simple_transform::rot_90).empty(),
	             "aligned rotate stays lossless");

	jpeg_encoder unaligned_encoder;
	const jpeg_decoder_x unaligned_decoder;
	assert_equal(true,
	             unaligned_decoder.transform(unaligned->data(), unaligned_encoder, simple_transform::rot_90).empty(),
	             "unaligned rotate refuses rather than trimming");
}

// Offset of the start-of-scan marker, or 0 when there is none.
static size_t sos_offset(const df::cspan jpeg)
{
	for (size_t i = 2; i + 4 < jpeg.size;)
	{
		if (jpeg.data[i] != 0xFF) break;

		const auto marker = jpeg.data[i + 1];

		if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7))
		{
			i += 2;
			continue;
		}

		if (marker == 0xDA) return i;

		i += 2 + ((static_cast<size_t>(jpeg.data[i + 2]) << 8) | jpeg.data[i + 3]);
	}

	return 0;
}

// A JPEG that ends inside its entropy data makes jpeg_read_coefficients suspend and hand back a
// null coefficient array, which transupp indexes straight into a crash. The rotate must refuse -
// and leave both codecs usable, because files holds one decoder and one encoder for every file.
static void should_survive_truncated_lossless_rotate()
{
	files ff;

	const auto whole = ff.surface_to_image(make_gradient_surface(256, 256), {}, {}, ui::image_format::JPEG);
	const auto& data = whole->data();
	const auto sos = sos_offset(data);

	assert_equal(true, sos > 0 && sos + 64 < data.size(), "test image has scan data to truncate");

	const std::vector<uint8_t> truncated(data.data(), data.data() + sos + 64);

	jpeg_encoder encoder;
	const jpeg_decoder_x decoder;

	assert_equal(true,
	             decoder.transform({truncated.data(), truncated.size()}, encoder, simple_transform::rot_90).empty(),
	             "truncated rotate refuses");

	assert_equal(false, decoder.transform(data, encoder, simple_transform::rot_90).empty(),
	             "decoder and encoder stay usable after the refusal");
}

// Every 8-bit YCbCr JPEG belongs on the GPU NV12 path; read_nv12 averages whatever chroma the
// source carries down to one pair per 2x2 block. Only formats it cannot pack take the RGB path.
static bool jpeg_uses_nv12(files& ff, const char* const name)
{
	const auto loaded = ff.load(test_files_folder.combine_file(name), false);
	assert_equal(true, is_valid(loaded.i), "loaded jpeg");

	jpeg_decoder_x decoder;
	assert_equal(true, decoder.read_header(loaded.i->data()), "read jpeg header");

	const auto result = decoder.can_render_nv12();
	decoder.start_decompress(1, result, true);
	decoder.close();

	return result;
}

static void should_render_ycbcr_jpeg_as_nv12()
{
	files ff;

	assert_equal(true, jpeg_uses_nv12(ff, "exif-rotated.jpg"), "4:2:0 renders as nv12");
	assert_equal(true, jpeg_uses_nv12(ff, "Small.jpg"), "4:2:2 renders as nv12");
	assert_equal(false, jpeg_uses_nv12(ff, "cmyk.jpg"), "cmyk avoids nv12");
}

// Fixtures for the deep-precision and transfer-function paths are four flat horizontal bands, so a
// correct decode lands on known 8-bit values and a truncating or unconverted one visibly does not.
static void should_decode_bands(const char* const name, const std::initializer_list<int> expected, const int tolerance)
{
	// The fixtures live in excluded1 so that adding them does not change the indexed item counts.
	files ff;
	const auto loaded = ff.load(test_files_folder.combine("excluded1").combine_file(name), false);
	assert_equal(true, is_valid(loaded.i), std::format("loaded {}", name));

	const auto surface = loaded.to_surface();
	assert_equal(true, is_valid(surface), std::format("decoded {}", name));

	const auto cy = surface->height();
	auto band = 0;

	for (const auto want : expected)
	{
		const auto* const row = surface->pixels_line(cy * band / 4 + cy / 8);
		const auto message = std::format("{} band {}: wanted {}, got {},{},{},{}", name, band, want,
		                                 row[0], row[1], row[2], row[3]);

		for (auto c = 0; c < 3; c++)
		{
			assert_equal(true, std::abs(static_cast<int>(row[c]) - want) <= tolerance, message);
		}

		++band;
	}
}

// 12-bit lossy and 16-bit lossless JPEGs used to fail to load outright.
static void should_decode_deep_precision_jpeg(const char* const name)
{
	should_decode_bands(name, {0, 85, 170, 255}, 2);
}

// Scaling 16-bit samples gives 1, 33, 65, 97 where the old truncation gave 0, 32, 64, 96, so this
// only tells the two apart if it demands the exact value.
static void should_scale_16bit_png()
{
	should_decode_bands("deep16.png", {1, 33, 65, 97}, 0);
}

// gamma.png declares gAMA 1.0, so its linear samples need an ~1/2.2 encode for an sRGB display.
static void should_apply_png_gamma()
{
	should_decode_bands("gamma.png", {0, 136, 186, 224}, 2);
}

static void should_decode_12bit_gray_jpeg()
{
	should_decode_deep_precision_jpeg("deep12gray.jpg");
}

static void should_decode_12bit_colour_jpeg()
{
	should_decode_deep_precision_jpeg("deep12.jpg");
}

static void should_decode_16bit_gray_jpeg()
{
	should_decode_deep_precision_jpeg("deep16gray.jpg");
}

static void should_decode_16bit_colour_jpeg()
{
	should_decode_deep_precision_jpeg("deep16.jpg");
}

// The pixel format is what the properties panel and list rows show, and it is indexed for search.
// Chroma subsampling is a headline property of a JPEG, so it belongs in that name - and it has to
// use the same words HEIF, WebP and video already use or a search for one finds only some of them.
static void should_report_jpeg_chroma_subsampling()
{
	files ff;

	const auto reported = [&ff](const char* const name)
	{
		return ff_scan_file(ff, test_files_folder.combine_file(name)).pixel_format;
	};

	assert_equal("yuv420", reported("exif-rotated.jpg").sv(), "4:2:0 jpeg");
	assert_equal("yuv422", reported("Small.jpg").sv(), "4:2:2 jpeg");
	assert_equal("ycck", reported("cmyk.jpg").sv(), "adobe ycck jpeg");
}

struct webp_chunk
{
	std::string tag;
	df::blob data;
};

static std::vector<webp_chunk> read_webp_chunks(const df::file_path path)
{
	file_read_stream fs;
	assert_equal(true, fs.open(path), "open webp");

	const auto bytes = fs.read_all();
	assert_equal(true, bytes.size() > 12u, "webp larger than the RIFF header");

	std::vector<webp_chunk> result;

	for (auto pos = size_t{12}; pos + 8 <= bytes.size();)
	{
		uint32_t len = 0;
		memcpy(&len, bytes.data() + pos + 4, 4);

		const auto payload = pos + 8;
		if (len > bytes.size() - payload) break; // subtract, so a 32-bit size_t cannot wrap

		result.emplace_back(std::string(reinterpret_cast<const char*>(bytes.data() + pos), 4),
		                    df::blob(bytes.begin() + payload, bytes.begin() + payload + len));
		pos = payload + len + (len & 1);
	}

	return result;
}

// Regression guard for the WebP metadata rewrite. The XMP handler used to re-emit
// chunks grouped by category and then truncate the file, so a save could move the
// XMP packet on top of image or alpha data and destroy the picture. Saving metadata
// must leave every non-XMP chunk byte-identical and in its original position, and
// the image must still decode unchanged.
static void should_preserve_webp_chunks_on_metadata_save()
{
	files ff;
	const auto load_path = test_files_folder.combine_file("lake.webp");
	const auto save_path = _temps.next_path(".webp");

	const auto original = ff.load(load_path, false);
	assert_equal(false, original.is_empty(), "original webp loaded");

	metadata_edits first;
	first.rating = 3;
	ff.update(load_path, save_path, first, {}, {}, false, {});
	const auto after_first = read_webp_chunks(save_path);

	metadata_edits second;
	second.rating = 5;
	ff.update(save_path, second, {}, {}, false, {});
	const auto after_second = read_webp_chunks(save_path);

	assert_equal(after_first.size(), after_second.size(), "chunk count unchanged");
	assert_equal(true, after_first.size() >= 3u, "extended webp has image and metadata chunks");
	assert_equal(true, std::ranges::any_of(after_first, [](const webp_chunk& c) { return c.tag == "XMP "; }),
	             "xmp chunk written");

	for (auto i = size_t{0}; i < after_first.size() && i < after_second.size(); ++i)
	{
		assert_equal(after_first[i].tag, after_second[i].tag, std::format("chunk {} tag", i));

		if (after_first[i].tag != "XMP ")
		{
			assert_equal(true, after_first[i].data == after_second[i].data,
			             std::format("chunk {} {} bytes unchanged", i, after_first[i].tag));
		}
	}

	const auto reloaded = ff.load(save_path, false);
	assert_equal(false, reloaded.is_empty(), "webp still decodes");
	assert_equal(original.i->width(), reloaded.i->width(), "width preserved");
	assert_equal(original.i->height(), reloaded.i->height(), "height preserved");

	const auto scanned = ff_scan_file(ff, save_path);
	assert_equal(5, scanned.to_props()->rating, "rating written");
}

// Regression guard for the WebP save-quality path. The editor maps the user's
// save settings (setting.webp_lossless / setting.webp_quality) onto
// file_encode_params before writing; a swapped or ignored knob would silently
// bloat or degrade the images users save. Verify the encoder honours both:
// lossless produces a substantially larger file than a heavily compressed lossy
// encode of the same pixels, and a higher quality produces a larger file than
// the lowest quality - proving each parameter actually takes effect.
static void should_honor_webp_save_quality()
{
	files ff;
	const auto load_path = test_files_folder.combine_file("Test.jpg");
	const auto source = ff.load(load_path, false);
	assert_equal(false, source.is_empty(), "source loaded");

	const auto lossless_path = _temps.next_path(".webp");
	{
		file_encode_params params;
		params.webp_lossless = true;
		ff.update(load_path, lossless_path, {}, {}, params, false, {});
	}

	const auto low_quality_path = _temps.next_path(".webp");
	{
		file_encode_params params;
		params.webp_lossless = false;
		params.webp_quality = 1;
		ff.update(load_path, low_quality_path, {}, {}, params, false, {});
	}

	const auto high_quality_path = _temps.next_path(".webp");
	{
		file_encode_params params;
		params.webp_lossless = false;
		params.webp_quality = 95;
		ff.update(load_path, high_quality_path, {}, {}, params, false, {});
	}

	const auto lossless_size = platform::file_attributes(lossless_path).size;
	const auto low_quality_size = platform::file_attributes(low_quality_path).size;
	const auto high_quality_size = platform::file_attributes(high_quality_path).size;

	assert_equal(true, lossless_size > 0 && low_quality_size > 0 && high_quality_size > 0, "webp files written");

	// Lossless keeps every detail, so it must be much larger than a heavily
	// compressed lossy encode of the same pixels.
	assert_equal(true, lossless_size > low_quality_size, "webp lossless larger than low quality");

	// Higher quality retains more detail, so it must be larger than the lowest
	// quality - this proves webp_quality is applied (and not treated as a bool).
	assert_equal(true, high_quality_size > low_quality_size, "webp high quality larger than low quality");
}

// The WebP loader used to tag every surface ARGB, which forces the renderer down
// the alpha-blended path for images that are entirely opaque. Verify the decoded
// format follows the bitstream: opaque in, RGB out; alpha in, ARGB out.
static void should_tag_webp_surface_alpha()
{
	const auto opaque_path = test_files_folder.combine_file("lake.webp");
	const auto opaque_data = df::blob_from_file(opaque_path);
	assert_equal(false, opaque_data.empty(), "opaque webp read");

	const auto opaque_surface = load_webp(opaque_data);
	assert_equal(true, is_valid(opaque_surface), "opaque webp decoded");
	assert_equal(true, opaque_surface->format() == ui::texture_format::RGB, "opaque webp surface is RGB");

	const auto opaque_scan = scan_webp(opaque_data, true);
	assert_equal(1u, static_cast<uint32_t>(opaque_scan.frames.size()), "opaque webp frame count");
	assert_equal(true, opaque_scan.frames[0]->format() == ui::texture_format::RGB, "opaque webp scan is RGB");

	const auto transparent = std::make_shared<ui::surface>();
	const auto* const pixels = transparent->alloc(16, 16, ui::texture_format::ARGB);
	assert_equal(true, pixels != nullptr, "alpha surface allocated");

	for (auto y = 0; y < 16; ++y)
	{
		auto* const line = std::bit_cast<ui::color32*>(transparent->pixels_line(y));

		for (auto x = 0; x < 16; ++x)
		{
			// BGRA in memory: alpha ramps across the row so the encode keeps an alpha plane.
			line[x] = (static_cast<ui::color32>(x * 16) << 24) | 0x00FF8040u;
		}
	}

	file_encode_params params;
	params.webp_lossless = true;
	const auto encoded = save_webp(transparent, {}, params);
	assert_equal(true, is_valid(encoded), "alpha webp encoded");

	const auto decoded = load_webp(encoded->data());
	assert_equal(true, is_valid(decoded), "alpha webp decoded");
	assert_equal(true, decoded->format() == ui::texture_format::ARGB, "alpha webp surface is ARGB");
}

static void should_decode_opaque_lossy_webp_as_nv12()
{
	const auto data = df::blob_from_file(test_files_folder.combine_file("lake.webp"));
	const auto rgb = load_webp(data, false);
	const auto nv12 = load_webp(data, true);

	assert_equal(true, is_valid(rgb) && rgb->format() == ui::texture_format::RGB, "webp RGB fallback decoded");
	assert_equal(true, is_valid(nv12) && nv12->format() == ui::texture_format::NV12, "webp NV12 decoded");
	assert_equal(true, nv12->size() * 2 < rgb->size(), "webp NV12 uses less than half the RGB surface memory");
	assert_equal(true, nv12->color_space() == ui::color_space::rec601_limited, "webp NV12 color space");

	const auto converted = std::make_shared<ui::surface>();
	av_scaler scaler;
	assert_equal(true, scaler.convert_yuv_surface(*nv12, converted), "webp NV12 converts for comparison");

	uint64_t total_difference = 0;
	const auto dimensions = rgb->dimensions();

	for (auto y = 0; y < dimensions.cy; ++y)
	{
		const auto* const expected = rgb->pixels_line(y);
		const auto* const actual = converted->pixels_line(y);

		for (auto x = 0; x < dimensions.cx * 4; x += 4)
		{
			for (auto channel = 0; channel < 3; ++channel)
			{
				total_difference += std::abs(static_cast<int>(expected[x + channel]) - actual[x + channel]);
			}
		}
	}

	const auto average_difference = static_cast<double>(total_difference) / (dimensions.cx * dimensions.cy * 3);
	assert_equal(true, average_difference < 3.0,
	             std::format("webp NV12 average RGB difference: {}", average_difference));

	assert_equal(true, !is_valid(save_webp(nv12, {}, {})), "webp encoder rejects NV12 rather than reading it as BGRX");

	files ff;
	const auto image = std::make_shared<ui::image>(df::cspan(data), dimensions, ui::image_format::WEBP,
	                                              ui::orientation::top_left);
	const auto dispatched = ff.image_to_surface(image, {}, true);
	assert_equal(true, is_valid(dispatched) && dispatched->format() == ui::texture_format::NV12,
	             "webp image dispatch preserves NV12");

	const auto target_extent = sizei{32, 32};
	const auto scaled = ff.image_to_surface(image, target_extent, true);
	assert_equal(true, is_valid(scaled) && scaled->format() == ui::texture_format::RGB,
	             "webp NV12 downscale produces target RGB");
	assert_equal(true, ui::scale_dimensions(dimensions, target_extent) == scaled->dimensions(),
	             "webp downscale honors target extent");
}

static void should_refuse_truncated_webp_decode()
{
	const auto data = df::blob_from_file(test_files_folder.combine_file("lake.webp"));
	auto truncated_size = 0_z;

	for (auto size = 12_z; size < data.size(); ++size)
	{
		WebPBitstreamFeatures features;
		if (WebPGetFeatures(data.data(), size, &features) == VP8_STATUS_OK)
		{
			truncated_size = size;
			break;
		}
	}

	assert_equal(true, truncated_size > 0, "truncated webp retains a readable header");
	assert_equal(true, !is_valid(load_webp({data.data(), truncated_size})),
	             "truncated webp does not return an allocated partial surface");
}

static df::blob make_test_animated_webp()
{
	constexpr auto width = 16;
	constexpr auto height = 16;
	WebPAnimEncoderOptions options;
	if (!WebPAnimEncoderOptionsInit(&options)) return {};

	auto* const encoder = WebPAnimEncoderNew(width, height, &options);
	if (!encoder) return {};
	const df::releaser<WebPAnimEncoder> encoder_releaser(encoder, [](auto* i) { WebPAnimEncoderDelete(i); });

	WebPConfig config;
	if (!WebPConfigInit(&config)) return {};
	config.lossless = 1;
	config.quality = 100;

	std::array<uint32_t, width * height> pixels;
	const auto add_frame = [&](const uint32_t color, const int timestamp)
	{
		pixels.fill(color);
		WebPPicture picture;
		if (!WebPPictureInit(&picture)) return false;
		const df::scope_exit free_picture([&picture] { WebPPictureFree(&picture); });
		picture.width = width;
		picture.height = height;
		picture.use_argb = true;
		return WebPPictureImportBGRA(&picture, std::bit_cast<const uint8_t*>(pixels.data()), width * 4) &&
			WebPAnimEncoderAdd(encoder, &picture, timestamp, &config);
	};

	if (!add_frame(0xff102040, 0) || !add_frame(0xffc08020, 100) ||
		!WebPAnimEncoderAdd(encoder, nullptr, 350, nullptr))
	{
		return {};
	}

	WebPData encoded;
	WebPDataInit(&encoded);
	if (!WebPAnimEncoderAssemble(encoder, &encoded)) return {};
	const df::scope_exit clear_encoded([&encoded] { WebPDataClear(&encoded); });
	return {encoded.bytes, encoded.bytes + encoded.size};
}

static void should_bound_and_time_animated_webp()
{
	const auto data = make_test_animated_webp();
	assert_equal(true, !data.empty(), "animated webp encoded");

	const auto decoded = scan_webp(data, true);
	assert_equal(2u, static_cast<uint32_t>(decoded.frames.size()), "animated webp frame count");
	assert_equal(true, std::abs(decoded.frames[0]->time() - 0.1) < 0.001, "animated webp first timestamp");
	assert_equal(true, std::abs(decoded.frames[1]->time() - 0.35) < 0.001, "animated webp second timestamp");

	const auto restore_budget = df::max_decode_bytes;
	const df::scope_exit restore([restore_budget] { df::max_decode_bytes = restore_budget; });
	const auto frame_bytes = 16ll * 16ll * 4ll;
	df::max_decode_bytes = frame_bytes * 3;
	const auto bounded = scan_webp(data, true);
	assert_equal(1u, static_cast<uint32_t>(bounded.frames.size()),
	             "animated webp budget includes two decoder canvases");

	auto corrupt = data.clone();
	auto frame = 0;
	for (auto offset = 12_z; offset + 32 < corrupt.size();)
	{
		const auto chunk_size = static_cast<size_t>(corrupt[offset + 4]) |
			(static_cast<size_t>(corrupt[offset + 5]) << 8) |
			(static_cast<size_t>(corrupt[offset + 6]) << 16) |
			(static_cast<size_t>(corrupt[offset + 7]) << 24);

		if (memcmp(corrupt.data() + offset, "ANMF", 4) == 0 && ++frame == 2)
		{
			corrupt[offset + 32] ^= 0xff;
			break;
		}

		offset += 8 + chunk_size + (chunk_size & 1);
	}

	df::max_decode_bytes = restore_budget;
	const auto malformed = scan_webp(corrupt, true);
	assert_equal(true, malformed.frames.size() < 2, "malformed animated webp terminates on decode failure");
}

// Guards the drawn mark against silent drift. The same artwork is drawn independently by
// tools/generate_store_assets.py for app.ico and the Store assets.
static void should_draw_the_logo()
{
	for (const auto size : {16, 32, 44, 150, 256})
	{
		const auto s = std::make_shared<ui::surface>();
		assert_equal(true, s->alloc(size, size, ui::texture_format::ARGB), "logo surface allocated");
		s->fill_logo();

		const auto last = size - 1;
		assert_equal(0u, s->get_pixel(0, 0), "logo corner is transparent");
		assert_equal(0u, s->get_pixel(last, last), "logo opposite corner is transparent");

		// The four squares sit on the vertical and horizontal axes through the centre.
		const auto mid = size / 2;
		const auto near_edge = std::max(1, size / 8);
		const auto top = s->get_pixel(mid, near_edge);
		const auto bottom = s->get_pixel(mid, last - near_edge);
		const auto left = s->get_pixel(near_edge, mid);
		const auto right = s->get_pixel(last - near_edge, mid);

		// Surface pixels are stored blue first, so ui::get_r reads the blue channel here.
		const auto red_of = [](const ui::color32 c) { return ui::get_b(c); };
		const auto green_of = [](const ui::color32 c) { return ui::get_g(c); };
		const auto blue_of = [](const ui::color32 c) { return ui::get_r(c); };

		assert_equal(true, green_of(top) > red_of(top) && green_of(top) > blue_of(top), "logo top is green");
		assert_equal(true, red_of(bottom) > green_of(bottom) && red_of(bottom) > blue_of(bottom),
		             "logo bottom is red");
		assert_equal(true, red_of(left) > 0x80 && green_of(left) > 0x60 && blue_of(left) < 0x40,
		             "logo left is yellow");
		assert_equal(true, blue_of(right) > red_of(right) && blue_of(right) > green_of(right),
		             "logo right is blue");

		for (const auto c : {top, bottom, left, right})
		{
			assert_equal(255u, ui::get_a(c), "logo square centres are opaque");
		}
	}
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
		"gen.asf",
		"gen.flv",
		"gen.mpg",
		"gen.wav",
		"gen.wmv",
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
		tests.add(std::format("Should update label {}", name), [name] { should_update_label(name); });
		tests.add(std::format("Should update tags {}", name), [name] { should_add_remove_tags(name); });
	}

	tests.add("Should update gps in exif"s, should_update_gps_in_exif);
	tests.add("Issue #134: Should update rating and label for emoji filename"s,
	          should_update_rating_and_label_for_emoji_filename);
	tests.add("Should handle international characters"s, should_handle_international_characters);
	tests.add("Should handle korean characters"s, should_handle_korean_characters);
	tests.add("Should not rewrite unchanged file"s, should_not_rewrite_unchanged_file);
	tests.add("Should edit video metadata in place gizmo.mp4"s,
	          [] { should_edit_video_metadata_in_place("gizmo.mp4"); });
	tests.add("Should edit video metadata in place ipod.mov"s,
	          [] { should_edit_video_metadata_in_place("ipod.mov"); });
	tests.add("Should edit video metadata in place gen.asf"s,
	          [] { should_edit_video_metadata_in_place("gen.asf"); });
	tests.add("Should inject video metadata in place indy.mp4"s,
	          [] { should_inject_video_metadata_in_place("indy.mp4"); });
	// Faststart: moov leads the file with only an 8-byte free box after it, so a first packet
	// cannot grow in place and the handler has to relocate rather than fall back to a copy.
	tests.add("Should inject video metadata in place anamorphic.mp4"s,
	          [] { should_inject_video_metadata_in_place("anamorphic.mp4"); });
	tests.add("Should stage metadata edit Colorblind.mp3"s,
	          [] { should_stage_metadata_edit("Colorblind.mp3"); });
	tests.add("Should stage metadata edit gen.wav"s, [] { should_stage_metadata_edit("gen.wav"); });
	tests.add("Should stage metadata edit Byzantium.avi"s,
	          [] { should_stage_metadata_edit("Byzantium.avi"); });
	tests.add("Should stage metadata edit Test.jpg"s, [] { should_stage_metadata_edit("Test.jpg"); });
	tests.add("Should edit raw sidecar only"s, should_edit_raw_sidecar_only);
	tests.add("Should return written image"s, should_return_written_image);
	tests.add("Should hand over written handle"s, should_hand_over_written_handle);
	tests.add("Should save as with distinct xmp sidecar"s, should_save_as_with_distinct_xmp_sidecar);
	tests.add("Should update exif rating"s, should_update_exif_rating);
	tests.add("Should update formatted description"s, should_update_formatted_text);
	tests.add("Should update synopsis"s, should_update_synopsis);
	tests.add("Should remove shell written tags"s, should_remove_shell_written_tags);

	// Windows Explorer / Media Player tag interop (#123)
	tests.add("Should read mp4 Xtra metadata gizmo.mp4"s, [] { should_read_mp4_xtra_metadata("gizmo.mp4", "xTag1"); });
	tests.add("Should read mp4 Xtra metadata ipod.mov"s, [] { should_read_mp4_xtra_metadata("ipod.mov", "ipad"); });
	tests.add("Should read asf WM/Category multi-value"s, should_read_asf_wm_categories);
	tests.add("Should write windows tags via xmp gizmo.mp4"s, [] { should_write_windows_tags_via_xmp("gizmo.mp4"); });
	tests.add("Should write windows tags via xmp ipod.mov"s, [] { should_write_windows_tags_via_xmp("ipod.mov"); });
	tests.add("Should write windows tags via xmp gen.wmv"s, [] { should_write_windows_tags_via_xmp("gen.wmv"); });

	constexpr std::string_view shell_tag_files[] = {"gizmo.mp4", "ipod.mov", "gen.wmv", "gen.asf"};
	for (auto name : shell_tag_files)
	{
		tests.add(std::format("Should round-trip windows shell tags {}", name),
		          [name] { should_roundtrip_windows_shell_tags(name); });
	}

	constexpr std::string_view shell_unsupported_files[] = {"Byzantium.avi", "gen.mpg"};
	for (auto name : shell_unsupported_files)
	{
		tests.add(std::format("Should handle unsupported shell tags {}", name),
		          [name] { should_handle_unsupported_shell_tags(name); });
	}

	//
	// Bitmap Edit
	//
	tests.add("Should preserve orientation"s, should_preserve_orientation);
	tests.add("Should apply perspective correction"s, should_apply_perspective_correction);
	tests.add("Should fit document correction to controls"s, should_fit_document_correction_to_controls);
	tests.add("Should detect only clear document regions"s, should_detect_only_clear_document_regions);
	tests.add("Should detect photographed document"s, should_detect_photographed_document);
	tests.add("Should detect document at image edge"s, should_detect_document_at_image_edge);
	tests.add("Should detect low contrast document"s, should_detect_low_contrast_document);
	tests.add("Should apply temperature and tint"s, should_apply_temperature_and_tint);
	tests.add("Should crop without resampling"s, should_crop_without_resampling);
	tests.add("Should initialize edit rotation from orientation"s, should_initialize_edit_rotation_from_orientation);
	tests.add("Should build rotated edit preview surface"s, should_build_rotated_edit_preview_surface);
	tests.add("Should build straightened edit preview without black corners"s,
	          should_build_straightened_edit_preview_without_black_corners);
	tests.add("Should not treat initial orientation as an edit"s, should_not_treat_initial_orientation_as_an_edit);
	tests.add("Should round-trip small ICC"s, should_roundtrip_small_icc);
	tests.add("Should round-trip large ICC"s, should_roundtrip_large_icc);
	tests.add("Should round-trip largest XMP"s, should_roundtrip_largest_xmp);
	tests.add("Should report oversized XMP"s, should_report_oversized_xmp);
	tests.add("Should read jpeg orientation"s, should_read_jpeg_orientation);
	tests.add("Should reuse source jpeg tables"s, should_reuse_source_jpeg_tables);
	tests.add("Should refuse imperfect lossless rotate"s, should_refuse_imperfect_lossless_rotate);
	tests.add("Should survive truncated lossless rotate"s, should_survive_truncated_lossless_rotate);
	tests.add("Should render ycbcr jpeg as nv12"s, should_render_ycbcr_jpeg_as_nv12);
	tests.add("Should report jpeg chroma subsampling"s, should_report_jpeg_chroma_subsampling);
	tests.add("Should decode 12bit gray jpeg"s, should_decode_12bit_gray_jpeg);
	tests.add("Should decode 12bit colour jpeg"s, should_decode_12bit_colour_jpeg);
	tests.add("Should decode 16bit gray jpeg"s, should_decode_16bit_gray_jpeg);
	tests.add("Should decode 16bit colour jpeg"s, should_decode_16bit_colour_jpeg);
	tests.add("Should scale 16bit png"s, should_scale_16bit_png);
	tests.add("Should apply png gamma"s, should_apply_png_gamma);
	tests.add("Should resize"s, should_resize);
	tests.add("Should rotate"s, should_rotate);
	tests.add("Should rotate 133"s, should_rotate133);
	tests.add("Should rotate lossless"s, should_rotate_lossless);
	tests.add("Should save .png"s, [] { should_save(".png", true); });
	tests.add("Should save .jpg"s, [] { should_save(".jpg", true); });
	tests.add("Should save .webp"s, [] { should_save(".webp", true); });
	tests.add("Should honor webp save quality"s, should_honor_webp_save_quality);
	tests.add("Should tag webp surface alpha"s, should_tag_webp_surface_alpha);
	tests.add("Should decode opaque lossy webp as nv12"s, should_decode_opaque_lossy_webp_as_nv12);
	tests.add("Should refuse truncated webp decode"s, should_refuse_truncated_webp_decode);
	tests.add("Should bound and time animated webp"s, should_bound_and_time_animated_webp);
	tests.add("Should preserve webp chunks on metadata save"s, should_preserve_webp_chunks_on_metadata_save);
	tests.add("Should convert raw to jpeg"s, should_convert_raw_to_jpeg);
	tests.add("Should draw the logo"s, should_draw_the_logo);
}
