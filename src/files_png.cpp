// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "pch.h"
#include "files.h"
#include "metadata_exif.h"

#include <png.h>

static void png_error_handler(png_structp png_ptr, png_const_charp msg)
{
	df::log(__FUNCTION__, msg);
	throw app_exception(msg);
}

enum class png_chunk : uint32_t
{
	// Critical chunks - (shall appear in this order, except PLTE is optional)
	IHDR = 'IHDR',
	PLTE = 'PLTE',
	IDAT = 'IDAT',
	IEND = 'IEND',
	// Ancillary chunks - (need not appear in this order)
	cHRM = 'cHRM',
	gAMA = 'gAMA',
	iCCP = 'iCCP',
	sBIT = 'sBIT',
	sRGB = 'sRGB',
	bKGD = 'bKGD',
	hIST = 'hIST',
	tRNS = 'tRNS',
	pHYs = 'pHYs',
	sPLT = 'sPLT',
	tIME = 'tIME',
	iTXt = 'iTXt',
	tEXt = 'tEXt',
	zTXt = 'zTXt',
	eXIf = 'eXIf',
};

static const auto png_xmp_header = "XML:com.adobe.xmp\0\0\0\0\0"sv;
static const int png_xmp_header_len = 22;

class buffer_stream
{
	uint64_t _pos = 0;
	const uint8_t* _data = nullptr;
	const size_t _size = 0;

public:
	buffer_stream(df::cspan data) : _data(data.data), _size(data.size)
	{
	}

	void read(uint8_t* dest, size_t len)
	{
		if ((_pos + len) > _size) throw app_exception(u8"invalid read"s);
		memcpy_s(dest, len, _data + _pos, len);
		_pos += len;
	}

	void skip(size_t len)
	{
		if (_pos > _size - len) throw app_exception(u8"invalid skip"s);
		_pos += len;
	}
};

class buffer_stream2
{
	uint64_t _pos = 0;
	read_stream& _stream;

public:
	buffer_stream2(read_stream& rs) : _stream(rs)
	{
	}

	void read(uint8_t* dest, size_t len)
	{
		_stream.read(_pos, dest, len);
		_pos += len;
	}

	void skip(size_t len)
	{
		_pos += len;
	}
};

static void png_write_callback(png_structp png_ptr, png_bytep data, png_size_t length)
{
	auto* p = static_cast<df::blob*>(png_get_io_ptr(png_ptr));
	p->insert(p->end(), data, data + length);
}


ui::image_ptr save_png(const ui::const_surface_ptr& surface_in, const metadata_parts& metadata)
{
	const std::unique_ptr<png_struct, std::function<void(png_structp)>> png(
		png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, png_error_handler, nullptr),
		[](png_structp p)
		{
			if (p) png_destroy_write_struct(&p, nullptr);
		});

	if (png)
	{
		const auto is_rgb = surface_in->format() == ui::texture_format::RGB;

		/*if (setjmp(png_jmpbuf(png.get())))
		{
			throw app_exception(u8"write_png failed"sv);
		}*/

		df::blob result;

		auto* const info_ptr = png_create_info_struct(png.get());

		if (info_ptr)
		{
			png_set_write_fn(png.get(), &result, png_write_callback, nullptr);

			const auto dims = surface_in->dimensions();
			const auto stride = surface_in->stride();

			png_set_IHDR(png.get(), info_ptr, dims.cx, dims.cy, 8,
			             is_rgb ? PNG_COLOR_TYPE_RGB : PNG_COLOR_TYPE_RGBA,
			             PNG_INTERLACE_NONE,
			             PNG_COMPRESSION_TYPE_DEFAULT,
			             PNG_FILTER_TYPE_DEFAULT);

			if (!metadata.icc.empty())
			{
				const df::cspan data = metadata.icc;
				png_set_iCCP(png.get(), info_ptr, "icc", PNG_COMPRESSION_TYPE_BASE, data.data, data.size);
			}

			if (!metadata.xmp.empty())
			{
				png_text text_metadata = {};

				text_metadata.compression = PNG_ITXT_COMPRESSION_NONE;
				text_metadata.key = const_cast<png_charp>("XML:com.adobe.xmp");
				text_metadata.text = std::bit_cast<png_charp>(metadata.xmp.data());
				png_set_text(png.get(), info_ptr, &text_metadata, 1);
			}

			if (!metadata.exif.empty())
			{
				const df::cspan exif_data = metadata.exif;
				const auto exif_skip = is_exif_signature(exif_data) ? 6u : 0u;
				png_set_eXIf_1(png.get(), info_ptr, exif_data.size - exif_skip,
				               const_cast<png_bytep>(exif_data.data) + exif_skip);
			}
			else if (surface_in->orientation() != ui::orientation::top_left)
			{
				auto exif = make_orientation_exif(surface_in->orientation());
				const auto exif_skip = is_exif_signature(exif) ? 6u : 0u;
				png_set_eXIf_1(png.get(), info_ptr, exif.size() - exif_skip, exif.data() + exif_skip);
			}

			//png_set_compression_level(p, 6);
			std::vector<uint8_t*> rows(dims.cy);
			for (auto y = 0; y < dims.cy; ++y)
			{
				rows[y] = const_cast<uint8_t*>(surface_in->pixels()) + y * stride;
			}

			//png_set_bgr(png.get());
			//png_set_filler(png.get(), 0xFF, PNG_FILLER_AFTER);
			png_set_compression_level(png.get(), 6);
			//png_set_compression_strategy(png.get(), 0);
			//png_set_filter(png.get(), 0, PNG_NO_FILTERS);
			png_set_rows(png.get(), info_ptr, &rows[0]);


			png_write_png(png.get(), info_ptr,
			              PNG_TRANSFORM_BGR | (is_rgb ? PNG_TRANSFORM_STRIP_FILLER_AFTER : PNG_TRANSFORM_IDENTITY),
			              nullptr);
			png_write_end(png.get(), info_ptr);

			return std::make_shared<ui::image>(std::move(result), dims, ui::image_format::PNG,
			                                   surface_in->orientation());
		}
	}

	return {};
}


static void png_read_callback(png_structp png_ptr, png_bytep result, png_size_t result_size)
{
	auto* stream = static_cast<buffer_stream*>(png_get_io_ptr(png_ptr));
	stream->read(result, result_size);
}

static void png_read_callback2(png_structp png_ptr, png_bytep result, png_size_t result_size)
{
	auto* stream = static_cast<buffer_stream2*>(png_get_io_ptr(png_ptr));
	stream->read(result, result_size);
}

ui::surface_ptr load_png(df::cspan data)
{
	if (data.size < 8 || png_sig_cmp(data.data, 0, 8))
	{
		throw app_exception(u8"load_png invalid png header"s);
	}

	buffer_stream stream(data);
	stream.skip(8);

	const std::unique_ptr<png_struct, void(*)(png_structp)> png(
		png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, png_error_handler, nullptr), [](png_structp png_ptr)
		{
			png_destroy_read_struct(&png_ptr, nullptr, nullptr);
		});

	const png_infop info_ptr = png_create_info_struct(png.get());

	/*if (setjmp(png_jmpbuf(png.get())))
	{
		throw app_exception(u8"load_png failed"sv);
	}*/

	png_set_read_fn(png.get(), &stream, png_read_callback);

	png_set_sig_bytes(png.get(), 8);
	png_read_info(png.get(), info_ptr);

	//png_size_t pitch = png_get_rowbytes(png_ptr.get(), info_ptr);	
	png_uint_32 width, height;
	int bit_depth, color_type;
	png_get_IHDR(png.get(), info_ptr, &width, &height, &bit_depth, &color_type, nullptr, nullptr, nullptr);

	if (bit_depth == 16)
		png_set_strip_16(png.get());

	// png_set_swap_alpha(png_ptr);
	if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
	{
		png_set_expand_gray_1_2_4_to_8(png.get());
	}

	if (color_type == PNG_COLOR_TYPE_PALETTE)
	{
		png_set_palette_to_rgb(png.get());
	}

	if (png_get_valid(png.get(), info_ptr, PNG_INFO_tRNS) != 0)
	{
		png_set_tRNS_to_alpha(png.get());
	}

	if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
	{
		png_set_gray_to_rgb(png.get());
	}

	if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_PALETTE)
	{
		png_set_filler(png.get(), 0xFF, PNG_FILLER_AFTER);
	}

	//if (png_get_valid(png.get(), info_ptr, PNG_INFO_iCCP)) 
	//{
	//	png_charp profile_name = nullptr;
	//	png_bytep profile_data = nullptr;
	//	png_uint_32 profile_length = 0;
	//	int  compression_type = 0;

	//	png_get_iCCP(png.get(), info_ptr, &profile_name, &compression_type, &profile_data, &profile_length);
	//}

	//if (color_type == PNG_COLOR_TYPE_RGB)
	png_set_bgr(png.get());
	png_read_update_info(png.get(), info_ptr);

	const bool has_alpha = color_type == PNG_COLOR_TYPE_RGB_ALPHA;
	auto result = std::make_shared<ui::surface>();

	if (result->alloc(width, height, has_alpha ? ui::texture_format::ARGB : ui::texture_format::RGB))
	{
		auto rows = std::make_unique<png_bytep[]>(height);

		for (png_uint_32 y = 0; y < height; y++)
		{
			rows[y] = result->pixels() + y * result->stride();
		}

		png_read_image(png.get(), rows.get());
	}

	png_uint_32 num_exif = 0;
	png_bytep exif_data = nullptr;

	if (png_get_eXIf_1(png.get(), info_ptr, &num_exif, &exif_data))
	{
		if (num_exif > 16)
		{
			prop::item_metadata md;
			metadata_exif::parse(md, {exif_data, num_exif});
			result->orientation(md.orientation);
		}
	}

	return result;
};

static df::blob load_profile(png_textp txt)
{
	df::blob result;
	png_size_t length = 0;

	switch (txt->compression)
	{
#ifdef PNG_iTXt_SUPPORTED
	case PNG_ITXT_COMPRESSION_NONE:
	case PNG_ITXT_COMPRESSION_zTXt:
		length = txt->itxt_length;
		break;
#endif
	case PNG_TEXT_COMPRESSION_NONE:
	case PNG_TEXT_COMPRESSION_zTXt:
	default:
		length = txt->text_length;
		break;
	}

	result.assign(txt->text, txt->text + length);

	return result;
}

file_scan_result scan_png(read_stream& rs)
{
	file_scan_result result;
	//uint64_t pos = 8;
	//const auto len = s.size();

	//// read first and following chunks
	//while (pos + 16 < len)
	//{
	//	const auto chunk_length = df::byteswap32(s.peek32(pos));
	//	const auto chunk_type = static_cast<png_chunk>(df::byteswap32(s.peek32(pos + 4)));

	//	if (pos + chunk_length > len) // Overflow
	//		break;

	//	pos += 8;
	//	//const auto crc = _byteswap_ulong(*reinterpret_cast<const uint32_t*>(p + pos + chunk_length + 8));

	//	if (chunk_type == png_chunk::IHDR)
	//	{
	//		result.width = df::byteswap32(s.peek32(pos));
	//		result.height = df::byteswap32(s.peek32(pos + 4));
	//	}
	//	else if (chunk_type == png_chunk::iTXt && chunk_length > png_xmp_header_len)
	//	{
	//		const auto chunk = s.read(pos, chunk_length);

	//		if (memcmp(chunk.data(), png_xmp_header, png_xmp_header_len) == 0)
	//		{
	//			result.metadata.xmp.copy(chunk.data() + png_xmp_header_len, chunk_length - png_xmp_header_len);
	//		}
	//	}
	//	else if (chunk_type == png_chunk::eXIf && chunk_length > 16)
	//	{
	//		result.metadata.exif = s.read(pos, chunk_length);
	//	}
	//	else if (chunk_type == png_chunk::iCCP && chunk_length > 16)
	//	{
	//		result.metadata.icc = s.read(pos, chunk_length);
	//	}

	//	pos += chunk_length + 4;
	//}


	//ui::const_surface_ptr result;

	const auto sig_len = 8u;
	uint8_t sig[sig_len];
	rs.read(0, sig, sig_len);

	if (rs.size() < sig_len || png_sig_cmp(sig, 0, sig_len))
	{
		throw app_exception(u8"load_png invalid png header"s);
	}

	buffer_stream2 stream(rs);
	stream.skip(sig_len);

	const std::unique_ptr<png_struct, void(*)(png_structp)> png(
		png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, png_error_handler, nullptr), [](png_structp png_ptr)
		{
			png_destroy_read_struct(&png_ptr, nullptr, nullptr);
		});

	const png_infop info_ptr = png_create_info_struct(png.get());

	/*if (setjmp(png_jmpbuf(png.get())))
	{
		throw app_exception(u8"load_png failed"sv);
	}*/

	png_set_read_fn(png.get(), &stream, png_read_callback2);

	png_set_sig_bytes(png.get(), sig_len);
	png_read_info(png.get(), info_ptr);

	//png_size_t pitch = png_get_rowbytes(png_ptr.get(), info_ptr);	
	png_uint_32 width = 0, height = 0;
	int bit_depth, color_type;
	png_get_IHDR(png.get(), info_ptr, &width, &height, &bit_depth, &color_type, nullptr, nullptr, nullptr);

	if (bit_depth == 16)
		png_set_strip_16(png.get());

	result.width = width;
	result.height = height;

	if (color_type == PNG_COLOR_TYPE_GRAY)
	{
		result.pixel_format = u8"gray"_c;
	}
	else if (color_type == PNG_COLOR_TYPE_PALETTE)
	{
		result.pixel_format = u8"palette"_c;
	}
	else if (color_type == PNG_COLOR_TYPE_RGB)
	{
		result.pixel_format = u8"rgb"_c;
	}
	else if (color_type == PNG_COLOR_TYPE_RGB_ALPHA)
	{
		result.pixel_format = u8"rgba"_c;
	}

	// png_set_swap_alpha(png_ptr);
	if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < sig_len)
	{
		png_set_expand_gray_1_2_4_to_8(png.get());
	}

	if (color_type == PNG_COLOR_TYPE_PALETTE)
	{
		png_set_palette_to_rgb(png.get());
	}

	if (png_get_valid(png.get(), info_ptr, PNG_INFO_tRNS) != 0)
	{
		png_set_tRNS_to_alpha(png.get());
	}

	if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
	{
		png_set_gray_to_rgb(png.get());
	}

	if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_PALETTE)
	{
		png_set_filler(png.get(), 0xFF, PNG_FILLER_AFTER);
	}

	if (png_get_valid(png.get(), info_ptr, PNG_INFO_iCCP))
	{
		png_charp profile_name = nullptr;
		png_bytep profile_data = nullptr;
		png_uint_32 profile_length = 0;
		int compression_type = 0;

		if (png_get_iCCP(png.get(), info_ptr, &profile_name, &compression_type, &profile_data, &profile_length))
		{
			result.metadata.icc.assign(profile_data, profile_data + profile_length);
		}
	}

	//if (color_type == PNG_COLOR_TYPE_RGB)
	/*png_set_bgr(png.get());

	png_read_update_info(png.get(), info_ptr);

	const bool has_alpha = color_type == PNG_COLOR_TYPE_RGB_ALPHA;

	if (result.alloc(width, height, has_alpha ? ui::texture_format::ARGB : ui::texture_format::RGB))
	{
		std::unique_ptr<png_bytep[]> rows = std::make_unique<png_bytep[]>(height);

		for (png_uint_32 y = 0; y < height; y++)
		{
			rows[y] = result.pixels() + y * result.stride();
		}

		png_read_image(png.get(), rows.get());
	}*/

	png_uint_32 num_exif = 0;
	png_bytep exif_data = nullptr;

	if (png_get_eXIf_1(png.get(), info_ptr, &num_exif, &exif_data))
	{
		result.metadata.exif.assign(exif_data, exif_data + num_exif);
	}

	png_textp txt = nullptr;
	const auto nb_txt_chunks = png_get_text(png.get(), info_ptr, &txt, nullptr);

	for (int i = 0; i < nb_txt_chunks; i++, txt++)
	{
		if (!strcmp(txt->key, "XML:com.adobe.xmp"))
		{
			result.metadata.xmp = load_profile(txt);
		}
	}

	result.success = true;
	return result;
}
