// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Commodore 64 disk image (D64, D71, D81, T64, CRT) and single-file
// program container (P00, PRG) parser. Reads and lists contents of retro
// computing disk, cartridge and program formats, and provides an embedded 8x8
// C64 character-set glyph atlas for rendering directory listings.

#include "pch.h"
#include "files.h"

namespace
{
struct media_entry
{
	uint8_t file_type = 0;
	std::vector<uint8_t> pet_name;
	uint32_t file_size = 0;
};

struct d64_media
{
	std::vector<media_entry> entries;
};
}

// 8x8 bitmaps of the C64 uppercase/graphics character set, indexed by screen
// code. Each glyph is 8 rows; within a row the most-significant bit is the
// left-most pixel. Screen codes 0x00-0x3F cover @, A-Z, punctuation and digits
// (everything a normal directory listing needs); 0x40-0x7F are the CBM graphics
// characters and are left blank (rendered as spaces) for now.
static const uint8_t c64_font_8x8[128][8] = {
	{0x3C, 0x66, 0x6E, 0x6E, 0x60, 0x62, 0x3C, 0x00}, // 0x00 @
	{0x18, 0x3C, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00}, // 0x01 A
	{0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0x00}, // 0x02 B
	{0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00}, // 0x03 C
	{0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0x00}, // 0x04 D
	{0x7E, 0x60, 0x60, 0x78, 0x60, 0x60, 0x7E, 0x00}, // 0x05 E
	{0x7E, 0x60, 0x60, 0x78, 0x60, 0x60, 0x60, 0x00}, // 0x06 F
	{0x3C, 0x66, 0x60, 0x6E, 0x66, 0x66, 0x3C, 0x00}, // 0x07 G
	{0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00}, // 0x08 H
	{0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00}, // 0x09 I
	{0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x6C, 0x38, 0x00}, // 0x0A J
	{0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66, 0x00}, // 0x0B K
	{0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E, 0x00}, // 0x0C L
	{0x63, 0x77, 0x7F, 0x6B, 0x63, 0x63, 0x63, 0x00}, // 0x0D M
	{0x66, 0x76, 0x7E, 0x7E, 0x6E, 0x66, 0x66, 0x00}, // 0x0E N
	{0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00}, // 0x0F O
	{0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60, 0x00}, // 0x10 P
	{0x3C, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x0E, 0x00}, // 0x11 Q
	{0x7C, 0x66, 0x66, 0x7C, 0x78, 0x6C, 0x66, 0x00}, // 0x12 R
	{0x3C, 0x66, 0x60, 0x3C, 0x06, 0x66, 0x3C, 0x00}, // 0x13 S
	{0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00}, // 0x14 T
	{0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00}, // 0x15 U
	{0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00}, // 0x16 V
	{0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00}, // 0x17 W
	{0x66, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x66, 0x00}, // 0x18 X
	{0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x18, 0x00}, // 0x19 Y
	{0x7E, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x7E, 0x00}, // 0x1A Z
	{0x3C, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3C, 0x00}, // 0x1B [
	{0x0C, 0x12, 0x30, 0x7C, 0x30, 0x62, 0xFC, 0x00}, // 0x1C British pound
	{0x3C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3C, 0x00}, // 0x1D ]
	{0x00, 0x18, 0x3C, 0x7E, 0x18, 0x18, 0x18, 0x00}, // 0x1E up arrow
	{0x00, 0x10, 0x30, 0x7F, 0x7F, 0x30, 0x10, 0x00}, // 0x1F left arrow
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x20 space
	{0x18, 0x18, 0x18, 0x18, 0x00, 0x00, 0x18, 0x00}, // 0x21 !
	{0x66, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x22 "
	{0x66, 0x66, 0xFF, 0x66, 0xFF, 0x66, 0x66, 0x00}, // 0x23 #
	{0x18, 0x3E, 0x60, 0x3C, 0x06, 0x7C, 0x18, 0x00}, // 0x24 $
	{0x62, 0x66, 0x0C, 0x18, 0x30, 0x66, 0x46, 0x00}, // 0x25 %
	{0x3C, 0x66, 0x3C, 0x38, 0x67, 0x66, 0x3F, 0x00}, // 0x26 &
	{0x06, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x27 '
	{0x0C, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0C, 0x00}, // 0x28 (
	{0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x18, 0x30, 0x00}, // 0x29 )
	{0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00}, // 0x2A *
	{0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00}, // 0x2B +
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30}, // 0x2C ,
	{0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00}, // 0x2D -
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00}, // 0x2E .
	{0x00, 0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x00}, // 0x2F /
	{0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C, 0x00}, // 0x30 0
	{0x18, 0x18, 0x38, 0x18, 0x18, 0x18, 0x7E, 0x00}, // 0x31 1
	{0x3C, 0x66, 0x06, 0x0C, 0x30, 0x60, 0x7E, 0x00}, // 0x32 2
	{0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C, 0x00}, // 0x33 3
	{0x06, 0x0E, 0x1E, 0x66, 0x7F, 0x06, 0x06, 0x00}, // 0x34 4
	{0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C, 0x00}, // 0x35 5
	{0x3C, 0x66, 0x60, 0x7C, 0x66, 0x66, 0x3C, 0x00}, // 0x36 6
	{0x7E, 0x66, 0x0C, 0x18, 0x18, 0x18, 0x18, 0x00}, // 0x37 7
	{0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C, 0x00}, // 0x38 8
	{0x3C, 0x66, 0x66, 0x3E, 0x06, 0x66, 0x3C, 0x00}, // 0x39 9
	{0x00, 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00}, // 0x3A :
	{0x00, 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x30}, // 0x3B ;
	{0x0E, 0x18, 0x30, 0x60, 0x30, 0x18, 0x0E, 0x00}, // 0x3C <
	{0x00, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x00, 0x00}, // 0x3D =
	{0x70, 0x18, 0x0C, 0x06, 0x0C, 0x18, 0x70, 0x00}, // 0x3E >
	{0x3C, 0x66, 0x06, 0x0C, 0x18, 0x00, 0x18, 0x00}, // 0x3F ?
	// 0x40-0x7F: CBM graphics characters (blank for now)
};

// Convert a PETSCII code to the C64 screen code that indexes c64_font_8x8.
static uint8_t petscii_to_screen_code(const uint8_t c)
{
	if (c < 0x40) return c; // 0x20-0x3F: punctuation & digits
	if (c < 0x60) return c - 0x40; // 0x40-0x5F: @ A-Z [ pound ] up-arrow left-arrow
	if (c < 0x80) return c - 0x20; // 0x60-0x7F: graphics
	if (c < 0xC0) return c - 0x40; // 0xA0-0xBF: graphics
	if (c < 0xFF) return c - 0x80; // 0xC0-0xFE: graphics
	return 0x5E; // 0xFF
}

// Map a raw PETSCII/ASCII byte to the screen code used for glyph rendering. Control
// codes, space and shifted space all render as a blank cell.
static uint8_t to_glyph_code(const uint8_t c)
{
	if (c <= 0x20 || c == 0xA0 || (c >= 0x80 && c < 0xA0)) return 0x20;
	return petscii_to_screen_code(c);
}

// Map a PETSCII byte to the plain-text form of the listing. Graphics and control codes
// have no ASCII equivalent, so they become blanks and stay aligned with the glyph cells.
static char to_ascii(const uint8_t c)
{
	if (c >= 0x20 && c <= 0x5A) return static_cast<char>(c);
	if (c >= 0xC1 && c <= 0xDA) return static_cast<char>(c - 0x80); // shifted A-Z
	return ' ';
}

// A real 1581 directory holds at most 296 entries, so this only bites on a corrupt or
// hostile image. It matters because the listing is rasterised into a single texture on
// the UI thread: an uncapped chain can yield thousands of rows, and once the resulting
// texture exceeds the Direct3D size limit the upload fails and the whole surface is
// rebuilt on every frame.
static constexpr size_t max_listing_lines = 512;

static std::vector<files::d64_item> dir_list(const d64_media& disk)
{
	std::vector<files::d64_item> result;

	for (const auto& entry : disk.entries)
	{
		if (result.size() >= max_listing_lines) break;

		if (entry.file_type != 0)
		{
			std::string line;
			std::vector<uint8_t> codes;

			// Append a character to both the display string and the glyph list.
			const auto push = [&](const uint8_t src_byte)
			{
				line += to_ascii(src_byte);
				codes.emplace_back(to_glyph_code(src_byte));
			};

			for (const auto c : str::to_string(entry.file_size))
			{
				push(static_cast<uint8_t>(c));
			}

			// Always leave at least one space, even for a four-digit 1581 block count.
			do
			{
				push(' ');
			}
			while (line.size() < 4);

			push('\"');

			for (const auto c : entry.pet_name)
			{
				push(c);
			}

			push('\"');

			// Pad out to the file-type column. A CRT name is 32 bytes and already runs past
			// it, so fall back to a single separator rather than butting up against the type.
			if (line.size() < 23)
			{
				while (line.size() < 23) push(' ');
			}
			else
			{
				push(' ');
			}

			std::string file_type;

			if (entry.file_type == 0x80) file_type = "DEL";
			if (entry.file_type == 0x81) file_type = "SEQ";
			if (entry.file_type == 0x82) file_type = "PRG";
			if (entry.file_type == 0x83) file_type = "USR";
			if (entry.file_type == 0x84) file_type = "REL";
			if (entry.file_type == 0x99) file_type = "CRT";

			for (const auto c : file_type)
			{
				push(static_cast<uint8_t>(c));
			}

			files::d64_item item;
			item.line = std::move(line);
			item.screen_codes = std::move(codes);
			result.emplace_back(std::move(item));
		}
	}

	return result;
}

enum class disk_format
{
	unknown,
	d64, // 1541 single sided (35 or 40 tracks, optional error info)
	d71, // 1571 double sided (70 tracks, optional error info)
	d81 // 1581 3.5" (80 tracks)
};

static disk_format classify_disk(const size_t len)
{
	switch (len)
	{
	case 174848: // 35 tracks, no error info
	case 175531: // 35 tracks + 683 error bytes
	case 196608: // 40 tracks, no error info
	case 197376: // 40 tracks + 768 error bytes
		return disk_format::d64;
	case 349696: // 70 tracks, no error info
	case 351062: // 70 tracks + 1366 error bytes
		return disk_format::d71;
	case 819200: // 80 tracks
		return disk_format::d81;
	default:
		return disk_format::unknown;
	}
}

// 1541/1571 use a zoned recording scheme with a variable number of sectors per track.
static int d64_sectors_per_track(const int track)
{
	if (track <= 17) return 21;
	if (track <= 24) return 19;
	if (track <= 30) return 18;
	return 17;
}

// Byte offset of a (track, sector) within a disk image. Tracks are 1-based.
static int disk_sector_offset(const disk_format fmt, const int track, const int sector)
{
	constexpr int SECTOR_SIZE = 256;

	if (fmt == disk_format::d81)
	{
		return ((track - 1) * 40 + sector) * SECTOR_SIZE;
	}

	int sectors = 0;

	for (int t = 1; t < track; ++t)
	{
		// On a 1571 the second side (tracks 36..70) mirrors the zoning of side one.
		const int zone_track = (fmt == disk_format::d71 && t > 35) ? t - 35 : t;
		sectors += d64_sectors_per_track(zone_track);
	}

	return (sectors + sector) * SECTOR_SIZE;
}

static d64_media parse_disk(const uint8_t* const data, const size_t data_len)
{
	const auto fmt = classify_disk(data_len);

	if (fmt == disk_format::unknown)
		return {};

	constexpr int SECTOR_SIZE = 256;
	constexpr int DIR_ENTRY_SIZE = 32;
	const int dir_track = (fmt == disk_format::d81) ? 40 : 18;
	const int dir_sector = (fmt == disk_format::d81) ? 3 : 1;

	d64_media disk;
	std::unordered_set<int> visited; // guard against cyclic directory chains

	int next_track = dir_track;
	int next_sector = dir_sector;

	while (next_track != 0)
	{
		const auto dir_sector_offset = disk_sector_offset(fmt, next_track, next_sector);

		if (dir_sector_offset < 0 ||
			dir_sector_offset + SECTOR_SIZE > static_cast<int>(data_len))
			break;

		if (!visited.insert(dir_sector_offset).second)
			break; // already visited this sector - malformed cyclic chain

		for (int i = 0; i < SECTOR_SIZE; i += DIR_ENTRY_SIZE)
		{
			const auto dir_entry = dir_sector_offset + i;
			const uint8_t file_type = data[dir_entry + 2];

			if (file_type != 0)
			{
				media_entry entry;
				entry.file_type = file_type;
				entry.file_size = data[dir_entry + 30] + (data[dir_entry + 31] << 8);
				entry.pet_name.assign(data + dir_entry + 5, data + dir_entry + 5 + 16);
				disk.entries.emplace_back(entry);
			}
		}
		next_track = data[dir_sector_offset];
		next_sector = data[dir_sector_offset + 1];
	}

	return disk;
}

#pragma pack(push, 1)

struct t64_header
{
	uint8_t signature[32];
	uint16_t version;
	uint16_t used_entries;
	uint16_t total_entries;
	uint8_t reserved[26];
};

struct t64_file_entry
{
	uint8_t type;
	uint8_t type_1541;
	uint16_t start_address;
	uint16_t end_address;
	uint16_t reserved1;
	uint32_t offset;
	uint32_t reserved2;
	uint8_t file_name[16];
};

struct crt_header
{
	uint8_t signature[16];
	uint32_t header_length;
	uint16_t version;
	uint16_t cartridge_type;
	uint8_t port_exrom;
	uint8_t port_game;
	uint8_t reserved[6];
	uint8_t name[32];
};

#pragma pack(pop)

static d64_media parse_t64(const uint8_t* const data, const size_t data_len)
{
	d64_media result;

	if (data_len < sizeof(t64_header))
		return result;

	const auto header = reinterpret_cast<const t64_header*>(data);

	// Some tools write an incorrect used_entries count; fall back to the total.
	uint32_t count = header->used_entries;
	if (count == 0 || count > header->total_entries) count = header->total_entries;

	for (uint32_t i = 0; i < count; ++i)
	{
		const auto dir_offset = sizeof(t64_header) + i * sizeof(t64_file_entry);

		if (dir_offset + sizeof(t64_file_entry) > data_len)
			break;

		const auto file = reinterpret_cast<const t64_file_entry*>(data + dir_offset);

		if (file->type == 0)
			continue; // free / unused entry

		media_entry entry;
		entry.file_type = file->type_1541 ? file->type_1541 : 0x82; // default to PRG
		entry.pet_name.assign(file->file_name, file->file_name + 16);

		const uint32_t data_bytes = file->end_address > file->start_address
			                            ? file->end_address - file->start_address
			                            : 0u;
		entry.file_size = (data_bytes + 2 + 253) / 254; // load address plus data, in disk blocks
		result.entries.push_back(entry);
	}

	return result;
}

static d64_media parse_crt(const uint8_t* const data, const size_t data_len)
{
	d64_media result;

	// A CRT header is 64 bytes; the 32-byte cartridge name lives at offset 32.
	if (data_len < sizeof(crt_header))
		return result;

	const auto header = reinterpret_cast<const crt_header*>(data);

	// A cartridge is a single logical item regardless of how many ROM (CHIP)
	// banks it contains, so list it once by name.
	media_entry entry;
	entry.file_type = 0x99;
	entry.pet_name.assign(header->name, header->name + 32);
	entry.file_size = static_cast<uint32_t>((data_len - sizeof(crt_header) + 253) / 254);
	result.entries.push_back(entry);

	return result;
}

// A .P00 file wraps a single program: an 8-byte "C64File\0" signature, a
// 16-byte PETSCII name, a record-size byte, then the raw file data.
static d64_media parse_p00(const uint8_t* const data, const size_t data_len)
{
	d64_media result;

	if (data_len < 26)
		return result;

	media_entry entry;
	entry.file_type = 0x82; // PRG
	entry.pet_name.assign(data + 8, data + 8 + 16);
	const size_t payload = data_len - 26;
	entry.file_size = static_cast<uint32_t>((payload + 253) / 254); // size in disk blocks
	result.entries.push_back(entry);

	return result;
}

// A raw .PRG is just a 2-byte little-endian load address followed by data.
// It carries no embedded name, so present it as a single unnamed program.
static d64_media parse_prg(const uint8_t* const data, const size_t data_len)
{
	d64_media result;

	if (data_len < 2)
		return result;

	media_entry entry;
	entry.file_type = 0x82; // PRG
	entry.pet_name.assign(16, 0xA0); // shifted spaces render as blanks
	// The load address counts towards the block total, as it does inside a .P00.
	entry.file_size = static_cast<uint32_t>((data_len + 253) / 254);
	result.entries.push_back(entry);

	return result;
}


std::vector<files::d64_item> files::list_disk(const df::blob& selected_item_data)
{
	const auto* const data = selected_item_data.data();
	const auto len = selected_item_data.size();

	d64_media media;

	if (len >= 8 &&
		std::memcmp(data, "C64File", 7) == 0)
	{
		media = parse_p00(data, len);
	}
	else if (len > 32 &&
		std::memcmp(data, "C64", 3) == 0 &&
		// The 32-byte descriptor is not fixed text: writers emit "C64 tape image file",
		// "C64S tape image file" and "C64S tape file", so match the common substring
		// rather than one exact spelling.
		std::string_view(std::bit_cast<const char*>(data), 32).find("tape") != std::string_view::npos)
	{
		media = parse_t64(data, len);
	}
	else if (len >= sizeof(crt_header) &&
		std::memcmp(data, "C64 CARTRIDGE   ", 16) == 0)
	{
		media = parse_crt(data, len);
	}
	else if (classify_disk(len) != disk_format::unknown)
	{
		media = parse_disk(data, len);
	}
	else if (len <= 65538)
	{
		// A raw .PRG program has no signature or fixed size, but it cannot exceed a full
		// C64 memory image plus its load address. Anything larger that matched no format
		// above is a corrupt or unsupported container, so list nothing rather than one
		// bogus unnamed row.
		media = parse_prg(data, len);
	}

	return dir_list(media);
}

ui::const_surface_ptr files::c64_listing_surface(const std::vector<d64_item>& lines, const uint32_t fg,
                                                 const uint32_t bg)
{
	if (lines.empty()) return {};

	size_t cols = 0;
	for (const auto& line : lines) cols = std::max(cols, line.screen_codes.size());
	if (cols == 0) return {};

	const int w = static_cast<int>(cols) * 8;
	const int h = static_cast<int>(lines.size()) * 8;

	auto surface = std::make_shared<ui::surface>();
	auto* const pixels = surface->alloc(w, h, ui::texture_format::RGB);
	const auto stride = surface->stride();

	// RGB textures upload as B8G8R8X8, so pixels are written in BGR order.
	const auto fg_px = ui::bgr(fg);
	const auto bg_px = ui::bgr(bg);

	for (size_t li = 0; li < lines.size(); ++li)
	{
		const auto& codes = lines[li].screen_codes;
		const int oy = static_cast<int>(li) * 8;

		for (int row = 0; row < 8; ++row)
		{
			auto* const dst = std::bit_cast<uint32_t*>(pixels + (oy + row) * stride);

			// alloc does not zero, so fill the row padding too rather than uploading
			// uninitialised bytes to the GPU.
			std::fill_n(dst, stride / 4u, bg_px);

			for (size_t ci = 0; ci < codes.size(); ++ci)
			{
				const uint8_t bits = c64_font_8x8[codes[ci] & 0x7f][row];

				// Most rows of a directory listing are blank; skipping them avoids eight
				// pointless tests per empty cell.
				if (bits == 0) continue;

				auto* const cell = dst + ci * 8;

				for (int col = 0; col < 8; ++col)
				{
					if (bits & (0x80 >> col)) cell[col] = fg_px;
				}
			}
		}
	}

	return surface;
}
