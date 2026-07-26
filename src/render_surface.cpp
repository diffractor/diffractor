// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Image surface management. Handles bitmap memory allocation,
// pixel access, scaling, cropping, and format conversions.

#include "pch.h"

#include "files.h"
#include "util.h"

void ui::surface::clear(const color32 clr) const
{
	for (auto y = 0; y < _dimensions.cy; ++y)
	{
		auto* const line = std::bit_cast<color32*>(_pixels.get() + y * _stride);

		for (int x = 0; x < _dimensions.cx; ++x)
		{
			line[x] = clr;
		}
	}
}

ui::surface_ptr ui::surface::transform(const simple_transform t) const
{
	surface_ptr result;

	const auto cy = _dimensions.cy;
	const auto cx = _dimensions.cx;

	if (t == simple_transform::rot_90)
	{
		result = std::make_shared<surface>();

		if (result->alloc(cy, cx, _format, _orientation, _time))
		{
			for (int y = 0; y < cy; ++y)
			{
				const auto* const line_src = std::bit_cast<const color32*>(_pixels.get() + y * _stride);

				for (int x = 0; x < cx; ++x)
				{
					auto* const line_dst = std::bit_cast<color32*>(
						result->_pixels.get() + (cx - 1 - x) * result->_stride);
					line_dst[y] = line_src[x];
				}
			}
		}
	}
	else if (t == simple_transform::rot_270)
	{
		result = std::make_shared<surface>();

		if (result->alloc(cy, cx, _format, _orientation, _time))
		{
			for (int y = 0; y < cy; ++y)
			{
				const auto* const line_src = std::bit_cast<const color32*>(_pixels.get() + (cy - 1 - y) * _stride);

				for (int x = 0; x < cx; ++x)
				{
					auto* const line_dst = std::bit_cast<color32*>(result->_pixels.get() + x * result->_stride);
					line_dst[y] = line_src[x];
				}
			}
		}
	}
	else if (t == simple_transform::rot_180)
	{
		result = std::make_shared<surface>();

		if (result->alloc(cx, cy, _format, _orientation, _time))
		{
			for (int y = 0; y < cy; ++y)
			{
				const auto* const line_src = std::bit_cast<const color32*>(_pixels.get() + (cy - 1 - y) * _stride);
				auto* const line_dst = std::bit_cast<color32*>(result->_pixels.get() + y * result->_stride);

				for (int x = 0; x < cx; ++x)
				{
					line_dst[x] = line_src[cx - 1 - x];
				}
			}
		}
	}

	return result;
}

void ui::surface::fill_pie(const pointi center, const int radius, const color32 color[64], const color32 color_center,
                           const color32 color_bg) const
{
	const auto outer_radius_limit1 = (radius - 1) * (radius - 1);
	const auto outer_radius_limit2 = radius * radius;
	const auto outer_radius_diff = outer_radius_limit2 - outer_radius_limit1;

	const auto inner_radius = radius / 2;
	const auto inner_radius_limit1 = (inner_radius - 1) * (inner_radius - 1);
	const auto inner_radius_limit2 = inner_radius * inner_radius;
	const auto inner_radius_diff = inner_radius_limit2 - inner_radius_limit1;

	const auto cx = _dimensions.cx;
	const auto cy = _dimensions.cy;


	constexpr float dither_matrix[4][4] = {
		{1.0f / 64.0f, 9.0f / 64.0f, 3.0f / 64.0f, 11.0f / 64.0f},
		{13.0f / 64.0f, 5.0f / 64.0f, 15.0f / 64.0f, 7.0f / 64.0f},
		{4.0f / 64.0f, 12.0f / 64.0f, 2.0f / 64.0f, 10.0f / 64.0f},
		{16.0f / 64.0f, 8.0f / 64.0f, 14.0f / 64.0f, 6.0f / 64.0f}
	};


	for (auto y = 0; y < cy; ++y)
	{
		auto* const line = std::bit_cast<color32*>(_pixels.get() + y * _stride);
		const auto pdy = y - center.y;

		for (auto x = 0; x < cx; ++x)
		{
			const auto pdx = x - center.x;
			const auto r = pdx * pdx + pdy * pdy;

			color32 c;

			if (r < inner_radius_limit1)
			{
				c = color_center;
			}
			else if (r < outer_radius_limit2)
			{
				const auto i1 = (M_PI + atan2(pdy, pdx)) / M_PI * 32.0;
				const auto i2 = (M_PI + atan2(pdy + 1, pdx + 1)) / M_PI * 32.0;

				const auto c1 = color[static_cast<int>(i1) % 64];
				const auto c2 = color[static_cast<int>(i2) % 64];

				c = c1 == c2 ? c1 : lerp(c1, c2, i2 > i1 ? i2 - i1 : i1 - i2);

				if (r > outer_radius_limit1)
				{
					c = lerp(c, color_bg, df::mul_div(r - outer_radius_limit1, 255, outer_radius_diff));
				}
				else if (r < inner_radius_limit2)
				{
					c = lerp(color_center, c, df::mul_div(r - inner_radius_limit1, 255, inner_radius_diff));
				}

				const auto ff = (static_cast<float>(r) / static_cast<float>(outer_radius_limit2) + dither_matrix[x %
					4][y % 4]) * 0.25f;
				c = lighten(c, ff);
			}
			else
			{
				c = color_bg;
			}

			line[x] = c;
		}
	}
}

static simple_transform to_simple_transform(const image_edits& pe)
{
	const auto a = pe.rotation_angle();

	if (df::equiv(a, -90) || df::equiv(a, 270))
	{
		return simple_transform::rot_270;
	}

	if (df::equiv(a, 90) || df::equiv(a, -270))
	{
		return simple_transform::rot_90;
	}

	if (df::equiv(a, 180) || df::equiv(a, -180))
	{
		return simple_transform::rot_180;
	}

	return simple_transform::none;
}

// get_pixel returns transparent black outside the surface, so an interpolated edge sample would drag
// every rotated, straightened or perspective-corrected border toward black. Clamp to the edge instead.
static __forceinline ui::color32 sample_clamped(const ui::surface& src, const int x, const int y)
{
	return src.get_pixel(std::clamp(x, 0, static_cast<int>(src.width()) - 1),
	                     std::clamp(y, 0, static_cast<int>(src.height()) - 1));
}

static __forceinline double catmull_rom_weight(const double t)
{
	const auto a = std::abs(t);
	if (a < 1.0) return ((1.5 * a - 2.5) * a) * a + 1.0;
	if (a < 2.0) return ((-0.5 * a + 2.5) * a - 4.0) * a + 2.0;
	return 0.0;
}

// Catmull-Rom, the same sampler the edit preview draws with, so a saved straighten keeps the detail
// the preview showed rather than the extra softening bilinear leaves behind.
static ui::color32 sample_catmull_rom(const ui::surface& src, const double x, const double y, const bool has_alpha)
{
	const auto left_f = floor(x);
	const auto top_f = floor(y);
	const auto left = static_cast<int>(left_f);
	const auto top = static_cast<int>(top_f);
	const auto offset_x = x - left_f;
	const auto offset_y = y - top_f;

	double weights_x[4];
	double weights_y[4];

	for (auto i = 0; i < 4; ++i)
	{
		weights_x[i] = catmull_rom_weight(offset_x - (i - 1));
		weights_y[i] = catmull_rom_weight(offset_y - (i - 1));
	}

	auto red = 0.0;
	auto green = 0.0;
	auto blue = 0.0;
	auto alpha = 0.0;

	for (auto j = 0; j < 4; ++j)
	{
		for (auto i = 0; i < 4; ++i)
		{
			const auto c = sample_clamped(src, left + i - 1, top + j - 1);
			const auto weight = weights_x[i] * weights_y[j];
			// Filtering straight alpha would let a transparent pixel pull its colour into the result.
			const auto coverage = has_alpha ? weight * (ui::get_a(c) / 255.0) : weight;

			red += ui::get_r(c) * coverage;
			green += ui::get_g(c) * coverage;
			blue += ui::get_b(c) * coverage;
			alpha += coverage;
		}
	}

	if (alpha > 1e-6)
	{
		red /= alpha;
		green /= alpha;
		blue /= alpha;
	}

	return ui::saturate_rgba(df::round(red), df::round(green), df::round(blue),
	                         has_alpha ? df::round(alpha * 255.0) : 255);
}

ui::const_surface_ptr ui::surface::transform(const image_edits& photo_edits) const

{
	const df::cancel_token token;
	return transform(photo_edits, token);
}

ui::const_surface_ptr ui::surface::transform(const image_edits& photo_edits, const df::cancel_token& token) const
{
	if (photo_edits.has_perspective())
	{
		auto canvas = std::make_shared<surface>();

		if (!canvas->alloc(_dimensions, _format)) return {};

		const auto display_angle = to_radian(-photo_edits.crop_bounds(_dimensions).angle());
		const auto angle_sin = sin(display_angle);
		const auto angle_cos = cos(display_angle);
		const auto horizontal = photo_edits.perspective_horizontal() * angle_cos -
			photo_edits.perspective_vertical() * angle_sin;
		const auto vertical = photo_edits.perspective_horizontal() * angle_sin +
			photo_edits.perspective_vertical() * angle_cos;
		const auto has_alpha = _format == texture_format::ARGB;

		for (auto y = 0u; y < canvas->height(); ++y)
		{
			if (token.is_cancelled()) return {};
			auto* const dst_line = std::bit_cast<color32*>(canvas->pixels_line(y));
			const auto normalized_y = (y + 0.5) / canvas->height() - 0.5;

			for (auto x = 0u; x < canvas->width(); ++x)
			{
				const auto normalized_x = (x + 0.5) / canvas->width() - 0.5;
				const auto denominator = 1.0 + horizontal * normalized_x + vertical * normalized_y;
				// A vanishing denominator produces infinite or NaN source coordinates, and the sampling
				// below indexes with them.
				if (std::abs(denominator) < 1e-12) continue;
				const auto source_x = (0.5 + normalized_x / denominator) * _dimensions.cx - 0.5;
				const auto source_y = (0.5 + normalized_y / denominator) * _dimensions.cy - 0.5;

				dst_line[x] = sample_catmull_rom(*this, source_x, source_y, has_alpha);
			}
		}

		auto remaining_edits = photo_edits;
		remaining_edits.perspective_horizontal(0);
		remaining_edits.perspective_vertical(0);
		if (photo_edits.has_crop_bounds())
		{
			remaining_edits.crop_bounds(photo_edits.effective_crop_bounds(_dimensions));
		}
		return canvas->transform(remaining_edits, token);
	}

	const_surface_ptr surface_result;

	const auto rot_angle = photo_edits.rotation_angle();
	const auto is_flip_rotate = !df::is_zero(rot_angle) &&
		df::is_zero(fabs(fmod(rot_angle, 90))) &&
		!photo_edits.has_crop(_dimensions) &&
		!photo_edits.has_scale();

	if (is_flip_rotate)
	{
		surface_result = transform(to_simple_transform(photo_edits));
	}
	else if (!photo_edits.has_rotation() && !photo_edits.has_scale() && photo_edits.has_crop(_dimensions))
	{
		// A crop must not resample. The rectangle arrives from device coordinates and lands on
		// fractional source pixels, so interpolating would shift and soften every pixel it keeps.
		const auto crop = photo_edits.crop_bounds(_dimensions).crop(rectd(0, 0, _dimensions.cx, _dimensions.cy)).
		                              bounding_rect();
		const auto extent = crop.extent().round();
		const auto canvas_extent = sizei(std::clamp(extent.cx, 1, _dimensions.cx),
		                                 std::clamp(extent.cy, 1, _dimensions.cy));
		const auto left = std::clamp(df::round(crop.left()), 0, _dimensions.cx - canvas_extent.cx);
		const auto top = std::clamp(df::round(crop.top()), 0, _dimensions.cy - canvas_extent.cy);

		auto canvas = std::make_shared<surface>();

		if (canvas->alloc(canvas_extent, _format))
		{
			for (auto y = 0; y < canvas_extent.cy; ++y)
			{
				memcpy(canvas->pixels_line(y), _pixels.get() + (top + y) * _stride + left * 4,
				       static_cast<size_t>(canvas_extent.cx) * 4);
			}

			surface_result = std::move(canvas);
		}
	}
	else if (photo_edits.has_crop(_dimensions) || photo_edits.has_scale() || photo_edits.has_rotation())
	{
		const auto crop = photo_edits.crop_bounds(_dimensions).crop(rectd(0, 0, _dimensions.cx, _dimensions.cy));
		const auto angle = -crop.angle();
		const quadd bounds(_dimensions);
		auto aff = affined().translate(-bounds.center_point()).rotate(angle);

		if (photo_edits.has_scale())
		{
			const auto result_dimensions = crop.transform(aff).bounding_rect().extent();
			const auto limit = photo_edits.scale();
			const auto scale = std::min(limit.cx / result_dimensions.Width, limit.cy / result_dimensions.Height);
			aff = aff.scale(scale);
		}

		const auto transformed_crop = crop.transform(aff).bounding_rect();
		aff = aff.translate(-transformed_crop.top_left());

		//const auto dst = bounds.transform(aff);
		const auto inc_aff = aff.invert();
		const auto canvas_extent = transformed_crop.extent().round();

		auto canvas = std::make_shared<surface>();

		if (canvas->alloc(canvas_extent, _format))
		{
			const auto has_alpha = _format == texture_format::ARGB;

			const auto dst_to_src_points0 = inc_aff.transform({0.0, 0.0});
			const auto dst_to_src_points1 = inc_aff.transform({1.0, 0.0});
			const auto dst_to_src_points2 = inc_aff.transform({0.0, 1.0});

			const auto x_dx = dst_to_src_points1.X - dst_to_src_points0.X;
			const auto x_dy = dst_to_src_points1.Y - dst_to_src_points0.Y;
			const auto y_dx = dst_to_src_points2.X - dst_to_src_points0.X;
			const auto y_dy = dst_to_src_points2.Y - dst_to_src_points0.Y;

			for (auto y = 0u; y < canvas->height(); y++)
			{
				auto* const dst_line = std::bit_cast<color32*>(canvas->pixels_line(y));

				for (auto x = 0u; x < canvas->width(); x++)
				{
					const pointd src_pointf = {
						dst_to_src_points0.X + x * x_dx + y * y_dx,
						dst_to_src_points0.Y + x * x_dy + y * y_dy
					};

					const auto leftxf = floor(src_pointf.X);
					const auto leftx = static_cast<int>(leftxf);
					const auto rightx = static_cast<int>(ceil(src_pointf.X));
					const auto topyf = floor(src_pointf.Y);
					const auto topy = static_cast<int>(topyf);
					const auto bottomy = static_cast<int>(ceil(src_pointf.Y));

					if (leftx == rightx && topy == bottomy)
					{
						dst_line[x] = get_pixel(leftx, topy);
					}
					else
					{
						dst_line[x] = sample_catmull_rom(*this, src_pointf.X, src_pointf.Y, has_alpha);
					}
				}
			}

			surface_result = std::move(canvas);
		}
	}
	else
	{
		surface_result = shared_from_this();
	}

	if (photo_edits.has_color_changes())
	{
		color_adjust adjust;
		adjust.color_params(photo_edits.vibrance(), photo_edits.saturation(), photo_edits.darks(),
		                    photo_edits.midtones(),
		                    photo_edits.lights(), photo_edits.contrast(), photo_edits.brightness(),
		                    photo_edits.temperature(), photo_edits.tint());

		auto canvas = std::make_shared<surface>();

		if (canvas->alloc(surface_result->dimensions(), surface_result->format()))
		{
			adjust.apply(surface_result, canvas->pixels(), canvas->stride(), token);
			surface_result = std::move(canvas);
		}
	}

	return surface_result;
}


static constexpr int denom = 100000;

sizei ui::scale_dimensions(const sizei dims, const int limit, const bool dont_scale_up) noexcept
{
	const auto sx = df::mul_div(limit, denom, dims.cx);
	const auto sy = df::mul_div(limit, denom, dims.cy);
	const auto s = std::min(sx, sy);

	if (dont_scale_up && s >= denom)
		return dims;

	return {
		std::max(1, df::mul_div(dims.cx, s, denom)),
		std::max(1, df::mul_div(dims.cy, s, denom))
	};
}

sizei ui::scale_dimensions(const sizei dims, const sizei limit, const bool dont_scale_up) noexcept
{
	if (limit.is_empty()) return dims;

	const auto sx = df::mul_div(limit.cx, denom, dims.cx);
	const auto sy = df::mul_div(limit.cy, denom, dims.cy);
	const auto s = std::min(sx, sy);

	if (dont_scale_up && s >= denom)
		return dims;

	return {
		std::max(1, df::mul_div(dims.cx, s, denom)),
		std::max(1, df::mul_div(dims.cy, s, denom))
	};
}

recti ui::scale_dimensions(const sizei dims, const recti limit, const bool dont_scale_up) noexcept
{
	const auto scaled = scale_dimensions(dims, limit.extent(), dont_scale_up);

	const auto x = (limit.width() - scaled.cx) / 2;
	const auto y = (limit.height() - scaled.cy) / 2;

	return {limit.left + x, limit.top + y, limit.left + x + scaled.cx, limit.top + y + scaled.cy};
}

int ui::calc_scale_down_factor(const sizei dims_in, const sizei size_out) noexcept
{
	if (size_out.cx > 0 || size_out.cy > 0)
	{
		if (dims_in.cx / 8 >= size_out.cx && dims_in.cy / 8 >= size_out.cy)
		{
			return 8;
		}
		if (dims_in.cx / 4 >= size_out.cx && dims_in.cy / 4 >= size_out.cy)
		{
			return 4;
		}
		if (dims_in.cx / 2 >= size_out.cx && dims_in.cy / 2 >= size_out.cy)
		{
			return 2;
		}
	}

	return 1;
}
