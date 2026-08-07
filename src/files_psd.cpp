// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Adobe Photoshop (PSD) file format parser. Decodes layered images,
// extracts metadata (IPTC, XMP, EXIF, ICC), and converts color modes.

#include "pch.h"
#include "files.h"

enum psd_image_type
{
	BitmapMode = 0,
	GrayscaleMode = 1,
	IndexedMode = 2,
	RGBMode = 3,
	CMYKMode = 4,
	MultichannelMode = 7,
	DuotoneMode = 8,
	LabMode = 9
};

int channel_to_channel_shift(const int channel)
{
	// case -1  transparency mask
	// case 0	first component (Red, Cyan, Gray or Index)
	// case 1:  second component (Green, Magenta, or opacity)
	// case 2:  third component (Blue or Yellow)
	// case 3:  fourth component (Opacity or Black)
	// case 4:  fifth component (opacity)

	switch (channel)
	{
	case 0: return 16;
	case 1: return 8;
	case 2: return 0;
	case 3:
	case 4:
	case -1:
	default:
		return 24;
	}
}

class msb_stream
{
	read_stream& _s;
	uint64_t _len = 0;
	uint64_t _pos = 0;

public:
	msb_stream(read_stream& s) : _s(s)
	{
		_len = _s.size();
	}

	uint64_t remaining() const
	{
		if (_pos > _len) throw app_exception(__FUNCTION__);
		return _len - _pos;
	}

	// Lengths come from untrusted file fields; read_stream takes a size_t.
	static size_t to_len(const uint64_t size)
	{
		const auto result = static_cast<size_t>(size);
		if (result != size) throw app_exception(__FUNCTION__);
		return result;
	}

	void read(uint8_t* data, const uint64_t size)
	{
		if (remaining() < size) throw app_exception(__FUNCTION__);
		_s.read(_pos, data, to_len(size));
		_pos += size;
	}

	df::blob read_blob(const uint64_t size)
	{
		if (remaining() < size) throw app_exception(__FUNCTION__);
		auto result = _s.read(_pos, to_len(size));
		_pos += size;
		return result;
	}

	void skip(const uint64_t size)
	{
		if (remaining() < size) throw app_exception(__FUNCTION__);
		_pos += size;
	}

	void pos(const uint64_t p)
	{
		if (p > _len) throw app_exception(__FUNCTION__);
		_pos = p;
	}

	uint64_t pos() const
	{
		return _pos;
	}

	uint8_t read_u8()
	{
		if (remaining() < 1) throw app_exception(__FUNCTION__);
		const auto result = _s.peek8(_pos);
		_pos += 1;
		return result;
	}

	uint16_t read_u16()
	{
		if (remaining() < 2) throw app_exception(__FUNCTION__);
		const auto result = df::byteswap16(_s.peek16(_pos));
		_pos += 2;
		return result;
	}


	uint32_t read_u32()
	{
		if (remaining() < 4) throw app_exception(__FUNCTION__);
		const auto result = df::byteswap32(_s.peek32(_pos));
		_pos += 4;
		return result;
	}
};


// PackBits. Fills exactly len bytes; the stream must hold that run and nothing else.
static bool decode_rle_bytes(uint8_t* dst, const int len, msb_stream& stream)
{
	int x = 0;
	while (x < len && stream.remaining() > 0)
	{
		const auto control = static_cast<int8_t>(stream.read_u8());
		if (control >= 0)
		{
			const auto count = static_cast<int>(control) + 1;
			if (count > len - x || static_cast<uint64_t>(count) > stream.remaining()) return false;
			for (auto i = 0; i < count; ++i)
			{
				dst[x++] = stream.read_u8();
			}
		}
		else if (control != -128)
		{
			const auto count = 1 - static_cast<int>(control);
			if (count > len - x || stream.remaining() < 1) return false;
			const auto pixel = stream.read_u8();
			for (auto i = 0; i < count; ++i) dst[x++] = pixel;
		}
	}

	return x == len && stream.remaining() == 0;
}

// Merges one decoded plane into a surface row. Bitmap mode packs eight pixels per byte,
// most-significant bit first, and a set bit means black; every other mode is a byte per pixel.
static void scatter_plane(uint8_t* const dst_line, const uint8_t* const src, const int cx, const bool is_bitmap,
                          const int channel_shift)
{
	auto* const line = std::bit_cast<uint32_t*>(dst_line);

	if (is_bitmap)
	{
		for (int x = 0; x < cx; x++)
		{
			const auto is_black = (src[x / 8] >> (7 - (x % 8))) & 1;
			line[x] |= (is_black ? 0u : 0xFFu) << channel_shift;
		}
	}
	else
	{
		for (int x = 0; x < cx; x++)
		{
			line[x] |= static_cast<uint32_t>(src[x]) << channel_shift;
		}
	}
}


static void lab_to_rgb(const int L, const int a, const int b, int& R, int& G, int& B)
{
	// Convert between RGB and CIE-Lab color spaces
	// Uses ITU-R recommendation BT.709 with D65 as reference white.
	// algorithm contributed by "Mark A. Ruzon" <ruzon@CS.Stanford.EDU>
	double X, Z;
	double fY = pow((L + 16.0) / 116.0, 3.0);
	if (fY < 0.008856)
		fY = L / 903.3;
	double Y = fY;

	if (fY > 0.008856)
		fY = pow(fY, 1.0 / 3.0);
	else
		fY = 7.787 * fY + 16.0 / 116.0;

	const double fX = a / 500.0 + fY;
	if (fX > 0.206893)
		X = pow(fX, 3.0);
	else
		X = (fX - 16.0 / 116.0) / 7.787;

	const double fZ = fY - b / 200.0;
	if (fZ > 0.206893)
		Z = pow(fZ, 3.0);
	else
		Z = (fZ - 16.0 / 116.0) / 7.787;

	X *= 0.950456 * 255;
	Y *= 255;
	Z *= 1.088754 * 255;

	const int RR = static_cast<int>(3.240479 * X - 1.537150 * Y - 0.498535 * Z + 0.5);
	const int GG = static_cast<int>(-0.969256 * X + 1.875992 * Y + 0.041556 * Z + 0.5);
	const int BB = static_cast<int>(0.055648 * X - 0.204043 * Y + 1.057311 * Z + 0.5);

	R = RR < 0 ? 0 : RR > 255 ? 255 : RR;
	G = GG < 0 ? 0 : GG > 255 ? 255 : GG;
	B = BB < 0 ? 0 : BB > 255 ? 255 : BB;
}

static bool lab_to_rgb(const ui::surface_ptr& imageIn)
{
	int bb, gg, rr;

	const auto cy = imageIn->height();
	const auto cx = imageIn->width();
	const auto stride = imageIn->stride();
	auto* const pixels = imageIn->pixels();

	for (auto y = 0u; y < cy; y++)
	{
		// stride is padded, so it is not safe to index the surface as width-sized rows.
		auto* const line = std::bit_cast<uint32_t*>(pixels + static_cast<size_t>(y) * stride);

		for (auto x = 0u; x < cx; x++)
		{
			const auto c = line[x];

			const int b = ui::get_r(c);
			const int a = ui::get_g(c);
			const int l = ui::get_b(c);

			lab_to_rgb(l, a, b, rr, gg, bb);
			constexpr int aa = 255;
			line[x] = ui::rgba(bb, gg, rr, aa);
		}
	}

	return true;
}

static bool cmy_to_rgb(const ui::surface_ptr& imageIn)
{
	const auto cy = imageIn->height();
	const auto cx = imageIn->width();
	const auto stride = imageIn->stride();
	auto* const pixels = imageIn->pixels();

	for (auto y = 0u; y < cy; y++)
	{
		auto* const line = std::bit_cast<uint32_t*>(pixels + static_cast<size_t>(y) * stride);

		for (auto x = 0u; x < cx; x++)
		{
			const auto c = line[x];

			// Signed arithmetic is required: ink + black routinely exceeds 255 and the
			// unsigned form wrapped to a huge positive value, so byte_clamp saturated
			// dark pixels to white instead of black.
			const int bb = ui::get_r(c);
			const int gg = ui::get_g(c);
			const int rr = ui::get_b(c);
			const int aa = ui::get_a(c);

			line[x] = ui::rgba(df::byte_clamp(255 - (bb + aa)),
			                   df::byte_clamp(255 - (gg + aa)),
			                   df::byte_clamp(255 - (rr + aa)),
			                   255);
		}
	}

	return true;
}


// Header sanity limits, shared by the scanner and the loader so an image that
// scans successfully is one we can actually decode.
constexpr uint32_t max_psd_dimension = 30000;
constexpr uint64_t max_psd_pixels = 256ull * 1024ull * 1024ull;

static bool is_valid_psd_header(const uint32_t columns, const uint32_t rows, const uint16_t channels,
                                const uint16_t depth, const uint16_t mode)
{
	// Bitmap mode is the only depth besides 8 we decode: a single 1-bit-per-pixel plane.
	const auto depth_ok = depth == 8 || (depth == 1 && mode == BitmapMode && channels == 1);

	return columns != 0 && rows != 0 && columns <= max_psd_dimension && rows <= max_psd_dimension &&
		static_cast<uint64_t>(columns) * rows <= max_psd_pixels && channels != 0 && channels <= 56 && depth_ok;
}


file_scan_result scan_psd(read_stream& s)
{
	file_scan_result result;
	msb_stream stream(s);

	const auto signature = stream.read_u32();
	const auto version = stream.read_u16();

	if (signature != 0x38425053 || version != 1)
	{
		return result;
	}

	stream.skip(6); // reserved

	const auto channels = stream.read_u16();
	const auto rows = stream.read_u32();
	const auto columns = stream.read_u32();
	const auto depth = stream.read_u16();
	const auto mode = stream.read_u16();

	if (!is_valid_psd_header(columns, rows, channels, depth, mode))
	{
		return result;
	}

	switch (mode)
	{
	case BitmapMode:
		result.pixel_format = "mono"_c;
		break;
	case RGBMode:
		result.pixel_format = channels >= 4 ? "argb32"_c : "rgb32"_c;
		break;
	case LabMode:
		result.pixel_format = "lab"_c;
		break;
	case CMYKMode:
		result.pixel_format = "cmyk"_c;
		break;
	case GrayscaleMode:
		result.pixel_format = "gray8"_c;
		break;
	case IndexedMode:
		result.pixel_format = "pal8"_c;
		break;
	case MultichannelMode:
		result.pixel_format = "multichannel"_c;
		break;
	case DuotoneMode:
		result.pixel_format = "duotone"_c;
		break;
	}

	result.width = columns;
	result.height = rows;

	// Read PSD raster colormap only present for indexed and duotone images.
	const auto colormap_len = stream.read_u32();

	if (colormap_len != 0)
	{
		stream.skip(colormap_len);
	}

	// Resources
	const auto resources_len = stream.read_u32();

	// A truncated file must not throw away the dimensions already parsed, so stop before
	// walking a resource section that runs past the end.
	if (resources_len > stream.remaining())
	{
		result.success = true;
		return result;
	}

	const auto after_resource_pos = stream.pos() + resources_len;

	if (resources_len > 6)
	{
		auto marker = stream.read_u32();

		while (marker == 0x3842494D) // 8BIM
		{
			const auto type = stream.read_u16();

			// The resource name is a Pascal string padded so the length byte plus the name
			// occupy an even number of bytes. Photoshop leaves it empty for the standard
			// resources, but a named one desynchronises a fixed two-byte skip and the rest
			// of the section - including IPTC and XMP - is then read as garbage.
			const auto name_len = stream.read_u8();
			stream.skip((name_len & 1) ? name_len : name_len + 1u);

			const uint64_t len = stream.read_u32();
			const auto padded_len = len + (len & 1); // resource data is padded to even

			if (stream.pos() + padded_len > after_resource_pos)
				break;

			if (type == 0x0404) // IPTC
			{
				result.metadata.iptc = stream.read_blob(len);
			}
			else if (type == 0x0424) // XMP
			{
				result.metadata.xmp = stream.read_blob(len);
			}
			else if (type == 0x0422) // EXIF
			{
				result.metadata.exif = stream.read_blob(len);
			}
			else if (type == 0x040f) // icc
			{
				result.metadata.icc = stream.read_blob(len);
			}
			else
			{
				stream.skip(len);
			}

			stream.skip(padded_len - len);

			if (stream.pos() + 4u > after_resource_pos)
				break;

			marker = stream.read_u32();
		}
	}

	result.success = true;
	return result;
}

ui::surface_ptr load_psd(read_stream& s, load_diagnostic* const diagnostic)
{
	msb_stream stream(s);

	const auto signature = stream.read_u32();
	const auto version = stream.read_u16();

	if (signature != 0x38425053 || version != 1)
	{
		return {};
	}

	stream.skip(6); // reserved

	const auto channels = stream.read_u16();
	const auto rows = stream.read_u32();
	const auto columns = stream.read_u32();
	const auto depth = stream.read_u16();
	const auto mode = stream.read_u16();

	if (!is_valid_psd_header(columns, rows, channels, depth, mode))
	{
		return {};
	}

	const int cx = columns;
	const int cy = rows;

	if (reject_over_budget_source(diagnostic, {cx, cy}, "PSD"))
	{
		return {};
	}

	const bool is_bitmap = mode == BitmapMode;
	bool is_single_channel = is_bitmap;

	// cannot yet handle all modes
	switch (mode)
	{
	case BitmapMode:
	case RGBMode:
	case LabMode:
	case CMYKMode:
		break;

	case GrayscaleMode:
	case IndexedMode:
	case MultichannelMode:
	case DuotoneMode:
		is_single_channel = true;
		break;
	}

	// Read PSD raster colormap only present for indexed and duotone images.
	auto colormap_len = stream.read_u32();
	unsigned num_colors = 0;
	uint32_t palette[256] = {};

	if (colormap_len != 0)
	{
		const auto buffer = stream.read_blob(colormap_len);

		if (mode == DuotoneMode)
		{
			// Duotone image data; the format of this data is undocumented.			
		}
		else if (mode == IndexedMode && colormap_len == 768)
		{
			// Read PSD raster colormap.
			num_colors = colormap_len / 3;
		}

		if (num_colors)
		{
			const auto* const data = buffer.data();

			for (unsigned i = 0; i < static_cast<unsigned>(std::min(num_colors, 256u)); i++)
			{
				palette[i] = ui::rgb(
					data[i + 2 * num_colors],
					data[i + num_colors],
					data[i]);
			}
		}
	}
	if (mode == IndexedMode && num_colors != 256) return {};

	// Resources
	const auto resources_len = stream.read_u32();
	if (resources_len > stream.remaining()) return {};
	const auto after_resource_pos = stream.pos() + resources_len;
	stream.pos(after_resource_pos);

	// Layer and mask block.
	colormap_len = stream.read_u32();

	if (colormap_len == 8)
	{
		colormap_len = stream.read_u32();
		colormap_len = stream.read_u32();
	}
	if (colormap_len != 0)
	{
		stream.skip(colormap_len);
	}

	ui::surface_ptr result = std::make_shared<ui::surface>();
	if (!result->alloc(cx, cy, ui::texture_format::RGB)) return {};
	result->make_blank();

	// Read the precombined image, present for PSD < 4 compatibility
	const auto compression = stream.read_u16();
	if (compression > 1) return {};

	// Grayscale, duotone, indexed and bitmap images are post-processed from the low byte,
	// so their single plane must be decoded there rather than into the red lane.
	const auto plane_shift = [is_single_channel](const int channel)
	{
		return is_single_channel ? 0 : channel_to_channel_shift(channel);
	};

	// A bitmap-mode row is packed eight pixels to the byte; every other mode is one byte each.
	const int plane_bytes = is_bitmap ? (cx + 7) / 8 : cx;
	const auto plane_buffer = df::unique_alloc<uint8_t>(plane_bytes);
	auto* const plane_data = plane_buffer.get();

	if (compression == 1)
	{
		const auto scanline_count = static_cast<size_t>(cy) * channels;
		std::vector<uint16_t> scanline_lengths(scanline_count);
		for (auto& scanline_length : scanline_lengths)
		{
			scanline_length = stream.read_u16();
		}

		const auto decoded_channels = is_single_channel ? 1u : channels;
		for (auto channel = 0u; channel < decoded_channels; ++channel)
		{
			for (auto y = 0; y < cy; ++y)
			{
				const auto line_length = scanline_lengths[static_cast<size_t>(channel) * cy + y];
				if (line_length > stream.remaining()) return {};
				const auto line_data = stream.read_blob(line_length);
				mem_read_stream line_source(line_data);
				msb_stream line_stream(line_source);
				if (!decode_rle_bytes(plane_data, plane_bytes, line_stream)) return {};
				scatter_plane(result->pixels_line(y), plane_data, cx, is_bitmap, plane_shift(channel));
			}
		}
		for (auto channel = decoded_channels; channel < channels; ++channel)
		{
			for (auto y = 0; y < cy; ++y) stream.skip(scanline_lengths[static_cast<size_t>(channel) * cy + y]);
		}
	}
	else
	{
		// Read uncompressed pixel data as separate planes.
		const auto decoded_channels = is_single_channel ? 1 : channels;

		for (auto channel = 0; channel < decoded_channels; channel++)
		{
			for (auto y = 0; y < cy; ++y)
			{
				stream.read(plane_data, plane_bytes);
				scatter_plane(result->pixels_line(y), plane_data, cx, is_bitmap, plane_shift(channel));
			}
		}
	}

	if (mode == CMYKMode)
	{
		// Convert to rgb
		cmy_to_rgb(result);
	}
	else if (mode == LabMode)
	{
		lab_to_rgb(result);
	}
	else if (mode == GrayscaleMode || mode == DuotoneMode || mode == BitmapMode)
	{
		const auto* const pixels = result->pixels();
		const auto stride = result->stride();

		for (auto y = 0; y < cy; y++)
		{
			auto* const line = std::bit_cast<uint32_t*>(pixels + y * stride);

			for (auto x = 0; x < cx; x++)
			{
				const auto g = line[x] & 0xFF;
				line[x] = ui::rgb(g, g, g);
			}
		}
	}
	else if (mode == IndexedMode)
	{
		const auto* const pixels = result->pixels();
		const auto stride = result->stride();

		for (auto y = 0; y < cy; y++)
		{
			auto* const line = std::bit_cast<uint32_t*>(pixels + y * stride);

			for (auto x = 0; x < cx; x++)
			{
				line[x] = palette[line[x] & 0xFF];
			}
		}
	}

	return result;
}
