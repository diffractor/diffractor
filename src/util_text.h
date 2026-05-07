// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Text formatting and natural language processing. Handles
// pluralization, localized text formatting, and text templates.

#pragma once

namespace str
{
	df::string_map extract_url_params(std::string_view s);
	df::string_map split_url_params(std::string_view s);
	void count_ranges(df::dense_string_counts& counts, std::string_view text);
	df::string_counts guess_word(const df::string_counts& counts, std::string_view pattern);
};
