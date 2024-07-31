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
#include "metadata_exif.h"

#define LIBHEIF_STATIC_BUILD 1
#define LIBDE265_STATIC_BUILD 1

#include <libheif/heif.h>


static ui::surface_ptr image_to_surface(heif_image_handle* handle, heif_image* img)
{
	const auto width = heif_image_get_primary_width(img);
	const auto height = heif_image_get_primary_height(img);
	const auto has_alpha = heif_image_handle_has_alpha_channel(handle);

	int heif_stride = 0;
	const auto* heif_image_data = heif_image_get_plane_readonly(img, heif_channel_interleaved, &heif_stride);

	auto result = std::make_shared<ui::surface>();

	if (result->alloc(width, height, has_alpha ? ui::texture_format::ARGB : ui::texture_format::RGB))
	{
		const auto dest_stride = result->stride();
		const auto copy_stride = std::min(static_cast<size_t>(heif_stride), dest_stride);

		for (auto y = 0; y < height; y++)
		{
			const auto dst = result->pixels_line(y);
			const auto src = heif_image_data + (static_cast<size_t>(heif_stride) * y);
			std::memcpy(dst, src, copy_stride);
		}

		result->swap_rb();
	}

	return result;
}


struct heif_read_stream
{
	uint64_t pos = 0;
	read_stream* stream = nullptr;
};

static int64_t get_position(void* userdata)
{
	const auto s = static_cast<heif_read_stream*>(userdata);
	return s->pos;
}

static int read(void* data, const size_t size, void* userdata)
{
	const auto s = static_cast<heif_read_stream*>(userdata);
	if ((s->pos + size) > s->stream->size()) return heif_error_Usage_error;
	s->stream->read(s->pos, static_cast<uint8_t*>(data), size);
	s->pos += size;
	return heif_error_Ok;
}

static int seek(int64_t position, void* userdata)
{
	const auto s = static_cast<heif_read_stream*>(userdata);
	if (position > static_cast<int64_t>(s->stream->size())) return heif_error_Usage_error;
	s->pos = position;
	return heif_error_Ok;
}

static heif_reader_grow_status wait_for_file_size(int64_t target_size, void* userdata)
{
	const auto s = static_cast<heif_read_stream*>(userdata);
	if (target_size > static_cast<int64_t>(s->stream->size())) return heif_reader_grow_status_size_beyond_eof;
	return heif_reader_grow_status_size_reached;
}

static metadata_parts extract_metadata(heif_image_handle* handle)
{
	metadata_parts result = {};
	const int metadata_block_count = heif_image_handle_get_number_of_metadata_blocks(handle, nullptr);

	if (metadata_block_count > 0)
	{
		std::vector<heif_item_id> ids(metadata_block_count);
		heif_image_handle_get_list_of_metadata_block_IDs(handle, nullptr, ids.data(), metadata_block_count);

		for (const auto& id : ids)
		{
			const auto metadata_type = heif_image_handle_get_metadata_type(handle, id);

			if (str::icmp(metadata_type, "Exif"sv) == 0)
			{
				const auto metadataSize = heif_image_handle_get_metadata_size(handle, id);
				df::blob raw_metatdata(metadataSize, 0);
				const auto result_info = heif_image_handle_get_metadata(handle, id, raw_metatdata.data());

				if (result_info.code == heif_error_Ok)
				{
					int offset = 0;

					if (raw_metatdata.size() > 4 && raw_metatdata[0] == 0)
					{
						raw_metatdata.erase(raw_metatdata.begin(), raw_metatdata.begin() + 4);
					}

					if (is_exif_signature(raw_metatdata))
					{
						raw_metatdata.erase(raw_metatdata.begin(), raw_metatdata.begin() + exif_signature.size());
					}

					result.exif = std::move(raw_metatdata);
				}
			}
			else if (str::icmp(metadata_type, "XMP"sv) == 0)
			{
				const size_t metadataSize = heif_image_handle_get_metadata_size(handle, id);
				df::blob raw_metatdata(metadataSize, 0);

				const auto error = heif_image_handle_get_metadata(handle, id, raw_metatdata.data());
				if (error.code == heif_error_Ok)
				{
					result.xmp = std::move(raw_metatdata);
				}
			}
			else if (str::icmp(metadata_type, "iptc"sv) == 0)
			{
				const size_t metadataSize = heif_image_handle_get_metadata_size(handle, id);
				df::blob raw_metatdata(metadataSize, 0);

				const auto error = heif_image_handle_get_metadata(handle, id, raw_metatdata.data());
				if (error.code == heif_error_Ok)
				{
					result.iptc = std::move(raw_metatdata);
				}
			}
		}
	}

	return result;
}

static str::cached extract_pixel_format(heif_image_handle* image_handle)
{
	str::cached result = {};

	heif_colorspace out_colorspace;
	heif_chroma out_chroma;
	const auto colorspace_result = heif_image_handle_get_preferred_decoding_colorspace(image_handle, &out_colorspace, &out_chroma);

	if (colorspace_result.code == heif_error_Ok)
	{
		switch (out_chroma) {
		case heif_chroma_monochrome: result = u8"grayscale"_c; break;
		case heif_chroma_420: result = u8"yuv420"_c; break;
		case heif_chroma_422: result = u8"yuv422"_c; break;
		case heif_chroma_444: result = u8"yuv444"_c; break;
		case heif_chroma_undefined:
			break;
		case heif_chroma_interleaved_RGB:
			result = u8"rgb"_c; break;
			break;
		case heif_chroma_interleaved_RGBA:
			result = u8"rgba"_c; break;
			break;
		case heif_chroma_interleaved_RRGGBB_BE:
			result = u8"rgb48"_c; break;
			break;
		case heif_chroma_interleaved_RRGGBBAA_BE:
			result = u8"rgba64"_c; break;
			break;
		case heif_chroma_interleaved_RRGGBB_LE:
			result = u8"rgb48"_c; break;
			break;
		case heif_chroma_interleaved_RRGGBBAA_LE:
			result = u8"rgba64"_c; break;
			break;
		}
	}

	return result;
}

file_scan_result scan_heif(read_stream& s)
{
	file_scan_result result = {};

	const df::releaser<heif_context> ctx(heif_context_alloc(),
		[](auto* c) { heif_context_free(c); });

	heif_reader reader = {};
	reader.get_position = get_position;
	reader.read = read;
	reader.seek = seek;
	reader.wait_for_file_size = wait_for_file_size;

	heif_read_stream stream = {};
	stream.stream = &s;

	const auto read_result = heif_context_read_from_reader(ctx.get(), &reader, &stream, nullptr);

	if (read_result.code == heif_error_Ok)
	{
		heif_image_handle* image_handle = nullptr;
		const auto image_result = heif_context_get_primary_image_handle(ctx.get(), &image_handle);
		const df::releaser<heif_image_handle> image_handle_releaser(image_handle, [](auto* c) { heif_image_handle_release(c); });

		if (image_result.code == heif_error_Ok)
		{
			result.width = heif_image_handle_get_width(image_handle);
			result.height = heif_image_handle_get_height(image_handle);
			result.pixel_format = extract_pixel_format(image_handle);

			heif_item_id thumbnail_id = 0;
			const auto thumbnail_count = heif_image_handle_get_list_of_thumbnail_IDs(image_handle, &thumbnail_id, 1);

			if (thumbnail_count > 0)
			{
				heif_image_handle* thumbnail_handle = nullptr;
				const auto thumbnail_result = heif_image_handle_get_thumbnail(image_handle, thumbnail_id, &thumbnail_handle);
				const df::releaser<heif_image_handle> thumbnail_handle_releaser(thumbnail_handle, [](auto* c) { heif_image_handle_release(c); });

				if (thumbnail_result.code != heif_error_Ok)
				{
					heif_image* img = nullptr;
					const auto decode_image_result = heif_decode_image(image_handle, &img, heif_colorspace_RGB,
						heif_chroma_interleaved_RGBA, nullptr);
					const df::releaser<heif_image> heif_image_releaser(img, [](auto* i) { heif_image_release(i); });

					if (decode_image_result.code == heif_error_Ok)
					{
						result.thumbnail_surface = image_to_surface(thumbnail_handle, img);
					}
				}
			}

			result.metadata = std::move(extract_metadata(image_handle));
			result.success = true;

			if (result.thumbnail_surface)
			{
				prop::item_metadata md;
				metadata_exif::parse(md, result.metadata.exif);
				result.thumbnail_surface->orientation(md.orientation);
			}
		}
	}

	return result;
}

ui::surface_ptr load_heif(read_stream& s)
{
	ui::surface_ptr result;

	const std::shared_ptr<heif_context> ctx(heif_context_alloc(),
		[](heif_context* c) { heif_context_free(c); });

	heif_reader reader = {};
	reader.get_position = get_position;
	reader.read = read;
	reader.seek = seek;
	reader.wait_for_file_size = wait_for_file_size;

	heif_read_stream stream = {};
	stream.stream = &s;

	const auto read_result = heif_context_read_from_reader(ctx.get(), &reader, &stream, nullptr);

	if (read_result.code == heif_error_Ok)
	{
		// get a handle to the primary image
		heif_image_handle* image_handle = nullptr;
		const auto image_handle_result = heif_context_get_primary_image_handle(ctx.get(), &image_handle);
		const df::releaser<heif_image_handle> image_handle_releaser(image_handle, [](auto* c) { heif_image_handle_release(c); });

		if (image_handle_result.code == heif_error_Ok)
		{
			// decode the image and convert colorspace to RGB, saved as 24bit interleaved
			heif_image* img = nullptr;
			const auto decode_image_result = heif_decode_image(image_handle, &img, heif_colorspace_RGB,
				heif_chroma_interleaved_RGBA, nullptr);

			const df::releaser<heif_image> heif_image_releaser(img, [](auto* i) { heif_image_release(i); });

			if (decode_image_result.code == heif_error_Ok)
			{
				result = image_to_surface(image_handle, img);
			}

			const auto metadata = extract_metadata(image_handle);

			if (result)
			{
				prop::item_metadata md;
				metadata_exif::parse(md, metadata.exif);
				result->orientation(md.orientation);
			}
		}
	}

	return result;
}
