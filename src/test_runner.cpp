// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Shared test infrastructure definitions, utility tests, test registration,
// and console test runner.

#include "pch.h"
#include "test_utils.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
// Shared global definitions
///////////////////////////////////////////////////////////////////////////////////////////////////

temp_files _temps;
std::atomic_int test_version = 0;
df::cancel_token test_token(test_version);


///////////////////////////////////////////////////////////////////////////////////////////////////
// Test runner infrastructure
///////////////////////////////////////////////////////////////////////////////////////////////////

void run_test(view_state& state, const test_ptr& test)
{
	shared_test_context stc;
	test->perform(state, stc);
	state.invalidate_view(view_invalid::command_state | view_invalid::status);

	state.queue_ui([&s = state]
	{
		_temps.delete_temps();
		s.invalidate_view(view_invalid::command_state | view_invalid::status);
	});
}

static std::atomic_int tests_running = 0;

bool is_running_tests()
{
	return tests_running != 0;
}

void run_tests(view_state& state, std::vector<test_ptr> tests)
{
	if (tests_running == 0)
	{
		++tests_running;
		state.invalidate_view(view_invalid::command_state | view_invalid::status);

		state.queue_async(async_queue::work, [&s = state, tests]
		{
			shared_test_context stc;

			for (const auto& test : tests)
			{
				test->perform(s, stc);
				s.queue_ui([test] { test->update_row(); });
			}

			_temps.delete_temps();
			--tests_running;
			s.invalidate_view(view_invalid::command_state | view_invalid::status);
		});
	}
}


///////////////////////////////////////////////////////////////////////////////////////////////////
// Console test runner
///////////////////////////////////////////////////////////////////////////////////////////////////

struct console_test_entry
{
	std::u8string name;
	std::function<void(shared_test_context& stc)> func;
};

struct console_test_registry final : test_registry
{
	std::vector<console_test_entry> entries;

	void add(std::u8string name, std::function<void(shared_test_context& stc)> f) override
	{
		entries.push_back({std::move(name), std::move(f)});
	}

	void add(std::u8string name, std::function<void(void)> f) override
	{
		entries.push_back({std::move(name), [f = std::move(f)](shared_test_context&) { f(); }});
	}
};

int run_console_tests(const std::u8string_view test_filter)
{
	load_file_types();
	metadata_xmp::initialise();

	null_state_strategy ss;
	null_async_strategy as;
	location_cache locations;
	index_state index(as, locations);
	view_state state(ss, as, index, nullptr);

	console_test_registry registry;
	register_tests(state, registry);

	// Filter tests by wildcard pattern
	std::vector<console_test_entry> filtered;
	for (auto& entry : registry.entries)
	{
		if (str::wildcard_icmp(entry.name, test_filter))
		{
			filtered.push_back(std::move(entry));
		}
	}

	const auto total = static_cast<int>(filtered.size());
	int passed = 0;
	int failed = 0;

	printf("Running %d tests...\n\n", total);

	shared_test_context stc;

	for (const auto& entry : filtered)
	{
		const auto t = df::now();
		const auto name_utf8 = std::string(reinterpret_cast<const char*>(entry.name.data()), entry.name.size());

		try
		{
			entry.func(stc);
			const auto elapsed_ms = df::round((df::now() - t) * 1000);
			printf("  PASS  %s (%dms)\n", name_utf8.c_str(), elapsed_ms);
			++passed;
		}
		catch (const test_assert_exception& e)
		{
			const auto elapsed_ms = df::round((df::now() - t) * 1000);
			const auto msg = std::string(reinterpret_cast<const char*>(e.message.data()), e.message.size());
			printf("  FAIL  %s (%dms)\n        %s\n", name_utf8.c_str(), elapsed_ms, msg.c_str());
			++failed;
		}
		catch (const std::exception& e)
		{
			const auto elapsed_ms = df::round((df::now() - t) * 1000);
			printf("  FAIL  %s (%dms)\n        Exception: %s\n", name_utf8.c_str(), elapsed_ms, e.what());
			++failed;
		}
	}

	_temps.delete_temps();

	printf("\n========================================\n");
	printf("Results: %d passed, %d failed, %d total\n", passed, failed, total);
	printf("========================================\n");

	return failed > 0 ? 1 : 0;
}
