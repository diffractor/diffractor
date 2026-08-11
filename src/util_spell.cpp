// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Spell checking integration. Uses Hunspell library for spell checking,
// suggestions, and custom dictionary management.

#include "pch.h"
#include "util_spell.h"

#define HUNSPELL_STATIC
#include <hunspell.hxx>

spell_check& spell()
{
	static spell_check instance;
	return instance;
}

spell_check::spell_check()
{
	_shipped_folder = known_path(platform::known_folder::running_app_folder).combine("dictionaries");
	_user_folder = known_path(platform::known_folder::app_data).combine("dictionaries");
	_custom_dic_path = _user_folder.combine_file("custom.dic");
}

// The per-user copy wins, so a downloaded dictionary supersedes a shipped one of the same name.
df::file_path spell_check::find_dictionary(const std::string_view name, const std::string_view ext) const
{
	const auto user_path = _user_folder.combine_file_ext(name, ext);
	return user_path.exists() ? user_path : _shipped_folder.combine_file_ext(name, ext);
}

// Created on demand rather than at construction, so a user who never adds a word or downloads a
// dictionary gets no empty folder.
bool spell_check::ensure_user_folder() const
{
	return _user_folder.exists() || platform::create_folder(_user_folder).success();
}

spell_check::~spell_check()
{
	platform::exclusive_lock lock(_rw);
	_hunspell.reset();
}

static void download_dic(df::async_i& async, const df::file_path path)
{
	// example https://diffractor.com/static/dictionaries/en_GB.aff

	const auto temp_path = platform::temp_file(path.extension());

	platform::web_request req;
	req.path = std::format("/static/dictionaries/{}", path.name());
	req.download_file_path = temp_path;

	async.queue_async(async_queue::web, [req, path, temp_path]
	{
		const auto con = platform::connect_to_host("diffractor.com");
		const auto response = send_request(con, req);

		if (response.status_code == 200)
		{
			platform::move_file(temp_path, path, true);
		}
	});
}

void spell_check::lazy_download(df::async_i& async) const
{
	const std::unordered_set<std::string_view, df::ihash, df::ieq> known_dics =
	{
		"bg_BG",
		"ca_ES",
		"cs_CZ",
		"cy_GB",
		"da_DK",
		"de_DE",
		"el_GR",
		"en_AU",
		"en_CA",
		"en_GB",
		"en_US",
		"es_ES",
		"et_EE",
		"fa_IR",
		"fr_FR",
		"he_IL",
		"hi_IN",
		"hr_HR",
		"hu_HU",
		"id_ID",
		"it_IT",
		"ja_JP",
		"lt_LT",
		"lv_LV",
		"nb_NO",
		"nl_NL",
		"pl_PL",
		"pt_BR",
		"pt_PT",
		"ro_RO",
		"ru_RU",
		"sk_SK",
		"sl_SI",
		"sv_SE",
		"ta_IN",
		"tg_TJ",
		"uk_UA",
		"vi_VN",
	};

	const auto language = platform::user_language();

	if (known_dics.contains(language))
	{
		const auto aff = find_dictionary(language, ".aff");
		const auto dic = find_dictionary(language, ".dic");

		if ((!aff.exists() || !dic.exists()) && ensure_user_folder())
		{
			if (!aff.exists()) download_dic(async, _user_folder.combine_file_ext(language, ".aff"s));
			if (!dic.exists()) download_dic(async, _user_folder.combine_file_ext(language, ".dic"s));
		}
	}
}


void spell_check::lazy_load()
{
	platform::exclusive_lock lock(_rw);

	if (!_hunspell)
	{
		try
		{
			const auto language = platform::user_language();
			auto aff_path = find_dictionary(language, ".aff");
			auto dic_path = find_dictionary(language, ".dic");
			const auto custom_path = _custom_dic_path;

			if (!aff_path.exists())
			{
				aff_path = find_dictionary("en_US", ".aff");
				dic_path = find_dictionary("en_US", ".dic");
			}

			if (aff_path.exists())
			{
				_hunspell = std::make_unique<Hunspell>(str::utf8_to_a(aff_path.str()).c_str(),
				                                       str::utf8_to_a(dic_path.str()).c_str());

				// Load custom dictionary with proper RAII
				std::ifstream f(str::utf8_to_utf16(custom_path.str()));

				if (f.is_open())
				{
					std::string line;

					while (std::getline(f, line))
					{
						if (!line.empty()) // Added validation to skip empty lines
						{
							_hunspell->add(str::utf8_cast2(line));
						}
					}
					// f.close() is called automatically by destructor
				}
			}
		}
		catch (const std::exception& e)
		{
			// If Hunspell construction fails, ensure _hunspell remains nullptr
			_hunspell.reset();
			df::log(__FUNCTION__, std::format("failed to load dictionary: {}", e.what()));
		}
	}
}

bool spell_check::is_word_valid(const std::string_view word) const
{
	// Add input validation
	if (word.empty())
		return true;

	platform::shared_lock lock(_rw);
	if (!_hunspell) return true;
	return _hunspell->spell(str::utf8_cast2(word));
}

std::vector<std::string> spell_check::suggest(const std::string_view word) const
{
	// Add input validation
	if (word.empty())
		return {};

	platform::shared_lock lock(_rw);
	if (!_hunspell) return {};

	const auto suggestions = _hunspell->suggest(str::utf8_cast2(word));

	std::vector<std::string> result;
	result.reserve(suggestions.size()); // Reserve space for better performance
	std::transform(suggestions.begin(),
	               suggestions.end(),
	               std::back_inserter(result),
	               [](const std::string& s) { return str::utf8_cast2(s); });

	return result;
}

void spell_check::add_word(const std::string_view word) const
{
	// Add input validation
	if (word.empty())
		return;

	platform::exclusive_lock lock(_rw);

	if (_hunspell)
	{
		_hunspell->add(str::utf8_cast2(word));

		// Improved file writing with better error handling and RAII
		try
		{
			if (!ensure_user_folder())
			{
				df::log(__FUNCTION__, std::format("could not create {}", _user_folder));
				return;
			}

			std::ofstream f(platform::to_file_system_path(_custom_dic_path), std::ios::out | std::ios::app);
			if (f.is_open())
			{
				f << word << '\n';
				f.flush(); // Ensure data is written
				// f.close() is called automatically by destructor
			}
			else
			{
				df::log(__FUNCTION__, std::format("could not open {}", _custom_dic_path));
			}
		}
		catch (const std::exception& e)
		{
			// the word stays in the runtime dictionary even when the file write fails
			df::log(__FUNCTION__, std::format("failed to persist custom word: {}", e.what()));
		}
	}
}
