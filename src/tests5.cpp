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
	assert_equal(false, df::match_volume_label("\\\\server\\share", labels, exact_term), "unc path has no drive letter");

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

static void should_match_related(shared_test_context& stc)
{
	stc.lazy_load_index();

	const df::file_path path(test_files_folder, "Test.jpg");
	const auto i = std::make_shared<df::item_element>(path, stc.test_index.find_item(path));
	stc.test_index.scan_item(i, true, false);

	df::related_info r;
	r.load(i);

	assert_equal(1, count_search_results(stc.test_index, df::search_t().related(r)), "Related");
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
	view_host_base_ptr view;
	location_cache locations;
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
	assert_equal(base_folder, s.parent_search().parent.text(), "parent @photo with selection");

	// DATES
	s.open(view, "1972-may-25");
	assert_equal("1972-may", s.parent_search().parent.text(), "parent");

	s.open(view, "may-25");
	assert_equal("May", s.parent_search().parent.text(), "parent");

	s.open(view, "1972-may");
	assert_equal("1972", s.parent_search().parent.text(), "parent");

	s.open(view, "2009 December");
	assert_equal("2009", s.parent_search().parent.text(), "parent");
}

static void should_escape()
{
	const auto base_folder = std::string(test_files_folder.text());

	null_state_strategy ss;
	null_async_strategy as;
	location_cache locations;
	view_host_base_ptr view;
	index_state index(as, locations);
	view_state s(ss, as, index, make_test_player());

	df::index_roots roots;
	roots.folders = {test_files_folder};
	s.item_index.index_roots(roots);
	s.item_index.index_folders(test_token);

	s.view_mode(view_type::items);
	s.open(view, base_folder + "\\**"s);
	s.escape(view);
	assert_equal(base_folder, s.search().text(), "parent escape **");
}

static void should_find_closest_location()
{
	location_cache locations;
	locations.load_index();

	assert_equal("St Paul's", locations.find_closest(51.5142, -000.0985).place, "City");
	assert_equal("Armidale", locations.find_closest(-30.515, 151.665).place, "City");
	assert_equal("Johannesburg", locations.find_closest(-26.204444, 28.045556).place, "City");
	assert_equal("Santiago", locations.find_closest(-33.45, -70.666667).place, "City");
	assert_equal("Eastern Parkway", locations.find_closest(40.664167, -73.938611).place, "City");
	assert_equal("Beijing", locations.find_closest(39.913889, 116.391667).place, "City");
}

static void should_find_location()
{
	location_cache locations;
	locations.load_index();

	const auto default_location = gps_coordinate(51.5255317687988, -0.116743430495262); // London

	assert_equal("City of London", locations.find_by_id(2643741).place, "City");
	assert_equal("King of Prussia", locations.find_by_id(5196220).place, "City");

	assert_equal("London, England, United Kingdom",
	             locations.auto_complete("london", 8, default_location)[0].location.str(), "City");
	assert_equal("Londonderry Station, Nova Scotia, Canada",
	             locations.auto_complete("london canada", 8, default_location)[0].location.str(), "City");
	assert_equal("Armidale, New South Wales, Australia",
	             locations.auto_complete("armid aust", 8, default_location)[0].location.str(), "City");
	assert_equal("Birmingham, England, United Kingdom",
	             locations.auto_complete("birm gb", 8, default_location)[0].location.str(), "City");
	assert_equal("King of Prussia, Pennsylvania, United States",
	             locations.auto_complete("king pru usa", 8, default_location)[0].location.str(), "City");
}

void register_tests5(view_state& state, test_registry& tests)
{
	//
	// Search parsing
	//
	tests.add("Should parse search"s, should_parse_search);
	tests.add("Should parse search input"s, should_parse_search_input);
	tests.add("Should parse searches"s, should_parse_searches);
	tests.add("Should next date search"s, should_next_date_search);
	tests.add("Should parse selector"s, should_parse_selector);
	tests.add("Should select files"s, should_select_files);
	tests.add("Should parent"s, should_parent);
	tests.add("Should escape"s, should_escape);
	tests.add("Should find Location"s, should_find_location);

	//
	// Search matching
	//
	tests.add("Should match terms"s, should_match_terms);
	tests.add("Should match related"s, should_match_related);
	tests.add("Should match volume label"s, should_match_volume_label);
	tests.add("Should not match folder without "s, should_not_match_folder_without);
	tests.add("Should match date"s, [] { should_match_date("2012-09-14", df::date_t(2012, 9, 14)); });
	tests.add("Should match date"s, [] { should_match_date("2012", df::date_t(2012, 1, 14)); });
	tests.add("Should match date"s, [] { should_match_date("2012|2013", df::date_t(2012, 1, 14)); });
	tests.add("Should match date"s, []
	{
		should_match_date("(April or June) (2013 or 2015)"s, df::date_t(2013, 4, 22));
	});
	tests.add("Should match date"s, []
	{
		should_match_date("(April or June) (2013 or 2015)"s, df::date_t(2015, 6, 7));
	});
	tests.add("Should match date"s, [] { should_match_date("age:4", df::date_t(1999, 12, 30)); });

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
			const auto parsed = df::search_t::parse(query).format_terms();
			assert_equal(formatted, parsed, "parse formatted");
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
	register_assert_parse("loc:-30.515+151.66+5"s);
	register_assert_parse("size:1mb"s, "size: 1 MB"s);
	register_assert_parse("> size:1mb"s, "> size: 1 MB"s);
	register_assert_parse("ext:jpg"s);
	register_assert_parse("volume:Backup"s);
	register_assert_parse("with:tag"s, "with: tag"s);
	register_assert_parse("without:tag"s, "without: tag"s);
	register_assert_parse("c:\\windows"s);
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

	register_should_search("@video", 5, 5, 5);
	register_should_search("@audio", 5, 5, 4);
	register_should_search("@commodore", 1, 1, 0);
	register_should_search("@archive", 1, 1, 1);

	register_should_search("@photo", 28, 28, 27);
	register_should_search("@ photo", 28, 28, 27);
	register_should_search("@   photo", 28, 28, 27);

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
	register_should_search("(< Megapixels:1.0 > Megapixels:0.5)", 7, 7, 7);
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

	register_should_search("with:Exposure @photo", 15, 15, 14);
	register_should_search("with: Exposure @ photo", 15, 15, 14);
	register_should_search("without:Exposure @photo", 13, 13, 13);
	register_should_search("without:Exposure", 26, 26, 24);
	register_should_search("with:Exposure", 16, 16, 15);
	register_should_search("with: Exposure", 16, 16, 15);
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

	register_should_search("key1", 4, 4, 4);
	register_should_search("dog london", 1, 1, 1);
	register_should_search("prague", 5, 5, 5);
	register_should_search("ipad", 1, 1, 1);
	register_should_search("48kHz", 4, 4, 3);
	register_should_search("44.1kHz", 4, 4, 4);
	register_should_search("tag:dog tag:london", 1, 1, 1);
	register_should_search("dog or london", 3, 3, 3);
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

	register_should_search("london", 3, 3, 3);
	register_should_search("\"london\"", 3, 3, 3);
	register_should_search("london*", 3, 3, 3);
	register_should_search("*london*", 3, 3, 3);

	register_should_search("d64", 1, 1, 0);
	register_should_search("ace -retro", 1, 1, 1);
	register_should_search("jpg", 15, 15, 15);
	register_should_search("-jpg", 30, 29, 28);
	register_should_search("-ext:jpg", 30, 29, 28);

#ifndef _DEBUG
	tests.add("Should Find Closest Location"s, should_find_closest_location);
#endif
}
