// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2024  Zac Walker
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
	const auto module_folder = known_path(platform::known_folder::running_app_folder).combine(u8"dictionaries"sv);
	const auto app_data_folder = known_path(platform::known_folder::app_data).combine(u8"dictionaries"sv);

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

	_custom_dic_path = _dictionaries_folder.combine_file(u8"custom.dic"sv);
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
	req.host = u8"diffractor.com"sv;
	req.path = format(u8"/static/dictionaries/{}"sv, path.name());
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
		u8"bg_BG"sv,
		u8"ca_ES"sv,
		u8"cs_CZ"sv,
		u8"cy_GB"sv,
		u8"da_DK"sv,
		u8"de_DE"sv,
		u8"el_GR"sv,
		u8"en_AU"sv,
		u8"en_CA"sv,
		u8"en_GB"sv,
		u8"en_US"sv,
		u8"es_ES"sv,
		u8"et_EE"sv,
		u8"fa_IR"sv,
		u8"fr_FR"sv,
		u8"he_IL"sv,
		u8"hi_IN"sv,
		u8"hr_HR"sv,
		u8"hu-HU"sv,
		u8"id_ID"sv,
		u8"it_IT"sv,
		u8"ja_JP"sv,
		u8"lt_LT"sv,
		u8"lv_LV"sv,
		u8"nb_NO"sv,
		u8"nl_NL"sv,
		u8"pl_PL"sv,
		u8"pt_BR"sv,
		u8"pt_PT"sv,
		u8"ro_RO"sv,
		u8"ru_RU"sv,
		u8"sk_SK"sv,
		u8"sl_SI"sv,
		u8"sv_SE"sv,
		u8"ta_IN"sv,
		u8"tg_TG"sv,
		u8"uk_UA"sv,
		u8"vi_VI"sv,
	};

	const auto language = platform::user_language();

	if (known_dics.contains(language))
	{
		const auto aff_path = _dictionaries_folder.combine_file_ext(language, u8".aff"s);
		const auto dic_path = _dictionaries_folder.combine_file_ext(language, u8".dic"s);

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
	platform::exclusive_lock lock(_rw);

	if (!_hunspell)
	{
		const auto language = platform::user_language();
		auto aff_path = _dictionaries_folder.combine_file_ext(language, u8".aff"sv);
		auto dic_path = _dictionaries_folder.combine_file_ext(language, u8".dic"sv);
		const auto custom_path = _dictionaries_folder.combine_file(u8"custom.dic"sv);

		if (!aff_path.exists())
		{
			aff_path = _dictionaries_folder.combine_file(u8"en_US.aff"sv);
			dic_path = _dictionaries_folder.combine_file(u8"en_US.dic"sv);
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
	platform::shared_lock lock(_rw);
	if (!_hunspell) return true;
	return _hunspell->spell(str::utf8_cast2(word));
}

std::vector<std::u8string> spell_check::suggest(const std::u8string_view word) const
{
	platform::shared_lock lock(_rw);
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
	platform::exclusive_lock lock(_rw);

	if (_hunspell)
	{
		_hunspell->add(str::utf8_cast2(word));

		u8ostream f(platform::to_file_system_path(_custom_dic_path), std::ios::out | std::ios::app);
		f << word << '\n';
	}
}
