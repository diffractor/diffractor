// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Built-in test runner UI. Displays and runs unit tests
// from the embedded test suite with result reporting.

#pragma once

#include "view_list.h"

class test_view;
struct shared_test_context;
struct test_item;

using test_ptr = std::shared_ptr<test_item>;

struct test_registry
{
	virtual void add(std::string name, std::function<void(shared_test_context& stc)> f) = 0;
	virtual void add(std::string name, std::function<void(void)> f) = 0;
};

void register_tests1(view_state& state, test_registry& tests);
void register_tests2(view_state& state, test_registry& tests);
void register_tests3(view_state& state, test_registry& tests);
void register_tests4(view_state& state, test_registry& tests);
void register_tests5(view_state& state, test_registry& tests);
void register_tests6(view_state& state, test_registry& tests);

inline void register_tests(view_state& state, test_registry& registry)
{
	register_tests1(state, registry);
	register_tests2(state, registry);
	register_tests3(state, registry);
	register_tests4(state, registry);
	register_tests5(state, registry);
	register_tests6(state, registry);
}

bool is_running_tests();
void run_tests(view_state& state, std::vector<test_ptr> tests);
int run_console_tests(std::string_view test_filter = "*");

class test_assert_exception final : public std::exception
{
public:
	std::string message;

	explicit test_assert_exception(std::string m) : message(std::move(m))
	{
	}
};

enum class test_state
{
	pass,
	fail,
	running,
	unknown
};

struct test_item final : std::enable_shared_from_this<test_item>, view_element
{
	std::string _name;
	std::string _message;
	std::string _time_text;
	test_state _state = test_state::unknown;
	int _time = 0;
	std::function<void(shared_test_context& stc)> _f;
	test_view& _view;
	list_view::row_element_ptr _row;

	test_item(test_view& view, std::string name, list_view::row_element_ptr row,
	          std::function<void(shared_test_context& stc)> f) noexcept
		: view_element(view_element_style::can_invoke), _name(std::move(name)), _f(std::move(f)), _view(view),
		  _row(std::move(row))
	{
		update_row();
	}

	bool has_failed() const
	{
		return _state == test_state::fail;
	}

	void update_row();

	void perform(const view_state& s, shared_test_context& stc);
};


class test_view final : public list_view, public test_registry
{
protected:
	using this_type = test_view;

	friend class scroll_controller;
	friend class clickable_controller;


	std::vector<test_ptr> _tests;
	std::string _title;
	std::string _status;

public:
	test_view(view_state& state, view_host_ptr host);
	~test_view() override;

	void activate(sizei extent) override;
	void refresh() override;
	void reload() override;
	static void gen_po();
	void reset_graphics() const;
	void run_test(const test_ptr& t);
	void run_all() const { run_tests(_state, _tests); };

	void add(std::string name, std::function<void(shared_test_context& stc)> f) override
	{
		auto row = std::make_shared<row_element>(*this);
		auto test = std::make_shared<test_item>(*this, std::move(name), row, std::move(f));
		_rows.emplace_back(row);
		_tests.emplace_back(test);
	}

	void add(std::string name, std::function<void(void)> f) override
	{
		auto row = std::make_shared<row_element>(*this);
		auto test = std::make_shared<test_item>(*this, std::move(name), row, [f = std::move(f)](shared_test_context&)
		{
			f();
		});
		_rows.emplace_back(row);
		_tests.emplace_back(test);
	}

	void deactivate() override
	{
	}

	std::string_view status() override;

	view_controller_ptr controller_from_location(const view_host_ptr& host, pointi loc) override;

	test_ptr test_from_location(int y) const;

	void exit() override
	{
		_state.view_mode(view_type::items);
	}

	std::string_view title() override
	{
		_title = std::format("{}: Test", s_app_name);
		return _title;
	}

	std::array<text_t, max_col_count> col_titles() override
	{
		return std::array<text_t, max_col_count>
		{
			tt.status,
			tt.test,
			tt.message,
			{}
		};
	};
};
