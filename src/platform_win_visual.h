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

#include <d3d11_1.h>
#include <dxgi1_3.h>
#include <wincodec.h>
#include <dwrite.h>
#include <directxmath.h>


constexpr int swap_buffer_count = 2;
constexpr auto back_buffer_format = DXGI_FORMAT_B8G8R8A8_UNORM; // DXGI_FORMAT_B8G8R8A8_UNORM;

// Back buffers are allocated in steps of this many pixels rather than at the exact client size, so
// a window drag or splitter move re-uses the existing buffers instead of reallocating them.
constexpr int back_buffer_quantum = 256;

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

	render_char_result render_glyph(uint16_t glyph_index, int spacing, const DWRITE_GLYPH_RUN* glyph_run) const;
	sizei measure(std::wstring_view text, ui::style::text_style style, int width, int height) const;
	void draw(ui::draw_context*, IDWriteTextRenderer*, std::wstring_view text, recti bounds,
	          ui::style::text_style style, ui::color color, ui::color bg,
	          const std::vector<ui::text_highlight_t>& highlights);
	static void draw(ui::draw_context*, IDWriteTextRenderer*, IDWriteTextLayout*, recti bounds, ui::color color,
	                 ui::color bg);
};

using font_renderer_ptr = std::shared_ptr<font_renderer>;

// A glyph cache must be keyed by the face a glyph index belongs to. Font fallback means one text
// renderer sees several faces, and an index is only meaningful inside its own face, so keying on
// the index alone - or on a proxy such as the face's glyph count - lets one face's glyph resolve
// to another face's raster. The face pointer alone is not a safe key either, because a released
// face can be reallocated at the same address, so each keyed face keeps a reference for as long
// as its id is in use.
//
// The em size is part of the key as well. A face carries no size - the size lives on the glyph
// run - so a renderer that ever sees two sizes (a text layout built before a font-size change,
// drawn after it) would otherwise serve the earlier size's raster for the later size's text and
// mix glyph sizes within a single string.
class glyph_face_keys
{
	std::unordered_map<IDWriteFontFace*, uint32_t> _ids;
	std::vector<ComPtr<IDWriteFontFace>> _faces;

public:
	uint64_t key(IDWriteFontFace* face, const float em_size, const uint16_t glyph_index)
	{
		uint32_t id = 0;

		if (face)
		{
			const auto found = _ids.find(face);

			if (found != _ids.cend())
			{
				id = found->second;
			}
			else
			{
				_faces.emplace_back(face);
				id = static_cast<uint32_t>(_faces.size());
				_ids[face] = id;
			}
		}

		const auto em = std::clamp(std::lround(em_size), 0l, 0xFFFFl);

		return static_cast<uint64_t>(id) << 32 | static_cast<uint64_t>(em) << 16 | glyph_index;
	}

	void clear()
	{
		_ids.clear();
		_faces.clear();
	}
};

class text_layout_impl final : public ui::text_layout
{
public:
	explicit text_layout_impl(const font_renderer_ptr& renderer, const ui::style::font_face font) : _renderer(renderer),
		_font(font)
	{
	}

	void update(std::string_view text, ui::style::text_style text_style) override;
	sizei measure_text(int cx, int cy) override;

	font_renderer_ptr _renderer;
	ComPtr<IDWriteTextLayout> _layout;
	ui::style::font_face _font = ui::style::font_face::dialog;

private:
	// GetMetrics re-shapes the whole string whenever SetMaxWidth or SetMaxHeight dirties the layout, and
	// a flex pass measures against one limit then lays out against another, so both results are kept.
	struct measured_extent
	{
		sizei limit;
		sizei extent;
		bool valid = false;
	};

	measured_extent _measured[2];
	size_t _measured_next = 0;
};

struct factories
{
	ComPtr<IDXGIFactory1> dxgi;
	ComPtr<IDWriteFactory> dwrite;
	ComPtr<IWICImagingFactory> wic;

	// True when Direct3D 11 hardware/WARP device creation failed and the app is running
	// with the CPU software rendering backend.
	bool software_mode = false;

	ComPtr<ID3D11Device> d3d_device;
	ComPtr<ID3D11DeviceContext> d3d_context;
	ComPtr<IDXGIDevice> dxgi_device;

	ComPtr<IDWriteFontCollection> font_collection;

	std::unordered_map<int, font_renderer_ptr> font_renderers;

	font_renderer_ptr create_font_face(const wchar_t* font_name, int font_height) const;
	font_renderer_ptr create_icon_font_face(int font_height);
	font_renderer_ptr font_face(ui::style::font_face font, int base_font_size);

	void reset();
	void reset_fonts();

	D3D_FEATURE_LEVEL d3d_feature_level = D3D_FEATURE_LEVEL_1_0_CORE;

	bool init(bool use_gpu);

	// Releases the Direct3D device and switches every draw context created from here on to
	// the CPU software backend. Used when the GPU device is lost at runtime.
	void downgrade_to_software();

	void register_fonts() const;
	void unregister_fonts() const;
	void destroy();
};


using factories_ptr = std::shared_ptr<factories>;
