// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2024  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#include "pch.h"
#include "view_test.h"
#include "app_text.h"


void test_item::update_row()
{
	df::assert_true(ui::is_ui_thread());

	_row->_text[1] = _name;

	switch (_state)
	{
	case test_state::pass:
		_row->_text_color[0] = ui::lighten(ui::style::color::info_background, 0.55f);
		_row->_text[0] = u8"Success"sv;
		_row->_order = 100;
		break;
	case test_state::fail:
		_row->_text_color[0] = ui::lighten(ui::style::color::warning_background, 0.55f);
		_row->_text[0] = u8"Failed"sv;
		_row->_order = 1;
		break;
	case test_state::running:
		_row->_text_color[0] = ui::lighten(ui::style::color::important_background, 0.55f);
		_row->_text[0] = u8"Running"sv;
		_row->_order = 2;
		break;
	case test_state::unknown:
		_row->_text_color[0] = ui::darken(ui::style::color::view_text, 0.22f);
		_row->_text_color[1] = ui::darken(ui::style::color::view_text, 0.22f);
		_row->_text[0] = u8"Unknown"sv;
		_row->_order = 200;
		break;
	}

	if (_message.empty() && _time > 0)
	{
		_time_text = str::format(u8"{:8} milliseconds"sv, _time);
		_row->_text[2] = _time_text;
	}
	else
	{
		_row->_text[2] = _message;
	}
}

void test_item::perform(view_state& s, shared_test_context& stc)
{
	const auto t = df::now();

	try
	{
		s.queue_ui([this] { _state = test_state::running; });
		_f(stc);
		s.queue_ui([this, t]
		{
			_message.clear();
			_state = test_state::pass;
			_time = df::round((df::now() - t) * 1000);
		});
	}
	catch (const test_assert_exception& e)
	{
		s.queue_ui([this, e, t]
		{
			_state = test_state::fail;
			_message = e.message;
			_time = df::round((df::now() - t) * 1000);
		});
	}
}

test_view::test_view(view_state& state, view_host_ptr host) : list_view(state, std::move(host))
{
	col_count = 3;
}

test_view::~test_view()
{
	_tests.clear();
}

void test_view::activate(const sizei extent)
{
	list_view::activate(extent);

	if (_tests.empty())
	{
		register_tests(_state, *this);
	}

	refresh();
}


void test_view::refresh()
{
	_state.invalidate_view(view_invalid::view_redraw | view_invalid::view_layout);
}

void test_view::reload()
{
	run_tests(_state, _tests);
}

test_ptr test_view::test_from_location(const int y) const
{
	test_ptr result;
	const auto offset = _scroller.scroll_offset();
	int distance = 10000;

	for (const auto& i : _tests)
	{
		const auto yy = (i->bounds.top + i->bounds.bottom) / 2;
		const auto d = abs(yy - y - offset.y);

		if (d < distance)
		{
			distance = d;
			result = i;
		}
	}

	return result;
}

std::u8string_view test_view::status()
{
	int count_fail = 0;
	int count_ok = 0;
	int total = 0;

	for (const auto& t : _tests)
	{
		if (t->_state == test_state::fail) count_fail += 1;
		if (t->_state == test_state::pass) count_ok += 1;
		total += 1;
	}

	if (is_running_tests())
	{
		_status = u8"Running tests..."sv;
	}
	else
	{
		_status = str::format(u8"{} tests: {} fail - pass {}"sv, total, count_fail, count_ok);
	}

	return _status;
}

view_controller_ptr test_view::controller_from_location(const view_host_ptr& host, const pointi loc)
{
	for (int i = 0; i < col_count; ++i)
	{
		if (_col_header_bounds[i].contains(loc))
		{
			return std::make_shared<header_controller>(host, *this, _col_header_bounds[i], i);
		}
	}

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

std::u8string po_escape(std::u8string_view val)
{
	auto result = std::u8string(val);
	result = str::replace(result, u8"\\"sv, u8"\\\\"sv);
	result = str::replace(result, u8"\""sv, u8"\\\""sv);
	result = str::replace(result, u8"\n"sv, u8"\\n"sv);
	return result;
}

static void write_po_entry(u8ostream& f, const std::u8string_view& key, const std::u8string_view& val)
{
	const auto found = val.find(u8'\n');

	if (found == std::u8string_view::npos)
	{
		f << key << u8" \"" << po_escape(val) << u8"\"\n"sv;
	}
	else
	{
		f << key << u8" \"\"\n"sv;

		auto lines = str::split(val, false, str::is_cr_or_lf);
		auto lines_left = lines.size() - 1;

		for (auto line : lines)
		{
			if (lines_left > 0)
			{
				f << u8"\"" << po_escape(line) << u8"\\n\"\n"sv;
			}
			else
			{
				f << u8"\"" << po_escape(line) << u8"\"\n"sv;
			}

			lines_left -= 1;
		}
	}
}

static void write_po_entry(u8ostream& f, const po_entry& poe)
{
	write_po_entry(f, u8"msgid"sv, poe.id);

	if (poe.id_plural.empty())
	{
		write_po_entry(f, u8"msgstr"sv, poe.str);
	}
	else
	{
		write_po_entry(f, u8"msgid_plural"sv, poe.id_plural);
		write_po_entry(f, u8"msgstr[0]"sv, poe.str);
		write_po_entry(f, u8"msgstr[1]"sv, poe.str_plural);
	}

	f << u8"\n"sv;
}

static void generate_po(const std::u8string_view file_name)
{
	const auto app_folder = known_path(platform::known_folder::running_app_folder);
	const auto lang_folder = app_folder.combine(u8"languages"sv);
	const auto lang_path_in = lang_folder.combine_file(file_name);
	const auto lang_path_out = app_folder.combine_file(file_name);

	auto loaded_po = load_po(lang_path_in);

	if (!loaded_po.empty())
	{
		app_text_t texts;
		texts.load_lang(lang_path_in.name(), loaded_po);
		const auto generated_po = texts.gen_po();

		u8ostream f(platform::to_file_system_path(lang_path_out), std::ios::out);
		f << u8"# Diffractor translation\n"sv;

		write_po_entry(f, loaded_po[0]);

		for (const auto& p : generated_po)
		{
			write_po_entry(f, p);
		}
	}
}

void test_view::gen_po()
{
	generate_po(u8"cs.po"sv);
	generate_po(u8"de.po"sv);
	generate_po(u8"es.po"sv);
	generate_po(u8"it.po"sv);
	generate_po(u8"ja.po"sv);
}

void test_view::reset_graphics()
{
	_state._events.free_graphics_resources(false, false);
	_host->frame()->reset_graphics();
}
