// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Tests for the shared utility layer (util*, crypto*) -- strings, wildcards, versions, natural compare, interning, cancellation tokens, result scopes, hashes and perceptual hashes, json parsing, file presence and memory-mapped files.

#include "pch.h"
#include "files.h"
#include "test.h"
#include "app_command_line.h"
#include "crypto.h"
#include "crypto_aes256.h"
#include "util_base64.h"
#include "crypto_sha.h"
#include "util_json.h"
#include "util_kdtree.h"
#include "util_simd.h"
#include "util_top.h"
#include "test_fixtures.h"
#include "test_runner.h"
#include "app_util.h"

static void should_complete_result_scope()
{
	const auto results = std::make_shared<null_item_results_ui>();
	{
		result_scope scope(results);
	}

	assert_equal(1, results->complete_count, "normal scope exit completes results");
	assert_equal(0, results->abort_count, "normal scope exit does not abort results");
}

static void should_abort_result_scope_during_exception()
{
	const auto results = std::make_shared<null_item_results_ui>();
	try
	{
		result_scope scope(results);
		throw std::runtime_error("test");
	}
	catch (const std::runtime_error&)
	{
	}

	assert_equal(0, results->complete_count, "exception unwinding does not complete results");
	assert_equal(1, results->abort_count, "exception unwinding aborts results");
}

static void should_icmp_natural()
{
	// Test basic numeric comparison - the key bug fix
	// Files like 43_100 should come after 43_99, not between 43_10 and 43_11
	assert_equal(true, str::icmp_natural("43_09", "43_10") < 0, "43_09 < 43_10");
	assert_equal(true, str::icmp_natural("43_10", "43_11") < 0, "43_10 < 43_11");
	assert_equal(true, str::icmp_natural("43_10", "43_100") < 0, "43_10 < 43_100");
	assert_equal(true, str::icmp_natural("43_99", "43_100") < 0, "43_99 < 43_100");
	assert_equal(true, str::icmp_natural("43_100", "43_101") < 0, "43_100 < 43_101");

	// Verify the order reported in the bug is fixed
	assert_equal(true, str::icmp_natural("43_09", "43_100") < 0, "43_09 < 43_100");
	assert_equal(true, str::icmp_natural("43_11", "43_100") < 0, "43_11 < 43_100");

	// Test equality
	assert_equal(0, str::icmp_natural("file10", "file10"), "equal strings");
	assert_equal(0, str::icmp_natural("", ""), "empty strings");

	// Test case insensitivity
	assert_equal(0, str::icmp_natural("File10", "file10"), "case insensitive");
	assert_equal(0, str::icmp_natural("FILE10", "file10"), "case insensitive upper");

	// Test basic natural ordering
	assert_equal(true, str::icmp_natural("file1", "file2") < 0, "file1 < file2");
	assert_equal(true, str::icmp_natural("file2", "file10") < 0, "file2 < file10");
	assert_equal(true, str::icmp_natural("file9", "file10") < 0, "file9 < file10");
	assert_equal(true, str::icmp_natural("file10", "file11") < 0, "file10 < file11");
	assert_equal(true, str::icmp_natural("file19", "file20") < 0, "file19 < file20");
	// Issue #197: "43_100" sorted between "43_10" and "43_11" (lexicographic instead of numeric).
	assert_equal(true, str::icmp_natural("file99", "file100") < 0, "file99 < file100");
	assert_equal(true, str::icmp_natural("file100", "file1000") < 0, "file100 < file1000");

	// Test reverse ordering
	assert_equal(true, str::icmp_natural("file10", "file9") > 0, "file10 > file9");
	assert_equal(true, str::icmp_natural("file100", "file99") > 0, "file100 > file99");

	// Test with different prefixes
	assert_equal(true, str::icmp_natural("a10", "b1") < 0, "a10 < b1");
	assert_equal(true, str::icmp_natural("img001", "img002") < 0, "img001 < img002");
	assert_equal(true, str::icmp_natural("img009", "img010") < 0, "img009 < img010");

	// Test numbers at the start
	assert_equal(true, str::icmp_natural("1file", "2file") < 0, "1file < 2file");
	assert_equal(true, str::icmp_natural("9file", "10file") < 0, "9file < 10file");
	assert_equal(true, str::icmp_natural("10file", "100file") < 0, "10file < 100file");

	// Test multiple number groups
	assert_equal(true, str::icmp_natural("file1-1", "file1-2") < 0, "file1-1 < file1-2");
	assert_equal(true, str::icmp_natural("file1-9", "file1-10") < 0, "file1-9 < file1-10");
	assert_equal(true, str::icmp_natural("file1-10", "file2-1") < 0, "file1-10 < file2-1");

	// Test leading zeros
	assert_equal(true, str::icmp_natural("file007", "file7") > 0, "file007 > file7 (more leading zeros)");
	assert_equal(true, str::icmp_natural("file07", "file007") < 0,
	             "file07 < file007 (fewer leading zeros)");
	assert_equal(0, str::icmp_natural("file007", "file007"), "same with leading zeros");

	// Test purely numeric strings
	assert_equal(true, str::icmp_natural("1", "2") < 0, "1 < 2");
	assert_equal(true, str::icmp_natural("9", "10") < 0, "9 < 10");
	assert_equal(true, str::icmp_natural("99", "100") < 0, "99 < 100");
	assert_equal(true, str::icmp_natural("999", "1000") < 0, "999 < 1000");

	// Test strings with no numbers
	assert_equal(true, str::icmp_natural("abc", "abd") < 0, "abc < abd");
	assert_equal(true, str::icmp_natural("abc", "abcd") < 0, "abc < abcd");
	assert_equal(0, str::icmp_natural("abc", "ABC"), "abc == ABC (case insensitive)");

	// Test image sequence patterns (common use case)
	assert_equal(true, str::icmp_natural("DSC_0001.jpg", "DSC_0002.jpg") < 0, "DSC sequence");
	assert_equal(true, str::icmp_natural("DSC_0099.jpg", "DSC_0100.jpg") < 0, "DSC sequence 99-100");
	assert_equal(true, str::icmp_natural("IMG_9999.png", "IMG_10000.png") < 0, "IMG sequence overflow");
}

static void should_group_elements_by_folder()
{
	struct element
	{
		df::folder_path folder;
		int id = 0;
		bool skip = false;
	};

	const std::vector<element> elements{
		{df::folder_path("c:\\a"), 1},
		{df::folder_path("c:\\b"), 2},
		{df::folder_path("c:\\a"), 3},
		{df::folder_path("c:\\b"), 4, true},
		{df::folder_path("C:\\A"), 5},
		{df::folder_path("c:\\c"), 6},
	};

	df::folder_groups groups;
	groups.build(elements,
	             [](const element& e) { return e.folder; },
	             [](const element& e) { return !e.skip; });

	assert_equal(3, static_cast<int>(groups.groups().size()), "one group per distinct folder");

	// Groups are in first-seen order and each keeps its elements in input order.
	assert_equal("c:\\a", groups.groups()[0].folder.text().str(), "first group");
	assert_equal("c:\\b", groups.groups()[1].folder.text().str(), "second group");
	assert_equal("c:\\c", groups.groups()[2].folder.text().str(), "third group");

	const auto ids = [&](const size_t g)
	{
		std::string result;
		for (const auto i : groups.elements(groups.groups()[g])) result += std::to_string(elements[i].id);
		return result;
	};

	// Interning is case sensitive, so "C:\A" must still reach the group "c:\a" created.
	assert_equal("135", ids(0), "case variants share one group");
	assert_equal("2", ids(1), "excluded element is dropped");
	assert_equal("6", ids(2), "single element group");

	groups.build(elements, [](const element& e) { return e.folder; });
	assert_equal(3, static_cast<int>(groups.groups().size()), "rebuild replaces the previous grouping");
	assert_equal("24", ids(1), "no predicate includes every element");

	// Enough distinct folders to force the lookup tables to grow and rehash.
	std::vector<element> many;
	for (auto i = 0; i < 500; ++i) many.emplace_back(df::folder_path(std::format("c:\\f{}", i)), i);
	for (auto i = 0; i < 500; ++i) many.emplace_back(df::folder_path(std::format("c:\\f{}", i)), i);

	groups.build(many, [](const element& e) { return e.folder; });
	assert_equal(500, static_cast<int>(groups.groups().size()), "grown tables keep folders distinct");

	for (const auto& g : groups.groups())
	{
		assert_equal(2, static_cast<int>(groups.elements(g).size()), "each folder collected both elements");
	}

	groups.clear();
	assert_equal(true, groups.empty(), "cleared grouping has no groups");
}

static void should_cancel_superseded_tokens()
{
	std::atomic_int version = 0;
	const df::cancel_token first(version);
	const auto first_copy = first;

	assert_equal(false, first.is_cancelled(), "current token is active");
	assert_equal(false, first_copy.is_cancelled(), "copied current token is active");

	const df::cancel_token second(version);
	assert_equal(true, first.is_cancelled(), "new generation cancels previous token");
	assert_equal(true, first_copy.is_cancelled(), "new generation cancels copies of previous token");
	assert_equal(false, second.is_cancelled(), "new generation remains active");
}

static void should_calc_HMACSHA1()
{
	const auto signature = crypto::hmac_sha1("Jefe", "what do ya want for nothing?");
	assert_equal("7/zfauXrL6LSdBbV8YTfnCWafHk=", signature, "Signature");
}

static void should_calc_hashes()
{
	assert_equal("A9993E364706816ABA3E25717850C26C9CD0D89D", crypto::to_sha1("abc"), "SHA1");
	assert_equal("187797D630ECAA0FC1B920CD9F809C2BBFFCBF4C", crypto::to_sha1(long_text), "SHA1");
	assert_equal("BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD", crypto::to_sha256("abc"),
	             "SHA256");
	assert_equal("1660F10AEC042D762CF8B1C53E976F890C8E797BEF74807F505EDCE20308FC2F", crypto::to_sha256(long_text),
	             "SHA256");

	const auto crc_data = "hello world"s;
	const auto crc_result = crypto::crc32c(crc_data.data(), crc_data.size());
	assert_equal(0xc99465aa, crc_result, "crc32");

	const auto crc_c = ~calc_crc32c_c(crypto::CRCINIT, crc_data.data(), crc_data.size());
	assert_equal(0xc99465aa, crc_c, "crc32 c");

	if (platform::crc32_supported)
	{
		const auto crc_x86 = ~calc_crc32c_x86(crypto::CRCINIT, crc_data.data(), crc_data.size());
		assert_equal(0xc99465aa, crc_x86, "crc32 x86");
	}

	if (platform::arm_crc32_supported)
	{
		const auto crc_neon = ~calc_crc32c_arm(crypto::CRCINIT, crc_data.data(), crc_data.size());
		assert_equal(0xc99465aa, crc_neon, "crc32 neon");
	}

	alignas(16) std::array<uint8_t, 96> boundary_data;
	for (auto i = 0u; i < boundary_data.size(); ++i)
	{
		boundary_data[i] = static_cast<uint8_t>(i * 37u + 11u);
	}

	for (auto offset = 0u; offset < 16u; ++offset)
	{
		for (auto len = 0u; len <= 80u; ++len)
		{
			const auto* const data = boundary_data.data() + offset;
			const auto expected = calc_crc32c_c(crypto::CRCINIT, data, len);
			assert_equal(~expected, crypto::crc32c(data, len), "crc32 dispatched boundary");

			if (platform::crc32_supported)
			{
				assert_equal(expected, calc_crc32c_x86(crypto::CRCINIT, data, len), "crc32 x86 boundary");
				const auto split = len / 2;
				const auto first = calc_crc32c_x86(crypto::CRCINIT, data, split);
				assert_equal(expected, calc_crc32c_x86(first, data + split, len - split), "crc32 x86 continuation");
			}

			if (platform::arm_crc32_supported)
			{
				assert_equal(expected, calc_crc32c_arm(crypto::CRCINIT, data, len), "crc32 arm boundary");
				const auto split = len / 2;
				const auto first = calc_crc32c_arm(crypto::CRCINIT, data, split);
				assert_equal(expected, calc_crc32c_arm(first, data + split, len - split), "crc32 arm continuation");
			}
		}
	}
}

// A synthetic 32x32 field, so the hash is tested on its own terms without a decoder in the way.
static std::array<uint8_t, crypto::phash_pixels> make_phash_field(const int seed)
{
	std::array<uint8_t, crypto::phash_pixels> result{};

	for (auto y = 0u; y < crypto::phash_extent; ++y)
	{
		for (auto x = 0u; x < crypto::phash_extent; ++x)
		{
			const auto v = (x * 7 + y * 13 + seed * 29) % 251;
			result[y * crypto::phash_extent + x] = static_cast<uint8_t>((v * v) % 256);
		}
	}

	return result;
}

static void should_calc_perceptual_hashes()
{
	const auto field = make_phash_field(1);
	const auto hash = crypto::perceptual_hash(field.data(), field.size());

	assert_equal(true, crypto::phash_is_usable(hash), "a detailed field hashes");
	assert_equal(hash, crypto::perceptual_hash(field.data(), field.size()), "the same pixels hash the same");
	assert_equal(0, crypto::phash_distance(hash, hash), "distance to itself");

	// Bit 0 comes from the DC coefficient, which is excluded, so it is free to mark a declined hash.
	assert_equal(0ull, hash & 1ull, "a real hash never sets the reserved bit");
	assert_equal(false, crypto::phash_is_usable(crypto::phash_declined), "the declined marker is not a hash");
	assert_equal(false, crypto::phash_is_usable(0), "not computed is not a hash");

	// Brightness and contrast move every pixel but not the picture, which is what a re-encode does.
	auto brightened = field;
	for (auto& v : brightened) v = static_cast<uint8_t>(std::min(255, v + 20));
	assert_equal(true, crypto::phash_distance(hash, crypto::perceptual_hash(brightened.data(), brightened.size())) <= 6,
	             "brightness does not change the picture");

	// A different picture has to land far away, or the threshold means nothing.
	const auto other = make_phash_field(9);
	assert_equal(true, crypto::phash_distance(hash, crypto::perceptual_hash(other.data(), other.size())) > 6,
	             "a different picture is far away");

	// Flat fields are where a 64-bit hash quietly starts matching everything.
	std::array<uint8_t, crypto::phash_pixels> blank{};
	blank.fill(128);
	assert_equal(false, crypto::phash_is_usable(crypto::perceptual_hash(blank.data(), blank.size())),
	             "a blank image has no opinion");

	std::array<uint8_t, crypto::phash_pixels> almost_blank{};
	almost_blank.fill(128);
	almost_blank[0] = 129;
	assert_equal(false, crypto::phash_is_usable(crypto::perceptual_hash(almost_blank.data(), almost_blank.size())),
	             "one different pixel is not detail");

	assert_equal(0ull, crypto::perceptual_hash(nullptr, crypto::phash_pixels), "no pixels");
	assert_equal(0ull, crypto::perceptual_hash(field.data(), crypto::phash_pixels - 1), "short buffer");
}

static uint64_t phash_of_file(const std::string_view name)
{
	files ff;
	file_read_stream stream;
	if (!stream.open(test_files_folder.combine_file(name))) return 0;

	df::blob owner;
	return ff.calc_perceptual_hash(stream.view_all(owner));
}

static crypto::phash_rotations phash_rotations_of_file(const std::string_view name)
{
	files ff;
	file_read_stream stream;
	if (!stream.open(test_files_folder.combine_file(name))) return {};

	df::blob owner;
	return ff.calc_perceptual_hash_rotations(stream.view_all(owner));
}

// The point of the hash is the case a checksum cannot see: the same picture in a different file.
static void should_recognise_the_same_picture()
{
	const auto original = phash_of_file("Test.jpg");
	const auto resized = phash_of_file("Small.jpg");

	assert_equal(true, crypto::phash_is_usable(original), "Test.jpg hashes");
	assert_equal(true, crypto::phash_is_usable(resized), "Small.jpg hashes");

	// Measured separation on these fixtures: 0 for the resize, 30 for a rotation, 32 for an
	// unrelated photo. The threshold sits in that gap rather than near either side of it.
	assert_equal(0, crypto::phash_distance(original, resized), "a resized copy is the same picture");

	// A rotation is a different bitmap, so the single-orientation hash must still land far away: the
	// rotations are what recognise it, and nothing else should quietly start matching.
	assert_equal(true, crypto::phash_distance(original, phash_of_file("Test90.jpg")) > 20,
	             "a rotated copy is not the same bitmap");

	const auto unrelated = phash_of_file("IMG_0096.JPG");
	assert_equal(true, crypto::phash_is_usable(unrelated), "an unrelated photo hashes");
	assert_equal(true, crypto::phash_distance(original, unrelated) > 20, "an unrelated photo is far away");
}

// A quarter turn is a common grading step, so the same picture rotated has to be recognised as a
// copy - while an unrelated photo stays clear in every orientation, not just the one it was saved in.
static void should_recognise_a_rotated_picture()
{
	const auto original = phash_of_file("Test.jpg");

	assert_equal(true, crypto::phash_is_usable(original), "Test.jpg hashes");

	// Every quarter turn of the same photograph, and a losslessly rotated pair from a second source.
	for (const auto name : {"Test90.jpg"sv, "Test180.jpg"sv, "Test270.jpg"sv})
	{
		const auto rotated = phash_rotations_of_file(name);
		assert_equal(true, crypto::phash_is_usable(rotated[0]), "a turned copy hashes");
		assert_equal(true, crypto::phash_distance(original, rotated) <= 6, "a turned copy is the same picture");
	}

	const auto lossless = phash_of_file("Lossless0.jpg");
	assert_equal(true, crypto::phash_is_usable(lossless), "Lossless0.jpg hashes");
	assert_equal(true, crypto::phash_distance(lossless, phash_rotations_of_file("Lossless90.jpg")) <= 6,
	             "a losslessly rotated copy matches");

	// Whichever side carries the turns, the pair has to meet: presence compares the outside file's
	// stored hash against an indexed picture's turns, and duplicate search does the reverse.
	assert_equal(true, crypto::phash_distance(phash_of_file("Test90.jpg"), phash_rotations_of_file("Test.jpg")) <= 6,
	             "the turned file recognises the upright original");
	assert_equal(true, crypto::phash_is_usable(phash_rotations_of_file("Test.jpg")[0]),
	             "the upright original keeps a usable set of turns");

	// The rotations must not become a way for anything to match anything.
	assert_equal(true, crypto::phash_distance(original, phash_rotations_of_file("IMG_0096.JPG")) > 6,
	             "an unrelated photo stays clear in every orientation");
	assert_equal(true, crypto::phash_distance(original, phash_rotations_of_file("Lossless0.jpg")) > 6,
	             "a second unrelated photo stays clear in every orientation");

	// A resize still matches without needing a turn, so the plain comparison is not weakened.
	assert_equal(0, crypto::phash_distance(original, phash_rotations_of_file("Small.jpg")),
	             "a resized copy still matches at zero");
}

static void should_convert_utf8()
{
	// icon font
	wchar_t stars_utf16[6] = {};

	for (auto i = 0; i < 5; i++)
	{
		stars_utf16[i] = static_cast<uint16_t>(i & 0x01 ? icon_index::star_solid : icon_index::star);
	}
	stars_utf16[5] = 0;

	const auto stars = platform::utf16_to_utf8(stars_utf16);

	std::string_view strings[] = {
		"In vollen Zügen genießen",
		"Nældens takvinge",
		"💉💎👦🏻👓⚡",
		"Žižkov",
		"Доброго ранку!",
		"Japanese こんにちは世界",
		"Arabic مرحبا العالم",
		stars
	};

	for (const auto src : strings)
	{
		assert_equal(src, platform::utf16_to_utf8(platform::utf8_to_utf16(src)), "platform conversions");
		assert_equal(platform::utf8_to_utf16(src), str::utf8_to_utf16(src), "to utf16");
		assert_equal(src, str::utf16_to_utf8(platform::utf8_to_utf16(src)), "to utf8");
		assert_equal(src, str::utf16_to_utf8(str::utf8_to_utf16(src)), "internal conversions");
	}

	constexpr wchar_t icon_text[2] = {static_cast<wchar_t>(icon_index::fit), 0};
	const auto icon_text_converted = str::utf8_to_utf16(str::utf16_to_utf8(icon_text));
	assert_equal(icon_text, icon_text_converted, "icon to utf8");

	// Verify icon_to_utf8 matches the old wchar_t + utf16_to_utf8 approach
	const auto icon_new = icon_to_utf8(icon_index::fit);
	constexpr wchar_t icon_old_text[2] = {static_cast<wchar_t>(icon_index::fit), 0};
	const auto icon_old = str::utf16_to_utf8(icon_old_text);
	assert_equal(icon_old, icon_new, "icon_to_utf8 matches old approach");
	assert_equal(false, icon_is_mirrored(icon_index::rotate_clockwise), "clockwise icon is not mirrored");
	assert_equal(true, icon_is_mirrored(icon_index::rotate_anticlockwise), "anticlockwise icon is mirrored");

	// Verify char32_to_utf8 round-trips for icon code points
	std::string char32_result;
	str::char32_to_utf8(std::back_inserter(char32_result), static_cast<uint32_t>(icon_index::fit) & 0xFFFF);
	assert_equal(icon_old, char32_result, "char32_to_utf8 for icon");

	// Verify every icon in the icon_index enum round-trips correctly through UTF-8
	constexpr icon_index all_icons[] = {
		icon_index::add, icon_index::remove, icon_index::audio, icon_index::camera,
		icon_index::cancel, icon_index::check, icon_index::del, icon_index::edit,
		icon_index::folder, icon_index::search, icon_index::star, icon_index::star_solid,
		icon_index::play, icon_index::pause, icon_index::stop, icon_index::copyright,
		icon_index::photo, icon_index::video, icon_index::settings, icon_index::save,
		icon_index::rotate_clockwise, icon_index::rotate_anticlockwise,
		icon_index::fit, icon_index::zoom_in, icon_index::zoom_out,
	};

	for (const auto icon : all_icons)
	{
		const auto icon_val = static_cast<uint32_t>(icon) & 0xFFFF;
		const wchar_t expected_utf16[2] = {static_cast<wchar_t>(icon_val), 0};
		const auto expected_utf8 = platform::utf16_to_utf8(expected_utf16);

		// icon_to_utf8 should produce correct UTF-8
		const auto actual_utf8 = icon_to_utf8(icon);
		assert_equal(expected_utf8, actual_utf8,
		             std::format("icon_to_utf8 for 0x{:X}", static_cast<uint32_t>(icon)));

		// UTF-8 should be exactly 3 bytes for BMP icons >= 0x800
		if (icon_val >= 0x800)
		{
			assert_equal(3, static_cast<int>(actual_utf8.size()),
			             std::format("icon UTF-8 byte count for 0x{:X}", icon_val));
		}

		// Round-trip: UTF-8 -> UTF-16 should recover the original code point
		const auto round_tripped_utf16 = str::utf8_to_utf16(actual_utf8);
		assert_equal(1, static_cast<int>(round_tripped_utf16.size()),
		             std::format("icon round-trip UTF-16 length for 0x{:X}", icon_val));
		assert_equal(static_cast<int>(icon_val), round_tripped_utf16[0],
		             std::format("icon round-trip code point for 0x{:X}", icon_val));

		// Also verify str:: matches platform:: conversion
		const auto platform_utf16 = platform::utf8_to_utf16(actual_utf8);
		assert_equal(platform_utf16, round_tripped_utf16,
		             std::format("icon str vs platform utf8_to_utf16 for 0x{:X}", icon_val));
	}
}

static void should_split()
{
	constexpr auto to_be_split = "H:\\2-Archief VIDEO privé\\Eigen video's\nF:\\1-Archief FOTOGRAFIE privé";
	const auto parts = str::split(to_be_split, false, [](const wchar_t c) { return c == '\n' || c == '\r'; });

	assert_equal("H:\\2-Archief VIDEO privé\\Eigen video's", parts[0], "Split 1");
	assert_equal("F:\\1-Archief FOTOGRAFIE privé", parts[1], "Split 2");

	constexpr auto to_be_split2 = "aaa 'bbb ccc' ddd \"ee ff \"";
	const auto parts2 = str::split(to_be_split2, true);

	constexpr auto to_be_split3 = "Доброго ранку!";
	const auto parts3 = str::split(to_be_split3, true);

	assert_equal("aaa", parts2[0], "Split 1");
	assert_equal("bbb ccc", parts2[1], "Split 2");
	assert_equal("ddd", parts2[2], "Split 3");
	assert_equal("ee ff ", parts2[3], "Split 4");
	assert_equal("ранку!", parts3[1], "Split 5");

	// Random data checking for crashes
	std::string_view strings[] = {
		"In vollen Zügen genießen",
		"Nældens takvinge",
		"Žižkov",
		"Доброго ранку!",
		"Japanese こんにちは世界",
		"Arabic مرحبا العالم",
		"Доброго ранку!",
		"\"'",
		"\" \" \"",
		"''''",
		"aaa'bb  bbb'aa",
		"aaa\0\0\'",
		"\r\t\naaaa\" aaa bbb",
		"'\t \n abc",
		"'",
	};

	for (const auto& src : strings)
	{
		str::split_count(src, true);
	}
}

static void should_split_genre()
{
	// Genre values use ';' as the multi-value separator. Multi-word genres and
	// genres containing '&' or '/' must survive splitting intact.
	const auto parts = str::split("Rock; Pop ; Hip Hop", false, str::is_genre_separator);
	assert_equal(size_t{3}, parts.size(), "genre part count");
	assert_equal("Rock", str::trim(parts[0]), "genre 1");
	assert_equal("Pop", str::trim(parts[1]), "genre 2");
	assert_equal("Hip Hop", str::trim(parts[2]), "genre 3");

	const auto parts2 = str::split("Action & Adventure; R&B/Soul", false, str::is_genre_separator);
	assert_equal(size_t{2}, parts2.size(), "genre part count 2");
	assert_equal("Action & Adventure", str::trim(parts2[0]), "genre with ampersand");
	assert_equal("R&B/Soul", str::trim(parts2[1]), "genre with slash");

	const auto parts3 = str::split("Jazz", false, str::is_genre_separator);
	assert_equal(size_t{1}, parts3.size(), "single genre part count");
	assert_equal("Jazz", str::trim(parts3[0]), "single genre");
}

static void should_extract_url()
{
	constexpr auto input1 = "Visit my website at https://www.example.com for more info.";
	constexpr auto input2 = "Check out this article: http://anotherexample.org/article";
	constexpr auto input3 = "No URLs here.";
	constexpr auto input4 =
		"Quite nice  <a href=\"http://bighugelabs.com/flickr/onblack.php?id=1397504988\"> On Black</a>";

	assert_equal("https://www.example.com", df::url_extract(input1), "extract url");
	assert_equal("http://anotherexample.org/article", df::url_extract(input2), "extract url");
	assert_equal("", df::url_extract(input3), "extract url");
	assert_equal("", df::url_extract(input3), "extract url");
	assert_equal("http://bighugelabs.com/flickr/onblack.php?id=1397504988", df::url_extract(input4),
	             "extract url");

	// A description panel offering a choice of links needs every distinct one, in reading order.
	const auto all = df::url_extract_all(
		"See https://example.com/a and https://example.com/b then https://example.com/a again.");
	assert_equal(size_t{2}, all.size(), "repeated url listed once");
	assert_equal("https://example.com/a", all[0], "first url in source order");
	assert_equal("https://example.com/b", all[1], "second url in source order");
	assert_equal(size_t{0}, df::url_extract_all(input3).size(), "no urls found");
}

static void should_match_wildcard()
{
	assert_equal(true, str::wildcard_icmp("", ""));
	assert_equal(true, str::wildcard_icmp("", "*"));
	assert_equal(true, str::wildcard_icmp(" ", "*"));
	assert_equal(true, str::wildcard_icmp(" ", " *"));
	assert_equal(false, str::wildcard_icmp(" ", "  *"));

	assert_equal(true, str::wildcard_icmp("hello world", "hello world"));
	assert_equal(true, str::wildcard_icmp("hello ?! world", "hello * world"));
	assert_equal(true, str::wildcard_icmp("hello-xx-world", "hello*world"));
	assert_equal(false, str::wildcard_icmp("hello-xx-world", "hello *world"));
	assert_equal(true, str::wildcard_icmp("hello-xx-world", "*world"));
	assert_equal(true, str::wildcard_icmp("hello-xx-world", "hello*"));

	assert_equal(true, str::wildcard_icmp("HELLO-XX-WORLD", "hello*"));
	assert_equal(true, str::wildcard_icmp("HELLO-XX-WORLD", "hello*world"));


	assert_equal(0, str::icmp("ДОБРОГО РАНКУ", "Доброго ранку"));
	assert_equal(0, str::icmp("ARABIC مرحبا العالم", "Arabic مرحبا العالم"));
	assert_equal(0, str::icmp("JAPANESE こんにちは世界", "Japanese こんにちは世界"));
	assert_equal(0, str::icmp("💉💎👦🏻👓⚡", "💉💎👦🏻👓⚡"));

	assert_equal(true, str::wildcard_icmp("Доброго ранку", "Доброго*"));
	assert_equal(true, str::wildcard_icmp("ДОБРОГО РАНКУ", "Доброго*"));
	assert_equal(true, str::wildcard_icmp("ДОБРОГО РАНКУ", "*ранку"));
	assert_equal(true, str::wildcard_icmp("💉💎👦🏻👓⚡", "*💎*"));
	assert_equal(true, str::wildcard_icmp("💉💎👦🏻👓⚡", "💉*"));
}

static void should_detect_wildcard()
{
	assert_equal(false, str::is_wildcard(""));
	assert_equal(false, str::is_wildcard("abcdef"));
	assert_equal(true, str::is_wildcard("abc*"));
	assert_equal(false, str::is_wildcard("abc\\*"));
	assert_equal(false, str::is_wildcard("abc\\*ef"));
}

static void should_encrypt_password()
{
	const std::vector<uint8_t> test_key = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
		0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
	};

	const std::vector<uint8_t> test_dec = {
		0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
		0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87, 0x98, 0xa9, 0xba, 0xcb, 0xdc, 0xed, 0xfe, 0x0f,
		0x20, 0x31, 0x42, 0x53, 0x64, 0x75, 0x86, 0x97, 0xa8, 0xb9, 0xca, 0xdb, 0xec, 0xfd, 0x0e, 0x1f,
		0x30, 0x41, 0x52, 0x63, 0x74, 0x85, 0x96, 0xa7, 0xb8, 0xc9, 0xda, 0xeb, 0xfc, 0x0d, 0x1e, 0x2f,
		0x40, 0x51, 0x62, 0x73, 0x84, 0x95, 0xa6, 0xb7, 0xc8, 0xd9, 0xea, 0xfb, 0x0c, 0x1d, 0x2e, 0x3f,
		0x50, 0x61, 0x72, 0x83, 0x94, 0xa5, 0xb6, 0xc7, 0xd8, 0xe9, 0xfa, 0x0b, 0x1c, 0x2d, 0x3e, 0x4f,
		0x60, 0x71, 0x82, 0x93
	};

	// CBC mode uses random IV, so output is non-deterministic; verify round-trip instead
	const auto encrypted = crypto::encrypt(test_dec, test_key);
	std::vector<uint8_t> decrypted;
	crypto::aes256::decrypt(test_key, encrypted, decrypted);
	assert_equal(base64_encode(test_dec), base64_encode(decrypted), "encrypt using aes");

	const std::vector<std::string_view> test_values =
	{
		{},
		"This is a test.",
		long_text
	};

	static constexpr auto password = "diffractor-hello";

	for (const auto& val : test_values)
	{
		auto result = crypto::decrypt(crypto::encrypt(val, password), password);
		assert_equal(val, std::string(result.begin(), result.end()), "Encode - Decode");
	}

	// Check for crash based on bad data
	const std::vector<uint8_t> empty;
	crypto::decrypt(empty, password);

	std::vector<uint8_t> invalid;
	for (auto i = 0; i < 8; i++) invalid.emplace_back(i);
	crypto::decrypt(invalid, password);
}

static void should_parse_command_line()
{
	command_line_t cl1;
	cl1.parse("-no-gpu");

	assert_equal(true, cl1.no_gpu, "no_gpu");
	assert_equal(false, cl1.no_indexing, "no_indexing");

	command_line_t cl2;
	cl2.parse(test_files_folder.text());

	assert_equal(false, cl2.folder_path.is_empty(), "folder_path");
	assert_equal(std::string_view{}, cl2.selection.name(), "selection name");
	assert_equal(test_files_folder.text(), cl2.folder_path.folder().text(), "folder path");
	assert_equal(false, cl2.no_gpu, "no_gpu");
	assert_equal(false, cl2.no_indexing, "no_indexing");


	const auto path3 = test_files_folder.combine_file("test.jpg");
	command_line_t cl3;
	cl3.parse(std::format("{} -no-indexing", path3));

	assert_equal(path3.folder().text(), cl3.folder_path.folder().text(), "folder_path");
	assert_equal(path3.name(), cl3.selection.name(), "selection name");
	assert_equal(path3.folder().text(), cl3.selection.folder().text(), "selection folder");
	assert_equal(false, cl3.no_gpu, "no_gpu");
	assert_equal(true, cl3.no_indexing, "no_indexing");

	command_line_t cl4;
	cl4.parse("--no-gpu \"C:\\Program Files\"");
	assert_equal(true, cl4.no_gpu, "no_gpu");
	assert_equal(false, cl4.folder_path.is_empty(), "folder_path program Files");

	command_line_t cl5;
	cl5.parse("----- --no-gpu");
	assert_equal(true, cl5.no_gpu, "no_gpu");

	command_line_t cl6;
	cl6.parse("-no-gpu -no-indexing");
	assert_equal(true, cl6.no_gpu, "multiple options no_gpu");
	assert_equal(true, cl6.no_indexing, "multiple options no_indexing");

#ifdef _DEBUG
	command_line_t cl7;
	cl7.parse("-screenshot:edit \"-screenshot-output:C:\\temp\\edit.png\"");
	assert_equal("edit"sv, cl7.screenshot_scene, "screenshot scene");
	assert_equal("C:\\temp\\edit.png"sv, cl7.screenshot_output, "screenshot output");

	command_line_t cl8;
	cl8.parse("-test-reset-graphics");
	assert_equal("reset-graphics"sv, cl8.test_action, "test action");
#endif

	command_line_t cl9;
	cl9.parse("-run-tests");
	assert_equal(true, cl9.console_test, "run-tests alias");
}

static void should_trim_strings()
{
	assert_equal("xxx", str::trim_and_cache("xxx\n"), "remove cr lf");
	assert_equal("xxx", str::trim_and_cache("\rxxx\r"), "remove lf");
	assert_equal("xxx", str::trim_and_cache("   xxx\t\t "), "remove space");
}

static void should_format_text()
{
	assert_equal("ac-dc", std::format("{2}{0}-{1}{0}", "c", "d", "a"), "order");
	assert_equal("0.00123", std::format("{}", 0.00123), "double");
	assert_equal("0.001", std::format("{:.3f}", 0.00123), "double");
	assert_equal("5.5", std::format("{}", 5.5000), "double");
	assert_equal("123", std::format("{}", 123), "int");
	assert_equal("0123", std::format("{:04}", 123), "int");
	assert_equal(" 123", std::format("{:4}", 123), "int");
	assert_equal("hex=7B", std::format("hex={:x}", 0x7B), "hex");
	assert_equal("-test-", std::format("-{}-", "test"), "char*");
	assert_equal("-test-", std::format("-{}-", std::string("test")), "string");
	assert_equal("-test-", std::format("-{}-", std::string_view("test")), "string_view");
	assert_equal("33 {} {test}", std::format("{} {{}} {{test}}", 33), "string_view");
	assert_equal("22 x 33", std::format("{} x {}", 22, 33), "string_view");
}

static std::string find_and_format_result(const std::string_view text, const std::string_view sub_string)
{
	const auto r = str::ifind2(text, sub_string, 0);
	auto result = std::string(text);

	if (r.found)
	{
		for (auto i = static_cast<int>(r.parts.size()) - 1; i >= 0; --i)
		{
			const auto part = r.parts[i];
			result.insert(part.offset + part.length, 1, '*');
			result.insert(part.offset, 1, '*');
		}
	}

	return result;
}

static void should_find_text()
{
	assert_equal("*white* on blond", find_and_format_result("white on blond", "white"));
	assert_equal("*whi*te on *bl*ond", find_and_format_result("white on blond", "whi bl"));
	assert_equal("*white* on *blond*", find_and_format_result("white on blond", "white blond"));
	assert_equal("*white* bl on *blond*", find_and_format_result("white bl on blond", "white blond"));

	// Offsets are byte positions of a character start - a match following multi-byte characters
	// must not land on a continuation byte, or the renderer drops the highlight.
	const auto cyrillic = str::ifind2("\xd0\xbf\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82", "\xd0\xb2\xd0\xb5", 0);
	assert_equal(true, cyrillic.found, "cyrillic match found");
	assert_equal(6, static_cast<int>(cyrillic.parts[0].offset), "cyrillic match byte offset");
}

static void should_compare_versions()
{
	const df::version current_version(s_app_version);
	assert_equal(s_app_version, current_version.to_string(), "Can parse and to_string current version");

	const df::version test_version1("123.45");
	const df::version test_version1b("123.45");
	const df::version test_version2("456.1");

	assert_equal("123.45", test_version1.to_string(), "Can parse and to_string test version 1");
	assert_equal("456.1", test_version2.to_string(), "Can parse and to_string test version 2");

	assert_equal(true, test_version1 < test_version2, "Less op version");
	assert_equal(false, test_version2 < test_version1, "Less op version");
	assert_equal(false, test_version1 == test_version2, "== op version");
	assert_equal(true, test_version1 == test_version1b, "== op version");

	assert_equal("457.1", (test_version2 + 1).to_string(), "+ op version");
}

// Verifies the append-only string interning table: one shared immutable copy per unique
// string, identity == handle equality, stability across table growth, and correct
// deduplication under concurrent interning from multiple threads.
static void should_intern_strings()
{
	const auto a = str::cache("interned-example");
	const auto b = str::cache(std::string("interned-example"));
	assert_equal(true, a == b, "same content -> same handle");
	assert_equal(true, a.id == b.id, "identity is handle equality");
	assert_equal("interned-example"s, a.str(), "round-trips content");
	assert_equal(false, str::cache("interned-example") == str::cache("interned-different"),
	             "different content -> different handle");
	assert_equal(true, str::cache(std::string_view{}).is_empty(), "empty interns to empty");

	const std::string largest(platform::memory_pool::block_size -
	                          offsetof(str::chached_string_storage_t, sz) - 1, 'x');
	assert_equal(largest.size(), str::cache(largest).size(), "largest pool record is interned");
	const std::string too_large(largest.size() + 1, 'x');
	assert_equal(true, str::cache(too_large).is_empty(), "oversized pool record is rejected");

	// Many distinct strings force table growth / rehash; handles stay valid, unique and stable.
	constexpr int n = 5000;
	std::vector<str::cached> handles;
	handles.reserve(n);
	for (auto i = 0; i < n; ++i) handles.emplace_back(str::cache(std::format("intern-word-{}", i)));

	df::hash_set<uint32_t> seen;
	for (auto i = 0; i < n; ++i)
	{
		const auto w = std::format("intern-word-{}", i);
		assert_equal(w, handles[i].str(), "content preserved after growth");
		assert_equal(true, str::cache(w) == handles[i], "re-intern returns the same handle");
		seen.insert(handles[i].id);
	}
	assert_equal(n, static_cast<int>(seen.size()), "each distinct string interned exactly once");

	// Concurrent interning of an overlapping set must still yield one handle per string.
	constexpr int thread_count = 8;
	constexpr int word_count = 500;
	std::vector<std::vector<str::cached>> per_thread(thread_count);
	std::vector<std::thread> threads;

	for (auto t = 0; t < thread_count; ++t)
	{
		threads.emplace_back([t, &per_thread]
		{
			auto& out = per_thread[t];
			out.reserve(word_count);
			for (auto i = 0; i < word_count; ++i) out.emplace_back(str::cache(std::format("shared-word-{}", i)));
		});
	}

	for (auto& th : threads) th.join();

	for (auto i = 0; i < word_count; ++i)
	{
		const auto handle = per_thread[0][i];
		for (auto t = 1; t < thread_count; ++t)
		{
			assert_equal(true, per_thread[t][i] == handle, "all threads share one interned handle");
		}
	}
}

// A query that fails is not proof the file is gone: a caller that deletes or overwrites on
// "not there" must be able to tell a removed file from one it simply could not read.
static void should_report_file_presence()
{
	const auto scratch = _temps.next_folder("file-presence");
	const auto present = scratch.combine_file("present.txt");
	{
		std::ofstream fs(platform::to_file_system_path(present));
		fs << "content";
	}

	const auto found = platform::file_attributes(present);
	assert_equal(true, found.exists(), "existing file is found");
	assert_equal(false, found.confirmed_missing(), "existing file is not missing");

	const auto missing = platform::file_attributes(scratch.combine_file("missing.txt"));
	assert_equal(false, missing.exists(), "removed file does not exist");
	assert_equal(true, missing.confirmed_missing(), "removed file is confirmed missing");

	// A path under a folder that is not there is absent for the same reason, not a failure.
	const auto missing_folder = platform::file_attributes(scratch.combine("gone").combine_file("missing.txt"));
	assert_equal(true, missing_folder.confirmed_missing(), "file under a missing folder is confirmed missing");

	// An empty file must not read as absent just because it has no bytes.
	const auto empty_path = scratch.combine_file("empty.txt");
	{
		std::ofstream fs(platform::to_file_system_path(empty_path));
	}
	const auto empty = platform::file_attributes(empty_path);
	assert_equal(true, empty.exists(), "empty file exists");
	assert_equal(0, static_cast<int>(empty.size), "empty file has no bytes");

	assert_equal(true, platform::file_attributes(scratch).exists(), "existing folder is found");
	assert_equal(true, platform::file_attributes(scratch.combine("gone")).confirmed_missing(),
	             "removed folder is confirmed missing");

	// Enumeration only ever reports what it found, so those records are never left unknown.
	const auto contents = platform::iterate_file_items(scratch, false);
	assert_equal(2, static_cast<int>(contents.files.size()), "both files enumerated");
	assert_equal(true, std::ranges::all_of(contents.files, [](const platform::file_info& f)
	             {
		             return f.attributes.exists();
	             }),
	             "enumerated files are found");

	assert_equal(false, platform::file_attributes_t{}.exists(), "unqueried attributes do not exist");
	assert_equal(false, platform::file_attributes_t{}.confirmed_missing(),
	             "unqueried attributes are not confirmed missing");
}

static void should_map_files()
{
	const auto scratch = _temps.next_folder("map-file");
	const auto path = scratch.combine_file("mapped.txt");

	// Larger than one allocation granularity so a window can be placed past the first boundary.
	std::string content;
	content.reserve(200'000);
	for (auto i = 0; content.size() < 200'000; ++i) content += std::format("line {}\n", i);
	write_test_file(path, content);

	const auto whole = platform::map_file(path);
	assert_equal(true, whole != nullptr, "whole file maps");
	assert_equal(content.size(), static_cast<size_t>(whole->file_size()), "mapped size matches the file");
	assert_equal(content.size(), whole->data().size, "whole view covers the file");
	assert_equal(0, memcmp(whole->data().data, content.data(), content.size()), "mapped bytes match the file");

	// Trimmed pages must still read back correctly - the mapping stays valid, it just faults in.
	whole->release_working_set();
	assert_equal(0, memcmp(whole->data().data, content.data(), content.size()), "bytes survive a working-set release");

	const auto windowed = platform::map_file(path, platform::map_mode::windowed);
	assert_equal(true, windowed != nullptr, "windowed file maps");
	assert_equal(true, windowed->data().empty(), "windowed mapping has no view until a window is set");

	// An offset that is not a multiple of the allocation granularity must still return the exact
	// bytes asked for, because the granularity alignment is the mapping's business, not a caller's.
	constexpr uint64_t odd_offset = 70'001;
	constexpr uint64_t window_len = 4'096;
	const auto window = windowed->set_window(odd_offset, window_len);
	assert_equal(static_cast<size_t>(window_len), window.size, "window is the requested length");
	assert_equal(0, memcmp(window.data, content.data() + odd_offset, window_len), "window starts at the offset");

	// A window running past the end is clamped rather than refused.
	const auto tail = windowed->set_window(content.size() - 10, 4'096);
	assert_equal(10_z, tail.size, "window past the end is clamped to the file");
	assert_equal(0, memcmp(tail.data, content.data() + content.size() - 10, 10), "clamped window holds the tail");

	assert_equal(true, windowed->set_window(content.size(), 16).empty(), "window at the end is empty");

	assert_equal(true, platform::map_file(scratch.combine_file("missing.txt")) == nullptr,
	             "a missing file does not map");

	// An empty file cannot be mapped at all, which is an answer rather than a fault.
	const auto empty_path = scratch.combine_file("empty.txt");
	write_test_file(empty_path, {});
	assert_equal(true, platform::map_file(empty_path) == nullptr, "an empty file does not map");
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

///////////////////////////////////////////////////////////////////////////////////////////////////
// Issue #219 - Korean tags are not working
// Hangul has no letter case, so case-folding must leave it unchanged.
///////////////////////////////////////////////////////////////////////////////////////////////////

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

static void should_parse_facebook_json()
{
	const auto path_status = test_files_folder.combine_file("place.json");
	const auto json = df::util::json::json_from_file(path_status);

	auto& result = json["result"];
	auto& address_components = result["address_components"];
	assert_equal(5u, address_components.Size(), "data");
	assert_equal("WC1X", address_components[0]["long_name"].GetString(), "long_name");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// kd-tree
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_query_kdtree_bounds()
{
	// A 10x10 integer grid of points; offset encodes the original index.
	kd_points pts;
	for (int gx = 0; gx < 10; ++gx)
	{
		for (int gy = 0; gy < 10; ++gy)
		{
			pts.emplace_back(static_cast<float>(gx), static_cast<float>(gy),
			                 static_cast<uint32_t>(gx * 10 + gy), 0, 0, 0.0f);
		}
	}

	kd_tree tree;
	tree.build(pts); // reorders pts, but offset travels with each point

	// x in {3,4,5} and y in {3,4,5} => 9 points.
	std::vector<kd_coordinates_t> found;
	tree.find_in_bounds(pts, 2.5f, 2.5f, 5.5f, 5.5f, found);
	assert_equal(9, static_cast<int>(found.size()), "window count", "kd bounds");

	for (const auto& c : found)
	{
		assert_equal(true, c.x >= 2.5f && c.x <= 5.5f && c.y >= 2.5f && c.y <= 5.5f, "point in range", "kd bounds");
	}

	// A window far from every point returns nothing.
	std::vector<kd_coordinates_t> none;
	tree.find_in_bounds(pts, 100.0f, 100.0f, 200.0f, 200.0f, none);
	assert_equal(0, static_cast<int>(none.size()), "empty window", "kd bounds");

	// A window covering everything returns all points exactly once.
	std::vector<kd_coordinates_t> all;
	tree.find_in_bounds(pts, -1.0f, -1.0f, 100.0f, 100.0f, all);
	assert_equal(100, static_cast<int>(all.size()), "full window", "kd bounds");
}

static void should_find_the_closest_kdtree_point()
{
	// Deliberately elongated (x spans 4, y spans 400): a node's bounding box prunes such a spread
	// far more tightly than a bounding circle, so a pruning error surfaces here as a wrong answer.
	kd_points pts;
	std::vector<std::pair<float, float>> expected;
	uint32_t seed = 12345;
	const auto next_rand = [&seed]
	{
		seed = seed * 1664525u + 1013904223u;
		return static_cast<float>(seed % 100000u) * 0.00001f;
	};

	for (uint32_t i = 0; i < 400; ++i)
	{
		const auto x = next_rand() * 4.0f;
		const auto y = next_rand() * 400.0f;
		pts.emplace_back(x, y, i * 3 + 1, 7, i, 0.0f);
		expected.emplace_back(x, y);
	}

	kd_tree tree;
	tree.build(pts);

	// Query points inside, at the corners of, and far outside the data extent.
	const std::vector<std::pair<float, float>> queries{
		{0.0f, 0.0f}, {2.0f, 200.0f}, {3.9f, 399.0f}, {0.1f, 12.5f}, {-40.0f, -40.0f},
		{900.0f, 900.0f}, {-1000.0f, 25.0f}, {2.0f, 100000.0f}
	};

	for (const auto& [qx, qy] : queries)
	{
		auto best_d2 = std::numeric_limits<double>::max();

		for (const auto& [px, py] : expected)
		{
			const double dx = px - qx, dy = py - qy;
			best_d2 = std::min(best_d2, dx * dx + dy * dy);
		}

		const auto found = tree.find_closest(pts, qx, qy);
		const double fdx = found.x - qx, fdy = found.y - qy;
		const auto found_d2 = fdx * fdx + fdy * fdy;

		assert_equal(true, std::abs(found_d2 - best_d2) <= 1e-6 * std::max(1.0, best_d2),
		             "matches the brute-force nearest", "kd closest");

		// The build reorders coordinates and record fields in lockstep, so the record that comes
		// back must still be the one that owns the coordinate it came back with.
		assert_equal(true, found.id < expected.size(), "record identifies a point", "kd closest");
		assert_equal(true, df::equiv(found.x, expected[found.id].first) &&
		             df::equiv(found.y, expected[found.id].second), "record travels with its coordinate",
		             "kd closest");
		assert_equal(static_cast<int>(found.id * 3 + 1), static_cast<int>(found.offset),
		             "every record field travels together", "kd closest");
	}

	// An empty set has no tree and therefore no answer.
	kd_points none;
	kd_tree empty;
	empty.build(none);
	assert_equal(true, empty.is_empty(), "an empty point set builds no nodes", "kd closest");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Platform queue
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_signal_and_replace_pending_queue_work()
{
	platform::queue<int> values;
	assert_equal(true, values.enqueue(1), "first enqueue signals empty transition");
	assert_equal(false, values.enqueue(2), "additional queued work does not signal again");

	int value = 0;
	assert_equal(true, values.dequeue(value), "first value dequeued");
	assert_equal(false, values.enqueue(3), "queue remains nonempty until the final value is dequeued");
	assert_equal(true, values.dequeue(value), "second value dequeued");
	assert_equal(true, values.dequeue(value), "third value dequeued");
	assert_equal(true, values.enqueue(4), "enqueue signals after queue becomes empty again");

	platform::task_queue tasks;
	auto executed = 0;
	tasks.enqueue([&executed] { executed = 1; });
	tasks.reset_and_enqueue([&executed] { executed = 2; });
	for (const auto& task : tasks.dequeue_all()) task();
	assert_equal(2, executed, "reset_and_enqueue retains only the latest pending task");
}

// Base64 carries the saved password and the web service payloads, so a padding or alphabet slip
// corrupts stored credentials silently. Every length modulo 3 is covered because the padding rule
// differs per remainder.
static void should_round_trip_base64()
{
	assert_equal("", base64_encode(""sv), "empty encodes to empty");
	assert_equal("TQ==", base64_encode("M"sv), "one byte pads twice");
	assert_equal("TWE=", base64_encode("Ma"sv), "two bytes pad once");
	assert_equal("TWFu", base64_encode("Man"sv), "three bytes need no padding");

	for (auto length = 0u; length < 64u; ++length)
	{
		std::vector<uint8_t> data(length);

		for (auto i = 0u; i < length; ++i)
		{
			data[i] = static_cast<uint8_t>((i * 61u + 7u) & 0xff);
		}

		const auto encoded = base64_encode(data);
		const auto decoded = base64_decode(encoded);

		assert_equal(static_cast<uint32_t>(length), static_cast<uint32_t>(decoded.size()),
		             "decoded length matches");
		assert_equal(true, decoded == data, "base64 round trips every length");
	}

	// Every byte value must survive, not just printable ones.
	std::vector<uint8_t> all(256);
	for (auto i = 0u; i < 256u; ++i) all[i] = static_cast<uint8_t>(i);
	assert_equal(true, base64_decode(base64_encode(all)) == all, "every byte value round trips");
}

// The sidebar's most-common tags and file types are built from this, so a wrong order or an
// off-by-one limit is directly visible to the user.
static void should_rank_the_most_common_values()
{
	df::string_counts counts;
	counts["alpha"] = 3;
	counts["bravo"] = 11;
	counts["charlie"] = 7;
	counts["delta"] = 1;

	const auto top_two = top_map(counts, 2);
	assert_equal(2, static_cast<int>(top_two.size()), "the limit bounds the result");

	// The winners are chosen by count but presented in name order, so the list does not reshuffle
	// as counts drift during indexing.
	assert_equal("bravo", top_two[0], "first by name");
	assert_equal("charlie", top_two[1], "second by name");

	const auto all = top_map(counts, 10);
	assert_equal(4, static_cast<int>(all.size()), "a limit above the size returns everything");
	assert_equal("alpha", all[0], "sorted by name when nothing is dropped");

	assert_equal(0, static_cast<int>(top_map(counts, 0).size()), "a zero limit returns nothing");
	assert_equal(0, static_cast<int>(top_map({}, 5).size()), "no counts returns nothing");
}

void register_util_tests(view_state& state, test_registry& tests)
{
	tests.add("Should natural compare"s, should_icmp_natural);
	tests.add("Should complete result scope"s, should_complete_result_scope);
	tests.add("Should abort result scope during exception"s, should_abort_result_scope_during_exception);
	tests.add("Should cancel superseded tokens"s, should_cancel_superseded_tokens);
	tests.add("Should group elements by folder"s, should_group_elements_by_folder);
	tests.add("Should report file presence"s, should_report_file_presence);
	tests.add("Should map files"s, should_map_files);
	tests.add("Should intern strings"s, should_intern_strings);
	tests.add("Should round-trip base64"s, should_round_trip_base64);
	tests.add("Should rank the most common values"s, should_rank_the_most_common_values);
	tests.add("Should calc HMAC SHA1"s, should_calc_HMACSHA1);
	tests.add("Should calc Hashes"s, should_calc_hashes);
	tests.add("Should calc perceptual hashes"s, should_calc_perceptual_hashes);
	tests.add("Should recognise the same picture"s, should_recognise_the_same_picture);
	tests.add("Should recognise a rotated picture"s, should_recognise_a_rotated_picture);
	tests.add("Should convert Utf8"s, should_convert_utf8);
	tests.add("Should split"s, should_split);
	tests.add("Should split genre"s, should_split_genre);
	tests.add("Should extract url"s, should_extract_url);
	tests.add("Should detect wildcard"s, should_detect_wildcard);
	tests.add("Should match wildcard"s, should_match_wildcard);
	tests.add("Should compare versions"s, should_compare_versions);
	tests.add("Should Encrypt Password"s, should_encrypt_password);
	tests.add("Should parse command line"s, should_parse_command_line);
	tests.add("Should trim strings"s, should_trim_strings);
	tests.add("Should format text"s, should_format_text);
	tests.add("Should find text"s, should_find_text);

	//
	// Json
	//
	tests.add("Should parse facebook Json"s, should_parse_facebook_json);

	// Issue #203 - Cyrillic character search
	tests.add("Should handle Cyrillic case folding"s, should_handle_cyrillic_case_folding);

	// Issue #219 - Korean tags
	tests.add("Should case-fold Korean"s, should_case_fold_korean);

	//
	// kd-tree
	//
	tests.add("Should query kd-tree bounds"s, should_query_kdtree_bounds);
	tests.add("Should find the closest kd-tree point"s, should_find_the_closest_kdtree_point);

	//
	// Platform queue
	//
	tests.add("Should signal and replace pending queue work"s, should_signal_and_replace_pending_queue_work);
}
