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
#include "app_command_status.h"
#include "app_util.h"
#include "view_rename.h"

#include "ui_controls.h"

void rename_view::run()
{
	const auto title = tt.command_rename;
	const auto icon = icon_index::rename;
	auto dlg = make_dlg(_host->owner());

	const auto& items = _state.selected_items();

	detach_file_handles detach(_state);
	const auto results = std::make_shared<command_status>(_state._async, dlg, icon, title, items.size());

	_state.queue_async(async_queue::work, [&s = _state, items, results]()
		{
			const auto name_template = setting.rename.name_template;
			const auto start_seq = str::to_int(setting.rename.start_seq);
			const auto renames = calc_item_renames(items, name_template, start_seq);

			result_scope rr(results);
			platform::file_op_result result;

			for (const auto& i : renames)
			{
				if (results->is_canceled())
					break;

				results->start_item(i.item->name());
				result = i.item->rename(s.item_index, i.new_name);
				results->end_item(i.item->name(), to_status(result.code));

				if (result.failed())
					break;
			}

			rr.complete(result.failed() ? result.format_error() : std::u8string{});
		});

	results->wait_for_complete();
}

void rename_view::refresh()
{
	const auto& items = _state.selected_items();

	std::vector<row_element_ptr> rows;
	rows.reserve(rows.size());

	const auto error_text_color = ui::lighten(ui::style::color::warning_background, 0.55f);
	const auto rename_text_color = ui::lighten(ui::style::color::dialog_selected_background, 0.55f);

	const auto name_template = setting.rename.name_template;
	const auto start_seq = str::to_int(setting.rename.start_seq);
	_renames = calc_item_renames(items, name_template, start_seq);

	int count = 0;

	for (const auto& rename : _renames)
	{
		auto row = std::make_shared<row_element>(*this);
		row->_text[1] = rename.original_name;
		row->_text[2] = rename.new_name;
		row->_text_color[2] = platform::is_valid_file_name(rename.new_name) ? rename_text_color : error_text_color;
		row->_order = count++;
		rows.emplace_back(row);
	}

	_rows = std::move(rows);
	_status = format_plural_text(tt.rename_fmt, items);
	_state.invalidate_view(view_invalid::view_layout | view_invalid::controller | view_invalid::status);
}

view_controls_host_ptr rename_view::controls(const ui::control_frame_ptr& owner)
{
	auto result = std::make_shared<view_controls_host>(_state);

	const auto& items = _state.selected_items();
	auto frame = owner->create_dlg(result, false);

	std::vector<std::u8string> folder_structure_completes
	{
		setting.rename.name_template,
		u8"file-###"s,
		u8"{created}-###"s,
		u8"{year}-{month}-###"s,
		u8"travel-{country}-###"s,
		u8"{artist}-{album}-{title}"s,
		u8"{show}.S{season}E{episode}.{title}"s,
	};

	std::sort(folder_structure_completes.begin(), folder_structure_completes.end());       // Sort
	auto last = std::unique(folder_structure_completes.begin(), folder_structure_completes.end());   // Move duplicates
	folder_structure_completes.erase(last, folder_structure_completes.end());

	std::vector<view_element_ptr> controls = {
		std::make_shared<text_element>(tt.command_rename, ui::style::font_face::title),
		std::make_shared<text_element>(tt.rename_info),
		std::make_shared<text_element>(format_plural_text(tt.rename_fmt, items)),
		std::make_shared<divider_element>(),
		std::make_shared<text_element>(tt.rename_help_template_1),
		std::make_shared<bullet_element>(icon_index::bullet, tt.rename_help_template_2),
		std::make_shared<bullet_element>(icon_index::bullet, tt.rename_help_template_3),
		std::make_shared<text_element>(tt.for_example),
		std::make_shared<bullet_element>(icon_index::bullet, tt.rename_help_template_example_2),
		std::make_shared<bullet_element>(icon_index::bullet, tt.rename_help_template_example_3),
		std::make_shared<bullet_element>(icon_index::bullet, tt.rename_help_template_example_4),
		std::make_shared<link_element>(tt.more_template_information, []() { platform::open(doc_template_url); }),
		std::make_shared<divider_element>(),
		std::make_shared<text_element>(tt.rename_template_label),
		std::make_shared<ui::edit_picker_control>(frame, setting.rename.name_template, folder_structure_completes, [this](std::u8string_view) { refresh(); }),
		std::make_shared<ui::edit_control>(frame, tt.rename_template_start_label, setting.rename.start_seq, [this](std::u8string_view) { refresh(); }),
	};

	for (auto& c : controls)
	{
		c->margin.cx = 8;
		c->margin.cy = 4;
	}

	result->_controls = controls;
	result->_frame = result->_dlg = frame;

	return result;
}
