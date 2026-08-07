// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Image file scanning and metadata extraction. Detects image formats,
// parses headers for dimensions and orientation, extracts embedded thumbnails.

#include "pch.h"
#include "metadata_exif.h"
#include "files.h"
#include "files_structs.h"


static constexpr auto GIF_Header_LEN = 6;
static constexpr auto GIF_87_Header_DATA = "GIF87a";
static constexpr auto GIF_89_Header_DATA = "GIF89a";

static constexpr auto APP_ID_LEN = 11;
static constexpr auto XMP_APP_ID_DATA = R"(XMP DataXMP)";

// A GIF XMP Data extension stores the packet raw, followed by a 258-byte magic trailer
// that begins 0x01 0xFF. Bound the search for that trailer so a packet without one
// cannot walk the entire file one byte at a time.
static constexpr uint64_t max_gif_xmp_bytes = 8ull * 1024ull * 1024ull;

// Offsets and lengths taken from IFD entries are unvalidated 32-bit file fields. Cap what they may
// ask the scanner to read or allocate, so a crafted file cannot demand a file-sized buffer.
static constexpr uint32_t max_embedded_thumbnail_bytes = 8u * 1024u * 1024u;
static constexpr uint32_t max_tiff_xmp_bytes = 8u * 1024u * 1024u;

enum gif_block_type
{
	kXMP_block_ImageDesc = 0x2C,
	kXMP_block_Extension = 0x21,
	kXMP_block_Trailer = 0x3B,
	kXMP_block_Header = 0x47
};

// The only two TIFF/EXIF byte-order marks. Both are byte pairs that read the same either way, so
// the mark itself can always be read as little-endian.
static constexpr uint16_t tiff_order_intel = 0x4949;
static constexpr uint16_t tiff_order_motorola = 0x4D4D;

// Anything else is not a TIFF header. Without this check get_uint16/get_uint32 silently treat a
// garbage mark as big-endian and every offset read from the file is fabricated.
static bool is_valid_tiff_order(const uint16_t order)
{
	return order == tiff_order_intel || order == tiff_order_motorola;
}

static uint16_t get_uint16(const uint16_t n, const uint16_t order)
{
	return order == tiff_order_intel ? n : df::byteswap16(n);
}

static uint16_t get_uint16(const uint8_t* p, const uint16_t order)
{
	uint16_t n;
	std::memcpy(&n, p, sizeof(n));
	return order == tiff_order_intel ? n : df::byteswap16(n);
}

static uint32_t get_uint32(const uint32_t n, const uint16_t order)
{
	return order == tiff_order_intel ? n : df::byteswap32(n);
}

static uint32_t get_uint32(const uint8_t* p, const uint16_t order)
{
	uint32_t n;
	std::memcpy(&n, p, sizeof(n));
	return order == tiff_order_intel ? n : df::byteswap32(n);
}

// Reached only for an embedded thumbnail whose format load_image_file cannot wrap, which needs a
// files instance to decode. Borrows the caller's rather than building a second libjpeg context.
static ui::surface_ptr decode_thumbnail(files* const decoder, const df::cspan data)
{
	if (decoder) return decoder->image_to_surface(data, {}, false, decode_intent::thumbnail);
	files ff;
	return ff.image_to_surface(data, {}, false, decode_intent::thumbnail);
}

// Walks the GIF block chain looking for the XMP Data extension. Every read is guarded against the
// end of the file, and the caller treats a throw from here as "no XMP" rather than a failed scan:
// the dimensions are already parsed by then, and a truncated GIF must not lose them.
static void scan_gif_blocks(read_stream& s, file_scan_result& result, uint64_t offset)
{
	const auto size = s.size();

	while (offset + 1 < size)
	{
		const auto block_type = s.peek8(offset);
		offset += 1;

		if (block_type == kXMP_block_ImageDesc)
		{
			constexpr auto image_desc_len = 9;

			if (offset + image_desc_len > size)
			{
				break;
			}

			uint8_t image_desc[image_desc_len];
			s.read(offset, image_desc, image_desc_len);

			// ImageDesc is a special case, So read data just like its structure.

			// Reading Dimensions of image as 
			// 2 bytes = Image Left Position
			// + 2 bytes = Image Right Position
			// + 2 bytes = Image Width
			// + 2 bytes = Image Height
			// = 8 bytes
			offset += 8;

			// Reading one byte for Packed Fields
			const auto fields = image_desc[8];
			offset += 1;

			// Getting Local Table Size and skipping table size
			if (fields & 0x80)
			{
				const auto table_size = (1 << ((fields & 0x07) + 1)) * 3;
				offset += table_size;
			}

			// 1 byte LZW Minimum code size
			offset += 1;

			// 1 byte compressed sub-block size
			if (offset >= size)
			{
				break;
			}

			auto sub_block_size = s.peek8(offset);
			offset += 1;

			// Strictly less than: the loop body peeks at offset after advancing, so the last
			// sub-block must leave at least the following size byte inside the file.
			while (sub_block_size != 0 && offset + sub_block_size < size)
			{
				offset += sub_block_size;
				sub_block_size = s.peek8(offset);
				offset += 1;
			}
		}
		else if (block_type == kXMP_block_Extension)
		{
			if (offset + 2u > size)
			{
				break;
			}

			uint8_t read_buffer[256];
			const auto sub_extension_lbl = s.peek8(offset);
			auto sub_block_size = s.peek8(offset + 1);
			auto skip_block = true;

			offset += 2;

			if (sub_extension_lbl == 0xFF &&
				sub_block_size == APP_ID_LEN &&
				offset + APP_ID_LEN < size)
			{
				constexpr auto app_header_len = APP_ID_LEN;
				uint8_t app_header[app_header_len];
				s.read(offset, app_header, app_header_len);

				if (memcmp(app_header, XMP_APP_ID_DATA, APP_ID_LEN) == 0)
				{
					offset += APP_ID_LEN;
					const auto first_byte = s.peek8(offset);

					if (first_byte == '<')
					{
						const auto start = offset;
						const auto search_end = std::min(size, start + max_gif_xmp_bytes);

						while (offset < search_end && s.peek8(offset) != 0xff)
						{
							offset += 1;
						}

						if (offset < search_end)
						{
							const auto xmp_blob = s.read(start, static_cast<size_t>(offset - start - 1));
							const std::string_view text(std::bit_cast<const char*>(xmp_blob.data()), xmp_blob.size());
							constexpr std::string_view end_tag = "</x:xmpmeta>";

							// Trim anything after the closing tag. find() cannot report a partial
							// match, so a payload shorter than the tag simply keeps its full length.
							const auto pos = text.find(end_tag);
							const auto xmp_size = pos == std::string_view::npos
								                      ? xmp_blob.size()
								                      : pos + end_tag.size();

							result.metadata.xmp.assign(xmp_blob.begin(), xmp_blob.begin() + xmp_size);
						}
					}
					else
					{
						sub_block_size = first_byte;
						offset += 1;

						// The packet may be split over several sub-blocks, so accumulate them
						// rather than keeping only the last one.
						result.metadata.xmp.clear();

						while (sub_block_size != 0 && offset + sub_block_size < size)
						{
							s.read(offset, read_buffer, sub_block_size);
							result.metadata.xmp.insert(result.metadata.xmp.end(), read_buffer,
							                           read_buffer + sub_block_size);
							offset += sub_block_size;
							sub_block_size = s.peek8(offset);
							offset += 1;
						}
					}

					skip_block = false;
				}
			}

			if (skip_block)
			{
				// Extension block other than Application Extension
				while (sub_block_size != 0 && offset + sub_block_size < size)
				{
					offset += sub_block_size;
					sub_block_size = s.peek8(offset);
					offset += 1;
				}
			}
		}
		else if (block_type == kXMP_block_Trailer)
		{
			break;
		}
		else
		{
			// Invaild GIF Block
			break;
		}
	}
}

static file_scan_result scan_gif(read_stream& s)
{
	file_scan_result result;
	constexpr auto header_len = 11;
	uint8_t header[header_len];
	s.read(0, header, header_len);

	const auto isGif87a = memcmp(GIF_87_Header_DATA, header, GIF_Header_LEN) == 0;
	const auto isGif89a = memcmp(GIF_89_Header_DATA, header, GIF_Header_LEN) == 0;

	if (!isGif87a && !isGif89a)
	{
		throw app_exception("no gif header"s);
	}

	// GIF is little-endian, and header is a byte array: read through memcpy rather than casting
	// the pointer, which is both unaligned and host-endian dependent.
	result.width = get_uint16(header + 6, tiff_order_intel);
	result.height = get_uint16(header + 8, tiff_order_intel);

	uint64_t offset = 0u;
	offset += GIF_Header_LEN;

	// 2 bytes for Screen Width
	// 2 bytes for Screen Height
	// 1 byte for Packed Fields
	// 1 byte for Background Color Index
	// 1 byte for Pixel Aspect Ratio
	// Look for Global Color Table if exists
	const uint8_t fields = header[offset + 4];
	const int table_size = (fields & 0x80) ? (1 << ((fields & 0x07) + 1)) * 3 : 0;

	offset += table_size + 7;

	try
	{
		scan_gif_blocks(s, result, offset);
	}
	catch (const std::exception& e)
	{
		// XMP is optional; the dimensions above are not, and they are already read.
		df::log(__FUNCTION__, e.what());
	}

	// A truncated or corrupt GIF must not be cached as a successful 0x0 scan, or the timestamp
	// stamped alongside it means the file is never scanned again.
	result.success = result.width != 0 && result.height != 0;
	return result;
}


// A RIFF chunk inventory. WebP hides quite a lot behind its feature flags - alpha, animation,
// colour profile - and the chunk list is the plain statement of what the file actually holds.
static void scan_webp_structure(file_scan_result& result, const df::cspan data)
{
	if (data.size < 12u || memcmp(data.data, "RIFF", 4) != 0 || memcmp(data.data + 8, "WEBP", 4) != 0)
	{
		return;
	}

	metadata_kv_list kv;

	add_structure_section(kv, "Summary", "webp.summary", true);
	add_structure_row(kv, "File size", std::format("{} bytes", data.size));
	add_structure_row(kv, "RIFF size", std::format("{} bytes", get_uint32(data.data + 4, 0x4949) + 8u));

	add_structure_section(kv, "Chunks", "webp.chunks");

	size_t offset = 12;
	auto simple_format = std::string_view{};

	while (offset + 8u <= data.size)
	{
		const char fourcc[5] = {
			static_cast<char>(data.data[offset]), static_cast<char>(data.data[offset + 1]),
			static_cast<char>(data.data[offset + 2]), static_cast<char>(data.data[offset + 3]), 0
		};

		const auto chunk_size = get_uint32(data.data + offset + 4, 0x4949);
		if (chunk_size > data.size - offset - 8u) break;

		std::string_view description;

		if (memcmp(fourcc, "VP8 ", 4) == 0) description = "lossy image data";
		else if (memcmp(fourcc, "VP8L", 4) == 0) description = "lossless image data";
		else if (memcmp(fourcc, "VP8X", 4) == 0) description = "extended format header";
		else if (memcmp(fourcc, "ALPH", 4) == 0) description = "alpha channel";
		else if (memcmp(fourcc, "ANIM", 4) == 0) description = "animation parameters";
		else if (memcmp(fourcc, "ANMF", 4) == 0) description = "animation frame";
		else if (memcmp(fourcc, "ICCP", 4) == 0) description = "ICC colour profile";
		else if (memcmp(fourcc, "EXIF", 4) == 0) description = "Exif metadata";
		else if (memcmp(fourcc, "XMP ", 4) == 0) description = "XMP metadata";

		if (simple_format.empty() && (memcmp(fourcc, "VP8 ", 4) == 0 || memcmp(fourcc, "VP8L", 4) == 0))
		{
			simple_format = memcmp(fourcc, "VP8L", 4) == 0 ? "lossless (VP8L)" : "lossy (VP8)";
		}

		auto value = std::format("offset {}, {} bytes", offset, chunk_size);
		if (!description.empty()) value = std::format("{}, {}", description, value);

		auto& row = kv.emplace_back(std::string(fourcc), std::move(value));
		row.depth = 1;
		row.id = std::format("webp.chunk.{}", offset);

		// Chunks are padded to an even length.
		offset += 8u + chunk_size + (chunk_size & 1u);
	}

	if (!simple_format.empty())
	{
		// Reported next to the summary rather than buried in the chunk list.
		auto compression = metadata_kv("Compression"s, std::string(simple_format));
		compression.depth = 1;
		kv.insert(kv.begin() + 1, std::move(compression));
	}

	finish_structure_sections(kv);
	result.structure_metadata = std::move(kv);
}

static void scan_webp(file_scan_result& result, read_stream& s, const scan_intent intent)
{
	// EXIF/XMP chunks trail the image data in a RIFF container, so the whole file is
	// needed here; move the parsed blobs out rather than deep-copying them.
	df::blob owned;
	const auto data = s.view_all(owned);
	auto parts = scan_webp(data, false);

	result.width = parts.width;
	result.height = parts.height;
	result.pixel_format = parts.pixel_format;
	result.metadata = std::move(parts.metadata);

	if (intent == scan_intent::inspect)
	{
		scan_webp_structure(result, data);
	}

	// Set last: this writes through a reference, so a throw in the structure walk must not leave
	// behind a result that claims success.
	result.success = result.width != 0 && result.height != 0;
}

enum exif_tag
{
	EXIF_TAG_ORIENTATION = 0x0112,
	EXIF_TAG_JPEG_INTERCHANGE_FORMAT = 0x0201,
	EXIF_TAG_JPEG_INTERCHANGE_FORMAT_LENGTH = 0x0202,
	EXIF_TAG_IMAGE_WIDTH = 0x0100,
	EXIF_TAG_IMAGE_LENGTH = 0x0101,
	TAG_XMP = 700,
	EXIF_TAG_GPS_INFO_IFD_POINTER = 0x8825,
	EXIF_TAG_GPS_LATITUDE_REF = 0x0001,
	EXIF_TAG_GPS_LATITUDE = 0x0002,
	EXIF_TAG_GPS_LONGITUDE_REF = 0x0003,
	EXIF_TAG_GPS_LONGITUDE = 0x0004,
};

static void scan_exif(file_scan_result& result, const df::cspan data, const bool want_thumbnail,
                      files* const decoder)
{
	if (data.size > 16)
	{
		const auto order = get_uint16(data.data, tiff_order_intel);
		const auto limit = data.size - 12u;

		if (!is_valid_tiff_order(order))
		{
			return;
		}

		const auto magic = get_uint16(data.data + 2, order);

		if (magic == 42u)
		{
			const auto offset_ifd0 = get_uint32(data.data + 4u, order);

			if (offset_ifd0 < limit)
			{
				const auto entry_count = get_uint16(data.data + offset_ifd0, order);

				for (auto i = 0u; i < entry_count; ++i)
				{
					const uint64_t pos = offset_ifd0 + 2ull + 12ull * i;

					// entry_count is an unvalidated file field; stop at the first entry that would
					// run past the end rather than walking the remaining thousands doing nothing.
					if (pos >= limit)
					{
						break;
					}

					const auto tag = static_cast<exif_tag>(get_uint16(data.data + pos, order));

					switch (tag)
					{
					case EXIF_TAG_ORIENTATION:
						result.orientation = static_cast<ui::orientation>(get_uint16(data.data + pos + 8, order));
						break;
					}
				}

				const uint64_t entry_ifd1 = offset_ifd0 + 2ull + 12ull * entry_count;

				if (entry_ifd1 < limit)
				{
					auto offset_ifd1 = get_uint32(data.data + entry_ifd1, order);

					if (offset_ifd1 && offset_ifd1 < limit)
					{
						uint32_t possible_thumbnail_offset = 0;
						size_t possible_thumbnail_len = 0u;
						const auto ifd1_entry_count = get_uint16(data.data + offset_ifd1, order);

						offset_ifd1 += 2u;

						for (auto i = 0u; i < ifd1_entry_count; ++i)
						{
							const uint64_t pos = offset_ifd1 + 12ull * i;

							if (pos >= limit)
							{
								break;
							}

							const auto tag = static_cast<exif_tag>(get_uint16(data.data + pos, order));
							const auto format = static_cast<exif_format>(get_uint16(data.data + pos + 2, order));
							const auto ifd1_components = get_uint32(data.data + pos + 4, order);

							// A thumbnail pointer or length is one SHORT or LONG. Reading anything
							// else as one yields a plausible but fabricated offset into the file.
							const auto read_scalar = [&]() -> uint32_t
							{
								if (ifd1_components != 1u) return 0u;

								switch (format)
								{
								case FMT_USHORT: return get_uint16(data.data + pos + 8, order);
								case FMT_ULONG: return get_uint32(data.data + pos + 8, order);
								default: return 0u;
								}
							};

							switch (tag)
							{
							case EXIF_TAG_JPEG_INTERCHANGE_FORMAT:
								possible_thumbnail_offset = read_scalar();
								break;

							case EXIF_TAG_JPEG_INTERCHANGE_FORMAT_LENGTH:
								possible_thumbnail_len = read_scalar();
								break;

							case EXIF_TAG_ORIENTATION:
								result.orientation = static_cast<ui::orientation>(get_uint16(
									data.data + pos + 8, order));
								break;
							}
						}

						auto detected = detected_format::Unknown;

						// The IFD1 walk above still runs unconditionally - it carries the orientation.
						if (want_thumbnail &&
							possible_thumbnail_offset > 0 && possible_thumbnail_offset <= data.size &&
							possible_thumbnail_len <= max_embedded_thumbnail_bytes &&
							possible_thumbnail_len <= data.size - possible_thumbnail_offset)
						{
							const auto possible_thumbnail = data.data + possible_thumbnail_offset;
							if ((detected = files::detect_format({possible_thumbnail, possible_thumbnail_len})) !=
								detected_format::Unknown)
							{
								if (is_image_format(detected))
								{
									result.thumbnail_image = load_image_file({
										possible_thumbnail, possible_thumbnail_len
									});
								}
								else
								{
									result.thumbnail_surface = decode_thumbnail(decoder, {
										possible_thumbnail, possible_thumbnail_len
									});
								}
							}
						}
					}
				}
			}
		}
	}
}

static double load_gps_val(read_stream& s, const uint64_t pos, const unsigned short order)
{
	// Widen before adding: a near-UINT32_MAX offset would otherwise wrap back into the
	// file and yield a plausible but fabricated coordinate.
	const uint64_t offset = get_uint32(s.peek32(pos + 8), order);

	if (offset == 0 || offset + 24u > s.size())
	{
		throw app_exception("invalid TIFF GPS offset"s);
	}

	const auto degrees = metadata_exif::urational32_t(get_uint32(s.peek32(offset + 0), order),
	                                                  get_uint32(s.peek32(offset + 4), order));
	const auto minutes = metadata_exif::urational32_t(get_uint32(s.peek32(offset + 8), order),
	                                                  get_uint32(s.peek32(offset + 12), order));
	const auto seconds = metadata_exif::urational32_t(get_uint32(s.peek32(offset + 16), order),
	                                                  get_uint32(s.peek32(offset + 20), order));
	return gps_coordinate::dms_to_decimal(degrees.to_real(), minutes.to_real(), seconds.to_real());
}

static std::string load_text(read_stream& s, const uint64_t pos, const unsigned short order)
{
	const auto len = get_uint32(s.peek32(pos + 4), order);
	const auto offset = len <= 4 ? pos + 8u : get_uint32(s.peek32(pos + 8), order);
	constexpr uint32_t max_tiff_text_length = 64u * 1024u;
	if (len > max_tiff_text_length || offset > s.size() || len > s.size() - offset)
	{
		throw app_exception("invalid TIFF text field"s);
	}

	std::string result;
	result.resize(len, 0);
	s.read(offset, std::bit_cast<uint8_t*>(result.data()), len);
	return result;
}

static file_scan_result scan_tiff(read_stream& s, const bool want_thumbnail, files* const decoder)
{
	file_scan_result result;
	uint8_t header[8];
	s.read(0, header, 8);

	// header is a byte array, so read the mark through memcpy rather than casting the pointer.
	const auto order = get_uint16(header, tiff_order_intel);
	const auto size = s.size();

	if (size < 12u || !is_valid_tiff_order(order))
	{
		return result;
	}

	const auto magic = get_uint16(header + 2u, order);
	const auto limit = size - 12u;

	if (magic == 42u)
	{
		const auto offset_ifd0 = get_uint32(header + 4, order);

		if (offset_ifd0 < limit)
		{
			const auto entry_count = get_uint16(s.peek16(offset_ifd0), order);

			for (auto i = 0u; i < entry_count; ++i)
			{
				const auto pos = offset_ifd0 + 2u + 12ull * i;

				// entry_count comes straight from the file; stop at the first entry that
				// would run past the end rather than throwing away everything read so far.
				if (pos + 12u > size)
				{
					break;
				}

				uint8_t dir_data[12];
				s.read(pos, dir_data, 12u);

				const auto tag = static_cast<exif_tag>(get_uint16(dir_data, order));
				const auto format = static_cast<exif_format>(get_uint16(dir_data + 2u, order));
				const auto components = get_uint32(dir_data + 4u, order);

				// ImageWidth/ImageLength are SHORT or LONG. Reading a big-endian LONG as a
				// SHORT picks up the high half and yields 0 for any realistic dimension.
				const auto read_dimension = [&]() -> uint32_t
				{
					switch (format)
					{
					case FMT_USHORT: return get_uint16(dir_data + 8u, order);
					case FMT_ULONG: return get_uint32(dir_data + 8u, order);
					default: return 0u;
					}
				};

				switch (tag)
				{
				case EXIF_TAG_ORIENTATION:
					result.orientation = static_cast<ui::orientation>(get_uint16(dir_data + 8u, order));
					break;
				case EXIF_TAG_IMAGE_WIDTH:
					result.width = read_dimension();
					break;
				case EXIF_TAG_IMAGE_LENGTH:
					result.height = read_dimension();
					break;
				case TAG_XMP:
					{
						if (components != 0u && components <= max_tiff_xmp_bytes)
						{
							// A TIFF value of four bytes or fewer is stored in the entry itself; only a
							// larger one puts an offset there.
							if (components <= 4u)
							{
								result.metadata.xmp.assign(dir_data + 8u, dir_data + 8u + components);
							}
							else
							{
								const uint64_t xmp_offset = get_uint32(dir_data + 8u, order);

								// Both operands are already 64-bit, so this cannot wrap.
								if (xmp_offset + components <= size)
								{
									result.metadata.xmp = s.read(xmp_offset, components);
								}
							}
						}
					}
					break;
				case EXIF_TAG_GPS_INFO_IFD_POINTER:
					{
						const auto offset_gps = get_uint32(dir_data + 8u, order);

						if (offset_gps && offset_gps < limit)
						{
							exif_gps_coordinate_builder coordinate;
							const auto gps_entry_count = get_uint16(s.peek16(offset_gps), order);

							for (auto j = 0u; j < gps_entry_count; ++j)
							{
								const uint64_t gps_entry_pos = offset_gps + 2ull + 12ull * j;

								if (gps_entry_pos >= limit)
								{
									break;
								}

								{
									const auto gps_tag = static_cast<exif_tag>(get_uint16(
										s.peek16(gps_entry_pos), order));

									switch (gps_tag)
									{
									case EXIF_TAG_GPS_LATITUDE:
										{
											coordinate.latitude(load_gps_val(s, gps_entry_pos, order));
										}
										break;
									case EXIF_TAG_GPS_LATITUDE_REF:
										{
											const auto text = load_text(s, gps_entry_pos, order);

											// 'N' or 'S'
											if (first_char_is(text, 'S'))
											{
												coordinate.latitude_north_south(
													exif_gps_coordinate_builder::NorthSouth::South);
											}
											else
											{
												coordinate.latitude_north_south(
													exif_gps_coordinate_builder::NorthSouth::North);
											}
										}
										break;
									case EXIF_TAG_GPS_LONGITUDE:
										{
											coordinate.longitude(load_gps_val(s, gps_entry_pos, order));
										}
										break;
									case EXIF_TAG_GPS_LONGITUDE_REF:
										{
											const auto text = load_text(s, gps_entry_pos, order);
											// 'E' or 'W'
											if (first_char_is(text, 'W'))
											{
												coordinate.longitude_east_west(
													exif_gps_coordinate_builder::EastWest::West);
											}
											else
											{
												coordinate.longitude_east_west(
													exif_gps_coordinate_builder::EastWest::East);
											}
										}
										break;
									}
								}
							}

							result.gps = coordinate.build();
						}
					}
					break;
				}
			}

			const uint64_t entry_ifd1 = offset_ifd0 + 2ull + 12ull * entry_count;

			if (entry_ifd1 < limit)
			{
				const auto offset_ifd1 = get_uint32(s.peek32(entry_ifd1), order);

				if (offset_ifd1 && offset_ifd1 < limit)
				{
					size_t possible_offset = 0;
					size_t possible_thumbnail_len = 0;
					const auto ifd1_entry_count = get_uint16(s.peek16(offset_ifd1), order);

					for (auto i = 0u; i < ifd1_entry_count; ++i)
					{
						const uint64_t pos = offset_ifd1 + 2ull + 12ull * i;

						if (pos >= limit)
						{
							break;
						}

						{
							const auto tag = static_cast<exif_tag>(get_uint16(s.peek16(pos), order));
							const auto format = static_cast<exif_format>(get_uint16(s.peek16(pos + 2), order));
							const auto components = get_uint32(s.peek32(pos + 4), order);

							// A thumbnail pointer or length is one SHORT or LONG. Reading anything
							// else as one yields a plausible but fabricated offset into the file.
							const auto read_scalar = [&]() -> uint32_t
							{
								if (components != 1u) return 0u;

								switch (format)
								{
								case FMT_USHORT: return get_uint16(s.peek16(pos + 8), order);
								case FMT_ULONG: return get_uint32(s.peek32(pos + 8), order);
								default: return 0u;
								}
							};

							switch (tag)
							{
							case EXIF_TAG_JPEG_INTERCHANGE_FORMAT:
								possible_offset = read_scalar();
								break;

							case EXIF_TAG_JPEG_INTERCHANGE_FORMAT_LENGTH:
								possible_thumbnail_len = read_scalar();
								break;

							case EXIF_TAG_ORIENTATION:
								result.orientation = static_cast<ui::orientation>(get_uint16(
									s.peek16(pos + 8), order));
								break;
							}
						}
					}

					// The length is an unvalidated 32-bit field; cap it so a bogus value cannot
					// trigger a huge allocation or throw away the rest of the scan.

					// s.read here is a seek and a read off the file, not a view into one, so a scan
					// that will not use the thumbnail must not reach it.
					if (want_thumbnail &&
						possible_offset != 0 && possible_thumbnail_len != 0 &&
						possible_thumbnail_len <= max_embedded_thumbnail_bytes &&
						possible_offset <= size && possible_thumbnail_len <= size - possible_offset)
					{
						const auto thumb = s.read(possible_offset, possible_thumbnail_len);
						auto detected = detected_format::Unknown;

						if (thumb.size() > 16 &&
							(detected = files::detect_format(thumb)) != detected_format::Unknown)
						{
							if (is_image_format(detected))
							{
								result.thumbnail_image = load_image_file(thumb);
							}
							else
							{
								result.thumbnail_surface = decode_thumbnail(decoder, thumb);
							}
						}
					}
				}
			}
		}
	}

	// A recognised byte order with an unusable magic (BigTIFF, truncated or corrupt) must
	// not be cached as a successful 0x0 scan, or it will never be retried. The dimensions are
	// unvalidated 32-bit fields, and a nonsense one that reaches sizei turns negative and defeats
	// the decode budget it is later checked against.
	result.success = result.width != 0 && result.height != 0 &&
		result.width <= max_image_dimension && result.height <= max_image_dimension;

	if (!result.success)
	{
		result.width = 0;
		result.height = 0;
	}

	return result;
}

file_scan_result scan_photo(read_stream& s, const scan_intent intent, const bool want_thumbnail,
                            files* const decoder)
{
	file_scan_result result;

	if (s.size() < sizeof(pack128))
	{
		// Empty or too small to contain a recognisable image signature.
		return result;
	}

	try
	{
		const auto expected = files::detect_format(s.peek128(0));

		if (expected == detected_format::BMP)
		{
			constexpr auto bmp_header_bytes = sizeof(files_structs::BITMAPFILEHEADER) +
				sizeof(files_structs::BITMAPINFOHEADER);

			// detect_format only guarantees the 2-byte "BM" signature, so verify the file is
			// long enough before reading the info header.
			if (s.size() < bmp_header_bytes)
			{
				return result;
			}

			uint8_t header[sizeof(files_structs::BITMAPINFOHEADER)];
			s.read(sizeof(files_structs::BITMAPFILEHEADER), header, sizeof(files_structs::BITMAPINFOHEADER));
			files_structs::BITMAPINFOHEADER bmp_info;
			std::memcpy(&bmp_info, header, sizeof(bmp_info));

			// biWidth/biHeight are signed (a negative height means a top-down bitmap). Take the
			// magnitude in 64-bit: abs(INT32_MIN) is undefined and would produce a bogus size.
			const auto abs_width = std::abs(static_cast<int64_t>(bmp_info.biWidth));
			const auto abs_height = std::abs(static_cast<int64_t>(bmp_info.biHeight));

			if (bmp_info.biSize < sizeof(files_structs::BITMAPINFOHEADER) ||
				abs_width <= 0 || abs_height <= 0 ||
				abs_width > max_image_dimension || abs_height > max_image_dimension)
			{
				return result;
			}

			result.width = static_cast<uint32_t>(abs_width);
			result.height = static_cast<uint32_t>(abs_height);
			result.pixel_format = str::cache(std::format("RGB{}", bmp_info.biBitCount));
			result.format = detected_format::BMP;
			result.success = true;
		}
		else if (expected == detected_format::GIF)
		{
			result = scan_gif(s);
			result.format = detected_format::GIF;
			result.pixel_format = "pal8"_c;
		}
		else if (expected == detected_format::JPEG) // && memcmp(p, sig_jpg, 3) == 0)
		{
			result = scan_jpg(s, intent, want_thumbnail);
			result.format = detected_format::JPEG;
		}
		else if (expected == detected_format::PNG) // && memcmp(p, sig_png, 3) == 0)
		{
			result = scan_png(s);
			result.format = detected_format::PNG;
		}
		else if (expected == detected_format::TIFF)
		{
			result = scan_tiff(s, want_thumbnail, decoder);
			result.format = detected_format::TIFF;
		}
		else if (expected == detected_format::HEIF)
		{
			result = scan_heif(s, intent, want_thumbnail);
			result.format = detected_format::HEIF;
		}
		else if (expected == detected_format::WEBP)
		{
			scan_webp(result, s, intent);
			result.format = detected_format::WEBP;
		}
		else if (expected == detected_format::JXL)
		{
			result = scan_jxl(s);
			result.format = detected_format::JXL;
		}
		else if (expected == detected_format::PSD)
		{
			result = scan_psd(s);
			result.format = detected_format::PSD;
		}

		if (!result.metadata.exif.empty())
		{
			scan_exif(result, result.metadata.exif, want_thumbnail, decoder);

			// An Exif thumbnail is stored unrotated whatever the container did, so it always takes
			// the Exif orientation; a decoded thumbnail only needs it when the decoder did not
			// already apply that item's own transform.
			if (result.thumbnail_image) result.thumbnail_image->orientation(result.orientation);

			if (result.thumbnail_surface && !result.thumbnail_orientation_applied)
			{
				result.thumbnail_surface->orientation(result.orientation);
			}

			if (result.orientation_applied)
			{
				result.orientation = ui::orientation::top_left;
			}
		}
	}
	catch (std::exception& e)
	{
		df::log(__FUNCTION__, e.what());
	}

	return result;
}
