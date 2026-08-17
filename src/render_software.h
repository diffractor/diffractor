// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: The CPU rasterizer and the seam between it and whatever shows its pixels.
// software_canvas owns a BGRA buffer and a clip and knows how to fill, blend and sample into it;
// a present target owns only where that buffer comes from and where a finished tile goes. Windows
// presents through GDI, a port supplies its own, and a target that presents nothing is what makes
// a headless render possible.

#pragma once

#include "util_simd.h"

namespace ui
{
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Pixel helpers - the software canvas is a top-down 32-bit BGRA buffer (straight alpha).
	//////////////////////////////////////////////////////////////////////////////////////////////

	inline uint8_t to_byte(const float f) noexcept
	{
		const auto i = static_cast<int>(f * 255.0f + 0.5f);
		return static_cast<uint8_t>(i < 0 ? 0 : (i > 255 ? 255 : i));
	}

	// Porter-Duff "over" of a straight-alpha source colour onto a BGRA pixel. When opaque is set
	// the destination is treated as fully opaque (the case for a surface presented by copy, where
	// the alpha channel is ignored). Treating the destination as opaque is essential: it makes
	// anti-aliased glyph edges and semi-transparent panel overlays composite against the real
	// background instead of an empty (alpha 0) canvas.
	inline void blend_over(uint8_t* p, const ui::color& c, const float coverage, const bool opaque) noexcept
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
	inline void blend_over_bgra(uint8_t* p, const int sb, const int sg, const int sr, const float sa,
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
	inline ui::color lerp_color(const ui::color& a, const ui::color& b, const float t) noexcept
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
	inline void build_taps(const double s, const int limit, const int step, const int taps, int* offsets,
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
	inline __m128 load_bgra_ps(const uint8_t* p) noexcept
	{
		const auto zero = _mm_setzero_si128();
		const auto bytes = _mm_cvtsi32_si128(*std::bit_cast<const int32_t*>(p));
		return _mm_cvtepi32_ps(_mm_unpacklo_epi16(_mm_unpacklo_epi8(bytes, zero), zero));
	}
#endif

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
		// When true the canvas is treated as fully opaque (a surface presented by copy).
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
				const auto mid_right = std::clamp(static_cast<int>(std::floor(cx + span - 0.5f)) + 1, mid_left,
				                                  r.right);

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

		// Blend an 8-bit alpha coverage bitmap using colour clr. Takes the bitmap rather than a glyph
		// type: what produced the coverage is the text layer's business, and the text layer is the
		// half of this backend that is still per-platform.
		void blend_glyph(const int px, const int py, const uint8_t* coverage, const sizei extent,
		                 const ui::color clr) const
		{
			if (!coverage || extent.cx < 1 || extent.cy < 1) return;

			const recti glyph_bounds(px, py, px + extent.cx, py + extent.cy);
			const auto r = clamp_to_clip(glyph_bounds);
			if (r.is_empty()) return;

			for (auto y = r.top; y < r.bottom; ++y)
			{
				const auto* src = coverage + static_cast<ptrdiff_t>(y - py) * extent.cx + (r.left - px);
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
			// Opaque source into an opaque canvas: skip Porter-Duff and write the sample.
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
			// Opaque source into an opaque canvas: skip Porter-Duff and write the sample.
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
						const auto curve_index = std::clamp(
							static_cast<int>(luminance * ui::texture_transform::curve_len),
							0, ui::texture_transform::curve_len - 1);
						luminance = transform->curve[curve_index];
						if (!df::is_zero(transform->vibrance))
						{
							const auto chroma_x = -0.105f - chroma_u;
							const auto chroma_y = 0.227f - chroma_v;
							const auto distance = std::sqrt(chroma_x * chroma_x + chroma_y * chroma_y);
							const auto saturation = transform->saturation *
								(1.0f + transform->vibrance * distance * 4.0f);
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
	// The present seam - where the buffer comes from, and where a finished tile goes.
	//////////////////////////////////////////////////////////////////////////////////////////////

	// A top-down 32-bit BGRA view onto memory the target owns. Null bits means the frame has
	// nowhere to go, which is a dropped frame rather than an error.
	struct software_buffer
	{
		uint8_t* bits = nullptr;
		sizei extent;
		int stride = 0;
	};

	class software_present_target
	{
	public:
		virtual ~software_present_target() = default;

		// A shell-composited surface keeps real per-pixel alpha and is presented whole, so it
		// cannot be walked a tile at a time and its buffer has to be the size of the window.
		virtual bool is_layered() const = 0;

		// Idempotent for a size already held: the rasterizer asks every frame.
		virtual software_buffer acquire_buffer(sizei extent) = 0;
		virtual void release_buffer() = 0;

		// False drops the frame. The destination was unavailable, which is not the same as the
		// drawing being wrong, so nothing is rasterised and nothing is reported.
		virtual bool begin_present() = 0;
		virtual void present_tile(recti tile, pointi buffer_origin) = 0;
		virtual void end_present(int layer_alpha) = 0;
	};

	using software_present_target_ptr = std::shared_ptr<software_present_target>;

	// Keeps the pixels and shows them to nobody. This is what a render with no window targets, and
	// it is the whole of the seam a port has to satisfy before it has a window at all.
	software_present_target_ptr create_memory_present_target();

	//////////////////////////////////////////////////////////////////////////////////////////////
	// The rasterizer probe. Tiling is sound only while every primitive derives its colour, coverage
	// and source mapping from its own bounds and treats the clip purely as a write mask, so this
	// draws one representative scene whole and again tile by tile and compares the two. It is also
	// the backend's only content-independent artifact: `samples` is a fixed spread of pixels from the
	// whole-surface rendering, which is what a second platform's rasterizer has to agree with.
	//////////////////////////////////////////////////////////////////////////////////////////////

	constexpr int rasterizer_sample_count = 24;

	struct rasterizer_probe
	{
		int painted_pixels = 0; // pixels the scene changed from the initial fill
		int mismatched_pixels = 0; // pixels where the tiled result differs from the untiled one
		int tiles = 0; // tiles the scene was rasterised in
		// Evenly spaced pixels of the whole-surface rendering, packed BGRA. Sampled rather than
		// hashed because the rasterizer is float arithmetic that an optimiser may reassociate, so an
		// exact digest differs between two builds of one compiler and says nothing about portability.
		std::array<uint32_t, rasterizer_sample_count> samples{};
	};

	rasterizer_probe probe_software_rasterizer();
}
