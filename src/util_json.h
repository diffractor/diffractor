// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: JSON parsing and generation. Provides simple JSON document
// reading and writing for configuration and API responses.

#pragma once

#define RAPIDJSON_HAS_STDSTRING 1

#include <rapidjson/document.h>


namespace df::util::json
{
	// Values arrive from untrusted network responses: RapidJSON's typed getters assert rather than
	// check, and those asserts compile out in release, so the type must be verified here. That
	// includes the container: FindMember asserts IsObject(), so a document that failed to parse
	// aborts before any value check runs.
	inline std::string safe_string(const rapidjson::GenericValue<rapidjson::UTF8<char>>& json, const char* name)
	{
		if (!json.IsObject()) return {};

		const auto found = json.FindMember(name);
		return found != json.MemberEnd() && found->value.IsString()
			       ? str::safe_string(found->value.GetString())
			       : std::string{};
	}

	// Returns an empty object rather than asserting when the member is missing or not an object.
	inline const rapidjson::GenericValue<rapidjson::UTF8<char>>& safe_object(
		const rapidjson::GenericValue<rapidjson::UTF8<char>>& json, const char* name)
	{
		static const rapidjson::GenericValue<rapidjson::UTF8<char>> empty(rapidjson::kObjectType);
		if (!json.IsObject()) return empty;

		const auto found = json.FindMember(name);
		return found != json.MemberEnd() && found->value.IsObject() ? found->value : empty;
	}

	using json_doc = rapidjson::GenericDocument<rapidjson::UTF8<char>>;
	json_doc json_from_file(file_path path);
}
