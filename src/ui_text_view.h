// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2024  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once
#include "ui_elements.h"


class text_view_control : public view_element
{
	const int padding = 4;
	df::blob _data;

	mutable uint32_t _x_data = 0;
	mutable uint32_t _x_text = 0;
	mutable uint32_t _bytes_per_line = 0;
	mutable uint32_t _chars_per_line = 0;
	mutable uint32_t _line_height = 0;
	mutable uint32_t _char_width = 0;
	mutable uint32_t _line_count = 0;

	ui::style::font_face _font = ui::style::font_face::code;

public:
	text_view_control(df::blob data, const view_element_style style_in) noexcept : view_element(style_in),
		_data(std::move(data))
	{
	}

	static std::array<int, 256> make_char_map()
	{
		std::array<int, 256> result;

		for (auto i = 0; i < 256; i++)
		{
			auto c = i;

			if (c < 32 || c > 127) { c = '.'; }
			else if (std::isspace(c)) { c = ' '; }
			//else if (std::ispunct(c)) {}
			//else if (std::isalnum(c)) {}
			//else { c = "."sv; };

			result[i] = c;
		}

		return result;
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		static const auto char_map = make_char_map();
		static constexpr char8_t hex_chars[16] = {
			'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
		};

		if (_line_count > 0)
		{
			const auto logical_bounds = bounds.offset(element_offset);
			const auto clip_bounds = dc.clip_bounds().intersection(logical_bounds);
			const auto first_line = (clip_bounds.top - logical_bounds.top) / _line_height;
			const auto last_line = first_line + ((clip_bounds.height() + _line_height) / _line_height);
			const auto clr = ui::color(dc.colors.foreground, dc.colors.alpha);

			std::u8string line(_chars_per_line, ' ');
			const auto left = (logical_bounds.left + logical_bounds.right - (_char_width * line.size())) / 2;

			for (auto i = first_line; i < last_line; ++i)
			{
				const auto start_address = i * _bytes_per_line;

				if (start_address < _data.size())
				{
					const auto limit = std::min(_bytes_per_line, _data.size() - start_address);
					const auto data = _data.data() + start_address;

					auto x = 0u;

					for (auto j = 0u; j < sizeof(start_address); ++j)
					{
						const auto byte = std::bit_cast<const uint8_t*>(&start_address)[j];

						line[x] = hex_chars[(byte & 0xF0) >> 4];
						++x;
						line[x] = hex_chars[(byte & 0x0F) >> 0];
						++x;
					}

					x += 2;

					for (auto j = 0u; j < limit; ++j)
					{
						const auto byte = data[j];

						line[x] = hex_chars[(byte & 0xF0) >> 4];
						++x;
						line[x] = hex_chars[(byte & 0x0F) >> 0];
						x += ((j % 8) == 7) ? 3 : 2;
					}

					for (auto j = 0u; j < limit; ++j)
					{
						line[x] = char_map[data[j] & 0xff];
						x += ((j % 8) == 7) ? 2 : 1;
					}

					const auto y = logical_bounds.top + (i * _line_height);
					dc.draw_text(line, recti(left, y, logical_bounds.right, y + _line_height), _font,
						ui::style::text_style::single_line, clr, {});
				}
			}
		}
	}

	static uint32_t calc_chars_per_line(uint32_t bytes_per_line)
	{
		return 8 + 4 + (bytes_per_line * 4) + ((bytes_per_line / 8) * 2);
	}

	sizei measure(ui::measure_context& mc, const int width_limit) const override
	{
		const auto extent = mc.measure_text("00000000"sv, _font, ui::style::text_style::single_line, 200);

		_char_width = extent.cx / 8;
		_line_height = extent.cy + padding;
		_bytes_per_line = 0;
		_chars_per_line = 0;

		while (static_cast<int>(calc_chars_per_line(_bytes_per_line + 8) * _char_width) < width_limit)
		{
			_bytes_per_line += 8;
			_chars_per_line = calc_chars_per_line(_bytes_per_line);
		}

		_line_count = _bytes_per_line > 0 ? df::round_up(_data.size(), _bytes_per_line) : 0;

		return { width_limit, static_cast<int>(_line_height * _line_count) };
	}
};
