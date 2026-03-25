// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Main application implementation. Handles app initialization, window management,
// command processing, toolbar/menu creation, and coordinates all background worker threads.

#include "pch.h"

#include "app_text.h"
#include "util_zip.h"

#include "model_location.h"
#include "model_db.h"
#include "model_index.h"
#include "model.h"

#include "ui_dialog.h"
#include "util_crash_files_db.h"
#include "util_log.h"
#include "metadata_xmp.h"
#include "view_test.h"
#include "view_items.h"
#include "view_edit.h"
#include "view_media.h"
#include "view_import.h"
#include "view_locate.h"
#include "view_rename.h"
#include "view_sync.h"

#include "app_sidebar.h"
#include "app_commands.h"
#include "app_command_line.h"
#include "app.h"

#include <utility>

#include "app_command_status.h"
#include "util_spell.h"
#include "app_match.h"


command_line_t command_line;

auto s_app_name_l = L"Diffractor";
const auto s_app_name = u8"Diffractor"sv;
const auto s_app_version = u8"126.2"sv;
const auto g_app_build = u8"1206"sv;
constexpr auto stage_file_name = u8"diffractor-setup-update.exe"sv;
static constexpr auto installed_file_name = u8"diffractor-setup-installed.exe"sv;
static constexpr auto s_search = u8"search"sv;

extern void start_worker(platform::task_queue& q, std::u8string_view name);

std::u8string df::format_version(const bool short_text)
{
	if (short_text)
	{
		return str::format(u8"{}.{}"sv, s_app_version, g_app_build);
	}

	return str::format(u8"{}: {}.{}  |  {}"sv, tt.version, s_app_version, g_app_build, str::utf8_cast(__DATE__));
};

bool is_app_installed()
{
	const auto running_folder = platform::running_app_path().folder();
	const auto install_folder = known_path(platform::known_folder::app_data);
	return running_folder == install_folder;
}

struct app_updates_and_location_params
{
	gps_coordinate gps = setting.default_location;
	std::u8string city;
	std::u8string country;
	std::u8string version;
	std::u8string test_version = setting.available_test_version;
	bool should_update = false;

	void apply(const app_frame_ptr& app, const view_state& s) const
	{
		df::assert_true(ui::is_ui_thread());

		setting.default_location = gps;
		setting.available_version = version;
		setting.available_test_version = test_version;

		s.invalidate_view(view_invalid::app_layout);

#ifndef WINSTORE
		if (setting.install_updates && should_update && is_app_installed())
		{
			app->stage_update();
		}
#endif

		app->save_options(true);
	}
};


static gps_coordinate parse_coordinates(const std::u8string_view text, const gps_coordinate def_coords)
{
	const auto splits = str::split(text, true);

	if (splits.size() == 2)
	{
		const auto latitude = splits[0];
		const auto longitude = splits[1];

		if (!longitude.empty() && !latitude.empty())
		{
			return {str::to_double(latitude), str::to_double(longitude)};
		}
	}

	return def_coords;
}

static void check_for_updates_and_location(const app_frame_ptr& app, view_state& s)
{
	log_func lf(__FUNCTION__);
	platform::set_thread_description(u8"CheckForUpdates"sv);
	platform::thread_init c;

	if (platform::is_online())
	{
#ifndef WINSTORE
		if (setting.first_run_today && setting.check_for_updates)
		{
			s.queue_async(async_queue::web, [app, &s]
			{
				platform::web_request req;
				req.path = u8"/ver"sv;
				req.query = platform::web_params{
					{u8"v"s, std::u8string(s_app_version)},
					{u8"b"s, std::u8string(g_app_build)},
					{u8"f"s, setting.first_run_ever ? u8"1"s : u8"0"s},
					{u8"ft"s, str::to_hex(setting.features_used_since_last_report)},
					{u8"i"s, str::to_hex(setting.instantiations)},
					{u8"os"s, platform::OS()},
				};

				const auto con = platform::connect_to_host(u8"diffractor.com"sv);
				const auto response = send_request(con, req);

				if (response.status_code == 200)
				{
					df::util::json::json_doc json;
					json.Parse(response.body);

					app_updates_and_location_params params;
					params.version = df::util::json::safe_string(json, u8"current_version");
					params.test_version = df::util::json::safe_string(json, u8"test_version");
					params.should_update = str::icmp(df::util::json::safe_string(json, u8"action"), u8"update"sv) == 0;

					params.city = df::util::json::safe_string(json, u8"city");
					params.country = df::util::json::safe_string(json, u8"country");
					params.gps = parse_coordinates(df::util::json::safe_string(json, u8"latlon"), params.gps);

					setting.first_run_ever = false;
					setting.features_used_since_last_report = 0;

					s.queue_ui([params, app, &s]
					{
						app->save_options(true);
						params.apply(app, s);
					});
				}
			});
		}
#endif
		spell.lazy_download(s._async);
	}
}

crash_files_db crash_files(df::probe_data_file(u8"diffractor-files-that-crash.txt"sv));

void flush_open_files_to_crash_files_list()
{
	crash_files.flush_open_files();
}

void log_open_files_to_crash_files_list()
{
	crash_files.log_open_files();
}

std::vector<std::pair<std::u8string_view, std::u8string>> calc_app_info(const index_state& index,
                                                                        const bool include_state)
{
	std::vector<std::pair<std::u8string_view, std::u8string>> result;
	auto arch = u8"32-bit"sv;
	auto config = u8"release"sv;

#ifdef _M_X64
	arch = u8"64-bit"sv;
#endif

#ifdef _DEBUG
	config = u8"debug"sv;
#endif //_DEBUG

	const auto seconds_running = platform::now().to_seconds() - df::start_time.to_seconds();

	result.emplace_back(u8"Version:"sv, df::format_version(true));
	result.emplace_back(u8"Windows:"sv, str::format(u8"{} {} {}"sv, platform::OS(), arch, config));
	result.emplace_back(u8"Id:"sv, str::to_hex(crypto::crc32c(platform::user_name()), false));
	result.emplace_back(u8"Running:"sv, str::format(u8"{} seconds"sv, seconds_running));
	int64_t current, peak;

	if (platform::working_set(current, peak))
	{
		result.emplace_back(u8"Memory:"sv,
		                    str::format(u8"{} (peak {})"sv, df::file_size(current), df::file_size(peak)));
	}

	result.emplace_back(u8"Static Memory:"sv, df::file_size(platform::static_memory_usage).str());

	result.emplace_back(u8"GPU:"sv, df::gpu_desc);
	result.emplace_back(u8"GPU Id:"sv, df::gpu_id);
	result.emplace_back(u8"D3D:"sv, df::d3d_info);

	result.emplace_back(u8"Indexed items:"sv, str::to_string(index.stats.index_item_count));
	result.emplace_back(u8"Indexed folders:"sv, str::to_string(index.stats.index_folder_count));
	result.emplace_back(u8"Duplicates:"sv,
	                    str::format(u8"g={} mcomp={}"sv, index.stats.indexed_dup_folder_count,
	                                index.stats.indexed_max_compare_count));
	result.emplace_back(u8"Hashes:"sv,
	                    str::format(u8"crc={}"sv, index.stats.indexed_crc_count));
	result.emplace_back(u8"DB size:"sv, index.stats.database_size.str());
	result.emplace_back(u8"Saved:"sv, str::format(u8"{} items | {} thumbs"sv, index.stats.items_saved,
	                                              index.stats.thumbs_saved));


	result.emplace_back(u8"Index load:"sv, str::format(u8"{} ms"sv, index.stats.index_load_ms));
	result.emplace_back(u8"Predictions:"sv, str::format(u8"{} ms"sv, index.stats.predictions_ms));
	result.emplace_back(u8"Count Matches:"sv, str::format(u8"{} ms"sv, index.stats.count_matches_ms));

	if (include_state)
	{
		result.emplace_back(u8"Jobs running: "sv, str::to_string(df::jobs_running));
		result.emplace_back(u8"Is indexing: "sv, str::to_string(index.indexing));
		result.emplace_back(u8"Is searching: "sv, str::to_string(index.searching));
		result.emplace_back(u8"Is command active: "sv, str::to_string(df::command_active));
		result.emplace_back(u8"Is closing: "sv, str::to_string(df::is_closing));
		result.emplace_back(u8"Rendering function: "sv, str::utf8_cast(df::rendering_func));
	}

	return result;
}

void sidebar_logo_element::tooltip(view_hover_element& hover, const pointi loc, const pointi element_offset) const
{
	hover.elements->add(std::make_shared<text_element>(_text, ui::style::font_face::title,
	                                                   ui::style::text_style::single_line,
	                                                   view_element_style::line_break));
	hover.elements->add(std::make_shared<text_element>(df::format_version(false), ui::style::font_face::dialog,
	                                                   ui::style::text_style::single_line,
	                                                   view_element_style::line_break));
	//hover.elements.add(std::make_shared<text_element>(tt.indexed_locations_makes_collection, render::style::font_size::dialog, render::style::text_style::multiline, view_element_style::line_break));

	if (setting.show_debug_info)
	{
		const auto table = std::make_shared<ui::table_element>(view_element_style::center);

		for (const auto& i : calc_app_info(_state.item_index, true))
		{
			table->add(i.first, i.second);
		}

		hover.elements->add(table);
	}

	hover.elements->add(std::make_shared<action_element>(tt.help_more_info));
	hover.active_bounds = hover.window_bounds = bounds.offset(element_offset);
}

void app_frame::app_fail(const std::u8string_view message, const std::u8string_view more_text)
{
	auto message_s = std::u8string(message);
	auto more_text_s = std::u8string(more_text);

	queue_ui([message_s, more_text_s, parent = _app_frame]
	{
		const auto dlg = make_dlg(parent);

		std::vector<view_element_ptr> controls;
		controls.emplace_back(set_margin(std::make_shared<ui::title_control>(icon_index::error, tt.title_error)));
		controls.emplace_back(set_margin(std::make_shared<text_element>(tt.error_cannot_continue)));
		if (!message_s.empty()) controls.emplace_back(set_margin(std::make_shared<text_element>(message_s)));
		if (!more_text_s.empty()) controls.emplace_back(set_margin(std::make_shared<text_element>(more_text_s)));
		controls.emplace_back(std::make_shared<ui::close_control>(dlg->_frame));

		dlg->show_modal(controls);

		if (parent)
		{
			parent->close();
		}
	});
}

#ifndef WINSTORE
void app_frame::stage_update()
{
	static bool first_time_this_instance = true;

	if (first_time_this_instance)
	{
		first_time_this_instance = false; // Prevent multiple downloads per instance

		if (setting.install_updates && setting.first_run_today)
		{
			auto download_complete = [this](const df::file_path download_path)
			{
				const auto stage_path = known_path(platform::known_folder::app_data).combine_file(stage_file_name);

				if (download_path.exists())
				{
					platform::move_file(download_path, stage_path, false);
				}
			};

			queue_async(async_queue::web, [download_complete]
			{
				platform::download_and_verify(download_complete);
			});
		}
	}
}
#endif

void media_view::update_media_elements()
{
	df::assert_true(ui::is_ui_thread());
	const auto display = _state.display_state();
	_display = display;
	view_element_ptr media_element;

	if (display)
	{
		if (display->is_one())
		{
			const auto& item = display->_item1;
			const auto* const file_type = item->file_type();

			if (file_type->has_trait(file_traits::bitmap))
			{
				media_element = std::make_shared<photo_control>(_state, display, _host, false);
			}
			else if (file_type->has_trait(file_traits::visualize_audio))
			{
				media_element = std::make_shared<audio_control>(_state, display, _host);
			}
			else if (file_type->has_trait(file_traits::av))
			{
				media_element = std::make_shared<video_control>(_state, display, _host);
			}
			else if (file_type->has_trait(file_traits::archive))
			{
				media_element = std::make_shared<file_list_control>(display, view_element_style::center);
			}
			else
			{
				// std::make_shared<hex_control>(df::blob::from_file(item->path()), view_element_style::center);
			}
		}
		else if (display->is_two())
		{
			media_element = std::make_shared<side_by_side_control>(display);
		}
		else if (display->is_multi())
		{
			media_element = std::make_shared<images_control2>(_state, display);
		}
	}

	std::swap(_media_element, media_element);
	_controls_element = _state.create_selection_controls();

	if (_controls_element)
	{
		_controls_element->set_style_bit(view_element_style::dark_background, true);
	}
}

#ifndef WINSTORE
static bool install_update_if_exists()
{
	const auto module_folder = known_path(platform::known_folder::running_app_folder);
	const auto stage_path = module_folder.combine_file(stage_file_name);
	const auto installed_path = module_folder.combine_file(installed_file_name);

	if (stage_path.exists())
	{
		df::log(__FUNCTION__, u8"Staged install found"sv);

		if (const auto move_file_result = platform::move_file(stage_path, installed_path, false); move_file_result.
			success())
		{
			if (const auto install_result = platform::install(installed_path, module_folder, true, true); install_result
				.success())
			{
			}
			else
			{
				df::log(__FUNCTION__, install_result.format_error());
			}
		}
		else
		{
			df::log(__FUNCTION__, move_file_result.format_error());
		}
	}

	return false;
}
#endif


void command_line_t::parse(const std::u8string_view command_line_text)
{
	const auto raw_cl = command_line_text;
	const auto trimmed_cl = str::trim(raw_cl);

	if (!trimmed_cl.empty())
	{
		for (const auto part : str::split(trimmed_cl, true, str::is_white_space))
		{
			const auto stripped = strip_quotes(part);

			if (!stripped.empty())
			{
				if ((stripped[0] == '-' || stripped[0] == '/') && stripped.size() > 1)
				{
					const auto op = stripped.substr(stripped[1] == '-' ? 2 : 1);
					no_gpu = str::icmp(op, u8"no-gpu"sv) == 0;
					no_indexing = str::icmp(op, u8"no-indexing"sv) == 0;
					run_tests = str::icmp(op, u8"run-tests"sv) == 0;

					if (str::icmp(op, u8"test"sv) == 0)
					{
						console_test = true;
					}
					else if (op.size() > 5 && str::icmp(op.substr(0, 5), u8"test:"sv) == 0)
					{
						console_test = true;
						test_filter = std::u8string(op.substr(5));
					}
				}
				else if (df::is_path(stripped))
				{
					const auto folder = df::folder_path(stripped);

					if (platform::exists(folder))
					{
						folder_path = folder;
						selection = {};
					}
					else
					{
						const auto file = df::file_path(stripped);

						if (platform::exists(file))
						{
							folder_path = file.folder();
							selection = file;
						}
					}
				}
			}
		}
	}
}

std::u8string command_line_t::format_restart_cmd_line() const
{
	std::u8string result;
	if (no_gpu) result += u8" -no-gpu"sv;
	if (no_indexing) result += u8" -no-indexing"sv;
	return result;
}

std::u8string format_plural_text(const plural_text& fmt, const std::u8string_view first_name, const int64_t count,
                                 const df::file_size size, const int64_t of_total)
{
	const std::u8string_view template_text = count == 1 ? fmt.one : fmt.plural;

	auto substitute = [&](u8ostringstream& result, const std::u8string_view token)
	{
		if (token == u8"first-name"sv) result << first_name;
		else if (token == u8"count"sv) result << platform::format_number(str::to_string(count));
		else if (token == u8"other"sv) result << platform::format_number(str::to_string(count - 1));
		else if (token == u8"total"sv) result << platform::format_number(str::to_string(of_total));
		else if (token == u8"size"sv) result << prop::format_size(size);
		else if (token.empty()) result << platform::format_number(str::to_string(count));
	};

	return str::replace_tokens(template_text, substitute);
}

std::u8string format_plural_text(const plural_text& fmt, const int64_t count, const int64_t of_total)
{
	return format_plural_text(fmt, {}, count, {}, of_total);
}

std::u8string format_plural_text(const plural_text& fmt, const df::item_set& items)
{
	const auto summary = items.summary();
	const auto total_items = summary.total_items() + summary.total_folders();
	return format_plural_text(fmt, items.first_name(), total_items.count, total_items.size, 0);
}

std::u8string format_plural_text(const plural_text& fmt, const std::vector<std::u8string>& result)
{
	return format_plural_text(fmt, result.front(), static_cast<int>(result.size()), {}, 0);
}

void rating_control::dispatch_event(const view_element_event& event)
{
	if (event.type == view_element_event_type::invoke)
	{
		auto dlg = make_dlg(event.host->owner());
		const auto results = std::make_shared<command_status>(_state._async, dlg, icon_index::star, tt.rate_title,
		                                                      _state.selected_count());
		_state.toggle_rating(results, {_item}, _hover_rating, event.host);
	}
}


void view_frame::update_status(const std::u8string_view title, const std::u8string_view text)
{
	if (_status_title != title || _status_text != text)
	{
		_status_title = title;
		_status_text = text;
		_state.invalidate_view(view_invalid::view_redraw);
	}
}

void view_frame::clear_status()
{
	update_status({}, {});
	_status_title.clear();
	_status_text.clear();
}

void view_frame::draw_status(ui::draw_context& dc) const
{
	if (!_status_title.empty() || !_status_text.empty())
	{
		constexpr auto padding = 8;
		constexpr auto title_font = ui::style::font_face::title;
		constexpr auto text_font = ui::style::font_face::dialog;

		const auto text_color = ui::color(dc.colors.foreground, dc.colors.alpha);
		const auto bg_color = ui::color(ui::style::color::important_background, dc.colors.alpha);
		const sizei avail_extent{_extent.cx / 2, _extent.cy / 2};

		const auto title_extent = dc.measure_text(_status_title, title_font, ui::style::text_style::multiline_center,
		                                          avail_extent.cx);
		const auto text_extent = dc.measure_text(_status_text, text_font, ui::style::text_style::multiline_center,
		                                         avail_extent.cx);

		const auto extent = sizei(std::max(title_extent.cx, text_extent.cx),
		                          title_extent.cy + text_extent.cy + padding);
		const auto bounds = center_rect(extent, recti(_extent));

		auto title_bounds = bounds;
		title_bounds.bottom = title_bounds.top + title_extent.cy;
		auto text_bounds = bounds;
		text_bounds.top = text_bounds.bottom - text_extent.cy;

		dc.draw_rect(bounds.inflate(padding), bg_color);
		dc.draw_text(_status_title, title_bounds, title_font, ui::style::text_style::multiline_center, text_color, {});
		dc.draw_text(_status_text, text_bounds, text_font, ui::style::text_style::multiline_center, text_color, {});
	}
}

app_frame::app_frame(ui::plat_app_ptr pa) :
	_player(std::make_shared<av_player>(*this)),
	_state(*this, *this, _item_index, _player),
	_edit_view_state(_state),
	_item_index(*this, _locations),
	_db(_state.item_index),
	_pa(std::move(pa))
{
	_sidebar = std::make_shared<sidebar_host>(_state);
	_view_frame = std::make_shared<view_frame>(_state);
	_view_test = std::make_shared<test_view>(_state, _view_frame);
	_view_sync = std::make_shared<sync_view>(_state, _view_frame);
	_view_import = std::make_shared<import_view>(_state, _view_frame);
	_view_locate = std::make_shared<locate_view>(_state, _view_frame);
	_view_rename = std::make_shared<rename_view>(_state, _view_frame);
	_view_items = std::make_shared<items_view>(_state, _view_frame);
	_view_edit = std::make_shared<edit_view>(_state, _view_frame, _edit_view_state);
	_view_media = std::make_shared<media_view>(_state, _view_frame);
}

app_frame::~app_frame()
{
	_threads.clear();
	_state.close();

	df::log(__FUNCTION__, u8"destruct"sv);
}

void app_frame::prepare_frame()
{
	df::assert_true(ui::is_ui_thread());
	const auto vf = _view_frame;

	if (!df::is_closing && vf)
	{
		//const auto displayFrequency = platform::FrameRate();
		//const auto animationDelay = std::chrono::milliseconds(1000 / ((displayFrequency > 60) ? displayFrequency / 2 : displayFrequency));
		//const auto audioDelay = std::chrono::milliseconds(1000 / ((displayFrequency > 30) ? displayFrequency / 2 : displayFrequency));
		//   const auto idleDelay = std::chrono::milliseconds(1000 / 10);
		//   auto delay = animationDelay;

		const auto time_now = df::now();
		const auto display_frequency = platform::display_frequency();
		const auto animation_delay_ms = 1000 / display_frequency;
		// std::clamp((display_frequency > 30 ? display_frequency / 2 : display_frequency), 20, 30);
		constexpr auto idle_delay_ms = 1000 / ui::default_ticks_per_second;

		//   while (!df::is_closing)
		//   {
		//	std::this_thread::sleep_for(delay);

		std::vector<void*> removals;
		auto is_animating = false;
		auto is_animating2 = false;

		for (const auto& i : ui::animations)
		{
			if (i.second())
			{
				is_animating2 = true;
			}
			else
			{
				removals.emplace_back(i.first);
			}
		}

		for (const auto& i : removals)
		{
			ui::animations.erase(i);
		}

		const auto display = _state.display_state();
		auto frame_delay = idle_delay_ms;

		if (display)
		{
			is_animating |= display->step();

			const auto prepare_result = display->update_for_present(time_now);

			if (prepare_result == render_valid::invalid || is_animating)
			{
				invalidate_status();
				vf->_frame->invalidate();
			}
			else if (prepare_result == render_valid::present)
			{
				vf->_frame->redraw();
			}

			if (is_animating2 || is_animating || display->is_playing_media())
			{
				frame_delay = animation_delay_ms;
			}
		}

		if (vf->is_occluded())
		{
			frame_delay = 200;
		}

		_search_color_lerp.target = _state.item_index.searching > 0 ? 255 : 0;

		if (_search_color_lerp.step())
		{
			_search_edit->set_background(
				_search_color_lerp.lerp(ui::style::color::edit_background, ui::style::color::important_background));
		}

		_frame_delay = frame_delay;
		_pa->frame_delay(frame_delay);
	}
}

void app_frame::invalidate_status() const
{
	const auto is_full_screen_media = _state.is_full_screen && _state.view_mode() == view_type::media;
	const auto show_status_bar = !is_full_screen_media;

	if (show_status_bar && setting.show_debug_info && !_status_bounds.is_empty())
	{
		_app_frame->invalidate(_status_bounds);
	}
}

void app_frame::update_overlay()
{
	if (_view_frame)
	{
		const bool show_overlays = _state.should_show_overlays();

		if (show_overlays != _view_frame->show_cursor())
		{
			_view_frame->show_cursor(show_overlays);
			_view_media->overlay_alpha_target = show_overlays ? 1.0f : 0.0f;

			ui::animations[this] = [v = _view_media, fv = _view_frame]
			{
				const auto dd = v->overlay_alpha_target - v->overlay_alpha;
				bool invalidate = false;

				if (fabs(dd) > 0.001f)
				{
					v->overlay_alpha += dd * 0.2345f;
					fv->invalidate_view(view_invalid::view_redraw);
					invalidate = true;
				}

				return invalidate;
			};

			if (!show_overlays)
			{
				_bubble->hide();
			}

			invalidate_view(view_invalid::animations);
		}
	}
}

void app_frame::tick()
{
	const auto display = _state.display_state();

	if (display && display->_session)
	{
		display->_session->adjust_volume();
	}

	if (_app_frame && _app_frame->is_visible())
	{
		const auto time_now = df::now();

		_state.tick(_view_frame, time_now);
		_view_frame->tick();

		free_graphics_resources(true, true);

		if (setting.show_debug_info && !_status_bounds.is_empty())
		{
			_app_frame->invalidate(_status_bounds);
		}

		update_overlay();
	}

	if (_search_predictions_frame && _search_predictions_frame->_frame)
	{
		if (!_search_has_focus && _search_predictions_frame->_frame->is_visible())
		{
			hide_search_predictions();
		}
	}

	if (_invalids != view_invalid::none)
	{
		idle();
	}
}

static constexpr auto s_recent_folders = u8"recent_folders"sv;
static constexpr auto s_recent_searches = u8"recent_searches"sv;
static constexpr auto s_recent_apps = u8"recent_apps"sv;
static constexpr auto s_recent_tags = u8"recent_tags"sv;
static constexpr auto s_recent_locations = u8"recent_locations"sv;
static constexpr auto s_group_order = u8"group_order"sv;
static constexpr auto s_sort_order = u8"sort_order"sv;

void app_frame::load_options(const platform::setting_file_ptr& store)
{
	if (store->root_created())
	{
		const str::cached default_folders[] =
		{
			known_path(platform::known_folder::pictures).text(),
			known_path(platform::known_folder::video).text(),
			known_path(platform::known_folder::music).text(),
			known_path(platform::known_folder::desktop).text(),
			known_path(platform::known_folder::downloads).text(),
		};

		for (const auto& f : default_folders)
		{
			_state.recent_folders.add(f);
			_state.recent_searches.add(f);
		}

		saved_current_search = known_path(platform::known_folder::pictures).text();
	}
	else
	{
		_state.recent_folders.read({}, s_recent_folders, store);
		_state.recent_searches.read({}, s_recent_searches, store);
		_state.recent_apps.read({}, s_recent_apps, store);
		_state.recent_tags.read({}, s_recent_tags, store);
		_state.recent_locations.read({}, s_recent_locations, store);

		auto group_order = static_cast<uint32_t>(group_by::file_type);
		auto sort_order = static_cast<uint32_t>(sort_by::name);
		store->read({}, s_group_order, group_order);
		store->read({}, s_sort_order, sort_order);
		_starting_group_order = static_cast<group_by>(group_order);
		_starting_sort_order = static_cast<sort_by>(sort_order);

		setting.read(store);
		store->read({}, s_search, saved_current_search);
	}
}

void app_frame::save_options(const bool search_only)
{
	auto store = platform::create_registry_settings();
	store->write({}, s_search, _state.search().text());
	store->write({}, s_group_order, static_cast<uint32_t>(_state.group_order()));
	store->write({}, s_sort_order, static_cast<uint32_t>(_state.sort_order()));

	if (_app_frame)
	{
		_app_frame->save_window_position(store);
	}

	if (!search_only)
	{
		_state.recent_folders.write({}, s_recent_folders, store);
		_state.recent_searches.write({}, s_recent_searches, store);
		_state.recent_apps.write({}, s_recent_apps, store);
		_state.recent_tags.write({}, s_recent_tags, store);
		_state.recent_locations.write({}, s_recent_locations, store);
		setting.write(store);
	}
}

void app_frame::search_complete(const df::search_t& search, const bool path_changed)
{
	if (path_changed)
	{
		save_options(true);
		invalidate_view(view_invalid::address);

		std::vector<df::folder_path> folders;

		for (const auto& s : search.selectors())
		{
			if (s.folder().exists())
			{
				folders.emplace_back(s.folder());
			}
		}

		_pa->monitor_folders(folders);
	}

	_view->items_changed(path_changed);

	_sidebar->update_current_search();
	_sidebar->invalidate();

	invalidate_view(view_invalid::item_scan |
		view_invalid::media_elements |
		view_invalid::group_layout |
		view_invalid::tooltip |
		view_invalid::command_state |
		view_invalid::app_layout);

	if (path_changed)
	{
		invalidate_view(view_invalid::focus_item_visible | view_invalid::presence);
	}
}

static recti calc_command_bounds(const ui::measure_context& mc, const sizei media_edit_commands_extent,
                                 const int y_status_top, const int status_height, const int client_bounds_right)
{
	const auto y_media_edit = y_status_top + (status_height - media_edit_commands_extent.cy) / 2;
	const auto x_media_edit = client_bounds_right - (mc.padding1 + media_edit_commands_extent.cx + mc.handle_cxy);

	return recti(x_media_edit, y_media_edit, x_media_edit + media_edit_commands_extent.cx,
	             y_media_edit + media_edit_commands_extent.cy);
}

void app_frame::layout(ui::measure_context& mc)
{
	if (!df::is_closing && _app_frame && _tools && !_extent.is_empty())
	{
		update_button_state(true);

		const auto view_mode = _state.view_mode();
		const auto is_items_or_media_view = _state.is_items_or_media_view();
		const auto is_command_view = !is_items_or_media_view;
		const auto is_full_screen_media = _state.is_full_screen && view_mode == view_type::media;
		const auto can_show_top_bar = is_command_view || !is_full_screen_media;
		const auto can_show_status_bar = !is_full_screen_media;
		const auto can_show_tools = is_items_or_media_view && can_show_status_bar;
		const auto can_show_filter = is_items_or_media_view && can_show_status_bar && view_mode != view_type::test;
		const auto can_show_sidebar = setting.show_sidebar && !is_full_screen_media && is_items_or_media_view;
		const auto can_show_view_controls = view_mode == view_type::edit ||
			view_mode == view_type::rename ||
			view_mode == view_type::sync ||
			view_mode == view_type::import ||
			view_mode == view_type::locate;

		const auto scale_factor = mc.scale_factor;
		const auto scale1 = df::round(1 * scale_factor);
		const auto scale16 = df::round(16 * scale_factor);
		const auto scale32 = df::round(32 * scale_factor);
		const auto scale64 = df::round(64 * scale_factor);
		const auto scale150 = df::round(150 * scale_factor);
		const auto scale300 = df::round(300 * scale_factor);
		const auto scale400 = df::round(400 * scale_factor);
		const auto scale1000 = df::round(1000 * scale_factor);

		const auto tools_extent = _tools->measure_toolbar(_extent.cx);
		const auto sorting_extent = _sorting->measure_toolbar(_extent.cx);
		const auto navigate1_extent = _navigate1->measure_toolbar(_extent.cx);
		const auto navigate2_extent = _navigate2->measure_toolbar(_extent.cx);
		const auto navigate3_extent = _navigate3->measure_toolbar(_extent.cx);
		const auto media_edit_commands_extent = _media_edit_commands->measure_toolbar(_extent.cx);
		const auto rename_commands_extent = _rename_commands->measure_toolbar(_extent.cx);
		const auto import_commands_extent = _import_commands->measure_toolbar(_extent.cx);
		const auto locate_commands_extent = _locate_commands->measure_toolbar(_extent.cx);
		const auto sync_commands_extent = _sync_commands->measure_toolbar(_extent.cx);
		const auto test_commands_extent = _test_commands->measure_toolbar(_extent.cx);

		const auto text_line_height = mc.text_line_height(ui::style::font_face::dialog);
		const auto cy_address = text_line_height + mc.padding2 + mc.padding2;
		const auto cy_filter = text_line_height + mc.padding2;

		const auto top_height = std::max(
			std::max(navigate1_extent.cy, navigate2_extent.cy),
			std::max(navigate3_extent.cy, cy_address)) + mc.padding2;
		const auto status_height = std::max(
			std::max(
				std::max(tools_extent.cy, sorting_extent.cy),
				std::max(media_edit_commands_extent.cy, cy_filter)),
			std::max(
				std::max(rename_commands_extent.cy, import_commands_extent.cy),
				std::max(std::max(sync_commands_extent.cy, test_commands_extent.cy),
				         locate_commands_extent.cy))) + mc.padding1;

		const auto client_bounds = recti(_extent).inflate(is_full_screen_media ? 0 : -scale1);
		const auto y_status_top = client_bounds.bottom - status_height;
		const auto y_address = client_bounds.top + (top_height - cy_address) / 2;
		const auto y_nav1 = client_bounds.top + (top_height - navigate1_extent.cy) / 2;
		const auto y_nav2 = client_bounds.top + (top_height - navigate2_extent.cy) / 2;
		const auto y_nav3 = client_bounds.top + (top_height - navigate3_extent.cy) / 2;
		const auto y_tools = y_status_top + (status_height - tools_extent.cy) / 2;
		const auto y_sorting = y_status_top + (status_height - sorting_extent.cy) / 2;
		const auto cx_avail = client_bounds.width();
		const auto sidebar_min = scale64;
		const auto sidebar_avail = std::max(sidebar_min, cx_avail / 3);
		const auto sidebar_cx = std::clamp(_sidebar->preferred_width(mc), sidebar_min, sidebar_avail);
		const auto toolbar_widths = navigate1_extent.cx + navigate2_extent.cx + navigate3_extent.cx;
		const auto cx_address = std::clamp((client_bounds.width() - toolbar_widths) / 2, scale300, scale1000);
		const auto x_address_center = (client_bounds.left + client_bounds.right) / 2;
		const auto x_address_left = x_address_center - cx_address / 2;
		const auto x_address_right = x_address_center + cx_address / 2;
		const auto x_nav1 = x_address_left - mc.padding1 - navigate1_extent.cx;
		const auto x_nav2 = x_address_right + mc.padding1;
		const auto x_nav3 = client_bounds.right - mc.padding1 - navigate3_extent.cx;
		const auto x_tools_avail = cx_avail - sidebar_cx;
		const auto x_tools = (can_show_sidebar ? sidebar_cx : 0) + (is_full_screen_media
			                                                            ? (x_tools_avail - tools_extent.cx) / 2
			                                                            : mc.padding1);
		_sorting_width = std::max(_sorting_width, sorting_extent.cx);
		// we want sorting width not to just grow not shrink.
		const auto x_sorting = client_bounds.right - (mc.padding1 + _sorting_width + mc.handle_cxy);
		const auto y_client = can_show_top_bar ? client_bounds.top + top_height : client_bounds.top;
		const auto cx_view_controls = std::max(client_bounds.width() / 4, scale400);
		const auto r_tools = x_tools + (can_show_tools ? tools_extent.cx : 0);
		const auto x_filter = std::max(r_tools + mc.padding1, x_sorting - scale150);
		const auto y_filter = y_status_top + (status_height - cy_filter) / 2;

		const recti address_bounds(x_address_left, y_address, x_address_right, y_address + cy_address);
		const recti nav1_bounds(x_nav1, y_nav1, x_nav1 + navigate1_extent.cx, y_nav1 + navigate1_extent.cy);
		const recti nav2_bounds(x_nav2, y_nav2, x_nav2 + navigate2_extent.cx, y_nav2 + navigate2_extent.cy);
		const recti nav3_bounds(x_nav3, y_nav3, x_nav3 + navigate3_extent.cx, y_nav3 + navigate3_extent.cy);
		const recti tool_rect(x_tools, y_tools, r_tools, y_tools + tools_extent.cy);
		const recti sorting_bounds(x_sorting, y_sorting, x_sorting + _sorting_width, y_sorting + sorting_extent.cy);
		const recti filter_rect(x_filter, y_filter, x_sorting - mc.padding1, y_filter + cy_filter);
		const recti view_controls_bounds(client_bounds.right - cx_view_controls, y_client, client_bounds.right,
		                                 y_status_top);
		const recti sidebar_bounds(client_bounds.left, client_bounds.top, client_bounds.left + sidebar_cx,
		                           client_bounds.bottom);

		const recti media_edit_bounds = calc_command_bounds(mc, media_edit_commands_extent, y_status_top, status_height,
		                                                    client_bounds.right);
		const recti rename_commands_bounds = calc_command_bounds(mc, rename_commands_extent, y_status_top,
		                                                         status_height, client_bounds.right);
		const recti import_commands_bounds = calc_command_bounds(mc, import_commands_extent, y_status_top,
		                                                         status_height, client_bounds.right);
		const recti locate_commands_bounds = calc_command_bounds(mc, locate_commands_extent, y_status_top,
		                                                         status_height, client_bounds.right);
		const recti sync_commands_bounds = calc_command_bounds(mc, sync_commands_extent, y_status_top, status_height,
		                                                       client_bounds.right);
		const recti test_commands_bounds = calc_command_bounds(mc, test_commands_extent, y_status_top, status_height,
		                                                       client_bounds.right);

		const auto show_sorting = view_mode == view_type::items && tool_rect.right < sorting_bounds.left;
		const auto show_tools = can_show_tools && tool_rect.right <= client_bounds.right;
		const auto show_filter = can_show_filter && filter_rect.width() > scale16;
		const auto show_address = can_show_top_bar && is_items_or_media_view && address_bounds.right < nav3_bounds.
			right;
		const auto show_navigate1 = can_show_top_bar && is_items_or_media_view && nav1_bounds.left > client_bounds.left;
		const auto show_navigate2 = can_show_top_bar && is_items_or_media_view && nav2_bounds.right < nav3_bounds.left;
		const auto show_navigate3 = can_show_top_bar;
		const auto show_media_edit_commands = view_mode == view_type::edit;
		const auto show_rename_commands = view_mode == view_type::rename;
		const auto show_import_commands = view_mode == view_type::import;
		const auto show_locate_commands = view_mode == view_type::locate;
		const auto show_sync_commands = view_mode == view_type::sync;
		const auto show_test_commands = view_mode == view_type::test;

		auto view_bounds = client_bounds;

		if (can_show_top_bar)
		{
			view_bounds.top += top_height;
		}

		if (can_show_sidebar)
		{
			view_bounds.left += sidebar_cx;
		}

		if (can_show_status_bar)
		{
			view_bounds.bottom = y_status_top;
		}

		if (can_show_view_controls)
		{
			view_bounds.right -= cx_view_controls;
		}

		_title_bounds = client_bounds;
		_title_bounds.top = client_bounds.top;
		_title_bounds.bottom = client_bounds.top + top_height;
		_title_bounds.left = (can_show_sidebar ? sidebar_bounds.right : client_bounds.left) + scale16;
		_title_bounds.right = nav3_bounds.left - scale16;

		ui::control_layouts positions;
		positions.emplace_back(_navigate1, nav1_bounds, show_navigate1);
		positions.emplace_back(_search_edit, address_bounds, show_address);
		positions.emplace_back(_navigate2, nav2_bounds, show_navigate2);
		positions.emplace_back(_navigate3, nav3_bounds, show_navigate3);
		positions.emplace_back(_sidebar->_frame, sidebar_bounds, can_show_sidebar);
		positions.emplace_back(_view_frame->_frame, view_bounds, _view->_show_render_window);
		positions.emplace_back(_tools, tool_rect, show_tools);
		positions.emplace_back(_filter_edit, filter_rect, show_filter);
		positions.emplace_back(_sorting, sorting_bounds, show_sorting);
		positions.emplace_back(_media_edit_commands, media_edit_bounds, show_media_edit_commands);
		positions.emplace_back(_rename_commands, rename_commands_bounds, show_rename_commands);
		positions.emplace_back(_import_commands, import_commands_bounds, show_import_commands);
		positions.emplace_back(_locate_commands, locate_commands_bounds, show_locate_commands);
		positions.emplace_back(_sync_commands, sync_commands_bounds, show_sync_commands);
		positions.emplace_back(_test_commands, test_commands_bounds, show_test_commands);

		if (_view_controls && _view_controls->_dlg)
		{
			positions.emplace_back(_view_controls->_dlg, view_controls_bounds, can_show_view_controls);
		}

		if (_search_predictions_frame && _search_predictions_frame->_frame->is_visible())
		{
			_search_predictions_frame->_frame->window_bounds(calc_search_popup_bounds(), true);
		}

		_view_bounds = view_bounds;
		_app_frame->apply_layout(positions, {0, 0});
		_status_bounds.set(tool_rect.right, view_bounds.bottom, sorting_bounds.left, client_bounds.bottom);
	}
}


void parse_more_folders(df::index_roots& result, const std::u8string_view more_folders)
{
	df::hash_set<std::u8string, df::ihash, df::ieq> drive_label_includes;

	for (auto line : split_collection_folders(more_folders))
	{
		if (!str::is_empty(line))
		{
			if (str::is_exclude(line))
			{
				while (line.size() > 0_z && (str::is_white_space(line.front()) || line.front() == '-'))
					line = line.
						substr(1);

				if (df::is_path(str::trim(line)))
				{
					const auto path = df::folder_path(line);

					if (path.exists() ||
						platform::is_server(line))
					{
						result.excludes.emplace(path);
					}
				}
				else
				{
					result.exclude_wildcards.emplace(str::cache(line));
				}
			}
			else
			{
				const auto path = df::folder_path(line);

				if (path.exists() ||
					platform::is_server(line))
				{
					result.folders.emplace(path);
				}
				else
				{
					drive_label_includes.emplace(line);
				}
			}
		}
	}

	const auto drives = platform::scan_drives(false);

	for (const auto& d : drives)
	{
		const auto path = df::folder_path(d.name);
		const auto vol = d.vol_name;

		if (drive_label_includes.contains(d.name))
		{
			result.folders.emplace(path);
		}
	}

	for (auto exclude : result.excludes)
	{
		result.folders.erase(exclude);
	}
}


static bool pop_invalid_flag(std::atomic<view_invalid>& invalids, const view_invalid flag)
{
	auto expected = invalids.load();
	auto updated = static_cast<view_invalid>(static_cast<uint32_t>(expected) & ~static_cast<uint32_t>(flag));

	while (!invalids.compare_exchange_weak(expected, updated))
	{
		updated = static_cast<view_invalid>(static_cast<uint32_t>(expected) & ~static_cast<uint32_t>(flag));
	}

	return (static_cast<uint32_t>(expected) & static_cast<uint32_t>(flag)) != 0;
}

void app_frame::complete_pending_events()
{
	try
	{
		if (!df::is_closing && _app_frame)
		{
			const auto tasks = _ui_queue.dequeue_all();

			for (const auto& t : tasks)
			{
				t();
			}

			if (pop_invalid_flag(_invalids, view_invalid::font_size))
			{
				update_font_size();
			}

			if (pop_invalid_flag(_invalids, view_invalid::status))
			{
				_app_frame->invalidate(_status_bounds);
			}

			if (pop_invalid_flag(_invalids, view_invalid::filters))
			{
				if (_filter_edit->window_text() != _state.filter().text())
				{
					_filter_edit->window_text(_state.filter().text());
				}
			}

			if (pop_invalid_flag(_invalids, view_invalid::animations))
			{
				prepare_frame();
			}

			if (pop_invalid_flag(_invalids, view_invalid::options_save))
			{
				update_command_text();
				save_options();

				_view_frame->_frame->options_changed();
				_view_edit->options_changed();
				_navigate1->options_changed();
				_navigate2->options_changed();
				_navigate3->options_changed();
				_media_edit_commands->options_changed();
				_search_edit->options_changed();
				_filter_edit->options_changed();
				_tools->options_changed();
				_sorting->options_changed();

				_state.update_search_is_favorite_or_collection_root();

				if (_search_predictions_frame && _search_predictions_frame->_frame)
				{
					_search_predictions_frame->_frame->options_changed();
				}
			}

			if (pop_invalid_flag(_invalids, view_invalid::refresh_items))
			{
				if (df::file_handles_detached == 0 && df::command_active == 0)
				{
					_view->refresh();
				}
				else
				{
					// re-invalidate for later
					invalidate_view(view_invalid::refresh_items);
				}
			}

			if (pop_invalid_flag(_invalids, view_invalid::selection_list))
			{
				_state.update_selection();
			}

			if (pop_invalid_flag(_invalids, view_invalid::image_compare))
			{
				_state.update_pixel_difference();
			}

			if (pop_invalid_flag(_invalids, view_invalid::screen_saver))
			{
				const auto display = _state.display_state();
				const auto is_playing = display && display->is_playing();
				_pa->enable_screen_saver(!_state.is_full_screen && !is_playing);
			}

			if (pop_invalid_flag(_invalids, view_invalid::sidebar))
			{
				_sidebar->populate();
				_sidebar->layout();
			}

			if (pop_invalid_flag(_invalids, view_invalid::sidebar_file_types_and_dates))
			{
				_sidebar->populate_file_types_and_dates();
				_sidebar->layout();
			}

			if (pop_invalid_flag(_invalids, view_invalid::index_summary))
			{
				_state.item_index.queue_update_predictions();
				_state.item_index.queue_update_summary();
			}

			if (pop_invalid_flag(_invalids, view_invalid::media_elements))
			{
				_view->update_media_elements();
				invalidate_view(view_invalid::view_layout | view_invalid::view_redraw | view_invalid::controller);
			}

			if (pop_invalid_flag(_invalids, view_invalid::command_state))
			{
				update_button_state(false);
			}

			if (pop_invalid_flag(_invalids, view_invalid::item_scan))
			{
				_item_index.queue_scan_listed_items(_state.display_items());
			}

			if (pop_invalid_flag(_invalids, view_invalid::presence))
			{
				_item_index.queue_update_presence(_state.display_items());
			}

			if (pop_invalid_flag(_invalids, view_invalid::group_layout))
			{
				_state.update_item_groups();
				_view->items_changed(false);
				invalidate_view(view_invalid::view_layout | view_invalid::controller);
			}

			if (pop_invalid_flag(_invalids, view_invalid::address))
			{
				update_address();
				_state.update_search_is_favorite_or_collection_root();
			}

			if (pop_invalid_flag(_invalids, view_invalid::app_layout))
			{
				_app_frame->layout();
			}

			if (pop_invalid_flag(_invalids, view_invalid::view_layout))
			{
				if (_view_frame)
				{
					_view_frame->layout();
				}
			}

			if (pop_invalid_flag(_invalids, view_invalid::index))
			{
				update_index();
			}

			if (pop_invalid_flag(_invalids, view_invalid::focus_item_visible))
			{
				if (_state.focus_item())
				{
					make_visible(_state.focus_item());
				}
			}

			if (pop_invalid_flag(_invalids, view_invalid::view_redraw))
			{
				_view_frame->_frame->invalidate();
			}

			if (pop_invalid_flag(_invalids, view_invalid::controller))
			{
				// needs to be last to allow layout
				_view_frame->invalidate_controller();
			}

			if (pop_invalid_flag(_invalids, view_invalid::tooltip))
			{
				update_tooltip();
			}
		}
	}
	catch (std::exception& e)
	{
		df::log(__FUNCTION__, e.what());
	}
}

void app_frame::idle()
{
	update_overlay();
	complete_pending_events();
}

bool app_frame::key_down(const char32_t key, const ui::key_state keys)
{
	if (df::command_active == 0)
	{
		if (_search_has_focus)
		{
			if (key == keys::UP)
			{
				df::scope_locked_inc l(_pin_search);
				_search_predictions_frame->step_selection(-1);
				return true;
			}
			if (key == keys::DOWN)
			{
				df::scope_locked_inc l(_pin_search);
				_search_predictions_frame->step_selection(1);
				return true;
			}
			if (key == keys::RETURN)
			{
				search_enter();
				return true;
			}
			if (key == keys::ESCAPE)
			{
				hide_search_predictions();
				focus_view();
				invalidate_view(view_invalid::address);
				return true;
			}
		}
		else if (_filter_has_focus)
		{
			if (key == keys::RETURN || key == keys::ESCAPE)
			{
				focus_view();
				return true;
			}
		}

		if (!_view_controls_have_focus && _state.is_items_or_media_view())
		{
			if (key == keys::APPS)
			{
				track_menu(_app_frame, _navigate2->window_bounds(), _commands[commands::menu_main]->menu());
				return true;
			}
			if (key == keys::BROWSER_BACK)
			{
				_state.browse_back(_view_frame);
				return true;
			}
			if (key == keys::BROWSER_FORWARD)
			{
				_state.browse_forward(_view_frame);
				return true;
			}
			if (key == keys::BROWSER_REFRESH)
			{
				reload();
				return true;
			}
			if (key == keys::BROWSER_HOME)
			{
				toggle_full_screen();
				return true;
			}
			/*if (key == keys::VOLUME_MUTE)
			{
				const auto display = _state.display_state();

				if (display && display->_session)
				{
					display->_session->toggle_mute();
				}

				return true;
			}
			if (key == keys::VOLUME_DOWN)
			{
				toggle_volume(true, false);
				return true;
			}
			if (key == keys::VOLUME_UP)
			{
				toggle_volume(false, false);
				return true;
			}*/
			if (key == keys::BROWSER_STOP || key == keys::MEDIA_STOP)
			{
				const auto display = _state.display_state();
				if (display && display->is_playing_media()) _state.stop();
				return true;
			}
			if (key == keys::MEDIA_PLAY_PAUSE)
			{
				_state.play(_view_frame);
				return true;
			}
			if (key == keys::MEDIA_PREV_TRACK)
			{
				_state.select_next(_view_frame, false, keys.control, keys.shift);
				return true;
			}
			if (key == keys::MEDIA_NEXT_TRACK)
			{
				_state.select_next(_view_frame, true, keys.control, keys.shift);
				return true;
			}
			if (key == keys::ESCAPE)
			{
				if (_state.escape(_view_frame)) return true;
			}
			if (key == keys::RETURN && !keys.control && !keys.shift)
			{
				if (_state.enter(_view_frame)) return true;
			}

			if (_view_has_focus)
			{
				if (!keys.alt)
				{
					if (key == keys::LEFT)
					{
						_state.select_next(_view_frame, false, keys.control, keys.shift);
						return true;
					}
					if (key == keys::RIGHT)
					{
						_state.select_next(_view_frame, true, keys.control, keys.shift);
						return true;
					}
					if (key == keys::HOME)
					{
						_state.select_end(_view_frame, false, keys.control, keys.shift);
						return true;
					}
					if (key == keys::END)
					{
						_state.select_end(_view_frame, true, keys.control, keys.shift);
						return true;
					}

					if (_state.view_mode() == view_type::items)
					{
						if (key == keys::UP)
						{
							_state.stop_slideshow();
							_view_items->line_up(keys.control, keys.shift);
							_state.make_visible(_state.focus_item());
							return true;
						}
						if (key == keys::DOWN)
						{
							_state.stop_slideshow();
							_view_items->line_down(keys.control, keys.shift);
							_state.make_visible(_state.focus_item());
							return true;
						}
					}
				}
			}
		}

		for (const auto& c : _commands)
		{
			for (const auto& ac : c.second->kba)
			{
				if (ac.key == key)
				{
					const auto key_state_match =
						(ac.key_state & keyboard_accelerator_t::control) != 0 == keys.control &&
						(ac.key_state & keyboard_accelerator_t::shift) != 0 == keys.shift &&
						(ac.key_state & keyboard_accelerator_t::alt) != 0 == keys.alt;

					if (key_state_match && c.second->enable)
					{
						invoke(c.second);
						return true;
					}
				}
			}
		}
	}

	return false;
}

void app_frame::toggle_full_screen()
{
	_state.is_full_screen = !_state.is_full_screen;

	if (_state.is_full_screen)
	{
		if (!_state.has_selection() && _state.has_display_items())
		{
			_state.select_next(_view_frame, true, false, false);
		}

		_state.view_mode(view_type::media);
		_view_frame->tick();
	}
	else
	{
		_state.view_mode(view_type::items);
	}

	_pa->full_screen(_state.is_full_screen);
	invalidate_view(view_invalid::app_layout | view_invalid::screen_saver | view_invalid::command_state);
}


void app_frame::open_default_folder()
{
	bool success = false;

	if (!command_line.folder_path.is_empty())
	{
		df::unique_paths selection;
		selection.emplace(command_line.selection);
		success = _state.open(_view_frame, df::search_t().add_selector(command_line.folder_path), selection);
	}

	if (!success)
	{
		if (!saved_current_search.empty())
		{
			const auto search = df::search_t::parse(saved_current_search);

			if (!search.is_empty())
			{
				if (search.has_related())
				{
					// Opening related needs loading of related fields
					_state.open(_view_frame, search.related().path);
					success = true;
				}
				else
				{
					success = _state.open(_view_frame, search, {});
				}
			}
		}
	}

	if (!success)
	{
		_state.open(_view_frame,
		            df::search_t().add_selector(df::item_selector(known_path(platform::known_folder::pictures))), {});
	}
}

inline void app_frame::make_visible(const df::item_element_ptr& i)
{
	_view_items->make_visible(i);
}

bool app_frame::is_command_checked(const commands id)
{
	const auto it = _commands.find(id);

	if (it != _commands.cend())
	{
		return it->second->checked;
	}

	return false;
}

void app_frame::element_broadcast(const view_element_event& event)
{
	_view_test->broadcast_event(event);
	_view_items->broadcast_event(event);
	_view_edit->broadcast_event(event);
	_view_media->broadcast_event(event);
	_view_rename->broadcast_event(event);
	_view_sync->broadcast_event(event);
	_view_import->broadcast_event(event);
	_view_locate->broadcast_event(event);
}

void app_frame::focus_changed(const bool has_focus, const ui::control_base_ptr& child)
{
	df::trace(str::format(u8"app_frame::focus {}"sv, has_focus));
	focus_search(_search_edit->has_focus());

	_filter_has_focus = _filter_edit->has_focus();
	_view_has_focus = _view_frame->_frame->has_focus();
	_toolbar_has_focus = _navigate1->has_focus() || _navigate2->has_focus() || _navigate3->has_focus() ||
		_media_edit_commands->
		has_focus() || _tools->has_focus() || _sorting->has_focus();
	_nav_has_focus = _sidebar->_frame->has_focus();
	_view_controls_have_focus = _view_controls && _view_controls->_dlg->has_focus();

	invalidate_view(view_invalid::view_redraw | view_invalid::command_state);
}

void app_frame::item_focus_changed(const df::item_element_ptr& focus, const df::item_element_ptr& previous)
{
	df::trace(u8"app_frame::focus_changed"sv);

	if (_view_items->is_visible(previous))
	{
		_view_items->make_visible(focus);
	}
}

void app_frame::display_changed()
{
	df::assert_true(ui::is_ui_thread());

	if (!df::is_closing)
	{
		_view->display_changed();

		invalidate_view(view_invalid::app_layout |
			view_invalid::view_layout |
			view_invalid::command_state |
			view_invalid::screen_saver |
			view_invalid::media_elements |
			view_invalid::controller |
			view_invalid::tooltip);
	}
};

void app_frame::play_state_changed(const bool play)
{
	df::assert_true(ui::is_ui_thread());
	invalidate_view(view_invalid::view_redraw | view_invalid::command_state);
}

void app_frame::reload()
{
	_view->reload();

	invalidate_view(view_invalid::view_layout |
		view_invalid::group_layout |
		view_invalid::index |
		view_invalid::refresh_items |
		view_invalid::item_scan);

	//_view_host._frame->reset_graphics();
	//free_graphics_resources();
}

void app_frame::view_changed(const view_type m)
{
	df::assert_true(ui::is_ui_thread());
	auto v = _view;
	view_controls_host_ptr vc;

	if (m == view_type::edit)
	{
		v = _view_edit;
		vc = _view_edit->controls(_app_frame);
	}
	else if (m == view_type::items)
	{
		v = _view_items;
	}
	else if (m == view_type::media)
	{
		v = _view_media;
	}
	else if (m == view_type::test)
	{
		v = _view_test;
	}
	else if (m == view_type::rename)
	{
		v = _view_rename;
		vc = _view_rename->controls(_app_frame);
	}
	else if (m == view_type::import)
	{
		v = _view_import;
		vc = _view_import->controls(_app_frame);
	}
	else if (m == view_type::sync)
	{
		v = _view_sync;
		vc = _view_sync->controls(_app_frame);
	}
	else if (m == view_type::locate)
	{
		v = _view_locate;
		vc = _view_locate->controls(_app_frame);
	}

	if (vc != _view_controls)
	{
		if (_view_controls)
		{
			_view_controls->_dlg->show(false);
		}

		//auto f = _view_controls->_frame;
		_view_controls = vc;

		if (_view_controls)
		{
			_view_controls->_dlg->show(true);
		}
	}

	if (v != _view)
	{
		if (_view)
		{
			_view->deactivate();
		}

		_view = v;
		_view_frame->view(v);
		_view->activate(_view_bounds.extent());
		free_graphics_resources(true, false);
		focus_view();

		invalidate_view(view_invalid::app_layout |
			view_invalid::view_layout |
			view_invalid::group_layout |
			view_invalid::sidebar |
			view_invalid::command_state |
			view_invalid::media_elements |
			view_invalid::tooltip |
			view_invalid::controller |
			view_invalid::address);
	}
}

void app_frame::invoke(const command_info_ptr& command)
{
	try
	{
		if (command->enable)
		{
			df::log(__FUNCTION__, command->text);

			df::scope_locked_inc sl(df::command_active);
			command->invoke();
			invalidate_view(view_invalid::command_state);
		}
	}
	catch (std::exception& e)
	{
		df::log(__FUNCTION__, e.what());
	}
}

void app_frame::track_menu(const ui::frame_ptr& parent, const recti bounds,
                           const std::vector<ui::command_ptr>& commands)
{
	pause_media pause(_state);
	complete_pending_events(); // process any menu updates
	parent->track_menu(bounds, commands);
}

void app_frame::invoke(const commands id)
{
	const auto command = find_command(id);

	if (command)
	{
		invoke(command);
	}
}

bool app_frame::can_open_search(const df::search_t& link)
{
	return _view->can_exit();
}

void app_frame::folder_changed()
{
	invalidate_view(view_invalid::refresh_items);
}

void app_frame::dpi_changed()
{
	_sidebar->dpi_changed();

	const view_element_event e{view_element_event_type::dpi_changed, nullptr};
	element_broadcast(e);

	invalidate_view(view_invalid::app_layout);
}

void app_frame::on_window_layout(ui::measure_context& mc, const sizei extent, const bool is_minimized)
{
	if (!is_minimized)
	{
		_extent = extent;
		layout(mc);
	}
}

void app_frame::on_window_paint(ui::draw_context& dc)
{
	const std::u8string_view status = _view->status();

	if (!status.empty())
	{
		const auto bounds = _status_bounds.inflate(-dc.padding2, 0);
		dc.draw_text(status, bounds, ui::style::font_face::dialog,
		             ui::style::text_style::single_line, ui::color(dc.colors.foreground, dc.colors.alpha), {});
	}
	else if (setting.show_debug_info)
	{
		const auto display = _state.display_state();
		char text[200];

		if (display && display->_session && display->_session->is_open())
		{
			const auto times = display->_session->times(df::now());
			sprintf_s(text, "%d fps (%.0f ms render | %d ms tick) %.2f|%.2f|%.2f", _view_frame->fps(),
			          _view_frame->frame_render_time * 1000.0, _frame_delay, times.pos, times.video, times.audio);
		}
		else
		{
			sprintf_s(text, "%d fps (%.0f ms render | %d ms tick)", _view_frame->fps(),
			          _view_frame->frame_render_time * 1000.0, _frame_delay);
		}

		dc.draw_text(str::utf8_cast(text), _status_bounds, ui::style::font_face::dialog,
		             ui::style::text_style::single_line_center, ui::color(dc.colors.foreground, dc.colors.alpha), {});
	}

	if (!_state.is_items_or_media_view())
	{
		if (!_logo_tex)
		{
			const auto t = dc.create_texture();

			if (t)
			{
				auto res = platform::resource_item::logo15;
				if (_title_bounds.height() >= 40) res = platform::resource_item::logo30;
				if (_title_bounds.height() >= 60) res = platform::resource_item::logo;

				files ff;
				const auto logo_surface = ff.image_to_surface(load_resource(res));

				_logo_tex = t;
				_logo_tex->update(logo_surface);
			}
		}

		auto logo_bounds = _title_bounds;
		auto title_bounds = _title_bounds;

		if (_logo_tex)
		{
			logo_bounds.right = logo_bounds.left + _title_bounds.height();
			title_bounds.left = logo_bounds.right;

			dc.draw_texture(_logo_tex, center_rect(_logo_tex->dimensions(), logo_bounds),
			                _logo_tex->dimensions(), dc.colors.alpha);
		}

		const std::u8string_view title = _view->title();

		dc.draw_text(title, title_bounds, ui::style::font_face::title, ui::style::text_style::single_line,
		             ui::color(dc.colors.foreground, dc.colors.alpha), {});
	}

	const auto border_outside = recti(_extent);
	const auto scale1 = df::round(1 * dc.scale_factor);
	const auto clr = ui::style::color::view_background;
	dc.draw_border(border_outside.inflate(-scale1), border_outside, clr, clr);
}

void app_frame::activate(const bool is_active)
{
	if (_is_active != is_active)
	{
		//if (is_active && _view_frame && _view_frame->_frame && _app_frame && _app_frame->has_focus())
		//{
		//focus _render_window->_frame->focus();
		//}

		if (_sidebar)
		{
			_sidebar->_is_active = _is_active = is_active;
		}
	}

	invalidate_view(view_invalid::view_redraw);
}


void app_frame::web_service_cache(std::u8string key, std::function<void(const std::u8string&)> f)
{
	queue_database([this, key = std::move(key), f = std::move(f)](const database& db)
	{
		auto result = db.web_service_cache(key);

		queue_async(async_queue::cloud, [result, f]
		{
			f(result);
		});
	});
}

void app_frame::web_service_cache(std::u8string key, std::u8string value)
{
	queue_database([key = std::move(key), value = std::move(value)](const database& db)
	{
		db.web_service_cache(key, value);
	});
}


void app_frame::search_edit_change(const std::u8string& text) const
{
	if (_search_predictions_frame && _search_has_focus && _pin_search == 0)
	{
		_search_predictions_frame->search(text);
	}
}

void app_frame::filter_edit_change(const std::u8string& text)
{
	_state.filter().wildcard(text);
	invalidate_view(view_invalid::group_layout);
}

void app_frame::delete_items(const df::item_set& items)
{
	const auto title = tt.command_delete;
	auto dlg = make_dlg(_app_frame);
	const auto can_process = _state.can_process_selection_and_mark_errors(
		_view_frame, df::process_items_type::local_file_or_folder);

	pause_media pause(_state);

	if (can_process.fail())
	{
		dlg->show_message(icon_index::error, title, can_process.to_string());
	}
	else
	{
		std::vector<view_element_ptr> controls;
		controls.emplace_back(set_margin(std::make_shared<ui::title_control2>(
			dlg->_frame, icon_index::cancel, title, format_plural_text(tt.delete_info_fmt, items), items.thumbs())));
		controls.emplace_back(std::make_shared<divider_element>());
		controls.emplace_back(std::make_shared<ui::ok_cancel_control>(dlg->_frame, tt.button_delete));

		if (!setting.confirm_deletions || dlg->show_modal(controls) == ui::close_result::ok)
		{
			bool should_select_next = false;
			const auto next = _state.next_unselected_item();

			{
				detach_file_handles detach(_state);

				constexpr auto allow_undo = true;
				const auto res = platform::delete_items(items.file_paths(), items.folder_paths(), allow_undo);

				if (res.success())
				{
					should_select_next = true;
				}
				else if (res.code != platform::file_op_result_code::CANCELLED)
				{
					dlg->show_message(icon_index::error, title, res.format_error(tt.delete_error));
				}

				_state.invalidate_view(view_invalid::view_layout |
					view_invalid::group_layout |
					view_invalid::command_state |
					view_invalid::app_layout);
			}

			if (should_select_next)
			{
				df::unique_paths selection;
				if (next) next->add_to(selection);
				_state.open(_view_frame, _state.search(), selection);
			}
		}
	}
}

void app_frame::focus_view()
{
	if (_view_frame && _view_frame->_frame)
	{
		_view_frame->_frame->focus();
	}
}

bool app_frame::can_exit()
{
	return _view->can_exit();
}


bool app_frame::pre_init()
{
	df::log(u8"main"sv, df::format_version(false));

#ifndef WINSTORE
	if (install_update_if_exists())
	{
		df::log(__FUNCTION__, u8"Exit because of install"sv);
		return false;
	}
#endif

	std::setlocale(LC_ALL, "en_US.UTF-8");

	return true;
}


void app_frame::update_font_size() const
{
	if (_app_frame && _pa)
	{
		_pa->set_font_base_size(setting.large_font ? large_font_size : normal_font_size);
	}
}

bool app_frame::init(const std::u8string_view command_line_text)
{
	//auto size_index_file_info = sizeof(df::index_file_info);
	//auto size_index_folder_info = sizeof(df::index_folder_info);
	//auto size_index_item_metadata = sizeof(prop::item_metadata);

	log_func lf(__FUNCTION__);
	df::log(__FUNCTION__, str::format(u8"is_app_installed {}"sv, is_app_installed()));


	command_line.parse(command_line_text);
	load_file_types();
	metadata_xmp::initialise();
	_item_index.init_item_index();

	const auto store = platform::create_registry_settings();
	load_options(store);
	setting.instantiations++;

	if (setting.language != u8"en"sv)
	{
		const auto lang_folder = known_path(platform::known_folder::running_app_folder).combine(u8"languages"sv);
		const auto lang_path = lang_folder.combine_file_ext(setting.language, u8".po"sv);

		auto po_entries = load_po(lang_path);
		tt.load_lang(lang_path.name(), po_entries);
	}

	update_font_size();
	if (str::is_empty(setting.favorite_tags)) setting.favorite_tags = tt.default_favorite_tags;

	initialise_commands();

	_app_frame = _pa->create_app_frame(store, shared_from_this());

	if (!_app_frame)
	{
		app_fail(tt.error_create_window_failed, {});
		return false;
	}

	_view_frame->init(_app_frame);
	_sidebar->init(_app_frame);

	create_toolbars();

	ui::edit_styles search_edit_styles;
	search_edit_styles.rounded_corners = true;
	search_edit_styles.horizontal_scroll = true;
	search_edit_styles.bg_clr = ui::style::color::toolbar_background;
	search_edit_styles.select_all_on_focus = true;

	ui::edit_styles filter_edit_styles;
	filter_edit_styles.rounded_corners = true;
	filter_edit_styles.horizontal_scroll = true;
	filter_edit_styles.bg_clr = ui::style::color::toolbar_background;
	filter_edit_styles.select_all_on_focus = true;
	filter_edit_styles.cue = tt.filter;
	filter_edit_styles.align_center = true;

	_search_edit = _app_frame->create_edit(search_edit_styles, {}, [this](const std::u8string& text)
	{
		search_edit_change(text);
	});
	_filter_edit = _app_frame->create_edit(filter_edit_styles, {}, [this](const std::u8string& text)
	{
		filter_edit_change(text);
	});

	init_search();

	_bubble = _app_frame->create_bubble();
	_state.view_mode(view_type::items);
	_threads.start([&q = work_task_queue] { start_worker(q, u8"work"sv); });

	open_default_folder();
	invalidate_view(view_invalid::address);

	work_task_queue.enqueue([this, app = shared_from_this()]
	{
		start_workers();
		check_for_updates_and_location(app, _state);
		load_tools();
	});

	if (command_line.run_tests)
	{
		queue_ui([this] { invoke(commands::test_run_all); });
	}

	if (!setting.sound_device.empty())
	{
		_state.change_audio_device(setting.sound_device);
	}

	_state.group_order(_starting_group_order, _starting_sort_order);
	focus_view();

	return true;
}


void app_frame::on_window_destroy()
{
	log_func lf(__FUNCTION__);

	_logo_tex.reset();
	_bubble.reset();
	_search_predictions_frame.reset();
	_view_controls.reset();
	_sidebar.reset();
	_view_frame.reset();
	_item_index.reset();
	_state.reset();
	save_options();
}

void app_frame::command_hover(const ui::command_ptr& c, const recti window_bounds)
{
	if (_hover_command != c)
	{
		_hover_command = c;
		_hover_command_bounds = window_bounds;
		invalidate_view(view_invalid::tooltip);
	}
}

std::vector<ui::command_ptr> app_frame::menu(const pointi loc)
{
	update_button_state(false);

	std::vector<ui::command_ptr> result;
	const auto view_window_bounds = _view_frame->_frame->window_bounds();

	if (view_window_bounds.contains(loc))
	{
		const auto menu_hint = _view->context_menu(loc - view_window_bounds.top_left());

		switch (menu_hint)
		{
		case menu_type::media:
			result.emplace_back(find_command(commands::play));
			result.emplace_back(find_command(commands::view_fullscreen));
			result.emplace_back(find_command(commands::view_zoom));
			result.emplace_back(find_command(commands::search_related));
			result.emplace_back(find_command(commands::tool_edit));
			result.emplace_back(nullptr);
			result.emplace_back(find_command(commands::menu_open));
			result.emplace_back(find_command(commands::menu_tools));
			result.emplace_back(find_command(commands::menu_tag_with));
			result.emplace_back(find_command(commands::menu_rate_or_label));
			result.emplace_back(find_command(commands::menu_display_options));
			result.emplace_back(find_command(commands::playback_menu));
			result.emplace_back(nullptr);
			result.emplace_back(find_command(commands::tool_save_current_video_frame));
			result.emplace_back(nullptr);
			result.emplace_back(find_command(commands::tool_delete));
			result.emplace_back(find_command(commands::tool_rename));
			result.emplace_back(find_command(commands::tool_copy_to_folder));
			result.emplace_back(find_command(commands::tool_move_to_folder));
			result.emplace_back(find_command(commands::edit_cut));
			result.emplace_back(find_command(commands::edit_copy));
			result.emplace_back(find_command(commands::edit_paste));
			break;
		case menu_type::items:
			result.emplace_back(find_command(commands::menu_navigate));
			result.emplace_back(find_command(commands::menu_open));
			result.emplace_back(find_command(commands::menu_tools));
			result.emplace_back(find_command(commands::menu_rate_or_label));
			result.emplace_back(find_command(commands::menu_select));
			result.emplace_back(find_command(commands::menu_group));
			result.emplace_back(find_command(commands::menu_display_options));
			result.emplace_back(find_command(commands::playback_menu));
			result.emplace_back(nullptr);
			result.emplace_back(find_command(commands::tool_delete));
			result.emplace_back(find_command(commands::tool_rename));
			result.emplace_back(find_command(commands::tool_copy_to_folder));
			result.emplace_back(find_command(commands::tool_move_to_folder));
			result.emplace_back(find_command(commands::edit_cut));
			result.emplace_back(find_command(commands::edit_copy));
			result.emplace_back(find_command(commands::edit_paste));
			result.emplace_back(nullptr);
			result.emplace_back(find_command(commands::option_toggle_details));
			result.emplace_back(find_command(commands::browse_recursive));
			result.emplace_back(nullptr);
			result.emplace_back(find_command(commands::refresh));
			result.emplace_back(find_command(commands::tool_new_folder));
			break;
		case menu_type::view:
		default:
			result.emplace_back(find_command(commands::view_close));
			break;
		}
	}
	else
	{
		result.emplace_back(find_command(commands::refresh));
		result.emplace_back(find_command(commands::tool_eject));
		result.emplace_back(nullptr);
		result.emplace_back(find_command(commands::options_collection));
		result.emplace_back(find_command(commands::options_sidebar));
		result.emplace_back(nullptr);
		result.emplace_back(find_command(commands::view_show_sidebar));
		result.emplace_back(find_command(commands::option_show_thumbnails));
		result.emplace_back(find_command(commands::view_favorite_tags));
		result.emplace_back(nullptr);
		result.emplace_back(find_command(commands::large_font));
	}

	return result;
}

void app_frame::exit()
{
	log_func lf(__FUNCTION__);
	_threads.clear();
	metadata_xmp::term();
}

void app_frame::system_event(const ui::os_event_type ost)
{
	df::assert_true(ui::is_ui_thread());

	if (ost == ui::os_event_type::system_device_change)
	{
		invalidate_view(
			view_invalid::app_layout | view_invalid::view_layout | view_invalid::sidebar | view_invalid::index);
	}
	else if (ost == ui::os_event_type::options_changed)
	{
		invalidate_view(view_invalid::options);
	}
	else if (ost == ui::os_event_type::dpi_changed)
	{
		invalidate_view(view_invalid::options);
	}
	else if (ost == ui::os_event_type::screen_locked)
	{
		_state.stop();
	}
}

void app_frame::final_exit()
{
	df::log(u8"main"sv, u8"exit"sv);
	df::close_log();
}

ui::app_ptr create_app(const ui::plat_app_ptr& pa)
{
	return std::make_shared<app_frame>(pa);
}

void app_frame::crash(const df::file_path dump_file_path)
{
	if (df::handling_crash == 0)
	{
		df::scope_locked_inc l(df::handling_crash);

		if (_app_frame)
		{
			_app_frame->show(false);
		}

		flush_open_files_to_crash_files_list();

#ifndef WINSTORE
		if (setting.send_crash_dumps)
		{
			df::log(__FUNCTION__, u8"*** CRASH ***"sv);

			if (!df::last_loaded_path.is_empty())
			{
				df::log(__FUNCTION__, str::format(u8"Last file type opened: {}"sv, df::last_loaded_path.extension()));
			}

			if (!str::is_empty(df::rendering_func))
			{
				df::log(__FUNCTION__, str::format(u8"Rendering function: {}"sv, str::utf8_cast(df::rendering_func)));
			}

			const auto log_file_path = df::close_log();
			const auto previous_log_path = df::previous_log_path;
			const auto crash_zip_path = platform::temp_file();

			const auto now = platform::now();
			const auto date = now.date();

			const auto name = str::format(u8"Diffractor-{}-{}-{:04}{:02}{:02}-{:02}{:02}{:02}.dmp"sv,
			                              s_app_version, g_app_build, date.year, date.month, date.day,
			                              date.hour, date.minute, date.second);

			df::zip_file zip;

			if (zip.create(crash_zip_path))
			{
				zip.add(dump_file_path, name);
				if (log_file_path.exists()) zip.add(log_file_path);
				if (previous_log_path.exists()) zip.add(previous_log_path);
				zip.close();
			}

			u8ostringstream message;

			for (const auto& i : calc_app_info(_state.item_index, true))
			{
				message << i.first << u8" "sv << i.second << '\n';
			}

			platform::web_request req;
			req.verb = platform::web_request_verb::POST;
			req.path = u8"/crash"sv;
			req.form_data.emplace_back(u8"message"sv, message.str());
			//req.form_data.emplace_back(u8"contactname"sv, platform::user_name());
			//req.form_data.emplace_back(u8"email"sv, setting.buy_email);
			req.form_data.emplace_back(u8"version"sv, platform::OS());
			req.form_data.emplace_back(u8"diffractor"sv, s_app_version);
			req.form_data.emplace_back(u8"build"sv, g_app_build);
			req.form_data.emplace_back(u8"subject"sv, u8"Diffractor CRASH report"sv);
			req.form_data.emplace_back(u8"submit"sv, u8"Send Report"sv);
			req.file_form_data_name = u8"ff"sv;
			req.file_name = u8"crash.zip"sv;
			req.file_path = crash_zip_path;

			const auto con = platform::connect_to_host(u8"diffractor.com"sv);
			send_request(con, req);
		}
#endif
	}
}

std::u8string app_frame::restart_cmd_line()
{
	return command_line.format_restart_cmd_line();
}

void app_frame::save_recovery_state()
{
	save_options();
}

void app_frame::invalidate_view(const view_invalid invalid)
{
	auto expected = _invalids.load();
	auto updated = expected | invalid;

	while (!_invalids.compare_exchange_weak(expected, updated))
	{
		updated = expected | invalid;
	}
}


void app_frame::free_graphics_resources(const bool items_only, const bool offscreen_only)
{
	if (!items_only)
	{
		const auto d = _state._display;

		if (d)
		{
			if (d->_selected_texture1) d->_selected_texture1->free_graphics_resources();
			if (d->_selected_texture2) d->_selected_texture2->free_graphics_resources();

			if (d->_session)
			{
				_player->close(d->_session, {});
			}
		}

		const view_element_event e{view_element_event_type::free_graphics_resources, nullptr};
		element_broadcast(e);
	}

	const auto logical_bounds = _view_items->calc_logical_items_bounds();
	const auto expanded_logical_bounds = logical_bounds.inflate(0, logical_bounds.height());

	for (const auto& i : _state.search_items().items())
	{
		if (!offscreen_only || !i->bounds.intersects(expanded_logical_bounds))
		{
			i->clear_cached_surface();
		}
	}

	_logo_tex.reset();
}
