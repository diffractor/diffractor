// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: CPU software rendering backend. Implements a draw_context_device that renders
// into a system-memory BGRA DIB and presents it via GDI (BitBlt / UpdateLayeredWindow).
// Used as the fallback when Direct3D 11 hardware acceleration is unavailable, and for
// dialogs and bubble popups. Text is rasterized via DirectWrite glyph alpha bitmaps and
// alpha-blended on the CPU.

#include "pch.h"
#include "platform_win.h"
#include "platform_win_visual.h"

#include "av_format.h"
#include "files.h"
#include "ui_elements.h"
#include "platform_win_res.h"

//////////////////////////////////////////////////////////////////////////////////////////////
// Pixel helpers - the software canvas is a top-down 32-bit BGRA buffer (straight alpha).
//////////////////////////////////////////////////////////////////////////////////////////////

namespace
{
	inline uint8_t to_byte(const float f) noexcept
	{
		const auto i = static_cast<int>(f * 255.0f + 0.5f);
		return static_cast<uint8_t>(i < 0 ? 0 : (i > 255 ? 255 : i));
	}

	// Porter-Duff "over" of a straight-alpha source colour onto a BGRA pixel. When opaque is set
	// the destination is treated as fully opaque (the case for non-layered windows presented with
	// BitBlt, where the alpha channel is ignored). Treating the destination as opaque is essential:
	// it makes anti-aliased glyph edges and semi-transparent panel overlays composite against the
	// real background instead of an empty (alpha 0) canvas.
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
}

//////////////////////////////////////////////////////////////////////////////////////////////
// software_canvas - owns the BGRA buffer + clip, provides primitive rasterisation.
//////////////////////////////////////////////////////////////////////////////////////////////

class software_canvas
{
public:
	uint8_t* _bits = nullptr;
	int _stride = 0;
	sizei _extent;
	recti _clip;
	// When true the canvas is treated as fully opaque (non-layered windows presented with BitBlt).
	// When false (layered bubble popups) real per-pixel alpha is preserved.
	bool _opaque = true;

	uint8_t* pixel(const int x, const int y) const noexcept
	{
		return _bits + static_cast<ptrdiff_t>(y) * _stride + static_cast<ptrdiff_t>(x) * 4;
	}

	recti clamp_to_clip(const recti r) const noexcept
	{
		return r.intersection(_clip).intersection(recti(0, 0, _extent.cx, _extent.cy));
	}

	void clear(const ui::color c) const
	{
		const auto b = to_byte(c.b);
		const auto g = to_byte(c.g);
		const auto r = to_byte(c.r);
		const auto a = _opaque ? uint8_t{255} : to_byte(c.a);

		for (auto y = 0; y < _extent.cy; ++y)
		{
			auto* p = pixel(0, y);

			for (auto x = 0; x < _extent.cx; ++x, p += 4)
			{
				p[0] = b;
				p[1] = g;
				p[2] = r;
				p[3] = a;
			}
		}
	}

	void fill_rect(const recti bounds, const ui::color c) const
	{
		const auto r = clamp_to_clip(bounds);
		if (r.is_empty()) return;

		for (auto y = r.top; y < r.bottom; ++y)
		{
			auto* p = pixel(r.left, y);

			for (auto x = r.left; x < r.right; ++x, p += 4)
			{
				blend_over(p, c, 1.0f, _opaque);
			}
		}
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

				if (x < cxl && y < cyt) { ccx = static_cast<float>(cxl); ccy = static_cast<float>(cyt); in_corner = true; }
				else if (x >= cxr && y < cyt) { ccx = static_cast<float>(cxr); ccy = static_cast<float>(cyt); in_corner = true; }
				else if (x < cxl && y >= cyb) { ccx = static_cast<float>(cxl); ccy = static_cast<float>(cyb); in_corner = true; }
				else if (x >= cxr && y >= cyb) { ccx = static_cast<float>(cxr); ccy = static_cast<float>(cyb); in_corner = true; }

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

			for (auto x = r.left; x < r.right; ++x, p += 4, ++src)
			{
				const auto a = *src;
				if (a) blend_over(p, clr, a / 255.0f, _opaque);
			}
		}
	}

	// Blit (with scaling) a BGRA source surface region into a destination rect.
	void blit_surface(const ui::surface& src, const recti src_rect, const recti dst_rect, const float alpha,
	                  const bool bilinear, const bool has_alpha) const
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

		for (auto y = r.top; y < r.bottom; ++y)
		{
			const auto sy = (y - dst_rect.top + 0.5) * scale_y + src_rect.top - 0.5;
			auto* p = pixel(r.left, y);

			for (auto x = r.left; x < r.right; ++x, p += 4)
			{
				const auto sx = (x - dst_rect.left + 0.5) * scale_x + src_rect.left - 0.5;

				int sb;
				int sg;
				int sr;
				int sample_a;

				if (bilinear)
				{
					const auto x0 = static_cast<int>(std::floor(sx));
					const auto y0 = static_cast<int>(std::floor(sy));
					const auto fx = static_cast<float>(sx - x0);
					const auto fy = static_cast<float>(sy - y0);

					const auto x0c = std::clamp(x0, 0, src_w - 1);
					const auto x1c = std::clamp(x0 + 1, 0, src_w - 1);
					const auto y0c = std::clamp(y0, 0, src_h - 1);
					const auto y1c = std::clamp(y0 + 1, 0, src_h - 1);

					const auto* p00 = src_pixels + y0c * src_stride + x0c * 4;
					const auto* p10 = src_pixels + y0c * src_stride + x1c * 4;
					const auto* p01 = src_pixels + y1c * src_stride + x0c * 4;
					const auto* p11 = src_pixels + y1c * src_stride + x1c * 4;

					const auto w00 = (1 - fx) * (1 - fy);
					const auto w10 = fx * (1 - fy);
					const auto w01 = (1 - fx) * fy;
					const auto w11 = fx * fy;

					sb = static_cast<int>(p00[0] * w00 + p10[0] * w10 + p01[0] * w01 + p11[0] * w11);
					sg = static_cast<int>(p00[1] * w00 + p10[1] * w10 + p01[1] * w01 + p11[1] * w11);
					sr = static_cast<int>(p00[2] * w00 + p10[2] * w10 + p01[2] * w01 + p11[2] * w11);
					sample_a = static_cast<int>(p00[3] * w00 + p10[3] * w10 + p01[3] * w01 + p11[3] * w11);
				}
				else
				{
					const auto x0c = std::clamp(static_cast<int>(std::floor(sx + 0.5)), 0, src_w - 1);
					const auto y0c = std::clamp(static_cast<int>(std::floor(sy + 0.5)), 0, src_h - 1);
					const auto* sp = src_pixels + y0c * src_stride + x0c * 4;
					sb = sp[0];
					sg = sp[1];
					sr = sp[2];
					sample_a = sp[3];
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
				for (auto x = r.left; x < r.right; ++x, p += 4, srow += 4)
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
	               const bool bilinear, const bool has_alpha) const
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

				const auto u = (dx * ey.Y - dy * ey.X) * inv_det;
				const auto v = (ex.X * dy - ex.Y * dx) * inv_det;

				if (u < 0.0 || u > 1.0 || v < 0.0 || v > 1.0) continue;

				const auto sx = src_rect.left + u * src_fw - 0.5;
				const auto sy = src_rect.top + v * src_fh - 0.5;

				int sb;
				int sg;
				int sr;
				int sample_a;

				if (bilinear)
				{
					const auto x0 = static_cast<int>(std::floor(sx));
					const auto y0 = static_cast<int>(std::floor(sy));
					const auto fx = static_cast<float>(sx - x0);
					const auto fy = static_cast<float>(sy - y0);

					const auto x0c = std::clamp(x0, 0, src_w - 1);
					const auto x1c = std::clamp(x0 + 1, 0, src_w - 1);
					const auto y0c = std::clamp(y0, 0, src_h - 1);
					const auto y1c = std::clamp(y0 + 1, 0, src_h - 1);

					const auto* p00 = src_pixels + y0c * src_stride + x0c * 4;
					const auto* p10 = src_pixels + y0c * src_stride + x1c * 4;
					const auto* p01 = src_pixels + y1c * src_stride + x0c * 4;
					const auto* p11 = src_pixels + y1c * src_stride + x1c * 4;

					const auto w00 = (1 - fx) * (1 - fy);
					const auto w10 = fx * (1 - fy);
					const auto w01 = (1 - fx) * fy;
					const auto w11 = fx * fy;

					sb = static_cast<int>(p00[0] * w00 + p10[0] * w10 + p01[0] * w01 + p11[0] * w11);
					sg = static_cast<int>(p00[1] * w00 + p10[1] * w10 + p01[1] * w01 + p11[1] * w11);
					sr = static_cast<int>(p00[2] * w00 + p10[2] * w10 + p01[2] * w01 + p11[2] * w11);
					sample_a = static_cast<int>(p00[3] * w00 + p10[3] * w10 + p01[3] * w01 + p11[3] * w11);
				}
				else
				{
					const auto x0c = std::clamp(static_cast<int>(std::floor(sx + 0.5)), 0, src_w - 1);
					const auto y0c = std::clamp(static_cast<int>(std::floor(sy + 0.5)), 0, src_h - 1);
					const auto* sp = src_pixels + y0c * src_stride + x0c * 4;
					sb = sp[0];
					sg = sp[1];
					sr = sp[2];
					sample_a = sp[3];
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
	if (SUCCEEDED(hr)) hr = f->wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
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

	software_texture() = default;

	~software_texture() override
	{
		_scaler.reset();
	}

	bool is_valid() const override
	{
		return ui::is_valid(_surface);
	}

	ui::texture_update_result update(const sizei dimensions, const ui::texture_format format,
	                                 const ui::orientation orientation, const uint8_t* pixels, const size_t stride,
	                                 const size_t buffer_size) override
	{
		if (dimensions.cx < 1 || dimensions.cy < 1 || !pixels)
		{
			return ui::texture_update_result::failed;
		}

		_display_valid = false; // source changed - drop the display-scaled cache

		const auto result = (_surface->empty() || _surface->dimensions() != dimensions)
			                    ? ui::texture_update_result::tex_created
			                    : ui::texture_update_result::tex_updated;

		_surface->alloc(dimensions, format, orientation);

		const auto copy_stride = std::min(static_cast<size_t>(_surface->stride()), stride);
		const auto rows = std::min(static_cast<size_t>(dimensions.cy), buffer_size ? buffer_size / stride : dimensions.cy);

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

		if (_scaler->scale_surface(frame, _surface))
		{
			_dimensions = _surface->dimensions();
			_format = _surface->format();
			_orientation = _surface->orientation();
			return ui::texture_update_result::tex_updated;
		}

		return ui::texture_update_result::failed;
	}

	ui::texture_update_result update(const ui::const_surface_ptr& s) override
	{
		df::assert_true(ui::is_ui_thread());

		if (ui::is_valid(s))
		{
			return update(s->dimensions(), s->format(), s->orientation(), s->pixels(), s->stride(), s->size());
		}

		return ui::texture_update_result::failed;
	}

	// Return the source surface scaled to dst (BGRA), cached until the source frame changes. Falls
	// back to the unscaled source if the format is unsupported by swscale.
	const ui::surface_ptr& display_scaled(const sizei dst)
	{
		if (_surface->dimensions() == dst) return _surface;

		if (_display_valid && _display_dims == dst) return _display_surface;

		if (!_display_scaler) _display_scaler = std::make_unique<av_scaler>();

		if (_display_scaler->scale_surface(_surface, _display_surface, dst) && ui::is_valid(_display_surface))
		{
			_display_dims = dst;
			_display_time = _surface->time();
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
	sizei _dib_size;

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

		_canvas._bits = _bits;
		_canvas._stride = sz.cx * 4;
		_canvas._extent = sz;
		_canvas._clip = recti(0, 0, sz.cx, sz.cy);
		_canvas._opaque = !_layered;
	}

	software_text_renderer* renderer_for(ui::style::font_face font);

	// draw_context_device -------------------------------------------------------------------

	bool is_valid() const override
	{
		return _bits != nullptr;
	}

	void begin_draw(const sizei client_extent, const int base_font_size) override
	{
		_base_font_size = base_font_size;
		ensure_dib(client_extent);
		_clip_stack.clear();
		_canvas._clip = recti(0, 0, _dib_size.cx, _dib_size.cy);
		_scene.clear();
	}

	// Replay the recorded command list into the DIB. Called on every present so that redraw()
	// (which re-presents without re-running the host paint logic) reflects textures whose contents
	// changed in place - e.g. a new video frame decoded into the same software_texture surface.
	void replay_scene()
	{
		_clip_stack.clear();
		_canvas._clip = recti(0, 0, _dib_size.cx, _dib_size.cy);

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

	void render() override
	{
		if (!_bits || !_hwnd) return;

		replay_scene();

		if (_layered)
		{
			present_layered();
		}
		else
		{
			const auto dc = GetDC(_hwnd);

			if (dc)
			{
				BitBlt(dc, 0, 0, _dib_size.cx, _dib_size.cy, _mem_dc, 0, 0, SRCCOPY);
				ReleaseDC(_hwnd, dc);
			}
		}
	}

	void present_layered() const
	{
		// UpdateLayeredWindow requires premultiplied alpha.
		auto premultiplied = std::make_unique<uint8_t[]>(static_cast<size_t>(_dib_size.cx) * _dib_size.cy * 4);
		const auto* src = _bits;
		auto* dst = premultiplied.get();

		for (auto i = 0; i < _dib_size.cx * _dib_size.cy; ++i, src += 4, dst += 4)
		{
			const auto a = src[3];
			dst[0] = static_cast<uint8_t>(src[0] * a / 255);
			dst[1] = static_cast<uint8_t>(src[1] * a / 255);
			dst[2] = static_cast<uint8_t>(src[2] * a / 255);
			dst[3] = a;
		}

		BITMAPINFO bmi = {};
		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = _dib_size.cx;
		bmi.bmiHeader.biHeight = -_dib_size.cy;
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;

		const auto screen_dc = GetDC(nullptr);
		const auto tmp_dc = CreateCompatibleDC(screen_dc);

		if (tmp_dc)
		{
			void* tmp_bits = nullptr;
			const auto tmp_dib = CreateDIBSection(tmp_dc, &bmi, DIB_RGB_COLORS, &tmp_bits, nullptr, 0);

			if (tmp_dib && tmp_bits)
			{
				memcpy(tmp_bits, premultiplied.get(), static_cast<size_t>(_dib_size.cx) * _dib_size.cy * 4);
				const auto old = SelectObject(tmp_dc, tmp_dib);

				RECT wr;
				GetWindowRect(_hwnd, &wr);

				POINT pt_dst = {wr.left, wr.top};
				POINT pt_src = {0, 0};
				SIZE size = {_dib_size.cx, _dib_size.cy};
				BLENDFUNCTION bf = {AC_SRC_OVER, 0, static_cast<BYTE>(_layer_alpha), AC_SRC_ALPHA};

				UpdateLayeredWindow(_hwnd, screen_dc, &pt_dst, &size, tmp_dc, &pt_src, 0, &bf, ULW_ALPHA);

				SelectObject(tmp_dc, old);
				DeleteObject(tmp_dib);
			}

			DeleteDC(tmp_dc);
		}

		ReleaseDC(nullptr, screen_dc);
	}

	void resize(const sizei client_extent) override
	{
		ensure_dib(client_extent);
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
		record_or_run([this, c] { _canvas.clear(c); });
	}

	void draw_rect(const recti bounds, const ui::color c) override
	{
		record_or_run([this, bounds, c] { _canvas.fill_rect(bounds, c); });
	}

	void draw_rounded_rect(const recti bounds, const ui::color c, const int radius) override
	{
		record_or_run([this, bounds, c, radius] { _canvas.fill_rounded_rect(bounds, c, radius); });
	}

	void draw_border(const recti inside, const recti outside, const ui::color c_inside,
	                 const ui::color c_outside) override
	{
		record_or_run([this, inside, outside, c_outside]
		{
			_canvas.fill_rect(recti(outside.left, outside.top, inside.left, outside.bottom), c_outside);
			_canvas.fill_rect(recti(inside.right, outside.top, outside.right, outside.bottom), c_outside);
			_canvas.fill_rect(recti(inside.left, outside.top, inside.right, inside.top), c_outside);
			_canvas.fill_rect(recti(inside.left, inside.bottom, inside.right, outside.bottom), c_outside);
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
			for (size_t i = 0; i < vv->_rects.size(); ++i)
			{
				_canvas.fill_rect(vv->_rects[i], vv->_colors[i].emphasize());
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

	void draw_texture_impl(const software_texture_ptr& t, const recti dst, const recti src, const float alpha,
	                       const ui::texture_sampler sampler) const
	{
		if (!t || !ui::is_valid(t->_surface)) return;

		const auto has_alpha = t->_surface->format() == ui::texture_format::ARGB;
		const auto src_dims = t->_surface->dimensions();
		const bool full_src = src.left <= 0 && src.top <= 0 && src.right >= src_dims.cx && src.bottom >= src_dims.cy;
		const bool scaling = dst.width() != src.width() || dst.height() != src.height();

		// Fast path: an opaque image/video scaled from its full source. Scale once with FFmpeg's SIMD
		// swscale to a display-sized surface, then a 1:1 copy - far faster than the scalar per-pixel
		// bilinear resample below. This is the full-screen video hot path.
		if (!has_alpha && full_src && scaling && alpha >= 0.999f)
		{
			const auto& scaled = t->display_scaled(dst.extent());

			if (ui::is_valid(scaled) && scaled->dimensions() == dst.extent())
			{
				_canvas.blit_copy(*scaled, dst, alpha, false);
				return;
			}
		}

		const auto bilinear = sampler != ui::texture_sampler::point;
		_canvas.blit_surface(*t->_surface, src, dst, alpha, bilinear, has_alpha);
	}

	void draw_texture(const ui::texture_ptr& t, const recti dst, const float alpha,
	                  const ui::texture_sampler sampler) override
	{
		if (!t) return;
		auto tt = std::dynamic_pointer_cast<software_texture>(t);
		const recti src(pointi(0, 0), t->dimensions());
		record_or_run([this, tt = std::move(tt), dst, src, alpha, sampler]
		{
			draw_texture_impl(tt, dst, src, alpha, sampler);
		});
	}

	void draw_texture(const ui::texture_ptr& t, const recti dst, const recti src, const float alpha,
	                  const ui::texture_sampler sampler, const float radius) override
	{
		auto tt = std::dynamic_pointer_cast<software_texture>(t);
		record_or_run([this, tt = std::move(tt), dst, src, alpha, sampler]
		{
			draw_texture_impl(tt, dst, src, alpha, sampler);
		});
	}

	void draw_texture(const ui::texture_ptr& t, const quadd& dst, const recti src, const float alpha,
	                  const ui::texture_sampler sampler) override
	{
		auto tt = std::dynamic_pointer_cast<software_texture>(t);
		record_or_run([this, tt = std::move(tt), dst, src, alpha, sampler]
		{
			if (!tt || !ui::is_valid(tt->_surface)) return;

			// A non-rotated / non-flipped quad is just a rectangle: take the faster rect path (which
			// includes the swscale fast path for full-screen video).
			recti rect;
			if (quad_upright_rect(dst, rect))
			{
				draw_texture_impl(tt, rect, src, alpha, sampler);
				return;
			}

			const auto bilinear = sampler != ui::texture_sampler::point;
			const auto has_alpha = tt->_surface->format() == ui::texture_format::ARGB;
			_canvas.blit_quad(*tt->_surface, src, dst, alpha, bilinear, has_alpha);
		});
	}

	// shadows -------------------------------------------------------------------------------

	void stretch_shadow(const ui::surface& s, const recti r, const int dst_width, const float alpha) const
	{
		const auto ext_w = static_cast<float>(s.width());
		const auto ext_h = static_cast<float>(s.height());

		const auto src_w = ext_w * 3.0f / 7.0f;
		const auto w = ext_w - src_w * 2.0f;
		const auto l = ext_h - src_w;
		const auto br = ext_w - src_w;

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
			_canvas.blit_surface(s, to_src(sx, sy, scx, scy), dst_r, alpha, true, true);
		};

		draw(left + dw, top, width - dw * 2.0f, dw, src_w, 0, w, src_w); // top
		draw(left, top + dw, dw, height - dw * 2.0f, 0, src_w, src_w, w); // left
		draw(right - dw, top + dw, dw, height - dw * 2.0f, l, src_w, src_w, w); // right
		draw(left + dw, bottom - dw, width - dw * 2.0f, dw, src_w, l, w, src_w); // bottom
		draw(left, top, dw, dw, 0, 0, src_w, src_w); // tl
		draw(right - dw, top, dw, dw, br, 0, src_w, src_w); // tr
		draw(left, bottom - dw, dw, dw, 0, br, src_w, src_w); // bl
		draw(right - dw, bottom - dw, dw, dw, br, br, src_w, src_w); // br
	}

	void do_draw_shadow(const recti bounds, const float alpha, const bool inverse)
	{
		if (!_shadow) _shadow = decode_png_resource_to_surface(_f, IDB_SHADOW);
		if (!_inverse_shadow) _inverse_shadow = decode_png_resource_to_surface(_f, IDB_INVERSE_SHADOW);

		const auto& s = inverse ? _inverse_shadow : _shadow;
		if (ui::is_valid(s)) stretch_shadow(*s, bounds.inflate(32), 32, alpha);
	}

	void draw_shadow(const recti bounds, const int width, const float alpha, const bool inverse) override
	{
		record_or_run([this, bounds, alpha, inverse] { do_draw_shadow(bounds, alpha, inverse); });
	}

	void do_edge_shadows(const float alpha)
	{
		if (!_inverse_shadow) _inverse_shadow = decode_png_resource_to_surface(_f, IDB_INVERSE_SHADOW);
		if (ui::is_valid(_inverse_shadow))
			stretch_shadow(*_inverse_shadow, recti(0, 0, _dib_size.cx, _dib_size.cy), 32, alpha);
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
		if (fr) return fr->measure(str::utf8_to_utf16(text), style, width, height);
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

	df::hash_map<uint32_t, render_char_result> _glyph_cache;

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
		_font->draw(_ctx, this, str::utf8_to_utf16(text), bounds, style, c, bg, {});
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
		// Key by font face (via its glyph count) as well as glyph index so a fallback
		// glyph (e.g. Hangul from Malgun Gothic) does not collide in the cache with a
		// same-indexed glyph of the primary face and get drawn as the wrong character.
		const uint32_t key = static_cast<uint32_t>(glyph_run->fontFace->GetGlyphCount()) << 16 | index;
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
	ULONG STDMETHODCALLTYPE Release() override { return static_cast<ULONG>(--_ref_count); }

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

	HRESULT STDMETHODCALLTYPE DrawInlineObject(void*, FLOAT, FLOAT, IDWriteInlineObject*, BOOL, BOOL, IUnknown*) override
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

	auto tr = std::make_shared<software_text_renderer>(this, fr);
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
