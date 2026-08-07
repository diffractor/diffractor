// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Settings persistence backends (registry and INI file) and backend selection.
// Also stores the graphics crash-guard flags used to fall back after a GPU/HW-decode crash.
// Binary data is encoded using base64.

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
	explicit setting_file_ini(const df::folder_path folder)
	{
		// Create the INI file path in the same folder as the database
		const auto ini_file_path = folder.combine_file("diffractor.ini");
		_ini_path = str::utf8_to_utf16(ini_file_path.str());
		_root_created = !platform::exists(ini_file_path);

		// Ensure the folder exists
		if (!folder.exists())
		{
			platform::create_folder(folder);
		}
	}

	bool root_created() const override
	{
		return _root_created;
	}

	bool write(const std::string_view section, const std::string_view name, const uint32_t v) override
	{
		return write(section, name, str::to_string(v));
	}

	bool write(const std::string_view section, const std::string_view name, const uint64_t v) override
	{
		return write(section, name, str::to_string(v));
	}

	bool write(const std::string_view section, const std::string_view name, const std::string_view v) override
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

	bool write(const std::string_view section, const std::string_view name, const df::cspan cs) override
	{
		// Encode binary data as base64
		const auto encoded = base64_encode(cs.data, cs.size);
		return write(section, name, encoded);
	}

	bool read(const std::string_view section, const std::string_view name, uint32_t& v) const override
	{
		std::string str_val;
		if (read(section, name, str_val))
		{
			v = str::to_uint(str_val);
			return true;
		}
		return false;
	}

	bool read(const std::string_view section, const std::string_view name, uint64_t& v) const override
	{
		std::string str_val;
		if (read(section, name, str_val))
		{
			// Parse uint64 from string
			v = std::strtoull(std::bit_cast<const char*>(str_val.data()), nullptr, 10);
			return true;
		}
		return false;
	}

	bool read(const std::string_view section, const std::string_view name, std::string& v) const override
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

	bool read(const std::string_view section, const std::string_view name, uint8_t* data,
	          size_t& len) const override
	{
		std::string encoded;
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
	return create_ini_file_settings(known_path(known_folder::app_data));
}

platform::setting_file_ptr platform::create_ini_file_settings(const df::folder_path folder)
{
	return std::make_shared<setting_file_ini>(folder);
}

// Thread-safe decorator around a setting_file backend. The process shares a single settings
// store (see platform::settings) that is read/written from several threads - the UI thread
// (save_options), the video decode thread and D3D init (crash-guard / YUV fallback). The
// underlying registry backend caches HKEYs lazily and is not itself thread-safe, so every
// call is serialised here under one mutex.
class setting_file_synchronized final : public platform::setting_file
{
	platform::setting_file_ptr _inner;
	mutable platform::mutex _mutex;

public:
	explicit setting_file_synchronized(platform::setting_file_ptr inner) : _inner(std::move(inner))
	{
	}

	bool root_created() const override
	{
		platform::exclusive_lock lock(_mutex);
		return _inner->root_created();
	}

	bool write(const std::string_view section, const std::string_view name, const uint32_t v) override
	{
		platform::exclusive_lock lock(_mutex);
		return _inner->write(section, name, v);
	}

	bool write(const std::string_view section, const std::string_view name, const uint64_t v) override
	{
		platform::exclusive_lock lock(_mutex);
		return _inner->write(section, name, v);
	}

	bool write(const std::string_view section, const std::string_view name, const std::string_view v) override
	{
		platform::exclusive_lock lock(_mutex);
		return _inner->write(section, name, v);
	}

	bool write(const std::string_view section, const std::string_view name, const df::cspan cs) override
	{
		platform::exclusive_lock lock(_mutex);
		return _inner->write(section, name, cs);
	}

	bool read(const std::string_view section, const std::string_view name, uint32_t& v) const override
	{
		platform::exclusive_lock lock(_mutex);
		return _inner->read(section, name, v);
	}

	bool read(const std::string_view section, const std::string_view name, uint64_t& v) const override
	{
		platform::exclusive_lock lock(_mutex);
		return _inner->read(section, name, v);
	}

	bool read(const std::string_view section, const std::string_view name, std::string& v) const override
	{
		platform::exclusive_lock lock(_mutex);
		return _inner->read(section, name, v);
	}

	bool read(const std::string_view section, const std::string_view name, uint8_t* data, size_t& len) const override
	{
		platform::exclusive_lock lock(_mutex);
		return _inner->read(section, name, data, len);
	}
};

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

// Resolve the settings backend: INI by default, registry if a legacy key already exists (so
// existing users keep their settings). File-local because the process should go through
// platform::settings(), which builds the single shared, synchronised store from this.
static platform::setting_file_ptr create_default_settings()
{
#ifdef WINSTORE
	// A packaged build has registry access, but writes are virtualized into the package and
	// discarded on uninstall, so settings always live in the INI under app data.
	static std::atomic_bool logged_backend{false};
	if (!logged_backend.exchange(true)) df::log(__FUNCTION__, "settings backend: ini (store)");
	return platform::create_ini_file_settings();
#else
	// Prefer INI file settings, but use registry if it already exists
	// This provides migration path for existing users
	const auto use_registry = registry_key_exists();
	static std::atomic_bool logged_backend{false};
	if (!logged_backend.exchange(true))
		df::log(__FUNCTION__, use_registry ? "settings backend: registry" : "settings backend: ini");

	if (use_registry)
	{
		return platform::create_registry_settings();
	}
	return platform::create_ini_file_settings();
#endif
}

// The single, process-wide settings store. The backend is resolved once (registry vs INI) and
// wrapped so all access is synchronised. Everything that persists settings - the app's option
// load/save, the crash-guard flags and the graphics fallbacks - shares this one instance rather
// than repeatedly re-probing the backend and constructing throwaway stores.
platform::setting_file_ptr platform::settings()
{
	static const setting_file_ptr cached =
		std::make_shared<setting_file_synchronized>(create_default_settings());
	return cached;
}

static const char* crash_guard_name(const platform::crash_guard g)
{
	switch (g)
	{
	case platform::crash_guard::gpu_render: return "gpu_render_active";
	case platform::crash_guard::hw_video_decode: return "hw_decode_active";
	}
	return "";
}

static std::array<std::atomic_bool, 2> crash_guard_failures{};
static std::array<std::atomic_bool, 2> crash_guard_suppressions{};

static size_t crash_guard_index(const platform::crash_guard g)
{
	return static_cast<size_t>(g);
}

// Crash-guard flags live under a dedicated section and use the same shared store the app reads
// at startup (platform::settings), so the fallback works on both registry and INI installs.
// Writes happen at safe times, never from a crashing thread.
void platform::set_crash_guard(const crash_guard g, const bool active)
{
	const auto store = settings();
	if (!store) return;

	// A failed clear is the dangerous direction: the guard stays set, so the next start believes
	// the last run crashed and permanently drops to software rendering or software decode. Log it
	// so the cause is visible instead of presenting as unexplained slowness.
	if (!store->write("crash", crash_guard_name(g), active ? 1u : 0u) && !active)
	{
		df::log(__FUNCTION__, "Failed to clear crash guard");
	}
}

bool platform::read_crash_guard(const crash_guard g)
{
	const auto store = settings();
	uint32_t v = 0;
	if (store) store->read("crash", crash_guard_name(g), v);
	return v != 0;
}

void platform::fail_crash_guard(const crash_guard g)
{
	crash_guard_failures[crash_guard_index(g)] = true;
	set_crash_guard(g, true);
}

bool platform::crash_guard_failed(const crash_guard g)
{
	return crash_guard_failures[crash_guard_index(g)];
}

void platform::suppress_crash_guard(const crash_guard g, const bool suppress)
{
	crash_guard_suppressions[crash_guard_index(g)] = suppress;
}

bool platform::crash_guard_suppressed(const crash_guard g)
{
	return crash_guard_suppressions[crash_guard_index(g)];
}


class setting_file_impl : public platform::setting_file
{
	mutable df::hash_map<std::string, HKEY__*> _keys;
	HKEY _root_key = nullptr;
	bool _root_created = false;

public:
	setting_file_impl()
	{
		constexpr auto s_reg_key = "Software\\Diffractor";
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


	HKEY Key(const std::string_view section_in) const
	{
		if (str::is_empty(section_in))
		{
			return _root_key;
		}

		const auto section = std::string(section_in);
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

	static HKEY create_key(const HKEY parent_key, const std::string_view name, bool& was_created)
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

	bool read(const std::string_view section, const std::string_view name, uint32_t& v) const override
	{
		uint32_t value = 0;
		DWORD dwType = 0;
		DWORD s = sizeof(value);
		const int32_t result = RegQueryValueEx(Key(section), str::utf8_to_utf16(name).c_str(), nullptr, &dwType,
		                                       std::bit_cast<uint8_t*>(&value), &s);
		if (result != ERROR_SUCCESS || dwType != REG_DWORD || s != sizeof(value)) return false;
		v = value;
		return true;
	}

	bool read(const std::string_view section, const std::string_view name, uint64_t& v) const override
	{
		uint64_t value = 0;
		DWORD dwType = 0;
		DWORD s = sizeof(value);
		const int32_t result = RegQueryValueEx(Key(section), str::utf8_to_utf16(name).c_str(), nullptr, &dwType,
		                                       std::bit_cast<uint8_t*>(&value), &s);
		if (result != ERROR_SUCCESS || dwType != REG_QWORD || s != sizeof(value)) return false;
		v = value;
		return true;
	}


	bool read(const std::string_view section, const std::string_view name, std::string& v) const override
	{
		DWORD alloc_len = 0;
		DWORD type = 0;
		const auto nameW = str::utf8_to_utf16(name);

		int32_t result = RegQueryValueEx(Key(section), nameW.c_str(), nullptr, &type, nullptr, &alloc_len);

		if (result != ERROR_SUCCESS || type != REG_SZ || alloc_len % sizeof(wchar_t) != 0) return false;

		std::vector<wchar_t> data(alloc_len / sizeof(wchar_t) + 1, L'\0');
		result = RegQueryValueEx(Key(section), nameW.c_str(), nullptr, &type,
		                         std::bit_cast<uint8_t*>(data.data()), &alloc_len);

		if (result == ERROR_SUCCESS && type == REG_SZ && alloc_len % sizeof(wchar_t) == 0)
		{
			auto char_len = alloc_len / sizeof(wchar_t);
			if (char_len > 0 && data[char_len - 1] == L'\0') --char_len;
			v = str::utf16_to_utf8({data.data(), char_len});
			return true;
		}

		return false;
	}

	bool read(const std::string_view section, const std::string_view name, uint8_t* data,
	          size_t& len) const override
	{
		if (len > MAXDWORD) return false;
		DWORD dwType = 0;
		DWORD dwSize = static_cast<DWORD>(len);
		bool success = false;

		if (ERROR_SUCCESS == RegQueryValueEx(Key(section), str::utf8_to_utf16(name).c_str(), nullptr, &dwType, data,
		                                     &dwSize) && dwType == REG_BINARY)
		{
			len = dwSize;
			success = true;
		}

		return success;
	}

	bool write(const std::string_view section, const std::string_view name, const uint32_t v) override
	{
		return ERROR_SUCCESS == RegSetValueEx(Key(section), str::utf8_to_utf16(name).c_str(), 0, REG_DWORD,
		                                      std::bit_cast<const uint8_t*>(&v), sizeof(uint32_t));
	}

	bool write(const std::string_view section, const std::string_view name, const uint64_t v) override
	{
		return ERROR_SUCCESS == RegSetValueEx(Key(section), str::utf8_to_utf16(name).c_str(), 0, REG_QWORD,
		                                      std::bit_cast<const uint8_t*>(&v), sizeof(uint64_t));
	}

	bool write(const std::string_view section, const std::string_view name, const std::string_view v) override
	{
		const auto w = str::utf8_to_utf16(v);
		return ERROR_SUCCESS == RegSetValueEx(Key(section), str::utf8_to_utf16(name).c_str(), 0, REG_SZ,
		                                      std::bit_cast<const uint8_t*>(w.c_str()),
		                                      static_cast<uint32_t>((w.size() + 1) * sizeof(wchar_t)));
	}

	bool write(const std::string_view section, const std::string_view name, const df::cspan data) override
	{
		return ERROR_SUCCESS == RegSetValueEx(Key(section), str::utf8_to_utf16(name).c_str(), 0, REG_BINARY, data.data,
		                                      static_cast<uint32_t>(data.size));
	}
};

platform::setting_file_ptr platform::create_registry_settings()
{
	return std::make_shared<setting_file_impl>();
}
