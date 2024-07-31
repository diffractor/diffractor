// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2024  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY
//
// Transform code inspired https://www.codeproject.com/Articles/13201/Anti-Aliased-Image-Transformation-Aaform
// AA shape code inspired https://www.codeproject.com/Articles/16600/CTGraphics-Anti-Alias-C-Drawing

#include "pch.h"

#include "files.h"
#include "util.h"

void ui::surface::clear(const color32 clr)
{
	for (auto y = 0; y < _dimensions.cy; ++y)
	{
		auto* const line = std::bit_cast<color32*>(_pixels.get() + (y * _stride));

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
				const auto* const line_src = std::bit_cast<const color32*>(_pixels.get() + (y * _stride));

				for (int x = 0; x < cx; ++x)
				{
					auto* const line_dst = std::bit_cast<color32*>(
						result->_pixels.get() + (((cx - 1) - x) * result->_stride));
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
				const auto* const line_src = std::bit_cast<const color32*>(_pixels.get() + (((cy - 1) - y) * _stride));

				for (int x = 0; x < cx; ++x)
				{
					auto* const line_dst = std::bit_cast<color32*>(result->_pixels.get() + (x * result->_stride));
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
				const auto* const line_src = std::bit_cast<const color32*>(_pixels.get() + ((cy - 1 - y) * _stride));
				auto* const line_dst = std::bit_cast<color32*>(result->_pixels.get() + (y * result->_stride));

				for (int x = 0; x < cx; ++x)
				{
					line_dst[y] = line_src[cx - 1 - x];
				}
			}
		}
	}

	return result;
}

void ui::surface::fill_pie(const pointi center, const int radius, const color32 color[64], const color32 color_center,
	const color32 color_bg)
{
	const auto outer_radius_limit1 = ((radius - 1) * (radius - 1));
	const auto outer_radius_limit2 = (radius * radius);
	const auto outer_radius_diff = outer_radius_limit2 - outer_radius_limit1;

	const auto inner_radius = radius / 2;
	const auto inner_radius_limit1 = ((inner_radius - 1) * (inner_radius - 1));
	const auto inner_radius_limit2 = (inner_radius * inner_radius);
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
		auto* const line = std::bit_cast<color32*>(_pixels.get() + (y * _stride));
		const auto pdy = y - center.y;

		for (auto x = 0; x < cx; ++x)
		{
			const auto pdx = x - center.x;
			const auto r = (pdx * pdx) + (pdy * pdy);

			color32 c;

			if (r < inner_radius_limit1)
			{
				c = color_center;
				const auto ff = ((static_cast<float>(r) / static_cast<float>(outer_radius_limit2)) + dither_matrix[x % 4][y % 4]) * 0.25f;
				c = lighten(c, ff);
			}
			else if (r < outer_radius_limit2)
			{
				const auto i1 = (M_PI + atan2(pdy, pdx)) / M_PI * 32.0;
				const auto i2 = (M_PI + atan2(pdy + 1, pdx + 1)) / M_PI * 32.0;

				const auto c1 = color[static_cast<int>(i1) % 64];
				const auto c2 = color[static_cast<int>(i2) % 64];

				c = (c1 == c2) ? c1 : lerp(c1, c2, (i2 > i1) ? i2 - i1 : i1 - i2);

				if (r > outer_radius_limit1)
				{
					c = lerp(c, color_bg, df::mul_div(r - outer_radius_limit1, 255, outer_radius_diff));
				}
				else if (r < inner_radius_limit2)
				{
					c = lerp(color_center, c, df::mul_div(r - inner_radius_limit1, 255, inner_radius_diff));
				}

				const auto ff = ((static_cast<float>(r) / static_cast<float>(outer_radius_limit2)) + dither_matrix[x % 4][y % 4]) * 0.25f;
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

static __forceinline ui::color32 blend_colors(const ui::color32 start, const ui::color32 end, const double position,
	const bool has_alpha)
{
	const auto pos = df::round(position * 0xff);

	if (has_alpha)
	{
		const auto start_a = ((start >> 24) & 0xff) * (pos ^ 0xff);
		const auto end_a = ((end >> 24) & 0xff) * pos;
		const auto final_a = start_a + end_a;

		if (final_a < 0xff) return 0;

		return (final_a / 0xff) << 24 |
			((((start >> 16) & 0xff) * start_a + (((end >> 16) & 0xff) * end_a)) / final_a) << 16 |
			((((start >> 8) & 0xff) * start_a + (((end >> 8) & 0xff) * end_a)) / final_a) << 8 |
			(((start & 0xff) * start_a + ((end & 0xff) * end_a)) / final_a);
	}
	const auto start_a = 0xff * (pos ^ 0xff);
	const auto end_a = 0xff * pos;
	const auto final_a = start_a + end_a;

	return
		((((start >> 16) & 0xff) * start_a + (((end >> 16) & 0xff) * end_a)) / final_a) << 16 |
		((((start >> 8) & 0xff) * start_a + (((end >> 8) & 0xff) * end_a)) / final_a) << 8 |
		(((start & 0xff) * start_a + ((end & 0xff) * end_a)) / final_a);
}

ui::const_surface_ptr ui::surface::transform(const image_edits& photo_edits) const
{
	const_surface_ptr surface_result;

	static std::atomic_int version;
	const df::cancel_token token(version);

	const auto rot_angle = photo_edits.rotation_angle();
	const auto is_flip_rotate = !df::is_zero(rot_angle) &&
		df::is_zero(fabs(fmod(rot_angle, 90))) &&
		!photo_edits.has_crop(_dimensions) &&
		!photo_edits.has_scale();

	if (is_flip_rotate)
	{
		surface_result = transform(to_simple_transform(photo_edits));
	}
	else if (photo_edits.has_crop(_dimensions) || photo_edits.has_scale() || photo_edits.has_rotation())
	{
		const auto crop = photo_edits.crop_bounds(_dimensions).crop(rectd(0, 0, _dimensions.cx, _dimensions.cy));
		const auto angle = -crop.angle();
		const quadd bounds((_dimensions));
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

			const auto dst_to_src_points0 = inc_aff.transform({ 0.0, 0.0 });
			const auto dst_to_src_points1 = inc_aff.transform({ 1.0, 0.0 });
			const auto dst_to_src_points2 = inc_aff.transform({ 0.0, 1.0 });

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
						const auto topleft = get_pixel(leftx, topy);
						const auto topright = get_pixel(rightx, topy);
						const auto bottomleft = get_pixel(leftx, bottomy);
						const auto bottomright = get_pixel(rightx, bottomy);

						const auto x_offset = src_pointf.X - leftxf;
						const auto top = blend_colors(topleft, topright, x_offset, has_alpha);
						const auto bottom = blend_colors(bottomleft, bottomright, x_offset, has_alpha);

						dst_line[x] = blend_colors(top, bottom, src_pointf.Y - topyf, has_alpha);
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
			photo_edits.lights(), photo_edits.contrast(), photo_edits.brightness());

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

	return { limit.left + x, limit.top + y, limit.left + x + scaled.cx, limit.top + y + scaled.cy };
}

recti ui::scale_dimensions_up(const sizei dims, const recti limit) noexcept
{
	const auto sx = df::mul_div(limit.width(), denom, dims.cx);
	const auto sy = df::mul_div(limit.height(), denom, dims.cy);
	const auto s = std::max(sx, sy);

	sizei scaled;
	scaled.cx = std::max(1, df::mul_div(dims.cx, s, denom));
	scaled.cy = std::max(1, df::mul_div(dims.cy, s, denom));

	const int x = (limit.width() - scaled.cx) / 2;
	const int y = (limit.height() - scaled.cy) / 2;

	return { limit.left + x, limit.top + y, limit.left + x + scaled.cx, limit.top + y + scaled.cy };
}

int ui::calc_scale_down_factor(const sizei dims_in, const sizei size_out) noexcept
{
	if (size_out.cx > 0 || size_out.cy > 0)
	{
		if ((dims_in.cx / 8) >= size_out.cx && (dims_in.cy / 8) >= size_out.cy)
		{
			return 8;
		}
		if ((dims_in.cx / 4) >= size_out.cx && (dims_in.cy / 4) >= size_out.cy)
		{
			return 4;
		}
		if ((dims_in.cx / 2) >= size_out.cx && (dims_in.cy / 2) >= size_out.cy)
		{
			return 2;
		}
	}

	return 1;
}
