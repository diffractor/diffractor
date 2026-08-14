// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Deliberate stand-ins for the subsystems the Linux port has not reached, so the rest can
// be built and run. Nothing here pretends to work: each returns the value the caller already treats
// as failure, so no path can mistake a stub for a result.
//
// Every entry is a to-do. docs/linux.md owns the order they come off this list; the codec and XMP
// entries need their vendored libraries built. The FFmpeg stand-ins live in
// platform_linux_av_stubs.cpp instead, because that whole file is an alternative to av_format.cpp
// rather than a gap in it.

#include "pch.h"

#include "files.h"
#include "metadata_xmp.h"
#include "util_spell.h"
#include "util_zip.h"

namespace
{
	void not_ported(const std::string_view what)
	{
		static df::hash_set<std::string> reported;

		// One line per subsystem, not per call: these sit on scan paths that run per file.
		if (reported.insert(std::string(what)).second)
		{
			df::log(__FUNCTION__, std::format("not ported to linux: {}", what));
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Codecs. Each needs its vendored library built; see docs/linux.md.
///////////////////////////////////////////////////////////////////////////////////////////////////

file_scan_result scan_heif(read_stream&, const scan_intent, const bool)
{
	not_ported("heif"sv);
	return {};
}

file_scan_result scan_jxl(read_stream&)
{
	not_ported("jxl"sv);
	return {};
}

ui::surface_ptr load_heif(read_stream&, load_diagnostic*)
{
	not_ported("heif"sv);
	return {};
}

ui::surface_ptr load_jxl(read_stream&, load_diagnostic*)
{
	not_ported("jxl"sv);
	return {};
}

file_load_result load_raw(df::file_path, bool)
{
	not_ported("raw"sv);
	return {};
}

file_scan_result files::scan_raw(df::file_path, std::string_view, bool, sizei, scan_intent)
{
	not_ported("raw"sv);
	return {};
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Spell checking. Needs hunspell, which is not linked here.
///////////////////////////////////////////////////////////////////////////////////////////////////

// spell_check holds a unique_ptr<Hunspell>, so its destructor needs the type complete even though
// lazy_load never creates one here. Installing libhunspell-dev and building util_spell.cpp
// replaces this whole section.
class Hunspell
{
};

void spell_check::lazy_download(df::async_i&) const
{
	not_ported("hunspell"sv);
}

void spell_check::lazy_load()
{
	not_ported("hunspell"sv);
}

// Every word is accepted: marking correct words as misspelled would be worse than not checking.
bool spell_check::is_word_valid(std::string_view) const
{
	return true;
}

std::vector<std::string> spell_check::suggest(std::string_view) const
{
	return {};
}

spell_check& spell()
{
	static spell_check instance;
	return instance;
}

spell_check::spell_check() = default;
spell_check::~spell_check() = default;

///////////////////////////////////////////////////////////////////////////////////////////////////
// Zip. Needs minizip, which is not linked here.
///////////////////////////////////////////////////////////////////////////////////////////////////

df::zip_file::~zip_file() = default;

bool df::zip_file::create(file_path)
{
	not_ported("minizip"sv);
	return false;
}

bool df::zip_file::close()
{
	return false;
}

bool df::zip_file::add(file_path, std::string_view) const
{
	return false;
}

bool df::zip_file::add(file_path)
{
	return false;
}
