// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Linux file system implementation of the platform file interface: opening, attributes,
// enumeration, copy/move/delete, temporary files and memory mapping. The Windows counterpart is
// platform_win_files.cpp; the shell integration it also carries has no equivalent here and is
// stubbed in platform_linux_stubs.cpp.

#include "pch.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

namespace
{
	// df::date_t counts 100ns intervals since 1601; the file system counts seconds since 1970.
	constexpr uint64_t ft_ticks_per_second = 10'000'000ull;
	constexpr uint64_t ft_epoch_to_unix_seconds = 11'644'473'600ull;

	uint64_t to_ticks(const timespec& ts)
	{
		return (static_cast<uint64_t>(ts.tv_sec) + ft_epoch_to_unix_seconds) * ft_ticks_per_second +
			static_cast<uint64_t>(ts.tv_nsec) / 100u;
	}

	time_t to_unix_seconds(const df::date_t date)
	{
		if (!date.is_valid()) return 0;
		return static_cast<time_t>(date._i / ft_ticks_per_second) - static_cast<time_t>(ft_epoch_to_unix_seconds);
	}

	platform::file_attributes_t attributes_from_stat(const std::string& path)
	{
		platform::file_attributes_t result;
		struct stat st = {};

		if (::stat(path.c_str(), &st) != 0)
		{
			// Only an unambiguous "it is not there" counts as absence; anything else stays unknown,
			// so a caller that would overwrite on "not found" cannot act on a denied or offline path.
			result.presence = (errno == ENOENT || errno == ENOTDIR)
				                  ? platform::file_presence::not_found
				                  : platform::file_presence::unknown;
			return result;
		}

		result.presence = platform::file_presence::found;
		result.size = static_cast<uint64_t>(st.st_size);
		result.modified = to_ticks(st.st_mtim);
		result.created = to_ticks(st.st_ctim);
		result.is_readonly = ::access(path.c_str(), W_OK) != 0;

		const auto slash = path.find_last_of('/');
		const auto name = slash == std::string::npos ? path : path.substr(slash + 1);
		result.is_hidden = name.size() > 1 && name.front() == '.';

		return result;
	}

	class posix_file final : public platform::file
	{
	public:
		posix_file(const int fd, const df::file_path path) : _fd(fd), _path(path)
		{
		}

		~posix_file() override
		{
			if (_fd >= 0) ::close(_fd);
		}

		uint64_t size() const override
		{
			struct stat st = {};
			if (::fstat(_fd, &st) != 0) return 0;
			return static_cast<uint64_t>(st.st_size);
		}

		uint64_t read(uint8_t* buf, const uint64_t buf_size) const override
		{
			uint64_t total = 0;

			while (total < buf_size)
			{
				const auto n = ::read(_fd, buf + total, buf_size - total);
				if (n < 0)
				{
					if (errno == EINTR) continue;
					break;
				}
				if (n == 0) break;
				total += static_cast<uint64_t>(n);
			}

			return total;
		}

		uint64_t write(const uint8_t* data, const uint64_t size) override
		{
			uint64_t total = 0;

			while (total < size)
			{
				const auto n = ::write(_fd, data + total, size - total);
				if (n < 0)
				{
					if (errno == EINTR) continue;
					break;
				}
				total += static_cast<uint64_t>(n);
			}

			return total;
		}

		uint64_t seek(const uint64_t pos, const whence w) const override
		{
			const int origin = w == whence::begin ? SEEK_SET : w == whence::current ? SEEK_CUR : SEEK_END;
			const auto result = ::lseek(_fd, static_cast<off_t>(pos), origin);
			return result < 0 ? static_cast<uint64_t>(-1) : static_cast<uint64_t>(result);
		}

		uint64_t pos() const override
		{
			const auto result = ::lseek(_fd, 0, SEEK_CUR);
			return result < 0 ? static_cast<uint64_t>(-1) : static_cast<uint64_t>(result);
		}

		bool trunc(const uint64_t pos) const override
		{
			return ::ftruncate(_fd, static_cast<off_t>(pos)) == 0;
		}

		df::date_t get_created() override
		{
			struct stat st = {};
			if (::fstat(_fd, &st) != 0) return {};
			return df::date_t(to_ticks(st.st_ctim));
		}

		// A creation time cannot be set on Linux; birth time is not writable through any portable
		// interface. Recorded as a no-op rather than silently writing the modified time instead.
		void set_created(df::date_t) override
		{
		}

		df::date_t get_modified() override
		{
			struct stat st = {};
			if (::fstat(_fd, &st) != 0) return {};
			return df::date_t(to_ticks(st.st_mtim));
		}

		void set_modified(const df::date_t date) override
		{
			timespec times[2] = {};
			times[0].tv_nsec = UTIME_OMIT;
			times[1].tv_sec = to_unix_seconds(date);
			::futimens(_fd, times);
		}

		df::file_path path() const override
		{
			return _path;
		}

	private:
		int _fd = -1;
		df::file_path _path;
	};

	class posix_mapped_file final : public platform::mapped_file
	{
	public:
		posix_mapped_file(const int fd, const uint64_t size) : _fd(fd), _size(size)
		{
		}

		~posix_mapped_file() override
		{
			unmap();
			if (_fd >= 0) ::close(_fd);
		}

		bool map_whole()
		{
			return !set_window(0, _size).empty();
		}

		uint64_t file_size() const override
		{
			return _size;
		}

		df::cspan data() const override
		{
			return {_view, _view_len};
		}

		df::cspan set_window(const uint64_t offset, const uint64_t len) override
		{
			unmap();

			if (offset >= _size) return {};

			const auto want = std::min(len, _size - offset);
			if (want == 0) return {};

			// mmap requires a page-aligned offset, so the mapping starts below the requested one and
			// the returned span is shifted back up to it.
			static const auto page = static_cast<uint64_t>(::sysconf(_SC_PAGESIZE));
			const auto aligned = offset & ~(page - 1);
			const auto slack = offset - aligned;

			_map_len = static_cast<size_t>(want + slack);
			_map = ::mmap(nullptr, _map_len, PROT_READ, MAP_PRIVATE, _fd, static_cast<off_t>(aligned));

			if (_map == MAP_FAILED)
			{
				_map = nullptr;
				_map_len = 0;
				return {};
			}

			_view = static_cast<const uint8_t*>(_map) + slack;
			_view_len = static_cast<size_t>(want);
			return {_view, _view_len};
		}

		void release_working_set() override
		{
			if (_map != nullptr) ::madvise(_map, _map_len, MADV_DONTNEED);
		}

	private:
		void unmap()
		{
			if (_map != nullptr) ::munmap(_map, _map_len);
			_map = nullptr;
			_map_len = 0;
			_view = nullptr;
			_view_len = 0;
		}

		int _fd = -1;
		uint64_t _size = 0;
		void* _map = nullptr;
		size_t _map_len = 0;
		const uint8_t* _view = nullptr;
		size_t _view_len = 0;
	};
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Paths
///////////////////////////////////////////////////////////////////////////////////////////////////

std::wstring platform::to_file_system_path(const df::file_path path)
{
	return str::utf8_to_utf16(path.str());
}

std::wstring platform::to_file_system_path(const df::folder_path path)
{
	return str::utf8_to_utf16(path.text());
}

// Linux has no system normaliser and ICU is a dependency this build has not taken on. Hangul is
// the one script whose composition is arithmetic rather than table-driven, and it is the script the
// reported problem is about (#219): macOS, Finder and some cameras write the decomposed jamo that a
// Windows IME never produces. Every other script is returned unchanged, which is a partial answer -
// but a correct one for the case that has been seen, and it carries no dependency.
std::string platform::normalize_nfc(const std::string_view text)
{
	constexpr uint32_t l_base = 0x1100, v_base = 0x1161, t_base = 0x11A7, s_base = 0xAC00;
	constexpr uint32_t l_count = 19, v_count = 21, t_count = 28;
	constexpr uint32_t n_count = v_count * t_count;

	std::vector<uint32_t> composed;
	composed.reserve(text.size());

	auto i = text.begin();

	while (i < text.end())
	{
		const auto code_point = str::pop_utf8_char(i, text.end());

		if (!composed.empty())
		{
			const auto previous = composed.back();

			// A leading jamo followed by a vowel becomes the syllable that holds both.
			if (previous >= l_base && previous < l_base + l_count &&
				code_point >= v_base && code_point < v_base + v_count)
			{
				composed.back() = s_base + ((previous - l_base) * v_count + (code_point - v_base)) * t_count;
				continue;
			}

			// A syllable with no trailing consonant absorbs one that follows it.
			if (previous >= s_base && previous < s_base + l_count * n_count &&
				(previous - s_base) % t_count == 0 &&
				code_point > t_base && code_point < t_base + t_count)
			{
				composed.back() = previous + (code_point - t_base);
				continue;
			}
		}

		composed.emplace_back(code_point);
	}

	std::string result;
	result.reserve(text.size());
	auto inserter = std::back_inserter(result);

	for (const auto code_point : composed) str::char32_to_utf8(inserter, code_point);

	return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Files
///////////////////////////////////////////////////////////////////////////////////////////////////

platform::file_ptr platform::open_file(const df::file_path path, const file_open_mode mode)
{
	int flags = 0;

	switch (mode)
	{
	case file_open_mode::read:
	case file_open_mode::sequential_scan:
		flags = O_RDONLY;
		break;
	case file_open_mode::write:
		flags = O_WRONLY | O_CREAT;
		break;
	case file_open_mode::create:
		flags = O_RDWR | O_CREAT | O_TRUNC;
		break;
	case file_open_mode::read_write:
		flags = O_RDWR;
		break;
	}

	const auto fd = ::open(path.str().c_str(), flags, 0644);
	if (fd < 0) return {};

	if (mode == file_open_mode::sequential_scan)
	{
		::posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
	}

	return std::make_shared<posix_file>(fd, path);
}

platform::mapped_file_ptr platform::map_file(const df::file_path path, const map_mode mode)
{
	const auto fd = ::open(path.str().c_str(), O_RDONLY);
	if (fd < 0) return {};

	struct stat st = {};
	if (::fstat(fd, &st) != 0 || st.st_size == 0)
	{
		::close(fd);
		return {};
	}

	auto result = std::make_shared<posix_mapped_file>(fd, static_cast<uint64_t>(st.st_size));

	if (mode == map_mode::whole_file && !result->map_whole())
	{
		return {};
	}

	return result;
}

platform::file_attributes_t platform::file_attributes(const df::file_path path)
{
	return attributes_from_stat(path.str());
}

platform::file_attributes_t platform::file_attributes(const df::folder_path path)
{
	return attributes_from_stat(std::string(path.text()));
}

platform::file_op_result platform::delete_file(const df::file_path path)
{
	if (::unlink(path.str().c_str()) != 0)
	{
		return {file_op_result_code::FAILED, std::string(::strerror(errno))};
	}

	return {file_op_result_code::OK};
}

platform::file_op_result platform::create_folder(const df::folder_path path)
{
	const std::string text(path.text());
	std::string partial;

	// Every parent has to exist, and the Windows implementation creates the chain too.
	for (size_t i = 0; i <= text.size(); ++i)
	{
		if (i == text.size() || text[i] == '/')
		{
			if (i > 0)
			{
				partial.assign(text, 0, i);

				if (::mkdir(partial.c_str(), 0755) != 0 && errno != EEXIST)
				{
					return {file_op_result_code::FAILED, std::string(::strerror(errno))};
				}
			}
		}
	}

	return {file_op_result_code::OK};
}

platform::file_op_result platform::copy_file(const df::file_path existing, const df::file_path destination,
                                             const bool fail_if_exists, const bool can_create_folder)
{
	if (fail_if_exists && exists(destination))
	{
		return {file_op_result_code::ALREADY_EXISTS};
	}

	if (can_create_folder)
	{
		const auto folder = destination.folder();
		if (!exists(folder)) create_folder(folder);
	}

	const auto src = ::open(existing.str().c_str(), O_RDONLY);
	if (src < 0) return {file_op_result_code::FAILED, std::string(::strerror(errno))};

	const auto dst = ::open(destination.str().c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (dst < 0)
	{
		::close(src);
		return {file_op_result_code::FAILED, std::string(::strerror(errno))};
	}

	uint8_t buffer[64 * 1024];
	file_op_result result{file_op_result_code::OK};

	for (;;)
	{
		const auto n = ::read(src, buffer, sizeof(buffer));

		if (n < 0)
		{
			if (errno == EINTR) continue;
			result = {file_op_result_code::FAILED, std::string(::strerror(errno))};
			break;
		}

		if (n == 0) break;

		if (::write(dst, buffer, static_cast<size_t>(n)) != n)
		{
			result = {file_op_result_code::FAILED, std::string(::strerror(errno))};
			break;
		}
	}

	::close(src);
	::close(dst);

	if (result.failed()) ::unlink(destination.str().c_str());
	return result;
}

platform::file_op_result platform::replace_file(const df::file_path destination, const df::file_path existing,
                                                const bool create_originals)
{
	// The replacement's bytes have to be on the volume before it is swapped in, or a crash between
	// the two leaves the destination naming an empty file.
	{
		const auto fd = ::open(existing.str().c_str(), O_RDONLY);
		if (fd < 0) return {file_op_result_code::FAILED, std::string(::strerror(errno))};

		const auto flushed = ::fsync(fd) == 0;
		const auto flush_errno = errno;
		::close(fd);

		if (!flushed) return {file_op_result_code::FAILED, std::string(::strerror(flush_errno))};
	}

	if (create_originals && exists(destination))
	{
		const auto base_name = std::string(destination.file_name_without_extension()) + ".original"s;
		const auto extension = destination.extension();
		df::file_path backup;

		// A requested backup is part of the contract, so keep uniquifying rather than replacing the
		// destination with no new recovery point when an earlier .original is already there.
		for (auto attempt = 0; attempt < 1000 && backup.is_empty(); ++attempt)
		{
			const auto name = attempt == 0 ? base_name : std::format("{}.{}", base_name, attempt);
			const auto candidate = df::file_path(existing.folder(), name, extension);

			if (!exists(candidate)) backup = candidate;
		}

		if (backup.is_empty())
		{
			return {
				file_op_result_code::FAILED,
				std::format("Could not create a backup of {}", destination.str())
			};
		}

		if (const auto backup_result = copy_file(destination, backup, true, false); backup_result.failed())
		{
			return backup_result;
		}
	}

	// Opened before the rename on purpose: a descriptor follows the inode, so this keeps reading the
	// bytes just written whatever later happens to the name. That is what Windows needs a rename by
	// handle to achieve.
	const auto fd = ::open(existing.str().c_str(), O_RDONLY);

	// rename is atomic within a filesystem, which is the property the write pipeline depends on.
	if (::rename(existing.str().c_str(), destination.str().c_str()) != 0)
	{
		const auto rename_errno = errno;
		if (fd >= 0) ::close(fd);
		return {file_op_result_code::FAILED, std::string(::strerror(rename_errno))};
	}

	file_op_result result{file_op_result_code::OK};

	if (fd >= 0)
	{
		struct stat st = {};
		if (::fstat(fd, &st) == 0) result.modified = to_ticks(st.st_mtim);

		result.coherent_handle = std::make_shared<posix_file>(fd, destination);
	}

	return result;
}

bool platform::wait_for_unlocked_write(const df::file_path path)
{
	// Linux has no mandatory locking, so a file is never held open against a writer the way it is
	// on Windows. Writability is the only question left.
	return ::access(path.str().c_str(), W_OK) == 0 || !exists(path);
}

std::string platform::file_write_error(const df::file_path path)
{
	// Probe write access the same way a metadata writer would, so the caller can report the
	// concrete reason instead of an opaque toolkit error. Without mandatory locking the answer is
	// about permissions, a read-only mount or a missing file rather than another process.
	const auto fd = ::open(path.str().c_str(), O_WRONLY);

	if (fd < 0)
	{
		return std::string(::strerror(errno));
	}

	::close(fd);
	return {};
}

df::file_path platform::temp_file(const std::string_view ext, const df::folder_path folder)
{
	const auto dir = folder.is_empty() ? known_path(known_folder::app_cache_data) : folder;
	if (!exists(dir)) create_folder(dir);

	for (auto attempt = 0; attempt < 64; ++attempt)
	{
		uint32_t r = 0;
		generate_random_bytes(std::bit_cast<uint8_t*>(&r), sizeof(r));

		const auto name = std::format("diffractor_{:08x}{}", r, ext);
		const auto candidate = dir.combine_file(name);

		if (!exists(candidate)) return candidate;
	}

	return dir.combine_file(std::format("diffractor_fallback{}", ext));
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Enumeration
///////////////////////////////////////////////////////////////////////////////////////////////////

platform::folder_contents platform::iterate_file_items(const df::folder_path folder, const bool show_hidden)
{
	folder_contents result;

	const std::string path(folder.text());
	auto* const dir = ::opendir(path.c_str());
	if (dir == nullptr) return result;

	while (const auto* const entry = ::readdir(dir))
	{
		const std::string_view name(entry->d_name);

		if (name == "." || name == "..") continue;
		if (!show_hidden && name.size() > 1 && name.front() == '.') continue;

		const auto full = std::format("{}/{}", path, name);
		auto attributes = attributes_from_stat(full);

		struct stat st = {};
		if (::stat(full.c_str(), &st) != 0) continue;

		if (S_ISDIR(st.st_mode))
		{
			result.folders.emplace_back(folder_info{str::cache(name), attributes});
		}
		else if (S_ISREG(st.st_mode))
		{
			// There is no cloud placeholder to detect here, but the online/offline model itself is
			// portable, so the seam that stands in for one is honoured as it is on Windows.
			if (test_offline_predicate && test_offline_predicate(folder.combine_file(name)))
			{
				attributes.is_offline = true;
			}

			result.files.emplace_back(file_info{folder, str::cache(name), attributes});
		}
	}

	::closedir(dir);
	result.success = true;
	return result;
}

std::string platform::file_op_result::format_error(const std::string_view text, const std::string_view more_text) const
{
	std::string result(text);

	if (!error_message.empty())
	{
		if (!result.empty()) result += ": ";
		result += error_message;
	}

	if (!more_text.empty())
	{
		if (!result.empty()) result += " ";
		result += more_text;
	}

	return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Not yet ported: desktop integration. docs/linux.md records which of these map to XDG portals and
// which have no Linux counterpart at all.
///////////////////////////////////////////////////////////////////////////////////////////////////

// A recursive browse shows files from every level and no folder rows at all, so the folders are
// answered only for a scope that is one level deep. The wildcard names files, not folders.
std::vector<platform::folder_info> platform::select_folders(const df::item_selector& selector, const bool show_hidden)
{
	if (selector.is_recursive()) return {};

	return iterate_file_items(selector.folder(), show_hidden).folders;
}

std::vector<platform::file_info> platform::select_files(const df::item_selector& selector, const bool show_hidden)
{
	std::vector<file_info> results;

	const auto recursive = selector.is_recursive();
	const auto has_wildcard = selector.has_wildcard();

	std::vector<df::folder_path> pending = {selector.folder()};

	while (!pending.empty())
	{
		const auto folder = pending.back();
		pending.pop_back();

		const auto contents = iterate_file_items(folder, show_hidden);

		for (const auto& file : contents.files)
		{
			if (has_wildcard && !str::wildcard_icmp(file.name, selector.wildcard())) continue;
			results.emplace_back(file);
		}

		if (recursive)
		{
			for (const auto& child : contents.folders) pending.emplace_back(folder.combine(child.name));
		}
	}

	return results;
}

bool platform::run(std::string_view)
{
	return false;
}

bool platform::run(df::file_path, std::string_view)
{
	return false;
}

// Generated from src/Res by CMake; see the resource block in CMakeLists.txt.
extern const unsigned char diffractor_resource_sql[];
extern const unsigned long diffractor_resource_sql_size;
extern const unsigned char diffractor_resource_map_png[];
extern const unsigned long diffractor_resource_map_png_size;

df::blob platform::load_resource(const resource_item i)
{
	switch (i)
	{
	case resource_item::sql:
		return {diffractor_resource_sql, diffractor_resource_sql + diffractor_resource_sql_size};
	case resource_item::map_png:
		return {diffractor_resource_map_png, diffractor_resource_map_png + diffractor_resource_map_png_size};
	}

	return {};
}

// The Windows Shell property handlers and thumbnail cache have no Linux counterpart. Answering
// `fail` is what the callers already do when the shell has nothing, so each falls through to
// Diffractor's own metadata and thumbnail pipeline.
platform::get_cached_file_properties_response platform::get_cached_file_properties(df::file_path,
	prop::item_metadata&, ui::const_image_ptr&)
{
	return get_cached_file_properties_response::fail;
}

platform::get_cached_file_properties_response platform::get_shell_thumbnail(df::file_path, sizei, bool,
	ui::const_image_ptr&)
{
	return get_cached_file_properties_response::fail;
}

df::count_and_size platform::calc_folder_summary(const df::folder_path folder, const bool show_hidden,
                                                 const df::cancel_token& token)
{
	df::count_and_size result;

	// Recursion is bounded by the token the caller already owns, as on Windows.
	const auto contents = iterate_file_items(folder, show_hidden);

	for (const auto& f : contents.files)
	{
		if (token.is_cancelled()) return result;
		result.add(df::file_size(f.attributes.size));
	}

	for (const auto& sub : contents.folders)
	{
		if (token.is_cancelled()) return result;
		result += calc_folder_summary(folder.combine(sub.name), show_hidden, token);
	}

	return result;
}

// Linux has no drive letters. The mount table would be the equivalent, and is a separate piece of
// work; an empty list is what the sidebar already renders when there is nothing to show.
platform::drives platform::scan_drives()
{
	return {};
}

platform::file_op_result platform::move_file(const df::folder_path existing, const df::folder_path destination)
{
	if (::rename(std::string(existing.text()).c_str(), std::string(destination.text()).c_str()) != 0)
	{
		return {file_op_result_code::FAILED, std::string(::strerror(errno))};
	}

	return {file_op_result_code::OK};
}

// True for a UNC or network location on Windows. A Linux mount point carries no such marker in the
// path itself, so answering false keeps the local-path behaviour rather than guessing.
bool platform::is_server(std::string_view)
{
	return false;
}

uint32_t platform::caret_blink_time()
{
	return 530;
}

std::string platform::format_time(const df::date_t date)
{
	return format_date_time(date);
}

df::date_t platform::dos_date_to_ts(const uint16_t dos_date, const uint16_t dos_time)
{
	// The MS-DOS packing an archive entry carries: two-second resolution, and a year counted from
	// 1980. It records local time, so mktime is the right conversion rather than timegm.
	struct tm parts = {};
	parts.tm_mday = dos_date & 0x1f;
	parts.tm_mon = ((dos_date >> 5) & 0x0f) - 1;
	parts.tm_year = ((dos_date >> 9) & 0x7f) + 80;
	parts.tm_sec = (dos_time & 0x1f) * 2;
	parts.tm_min = (dos_time >> 5) & 0x3f;
	parts.tm_hour = (dos_time >> 11) & 0x1f;
	parts.tm_isdst = -1;

	// An entry can carry an out-of-range date; report "no date" rather than the value mktime would
	// normalise it into.
	if (parts.tm_mday < 1 || parts.tm_mon < 0 || parts.tm_mon > 11) return {};
	if (parts.tm_hour > 23 || parts.tm_min > 59 || parts.tm_sec > 59) return {};

	const auto when = mktime(&parts);
	if (when == static_cast<time_t>(-1)) return {};

	return df::date_t((static_cast<uint64_t>(when) + ft_epoch_to_unix_seconds) * ft_ticks_per_second);
}

// Opening a location in the desktop's handler is an XDG portal call; see docs/linux.md.
bool platform::open(df::file_path)
{
	return false;
}

bool platform::open(std::string_view)
{
	return false;
}

bool platform::created_date(const df::file_path path, const df::date_t dt)
{
	// A creation time cannot be set on Linux, so report the failure rather than a silent no-op that
	// a caller would record as a written date.
	(void)path;
	(void)dt;
	return false;
}

bool platform::is_valid_file_name(const std::string_view name)
{
	if (name.empty()) return false;
	if (name == "." || name == "..") return false;

	// The only bytes a Linux filesystem refuses. The Windows implementation additionally rejects the
	// reserved device names and trailing dots and spaces, none of which mean anything here.
	return name.find('/') == std::string_view::npos && name.find('\0') == std::string_view::npos;
}

local_folders_result platform::local_folders()
{
	local_folders_result result;
	result.pictures = known_path(known_folder::pictures);
	result.video = known_path(known_folder::video);
	result.music = known_path(known_folder::music);
	result.desktop = known_path(known_folder::desktop);
	result.downloads = known_path(known_folder::downloads);
	return result;
}

std::string platform::format_number(const std::string& num_text)
{
	// Thousands separators come from the C locale; the C.UTF-8 default groups nothing, which is
	// what the Windows implementation produces for an invalid locale too.
	return num_text;
}

uint32_t platform::file_crc32(const df::file_path path)
{
	return file_crc32(path, {});
}

uint32_t platform::file_crc32(const df::file_path path, const df::cancel_token& token)
{
	const auto f = open_file(path, file_open_mode::sequential_scan);
	if (!f) return 0;

	const auto size = f->size();
	uint64_t total_read = 0;

	// crypto::crc32c seeds with CRCINIT and inverts on the way out; the incremental form does
	// neither, so a chained call has to do both itself or it computes a different checksum.
	uint32_t crc = crypto::CRCINIT;
	std::vector<uint8_t> buffer(64 * 1024);

	for (;;)
	{
		if (token.is_cancelled()) return 0;

		const auto n = f->read(buffer.data(), buffer.size());
		if (n == 0) break;

		crc = crypto::crc32c(crc, buffer.data(), static_cast<size_t>(n));
		total_read += n;
	}

	// A file truncated by another process reads short without failing, and a partial checksum would
	// be recorded as if it described the whole file.
	return total_read == size ? ~crc : 0;
}

// WIC on Windows; the vendored codecs already cover these formats, so this becomes a files:: call
// rather than a platform one when it is ported.
ui::surface_ptr platform::image_to_surface(df::cspan, sizei)
{
	return {};
}

// Renders from a Windows-only font. Linux needs a bundled icon set, which is a visual design
// decision rather than a port.
ui::const_surface_ptr platform::create_segoe_md2_icon(wchar_t)
{
	return {};
}

std::string platform::user_name()
{
	const auto* const name = std::getenv("USER");
	return name != nullptr ? std::string(name) : std::string("user");
}

// The INI store is already portable in shape and is selected at runtime on Windows, so Linux
// simply always takes it -- there is no registry to choose between.
platform::setting_file_ptr platform::settings()
{
	static const auto store = create_ini_file_settings(known_path(known_folder::app_data));
	return store;
}
