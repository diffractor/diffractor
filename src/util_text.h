// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2024  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

namespace str
{
	df::string_map extract_url_params(std::u8string_view s);
	df::string_map split_url_params(std::u8string_view s);
	void count_ranges(df::dense_string_counts& counts, std::u8string_view text);
	df::string_counts guess_word(df::string_counts& counts, std::u8string_view pattern);
};
