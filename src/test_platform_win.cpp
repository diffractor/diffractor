// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Tests for Windows platform integration. These are the only tests whose subject is the
// operating system itself -- extended path syntax, DXGI device loss, the crash-guard recovery
// session, the system font stack, the registry settings store, the shell drag data object and the
// common-control paint contract the flicker-free control buffering depends on.
// Keeping them here is what lets every other test file stay free of system headers.

#include "pch.h"
#include "platform_win.h"
#include "platform_win_visual.h"
#include "test_utils.h"

static void should_convert_extended_file_system_paths()
{
	const auto long_unc = std::string("\\\\server\\share\\") + std::string(MAX_PATH, 'x');
	const auto converted = platform::to_file_system_path(df::folder_path(long_unc));
	assert_equal(std::wstring(L"\\\\?\\UNC\\server\\share\\") + std::wstring(MAX_PATH, L'x'), converted,
	             "long UNC path");

	const auto extended = std::wstring(L"\\\\?\\C:\\") + std::wstring(MAX_PATH, L'x');
	assert_equal(extended, platform::to_file_system_path(df::folder_path(extended)), "extended path unchanged");
}

static void should_classify_dxgi_device_loss()
{
	assert_equal(true, is_device_loss_error(DXGI_ERROR_DEVICE_REMOVED), "device removed");
	assert_equal(true, is_device_loss_error(DXGI_ERROR_DEVICE_RESET), "device reset");
	assert_equal(true, is_device_loss_error(DXGI_ERROR_DEVICE_HUNG), "device hung");
	assert_equal(true, is_device_loss_error(DXGI_ERROR_DRIVER_INTERNAL_ERROR), "driver internal error");
	assert_equal(false, is_device_loss_error(DXGI_STATUS_OCCLUDED), "occlusion is not device loss");
	assert_equal(false, is_device_loss_error(E_FAIL), "generic failure is not device loss");
	assert_equal(false, is_device_loss_error(S_OK), "success is not device loss");
}

static void should_suppress_gpu_for_recovery_session()
{
	platform::suppress_crash_guard(platform::crash_guard::gpu_render, true);
	// Suppression is process-global; a failing assertion below must not leave it on.
	const df::scope_exit restore_guard([]
	{
		platform::suppress_crash_guard(platform::crash_guard::gpu_render, false);
	});
	assert_equal(true, platform::crash_guard_suppressed(platform::crash_guard::gpu_render),
	             "GPU suppressed during recovery");
	assert_equal(false, platform::crash_guard_suppressed(platform::crash_guard::hw_video_decode),
	             "decode suppression remains independent");
	platform::suppress_crash_guard(platform::crash_guard::gpu_render, false);
	assert_equal(false, platform::crash_guard_suppressed(platform::crash_guard::gpu_render),
	             "GPU suppression can be cleared");
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
	constexpr char32_t hangul = U'\uAC00'; // 가

	// "Calibri" is the app's dialog/UI font; "Malgun Gothic" is the Windows Korean font.
	const auto probe = platform::probe_glyph_fallback("Calibri", "Malgun Gothic", hangul);

	if (!probe.available)
	{
		// Neither font is guaranteed on every machine. Assert the probe reported that
		// honestly rather than silently passing with nothing checked.
		assert_equal(0, probe.primary_glyph, "an unavailable probe reports no glyph data");
		return;
	}

	// 1. The primary UI font genuinely lacks Hangul -> fallback is mandatory.
	assert_equal(0, probe.primary_glyph, "Calibri has no Hangul glyph (fallback required)");

	// 2. The fallback face maps the same character to a real glyph, and querying
	//    that face for the glyph (what render_glyph SHOULD do) succeeds.
	assert_equal(true, probe.fallback_glyph != 0, "fallback face has a Hangul glyph");
	assert_equal(true, probe.fallback_metrics_ok, "fallback-face metrics query succeeds (correct face)");

	// 3. Querying the PRIMARY face for the same (fallback) glyph index yields the
	//    wrong glyph's metrics - the latent render_glyph bug.
	assert_equal(true, probe.primary_metrics_differ,
	             "primary-face metrics for a fallback glyph are wrong (latent bug)");
}

// Issue #232 - a missing Calibri produced a huge log, because every draw re-ran the family
// lookup and re-logged the failure. Issue #189 - after toggling Large Font some glyphs kept
// drawing at the old size. Both are properties of the same font-face cache: a repeated
// request must be served from the cache, and the requested size must be part of the identity
// so a size change cannot return the previous face.
static void should_cache_font_faces_per_face_and_size()
{
	const auto probe = platform::probe_font_cache(16);

	if (!probe.available)
	{
		// No text engine on this machine. Say so rather than passing with nothing checked.
		assert_equal(0, probe.entries_after_first, "an unavailable probe reports no cache entries");
		return;
	}

	assert_equal(1, probe.entries_after_first, "one request caches exactly one face");

	// #232: the repeat is answered from the cache, so no lookup and no log line repeats.
	assert_equal(true, probe.same_request_is_cached, "an identical request is served from the cache");

	// #189: the size is part of the cache identity.
	assert_equal(true, probe.size_change_is_distinct, "a font-size change yields a distinct face");
	assert_equal(true, probe.face_change_is_distinct, "a face-type change yields a distinct face");

	// A settings change resets the fonts; nothing may survive that.
	assert_equal(true, probe.reset_clears_cache, "resetting fonts empties the cache");
}

// #189 again, one level down: a cached glyph raster belongs to a size as well as to a face.
// IDWriteFontFace carries no size - the size lives on the glyph run - so a cache keyed on the
// face alone serves a raster made at the previous font size to text drawn at the current one,
// which is how a popup ended up mixing glyph sizes within one string.
static void should_key_glyph_cache_by_size()
{
	glyph_face_keys keys;

	assert_equal(true, keys.key(nullptr, 16.0f, 42) == keys.key(nullptr, 16.0f, 42),
	             "the same face, size and glyph give the same key");
	assert_equal(true, keys.key(nullptr, 16.0f, 42) != keys.key(nullptr, 24.0f, 42),
	             "a font-size change yields a distinct glyph key");
	assert_equal(true, keys.key(nullptr, 16.0f, 42) != keys.key(nullptr, 16.0f, 43),
	             "a different glyph yields a distinct key");
}

static void should_persist_to_registry()
{
	const auto archive = platform::create_registry_settings();

	const std::vector<std::string> vals = {
		"Hello World"s,
		"\r\n\t hello"s,
		"Доброго ранку!"s,
		"Japanese こんにちは世界"s,
		"Доброго ранку!"s,
		std::string(64, 'x')
	};

	for (const auto& expected : vals)
	{
		std::string actual;
		archive->write({}, "test", expected);
		archive->read({}, "test", actual);

		assert_equal_strict(expected, actual, "Persist To Registry");
	}
}

static void should_validate_registry_value_types_and_sizes()
{
	const auto section = std::format("test-malformed-{}", platform::tick_count());
	const auto sectionW = str::utf8_to_utf16(std::format("Software\\Diffractor\\{}", section));
	HKEY key = nullptr;
	assert_equal(static_cast<int>(ERROR_SUCCESS),
	             static_cast<int>(RegCreateKeyExW(HKEY_CURRENT_USER, sectionW.c_str(), 0, nullptr,
	                                              REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &key, nullptr)),
	             "create registry test section");

	constexpr wchar_t text[] = L"text";
	constexpr uint16_t short_number = 42;
	constexpr wchar_t unterminated[] = {L'r', L'a', L'w'};
	RegSetValueExW(key, L"wrong-number-type", 0, REG_SZ, reinterpret_cast<const BYTE*>(text), sizeof(text));
	RegSetValueExW(key, L"short-number", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&short_number),
	               sizeof(short_number));
	RegSetValueExW(key, L"unterminated-string", 0, REG_SZ, reinterpret_cast<const BYTE*>(unterminated),
	               sizeof(unterminated));
	RegSetValueExW(key, L"wrong-binary-type", 0, REG_SZ, reinterpret_cast<const BYTE*>(text), sizeof(text));
	RegCloseKey(key);

	{
		const auto archive = platform::create_registry_settings();
		uint32_t number = 99;
		assert_equal(false, archive->read(section, "wrong-number-type", number), "reject numeric type");
		assert_equal(99u, number, "preserve numeric output after wrong type");
		assert_equal(false, archive->read(section, "short-number", number), "reject short numeric value");
		assert_equal(99u, number, "preserve numeric output after short value");

		std::string string_value;
		assert_equal(true, archive->read(section, "unterminated-string", string_value),
		             "read unterminated registry string");
		assert_equal("raw"s, string_value, "unterminated registry string value");

		uint8_t binary[16] = {};
		auto binary_size = std::size(binary);
		assert_equal(false, archive->read(section, "wrong-binary-type", binary, binary_size),
		             "reject binary type");
	}

	RegDeleteTreeW(HKEY_CURRENT_USER, sectionW.c_str());
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Premiere duplicate-import investigation
// The drag/clipboard IDataObject advertises BOTH CF_HDROP and CFSTR_SHELLIDLIST. Per the Windows
// shell contract a conformant drop target enumerates the offered formats and consumes the FIRST
// one it supports (one format => one copy of each item). This test proves that each file-bearing
// format INDEPENDENTLY resolves to the cached items exactly once, so a well-behaved consumer
// imports a clip once, whereas a consumer that greedily reads multiple formats (the suspected
// Premiere behaviour) would import the same clip twice.
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_offer_each_drag_format_once()
{
	const auto file1 = test_files_folder.combine_file("Test.jpg");
	const auto file2 = test_files_folder.combine_file("small.jpg");

	assert_equal(true, file1.exists(), "Test.jpg exists");
	assert_equal(true, file2.exists(), "small.jpg exists");

	const std::vector<df::file_path> files{file1, file2};
	const std::vector<df::folder_path> folders;

	const auto probe = platform::probe_drag_data_object(files, folders);

	// The source advertises BOTH file-bearing formats. This is spec-compliant source
	// behaviour (Explorer does exactly the same), not a bug in itself.
	assert_equal(true, probe.advertises_hdrop, "advertises CF_HDROP");
	assert_equal(true, probe.advertises_shell_id_list, "advertises CFSTR_SHELLIDLIST");

	// CF_HDROP is enumerated before CFSTR_SHELLIDLIST. A conformant target picks the
	// first format it understands - so it consumes CF_HDROP and stops.
	assert_equal(true, probe.hdrop_enum_index >= 0, "CF_HDROP is enumerated");
	assert_equal(true, probe.shell_id_list_enum_index >= 0, "CFSTR_SHELLIDLIST is enumerated");
	assert_equal(true, probe.hdrop_enum_index < probe.shell_id_list_enum_index,
	             "CF_HDROP enumerated before CFSTR_SHELLIDLIST");

	// Each file-bearing format INDEPENDENTLY resolves to exactly the 2 input items.
	// Neither format duplicates a clip on its own - both are well-formed.
	assert_equal(2, probe.hdrop_count, "CF_HDROP yields 2 files");
	assert_equal(2, probe.shell_id_list_count, "CFSTR_SHELLIDLIST yields 2 items");
	assert_equal(2, static_cast<int>(probe.hdrop_paths.size()), "CF_HDROP resolved path count");
	assert_equal(2, static_cast<int>(probe.shell_id_list_paths.size()), "CFSTR_SHELLIDLIST resolved path count");

	const auto contains_name = [](const std::vector<std::wstring>& paths, const wchar_t* name)
	{
		return std::ranges::any_of(paths, [name](const std::wstring& p)
		{
			auto lower = p;
			std::ranges::transform(lower, lower.begin(), towlower);
			return lower.find(name) != std::wstring::npos;
		});
	};

	// Both formats resolve to the SAME two files.
	assert_equal(true, contains_name(probe.hdrop_paths, L"test.jpg"), "CF_HDROP resolves Test.jpg");
	assert_equal(true, contains_name(probe.hdrop_paths, L"small.jpg"), "CF_HDROP resolves small.jpg");
	assert_equal(true, contains_name(probe.shell_id_list_paths, L"test.jpg"), "CFSTR_SHELLIDLIST resolves Test.jpg");
	assert_equal(true, contains_name(probe.shell_id_list_paths, L"small.jpg"), "CFSTR_SHELLIDLIST resolves small.jpg");

	// The duplicate mechanism: a conformant consumer reads ONE format => 2 imports.
	// A consumer that greedily harvests BOTH file formats sees each clip twice => 4 imports.
	assert_equal(2, probe.hdrop_count, "conformant consumer (one format) imports each clip once");
	assert_equal(4, probe.hdrop_count + probe.shell_id_list_count,
	             "greedy consumer (both file formats) would import each clip twice");
}

// Native common controls are double buffered (buffered_control_paint) so a resize or splitter drag
// never composites a control that has been erased but not yet drawn. That only works if the control
// renders itself into the device context it is handed; a control that ignored the request would
// blit an empty buffer and appear blank. This test holds comctl32 to that contract for the two
// classes the app buffers.
static void should_reject_unusable_file_names()
{
	assert_equal(true, platform::is_valid_file_name("holiday 2024.jpg"), "an ordinary name is usable");
	assert_equal(true, platform::is_valid_file_name("caf\xc3\xa9 \xe6\x97\xa5.jpg"), "non-ascii is usable");
	assert_equal(true, platform::is_valid_file_name("console.txt"), "a reserved name as a prefix is usable");

	assert_equal(false, platform::is_valid_file_name(""), "an empty name is not usable");
	assert_equal(false, platform::is_valid_file_name("a?b.jpg"), "a reserved character is not usable");
	assert_equal(false, platform::is_valid_file_name("a\tb.jpg"), "a control character is not usable");
	assert_equal(false, platform::is_valid_file_name("trailing."), "a trailing dot is not usable");
	assert_equal(false, platform::is_valid_file_name("trailing "), "a trailing space is not usable");
	assert_equal(false, platform::is_valid_file_name("CON"), "a device name is not usable");
	assert_equal(false, platform::is_valid_file_name("con.txt"), "a device name with an extension is not usable");
	assert_equal(false, platform::is_valid_file_name("Lpt9.jpeg"), "device names are case insensitive");
}

static void should_render_common_controls_into_a_buffer()
{
	const auto probe = platform::probe_buffered_control_paint();

	assert_equal(true, probe.trackbar_painted_pixels > 0, "trackbar draws into a supplied dc");
	assert_equal(true, probe.trackbar_colors > 1, "trackbar draws more than a flat fill");
	assert_equal(true, probe.toolbar_painted_pixels > 0, "toolbar draws into a supplied dc");
	assert_equal(true, probe.toolbar_colors > 1, "toolbar draws more than a flat fill");
	assert_equal(true, probe.button_painted_pixels > 0, "button draws into a supplied dc");
	assert_equal(true, probe.button_colors > 1, "button draws more than a flat fill");
}

void register_tests8(view_state& state, test_registry& tests)
{
	tests.add("Should convert extended file system paths"s, should_convert_extended_file_system_paths);
	tests.add("Should classify DXGI device loss"s, should_classify_dxgi_device_loss);
	tests.add("Should suppress GPU for one recovery session"s, should_suppress_gpu_for_recovery_session);
	tests.add("Issue #219: Should fall back for missing glyphs"s, should_fall_back_for_missing_glyphs);
	tests.add("Issue #232/#189: Should cache font faces per face and size"s,
	          should_cache_font_faces_per_face_and_size);
	tests.add("Issue #189: Should key glyph cache by size"s, should_key_glyph_cache_by_size);
	tests.add("Should persist strings in registry"s, should_persist_to_registry);
	tests.add("Should validate registry value types and sizes"s, should_validate_registry_value_types_and_sizes);
	tests.add("Premiere dup: drag offers each format once"s, should_offer_each_drag_format_once);
	tests.add("Should reject unusable file names"s, should_reject_unusable_file_names);
	tests.add("Should render common controls into a buffer"s, should_render_common_controls_into_a_buffer);
}
