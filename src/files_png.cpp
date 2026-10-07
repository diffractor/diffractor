// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: PNG image format support. Loads and saves PNG files using libpng,
// handles ICC profiles, EXIF metadata, and XMP data.

#include "pch.h"
#include "files.h"
#include "metadata_exif.h"

#include <png.h>
#include <zlib.h>

static void png_error_handler(png_structp png_ptr, const png_const_charp msg)
{
	df::log_once(__FUNCTION__, msg);
	throw app_exception(msg);
}

// libpng emits non-fatal warnings (e.g. "iTXt: CRC error" from images with a
// corrupt metadata-chunk CRC) via its default handler, which prints them to
// stderr and clutters the console/test output. Route them to the quiet debug
// log instead so they are still available for diagnosis but not shown.
static void png_warning_handler(png_structp, const png_const_charp msg)
{
	df::log_once("libpng", msg);
}

static constexpr auto png_xmp_key = "XML:com.adobe.xmp";
static constexpr size_t max_png_metadata_chunk = 16u * 1024u * 1024u;
static constexpr size_t max_png_decompressed_text = 16u * 1024u * 1024u;

// png_destroy_read_struct / png_destroy_write_struct only free the info struct when
// it is passed in. Destroying the png struct alone leaks the info struct and
// everything it owns - decompressed iTXt/tEXt (XMP), the iCCP profile, eXIf, the
// palette and tRNS - on every image. These own both so that cannot drift apart,
// and so the pair survives the C++ exceptions our error handler throws through
// libpng's C frames.
class png_reader
{
	png_structp _png = nullptr;
	png_infop _info = nullptr;

public:
	png_reader()
	{
		_png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, png_error_handler, png_warning_handler);

		if (_png)
		{
			_info = png_create_info_struct(_png);
			png_set_benign_errors(_png, 1);
		}

		if (!_png || !_info)
		{
			png_destroy_read_struct(&_png, &_info, nullptr);
			throw app_exception("could not create png reader"s);
		}
	}

	~png_reader()
	{
		png_destroy_read_struct(&_png, &_info, nullptr);
	}

	png_reader(const png_reader&) = delete;
	png_reader& operator=(const png_reader&) = delete;

	png_structp get() const { return _png; }
	png_infop info() const { return _info; }
};

class png_writer
{
	png_structp _png = nullptr;
	png_infop _info = nullptr;

public:
	png_writer()
	{
		_png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, png_error_handler, png_warning_handler);

		if (_png)
		{
			_info = png_create_info_struct(_png);
		}

		if (!_png || !_info)
		{
			png_destroy_write_struct(&_png, &_info);
			throw app_exception("could not create png writer"s);
		}
	}

	~png_writer()
	{
		png_destroy_write_struct(&_png, &_info);
	}

	png_writer(const png_writer&) = delete;
	png_writer& operator=(const png_writer&) = delete;

	png_structp get() const { return _png; }
	png_infop info() const { return _info; }
};

class buffer_stream
{
	uint64_t _pos = 0;
	const uint8_t* _data = nullptr;
	const size_t _size = 0;

public:
	buffer_stream(const df::cspan data) : _data(data.data), _size(data.size)
	{
	}

	void read(uint8_t* dest, const size_t len)
	{
		if (len > _size || _pos > _size - len) throw app_exception("invalid read"s);
		memcpy_s(dest, len, _data + _pos, len);
		_pos += len;
	}

	void skip(const size_t len)
	{
		// Overflow-safe: len can exceed _size for a truncated file.
		if (len > _size || _pos > _size - len) throw app_exception("invalid skip"s);
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

	void read(uint8_t* dest, const size_t len)
	{
		_stream.read(_pos, dest, len);
		_pos += len;
	}

	void skip(const size_t len)
	{
		_pos += len;
	}
};

static void png_write_callback(const png_structp png_ptr, const png_bytep data, const png_size_t length)
{
	auto* p = static_cast<df::blob*>(png_get_io_ptr(png_ptr));
	p->insert(p->end(), data, data + length);
}


ui::image_ptr save_png(const ui::const_surface_ptr& surface_in, const metadata_parts& metadata)
{
	if (!is_valid(surface_in)) return {};

	const png_writer png;
	auto* const info_ptr = png.info();

	const auto is_rgb = surface_in->format() == ui::texture_format::RGB;

	df::blob result;
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
		png_set_iCCP(png.get(), info_ptr, "icc", PNG_COMPRESSION_TYPE_BASE, data.data,
		             static_cast<png_uint_32>(data.size));
	}

	if (!metadata.xmp.empty())
	{
		png_text text_metadata = {};
		auto xmp_text = metadata.xmp.clone();
		xmp_text.push_back(0);

		text_metadata.compression = PNG_ITXT_COMPRESSION_NONE;
		text_metadata.key = const_cast<png_charp>(png_xmp_key);
		text_metadata.text = std::bit_cast<png_charp>(xmp_text.data());
		png_set_text(png.get(), info_ptr, &text_metadata, 1);
	}

	if (!metadata.exif.empty())
	{
		const df::cspan exif_data = metadata.exif;
		const auto exif_skip = is_exif_signature(exif_data) ? exif_signature_len : 0u;
		png_set_eXIf_1(png.get(), info_ptr, static_cast<png_uint_32>(exif_data.size - exif_skip),
		               const_cast<png_bytep>(exif_data.data) + exif_skip);
	}
	else if (surface_in->orientation() != ui::orientation::top_left)
	{
		auto exif = make_orientation_exif(surface_in->orientation());
		const auto exif_skip = is_exif_signature(exif) ? exif_signature_len : 0u;
		png_set_eXIf_1(png.get(), info_ptr, static_cast<png_uint_32>(exif.size() - exif_skip),
		               exif.data() + exif_skip);
	}

	std::vector<uint8_t*> rows(dims.cy);

	for (auto y = 0; y < dims.cy; ++y)
	{
		rows[y] = const_cast<uint8_t*>(surface_in->pixels()) + y * stride;
	}

	png_set_compression_level(png.get(), 6);
	png_set_rows(png.get(), info_ptr, rows.data());

	png_write_png(png.get(), info_ptr,
	              PNG_TRANSFORM_BGR | (is_rgb ? PNG_TRANSFORM_STRIP_FILLER_AFTER : PNG_TRANSFORM_IDENTITY),
	              nullptr);

	return std::make_shared<ui::image>(std::move(result), dims, ui::image_format::PNG,
	                                   surface_in->orientation());
}


static void png_read_callback(const png_structp png_ptr, const png_bytep result, const png_size_t result_size)
{
	auto* stream = static_cast<buffer_stream*>(png_get_io_ptr(png_ptr));
	stream->read(result, result_size);
}

static void png_read_callback2(const png_structp png_ptr, const png_bytep result, const png_size_t result_size)
{
	auto* stream = static_cast<buffer_stream2*>(png_get_io_ptr(png_ptr));
	stream->read(result, result_size);
}

static uint32_t read_png_be32(const uint8_t* p)
{
	return (static_cast<uint32_t>(p[0]) << 24) |
		(static_cast<uint32_t>(p[1]) << 16) |
		(static_cast<uint32_t>(p[2]) << 8) |
		static_cast<uint32_t>(p[3]);
}

static bool png_chunk_type_is(const std::array<uint8_t, 4>& type, const char (&name)[5])
{
	return type[0] == static_cast<uint8_t>(name[0]) &&
		type[1] == static_cast<uint8_t>(name[1]) &&
		type[2] == static_cast<uint8_t>(name[2]) &&
		type[3] == static_cast<uint8_t>(name[3]);
}

static bool png_crc_matches(const std::array<uint8_t, 4>& type, const df::blob& data, const uint32_t expected)
{
	auto crc = crc32(0, Z_NULL, 0);
	crc = crc32(crc, type.data(), static_cast<uInt>(type.size()));
	if (!data.empty()) crc = crc32(crc, data.data(), static_cast<uInt>(data.size()));
	return crc == expected;
}

static df::cspan png_exif_payload(const df::cspan data)
{
	const auto payload = is_exif_signature(data) ? data.sub(exif_signature_len) : data;

	if (payload.size < 4)
	{
		return {};
	}

	const auto* const p = payload.data;
	const auto little_endian_tiff = p[0] == 'I' && p[1] == 'I' && p[2] == 0x2a && p[3] == 0x00;
	const auto big_endian_tiff = p[0] == 'M' && p[1] == 'M' && p[2] == 0x00 && p[3] == 0x2a;

	return little_endian_tiff || big_endian_tiff ? payload : df::cspan{};
}

static df::blob png_inflate_text(const uint8_t* const compressed, const size_t compressed_size)
{
	df::blob result;
	z_stream z = {};

	if (inflateInit(&z) != Z_OK)
	{
		return {};
	}

	z.next_in = const_cast<Bytef*>(compressed);
	z.avail_in = static_cast<uInt>(std::min<size_t>(compressed_size, std::numeric_limits<uInt>::max()));

	std::array<uint8_t, 4096> temp = {};
	auto ok = true;

	do
	{
		z.next_out = temp.data();
		z.avail_out = static_cast<uInt>(temp.size());

		const auto ret = inflate(&z, Z_NO_FLUSH);
		if (ret != Z_OK && ret != Z_STREAM_END)
		{
			ok = false;
			break;
		}

		const auto produced = temp.size() - z.avail_out;
		if (produced > max_png_decompressed_text || result.size() > max_png_decompressed_text - produced)
		{
			ok = false;
			break;
		}

		result.insert(result.end(), temp.begin(), temp.begin() + produced);

		if (ret == Z_STREAM_END)
		{
			break;
		}
	}
	while (z.avail_out == 0 || z.avail_in != 0);

	inflateEnd(&z);
	if (!ok) return {};
	return result;
}

static const uint8_t* find_nul(const uint8_t* const first, const uint8_t* const last)
{
	return std::find(first, last, 0);
}

static void read_png_text_chunk(metadata_parts& metadata, const std::array<uint8_t, 4>& type, const df::blob& data)
{
	const auto* const first = data.data();
	const auto* const last = first + data.size();
	const auto* const key_end = find_nul(first, last);

	if (key_end == last || std::string_view(reinterpret_cast<const char*>(first), key_end - first) != png_xmp_key)
	{
		return;
	}

	if (png_chunk_type_is(type, "tEXt"))
	{
		metadata.xmp.assign(key_end + 1, last);
	}
	else if (png_chunk_type_is(type, "zTXt"))
	{
		if (last - key_end < 2 || key_end[1] != 0) return;
		metadata.xmp = png_inflate_text(key_end + 2, last - (key_end + 2));
	}
	else if (png_chunk_type_is(type, "iTXt"))
	{
		if (last - key_end < 3) return;

		const auto compressed = key_end[1] != 0;
		if (key_end[2] != 0) return;

		const auto* const language_end = find_nul(key_end + 3, last);
		if (language_end == last) return;

		const auto* const translated_end = find_nul(language_end + 1, last);
		if (translated_end == last) return;

		const auto* const text = translated_end + 1;
		if (compressed)
		{
			metadata.xmp = png_inflate_text(text, last - text);
		}
		else
		{
			metadata.xmp.assign(text, last);
		}
	}
}

static void scan_png_chunks_for_metadata(read_stream& rs, metadata_parts& metadata)
{
	const auto file_size = rs.size();
	constexpr auto sig_len = 8u;
	auto pos = static_cast<uint64_t>(sig_len);

	while (pos <= file_size && file_size - pos >= 12)
	{
		uint8_t header[8] = {};
		rs.read(pos, header, sizeof(header));
		const auto chunk_size = read_png_be32(header);
		const std::array<uint8_t, 4> type = {header[4], header[5], header[6], header[7]};
		const auto data_pos = pos + sizeof(header);

		if (chunk_size > file_size - data_pos || file_size - data_pos - chunk_size < 4)
		{
			return;
		}

		const auto crc_pos = data_pos + chunk_size;
		uint8_t crc_bytes[4] = {};
		rs.read(crc_pos, crc_bytes, sizeof(crc_bytes));
		const auto expected_crc = read_png_be32(crc_bytes);

		if (png_chunk_type_is(type, "eXIf") || png_chunk_type_is(type, "tEXt") ||
			png_chunk_type_is(type, "zTXt") || png_chunk_type_is(type, "iTXt"))
		{
			if (chunk_size > max_png_metadata_chunk)
			{
				return;
			}

			auto data = rs.read(data_pos, chunk_size);
			if (!png_crc_matches(type, data, expected_crc))
			{
				return;
			}

			if (png_chunk_type_is(type, "eXIf"))
			{
				const auto exif = png_exif_payload(data);
				if (exif.size > 0)
				{
					metadata.exif.assign(exif.data, exif.data + exif.size);
				}
			}
			else
			{
				read_png_text_chunk(metadata, type, data);
			}
		}

		pos = crc_pos + 4;
		if (png_chunk_type_is(type, "IEND")) return;
	}
}

ui::surface_ptr load_png(const df::cspan data)
{
	if (data.size < 8 || png_sig_cmp(data.data, 0, 8))
	{
		throw app_exception("load_png invalid png header"s);
	}

	buffer_stream stream(data);
	stream.skip(8);

	const png_reader png;
	auto* const info_ptr = png.info();

	png_set_read_fn(png.get(), &stream, png_read_callback);

	png_set_sig_bytes(png.get(), 8);
	png_read_info(png.get(), info_ptr);

	png_uint_32 width = 0, height = 0;
	int bit_depth = 0, color_type = 0;
	png_get_IHDR(png.get(), info_ptr, &width, &height, &bit_depth, &color_type, nullptr, nullptr, nullptr);

	// The df::cspan decode path carries no budget gate of its own, and IHDR dimensions cost nothing
	// to declare: a small, highly compressible PNG can otherwise ask for gigabytes.
	if (reject_over_budget_source(nullptr, {static_cast<int>(width), static_cast<int>(height)}, "PNG"))
	{
		return {};
	}

	if (bit_depth == 16)
		png_set_scale_16(png.get());

	// A PNG that declares its own transfer function has to be converted for an sRGB display or it
	// shows up washed out or over-contrasted. Without gAMA the file is already assumed to be sRGB.
	double file_gamma = 0.0;

	if (png_get_valid(png.get(), info_ptr, PNG_INFO_sRGB) == 0 &&
		png_get_gAMA(png.get(), info_ptr, &file_gamma) != 0 && file_gamma > 0.0)
	{
		png_set_gamma(png.get(), 2.2, file_gamma);
	}

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

	//if (color_type == PNG_COLOR_TYPE_RGB)
	png_set_bgr(png.get());
	png_read_update_info(png.get(), info_ptr);

	const bool has_alpha = png_get_channels(png.get(), info_ptr) == 4;
	auto result = std::make_shared<ui::surface>();

	if (result->alloc(width, height, has_alpha ? ui::texture_format::ARGB : ui::texture_format::RGB))
	{
		if (png_get_rowbytes(png.get(), info_ptr) > result->stride())
		{
			throw app_exception("load_png invalid row size"s);
		}

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
		const auto exif = png_exif_payload({exif_data, num_exif});
		if (exif.size > 16)
		{
			prop::item_metadata md;
			metadata_exif::parse(md, exif);
			result->orientation(md.orientation);
		}
	}

	metadata_parts trailing_metadata;
	mem_read_stream metadata_stream(data);
	scan_png_chunks_for_metadata(metadata_stream, trailing_metadata);
	if (!trailing_metadata.exif.empty())
	{
		prop::item_metadata md;
		metadata_exif::parse(md, trailing_metadata.exif);
		result->orientation(md.orientation);
	}

	return result;
};

static df::blob load_profile(const png_textp txt)
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

	constexpr auto sig_len = 8u;
	uint8_t sig[sig_len];

	if (rs.size() < sig_len)
	{
		throw app_exception("load_png invalid png header"s);
	}

	rs.read(0, sig, sig_len);

	if (png_sig_cmp(sig, 0, sig_len))
	{
		throw app_exception("load_png invalid png header"s);
	}

	buffer_stream2 stream(rs);
	stream.skip(sig_len);

	const png_reader png;
	auto* const info_ptr = png.info();

	png_set_read_fn(png.get(), &stream, png_read_callback2);

	png_set_sig_bytes(png.get(), sig_len);
	png_read_info(png.get(), info_ptr);

	png_uint_32 width = 0, height = 0;
	int bit_depth = 0, color_type = 0;
	png_get_IHDR(png.get(), info_ptr, &width, &height, &bit_depth, &color_type, nullptr, nullptr, nullptr);

	if (bit_depth == 16)
		png_set_scale_16(png.get());

	result.width = width;
	result.height = height;

	if (color_type == PNG_COLOR_TYPE_GRAY)
	{
		result.pixel_format = "gray"_c;
	}
	else if (color_type == PNG_COLOR_TYPE_PALETTE)
	{
		result.pixel_format = "palette"_c;
	}
	else if (color_type == PNG_COLOR_TYPE_RGB)
	{
		result.pixel_format = "rgb"_c;
	}
	else if (color_type == PNG_COLOR_TYPE_RGB_ALPHA)
	{
		result.pixel_format = "rgba"_c;
	}

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

	scan_png_chunks_for_metadata(rs, result.metadata);

	result.success = true;
	return result;
}
