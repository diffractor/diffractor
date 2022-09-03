// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

struct archive_item;

namespace df
{
	blob zlib_compress(cspan data_in);

	class zip_file
	{
	protected:
		std::any _handle;

	public:
		zip_file() = default;
		virtual ~zip_file();

		bool create(file_path path);
		bool close();

		bool add(file_path path, std::u8string_view name);
		bool add(file_path path);

		static size_t extract(file_path zip_file_path, folder_path dest_folder_path);
		static std::vector<archive_item> list(file_path zip_file_path);
	};
}
