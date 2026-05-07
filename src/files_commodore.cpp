// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Commodore 64 disk image (D64, D81, T64, CRT) parser. Reads and lists
// contents of retro computing disk and cartridge formats.

#include "pch.h"
#include "files.h"

struct media_entry
{
	//bool available = false;
	uint8_t next_track = 0;
	uint8_t next_sector = 0;
	uint8_t file_type;
	uint8_t start_track = 0;
	uint8_t start_sector = 0;
	std::vector<uint8_t> pet_name;
	uint32_t adress_start = 0;
	uint32_t adress_end = 0;
	uint8_t sectors = 0;
	uint32_t file_size = 0;
};

struct d64_media
{
	std::string filename;
	std::string diskname;
	std::vector<media_entry> entries;
};

static wchar_t c64_uppercase_normal_chars[] = {
	/* Non-printable C0 set */
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	/* Private use area matching C64 Pro Mono STYLE font */
	0x0020, 0xE021, 0xE022, 0xE023, 0xE024, 0xE025, 0xE026, 0xE027,
	0xE028, 0xE029, 0xE02A, 0xE02B, 0xE02C, 0xE02D, 0xE02E, 0xE02F,
	0xE030, 0xE031, 0xE032, 0xE033, 0xE034, 0xE035, 0xE036, 0xE037,
	0xE038, 0xE039, 0xE03A, 0xE03B, 0xE03C, 0xE03D, 0xE03E, 0xE03F,
	0xE040, 0xE041, 0xE042, 0xE043, 0xE044, 0xE045, 0xE046, 0xE047,
	0xE048, 0xE049, 0xE04A, 0xE04B, 0xE04C, 0xE04D, 0xE04E, 0xE04F,
	0xE050, 0xE051, 0xE052, 0xE053, 0xE054, 0xE055, 0xE056, 0xE057,
	0xE058, 0xE059, 0xE05A, 0xE05B, 0xE05C, 0xE05D, 0xE05E, 0xE05F,
	0xE060, 0xE061, 0xE062, 0xE063, 0xE064, 0xE065, 0xE066, 0xE067,
	0xE068, 0xE069, 0xE06A, 0xE06B, 0xE06C, 0xE06D, 0xE06E, 0xE06F,
	0xE070, 0xE071, 0xE072, 0xE073, 0xE074, 0xE075, 0xE076, 0xE077,
	0xE078, 0xE079, 0xE07A, 0xE07B, 0xE07C, 0xE07D, 0xE07E, 0xE07F,
	/* Non-printable C1 set */
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	/* Private use area matching C64 Pro Mono STYLE font */
	0x0020, 0xE0A1, 0xE0A2, 0xE0A3, 0xE0A4, 0xE0A5, 0xE0A6, 0xE0A7,
	0xE0A8, 0xE0A9, 0xE0AA, 0xE0AB, 0xE0AC, 0xE0AD, 0xE0AE, 0xE0AF,
	0xE0B0, 0xE0B1, 0xE0B2, 0xE0B3, 0xE0B4, 0xE0B5, 0xE0B6, 0xE0B7,
	0xE0B8, 0xE0B9, 0xE0BA, 0xE0BB, 0xE0BC, 0xE0BD, 0xE0BE, 0xE0BF,
	0xE0C0, 0xE0C1, 0xE0C2, 0xE0C3, 0xE0C4, 0xE0C5, 0xE0C6, 0xE0C7,
	0xE0C8, 0xE0C9, 0xE0CA, 0xE0CB, 0xE0CC, 0xE0CD, 0xE0CE, 0xE0CF,
	0xE0D0, 0xE0D1, 0xE0D2, 0xE0D3, 0xE0D4, 0xE0D5, 0xE0D6, 0xE0D7,
	0xE0D8, 0xE0D9, 0xE0DA, 0xE0DB, 0xE0DC, 0xE0DD, 0xE0DE, 0xE0DF,
	0xE0E0, 0xE0E1, 0xE0E2, 0xE0E3, 0xE0E4, 0xE0E5, 0xE0E6, 0xE0E7,
	0xE0E8, 0xE0E9, 0xE0EA, 0xE0EB, 0xE0EC, 0xE0ED, 0xE0EE, 0xE0EF,
	0xE0F0, 0xE0F1, 0xE0F2, 0xE0F3, 0xE0F4, 0xE0F5, 0xE0F6, 0xE0F7,
	0xE0F8, 0xE0F9, 0xE0FA, 0xE0FB, 0xE0FC, 0xE0FD, 0xE0FE, 0xE0FF
};

static uint32_t to_unicode(const uint32_t cp, const bool alt)
{
	const auto c = c64_uppercase_normal_chars[cp & 0xff];
	if (c == 0) return ' ';
	return c;
}

static std::vector<files::d64_item> dir_list(const d64_media& disk)
{
	std::vector<files::d64_item> result;

	for (const auto& entry : disk.entries)
	{
		if (entry.file_type != 0)
		{
			std::vector<uint32_t> line;

			for (const auto c : str::to_string(entry.file_size))
			{
				line.emplace_back(c);
			}

			while (line.size() < 4) line.emplace_back(' ');
			line.emplace_back('\"');
			//line.push_back(0x22); //	"

			for (const auto c : entry.pet_name)
			{
				line.emplace_back(to_unicode(c, false));
			}

			line.emplace_back('\"');
			while (line.size() < 23) line.emplace_back(' ');

			std::string file_type;

			if (entry.file_type == 0x80) file_type = "DEL";
			if (entry.file_type == 0x81) file_type = "SEQ";
			if (entry.file_type == 0x82) file_type = "PRG";
			if (entry.file_type == 0x83) file_type = "USR";
			if (entry.file_type == 0x84) file_type = "REL";
			if (entry.file_type == 0x99) file_type = "CRT";

			for (const auto c : file_type)
			{
				line.emplace_back(c);
			}

			std::string line2;
			auto inserter = std::back_inserter(line2);
			for (const uint32_t c : line)
			{
				str::char32_to_utf8(inserter, c);
			}


			result.emplace_back(line2);
		}
	}

	return result;
}

static d64_media parse_disk(const uint8_t* const data, const size_t data_len)
{
	const auto is_d64 = data_len == 0x002ab00;
	const auto is_d81 = data_len == 0x00c8000;

	constexpr int SECTOR_SIZE = 256;
	const int DIR_TRACK = is_d64 ? 18 : 40;
	const int DIR_SECTOR = is_d64 ? 1 : 3;
	constexpr int DIR_ENTRY_SIZE = 32;
	const int SECTORS_PER_TRACK = is_d64 ? 21 : 40;

	d64_media disk;

	int next_track = DIR_TRACK;
	int next_sector = DIR_SECTOR;

	while (next_track != 0)
	{
		const auto dir_sector_offset = ((next_track - 1) * SECTORS_PER_TRACK + next_sector) * SECTOR_SIZE;

		if (dir_sector_offset + SECTOR_SIZE > data_len)
			break;

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

struct crt_chip_header
{
	uint8_t signature[4];
	uint32_t chip_packet_length;
	uint16_t chip_type;
	uint16_t bank_number;
	uint16_t load_address;
	uint16_t rom_image_size;
};

#pragma pack(pop)

d64_media parse_t64(const uint8_t* const data, const size_t data_len)
{
	d64_media result;
	const auto header = reinterpret_cast<const t64_header*>(data);

	for (uint16_t i = 0; i < header->used_entries; ++i)
	{
		const auto dir_offset = sizeof(t64_header) + i * sizeof(t64_file_entry);

		if (dir_offset + sizeof(t64_file_entry) > data_len)
			break;

		const auto file = reinterpret_cast<const t64_file_entry*>(data + dir_offset);
		media_entry entry;
		entry.file_type = file->type_1541;
		entry.pet_name.assign(file->file_name, file->file_name + 16);
		result.entries.push_back(entry);
	}

	return result;
}

d64_media parse_crt(const uint8_t* const data, const size_t data_len)
{
	const auto header = reinterpret_cast<const crt_header*>(data);

	d64_media result;
	size_t offset = sizeof(crt_header);

	while (offset < data_len)
	{
		if (data_len - offset < sizeof(crt_chip_header))
		{
			break;
		}

		const auto chipHeader = reinterpret_cast<const crt_chip_header*>(data + offset);
		if (std::memcmp(chipHeader->signature, "CHIP", 4) != 0)
		{
			break;
		}

		media_entry entry;
		entry.file_type = 0x99;
		entry.pet_name.assign(header->name, header->name + 32);
		result.entries.push_back(entry);

		offset += sizeof(crt_chip_header) + chipHeader->rom_image_size;
	}

	return result;
}


std::vector<files::d64_item> files::list_disk(const df::blob& selected_item_data)
{
	const auto is_d64 = selected_item_data.size() == 0x002ab00;
	const auto is_d81 = selected_item_data.size() == 0x00c8000;

	d64_media media;

	if (selected_item_data.size() > 32 &&
		std::memcmp(selected_item_data.data(), "C64 tape image file", 19) == 0)
	{
		media = parse_t64(selected_item_data.data(), selected_item_data.size());
	}
	else if (selected_item_data.size() > 16 &&
		std::memcmp(selected_item_data.data(), "C64 CARTRIDGE   ", 16) == 0)
	{
		media = parse_crt(selected_item_data.data(), selected_item_data.size());
	}
	else if (is_d64 || is_d81)
	{
		media = parse_disk(selected_item_data.data(), selected_item_data.size());
	}

	return dir_list(media);
}
