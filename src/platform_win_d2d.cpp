// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Windows UI framework. Implements window management, message handling,
// input processing, clipboard, drag-drop, and system integration.

#include "pch.h"
#include "platform_win.h"
#include "platform_win_visual.h"

#include <dwmapi.h>
#include <versionhelpers.h>
#include <Shobjidl.h>

#ifndef WINSTORE
#include <DbgHelp.h>
#endif

#include "av_format.h"
#include "files.h"
#include "ui_elements.h"
#include "platform_win_res.h"


static ComPtr<ID2D1Bitmap> load_bitmap_resource(const factories_ptr& f, ID2D1RenderTarget* rt, const uint32_t res_id)
{
	ComPtr<IWICBitmapFrameDecode> pFrame;
	ComPtr<IWICFormatConverter> pConverter;
	ComPtr<ID2D1Bitmap> pBitmap;
	ComPtr<IWICBitmapDecoder> pDecoder;
	ComPtr<IWICStream> pStream;

	HGLOBAL imageResDataHandle = nullptr;
	void* pImageFile = nullptr;
	DWORD imageFileSize = 0;

	// Locate the resource.
	auto* imageResHandle = FindResourceA(get_resource_instance, MAKEINTRESOURCEA(res_id), "PNG");
	HRESULT hr = imageResHandle ? S_OK : E_FAIL;

	if (SUCCEEDED(hr))
	{
		// Load the resource.
		imageResDataHandle = LoadResource(get_resource_instance, imageResHandle);
		hr = imageResDataHandle ? S_OK : E_FAIL;
	}

	if (SUCCEEDED(hr))
	{
		// Lock it to get a system memory pointer.
		pImageFile = LockResource(imageResDataHandle);
		hr = pImageFile ? S_OK : E_FAIL;
	}
	if (SUCCEEDED(hr))
	{
		// Calculate the size.
		imageFileSize = SizeofResource(get_resource_instance, imageResHandle);
		hr = imageFileSize ? S_OK : E_FAIL;
	}

	if (SUCCEEDED(hr))
	{
		// Create a WIC stream to map onto the memory.
		hr = f->wic->CreateStream(&pStream);
	}

	if (SUCCEEDED(hr))
	{
		// Initialize the stream with the memory pointer and size.
		hr = pStream->InitializeFromMemory(
			std::bit_cast<BYTE*>(pImageFile),
			imageFileSize
		);
	}

	if (SUCCEEDED(hr))
	{
		// Create a decoder for the stream.
		hr = f->wic->CreateDecoderFromStream(
			pStream.Get(),
			nullptr,
			WICDecodeMetadataCacheOnLoad,
			&pDecoder
		);
	}

	if (SUCCEEDED(hr))
	{
		hr = pDecoder->GetFrame(0, &pFrame);
	}

	if (SUCCEEDED(hr))
	{
		// Convert the image format to 32bppPBGRA
		// (DXGI_FORMAT_B8G8R8A8_UNORM + D2D1_ALPHA_MODE_PREMULTIPLIED).
		hr = f->wic->CreateFormatConverter(&pConverter);
	}

	if (SUCCEEDED(hr))
	{
		hr = pConverter->Initialize(
			pFrame.Get(),
			GUID_WICPixelFormat32bppPBGRA,
			WICBitmapDitherTypeNone,
			nullptr,
			0.0f,
			WICBitmapPaletteTypeMedianCut
		);
	}

	if (SUCCEEDED(hr))
	{
		hr = rt->CreateBitmapFromWicBitmap(
			pConverter.Get(),
			nullptr,
			&pBitmap
		);
	}

	return pBitmap;
}


static void draw_bitmap(ID2D1RenderTarget* rt, ID2D1Bitmap* b, const float dx, const float dy, const float dcx,
                        const float dcy, const float sx, const float sy, const float scx, const float scy,
                        const float alpha)
{
	const D2D1_RECT_F dst = {dx, dy, dx + dcx, dy + dcy};
	const D2D1_RECT_F src = {sx, sy, sx + scx, sy + scy};

	rt->DrawBitmap(b, dst, alpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src);
}

static void streach_background(ID2D1RenderTarget* rt, ID2D1Bitmap* b, const recti r, const int dst_width_in,
                               const float alpha)
{
	const D2D1_SIZE_F extent = b->GetSize();
	const auto dst_width = static_cast<float>(dst_width_in);
	const auto double_dst_width = dst_width * 2.0f;

	const auto src_width = extent.width * 3.0f / 7.0f;
	const auto w = extent.width - src_width * 2.0f;
	const auto l = extent.height - src_width;
	const auto br = extent.width - src_width;

	const auto width = static_cast<float>(r.width());
	const auto height = static_cast<float>(r.height());
	const auto top = static_cast<float>(r.top);
	const auto left = static_cast<float>(r.left);
	const auto right = static_cast<float>(r.right);
	const auto bottom = static_cast<float>(r.bottom);

	draw_bitmap(rt, b, left + dst_width, top, width - double_dst_width, dst_width, src_width, 0, w, src_width, alpha);
	draw_bitmap(rt, b, left, top + dst_width, dst_width, height - double_dst_width, 0, src_width, src_width, w, alpha);
	draw_bitmap(rt, b, right - dst_width, top + dst_width, dst_width, height - double_dst_width, l, src_width,
	            src_width, w, alpha);
	draw_bitmap(rt, b, left + dst_width, bottom - dst_width, width - double_dst_width, dst_width, src_width, l, w,
	            src_width, alpha);
	draw_bitmap(rt, b, left, top, dst_width, dst_width, 0, 0, src_width, src_width, alpha);
	draw_bitmap(rt, b, right - dst_width, top, dst_width, dst_width, br, 0, src_width, src_width, alpha);
	draw_bitmap(rt, b, left, bottom - dst_width, dst_width, dst_width, 0, br, src_width, src_width, alpha);
	draw_bitmap(rt, b, right - dst_width, bottom - dst_width, dst_width, dst_width, br, br, src_width, src_width,
	            alpha);
}

class d2d_texture final : public ui::texture
{
public:
	ComPtr<ID2D1RenderTarget> _rt;
	ComPtr<ID2D1Bitmap> _bm;
	std::unique_ptr<av_scaler> _scaler;
	factories_ptr _f;

	d2d_texture(const factories_ptr& f, ID2D1RenderTarget* rt) : _rt(rt), _f(f)
	{
	}

	~d2d_texture() override
	{
		_scaler.reset();
		_bm.Reset();
		_scaler.reset();
	}

	bool is_valid() const override
	{
		return _bm != nullptr;
	}

	ui::texture_update_result update(const sizei dimensions, const ui::texture_format format,
	                                 const ui::orientation orientation, const uint8_t* pixels, const size_t stride,
	                                 const size_t buffer_size) override
	{
		auto result = ui::texture_update_result::failed;

		/*if (_bm && _dimensions == dimensions && _format == format)
		{
			D2D1_RECT_U bounds = { 0, 0, _dimensions.cx, _dimensions.cy };
			_bm->CopyFromMemory(&bounds, pixels, stride);
			result = ui::texture_update_result::tex_updated;
		}
		else*/
		{
			_bm.Reset();

			auto pixel_format = GUID_WICPixelFormat32bppBGR;

			switch (format)
			{
			case ui::texture_format::RGB:
				pixel_format = GUID_WICPixelFormat32bppBGR;
				break;
			case ui::texture_format::ARGB:
				pixel_format = GUID_WICPixelFormat32bppBGRA;
				break;
			case ui::texture_format::None:
				break;
			case ui::texture_format::NV12:
				break;
			case ui::texture_format::P010:
				break;
			default: ;
			}

			ComPtr<IWICBitmapSource> wic_source;
			ComPtr<IWICBitmap> wic_bitmap;

			auto hr = _f->wic->CreateBitmapFromMemory(dimensions.cx, dimensions.cy,
			                                          pixel_format,
			                                          static_cast<uint32_t>(stride),
			                                          static_cast<uint32_t>(buffer_size),
			                                          const_cast<uint8_t*>(pixels), &wic_bitmap);

			if (SUCCEEDED(hr))
			{
				if (format == ui::texture_format::ARGB)
				{
					ComPtr<IWICFormatConverter> wic_converter;

					if (SUCCEEDED(hr))
					{
						hr = _f->wic->CreateFormatConverter(&wic_converter);
					}

					if (SUCCEEDED(hr))
					{
						hr = wic_converter->Initialize(
							wic_bitmap.Get(), // Input bitmap to convert
							GUID_WICPixelFormat32bppPBGRA, // Destination pixel format
							WICBitmapDitherTypeNone, // Specified dither pattern
							nullptr, // Specify a particular palette 
							0.f, // Alpha threshold
							WICBitmapPaletteTypeCustom // Palette translation type
						);

						if (SUCCEEDED(hr))
						{
							wic_source = wic_converter;
						}
					}
				}
				else
				{
					wic_source = wic_bitmap;
				}
			}

			if (SUCCEEDED(hr))
			{
				hr = _rt->CreateBitmapFromWicBitmap(wic_source.Get(), nullptr, &_bm);

				if (SUCCEEDED(hr))
				{
					_dimensions = dimensions;
					_format = format;
					result = ui::texture_update_result::tex_created;
				}
			}
		}

		_orientation = orientation;

		return result;
	}

	ui::texture_update_result update(const av_frame_ptr& frame) override
	{
		if (!_scaler) _scaler = std::make_unique<av_scaler>();

		const auto surface = std::make_shared<ui::surface>();
		if (_scaler->scale_surface(frame, surface))
		{
			return update(surface);
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
};

static D2D1_RECT_F make_rectf(const int left, const int top, const int right, const int bottom)
{
	return {static_cast<float>(left), static_cast<float>(top), static_cast<float>(right), static_cast<float>(bottom)};
}

static D2D1_RECT_F make_rectf(const recti bounds)
{
	return make_rectf(bounds.left, bounds.top, bounds.right, bounds.bottom);
}

class d2d_draw_context;

using texture_gdi_ptr = std::shared_ptr<d2d_texture>;
using d2d_draw_context_ptr = std::shared_ptr<d2d_draw_context>;

class d2d_vertices final : public std::enable_shared_from_this<d2d_vertices>, public ui::vertices
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

class d2d_draw_context final : public draw_context_device, public std::enable_shared_from_this<d2d_draw_context>
{
public:
	ComPtr<ID2D1RenderTarget> _rt;
	ComPtr<ID2D1Bitmap> _shadow;
	ComPtr<ID2D1Bitmap> _inverse_shadow;
	recti _bounds;
	factories_ptr _f;
	int _base_font_size = normal_font_size;

	d2d_draw_context(const factories_ptr& f, ID2D1RenderTarget* rt, const int base_font_size) : _rt(rt), _f(f),
		_base_font_size(base_font_size)
	{
		df::assert_true(rt);
	}

	~d2d_draw_context() override = default;

	void clear(const ui::color c) override
	{
		_rt->Clear(D2D1::ColorF(c.r, c.g, c.b, c.a));
	}

	void draw_rounded_rect(const recti bounds, const ui::color c, const int radius) override
	{
		df::scope_rendering_func rf(__FUNCTION__);

		if (bounds.intersects(_bounds) && _rt)
		{
			ComPtr<ID2D1SolidColorBrush> brush;

			const auto hr = _rt->CreateSolidColorBrush(
				D2D1::ColorF(c.r, c.g, c.b, c.a),
				&brush
			);

			if (SUCCEEDED(hr))
			{
				D2D1_ROUNDED_RECT r;
				r.rect = make_rectf(bounds);
				r.radiusX = static_cast<float>(radius);
				r.radiusY = static_cast<float>(radius);
				_rt->FillRoundedRectangle(r, brush.Get());
			}
		}
	}

	void draw_rect(const recti bounds, const ui::color c) override
	{
		df::scope_rendering_func rf(__FUNCTION__);

		if (bounds.intersects(_bounds) && _rt)
		{
			ComPtr<ID2D1SolidColorBrush> brush;

			const auto hr = _rt->CreateSolidColorBrush(
				D2D1::ColorF(c.r, c.g, c.b, c.a),
				&brush
			);

			if (SUCCEEDED(hr))
			{
				_rt->FillRectangle(make_rectf(bounds), brush.Get());
			}
		}
	}

	void draw_text(const std::string_view text, const recti bounds, const ui::style::font_face font,
	               const ui::style::text_style style, const ui::color c, const ui::color bg) override
	{
		df::scope_rendering_func rf(__FUNCTION__);

		if (_bounds.intersects(bounds))
		{
			const auto fr = _f->font_face(font, _base_font_size);

			if (fr)
			{
				fr->draw(this, _rt.Get(), str::utf8_to_utf16(text), bounds, style, c, bg, {});
			}
		}
	}

	void draw_text(const std::string_view text, const std::vector<ui::text_highlight_t>& highlights,
	               const recti bounds, const ui::style::font_face font, const ui::style::text_style style,
	               const ui::color clr,
	               const ui::color bg) override
	{
		df::scope_rendering_func rf(__FUNCTION__);

		if (_bounds.intersects(bounds))
		{
			const auto w_highlights = highlights;

			std::wstring w;
			w.reserve(text.size());

			auto i = text.begin();
			while (i < text.end())
			{
				w += static_cast<wchar_t>(str::pop_utf8_char(i, text.end()));
			}

			const auto fr = _f->font_face(font, _base_font_size);

			if (fr)
			{
				fr->draw(this, _rt.Get(), w, bounds, style, clr, bg, w_highlights);
			}
		}
	}

	void draw_text(const ui::text_layout_ptr& tl, const recti bounds, const ui::color clr, const ui::color bg) override
	{
		df::scope_rendering_func rf(__FUNCTION__);
		df::assert_true(ui::is_ui_thread());

		if (_bounds.intersects(bounds))
		{
			const auto t = std::dynamic_pointer_cast<text_layout_impl>(tl);

			if (t)
			{
				const auto fr = _f->font_face(t->_font, _base_font_size);

				if (fr)
				{
					fr->draw(this, _rt.Get(), t->_layout.Get(), bounds, clr, bg);
				}
			}
		}
	}

	void draw_shadow(const recti bounds, const int width, const float alpha, const bool inverse) override
	{
		df::scope_rendering_func rf(__FUNCTION__);

		if (!_shadow)
		{
			_shadow = load_bitmap_resource(_f, _rt.Get(), IDB_SHADOW);
		}

		if (!_inverse_shadow)
		{
			_inverse_shadow = load_bitmap_resource(_f, _rt.Get(), IDB_INVERSE_SHADOW);
		}

		if (_shadow && _inverse_shadow)
		{
			streach_background(_rt.Get(), inverse ? _inverse_shadow.Get() : _shadow.Get(), bounds.inflate(32), 32,
			                   alpha);
		}
	}

	void draw_border(const recti inside, const recti outside, const ui::color c_inside,
	                 const ui::color c_outside) override
	{
		df::scope_rendering_func rf(__FUNCTION__);

		if (outside.intersects(_bounds) && _rt)
		{
			ComPtr<ID2D1SolidColorBrush> brush;

			const auto hr = _rt->CreateSolidColorBrush(
				D2D1::ColorF(c_outside.r, c_outside.g, c_outside.b, c_outside.a),
				&brush
			);

			if (SUCCEEDED(hr))
			{
				const auto r1 = make_rectf(outside.left, outside.top, inside.left, outside.bottom);
				const auto r2 = make_rectf(inside.right, outside.top, outside.right, outside.bottom);
				const auto r3 = make_rectf(inside.left, outside.top, inside.right, inside.top);
				const auto r4 = make_rectf(inside.left, inside.bottom, inside.right, outside.bottom);

				_rt->FillRectangle(r1, brush.Get());
				_rt->FillRectangle(r2, brush.Get());
				_rt->FillRectangle(r3, brush.Get());
				_rt->FillRectangle(r4, brush.Get());
			}
		}
	}

	void draw_texture(const ui::texture_ptr& t, const recti dst, const float alpha,
	                  const ui::texture_sampler sampler) override
	{
		df::scope_rendering_func rf(__FUNCTION__);
		draw_texture(t, dst, recti(pointi(0, 0), t->dimensions()), alpha, sampler);
	}

	void draw_texture_impl(const texture_gdi_ptr& t, const recti dst, const recti src, const float alpha,
	                       const ui::texture_sampler sampler, float radius) const
	{
		df::scope_rendering_func rf(__FUNCTION__);

		if (dst.intersects(_bounds) && _rt && t->_bm)
		{
			const auto dd = make_rectf(dst);
			const auto ss = make_rectf(src);
			const D2D1_BITMAP_INTERPOLATION_MODE mode = sampler == ui::texture_sampler::point
				                                            ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
				                                            : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR;

			_rt->DrawBitmap(t->_bm.Get(), &dd, alpha, mode, &ss);
		}
	}

	void draw_texture_impl(const texture_gdi_ptr& t, const quadd& qdst, const recti src, const float alpha,
	                       const ui::texture_sampler sampler) const
	{
		df::scope_rendering_func rf(__FUNCTION__);

		const auto dst = qdst.bounding_rect();

		if (dst.intersects(_bounds) && _rt && t->_bm)
		{
			const auto dd = make_rectf(dst.round());
			const auto ss = make_rectf(src);
			const D2D1_BITMAP_INTERPOLATION_MODE mode = sampler == ui::texture_sampler::point
				                                            ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
				                                            : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR;

			_rt->DrawBitmap(t->_bm.Get(), &dd, alpha, mode, &ss);
		}
	}

	void draw_texture(const ui::texture_ptr& t, const quadd& dst, const recti src, const float alpha,
	                  const ui::texture_sampler sampler) override
	{
		const auto tt = std::dynamic_pointer_cast<d2d_texture>(t);
		draw_texture_impl(tt, dst, src, alpha, sampler);
	}

	void draw_texture(const ui::texture_ptr& t, const recti dst, const recti src, const float alpha,
	                  const ui::texture_sampler sampler, const float radius) override
	{
		const auto tt = std::dynamic_pointer_cast<d2d_texture>(t);
		draw_texture_impl(tt, dst, src, alpha, sampler, radius);
	}

	sizei measure_text(const std::string_view text, const ui::style::font_face font,
	                   const ui::style::text_style style, const int width, const int height) override
	{
		df::scope_rendering_func rf(__FUNCTION__);
		const auto fr = _f->font_face(font, _base_font_size);

		if (fr)
		{
			return fr->measure(str::utf8_to_utf16(text), style, width, height);
		}

		return {};
	}

	int text_line_height(const ui::style::font_face font) override
	{
		df::scope_rendering_func rf(__FUNCTION__);
		const auto fr = _f->font_face(font, _base_font_size);

		if (fr)
		{
			return fr->calc_line_height();
		}

		return 0;
	}

	ui::texture_ptr create_texture() override
	{
		df::scope_rendering_func rf(__FUNCTION__);
		df::assert_true(ui::is_ui_thread());
		return std::make_shared<d2d_texture>(_f, _rt.Get());
	}

	ui::text_layout_ptr create_text_layout(const ui::style::font_face font) override
	{
		df::scope_rendering_func rf(__FUNCTION__);
		df::assert_true(ui::is_ui_thread());
		const auto fr = _f->font_face(font, _base_font_size);

		if (fr)
		{
			return std::make_shared<text_layout_impl>(fr, font);
		}

		return nullptr;
	}

	void draw_vertices(const ui::vertices_ptr& v) override
	{
		df::scope_rendering_func rf(__FUNCTION__);
		const auto vv = std::dynamic_pointer_cast<d2d_vertices>(v);

		for (auto i = 0u; i < vv->_rects.size(); i++)
		{
			const auto bounds = vv->_rects[i];
			const auto c2 = vv->_colors[i];
			const auto c1 = c2.emphasize();

			if (bounds.intersects(_bounds) && _rt)
			{
				ComPtr<ID2D1SolidColorBrush> brush;

				const auto hr = _rt->CreateSolidColorBrush(
					D2D1::ColorF(c1.r, c1.g, c1.b, c1.a),
					&brush
				);

				if (SUCCEEDED(hr))
				{
					const auto r = make_rectf(bounds);
					_rt->FillRectangle(r, brush.Get());
				}
			}
		}
	}

	ui::vertices_ptr create_vertices() override
	{
		df::scope_rendering_func rf(__FUNCTION__);
		return std::make_shared<d2d_vertices>();
	}

	recti clip_bounds() const override
	{
		return _bounds;
	}

	void clip_bounds(const recti r) override
	{
		df::scope_rendering_func rf(__FUNCTION__);

		if (_rt)
		{
			_rt->PushAxisAlignedClip(
				make_rectf(r),
				D2D1_ANTIALIAS_MODE_PER_PRIMITIVE
			);
		}
	}

	void restore_clip() override
	{
		df::scope_rendering_func rf(__FUNCTION__);

		if (_rt)
		{
			_rt->PopAxisAlignedClip();
		}
	}


	void draw_edge_shadows(const float alpha) override
	{
		df::scope_rendering_func rf(__FUNCTION__);

		if (!_inverse_shadow)
		{
			_inverse_shadow = load_bitmap_resource(_f, _rt.Get(), IDB_INVERSE_SHADOW);
		}

		if (_inverse_shadow)
		{
			streach_background(_rt.Get(), _inverse_shadow.Get(), _bounds, 32, alpha);
		}
	}

	bool is_valid() const override
	{
		return _rt != nullptr;
	}

	void begin_draw(const sizei client_extent, const int base_font_size) override
	{
		df::scope_rendering_func rf(__FUNCTION__);
		_bounds.set(0, 0, client_extent.cx, client_extent.cy);

		_rt->BeginDraw();
		_rt->SetTransform(D2D1::Matrix3x2F::Identity());
	}

	void render() override
	{
		df::scope_rendering_func rf(__FUNCTION__);
		_rt->EndDraw();
	}

	void resize(const sizei client_extent) override
	{
		_bounds.set(0, 0, client_extent.cx, client_extent.cy);
	}

	static ui::color32 hsv_to_rgb(const double h, const double s, const double v)
	{
		// From medit
		// shift the hue to the range [0, 360] before performing calculations
		const double hh = (360 + static_cast<int>(h) % 360) % 360 / 60.;
		const int i = static_cast<int>(std::floor(hh)); /* largest int <= h     */
		const double f = hh - i; /* fractional part of h */
		const double p = v * (1.0 - s);
		const double q = v * (1.0 - s * f);
		const double t = v * (1.0 - s * (1.0 - f));

		double r, g, b;

		switch (i)
		{
		case 0: r = v;
			g = t;
			b = p;
			break;
		case 1: r = q;
			g = v;
			b = p;
			break;
		case 2: r = p;
			g = v;
			b = t;
			break;
		case 3: r = p;
			g = q;
			b = v;
			break;
		case 4: r = t;
			g = p;
			b = v;
			break;
		case 5: r = v;
			g = p;
			b = q;
			break;
		}

		return ui::saturate_rgba(r, g, b, 1.0);
	}

	void destroy() override
	{
		df::scope_rendering_func rf(__FUNCTION__);
		_rt.Reset();
		_shadow.Reset();
		_inverse_shadow.Reset();
	}

	void update_font_size(const int base_font_size) override
	{
		df::scope_rendering_func rf(__FUNCTION__);
		_base_font_size = base_font_size;
	}
};


ComPtr<ID2D1PathGeometry> GenTriangleGeometry(ID2D1Factory* f, const D2D1_POINT_2F pt1, const D2D1_POINT_2F pt2,
                                              const D2D1_POINT_2F pt3)
{
	ComPtr<ID2D1GeometrySink> sink;
	ComPtr<ID2D1PathGeometry> pg;

	auto hr = f->CreatePathGeometry(&pg);

	if (SUCCEEDED(hr))
	{
		hr = pg->Open(&sink);
	}

	if (SUCCEEDED(hr))
	{
		sink->BeginFigure(pt1, D2D1_FIGURE_BEGIN_FILLED);
		sink->AddLine(pt2);
		sink->AddLine(pt3);
		sink->EndFigure(D2D1_FIGURE_END_CLOSED);

		hr = sink->Close();
	}

	return pg;
}


void draw_bubble_background(const factories_ptr& f, ID2D1RenderTarget* dc, const recti bounds,
                            const pointi focus_location, const float padding, const float shadow_xy,
                            const float radius)
{
	dc->Clear();

	const auto shadow = load_bitmap_resource(f, dc, IDB_SHADOW);

	if (shadow)
	{
		streach_background(dc, shadow.Get(), {0, 0, bounds.width(), bounds.height()}, 32, 1.0f);
	}

	const auto l = padding;
	const auto t = padding;
	const auto r = static_cast<float>(bounds.width()) - padding;
	const auto b = static_cast<float>(bounds.height()) - padding;

	ComPtr<ID2D1RoundedRectangleGeometry> rrg;
	ComPtr<ID2D1PathGeometry> tg;
	ComPtr<ID2D1PathGeometry> g;
	ComPtr<ID2D1GeometrySink> s;

	D2D1_ROUNDED_RECT rr;
	rr.rect = {l, t, r, b};
	rr.radiusX = radius;
	rr.radiusY = radius;

	auto hr = f->d2d->CreateRoundedRectangleGeometry(rr, &rrg);

	if (SUCCEEDED(hr))
	{
		if (focus_location.y <= bounds.top)
		{
			const float center = std::clamp(static_cast<float>(focus_location.x - bounds.left), l + 24.0f, r - 24.0f);
			tg = GenTriangleGeometry(f->d2d.Get(), {center - padding, t}, {center, t - padding}, {center + padding, t});
		}
		else if (focus_location.x >= bounds.right)
		{
			const float center = std::clamp(static_cast<float>(focus_location.y - bounds.top), t + 24.0f, b - 24.0f);
			tg = GenTriangleGeometry(f->d2d.Get(), {r, center - padding}, {r + padding, center}, {r, center + padding});
		}
		else if (focus_location.y >= bounds.bottom)
		{
			const float center = std::clamp(static_cast<float>(focus_location.x - bounds.left), l + 24.0f, r - 24.0f);
			tg = GenTriangleGeometry(f->d2d.Get(), {center + padding, b}, {center, b + padding}, {center - padding, b});
		}
		else //if (focus_location.x <= bounds.left)
		{
			const float center = std::clamp(static_cast<float>(focus_location.y - bounds.top), t + 24.0f, b - 24.0f);
			tg = GenTriangleGeometry(f->d2d.Get(), {l, center + padding}, {l - padding, center}, {l, center - padding});
		}

		if (SUCCEEDED(hr))
		{
			hr = f->d2d->CreatePathGeometry(&g);
		}

		if (SUCCEEDED(hr))
		{
			hr = g->Open(&s);
		}

		if (SUCCEEDED(hr))
		{
			hr = rrg->CombineWithGeometry(tg.Get(), D2D1_COMBINE_MODE_UNION, nullptr, 0.0f, s.Get());
		}

		if (SUCCEEDED(hr))
		{
			hr = s->Close();
		}
	}

	if (SUCCEEDED(hr))
	{
		ComPtr<ID2D1SolidColorBrush> brush;
		const ui::color color = ui::abgr(ui::style::color::bubble_background);
		hr = dc->CreateSolidColorBrush(D2D1::ColorF(color.r, color.g, color.b, color.a), &brush);

		if (SUCCEEDED(hr))
		{
			dc->FillGeometry(g.Get(), brush.Get());
		}
	}
}


draw_context_device_ptr create_d2d_draw_context(const factories_ptr& f, ID2D1RenderTarget* rt, const int base_font_size)
{
	return std::make_shared<
		d2d_draw_context>(f, rt, base_font_size);
}
