// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Tests for text and localization (app_text, util_spell, po catalogs) -- translated month names, po catalog loading and plural form selection.

#include "pch.h"
#include "test.h"
#include "test_fixtures.h"
#include "test_runner.h"
#include "app_text.h"
#include "util_spell.h"

static void should_parse_translated_short_month()
{
	// Shipped catalogs carry short months that are not 3 bytes - ru/uk/ja/ko/zh for all 12, fr for 8.
	const auto saved_oct = tt.month_short_oct.trans;
	const auto saved_jan = tt.month_short_jan.trans;

	tt.month_short_oct.trans = "\xd0\xbe\xd0\xba\xd1\x82"; // ru, 6 bytes
	tt.month_short_jan.trans = "janv"; // fr, 4 bytes

	const auto oct = str::month("\xd0\xbe\xd0\xba\xd1\x82");
	const auto jan = str::month("JANV");

	tt.month_short_oct.trans = saved_oct;
	tt.month_short_jan.trans = saved_jan;

	assert_equal(10, oct, "6 byte translated short month");
	assert_equal(1, jan, "4 byte translated short month, case insensitive");
	assert_equal(3, str::month("mar"), "ascii short month");
	assert_equal(5, str::month("May"), "long month");
	assert_equal(0, str::month("nope"), "non month");
}

static void should_format_plural_text()
{
	const plural_text items_fmt("{count} item", "{count} items");

	assert_equal("1 item", format_plural_text(items_fmt, 1), "singular");
	assert_equal("5 items", format_plural_text(items_fmt, 5), "plural");
	assert_equal("0 items", format_plural_text(items_fmt, 0), "zero");

	const plural_text name_fmt("{first-name} will be processed.",
	                           "{count} items including {first-name} will be processed.");

	assert_equal("photo.jpg will be processed.", format_plural_text(name_fmt, "photo.jpg", 1, {}), "named singular");
	assert_equal("3 items including photo.jpg will be processed.", format_plural_text(name_fmt, "photo.jpg", 3, {}),
	             "named plural");
}

static void should_load_po()
{
	const auto app_folder = known_path(platform::known_folder::running_app_folder);
	const auto lang_folder = app_folder.combine("languages");
	const auto lang_path = lang_folder.combine_file("de.po");

	const auto po_entries = load_po(lang_path);

	app_text_t t;
	t.load_lang(lang_path.name(), po_entries);

	assert_equal("Datenbank bereinigen und neu indexieren.\nAlle Daten werden regeneriert.", t.reset_database,
	             "reset_database");
}

static void should_select_slavic_plural_forms()
{
	// Czech (and Polish, Russian, Ukrainian) declare a third plural form:
	// msgstr[2]. load_po must capture it, and plural_form must select it for
	// "many" counts while keeping the binary behavior for other languages.
	const auto path = _temps.next_path(".po");

	{
		std::ofstream fs(platform::to_stream_path(path));
		fs << "msgid \"one apple\"\n";
		fs << "msgid_plural \"{count} apples\"\n";
		fs << "msgstr[0] \"jedno jablko\"\n";
		fs << "msgstr[1] \"{count} jablka\"\n";
		fs << "msgstr[2] \"{count} jablek\"\n";
	}

	const auto po_entries = load_po(path);

	assert_equal(1, static_cast<int>(po_entries.size()), "entry count");
	assert_equal("jedno jablko", po_entries.front().str, "msgstr[0]");
	assert_equal("{count} jablka", po_entries.front().str_plural, "msgstr[1]");
	assert_equal(1, static_cast<int>(po_entries.front().str_extra.size()), "extra form count");
	assert_equal("{count} jablek", po_entries.front().str_extra.front(), "msgstr[2] captured");

	// Czech uses three forms: one (1), few (2-4), many (0, 5+, ...).
	app_text_t cs;
	cs.load_lang("cs.po", po_entries);
	assert_equal(0, cs.plural_form(1), "cs form for 1");
	assert_equal(1, cs.plural_form(2), "cs form for 2");
	assert_equal(1, cs.plural_form(4), "cs form for 4");
	assert_equal(2, cs.plural_form(5), "cs form for 5");
	assert_equal(2, cs.plural_form(11), "cs form for 11");

	// Russian shares three forms but its form 0 also covers 21, 31, ...; those
	// are clamped to the plural form so the literal-"1" singular is never reused.
	app_text_t ru;
	ru.load_lang("ru.po", po_entries);
	assert_equal(0, ru.plural_form(1), "ru form for 1");
	assert_equal(1, ru.plural_form(2), "ru form for 2");
	assert_equal(2, ru.plural_form(5), "ru form for 5");
	assert_equal(1, ru.plural_form(21), "ru form for 21 clamped");

	// Unlisted languages keep the binary one/plural behavior.
	app_text_t de;
	de.load_lang("de.po", po_entries);
	assert_equal(0, de.plural_form(1), "de form for 1");
	assert_equal(1, de.plural_form(2), "de form for 2");
	assert_equal(1, de.plural_form(5), "de form for 5");

	cs.title_item_count_fmt.extra_forms.emplace_back("{count} polozek");
	cs.clear();
	assert_equal(1, cs.plural_form(5), "clear restores binary plural rule");
	assert_equal(0, static_cast<int>(cs.title_item_count_fmt.extra_forms.size()), "clear drops extra plural forms");
}

// The spell checker only reaches the user through metadata field editing, and it fails soft: a
// missing dictionary must leave every word "valid" rather than underlining the whole caption. Both
// halves are pinned here because a broken load looks exactly like a clean one from the caller.
static void should_check_spelling()
{
	auto& checker = spell();
	checker.lazy_load();

	const auto dictionary_present = known_path(platform::known_folder::running_app_folder)
	                                .combine("dictionaries").combine_file("en_US.dic").exists();

	assert_equal(true, checker.is_word_valid("photograph"), "a dictionary word is valid");

	if (!dictionary_present)
	{
		// Without a dictionary nothing may be reported wrong; that is the fail-soft contract.
		assert_equal(true, checker.is_word_valid("qwertyuiopasdfgh"), "no dictionary means no misspelling");
		return;
	}

	assert_equal(false, checker.is_word_valid("qwertyuiopasdfgh"), "a nonsense word is not valid");

	const auto suggestions = checker.suggest("photograpg");
	assert_equal(true, !suggestions.empty(), "a near miss produces suggestions");

	// Case and punctuation reach the checker straight from a caption field.
	assert_equal(true, checker.is_word_valid("Photograph"), "capitalisation is accepted");
	assert_equal(true, checker.is_word_valid("photographs"), "an inflected form is accepted");

	// Passing at all proves the read falls back to the shipped folder: en_US is only ever installed
	// beside the executable, never in the per-user folder writes go to.
	// add_word is deliberately NOT exercised: it appends to a real dictionary the user owns.
}

// The custom dictionary has to land somewhere the user can write. It used to be placed beside the
// executable whenever that folder existed - which it always does, because en_US ships there - so on
// a Store install "Add to dictionary" and any dictionary download failed silently and the word was
// lost at restart.
static void should_keep_the_custom_dictionary_where_it_can_be_written()
{
	const auto custom = spell().custom_dictionary_path();
	const auto install_folder = known_path(platform::known_folder::running_app_folder).combine("dictionaries");
	const auto user_folder = known_path(platform::known_folder::app_data).combine("dictionaries");

	assert_equal("custom.dic", custom.name(), "the custom dictionary keeps its name");
	assert_equal(user_folder.text(), custom.folder().text(), "the custom dictionary lives in the per-user folder");
	assert_not_equal(install_folder.text(), custom.folder().text(),
	                 "the custom dictionary is not written into the install folder");

	// The shipped dictionary is still readable from where it actually is.
	assert_equal(true, install_folder.combine_file("en_US.aff").exists(),
	             "the shipped dictionary is where the read fallback looks");
}

void register_text_tests(view_state& state, test_registry& tests)
{
	//
	// Formatting
	//
	tests.add("Should parse translated short month"s, should_parse_translated_short_month);
	tests.add("Should format plural text"s, should_format_plural_text);

	//
	// Catalogs
	//
	tests.add("Should load po"s, should_load_po);
	tests.add("Should select Slavic plural forms"s, should_select_slavic_plural_forms);

	//
	// Spell checking
	//
	tests.add("Should check spelling"s, should_check_spelling);
	tests.add("Should keep the custom dictionary where it can be written"s,
	          should_keep_the_custom_dictionary_where_it_can_be_written);
}
