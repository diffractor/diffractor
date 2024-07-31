// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2024  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

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

	return 0;
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
		return _len - _pos;
	}

	void read(uint8_t* data, uint64_t size)
	{
		if (remaining() < size) throw app_exception(__FUNCTION__);
		_s.read(_pos, data, size);
		_pos += size;
	}

	df::blob read_blob(uint64_t size)
	{
		if (remaining() < size) throw app_exception(__FUNCTION__);
		auto result = _s.read(_pos, size);
		_pos += size;
		return result;
	}

	void skip(uint64_t size)
	{
		if (remaining() < size) throw app_exception(__FUNCTION__);
		_pos += size;
	}

	void pos(uint64_t p)
	{
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


static bool decode_rle_pane(ui::surface_ptr& surface, msb_stream& stream, const int channel_shift,
	const int channel_scale)
{
	const int cx = surface->width();
	const int cy = surface->height();
	const auto* const pixels = surface->pixels();
	const auto stride = surface->stride();

	int number_pixels = cy * cx;
	uint32_t pixel;
	bool bReading = false;
	int count = 0;
	int x = 0, y = 0;
	auto* line = std::bit_cast<uint32_t*>(pixels + (y * stride));

	while (number_pixels > 0)
	{
		if (count <= 0)
		{
			count = stream.read_u8();

			if (count >= 128)
				count -= 256;

			if (count == -128)
			{
				continue;
			}
			if (count < 0)
			{
				pixel = stream.read_u8();
				count = (-count + 1);
				bReading = false;
			}
			else
			{
				count++;
				bReading = true;
			}
		}

		if (bReading)
		{
			pixel = stream.read_u8();
		}

		line[x] |= (0xFF & pixel) << channel_shift;

		x++;
		number_pixels--;
		count--;

		if (x >= cx)
		{
			y++;
			x = 0;
			line = std::bit_cast<uint32_t*>(pixels + (y * stride));
		}
	}

	// Guarentee the correct number of pixel packets.
	return number_pixels == 0;
}

static bool decode_uncompressed_plane(ui::surface_ptr& surface, msb_stream& stream, const int channel_shift,
	const int channel_scale)
{
	const int cx = surface->width();
	const int cy = surface->height();
	const auto* const pixels = surface->pixels();
	const auto stride = surface->stride();
	const auto line_buffer = df::unique_alloc<uint8_t>(cx);
	auto* const line_data = line_buffer.get();

	// Read uncompressed pixel data as separate planes.
	for (int y = 0; y < cy; y++)
	{
		stream.read(line_data, cx);

		auto* const dst_line = std::bit_cast<uint32_t*>(pixels + (y * stride));

		for (int x = 0; x < static_cast<long>(cx); x++)
		{
			dst_line[x] |= (0xFF & static_cast<uint32_t>(line_data[x])) << channel_shift;
		}
	}

	return true;
}


using Quantum = int;
using IndexPacket = int;
using PixelPacket = uint32_t;

const int MaxTextExtent = 300;
const int MaxRGB = 255;


static void lab_to_rgb(int L, int a, int b, int& R, int& G, int& B)
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

	X *= (0.950456 * 255);
	Y *= 255;
	Z *= (1.088754 * 255);

	const int RR = static_cast<int>(3.240479 * X - 1.537150 * Y - 0.498535 * Z + 0.5);
	const int GG = static_cast<int>(-0.969256 * X + 1.875992 * Y + 0.041556 * Z + 0.5);
	const int BB = static_cast<int>(0.055648 * X - 0.204043 * Y + 1.057311 * Z + 0.5);

	R = RR < 0 ? 0 : RR > 255 ? 255 : RR;
	G = GG < 0 ? 0 : GG > 255 ? 255 : GG;
	B = BB < 0 ? 0 : BB > 255 ? 255 : BB;
}

static bool lab_to_rgb(ui::surface_ptr& imageIn)
{
	int bb, gg, rr;

	const auto cy = imageIn->height();
	const auto cx = imageIn->width();
	auto* const pixels = std::bit_cast<uint32_t*>(imageIn->pixels());

	for (auto y = 0u; y < cy; y++)
	{
		auto* const line = pixels + (y * cx);

		for (auto x = 0u; x < cx; x++)
		{
			const auto c = line[x];

			const int b = ui::get_r(c);
			const int a = ui::get_g(c);
			const int l = ui::get_b(c);

			lab_to_rgb(l, a, b, rr, gg, bb);
			const int aa = 255;
			line[x] = ui::rgba(bb, gg, rr, aa);
		}
	}

	return true;
}

static bool cmy_to_rgb(ui::surface_ptr& imageIn)
{
	const auto cy = imageIn->height();
	const auto cx = imageIn->width();
	auto* const pixels = std::bit_cast<uint32_t*>(imageIn->pixels());

	for (auto y = 0u; y < cy; y++)
	{
		auto* const line = pixels + (y * cx);

		for (auto x = 0u; x < cx; x++)
		{
			const auto c = line[x];

			uint32_t bb = ui::get_r(c);
			uint32_t gg = ui::get_g(c);
			uint32_t rr = ui::get_b(c);
			uint32_t aa = ui::get_a(c);

			rr = df::byte_clamp(255 - (rr + aa));
			gg = df::byte_clamp(255 - (gg + aa));
			bb = df::byte_clamp(255 - (bb + aa));
			aa = 255;

			line[x] = ui::rgba(bb, gg, rr, aa);
		}
	}

	return true;
};


struct ChannelInfo
{
	short int type;
	unsigned long size;
};

struct RectangleInfo
{
	uint32_t width, height;
	int x, y;
};

struct LayerInfo
{
	RectangleInfo page, mask;
	uint16_t channels;
	ChannelInfo channel_info[24];
	uint8_t blendkey[4];
	Quantum opacity;
	uint8_t clipping, visible, flags;
	uint32_t offset_x, offset_y;
	uint8_t name[256];
} LayerInfo;


file_scan_result scan_psd(read_stream& s)
{
	file_scan_result result;
	msb_stream stream(s);

	const auto signature = stream.read_u32();
	const auto version = stream.read_u16();

	if (signature == 0x38425053 && version == 1)
	{
		stream.skip(6); // reserved

		const auto channels = stream.read_u16();
		const auto rows = stream.read_u32();
		const auto columns = stream.read_u32();
		const auto depth = stream.read_u16();
		const auto mode = stream.read_u16();

		//auto pixel_format_description = "RGB"sv;

		switch (mode)
		{
		case BitmapMode:
			result.pixel_format = u8"mono"_c;
			break;
		case RGBMode:
			result.pixel_format = channels >= 4 ? u8"argb32"_c : u8"rgb32"_c;
			break;
		case LabMode:
			result.pixel_format = u8"lab"_c;
			break;
		case CMYKMode:
			result.pixel_format = u8"cmyk"_c;
			break;
		case GrayscaleMode:
			result.pixel_format = u8"gray8"_c;
			break;
		case IndexedMode:
			result.pixel_format = u8"pal8"_c;
			break;
		case MultichannelMode:
			result.pixel_format = u8"multichannel"_c;
			break;
		case DuotoneMode:
			result.pixel_format = u8"duotone"_c;
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
		const auto after_resource_pos = stream.pos() + resources_len;

		if (resources_len > 6)
		{
			auto marker = stream.read_u32();

			while (marker == 0x3842494D) // 8BIM
			{
				const auto type = stream.read_u16();
				auto pad = stream.read_u16();
				auto len = stream.read_u32();

				if (len & 0x01) len += 1; // round up to 2 

				if ((stream.pos() + len) >= after_resource_pos)
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

				if (stream.pos() >= after_resource_pos)
					break;

				marker = stream.read_u32();
			}
		}
	}

	result.success = true;
	return result;
}

ui::surface_ptr load_psd(read_stream& s)
{
	ui::surface_ptr result;
	msb_stream stream(s);

	const auto signature = stream.read_u32();
	const auto version = stream.read_u16();

	if ((signature != 0x38425053) || (version != 1))
	{
		return {};
	}

	stream.skip(6); // reserved

	const auto channels = stream.read_u16();
	const auto rows = stream.read_u32();
	const auto columns = stream.read_u32();
	const auto depth = stream.read_u16();
	const auto mode = stream.read_u16();

	auto pf = u8"argb_32"_c;
	const int cx = columns;
	const int cy = rows;
	bool has_alpha = false;
	bool is_single_channel = false;

	// cannot yet handel all modes
	switch (mode)
	{
	case BitmapMode:
		pf = u8"mono"_c;
		break;
	case RGBMode:
	case LabMode:
		has_alpha = (channels >= 4);
		pf = has_alpha ? u8"argb_32"_c : u8"rgb_24"_c;
		break;

	case CMYKMode:
		has_alpha = (channels >= 5);
		pf = has_alpha ? u8"argb_32"_c : u8"rgb_24"_c;
		break;

	case GrayscaleMode:
		pf = u8"gray_8"_c;
		is_single_channel = true;
		break;
	case IndexedMode:
	case MultichannelMode:
	case DuotoneMode:
		pf = u8"pal_8"_c;
		is_single_channel = true;
		break;
	}

	// Read PSD raster colormap only present for indexed and duotone images.
	auto colormap_len = stream.read_u32();
	unsigned num_colors = 0;
	uint32_t palette[256];

	if (colormap_len != 0)
	{
		const auto buffer = stream.read_blob(colormap_len);

		if (mode == DuotoneMode)
		{
			// Duotone image data;  the format of this data is undocumented.			
		}
		else
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
					data[i + (2 * num_colors)],
					data[i + num_colors],
					data[i]);
			}
		}
	}

	// Resources
	const auto resources_len = stream.read_u32();
	const auto after_resource_pos = stream.pos() + resources_len;
	stream.pos(after_resource_pos);

	// Layer and mask block.		
	const auto number_layers = 0;
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
	else
	{
		//  image has no layers?
	}

	if constexpr (number_layers == 0)
	{
		result = std::make_shared<ui::surface>();
		result->alloc(cx, cy, ui::texture_format::RGB);

		const auto* const pixels = result->pixels();
		const auto stride = result->stride();

		for (auto y = 0; y < cy; y++)
		{
			auto* const line = std::bit_cast<uint32_t*>(pixels + (y * stride));

			for (auto x = 0; x < cx; x++)
			{
				line[x] = 0;
			}
		}

		// Read the precombined image, present for PSD < 4 compatibility
		const auto compression = stream.read_u16();

		if (compression == 1)
		{
			// Read Packbit encoded pixel data as separate planes.
			for (auto i = 0; i < static_cast<long>(cy * channels); i++)
			{
				stream.read_u16();
			}


			if (is_single_channel)
			{
				decode_rle_pane(result, stream, 0, 0);
			}
			else
			{
				for (auto i = 0; i < channels; i++)
				{
					decode_rle_pane(result, stream, channel_to_channel_shift(i), 0);
				}
			}
		}
		else
		{
			// Read uncompressed pixel data as separate planes.
			if (is_single_channel)
			{
				decode_uncompressed_plane(result, stream, 0, 0);
			}
			else
			{
				for (auto i = 0; i < channels; i++)
				{
					decode_uncompressed_plane(result, stream, channel_to_channel_shift(i), 0);
				}
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
	else if (mode == GrayscaleMode || mode == DuotoneMode)
	{
		const auto* const pixels = result->pixels();
		const auto stride = result->stride();

		for (auto y = 0; y < cy; y++)
		{
			auto* const line = std::bit_cast<uint32_t*>(pixels + (y * stride));

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
			auto* const line = std::bit_cast<uint32_t*>(pixels + (y * stride));

			for (auto x = 0; x < cx; x++)
			{
				line[x] = palette[line[x] & 0xFF];
			}
		}
	}

	return result;
}
