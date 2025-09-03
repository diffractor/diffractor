// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2024  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

class Hunspell;

class spell_check
{
private:
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

	void lazy_download(df::async_i& async);
	void lazy_load();
	bool is_word_valid(std::u8string_view word) const;
	std::vector<std::u8string> suggest(std::u8string_view word) const;
	void add_word(std::u8string_view word);
};

extern spell_check spell;
