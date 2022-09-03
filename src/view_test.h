// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

#include <utility>

#include "ui_view.h"
#include "ui_controllers.h"

class test_assert_exception : public std::exception
{
public:
	std::u8string message;

	explicit test_assert_exception(std::u8string m) : message(std::move(m))
	{
	}
};


class test_view final : public view_base
{
public:
	struct shared_test_context;

protected:
	using this_type = test_view;

	view_state& _state;
	view_host_ptr _host;

	sizei _extent;
	view_scroller _scroller;

	friend class scroll_controller;
	friend class clickable_controller;

	enum class test_state
	{
		Success,
		Failed,
		Running,
		Unknown
	};

	struct test_element final : public std::enable_shared_from_this<test_element>, public view_element
	{
		std::u8string _name;
		std::u8string _message;
		test_state _state = test_state::Unknown;
		int _time = 0;
		std::function<void(shared_test_context& stc)> _f;
		test_view& _view;

		test_element(test_view& view, std::u8string name, std::function<void(shared_test_context& stc)> f) noexcept
			: view_element(view_element_style::can_invoke), _name(std::move(name)), _f(std::move(f)), _view(view)
		{
		}

		bool has_failed() const
		{
			return _state == test_state::Failed;
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

		void render(ui::draw_context& dc, pointi element_offset) const override;
		sizei measure(ui::measure_context& mc, int width_limit) const override;
		void dispatch_event(const view_element_event& event) override;
		view_controller_ptr controller_from_location(const view_host_ptr& host, pointi loc, pointi element_offset,
		                                             const std::vector<recti>& excluded_bounds) override;
	};

	using test_ptr = std::shared_ptr<test_element>;
	std::vector<test_ptr> _tests;

public:
	test_view(view_state& state, view_host_ptr host);
	~test_view() override;

	void activate(sizei extent) override;
	void refresh();
	void run_tests();
	void run_test(const test_ptr& t);
	void register_tests();

	void register_test(std::u8string name, std::function<void(shared_test_context& stc)> f)
	{
		_tests.emplace_back(std::make_shared<test_element>(*this, std::move(name), std::move(f)));
	}

	void register_test(std::u8string name, std::function<void(void)> f)
	{
		_tests.emplace_back(
			std::make_shared<test_element>(*this, std::move(name), [f = std::move(f)](shared_test_context&) { f(); }));
	}

	void layout(ui::measure_context& mc, sizei extent) override;

	void deactivate() override
	{
	}

	view_controller_ptr controller_from_location(const view_host_ptr& host, pointi loc) override;

	void clear();
	void sort();

	test_ptr test_from_location(int y) const;

	void render_headers(ui::draw_context& dc, int x) const;
	void render(ui::draw_context& rc, view_controller_ptr controller) override;
	void mouse_wheel(pointi loc, int zDelta, ui::key_state keys) override;
};
