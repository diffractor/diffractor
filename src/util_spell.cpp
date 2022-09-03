// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "pch.h"
#include "util_spell.h"

#define HUNSPELL_STATIC
#include <hunspell.hxx>

spell_check spell;

spell_check::spell_check()
{
	const auto module_folder = known_path(platform::known_folder::running_app_folder).combine(u8"dictionaries");
	const auto app_data_folder = known_path(platform::known_folder::app_data).combine(u8"dictionaries");

	if (module_folder.exists())
	{
		_dictionaries_folder = module_folder;
	}
	else
	{
		_dictionaries_folder = app_data_folder;

		if (!_dictionaries_folder.exists())
		{
			platform::create_folder(_dictionaries_folder);
		}
	}

	_custom_dic_path = _dictionaries_folder.combine_file(u8"custom.dic");
}

spell_check::~spell_check()
{
	platform::exclusive_lock lock(_mutex);
	_hunspell.reset();
}

static void download_dic(df::async_i& async, const df::file_path path)
{
	// example https://diffractor.com/static/dictionaries/en_GB.aff

	const auto temp_path = platform::temp_file(path.extension());

	platform::web_request req;
	req.host = u8"diffractor.com";
	req.path = format(u8"/static/dictionaries/{}", path.name());
	req.download_file_path = temp_path;

	async.queue_async(async_queue::web, [req, path, temp_path]()
	{
		const auto response = send_request(req);

		if (response.status_code == 200)
		{
			platform::move_file(temp_path, path, true);
		}
	});
}

void spell_check::lazy_download(df::async_i& async)
{
	const std::unordered_set<std::u8string_view, df::ihash, df::ieq> known_dics =
	{
		u8"bg_BG",
		u8"ca_ES",
		u8"cs_CZ",
		u8"cy_GB",
		u8"da_DK",
		u8"de_DE",
		u8"el_GR",
		u8"en_AU",
		u8"en_CA",
		u8"en_GB",
		u8"en_US",
		u8"es_ES",
		u8"et_EE",
		u8"fa_IR",
		u8"fr_FR",
		u8"he_IL",
		u8"hi_IN",
		u8"hr_HR",
		u8"hu-HU",
		u8"id_ID",
		u8"it_IT",
		u8"lt_LT",
		u8"lv_LV",
		u8"nb_NO",
		u8"nl_NL",
		u8"pl_PL",
		u8"pt_BR",
		u8"pt_PT",
		u8"ro_RO",
		u8"ru_RU",
		u8"sk_SK",
		u8"sl_SI",
		u8"sv_SE",
		u8"ta_IN",
		u8"tg_TG",
		u8"uk_UA",
		u8"vi_VI",
	};

	const auto language = platform::user_language();

	if (known_dics.contains(language))
	{
		const auto aff_path = _dictionaries_folder.combine_file(language + u8".aff");
		const auto dic_path = _dictionaries_folder.combine_file(language + u8".dic");

		if (!aff_path.exists())
		{
			download_dic(async, aff_path);
		}

		if (!dic_path.exists())
		{
			download_dic(async, dic_path);
		}
	}
}


void spell_check::lazy_load()
{
	platform::exclusive_lock lock(_mutex);

	if (!_hunspell)
	{
		const auto language = platform::user_language();
		auto aff_path = _dictionaries_folder.combine_file(language + u8".aff");
		auto dic_path = _dictionaries_folder.combine_file(language + u8".dic");
		const auto custom_path = _dictionaries_folder.combine_file(u8"custom.dic");

		if (!aff_path.exists())
		{
			aff_path = _dictionaries_folder.combine_file(u8"en_US.aff");
			dic_path = _dictionaries_folder.combine_file(u8"en_US.dic");
		}

		if (aff_path.exists())
		{
			_hunspell = std::make_unique<Hunspell>(str::utf8_to_a(aff_path.str()).c_str(),
			                                       str::utf8_to_a(dic_path.str()).c_str());

			u8istream f(str::utf8_to_utf16(custom_path.str()));

			if (f.is_open())
			{
				std::u8string line;

				while (std::getline(f, line))
				{
					_hunspell->add(str::utf8_cast2(line));
				}

				f.close();
			}
		}
	}
}

bool spell_check::is_word_valid(const std::u8string_view word) const
{
	platform::shared_lock lock(_mutex);
	if (!_hunspell) return true;
	return _hunspell->spell(str::utf8_cast2(word));
}

std::vector<std::u8string> spell_check::suggest(const std::u8string_view word) const
{
	platform::shared_lock lock(_mutex);
	if (!_hunspell) return {};
	const auto suggestions = _hunspell->suggest(str::utf8_cast2(word));

	std::vector<std::u8string> result;
	std::transform(suggestions.begin(),
	               suggestions.end(),
	               std::back_inserter(result),
	               [](const std::string& s) { return str::utf8_cast2(s); });

	return result;
}

void spell_check::add_word(const std::u8string_view word)
{
	platform::exclusive_lock lock(_mutex);

	if (_hunspell)
	{
		_hunspell->add(str::utf8_cast2(word));

		u8ostream f(platform::to_file_system_path(_custom_dic_path), std::ios::out | std::ios::app);
		f << word << std::endl;
	}
}
