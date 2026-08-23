// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: ZIP archive handling. Creates and extracts ZIP files, lists archive
// contents, and provides zlib compression/decompression.

#include "pch.h"
#include "util_zip.h"

#include "files.h"
#include "util_file.h"

#define ZLIB_CONST 1

#include "zlib.h"
#include "minizip/mz.h"
#include "minizip/compat/zip.h"
#include "minizip/compat/unzip.h"

// Archive entry names are attacker-controlled: reduce to a bare leaf name so an entry such as
// "..\..\startup\evil.exe" cannot escape the destination folder. Returns empty to reject.

df::zip_file::~zip_file()
{
	if (_handle.has_value())
	{
		close();
	}
}

bool df::zip_file::create(const file_path zip_file_path)
{
	assert_true(!_handle.has_value());
	const auto handle = zipOpen64(zip_file_path.str().c_str(), 0);

	if (handle == nullptr) return false;

	_handle = handle;
	return true;
}

bool df::zip_file::close()
{
	if (_handle.has_value())
	{
		const auto result = zipClose(std::any_cast<zipFile>(_handle), nullptr) == ZIP_OK;
		_handle.reset();
		return result;
	}

	return true;
}

bool df::zip_file::add(const file_path path, const std::string_view name_in) const
{
	file f;

	if (f.open_read(path, true))
	{
		const auto ft = date_t(platform::file_attributes(path).modified).date();
		const auto name = std::string(name_in);
		//const auto wpath = platform::to_file_system_path(path)

		zip_fileinfo zi = {};
		zi.tmz_date.tm_year = ft.year;
		zi.tmz_date.tm_mon = ft.month;
		zi.tmz_date.tm_mday = ft.day;
		zi.tmz_date.tm_hour = ft.hour;
		zi.tmz_date.tm_min = ft.minute;
		zi.tmz_date.tm_sec = ft.second;

		auto err = zipOpenNewFileInZip(std::any_cast<zipFile>(_handle), str::utf8_cast2(name).c_str(), &zi, nullptr, 0,
		                               nullptr, 0, nullptr, Z_DEFLATED, Z_BEST_COMPRESSION);

		if (err != ZIP_OK)
		{
			df::log(__FUNCTION__, std::format("error in opening {} in zip file", name));
			return false;
		}

		while (f.read64k())
		{
			// Read in and write the item
			// in multiple buffer loads

			// Write
			err = zipWriteInFileInZip(std::any_cast<zipFile>(_handle), f.buffer(),
			                          static_cast<uint32_t>(f.buffer_data_size()));

			if (err != ZIP_OK)
			{
				//We could not write the file in the ZIP-File for whatever reason.
				df::log(__FUNCTION__, std::format("error writing {} in zip file", name));
				zipCloseFileInZip(std::any_cast<zipFile>(_handle));
				return false;
			}
		}

		err = zipCloseFileInZip(std::any_cast<zipFile>(_handle));

		if (err != ZIP_OK)
		{
			df::log(__FUNCTION__, std::format("error closing {} in zip file", name));
			return false;
		}

		return true;
	}

	return false;
}

bool df::zip_file::add(const file_path path)
{
	return add(path, path.name());
}

std::vector<archive_item> df::zip_file::list(const file_path zip_file_path)
{
	std::vector<archive_item> results;

	constexpr int max_path = 256;
	char filename[max_path];
	auto* const hz = unzOpen2_64(zip_file_path.str().c_str(), nullptr);

	if (hz)
	{
		unz_global_info64 info;
		unz_file_info64 file;

		if (UNZ_OK == unzGetGlobalInfo64(hz, &info) &&
			UNZ_OK == unzGoToFirstFile(hz))
		{
			do
			{
				if (UNZ_OK == unzGetCurrentFileInfo64(hz, &file, filename, max_path, nullptr, 0, nullptr, 0))
				{
					archive_item result_info;
					result_info.filename = str::utf8_cast(filename);
					result_info.uncompressed_size = file.uncompressed_size;
					result_info.compressed_size = file.compressed_size;
					result_info.created = platform::dos_date_to_ts(static_cast<uint16_t>(file.dosDate >> 16),
					                                               static_cast<uint16_t>(file.dosDate));
					results.emplace_back(result_info);
				}
			}
			while (UNZ_OK == unzGoToNextFile(hz));
		}

		unzClose(hz);
	}

	return results;
}
