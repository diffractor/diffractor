// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Scoped function entry/exit logging. Emits a start and an exit log
// line for the lifetime of the guard.

#pragma once

struct log_func
{
	const std::string_view context;
	const std::string_view context2;

	log_func(const std::string_view c, const std::string_view c2 = {}) : context(c), context2(c2)
	{
		if (!str::is_empty(context2))
		{
			df::log(context, std::format("start {}", context2));
		}
		else
		{
			df::log(context, "start");
		}
	}

	~log_func()
	{
		if (!str::is_empty(context2))
		{
			df::log(context, std::format("exit {}", context2));
		}
		else
		{
			df::log(context, "exit");
		}
	}
};
