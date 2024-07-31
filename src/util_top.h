// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2024  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once


template <typename val_t, typename sorter_t>
std::vector<val_t> top_vec(std::vector<std::pair<int, val_t>> all, int max)
{
	std::ranges::sort(all, [](auto&& l, auto&& r) { return l.first > r.first; });

	std::vector<val_t> results;
	results.reserve(max);

	for (const auto& i : all)
	{
		if (max-- > 0)
		{
			results.emplace_back(i.second);
		}
	}

	std::ranges::sort(results, sorter_t());

	return results;
}

inline std::vector<std::u8string_view> top_map(const df::string_counts& counts, const int limit)
{
	std::vector<std::pair<int, std::u8string_view>> all;
	all.reserve(counts.size());

	for (const auto& i : counts)
	{
		all.emplace_back(i.second, i.first);
	}

	return top_vec<std::u8string_view, str::iless>(all, limit);
}