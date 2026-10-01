// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: The still image formats Diffractor recognises by signature, one entry each: the file type
// it registers, how its first bytes identify it, how its header is scanned without decoding, how a
// file of it loads, and how in-memory bytes of it decode. Detection, scanning, loading and decoding
// all dispatch through this table.

#include "pch.h"
#include "files.h"

namespace
{
	// Read through memcpy: the span often points into the middle of another file - an embedded
	// thumbnail sits at an offset taken straight from an IFD entry - so its alignment is never ours
	// to assume. The signature constants below are little-endian host order throughout.
	uint32_t read_u32(const df::cspan data, const size_t offset)
	{
		uint32_t n;
		std::memcpy(&n, data.data + offset, sizeof(n));
		return n;
	}

	uint16_t read_u16(const df::cspan data)
	{
		uint16_t n;
		std::memcpy(&n, data.data, sizeof(n));
		return n;
	}

	bool has_brand(const df::cspan data, const std::span<const std::array<uint8_t, 4>> brands)
	{
		if (data.size < 12u) return false;

		constexpr std::array<uint8_t, 4> ftyp_header = {'f', 't', 'y', 'p'};

		if (!std::equal(std::begin(ftyp_header), std::end(ftyp_header), data.data + 4)) return false;

		return std::ranges::any_of(brands, [data](const auto& b)
		{
			return std::equal(std::begin(b), std::end(b), data.data + 8);
		});
	}

	constexpr std::array<std::array<uint8_t, 4>, 12> heif_brands = {
		{
			{'h', 'e', 'i', 'c'},
			{'h', 'e', 'i', 'x'},
			{'h', 'e', 'v', 'c'},
			{'h', 'e', 'v', 'x'},
			{'h', 'e', 'i', 'm'},
			{'h', 'e', 'i', 's'},
			{'h', 'e', 'v', 'm'},
			{'h', 'e', 'v', 's'},
			{'m', 'i', 'f', '1'},
			{'m', 's', 'f', '1'},
			{'a', 'v', 'i', 'f'},
			{'a', 'v', 'i', 's'},
		}
	};

	file_scan_result scanned_as(file_scan_result result, const detected_format format)
	{
		result.format = format;
		return result;
	}

	ui::surface_ptr fitted(files& ff, ui::surface_ptr loaded, const still_decode_request& request)
	{
		return is_valid(loaded) ? ff.fit_within(std::move(loaded), request.target_extent) : ui::surface_ptr{};
	}

	// These decoders report a malformed file by throwing. The failure is logged and answered with
	// nothing, so the caller still offers the bytes to ffmpeg.
	template <typename Decode>
	ui::surface_ptr decode_or_log(Decode&& decode)
	{
		try
		{
			return decode();
		}
		catch (const std::exception& e)
		{
			df::log("files::image_to_surface", e.what());
			return {};
		}
	}

	constexpr auto xmp_edit = file_traits::embedded_xmp | file_traits::edit;

	// In detection order. The signatures do not overlap, so the order only decides which test runs
	// first.
	constexpr still_format s_still_formats[] = {
		{
			.format = detected_format::PSD,
			.extensions = "psd",
			.description = "Adobe Photoshop Drawing",
			.traits = xmp_edit,
			.matches = [](const df::cspan h) { return h.size >= 4 && read_u32(h, 0) == 0x53504238; },
			.scan = [](read_stream& s, scan_intent, bool, files*)
			{
				return scanned_as(scan_psd(s), detected_format::PSD);
			},
			.load = [](read_stream& s, load_diagnostic* const d) { return load_psd(s, d); },
			.decode = [](files& ff, const df::cspan data, const still_decode_request& r)
			{
				mem_read_stream stream(data);
				return fitted(ff, load_psd(stream), r);
			},
		},
		{
			// 47 49 46 38
			.format = detected_format::GIF,
			.extensions = "gif,giff",
			.description = "CompuServe's Graphics Interchange Format",
			.traits = xmp_edit,
			.matches = [](const df::cspan h) { return h.size >= 4 && read_u32(h, 0) == 0x38464947; },
			.scan = [](read_stream& s, scan_intent, bool, files*)
			{
				auto result = scanned_as(scan_gif(s), detected_format::GIF);
				result.pixel_format = "pal8"_c;
				return result;
			},
		},
		{
			// A container (00 00 00 0C 'J' 'X' 'L' ' ') or a bare codestream (FF 0A).
			.format = detected_format::JXL,
			.extensions = "jxl",
			.description = "JPEG XL",
			.traits = file_traits::embedded_xmp,
			.matches = [](const df::cspan h)
			{
				if (h.size < 4) return false;
				if (read_u32(h, 0) == 0x0C000000 && h.size >= 8 && read_u32(h, 4) == 0x204C584A) return true;
				return read_u16(h) == 0x0AFF;
			},
			.scan = [](read_stream& s, scan_intent, bool, files*)
			{
				return scanned_as(scan_jxl(s), detected_format::JXL);
			},
			.load = [](read_stream& s, load_diagnostic* const d) { return load_jxl(s, d); },
			.decode = [](files& ff, const df::cspan data, const still_decode_request& r)
			{
				return decode_or_log([&ff, data, &r]
				{
					mem_read_stream stream(data);
					return fitted(ff, load_jxl(stream), r);
				});
			},
		},
		{
			// RIFF containers ('RIFF' .... 'WEBP'): the WEBP FourCC at offset 8 keeps other RIFF
			// payloads (WAV, AVI) from being taken for WebP.
			.format = detected_format::WEBP,
			.extensions = "webp",
			.traits = xmp_edit,
			.encoded = ui::image_format::WEBP,
			.matches = [](const df::cspan h)
			{
				return h.size >= 12 && read_u32(h, 0) == 0x46464952 && read_u32(h, 8) == 0x50424557;
			},
			.scan = [](read_stream& s, const scan_intent intent, bool, files*)
			{
				file_scan_result result;
				scan_webp(result, s, intent);
				return scanned_as(std::move(result), detected_format::WEBP);
			},
			.decode = [](files& ff, const df::cspan data, const still_decode_request& r)
			{
				return decode_or_log([&ff, data, &r]
				{
					return fitted(ff, load_webp(data, r.can_use_yuv, r.target_extent), r);
				});
			},
		},
		{
			.format = detected_format::JPEG,
			.extensions = "jpeg,jpg,jpe,jfif",
			.description = "Joint Photographic Experts Group",
			.traits = xmp_edit,
			.encoded = ui::image_format::JPEG,
			.matches = [](const df::cspan h) { return h.size >= 4 && read_u16(h) == 0xD8FF; },
			.scan = [](read_stream& s, const scan_intent intent, const bool want_thumbnail, files*)
			{
				return scanned_as(scan_jpg(s, intent, want_thumbnail), detected_format::JPEG);
			},
			.decode = decode_still_jpeg,
		},
		{
			.format = detected_format::BMP,
			.extensions = "bmp",
			.description = "Microsoft Windows Bitmap",
			.matches = [](const df::cspan h) { return h.size >= 4 && read_u16(h) == 0x4D42; },
			.scan = [](read_stream& s, scan_intent, bool, files*) { return scan_bmp(s); },
		},
		{
			.format = detected_format::PNG,
			.extensions = "png",
			.description = "Portable Network Graphic",
			.traits = xmp_edit,
			.encoded = ui::image_format::PNG,
			.matches = [](const df::cspan h) { return h.size >= 4 && read_u16(h) == 0x5089; },
			.scan = [](read_stream& s, scan_intent, bool, files*)
			{
				return scanned_as(scan_png(s), detected_format::PNG);
			},
			.decode = [](files& ff, const df::cspan data, const still_decode_request& r)
			{
				return decode_or_log([&ff, data, &r] { return fitted(ff, load_png(data), r); });
			},
		},
		{
			.format = detected_format::TIFF,
			.extensions = "tiff, tif",
			.description = "Tagged Image File Format",
			.traits = xmp_edit,
			.matches = [](const df::cspan h)
			{
				if (h.size < 4) return false;

				// 'II' little-endian or 'MM' big-endian. The byte-order mark alone matches any file
				// starting with those two letters, so the version word must be 42 for classic TIFF
				// or 43 for BigTIFF too.
				const auto order = read_u16(h);
				if (order != 0x4949 && order != 0x4d4d) return false;

				const auto* const data = h.data;
				const auto version = order == 0x4949
					                     ? static_cast<uint16_t>(data[2] | (data[3] << 8))
					                     : static_cast<uint16_t>((data[2] << 8) | data[3]);
				return version == 42u || version == 43u;
			},
			.scan = [](read_stream& s, scan_intent, const bool want_thumbnail, files* const decoder)
			{
				return scanned_as(scan_tiff(s, want_thumbnail, decoder), detected_format::TIFF);
			},
		},
		{
			// HEIF or AVIF: an ISO base media 'ftyp' box naming one of their brands.
			.format = detected_format::HEIF,
			.extensions = "heif, heifs, heic, heics, avci, avcs, avif, avifs",
			.description = "High Efficiency Image File Format",
			.traits = file_traits::embedded_xmp,
			.matches = [](const df::cspan h) { return has_brand(h, heif_brands); },
			.scan = [](read_stream& s, const scan_intent intent, const bool want_thumbnail, files*)
			{
				return scanned_as(scan_heif(s, intent, want_thumbnail), detected_format::HEIF);
			},
			.load = [](read_stream& s, load_diagnostic* const d) { return load_heif(s, d); },
			.decode = [](files& ff, const df::cspan data, const still_decode_request& r)
			{
				return decode_or_log([&ff, data, &r]
				{
					mem_read_stream stream(data);
					return fitted(ff, load_heif(stream), r);
				});
			},
		},
	};
}

// A JPEG decodes at a reduced scale already. Planar YUV for display goes to the sampler as it is,
// which resizes at draw time; a thumbnail is about to be encoded, so it is reduced here - and stays
// planar, because that is the form VP8 wants.
ui::surface_ptr decode_still_jpeg(files& ff, const df::cspan data, const still_decode_request& request)
{
	bool is_yuv = false;
	auto decoded = ff.decode_jpeg(data, request.target_extent, request.can_use_yuv, {}, is_yuv, {}, request.intent);

	if (!is_valid(decoded)) return {};

	return is_yuv && request.intent != decode_intent::thumbnail
		       ? std::move(decoded)
		       : ff.fit_within(std::move(decoded), request.target_extent);
}

std::span<const still_format> still_formats()
{
	return s_still_formats;
}

const still_format* find_still_format(const detected_format format)
{
	for (const auto& f : s_still_formats)
	{
		if (f.format == format) return &f;
	}

	return nullptr;
}

// Every still format is a photo.
file_type still_file_type(const detected_format format)
{
	const auto* const f = find_still_format(format);
	df::assert_true(f != nullptr);
	return f ? file_type(file_group::photo, f->extensions, f->description, f->traits) : file_type::other;
}
