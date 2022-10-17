// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

namespace platform
{
	bool exists(df::file_path path);
	bool exists(df::folder_path path);
	df::file_path resolve_link(df::file_path path);
	bool is_server(std::u8string_view path);
};

namespace df
{
	class file_path;
	class folder_path;

	using file_paths = std::vector<file_path>;
	using folder_paths = std::vector<folder_path>;

	struct paths
	{
		file_paths files;
		folder_paths folders;
	};


	constexpr bool is_path_sep(const wchar_t c)
	{
		return c == L'\\' || c == L'/';
	}

	constexpr bool is_path(const char8_t* s)
	{
		if (s == nullptr || s[0] == 0 || s[1] == 0) return false;

		if (s[1] == L':' && (is_path_sep(s[2]) || s[2] == 0))
		{
			const auto first_char = str::to_lower(s[0]);
			return first_char >= 'a' && first_char <= 'z';
		}

		return is_path_sep(s[0]) && is_path_sep(s[1]); // unc path
	}

	constexpr bool is_path(const std::u8string_view r)
	{
		if (r.size() < 3) return false;
		return is_path(r.data());
	}

	constexpr std::u8string_view::size_type find_last_slash(const std::u8string_view path)
	{
		return path.find_last_of(u8"/\\"sv);
	}

	constexpr std::u8string_view::size_type find_ext(const std::u8string_view path)
	{
		const auto last = path.find_last_of(u8"./\\"sv);
		if (last == std::u8string_view::npos || path[last] != '.') return path.size();
		return last;
	}

	constexpr std::u8string_view::size_type find_filename(const std::u8string_view path)
	{
		const auto last_slash = path.find_last_of(u8"/\\"sv);
		if (last_slash != std::u8string_view::npos && path.size() > last_slash) return last_slash;
		return std::u8string_view::npos;
	}

	constexpr bool is_illegal_name(const std::u8string_view name)
	{
		if (name.size() > 215)
		{
			return true;
		}

		constexpr auto illegal = u8"\\/:*?\"<>|"sv;

		return name.find_first_of(illegal) != std::u8string_view::npos;
	}

	class folder_path
	{
	private:
		str::cached _s;

		static str::cached cached_normalized_folder(const std::u8string_view sv_in)
		{
			auto sv = sv_in;
			while (sv.size() > 3 && str::is_slash(sv.back())) sv.remove_suffix(1);

			const auto has_wrong_slash = sv.find('/') != std::u8string_view::npos;
			const auto is_drive_missing_slash = sv.size() == 2 && sv[1] == ':';

			if (!has_wrong_slash && !is_drive_missing_slash)
			{
				return str::cache(sv);
			}

			auto normalized = std::u8string(sv);

			for (auto&& c : normalized)
			{
				if (c == '/')
				{
					c = '\\';
				}
			}

			if (is_drive_missing_slash)
			{
				// drive does need last slash
				normalized += '\\';
			}

			return str::cache(normalized);
		}

		static bool is_qualified(const std::u8string_view s)
		{
			if (s.size() > 2)
			{
				if (s[1] == 0 || s[2] == 0) return false; // Too short
				if (is_path_sep(s[0]) && is_path_sep(s[1])) return true; // network
				if (s[1] == ':' && is_path_sep(s[2])) return true; // drive
			}

			return false;
		}

	public:
		static bool is_guid_path(const std::u8string_view s)
		{
			if (s.size() < 8) return false;
			return s[0] == L':' && s[1] == L':' && s[2] == L'{';
		}

		static bool is_device_path(const std::u8string_view s)
		{
			return str::starts(s, u8"\\Device\\"sv);
		}

		folder_path() noexcept = default;
		folder_path(const folder_path&) noexcept = default;
		folder_path& operator=(const folder_path&) noexcept = default;
		folder_path(folder_path&&) noexcept = default;
		folder_path& operator=(folder_path&&) noexcept = default;

		/*folder_path(LPCWSTR w)
		{
			_s = cached_normalized_folder(str::utf16_to_utf8(w));
			df::assert_true(is_valid());
		}*/

		folder_path(const std::wstring_view w)
		{
			_s = cached_normalized_folder(str::utf16_to_utf8(w));
			assert_true(is_valid());
		}

		/*folder_path(const std::u8string_view str)
		{
			_s = cached_normalized_folder(str);
			df::assert_true(is_valid());
		}*/

		/*folder_path(LPCSTR sz, int len = -1)
		{
			_s = cached_normalized_folder(sz, len);
			df::assert_true(is_valid());
		}*/

		folder_path(const std::u8string_view sr) noexcept
		{
			_s = cached_normalized_folder(sr);
			assert_true(is_valid());
		}

		const str::cached text() const
		{
			return _s;
		}

		void clear()
		{
			_s.clear();
		}

		bool is_empty() const
		{
			return _s.is_empty();
		}

		size_t size() const
		{
			return _s.size();
		}

		bool is_valid() const
		{
			if (is_empty())
				return true;

			if (_s.is_empty())
			{
				return false;
			}

			return is_computer(_s) ||
				is_guid_path(_s) ||
				is_device_path(_s) ||
				is_path(_s) ||
				platform::is_server(_s);
		}


		bool is_save_valid() const
		{
			if (is_empty())
				return false;

			if (!is_valid())
				return false;

			const auto last_slash = find_last_slash();
			const auto name = is_root() ? _s : _s.substr(last_slash + 1);

			return !is_illegal_name(name);
		}

		folder_path parent() const
		{
			if (is_root())
			{
				return *this;
			}

			assert_true(!str::is_slash(last_char(_s)));
			const auto found_last_slash = find_last_slash();
			if (found_last_slash == std::u8string_view::npos || found_last_slash < 2) return folder_path(_s);
			return folder_path(_s.substr(0, found_last_slash));
		}

		bool is_root() const
		{
			return is_root(_s);
		}

		bool is_unc_path() const
		{
			if (_s.is_empty()) return false;
			return is_path_sep(_s[0]) && is_path_sep(_s[1]); // unc path
		}

		bool exists() const
		{
			return platform::exists(*this);
		}

		static bool is_drive(const std::u8string_view s)
		{
			const auto len = str::len(s);
			if (len != 3 && len != 2) return false;
			if (s[1] != ':') return false;
			if (len == 3 && !is_path_sep(s[2])) return false;
			const auto first_char = str::to_lower(s[0]);
			return first_char >= 'a' && first_char <= 'z';
		}

		/*static bool is_drive(const wchar_t* a)
		{
			const auto len = wcslen(a);
			if (len != 3 && len != 2) return false;
			if (a[1] != ':') return false;
			if (len == 3 && !is_path_sep(a[2])) return false;
			const auto first_char = str::to_lower(a[0]);
			return first_char >= 'a' && first_char <= 'z';
		}*/

		static bool is_root(const std::u8string_view sv)
		{
			return is_drive(sv);
		}

		bool is_drive() const
		{
			return is_drive(_s);
		}

		bool is_qualified() const
		{
			return is_qualified(_s);
		}

		static bool is_computer(const std::u8string_view path)
		{
			return str::icmp(path, u8"Computer"sv) == 0;
		}

		bool is_computer() const
		{
			return is_computer(_s);
		}

		/*folder_path combine(LPCSTR part) const
		{			
			char8_t result[dif_max_path];
			strcpy_s(result, _s);
			if (!str::is_empty(part))
			{
				if (!is_path_sep(str::last_char(result)) && !is_path_sep(part[0])) strcat_s(result, "\\"sv);
				strcat_s(result, part);
			}
			return result;
		}*/

		/*folder_path combine(LPCWSTR w) const
		{
			return combine(str::utf16_to_utf8(w));
		}*/

		folder_path combine(const std::u8string_view part) const
		{
			auto result = std::u8string(_s.sv());

			if (!str::is_empty(part))
			{
				if (!is_path_sep(str::last_char(result)) && !is_path_sep(part[0])) result += '\\';
				result += part;
			}
			return folder_path(result);
		}

		folder_path combine(const std::wstring_view part) const
		{
			return combine(str::utf16_to_utf8(part));
		}

		/*file_path combine_file(LPCSTR part) const;		
		file_path combine_file(LPCWSTR part) const;
		file_path combine_file(const std::u8string_view part) const;*/
		file_path combine_file(std::u8string_view part) const;
		file_path combine_file(str::cached part) const;
		file_path combine_file(std::wstring_view part) const;

		int compare(const folder_path other) const
		{
			return icmp(_s, other._s);
		}

		constexpr std::u8string_view::size_type find_last_slash() const
		{
			return df::find_last_slash(_s);
		}

		str::cached name() const
		{
			const auto last_slash = find_last_slash();
			return is_root() ? _s : str::cache(_s.substr(last_slash + 1));
		}


		bool operator==(const folder_path other) const
		{
			return compare(other) == 0;
		}

		bool operator!=(const folder_path other) const
		{
			return compare(other) != 0;
		}

		bool operator<(const folder_path other) const
		{
			return compare(other) < 0;
		}

		/*uint32_t calc_hash() const
		{
			return str::hash_gen(_s).result();
		}*/

		friend class file_path;
	};


	class file_path
	{
	private:
		folder_path _folder;
		str::cached _name;

	public:
		file_path(const std::wstring_view sv) noexcept
		{
			un_pack(str::utf16_to_utf8(sv));
			assert_true(is_valid());
		}

		file_path(const std::u8string_view sv) noexcept
		{
			un_pack(sv);
			assert_true(is_valid());
		}

		file_path() noexcept = default;
		file_path(const file_path&) noexcept = default;
		file_path& operator=(const file_path&) noexcept = default;
		file_path(file_path&&) noexcept = default;
		file_path& operator=(file_path&&) noexcept = default;

		file_path(const folder_path folder, const std::u8string_view name_in,
		          const std::u8string_view ext_in) : _folder(folder)
		{
			auto name = std::u8string(name_in);
			if (ext_in.front() != '.' && name.back() != '.') name += '.';
			name += ext_in;

			_name = str::cache(name);
			assert_true(is_valid());
		}

		explicit file_path(const folder_path folder) : _folder(folder)
		{
			assert_true(is_valid());
		}

		file_path(const folder_path folder, const std::u8string_view name) : _folder(folder), _name(str::cache(name))
		{
			assert_true(is_valid());
		}

		file_path(const folder_path folder, const str::cached name) : _folder(folder), _name(name)
		{
			assert_true(is_valid());
		}

		void un_pack(const std::u8string_view path)
		{
			const auto last_slash = find_last_slash(path);

			if (last_slash != std::u8string_view::npos && path.size() > last_slash && last_slash > 0)
			{
				_name = str::cache(path.substr(last_slash + 1));
				_folder = folder_path(path.substr(0, last_slash));
			}
			else
			{
				_folder = folder_path(path);
			}
		}

		std::u8string pack() const
		{
			auto result = std::u8string(_folder.text());
			if (!is_path_sep(last_char(_folder.text()))) result += '\\';
			result += _name;
			return result;
		}

		void clear()
		{
			_folder.clear();
			_name.clear();
		}

		bool is_empty() const
		{
			return _folder.is_empty() && str::is_empty(_name);
		}

		int icmp(const file_path other) const
		{
			const auto diff = _folder.compare(other._folder);
			return diff == 0 ? str::icmp(_name, other._name) : diff;
		}

		bool operator==(const file_path other) const
		{
			return icmp(other) == 0;
		}

		bool operator!=(const file_path other) const
		{
			return icmp(other) != 0;
		}

		bool exists() const
		{
			return !is_empty() && platform::exists(*this);
		}

		bool is_link() const
		{
			return is_link(_name);
		}

		file_path resolve_link() const
		{
			return is_link() ? platform::resolve_link(*this) : *this;
		}

		static bool is_original(const std::u8string_view name)
		{
			return str::contains(name, u8".original."sv);
		}

		bool is_original() const
		{
			return is_original(_name);
		}

		static bool is_link(const std::u8string_view path)
		{
			return str::ends(path, u8".lnk"sv);
		}

		std::u8string_view file_name_without_extension() const
		{
			const std::u8string_view sv = _name;
			return sv.substr(0, find_ext(sv));
		}

		file_path extension(const std::u8string_view ext) const
		{
			return file_path(_folder, file_name_without_extension(), ext);
		}

		std::u8string_view extension() const
		{
			const std::u8string_view sv = _name;
			return sv.substr(find_ext(sv));
		}

		const str::cached name() const
		{
			return _name;
		}

		const folder_path folder() const
		{
			return _folder;
		}

		void folder(const folder_path f)
		{
			_folder = f;
		};

		bool is_valid() const
		{
			if (is_empty())
				return true;

			if (!_folder.is_empty() && _name.is_empty())
			{
				return true;
			}

			if (_folder.is_empty() || _name.is_empty())
			{
				return false;
			}

			return _folder.is_valid();
		}

		bool is_save_valid() const
		{
			if (is_empty())
				return false;

			if (!is_valid())
				return false;

			return !is_illegal_name(_name);
		}

		std::u8string str() const
		{
			return pack();
		}

		bool operator<(const file_path other) const
		{
			return icmp(other) < 0;
		}

		/*uint32_t calc_hash() const
		{
			return crypto::hash_gen(_folder.text()).append(_name).result();
		}*/
	};

	inline file_path folder_path::combine_file(const std::u8string_view part) const
	{
		return file_path(*this, part);
	}

	inline file_path folder_path::combine_file(const str::cached part) const
	{
		return file_path(*this, part);
	}

	static std::u8string combine_paths(__in const folder_paths& paths, const std::u8string_view join = u8" "sv,
	                                   __in bool quote = true)
	{
		std::u8string result;
		for (const auto& s : paths) str::join(result, s.text(), join, quote);
		return result;
	}

	file_path probe_data_file(std::u8string_view file_name);
}
