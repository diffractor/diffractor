// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Direct3D 11 rendering backend. Implements GPU-accelerated texture rendering,
// shader management, and hardware video decoding support.

#include "pch.h"

#include "platform_win.h"
#include "util_geometry.h"
#include "files.h"

#include <versionhelpers.h>

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

// Animation is a rendering capability, so the setting and the ui gate consulted by
// animate_alpha must always agree. The CPU software backend cannot afford per-frame fades.
static void set_can_animate(const bool can_animate)
{
	setting.can_animate = can_animate;
	ui::animations_enabled = can_animate;
}

static constexpr std::string_view to_string(const D3D_FEATURE_LEVEL fl)
{
	switch (fl)
	{
	case D3D_FEATURE_LEVEL_1_0_CORE: return "1.0.CORE";
	case D3D_FEATURE_LEVEL_9_1: return "9.1";
	case D3D_FEATURE_LEVEL_9_2: return "9.2";
	case D3D_FEATURE_LEVEL_9_3: return "9.3";
	case D3D_FEATURE_LEVEL_10_0: return "10.0";
	case D3D_FEATURE_LEVEL_10_1: return "10.1";
	case D3D_FEATURE_LEVEL_11_0: return "11.0";
	case D3D_FEATURE_LEVEL_11_1: return "11.1";
	case D3D_FEATURE_LEVEL_12_0: return "12.0";
	case D3D_FEATURE_LEVEL_12_1: return "12.1";
	default:
		break;
	}

	return "?";
}

// Largest texture edge the runtime accepts at a feature level. Exceeding it fails CreateTexture2D
// outright, so it is a hard clamp on the decode size rather than a tuning choice.
static constexpr int texture_dimension_limit(const D3D_FEATURE_LEVEL fl)
{
	if (fl >= D3D_FEATURE_LEVEL_11_0) return D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
	if (fl >= D3D_FEATURE_LEVEL_10_0) return 8192;
	return D3D_FL9_3_REQ_TEXTURE2D_U_OR_V_DIMENSION;
}

// Publishes what one decoded image may cost. Both budgets only ever tighten the fixed ceilings the
// app shipped with, so a large machine behaves exactly as before and a small one refuses earlier
// instead of thrashing or failing the upload.
//
// vram_bytes is 0 when there is no GPU to ask, which leaves system memory as the only constraint.
static void publish_image_budgets(const D3D_FEATURE_LEVEL fl, const uint64_t vram_bytes)
{
	constexpr int64_t texture_ceiling = 128ll * 1024ll * 1024ll; // 32 megapixels, the historical fixed cap
	constexpr int64_t decode_ceiling = 2048ll * 1024ll * 1024ll;
	constexpr int64_t floor_bytes = 64ll * 1024ll * 1024ll;

	MEMORYSTATUSEX mem = {};
	mem.dwLength = sizeof(mem);
	const auto total_phys = GlobalMemoryStatusEx(&mem) ? static_cast<int64_t>(mem.ullTotalPhys) : 0;

	// A displayed image is one of several textures live at once (the compared image, its fade-out,
	// thumbnails, the glyph atlas, map tiles), so it gets a fraction of the card rather than the lot.
	auto texture_bytes = texture_ceiling;
	if (vram_bytes > 0) texture_bytes = std::min(texture_bytes, static_cast<int64_t>(vram_bytes / 8));
	if (total_phys > 0) texture_bytes = std::min(texture_bytes, total_phys / 16);

	// Bounds the transient full-resolution frame a codec must materialise before anything can be
	// scaled down. Total rather than available memory, so the same file behaves the same way twice.
	auto decode_bytes = decode_ceiling;
	if (total_phys > 0) decode_bytes = std::min(decode_bytes, total_phys / 8);

	df::max_texture_dimension = texture_dimension_limit(fl);
	df::max_texture_bytes = std::max(floor_bytes, texture_bytes);
	df::max_decode_bytes = std::max(floor_bytes, decode_bytes);

	df::log(__FUNCTION__, std::format("image budget: {} px edge, texture {}, decode {}",
	                                  df::max_texture_dimension, df::file_size(df::max_texture_bytes).str(),
	                                  df::file_size(df::max_decode_bytes).str()));
}

void factories::reset_fonts()
{
	font_collection.Reset();
	font_renderers.clear();
}

bool factories::init(const bool use_gpu)
{
	df::scope_rendering_func rf(__FUNCTION__);

	// DirectWrite is the only hard requirement here: every backend, the CPU software one included,
	// rasterises text through it, so there is no rendering path left if it is missing. DXGI and WIC
	// are not needed to put a window on screen, so each degrades on its own rather than stopping the
	// app - the whole point of this function is that it either starts something or says why.
	auto hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(dwrite),
	                              std::bit_cast<IUnknown**>(dwrite.GetAddressOf()));

	if (FAILED(hr))
	{
		df::log(__FUNCTION__, std::format("Failed to create IDWriteFactory {:x}", static_cast<uint32_t>(hr)));
		return false;
	}

	{
		HRESULT dxgi_hr;
#ifdef _DEBUG
		// The DXGI debug layer ships in the optional "Graphics Tools" feature. Without it the call
		// fails with DXGI_ERROR_SDK_COMPONENT_MISSING, so fall back to a plain factory instead of
		// treating a missing developer component as a fatal startup error.
		dxgi_hr = CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, __uuidof(dxgi),
		                             std::bit_cast<void**>(dxgi.GetAddressOf()));

		if (FAILED(dxgi_hr))
		{
			df::log(__FUNCTION__, "DXGI debug layer unavailable - creating factory without it");
			dxgi_hr = CreateDXGIFactory1(__uuidof(dxgi), std::bit_cast<void**>(dxgi.GetAddressOf()));
		}
#else
		dxgi_hr = CreateDXGIFactory1(__uuidof(dxgi), std::bit_cast<void**>(dxgi.GetAddressOf()));
#endif

		if (FAILED(dxgi_hr))
		{
			// No DXGI means no swap chain, so the Direct3D path is unreachable anyway; the CPU
			// backend presents through GDI and never touches it.
			df::log(__FUNCTION__, std::format("Failed to create IDXGIFactory {:x} - using CPU software rendering",
			                                  static_cast<uint32_t>(dxgi_hr)));
			dxgi.Reset();
		}
	}

	{
		const auto wic_hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
		                                     __uuidof(IWICImagingFactory),
		                                     std::bit_cast<void**>(wic.GetAddressOf()));

		if (FAILED(wic_hr))
		{
			// WIC decodes shell thumbnails, clipboard bitmaps and the shadow art. Losing it costs
			// those, not the window, so callers test factories::wic instead of the app refusing to run.
			df::log(__FUNCTION__, std::format("Failed to create IWICImagingFactory {:x} - image decoding degraded",
			                                  static_cast<uint32_t>(wic_hr)));
			wic.Reset();
		}
	}

	set_can_animate(should_animate());

	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;

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
		};

		constexpr D3D_FEATURE_LEVEL feature_levels_11[] =
		{
			D3D_FEATURE_LEVEL_11_0,
			D3D_FEATURE_LEVEL_10_1,
			D3D_FEATURE_LEVEL_10_0,
		};

		constexpr auto driver_type = D3D_DRIVER_TYPE_HARDWARE;

		// Without a DXGI factory there is nothing to create a swap chain from, so a device would
		// have no way to reach the screen.
		if (use_gpu && dxgi)
		{
			// Mark GPU rendering as active before creating the device so a crash during
			// device creation or subsequent rendering is attributed to the GPU on the next
			// launch (apply_gpu_crash_guard). A recovery session retains the marker until
			// clean shutdown; other software fallbacks clear it immediately.
			platform::set_crash_guard(platform::crash_guard::gpu_render, true);
			df::log(__FUNCTION__, "GPU rendering enabled - creating Direct3D 11 device");

			// Use the default adapter (nullptr) with D3D_DRIVER_TYPE_HARDWARE for the broadest
			// compatibility - the runtime selects the primary hardware device.
			hr = D3D11CreateDevice(nullptr, driver_type, nullptr, create_device_flags, feature_levels_11_1,
			                       std::size(feature_levels_11_1), D3D11_SDK_VERSION, &device,
			                       &feature_level, &context);

			if (hr == E_INVALIDARG)
			{
				df::log(__FUNCTION__, "D3D11CreateDevice failed with 11_1 - trying 11");
				hr = D3D11CreateDevice(nullptr, driver_type, nullptr, create_device_flags,
				                       feature_levels_11, std::size(feature_levels_11),
				                       D3D11_SDK_VERSION, &device, &feature_level, &context);
			}

			if (FAILED(hr) && (create_device_flags & D3D11_CREATE_DEVICE_DEBUG) != 0)
			{
				// The D3D11 debug layer is part of the optional "Graphics Tools" feature; a Debug
				// build must not silently drop to software rendering just because it is absent.
				df::log(__FUNCTION__, "D3D11 debug layer unavailable - retrying without it");
				create_device_flags &= ~static_cast<uint32_t>(D3D11_CREATE_DEVICE_DEBUG);

				hr = D3D11CreateDevice(nullptr, driver_type, nullptr, create_device_flags, feature_levels_11_1,
				                       std::size(feature_levels_11_1), D3D11_SDK_VERSION, &device,
				                       &feature_level, &context);

				if (hr == E_INVALIDARG)
				{
					hr = D3D11CreateDevice(nullptr, driver_type, nullptr, create_device_flags,
					                       feature_levels_11, std::size(feature_levels_11),
					                       D3D11_SDK_VERSION, &device, &feature_level, &context);
				}
			}
		}

		if (FAILED(hr) || !use_gpu || !device || !context)
		{
			// Hardware Direct3D 11 is unavailable (or disabled). Rather than falling back to the
			// WARP software rasterizer, run with the CPU software rendering backend. Leave the
			// D3D/DXGI device objects null and mark software_mode; frames use software_draw_context.
			if (!platform::crash_guard_suppressed(platform::crash_guard::gpu_render))
			{
				platform::set_crash_guard(platform::crash_guard::gpu_render, false);
			}
			df::log(__FUNCTION__, "D3D11 hardware unavailable - using CPU software rendering");
			software_mode = true;
			set_can_animate(false);
			device.Reset();
			context.Reset();
			hr = S_OK;
		}
	}

	if (SUCCEEDED(hr) && device)
	{
		// A device that cannot expose IDXGIDevice cannot drive a swap chain; treat that as
		// "no usable GPU" and run on the CPU backend rather than failing to start.
		if (FAILED(device.As(&dxgi_device)))
		{
			df::log(__FUNCTION__, "IDXGIDevice unavailable - using CPU software rendering");
			platform::set_crash_guard(platform::crash_guard::gpu_render, false);
			software_mode = true;
			set_can_animate(false);
			dxgi_device.Reset();
			device.Reset();
			context.Reset();
		}
	}

	if (SUCCEEDED(hr) && device)
	{
		ComPtr<IDXGIDevice1> dxgi_device1;

		if (SUCCEEDED(dxgi_device.As(&dxgi_device1)))
		{
			dxgi_device1->SetMaximumFrameLatency(1);
		}
	}

	if (SUCCEEDED(hr) && device)
	{
		d3d_device = device;
		d3d_context = context;
		d3d_feature_level = feature_level;

		df::d3d_info = to_string(feature_level);

		ComPtr<IDXGIAdapter> adapter;
		uint64_t vram_bytes = 0;

		if (SUCCEEDED(dxgi_device->GetAdapter(&adapter)))
		{
			DXGI_ADAPTER_DESC adapter_desc;

			if (SUCCEEDED(adapter->GetDesc(&adapter_desc)))
			{
				const auto description = str::utf16_to_utf8(adapter_desc.Description);
				const auto gpu_id = std::format("{:x}|{:x}|{:x}|{:x}", adapter_desc.VendorId, adapter_desc.DeviceId,
				                                adapter_desc.SubSysId, adapter_desc.Revision);

				df::gpu_desc = description;
				df::gpu_id = gpu_id;

				// Integrated parts report no dedicated memory and carve their working set out of the
				// shared aperture instead.
				vram_bytes = adapter_desc.DedicatedVideoMemory != 0
					             ? adapter_desc.DedicatedVideoMemory
					             : adapter_desc.SharedSystemMemory;

				df::log(__FUNCTION__, "     "s + description);
				df::log(__FUNCTION__, "     "s + gpu_id);
				df::log(__FUNCTION__,
				        "     DedicatedVideoMemory "s + df::file_size(adapter_desc.DedicatedVideoMemory).str());
				df::log(__FUNCTION__,
				        "     DedicatedSystemMemory "s + df::file_size(adapter_desc.DedicatedSystemMemory).str());
				df::log(__FUNCTION__,
				        "     SharedSystemMemory "s + df::file_size(adapter_desc.SharedSystemMemory).str());
			}
		}

		publish_image_budgets(feature_level, vram_bytes);
	}

	if (SUCCEEDED(hr))
	{
		if (software_mode)
		{
			df::d3d_info = "software";
			publish_image_budgets(D3D_FEATURE_LEVEL_11_0, 0);
		}

		register_fonts();
	}

	return SUCCEEDED(hr);
}

void factories::downgrade_to_software()
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());

	if (software_mode)
	{
		return;
	}

	df::log(__FUNCTION__, "Direct3D device lost - switching to CPU software rendering");

	software_mode = true;
	set_can_animate(false);

	// Drop every reference to the lost device. Draw contexts must already have been
	// destroyed by the caller; anything still holding a device child simply keeps a dead
	// object alive until it is released.
	if (d3d_context)
	{
		d3d_context->ClearState();
		d3d_context->Flush();
	}

	d3d_context.Reset();
	d3d_device.Reset();
	dxgi_device.Reset();
	d3d_feature_level = D3D_FEATURE_LEVEL_1_0_CORE;
	df::d3d_info = "software";
	publish_image_budgets(D3D_FEATURE_LEVEL_11_0, 0);
}


void factories::destroy()
{
	df::scope_rendering_func rf(__FUNCTION__);

	// Tear down DirectWrite in the correct order: release all font objects (faces, text
	// formats, collections) first, THEN detach the custom loaders, THEN release the factory.
	// Releasing the factory before the font objects (the previous order) relied on COM
	// reference counting to keep it alive and could leave the loaders attached while font
	// objects were still being freed.
	font_renderers.clear();
	font_collection.Reset();
	unregister_fonts();

	dxgi.Reset();
	dwrite.Reset();
	wic.Reset();
	d3d_device.Reset();
	d3d_context.Reset();
	dxgi_device.Reset();
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

	vertex_2d(const float x, const float y, const float u, const float v, const ui::color c, const sizei tex_dims)
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

	void set(const float x, const float y, const float u, const float v, const ui::color c)
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

	void set(const pointd xy, const pointd uv, const float a)
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

constexpr uint32_t vertex_stride = sizeof(vertex_2d);
constexpr uint32_t icon_texture_size = 512;

static_assert(std::is_trivial_v<vertex_2d>);

#pragma comment(lib, "d3d11")
#pragma comment(lib, "dxgi")

// vlc renderer
// https://github.com/videolan/vlc-unity/blob/master/Assets/PluginSource/RenderAPI_D3D11.cpp

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class d3d11_draw_context_impl;

// A texture together with the shader-resource views it is sampled through. A view holds a
// reference to its own resource, so whatever carries a binding keeps that texture alive. That is
// what lets an atom refer to a texture without a raw pointer that could go stale, and why there
// is no separate view cache or keep-alive list anywhere in this backend.
struct texture_binding
{
	ComPtr<ID3D11ShaderResourceView> y;
	ComPtr<ID3D11ShaderResourceView> uv; // chroma plane; null for everything but NV12/P010

	// Identity for atom merging and redundant-bind filtering. Two textures cannot share a view,
	// and the binding holds the view, so this stays meaningful for as long as it is used.
	ID3D11ShaderResourceView* id() const { return y.Get(); }
	explicit operator bool() const { return y != nullptr; }
};

// Answers an empty binding on failure, which leaves the caller to retry.
static texture_binding make_texture_binding(ID3D11Device* device, ID3D11Texture2D* t,
                                            const ui::texture_format fmt)
{
	texture_binding result;

	if (!device || !t) return result;

	D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
	srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	// Only the planar branch below uses this desc. A packed texture takes the null-desc branch and
	// gets a view over every level, which is what a mipped panorama needs - routing one through here
	// would give it a level-zero view and a GenerateMips that silently does nothing.
	srv.Texture2D.MipLevels = 1;
	srv.Texture2D.MostDetailedMip = 0;

	if (fmt == ui::texture_format::NV12 || fmt == ui::texture_format::P010)
	{
		const auto is_p010 = fmt == ui::texture_format::P010;
		srv.Format = is_p010 ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R8_UNORM;

		if (FAILED(device->CreateShaderResourceView(t, &srv, &result.y)))
		{
			return {};
		}

		// Luma without chroma would sample the wrong image with no error path, so a partial
		// failure discards both.
		srv.Format = is_p010 ? DXGI_FORMAT_R16G16_UNORM : DXGI_FORMAT_R8G8_UNORM;

		if (FAILED(device->CreateShaderResourceView(t, &srv, &result.uv)))
		{
			return {};
		}
	}
	else if (FAILED(device->CreateShaderResourceView(t, nullptr, &result.y)))
	{
		return {};
	}

	df::bump(df::gpu_perf.views_created, result.uv ? 2 : 1);
	return result;
}


class d3d11_text_renderer final : df::no_copy, public IDWriteTextRenderer
{
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
	df::hash_map<uint64_t, coords> _coords;
	glyph_face_keys _glyph_keys;
	font_renderer_ptr _font;
	pointi _next_location;

	ui::color _clr;
	std::vector<ui::text_highlight_t> _highlights;

	coords find_glyph(uint16_t c, const DWRITE_GLYPH_RUN* glyph_run);
	void create_a8_texture(int xy);

	// Same self-validating rebuild as d3d11_texture::binding: the binding holds the atlas it was
	// built from, so growing the atlas is detected rather than having to be signalled.
	texture_binding _atlas_binding;
	ID3D11Texture2D* _atlas_binding_source = nullptr;

	const texture_binding& atlas_binding()
	{
		if (!_atlas_binding || _atlas_binding_source != _texture.Get())
		{
			_atlas_binding = make_texture_binding(_f ? _f->d3d_device.Get() : nullptr, _texture.Get(),
			                                      ui::texture_format::RGB);
			_atlas_binding_source = _texture.Get();
		}

		return _atlas_binding;
	}

	std::atomic<int> _ref_count = 0;

public:
	d3d11_text_renderer() = default;
	~d3d11_text_renderer() override = default;

	void reset(const std::shared_ptr<d3d11_draw_context_impl>& c, const factories_ptr& f, font_renderer_ptr fr);
	void reset();

	[[nodiscard]] font_renderer_ptr font() const
	{
		return _font;
	}

	void draw_text(std::string_view text, recti bounds, ui::style::text_style style, ui::color c, ui::color bg);

	void draw_text(std::string_view text, const std::vector<ui::text_highlight_t>& highlights, recti bounds,
	               ui::style::text_style style, ui::color clr, ui::color bg);

	void draw_text(const std::shared_ptr<text_layout_impl>& text, recti bounds, ui::color clr, ui::color bg);

	sizei measure_text(const std::string_view text, const sizei avail, const ui::style::text_style style) const
	{
		df::scope_rendering_func rf(__FUNCTION__);
		if (!_font) return {};
		return _font->measure(text, style, avail.cx, avail.cy);
	}

	int line_height() const { return _line_height; }


	// ----- IUnknown -----

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
	                                         void** ppvObject) override
	{
		// IDWriteTextRenderer derives from IDWritePixelSnapping derives from IUnknown, so a single
		// static_cast yields a valid pointer for all three IIDs.
		if (riid == __uuidof(IUnknown) ||
			riid == __uuidof(IDWritePixelSnapping) ||
			riid == __uuidof(IDWriteTextRenderer))
		{
			*ppvObject = static_cast<IDWriteTextRenderer*>(this);
		}
		else
		{
			*ppvObject = nullptr;
			return E_NOINTERFACE;
		}

		AddRef();
		return S_OK;
	}

	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return static_cast<ULONG>(++_ref_count);
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		return static_cast<ULONG>(--_ref_count);
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
	// Cross-device video sharing: the decoder runs on FFmpeg's own D3D11 device and the
	// renderer on this device, so a decoded frame is bridged through a keyed-mutex shared
	// texture. The shared texture, its render-device view, and both keyed mutexes are
	// created once (per dimension/format) and reused every frame - opening a shared handle
	// per frame is a heavyweight kernel operation and must not sit in the render loop.
	ComPtr<ID3D11Texture2D> _shared_texture; // producer copy, on the video device
	ComPtr<ID3D11Texture2D> _shared_texture_render; // same resource opened on the render device
	ComPtr<IDXGIKeyedMutex> _shared_producer_mutex; // keyed mutex viewed from the video device
	ComPtr<IDXGIKeyedMutex> _shared_consumer_mutex; // keyed mutex viewed from the render device
	ComPtr<ID3D11Device> _shared_texture_device; // decode device the producer copy belongs to
	sizei _shared_texture_dimensions;
	ui::texture_format _shared_texture_format = ui::texture_format::None;

	void free_scaler()
	{
		_scaler.reset();
	}

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

	// Built on demand and rebuilt when the texture behind it is replaced. The binding holds the
	// texture it was built from, so that texture cannot be released while this comparison is
	// live and its address cannot be recycled by a different one - which is what makes a raw
	// pointer safe to compare here. A failed build leaves the binding empty and is retried.
	const texture_binding& binding()
	{
		if (!_binding || _binding_source != _texture.Get() || _binding_format != _format)
		{
			_binding = make_texture_binding(_f ? _f->d3d_device.Get() : nullptr, _texture.Get(), _format);
			_binding_source = _texture.Get();
			_binding_format = _format;
		}

		return _binding;
	}

	ui::texture_update_result update(const av_frame_ptr& frame) override;
	ui::texture_update_result update(const ui::const_surface_ptr& surface) override;
	ui::texture_update_result update_mipped(const ui::const_surface_ptr& surface) override;
	ui::texture_update_result update(sizei dims, ui::texture_format format, ui::orientation orientation,
	                                 const uint8_t* pixels, size_t stride, size_t buffer_size) override;

	friend class av_video_frames;

private:
	texture_binding _binding;
	ID3D11Texture2D* _binding_source = nullptr;
	ui::texture_format _binding_format = ui::texture_format::None;
};

class d3d11_vertices;

struct scene_atom
{
	texture_binding tex;
	ID3D11PixelShader* shader = nullptr;

	ui::texture_format tex_format = ui::texture_format::None;
	ui::texture_sampler sampler = ui::texture_sampler::point;

	uint32_t start_vertex = 0;
	uint32_t vertex_count = 0;

	uint32_t start_index = 0;
	uint32_t index_count = 0;

	std::shared_ptr<d3d11_vertices> verts;

	ui::color_space cs = ui::color_space::rec601_limited;
	std::shared_ptr<const ui::texture_transform> transform;
	// Set only by the panorama draw, which binds its own shader, constant buffer and sampler. A fresh
	// one per draw, so the constants are re-uploaded each frame - thirty-two bytes, against a frame
	// that has just resampled or reprojected the whole viewport.
	std::shared_ptr<const ui::panorama_params> pano;
	recti clip_bounds;
	bool has_clip = false;
};


df_assert_movable(scene_atom);

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


class d3d11_draw_context_impl final : public ui::draw_context_device,
                                      public std::enable_shared_from_this<d3d11_draw_context_impl>
{
public:
	recti _clip_bounds;
	std::vector<recti> _clip_stack;
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
	ComPtr<ID3D11PixelShader> _pixel_shader_pano;
	ComPtr<ID3D11Buffer> _yuv_cbuffer;
	ComPtr<ID3D11Buffer> _texture_transform_cbuffer;
	ComPtr<ID3D11Buffer> _pano_cbuffer;
	ComPtr<ID3D11Buffer> _vertex_buffer;
	ComPtr<ID3D11Buffer> _index_buffer;
	// Bytes currently allocated in the dynamic buffers above; they are reused across frames and only
	// reallocated when a frame needs more room.
	uint32_t _vertex_buffer_capacity = 0;
	uint32_t _index_buffer_capacity = 0;
	ComPtr<ID3D11BlendState> _blend_state;
	ComPtr<ID3D11RasterizerState> _rasterizer_state;
	ComPtr<ID3D11SamplerState> _sampler_point;
	ComPtr<ID3D11SamplerState> _sampler_bilinear;
	ComPtr<ID3D11SamplerState> _sampler_pano_clamp;
	ComPtr<ID3D11SamplerState> _sampler_pano_wrap;
	// Built once per back buffer, not once per frame. Dropped by release_back_buffer_references
	// before ResizeBuffers, which is the only thing that replaces the underlying surface.
	ComPtr<ID3D11RenderTargetView> _back_buffer_rtv;
	// Only for the video-memory gauge. Resolved once because QueryInterface per sample would cost
	// more than the reading is worth.
	ComPtr<IDXGIAdapter3> _vram_adapter;
	uint32_t _frames_until_vram_sample = 0;
	// The edit preview rebuilds an identical transform every paint. Holding the last one lets the
	// atom reuse it instead of allocating a 1KB curve per frame.
	std::shared_ptr<const ui::texture_transform> _last_transform;


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

	d3d11_draw_context_impl() = default;

	~d3d11_draw_context_impl() override
	{
		destroy();
	}

	void create(const factories_ptr& f, const ComPtr<IDXGISwapChain>& swap_chain, int base_font_size);

	void resize(sizei extent) override;
	void update_font_size(int base_font_size) override;

	void build_index_and_vertex_buffers();
	void sample_video_memory();
	HRESULT draw_scene(const ComPtr<ID3D11DeviceContext>& context);


	sizei measure_string(std::string_view text, sizei size_avail, ui::style::font_face, ui::style::text_style);
	int line_height(ui::style::font_face);

	bool is_valid() const override
	{
		return _is_valid && _f->d3d_device && _f->d3d_context;
	}

	sizei client_extent() const
	{
		return _client_extent;
	}

	ID3D11PixelShader* calc_shader(bool is_bicubic, ui::texture_format tex_fmt) const;

	void add_scene_atom(const texture_binding& tex, const ComPtr<ID3D11PixelShader>& ss,
	                    ui::texture_format tex_fmt, ui::texture_sampler sampler, const vertex_2d* vertices,
	                    size_t vertex_count, const WORD* indexes, size_t index_count,
	                    ui::color_space cs = ui::color_space::rec601_limited,
	                    std::shared_ptr<const ui::texture_transform> transform = nullptr,
	                    std::shared_ptr<const ui::panorama_params> pano = nullptr);
	void draw_texture(const texture_d3d11_ptr& t, const quadd& dst, recti src, ui::color c,
	                  ui::texture_sampler sampler);
	void draw_texture(const texture_d3d11_ptr& t, recti dst, recti src, ui::color c, ui::texture_sampler sampler,
	                  float radius);

	void destroy() override;
	void begin_draw(sizei client_extent, int base_font_size, recti damage = {}) override;
	ui::present_result render() override;
	void release_back_buffer_references() override;

	void clear(ui::color c) override;
	void draw_rounded_rect(recti bounds, ui::color c, int radius) override;
	void draw_rect(recti bounds, ui::color c) override;
	void draw_rect_gradient(recti bounds, ui::color c_centre, ui::color c_corner) override;
	void draw_text(std::string_view text, recti bounds, ui::style::font_face font, ui::style::text_style style,
	               ui::color c, ui::color bg) override;
	void draw_text(std::string_view text, const std::vector<ui::text_highlight_t>& highlights, recti bounds,
	               ui::style::font_face font, ui::style::text_style style, ui::color clr, ui::color bg) override;
	void draw_text(const ui::text_layout_ptr& tl, recti bounds, ui::color clr, ui::color bg) override;
	void draw_shadow(recti bounds, int width, float alpha, bool inverse) override;
	void draw_border(recti inside, recti outside, ui::color c_inside, ui::color c_outside) override;
	void draw_texture(const ui::texture_ptr& t, recti dst, float alpha, ui::texture_sampler sampler) override;
	void draw_texture(const ui::texture_ptr& t, recti dst, recti src, float alpha, ui::texture_sampler sampler,
	                  float radius) override;
	void draw_texture(const ui::texture_ptr& t, const quadd& dst, recti src, float alpha,
	                  ui::texture_sampler sampler) override;
	void draw_texture(const ui::texture_ptr& t, const quadd& dst, recti src, float alpha,
	                  ui::texture_sampler sampler, const ui::texture_transform& transform) override;
	bool draw_panorama(const ui::texture_ptr& t, recti dst, const ui::panorama_params& params, float alpha) override;
	void draw_vertices(const ui::vertices_ptr& v) override;

	ui::texture_ptr create_texture() override;
	ui::vertices_ptr create_vertices() override;
	font_renderer_ptr find_font(ui::style::font_face font);
	ui::text_layout_ptr create_text_layout(ui::style::font_face font) override;

	sizei measure_text(std::string_view text, ui::style::font_face font, ui::style::text_style style, int width,
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

	// A destroyed context must stop reporting itself as usable, otherwise the window layer
	// keeps presenting through it instead of recreating one.
	_is_valid = false;

	_shadow.reset();
	_inverse_shadow.reset();
	_scene_atoms.clear();
	_last_transform.reset();
	_font.clear();
	_vertex_buffer_staging.clear();
	_index_buffer_staging.clear();

	_back_buffer_rtv.Reset();
	_vram_adapter.Reset();
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
	_pixel_shader_pano.Reset();
	_yuv_cbuffer.Reset();
	_texture_transform_cbuffer.Reset();
	_pano_cbuffer.Reset();
	_vertex_buffer.Reset();
	_index_buffer.Reset();
	_vertex_buffer_capacity = 0;
	_index_buffer_capacity = 0;
	_blend_state.Reset();
	_rasterizer_state.Reset();
	_sampler_point.Reset();
	_sampler_bilinear.Reset();
	_sampler_pano_clamp.Reset();
	_sampler_pano_wrap.Reset();
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
		_font[ui::style::font_face::small_icons].reset(
			c, _f, _f->font_face(ui::style::font_face::small_icons, base_font_size));
	}
}

static texture_d3d11_ptr create_texture_from_resource(const factories_ptr& f, const int id, const LPCWSTR type)
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());
	files ff;
	auto result = std::make_shared<d3d11_texture>(f);
	result->update(ff.image_to_surface(load_resource(id, type)));
	return result;
}


void d3d11_draw_context_impl::create(const factories_ptr& f, const ComPtr<IDXGISwapChain>& swap_chain,
                                     const int base_font_size)
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());
	_f = f;
	_back_buffer_rtv.Reset();
	_swap_chain = swap_chain;

	auto hr = swap_chain && f && f->d3d_device ? S_OK : E_FAIL;

	if (SUCCEEDED(hr))
	{
		df::log(__FUNCTION__, std::format("draw context created - feature level {}", to_string(_f->d3d_feature_level)));


		uint32_t support = 0;

		_supports_p010 = SUCCEEDED(_f->d3d_device->CheckFormatSupport(to_format(ui::texture_format::P010), &support))
			&& support & D3D11_FORMAT_SUPPORT_TEXTURE2D;

		_supports_nv12 = SUCCEEDED(_f->d3d_device->CheckFormatSupport(to_format(ui::texture_format::NV12), &support))
			&& support & D3D11_FORMAT_SUPPORT_TEXTURE2D;

		df::log(__FUNCTION__, _supports_p010 ? "     p010 supported" : "     p010 not-supported");
		df::log(__FUNCTION__, _supports_nv12 ? "     nv12 supported" : "     nv12 not-supported");


		if (SUCCEEDED(hr))
		{
			// The ID3D11Device must have multithread protection 
			ComPtr<ID3D10Multithread> multithread;

			if (SUCCEEDED(
				_f->d3d_device->QueryInterface(__uuidof(ID3D10Multithread), std::bit_cast<void**>(multithread.
					GetAddressOf()))))
			{
				// SetMultithreadProtected returns the PREVIOUS protection state (a BOOL), not a
				// success/failure HRESULT, so its result must not be treated as an error indicator.
				multithread->SetMultithreadProtected(TRUE);
				multithread = nullptr;
			}
			else
			{
				df::log(__FUNCTION__, "Warning: ID3D10Multithread interface not available");
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

				constexpr uint32_t num_elements = ARRAYSIZE(layout);
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
			const auto shader = load_resource(IDR_SHADER_PANO, L"SHADER");
			hr = _f->d3d_device->CreatePixelShader(shader.data(), shader.size(), nullptr, &_pixel_shader_pano);
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
				df::log(__FUNCTION__, std::format("CreateBlendState failed {:x}", static_cast<uint32_t>(hr)));
			}
		}

		if (SUCCEEDED(hr))
		{
			D3D11_RASTERIZER_DESC desc = {};
			desc.CullMode = D3D11_CULL_NONE;
			desc.FillMode = D3D11_FILL_SOLID;
			desc.ScissorEnable = true;

			hr = _f->d3d_device->CreateRasterizerState(&desc, &_rasterizer_state);

			if (FAILED(hr))
			{
				df::log(__FUNCTION__, std::format("CreateRasterizerState failed {:x}", static_cast<uint32_t>(hr)));
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
				df::log(__FUNCTION__, std::format("CreateSamplerState point failed {:x}", static_cast<uint32_t>(hr)));
			}

			sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;

			hr = _f->d3d_device->CreateSamplerState(&sampler_desc, &_sampler_bilinear);

			if (FAILED(hr))
			{
				df::log(__FUNCTION__,
				        std::format("CreateSamplerState bilinear failed {:x}", static_cast<uint32_t>(hr)));
			}
		}

		if (SUCCEEDED(hr))
		{
			// The projection is the one place in the app that minifies a long way and samples at a
			// glancing angle at the same time, which is what anisotropy is for. Latitude always clamps -
			// there is no sphere past a pole - while longitude wraps for a file that closes the circle,
			// so the blend at the join reads the far edge instead of doubling the near one.
			D3D11_SAMPLER_DESC sampler_desc = {};
			sampler_desc.Filter = D3D11_FILTER_ANISOTROPIC;
			sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampler_desc.MipLODBias = 0.0f;
			sampler_desc.MaxAnisotropy = 8;
			sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
			sampler_desc.MinLOD = -FLT_MAX;
			sampler_desc.MaxLOD = FLT_MAX;

			hr = _f->d3d_device->CreateSamplerState(&sampler_desc, &_sampler_pano_clamp);

			if (SUCCEEDED(hr))
			{
				sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
				hr = _f->d3d_device->CreateSamplerState(&sampler_desc, &_sampler_pano_wrap);
			}

			if (FAILED(hr))
			{
				df::log(__FUNCTION__,
				        std::format("CreateSamplerState panorama failed {:x}", static_cast<uint32_t>(hr)));
			}
		}

		if (SUCCEEDED(hr))
		{
			// Constant buffer holding the YUV->RGB affine matrix (row_major float3x4 = 48 bytes).
			D3D11_BUFFER_DESC bd = {};
			bd.ByteWidth = sizeof(float) * 12;
			bd.Usage = D3D11_USAGE_DYNAMIC;
			bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

			hr = _f->d3d_device->CreateBuffer(&bd, nullptr, &_yuv_cbuffer);

			if (FAILED(hr))
			{
				df::log(__FUNCTION__,
				        std::format("CreateBuffer for yuv params failed {:x}", static_cast<uint32_t>(hr)));
			}
		}

		if (SUCCEEDED(hr))
		{
			// The panorama camera and coverage: two float4 registers, matching panorama_params in
			// pano_project.hlsli.
			D3D11_BUFFER_DESC bd = {};
			bd.ByteWidth = sizeof(float) * 8;
			bd.Usage = D3D11_USAGE_DYNAMIC;
			bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

			hr = _f->d3d_device->CreateBuffer(&bd, nullptr, &_pano_cbuffer);

			if (FAILED(hr))
			{
				df::log(__FUNCTION__,
				        std::format("CreateBuffer for panorama params failed {:x}", static_cast<uint32_t>(hr)));
			}
		}

		if (SUCCEEDED(hr))
		{
			D3D11_BUFFER_DESC bd = {};
			bd.ByteWidth = 1104;
			bd.Usage = D3D11_USAGE_DYNAMIC;
			bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			hr = _f->d3d_device->CreateBuffer(&bd, nullptr, &_texture_transform_cbuffer);

			if (FAILED(hr))
			{
				df::log(__FUNCTION__,
				        std::format("CreateBuffer for texture transform failed {:x}", static_cast<uint32_t>(hr)));
			}
		}

		update_font_size(base_font_size);
	}

	_is_valid = SUCCEEDED(hr);

	if (!_is_valid)
	{
		df::log(__FUNCTION__, std::format("draw_context_d3d11_impl::create failed {:x}", static_cast<uint32_t>(hr)));

		// Release whatever was created before the failure so the caller can fall back to the
		// CPU software backend without leaving half-built GPU state (and a swap chain the
		// window layer would otherwise keep presenting to) behind.
		destroy();
	}
}

void d3d11_draw_context_impl::resize(const sizei extent)
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());

	if (_client_extent != extent && extent.cx > 0 && extent.cy > 0)
	{
		_client_extent = extent;
		_clip_bounds = extent;
	}
}

// The damage rect is ignored: FLIP_SEQUENTIAL rotates back buffers, so the untouched region of the
// buffer we are about to draw into holds two-frames-ago content rather than the last frame.
// Redrawing the whole client is what keeps that correct, and is cheap on the GPU.
void d3d11_draw_context_impl::begin_draw(const sizei client_extent, int base_font_size, recti /*damage*/)
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());

	// Always reset per-frame state, even when the device is not usable: the view still issues
	// draw calls for the frame and the staging buffers would otherwise grow without bound.
	_client_extent = client_extent;
	_clip_bounds.set(0, 0, _client_extent.cx, _client_extent.cy);
	_clip_stack.clear();

	_scene_atoms.clear();

	_vertex_buffer_staging.clear();
	_index_buffer_staging.clear();

	_vertex_buffer_staging.reserve(5000);
	_index_buffer_staging.reserve(8000);
	_scene_atoms.reserve(256);
}

// Uploads the staged geometry into the shared vertex and index buffers. They must be replaced
// together - a frame that got one but not the other would be drawn with mismatched buffers
// (garbage geometry or a GPU fault), so this commits both or clears both. Atoms that use their
// own buffers (d3d11_vertices) are unaffected; draw_scene skips shared-buffer atoms when the
// buffers are null.
//
// When nothing is staged the existing buffers are KEPT. frame_impl::redraw re-renders the scene
// built by the last WM_PAINT without calling begin_draw, which is how a new video or visualiser
// frame is presented without rebuilding the scene; resetting the buffers here would draw it blank.
void d3d11_draw_context_impl::build_index_and_vertex_buffers()
{
	df::scope_rendering_func rf(__FUNCTION__);

	if (_vertex_buffer_staging.empty() && _index_buffer_staging.empty())
	{
		return;
	}

	if (_vertex_buffer_staging.empty() || _index_buffer_staging.empty())
	{
		// Vertices without indices (or the reverse) cannot describe a frame.
		_vertex_buffer.Reset();
		_index_buffer.Reset();
		_vertex_buffer_staging.clear();
		_index_buffer_staging.clear();
		return;
	}

	const auto vertex_bytes = sizeof(vertex_2d) * _vertex_buffer_staging.size();
	const auto index_bytes = sizeof(WORD) * _index_buffer_staging.size();

	if (vertex_bytes > UINT_MAX || index_bytes > UINT_MAX)
	{
		df::log(__FUNCTION__, "Vertex or index buffer size exceeds maximum allowed size");
		_vertex_buffer.Reset();
		_index_buffer.Reset();
		_vertex_buffer_staging.clear();
		_index_buffer_staging.clear();
		return;
	}

	const auto upload = [this](ComPtr<ID3D11Buffer>& buffer, uint32_t& capacity, const UINT bind_flag,
	                           const void* data, const uint32_t bytes)
	{
		if (!buffer || capacity < bytes)
		{
			buffer.Reset();
			capacity = 0;

			D3D11_BUFFER_DESC bd = {};
			bd.ByteWidth = std::max(bytes, 64u * 1024u);
			bd.Usage = D3D11_USAGE_DYNAMIC;
			bd.BindFlags = bind_flag;
			bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

			const auto hr = _f->d3d_device->CreateBuffer(&bd, nullptr, &buffer);

			if (FAILED(hr))
			{
				df::log(__FUNCTION__, std::format("CreateBuffer failed: {:x}", static_cast<uint32_t>(hr)));
				buffer.Reset();
				return false;
			}

			capacity = bd.ByteWidth;
			df::bump(df::gpu_perf.buffers_created);
		}

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		const auto hr = _f->d3d_context->Map(buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);

		if (FAILED(hr))
		{
			df::log(__FUNCTION__, std::format("Map failed: {:x}", static_cast<uint32_t>(hr)));
			return false;
		}

		memcpy(mapped.pData, data, bytes);
		_f->d3d_context->Unmap(buffer.Get(), 0);
		df::bump(df::gpu_perf.geometry_bytes, bytes);
		return true;
	};

	const auto uploaded = upload(_vertex_buffer, _vertex_buffer_capacity, D3D11_BIND_VERTEX_BUFFER,
	                             _vertex_buffer_staging.data(), static_cast<uint32_t>(vertex_bytes)) &&
		upload(_index_buffer, _index_buffer_capacity, D3D11_BIND_INDEX_BUFFER,
		       _index_buffer_staging.data(), static_cast<uint32_t>(index_bytes));

	_vertex_buffer_staging.clear();
	_index_buffer_staging.clear();

	if (!uploaded)
	{
		_vertex_buffer.Reset();
		_index_buffer.Reset();
		_vertex_buffer_capacity = 0;
		_index_buffer_capacity = 0;
	}
}

struct context_state final
{
	ID3D11PixelShader* shader = nullptr;
	ID3D11ShaderResourceView* bound_view = nullptr;
	ID3D11SamplerState* sampler = nullptr;
	ID3D11Buffer* vertex_buffer = nullptr;
	ID3D11Buffer* index_buffer = nullptr;
	ID3D11Buffer* pixel_cbuffer = nullptr;

	ID3D11Buffer* yuv_cbuffer = nullptr;
	ID3D11Buffer* texture_transform_cbuffer = nullptr;
	ID3D11Buffer* pano_cbuffer = nullptr;
	ui::color_space uploaded_cs = ui::color_space::rec601_limited;
	ui::texture_format uploaded_yuv_format = ui::texture_format::None;
	bool cs_uploaded = false;
	const ui::texture_transform* uploaded_transform = nullptr;
	const ui::panorama_params* uploaded_pano = nullptr;
	bool identity_transform_uploaded = false;

	ID3D11DeviceContext* context;
	D3D11_RECT client_clip = {};
	uint32_t draws = 0;

	context_state(ID3D11DeviceContext* c, const sizei client_extent) : context(c)
	{
		client_clip = {0, 0, client_extent.cx, client_extent.cy};
	}

	// clip_source lets a replayed vertices atom take the clip that was active when draw_vertices
	// queued it, rather than the (unclipped) state baked in when its buffers were built.
	void draw_atom(const scene_atom& a, ID3D11Buffer* vb, ID3D11Buffer* ib, ID3D11SamplerState* ss,
	               const scene_atom* clip_source = nullptr)
	{
		df::scope_rendering_func rf(__FUNCTION__);
		const auto& clip_from = clip_source ? *clip_source : a;

		if (clip_from.has_clip)
		{
			const D3D11_RECT clip = {
				clip_from.clip_bounds.left, clip_from.clip_bounds.top,
				clip_from.clip_bounds.right, clip_from.clip_bounds.bottom
			};
			context->RSSetScissorRects(1, &clip);
		}
		else
		{
			context->RSSetScissorRects(1, &client_clip);
		}
		auto* const s = a.shader;
		auto* const view = a.tex.id();
		const auto tx_fmt = a.tex_format;
		auto* const required_cbuffer = a.pano
			                               ? pano_cbuffer
			                               : tx_fmt == ui::texture_format::NV12 || tx_fmt == ui::texture_format::P010
			                               ? yuv_cbuffer
			                               : tx_fmt != ui::texture_format::None
			                               ? texture_transform_cbuffer
			                               : nullptr;
		if (required_cbuffer != pixel_cbuffer)
		{
			pixel_cbuffer = required_cbuffer;
			ID3D11Buffer* buffers[] = {required_cbuffer};
			context->PSSetConstantBuffers(0, 1, buffers);
		}

		if (s != shader)
		{
			shader = s;
			context->PSSetShader(s, nullptr, 0);
			df::bump(df::gpu_perf.shader_binds);
		}

		// For YUV shaders, upload the colour-space/range conversion matrix when it changes.
		// The matrix also depends on the pixel format (P010 needs a level correction), so a
		// format switch at the same colour space must still re-upload.
		if ((tx_fmt == ui::texture_format::NV12 || tx_fmt == ui::texture_format::P010) && yuv_cbuffer &&
			(!cs_uploaded || a.cs != uploaded_cs || tx_fmt != uploaded_yuv_format))
		{
			uploaded_cs = a.cs;
			uploaded_yuv_format = tx_fmt;
			cs_uploaded = true;

			const auto ym = ui::compute_yuv_matrix(a.cs, tx_fmt == ui::texture_format::P010);
			D3D11_MAPPED_SUBRESOURCE mapped;

			if (SUCCEEDED(context->Map(yuv_cbuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			{
				memcpy(mapped.pData, ym.m, sizeof(ym.m));
				context->Unmap(yuv_cbuffer, 0);
				df::bump(df::gpu_perf.cbuffer_uploads);
			}
		}

		if (a.pano && pano_cbuffer && a.pano.get() != uploaded_pano)
		{
			struct alignas(16) shader_panorama_params
			{
				float camera[4];
				float coverage[4];
			};

			static_assert(sizeof(shader_panorama_params) == 32);
			const shader_panorama_params params{
				{a.pano->yaw, a.pano->pitch, a.pano->tan_half_fov, a.pano->aspect},
				{a.pano->longitude_left, a.pano->u_scale, a.pano->latitude_top, a.pano->v_scale}
			};

			D3D11_MAPPED_SUBRESOURCE mapped;

			if (SUCCEEDED(context->Map(pano_cbuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			{
				memcpy(mapped.pData, &params, sizeof(params));
				context->Unmap(pano_cbuffer, 0);
				df::bump(df::gpu_perf.cbuffer_uploads);
			}

			uploaded_pano = a.pano.get();
		}

		if (tx_fmt != ui::texture_format::None && tx_fmt != ui::texture_format::NV12 &&
			tx_fmt != ui::texture_format::P010 && !a.pano && texture_transform_cbuffer &&
			(a.transform.get() != uploaded_transform || (!a.transform && !identity_transform_uploaded)))
		{
			struct alignas(16) shader_transform_params
			{
				float curve[ui::texture_transform::curve_len];
				float perspective[4];
				float color[4];
				float color2[4];
			};

			static_assert(sizeof(shader_transform_params) == 1072);
			shader_transform_params params = {};
			const ui::texture_transform identity;
			const auto& transform = a.transform ? *a.transform : identity;
			std::ranges::copy(transform.curve, params.curve);
			params.perspective[0] = transform.perspective_horizontal;
			params.perspective[1] = transform.perspective_vertical;
			params.perspective[2] = transform.has_perspective ? 1.0f : 0.0f;
			params.perspective[3] = transform.has_color_changes ? 1.0f : 0.0f;
			params.color[0] = transform.saturation;
			params.color[1] = transform.vibrance;
			params.color[2] = transform.red_gain;
			params.color[3] = transform.green_gain;
			params.color2[0] = transform.blue_gain;

			D3D11_MAPPED_SUBRESOURCE mapped;
			if (SUCCEEDED(context->Map(texture_transform_cbuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
			{
				memcpy(mapped.pData, &params, sizeof(params));
				context->Unmap(texture_transform_cbuffer, 0);
				df::bump(df::gpu_perf.cbuffer_uploads);
			}
			uploaded_transform = a.transform.get();
			identity_transform_uploaded = !a.transform;
		}

		if (ss != sampler)
		{
			sampler = ss;
			ID3D11SamplerState* samplers[] = {ss};
			context->PSSetSamplers(0, 1, samplers);
			df::bump(df::gpu_perf.sampler_binds);
		}

		if (vb != vertex_buffer)
		{
			vertex_buffer = vb;
			UINT offsets[] = {0};
			ID3D11Buffer* buffers[] = {vertex_buffer};
			context->IASetVertexBuffers(0, 1, buffers, &vertex_stride, offsets);
		}

		if (ib != index_buffer)
		{
			index_buffer = ib;
			context->IASetIndexBuffer(index_buffer, DXGI_FORMAT_R16_UINT, 0);
		}

		if (view != bound_view && view != nullptr)
		{
			// Both slots are bound together so a chroma view left over from a previous YUV atom does
			// not stay bound to slot 1 - a stale binding keeps the video texture referenced and forces
			// the runtime to unbind it on the next copy.
			bound_view = view;
			ID3D11ShaderResourceView* views_to_bind[] = {view, a.tex.uv.Get()};
			context->PSSetShaderResources(0, 2, views_to_bind);
			df::bump(df::gpu_perf.view_binds);
		}

		context->DrawIndexed(a.index_count, a.start_index, a.start_vertex);
		++draws;
	}
};

// Sampled rather than tracked: the runtime and the driver both allocate behind our back, so the
// adapter's own figure is the only one worth logging. Rate-limited because it is a driver call on
// the frame path and the value moves slowly.
void d3d11_draw_context_impl::sample_video_memory()
{
	constexpr uint32_t frames_between_samples = 256;

	if (_frames_until_vram_sample > 0)
	{
		--_frames_until_vram_sample;
		return;
	}

	_frames_until_vram_sample = frames_between_samples;

	if (!_vram_adapter)
	{
		ComPtr<IDXGIAdapter> adapter;

		if (!_f || !_f->dxgi_device || FAILED(_f->dxgi_device->GetAdapter(&adapter)) ||
			FAILED(adapter.As(&_vram_adapter)))
		{
			return;
		}
	}

	DXGI_QUERY_VIDEO_MEMORY_INFO info = {};

	if (SUCCEEDED(_vram_adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)))
	{
		const auto mb = static_cast<uint32_t>(info.CurrentUsage / (1024ull * 1024ull));
		df::set_gauge(df::gpu_perf.vram_mb, mb);
		df::record_peak(df::gpu_perf.vram_peak_mb, mb);
	}
}

HRESULT d3d11_draw_context_impl::draw_scene(const ComPtr<ID3D11DeviceContext>& context)
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::perf_timer timer(df::gpu_perf.submit_us, &df::gpu_perf.submit_max_us);
	df::bump(df::gpu_perf.frames);
	sample_video_memory();

	// The flip model rotates the surface behind buffer 0, but the object and its view stay valid
	// until ResizeBuffers replaces them, so this is built once rather than once per frame.
	if (!_back_buffer_rtv)
	{
		ComPtr<ID3D11Texture2D> back_buffer;
		auto hr = _swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D),
		                                 std::bit_cast<void**>(back_buffer.GetAddressOf()));

		if (FAILED(hr))
		{
			df::log(__FUNCTION__, std::format("IDXGISwapChain::GetBuffer failed {:x}", static_cast<uint32_t>(hr)));
			return hr;
		}

		hr = _f->d3d_device->CreateRenderTargetView(back_buffer.Get(), nullptr, &_back_buffer_rtv);

		if (FAILED(hr))
		{
			df::log(__FUNCTION__, std::format("CreateRenderTargetView failed {:x}", static_cast<uint32_t>(hr)));
			_back_buffer_rtv.Reset();
			return hr;
		}

		df::bump(df::gpu_perf.targets_created);
	}

	{
		ID3D11RenderTargetView* views[] = {_back_buffer_rtv.Get()};
		context->OMSetRenderTargets(1, views, nullptr);

		const D3D11_VIEWPORT viewport = {
			0.0f, 0.0f, static_cast<float>(_client_extent.cx), static_cast<float>(_client_extent.cy), 0.0f, 1.0f
		};
		context->RSSetViewports(1, &viewport);
		const D3D11_RECT client_clip = {0, 0, _client_extent.cx, _client_extent.cy};
		context->RSSetScissorRects(1, &client_clip);

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
			context->OMSetBlendState(_blend_state.Get(), nullptr, 0xFFFFFFFF);
		}

		constexpr DirectX::XMVECTORF32 bg_color = {
			{scene_clear_shade, scene_clear_shade, scene_clear_shade, 1.0f}
		};
		context->ClearRenderTargetView(_back_buffer_rtv.Get(), bg_color);

		// Starts with every cached binding null, so the first atom that needs one sets it. That is
		// what makes a per-frame ClearState unnecessary: this backend owns the context outright and
		// nothing else on it touches pipeline state.
		context_state state(context.Get(), _client_extent);
		state.yuv_cbuffer = _yuv_cbuffer.Get();
		state.texture_transform_cbuffer = _texture_transform_cbuffer.Get();
		state.pano_cbuffer = _pano_cbuffer.Get();

		for (const auto& a : _scene_atoms)
		{
			if (a.verts)
			{
				const auto& vb = a.verts->_vertex_buffer;
				const auto& ib = a.verts->_index_buffer;

				if (!vb || !ib) continue;

				for (const auto& aa : a.verts->_scene_atoms)
				{
					const auto ss = aa.sampler == ui::texture_sampler::point ? _sampler_point : _sampler_bilinear;
					state.draw_atom(aa, vb.Get(), ib.Get(), ss.Get(), &a);
				}
			}
			else if (_vertex_buffer && _index_buffer)
			{
				const auto ss = a.pano
					                ? (a.pano->wraps ? _sampler_pano_wrap : _sampler_pano_clamp)
					                : a.sampler == ui::texture_sampler::point
					                ? _sampler_point
					                : _sampler_bilinear;
				state.draw_atom(a, _vertex_buffer.Get(), _index_buffer.Get(), ss.Get());
			}
		}

		df::bump(df::gpu_perf.draws, state.draws);
		df::record_peak(df::gpu_perf.draws_peak, state.draws);
	}

	// Unbind the back buffer before returning: the flip model requires it before Present, and
	// IDXGISwapChain::ResizeBuffers fails with DXGI_ERROR_INVALID_CALL while a render target view
	// of a back buffer is still bound, which would leave the window rendering at a stale size.
	// The sampled textures go with it so a video surface is not still bound as an input when the
	// next decoded frame is copied into it.
	ID3D11ShaderResourceView* const no_views[] = {nullptr, nullptr};
	context->PSSetShaderResources(0, 2, no_views);
	context->OMSetRenderTargets(0, nullptr, nullptr);

	return S_OK;
}

void d3d11_draw_context_impl::release_back_buffer_references()
{
	if (_f && _f->d3d_context)
	{
		_f->d3d_context->OMSetRenderTargets(0, nullptr, nullptr);
	}

	// The cached view holds a reference of its own, so unbinding alone would not let
	// ResizeBuffers through.
	_back_buffer_rtv.Reset();
}

ui::present_result d3d11_draw_context_impl::render()
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());

	if (!is_valid() || !_swap_chain)
	{
		return {};
	}

	// Uploads anything staged since the last render. A redraw that only updated a texture
	// stages nothing and re-draws the scene from the buffers built by the last paint.
	build_index_and_vertex_buffers();

	const auto hr = draw_scene(_f->d3d_context);
	return {FAILED(hr), hr};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void d3d11_draw_context_impl::add_scene_atom(const texture_binding& tex,
                                             const ComPtr<ID3D11PixelShader>& shader, const ui::texture_format tex_fmt,
                                             const ui::texture_sampler sampler, const vertex_2d* vertices,
                                             const size_t vertex_count, const WORD* indexes, const size_t index_count,
                                             const ui::color_space cs,
                                             std::shared_ptr<const ui::texture_transform> transform,
                                             std::shared_ptr<const ui::panorama_params> pano)
{
	df::scope_rendering_func rf(__FUNCTION__);
	auto combine_with_last_atom = false;

	if (!_scene_atoms.empty())
	{
		// Optimise by extending the existing atom. Everything that is bound per atom must
		// match - shader, texture, sampler, pixel format, colour space, transform and clip -
		// otherwise the merged atom would silently render with the first atom's state.
		// Indices are 16-bit and relative to the atom's base vertex, so a merged atom must
		// also stay within the 16-bit range.
		auto&& back = _scene_atoms.back();
		combine_with_last_atom = back.tex.id() == tex.id() && back.shader == shader.Get() &&
			back.tex_format == tex_fmt && back.sampler == sampler && back.cs == cs &&
			!back.transform && !transform && !back.pano && !pano && back.has_clip == !_clip_stack.empty() &&
			(!back.has_clip || back.clip_bounds == _clip_bounds) &&
			(static_cast<size_t>(back.vertex_count) + vertex_count) <= std::numeric_limits<WORD>::max();
	}

	const auto vertex_pos = _vertex_buffer_staging.size();
	const auto index_pos = _index_buffer_staging.size();

	_vertex_buffer_staging.insert(_vertex_buffer_staging.end(), vertices, vertices + vertex_count);
	_index_buffer_staging.insert(_index_buffer_staging.end(), indexes, indexes + index_count);

	if (combine_with_last_atom)
	{
		const auto vc = _scene_atoms.back().vertex_count;

		df::bump(df::gpu_perf.merged);
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
			tex, shader.Get(), tex_fmt, sampler,
			static_cast<uint32_t>(vertex_pos),
			static_cast<uint32_t>(vertex_count),
			static_cast<uint32_t>(index_pos),
			static_cast<uint32_t>(index_count)
		};

		sa.cs = cs;
		sa.transform = std::move(transform);
		sa.pano = std::move(pano);
		sa.clip_bounds = _clip_bounds;
		sa.has_clip = !_clip_stack.empty();
		_scene_atoms.emplace_back(std::move(sa));
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

// The quad carries 0..1 texture coordinates across the destination rather than a source rectangle:
// the shader is inverting each of them onto the sphere, so there is no source rectangle to name.
bool d3d11_draw_context_impl::draw_panorama(const ui::texture_ptr& t, const recti dst,
                                            const ui::panorama_params& params, const float alpha)
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());

	const auto tt = std::dynamic_pointer_cast<d3d11_texture>(t);

	if (!tt || !_pixel_shader_pano || !_pano_cbuffer || !_sampler_pano_clamp || !_sampler_pano_wrap) return false;
	if (tt->_f->d3d_device != _f->d3d_device) return false;

	const auto binding = tt->binding();
	if (!binding) return false;

	const auto ex = static_cast<float>(_client_extent.cx);
	const auto ey = static_cast<float>(_client_extent.cy);
	const auto dl = dst.left / ex;
	const auto dt = dst.top / ey;
	const auto dr = dst.right / ex;
	const auto db = dst.bottom / ey;

	const ui::color c(1.0f, 1.0f, 1.0f, alpha);

	const vertex_2d vertices[] =
	{
		vertex_2d(dl, dt, 0.0f, 0.0f, c, tt->dimensions()),
		vertex_2d(dr, dt, 1.0f, 0.0f, c, tt->dimensions()),
		vertex_2d(dr, db, 1.0f, 1.0f, c, tt->dimensions()),
		vertex_2d(dl, db, 0.0f, 1.0f, c, tt->dimensions()),
	};

	const WORD indexes[] = {0, 1, 2, 3, 0, 2};

	add_scene_atom(binding, _pixel_shader_pano, tt->_format, ui::texture_sampler::bilinear, vertices,
	               std::size(vertices), indexes, std::size(indexes), tt->_cs, nullptr,
	               std::make_shared<const ui::panorama_params>(params));

	return true;
}

void d3d11_draw_context_impl::draw_texture(const texture_d3d11_ptr& t, const recti dst, const recti src,
                                           const ui::color c, const ui::texture_sampler sampler, float radius)
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());

	if (!t) return;

	df::assert_true(t->_f->d3d_device == _f->d3d_device);

	const auto alpha = c.a;

	if (alpha >= 0.01f && t->_f->d3d_device == _f->d3d_device)
	{
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
			add_scene_atom(t->binding(), shader, tex_fmt, sampler, vertices, std::size(vertices), indexes,
			               std::size(indexes), t->_cs);
		}
	}
}

void d3d11_draw_context_impl::draw_texture(const texture_d3d11_ptr& t, const quadd& dst, const recti src,
                                           const ui::color c, const ui::texture_sampler sampler)
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());

	if (!t) return;

	df::assert_true(t->_f->d3d_device == _f->d3d_device);

	const auto alpha = c.a;

	if (alpha >= 0.01f && t->_f->d3d_device == _f->d3d_device)
	{
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
			add_scene_atom(t->binding(), shader, tex_fmt, sampler, vertices, std::size(vertices), indexes,
			               std::size(indexes), t->_cs);
		}
	}
}


static rectd make_rectd(double x1, double y1, const double x2, const double y2)
{
	return {x1, y1, x2 - x1, y2 - y1};
}


static void add_rect(vertex_2d*& vv, WORD*& ii, const rectd& dst, const rectd& src, const float a,
                     const int vert_offset)
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
                                  const sizei client_extent, const int sxy, const float a)
{
	const auto tex_dims = texture->dimensions();
	const sized norm(tex_dims.cx * 2, tex_dims.cy * 2);

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

	constexpr auto expected_num_bars = 64;
	constexpr auto shadow_vertex_count = 4 * 8;
	constexpr auto shadow_index_count = 6 * 8;
	constexpr auto rect_verex_count = 5;
	constexpr auto rect_index_count = 12;

	constexpr auto visualizer_vertex_count = (shadow_vertex_count + rect_verex_count) * expected_num_bars;
	constexpr auto visualizer_index_count = (shadow_index_count + rect_index_count) * expected_num_bars;

	if (num_bars != expected_num_bars)
		return;

	// The canvas is released by destroy(), and the shadow texture is only loaded once the device is ready.
	// The device is checked explicitly because the buffer builds below dereference it directly, and after a
	// downgrade to software the canvas can still be present with no device behind it.
	if (!_canvas || !_canvas->_shadow || !_canvas->_shadow->is_valid() || !_canvas->_f ||
		!_canvas->_f->d3d_device || !_canvas->_f->d3d_context)
		return;

	// ~118KB of the UI thread's 1MB stack (vertex_2d is 48 bytes). Safe while this stays a leaf
	// call; move to a member buffer if a deeper call chain ever lands underneath it.
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
		_canvas->_shadow->binding(),
		_canvas->_pixel_shader_rgb.Get(),
		ui::texture_format::RGB,
		ui::texture_sampler::point,
		0,
		static_cast<uint32_t>(bar_vert_start),
		0,
		static_cast<uint32_t>(bar_ind_start)
	};


	scene_atom bar_atom = {
		{},
		_canvas->_pixel_shader_solid.Get(),
		ui::texture_format::None,
		ui::texture_sampler::point,
		static_cast<uint32_t>(bar_vert_start),
		static_cast<uint32_t>(vertex_count - bar_vert_start),
		static_cast<uint32_t>(bar_ind_start),
		static_cast<uint32_t>(index_count - bar_ind_start)
	};

	_scene_atoms.clear();
	_scene_atoms.emplace_back(std::move(shadow_atom));
	_scene_atoms.emplace_back(std::move(bar_atom));
}


// The one hardware decoder this renderer can present from. Frames must arrive as D3D11 array
// textures for update() to share them with the render device, so any other hwaccel the codec
// advertises - dxva2 among them - is deliberately not selected.
av_hw_decode_target av_platform_hw_decode_target()
{
	return {AV_HWDEVICE_TYPE_D3D11VA, AV_PIX_FMT_D3D11};
}

// Scoped hold of the FFmpeg D3D11VA device lock. The producer-side copy must run under it,
// but it is released as early as possible (and on every error path) so decoding on the worker
// thread is not serialised behind the render-device work that follows.
class scoped_hwctx_lock final : df::no_copy
{
	AVD3D11VADeviceContext* _ctx;

public:
	explicit scoped_hwctx_lock(AVD3D11VADeviceContext* ctx) : _ctx(ctx)
	{
		_ctx->lock(_ctx->lock_ctx);
	}

	~scoped_hwctx_lock() override
	{
		unlock();
	}

	void unlock()
	{
		if (_ctx)
		{
			_ctx->unlock(_ctx->lock_ctx);
			_ctx = nullptr;
		}
	}
};

// The keyed-mutex handoff below runs on the UI thread, so the wait is capped well inside a frame.
// Both keys are owned by this one function, so an uncontended acquire returns immediately.
constexpr uint32_t shared_texture_acquire_ms = 8;

ui::texture_update_result d3d11_texture::update(const av_frame_ptr& frame_in)
{
	df::scope_rendering_func rf(__FUNCTION__);

	auto result = ui::texture_update_result::failed;
	auto info = av_get_d3d_info(frame_in);
	_cs = info.color_space;

	if (info.tex)
	{
		// https://stackoverflow.com/questions/56863430/how-to-copy-texture-from-context-to-another-context-inside-gpu
		// https://github.com/videolan/vlc/blob/fd72480dfdb3eb30cddb7a06cef60d6b5c29828d/doc/libvlc/d3d11_player.cpp
		const sizei src_extent = {(info.width), (info.height)};
		bool shared_texture_valid = false;
		auto* frames_ctx = info.ctx;

		// The frame must carry a usable D3D11VA device context, and the renderer must still
		// have a live device; without either there is nothing to bridge and the caller falls
		// back to CPU scaling below.
		auto* const device_hwctx = frames_ctx && frames_ctx->device_ctx
			                           ? std::bit_cast<AVD3D11VADeviceContext*>(frames_ctx->device_ctx->hwctx)
			                           : nullptr;

		if (!device_hwctx || !device_hwctx->lock || !device_hwctx->unlock || !_f->d3d_device || !_f->d3d_context)
		{
			return ui::texture_update_result::failed;
		}

		// Hold the FFmpeg device lock for the producer-side work only, and release it on every
		// exit path (including the early returns below).
		scoped_hwctx_lock hwctx_lock(device_hwctx);

		auto device = _f->d3d_device;
		auto context = _f->d3d_context;

		ComPtr<ID3D11Texture2D> video_texture = info.tex;
		const auto video_texture_index = info.tex_index;

		ComPtr<ID3D11Device> video_device;
		ComPtr<ID3D11DeviceContext> video_context;

		video_texture->GetDevice(&video_device);

		if (!video_device)
		{
			return ui::texture_update_result::failed;
		}

		video_device->GetImmediateContext(&video_context);

		if (!video_context)
		{
			return ui::texture_update_result::failed;
		}

		D3D11_TEXTURE2D_DESC tex_desc_src = {};
		video_texture->GetDesc(&tex_desc_src);

		sizei texture_extent = {static_cast<int>(tex_desc_src.Width), static_cast<int>(tex_desc_src.Height)};

		auto video_tex_format = tex_desc_src.Format == DXGI_FORMAT_P010
			                        ? ui::texture_format::P010
			                        : ui::texture_format::NV12;

		if (video_texture_index >= tex_desc_src.ArraySize)
		{
			df::log(__FUNCTION__, std::format("Video texture index {} exceeds array size {}", video_texture_index,
			                                  tex_desc_src.ArraySize));
			return ui::texture_update_result::failed;
		}

		// The decode device is part of the key: a second video decodes on its own FFmpeg device,
		// and reusing a producer copy owned by the previous device makes CopySubresourceRegion a
		// cross-device call that the runtime rejects, leaving stale frames on screen.
		if (!_shared_texture || !_shared_texture_render || _shared_texture_device != video_device ||
			_shared_texture_dimensions != texture_extent ||
			_shared_texture_format != video_tex_format)
		{
			_shared_texture.Reset();
			_shared_texture_render.Reset();
			_shared_producer_mutex.Reset();
			_shared_consumer_mutex.Reset();
			_shared_texture_device.Reset();

			D3D11_TEXTURE2D_DESC texDesc = tex_desc_src;
			texDesc.BindFlags = D3D11_BIND_DECODER;
			texDesc.Usage = D3D11_USAGE_DEFAULT;
			texDesc.ArraySize = 1;
			texDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

			ComPtr<ID3D11Texture2D> shared_texture;
			auto hr = video_device->CreateTexture2D(&texDesc, nullptr, &shared_texture);

			// Open the shared texture on the render device once and cache the opened
			// resource plus both keyed mutexes. CreateSharedHandle / OpenSharedResource1
			// are expensive kernel operations - doing them here (only on resize/format
			// change) instead of per frame is the key win over the original code.
			ComPtr<IDXGIResource1> shared_resource;
			HANDLE shared_handle = nullptr;

			if (SUCCEEDED(hr)) hr = shared_texture.As(&shared_resource);
			if (SUCCEEDED(hr))
				hr = shared_resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ, nullptr, &shared_handle);

			ComPtr<ID3D11Device1> device1;
			if (SUCCEEDED(hr))
				hr = device->QueryInterface(__uuidof(ID3D11Device1), std::bit_cast<LPVOID*>(device1.GetAddressOf()));

			ComPtr<ID3D11Texture2D> render_texture;
			if (SUCCEEDED(hr))
				hr = device1->OpenSharedResource1(shared_handle, __uuidof(ID3D11Texture2D),
				                                  std::bit_cast<void**>(render_texture.GetAddressOf()));

			if (shared_handle) CloseHandle(shared_handle);

			ComPtr<IDXGIKeyedMutex> producer_mutex;
			ComPtr<IDXGIKeyedMutex> consumer_mutex;
			if (SUCCEEDED(hr)) hr = shared_texture.As(&producer_mutex);
			if (SUCCEEDED(hr)) hr = render_texture.As(&consumer_mutex);

			if (SUCCEEDED(hr))
			{
				_shared_texture = shared_texture;
				_shared_texture_render = render_texture;
				_shared_producer_mutex = producer_mutex;
				_shared_consumer_mutex = consumer_mutex;
				_shared_texture_dimensions = texture_extent;
				_shared_texture_format = video_tex_format;
				_shared_texture_device = video_device;
				_texture.Reset(); // force the render-side SRV texture to be recreated below

				// The decoder's surface pool is one texture array allocated in full when the stream
				// opens, and it is the largest single allocation playback makes. Reported here
				// because this is the only point that can see how many surfaces it really has.
				const int64_t bytes_per_sample = video_tex_format == ui::texture_format::P010 ? 2 : 1;
				const auto surface_bytes = static_cast<int64_t>(tex_desc_src.Width) * tex_desc_src.Height *
					3 * bytes_per_sample / 2;

				df::log(__FUNCTION__,
				        std::format("Video decode pool {} x {} x {} surfaces = {}", tex_desc_src.Width,
				                    tex_desc_src.Height, tex_desc_src.ArraySize,
				                    df::file_size(surface_bytes * tex_desc_src.ArraySize).str()));
			}
		}

		if (_shared_texture && _shared_producer_mutex)
		{
			// IDXGIKeyedMutex::AcquireSync returns WAIT_TIMEOUT (0x102) and WAIT_ABANDONED
			// (0x80) as *success* HRESULTs, so it must be tested against S_OK - anything else
			// means the mutex is not held and the copy below must not run.
			//
			// This runs on the UI thread, so the wait must stay well inside a frame. The producer
			// and consumer keys are handed back and forth by this one function, so an uncontended
			// acquire completes immediately; anything longer means the chain is wedged and the
			// recovery path below is the right answer, not a stall.
			if (_shared_producer_mutex->AcquireSync(0, shared_texture_acquire_ms) == S_OK)
			{
				video_context->CopySubresourceRegion(_shared_texture.Get(), 0, 0, 0, 0, video_texture.Get(),
				                                     static_cast<uint32_t>(video_texture_index), nullptr);
				video_context->Flush();
				shared_texture_valid = _shared_producer_mutex->ReleaseSync(1) == S_OK;

				if (!shared_texture_valid)
				{
					// The mutex is still held at key 0, so every later frame would block for the
					// full acquire timeout on the UI thread. Drop the chain and rebuild it.
					df::log(__FUNCTION__, "Video shared texture release failed - recreating");
					_shared_texture.Reset();
					_shared_texture_render.Reset();
					_shared_producer_mutex.Reset();
					_shared_consumer_mutex.Reset();
					_shared_texture_device.Reset();
					_shared_texture_format = ui::texture_format::None;
				}
			}
			else
			{
				// The mutex was left in an unexpected state (timeout or abandoned). Drop the
				// shared chain so the next frame rebuilds it instead of stalling video forever.
				df::log(__FUNCTION__, "Video shared texture mutex unavailable - recreating");
				_shared_texture.Reset();
				_shared_texture_render.Reset();
				_shared_producer_mutex.Reset();
				_shared_consumer_mutex.Reset();
				_shared_texture_device.Reset();
				_shared_texture_format = ui::texture_format::None;
			}
		}

		hwctx_lock.unlock();

		if (shared_texture_valid && _shared_texture_render && _shared_consumer_mutex)
		{
			// The producer released to key 1, so a failure here leaves the mutex held at a key nobody
			// waits on. Drop the chain rather than stalling the UI thread on every later frame.
			if (_shared_consumer_mutex->AcquireSync(1, shared_texture_acquire_ms) != S_OK)
			{
				df::log(__FUNCTION__, "Video shared texture consumer mutex unavailable - recreating");
				_shared_texture.Reset();
				_shared_texture_render.Reset();
				_shared_producer_mutex.Reset();
				_shared_consumer_mutex.Reset();
				_shared_texture_device.Reset();
				_shared_texture_format = ui::texture_format::None;
			}
			else
			{
				if (!_texture || _dimensions != _shared_texture_dimensions || _format != _shared_texture_format)
				{
					D3D11_TEXTURE2D_DESC tex_desc_render = {};
					_shared_texture_render->GetDesc(&tex_desc_render);

					D3D11_TEXTURE2D_DESC texDesc2 = tex_desc_render;
					texDesc2.BindFlags = D3D11_BIND_SHADER_RESOURCE;
					texDesc2.ArraySize = 1;
					texDesc2.MiscFlags = 0;

					ComPtr<ID3D11Texture2D> texture2;
					if (SUCCEEDED(device->CreateTexture2D(&texDesc2, nullptr, &texture2)))
					{
						context->CopyResource(texture2.Get(), _shared_texture_render.Get());

						_texture = texture2;
						_dimensions = _shared_texture_dimensions;
						_src_extent = src_extent;
						_format = _shared_texture_format;
						_orientation = info.orientation;
						result = ui::texture_update_result::tex_created;
					}
				}
				else
				{
					context->CopyResource(_texture.Get(), _shared_texture_render.Get());
					_src_extent = src_extent;
					result = ui::texture_update_result::tex_updated;
				}

				// Symmetrical with the producer release above: a failed release leaves the mutex held
				// at key 1, which the producer never waits on, so every later frame would time out.
				if (_shared_consumer_mutex->ReleaseSync(0) != S_OK)
				{
					df::log(__FUNCTION__, "Video shared texture consumer release failed - recreating");
					_shared_texture.Reset();
					_shared_texture_render.Reset();
					_shared_producer_mutex.Reset();
					_shared_consumer_mutex.Reset();
					_shared_texture_device.Reset();
					_shared_texture_format = ui::texture_format::None;
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


sizei d3d11_draw_context_impl::measure_string(const std::string_view text, const sizei s,
                                              const ui::style::font_face font,
                                              const ui::style::text_style style)
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());
	return _font[font].measure_text(text, s, style);
}

int d3d11_draw_context_impl::line_height(const ui::style::font_face font)
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());
	return _font[font].line_height();
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void log_update_texture_crash(const ui::texture_format fmt)
{
	df::log(__FUNCTION__, std::format("UpdateSubresource {} ****** crashed ******", to_string(fmt)));
}

// Only swallow access violations from the known-problematic YUV texture driver path (see the
// Chromium gpu_driver_bug_list reference below); let every other exception code - and all
// non-YUV faults - propagate to the global crash handler so real bugs are not hidden.
static int yuv_upload_seh_filter(const bool is_yuv, const unsigned int code)
{
	return (is_yuv && code == EXCEPTION_ACCESS_VIOLATION) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

// Fall back to RGB video/JPEG rendering after a YUV texture driver fault. Persist the choice
// immediately (via the same backend the app reads at startup) so the fallback survives even if
// the app later crashes before a clean shutdown - mirroring the graphics crash-guard durability.
static void disable_yuv_textures()
{
	setting.use_yuv = false;
	setting.write();
}

static HRESULT try_create_tex(ID3D11Device* pDevice, const D3D11_TEXTURE2D_DESC& desc,
                              const D3D11_SUBRESOURCE_DATA* p_source,
                              ID3D11Texture2D** t)
{
	const auto is_yuv = desc.Format == DXGI_FORMAT_NV12 || desc.Format == DXGI_FORMAT_P010;

	__try
	{
		return pDevice->CreateTexture2D(&desc, p_source, t);
	}
	__except (yuv_upload_seh_filter(is_yuv, GetExceptionCode()))
	{
		// Ensure we don't return a partially initialized texture on exception
		if (t && *t)
		{
			(*t)->Release();
			*t = nullptr;
		}
		disable_yuv_textures();
		df::log(__FUNCTION__, "Exception caught in CreateTexture2D, YUV disabled");
	}

	return E_FAIL;
}

static HRESULT try_update_tex(ID3D11DeviceContext* context, ID3D11Texture2D* texture, const sizei texture_dimensions,
                              const ui::texture_format fmt, const uint8_t* pixels, const size_t stride,
                              const size_t buffer_size)
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
	__except (yuv_upload_seh_filter(is_yuv, GetExceptionCode()))
	{
		log_update_texture_crash(fmt);
		disable_yuv_textures();
	}

	return E_FAIL;
}

ui::texture_update_result d3d11_texture::update(const sizei dims, const ui::texture_format fmt,
                                                const ui::orientation orientation, const uint8_t* pixels,
                                                const size_t stride, const size_t buffer_size)
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
		const auto has_valid_dims = cx == (cx & ~1) && cy == (cy & ~1) && cx >= 2 && cy >= 2;

		df::assert_true(has_valid_dims);

		if (!has_valid_dims)
		{
			return result;
		}
	}

	auto* const device = _f->d3d_device.Get();
	auto* const context = _f->d3d_context.Get();

	if (!device || !context)
	{
		// downgrade_to_software() releases the device, so this can be reached with a null
		// device while textures are still being updated. The access violation would be raised
		// inside try_create_tex, where yuv_upload_seh_filter would misread it as a driver
		// fault and durably clear setting.use_yuv.
		return result;
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

	const D3D11_SUBRESOURCE_DATA* p_source = nullptr;

	if (pixels && stride)
	{
		p_source = &source_data;
	}

	if (!_texture || _format != fmt || dims != _dimensions)
	{
		ComPtr<ID3D11Texture2D> t;
		const auto hr = try_create_tex(device, desc, p_source, &t);

		if (SUCCEEDED(hr))
		{
			_texture = t;
			_dimensions = {cx, cy};
			_format = fmt;
			df::bump(df::gpu_perf.textures_created);
			result = SUCCEEDED(hr) ? ui::texture_update_result::tex_created : ui::texture_update_result::failed;
		}
		else
		{
			if (hr == E_FAIL)
			{
				df::log(__FUNCTION__, std::format("CreateTexture2D {} ({} x {}) ****** crashed ******",
				                                  to_string(fmt), cx, cy));
			}
			else
			{
				df::log(__FUNCTION__,
				        std::format("CreateTexture2D {} ({} x {}) failed: {:x}", to_string(fmt), cx, cy,
				                    static_cast<uint32_t>(hr)));
			}
		}
	}
	else
	{
		const auto hr = try_update_tex(context, _texture.Get(), _dimensions, _format, pixels, stride,
		                               buffer_size);
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
		_cs = s->color_space();

		// Surfaces decoded before setting.use_yuv was turned off are still cached, so convert
		// them here rather than ask a driver that has already faulted for another YUV texture.
		if (!setting.use_yuv && (fmt == ui::texture_format::NV12 || fmt == ui::texture_format::P010))
		{
			if (!_scaler) _scaler = std::make_unique<av_scaler>();

			const auto converted = std::make_shared<ui::surface>();

			if (!_scaler->convert_yuv_surface(*s, converted))
			{
				return ui::texture_update_result::failed;
			}

			return update(converted->dimensions(), converted->format(), converted->orientation(),
			              converted->pixels(), converted->stride(), converted->size());
		}

		return update(s->dimensions(), fmt, s->orientation(), s->pixels(), s->stride(), s->size());
	}

	return ui::texture_update_result::failed;
}

// A mip chain is what lets the panorama shader minify thousands of pixels of sphere into a viewport
// without aliasing, and it is the reason the projection needs packed pixels: the planar video
// formats are neither render targets nor mipmappable, so GenerateMips has nothing to work with.
ui::texture_update_result d3d11_texture::update_mipped(const ui::const_surface_ptr& s)
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());

	if (!ui::is_valid(s) || !ui::is_packed(s->format())) return ui::texture_update_result::failed;

	auto* const device = _f ? _f->d3d_device.Get() : nullptr;
	auto* const context = _f ? _f->d3d_context.Get() : nullptr;

	if (!device || !context) return ui::texture_update_result::failed;

	const auto dims = s->dimensions();

	if (dims.cx < 1 || dims.cy < 1) return ui::texture_update_result::failed;

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = dims.cx;
	desc.Height = dims.cy;
	// Zero asks for the full chain down to 1x1, which the runtime allocates and GenerateMips fills.
	desc.MipLevels = 0;
	desc.ArraySize = 1;
	// Always the alpha format: the chain is built by rendering, and the alpha-less variant is not
	// guaranteed to be a render target. The shader ignores the sampled alpha for the same reason a
	// packed decode of an opaque photograph cannot be trusted to carry one.
	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

	ComPtr<ID3D11Texture2D> t;

	// No initial data: a request for the full chain would need every level supplied, so level 0 is
	// written afterwards and the rest generated from it.
	if (FAILED(try_create_tex(device, desc, nullptr, &t)) || !t)
	{
		return ui::texture_update_result::failed;
	}

	context->UpdateSubresource(t.Get(), 0, nullptr, s->pixels(), static_cast<UINT>(s->stride()), 0);

	_texture = std::move(t);
	_format = ui::texture_format::ARGB;
	_dimensions = dims;
	_orientation = s->orientation();
	_cs = s->color_space();
	_src_extent = {};

	const auto& b = binding();

	if (!b)
	{
		// A published texture with no view is one nothing can generate mips for, and the next caller
		// would sample level zero through an anisotropic sampler and alias without saying why.
		_texture.Reset();
		_format = ui::texture_format::None;
		_dimensions = {};
		return ui::texture_update_result::failed;
	}

	context->GenerateMips(b.y.Get());

	return ui::texture_update_result::tex_created;
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
			df::bump(df::gpu_perf.textures_created);
		}
	}
}

// Cap the glyph atlas so a pathological font size cannot grow it without bound.
constexpr uint32_t max_atlas_xy = 4096u;
constexpr uint32_t initial_atlas_xy = 256u;

d3d11_text_renderer::coords d3d11_text_renderer::find_glyph(const uint16_t c, const DWRITE_GLYPH_RUN* glyph_run)
{
	df::scope_rendering_func rf(__FUNCTION__);
	coords result = {0, 0, 0, 0, 0};

	if (!glyph_run)
	{
		return result;
	}

	if (!_texture)
	{
		create_a8_texture(initial_atlas_xy);
	}

	if (!_texture)
	{
		return result;
	}

	const auto key = _glyph_keys.key(glyph_run->fontFace, glyph_run->fontEmSize, c);
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

			// The atlas only grows on the vertical test below, so a glyph that is itself wider or
			// taller than the atlas would still be written through a D3D11_BOX outside the
			// resource. Refuse to cache it rather than issue an out-of-bounds UpdateSubresource.
			if (cx > static_cast<int>(max_atlas_xy) || cy > static_cast<int>(max_atlas_xy))
			{
				df::log(__FUNCTION__, std::format("Glyph {}x{} exceeds the font atlas - not cached", cx, cy));
				return result;
			}

			if (_next_location.x + cx > static_cast<int>(_xy_tex))
			{
				_next_location.x = 0;
				_next_location.y += _line_height;
			}

			// Grow while either axis still cannot hold this glyph; the loop below caps at max_atlas_xy.
			while (_texture && (cx > static_cast<int>(_xy_tex) || _next_location.y + cy > static_cast<int>(_xy_tex)))
			{
				const auto grown = std::min(_xy_tex * 2u, max_atlas_xy);

				if (grown <= _xy_tex) break;

				_coords.clear();
				_texture = nullptr;
				create_a8_texture(grown);
			}

			if (_next_location.y + _line_height > static_cast<int>(_xy_tex)) // Out of room
			{
				const auto new_size = std::min(_xy_tex * 2u, max_atlas_xy);
				if (new_size <= _xy_tex)
				{
					df::log(__FUNCTION__, "Font texture atlas reached maximum size, glyph rendering may fail");
					return result; // Return empty coordinates if we can't grow further
				}

				_coords.clear();
				_texture = nullptr;
				create_a8_texture(new_size);
			}

			if (!_texture || _next_location.x + cx > static_cast<int>(_xy_tex) ||
				_next_location.y + cy > static_cast<int>(_xy_tex))
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

void d3d11_text_renderer::draw_text(const std::shared_ptr<text_layout_impl>& text, const recti bounds,
                                    const ui::color clr, const ui::color bg)
{
	df::scope_rendering_func rf(__FUNCTION__);
	_clr = clr;
	text->_renderer->draw(_canvas.get(), this, text->_layout.Get(), bounds, clr, bg);
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
	_glyph_keys.clear();
	_chars_to_glyphs.clear();
	_texture.Reset();
	_spacing = 0;
	_xy_tex = 0;
	_line_height = 0;
	_base_line_height = 0;
	_next_location.x = 0;
	_next_location.y = 0;
}

void d3d11_text_renderer::draw_text(const std::string_view text, const recti bounds,
                                    const ui::style::text_style style, const ui::color c, const ui::color bg)
{
	df::scope_rendering_func rf(__FUNCTION__);
	_clr = c;

	if (_font)
	{
		_font->draw(_canvas.get(), this, text, bounds, style, c, bg);
	}
}

void d3d11_text_renderer::draw_text(const std::string_view text, const std::vector<ui::text_highlight_t>& highlights,
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
		_font->draw(_canvas.get(), this, w, bounds, style, clr, bg, _highlights);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////


HRESULT d3d11_text_renderer::GetPixelsPerDip(void* clientDrawingContext, FLOAT* pixelsPerDip)
{
	{
		*pixelsPerDip = 96.0;
	}

	return S_OK;
}

HRESULT d3d11_text_renderer::DrawGlyphRun(void* clientDrawingContext, const FLOAT baselineOriginX,
                                          const FLOAT baselineOriginY,
                                          DWRITE_MEASURING_MODE measuringMode, const DWRITE_GLYPH_RUN* glyphRun,
                                          const DWRITE_GLYPH_RUN_DESCRIPTION* glyphRunDescription,
                                          IUnknown* clientDrawingEffect)
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());

	if (glyphRun && _canvas)
	{
		const auto client_extent = _canvas->client_extent();
		auto char_pos = glyphRunDescription ? glyphRunDescription->textPosition : 0;
		auto i_highlights = _highlights.begin();

		const pointd screen_scale(client_extent.cx, client_extent.cy);
		// find_glyph can grow the atlas, which replaces the texture and invalidates the scale used for
		// every vertex staged so far, so this is tracked and flushed before the change is observed.
		auto tex_scale = pointd(_xy_tex, _xy_tex);

		// ~102KB of the UI thread's 1MB stack (vertex_2d is 48 bytes). Safe while this stays a leaf
		// call; move to a member buffer if a deeper call chain ever lands underneath it.
		vertex_2d vertices[max_vert_count];
		WORD indexes[max_index_count];

		constexpr auto vert_limit = max_vert_count - 4;
		constexpr auto index_limit = max_index_count - 6;

		auto vertex_count = 0;
		auto index_count = 0;
		const auto ty = floor(baselineOriginY + 0.5f);
		constexpr auto limit_char = -1;
		auto tx = 0.0f; // -_spacing;
		const auto is_left_to_right = (glyphRun->bidiLevel & 0x01) == 0;

		const auto len = glyphRun->glyphCount;
		auto t = _texture;
		// Held by value for the same reason as tex_scale: growing the atlas replaces both, and the
		// vertices staged so far belong to the atlas they were measured against.
		auto tb = atlas_binding();

		for (auto i = 0u; i < len; ++i)
		{
			const auto c = glyphRun->glyphIndices[i];
			const auto ax = glyphRun->glyphAdvances[i];
			const auto coord = find_glyph(c, glyphRun);

			if (t != _texture)
			{
				if (tb && index_count > 0)
				{
					_canvas->add_scene_atom(tb, _canvas->_pixel_shader_font, ui::texture_format::RGB,
					                        ui::texture_sampler::point, vertices, vertex_count, indexes,
					                        index_count);
				}

				vertex_count = 0;
				index_count = 0;
				t = _texture;
				tb = atlas_binding();
				tex_scale = pointd(_xy_tex, _xy_tex);
			}

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

			const auto tx1 = coord.tx1;
			const auto ty1 = coord.ty1;
			const auto tx2 = coord.tx2;
			const auto ty2 = coord.ty2;

			const auto w = tx2 - tx1;
			const auto h = ty2 - ty1;

			if (w > 0 && h > 0)
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

				const auto left_color = clr;
				const auto right_color = i == limit_char ? ui::color{} : clr;
				const auto pos = vertex_count;

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

				if (index_count > index_limit || vertex_count > vert_limit)
				{
					if (tb)
					{
						_canvas->add_scene_atom(tb, _canvas->_pixel_shader_font, ui::texture_format::RGB,
						                        ui::texture_sampler::point, vertices, vertex_count, indexes,
						                        index_count);
					}

					vertex_count = 0;
					index_count = 0;
				}
			}

			tx += ax;
			char_pos += 1;
		}

		// The remainder is flushed here rather than on the last iteration: the last glyph in a
		// run may contribute no geometry, and folding the flush into that case discarded it.
		if (tb && index_count > 0)
		{
			_canvas->add_scene_atom(tb, _canvas->_pixel_shader_font, ui::texture_format::RGB,
			                        ui::texture_sampler::point, vertices, vertex_count, indexes,
			                        index_count);
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

	// create_texture_from_resource always returns an object, so the decode has to be confirmed here:
	// an unloaded texture reports zero dimensions and build_shadow_vertices would divide by them.
	if (texture && texture->is_valid())
	{
		build_shadow_vertices(vertices, indexes, texture, dst, _client_extent, sxy, alpha);
		add_scene_atom(texture->binding(), _pixel_shader_rgb, ui::texture_format::RGB, ui::texture_sampler::point,
		               vertices, std::size(vertices), indexes, std::size(indexes));
	}
}

void d3d11_draw_context_impl::draw_text(const std::string_view textA, const recti bounds,
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

void d3d11_draw_context_impl::draw_text(const std::string_view text,
                                        const std::vector<ui::text_highlight_t>& highlights, const recti bounds,
                                        const ui::style::font_face font, const ui::style::text_style style,
                                        const ui::color clr,
                                        const ui::color bg)
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());

	if (_clip_bounds.intersects(bounds))
	{
		_font[font].draw_text(text, highlights, bounds, style, clr, bg);
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

		if (t)
		{
			_font[t->_font].draw_text(t, bounds, clr, bg);
		}
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

		add_scene_atom({}, _pixel_shader_solid, ui::texture_format::None, ui::texture_sampler::point, vertices,
		               std::size(vertices), indexes, std::size(indexes));
	}
}

recti d3d11_draw_context_impl::clip_bounds() const
{
	return _clip_bounds;
}

void d3d11_draw_context_impl::clip_bounds(const recti bounds)
{
	_clip_stack.emplace_back(_clip_bounds);
	_clip_bounds = _clip_bounds.intersection(bounds);
}

void d3d11_draw_context_impl::restore_clip()
{
	if (!_clip_stack.empty())
	{
		_clip_bounds = _clip_stack.back();
		_clip_stack.pop_back();
	}
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

		add_scene_atom({}, _pixel_shader_circle, ui::texture_format::None, ui::texture_sampler::point, vertices,
		               std::size(vertices), indexes, std::size(indexes));
	}
}

void d3d11_draw_context_impl::draw_rect(const recti bounds, const ui::color c)
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());

	if (c.a < 0.01f) return;

	const auto r = static_cast<quadd>(bounds).scale(_client_extent);

	const vertex_2d vertices[] = {

		vertex_2d(r[0], c),
		vertex_2d(r[1], c),
		vertex_2d(r[2], c),
		vertex_2d(r[3], c),
	};

	constexpr WORD indexes[] = {

		0, 1, 2,
		2, 3, 0
	};

	add_scene_atom({}, _pixel_shader_solid, ui::texture_format::None, ui::texture_sampler::point, vertices,
	               std::size(vertices), indexes, std::size(indexes));
}

// Four-triangle fan from the centre vertex: the rasteriser interpolates c_centre at the middle to
// c_corner at the four corners. software_canvas::fill_rect_gradient reproduces this on the CPU.
void d3d11_draw_context_impl::draw_rect_gradient(const recti bounds, const ui::color c_centre,
                                                 const ui::color c_corner)
{
	df::scope_rendering_func rf(__FUNCTION__);
	df::assert_true(ui::is_ui_thread());

	const auto dst = static_cast<quadd>(bounds);

	if (c_centre.a >= 0.01f || c_corner.a >= 0.01f)
	{
		const auto r = dst.scale(_client_extent);
		const auto center = r.center_point();

		const vertex_2d vertices[] = {

			vertex_2d(r[0], c_corner),
			vertex_2d(r[1], c_corner),
			vertex_2d(center, c_centre),
			vertex_2d(r[2], c_corner),
			vertex_2d(r[3], c_corner),
		};

		constexpr WORD indexes[] = {

			0, 1, 2,
			1, 3, 2,
			3, 4, 2,
			4, 0, 2
		};

		add_scene_atom({}, _pixel_shader_solid, ui::texture_format::None, ui::texture_sampler::point, vertices,
		               std::size(vertices), indexes, std::size(indexes));
	}
}

void d3d11_draw_context_impl::draw_texture(const ui::texture_ptr& t, const recti dst, const float alpha,
                                           const ui::texture_sampler sampler)
{
	if (!t) return;
	draw_texture(t, dst, recti(pointi(0, 0), t->dimensions()), alpha, sampler);
}

void d3d11_draw_context_impl::draw_texture(const ui::texture_ptr& t, const quadd& dst, const recti src,
                                           const float alpha, const ui::texture_sampler sampler)
{
	const auto tt = std::dynamic_pointer_cast<d3d11_texture>(t);
	draw_texture(tt, dst, src, ui::color::from_a(alpha), sampler);
}

void d3d11_draw_context_impl::draw_texture(const ui::texture_ptr& t, const quadd& dst, const recti src,
                                           const float alpha, const ui::texture_sampler sampler,
                                           const ui::texture_transform& transform)
{
	const auto tt = std::dynamic_pointer_cast<d3d11_texture>(t);
	if (!tt || alpha < 0.01f) return;

	auto d = dst.scale(_client_extent);
	const auto dimensions = tt->dimensions();
	const auto width = static_cast<float>(dimensions.cx);
	const auto height = static_cast<float>(dimensions.cy);
	const auto color = ui::color::from_a(alpha);
	const vertex_2d vertices[] =
	{
		vertex_2d(d[0], {src.left / width, src.top / height}, color, dimensions),
		vertex_2d(d[1], {src.right / width, src.top / height}, color, dimensions),
		vertex_2d(d[2], {src.right / width, src.bottom / height}, color, dimensions),
		vertex_2d(d[3], {src.left / width, src.bottom / height}, color, dimensions),
	};
	constexpr WORD indexes[] = {0, 1, 2, 3, 0, 2};
	const auto shader = calc_shader(sampler == ui::texture_sampler::bicubic, tt->_format);

	if (!_last_transform || *_last_transform != transform)
	{
		_last_transform = std::make_shared<ui::texture_transform>(transform);
	}

	add_scene_atom(tt->binding(), shader, tt->_format, sampler, vertices, std::size(vertices), indexes,
	               std::size(indexes), tt->_cs, _last_transform);
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

	if (vv && vv->_vertex_buffer && vv->_canvas)
	{
		df::assert_true(vv->_canvas->_f->d3d_device == _f->d3d_device);

		if (vv->_canvas->_f->d3d_device == _f->d3d_device)
		{
			scene_atom sa;
			sa.verts = vv;
			// The software backend clips the audio bars, so the GPU path must too or the two
			// backends disagree wherever the visualizer is drawn inside a clipped element.
			sa.clip_bounds = _clip_bounds;
			sa.has_clip = !_clip_stack.empty();
			_scene_atoms.emplace_back(sa);
		}
	}
}

sizei d3d11_draw_context_impl::measure_text(const std::string_view text, const ui::style::font_face font,
                                            const ui::style::text_style style, const int width, const int height)
{
	return measure_string(text, {width, height}, font, style);
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

	if (!f || !f->d3d_device || !swap_chain)
	{
		return nullptr;
	}

	auto result = std::make_shared<d3d11_draw_context_impl>();
	result->create(f, swap_chain, base_font_size);
	return result->is_valid() ? result : nullptr;
}
