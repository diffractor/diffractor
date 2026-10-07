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

static constexpr size_t max_psd_metadata_bytes = 16u * 1024u * 1024u;
static constexpr std::string_view photoshop_signature = "Photoshop 3.0\0"sv;

static uint16_t read_be16(const uint8_t* p)
{
	return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

static uint32_t read_be32(const uint8_t* p)
{
	return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 |
		static_cast<uint32_t>(p[2]) << 8 | p[3];
}

static void assign_bounded_resource(df::blob& dst, const df::cspan payload)
{
	if (payload.size > max_psd_metadata_bytes)
	{
		df::log(__FUNCTION__, std::format("PSD metadata resource is too large to read ({})",
		                                  df::file_size(payload.size).str()));
		return;
	}

	dst.assign(payload.begin(), payload.end());
}

bool parse_photoshop_resources(metadata_parts& metadata, df::cspan resources, const bool has_photoshop_signature)
{
	if (has_photoshop_signature)
	{
		if (resources.size < photoshop_signature.size() ||
			memcmp(resources.data, photoshop_signature.data(), photoshop_signature.size()) != 0)
		{
			return false;
		}

		resources.data += photoshop_signature.size();
		resources.size -= photoshop_signature.size();
	}

	size_t pos = 0;
	size_t accepted_bytes = 0;

	while (pos + 12u <= resources.size)
	{
		if (memcmp(resources.data + pos, "8BIM", 4) != 0)
			return false;

		pos += 4;
		const auto type = read_be16(resources.data + pos);
		pos += 2;

		const auto name_len = resources.data[pos++];
		const auto name_bytes = static_cast<size_t>(name_len) + ((name_len & 1u) ? 0u : 1u);
		if (name_bytes > resources.size - pos)
			return false;
		pos += name_bytes;

		if (resources.size - pos < 4u)
			return false;
		const auto len = static_cast<size_t>(read_be32(resources.data + pos));
		pos += 4;

		const auto padded_len = len + (len & 1u);
		if (padded_len > resources.size - pos)
			return false;

		const df::cspan payload{resources.data + pos, len};
		const auto resource_start = pos - 11u - name_bytes;
		if (type == 0x0404 || type == 0x0424 || type == 0x0422 || type == 0x040f)
		{
			if (len > max_psd_metadata_bytes || accepted_bytes > max_psd_metadata_bytes - len)
			{
				df::log(__FUNCTION__, std::format("PSD metadata resources are too large to read ({})",
				                                  df::file_size(accepted_bytes + len).str()));
			}
			else
			{
				accepted_bytes += len;

				if (type == 0x0404) assign_bounded_resource(metadata.iptc, payload);
				else if (type == 0x0424) assign_bounded_resource(metadata.xmp, payload);
				else if (type == 0x0422) assign_bounded_resource(metadata.exif, payload);
				else if (type == 0x040f) assign_bounded_resource(metadata.icc, payload);
			}
		}
		else if (type != 0x0425)
		{
			metadata.photoshop_resources.insert(metadata.photoshop_resources.end(),
			                                    resources.data + resource_start,
			                                    resources.data + pos + padded_len);
		}

		pos += padded_len;
	}

	return true;
}

df::blob make_photoshop_iptc_resource(const df::cspan iptc, const bool include_photoshop_signature)
{
	df::blob result;
	if (iptc.size > (std::numeric_limits<uint32_t>::max)()) throw app_exception("IPTC block is too large");

	const auto header = include_photoshop_signature ? photoshop_signature.size() : 0_z;
	result.reserve(header + 4u + 2u + 2u + 4u + iptc.size + (iptc.size & 1u));
	if (include_photoshop_signature)
		result.insert(result.end(), photoshop_signature.begin(), photoshop_signature.end());

	if (!iptc.empty())
	{
		constexpr std::array<uint8_t, 8> resource_header = {'8', 'B', 'I', 'M', 0x04, 0x04, 0, 0};
		result.insert(result.end(), resource_header.begin(), resource_header.end());
		const auto len = static_cast<uint32_t>(iptc.size);
		result.push_back(static_cast<uint8_t>(len >> 24));
		result.push_back(static_cast<uint8_t>(len >> 16));
		result.push_back(static_cast<uint8_t>(len >> 8));
		result.push_back(static_cast<uint8_t>(len));
		result.insert(result.end(), iptc.begin(), iptc.end());
		if (iptc.size & 1u) result.push_back(0);
	}
	return result;
}

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


static const std::array<int, 4097>& linear_to_srgb_table()
{
	static const auto table = []
	{
		std::array<int, 4097> result{};
		for (auto i = 0_z; i < result.size(); ++i)
		{
			const auto v = static_cast<double>(i) / 4096.0;
			const auto srgb = v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
			result[i] = df::byte_clamp(df::round(srgb * 255.0));
		}
		return result;
	}();
	return table;
}

static int linear_to_srgb_byte(double v)
{
	const auto clamped = std::clamp(v, 0.0, 1.0);
	const auto index = df::round(clamped * 4096.0);
	return linear_to_srgb_table()[index];
}

struct lab_l_lookup
{
	double y = 0.0;
	double fy = 0.0;
};

static const std::array<lab_l_lookup, 256>& lab_l_table()
{
	static const auto table = []
	{
		std::array<lab_l_lookup, 256> result{};
		for (auto i = 0_z; i < result.size(); ++i)
		{
			const auto L = static_cast<double>(i) * 100.0 / 255.0;
			const auto fy = (L + 16.0) / 116.0;
			auto y = fy * fy * fy;
			if (y < 0.008856) y = L / 903.3;
			result[i].y = y;
			result[i].fy = y > 0.008856 ? fy : 7.787 * y + 16.0 / 116.0;
		}
		return result;
	}();
	return table;
}

static double lab_cube(const double v)
{
	return v * v * v;
}

static void lab_to_rgb(const int stored_l, const int a, const int b, int& R, int& G, int& B)
{
	// Convert between RGB and CIE-Lab color spaces
	// Uses ITU-R recommendation BT.709 with D65 as reference white.
	// algorithm contributed by "Mark A. Ruzon" <ruzon@CS.Stanford.EDU>
	double X, Z;
	const auto& lookup = lab_l_table()[stored_l];
	const auto Y = lookup.y;
	const auto fY = lookup.fy;

	const double fX = a / 500.0 + fY;
	if (fX > 0.206893)
		X = lab_cube(fX);
	else
		X = (fX - 16.0 / 116.0) / 7.787;

	const double fZ = fY - b / 200.0;
	if (fZ > 0.206893)
		Z = lab_cube(fZ);
	else
		Z = (fZ - 16.0 / 116.0) / 7.787;

	X *= 0.950456;
	Z *= 1.088754;

	const auto RR = 3.240479 * X - 1.537150 * Y - 0.498535 * Z;
	const auto GG = -0.969256 * X + 1.875992 * Y + 0.041556 * Z;
	const auto BB = 0.055648 * X - 0.204043 * Y + 1.057311 * Z;

	R = linear_to_srgb_byte(RR);
	G = linear_to_srgb_byte(GG);
	B = linear_to_srgb_byte(BB);
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

			const int b = ui::get_r(c) - 128;
			const int a = ui::get_g(c) - 128;
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

	if (resources_len > 0)
	{
		size_t accepted_bytes = 0;

		while (stream.pos() + 12u <= after_resource_pos)
		{
			if (stream.read_u32() != 0x3842494D) break;
			const auto type = stream.read_u16();
			const auto name_len = stream.read_u8();
			stream.skip((name_len & 1u) ? name_len : name_len + 1u);
			const uint64_t len = stream.read_u32();
			const auto padded_len = len + (len & 1u);
			if (stream.pos() + padded_len > after_resource_pos) break;

			if (type == 0x0404 || type == 0x0424 || type == 0x0422 || type == 0x040f)
			{
				if (len > max_psd_metadata_bytes || accepted_bytes > max_psd_metadata_bytes - len)
				{
					df::log(__FUNCTION__, std::format("PSD metadata resources are too large to read ({})",
					                                  df::file_size(accepted_bytes + len).str()));
					stream.skip(len);
				}
				else
				{
					accepted_bytes += static_cast<size_t>(len);
					auto payload = stream.read_blob(len);
					if (type == 0x0404) result.metadata.iptc = std::move(payload);
					else if (type == 0x0424) result.metadata.xmp = std::move(payload);
					else if (type == 0x0422) result.metadata.exif = std::move(payload);
					else if (type == 0x040f) result.metadata.icc = std::move(payload);
				}
			}
			else
			{
				stream.skip(len);
			}

			stream.skip(padded_len - len);
		}

		stream.pos(after_resource_pos);
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
		if (mode == DuotoneMode)
		{
			// Duotone image data; the format of this data is undocumented.			
			stream.skip(colormap_len);
		}
		else if (mode == IndexedMode && colormap_len == 768)
		{
			// Read PSD raster colormap.
			num_colors = colormap_len / 3;
			const auto buffer = stream.read_blob(colormap_len);
			const auto* const data = buffer.data();

			for (unsigned i = 0; i < static_cast<unsigned>(std::min(num_colors, 256u)); i++)
			{
				palette[i] = ui::rgb(
					data[i + 2 * num_colors],
					data[i + num_colors],
					data[i]);
			}
		}
		else
		{
			stream.skip(colormap_len);
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
