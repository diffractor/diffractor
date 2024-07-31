// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2024  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

struct search_part
{
	df::search_term_modifier modifier;
	bool literal = false;

	std::u8string scope;
	std::u8string term;

	bool is_empty() const
	{
		return scope.empty() && term.empty();
	}

	void clear()
	{
		literal = false;
		modifier.clear();
		scope.clear();
		term.clear();
	}
};

class search_tokenizer
{
	enum class parse_state
	{
		scanning,
		text,
		quoted_text,
	};

	std::vector<search_part> results;
	search_part current_term;
	std::u8string current_text;

	static bool is_modifier(char8_t c)
	{
		return c == '-' || c == '~' || c == '!' || c == '|' || c == '&' || c == '(' || c == ')' || c == '>' || c == '<'
			|| c == '=';
	}

	static bool is_delimiter(char8_t c)
	{
		return c == '#' || c == ':' || c == ',' || c == ';' || c == '|' || c == '(' || c == ')';
	}

	static bool is_num(const std::u8string_view s)
	{
		for (const auto c : s)
		{
			if (!std::iswdigit(c))
				return false;
		}

		return !s.empty();
	}

	void append_current_term()
	{
		if (!current_text.empty())
		{
			if (current_term.scope.empty())
			{
				if (str::icmp(current_text, u8"or"sv) == 0 || str::icmp(current_text, tt.query_or) == 0)
				{
					current_term.modifier.logical_op = df::search_term_modifier_bool::m_or;
					current_text.clear();
					return;
				}
				if (str::icmp(current_text, u8"and"sv) == 0 || str::icmp(current_text, tt.query_and) == 0)
				{
					current_term.modifier.logical_op = df::search_term_modifier_bool::m_and;
					current_text.clear();
					return;
				}
			}

			current_term.term = current_text;
			current_text.clear();
		}

		if (!current_term.term.empty())
		{
			results.emplace_back(current_term);
			current_term.clear();
		}
	}

	void clear()
	{
		results.clear();
		current_term.clear();
		current_text.clear();
	}


public:
	std::vector<search_part> parse(const std::u8string_view text)
	{
		clear();

		auto st = parse_state::scanning;
		auto quote_char = '"';

		for (const auto c : text)
		{
			if (st == parse_state::text)
			{
				if (c == ':' && (is_num(current_text) || current_text.size() == 1))
					// This typically means a time 12:00 or folder c:\test
				{
					current_text += c;
				}
				else if (is_delimiter(c))
				{
					st = parse_state::scanning;
				}
				else if (current_text.empty() && std::iswspace(c))
				{
					// skip over
				}
				else if (!current_text.empty() && std::iswspace(c))
				{
					// space marks delimiter if we already have content
					st = parse_state::scanning;
				}
				else if (c == '\'' || c == '"')
				{
					st = parse_state::quoted_text;
					quote_char = c;
					current_term.literal = true;
				}
				else
				{
					current_text += c;
				}
			}
			else if (st == parse_state::quoted_text)
			{
				if (c == quote_char)
				{
					st = parse_state::scanning;
				}
				else
				{
					current_text += c;
				}

				continue;
			}

			if (st == parse_state::scanning)
			{
				if (c == '!')
				{
					append_current_term();
					current_term.modifier.positive = false;
				}
				else if (c == '-' && (current_term.scope.empty() || !current_text.empty()))
				{
					append_current_term();
					current_term.modifier.positive = false;
				}
				else if (c == '|')
				{
					append_current_term();
					current_term.modifier.logical_op = df::search_term_modifier_bool::m_or;
				}
				else if (c == '&')
				{
					append_current_term();
					current_term.modifier.logical_op = df::search_term_modifier_bool::m_and;
				}
				else if (c == '(')
				{
					append_current_term();
					current_term.modifier.begin_group += 1;
				}
				else if (c == ')')
				{
					current_term.modifier.end_group += 1;
				}
				else if (c == '<')
				{
					append_current_term();
					current_term.modifier.less_than = true;
				}
				else if (c == '>')
				{
					append_current_term();
					current_term.modifier.greater_than = true;
				}
				else if (c == '=')
				{
					append_current_term();
					current_term.modifier.equals = true;
				}
				else if (c == '#')
				{
					append_current_term();
					current_term.scope = u8"tag"sv;
				}
				else if (c == '@')
				{
					append_current_term();
					current_term.scope = u8"@"sv;
				}
				else if (c == ':')
				{
					if (!current_text.empty()) current_term.scope = current_text;
					current_text.clear();
				}
				else if (c == '\'' || c == '"')
				{
					append_current_term();
					st = parse_state::quoted_text;
					quote_char = c;
				}
				else if (!std::iswspace(c))
				{
					append_current_term();
					st = parse_state::text;
					current_text += c;
				}
			}
		}

		append_current_term();

		return results;
	}
};
