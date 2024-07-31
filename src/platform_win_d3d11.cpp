// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2024  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "pch.h"

#include "platform_win.h"
#include "util_geometry.h"
#include "files.h"

#include <versionhelpers.h>

#include "app_command_line.h"
#include "av_format.h"
#include "platform_win_res.h"
#include "platform_win_visual.h"

extern "C" {
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_d3d11va.h"
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool should_animate()
{
	BOOL result;
	if (::SystemParametersInfo(SPI_GETCLIENTAREAANIMATION, 0, &result, 0))
	{
		return !!result;
	}
	return !GetSystemMetrics(SM_REMOTESESSION);
}

static constexpr std::u8string_view to_string(D3D_FEATURE_LEVEL fl)
{
	switch (fl)
	{
	case D3D_FEATURE_LEVEL_1_0_CORE: return u8"1.0.CORE"sv;
	case D3D_FEATURE_LEVEL_9_1: return u8"9.1"sv;
	case D3D_FEATURE_LEVEL_9_2: return u8"9.2"sv;
	case D3D_FEATURE_LEVEL_9_3: return u8"9.3"sv;
	case D3D_FEATURE_LEVEL_10_0: return u8"10.0"sv;
	case D3D_FEATURE_LEVEL_10_1: return u8"10.1"sv;
	case D3D_FEATURE_LEVEL_11_0: return u8"11.0"sv;
	case D3D_FEATURE_LEVEL_11_1: return u8"11.1"sv;
	case D3D_FEATURE_LEVEL_12_0: return u8"12.0"sv;
	case D3D_FEATURE_LEVEL_12_1: return u8"12.1"sv;
	default:
		break;
	}

	return u8"?"sv;
}

void factories::reset_fonts()
{
	font_collection.Reset();
	font_renderers.clear();
}

bool factories::init(bool use_gpu)
{
	df::scope_rendering_func rf(__FUNCTION__);
	D2D1_FACTORY_OPTIONS d2d_options = { D2D1_DEBUG_LEVEL_NONE };

#ifdef _DEBUG
	d2d_options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif

	auto hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_options, d2d.GetAddressOf());

	if (FAILED(hr))
	{
		df::log(__FUNCTION__, str::format(u8"Failed to create ID2D1Factory2 {:x}"sv, hr));
	}

	if (SUCCEEDED(hr))
	{
		hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(dwrite),
			std::bit_cast<IUnknown**>(dwrite.GetAddressOf()));

		if (FAILED(hr))
		{
			df::log(__FUNCTION__, str::format(u8"Failed to create IDWriteFactory {:x}"sv, hr));
		}
	}

	if (SUCCEEDED(hr))
	{
#ifdef _DEBUG
		const uint32_t dxgi_flags = DXGI_CREATE_FACTORY_DEBUG;
		hr = CreateDXGIFactory2(dxgi_flags, __uuidof(dxgi), std::bit_cast<void**>(dxgi.GetAddressOf()));
#else
		hr = CreateDXGIFactory1(__uuidof(dxgi), std::bit_cast<void**>(dxgi.GetAddressOf()));
#endif

		if (FAILED(hr))
		{
			df::log(__FUNCTION__, str::format(u8"Failed to create IDXGIFactory {:x}"sv, hr));
		}
	}

	if (SUCCEEDED(hr))
	{
		hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory),
			std::bit_cast<void**>(wic.GetAddressOf()));

		if (FAILED(hr))
		{
			df::log(__FUNCTION__, str::format(u8"Failed to create IWICImagingFactory {:x}"sv, hr));
		}
	}

	setting.can_animate = should_animate();

	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;

	if (SUCCEEDED(hr))
	{
		uint32_t create_device_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

#ifdef _DEBUG
		create_device_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif


		constexpr D3D_FEATURE_LEVEL feature_levels_11_1[] =
		{
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
			D3D_FEATURE_LEVEL_10_1,
			D3D_FEATURE_LEVEL_10_0,
			D3D_FEATURE_LEVEL_9_3,
			D3D_FEATURE_LEVEL_9_2,
			D3D_FEATURE_LEVEL_9_1,
		};

		constexpr D3D_FEATURE_LEVEL feature_levels_11[] =
		{
			D3D_FEATURE_LEVEL_11_0,
			D3D_FEATURE_LEVEL_10_1,
			D3D_FEATURE_LEVEL_10_0,
			D3D_FEATURE_LEVEL_9_3,
			D3D_FEATURE_LEVEL_9_2,
			D3D_FEATURE_LEVEL_9_1,
		};

		auto driver_type = D3D_DRIVER_TYPE_HARDWARE;

		if (use_gpu)
		{
			//size_t BestAdapterVMem = 0;

			const ComPtr<IDXGIAdapter> intel_adapter;
			ComPtr<IDXGIAdapter> adapter;
			ComPtr<IDXGIFactory> factory;

			//if (SUCCEEDED(CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&factory)))
			//{
			//	for (uint32_t i = 0; factory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++)
			//	{
			//		DXGI_ADAPTER_DESC adapter_desc;
			//		adapter->GetDesc(&adapter_desc);
			//		
			//		if (adapter_desc.VendorId == 0x00008086) // intel's vendor ID
			//		{
			//			intel_adapter = adapter;
			//		}

			//		adapter = nullptr;
			//	}
			//}

			hr = D3D11CreateDevice(intel_adapter.Get(), driver_type, nullptr, create_device_flags, feature_levels_11_1,
				static_cast<uint32_t>(std::size(feature_levels_11_1)), D3D11_SDK_VERSION, &device,
				&feature_level, &context);

			if (hr == E_INVALIDARG)
			{
				df::log(__FUNCTION__, "D3D11CreateDevice failed with 11_1 - trying 11"sv);
				hr = D3D11CreateDevice(intel_adapter.Get(), driver_type, nullptr, create_device_flags,
					feature_levels_11, static_cast<uint32_t>(std::size(feature_levels_11)),
					D3D11_SDK_VERSION, &device, &feature_level, &context);
			}
		}

		if (FAILED(hr) || !use_gpu)
		{
			df::log(__FUNCTION__, "D3D11CreateDevice failed - trying software rendering"sv);

			driver_type = D3D_DRIVER_TYPE_WARP;

			hr = D3D11CreateDevice(nullptr, driver_type, nullptr, create_device_flags, feature_levels_11,
				static_cast<uint32_t>(std::size(feature_levels_11)),
				D3D11_SDK_VERSION, &device, &feature_level, &context);

			// dont animate for software rendering
			setting.can_animate = false;
		}
	}

	if (SUCCEEDED(hr))
	{
		hr = device.As(&dxgi_device);
	}

	if (SUCCEEDED(hr))
	{
		ComPtr<IDXGIDevice1> dxgi_device1;

		if (SUCCEEDED(dxgi_device.As(&dxgi_device1)))
		{
			dxgi_device1->SetMaximumFrameLatency(1);
		}
	}

	if (SUCCEEDED(hr))
	{
		d3d_device = device;
		d3d_context = context;
		d3d_feature_level = feature_level;

		df::d3d_info = to_string(feature_level);

		ComPtr<IDXGIAdapter> adapter;

		if (SUCCEEDED(dxgi_device->GetAdapter(&adapter)))
		{
			DXGI_ADAPTER_DESC adapter_desc;

			if (SUCCEEDED(adapter->GetDesc(&adapter_desc)))
			{
				const auto description = str::utf16_to_utf8(adapter_desc.Description);
				const auto gpu_id = str::format(u8"{:x}|{:x}|{:x}|{:x}"sv, adapter_desc.VendorId, adapter_desc.DeviceId,
					adapter_desc.SubSysId, adapter_desc.Revision);

				df::gpu_desc = description;
				df::gpu_id = gpu_id;

				df::log(__FUNCTION__, u8"     "s + description);
				df::log(__FUNCTION__, u8"     "s + gpu_id);
				df::log(__FUNCTION__,
					u8"     DedicatedVideoMemory "s + df::file_size(adapter_desc.DedicatedVideoMemory).str());
				df::log(__FUNCTION__,
					u8"     DedicatedSystemMemory "s + df::file_size(adapter_desc.DedicatedSystemMemory).str());
				df::log(__FUNCTION__,
					u8"     SharedSystemMemory "s + df::file_size(adapter_desc.SharedSystemMemory).str());
			}
		}

		register_fonts();
	}

	return SUCCEEDED(hr);
}


void factories::destroy()
{
	df::scope_rendering_func rf(__FUNCTION__);
	unregister_fonts();

	dxgi.Reset();
	d2d.Reset();
	dwrite.Reset();
	wic.Reset();
	d3d_device.Reset();
	d3d_context.Reset();
	//composition_device.Reset();
	dxgi_device.Reset();
	font_collection.Reset();
	font_renderers.clear();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct vertex_2d
{
	DirectX::XMFLOAT4 pos;
	DirectX::XMFLOAT2 tex;
	DirectX::XMFLOAT4 clr;
	DirectX::XMFLOAT2 tex_size;

	vertex_2d() = default;

	vertex_2d(const float x, const float y, const ui::color c) : tex()
	{
		pos.x = x;
		pos.y = y;
		pos.z = 0.0f;
		pos.w = 0.0f;
		tex_size.x = 0.0f;
		tex_size.y = 0.0f;
		clr.x = c.r;
		clr.y = c.g;
		clr.z = c.b;
		clr.w = c.a;
	}

	vertex_2d(const pointd loc, const ui::color c) : tex()
	{
		pos.x = static_cast<float>(loc.X);
		pos.y = static_cast<float>(loc.Y);
		pos.z = 0.0f;
		pos.w = 0.0f;
		tex_size.x = 0.0f;
		tex_size.y = 0.0f;
		clr.x = c.r;
		clr.y = c.g;
		clr.z = c.b;
		clr.w = c.a;
	}

	vertex_2d(const float x, const float y, const float u, const float v, const float a)
	{
		pos.x = x;
		pos.y = y;
		pos.z = 0.0f;
		pos.w = 0.0f;
		tex.x = u;
		tex.y = v;
		clr.w = a;
		clr.x = 1.0f;
		clr.y = 1.0f;
		clr.z = 1.0f;
		tex_size.x = 0.0f;
		tex_size.y = 0.0f;
	}

	vertex_2d(const pointd xy, const pointd uv, const ui::color c)
	{
		pos.x = static_cast<float>(xy.X);
		pos.y = static_cast<float>(xy.Y);
		pos.z = 0.0f;
		pos.w = 0.0f;
		tex.x = static_cast<float>(uv.X);
		tex.y = static_cast<float>(uv.Y);
		clr.x = c.r;
		clr.y = c.g;
		clr.z = c.b;
		clr.w = c.a;
		tex_size.x = 0.0f;
		tex_size.y = 0.0f;
	}

	vertex_2d(const pointd xy, const pointd uv, const ui::color c, const sizei tex_dims)
	{
		pos.x = static_cast<float>(xy.X);
		pos.y = static_cast<float>(xy.Y);
		pos.z = 0.0f;
		pos.w = 0.0f;
		tex.x = static_cast<float>(uv.X);
		tex.y = static_cast<float>(uv.Y);
		clr.x = c.r;
		clr.y = c.g;
		clr.z = c.b;
		clr.w = c.a;
		tex_size.x = static_cast<float>(tex_dims.cx);
		tex_size.y = static_cast<float>(tex_dims.cy);
	}

	vertex_2d(const float x, const float y, const float u, const float v, const ui::color c)
	{
		pos.x = x;
		pos.y = y;
		pos.z = 0.0f;
		pos.w = 0.0f;
		tex.x = u;
		tex.y = v;
		clr.x = c.r;
		clr.y = c.g;
		clr.z = c.b;
		clr.w = c.a;
		tex_size.x = 0.0f;
		tex_size.y = 0.0f;
	}

	vertex_2d(float x, float y, float u, float v, const ui::color c, const sizei tex_dims)
	{
		pos.x = x;
		pos.y = y;
		pos.z = 0.0f;
		pos.w = 0.0f;
		tex.x = u;
		tex.y = v;
		clr.x = c.r;
		clr.y = c.g;
		clr.z = c.b;
		clr.w = c.a;
		tex_size.x = static_cast<float>(tex_dims.cx);
		tex_size.y = static_cast<float>(tex_dims.cy);
	}

	void set(float x, float y, float u, float v, const ui::color c)
	{
		pos.x = x;
		pos.y = y;
		pos.z = 0.0f;
		pos.w = 0.0f;
		tex.x = u;
		tex.y = v;
		clr.x = c.r;
		clr.y = c.g;
		clr.z = c.b;
		clr.w = c.a;
		tex_size.x = 0.0f;
		tex_size.y = 0.0f;
	}

	void set(const pointd xy, const pointd uv, const ui::color c)
	{
		pos.x = static_cast<float>(xy.X);
		pos.y = static_cast<float>(xy.Y);
		pos.z = 0.0f;
		pos.w = 0.0f;
		tex.x = static_cast<float>(uv.X);
		tex.y = static_cast<float>(uv.Y);
		clr.x = c.r;
		clr.y = c.g;
		clr.z = c.b;
		clr.w = c.a;
		tex_size.x = 0.0f;
		tex_size.y = 0.0f;
	}

	void set(const pointd xy, const pointd uv, float a)
	{
		pos.x = static_cast<float>(xy.X);
		pos.y = static_cast<float>(xy.Y);
		pos.z = 0.0f;
		pos.w = 0.0f;
		tex.x = static_cast<float>(uv.X);
		tex.y = static_cast<float>(uv.Y);
		clr.w = a;
		clr.x = 1.0f;
		clr.y = 1.0f;
		clr.z = 1.0f;
		tex_size.x = 0.0f;
		tex_size.y = 0.0f;
	}
};

constexpr uint32_t vertex_stride = static_cast<uint32_t>(sizeof(vertex_2d));
constexpr uint32_t icon_texture_size = 512;

static_assert(std::is_trivial_v<vertex_2d>);

#pragma comment(lib, "d3d11")
#pragma comment(lib, "dxgi")
//#pragma comment(lib, "d3dcompiler"sv)

// vlc renderer
// https://github.com/videolan/vlc-unity/blob/master/Assets/PluginSource/RenderAPI_D3D11.cpp

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class d3d11_draw_context_impl;


class d3d11_text_renderer final : df::no_copy, public IDWriteTextRenderer
{
private:
	factories_ptr _f;
	std::shared_ptr<d3d11_draw_context_impl> _canvas;
	ComPtr<ID3D11Texture2D> _texture;

	uint32_t _xy_tex = 0u;
	uint32_t _spacing = 0u;
	uint32_t _line_height = 0u;
	uint32_t _base_line_height = 0u;

	struct coords
	{
		uint32_t tx1, ty1, tx2, ty2;
		int x_offset;
	};

	df::hash_map<char32_t, uint16_t> _chars_to_glyphs;
	df::hash_map<char32_t, coords> _coords;
	font_renderer_ptr _font;
	pointi _next_location;

	ui::color _clr;
	std::vector<ui::text_highlight_t> _highlights;

	coords find_glyph(uint16_t c, const DWRITE_GLYPH_RUN* glyph_run);
	void create_a8_texture(int xy);

	std::atomic<int> _ref_count = 0;

public:
	d3d11_text_renderer() = default;
	~d3d11_text_renderer() = default;

	void reset(const std::shared_ptr<d3d11_draw_context_impl>& c, const factories_ptr& f, font_renderer_ptr fr);
	void reset();

	[[nodiscard]] font_renderer_ptr font() const
	{
		return _font;
	}

	void draw_text(std::u8string_view text, recti bounds, ui::style::text_style style, ui::color c, ui::color bg);

	void draw_text(std::u8string_view text, const std::vector<ui::text_highlight_t>& highlights, recti bounds,
		ui::style::text_style style, ui::color clr, ui::color bg);

	void draw_text(const std::shared_ptr<text_layout_impl>& text, const recti bounds, const ui::color clr,
		const ui::color bg)
	{
		df::scope_rendering_func rf(__FUNCTION__);
		_clr = clr;
		text->_renderer->draw(std::bit_cast<ui::draw_context*>(_canvas.get()), this, text->_layout.Get(), bounds, clr,
			bg);
	}

	sizei measure_text(const std::u8string_view text, const sizei avail, const ui::style::text_style style) const
	{
		df::scope_rendering_func rf(__FUNCTION__);
		const auto text16 = str::utf8_to_utf16(text);
		return _font->measure(text16, style, avail.cx, avail.cy);
	}

	int line_height() const { return _line_height; }


	// ----- IUnknown -----

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
		void** ppvObject) override
	{
		if (riid == __uuidof(IDWriteTextRenderer))
		{
			(*ppvObject) = static_cast<IDWriteTextRenderer*>(this);
		}
		else if (riid == __uuidof(IDWritePixelSnapping))
		{
			(*ppvObject) = static_cast<IDWritePixelSnapping*>(this);
		}
		else
		{
			return E_NOINTERFACE;
		}

		AddRef();
		return S_OK;
	}

	ULONG STDMETHODCALLTYPE AddRef() override
	{
		++_ref_count;
		return _ref_count;
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		--_ref_count;
		return _ref_count;
	}

	HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void* clientDrawingContext, BOOL* isDisabled) override
	{
		*isDisabled = FALSE;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE GetCurrentTransform(void* clientDrawingContext, DWRITE_MATRIX* transform) override
	{
		*transform = {};
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void* clientDrawingContext, FLOAT* pixelsPerDip) override;

	HRESULT STDMETHODCALLTYPE DrawGlyphRun(void* clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY,
		DWRITE_MEASURING_MODE measuringMode, const DWRITE_GLYPH_RUN* glyphRun,
		const DWRITE_GLYPH_RUN_DESCRIPTION* glyphRunDescription,
		IUnknown* clientDrawingEffect) override;

	HRESULT STDMETHODCALLTYPE DrawUnderline(void* clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY,
		const DWRITE_UNDERLINE* underline, IUnknown* clientDrawingEffect) override
	{
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE DrawStrikethrough(void* clientDrawingContext, FLOAT baselineOriginX,
		FLOAT baselineOriginY, const DWRITE_STRIKETHROUGH* strikethrough,
		IUnknown* clientDrawingEffect) override
	{
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE DrawInlineObject(void* clientDrawingContext, FLOAT originX, FLOAT originY,
		IDWriteInlineObject* inlineObject, BOOL isSideways, BOOL isRightToLeft,
		IUnknown* clientDrawingEffect) override
	{
		return S_OK;
	}
};

class d3d11_texture final : public ui::texture
{
public:
	factories_ptr _f;
	ComPtr<ID3D11Texture2D> _texture;

	std::unique_ptr<av_scaler> _scaler;
	ComPtr<ID3D11Texture2D> _shared_texture;
	sizei _shared_texture_dimensions;
	ui::texture_format _shared_texture_format = ui::texture_format::None;

	void free_scaler()
	{
		_scaler.reset();
	}

public:
	explicit d3d11_texture(const factories_ptr& f) : _f(f)
	{
	}

	~d3d11_texture() override
	{
		free_scaler();
	}


	bool is_valid() const override
	{
		return _texture != nullptr;
	}

	bool supports_nv12() const
	{
		UINT support = 0;

		return SUCCEEDED(_f->d3d_device->CheckFormatSupport(DXGI_FORMAT_NV12, &support))
			&& (support & D3D11_FORMAT_SUPPORT_TEXTURE2D);
	}

	ui::texture_update_result update(const av_frame_ptr& frame) override;
	ui::texture_update_result update(const ui::const_surface_ptr& surface) override;
	ui::texture_update_result update(sizei dims, ui::texture_format format, ui::orientation orientation,
		const uint8_t* pixels, size_t stride, size_t buffer_size) override;

	friend class av_video_frames;
};

class d3d11_vertices;

struct scene_atom
{
	ID3D11Texture2D* texture = nullptr;
	ID3D11PixelShader* shader = nullptr;

	ui::texture_format tex_format = ui::texture_format::None;
	ui::texture_sampler sampler = ui::texture_sampler::point;

	uint32_t start_vertex = 0;
	uint32_t vertex_count = 0;

	uint32_t start_index = 0;
	uint32_t index_count = 0;

	std::shared_ptr<d3d11_vertices> verts;
};


static_assert(std::is_move_constructible_v<scene_atom>);

using texture_d3d11_ptr = std::shared_ptr<d3d11_texture>;

class d3d11_vertices final : public std::enable_shared_from_this<d3d11_vertices>, public ui::vertices
{
public:
	std::shared_ptr<d3d11_draw_context_impl> _canvas;
	std::vector<scene_atom> _scene_atoms;

	ComPtr<ID3D11Buffer> _vertex_buffer;
	ComPtr<ID3D11Buffer> _index_buffer;

	explicit d3d11_vertices(std::shared_ptr<d3d11_draw_context_impl> c) : _canvas(std::move(c))
	{
	}

	void update(recti rects[], ui::color colors[], int num_bars) override;
};


class d3d11_draw_context_impl final : public draw_context_device,
	public std::enable_shared_from_this<d3d11_draw_context_impl>
{
public:
	recti _clip_bounds;
	sizei _client_extent;

	factories_ptr _f;

	ComPtr<IDXGISwapChain> _swap_chain;

	ComPtr<ID3D11VertexShader> _vertex_shader;
	ComPtr<ID3D11PixelShader> _pixel_shader_solid;
	ComPtr<ID3D11InputLayout> _vertex_layout;
	ComPtr<ID3D11PixelShader> _pixel_shader_rgb;
	ComPtr<ID3D11PixelShader> _pixel_shader_rgb_bicubic;
	ComPtr<ID3D11PixelShader> _pixel_shader_font;
	ComPtr<ID3D11PixelShader> _pixel_shader_circle;
	ComPtr<ID3D11PixelShader> _pixel_shader_yuv;
	ComPtr<ID3D11PixelShader> _pixel_shader_yuv_bicubic;
	ComPtr<ID3D11Buffer> _vertex_buffer;
	ComPtr<ID3D11Buffer> _index_buffer;
	ComPtr<ID3D11BlendState> _blend_state;
	ComPtr<ID3D11RasterizerState> _rasterizer_state;
	ComPtr<ID3D11SamplerState> _sampler_point;
	ComPtr<ID3D11SamplerState> _sampler_bilinear;
	std::set<ComPtr<ID3D11Texture2D>> _scene_textures;


	int _adapters_count = 0;
	int _reset_device_count = 0;
	bool _supports_p010 = false;
	bool _supports_nv12 = false;
	bool _is_reset = false;
	bool _is_valid = false;
	int _base_font_size = 0;

	texture_d3d11_ptr _shadow;
	texture_d3d11_ptr _inverse_shadow;

	std::map<ui::style::font_face, d3d11_text_renderer> _font;

	std::vector<vertex_2d> _vertex_buffer_staging;
	std::vector<WORD> _index_buffer_staging;
	double time_now = 0.0;

	std::vector<scene_atom> _scene_atoms;

public:
	d3d11_draw_context_impl() = default;

	~d3d11_draw_context_impl() override
	{
		destroy();
	}

	void create(const factories_ptr& f, ComPtr<IDXGISwapChain> swap_chain, int base_font_size, bool use_gpu);

	void resize(sizei extent) override;
	void update_font_size(int base_font_size) override;

	void build_index_and_vertex_buffers();
	void draw_scene(const ComPtr<ID3D11DeviceContext>& context);


	sizei measure_string(std::u8string_view text, sizei size_avail, ui::style::font_face, ui::style::text_style);
	int line_height(ui::style::font_face);

	bool supports_p010() const
	{
		return _supports_p010 && setting.use_yuv;
	}

	bool supports_nv12() const
	{
		return _supports_nv12 && setting.use_yuv;
	}

	bool is_valid() const override
	{
		return _is_valid && _f->d3d_device && _f->d3d_context;
	}

	sizei client_extent() const
	{
		return _client_extent;
	}

	ID3D11PixelShader* calc_shader(bool is_bicubic, ui::texture_format tex_fmt) const;

	void add_scene_atom(const ComPtr<ID3D11Texture2D>& vv, const ComPtr<ID3D11PixelShader>& ss,
		ui::texture_format tex_fmt, ui::texture_sampler sampler, const vertex_2d* vertices,
		size_t vertex_count, const WORD* indexes, size_t index_count);
	void draw_texture(const texture_d3d11_ptr& t, const quadd& dst, recti src, ui::color c,
		ui::texture_sampler sampler);
	void draw_texture(const texture_d3d11_ptr& t, recti dst, recti src, ui::color c, ui::texture_sampler sampler,
		float radius);

	void destroy() override;
	void begin_draw(sizei client_extent, int base_font_size) override;
	void render() override;
	bool can_use_gpu() const;

	void draw_rect(const recti dst, const ui::color clr1, const ui::color clr2)
	{
		df::scope_rendering_func rf(__FUNCTION__);
		df::assert_true(ui::is_ui_thread());

		if (clr1.a >= 0.01f || clr2.a >= 0.01f)
		{
			if (!dst.is_empty())
			{
				const auto r = rectd(dst).scale(_client_extent);

				const auto center = r.center();
				const auto dl = static_cast<float>(r.X);
				const auto dt = static_cast<float>(r.Y);
				const auto dr = static_cast<float>(r.right());
				const auto db = static_cast<float>(r.bottom());
				const auto cx = static_cast<float>(center.X);
				const auto cy = static_cast<float>(center.Y);

				const vertex_2d vertices[] = {

					vertex_2d(dl, dt, clr2),
					vertex_2d(dr, dt, clr2),
					vertex_2d(cx, cy, clr1),
					vertex_2d(dr, db, clr2),
					vertex_2d(dl, db, clr2),
				};

				const WORD indexes[] = {

					0, 1, 2,
					1, 3, 2,
					3, 4, 2,
					4, 0, 2
				};

				add_scene_atom(nullptr, _pixel_shader_solid, ui::texture_format::None, ui::texture_sampler::point,
					vertices, std::size(vertices), indexes, std::size(indexes));
			}
		}
	}

	void clear(ui::color c) override;
	void draw_rounded_rect(recti bounds, ui::color c, int radius) override;
	void draw_rect(recti bounds, ui::color c) override;
	void draw_text(std::u8string_view text, recti bounds, ui::style::font_face font, ui::style::text_style style,
		ui::color c, ui::color bg) override;
	void draw_text(std::u8string_view text, const std::vector<ui::text_highlight_t>& highlights, recti bounds,
		ui::style::font_face font, ui::style::text_style style, ui::color clr, ui::color bg) override;
	void draw_text(const ui::text_layout_ptr& tl, recti bounds, ui::color clr, ui::color bg) override;
	void draw_shadow(recti bounds, int width, float alpha, bool inverse) override;
	void draw_border(recti inside, recti outside, ui::color c_inside, ui::color c_outside) override;
	void draw_texture(const ui::texture_ptr& t, recti dst, float alpha, ui::texture_sampler sampler) override;
	void draw_texture(const ui::texture_ptr& t, recti dst, recti src, float alpha, ui::texture_sampler sampler,
		float radius) override;
	void draw_texture(const ui::texture_ptr& t, const quadd& dst, recti src, float alpha,
		ui::texture_sampler sampler) override;
	void draw_vertices(const ui::vertices_ptr& v) override;

	ui::texture_ptr create_texture() override;
	ui::vertices_ptr create_vertices() override;
	font_renderer_ptr find_font(ui::style::font_face font);
	ui::text_layout_ptr create_text_layout(ui::style::font_face font) override;

	sizei measure_text(std::u8string_view text, ui::style::font_face font, ui::style::text_style style, int width,
		int height) override;
	int text_line_height(ui::style::font_face type) override;

	recti clip_bounds() const override;
	void clip_bounds(recti) override;
	void restore_clip() override;
	void draw_edge_shadows(float alpha) override;

private:
	friend class render_font_d3d11;
};

static constexpr DXGI_FORMAT to_format(const ui::texture_format format)
{
	switch (format)
	{
	case ui::texture_format::ARGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
	case ui::texture_format::NV12: return DXGI_FORMAT_NV12;
	case ui::texture_format::P010: return DXGI_FORMAT_P010;
	case ui::texture_format::None:
	case ui::texture_format::RGB:
	default:
		break;
	}

	return DXGI_FORMAT_B8G8R8X8_UNORM;
}

void d3d11_draw_context_impl::destroy()
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());

	_shadow.reset();
	_inverse_shadow.reset();
	_scene_atoms.clear();
	_scene_textures.clear();
	_font.clear();
	_vertex_buffer_staging.clear();
	_index_buffer_staging.clear();

	_swap_chain.Reset();
	_vertex_shader.Reset();
	_pixel_shader_solid.Reset();
	_vertex_layout.Reset();
	_pixel_shader_rgb.Reset();
	_pixel_shader_rgb_bicubic.Reset();
	_pixel_shader_font.Reset();
	_pixel_shader_circle.Reset();
	_pixel_shader_yuv.Reset();
	_pixel_shader_yuv_bicubic.Reset();
	_vertex_buffer.Reset();
	_index_buffer.Reset();
	_blend_state.Reset();
	_rasterizer_state.Reset();
	_sampler_point.Reset();
	_sampler_bilinear.Reset();
}

void d3d11_draw_context_impl::update_font_size(const int base_font_size)
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());

	if (_base_font_size != base_font_size || _font.empty())
	{
		_base_font_size = base_font_size;

		const auto c = shared_from_this();
		_font[ui::style::font_face::code].reset(c, _f, _f->font_face(ui::style::font_face::code, base_font_size));
		_font[ui::style::font_face::dialog].reset(c, _f, _f->font_face(ui::style::font_face::dialog, base_font_size));
		_font[ui::style::font_face::title].reset(c, _f, _f->font_face(ui::style::font_face::title, base_font_size));
		_font[ui::style::font_face::mega].reset(c, _f, _f->font_face(ui::style::font_face::mega, base_font_size));
		_font[ui::style::font_face::icons].reset(c, _f, _f->font_face(ui::style::font_face::icons, base_font_size));
		_font[ui::style::font_face::small_icons].reset(c, _f, _f->font_face(ui::style::font_face::small_icons, base_font_size));
		_font[ui::style::font_face::petscii].reset(c, _f, _f->font_face(ui::style::font_face::petscii, base_font_size));
	}
}

//HRESULT FindAdapter(IDirect3D9* pD3D9, HMONITOR hMonitor, uint32_t* puAdapterID)
//{
//    HRESULT hr = E_FAIL;
//    uint32_t cAdapters = 0;
//    uint32_t uAdapterID = static_cast<uint32_t>(-1);
//
//    cAdapters = pD3D9->GetAdapterCount();
//    for (uint32_t i = 0; i < cAdapters; i++)
//    {
//        HMONITOR hMonitorTmp = pD3D9->GetAdapterMonitor(i);
//
//        if (hMonitorTmp == nullptr)
//        {
//            break;
//        }
//        if (hMonitorTmp == hMonitor)
//        {
//            uAdapterID = i;
//            break;
//        }
//    }
//
//    if (uAdapterID != static_cast<uint32_t>(-1))
//    {
//        *puAdapterID = uAdapterID;
//        hr = S_OK;
//    }
//    return hr;
//}


static texture_d3d11_ptr create_texture_from_resource(const factories_ptr& f, int id, LPCWSTR type)
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());
	files ff;
	auto result = std::make_shared<d3d11_texture>(f);
	result->update(ff.image_to_surface(load_resource(id, type)));
	return result;
}


void d3d11_draw_context_impl::create(const factories_ptr& f, ComPtr<IDXGISwapChain> swap_chain,
	const int base_font_size, const bool use_gpu)
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());
	_f = f;
	_swap_chain = swap_chain;

	auto hr = S_OK;

	if (swap_chain)
	{
		df::log(__FUNCTION__, str::format(u8"D3D11CreateDevice success {}"sv, to_string(_f->d3d_feature_level)));


		uint32_t support = 0;

		_supports_p010 = SUCCEEDED(_f->d3d_device->CheckFormatSupport(to_format(ui::texture_format::P010), &support))
			&& (support & D3D11_FORMAT_SUPPORT_TEXTURE2D);

		_supports_nv12 = SUCCEEDED(_f->d3d_device->CheckFormatSupport(to_format(ui::texture_format::NV12), &support))
			&& (support & D3D11_FORMAT_SUPPORT_TEXTURE2D);

		df::log(__FUNCTION__, _supports_p010 ? "     p010 supported"sv : "     p010 not-supported"sv);
		df::log(__FUNCTION__, _supports_nv12 ? "     nv12 supported"sv : "     nv12 not-supported"sv);


		if (SUCCEEDED(hr))
		{
			// The ID3D11Device must have multithread protection 
			ComPtr<ID3D10Multithread> multithread;

			if (SUCCEEDED(
				_f->d3d_device->QueryInterface(__uuidof(ID3D10Multithread), std::bit_cast<void**>(multithread.
					GetAddressOf()))))
			{
				multithread->SetMultithreadProtected(TRUE);
				multithread = nullptr;
			}
		}

		if (SUCCEEDED(hr))
		{
			_shadow = create_texture_from_resource(_f, IDB_SHADOW, L"PNG");
			_inverse_shadow = create_texture_from_resource(_f, IDB_INVERSE_SHADOW, L"PNG");

			const auto shader = load_resource(IDR_SHADER_VERTEX, L"SHADER");
			hr = _f->d3d_device->CreateVertexShader(shader.data(), shader.size(), nullptr, &_vertex_shader);

			if (SUCCEEDED(hr))
			{
				D3D11_INPUT_ELEMENT_DESC layout[] =
				{
					{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
					{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
					{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
					{"EXTENT", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0},
				};

				const uint32_t num_elements = ARRAYSIZE(layout);
				hr = _f->d3d_device->CreateInputLayout(layout, num_elements, shader.data(), shader.size(),
					&_vertex_layout);
			}
		}

		if (SUCCEEDED(hr))
		{
			const auto shader = load_resource(IDR_SHADER_SOLID, L"SHADER");
			hr = _f->d3d_device->CreatePixelShader(shader.data(), shader.size(), nullptr, &_pixel_shader_solid);
		}

		if (SUCCEEDED(hr))
		{
			const auto shader = load_resource(IDR_SHADER_RGB, L"SHADER");
			hr = _f->d3d_device->CreatePixelShader(shader.data(), shader.size(), nullptr, &_pixel_shader_rgb);
		}

		if (SUCCEEDED(hr))
		{
			const auto shader = load_resource(IDR_SHADER_RGB_BICUBIC, L"SHADER");
			hr = _f->d3d_device->CreatePixelShader(shader.data(), shader.size(), nullptr, &_pixel_shader_rgb_bicubic);
		}

		if (SUCCEEDED(hr))
		{
			const auto shader = load_resource(IDR_SHADER_FONT, L"SHADER");
			hr = _f->d3d_device->CreatePixelShader(shader.data(), shader.size(), nullptr, &_pixel_shader_font);
		}

		if (SUCCEEDED(hr))
		{
			const auto shader = load_resource(IDR_SHADER_CIRCLE, L"SHADER");
			hr = _f->d3d_device->CreatePixelShader(shader.data(), shader.size(), nullptr, &_pixel_shader_circle);
		}

		if (SUCCEEDED(hr))
		{
			const auto shader = load_resource(IDR_SHADER_YUV, L"SHADER");
			hr = _f->d3d_device->CreatePixelShader(shader.data(), shader.size(), nullptr, &_pixel_shader_yuv);
		}

		if (SUCCEEDED(hr))
		{
			const auto shader = load_resource(IDR_SHADER_YUV_BICUBIC, L"SHADER");
			hr = _f->d3d_device->CreatePixelShader(shader.data(), shader.size(), nullptr, &_pixel_shader_yuv_bicubic);
		}

		if (SUCCEEDED(hr))
		{
			D3D11_BLEND_DESC desc = {};
			desc.AlphaToCoverageEnable = FALSE;
			desc.IndependentBlendEnable = FALSE;
			desc.RenderTarget[0].BlendEnable = TRUE;
			desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

			// color
			desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
			desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
			desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;

			// alpha
			desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
			desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
			desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;

			hr = _f->d3d_device->CreateBlendState(&desc, &_blend_state);

			if (FAILED(hr))
			{
				df::log(__FUNCTION__, str::format(u8"CreateBlendState failed {:x}"sv, hr));
			}
		}

		if (SUCCEEDED(hr))
		{
			D3D11_RASTERIZER_DESC desc = {};
			desc.CullMode = D3D11_CULL_NONE;
			desc.FillMode = D3D11_FILL_SOLID;

			hr = _f->d3d_device->CreateRasterizerState(&desc, &_rasterizer_state);

			if (FAILED(hr))
			{
				df::log(__FUNCTION__, str::format(u8"CreateRasterizerState failed {:x}"sv, hr));
			}
		}

		if (SUCCEEDED(hr))
		{
			D3D11_SAMPLER_DESC sampler_desc = {};
			sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampler_desc.MipLODBias = 0.0f;
			sampler_desc.MaxAnisotropy = 1;
			sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
			sampler_desc.MinLOD = -FLT_MAX;
			sampler_desc.MaxLOD = FLT_MAX;

			hr = _f->d3d_device->CreateSamplerState(&sampler_desc, &_sampler_point);

			if (FAILED(hr))
			{
				df::log(__FUNCTION__, str::format(u8"CreateSamplerState point failed {:x}"sv, hr));
			}

			sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;

			hr = _f->d3d_device->CreateSamplerState(&sampler_desc, &_sampler_bilinear);

			if (FAILED(hr))
			{
				df::log(__FUNCTION__, str::format(u8"CreateSamplerState bilinear failed {:x}"sv, hr));
			}
		}

		update_font_size(base_font_size);
	}

	if (FAILED(hr))
	{
		df::log(__FUNCTION__, str::format(u8"draw_context_d3d11_impl::create failed {:x}"sv, hr));

		if (use_gpu)
		{
			df::log(__FUNCTION__, "Retry without gpu."sv);
			create(_f, swap_chain, base_font_size, false);
			return;
		}
	}

	_is_valid = SUCCEEDED(hr);
}

void d3d11_draw_context_impl::resize(const sizei extent)
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());

	if (_client_extent != extent && extent.cx > 0 && extent.cy > 0)
	{
		_client_extent = extent;
		_clip_bounds = extent;

		//if (_swap_chain)
		//{
		//	//DXGI_MODE_DESC mode_desc;
		//	//mode_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		//	//mode_desc.Height = size.cy;
		//	//mode_desc.Width = size.cx;
		//	//_swap_chain->ResizeTarget(&mode_desc);

		//	UINT flags = 0;
		//	_swap_chain->ResizeBuffers(swap_buffer_count, extent.cx, extent.cy, back_buffer_format, flags);
		//}
	}
}

void d3d11_draw_context_impl::begin_draw(const sizei client_extent, int base_font_size)
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());

	if (is_valid())
	{
		_client_extent = client_extent;
		_clip_bounds.set(0, 0, _client_extent.cx, _client_extent.cy);

		_scene_atoms.clear();
		_scene_textures.clear();

		_vertex_buffer_staging.clear();
		_index_buffer_staging.clear();

		_vertex_buffer_staging.reserve(5000);
		_index_buffer_staging.reserve(8000);
		//_scene_textures.reserve(32);
		_scene_atoms.reserve(256);
	}
}

bool d3d11_draw_context_impl::can_use_gpu() const
{
	return setting.use_gpu && !command_line.no_gpu && _reset_device_count <= 3;
}

void d3d11_draw_context_impl::build_index_and_vertex_buffers()
{
	df::scope_rendering_func rf(__FUNCTION__);
	if (!_vertex_buffer_staging.empty())
	{
		ComPtr<ID3D11Buffer> buffer;
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = static_cast<uint32_t>(sizeof(vertex_2d) * _vertex_buffer_staging.size());
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		bd.CPUAccessFlags = 0;

		D3D11_SUBRESOURCE_DATA source_data = {};
		source_data.pSysMem = _vertex_buffer_staging.data();

		const auto hr = _f->d3d_device->CreateBuffer(&bd, &source_data, &buffer);

		if (SUCCEEDED(hr))
		{
			_vertex_buffer = buffer;
		}
		else
		{
			buffer = nullptr;
		}

		_vertex_buffer_staging.clear();
	}

	if (!_index_buffer_staging.empty())
	{
		ComPtr<ID3D11Buffer> buffer;
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = static_cast<uint32_t>(sizeof(WORD) * _index_buffer_staging.size());
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
		bd.CPUAccessFlags = 0;

		D3D11_SUBRESOURCE_DATA source_data = {};
		source_data.pSysMem = _index_buffer_staging.data();

		const auto hr = _f->d3d_device->CreateBuffer(&bd, &source_data, &buffer);

		if (SUCCEEDED(hr))
		{
			_index_buffer = buffer;
		}
		else
		{
			buffer = nullptr;
		}

		_index_buffer_staging.clear();
	}
}

struct context_state final
{
	ID3D11PixelShader* shader = nullptr;
	ID3D11Texture2D* texture = nullptr;
	ID3D11SamplerState* sampler = nullptr;
	ID3D11Buffer* vertex_buffer = nullptr;
	ID3D11Buffer* index_buffer = nullptr;

	df::hash_map<struct ID3D11Texture2D*, ComPtr<struct ID3D11ShaderResourceView>> texture_views;

	ID3D11DeviceContext* context;
	ID3D11Device* device;

	context_state(ID3D11Device* d, ID3D11DeviceContext* c) : context(c), device(d)
	{
	}

	void draw_atom(const scene_atom& a, ID3D11Buffer* vb, ID3D11Buffer* ib, ID3D11SamplerState* ss)
	{
		df::scope_rendering_func rf(__FUNCTION__);
		auto* const s = a.shader;
		auto* const t = a.texture;
		const auto tx_fmt = a.tex_format;

		if (s != shader)
		{
			shader = s;
			context->PSSetShader(s, nullptr, 0);
		}

		if (ss != sampler)
		{
			sampler = ss;
			ID3D11SamplerState* samplers[] = { ss };
			context->PSSetSamplers(0, 1, samplers);
		}

		if (vb != vertex_buffer)
		{
			vertex_buffer = vb;
			UINT offsets[] = { 0 };
			ID3D11Buffer* buffers[] = { vertex_buffer };
			context->IASetVertexBuffers(0, 1, buffers, &vertex_stride, offsets);
		}

		if (ib != index_buffer)
		{
			index_buffer = ib;
			context->IASetIndexBuffer(index_buffer, DXGI_FORMAT_R16_UINT, 0);
		}

		if (t != texture && t != nullptr)
		{
			texture = t;

			if (tx_fmt == ui::texture_format::NV12)
			{
				ComPtr<ID3D11ShaderResourceView> texture_view_y;
				ComPtr<ID3D11ShaderResourceView> texture_view_uv;

				D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
				srv.Format = DXGI_FORMAT_R8_UNORM;
				srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				srv.Texture2D.MipLevels = 1;
				srv.Texture2D.MostDetailedMip = 0;
				auto hr = device->CreateShaderResourceView(texture, &srv, &texture_view_y);

				if (SUCCEEDED(hr))
				{
					srv.Format = DXGI_FORMAT_R8G8_UNORM;
					hr = device->CreateShaderResourceView(texture, &srv, &texture_view_uv);

					if (SUCCEEDED(hr))
					{
						ID3D11ShaderResourceView* views[] = { texture_view_y.Get(), texture_view_uv.Get() };
						context->PSSetShaderResources(0, 2, views);
					}
				}
			}
			else if (tx_fmt == ui::texture_format::P010)
			{
				ComPtr<ID3D11ShaderResourceView> texture_view_y;
				ComPtr<ID3D11ShaderResourceView> texture_view_uv;

				D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
				srv.Format = DXGI_FORMAT_R16_UNORM;
				srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
				srv.Texture2D.MipLevels = 1;
				srv.Texture2D.MostDetailedMip = 0;
				HRESULT hr = device->CreateShaderResourceView(texture, &srv, &texture_view_y);

				if (SUCCEEDED(hr))
				{
					srv.Format = DXGI_FORMAT_R16G16_UNORM;
					hr = device->CreateShaderResourceView(texture, &srv, &texture_view_uv);

					if (SUCCEEDED(hr))
					{
						ID3D11ShaderResourceView* views[] = { texture_view_y.Get(), texture_view_uv.Get() };
						context->PSSetShaderResources(0, 2, views);
					}
				}
			}
			else
			{
				ComPtr<ID3D11ShaderResourceView> view;

				auto found = texture_views.find(t);

				if (found == texture_views.end())
				{
					if (SUCCEEDED(device->CreateShaderResourceView(t, nullptr, &view)))
					{
						texture_views[t] = view;
					}
				}
				else
				{
					view = found->second;
				}

				ID3D11ShaderResourceView* views[] = { view.Get() };
				context->PSSetShaderResources(0, 1, views);
			}
		}

		context->DrawIndexed(a.index_count, a.start_index, a.start_vertex);
	}
};

void d3d11_draw_context_impl::draw_scene(const ComPtr<ID3D11DeviceContext>& context)
{
	df::scope_rendering_func rf(__FUNCTION__);
	ComPtr<ID3D11RenderTargetView> rtv;
	ComPtr<ID3D11Texture2D> back_buffer;

	context->ClearState();

	auto hr = _swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), std::bit_cast<void**>(back_buffer.GetAddressOf()));

	df::assert_true(SUCCEEDED(hr));

	if (SUCCEEDED(hr) && back_buffer)
	{
		hr = _f->d3d_device->CreateRenderTargetView(back_buffer.Get(), nullptr, &rtv);
	}

	if (SUCCEEDED(hr))
	{
		ID3D11RenderTargetView* views[] = { rtv.Get() };
		context->OMSetRenderTargets(1, views, nullptr);

		const D3D11_VIEWPORT viewport = {
			0.0f, 0.0f, static_cast<float>(_client_extent.cx), static_cast<float>(_client_extent.cy), 0.0f, 0.0f
		};
		context->RSSetViewports(1, &viewport);

		if (_rasterizer_state)
		{
			context->RSSetState(_rasterizer_state.Get());
		}

		if (_vertex_layout)
		{
			context->IASetInputLayout(_vertex_layout.Get());
		}

		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		if (_vertex_shader)
		{
			context->VSSetShader(_vertex_shader.Get(), nullptr, 0);
		}

		if (_blend_state)
		{
			context->OMSetBlendState(_blend_state.Get(), nullptr, 0xFFFFFF);
		}

		const DirectX::XMVECTORF32 bg_color = { {0.222f, 0.222f, 0.222f, 1.0f} };
		context->ClearRenderTargetView(rtv.Get(), bg_color);

		context_state state(_f->d3d_device.Get(), context.Get());

		for (const auto& a : _scene_atoms)
		{
			if (a.verts)
			{
				const auto& vb = a.verts->_vertex_buffer;
				const auto& ib = a.verts->_index_buffer;

				for (const auto& aa : a.verts->_scene_atoms)
				{
					const auto ss = aa.sampler == ui::texture_sampler::point ? _sampler_point : _sampler_bilinear;
					state.draw_atom(aa, vb.Get(), ib.Get(), ss.Get());
				}
			}
			else
			{
				const auto ss = a.sampler == ui::texture_sampler::point ? _sampler_point : _sampler_bilinear;
				state.draw_atom(a, _vertex_buffer.Get(), _index_buffer.Get(), ss.Get());
			}
		}
	}
}

void d3d11_draw_context_impl::render()
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());

	if (_f->d3d_context && _swap_chain)
	{
		if (!_index_buffer_staging.empty())
		{
			build_index_and_vertex_buffers();
		}

		draw_scene(_f->d3d_context);
	}
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void d3d11_draw_context_impl::add_scene_atom(const ComPtr<ID3D11Texture2D>& texture,
	const ComPtr<ID3D11PixelShader>& shader, ui::texture_format tex_fmt,
	ui::texture_sampler sampler, const vertex_2d* vertices,
	size_t vertex_count, const WORD* indexes, size_t index_count)
{
	df::scope_rendering_func rf(__FUNCTION__);
	auto combine_with_last_atom = false;

	if (!_scene_atoms.empty())
	{
		// optimise by extending exiting atom
		auto&& back = _scene_atoms.back();
		combine_with_last_atom = back.texture == texture.Get() && back.shader == shader.Get();
	}

	const auto vertex_pos = _vertex_buffer_staging.size();
	const auto index_pos = _index_buffer_staging.size();

	_vertex_buffer_staging.insert(_vertex_buffer_staging.end(), vertices, vertices + vertex_count);
	_index_buffer_staging.insert(_index_buffer_staging.end(), indexes, indexes + index_count);

	if (combine_with_last_atom)
	{
		const auto vc = _scene_atoms.back().vertex_count;

		_scene_atoms.back().vertex_count += static_cast<uint32_t>(vertex_count);
		_scene_atoms.back().index_count += static_cast<uint32_t>(index_count);

		for (auto i = 0u; i < index_count; i++)
		{
			_index_buffer_staging[index_pos + i] += static_cast<WORD>(vc);
		}
	}
	else
	{
		scene_atom sa = {
			texture.Get(), shader.Get(), tex_fmt, sampler,
			static_cast<uint32_t>(vertex_pos),
			static_cast<uint32_t>(vertex_count),
			static_cast<uint32_t>(index_pos),
			static_cast<uint32_t>(index_count)
		};

		_scene_atoms.emplace_back(sa);

		if (!_scene_textures.contains(texture))
		{
			_scene_textures.emplace(texture);
		}
	}
}

ID3D11PixelShader* d3d11_draw_context_impl::calc_shader(const bool is_bicubic, const ui::texture_format tex_fmt) const
{
	df::scope_rendering_func rf(__FUNCTION__);
	ID3D11PixelShader* shader = is_bicubic ? _pixel_shader_rgb_bicubic.Get() : _pixel_shader_rgb.Get();
	if (tex_fmt == ui::texture_format::NV12 || tex_fmt == ui::texture_format::P010)
		shader = is_bicubic
		? _pixel_shader_yuv_bicubic.Get()
		: _pixel_shader_yuv.Get();
	return shader;
}

void d3d11_draw_context_impl::draw_texture(const texture_d3d11_ptr& t, const recti dst, const recti src,
	const ui::color c, ui::texture_sampler sampler, float radius)
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());
	df::assert_true(t->_f->d3d_device == _f->d3d_device);

	const auto alpha = c.a;

	if (alpha >= 0.01f && t->_f->d3d_device == _f->d3d_device)
	{
		if (t)
		{
			const auto ex = static_cast<float>(_client_extent.cx);
			const auto ey = static_cast<float>(_client_extent.cy);
			const auto dl = dst.left / ex;
			const auto dt = dst.top / ey;
			const auto dr = dst.right / ex;
			const auto db = dst.bottom / ey;

			const auto dimensions = t->dimensions();
			const float w = static_cast<float>(dimensions.cx);
			const float h = static_cast<float>(dimensions.cy);

			const vertex_2d vertices[] =
			{
				vertex_2d(dl, dt, src.left / w, src.top / h, c, t->dimensions()),
				vertex_2d(dr, dt, src.right / w, src.top / h, c, t->dimensions()),
				vertex_2d(dr, db, src.right / w, src.bottom / h, c, t->dimensions()),
				vertex_2d(dl, db, src.left / w, src.bottom / h, c, t->dimensions()),
			};

			const WORD indexes[] = {

				0, 1, 2,
				3, 0, 2,
			};

			const auto tex_fmt = t->_format;
			const auto shader = calc_shader(sampler == ui::texture_sampler::bicubic, tex_fmt);
			add_scene_atom(t->_texture, shader, tex_fmt, sampler, vertices, std::size(vertices), indexes,
				std::size(indexes));
		}
	}
}

void d3d11_draw_context_impl::draw_texture(const texture_d3d11_ptr& t, const quadd& dst, const recti src,
	const ui::color c, ui::texture_sampler sampler)
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());
	df::assert_true(t->_f->d3d_device == _f->d3d_device);

	const auto alpha = c.a;

	if (alpha >= 0.01f && t->_f->d3d_device == _f->d3d_device)
	{
		if (t)
		{
			auto d = dst.scale(_client_extent);
			const auto dimensions = t->dimensions();
			const float w = static_cast<float>(dimensions.cx);
			const float h = static_cast<float>(dimensions.cy);

			const vertex_2d vertices[] =
			{
				vertex_2d(d[0], {src.left / w, src.top / h}, c, t->dimensions()),
				vertex_2d(d[1], {src.right / w, src.top / h}, c, t->dimensions()),
				vertex_2d(d[2], {src.right / w, src.bottom / h}, c, t->dimensions()),
				vertex_2d(d[3], {src.left / w, src.bottom / h}, c, t->dimensions()),
			};

			const WORD indexes[] = {

				0, 1, 2,
				3, 0, 2,
			};

			const auto tex_fmt = t->_format;
			const auto shader = calc_shader(sampler == ui::texture_sampler::bicubic, tex_fmt);
			add_scene_atom(t->_texture, shader, tex_fmt, sampler, vertices, std::size(vertices), indexes,
				std::size(indexes));
		}
	}
}


static recti Cr(int x, int y, int cx, int cy)
{
	return { x, y, x + cx, y + cy };
}

static rectd make_rectd(double x1, double y1, double x2, double y2)
{
	return { x1, y1, x2 - x1, y2 - y1 };
}


static void add_rect(vertex_2d*& vv, WORD*& ii, const rectd dst, const rectd src, float a, int vert_offset)
{
	vv->set(dst.top_left(), src.top_left(), a);
	vv++;
	vv->set(dst.top_right(), src.top_right(), a);
	vv++;
	vv->set(dst.bottom_right(), src.bottom_right(), a);
	vv++;
	vv->set(dst.bottom_left(), src.bottom_left(), a);
	vv++;

	*ii++ = vert_offset + 0;
	*ii++ = vert_offset + 1;
	*ii++ = vert_offset + 2;

	*ii++ = vert_offset + 0;
	*ii++ = vert_offset + 2;
	*ii++ = vert_offset + 3;
}

static void build_shadow_vertices(vertex_2d* vertices, WORD* indexes, const texture_d3d11_ptr& texture, const recti dst,
	const sizei client_extent, int sxy, float a)
{
	const auto tex_dims = texture->dimensions();
	const sized norm(tex_dims.cx * 2, tex_dims.cy * 2);
	//auto r = Core::inflate(rect, sxy);

	const auto ex = static_cast<float>(client_extent.cx);
	const auto ey = static_cast<float>(client_extent.cy);
	const auto xx = sxy / ex; // Core::Min(xy, (int)(size.Width / 2));
	const auto yy = sxy / ey; //Core::Min(xy, (int)(size.Height / 2));

	const auto r = rectd(dst).scale(client_extent);

	const auto tl = r.top_left();
	const auto tr = r.top_right();
	const auto br = r.bottom_right();
	const auto bl = r.bottom_left();

	auto* v = vertices;
	auto* i = indexes;

	// corners
	add_rect(v, i, make_rectd(tl.X - xx, tl.Y - yy, tl.X, tl.Y), rectd(0, 0, tex_dims.cx, tex_dims.cy).scale(norm), a,
		static_cast<uint32_t>(v - vertices));
	add_rect(v, i, make_rectd(bl.X - xx, bl.Y, bl.X, bl.Y + yy),
		rectd(0, tex_dims.cy, tex_dims.cx, tex_dims.cy).scale(norm), a, static_cast<uint32_t>(v - vertices));
	add_rect(v, i, make_rectd(tr.X, tr.Y - yy, tr.X + xx, tr.Y),
		rectd(tex_dims.cx, 0, tex_dims.cx, tex_dims.cy).scale(norm), a, static_cast<uint32_t>(v - vertices));
	add_rect(v, i, make_rectd(br.X, br.Y, br.X + xx, br.Y + yy),
		rectd(tex_dims.cx, tex_dims.cy, tex_dims.cx, tex_dims.cy).scale(norm), a,
		static_cast<uint32_t>(v - vertices));

	auto rt = r;
	rt.Y = r.top() - yy;
	rt.Height = yy;

	auto rb = r;
	rb.Y = r.bottom();
	rb.Height = yy;

	auto rl = r;
	rl.X = r.left() - xx;
	rl.Width = xx;

	auto rr = r;
	rr.X = r.right();
	rr.Width = xx;

	// horizontals
	add_rect(v, i, rt, rectd(tex_dims.cx - 1, 0, 2, tex_dims.cy).scale(norm), a, static_cast<uint32_t>(v - vertices));
	add_rect(v, i, rb, rectd(tex_dims.cx - 1, tex_dims.cy, 2, tex_dims.cy).scale(norm), a,
		static_cast<uint32_t>(v - vertices));

	// verticals
	add_rect(v, i, rl, rectd(0, tex_dims.cy - 1, tex_dims.cx, 2).scale(norm), a, static_cast<uint32_t>(v - vertices));
	add_rect(v, i, rr, rectd(tex_dims.cx, tex_dims.cy - 1, tex_dims.cx, 2).scale(norm), a,
		static_cast<uint32_t>(v - vertices));
}

void d3d11_vertices::update(recti rects[], ui::color colors[], const int num_bars)
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());

	const auto expected_num_bars = 64;
	const auto shadow_vertex_count = 4 * 8;
	const auto shadow_index_count = 6 * 8;
	const auto rect_verex_count = 5;
	const auto rect_index_count = 12;

	const auto visualizer_vertex_count = (shadow_vertex_count + rect_verex_count) * expected_num_bars;
	const auto visualizer_index_count = (shadow_index_count + rect_index_count) * expected_num_bars;

	if (num_bars != expected_num_bars)
		return;

	vertex_2d vertices[visualizer_vertex_count];
	WORD indexes[visualizer_index_count];
	auto vertex_count = 0;
	auto index_count = 0;

	for (auto i = 0; i < num_bars; i++)
	{
		auto r = rects[i];
		auto a = r.height() > 1 ? colors[i].a / 2.0f : 0.0f;

		build_shadow_vertices(vertices + vertex_count, indexes + index_count, _canvas->_shadow, r,
			_canvas->_client_extent, 8, a);

		for (int i = 0; i < shadow_index_count; i++)
		{
			indexes[index_count + i] += vertex_count;
		}

		vertex_count += shadow_vertex_count;
		index_count += shadow_index_count;
	}

	const auto bar_vert_start = vertex_count;
	const auto bar_ind_start = index_count;

	for (auto i = 0; i < num_bars; i++)
	{
		rectd dst = rects[i];
		auto clr2 = colors[i];
		auto clr1 = clr2.emphasize();

		const auto r = rectd(dst).scale(_canvas->_client_extent);

		const auto center = r.center();
		const auto dl = static_cast<float>(r.X);
		const auto dt = static_cast<float>(r.Y);
		const auto dr = static_cast<float>(r.right());
		const auto db = static_cast<float>(r.bottom());
		const auto cx = static_cast<float>(center.X);
		const auto cy = static_cast<float>(center.Y);

		const auto vi = vertex_count - bar_vert_start;

		vertices[vertex_count + 0] = vertex_2d(dl, dt, clr2);
		vertices[vertex_count + 1] = vertex_2d(dr, dt, clr2);
		vertices[vertex_count + 2] = vertex_2d(cx, cy, clr1);
		vertices[vertex_count + 3] = vertex_2d(dr, db, clr2);
		vertices[vertex_count + 4] = vertex_2d(dl, db, clr2);

		indexes[index_count++] = vi + 0;
		indexes[index_count++] = vi + 1;
		indexes[index_count++] = vi + 2;

		indexes[index_count++] = vi + 1;
		indexes[index_count++] = vi + 3;
		indexes[index_count++] = vi + 2;

		indexes[index_count++] = vi + 3;
		indexes[index_count++] = vi + 4;
		indexes[index_count++] = vi + 2;

		indexes[index_count++] = vi + 4;
		indexes[index_count++] = vi + 0;
		indexes[index_count++] = vi + 2;

		vertex_count += rect_verex_count;
		//index_count += rect_index_count;
	}

	if (!_vertex_buffer)
	{
		ComPtr<ID3D11Buffer> buffer;
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = sizeof(vertex_2d) * vertex_count;
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		bd.CPUAccessFlags = 0;

		D3D11_SUBRESOURCE_DATA source_data = {};
		source_data.pSysMem = vertices;

		auto hr = _canvas->_f->d3d_device->CreateBuffer(&bd, &source_data, &buffer);

		if (SUCCEEDED(hr))
		{
			_vertex_buffer = buffer;
			_canvas->_f->d3d_device = _canvas->_f->d3d_device;
		}
	}
	else
	{
		D3D11_BOX box{};
		box.left = 0;
		box.right = vertex_count * vertex_stride;
		box.top = 0;
		box.bottom = 1;
		box.front = 0;
		box.back = 1;

		_canvas->_f->d3d_context->UpdateSubresource(_vertex_buffer.Get(), 0, &box, vertices, 0, 0);
	}

	if (!_index_buffer)
	{
		ComPtr<ID3D11Buffer> buffer;
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = sizeof(WORD) * index_count;
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
		bd.CPUAccessFlags = 0;

		D3D11_SUBRESOURCE_DATA source_data = {};
		source_data.pSysMem = indexes;

		auto hr = _canvas->_f->d3d_device->CreateBuffer(&bd, &source_data, &buffer);

		if (SUCCEEDED(hr))
		{
			_index_buffer = buffer;
		}
	}
	else
	{
		D3D11_BOX box{};
		box.left = 0;
		box.right = sizeof(WORD) * index_count;
		box.top = 0;
		box.bottom = 1;
		box.front = 0;
		box.back = 1;

		_canvas->_f->d3d_context->UpdateSubresource(_index_buffer.Get(), 0, &box, indexes, 0, 0);
	}

	scene_atom shadow_atom = {
		_canvas->_shadow->_texture.Get(),
		_canvas->_pixel_shader_rgb.Get(),
		ui::texture_format::RGB,
		ui::texture_sampler::point,
		0,
		static_cast<uint32_t>(bar_vert_start),
		0,
		static_cast<uint32_t>(bar_ind_start)
	};


	scene_atom bar_atom = {
		nullptr,
		_canvas->_pixel_shader_solid.Get(),
		ui::texture_format::None,
		ui::texture_sampler::point,
		static_cast<uint32_t>(bar_vert_start),
		static_cast<uint32_t>(vertex_count - bar_vert_start),
		static_cast<uint32_t>(bar_ind_start),
		static_cast<uint32_t>(index_count - bar_ind_start)
	};

	_scene_atoms.clear();
	_scene_atoms.emplace_back(shadow_atom);
	_scene_atoms.emplace_back(bar_atom);

	//if (!update_only)
	//{
	//	if (rect_vertex_pos > 0)
	//	{
	//		//scene_atom shadow_atom = { _shadow._texture, _pixel_shader_rgb, _visualizer_vertex_buffer, 0, rect_vertex_pos };
	//		//_scene_atoms.emplace_back(shadow_atom);
	//	}

	//	auto rect_vertex_count = vertex_pos - rect_vertex_pos;

	//	if (rect_vertex_count > 0)
	//	{
	//		//scene_atom rects_atom = { nullptr, _pixel_shader_solid, _visualizer_vertex_buffer, rect_vertex_pos, rect_vertex_count };
	//		//_scene_atoms.emplace_back(rects_atom);
	//	}
	//}

	//if (vertex_mode == 0)
	//{
	//	D3D11_MAPPED_SUBRESOURCE mapped;
	//	ZeroMemory(&mapped, sizeof(D3D11_MAPPED_SUBRESOURCE));

	//	if (SUCCEEDED(_f->d3d_context->Map(_visualizer_vertex_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
	//	{
	//		memcpy(mapped.pData, vertices, vertex_pos * vertex_stride);
	//		_f->d3d_context->Unmap(_visualizer_vertex_buffer, 0);
	//	}
	//}
	//else if (vertex_mode == 1)
	//{
	//	D3D11_BOX box{};
	//	box.left = 0;
	//	box.right = vertex_pos * vertex_stride;
	//	box.top = 0;
	//	box.bottom = 1;
	//	box.front = 0;
	//	box.back = 1;

	//	_f->d3d_context->UpdateSubresource(_visualizer_vertex_buffer, 0, &box, reinterpret_cast<const uint8_t*>(vertices), 0, 0);
	//}
}


ui::texture_update_result d3d11_texture::update(const av_frame_ptr& frame_in)
{
	df::scope_rendering_func rf(__FUNCTION__);

	auto result = ui::texture_update_result::failed;
	auto info = av_get_d3d_info(frame_in);

	if (info.tex)
	{
		// https://stackoverflow.com/questions/56863430/how-to-copy-texture-from-context-to-another-context-inside-gpu
		// https://github.com/videolan/vlc/blob/fd72480dfdb3eb30cddb7a06cef60d6b5c29828d/doc/libvlc/d3d11_player.cpp
		const sizei src_extent = { (info.width), (info.height) };
		bool shared_texture_valid = false;
		auto* frames_ctx = info.ctx;
		auto device_hwctx = std::bit_cast<AVD3D11VADeviceContext*>(frames_ctx->device_ctx->hwctx);
		device_hwctx->lock(device_hwctx->lock_ctx);

		auto device = _f->d3d_device;
		auto context = _f->d3d_context;

		ComPtr<ID3D11Texture2D> video_texture = info.tex;
		const auto video_texture_index = info.tex_index;

		ComPtr<ID3D11Device> video_device;
		ComPtr<ID3D11DeviceContext> video_context;

		video_texture->GetDevice(&video_device);
		video_device->GetImmediateContext(&video_context);

		D3D11_TEXTURE2D_DESC tex_desc_src = {};
		video_texture->GetDesc(&tex_desc_src);

		sizei texture_extent = { static_cast<int>(tex_desc_src.Width), static_cast<int>(tex_desc_src.Height) };

		auto video_tex_format = tex_desc_src.Format == DXGI_FORMAT_P010
			? ui::texture_format::P010
			: ui::texture_format::NV12;

		if (video_texture_index >= tex_desc_src.ArraySize)
		{
			device_hwctx->unlock(device_hwctx->lock_ctx);
			return ui::texture_update_result::failed;
		}

		if (!_shared_texture || _shared_texture_dimensions != texture_extent || _shared_texture_format != video_tex_format)
		{
			ComPtr<ID3D11Texture2D> shared_texture;

			D3D11_TEXTURE2D_DESC texDesc = tex_desc_src;
			texDesc.BindFlags = D3D11_BIND_DECODER;
			texDesc.Usage = D3D11_USAGE_DEFAULT;
			texDesc.ArraySize = 1;
			texDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;

			auto hr = video_device->CreateTexture2D(&texDesc, nullptr, &shared_texture);

			if (SUCCEEDED(hr))
			{
				video_context->CopySubresourceRegion(shared_texture.Get(), 0, 0, 0, 0, video_texture.Get(),
					static_cast<uint32_t>(video_texture_index), nullptr);

				_shared_texture = shared_texture;
				_shared_texture_dimensions = texture_extent;
				_shared_texture_format = video_tex_format;

				shared_texture_valid = true;
			}
		}
		else if (_shared_texture)
		{
			video_context->CopySubresourceRegion(_shared_texture.Get(), 0, 0, 0, 0, video_texture.Get(),
				static_cast<uint32_t>(video_texture_index), nullptr);
			shared_texture_valid = true;
		}

		device_hwctx->unlock(device_hwctx->lock_ctx);

		if (shared_texture_valid)
		{
			video_context->Flush();

			ComPtr<IDXGIResource1> shared_resource;
			auto hr = _shared_texture->QueryInterface(__uuidof(IDXGIResource1),
				std::bit_cast<LPVOID*>(shared_resource.GetAddressOf()));

			if (SUCCEEDED(hr))
			{
				HANDLE shared_handle = nullptr;
				hr = shared_resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ, nullptr, &shared_handle);

				if (SUCCEEDED(hr))
				{
					ComPtr<ID3D11Device1> device1;
					hr = device->QueryInterface(__uuidof(ID3D11Device1),
						std::bit_cast<LPVOID*>(device1.GetAddressOf()));

					if (SUCCEEDED(hr))
					{
						ComPtr<ID3D11Texture2D> video_shared_texture;
						hr = device1->OpenSharedResource1(shared_handle, __uuidof(ID3D11Texture2D),
							std::bit_cast<void**>(video_shared_texture.GetAddressOf()));

						if (SUCCEEDED(hr))
						{
							if (!_texture || _dimensions != _shared_texture_dimensions || _format !=
								_shared_texture_format)
							{
								D3D11_TEXTURE2D_DESC tex_desc_src = {};
								video_shared_texture->GetDesc(&tex_desc_src);

								D3D11_TEXTURE2D_DESC texDesc2 = tex_desc_src;
								texDesc2.BindFlags = D3D11_BIND_SHADER_RESOURCE;
								texDesc2.ArraySize = 1;
								texDesc2.MiscFlags = 0;

								ComPtr<ID3D11Texture2D> texture2;
								hr = device->CreateTexture2D(&texDesc2, nullptr, &texture2);

								if (SUCCEEDED(hr))
								{
									context->CopyResource(texture2.Get(), video_shared_texture.Get());

									_texture = texture2;
									_dimensions = _shared_texture_dimensions;
									_src_extent = src_extent;
									_format = _shared_texture_format;
									_orientation = info.orientation;

									//context->Flush();
									result = ui::texture_update_result::tex_created;
								}
							}
							else if (_texture && _dimensions == _shared_texture_dimensions && _format ==
								_shared_texture_format)
							{
								context->CopyResource(_texture.Get(), video_shared_texture.Get());
								//context->Flush();
								result = ui::texture_update_result::tex_updated;
							}
						}
					}

					CloseHandle(shared_handle);
				}
			}
		}
	}
	else
	{
		if (!_scaler) _scaler = std::make_unique<av_scaler>();

		auto surface = std::make_shared<ui::surface>();
		if (_scaler->scale_surface(frame_in, surface))
		{
			result = update(surface);
		}
	}

	if (result != ui::texture_update_result::failed && _orientation != info.orientation)
	{
		_orientation = info.orientation;
	}

	return result;
}


sizei d3d11_draw_context_impl::measure_string(const std::u8string_view text, const sizei s, ui::style::font_face font,
	ui::style::text_style style)
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());
	return _font[font].measure_text(text, s, style);
}

int d3d11_draw_context_impl::line_height(ui::style::font_face font)
{
	df::scope_rendering_func rf(__FUNCTION__);
	return _font[font].line_height();
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void log_update_texture_crash(const ui::texture_format fmt)
{
	df::log(__FUNCTION__, str::format(u8"UpdateSubresource {} ****** crashed ******"sv, to_string(fmt)));
}

static HRESULT try_create_tex(ID3D11Device* pDevice, const D3D11_TEXTURE2D_DESC& desc, D3D11_SUBRESOURCE_DATA* p_source,
	ID3D11Texture2D** t)
{
	const auto is_yuv = desc.Format == DXGI_FORMAT_NV12 || desc.Format == DXGI_FORMAT_P010;

	__try
	{
		return pDevice->CreateTexture2D(&desc, p_source, t);
	}
	__except (is_yuv ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
	{
		setting.use_yuv = false;
	}

	return E_FAIL;
}

static HRESULT try_update_tex(ID3D11DeviceContext* context, ID3D11Texture2D* texture, const sizei texture_dimensions,
	const ui::texture_format fmt, const uint8_t* pixels, size_t stride, size_t buffer_size)
{
	const auto is_yuv = fmt == ui::texture_format::NV12 || fmt == ui::texture_format::P010;

	D3D11_BOX box;
	box.left = 0;
	box.top = 0;
	box.front = 0;
	box.right = texture_dimensions.cx;
	box.bottom = texture_dimensions.cy;
	box.back = 1;

	__try
	{
		context->UpdateSubresource(texture, 0, &box, pixels, static_cast<UINT>(stride), static_cast<UINT>(buffer_size));
		return S_OK;
	}
	__except (is_yuv ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
	{
		log_update_texture_crash(fmt);
		setting.use_yuv = false;
	}

	return E_FAIL;
}

ui::texture_update_result d3d11_texture::update(const sizei dims, const ui::texture_format fmt,
	const ui::orientation orientation, const uint8_t* pixels,
	size_t stride, size_t buffer_size)
{
	df::scope_rendering_func rf(__FUNCTION__);
	auto result = ui::texture_update_result::failed;

	df::assert_true(ui::is_ui_thread());
	// https://chromium.googlesource.com/chromium/src/gpu/+/master/config/gpu_driver_bug_list.json

	auto cx = dims.cx;
	auto cy = dims.cy;
	const auto is_yuv = fmt == ui::texture_format::NV12 || fmt == ui::texture_format::P010;

	if (is_yuv)
	{
		const auto has_valid_dims = (cx == (cx & ~1)) && (cy == (cy & ~1)) && cx >= 2 && cy >= 2;

		df::assert_true(has_valid_dims);

		if (!has_valid_dims)
		{
			return result;
		}
	}

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = cx;
	desc.Height = cy;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = to_format(fmt);
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = 0;
	desc.MiscFlags = 0;

	D3D11_SUBRESOURCE_DATA source_data = {};
	source_data.pSysMem = pixels;
	source_data.SysMemPitch = static_cast<UINT>(stride);
	source_data.SysMemSlicePitch = static_cast<UINT>(buffer_size);

	D3D11_SUBRESOURCE_DATA* p_source = nullptr;

	if (pixels && stride)
	{
		p_source = &source_data;
	}

	if (!_texture || _format != fmt || dims != _dimensions)
	{
		ComPtr<ID3D11Texture2D> t;
		const auto hr = try_create_tex(_f->d3d_device.Get(), desc, p_source, &t);

		if (SUCCEEDED(hr))
		{
			_texture = t;
			_dimensions = { cx, cy };
			_format = fmt;
			result = SUCCEEDED(hr) ? ui::texture_update_result::tex_created : ui::texture_update_result::failed;
		}
		else
		{
			if (hr == E_FAIL)
			{
				df::log(__FUNCTION__, str::format(u8"CreateTexture2D {} ({} x {}) ****** crashed ******"sv,
					to_string(fmt), cx, cy, hr));
			}
			else
			{
				df::log(__FUNCTION__,
					str::format(u8"CreateTexture2D {} ({} x {}) failed: {:x}"sv, to_string(fmt), cx, cy, hr));
			}
		}
	}
	else
	{
		const auto hr = SUCCEEDED(
			try_update_tex(_f->d3d_context.Get(), _texture.Get(), _dimensions, _format, pixels, stride, buffer_size));
		result = SUCCEEDED(hr) ? ui::texture_update_result::tex_updated : ui::texture_update_result::failed;
	}

	if (result != ui::texture_update_result::failed && _orientation != orientation)
	{
		_orientation = orientation;
	}

	return result;
}

ui::texture_update_result d3d11_texture::update(const ui::const_surface_ptr& s)
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());

	if (ui::is_valid(s))
	{
		const auto fmt = s->format();
		return update(s->dimensions(), fmt, s->orientation(), s->pixels(), s->stride(), s->size());
	}

	return ui::texture_update_result::failed;
}

void d3d11_text_renderer::create_a8_texture(const int xy)
{
	df::scope_rendering_func rf(__FUNCTION__);

	if (_f->d3d_device)
	{
		ComPtr<ID3D11Texture2D> t;

		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = xy;
		desc.Height = xy;
		desc.MipLevels = desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = 0;

		const auto hr = _f->d3d_device->CreateTexture2D(&desc, nullptr, &t);

		if (SUCCEEDED(hr))
		{
			_texture = t;
			_xy_tex = xy;
			_next_location.x = 0;
			_next_location.y = 0;
		}
	}
}

d3d11_text_renderer::coords d3d11_text_renderer::find_glyph(const uint16_t c, const DWRITE_GLYPH_RUN* glyph_run)
{
	df::scope_rendering_func rf(__FUNCTION__);
	coords result = { 0, 0, 0, 0, 0 };

	if (!_texture)
	{
		create_a8_texture(256);
	}

	if (!_texture)
	{
		return result;
	}

	const auto index = glyph_run->fontFace->GetGlyphCount();
	const auto key = (index << 16) | c;
	const auto found = _coords.find(key);

	if (found != _coords.cend())
	{
		result = found->second;
	}
	else
	{
		const auto alpha_pixels = _font->render_glyph(c, _spacing, glyph_run);

		if (!alpha_pixels.is_empty())
		{
			const auto cx = alpha_pixels.cx;
			const auto cy = alpha_pixels.cy;

			if ((_next_location.x + cx) > static_cast<int>(_xy_tex))
			{
				_next_location.x = 0;
				_next_location.y += _line_height;
			}

			if ((_next_location.y + _line_height) > static_cast<int>(_xy_tex)) // Out of room
			{
				_coords.clear();
				_texture = nullptr;
				create_a8_texture(_xy_tex * 2);
			}

			if (!_texture)
			{
				return result;
			}

			const auto x = static_cast<uint32_t>(_next_location.x);
			const auto y = static_cast<uint32_t>(_next_location.y);

			D3D11_BOX box = {};
			box.left = x;
			box.top = y;
			box.front = 0;
			box.right = x + alpha_pixels.cx;
			box.bottom = y + alpha_pixels.cy;
			box.back = 1;

			_f->d3d_context->UpdateSubresource(_texture.Get(), 0, &box, alpha_pixels.pixels.data(), alpha_pixels.cx, 0);

			const coords glyph_bounds = {
				x,
				y,
				x + cx,
				y + cy,
				alpha_pixels.x
			};

			_coords[key] = result = glyph_bounds;
			_next_location.x += cx;
		}
		else
		{
			_coords[key] = result = {};
		}
	}

	return result;
}

void d3d11_text_renderer::reset(const std::shared_ptr<d3d11_draw_context_impl>& c, const factories_ptr& f,
	font_renderer_ptr fr)
{
	df::scope_rendering_func rf(__FUNCTION__);
	reset();

	_f = f;
	_canvas = c;
	_font = std::move(fr);
	_line_height = _font->calc_line_height();
	_base_line_height = _font->calc_base_line_height();
	_spacing = 2;
}

void d3d11_text_renderer::reset()
{
	df::scope_rendering_func rf(__FUNCTION__);
	_coords.clear();
	_chars_to_glyphs.clear();
	_texture.Reset();
	_spacing = 0;
	_xy_tex = 0;
	_line_height = 0;
	_base_line_height = 0;
	_next_location.x = 0;
	_next_location.y = 0;
}

void d3d11_text_renderer::draw_text(const std::u8string_view text, const recti bounds,
	const ui::style::text_style style, const ui::color c, const ui::color bg)
{
	df::scope_rendering_func rf(__FUNCTION__);
	_clr = c;

	if (_font)
	{
		_font->draw(std::bit_cast<ui::draw_context*>(_canvas.get()), this, str::utf8_to_utf16(text), bounds, style, c,
			bg, {});
	}
}

void d3d11_text_renderer::draw_text(const std::u8string_view text, const std::vector<ui::text_highlight_t>& highlights,
	const recti bounds, const ui::style::text_style style, const ui::color clr,
	const ui::color bg)
{
	df::scope_rendering_func rf(__FUNCTION__);
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

	if (_font)
	{
		_font->draw(std::bit_cast<ui::draw_context*>(_canvas.get()), this, w, bounds, style, clr, bg, _highlights);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////


HRESULT d3d11_text_renderer::GetPixelsPerDip(void* clientDrawingContext, FLOAT* pixelsPerDip)
{
	/*if (_canvas)
	{
		*pixelsPerDip = 96.0 * _canvas->scale_factor;
	}
	else*/
	{
		*pixelsPerDip = 96.0;
	}

	return S_OK;
}

HRESULT d3d11_text_renderer::DrawGlyphRun(void* clientDrawingContext, FLOAT baselineOriginX, FLOAT baselineOriginY,
	DWRITE_MEASURING_MODE measuringMode, const DWRITE_GLYPH_RUN* glyphRun,
	const DWRITE_GLYPH_RUN_DESCRIPTION* glyphRunDescription,
	IUnknown* clientDrawingEffect)
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());

	if (glyphRun)
	{
		auto client_extent = _canvas->client_extent();
		auto char_pos = glyphRunDescription ? glyphRunDescription->textPosition : 0;
		auto i_highlights = _highlights.begin();
		//auto transparent = clr.aa(0.0f);

		pointd screen_scale(client_extent.cx, client_extent.cy);
		pointd tex_scale(_xy_tex, _xy_tex);

		vertex_2d vertices[max_vert_count];
		WORD indexes[max_index_count];

		const auto vert_limit = max_vert_count - 4;
		const auto index_limit = max_index_count - 6;

		auto vertex_count = 0;
		auto index_count = 0;
		auto ty = floor(baselineOriginY + 0.5f);
		auto limit_char = -1;
		auto tx = 0.0f; // -_spacing;
		const auto is_left_to_right = (glyphRun->bidiLevel & 0x01) == 0;
		//const int cx = xend - xbegin;

		auto len = glyphRun->glyphCount;
		auto t = _texture;

		for (auto i = 0u; i < len; ++i)
		{
			const auto c = glyphRun->glyphIndices[i];
			const auto ax = glyphRun->glyphAdvances[i];
			const auto coord = find_glyph(c, glyphRun);
			auto sx = tx;
			auto sy = ty - _base_line_height; // coord.x_offset;

			if (glyphRun->glyphOffsets)
			{
				sx += glyphRun->glyphOffsets[i].advanceOffset;
				sy += glyphRun->glyphOffsets[i].ascenderOffset;
			}

			if (is_left_to_right)
			{
				sx = baselineOriginX + sx - _spacing;
			}
			else
			{
				sx = baselineOriginX - sx - ax - _spacing;
			}

			auto tx1 = coord.tx1;
			auto ty1 = coord.ty1;
			auto tx2 = coord.tx2;
			auto ty2 = coord.ty2;

			auto w = tx2 - tx1;
			auto h = ty2 - ty1;

			if (c != L' ')
			{
				pointd tl(sx, sy);
				pointd tr(sx + w, sy);
				pointd bl(sx, sy + h);
				pointd br(sx + w, sy + h);

				auto clr = _clr;

				if (i_highlights != _highlights.end())
				{
					const auto begin = i_highlights->offset;
					const auto end = begin + i_highlights->length;

					if (char_pos >= begin && char_pos < end)
					{
						clr = i_highlights->clr;
					}

					if (char_pos >= end - 1u)
					{
						++i_highlights;
					}
				}

				auto left_color = clr;
				auto right_color = (i == limit_char) ? ui::color{} : clr;
				auto pos = vertex_count;

				vertices[vertex_count++].set(bl / screen_scale, pointd(tx1, ty2) / tex_scale, left_color);
				vertices[vertex_count++].set(tl / screen_scale, pointd(tx1, ty1) / tex_scale, left_color);
				vertices[vertex_count++].set(br / screen_scale, pointd(tx2, ty2) / tex_scale, right_color);
				vertices[vertex_count++].set(tr / screen_scale, pointd(tx2, ty1) / tex_scale, right_color);

				indexes[index_count++] = pos + 0;
				indexes[index_count++] = pos + 1;
				indexes[index_count++] = pos + 2;
				indexes[index_count++] = pos + 3;
				indexes[index_count++] = pos + 2;
				indexes[index_count++] = pos + 1;

				if (index_count > index_limit || i == len - 1 || t != _texture)
				{
					if (_texture)
					{
						_canvas->add_scene_atom(_texture, _canvas->_pixel_shader_font, ui::texture_format::RGB,
							ui::texture_sampler::point, vertices, vertex_count, indexes,
							index_count);
					}

					vertex_count = 0;
					index_count = 0;

					if (t != _texture)
					{
						t = _texture;
					}
				}
			}

			tx += ax;
			char_pos += 1;
		}
	}

	return S_OK;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////

void d3d11_draw_context_impl::draw_shadow(const recti dst, const int sxy, const float alpha, const bool inverse)
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());

	vertex_2d vertices[4 * 8];
	WORD indexes[6 * 8];
	const auto& texture = inverse ? _inverse_shadow : _shadow;

	if (texture)
	{
		build_shadow_vertices(vertices, indexes, texture, dst, _client_extent, sxy, alpha);
		add_scene_atom(texture->_texture, _pixel_shader_rgb, ui::texture_format::RGB, ui::texture_sampler::point,
			vertices, std::size(vertices), indexes, std::size(indexes));
	}
}

void d3d11_draw_context_impl::draw_text(const std::u8string_view textA, const recti bounds,
	const ui::style::font_face font, const ui::style::text_style style,
	const ui::color clr, const ui::color bg)
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());

	if (clr.a >= 0.01f && _clip_bounds.intersects(bounds))
	{
		_font[font].draw_text(textA, bounds, style, clr, bg);
	}
}

void d3d11_draw_context_impl::draw_text(const std::u8string_view text,
	const std::vector<ui::text_highlight_t>& highlights, const recti bounds,
	ui::style::font_face font, ui::style::text_style style, const ui::color clr,
	const ui::color bg)
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());

	if (_clip_bounds.intersects(bounds))
	{
		_font[font].draw_text(text, highlights, bounds, style, clr, {});
	}
}

void d3d11_draw_context_impl::draw_text(const ui::text_layout_ptr& tl, const recti bounds, const ui::color clr,
	const ui::color bg)
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());

	if (_clip_bounds.intersects(bounds))
	{
		const auto t = std::dynamic_pointer_cast<text_layout_impl>(tl);
		_font[t->_font].draw_text(t, bounds, clr, bg);
	}
}

void d3d11_draw_context_impl::draw_edge_shadows(const float alpha)
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());
	const auto client_extent = _client_extent;
	const auto size = std::min(std::min(client_extent.cx / 2, client_extent.cy / 2), 96);
	draw_shadow(recti(0, 0, client_extent.cx, client_extent.cy).inflate(-size), size, alpha, true);
}


void d3d11_draw_context_impl::draw_border(const recti inside, const recti outside, const ui::color c_inside,
	const ui::color c_outside)
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());

	if (c_inside.a >= 0.01f || c_outside.a >= 0.01f)
	{
		const auto ex = static_cast<float>(_client_extent.cx);
		const auto ey = static_cast<float>(_client_extent.cy);

		const auto ol = outside.left / ex;
		const auto ob = outside.bottom / ey;
		const auto ot = outside.top / ey;
		const auto il = inside.left / ex;
		const auto ib = inside.bottom / ey;
		const auto it = inside.top / ey;
		const auto or1 = outside.right / ex;
		const auto ir = inside.right / ex;

		const vertex_2d vertices[] = {

			vertex_2d(ol, ob, c_outside),
			vertex_2d(ol, ot, c_outside),
			vertex_2d(il, ib, c_inside),
			vertex_2d(il, it, c_inside),
			vertex_2d(or1, ot, c_outside),
			vertex_2d(ir, it, c_inside),
			vertex_2d(or1, ob, c_outside),
			vertex_2d(ir, ib, c_inside),
		};

		const WORD indexes[] = {

			0, 1, 2,
			2, 1, 3,
			1, 4, 3,
			3, 4, 5,
			4, 6, 5,
			5, 6, 7,
			6, 0, 7,
			7, 0, 2,
		};

		add_scene_atom(nullptr, _pixel_shader_solid, ui::texture_format::None, ui::texture_sampler::point, vertices,
			std::size(vertices), indexes, std::size(indexes));
	}
}

recti d3d11_draw_context_impl::clip_bounds() const
{
	return _clip_bounds;
}

void d3d11_draw_context_impl::clip_bounds(const recti)
{
	// nop	
}

void d3d11_draw_context_impl::restore_clip()
{
	// nop
}

void d3d11_draw_context_impl::clear(const ui::color c)
{
	df::scope_rendering_func rf(__FUNCTION__);
	draw_rect(_client_extent, c);
}

void d3d11_draw_context_impl::draw_rounded_rect(const recti bounds_in, const ui::color c, const int radius)
{
	df::scope_rendering_func rf(__FUNCTION__);
	const auto c_outside = c;
	const auto c_inside = c.emphasize();

	if (c_outside.a >= 0.01f || c_inside.a >= 0.01f)
	{
		const auto ex = static_cast<float>(_client_extent.cx);
		const auto ey = static_cast<float>(_client_extent.cy);

		const auto bounds = bounds_in.inflate(2);
		const auto inside = bounds_in.inflate(std::max(-bounds_in.width(), -radius));

		const auto ol = bounds.left / ex;
		const auto ob = bounds.bottom / ey;
		const auto ot = bounds.top / ey;
		const auto il = inside.left / ex;
		const auto ib = inside.bottom / ey;
		const auto it = inside.top / ey;
		const auto or1 = bounds.right / ex;
		const auto ir = inside.right / ex;

		const vertex_2d vertices[] = {

			vertex_2d(ol, ot, -1.0f, -1.0f, c_outside),
			vertex_2d(il, ot, 0.0f, -1.0f, c_inside),
			vertex_2d(il, it, 0.0f, 0.0f, c_inside),
			vertex_2d(ol, it, -1.0f, 0.0f, c_inside),

			vertex_2d(ir, ot, 0.0f, -1.0f, c_inside),
			vertex_2d(or1, ot, 1.0f, -1.0f, c_outside),
			vertex_2d(or1, it, 1.0f, 0.0f, c_inside),
			vertex_2d(ir, it, 0.0f, 0.0f, c_inside),

			vertex_2d(ir, ib, 0.0f, 0.0f, c_inside),
			vertex_2d(or1, ib, 1.0f, 0.0f, c_inside),
			vertex_2d(or1, ob, 1.0f, 1.0f, c_outside),
			vertex_2d(ir, ob, 0.0f, 1.0f, c_inside),

			vertex_2d(ol, ib, -1.0f, 0.0f, c_inside),
			vertex_2d(il, ib, 0.0f, 0.0f, c_inside),
			vertex_2d(il, ob, 0.0f, 1.0f, c_inside),
			vertex_2d(ol, ob, -1.0f, 1.0f, c_outside),
		};

		constexpr WORD indexes[] = {

			0, 1, 2,
			2, 3, 0,

			1, 4, 7,
			7, 2, 1,

			4, 5, 6,
			6, 7, 4,

			7, 6, 9,
			9, 8, 7,

			8, 9, 10,
			10, 11, 8,

			13, 8, 11,
			11, 14, 13,

			12, 13, 14,
			14, 15, 12,

			3, 2, 13,
			13, 12, 3,

			2, 7, 8,
			8, 13, 2,
		};

		add_scene_atom(nullptr, _pixel_shader_circle, ui::texture_format::None, ui::texture_sampler::point, vertices,
			std::size(vertices), indexes, std::size(indexes));
	}
}

void d3d11_draw_context_impl::draw_rect(const recti bounds, const ui::color c)
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());

	const auto clr1 = c;
	const auto clr2 = c.emphasize();
	const auto dst = static_cast<quadd>(bounds);

	if (clr1.a >= 0.01f || clr2.a >= 0.01f)
	{
		const auto r = dst.scale(_client_extent);
		const auto center = r.center_point();

		const vertex_2d vertices[] = {

			vertex_2d(r[0], clr2),
			vertex_2d(r[1], clr2),
			vertex_2d(center, clr1),
			vertex_2d(r[2], clr2),
			vertex_2d(r[3], clr2),
		};

		constexpr WORD indexes[] = {

			0, 1, 2,
			1, 3, 2,
			3, 4, 2,
			4, 0, 2
		};

		add_scene_atom(nullptr, _pixel_shader_solid, ui::texture_format::None, ui::texture_sampler::point, vertices,
			std::size(vertices), indexes, std::size(indexes));
	}
}

void d3d11_draw_context_impl::draw_texture(const ui::texture_ptr& t, const recti dst, const float alpha,
	const ui::texture_sampler sampler)
{
	draw_texture(t, dst, recti(pointi(0, 0), t->dimensions()), alpha, sampler);
}

void d3d11_draw_context_impl::draw_texture(const ui::texture_ptr& t, const quadd& dst, const recti src,
	const float alpha, const ui::texture_sampler sampler)
{
	const auto tt = std::dynamic_pointer_cast<d3d11_texture>(t);
	draw_texture(tt, dst, src, ui::color::from_a(alpha), sampler);
}

void d3d11_draw_context_impl::draw_texture(const ui::texture_ptr& t, const recti dst, const recti src,
	const float alpha, const ui::texture_sampler sampler, const float radius)
{
	const auto tt = std::dynamic_pointer_cast<d3d11_texture>(t);
	draw_texture(tt, dst, src, ui::color::from_a(alpha), sampler, radius);
}

void d3d11_draw_context_impl::draw_vertices(const ui::vertices_ptr& v)
{
	df::scope_rendering_func rf(__FUNCTION__);
	const auto vv = std::dynamic_pointer_cast<d3d11_vertices>(v);

	if (vv && vv->_vertex_buffer)
	{
		df::assert_true(vv->_canvas->_f->d3d_device == _f->d3d_device);

		if (vv->_canvas->_f->d3d_device == _f->d3d_device)
		{
			scene_atom sa;
			sa.verts = vv;
			_scene_atoms.emplace_back(sa);
		}
	}
}

sizei d3d11_draw_context_impl::measure_text(const std::u8string_view text, const ui::style::font_face font,
	const ui::style::text_style style, const int width, const int height)
{
	return measure_string(text, { width, height }, font, style);
}

int d3d11_draw_context_impl::text_line_height(const ui::style::font_face font)
{
	return line_height(font);
}

ui::texture_ptr d3d11_draw_context_impl::create_texture()
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());
	return std::make_shared<d3d11_texture>(_f);
}

ui::vertices_ptr d3d11_draw_context_impl::create_vertices()
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());
	return std::make_shared<d3d11_vertices>(shared_from_this());
}

font_renderer_ptr d3d11_draw_context_impl::find_font(const ui::style::font_face font)
{
	const auto found = _font.find(font);
	if (found != _font.end()) return found->second.font();
	return _f->font_face(font, _base_font_size);
}

ui::text_layout_ptr d3d11_draw_context_impl::create_text_layout(const ui::style::font_face font)
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());
	return std::make_shared<text_layout_impl>(find_font(font), font);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////


draw_context_device_ptr d3d11_create_context(const factories_ptr& f, const ComPtr<IDXGISwapChain>& swap_chain,
	const int base_font_size)
{
	df::scope_rendering_func rf(__FUNCTION__);
	auto result = std::make_shared<d3d11_draw_context_impl>();
	result->create(f, swap_chain, base_font_size, result->can_use_gpu());
	return result->is_valid() ? result : nullptr;
}
