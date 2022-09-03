// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

class metadata_edits;

struct xmp_update_result
{
	bool success = false;
	df::file_path xmp_path;
};

namespace metadata_xmp
{
	void initialise();
	void term();

	void parse(prop::item_metadata& pd, df::cspan xmp);
	void parse(prop::item_metadata& pd, df::file_path path);

	xmp_update_result update(df::file_path file_path, df::file_path src_path, const metadata_edits& edits,
	                         std::u8string_view xmp_name);
	void update(std::u8string& buffer, const metadata_edits& edits);
	metadata_kv_list to_info(df::cspan xmp);
};
