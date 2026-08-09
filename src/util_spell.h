// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Spell checking integration. Wraps Hunspell library for
// spell checking user input in metadata fields.

#pragma once

class Hunspell;

class spell_check
{
	mutable platform::mutex _rw;

	_Guarded_by_(_rw) std::unique_ptr<Hunspell> _hunspell;
	df::file_path _custom_dic_path;
	df::folder_path _dictionaries_folder;

public:
	spell_check();
	~spell_check();

	// Delete copy constructor and assignment operator for safety
	spell_check(const spell_check&) = delete;
	spell_check& operator=(const spell_check&) = delete;

	void lazy_download(df::async_i& async) const;
	void lazy_load();
	bool is_word_valid(std::string_view word) const;
	std::vector<std::string> suggest(std::string_view word) const;
	void add_word(std::string_view word) const;
};

// Its constructor resolves known folders, probes the file system and may create the dictionaries
// directory. As a global that would run before WinMain, where a throw ends the process with nothing
// logged and no message box; constructed on first use instead, inside the app's error handling.
spell_check& spell();
