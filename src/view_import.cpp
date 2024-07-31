// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2024  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY


#include "pch.h"
#include "model.h"
#include "model_db.h"
#include "model_index.h"
#include "view_import.h"

#include "app_command_status.h"
#include "app_util.h"
#include "ui_controls.h"
#include "ui_dialog.h"


static import_options make_import_options()
{
	import_options result;
	result.source_filter = setting.import.source_filter;
	result.dest_folder = df::folder_path(setting.import.destination_path);
	result.dest_structure = setting.import.dest_folder_structure.empty()
		? defaut_custom_folder_structure
		: setting.import.dest_folder_structure;
	result.is_move = setting.import.is_move;
	result.overwrite_if_newer = setting.import.overwrite_if_newer;
	result.set_created_date = setting.import.set_created_date;
	result.rename_different_attributes = setting.import.rename_different_attributes;
	return result;
}


//static void import_invoke(view_state& s, const ui::control_frame_ptr& parent, const view_host_base_ptr& view)
//{
//	const auto title = tt.command_import;
//	const auto icon = icon_index::import;
//
//	auto dlg = make_dlg(parent);
//	dlg->show_status(icon_index::star, tt.import_detecting);
//
//	pause_media pause(s);
//	platform::thread_event event_init(true, false);
//
//	std::vector<import_source> sources;
//
//	s.queue_async(async_queue::work, [&s, &sources, &event_init]()
//		{
//			auto sources_temp = calc_import_sources(s);
//
//			s.queue_ui([&sources, &event_init, sources_temp]()
//				{
//					sources = sources_temp;
//					event_init.set();
//				});
//		});
//
//	platform::wait_for({ event_init }, 100000, false);
//
//	//auto import_settings = std::make_shared<ui::group_control>();
//	auto is_first_source = true;
//	auto import_settings = std::make_shared<ui::group_control>();
//
//	import_settings->add(std::make_shared<text_element>(tt.import_from));
//
//	for (auto&& src : sources)
//	{
//		src.selected = is_first_source;
//		auto check = std::make_shared<ui::check_control>(dlg->_frame, src.text, src.selected, true);
//		import_settings->add(check);
//		is_first_source = false;
//	}
//
//	const std::vector<std::u8string> source_filter_completes
//	{
//		u8"*.*"s,
//		u8"@photos"s,
//		u8"@video"s,
//		u8"@audio"s,
//		u8"*.jpg;*.jpeg"s,
//		u8"*.mp4;*.mpv;*.mov"s,
//	};
//
//	const std::vector<std::u8string> folder_structure_completes
//	{
//		std::u8string(defaut_custom_folder_structure),
//		u8"{artist}\\{album}"s,
//		u8"{show}\\Season {season}"s,
//		u8"{year}\\{created}"s,
//		u8"{year}\\{country}"s
//	};
//
//	bool select_other_folder = is_first_source;
//	auto limit = std::make_shared<ui::check_control>(dlg->_frame, tt.import_other_folder, select_other_folder, true);
//	limit->child(std::make_shared<ui::folder_picker_control>(dlg->_frame, setting.import.source_path));
//	import_settings->add(limit);
//	import_settings->add(std::make_shared<divider_element>());
//	// TODO import_settings->add(std::make_shared<text_element>(tt.import_src_filter));
//	// TODO import_settings->add(std::make_shared<ui::edit_picker_control>(dlg->_frame, setting.import.source_filter, source_filter_completes));
//	// TODO import_settings->add(std::make_shared<divider_element>());
//	import_settings->add(std::make_shared<text_element>(tt.import_dest_folder));
//	import_settings->add(std::make_shared<ui::folder_picker_control>(dlg->_frame, setting.import.destination_path));
//	import_settings->add(std::make_shared<text_element>(tt.import_dest_folder_structure));
//	import_settings->add(
//		std::make_shared<ui::edit_picker_control>(dlg->_frame, setting.import.dest_folder_structure,
//			folder_structure_completes));
//	import_settings->add(std::make_shared<divider_element>());
//	import_settings->add(
//		std::make_shared<ui::check_control>(dlg->_frame, tt.import_ignore_previous, setting.import.ignore_previous));
//	import_settings->add(
//		std::make_shared<ui::check_control>(dlg->_frame, tt.import_overwrite_if_newer,
//			setting.import.overwrite_if_newer));
//	import_settings->add(std::make_shared<ui::check_control>(dlg->_frame, tt.move_items, setting.import.is_move));
//	// TODO import_settings->add(std::make_shared<ui::check_control>(dlg->_frame, tt.import_rename_different_attributes, setting.import.rename_different_attributes));
//	import_settings->add(
//		std::make_shared<ui::check_control>(dlg->_frame, tt.import_set_created_date, setting.import.set_created_date));
//
//	const auto calc_import_root = [&]
//		{
//			df::index_roots result;
//
//			if (select_other_folder)
//			{
//				result.folders.emplace(df::folder_path(setting.import.source_path));
//			}
//			else
//			{
//				for (const auto& src : sources)
//				{
//					if (src.selected)
//					{
//						if (!src.path.is_empty())
//						{
//							result.folders.emplace(src.path);
//						}
//
//						for (const auto path : src.items.file_paths())
//						{
//							result.files.emplace(path);
//						}
//
//						for (const auto path : src.items.folder_paths())
//						{
//							result.folders.emplace(path);
//						}
//					}
//				}
//			}
//
//			return result;
//		};
//
//
//	auto analysis_table = std::make_shared<ui::table_element>(view_element_style::grow | view_element_style::shrink);
//	analysis_table->can_scroll = true;
//	analysis_table->frame_when_empty = true;
//
//	const auto run_analyze = [dlg, &s, calc_import_root, analysis_table, icon]()
//		{
//			platform::thread_event event_analyze(true, false);
//			const auto import_root = calc_import_root();
//			auto token = df::cancel_token(ui::cancel_gen);
//
//			auto status_dlg = make_dlg(dlg->_frame);
//			status_dlg->show_cancel_status(icon, tt.analyzing, [] { ++ui::cancel_gen; });
//
//			const auto options = make_import_options();
//
//			s._async.queue_database([&s, &event_analyze, import_root, analysis_table, options, token](database& db)
//				{
//					const auto previous_imported = setting.import.ignore_previous ? db.load_item_imports() : item_import_set{};
//
//					s.queue_async(async_queue::work,
//						[&s, &event_analyze, import_root, previous_imported, analysis_table, options, token]()
//						{
//							const auto items = s.item_index.scan_items(import_root, true, true, token);
//							const auto analysis_result = import_analysis(items, options, previous_imported, token);
//
//							s.queue_ui([&s, &event_analyze, analysis_result, analysis_table, token]()
//								{
//									analysis_table->clear();
//
//									if (!token.is_cancelled())
//									{
//										const auto clr1 = ui::average(ui::style::color::dialog_text,
//											ui::style::color::view_selected_background);
//										const auto clr2 = ui::average(ui::style::color::dialog_text,
//											ui::style::color::important_background);
//										const auto clr3 = ui::average(ui::style::color::dialog_text,
//											ui::style::color::duplicate_background);
//
//										for (const auto& i : analysis_result)
//										{
//											auto element1 = std::make_shared<text_element>(
//												str::to_string(count_imports(i.second)),
//												ui::style::text_style::single_line_far);
//											auto element2 = std::make_shared<text_element>(
//												str::to_string(count_overwrites(i.second)),
//												ui::style::text_style::single_line_far);
//											auto element3 = std::make_shared<text_element>(
//												str::to_string(count_ignores(i.second)),
//												ui::style::text_style::single_line_far);
//
//											element1->foreground_color(clr1);
//											element2->foreground_color(clr2);
//											element3->foreground_color(clr3);
//
//											analysis_table->add(u8"...\\"s + i.second.front().sub_folder, element1,
//												element2, element3);
//										}
//
//										const auto element0 = std::make_shared<text_element>(tt.folder_title);
//										const auto element1 = std::make_shared<text_element>(
//											setting.import.is_move ? tt.menu_move : tt.menu_copy);
//										const auto element2 = std::make_shared<text_element>(tt.import_overwrite);
//										const auto element3 = std::make_shared<text_element>(tt.import_ignore);
//
//										element1->foreground_color(clr1);
//										element2->foreground_color(clr2);
//										element3->foreground_color(clr3);
//
//										element0->padding(8);
//										element1->padding(8);
//										element2->padding(8);
//										element3->padding(8);
//
//										analysis_table->add(element0, element1, element2, element3);
//
//										analysis_table->reverse();
//									}
//
//									event_analyze.set();
//								});
//						});
//				});
//
//			platform::wait_for({ event_analyze }, 100000, false);
//			status_dlg->_frame->destroy();
//			dlg->layout();
//		};
//
//	auto cols = std::make_shared<ui::col_control>();
//	cols->add(set_margin(import_settings));
//	cols->add(set_margin(analysis_table));
//
//	std::vector<view_element_ptr> controls = {
//		set_margin(set_margin(std::make_shared<ui::title_control2>(dlg->_frame, icon, title, tt.import_info))),
//		std::make_shared<divider_element>(),
//		cols,
//		std::make_shared<divider_element>(),
//		std::make_shared<ui::ok_cancel_control_analyze>(dlg->_frame, tt.button_import, run_analyze)
//	};
//
//
//	if (dlg->show_modal(controls, { 111 }) == ui::close_result::ok)
//	{
//		detach_file_handles detach(s);
//
//		record_feature_use(features::import);
//
//		const auto import_root = calc_import_root();
//		s.recent_folders.add(setting.import.destination_path);
//
//		const auto options = make_import_options();
//
//		dlg->show_status(icon, tt.processing);
//
//		auto token = df::cancel_token(ui::cancel_gen);
//		const auto results = std::make_shared<command_status>(s._async, dlg, icon, title, 0);
//
//		s._async.queue_database([&s, results, view, import_root, options, token](database& db)
//			{
//				const auto previous_imported = setting.import.ignore_previous ? db.load_item_imports() : item_import_set{};
//
//				s.queue_async(async_queue::work, [&s, results, view, previous_imported, import_root, options, token]()
//					{
//						result_scope rr(results);
//						const auto items = s.item_index.scan_items(import_root, true, true, token);
//						const auto analysis_result = import_analysis(items, options, previous_imported, token);
//						results->total(count_imports(analysis_result));
//
//						const auto copy_result = import_copy(s.item_index, results, analysis_result, options, token);
//
//						s._async.queue_database([&s, copy_result](database& db)
//							{
//								db.writes_item_imports(copy_result.imports);
//							});
//
//						if (!copy_result.folder.is_empty())
//						{
//							s.queue_ui([&s, view, folder = copy_result.folder]()
//								{
//									s.open(view, df::search_t().add_selector(folder), {});
//									s.invalidate_view(view_invalid::index);
//								});
//						}
//
//						rr.complete();
//					});
//			});
//
//		results->wait_for_complete();
//	}
//
//	dlg->_frame->destroy();
//}

view_controls_host_ptr import_view::controls(const ui::control_frame_ptr& owner)
{
	auto result = std::make_shared<view_controls_host>(_state);
	auto frame = owner->create_dlg(result, false);

	const auto& items = _state.selected_items();

	std::vector<view_element_ptr> controls;
	controls.emplace_back(std::make_shared<text_element>(tt.command_import, ui::style::font_face::title));
	controls.emplace_back(std::make_shared<text_element>(tt.import_info));
	controls.emplace_back(std::make_shared<divider_element>());

	platform::thread_event event_init(true, false);



	_state.queue_async(async_queue::work, [&s = _state, &sources = _sources, &event_init]()
		{
			auto sources_temp = calc_import_sources(s);

			s.queue_ui([&sources, &event_init, sources_temp]()
				{
					sources = sources_temp;
					event_init.set();
				});
		});

	platform::wait_for({ event_init }, 100000, false);

	//auto import_settings = std::make_shared<ui::group_control>();
	auto is_first_source = true;

	controls.emplace_back(std::make_shared<text_element>(tt.import_from));

	for (auto&& src : _sources)
	{
		src.selected = is_first_source;
		auto check = std::make_shared<ui::check_control>(frame, src.text, src.selected, true);
		controls.emplace_back(check);
		is_first_source = false;
	}

	const std::vector<std::u8string> source_filter_completes
	{
		u8"*.*"s,
		u8"@photos"s,
		u8"@video"s,
		u8"@audio"s,
		u8"*.jpg;*.jpeg"s,
		u8"*.mp4;*.mpv;*.mov"s,
	};

	const std::vector<std::u8string> folder_structure_completes
	{
		std::u8string(defaut_custom_folder_structure),
		u8"{artist}\\{album}"s,
		u8"{show}\\Season {season}"s,
		u8"{year}\\{created}"s,
		u8"{year}\\{country}"s
	};

	_select_other_folder = is_first_source;
	auto limit = std::make_shared<ui::check_control>(frame, tt.import_other_folder, _select_other_folder, true);
	limit->child(std::make_shared<ui::folder_picker_control>(frame, setting.import.source_path));

	controls.emplace_back(limit);
	controls.emplace_back(std::make_shared<divider_element>());
	// TODO controls.emplace_back(std::make_shared<text_element>(tt.import_src_filter));
	// TODO controls.emplace_back(std::make_shared<ui::edit_picker_control>(frame, setting.import.source_filter, source_filter_completes));
	// TODO controls.emplace_back(std::make_shared<divider_element>());
	controls.emplace_back(std::make_shared<text_element>(tt.import_dest_folder));
	controls.emplace_back(std::make_shared<ui::folder_picker_control>(frame, setting.import.destination_path));
	controls.emplace_back(std::make_shared<text_element>(tt.import_dest_folder_structure));
	controls.emplace_back(std::make_shared<ui::edit_picker_control>(frame, setting.import.dest_folder_structure, folder_structure_completes));
	controls.emplace_back(set_margin(std::make_shared<link_element>(tt.more_template_information, []() { platform::open(doc_template_url); })));
	controls.emplace_back(std::make_shared<divider_element>());
	controls.emplace_back(
		std::make_shared<ui::check_control>(frame, tt.import_ignore_previous, setting.import.ignore_previous));
	controls.emplace_back(
		std::make_shared<ui::check_control>(frame, tt.import_overwrite_if_newer,
			setting.import.overwrite_if_newer));
	controls.emplace_back(std::make_shared<ui::check_control>(frame, tt.move_items, setting.import.is_move));
	// TODO controls.emplace_back(std::make_shared<ui::check_control>(frame, tt.import_rename_different_attributes, setting.import.rename_different_attributes));
	controls.emplace_back(
		std::make_shared<ui::check_control>(frame, tt.import_set_created_date, setting.import.set_created_date));

	for (auto& c : controls)
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
	std::vector<std::u8string> text;

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
	_status = str::format(u8"{} {}   {} {}   {} {}"sv, imports, tt.import, exists, tt.exists, already_imported, tt.previously_imported);
	_state.invalidate_view(view_invalid::view_layout | view_invalid::controller | view_invalid::status);
}

static auto calc_import_root(const std::vector<import_source>& sources, bool select_other_folder)
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
	const auto icon = icon_index::import;
	auto dlg = make_dlg(_host->owner());

	record_feature_use(features::import);

	const auto import_root = calc_import_root(_sources, _select_other_folder);
	_state.recent_folders.add(setting.import.destination_path);

	const auto options = make_import_options();

	dlg->show_status(icon, tt.processing);

	auto token = df::cancel_token(ui::cancel_gen);
	const auto results = std::make_shared<command_status>(_state._async, dlg, icon, title, 0);

	_state._async.queue_database([&s = _state, results, view = shared_from_this(), import_root, options, token](database& db)
		{
			const auto previous_imported = setting.import.ignore_previous ? db.load_item_imports() : item_import_set{};

			s.queue_async(async_queue::work, [&s, results, view, previous_imported, import_root, options, token]()
				{
					result_scope rr(results);
					const auto items = s.item_index.scan_items(import_root, true, true, token);
					const auto analysis_result = import_analysis(items, options, previous_imported, token);

					if (!token.is_cancelled())
					{
						s.queue_ui([analysis_result, view]()
							{
								view->update_rows(analysis_result);
							});
					}

					results->total(count_imports(analysis_result));

					const auto copy_result = import_copy(s.item_index, results, analysis_result, options, token);

					s._async.queue_database([&s, copy_result](database& db)
						{
							db.writes_item_imports(copy_result.imports);
						});

					if (!copy_result.folder.is_empty())
					{
						s.queue_ui([&s, view, folder = copy_result.folder]()
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
	const auto icon = icon_index::import;
	platform::thread_event event_analyze(true, false);
	const auto import_root = calc_import_root(_sources, _select_other_folder);
	auto token = df::cancel_token(ui::cancel_gen);

	auto status_dlg = make_dlg(_host->owner());
	status_dlg->show_cancel_status(icon, tt.analyzing, [] { ++ui::cancel_gen; });

	const auto options = make_import_options();

	_state._async.queue_database([&s = _state, &event_analyze, import_root, view = shared_from_this(), options, token](database& db)
		{
			const auto previous_imported = setting.import.ignore_previous ? db.load_item_imports() : item_import_set{};

			s.queue_async(async_queue::work,
				[&s, &event_analyze, import_root, previous_imported, view, options, token]()
				{
					const auto items = s.item_index.scan_items(import_root, true, true, token);
					const auto analysis_result = import_analysis(items, options, previous_imported, token);

					if (!token.is_cancelled())
					{
						s.queue_ui([analysis_result, view]()
							{
								view->update_rows(analysis_result);
							});

						event_analyze.set();
					}



					/*analysis_table->clear();

					if (!token.is_cancelled())
					{
						const auto clr1 = ui::average(ui::style::color::dialog_text,
							ui::style::color::view_selected_background);
						const auto clr2 = ui::average(ui::style::color::dialog_text,
							ui::style::color::important_background);
						const auto clr3 = ui::average(ui::style::color::dialog_text,
							ui::style::color::duplicate_background);

						for (const auto& i : analysis_result)
						{
							auto element1 = std::make_shared<text_element>(
								str::to_string(count_imports(i.second)),
								ui::style::text_style::single_line_far);
							auto element2 = std::make_shared<text_element>(
								str::to_string(count_overwrites(i.second)),
								ui::style::text_style::single_line_far);
							auto element3 = std::make_shared<text_element>(
								str::to_string(count_ignores(i.second)),
								ui::style::text_style::single_line_far);

							element1->foreground_color(clr1);
							element2->foreground_color(clr2);
							element3->foreground_color(clr3);

							analysis_table->add(u8"...\\"s + i.second.front().sub_folder, element1,
								element2, element3);
						}

						const auto element0 = std::make_shared<text_element>(tt.folder_title);
						const auto element1 = std::make_shared<text_element>(
							setting.import.is_move ? tt.menu_move : tt.menu_copy);
						const auto element2 = std::make_shared<text_element>(tt.import_overwrite);
						const auto element3 = std::make_shared<text_element>(tt.import_ignore);

						element1->foreground_color(clr1);
						element2->foreground_color(clr2);
						element3->foreground_color(clr3);

						element0->padding(8);
						element1->padding(8);
						element2->padding(8);
						element3->padding(8);

						analysis_table->add(element0, element1, element2, element3);

						analysis_table->reverse();
					}*/

					event_analyze.set();
				});
		});

	platform::wait_for({ event_analyze }, 100000, false);
	status_dlg->_frame->destroy();
	//dlg->layout();
}

void import_view::refresh()
{
	_state.invalidate_view(view_invalid::view_layout | view_invalid::controller);
}

void import_view::reload()
{
	analyze();
}
