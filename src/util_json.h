// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

#define RAPIDJSON_HAS_STDSTRING 1

#include <rapidjson/document.h>


namespace df::util::json
{
	inline std::u8string safe_string(const rapidjson::GenericValue<rapidjson::UTF8<char8_t>>& json, const char8_t* name)
	{
		const auto found = json.FindMember(name);
		return (found != json.MemberEnd()) ? str::safe_string(found->value.GetString()) : std::u8string{};
	}

	inline int safe_int(const rapidjson::GenericValue<rapidjson::UTF8<char8_t>>& json, const char8_t* name)
	{
		const auto found = json.FindMember(name);
		return (found != json.MemberEnd()) ? found->value.GetInt() : 0;
	}

	inline float safe_float(const rapidjson::GenericValue<rapidjson::UTF8<char8_t>>& json, const char8_t* name)
	{
		const auto found = json.FindMember(name);
		return (found != json.MemberEnd()) ? found->value.GetFloat() : 0.0f;
	}

	inline bool safe_bool(const rapidjson::GenericValue<rapidjson::UTF8<char8_t>>& json, const char8_t* name)
	{
		const auto found = json.FindMember(name);
		return (found != json.MemberEnd()) ? found->value.GetBool() : false;
	}

	using json_doc = rapidjson::GenericDocument<rapidjson::UTF8<char8_t>>;
	json_doc json_from_file(file_path path);
}
