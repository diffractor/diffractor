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

	// Dictionaries ship beside the executable, but that folder is read-only in a Store install and
	// in any per-machine install, so reads come from either folder and every write goes to the
	// per-user one.
	df::folder_path _shipped_folder;
	df::folder_path _user_folder;

	df::file_path find_dictionary(std::string_view name, std::string_view ext) const;
	bool ensure_user_folder() const;

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

	// The file "Add to dictionary" appends to. Exposed so a test can prove it is somewhere the user
	// can write, without writing there.
	df::file_path custom_dictionary_path() const { return _custom_dic_path; }
};

// Its constructor resolves known folders, probes the file system and may create the dictionaries
// directory. As a global that would run before WinMain, where a throw ends the process with nothing
// logged and no message box; constructed on first use instead, inside the app's error handling.
spell_check& spell();
