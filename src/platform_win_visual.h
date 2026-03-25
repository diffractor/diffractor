// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Windows visual styles and theming. Handles DWM composition,
// visual style detection, and Windows appearance settings.

#pragma once

#include "platform_win.h"

#undef SelectBitmap

#include <dcomp.h>
#include <d3d11_1.h>
#include <wincodec.h>
#include <dwrite.h>
#include <directxmath.h>


constexpr int swap_buffer_count = 2;
constexpr auto back_buffer_format = DXGI_FORMAT_B8G8R8A8_UNORM; // DXGI_FORMAT_B8G8R8A8_UNORM;

struct render_char_result
{
	std::vector<uint8_t> pixels;
	int cx = 0;
	int cy = 0;
	int x = 0;
	int glyph = 0;

	bool is_empty() const
	{
		return cx < 1 && cy < 1;
	}
};

class font_renderer
{
public:
	ComPtr<IDWriteFactory> _factory;
	ComPtr<IDWriteFontFace> _face;
	ComPtr<IDWriteTextFormat> _text_format;

	int _font_size = 0;
	DWRITE_FONT_METRICS _metrics{};


	explicit font_renderer(const ComPtr<IDWriteFactory>& factory, const ComPtr<IDWriteFontFace>& face,
	                       ComPtr<IDWriteTextFormat> text_format, int font_size);

	uint32_t calc_line_height() const;
	uint32_t calc_base_line_height() const;
	calc_text_extent_result calc_glyph_extent(std::u32string_view code_points) const;

	render_char_result render_glyph(uint16_t glyph_index, int spacing, const DWRITE_GLYPH_RUN* glyph_run) const;
	sizei measure(std::wstring_view text, ui::style::text_style style, int width, int height) const;
	void draw(ui::draw_context*, ID2D1RenderTarget*, std::wstring_view text, recti bounds, ui::style::text_style style,
	          ui::color color, ui::color bg, const std::vector<ui::text_highlight_t>& highlights) const;
	static void draw(ui::draw_context*, ID2D1RenderTarget*, IDWriteTextLayout*, recti bounds, ui::color color,
	                 ui::color bg);
	void draw(ui::draw_context*, IDWriteTextRenderer*, std::wstring_view text, recti bounds,
	          ui::style::text_style style, ui::color color, ui::color bg,
	          const std::vector<ui::text_highlight_t>& highlights);
	static void draw(ui::draw_context*, IDWriteTextRenderer*, IDWriteTextLayout*, recti bounds, ui::color color,
	                 ui::color bg);
};

using font_renderer_ptr = std::shared_ptr<font_renderer>;

class text_layout_impl final : public ui::text_layout
{
public:
	explicit text_layout_impl(const font_renderer_ptr& renderer, const ui::style::font_face font) : _renderer(renderer),
		_font(font)
	{
	}

	void update(std::u8string_view text, ui::style::text_style text_style) override;
	sizei measure_text(int cx, int cy) override;

	font_renderer_ptr _renderer;
	ComPtr<IDWriteTextLayout> _layout;
	ui::style::font_face _font = ui::style::font_face::dialog;
};

struct factories
{
	ComPtr<IDXGIFactory1> dxgi;
	ComPtr<ID2D1Factory> d2d;
	ComPtr<IDWriteFactory> dwrite;
	ComPtr<IWICImagingFactory> wic;

	ComPtr<ID3D11Device> d3d_device;
	ComPtr<ID3D11DeviceContext> d3d_context;
	ComPtr<IDXGIDevice> dxgi_device;

	ComPtr<IDWriteFontCollection> font_collection;

	std::unordered_map<int, font_renderer_ptr> font_renderers;

	font_renderer_ptr create_font_face(const wchar_t* font_name, int font_height) const;
	font_renderer_ptr create_icon_font_face(int font_height);
	font_renderer_ptr create_petscii_font_face(int font_height);
	font_renderer_ptr font_face(ui::style::font_face font, int base_font_size);

	void reset();
	void reset_fonts();

	D3D_FEATURE_LEVEL d3d_feature_level = D3D_FEATURE_LEVEL_1_0_CORE;

	bool init(bool use_gpu);
	void register_fonts() const;
	void unregister_fonts() const;
	void destroy();
};


using factories_ptr = std::shared_ptr<factories>;
