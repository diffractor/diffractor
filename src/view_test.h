// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2024  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

#include "view_list.h"

class test_assert_exception : public std::exception
{
public:
	std::u8string message;

	explicit test_assert_exception(std::u8string m) : message(std::move(m))
	{
	}
};


class test_view final : public list_view
{
public:
	struct shared_test_context;

protected:
	using this_type = test_view;

	friend class scroll_controller;
	friend class clickable_controller;

	enum class test_state
	{
		Success,
		Failed,
		Running,
		Unknown
	};

	struct test_item final : public std::enable_shared_from_this<test_item>, public view_element
	{
		std::u8string _name;
		std::u8string _message;
		std::u8string _time_text;
		test_state _state = test_state::Unknown;
		int _time = 0;
		std::function<void(shared_test_context& stc)> _f;
		test_view& _view;
		row_element_ptr _row;

		test_item(test_view& view, std::u8string name, row_element_ptr row, std::function<void(shared_test_context& stc)> f) noexcept
			: view_element(view_element_style::can_invoke), _name(std::move(name)), _f(std::move(f)), _view(view), _row(std::move(row))
		{
			update_row();
		}

		bool has_failed() const
		{
			return _state == test_state::Failed;
		}

		void update_row()
		{
			_row->_text[1] = _name;

			switch (_state)
			{
			case test_state::Success:
				_row->_text_color[0] = ui::lighten(ui::style::color::info_background, 0.55f);
				_row->_text[0] = u8"Success"sv;
				_row->_order = 100;
				break;
			case test_state::Failed:
				_row->_text_color[0] = ui::lighten(ui::style::color::warning_background, 0.55f);
				_row->_text[0] = u8"Failed"sv;
				_row->_order = 1;
				break;
			case test_state::Running:
				_row->_text_color[0] = ui::lighten(ui::style::color::important_background, 0.55f);
				_row->_text[0] = u8"Running"sv;
				_row->_order = 2;
				break;
			case test_state::Unknown:
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

		void perform(view_state& s, shared_test_context& stc)
		{
			const auto t = df::now();

			try
			{
				s.queue_ui([this] { _state = test_state::Running; });
				_f(stc);
				s.queue_ui([this, t]
					{
						_message.clear();
						_state = test_state::Success;
						_time = df::round((df::now() - t) * 1000);
					});
			}
			catch (const test_assert_exception& e)
			{
				s.queue_ui([this, e, t]
					{
						_state = test_state::Failed;
						_message = e.message;
						_time = df::round((df::now() - t) * 1000);
					});
			}
		}
	};

	using test_ptr = std::shared_ptr<test_item>;
	std::vector<test_ptr> _tests;
	std::u8string _title;

public:
	test_view(view_state& state, view_host_ptr host);
	~test_view() override;

	void activate(sizei extent) override;
	void refresh() override;
	void reload() override;
	void run_tests();
	void run_test(const test_ptr& t);
	void register_tests();

	void register_test(std::u8string name, std::function<void(shared_test_context& stc)> f)
	{
		auto row = std::make_shared<row_element>(*this);
		auto test = std::make_shared<test_item>(*this, std::move(name), row, std::move(f));
		_rows.emplace_back(row);
		_tests.emplace_back(test);
	}

	void register_test(std::u8string name, std::function<void(void)> f)
	{
		auto row = std::make_shared<row_element>(*this);
		auto test = std::make_shared<test_item>(*this, std::move(name), row, [f = std::move(f)](shared_test_context&) { f(); });
		_rows.emplace_back(row);
		_tests.emplace_back(test);
	}

	void deactivate() override
	{
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, pointi loc) override;

	void clear();
	void sort();

	test_ptr test_from_location(int y) const;

	void exit() override
	{
		_state.view_mode(view_type::items);
	}

	std::u8string_view title() override
	{
		_title = str::format(u8"{}: Test"sv, s_app_name);
		return _title;
	}
};
