// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Tests for Windows platform integration. These are the only tests whose subject is the
// operating system itself -- extended path syntax, DXGI device loss, adapter classification and the
// texture budget taken from it, the adapter the decode device is made on, the crash-guard recovery
// session, the system font stack, the registry settings store, the shell drag data object and the
// common-control paint contract the flicker-free control buffering depends on.
// Keeping them here is what lets every other test file stay free of system headers.

#include "pch.h"
#include "platform_win.h"
#include "platform_win_visual.h"
#include "av_format.h"
#include "test_fixtures.h"

extern "C" {
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_d3d11va.h"
}

static void should_convert_extended_file_system_paths()
{
	const auto long_unc = std::string("\\\\server\\share\\") + std::string(MAX_PATH, 'x');
	const auto converted = platform::to_file_system_path(df::folder_path(long_unc));
	assert_equal(std::wstring(L"\\\\?\\UNC\\server\\share\\") + std::wstring(MAX_PATH, L'x'), converted,
	             "long UNC path");

	const auto extended = std::wstring(L"\\\\?\\C:\\") + std::wstring(MAX_PATH, L'x');
	assert_equal(extended, platform::to_file_system_path(df::folder_path(str::utf16_to_utf8(extended))),
	             "extended path unchanged");
}

// The measured length excludes the terminator a string_view does not carry, so converting with -1
// both over-read the view and asked for one wchar more than the measurement allowed. The call then
// failed and left the buffer zeroed, which made every ordinary path convert to a run of NULs.
static void should_convert_utf8_to_ansi()
{
	assert_equal("photo.jpg", platform::utf8_to_a("photo.jpg"), "an ordinary name converts");
	assert_equal(9_z, platform::utf8_to_a("photo.jpg").size(), "and is not padded with terminators");
	assert_equal(true, platform::utf8_to_a("").empty(), "an empty name converts to an empty name");

	// The source has no terminator of its own: the conversion must respect the view's length rather
	// than read past it.
	const std::string backing("C:\\photos\\holiday.jpgTRAILING");
	assert_equal("C:\\photos\\holiday.jpg", platform::utf8_to_a(std::string_view(backing).substr(0, 21)),
	             "a view stops where it says it stops");

	// The console commands run with a UTF-8 C locale, where each of these characters takes three
	// bytes. An output sized at two bytes per UTF-16 unit truncated a name that is more CJK than
	// ASCII, and a truncated conversion answers empty - so the file could not be opened by name.
	const std::string cjk = "D:\\\xe5\x8b\x95\xe7\x94\xbb\\\xe6\x9d\xb1\xe4\xba\xac\xe6\x97\x85\xe8\xa1\x8c"
		"\xe3\x81\xae\xe6\x80\x9d\xe3\x81\x84\xe5\x87\xba.mp4";
	assert_equal(cjk, platform::utf8_to_a(cjk), "a name of three-byte characters converts whole");
}

// Undocking or unplugging a second display leaves a saved rect that intersects no monitor at all.
static void should_restore_a_window_onto_a_display()
{
	constexpr recti work(0, 0, 1920, 1040);

	const auto from_a_missing_display = platform::fit_window_to_work_area({2200, 300, 3000, 900}, work);
	assert_equal(800, from_a_missing_display.width(), "width is preserved");
	assert_equal(600, from_a_missing_display.height(), "height is preserved");
	assert_equal(true, work.intersects(from_a_missing_display), "and the window lands on the display");
	assert_equal(1120, from_a_missing_display.left, "pushed just inside the right edge");
	assert_equal(300, from_a_missing_display.top, "the axis that already fitted is left alone");

	const auto from_the_left = platform::fit_window_to_work_area({-3000, -400, -2600, -100}, work);
	assert_equal(0, from_the_left.left, "a negative origin comes back to the work area");
	assert_equal(0, from_the_left.top, "on both axes");

	const auto too_large = platform::fit_window_to_work_area({4000, 0, 8000, 3000}, work);
	assert_equal(1920, too_large.width(), "a window larger than the display is clamped to it");
	assert_equal(1040, too_large.height(), "on both axes");
	assert_equal(0, too_large.left, "and positioned at the origin rather than hanging off");

	// Which saved rects are corrected at all. Reaching a display is not on its own enough to be usable.
	assert_equal(true, platform::window_needs_refit({2200, 300, 3000, 900}, work, false),
	             "a rect saved on a display that is gone");
	assert_equal(false, platform::window_needs_refit({100, 100, 900, 700}, work, true),
	             "an ordinary rect is restored exactly as it was saved");
	assert_equal(false, platform::window_needs_refit({1500, 100, 2400, 700}, work, true),
	             "and so is one deliberately straddling two displays");
	assert_equal(true, platform::window_needs_refit({0, 0, 3840, 2160}, work, true),
	             "a window saved on a larger display is clamped to this one");
	assert_equal(true, platform::window_needs_refit({1900, 100, 2700, 700}, work, true),
	             "a sliver at the edge leaves nothing to grab");
	assert_equal(true, platform::window_needs_refit({100, 1030, 900, 1630}, work, true),
	             "and neither has one pushed off the bottom");
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

// A machine with no working GPU driver is handed WARP posing as a hardware adapter. Drawn through, it
// redrew the whole window on the CPU every frame, so it is recognised and the CPU backend used instead.
static void should_recognise_the_basic_render_driver()
{
	assert_equal(true, is_software_adapter(0x1414, 0x8c, 0), "the Microsoft Basic Render Driver");
	assert_equal(true, is_software_adapter(0x10de, 0x2684, DXGI_ADAPTER_FLAG_SOFTWARE),
	             "any adapter that says it is software");
	assert_equal(false, is_software_adapter(0x8086, 0x3ea0, 0), "an Intel integrated GPU");
	assert_equal(false, is_software_adapter(0x10de, 0x2684, 0), "an NVIDIA card");
	assert_equal(false, is_software_adapter(0x1414, 0x5353, 0), "the vendor alone does not make an adapter WARP");
}

// NV12 and P010 are drawn by sampling views of their planes. A device that could only hold the format
// accepted the texture, refused its views, and the picture never appeared.
static void should_need_sampling_for_planar_video()
{
	assert_equal(false, can_sample_texture_format(D3D11_FORMAT_SUPPORT_TEXTURE2D), "holding the format is not enough");
	assert_equal(false, can_sample_texture_format(D3D11_FORMAT_SUPPORT_SHADER_SAMPLE),
	             "nor is sampling a format that cannot be a 2D texture");
	assert_equal(false, can_sample_texture_format(0), "nothing supported");
	assert_equal(true, can_sample_texture_format(D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE |
		             D3D11_FORMAT_SUPPORT_DECODER_OUTPUT), "a device that samples it");
}

// Intel parts report the 128MB carved out at boot as dedicated memory, and allocate textures from shared
// memory. Read as the card's own, it held every Intel laptop at the 64MB floor, and a photo over 16
// megapixels was shown downscaled even at 100%.
static void should_count_shared_memory_for_an_integrated_gpu()
{
	constexpr uint64_t mb = 1024ull * 1024ull;
	constexpr uint64_t gb = 1024ull * mb;
	constexpr auto laptop = static_cast<int64_t>(16 * gb);
	const auto budget = [](const uint64_t gpu_bytes, const int64_t total_phys)
	{
		return static_cast<uint64_t>(calc_texture_budget(gpu_bytes, total_phys));
	};

	assert_equal(128 * mb + 8 * gb, gpu_texture_memory(128 * mb, 8 * gb, true), "an integrated part uses shared memory");
	assert_equal(8 * gb, gpu_texture_memory(8 * gb, 16 * gb, false), "a discrete card uses its own");
	assert_equal(8 * gb, gpu_texture_memory(0, 8 * gb, false), "an adapter with none of its own still has somewhere");

	assert_equal(128 * mb, budget(gpu_texture_memory(128 * mb, 8 * gb, true), laptop),
	             "an integrated laptop GPU is given the full ceiling");
	assert_equal(64 * mb, budget(128 * mb, laptop), "which the carve-out alone held at the floor");
	assert_equal(64 * mb, budget(512 * mb, laptop), "a small discrete card is still held back");
	assert_equal(128 * mb, budget(8 * gb, laptop), "a large card is held to the ceiling");
	assert_equal(96 * mb, budget(gpu_texture_memory(128 * mb, 768 * mb, true), static_cast<int64_t>(1536 * mb)),
	             "physical memory still bounds a small machine");
	assert_equal(128 * mb, budget(0, 0), "with nothing to ask, the shipped ceiling");
}

static bool same_adapter(const LUID a, const LUID b)
{
	return a.LowPart == b.LowPart && a.HighPart == b.HighPart;
}

struct listed_adapter
{
	LUID luid = {};
	bool makes_video_devices = false;
};

// In the order Windows lists them, so the first is the default FFmpeg would take when left to choose.
static std::vector<listed_adapter> list_adapters()
{
	std::vector<listed_adapter> result;
	ComPtr<IDXGIFactory1> factory;
	if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return result;

	ComPtr<IDXGIAdapter1> adapter;

	for (UINT i = 0; SUCCEEDED(factory->EnumAdapters1(i, &adapter)); ++i)
	{
		DXGI_ADAPTER_DESC1 desc = {};
		if (FAILED(adapter->GetDesc1(&desc))) continue;

		ComPtr<ID3D11Device> device;
		const auto makes_video = SUCCEEDED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
		                                                     D3D11_CREATE_DEVICE_VIDEO_SUPPORT, nullptr, 0,
		                                                     D3D11_SDK_VERSION, &device, nullptr, nullptr));
		result.push_back({desc.AdapterLuid, makes_video});
	}

	return result;
}

static LUID adapter_of(const AVBufferRef* hw_device)
{
	const auto* const ctx = reinterpret_cast<const AVHWDeviceContext*>(hw_device->data);
	const auto* const hwctx = static_cast<const AVD3D11VADeviceContext*>(ctx->hwctx);
	ComPtr<IDXGIDevice> dxgi_device;
	ComPtr<IDXGIAdapter> adapter;
	DXGI_ADAPTER_DESC desc = {};

	if (hwctx->device && SUCCEEDED(hwctx->device->QueryInterface(IID_PPV_ARGS(&dxgi_device))) &&
		SUCCEEDED(dxgi_device->GetAdapter(&adapter)) && SUCCEEDED(adapter->GetDesc(&desc)))
	{
		return desc.AdapterLuid;
	}

	return {};
}

// Left to itself FFmpeg made the decode device on whichever adapter was the default when the first
// video opened. Where that was not the renderer's, the shared-texture bridge could not cross to it and
// every frame went through system memory. The adapters asked about here are the last ones listed,
// which are never that default wherever there are two: on every machine since Windows 8 the
// Microsoft Basic Render Driver is listed after the GPUs.
static void should_create_the_decode_device_on_the_render_adapter()
{
	assert_equal(true, av_platform_create_hw_device(AV_HWDEVICE_TYPE_DXVA2) == nullptr,
	             "a device the renderer cannot present from is not made");

	const auto adapters = list_adapters();
	if (adapters.empty()) return;

	const auto last = adapters.back().luid;
	DXGI_ADAPTER_DESC1 found = {};
	const auto adapter = find_adapter_by_luid(last);
	assert_equal(true, adapter && SUCCEEDED(adapter->GetDesc1(&found)) && same_adapter(last, found.AdapterLuid),
	             "an adapter is found by what it is, not where it is listed");
	assert_equal(true, find_adapter_by_luid({}) == nullptr, "and none for the renderer that is not there");

	const auto video = std::ranges::find_if(adapters.rbegin(), adapters.rend(),
	                                        [](const listed_adapter& a) { return a.makes_video_devices; });
	if (video == adapters.rend()) return;

	const auto previous = render_adapter();
	const df::scope_exit restore([previous] { publish_render_adapter(previous); });
	publish_render_adapter(video->luid);

	auto* device = av_platform_create_hw_device(AV_HWDEVICE_TYPE_D3D11VA);
	const df::scope_exit release([&device] { av_buffer_unref(&device); });

	assert_equal(true, device != nullptr, "a decode device is made");
	if (!device) return;

	assert_equal(true, same_adapter(video->luid, adapter_of(device)), "on the adapter the renderer draws with");
	assert_equal(true, av_platform_hw_device_usable(device), "and FFmpeg can decode on it");
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

class registry_test_root
{
	std::string _root;
	std::wstring _root_w;

public:
	explicit registry_test_root(const std::string_view name)
	{
		_root = std::format("Software\\DiffractorTest\\{}-{}", name, platform::tick_count());
		_root_w = str::utf8_to_utf16(_root);
	}

	~registry_test_root()
	{
		RegDeleteTreeW(HKEY_CURRENT_USER, _root_w.c_str());
	}

	registry_test_root(const registry_test_root&) = delete;
	registry_test_root& operator=(const registry_test_root&) = delete;

	const std::string& path() const
	{
		return _root;
	}

	std::wstring subkey(const std::string_view section) const
	{
		return str::utf8_to_utf16(std::format("{}\\{}", _root, section));
	}
};

static void should_persist_to_registry()
{
	const registry_test_root root("strings");
	const auto archive = platform::create_registry_settings(root.path());

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
	const registry_test_root root("malformed");
	const auto section = std::format("test-malformed-{}", platform::tick_count());
	const auto sectionW = root.subkey(section);
	HKEY key = nullptr;
	assert_equal(static_cast<int>(ERROR_SUCCESS),
	             static_cast<int>(RegCreateKeyExW(HKEY_CURRENT_USER, sectionW.c_str(), 0, nullptr,
	                                              REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &key, nullptr)),
	             "create registry test section");
	const df::scope_exit close_key([&key] { if (key) RegCloseKey(key); });

	constexpr wchar_t text[] = L"text";
	constexpr uint16_t short_number = 42;
	constexpr wchar_t unterminated[] = {L'r', L'a', L'w'};
	assert_equal(static_cast<int>(ERROR_SUCCESS),
	             static_cast<int>(RegSetValueExW(key, L"wrong-number-type", 0, REG_SZ,
	                                             reinterpret_cast<const BYTE*>(text), sizeof(text))),
	             "write wrong-number-type");
	assert_equal(static_cast<int>(ERROR_SUCCESS),
	             static_cast<int>(RegSetValueExW(key, L"short-number", 0, REG_DWORD,
	                                             reinterpret_cast<const BYTE*>(&short_number),
	                                             sizeof(short_number))),
	             "write short-number");
	assert_equal(static_cast<int>(ERROR_SUCCESS),
	             static_cast<int>(RegSetValueExW(key, L"unterminated-string", 0, REG_SZ,
	                                             reinterpret_cast<const BYTE*>(unterminated),
	                                             sizeof(unterminated))),
	             "write unterminated-string");
	assert_equal(static_cast<int>(ERROR_SUCCESS),
	             static_cast<int>(RegSetValueExW(key, L"wrong-binary-type", 0, REG_SZ,
	                                             reinterpret_cast<const BYTE*>(text), sizeof(text))),
	             "write wrong-binary-type");
	RegCloseKey(key);
	key = nullptr;

	{
		const auto archive = platform::create_registry_settings(root.path());
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
	const auto file2 = test_files_folder.combine_file("Small.jpg");

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

static int WINAPI shell_success_w(SHFILEOPSTRUCTW*)
{
	return 0;
}

static int WINAPI shell_aborts_w(SHFILEOPSTRUCTW* op)
{
	op->fAnyOperationsAborted = TRUE;
	return 0;
}

static int WINAPI shell_fails_w(SHFILEOPSTRUCTW*)
{
	return ERROR_ACCESS_DENIED;
}

static int WINAPI shell_aborts_a(SHFILEOPSTRUCTA* op)
{
	op->fAnyOperationsAborted = TRUE;
	return 0;
}

static void should_double_terminate_shell_destination_buffers()
{
	const auto target = df::folder_path("C:\\photos");
	const auto wide_destination = platform::probe_shell_destination_w(target);
	const auto narrow_destination = platform::probe_shell_destination_a(target);

	assert_equal(L'\0', wide_destination[wide_destination.size() - 1], "wide destination final terminator");
	assert_equal(L'\0', wide_destination[wide_destination.size() - 2], "wide destination penultimate terminator");
	assert_equal('\0', narrow_destination[narrow_destination.size() - 1], "narrow destination final terminator");
	assert_equal('\0', narrow_destination[narrow_destination.size() - 2], "narrow destination penultimate terminator");
}

static void should_classify_an_aborted_shell_operation_as_cancelled()
{
	assert_equal(static_cast<int>(platform::file_op_result_code::CANCELLED),
	             static_cast<int>(platform::probe_shell_file_operation_w(shell_aborts_w).result.code),
	             "wide shell abort is cancellation");
	assert_equal(static_cast<int>(platform::file_op_result_code::CANCELLED),
	             static_cast<int>(platform::probe_shell_file_operation_a(shell_aborts_a).result.code),
	             "narrow shell abort is cancellation");
	assert_equal(static_cast<int>(platform::file_op_result_code::OK),
	             static_cast<int>(platform::probe_shell_delete_operation(shell_success_w, true).result.code),
	             "zero and no abort is success");
	assert_equal(static_cast<int>(platform::file_op_result_code::FAILED),
	             static_cast<int>(platform::probe_shell_delete_operation(shell_fails_w, true).result.code),
	             "nonzero shell result is failure");
}

static void should_let_windows_confirm_recycle_that_would_destroy()
{
	const auto recycle_flags = platform::probe_shell_delete_flags(true);
	assert_equal(true, (recycle_flags & FOF_ALLOWUNDO) != 0, "requested recycle allows undo");
	assert_equal(true, (recycle_flags & FOF_WANTNUKEWARNING) != 0,
	             "requested recycle keeps Windows permanent-delete warning");
	assert_equal(true, (recycle_flags & FOF_SILENT) != 0, "requested recycle suppresses progress UI");
	assert_equal(false, (recycle_flags & FOF_NOCONFIRMATION) != 0,
	             "requested recycle lets Windows ask before permanent fallback");

	const auto permanent_flags = platform::probe_shell_delete_flags(false);
	assert_equal(true, (permanent_flags & FOF_NOCONFIRMATION) != 0,
	             "explicit permanent delete suppresses duplicate shell confirmation");
	assert_equal(false, (permanent_flags & FOF_ALLOWUNDO) != 0, "explicit permanent delete does not allow undo");
	assert_equal(false, (permanent_flags & FOF_WANTNUKEWARNING) != 0,
	             "explicit permanent delete was already confirmed by the application");
}

static void should_materialize_advertised_clipboard_images()
{
	files ff;
	const auto loaded = ff.load(test_files_folder.combine_file("Test.jpg"), false);
	assert_equal(true, loaded.success, "test image loads");

	const auto probe = platform::probe_drag_data_object(loaded);

	assert_equal(true, probe.advertises_bitmap, "captured image advertises CF_BITMAP");
	assert_equal(true, probe.advertises_dib, "captured image advertises CF_DIB");
	assert_equal(true, probe.bitmap_retrieved, "advertised bitmap retrieves");
	assert_equal(true, probe.dib_retrieved, "advertised DIB retrieves");
	assert_equal(loaded.dimensions().cx, probe.bitmap_dimensions.cx, "bitmap width");
	assert_equal(loaded.dimensions().cy, probe.bitmap_dimensions.cy, "bitmap height");
	assert_equal(loaded.dimensions().cx, probe.dib_dimensions.cx, "DIB width");
	assert_equal(loaded.dimensions().cy, probe.dib_dimensions.cy, "DIB height");
	assert_equal(2, static_cast<int>(probe.image_formats.size()), "two image formats advertised");
	assert_equal(static_cast<uint32_t>(CF_BITMAP), probe.image_formats[0], "bitmap is first image offer");
	assert_equal(static_cast<uint32_t>(TYMED_GDI), probe.image_tymed[0], "bitmap uses GDI medium");
	assert_equal(static_cast<uint32_t>(CF_DIB), probe.image_formats[1], "DIB is second image offer");
	assert_equal(static_cast<uint32_t>(TYMED_HGLOBAL), probe.image_tymed[1], "DIB uses global memory");
}

static void should_publish_pasted_bitmap_after_closing_stage()
{
	const auto folder = _temps.folder().combine(std::format("pasted-bitmap-{}", platform::tick_count()));
	assert_equal(true, platform::create_folder(folder).success(), "create bitmap paste temp folder");

	const uint32_t pixels[] = {
		0xffff0000, 0xff00ff00,
		0xff0000ff, 0xffffffff
	};
	const auto bitmap = CreateBitmap(2, 2, 1, 32, pixels);
	assert_equal(true, bitmap != nullptr, "create test bitmap");
	const df::scope_exit delete_bitmap([bitmap] { DeleteObject(bitmap); });

	const auto result = save_bitmap_info(folder, "pasted", true, bitmap);

	assert_equal(true, result.success(), "bitmap save succeeds");
	assert_equal(1, static_cast<int>(result.created_files.files.size()), "one published file");
	const auto final_path = result.created_files.files.front();
	assert_equal(true, final_path.exists(), "published file exists");

	const auto attributes = GetFileAttributesW(platform::to_file_system_path(final_path).c_str());
	assert_equal(false, attributes == INVALID_FILE_ATTRIBUTES, "published file attributes read");
	assert_equal(false, (attributes & FILE_ATTRIBUTE_TEMPORARY) != 0, "published file is not temporary");

	auto stage_count = 0;
	for (const auto& entry : std::filesystem::directory_iterator(platform::to_file_system_path(folder)))
	{
		const auto name = entry.path().filename().wstring();
		if (name.starts_with(L"diffractor_")) ++stage_count;
	}
	assert_equal(0, stage_count, "owned stage file was removed or published");
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

static void should_replace_the_full_trailing_spelling_word()
{
	const auto invalid = [](const std::string_view word)
	{
		return word != "teh" && word != "x";
	};

	const auto replace_first = [](std::wstring text, const std::vector<platform::edit_spelling_range>& ranges)
	{
		assert_equal(1_z, ranges.size(), "one misspelling is found");
		text.replace(static_cast<size_t>(ranges.front().pos_start),
		             static_cast<size_t>(ranges.front().pos_end - ranges.front().pos_start), L"the");
		return text;
	};

	assert_equal(L"the"s, replace_first(L"teh", platform::probe_edit_spelling_ranges(L"teh", invalid)),
	             "a final word includes its last character");
	assert_equal(L"prefix the"s, replace_first(L"prefix teh", platform::probe_edit_spelling_ranges(L"prefix teh", invalid)),
	             "surrounding prefix text is untouched");
	assert_equal(L"the "s, replace_first(L"teh ", platform::probe_edit_spelling_ranges(L"teh ", invalid)),
	             "a word followed by whitespace keeps the separator");

	const auto one = platform::probe_edit_spelling_ranges(L"x", invalid);
	assert_equal(1_z, one.size(), "a one-character final word is found");
	assert_equal(0, one.front().pos_start, "one-character start");
	assert_equal(1, one.front().pos_end, "one-character end remains exclusive");
}

static void should_hit_test_the_last_trailing_spelling_character()
{
	const auto point_x = [](const DWORD p) { return static_cast<int>(static_cast<short>(LOWORD(p))); };
	const auto point_y = [](const DWORD p) { return static_cast<int>(static_cast<short>(HIWORD(p))); };

	const auto parent = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 240, 60, nullptr, nullptr,
	                                   get_resource_instance, nullptr);
	assert_equal(true, parent != nullptr, "parent window created");
	const df::scope_exit destroy_parent([parent] { if (parent) DestroyWindow(parent); });

	const auto edit = CreateWindowExW(0, L"EDIT", L"prefix teh", WS_CHILD | WS_VISIBLE, 0, 0, 220, 30, parent,
	                                 nullptr, get_resource_instance, nullptr);
	assert_equal(true, edit != nullptr, "edit control created");
	const df::scope_exit destroy_edit([edit] { if (edit) DestroyWindow(edit); });

	const auto last_char = static_cast<DWORD>(SendMessageW(edit, EM_POSFROMCHAR, 9, 0));
	const auto end_pos = static_cast<DWORD>(SendMessageW(edit, EM_POSFROMCHAR, 10, 0));
	assert_equal(true, point_x(last_char) >= 0, "last character has a native edit position");
	assert_equal(-1, point_x(end_pos), "exclusive end is past the last native edit character");

	const auto bounds = platform::probe_edit_spelling_bounds(edit, 7, 10);
	assert_equal(true, !bounds.is_empty(), "the spelling bounds include a trailing word whose end is exclusive");
	assert_equal(true, bounds.contains({point_x(last_char) + 1, point_y(last_char) + 1}),
	             "the spelling hit-test reaches the final character");
}

static void should_delegate_ordinary_edit_context_menus()
{
	using route = platform::edit_context_menu_route;

	assert_equal(static_cast<int>(route::native_edit_procedure),
	             static_cast<int>(platform::probe_edit_context_menu_route(false, false)),
	             "spelling-disabled or correctly-spelled text is delegated");
	assert_equal(static_cast<int>(route::native_edit_procedure),
	             static_cast<int>(platform::probe_edit_context_menu_route(true, false)),
	             "a failed custom menu creation is delegated");
	assert_equal(static_cast<int>(route::custom_spelling_menu),
	             static_cast<int>(platform::probe_edit_context_menu_route(true, true)),
	             "a real misspelling shows exactly the custom spelling menu");
}

static void should_read_coherent_number_format_snapshots()
{
	platform::set_number_format_probe_snapshots({
		                                            {L".", L",", 1, 1},
		                                            {L",", L".", 1, 1},
	                                            },
	                                            true);
	const df::scope_exit restore([] { platform::clear_number_format_probe_snapshots(); });

	assert_equal("1,234", platform::format_number("1234"), "the first format uses one complete snapshot");
	assert_equal(",", platform::number_dec_sep(), "invalidation during refresh remains pending for the next read");
	assert_equal("1.234", platform::format_number("1234"), "the second format uses the next complete snapshot");
	assert_equal(",", platform::number_dec_sep(), "the separator belongs to the same refreshed snapshot");
}

static void should_bound_the_software_buffer_as_the_client_grows()
{
	const auto probe = platform::probe_software_tiling();

	// Without this the assertion below would still pass if the buffer went back to tracking the
	// window, which is the thing tiling exists to stop.
	assert_equal(true, probe.grown_buffer_pixels < probe.grown_client_pixels / 4,
	             "the buffer stayed bounded while the client grew");
	assert_equal(probe.grown_client_pixels, probe.grown_writable_pixels,
	             "a grown client is writable to its new edges without reallocating the tile");
}

static void should_translate_highlight_spans_to_utf16_clusters()
{
	const auto highlight = ui::color(1.0f, 0.0f, 0.0f, 1.0f);
	const auto plain = ui::color(0.0f, 0.0f, 1.0f, 1.0f);
	const std::string text = "\xf0\x9f\x98\x80" "caf\xc3\xa9" "X";
	const auto converted = ui::utf8_to_utf16(text, {{4, 5, highlight}});

	assert_equal(7_z, converted.text.size(), "emoji is preserved as a surrogate pair");
	assert_equal(0xd83d, static_cast<int>(converted.text[0]), "lead surrogate");
	assert_equal(0xde00, static_cast<int>(converted.text[1]), "trail surrogate");
	assert_equal(1_z, converted.highlights.size(), "one highlight survives conversion");
	assert_equal(2u, converted.highlights[0].offset, "highlight begins after the surrogate pair");
	assert_equal(4u, converted.highlights[0].length, "highlight length is UTF-16 text, not UTF-8 bytes");

	const uint16_t clusters[] = {0, 0, 1};
	const auto first = ui::text_cluster_span_for_glyph(0, 2, clusters, 3, 10);
	const auto second = ui::text_cluster_span_for_glyph(1, 2, clusters, 3, 10);
	assert_equal(10u, first.offset, "cluster offset includes run text position");
	assert_equal(2u, first.length, "one glyph cluster can span multiple text units");
	assert_equal(12u, second.offset, "the next glyph starts after the multi-unit cluster");
	assert_equal(highlight.r, ui::text_cluster_color(plain, first, {{10, 2, highlight}}).r,
	             "a complete cluster is highlighted");
	assert_equal(highlight.r, ui::text_cluster_color(plain, first, {{10, 1, highlight}}).r,
	             "a base letter highlight colours its combining-mark cluster");
}

static void should_build_text_cluster_spans_once_per_run()
{
	const uint16_t clusters[] = {0, 0, 1, 2, 2};
	const auto spans = ui::text_cluster_spans_for_glyph_run(3, clusters, 5, 4);

	assert_equal(3_z, spans.size(), "one span per glyph");
	assert_equal(4u, spans[0].offset, "first cluster starts at run text position");
	assert_equal(2u, spans[0].length, "first cluster spans two text units");
	assert_equal(6u, spans[1].offset, "second cluster follows");
	assert_equal(1u, spans[1].length, "second cluster is one text unit");
	assert_equal(7u, spans[2].offset, "third cluster follows");
	assert_equal(2u, spans[2].length, "third cluster spans two text units");
}

static void should_apply_positive_ascender_offsets_upward()
{
	assert_equal(80.0f, ui::glyph_top_from_baseline(100.0f, 20, 0.0f), "no ascender offset");
	assert_equal(77.0f, ui::glyph_top_from_baseline(100.0f, 20, 3.0f), "positive ascender moves up");
	assert_equal(83.0f, ui::glyph_top_from_baseline(100.0f, 20, -3.0f), "negative ascender moves down");
}

static void should_clear_text_highlights_between_draws()
{
	std::vector<ui::text_highlight_t> highlights = {{0, 3, ui::color(1.0f, 0.0f, 0.0f, 1.0f)}};
	ui::clear_text_highlights(highlights);
	assert_equal(true, highlights.empty(), "plain/layout draws start without stale highlight intervals");
}

static void should_gate_native_bubble_alpha_steps()
{
	const auto restore_animations = ui::animations_enabled;
	const df::scope_exit restore_scope([restore_animations] { ui::animations_enabled = restore_animations; });

	ui::animations_enabled = true;
	assert_equal(72, ui::fade_alpha_step(0, 255), "enabled fade advances toward visible");
	assert_equal(0, ui::fade_alpha_step(1, 0), "enabled fade snaps the terminal hidden alpha");

	ui::animations_enabled = false;
	auto alpha = 0;
	alpha = ui::gated_fade_alpha_step(alpha, 255);
	assert_equal(255, alpha, "disabled show snaps visible");
	alpha = ui::gated_fade_alpha_step(alpha, 0);
	assert_equal(0, alpha, "disabled hide snaps hidden");
}

static void should_handle_unavailable_font_layout()
{
	text_layout_impl layout(nullptr, ui::style::font_face::dialog);

	layout.update("text", ui::style::text_style::single_line);
	const auto measured = layout.measure_text(100, 100);
	assert_equal(0, measured.cx, "unavailable font has no measured width");
	assert_equal(0, measured.cy, "unavailable font has no measured height");

	const auto offsets = layout.offset_xs({0, 2, 4}, 100, 100);
	assert_equal(3_z, offsets.size(), "unavailable font still answers each requested offset");
	assert_equal(0, offsets[0], "first unavailable offset");
	assert_equal(0, offsets[1], "middle unavailable offset");
	assert_equal(0, offsets[2], "last unavailable offset");
	assert_equal(true, should_retry_font_renderer(nullptr), "GPU renderer retries a transient null font");
}

static void should_refuse_an_impossible_movie()
{
	// The probe is the whole of whether Render is offered, so an unstable answer would make the
	// command appear and disappear between toolbar rebuilds.
	const auto answer = platform::can_write_movies();
	assert_equal(answer, platform::can_write_movies(), "the encoder probe answers the same way twice");

	platform::movie_writer_request request;
	request.path = _temps.next_path(".mp4");
	request.extent = {0, 0};
	request.frame_rate = 30;
	request.video_bitrate = 6000000;

	// A writer that accepted this would leave a zero-byte file where the movie should be, and the
	// render would report success.
	assert_equal(true, platform::create_movie_writer(request) == nullptr, "a zero-sized movie is refused");
	assert_equal(false, platform::exists(request.path), "and no partial file is left behind");

	request.extent = {1920, 1080};
	request.frame_rate = 0;
	assert_equal(true, platform::create_movie_writer(request) == nullptr, "a movie with no frame rate is refused");
	assert_equal(false, platform::exists(request.path), "and still leaves nothing behind");
}

void register_platform_tests(view_state& state, test_registry& tests)
{
	tests.add("Should convert extended file system paths"s, should_convert_extended_file_system_paths);
	tests.add("Should convert utf8 to ansi"s, should_convert_utf8_to_ansi);
	tests.add("Should restore a window onto a display that still exists"s, should_restore_a_window_onto_a_display);
	tests.add("Should classify DXGI device loss"s, should_classify_dxgi_device_loss);
	tests.add("Should recognise the Microsoft Basic Render Driver as software"s,
	          should_recognise_the_basic_render_driver);
	tests.add("Should need shader sampling for planar video textures"s, should_need_sampling_for_planar_video);
	tests.add("Should count shared memory for an integrated GPU"s, should_count_shared_memory_for_an_integrated_gpu);
	tests.add("Should create the decode device on the render adapter"s,
	          should_create_the_decode_device_on_the_render_adapter);
	tests.add("Should suppress GPU for one recovery session"s, should_suppress_gpu_for_recovery_session);
	tests.add("Issue #219: Should fall back for missing glyphs"s, should_fall_back_for_missing_glyphs);
	tests.add("Issue #232/#189: Should cache font faces per face and size"s,
	          should_cache_font_faces_per_face_and_size);
	tests.add("Issue #189: Should key glyph cache by size"s, should_key_glyph_cache_by_size);
	tests.add("Should persist strings in registry"s, should_persist_to_registry);
	tests.add("Should validate registry value types and sizes"s, should_validate_registry_value_types_and_sizes);
	tests.add("Premiere dup: drag offers each format once"s, should_offer_each_drag_format_once);
	// PLAT-002 - shell destination double-NUL.
	tests.add("Should double-terminate shell destination buffers"s,
	          should_double_terminate_shell_destination_buffers);
	// PLAT-003 - shell aborted flag sequencing.
	tests.add("Should classify an aborted shell operation as cancelled"s,
	          should_classify_an_aborted_shell_operation_as_cancelled);
	// PLAT-001 - Windows owns permanent-delete confirmation for recycle fallback.
	tests.add("Should let Windows confirm recycle that would destroy"s,
	          should_let_windows_confirm_recycle_that_would_destroy);
	// PLAT-004 - advertised clipboard image formats are materializable.
	tests.add("Should materialize advertised clipboard images"s, should_materialize_advertised_clipboard_images);
	// PLAT-006 - pasted bitmap stages close before publication and cleanup.
	tests.add("Should publish pasted bitmap after closing stage"s, should_publish_pasted_bitmap_after_closing_stage);
	tests.add("Should reject unusable file names"s, should_reject_unusable_file_names);
	tests.add("Should render common controls into a buffer"s, should_render_common_controls_into_a_buffer);
	// PLAT-011 - trailing spelling correction range.
	tests.add("Should replace the full trailing spelling word"s, should_replace_the_full_trailing_spelling_word);
	tests.add("Should hit test the last trailing spelling character"s,
	          should_hit_test_the_last_trailing_spelling_character);
	// PLAT-012 - native edit context-menu routing.
	tests.add("Should delegate ordinary common controls edit context menus"s,
	          should_delegate_ordinary_edit_context_menus);
	// PLAT-018 - coherent locale-format snapshots.
	tests.add("Should read coherent number format snapshots"s, should_read_coherent_number_format_snapshots);
	tests.add("Should bound the software buffer as the client grows"s,
	          should_bound_the_software_buffer_as_the_client_grows);
	// UI-006 - Unicode highlights must be translated to the UTF-16 clusters DirectWrite draws.
	tests.add("Should translate highlight spans to utf16 clusters"s, should_translate_highlight_spans_to_utf16_clusters);
	// UI-006 - cluster spans are computed once for the run instead of scanning for each glyph.
	tests.add("Should build text cluster spans once per run"s, should_build_text_cluster_spans_once_per_run);
	// PLAT-015 - DirectWrite positive ascender offsets are upward in screen coordinates.
	tests.add("Should apply positive ascender offsets upward"s, should_apply_positive_ascender_offsets_upward);
	// PLAT-014 - plain and layout draws must drop transient highlight state from previous draws.
	tests.add("Should clear text highlights between draws"s, should_clear_text_highlights_between_draws);
	// PLAT-017 - Native bubble fades must honor the shared animation gate.
	tests.add("Should gate native bubble alpha steps"s, should_gate_native_bubble_alpha_steps);
	// PLAT-016 - a missing font renderer is a degraded text outcome, not a null dereference.
	tests.add("Should handle unavailable font layout"s, should_handle_unavailable_font_layout);
	tests.add("Should refuse an impossible movie"s, should_refuse_an_impossible_movie);
}
