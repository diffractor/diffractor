// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "pch.h"
#include "model.h"
#include "model_db.h"
#include "view_test.h"

#include "app_command_line.h"
#include "metadata_exif.h"
#include "metadata_iptc.h"
#include "metadata_xmp.h"
#include "crypto.h"
#include "util_base64.h"
#include "model_locations.h"
#include "model_index.h"
#include "crypto_sha.h"
#include "model_db_pack.h"
#include "model_tokenizer.h"
#include "ui_controls.h"
#include "util_crash_files_db.h"
#include "util_simd.h"

static constexpr int padding_y = 32;
static constexpr int padding_x = 32;
static constexpr int padding_bar = 4;
static constexpr int name_width = 300;
static constexpr int success_width = 80;
static constexpr int title_height = 32;
static constexpr int row_height = 32;
static constexpr int widths[] = {name_width, success_width, 0};

const static auto long_text =
	u8"The Commodore 64, also known as the C64, C-64, C= 64, or occasionally CBM 64 or VIC-64, is an 8-bit home computer introduced in January 1982 by Commodore International. "
	u8"It is listed in the Guinness World Records as the highest-selling single computer model of all time, with independent estimates placing the number sold between 10 and 17 million units. "
	u8"Volume production started in early 1982, with machines being released on to the market in August at a price of US $595(roughly equivalent to $1, 500 in 2015)."
	u8"Preceded by the Commodore VIC - 20 and Commodore PET, the C64 takes its name from its 64 kilobytes(65, 536 bytes) of RAM, and has technologically superior sound and graphical specifications when compared to some earlier systems such as the Apple II and Atari 800, with multi - color sprites and a more advanced sound processor.";

static const auto test_files_folder = known_path(platform::known_folder::test_files_folder);
static constexpr sizei thumbnail_max_dimension = {256, 256};

class null_item_results_ui final : public df::status_i
{
public:
	void start_item(const std::u8string_view name) override
	{
	}


	void end_item(const std::u8string_view name, const item_status status) override
	{
	}

	bool has_failures() const override { return false; }

	void abort(const std::u8string_view error_message) override
	{
	}

	void complete(const std::u8string_view message) override
	{
	}

	void show_errors() override
	{
	}

	void message(const std::u8string_view message, int pos, int total) override
	{
	}

	void show_message(const std::u8string_view message) override
	{
	}

	bool is_canceled() const override { return false; }

	void wait_for_complete() const override
	{
	}
};


class null_state_strategy final : public state_strategy
{
public:
	explicit null_state_strategy() = default;

	void toggle_full_screen() override
	{
	}

	bool can_open_search(const df::search_t& path) override
	{
		return true;
	}

	void element_broadcast(const view_element_event& event) override
	{
	}

	void item_focus_changed(const df::item_element_ptr& focus, const df::item_element_ptr& previous) override
	{
	}

	void display_changed() override
	{
	}

	void view_changed(view_type m) override
	{
	}

	void play_state_changed(const bool play) override
	{
	}

	void search_complete(const df::search_t& path, bool path_changed) override
	{
	}

	void invoke(const commands id) override
	{
	}

	void make_visible(const df::item_element_ptr& i) override
	{
	}

	void command_hover(const ui::command_ptr& c, const recti window_bounds) override
	{
	}

	bool is_command_checked(const commands cmd) override
	{
		return false;
	}

	void focus_view() override
	{
	}

	void delete_items(const df::item_set& items) override
	{
	}

	void invalidate_view(const view_invalid invalid) override
	{
	}

	void track_menu(const ui::frame_ptr& parent, const recti bounds,
	                const std::vector<ui::command_ptr>& commands) override
	{
	}

	void free_graphics_resources(const bool items_only, const bool offscreen_only) override
	{
	}
};

class null_async_strategy : public async_strategy
{
public:
	location_cache locations;

	void queue_ui(std::function<void()> f) override
	{
		f();
	}

	void queue_media_preview(std::function<void(media_preview_state&)>) override
	{
	}

	void queue_database(const std::function<void(database&)> f) override
	{
	}

	void web_service_cache(std::u8string key, std::function<void(const std::u8string&)> f) override
	{
	}

	void web_service_cache(std::u8string key, std::u8string value) override
	{
	}

	void queue_async(async_queue q, std::function<void()> f) override
	{
		f();
	}

	void queue_location(std::function<void(location_cache&)> f) override
	{
		if (!locations.is_index_loaded()) locations.load_index();
		f(locations);
	}

	void invalidate_view(const view_invalid invalid) override
	{
	}
};


class temp_files
{
	df::folder_path _folder;
	df::file_paths _paths;

public:
	temp_files() : _folder(platform::temp_folder().combine(str::format(u8"diffractor-{}"sv, platform::tick_count())))
	{
	}

	~temp_files()
	{
		delete_temps();
	}

	temp_files(const temp_files& other) noexcept = delete;
	temp_files(temp_files&& other) noexcept = delete;
	temp_files& operator=(const temp_files& other) = delete;
	temp_files& operator=(temp_files&& other) noexcept = delete;

	void delete_temps()
	{
		df::file_paths paths;
		std::swap(paths, _paths);

		for (const auto& path : paths)
		{
			platform::delete_file(path);
		}
	}

	df::folder_path folder() const
	{
		return _folder;
	}

	df::file_path next_path(const std::u8string_view ext = u8".jpg"sv)
	{
		if (!_folder.exists())
		{
			platform::create_folder(_folder);
		}

		auto result = platform::temp_file(ext, _folder);
		_paths.emplace_back(result);
		return result;
	}
};

static temp_files _temps;

static df::index_file_item make_index_file_info(const df::date_t date)
{
	df::index_file_item result;
	result.file_modified = date;
	result.file_created = date;
	result.ft = files::file_type_from_name(u8"test.jpg"sv);
	result.safe_ps();
	return result;
}

static std::atomic_int test_version = 0;
static df::cancel_token test_token(test_version);
constexpr int expected_cached_item_count = 34;

static df::file_item_ptr load_item(index_state& index, const df::file_path path, bool load_thumb)
{
	auto i = std::make_shared<df::file_item>(path, index.find_item(path));
	index.scan_item(i, load_thumb, false);
	return i;
}


void test_view::test_element::render(ui::draw_context& dc, const pointi element_offset) const
{
	const auto bg_alpha = dc.colors.alpha * dc.colors.bg_alpha;
	const auto text_color = ui::color(dc.colors.foreground, dc.colors.alpha);
	auto state = u8"Unknown"sv;
	ui::color bg;

	switch (_state)
	{
	case test_state::Success:
		bg = ui::color(ui::style::color::info_background, bg_alpha);
		state = u8"Success"sv;
		break;
	case test_state::Failed:
		bg = ui::color(ui::style::color::warning_background, bg_alpha);
		state = u8"Failed"sv;
		break;
	case test_state::Running:
		bg = ui::color(ui::style::color::important_background, bg_alpha);
		state = u8"Running"sv;
		break;
	case test_state::Unknown:
		bg = ui::color(ui::style::color::view_background, bg_alpha / 2.0f);
		state = u8"Unknown"sv;
		break;
	}

	const auto logical_bounds = bounds.offset(element_offset);
	dc.draw_rect(logical_bounds, bg);

	auto x = logical_bounds.left;

	const recti rectName(x + 8, logical_bounds.top, x + widths[0], logical_bounds.bottom);
	dc.draw_text(_name, rectName, ui::style::font_size::dialog,
	             ui::style::text_style::single_line, text_color, {});

	x += widths[0];
	const recti rectSuccess(x + 8, logical_bounds.top, x + widths[1], logical_bounds.bottom);
	dc.draw_text(state, rectSuccess, ui::style::font_size::dialog, ui::style::text_style::single_line,
	             text_color, {});

	x += widths[1];
	const recti rectMessage(x + 8, logical_bounds.top, logical_bounds.right, logical_bounds.bottom);

	auto message = _message;

	if (message.empty() && _time > 0)
	{
		message = str::format(u8"{:8} milliseconds"sv, _time);
	}

	dc.draw_text(message, rectMessage, ui::style::font_size::dialog,
	             ui::style::text_style::single_line, text_color, {});
}

sizei test_view::test_element::measure(ui::measure_context& mc, const int width_limit) const
{
	return {width_limit, row_height};
}

void test_view::test_element::dispatch_event(const view_element_event& event)
{
	if (event.type == view_element_event_type::invoke)
	{
		_view.run_test(shared_from_this());
	}
}

view_controller_ptr test_view::test_element::controller_from_location(const view_host_ptr& host, const pointi loc,
                                                                      const pointi element_offset,
                                                                      const std::vector<recti>& excluded_bounds)
{
	return default_controller_from_location(*this, host, loc, element_offset, excluded_bounds);
}

test_view::test_view(view_state& state, view_host_ptr host) : _state(state), _host(std::move(host))
{
}

test_view::~test_view()
{
	_tests.clear();
}

void test_view::activate(const sizei extent)
{
	_extent = extent;

	if (_tests.empty())
	{
		register_tests();
	}

	refresh();
}


void test_view::refresh()
{
	_host->frame()->layout();
	run_tests();
}

void test_view::clear()
{
	_scroller.reset();
	sort();
	_host->frame()->invalidate();
}


void test_view::render_headers(ui::draw_context& dc, int x) const
{
	static constexpr std::u8string_view titles[] = {u8"Name"sv, u8"Success"sv, u8"Message"};

	constexpr int cy = title_height;
	constexpr int y = 0;
	const auto bg_alpha = dc.colors.alpha * dc.colors.bg_alpha;
	const auto text_color = ui::color(dc.colors.foreground, dc.colors.alpha);

	dc.draw_rect(recti(0, y, _extent.cx, y + cy), ui::color(0, bg_alpha));

	for (auto i = 0u; i < std::size(widths); ++i)
	{
		auto cx = widths[i];

		if (cx == 0)
		{
			cx = _extent.cx - x;
		}

		const recti r(x, y, x + cx, y + cy);
		dc.draw_rect(r, ui::color(dc.colors.background, bg_alpha));
		dc.draw_text(titles[i], r, ui::style::font_size::dialog,
		             ui::style::text_style::single_line_center, text_color, {});

		x += cx;
	}
}

void test_view::render(ui::draw_context& rc, view_controller_ptr controller)
{
	constexpr auto left = 8;
	const auto offset = -_scroller.scroll_offset();

	for (const auto& i : _tests)
	{
		i->render(rc, offset);
	}

	render_headers(rc, left);

	if (!_scroller._active)
	{
		_scroller.draw_scroll(rc);
	}
}


void test_view::layout(ui::measure_context& mc, const sizei extent)
{
	_extent = extent;

	const recti scroll_bounds{_extent.cx - view_scroller::def_width, 0, _extent.cx, _extent.cy};
	const recti client_bounds{0, 0, _extent.cx - view_scroller::def_width, _extent.cy};

	auto y_max = 0;

	if (!_tests.empty())
	{
		auto y = (padding_y * 2);

		for (const auto& i : _tests)
		{
			i->bounds.set(client_bounds.left, y, client_bounds.right, y + row_height);
			y += row_height + padding_bar;
		}

		y_max = y + padding_y;
	}

	_scroller.layout({client_bounds.width(), y_max}, client_bounds, scroll_bounds);
	_host->frame()->invalidate();
}

test_view::test_ptr test_view::test_from_location(const int y) const
{
	test_ptr result;
	const auto offset = _scroller.scroll_offset();
	int distance = 10000;

	for (const auto& i : _tests)
	{
		const auto yy = i->bounds.top + (row_height / 2);
		const auto d = abs(yy - y - offset.y);

		if (d < distance)
		{
			distance = d;
			result = i;
		}
	}

	return result;
}

view_controller_ptr test_view::controller_from_location(const view_host_ptr& host, const pointi loc)
{
	if (_scroller.scroll_bounds().contains(loc))
	{
		return std::make_shared<scroll_controller>(host, _scroller, _scroller.scroll_bounds());
	}

	const auto test = test_from_location(loc.y);

	if (test)
	{
		const auto test_offset = -_scroller.scroll_offset();
		return std::make_shared<clickable_controller>(host, test, test_offset);
	}

	return nullptr;
}

void test_view::mouse_wheel(const pointi loc, const int zDelta, const ui::key_state keys)
{
	_scroller.offset(_host, 0, -zDelta);
	_state.invalidate_view(view_invalid::controller);
}

void test_view::sort()
{
	df::assert_true(ui::is_ui_thread());

	std::ranges::sort(_tests, [](auto&& l, auto&& r) -> bool
	{
		if (l->has_failed() != r->has_failed()) return l->has_failed();
		return str::icmp(l->_name, r->_name) < 0;
	});

	_host->frame()->layout();
}

static file_scan_result ff_scan_file(files& ff, const df::file_path path, const std::u8string_view xmp_sidecar = {})
{
	const auto* const ft = files::file_type_from_name(path);
	return ff.scan_file(path, false, ft, xmp_sidecar, {});
}

static file_scan_result ff_scan_and_load_thumb(files& ff, const df::file_path path,
                                               const std::u8string_view xmp_sidecar = {})
{
	const auto* const ft = files::file_type_from_name(path);
	return ff.scan_file(path, true, ft, xmp_sidecar, thumbnail_max_dimension);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

struct metadata_type
{
	static constexpr uint32_t EXIF = 1;
	static constexpr uint32_t IPTC = 2;
	static constexpr uint32_t XMP = 4;
	static constexpr uint32_t ALL = EXIF | IPTC | XMP;
};


static void assert_equal(const std::u8string_view expected, const std::u8string_view actual,
                         const std::u8string_view name = {}, const std::u8string_view message = {})
{
	if (str::icmp(actual, expected) != 0)
	{
		throw test_assert_exception(str::format(u8"{} - {}: expected '{}', got '{}'"sv, message, name, expected, actual));
	}
}

static void assert_not_equal(const std::u8string_view expected, const std::u8string_view actual,
                             const std::u8string_view name = {}, const std::u8string_view message = {})
{
	if (str::icmp(actual, expected) == 0)
	{
		throw test_assert_exception(str::format(u8"{} - {}: expected '{}' not to equal '{}'"sv, message, name, expected,
		                                        actual));
	}
}

static void assert_equal_strict(const std::u8string_view expected, const std::u8string_view actual,
                                const std::u8string_view name = {}, const std::u8string_view message = {})
{
	if (actual != expected)
	{
		throw test_assert_exception(str::format(u8"{} - {}: expected '{}', got '{}'"sv, message, name, expected, actual));
	}
}

static void assert_equal(const std::wstring_view expected, const std::wstring_view actual,
                         const std::u8string_view name = {}, const std::u8string_view message = {})
{
	assert_equal(str::utf16_to_utf8(expected), str::utf16_to_utf8(actual), name, message);
}

static void assert_equal(int expected, int actual, const std::u8string_view name = {},
                         const std::u8string_view message = {})
{
	assert_equal(str::to_string(expected), str::to_string(actual), name, message);
}

static void assert_equal(uint32_t expected, uint32_t actual, const std::u8string_view name = {},
                         const std::u8string_view message = {})
{
	assert_equal(str::to_string(expected), str::to_string(actual), name, message);
}

static void assert_equal(uint64_t expected, uint64_t actual, const std::u8string_view name = {},
	const std::u8string_view message = {})
{
	assert_equal(str::to_string(expected), str::to_string(actual), name, message);
}

static void assert_equal(const ui::const_image_ptr& expected, const ui::const_image_ptr& actual,
                         const std::u8string_view name = {}, const std::u8string_view message = {})
{
	assert_equal(static_cast<int>(expected->width()), static_cast<int>(actual->width()), name, message);

	files ff;
	const auto diff = ff.pixel_difference(expected, actual);
	assert_equal(true, diff == ui::pixel_difference_result::equal, name, message);
}

static void assert_equal(bool expected, bool actual, const std::u8string_view name = {},
                         const std::u8string_view message = {})
{
	assert_equal(str::to_string(expected), str::to_string(actual), name, message);
}

static void assert_equal(ui::orientation expected, ui::orientation actual, const std::u8string_view name = {},
                         const std::u8string_view message = {})
{
	assert_equal(orientation_to_string(expected), orientation_to_string(actual), name, message);
}

static void assert_equal(df::search_term_modifier_bool expected, df::search_term_modifier_bool actual,
                         const std::u8string_view name = {}, const std::u8string_view message = {})
{
	assert_equal(static_cast<int>(expected), static_cast<int>(actual), name, message);
}

static void assert_equal(double expected, double actual, const std::u8string_view name = {},
                         const std::u8string_view message = {})
{
	assert_equal(str::to_string(expected, 5), str::to_string(actual, 5), name, message);
}

static void assert_equal(df::date_t expected, df::date_t actual, const std::u8string_view name = {},
                         const std::u8string_view message = {})
{
	assert_equal(platform::format_date_time(expected), platform::format_date_time(actual), name, message);
}

static void assert_equal(gps_coordinate expected, gps_coordinate actual, const std::u8string_view name = {},
                         const std::u8string_view message = {})
{
	assert_equal(gps_coordinate::decimal_to_dms_str(expected.latitude(), true),
	             gps_coordinate::decimal_to_dms_str(actual.latitude(), true), name, message);
	assert_equal(gps_coordinate::decimal_to_dms_str(expected.longitude(), false),
	             gps_coordinate::decimal_to_dms_str(actual.longitude(), false), name, message);
}

static void assert_equal(df::xy8 expected, df::xy8 actual, const std::u8string_view name = {},
                         const std::u8string_view message = {})
{
	assert_equal(expected.str(), actual.str(), name, message);
}


void assert_metadata(const prop::item_metadata& expected, const prop::item_metadata& actual,
                     const std::u8string_view message = {})
{
	assert_equal(expected.album, actual.album, u8"album"sv, message);
	assert_equal(expected.album_artist, actual.album_artist, u8"album_artist"sv, message);
	assert_equal(expected.artist, actual.artist, u8"artist"sv, message);
	assert_equal(expected.audio_codec, actual.audio_codec, u8"audio_codec"sv, message);
	assert_equal(expected.audio_sample_rate, actual.audio_sample_rate, u8"audio_sample_rate"sv, message);
	assert_equal(expected.audio_sample_type, actual.audio_sample_type, u8"audio_sample_type"sv, message);
	assert_equal(expected.audio_channels, actual.audio_channels, u8"audio_channels"sv, message);
	assert_equal(expected.bitrate, actual.bitrate, u8"bitrate"sv, message);
	assert_equal(expected.camera_manufacturer, actual.camera_manufacturer, u8"camera_manufacturer"sv, message);
	assert_equal(expected.camera_model, actual.camera_model, u8"camera_model"sv, message);
	assert_equal(expected.comment, actual.comment, u8"comment"sv, message);
	assert_equal(expected.composer, actual.composer, u8"composer"sv, message);
	assert_equal(expected.coordinate, actual.coordinate, u8"coordinate"sv, message);
	assert_equal(expected.copyright_creator, actual.copyright_creator, u8"copyright_creator"sv, message);
	assert_equal(expected.copyright_credit, actual.copyright_credit, u8"copyright_credit"sv, message);
	assert_equal(expected.copyright_notice, actual.copyright_notice, u8"copyright_notice"sv, message);
	assert_equal(expected.copyright_source, actual.copyright_source, u8"copyright_source"sv, message);
	assert_equal(expected.copyright_url, actual.copyright_url, u8"copyright_url"sv, message);
	assert_equal(expected.created_digitized, actual.created_digitized, u8"created_digitized"sv, message);
	assert_equal(expected.created_exif, actual.created_exif, u8"created_exif"sv, message);
	assert_equal(expected.created_utc, actual.created_utc, u8"created_utc"sv, message);
	assert_equal(expected.description, actual.description, u8"description"sv, message);
	assert_equal(expected.width, actual.width, u8"width"sv, message);
	assert_equal(expected.height, actual.height, u8"height"sv, message);
	assert_equal(expected.disk, actual.disk, u8"disk"sv, message);
	assert_equal(expected.duration, actual.duration, u8"duration"sv, message);
	assert_equal(expected.encoder, actual.encoder, u8"encoder"sv, message);
	assert_equal(expected.episode, actual.episode, u8"episode"sv, message);
	assert_equal(prop::format_exposure(expected.exposure_time), prop::format_exposure(actual.exposure_time),
	             u8"exposure_time"sv, message);
	assert_equal(prop::format_f_num(expected.f_number), prop::format_f_num(actual.f_number), u8"f_number"sv, message);
	assert_equal(expected.file_name, actual.file_name, u8"file_name"sv, message);
	assert_equal(expected.focal_length, actual.focal_length, u8"focal_length"sv, message);
	assert_equal(expected.focal_length_35mm_equivalent, actual.focal_length_35mm_equivalent,
	             u8"focal_length_35mm_equivalent"sv, message);
	assert_equal(expected.genre, actual.genre, u8"genre"sv, message);
	assert_equal(expected.iso_speed, actual.iso_speed, u8"iso_speed"sv, message);
	assert_equal(expected.lens, actual.lens, u8"lens"sv, message);
	assert_equal(expected.location_place, actual.location_place, u8"location_city"sv, message);
	assert_equal(expected.location_country, actual.location_country, u8"location_country"sv, message);
	assert_equal(expected.location_state, actual.location_state, u8"location_state"sv, message);
	assert_equal(expected.orientation, actual.orientation, u8"orientation"sv, message);
	assert_equal(expected.performer, actual.performer, u8"performer"sv, message);
	//assert_equal(expected.pixel_format, actual.pixel_format, u8"pixel_format"sv, message);
	assert_equal(expected.publisher, actual.publisher, u8"publisher"sv, message);
	assert_equal(expected.rating, actual.rating, u8"rating"sv, message);
	assert_equal(expected.audio_sample_rate, actual.audio_sample_rate, u8"sample_rate"sv, message);
	assert_equal(expected.season, actual.season, u8"season"sv, message);
	assert_equal(expected.show, actual.show, u8"show"sv, message);
	assert_equal(expected.synopsis, actual.synopsis, u8"synopsis"sv, message);
	assert_equal(expected.tags, actual.tags, u8"tags"sv, message);
	assert_equal(expected.title, actual.title, u8"title"sv, message);
	assert_equal(expected.track, actual.track, u8"track"sv, message);
	assert_equal(expected.video_codec, actual.video_codec, u8"video_codec"sv, message);
	assert_equal(expected.year, actual.year, u8"year"sv, message);
}

static prop::item_metadata_ptr metadata_from_cache(index_state& index, const df::file_path path)
{
	const auto node = index.validate_folder(path.folder(), true, platform::now());
	node.folder->is_indexed = true;
	index.scan_item(node.folder, path, false, false, nullptr, files::file_type_from_name(path.name()));
	return index.find_item(path).metadata;
}

prop::item_metadata_ptr extract_properties(const df::file_path path, uint32_t t = metadata_type::ALL)
{
	auto result = std::make_shared<prop::item_metadata>();
	const auto data = blob_from_file(path);

	if (!data.empty())
	{
		mem_read_stream stream(data);

		if (stream.size() > 16)
		{
			const auto info = scan_photo(stream);

			if ((t & metadata_type::EXIF) && !info.metadata.exif.empty())
			{
				metadata_exif::parse(*result, info.metadata.exif);
			}

			if ((t & metadata_type::IPTC) && !info.metadata.iptc.empty())
			{
				metadata_iptc::parse(*result, info.metadata.iptc);
			}

			if ((t & metadata_type::XMP) && !info.metadata.xmp.empty())
			{
				metadata_xmp::parse(*result, info.metadata.xmp);
			}

			if (t == metadata_type::ALL)
			{
				result = info.to_props();
			}
		}
	}

	return result;
}

prop::item_metadata_ptr expected_test_jpg()
{
	auto result = std::make_shared<prop::item_metadata>();
	result->f_number = 6.3f;
	result->focal_length = 15.0f;
	result->camera_manufacturer = u8"Canon"_c;
	result->camera_model = u8"Canon EOS 7D"_c;
	result->coordinate = gps_coordinate(50.08806, 14.42083);
	result->copyright_notice = u8"Copyright"_c;
	result->created_digitized = df::date_t(2012, 9, 14, 19, 21, 14);
	result->created_exif = df::date_t(2012, 9, 14, 19, 21, 14);
	result->description = u8"Caption"_c;
	result->exposure_time = 1.0f / 100.0f;
	result->iso_speed = 100;
	result->lens = u8"EF-S15-85mm f/3.5-5.6 IS USM"_c;
	result->location_place = u8"Prague"_c;
	result->location_country = u8"Czech Republic"_c;
	result->location_state = u8"Hlavní Mesto Praha"_c;
	result->rating = 4;
	result->tags = u8"key1 key2 key3"_c;
	result->title = u8"Title"_c;
	result->width = 1024;
	result->height = 683;
	result->pixel_format = u8"YCbCr"_c;

	return result;
}


///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

static void should_replace_tokens()
{
	files ff;
	const auto image_md = ff_scan_file(ff, test_files_folder.combine_file(u8"Test.jpg"sv));
	const auto audio_md = ff_scan_file(ff, test_files_folder.combine_file(u8"Colorblind.mp3"sv));

	assert_equal(u8"2012\\2012-09-14"sv, replace_tokens(u8"{year}\\{created}"s, image_md.to_props()));
	assert_equal(u8"Counting Crows\\This Desert Life"sv, replace_tokens(u8"{artist}\\{album}"s, audio_md.to_props()));
}

static void should_scan_jpeg()
{
	const auto load_path = test_files_folder.combine_file(u8"Test.jpg"sv);

	prop::item_metadata expected_exif;
	expected_exif.created_exif = df::date_t(2012, 9, 14);
	expected_exif.camera_manufacturer = u8"Canon"_c;
	expected_exif.camera_model = u8"Canon EOS 7D"_c;
	expected_exif.lens = u8"EF-S15-85mm f/3.5-5.6 IS USM"_c;
	expected_exif.description = u8"Caption"_c;
	expected_exif.coordinate = gps_coordinate(50.08806, 14.42083);
	expected_exif.copyright_notice = u8"Copyright"_c;
	expected_exif.f_number = 6.3f;
	expected_exif.exposure_time = 1.0f / 100.0f;
	expected_exif.iso_speed = 100;
	expected_exif.focal_length = 15.0f;
	expected_exif.created_digitized = df::date_t(2012, 9, 14, 19, 21, 14);
	expected_exif.created_exif = df::date_t(2012, 9, 14, 19, 21, 14);
	expected_exif.rating = 0;

	assert_metadata(expected_exif, *extract_properties(load_path, metadata_type::EXIF), u8"EXIF"sv);

	prop::item_metadata expected_iptc;
	expected_iptc.title = u8"Title"_c;
	expected_iptc.description = u8"Caption"_c;
	expected_iptc.tags = u8"key1 key2 key3"_c;
	expected_iptc.location_place = u8"Prague"_c;
	expected_iptc.location_state = u8"Hlavní Mesto Praha"_c;
	expected_iptc.location_country = u8"Czech Republic"_c;
	expected_iptc.copyright_notice = u8"Copyright"_c;
	expected_iptc.rating = 0;

	assert_metadata(expected_iptc, *extract_properties(load_path, metadata_type::IPTC), u8"IPTC"sv);

	const auto actual_xmp = extract_properties(load_path, metadata_type::XMP);

	prop::item_metadata expected_xmp;
	expected_xmp.created_exif = df::date_t(2012, 9, 14);
	expected_xmp.title = u8"Title"_c;
	expected_xmp.description = u8"Caption"_c;
	expected_xmp.tags = u8"key1 key2 key3"_c;
	expected_xmp.location_place = u8"Prague"_c;
	expected_xmp.location_state = u8"Hlavní Mesto Praha"_c;
	expected_xmp.location_country = u8"Czech Republic"_c;
	expected_xmp.lens = u8"EF-S15-85mm f/3.5-5.6 IS USM"_c;
	expected_xmp.copyright_notice = u8"Copyright"_c;
	expected_xmp.rating = 4;
	expected_xmp.created_digitized = df::date_t(2012, 9, 14, 19, 21, 14);
	expected_xmp.created_exif = df::date_t(2012, 9, 14, 19, 21, 14);

	assert_metadata(expected_xmp, *actual_xmp, u8"XMP"sv);

	const auto actual_all = extract_properties(load_path, metadata_type::ALL);
	assert_metadata(*expected_test_jpg(), *actual_all, u8"all"sv);
}

static void should_parse_xmp()
{
	const auto load_path = test_files_folder.combine_file(u8"IMG_0604.xmp"sv);

	prop::item_metadata actual;
	metadata_xmp::parse(actual, load_path);

	assert_equal(u8"Flower"sv, actual.title);
	assert_equal(u8"Blomst"sv, actual.tags);
	assert_equal(u8"Følfod ( Tussilago farfara ) Lægeurt"sv, actual.description);
	assert_equal(u8"Frank Aalestrup www.fdaa.dk"sv, actual.copyright_notice);
	assert_equal(u8"\"Frank Aalestrup.\nwww.fdaa.dk\""sv, actual.copyright_creator);
	assert_equal(u8"EF100mm f/2.8L Macro IS USM"sv, actual.lens);
	assert_equal({56.19283, 9.88415}, actual.coordinate);
	assert_equal(u8"Canon"sv, actual.camera_manufacturer);
	assert_equal(u8"Canon EOS 50D"sv, actual.camera_model);
	assert_equal(u8"IMG_0604.CR2"sv, actual.raw_file_name);
	//assert_equal(u8"Toustrup Mark"sv, actual.location_city);
	//assert_equal(u8"New York"sv, actual.location_state);
	assert_equal(u8"Denmark"sv, actual.location_country);
}

static void should_merge_xmp_sidecar()
{
	const auto cr2_path = test_files_folder.combine_file(u8"Gherkin.CR2"sv);
	const auto xmp_path = test_files_folder.combine_file(u8"Gherkin.XMP"sv);

	const auto actual = extract_properties(cr2_path);

	metadata_xmp::parse(*actual, xmp_path);

	assert_equal(u8"Canon"sv, actual->camera_manufacturer);
	assert_equal(u8"United Kingdom"sv, actual->location_country);
	assert_equal(u8"\xA9 Mark Ridgwell"sv, actual->copyright_notice);
	assert_equal(u8"\"Mark Ridgwell\""sv, actual->copyright_creator);
}

static void should_save(const std::u8string_view ext, bool should_support_metadata)
{
	const auto save_path = _temps.next_path(ext);
	const auto load_path = test_files_folder.combine_file(u8"Test.jpg"sv);

	files ff;
	const image_edits color;
	ff.update(load_path, save_path, {}, color, {}, false, {});

	const auto expected = extract_properties(load_path);
	const auto actual = extract_properties(save_path);


	assert_equal(expected->width, actual->width);
	assert_equal(expected->height, actual->height);

	if (should_support_metadata)
	{
		assert_metadata(*expected, *actual, save_path.name());

		//assert_equal(expected->created_exif, actual->created_exif);
		//assert_equal(expected->title, actual->title);
		//assert_equal(expected->comment, actual->comment);
	}
}

static void should_scan_mp3()
{
	const auto load_path = test_files_folder.combine_file(u8"Colorblind.mp3"sv);

	ui::const_surface_ptr thumb;
	ui::const_image_ptr thumbScan;
	ui::const_image_ptr image;

	files ff;
	const auto actual = ff_scan_file(ff, load_path);

	prop::item_metadata expected;
	expected.artist = u8"Counting Crows"_c;
	expected.album_artist = u8"Counting Crows"_c;
	expected.title = u8"Colorblind"_c;
	expected.album = u8"This Desert Life"_c;
	expected.comment = u8"Comments"_c;
	expected.composer = u8"Adam Duritz/Charlie Gillingham"_c;
	expected.publisher = u8"Interscope"_c;
	expected.rating = 5;
	expected.genre = u8"Rock"_c;
	expected.duration = 10;
	expected.audio_sample_rate = 22050;
	expected.audio_sample_type = 35;
	expected.audio_channels = 2;
	expected.audio_codec = u8"mp3float"_c;
	expected.track.x = 7;
	expected.year = 1999;

	assert_metadata(expected, *actual.to_props(), u8"Colorblind.mp3"sv);

	const auto load_path2 = test_files_folder.combine_file(u8"Games Without Frontiers.mp3"sv);
	const auto actual2 = ff_scan_file(ff, load_path2);

	prop::item_metadata expected2;
	expected2.artist = u8"Peter Gabriel"_c;
	expected2.album = u8"Hit"_c;
	expected2.title = u8"Games Without Frontiers"_c;
	expected2.year = 2003;
	expected2.genre = u8"Rock"_c;
	expected2.duration = 10;
	expected2.audio_sample_rate = 44100;
	expected2.audio_sample_type = 35;
	expected2.audio_channels = 2;
	expected2.track.x = 5;
	expected2.disk.x = 1;
	expected2.encoder = u8"Lavf51.12.1"_c;
	expected2.audio_codec = u8"mp3float"_c;

	assert_metadata(expected2, *actual2.to_props(), u8"Games Without Frontiers.mp3"sv);

	const auto load_path3 = test_files_folder.combine_file(u8"Is It Any Wonder.mp3"sv);
	const auto actual3 = ff_scan_file(ff, load_path3);

	prop::item_metadata expected3;
	expected3.artist = u8"Keane"_c;
	expected3.album = u8"Under The Iron Sea"_c;
	expected3.title = u8"Is It Any Wonder?"_c;
	expected3.year = 2006;
	expected3.genre = u8"Rock"_c;
	expected3.duration = 10;
	expected3.audio_sample_rate = 44100;
	expected3.audio_sample_type = 35;
	expected3.audio_channels = 2;
	expected3.track.x = 2;
	expected3.audio_codec = u8"mp3float"_c;
	expected3.created_utc = df::date_t(2006, 6, 20, 0, 0, 0);
	expected3.created_digitized = df::date_t(2006, 6, 20, 0, 0, 0);
	expected3.publisher = u8"Interscope"_c;

	assert_metadata(expected3, *actual3.to_props(), u8"Is It Any Wonder.mp3"sv);
}

static void should_scan_mp4()
{
	const auto load_path = test_files_folder.combine_file(u8"gizmo.mp4"sv);

	files ff;
	const auto actual = ff_scan_file(ff, load_path);

	prop::item_metadata expected;
	expected.title = u8"Title xxx"_c;
	expected.comment = u8"Comments xxx"_c;
	expected.tags = u8"gadget test"_c;
	expected.audio_codec = u8"aac"_c;
	expected.audio_sample_rate = 48000;
	expected.audio_sample_type = 35;
	expected.audio_channels = 1;
	expected.duration = 6;
	expected.width = 560;
	expected.height = 320;
	expected.created_utc = df::date_t(2010, 3, 20, 21, 29, 11);
	expected.created_digitized = df::date_t(2010, 3, 20, 21, 29, 11);
	expected.year = 2010;
	expected.video_codec = u8"h264"_c;
	expected.encoder = u8"HandBrake 0.9.4 2009112300"_c;
	expected.pixel_format = u8"yuv420p"_c;

	assert_metadata(expected, *actual.to_props(), u8"gizmo.mp4"sv);

	const auto load_path2 = test_files_folder.combine_file(u8"This Year's Love.m4a"sv);
	const auto actual2 = ff_scan_file(ff, load_path2);

	prop::item_metadata expected2;
	expected2.title = u8"This Year's Love"_c;
	expected2.artist = u8"David Gray"_c;
	expected2.album_artist = u8"David Gray"_c;
	expected2.composer = u8"David Gray"_c;
	expected2.album = u8"David Gray: Greatest Hits"_c;
	expected2.copyright_notice = u8"\u2117 2007 Iht Records Ltd under exclusive licence to Warner Music UK Ltd"_c;
	expected2.created_utc = df::date_t(2007, 11, 9, 8, 0, 0);
	expected2.created_digitized = df::date_t(2007, 11, 9, 8, 0, 0);
	expected2.genre = u8"Pop"_c;
	expected2.duration = 10;
	expected2.audio_codec = u8"aac"_c;
	expected2.audio_sample_rate = 44100;
	expected2.audio_sample_type = 35;
	expected2.audio_channels = 2;
	expected2.disk.x = 1;
	expected2.track = {7, 0};
	expected2.width = 0;
	expected2.height = 0;
	expected2.disk = {0, 0};
	expected2.year = 2007;
	expected2.encoder = u8"Lavf54.63.100"_c;

	assert_metadata(expected2, *actual2.to_props(), u8"This Year's Love.m4a"sv);
	assert_equal(true, is_empty(actual2.thumbnail_image), u8"m4a scan thumbnail"sv);

	const auto loaded = ff_scan_and_load_thumb(ff, load_path2);
	assert_equal(false, loaded.success && is_valid(loaded.thumbnail_surface), u8"m4a load thumbnail"sv);
}

static void should_scan_mov()
{
	const auto load_path = test_files_folder.combine_file(u8"ipod.mov"sv);

	files ff;
	const auto actual = ff_scan_file(ff, load_path);

	prop::item_metadata expected;
	expected.title = u8"iPad Help"_c;
	expected.tags = u8"apple ipad ipod support"_c;
	expected.comment = u8"What to do if you ipad dies"_c;
	expected.audio_codec = u8"aac"_c;
	expected.audio_sample_rate = 32000;
	expected.audio_sample_type = 35;
	expected.audio_channels = 1;
	expected.pixel_format = u8"yuv420p"_c;
	expected.duration = 86;
	expected.width = 640;
	expected.height = 480;
	expected.video_codec = u8"mpeg4"_c;
	expected.created_digitized = df::date_t(2005, 10, 17, 22, 54, 32);
	expected.created_utc = df::date_t(2005, 10, 17, 22, 54, 32);
	expected.year = 2005;

	assert_metadata(expected, *actual.to_props(), u8"ipod.mov"sv);

	const auto load_path2 = test_files_folder.combine_file(u8"StPauls.MOV"sv);
	const auto actual2 = ff_scan_file(ff, load_path2);

	prop::item_metadata expected2;
	expected2.created_utc = df::date_t(2011, 3, 13, 15, 13, 49);
	expected2.created_digitized = df::date_t(2011, 3, 13, 15, 13, 49);
	expected2.audio_codec = u8"aac"_c;
	expected2.coordinate = {51.51420, -0.09850};
	expected2.width = 640;
	expected2.height = 480;
	expected2.duration = 10;
	expected2.audio_sample_rate = 44100;
	expected2.audio_sample_type = 35;
	expected2.audio_channels = 1;
	expected2.video_codec = u8"h264"_c;
	expected2.camera_manufacturer = u8"Apple"_c;
	expected2.camera_model = u8"iPhone 3GS"_c;
	expected2.encoder = u8"4.3"_c;
	expected2.pixel_format = u8"yuv420p"_c;
	expected2.year = 2011;

	assert_metadata(expected2, *actual2.to_props(), u8"StPauls.MOV"sv);
}

static void should_scan_avi()
{
	const auto load_path = test_files_folder.combine_file(u8"Byzantium.avi"sv);

	files ff;
	const auto actual = ff_scan_file(ff, load_path);

	prop::item_metadata expected;
	expected.audio_codec = u8"wmav2"_c;
	expected.title = u8"Byzantium"_c;
	expected.comment =
		u8"John Romer recreates the glory and history of Byzantium. From the Hagia Sophia in present-day Istanbul to the looted treasures of the empire now located in St. Marks in Venice."_c;
	expected.audio_sample_rate = 48000;
	expected.audio_sample_type = 35;
	expected.audio_channels = 2;
	expected.duration = 12;
	expected.width = 854;
	expected.height = 480;
	expected.video_codec = u8"wmv3"_c;
	expected.tags = u8"Byzantium History Turkey"_c;
	expected.pixel_format = u8"yuv420p"_c;

	assert_metadata(expected, *actual.to_props(), u8"Byzantium.avi"sv);
}

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


std::u8string_view detect_xmp_sidecar(const df::file_path path)
{
	if (!files::is_raw(path)) return {};
	const auto xmp_path = path.extension(u8".xmp"sv);
	return xmp_path.exists() ? xmp_path.name() : std::u8string_view{};
}

static void should_update_rating(const std::u8string_view name)
{
	files ff;

	const auto ext = name.substr(df::find_ext(name));
	const auto save_path = _temps.next_path(ext);
	const auto load_path = test_files_folder.combine_file(name);

	metadata_edits edits1;
	edits1.rating = 3;

	ff.update(load_path, save_path, edits1, {}, {}, false, {});

	{
		const auto actual_scanned = ff_scan_file(ff, save_path, detect_xmp_sidecar(save_path));
		const auto ps = actual_scanned.to_props();
		assert_equal(3, ps->rating, u8"rating"sv);
	}

	metadata_edits edits2;
	edits2.remove_rating = true;
	ff.update(save_path, edits2, {}, {}, false, {});

	{
		const auto actual_scanned = ff_scan_file(ff, save_path, detect_xmp_sidecar(save_path));
		const auto ps = actual_scanned.to_props();
		assert_equal(0, ps->rating, u8"rating"sv);
	}
}

static void should_update_exif_rating()
{
	const auto load_path = test_files_folder.combine_file(u8"exif-rating.jpg"sv);
	const auto save_path = _temps.next_path(u8".jpg"sv);

	files ff;
	metadata_edits edits1;
	edits1.rating = 3;

	ff.update(load_path, save_path, edits1, {}, {}, false, {});

	{
		const auto actual_scanned = ff_scan_file(ff, save_path);
		const auto ps = actual_scanned.to_props();
		assert_equal(3, ps->rating, u8"to_props"sv);

		const auto actual_exif = extract_properties(save_path, metadata_type::EXIF);
		const auto actual_iptc = extract_properties(save_path, metadata_type::IPTC);
		const auto actual_xmp = extract_properties(save_path, metadata_type::XMP);

		assert_equal(3, actual_xmp->rating, u8"XMP"sv);
		assert_equal(3, actual_exif->rating, u8"exif"sv);
		//assert_equal(3, actual_iptc.rating, u8"IPC"sv);
	}

	metadata_edits edits2;
	edits2.remove_rating = true;
	ff.update(save_path, edits2, {}, {}, false, {});

	{
		const auto actual_scanned = ff_scan_file(ff, save_path);
		assert_equal(0, actual_scanned.to_props()->rating);

		const auto actual_exif = extract_properties(save_path, metadata_type::EXIF);
		const auto actual_iptc = extract_properties(save_path, metadata_type::IPTC);
		const auto actual_xmp = extract_properties(save_path, metadata_type::XMP);

		assert_equal(0, actual_xmp->rating, u8"XMP"sv);
		assert_equal(0, actual_exif->rating, u8"exif"sv);
		assert_equal(0, actual_iptc->rating, u8"IPC"sv);
	}
}

static void should_update_formatted_text()
{
	const auto load_path = test_files_folder.combine_file(u8"exif-rating.jpg"sv);
	const auto save_path = _temps.next_path(u8".jpg"sv);
	const auto desc_text = u8"a\tb\nc"sv;

	files ff;
	metadata_edits edits1;
	edits1.description = desc_text;

	ff.update(load_path, save_path, edits1, {}, {}, false, {});

	{
		const auto actual_scanned = ff_scan_file(ff, save_path);
		const auto ps = actual_scanned.to_props();
		assert_equal(desc_text, ps->description, u8"to_props"sv);

		const auto actual_exif = extract_properties(save_path, metadata_type::EXIF);
		const auto actual_iptc = extract_properties(save_path, metadata_type::IPTC);
		const auto actual_xmp = extract_properties(save_path, metadata_type::XMP);

		assert_equal(desc_text, actual_xmp->description, u8"XMP"sv);
		assert_equal(desc_text, actual_exif->description, u8"exif"sv);
		assert_equal(desc_text, actual_iptc->description, u8"IPC"sv);
	}
}

static str::cached make_unique_tags(tag_set tags1, const tag_set& tags2)
{
	tags1.add(tags2);
	tags1.make_unique();
	return str::cache(tags1.to_string());
}

static void should_update_metadata(const std::u8string_view name)
{
	files ff;

	const auto ext = name.substr(df::find_ext(name));
	const auto save_path = _temps.next_path(ext);
	const auto load_path = test_files_folder.combine_file(name);

	const auto tags_to_add = tag_set(u8"key1 key2 key3"sv);

	metadata_edits metadata_edits;
	metadata_edits.add_tags = tags_to_add;
	metadata_edits.copyright_notice = u8"Copyright xx"sv;
	metadata_edits.rating = 3;
	metadata_edits.title = u8"Title xx"sv;
	metadata_edits.description = u8"Description xx"sv;

	ff.update(load_path, save_path, metadata_edits, {}, {}, false, {});

	const auto sr_expected = ff_scan_file(ff, load_path, detect_xmp_sidecar(load_path));
	const auto sr_actual = ff_scan_file(ff, save_path, detect_xmp_sidecar(save_path));

	const auto expected = sr_expected.to_props();
	const auto actual = sr_actual.to_props();

	expected->title = u8"Title xx"_c;
	expected->copyright_notice = u8"Copyright xx"_c;
	expected->description = u8"Description xx"_c;
	expected->rating = 3;
	expected->tags = make_unique_tags(tag_set(expected->tags), tags_to_add);

	assert_metadata(*expected, *actual);
}

static void should_add_remove_tags(const std::u8string_view name)
{
	const auto ext = name.substr(df::find_ext(name));
	const auto save_path = _temps.next_path(ext);
	const auto load_path = test_files_folder.combine_file(name);

	const auto tags_to_add = tag_set(u8"Максим yyyy zzzz"sv);
	const auto tags_to_remove = tag_set(u8"Максим zzzz"sv);

	metadata_edits edits1;
	edits1.add_tags = tags_to_add;

	files ff;
	ff.update(load_path, save_path, edits1, {}, {}, false, {});

	const auto sr_expected = ff_scan_file(ff, load_path, detect_xmp_sidecar(load_path));
	const auto sr_actual = ff_scan_file(ff, save_path, detect_xmp_sidecar(save_path));

	const auto expected = sr_expected.to_props();
	const auto actual = sr_actual.to_props();

	expected->tags = make_unique_tags(tag_set(expected->tags), tags_to_add);

	assert_metadata(*expected, *actual, u8"added"sv);

	metadata_edits edits2;
	edits2.remove_tags = tags_to_remove;
	ff.update(save_path, edits2, {}, {}, false, {});

	tag_set expected_tags(expected->tags);
	expected_tags.remove(tags_to_remove);
	expected->tags = make_unique_tags(expected_tags, {});

	const auto sr_actual2 = ff_scan_file(ff, save_path, detect_xmp_sidecar(save_path));
	const auto actual2 = sr_actual2.to_props();
	assert_metadata(*expected, *actual2, u8"removed"sv);
}

static void should_update_location(const std::u8string_view name)
{
	files ff;

	const auto ext = name.substr(df::find_ext(name));
	const auto save_path = _temps.next_path(ext);
	const auto load_path = test_files_folder.combine_file(name);
	const auto coordinate = gps_coordinate(40.71417, -74.00611);

	metadata_edits metadata_edits;
	metadata_edits.location_coordinate = coordinate;
	metadata_edits.location_place = u8"Big Apple"_c;
	metadata_edits.location_state = u8"New York"_c;
	metadata_edits.location_country = u8"USA"_c;

	ff.update(load_path, save_path, metadata_edits, {}, {}, false, {});

	const auto sr_expected = ff_scan_file(ff, load_path, detect_xmp_sidecar(load_path));
	const auto sr_actual = ff_scan_file(ff, save_path, detect_xmp_sidecar(save_path));

	const auto expected = sr_expected.to_props();
	const auto actual = sr_actual.to_props();

	expected->coordinate = coordinate;
	expected->location_place = u8"Big Apple"_c;
	expected->location_state = u8"New York"_c;
	expected->location_country = u8"USA"_c;

	assert_metadata(*expected, *actual);
}


static void should_update_gps_in_exif()
{
	const auto save_path = _temps.next_path(u8".jpg"sv);
	const auto load_path = test_files_folder.combine_file(u8"IMG_9340.jpg"sv);
	const auto coordinate = gps_coordinate(40.71417, -74.00611);

	metadata_edits metadata_edits;
	metadata_edits.location_coordinate = coordinate;
	metadata_edits.location_place = u8"Big Apple"_c;
	metadata_edits.location_state = u8"New York"_c;
	metadata_edits.location_country = u8"USA"_c;

	files ff;
	ff.update(load_path, save_path, metadata_edits, {}, {}, false, {});

	const auto actual_exif = extract_properties(save_path, metadata_type::EXIF);
	const auto actual_iptc = extract_properties(save_path, metadata_type::IPTC);
	const auto actual_xmp = extract_properties(save_path, metadata_type::XMP);

	assert_equal(u8"Big Apple"sv, actual_iptc->location_place, u8"IPTC"sv);
	assert_equal(u8"New York"sv, actual_iptc->location_state, u8"IPTC"sv);
	assert_equal(u8"USA"sv, actual_iptc->location_country, u8"IPTC"sv);
	assert_equal({40.71417, -74.00611}, actual_exif->coordinate, u8"EXIF"sv);

	assert_equal(u8"Big Apple"sv, actual_xmp->location_place, u8"XMP"sv);
	assert_equal(u8"New York"sv, actual_xmp->location_state, u8"XMP"sv);
	assert_equal(u8"USA"sv, actual_xmp->location_country, u8"XMP"sv);

	// TODO
	//AssertEqual(u8"40.71417"sv, actual_xmp, Property::Latitude, u8"XMP"sv);
	//AssertEqual(u8"-74.00611"sv, actual_xmp, Property::Longitude, u8"XMP"sv);
}

static void should_handle_international_characters()
{
	const auto save_path = _temps.next_path(u8".jpg"sv);
	const auto load_path = test_files_folder.combine_file(u8"test.jpg"sv);
	const auto description = u8"In vollen Zügen genießen"sv;

	tag_set tags;
	tags.add_one(u8"In vollen Zügen genießen"sv);
	tags.add_one(u8"Nældens takvinge"sv);
	tags.add_one(u8"Žižkov"sv);

	const auto test = str::utf16_to_utf8(str::utf8_to_utf16(description));
	assert_equal(description, test);

	metadata_edits edits;
	edits.description = description;
	edits.remove_tags = tag_set(u8"key1 key2 key3"sv);
	edits.add_tags = tags;

	files ff;
	ff.update(load_path, save_path, edits, {}, {}, false, {});

	const auto actual_exif = extract_properties(save_path, metadata_type::EXIF);
	const auto actual_iptc = extract_properties(save_path, metadata_type::IPTC);
	const auto actual_xmp = extract_properties(save_path, metadata_type::XMP);

	assert_equal(description, actual_xmp->description, u8"XMP"sv);
	assert_equal(description, actual_exif->description, u8"EXIF"sv);
	assert_equal(description, actual_iptc->description, u8"IPC"sv);

	const auto expected_tags = tags.to_string();
	assert_equal(expected_tags, actual_xmp->tags, u8"XMP Tags"sv);
	//AssertEqual(expectedTags, actual_exif.Combine(Property::Tag), u8"Exif Tags"sv);
	assert_equal(expected_tags, actual_iptc->tags, u8"IPTC Tags"sv);

	const auto sr = ff_scan_file(ff, save_path);
	const auto ps = sr.to_props();
	assert_equal(description, ps->description);
	assert_equal(expected_tags, ps->tags, u8"Tags"sv);
}

static void should_scan_raw()
{
	const auto load_path = test_files_folder.combine(u8"raw"sv).combine_file(u8"Screws.CR2"sv);

	prop::item_metadata expected;
	expected.title = u8"Screws on Desk"_c;
	expected.file_name = u8"Screws.CR2"_c;
	expected.copyright_notice = u8"Copyright"_c;
	expected.tags = u8"Desk Macro Screws"_c;
	expected.title = u8"Screws on Desk"_c;
	expected.description = u8"This is a Description"_c;
	expected.comment = u8"This is a Comment"_c;
	expected.rating = 4;
	expected.width = 3522;
	expected.height = 2348;
	expected.f_number = 2.8f;
	expected.camera_manufacturer = u8"Canon"_c;
	expected.camera_model = u8"EOS-1D Mark II"_c;
	expected.exposure_time = 1 / 25.0f;
	expected.focal_length = 100;
	expected.iso_speed = 100;
	expected.pixel_format = u8"RGBG"_c;
	expected.created_utc = df::date_t(2011, 9, 23, 21, 49, 16);

	null_async_strategy as;
	location_cache locations;
	index_state index(as, locations);
	auto actual = metadata_from_cache(index, load_path);
	assert_metadata(expected, *actual, u8"Screws.CR2"sv);
}

static void should_convert_raw_to_jpeg()
{
	const auto load_path = test_files_folder.combine(u8"raw"sv).combine_file(u8"Screws.CR2"sv);
	const auto save_path = _temps.next_path(u8".jpg"sv);

	files ff;
	ff.update(load_path, save_path, {}, {}, {}, false, {});

	const auto sr_expected = ff_scan_file(ff, load_path, detect_xmp_sidecar(load_path));
	const auto sr_actual = ff_scan_file(ff, save_path);

	const auto expected = sr_expected.to_props();
	const auto actual = sr_actual.to_props();

	//assert_metadata(*expected, *actual);
	assert_equal(expected->tags, actual->tags, u8"tags"sv);
	assert_equal(expected->title, actual->title, u8"title"sv);
	assert_equal(expected->description, actual->description, u8"description"sv);
	assert_equal(expected->width, actual->width, u8"width"sv);
	assert_equal(expected->height, actual->height, u8"height"sv);
}

static void should_parse_facebook_json()
{
	const auto path_status = test_files_folder.combine_file(u8"facebook.state.json"sv);
	const auto status = df::util::json::json_from_file(path_status);

	assert_equal(u8"804051981"sv, status[u8"id"].GetString(), u8"Id"sv);

	const auto path_albums = test_files_folder.combine_file(u8"facebook.albums.json"sv);
	auto json = df::util::json::json_from_file(path_albums);
	auto& albums = json[u8"data"];

	assert_equal(19u, albums.Size(), u8"data"sv);
	assert_equal(u8"10150677908351982"sv, albums[0][u8"id"].GetString(), u8"id"sv);
	assert_equal(u8"Wall Photos"sv, albums[0][u8"name"].GetString(), u8"name"sv);
	assert_equal(false, albums[0][u8"can_upload"].GetBool(), u8"can_upload"sv);
}

static void should_rotate()
{
	files ff;

	{
		const auto save_path = _temps.next_path();
		const auto load_path = test_files_folder.combine_file(u8"Test.jpg"sv);
		const auto loaded = ff.load(load_path, false);

		image_edits edits;
		const quadd crop(loaded.i->dimensions());
		edits.crop_bounds(crop.transform(simple_transform::rot_90));

		metadata_edits md_edits;
		md_edits.orientation = ui::orientation::top_left;

		ff.update(load_path, save_path, md_edits, edits, {}, false, {});

		const auto expected = extract_properties(test_files_folder.combine_file(u8"Test90.jpg"sv));
		const auto actual = extract_properties(save_path);

		assert_equal(expected->width, actual->width, u8"width"sv);
		assert_equal(expected->height, actual->height, u8"height"sv);

		assert_metadata(*expected, *actual);
	}

	{
		// Check Exif orientation is correctly updated

		const auto save_path = _temps.next_path();
		const auto load_path = test_files_folder.combine_file(u8"exif-rotated.jpg"sv);
		const auto loaded = ff.load(load_path, false);

		image_edits edits;
		const quadd crop(loaded.i->dimensions());
		edits.crop_bounds(crop.transform(simple_transform::rot_90));

		metadata_edits md_edits;
		md_edits.orientation = ui::orientation::top_left;

		ff.update(load_path, save_path, md_edits, edits, {}, false, {});

		const auto actual_exif = extract_properties(save_path, metadata_type::EXIF);
		assert_equal(ui::orientation::top_left, actual_exif->orientation, u8"orientation"sv);
	}

	{
		// PNG
		const auto save_path = _temps.next_path(u8".png"sv);
		const auto load_path = test_files_folder.combine_file(u8"engine.png"sv);
		const auto loaded = ff.load(load_path, false);

		image_edits edits;
		const quadd crop(loaded.i->dimensions());
		edits.crop_bounds(crop.transform(simple_transform::rot_90));
		ff.update(load_path, save_path, {}, edits, {}, false, {});

		const auto updated = ff.load(save_path, false);
		assert_equal(loaded.i->height(), updated.i->width(), u8"png width"sv);
		assert_equal(loaded.i->width(), updated.i->height(), u8"png width"sv);
	}

	{
		// WEBP
		const auto save_path = _temps.next_path(u8".webp"sv);
		const auto load_path = test_files_folder.combine_file(u8"lake.webp"sv);
		const auto loaded = ff.load(load_path, false);

		image_edits edits;
		const quadd crop(loaded.i->dimensions());
		edits.crop_bounds(crop.transform(simple_transform::rot_90));
		ff.update(load_path, save_path, {}, edits, {}, false, {});

		const auto updated = ff.load(save_path, false);
		assert_equal(false, updated.is_empty(), u8"webp result empty"sv);
		assert_equal(loaded.i->height(), updated.i->width(), u8"webp width"sv);
		assert_equal(loaded.i->width(), updated.i->height(), u8"webp width"sv);
	}
}

static void should_rotate133()
{
	const auto save_path = _temps.next_path();
	const auto load_path = test_files_folder.combine_file(u8"Test.jpg"sv);

	files ff;
	const auto loaded = ff.load(load_path, false);

	const quadd crop(loaded.i->dimensions());
	image_edits edits;
	edits.crop_bounds(crop.rotate(133, crop.center_point()));

	ff.update(load_path, save_path, {}, edits, {}, false, {});

	const auto actual = extract_properties(save_path);
	const auto expected = expected_test_jpg();
	expected->width = 576;
	expected->height = 384;
	assert_metadata(*expected, *actual);
}

static void should_rotate_lossless()
{
	const auto save_path = _temps.next_path();
	const auto load_path = test_files_folder.combine_file(u8"Lossless0.jpg"sv);

	image_edits edits;
	const quadd crop(sizei(640, 480));
	edits.crop_bounds(crop.transform(simple_transform::rot_90));

	files ff;
	ff.update(load_path, save_path, {}, edits, {}, false, {});

	const auto expected = extract_properties(test_files_folder.combine_file(u8"Lossless90.jpg"sv));
	const auto actual = extract_properties(save_path);

	assert_equal(expected->width, actual->width);
	assert_equal(expected->height, actual->height);
}

static void should_resize()
{
	const auto save_path = _temps.next_path();
	const auto load_path = test_files_folder.combine_file(u8"Test.jpg"sv);

	image_edits edits;
	edits.scale(sizei(200, 150));

	files ff;
	ff.update(load_path, save_path, {}, edits, {}, false, {});

	const auto actual = extract_properties(save_path);
	assert_equal(200, actual->width);
	assert_equal(133, actual->height);
}

static void should_preserve_orientation()
{
	const auto load_path = test_files_folder.combine_file(u8"exif-rotated.jpg"sv);

	files ff;
	const auto loaded = ff.load(load_path, false);
	const auto surface = loaded.to_surface();
	const auto loaded_orientation = loaded.i->orientation();

	const auto encoded_jpg = ff.surface_to_image(surface, {}, {}, ui::image_format::JPEG);
	assert_equal(loaded_orientation, encoded_jpg->orientation(), u8"orientation for created jpg"sv);

	const auto encoded_png = ff.surface_to_image(surface, {}, {}, ui::image_format::PNG);
	assert_equal(loaded_orientation, encoded_png->orientation(), u8"orientation for created png"sv);

	const auto encoded_webp = ff.surface_to_image(surface, {}, {}, ui::image_format::WEBP);
	assert_equal(loaded_orientation, encoded_webp->orientation(), u8"orientation for created webp"sv);

	const auto loaded_jpg = load_image_file(encoded_jpg->data()); // will cause re-parse
	assert_equal(loaded_orientation, loaded_jpg->orientation(), u8"orientation for re-loaded jpg"sv);

	const auto loaded_png = load_image_file(encoded_png->data()); // will cause re-parse
	assert_equal(loaded_orientation, loaded_png->orientation(), u8"orientation for re-loaded png"sv);

	const auto loaded_webp = load_image_file(encoded_webp->data()); // will cause re-parse
	assert_equal(loaded_orientation, loaded_webp->orientation(), u8"orientation for re-loaded webp"sv);
}


static void should_store_thumbnails()
{
	const auto index_path = _temps.next_path();
	const auto file_path = test_files_folder.combine_file(u8"Test.jpg"sv);

	null_async_strategy as;
	location_cache locations;
	index_state index(as, locations);

	database db(index);
	db.open(index_path.folder(), index_path.file_name_without_extension());

	// test local item
	auto i = load_item(index, file_path, true);
	db.perform_writes();

	auto thumb = db.load_thumbnail(i->path());
	assert_equal(i->thumbnail(), thumb.thumb, u8"local loaded thumb"sv);
}


static void should_store_item_properties()
{
	const auto index_path = _temps.next_path();
	const auto file_path = test_files_folder.combine_file(u8"Test.jpg"sv);

	null_async_strategy as;
	location_cache locations;
	index_state index(as, locations);

	database db(index);
	db.open(index_path.folder(), index_path.file_name_without_extension());

	auto data = blob_from_file(file_path);
	const auto crc32c_expected = crypto::crc32c(data.data(), data.size());

	auto md = std::make_shared<prop::item_metadata>();
	md->album = u8"test"_c;
	md->orientation = ui::orientation::bottom_right;

	std::deque<item_db_write> writes;

	const auto media_pos = 111.1;

	{
		item_db_write w;
		w.crc32c = crc32c_expected;
		w.md = md;
		w.media_position = media_pos;
		w.path = file_path;
		writes.emplace_back(std::move(w));
	}

	db.perform_writes(std::move(writes));
	db.load_index_values();

	auto item = index.find_item(file_path);
	assert_metadata(*md, *item.metadata, u8"index"sv);
	assert_equal(crc32c_expected, item.crc32c, u8"index crc32"sv);
	assert_equal(static_cast<int>(media_pos), static_cast<int>(item.metadata->media_position),
	             u8"index media position"sv);
	assert_equal(md->orientation, item.metadata->orientation, u8"index orientation"sv);

	const auto reloaded_crc = platform::file_crc32(file_path);
	assert_equal(reloaded_crc, item.crc32c, u8"platform::file_crc32 crc32"sv);
}


static void should_pack_item_properties()
{
	const auto file_path = test_files_folder.combine_file(u8"Test.jpg"sv);
	const auto md = extract_properties(file_path);
	md->album = u8"test"_c;
	md->orientation = ui::orientation::bottom_right;

	metadata_packer packer;
	packer.pack(md);

	const auto unpacked = std::make_shared<prop::item_metadata>();

	metadata_unpacker unpacker(packer.cdata());
	unpacker.unpack(unpacked);

	assert_metadata(*md, *unpacked, u8"index"sv);
	assert_equal(md->orientation, unpacked->orientation, u8"index orientation"sv);
}

static void should_store_webservice_results()
{
	const auto index_path = _temps.next_path();

	null_async_strategy as;
	location_cache locations;
	index_state index(as, locations);
	database db(index);
	db.open(index_path.folder(), index_path.file_name_without_extension());

	const auto key = u8"key   xxxxxxxxxx"sv;
	const auto value = long_text;
	db.web_service_cache(key, value);
	const auto result = db.web_service_cache(key);

	assert_equal(result, value, u8"web_service_cache"sv);
}

// IMG_0096.JPG 15/11/2009
// IMG_9340.JPG 15/05/2011
// Lossless0.jpg 22/4/11
// Test.jpg 25/5/08


static int count_search_results(index_state& index, df::search_t search)
{
	//const df::item_selector folder(test_files_folder, true);
	//const auto search = search_base.add_selector(folder);

	const df::unique_items existing;
	int result = 0;

	auto cb = [&result](const df::item_set& append_items, const bool completed)
	{
		result += static_cast<int>(append_items.size());
	};

	index.query_items(search, existing, cb, test_token);
	return result;
}

static int count_search_results(index_state& index, const std::u8string_view query)
{
	return count_search_results(index, df::search_t::parse(query));
}

static void build_index(index_state& index, database& db)
{
	df::index_roots paths;
	paths.folders.emplace(test_files_folder);

	index.index_roots(paths);
	index.index_folders(test_token);
	index.scan_uncached(test_token);
	db.perform_writes();

	assert_equal(expected_cached_item_count, index.stats.media_item_count, u8"cached item count"sv);
}


struct test_view::shared_test_context
{
	null_async_strategy as;
	location_cache locations;
	index_state index;
	bool loaded = false;

	shared_test_context() : index(as, locations)
	{
	}

	void lazy_load_index()
	{
		if (!loaded)
		{
			auto cache_path = _temps.next_path();
			database db(index);
			db.open(cache_path.folder(), cache_path.file_name_without_extension());
			build_index(index, db);

			df::unique_items existing;
			df::item_selector selector(test_files_folder, true);
			df::item_set items;

			auto cb = [&items](const df::item_set& append_items, const bool completed)
			{
				items.append(append_items);
			};

			index.query_items(df::search_t().add_selector(selector), existing, cb, test_token);
			db.load_thumbnails(index, items);
			index.scan_items(items, false, false, false, false, test_token);
			db.perform_writes();

			assert_equal(expected_cached_item_count, index.stats.media_item_count, u8"cached item count"sv);
			loaded = true;
		}
	}
};


static void should_index(test_view::shared_test_context& stc)
{
	stc.lazy_load_index();

	assert_equal(expected_cached_item_count, stc.index.stats.media_item_count, u8"cached item count"sv);

	const auto expected_md = expected_test_jpg();
	expected_md->file_name = u8"Test.jpg"_c;
	// Embedded values
	assert_metadata(*expected_md, *metadata_from_cache(stc.index, test_files_folder.combine_file(u8"Test.jpg"sv)),
	                u8"Test.jpg"sv);

	// Sidecar values
	const auto actual = metadata_from_cache(stc.index, test_files_folder.combine_file(u8"Gherkin.CR2"sv));
	assert_equal(u8"Canon"sv, actual->camera_manufacturer, u8"camera_manufacturer"sv);
	assert_equal(u8"United Kingdom"sv, actual->location_country, u8"location_country"sv);
	assert_equal(u8"\xA9 Mark Ridgwell"sv, actual->copyright_notice, u8"copyright_notice"sv);
	assert_equal(u8"\"Mark Ridgwell\""sv, actual->copyright_creator, u8"copyright_creator"sv);
}


class prop_test
{
	df::index_file_item _f;
	uint32_t now_days = df::date_t(2000, 1, 1).to_days();

public:
	prop_test()
	{
		_f.ft = files::file_type_from_name(u8"test.jpg"sv);
	}

	prop_test& tag(const std::u8string_view s)
	{
		_f.safe_ps()->tags = str::cache(s);
		return *this;
	}

	prop_test& date(const int y, const int m, const int d)
	{
		_f.safe_ps()->created_utc = df::date_t(y, m, d);
		return *this;
	}

	prop_test& file_created_date(const int y, const int m, const int d)
	{
		_f.file_created = df::date_t(y, m, d);
		_f.file_modified = df::date_t(y, m, d);
		return *this;
	}

	prop_test& desc(const std::u8string_view s)
	{
		_f.safe_ps()->description = str::cache(s);
		return *this;
	}

	prop_test& rate(const int16_t n)
	{
		_f.safe_ps()->rating = n;
		return *this;
	}

	prop_test& is_match(const std::u8string_view query)
	{
		const auto search = df::search_t::parse(query);
		const df::search_matcher matcher(search, now_days);

		assert_equal(true, matcher.match_all_terms(_f).is_match(), query);
		return *this;
	}

	prop_test& is_not_match(const std::u8string_view query)
	{
		const auto search = df::search_t::parse(query);
		const df::search_matcher matcher(search, now_days);

		assert_equal(false, matcher.match_all_terms(_f).is_match(), query);
		return *this;
	}
};


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

	// Metadata and file date. Should use meta
	prop_test().file_created_date(2000, 1, 1).date(1999, 5, 25)
	           .is_not_match(u8"created:9"sv)
	           .is_not_match(u8"created:2000-jan"sv)
	           .is_match(u8"created:1999-may"sv);

	// file date but not metadata
	prop_test().file_created_date(2000, 1, 1)
	           .is_match(u8"age:10"sv)
	           .is_match(u8"created:2000-jan"sv);
}

static void should_not_match_folder_without()
{
	const auto now_days = df::date_t(2000, 1, 1).to_days();
	const auto search = df::search_t::parse(u8"without:tag"sv);
	const df::search_matcher matcher(search, now_days);
	assert_equal(false, matcher.match_folder(u8"test"_c).is_match(), u8"folder name test"sv);
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

	assert_equal(true, matcher.match_all_terms(props_with_val).is_match(), query);
	assert_equal(false, matcher.match_all_terms(props_without_val).is_match(), query);
}


void assert_parse(const std::u8string_view selector, const std::u8string_view display, const std::u8string_view folder,
                  bool is_recursive, const std::u8string_view wildcard)
{
	const df::item_selector sel(selector);

	assert_equal(display, sel.str(), u8"item_selector.str"sv);
	assert_equal(folder, sel.folder().text(), u8"item_selector.folder"sv);
	assert_equal(is_recursive, sel.is_recursive(), u8"item_selector.is_recursive"sv);
	assert_equal(wildcard, sel.wildcard(), u8"item_selector.wildcard"sv);
}

void assert_parse(const std::u8string_view selector, const std::u8string_view folder, bool is_recursive,
                  const std::u8string_view wildcard)
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
	assert_equal(true, contains(platform::select_files(recursive, true), u8"Screws.CR2"sv), u8"files from sub folders"sv);
	assert_equal(true, contains(platform::select_files(recursive, true), u8"Cube.png"sv), u8"files from sub folders"sv);
	assert_equal(true, contains(platform::select_files(recursive, true), u8"Test.jpg"sv), u8"files from sub folders"sv);
	assert_equal(false, contains(platform::select_folders(recursive, true), u8"raw"sv), u8"no folders"sv);

	const auto not_recursive = df::item_selector(test_files_folder, false);
	assert_equal(false, contains(platform::select_files(not_recursive, true), u8"Screws.CR2"sv),
	             u8"no files from sub folder"sv);
	assert_equal(true, contains(platform::select_files(not_recursive, true), u8"Cube.png"sv), u8"files from sub folders"sv);
	assert_equal(true, contains(platform::select_files(not_recursive, true), u8"Test.jpg"sv), u8"files from sub folders"sv);
	assert_equal(true, contains(platform::select_folders(not_recursive, true), u8"raw"sv), u8"include folders"sv);

	const auto recursive_png = df::item_selector(test_files_folder, true, u8"*.png"sv);
	assert_equal(false, contains(platform::select_files(recursive_png, true), u8"Screws.CR2"sv),
	             u8"no files from sub folder"sv);
	assert_equal(true, contains(platform::select_files(recursive_png, true), u8"Cube.png"sv), u8"files from sub folders"sv);
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

static void should_match_related(test_view::shared_test_context& stc)
{
	stc.lazy_load_index();

	const df::file_path path(test_files_folder, u8"Test.jpg"sv);
	const auto i = std::make_shared<df::file_item>(path, stc.index.find_item(path));
	stc.index.scan_item(i, true, false);

	df::related_info r;
	r.load(i);

	assert_equal(1, count_search_results(stc.index, df::search_t().related(r)), u8"Related"sv);
}

static df::item_element_ptr find_item_n(view_state& s, const int n)
{
	auto count = 0;

	for (const auto& b : s.groups())
	{
		for (const auto& i : b->items())
		{
			if (count == n) return i;
			count++;
		}
	}

	return nullptr;
}

struct null_av_host : public av_host
{
	void invalidate_view(const view_invalid invalid) override
	{
	}

	void queue_ui(std::function<void()> f) override
	{
		f();
	}
};

static std::shared_ptr<av_player> make_test_player()
{
	static null_av_host navh;
	return std::make_shared<av_player>(navh);
}

static void should_select_items()
{
	null_state_strategy ss;
	null_async_strategy as;
	view_host_base_ptr view;

	location_cache locations;
	index_state index(as, locations);
	view_state s(ss, as, index, make_test_player());
	s.view_mode(view_type::items);
	s.open(view, df::search_t().add_selector(test_files_folder), {});
	s.update_item_groups();
	s.update_selection();

	assert_equal(expected_cached_item_count + 2_z, s.search_items().items().size(), u8"items loaded into state"sv);
	assert_equal(1_z, s.search_items().folders().size(), u8"folders loaded into state"sv);

	assert_equal(0_z, s.selected_count(), u8"invalid selection"sv);

	s.select(view, find_item_n(s, 3), false, false, false);
	s.update_selection();
	assert_equal(1_z, s.selected_count(), u8"invalid selection"sv);

	s.select(view, find_item_n(s, 1), false, false, false);
	s.update_selection();
	assert_equal(1_z, s.selected_count(), u8"invalid selection"sv);

	s.select(view, find_item_n(s, 8), true, false, false);
	s.update_selection();
	assert_equal(2_z, s.selected_count(), u8"invalid control selection"sv);

	s.select(view, find_item_n(s, 1), false, false, false);
	s.update_selection();
	assert_equal(1_z, s.selected_count(), u8"invalid selection"sv);

	s.select(view, find_item_n(s, 4), false, true, false);
	s.update_selection();
	assert_equal(4_z, s.selected_count(), u8"invalid selection"sv);

	s.select(view, find_item_n(s, 2), true, false, false);
	s.update_selection();
	assert_equal(3_z, s.selected_count(), u8"invalid selection"sv);

	s.select(view, find_item_n(s, 6), true, false, false);
	s.update_selection();
	assert_equal(4_z, s.selected_count(), u8"invalid selection"sv);
}

static void should_toggle_rating()
{
	const df::file_path src_path1(test_files_folder, u8"Test.jpg"sv);
	const df::file_path src_path2(test_files_folder, u8"Gherkin.CR2"sv);

	const auto save_path_1 = _temps.next_path(u8".jpg"sv);
	const auto save_path_2 = _temps.next_path(u8".cr2"sv);

	platform::copy_file(src_path1, save_path_1, false, false);
	platform::copy_file(src_path2, save_path_2, false, false);

	const auto temp_folder = _temps.folder();

	null_state_strategy ss;
	null_async_strategy as;
	view_host_base_ptr view;
	auto results = std::make_shared<null_item_results_ui>();

	location_cache locations;
	index_state index(as, locations);
	view_state s(ss, as, index, make_test_player());
	s.view_mode(view_type::items);
	s.open(view, df::search_t().add_selector(temp_folder), {});
	s.item_index.scan_items(s.search_items(), false, false, false, false, test_token);
	s.update_item_groups();

	s.select(view, save_path_1.name(), false);
	s.update_selection();

	assert_equal(1_z, s.selected_count(), u8"invalid selection"sv);
	s.toggle_rating(results, s.selected_items().items(), 3, view);
	s.item_index.scan_items(s.search_items(), false, false, false, false, test_token);
	assert_equal(3, s.displayed_rating(), u8"invalid jpeg rating"sv);

	s.toggle_rating(results, s.selected_items().items(), 0, view);
	s.item_index.scan_items(s.search_items(), false, false, false, false, test_token);
	assert_equal(0, s.displayed_rating(), u8"invalid remove jpeg rating"sv);

	s.select(view, save_path_2.name(), false);
	s.update_selection();

	assert_equal(1_z, s.selected_count(), u8"invalid selection"sv);
	s.toggle_rating(results, s.selected_items().items(), 4, view);
	s.item_index.scan_items(s.search_items(), false, false, false, false, test_token);
	assert_equal(4, s.displayed_rating(), u8"invalid cr2 rating"sv);

	s.item_index.scan_items(s.search_items(), true, false, false, false, test_token);
	assert_equal(4, s.displayed_rating(), u8"invalid cr2 rating after thumbnail load"sv);

	s.toggle_rating(results, s.selected_items().items(), 0, view);
	s.item_index.scan_items(s.search_items(), false, false, false, false, test_token);
	assert_equal(0, s.displayed_rating(), u8"invalid remove cr2 rating"sv);
}

static void assert_can_process(const view_state& s, bool photos_only, bool can_save_pixels, bool can_save_metadata,
                               bool local_file, bool local_file_or_folder, const std::u8string_view message)
{
	const view_host_base_ptr view;
	assert_equal(photos_only, s.can_process_selection(view, df::process_items_type::photos_only), message);
	assert_equal(can_save_pixels, s.can_process_selection(view, df::process_items_type::can_save_pixels), message);
	assert_equal(can_save_metadata, s.can_process_selection(view, df::process_items_type::can_save_metadata), message);
	assert_equal(local_file, s.can_process_selection(view, df::process_items_type::local_file), message);
	assert_equal(local_file_or_folder, s.can_process_selection(view, df::process_items_type::local_file_or_folder),
	             message);
}

static void should_enable_based_on_selection()
{
	null_state_strategy ss;
	null_async_strategy as;
	view_host_base_ptr view;

	location_cache locations;
	index_state index(as, locations);
	view_state s(ss, as, index, make_test_player());
	s.view_mode(view_type::items);
	s.open(view, df::search_t().add_selector(test_files_folder), {});
	s.update_item_groups();
	s.update_selection();

	assert_equal(0_z, s.selected_count(), u8"by default nothing selected"sv);
	assert_can_process(s, false, false, false, false, false, u8"nothing selected"sv);

	s.select(view, u8"test.jpg"sv, false);
	s.update_selection();

	assert_equal(1_z, s.selected_count(), u8"select test.jpg"sv);
	assert_can_process(s, true, true, true, true, true, u8"photo selected"sv);

	s.select(view, u8"gizmo.mp4"sv, false);
	s.update_selection();

	assert_equal(1_z, s.selected_count(), u8"select gizmo.mp4"sv);
	assert_can_process(s, false, false, true, true, true, u8"mp4 selected"sv);

	s.select(view, u8"Gherkin.xmp"sv, false);
	s.update_selection();

	assert_equal(1_z, s.selected_count(), u8"Gherkin.xmp"sv);
	assert_can_process(s, false, false, true, true, true, u8"xmp selected"sv);

	s.select(view, u8"test.jpg"sv, false);
	s.select(view, u8"gizmo.mp4"sv, true);
	s.update_selection();

	assert_equal(2_z, s.selected_count(), u8"test.jpg and gizmo.mp4"sv);
	assert_can_process(s, false, false, true, true, true, u8"jpg and mp4 selected"sv);

	s.select(view, u8"raw"sv, false);
	s.update_selection();

	assert_equal(1_z, s.selected_count(), u8"folder"sv);
	assert_can_process(s, false, false, false, false, true, u8"folder selected"sv);
}

static void should_parse_search()
{
	const auto message = u8"Should tokenize search"sv;
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

	// TODO more things
	s.view_mode(view_type::items);
	s.open(view, base_folder + u8"\\**"s);
	s.escape(view);
	assert_equal(base_folder, s.search().text(), u8"parent escape **"sv);
}


static void should_calc_HMACSHA1()
{
	const auto signature = crypto::hmac_sha1(u8"Jefe"sv, u8"what do ya want for nothing?"sv);
	assert_equal(u8"7/zfauXrL6LSdBbV8YTfnCWafHk="sv, signature, u8"Signature"sv);
}

static void should_calc_hashes()
{
	assert_equal(u8"A9993E364706816ABA3E25717850C26C9CD0D89D"sv, crypto::to_sha1(u8"abc"sv), u8"SHA1"sv);
	assert_equal(u8"187797D630ECAA0FC1B920CD9F809C2BBFFCBF4C"sv, crypto::to_sha1(long_text), u8"SHA1"sv);
	assert_equal(u8"BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"sv, crypto::to_sha256(u8"abc"sv), u8"SHA256"sv);
	assert_equal(u8"1660F10AEC042D762CF8B1C53E976F890C8E797BEF74807F505EDCE20308FC2F"sv, crypto::to_sha256(long_text), u8"SHA256"sv);

	const auto crc_data = u8"hello world"s;
	const auto crc_result = crypto::crc32c(crc_data.data(), crc_data.size());
	assert_equal(0xc99465aa, crc_result, u8"crc32"sv);

	const auto crc_c = ~calc_crc32c_c(crypto::CRCINIT, crc_data.data(), crc_data.size());
	assert_equal(0xc99465aa, crc_c, u8"crc32 c"sv);

	if (platform::sse2_supported)
	{
		const auto crc_sse2 = ~calc_crc32c_sse2(crypto::CRCINIT, crc_data.data(), crc_data.size());
		assert_equal(0xc99465aa, crc_sse2, u8"crc32 sse"sv);
	}

	if (platform::neon_supported)
	{
		const auto crc_neon = ~calc_crc32c_arm(crypto::CRCINIT, crc_data.data(), crc_data.size());
		assert_equal(0xc99465aa, crc_neon, u8"crc32 neon"sv);
	}
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

	std::u8string_view strings[] = {
		u8"In vollen Zügen genießen"sv,
		u8"Nældens takvinge"sv,
		u8"💉💎👦🏻👓⚡"sv,
		u8"Žižkov"sv,
		u8"Доброго ранку!"sv,
		u8"Japanese こんにちは世界"sv,
		u8"Arabic مرحبا العالم"sv,
		stars
	};

	for (const auto src : strings)
	{
		assert_equal(src, platform::utf16_to_utf8(platform::utf8_to_utf16(src)), u8"platform conversions"sv);
		assert_equal(platform::utf8_to_utf16(src), str::utf8_to_utf16(src), u8"to utf16"sv);
		assert_equal(src, str::utf16_to_utf8(platform::utf8_to_utf16(src)), u8"to utf8"sv);
		assert_equal(src, str::utf16_to_utf8(str::utf8_to_utf16(src)), u8"internal conversions"sv);
		
	}

	constexpr wchar_t icon_text[2] = {static_cast<wchar_t>(icon_index::fit), 0};
	const auto icon_text_converted = str::utf8_to_utf16(str::utf16_to_utf8(icon_text));
	assert_equal(icon_text, icon_text_converted, u8"icon to utf8"sv);
}

static void should_split()
{
	constexpr auto to_be_split = u8"H:\\2-Archief VIDEO privé\\Eigen video's\nF:\\1-Archief FOTOGRAFIE privé"sv;
	const auto parts = str::split(to_be_split, false, [](wchar_t c) { return c == '\n' || c == '\r'; });

	assert_equal(u8"H:\\2-Archief VIDEO privé\\Eigen video's"sv, parts[0], u8"Split 1"sv);
	assert_equal(u8"F:\\1-Archief FOTOGRAFIE privé"sv, parts[1], u8"Split 2"sv);

	constexpr auto to_be_split2 = u8"aaa 'bbb ccc' ddd \"ee ff \""sv;
	const auto parts2 = str::split(to_be_split2, true);

	constexpr auto to_be_split3 = u8"Доброго ранку!"sv;
	const auto parts3 = str::split(to_be_split3, true);

	assert_equal(u8"aaa"sv, parts2[0], u8"Split 1"sv);
	assert_equal(u8"bbb ccc"sv, parts2[1], u8"Split 2"sv);
	assert_equal(u8"ddd"sv, parts2[2], u8"Split 3"sv);
	assert_equal(u8"ee ff "sv, parts2[3], u8"Split 4"sv);
	assert_equal(u8"ранку!"sv, parts3[1], u8"Split 5"sv);

	// Random data checking for crashes
	std::u8string_view strings[] = {
		u8"In vollen Zügen genießen"sv,
		u8"Nældens takvinge"sv,
		u8"Žižkov"sv,
		u8"Доброго ранку!"sv,
		u8"Japanese こんにちは世界"sv,
		u8"Arabic مرحبا العالم"sv,
		u8"Доброго ранку!"sv,
		u8"\"'"sv,
		u8"\" \" \""sv,
		u8"''''"sv,
		u8"aaa'bb  bbb'aa"sv,
		u8"aaa\0\0\'"sv,
		u8"\r\t\naaaa\" aaa bbb"sv,
		u8"'\t \n abc"sv,
		u8"'"sv,
	};

	for (const auto& src : strings)
	{
		str::split_count(src, true);
	}
}

static void should_match_wildcard()
{
	assert_equal(true, str::wildcard_icmp(u8""sv, u8""sv));
	assert_equal(true, str::wildcard_icmp(u8""sv, u8"*"sv));
	assert_equal(true, str::wildcard_icmp(u8" "sv, u8"*"sv));
	assert_equal(true, str::wildcard_icmp(u8" "sv, u8" *"sv));
	assert_equal(false, str::wildcard_icmp(u8" "sv, u8"  *"sv));

	assert_equal(true, str::wildcard_icmp(u8"hello world"sv, u8"hello world"sv));
	assert_equal(true, str::wildcard_icmp(u8"hello ?! world"sv, u8"hello * world"sv));
	assert_equal(true, str::wildcard_icmp(u8"hello-xx-world"sv, u8"hello*world"sv));
	assert_equal(false, str::wildcard_icmp(u8"hello-xx-world"sv, u8"hello *world"sv));
	assert_equal(true, str::wildcard_icmp(u8"hello-xx-world"sv, u8"*world"sv));
	assert_equal(true, str::wildcard_icmp(u8"hello-xx-world"sv, u8"hello*"sv));

	assert_equal(true, str::wildcard_icmp(u8"HELLO-XX-WORLD"sv, u8"hello*"sv));
	assert_equal(true, str::wildcard_icmp(u8"HELLO-XX-WORLD"sv, u8"hello*world"sv));


	assert_equal(0, str::icmp(u8"ДОБРОГО РАНКУ"sv, u8"Доброго ранку"sv));

	assert_equal(true, str::wildcard_icmp(u8"Доброго ранку"sv, u8"Доброго*"sv));
	assert_equal(true, str::wildcard_icmp(u8"ДОБРОГО РАНКУ"sv, u8"Доброго*"sv));
	assert_equal(true, str::wildcard_icmp(u8"ДОБРОГО РАНКУ"sv, u8"*ранку"sv));
}

static void should_detect_wildcard()
{
	assert_equal(false, str::is_wildcard(u8""sv));
	assert_equal(false, str::is_wildcard(u8"abcdef"sv));
	assert_equal(true, str::is_wildcard(u8"abc*"sv));
	assert_equal(false, str::is_wildcard(u8"abc\\*"sv));
	assert_equal(false, str::is_wildcard(u8"abc\\*ef"sv));
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

//auto_complete_results auto_complete(const char8_t * query, int max_results);

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

static void should_detect_original_path()
{
	const df::file_path path(u8"c:\\temp\\test.original.jpg"sv);
	assert_equal(true, path.is_original(), u8"detect original"sv);
}


static void should_not_reload_thumb_when_valid()
{
	const auto load_path = test_files_folder.combine_file(u8"test.jpg"sv);

	const df::date_t date(1972, 5, 25);
	const df::date_t date2(1972, 5, 26);

	files ff;
	const auto loaded = ff.load(load_path, false);

	const auto i_local = std::make_shared<df::file_item>(load_path, make_index_file_info(date));
	assert_equal(false, i_local->should_load_thumbnail(), u8"should not load by default"sv);

	i_local->db_thumb_query_complete();
	assert_equal(true, i_local->should_load_thumbnail(), u8"should load after db load"sv);

	i_local->thumbnail(loaded.i);
	assert_equal(true, i_local->should_load_thumbnail(), u8"should load without timestamp"sv);

	i_local->thumbnail(loaded.i, date);
	assert_equal(false, i_local->should_load_thumbnail(), u8"should load if thumb but not hash"sv);

	i_local->update(load_path, make_index_file_info(date2));
	assert_equal(true, i_local->should_load_thumbnail(), u8"should if date changes"sv);
}


void should_copy_preserve_properties()
{
	// TODO
	/*prop::item_metadata ps;
	ps.store(prop::media_position, 19);

	prop::item_metadata ps_copy;
	ps_copy.copy_preserve_properties(ps);
	assert_equal(19, ps_copy.read_int(prop::media_position), u8"should copy media_position"sv);*/
}

static void should_reload_thumb_after_scan()
{
	// i->db_thumb_query_complete();

	files ff;
	const auto index_path = _temps.next_path();
	null_async_strategy as;
	location_cache locations;
	index_state index(as, locations);

	database db(index);
	db.open(index_path.folder(), index_path.file_name_without_extension());
	build_index(index, db);

	auto path_test = df::file_path(test_files_folder, u8"Test.jpg"sv);
	auto path_sony = df::file_path(test_files_folder, u8"Sony.jpg"sv);

	const auto test_item = load_item(index, path_test, false);
	const auto sony_item = load_item(index, path_sony, false);

	assert_equal(false, test_item->should_load_thumbnail(), u8"should_load_thumbnail for test.jpg"sv);
	assert_equal(false, sony_item->should_load_thumbnail(), u8"should_load_thumbnail for sony.jpg"sv);

	df::item_set items;
	items._items = {test_item, sony_item};


	index.scan_items(items, false, false, false, false, test_token);
	db.perform_writes();

	assert_equal(false, test_item->should_load_thumbnail(), u8"should_load_thumbnail for test.jpg"sv);
	assert_equal(false, sony_item->should_load_thumbnail(), u8"should_load_thumbnail for sony.jpg"sv);

	db.load_thumbnails(index, items);

	assert_equal(true, test_item->should_load_thumbnail(),
	             u8"should_load_thumbnail for test.jpg after db load_thumbnails"sv);
	assert_equal(true, sony_item->should_load_thumbnail(),
	             u8"should_load_thumbnail for sony.jpg after db load_thumbnails"sv);

	index.scan_items(items, true, false, false, false, test_token);
	db.perform_writes();

	assert_equal(false, test_item->should_load_thumbnail(),
	             u8"should_load_thumbnail for test.jpg after index load_thumbnails"sv);
	assert_equal(false, sony_item->should_load_thumbnail(),
	             u8"should_load_thumbnail for sony.jpg after index load_thumbnails"sv);
}

static void should_rename()
{
	null_async_strategy as;
	location_cache locations;
	index_state index(as, locations);

	const df::file_path src_path(test_files_folder, u8"Test.jpg"sv);
	const auto save_path_1 = _temps.next_path(u8".jpg"sv);
	const auto save_path_2 = _temps.next_path(u8".jpg"sv);

	platform::copy_file(src_path, save_path_1, false, false);

	auto test_item = load_item(index, save_path_1, false);

	assert_equal(true, test_item->rename(index, save_path_2.file_name_without_extension()).success(), u8"can rename"sv);
	assert_equal(save_path_2.name(), test_item->name(), u8"renamed"sv);
	assert_equal(true, save_path_2.exists(), u8"renamed exists"sv);

	// TODO test index handles rename
}

static void should_rename_with_substitutions()
{
	null_async_strategy as;
	location_cache locations;
	index_state index(as, locations);

	const df::file_path src_path(test_files_folder, u8"Test.jpg"sv);
	const auto save_path_1 = _temps.next_path(u8".jpg"sv);
	const auto save_path_2 = _temps.next_path(u8".jpg"sv);

	platform::copy_file(src_path, save_path_1, false, false);

	auto test_item = load_item(index, save_path_1, false);

	assert_equal(true, test_item->rename(index, save_path_2.file_name_without_extension()).success(), u8"can rename"sv);
	assert_equal(save_path_2.name(), test_item->name(), u8"renamed"sv);
	assert_equal(true, save_path_2.exists(), u8"renamed exists"sv);

	// TODO test index handles rename
}

static void should_not_overwrite_during_rename()
{
	null_async_strategy as;
	location_cache locations;
	index_state index(as, locations);

	const df::file_path src_path(test_files_folder, u8"Test.jpg"sv);
	const auto save_path_1 = _temps.next_path(u8".jpg"sv);
	const auto save_path_2 = _temps.next_path(u8".jpg"sv);

	platform::copy_file(src_path, save_path_1, false, false);
	platform::copy_file(src_path, save_path_2, false, false);

	auto test_item = load_item(index, save_path_1, false);

	assert_equal(false, test_item->rename(index, save_path_2.file_name_without_extension()).success(),
	             u8"should not rename"sv);
	assert_equal(save_path_1.name(), test_item->name(), u8"not renamed"sv);
	assert_equal(true, test_item->path().exists(), u8"exists"sv);
}

static void should_encrypt_password()
{
	const std::vector<uint8_t> test_key = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
		0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
	};

	const std::vector<uint8_t> test_enc = {
		0x0c,
		0x8e, 0xa2, 0xb7, 0xca, 0x51, 0x67, 0x45, 0xbf, 0xea, 0xfc, 0x49, 0x90, 0x4b, 0x49, 0x60, 0x89,
		0x3e, 0x8d, 0x89, 0x22, 0xaf, 0x24, 0xef, 0x56, 0x57, 0x96, 0x84, 0x29, 0xfe, 0x01, 0xcd, 0xa0,
		0xd2, 0xfb, 0x4c, 0xd1, 0xf1, 0x95, 0x62, 0xea, 0x68, 0x7f, 0xce, 0x26, 0xc6, 0x34, 0xa8, 0xc2,
		0xda, 0xd8, 0x22, 0x75, 0xcc, 0x1c, 0x87, 0x3d, 0x77, 0xde, 0x14, 0xfc, 0x09, 0x38, 0x4c, 0xc2,
		0x40, 0xec, 0xbb, 0x5a, 0x2d, 0x7f, 0x53, 0xbc, 0x64, 0xef, 0x45, 0x16, 0xbf, 0xee, 0x3a, 0xac,
		0xa3, 0xc9, 0x9c, 0x16, 0x87, 0x39, 0xf3, 0x9e, 0xad, 0x0a, 0xbd, 0xae, 0x19, 0x1a, 0x5a, 0xb5,
		0xb9, 0x5f, 0xe8, 0xea, 0x96, 0x99, 0xf3, 0x47, 0x83, 0x20, 0x69, 0x9e, 0xcd, 0xf3, 0xc8, 0x6e
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

	assert_equal(base64_encode(test_enc), base64_encode(crypto::encrypt(test_dec, test_key)), u8"encrypt using aes"sv);

	const std::vector<std::u8string_view> test_values =
	{
		{},
		u8"This is a test."sv,
		long_text
	};

	static constexpr std::u8string_view password = u8"diffractor-hello"sv;

	for (const auto& val : test_values)
	{
		auto result = crypto::decrypt(crypto::encrypt(val, password), password);
		assert_equal(val, std::u8string(result.begin(), result.end()), u8"Encode - Decode"sv);
	}

	// Check for crash based on bad data
	const std::vector<uint8_t> empty;
	crypto::decrypt(empty, password);

	std::vector<uint8_t> invalid;
	for (auto i = 0; i < 8; i++) invalid.emplace_back(i);
	crypto::decrypt(invalid, password);
}

static void should_persist_to_registry()
{
	const auto archive = platform::create_registry_settings();

	const std::vector<std::u8string> vals = {
		u8"Hello World"s,
		u8"\r\n\t hello"s,
		u8"Доброго ранку!"s,
		u8"Japanese こんにちは世界"s,
		u8"Доброго ранку!"s,
		std::u8string(64, 'x')
	};

	for (const auto& expected : vals)
	{
		std::u8string actual;
		archive->write({}, u8"test"sv, expected);
		archive->read({}, u8"test"sv, actual);

		assert_equal_strict(expected, actual, u8"Persist To Registry"sv);
	}
}

static void write_binary_file(const df::file_path path, const uint8_t* const data, const int size)
{
	const auto f = open_file(path, platform::file_open_mode::create);

	if (f)
	{
		f->write(data, size);
	}
}

static void should_not_crash(const std::u8string_view name)
{
	const auto path = test_files_folder.combine_file(name);
	auto blob = blob_from_file(path);
	const auto ext = name.substr(df::find_ext(name));

	files ff;

	auto* const data = blob.data();
	const auto size = blob.size();

	for (auto i = 0u; i < size && !df::is_closing; i++)
	{
		const auto v = data[i];
		data[i] = 0xff;

		prop::item_metadata md;
		mem_read_stream stream(blob);
		const auto info = scan_photo(stream);

		metadata_exif::parse(md, info.metadata.exif);
		metadata_iptc::parse(md, info.metadata.iptc);
		metadata_xmp::parse(md, info.metadata.xmp);

		data[i] = v;
	}

	constexpr auto truncation_steps = 32;

	// Truncated loads
	for (auto i = 0u; i < truncation_steps && !df::is_closing; i++)
	{
		const auto save_path = _temps.next_path(ext);
		write_binary_file(save_path, data, df::mul_div(static_cast<int>(size), i, truncation_steps));
		ff_scan_and_load_thumb(ff, save_path);
	}
}

ui::const_image_ptr transform_jpeg(const ui::const_image_ptr& image, const simple_transform transform)
{
	df::assert_true(is_jpeg(image));

	jpeg_decoder decoder;
	jpeg_encoder encoder;
	return load_image_file(decoder.transform(image->data(), encoder, transform));
}

static void should_detect_duplicates(test_view::shared_test_context& stc)
{
	files ff;
	null_async_strategy as;
	location_cache locations;
	index_state index(as, locations);
	auto cache_path = _temps.next_path();
	database db(index);
	db.open(cache_path.folder(), cache_path.file_name_without_extension());
	build_index(index, db);

	const auto path1 = df::file_path(test_files_folder, u8"Test.jpg"sv);
	const auto path2 = df::file_path(test_files_folder, u8"Test90.jpg"sv);
	const auto path3 = df::file_path(test_files_folder, u8"Test180.jpg"sv);
	const auto path4 = df::file_path(test_files_folder, u8"Test270.jpg"sv);
	const auto path5 = df::file_path(test_files_folder, u8"Small.jpg"sv);
	const auto path_sony = df::file_path(test_files_folder, u8"Sony.jpg"sv);

	const auto test_item1 = std::make_shared<df::file_item>(path1, index.find_item(path1));
	const auto test_item2 = std::make_shared<df::file_item>(path2, index.find_item(path2));
	const auto test_item3 = std::make_shared<df::file_item>(path3, index.find_item(path3));
	const auto test_item4 = std::make_shared<df::file_item>(path4, index.find_item(path4));
	const auto test_item5 = std::make_shared<df::file_item>(path5, index.find_item(path5));
	const auto sony_item = std::make_shared<df::file_item>(path_sony, index.find_item(path_sony));

	df::item_set items({test_item1, test_item2, test_item3, test_item4, test_item5, sony_item});
	db.load_thumbnails(index, items);

	index.scan_item(test_item1, true, false);
	index.scan_item(test_item2, true, false);
	index.scan_item(test_item3, true, false);
	index.scan_item(test_item4, true, false);
	index.scan_item(test_item5, true, false);
	index.scan_item(sony_item, true, false);

	// Test duplicate counts
	index.update_predictions();

	assert_equal(1u, index.find_item(test_item1->path()).duplicates.count, u8"duplicates"sv);
	assert_equal(1u, index.find_item(test_item2->path()).duplicates.count, u8"duplicates"sv);
	assert_equal(1u, index.find_item(test_item3->path()).duplicates.count, u8"duplicates"sv);
	assert_equal(1u, index.find_item(sony_item->path()).duplicates.count, u8"duplicates"sv);
}

static void should_detect_rotation(test_view::shared_test_context& stc)
{
	files ff;
	const auto index_path = _temps.next_path();
	null_async_strategy as;
	location_cache locations;
	index_state index(as, locations);

	database db(index);
	db.open(index_path.folder(), index_path.file_name_without_extension());

	df::index_roots paths;
	paths.folders.emplace(test_files_folder);

	index.index_roots(paths);
	index.index_folders(test_token);

	const auto path_test = df::file_path(test_files_folder, u8"exif-rotated.jpg"sv);
	const auto test_item = std::make_shared<df::file_item>(path_test, index.find_item(path_test));

	assert_equal(ui::orientation::top_left, test_item->thumbnail_orientation());

	df::item_set items;
	items._items = {test_item};

	index.scan_items(items, false, false, false, false, test_token);
	db.perform_writes();

	assert_equal(ui::orientation::right_top, test_item->thumbnail_orientation());

	db.load_thumbnails(index, items);

	assert_equal(ui::orientation::right_top, test_item->thumbnail_orientation());

	index.scan_items(items, true, false, false, false, test_token);
	db.perform_writes();

	assert_equal(ui::orientation::right_top, test_item->thumbnail_orientation());
	assert_equal(ui::orientation::right_top, test_item->thumbnail()->orientation());
}

static void should_record_crashes()
{
	const auto db_path = _temps.next_path();
	const auto paths = {
		test_files_folder.combine_file(u8"Test.jpg"sv),
		test_files_folder.combine_file(u8"Test90.jpg"sv),
		test_files_folder.combine_file(u8"Small.jpg"sv),
		test_files_folder.combine_file(u8"Lossless0.jpg"sv)
	};

	for (auto path : paths)
	{
		{
			crash_files_db test_crash_files(db_path);

			assert_equal(test_crash_files.is_known_crash_file(path), false, u8"is_known_crash_file"sv);

			test_crash_files.add_open(path, str::utf8_cast(__FUNCTION__));
			test_crash_files.remove_open(path);
			test_crash_files.flush_open_files();
		}

		{
			crash_files_db test_crash_files(db_path);

			assert_equal(test_crash_files.is_known_crash_file(path), false, u8"is_known_crash_file"sv);

			test_crash_files.add_open(path, str::utf8_cast(__FUNCTION__));
			test_crash_files.flush_open_files();
		}

		{
			crash_files_db test_crash_files(db_path);

			assert_equal(test_crash_files.is_known_crash_file(path), true, u8"is_known_crash_file"sv);
		}
	}
}

//static void should_measure_text_consistently()
//{
//	const CClientDC dc(app_wnd());
//
//	const auto short_text = u8"Hide/Show Thumbnails\t"sv;
//	const auto font = render::style::font_size::dialog;
//	const auto style = render::style::text_style::multiline;
//
//	for (auto cx = 16; cx < 150; cx++)
//	{
//		const auto extent_text = render::measure(dc, short_text, font, style, cx);
//		const auto extent_text2 = render::measure(dc, short_text, font, style, extent_text.cx);
//
//		assert_equal(extent_text.cy, extent_text2.cy, str::format(u8"short text height at width {} ({} / {})"sv, cx, extent_text.cx, extent_text2.cx).c_str());
//	}
//
//	for (auto cx = 100; cx < 300; cx++)
//	{
//		const auto extent_text = render::measure(dc, long_text, font, style, cx);
//		const auto extent_text2 = render::measure(dc, long_text, font, style, extent_text.cx);
//
//		assert_equal(extent_text.cy, extent_text2.cy, str::format(u8"long text height at width {} ({} / {})"sv, cx, extent_text.cx, extent_text2.cx).c_str());
//	}
//}

static void should_parse_command_line()
{
	command_line_t cl1;
	cl1.parse(u8"-no-gpu"sv);

	assert_equal(true, cl1.no_gpu, u8"no_gpu"sv);
	assert_equal(false, cl1.no_indexing, u8"no_indexing"sv);

	command_line_t cl2;
	cl2.parse(test_files_folder.text());

	assert_equal(false, cl2.folder_path.is_empty(), u8"folder_path"sv);
	assert_equal(std::u8string_view{}, cl2.selection.name(), u8"selection name"sv);
	assert_equal(test_files_folder.text(), cl2.folder_path.folder().text(), u8"folder path"sv);
	assert_equal(false, cl2.no_gpu, u8"no_gpu"sv);
	assert_equal(false, cl2.no_indexing, u8"no_indexing"sv);


	const auto path3 = test_files_folder.combine_file(u8"test.jpg"sv);
	command_line_t cl3;
	cl3.parse(str::format(u8"{} -no-indexing"sv, path3));

	assert_equal(path3.folder().text(), cl3.folder_path.folder().text(), u8"folder_path"sv);
	assert_equal(path3.name(), cl3.selection.name(), u8"selection name"sv);
	assert_equal(path3.folder().text(), cl3.selection.folder().text(), u8"selection folder"sv);
	assert_equal(false, cl3.no_gpu, u8"no_gpu"sv);
	assert_equal(true, cl3.no_indexing, u8"no_indexing"sv);

	command_line_t cl4;
	cl4.parse(u8"--no-gpu \"C:\\Program Files\""sv);
	assert_equal(true, cl4.no_gpu, u8"no_gpu"sv);
	assert_equal(false, cl4.folder_path.is_empty(), u8"folder_path program Files"sv);

	command_line_t cl5;
	cl5.parse(u8"----- --no-gpu"sv);
	assert_equal(true, cl5.no_gpu, u8"no_gpu"sv);
}

static void should_trim_strings()
{
	assert_equal(u8"xxx"sv, str::trim_and_cache(u8"xxx\n"sv), u8"remove cr lf"sv);
	assert_equal(u8"xxx"sv, str::trim_and_cache(u8"\rxxx\r"sv), u8"remove lf"sv);
	assert_equal(u8"xxx"sv, str::trim_and_cache(u8"   xxx\t\t "sv), u8"remove space"sv);
}


static void should_format_text()
{
	assert_equal(u8"ac-dc"sv, str::format(u8"{2}{0}-{1}{0}"sv, u8"c"sv, u8"d"sv, u8"a"sv), u8"order"sv);
	assert_equal(u8"0.00123"sv, str::format(u8"{}"sv, 0.00123), u8"double"sv);
	assert_equal(u8"0.001"sv, str::format(u8"{:0.3}"sv, 0.00123), u8"double"sv);
	assert_equal(u8"5.5"sv, str::format(u8"{}"sv, 5.5000), u8"double"sv);
	assert_equal(u8"123"sv, str::format(u8"{}"sv, 123), u8"int"sv);
	assert_equal(u8"0123"sv, str::format(u8"{:04}"sv, 123), u8"int"sv);
	assert_equal(u8" 123"sv, str::format(u8"{:4}"sv, 123), u8"int"sv);
	assert_equal(u8"hex=7B"sv, str::format(u8"hex={:x}"sv, 0x7B), u8"hex"sv);
	assert_equal(u8"-test-"sv, str::format(u8"-{}-"sv, u8"test"sv), u8"char8_t*"sv);
	assert_equal(u8"-test-"sv, str::format(u8"-{}-"sv, std::u8string(u8"test"sv)), u8"string"sv);
	assert_equal(u8"-test-"sv, str::format(u8"-{}-"sv, std::u8string_view(u8"test"sv)), u8"string_view"sv);
	assert_equal(u8"33 {} {test}"sv, str::format(u8"{} {{}} {{test}}"sv, 33), u8"string_view"sv);
	assert_equal(u8"22 x 33"sv, str::format(u8"{} x {}"sv, 22, 33), u8"string_view"sv);
}

static std::u8string find_and_format_result(const std::u8string_view text, const std::u8string_view sub_string)
{
	const auto r = str::ifind2(text, sub_string, 0);
	auto result = std::u8string(text);

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
	assert_equal(u8"*white* on blond"sv, find_and_format_result(u8"white on blond"sv, u8"white"sv));
	assert_equal(u8"*whi*te on *bl*ond"sv, find_and_format_result(u8"white on blond"sv, u8"whi bl"sv));
	assert_equal(u8"*white* on *blond*"sv, find_and_format_result(u8"white on blond"sv, u8"white blond"sv));
	assert_equal(u8"*white* bl on *blond*"sv, find_and_format_result(u8"white bl on blond"sv, u8"white blond"sv));
}


static void should_scan_info_from_title()
{
	const auto ps1 = scan_info_from_title(u8"Game.of.Thrones.S02E06.HDTV.x264 - 2HD.mp4"sv);

	assert_equal(2, ps1.season, u8"season"sv);
	assert_equal(6, ps1.episode, u8"episode"sv);
	assert_equal(u8"Game of Thrones"sv, ps1.show, u8"show"sv);

	const auto ps2 = scan_info_from_title(u8"It's.a.Wonderful.Life.1946.720p.BluRay.x264.YIFY.mp4"sv);

	assert_equal(1946, ps2.year, u8"year"sv);
	assert_equal(u8"It's a Wonderful Life"sv, ps2.title, u8"title"sv);
}

static void should_compare_versions()
{
	const df::version current_version(s_app_version);
	assert_equal(s_app_version, current_version.to_string(), u8"Can parse and to_string current version"sv);

	const df::version test_version1(u8"123.45"sv);
	const df::version test_version1b(u8"123.45"sv);
	df::version test_version2(u8"456.1"sv);

	assert_equal(u8"123.45"sv, test_version1.to_string(), u8"Can parse and to_string test version 1"sv);
	assert_equal(u8"456.1"sv, test_version2.to_string(), u8"Can parse and to_string test version 2"sv);

	assert_equal(true, test_version1 < test_version2, u8"Less op version"sv);
	assert_equal(false, test_version2 < test_version1, u8"Less op version"sv);
	assert_equal(false, test_version1 == test_version2, u8"== op version"sv);
	assert_equal(true, test_version1 == test_version1b, u8"== op version"sv);

	assert_equal(u8"457.1"sv, (test_version2 + 1).to_string(), u8"+ op version"sv);
}


void test_view::run_test(const test_ptr& test)
{
	shared_test_context stc;
	test->perform(_state, stc);
	_host->frame()->invalidate();

	_state.queue_ui([this]()
	{
		sort();
		_temps.delete_temps();
	});
}

void test_view::run_tests()
{
	const auto tests = _tests;

	_state.queue_async(async_queue::work, [this, tests]()
	{
		shared_test_context stc;

		for (const auto& test : tests)
		{
			test->perform(_state, stc);
			_state.queue_ui([this]() { sort(); });
		}
	});

	_temps.delete_temps();
}

void test_view::register_tests()
{
	//
	// Infra
	//
	register_test(u8"Should calc HMAC SHA1"s, should_calc_HMACSHA1);
	register_test(u8"Should calc Hashes"s, should_calc_hashes);
	register_test(u8"Should convert Utf8"s, should_convert_utf8);
	register_test(u8"Should split"s, should_split);
	register_test(u8"Should detect wildcard"s, should_detect_wildcard);
	register_test(u8"Should match wildcard"s, should_match_wildcard);
	register_test(u8"Should handle international characters"s, should_handle_international_characters);
	register_test(u8"Should reset dx11"s, [this]
	{
		_state.queue_ui([this]
		{
			_state._events.free_graphics_resources(false, false);
			_host->frame()->reset_graphics();
		});
	});
	//register_test(u8"Should measure text consistently"sv, should_measure_text_consistently);
	register_test(u8"Should record crashes"s, should_record_crashes);
	register_test(u8"Should compare versions"s, should_compare_versions);
	register_test(u8"Should parse facebook Json"s, should_parse_facebook_json);
	register_test(u8"Should Encrypt Password"s, should_encrypt_password);
	register_test(u8"Should persist strings in registry"s, should_persist_to_registry);
	register_test(u8"Should parse selector"s, should_parse_selector);
	register_test(u8"Should select files"s, should_select_files);
	register_test(u8"Should parent"s, should_parent);
	register_test(u8"Should escape"s, should_escape);
	register_test(u8"Should scan info from title"s, should_scan_info_from_title);
	register_test(u8"Should parse command line"s, should_parse_command_line);
	register_test(u8"Should trim strings"s, should_trim_strings);
	register_test(u8"Should format text"s, should_format_text);
	register_test(u8"Should find text"s, should_find_text);
	register_test(u8"Should find Location"s, should_find_location);

	//
	// Scan Metadata
	//
	register_test(u8"Should scan jpg metadata"s, should_scan_jpeg);
	register_test(u8"Should scan avi metadata"s, should_scan_avi);
	register_test(u8"Should scan mov metadata"s, should_scan_mov);
	register_test(u8"Should scan mp3 metadata"s, should_scan_mp3);
	register_test(u8"Should scan mp4 metadata"s, should_scan_mp4);
	register_test(u8"Should scan raw metadata"s, should_scan_raw);
	register_test(u8"Should parse Xmp"s, should_parse_xmp);
	register_test(u8"Should merge Xmp sidecar"s, should_merge_xmp_sidecar);
	register_test(u8"Should replace tokens"s, should_replace_tokens);

	//
	// Modify media
	//
	const std::u8string_view common_files[] = {
		u8"Byzantium.avi"sv,
		u8"cherrys.psd"sv,
		u8"Colorblind.mp3"sv,
		u8"engine.png"sv,
		u8"Gherkin.CR2"sv,
		u8"gizmo.mp4"sv,
		u8"IMG_0096.JPG"sv,
		u8"cmyk.JPG"sv,
		u8"ipod.mov"sv,
		u8"jello.tif"sv,
		u8"StPauls.MOV"sv,
		u8"Test.jpg"sv,
		u8"tuesday.gif"sv,
		u8"lake.webp"sv,
	};

	for (auto name : common_files)
	{
		register_test(str::format(u8"Should update metadata {}"sv, name), [name] { should_update_metadata(name); });
		register_test(str::format(u8"Should update location {}"sv, name), [name] { should_update_location(name); });
		register_test(str::format(u8"Should update rating {}"sv, name), [name] { should_update_rating(name); });
		register_test(str::format(u8"Should update tags {}"sv, name), [name] { should_add_remove_tags(name); });
	}

	register_test(u8"Should update gps in exif"s, should_update_gps_in_exif);

	// 
	// Bitmap Edit
	//

	register_test(u8"Should preserve orientation"s, should_preserve_orientation);
	register_test(u8"Should resize"s, should_resize);
	register_test(u8"Should rotate"s, should_rotate);
	register_test(u8"Should rotate 133"s, should_rotate133);
	register_test(u8"Should rotate lossless"s, should_rotate_lossless);
	register_test(u8"Should save .png"s, [] { should_save(u8".png"sv, true); });
	register_test(u8"Should save .jpg"s, [] { should_save(u8".jpg"sv, true); });
	register_test(u8"Should save .webp"s, [] { should_save(u8".webp"sv, true); });
	register_test(u8"Should convert raw to jpeg"s, should_convert_raw_to_jpeg);

	//
	// UI
	//
	register_test(u8"Should select correctly"s, should_select_items);
	register_test(u8"Should Enable based on selection"s, should_enable_based_on_selection);
	register_test(u8"Should update exif rating"s, should_update_exif_rating);
	register_test(u8"Should update formatted description"s, should_update_formatted_text);
	register_test(u8"Should toggle rating"s, should_toggle_rating);


	//
	// Index
	//
	register_test(u8"Should index"s, should_index);
	register_test(u8"Should store thumbnails"s, should_store_thumbnails);
	register_test(u8"Should store item properties"s, should_store_item_properties);
	register_test(u8"Should store pack properties"s, should_pack_item_properties);
	register_test(u8"Should store webservice results"s, should_store_webservice_results);
	register_test(u8"Should detect duplicates"s, should_detect_duplicates);
	register_test(u8"Should Rename"s, should_rename);
	register_test(u8"Should Rename with substitutions"s, should_rename_with_substitutions);
	register_test(u8"Should not overwrite during rename"s, should_not_overwrite_during_rename);
	register_test(u8"Should detect original path"s, should_detect_original_path);
	register_test(u8"Should not reload thumb when valid"s, should_not_reload_thumb_when_valid);
	register_test(u8"Should reload thumb after scan"s, should_reload_thumb_after_scan);
	register_test(u8"Should copy preserve properties"s, should_copy_preserve_properties);
	register_test(u8"Should detect rotation"s, should_detect_rotation);


	//
	// Search
	//
	register_test(u8"Should parse search"s, should_parse_search);
	register_test(u8"Should parse search input"s, should_parse_search_input);
	register_test(u8"Should parse searches"s, should_parse_searches);
	register_test(u8"Should next date search"s, should_next_date_search);


	const auto alfie = u8"Alfie"_c;
	const auto jana = u8"Jana"_c;
	const auto app_data_folder = known_path(platform::known_folder::app_data);

	auto register_assert_format = [this](const df::search_t& query)
	{
		register_test(str::format(u8"Should format {}"sv, query.text()), [query](shared_test_context& stc)
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

	auto register_assert_parse = [this](const std::u8string& query, const std::u8string& expected = {})
	{
		register_test(str::format(u8"Should parse {}"sv, query), [query, expected](shared_test_context& stc)
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

	register_test(u8"Should match terms"s, should_match_terms);
	register_test(u8"Should match related"s, should_match_related);
	register_test(u8"Should not match folder without "s, should_not_match_folder_without);
	register_test(u8"Should match date"s, [] { should_match_date(u8"2012-09-14"sv, df::date_t(2012, 9, 14)); });
	register_test(u8"Should match date"s, [] { should_match_date(u8"2012"sv, df::date_t(2012, 1, 14)); });
	register_test(u8"Should match date"s, [] { should_match_date(u8"2012|2013"sv, df::date_t(2012, 1, 14)); });
	register_test(u8"Should match date"s, []
	{
		should_match_date(u8"(April or June) (2013 or 2015)"s, df::date_t(2013, 4, 22));
	});
	register_test(u8"Should match date"s, []
	{
		should_match_date(u8"(April or June) (2013 or 2015)"s, df::date_t(2015, 6, 7));
	});
	register_test(u8"Should match date"s, [] { should_match_date(u8"age:4"sv, df::date_t(1999, 12, 30)); });

	auto register_should_search = [this](const std::u8string_view query, int expected)
	{
		register_test(str::format(u8"Should search {}"s, query), [query, expected](shared_test_context& stc)
		{
			stc.lazy_load_index();
			assert_equal(expected, count_search_results(stc.index, query), query);
		});
	};

	register_should_search(u8"2012-09-14"sv, 5);
	register_should_search(u8"Created:2012-09-14"sv, 5);
	register_should_search(u8"2010-05-25"sv, 0);
	register_should_search(u8"2010-5-25"sv, 0);
	register_should_search(u8"Created:2010-05-25"sv, 0);
	register_should_search(u8"2009-11-15"sv, 1);

	register_should_search(u8"@video"sv, 4);
	register_should_search(u8"@audio"sv, 4);
	register_should_search(u8"@photo"sv, 26);

	register_should_search(u8"@ photo"sv, 26);
	register_should_search(u8"@   photo"sv, 26);

	register_should_search(u8"key1"sv, 4);
	register_should_search(u8"Tag:key1"sv, 4);
	register_should_search(u8"Tag :key1"sv, 4);
	register_should_search(u8"Tag: key1"sv, 4);
	register_should_search(u8"#key1"sv, 4);
	register_should_search(u8"# key1"sv, 4);
	register_should_search(u8"Tag:dog Tag:london"sv, 1);
	register_should_search(u8"not_exist"sv, 0);
	register_should_search(u8"Tag:not_exist"sv, 0);
	register_should_search(u8"#ke*"sv, 4);
	register_should_search(u8"ke*"sv, 5);

	register_should_search(u8"Megapixels:1.6 dog"sv, 1);
	register_should_search(u8"Megapixels:1.6"sv, 2);
	register_should_search(u8"(< Megapixels:1.0 > Megapixels:0.5)"sv, 7);
	register_should_search(u8"Megapixels:2"sv, 1);
	register_should_search(u8"pixels:2"sv, 1);
	register_should_search(u8"> pixels:1"sv, 8);
	register_should_search(u8">pixels:1"sv, 8);
	register_should_search(u8">pixels :1"sv, 8);
	register_should_search(u8">pixels: 1"sv, 8);
	register_should_search(u8"> pixels : 1"sv, 8);
	register_should_search(u8"2mp"sv, 1);
	register_should_search(u8"6000x4000"sv, 1);

	register_should_search(u8"Aperture:f/5"sv, 1);
	register_should_search(u8"f/3.5"sv, 6);
	register_should_search(u8"f/1.8"sv, 0);
	register_should_search(u8"f/4.0"sv, 0);
	register_should_search(u8"f/6.3"sv, 5);

	register_should_search(u8"with:Exposure @photo"sv, 14);
	register_should_search(u8"with: Exposure @ photo"sv, 14);
	register_should_search(u8"without:Exposure @photo"sv, 12);
	register_should_search(u8"without:Exposure"sv, 22);
	register_should_search(u8"with:Exposure"sv, 15);
	register_should_search(u8"with: Exposure"sv, 15);
	register_should_search(u8"ExposureTime:1/20s"sv, 1);
	register_should_search(u8"ExposureTime: 1/20s"sv, 1);
	register_should_search(u8"1/20s"sv, 1);
	register_should_search(u8"1/100s"sv, 5);
	register_should_search(u8"1/1000s"sv, 0);

	register_should_search(u8"iso400 @photo"sv, 2);
	register_should_search(u8"iso:400 @photo"sv, 2);
	register_should_search(u8"iso: 400 @photo"sv, 2);
	register_should_search(u8"iso : 400 @photo"sv, 2);
	register_should_search(u8">= iso:400 @photo"sv, 3);

	register_should_search(u8"1:26"sv, 1);
	register_should_search(u8"0:10"sv, 5);
	register_should_search(u8"7:77"sv, 0);
	register_should_search(u8"10:00"sv, 0);

	register_should_search(u8"size:0.3mb"sv, 4);
	register_should_search(u8"size:14kb"sv, 1);
	register_should_search(u8"size:5.1mb"sv, 1);
	register_should_search(u8">size:1mb"sv, 8);

	register_should_search(u8"key1"sv, 4);
	register_should_search(u8"dog london"sv, 1);
	register_should_search(u8"prague"sv, 5);
	register_should_search(u8"ipad"sv, 1);
	register_should_search(u8"48kHz"sv, 2);
	register_should_search(u8"44.1kHz"sv, 4);
	register_should_search(u8"tag:dog tag:london"sv, 1);
	register_should_search(u8"dog or london"sv, 4);
	register_should_search(u8"Rock"sv, 3);
	register_should_search(u8"canon @photo"sv, 10);
	register_should_search(u8"nikon d100"sv, 1);

	register_should_search(u8"ext:cr2"sv, 2);
	register_should_search(u8"ext:.cr2"sv, 2);


	// Dont run slow tests in debug

#ifndef _DEBUG

	register_test(u8"Should Find Closest Location"s, should_find_closest_location);
	register_test(u8"Should not crash on JPEG"s, [] { should_not_crash(u8"small.jpg"sv); });
	register_test(u8"Should not crash on GIF"s, [] { should_not_crash(u8"tuesday.gif"sv); });
	register_test(u8"Should not crash on TIFF"s, [] { should_not_crash(u8"small.tif"sv); });
	register_test(u8"Should not crash on PNG"s, [] { should_not_crash(u8"cube.png"sv); });
	//register_test(u8"Should not crash on PSD"sv, [] { should_not_crash(u8"cherrys.psd"sv); });

#endif

	sort();
}
