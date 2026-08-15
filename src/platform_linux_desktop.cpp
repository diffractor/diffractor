// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Stand-ins for the Windows desktop integration surface. docs/linux.md lists which of
// these map to an XDG portal, which map to nothing, and which are product decisions rather than
// ports -- the Recycle Bin in particular, because recoverable deletion is a design promise.
//
// Every entry answers what the caller already treats as "not available", so nothing can mistake a
// stub for a result. A capability query answers false, which is what keeps the commands that
// depend on it out of the interface.

#include "pch.h"

#include "av_sound.h"
#include "model.h"
#include "test_runner.h"

#include <unistd.h>

///////////////////////////////////////////////////////////////////////////////////////////////////
// Shell verbs. Some map to XDG portals; several have no counterpart.
///////////////////////////////////////////////////////////////////////////////////////////////////

std::string platform::OS()
{
	return "Linux";
}

bool platform::has_burner()
{
	return false;
}

bool platform::burn_to_cd(const std::vector<df::file_path>&, const std::vector<df::folder_path>&)
{
	return false;
}

void platform::print(const std::vector<df::file_path>&, const std::vector<df::folder_path>&)
{
}

void platform::set_desktop_wallpaper(df::file_path)
{
}

void platform::show_file_properties(const std::vector<df::file_path>&, const std::vector<df::folder_path>&)
{
}

void platform::show_in_file_browser(df::file_path)
{
}

void platform::show_in_file_browser(df::folder_path)
{
}

std::vector<platform::open_with_entry> platform::assoc_handlers(std::string_view)
{
	return {};
}

platform::scan_result platform::scan(df::folder_path)
{
	return {};
}

bool platform::eject(df::folder_path)
{
	return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Deletion and file operations.
//
// can_recycle answers false, so the interface presents deletion as permanent rather than implying
// a recovery that does not exist here.
///////////////////////////////////////////////////////////////////////////////////////////////////

bool platform::can_recycle(const std::vector<df::file_path>&, const std::vector<df::folder_path>&)
{
	return false;
}

namespace
{
	// The shell appends " (2)", " (3)" and so on to a colliding name. Matching that keeps a
	// collection that has moved between the two platforms looking like one collection.
	constexpr int max_collision_attempts = 1000;

	df::file_path next_free_file(const df::folder_path folder, const std::string_view name)
	{
		if (const auto first = folder.combine_file(name); !platform::exists(first)) return first;

		const auto extension_at = df::find_ext(name);
		const auto stem = name.substr(0, extension_at);
		const auto extension = name.substr(extension_at);

		for (auto attempt = 2; attempt < max_collision_attempts; ++attempt)
		{
			const auto candidate = folder.combine_file(std::format("{} ({}){}", stem, attempt, extension));
			if (!platform::exists(candidate)) return candidate;
		}

		return {};
	}

	df::folder_path next_free_folder(const df::folder_path parent, const std::string_view name)
	{
		if (const auto first = parent.combine(name); !platform::exists(first)) return first;

		for (auto attempt = 2; attempt < max_collision_attempts; ++attempt)
		{
			const auto candidate = parent.combine(std::format("{} ({})", name, attempt));
			if (!platform::exists(candidate)) return candidate;
		}

		return {};
	}

	platform::file_op_result copy_folder_contents(const df::folder_path source, const df::folder_path destination)
	{
		if (const auto created = platform::create_folder(destination); created.failed()) return created;

		// Hidden entries are part of the folder whatever the browser shows, so a copy that dropped
		// them would be a quietly incomplete copy.
		const auto contents = platform::iterate_file_items(source, true);

		for (const auto& file : contents.files)
		{
			const auto result = platform::copy_file(source.combine_file(file.name),
			                                        destination.combine_file(file.name), false, false);
			if (result.failed()) return result;
		}

		for (const auto& folder : contents.folders)
		{
			const auto result = copy_folder_contents(source.combine(folder.name), destination.combine(folder.name));
			if (result.failed()) return result;
		}

		return {platform::file_op_result_code::OK};
	}

	platform::file_op_result delete_folder_contents(const df::folder_path folder)
	{
		const auto contents = platform::iterate_file_items(folder, true);

		for (const auto& file : contents.files)
		{
			const auto result = platform::delete_file(folder.combine_file(file.name));
			if (result.failed()) return result;
		}

		for (const auto& child : contents.folders)
		{
			const auto result = delete_folder_contents(folder.combine(child.name));
			if (result.failed()) return result;
		}

		// The interface has no delete_folder: on Windows a folder only ever leaves through the shell.
		if (::rmdir(std::string(folder.text()).c_str()) != 0)
		{
			return {platform::file_op_result_code::FAILED, std::string(::strerror(errno))};
		}

		return {platform::file_op_result_code::OK};
	}
}

platform::file_op_result platform::delete_items(const std::vector<df::file_path>& files,
                                                const std::vector<df::folder_path>& folders, bool)
{
	// The undo flag is not honoured because can_recycle already answered false, so every caller has
	// been told this delete is permanent before asking for it.
	for (const auto& file : files)
	{
		if (const auto result = delete_file(file); result.failed()) return result;
	}

	for (const auto& folder : folders)
	{
		if (const auto result = delete_folder_contents(folder); result.failed()) return result;
	}

	return {file_op_result_code::OK};
}

platform::file_op_result platform::move_or_copy(const std::vector<df::file_path>& files,
                                                const std::vector<df::folder_path>& folders,
                                                const df::folder_path target, const bool is_move,
                                                const bool replace_existing)
{
	if (const auto created = create_folder(target); created.failed()) return created;

	file_op_result result{file_op_result_code::OK};

	for (const auto& file : files)
	{
		const auto destination = replace_existing
			                         ? target.combine_file(file.name())
			                         : next_free_file(target, file.name());

		if (destination.is_empty())
		{
			return {file_op_result_code::FAILED, std::format("Could not find a free name for {}", file.str())};
		}

		const auto copied = copy_file(file, destination, false, false);
		if (copied.failed()) return copied;

		if (is_move)
		{
			if (const auto removed = delete_file(file); removed.failed()) return removed;
		}

		result.created_files.files.emplace_back(destination);
	}

	for (const auto& folder : folders)
	{
		const auto destination = replace_existing
			                         ? target.combine(folder.name())
			                         : next_free_folder(target, folder.name());

		if (destination.is_empty())
		{
			return {file_op_result_code::FAILED, std::format("Could not find a free name for {}", folder.text())};
		}

		const auto copied = copy_folder_contents(folder, destination);
		if (copied.failed()) return copied;

		if (is_move)
		{
			if (const auto removed = delete_folder_contents(folder); removed.failed()) return removed;
		}

		result.created_files.folders.emplace_back(destination);
	}

	return result;
}

platform::file_op_result platform::replacement_flush_result(const bool flushed, std::string error_message)
{
	return flushed
		       ? file_op_result{file_op_result_code::OK}
		       : file_op_result{file_op_result_code::FAILED, std::move(error_message)};
}

df::folder_path platform::temp_folder()
{
	return known_path(known_folder::app_cache_data);
}

df::file_path platform::running_app_path()
{
	return known_path(known_folder::running_app_folder).combine_file("diffractor");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Shell metadata. There are no property handlers here, so the app's own pipeline covers these.
///////////////////////////////////////////////////////////////////////////////////////////////////

platform::metadata_result platform::read_shell_metadata(df::file_path)
{
	return {};
}

platform::file_op_result platform::write_shell_tags(df::file_path, const std::vector<std::string>&)
{
	return {file_op_result_code::FAILED, "shell tags are not available in this build"};
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Dialogs, clipboard and drag. A toolkit supplies these; none is linked yet.
///////////////////////////////////////////////////////////////////////////////////////////////////

bool platform::browse_for_folder(df::folder_path&)
{
	return false;
}

bool platform::prompt_for_save_path(df::file_path&)
{
	return false;
}

platform::clipboard_data_ptr platform::clipboard()
{
	return {};
}

std::string platform::clipboard_text()
{
	return {};
}

void platform::set_clipboard(const std::vector<df::file_path>&, const std::vector<df::folder_path>&,
                             const file_load_result&, bool)
{
}

void platform::set_clipboard(std::string_view)
{
}

platform::drop_effect platform::perform_drag(const std::any&, const std::vector<df::file_path>&,
                                             const std::vector<df::folder_path>&)
{
	return drop_effect::none;
}

void platform::show_startup_failure(const std::string_view message)
{
	// No message box: report on stderr so a headless run still says why it stopped.
	std::fprintf(stderr, "diffractor: %.*s\n", static_cast<int>(message.size()), message.data());
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Network and mail.
///////////////////////////////////////////////////////////////////////////////////////////////////

bool platform::is_online()
{
	return false;
}

platform::web_host_ptr platform::connect_to_host(std::string_view, bool, int)
{
	return {};
}

platform::web_response platform::send_request(const web_host_ptr&, const web_request&)
{
	return {};
}

void platform::download_and_verify(const std::function<void(df::file_path)>&)
{
}

platform::file_op_result platform::install(df::file_path, df::folder_path, bool, bool)
{
	return {file_op_result_code::FAILED, "install is not available in this build"};
}

platform::mapi_send_result platform::mapi_send(std::string_view, std::string_view, std::string_view,
                                               const attachments_t&)
{
	return mapi_send_result::failed;
}

platform::mapi_send_result platform::classify_mapi_send_result(uint32_t)
{
	return mapi_send_result::failed;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Process and thread services.
///////////////////////////////////////////////////////////////////////////////////////////////////

platform::thread_init::thread_init() = default;
platform::thread_init::~thread_init() = default;
platform::media_thread_priority::media_thread_priority() = default;
platform::media_thread_priority::~media_thread_priority() = default;

void platform::set_thread_description(const std::string_view name)
{
	// pthread_setname_np truncates at 16 bytes including the terminator.
	char buffer[16] = {};
	const auto len = std::min(name.size(), sizeof(buffer) - 1);
	std::memcpy(buffer, name.data(), len);
	::pthread_setname_np(::pthread_self(), buffer);
}

bool platform::memory_usage(memory_usage_t& result)
{
	result = {};
	return false;
}

int platform::display_frequency()
{
	return 60;
}

std::atomic<size_t> platform::static_memory_usage = 0;

// The seam tests use to simulate a cloud placeholder. Empty means "ask the filesystem", which is
// all this build can do.
std::function<bool(const df::file_path&)> platform::test_offline_predicate;

int ui::ticks_since_last_user_action = 0;

ui::key_state ui::current_key_state()
{
	return {};
}

std::string_view keys::format(int)
{
	return {};
}

// The crash-recovery backstop needs a real single-instance claim; without one a Linux build cannot
// distinguish concurrent launches from repeated failed ones, so it always claims the scope.
bool platform::claim_startup_scope()
{
	return true;
}

void platform::release_startup_scope()
{
}

std::string platform::utf16_to_utf8(const std::wstring_view text)
{
	return str::utf16_to_utf8(text);
}

std::wstring platform::utf8_to_utf16(const std::string_view text)
{
	return str::utf8_to_utf16(text);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Audio output. Needs PipeWire or PulseAudio; see docs/linux.md.
///////////////////////////////////////////////////////////////////////////////////////////////////

av_audio_device_ptr create_av_audio_device(std::string_view)
{
	return {};
}

std::vector<sound_device> list_audio_playback_devices()
{
	return {};
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Tests. The Windows-only subject file is not built here, so the registration is empty.
///////////////////////////////////////////////////////////////////////////////////////////////////

void register_platform_tests(view_state&, test_registry&)
{
}
