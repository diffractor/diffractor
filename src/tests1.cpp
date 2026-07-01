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

#include <dwrite.h>
#include <wrl/client.h>
#pragma comment(lib, "dwrite")

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
// Issue #175 - Sidebar history chart doesn't show the whole collection
// The date histogram must record files older than 10 years so the (now
// user-configurable) chart can display the full collection span.
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_record_history_beyond_ten_years()
{
	const auto this_year = platform::now().year();
	location_cache locations;
	index_histograms h;

	const auto record_created = [&](const int years_ago, const int month)
	{
		df::index_file_item f;
		f.ft = files::file_type_from_name("test.jpg");
		f.file_created = df::date_t(this_year - years_ago, month, 1);
		f.file_modified = df::date_t(this_year - years_ago, month, 1);
		h.record(locations, f);
	};

	record_created(0, 3); // this year, March
	record_created(9, 6); // within the old 10-year window
	record_created(15, 6); // BEYOND the old 10-year cap - previously dropped
	record_created(40, 1); // decades back, still within max_history_years

	assert_equal(1, h._dates.dates[0 * 12 + (3 - 1)].created, "this year recorded");
	assert_equal(1, h._dates.dates[9 * 12 + (6 - 1)].created, "9-year-old recorded");
	assert_equal(1, h._dates.dates[15 * 12 + (6 - 1)].created, "15-year-old recorded (beyond old cap)");
	assert_equal(1, h._dates.dates[40 * 12 + (1 - 1)].created, "40-year-old recorded");

	// The storage capacity must exceed the old hard-coded 10-year limit.
	assert_equal(true, df::max_history_years > 10, "history capacity beyond 10 years");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Issue #219 - Korean tags are not working
// Korean (Hangul) tags must round-trip through tag parsing and be searchable.
// Hangul has no letter case, so case-folding must leave it unchanged.
///////////////////////////////////////////////////////////////////////////////////////////////////

// Hangul samples (via \u escapes to avoid source-encoding issues):
//   family = \uAC00\uC871 (가족), travel = \uC5EC\uD589 (여행),
//   photo  = \uC0AC\uC9C4 (사진), Seoul  = \uC11C\uC6B8 (서울)

static void should_case_fold_korean()
{
	// Hangul has no case - normalisation/lowercasing must be identity.
	constexpr auto family = "\uAC00\uC871"; // 가족
	assert_equal_strict(family, str::to_lower(family), "Korean to_lower identity");
	assert_equal(0, str::icmp(family, family), "Korean icmp equal");

	// A single Hangul syllable normalises to itself for comparison.
	assert_equal(static_cast<int>(0xAC00), str::normalze_for_compare(0xAC00), "Hangul normalise identity");

	// Different Korean words must not compare equal.
	assert_equal(true, str::icmp(family, "\uC5EC\uD589") != 0, "different Korean words differ");
}

static void should_parse_korean_tags()
{
	constexpr auto family = "\uAC00\uC871"; // 가족
	constexpr auto travel = "\uC5EC\uD589"; // 여행
	constexpr auto photo = "\uC0AC\uC9C4"; // 사진

	// Space-separated Korean tags split into individual tags.
	tag_set tags(std::format("{} {} {}", family, travel, photo));
	assert_equal(3, static_cast<int>(tags.size()), "korean tag count");

	// Removing one Korean tag leaves the others intact (case-insensitive path).
	tags.remove(tag_set(travel));
	assert_equal(2, static_cast<int>(tags.size()), "korean tag count after remove");
	assert_equal(true, tags.to_string().find(travel) == std::string::npos, "travel tag removed");
	assert_equal(true, tags.to_string().find(family) != std::string::npos, "family tag remains");
	assert_equal(true, tags.to_string().find(photo) != std::string::npos, "photo tag remains");
}

static void should_search_korean_tags()
{
	constexpr auto family = "\uAC00\uC871"; // 가족
	constexpr auto travel = "\uC5EC\uD589"; // 여행
	constexpr auto photo = "\uC0AC\uC9C4"; // 사진

	// A file tagged with Korean words matches a #tag search for those words.
	prop_test().tag(std::format("{} {}", family, travel))
	           .is_match(std::format("#{}", family))
	           .is_match(std::format("#{}", travel))
	           .is_not_match(std::format("#{}", photo));

	// Bare (non-scoped) text search across metadata finds the Korean tag too.
	prop_test().tag(family)
	           .is_match(std::string(family));
}

static void should_search_korean_description()
{
	// Korean text embedded in a description is found by a substring search,
	// mirroring the Cyrillic #203 scenario for Hangul.
	// "서울에서 찍은 사진" (photo taken in Seoul)
	constexpr auto description = "\uC11C\uC6B8\uC5D0\uC11C \uCC0D\uC740 \uC0AC\uC9C4";

	const auto search = df::search_t::parse("\uC11C\uC6B8"); // 서울 (Seoul)
	const df::search_matcher matcher(search);

	df::index_file_item info;
	info.ft = files::file_type_from_name("test.jpg");
	info.safe_ps()->description = str::cache(description);

	assert_equal(true, matcher.match_item({}, info).is_match(), "Korean description search");
}

// Issue #219 - Font glyph fallback for missing (Hangul) glyphs.
// The custom UI renders text with the system font "Calibri" (factories::font_face),
// which has NO Hangul. DirectWrite substitutes a fallback face (e.g. Malgun Gothic)
// for Korean. This test verifies the fallback path resolves Korean glyphs, and
// guards the render_glyph fix: metrics for a glyph must be read from the glyph
// run's OWN face (glyph_run->fontFace), not the primary UI font (_face). Reading
// them from the primary face returns a different glyph's metrics (or fails for an
// out-of-range index, dropping the glyph). render_glyph now queries glyph_face.
static void should_fall_back_for_missing_glyphs()
{
	using Microsoft::WRL::ComPtr;

	ComPtr<IDWriteFactory> factory;
	const auto hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
	                                    reinterpret_cast<IUnknown**>(factory.GetAddressOf()));
	assert_equal(true, SUCCEEDED(hr), "DWriteCreateFactory");
	if (!factory) return;

	ComPtr<IDWriteFontCollection> sys;
	factory->GetSystemFontCollection(sys.GetAddressOf());
	if (!sys) return;

	const auto make_face = [&](const wchar_t* name) -> ComPtr<IDWriteFontFace>
	{
		ComPtr<IDWriteFontFace> face;
		uint32_t idx = 0;
		BOOL exists = FALSE;
		if (SUCCEEDED(sys->FindFamilyName(name, &idx, &exists)) && exists)
		{
			ComPtr<IDWriteFontFamily> family;
			ComPtr<IDWriteFont> font;
			if (SUCCEEDED(sys->GetFontFamily(idx, family.GetAddressOf())) &&
				SUCCEEDED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
				                                       DWRITE_FONT_STYLE_NORMAL, font.GetAddressOf())))
			{
				font->CreateFontFace(face.GetAddressOf());
			}
		}
		return face;
	};

	const auto primary = make_face(L"Calibri"); // the app's dialog/UI font
	const auto fallback = make_face(L"Malgun Gothic"); // Windows Korean font
	if (!primary || !fallback) return; // fonts not installed on this machine - skip

	// 1. The primary UI font genuinely lacks Hangul -> fallback is mandatory.
	const uint32_t hangul = 0xAC00; // 가
	uint16_t primary_glyph = 0xFFFF;
	primary->GetGlyphIndices(&hangul, 1, &primary_glyph);
	assert_equal(0, static_cast<int>(primary_glyph), "Calibri has no Hangul glyph (fallback required)");

	// 2. The fallback face maps the same character to a real glyph, and querying
	//    that face for the glyph (what render_glyph SHOULD do) succeeds.
	uint16_t fallback_glyph = 0;
	fallback->GetGlyphIndices(&hangul, 1, &fallback_glyph);
	assert_equal(true, fallback_glyph != 0, "fallback face has a Hangul glyph");

	DWRITE_GLYPH_METRICS gm_right{};
	const auto right_hr = fallback->GetDesignGlyphMetrics(&fallback_glyph, 1, &gm_right);
	assert_equal(true, SUCCEEDED(right_hr), "fallback-face metrics query succeeds (correct face)");

	// 3. Querying the PRIMARY face for the same (fallback) glyph index yields the
	//    wrong glyph's metrics - the latent render_glyph bug.
	DWRITE_GLYPH_METRICS gm_wrong{};
	const auto wrong_hr = primary->GetDesignGlyphMetrics(&fallback_glyph, 1, &gm_wrong);
	const bool wrong_is_broken = FAILED(wrong_hr) ||
		gm_wrong.advanceWidth != gm_right.advanceWidth ||
		gm_wrong.verticalOriginY != gm_right.verticalOriginY;
	assert_equal(true, wrong_is_broken, "primary-face metrics for a fallback glyph are wrong (latent bug)");
}

// Issue #219 - Korean tags stored in a different Unicode normalization form.
// Hangul can be encoded precomposed (NFC: 가 = U+AC00) or decomposed into
// conjoining jamo (NFD: U+1100 U+1161). macOS/Finder, some cameras and apps emit
// NFD; Windows IMEs emit NFC. The two are canonically equivalent and must compare
// and search as equal, otherwise a Korean tag "cannot be found / deleted".
static void should_match_korean_nfc_nfd()
{
	constexpr auto nfc = "\uAC00"; // 가 precomposed syllable
	constexpr auto nfd = "\u1100\u1161"; // 가 decomposed conjoining jamo

	// Canonical equivalence: normalising both forms to NFC yields identical bytes.
	// (Generic str::icmp intentionally stays byte-exact for path safety, so the
	// normalization happens in the search layer, not in icmp.)
	assert_equal(platform::normalize_nfc(nfc), platform::normalize_nfc(nfd), "NFC(nfc) == NFC(nfd)");

	// A tag stored in either form is found by a search in the other form.
	prop_test().tag(nfd).is_match(std::string(nfc));
	prop_test().tag(nfc).is_match(std::string(nfd));
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

// Reproduces the follow-up report on #174: excluding a dot-folder by NAME
// ("-.dtrash") must work the same as excluding it by full path
// ("-c:\path\.dtrash"). This exercises the real parse path used by the
// collection-folders edit box (parse_more_folders), not just is_excluded.
static void should_parse_exclude_dot_folder_by_name()
{
	// Exclude a dot-folder by bare name.
	df::index_roots by_name;
	parse_more_folders(by_name, "-.dtrash");

	assert_equal(0_z, by_name.excludes.size(), "no full-path excludes");
	assert_equal(1_z, by_name.exclude_wildcards.size(), "dot name stored as wildcard");
	assert_equal(".dtrash", *by_name.exclude_wildcards.begin(), "wildcard text preserved");

	assert_equal(true, df::is_excluded(by_name, df::folder_path("c:\\photos\\.dtrash")),
	             ".dtrash excluded by name");
	assert_equal(true, df::is_excluded(by_name, df::folder_path("d:\\other\\sub\\.dtrash")),
	             ".dtrash excluded by name in any parent");
	assert_equal(false, df::is_excluded(by_name, df::folder_path("c:\\photos\\dtrash")),
	             "non-dot folder not excluded");

	// Exclude the same dot-folder by full path - the case the user confirmed works.
	df::index_roots by_path;
	parse_more_folders(by_path, "-c:\\photos\\.dtrash");

	assert_equal(1_z, by_path.excludes.size(), "full path stored as exclude");
	assert_equal(true, df::is_excluded(by_path, df::folder_path("c:\\photos\\.dtrash")),
	             ".dtrash excluded by full path");
}

// A dot-prefixed wildcard ("-.*") entered in the collection box should also
// round-trip through the parser into an exclude wildcard.
static void should_parse_exclude_dot_wildcard()
{
	df::index_roots roots;
	parse_more_folders(roots, "-.*");

	assert_equal(1_z, roots.exclude_wildcards.size(), "dot wildcard stored");
	assert_equal(".*", *roots.exclude_wildcards.begin(), "wildcard text preserved");

	assert_equal(true, df::is_excluded(roots, df::folder_path("c:\\photos\\.git")),
	             ".git excluded by .* wildcard");
	assert_equal(false, df::is_excluded(roots, df::folder_path("c:\\photos\\normal")),
	             "normal folder not excluded by .* wildcard");
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
	tests.add("Issue #174: Should parse -.dtrash exclude by name"s, should_parse_exclude_dot_folder_by_name);
	tests.add("Issue #174: Should parse -.* exclude wildcard"s, should_parse_exclude_dot_wildcard);

	//
	// Issue #219 - Korean tags
	//
	tests.add("Issue #219: Should case-fold Korean"s, should_case_fold_korean);
	tests.add("Issue #219: Should parse Korean tags"s, should_parse_korean_tags);
	tests.add("Issue #219: Should search Korean tags"s, should_search_korean_tags);
	tests.add("Issue #219: Should search Korean description"s, should_search_korean_description);
	tests.add("Issue #219: Should fall back for missing glyphs"s, should_fall_back_for_missing_glyphs);
	tests.add("Issue #219: Should match Korean NFC and NFD"s, should_match_korean_nfc_nfd);

	//
	// Issue #175 - Sidebar history chart span
	//
	tests.add("Issue #175: Should record history beyond ten years"s, should_record_history_beyond_ten_years);
}
