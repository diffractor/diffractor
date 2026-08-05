// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Search tests. Verifies search parsing, term matching, date matching,
// selectors, file selection, search navigation, and location lookups.

#include "pch.h"
#include "test_utils.h"
#include "model_tokenizer.h"
#include "model_visits.h"
#include "app_match.h"

static void should_parse_searches()
{
	const auto date_filter = df::search_t::parse("2012-09-14");
	const df::search_matcher matcher(date_filter);

	df::index_file_item info;
	info.ft = files::file_type_from_name("test.jpg");
	info.safe_ps()->created_exif = df::date_t(2012, 9, 14);
	assert_equal(true, matcher.match_item({}, info).is_match(), "date");

	info.safe_ps()->created_exif = df::date_t(2012, 9, 13);
	assert_equal(false, matcher.match_item({}, info).is_match(), "date");
}

static void should_calculate_safe_presence_requirements()
{
	const auto photo_bit = file_group::photo.search_presence_bit();
	const auto video_bit = file_group::video.search_presence_bit();

	assert_equal(photo_bit, df::search_t::parse("@photo").calc_required_presence().types,
	             "positive media type is required");
	assert_equal(0u, df::search_t::parse("-@photo").calc_required_presence().types,
	             "negated media type cannot be required");
	assert_equal(0u, df::search_t::parse("@photo or @video").calc_required_presence().types,
	             "OR branches have no individually required bit");
	assert_equal(photo_bit | video_bit, df::search_t::parse("@photo @video").calc_required_presence().types,
	             "positive conjunction requires every media bit");
}

static void should_update_duplicate_search_presence()
{
	df::index_item_infos files(1);
	files.front().ft = files::file_type_from_name("test.jpg");
	files.front().calc_search_presence();

	const auto folder = std::make_shared<df::index_folder_item>(std::move(files));
	folder->reset_search_presence();
	const auto duplicate_search = df::search_t::parse("@duplicates");
	const df::search_matcher matcher(duplicate_search);
	const auto& file = folder->files.front();

	assert_equal(false, matcher.can_contain(file.search_presence), "initial item presence");
	assert_equal(false, matcher.can_contain(folder->search_presence_summary), "initial folder presence");

	file.update_duplicates(folder, {.group = 7, .count = 2});
	assert_equal(true, matcher.can_contain(file.search_presence), "added item presence");
	assert_equal(true, matcher.can_contain(folder->search_presence_summary), "added folder presence");
	assert_equal(true, matcher.match_item({}, file).is_match(), "added exact duplicate state");

	file.update_duplicates(folder, {});
	assert_equal(false, matcher.can_contain(file.search_presence), "removed item presence");
	assert_equal(true, matcher.can_contain(folder->search_presence_summary), "safe stale folder presence");
	assert_equal(false, matcher.match_item({}, file).is_match(), "removed exact duplicate state");
}

static void should_search_boolean_presence_terms(shared_test_context& stc)
{
	stc.lazy_load_index();

	const auto photos = count_search_results(stc.test_index, "@photo");
	const auto videos = count_search_results(stc.test_index, "@video");

	// The positive media term keeps these comparisons file-only (a bare negative term
	// can also match folders). These identities verify that the bitmap prefilter does
	// not discard files before the exact Boolean matcher sees them.
	assert_equal(photos, count_search_results(stc.test_index, "@photo -@video"),
	             "negated media search");
	assert_equal(photos + videos, count_search_results(stc.test_index, "@photo or @video"),
	             "OR media search");

	const auto duplicate_photos = count_search_results(stc.test_index, "@photo @duplicates");
	assert_equal(photos - duplicate_photos, count_search_results(stc.test_index, "@photo -@duplicates"),
	             "negated duplicate search");
}

static void should_match_terms()
{
	prop_test().tag("aaa")
	           .is_match("#aaa")
	           .is_not_match("#bbb")
	           .is_match("-#bbb")
	           .is_match("!#bbb")
	           .is_not_match("-#aaa")
	           .is_not_match("!#aaa")
	           .is_match("#aaa or #bbb")
	           .is_not_match("#aaa and #bbb")
	           .is_not_match("#aaa #bbb");

	prop_test().tag("bbb")
	           .is_not_match("#aaa")
	           .is_match("#bbb")
	           .is_match("#aaa or #bbb");

	prop_test().tag("'aa bb'")
	           .is_not_match("#aaa")
	           .is_not_match("#bb")
	           .is_match("#'aa bb'");

	prop_test()
		.tag("aaa bbb")
		.is_match("#aaa")
		.is_match("#bbb")
		.is_match("#aaa or #bbb")
		.is_match("#aaa and #bbb")
		.is_match("#aaa #bbb")
		.is_not_match("#aaa !#bbb");

	prop_test().rate(4)
	           .is_match("rate:4")
	           .is_match("4")
	           .is_match(">= 4")
	           .is_not_match("> 4")
	           .is_match(">3")
	           .is_match("3 | 4")
	           .is_match("3 or 4")
	           .is_not_match("3 and 4");

	prop_test().desc("one two three")
	           .is_match("two")
	           .is_match("one two")
	           .is_match("two three")
	           .is_match("'two three'")
	           .is_not_match("'one three'");

	prop_test().date(1999, 12, 27)
	           .is_match("age:5")
	           .is_match("age:10")
	           .is_not_match("age:1")
	           .is_not_match("age:2");

	prop_test().date(2000, 1, 1)
	           .is_match("age:1")
	           .is_match("age:5")
	           .is_not_match("-age:5")
	           .is_not_match("!age:5");

	prop_test().tag("aaa").date(2000, 1, 1)
	           .is_match("#aaa age:1")
	           .is_match("#aaa created:2000-jan")
	           .is_not_match("created:2000-feb")
	           .is_not_match("#bbb created:2000-jan");

	prop_test().file_created_date(2000, 1, 1).date(1999, 5, 25)
	           .is_not_match("created:9")
	           .is_not_match("created:2000-jan")
	           .is_match("created:1999-may");

	prop_test().file_created_date(2000, 1, 1)
	           .is_match("age:10")
	           .is_match("created:2000-jan");
}

// A query the user is midway through typing has an unbalanced '(' on almost every
// keystroke. The Boolean evaluator must fold that open group back into the outermost
// level, otherwise the outermost level keeps its initial 'true' and the query silently
// matches every item in scope.
static void should_match_unbalanced_groups()
{
	prop_test().tag("aaa")
	           .is_match("(#aaa")
	           .is_match("(#aaa)")
	           .is_not_match("(#bbb")
	           .is_not_match("(#bbb)")
	           .is_match("#aaa (#bbb or #aaa")
	           .is_not_match("#aaa (#bbb")
	           .is_match("((#aaa")
	           .is_not_match("((#bbb");

	prop_test().tag("bbb")
	           .is_not_match("(#aaa")
	           .is_match("(#aaa or #bbb")
	           .is_not_match("(#aaa #bbb");
}

// An unknown property name after with:/without: used to build a null-key term, which
// inverted into "match everything" for without: and "match nothing" for with:.
static void should_ignore_unknown_property_scopes()
{
	prop_test().tag("aaa")
	           .is_match("without:rating")
	           .is_not_match("with:rating")
	           .is_not_match("without:not-a-property #bbb")
	           .is_match("with:tag");

	prop_test().rate(4)
	           .is_match("with:rating")
	           .is_not_match("without:rating");
}

// Property kinds that the parser used to drop entirely, leaving an empty term list and
// therefore an unfiltered result set.
static void should_match_parsed_property_values()
{
	// int_pair: "N" matches that number of any total, "N/M" must match both parts.
	prop_test().track(3, 12)
	           .is_match("track:3")
	           .is_match("track:3/12")
	           .is_not_match("track:4")
	           .is_not_match("track:3/11");

	// a date property other than created/modified reached a poisoned comparison
	prop_test().digitized(2000, 1, 1)
	           .is_match("digitized:2000-jan")
	           .is_not_match("digitized:2000-feb");

	// "ISO400" parsed the number but then matched against zero
	prop_test().iso(400)
	           .is_match("iso:ISO400")
	           .is_match("iso:400")
	           .is_not_match("iso:800");

	// "%d:%d" also matches the prefix of "1:02:03", so H:MM:SS must be tried first
	prop_test().duration(3723)
	           .is_match("duration:1:02:03")
	           .is_not_match("duration:1:02");
}

static void should_match_multi_value_genre()
{
	// A single ';'-separated genre field should match a query for any one of its values.
	prop_test().genre("Rock; Pop; Hip Hop")
	           .is_match("genre:Rock")
	           .is_match("genre:Pop")
	           .is_match("genre:'Hip Hop'")
	           .is_not_match("genre:Jazz")
	           .is_match("genre:Rock or genre:Jazz")
	           .is_not_match("-genre:Rock");

	// A single-value genre still matches exactly.
	prop_test().genre("Jazz")
	           .is_match("genre:Jazz")
	           .is_not_match("genre:Rock");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Issue #139 / #178 - Quoted terms containing spaces.
// A quoted value with spaces (e.g. a path or multi-word tag) must be parsed as a
// single term, not split on the space, and must survive round-tripping through
// the parser. This is the search-engine capability behind #139 (auto-quoting
// paths with spaces) and #178 (quoted/edited terms must not be silently dropped);
// the search-bar UI behaviour in those reports is tracked separately.
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_match_quoted_terms()
{
	// A multi-word tag is kept whole when quoted and does not match its parts.
	prop_test().tag("'holiday photos'")
	           .is_match("#'holiday photos'")
	           .is_not_match("#holiday")
	           .is_not_match("#photos");

	// A quoted phrase matches a contiguous substring only.
	prop_test().desc("new york city")
	           .is_match("'new york'")
	           .is_not_match("'york new'");

	// A path-like value with spaces round-trips through parse + match.
	prop_test().desc("C:/My Photos/trip")
	           .is_match("'My Photos'")
	           .is_not_match("'Your Photos'");
}

// Issue #178 - the search bar must echo exactly what the user typed instead of
// silently reverting it to the normalized form. Issue #139 - a bare path that
// contains spaces is auto-quoted so it reads as a single term.
static void should_preserve_and_auto_quote_search_input()
{
	const df::search_t base;

	// #178: user text is preserved, not reverted to a normalized reconstruction.
	assert_equal("#a #b #a"s, base.parse_from_input("#a #b #a").text(),
	             "duplicate tags preserved in search bar text");
	assert_equal("\"aaa\""s, base.parse_from_input("\"aaa\"").text(),
	             "quotes preserved in search bar text");

	// #139: a bare path containing spaces is auto-quoted.
	assert_equal("\"C:\\My Photos\""s, base.parse_from_input("C:\\My Photos").text(),
	             "path with spaces auto-quoted");
	assert_equal("C:\\Photos"s, base.parse_from_input("C:\\Photos").text(),
	             "path without spaces left unquoted");
	assert_equal("\"C:\\My Photos\""s, base.parse_from_input("\"C:\\My Photos\"").text(),
	             "already-quoted path not double-quoted");
}

// Issue #157 - scope-aware search auto-completion. The active (last) token of a query
// is classified so the value part of a scoped term can be completed from the matching
// vocabulary, and matches are formatted back into the text that will be committed.
static void should_classify_search_scope()
{
	using k = df::search_scope_kind;

	// A plain word / empty query is not scoped.
	assert_equal(true, df::classify_search_scope("hello").kind == k::none, "plain word not scoped");
	assert_equal(true, df::classify_search_scope("").kind == k::none, "empty query not scoped");

	// '#' tag scope: completes from "#tag" vocabulary and keeps the '#' form.
	{
		const auto s = df::classify_search_scope("#do");
		assert_equal(true, s.kind == k::tag, "# is a tag scope");
		assert_equal(false, s.tag_as_scope, "# is not the tag: form");
		assert_equal("do"s, s.value, "# value fragment");
		assert_equal("#do"s, s.vocab_query(), "# vocabulary query");
		assert_equal("#dog"s, s.format("#dog"), "# keeps the hash form");
	}

	// 'tag:' scope: completes from the same vocabulary but rewrites to the tag: form.
	{
		const auto s = df::classify_search_scope("tag:do");
		assert_equal(true, s.kind == k::tag, "tag: is a tag scope");
		assert_equal(true, s.tag_as_scope, "tag: form recorded");
		assert_equal("#do"s, s.vocab_query(), "tag: uses the # vocabulary");
		assert_equal("tag:dog"s, s.format("#dog"), "tag: rewrites #dog -> tag:dog");
	}

	// '@' media group scope.
	{
		const auto s = df::classify_search_scope("@vi");
		assert_equal(true, s.kind == k::group, "@ is a group scope");
		assert_equal("@vi"s, s.vocab_query(), "@ vocabulary query");
		assert_equal("@video"s, s.format("@video"), "@ keeps the group form");
	}

	// with: / without: complete from property scope names (no vocabulary query).
	{
		const auto sw = df::classify_search_scope("with:ex");
		assert_equal(true, sw.kind == k::with, "with: scope");
		assert_equal(""s, sw.vocab_query(), "with: has no word vocabulary");
		assert_equal("with:exposure"s, sw.format("exposure"), "with: formats the scope name");

		const auto swo = df::classify_search_scope("without:ta");
		assert_equal(true, swo.kind == k::without, "without: scope");
		assert_equal("without:tag"s, swo.format("tag"), "without: formats the scope name");
	}

	// Only the last token is scoped; the lead text is preserved verbatim.
	{
		const auto s = df::classify_search_scope("beach tag:do");
		assert_equal(true, s.kind == k::tag, "last token classified");
		assert_equal("beach "s, s.lead, "lead text preserved");
		assert_equal("do"s, s.value, "value taken from last token");
		assert_equal("beach tag:dog"s, str::combine2(s.lead, s.format("#dog")), "lead + formatted value");
	}

	// Scope prefixes are case-insensitive.
	assert_equal(true, df::classify_search_scope("TAG:do").kind == k::tag, "tag: is case-insensitive");
	assert_equal(true, df::classify_search_scope("WITH:x").kind == k::with, "with: is case-insensitive");

	// An empty value (just the prefix) still classifies and yields a broad vocabulary query.
	{
		const auto s = df::classify_search_scope("tag:");
		assert_equal(true, s.kind == k::tag, "bare tag: is a tag scope");
		assert_equal("#"s, s.vocab_query(), "bare tag: matches all tags");
	}

	// locations.md 3.4/3.5: every location scope completes from the gazetteer, differing only in
	// the level it constrains to, and the candidate arrives as a finished canonical term.
	{
		using l = df::location_level;

		const auto loc = df::classify_search_scope("loc:lond");
		assert_equal(true, loc.kind == k::location, "loc: is a location scope");
		assert_equal(true, loc.level == l::any, "loc: is unqualified");
		assert_equal("lond"s, loc.value, "loc: value fragment");
		assert_equal(""s, loc.vocab_query(), "loc: has no word vocabulary");
		assert_equal("loc:\"London, United Kingdom\""s, loc.format("loc:\"London, United Kingdom\""),
		             "location candidates commit verbatim");

		assert_equal(true, df::classify_search_scope("near:lond").kind == k::location, "near: is a location scope");
		assert_equal(true, df::classify_search_scope("place:lond").level == l::place, "place: is place level");
		assert_equal(true, df::classify_search_scope("city:lond").level == l::place, "city: is place level");
		assert_equal(true, df::classify_search_scope("state:cali").level == l::state, "state: is state level");
		assert_equal(true, df::classify_search_scope("country:fra").level == l::country, "country: is country level");
		assert_equal(true, df::classify_search_scope("countries:fra").level == l::country,
		             "countries: is country level");
		assert_equal(true, df::classify_search_scope("LOC:x").kind == k::location, "loc: is case-insensitive");

		// Only the last token is scoped, and a bare prefix still classifies.
		const auto lead = df::classify_search_scope("beach loc:lond");
		assert_equal(true, lead.kind == k::location, "last token classified");
		assert_equal("beach "s, lead.lead, "lead text preserved");
		assert_equal(""s, df::classify_search_scope("loc:").value, "bare loc: matches all places");

		// A word that merely begins with a scope name is not a scope.
		assert_equal(true, df::classify_search_scope("location").kind == k::none, "'location' is a plain word");
	}
}

// locations.md 3.4/3.5: typing a bare place name must offer the canonical location term. Before
// this, the prediction committed the plain qualified name, which ran a text search over stored
// place fields and found nothing for the (typical) GPS-only photo.
static void should_complete_locations_as_search_terms()
{
	null_async_strategy as;
	const auto& locations = test_locations();
	const index_state index(as, locations);

	const auto london = index.auto_complete_locations("london", 5);
	assert_equal(true, !london.empty(), "london predicts a location");
	assert_equal("loc:\"London, United Kingdom\""s, london.front().text, "bare name predicts the canonical term");
	assert_equal(true, !london.front().highlights.empty(), "typed fragment is highlighted");

	// The prediction must round-trip: committing it has to produce a location term, not text.
	const auto parsed = df::search_t::parse(london.front().text);
	assert_equal(1, static_cast<int>(parsed.terms().size()), "prediction is a single term");
	assert_equal(true, parsed.terms().front().type == df::search_term_type::location,
	             "prediction commits a location term");

	// A level-qualified scope labels the place at that level and keeps the scope prefix.
	const auto states = index.auto_complete_locations("cali", 5, df::location_level::state);
	assert_equal(true, !states.empty(), "state scope predicts states");
	assert_equal(true, str::starts(states.front().text, "state:"), "state scope keeps its prefix");

	const auto countries = index.auto_complete_locations("franc", 5, df::location_level::country);
	assert_equal(true, !countries.empty(), "country scope predicts countries");
	assert_equal("country:France"s, countries.front().text, "country scope labels at country level");

	// Predictions are distinct; the gazetteer holds many rows per named place.
	const auto many = index.auto_complete_locations("lond", 8);
	df::hash_set<std::string, df::ihash, df::ieq> seen;
	for (const auto& m : many) assert_equal(true, seen.emplace(m.text).second, "predictions are distinct");

	assert_equal(true, index.auto_complete_locations("", 5).empty(), "empty query predicts nothing");
}

static void should_format_search_predictions()
{
	assert_equal("#holiday #beach"s, str::combine2(auto_complete_lead("#holiday"), "#beach"),
	             "missing separator is inserted");
	assert_equal("#holiday #beach"s, str::combine2(auto_complete_lead("#holiday "), "#beach"),
	             "existing separator is preserved");

	assert_equal(true, search_icon("holiday") == icon_index::search, "plain text uses search icon");
	assert_equal(true, search_icon("C:\\Photos") == icon_index::folder, "path uses folder icon");
	assert_equal(true, search_icon("#holiday") == icon_index::tag, "tag uses tag icon");
	assert_equal(true, search_icon("rating:4") == prop::rating.icon, "property uses property icon");
	assert_equal(true, search_icon("2012-09-14") == icon_index::time, "date uses time icon");
	assert_equal(true, search_icon("@photo") == icon_index::photo, "media group uses media icon");
}

static void should_not_match_folder_without()
{
	const auto now_days = df::date_t(2000, 1, 1).to_days();
	const auto search = df::search_t::parse("without:tag");
	const df::search_matcher matcher(search, now_days);
	assert_equal(false, matcher.match_folder(test_files_folder.text(), "test"_c).is_match(), "folder name test");
}

static void should_match_volume_label()
{
	// Map a drive letter to a volume label so the volume: term can be matched
	// deterministically without depending on the drives present on the machine.
	df::hash_map<char, str::cached> labels;
	labels['X'] = str::cache("Backup");

	const auto exact = df::search_t::parse("volume:Backup");
	const auto& exact_term = exact.terms().front();

	assert_equal(true, df::match_volume_label("X:\\photos\\2024", labels, exact_term), "match on drive X");
	assert_equal(true, df::match_volume_label("x:\\photos", labels, exact_term), "drive letter case-insensitive");
	assert_equal(false, df::match_volume_label("Y:\\photos", labels, exact_term), "different drive");
	assert_equal(false, df::match_volume_label("\\\\server\\share", labels, exact_term),
	             "unc path has no drive letter");

	assert_equal(true, df::match_volume_label("X:\\x", labels, df::search_t::parse("volume:BACKUP").terms().front()),
	             "label case-insensitive");
	assert_equal(true, df::match_volume_label("X:\\x", labels, df::search_t::parse("volume:Back*").terms().front()),
	             "label wildcard");
	assert_equal(false, df::match_volume_label("X:\\x", labels, df::search_t::parse("volume:Other").terms().front()),
	             "non-matching label");
}

static void should_match_date(const std::string_view query, const df::date_t d)
{
	df::index_file_item props_without_val;
	props_without_val.ft = files::file_type_from_name("test.jpg");
	props_without_val.file_modified = df::date_t(1972, 5, 25);
	props_without_val.safe_ps();

	df::index_file_item props_with_val;
	props_with_val.ft = files::file_type_from_name("test.jpg");
	props_with_val.safe_ps()->created_utc = d;

	const auto search = df::search_t::parse(query);
	df::search_matcher matcher(search, df::date_t(2000, 1, 1).to_days());

	assert_equal(true, matcher.match_all_terms(test_files_folder.text(), props_with_val).is_match(), query);
	assert_equal(false, matcher.match_all_terms(test_files_folder.text(), props_without_val).is_match(), query);
}

static void assert_parse(const std::string_view selector, const std::string_view display,
                         const std::string_view folder,
                         const bool is_recursive, const std::string_view wildcard)
{
	const df::item_selector sel(selector);

	assert_equal(display, sel.str(), "item_selector.str");
	assert_equal(folder, sel.folder().text(), "item_selector.folder");
	assert_equal(is_recursive, sel.is_recursive(), "item_selector.is_recursive");
	assert_equal(wildcard, sel.wildcard(), "item_selector.wildcard");
}

static void assert_parse(const std::string_view selector, const std::string_view folder,
                         const bool is_recursive, const std::string_view wildcard)
{
	assert_parse(selector, selector, folder, is_recursive, wildcard);
}

static void should_parse_selector()
{
	assert_parse("c:\\", "c:\\", false, "*.*");
	assert_parse("c:\\**", "c:\\", true, "*.*");
	assert_parse("c:\\**\\", "c:\\**", "c:\\", true, "*.*");
	assert_parse("c:/**/", "c:\\**", "c:\\", true, "*.*");
	assert_parse("c:\\*.jpg", "c:\\", false, "*.jpg");
	assert_parse("c:\\temp\\*.jpg", "c:\\temp", false, "*.jpg");
	assert_parse(R"(c:\temp\**\*.jpg)", "c:\\temp", true, "*.jpg");
	assert_parse("c:\\temp\\***.jpg", "c:\\temp", false, "***.jpg");
	assert_parse("c:\\temp\\?x.jpg", "c:\\temp", false, "?x.jpg");
}

static void should_detect_folder_browse()
{
	// A plain folder path (with no search terms) is a folder browse. This drives the
	// "Empty Folder" vs "Nothing found" message when the view has no items.
	assert_equal(true, df::search_t::parse("\\\\").is_empty(), "incomplete UNC root");
	assert_equal(true, df::search_t::parse("\\\\server").is_empty(), "incomplete UNC server");
	assert_equal(true, df::search_t::parse("\\\\server\\").is_empty(), "incomplete UNC server root");
	assert_equal(true, df::search_t::parse("c:\\windows").is_folder(), "plain folder path");
	assert_equal(true, df::search_t::parse("c:\\windows\\**").is_folder(), "recursive folder browse");
	assert_equal(true, df::search_t::parse("c:\\windows\\*.jpg").is_folder(), "folder with wildcard, no terms");

	// A search - even one scoped to a folder - is not a folder browse.
	assert_equal(false, df::search_t::parse("c:\\windows with:tag").is_folder(), "folder scoped search");
	assert_equal(false, df::search_t::parse("dog").is_folder(), "term with no selector");
	assert_equal(false, df::search_t::parse("2014").is_folder(), "date term");
	assert_equal(false, df::search_t().is_folder(), "empty search");
}

static bool contains(const std::vector<platform::file_info>& files, const std::string_view find)
{
	for (const auto& f : files)
	{
		if (icmp(f.name, find) == 0)
		{
			return true;
		}
	}

	return false;
}

static bool contains(const std::vector<platform::folder_info>& files, const std::string_view find)
{
	for (const auto& f : files)
	{
		if (icmp(f.name, find) == 0)
		{
			return true;
		}
	}

	return false;
}

static void should_select_files()
{
	const auto recursive = df::item_selector(test_files_folder, true);
	assert_equal(true, contains(platform::select_files(recursive, true), "Screws.CR2"),
	             "files from sub folders");
	assert_equal(true, contains(platform::select_files(recursive, true), "Cube.png"), "files from sub folders");
	assert_equal(true, contains(platform::select_files(recursive, true), "Test.jpg"), "files from sub folders");
	assert_equal(false, contains(platform::select_folders(recursive, true), "raw"), "no folders");

	const auto not_recursive = df::item_selector(test_files_folder, false);
	assert_equal(false, contains(platform::select_files(not_recursive, true), "Screws.CR2"),
	             "no files from sub folder");
	assert_equal(true, contains(platform::select_files(not_recursive, true), "Cube.png"),
	             "files from sub folders");
	assert_equal(true, contains(platform::select_files(not_recursive, true), "Test.jpg"),
	             "files from sub folders");
	assert_equal(true, contains(platform::select_folders(not_recursive, true), "raw"), "include folders");

	const auto recursive_png = df::item_selector(test_files_folder, true, "*.png");
	assert_equal(false, contains(platform::select_files(recursive_png, true), "Screws.CR2"),
	             "no files from sub folder");
	assert_equal(true, contains(platform::select_files(recursive_png, true), "Cube.png"),
	             "files from sub folders");
	assert_equal(false, contains(platform::select_files(recursive_png, true), "Test.jpg"),
	             "files from sub folders");
	assert_equal(false, contains(platform::select_folders(recursive_png, true), "raw"), "include folders");

	const auto not_recursive_cr2 = df::item_selector(test_files_folder, false, "*.cr2");
	assert_equal(false, contains(platform::select_files(not_recursive_cr2, true), "Screws.CR2"),
	             "no files from sub folder");
	assert_equal(false, contains(platform::select_files(not_recursive_cr2, true), "Cube.png"),
	             "files from sub folders");
	assert_equal(false, contains(platform::select_files(not_recursive_cr2, true), "Test.jpg"),
	             "files from sub folders");
	assert_equal(true, contains(platform::select_folders(not_recursive_cr2, true), "raw"), "include folders");

	const auto recursive_cr2 = df::item_selector(test_files_folder, true, "*.cr2");
	assert_equal(true, contains(platform::select_files(recursive_cr2, true), "Screws.CR2"),
	             "no files from sub folder");
	assert_equal(false, contains(platform::select_files(recursive_cr2, true), "Cube.png"),
	             "files from sub folders");
	assert_equal(false, contains(platform::select_files(recursive_cr2, true), "Test.jpg"),
	             "files from sub folders");
	assert_equal(false, contains(platform::select_folders(recursive_cr2, true), "raw"), "include folders");
}

// A related search answers with the closest matches on each axis, so the collector has to keep the
// best `limit` and drop the rest whatever order the index happened to hand them over in.
static void should_bound_related_results_by_closeness()
{
	df::bounded_best<int> slots;
	slots.limit(3);

	const df::folder_path folder("c:\\test");

	for (const auto distance : {50, 10, 40, 20, 30})
	{
		slots.offer(distance, df::file_path(folder, std::format("{}.jpg", distance)), distance);
	}

	const auto best = slots.take();

	assert_equal(3u, static_cast<uint32_t>(best.size()), "cap holds");
	assert_equal(10, best[0].value, "closest first");
	assert_equal(20, best[1].value, "then next closest");
	assert_equal(30, best[2].value, "furthest kept is the third closest");
	assert_equal(true, slots.empty(), "taking drains the collector");
}

// Which items survive a full axis must not depend on the order folders were walked in, or the same
// search would answer differently each time it ran.
static void should_break_related_ties_on_path()
{
	const df::folder_path folder("c:\\test");
	const df::file_path a(folder, "a.jpg");
	const df::file_path b(folder, "b.jpg");
	const df::file_path c(folder, "c.jpg");

	const auto survivors = [&](const df::file_paths& order)
	{
		df::bounded_best<int> slots;
		slots.limit(2);
		for (const auto& path : order) slots.offer(7, path, 0);

		std::string result;
		for (const auto& e : slots.take()) result += e.path.name();
		return result;
	};

	assert_equal("a.jpgb.jpg", survivors({a, b, c}), "equal distances keep the first paths");
	assert_equal("a.jpgb.jpg", survivors({c, b, a}), "and do not depend on offer order");
	assert_equal("a.jpgb.jpg", survivors({b, c, a}), "or on which arrived first");
}

static void should_group_related_results_by_axis()
{
	df::related_collector<int> collector;
	const df::folder_path folder("c:\\test");

	collector.offer({df::related_axis::location, 5}, df::file_path(folder, "place.jpg"), 4);
	collector.offer({df::related_axis::duplicate, 0}, df::file_path(folder, "copy.jpg"), 1);
	collector.offer({df::related_axis::time, 5}, df::file_path(folder, "when.jpg"), 3);
	collector.offer({df::related_axis::album, 5}, df::file_path(folder, "album.jpg"), 2);

	assert_equal(4u, static_cast<uint32_t>(collector.size()), "every axis holds its own matches");
	assert_equal(1u, static_cast<uint32_t>(collector.size(df::related_axis::time)), "per axis count");

	std::string order;
	collector.drain([&order](const df::file_path, int&& value) { order += std::to_string(value); });

	assert_equal("1234", order, "axes drain in priority order");
}

static df::index_file_item make_related_candidate(const std::string_view name)
{
	df::index_file_item result;
	result.ft = files::file_type_from_name(name);
	result.name = str::cache(name);
	return result;
}

static std::string_view related_axis_name(const df::related_axis axis)
{
	switch (axis)
	{
	case df::related_axis::duplicate: return "duplicate";
	case df::related_axis::album: return "album";
	case df::related_axis::series: return "series";
	case df::related_axis::time: return "time";
	default: return "location";
	}
}

static std::string_view matched_axis(const df::search_result& match)
{
	return related_axis_name(df::related_axis_of(match.type));
}

// Names each result with the relation that earned it, so a related search is asserted on what it
// found and why rather than on a bare count.
static std::string related_result_summary(index_state& index, const df::search_t& search)
{
	std::vector<std::string> found;

	auto cb = [&found](const index_state::query_item_results& items, const bool)
	{
		for (const auto& i : items)
		{
			found.emplace_back(std::format("{}:{}", matched_axis(i.match), i.path.name()));
		}
	};

	index.query_items(search, cb, test_token);
	std::ranges::sort(found);

	std::string result;
	for (const auto& text : found)
	{
		if (!result.empty()) result += ' ';
		result += text;
	}
	return result;
}

static df::related_info make_related_anchor()
{
	df::related_info anchor;
	anchor.path = df::file_path(test_files_folder, "anchor.jpg");
	anchor.name = str::cache("anchor.jpg");
	anchor.ft = files::file_type_from_name("anchor.jpg");
	anchor.size = df::file_size(1000);
	anchor.is_loaded = true;
	return anchor;
}

static df::search_result match_related(const df::related_info& anchor, const std::string_view name,
                                       const df::index_file_item& candidate)
{
	const auto search = df::search_t().related(anchor);
	const df::search_matcher matcher(search);
	return matcher.match_item(df::file_path(test_files_folder, name), candidate);
}

static void should_match_related_by_album()
{
	auto anchor = make_related_anchor();
	anchor.album = str::cache("Album");
	anchor.disk = df::xy8::make(1, 1);
	anchor.track = df::xy8::make(3, 12);

	auto candidate = make_related_candidate("track5.mp3");
	candidate.safe_ps()->album = str::cache("Album");
	candidate.safe_ps()->disk = df::xy8::make(1, 1);
	candidate.safe_ps()->track = df::xy8::make(5, 12);

	const auto match = match_related(anchor, "track5.mp3", candidate);
	assert_equal(true, match.is_match(), "same album is related");
	assert_equal("album", matched_axis(match), "album axis");
	assert_equal(2, static_cast<int>(match.distance), "distance is the gap in track order");

	// Two artists can both have a "Greatest Hits", so a named artist on each side has to agree.
	anchor.album_artist = str::cache("One");
	candidate.safe_ps()->album_artist = str::cache("Another");
	assert_equal(false, match_related(anchor, "track5.mp3", candidate).is_match(),
	             "a different album artist is not the same album");
}

static void should_match_related_by_series()
{
	auto anchor = make_related_anchor();
	anchor.show = str::cache("Show");
	anchor.season = 2;
	anchor.episode = df::xy8::make(5, 10);

	auto candidate = make_related_candidate("episode7.mkv");
	candidate.safe_ps()->show = str::cache("Show");
	candidate.safe_ps()->season = 2;
	candidate.safe_ps()->episode = df::xy8::make(7, 10);

	const auto match = match_related(anchor, "episode7.mkv", candidate);
	assert_equal(true, match.is_match(), "same series is related");
	assert_equal("series", matched_axis(match), "series axis");
	assert_equal(2, static_cast<int>(match.distance), "distance is the gap in episode order");

	candidate.safe_ps()->show = str::cache("Other show");
	assert_equal(false, match_related(anchor, "episode7.mkv", candidate).is_match(), "a different show is unrelated");
}

static void should_match_related_by_capture_time()
{
	auto anchor = make_related_anchor();
	anchor.metadata_created = df::date_t(2020, 5, 1, 12, 0, 0);

	auto candidate = make_related_candidate("burst.jpg");
	candidate.safe_ps()->created_exif = df::date_t(2020, 5, 1, 12, 0, 30);

	const auto match = match_related(anchor, "burst.jpg", candidate);
	assert_equal(true, match.is_match(), "a near capture time is related");
	assert_equal("time", matched_axis(match), "time axis");
	assert_equal(30, static_cast<int>(match.distance), "distance is the gap in seconds");

	candidate.safe_ps()->created_exif = df::date_t(2020, 5, 3, 12, 0, 0);
	assert_equal(false, match_related(anchor, "burst.jpg", candidate).is_match(),
	             "outside the window is not the same time");

	// A whole collection copied in one pass shares a file time, which would otherwise make every
	// item in it equally and meaninglessly close.
	auto file_time_only = make_related_candidate("copied.jpg");
	file_time_only.file_created = anchor.metadata_created;
	assert_equal(false, match_related(anchor, "copied.jpg", file_time_only).is_match(),
	             "file time alone is not capture time");
}

static void should_match_related_by_capture_place()
{
	auto anchor = make_related_anchor();
	anchor.gps = gps_coordinate(51.5007, -0.1246);

	auto candidate = make_related_candidate("nearby.jpg");
	candidate.safe_ps()->coordinate = gps_coordinate(51.5033, -0.1195);

	const auto match = match_related(anchor, "nearby.jpg", candidate);
	assert_equal(true, match.is_match(), "a near capture place is related");
	assert_equal("location", matched_axis(match), "location axis");
	assert_equal(true, match.distance > 0 && match.distance < 1000, "distance is metres apart");

	candidate.safe_ps()->coordinate = gps_coordinate(48.8584, 2.2945);
	assert_equal(false, match_related(anchor, "nearby.jpg", candidate).is_match(),
	             "outside the window is not the same place");
}

// An item can be near in time and in place at once. It belongs to one group, so the strongest
// relation decides which, and it is never listed twice.
static void should_report_the_strongest_related_axis()
{
	auto anchor = make_related_anchor();
	anchor.metadata_created = df::date_t(2020, 5, 1, 12, 0, 0);
	anchor.gps = gps_coordinate(51.5007, -0.1246);
	anchor.album = str::cache("Album");

	auto candidate = make_related_candidate("everything.jpg");
	candidate.safe_ps()->created_exif = anchor.metadata_created;
	candidate.safe_ps()->coordinate = anchor.gps;
	candidate.safe_ps()->album = str::cache("Album");

	assert_equal("album", matched_axis(match_related(anchor, "everything.jpg", candidate)),
	             "album outranks time and place");

	candidate.safe_ps()->album.clear();
	assert_equal("time", matched_axis(match_related(anchor, "everything.jpg", candidate)),
	             "time outranks place");

	candidate.safe_ps()->created_exif.clear();
	assert_equal("location", matched_axis(match_related(anchor, "everything.jpg", candidate)),
	             "place is the weakest relation");
}

// A file matched by two overlapping selectors is still one file. `folders_scanned` only stops a
// folder being walked twice within one selector, so the result set is what has to hold the rule.
static void should_list_an_item_once(shared_test_context& stc)
{
	stc.lazy_load_index();

	const df::item_selector whole_tree(test_files_folder, true);
	const df::item_selector sub_folder(test_files_folder.combine("raw"), true);

	const auto tree_only = count_search_results(stc.test_index, df::search_t().add_selector(whole_tree));
	const auto overlapping = count_search_results(stc.test_index,
	                                              df::search_t().add_selector(whole_tree).add_selector(sub_folder));

	assert_equal(tree_only, overlapping, "an overlapping selector adds no items");
}

static void should_match_related(shared_test_context& stc)
{
	stc.lazy_load_index();

	const df::file_path path(test_files_folder, "Test.jpg");
	const auto i = std::make_shared<df::item_element>(path, stc.test_index.find_item(path));
	stc.test_index.scan_item(i, true, false);

	df::related_info r;
	r.load(i);

	// The item the search started at, then the rotated variants that share its capture time.
	assert_equal("duplicate:Test.jpg time:Small.jpg time:Test180.jpg time:Test270.jpg time:Test90.jpg",
	             related_result_summary(stc.test_index, df::search_t().related(r)), "Related");

	r.group = 0;
	df::index_file_item different_size = stc.test_index.find_item(path);
	different_size.name = str::cache("different-name.jpg");
	different_size.size = df::file_size(different_size.size.to_int64() + 1);
	const auto related_search = df::search_t().related(r);
	const df::search_matcher matcher(related_search);

	// Equal CRC is evidence of a copy only when the sizes agree, so a bumped size has to fall through
	// the duplicate rules. It is still the same photo, so capture time catches it.
	assert_equal("time", matched_axis(matcher.match_item({}, different_size)),
	             "Related CRC requires equal size");
}

// A related search written as text - a favorite, a saved search, or typed into the address
// box - carries only the path, so the query must resolve the rest instead of matching
// against unloaded fields.
static void should_match_related_from_text(shared_test_context& stc)
{
	stc.lazy_load_index();

	const df::file_path path(test_files_folder, "Test.jpg");
	const auto i = std::make_shared<df::item_element>(path, stc.test_index.find_item(path));
	stc.test_index.scan_item(i, true, false);

	df::related_info r;
	r.load(i);

	const auto reparsed = df::search_t::parse(df::search_t().related(r).format_terms());

	assert_equal(true, reparsed.has_related(), "related survives a text round trip");
	assert_equal(path.str(), reparsed.related().path.str(), "related path");

	// Every field the axes compare has to be recovered from the index, so a favorite answers with the
	// same relations as the command that created it.
	assert_equal(related_result_summary(stc.test_index, df::search_t().related(r)),
	             related_result_summary(stc.test_index, reparsed), "related search from text");
}

static void should_parse_search()
{
	constexpr auto message = "Should tokenize search";
	search_tokenizer t;
	const auto parts = t.parse(
		"  (#tree or house)    -nega-tive aperture:f/2.0   's\"om:e#inv-lid~s' -description:\"hello world\" tag : spaces c:\\test 12:00 or -#tree|ant");

	assert_equal(parts[0].modifier.positive, true, message);
	assert_equal(parts[0].modifier.logical_op, df::search_term_modifier_bool::none, message);
	assert_equal(parts[0].modifier.begin_group, 1, message);
	assert_equal(parts[0].modifier.end_group, 0, message);
	assert_equal(parts[0].scope, "tag", message);
	assert_equal(parts[0].term, "tree", message);

	assert_equal(parts[1].modifier.positive, true, message);
	assert_equal(parts[1].modifier.logical_op, df::search_term_modifier_bool::m_or, message);
	assert_equal(parts[1].modifier.begin_group, 0, message);
	assert_equal(parts[1].modifier.end_group, 1, message);
	assert_equal(parts[1].scope.empty(), true, message);
	assert_equal(parts[1].term, "house", message);

	assert_equal(parts[2].modifier.positive, false, message);
	assert_equal(parts[2].modifier.logical_op, df::search_term_modifier_bool::none, message);
	assert_equal(parts[2].modifier.begin_group, 0, message);
	assert_equal(parts[2].modifier.end_group, 0, message);
	assert_equal(parts[2].scope.empty(), true, message);
	assert_equal(parts[2].term, "nega-tive", message);

	assert_equal(parts[3].modifier.positive, true, message);
	assert_equal(parts[3].modifier.logical_op, df::search_term_modifier_bool::none, message);
	assert_equal(parts[3].modifier.begin_group, 0, message);
	assert_equal(parts[3].modifier.end_group, 0, message);
	assert_equal(parts[3].scope, "aperture", message);
	assert_equal(parts[3].term, "f/2.0", message);

	assert_equal(parts[4].modifier.positive, true, message);
	assert_equal(parts[4].scope.empty(), true, message);
	assert_equal(parts[4].term, "s\"om:e#inv-lid~s", message);

	assert_equal(parts[5].modifier.positive, false, message);
	assert_equal(parts[5].scope, "description", message);
	assert_equal(parts[5].term, "hello world", message);

	assert_equal(parts[6].scope, "tag", message);
	assert_equal(parts[6].term, "spaces", message);

	assert_equal(parts[7].modifier.positive, true, message);
	assert_equal(parts[7].scope.empty(), true, message);
	assert_equal(parts[7].term, "c:\\test", message);

	assert_equal(parts[8].modifier.positive, true, message);
	assert_equal(parts[8].scope.empty(), true, message);
	assert_equal(parts[8].term, "12:00", message);

	assert_equal(parts[9].modifier.positive, false, message);
	assert_equal(parts[9].modifier.logical_op, df::search_term_modifier_bool::m_or, message);
	assert_equal(parts[9].scope, "tag", message);
	assert_equal(parts[9].term, "tree", message);

	assert_equal(parts[10].modifier.positive, true, message);
	assert_equal(parts[10].modifier.logical_op, df::search_term_modifier_bool::m_or, message);
	assert_equal(parts[10].scope.empty(), true, message);
	assert_equal(parts[10].term, "ant", message);

	const auto location_parts = t.parse("loc: London, UK 5km");
	assert_equal(3, static_cast<int>(location_parts.size()), "a comma separates without creating a term");
	assert_equal("loc", location_parts[0].scope, "location scope is retained");
	assert_equal("London", location_parts[0].term, "location term");
	assert_equal(true, location_parts[1].after_comma, "a comma binds the following token");
	assert_equal("UK", location_parts[1].term, "comma bound token");
	assert_equal(false, location_parts[2].after_comma, "a space does not bind");
}

static void assert_date_shift(df::search_t d, const std::string_view expected_prev,
                              const std::string_view expected_next)
{
	d.next_date(false);
	assert_equal(expected_prev, d.text(), "prev date");
	d.next_date(true);
	assert_equal(expected_next, d.text(), "next date");
}

static void should_next_date_search()
{
	assert_date_shift(df::search_t().day(25, 5, 1972), "1972-may-24", "1972-may-25");
	assert_date_shift(df::search_t().day(1, 1, 2020), "2019-dec-31", "2020-jan-1");
	assert_date_shift(df::search_t().day(1, 1, 0), "dec-31", "jan-1");
	assert_date_shift(df::search_t().year(2010), "2009", "2010");
	assert_date_shift(df::search_t().month(1), "December", "January");
	assert_date_shift(df::search_t().month(1).year(2010), "2009-dec", "2010-jan");

	assert_date_shift(df::search_t().day(1, 1, 2020, df::date_parts_prop::modified), "modified:2019-dec-31",
	                  "modified:2020-jan-1");
	assert_date_shift(df::search_t().day(1, 1, 0, df::date_parts_prop::modified), "modified:dec-31",
	                  "modified:jan-1");
	assert_date_shift(df::search_t().year(2010, df::date_parts_prop::created), "created:2009", "created:2010");
	assert_date_shift(df::search_t().month(1, df::date_parts_prop::created).year(2010, df::date_parts_prop::created),
	                  "created:2009-dec", "created:2010-jan");
}

static void should_parse_search_input()
{
	const auto base_folder = std::string(test_files_folder.text());

	null_state_strategy ss;
	null_async_strategy as;
	const view_host_base_ptr view;
	const location_cache locations;
	index_state index(as, locations);
	view_state s(ss, as, index, make_test_player());
	s.view_mode(view_type::items);
	s.open(view, test_files_folder.text());
	s.open(view, "..");

	assert_equal(test_files_folder.parent().text(), s.search().text(), "Parse ..");

	s.open(view, test_files_folder.text());
	s.open(view, "**");

	assert_equal(base_folder + "\\**"s, s.search().text(), "Parse **");

	s.open(view, test_files_folder.text());
	s.open(view, "aaa");

	assert_equal("aaa"s, s.search().text(), "Parse aaa");

	s.open(view, "\"aaa\"");
	assert_equal("\"aaa\""s, s.search().text(), "Parse \"aaa\"");
}

static void should_parent()
{
	const auto folder = df::folder_path("c:\\windows\\system32");
	assert_equal("c:\\windows", folder.parent().text(), "parent test");
	assert_equal("c:\\", folder.parent().parent().text(), "parent test");
	assert_equal("c:\\", folder.parent().parent().parent().text(), "parent test");

	const auto base_folder = std::string(test_files_folder.text());
	null_state_strategy ss;
	null_async_strategy as;
	location_cache locations;
	view_host_base_ptr view;
	index_state index(as, locations);
	view_state s(ss, as, index, make_test_player());
	s.view_mode(view_type::items);

	df::index_roots roots;
	roots.folders = {test_files_folder};
	s.item_index.index_roots(roots);
	s.item_index.index_folders(test_token);

	s.open(view, base_folder + " test"s);
	assert_equal(base_folder, s.parent_search().parent.text(), "parent test");

	s.open(view, base_folder + "\\raw"s);
	const auto raw_parent = s.parent_search();
	assert_equal(base_folder, raw_parent.parent.text(), "parent \\raw");
	assert_equal("raw", raw_parent.name, "parent name \\raw");

	s.open(view, base_folder + "\\*.png"s);
	assert_equal(base_folder, s.parent_search().parent.text(), "parent *.png");

	s.open(view, base_folder + " @photo"s);
	assert_equal(base_folder, s.parent_search().parent.text(), "parent @photo");

	s.open(view, "@photo");
	assert_equal({}, s.parent_search().parent.text(), "parent @photo");

	s.open(view, "@photo");
	s.update_item_groups();
	s.select_next(view, true, false, false);
	s.update_selection();
	// Parent broadens the query. It must not switch to a folder taken from the selection -
	// that would be a different scope, not a wider one.
	assert_equal({}, s.parent_search().parent.text(), "parent @photo with selection");

	// DATES
	s.open(view, "1972-may-25");
	assert_equal("1972-may", s.parent_search().parent.text(), "parent");

	s.open(view, "may-25");
	assert_equal("May", s.parent_search().parent.text(), "parent");

	s.open(view, "1972-may");
	assert_equal("1972", s.parent_search().parent.text(), "parent");

	s.open(view, "2009 December");
	assert_equal("2009", s.parent_search().parent.text(), "parent");

	// A drive root has nothing wider to show, so Parent reports no parent and the command is
	// disabled rather than re-running the same query.
	assert_equal(true, find_parent_search(df::search_t::parse("c:\\")).parent.is_empty(), "parent of drive root");

	// A wildcard or recursive scope at a root still broadens - to the plain root folder.
	assert_equal("c:\\", find_parent_search(df::search_t::parse("c:\\*.png")).parent.text(), "parent of root *.png");
	assert_equal("c:\\", find_parent_search(df::search_t::parse("c:\\**")).parent.text(), "parent of root **");

	// Terms are dropped one at a time, dates first, whether or not a folder selector is present.
	const auto folder_photo_date = find_parent_search(df::search_t::parse("c:\\windows @photo 1972-may")).parent;
	assert_equal(true, folder_photo_date.has_media_type(), "parent keeps the media type");
	assert_equal(true, folder_photo_date.has_selector(), "parent keeps the folder scope");
	assert_equal(1972, folder_photo_date.find_date_parts().year, "parent steps the date first");
	assert_equal(0, folder_photo_date.find_date_parts().month, "parent steps the date first");

	assert_equal("c:\\windows", find_parent_search(df::search_t::parse("c:\\windows @photo")).parent.text(),
	             "parent then drops the media type");

	// A place inside a state inside a country steps straight out to the country. Location terms are
	// written most specific first, so dropping the last term would widen the broadest end instead.
	const auto place = df::search_t()
	                   .location("London", df::location_level::place)
	                   .location("England", df::location_level::state)
	                   .location("United Kingdom", df::location_level::country);
	const auto place_parent = find_parent_search(place).parent;
	assert_equal(1, static_cast<int>(place_parent.terms().size()), "parent keeps one location term");
	assert_equal("United Kingdom", place_parent.terms().front().text, "parent keeps the country");

	// The country is then the whole location, so the next step drops it.
	assert_equal(true, find_parent_search(place_parent).parent.terms().empty(), "parent of a country drops it");
}

static void should_escape()
{
	const auto base_folder = std::string(test_files_folder.text());

	null_state_strategy ss;
	null_async_strategy as;
	const location_cache locations;
	const view_host_base_ptr view;
	index_state index(as, locations);
	view_state s(ss, as, index, make_test_player());

	df::index_roots roots;
	roots.folders = {test_files_folder};
	s.item_index.index_roots(roots);
	s.item_index.index_folders(test_token);

	s.view_mode(view_type::items);
	s.open(view, base_folder + "\\**"s);
	s.is_full_screen = true;
	assert_equal(true, s.escape(view), "fullscreen escape handled");
	assert_equal(1, ss.toggle_full_screen_count, "fullscreen escape toggled fullscreen");
	assert_equal(base_folder + "\\**"s, s.search().text(), "fullscreen escape preserves search");
	s.is_full_screen = false;
	s.escape(view);
	assert_equal(base_folder, s.search().text(), "parent escape **");
}

static void should_find_closest_location()
{
	const auto& locations = test_locations();

	assert_equal("Bread Street", locations.find_closest(51.5142, -000.0985).place, "City");
	assert_equal("Armidale", locations.find_closest(-30.515, 151.665).place, "City");
	assert_equal("Johannesburg", locations.find_closest(-26.204444, 28.045556).place, "City");
	assert_equal("Santiago", locations.find_closest(-33.45, -70.666667).place, "City");
	assert_equal("Eastern Parkway", locations.find_closest(40.664167, -73.938611).place, "City");
	assert_equal("Beijing", locations.find_closest(39.913889, 116.391667).place, "City");
}

static void should_offset_localized_name()
{
	// Issue #119: bit unset (or no language selected) resolves to the default name (offset 0).
	assert_equal(0, location_localized_name_offset(0u, 2), "no bits set");
	assert_equal(0, location_localized_name_offset(0b0010u, 2), "requested bit unset");
	assert_equal(0, location_localized_name_offset(0xFFFFFFFFu, -1), "no language selected");
	assert_equal(0, location_localized_name_offset(0xFFFFFFFFu, 32), "bit out of range");

	// Only the requested bit set -> first localized name (offset 1).
	assert_equal(1, location_localized_name_offset(0b0001u, 0), "first bit");
	assert_equal(1, location_localized_name_offset(0b0100u, 2), "single higher bit");

	// Lower set bits push the localized name further along, one column per earlier name.
	assert_equal(2, location_localized_name_offset(0b0101u, 2), "one lower bit");
	assert_equal(3, location_localized_name_offset(0b0111u, 2), "two lower bits");

	// Highest bit works and counts all lower set bits.
	assert_equal(1, location_localized_name_offset(1u << 31, 31), "top bit alone");
	assert_equal(2, location_localized_name_offset((1u << 31) | 1u, 31), "top bit with one lower");
}

static void should_read_place_qualification_level()
{
	// locations.md 2.1/2.2: the flags column carries the smallest name form that identifies
	// the place. Levels are assigned by tools/generate_locations.py over the emitted records.
	auto& locations = test_locations();

	const auto level = [&locations](const std::string_view query)
	{
		return static_cast<int>(locations.find_by_name(query).qualification());
	};

	// A name held by exactly one record needs no qualifier.
	assert_equal(0, level("Reykjavík"), "unique name is level 0");
	assert_equal(0, level("Ouagadougou"), "unique name is level 0");

	// London collides across countries but is unique within each, so the country is enough.
	assert_equal(1, level("London"), "name unique within its country is level 1");
	assert_equal(1, level("London, Canada"), "the qualifier does not change the level");

	// Springfield and Toronto collide inside one country, so the region is also needed.
	assert_equal(2, level("Springfield"), "name repeated within a country is level 2");
	assert_equal(2, level("Toronto"), "name repeated within a country is level 2");

	// Populated places are never extent features; bits 3-31 are reserved and read as zero.
	assert_equal(false, locations.find_by_name("London").is_extent(), "a place is not an extent feature");
	assert_equal(false, locations.find_by_name("Reykjavík").is_extent(), "a place is not an extent feature");

	// The reserved level encoding 3 degrades to level 2, so a newer file over-qualifies
	// rather than mislabels.
	location_t reserved;
	reserved.flags = 3u;
	assert_equal(2, static_cast<int>(reserved.qualification()), "reserved level reads as level 2");

	location_t extent;
	extent.flags = location_flag_extent;
	assert_equal(0, static_cast<int>(extent.qualification()), "the extent bit does not disturb the level");
	assert_equal(true, extent.is_extent(), "the extent bit is readable");
}

static void should_compose_qualified_place_name()
{
	// locations.md 2.3: one composer, driven by the qualification level, using the country's
	// short common form.
	auto& locations = test_locations();

	const auto name = [&locations](const std::string_view query)
	{
		return qualified_name(locations.find_by_name(query));
	};

	// Level 0 carries no qualifier at all.
	assert_equal("Reykjavík"s, name("Reykjavík"), "level 0 is the bare name");
	assert_equal("King of Prussia"s, name("King of Prussia"), "level 0 is the bare name");

	// Level 1 adds the country and nothing else, so the region is omitted even though it is known.
	assert_equal("London, United Kingdom"s, name("London"), "level 1 is name and country");
	assert_equal("London, Canada"s, name("London, Canada"), "level 1 is name and country");
	assert_equal("Birmingham, United Kingdom"s, name("Birmingham, United Kingdom"), "level 1 is name and country");

	// The short common form, not the long official one.
	assert_equal("United Kingdom"s, std::string(locations.find_by_name("London").country.sv()),
	             "the country is the short common form");

	// Level 2 needs the region to separate same-country namesakes.
	assert_equal("Reno, Nevada, United States"s, name("Reno, Nevada"), "level 2 is name, region and country");

	// A repeated part is dropped and the level is treated as satisfied.
	location_t city_state;
	city_state.place = "Singapore"_c;
	city_state.state = "Singapore"_c;
	city_state.country = "Singapore"_c;
	city_state.flags = 2u;
	assert_equal("Singapore"s, qualified_name(city_state), "a city-state does not repeat itself");

	location_t place_is_region;
	place_is_region.place = "Luxembourg"_c;
	place_is_region.state = "Luxembourg"_c;
	place_is_region.country = "Luxembourg"_c;
	place_is_region.flags = 2u;
	assert_equal("Luxembourg"s, qualified_name(place_is_region), "a place equal to its region does not repeat it");

	// A record with no place name still labels itself.
	location_t country_only;
	country_only.country = "Portugal"_c;
	assert_equal("Portugal"s, qualified_name(country_only), "a country hit labels itself");

	assert_equal(""s, qualified_name(location_t{}), "an empty location has no label");
}

static void should_bound_place_attribution()
{
	// locations.md 2.5: a place may label an item only within a radius scaled to its
	// significance, so a photo taken far from anywhere is not confidently mislabelled.
	assert_equal(100.0, location_attribution_radius_km(8961989.0), "a megacity reaches 100km");
	assert_equal(100.0, location_attribution_radius_km(1000000.0), "the 1M boundary is inclusive");
	assert_equal(50.0, location_attribution_radius_km(999999.0), "just below 1M reaches 50km");
	assert_equal(25.0, location_attribution_radius_km(10000.0), "the 10k boundary is inclusive");
	assert_equal(15.0, location_attribution_radius_km(1000.0), "the 1k boundary is inclusive");
	assert_equal(10.0, location_attribution_radius_km(0.0), "an unknown population reaches 10km");
	assert_equal(300.0, location_max_attribution_km, "no place ever reaches beyond three max radii");

	auto& locations = test_locations();

	// Step 2: the nearest place is close enough to stand for the item.
	const auto london = locations.find_attributed(51.5142, -0.0985);
	assert_equal(static_cast<int>(location_attribution::at), static_cast<int>(london.attribution),
	             "a city centre is At its place");
	assert_equal("Bread Street", london.place.place, "At keeps the nearest place");

	const auto singapore = locations.find_attributed(1.291985, 103.866511);
	assert_equal(static_cast<int>(location_attribution::at), static_cast<int>(singapore.attribution),
	             "a Singapore photo is At its nearest feature");
	assert_equal("Marina Bay", singapore.place.place, "item attribution keeps the nearest feature");
	assert_equal("Singapore"s,
	             qualified_name(locations.find_largest_attributed(1.291985, 103.866511)),
	             "a map cluster prefers the recognizable city");

	// Step 3: outside its radius but within three, on land. It is still located, and it still
	// carries the place identity so rural photography groups instead of shattering.
	const auto outback = locations.find_attributed(-25.153, 131.75);
	assert_equal(static_cast<int>(location_attribution::near), static_cast<int>(outback.attribution),
	             "20km from a hamlet is Near it");
	assert_equal("Curtin Springs", outback.place.place, "Near keeps the place identity");
	assert_equal(true, outback.is_located(), "Near is located");

	// Step 5: land, nothing within three radii. The country is still a true answer.
	const auto sahara = locations.find_attributed(23.0, 15.0);
	assert_equal(static_cast<int>(location_attribution::remote), static_cast<int>(sahara.attribution),
	             "220km from a small town is Remote");
	assert_equal(true, str::is_empty(sahara.place.place), "Remote names no place");
	assert_equal("Libya", sahara.place.country, "Remote still names a country when it can");
	assert_equal("Libya"s, qualified_name(sahara.place), "a Remote label is the country alone");

	const auto iceland = locations.find_attributed(64.85, -18.6);
	assert_equal(static_cast<int>(location_attribution::remote), static_cast<int>(iceland.attribution),
	             "96km from a 19k town is Remote");
	assert_equal("Iceland", iceland.place.country, "Remote still names a country when it can");

	// Issue #119: the out-param country stays canonical because it doubles as a search term.
	{
		locations.set_display_language("de");
		const df::scope_exit restore_language([&locations] { locations.set_display_language({}); });
		country_loc canonical;
		const auto munich_area = locations.find_attributed(48.137, 11.575, &canonical);
		assert_equal("Deutschland", munich_area.place.country, "the displayed country is localized");
		assert_equal("Germany", canonical.name, "the search country stays canonical");
	}

	// Baseline defect 10: find_closest stays unbounded because 2.7's bearing descriptor needs
	// the nearest place at any distance, but it must no longer be what labels an item.
	const auto atlantic = locations.find_attributed(35.0, -45.0);
	assert_equal(static_cast<int>(location_attribution::remote), static_cast<int>(atlantic.attribution),
	             "mid-ocean is Remote");
	assert_equal(true, str::is_empty(atlantic.place.country), "mid-ocean claims no country");
	assert_equal(""s, qualified_name(atlantic.place), "mid-ocean has no place label");
	assert_equal(false, str::is_empty(locations.find_closest(35.0, -45.0).place),
	             "find_closest stays unbounded for the bearing descriptor");

	// An item with no coordinates is not located at all.
	assert_equal(static_cast<int>(location_attribution::none),
	             static_cast<int>(locations.find_attributed(gps_coordinate{}).attribution),
	             "no coordinate is not located");
}

static void should_describe_bearing()
{
	// locations.md 2.7: every item resolved at step 3, 4 or 5 also gets a secondary descriptor
	// computed from the nearest place regardless of radius. It is never a group key and never a
	// search term, so it exists only to answer "where was that?".
	const auto point = [](const double degrees) { return static_cast<int>(location_bearing_from_degrees(degrees)); };

	assert_equal(static_cast<int>(location_bearing::north), point(0.0), "0 degrees is north");
	assert_equal(static_cast<int>(location_bearing::north), point(22.4), "just under 22.5 is still north");
	assert_equal(static_cast<int>(location_bearing::north_east), point(22.5), "22.5 rounds up to north-east");
	assert_equal(static_cast<int>(location_bearing::east), point(90.0), "90 degrees is east");
	assert_equal(static_cast<int>(location_bearing::south_east), point(135.0), "135 degrees is south-east");
	assert_equal(static_cast<int>(location_bearing::south), point(180.0), "180 degrees is south");
	assert_equal(static_cast<int>(location_bearing::south_west), point(225.0), "225 degrees is south-west");
	assert_equal(static_cast<int>(location_bearing::west), point(270.0), "270 degrees is west");
	assert_equal(static_cast<int>(location_bearing::north_west), point(315.0), "315 degrees is north-west");
	assert_equal(static_cast<int>(location_bearing::north), point(350.0), "350 degrees wraps back to north");
	assert_equal(static_cast<int>(location_bearing::north_west), point(-45.0), "a negative bearing wraps");
	assert_equal(static_cast<int>(location_bearing::north), point(720.0), "a bearing beyond one turn wraps");

	// The angle runs from the named place towards the item, because the descriptor reads as a
	// direction a user would give: the item is north-west *of* the place.
	const gps_coordinate origin(10.0, 10.0);
	const auto from_origin = [origin](const gps_coordinate to)
	{
		return static_cast<int>(location_bearing_from_degrees(location_bearing_degrees(origin, to)));
	};

	assert_equal(static_cast<int>(location_bearing::north), from_origin(gps_coordinate(11.0, 10.0)), "due north");
	assert_equal(static_cast<int>(location_bearing::south), from_origin(gps_coordinate(9.0, 10.0)), "due south");
	assert_equal(static_cast<int>(location_bearing::east), from_origin(gps_coordinate(10.0, 11.0)), "due east");
	assert_equal(static_cast<int>(location_bearing::west), from_origin(gps_coordinate(10.0, 9.0)), "due west");
	assert_equal(static_cast<int>(location_bearing::north_west), from_origin(gps_coordinate(11.0, 9.0)), "north-west");
	assert_equal(static_cast<int>(location_bearing::south_east), from_origin(gps_coordinate(9.0, 11.0)), "south-east");

	// The composed string, pinned without depending on which gazetteer record wins.
	located_place composed;
	composed.attribution = location_attribution::remote;
	composed.nearest.place = "Lisbon"_c;
	composed.nearest.country = "Portugal"_c;
	composed.nearest.flags = 1u;
	composed.nearest_km = 410.0;
	composed.nearest_bearing = location_bearing::north_west;
	assert_equal("410 km NW of Lisbon, Portugal"s, bearing_descriptor(composed),
	             "the descriptor is distance, compass point and qualified place");

	composed.nearest_bearing = location_bearing::south;
	composed.nearest_km = 0.4;
	assert_equal("400 m S of Lisbon, Portugal"s, bearing_descriptor(composed),
	             "the descriptor shares the distance format the user types");

	// An item that is At its place is already answered by the place name.
	composed.attribution = location_attribution::at;
	assert_equal(""s, bearing_descriptor(composed), "an item At its place carries no bearing");
	assert_equal(""s, bearing_descriptor({}), "an unlocated item carries no bearing");

	auto& locations = test_locations();

	// find_closest stays unbounded precisely so a Remote item can still say where it was.
	const auto atlantic = locations.find_attributed(35.0, -45.0);
	const auto atlantic_text = bearing_descriptor(atlantic);
	assert_equal(false, str::is_empty(atlantic.nearest.place), "Remote still resolves a nearest place");
	assert_equal(true, atlantic.nearest_km > location_max_attribution_km,
	             "the nearest place is beyond every attribution radius");
	assert_equal(true, atlantic_text.find(qualified_name(atlantic.nearest)) != std::string::npos,
	             "the descriptor names the nearest place regardless of radius");

	const auto outback = locations.find_attributed(-25.153, 131.75);
	assert_equal(false, bearing_descriptor(outback).empty(), "a Near item carries a bearing");

	// Step 2 keeps the nearest record too, so nothing downstream has to resolve it a second time.
	const auto london = locations.find_attributed(51.5142, -0.0985);
	assert_equal("Bread Street", london.nearest.place, "At records the nearest place as well");
	assert_equal(""s, bearing_descriptor(london), "an item At its place carries no bearing");
}

// locations.md 3.5: `@remote` is a built-in location class -- a closed word, no argument -- that
// selects the items step 5 of the attribution ladder refuses to name.
static void should_select_remote_items()
{
	assert_equal("@remote", df::search_t::parse("@remote").format_terms(), "the canonical spelling");
	assert_equal("@remote", df::search_t::parse("loc:remote").format_terms(),
	             "the guessable spelling canonicalizes to the class");
	assert_equal("-@remote", df::search_t::parse("-@remote").format_terms(), "negation");
	assert_equal(search_presence_mask::location,
	             df::search_t::parse("@remote").calc_required_presence().types,
	             "only a coordinate can be remote");

	auto& locations = test_locations();

	const auto is_remote = [&locations](const gps_coordinate coord, const str::cached stored_place)
	{
		df::index_file_item file;
		file.ft = files::file_type_from_name("test.jpg");
		const auto md = file.safe_ps();
		md->coordinate = coord;
		md->location_place = stored_place;
		file.calc_search_presence();

		const auto search = df::search_t::parse("@remote");
		const df::search_matcher matcher(search, platform::now().to_days(), &locations);
		return matcher.match_item({}, file).is_match();
	};

	assert_equal(true, is_remote({23.0, 15.0}, {}), "the Sahara is remote");
	assert_equal(false, is_remote({51.5142, -0.0985}, {}), "central London is not remote");
	assert_equal(false, is_remote({-25.153, 131.75}, {}), "Near a place is not remote");
	assert_equal(false, is_remote({23.0, 15.0}, "Camp"_c), "a stored place name is the user's own answer");
	assert_equal(false, is_remote({}, {}), "an item with no coordinate is not remote");
}

// locations.md 3.7: a location query that finds nothing must explain itself and offer the next
// action. Each action is a normal search, so Back reverses it like any other navigation.
static void should_recover_from_an_empty_location_search()
{
	const auto search = df::search_t::parse("loc:\"London, Canada, 2km\"");
	const auto* const term = search.single_place_term();

	assert_equal(true, term != nullptr, "the empty state only speaks for a single place term");
	assert_equal("London, Canada", term->text, "the resolved centre is what the message names");
	assert_equal(2.0, term->float_val, "and the radius it searched");

	// Widen to the next detent -- the single most likely fix, one click.
	const auto wider = location_distance_at_detent(location_nearest_distance_detent(term->float_val) + 1);
	assert_equal(5.0, wider, "2 km widens to 5 km");

	auto widened = search;
	widened.set_place_distance(wider);
	assert_equal("loc:London, Canada, 5km", widened.format_terms(), "widening keeps the place");
	assert_equal(location_distance_at_detent(std::size(location_distance_detents_km) - 1),
	             location_distance_at_detent(std::size(location_distance_detents_km)),
	             "the widest detent has nothing above it to offer");

	// Drop the qualifier when the name resolved to the wrong namesake.
	auto all_named = search;
	all_named.set_place_name("London");
	assert_equal("loc:London, 2km", all_named.format_terms(), "dropping the qualifier keeps the radius");

	// Never let a user mistake "not placed" for "not present".
	auto unplaced = search;
	unplaced.clear_terms();
	unplaced.without(df::search_term_type::has_location);
	assert_equal(true, unplaced.single_place_term() == nullptr, "the location term is gone");

	const auto has_no_location = [&unplaced](const gps_coordinate coord, const str::cached stored_place)
	{
		df::index_file_item file;
		file.ft = files::file_type_from_name("test.jpg");
		const auto md = file.safe_ps();
		md->coordinate = coord;
		md->location_place = stored_place;
		file.calc_search_presence();

		const df::search_matcher matcher(unplaced);
		return matcher.match_item({}, file).is_match();
	};

	assert_equal(true, has_no_location({}, {}), "neither text nor coordinates");
	assert_equal(false, has_no_location({51.5142, -0.0985}, {}), "coordinates are a location");
	assert_equal(false, has_no_location({}, "London"_c), "stored text is a location");
}

// locations.md 5.1: a map area resolves to a place plus a radius. `area:` was an internal
// heat-map concept leaking into the user's address bar; "within 25 km of London" is not.
static void should_open_a_map_area_as_a_place_and_radius()
{
	map_location_area area;
	area.name = "London, United Kingdom";
	area.position = gps_coordinate(51.5, -0.1);
	area.place_position = gps_coordinate(51.5142, -0.0985);
	area.min_latitude = 51.4;
	area.max_latitude = 51.6;
	area.min_longitude = -0.2;
	area.max_longitude = 0.0;

	const auto km = area.search_radius_km();
	const auto furthest = area.place_position.distance_in_kilometers(gps_coordinate(51.4, 0.0));

	assert_equal(true, km >= furthest, "the radius never drops an item the area contained");
	assert_equal(true, location_distance_at_detent(location_distance_detent_at_least(furthest) - 1) < furthest,
	             "and is the smallest detent that covers it");

	// A degenerate area still gets a usable radius rather than a zero one that matches nothing.
	map_location_area point;
	point.position = point.place_position = gps_coordinate(51.5, -0.1);
	point.min_latitude = point.max_latitude = 51.5;
	point.min_longitude = point.max_longitude = -0.1;
	assert_equal(location_distance_at_detent(0), point.search_radius_km(),
	             "a degenerate area uses the smallest detent");

	// What the click opens: a resolved place term the distance slider can then widen.
	auto named = df::search_t();
	named.location(area.name, df::location_level::any).set_place_distance(km);

	const auto* const term = named.single_place_term();
	assert_equal(true, term != nullptr, "the map produces a place term the slider can drive");
	assert_equal("London, United Kingdom", term->text, "qualified, so the right London is searched");
	assert_equal(km, term->float_val, "carrying the area's radius");
	assert_equal(25.0, km, "these bounds round up to the 25 km detent");
	assert_equal("loc:\"London, United Kingdom, 25km\"", named.format_terms(),
	             "and canonicalizes to a loc: term, never an area: term");

	// Nothing near enough to name still opens exactly what was clicked, by coordinate.
	auto unnamed = df::search_t();
	unnamed.location(area.position, km);
	assert_equal(true, unnamed.single_place_term() == nullptr, "a coordinate term names no place");
	assert_equal(true, unnamed.has_term_type(df::search_term_type::location), "but is still a location search");
}

// locations.md 3.8, baseline defect 7: a location group header must reproduce its own group.
static void should_reproduce_a_location_group_from_its_header()
{
	// The link the header builds for `London` inside `England, United Kingdom`.
	auto search = df::search_t()
	              .location("London", df::location_level::place)
	              .location("England", df::location_level::state);

	const auto matches = [&search](const str::cached place, const str::cached state)
	{
		df::index_file_item file;
		file.ft = files::file_type_from_name("test.jpg");
		const auto md = file.safe_ps();
		md->location_place = place;
		md->location_state = state;
		file.calc_search_presence();

		const df::search_matcher matcher(search);
		return matcher.match_item({}, file).is_match();
	};

	assert_equal(true, matches("London"_c, "England"_c), "the group's own items come back");
	assert_equal(false, matches("London"_c, "Ontario"_c), "the other London does not");
	assert_equal(false, matches("Bristol"_c, "England"_c), "nor the rest of the region");
}

static void should_defer_height_classes()
{
	assert_equal(std::string_view{}, df::search_t::parse("@flying").format_terms(), "flying is deferred");
	assert_equal(std::string_view{}, df::search_t::parse("@altitude").format_terms(), "altitude is deferred");
	assert_equal(std::string_view{}, df::search_t::parse("@underwater").format_terms(), "underwater is deferred");
}

// locations.md 4: the distance slider rewrites the radius of the one location term it controls.
// The detent ladder and the term it applies to are both pure, so both are pinned here.
static void should_step_search_distance()
{
	assert_equal(0.1, location_distance_at_detent(0), "the smallest detent");
	assert_equal(100.0, location_distance_at_detent(location_distance_detent_count - 1), "the largest detent");
	assert_equal(0.1, location_distance_at_detent(-5), "below the ladder clamps");
	assert_equal(100.0, location_distance_at_detent(99), "above the ladder clamps");

	assert_equal(0, location_nearest_distance_detent(0.0), "no radius sits at the smallest detent");
	assert_equal(3, location_nearest_distance_detent(1.0), "an exact detent is itself");
	assert_equal(4, location_nearest_distance_detent(2.0), "an exact detent is itself");
	assert_equal(3, location_nearest_distance_detent(1.4), "1.4 km is nearer 1 km on a ratio scale");
	assert_equal(4, location_nearest_distance_detent(1.5), "1.5 km is nearer 2 km on a ratio scale");
	assert_equal(4, location_nearest_distance_detent(3.0), "3 km is nearer 2 km than 5 km");
	assert_equal(5, location_nearest_distance_detent(3.5), "3.5 km is nearer 5 km");
	assert_equal(9, location_nearest_distance_detent(1000.0), "beyond the ladder clamps");

	assert_equal(3, location_distance_detent_at_least(1.0), "an exact detent needs no widening");
	assert_equal(5, location_distance_detent_at_least(3.0), "3 km rounds up to 5 km, never down");
	assert_equal(9, location_distance_detent_at_least(1000.0), "beyond the ladder clamps");

	assert_equal("100 m", format_distance_km(0.1), "below a kilometre reads in metres");
	assert_equal("250 m", format_distance_km(0.25), "below a kilometre reads in metres");
	assert_equal("1 km", format_distance_km(1.0), "a whole kilometre drops the fraction");
	assert_equal("2.5 km", format_distance_km(2.5), "a small fraction is kept");
	assert_equal("100 km", format_distance_km(100.0), "the widest detent");

	const auto place = df::search_t::parse("loc:\"London, 2km\"");
	assert_equal(true, place.single_place_term() != nullptr, "one named place offers a radius");
	assert_equal(2.0, place.single_place_term()->float_val, "the parsed radius in km");

	assert_equal(true, df::search_t::parse("loc:London").single_place_term() != nullptr,
	             "a place with no radius still offers one");
	assert_equal(true, df::search_t::parse("country:France").single_place_term() != nullptr,
	             "a level-constrained place is still a place");
	assert_equal(false, df::search_t::parse("loc:+51.5-0.09+10").single_place_term() != nullptr,
	             "a coordinate the user typed is not widened for them");
	assert_equal(false, df::search_t::parse("loc:London loc:Paris").single_place_term() != nullptr,
	             "two places have no single radius");
	assert_equal(false, df::search_t::parse("@remote").single_place_term() != nullptr,
	             "a built-in class takes no distance");
	assert_equal(false, df::search_t::parse("cat").single_place_term() != nullptr,
	             "a search with no location has no slider");

	auto widened = df::search_t::parse("loc:\"London, 2km\"");
	widened.set_place_distance(10.0);
	assert_equal("loc:London, 10km", widened.format_terms(), "widening rewrites the canonical term");

	auto added = df::search_t::parse("loc:London");
	added.set_place_distance(0.5);
	assert_equal("loc:London, 500m", added.format_terms(), "a radius can be added to a bare place");
}

static void should_resolve_location_vocabulary()
{
	auto& locations = test_locations();

	const auto boulder_city_coord = gps_coordinate(35.9786, -114.8325);

	// A GPS-only item: no stored place, region or country field at all.
	df::index_file_item gps_only;
	gps_only.ft = files::file_type_from_name("test.jpg");
	gps_only.safe_ps()->coordinate = boulder_city_coord;

	// An item with stored text but no coordinates.
	df::index_file_item text_only;
	text_only.ft = files::file_type_from_name("test.jpg");
	const auto text_only_metadata = text_only.safe_ps();
	text_only_metadata->location_place = "Boulder City"_c;
	text_only_metadata->location_state = "Nevada"_c;
	text_only_metadata->location_country = "United States"_c;

	const auto matches = [&locations](const std::string_view query, const df::index_file_item& file)
	{
		const auto search = df::search_t::parse(query);
		const df::search_matcher matcher(search, platform::now().to_days(), &locations);
		return matcher.match_item({}, file).is_match();
	};

	// The defect this fixes: the most guessable spelling used to read the raw field only.
	assert_equal(true, matches("place:\"Boulder City\"", gps_only),
	             "place resolves a location for a GPS-only item");
	assert_equal(true, matches("city:\"Boulder City\"", gps_only), "city is a synonym of place");
	assert_equal(true, matches("state:Nevada", gps_only), "state resolves for a GPS-only item");
	assert_equal(true, matches("country:\"United States\"", gps_only), "country resolves for a GPS-only item");
	assert_equal(true, matches("countries:\"United States\"", gps_only), "countries is a synonym of country");
	assert_equal(true, matches("near:\"Boulder City\"", gps_only), "near is a synonym of loc");

	// Level constraints: the point of keeping the narrower spellings.
	assert_equal(false, matches("state:\"Boulder City\"", gps_only), "a place does not match at region level");
	assert_equal(false, matches("place:Nevada", gps_only), "a region does not match at place level");
	assert_equal(false, matches("place:\"United States\"", gps_only), "a country does not match at place level");
	assert_equal(true, matches("loc:Nevada", gps_only), "loc matches at any level");

	// Stored text is authoritative and needs no coordinates.
	assert_equal(true, matches("place:\"Boulder City\"", text_only), "stored place matches without coordinates");
	assert_equal(true, matches("country:\"United States\"", text_only), "stored country matches without coordinates");
	assert_equal(false, matches("place:Reno", text_only), "a different place does not match");

	// Negation still composes.
	assert_equal(false, matches("-place:\"Boulder City\"", gps_only), "negated location term excludes the item");
	assert_equal(true, matches("-place:Reno", gps_only), "negated non-matching location term keeps the item");

	// locations.md 2.3: a completion commits the gazetteer's qualified name, which is labelled at
	// the record's own level. London omits its region; the index still knows the item as England.
	df::index_file_item london;
	london.ft = files::file_type_from_name("test.jpg");
	london.safe_ps()->coordinate = gps_coordinate(51.5142, -0.0985);

	assert_equal("loc:\"London, United Kingdom\"",
	             df::search_t().location("London, United Kingdom", df::location_level::any).format_terms(),
	             "the term the address bar commits for a London completion");
	assert_equal(true, matches("loc:\"London, United Kingdom\"", london),
	             "a name qualified to its country matches an item qualified to its region");
	assert_equal(true, matches("loc:London", london), "the bare name still matches");
	assert_equal(false, matches("loc:\"London, Ontario\"", london), "and the other London still does not");
	assert_equal(false, matches("loc:\"Reno, United States\"", london), "nor a qualifier that holds without the place");

	// The reach stops at the record that named the item, so a town near a metropolis keeps its own
	// identity rather than being swallowed by it.
	df::index_file_item windsor;
	windsor.ft = files::file_type_from_name("test.jpg");
	windsor.safe_ps()->coordinate = gps_coordinate(51.483, -0.6);

	assert_equal(true, matches("loc:Windsor", windsor), "the town answers for itself");
	assert_equal(false, matches("loc:\"London, United Kingdom\"", windsor), "35 km away is not in London");
}

static void should_match_location_radius_and_presence()
{
	auto& locations = test_locations();

	const auto boulder_city_coord = gps_coordinate(35.9786, -114.8325);

	df::index_file_item gps_only;
	gps_only.ft = files::file_type_from_name("test.jpg");
	gps_only.safe_ps()->coordinate = boulder_city_coord;

	df::index_file_item text_only;
	text_only.ft = files::file_type_from_name("test.jpg");
	const auto text_only_metadata = text_only.safe_ps();
	text_only_metadata->location_place = "Boulder City"_c;
	text_only_metadata->location_state = "Nevada"_c;
	text_only_metadata->location_country = "United States"_c;

	df::index_file_item unlocated;
	unlocated.ft = files::file_type_from_name("test.jpg");
	unlocated.safe_ps()->title = "no location here"_c;

	const auto matches = [&locations](const std::string_view query, const df::index_file_item& file)
	{
		const auto search = df::search_t::parse(query);
		const df::search_matcher matcher(search, platform::now().to_days(), &locations);
		return matcher.match_item({}, file).is_match();
	};

	// A trailing distance component is a radius, not part of the name.
	const auto is_km = [](const double actual, const double expected)
	{
		return std::fabs(actual - expected) < 0.0001;
	};

	const auto radius_search = df::search_t::parse("loc:\"Boulder City, 10km\"");
	assert_equal(1, static_cast<int>(radius_search.terms().size()), "radius query is one term");
	assert_equal("Boulder City", std::string(radius_search.terms()[0].text), "distance is stripped from the name");
	assert_equal(true, is_km(radius_search.terms()[0].float_val, 10.0), "distance parsed as kilometres");

	assert_equal(true, is_km(df::search_t::parse("loc:\"Boulder City, 1000m\"").terms()[0].float_val, 1.0),
	             "metres convert to kilometres");
	assert_equal(true, is_km(df::search_t::parse("loc:\"Boulder City, 1mi\"").terms()[0].float_val, 1.609344),
	             "miles convert to kilometres");
	assert_equal(true, is_km(df::search_t::parse("loc:\"Boulder City, 1 miles\"").terms()[0].float_val, 1.609344),
	             "the plural unit is accepted");

	// A component that does not parse completely stays part of the name.
	assert_equal("Boulder City, 10 furlongs", std::string(df::search_t::parse("loc:\"Boulder City, 10 furlongs\"").
		             terms()[0].text),
	             "an unrecognised unit is not a distance");
	assert_equal("Nevada, United States", std::string(df::search_t::parse("loc:\"Nevada, United States\"").terms()[0].
		             text),
	             "an ordinary qualifier is not a distance");

	// Radius matching needs coordinates; stored text alone can never satisfy one.
	assert_equal(true, matches("loc:\"Boulder City, 10km\"", gps_only), "an item inside the radius matches");
	assert_equal(false, matches("loc:\"Boulder City, 1km\"", text_only),
	             "stored text without coordinates never satisfies a radius");
	assert_equal(true, matches("loc:\"Boulder City\"", text_only), "the same query without a radius still matches");
	assert_equal(false, matches("loc:\"Reno, 10km\"", gps_only), "an item outside the radius does not match");

	// A bare name resolves to its canonical record: exact name, largest population
	// (locations.md baseline defect 1).
	const auto reno = locations.find_by_name("Reno");
	assert_equal("Reno/United States", std::string(reno.place.sv()) + "/" + std::string(reno.country.sv()),
	             "a bare name resolves to its canonical record");
	assert_equal("Reno/United States", std::string(locations.find_by_name("Reno, Nevada").place.sv()) + "/" +
	             std::string(locations.find_by_name("Reno, Nevada").country.sv()),
	             "a region qualifier selects the same record");
	assert_equal(true, str::is_empty(locations.find_by_name("Reno, France").place),
	             "a qualifier that does not hold resolves nothing");
	assert_equal(true, matches("loc:\"Reno, 800km\"", gps_only), "a wide enough radius reaches the item");

	// A region or country has extent, not a centre, so it takes no radius.
	assert_equal("Nevada, 10km", std::string(df::search_t::parse("state:\"Nevada, 10km\"").terms()[0].text),
	             "state does not take a distance");

	// without:location is about having no location at all, not about the stored field.
	assert_equal(true, matches("without:location", unlocated), "an item with neither text nor coordinates matches");
	assert_equal(false, matches("without:location", gps_only), "coordinates alone count as a location");
	assert_equal(false, matches("without:location", text_only), "stored text alone counts as a location");
	assert_equal(true, matches("with:location", gps_only), "with:location is the complement");
	assert_equal(false, matches("with:location", unlocated), "with:location excludes the unlocated item");
}

static void should_find_location()
{
	auto& locations = test_locations();
	// The display language is shared state; every early return below would otherwise leak it.
	const df::scope_exit restore_language([&locations] { locations.set_display_language({}); });

	const auto default_location = gps_coordinate(51.5255317687988, -0.116743430495262); // London

	assert_equal("City of London", locations.find_by_id(2643741).place, "City");
	assert_equal(true, locations.find_by_id(2643741).population > 0.0, "City population");
	assert_equal("King of Prussia", locations.find_by_id(5196220).place, "City");
	assert_equal("London", locations.find_largest(51.3, -0.5, 51.7, 0.3).place,
	             "largest population center inside photo bounds");
	const auto boulder_city_coord = gps_coordinate(35.9786, -114.8325);
	assert_equal("Boulder City", locations.find_closest(boulder_city_coord.latitude(),
	                                                    boulder_city_coord.longitude()).place,
	             "Boulder City reverse geocode");

	df::index_file_item boulder_city_file;
	boulder_city_file.ft = files::file_type_from_name("test.jpg");
	auto boulder_city_metadata = boulder_city_file.safe_ps();
	boulder_city_metadata->coordinate = boulder_city_coord;
	boulder_city_metadata->location_state = "Nevada"_c;
	boulder_city_metadata->location_country = "United States"_c;
	const auto boulder_city_search = df::search_t::parse("loc:\"Boulder City\"");
	const df::search_matcher boulder_city_matcher(boulder_city_search, platform::now().to_days(), &locations);
	assert_equal(true, boulder_city_matcher.match_item({}, boulder_city_file).is_match(),
	             "loc place uses map reverse geocode when stored place is empty");

	// locations.md 2.3: a prediction is labelled at its qualification level, not always fully.
	assert_equal("London, United Kingdom",
	             locations.auto_complete("london", 8, default_location)[0].location.str(), "City");
	assert_equal("London, Canada",
	             locations.auto_complete("london canada", 8, default_location)[0].location.str(), "City");
	assert_equal("Armidale",
	             locations.auto_complete("armid aust", 8, default_location)[0].location.str(), "City");
	assert_equal("Birmingham, United Kingdom",
	             locations.auto_complete("birm gb", 8, default_location)[0].location.str(), "City");
	assert_equal("King of Prussia",
	             locations.auto_complete("king pru usa", 8, default_location)[0].location.str(), "City");
	assert_equal("King of Prussia",
	             locations.auto_complete("king of prussia pennsylvania", 8, default_location)[0].location.str(),
	             "State");

	const auto czech_matches = locations.auto_complete("cz", 8, default_location);
	assert_equal(true, !czech_matches.empty(), "country code produces location predictions");
	assert_equal("Czechia"s, std::string(czech_matches.front().location.country), "CZ predicts Czechia");

	// Issue #119: the displayed place and country names follow the selected UI language.
	locations.set_display_language("de");
	const auto munich_de = locations.find_by_id(2867714);
	assert_equal("München", munich_de.place, "German place");
	assert_equal("Deutschland", munich_de.country, "German country");
	locations.set_display_language("es");
	const auto munich_es = locations.find_by_id(2867714);
	assert_equal("Múnich", munich_es.place, "Spanish place");
	assert_equal("Alemania", munich_es.country, "Spanish country");
	locations.set_display_language("zh");
	const auto munich_zh = locations.find_by_id(2867714);
	assert_equal("慕尼黑", munich_zh.place, "Chinese place");
	assert_equal("德国", munich_zh.country, "Chinese country");

	// Issue #119 regression guard: find_country() feeds the map/heat-map country grouping whose
	// label doubles as a SEARCH term (sidebar .with(name)). It must stay canonical (English)
	// regardless of the selected display language so a map click still matches the country name
	// stored in the index -- localizing it here previously broke map-click search in zh/de/etc.
	const auto grouping = locations.find_country(48.137, 11.575); // Munich, Germany
	assert_equal("Germany", grouping.name, "Country grouping key stays canonical while display is localized");
	country_loc combined_country;
	const auto combined_location = locations.find_closest(48.137, 11.575, &combined_country);
	assert_equal("慕尼黑", combined_location.place, "Combined lookup keeps localized place");
	assert_equal("Germany", combined_country.name, "Combined lookup keeps canonical country grouping key");

	locations.set_display_language("xx"); // unknown language falls back to the default names
	const auto munich_default = locations.find_by_id(2867714);
	assert_equal("Munich", munich_default.place, "Unknown language falls back");
	assert_equal("Germany", munich_default.country, "Default country");
	locations.set_display_language("en"); // English exonym equals the default name -> fallback
	assert_equal("Munich", locations.find_by_id(2867714).place, "English name");
}

static void should_contain_map_location_cells()
{
	const map_location_area area{.cell = {32, 48}, .cell_span = 4};
	assert_equal(true, area.contains({32, 48}));
	assert_equal(true, area.contains({35, 51}));
	assert_equal(false, area.contains({36, 48}));
	assert_equal(false, area.contains({32, 52}));
}

static void should_scale_map_location_cells()
{
	assert_equal(16, map_location_cell_span(0, 220), "default span before map layout");
	assert_equal(8, map_location_cell_span(256, 220), "world map uses regional areas");
	assert_equal(4, map_location_cell_span(85, 220), "regional crop uses local areas");
	assert_equal(2, map_location_cell_span(45, 220), "tight crop exposes finer areas");
	assert_equal(1, map_location_cell_span(20, 220), "closest crop uses individual cells");
}

static void should_average_map_location_coordinates()
{
	index_histograms histograms;
	constexpr auto map_width = static_cast<int>(df::location_heat_map::map_width);
	constexpr auto first = 48 * map_width + 32;
	constexpr auto second = 49 * map_width + 33;
	histograms._locations.coordinates[first] = 2;
	histograms._location_latitude_sums[first] = 20.0;
	histograms._location_longitude_sums[first] = 40.0;
	histograms._location_min_latitudes[first] = 8.0;
	histograms._location_min_longitudes[first] = 18.0;
	histograms._location_max_latitudes[first] = 12.0;
	histograms._location_max_longitudes[first] = 22.0;
	histograms._locations.coordinates[second] = 1;
	histograms._location_latitude_sums[second] = 40.0;
	histograms._location_longitude_sums[second] = 80.0;
	histograms._location_min_latitudes[second] = 40.0;
	histograms._location_min_longitudes[second] = 80.0;
	histograms._location_max_latitudes[second] = 40.0;
	histograms._location_max_longitudes[second] = 80.0;

	const auto areas = histograms.map_locations(4);
	assert_equal(1_z, areas.size(), "cells fold into one map area");
	assert_equal(3u, areas.front().count, "area contains all photos");
	assert_equal(20.0, areas.front().position.latitude(), "latitude is weighted by photo count");
	assert_equal(40.0, areas.front().position.longitude(), "longitude is weighted by photo count");
	assert_equal(8.0, areas.front().min_latitude, "area tracks southern photo bound");
	assert_equal(18.0, areas.front().min_longitude, "area tracks western photo bound");
	assert_equal(40.0, areas.front().max_latitude, "area tracks northern photo bound");
	assert_equal(80.0, areas.front().max_longitude, "area tracks eastern photo bound");
}

static void should_resolve_named_map_area_on_demand()
{
	const auto& locations = test_locations();
	index_histograms histograms;
	const gps_coordinate munich(48.137, 11.575);
	const auto cell = df::location_heat_map::calc_map_loc(munich);
	const auto map_index = cell.y * df::location_heat_map::map_width + cell.x;
	histograms._locations.coordinates[map_index] = 1;
	histograms._location_latitude_sums[map_index] = munich.latitude();
	histograms._location_longitude_sums[map_index] = munich.longitude();
	histograms._location_min_latitudes[map_index] = 48.0;
	histograms._location_min_longitudes[map_index] = 11.4;
	histograms._location_max_latitudes[map_index] = 48.3;
	histograms._location_max_longitudes[map_index] = 11.8;

	const auto area = histograms.find_map_location("Munich", locations, munich);
	assert_equal(true, area.has_value(), "name-only area is reconstructed from histogram");
	assert_equal("Munich", area->name, "reconstructed area keeps its population-center name");
	assert_equal(1, area->cell_span, "on-demand resolution selects the finest matching bucket");
	assert_equal(true, area->contains(cell), "reconstructed area contains the named center");
	auto search = df::search_t::parse("area:Munich");
	search.resolve_area(*area);
	assert_equal(1, search.terms().front().location_cell_span, "saved area address receives geometry");
	assert_equal(true, cell == search.terms().front().location_cell, "saved area resolves to the photo bucket");
}

static void should_parse_map_area()
{
	const map_location_area area{.name = "Brisbane", .cell = {32, 48}, .cell_span = 4};
	auto search = df::search_t();
	search.area(area);
	assert_equal("area:Brisbane", search.format_terms(), "formatted map area");
}

static df::visit_sample visit_sample_at(const int y, const int m, const int d, const double lat, const double lon,
                                        const std::string_view place)
{
	df::visit_sample s;
	s.days = df::date_t(y, m, d).to_days();
	s.coordinate = gps_coordinate(lat, lon);
	if (!place.empty()) s.place = str::cache(place);
	return s;
}

static void should_derive_visits_from_a_result_set()
{
	const location_cache locations;
	df::visit_request request;

	// Two separated trips plus a run at home, all far enough apart in space to cluster apart.
	for (auto d = 1; d <= 10; ++d)
	{
		request.samples.emplace_back(visit_sample_at(2019, 6, d, 35.68, 139.69, "Tokyo"));
	}

	for (auto d = 1; d <= 8; ++d)
	{
		request.samples.emplace_back(visit_sample_at(2021, 3, d, -33.87, 151.21, "Sydney"));
	}

	for (auto d = 1; d <= 9; ++d)
	{
		request.samples.emplace_back(visit_sample_at(2023, 9, d, 51.51, -0.13, "London"));
	}

	const auto timeline = df::compute_visits(request, locations);

	assert_equal(27u, timeline.located_count, "located count");
	assert_equal(0u, timeline.undated_count, "undated count");
	assert_equal(3u, static_cast<uint32_t>(timeline.nodes.size()), "node count");
	assert_equal(true, timeline.publish, "publishes a timeline");

	// locations.md 6.2 step 7: chronological order after selection, never score order.
	assert_equal("Tokyo", timeline.nodes[0].name, "first node");
	assert_equal("Sydney", timeline.nodes[1].name, "second node");
	assert_equal("London", timeline.nodes[2].name, "third node");
	assert_equal(true, timeline.nodes[0].first < timeline.nodes[1].first, "nodes ordered by date");
	assert_equal(10u, timeline.nodes[0].count, "first node count");

	// locations.md 7.2: the same clustering counted by place.
	assert_equal(3u, static_cast<uint32_t>(timeline.places.size()), "place tallies");
	assert_equal("Tokyo", timeline.places[0].name, "largest place first");
	assert_equal(10u, timeline.places[0].count, "largest place count");
}

static void should_split_a_cluster_at_long_gaps()
{
	const location_cache locations;
	df::visit_request request;

	// The same place visited in two summers three years apart is two visits, not one long one.
	for (auto d = 1; d <= 12; ++d)
	{
		request.samples.emplace_back(visit_sample_at(2018, 7, d, 45.44, 12.34, "Venice"));
		request.samples.emplace_back(visit_sample_at(2021, 7, d, 45.44, 12.34, "Venice"));
	}

	const auto timeline = df::compute_visits(request, locations);

	assert_equal(2u, static_cast<uint32_t>(timeline.nodes.size()), "one node per summer");
	assert_equal(2018, timeline.nodes[0].first.year(), "first visit year");
	assert_equal(2021, timeline.nodes[1].first.year(), "second visit year");
	assert_equal(1u, static_cast<uint32_t>(timeline.places.size()), "one place");
	assert_equal(24u, timeline.places[0].count, "place holds both visits");
}

static void should_exclude_items_that_cannot_sit_on_a_timeline()
{
	const location_cache locations;
	df::visit_request request;

	for (auto d = 1; d <= 8; ++d)
	{
		request.samples.emplace_back(visit_sample_at(2020, 5, d, 48.85, 2.35, "Paris"));
		request.samples.emplace_back(visit_sample_at(2022, 1, d, 40.71, -74.01, "New York"));
	}

	// A date with no location, and a location with no date, are counted rather than invented
	// into a node (locations.md 6.5).
	auto dated_only = visit_sample_at(2020, 5, 20, 0.0, 0.0, {});
	dated_only.coordinate = {};
	request.samples.emplace_back(dated_only);

	auto located_only = visit_sample_at(2020, 5, 21, 48.85, 2.35, "Paris");
	located_only.days = 0;
	request.samples.emplace_back(located_only);

	const auto timeline = df::compute_visits(request, locations);

	assert_equal(16u, timeline.located_count, "located count excludes both");
	assert_equal(1u, timeline.undated_count, "undated count");
	assert_equal(1u, timeline.unlocated_count, "unlocated count");
	assert_equal(2u, static_cast<uint32_t>(timeline.nodes.size()), "node count");
}

// locations.md 7.2: a chip states a count and then runs a search. They are one promise, so the
// search has to return exactly what the chip counted -- whatever placed those items.
static void should_reproduce_a_place_breakdown_from_its_chip()
{
	auto& locations = test_locations();

	df::visit_request request;

	const auto stored = [](df::visit_sample s)
	{
		s.state = "Nevada"_c;
		s.country = "United States"_c;
		return s;
	};

	// A place whose items carry stored text, some of them with no coordinates at all.
	for (auto d = 1; d <= 10; ++d)
	{
		request.samples.emplace_back(stored(visit_sample_at(2019, 6, d, 35.9786 + d * 0.002,
		                                                    -114.8325 + d * 0.002, "Boulder City")));
	}

	for (auto d = 1; d <= 5; ++d)
	{
		auto s = stored(visit_sample_at(2019, 6, d, 0.0, 0.0, "Boulder City"));
		s.coordinate = {};
		request.samples.emplace_back(s);
	}

	// An undated item is still one of the results, and the chip's query carries no date.
	auto undated = stored(visit_sample_at(2019, 6, 1, 35.9786, -114.8325, "Boulder City"));
	undated.days = 0;
	request.samples.emplace_back(undated);

	// A place named only by attribution. It sits beside a much larger neighbour, close enough that
	// any circle drawn around it swallows that neighbour whole -- the case that made a chip read 5
	// and then return 560.
	for (auto d = 1; d <= 60; ++d)
	{
		request.samples.emplace_back(visit_sample_at(2021, 3, 1 + d % 28, 35.2325, 139.1069, {}));
	}

	for (auto d = 1; d <= 5; ++d)
	{
		request.samples.emplace_back(visit_sample_at(2021, 4, d, 35.0955, 138.8634, {}));
	}

	const auto timeline = df::compute_visits(request, locations);

	assert_equal(3u, static_cast<uint32_t>(timeline.places.size()), "three places");
	assert_equal(60u, timeline.places[0].count, "the large neighbour");
	assert_equal(16u, timeline.places[1].count, "an undated item is still in the place it was taken");
	assert_equal(5u, timeline.places[2].count, "the small place is not swallowed by the large one");

	// Every item that has a place is in exactly one entry, including the undated one, because the
	// chip's query carries no date either.
	auto tallied = 0u;
	for (const auto& place : timeline.places) tallied += place.count;
	assert_equal(81u, tallied, "the breakdown partitions the items that have a place");

	const auto count_matches = [&locations, &request](const df::search_t& search)
	{
		auto count = 0u;
		const df::search_matcher matcher(search, platform::now().to_days(), &locations);

		for (const auto& s : request.samples)
		{
			df::index_file_item file;
			file.ft = files::file_type_from_name("test.jpg");
			const auto md = file.safe_ps();
			md->coordinate = s.coordinate;
			md->location_place = s.place;
			md->location_state = s.state;
			md->location_country = s.country;
			file.calc_search_presence();

			if (matcher.match_item({}, file).is_match()) ++count;
		}

		return count;
	};

	for (const auto& place : timeline.places)
	{
		const auto search = df::visit_place_search(df::search_t(), place);
		assert_equal(place.count, count_matches(search), "the chip's search returns the count it displayed");
	}
}

// locations.md 7.2: two chips that read the same are one affordance the user cannot choose from,
// and a chip that omits a field runs a query that already returns the items a sharper chip counted.
static void should_tell_two_place_chips_apart()
{
	const location_cache locations;

	const auto sample = [](const int day, const char* place, const char* state, const char* country)
	{
		auto s = visit_sample_at(2022, 4, day, 0.0, 0.0, place);
		s.coordinate = {};
		s.state = str::cache(state);
		s.country = str::cache(country);
		return s;
	};

	df::visit_request request;

	// One place recorded three ways: the region GeoNames no longer names, the region a user typed,
	// and no region at all. The vaguest chip's query returns all three, so it is the only honest one.
	for (auto d = 1; d <= 9; ++d) request.samples.emplace_back(sample(d, "Singapore", "", "Singapore"));
	for (auto d = 1; d <= 4; ++d) request.samples.emplace_back(sample(d, "Singapore", "Singapore", "Singapore"));
	for (auto d = 1; d <= 2; ++d)
	{
		request.samples.emplace_back(sample(d, "Singapore", "Singapore (general)", "Singapore"));
	}

	// Two genuinely different places that share a name. Neither omits what the other names, so
	// both survive and both have to say which one they are.
	for (auto d = 1; d <= 8; ++d) request.samples.emplace_back(sample(d, "London", "England", "United Kingdom"));
	for (auto d = 1; d <= 3; ++d) request.samples.emplace_back(sample(d, "London", "Ontario", "Canada"));

	const auto timeline = df::compute_visits(request, locations);

	assert_equal(3u, static_cast<uint32_t>(timeline.places.size()), "three distinguishable places");
	assert_equal("Singapore"s, timeline.places[0].name, "one chip for one place");
	assert_equal(15u, timeline.places[0].count, "the vaguest chip absorbs what its query returns");
	assert_equal("London, United Kingdom"s, timeline.places[1].name, "a shared name is qualified");
	assert_equal("London, Canada"s, timeline.places[2].name, "and so is the place it collided with");
	assert_equal(8u, timeline.places[1].count, "the larger London");
	assert_equal(3u, timeline.places[2].count, "the smaller London");
}

static void should_suppress_an_era_until_the_query_names_it()
{
	df::visit_request request;

	// Five years of steady photos from one place is a residence, not a trip.
	for (auto y = 2015; y <= 2020; ++y)
	{
		for (auto m = 1; m <= 12; ++m)
		{
			for (auto d = 1; d <= 6; ++d)
			{
				request.samples.emplace_back(visit_sample_at(y, m, d, 51.51, -0.13, "London"));
			}
		}
	}

	{
		const location_cache locations;
		const auto suppressed = df::compute_visits(request, locations);
		assert_equal(0u, static_cast<uint32_t>(suppressed.nodes.size()), "era suppressed by default");
		assert_equal(false, suppressed.publish, "nothing published for an era alone");
	}

	request.intent_place = "London";

	{
		const location_cache locations;
		const auto revealed = df::compute_visits(request, locations);
		assert_equal(false, revealed.nodes.empty(), "era revealed when named");
		assert_equal(true, revealed.nodes[0].kind == df::visit_kind::era, "revealed node is an era");
	}
}

static void should_run_the_search_a_timeline_node_promises()
{
	df::visit_node node;
	node.name = "Tokyo, Japan";
	node.named = true;
	node.first = df::date_t(2019, 6, 12);
	node.last = df::date_t(2019, 6, 21);
	node.count = 10;
	node.radius_km = 25.0;
	node.centre = gps_coordinate(35.68, 139.69);

	assert_equal("Jun 2019", df::visit_node_dates(node), "node label inside one month");

	// locations.md 6.3: refining an existing query replaces its location and date scope rather
	// than intersecting with it, so clicking a second node moves the view instead of emptying it.
	auto current = df::search_t();
	current.location("Paris", df::location_level::any);
	current.year(2011);

	const auto search = df::visit_node_search(current, node);
	const auto formatted = search.format_terms();

	assert_equal(formatted, df::search_t::parse(formatted).format_terms(), "node search round-trips");
	assert_equal(true, df::is_visit_node_selected(search, node), "node latched by its own search");
	assert_equal(false, df::is_visit_node_selected(current, node), "node not latched by another search");

	// locations.md 6.5: the date bounds have to include their own end days, or the query a node
	// runs returns fewer items than the node displayed.
	const auto range = df::search_t().date_range(node.first.date(), node.last.date());

	const auto matches = [&range](const int y, const int m, const int d)
	{
		df::index_file_item file;
		file.ft = files::file_type_from_name("test.jpg");
		const auto md = file.safe_ps();
		md->created_utc = df::date_t(y, m, d, 12, 0, 0);
		file.calc_search_presence();

		const df::search_matcher matcher(range, platform::now().to_days());
		return matcher.match_item({}, file).is_match();
	};

	assert_equal(true, matches(2019, 6, 15), "item inside the range");
	assert_equal(true, matches(2019, 6, 12), "item on the first day");
	assert_equal(true, matches(2019, 6, 21), "item on the last day");
	assert_equal(false, matches(2019, 6, 11), "item before the range");
	assert_equal(false, matches(2019, 6, 22), "item after the range");

	// A node with no name at all clicks through as coordinates, never as an invented place.
	df::visit_node remote;
	remote.name = "Remote area";
	remote.first = df::date_t(2004, 2, 1);
	remote.last = df::date_t(2006, 9, 4);
	remote.radius_km = 100.0;
	remote.centre = gps_coordinate(-40.5, -110.25);

	assert_equal("2004-2006", df::visit_node_dates(remote), "node label across years");
	assert_equal(true, df::visit_node_search(df::search_t(), remote).format_terms().starts_with("loc:"),
	             "unnamed node searches coordinates");
}

void register_tests5(view_state& state, test_registry& tests)
{
	//
	// Search parsing
	//
	tests.add("Should parse search"s, should_parse_search);
	tests.add("Should parse search input"s, should_parse_search_input);
	tests.add("Should parse searches"s, should_parse_searches);
	tests.add("Should calculate safe presence requirements"s, should_calculate_safe_presence_requirements);
	tests.add("Should update duplicate search presence"s, should_update_duplicate_search_presence);
	tests.add("Should next date search"s, should_next_date_search);
	tests.add("Should parse selector"s, should_parse_selector);
	tests.add("Should detect folder browse"s, should_detect_folder_browse);
	tests.add("Should select files"s, should_select_files);
	tests.add("Should parent"s, should_parent);
	tests.add("Should escape"s, should_escape);
	tests.add("Should find Location"s, should_find_location);
	tests.add("Should read place qualification level"s, should_read_place_qualification_level);
	tests.add("Should compose qualified place name"s, should_compose_qualified_place_name);
	tests.add("Should bound place attribution"s, should_bound_place_attribution);
	tests.add("Should describe bearing"s, should_describe_bearing);
	tests.add("Should select remote items"s, should_select_remote_items);
	tests.add("Should recover from an empty location search"s, should_recover_from_an_empty_location_search);
	tests.add("Should open a map area as a place and radius"s, should_open_a_map_area_as_a_place_and_radius);
	tests.add("Should reproduce a location group from its header"s,
	          should_reproduce_a_location_group_from_its_header);
	tests.add("Should defer height classes"s, should_defer_height_classes);
	tests.add("Should step search distance"s, should_step_search_distance);
	tests.add("Should resolve location vocabulary"s, should_resolve_location_vocabulary);
	tests.add("Should complete locations as search terms"s, should_complete_locations_as_search_terms);
	tests.add("Should match location radius and presence"s, should_match_location_radius_and_presence);
	tests.add("Should contain map location cells"s, should_contain_map_location_cells);
	tests.add("Should scale map location cells"s, should_scale_map_location_cells);
	tests.add("Should average map location coordinates"s, should_average_map_location_coordinates);
	tests.add("Should resolve named map area on demand"s, should_resolve_named_map_area_on_demand);
	tests.add("Should parse map area"s, should_parse_map_area);
	tests.add("Should derive visits from a result set"s, should_derive_visits_from_a_result_set);
	tests.add("Should split a cluster at long gaps"s, should_split_a_cluster_at_long_gaps);
	tests.add("Should reproduce a place breakdown from its chip"s,
	          should_reproduce_a_place_breakdown_from_its_chip);
	tests.add("Should tell two place chips apart"s, should_tell_two_place_chips_apart);
	tests.add("Should exclude items that cannot sit on a timeline"s,
	          should_exclude_items_that_cannot_sit_on_a_timeline);
	tests.add("Should suppress an era until the query names it"s, should_suppress_an_era_until_the_query_names_it);
	tests.add("Should run the search a timeline node promises"s, should_run_the_search_a_timeline_node_promises);
	tests.add("Issue #119: Should offset localized name"s, should_offset_localized_name);

	//
	// Search matching
	//
	tests.add("Should match terms"s, should_match_terms);
	tests.add("Should match unbalanced groups"s, should_match_unbalanced_groups);
	tests.add("Should ignore unknown property scopes"s, should_ignore_unknown_property_scopes);
	tests.add("Should match parsed property values"s, should_match_parsed_property_values);
	tests.add("Should search Boolean presence terms"s, should_search_boolean_presence_terms);
	tests.add("Should match related"s, should_match_related);
	tests.add("Should list an item once"s, should_list_an_item_once);
	tests.add("Should match related from text"s, should_match_related_from_text);
	tests.add("Should bound related results by closeness"s, should_bound_related_results_by_closeness);
	tests.add("Should break related ties on path"s, should_break_related_ties_on_path);
	tests.add("Should group related results by axis"s, should_group_related_results_by_axis);
	tests.add("Should match related by album"s, should_match_related_by_album);
	tests.add("Should match related by series"s, should_match_related_by_series);
	tests.add("Should match related by capture time"s, should_match_related_by_capture_time);
	tests.add("Should match related by capture place"s, should_match_related_by_capture_place);
	tests.add("Should report the strongest related axis"s, should_report_the_strongest_related_axis);
	tests.add("Should match volume label"s, should_match_volume_label);
	tests.add("Should match multi-value genre"s, should_match_multi_value_genre);
	tests.add("Issue #139/#178: Should match quoted terms"s, should_match_quoted_terms);
	tests.add("Issue #139/#178: Should preserve and auto-quote search input"s,
	          should_preserve_and_auto_quote_search_input);
	tests.add("Issue #157: Should classify search scope"s, should_classify_search_scope);
	tests.add("Should format search predictions"s, should_format_search_predictions);
	tests.add("Should not match folder against without:tag"s, should_not_match_folder_without);
	// One row per query/date pair. The name carries both so a failure names the case and
	// /test: can select a single row.
	const auto register_match_date = [&tests](const std::string& query, const df::date_t d)
	{
		tests.add(std::format("Should match date {} on {}", query, d.to_xmp_date()),
		          [query, d] { should_match_date(query, d); });
	};

	register_match_date("2012-09-14", df::date_t(2012, 9, 14));
	register_match_date("2012", df::date_t(2012, 1, 14));
	register_match_date("2012|2013", df::date_t(2012, 1, 14));
	register_match_date("(April or June) (2013 or 2015)", df::date_t(2013, 4, 22));
	register_match_date("(April or June) (2013 or 2015)", df::date_t(2015, 6, 7));
	register_match_date("age:4", df::date_t(1999, 12, 30));

	//
	// Search format round-trip
	//
	const auto alfie = "Alfie"_c;
	const auto jana = "Jana"_c;
	const auto app_data_folder = known_path(platform::known_folder::app_data);

	auto register_assert_format = [&tests](const df::search_t& query)
	{
		tests.add(std::format("Should format {}", query.text()), [query](shared_test_context& stc)
		{
			const auto text = query.format_terms();
			const auto parsed = df::search_t::parse(text).format_terms();
			assert_equal(text, parsed, "assert_parse");
		});
	};

	register_assert_format(df::search_t().with(prop::duration));
	register_assert_format(df::search_t().add_selector(app_data_folder));
	register_assert_format(df::search_t().add_selector(app_data_folder).with("*.jpg"));
	register_assert_format(df::search_t().without(prop::tag));
	register_assert_format(df::search_t().without(prop::location_place).with(prop::latitude));
	register_assert_format(df::search_t().year(2000));
	register_assert_format(df::search_t().year(2000).month(5));
	register_assert_format(df::search_t().day(25, 5, 1972));
	register_assert_format(df::search_t().day(25, 5, 0));
	register_assert_format(df::search_t().day(0, 5, 2005));
	register_assert_format(df::search_t().with(prop::tag, alfie));
	register_assert_format(df::search_t().with(prop::duration, 500));
	register_assert_format(df::search_t().with("!-"));
	register_assert_format(df::search_t().without(prop::tag, alfie));
	register_assert_format(df::search_t().with(prop::tag, alfie).with(prop::tag, jana));
	register_assert_format(df::search_t().with(prop::tag, alfie).without(prop::tag, jana));
	register_assert_format(df::search_t().with(prop::tag, "tag with space").without(prop::tag, "space tag"));
	register_assert_format(df::search_t().with(alfie).with(jana));
	register_assert_format(df::search_t().age(7, df::date_parts_prop::any));
	register_assert_format(df::search_t().age(7, df::date_parts_prop::modified));
	register_assert_format(df::search_t().fuzzy(prop::duration, 33));
	register_assert_format(df::search_t().location(gps_coordinate(-30.515, 151.665), 5.0));
	register_assert_format(df::search_t().with_extension("jpg"));

	auto register_assert_parse = [&tests](const std::string& query, const std::string& expected = {})
	{
		tests.add(std::format("Should parse {}", query), [query, expected](shared_test_context& stc)
		{
			const auto formatted = df::search_t::parse(query).format_terms();
			assert_equal(str::is_empty(expected) ? query : expected, formatted, "parse query");
			// The canonical form must survive a second trip, or the address box would rewrite
			// itself every time the user pressed Enter on an unchanged query.
			const auto reparsed = df::search_t::parse(formatted).format_terms();
			assert_equal(formatted, reparsed, "parse formatted");
		});
	};

	register_assert_parse("2014 2015"s);
	register_assert_parse("2014 @photo"s);
	register_assert_parse("2014 -@video"s);
	register_assert_parse("2014 @audio"s);
	register_assert_parse("2014 or 2015"s);
	register_assert_parse("Jana Alfie"s);
	register_assert_parse("Jana or Alfie"s);
	register_assert_parse("Jana -Alfie"s);
	register_assert_parse("genre: Rock -artist: \"Counting Crows\""s);
	register_assert_parse("Created:7"s);
	register_assert_parse("Modified:7"s);
	register_assert_parse("(2014 or 2015) (May or June)"s);
	register_assert_parse("Bertie or ((Jana or Alfie) or Amalka)"s);
	register_assert_parse("(2013 or 2014 or 2015) (May or June or July)"s);
	register_assert_parse("Rating:5"s, "rating: 5"s);
	register_assert_parse("dec-25"s);
	register_assert_parse("2020-aug-16"s);
	register_assert_parse("2020-aug"s);
	register_assert_parse("modified:2020-aug"s);
	register_assert_parse("modified:2020-aug-16"s);
	register_assert_parse("modified:aug-16"s);
	register_assert_parse("@duplicates"s);
	register_assert_parse("@remote"s);
	register_assert_parse("loc:-30.515+151.66+5"s, "loc:-30.515,151.66,5km"s);
	register_assert_parse("loc:\"Boulder City\""s);
	register_assert_parse("place:London"s);
	register_assert_parse("state:Nevada"s);
	register_assert_parse("country:\"United Kingdom\""s);
	register_assert_parse("city:London"s, "place:London"s);
	register_assert_parse("countries:France"s, "country:France"s);
	register_assert_parse("near:London"s, "loc:London"s);
	register_assert_parse("loc:\"London, 10km\""s, "loc:London, 10km"s);
	register_assert_parse("loc: 'London UK 5km'"s, "loc:\"London UK, 5km\""s);
	register_assert_parse("loc: London UK 5km"s, "loc:London, UK, 5km"s);
	register_assert_parse("loc: London, UK, 5km"s, "loc:London, UK, 5km"s);
	register_assert_parse("loc: London UK tag:travel"s, "loc:London, UK #travel"s);
	register_assert_parse("loc: London, Ontario, 5km"s, "loc:London, Ontario, 5km"s);
	register_assert_parse("loc: London sunset"s, "loc:London sunset"s);
	register_assert_parse("place:\"London, 2.5km\""s, "place:London, 2.5km"s);
	register_assert_parse("loc:\"London, 500m\""s, "loc:London, 500m"s);
	register_assert_parse("with:location"s, "with: location"s);
	register_assert_parse("without:location"s, "without: location"s);
	register_assert_parse("area:Brisbane"s);
	register_assert_parse("size:1mb"s, "size: 1 MB"s);
	register_assert_parse("> size:1mb"s, "> size: 1 MB"s);
	register_assert_parse("ext:jpg"s);
	register_assert_parse("volume:Backup"s);
	register_assert_parse("with:tag"s, "with: tag"s);
	register_assert_parse("without:tag"s, "without: tag"s);
	register_assert_parse("c:\\windows"s);
	register_assert_parse("c:\\windows with:tag"s, "c:\\windows with: tag"s);
	register_assert_parse("c:\\windows without:tag"s, "c:\\windows without: tag"s);
	register_assert_parse("@ photo"s, "@photo"s);
	register_assert_parse("# key1"s, "#key1"s);
	register_assert_parse("# \"tag with space\""s, "#\"tag with space\""s);
	register_assert_parse("#'tag with space'"s, "#\"tag with space\""s);

	//
	// Search execution
	//
	auto register_should_search = [&tests](const std::string_view query, int expected_index, int expected_recurse,
	                                       int expected_folder)
	{
		tests.add(std::format("Should search {}", query),
		          [query, expected_index, expected_recurse, expected_folder](shared_test_context& stc)
		          {
			          stc.lazy_load_index();
			          assert_equal(expected_index, count_search_results(stc.test_index, query), query);

			          const auto query_recurse_test_folder = std::format(
				          "\"{}\\**\" {} -excluded", test_files_folder, query);
			          assert_equal(expected_recurse, count_search_results(stc.empty_index, query_recurse_test_folder),
			                       std::format("recurse {}", query));

			          const auto query_test_folder = std::format("\"{}\" {}", test_files_folder, query);
			          assert_equal(expected_folder, count_search_results(stc.empty_index, query_test_folder),
			                       std::format("folder {}", query));
		          });
	};

	register_should_search("2012-09-14", 5, 5, 5);
	register_should_search("Created:2012-09-14", 5, 5, 5);
	register_should_search("2010-05-25", 0, 0, 0);
	register_should_search("2010-5-25", 0, 0, 0);
	register_should_search("Created:2010-05-25", 0, 0, 0);
	register_should_search("2009-11-15", 1, 1, 1);

	register_should_search("@video", 12, 12, 12);
	register_should_search("@audio", 6, 6, 5);
	register_should_search("@commodore", 1, 1, 0);
	register_should_search("@archive", 1, 1, 1);

	register_should_search("@photo", 29, 29, 28);
	register_should_search("@ photo", 29, 29, 28);
	register_should_search("@   photo", 29, 29, 28);

	register_should_search("key1", 4, 4, 4);
	register_should_search("Tag:key1", 4, 4, 4);
	register_should_search("Tag :key1", 4, 4, 4);
	register_should_search("Tag: key1", 4, 4, 4);
	register_should_search("#key1", 4, 4, 4);
	register_should_search("# key1", 4, 4, 4);
	register_should_search("Tag:dog Tag:london", 1, 1, 1);
	register_should_search("not_exist", 0, 0, 0);
	register_should_search("Tag:not_exist", 0, 0, 0);
	register_should_search("#ke*", 4, 4, 4);
	register_should_search("ke*", 5, 5, 5);

	register_should_search("Megapixels:1.6 dog", 1, 1, 1);
	register_should_search("Megapixels:1.6", 2, 2, 2);
	register_should_search("(< Megapixels:1.0 > Megapixels:0.5)", 8, 8, 8);
	register_should_search("Megapixels:2", 1, 1, 1);
	register_should_search("pixels:2", 1, 1, 1);
	register_should_search("> pixels:1", 11, 11, 10);
	register_should_search(">pixels:1", 11, 11, 10);
	register_should_search(">pixels :1", 11, 11, 10);
	register_should_search(">pixels: 1", 11, 11, 10);
	register_should_search("> pixels : 1", 11, 11, 10);
	register_should_search("2mp", 1, 1, 1);
	register_should_search("6000x4000", 1, 1, 1);

	register_should_search("Aperture:f/5", 1, 1, 1);
	register_should_search("f/3.5", 6, 6, 6);
	register_should_search("f/1.8", 0, 0, 0);
	register_should_search("f/4.0", 0, 0, 0);
	register_should_search("f/6.3", 5, 5, 5);

	register_should_search("with:Exposure @photo", 17, 17, 16);
	register_should_search("with: Exposure @ photo", 17, 17, 16);
	register_should_search("without:Exposure @photo", 12, 12, 12);
	register_should_search("without:Exposure", 33, 33, 31);
	register_should_search("with:Exposure", 18, 18, 17);
	register_should_search("with: Exposure", 18, 18, 17);
	register_should_search("ExposureTime:1/20s", 1, 1, 1);
	register_should_search("ExposureTime: 1/20s", 1, 1, 1);
	register_should_search("1/20s", 1, 1, 1);
	register_should_search("1/100s", 5, 5, 5);
	register_should_search("1/1000s", 0, 0, 0);

	register_should_search("iso400 @photo", 2, 2, 2);
	register_should_search("iso:400 @photo", 2, 2, 2);
	register_should_search("iso: 400 @photo", 2, 2, 2);
	register_should_search("iso : 400 @photo", 2, 2, 2);
	register_should_search(">= iso:400 @photo", 3, 3, 3);

	register_should_search("1:26", 1, 1, 1);
	register_should_search("0:10", 5, 5, 5);
	register_should_search("7:77", 0, 0, 0);
	register_should_search("10:00", 0, 0, 0);

	register_should_search("size:0.3mb", 5, 5, 5);
	register_should_search("size:14kb", 1, 1, 1);
	register_should_search("size:5.1mb", 1, 1, 1);
	register_should_search(">size:1mb", 10, 10, 9);

	register_should_search("dog london", 1, 1, 1);
	// locations.md 3.6: free text searches the stored field. The GPS-only Prague photo carries no
	// stored place, so the location vocabulary is what finds it.
	register_should_search("prague", 4, 4, 4);
	register_should_search("loc:prague", 5, 5, 5);
	register_should_search("ipad", 1, 1, 1);
	register_should_search("48kHz", 4, 4, 3);
	register_should_search("44.1kHz", 5, 5, 5);
	register_should_search("dog or london", 4, 4, 4);
	register_should_search("Rock", 3, 3, 3);
	register_should_search("canon @photo", 10, 10, 9);
	register_should_search("nikon d100", 1, 1, 1);

	register_should_search("ext:cr2", 2, 2, 1);
	register_should_search("ext:.cr2", 2, 2, 1);

	register_should_search("sony", 1, 1, 1);
	register_should_search("\"sony\"", 1, 1, 1);
	register_should_search("sony*", 1, 1, 1);
	register_should_search("*sony*", 1, 1, 1);

	register_should_search("screws", 1, 1, 0);
	register_should_search("\"screws\"", 1, 1, 0);
	register_should_search("screws*", 1, 1, 0);
	register_should_search("*screws*", 1, 1, 0);

	register_should_search("london", 4, 4, 4);
	register_should_search("\"london\"", 4, 4, 4);
	register_should_search("london*", 4, 4, 4);
	register_should_search("*london*", 4, 4, 4);

	register_should_search("d64", 1, 1, 0);
	register_should_search("ace -retro", 1, 1, 1);
	register_should_search("jpg", 15, 15, 15);
	register_should_search("-jpg", 39, 38, 37);
	register_should_search("-ext:jpg", 39, 38, 37);

#ifndef _DEBUG
	tests.add("Should Find Closest Location"s, should_find_closest_location);
#endif
}
