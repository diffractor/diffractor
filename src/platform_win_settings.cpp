// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2025  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: INI file-based settings persistence. Provides an alternative to registry-based
// settings storage using Windows INI file APIs. Binary data is encoded using base64.

#include "pch.h"
#include "platform_win.h"
#include "util_base64.h"

// INI file implementation of setting_file interface
// Stores settings in an INI file in the same folder as the database
class setting_file_ini final : public platform::setting_file
{
	std::wstring _ini_path;
	bool _root_created = false;

public:
	explicit setting_file_ini(df::folder_path folder)
	{
		// Create the INI file path in the same folder as the database
		const auto ini_file_path = folder.combine_file(u8"diffractor.ini"sv);
		_ini_path = str::utf8_to_utf16(ini_file_path.str());

		// Check if the file exists or can be created
		_root_created = platform::exists(ini_file_path) || folder.exists();

		// Ensure the folder exists
		if (!folder.exists())
		{
			platform::create_folder(folder);
			_root_created = folder.exists();
		}
	}

	bool root_created() const override
	{
		return _root_created;
	}

	bool write(std::u8string_view section, std::u8string_view name, uint32_t v) override
	{
		return write(section, name, str::to_string(v));
	}

	bool write(std::u8string_view section, std::u8string_view name, uint64_t v) override
	{
		return write(section, name, str::to_string(v));
	}

	bool write(std::u8string_view section, std::u8string_view name, std::u8string_view v) override
	{
		const auto section_w = str::utf8_to_utf16(section);
		const auto name_w = str::utf8_to_utf16(name);
		const auto value_w = str::utf8_to_utf16(v);

		return WritePrivateProfileStringW(
			section_w.c_str(),
			name_w.c_str(),
			value_w.c_str(),
			_ini_path.c_str()) != 0;
	}

	bool write(std::u8string_view section, std::u8string_view name, df::cspan cs) override
	{
		// Encode binary data as base64
		const auto encoded = base64_encode(cs.data, cs.size);
		return write(section, name, encoded);
	}

	bool read(std::u8string_view section, std::u8string_view name, uint32_t& v) const override
	{
		std::u8string str_val;
		if (read(section, name, str_val))
		{
			v = str::to_uint(str_val);
			return true;
		}
		return false;
	}

	bool read(std::u8string_view section, std::u8string_view name, uint64_t& v) const override
	{
		std::u8string str_val;
		if (read(section, name, str_val))
		{
			// Parse uint64 from string
			v = std::strtoull(std::bit_cast<const char*>(str_val.data()), nullptr, 10);
			return true;
		}
		return false;
	}

	bool read(std::u8string_view section, std::u8string_view name, std::u8string& v) const override
	{
		const auto section_w = str::utf8_to_utf16(section);
		const auto name_w = str::utf8_to_utf16(name);

		// Start with a reasonable buffer size
		std::vector<wchar_t> buffer(1024);

		while (true)
		{
			const DWORD result = GetPrivateProfileStringW(
				section_w.c_str(),
				name_w.c_str(),
				L"",
				buffer.data(),
				static_cast<DWORD>(buffer.size()),
				_ini_path.c_str());

			// If the result is less than buffer size - 1, we got all the data
			if (result < buffer.size() - 1)
			{
				if (result == 0)
				{
					// Check if the key exists or if it's just empty
					const DWORD error = GetLastError();
					if (error != ERROR_SUCCESS && error != ERROR_FILE_NOT_FOUND)
					{
						return false;
					}
					// Key doesn't exist
					return false;
				}

				v = str::utf16_to_utf8(std::wstring_view(buffer.data(), result));
				return true;
			}

			// Buffer was too small, double it and try again
			buffer.resize(buffer.size() * 2);
			if (buffer.size() > 1024 * 1024) // Safety limit of 1MB
			{
				return false;
			}
		}
	}

	bool read(std::u8string_view section, std::u8string_view name, uint8_t* data, size_t& len) const override
	{
		std::u8string encoded;
		if (read(section, name, encoded))
		{
			const auto decoded = base64_decode(encoded);
			if (decoded.size() <= len)
			{
				std::memcpy(data, decoded.data(), decoded.size());
				len = decoded.size();
				return true;
			}
		}
		len = 0;
		return false;
	}
};

platform::setting_file_ptr platform::create_ini_file_settings()
{
	const auto app_data_folder = known_path(platform::known_folder::app_data);
	return std::make_shared<setting_file_ini>(app_data_folder);
}

// Helper function to check if the Diffractor registry key exists
static bool registry_key_exists()
{
	HKEY hKey = nullptr;
	const auto result = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Diffractor", 0, KEY_READ, &hKey);
	if (result == ERROR_SUCCESS)
	{
		RegCloseKey(hKey);
		return true;
	}
	return false;
}

platform::setting_file_ptr platform::create_default_settings()
{
#ifdef WINSTORE
	// Windows Store apps must use INI file settings (no registry access)
	return create_ini_file_settings();
#else
	// Prefer INI file settings, but use registry if it already exists
	// This provides migration path for existing users
	if (registry_key_exists())
	{
		return create_registry_settings();
	}
	return create_ini_file_settings();
#endif
}


class setting_file_impl : public platform::setting_file
{
	mutable df::hash_map<std::u8string, HKEY__*> _keys;
	HKEY _root_key = nullptr;
	bool _root_created = false;

public:
	setting_file_impl()
	{
		constexpr auto s_reg_key = u8"Software\\Diffractor"sv;
		_root_key = create_key(HKEY_CURRENT_USER, s_reg_key, _root_created);
	}

	~setting_file_impl()
	{
		close();
	}

	bool root_created() const override
	{
		return _root_created;
	}


	HKEY Key(const std::u8string_view section_in) const
	{
		if (str::is_empty(section_in))
		{
			return _root_key;
		}

		const auto section = std::u8string(section_in);
		const auto found = _keys.find(section);

		if (found != _keys.end())
		{
			return found->second;
		}

		auto was_created = false;
		auto* const new_key = create_key(_root_key, section, was_created);

		if (new_key != nullptr)
		{
			_keys[section] = new_key;
			return new_key;
		}

		return nullptr;
	}

	void close()
	{
		for (const auto& k : _keys)
		{
			RegCloseKey(k.second);
		}

		_keys.clear();
		RegCloseKey(_root_key);
		_root_key = nullptr;
	}

	static HKEY create_key(const HKEY parent_key, const std::u8string_view name, bool& was_created)
	{
		df::assert_true(parent_key != nullptr);

		DWORD disposition = 0;
		HKEY result_key = nullptr;
		const auto result = RegCreateKeyEx(parent_key, str::utf8_to_utf16(name).c_str(), 0, REG_NONE,
			REG_OPTION_NON_VOLATILE,
			KEY_ALL_ACCESS, nullptr, &result_key, &disposition);
		was_created = disposition == REG_CREATED_NEW_KEY;
		return result == ERROR_SUCCESS ? result_key : nullptr;
	}

	bool read(const std::u8string_view section, const std::u8string_view name, uint32_t& v) const override
	{
		DWORD dwType = 0;
		DWORD s = sizeof(uint32_t);
		const int32_t result = RegQueryValueEx(Key(section), str::utf8_to_utf16(name).c_str(), nullptr, &dwType,
			std::bit_cast<uint8_t*>(&v), &s);
		df::assert_true(result != ERROR_SUCCESS || dwType == REG_DWORD);
		df::assert_true(result != ERROR_SUCCESS || s == sizeof(uint32_t));
		return ERROR_SUCCESS == result;
	}

	bool read(const std::u8string_view section, const std::u8string_view name, uint64_t& v) const override
	{
		DWORD dwType = 0;
		DWORD s = sizeof(uint64_t);
		const int32_t result = RegQueryValueEx(Key(section), str::utf8_to_utf16(name).c_str(), nullptr, &dwType,
			std::bit_cast<uint8_t*>(&v), &s);
		df::assert_true(result != ERROR_SUCCESS || dwType == REG_QWORD);
		df::assert_true(result != ERROR_SUCCESS || s == sizeof(uint64_t));
		return ERROR_SUCCESS == result;
	}


	bool read(const std::u8string_view section, const std::u8string_view name, std::u8string& v) const override
	{
		DWORD alloc_len = 0;
		DWORD type = 0;
		const auto nameW = str::utf8_to_utf16(name);

		int32_t result = RegQueryValueEx(Key(section), nameW.c_str(), nullptr, &type, nullptr, &alloc_len);

		if (result == ERROR_SUCCESS && (type == REG_SZ || type == REG_MULTI_SZ || type == REG_EXPAND_SZ))
		{
			std::vector<uint8_t> data(alloc_len, 0);
			result = RegQueryValueEx(Key(section), nameW.c_str(), nullptr, &type, data.data(), &alloc_len);

			if (result == ERROR_SUCCESS)
			{
				const auto char_len = alloc_len >= sizeof(wchar_t) ? alloc_len / sizeof(wchar_t) - 1 : 0;
				v = str::utf16_to_utf8({ std::bit_cast<const wchar_t*>(data.data()), char_len });
			}
		}

		return ERROR_SUCCESS == result;
	}

	bool read(const std::u8string_view section, const std::u8string_view name, uint8_t* data,
		size_t& len) const override
	{
		DWORD dwType = REG_BINARY;
		DWORD dwSize = static_cast<uint32_t>(len);
		bool success = false;

		if (ERROR_SUCCESS == RegQueryValueEx(Key(section), str::utf8_to_utf16(name).c_str(), nullptr, &dwType, data,
			&dwSize))
		{
			len = dwSize;
			success = true;
		}

		return success;
	}

	bool write(const std::u8string_view section, const std::u8string_view name, const uint32_t v) override
	{
		return ERROR_SUCCESS == RegSetValueEx(Key(section), str::utf8_to_utf16(name).c_str(), 0, REG_DWORD,
			std::bit_cast<const uint8_t*>(&v), sizeof(uint32_t));
	}

	bool write(const std::u8string_view section, const std::u8string_view name, const uint64_t v) override
	{
		return ERROR_SUCCESS == RegSetValueEx(Key(section), str::utf8_to_utf16(name).c_str(), 0, REG_QWORD,
			std::bit_cast<const uint8_t*>(&v), sizeof(uint64_t));
	}

	bool write(const std::u8string_view section, const std::u8string_view name, const std::u8string_view v) override
	{
		const auto w = str::utf8_to_utf16(v);
		return ERROR_SUCCESS == RegSetValueEx(Key(section), str::utf8_to_utf16(name).c_str(), 0, REG_SZ,
			std::bit_cast<const uint8_t*>(w.c_str()),
			static_cast<uint32_t>(w.size() * sizeof(wchar_t)));
	}

	bool write(const std::u8string_view section, const std::u8string_view name, const df::cspan data) override
	{
		return ERROR_SUCCESS == RegSetValueEx(Key(section), str::utf8_to_utf16(name).c_str(), 0, REG_BINARY, data.data,
			static_cast<uint32_t>(data.size));
	}
};

platform::setting_file_ptr platform::create_registry_settings()
{
	return std::make_shared<setting_file_impl>();
}