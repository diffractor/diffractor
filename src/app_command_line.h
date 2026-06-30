// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Command line argument parsing. Handles startup options like folder paths,
// file selection, no-gpu mode, and test execution flags.

#pragma once

struct command_line_t
{
	df::file_path selection;
	df::item_selector folder_path;

	bool no_gpu = false;
	bool no_indexing = false;
	bool run_tests = false;
	bool console_test = false;
	std::string test_filter = "*";

	bool gen_docs = false;
	std::string docs_path;

	void parse(std::string_view command_line_text);
	std::string format_restart_cmd_line() const;
};

extern command_line_t command_line;
