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
#include "av_sound.h"
#include "test_utils.h"
#include "ui_text_edit.h"
#include "view_tags.h"

static void should_edit_single_line_text()
{
	ui::single_line_edit_model edit;
	edit.text("Hello world");
	edit.select(6, 11);
	edit.insert("Diffractor");
	assert_equal_strict("Hello Diffractor", edit.text(), "replace selection");

	edit.move_left(true);
	assert_equal(true, edit.has_selection(), "shift-left selects");
	edit.move_left();
	assert_equal(false, edit.has_selection(), "left collapses selection");
	assert_equal(15_z, edit.caret(), "left collapses to selection start");

	edit.text("A\u00e9B");
	edit.move_left();
	edit.backspace();
	assert_equal_strict("AB", edit.text(), "backspace removes one UTF-8 code point");
	assert_equal(1_z, edit.caret(), "caret remains on UTF-8 boundary");

	edit.select_all();
	edit.insert("one\r\ntwo\nthree");
	assert_equal_strict("one two three", edit.text(), "paste is normalized to one line");

	edit.text("one two three");
	edit.move_word_left();
	assert_equal(8_z, edit.caret(), "control-left moves to previous word");
	edit.backspace_word();
	assert_equal_strict("one three", edit.text(), "control-backspace removes previous word");
	edit.undo();
	assert_equal_strict("one two three", edit.text(), "undo restores word deletion");
	edit.redo();
	assert_equal_strict("one three", edit.text(), "redo reapplies word deletion");

	edit.text("alpha beta");
	edit.select_word(7);
	assert_equal_strict("beta", edit.selected_text(), "double-click model selects a word");
	edit.erase_selection();
	assert_equal_strict("alpha ", edit.text(), "cut removes selected word");
	edit.undo();
	assert_equal_strict("alpha beta", edit.text(), "undo restores cut text");

	edit.begin_edit();
	edit.select_all();
	edit.insert("changed");
	edit.cancel_edit();
	assert_equal_strict("alpha beta", edit.text(), "cancel restores edit-start value");

	filter_t filter;
	filter.wildcard("cat");
	assert_equal_strict("cat", filter.text(), "filter preserves user input");
	assert_equal(true, filter.match_text(str::cache("bobcatfish")), "filter applies contains matching");
}

static void should_settle_transport_stream_extension_by_header()
{
	assert_equal(true, files::has_media_header_rule(".ts"), "the transport stream extension has a header rule");
	assert_equal(true, files::has_media_header_rule("m2ts"), "the rule ignores a leading dot");
	assert_equal(false, files::has_media_header_rule(".mp4"), "an unambiguous extension is left to the decoder");

	std::array<uint8_t, files::media_header_probe_bytes> header{};

	const auto write_packets = [&header](const size_t start, const size_t packet_size)
	{
		header.fill(0);
		for (auto i = size_t{0}; i < 4; ++i) header[start + i * packet_size] = 0x47;
	};

	write_packets(0, 188);
	assert_equal(true, files::media_header_matches(".ts", {header.data(), header.size()}),
	             "a broadcast packet run is accepted");

	write_packets(4, 192);
	assert_equal(true, files::media_header_matches(".m2ts", {header.data(), header.size()}),
	             "an M2TS timestamp prefix is accepted");

	write_packets(0, 204);
	assert_equal(true, files::media_header_matches(".ts", {header.data(), header.size()}),
	             "a Reed-Solomon packet run is accepted");

	// A capture that begins mid-packet still aligns further in, and ffmpeg would find it, so refusing
	// it here would hide real video.
	write_packets(97, 188);
	assert_equal(true, files::media_header_matches(".ts", {header.data(), header.size()}),
	             "a stream that starts mid-packet is accepted");

	// A TypeScript file that opens with 'G' (0x47) matches the sync byte but not the packet run.
	const std::string_view typescript = "Get the exported type before anything else is imported;\n";
	header.fill(0);
	std::memcpy(header.data(), typescript.data(), typescript.size());
	assert_equal(false, files::media_header_matches(".ts", {header.data(), typescript.size()}),
	             "TypeScript source is not mistaken for a transport stream");

	assert_equal(true, files::media_header_matches(".mp4", {header.data(), typescript.size()}),
	             "an extension with no rule always matches");
}

static void should_offer_every_matching_tool()
{
	const auto make_tool = [](const std::string_view exe, const std::string_view group)
	{
		auto tool = std::make_shared<file_tool>();
		tool->exe = str::cache(exe);
		tool->text = str::cache(exe);
		tool->group = str::cache(group);
		tool->invoke_text = str::cache("{exe-path} {item-path}");
		return tool;
	};

	const auto first = make_tool("first", "photo");
	const auto second = make_tool("second", "photo");
	const auto player = make_tool("player", "video");

	file_tools_result result;
	result.by_extension[str::cache("jpg")] = {first, second};
	result.by_group[str::cache("photo")] = {second};
	result.by_group[str::cache("video")] = {player};
	apply_tools(std::move(result));
	// The tool table is process-global; a failing assertion below must not leave it replaced.
	const df::scope_exit restore_tools([] { apply_tools({}); });

	const auto jpeg = files::file_type_from_name("test.jpg");
	const auto tools = jpeg->all_tools();
	assert_equal(2_z, tools.size(), "every tool declaring the extension is offered");
	assert_equal_strict("first", tools[0]->text.sv(), "config order is preserved");
	assert_equal_strict("second", tools[1]->text.sv(), "a tool matched by extension and group is listed once");

	const auto mp4 = files::file_type_from_name("test.mp4");
	assert_equal(1_z, mp4->all_tools().size(), "group membership offers a tool with no extension match");

	assert_equal(false, first->invoke(df::file_path{}), "an unresolved executable is never launched");

	apply_tools({});
	assert_equal(0_z, jpeg->all_tools().size(), "reapplying replaces rather than accumulates");
}


static void should_compact_consumed_audio_only_when_needed()
{
	audio_info_t format;
	format.channel_layout = av_get_def_channel_layout(2);
	format.sample_fmt = prop::audio_sample_t::signed_16bit;
	format.sample_rate = 10;

	audio_buffer buffer;
	buffer.init(format);

	const std::array<uint8_t, 60> first{};
	const std::array<uint8_t, 40> second{};
	buffer.append(first.data(), static_cast<uint32_t>(first.size()), 1.0, 1);
	buffer.remove(40);
	buffer.append(second.data(), static_cast<uint32_t>(second.size()), 2.0, 1);

	assert_equal(60u, buffer.used_bytes(), "audio bytes retained after cursor compaction");
	assert_equal(1.5, buffer.seconds(), "audio duration retained after cursor compaction");
	assert_equal(1.5, buffer.start_time(), "audio start time follows appended frame timing");
	assert_equal(3.0, buffer.end_time(), "audio end time follows appended frame timing");

	audio_info_t visualizer_format;
	visualizer_format.channel_layout = av_get_def_channel_layout(2);
	visualizer_format.sample_fmt = prop::audio_sample_t::signed_16bit;
	visualizer_format.sample_rate = 48000;

	audio_buffer visualizer_buffer;
	visualizer_buffer.init(visualizer_format);
	std::array<int16_t, FFT_BUFFER_SIZE * 4> samples{};
	std::fill_n(samples.begin(), FFT_BUFFER_SIZE * 2, 100);
	for (size_t frame = 0; frame < FFT_BUFFER_SIZE; ++frame)
	{
		const auto sample = static_cast<int16_t>(12000.0 * sin(2.0 * M_PI * 16.0 * frame / FFT_BUFFER_SIZE));
		const auto i = FFT_BUFFER_SIZE * 2 + frame * 2;
		samples[i] = sample;
		samples[i + 1] = sample;
	}
	visualizer_buffer.append(std::bit_cast<const uint8_t*>(samples.data()),
	                         static_cast<uint32_t>(samples.size() * sizeof(int16_t)), 0.0, 1);
	visualizer_buffer.remove(FFT_BUFFER_SIZE * 4);

	av_visualizer visualizer;
	visualizer.update(visualizer_buffer);
	assert_equal(true, visualizer.step(1.0),
	             "visualizer consumes the live audio window");
	assert_equal(true, std::any_of(std::begin(visualizer._frame._data[0]),
	                               std::end(visualizer._frame._data[0]), [](const int bar) { return bar > 0; }),
	             "visualizer ignores consumed samples before the cursor");

	audio_info_t masked;
	masked.channel_layout = av_get_channel_layout(3, 1);
	assert_equal(2u, masked.channel_count(), "speaker mask defines channel count");
	audio_info_t fallback;
	fallback.channel_layout = av_get_channel_layout(0, 6);
	assert_equal(6u, fallback.channel_count(), "missing speaker mask uses endpoint channel count");
}

// audio_buffer keeps its samples private; this declared friend lets the test read them.
struct audio_ramp_probe
{
	static int16_t sample(const audio_buffer& buffer, const size_t i)
	{
		return std::bit_cast<const int16_t*>(buffer.data + buffer.start_pos)[i];
	}
};

static void should_ramp_audio_at_buffer_edges()
{
	audio_info_t format;
	format.channel_layout = av_get_def_channel_layout(2);
	format.sample_fmt = prop::audio_sample_t::signed_16bit;
	format.sample_rate = 1000;

	audio_buffer buffer;
	buffer.init(format);

	std::array<int16_t, 2000> samples{};
	samples.fill(1000);
	buffer.append(std::bit_cast<const uint8_t*>(samples.data()),
	              static_cast<uint32_t>(samples.size() * sizeof(int16_t)), 0.0, 1);

	buffer.apply_fade_in(0.010);
	buffer.apply_fade_out(0.010);

	// 1000 frames of stereo audio at 1kHz, so a 10ms ramp covers 10 frames = 20 samples.
	const auto sample = [&buffer](const size_t i) { return audio_ramp_probe::sample(buffer, i); };

	assert_equal(true, sample(0) < 200, "playback starts near silence");
	assert_equal(1000, static_cast<int>(sample(19)), "the fade in reaches full level after 10ms");
	assert_equal(1000, static_cast<int>(sample(1000)), "audio between the ramps is untouched");
	assert_equal(0, static_cast<int>(sample(1999)), "playback ends at silence");
	assert_equal(true, sample(1979) == 1000, "the fade out only covers the last 10ms");
	assert_equal(4000u, buffer.used_bytes(), "ramping does not consume buffered audio");
}

static void should_time_visualizer_independently_of_refresh_rate()
{
	auto animate = [](const double frame_seconds)
	{
		av_visualizer visualizer;
		av_visualizer::frame peak(0.0);
		peak._data[0][8] = 1000;
		visualizer._frames.push(peak);

		for (auto time = 0.0; time <= 0.1; time += frame_seconds)
		{
			visualizer.step(time);
		}

		return visualizer._frame._data[0][8];
	};

	const auto level_30hz = animate(1.0 / 30.0);
	const auto level_120hz = animate(1.0 / 120.0);
	assert_equal(true, std::abs(level_30hz - level_120hz) < 30,
	             "visualizer attack is independent of display refresh rate");

	av_visualizer visualizer;
	av_visualizer::frame loud(0.010);
	loud._data[0][8] = 1000;
	av_visualizer::frame quiet(0.020);
	visualizer._frames.push(loud);
	visualizer._frames.push(quiet);
	visualizer.step(0.0);
	assert_equal(true, visualizer._frame._data[0][8] > 0,
	             "visualizer preserves a transient between presentation frames");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
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
	const location_cache locations;
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

static void should_calculate_history_span_from_start_year()
{
	assert_equal(10, df::history_year_count(0, 2026), "empty start year defaults to 10 years");
	assert_equal(17, df::history_year_count(2010, 2026), "start year is inclusive");
	assert_equal(1, df::history_year_count(2030, 2026), "future start year clamps to current year");
	assert_equal(df::max_history_years, df::history_year_count(1900, 2026), "old start year clamps to capacity");
	assert_equal(17, df::history_row_count(17, 1), "one year per row");
	assert_equal(9, df::history_row_count(17, 2), "two years per row rounds up");
}

static void should_group_sidebar_map_by_place()
{
	const location_cache locations;
	index_histograms histograms;

	const auto record = [&]
	{
		df::index_file_item file;
		file.ft = files::file_type_from_name("test.jpg");
		const auto metadata = std::make_shared<prop::item_metadata>();
		metadata->coordinate = gps_coordinate(50.08806, 14.42083);
		metadata->location_place = "Prague"_c;
		metadata->location_state = "Prague"_c;
		metadata->location_country = "Czechia"_c;
		file.metadata.store(metadata);
		histograms.record(locations, file);
	};

	record();
	record();

	const auto map_loc = df::location_heat_map::calc_map_loc({50.08806, 14.42083});
	const auto heat = histograms._locations.coordinates[
		map_loc.y * df::location_heat_map::map_width + map_loc.x];
	assert_equal(2u, heat, "map heat accumulates repeated coordinates");
	assert_equal(1u, static_cast<uint32_t>(histograms._location_groups.size()), "one place group");
	const auto& group = histograms._location_groups.begin()->second;
	assert_equal(2u, group.count, "place group count");
	assert_equal(true, !str::is_empty(group.country), "place group country");
	assert_equal(true, group.centroid().distance_in_kilometers({50.08806, 14.42083}) < 0.01,
	             "place group uses photo centroid");
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
	assert_equal(0xAC00, str::normalze_for_compare(0xAC00), "Hangul normalise identity");

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

///////////////////////////////////////////////////////////////////////////////////////////////////
// Issue #134 - Emojis breaking labels and rating
// The filename half of this issue is covered end-to-end by
// "Issue #134: Should update rating and label for emoji filename" in test_media_edit.cpp.
// What follows is the string half: emoji are non-BMP code points (surrogate pairs in UTF-16,
// 4-byte UTF-8) and must round-trip through tag parsing, case-folding and search like any
// other Unicode text, without corrupting neighbouring values.
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_handle_emoji_tags()
{
	constexpr auto party = "\U0001F389"; // 🎉
	constexpr auto cat = "\U0001F431"; // 🐱

	// Space-separated emoji split into individual tags.
	tag_set tags(std::format("{} {}", party, cat));
	assert_equal(2, static_cast<int>(tags.size()), "emoji tag count");

	// Case-folding a non-BMP code point is the identity (no case).
	assert_equal_strict(party, str::to_lower(party), "emoji to_lower identity");
	assert_equal(0, str::icmp(party, party), "emoji icmp equal");
	assert_equal(true, str::icmp(party, cat) != 0, "different emoji differ");

	// Removing one emoji tag leaves the other intact.
	tags.remove(tag_set(cat));
	assert_equal(1, static_cast<int>(tags.size()), "emoji tag count after remove");
	assert_equal(true, tags.to_string().find(cat) == std::string::npos, "cat emoji removed");
	assert_equal(true, tags.to_string().find(party) != std::string::npos, "party emoji remains");

	// An emoji tag is searchable via a #tag query.
	prop_test().tag(party)
	           .is_match(std::format("#{}", party))
	           .is_not_match(std::format("#{}", cat));
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
// Issue #58 - Search "with"/"without" scope returns identical results
// "with:<property>" must match only items that HAVE the property and
// "without:<property>" must match only items that LACK it. Previously both
// scopes produced the same result set.
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_distinguish_with_without_scope()
{
	// An item that HAS a genre matches with:genre and must NOT match without:genre.
	prop_test().genre("Rock")
	           .is_match("with:genre")
	           .is_not_match("without:genre");

	// An item that LACKS a genre matches without:genre and must NOT match with:genre.
	// (rating is set only so the item still carries metadata; genre stays empty.)
	prop_test().rate(3)
	           .is_match("without:genre")
	           .is_not_match("with:genre");

	// The same complementary behaviour must hold for another property (rating).
	prop_test().rate(5)
	           .is_match("with:rating")
	           .is_not_match("without:rating");

	prop_test().genre("Jazz")
	           .is_match("without:rating")
	           .is_not_match("with:rating");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Issue #184 - 'Group by date created' uses the wrong date (ignores DateTimeOriginal)
// The media "created" date (used for group-by/sort-by date created and the
// displayed creation date) must prefer the EXIF DateTimeOriginal capture time
// over the container/file creation time, falling back to the latter only when
// no capture time is present.
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_prefer_datetimeoriginal_for_created()
{
	// DateTimeOriginal (created_exif) wins over the file/container date (created_utc).
	prop::item_metadata md;
	md.created_utc = df::date_t(2020, 1, 1, 12, 0, 0); // e.g. date the file was written
	md.created_exif = df::date_t(2005, 6, 15, 9, 30, 0); // capture time
	assert_equal(df::date_t(2005, 6, 15, 9, 30, 0), md.created(), "prefers DateTimeOriginal");

	// With no capture time, fall back to the container/file creation date.
	prop::item_metadata md2;
	md2.created_utc = df::date_t(2020, 1, 1, 12, 0, 0);
	assert_equal(md2.created_utc.system_to_local(), md2.created(), "falls back to created_utc");

	// With neither set the date is invalid (the item then uses the file date).
	constexpr prop::item_metadata md3;
	assert_equal(false, md3.created().is_valid(), "no dates -> invalid");
}

static void should_classify_aspect_ratio_groups()
{
	const auto assert_group = [](const sizei dimensions, const aspect_ratio_bucket expected_bucket,
	                             const bool expected_portrait, const std::string_view name)
	{
		const auto actual = calc_aspect_ratio_group(dimensions);
		assert_equal(static_cast<int>(expected_bucket), static_cast<int>(actual.bucket), name, "aspect ratio bucket");
		assert_equal(expected_portrait, actual.is_portrait, name, "aspect ratio orientation");
	};

	assert_group({1000, 1000}, aspect_ratio_bucket::square, false, "square");
	assert_group({1280, 1024}, aspect_ratio_bucket::five_four, false, "5:4");
	assert_group({4000, 3000}, aspect_ratio_bucket::four_three, false, "4:3");
	assert_group({6000, 4000}, aspect_ratio_bucket::three_two, false, "3:2");
	assert_group({1920, 1200}, aspect_ratio_bucket::sixteen_ten, false, "16:10");
	assert_group({1920, 1080}, aspect_ratio_bucket::sixteen_nine, false, "16:9");
	assert_group({2520, 1080}, aspect_ratio_bucket::twenty_one_nine, false, "21:9");
	assert_group({1080, 1920}, aspect_ratio_bucket::sixteen_nine, true, "9:16 portrait");
	assert_group({4032, 3024}, aspect_ratio_bucket::four_three, false, "cropped within tolerance");
	assert_group({1000, 700}, aspect_ratio_bucket::other, false, "other landscape");
	assert_group({700, 1000}, aspect_ratio_bucket::other, true, "other portrait");
	assert_group({}, aspect_ratio_bucket::other, false, "invalid dimensions");
}

static void should_clear_detail_row_layout_metrics()
{
	df::item_row_draw_info info;
	info.title.extent = 400;
	info.title.width = 300;
	info.file_size.extent = 100;
	info.file_size.width = 80;
	info.file_size.val_min = 10;
	info.file_size.val_max = 1000;
	info.presence.extent = 40;
	info.presence.width = 40;

	info.clear_for_layout();

	assert_equal(0, info.title.extent, "title extent reset");
	assert_equal(0, info.title.width, "title width reset");
	assert_equal(0, info.file_size.extent, "size extent reset");
	assert_equal(0, info.file_size.width, "size width reset");
	assert_equal(static_cast<double>(INT64_MAX), info.file_size.val_min, "size minimum reset");
	assert_equal(static_cast<double>(INT64_MIN), info.file_size.val_max, "size maximum reset");
	assert_equal(0, info.presence.extent, "presence extent reset");
	assert_equal(0, info.presence.width, "presence width reset");
}

static void should_animate_alpha_between_values()
{
	// The gate is off when the CPU software backend is active, so force the animated
	// behaviour under test and restore whatever the running renderer selected.
	const auto restore_animations = ui::animations_enabled;
	const df::scope_exit restore_scope([restore_animations] { ui::animations_enabled = restore_animations; });
	ui::animations_enabled = true;

	ui::animate_alpha alpha;
	alpha.reset(0.0f, 1.0f);
	assert_equal(0.0f, alpha.val(), "fade-in starts at zero");
	assert_equal(1.0f, alpha.target(), "fade-in targets one");
	assert_equal(true, alpha.step(), "fade-in advances");
	assert_equal(true, alpha.val() > 0.0f && alpha.val() < 1.0f, "fade-in interpolates");

	alpha.target(0.0f);
	assert_equal(true, alpha.val() > 0.0f, "retarget preserves current value");
	assert_equal(true, alpha.step(), "fade-out advances");

	for (auto i = 0; i < 100; ++i) alpha.step();
	assert_equal(0.0f, alpha.val(), "fade-out reaches target");
	assert_equal(false, alpha.step(), "completed animation stops");

	alpha.reset(0.0005f, 0.0f);
	assert_equal(true, alpha.step(), "final snap requests a frame");
	assert_equal(0.0f, alpha.val(), "final snap reaches target");
	assert_equal(false, alpha.step(), "final snap completes once");
}

static void should_skip_alpha_animation_when_disabled()
{
	// CPU software rendering cannot afford per-frame fades: every alpha reaches its target
	// immediately so draws stay on the opaque fast paths and no extra frames are requested.
	const auto restore_animations = ui::animations_enabled;
	const df::scope_exit restore_scope([restore_animations] { ui::animations_enabled = restore_animations; });
	ui::animations_enabled = false;

	ui::animate_alpha alpha;
	alpha.reset(0.0f, 1.0f);
	assert_equal(1.0f, alpha.val(), "fade-in starts complete");
	assert_equal(false, alpha.step(), "fade-in requests no frame");

	alpha.target(0.0f);
	assert_equal(0.0f, alpha.val(), "fade-out completes immediately");
	assert_equal(false, alpha.step(), "fade-out requests no frame");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// The media-type filter (photo/video/audio) survives a save/restore cycle. The
// filter is serialized to a stable
// comma-separated string of group names and rebuilt from it on load.
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_persist_media_filter()
{
	filter_t filter;
	filter.toggle(file_group::photo);
	filter.toggle(file_group::video);

	// Round-trip through the same serialization used to persist the setting.
	const auto restored = media_filter_from_string(media_filter_to_string(filter));

	assert_equal(true, restored.has_group(file_group::photo), "photo filter restored");
	assert_equal(true, restored.has_group(file_group::video), "video filter restored");
	assert_equal(false, restored.has_group(file_group::audio), "audio filter stays off");

	// An empty filter round-trips to an empty filter (no groups selected).
	const auto empty = media_filter_from_string(media_filter_to_string(filter_t{}));
	assert_equal(true, empty.is_empty(), "empty filter round-trips to empty");

	// Unknown group names are ignored rather than producing bogus groups.
	const auto bogus = media_filter_from_string("photo,not_a_group");
	assert_equal(true, bogus.has_group(file_group::photo), "known group parsed");
	assert_equal(1_z, bogus.groups().size(), "unknown group ignored");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Issue #228 - the tag view's action field appeared blank.
// The field is the action to apply ("what to add or remove"), not a view of the current tags,
// so it starts empty by design. What it must get right is the round-trip: a '-' prefix means
// remove, a tag containing white space is quoted, a repeated tag keeps only the last modifier,
// and matching is case-insensitive so "Beach" and "beach" are one action.
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_round_trip_tag_actions()
{
	// The field is an action list, so no text means no actions - not "all current tags".
	assert_equal(0_z, parse_tag_actions({}).size(), "an empty field is no actions");
	assert_equal(0_z, parse_tag_actions("   ").size(), "white space alone is no actions");
	assert_equal_strict("", serialize_tag_actions({}), "no actions serializes to an empty field");

	const auto actions = parse_tag_actions("beach -winter \"new york\" -\"old town\"");
	assert_equal(4_z, actions.size(), "action count");

	assert_equal_strict("beach", actions[0].first, "first tag");
	assert_equal(true, actions[0].second, "unprefixed tag is an add");
	assert_equal_strict("winter", actions[1].first, "second tag");
	assert_equal(false, actions[1].second, "'-' prefixed tag is a remove");
	assert_equal_strict("new york", actions[2].first, "quoted tag keeps its space");
	assert_equal(true, actions[2].second, "quoted tag is an add");
	assert_equal_strict("old town", actions[3].first, "quoted removal keeps its space");
	assert_equal(false, actions[3].second, "quoted '-' prefixed tag is a remove");

	// Serializing re-quotes and re-prefixes, so the field survives an edit/apply/edit cycle.
	const auto text = serialize_tag_actions(actions);
	const auto reparsed = parse_tag_actions(text);
	assert_equal(actions.size(), reparsed.size(), "round-trip action count");

	for (auto i = 0_z; i < actions.size(); ++i)
	{
		assert_equal_strict(actions[i].first, reparsed[i].first, "round-trip tag");
		assert_equal(actions[i].second, reparsed[i].second, "round-trip modifier");
	}

	// A tag typed twice is one action, matched case-insensitively, and the last modifier wins.
	const auto deduped = parse_tag_actions("Beach -beach");
	assert_equal(1_z, deduped.size(), "a repeated tag is one action");
	assert_equal_strict("Beach", deduped[0].first, "the first spelling is kept");
	assert_equal(false, deduped[0].second, "the last modifier wins");

	const auto readded = parse_tag_actions("-beach Beach");
	assert_equal(1_z, readded.size(), "a repeated tag is one action either way round");
	assert_equal(true, readded[0].second, "the last modifier wins the other way round");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Issue #229 - switching a group between thumbnails and details did not stick: any search that
// recreated the groups reverted them. The choice is held per media type as a bitmask over
// group_key_type in setting.detail_items, which is what both the group toggle and the group
// construction read, and what the settings store persists.
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_persist_detail_display_per_media_type()
{
	const auto saved = setting.detail_items;
	const df::scope_exit restore([saved] { setting.detail_items = saved; });

	setting.detail_items = 0;

	setting.set_detail_display(group_key_type::photo, true);
	assert_equal(true, setting.is_detail_display(group_key_type::photo), "photo set to detail");

	// Each media type is an independent bit: setting one must not change the others.
	assert_equal(false, setting.is_detail_display(group_key_type::video), "video unaffected");
	assert_equal(false, setting.is_detail_display(group_key_type::audio), "audio unaffected");
	assert_equal(false, setting.is_detail_display(group_key_type::folder), "folder unaffected");

	setting.set_detail_display(group_key_type::video, true);
	setting.set_detail_display(group_key_type::photo, false);
	assert_equal(false, setting.is_detail_display(group_key_type::photo), "photo back to thumbnails");
	assert_equal(true, setting.is_detail_display(group_key_type::video), "video still detail");

	// Clearing a type that was never set must not disturb the ones that were.
	setting.set_detail_display(group_key_type::audio, false);
	assert_equal(true, setting.is_detail_display(group_key_type::video), "video survives an unrelated clear");

	// Every media type has its own bit, so no two types can alias each other.
	constexpr group_key_type all[] = {
		group_key_type::folder, group_key_type::photo, group_key_type::video, group_key_type::audio,
		group_key_type::grouped_value, group_key_type::grouped_no_value, group_key_type::archive,
		group_key_type::retro, group_key_type::other,
	};

	for (const auto type : all)
	{
		setting.detail_items = 0;
		setting.set_detail_display(type, true);
		assert_equal(true, setting.is_detail_display(type), "media type set to detail");

		auto others_unset = true;

		for (const auto other : all)
		{
			if (other != type && setting.is_detail_display(other)) others_unset = false;
		}

		assert_equal(true, others_unset, "no other media type shares this bit");
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Issue #135 - Rating/labeling via NumPad not working
// The rating (0-5) and label (6-9) shortcuts are bound to the top-row digit keys
// '0'..'9'. With NumLock on, the numeric keypad sends the distinct VK_NUMPAD0..9
// virtual-key codes (0x60..0x69), which never matched the '0'..'9' bindings, so the
// keypad could not rate or label. keys::normalize_numpad maps the keypad digits onto
// the equivalent top-row digit while leaving every other key untouched.
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_map_numpad_digits_to_rating_keys()
{
	// Every keypad digit VK_NUMPAD0..VK_NUMPAD9 maps to the matching top-row digit,
	// so the rating (0-5) and label (6-9) accelerators fire from the keypad.
	for (char32_t d = 0; d <= 9; ++d)
	{
		const auto numpad_key = keys::numpad0 + d;
		assert_equal(static_cast<char32_t>(U'0' + d), keys::normalize_numpad(numpad_key),
		             "keypad digit maps to top-row digit");
	}

	// Top-row digits are already correct and must pass through unchanged.
	for (char32_t c = U'0'; c <= U'9'; ++c)
	{
		assert_equal(c, keys::normalize_numpad(c), "top-row digit unchanged");
	}

	// Non-digit keys (letters, and the keypad codes just outside the digit range) are
	// left untouched so unrelated shortcuts are not disturbed.
	assert_equal(U'A', keys::normalize_numpad(U'A'), "letter unchanged");
	assert_equal(keys::numpad0 - 1, keys::normalize_numpad(keys::numpad0 - 1), "below range unchanged");
	assert_equal(keys::numpad9 + 1, keys::normalize_numpad(keys::numpad9 + 1), "above range unchanged");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Issue #227 - Removed default sidebar tags reappear after restart
// The default favorite (sidebar) tags must be seeded only on the very first run. Because
// favorite_tags is persisted as one string, an empty saved value is otherwise treated as
// "never configured" and the defaults are re-injected on every launch, resurrecting the
// tags the user deliberately removed. The persisted favorite_tags_initialized flag records
// that favorites have been configured; after that an empty list is respected.
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_only_seed_favorite_tags_on_first_run()
{
	// First run: nothing configured yet (flag unset) and the list is empty -> seed defaults.
	assert_equal(true, should_seed_default_favorite_tags(false, true, true), "first run seeds defaults");

	// After the user configured and then cleared favorites (flag set, list empty) the empty
	// list must be respected -- the removed defaults must NOT come back.
	assert_equal(false, should_seed_default_favorite_tags(true, true, false), "cleared list stays cleared");

	// A configured, non-empty list is never overwritten.
	assert_equal(false, should_seed_default_favorite_tags(true, false, false), "configured list not seeded");

	// Upgrade case: an existing user has favorites (non-empty) but no flag yet -> do NOT
	// re-seed over their existing tags.
	assert_equal(false, should_seed_default_favorite_tags(false, false, false), "existing tags preserved on upgrade");

	// Upgrade from a version without the initialization flag: an existing empty saved list
	// means the user removed every favorite, not that this is a first run.
	assert_equal(false, should_seed_default_favorite_tags(false, true, false), "legacy empty list stays empty");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// 1.27.0 - the app crashed during startup with the sidebar hidden, before the user could act.
// A start that never reaches idle is counted; once enough consecutive starts have failed that
// way the next one reverts presentation instead of repeating the crash. The threshold has to
// tolerate a single unsettled start, which is also what a kill or power loss during launch
// looks like, without needing a third crash before it helps.
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_start_safe_only_after_repeated_failures()
{
	assert_equal(false, should_start_safe(0), "a clean history starts normally");
	assert_equal(false, should_start_safe(1), "one unsettled start could be a kill or power loss");
	assert_equal(true, should_start_safe(2), "two in a row is a reproducible startup crash");
	assert_equal(true, should_start_safe(7), "and it stays safe until one start settles");

	// The reset covers what the window puts on screen and turns the graphics path off, and must
	// leave everything the user cannot re-reach from a working window alone.
	settings_t s;
	s.show_sidebar = false;
	s.large_font = true;
	s.show_debug_info = true;
	s.sidebar.show_world_map = false;
	s.sidebar.width = 900;
	s.use_gpu = true;
	s.use_d3d11va = true;
	s.write_folder = "c:\\keep-me";
	s.favorite_tags = "keep";
	s.language = "de";

	s.reset_presentation();

	assert_equal(true, s.show_sidebar, "the sidebar comes back");
	assert_equal(false, s.large_font, "font scale returns to default");
	assert_equal(false, s.show_debug_info, "debug overlay off");
	assert_equal(true, s.sidebar.show_world_map, "sidebar contents return to default");
	assert_equal(0, s.sidebar.width, "sidebar width returns to auto");
	assert_equal(false, s.use_gpu, "hardware acceleration off, not merely defaulted");
	assert_equal(false, s.use_d3d11va, "hardware video decode off");
	assert_equal(false, s.use_yuv, "hardware yuv textures off");
	assert_equal("c:\\keep-me"s, s.write_folder, "user paths survive");
	assert_equal("keep"s, s.favorite_tags, "favorite tags survive");
	assert_equal("de"s, s.language, "language survives");
}

static void should_restore_history_selection()
{
	history_state history;
	const auto search_a = df::search_t::parse("one");
	const auto search_b = df::search_t::parse("two");
	const auto unresolved_area = df::search_t::parse("area:Munich");
	auto resolved_area = unresolved_area;
	resolved_area.resolve_area(map_location_area{.name = "Munich", .cell = {136, 29}, .cell_span = 1});
	const auto selected_a = df::file_path("c:\\one.jpg");
	const auto selected_b = df::file_path("c:\\two.jpg");

	history.history_add(search_a, {});
	history.history_add(search_b, df::paths{{selected_a}, {}});

	history_state::history_entry entry;
	assert_equal(true, history.move_history_pos(-1, df::paths{{selected_b}, {}}, entry), "browse back succeeds");
	assert_equal(true, entry.search == search_a, "back restores first search");
	assert_equal(true, entry.selected.files.size() == 1 && entry.selected.files.front() == selected_a,
	             "back restores first selection");
	history.history_add(entry.search, df::paths{{selected_b}, {}});

	assert_equal(true, history.move_history_pos(1, entry.selected, entry), "browse forward succeeds");
	assert_equal(true, entry.search == search_b, "forward restores second search");
	assert_equal(true, entry.selected.files.size() == 1 && entry.selected.files.front() == selected_b,
	             "forward restores second selection");
	history.history_add(entry.search, df::paths{{selected_a}, {}});

	assert_equal(true, history.move_history_pos(-1, entry.selected, entry), "second browse back succeeds");
	assert_equal(true, entry.selected.files.size() == 1 && entry.selected.files.front() == selected_a,
	             "opening history entries does not overwrite their selection");

	history_state area_history;
	area_history.history_add(unresolved_area, {});
	area_history.replace_current_search(unresolved_area, resolved_area);
	assert_equal(true, area_history._history.back().search == resolved_area,
	             "deferred area resolution updates the current history entry");
	assert_equal(1_z, area_history._history.size(), "deferred area resolution does not add a history entry");
}

static void should_estimate_decode_cost()
{
	// The estimate reads only the header fields, but an image with no bytes counts as empty.
	const auto make_image = [](const ui::image_format format)
	{
		df::blob bytes;
		bytes.resize(16);
		return std::make_shared<ui::image>(std::move(bytes), sizei{4000, 3000}, format,
		                                   ui::orientation::top_left);
	};

	const auto png = make_image(ui::image_format::PNG);
	assert_equal(4000ll * 3000ll * 4, static_cast<uint64_t>(
		             files::estimate_decode_bytes(png, {400, 300})),
	             "a PNG builds the whole frame however small a target is asked for");

	const auto jpeg = make_image(ui::image_format::JPEG);
	assert_equal(500ll * 375ll * 4, static_cast<uint64_t>(
		             files::estimate_decode_bytes(jpeg, {400, 300})),
	             "libjpeg reduces by up to 1/8 while decoding");
	assert_equal(4000ll * 3000ll * 4, static_cast<uint64_t>(
		             files::estimate_decode_bytes(jpeg, {4000, 3000})),
	             "a full-size request costs a JPEG its whole frame");

	assert_equal(1000ll * 1000ll * 4, static_cast<uint64_t>(
		             files::estimate_decode_bytes(sizei{1000, 1000})),
	             "a bare source size costs four bytes a pixel");
}

static void should_refuse_over_budget_sources()
{
	const auto restore_budget = df::max_decode_bytes;
	const df::scope_exit restore([restore_budget] { df::max_decode_bytes = restore_budget; });

	df::max_decode_bytes = 64ll * 1024ll * 1024ll; // exactly 4096 x 4096

	load_diagnostic fits;
	assert_equal(false, reject_over_budget_source(&fits, {4096, 4096}, "test"), "a source at the budget is decoded");
	assert_equal(false, fits.over_budget, "a source that fits is not flagged");
	assert_equal(4096, fits.source_dimensions.cx, "the size is reported whether or not it fits");

	load_diagnostic refused;
	assert_equal(true, reject_over_budget_source(&refused, {4097, 4096}, "test"), "one column over is refused");
	assert_equal(true, refused.over_budget, "the refusal reaches the caller");
	assert_equal(4097, refused.source_dimensions.cx, "the refused size is reported so it can be shown");

	assert_equal(true, files::exceeds_decode_budget(sizei{8000, 8000}), "the predicate agrees with the loader");
	assert_equal(true, reject_over_budget_source(nullptr, {8000, 8000}, "test"),
	             "a caller that wants no diagnostic is still told to refuse");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Registration
///////////////////////////////////////////////////////////////////////////////////////////////////

void register_tests1(view_state& state, test_registry& tests)
{
	tests.add("Should edit single-line text"s, should_edit_single_line_text);
	tests.add("Should offer every matching external tool"s, should_offer_every_matching_tool);
	tests.add("Should settle a transport stream extension by header"s,
	          should_settle_transport_stream_extension_by_header);
	tests.add("Should retain audio buffer timing across cursor compaction"s,
	          should_compact_consumed_audio_only_when_needed);
	tests.add("Should ramp audio at buffer edges"s, should_ramp_audio_at_buffer_edges);
	tests.add("Should time visualizer independently of refresh rate"s,
	          should_time_visualizer_independently_of_refresh_rate);

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
	tests.add("Issue #219: Should match Korean NFC and NFD"s, should_match_korean_nfc_nfd);
	//
	// Issue #134 - Emoji tags/labels
	//
	tests.add("Issue #134: Should handle emoji tags"s, should_handle_emoji_tags);
	//
	// Issue #175 - Sidebar history chart span
	//
	tests.add("Issue #175: Should record history beyond ten years"s, should_record_history_beyond_ten_years);
	tests.add("Issue #175: Should calculate history span from start year"s,
	          should_calculate_history_span_from_start_year);
	tests.add("Should group sidebar map by place"s, should_group_sidebar_map_by_place);

	//
	// Issue #58 - Search with/without scope
	//
	tests.add("Issue #58: Should distinguish with/without scope"s, should_distinguish_with_without_scope);
	//
	// Issue #184 - Group by date created uses DateTimeOriginal
	//
	tests.add("Issue #184: Should prefer DateTimeOriginal for created"s, should_prefer_datetimeoriginal_for_created);
	tests.add("Should classify aspect ratio groups"s, should_classify_aspect_ratio_groups);
	tests.add("Should estimate decode cost"s, should_estimate_decode_cost);
	tests.add("Should refuse over budget sources"s, should_refuse_over_budget_sources);
	tests.add("Should clear detail row layout metrics"s, should_clear_detail_row_layout_metrics);
	tests.add("Should animate alpha between values"s, should_animate_alpha_between_values);
	tests.add("Should skip alpha animation when disabled"s, should_skip_alpha_animation_when_disabled);

	//
	// Media-type filter persistence
	//
	tests.add("Should persist media filter"s, should_persist_media_filter);
	tests.add("Issue #228: Should round trip tag actions"s, should_round_trip_tag_actions);
	tests.add("Issue #229: Should persist detail display per media type"s,
	          should_persist_detail_display_per_media_type);

	//
	// Issue #135 - Rating/labeling via NumPad
	//
	tests.add("Issue #135: Should map numpad digits to rating keys"s, should_map_numpad_digits_to_rating_keys);

	//
	// Issue #227 - Removed default sidebar tags reappear after restart
	//
	tests.add("Issue #227: Should only seed favorite tags on first run"s,
	          should_only_seed_favorite_tags_on_first_run);
	tests.add("Should start safe only after repeated failures"s, should_start_safe_only_after_repeated_failures);
	tests.add("Should restore history selection"s, should_restore_history_selection);
}
