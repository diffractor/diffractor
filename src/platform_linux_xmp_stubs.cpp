// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Stand-ins for everything metadata_xmp.cpp defines, for a build configured without the
// Adobe toolkit fork. Built only when diffractor::xmp did not resolve, and it is metadata_xmp.cpp
// that is built instead: the two are alternatives, never both.

#include "pch.h"

#include "metadata_xmp.h"

namespace
{
	void not_ported()
	{
		static bool reported = false;

		// One line for the subsystem, not one per call: these sit on scan paths that run per file.
		if (!std::exchange(reported, true))
		{
			df::log(__FUNCTION__, "not ported to linux: xmp");
		}
	}
}

void metadata_xmp::initialise()
{
	not_ported();
}

void metadata_xmp::term()
{
}

void metadata_xmp::parse(prop::item_metadata&, df::cspan)
{
	not_ported();
}

void metadata_xmp::parse(prop::item_metadata&, df::file_path)
{
	not_ported();
}

metadata_xmp::property_presence metadata_xmp::properties(df::cspan)
{
	not_ported();
	return {};
}

bool metadata_xmp::has_embedded_xmp(df::file_path)
{
	not_ported();
	return false;
}

// Reports failure rather than success-with-no-write: the write pipeline treats a false result as a
// refusal and leaves the user's file alone, which is the safe reading of "not implemented".
xmp_update_result metadata_xmp::update(df::file_path, df::file_path, const metadata_edits&, std::string_view,
                                       df::file_path)
{
	not_ported();
	return {};
}

void metadata_xmp::update(std::string&, const metadata_edits&)
{
	not_ported();
}

metadata_kv_list metadata_xmp::to_info(df::cspan)
{
	not_ported();
	return {};
}
