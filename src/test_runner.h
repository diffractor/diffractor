// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Embedded test registration and command-line test runner declarations.

#pragma once

#include "test.h"

void register_tests1(view_state& state, test_registry& tests);
void register_tests2(view_state& state, test_registry& tests);
void register_tests3(view_state& state, test_registry& tests);
void register_tests4(view_state& state, test_registry& tests);
void register_tests5(view_state& state, test_registry& tests);
void register_tests6(view_state& state, test_registry& tests);
void register_tests7(view_state& state, test_registry& tests);
void register_tests8(view_state& state, test_registry& tests);

inline void register_tests(view_state& state, test_registry& registry)
{
	register_tests1(state, registry);
	register_tests2(state, registry);
	register_tests3(state, registry);
	register_tests4(state, registry);
	register_tests5(state, registry);
	register_tests6(state, registry);
	register_tests7(state, registry);
	register_tests8(state, registry);
}

int run_console_tests(std::string_view test_filter = "*");
