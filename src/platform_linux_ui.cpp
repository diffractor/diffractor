// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Linux side of the UI platform surface -- the theme colours the palette takes from the
// system, the key table, and the UI-thread identity. platform_win_ui.cpp owns the same three on
// Windows, along with the window and message loop that have no counterpart here yet.

#include "pch.h"

#include <pthread.h>

///////////////////////////////////////////////////////////////////////////////////////////////////
// Style palette
///////////////////////////////////////////////////////////////////////////////////////////////////

namespace
{
	// The palette itself is portable (ui.cpp). The colours Windows reads from the system theme are
	// given their Windows dark-mode equivalents; a desktop integration would replace them with
	// whatever the running theme reports.
	struct color_style_initialiser
	{
		color_style_initialiser()
		{
			ui::style::color::install({0x00ffffff, 0x00000000, 0x00000000});
		}
	};

	const color_style_initialiser init_color_styles;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Key table
//
// The values are the Win32 virtual key codes the application logic was written against, restated
// here rather than remapped: a Linux input layer translates its own keysyms into these, so the
// meaning of a key stays in one place.
///////////////////////////////////////////////////////////////////////////////////////////////////

char32_t keys::APPS = 0x5D;
char32_t keys::BACK = 0x08;
char32_t keys::BROWSER_BACK = 0xA6;
char32_t keys::BROWSER_FAVORITES = 0xAB;
char32_t keys::BROWSER_FORWARD = 0xA7;
char32_t keys::BROWSER_HOME = 0xAC;
char32_t keys::BROWSER_REFRESH = 0xA8;
char32_t keys::BROWSER_SEARCH = 0xAA;
char32_t keys::BROWSER_STOP = 0xA9;
char32_t keys::DEL = 0x2E;
char32_t keys::DOWN = 0x28;
char32_t keys::END = 0x23;
char32_t keys::ESCAPE = 0x1B;
char32_t keys::F1 = 0x70;
char32_t keys::F2 = 0x71;
char32_t keys::F3 = 0x72;
char32_t keys::F4 = 0x73;
char32_t keys::F5 = 0x74;
char32_t keys::F6 = 0x75;
char32_t keys::F7 = 0x76;
char32_t keys::F8 = 0x77;
char32_t keys::F9 = 0x78;
char32_t keys::F10 = 0x79;
char32_t keys::F11 = 0x7A;
char32_t keys::HOME = 0x24;
char32_t keys::INSERT = 0x2D;
char32_t keys::LEFT = 0x25;
char32_t keys::MEDIA_NEXT_TRACK = 0xB0;
char32_t keys::MEDIA_PLAY_PAUSE = 0xB3;
char32_t keys::MEDIA_PREV_TRACK = 0xB1;
char32_t keys::MEDIA_STOP = 0xB2;
char32_t keys::NEXT = 0x22;
char32_t keys::OEM_4 = 0xDB;
char32_t keys::OEM_6 = 0xDD;
char32_t keys::OEM_MINUS = 0xBD;
char32_t keys::OEM_PLUS = 0xBB;
char32_t keys::PRIOR = 0x21;
char32_t keys::RETURN = 0x0D;
char32_t keys::RIGHT = 0x27;
char32_t keys::SPACE = 0x20;
char32_t keys::TAB = 0x09;
char32_t keys::UP = 0x26;
char32_t keys::VOLUME_DOWN = 0xAE;
char32_t keys::VOLUME_MUTE = 0xAD;
char32_t keys::VOLUME_UP = 0xAF;

///////////////////////////////////////////////////////////////////////////////////////////////////
// Thread identity
///////////////////////////////////////////////////////////////////////////////////////////////////

namespace
{
	// Claimed by whichever thread runs main, matching the Windows build where the UI thread is the
	// one that created the window.
	const pthread_t ui_thread = ::pthread_self();
}

bool ui::is_ui_thread()
{
	return ::pthread_equal(::pthread_self(), ui_thread) != 0;
}

// No display connection yet, so report a conventional desktop rather than an empty rectangle a
// layout would divide by.
recti ui::desktop_bounds(bool)
{
	return {0, 0, 1920, 1080};
}

std::any ui::focus()
{
	return {};
}

void ui::focus(const std::any&)
{
}
