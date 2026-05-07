// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: File selection patterns. Implements item_selector for matching
// files by path patterns with recursive folder support.

#pragma once

namespace df
{
	class item_selector
	{
		folder_path _root;
		std::string _wildcard = "*.*"s;
		bool _recursive = false;

	public:
		item_selector() = default;

		item_selector(const std::string_view sv)
		{
			parse(sv);
			assert_true(is_valid());
		}

		item_selector(const folder_path f, const bool recursive = false,
		              const std::string_view wildcard = "*.*") :
			_root(f), _wildcard(wildcard), _recursive(recursive)
		{
			assert_true(is_valid());
		}

		bool match(const file_path path) const
		{
			bool is_match = false;

			if (_recursive)
			{
				is_match = starts(path.folder().text(), _root.text());
			}
			else
			{
				is_match = icmp(path.folder().text(), _root.text()) == 0;
			}

			if (str::icmp(_wildcard, "*.*") != 0)
			{
				is_match = wildcard_icmp(path.name(), _wildcard);
			}

			return is_match;
		}

		static int count_ending_stars(const std::string_view sv, size_t& star_start)
		{
			auto stars = 0;
			auto n = sv.size() - 1;

			while (n > 0)
			{
				const auto c = sv[n];

				if (c == '*')
				{
					stars += 1;
					star_start = n;
				}
				else if (!str::is_slash(c))
				{
					break;
				}

				n -= 1;
			}

			return stars;
		}

		void parse(const std::string_view sv)
		{
			const auto len = sv.size();

			if (len >= 2)
			{
				auto star_start = 0_z;
				auto stars = count_ending_stars(sv, star_start);

				if (stars >= 2)
				{
					_root = folder_path(sv.substr(0, star_start));
					_recursive = true;
				}
				else
				{
					const auto last_slash = sv.find_last_of("/\\");

					if (last_slash != std::string_view::npos && last_slash < sv.size())
					{
						const auto tail = sv.substr(last_slash + 1);
						auto root = sv;

						if (tail.find_first_of("*?") != std::string_view::npos)
						{
							_wildcard = str::cache(tail);
							root = sv.substr(0, last_slash);
							stars = count_ending_stars(root, star_start);
						}

						if (stars >= 2)
						{
							_root = folder_path(root.substr(0, star_start));
							_recursive = true;
						}
						else
						{
							_root = folder_path(root);
						}
					}
					else
					{
						_root = folder_path(sv);
					}
				}
			}
		}

		std::string str() const
		{
			auto result = std::string(_root.text());

			if (_recursive)
			{
				if (result.back() != '\\') result += '\\';
				result += "**";
			}

			if (has_wildcard())
			{
				if (result.back() != '\\') result += '\\';
				result += _wildcard;
			}

			return result;
		}

		item_selector parent() const
		{
			return _recursive || has_wildcard() ? _root : _root.parent();
		}

		folder_path folder() const
		{
			return _root;
		}

		const std::string& wildcard() const
		{
			return _wildcard;
		}

		bool has_wildcard() const
		{
			return _wildcard != "*.*";
		}

		bool is_recursive() const
		{
			return _recursive;
		}

		int compare(const item_selector& other) const
		{
			const auto folder_comp = _root.compare(other._root);
			if (folder_comp != 0) return folder_comp;
			if (_recursive != other._recursive) return !_recursive ? -1 : 1;
			return str::icmp(_wildcard, other._wildcard);
		}

		bool is_empty() const
		{
			return _root.is_empty();
		}

		static bool can_iterate(const std::string_view sv)
		{
			if (is_path(sv))
			{
				// no colans after 
				const item_selector sel(sv);
				return sel.can_iterate();
			}

			return false;
		}

		bool can_iterate() const
		{
			return _root.exists() ||
				platform::is_server(_root.text());
		}

		bool is_valid() const
		{
			return _root.is_valid();
		}

		bool has_media() const;

		friend bool operator==(const item_selector& lhs, const item_selector& rhs)
		{
			return lhs._root == rhs._root
				&& lhs._wildcard == rhs._wildcard
				&& lhs._recursive == rhs._recursive;
		}

		friend bool operator!=(const item_selector& lhs, const item_selector& rhs)
		{
			return !(lhs == rhs);
		}
	};
}
