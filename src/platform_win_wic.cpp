// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Windows Imaging Component integration. Provides fallback image decoding
// for formats not handled by specialized decoders.

#include "pch.h"

#include "platform_win.h"
#include <wincodec.h>
#include <dwrite.h>

#include "app_text.h"
#include "files.h"

static void flip_buffer_vertically(uint32_t* buffer, const unsigned width, const unsigned height)
{
	if (!buffer || width == 0 || height <= 1)
	{
		return; // Nothing to flip or invalid parameters
	}

	// Check for potential overflow before computing stride
	if (width > SIZE_MAX / sizeof(uint32_t))
	{
		return; // Avoid overflow in stride calculation
	}

	const auto rows = height / 2;
	const auto stride = width * sizeof(uint32_t);

	const auto temp_row = df::unique_alloc<uint32_t*>(stride);

	if (temp_row)
	{
		for (uint32_t i = 0; i < rows; i++)
		{
			memcpy(temp_row.get(), buffer + i * width, stride);
			memcpy(buffer + i * width, buffer + (height - i - 1) * width, stride);
			memcpy(buffer + (height - i - 1) * width, temp_row.get(), stride);
		}
	}
}

HGLOBAL image_to_handle(const file_load_result& loaded)
{
	const auto dimensions = loaded.dimensions();

	// Validate dimensions to prevent integer overflow
	if (dimensions.cx <= 0 || dimensions.cy <= 0 ||
		dimensions.cx > 65536 || dimensions.cy > 65536)
	{
		df::log(__FUNCTION__, "Invalid image dimensions");
		throw std::invalid_argument("Invalid image dimensions");
	}

	BITMAPINFOHEADER bi;
	bi.biSize = sizeof(BITMAPINFOHEADER);
	bi.biWidth = dimensions.cx;
	bi.biHeight = dimensions.cy;
	bi.biPlanes = 1;
	bi.biBitCount = 32;
	bi.biCompression = BI_RGB;

	// Check for potential overflow in image size calculation
	const size_t pixel_count = static_cast<size_t>(dimensions.cx) * dimensions.cy;
	if (pixel_count > SIZE_MAX / 4)
	{
		df::log(__FUNCTION__, "Image too large, potential overflow");
		throw std::invalid_argument("Image too large");
	}

	bi.biSizeImage = static_cast<DWORD>(pixel_count * 4);

	const auto alloc_size = sizeof(bi) + bi.biSizeImage;
	auto* const h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, alloc_size);

	if (h == nullptr)
	{
		df::log(__FUNCTION__, "GlobalAlloc failed");
		throw std::bad_alloc();
	}

	auto* const buffer_out = static_cast<uint8_t*>(GlobalLock(h));

	if (buffer_out == nullptr)
	{
		df::log(__FUNCTION__, "GlobalLock failed");
		throw std::bad_alloc();
	}

	memcpy_s(buffer_out, alloc_size, &bi, sizeof(bi));

	const auto s = loaded.to_surface();

	if (is_valid(s))
	{
		const auto stride_out = dimensions.cx * 4_z;
		const auto stride_in = s->stride();
		const auto copy_len = std::min(stride_out, stride_in);
		const auto* const pixels_in = s->pixels();
		auto* const pixels_out = buffer_out + sizeof(bi);

		// Validate buffer bounds
		if (pixels_in == nullptr || pixels_out == nullptr)
		{
			df::log(__FUNCTION__, "Invalid pixel buffer pointers");
			GlobalUnlock(h);
			GlobalFree(h);
			throw std::invalid_argument("Invalid pixel buffers");
		}

		for (auto y = 0; y < dimensions.cy; ++y)
		{
			const auto src_offset = stride_in * y;
			const auto dest_offset = stride_out * y;

			// Bounds checking for buffer operations
			if (src_offset + copy_len > s->size() ||
				dest_offset + copy_len > bi.biSizeImage)
			{
				df::log(__FUNCTION__, "Buffer bounds exceeded");
				break;
			}

			memcpy(pixels_out + dest_offset, pixels_in + src_offset, copy_len);
		}

		flip_buffer_vertically(reinterpret_cast<uint32_t*>(pixels_out), dimensions.cx, dimensions.cy);
	}

	GlobalUnlock(h);

	return h;
}

static CLSID wic_encoder_clsid(const std::string_view format)
{
	CLSID result = {};

	static const df::hash_map<std::string_view, CLSID, df::ihash, df::ieq> extensions
	{
		{".jpg", GUID_ContainerFormatJpeg},
		{".jpeg", GUID_ContainerFormatJpeg},
		{".jpe", GUID_ContainerFormatJpeg},
		{".png", GUID_ContainerFormatPng},
		{".tiff", GUID_ContainerFormatTiff},
		{".tif", GUID_ContainerFormatTiff},
		{".gif", GUID_ContainerFormatGif},
		{".bmp", GUID_ContainerFormatBmp},
		{".webp", GUID_ContainerFormatWebp},
		{"jpg", GUID_ContainerFormatJpeg},
		{"jpeg", GUID_ContainerFormatJpeg},
		{"jpe", GUID_ContainerFormatJpeg},
		{"png", GUID_ContainerFormatPng},
		{"tiff", GUID_ContainerFormatTiff},
		{"tif", GUID_ContainerFormatTiff},
		{"gif", GUID_ContainerFormatGif},
		{"bmp", GUID_ContainerFormatBmp},
		{"webp", GUID_ContainerFormatWebp},
		{"heic", GUID_ContainerFormatHeif},
	};

	const auto found = extensions.find(format.substr(df::find_ext(format)));
	return found == extensions.end() ? GUID_ContainerFormatPng : found->second;
}

platform::file_op_result save_bitmap_info(const df::folder_path save_path, const std::string_view name,
                                          const bool as_png, const HBITMAP image_buffer_in)
{
	platform::file_op_result result;

	ComPtr<IWICImagingFactory> wic;
	ComPtr<IWICBitmap> decoder;

	auto hr = CoCreateInstance(
		CLSID_WICImagingFactory,
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&wic));

	if (SUCCEEDED(hr))
	{
		hr = wic->CreateBitmapFromHBITMAP(
			image_buffer_in,
			nullptr,
			WICBitmapIgnoreAlpha,
			&decoder
		);
	}

	if (SUCCEEDED(hr))
	{
		auto i = 2;
		const auto folder = save_path;
		const auto ext = as_png ? "png" : "jpg";

		df::file_path path(folder, name, ext);
		const auto encoder_format = wic_encoder_clsid(ext);
		constexpr int max_file_name = 100;

		// Validate path creation to prevent infinite loops
		if (name.empty())
		{
			result.error_message = "Invalid filename";
			return result;
		}

		while (path.exists() && i < max_file_name)
		{
			path = df::file_path(folder, std::format("{}{}", name, i++), ext);
		}

		if (i >= max_file_name)
		{
			result.error_message = "Unable to create unique filename after 100 attempts";
			return result;
		}
		{
			ComPtr<IWICStream> piFileStream;
			hr = wic->CreateStream(&piFileStream);

			if (SUCCEEDED(hr))
			{
				const auto w = platform::to_file_system_path(path);
				hr = piFileStream->InitializeFromFilename(w.c_str(), GENERIC_WRITE);
			}

			ComPtr<IWICBitmapEncoder> piEncoder;

			if (SUCCEEDED(hr))
			{
				hr = wic->CreateEncoder(encoder_format, nullptr, &piEncoder);
			}

			if (SUCCEEDED(hr))
			{
				hr = piEncoder->Initialize(piFileStream.Get(), WICBitmapEncoderNoCache);
			}

			WICPixelFormatGUID pixelFormat = {0};
			UINT width, height = 0;
			ComPtr<IWICBitmapFrameEncode> piFrameEncode;

			if (SUCCEEDED(hr))
			{
				hr = piEncoder->CreateNewFrame(&piFrameEncode, nullptr);
			}

			if (SUCCEEDED(hr))
			{
				hr = piFrameEncode->Initialize(nullptr);
			}
			if (SUCCEEDED(hr))
			{
				hr = decoder->GetSize(&width, &height);
			}
			if (SUCCEEDED(hr))
			{
				hr = piFrameEncode->SetSize(width, height);
			}
			if (SUCCEEDED(hr))
			{
				double dpiX, dpiY = 0.0;
				const auto hr2 = decoder->GetResolution(&dpiX, &dpiY);

				if (SUCCEEDED(hr2) && dpiX > 0.0)
				{
					hr = piFrameEncode->SetResolution(dpiX, dpiY);
				}
			}

			if (SUCCEEDED(hr))
			{
				decoder->GetPixelFormat(&pixelFormat);
			}
			if (SUCCEEDED(hr))
			{
				hr = piFrameEncode->SetPixelFormat(&pixelFormat);
			}

			if (SUCCEEDED(hr))
			{
				hr = piFrameEncode->WriteSource(decoder.Get(), nullptr);
			}

			if (SUCCEEDED(hr))
			{
				hr = piFrameEncode->Commit();
			}

			piFrameEncode.Reset();

			if (SUCCEEDED(hr))
			{
				hr = piEncoder->Commit();
			}

			if (SUCCEEDED(hr))
			{
				hr = piFileStream->Commit(STGC_DEFAULT);
			}

			if (SUCCEEDED(hr))
			{
				result.created_files.files.emplace_back(path);
				result.code = platform::file_op_result_code::OK;
			}
		}
	}

	if (result.failed())
	{
		result.error_message = tt.error_save_image;
	}

	return result;
}


// Minimal DirectWrite text renderer that rasterises glyph coverage as opaque white
// (straight-alpha BGRA) into a ui::surface. Replaces the previous Direct2D DrawText path so
// that icon glyphs can be produced without any Direct2D dependency.
namespace
{
	class icon_glyph_renderer final : public IDWriteTextRenderer
	{
		IDWriteFactory* _factory = nullptr;
		ui::surface* _surface = nullptr;
		std::atomic<ULONG> _ref = 1;

	public:
		icon_glyph_renderer(IDWriteFactory* factory, ui::surface* surface) : _factory(factory), _surface(surface)
		{
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
		{
			if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWritePixelSnapping) ||
				riid == __uuidof(IDWriteTextRenderer))
			{
				*ppv = static_cast<IDWriteTextRenderer*>(this);
				AddRef();
				return S_OK;
			}

			*ppv = nullptr;
			return E_NOINTERFACE;
		}

		ULONG STDMETHODCALLTYPE AddRef() override { return ++_ref; }
		ULONG STDMETHODCALLTYPE Release() override { return --_ref; }

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

		HRESULT STDMETHODCALLTYPE DrawGlyphRun(void*, const FLOAT baselineOriginX, const FLOAT baselineOriginY,
		                                       DWRITE_MEASURING_MODE, const DWRITE_GLYPH_RUN* glyphRun,
		                                       const DWRITE_GLYPH_RUN_DESCRIPTION*, IUnknown*) override
		{
			if (!glyphRun || glyphRun->glyphCount == 0) return S_OK;

			ComPtr<IDWriteGlyphRunAnalysis> analysis;

			if (FAILED(_factory->CreateGlyphRunAnalysis(glyphRun, 1.0f, nullptr, DWRITE_RENDERING_MODE_NATURAL,
			                                            DWRITE_MEASURING_MODE_NATURAL, baselineOriginX, baselineOriginY,
			                                            &analysis)))
			{
				return S_OK;
			}

			RECT bounds{};
			if (FAILED(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds))) return S_OK;

			const auto w = bounds.right - bounds.left;
			const auto h = bounds.bottom - bounds.top;
			if (w <= 0 || h <= 0) return S_OK;

			std::vector<uint8_t> coverage(static_cast<size_t>(w) * h * 3);

			if (FAILED(analysis->CreateAlphaTexture(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds, coverage.data(),
			                                        static_cast<uint32_t>(coverage.size()))))
			{
				return S_OK;
			}

			const auto sw = static_cast<int>(_surface->width());
			const auto sh = static_cast<int>(_surface->height());

			for (auto y = 0; y < h; ++y)
			{
				const auto dy = bounds.top + y;
				if (dy < 0 || dy >= sh) continue;

				auto* const dst = _surface->pixels_line(dy);
				const auto* src = coverage.data() + static_cast<size_t>(y) * w * 3;

				for (auto x = 0; x < w; ++x, src += 3)
				{
					const auto dx = bounds.left + x;
					if (dx < 0 || dx >= sw) continue;

					const auto a = (src[0] + src[1] + src[2]) / 3;
					if (a == 0) continue;

					auto* const p = dst + static_cast<ptrdiff_t>(dx) * 4;
					const auto na = std::max<int>(a, p[3]);
					p[0] = 255;
					p[1] = 255;
					p[2] = 255;
					p[3] = static_cast<uint8_t>(na);
				}
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

		HRESULT STDMETHODCALLTYPE DrawInlineObject(void*, FLOAT, FLOAT, IDWriteInlineObject*, BOOL, BOOL,
		                                           IUnknown*) override
		{
			return S_OK;
		}
	};
}

ui::const_surface_ptr platform::create_segoe_md2_icon(const wchar_t ch)
{
	static std::unordered_map<wchar_t, ui::const_surface_ptr> cache;
	const auto found = cache.find(ch);

	if (found != cache.end())
	{
		return found->second;
	}

	auto surface_result = std::make_shared<ui::surface>();

	constexpr auto cxy = 160;

	if (!surface_result->alloc(cxy, cxy, ui::texture_format::ARGB, ui::orientation::top_left))
	{
		return surface_result;
	}

	surface_result->make_blank(); // transparent background

	ComPtr<IDWriteFactory> dwrite_factory;
	ComPtr<IDWriteTextFormat> text_format;
	ComPtr<IDWriteTextLayout> text_layout;

	auto hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(dwrite_factory),
	                              reinterpret_cast<IUnknown**>(dwrite_factory.GetAddressOf()));

	if (SUCCEEDED(hr))
	{
		hr = dwrite_factory->CreateTextFormat(L"Segoe MDL2 Assets", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
		                                      DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 148, L"",
		                                      &text_format);
	}

	if (SUCCEEDED(hr))
	{
		text_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
		text_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

		const wchar_t icon_text[2] = {ch, 0};
		hr = dwrite_factory->CreateTextLayout(icon_text, 1, text_format.Get(), static_cast<float>(cxy),
		                                      static_cast<float>(cxy), &text_layout);
	}

	if (SUCCEEDED(hr))
	{
		icon_glyph_renderer renderer(dwrite_factory.Get(), surface_result.get());
		hr = text_layout->Draw(nullptr, &renderer, 0.0f, 0.0f);
	}

	if (SUCCEEDED(hr))
	{
		cache[ch] = surface_result;
	}

	return surface_result;
}


ui::surface_ptr platform::image_to_surface(const df::cspan image_buffer_in, const sizei target_extent)
{
	if (!image_buffer_in.data || image_buffer_in.size == 0)
	{
		return nullptr; // Invalid input
	}

	if (image_buffer_in.size > UINT32_MAX)
	{
		return nullptr; // Size too large for WIC API
	}

	ui::surface_ptr surface_result;
	ComPtr<IWICImagingFactory> wic;

	auto hr = CoCreateInstance(
		CLSID_WICImagingFactory,
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&wic));

	ComPtr<IWICBitmapDecoder> wic_decoder;
	ComPtr<IWICBitmapFrameDecode> wic_source;
	ComPtr<IWICStream> stream;

	if (SUCCEEDED(hr))
	{
		hr = wic->CreateStream(&stream);
	}

	if (SUCCEEDED(hr))
	{
		hr = stream->InitializeFromMemory(
			const_cast<BYTE*>(image_buffer_in.data),
			static_cast<uint32_t>(std::min(image_buffer_in.size, static_cast<size_t>(UINT32_MAX)))
		);
	}

	if (SUCCEEDED(hr))
	{
		hr = wic->CreateDecoderFromStream(
			stream.Get(),
			nullptr,
			WICDecodeMetadataCacheOnDemand,
			&wic_decoder
		);
	}

	ComPtr<IWICBitmapFrameDecode> pBitmapFrameDecode;
	ComPtr<IWICBitmapSource> pConverter;

	UINT uiFrameCount = 0;
	UINT uiWidth = 0, uiHeight = 0;
	WICPixelFormatGUID pixel_format = {};
	GUID container_format = {};

	if (SUCCEEDED(hr))
	{
		hr = wic_decoder->GetFrameCount(&uiFrameCount);
	}

	if (SUCCEEDED(hr))
	{
		hr = wic_decoder->GetContainerFormat(&container_format);
	}

	if (SUCCEEDED(hr) && uiFrameCount > 0)
	{
		ComPtr<IWICBitmapSource> pSource;

		hr = wic_decoder->GetFrame(0, &pBitmapFrameDecode);

		if (SUCCEEDED(hr))
		{
			pSource = pBitmapFrameDecode;

			hr = pSource->GetSize(&uiWidth, &uiHeight);

			// Validate image dimensions
			if (SUCCEEDED(hr) && (uiWidth == 0 || uiHeight == 0 ||
				uiWidth > 65536 || uiHeight > 65536))
			{
				hr = E_INVALIDARG;
			}
		}

		if (SUCCEEDED(hr))
		{
			hr = pSource->GetPixelFormat(&pixel_format);
		}

		if (SUCCEEDED(hr))
		{
			ComPtr<IWICComponentInfo> componentInfo;
			ComPtr<IWICPixelFormatInfo2> pixelFormatInfo;

			HRESULT hr = wic->CreateComponentInfo(pixel_format, &componentInfo);

			if (SUCCEEDED(hr))
			{
				hr = componentInfo->QueryInterface(__uuidof(IWICPixelFormatInfo2), &pixelFormatInfo);
			}

			BOOL supportsTransparency = FALSE;

			if (SUCCEEDED(hr))
			{
				hr = pixelFormatInfo->SupportsTransparency(&supportsTransparency);
			}

			const auto use_transparency = supportsTransparency != 0 || container_format == GUID_ContainerFormatGif;

			if (SUCCEEDED(hr))
			{
				if (!IsEqualGUID(pixel_format, GUID_WICPixelFormat32bppBGRA) &&
					!IsEqualGUID(pixel_format, GUID_WICPixelFormat32bppBGR))
				{
					hr = WICConvertBitmapSource(
						use_transparency ? GUID_WICPixelFormat32bppBGRA : GUID_WICPixelFormat32bppBGR, pSource.Get(),
						&pConverter);

					if (SUCCEEDED(hr))
					{
						pSource = pConverter;
					}
				}
			}

			if (SUCCEEDED(hr))
			{
				const auto fmt = use_transparency ? ui::texture_format::ARGB : ui::texture_format::RGB;
				surface_result = std::make_shared<ui::surface>();

				if (surface_result->alloc(uiWidth, uiHeight, fmt, ui::orientation::top_left))
				{
					WICRect rc;
					rc.X = 0;
					rc.Y = 0;
					rc.Width = uiWidth;
					rc.Height = uiHeight;

					// Validate buffer size before copy
					const auto required_size = static_cast<size_t>(uiWidth) * uiHeight * 4;
					if (surface_result->size() < required_size)
					{
						hr = E_OUTOFMEMORY;
					}
					else
					{
						hr = pSource->CopyPixels(&rc,
						                         static_cast<uint32_t>(surface_result->stride()),
						                         static_cast<uint32_t>(surface_result->size()),
						                         surface_result->pixels());
					}
				}
				else
				{
					hr = E_OUTOFMEMORY;
				}
			}
		}
	}

	return surface_result;
}
