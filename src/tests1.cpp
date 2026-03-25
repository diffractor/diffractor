// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Regression tests for reported issues. Each test targets a specific
// GitHub issue to verify the fix and prevent regressions.

#include "pch.h"
#include "test_utils.h"
#include "model_tokenizer.h"
#include "app_util.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
// Issue #197 - Natural sort order broken for numbers > 99
// Files like "43_100" should sort after "43_99", not between "43_10" and "43_11".
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_natural_sort_large_numbers()
{
	// Core bug: "43_100" sorted between "43_10" and "43_11" (lexicographic instead of numeric)
	assert_equal(true, str::icmp_natural(u8"43_10"sv, u8"43_100"sv) < 0, u8"43_10 < 43_100"sv);
	assert_equal(true, str::icmp_natural(u8"43_100"sv, u8"43_11"sv) > 0, u8"43_100 > 43_11"sv);
	assert_equal(true, str::icmp_natural(u8"43_99"sv, u8"43_100"sv) < 0, u8"43_99 < 43_100"sv);
	assert_equal(true, str::icmp_natural(u8"43_100"sv, u8"43_101"sv) < 0, u8"43_100 < 43_101"sv);

	// Verify natural order for a sequence
	assert_equal(true, str::icmp_natural(u8"file1"sv, u8"file2"sv) < 0, u8"file1 < file2"sv);
	assert_equal(true, str::icmp_natural(u8"file2"sv, u8"file10"sv) < 0, u8"file2 < file10"sv);
	assert_equal(true, str::icmp_natural(u8"file9"sv, u8"file10"sv) < 0, u8"file9 < file10"sv);
	assert_equal(true, str::icmp_natural(u8"file10"sv, u8"file100"sv) < 0, u8"file10 < file100"sv);
	assert_equal(true, str::icmp_natural(u8"file99"sv, u8"file100"sv) < 0, u8"file99 < file100"sv);
	assert_equal(true, str::icmp_natural(u8"file100"sv, u8"file1000"sv) < 0, u8"file100 < file1000"sv);

	// Equal values
	assert_equal(0, str::icmp_natural(u8"file100"sv, u8"file100"sv), u8"file100 == file100"sv);

	// Case insensitivity
	assert_equal(0, str::icmp_natural(u8"File100"sv, u8"file100"sv), u8"File100 == file100"sv);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Issue #203 - Search broken with Russian letter "Х" (U+0425)
// Cyrillic characters must be properly case-folded for case-insensitive comparison.
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_handle_cyrillic_case_folding()
{
	// Cyrillic uppercase Х (U+0425) should lowercase to х (U+0445)
	constexpr auto upper_ha = u8"\u0425"sv; // Х
	constexpr auto lower_ha = u8"\u0445"sv; // х

	// to_lower should convert uppercase Cyrillic to lowercase
	assert_equal_strict(lower_ha, str::to_lower(upper_ha), u8"Cyrillic to_lower"sv);

	// Case-insensitive comparison should treat them as equal
	assert_equal(0, str::icmp(upper_ha, lower_ha), u8"Cyrillic icmp"sv);

	// Additional Cyrillic pairs
	assert_equal(0, str::icmp(u8"\u0410"sv, u8"\u0430"sv), u8"А == а"sv); // А/а
	assert_equal(0, str::icmp(u8"\u0411"sv, u8"\u0431"sv), u8"Б == б"sv); // Б/б
	assert_equal(0, str::icmp(u8"\u042F"sv, u8"\u044F"sv), u8"Я == я"sv); // Я/я

	// Mixed Cyrillic text comparison (was failing due to towlower() locale dependency)
	// Test individual characters from "Текст" to isolate failures
	// Use \u escapes to avoid source-file encoding issues
	assert_equal(0, str::icmp(u8"\u0422"sv, u8"\u0442"sv), u8"T == t cyrillic"sv); // Т/т U+0422/U+0442

	// Текст = U+0422 U+0435 U+043A U+0441 U+0442
	// текст = U+0442 U+0435 U+043A U+0441 U+0442
	constexpr auto cyrillic_upper = u8"\u0422\u0435\u043a\u0441\u0442"sv; // Текст
	constexpr auto cyrillic_lower = u8"\u0442\u0435\u043a\u0441\u0442"sv; // текст
	const auto upper_text = str::to_lower(cyrillic_upper);
	assert_equal_strict(cyrillic_lower, upper_text, u8"to_lower cyrillic word"sv);

	assert_equal(0, str::icmp(cyrillic_upper, cyrillic_lower), u8"mixed Cyrillic icmp"sv);
	// МОСКВА = U+041C U+041E U+0421 U+041A U+0412 U+0410
	// москва = U+043C U+043E U+0441 U+043A U+0432 U+0430
	assert_equal(true, str::icmp(u8"\u041c\u041e\u0421\u041a\u0412\u0410"sv,
	                             u8"\u043c\u043e\u0441\u043a\u0432\u0430"sv) == 0, u8"MOSKVA icmp"sv);

	// Latin Extended pairs
	assert_equal(0, str::icmp(u8"\u00C0"sv, u8"\u00E0"sv), u8"A-grave icmp"sv);
	assert_equal(0, str::icmp(u8"\u00D6"sv, u8"\u00F6"sv), u8"O-umlaut icmp"sv);
	assert_equal(0, str::icmp(u8"\u00D8"sv, u8"\u00F8"sv), u8"O-stroke icmp"sv);
}

static void should_search_cyrillic_text()
{
	// Simulate the actual bug: searching for metadata containing Cyrillic Х (U+0425)
	// "Фото с буквой Х в описании" using \u escapes
	constexpr auto description_with_x =
		u8"\u0424\u043e\u0442\u043e \u0441 \u0431\u0443\u043a\u0432\u043e\u0439 \u0425 \u0432 \u043e\u043f\u0438\u0441\u0430\u043d\u0438\u0438"sv;

	// The search should find text containing Cyrillic Х
	const auto search = df::search_t::parse(u8"\u0425"sv);
	const df::search_matcher matcher(search);

	df::index_file_item info;
	info.ft = files::file_type_from_name(u8"test.jpg"sv);
	info.safe_ps()->description = str::cache(description_with_x);

	assert_equal(true, matcher.match_item({}, info).is_match(), u8"Cyrillic X search"sv);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Issue #209 - Unable to remove tags by any means
// Tags removed via tag_set::remove() should actually be deleted.
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_remove_tags()
{
	tag_set tags(u8"cat dog bird fish"sv);
	assert_equal(4, static_cast<int>(tags.size()), u8"initial tag count"sv);

	// Remove a single tag
	tag_set to_remove(u8"dog"sv);
	tags.remove(to_remove);
	assert_equal(3, static_cast<int>(tags.size()), u8"after removing dog"sv);

	// Verify removed tag is gone and others remain
	const auto result = tags.to_string();
	assert_equal(true, result.find(u8"dog") == std::u8string::npos, u8"dog should be gone"sv);
	assert_equal(true, result.find(u8"cat") != std::u8string::npos, u8"cat should remain"sv);
	assert_equal(true, result.find(u8"bird") != std::u8string::npos, u8"bird should remain"sv);
	assert_equal(true, result.find(u8"fish") != std::u8string::npos, u8"fish should remain"sv);
}

static void should_remove_tags_case_insensitive()
{
	tag_set tags(u8"Cat Dog Bird"sv);
	tag_set to_remove(u8"DOG"sv);
	tags.remove(to_remove);

	assert_equal(2, static_cast<int>(tags.size()), u8"case insensitive remove"sv);
	assert_equal(true, tags.to_string().find(u8"Dog") == std::u8string::npos, u8"Dog gone"sv);
}

static void should_remove_multiple_tags()
{
	tag_set tags(u8"cat dog bird fish"sv);
	tag_set to_remove(u8"dog fish"sv);
	tags.remove(to_remove);

	assert_equal(2, static_cast<int>(tags.size()), u8"multi-remove count"sv);
}

static void should_remove_all_tags()
{
	tag_set tags(u8"cat dog"sv);
	tag_set to_remove(u8"cat dog"sv);
	tags.remove(to_remove);

	assert_equal(true, tags.is_empty(), u8"all tags removed"sv);
}

static void should_remove_nonexistent_tag()
{
	tag_set tags(u8"cat dog"sv);
	tag_set to_remove(u8"elephant"sv);
	tags.remove(to_remove);

	// Should not crash and should leave existing tags untouched
	assert_equal(2, static_cast<int>(tags.size()), u8"remove nonexistent"sv);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Issue #177 - Excluding terms from search excludes everything
// Negated search terms (e.g., "-term" or "!term") should exclude only matching items.
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_negate_search_terms()
{
	// Item with tag "aaa" should NOT match "-#aaa" (negated tag search)
	prop_test().tag(u8"aaa"sv)
	           .is_not_match(u8"-#aaa"sv)
	           .is_not_match(u8"!#aaa"sv);

	// Item with tag "aaa" SHOULD match "-#bbb" (negated search for a different tag)
	prop_test().tag(u8"aaa"sv)
	           .is_match(u8"-#bbb"sv)
	           .is_match(u8"!#bbb"sv);
}

static void should_negate_text_search()
{
	// Item with description "hello world" should NOT match when "hello" is negated
	prop_test().desc(u8"hello world"sv)
	           .is_not_match(u8"-hello"sv);

	// Item with description "hello world" SHOULD match when "goodbye" is negated
	prop_test().desc(u8"hello world"sv)
	           .is_match(u8"-goodbye"sv);
}

static void should_combine_positive_and_negative_terms()
{
	// Positive + negative: find items with "aaa" but NOT "bbb"
	prop_test().tag(u8"aaa bbb"sv)
	           .is_not_match(u8"#aaa !#bbb"sv);

	prop_test().tag(u8"aaa"sv)
	           .is_match(u8"#aaa !#bbb"sv);

	prop_test().tag(u8"bbb"sv)
	           .is_not_match(u8"#aaa !#bbb"sv);
}

static void should_negate_rating_search()
{
	// Item with rating 5 should NOT match "-rate:5"
	prop_test().rate(5)
	           .is_not_match(u8"-rate:5"sv);

	// Item with rating 3 SHOULD match "-rate:5" (it's not rating 5)
	prop_test().rate(3)
	           .is_match(u8"-rate:5"sv);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Issue #174 - Folders starting with '.' can't be excluded with '-'
// Wildcard patterns like ".*" should match folder names starting with a dot.
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_exclude_dot_folders_with_wildcard()
{
	df::index_roots roots;
	roots.exclude_wildcards.emplace(u8".*"_c); // pattern to match any dot-prefixed folder

	// Folder names starting with '.' should be excluded
	assert_equal(true, df::is_excluded(roots, df::folder_path(u8"c:\\photos\\.git"sv)),
	             u8".git excluded"sv);
	assert_equal(true, df::is_excluded(roots, df::folder_path(u8"c:\\photos\\.thumbnails"sv)),
	             u8".thumbnails excluded"sv);
	assert_equal(true, df::is_excluded(roots, df::folder_path(u8"c:\\photos\\.hidden"sv)),
	             u8".hidden excluded"sv);

	// Folder names NOT starting with '.' should NOT be excluded
	assert_equal(false, df::is_excluded(roots, df::folder_path(u8"c:\\photos\\vacation"sv)),
	             u8"vacation not excluded"sv);
	assert_equal(false, df::is_excluded(roots, df::folder_path(u8"c:\\photos\\2024"sv)),
	             u8"2024 not excluded"sv);
}

static void should_exclude_specific_folder_name()
{
	df::index_roots roots;
	roots.exclude_wildcards.emplace(u8"Proxy"_c);

	assert_equal(true, df::is_excluded(roots, df::folder_path(u8"c:\\photos\\Proxy"sv)),
	             u8"Proxy excluded"sv);
	assert_equal(true, df::is_excluded(roots, df::folder_path(u8"c:\\videos\\Proxy"sv)),
	             u8"Proxy in different parent excluded"sv);
	assert_equal(false, df::is_excluded(roots, df::folder_path(u8"c:\\photos\\originals"sv)),
	             u8"originals not excluded"sv);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Registration
///////////////////////////////////////////////////////////////////////////////////////////////////

void register_tests1(view_state& state, test_registry& tests)
{
	//
	// Issue #197 - Natural sort for large numbers
	//
	tests.add(u8"Issue #197: Should natural sort numbers > 99"s, should_natural_sort_large_numbers);

	//
	// Issue #203 - Cyrillic character search
	//
	tests.add(u8"Issue #203: Should handle Cyrillic case folding"s, should_handle_cyrillic_case_folding);
	tests.add(u8"Issue #203: Should search Cyrillic text"s, should_search_cyrillic_text);

	//
	// Issue #209 - Tag removal
	//
	tests.add(u8"Issue #209: Should remove tags"s, should_remove_tags);
	tests.add(u8"Issue #209: Should remove tags case insensitive"s, should_remove_tags_case_insensitive);
	tests.add(u8"Issue #209: Should remove multiple tags"s, should_remove_multiple_tags);
	tests.add(u8"Issue #209: Should remove all tags"s, should_remove_all_tags);
	tests.add(u8"Issue #209: Should remove nonexistent tag"s, should_remove_nonexistent_tag);

	//
	// Issue #177 - Search term negation
	//
	tests.add(u8"Issue #177: Should negate search terms"s, should_negate_search_terms);
	tests.add(u8"Issue #177: Should negate text search"s, should_negate_text_search);
	tests.add(u8"Issue #177: Should combine positive and negative terms"s, should_combine_positive_and_negative_terms);
	tests.add(u8"Issue #177: Should negate rating search"s, should_negate_rating_search);

	//
	// Issue #174 - Exclude dot folders
	//
	tests.add(u8"Issue #174: Should exclude dot folders with wildcard"s, should_exclude_dot_folders_with_wildcard);
	tests.add(u8"Issue #174: Should exclude specific folder name"s, should_exclude_specific_folder_name);
}
