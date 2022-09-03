// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "pch.h"
#include "metadata_exif.h"
#include "files.h"
#include "files_structs.h"


static constexpr auto GIF_Header_LEN = 6;
static constexpr auto GIF_87_Header_DATA = "GIF87a";
static constexpr auto GIF_89_Header_DATA = "GIF89a";

static constexpr auto APP_ID_LEN = 11;
static constexpr auto XMP_APP_ID_DATA = R"(XMP DataXMP)";
static constexpr auto MAGIC_TRAILER_LEN = 258;

enum gif_block_type
{
	kXMP_block_ImageDesc = 0x2C,
	kXMP_block_Extension = 0x21,
	kXMP_block_Trailer = 0x3B,
	kXMP_block_Header = 0x47
};

static const char* strnstr(const char* haystack, const char* needle, size_t len)
{
	size_t needle_len;

	if (0 == (needle_len = strnlen(needle, len)))
		return haystack;

	for (auto i = 0; i <= static_cast<int>(len - needle_len); i++)
	{
		if ((haystack[0] == needle[0]) &&
			(0 == strncmp(haystack, needle, needle_len)))
			return haystack;

		haystack++;
	}
	return nullptr;
}

static file_scan_result scan_gif(read_stream& s)
{
	file_scan_result result;
	const auto header_len = 11;
	uint8_t header[header_len];
	s.read(0, header, header_len);

	const auto isGif87a = memcmp(GIF_87_Header_DATA, header, GIF_Header_LEN) == 0;
	const auto isGif89a = memcmp(GIF_89_Header_DATA, header, GIF_Header_LEN) == 0;

	if (!isGif87a && !isGif89a)
	{
		throw app_exception("no gif header");
	}

	result.width = *std::bit_cast<const uint16_t*>(header + 6);
	result.height = *std::bit_cast<const uint16_t*>(header + 8);

	uint64_t offset = 0u;
	offset += GIF_Header_LEN;

	// 2 bytes for Screen Width
	// 2 bytes for Screen Height
	// 1 byte for Packed Fields
	// 1 byte for Background Color Index
	// 1 byte for Pixel Aspect Ratio
	// Look for Global Color Table if exists
	const uint8_t fields = header[offset + 4];
	const int table_size = (1 << ((fields & 0x07) + 1)) * 3;

	offset += table_size + 7;

	const auto size = s.size();

	// Parsing rest of the blocks
	while (offset + 1 < size)
	{
		const auto block_type = s.peek8(offset);
		offset += 1;

		if (block_type == kXMP_block_ImageDesc)
		{
			const auto image_desc_len = 9;
			uint8_t image_desc[image_desc_len];
			s.read(offset, image_desc, image_desc_len);

			// ImageDesc is a special case, So read data just like its structure.

			// Reading Dimesnions of image as 
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
			auto sub_block_size = s.peek8(offset);
			offset += 1;

			while (sub_block_size != 0 && offset + sub_block_size <= size)
			{
				offset += sub_block_size;
				sub_block_size = s.peek8(offset);
				offset += 1;
			}
		}
		else if (block_type == kXMP_block_Extension)
		{
			uint8_t read_buffer[256];
			const auto sub_extension_lbl = s.peek8(offset);
			auto sub_block_size = s.peek8(offset + 1);
			auto skip_block = true;

			offset += 2;

			if (sub_extension_lbl == 0xFF &&
				sub_block_size == APP_ID_LEN)
			{
				const auto app_header_len = APP_ID_LEN;
				uint8_t app_header[app_header_len];
				s.read(offset, app_header, app_header_len);

				if (memcmp(app_header, XMP_APP_ID_DATA, APP_ID_LEN) == 0)
				{
					offset += APP_ID_LEN;
					const auto n = s.peek8(offset);

					if (n == '<')
					{
						const auto start = offset;
						auto n = s.peek8(offset);

						while (n != 0xff)
						{
							offset += 1;
							n = s.peek8(offset);
						}

						auto xmp_size = offset - start - 1;

						const auto xmp_blob = s.read(start, xmp_size);
						const auto* const xmp = std::bit_cast<const char*>(xmp_blob.data());
						const auto* const end_marker = strnstr(xmp, "</x:xmpmeta>", xmp_size);

						if (end_marker != nullptr)
						{
							xmp_size = end_marker + 12u - xmp;
						}

						const auto* const xmp_data = std::bit_cast<const uint8_t*>(xmp);
						result.metadata.xmp.assign(xmp_data, xmp_data + xmp_size);
					}
					else
					{
						sub_block_size = n;
						offset += 1;

						while (sub_block_size != 0 && offset + sub_block_size <= size)
						{
							s.read(offset, read_buffer, sub_block_size);
							result.metadata.xmp.assign(read_buffer, read_buffer + sub_block_size);
							offset += sub_block_size;
							sub_block_size = s.peek8(offset);
							offset += 1;
						}
					}

					df::assert_true(result.metadata.xmp.data()[result.metadata.xmp.size() - 1] == '>');
					skip_block = false;
				}
			}

			if (skip_block)
			{
				// Extension block other than Application Extension
				while (sub_block_size != 0 && offset + sub_block_size <= size)
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

	result.success = true;
	return result;
}


uint16_t get_uint16(uint16_t n, uint16_t order)
{
	return order == 0x4949 ? n : df::byteswap16(n);
}

uint16_t get_uint16(const uint8_t* p, uint16_t order)
{
	const auto n = *std::bit_cast<const uint16_t*>(p);
	return order == 0x4949 ? n : df::byteswap16(n);
}

uint32_t get_uint32(const uint32_t n, uint16_t order)
{
	return order == 0x4949 ? n : df::byteswap32(n);
}

uint32_t get_uint32(const uint8_t* p, uint16_t order)
{
	const auto n = *std::bit_cast<const uint32_t*>(p);
	return order == 0x4949 ? n : df::byteswap32(n);
}

static void scan_webp(file_scan_result& result, read_stream& s)
{
	const auto blob = s.read_all();
	const auto parts = scan_webp(blob, false);

	result.width = parts.width;
	result.height = parts.height;
	result.metadata = parts.metadata;
	result.success = true;
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

static void scan_exif(file_scan_result& result, df::cspan data)
{
	if (data > 16)
	{
		df::assert_true(!is_exif_signature(data));

		const auto order = *std::bit_cast<const uint16_t*>(data.data);
		const auto magic = get_uint16(data.data + 2, order);
		const auto limit = data.size - 12u;

		if (magic == 42u)
		{
			const auto offset_ifd0 = get_uint32(data.data + 4u, order);

			if (offset_ifd0 < limit)
			{
				const auto entry_count = get_uint16(data.data + offset_ifd0, order);

				for (auto i = 0u; i < entry_count; ++i)
				{
					const auto pos = offset_ifd0 + 2u + (12u * i);

					if (pos < limit)
					{
						const auto tag = static_cast<exif_tag>(get_uint16(data.data + pos, order));

						switch (tag)
						{
						case EXIF_TAG_ORIENTATION:
							result.orientation = static_cast<ui::orientation>(get_uint16(data.data + pos + 8, order));
							break;
						}
					}
				}

				const auto entry_ifd1 = offset_ifd0 + 2 + 12 * entry_count;

				if (entry_ifd1 < limit)
				{
					auto offset_ifd1 = get_uint32(data.data + entry_ifd1, order);

					if (offset_ifd1 && offset_ifd1 < limit)
					{
						const uint8_t* possible_thumbnail = nullptr;
						size_t posible_thumbnail_len = 0u;
						const auto ifd1_entry_count = get_uint16(data.data + offset_ifd1, order);

						offset_ifd1 += 2u;

						for (auto i = 0u; i < ifd1_entry_count; ++i)
						{
							const auto pos = offset_ifd1 + (12u * i);

							if (pos < limit)
							{
								const auto tag = static_cast<exif_tag>(get_uint16(data.data + pos, order));
								const auto format = static_cast<exif_format>(get_uint16(data.data + pos + 2, order));
								const auto ifd1_components = get_uint32(data.data + pos + 4, order);

								switch (tag)
								{
								case EXIF_TAG_JPEG_INTERCHANGE_FORMAT:
									possible_thumbnail = data.data + get_uint32(data.data + pos + 8, order);
									break;

								case EXIF_TAG_JPEG_INTERCHANGE_FORMAT_LENGTH:
									posible_thumbnail_len = get_uint32(data.data + pos + 8, order);
									break;

								case EXIF_TAG_ORIENTATION:
									result.orientation = static_cast<ui::orientation>(get_uint16(
										data.data + pos + 8, order));
									break;
								}
							}
						}

						auto detected = detected_format::Unknown;

						if (possible_thumbnail > data.data &&
							possible_thumbnail + posible_thumbnail_len <= data.data + data.size &&
							(detected = files::detect_format({possible_thumbnail, posible_thumbnail_len})) !=
							detected_format::Unknown)
						{
							if (is_image_format(detected))
							{
								result.thumbnail_image = load_image_file({possible_thumbnail, posible_thumbnail_len});
							}
							else
							{
								files ff;
								result.thumbnail_surface = ff.image_to_surface({
									possible_thumbnail, posible_thumbnail_len
								});
							}
						}
					}
				}
			}
		}
		else
		{
			df::assert_true(false);
		}
	}
}

static double load_gps_val(read_stream& s, const uint32_t pos, const unsigned short order)
{
	const auto offset = get_uint32(s.peek32(pos + 8), order);
	const auto degrees = metadata_exif::urational32_t(get_uint32(s.peek32(offset + 0), order),
	                                                  get_uint32(s.peek32(offset + 4), order));
	const auto minutes = metadata_exif::urational32_t(get_uint32(s.peek32(offset + 8), order),
	                                                  get_uint32(s.peek32(offset + 12), order));
	const auto seconds = metadata_exif::urational32_t(get_uint32(s.peek32(offset + 16), order),
	                                                  get_uint32(s.peek32(offset + 20), order));
	return gps_coordinate::dms_to_decimal(degrees.to_real(), minutes.to_real(), seconds.to_real());
}

static std::u8string load_text(read_stream& s, const uint32_t pos, const unsigned short order)
{
	const auto len = get_uint32(s.peek32(pos + 4), order);
	const auto offset = len <= 4 ? pos + 8u : get_uint32(s.peek32(pos + 8), order);

	std::u8string result;
	result.resize(len, 0);
	s.read(offset, std::bit_cast<uint8_t*>(result.data()), len);
	return result;
}

static file_scan_result scan_tiff(read_stream& s)
{
	file_scan_result result;
	uint8_t header[8];
	s.read(0, header, 8);

	const auto order = *std::bit_cast<const uint16_t*>(static_cast<const uint8_t*>(header));
	const auto magic = get_uint16(header + 2u, order);
	const auto size = s.size();
	const auto limit = size - 12u;

	if (magic == 42u)
	{
		const auto offset_ifd0 = get_uint32(header + 4, order);

		if (offset_ifd0 < limit)
		{
			const auto entry_count = get_uint16(s.peek16(offset_ifd0), order);

			for (auto i = 0u; i < entry_count; ++i)
			{
				const auto pos = (12u * i);
				uint8_t dir_data[12];
				s.read(offset_ifd0 + 2u + pos, dir_data, 12u);

				const auto tag = static_cast<exif_tag>(get_uint16(dir_data, order));
				const auto components = get_uint32(dir_data + 4u, order);

				switch (tag)
				{
				case EXIF_TAG_ORIENTATION:
					result.orientation = static_cast<ui::orientation>(get_uint16(dir_data + 8u, order));
					break;
				case EXIF_TAG_IMAGE_WIDTH:
					result.width = get_uint16(dir_data + 8u, order);
					break;
				case EXIF_TAG_IMAGE_LENGTH:
					result.height = get_uint16(dir_data + 8u, order);
					break;
				case TAG_XMP:
					{
						const uint64_t xmp_offset = get_uint32(dir_data + 8u, order);

						if (xmp_offset + components <= size) // overflow
						{
							result.metadata.xmp = s.read(xmp_offset, components);
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
								const auto gps_entry_pos = offset_gps + 2 + (12 * j);

								if (gps_entry_pos < limit)
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

				const auto entry_ifd1 = offset_ifd0 + 2 + 12 * entry_count;

				if (entry_ifd1 < limit)
				{
					const auto offset_ifd1 = get_uint32(s.peek32(entry_ifd1), order);

					if (offset_ifd1 && offset_ifd1 < limit)
					{
						size_t possible_offset = 0;
						size_t posible_thumbnail_len = 0;
						const auto ifd1_entry_count = get_uint16(s.peek16(offset_ifd1), order);

						for (auto i = 0u; i < ifd1_entry_count; ++i)
						{
							const auto pos = offset_ifd1 + 2 + (12 * i);

							if (pos < limit)
							{
								const auto tag = static_cast<exif_tag>(get_uint16(s.peek16(pos), order));
								// auto format = static_cast<metadata_exif::Format>(get_uint16(s.peek16(pos + 2), order));
								// auto ifd1_components = get_uint32(s.peek32(pos + 4), order);

								switch (tag)
								{
								case EXIF_TAG_JPEG_INTERCHANGE_FORMAT:
									possible_offset = get_uint32(s.peek32(pos + 8), order);
									break;

								case EXIF_TAG_JPEG_INTERCHANGE_FORMAT_LENGTH:
									posible_thumbnail_len = get_uint32(s.peek32(pos + 8), order);
									break;

								case EXIF_TAG_ORIENTATION:
									result.orientation = static_cast<ui::orientation>(get_uint16(
										s.peek16(pos + 8), order));
									break;
								}
							}
						}

						if (possible_offset != 0 && posible_thumbnail_len != 0)
						{
							auto thumb = s.read(possible_offset, posible_thumbnail_len);
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
									files ff;
									result.thumbnail_surface = ff.image_to_surface(thumb);
								}
							}
						}
					}
				}
			}
		}
	}

	result.success = true;
	return result;
}

file_scan_result scan_photo(read_stream& s)
{
	file_scan_result result;

	try
	{
		const auto expected = files::detect_format(s.peek128(0));

		if (expected == detected_format::BMP)
		{
			//const auto bmp_header = std::bit_cast<const BITMAPFILEHEADER*>(s.read(0, sizeof(BITMAPFILEHEADER)));
			uint8_t header[sizeof(files_structs::BITMAPINFOHEADER)];
			s.read(sizeof(files_structs::BITMAPFILEHEADER), header, sizeof(files_structs::BITMAPINFOHEADER));
			const auto* const bmp_info = std::bit_cast<const files_structs::BITMAPINFOHEADER*>(
				static_cast<uint8_t*>(header));

			result.width = abs(bmp_info->biWidth);
			result.height = abs(bmp_info->biHeight);
			result.pixel_format = str::cache(str::format(u8"RGB{}", bmp_info->biBitCount));
			result.format = detected_format::BMP;
			result.success = true;
		}
		else if (expected == detected_format::GIF)
		{
			result = scan_gif(s);
			result.format = detected_format::GIF;
			result.pixel_format = str::cache(u8"pal8");
		}
		else if (expected == detected_format::JPEG) // && memcmp(p, sig_jpg, 3) == 0)
		{
			result = scan_jpg(s);
			result.format = detected_format::JPEG;
		}
		else if (expected == detected_format::PNG) // && memcmp(p, sig_png, 3) == 0)
		{
			result = scan_png(s);
			result.format = detected_format::PNG;
		}
		else if (expected == detected_format::TIFF)
		{
			result = scan_tiff(s);
			result.format = detected_format::TIFF;
		}
		else if (expected == detected_format::HEIF)
		{
			result = scan_heif(s);
			result.format = detected_format::HEIF;
		}
		else if (expected == detected_format::WEBP)
		{
			scan_webp(result, s);
			result.format = detected_format::WEBP;
		}
		else if (expected == detected_format::PSD)
		{
			result = scan_psd(s);
			result.format = detected_format::PSD;
		}

		if (!result.metadata.exif.empty())
		{
			scan_exif(result, result.metadata.exif);

			if (result.thumbnail_image) result.thumbnail_image->orientation(result.orientation);
			if (result.thumbnail_surface) result.thumbnail_surface->orientation(result.orientation);
		}
	}
	catch (std::exception& e)
	{
		df::log(__FUNCTION__, e.what());
	}

	return result;
}
