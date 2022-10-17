// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "pch.h"

#include "app_text.h"
#include "util_zip.h"

#include "model_location.h"
#include "model_db.h"
#include "model_index.h"
#include "model.h"

#include "ui_dialog.h"
#include "util_crash_files_db.h"
#include "metadata_xmp.h"
#include "view_test.h"
#include "view_items.h"
#include "view_edit.h"
#include "view_media.h"

#include "app_sidebar.h"
#include "app_commands.h"
#include "app_command_line.h"
#include "app.h"

#include <utility>

#include "app_command_status.h"
#include "util_spell.h"
#include "app_match.h"

platform::thread_event media_preview_event(false, false);
static platform::mutex media_preview_mutex;
static _Guarded_by_(media_preview_mutex) std::function<void(media_preview_state&)> next_media_preview;
command_line_t command_line;
static std::atomic_int index_version;

const wchar_t* s_app_name_l = L"Diffractor";
const std::u8string_view s_app_name = u8"Diffractor"sv;
const std::u8string_view s_app_version = u8"125.0"sv;
const std::u8string_view g_app_build = u8"1158"sv;
const std::u8string_view stage_file_name = u8"diffractor-setup-update.exe"sv;
static constexpr std::u8string_view installed_file_name = u8"diffractor-setup-installed.exe"sv;
static constexpr std::u8string_view s_search = u8"search"sv;

std::u8string df::format_version(const bool short_text)
{
	if (short_text)
	{
		return str::format(u8"{}.{}"sv, s_app_version, g_app_build);
	}

	return str::format(u8"{}: {}.{}  |  {}"sv, tt.version, s_app_version, g_app_build, str::utf8_cast(__DATE__));
};

crash_files_db crash_files(df::probe_data_file(u8"diffractor-files-that-crash.txt"sv));

void flush_open_files_to_crash_files_list()
{
	crash_files.flush_open_files();
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
		result.emplace_back(u8"Memory:"sv, str::format(u8"{} (peak {})"sv, df::file_size(current), df::file_size(peak)));
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
	hover.elements.add(std::make_shared<text_element>(_text, ui::style::font_size::title,
		ui::style::text_style::single_line,
		view_element_style::line_break));
	hover.elements.add(std::make_shared<text_element>(df::format_version(false), ui::style::font_size::dialog,
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

		hover.elements.add(table);
	}

	hover.elements.add(std::make_shared<action_element>(tt.help_more_info));
	hover.active_bounds = hover.window_bounds = bounds.offset(element_offset);
}

void app_frame::app_fail(const std::u8string_view message, const std::u8string_view more_text)
{
	auto message_s = std::u8string(message);
	auto more_text_s = std::u8string(more_text);

	queue_ui([message_s, more_text_s, parent = _app_frame]()
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

void app_frame::stage_update()
{
	static bool first_time_this_instance = true;

	if (first_time_this_instance)
	{
		if (setting.is_tester || (setting.install_updates && setting.first_run_today))
		{
			auto download_complete = [this](const df::file_path download_path)
			{
				const auto stage_path = known_path(platform::known_folder::app_data).combine_file(stage_file_name);

				if (download_path.exists())
				{
					platform::move_file(download_path, stage_path, false);
				}
			};

			queue_async(async_queue::web, [download_complete]()
				{
					platform::download_and_verify(setting.is_tester, download_complete);
				});
		}
	}
}

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

			if (file_type->has_trait(file_type_traits::bitmap))
			{
				media_element = std::make_shared<photo_control>(_state, display, _host, false);
			}
			else if (file_type->has_trait(file_type_traits::visualize_audio))
			{
				media_element = std::make_shared<audio_control>(_state, display, _host);
			}
			else if (file_type->has_trait(file_type_traits::av))
			{
				media_element = std::make_shared<video_control>(_state, display, _host);
			}
			else if (file_type->has_trait(file_type_traits::archive))
			{
				// const auto _archive_items = files::list_archive(item->path());
				// std::make_shared<file_list_control>(_archive_items, view_element_style::center);
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


class search_auto_complete final : public std::enable_shared_from_this<search_auto_complete>,
	public ui::complete_strategy_t
{
	view_state& _state;
	ui::edit_ptr _edit;
	df::string_counts _known;
	ui::auto_complete_match_ptr _selected;

public:
	std::u8string no_results_message() override
	{
		return std::u8string(tt.type_to_search);
	}

	void clear()
	{
		_known.clear();
	}

	search_auto_complete(view_state& s, ui::edit_ptr edit) : _state(s), _edit(std::move(edit))
	{
		resize_to_show_results = true;
		auto_select_first = false;
		max_predictions = 25u;
	}

	void initialise(std::function<void(const ui::auto_complete_results&)> complete) override
	{
		_selected.reset();
		_known.clear();
		_state.history.count_strings(_known, 1);
		_state.recent_folders.count_strings(_known, 1);
		//_state.recent_apps.count_strings(_recents, 1);
		_state.recent_tags.count_strings(_known, 1, u8"#"sv);
		_state.recent_tags.count_strings(_known, 1);
		//_state.recent_locations.count_strings(_recents, 1);
		if (!setting.write_folder.empty()) ++_known[str::cache(setting.write_folder)];

		for (const auto ks : prop::key_scopes())
		{
			++_known[str::cache(str::format(u8"with:{}"sv, ks.scope))];
			++_known[str::cache(str::format(u8"without:{}"sv, ks.scope))];
		}
	}

	int calc_auto_complete_word_weight(const index_state::auto_complete_word& word) const
	{
		auto result = 1;
		const auto found = _known.find(word.text);
		if (found != _known.end()) result += found->second;
		return result;
	}

	int calc_auto_complete_word_weight(const index_state::auto_complete_folder& path,
		const std::u8string_view query) const
	{
		auto result = 1;
		const auto found = _known.find(path.path.text());
		if (found != _known.end()) result += found->second;
		if (str::icmp(path.path.name().sv(), query) == 0) result += 5;
		return result;
	}

	void search(const std::u8string& query, std::function<void(const ui::auto_complete_results&)> complete) override
	{
		_state._async.queue_async(async_queue::auto_complete, [this, query, complete]()
			{
				ui::auto_complete_results found;

				const auto query_parts = str::split(query, true);

				if (query_parts.empty() || str::icmp(query, _state.search().text()) == 0)
				{
					auto recents = _state.recent_searches.items();
					std::ranges::reverse(recents);

					for (const auto& path : recents)
					{
						found.emplace_back(std::make_shared<folder_match>(*this, df::folder_path(path)));
					}
				}
				else
				{
					if (df::is_path(str::trim(query)))
					{
						df::folder_path folder(query);

						if (!folder.exists())
						{
							folder = folder.parent();
						}

						const df::item_selector selector(folder);

						for (const auto& fi : platform::select_folders(selector, setting.show_hidden))
						{
							ui::match_highlights m;
							auto path = folder.combine(fi.name);

							if (find_auto_complete(query_parts, path.text(), true, m))
							{
								found.emplace_back(std::make_shared<folder_match>(*this, path, m, 10));
							}
						}
					}

					if (found.size() < max_predictions)
					{
						const auto found_folders = _state.item_index.auto_complete_folders(query, max_predictions);

						for (const auto& path : found_folders)
						{
							found.emplace_back(std::make_shared<folder_match>(
								*this, path.path, path.highlights, calc_auto_complete_word_weight(path, query)));
						}
					}

					if (found.size() < max_predictions)
					{
						const auto found_words = _state.item_index.auto_complete_words(query, max_predictions);

						for (const auto& word : found_words)
						{
							found.emplace_back(std::make_shared<text_match>(*this, word.text, std::u8string{},
								word.highlights,
								calc_auto_complete_word_weight(word)));
						}
					}
				}

				if (found.size() < max_predictions && query_parts.size() > 1)
				{
					const auto query_back = query_parts.back();
					const auto lead_text = query.substr(0, query.rfind(query_back));
					const auto found_words = _state.item_index.auto_complete_words(query_back, max_predictions);

					for (const auto& word : found_words)
					{
						found.emplace_back(std::make_shared<text_match>(*this, word.text, lead_text, word.highlights,
							calc_auto_complete_word_weight(word)));
					}

					if (found.size() < max_predictions)
					{
						const auto found_folders = _state.item_index.auto_complete_folders(query_back, max_predictions);

						for (const auto& path : found_folders)
						{
							found.emplace_back(std::make_shared<folder_match>(
								*this, path.path, lead_text, path.highlights, calc_auto_complete_word_weight(path, query)));
						}
					}
				}

				if (found.size() < max_predictions && !query.empty() && std::iswspace(query.back()))
				{
					if (found.size() < max_predictions)
					{
						for (const auto& word : _known)
						{
							found.emplace_back(std::make_shared<text_match>(*this, std::u8string(word.first), query,
								ui::match_highlights{}, word.second));
						}
					}

					if (found.size() < max_predictions)
					{
						const auto found_groups = _state.item_index.auto_complete_words(u8"@"sv, max_predictions);

						for (const auto& word : found_groups)
						{
							found.emplace_back(std::make_shared<text_match>(*this, word.text, query, word.highlights,
								calc_auto_complete_word_weight(word)));
						}
					}

					if (found.size() < max_predictions)
					{
						const auto found_tags = _state.item_index.auto_complete_words(u8"#"sv, max_predictions);

						for (const auto& word : found_tags)
						{
							found.emplace_back(std::make_shared<text_match>(*this, word.text, query, word.highlights,
								calc_auto_complete_word_weight(word)));
						}
					}
				}

				if (found.size() < max_predictions)
				{
					for (const auto &k : _known)
					{
						const auto trimmed = str::trim(query);

						if (str::starts(k.first, trimmed) &&
							found.size() < max_predictions)
						{
							found.emplace_back(
								std::make_shared<text_match>(*this, std::u8string(k.first), std::u8string{}));
						}
					}
				}

				if (found.size() > max_predictions) found.resize(max_predictions);
				_state.queue_ui([complete, found]() { complete(found); });
			});
	}

	void selected(const ui::auto_complete_match_ptr& i, const select_type st) override
	{
		if (i)
		{
			const auto text = i->edit_text();

			if (st == select_type::arrow)
			{
				_edit->window_text(text);
			}

			if (st == select_type::click || st == select_type::double_click)
			{
				_state.recent_searches.add(text);
				_state.open({}, text);
				_state._events.focus_view();
			}

			_selected = i;
		}
	}

	ui::auto_complete_match_ptr selected() const override
	{
		return _selected;
	}
};

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

std::u8string format_plural_text(const plural_text& fmt, const std::u8string_view first_name, const int count,
	const df::file_size size, const int of_total)
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

std::u8string format_plural_text(const plural_text& fmt, const int count, const int of_total)
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
		const auto results = std::make_shared<command_status>(_state._async, dlg, icon_index::star, tt.rate_title, _state.selected_count());
		_state.toggle_rating(results, { _item }, _hover_rating, event.host);
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

void view_frame::draw_status(ui::draw_context& dc)
{
	if (!_status_title.empty() || !_status_text.empty())
	{
		const auto padding = 8;
		const auto title_font = ui::style::font_size::title;
		const auto text_font = ui::style::font_size::dialog;

		const auto text_color = ui::color(dc.colors.foreground, dc.colors.alpha);
		const auto bg_color = ui::color(ui::style::color::important_background, dc.colors.alpha);
		const sizei avail_extent{ _extent.cx / 2, _extent.cy / 2 };

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
	_edit_view_controls = std::make_shared<edit_view_controls>(_state, _edit_view_state);
	_sidebar = std::make_shared<sidebar_host>(_state);
	_view_frame = std::make_shared<view_frame>(_state);
	_view_test = std::make_shared<test_view>(_state, _view_frame);
	_view_items = std::make_shared<items_view>(_state, _view_frame);
	_view_edit = std::make_shared<edit_view>(_state, _view_frame, _edit_view_state, _edit_view_controls);
	_view_media = std::make_shared<media_view>(_state, _view_frame);
	_edit_view_controls->_view = _view_edit;
}

app_frame::~app_frame()
{
	_threads.clear();
	_state.close();

	df::log(__FUNCTION__, u8"destruct"sv);
}

static gps_coordinate parse_coordinates(const std::u8string_view text, const gps_coordinate def_coords)
{
	const auto splits = str::split(text, true);

	if (splits.size() == 2)
	{
		const auto latitude = splits[0];
		const auto longitude = splits[1];

		if (!longitude.empty() && !latitude.empty())
		{
			return { str::to_double(latitude), str::to_double(longitude) };
		}
	}

	return def_coords;
}

struct log_func
{
	const std::string_view context;
	const std::u8string_view context2;

	log_func(const std::string_view c, const std::u8string_view c2 = {}) : context(c), context2(c2)
	{
		if (!str::is_empty(context2))
		{
			df::log(context, str::format(u8"start {}"sv, context2));
		}
		else
		{
			df::log(context, u8"start"sv);
		}
	}

	~log_func()
	{
		if (!str::is_empty(context2))
		{
			df::log(context, str::format(u8"exit {}"sv, context2));
		}
		else
		{
			df::log(context, u8"exit"sv);
		}
	}
};

static void start_media_decode_video(const std::shared_ptr<av_player>& player)
{
	log_func lf(__FUNCTION__);
	platform::set_thread_description(u8"media decode_video"sv);
	platform::thread_init c;
	player->decode_video();
}

static void start_media_decode_audio(const std::shared_ptr<av_player>& player)
{
	log_func lf(__FUNCTION__);
	platform::set_thread_description(u8"media decode_audio"sv);
	platform::thread_init c;
	player->decode_audio();
}

static void start_media_reading(const std::shared_ptr<av_player>& player)
{
	log_func lf(__FUNCTION__);
	platform::set_thread_description(u8"media read"sv);
	platform::thread_init c;
	player->reading();
}

struct app_updates_and_location_params
{
	gps_coordinate gps = setting.default_location;
	std::u8string city;
	std::u8string country;
	std::u8string version;
	std::u8string test_version = setting.available_test_version;
	bool should_update = false;

	void apply(const app_frame_ptr& app, view_state& s) const
	{
		df::assert_true(ui::is_ui_thread());

		setting.default_location = gps;
		setting.available_version = version;
		setting.available_test_version = test_version;

		s.invalidate_view(view_invalid::app_layout);

		if (setting.install_updates && should_update)
		{
			app->stage_update();
		}

		app->save_options(true);
	}
};

static void check_for_updates_and_location(const app_frame_ptr& app, view_state& s)
{
	log_func lf(__FUNCTION__);
	platform::set_thread_description(u8"CheckForUpdates"sv);
	platform::thread_init c;

	if (platform::is_online())
	{
		if (setting.first_run_today && setting.check_for_updates)
		{
			s.queue_async(async_queue::web, [app, &s]()
				{
					platform::web_request req;
					req.host = u8"diffractor.com"sv;
					req.path = u8"/ver"sv;
					req.query = platform::web_params{
						{u8"v"s, std::u8string(s_app_version)},
						{u8"b"s, std::u8string(g_app_build)},
						{u8"f"s, setting.first_run_ever ? u8"1"s : u8"0"s},
						{u8"ft"s, str::to_hex(setting.features_used_since_last_report)},
						{u8"i"s, str::to_hex(setting.instantiations)},
						{u8"os"s, platform::OS()},
					};

					const auto response = send_request(req);

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

						s.queue_ui([params, app, &s]()
							{
								app->save_options(true);
								params.apply(app, s);
							});
					}
				});
		}

		spell.lazy_download(s._async);
	}
}

static void start_database(database& db, platform::task_queue& database_task_queue, async_strategy& async,
	const app_frame_ptr& app, std::function<void()> index_loaded_func)
{
	log_func lf(__FUNCTION__);
	platform::set_thread_description(u8"database"sv);

	try
	{
		platform::thread_init c;
		db.open(known_path(platform::known_folder::app_data), u8"diffractor-cache"sv);
		async.queue_ui(std::move(index_loaded_func));

		if (db.is_open())
		{
			async.invalidate_view(view_invalid::refresh_items);
			const std::vector<std::reference_wrapper<platform::thread_event>> events = {
				database_task_queue._event, platform::event_exit
			};

			while (!df::is_closing)
			{
				if (wait_for(events, 1000, false) == 0)
				{
					try
					{
						df::scope_locked_inc l(df::jobs_running);

						for (const auto& t : database_task_queue.dequeue_all())
						{
							t();
						}
					}
					catch (std::exception& e)
					{
						df::log(__FUNCTION__, e.what());
					}
				}
				else
				{
					df::scope_locked_inc l(df::jobs_running);
					db.perform_writes();
				}
			}

			db.perform_writes();
		}
	}
	catch (std::exception& e)
	{
		df::log(__FUNCTION__, e.what());
		app->app_fail(tt.error_index_database_failed, str::utf8_cast(e.what()));
	}

	try
	{
		if (db.is_open())
		{
			db.close();
		}
	}
	catch (std::exception& e)
	{
		df::log(__FUNCTION__, e.what());
		app->app_fail(tt.error_index_database_failed, str::utf8_cast(e.what()));
	}
}

static void start_media_preview()
{
	log_func lf(__FUNCTION__);
	platform::set_thread_description(u8"media_preview"sv);

	try
	{
		platform::thread_init c;
		const std::vector<std::reference_wrapper<platform::thread_event>> events = {
			media_preview_event, platform::event_exit
		};
		media_preview_state preview_decoder;

		while (!df::is_closing)
		{
			const auto w = wait_for(events, 5000, false);

			if (0 == w)
			{
				std::function<void(media_preview_state&)> f;

				{
					platform::exclusive_lock media_lock(media_preview_mutex);
					std::swap(next_media_preview, f);
				}

				if (f)
				{
					f(preview_decoder);
				}
			}
			else if (platform::wait_for_timeout == w)
			{
				preview_decoder.close();
			}
		}
	}
	catch (std::exception& e)
	{
		df::log(__FUNCTION__, e.what());
	}
}

static void start_worker(platform::task_queue& q, const std::u8string_view name)
{
	log_func lf(__FUNCTION__, name);
	platform::set_thread_description(name);

	if (!df::is_closing)
	{
		try
		{
			platform::thread_init c;
			const std::vector<std::reference_wrapper<platform::thread_event>> events = { q._event, platform::event_exit };

			while (!df::is_closing)
			{
				if (wait_for(events, 10000, false) == 0)
				{
					try
					{
						df::scope_locked_inc l(df::jobs_running);
						const auto tasks = q.dequeue_all();

						for (const auto& t : tasks)
						{
							t();
						}
					}
					catch (std::exception& e)
					{
						df::log(__FUNCTION__, e.what());
					}
				}
			}
		}
		catch (std::exception& e)
		{
			df::log(__FUNCTION__, e.what());
		}
	}
}

//void logo_popup(ui_hover_element& popup, view_state& s)
//{
//	ui_elements info;
//	codecs c;
//	const auto image = load_resource(platform::resource_item::logo);
//
//	info.add(c.image_to_surface(image), ui_element_style::center);
//	info.add(s_app_name, render::style::font_size::title, render::style::text_style::multiline, ui_element_style::line_break | ui_element_style::center);
//	info.add(u8"Diffractor is a fast way to search, compare and organize photos or videos on your PC."sv, render::style::font_size::dialog, render::style::text_style::multiline, ui_element_style::line_break);
//
//	auto summary = s.item_index.summary();
//
//	if (summary.total > 0)
//	{
//		info.add(u8"Total items indexed:"sv, render::style::font_size::dialog, render::style::text_style::multiline, ui_element_style::line_break);
//		info.add(std::make_shared<summary_control>(summary, ui_element_style::line_break)).padding(16);
//	}
//
//	if (setting.show_debug_info)
//	{
//		popup.prefered_size = 350;
//		for (const auto& i : df::calc_app_info(false))
//		{
//			info.add_row(i.first, i.second);
//		}
//	}
//
//	info.add(u8"Click for help or more information."sv, render::style::font_size::dialog, render::style::text_style::multiline, ui_element_style::new_line);
//
//	popup._info = info;
//}

void app_frame::update_tooltip()
{
	static view_hover_element hover;

	hover.clear();

	if (df::command_active == 0)
	{
		auto c = setting.show_help_tooltips ? _view_frame->_active_controller : view_controller_ptr{};
		std::shared_ptr<view_host> frame = _view_frame;

		if (!c)
		{
			c = _sidebar->_active_controller;
			frame = _sidebar;
		}

		if (c)
		{
			c->popup_from_location(hover);

			if (hover.id != commands::none)
			{
				tooltip(hover, hover.id);
			}

			const auto window_bounds = frame->frame()->window_bounds();
			hover.window_bounds = hover.window_bounds.offset(window_bounds.top_left());
			frame->_tooltip_bounds = hover.active_bounds;
		}
		else if (setting.show_help_tooltips && _hover_command)
		{
			tooltip(hover, std::any_cast<const commands>(_hover_command->opaque));
			hover.window_bounds = _hover_command_bounds;
			hover.active_bounds = _hover_command_bounds;
		}
	}

	if (!hover.is_empty())
	{
		_bubble->show(hover.elements, hover.window_bounds, hover.x_focus, hover.prefered_size, hover.horizontal);
	}
	else
	{
		_bubble->hide();
	}
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
		const auto idle_delay_ms = 1000 / ui::default_ticks_per_second;

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

void app_frame::invalidate_status()
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

static constexpr std::u8string_view s_recent_folders = u8"RecentFolders"sv;
static constexpr std::u8string_view s_recent_searches = u8"RecentSearches"sv;
static constexpr std::u8string_view s_recent_apps = u8"RecentApps"sv;
static constexpr std::u8string_view s_recent_tags = u8"RecentTags"sv;
static constexpr std::u8string_view s_recent_locations = u8"RecentLocations"sv;
static constexpr std::u8string_view s_group_order = u8"GroupOrder"sv;
static constexpr std::u8string_view s_sort_order = u8"SortOrder"sv;

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

void app_frame::layout(ui::measure_context& mc)
{
	if (!df::is_closing && _app_frame && _tools && !_extent.is_empty())
	{
		update_button_state(true);

		const auto view_mode = _state.view_mode();
		const auto is_full_screen_media = _state.is_full_screen && view_mode == view_type::media;
		const auto is_editing = _state.is_editing();
		const auto show_top_bar = is_editing || !is_full_screen_media;
		const auto show_status_bar = !is_full_screen_media;
		const auto show_sidebar = (setting.show_sidebar && !is_full_screen_media && !_state.is_editing());
		const auto show_edit_controls = view_mode == view_type::edit;

		const auto client_bounds = recti(_extent).inflate(is_full_screen_media ? 0 : -1);

		const auto tools_extent = _tools->measure_toolbar(_extent.cx);
		const auto sorting_extent = _sorting->measure_toolbar(_extent.cx);
		const auto navigate1_extent = _navigate1->measure_toolbar(_extent.cx);
		const auto navigate2_extent = _navigate2->measure_toolbar(_extent.cx);
		const auto navigate3_extent = _navigate3->measure_toolbar(_extent.cx);
		const auto media_edit_extent = _media_edit->measure_toolbar(_extent.cx);
		const auto text_line_height = mc.text_line_height(ui::style::font_size::dialog);
		const auto cy_address = text_line_height + 14;

		const auto top_height = std::max(std::max(navigate1_extent.cy, navigate2_extent.cy),
			std::max(navigate3_extent.cy, cy_address)) + df::round(4 * mc.scale_factor);
		const auto status_height = std::max(std::max(tools_extent.cy, sorting_extent.cy),
			std::max(media_edit_extent.cy, text_line_height)) + df::round(
				10 * mc.scale_factor);

		const auto y_status_top = client_bounds.bottom - status_height;

		const auto y_address = client_bounds.top + (top_height - cy_address) / 2;
		const auto y_nav1 = client_bounds.top + (top_height - navigate1_extent.cy) / 2;
		const auto y_nav2 = client_bounds.top + (top_height - navigate2_extent.cy) / 2;
		const auto y_nav3 = client_bounds.top + (top_height - navigate3_extent.cy) / 2;
		const auto y_tools = y_status_top + (status_height - tools_extent.cy) / 2;
		const auto y_sorting = y_status_top + (status_height - sorting_extent.cy) / 2;
		const auto y_media_edit = y_status_top + (status_height - media_edit_extent.cy) / 2;

		const auto cx_avail = client_bounds.width();
		const auto sidebar_min = 64;
		const auto sidebar_avail = std::max(sidebar_min, cx_avail / 3);
		const auto sidebar_cx = std::clamp(_sidebar->prefered_width(mc), sidebar_min, sidebar_avail);
		const auto toolbar_widths = navigate1_extent.cx + navigate2_extent.cx + navigate3_extent.cx;

		const auto cx_address = std::clamp((client_bounds.width() - toolbar_widths) / 2,
			df::round(300 * mc.scale_factor), df::round(1000 * mc.scale_factor));
		const auto x_addres_center = (client_bounds.left + client_bounds.right) / 2;

		const auto x_address_left = x_addres_center - (cx_address / 2);
		const auto x_address_right = x_addres_center + (cx_address / 2);

		const auto x_nav1 = x_address_left - mc.baseline_snap - navigate1_extent.cx;
		const auto x_nav2 = x_address_right + mc.baseline_snap;
		const auto x_nav3 = client_bounds.right - mc.baseline_snap - navigate3_extent.cx;

		const recti address_bounds(x_address_left, y_address, x_address_right, y_address + cy_address);
		const recti nav1_bounds(x_nav1, y_nav1, x_nav1 + navigate1_extent.cx, y_nav1 + navigate1_extent.cy);
		const recti nav2_bounds(x_nav2, y_nav2, x_nav2 + navigate2_extent.cx, y_nav2 + navigate2_extent.cy);
		const recti nav3_bounds(x_nav3, y_nav3, x_nav3 + navigate3_extent.cx, y_nav3 + navigate3_extent.cy);

		const auto x_tools_avail = cx_avail - sidebar_cx;
		const auto x_tools = (show_sidebar ? sidebar_cx : 0) + (is_full_screen_media
			? (x_tools_avail - tools_extent.cx) / 2
			: mc.baseline_snap);
		const auto x_sorting = client_bounds.right - (mc.baseline_snap + sorting_extent.cx + ui::cx_resize_handle);
		const auto x_media_edit = client_bounds.right - (mc.baseline_snap + media_edit_extent.cx +
			ui::cx_resize_handle);
		const auto y_client = show_top_bar ? client_bounds.top + top_height : client_bounds.top;

		const auto edit_cx = std::max(client_bounds.width() / 4, df::round(400 * mc.scale_factor));

		const recti tool_rect(x_tools, y_tools, x_tools + tools_extent.cx, y_tools + tools_extent.cy);
		const recti sorting_rect(x_sorting, y_sorting, x_sorting + sorting_extent.cx, y_sorting + sorting_extent.cy);
		const recti filter_rect(std::max(tool_rect.right + mc.baseline_snap, x_sorting - cy_address * 5),
			y_status_top + 3, x_sorting - mc.baseline_snap, y_status_top + status_height - 3);
		const recti media_edit_rect(x_media_edit, y_media_edit, x_media_edit + media_edit_extent.cx,
			y_media_edit + media_edit_extent.cy);
		const recti edit_bounds(client_bounds.right - edit_cx, y_client, client_bounds.right, y_status_top);
		const recti sidebar_bounds(client_bounds.left, client_bounds.top, client_bounds.left + sidebar_cx,
			client_bounds.bottom);

		const auto show_sorting = view_mode == view_type::items && tool_rect.right < sorting_rect.left;
		const auto show_tools = !is_editing && show_status_bar && tool_rect.right <= client_bounds.right;
		const auto show_filter = !is_editing && show_status_bar && filter_rect.width() > 16 && view_mode != view_type::Test;
		const auto show_address = show_top_bar && !is_editing && address_bounds.right < nav3_bounds.right;
		const auto show_navigate1 = show_top_bar && !is_editing && nav1_bounds.left > client_bounds.left;
		const auto show_navigate2 = show_top_bar && !is_editing && nav2_bounds.right < nav3_bounds.left;
		const auto show_navigate3 = show_top_bar;
		const auto show_media_edit = is_editing;

		auto view_bounds = client_bounds;

		if (show_top_bar)
		{
			view_bounds.top += top_height;
		}

		if (show_sidebar)
		{
			view_bounds.left += sidebar_cx;
		}

		if (show_status_bar)
		{
			view_bounds.bottom = y_status_top;
		}

		if (show_edit_controls)
		{
			view_bounds.right -= edit_cx;
		}

		const auto title_bounds_padding = df::round(32 * mc.scale_factor);

		_title_bounds = client_bounds;
		_title_bounds.top = client_bounds.top;
		_title_bounds.bottom = client_bounds.top + top_height;
		_title_bounds.left = (show_sidebar ? sidebar_bounds.right : client_bounds.left) + title_bounds_padding;
		_title_bounds.right = nav3_bounds.left - title_bounds_padding;

		ui::control_layouts positions;
		positions.emplace_back(_navigate1, nav1_bounds, show_navigate1);
		positions.emplace_back(_search_edit, address_bounds, show_address);
		positions.emplace_back(_navigate2, nav2_bounds, show_navigate2);
		positions.emplace_back(_navigate3, nav3_bounds, show_navigate3);
		positions.emplace_back(_sidebar->_frame, sidebar_bounds, show_sidebar);
		positions.emplace_back(_view_frame->_frame, view_bounds, _view->_show_render_window);
		positions.emplace_back(_tools, tool_rect, show_tools);
		positions.emplace_back(_filter_edit, filter_rect, show_filter);
		positions.emplace_back(_sorting, sorting_rect, show_sorting);
		positions.emplace_back(_media_edit, media_edit_rect, show_media_edit);
		positions.emplace_back(_edit_view_controls->_dlg, edit_bounds, show_edit_controls);

		if (_search_predictions_frame && _search_predictions_frame->_frame->is_visible())
		{
			_search_predictions_frame->_frame->window_bounds(calc_search_popup_bounds(), true);
		}

		_view_bounds = view_bounds;
		_app_frame->apply_layout(positions, { 0, 0 });
		_status_bounds.set(tool_rect.right, view_bounds.bottom, sorting_rect.left, client_bounds.bottom);
	}
}


df::index_roots index_folders()
{
	df::index_roots result;
	const auto local_folders = platform::local_folders();

	if (setting.index.pictures && !local_folders.pictures.is_empty()) result.folders.emplace(local_folders.pictures);
	if (setting.index.video && !local_folders.video.is_empty()) result.folders.emplace(local_folders.video);
	if (setting.index.music && !local_folders.music.is_empty()) result.folders.emplace(local_folders.music);
	if (setting.index.drop_box && !local_folders.dropbox_photos.is_empty())
		result.folders.emplace(
			local_folders.dropbox_photos);
	if (setting.index.onedrive_pictures && !local_folders.onedrive_pictures.is_empty())
		result.folders.emplace(
			local_folders.onedrive_pictures);
	if (setting.index.onedrive_video && !local_folders.onedrive_video.is_empty())
		result.folders.emplace(
			local_folders.onedrive_video);
	if (setting.index.onedrive_music && !local_folders.onedrive_music.is_empty())
		result.folders.emplace(
			local_folders.onedrive_music);

	df::hash_set<std::u8string, df::ihash, df::ieq> drive_label_includes;
	df::hash_set<std::u8string, df::ihash, df::ieq> drive_label_excludes;

	for (auto f : setting.index.collection_folders())
	{
		if (!str::is_empty(f))
		{
			const auto path = df::folder_path(f);

			if (str::is_exclude(f))
			{
				f = f.substr(1);

				if (path.exists() ||
					platform::is_server(f))
				{
					result.excludes.emplace(df::folder_path(path));
				}
				else
				{
					drive_label_excludes.emplace(f);
				}
			}
			else
			{
				if (path.exists() ||
					platform::is_server(f))
				{
					result.folders.emplace(df::folder_path(path));
				}
				else
				{
					drive_label_includes.emplace(f);
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

		if (drive_label_excludes.contains(d.name))
		{
			result.excludes.emplace(path);
		}

		if (drive_label_includes.contains(vol))
		{
			result.folders.emplace(path);
		}

		if (drive_label_excludes.contains(vol))
		{
			result.excludes.emplace(path);
		}
	}

	for (auto exclude : result.excludes)
	{
		result.folders.erase(exclude);
	}

	return result;
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
				_edit_view_controls->options_changed();
				_navigate1->options_changed();
				_navigate2->options_changed();
				_navigate3->options_changed();
				_media_edit->options_changed();
				_search_edit->options_changed();
				_filter_edit->options_changed();
				_tools->options_changed();
				_sorting->options_changed();

				_state.update_search_is_favorite();

				if (_search_predictions_frame && _search_predictions_frame->_frame)
				{
					_search_predictions_frame->_frame->options_changed();
				}
			}

			if (pop_invalid_flag(_invalids, view_invalid::refresh_items))
			{
				if (df::file_handles_detached == 0 && df::command_active == 0 && !_state.is_editing())
				{
					_state.open(_view_frame, _state.search(), {});
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
				auto token = df::cancel_token(index_version);

				index_task_queue.reset_and_enqueue([this, token]
					{
						_state.item_index.index_roots(index_folders());
						_state.item_index.index_folders(token);
						invalidate_view(view_invalid::sidebar);

						_state.item_index.scan_uncached(token);
						invalidate_view(view_invalid::sidebar | view_invalid::item_scan | view_invalid::refresh_items);
					});
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


void app_frame::hide_search_predictions()
{
	if (_search_completes && _search_predictions_frame && _search_predictions_frame->_frame)
	{
		_search_completes->clear();
		_search_predictions_frame->_frame->show(false);
		focus_view();
	}
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
				const auto selected = _search_completes->selected();
				const auto text = selected ? selected->edit_text() : _search_edit->window_text();
				_state.open(_view_frame, text);
				focus_view();
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

		if (!_edit_controls_have_focus && !_state.is_editing())
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
						(((ac.key_state & keyboard_accelerator_t::control) != 0) == keys.control) &&
						(((ac.key_state & keyboard_accelerator_t::shift) != 0) == keys.shift) &&
						(((ac.key_state & keyboard_accelerator_t::alt) != 0) == keys.alt);

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

	//const auto display = _state.display_state();
	//const auto is_playing = display && display->is_playing();

	_pa->full_screen(_state.is_full_screen);
	//_commands[commands::fullscreen]->checked = _state.is_full_screen;
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
}

void app_frame::focus_changed(const bool has_focus, const ui::control_base_ptr& child)
{
	df::trace(str::format(u8"app_frame::focus {}"sv, has_focus));
	focus_search(_search_edit->has_focus());

	_filter_has_focus = _filter_edit->has_focus();
	_view_has_focus = _view_frame->_frame->has_focus();
	_toolbar_has_focus = _navigate1->has_focus() || _navigate2->has_focus() || _navigate3->has_focus() || _media_edit->
		has_focus() || _tools->has_focus() || _sorting->has_focus();
	_nav_has_focus = _sidebar->_frame->has_focus();
	_edit_controls_have_focus = _edit_view_controls->_dlg->has_focus();

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
	invalidate_view(view_invalid::view_layout |
		view_invalid::group_layout |
		view_invalid::index |
		view_invalid::refresh_items |
		view_invalid::item_scan);

	//_view_host._frame->reset_graphics();
	//free_graphics_resources();
}

void app_frame::view_changed(view_type m)
{
	df::assert_true(ui::is_ui_thread());
	auto v = _view;

	if (m == view_type::edit)
	{
		v = _view_edit;
	}
	else if (m == view_type::items)
	{
		v = _view_items;
	}
	else if (m == view_type::media)
	{
		v = _view_media;
	}
	else if (m == view_type::Test)
	{
		v = _view_test;
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

	const view_element_event e{ view_element_event_type::dpi_changed, nullptr };
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
	if (setting.show_debug_info)
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

		dc.draw_text(str::utf8_cast(text), _status_bounds, ui::style::font_size::dialog,
			ui::style::text_style::single_line_center, ui::color(dc.colors.foreground, dc.colors.alpha), {});
	}

	if (_state.view_mode() == view_type::edit)
	{
		const auto i = _state._edit_item;

		if (i)
		{
			const std::u8string title = format(u8"{}: {}"sv, tt.editing_title, i->name());
			dc.draw_text(title, _title_bounds, ui::style::font_size::title, ui::style::text_style::single_line,
				ui::color(dc.colors.foreground, dc.colors.alpha), {});
		}
	}

	const auto border_outside = recti(_extent);
	const auto clr = ui::style::color::view_background;
	dc.draw_border(border_outside.inflate(-1), border_outside, clr, clr);
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

void app_frame::queue_ui(std::function<void()> f)
{
	_ui_queue.enqueue(std::move(f));
	_pa->queue_idle();
}

void app_frame::queue_async(const async_queue q, std::function<void()> f)
{
	switch (q)
	{
	case async_queue::crc:
		crc_task_queue.enqueue(std::move(f));
		break;
	case async_queue::scan_folder:
		scan_folder_task_queue.enqueue(std::move(f));
		break;
	case async_queue::scan_modified_items:
		scan_modified_items_task_queue.enqueue(std::move(f));
		break;
	case async_queue::scan_displayed_items:
		scan_displayed_items_task_queue.enqueue(std::move(f));
		break;
	case async_queue::load:
		load_task_queue.enqueue(std::move(f));
		break;
	case async_queue::load_raw:
		load_raw_task_queue.reset_and_enqueue(std::move(f));
		break;
	case async_queue::render:
		render_task_queue.enqueue(std::move(f));
		break;
	case async_queue::query:
		query_task_queue.enqueue(std::move(f));
		break;
	case async_queue::index_predictions_single:
		predictions_task_queue.reset_and_enqueue(std::move(f));
		break;
	case async_queue::index_summary_single:
		summary_task_queue.reset_and_enqueue(std::move(f));
		break;
	case async_queue::index_presence_single:
		presence_task_queue.reset_and_enqueue(std::move(f));
		break;
	case async_queue::auto_complete:
		auto_complete_task_queue.enqueue(std::move(f));
		break;
	case async_queue::cloud:
		cloud_task_queue.enqueue(std::move(f));
		break;
	case async_queue::index:
		index_task_queue.enqueue(std::move(f));
		break;
	case async_queue::sidebar:
		sidebar_task_queue.enqueue(std::move(f));
		break;
	case async_queue::web:
		web_task_queue.enqueue(std::move(f));
		break;

	case async_queue::work:
	default:
		work_task_queue.enqueue(std::move(f));
		break;
	}
}

void app_frame::queue_location(std::function<void(location_cache&)> f)
{
	location_task_queue.enqueue([f = std::move(f), &lc = _locations]() { f(lc); });
}

void app_frame::queue_database(std::function<void(database&)> f)
{
	database_task_queue.enqueue([f = std::move(f), &db = _db]() { f(db); });
}

void app_frame::web_service_cache(std::u8string key, std::function<void(const std::u8string&)> f)
{
	queue_database([this, key = std::move(key), f = std::move(f)](database& db)
		{
			auto result = db.web_service_cache(key);

			queue_async(async_queue::cloud, [result, f]()
				{
					f(result);
				});
		});
}

void app_frame::web_service_cache(std::u8string key, std::u8string value)
{
	queue_database([key = std::move(key), value = std::move(value)](database& db)
		{
			db.web_service_cache(key, value);
		});
}

void app_frame::queue_media_preview(std::function<void(media_preview_state&)> f)
{
	platform::exclusive_lock media_lock(media_preview_mutex);
	next_media_preview = std::move(f);
	media_preview_event.set();
}

void app_frame::search_edit_change(const std::u8string& text)
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

	pause_media pause((_state));

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
				detach_file_handles detach((_state));

				const auto allow_undo = true;
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

void app_frame::create_toolbars()
{
	const std::vector<ui::command_ptr> tbButtonsNav1 =
	{
		find_command(commands::view_show_sidebar),
		find_command(commands::info_new_version),
		find_command(commands::menu_test),
		find_command(commands::browse_back),
		find_command(commands::browse_forward),
	};

	const std::vector<ui::command_ptr> tbButtonsNav2 =
	{
		find_command(commands::favourite),
		find_command(commands::browse_parent),
		find_command(commands::browse_previous_folder),
		find_command(commands::browse_next_folder),
		find_command(commands::advanced_search),
		find_command(commands::menu_main),
	};

	const std::vector<ui::command_ptr> tbButtonsNav3 =
	{
		find_command(commands::view_minimize),
		find_command(commands::view_maximize),
		find_command(commands::view_restore),
		find_command(commands::exit),
	};

	const std::vector<ui::command_ptr> tool_buttons =
	{
		find_command(commands::browse_previous_item),
		find_command(commands::browse_next_item),
		nullptr,
		find_command(commands::menu_open),
		find_command(commands::tool_edit),
		find_command(commands::tool_rotate_anticlockwise),
		find_command(commands::tool_rotate_clockwise),
		find_command(commands::menu_tag_with),
		find_command(commands::menu_tools_toolbar),
		find_command(commands::menu_playback)
	};

	const std::vector<ui::command_ptr> sorting_buttons =
	{
		find_command(commands::filter_photos),
		find_command(commands::filter_videos),
		find_command(commands::filter_audio),
		nullptr,
		find_command(commands::browse_recursive),
		find_command(commands::option_toggle_details),
		find_command(commands::option_toggle_item_size),
		find_command(commands::menu_group_toolbar),
	};

	const std::vector<ui::command_ptr> media_edit_buttons =
	{
		find_command(commands::edit_item_save_and_prev),
		find_command(commands::edit_item_save_and_next),
		find_command(commands::edit_item_options),
		find_command(commands::edit_item_save_as),
		find_command(commands::edit_item_save),
		find_command(commands::edit_item_exit),
	};

	ui::toolbar_styles tb_styles;
	tb_styles.button_extent = { 30, 40 };
	_navigate1 = _app_frame->create_toolbar(tb_styles, tbButtonsNav1);
	_navigate2 = _app_frame->create_toolbar(tb_styles, tbButtonsNav2);

	tb_styles.button_extent = { 40, 40 };
	_navigate3 = _app_frame->create_toolbar(tb_styles, tbButtonsNav3);

	tb_styles.xTBSTYLE_LIST = true;
	tb_styles.button_extent = { 0, 0 };

	_tools = _app_frame->create_toolbar(tb_styles, tool_buttons);
	_sorting = _app_frame->create_toolbar(tb_styles, sorting_buttons);
	_media_edit = _app_frame->create_toolbar(tb_styles, media_edit_buttons);
}

static std::u8string format_items_summary(const group_by grouping, const sort_by order,
	const df::file_group_histogram& summary, const bool is_init_complete)
{
	std::u8string_view group_text;
	std::u8string_view sort_text;

	switch (grouping)
	{
	case group_by::presence: group_text = tt.sort_by_presence;
		break;
	case group_by::file_type: group_text = tt.sort_by_file_type;
		break;
	case group_by::shuffle: group_text = tt.sort_by_shuffle;
		break;
	case group_by::size: group_text = tt.sort_by_size;
		break;
	case group_by::extension: group_text = tt.sort_by_extension;
		break;
	case group_by::location: group_text = tt.sort_by_location;
		break;
	case group_by::rating_label: group_text = tt.sort_by_rating_label;
		break;
	case group_by::date_created: group_text = tt.prop_name_created;
		break;
	case group_by::date_modified: group_text = tt.prop_name_modified;
		break;
	case group_by::camera: group_text = tt.prop_name_camera;
		break;
	case group_by::album_show: group_text = tt.sort_by_album_show;
		break;
	case group_by::resolution: group_text = tt.sort_by_resolution;
		break;
	case group_by::folder: group_text = tt.sort_by_Folder;
		break;
	}

	switch (order)
	{
	case sort_by::def: sort_text = tt.sort_by_def;
		break;
	case sort_by::name: sort_text = tt.sort_by_name;
		break;
	case sort_by::size: sort_text = tt.sort_by_size;
		break;
	case sort_by::date_modified: sort_text = tt.prop_name_modified;
		break;
	}

	const auto total_items = summary.total_items();

	if (total_items.count > 0)
	{
		const auto total_count = str::format_count(total_items.count);
		const auto total_size = prop::format_size(total_items.size);

		if (group_text == sort_text || grouping == group_by::shuffle || sort_text.empty() || order == sort_by::def)
		{
			return str::format(u8"{}|{} - {}"sv, total_count, total_size, group_text);
		}

		return str::format(u8"{}|{} - {}|{}"sv, total_count, total_size, group_text, sort_text);
	}
	const auto total_folders = summary.total_folders();

	if (total_folders.count > 0)
	{
		const auto total_count = str::format_count(total_folders.count);

		if (group_text == sort_text || grouping == group_by::shuffle || sort_text.empty() || order == sort_by::def)
		{
			return str::format(u8"{} {} - {}"sv, total_count, tt.folders, group_text);
		}

		return str::format(u8"{} {} - {}|{}"sv, total_count, tt.folders, group_text, sort_text);
	}

	return std::u8string(is_init_complete ? tt.empty : tt.loading);
}

icon_index volumes_icons[] = {
	icon_index::volume3, icon_index::volume2, icon_index::volume1, icon_index::volume0, icon_index::mute
};

void app_frame::toggle_volume()
{
	auto v = setting.media_volume;

	if (v == 0)
	{
		v = media_volumes[0];
	}
	else
	{
		for (const int volume : media_volumes)
		{
			if (v > volume)
			{
				v = volume;
				break;
			}
		}
	}

	setting.media_volume = std::clamp(v, 0, 1000);
	_commands[commands::playback_volume_toggle]->icon = sound_icon();
	_tools->update_button_state(false, false);
}

icon_index app_frame::sound_icon() const
{
	const auto v = setting.media_volume;

	for (auto i = 0u; i < std::size(media_volumes); i++)
	{
		if (v >= media_volumes[i])
		{
			return volumes_icons[i];
		}
	}

	return icon_index::mute;
}


icon_index app_frame::repeat_toggle_icon()
{
	if (setting.repeat == repeat_mode::repeat_one) return icon_index::repeat_one;
	if (setting.repeat == repeat_mode::repeat_all) return icon_index::repeat_all;
	return icon_index::repeat_none;
}

bool app_frame::update_toolbar_text(const commands cc, const std::u8string& text)
{
	const auto command = _commands[cc];
	const auto changed = text != command->toolbar_text;

	if (changed)
	{
		command->toolbar_text = text;
		invalidate_view(view_invalid::app_layout);
	}

	return changed;
}

void app_frame::update_button_state(const bool resize)
{
	static const auto has_tests = known_path(platform::known_folder::test_files_folder).exists();
	static const auto now_days = platform::now().to_days();
	static const auto has_burner = platform::has_burner();

	const auto display = _state.display_state();
	const auto is_playing = display && display->is_playing();
	const auto can_zoom = display && display->can_zoom();
	const auto is_maximized = _app_frame->is_maximized();
	const auto is_editing = df::command_active == 0 && _state.is_editing();
	const auto is_showing_media_or_items_view = df::command_active == 0 && _state.is_showing_media_or_items();
	const auto has_selection = is_showing_media_or_items_view && _state.has_selection();
	const auto new_version_avail = is_showing_media_or_items_view && !setting.install_updates &&
		df::version(s_app_version) < df::version(setting.available_version) && static_cast<int>(now_days) >= setting.
		min_show_update_day;
	const auto show_new_version = is_showing_media_or_items_view && setting.force_available_version ||
		new_version_avail;
	const auto command_item = _state.command_item();
	const auto has_selection_and_is_media_or_items_view = has_selection && is_showing_media_or_items_view;
	const auto is_displaying_item = is_showing_media_or_items_view && command_item;

	_commands[commands::info_new_version]->visible = !is_editing && show_new_version;
	_commands[commands::view_maximize]->visible = !is_maximized;
	_commands[commands::view_restore]->visible = is_maximized;
	_commands[commands::view_show_sidebar]->visible = !is_editing;
	_commands[commands::menu_test]->visible = !is_editing && has_tests;
	_commands[commands::browse_forward]->visible = !is_editing;

	_commands[commands::play]->enable = _state.has_display_items();
	_commands[commands::tool_edit]->enable = _state.can_edit_media();
	_commands[commands::menu_open]->enable = has_selection;
	_commands[commands::menu_tools_toolbar]->enable = has_selection;
	_commands[commands::menu_tag_with]->enable = has_selection;
	_commands[commands::tool_save_current_video_frame]->enable = has_selection;
	_commands[commands::tool_tag]->enable = has_selection_and_is_media_or_items_view;
	_commands[commands::tool_locate]->enable = has_selection_and_is_media_or_items_view;
	_commands[commands::tool_adjust_date]->enable = has_selection_and_is_media_or_items_view;
	_commands[commands::tool_edit_metadata]->enable = has_selection_and_is_media_or_items_view;
	_commands[commands::browse_back]->enable = _state.history.can_browse_back();
	_commands[commands::browse_forward]->enable = _state.history.can_browse_forward();
	_commands[commands::browse_parent]->enable = _state.has_parent_search() || _state.view_mode() != view_type::items;
	_commands[commands::tool_burn]->enable = has_selection_and_is_media_or_items_view && has_burner;
	_commands[commands::view_zoom]->enable = can_zoom;
	_commands[commands::tool_open_with]->enable = has_selection_and_is_media_or_items_view;
	_commands[commands::tool_email]->enable = has_selection_and_is_media_or_items_view;
	_commands[commands::pin_item]->enable = has_selection;
	_commands[commands::rate_none]->enable = has_selection_and_is_media_or_items_view;
	_commands[commands::rate_1]->enable = has_selection_and_is_media_or_items_view;
	_commands[commands::rate_2]->enable = has_selection_and_is_media_or_items_view;
	_commands[commands::rate_3]->enable = has_selection_and_is_media_or_items_view;
	_commands[commands::rate_4]->enable = has_selection_and_is_media_or_items_view;
	_commands[commands::rate_5]->enable = has_selection_and_is_media_or_items_view;
	_commands[commands::rate_rejected]->enable = has_selection_and_is_media_or_items_view;
	_commands[commands::label_approved]->enable = has_selection_and_is_media_or_items_view;
	_commands[commands::label_to_do]->enable = has_selection_and_is_media_or_items_view;
	_commands[commands::label_select]->enable = has_selection_and_is_media_or_items_view;
	_commands[commands::label_review]->enable = has_selection_and_is_media_or_items_view;
	_commands[commands::label_second]->enable = has_selection_and_is_media_or_items_view;

	_commands[commands::browse_open_googlemap]->enable = _state.has_gps();
	_commands[commands::browse_open_containingfolder]->enable = is_displaying_item;
	_commands[commands::browse_open_in_file_browser]->enable = has_selection_and_is_media_or_items_view;
	_commands[commands::tool_new_folder]->enable = _state.search().has_selector();
	_commands[commands::print]->enable = has_selection_and_is_media_or_items_view;
	_commands[commands::tool_remove_metadata]->enable = has_selection_and_is_media_or_items_view;
	_commands[commands::tool_rename]->enable = has_selection_and_is_media_or_items_view;
	_commands[commands::tool_prile_properties]->enable = has_selection_and_is_media_or_items_view;
	_commands[commands::tool_delete]->enable = has_selection_and_is_media_or_items_view;
	_commands[commands::edit_copy]->enable = has_selection_and_is_media_or_items_view;
	_commands[commands::edit_cut]->enable = has_selection_and_is_media_or_items_view;
	_commands[commands::tool_move_to_folder]->enable = has_selection_and_is_media_or_items_view;
	_commands[commands::tool_copy_to_folder]->enable = has_selection_and_is_media_or_items_view;
	_commands[commands::tool_convert]->enable = _state.can_process_selection(
		_view_frame, df::process_items_type::photos_only);
	//_commands[ID_SHARE_FACEBOOK]->enable = _state.can_process_selection(df::process_items_type::photos_only);
	//_commands[ID_SHARE_FLICKR]->enable = _state.can_process_selection(df::process_items_type::photos_only);
	//_commands[ID_SHARE_TWITTER]->enable = _state.can_process_selection(df::process_items_type::photos_only);
	_commands[commands::edit_paste]->enable = is_showing_media_or_items_view && _state.search().has_selector();
	_commands[commands::select_nothing]->enable = has_selection;
	_commands[commands::browse_recursive]->enable = _state.search().has_selector();
	_commands[commands::tool_rotate_anticlockwise]->enable = has_selection_and_is_media_or_items_view;
	_commands[commands::tool_rotate_clockwise]->enable = has_selection_and_is_media_or_items_view;
	_commands[commands::tool_desktop_background]->enable = display && (display->
		display_item_has_trait(file_type_traits::bitmap) || display->player_has_video());
	_commands[commands::search_related]->enable = is_displaying_item;
	_commands[commands::edit_item_exit]->enable = is_editing;
	_commands[commands::edit_item_save]->enable = is_editing;
	_commands[commands::edit_item_save_and_prev]->enable = is_editing;
	_commands[commands::edit_item_save_and_next]->enable = is_editing;
	_commands[commands::edit_item_save_as]->enable = is_editing;
	_commands[commands::edit_item_options]->enable = is_editing;

	_commands[commands::playback_auto_play]->checked = setting.auto_play;
	_commands[commands::playback_last_played_pos]->checked = setting.last_played_pos;
	_commands[commands::playback_repeat_one]->checked = setting.repeat == repeat_mode::repeat_one;
	_commands[commands::playback_repeat_all]->checked = setting.repeat == repeat_mode::repeat_all;
	_commands[commands::playback_repeat_none]->checked = setting.repeat == repeat_mode::repeat_none;
	_commands[commands::play]->checked = is_playing;

	/*_commands[commands::rate_rejected]->checked = is_labeled(sv_rejected);
	_commands[commands::label_approved]->checked = has_selection_and_is_media_or_items_view;
	_commands[commands::label_to_do]->checked = has_selection_and_is_media_or_items_view;
	_commands[commands::label_select]->checked = has_selection_and_is_media_or_items_view;
	_commands[commands::label_review]->checked = has_selection_and_is_media_or_items_view;
	_commands[commands::label_second]->checked = has_selection_and_is_media_or_items_view;*/

	_commands[commands::pin_item]->checked = _state.has_pin();
	_commands[commands::option_highlight_large_items]->checked = setting.highlight_large_items;
	_commands[commands::sort_dates_descending]->checked = setting.sort_dates_descending;
	_commands[commands::sort_dates_ascending]->checked = !setting.sort_dates_descending;
	_commands[commands::view_show_sidebar]->checked = setting.show_sidebar;
	_commands[commands::option_scale_up]->checked = setting.scale_up;
	_commands[commands::option_show_rotated]->checked = setting.show_rotated;
	_commands[commands::verbose_metadata]->checked = setting.verbose_metadata;
	_commands[commands::show_raw_preview]->checked = setting.raw_preview;
	_commands[commands::run_tests]->checked = _state.view_mode() == view_type::Test;
	_commands[commands::view_items]->checked = _state.view_mode() == view_type::items;
	_commands[commands::option_show_thumbnails]->checked = _state.view_mode() == view_type::items;
	_commands[commands::browse_recursive]->checked = _state.search().has_recursive_selector();
	_commands[commands::filter_photos]->checked = _state.filter().has_group(file_group::photo);
	_commands[commands::filter_videos]->checked = _state.filter().has_group(file_group::video);
	_commands[commands::filter_audio]->checked = _state.filter().has_group(file_group::audio);
	_commands[commands::large_font]->checked = setting.large_font;
	_commands[commands::view_fullscreen]->checked = _state.is_full_screen;
	_commands[commands::option_toggle_details]->checked = setting.detail_file_items;

	_commands[commands::group_album]->checked = _state.group_order() == group_by::album_show;
	_commands[commands::group_camera]->checked = _state.group_order() == group_by::camera;
	_commands[commands::group_created]->checked = _state.group_order() == group_by::date_created;
	_commands[commands::group_presence]->checked = _state.group_order() == group_by::presence;
	_commands[commands::group_file_type]->checked = _state.group_order() == group_by::file_type;
	_commands[commands::group_location]->checked = _state.group_order() == group_by::location;
	_commands[commands::group_modified]->checked = _state.group_order() == group_by::date_modified;
	_commands[commands::group_pixels]->checked = _state.group_order() == group_by::resolution;
	_commands[commands::group_rating]->checked = _state.group_order() == group_by::rating_label;
	_commands[commands::group_shuffle]->checked = _state.group_order() == group_by::shuffle;
	_commands[commands::group_size]->checked = _state.group_order() == group_by::size;
	_commands[commands::group_extension]->checked = _state.group_order() == group_by::extension;
	_commands[commands::group_folder]->checked = _state.group_order() == group_by::folder;
	_commands[commands::sort_def]->checked = _state.sort_order() == sort_by::def;
	_commands[commands::sort_name]->checked = _state.sort_order() == sort_by::name;
	_commands[commands::sort_size]->checked = _state.sort_order() == sort_by::size;
	_commands[commands::sort_date_modified]->checked = _state.sort_order() == sort_by::date_modified;
	_commands[commands::english]->checked = setting.language == u8"en"sv;

	_commands[commands::playback_volume100]->checked = setting.media_volume == media_volumes[0];
	_commands[commands::playback_volume75]->checked = setting.media_volume == media_volumes[1];
	_commands[commands::playback_volume50]->checked = setting.media_volume == media_volumes[2];
	_commands[commands::playback_volume25]->checked = setting.media_volume == media_volumes[3];
	_commands[commands::playback_volume0]->checked = setting.media_volume == media_volumes[4];

	_commands[commands::play]->icon = is_playing ? icon_index::pause : icon_index::play;
	_commands[commands::view_fullscreen]->icon = _state.is_full_screen
		? icon_index::fullscreen_exit
		: icon_index::fullscreen;
	_commands[commands::playback_volume_toggle]->icon = sound_icon();
	_commands[commands::repeat_toggle]->icon = repeat_toggle_icon();
	_commands[commands::favourite]->icon = _state.search_is_favorite() ? icon_index::star_solid : icon_index::star;


	const auto summary_text = format_items_summary(_state.group_order(), _state.sort_order(), _state.summary_shown(),
		_state.item_index.is_init_complete());

	auto toolbar_text_changed = update_toolbar_text(commands::menu_group_toolbar, summary_text);
	toolbar_text_changed |= update_toolbar_text(commands::filter_photos,
		str::format_count(_state.count_total(file_group::photo), true));
	toolbar_text_changed |= update_toolbar_text(commands::filter_videos,
		str::format_count(_state.count_total(file_group::video), true));
	toolbar_text_changed |= update_toolbar_text(commands::filter_audio,
		str::format_count(_state.count_total(file_group::audio), true));

	_tools->update_button_state(resize, false);
	_sorting->update_button_state(resize, toolbar_text_changed);
	_navigate1->update_button_state(resize, false);
	_navigate2->update_button_state(resize, false);
	_navigate3->update_button_state(resize, false);
	_media_edit->update_button_state(resize, false);

	const view_element_event e{ view_element_event_type::update_command_state, _view_frame };
	_view->broadcast_event(e);
}

void app_frame::update_address() const
{
	const auto& search = _state.search();
	auto icon = icon_index::search;
	std::u8string text;

	switch (_state.view_mode())
	{
	case view_type::Test:
		text = u8"Testing"sv;
		icon = icon_index::check;
		break;
	case view_type::media:
	case view_type::items:
		if (search.has_selector()) icon = icon_index::folder;
		else if (search.has_terms()) icon = search.first_type()->icon;
		text = search.text();
		break;
	case view_type::edit:
		text = _state._edit_item ? _state._edit_item->path().str() : std::u8string{};
		icon = _state.displayed_item_icon();
		break;
	}

	if (icon == icon_index::star)
	{
		icon = icon_index::search;
	}

	if (!_search_has_focus)
	{
		_search_edit->window_text(text);
		_search_edit->set_icon(icon);
	}
}

recti app_frame::calc_search_popup_bounds() const
{
	const auto edit_bounds = _search_edit->window_bounds();
	const auto height = _search_predictions_frame ? _search_predictions_frame->height() : 320;
	return { edit_bounds.left + 8, edit_bounds.bottom, edit_bounds.right - 8, edit_bounds.bottom + height };
}


void app_frame::update_command_text()
{
	def_command(commands::tool_adjust_date, command_group::tools, icon_index::time, tt.command_adjust_date);
	def_command(commands::tool_edit_metadata, command_group::tools, icon_index::edit_metadata,
		tt.command_edit_metadata);
	def_command(commands::exit, command_group::help, icon_index::close, tt.command_app_exit);
	def_command(commands::playback_auto_play, command_group::media_playback, icon_index::play, tt.command_autoplay);
	def_command(commands::playback_last_played_pos, command_group::media_playback, icon_index::none,
		tt.command_last_played_pos);
	def_command(commands::browse_back, command_group::navigation, icon_index::back, tt.command_browse_back);
	def_command(commands::browse_forward, command_group::navigation, icon_index::next, tt.command_browse_forward);
	def_command(commands::browse_next_folder, command_group::navigation, icon_index::next_folder,
		tt.command_browse_next_folder);
	def_command(commands::browse_next_group, command_group::selection, icon_index::none, tt.command_browse_next_group);
	def_command(commands::browse_next_item, command_group::selection, icon_index::next_image,
		tt.command_browse_next_item);
	def_command(commands::browse_next_item_extend, command_group::selection, icon_index::next_image,
		tt.command_browse_next_item_extend);
	def_command(commands::browse_parent, command_group::navigation, icon_index::parent, tt.command_browse_parent);
	def_command(commands::browse_previous_folder, command_group::navigation, icon_index::back_folder,
		tt.command_browse_previous_folder);
	def_command(commands::browse_previous_group, command_group::selection, icon_index::none,
		tt.command_browse_previous_group);
	def_command(commands::browse_previous_item, command_group::selection, icon_index::back_image,
		tt.command_browse_previous_item);
	def_command(commands::browse_previous_item_extend, command_group::selection, icon_index::back_image,
		tt.command_browse_previous_item_extend);
	def_command(commands::tool_burn, command_group::tools, icon_index::disk, tt.command_burn);
	def_command(commands::tool_save_current_video_frame, command_group::tools, icon_index::none, tt.command_capture);
	def_command(commands::edit_item_exit, command_group::none, icon_index::close, tt.command_close);
	def_command(commands::edit_item_color_reset, command_group::none, icon_index::undo, tt.command_color_reset,
		tt.tooltip_color_reset);
	def_command(commands::tool_convert, command_group::tools, icon_index::convert, tt.command_convert_or_resize);
	def_command(commands::tool_copy_to_folder, command_group::file_management, icon_index::copy_to_folder,
		tt.command_copy);
	def_command(commands::test_crash, command_group::none, icon_index::none, tt.command_crash);
	def_command(commands::test_boom, command_group::none, icon_index::none, tt.command_boom);
	def_command(commands::tool_delete, command_group::file_management, icon_index::cancel, tt.command_delete);
	def_command(commands::tool_desktop_background, command_group::tools, icon_index::wallpaper,
		tt.command_desktop_background);
	def_command(commands::menu_display_options, command_group::none, icon_index::none, tt.command_display_options);
	def_command(commands::tool_edit, command_group::tools, icon_index::edit, tt.command_edit,
		str::format(u8"{}\n{}"sv, tt.tooltip_edit1, tt.tooltip_edit2));
	def_command(commands::edit_copy, command_group::file_management, icon_index::edit_copy, tt.command_edit_copy);
	def_command(commands::edit_cut, command_group::file_management, icon_index::edit_cut, tt.command_edit_cut);
	def_command(commands::edit_paste, command_group::file_management, icon_index::edit_paste, tt.command_edit_paste);
	def_command(commands::tool_eject, command_group::file_management, icon_index::eject, tt.command_eject);
	def_command(commands::tool_prile_properties, command_group::file_management, icon_index::none,
		tt.command_file_properties);
	def_command(commands::browse_search, command_group::navigation, icon_index::search, tt.command_file_search);
	def_command(commands::browse_recursive, command_group::navigation, icon_index::recursive, tt.command_flatten);
	def_command(commands::view_fullscreen, command_group::media_playback, icon_index::fullscreen, tt.command_fullscreen,
		tt.tooltip_fullscreen);
	def_command(commands::option_highlight_large_items, command_group::options, icon_index::none,
		tt.command_highlight_large_items);
	def_command(commands::tool_import, command_group::tools, icon_index::import, tt.command_import);
	def_command(commands::sync, command_group::tools, icon_index::sync, tt.command_sync);
	def_command(commands::options_collection, command_group::options, icon_index::none, tt.command_collection_options);
	def_command(commands::keyboard, command_group::help, icon_index::keyboard, tt.command_keyboard);
	def_command(commands::tool_locate, command_group::tools, icon_index::location, tt.command_locate);
	def_command(commands::view_maximize, command_group::help, icon_index::maximize, tt.command_maximize);
	def_command(commands::view_minimize, command_group::help, icon_index::minimize, tt.command_minimize);
	def_command(commands::tool_move_to_folder, command_group::file_management, icon_index::move_to_folder,
		tt.command_move);
	def_command(commands::view_show_sidebar, command_group::options, icon_index::navigation, tt.command_nav_bar,
		tt.tooltip_nav_bar);
	def_command(commands::menu_navigate, command_group::none, icon_index::none, tt.command_navigate);
	def_command(commands::tool_new_folder, command_group::file_management, icon_index::new_folder,
		tt.command_new_folder);
	def_command(commands::info_new_version, command_group::none, icon_index::lightbulb, tt.command_new_version);
	def_command(commands::menu_open, command_group::none, icon_index::open_one, tt.command_open, tt.tooltip_open);
	def_command(commands::browse_open_containingfolder, command_group::navigation, icon_index::none,
		tt.command_show_in_folder);
	def_command(commands::browse_open_googlemap, command_group::open, icon_index::location, tt.command_open_google_map);
	def_command(commands::browse_open_in_file_browser, command_group::open, icon_index::none,
		tt.command_show_in_file_browser);
	def_command(commands::tool_open_with, command_group::open, icon_index::open_one, tt.command_open_with);
	def_command(commands::options_general, command_group::options, icon_index::settings, tt.command_options);
	def_command(commands::pin_item, command_group::selection, icon_index::pin, tt.command_pin, tt.tooltip_pin);
	def_command(commands::play, command_group::media_playback, icon_index::play, tt.command_play, tt.tooltip_play);
	def_command(commands::print, command_group::tools, icon_index::print, tt.command_print);
	def_command(commands::rate_none, command_group::rate_flag, icon_index::none, tt.command_rate_0);
	def_command(commands::rate_1, command_group::rate_flag, icon_index::none, tt.command_rate_1);
	def_command(commands::rate_2, command_group::rate_flag, icon_index::none, tt.command_rate_2);
	def_command(commands::rate_3, command_group::rate_flag, icon_index::none, tt.command_rate_3);
	def_command(commands::rate_4, command_group::rate_flag, icon_index::none, tt.command_rate_4);
	def_command(commands::rate_5, command_group::rate_flag, icon_index::none, tt.command_rate_5);
	def_command(commands::rate_rejected, command_group::rate_flag, icon_index::none, tt.command_rate_rejected);
	def_command(commands::label_approved, command_group::rate_flag, icon_index::none, tt.command_label_approved);
	def_command(commands::label_to_do, command_group::rate_flag, icon_index::none, tt.command_label_to_do);
	def_command(commands::label_select, command_group::rate_flag, icon_index::none, tt.command_label_select);
	def_command(commands::label_review, command_group::rate_flag, icon_index::none, tt.command_label_review);
	def_command(commands::label_second, command_group::rate_flag, icon_index::none, tt.command_label_second);
	def_command(commands::label_none, command_group::rate_flag, icon_index::none, tt.command_label_none);
	def_command(commands::refresh, command_group::navigation, icon_index::refresh, tt.command_refresh);
	def_command(commands::search_related, command_group::tools, icon_index::compare, tt.command_related);
	def_command(commands::tool_rename, command_group::file_management, icon_index::rename, tt.command_rename);
	def_command(commands::playback_repeat_none, command_group::options, icon_index::repeat_none, tt.command_repeat_none,
		tt.repeat_off_help);
	def_command(commands::playback_repeat_one, command_group::options, icon_index::repeat_one, tt.command_repeat_one,
		tt.repeat_one_help);
	def_command(commands::playback_repeat_all, command_group::options, icon_index::repeat_all, tt.command_repeat_all,
		tt.repeat_help);
	def_command(commands::playback_menu, command_group::media_playback, icon_index::media_options,
		tt.command_playback_menu);
	def_command(commands::menu_playback, command_group::media_playback, icon_index::media_options,
		tt.command_playback_toolbar, tt.command_playback_menu);
	def_command(commands::repeat_toggle, command_group::media_playback, icon_index::repeat_all,
		tt.command_repeat_toggle);
	def_command(commands::view_restore, command_group::help, icon_index::restore, tt.command_restore);
	def_command(commands::tool_rotate_anticlockwise, command_group::tools, icon_index::rotate_anticlockwise,
		tt.command_rotate_anticlockwise);
	def_command(commands::tool_rotate_clockwise, command_group::tools, icon_index::rotate_clockwise,
		tt.command_rotate_clockwise);
	def_command(commands::tool_rotate_reset, command_group::none, icon_index::undo, tt.command_rotate_reset,
		tt.tooltip_rotate_reset);
	def_command(commands::run_tests, command_group::none, icon_index::none, tt.command_run_tests);
	def_command(commands::edit_item_save, command_group::edit_item, icon_index::save, tt.command_save);
	def_command(commands::edit_item_save_and_prev, command_group::edit_item, icon_index::back_image,
		tt.command_save_and_back, tt.command_save_and_back_tooltip);
	def_command(commands::edit_item_save_and_next, command_group::edit_item, icon_index::next_image,
		tt.command_save_and_next, tt.command_save_and_next_tooltip);
	def_command(commands::edit_item_save_as, command_group::edit_item, icon_index::save_copy, tt.command_save_as);
	def_command(commands::edit_item_options, command_group::edit_item, icon_index::settings, tt.command_save_options);
	def_command(commands::option_scale_up, command_group::options, icon_index::fit, tt.command_scale_up,
		tt.tooltip_scale_up);
	def_command(commands::tool_scan, command_group::tools, icon_index::scan, tt.command_scan);
	def_command(commands::options_sidebar, command_group::options, icon_index::none, tt.command_customise);
	def_command(commands::select_all, command_group::selection, icon_index::none, tt.command_select_all);
	def_command(commands::select_invert, command_group::selection, icon_index::none, tt.command_select_invert);
	def_command(commands::select_nothing, command_group::selection, icon_index::none, tt.command_select_nothing);
	def_command(commands::tool_email, command_group::tools, icon_index::mail, tt.command_share_email);
	def_command(commands::option_show_thumbnails, command_group::options, icon_index::items,
		tt.command_show_thumbnails);
	def_command(commands::option_show_rotated, command_group::options, icon_index::orientation, tt.item_oriented);
	def_command(commands::verbose_metadata, command_group::options, icon_index::verbose_metadata,
		tt.show_verbose_metadata);
	def_command(commands::show_raw_preview, command_group::options, icon_index::preview, tt.preview_show_preview);
	def_command(commands::tool_tag, command_group::tools, icon_index::tag, tt.prop_name_tag);
	def_command(commands::menu_tag_with, command_group::none, icon_index::tag, tt.prop_name_tag, tt.tooltip_tag_with);
	def_command(commands::menu_language, command_group::none, icon_index::language, tt.command_language,
		tt.tooltip_language);
	def_command(commands::english, command_group::none, icon_index::none, u8"English"sv, u8"English language"sv);
	def_command(commands::test_new_version, command_group::none, icon_index::none, tt.command_test_new_version);
	def_command(commands::option_toggle_details, command_group::options, icon_index::details, tt.command_toggle_details,
		tt.tooltip_toggle_details_all);
	def_command(commands::option_toggle_item_size, command_group::options, icon_index::zoom_in,
		tt.command_toggle_item_size);
	def_command(commands::menu_tools_toolbar, command_group::none, icon_index::tools, tt.command_tools,
		tt.tooltip_tools);
	def_command(commands::menu_tools, command_group::none, icon_index::tools, tt.command_tools, tt.tooltip_tools);
	def_command(commands::view_help, command_group::help, icon_index::question, tt.command_view_help);
	def_command(commands::view_items, command_group::options, icon_index::items, tt.command_view_items);
	def_command(commands::large_font, command_group::options, icon_index::none, tt.command_view_large_font);
	def_command(commands::menu_main, command_group::none, icon_index::more, tt.command_view_menu, tt.tooltip_view_menu);
	def_command(commands::menu_rate_or_label, command_group::none, icon_index::none, tt.command_view_rate_label);
	def_command(commands::menu_select, command_group::none, icon_index::none, tt.command_view_select);
	def_command(commands::menu_group_toolbar, command_group::none, icon_index::group, tt.command_view_sort);
	def_command(commands::filter_photos, command_group::selection, icon_index::photo, tt.command_filter_photos);
	def_command(commands::filter_videos, command_group::selection, icon_index::video, tt.command_filter_videos);
	def_command(commands::filter_audio, command_group::selection, icon_index::audio, tt.command_filter_audio);
	def_command(commands::menu_group, command_group::none, icon_index::group, tt.command_menu_group_sort);
	def_command(commands::menu_test, command_group::none, icon_index::check, tt.command_view_tests,
		tt.tooltip_view_tests);
	def_command(commands::playback_volume100, command_group::media_playback, icon_index::volume3, tt.command_volume100);
	def_command(commands::playback_volume75, command_group::media_playback, icon_index::volume2, tt.command_volume75);
	def_command(commands::playback_volume50, command_group::media_playback, icon_index::volume1, tt.command_volume50);
	def_command(commands::playback_volume25, command_group::media_playback, icon_index::volume0, tt.command_volume25);
	def_command(commands::playback_volume0, command_group::media_playback, icon_index::mute, tt.command_volume0);
	def_command(commands::playback_volume_toggle, command_group::media_playback, icon_index::volume3,
		tt.command_toggle_volume);
	def_command(commands::view_zoom, command_group::media_playback, icon_index::zoom_in, tt.command_zoom);

	def_command(commands::favourite, command_group::navigation, icon_index::star, tt.command_favorite);
	def_command(commands::advanced_search, command_group::navigation, icon_index::search, tt.command_advanced_search);

	def_command(commands::group_album, command_group::group_by, icon_index::none, tt.command_group_album);
	def_command(commands::group_presence, command_group::group_by, icon_index::none, tt.command_group_presence);
	def_command(commands::group_camera, command_group::group_by, icon_index::none, tt.command_group_camera);
	def_command(commands::group_created, command_group::group_by, icon_index::none, tt.command_group_created);
	def_command(commands::group_file_type, command_group::group_by, icon_index::none, tt.command_group_file_type);
	def_command(commands::group_location, command_group::group_by, icon_index::none, tt.command_group_location);
	def_command(commands::group_modified, command_group::group_by, icon_index::none, tt.command_group_modified);
	def_command(commands::group_pixels, command_group::group_by, icon_index::none, tt.command_group_resolution);
	def_command(commands::group_rating, command_group::group_by, icon_index::none, tt.command_group_rating);
	def_command(commands::group_shuffle, command_group::group_by, icon_index::none, tt.command_group_shuffle);
	def_command(commands::group_size, command_group::group_by, icon_index::none, tt.command_group_size);
	def_command(commands::group_extension, command_group::group_by, icon_index::none, tt.command_group_extension);
	def_command(commands::group_folder, command_group::group_by, icon_index::none, tt.command_group_folder);
	def_command(commands::group_toggle, command_group::group_by, icon_index::none, tt.command_toggle_group_by);

	def_command(commands::sort_dates_descending, command_group::options, icon_index::none,
		tt.command_sort_dates_descending);
	def_command(commands::sort_dates_ascending, command_group::options, icon_index::none,
		tt.command_sort_dates_ascending);
	def_command(commands::sort_name, command_group::sort_by, icon_index::none, tt.command_sort_name);
	def_command(commands::sort_size, command_group::sort_by, icon_index::none, tt.command_sort_size);
	def_command(commands::sort_def, command_group::sort_by, icon_index::none, tt.command_sort_def);
	def_command(commands::sort_date_modified, command_group::sort_by, icon_index::none, tt.command_sort_date_modified);

	_commands[commands::browse_previous_item]->keyboard_accelerator_text = tt.keyboard_left;
	_commands[commands::browse_next_item]->keyboard_accelerator_text = tt.keyboard_right;

	const auto control = keyboard_accelerator_t::control;
	const auto shift = keyboard_accelerator_t::shift;
	const auto alt = keyboard_accelerator_t::alt;

	_commands[commands::rate_none]->kba.emplace_back('0');
	_commands[commands::rate_1]->kba.emplace_back('1');
	_commands[commands::rate_2]->kba.emplace_back('2');
	_commands[commands::rate_3]->kba.emplace_back('3');
	_commands[commands::rate_4]->kba.emplace_back('4');
	_commands[commands::rate_5]->kba.emplace_back('5');
	_commands[commands::rate_rejected]->kba.emplace_back(keys::DEL, alt);
	_commands[commands::label_select]->kba.emplace_back('6');
	_commands[commands::label_second]->kba.emplace_back('7');
	_commands[commands::label_approved]->kba.emplace_back('8');
	_commands[commands::label_review]->kba.emplace_back('9');


	_commands[commands::pin_item]->kba.emplace_back('A');
	_commands[commands::select_all]->kba.emplace_back('A', control);
	_commands[commands::tool_edit_metadata]->kba.emplace_back('E', control);
	_commands[commands::tool_copy_to_folder]->kba.emplace_back('C', shift | control);
	_commands[commands::edit_copy]->kba.emplace_back('C', control);
	_commands[commands::select_invert]->kba.emplace_back('D', control);
	_commands[commands::tool_adjust_date]->kba.emplace_back('D', shift | control);
	_commands[commands::tool_edit]->kba.emplace_back('E');
	_commands[commands::tool_eject]->kba.emplace_back('E', shift | control);
	_commands[commands::browse_search]->kba.emplace_back('F', control);
	_commands[commands::option_toggle_details]->kba.emplace_back('K');
	_commands[commands::tool_locate]->kba.emplace_back('L');
	_commands[commands::repeat_toggle]->kba.emplace_back('L', control);
	_commands[commands::rate_rejected]->kba.emplace_back('M');
	_commands[commands::tool_new_folder]->kba.emplace_back('N', control);
	_commands[commands::view_items]->kba.emplace_back('N', shift | control);
	_commands[commands::browse_open_containingfolder]->kba.emplace_back('O', shift | control);
	_commands[commands::label_to_do]->kba.emplace_back('P');
	_commands[commands::search_related]->kba.emplace_back('R');
	_commands[commands::tool_tag]->kba.emplace_back('T');
	_commands[commands::tool_save_current_video_frame]->kba.emplace_back('S', control);
	_commands[commands::select_nothing]->kba.emplace_back('U', control);
	_commands[commands::edit_paste]->kba.emplace_back('V', control);
	_commands[commands::edit_cut]->kba.emplace_back('X', control);
	_commands[commands::tool_move_to_folder]->kba.emplace_back('X', shift | control);
	_commands[commands::browse_back]->kba.emplace_back(keys::BACK);
	_commands[commands::tool_delete]->kba.emplace_back(keys::DEL);
	_commands[commands::edit_cut]->kba.emplace_back(keys::DEL, shift);
	_commands[commands::group_toggle]->kba.emplace_back(keys::DOWN, alt);
	_commands[commands::group_file_type]->kba.emplace_back('K', control);
	_commands[commands::group_created]->kba.emplace_back('K', control | shift);

	_commands[commands::view_help]->kba.emplace_back(keys::F1);
	_commands[commands::keyboard]->kba.emplace_back(keys::F1, control);
	_commands[commands::tool_rename]->kba.emplace_back(keys::F2);
	_commands[commands::browse_search]->kba.emplace_back(keys::F3);
	_commands[commands::browse_search]->kba.emplace_back(keys::BROWSER_SEARCH);
	_commands[commands::advanced_search]->kba.emplace_back(keys::F3, control);
	_commands[commands::view_show_sidebar]->kba.emplace_back(keys::F4);
	_commands[commands::view_show_sidebar]->kba.emplace_back(keys::BROWSER_FAVORITES);
	_commands[commands::refresh]->kba.emplace_back(keys::F5);
	_commands[commands::options_general]->kba.emplace_back(keys::F6);
	_commands[commands::options_sidebar]->kba.emplace_back(keys::F6, shift | control);
	_commands[commands::options_collection]->kba.emplace_back(keys::F6, control);
	_commands[commands::playback_auto_play]->kba.emplace_back(keys::F7, control);
	_commands[commands::playback_volume_toggle]->kba.emplace_back(keys::F7);
	_commands[commands::tool_convert]->kba.emplace_back(keys::F8);
	_commands[commands::tool_import]->kba.emplace_back(keys::F9);
	_commands[commands::browse_recursive]->kba.emplace_back(keys::F9, control);
	_commands[commands::sync]->kba.emplace_back(keys::F9, shift | control);
	_commands[commands::tool_email]->kba.emplace_back(keys::F10);
	_commands[commands::option_scale_up]->kba.emplace_back(keys::F11, shift | control);
	_commands[commands::view_fullscreen]->kba.emplace_back(keys::F11);
	_commands[commands::view_fullscreen]->kba.emplace_back(keys::SPACE, shift | control);
	_commands[commands::option_show_thumbnails]->kba.emplace_back(keys::F11, shift);
	_commands[commands::edit_copy]->kba.emplace_back(keys::INSERT, control);
	_commands[commands::edit_paste]->kba.emplace_back(keys::INSERT, shift);
	_commands[commands::browse_back]->kba.emplace_back(keys::LEFT, alt);
	_commands[commands::browse_previous_folder]->kba.emplace_back(keys::LEFT, alt | control);
	_commands[commands::browse_previous_item_extend]->kba.emplace_back(keys::LEFT, control);
	_commands[commands::tool_rotate_anticlockwise]->kba.emplace_back(keys::OEM_4);
	_commands[commands::tool_rotate_clockwise]->kba.emplace_back(keys::OEM_6);
	_commands[commands::option_toggle_item_size]->kba.emplace_back(keys::OEM_PLUS, control);
	_commands[commands::large_font]->kba.emplace_back(keys::OEM_PLUS, shift | control);
	_commands[commands::tool_prile_properties]->kba.emplace_back(keys::RETURN, shift | control);
	_commands[commands::browse_open_in_file_browser]->kba.emplace_back(keys::RETURN, shift);
	_commands[commands::tool_open_with]->kba.emplace_back(keys::RETURN, control);
	_commands[commands::view_zoom]->kba.emplace_back(keys::SPACE, control);
	_commands[commands::play]->kba.emplace_back(keys::SPACE);
	_commands[commands::browse_forward]->kba.emplace_back(keys::RIGHT, alt);
	_commands[commands::browse_next_folder]->kba.emplace_back(keys::RIGHT, alt | control);
	_commands[commands::browse_next_item_extend]->kba.emplace_back(keys::RIGHT, control);
	_commands[commands::browse_parent]->kba.emplace_back(keys::UP, alt);
	_commands[commands::browse_previous_group]->kba.emplace_back(keys::PRIOR);
	_commands[commands::browse_next_group]->kba.emplace_back(keys::NEXT);

	_commands[commands::edit_item_save_and_prev]->kba.emplace_back(keys::LEFT, alt);
	_commands[commands::edit_item_save_and_next]->kba.emplace_back(keys::RIGHT, alt);
	_commands[commands::edit_item_save]->kba.emplace_back(keys::RETURN, alt);
	_commands[commands::edit_item_exit]->kba.emplace_back(keys::ESCAPE);

	for (const auto& c : _commands)
	{
		c.second->keyboard_accelerator_text = format_keyboard_accelerator(c.second->kba);
	}
}


command_info_ptr app_frame::find_or_create_command_info(const commands id)
{
	const auto found = _commands.find(id);
	if (found != _commands.end()) return found->second;
	return _commands[id] = std::make_shared<ui::command>();
}

void app_frame::add_command_invoke(const commands id, std::function<void()> invoke)
{
	find_or_create_command_info(id)->invoke = std::move(invoke);
}

void app_frame::def_command(const commands id, const command_group group, const icon_index icon,
	const std::u8string_view text, const std::u8string_view tooltip)
{
	const auto c = find_or_create_command_info(id);
	c->group = group;
	c->icon = icon;
	c->text = text;
	c->tooltip_text = tooltip;
	c->kba.clear();
	c->keyboard_accelerator_text.clear();
}

ui::command_ptr app_frame::find_command(const commands id) const
{
	const auto it = _commands.find(id);

	if (it != _commands.cend())
	{
		return it->second;
	}

	return nullptr;
}

void app_frame::tooltip(view_hover_element& hover, const commands id)
{
	const auto command = find_command(id);

	if (command)
	{
		if (command->icon != icon_index::none)
		{
			hover.elements.add(make_icon_element(command->icon, view_element_style::no_break));
		}

		if (!command->text.empty())
		{
			hover.elements.add(std::make_shared<text_element>(command->text, ui::style::font_size::dialog,
				ui::style::text_style::multiline,
				view_element_style::line_break));
		}

		if (!command->tooltip_text.empty())
		{
			hover.elements.add(std::make_shared<text_element>(command->tooltip_text, ui::style::font_size::dialog,
				ui::style::text_style::multiline,
				view_element_style::line_break));
		}
	}

	std::u8string keyboard_accelerator;

	if (id == commands::browse_previous_item || id == commands::browse_next_item)
	{
		const auto forward = id == commands::browse_next_item;
		const auto i = _state.next_item(forward, false);

		if (i)
		{
			const auto& image = i->thumbnail();

			if (is_valid(image))
			{
				files ff;
				const auto surface = ff.image_to_surface(image);

				if (is_valid(surface))
				{
					hover.elements.add(
						std::make_shared<surface_element>(surface, 200,
							view_element_style::center | view_element_style::new_line));
				}
			}

			hover.elements.add(std::make_shared<text_element>(i->name()));
		}

		keyboard_accelerator = forward ? tt.keyboard_right : tt.keyboard_left;
	}
	else if (id == commands::menu_group_toolbar)
	{
		hover.elements.clear();
		hover.elements.add(make_icon_element(icon_index::group, view_element_style::no_break));
		hover.elements.add(std::make_shared<text_element>(tt.group_sort_tooltip, ui::style::font_size::dialog,
			ui::style::text_style::multiline,
			view_element_style::line_break));

		hover.elements.add(std::make_shared<summary_control>(_state.summary_shown(), view_element_style::line_break));
		hover.elements.add(std::make_shared<text_element>(tt.group_sort_click, ui::style::font_size::dialog,
			ui::style::text_style::multiline,
			view_element_style::new_line));
	}
	else if (id == commands::browse_open_containingfolder)
	{
		const auto i = _state.command_item();

		if (i)
		{
			hover.elements.add(std::make_shared<text_element>(i->containing().text()));
		}
	}
	else if (id == commands::browse_next_folder || id == commands::browse_previous_folder)
	{
		hover.elements.add(std::make_shared<text_element>(_state.next_path(id == commands::browse_next_folder)));
	}
	else if (id == commands::browse_back)
	{
		if (_state.history.can_browse_back())
		{
			hover.elements.add(std::make_shared<text_element>(_state.history.back_entry().search.text()));
		}
	}
	else if (id == commands::browse_forward)
	{
		if (_state.history.can_browse_forward())
		{
			hover.elements.add(std::make_shared<text_element>(_state.history.forward_entry().search.text()));
		}
	}
	else if (id == commands::browse_parent)
	{
		const auto parent = _state.parent_search().parent;

		if (!parent.is_empty())
		{
			hover.elements.add(std::make_shared<text_element>(parent.text(), ui::style::font_size::dialog,
				ui::style::text_style::multiline,
				view_element_style::new_line));
		}
	}
	else if (id == commands::favourite)
	{
		const auto search = _state.search();

		if (!search.is_empty())
		{
			const auto is_favorite = _state.search_is_favorite();
			const auto text = str::format(is_favorite ? tt.favorite_remove_fmt : tt.favorite_add_fmt, search.text());
			hover.elements.add(std::make_shared<text_element>(text, ui::style::font_size::dialog,
				ui::style::text_style::multiline,
				view_element_style::new_line));
		}
	}
	else if (id == commands::info_new_version)
	{
		hover.elements.add(std::make_shared<text_element>(tt.update_available, ui::style::font_size::dialog,
			ui::style::text_style::multiline,
			view_element_style::line_break));
		hover.elements.add(
			std::make_shared<text_element>(str::format(tt.update_avail_version_fmt, setting.available_version)));
		hover.elements.add(std::make_shared<text_element>(str::format(tt.update_current_version_fmt, s_app_version)));
	}
	else if (id == commands::option_show_rotated)
	{
		const auto i = _state.command_item();

		if (i != nullptr)
		{
			const auto md = i->metadata();

			if (md)
			{
				hover.elements.add(std::make_shared<text_element>(
					str::format(tt.item_oriented_tooltip_fmt, orientation_to_string(md->orientation))));
			}
		}

		hover.elements.add(std::make_shared<action_element>(tt.item_show_oriented));
	}

	if (keyboard_accelerator.empty())
	{
		if (command)
		{
			if (str::is_empty(keyboard_accelerator))
			{
				keyboard_accelerator = command->keyboard_accelerator_text;
			}
		}
	}

	if (!keyboard_accelerator.empty())
	{
		keyboard_accelerator = str::format(u8"{} {}"sv, tt.keyboard_accelerator_press, keyboard_accelerator);
		hover.elements.add(std::make_shared<action_element>(keyboard_accelerator));
	}
}


bool app_frame::pre_init()
{
	df::log(u8"main"sv, df::format_version(false));

	if (install_update_if_exists())
	{
		df::log(__FUNCTION__, u8"Exit because of install"sv);
		return false;
	}

	return true;
}

void app_frame::start_workers()
{
	auto app = shared_from_this();
	async_strategy& async = *this;

	_threads.start([&player = _player] { start_media_decode_video(player); });
	_threads.start([&player = _player] { start_media_decode_audio(player); });
	_threads.start([&player = _player] { start_media_reading(player); });
	_threads.start([] { start_media_preview(); });

	auto scan_uncached_func = [this]
	{
		if (!df::is_closing && _state.item_index.is_init_complete())
		{
			auto token = df::cancel_token(index_version);

			index_task_queue.enqueue([this, token]
				{
					_state.item_index.scan_uncached(token);

					invalidate_view(view_invalid::sidebar |
						view_invalid::command_state |
						view_invalid::item_scan |
						view_invalid::refresh_items |
						view_invalid::index_summary);
				});

			_threads.start([&q = crc_task_queue] { start_worker(q, u8"crc"sv); });
			_threads.start([&q = scan_folder_task_queue] { start_worker(q, u8"scan_folder"sv); });
			_threads.start([&q = scan_modified_items_task_queue] { start_worker(q, u8"scan_modified_items"sv); });
			_threads.start([&q = scan_displayed_items_task_queue] { start_worker(q, u8"scan_displayed_items"sv); });
			_threads.start([&q = predictions_task_queue] { start_worker(q, u8"predictions"sv); });
			_threads.start([&q = summary_task_queue] { start_worker(q, u8"summary"sv); });
			_threads.start([&q = presence_task_queue] { start_worker(q, u8"presence"sv); });
		}

		invalidate_view(view_invalid::sidebar |
			view_invalid::item_scan |
			view_invalid::presence |
			view_invalid::refresh_items |
			view_invalid::command_state |
			view_invalid::index_summary);
	};

	auto index_loaded_func = [this, scan_uncached_func]
	{
		if (!df::is_closing)
		{
			scan_uncached_func();
		}
	};

	_threads.start([&db = _db, &q = database_task_queue, &async, app, index_loaded_func]
		{
			start_database(db, q, async, app, index_loaded_func);
		});

	_threads.start([&q = load_task_queue] { start_worker(q, u8"load"sv); });
	_threads.start([&q = load_raw_task_queue] { start_worker(q, u8"load_raw"sv); });
	_threads.start([&q = render_task_queue] { start_worker(q, u8"render"sv); });
	_threads.start([&q = query_task_queue] { start_worker(q, u8"query"sv); });
	_threads.start([&q = auto_complete_task_queue] { start_worker(q, u8"auto_complete"sv); });
	_threads.start([&q = location_task_queue] { start_worker(q, u8"locations"sv); });
	_threads.start([&q = sidebar_task_queue] { start_worker(q, u8"sidebar"sv); });
	_threads.start([&q = web_task_queue] { start_worker(q, u8"web"sv); });

	_threads.start([&q = cloud_task_queue] { start_worker(q, u8"cloud"sv); });
	_threads.start([&q = index_task_queue] { start_worker(q, u8"index"sv); });

	index_task_queue.enqueue([this, scan_uncached_func]
		{
			_state.item_index.index_roots(index_folders());

			const auto token = df::cancel_token(index_version);
			_state.item_index.index_folders(token);
			queue_ui(scan_uncached_func);
			invalidate_view(view_invalid::sidebar | view_invalid::command_state | view_invalid::index_summary);
		});

	location_task_queue.enqueue([&lc = _locations]() { lc.load_index(); });
}

void app_frame::update_font_size()
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
		const auto lang_path = lang_folder.combine_file(setting.language + u8".po"s);
		tt.load_lang(lang_path);
	}

	update_font_size();
	if (str::is_empty(setting.favourite_tags)) setting.favourite_tags = tt.default_favourite_tags;

	initialise_commands();

	_app_frame = _pa->create_app_frame(store, shared_from_this());

	if (!_app_frame)
	{
		app_fail(tt.error_create_window_failed, {});
		return false;
	}

	_edit_view_controls->init(_app_frame);
	_view_frame->init(_app_frame);
	_sidebar->init(_app_frame);

	create_toolbars();

	ui::edit_styles edit_styles;
	edit_styles.rounded_corners = true;
	edit_styles.horizontal_scroll = true;
	edit_styles.bg_clr = ui::style::color::toolbar_background;
	edit_styles.select_all_on_focus = true;

	_search_edit = _app_frame->create_edit(edit_styles, {}, [this](const std::u8string& text)
		{
			search_edit_change(text);
		});
	_filter_edit = _app_frame->create_edit(edit_styles, {}, [this](const std::u8string& text)
		{
			filter_edit_change(text);
		});
	_search_completes = std::make_shared<search_auto_complete>(_state, _search_edit);
	_bubble = _app_frame->create_bubble();
	_state.view_mode(view_type::items);
	_threads.start([&q = work_task_queue] { start_worker(q, u8"work"sv); });

	open_default_folder();
	invalidate_view(view_invalid::address);

	work_task_queue.enqueue([this, app = shared_from_this()]()
		{
			start_workers();
			check_for_updates_and_location(app, _state);
			load_tools();
		});

	if (command_line.run_tests)
	{
		queue_ui([this] { invoke(commands::run_tests); });
	}

	if (!setting.sound_device.empty())
	{
		_state.change_audio_device(setting.sound_device);
	}

	_state.group_order(_starting_group_order, _starting_sort_order);
	focus_view();

	return true;
}

void app_frame::focus_search(const bool has_focus)
{
	if (_search_has_focus != has_focus)
	{
		_search_has_focus = has_focus;

		if (!_search_predictions_frame)
		{
			ui::frame_style style;
			style.child = false;
			style.no_focus = true;
			style.colors = {
				ui::style::color::toolbar_background, ui::style::color::view_text,
				ui::style::color::view_selected_background
			};

			_search_predictions_frame = std::make_shared<ui::list_frame>();
			_search_predictions_frame->init(_app_frame, _search_completes, style);
		}

		if (_search_has_focus)
		{
			_search_completes->initialise([&p = _search_predictions_frame](const ui::auto_complete_results& results)
				{
					p->show_results(results);
				});
			_search_predictions_frame->selected(nullptr, ui::complete_strategy_t::select_type::init);
			_search_predictions_frame->search({});
			_search_predictions_frame->_frame->window_bounds(calc_search_popup_bounds(), true);
		}
		else
		{
			hide_search_predictions();
		}
	}
}

void app_frame::on_window_destroy()
{
	log_func lf(__FUNCTION__);

	_bubble.reset();
	_search_predictions_frame.reset();
	_edit_view_controls.reset();
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
		case menu_type::view:
		case menu_type::items:
		default:
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
				message << i.first << u8" "sv << i.second << std::endl;
			}

			platform::web_request req;
			req.verb = platform::web_request_verb::POST;
			req.host = u8"diffractor.com"sv;
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

			send_request(req);
		}
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

		const view_element_event e{ view_element_event_type::free_graphics_resources, nullptr };
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

	for (const auto& i : _state.search_items().folders())
	{
		if (!offscreen_only || !i->bounds.intersects(expanded_logical_bounds))
		{
			i->clear_cached_surface();
		}
	}
}
