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

///////////////////////////////////////////////////////////////////////////////////////////////////
// Issue #197 - Natural sort order broken for numbers > 99
// Files like "43_100" should sort after "43_99", not between "43_10" and "43_11".
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_natural_sort_large_numbers()
{
	// Core bug: "43_100" sorted between "43_10" and "43_11" (lexicographic instead of numeric)
	assert_equal(true, str::icmp_natural("43_10", "43_100") < 0, "43_10 < 43_100");
	assert_equal(true, str::icmp_natural("43_100", "43_11") > 0, "43_100 > 43_11");
	assert_equal(true, str::icmp_natural("43_99", "43_100") < 0, "43_99 < 43_100");
	assert_equal(true, str::icmp_natural("43_100", "43_101") < 0, "43_100 < 43_101");

	// Verify natural order for a sequence
	assert_equal(true, str::icmp_natural("file1", "file2") < 0, "file1 < file2");
	assert_equal(true, str::icmp_natural("file2", "file10") < 0, "file2 < file10");
	assert_equal(true, str::icmp_natural("file9", "file10") < 0, "file9 < file10");
	assert_equal(true, str::icmp_natural("file10", "file100") < 0, "file10 < file100");
	assert_equal(true, str::icmp_natural("file99", "file100") < 0, "file99 < file100");
	assert_equal(true, str::icmp_natural("file100", "file1000") < 0, "file100 < file1000");

	// Equal values
	assert_equal(0, str::icmp_natural("file100", "file100"), "file100 == file100");

	// Case insensitivity
	assert_equal(0, str::icmp_natural("File100", "file100"), "File100 == file100");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Issue #203 - Search broken with Russian letter "Х" (U+0425)
// Cyrillic characters must be properly case-folded for case-insensitive comparison.
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_handle_cyrillic_case_folding()
{
	// Cyrillic uppercase Х (U+0425) should lowercase to х (U+0445)
	constexpr auto upper_ha = "\u0425"; // Х
	constexpr auto lower_ha = "\u0445"; // х

	// to_lower should convert uppercase Cyrillic to lowercase
	assert_equal_strict(lower_ha, str::to_lower(upper_ha), "Cyrillic to_lower");

	// Case-insensitive comparison should treat them as equal
	assert_equal(0, str::icmp(upper_ha, lower_ha), "Cyrillic icmp");

	// Additional Cyrillic pairs
	assert_equal(0, str::icmp("\u0410", "\u0430"), "А == а"); // А/а
	assert_equal(0, str::icmp("\u0411", "\u0431"), "Б == б"); // Б/б
	assert_equal(0, str::icmp("\u042F", "\u044F"), "Я == я"); // Я/я

	// Mixed Cyrillic text comparison (was failing due to towlower() locale dependency)
	// Test individual characters from "Текст" to isolate failures
	// Use \u escapes to avoid source-file encoding issues
	assert_equal(0, str::icmp("\u0422", "\u0442"), "T == t cyrillic"); // Т/т U+0422/U+0442

	// Текст = U+0422 U+0435 U+043A U+0441 U+0442
	// текст = U+0442 U+0435 U+043A U+0441 U+0442
	constexpr auto cyrillic_upper = "\u0422\u0435\u043a\u0441\u0442"; // Текст
	constexpr auto cyrillic_lower = "\u0442\u0435\u043a\u0441\u0442"; // текст
	const auto upper_text = str::to_lower(cyrillic_upper);
	assert_equal_strict(cyrillic_lower, upper_text, "to_lower cyrillic word");

	assert_equal(0, str::icmp(cyrillic_upper, cyrillic_lower), "mixed Cyrillic icmp");
	// МОСКВА = U+041C U+041E U+0421 U+041A U+0412 U+0410
	// москва = U+043C U+043E U+0441 U+043A U+0432 U+0430
	assert_equal(true, str::icmp("\u041c\u041e\u0421\u041a\u0412\u0410",
	                             "\u043c\u043e\u0441\u043a\u0432\u0430") == 0, "MOSKVA icmp");

	// Latin Extended pairs
	assert_equal(0, str::icmp("\u00C0", "\u00E0"), "A-grave icmp");
	assert_equal(0, str::icmp("\u00D6", "\u00F6"), "O-umlaut icmp");
	assert_equal(0, str::icmp("\u00D8", "\u00F8"), "O-stroke icmp");
}

static void should_search_cyrillic_text()
{
	// Simulate the actual bug: searching for metadata containing Cyrillic Х (U+0425)
	// "Фото с буквой Х в описании" using \u escapes
	constexpr auto description_with_x =
		"\u0424\u043e\u0442\u043e \u0441 \u0431\u0443\u043a\u0432\u043e\u0439 \u0425 \u0432 \u043e\u043f\u0438\u0441\u0430\u043d\u0438\u0438";

	// The search should find text containing Cyrillic Х
	const auto search = df::search_t::parse("\u0425");
	const df::search_matcher matcher(search);

	df::index_file_item info;
	info.ft = files::file_type_from_name("test.jpg");
	info.safe_ps()->description = str::cache(description_with_x);

	assert_equal(true, matcher.match_item({}, info).is_match(), "Cyrillic X search");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Issue #209 - Unable to remove tags by any means
// Tags removed via tag_set::remove() should actually be deleted.
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_remove_tags()
{
	tag_set tags("cat dog bird fish");
	assert_equal(4, static_cast<int>(tags.size()), "initial tag count");

	// Remove a single tag
	const tag_set to_remove("dog");
	tags.remove(to_remove);
	assert_equal(3, static_cast<int>(tags.size()), "after removing dog");

	// Verify removed tag is gone and others remain
	const auto result = tags.to_string();
	assert_equal(true, result.find("dog") == std::string::npos, "dog should be gone");
	assert_equal(true, result.find("cat") != std::string::npos, "cat should remain");
	assert_equal(true, result.find("bird") != std::string::npos, "bird should remain");
	assert_equal(true, result.find("fish") != std::string::npos, "fish should remain");
}

static void should_remove_tags_case_insensitive()
{
	tag_set tags("Cat Dog Bird");
	const tag_set to_remove("DOG");
	tags.remove(to_remove);

	assert_equal(2, static_cast<int>(tags.size()), "case insensitive remove");
	assert_equal(true, tags.to_string().find("Dog") == std::string::npos, "Dog gone");
}

static void should_remove_multiple_tags()
{
	tag_set tags("cat dog bird fish");
	const tag_set to_remove("dog fish");
	tags.remove(to_remove);

	assert_equal(2, static_cast<int>(tags.size()), "multi-remove count");
}

static void should_remove_all_tags()
{
	tag_set tags("cat dog");
	const tag_set to_remove("cat dog");
	tags.remove(to_remove);

	assert_equal(true, tags.is_empty(), "all tags removed");
}

static void should_remove_nonexistent_tag()
{
	tag_set tags("cat dog");
	const tag_set to_remove("elephant");
	tags.remove(to_remove);

	// Should not crash and should leave existing tags untouched
	assert_equal(2, static_cast<int>(tags.size()), "remove nonexistent");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Issue #177 - Excluding terms from search excludes everything
// Negated search terms (e.g., "-term" or "!term") should exclude only matching items.
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_negate_search_terms()
{
	// Item with tag "aaa" should NOT match "-#aaa" (negated tag search)
	prop_test().tag("aaa")
	           .is_not_match("-#aaa")
	           .is_not_match("!#aaa");

	// Item with tag "aaa" SHOULD match "-#bbb" (negated search for a different tag)
	prop_test().tag("aaa")
	           .is_match("-#bbb")
	           .is_match("!#bbb");
}

static void should_negate_text_search()
{
	// Item with description "hello world" should NOT match when "hello" is negated
	prop_test().desc("hello world")
	           .is_not_match("-hello");

	// Item with description "hello world" SHOULD match when "goodbye" is negated
	prop_test().desc("hello world")
	           .is_match("-goodbye");
}

static void should_combine_positive_and_negative_terms()
{
	// Positive + negative: find items with "aaa" but NOT "bbb"
	prop_test().tag("aaa bbb")
	           .is_not_match("#aaa !#bbb");

	prop_test().tag("aaa")
	           .is_match("#aaa !#bbb");

	prop_test().tag("bbb")
	           .is_not_match("#aaa !#bbb");
}

static void should_negate_rating_search()
{
	// Item with rating 5 should NOT match "-rate:5"
	prop_test().rate(5)
	           .is_not_match("-rate:5");

	// Item with rating 3 SHOULD match "-rate:5" (it's not rating 5)
	prop_test().rate(3)
	           .is_match("-rate:5");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Issue #174 - Folders starting with '.' can't be excluded with '-'
// Wildcard patterns like ".*" should match folder names starting with a dot.
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_exclude_dot_folders_with_wildcard()
{
	df::index_roots roots;
	roots.exclude_wildcards.emplace(".*"_c); // pattern to match any dot-prefixed folder

	// Folder names starting with '.' should be excluded
	assert_equal(true, df::is_excluded(roots, df::folder_path("c:\\photos\\.git")),
	             ".git excluded");
	assert_equal(true, df::is_excluded(roots, df::folder_path("c:\\photos\\.thumbnails")),
	             ".thumbnails excluded");
	assert_equal(true, df::is_excluded(roots, df::folder_path("c:\\photos\\.hidden")),
	             ".hidden excluded");

	// Folder names NOT starting with '.' should NOT be excluded
	assert_equal(false, df::is_excluded(roots, df::folder_path("c:\\photos\\vacation")),
	             "vacation not excluded");
	assert_equal(false, df::is_excluded(roots, df::folder_path("c:\\photos\\2024")),
	             "2024 not excluded");
}

static void should_exclude_specific_folder_name()
{
	df::index_roots roots;
	roots.exclude_wildcards.emplace("Proxy"_c);

	assert_equal(true, df::is_excluded(roots, df::folder_path("c:\\photos\\Proxy")),
	             "Proxy excluded");
	assert_equal(true, df::is_excluded(roots, df::folder_path("c:\\videos\\Proxy")),
	             "Proxy in different parent excluded");
	assert_equal(false, df::is_excluded(roots, df::folder_path("c:\\photos\\originals")),
	             "originals not excluded");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Registration
///////////////////////////////////////////////////////////////////////////////////////////////////

void register_tests1(view_state& state, test_registry& tests)
{
	//
	// Issue #197 - Natural sort for large numbers
	//
	tests.add("Issue #197: Should natural sort numbers > 99"s, should_natural_sort_large_numbers);

	//
	// Issue #203 - Cyrillic character search
	//
	tests.add("Issue #203: Should handle Cyrillic case folding"s, should_handle_cyrillic_case_folding);
	tests.add("Issue #203: Should search Cyrillic text"s, should_search_cyrillic_text);

	//
	// Issue #209 - Tag removal
	//
	tests.add("Issue #209: Should remove tags"s, should_remove_tags);
	tests.add("Issue #209: Should remove tags case insensitive"s, should_remove_tags_case_insensitive);
	tests.add("Issue #209: Should remove multiple tags"s, should_remove_multiple_tags);
	tests.add("Issue #209: Should remove all tags"s, should_remove_all_tags);
	tests.add("Issue #209: Should remove nonexistent tag"s, should_remove_nonexistent_tag);

	//
	// Issue #177 - Search term negation
	//
	tests.add("Issue #177: Should negate search terms"s, should_negate_search_terms);
	tests.add("Issue #177: Should negate text search"s, should_negate_text_search);
	tests.add("Issue #177: Should combine positive and negative terms"s, should_combine_positive_and_negative_terms);
	tests.add("Issue #177: Should negate rating search"s, should_negate_rating_search);

	//
	// Issue #174 - Exclude dot folders
	//
	tests.add("Issue #174: Should exclude dot folders with wildcard"s, should_exclude_dot_folders_with_wildcard);
	tests.add("Issue #174: Should exclude specific folder name"s, should_exclude_specific_folder_name);
}
