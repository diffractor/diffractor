// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Tests for the rendering layer (render*, util_simd) -- software blends, YUV conversion for the software backend, packed-surface area downscaling, surface transforms (resize, rotate) and the drawn logo.

#include "pch.h"
#include "test.h"
#include "util_simd.h"
#include "av_format.h"
#include "render_software.h"
#include "test_fixtures.h"
#include "test_runner.h"
#include "ui_elements.h"

static uint8_t blend_opaque_channel(const uint8_t dest, const float src, const float alpha)
{
	return static_cast<uint8_t>(std::clamp(static_cast<int>(src * alpha + dest * (1.0f - alpha) + 0.5f), 0, 255));
}

static uint8_t blend_opaque_normalized_channel(const uint8_t dest, const float src, const float alpha)
{
	const auto value = src * alpha + (dest / 255.0f) * (1.0f - alpha);
	return static_cast<uint8_t>(std::clamp(static_cast<int>(value * 255.0f + 0.5f), 0, 255));
}

static void should_match_simd_software_blends()
{
#if defined(COMPILE_SIMD_INTRINSIC)
	constexpr size_t max_pixels = 19;
	alignas(16) std::array<uint8_t, max_pixels * 4> original{};
	alignas(16) std::array<uint8_t, max_pixels * 4> source{};
	alignas(16) std::array<uint8_t, max_pixels> coverage{};

	for (size_t i = 0; i < original.size(); ++i) original[i] = static_cast<uint8_t>(i * 47u + 13u);
	for (size_t i = 0; i < source.size(); ++i) source[i] = static_cast<uint8_t>(i * 29u + 7u);
	for (size_t i = 0; i < coverage.size(); ++i) coverage[i] = static_cast<uint8_t>(i * 61u + 3u);

	constexpr std::array<float, 5> alphas = {0.0f, 0.13f, 0.5f, 0.87f, 1.0f};
	constexpr float sb = 37.0f;
	constexpr float sg = 149.0f;
	constexpr float sr = 231.0f;

	for (size_t count = 0; count <= max_pixels; ++count)
	{
		for (const auto alpha : alphas)
		{
			auto check = [&](auto&& scalar, auto&& simd, const std::string_view message)
			{
				auto expected = original;
				auto actual = original;
				for (size_t i = 0; i < count; ++i) scalar(expected.data() + i * 4, i);
				const auto processed = simd(actual.data());
				for (size_t i = processed; i < count; ++i) scalar(actual.data() + i * 4, i);
				assert_equal(true, expected == actual, message);
			};

			auto blend_solid_pixel = [](uint8_t* dest, const float b, const float g, const float r, const float a)
			{
				dest[0] = blend_opaque_normalized_channel(dest[0], b, a);
				dest[1] = blend_opaque_normalized_channel(dest[1], g, a);
				dest[2] = blend_opaque_normalized_channel(dest[2], r, a);
				dest[3] = 255;
			};

			check([&](uint8_t* dest, size_t) { blend_solid_pixel(dest, sb / 255.0f, sg / 255.0f, sr / 255.0f, alpha); },
			      [&](uint8_t* dest)
			      {
				      return blend_solid_opaque_sse2(dest, count, sb / 255.0f, sg / 255.0f,
				                                     sr / 255.0f, alpha);
			      }, "SIMD solid blend");

			check([&](uint8_t* dest, const size_t i)
			      {
				      blend_solid_pixel(dest, sb / 255.0f, sg / 255.0f,
				                        sr / 255.0f, alpha * coverage[i] / 255.0f);
			      },
			      [&](uint8_t* dest)
			      {
				      return blend_glyph_opaque_sse2(dest, coverage.data(), count, sb / 255.0f,
				                                     sg / 255.0f, sr / 255.0f, alpha);
			      }, "SIMD glyph blend");

			for (const auto has_alpha : {false, true})
			{
				check([&](uint8_t* dest, const size_t i)
				      {
					      const auto* src = source.data() + i * 4;
					      const auto pixel_alpha = (has_alpha ? src[3] / 255.0f : 1.0f) * alpha;
					      dest[0] = blend_opaque_channel(dest[0], src[0], pixel_alpha);
					      dest[1] = blend_opaque_channel(dest[1], src[1], pixel_alpha);
					      dest[2] = blend_opaque_channel(dest[2], src[2], pixel_alpha);
					      dest[3] = 255;
				      }, [&](uint8_t* dest)
				      {
					      return blend_bgra_opaque_sse2(dest, source.data(), count, has_alpha, alpha);
				      }, "SIMD BGRA blend");
			}
		}
	}
#endif
}

static void should_convert_yuv_surfaces_for_software_rendering()
{
	auto check_output = [](const ui::surface_ptr& output)
	{
		assert_equal(true, ui::is_valid(output), "converted YUV surface is valid");
		assert_equal(uint32_t{4}, output->width(), "converted YUV width");
		assert_equal(uint32_t{2}, output->height(), "converted YUV height");
		for (auto y = 0; y < 2; ++y)
		{
			const auto* const row = output->pixels_line(y);
			for (auto x = 0; x < 4; ++x)
			{
				const auto* const pixel = row + x * 4;
				const auto expected = x < 2 ? 0 : 255;
				assert_equal(true, std::abs(static_cast<int>(pixel[0]) - expected) <= 2, "YUV blue channel");
				assert_equal(true, std::abs(static_cast<int>(pixel[1]) - expected) <= 2, "YUV green channel");
				assert_equal(true, std::abs(static_cast<int>(pixel[2]) - expected) <= 2, "YUV red channel");
				assert_equal(255, pixel[3], "YUV alpha channel");
			}
		}
	};

	av_scaler scaler;
	const auto output = std::make_shared<ui::surface>();
	ui::surface nv12;
	nv12.alloc(4, 2, ui::texture_format::NV12);
	nv12.color_space(ui::color_space::rec601_limited);
	for (auto y = 0; y < 2; ++y)
	{
		auto* const row = nv12.pixels_line(y);
		row[0] = row[1] = 16;
		row[2] = row[3] = 235;
	}
	auto* const nv12_chroma = nv12.pixels() + nv12.stride() * nv12.height();
	std::fill_n(nv12_chroma, nv12.stride(), uint8_t{128});
	assert_equal(true, scaler.convert_yuv_surface(nv12, output), "convert NV12 surface");
	check_output(output);
	ui::surface_ptr scaled;
	const auto nv12_view = ui::const_surface_ptr(&nv12, [](const ui::surface*)
	{
	});
	assert_equal(true, scaler.scale_surface(nv12_view, scaled, {2, 1}), "scale NV12 surface");
	assert_equal(true, scaled->dimensions() == sizei{2, 1}, "scaled NV12 dimensions");
	assert_equal(true, scaled->format() == ui::texture_format::RGB, "scaled NV12 format");

	ui::surface p010;
	p010.alloc(4, 2, ui::texture_format::P010);
	p010.color_space(ui::color_space::rec601_limited);
	for (auto y = 0; y < 2; ++y)
	{
		auto* const row = std::bit_cast<uint16_t*>(p010.pixels_line(y));
		row[0] = row[1] = 64u << 6;
		row[2] = row[3] = 940u << 6;
	}
	auto* const p010_chroma = std::bit_cast<uint16_t*>(p010.pixels() + p010.stride() * p010.height());
	std::fill_n(p010_chroma, p010.stride() / sizeof(uint16_t), uint16_t{512u << 6});
	assert_equal(true, scaler.convert_yuv_surface(p010, output), "convert P010 surface");
	check_output(output);
	const auto p010_view = ui::const_surface_ptr(&p010, [](const ui::surface*)
	{
	});
	assert_equal(true, scaler.scale_surface(p010_view, scaled, {2, 1}), "scale P010 surface");
	assert_equal(true, scaled->dimensions() == sizei{2, 1}, "scaled P010 dimensions");
	assert_equal(true, scaled->format() == ui::texture_format::RGB, "scaled P010 format");
}

static void should_area_downscale_packed_surfaces()
{
	const auto make_source = [](const int cx, const int cy, const ui::texture_format format)
	{
		auto s = std::make_shared<ui::surface>();
		s->alloc(cx, cy, format, ui::orientation::left_bottom);
		s->color_space(ui::color_space::rec709_limited);
		return s;
	};

	// A flat source must survive a reduction untouched. Any weight run that does not sum to exactly
	// 256 shifts the level here, which is the failure this whole scheme has to rule out.
	{
		const auto src = make_source(97, 61, ui::texture_format::ARGB);

		for (auto y = 0; y < 61; ++y)
		{
			auto* const row = src->pixels_line(y);
			for (auto x = 0; x < 97; ++x)
			{
				row[x * 4 + 0] = 17;
				row[x * 4 + 1] = 134;
				row[x * 4 + 2] = 251;
				row[x * 4 + 3] = 200;
			}
		}

		ui::surface_ptr dst;
		assert_equal(true, ui::area_downscale(src, dst, {13, 9}), "reduce a flat surface");
		assert_equal(true, dst->dimensions() == sizei{13, 9}, "reduced dimensions");
		assert_equal(true, dst->format() == ui::texture_format::ARGB, "reduced format");
		assert_equal(true, dst->orientation() == ui::orientation::left_bottom, "reduced orientation");
		assert_equal(true, dst->color_space() == ui::color_space::rec709_limited, "reduced colour space");

		for (auto y = 0; y < 9; ++y)
		{
			const auto* const row = dst->pixels_line(y);

			for (auto x = 0; x < 13; ++x)
			{
				assert_equal(17, static_cast<int>(row[x * 4 + 0]), "flat blue");
				assert_equal(134, static_cast<int>(row[x * 4 + 1]), "flat green");
				assert_equal(251, static_cast<int>(row[x * 4 + 2]), "flat red");
				assert_equal(200, static_cast<int>(row[x * 4 + 3]), "flat alpha");
			}
		}
	}

	// An exact 2:1 reduction must be the mean of each 2x2 block, so a checkerboard of 0 and 200
	// answers 100 everywhere. Nearest neighbour or a dropped contribution cannot produce that.
	{
		const auto src = make_source(64, 64, ui::texture_format::RGB);

		for (auto y = 0; y < 64; ++y)
		{
			auto* const row = src->pixels_line(y);
			for (auto x = 0; x < 64; ++x)
			{
				const auto v = static_cast<uint8_t>(((x + y) & 1) ? 200 : 0);
				row[x * 4 + 0] = row[x * 4 + 1] = row[x * 4 + 2] = row[x * 4 + 3] = v;
			}
		}

		ui::surface_ptr dst;
		assert_equal(true, ui::area_downscale(src, dst, {32, 32}), "reduce a checkerboard");

		for (auto y = 0; y < 32; ++y)
		{
			const auto* const row = dst->pixels_line(y);
			for (auto x = 0; x < 32; ++x)
			{
				assert_equal(100, static_cast<int>(row[x * 4 + 1]), "checkerboard mean");
			}
		}
	}

	// A horizontal ramp reduced 4:1 must stay monotonic and keep its ends, which a wrong first index
	// or a truncated run would break.
	{
		const auto src = make_source(256, 8, ui::texture_format::RGB);

		for (auto y = 0; y < 8; ++y)
		{
			auto* const row = src->pixels_line(y);
			for (auto x = 0; x < 256; ++x)
			{
				row[x * 4 + 0] = row[x * 4 + 1] = row[x * 4 + 2] = static_cast<uint8_t>(x);
				row[x * 4 + 3] = 255;
			}
		}

		ui::surface_ptr dst;
		assert_equal(true, ui::area_downscale(src, dst, {64, 2}), "reduce a ramp");

		const auto* const row = dst->pixels_line(0);
		assert_equal(2, static_cast<int>(row[0]), "ramp first sample");
		assert_equal(254, static_cast<int>(row[63 * 4]), "ramp last sample");

		auto monotonic = true;
		for (auto x = 1; x < 64; ++x) monotonic = monotonic && row[x * 4] > row[(x - 1) * 4];
		assert_equal(true, monotonic, "ramp stays monotonic");
	}

	// Refusals hand the work back to swscale rather than producing a wrong picture.
	{
		const auto rgb = make_source(8, 8, ui::texture_format::RGB);
		ui::surface_ptr dst;
		assert_equal(false, ui::area_downscale(rgb, dst, {16, 4}), "refuse an enlargement");
		assert_equal(false, ui::area_downscale(rgb, dst, {0, 4}), "refuse an empty extent");

		ui::surface nv12;
		nv12.alloc(8, 8, ui::texture_format::NV12);
		const auto nv12_view = ui::const_surface_ptr(&nv12, [](const ui::surface*)
		{
		});
		assert_equal(false, ui::area_downscale(nv12_view, dst, {4, 4}), "refuse a planar format");
	}

	// Same size on one axis is still a reduction; the identity run must not be rejected or shifted.
	{
		const auto src = make_source(40, 10, ui::texture_format::RGB);

		for (auto y = 0; y < 10; ++y)
		{
			auto* const row = src->pixels_line(y);
			for (auto x = 0; x < 40; ++x)
			{
				row[x * 4 + 0] = row[x * 4 + 1] = row[x * 4 + 2] = row[x * 4 + 3] = static_cast<uint8_t>(
					(x == 20) ? 240 : 10);
			}
		}

		ui::surface_ptr dst;
		assert_equal(true, ui::area_downscale(src, dst, {40, 5}), "reduce one axis only");
		assert_equal(240, static_cast<int>(dst->pixels_line(0)[20 * 4]), "identity axis keeps the column");
		assert_equal(10, static_cast<int>(dst->pixels_line(0)[19 * 4]), "identity axis keeps its neighbour");
	}

	// The AVX2 and SSE2 accumulators must agree byte for byte, or a picture would depend on the CPU
	// it was reduced on. Sizes are deliberately not multiples of the vector widths so both the wide
	// loops and their scalar tails are covered.
	{
		const auto src = make_source(211, 97, ui::texture_format::ARGB);
		auto seed = 0x9e3779b9u;

		for (auto y = 0; y < 97; ++y)
		{
			auto* const row = src->pixels_line(y);

			for (auto x = 0; x < 211 * 4; ++x)
			{
				seed = seed * 1664525u + 1013904223u;
				row[x] = static_cast<uint8_t>(seed >> 24);
			}
		}

		for (const auto extent : {sizei{53, 29}, sizei{211, 12}, sizei{7, 97}, sizei{210, 96}})
		{
			ui::surface_ptr wide;
			ui::surface_ptr baseline;
			assert_equal(true, ui::area_downscale(src, wide, extent), "reduce with the selected width");
			assert_equal(true, ui::area_downscale_baseline(src, baseline, extent), "reduce with the SSE2 baseline");

			auto identical = true;

			for (auto y = 0; y < extent.cy; ++y)
			{
				identical = identical && memcmp(wide->pixels_line(y), baseline->pixels_line(y),
				                                static_cast<size_t>(extent.cx) * 4u) == 0;
			}

			assert_equal(true, identical, std::format("SIMD widths agree at {}x{}", extent.cx, extent.cy));
		}
	}
}

static void should_estimate_decode_cost()
{
	// The estimate reads only the header fields, but an image with no bytes counts as empty.
	const auto make_image = [](const ui::image_format format)
	{
		df::blob bytes;
		bytes.resize(16);
		return std::make_shared<ui::image>(std::move(bytes), sizei{4000, 3000}, format,
		                                   ui::orientation::top_left);
	};

	const auto png = make_image(ui::image_format::PNG);
	assert_equal(4000ll * 3000ll * 4, static_cast<uint64_t>(
		             files::estimate_decode_bytes(png, {400, 300})),
	             "a PNG builds the whole frame however small a target is asked for");

	const auto jpeg = make_image(ui::image_format::JPEG);
	assert_equal(500ll * 375ll * 4, static_cast<uint64_t>(
		             files::estimate_decode_bytes(jpeg, {400, 300})),
	             "libjpeg reduces by up to 1/8 while decoding");
	assert_equal(4000ll * 3000ll * 4, static_cast<uint64_t>(
		             files::estimate_decode_bytes(jpeg, {4000, 3000})),
	             "a full-size request costs a JPEG its whole frame");

	assert_equal(1000ll * 1000ll * 4, static_cast<uint64_t>(
		             files::estimate_decode_bytes(sizei{1000, 1000})),
	             "a bare source size costs four bytes a pixel");
}

static void should_refuse_over_budget_sources()
{
	const auto restore_budget = df::max_decode_bytes;
	const df::scope_exit restore([restore_budget] { df::max_decode_bytes = restore_budget; });

	df::max_decode_bytes = 64ll * 1024ll * 1024ll; // exactly 4096 x 4096

	load_diagnostic fits;
	assert_equal(false, reject_over_budget_source(&fits, {4096, 4096}, "test"), "a source at the budget is decoded");
	assert_equal(false, fits.over_budget, "a source that fits is not flagged");
	assert_equal(4096, fits.source_dimensions.cx, "the size is reported whether or not it fits");

	load_diagnostic refused;
	assert_equal(true, reject_over_budget_source(&refused, {4097, 4096}, "test"), "one column over is refused");
	assert_equal(true, refused.over_budget, "the refusal reaches the caller");
	assert_equal(4097, refused.source_dimensions.cx, "the refused size is reported so it can be shown");

	assert_equal(true, files::exceeds_decode_budget(sizei{8000, 8000}), "the predicate agrees with the loader");
	assert_equal(true, reject_over_budget_source(nullptr, {8000, 8000}, "test"),
	             "a caller that wants no diagnostic is still told to refuse");
}

static void should_animate_alpha_between_values()
{
	// The gate is off when the CPU software backend is active, so force the animated
	// behaviour under test and restore whatever the running renderer selected.
	const auto restore_animations = ui::animations_enabled;
	const df::scope_exit restore_scope([restore_animations] { ui::animations_enabled = restore_animations; });
	ui::animations_enabled = true;

	ui::animate_alpha alpha;
	alpha.reset(0.0f, 1.0f);
	assert_equal(0.0f, alpha.val(), "fade-in starts at zero");
	assert_equal(1.0f, alpha.target(), "fade-in targets one");
	assert_equal(true, alpha.step(), "fade-in advances");
	assert_equal(true, alpha.val() > 0.0f && alpha.val() < 1.0f, "fade-in interpolates");

	alpha.target(0.0f);
	assert_equal(true, alpha.val() > 0.0f, "retarget preserves current value");
	assert_equal(true, alpha.step(), "fade-out advances");

	for (auto i = 0; i < 100; ++i) alpha.step();
	assert_equal(0.0f, alpha.val(), "fade-out reaches target");
	assert_equal(false, alpha.step(), "completed animation stops");

	alpha.reset(0.0005f, 0.0f);
	assert_equal(true, alpha.step(), "final snap requests a frame");
	assert_equal(0.0f, alpha.val(), "final snap reaches target");
	assert_equal(false, alpha.step(), "final snap completes once");
}

// A fade used to advance a fixed fraction per frame, so the same fade ran twice as fast on a 120Hz
// display as on a 60Hz one. It now advances by elapsed time, so equal wall-clock time must leave it
// in the same place whatever the refresh rate that produced it.
static void should_fade_at_the_same_rate_on_any_refresh_rate()
{
	const auto restore_animations = ui::animations_enabled;
	const auto restore_factor = ui::animation_step_factor;
	const df::scope_exit restore_scope([restore_animations, restore_factor]
	{
		ui::animations_enabled = restore_animations;
		ui::animation_step_factor = restore_factor;
	});
	ui::animations_enabled = true;

	const auto fade_for = [](const int frames_per_second, const double seconds)
	{
		const auto dt = 1.0 / frames_per_second;
		ui::animation_step_factor = static_cast<float>(1.0 - std::exp(-ui::alpha_fade_rate * dt));

		ui::animate_alpha alpha;
		alpha.reset(0.0f, 1.0f);

		for (auto i = 0; i < static_cast<int>(seconds * frames_per_second); ++i) alpha.step();

		return alpha.val();
	};

	constexpr auto quarter_second = 0.25;
	const auto at_60 = fade_for(60, quarter_second);
	const auto at_120 = fade_for(120, quarter_second);
	const auto at_30 = fade_for(30, quarter_second);

	assert_equal(true, at_60 > 0.1f && at_60 < 0.999f, "a quarter second is mid-fade");
	assert_equal(true, std::abs(at_120 - at_60) < 0.02f, "120Hz matches 60Hz at the same elapsed time");
	assert_equal(true, std::abs(at_30 - at_60) < 0.02f, "30Hz matches 60Hz at the same elapsed time");

	// The replaced constant: a 60Hz frame must still close a third of the remaining distance, so
	// nothing about the feel of a fade changes on the refresh rate almost every display reports.
	ui::animation_step_factor = static_cast<float>(1.0 - std::exp(-ui::alpha_fade_rate / 60.0));
	assert_equal(true, std::abs(ui::animation_step_factor - 0.333f) < 0.001f, "60Hz reproduces the old step");
}

static void should_skip_alpha_animation_when_disabled()
{
	// CPU software rendering cannot afford per-frame fades: every alpha reaches its target
	// immediately so draws stay on the opaque fast paths and no extra frames are requested.
	const auto restore_animations = ui::animations_enabled;
	const df::scope_exit restore_scope([restore_animations] { ui::animations_enabled = restore_animations; });
	ui::animations_enabled = false;

	ui::animate_alpha alpha;
	alpha.reset(0.0f, 1.0f);
	assert_equal(1.0f, alpha.val(), "fade-in starts complete");
	assert_equal(false, alpha.step(), "fade-in requests no frame");

	alpha.target(0.0f);
	assert_equal(0.0f, alpha.val(), "fade-out completes immediately");
	assert_equal(false, alpha.step(), "fade-out requests no frame");
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

// The neutral parameter set is the one a picture with no edits is rendered through, so any drift in
// the tone curve, the YUV round trip or the rounding shows up here as a picture the app quietly
// altered. Truncating instead of rounding in adjust_color moves every channel a level and fails this.
static void should_leave_colour_unchanged_when_neutral()
{
	const auto src = std::make_shared<ui::surface>();
	assert_equal(true, src->alloc(16, 4, ui::texture_format::ARGB), "neutral source allocated");

	for (auto y = 0; y < 4; ++y)
	{
		auto* const row = src->pixels_line(y);

		for (auto x = 0; x < 16; ++x)
		{
			row[x * 4 + 0] = static_cast<uint8_t>(x * 17);
			row[x * 4 + 1] = static_cast<uint8_t>(255 - x * 17);
			row[x * 4 + 2] = static_cast<uint8_t>((x * 37 + y * 11) & 0xff);
			row[x * 4 + 3] = 255;
		}
	}

	const auto dst = std::make_shared<ui::surface>();
	assert_equal(true, dst->alloc(16, 4, ui::texture_format::ARGB), "neutral destination allocated");

	ui::color_adjust adjust;
	adjust.color_params(0, 0, 0, 0, 0, 0, 0);
	adjust.apply(src, dst->pixels(), dst->stride(), {});

	auto worst = 0;
	auto bias = 0;
	auto samples = 0;

	for (auto y = 0; y < 4; ++y)
	{
		const auto* const in = src->pixels_line(y);
		const auto* const out = dst->pixels_line(y);

		for (auto i = 0; i < 16 * 4; ++i)
		{
			const auto delta = static_cast<int>(out[i]) - static_cast<int>(in[i]);
			worst = std::max(worst, std::abs(delta));
			bias += delta;
			++samples;
		}
	}

	// One level of slack for the RGB -> YUV -> RGB round trip; two would hide a real curve error.
	assert_equal(true, worst <= 1, "neutral adjustment leaves the picture alone");

	// The residual must also be unbiased. A worst-case bound alone cannot see truncation, which costs
	// at most one level but costs it in the same direction on every pixel, darkening the whole picture.
	assert_equal(true, std::abs(bias) * 8 <= samples, "the neutral residual is not biased in one direction");
}

// The GPU preview and the saved result must agree: populate_texture_transform is a decimation of the
// same curve apply() uses, so a preview that does not track it shows the user one picture and commits
// another. The tolerance here is a level either side of the byte quantisation -- it catches a curve
// that is missing, stale, inverted or wrongly scaled, not a one-bin sampling offset.
static void should_match_the_preview_curve_to_the_applied_curve()
{
	// A neutral adjustment must leave the preview at the identity ramp the transform is born with,
	// or an unedited picture is previewed through a curve.
	{
		ui::color_adjust neutral;
		neutral.color_params(0, 0, 0, 0, 0, 0, 0);

		ui::texture_transform transform;
		const ui::texture_transform identity;
		neutral.populate_texture_transform(transform);

		auto worst = 0.0f;

		for (auto index = 0; index < ui::texture_transform::curve_len; ++index)
		{
			worst = std::max(worst, std::abs(transform.curve[index] - identity.curve[index]));
		}

		assert_equal(true, worst < 0.01f, "a neutral adjustment previews as the identity curve");
	}

	ui::color_adjust adjust;
	adjust.color_params(0, 0, 0.5, 0.0, -0.5, 0.4, 0.1);

	ui::texture_transform transform;
	adjust.populate_texture_transform(transform);

	assert_equal(true, transform.has_color_changes, "an adjusted curve reports colour changes");

	// It must also no longer be the identity, which is what a preview that was never repopulated
	// would still be showing.
	{
		const ui::texture_transform identity;
		assert_equal(false, transform.curve == identity.curve, "an adjusted curve is not the identity");
	}

	// A grey ramp isolates the curve: with no saturation change the output luma is the curve itself.
	const auto src = std::make_shared<ui::surface>();
	assert_equal(true, src->alloc(ui::texture_transform::curve_len, 1, ui::texture_format::ARGB),
	             "ramp source allocated");

	auto* const row = src->pixels_line(0);

	for (auto x = 0; x < ui::texture_transform::curve_len; ++x)
	{
		row[x * 4 + 0] = static_cast<uint8_t>(x);
		row[x * 4 + 1] = static_cast<uint8_t>(x);
		row[x * 4 + 2] = static_cast<uint8_t>(x);
		row[x * 4 + 3] = 255;
	}

	const auto dst = std::make_shared<ui::surface>();
	assert_equal(true, dst->alloc(ui::texture_transform::curve_len, 1, ui::texture_format::ARGB),
	             "ramp destination allocated");
	adjust.apply(src, dst->pixels(), dst->stride(), {});

	const auto* const out = dst->pixels_line(0);
	auto worst = 0;

	for (auto x = 0; x < ui::texture_transform::curve_len; ++x)
	{
		const auto previewed = df::round(transform.curve[x] * 255.0);
		worst = std::max(worst, std::abs(previewed - static_cast<int>(out[x * 4 + 1])));
	}

	assert_equal(true, worst <= 2, "the preview curve tracks the curve apply uses");

	// Monotonic, because a tone curve that folds back inverts contrast somewhere in the picture.
	for (auto x = 1; x < ui::texture_transform::curve_len; ++x)
	{
		assert_equal(true, transform.curve[x] >= transform.curve[x - 1], "the tone curve never folds back");
	}
}

// Full desaturation is the one adjustment with an answer that can be stated exactly, so it pins the
// chroma path independently of the tone curve.
static void should_desaturate_to_grey()
{
	const auto src = std::make_shared<ui::surface>();
	assert_equal(true, src->alloc(8, 1, ui::texture_format::ARGB), "chroma source allocated");

	auto* const row = src->pixels_line(0);

	for (auto x = 0; x < 8; ++x)
	{
		row[x * 4 + 0] = static_cast<uint8_t>(x * 30);
		row[x * 4 + 1] = static_cast<uint8_t>(200 - x * 20);
		row[x * 4 + 2] = static_cast<uint8_t>(40 + x * 25);
		row[x * 4 + 3] = 255;
	}

	const auto dst = std::make_shared<ui::surface>();
	assert_equal(true, dst->alloc(8, 1, ui::texture_format::ARGB), "chroma destination allocated");

	ui::color_adjust adjust;
	adjust.color_params(0, -1.0, 0, 0, 0, 0, 0);
	adjust.apply(src, dst->pixels(), dst->stride(), {});

	const auto* const out = dst->pixels_line(0);

	for (auto x = 0; x < 8; ++x)
	{
		const auto b = static_cast<int>(out[x * 4 + 0]);
		const auto g = static_cast<int>(out[x * 4 + 1]);
		const auto r = static_cast<int>(out[x * 4 + 2]);

		assert_equal(true, std::abs(r - g) <= 1 && std::abs(g - b) <= 1,
		             "saturation of -1 leaves every pixel grey");
		assert_equal(255u, static_cast<uint32_t>(out[x * 4 + 3]), "alpha survives desaturation");
	}
}

static void should_rasterise_tiles_identically_to_the_whole_surface()
{
	const auto probe = ui::probe_software_rasterizer();

	assert_equal(true, probe.tiles > 1, "the scene was split across tiles");
	assert_equal(true, probe.painted_pixels > 1000, "the scene painted something to compare");
	assert_equal(0, probe.mismatched_pixels, "tiled rasterisation matches the whole surface");
}

// The backend parity contract says the same scene is the same picture everywhere, and nothing
// checked that across a machine: probe_software_rasterizer compares two renderings inside one
// process, which cannot see one platform disagreeing with another. These samples are that artifact.
//
// Compared within a tolerance, not hashed. An exact digest of this scene is stable across MSVC and
// GCC but *not* across MSVC Debug and Release, because the rasterizer is float arithmetic an
// optimiser may reassociate - so an exact comparison fails for a reason this code does not own. A
// structural break, which is what the gate is for, moves a sample far further than a rounding step:
// a transposed, mirrored or channel-swapped blit, or a blend against the wrong background, changes
// whole channels.
static void should_rasterise_one_scene_to_one_answer_on_every_platform()
{
	// Recorded 2026-08-16 from MSVC x64 Debug, and holding unchanged under MSVC x64 Release and
	// GCC 13 x86-64 in both configurations. Re-record from a run on both platforms when the scene
	// or a primitive changes.
	// The first and last samples are the untouched fill, which is how the scene is pinned to
	// painting only inside itself.
	static constexpr std::array<uint32_t, ui::rasterizer_sample_count> recorded{
		0x40404040, 0xff543e8b, 0xff3366e6, 0xff8344a1, 0xff8344a1, 0xffe6d972,
		0xff4e4b8e, 0xff2d3d7c, 0xff6ba8a5, 0xff719d69, 0xff6c7561, 0xff869a5a,
		0xff5e954b, 0xff56a764, 0xff406e73, 0xff26377e, 0xff838387, 0xff5b8773,
		0xff3bbe71, 0xff84a37b, 0xff9b888c, 0xff40703f, 0xff5c7440, 0x40404040,
	};

	constexpr int tolerance = 3;

	const auto probe = ui::probe_software_rasterizer();

	const auto format = [](const std::array<uint32_t, ui::rasterizer_sample_count>& s)
	{
		std::string result;
		for (const auto v : s) result += std::format("0x{:08x}, ", v);
		return result;
	};

	auto within = true;

	for (auto i = 0; i < ui::rasterizer_sample_count; ++i)
	{
		for (auto shift = 0; shift < 32; shift += 8)
		{
			const auto expected = static_cast<int>((recorded[i] >> shift) & 0xff);
			const auto actual = static_cast<int>((probe.samples[i] >> shift) & 0xff);
			if (std::abs(expected - actual) > tolerance) within = false;
		}
	}

	// Compared as text only once a sample is out of tolerance, so the failure carries the whole
	// vector and can be pasted back as the new record rather than reassembled a channel at a time.
	assert_equal(format(recorded), within ? format(recorded) : format(probe.samples),
	             "the reference scene rasterises to the recorded pixels");
}

void register_render_tests(view_state& state, test_registry& tests)
{
	tests.add("Should match SIMD software blends"s, should_match_simd_software_blends);
	tests.add("Should rasterise tiles identically to the whole surface"s,
	          should_rasterise_tiles_identically_to_the_whole_surface);
	tests.add("Should rasterise one scene to one answer on every platform"s,
	          should_rasterise_one_scene_to_one_answer_on_every_platform);
	tests.add("Should convert YUV surfaces for software rendering"s,
	          should_convert_yuv_surfaces_for_software_rendering);
	tests.add("Should area downscale packed surfaces"s, should_area_downscale_packed_surfaces);
	tests.add("Should estimate decode cost"s, should_estimate_decode_cost);
	tests.add("Should refuse over budget sources"s, should_refuse_over_budget_sources);
	tests.add("Should animate alpha between values"s, should_animate_alpha_between_values);
	tests.add("Should fade at the same rate on any refresh rate"s, should_fade_at_the_same_rate_on_any_refresh_rate);
	tests.add("Should skip alpha animation when disabled"s, should_skip_alpha_animation_when_disabled);

	//
	// Colour adjustment
	//
	tests.add("Should leave colour unchanged when neutral"s, should_leave_colour_unchanged_when_neutral);
	tests.add("Should match the preview curve to the applied curve"s,
	          should_match_the_preview_curve_to_the_applied_curve);
	tests.add("Should desaturate to grey"s, should_desaturate_to_grey);

	//
	// Surface transforms
	//
	tests.add("Should resize"s, should_resize);
	tests.add("Should rotate"s, should_rotate);
	tests.add("Should rotate 133"s, should_rotate133);
	tests.add("Should draw the logo"s, should_draw_the_logo);
}
