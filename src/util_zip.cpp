// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "pch.h"
#include "util_zip.h"

#include "files.h"
#include "util_file.h"

#define ZLIB_CONST 1

#include "zlib.h"
#include "minizip/mz.h"
#include "minizip/mz_compat.h"

df::blob df::zlib_compress(cspan data_in)
{
	blob result;

	constexpr auto BUFSIZE = 128_z * 1024_z;
	const auto temp_buffer = unique_alloc<uint8_t>(BUFSIZE);

	z_stream strm;
	strm.zalloc = nullptr;
	strm.zfree = nullptr;
	strm.next_in = data_in.data;
	strm.avail_in = data_in.size;
	strm.next_out = temp_buffer.get();
	strm.avail_out = BUFSIZE;

	deflateInit(&strm, Z_BEST_COMPRESSION);

	while (strm.avail_in != 0)
	{
		const int res = deflate(&strm, Z_NO_FLUSH);
		assert_true(res == Z_OK);
		if (strm.avail_out == 0)
		{
			result.insert(result.end(), temp_buffer.get(), temp_buffer.get() + BUFSIZE);
			strm.next_out = temp_buffer.get();
			strm.avail_out = BUFSIZE;
		}
	}

	int deflate_res = Z_OK;
	while (deflate_res == Z_OK)
	{
		if (strm.avail_out == 0)
		{
			result.insert(result.end(), temp_buffer.get(), temp_buffer.get() + BUFSIZE);
			strm.next_out = temp_buffer.get();
			strm.avail_out = BUFSIZE;
		}
		deflate_res = deflate(&strm, Z_FINISH);
	}

	assert_true(deflate_res == Z_STREAM_END);
	result.insert(result.end(), temp_buffer.get(), temp_buffer.get() + BUFSIZE - strm.avail_out);
	deflateEnd(&strm);

	return result;
}

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
	_handle = zipOpen64(zip_file_path.str().c_str(), 0);
	return _handle.has_value();
}

bool df::zip_file::close()
{
	if (_handle.has_value())
	{
		zipClose(std::any_cast<zipFile>(_handle), nullptr);
		_handle.reset();
	}

	return true;
}

bool df::zip_file::add(const file_path path, const std::u8string_view name_in)
{
	file f;

	if (f.open_read(path, true))
	{
		const auto ft = date_t(platform::file_attributes(path).modified).date();
		const auto name = std::u8string(name_in);
		//const auto wpath = platform::to_file_system_path(path)

		zip_fileinfo zi;
		memset(&zi, 0, sizeof(zi));
		zi.tmz_date.tm_year = ft.year;
		zi.tmz_date.tm_mon = ft.month;
		zi.tmz_date.tm_mday = ft.day;
		zi.tmz_date.tm_hour = ft.hour;
		zi.tmz_date.tm_min = ft.minute;
		zi.tmz_date.tm_sec = ft.second;

		auto err = zipOpenNewFileInZip(std::any_cast<zipFile>(_handle), str::utf8_cast2(name).c_str(), &zi, nullptr, 0,
		                               nullptr, 0, nullptr,Z_DEFLATED, Z_BEST_COMPRESSION);

		if (err != ZIP_OK)
		{
			df::log(__FUNCTION__, str::format(u8"error in opening {} in zip file"sv, name));
			return false;
		}

		while (f.read64k())
		{
			// Read in an write the item
			// in multiple buffer loads

			// Write
			err = zipWriteInFileInZip(std::any_cast<zipFile>(_handle), f.buffer(),
			                          static_cast<uint32_t>(f.buffer_data_size()));

			if (err != ZIP_OK)
			{
				//We could not write the file in the ZIP-File for whatever reason.
				df::log(__FUNCTION__, str::format(u8"error writing {} in zip file"sv, name));
				return false;
			}
		}

		err = zipCloseFileInZip(std::any_cast<zipFile>(_handle));

		if (err != ZIP_OK)
		{
			df::log(__FUNCTION__, str::format(u8"error closing {} in zip file"sv, name));
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

size_t df::zip_file::extract(const file_path zip_file_path, const folder_path dest_folder_path)
{
	std::vector<std::pair<file_path, file_path>> moves;

	const int max_path = 256;
	char filename[max_path];
	const auto write_buffer = df::unique_alloc<uint8_t>(sixty_four_k);
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
					if (file.uncompressed_size == 0) // Folder
					{
						// ?
					}
					else
					{
						if (UNZ_OK == unzOpenCurrentFile(hz))
						{
							auto file_path = platform::temp_file();
							auto f = open_file(file_path, platform::file_open_mode::write);

							if (f)
							{
								moves.emplace_back(
									file_path, folder_path(dest_folder_path).combine_file(str::utf8_cast(filename)));

								auto read = unzReadCurrentFile(hz, write_buffer.get(), sixty_four_k);

								while (read > 0)
								{
									f->write(write_buffer.get(), read);
									read = unzReadCurrentFile(hz, write_buffer.get(), sixty_four_k);
								}

								f->set_created(platform::dos_date_to_ts(static_cast<uint16_t>(file.dosDate >> 16),
								                                        static_cast<uint16_t>(file.dosDate)));
							}
						}
					}
				}
				unzCloseCurrentFile(hz);
			}
			while (UNZ_OK == unzGoToNextFile(hz));
		}

		unzClose(hz);
	}

	for (const auto& m : moves)
	{
		const auto move_result = platform::move_file(m.first, m.second, true);

		if (move_result.failed())
		{
			throw app_exception(str::format(u8"Failed to write file {}\n{}"sv, m.second, move_result.format_error()));
		}
	}

	return moves.size();
}

std::vector<archive_item> df::zip_file::list(const file_path zip_file_path)
{
	std::vector<archive_item> results;

	const int max_path = 256;
	char filename[max_path];
	const auto write_buffer = df::unique_alloc<uint8_t>(sixty_four_k);
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
