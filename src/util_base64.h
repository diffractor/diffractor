// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

std::u8string base64_encode(const uint8_t* input, size_t input_len);
std::vector<uint8_t> base64_decode(const char8_t* input, size_t input_len);

inline std::u8string base64_encode(const std::u8string_view s)
{
	return base64_encode(std::bit_cast<const uint8_t*>(s.data()), s.size());
};

inline std::u8string base64_encode(const std::vector<uint8_t>& s)
{
	return base64_encode(s.data(), s.size());
};

inline std::vector<uint8_t> base64_decode(const std::u8string_view s)
{
	return base64_decode(s.data(), s.size());
};
