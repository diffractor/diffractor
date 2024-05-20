// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

class crash_files_db
{
	using path_map = df::hash_map<df::file_path, std::u8string_view, df::ihash, df::ieq>;
	using path_set = df::hash_set<df::file_path, df::ihash, df::ieq>;

	df::file_path _crash_files_path;

	path_set load_paths(const df::file_path path)
	{
		path_set result;

		u8istream file(platform::to_file_system_path(path));
		std::u8string line;

		while (std::getline(file, line))
		{
			result.emplace(df::file_path{line});
		}

		return result;
	}

	path_set _crash_files;
	path_map _open_files;

	platform::mutex _mtx;

public:
	crash_files_db(const df::file_path path) : _crash_files_path(path)
	{
		_crash_files = load_paths(path);
	}

	bool is_known_crash_file(const df::file_path path)
	{
		return _crash_files.contains(path);
	}

	void add_open(const df::file_path path, const std::u8string_view context)
	{
		platform::exclusive_lock lock(_mtx);
		_open_files[path] = context;
	}

	void remove_open(const df::file_path path)
	{
		platform::exclusive_lock lock(_mtx);
		_open_files.erase(path);
	}

	void flush_open_files()
	{
		if (!_open_files.empty())
		{
			u8ostream file;
			file.open(platform::to_file_system_path(_crash_files_path), std::ios_base::app); // append 

			for (const auto& path : _open_files)
			{
				df::log(__FUNCTION__, str::format(u8"Add file type to crash list {}"sv, path.first.extension()));
				file << path.first.str() << std::endl;
			}
		}
	}

	void log_open_files()
	{
		if (!_open_files.empty())
		{
			for (const auto& path : _open_files)
			{
				df::log(__FUNCTION__, str::format(u8"Open file {}"sv, path.first.str()));
			}
		}
	}

	std::u8string open_files_list()
	{
		u8ostringstream result;

		for (const auto& path : _open_files)
		{
			result << path.first.str() << u8" ["sv << path.second << u8"] "sv << std::endl;
		}

		return result.str();
	}
};


struct record_open_path
{
	df::file_path _path;
	crash_files_db& files_that_crash_diffractor_;
	std::u8string_view _context;

	record_open_path(crash_files_db& files_that_crash_diffractor, const df::file_path path,
	                 const std::u8string_view context) : _path(path),
	                                                     files_that_crash_diffractor_(files_that_crash_diffractor),
	                                                     _context(context)
	{
		files_that_crash_diffractor_.add_open(path, _context);
	}

	~record_open_path()
	{
		files_that_crash_diffractor_.remove_open(_path);
	}
};

extern crash_files_db crash_files;
