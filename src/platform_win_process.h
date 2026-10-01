// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: What wWinMain in platform_win_ui.cpp needs from platform_win_process.cpp, which carries the
// process lifecycle around the window. Windows-only and internal to those two files.

#pragma once

// The running application, for the crash and recovery callbacks Windows makes with no context.
extern std::weak_ptr<ui::app> g_app;

// Binds stdout and stderr for the command-line tools (/test, /gen-docs, /validate-po, /dup-report).
void setup_headless_console();

// A bare message box, for failures before there is a window to report them in.
void show_fatal_error(std::string_view message);

// Registers the command line Windows restarts with, and the recovery callback that saves state when
// a hung or crashing process is terminated.
void setup_restart(std::string_view restart_cmd_line);
void unregister_restart();

// Falls back from whatever graphics subsystem the previous run crashed in.
void apply_gpu_crash_guard();

#ifndef WINSTORE
// Writes a minidump and reports the crash for as long as it is in scope.
struct unhandled_exception_filter
{
	PTOP_LEVEL_EXCEPTION_FILTER _original = nullptr;

	unhandled_exception_filter();
	~unhandled_exception_filter();
};
#endif
