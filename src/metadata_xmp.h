// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: XMP metadata parsing and writing using Adobe XMP SDK. Handles extensible
// metadata including ratings, labels, keywords, and editing history.

#pragma once

#include "model_property.h"

class metadata_edits;

struct xmp_update_result
{
	bool success = false;
	df::file_path xmp_path;
};

namespace metadata_xmp
{
	struct property_presence
	{
		bool tags = false;
		bool rating = false;
	};

	void initialise();
	void term();

	void parse(prop::item_metadata& pd, df::cspan xmp);
	void parse(prop::item_metadata& pd, df::file_path path);
	property_presence properties(df::cspan xmp);

	// Which patch of the sphere a panorama declares it holds. Read from the file being displayed
	// rather than from the index: only the item on screen needs it, and reading it here costs no
	// stored field and no re-index. An undeclared or contradictory crop comes back invalid, which
	// the caller resolves against the pixels it actually has.
	prop::panorama_geometry panorama(df::file_path path);

	// True when the file already carries an embedded XMP packet. Such a file can be updated
	// where it lies, because the toolkit rewrites the existing packet instead of restructuring
	// the format to make room for a first one.
	bool has_embedded_xmp(df::file_path path);

	// dst_xmp_path is the exact sidecar file to write. The caller stages it next to the media
	// file actually being written so a failure can never touch the live sidecar.
	xmp_update_result update(df::file_path file_path, df::file_path src_path, const metadata_edits& edits,
	                         std::string_view src_xmp_name, df::file_path dst_xmp_path);
	void update(std::string& buffer, const metadata_edits& edits);
	metadata_kv_list to_info(df::cspan xmp);
};
