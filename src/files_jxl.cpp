// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: JPEG XL image format support. Decodes JPEG XL files using libjxl,
// extracts basic info and Exif/XMP metadata boxes.

#include "pch.h"
#include "files.h"
#include "files_jpeg.h"

#include <jxl/decode.h>

// Metadata boxes are attacker-controlled and JxlDecoderSetDecompressBoxes expands 'brob'
// boxes for us, so cap the total payload we are willing to buffer for a single box.
constexpr size_t max_metadata_bytes = 16u * 1024u * 1024u;
constexpr size_t box_chunk = 64u * 1024u;

// JxlOrientation uses the same numbering as EXIF (1..8), which matches ui::orientation.
static ui::orientation to_orientation(const uint32_t o)
{
	if (o >= JXL_ORIENT_IDENTITY && o <= JXL_ORIENT_ROTATE_90_CCW)
	{
		return static_cast<ui::orientation>(o);
	}

	return ui::orientation::top_left;
}

static str::cached to_pixel_format(const JxlBasicInfo& info)
{
	if (info.num_color_channels == 1) return "grayscale"_c;
	return info.alpha_bits > 0 ? "rgba"_c : "rgb"_c;
}

file_scan_result scan_jxl(read_stream& s)
{
	file_scan_result result = {};
	df::blob owned;
	const auto data = s.view_all(owned);

	const df::releaser<JxlDecoder> dec(JxlDecoderCreate(nullptr), [](auto* d) { JxlDecoderDestroy(d); });
	if (!dec.get()) return result;

	if (JxlDecoderSubscribeEvents(dec.get(), JXL_DEC_BASIC_INFO | JXL_DEC_BOX) != JXL_DEC_SUCCESS)
	{
		return result;
	}

	JxlDecoderSetDecompressBoxes(dec.get(), JXL_TRUE);

	// By default libjxl re-orients the pixel data and reports xsize/ysize already rotated,
	// with orientation forced to identity. Scanning records unrotated dimensions plus a
	// separate orientation (scan_photo applies the Exif tag afterwards), so keep the
	// bitstream geometry here or the rotation would be counted twice.
	JxlDecoderSetKeepOrientation(dec.get(), JXL_TRUE);

	JxlDecoderSetInput(dec.get(), data.data, data.size);
	JxlDecoderCloseInput(dec.get());

	enum class box_kind { none, exif, xmp };

	df::blob box_data;
	auto current_kind = box_kind::none;

	const auto finalize_box = [&]()
	{
		if (current_kind == box_kind::none) return;

		const auto remaining = JxlDecoderReleaseBoxBuffer(dec.get());
		box_data.resize(box_data.size() - remaining);

		if (current_kind == box_kind::exif)
		{
			result.metadata.exif = strip_exif_tiff_prefix(std::move(box_data));
		}
		else if (current_kind == box_kind::xmp)
		{
			result.metadata.xmp = std::move(box_data);
		}

		box_data.clear();
		current_kind = box_kind::none;
	};

	for (;;)
	{
		const auto status = JxlDecoderProcessInput(dec.get());

		if (status == JXL_DEC_BASIC_INFO)
		{
			JxlBasicInfo info = {};

			if (JxlDecoderGetBasicInfo(dec.get(), &info) == JXL_DEC_SUCCESS)
			{
				result.width = info.xsize;
				result.height = info.ysize;
				result.orientation = to_orientation(info.orientation);
				result.pixel_format = to_pixel_format(info);
				result.success = true;
			}
		}
		else if (status == JXL_DEC_BOX)
		{
			finalize_box();

			JxlBoxType type = {};

			if (JxlDecoderGetBoxType(dec.get(), type, JXL_TRUE) == JXL_DEC_SUCCESS)
			{
				if (memcmp(type, "Exif", 4) == 0) current_kind = box_kind::exif;
				else if (memcmp(type, "xml ", 4) == 0) current_kind = box_kind::xmp;

				if (current_kind != box_kind::none)
				{
					// The raw size is only a hint (a compressed box expands past it), but it
					// avoids reallocating through every chunk for the common small box.
					uint64_t raw_size = 0;
					const auto hint = JxlDecoderGetBoxSizeRaw(dec.get(), &raw_size) == JXL_DEC_SUCCESS
						                  ? std::min<uint64_t>(raw_size, max_metadata_bytes)
						                  : 0u;

					box_data.resize(std::max<size_t>(box_chunk, static_cast<size_t>(hint)));

					if (JxlDecoderSetBoxBuffer(dec.get(), box_data.data(), box_data.size()) != JXL_DEC_SUCCESS)
					{
						box_data.clear();
						current_kind = box_kind::none;
					}
				}
			}
		}
		else if (status == JXL_DEC_BOX_NEED_MORE_OUTPUT)
		{
			const auto remaining = JxlDecoderReleaseBoxBuffer(dec.get());
			const auto written = box_data.size() - remaining;

			if (box_data.size() >= max_metadata_bytes)
			{
				// Refuse to keep growing; drop the oversized box and stop scanning boxes.
				box_data.clear();
				current_kind = box_kind::none;
				break;
			}

			// Grow geometrically so a multi-megabyte box does not cost hundreds of copies.
			box_data.resize(std::min(max_metadata_bytes, box_data.size() * 2u));
			JxlDecoderSetBoxBuffer(dec.get(), box_data.data() + written, box_data.size() - written);
		}
		else
		{
			// A box is only complete once the decoder reached the end of the stream. On
			// JXL_DEC_ERROR or a truncated input the buffer holds a partial payload, so
			// publishing it would surface half-parsed metadata.
			if (status == JXL_DEC_SUCCESS) finalize_box();
			break;
		}
	}

	return result;
}

ui::surface_ptr load_jxl(read_stream& s, load_diagnostic* const diagnostic)
{
	ui::surface_ptr result;
	ui::surface_ptr pending_surface;

	try
	{
		df::blob owned;
		const auto data = s.view_all(owned);

		const df::releaser<JxlDecoder> dec(JxlDecoderCreate(nullptr), [](auto* d) { JxlDecoderDestroy(d); });
		if (!dec.get()) return result;

		if (JxlDecoderSubscribeEvents(dec.get(), JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE) != JXL_DEC_SUCCESS)
		{
			return result;
		}

		// Match scan_jxl (and every other format): keep the bitstream geometry and carry the
		// orientation on the surface, so scanned and loaded dimensions always agree.
		JxlDecoderSetKeepOrientation(dec.get(), JXL_TRUE);

		JxlDecoderSetInput(dec.get(), data.data, data.size);
		JxlDecoderCloseInput(dec.get());

		JxlBasicInfo info = {};
		bool have_info = false;

		for (;;)
		{
			const auto status = JxlDecoderProcessInput(dec.get());

			if (status == JXL_DEC_BASIC_INFO)
			{
				if (JxlDecoderGetBasicInfo(dec.get(), &info) != JXL_DEC_SUCCESS) break;
				if (info.xsize == 0 || info.ysize == 0) break;

				// Header dimensions are untrusted and the surface stride is computed with
				// int arithmetic, so reject anything that could overflow it.
				if (info.xsize > max_image_dimension || info.ysize > max_image_dimension) break;

				if (reject_over_budget_source(diagnostic,
				                              {static_cast<int>(info.xsize), static_cast<int>(info.ysize)}, "JXL"))
				{
					break;
				}

				have_info = true;
			}
			else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER)
			{
				if (!have_info) break;

				auto surface = std::make_shared<ui::surface>();

				surface->alloc(info.xsize, info.ysize,
				               info.alpha_bits > 0 ? ui::texture_format::ARGB : ui::texture_format::RGB,
				               to_orientation(info.orientation));

				// libjxl writes interleaved RGBA; the surface stores BGRA, so swap once decoded.
				JxlPixelFormat format = {
					4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, static_cast<uint32_t>(surface->stride())
				};

				if (JxlDecoderSetImageOutBuffer(dec.get(), &format, surface->pixels(), surface->size()) !=
					JXL_DEC_SUCCESS)
				{
					break;
				}

				pending_surface = std::move(surface);
			}
			else if (status == JXL_DEC_FULL_IMAGE)
			{
				// First frame is sufficient for a still image.
				if (pending_surface)
				{
					pending_surface->swap_rb();
					result = std::move(pending_surface);
				}
				break;
			}
			else
			{
				// JXL_DEC_SUCCESS, JXL_DEC_ERROR, JXL_DEC_NEED_MORE_INPUT or anything else: done.
				break;
			}
		}
	}
	catch (std::exception& e)
	{
		df::log(__FUNCTION__, e.what());
	}

	return result;
}
