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
#include <wincodec.h>
#include <d2d1.h>
#include <dwrite.h>

#include "app_text.h"
#include "files.h"

static void flip_buffer_vertically(uint32_t* buffer, const unsigned width, const unsigned height)
{
	if (!buffer || width == 0 || height <= 1)
	{
		return; // Nothing to flip or invalid parameters
	}
	
	const auto rows = height / 2;
	const auto stride = width * sizeof(uint32_t);
	
	// Check for potential overflow
	if (width > SIZE_MAX / sizeof(uint32_t))
	{
		return; // Avoid overflow in stride calculation
	}
	
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
		df::log(__FUNCTION__, "Invalid image dimensions"sv);
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
		df::log(__FUNCTION__, "Image too large, potential overflow"sv);
		throw std::invalid_argument("Image too large");
	}
	
	bi.biSizeImage = static_cast<DWORD>(pixel_count * 4);

	const auto alloc_size = sizeof(bi) + bi.biSizeImage;
	auto* const h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, alloc_size);

	if (h == nullptr)
	{
		df::log(__FUNCTION__, "GlobalAlloc failed"sv);
		throw std::bad_alloc();
	}

	auto* const buffer_out = static_cast<uint8_t*>(GlobalLock(h));

	if (buffer_out == nullptr)
	{
		df::log(__FUNCTION__, "GlobalLock failed"sv);
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
			df::log(__FUNCTION__, "Invalid pixel buffer pointers"sv);
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
				df::log(__FUNCTION__, "Buffer bounds exceeded"sv);
				break;
			}
			
			memcpy(pixels_out + dest_offset, pixels_in + src_offset, copy_len);
		}

		flip_buffer_vertically(reinterpret_cast<uint32_t*>(pixels_out), dimensions.cx, dimensions.cy);
	}

	GlobalUnlock(h);

	return h;
}

static CLSID wic_encoder_clsid(const std::u8string_view format)
{
	CLSID result = {};

	static const df::hash_map<std::u8string_view, CLSID, df::ihash, df::ieq> extensions
	{
		{u8".jpg"sv, GUID_ContainerFormatJpeg},
		{u8".jpeg"sv, GUID_ContainerFormatJpeg},
		{u8".jpe"sv, GUID_ContainerFormatJpeg},
		{u8".png"sv, GUID_ContainerFormatPng},
		{u8".tiff"sv, GUID_ContainerFormatTiff},
		{u8".tif"sv, GUID_ContainerFormatTiff},
		{u8".gif"sv, GUID_ContainerFormatGif},
		{u8".bmp"sv, GUID_ContainerFormatBmp},
		{u8".webp"sv, GUID_ContainerFormatWebp},
		{u8"jpg"sv, GUID_ContainerFormatJpeg},
		{u8"jpeg"sv, GUID_ContainerFormatJpeg},
		{u8"jpe"sv, GUID_ContainerFormatJpeg},
		{u8"png"sv, GUID_ContainerFormatPng},
		{u8"tiff"sv, GUID_ContainerFormatTiff},
		{u8"tif"sv, GUID_ContainerFormatTiff},
		{u8"gif"sv, GUID_ContainerFormatGif},
		{u8"bmp"sv, GUID_ContainerFormatBmp},
		{u8"webp"sv, GUID_ContainerFormatWebp},
		{u8"heic"sv, GUID_ContainerFormatHeif},
	};

	const auto found = extensions.find(format.substr(df::find_ext(format)));
	return found == extensions.end() ? GUID_ContainerFormatPng : found->second;
}

platform::file_op_result save_bitmap_info(const df::folder_path save_path, const std::u8string_view name,
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
		const auto ext = as_png ? u8"png"sv : u8"jpg"sv;

		df::file_path path(folder, name, ext);
		const auto encoder_format = wic_encoder_clsid(ext);
		const int max_file_name = 100;

		// Validate path creation to prevent infinite loops
		if (name.empty())
		{
			result.error_message = u8"Invalid filename";
			return result;
		}

		while (path.exists() && i < max_file_name)
		{
			path = df::file_path(folder, str::format(u8"{}{}"sv, name, i++), ext);
		}

		if (i >= max_file_name)
		{
			result.error_message = u8"Unable to create unique filename after 100 attempts";
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

			WICPixelFormatGUID pixelFormat = { 0 };
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


ui::const_surface_ptr platform::create_segoe_md2_icon(const wchar_t ch)
{
	static std::unordered_map<wchar_t, ui::const_surface_ptr> cache;
	const auto found = cache.find(ch);

	if (found != cache.end())
	{
		return found->second;
	}

	auto surface_result = std::make_shared<ui::surface>();

	const auto cxy = 160;
	surface_result->alloc(cxy, cxy, ui::texture_format::ARGB, ui::orientation::top_left);

	{
		ComPtr<ID2D1Factory> pD2DFactory;

		HRESULT hr = D2D1CreateFactory(
			D2D1_FACTORY_TYPE_SINGLE_THREADED,
			pD2DFactory.GetAddressOf());

		if (SUCCEEDED(hr))
		{
			const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
				D2D1_RENDER_TARGET_TYPE_DEFAULT,
				D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
				0,
				0,
				D2D1_RENDER_TARGET_USAGE_NONE,
				D2D1_FEATURE_LEVEL_DEFAULT
			);

			ComPtr<IDWriteFactory> m_pDWriteFactory;
			ComPtr<IWICImagingFactory> wic;
			//ComPtr<IWICBitmapSource> wic_source;
			ComPtr<IWICBitmap> wic_bitmap;
			ComPtr<ID2D1RenderTarget> rt;
			ComPtr<IDWriteTextFormat> m_pTextFormat;
			ComPtr<ID2D1SolidColorBrush> brush;

			hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IWICImagingFactory),
				reinterpret_cast<void**>(wic.GetAddressOf()));

			if (SUCCEEDED(hr))
			{
				hr = wic->CreateBitmap(cxy, cxy,
					GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &wic_bitmap);
			}

			if (SUCCEEDED(hr))
			{
				hr = pD2DFactory->CreateWicBitmapRenderTarget(wic_bitmap.Get(), props, rt.GetAddressOf());
			}

			if (SUCCEEDED(hr))
			{
				// Create a DirectWrite factory.
				hr = DWriteCreateFactory(
					DWRITE_FACTORY_TYPE_SHARED,
					__uuidof(m_pDWriteFactory),
					reinterpret_cast<IUnknown**>(m_pDWriteFactory.GetAddressOf())
				);
			}
			if (SUCCEEDED(hr))
			{
				// Create a DirectWrite text format object.
				hr = m_pDWriteFactory->CreateTextFormat(
					L"Segoe MDL2 Assets",
					nullptr,
					DWRITE_FONT_WEIGHT_NORMAL,
					DWRITE_FONT_STYLE_NORMAL,
					DWRITE_FONT_STRETCH_NORMAL,
					148,
					L"", //locale
					&m_pTextFormat
				);
			}

			if (SUCCEEDED(hr))
			{
				m_pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
				m_pTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
			}

			if (SUCCEEDED(hr))
			{
				auto hr = rt->CreateSolidColorBrush(
					D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f),
					&brush
				);
			}

			if (SUCCEEDED(hr))
			{
				const wchar_t icon_text[2] = { ch, 0 };

				rt->BeginDraw();

				rt->SetTransform(D2D1::Matrix3x2F::Identity());

				rt->Clear();

				rt->DrawText(
					icon_text,
					1,
					m_pTextFormat.Get(),
					D2D1::RectF(0, 0, cxy, cxy),
					brush.Get()
				);

				hr = rt->EndDraw();
			}

			ComPtr<IWICBitmapSource> pConverter;

			if (SUCCEEDED(hr))
			{
				hr = WICConvertBitmapSource(GUID_WICPixelFormat32bppBGRA, wic_bitmap.Get(), &pConverter);
			}

			if (SUCCEEDED(hr))
			{
				surface_result = std::make_shared<ui::surface>();
				if (surface_result->alloc(cxy, cxy, ui::texture_format::ARGB, ui::orientation::top_left))
				{
					WICRect rc;
					rc.X = 0;
					rc.Y = 0;
					rc.Width = cxy;
					rc.Height = cxy;

					hr = pConverter->CopyPixels(&rc,
						static_cast<uint32_t>(surface_result->stride()),
						static_cast<uint32_t>(surface_result->size()),
						surface_result->pixels());
				}
			}

			if (SUCCEEDED(hr))
			{
				cache[ch] = surface_result;
			}
		}
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

	if (SUCCEEDED(hr) && (uiFrameCount > 0))
	{
		ComPtr<IWICBitmapSource> pSource;

		hr = wic_decoder->GetFrame(0, &pBitmapFrameDecode);

		if (SUCCEEDED(hr))
		{
			pSource = pBitmapFrameDecode;
			pSource->AddRef();

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
