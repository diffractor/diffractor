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
	bool console_test = false;
	std::string test_filter = "*";
	std::string test_temp_folder;

	bool gen_docs = false;
	std::string docs_path;

	bool validate_po = false;

	// Read-only measurement of what perceptual-hash duplicate grouping does to a real library.
	bool dup_report = false;
	std::string dup_report_folder;
	std::string dup_report_output;

#ifdef _DEBUG
	std::string test_action;
	std::string screenshot_scene;
	std::string screenshot_output;
#endif

	void parse(std::string_view command_line_text);
	std::string format_restart_cmd_line() const;
};

extern command_line_t command_line;
