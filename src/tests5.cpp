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
	const auto date_filter = df::search_t::parse(u8"2012-09-14"sv);
	const df::search_matcher matcher(date_filter);

	df::index_file_item info;
	info.ft = files::file_type_from_name(u8"test.jpg"sv);
	info.safe_ps()->created_exif = df::date_t(2012, 9, 14);
	assert_equal(true, matcher.match_item({}, info).is_match(), u8"date"sv);

	info.safe_ps()->created_exif = df::date_t(2012, 9, 13);
	assert_equal(false, matcher.match_item({}, info).is_match(), u8"date"sv);
}

static void should_match_terms()
{
	prop_test().tag(u8"aaa"sv)
	           .is_match(u8"#aaa"sv)
	           .is_not_match(u8"#bbb"sv)
	           .is_match(u8"-#bbb"sv)
	           .is_match(u8"!#bbb"sv)
	           .is_not_match(u8"-#aaa"sv)
	           .is_not_match(u8"!#aaa"sv)
	           .is_match(u8"#aaa or #bbb"sv)
	           .is_not_match(u8"#aaa and #bbb"sv)
	           .is_not_match(u8"#aaa #bbb"sv);

	prop_test().tag(u8"bbb"sv)
	           .is_not_match(u8"#aaa"sv)
	           .is_match(u8"#bbb"sv)
	           .is_match(u8"#aaa or #bbb"sv);

	prop_test().tag(u8"'aa bb'"sv)
	           .is_not_match(u8"#aaa"sv)
	           .is_not_match(u8"#bb"sv)
	           .is_match(u8"#'aa bb'"sv);

	prop_test()
		.tag(u8"aaa bbb"sv)
		.is_match(u8"#aaa"sv)
		.is_match(u8"#bbb"sv)
		.is_match(u8"#aaa or #bbb"sv)
		.is_match(u8"#aaa and #bbb"sv)
		.is_match(u8"#aaa #bbb"sv)
		.is_not_match(u8"#aaa !#bbb"sv);

	prop_test().rate(4)
	           .is_match(u8"rate:4"sv)
	           .is_match(u8"4"sv)
	           .is_match(u8">= 4"sv)
	           .is_not_match(u8"> 4"sv)
	           .is_match(u8">3"sv)
	           .is_match(u8"3 | 4"sv)
	           .is_match(u8"3 or 4"sv)
	           .is_not_match(u8"3 and 4"sv);

	prop_test().desc(u8"one two three"sv)
	           .is_match(u8"two"sv)
	           .is_match(u8"one two"sv)
	           .is_match(u8"two three"sv)
	           .is_match(u8"'two three'"sv)
	           .is_not_match(u8"'one three'"sv);

	prop_test().date(1999, 12, 27)
	           .is_match(u8"age:5"sv)
	           .is_match(u8"age:10"sv)
	           .is_not_match(u8"age:1"sv)
	           .is_not_match(u8"age:2"sv);

	prop_test().date(2000, 1, 1)
	           .is_match(u8"age:1"sv)
	           .is_match(u8"age:5"sv)
	           .is_not_match(u8"-age:5"sv)
	           .is_not_match(u8"!age:5"sv);

	prop_test().tag(u8"aaa"sv).date(2000, 1, 1)
	           .is_match(u8"#aaa age:1"sv)
	           .is_match(u8"#aaa created:2000-jan"sv)
	           .is_not_match(u8"created:2000-feb"sv)
	           .is_not_match(u8"#bbb created:2000-jan"sv);

	prop_test().file_created_date(2000, 1, 1).date(1999, 5, 25)
	           .is_not_match(u8"created:9"sv)
	           .is_not_match(u8"created:2000-jan"sv)
	           .is_match(u8"created:1999-may"sv);

	prop_test().file_created_date(2000, 1, 1)
	           .is_match(u8"age:10"sv)
	           .is_match(u8"created:2000-jan"sv);
}

static void should_not_match_folder_without()
{
	const auto now_days = df::date_t(2000, 1, 1).to_days();
	const auto search = df::search_t::parse(u8"without:tag"sv);
	const df::search_matcher matcher(search, now_days);
	assert_equal(false, matcher.match_folder(test_files_folder.text(), u8"test"_c).is_match(), u8"folder name test"sv);
}

static void should_match_date(const std::u8string_view query, const df::date_t d)
{
	df::index_file_item props_without_val;
	props_without_val.ft = files::file_type_from_name(u8"test.jpg"sv);
	props_without_val.file_modified = df::date_t(1972, 5, 25);
	props_without_val.safe_ps();

	df::index_file_item props_with_val;
	props_with_val.ft = files::file_type_from_name(u8"test.jpg"sv);
	props_with_val.safe_ps()->created_utc = d;

	const auto search = df::search_t::parse(query);
	df::search_matcher matcher(search, df::date_t(2000, 1, 1).to_days());

	assert_equal(true, matcher.match_all_terms(test_files_folder.text(), props_with_val).is_match(), query);
	assert_equal(false, matcher.match_all_terms(test_files_folder.text(), props_without_val).is_match(), query);
}

static void assert_parse(const std::u8string_view selector, const std::u8string_view display,
                         const std::u8string_view folder,
                         const bool is_recursive, const std::u8string_view wildcard)
{
	const df::item_selector sel(selector);

	assert_equal(display, sel.str(), u8"item_selector.str"sv);
	assert_equal(folder, sel.folder().text(), u8"item_selector.folder"sv);
	assert_equal(is_recursive, sel.is_recursive(), u8"item_selector.is_recursive"sv);
	assert_equal(wildcard, sel.wildcard(), u8"item_selector.wildcard"sv);
}

static void assert_parse(const std::u8string_view selector, const std::u8string_view folder,
                         const bool is_recursive, const std::u8string_view wildcard)
{
	assert_parse(selector, selector, folder, is_recursive, wildcard);
}

static void should_parse_selector()
{
	assert_parse(u8"c:\\"sv, u8"c:\\"sv, false, u8"*.*"sv);
	assert_parse(u8"c:\\**"sv, u8"c:\\"sv, true, u8"*.*"sv);
	assert_parse(u8"c:\\**\\"sv, u8"c:\\**"sv, u8"c:\\"sv, true, u8"*.*"sv);
	assert_parse(u8"c:/**/"sv, u8"c:\\**"sv, u8"c:\\"sv, true, u8"*.*"sv);
	assert_parse(u8"c:\\*.jpg"sv, u8"c:\\"sv, false, u8"*.jpg"sv);
	assert_parse(u8"c:\\temp\\*.jpg"sv, u8"c:\\temp"sv, false, u8"*.jpg"sv);
	assert_parse(u8R"(c:\temp\**\*.jpg)"sv, u8"c:\\temp"sv, true, u8"*.jpg"sv);
	assert_parse(u8"c:\\temp\\***.jpg"sv, u8"c:\\temp"sv, false, u8"***.jpg"sv);
	assert_parse(u8"c:\\temp\\?x.jpg"sv, u8"c:\\temp"sv, false, u8"?x.jpg"sv);
}

static bool contains(const std::vector<platform::file_info>& files, const std::u8string_view find)
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

static bool contains(const std::vector<platform::folder_info>& files, const std::u8string_view find)
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
	assert_equal(true, contains(platform::select_files(recursive, true), u8"Screws.CR2"sv),
	             u8"files from sub folders"sv);
	assert_equal(true, contains(platform::select_files(recursive, true), u8"Cube.png"sv), u8"files from sub folders"sv);
	assert_equal(true, contains(platform::select_files(recursive, true), u8"Test.jpg"sv), u8"files from sub folders"sv);
	assert_equal(false, contains(platform::select_folders(recursive, true), u8"raw"sv), u8"no folders"sv);

	const auto not_recursive = df::item_selector(test_files_folder, false);
	assert_equal(false, contains(platform::select_files(not_recursive, true), u8"Screws.CR2"sv),
	             u8"no files from sub folder"sv);
	assert_equal(true, contains(platform::select_files(not_recursive, true), u8"Cube.png"sv),
	             u8"files from sub folders"sv);
	assert_equal(true, contains(platform::select_files(not_recursive, true), u8"Test.jpg"sv),
	             u8"files from sub folders"sv);
	assert_equal(true, contains(platform::select_folders(not_recursive, true), u8"raw"sv), u8"include folders"sv);

	const auto recursive_png = df::item_selector(test_files_folder, true, u8"*.png"sv);
	assert_equal(false, contains(platform::select_files(recursive_png, true), u8"Screws.CR2"sv),
	             u8"no files from sub folder"sv);
	assert_equal(true, contains(platform::select_files(recursive_png, true), u8"Cube.png"sv),
	             u8"files from sub folders"sv);
	assert_equal(false, contains(platform::select_files(recursive_png, true), u8"Test.jpg"sv),
	             u8"files from sub folders"sv);
	assert_equal(false, contains(platform::select_folders(recursive_png, true), u8"raw"sv), u8"include folders"sv);

	const auto not_recursive_cr2 = df::item_selector(test_files_folder, false, u8"*.cr2"sv);
	assert_equal(false, contains(platform::select_files(not_recursive_cr2, true), u8"Screws.CR2"sv),
	             u8"no files from sub folder"sv);
	assert_equal(false, contains(platform::select_files(not_recursive_cr2, true), u8"Cube.png"sv),
	             u8"files from sub folders"sv);
	assert_equal(false, contains(platform::select_files(not_recursive_cr2, true), u8"Test.jpg"sv),
	             u8"files from sub folders"sv);
	assert_equal(true, contains(platform::select_folders(not_recursive_cr2, true), u8"raw"sv), u8"include folders"sv);

	const auto recursive_cr2 = df::item_selector(test_files_folder, true, u8"*.cr2"sv);
	assert_equal(true, contains(platform::select_files(recursive_cr2, true), u8"Screws.CR2"sv),
	             u8"no files from sub folder"sv);
	assert_equal(false, contains(platform::select_files(recursive_cr2, true), u8"Cube.png"sv),
	             u8"files from sub folders"sv);
	assert_equal(false, contains(platform::select_files(recursive_cr2, true), u8"Test.jpg"sv),
	             u8"files from sub folders"sv);
	assert_equal(false, contains(platform::select_folders(recursive_cr2, true), u8"raw"sv), u8"include folders"sv);
}

static void should_match_related(shared_test_context& stc)
{
	stc.lazy_load_index();

	const df::file_path path(test_files_folder, u8"Test.jpg"sv);
	const auto i = std::make_shared<df::item_element>(path, stc.test_index.find_item(path));
	stc.test_index.scan_item(i, true, false);

	df::related_info r;
	r.load(i);

	assert_equal(1, count_search_results(stc.test_index, df::search_t().related(r)), u8"Related"sv);
}

static void should_parse_search()
{
	constexpr auto message = u8"Should tokenize search"sv;
	search_tokenizer t;
	const auto parts = t.parse(
		u8"  (#tree or house)    -nega-tive aperture:f/2.0   's\"om:e#inv-lid~s' -description:\"hello world\" tag : spaces c:\\test 12:00 or -#tree|ant"sv);

	assert_equal(parts[0].modifier.positive, true, message);
	assert_equal(parts[0].modifier.logical_op, df::search_term_modifier_bool::none, message);
	assert_equal(parts[0].modifier.begin_group, 1, message);
	assert_equal(parts[0].modifier.end_group, 0, message);
	assert_equal(parts[0].scope, u8"tag"sv, message);
	assert_equal(parts[0].term, u8"tree"sv, message);

	assert_equal(parts[1].modifier.positive, true, message);
	assert_equal(parts[1].modifier.logical_op, df::search_term_modifier_bool::m_or, message);
	assert_equal(parts[1].modifier.begin_group, 0, message);
	assert_equal(parts[1].modifier.end_group, 1, message);
	assert_equal(parts[1].scope.empty(), true, message);
	assert_equal(parts[1].term, u8"house"sv, message);

	assert_equal(parts[2].modifier.positive, false, message);
	assert_equal(parts[2].modifier.logical_op, df::search_term_modifier_bool::none, message);
	assert_equal(parts[2].modifier.begin_group, 0, message);
	assert_equal(parts[2].modifier.end_group, 0, message);
	assert_equal(parts[2].scope.empty(), true, message);
	assert_equal(parts[2].term, u8"nega-tive"sv, message);

	assert_equal(parts[3].modifier.positive, true, message);
	assert_equal(parts[3].modifier.logical_op, df::search_term_modifier_bool::none, message);
	assert_equal(parts[3].modifier.begin_group, 0, message);
	assert_equal(parts[3].modifier.end_group, 0, message);
	assert_equal(parts[3].scope, u8"aperture"sv, message);
	assert_equal(parts[3].term, u8"f/2.0"sv, message);

	assert_equal(parts[4].modifier.positive, true, message);
	assert_equal(parts[4].scope.empty(), true, message);
	assert_equal(parts[4].term, u8"s\"om:e#inv-lid~s"sv, message);

	assert_equal(parts[5].modifier.positive, false, message);
	assert_equal(parts[5].scope, u8"description"sv, message);
	assert_equal(parts[5].term, u8"hello world"sv, message);

	assert_equal(parts[6].scope, u8"tag"sv, message);
	assert_equal(parts[6].term, u8"spaces"sv, message);

	assert_equal(parts[7].modifier.positive, true, message);
	assert_equal(parts[7].scope.empty(), true, message);
	assert_equal(parts[7].term, u8"c:\\test"sv, message);

	assert_equal(parts[8].modifier.positive, true, message);
	assert_equal(parts[8].scope.empty(), true, message);
	assert_equal(parts[8].term, u8"12:00"sv, message);

	assert_equal(parts[9].modifier.positive, false, message);
	assert_equal(parts[9].modifier.logical_op, df::search_term_modifier_bool::m_or, message);
	assert_equal(parts[9].scope, u8"tag"sv, message);
	assert_equal(parts[9].term, u8"tree"sv, message);

	assert_equal(parts[10].modifier.positive, true, message);
	assert_equal(parts[10].modifier.logical_op, df::search_term_modifier_bool::m_or, message);
	assert_equal(parts[10].scope.empty(), true, message);
	assert_equal(parts[10].term, u8"ant"sv, message);
}

static void assert_date_shift(df::search_t d, const std::u8string_view expected_prev,
                              const std::u8string_view expected_next)
{
	d.next_date(false);
	assert_equal(expected_prev, d.text(), u8"prev date"sv);
	d.next_date(true);
	assert_equal(expected_next, d.text(), u8"next date"sv);
}

static void should_next_date_search()
{
	assert_date_shift(df::search_t().day(25, 5, 1972), u8"1972-may-24"sv, u8"1972-may-25"sv);
	assert_date_shift(df::search_t().day(1, 1, 2020), u8"2019-dec-31"sv, u8"2020-jan-1"sv);
	assert_date_shift(df::search_t().day(1, 1, 0), u8"dec-31"sv, u8"jan-1"sv);
	assert_date_shift(df::search_t().year(2010), u8"2009"sv, u8"2010"sv);
	assert_date_shift(df::search_t().month(1), u8"December"sv, u8"January"sv);
	assert_date_shift(df::search_t().month(1).year(2010), u8"2009-dec"sv, u8"2010-jan"sv);

	assert_date_shift(df::search_t().day(1, 1, 2020, df::date_parts_prop::modified), u8"modified:2019-dec-31"sv,
	                  u8"modified:2020-jan-1"sv);
	assert_date_shift(df::search_t().day(1, 1, 0, df::date_parts_prop::modified), u8"modified:dec-31"sv,
	                  u8"modified:jan-1"sv);
	assert_date_shift(df::search_t().year(2010, df::date_parts_prop::created), u8"created:2009"sv, u8"created:2010"sv);
	assert_date_shift(df::search_t().month(1, df::date_parts_prop::created).year(2010, df::date_parts_prop::created),
	                  u8"created:2009-dec"sv, u8"created:2010-jan"sv);
}

static void should_parse_search_input()
{
	const auto base_folder = std::u8string(test_files_folder.text());

	null_state_strategy ss;
	null_async_strategy as;
	view_host_base_ptr view;
	location_cache locations;
	index_state index(as, locations);
	view_state s(ss, as, index, make_test_player());
	s.view_mode(view_type::items);
	s.open(view, test_files_folder.text());
	s.open(view, u8".."sv);

	assert_equal(test_files_folder.parent().text(), s.search().text(), u8"Parse .."sv);

	s.open(view, test_files_folder.text());
	s.open(view, u8"**"sv);

	assert_equal(base_folder + u8"\\**"s, s.search().text(), u8"Parse **"sv);

	s.open(view, test_files_folder.text());
	s.open(view, u8"aaa"sv);

	assert_equal(u8"aaa"s, s.search().text(), u8"Parse aaa"sv);

	s.open(view, u8"\"aaa\""sv);
	assert_equal(u8"\"aaa\""s, s.search().text(), u8"Parse \"aaa\""sv);
}

static void should_parent()
{
	const auto folder = df::folder_path(u8"c:\\windows\\system32"sv);
	assert_equal(u8"c:\\windows"sv, folder.parent().text(), u8"parent test"sv);
	assert_equal(u8"c:\\"sv, folder.parent().parent().text(), u8"parent test"sv);
	assert_equal(u8"c:\\"sv, folder.parent().parent().parent().text(), u8"parent test"sv);

	const auto base_folder = std::u8string(test_files_folder.text());
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

	s.open(view, base_folder + u8" test"s);
	assert_equal(base_folder, s.parent_search().parent.text(), u8"parent test"sv);

	s.open(view, base_folder + u8"\\raw"s);
	const auto raw_parent = s.parent_search();
	assert_equal(base_folder, raw_parent.parent.text(), u8"parent \\raw"sv);
	assert_equal(u8"raw"sv, raw_parent.name, u8"parent name \\raw"sv);

	s.open(view, base_folder + u8"\\*.png"s);
	assert_equal(base_folder, s.parent_search().parent.text(), u8"parent *.png"sv);

	s.open(view, base_folder + u8" @photo"s);
	assert_equal(base_folder, s.parent_search().parent.text(), u8"parent @photo"sv);

	s.open(view, u8"@photo"sv);
	assert_equal({}, s.parent_search().parent.text(), u8"parent @photo"sv);

	s.open(view, u8"@photo"sv);
	s.update_item_groups();
	s.select_next(view, true, false, false);
	s.update_selection();
	assert_equal(base_folder, s.parent_search().parent.text(), u8"parent @photo with selection"sv);

	// DATES
	s.open(view, u8"1972-may-25"sv);
	assert_equal(u8"1972-may"sv, s.parent_search().parent.text(), u8"parent"sv);

	s.open(view, u8"may-25"sv);
	assert_equal(u8"May"sv, s.parent_search().parent.text(), u8"parent"sv);

	s.open(view, u8"1972-may"sv);
	assert_equal(u8"1972"sv, s.parent_search().parent.text(), u8"parent"sv);

	s.open(view, u8"2009 December"sv);
	assert_equal(u8"2009"sv, s.parent_search().parent.text(), u8"parent"sv);
}

static void should_escape()
{
	const auto base_folder = std::u8string(test_files_folder.text());

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
	s.open(view, base_folder + u8"\\**"s);
	s.escape(view);
	assert_equal(base_folder, s.search().text(), u8"parent escape **"sv);
}

static void should_find_closest_location()
{
	location_cache locations;
	locations.load_index();

	assert_equal(u8"St Paul's"sv, locations.find_closest(51.5142, -000.0985).place, u8"City"sv);
	assert_equal(u8"Armidale"sv, locations.find_closest(-30.515, 151.665).place, u8"City"sv);
	assert_equal(u8"Johannesburg"sv, locations.find_closest(-26.204444, 28.045556).place, u8"City"sv);
	assert_equal(u8"Santiago"sv, locations.find_closest(-33.45, -70.666667).place, u8"City"sv);
	assert_equal(u8"Eastern Parkway"sv, locations.find_closest(40.664167, -73.938611).place, u8"City"sv);
	assert_equal(u8"Beijing"sv, locations.find_closest(39.913889, 116.391667).place, u8"City"sv);
}

static void should_find_location()
{
	location_cache locations;
	locations.load_index();

	const auto default_location = gps_coordinate(51.5255317687988, -0.116743430495262); // London

	assert_equal(u8"City of London"sv, locations.find_by_id(2643741).place, u8"City"sv);
	assert_equal(u8"King of Prussia"sv, locations.find_by_id(5196220).place, u8"City"sv);

	assert_equal(u8"London, England, United Kingdom"sv,
	             locations.auto_complete(u8"london"sv, 8, default_location)[0].location.str(), u8"City"sv);
	assert_equal(u8"Londonderry Station, Nova Scotia, Canada"sv,
	             locations.auto_complete(u8"london canada"sv, 8, default_location)[0].location.str(), u8"City"sv);
	assert_equal(u8"Armidale, New South Wales, Australia"sv,
	             locations.auto_complete(u8"armid aust"sv, 8, default_location)[0].location.str(), u8"City"sv);
	assert_equal(u8"Birmingham, England, United Kingdom"sv,
	             locations.auto_complete(u8"birm gb"sv, 8, default_location)[0].location.str(), u8"City"sv);
	assert_equal(u8"King of Prussia, Pennsylvania, United States"sv,
	             locations.auto_complete(u8"king pru usa"sv, 8, default_location)[0].location.str(), u8"City"sv);
}

void register_tests5(view_state& state, test_registry& tests)
{
	//
	// Search parsing
	//
	tests.add(u8"Should parse search"s, should_parse_search);
	tests.add(u8"Should parse search input"s, should_parse_search_input);
	tests.add(u8"Should parse searches"s, should_parse_searches);
	tests.add(u8"Should next date search"s, should_next_date_search);
	tests.add(u8"Should parse selector"s, should_parse_selector);
	tests.add(u8"Should select files"s, should_select_files);
	tests.add(u8"Should parent"s, should_parent);
	tests.add(u8"Should escape"s, should_escape);
	tests.add(u8"Should find Location"s, should_find_location);

	//
	// Search matching
	//
	tests.add(u8"Should match terms"s, should_match_terms);
	tests.add(u8"Should match related"s, should_match_related);
	tests.add(u8"Should not match folder without "s, should_not_match_folder_without);
	tests.add(u8"Should match date"s, [] { should_match_date(u8"2012-09-14"sv, df::date_t(2012, 9, 14)); });
	tests.add(u8"Should match date"s, [] { should_match_date(u8"2012"sv, df::date_t(2012, 1, 14)); });
	tests.add(u8"Should match date"s, [] { should_match_date(u8"2012|2013"sv, df::date_t(2012, 1, 14)); });
	tests.add(u8"Should match date"s, []
	{
		should_match_date(u8"(April or June) (2013 or 2015)"s, df::date_t(2013, 4, 22));
	});
	tests.add(u8"Should match date"s, []
	{
		should_match_date(u8"(April or June) (2013 or 2015)"s, df::date_t(2015, 6, 7));
	});
	tests.add(u8"Should match date"s, [] { should_match_date(u8"age:4"sv, df::date_t(1999, 12, 30)); });

	//
	// Search format round-trip
	//
	const auto alfie = u8"Alfie"_c;
	const auto jana = u8"Jana"_c;
	const auto app_data_folder = known_path(platform::known_folder::app_data);

	auto register_assert_format = [&tests](const df::search_t& query)
	{
		tests.add(str::format(u8"Should format {}"sv, query.text()), [query](shared_test_context& stc)
		{
			const auto text = query.format_terms();
			const auto parsed = df::search_t::parse(text).format_terms();
			assert_equal(text, parsed, u8"assert_parse"sv);
		});
	};

	register_assert_format(df::search_t().with(prop::duration));
	register_assert_format(df::search_t().add_selector(app_data_folder));
	register_assert_format(df::search_t().add_selector(app_data_folder).with(u8"*.jpg"sv));
	register_assert_format(df::search_t().without(prop::tag));
	register_assert_format(df::search_t().without(prop::location_place).with(prop::latitude));
	register_assert_format(df::search_t().year(2000));
	register_assert_format(df::search_t().year(2000).month(5));
	register_assert_format(df::search_t().day(25, 5, 1972));
	register_assert_format(df::search_t().day(25, 5, 0));
	register_assert_format(df::search_t().day(0, 5, 2005));
	register_assert_format(df::search_t().with(prop::tag, alfie));
	register_assert_format(df::search_t().with(prop::duration, 500));
	register_assert_format(df::search_t().with(u8"!-"sv));
	register_assert_format(df::search_t().without(prop::tag, alfie));
	register_assert_format(df::search_t().with(prop::tag, alfie).with(prop::tag, jana));
	register_assert_format(df::search_t().with(prop::tag, alfie).without(prop::tag, jana));
	register_assert_format(df::search_t().with(prop::tag, u8"tag with space"sv).without(prop::tag, u8"space tag"sv));
	register_assert_format(df::search_t().with(alfie).with(jana));
	register_assert_format(df::search_t().age(7, df::date_parts_prop::any));
	register_assert_format(df::search_t().age(7, df::date_parts_prop::any));
	register_assert_format(df::search_t().age(7, df::date_parts_prop::modified));
	register_assert_format(df::search_t().fuzzy(prop::duration, 33));
	register_assert_format(df::search_t().location(gps_coordinate(-30.515, 151.665), 5.0));
	register_assert_format(df::search_t().with_extension(u8"jpg"sv));

	auto register_assert_parse = [&tests](const std::u8string& query, const std::u8string& expected = {})
	{
		tests.add(str::format(u8"Should parse {}"sv, query), [query, expected](shared_test_context& stc)
		{
			const auto formatted = df::search_t::parse(query).format_terms();
			assert_equal(str::is_empty(expected) ? query : expected, formatted, u8"parse query"sv);
			const auto parsed = df::search_t::parse(query).format_terms();
			assert_equal(formatted, parsed, u8"parse formatted"sv);
		});
	};

	register_assert_parse(u8"2014 2015"s);
	register_assert_parse(u8"2014 @photo"s);
	register_assert_parse(u8"2014 -@video"s);
	register_assert_parse(u8"2014 @audio"s);
	register_assert_parse(u8"2014 or 2015"s);
	register_assert_parse(u8"Jana Alfie"s);
	register_assert_parse(u8"Jana or Alfie"s);
	register_assert_parse(u8"Jana -Alfie"s);
	register_assert_parse(u8"genre: Rock -artist: \"Counting Crows\""s);
	register_assert_parse(u8"Created:7"s);
	register_assert_parse(u8"Modified:7"s);
	register_assert_parse(u8"(2014 or 2015) (May or June)"s);
	register_assert_parse(u8"Bertie or ((Jana or Alfie) or Amalka)"s);
	register_assert_parse(u8"(2013 or 2014 or 2015) (May or June or July)"s);
	register_assert_parse(u8"Rating:5"s, u8"rating: 5"s);
	register_assert_parse(u8"dec-25"s);
	register_assert_parse(u8"2020-aug-16"s);
	register_assert_parse(u8"2020-aug"s);
	register_assert_parse(u8"modified:2020-aug"s);
	register_assert_parse(u8"modified:2020-aug-16"s);
	register_assert_parse(u8"modified:aug-16"s);
	register_assert_parse(u8"@duplicates"s);
	register_assert_parse(u8"loc:-30.515+151.66+5"s);
	register_assert_parse(u8"size:1mb"s, u8"size: 1 MB"s);
	register_assert_parse(u8"> size:1mb"s, u8"> size: 1 MB"s);
	register_assert_parse(u8"ext:jpg"s);
	register_assert_parse(u8"with:tag"s, u8"with: tag"s);
	register_assert_parse(u8"without:tag"s, u8"without: tag"s);
	register_assert_parse(u8"c:\\windows"s);
	register_assert_parse(u8"c:\\windows"s);
	register_assert_parse(u8"c:\\windows with:tag"s, u8"c:\\windows with: tag"s);
	register_assert_parse(u8"c:\\windows without:tag"s, u8"c:\\windows without: tag"s);
	register_assert_parse(u8"@ photo"s, u8"@photo"s);
	register_assert_parse(u8"# key1"s, u8"#key1"s);
	register_assert_parse(u8"# \"tag with space\""s, u8"#\"tag with space\""s);
	register_assert_parse(u8"#'tag with space'"s, u8"#\"tag with space\""s);

	//
	// Search execution
	//
	auto register_should_search = [&tests](const std::u8string_view query, int expected_index, int expected_recurse,
	                                       int expected_folder)
	{
		tests.add(str::format(u8"Should search {}"s, query),
		          [query, expected_index, expected_recurse, expected_folder](shared_test_context& stc)
		          {
			          stc.lazy_load_index();
			          assert_equal(expected_index, count_search_results(stc.test_index, query), query);

			          const auto query_recurse_test_folder = str::format(
				          u8"\"{}\\**\" {} -excluded"sv, test_files_folder, query);
			          assert_equal(expected_recurse, count_search_results(stc.empty_index, query_recurse_test_folder),
			                       str::format(u8"recurse {}"sv, query));

			          const auto query_test_folder = str::format(u8"\"{}\" {}"sv, test_files_folder, query);
			          assert_equal(expected_folder, count_search_results(stc.empty_index, query_test_folder),
			                       str::format(u8"folder {}"sv, query));
		          });
	};

	register_should_search(u8"2012-09-14"sv, 5, 5, 5);
	register_should_search(u8"Created:2012-09-14"sv, 5, 5, 5);
	register_should_search(u8"2010-05-25"sv, 0, 0, 0);
	register_should_search(u8"2010-5-25"sv, 0, 0, 0);
	register_should_search(u8"Created:2010-05-25"sv, 0, 0, 0);
	register_should_search(u8"2009-11-15"sv, 1, 1, 1);

	register_should_search(u8"@video"sv, 5, 5, 5);
	register_should_search(u8"@audio"sv, 5, 5, 4);
	register_should_search(u8"@commodore"sv, 1, 1, 0);
	register_should_search(u8"@archive"sv, 1, 1, 1);

	register_should_search(u8"@photo"sv, 28, 28, 27);
	register_should_search(u8"@ photo"sv, 28, 28, 27);
	register_should_search(u8"@   photo"sv, 28, 28, 27);

	register_should_search(u8"key1"sv, 4, 4, 4);
	register_should_search(u8"Tag:key1"sv, 4, 4, 4);
	register_should_search(u8"Tag :key1"sv, 4, 4, 4);
	register_should_search(u8"Tag: key1"sv, 4, 4, 4);
	register_should_search(u8"#key1"sv, 4, 4, 4);
	register_should_search(u8"# key1"sv, 4, 4, 4);
	register_should_search(u8"Tag:dog Tag:london"sv, 1, 1, 1);
	register_should_search(u8"not_exist"sv, 0, 0, 0);
	register_should_search(u8"Tag:not_exist"sv, 0, 0, 0);
	register_should_search(u8"#ke*"sv, 4, 4, 4);
	register_should_search(u8"ke*"sv, 5, 5, 5);

	register_should_search(u8"Megapixels:1.6 dog"sv, 1, 1, 1);
	register_should_search(u8"Megapixels:1.6"sv, 2, 2, 2);
	register_should_search(u8"(< Megapixels:1.0 > Megapixels:0.5)"sv, 7, 7, 7);
	register_should_search(u8"Megapixels:2"sv, 1, 1, 1);
	register_should_search(u8"pixels:2"sv, 1, 1, 1);
	register_should_search(u8"> pixels:1"sv, 11, 11, 10);
	register_should_search(u8">pixels:1"sv, 11, 11, 10);
	register_should_search(u8">pixels :1"sv, 11, 11, 10);
	register_should_search(u8">pixels: 1"sv, 11, 11, 10);
	register_should_search(u8"> pixels : 1"sv, 11, 11, 10);
	register_should_search(u8"2mp"sv, 1, 1, 1);
	register_should_search(u8"6000x4000"sv, 1, 1, 1);

	register_should_search(u8"Aperture:f/5"sv, 1, 1, 1);
	register_should_search(u8"f/3.5"sv, 6, 6, 6);
	register_should_search(u8"f/1.8"sv, 0, 0, 0);
	register_should_search(u8"f/4.0"sv, 0, 0, 0);
	register_should_search(u8"f/6.3"sv, 5, 5, 5);

	register_should_search(u8"with:Exposure @photo"sv, 15, 15, 14);
	register_should_search(u8"with: Exposure @ photo"sv, 15, 15, 14);
	register_should_search(u8"without:Exposure @photo"sv, 13, 13, 13);
	register_should_search(u8"without:Exposure"sv, 26, 26, 24);
	register_should_search(u8"with:Exposure"sv, 16, 16, 15);
	register_should_search(u8"with: Exposure"sv, 16, 16, 15);
	register_should_search(u8"ExposureTime:1/20s"sv, 1, 1, 1);
	register_should_search(u8"ExposureTime: 1/20s"sv, 1, 1, 1);
	register_should_search(u8"1/20s"sv, 1, 1, 1);
	register_should_search(u8"1/100s"sv, 5, 5, 5);
	register_should_search(u8"1/1000s"sv, 0, 0, 0);

	register_should_search(u8"iso400 @photo"sv, 2, 2, 2);
	register_should_search(u8"iso:400 @photo"sv, 2, 2, 2);
	register_should_search(u8"iso: 400 @photo"sv, 2, 2, 2);
	register_should_search(u8"iso : 400 @photo"sv, 2, 2, 2);
	register_should_search(u8">= iso:400 @photo"sv, 3, 3, 3);

	register_should_search(u8"1:26"sv, 1, 1, 1);
	register_should_search(u8"0:10"sv, 5, 5, 5);
	register_should_search(u8"7:77"sv, 0, 0, 0);
	register_should_search(u8"10:00"sv, 0, 0, 0);

	register_should_search(u8"size:0.3mb"sv, 5, 5, 5);
	register_should_search(u8"size:14kb"sv, 1, 1, 1);
	register_should_search(u8"size:5.1mb"sv, 1, 1, 1);
	register_should_search(u8">size:1mb"sv, 10, 10, 9);

	register_should_search(u8"key1"sv, 4, 4, 4);
	register_should_search(u8"dog london"sv, 1, 1, 1);
	register_should_search(u8"prague"sv, 5, 5, 5);
	register_should_search(u8"ipad"sv, 1, 1, 1);
	register_should_search(u8"48kHz"sv, 4, 4, 3);
	register_should_search(u8"44.1kHz"sv, 4, 4, 4);
	register_should_search(u8"tag:dog tag:london"sv, 1, 1, 1);
	register_should_search(u8"dog or london"sv, 3, 3, 3);
	register_should_search(u8"Rock"sv, 3, 3, 3);
	register_should_search(u8"canon @photo"sv, 10, 10, 9);
	register_should_search(u8"nikon d100"sv, 1, 1, 1);

	register_should_search(u8"ext:cr2"sv, 2, 2, 1);
	register_should_search(u8"ext:.cr2"sv, 2, 2, 1);

	register_should_search(u8"sony"sv, 1, 1, 1);
	register_should_search(u8"\"sony\""sv, 1, 1, 1);
	register_should_search(u8"sony*"sv, 1, 1, 1);
	register_should_search(u8"*sony*"sv, 1, 1, 1);

	register_should_search(u8"screws"sv, 1, 1, 0);
	register_should_search(u8"\"screws\""sv, 1, 1, 0);
	register_should_search(u8"screws*"sv, 1, 1, 0);
	register_should_search(u8"*screws*"sv, 1, 1, 0);

	register_should_search(u8"london"sv, 3, 3, 3);
	register_should_search(u8"\"london\""sv, 3, 3, 3);
	register_should_search(u8"london*"sv, 3, 3, 3);
	register_should_search(u8"*london*"sv, 3, 3, 3);

	register_should_search(u8"d64"sv, 1, 1, 0);
	register_should_search(u8"ace -retro"sv, 1, 1, 1);
	register_should_search(u8"jpg"sv, 15, 15, 15);
	register_should_search(u8"-jpg"sv, 30, 29, 28);
	register_should_search(u8"-ext:jpg"sv, 30, 29, 28);

#ifndef _DEBUG
	tests.add(u8"Should Find Closest Location"s, should_find_closest_location);
#endif
}
