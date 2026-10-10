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
#include <sys/stat.h>
#include <limits.h>

///////////////////////////////////////////////////////////////////////////////////////////////////
// Shell verbs. Some map to XDG portals; several have no counterpart.
///////////////////////////////////////////////////////////////////////////////////////////////////

std::string platform::OS()
{
	return "Linux";
}

// There is no Linux window or packaging yet, so the only honest answers are the family and the
// architecture this binary was built for. A field with nothing behind it reports unknown rather
// than a plausible value: with daily aggregates a wrong reading cannot be revisited.
df::environment_facts platform::environment()
{
	df::environment_facts result;
	result.family = df::os_family::linux_;

#if defined(__aarch64__)
	result.process = df::machine_arch::arm64;
#elif defined(__arm__)
	result.process = df::machine_arch::arm32;
#elif defined(__x86_64__)
	result.process = df::machine_arch::x64;
#elif defined(__i386__)
	result.process = df::machine_arch::x86;
#elif defined(__riscv) && __riscv_xlen == 64
	result.process = df::machine_arch::riscv64;
#else
	result.process = df::machine_arch::other;
#endif

	// Nothing here emulates, so the native machine is the one the process runs on. When a Linux
	// build ships this needs the same treatment IsWow64Process2 gives it on Windows.
	result.machine = result.process;
	return result;
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
// Writing a movie.
//
// There is no system encoder to hand the job to here, and Diffractor ships none: the FFmpeg build
// is configured without encoders on both platforms. Answering false is what makes Render absent
// rather than dimmed, so the Movie view builds and runs with everything except the render.
// docs/movie.md#8-ownership-and-limits and docs/linux.md record this as debt.
///////////////////////////////////////////////////////////////////////////////////////////////////

bool platform::can_write_movies()
{
	return false;
}

platform::movie_writer_ptr platform::create_movie_writer(const movie_writer_request&)
{
	return {};
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

// No mount-type query here is free of the wait it would be avoiding - statfs on a hung network
// mount blocks too - so every location is treated as one whose presence can be checked.
bool platform::is_network_location(const df::folder_path)
{
	return false;
}

namespace
{
	// The shell appends " (2)", " (3)" and so on to a colliding name. Matching that keeps a
	// collection that has moved between the two platforms looking like one collection.
	constexpr int max_collision_attempts = 1000;

	df::file_path collision_file_candidate(const df::folder_path folder, const std::string_view name,
	                                       const int attempt)
	{
		if (attempt == 0) return folder.combine_file(name);

		const auto extension_at = df::find_ext(name);
		const auto stem = name.substr(0, extension_at);
		const auto extension = name.substr(extension_at);
		return folder.combine_file(std::format("{} ({}){}", stem, attempt + 1, extension));
	}

	df::folder_path collision_folder_candidate(const df::folder_path parent, const std::string_view name,
	                                           const int attempt)
	{
		return attempt == 0 ? parent.combine(name) : parent.combine(std::format("{} ({})", name, attempt + 1));
	}

	platform::file_op_result create_folder_exclusive(const df::folder_path path)
	{
		if (::mkdir(std::string(path.text()).c_str(), 0755) == 0) return {platform::file_op_result_code::OK};
		if (errno == EEXIST) return {platform::file_op_result_code::ALREADY_EXISTS};
		return {platform::file_op_result_code::FAILED, std::string(::strerror(errno))};
	}

	struct directory_identity
	{
		dev_t dev = 0;
		ino_t ino = 0;
	};

	bool directory_identity_of(const df::folder_path path, directory_identity& result)
	{
		struct stat st = {};
		if (::stat(std::string(path.text()).c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return false;
		result = {st.st_dev, st.st_ino};
		return true;
	}

	bool directory_identity_of_text(const std::string& path, directory_identity& result)
	{
		struct stat st = {};
		if (::stat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return false;
		result = {st.st_dev, st.st_ino};
		return true;
	}

	bool is_directory_symlink(const df::folder_path path)
	{
		struct stat st = {};
		const std::string text(path.text());
		return ::lstat(text.c_str(), &st) == 0 && S_ISLNK(st.st_mode);
	}

	bool same_directory(const directory_identity& left, const directory_identity& right)
	{
		return left.dev == right.dev && left.ino == right.ino;
	}

	bool destination_is_same_or_descendant(const df::folder_path source, const df::folder_path destination)
	{
		directory_identity source_id;
		if (!directory_identity_of(source, source_id)) return false;

		auto current = std::string(destination.text());

		for (;;)
		{
			char resolved[PATH_MAX] = {};
			if (::realpath(current.c_str(), resolved) != nullptr)
			{
				current = resolved;
				break;
			}

			const auto parent = df::folder_path(current).parent();
			const auto parent_text = std::string(parent.text());
			if (parent_text == current) return false;
			current = parent_text;
		}

		for (auto folder = df::folder_path(current); !folder.is_empty(); folder = folder.parent())
		{
			directory_identity current_id;
			if (directory_identity_of_text(std::string(folder.text()), current_id) &&
				same_directory(source_id, current_id))
			{
				return true;
			}

			const auto parent = folder.parent();
			if (parent == folder) break;
		}

		return false;
	}

	platform::file_op_result copy_directory_symlink(const df::folder_path source, const df::folder_path destination,
	                                                const bool replace_existing)
	{
		std::array<char, PATH_MAX> target{};
		const auto target_len = ::readlink(std::string(source.text()).c_str(), target.data(), target.size() - 1);
		if (target_len < 0) return {platform::file_op_result_code::FAILED, std::string(::strerror(errno))};

		const std::string destination_text(destination.text());
		if (!replace_existing && platform::exists(destination)) return {platform::file_op_result_code::ALREADY_EXISTS};
		if (replace_existing && platform::exists(destination) && ::unlink(destination_text.c_str()) != 0)
		{
			return {platform::file_op_result_code::FAILED, std::string(::strerror(errno))};
		}

		if (::symlink(std::string_view(target.data(), static_cast<size_t>(target_len)).data(),
		              destination_text.c_str()) != 0)
		{
			return errno == EEXIST
				       ? platform::file_op_result{platform::file_op_result_code::ALREADY_EXISTS}
				       : platform::file_op_result{platform::file_op_result_code::FAILED, std::string(::strerror(errno))};
		}

		return {platform::file_op_result_code::OK};
	}

	platform::file_op_result copy_folder_contents(const df::folder_path source, const df::folder_path destination,
	                                              const bool replace_existing)
	{
		if (!replace_existing && platform::exists(destination)) return {platform::file_op_result_code::ALREADY_EXISTS};

		if (destination_is_same_or_descendant(source, destination))
		{
			return {
				platform::file_op_result_code::FAILED,
				std::format("Cannot copy {} into itself", source.text())
			};
		}

		if (const auto created = replace_existing
			                         ? platform::create_folder(destination)
			                         : create_folder_exclusive(destination);
			created.failed())
		{
			return created;
		}

		// Hidden entries are part of the folder whatever the browser shows, so a copy that dropped
		// them would be a quietly incomplete copy.
		const auto contents = platform::iterate_file_items(source, true);

		// An enumeration that failed lists only part of the folder - or none of it - and copying
		// that listing and answering OK reports a folder as copied whole when it is not.
		if (!contents.success)
		{
			return {platform::file_op_result_code::FAILED, std::format("Could not read {}", source.text())};
		}

		for (const auto& file : contents.files)
		{
			if (platform::test_linux_before_copy_folder_file_path)
			{
				platform::test_linux_before_copy_folder_file_path(destination.combine_file(file.name));
			}

			const auto result = platform::copy_file(source.combine_file(file.name),
			                                        destination.combine_file(file.name), !replace_existing, false);
			if (result.code == platform::file_op_result_code::ALREADY_EXISTS)
			{
				return {platform::file_op_result_code::FAILED, std::format("Destination exists: {}", file.name)};
			}
			if (result.failed()) return result;
		}

		for (const auto& folder : contents.folders)
		{
			const auto child_source = source.combine(folder.name);
			const auto child_destination = destination.combine(folder.name);
			const auto result = is_directory_symlink(child_source)
				                    ? copy_directory_symlink(child_source, child_destination, replace_existing)
				                    : copy_folder_contents(child_source, child_destination, replace_existing);
			if (result.code == platform::file_op_result_code::ALREADY_EXISTS)
			{
				return {platform::file_op_result_code::FAILED, std::format("Destination exists: {}", folder.name)};
			}
			if (result.failed()) return result;
		}

		return {platform::file_op_result_code::OK};
	}

	platform::file_op_result delete_folder_contents(const df::folder_path folder)
	{
		const std::string path(folder.text());

		// A symlink to a directory enumerates as a folder, so recursing into it would delete files
		// outside the tree that was selected - and rmdir on the link would then fail, leaving the
		// damage with nothing to show for it. The link itself is what a delete removes.
		struct stat link_st = {};

		if (::lstat(path.c_str(), &link_st) == 0 && S_ISLNK(link_st.st_mode))
		{
			if (::unlink(path.c_str()) != 0)
			{
				return {platform::file_op_result_code::FAILED, std::string(::strerror(errno))};
			}

			return {platform::file_op_result_code::OK};
		}

		const auto contents = platform::iterate_file_items(folder, true);

		// An enumeration that failed lists only part of the folder, and rmdir below would fail on
		// what it never reached. Deleting half a tree and reporting success is the worse answer.
		if (!contents.success)
		{
			return {platform::file_op_result_code::FAILED, std::format("Could not read {}", folder.text())};
		}

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
		if (::rmdir(path.c_str()) != 0)
		{
			return {platform::file_op_result_code::FAILED, std::string(::strerror(errno))};
		}

		return {platform::file_op_result_code::OK};
	}
}

std::function<void(df::file_path)> platform::test_linux_before_claim_file_path;
std::function<void(df::folder_path)> platform::test_linux_before_claim_folder_path;
std::function<void(df::file_path)> platform::test_linux_before_copy_folder_file_path;

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
	for (const auto& folder : folders)
	{
		if (destination_is_same_or_descendant(folder, target))
		{
			return {
				file_op_result_code::FAILED,
				std::format("Cannot copy {} into itself", folder.text())
			};
		}
	}

	if (const auto created = create_folder(target); created.failed()) return created;

	file_op_result result{file_op_result_code::OK};

	// Whatever the run has already created stays in the answer. The caller selects and indexes from
	// created_files, so returning a bare failure hid every mutation that had already been made - the
	// files were on disk and nothing in the application knew about them.
	const auto stopped_by = [&result](const file_op_result& failure)
	{
		result.code = failure.code;
		result.error_message = failure.error_message;
		return result;
	};

	for (const auto& file : files)
	{
		df::file_path destination;
		file_op_result copied{file_op_result_code::FAILED};
		const auto attempts = replace_existing ? 1 : max_collision_attempts;

		for (auto attempt = 0; attempt < attempts; ++attempt)
		{
			destination = replace_existing ? target.combine_file(file.name()) : collision_file_candidate(target, file.name(), attempt);
			if (test_linux_before_claim_file_path) test_linux_before_claim_file_path(destination);

			copied = copy_file(file, destination, !replace_existing, false);
			if (!copied.failed() || copied.code != file_op_result_code::ALREADY_EXISTS || replace_existing) break;
		}

		if (copied.failed())
		{
			if (!replace_existing && copied.code == file_op_result_code::ALREADY_EXISTS)
			{
				return stopped_by({
					file_op_result_code::FAILED, std::format("Could not find a free name for {}", file.str())
				});
			}

			return stopped_by(copied);
		}

		if (is_move)
		{
			if (const auto removed = delete_file(file); removed.failed())
			{
				// The copy landed before the source refused to go, so it is named too.
				result.created_files.files.emplace_back(destination);
				return stopped_by(removed);
			}
		}

		result.created_files.files.emplace_back(destination);
	}

	for (const auto& folder : folders)
	{
		df::folder_path destination;
		file_op_result copied{file_op_result_code::FAILED};
		const auto attempts = replace_existing ? 1 : max_collision_attempts;

		for (auto attempt = 0; attempt < attempts; ++attempt)
		{
			destination = replace_existing ? target.combine(folder.name()) : collision_folder_candidate(target, folder.name(), attempt);
			if (test_linux_before_claim_folder_path) test_linux_before_claim_folder_path(destination);

			copied = copy_folder_contents(folder, destination, replace_existing);
			if (!copied.failed() || copied.code != file_op_result_code::ALREADY_EXISTS || replace_existing) break;
		}

		if (copied.failed())
		{
			if (!replace_existing && copied.code == file_op_result_code::ALREADY_EXISTS)
			{
				return stopped_by({
					file_op_result_code::FAILED, std::format("Could not find a free name for {}", folder.text())
				});
			}

			// The folder was created and partly filled before the failure, so it is named too.
			if (platform::exists(destination)) result.created_files.folders.emplace_back(destination);
			return stopped_by(copied);
		}

		if (is_move)
		{
			if (const auto removed = delete_folder_contents(folder); removed.failed())
			{
				result.created_files.folders.emplace_back(destination);
				return stopped_by(removed);
			}
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

bool platform::prompt_for_open_paths(std::vector<df::file_path>&, const std::vector<file_dialog_filter>&, bool)
{
	return false;
}

bool platform::prompt_for_save_path(df::file_path&, const std::vector<file_dialog_filter>&)
{
	return false;
}

platform::clipboard_data_ptr platform::clipboard()
{
	return {};
}

std::optional<std::string> platform::clipboard_text()
{
	return std::nullopt;
}

bool platform::set_clipboard(const std::vector<df::file_path>&, const std::vector<df::folder_path>&,
                             const file_load_result&, bool)
{
	return false;
}

bool platform::set_clipboard(std::string_view)
{
	return false;
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

bool platform::can_share_files()
{
	return false;
}

bool platform::share_files(const std::vector<df::file_path>&, std::string_view)
{
	return false;
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

av_audio_device_ptr create_av_audio_device(std::string_view, double)
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
