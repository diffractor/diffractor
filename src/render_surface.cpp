// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
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

using aaf_callback = bool(*)(double);
using pixel_setter = std::function<void(int x, int y, float d)>;

static void draw_line(int x1, int y1, int x2, int y2, const pixel_setter& set)
{
	// Calculate line params
	const int dx = (x2 - x1);
	const int dy = (y2 - y1);

	// Set start pixel
	set(x1, y1, 1.0f);

	// X-dominant line
	if (abs(dx) > abs(dy))
	{
		// Ex-change line end points
		if (dx < 0)
		{
			std::swap(x1, x2);
			std::swap(y1, y2);
		}

		const auto k = static_cast<float>(dy) / static_cast<float>(dx);

		// Set middle pixels		
		float yt = static_cast<float>(y1) + k;

		for (auto xs = x1 + 1; xs < x2; xs++)
		{
			const auto distance = yt - static_cast<int>(yt);

			set(xs, static_cast<int>(yt), 1.0f - distance);
			set(xs, static_cast<int>(yt) + 1, distance);

			yt += k;
		}
	}
	// Y-dominant line
	else
	{
		// Ex-change line end points
		if (dy < 0)
		{
			std::swap(x1, x2);
			std::swap(y1, y2);
		}

		const auto k = static_cast<float>(dx) / static_cast<float>(dy);

		// Set middle pixels
		float xt = static_cast<float>(x1) + k;

		for (auto ys = y1 + 1; ys < y2; ys++)
		{
			const auto distance = xt - static_cast<int>(xt);

			set(static_cast<int>(xt), ys, 1.0f - distance);
			set(static_cast<int>(xt) + 1, ys, distance);

			xt += k;
		}
	}

	// Set end pixel
	set(x2, y2, 1.0);
}

static void draw_ellipse(int x1, int y1, int x2, int y2, const pixel_setter& set)
{
	const auto centerX = (x1 + x2) / 2;
	const auto centerY = (y1 + y2) / 2;
	const auto dx = (x2 - x1);
	const auto dy = (y2 - y1);
	const auto radiusX = static_cast<int>(fabs(dx) / 2.0f);
	const auto radiusY = static_cast<int>(fabs(dy) / 2.0f);
	const auto rel = static_cast<float>(sqrt(radiusX * radiusX + radiusY * radiusY)) / static_cast<float>(radiusY);

	if (x2 < x1)
	{
		std::swap(x1, x2);
		std::swap(y1, y2);
	}

	for (auto xs = -radiusX; xs <= radiusX; xs++)
	{
		const auto yt = radiusY * sqrt(1 - static_cast<float>(xs * xs) / static_cast<float>(radiusX * radiusX));
		const auto distance = yt - static_cast<int>(yt);

		set(centerX + xs, centerY + static_cast<int>(yt), 1.0f - distance);
		set(centerX + xs, centerY + static_cast<int>(yt) + 1, distance);
		set(centerX - xs, centerY - static_cast<int>(yt), 1.0f - distance);
		set(centerX - xs, centerY - static_cast<int>(yt) - 1, distance);
	}

	for (auto ys = -static_cast<int>(static_cast<float>(radiusY) / rel); ys <= static_cast<int>(static_cast<float>(
		     radiusY) / rel); ys++)
	{
		const auto xt = radiusX * sqrt(1 - static_cast<float>(ys * ys) / static_cast<float>(radiusY * radiusY));
		const auto distance = xt - static_cast<int>(xt);

		set(centerX + static_cast<int>(xt), centerY + ys, 1.0f - distance);
		set(centerX + static_cast<int>(xt) + 1, centerY + ys, distance);
		set(centerX - static_cast<int>(xt), centerY - ys, 1.0f - distance);
		set(centerX - static_cast<int>(xt) - 1, centerY - ys, distance);
	}
}

static void draw_arc(const int x1, const int y1, const int x2, const int y2, float startAngle, const float endAngle,
                     const pixel_setter& set)
{
	const auto centerX = (x1 + x2) / 2;
	const auto centerY = (y1 + y2) / 2;
	const auto dx = (x2 - x1);
	const auto dy = (y2 - y1);
	const auto radiusX = (abs(dx) / 2);
	const auto radiusY = (abs(dy) / 2);

	float angle = 0.0f;
	while ((angle + 90.0f) < startAngle)
		angle += 90.0f;
	while (angle < endAngle)
	{
		if (startAngle >= angle)
		{
			const auto eAngle = endAngle <= angle + 90.0f ? endAngle : angle + 90.0f;
			const auto startX = df::round(radiusX * cos((startAngle / 180.0f) * 3.1415f));
			const auto endX = static_cast<int>(radiusX * cos((eAngle / 180.0f) * 3.1415f));
			const auto deltaX = endX - startX != 0 ? static_cast<int>((endX - startX) / fabs(endX - startX)) : 0;
			const auto startY = df::round(radiusY * sin((startAngle / 180.0f) * 3.1415f));
			const auto endY = static_cast<int>(radiusY * sin((eAngle / 180.0f) * 3.1415f));
			const auto deltaY = endY - startY != 0 ? static_cast<int>((endY - startY) / fabs(endY - startY)) : 0;

			if (deltaX != 0)
			{
				int oldY = startY;
				int xs = startX;

				while (xs != endX)
				{
					const auto yt = radiusY * sqrt(
						1.0f - static_cast<float>(xs * xs) / static_cast<float>(radiusX * radiusX));
					const auto distance = yt - static_cast<int>(yt);

					if (abs(oldY - static_cast<int>(yt)) < 2)
					{
						if (deltaX < 0)
						{
							if (deltaY > 0)
								set(centerX + xs, centerY + static_cast<int>(yt), 1.0f - distance);
							else
								set(centerX + xs, centerY + static_cast<int>(yt), 1.0f - distance);
						}
						else
						{
							if (deltaY < 0)
								set(centerX + xs, centerY - static_cast<int>(yt), 1.0f - distance);
							else
								set(centerX + xs, centerY - static_cast<int>(yt), 1.0f - distance);
						}

						if (deltaX < 0)
						{
							if (deltaY > 0)
								set(centerX + xs, centerY + static_cast<int>(yt) + 1, distance);
							else
								set(centerX + xs, centerY + static_cast<int>(yt) + 1, distance);
						}
						else
						{
							if (deltaY < 0)
								set(centerX + xs, centerY - static_cast<int>(yt) - 1, distance);
							else
								set(centerX + xs, centerY - static_cast<int>(yt) - 1, distance);
						}
					}

					oldY = static_cast<int>(yt);
					xs += deltaX;
				}
			}

			if (deltaY != 0)
			{
				int oldX = startX;
				int ys = startY;

				while (ys != endY)
				{
					const auto xt = radiusX * sqrt(
						1.0f - static_cast<float>(ys * ys) / static_cast<float>(radiusY * radiusY));
					const int ixt = static_cast<int>(xt);
					const auto distance = xt - ixt;

					if (abs(oldX - ixt) < 2)
					{
						if (deltaX < 0)
						{
							if (deltaY > 0)
								set(centerX + ixt, centerY + ys, 1.0f - distance);
							else
								set(centerX - ixt, centerY + ys, 1.0f - distance);
						}
						else
						{
							if (deltaY < 0)
								set(centerX - ixt, centerY + ys, 1.0f - distance);
							else
								set(centerX + ixt, centerY + ys, 1.0f - distance);
						}
						if (deltaX < 0)
						{
							if (deltaY > 0)
								set(centerX + ixt + 1, centerY + ys, distance);
							else
								set(centerX - ixt - 1, centerY + ys, distance);
						}
						else
						{
							if (deltaY < 0)
								set(centerX - ixt - 1, centerY + ys, distance);
							else
								set(centerX + ixt + 1, centerY + ys, distance);
						}
					}

					oldX = ixt;
					ys += deltaY;
				}
			}
		}

		angle += 90.0f;
		startAngle = angle;
	}
}

static void draw_pie(const int x1, const int y1, const int x2, const int y2, const float startAngle,
                     const float endAngle, const pixel_setter& set)
{
	const int centerX = (x1 + x2) / 2;
	const int centerY = (y1 + y2) / 2;
	const int dx = (x2 - x1);
	const int dy = (y2 - y1);
	const int radiusX = (abs(dx) / 2);
	const int radiusY = (abs(dy) / 2);

	const int startX = static_cast<int>(radiusX * cos((startAngle / 180.0f) * 3.1415f));
	const int startY = static_cast<int>(radiusY * sin((startAngle / 180.0f) * 3.1415f));
	int endX = static_cast<int>(radiusX * cos((endAngle / 180.0f) * 3.1415f));
	int endY = static_cast<int>(radiusY * sin((endAngle / 180.0f) * 3.1415f));

	if (endX < 0)
	{
		if (endY > 0)
			endY++;
		else
			endX--;
	}
	else
	{
		if (endY < 0)
			endY--;
		else
			endX++;
	}

	draw_arc(x1, y1, x2, y2, startAngle, endAngle, set);
	draw_line(centerX, centerY, centerX + startX, centerY + startY, set);
	draw_line(centerX, centerY, centerX + endX, centerY + endY, set);
}

static void draw_chord(const int x1, const int y1, const int x2, const int y2, const float startAngle,
                       const float endAngle, const pixel_setter& set)
{
	const int centerX = (x1 + x2) / 2;
	const int centerY = (y1 + y2) / 2;
	const int dx = (x2 - x1);
	const int dy = (y2 - y1);
	const int radiusX = (abs(dx) / 2);
	const int radiusY = (abs(dy) / 2);

	const int startX = static_cast<int>(radiusX * cos((startAngle / 180.0f) * 3.1415f));
	const int startY = static_cast<int>(radiusY * sin((startAngle / 180.0f) * 3.1415f));
	int endX = static_cast<int>(radiusX * cos((endAngle / 180.0f) * 3.1415f));
	int endY = static_cast<int>(radiusY * sin((endAngle / 180.0f) * 3.1415f));

	if (endX < 0)
	{
		if (endY > 0)
			endY++;
		else
			endX--;
	}
	else
	{
		if (endY < 0)
			endY--;
		else
			endX++;
	}

	draw_arc(x1, y1, x2, y2, startAngle, endAngle, set);
	draw_line(centerX + startX, centerY + startY, centerX + endX, centerY + endY, set);
}

static void draw_round_rect(const int x1, const int y1, const int x2, const int y2, int width, int height,
                            const pixel_setter& set)
{
	const auto dx = (x2 - x1);
	const auto dy = (y2 - y1);
	const auto offsetX = width / 2;
	width = offsetX * 2;
	const auto offsetY = height / 2;
	height = offsetY * 2;

	if ((width > abs(dx)) || (height > abs(dy)))
	{
		draw_ellipse(x1, y1, x2, y2, set);
	}
	else
	{
		draw_line(x1 + offsetX, y1, x2 - offsetX, y1, set);
		draw_arc(x2 - width, y1, x2, y1 + height, 270.0f, 360.0f, set);
		draw_line(x2, y1 + offsetY, x2, y2 - offsetY, set);
		draw_arc(x2 - width, y2 - height, x2, y2, 0.0f, 90.0f, set);
		draw_line(x2 - offsetX, y2, x1 + offsetX, y2, set);
		draw_arc(x1, y2 - height, x1 + width, y2, 90.0f, 180.0f, set);
		draw_line(x1, y2 - offsetY, x1, y1 + offsetY, set);
		draw_arc(x1, y1, x1 + width, y1 + height, 180.0f, 270.0f, set);
	}
}

static void draw_bezier(const pointi* points, const int numPoints, const pixel_setter& set)
{
	const int numCurves = (numPoints - 1) / 3;
	const int numReqPoints = numCurves * 3 + 1;

	if (numPoints >= numReqPoints)
	{
		for (int i = 0; i < numCurves; i++)
		{
			const auto startPoint = points[i * 3];
			const auto controlPoint1 = points[i * 3 + 1];
			const auto controlPoint2 = points[i * 3 + 2];
			const auto endPoint = points[i * 3 + 3];

			if (controlPoint1.y == controlPoint2.y)
			{
				draw_line(startPoint.x, startPoint.y, endPoint.x, endPoint.y, set);
			}
			else
			{
				const auto distance1 = static_cast<float>(sqrt(
					pow(controlPoint1.x - startPoint.x, 2) + pow(controlPoint1.y - startPoint.y, 2)));
				const auto distance2 = static_cast<float>(sqrt(
					pow(controlPoint2.x - controlPoint1.x, 2) + pow(controlPoint2.y - controlPoint1.y, 2)));
				const auto distance3 = static_cast<float>(sqrt(
					pow(endPoint.x - controlPoint2.x, 2) + pow(endPoint.y - controlPoint2.y, 2)));
				const auto step = 1.0f / (distance1 + distance2 + distance3);

				const auto cx = 3.0f * (controlPoint1.x - startPoint.x);
				const auto bx = 3.0f * (controlPoint2.x - controlPoint1.x) - cx;
				const auto ax = endPoint.x - startPoint.x - bx - cx;

				const auto cy = 3.0f * (controlPoint1.y - startPoint.y);
				const auto by = 3.0f * (controlPoint2.y - controlPoint1.y) - cy;
				const auto ay = endPoint.y - startPoint.y - by - cy;

				auto oldX = startPoint.x;
				auto oldY = startPoint.y;
				auto k1 = 0.0f;
				auto xt = 0.0f;
				auto yt = 0.0f;
				auto oldX1 = 0.0f;
				auto oldY1 = 0.0f;

				for (auto t = 0.0f; t <= 1.0f; t += step)
				{
					xt = ax * (t * t * t) + bx * (t * t) + cx * t + startPoint.x;
					yt = ay * (t * t * t) + by * (t * t) + cy * t + startPoint.y;

					if ((static_cast<int>(xt) != oldX) && (static_cast<int>(yt) != oldY))
					{
						oldX1 = oldX;
						oldY1 = oldY;

						const auto k = static_cast<float>(static_cast<int>(yt) - oldY) / static_cast<float>(static_cast<
							int>(xt) - oldX);

						if (k != k1)
						{
							draw_line(oldX, oldY, static_cast<int>(xt), static_cast<int>(yt), set);

							k1 = k;
							oldX = static_cast<int>(xt);
							oldY = static_cast<int>(yt);
						}
					}
				}

				if ((static_cast<int>(xt) != oldX1) || (static_cast<int>(yt) != oldY1))
				{
					const auto dx = (static_cast<int>(xt) - oldX1);
					const auto dy = (static_cast<int>(yt) - oldY1);

					if (abs(dx) > abs(dy))
					{
						if (dy > 0)
							draw_line(oldX, oldY, static_cast<int>(xt), static_cast<int>(yt) + 1, set);
						else
							draw_line(oldX, oldY, static_cast<int>(xt), static_cast<int>(yt) - 1, set);
					}
					else
					{
						if (dx > 0)
							draw_line(oldX, oldY, static_cast<int>(xt) + 1, static_cast<int>(yt), set);
						else
							draw_line(oldX, oldY, static_cast<int>(xt) - 1, static_cast<int>(yt), set);
					}
				}
			}
		}
	}
}

struct raster_line
{
	int x1 = INT_MAX;
	int x2 = INT_MIN;
	float d1 = 0.0f;
	float d2 = 0.0f;

	void set(int x, float d)
	{
		if (x < x1)
		{
			x1 = x;
			d1 = d;
		}

		if (x > x2)
		{
			x2 = x;
			d2 = d;
		}
	}
};

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

static void color_pixel(ui::color32& pixel, const ui::color32 color, const float d)
{
	const auto bgColor = pixel;
	const auto red = ((1.0f - d) * ui::get_r(bgColor)) + (d * ui::get_r(color));
	const auto green = ((1.0f - d) * ui::get_g(bgColor)) + (d * ui::get_g(color));
	const auto blue = ((1.0f - d) * ui::get_b(bgColor)) + (d * ui::get_b(color));
	pixel = ui::rgb(red, green, blue);
}

static double normalize_angle(double a)
{
	while (a > 180.0) a -= 360.0;
	while (a < -180.0) a += 360.0;
	return a;
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


	constexpr uint32_t dither_matrix[4][4] = {
		{1, 9, 3, 11},
		{13, 5, 15, 7},
		{4, 12, 2, 10},
		{16, 8, 14, 6}
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
				c = lighten(c, 64 - df::mul_div(r, 64, outer_radius_limit2) + dither_matrix[x % 4][y % 4]);
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

				c = lighten(c, 64 - df::mul_div(r, 64, outer_radius_limit2) + dither_matrix[x % 4][y % 4]);
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




void ui::surface::fill_ellipse(const recti bounds, const color32 color)
{
	const int cx = _dimensions.cx;
	const int cy = _dimensions.cy;

	std::vector<raster_line> lines;
	lines.resize(cy);

	const auto fill_setter = [cy, &lines](int x, int y, float d)
	{
		if (y < 0 || y >= cy) return;
		lines[y].set(x, d);
	};

	draw_ellipse(bounds.left, bounds.top, bounds.right, bounds.bottom, fill_setter);

	for (auto y = 0; y < cy; ++y)
	{
		const auto& r = lines[y];
		const auto x1 = r.x1;
		const auto x2 = r.x2;

		if (x1 < x2)
		{
			auto* const line = std::bit_cast<color32*>(_pixels.get() + (y * _stride));
			const auto xmax = std::min(cx, x2);

			if (x1 > 0 && x1 < cx)
			{
				color_pixel(line[x1], color, r.d1);
			}

			for (auto x = std::max(0, x1 + 1); x < xmax; ++x)
			{
				line[x] = color;
			}

			if (x2 > 0 && x2 < cx)
			{
				color_pixel(line[x2], color, r.d2);
			}
		}
	}
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

	return {limit.left + x, limit.top + y, limit.left + x + scaled.cx, limit.top + y + scaled.cy};
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
