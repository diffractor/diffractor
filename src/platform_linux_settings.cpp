// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Linux settings store. There is no registry to choose between, so the INI backend that
// platform_win_settings.cpp offers as an alternative is the only one here.

#include "pch.h"

#include "util_base64.h"

#include <fcntl.h>
#include <unistd.h>

namespace
{
	constexpr std::string_view settings_file_name = "diffractor.ini";
	platform::linux_settings_test_failure settings_test_failure = platform::linux_settings_test_failure::none;
	int settings_save_count = 0;

	std::optional<std::pair<df::file_path, int>> open_settings_stage(const df::folder_path folder)
	{
		for (auto attempt = 0; attempt < 64; ++attempt)
		{
			uint32_t r = 0;
			platform::generate_random_bytes(std::bit_cast<uint8_t*>(&r), sizeof(r));

			const auto stage = folder.combine_file(std::format(".diffractor-ini-{:08x}.tmp", r));
			const auto fd = ::open(stage.str().c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
			if (fd >= 0) return std::pair{stage, fd};
			if (errno != EEXIST) return {};
		}

		return {};
	}

	bool write_all(const int fd, const std::string_view text)
	{
		size_t written = 0;
		while (written < text.size())
		{
			const auto n = ::write(fd, text.data() + written, text.size() - written);
			if (n < 0)
			{
				if (errno == EINTR) continue;
				return false;
			}
			if (n == 0) return false;
			written += static_cast<size_t>(n);
		}
		return true;
	}

	// Values are held in memory and the whole file is rewritten on change: it is a few kilobytes,
	// and a full rewrite cannot leave a half-updated section behind.
	class ini_settings final : public platform::setting_file
	{
	public:
		explicit ini_settings(df::folder_path folder) : _path(folder.combine_file(settings_file_name))
		{
			// Answers whether this run is starting a NEW settings root, so it is decided before the
			// file is read and never revised by a later write.
			_root_created = !platform::exists(_path);
			load();
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
			const auto key = key_of(section, name);
			const auto value = std::string(v);
			if (const auto found = _values.find(key); found != _values.end() && found->second == value) return true;

			auto candidate = _values;
			candidate[key] = value;

			if (!save(candidate)) return false;

			_values = std::move(candidate);
			return true;
		}

		bool write(const std::string_view section, const std::string_view name, const df::cspan cs) override
		{
			return write(section, name, base64_encode(std::string_view(std::bit_cast<const char*>(cs.data), cs.size)));
		}

		bool read(const std::string_view section, const std::string_view name, uint32_t& v) const override
		{
			std::string text;
			if (!read(section, name, text)) return false;
			v = static_cast<uint32_t>(str::to_int64(text));
			return true;
		}

		bool read(const std::string_view section, const std::string_view name, uint64_t& v) const override
		{
			std::string text;
			if (!read(section, name, text)) return false;
			v = static_cast<uint64_t>(str::to_int64(text));
			return true;
		}

		bool read(const std::string_view section, const std::string_view name, std::string& v) const override
		{
			const auto found = _values.find(key_of(section, name));
			if (found == _values.end()) return false;
			v = found->second;
			return true;
		}

		bool read(const std::string_view section, const std::string_view name, uint8_t* data,
		          size_t& len) const override
		{
			std::string text;
			if (!read(section, name, text)) return false;

			const auto decoded = base64_decode(text);
			if (decoded.size() > len) return false;

			std::memcpy(data, decoded.data(), decoded.size());
			len = decoded.size();
			return true;
		}

	private:
		static std::string key_of(const std::string_view section, const std::string_view name)
		{
			return std::format("{}/{}", section, name);
		}

		void load()
		{
			std::ifstream file(platform::to_stream_path(_path));
			if (!file.is_open()) return;

			std::string line;
			std::string section;

			while (std::getline(file, line))
			{
				while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
				if (line.empty() || line.front() == ';' || line.front() == '#') continue;

				if (line.front() == '[' && line.back() == ']')
				{
					section = line.substr(1, line.size() - 2);
					continue;
				}

				const auto equals = line.find('=');
				if (equals == std::string::npos) continue;

				_values[key_of(section, line.substr(0, equals))] = line.substr(equals + 1);
			}
		}

		bool save(const std::map<std::string, std::string>& values)
		{
			++settings_save_count;
			const auto folder = _path.folder();
			if (!platform::exists(folder)) platform::create_folder(folder);

			// Grouped by section so the file stays readable and diffable by hand.
			std::map<std::string, std::vector<std::pair<std::string, std::string>>> sections;

			for (const auto& [key, value] : values)
			{
				const auto slash = key.find('/');
				if (slash == std::string::npos) continue;
				sections[key.substr(0, slash)].emplace_back(key.substr(slash + 1), value);
			}

			std::string text;
			for (const auto& [section, entries] : sections)
			{
				text += std::format("[{}]\n", section);
				for (const auto& [name, value] : entries) text += std::format("{}={}\n", name, value);
				text += '\n';
			}

			const auto stage_file = open_settings_stage(folder);
			if (!stage_file.has_value()) return false;
			const auto [stage, fd] = *stage_file;

			if (settings_test_failure == platform::linux_settings_test_failure::after_open)
			{
				::close(fd);
				platform::delete_file(stage);
				return false;
			}

			const auto wrote = write_all(fd, text);

			if (!wrote || settings_test_failure == platform::linux_settings_test_failure::after_write)
			{
				::close(fd);
				platform::delete_file(stage);
				return false;
			}

			if (::close(fd) != 0)
			{
				platform::delete_file(stage);
				return false;
			}

			if (settings_test_failure == platform::linux_settings_test_failure::before_replace)
			{
				platform::delete_file(stage);
				return false;
			}

			if (platform::test_linux_before_settings_replace) platform::test_linux_before_settings_replace();

			const auto replaced = platform::replace_file(_path, stage);
			if (replaced.failed()) platform::delete_file(stage);
			return replaced.success();
		}

		df::file_path _path;
		std::map<std::string, std::string> _values;
		bool _root_created = false;
	};
}

platform::setting_file_ptr platform::create_ini_file_settings()
{
	return create_ini_file_settings(known_path(known_folder::app_data));
}

platform::setting_file_ptr platform::create_ini_file_settings(const df::folder_path folder)
{
	return std::make_shared<ini_settings>(folder);
}

void platform::test_linux_settings_failure(const linux_settings_test_failure failure)
{
	settings_test_failure = failure;
}

int platform::test_linux_settings_save_count()
{
	return settings_save_count;
}

void platform::test_linux_reset_settings_save_count()
{
	settings_save_count = 0;
}

std::function<void()> platform::test_linux_before_settings_replace;
