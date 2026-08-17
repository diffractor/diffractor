// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: CPU software rendering backend. Implements a draw_context_device that rasterises a
// window-sized scene through one fixed 512-square system-memory BGRA buffer, walked across the
// damaged region a tile at a time, so the buffer does not track the window size. Where that buffer
// comes from and where a finished tile goes is the ui::software_present_target seam in
// render_software.h; the GDI target below is the Windows answer (BitBlt / UpdateLayeredWindow).
// Used as the fallback when Direct3D 11 hardware acceleration is unavailable, and for dialogs and
// bubble popups. Text is rasterized via DirectWrite glyph alpha bitmaps and alpha-blended on the CPU.

#include "pch.h"
#include "platform_win.h"
#include "platform_win_visual.h"

#include "av_format.h"
#include "render_software.h"
#include "ui_elements.h"
#include "platform_win_res.h"
#include "util_simd.h"


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
					px[0] = ui::to_byte(mat.m[8] * yy + mat.m[9] * u + mat.m[10] * v + mat.m[11]);
					px[1] = ui::to_byte(mat.m[4] * yy + mat.m[5] * u + mat.m[6] * v + mat.m[7]);
					px[2] = ui::to_byte(mat.m[0] * yy + mat.m[1] * u + mat.m[2] * v + mat.m[3]);
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
					px[0] = ui::to_byte(mat.m[8] * yy + mat.m[9] * u + mat.m[10] * v + mat.m[11]);
					px[1] = ui::to_byte(mat.m[4] * yy + mat.m[5] * u + mat.m[6] * v + mat.m[7]);
					px[2] = ui::to_byte(mat.m[0] * yy + mat.m[1] * u + mat.m[2] * v + mat.m[3]);
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

//////////////////////////////////////////////////////////////////////////////////////////////
// gdi_present_target - the whole of what the software backend needs Windows for. A DIB section
// is a plain BGRA buffer that GDI is also willing to blit from, which is why the rasterizer can
// write into it directly and the present costs no copy.
//////////////////////////////////////////////////////////////////////////////////////////////

class gdi_present_target final : public ui::software_present_target
{
	HWND _hwnd = nullptr;
	bool _layered = false;

	HDC _mem_dc = nullptr;
	HBITMAP _dib = nullptr;
	HGDIOBJ _old_bitmap = nullptr;
	uint8_t* _bits = nullptr;
	sizei _extent;

	// The device context held for the duration of one present, so a tiled frame takes one
	// GetDC rather than one per tile.
	HDC _present_dc = nullptr;

	// UpdateLayeredWindow needs premultiplied alpha in its own DIB. It is kept across presents so a
	// layered window does not allocate a buffer, a DC and a DIB section on every frame.
	HDC _layered_dc = nullptr;
	HBITMAP _layered_dib = nullptr;
	HGDIOBJ _layered_old_bitmap = nullptr;
	uint8_t* _layered_bits = nullptr;
	sizei _layered_size;

public:
	gdi_present_target(const HWND hwnd, const bool layered) : _hwnd(hwnd), _layered(layered)
	{
	}

	~gdi_present_target() override
	{
		release_buffer();
	}

	bool is_layered() const override
	{
		return _layered;
	}

	ui::software_buffer acquire_buffer(const sizei extent) override
	{
		if (_dib && _extent == extent && _bits)
		{
			return {_bits, _extent, _extent.cx * 4};
		}

		release_buffer();

		if (extent.cx < 1 || extent.cy < 1)
		{
			return {};
		}

		BITMAPINFO bmi = {};
		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = extent.cx;
		bmi.bmiHeader.biHeight = -extent.cy; // top-down
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
				_extent = extent;
			}
			else
			{
				release_buffer();
			}
		}

		return {_bits, _extent, _extent.cx * 4};
	}

	void release_buffer() override
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
		_extent = {};

		free_layered_dib();
	}

	bool begin_present() override
	{
		if (!_bits || !_hwnd) return false;

		// A layered window is presented whole by UpdateLayeredWindow against the screen, so it
		// needs no device context of its own for the tile loop.
		if (_layered) return true;

		_present_dc = GetDC(_hwnd);
		return _present_dc != nullptr;
	}

	void present_tile(const recti tile, const pointi buffer_origin) override
	{
		if (!_present_dc) return;

		BitBlt(_present_dc, tile.left, tile.top, tile.width(), tile.height(),
		       _mem_dc, tile.left - buffer_origin.x, tile.top - buffer_origin.y, SRCCOPY);
	}

	void end_present(const int layer_alpha) override
	{
		if (_present_dc)
		{
			ReleaseDC(_hwnd, _present_dc);
			_present_dc = nullptr;
			return;
		}

		if (_layered) present_layered(layer_alpha);
	}

private:
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

	void present_layered(const int layer_alpha)
	{
		if (!_bits || _extent.cx < 1 || _extent.cy < 1)
		{
			return;
		}

		if (!_layered_bits || _layered_size != _extent)
		{
			free_layered_dib();

			BITMAPINFO bmi = {};
			bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			bmi.bmiHeader.biWidth = _extent.cx;
			bmi.bmiHeader.biHeight = -_extent.cy;
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
			_layered_size = _extent;
		}

		// UpdateLayeredWindow requires premultiplied alpha.
		const auto* src = _bits;
		auto* dst = _layered_bits;

		for (auto i = 0; i < _extent.cx * _extent.cy; ++i, src += 4, dst += 4)
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
		SIZE size = {_extent.cx, _extent.cy};
		BLENDFUNCTION bf = {AC_SRC_OVER, 0, static_cast<BYTE>(layer_alpha), AC_SRC_ALPHA};

		UpdateLayeredWindow(_hwnd, screen_dc, &pt_dst, &size, _layered_dc, &pt_src, 0, &bf, ULW_ALPHA);

		ReleaseDC(nullptr, screen_dc);
	}
};

class software_draw_context final : public draw_context_device,
                                    public std::enable_shared_from_this<software_draw_context>
{
public:
	factories_ptr _f;
	ui::software_present_target_ptr _target;
	bool _layered = false;
	int _base_font_size = normal_font_size;
	int _layer_alpha = 255;

	ui::software_canvas _canvas;
	std::vector<recti> _clip_stack;

	// Recorded command list (retained mode). Each draw call appends a closure; render() replays
	// them into the buffer. This lets redraw() re-present a frame - re-sampling textures updated in
	// place (e.g. video) - without re-running the host's paint logic, matching the Direct3D
	// backend's behaviour so the software renderer is a drop-in replacement.
	std::vector<std::function<void()>> _scene;
	// True while replay_scene() is running. Some draw paths re-enter the context during replay
	// (e.g. font_renderer::draw calls draw_rounded_rect to paint a text background); when replaying
	// those calls must rasterise immediately rather than append to _scene, which is being iterated.
	bool _replaying = false;

	// The scratch tile, not the window: one tile edge of software_tile_extent unless the client is
	// smaller, or the whole client for a layered surface (which cannot be tiled - see render).
	ui::software_buffer _buffer;
	// Window-space position of the tile currently being rasterised.
	pointi _buffer_origin;
	// The window client size, which is the space the recorded scene is laid out in.
	sizei _client_extent;

	// Region this frame is allowed to touch, and whether the scene's own opening clear already
	// covers it. Both are set by begin_draw and consumed by replay_scene / render.
	recti _damage;
	bool _scene_covers_damage = false;

	ui::surface_ptr _shadow;
	ui::surface_ptr _inverse_shadow;

	std::map<ui::style::font_face, std::shared_ptr<software_text_renderer>> _text_renderers;

	software_draw_context(const factories_ptr& f, ui::software_present_target_ptr target,
	                      const int base_font_size)
		: _f(f), _target(std::move(target)), _base_font_size(base_font_size)
	{
		_layered = _target && _target->is_layered();
	}

	~software_draw_context() override
	{
		free_buffer();
	}

	void free_buffer()
	{
		if (_target) _target->release_buffer();
		_buffer = {};
		_buffer_origin = {};
		sync_canvas();
	}

	recti tile_bounds() const noexcept
	{
		return {
			_buffer_origin.x, _buffer_origin.y, _buffer_origin.x + _buffer.extent.cx,
			_buffer_origin.y + _buffer.extent.cy
		};
	}

	// The canvas is a view onto the target's buffer, so it must be re-pointed wherever that buffer
	// is acquired or released. Doing it in one place keeps a failed or zero-size allocation from
	// leaving the canvas describing a non-empty surface backed by freed (or null) pixels.
	void sync_canvas()
	{
		_canvas._bits = _buffer.bits;
		_canvas._stride = _buffer.stride;
		_canvas._buffer_extent = _buffer.extent;
		_canvas._origin = _buffer_origin;
		_canvas._clip = tile_bounds();
		_canvas._opaque = !_layered;
	}

	void set_tile_origin(const pointi origin)
	{
		_buffer_origin = origin;
		_canvas._origin = origin;
	}

	sizei buffer_extent_for(const sizei client) const
	{
		// A layered surface is presented whole, so it cannot be tiled and keeps a full-client buffer.
		if (_layered) return client;

		// Capped at the tile so the allocation never tracks the window, floored at what is already
		// allocated so a resize drag cannot reallocate on the way back down.
		const auto edge = [](const int want, const int have)
		{
			return std::max(have, std::min(software_tile_extent, std::max(want, 1)));
		};

		return {edge(client.cx, _buffer.extent.cx), edge(client.cy, _buffer.extent.cy)};
	}

	void ensure_buffer(const sizei sz)
	{
		if (!_target)
		{
			_buffer = {};
		}
		else
		{
			_buffer = _target->acquire_buffer(sz);
			_buffer_origin = {};
		}

		sync_canvas();
	}


	software_text_renderer* renderer_for(ui::style::font_face font);

	// draw_context_device -------------------------------------------------------------------

	bool is_valid() const override
	{
		return _buffer.bits != nullptr;
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
		ensure_buffer(buffer_extent_for(client_extent));

		const recti client(0, 0, client_extent.cx, client_extent.cy);

		// The tile carries nothing between frames, so a partial repaint needs no retained pixels:
		// every pixel of the damaged region is written each frame, by the scene's own opening clear
		// or by the neutral pre-clear in replay_scene. A layered surface is presented whole.
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
		if (_buffer.extent.cx < 1 || _buffer.extent.cy < 1) return;

		for (auto y = region.top; y < region.bottom; y += _buffer.extent.cy)
		{
			for (auto x = region.left; x < region.right; x += _buffer.extent.cx)
			{
				const auto tile = recti(x, y, x + _buffer.extent.cx, y + _buffer.extent.cy).intersection(region);
				if (tile.is_empty()) continue;

				set_tile_origin({x, y});
				fn(tile);
			}
		}
	}

	// A CPU backend has no device to lose, so a frame it cannot present is dropped rather than
	// reported: the caller's remedy for a failure is to fall back to this backend.
	ui::present_result render() override
	{
		if (!_buffer.bits || !_target) return {};

		const auto region = _damage.intersection(recti(0, 0, _client_extent.cx, _client_extent.cy));
		if (region.is_empty()) return {};

		// A layered surface is presented whole, so it cannot be tiled: its buffer is the full client
		// and begin_draw forces full damage, which makes the loop below a single tile covering
		// everything.
		if (!_target->begin_present()) return {};

		for_each_tile(region, [&](const recti tile)
		{
			replay_scene(tile);
			_target->present_tile(tile, _buffer_origin);
		});

		_target->end_present(_layer_alpha);

		return {};
	}

	void resize(const sizei client_extent) override
	{
		_client_extent = client_extent;
		ensure_buffer(buffer_extent_for(client_extent));
	}

	void destroy() override
	{
		_scene.clear();
		_text_renderers.clear();
		_shadow.reset();
		_inverse_shadow.reset();
		free_buffer();
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
				_ctx->_canvas.blend_glyph(df::round(sx), df::round(sy), g.pixels.data(), {g.cx, g.cy}, clr);
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
	return std::make_shared<software_draw_context>(f, std::make_shared<gdi_present_target>(hwnd, layered),
	                                              base_font_size);
}

// What is left here after the rasterizer moved to render_software: the scratch buffer stops
// tracking the window, which only a real draw context can be asked. The seamlessness of tiling is
// ui::probe_software_rasterizer, and is not Windows.
platform::software_tiling_probe platform::probe_software_tiling()
{
	software_tiling_probe result;

	// Both extents exceed the tile, so the buffer is already at its final size and the target will
	// not reallocate across the grow - the case where the canvas last stopped following the window.
	// Driven through the memory target, which is the seam with no window behind it.
	software_draw_context ctx(nullptr, ui::create_memory_present_target(), normal_font_size);
	constexpr sizei started{software_tile_extent + 88, software_tile_extent + 88};
	constexpr sizei grown{software_tile_extent * 2 + 376, software_tile_extent + 388};

	ctx.begin_draw(started, normal_font_size, {});
	ctx.begin_draw(grown, normal_font_size, {});

	const recti client(0, 0, grown.cx, grown.cy);
	result.grown_client_pixels = client.width() * client.height();
	result.grown_buffer_pixels = ctx._buffer.extent.cx * ctx._buffer.extent.cy;

	ctx.for_each_tile(client, [&](const recti tile)
	{
		ctx._canvas._clip = tile;
		const auto writable = ctx._canvas.clamp_to_clip(client);
		result.grown_writable_pixels += writable.width() * writable.height();
	});

	return result;
}
