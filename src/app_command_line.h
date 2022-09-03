// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

struct command_line_t
{
	df::file_path selection;
	df::item_selector folder_path;

	bool no_gpu = false;
	bool no_indexing = false;
	bool run_tests = false;

	void parse(std::u8string_view command_line_text);
	std::u8string format_restart_cmd_line() const;
};

extern command_line_t command_line;
