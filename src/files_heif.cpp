// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: HEIF/HEIC image format support. Decodes High Efficiency Image Format files
// using libheif, extracts metadata, reports container brands and items, and handles thumbnails.

#include "pch.h"
#include "files.h"
#include "files_jpeg.h"
#include "metadata_exif.h"

#define LIBHEIF_STATIC_BUILD 1
#define LIBDE265_STATIC_BUILD 1

#include <libheif/heif.h>
#include <libheif/heif_properties.h>


static ui::surface_ptr image_to_surface(const heif_image_handle* handle, const heif_image* img)
{
	// Query the plane we are about to read, not the primary image: the two are allowed to
	// differ, and trusting the primary extent would read past the end of libheif's buffer.
	const auto width = heif_image_get_width(img, heif_channel_interleaved);
	const auto height = heif_image_get_height(img, heif_channel_interleaved);

	if (width <= 0 || height <= 0 || width > max_image_dimension || height > max_image_dimension)
	{
		return {};
	}

	int heif_stride = 0;
	const auto* heif_image_data = heif_image_get_plane_readonly(img, heif_channel_interleaved, &heif_stride);
	const auto row_bytes = static_cast<size_t>(width) * 4u;

	if (heif_image_data == nullptr || heif_stride <= 0 || static_cast<size_t>(heif_stride) < row_bytes)
	{
		return {};
	}

	const auto has_alpha = heif_image_handle_has_alpha_channel(handle);
	auto temp_surface = std::make_shared<ui::surface>();
	if (!temp_surface->alloc(width, height, has_alpha ? ui::texture_format::ARGB : ui::texture_format::RGB))
	{
		return {};
	}

	const auto dest_stride = temp_surface->stride();

	for (auto y = 0; y < height; y++)
	{
		const auto dst = temp_surface->pixels_line(y);
		const auto src = heif_image_data + static_cast<size_t>(heif_stride) * y;
		std::memcpy(dst, src, row_bytes);

		// alloc does not zero, and swap_rb walks the whole stride, so clear the padding
		// rather than shuffling uninitialised bytes into the texture upload.
		std::memset(dst + row_bytes, 0, dest_stride - row_bytes);
	}

	temp_surface->swap_rb();

	// Only publish the surface once it is fully allocated and written.
	return temp_surface;
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
	if (s->pos > s->stream->size() || size > s->stream->size() - s->pos) return heif_error_Usage_error;
	try
	{
		s->stream->read(s->pos, static_cast<uint8_t*>(data), size);
		s->pos += size;
		return heif_error_Ok;
	}
	catch (...)
	{
		return heif_error_Usage_error;
	}
}

static int seek(const int64_t position, void* userdata)
{
	const auto s = static_cast<heif_read_stream*>(userdata);
	if (position < 0 || static_cast<uint64_t>(position) > s->stream->size()) return heif_error_Usage_error;
	s->pos = static_cast<uint64_t>(position);
	return heif_error_Ok;
}

static heif_reader_grow_status wait_for_file_size(const int64_t target_size, void* userdata)
{
	const auto s = static_cast<heif_read_stream*>(userdata);
	if (target_size < 0 || static_cast<uint64_t>(target_size) > s->stream->size())
		return heif_reader_grow_status_size_beyond_eof;
	return heif_reader_grow_status_size_reached;
}

static df::blob extract_exif(const heif_image_handle* handle)
{
	// libheif can filter the block list by type, so the full-image load path can fetch the
	// Exif block without also materialising the XMP, IPTC and (often large) ICC blobs.
	heif_item_id id = 0;

	if (heif_image_handle_get_list_of_metadata_block_IDs(handle, "Exif", &id, 1) != 1)
	{
		return {};
	}

	df::blob raw_metadata(heif_image_handle_get_metadata_size(handle, id), 0);

	if (heif_image_handle_get_metadata(handle, id, raw_metadata.data()).code != heif_error_Ok)
	{
		return {};
	}

	return strip_exif_tiff_prefix(std::move(raw_metadata));
}

static metadata_parts extract_metadata(const heif_image_handle* handle)
{
	metadata_parts result = {};
	result.exif = extract_exif(handle);

	const int metadata_block_count = heif_image_handle_get_number_of_metadata_blocks(handle, nullptr);

	if (metadata_block_count > 0)
	{
		std::vector<heif_item_id> ids(metadata_block_count);
		heif_image_handle_get_list_of_metadata_block_IDs(handle, nullptr, ids.data(), metadata_block_count);

		for (const auto& id : ids)
		{
			const auto metadata_type = heif_image_handle_get_metadata_type(handle, id);

			if (metadata_type == nullptr)
			{
				continue;
			}

			if (str::icmp(metadata_type, "XMP") == 0)
			{
				const size_t metadataSize = heif_image_handle_get_metadata_size(handle, id);
				df::blob raw_metatdata(metadataSize, 0);

				const auto error = heif_image_handle_get_metadata(handle, id, raw_metatdata.data());
				if (error.code == heif_error_Ok)
				{
					result.xmp = std::move(raw_metatdata);
				}
			}
			else if (str::icmp(metadata_type, "iptc") == 0)
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

	// The ICC / color profile is stored separately from the metadata blocks.
	const auto profile_type = heif_image_handle_get_color_profile_type(handle);

	if (profile_type == heif_color_profile_type_prof || profile_type == heif_color_profile_type_rICC)
	{
		const auto icc_size = heif_image_handle_get_raw_color_profile_size(handle);

		if (icc_size > 0)
		{
			df::blob icc(icc_size, 0);
			const auto icc_error = heif_image_handle_get_raw_color_profile(handle, icc.data());

			if (icc_error.code == heif_error_Ok)
			{
				result.icc = std::move(icc);
			}
		}
	}

	return result;
}

static str::cached extract_pixel_format(const heif_image_handle* image_handle)
{
	str::cached result = {};

	heif_colorspace out_colorspace;
	heif_chroma out_chroma;
	const auto colorspace_result = heif_image_handle_get_preferred_decoding_colorspace(
		image_handle, &out_colorspace, &out_chroma);

	if (colorspace_result.code == heif_error_Ok)
	{
		switch (out_chroma)
		{
		case heif_chroma_monochrome: result = "grayscale"_c;
			break;
		case heif_chroma_420: result = "yuv420"_c;
			break;
		case heif_chroma_422: result = "yuv422"_c;
			break;
		case heif_chroma_444: result = "yuv444"_c;
			break;
		case heif_chroma_undefined:
			result = "rgb"_c;
			break;
		case heif_chroma_interleaved_RGBA:
			result = "rgba"_c;
			break;
		case heif_chroma_interleaved_RRGGBB_BE:
			result = "rgb48"_c;
			break;
		case heif_chroma_interleaved_RRGGBBAA_BE:
			result = "rgba64"_c;
			break;
		case heif_chroma_interleaved_RRGGBB_LE:
			result = "rgb48"_c;
			break;
		case heif_chroma_interleaved_RRGGBBAA_LE:
			result = "rgba64"_c;
			break;
		}
	}

	return result;
}

static bool has_orientation_transform(const heif_context* context, const heif_image_handle* image_handle)
{
	const auto item_id = heif_image_handle_get_item_id(image_handle);
	return heif_item_get_properties_of_type(context, item_id, heif_item_property_type_transform_rotation, nullptr, 0) > 0 ||
		heif_item_get_properties_of_type(context, item_id, heif_item_property_type_transform_mirror, nullptr, 0) > 0;
}

// The 'ftyp' box that opens every ISO base media file names the brand the writer claimed and the
// brands it says the file is also readable as, which is what tells HEIC apart from AVIF.
static void scan_heif_brands(metadata_kv_list& kv, read_stream& s)
{
	if (s.size() < 16u) return;

	uint8_t header[16];
	s.read(0, header, sizeof(header));

	if (memcmp(header + 4, "ftyp", 4) != 0) return;

	const auto box_size = df::byteswap32(header + 0);
	if (box_size < 16u || box_size > s.size() || box_size > 4096u) return;

	const std::string major(header + 8, header + 12);
	add_structure_row(kv, "Major brand", major);
	add_structure_row(kv, "Minor version", str::to_string(df::byteswap32(header + 12)));

	std::vector<uint8_t> box(box_size);
	s.read(0, box.data(), box_size);

	std::string compatible;

	for (uint32_t offset = 16; offset + 4u <= box_size; offset += 4u)
	{
		if (!compatible.empty()) compatible += ' ';
		compatible.append(box.begin() + offset, box.begin() + offset + 4);
	}

	if (!compatible.empty())
	{
		add_structure_row(kv, "Compatible brands", compatible);
	}
}

static void scan_heif_structure(file_scan_result& result, read_stream& s, heif_context* ctx,
                                const heif_image_handle* handle)
{
	metadata_kv_list kv;

	add_structure_section(kv, "Summary", "heif.summary", true);
	add_structure_row(kv, "File size", std::format("{} bytes", s.size()));
	scan_heif_brands(kv, s);

	add_structure_row(kv, "Top level images", str::to_string(heif_context_get_number_of_top_level_images(ctx)));
	add_structure_row(kv, "Primary item id", str::to_string(heif_image_handle_get_item_id(handle)));
	add_structure_row(kv, "Stored size", std::format("{} x {}", heif_image_handle_get_ispe_width(handle),
	                                                 heif_image_handle_get_ispe_height(handle)));
	add_structure_row(kv, "Displayed size", std::format("{} x {}", heif_image_handle_get_width(handle),
	                                                    heif_image_handle_get_height(handle)));

	const auto luma_bits = heif_image_handle_get_luma_bits_per_pixel(handle);
	const auto chroma_bits = heif_image_handle_get_chroma_bits_per_pixel(handle);

	if (luma_bits > 0) add_structure_row(kv, "Luma depth", std::format("{} bits", luma_bits));
	if (chroma_bits > 0) add_structure_row(kv, "Chroma depth", std::format("{} bits", chroma_bits));

	const auto has_alpha = heif_image_handle_has_alpha_channel(handle) != 0;
	add_structure_row(kv, "Alpha channel", has_alpha
		                                       ? heif_image_handle_is_premultiplied_alpha(handle)
			                                         ? "present, premultiplied"
			                                         : "present"
		                                       : "none");

	uint32_t aspect_h = 1;
	uint32_t aspect_v = 1;

	if (heif_image_handle_get_pixel_aspect_ratio(handle, &aspect_h, &aspect_v) && aspect_h != aspect_v)
	{
		add_structure_row(kv, "Pixel aspect ratio", std::format("{}:{}", aspect_h, aspect_v));
	}

	switch (heif_image_handle_get_color_profile_type(handle))
	{
	case heif_color_profile_type_not_present:
		add_structure_row(kv, "Colour profile", "none");
		break;
	case heif_color_profile_type_nclx:
		add_structure_row(kv, "Colour profile", "nclx (coded primaries and transfer)");
		break;
	default:
		add_structure_row(kv, "Colour profile", "ICC");
		break;
	}

	add_structure_row(kv, "Thumbnails", str::to_string(heif_image_handle_get_number_of_thumbnails(handle)));

	if (const auto depth_images = heif_image_handle_get_number_of_depth_images(handle); depth_images > 0)
	{
		add_structure_row(kv, "Depth images", str::to_string(depth_images));
	}

	if (const auto aux_images = heif_image_handle_get_number_of_auxiliary_images(handle, 0); aux_images > 0)
	{
		add_structure_row(kv, "Auxiliary images", str::to_string(aux_images));
	}

	add_structure_section(kv, "Metadata items", "heif.metadata");

	const auto metadata_count = heif_image_handle_get_number_of_metadata_blocks(handle, nullptr);

	if (metadata_count > 0)
	{
		std::vector<heif_item_id> ids(metadata_count);
		const auto id_count = heif_image_handle_get_list_of_metadata_block_IDs(handle, nullptr, ids.data(),
		                                                                      metadata_count);

		for (auto i = 0; i < id_count; ++i)
		{
			const auto type = str::safe_string(heif_image_handle_get_metadata_type(handle, ids[i]));
			const auto content = str::safe_string(heif_image_handle_get_metadata_content_type(handle, ids[i]));
			const auto size = heif_image_handle_get_metadata_size(handle, ids[i]);

			auto value = std::format("{} bytes, item {}", size, ids[i]);
			if (!content.empty()) value = std::format("{}, {}", content, value);

			auto& row = kv.emplace_back(type.empty() ? "unnamed"s : type,
			                            std::move(value));
			row.depth = 1;
			row.id = std::format("heif.metadata.{}", ids[i]);
		}
	}

	finish_structure_sections(kv);
	result.structure_metadata = std::move(kv);
}

// Owns a parsed libheif context together with the reader and stream structures libheif keeps
// pointers into, so all three share one lifetime.
namespace
{
	class heif_source
	{
		heif_reader _reader = {};
		heif_read_stream _stream = {};
		df::releaser<heif_context> _ctx;
		bool _open = false;

	public:
		explicit heif_source(read_stream& s) : _ctx(heif_context_alloc(), [](auto* c) { heif_context_free(c); })
		{
			if (!_ctx.get()) return;

			_reader.reader_api_version = 1;
			_reader.get_position = get_position;
			_reader.read = read;
			_reader.seek = seek;
			_reader.wait_for_file_size = wait_for_file_size;
			_stream.stream = &s;

			_open = heif_context_read_from_reader(_ctx.get(), &_reader, &_stream, nullptr).code == heif_error_Ok;
		}

		bool is_open() const { return _open; }
		heif_context* get() const { return _ctx.get(); }
	};
}

file_scan_result scan_heif(read_stream& s, const scan_intent intent)
{
	file_scan_result result = {};

	const heif_source src(s);

	if (src.is_open())
	{
		heif_image_handle* image_handle = nullptr;
		const auto image_result = heif_context_get_primary_image_handle(src.get(), &image_handle);
		const df::releaser<heif_image_handle> image_handle_releaser(image_handle, [](auto* c)
		{
			heif_image_handle_release(c);
		});

		if (image_result.code == heif_error_Ok)
		{
			result.width = heif_image_handle_get_width(image_handle);
			result.height = heif_image_handle_get_height(image_handle);
			result.pixel_format = extract_pixel_format(image_handle);
			result.orientation_applied = has_orientation_transform(src.get(), image_handle);

			heif_item_id thumbnail_id = 0;
			const auto thumbnail_count = heif_image_handle_get_list_of_thumbnail_IDs(image_handle, &thumbnail_id, 1);

			if (thumbnail_count > 0)
			{
				heif_image_handle* thumbnail_handle = nullptr;
				const auto thumbnail_result = heif_image_handle_get_thumbnail(
					image_handle, thumbnail_id, &thumbnail_handle);
				const df::releaser<heif_image_handle> thumbnail_handle_releaser(
					thumbnail_handle, [](auto* c) { heif_image_handle_release(c); });

				if (thumbnail_result.code == heif_error_Ok)
				{
					heif_image* img = nullptr;
					const auto decode_image_result = heif_decode_image(thumbnail_handle, &img, heif_colorspace_RGB,
					                                                   heif_chroma_interleaved_RGBA, nullptr);
					const df::releaser<heif_image> heif_image_releaser(img, [](auto* i) { heif_image_release(i); });

					if (decode_image_result.code == heif_error_Ok)
					{
						result.thumbnail_surface = image_to_surface(thumbnail_handle, img);
					}
				}
			}

			result.metadata = extract_metadata(image_handle);
			result.success = result.width != 0 && result.height != 0;

			if (intent == scan_intent::inspect)
			{
				scan_heif_structure(result, s, src.get(), image_handle);
			}
		}
	}

	return result;
}

ui::surface_ptr load_heif(read_stream& s, load_diagnostic* const diagnostic)
{
	ui::surface_ptr result;

	const heif_source src(s);

	if (src.is_open())
	{
		// get a handle to the primary image
		heif_image_handle* image_handle = nullptr;
		const auto image_handle_result = heif_context_get_primary_image_handle(src.get(), &image_handle);
		const df::releaser<heif_image_handle> image_handle_releaser(image_handle, [](auto* c)
		{
			heif_image_handle_release(c);
		});

		if (image_handle_result.code == heif_error_Ok)
		{
			if (reject_over_budget_source(diagnostic,
			                              {heif_image_handle_get_width(image_handle),
			                               heif_image_handle_get_height(image_handle)}, "HEIF"))
			{
				return result;
			}

			// decode the image and convert colorspace to RGB, saved as 24bit interleaved
			heif_image* img = nullptr;
			const auto decode_image_result = heif_decode_image(image_handle, &img, heif_colorspace_RGB,
			                                                   heif_chroma_interleaved_RGBA, nullptr);

			const df::releaser<heif_image> heif_image_releaser(img, [](auto* i) { heif_image_release(i); });

			if (decode_image_result.code == heif_error_Ok)
			{
				result = image_to_surface(image_handle, img);
			}

			if (result)
			{
				prop::item_metadata md;
				metadata_exif::parse(md, extract_exif(image_handle));
				result->orientation(md.orientation);
			}
		}
	}

	return result;
}
