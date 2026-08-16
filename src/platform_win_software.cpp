// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: CPU software rendering backend. Implements a draw_context_device that rasterises a
// window-sized scene through one fixed 512-square system-memory BGRA DIB, walked across the
// damaged region a tile at a time and presented via GDI (BitBlt / UpdateLayeredWindow), so the
// buffer does not track the window size. Used as the fallback when Direct3D 11 hardware
// acceleration is unavailable, and for dialogs and bubble popups. Text is rasterized via
// DirectWrite glyph alpha bitmaps and alpha-blended on the CPU.

#include "pch.h"
#include "platform_win.h"
#include "platform_win_visual.h"

#include "av_format.h"
#include "ui_elements.h"
#include "platform_win_res.h"
#include "util_simd.h"

//////////////////////////////////////////////////////////////////////////////////////////////
// Pixel helpers - the software canvas is a top-down 32-bit BGRA buffer (straight alpha).
//////////////////////////////////////////////////////////////////////////////////////////////

namespace
{
	uint8_t to_byte(const float f) noexcept
	{
		const auto i = static_cast<int>(f * 255.0f + 0.5f);
		return static_cast<uint8_t>(i < 0 ? 0 : (i > 255 ? 255 : i));
	}

	// Porter-Duff "over" of a straight-alpha source colour onto a BGRA pixel. When opaque is set
	// the destination is treated as fully opaque (the case for non-layered windows presented with
	// BitBlt, where the alpha channel is ignored). Treating the destination as opaque is essential:
	// it makes anti-aliased glyph edges and semi-transparent panel overlays composite against the
	// real background instead of an empty (alpha 0) canvas.
	void blend_over(uint8_t* p, const ui::color& c, const float coverage, const bool opaque) noexcept
	{
		const auto sa = c.a * coverage;

		if (sa <= 0.0f) return;

		if (sa >= 1.0f)
		{
			p[0] = to_byte(c.b);
			p[1] = to_byte(c.g);
			p[2] = to_byte(c.r);
			p[3] = 255;
			return;
		}

		const auto da = opaque ? 1.0f : p[3] / 255.0f;
		const auto out_a = sa + da * (1.0f - sa);

		if (out_a <= 0.0001f)
		{
			p[0] = p[1] = p[2] = p[3] = 0;
			return;
		}

		const auto inv = da * (1.0f - sa);
		const auto out_b = (c.b * sa + (p[0] / 255.0f) * inv) / out_a;
		const auto out_g = (c.g * sa + (p[1] / 255.0f) * inv) / out_a;
		const auto out_r = (c.r * sa + (p[2] / 255.0f) * inv) / out_a;

		p[0] = to_byte(out_b);
		p[1] = to_byte(out_g);
		p[2] = to_byte(out_r);
		p[3] = opaque ? 255 : to_byte(out_a);
	}

	// Blend a straight-alpha BGRA source sample (0-255 components) onto a BGRA pixel.
	void blend_over_bgra(uint8_t* p, const int sb, const int sg, const int sr, const float sa,
	                     const bool opaque) noexcept
	{
		if (sa <= 0.0f) return;

		if (sa >= 1.0f)
		{
			p[0] = static_cast<uint8_t>(sb);
			p[1] = static_cast<uint8_t>(sg);
			p[2] = static_cast<uint8_t>(sr);
			p[3] = 255;
			return;
		}

		const auto da = opaque ? 1.0f : p[3] / 255.0f;
		const auto out_a = sa + da * (1.0f - sa);

		if (out_a <= 0.0001f)
		{
			p[0] = p[1] = p[2] = p[3] = 0;
			return;
		}

		const auto inv = da * (1.0f - sa);
		const auto out_b = (sb * sa + p[0] * inv) / out_a;
		const auto out_g = (sg * sa + p[1] * inv) / out_a;
		const auto out_r = (sr * sa + p[2] * inv) / out_a;

		p[0] = to_byte(out_b / 255.0f);
		p[1] = to_byte(out_g / 255.0f);
		p[2] = to_byte(out_r / 255.0f);
		p[3] = opaque ? 255 : to_byte(out_a);
	}

	// Linear interpolation between two colours, matching the vertex-colour interpolation the
	// hardware backend gets for free from the rasteriser.
	ui::color lerp_color(const ui::color& a, const ui::color& b, const float t) noexcept
	{
		return {
			a.r + (b.r - a.r) * t,
			a.g + (b.g - a.g) * t,
			a.b + (b.b - a.b) * t,
			a.a + (b.a - a.a) * t
		};
	}

	constexpr int tap_count(const ui::texture_sampler sampler) noexcept
	{
		if (sampler == ui::texture_sampler::point) return 1;
		return sampler == ui::texture_sampler::bilinear ? 2 : 4;
	}

	// Footprint (byte offsets into the source, scaled by `step`) and filter weights for one source
	// coordinate. Offsets are clamped to the source extent, matching software_canvas::sample.
	void build_taps(const double s, const int limit, const int step, const int taps, int* offsets,
	                float* weights) noexcept
	{
		if (taps == 1)
		{
			offsets[0] = std::clamp(static_cast<int>(std::floor(s + 0.5)), 0, limit - 1) * step;
			weights[0] = 1.0f;
			return;
		}

		const auto p0 = static_cast<int>(std::floor(s));
		const auto f = s - p0;

		if (taps == 2)
		{
			weights[0] = static_cast<float>(1.0 - f);
			weights[1] = static_cast<float>(f);
			offsets[0] = std::clamp(p0, 0, limit - 1) * step;
			offsets[1] = std::clamp(p0 + 1, 0, limit - 1) * step;
			return;
		}

		weights[0] = static_cast<float>(f * (-0.5 + f * (1.0 - 0.5 * f)));
		weights[1] = static_cast<float>(1.0 + f * f * (1.5 * f - 2.5));
		weights[2] = static_cast<float>(f * (0.5 + f * (2.0 - 1.5 * f)));
		weights[3] = static_cast<float>(f * f * (-0.5 + 0.5 * f));

		for (auto t = 0; t < 4; ++t)
		{
			offsets[t] = std::clamp(p0 + t - 1, 0, limit - 1) * step;
		}
	}

#if defined(COMPILE_SIMD_INTRINSIC)
	__m128 load_bgra_ps(const uint8_t* p) noexcept
	{
		const auto zero = _mm_setzero_si128();
		const auto bytes = _mm_cvtsi32_si128(*std::bit_cast<const int32_t*>(p));
		return _mm_cvtepi32_ps(_mm_unpacklo_epi16(_mm_unpacklo_epi8(bytes, zero), zero));
	}
#endif
}

//////////////////////////////////////////////////////////////////////////////////////////////
// software_canvas - owns the BGRA buffer + clip, provides primitive rasterisation.
//////////////////////////////////////////////////////////////////////////////////////////////

class software_canvas
{
public:
	struct sampled_pixel
	{
		int b;
		int g;
		int r;
		int a;
	};

	uint8_t* _bits = nullptr;
	int _stride = 0;
	// The allocation, not the window: callers pass window-space coordinates, but the buffer behind
	// _bits is a scratch tile the context walks across the damaged region.
	sizei _buffer_extent;
	// Window-space position of buffer pixel (0,0).
	pointi _origin;
	recti _clip;
	// When true the canvas is treated as fully opaque (non-layered windows presented with BitBlt).
	// When false (layered bubble popups) real per-pixel alpha is preserved.
	bool _opaque = true;

	uint8_t* pixel(const int x, const int y) const noexcept
	{
		return _bits + static_cast<ptrdiff_t>(y - _origin.y) * _stride + static_cast<ptrdiff_t>(x - _origin.x) * 4;
	}

	// The buffer in window space. Every write is clamped to it, so it is the bounds check on the
	// allocation - deriving it from the same fields pixel() uses is what stops the two drifting.
	recti buffer_bounds() const noexcept
	{
		return {_origin.x, _origin.y, _origin.x + _buffer_extent.cx, _origin.y + _buffer_extent.cy};
	}

	recti clamp_to_clip(const recti r) const noexcept
	{
		return r.intersection(_clip).intersection(buffer_bounds());
	}

	static sampled_pixel sample(const uint8_t* pixels, const int stride, const int width, const int height,
	                            const double x, const double y, const ui::texture_sampler sampler) noexcept
	{
		const auto pixel_at = [=](const int px, const int py)
		{
			return pixels + static_cast<ptrdiff_t>(std::clamp(py, 0, height - 1)) * stride +
				static_cast<ptrdiff_t>(std::clamp(px, 0, width - 1)) * 4;
		};

		if (sampler == ui::texture_sampler::point)
		{
			const auto* p = pixel_at(static_cast<int>(std::floor(x + 0.5)),
			                         static_cast<int>(std::floor(y + 0.5)));
			return {p[0], p[1], p[2], p[3]};
		}

		const auto x0 = static_cast<int>(std::floor(x));
		const auto y0 = static_cast<int>(std::floor(y));
		const auto fx = x - x0;
		const auto fy = y - y0;
		if (sampler == ui::texture_sampler::bilinear)
		{
			const auto* p00 = pixel_at(x0, y0);
			const auto* p10 = pixel_at(x0 + 1, y0);
			const auto* p01 = pixel_at(x0, y0 + 1);
			const auto* p11 = pixel_at(x0 + 1, y0 + 1);
			const auto w00 = (1.0 - fx) * (1.0 - fy);
			const auto w10 = fx * (1.0 - fy);
			const auto w01 = (1.0 - fx) * fy;
			const auto w11 = fx * fy;
			const auto channel = [&](const int c)
			{
				return static_cast<int>(p00[c] * w00 + p10[c] * w10 + p01[c] * w01 + p11[c] * w11);
			};
			return {channel(0), channel(1), channel(2), channel(3)};
		}

		const auto weights = [](const double f)
		{
			return std::array{
				f * (-0.5 + f * (1.0 - 0.5 * f)),
				1.0 + f * f * (1.5 * f - 2.5),
				f * (0.5 + f * (2.0 - 1.5 * f)),
				f * f * (-0.5 + 0.5 * f)
			};
		};
		const auto wx = weights(fx);
		const auto wy = weights(fy);
		double channels[4]{};
		for (auto yy = 0; yy < 4; ++yy)
		{
			for (auto xx = 0; xx < 4; ++xx)
			{
				const auto* p = pixel_at(x0 + xx - 1, y0 + yy - 1);
				const auto weight = wx[xx] * wy[yy];
				for (auto channel = 0; channel < 4; ++channel) channels[channel] += p[channel] * weight;
			}
		}
		return {
			static_cast<int>(std::clamp(channels[0], 0.0, 255.0)),
			static_cast<int>(std::clamp(channels[1], 0.0, 255.0)),
			static_cast<int>(std::clamp(channels[2], 0.0, 255.0)),
			static_cast<int>(std::clamp(channels[3], 0.0, 255.0))
		};
	}

	// Unconditional write of the clipped region: unlike fill_rect this ignores source alpha and
	// stamps the destination, which is what a render-target clear means.
	void clear(const ui::color c) const
	{
		const auto r = clamp_to_clip(buffer_bounds());
		if (r.is_empty()) return;

		const auto b = to_byte(c.b);
		const auto g = to_byte(c.g);
		const auto red = to_byte(c.r);
		const auto a = _opaque ? uint8_t{255} : to_byte(c.a);
		const auto pixel_value = static_cast<uint32_t>(b) | static_cast<uint32_t>(g) << 8 |
			static_cast<uint32_t>(red) << 16 | static_cast<uint32_t>(a) << 24;

		for (auto y = r.top; y < r.bottom; ++y)
		{
			std::fill_n(std::bit_cast<uint32_t*>(pixel(r.left, y)), r.width(), pixel_value);
		}
	}

	void fill_rect(const recti bounds, const ui::color c) const
	{
		const auto r = clamp_to_clip(bounds);
		if (r.is_empty()) return;
		if (c.a <= 0.0f) return;

		const auto b = to_byte(c.b);
		const auto g = to_byte(c.g);
		const auto red = to_byte(c.r);
		if (c.a >= 1.0f)
		{
			const auto pixel_value = static_cast<uint32_t>(b) | static_cast<uint32_t>(g) << 8 |
				static_cast<uint32_t>(red) << 16 | 0xff000000u;
			for (auto y = r.top; y < r.bottom; ++y)
			{
				std::fill_n(std::bit_cast<uint32_t*>(pixel(r.left, y)), r.width(), pixel_value);
			}
			return;
		}

		for (auto y = r.top; y < r.bottom; ++y)
		{
			auto* p = pixel(r.left, y);
			auto x = r.left;

#if defined(COMPILE_SIMD_INTRINSIC)
			if (_opaque)
			{
				const auto processed = blend_solid_opaque_sse2(p, r.width(), c.b, c.g, c.r, c.a);
				x += static_cast<int>(processed);
				p += processed * 4;
			}
#endif

			for (; x < r.right; ++x, p += 4)
			{
				blend_over(p, c, 1.0f, _opaque);
			}
		}
	}

	// Matches the hardware backend's draw_rect_gradient, which draws a four-triangle fan from the
	// centre vertex: the centre carries c_centre and all four corners c_corner, so the colour the
	// rasteriser interpolates at a point is lerp(c_centre, c_corner, max(|dx|/hw, |dy|/hh)).
	// Each row is split into a constant middle span (where the vertical term dominates) and a
	// horizontal ramp either side, so most of a large fill is still a straight run.
	void fill_rect_gradient(const recti bounds, const ui::color c_centre, const ui::color c_corner) const
	{
		const auto r = clamp_to_clip(bounds);
		if (r.is_empty()) return;
		if (c_centre.a <= 0.0f && c_corner.a <= 0.0f) return;

		const auto cx = (bounds.left + bounds.right) * 0.5f;
		const auto cy = (bounds.top + bounds.bottom) * 0.5f;
		const auto hw = std::max(bounds.width() * 0.5f, 0.5f);
		const auto hh = std::max(bounds.height() * 0.5f, 0.5f);
		const auto opaque_fill = _opaque && c_centre.a >= 1.0f && c_corner.a >= 1.0f;

		for (auto y = r.top; y < r.bottom; ++y)
		{
			const auto ty = std::min(std::abs((y + 0.5f) - cy) / hh, 1.0f);
			const auto span = ty * hw;

			// [mid_left, mid_right) is where the vertical term wins and the colour is constant.
			const auto mid_left = std::clamp(static_cast<int>(std::ceil(cx - span - 0.5f)), r.left, r.right);
			const auto mid_right = std::clamp(static_cast<int>(std::floor(cx + span - 0.5f)) + 1, mid_left, r.right);

			const auto ramp = [&](const int x)
			{
				const auto tx = std::min(std::abs((x + 0.5f) - cx) / hw, 1.0f);
				return lerp_color(c_centre, c_corner, std::max(tx, ty));
			};

			auto* p = pixel(r.left, y);

			for (auto x = r.left; x < mid_left; ++x, p += 4)
			{
				blend_over(p, ramp(x), 1.0f, _opaque);
			}

			if (mid_right > mid_left)
			{
				const auto mid = lerp_color(c_centre, c_corner, ty);

				if (opaque_fill)
				{
					const auto pixel_value = static_cast<uint32_t>(to_byte(mid.b)) |
						static_cast<uint32_t>(to_byte(mid.g)) << 8 |
						static_cast<uint32_t>(to_byte(mid.r)) << 16 | 0xff000000u;
					std::fill_n(std::bit_cast<uint32_t*>(p), mid_right - mid_left, pixel_value);
					p += static_cast<ptrdiff_t>(mid_right - mid_left) * 4;
				}
				else
				{
					for (auto x = mid_left; x < mid_right; ++x, p += 4)
					{
						blend_over(p, mid, 1.0f, _opaque);
					}
				}
			}

			for (auto x = mid_right; x < r.right; ++x, p += 4)
			{
				blend_over(p, ramp(x), 1.0f, _opaque);
			}
		}
	}

	// Matches the hardware backend, which fills the border ring with four mitred trapezoids that
	// interpolate c_outside on the outer edge to c_inside on the inner edge. Deciding which
	// trapezoid a pixel falls in by the 45-degree mitres is the same as taking the smallest of
	// the four normalised edge distances, which is also the interpolation factor.
	void fill_border_gradient(const recti inside, const recti outside, const ui::color c_inside,
	                          const ui::color c_outside) const
	{
		if (c_inside.a <= 0.0f && c_outside.a <= 0.0f) return;

		constexpr auto never = std::numeric_limits<float>::max();
		const auto wl = static_cast<float>(inside.left - outside.left);
		const auto wr = static_cast<float>(outside.right - inside.right);
		const auto wt = static_cast<float>(inside.top - outside.top);
		const auto wb = static_cast<float>(outside.bottom - inside.bottom);

		const auto fill_band = [&](const recti band)
		{
			const auto r = clamp_to_clip(band);
			if (r.is_empty()) return;

			for (auto y = r.top; y < r.bottom; ++y)
			{
				const auto py = y + 0.5f;
				const auto dt = wt > 0.0f ? (py - outside.top) / wt : never;
				const auto db = wb > 0.0f ? (outside.bottom - py) / wb : never;
				const auto ty = std::min(dt, db);

				auto* p = pixel(r.left, y);

				for (auto x = r.left; x < r.right; ++x, p += 4)
				{
					const auto px = x + 0.5f;
					const auto dl = wl > 0.0f ? (px - outside.left) / wl : never;
					const auto dr = wr > 0.0f ? (outside.right - px) / wr : never;
					const auto t = std::min(std::min(dl, dr), ty);

					// Everything the ring does not cover interpolates past the inner edge.
					if (t > 1.0f) continue;
					blend_over(p, lerp_color(c_outside, c_inside, std::max(t, 0.0f)), 1.0f, _opaque);
				}
			}
		};

		fill_band(recti(outside.left, outside.top, inside.left, outside.bottom));
		fill_band(recti(inside.right, outside.top, outside.right, outside.bottom));
		fill_band(recti(inside.left, outside.top, inside.right, inside.top));
		fill_band(recti(inside.left, inside.bottom, inside.right, outside.bottom));
	}

	void fill_rounded_rect(const recti bounds, const ui::color c, int radius) const
	{
		const auto max_radius = std::min(bounds.width(), bounds.height()) / 2;
		radius = std::clamp(radius, 0, max_radius);

		if (radius <= 0)
		{
			fill_rect(bounds, c);
			return;
		}

		const auto r = clamp_to_clip(bounds);
		if (r.is_empty()) return;

		const auto cxl = bounds.left + radius; // left corner centre x
		const auto cxr = bounds.right - radius; // right corner centre x
		const auto cyt = bounds.top + radius; // top corner centre y
		const auto cyb = bounds.bottom - radius; // bottom corner centre y
		const auto rf = static_cast<float>(radius);

		for (auto y = r.top; y < r.bottom; ++y)
		{
			auto* p = pixel(r.left, y);

			for (auto x = r.left; x < r.right; ++x, p += 4)
			{
				auto coverage = 1.0f;

				float ccx = 0.0f;
				float ccy = 0.0f;
				auto in_corner = false;

				if (x < cxl && y < cyt)
				{
					ccx = static_cast<float>(cxl);
					ccy = static_cast<float>(cyt);
					in_corner = true;
				}
				else if (x >= cxr && y < cyt)
				{
					ccx = static_cast<float>(cxr);
					ccy = static_cast<float>(cyt);
					in_corner = true;
				}
				else if (x < cxl && y >= cyb)
				{
					ccx = static_cast<float>(cxl);
					ccy = static_cast<float>(cyb);
					in_corner = true;
				}
				else if (x >= cxr && y >= cyb)
				{
					ccx = static_cast<float>(cxr);
					ccy = static_cast<float>(cyb);
					in_corner = true;
				}

				if (in_corner)
				{
					const auto dx = (x + 0.5f) - ccx;
					const auto dy = (y + 0.5f) - ccy;
					const auto dist = std::sqrt(dx * dx + dy * dy);
					coverage = std::clamp(rf - dist + 0.5f, 0.0f, 1.0f);
				}

				if (coverage > 0.0f)
				{
					blend_over(p, c, coverage, _opaque);
				}
			}
		}
	}

	// Fill a triangle (used for the bubble pointer).
	void fill_triangle(const pointd a, const pointd b, const pointd c, const ui::color clr) const
	{
		const auto min_x = static_cast<int>(std::floor(std::min({a.X, b.X, c.X})));
		const auto max_x = static_cast<int>(std::ceil(std::max({a.X, b.X, c.X})));
		const auto min_y = static_cast<int>(std::floor(std::min({a.Y, b.Y, c.Y})));
		const auto max_y = static_cast<int>(std::ceil(std::max({a.Y, b.Y, c.Y})));

		const recti tri_bounds(min_x, min_y, max_x, max_y);
		const auto r = clamp_to_clip(tri_bounds);
		if (r.is_empty()) return;

		const auto area = (b.X - a.X) * (c.Y - a.Y) - (b.Y - a.Y) * (c.X - a.X);
		if (std::abs(area) < 0.0001) return;
		const auto inv_area = 1.0 / area;

		for (auto y = r.top; y < r.bottom; ++y)
		{
			auto* p = pixel(r.left, y);

			for (auto x = r.left; x < r.right; ++x, p += 4)
			{
				const auto px = x + 0.5;
				const auto py = y + 0.5;

				const auto w0 = ((b.X - px) * (c.Y - py) - (b.Y - py) * (c.X - px)) * inv_area;
				const auto w1 = ((c.X - px) * (a.Y - py) - (c.Y - py) * (a.X - px)) * inv_area;
				const auto w2 = 1.0 - w0 - w1;

				if (w0 >= 0.0 && w1 >= 0.0 && w2 >= 0.0)
				{
					blend_over(p, clr, 1.0f, _opaque);
				}
			}
		}
	}

	// Blend an 8-bit alpha glyph bitmap using colour clr.
	void blend_glyph(const int px, const int py, const render_char_result& g, const ui::color clr) const
	{
		if (g.is_empty()) return;

		const recti glyph_bounds(px, py, px + g.cx, py + g.cy);
		const auto r = clamp_to_clip(glyph_bounds);
		if (r.is_empty()) return;

		for (auto y = r.top; y < r.bottom; ++y)
		{
			const auto* src = g.pixels.data() + static_cast<ptrdiff_t>(y - py) * g.cx + (r.left - px);
			auto* p = pixel(r.left, y);
			auto x = r.left;

#if defined(COMPILE_SIMD_INTRINSIC)
			if (_opaque)
			{
				const auto processed = blend_glyph_opaque_sse2(p, src, r.width(), clr.b, clr.g, clr.r, clr.a);
				x += static_cast<int>(processed);
				p += processed * 4;
				src += processed;
			}
#endif

			for (; x < r.right; ++x, p += 4, ++src)
			{
				const auto a = *src;
				if (a) blend_over(p, clr, a / 255.0f, _opaque);
			}
		}
	}

	// Blit (with scaling) a BGRA source surface region into a destination rect.
	void blit_surface(const ui::surface& src, const recti src_rect, const recti dst_rect, const float alpha,
	                  const ui::texture_sampler sampler, const bool has_alpha) const
	{
		const auto r = clamp_to_clip(dst_rect);
		if (r.is_empty() || dst_rect.width() <= 0 || dst_rect.height() <= 0) return;

		const auto* src_pixels = src.pixels();
		const auto src_stride = static_cast<int>(src.stride());
		const auto src_w = static_cast<int>(src.width());
		const auto src_h = static_cast<int>(src.height());
		if (!src_pixels || src_w <= 0 || src_h <= 0) return;

		const auto scale_x = static_cast<double>(src_rect.width()) / dst_rect.width();
		const auto scale_y = static_cast<double>(src_rect.height()) / dst_rect.height();
		// Opaque source into an opaque (BitBlt) canvas: skip Porter-Duff and write the sample.
		const bool opaque_write = _opaque && !has_alpha && alpha >= 0.999f;

		// The destination -> source map is affine, so every row shares the same column footprint.
		// Building it once removes a floor, two clamps and a weight evaluation per pixel.
		const auto taps = tap_count(sampler);
		const auto width = r.width();
		const auto first_sx = (r.left - dst_rect.left + 0.5) * scale_x + src_rect.left - 0.5;
		std::vector<int> col_offsets(static_cast<size_t>(width) * taps);
		std::vector<float> col_weights(static_cast<size_t>(width) * taps);

		for (auto i = 0; i < width; ++i)
		{
			build_taps(first_sx + scale_x * i, src_w, 4, taps,
			           col_offsets.data() + static_cast<size_t>(i) * taps,
			           col_weights.data() + static_cast<size_t>(i) * taps);
		}

		int row_offsets[4]{};
		float row_weights[4]{};

		for (auto y = r.top; y < r.bottom; ++y)
		{
			const auto sy = (y - dst_rect.top + 0.5) * scale_y + src_rect.top - 0.5;
			build_taps(sy, src_h, src_stride, taps, row_offsets, row_weights);
			auto* p = pixel(r.left, y);

			if (taps == 1)
			{
				const auto* const row = src_pixels + row_offsets[0];

				for (auto i = 0; i < width; ++i, p += 4)
				{
					const auto* const s = row + col_offsets[i];

					if (opaque_write)
					{
						p[0] = s[0];
						p[1] = s[1];
						p[2] = s[2];
						p[3] = 255;
					}
					else
					{
						const auto a = (has_alpha ? s[3] / 255.0f : 1.0f) * alpha;
						blend_over_bgra(p, s[0], s[1], s[2], a, _opaque);
					}
				}

				continue;
			}

			for (auto i = 0; i < width; ++i, p += 4)
			{
				const auto* const off = col_offsets.data() + static_cast<size_t>(i) * taps;
				const auto* const cw = col_weights.data() + static_cast<size_t>(i) * taps;
				alignas(16) float channels[4];

#if defined(COMPILE_SIMD_INTRINSIC)
				auto acc = _mm_setzero_ps();

				for (auto j = 0; j < taps; ++j)
				{
					const auto* const row = src_pixels + row_offsets[j];
					const auto wy = row_weights[j];

					for (auto t = 0; t < taps; ++t)
					{
						acc = _mm_add_ps(acc, _mm_mul_ps(load_bgra_ps(row + off[t]), _mm_set1_ps(cw[t] * wy)));
					}
				}

				_mm_store_ps(channels, _mm_min_ps(_mm_max_ps(acc, _mm_setzero_ps()), _mm_set1_ps(255.0f)));
#else
				channels[0] = channels[1] = channels[2] = channels[3] = 0.0f;

				for (auto j = 0; j < taps; ++j)
				{
					const auto* const row = src_pixels + row_offsets[j];
					const auto wy = row_weights[j];

					for (auto t = 0; t < taps; ++t)
					{
						const auto* const s = row + off[t];
						const auto w = cw[t] * wy;
						for (auto c = 0; c < 4; ++c) channels[c] += s[c] * w;
					}
				}

				for (auto c = 0; c < 4; ++c) channels[c] = std::clamp(channels[c], 0.0f, 255.0f);
#endif

				const auto sb = static_cast<int>(channels[0]);
				const auto sg = static_cast<int>(channels[1]);
				const auto sr = static_cast<int>(channels[2]);

				if (opaque_write)
				{
					p[0] = static_cast<uint8_t>(sb);
					p[1] = static_cast<uint8_t>(sg);
					p[2] = static_cast<uint8_t>(sr);
					p[3] = 255;
				}
				else
				{
					const auto a = (has_alpha ? channels[3] / 255.0f : 1.0f) * alpha;
					blend_over_bgra(p, sb, sg, sr, a, _opaque);
				}
			}
		}
	}

	// 1:1 copy of a source surface (already at destination size) into dst_rect, honouring the clip.
	// The opaque case is a per-row memcpy - the fast tail after swscale has scaled the source to the
	// destination size (see software_texture::display_scaled).
	void blit_copy(const ui::surface& src, const recti dst_rect, const float alpha, const bool has_alpha) const
	{
		const auto r = clamp_to_clip(dst_rect);
		if (r.is_empty()) return;

		const auto* src_pixels = src.pixels();
		const auto src_stride = static_cast<int>(src.stride());
		if (!src_pixels) return;

		const bool opaque_write = _opaque && !has_alpha && alpha >= 0.999f;

		for (auto y = r.top; y < r.bottom; ++y)
		{
			const auto* srow = src_pixels + static_cast<ptrdiff_t>(y - dst_rect.top) * src_stride +
				static_cast<ptrdiff_t>(r.left - dst_rect.left) * 4;
			auto* p = pixel(r.left, y);

			if (opaque_write)
			{
				memcpy(p, srow, static_cast<size_t>(r.width()) * 4);
			}
			else
			{
				auto x = r.left;

#if defined(COMPILE_SIMD_INTRINSIC)
				if (_opaque)
				{
					const auto processed = blend_bgra_opaque_sse2(p, srow, r.width(), has_alpha, alpha);
					x += static_cast<int>(processed);
					p += processed * 4;
					srow += processed * 4;
				}
#endif

				for (; x < r.right; ++x, p += 4, srow += 4)
				{
					const auto a = (has_alpha ? srow[3] / 255.0f : 1.0f) * alpha;
					blend_over_bgra(p, srow[0], srow[1], srow[2], a, _opaque);
				}
			}
		}
	}

	// rotated / transformed images (e.g. the edit view). The transforms in use (rotate / scale /
	// translate) are affine, so the quad is a parallelogram and dest->source is an exact linear map.
	void blit_quad(const ui::surface& src, const recti src_rect, const quadd& q, const float alpha,
	               const ui::texture_sampler sampler, const bool has_alpha,
	               const ui::texture_transform* transform = nullptr) const
	{
		const auto r = clamp_to_clip(q.bounding_rect_i());
		if (r.is_empty()) return;

		const auto* src_pixels = src.pixels();
		const auto src_stride = static_cast<int>(src.stride());
		const auto src_w = static_cast<int>(src.width());
		const auto src_h = static_cast<int>(src.height());
		if (!src_pixels || src_w <= 0 || src_h <= 0) return;

		const auto o = q[0];
		const auto ex = q[1] - q[0]; // dest vector for the source width
		const auto ey = q[3] - q[0]; // dest vector for the source height
		const auto det = ex.X * ey.Y - ex.Y * ey.X;
		if (std::abs(det) < 1e-9) return;
		const auto inv_det = 1.0 / det;

		const auto src_fw = static_cast<double>(src_rect.width());
		const auto src_fh = static_cast<double>(src_rect.height());
		// Opaque source into an opaque (BitBlt) canvas: skip Porter-Duff and write the sample.
		const bool opaque_write = _opaque && !has_alpha && alpha >= 0.999f;

		for (auto y = r.top; y < r.bottom; ++y)
		{
			auto* p = pixel(r.left, y);

			for (auto x = r.left; x < r.right; ++x, p += 4)
			{
				const auto dx = (x + 0.5) - o.X;
				const auto dy = (y + 0.5) - o.Y;

				auto u = (dx * ey.Y - dy * ey.X) * inv_det;
				auto v = (ex.X * dy - ex.Y * dx) * inv_det;

				if (u < 0.0 || u > 1.0 || v < 0.0 || v > 1.0) continue;

				if (transform && transform->has_perspective)
				{
					const auto normalized_x = u - 0.5;
					const auto normalized_y = v - 0.5;
					const auto denominator = 1.0 + transform->perspective_horizontal * normalized_x +
						transform->perspective_vertical * normalized_y;
					if (denominator <= 0.0) continue;
					u = 0.5 + normalized_x / denominator;
					v = 0.5 + normalized_y / denominator;
					if (u < 0.0 || u > 1.0 || v < 0.0 || v > 1.0) continue;
				}

				const auto sx = src_rect.left + u * src_fw - 0.5;
				const auto sy = src_rect.top + v * src_fh - 0.5;

				const auto sampled = sample(src_pixels, src_stride, src_w, src_h, sx, sy, sampler);
				auto [sb, sg, sr, sample_a] = sampled;

				if (transform && transform->has_color_changes)
				{
					const auto red = std::clamp(sr / 255.0f * transform->red_gain, 0.0f, 1.0f);
					const auto green = std::clamp(sg / 255.0f * transform->green_gain, 0.0f, 1.0f);
					const auto blue = std::clamp(sb / 255.0f * transform->blue_gain, 0.0f, 1.0f);
					auto luminance = 0.299f * red + 0.587f * green + 0.114f * blue;
					auto chroma_u = -0.147f * red - 0.289f * green + 0.436f * blue;
					auto chroma_v = 0.615f * red - 0.515f * green - 0.100f * blue;
					const auto curve_index = std::clamp(static_cast<int>(luminance * ui::texture_transform::curve_len),
					                                    0, ui::texture_transform::curve_len - 1);
					luminance = transform->curve[curve_index];
					if (!df::is_zero(transform->vibrance))
					{
						const auto chroma_x = -0.105f - chroma_u;
						const auto chroma_y = 0.227f - chroma_v;
						const auto distance = std::sqrt(chroma_x * chroma_x + chroma_y * chroma_y);
						const auto saturation = transform->saturation * (1.0f + transform->vibrance * distance * 4.0f);
						chroma_u *= saturation;
						chroma_v *= saturation;
					}
					else
					{
						chroma_u *= transform->saturation;
						chroma_v *= transform->saturation;
					}
					chroma_u = std::clamp(chroma_u, -1.0f, 1.0f);
					chroma_v = std::clamp(chroma_v, -1.0f, 1.0f);
					sr = df::round(std::clamp(luminance + 1.14025f * chroma_v, 0.0f, 1.0f) * 255.0f);
					sg = df::round(
						std::clamp(luminance - 0.39473f * chroma_u - 0.58081f * chroma_v, 0.0f, 1.0f) * 255.0f);
					sb = df::round(std::clamp(luminance + 2.03252f * chroma_u, 0.0f, 1.0f) * 255.0f);
				}

				if (opaque_write)
				{
					p[0] = static_cast<uint8_t>(sb);
					p[1] = static_cast<uint8_t>(sg);
					p[2] = static_cast<uint8_t>(sr);
					p[3] = 255;
				}
				else
				{
					const auto a = (has_alpha ? sample_a / 255.0f : 1.0f) * alpha;
					blend_over_bgra(p, sb, sg, sr, a, _opaque);
				}
			}
		}
	}
};

//////////////////////////////////////////////////////////////////////////////////////////////
// Decode a PNG resource (shadow art) into a straight-alpha BGRA surface.
//////////////////////////////////////////////////////////////////////////////////////////////

static ui::surface_ptr decode_png_resource_to_surface(const factories_ptr& f, const uint32_t res_id)
{
	// WIC is optional - startup continues without it - so the shadow art simply does not load.
	if (!f || !f->wic) return nullptr;

	ComPtr<IWICStream> stream;
	ComPtr<IWICBitmapDecoder> decoder;
	ComPtr<IWICBitmapFrameDecode> frame;
	ComPtr<IWICFormatConverter> converter;

	auto* const res_handle = FindResourceA(get_resource_instance, MAKEINTRESOURCEA(res_id), "PNG");
	if (!res_handle) return nullptr;

	auto* const data_handle = LoadResource(get_resource_instance, res_handle);
	if (!data_handle) return nullptr;

	auto* const data = LockResource(data_handle);
	const auto data_size = SizeofResource(get_resource_instance, res_handle);
	if (!data || !data_size) return nullptr;

	auto hr = f->wic->CreateStream(&stream);
	if (SUCCEEDED(hr)) hr = stream->InitializeFromMemory(static_cast<BYTE*>(data), data_size);
	if (SUCCEEDED(hr))
		hr = f->wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad,
		                                     &decoder);
	if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
	if (SUCCEEDED(hr)) hr = f->wic->CreateFormatConverter(&converter);
	if (SUCCEEDED(hr))
		hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0f,
		                           WICBitmapPaletteTypeMedianCut);

	if (FAILED(hr)) return nullptr;

	uint32_t w = 0;
	uint32_t h = 0;
	if (FAILED(converter->GetSize(&w, &h)) || w == 0 || h == 0) return nullptr;

	auto surface = std::make_shared<ui::surface>();
	surface->alloc(static_cast<int>(w), static_cast<int>(h), ui::texture_format::ARGB, ui::orientation::top_left);

	if (FAILED(converter->CopyPixels(nullptr, static_cast<uint32_t>(surface->stride()),
		static_cast<uint32_t>(surface->size()), surface->pixels())))
	{
		return nullptr;
	}

	return surface;
}

//////////////////////////////////////////////////////////////////////////////////////////////
// YUV -> BGRA conversion. NV12/P010 surfaces (produced by the JPEG and video decoders for the
// GPU YUV path) are not directly presentable by the CPU canvas, which expects BGRA. Convert
// them once at texture-update time so the rest of the software draw path is unchanged. The
// affine matrix comes from ui::compute_yuv_matrix() so both renderers agree.
//////////////////////////////////////////////////////////////////////////////////////////////

namespace
{
	// Convert an NV12 or P010 4:2:0 surface to a top-down BGRA surface (straight alpha, opaque).
	// The chroma plane follows the full-resolution luma plane at half height, with 2x1-packed
	// interleaved U,V samples shared across each 2x2 luma block (nearest-neighbour chroma upsample).
	void convert_yuv_to_bgra(const ui::surface& src, ui::surface& dst) noexcept
	{
		const auto dims = src.dimensions();
		const int w = dims.cx;
		const int h = dims.cy;
		const auto stride = src.stride();
		const auto mat = ui::compute_yuv_matrix(src.color_space());
		const bool p010 = src.format() == ui::texture_format::P010;
		// P010 keeps its 10 significant bits at the top of each 16 bit word, so full scale is
		// 1023 << 6 = 65472, not 65535. Normalising by 65535 leaves every sample 0.096% low.
		const float norm = p010 ? (1.0f / 65472.0f) : (1.0f / 255.0f);

		const auto* const y_base = src.pixels();
		const auto* const c_base = y_base + stride * h; // chroma plane follows luma

		for (int y = 0; y < h; ++y)
		{
			auto* const out = dst.pixels_line(y);
			const auto* const c_row = c_base + (y >> 1) * stride;

			if (p010)
			{
				const auto* const y_row = std::bit_cast<const uint16_t*>(y_base + y * stride);
				const auto* const cp = std::bit_cast<const uint16_t*>(c_row);

				for (int x = 0; x < w; ++x)
				{
					const int cx = (x >> 1) * 2;
					const float yy = static_cast<float>(y_row[x]) * norm;
					const float u = static_cast<float>(cp[cx]) * norm;
					const float v = static_cast<float>(cp[cx + 1]) * norm;

					auto* const px = out + x * 4;
					px[0] = to_byte(mat.m[8] * yy + mat.m[9] * u + mat.m[10] * v + mat.m[11]);
					px[1] = to_byte(mat.m[4] * yy + mat.m[5] * u + mat.m[6] * v + mat.m[7]);
					px[2] = to_byte(mat.m[0] * yy + mat.m[1] * u + mat.m[2] * v + mat.m[3]);
					px[3] = 255;
				}
			}
			else
			{
				const auto* const y_row = y_base + y * stride;

				for (int x = 0; x < w; ++x)
				{
					const int cx = (x >> 1) * 2;
					const float yy = static_cast<float>(y_row[x]) * norm;
					const float u = static_cast<float>(c_row[cx]) * norm;
					const float v = static_cast<float>(c_row[cx + 1]) * norm;

					auto* const px = out + x * 4;
					px[0] = to_byte(mat.m[8] * yy + mat.m[9] * u + mat.m[10] * v + mat.m[11]);
					px[1] = to_byte(mat.m[4] * yy + mat.m[5] * u + mat.m[6] * v + mat.m[7]);
					px[2] = to_byte(mat.m[0] * yy + mat.m[1] * u + mat.m[2] * v + mat.m[3]);
					px[3] = 255;
				}
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////
// software_texture - a system-memory surface used as a drawable image source.
//////////////////////////////////////////////////////////////////////////////////////////////

class software_texture final : public ui::texture
{
public:
	ui::surface_ptr _surface = std::make_shared<ui::surface>();
	std::unique_ptr<av_scaler> _scaler;

	// Cache of the source scaled to a display size via FFmpeg's SIMD swscale, so full-screen video
	// is scaled once per frame with optimised code instead of a scalar per-pixel bilinear resample.
	std::unique_ptr<av_scaler> _display_scaler;
	ui::surface_ptr _display_surface;
	sizei _display_dims;
	double _display_time = -1.0;
	bool _display_valid = false;
	bool _display_high_quality = false;

	software_texture() = default;

	~software_texture() override
	{
		_scaler.reset();
	}

	bool is_valid() const override
	{
		return ui::is_valid(_surface);
	}

	// Convert an NV12/P010 (YUV 4:2:0) source into the stored BGRA surface. The CPU canvas can only
	// present BGRA, so YUV frames/images that would be colour-converted on the GPU are converted here.
	ui::texture_update_result update_yuv(const ui::surface& src)
	{
		_display_valid = false; // source changed - drop the display-scaled cache
		if (!_scaler) _scaler = std::make_unique<av_scaler>();

		const auto dimensions = src.dimensions();
		const auto result = (_surface->empty() || _surface->dimensions() != dimensions)
			                    ? ui::texture_update_result::tex_created
			                    : ui::texture_update_result::tex_updated;

		if (!_scaler->convert_yuv_surface(src, _surface))
		{
			// alloc returns null rather than throwing when the buffer cannot be reserved, and the
			// conversion below writes straight into it.
			if (!_surface->alloc(dimensions, ui::texture_format::RGB, src.orientation(), src.time()))
			{
				return ui::texture_update_result::failed;
			}

			_surface->color_space(src.color_space());
			convert_yuv_to_bgra(src, *_surface);
		}

		_dimensions = dimensions;
		_format = ui::texture_format::RGB;
		_orientation = src.orientation();
		return result;
	}

	ui::texture_update_result update(const sizei dimensions, const ui::texture_format format,
	                                 const ui::orientation orientation, const uint8_t* pixels, const size_t stride,
	                                 const size_t buffer_size) override
	{
		if (dimensions.cx < 1 || dimensions.cy < 1 || !pixels || stride == 0)
		{
			return ui::texture_update_result::failed;
		}

		_display_valid = false; // source changed - drop the display-scaled cache

		const auto result = (_surface->empty() || _surface->dimensions() != dimensions)
			                    ? ui::texture_update_result::tex_created
			                    : ui::texture_update_result::tex_updated;

		// alloc returns null rather than throwing when the buffer cannot be reserved, and the
		// copy below writes straight into it.
		if (!_surface->alloc(dimensions, format, orientation))
		{
			return ui::texture_update_result::failed;
		}

		const auto copy_stride = std::min(_surface->stride(), stride);
		const auto rows = std::min(static_cast<size_t>(dimensions.cy),
		                           buffer_size ? buffer_size / stride : dimensions.cy);

		for (size_t y = 0; y < rows; ++y)
		{
			memcpy(_surface->pixels_line(static_cast<int>(y)), pixels + y * stride, copy_stride);
		}

		_dimensions = dimensions;
		_format = format;
		_orientation = orientation;
		return result;
	}

	ui::texture_update_result update(const av_frame_ptr& frame) override
	{
		if (!_scaler) _scaler = std::make_unique<av_scaler>();

		_display_valid = false; // new frame - drop the display-scaled cache

		// The first frame must report tex_created like the two overloads above: tex_updated maps to a
		// redraw, which replays the scene recorded before this texture existed, so the video would never
		// be drawn at all.
		const auto was_empty = _surface->empty();

		if (_scaler->scale_surface(frame, _surface))
		{
			const auto created = was_empty || _dimensions != _surface->dimensions();
			_dimensions = _surface->dimensions();
			_format = _surface->format();
			_orientation = _surface->orientation();
			return created ? ui::texture_update_result::tex_created : ui::texture_update_result::tex_updated;
		}

		return ui::texture_update_result::failed;
	}

	ui::texture_update_result update(const ui::const_surface_ptr& s) override
	{
		df::assert_true(ui::is_ui_thread());

		if (ui::is_valid(s))
		{
			const auto fmt = s->format();

			if (fmt == ui::texture_format::NV12 || fmt == ui::texture_format::P010)
			{
				return update_yuv(*s);
			}

			return update(s->dimensions(), s->format(), s->orientation(), s->pixels(), s->stride(), s->size());
		}

		return ui::texture_update_result::failed;
	}

	// Return the source surface scaled to dst (BGRA), cached until the source frame changes. Falls
	// back to the unscaled source if the format is unsupported by swscale.
	const ui::surface_ptr& display_scaled(const sizei dst, const bool high_quality)
	{
		if (_surface->dimensions() == dst) return _surface;

		if (_display_valid && _display_dims == dst && _display_high_quality == high_quality) return _display_surface;

		if (!_display_scaler) _display_scaler = std::make_unique<av_scaler>();

		if (_display_scaler->scale_surface(_surface, _display_surface, dst, high_quality) && ui::is_valid(
			_display_surface))
		{
			_display_dims = dst;
			_display_time = _surface->time();
			_display_high_quality = high_quality;
			_display_valid = true;
			return _display_surface;
		}

		_display_valid = false;
		return _surface;
	}
};

using software_texture_ptr = std::shared_ptr<software_texture>;

// Detect an axis-aligned, upright (non-rotated, non-flipped) quad and return it as a rect, so the
// common non-rotated texture draw can take the faster rect / swscale path instead of blit_quad.
static bool quad_upright_rect(const quadd& q, recti& out)
{
	const auto p0 = q[0];
	const auto p1 = q[1];
	const auto p2 = q[2];
	const auto p3 = q[3];

	constexpr double eps = 0.01;
	const auto eq = [](const double a, const double b) { return std::abs(a - b) < eps; };

	if (eq(p0.Y, p1.Y) && eq(p2.Y, p3.Y) && eq(p0.X, p3.X) && eq(p1.X, p2.X) &&
		p1.X - p0.X > eps && p3.Y - p0.Y > eps)
	{
		out = recti(df::round(p0.X), df::round(p0.Y), df::round(p1.X), df::round(p2.Y));
		return true;
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////////////////////////
// software_vertices - coloured bars (audio visualizer).
//////////////////////////////////////////////////////////////////////////////////////////////

class software_vertices final : public ui::vertices
{
public:
	std::vector<recti> _rects;
	std::vector<ui::color> _colors;

	void update(recti rects[], ui::color colors[], const int num_bars) override
	{
		_rects.assign(rects, rects + num_bars);
		_colors.assign(colors, colors + num_bars);
	}
};

//////////////////////////////////////////////////////////////////////////////////////////////
// software_draw_context
//////////////////////////////////////////////////////////////////////////////////////////////

class software_text_renderer;

// Edge of the fixed scratch tile the software backend rasterises into. The scene is retained, so
// render() replays it once per tile instead of keeping a buffer the size of the window: 512x512 is
// 1 MiB, the largest power-of-two square that stays resident in a typical per-core L2.
constexpr int software_tile_extent = 512;

class software_draw_context final : public draw_context_device,
                                    public std::enable_shared_from_this<software_draw_context>
{
public:
	factories_ptr _f;
	HWND _hwnd = nullptr;
	bool _layered = false;
	int _base_font_size = normal_font_size;
	int _layer_alpha = 255;

	software_canvas _canvas;
	std::vector<recti> _clip_stack;

	// Recorded command list (retained mode). Each draw call appends a closure; render() replays
	// them into the DIB. This lets redraw() re-present a frame - re-sampling textures updated in
	// place (e.g. video) - without re-running the host's paint logic, matching the Direct3D
	// backend's behaviour so the software renderer is a drop-in replacement.
	std::vector<std::function<void()>> _scene;
	// True while replay_scene() is running. Some draw paths re-enter the context during replay
	// (e.g. font_renderer::draw calls draw_rounded_rect to paint a text background); when replaying
	// those calls must rasterise immediately rather than append to _scene, which is being iterated.
	bool _replaying = false;

	HDC _mem_dc = nullptr;
	HBITMAP _dib = nullptr;
	HGDIOBJ _old_bitmap = nullptr;
	uint8_t* _bits = nullptr;
	// The scratch tile, not the window: one tile edge of software_tile_extent unless the client is
	// smaller, or the whole client for a layered window (which cannot be tiled - see render).
	sizei _dib_size;
	// Window-space position of the tile currently being rasterised.
	pointi _dib_origin;
	// The window client size, which is the space the recorded scene is laid out in.
	sizei _client_extent;

	// Region this frame is allowed to touch, and whether the scene's own opening clear already
	// covers it. Both are set by begin_draw and consumed by replay_scene / render.
	recti _damage;
	bool _scene_covers_damage = false;

	// UpdateLayeredWindow needs premultiplied alpha in its own DIB. It is kept across presents so a
	// layered window does not allocate a buffer, a DC and a DIB section on every frame.
	HDC _layered_dc = nullptr;
	HBITMAP _layered_dib = nullptr;
	HGDIOBJ _layered_old_bitmap = nullptr;
	uint8_t* _layered_bits = nullptr;
	sizei _layered_size;

	ui::surface_ptr _shadow;
	ui::surface_ptr _inverse_shadow;

	std::map<ui::style::font_face, std::shared_ptr<software_text_renderer>> _text_renderers;

	software_draw_context(const factories_ptr& f, const HWND hwnd, const bool layered, const int base_font_size)
		: _f(f), _hwnd(hwnd), _layered(layered), _base_font_size(base_font_size)
	{
	}

	~software_draw_context() override
	{
		free_dib();
	}

	void free_dib()
	{
		if (_mem_dc)
		{
			if (_old_bitmap) SelectObject(_mem_dc, _old_bitmap);
			DeleteDC(_mem_dc);
			_mem_dc = nullptr;
			_old_bitmap = nullptr;
		}

		if (_dib)
		{
			DeleteObject(_dib);
			_dib = nullptr;
		}

		_bits = nullptr;
		_dib_size = {};
		_dib_origin = {};
		sync_canvas();

		free_layered_dib();
	}

	void free_layered_dib()
	{
		if (_layered_dc)
		{
			if (_layered_old_bitmap) SelectObject(_layered_dc, _layered_old_bitmap);
			DeleteDC(_layered_dc);
			_layered_dc = nullptr;
			_layered_old_bitmap = nullptr;
		}

		if (_layered_dib)
		{
			DeleteObject(_layered_dib);
			_layered_dib = nullptr;
		}

		_layered_bits = nullptr;
		_layered_size = {};
	}

	recti tile_bounds() const noexcept
	{
		return {_dib_origin.x, _dib_origin.y, _dib_origin.x + _dib_size.cx, _dib_origin.y + _dib_size.cy};
	}

	// The canvas is a view onto the DIB, so it must be re-pointed wherever the DIB is created or
	// released. Doing it in one place keeps a failed or zero-size allocation from leaving the canvas
	// describing a non-empty surface backed by freed (or null) pixels.
	void sync_canvas()
	{
		_canvas._bits = _bits;
		_canvas._stride = _dib_size.cx * 4;
		_canvas._buffer_extent = _dib_size;
		_canvas._origin = _dib_origin;
		_canvas._clip = tile_bounds();
		_canvas._opaque = !_layered;
	}

	void set_tile_origin(const pointi origin)
	{
		_dib_origin = origin;
		_canvas._origin = origin;
	}

	sizei buffer_extent_for(const sizei client) const
	{
		// UpdateLayeredWindow presents the whole surface, so a layered window cannot be tiled and
		// keeps a full-client buffer.
		if (_layered) return client;

		// Capped at the tile so the allocation never tracks the window, floored at what is already
		// allocated so a resize drag cannot reallocate on the way back down.
		const auto edge = [](const int want, const int have)
		{
			return std::max(have, std::min(software_tile_extent, std::max(want, 1)));
		};

		return {edge(client.cx, _dib_size.cx), edge(client.cy, _dib_size.cy)};
	}

	void ensure_dib(const sizei sz)
	{
		if (_dib && _dib_size == sz && _bits)
		{
			return;
		}

		free_dib();

		if (sz.cx < 1 || sz.cy < 1)
		{
			return;
		}

		BITMAPINFO bmi = {};
		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = sz.cx;
		bmi.bmiHeader.biHeight = -sz.cy; // top-down
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;

		_mem_dc = CreateCompatibleDC(nullptr);

		if (_mem_dc)
		{
			void* bits = nullptr;
			_dib = CreateDIBSection(_mem_dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);

			if (_dib && bits)
			{
				_old_bitmap = SelectObject(_mem_dc, _dib);
				_bits = static_cast<uint8_t*>(bits);
				_dib_size = sz;
			}
			else
			{
				free_dib();
			}
		}

		sync_canvas();
	}

	software_text_renderer* renderer_for(ui::style::font_face font);

	// draw_context_device -------------------------------------------------------------------

	bool is_valid() const override
	{
		return _bits != nullptr;
	}

	void reset_damage() override
	{
		_damage = recti(0, 0, _client_extent.cx, _client_extent.cy);
		_scene_covers_damage = false;
	}

	void begin_draw(const sizei client_extent, const int base_font_size, const recti damage) override
	{
		// Through update_font_size, not a bare assignment: recording the size here without
		// discarding the text renderers would leave them built at the previous size while
		// making the later update_font_size call believe it had nothing to do.
		update_font_size(base_font_size);
		_client_extent = client_extent;
		ensure_dib(buffer_extent_for(client_extent));

		const recti client(0, 0, client_extent.cx, client_extent.cy);

		// The tile carries nothing between frames, so a partial repaint needs no retained pixels:
		// every pixel of the damaged region is written each frame, by the scene's own opening clear
		// or by the neutral pre-clear in replay_scene. A layered window is presented whole.
		_damage = (damage.is_empty() || _layered)
			          ? client
			          : damage.intersection(client);

		_scene_covers_damage = false;
		_clip_stack.clear();
		// Recording-phase clip: the widest region the frame may touch, so clip_bounds() queries and
		// the clip captured by draw_texture see the window, not whichever tile replay reaches first.
		_canvas._clip = _damage;
		_scene.clear();
	}

	// Replay the recorded command list into the tile. Called for every tile of every present, so that
	// redraw() (which re-presents without re-running the host paint logic) reflects textures whose
	// contents changed in place - e.g. a new video frame decoded into the same software_texture.
	// Correct to run per tile because every primitive derives its colour, coverage and source mapping
	// from its own bounds and uses the clip only as a write mask.
	void replay_scene(const recti tile_clip)
	{
		_clip_stack.clear();
		_canvas._clip = tile_clip;

		// The hardware backend clears the render target before drawing the scene, so anything the
		// scene does not paint shows as this neutral grey rather than black. Layered windows are
		// composited by the shell and must stay transparent where nothing is drawn. A scene that
		// opens by covering the damaged region opaquely leaves nothing to show through, so the
		// pre-clear would only be overwritten.
		if (!_layered && !_scene_covers_damage)
		{
			_canvas.clear(ui::color(scene_clear_shade, scene_clear_shade, scene_clear_shade, 1.0f));
		}

		struct replay_scope
		{
			bool& flag;
			~replay_scope() { flag = false; }
		} scope{_replaying};
		_replaying = true;

		// Iterate by index: a re-entrant draw during replay runs immediately (see record_or_run)
		// and never appends, so the size is stable, but this is defensive against reallocation.
		for (size_t i = 0; i < _scene.size(); ++i)
		{
			_scene[i]();
		}
	}

	// Append a draw command while recording, or run it immediately when called re-entrantly during
	// replay (so it does not mutate the _scene vector that replay_scene() is iterating).
	template <typename F>
	void record_or_run(F&& f)
	{
		if (_replaying) f();
		else _scene.emplace_back(std::forward<F>(f));
	}

	// Walk the scratch tile across a region, positioning the canvas for each step. Anchored to the
	// region, not to a global grid: nothing is reused between frames, so a small repaint straddling
	// a grid line should still cost one tile. Shared with the tiling probe so tests drive this loop.
	template <typename F>
	void for_each_tile(const recti region, F&& fn)
	{
		// The step is the tile, so a degenerate one would not advance: fail the frame, never hang.
		if (_dib_size.cx < 1 || _dib_size.cy < 1) return;

		for (auto y = region.top; y < region.bottom; y += _dib_size.cy)
		{
			for (auto x = region.left; x < region.right; x += _dib_size.cx)
			{
				const auto tile = recti(x, y, x + _dib_size.cx, y + _dib_size.cy).intersection(region);
				if (tile.is_empty()) continue;

				set_tile_origin({x, y});
				fn(tile);
			}
		}
	}

	HRESULT render() override
	{
		if (!_bits || !_hwnd) return S_OK;

		const auto region = _damage.intersection(recti(0, 0, _client_extent.cx, _client_extent.cy));
		if (region.is_empty()) return S_OK;

		// A layered window is presented whole by UpdateLayeredWindow, so it cannot be tiled: its
		// buffer is the full client and begin_draw forces full damage, which makes the loop below a
		// single tile covering everything.
		const auto dc = _layered ? nullptr : GetDC(_hwnd);
		if (!_layered && !dc) return S_OK;

		for_each_tile(region, [&](const recti tile)
		{
			replay_scene(tile);

			if (dc)
			{
				BitBlt(dc, tile.left, tile.top, tile.width(), tile.height(),
				       _mem_dc, tile.left - _dib_origin.x, tile.top - _dib_origin.y, SRCCOPY);
			}
		});

		if (dc) ReleaseDC(_hwnd, dc);
		else present_layered();

		return S_OK;
	}

	void present_layered()
	{
		if (!_bits || _dib_size.cx < 1 || _dib_size.cy < 1)
		{
			return;
		}

		if (!_layered_bits || _layered_size != _dib_size)
		{
			free_layered_dib();

			BITMAPINFO bmi = {};
			bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			bmi.bmiHeader.biWidth = _dib_size.cx;
			bmi.bmiHeader.biHeight = -_dib_size.cy;
			bmi.bmiHeader.biPlanes = 1;
			bmi.bmiHeader.biBitCount = 32;
			bmi.bmiHeader.biCompression = BI_RGB;

			_layered_dc = CreateCompatibleDC(nullptr);

			if (!_layered_dc)
			{
				return;
			}

			void* bits = nullptr;
			_layered_dib = CreateDIBSection(_layered_dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);

			if (!_layered_dib || !bits)
			{
				free_layered_dib();
				return;
			}

			_layered_old_bitmap = SelectObject(_layered_dc, _layered_dib);
			_layered_bits = static_cast<uint8_t*>(bits);
			_layered_size = _dib_size;
		}

		// UpdateLayeredWindow requires premultiplied alpha.
		const auto* src = _bits;
		auto* dst = _layered_bits;

		for (auto i = 0; i < _dib_size.cx * _dib_size.cy; ++i, src += 4, dst += 4)
		{
			const auto a = src[3];
			dst[0] = static_cast<uint8_t>(src[0] * a / 255);
			dst[1] = static_cast<uint8_t>(src[1] * a / 255);
			dst[2] = static_cast<uint8_t>(src[2] * a / 255);
			dst[3] = a;
		}

		const auto screen_dc = GetDC(nullptr);

		if (!screen_dc)
		{
			return;
		}

		RECT wr = {};

		if (!GetWindowRect(_hwnd, &wr))
		{
			ReleaseDC(nullptr, screen_dc);
			return;
		}

		POINT pt_dst = {wr.left, wr.top};
		POINT pt_src = {0, 0};
		SIZE size = {_dib_size.cx, _dib_size.cy};
		BLENDFUNCTION bf = {AC_SRC_OVER, 0, static_cast<BYTE>(_layer_alpha), AC_SRC_ALPHA};

		UpdateLayeredWindow(_hwnd, screen_dc, &pt_dst, &size, _layered_dc, &pt_src, 0, &bf, ULW_ALPHA);

		ReleaseDC(nullptr, screen_dc);
	}

	void resize(const sizei client_extent) override
	{
		_client_extent = client_extent;
		ensure_dib(buffer_extent_for(client_extent));
	}

	void destroy() override
	{
		_scene.clear();
		_text_renderers.clear();
		_shadow.reset();
		_inverse_shadow.reset();
		free_dib();
	}

	void update_font_size(const int base_font_size) override
	{
		// Only rebuild when the size actually changes: this runs on every options change, and
		// discarding the renderers re-rasterises every glyph the next frame needs.
		if (_base_font_size == base_font_size) return;
		_base_font_size = base_font_size;
		_text_renderers.clear();
	}

	void set_layer_alpha(const int a) override
	{
		_layer_alpha = std::clamp(a, 0, 255);
	}

	// clip ----------------------------------------------------------------------------------

	recti clip_bounds() const override
	{
		return _canvas._clip;
	}

	void clip_bounds(const recti r) override
	{
		// A re-entrant clip during replay must apply immediately; appending here would grow the
		// command list that replay_scene() is iterating and re-apply the clip on every later frame.
		if (_replaying)
		{
			_clip_stack.push_back(_canvas._clip);
			_canvas._clip = _canvas._clip.intersection(r);
			return;
		}

		// Maintain the clip during recording so clip_bounds() queries are correct, and record the
		// same operation so it is re-applied identically during replay.
		_clip_stack.push_back(_canvas._clip);
		_canvas._clip = _canvas._clip.intersection(r);

		_scene.emplace_back([this, r]
		{
			_clip_stack.push_back(_canvas._clip);
			_canvas._clip = _canvas._clip.intersection(r);
		});
	}

	void restore_clip() override
	{
		if (!_clip_stack.empty())
		{
			_canvas._clip = _clip_stack.back();
			_clip_stack.pop_back();
		}

		if (_replaying)
		{
			return;
		}

		_scene.emplace_back([this]
		{
			if (!_clip_stack.empty())
			{
				_canvas._clip = _clip_stack.back();
				_clip_stack.pop_back();
			}
		});
	}

	// primitives ----------------------------------------------------------------------------

	void clear(const ui::color c) override
	{
		const recti bounds(0, 0, _client_extent.cx, _client_extent.cy);

		// Recorded as the first command, opaque, and covering everything the frame may touch: the
		// neutral pre-clear in replay_scene cannot survive it, so replay_scene skips it.
		if (_scene.empty() && !_replaying && c.a >= 1.0f && bounds.intersection(_damage) == _damage)
		{
			_scene_covers_damage = true;
		}

		record_or_run([this, bounds, c] { _canvas.fill_rect(bounds, c); });
	}

	void draw_rect(const recti bounds, const ui::color c) override
	{
		record_or_run([this, bounds, c] { _canvas.fill_rect(bounds, c); });
	}

	void draw_rect_gradient(const recti bounds, const ui::color c_centre, const ui::color c_corner) override
	{
		record_or_run([this, bounds, c_centre, c_corner]
		{
			_canvas.fill_rect_gradient(bounds, c_centre, c_corner);
		});
	}

	// The hardware backend inflates by 2, fills the body with c.emphasize() and lets the circle
	// pixel shader fade the outside away: the visible shape is a rounded rect whose edge sits at
	// 0.833 * (radius + 2) from the corner centres, which is what is reproduced here. The four
	// corner vertices that carry the un-emphasized colour are all in the faded-out region.
	void draw_rounded_rect(const recti bounds, const ui::color c, const int radius) override
	{
		const auto edge = 0.83333f * (radius + 2);
		const auto grow = std::max(0, df::round(edge - radius));
		const auto r = df::round(edge);

		record_or_run([this, bounds, c, grow, r]
		{
			_canvas.fill_rounded_rect(bounds.inflate(grow), c.emphasize(), r);
		});
	}

	void draw_border(const recti inside, const recti outside, const ui::color c_inside,
	                 const ui::color c_outside) override
	{
		record_or_run([this, inside, outside, c_inside, c_outside]
		{
			_canvas.fill_border_gradient(inside, outside, c_inside, c_outside);
		});
	}

	void draw_vertices(const ui::vertices_ptr& v) override
	{
		auto vv = std::dynamic_pointer_cast<software_vertices>(v);
		if (!vv) return;

		// Capture the vertices object (not a snapshot) and read its geometry at replay time. The
		// audio visualizer mutates the same vertices object in place each frame and then calls
		// redraw() (which replays without re-recording), so replay must see the latest bars - the
		// same way a video texture updated in place is re-sampled on replay.
		record_or_run([this, vv = std::move(vv)]
		{
			// Same treatment as the hardware backend: a drop shadow behind each bar (suppressed for
			// flat bars) and then the bar as a centre gradient - note the bar carries the plain
			// colour at its corners and the emphasized one in the middle, the opposite of draw_rect.
			for (size_t i = 0; i < vv->_rects.size(); ++i)
			{
				const auto r = vv->_rects[i];
				const auto c = vv->_colors[i];

				if (r.height() > 1) do_draw_shadow(r, 8, c.a / 2.0f, false);
				_canvas.fill_rect_gradient(r, c.emphasize(), c);
			}
		});
	}

	ui::vertices_ptr create_vertices() override
	{
		return std::make_shared<software_vertices>();
	}

	// textures ------------------------------------------------------------------------------

	ui::texture_ptr create_texture() override
	{
		df::assert_true(ui::is_ui_thread());
		return std::make_shared<software_texture>();
	}

	// `visible` is the clip as the host saw it when the call was recorded, not the live clip: replay
	// runs per tile, and testing the tile would reject every destination below.
	void draw_texture_impl(const software_texture_ptr& t, const recti dst, const recti src, const float alpha,
	                       const ui::texture_sampler sampler, const recti visible) const
	{
		if (!t || !ui::is_valid(t->_surface)) return;

		const auto has_alpha = t->_surface->format() == ui::texture_format::ARGB;
		const auto src_dims = t->_surface->dimensions();
		const bool full_src = src.left <= 0 && src.top <= 0 && src.right >= src_dims.cx && src.bottom >= src_dims.cy;
		const bool scaling = dst.width() != src.width() || dst.height() != src.height();
		const bool fully_visible = visible.intersection(dst) == dst;

		// Fast path: an opaque image/video scaled from its full source. Scale once with FFmpeg's SIMD
		// swscale to a display-sized surface, then a 1:1 copy - far faster than the scalar per-pixel
		// bilinear resample below. Keep this to wholly visible destinations: a magnified photo can have
		// a very large nominal destination, while direct sampling below only visits the clipped viewport.
		// Point sampling is excluded because swscale has no point filter here and would blur the
		// deliberately crisp source pixels of a heavily magnified image.
		if (!has_alpha && full_src && scaling && fully_visible && alpha >= 0.999f &&
			sampler != ui::texture_sampler::point)
		{
			const auto& scaled = t->display_scaled(dst.extent(), sampler == ui::texture_sampler::bicubic);

			if (ui::is_valid(scaled) && scaled->dimensions() == dst.extent())
			{
				_canvas.blit_copy(*scaled, dst, alpha, false);
				return;
			}
		}

		_canvas.blit_surface(*t->_surface, src, dst, alpha, sampler, has_alpha);
	}

	void draw_texture(const ui::texture_ptr& t, const recti dst, const float alpha,
	                  const ui::texture_sampler sampler) override
	{
		if (!t) return;
		auto tt = std::dynamic_pointer_cast<software_texture>(t);
		const recti src(pointi(0, 0), t->dimensions());
		const auto visible = _canvas._clip;
		record_or_run([this, tt = std::move(tt), dst, src, alpha, sampler, visible]
		{
			draw_texture_impl(tt, dst, src, alpha, sampler, visible);
		});
	}

	void draw_texture(const ui::texture_ptr& t, const recti dst, const recti src, const float alpha,
	                  const ui::texture_sampler sampler, const float radius) override
	{
		auto tt = std::dynamic_pointer_cast<software_texture>(t);
		const auto visible = _canvas._clip;
		record_or_run([this, tt = std::move(tt), dst, src, alpha, sampler, visible]
		{
			draw_texture_impl(tt, dst, src, alpha, sampler, visible);
		});
	}

	void draw_texture(const ui::texture_ptr& t, const quadd& dst, const recti src, const float alpha,
	                  const ui::texture_sampler sampler) override
	{
		auto tt = std::dynamic_pointer_cast<software_texture>(t);
		const auto visible = _canvas._clip;
		record_or_run([this, tt = std::move(tt), dst, src, alpha, sampler, visible]
		{
			if (!tt || !ui::is_valid(tt->_surface)) return;

			// A non-rotated / non-flipped quad is just a rectangle: take the faster rect path (which
			// includes the swscale fast path for full-screen video).
			recti rect;
			if (quad_upright_rect(dst, rect))
			{
				draw_texture_impl(tt, rect, src, alpha, sampler, visible);
				return;
			}

			const auto has_alpha = tt->_surface->format() == ui::texture_format::ARGB;
			_canvas.blit_quad(*tt->_surface, src, dst, alpha, sampler, has_alpha);
		});
	}

	void draw_texture(const ui::texture_ptr& t, const quadd& dst, const recti src, const float alpha,
	                  const ui::texture_sampler sampler, const ui::texture_transform& transform) override
	{
		auto tt = std::dynamic_pointer_cast<software_texture>(t);
		record_or_run([this, tt = std::move(tt), dst, src, alpha, sampler, transform]
		{
			if (!tt || !ui::is_valid(tt->_surface)) return;
			const auto has_alpha = tt->_surface->format() == ui::texture_format::ARGB;
			_canvas.blit_quad(*tt->_surface, src, dst, alpha, sampler, has_alpha, &transform);
		});
	}

	// shadows -------------------------------------------------------------------------------

	// The nine-slice split must match build_shadow_vertices in the hardware backend or the two
	// backends draw visibly different shadows. The artwork is a centred radial blob, so each
	// quadrant of the source is a corner tile and each edge stretches the two-pixel band that
	// straddles the centre line.
	void stretch_shadow(const ui::surface& s, const recti r, const int dst_width, const float alpha) const
	{
		const auto ext_w = static_cast<float>(s.width());
		const auto ext_h = static_cast<float>(s.height());

		if (ext_w < 4.0f || ext_h < 4.0f) return;

		const auto half_w = ext_w / 2.0f;
		const auto half_h = ext_h / 2.0f;

		const auto dw = static_cast<float>(dst_width);

		const auto to_src = [](const float x, const float y, const float cx, const float cy)
		{
			return recti(df::round(x), df::round(y), df::round(x + cx), df::round(y + cy));
		};

		const auto width = static_cast<float>(r.width());
		const auto height = static_cast<float>(r.height());
		const auto left = static_cast<float>(r.left);
		const auto top = static_cast<float>(r.top);
		const auto right = static_cast<float>(r.right);
		const auto bottom = static_cast<float>(r.bottom);

		auto draw = [&](const float dx, const float dy, const float dcx, const float dcy, const float sx,
		                const float sy, const float scx, const float scy)
		{
			const recti dst_r(df::round(dx), df::round(dy), df::round(dx + dcx), df::round(dy + dcy));
			_canvas.blit_surface(s, to_src(sx, sy, scx, scy), dst_r, alpha, ui::texture_sampler::bilinear, true);
		};

		// edges - the two-pixel band across the centre, stretched along the run
		draw(left + dw, top, width - dw * 2.0f, dw, half_w - 1.0f, 0.0f, 2.0f, half_h); // top
		draw(left + dw, bottom - dw, width - dw * 2.0f, dw, half_w - 1.0f, half_h, 2.0f, half_h); // bottom
		draw(left, top + dw, dw, height - dw * 2.0f, 0.0f, half_h - 1.0f, half_w, 2.0f); // left
		draw(right - dw, top + dw, dw, height - dw * 2.0f, half_w, half_h - 1.0f, half_w, 2.0f); // right

		// corners - one source quadrant each
		draw(left, top, dw, dw, 0.0f, 0.0f, half_w, half_h); // tl
		draw(right - dw, top, dw, dw, half_w, 0.0f, half_w, half_h); // tr
		draw(left, bottom - dw, dw, dw, 0.0f, half_h, half_w, half_h); // bl
		draw(right - dw, bottom - dw, dw, dw, half_w, half_h, half_w, half_h); // br
	}

	// The shadow is drawn in the band of `width` pixels outside `bounds`, which is what the
	// hardware backend's shadow vertices cover. `width` must be honoured - draw_edge_shadows and
	// the audio visualizer both pass sizes other than the frame default.
	void do_draw_shadow(const recti bounds, const int width, const float alpha, const bool inverse)
	{
		if (width <= 0 || alpha <= 0.0f) return;

		if (!_shadow) _shadow = decode_png_resource_to_surface(_f, IDB_SHADOW);
		if (!_inverse_shadow) _inverse_shadow = decode_png_resource_to_surface(_f, IDB_INVERSE_SHADOW);

		const auto& s = inverse ? _inverse_shadow : _shadow;
		if (ui::is_valid(s)) stretch_shadow(*s, bounds.inflate(width), width, alpha);
	}

	void draw_shadow(const recti bounds, const int width, const float alpha, const bool inverse) override
	{
		record_or_run([this, bounds, width, alpha, inverse] { do_draw_shadow(bounds, width, alpha, inverse); });
	}

	void do_edge_shadows(const float alpha)
	{
		// Same geometry as the hardware backend.
		const auto size = std::min(std::min(_client_extent.cx / 2, _client_extent.cy / 2), 96);
		do_draw_shadow(recti(0, 0, _client_extent.cx, _client_extent.cy).inflate(-size), size, alpha, true);
	}

	void draw_edge_shadows(const float alpha) override
	{
		record_or_run([this, alpha] { do_edge_shadows(alpha); });
	}

	// bubble --------------------------------------------------------------------------------

	void do_bubble_background(recti bounds, pointi focus_location, int padding, float radius);

	void draw_bubble_background(const recti bounds, const pointi focus_location, const int padding,
	                            const float radius) override
	{
		record_or_run([this, bounds, focus_location, padding, radius]
		{
			do_bubble_background(bounds, focus_location, padding, radius);
		});
	}

	// text ----------------------------------------------------------------------------------

	void draw_text(std::string_view text, recti bounds, ui::style::font_face font, ui::style::text_style style,
	               ui::color c, ui::color bg) override;
	void draw_text(std::string_view text, const std::vector<ui::text_highlight_t>& highlights, recti bounds,
	               ui::style::font_face font, ui::style::text_style style, ui::color clr, ui::color bg) override;
	void draw_text(const ui::text_layout_ptr& tl, recti bounds, ui::color clr, ui::color bg) override;

	sizei measure_text(const std::string_view text, const ui::style::font_face font,
	                   const ui::style::text_style style, const int width, const int height) override
	{
		const auto fr = _f->font_face(font, _base_font_size);
		if (fr) return fr->measure(text, style, width, height);
		return {};
	}

	int text_line_height(const ui::style::font_face font) override
	{
		const auto fr = _f->font_face(font, _base_font_size);
		if (fr) return fr->calc_line_height();
		return 0;
	}

	ui::text_layout_ptr create_text_layout(const ui::style::font_face font) override
	{
		df::assert_true(ui::is_ui_thread());
		const auto fr = _f->font_face(font, _base_font_size);
		if (fr) return std::make_shared<text_layout_impl>(fr, font);
		return nullptr;
	}
};

//////////////////////////////////////////////////////////////////////////////////////////////
// software_text_renderer - IDWriteTextRenderer that alpha-blends glyphs into the canvas.
//////////////////////////////////////////////////////////////////////////////////////////////

class software_text_renderer final : public df::no_copy, public IDWriteTextRenderer
{
	software_draw_context* _ctx = nullptr;
	font_renderer_ptr _font;
	int _spacing = 2;
	int _line_height = 0;
	int _base_line_height = 0;

	df::hash_map<uint64_t, render_char_result> _glyph_cache;
	glyph_face_keys _glyph_keys;

	ui::color _clr;
	std::vector<ui::text_highlight_t> _highlights;

	std::atomic<int> _ref_count = 0;

public:
	software_text_renderer(software_draw_context* ctx, font_renderer_ptr font)
		: _ctx(ctx), _font(std::move(font))
	{
		_line_height = _font->calc_line_height();
		_base_line_height = _font->calc_base_line_height();
	}

	~software_text_renderer() override = default;

	font_renderer_ptr font() const { return _font; }

	void draw_text(const std::string_view text, const recti bounds, const ui::style::text_style style,
	               const ui::color c, const ui::color bg)
	{
		_clr = c;
		_highlights.clear();
		_font->draw(_ctx, this, text, bounds, style, c, bg);
	}

	void draw_text(const std::string_view text, const std::vector<ui::text_highlight_t>& highlights, const recti bounds,
	               const ui::style::text_style style, const ui::color clr, const ui::color bg)
	{
		_clr = clr;
		_highlights.clear();
		_highlights.reserve(highlights.size());

		std::wstring w;
		w.reserve(text.size());

		auto i_text = text.begin();
		auto i_highlights = highlights.begin();

		while (i_text < text.end())
		{
			const auto i = std::distance(text.begin(), i_text);
			w.append(1, static_cast<wchar_t>(str::pop_utf8_char(i_text, text.end())));

			if (i_highlights != highlights.end() && i == i_highlights->offset)
			{
				_highlights.emplace_back(static_cast<uint32_t>(w.size() - 1u), i_highlights->length, i_highlights->clr);
				++i_highlights;
			}
		}

		_font->draw(_ctx, this, w, bounds, style, clr, bg, _highlights);
	}

	void draw_layout(const std::shared_ptr<text_layout_impl>& text, const recti bounds, const ui::color clr,
	                 const ui::color bg)
	{
		_clr = clr;
		_highlights.clear();
		text->_renderer->draw(_ctx, this, text->_layout.Get(), bounds, clr, bg);
	}

	const render_char_result& glyph(const uint16_t index, const DWRITE_GLYPH_RUN* glyph_run)
	{
		// Key by the face the index belongs to and by the run's em size, so a fallback glyph
		// (e.g. Hangul from Malgun Gothic) cannot collide in the cache with a same-indexed glyph
		// of the primary face, and a raster made for one font size is never reused at another.
		const auto key = _glyph_keys.key(glyph_run ? glyph_run->fontFace : nullptr,
		                                 glyph_run ? glyph_run->fontEmSize : 0.0f, index);
		const auto found = _glyph_cache.find(key);
		if (found != _glyph_cache.end()) return found->second;
		return _glyph_cache[key] = _font->render_glyph(index, _spacing, glyph_run);
	}

	// IUnknown ------------------------------------------------------------------------------

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
	{
		if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWritePixelSnapping) ||
			riid == __uuidof(IDWriteTextRenderer))
		{
			*ppvObject = static_cast<IDWriteTextRenderer*>(this);
			AddRef();
			return S_OK;
		}

		*ppvObject = nullptr;
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(++_ref_count); }

	ULONG STDMETHODCALLTYPE Release() override
	{
		// The renderer is owned by the draw context's map, so Release never destroys it. The count is
		// clamped at zero so an unbalanced Release cannot report a huge ULONG.
		auto n = _ref_count.load(std::memory_order_relaxed);
		while (n > 0 && !_ref_count.compare_exchange_weak(n, n - 1, std::memory_order_relaxed))
		{
		}
		return static_cast<ULONG>(n > 0 ? n - 1 : 0);
	}

	// IDWritePixelSnapping ------------------------------------------------------------------

	HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*, BOOL* isDisabled) override
	{
		*isDisabled = FALSE;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*, DWRITE_MATRIX* transform) override
	{
		*transform = {1, 0, 0, 1, 0, 0};
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*, FLOAT* pixelsPerDip) override
	{
		*pixelsPerDip = 1.0f;
		return S_OK;
	}

	// IDWriteTextRenderer -------------------------------------------------------------------

	HRESULT STDMETHODCALLTYPE DrawGlyphRun(void*, const FLOAT baselineOriginX, const FLOAT baselineOriginY,
	                                       DWRITE_MEASURING_MODE, const DWRITE_GLYPH_RUN* glyphRun,
	                                       const DWRITE_GLYPH_RUN_DESCRIPTION* glyphRunDescription,
	                                       IUnknown*) override
	{
		if (!glyphRun) return S_OK;

		auto char_pos = glyphRunDescription ? glyphRunDescription->textPosition : 0;
		auto i_highlights = _highlights.begin();

		const auto ty = std::floor(baselineOriginY + 0.5f);
		const auto is_left_to_right = (glyphRun->bidiLevel & 0x01) == 0;

		auto tx = 0.0f;

		for (auto i = 0u; i < glyphRun->glyphCount; ++i)
		{
			const auto c = glyphRun->glyphIndices[i];
			const auto ax = glyphRun->glyphAdvances[i];
			auto sx = tx;
			auto sy = ty - _base_line_height;

			if (glyphRun->glyphOffsets)
			{
				sx += glyphRun->glyphOffsets[i].advanceOffset;
				sy += glyphRun->glyphOffsets[i].ascenderOffset;
			}

			sx = is_left_to_right ? baselineOriginX + sx - _spacing : baselineOriginX - sx - ax - _spacing;

			if (c != 0)
			{
				auto clr = _clr;

				if (i_highlights != _highlights.end())
				{
					const auto begin = i_highlights->offset;
					const auto end = begin + i_highlights->length;

					if (char_pos >= begin && char_pos < end) clr = i_highlights->clr;
					if (char_pos >= end - 1u) ++i_highlights;
				}

				const auto& g = glyph(c, glyphRun);
				_ctx->_canvas.blend_glyph(df::round(sx), df::round(sy), g, clr);
			}

			tx += ax;
			char_pos += 1;
		}

		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE DrawUnderline(void*, FLOAT, FLOAT, const DWRITE_UNDERLINE*, IUnknown*) override
	{
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*, FLOAT, FLOAT, const DWRITE_STRIKETHROUGH*, IUnknown*) override
	{
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE
	DrawInlineObject(void*, FLOAT, FLOAT, IDWriteInlineObject*, BOOL, BOOL, IUnknown*) override
	{
		return S_OK;
	}
};

software_text_renderer* software_draw_context::renderer_for(const ui::style::font_face font)
{
	const auto found = _text_renderers.find(font);
	if (found != _text_renderers.end()) return found->second.get();

	const auto fr = _f->font_face(font, _base_font_size);
	if (!fr) return nullptr;

	const auto tr = std::make_shared<software_text_renderer>(this, fr);
	_text_renderers[font] = tr;
	return tr.get();
}

void software_draw_context::draw_text(const std::string_view text, const recti bounds, const ui::style::font_face font,
                                      const ui::style::text_style style, const ui::color c, const ui::color bg)
{
	record_or_run([this, text = std::string(text), bounds, font, style, c, bg]
	{
		if (!_canvas._clip.intersects(bounds)) return;
		auto* const tr = renderer_for(font);
		if (tr) tr->draw_text(text, bounds, style, c, bg);
	});
}

void software_draw_context::draw_text(const std::string_view text, const std::vector<ui::text_highlight_t>& highlights,
                                      const recti bounds, const ui::style::font_face font,
                                      const ui::style::text_style style, const ui::color clr, const ui::color bg)
{
	record_or_run([this, text = std::string(text), highlights, bounds, font, style, clr, bg]
	{
		if (!_canvas._clip.intersects(bounds)) return;
		auto* const tr = renderer_for(font);
		if (tr) tr->draw_text(text, highlights, bounds, style, clr, bg);
	});
}

void software_draw_context::draw_text(const ui::text_layout_ptr& tl, const recti bounds, const ui::color clr,
                                      const ui::color bg)
{
	df::assert_true(ui::is_ui_thread());

	auto t = std::dynamic_pointer_cast<text_layout_impl>(tl);
	if (!t) return;

	record_or_run([this, t = std::move(t), bounds, clr, bg]
	{
		if (!_canvas._clip.intersects(bounds)) return;
		auto* const tr = renderer_for(t->_font);
		if (tr) tr->draw_layout(t, bounds, clr, bg);
	});
}

void software_draw_context::do_bubble_background(const recti bounds, const pointi focus_location, const int padding,
                                                 const float radius)
{
	_canvas.clear(ui::color(0, 0, 0, 0));

	if (!_shadow) _shadow = decode_png_resource_to_surface(_f, IDB_SHADOW);
	if (ui::is_valid(_shadow))
	{
		stretch_shadow(*_shadow, recti(0, 0, bounds.width(), bounds.height()), 32, 1.0f);
	}

	const auto l = static_cast<float>(padding);
	const auto t = static_cast<float>(padding);
	const auto r = static_cast<float>(bounds.width() - padding);
	const auto b = static_cast<float>(bounds.height() - padding);

	const ui::color clr(ui::style::color::bubble_background, 1.0f);

	_canvas.fill_rounded_rect(recti(df::round(l), df::round(t), df::round(r), df::round(b)), clr, df::round(radius));

	const auto p = static_cast<float>(padding);

	if (focus_location.y <= bounds.top)
	{
		const auto center = std::clamp(static_cast<float>(focus_location.x - bounds.left), l + 24.0f, r - 24.0f);
		_canvas.fill_triangle({center - p, t}, {center, t - p}, {center + p, t}, clr);
	}
	else if (focus_location.x >= bounds.right)
	{
		const auto center = std::clamp(static_cast<float>(focus_location.y - bounds.top), t + 24.0f, b - 24.0f);
		_canvas.fill_triangle({r, center - p}, {r + p, center}, {r, center + p}, clr);
	}
	else if (focus_location.y >= bounds.bottom)
	{
		const auto center = std::clamp(static_cast<float>(focus_location.x - bounds.left), l + 24.0f, r - 24.0f);
		_canvas.fill_triangle({center + p, b}, {center, b + p}, {center - p, b}, clr);
	}
	else
	{
		const auto center = std::clamp(static_cast<float>(focus_location.y - bounds.top), t + 24.0f, b - 24.0f);
		_canvas.fill_triangle({l, center + p}, {l - p, center}, {l, center - p}, clr);
	}
}

draw_context_device_ptr create_software_draw_context(const factories_ptr& f, const HWND hwnd, const bool layered,
                                                     const int base_font_size)
{
	return std::make_shared<software_draw_context>(f, hwnd, layered, base_font_size);
}

platform::software_tiling_probe platform::probe_software_tiling()
{
	software_tiling_probe result;

	// Deliberately indivisible by the tile edge below, so tiles are clipped on both axes and no
	// primitive lands on a tile boundary by luck.
	constexpr sizei extent{203, 141};
	constexpr int tile_edge = 32;
	constexpr uint8_t fill = 0x40;

	const auto source = std::make_shared<ui::surface>();
	auto* const source_pixels = source->alloc(19, 13, ui::texture_format::ARGB, ui::orientation::top_left);
	if (!source_pixels) return result;

	for (auto y = 0; y < 13; ++y)
	{
		auto* const row = source_pixels + static_cast<ptrdiff_t>(y) * source->stride();

		for (auto x = 0; x < 19; ++x)
		{
			row[x * 4 + 0] = static_cast<uint8_t>(x * 11 + y * 3);
			row[x * 4 + 1] = static_cast<uint8_t>(x * 5 + y * 17);
			row[x * 4 + 2] = static_cast<uint8_t>(x * 23 + y * 7);
			row[x * 4 + 3] = static_cast<uint8_t>(128 + ((x + y) & 63));
		}
	}

	// One glyph-shaped alpha mask, so blend_glyph is covered without needing a font.
	render_char_result glyph;
	glyph.cx = 21;
	glyph.cy = 17;
	glyph.pixels.resize(static_cast<size_t>(glyph.cx) * glyph.cy);
	for (size_t i = 0; i < glyph.pixels.size(); ++i) glyph.pixels[i] = static_cast<uint8_t>(i * 37u + 11u);

	const recti source_rect(0, 0, 19, 13);

	// Every primitive that does non-trivial per-pixel arithmetic, positioned to straddle tile edges.
	const auto draw_scene = [&](const software_canvas& canvas)
	{
		canvas.fill_rect(recti(5, 7, 190, 44), ui::color(0.2f, 0.4f, 0.9f, 1.0f));
		canvas.fill_rect(recti(11, 15, 170, 39), ui::color(0.9f, 0.1f, 0.3f, 0.45f));
		canvas.fill_rect_gradient(recti(3, 33, 199, 96), ui::color(0.9f, 0.8f, 0.2f, 1.0f),
		                          ui::color(0.1f, 0.2f, 0.6f, 0.7f));
		canvas.fill_rounded_rect(recti(17, 51, 145, 121), ui::color(0.3f, 0.7f, 0.4f, 0.8f), 23);
		canvas.fill_border_gradient(recti(41, 63, 161, 111), recti(29, 55, 173, 123),
		                            ui::color(0.95f, 0.35f, 0.15f, 0.9f), ui::color(0.05f, 0.55f, 0.85f, 0.4f));
		canvas.fill_triangle({13.5, 97.25}, {121.75, 71.5}, {87.25, 137.75}, ui::color(0.6f, 0.2f, 0.8f, 0.55f));
		canvas.blend_glyph(57, 19, glyph, ui::color(1.0f, 1.0f, 0.4f, 0.85f));
		canvas.blend_glyph(131, 88, glyph, ui::color(0.2f, 0.9f, 1.0f, 0.7f));
		canvas.blit_surface(*source, source_rect, recti(63, 29, 187, 119), 0.75f,
		                    ui::texture_sampler::bilinear, true);
		canvas.blit_surface(*source, source_rect, recti(9, 83, 101, 133), 0.6f,
		                    ui::texture_sampler::bicubic, true);
		canvas.blit_surface(*source, source_rect, recti(151, 5, 199, 67), 1.0f,
		                    ui::texture_sampler::point, true);
		canvas.blit_quad(*source, source_rect,
		                 quadd(recti(23, 79, 119, 155)).rotate(27.0, pointd(71.0, 117.0)),
		                 0.8f, ui::texture_sampler::bilinear, true);
	};

	std::vector<uint8_t> reference(static_cast<size_t>(extent.cx) * extent.cy * 4, fill);
	std::vector<uint8_t> tiled(static_cast<size_t>(extent.cx) * extent.cy * 4, fill);

	software_canvas whole;
	whole._bits = reference.data();
	whole._stride = extent.cx * 4;
	whole._buffer_extent = extent;
	whole._clip = recti(0, 0, extent.cx, extent.cy);
	whole._opaque = true;
	draw_scene(whole);

	std::vector<uint8_t> tile_bits(static_cast<size_t>(tile_edge) * tile_edge * 4);

	for (auto ty = 0; ty < extent.cy; ty += tile_edge)
	{
		for (auto tx = 0; tx < extent.cx; tx += tile_edge)
		{
			const auto tile = recti(tx, ty, tx + tile_edge, ty + tile_edge)
				.intersection(recti(0, 0, extent.cx, extent.cy));

			std::ranges::fill(tile_bits, fill);

			software_canvas canvas;
			canvas._bits = tile_bits.data();
			canvas._stride = tile_edge * 4;
			canvas._buffer_extent = {tile_edge, tile_edge};
			canvas._origin = {tx, ty};
			canvas._clip = tile;
			canvas._opaque = true;
			draw_scene(canvas);

			for (auto y = tile.top; y < tile.bottom; ++y)
			{
				memcpy(tiled.data() + (static_cast<ptrdiff_t>(y) * extent.cx + tile.left) * 4,
				       tile_bits.data() + static_cast<ptrdiff_t>(y - ty) * tile_edge * 4 + (tile.left - tx) * 4,
				       static_cast<size_t>(tile.width()) * 4);
			}

			++result.tiles;
		}
	}

	for (size_t i = 0; i < reference.size(); i += 4)
	{
		if (memcmp(reference.data() + i, tiled.data() + i, 4) != 0) ++result.mismatched_pixels;

		const std::array untouched{fill, fill, fill, fill};
		if (memcmp(reference.data() + i, untouched.data(), 4) != 0) ++result.painted_pixels;
	}

	// Both extents exceed the tile, so the buffer is already at its final size and ensure_dib will
	// not reallocate across the grow - the case where the canvas last stopped following the window.
	{
		software_draw_context ctx(nullptr, nullptr, false, normal_font_size);
		constexpr sizei started{software_tile_extent + 88, software_tile_extent + 88};
		constexpr sizei grown{software_tile_extent * 2 + 376, software_tile_extent + 388};

		ctx.begin_draw(started, normal_font_size, {});
		ctx.begin_draw(grown, normal_font_size, {});

		const recti client(0, 0, grown.cx, grown.cy);
		result.grown_client_pixels = client.width() * client.height();
		result.grown_buffer_pixels = ctx._dib_size.cx * ctx._dib_size.cy;

		ctx.for_each_tile(client, [&](const recti tile)
		{
			ctx._canvas._clip = tile;
			const auto writable = ctx._canvas.clamp_to_clip(client);
			result.grown_writable_pixels += writable.width() * writable.height();
		});
	}

	return result;
}
