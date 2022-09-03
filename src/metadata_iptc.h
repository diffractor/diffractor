// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

struct tag_iptc
{
	uint32_t id;
	uint32_t string_id;
};

extern tag_iptc g_iptc_tags[];

struct metadata_iptc
{
	static void parse(prop::item_metadata& pd, df::cspan cs);
	static metadata_kv_list to_info(df::cspan cs);
};
