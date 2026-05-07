// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: File import workflow. Scans source folders, displays import preview,
// handles file copying/moving with rename templates and duplicate detection.

#include "pch.h"
#include "model.h"
#include "model_db.h"
#include "model_index.h"
#include "view_import.h"

#include "app_command_status.h"
#include "app_util.h"
#include "ui_dialog.h"


static import_options make_import_options()
{
	import_options result;
	result.source_filter = setting.import.source_filter;
	result.dest_folder = df::folder_path(setting.import.destination_path);
	result.dest_structure = setting.import.dest_folder_structure.empty()
		                        ? default_custom_folder_structure
		                        : setting.import.dest_folder_structure;
	result.is_move = setting.import.is_move;
	result.overwrite_if_newer = setting.import.overwrite_if_newer;
	result.set_created_date = setting.import.set_created_date;
	result.rename_different_attributes = setting.import.rename_different_attributes;
	return result;
}


view_controls_host_ptr import_view::controls(const ui::control_frame_ptr& owner)
{
	auto result = std::make_shared<view_controls_host>(_state);
	auto frame = owner->create_dlg(result, false);

	std::vector<view_element_ptr> controls;
	controls.emplace_back(std::make_shared<text_element>(tt.command_import, ui::style::font_face::title));
	controls.emplace_back(std::make_shared<text_element>(tt.import_info));
	controls.emplace_back(std::make_shared<divider_element>());

	platform::thread_event event_init(true, false);


	_state.queue_async(async_queue::work, [&s = _state, &sources = _sources, &event_init]
	{
		auto sources_temp = calc_import_sources(s);

		s.queue_ui([&sources, &event_init, sources_temp]
		{
			sources = sources_temp;
			event_init.set();
		});
	});

	platform::wait_for({event_init}, 100000, false);

	auto is_first_source = true;

	controls.emplace_back(std::make_shared<text_element>(tt.import_from));

	for (auto&& src : _sources)
	{
		src.selected = is_first_source;
		auto check = std::make_shared<ui::check_control>(frame, src.text, src.selected, true);
		controls.emplace_back(check);
		is_first_source = false;
	}

	const std::vector<std::string> folder_structure_completes
	{
		std::string(default_custom_folder_structure),
		"{artist}\\{album}"s,
		"{show}\\Season {season}"s,
		"{year}\\{created}"s,
		"{year}\\{country}"s
	};

	_select_other_folder = is_first_source;
	auto limit = std::make_shared<ui::check_control>(frame, tt.import_other_folder, _select_other_folder, true);
	limit->child(std::make_shared<ui::folder_picker_control>(frame, setting.import.source_path));

	controls.emplace_back(limit);
	controls.emplace_back(std::make_shared<divider_element>());
	controls.emplace_back(std::make_shared<text_element>(tt.import_dest_folder));
	controls.emplace_back(std::make_shared<ui::folder_picker_control>(frame, setting.import.destination_path));
	controls.emplace_back(std::make_shared<text_element>(tt.import_dest_folder_structure));
	controls.emplace_back(
		std::make_shared<ui::edit_picker_control>(frame, setting.import.dest_folder_structure,
		                                          folder_structure_completes));
	controls.emplace_back(set_margin(
		std::make_shared<link_element>(tt.more_template_information, [] { platform::open(doc_template_url); })));
	controls.emplace_back(std::make_shared<divider_element>());
	controls.emplace_back(
		std::make_shared<ui::check_control>(frame, tt.import_ignore_previous, setting.import.ignore_previous));
	controls.emplace_back(
		std::make_shared<ui::check_control>(frame, tt.import_overwrite_if_newer,
		                                    setting.import.overwrite_if_newer));
	controls.emplace_back(std::make_shared<ui::check_control>(frame, tt.move_items, setting.import.is_move));
	controls.emplace_back(
		std::make_shared<ui::check_control>(frame, tt.import_set_created_date, setting.import.set_created_date));

	for (const auto& c : controls)
	{
		c->margin.cx = 8;
		c->margin.cy = 4;
	}

	result->_controls = controls;
	result->_frame = result->_dlg = frame;

	return result;
}

void import_view::update_rows(const import_analysis_result& analysis_result)
{
	const auto gray_text_color = ui::darken(ui::style::color::view_text, 0.22f);
	const auto blue_text_color = ui::lighten(ui::style::color::dialog_selected_background, 0.55f);
	const auto orange_text_color = ui::lighten(ui::style::color::important_background, 0.55f);

	std::vector<row_element_ptr> rows;
	std::vector<std::string> text;

	int imports = 0;
	int exists = 0;
	int already_imported = 0;

	for (const auto& a : analysis_result)
	{
		for (const auto& i : a.second)
		{
			auto row = std::make_shared<row_element>(*this);
			row->_text[1] = i.source.pack();
			row->_text[2] = i.destination.pack();

			switch (i.action)
			{
			case import_action::import:
				row->_text[0] = tt.import;
				row->_text_color[0] = blue_text_color;
				row->_order = 1;
				imports += 1;
				break;
			case import_action::already_exists:
				row->_text_color[1] = gray_text_color;
				row->_text_color[2] = gray_text_color;
				row->_order = 2;
				row->_text[0] = tt.exists;
				exists += 1;
				break;
			case import_action::already_imported:
				row->_text_color[1] = gray_text_color;
				row->_text_color[2] = gray_text_color;
				row->_text_color[0] = orange_text_color;
				row->_text[0] = tt.previously_imported;
				row->_order = 3;
				already_imported += 1;
				break;
			}

			rows.emplace_back(row);
		}
	}

	_rows = std::move(rows);
	_status = std::format("{} {}   {} {}   {} {}", imports, tt.import, exists, tt.exists, already_imported,
	                      tt.previously_imported);
	_state.invalidate_view(view_invalid::view_layout | view_invalid::controller | view_invalid::status);
}

static auto calc_import_root(const std::vector<import_source>& sources, const bool select_other_folder)
{
	df::index_roots result;

	if (select_other_folder)
	{
		result.folders.emplace(df::folder_path(setting.import.source_path));
	}
	else
	{
		for (const auto& src : sources)
		{
			if (src.selected)
			{
				if (!src.path.is_empty())
				{
					result.folders.emplace(src.path);
				}

				for (const auto path : src.items.file_paths())
				{
					result.files.emplace(path);
				}

				for (const auto path : src.items.folder_paths())
				{
					result.folders.emplace(path);
				}
			}
		}
	}

	return result;
};

void import_view::run()
{
	detach_file_handles detach(_state);

	const auto title = tt.command_import;
	constexpr auto icon = icon_index::import;
	auto dlg = make_dlg(_host->owner());

	record_feature_use(features::import);

	const auto import_root = calc_import_root(_sources, _select_other_folder);
	_state.recent_folders.add(setting.import.destination_path);

	const auto options = make_import_options();

	dlg->show_status(icon, tt.processing);

	auto token = df::cancel_token(ui::cancel_gen);
	const auto results = std::make_shared<command_status>(_state._async, dlg, icon, title, 0);

	_state._async.queue_database(
		[&s = _state, results, view = shared_from_this(), import_root, options, token](const database& db)
		{
			const auto previous_imported = setting.import.ignore_previous ? db.load_item_imports() : item_import_set{};

			s.queue_async(async_queue::work, [&s, results, view, previous_imported, import_root, options, token]
			{
				result_scope rr(results);
				const auto items = s.item_index.scan_items(import_root, true, true, token);
				const auto analysis_result = import_analysis(items, options, previous_imported, token);

				if (!token.is_cancelled())
				{
					s.queue_ui([analysis_result, view]
					{
						view->update_rows(analysis_result);
					});
				}

				results->total(count_imports(analysis_result));

				const auto copy_result = import_copy(s.item_index, results, analysis_result, options, token);

				s._async.queue_database([&s, copy_result](const database& db)
				{
					db.writes_item_imports(copy_result.imports);
				});

				if (!copy_result.folder.is_empty())
				{
					s.queue_ui([&s, view, folder = copy_result.folder]
					{
						s.open(view->_host, df::search_t().add_selector(folder), {});
						s.invalidate_view(view_invalid::index);
					});
				}

				rr.complete();
			});
		});

	results->wait_for_complete();

	dlg->_frame->destroy();
}

void import_view::analyze()
{
	const auto title = tt.command_import;
	constexpr auto icon = icon_index::import;
	platform::thread_event event_analyze(true, false);
	const auto import_root = calc_import_root(_sources, _select_other_folder);
	auto token = df::cancel_token(ui::cancel_gen);

	const auto status_dlg = make_dlg(_host->owner());
	status_dlg->show_cancel_status(icon, tt.analyzing, [] { ++ui::cancel_gen; });

	const auto options = make_import_options();

	_state._async.queue_database(
		[&s = _state, &event_analyze, import_root, view = shared_from_this(), options, token](const database& db)
		{
			const auto previous_imported = setting.import.ignore_previous ? db.load_item_imports() : item_import_set{};

			s.queue_async(async_queue::work,
			              [&s, &event_analyze, import_root, previous_imported, view, options, token]
			              {
				              const auto items = s.item_index.scan_items(import_root, true, true, token);
				              const auto analysis_result = import_analysis(items, options, previous_imported, token);

				              if (!token.is_cancelled())
				              {
					              s.queue_ui([analysis_result, view]
					              {
						              view->update_rows(analysis_result);
					              });
				              }

				              // Always signal completion so the modal status dialog
				              // is dismissed even when analysis is cancelled.
				              event_analyze.set();
			              });
		});

	platform::wait_for({event_analyze}, 100000, false);
	status_dlg->_frame->destroy();
}

void import_view::refresh()
{
	_state.invalidate_view(view_invalid::view_layout | view_invalid::controller);
}

void import_view::reload()
{
	analyze();
}
