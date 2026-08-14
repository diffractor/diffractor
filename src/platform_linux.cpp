// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Linux platform implementation and process entry point. This is the start of the port
// described in docs/linux.md: it covers only what the portable core needs in order to link and
// run headless. There is no window, no renderer and no desktop integration here yet.

#include "pch.h"

#include "app_text.h"
#include "util_base64.h"

#include <condition_variable>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <linux/limits.h>
#include <ctime>

///////////////////////////////////////////////////////////////////////////////////////////////////
// Not yet ported. These belong to app.cpp and app_text.cpp, which do not compile under GCC yet.
// Delete this block when they join the build rather than letting two definitions exist.
///////////////////////////////////////////////////////////////////////////////////////////////////

const std::string_view s_app_name = "Diffractor";
const std::string_view s_app_version = "127.2";
const std::string_view g_app_build = "1275";
const wchar_t* s_app_name_l = L"Diffractor";

std::string df::format_version(bool)
{
	return std::format("{}.{}", s_app_version, g_app_build);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Paths and time
///////////////////////////////////////////////////////////////////////////////////////////////////

// A Linux native path is the UTF-8 bytes, so no conversion is needed. The Windows implementation
// hands back a UTF-16 extended path instead; neither call site knows which.
std::filesystem::path platform::to_stream_path(const df::file_path path)
{
	return {path.str()};
}

std::filesystem::path platform::to_stream_path(const df::folder_path path)
{
	return {std::string(path.text())};
}

uint32_t platform::tick_count()
{
	return static_cast<uint32_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
}

uint32_t platform::current_thread_id()
{
	return static_cast<uint32_t>(::gettid());
}

int64_t df::now_us()
{
	return std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

int64_t df::now_ms()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Events
///////////////////////////////////////////////////////////////////////////////////////////////////

namespace
{
	// platform::thread_event stores an opaque handle, so the condition variable and its state live
	// behind it exactly as the Win32 HANDLE does.
	struct event_state
	{
		std::mutex mutex;
		std::condition_variable cv;
		bool signalled = false;
		bool manual_reset = false;
	};
}

platform::thread_event::thread_event(const bool manual_reset, const bool initial_state)
{
	create(manual_reset, initial_state);
}

platform::thread_event::~thread_event()
{
	delete static_cast<event_state*>(_h);
	_h = nullptr;
}

void platform::thread_event::create(const bool manual_reset, const bool initial_state)
{
	auto* const state = new event_state();
	state->manual_reset = manual_reset;
	state->signalled = initial_state;
	_h = state;
}

void platform::thread_event::set() const noexcept
{
	auto* const state = static_cast<event_state*>(_h);
	if (state == nullptr) return;

	{
		std::lock_guard lock(state->mutex);
		state->signalled = true;
	}

	if (state->manual_reset) state->cv.notify_all();
	else state->cv.notify_one();
}

void platform::thread_event::reset() const noexcept
{
	auto* const state = static_cast<event_state*>(_h);
	if (state == nullptr) return;

	std::lock_guard lock(state->mutex);
	state->signalled = false;
}

namespace
{
	// Timestamps are Windows FILETIME units -- 100ns intervals since 1601-01-01 UTC -- because
	// df::date_t stores them that way in portable code. See docs/linux.md.
	constexpr uint64_t ft_ticks_per_second = 10'000'000ull;
	constexpr uint64_t ft_epoch_to_unix_seconds = 11'644'473'600ull;

	int64_t local_utc_offset_seconds(const uint64_t ts)
	{
		const auto seconds = static_cast<int64_t>(ts / ft_ticks_per_second) -
			static_cast<int64_t>(ft_epoch_to_unix_seconds);
		auto when = static_cast<time_t>(seconds);
		tm local = {};
		if (localtime_r(&when, &local) == nullptr) return 0;
		return local.tm_gmtoff;
	}
}

uint64_t platform::utc_to_local(const uint64_t ts)
{
	const auto offset = local_utc_offset_seconds(ts) * static_cast<int64_t>(ft_ticks_per_second);
	const auto result = static_cast<int64_t>(ts) + offset;
	return result < 0 ? ts : static_cast<uint64_t>(result);
}

uint64_t platform::local_to_utc(const uint64_t ts)
{
	// The offset is resolved at the local time being converted, which is the best available answer
	// for a timestamp inside a daylight-saving transition.
	const auto offset = local_utc_offset_seconds(ts) * static_cast<int64_t>(ft_ticks_per_second);
	const auto result = static_cast<int64_t>(ts) - offset;
	return result < 0 ? ts : static_cast<uint64_t>(result);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Known folders, following the XDG base directory specification
///////////////////////////////////////////////////////////////////////////////////////////////////

namespace
{
	std::string env_or(const char* name, const std::string& fallback)
	{
		const auto* const value = std::getenv(name);
		return value != nullptr && *value != 0 ? std::string(value) : fallback;
	}

	std::string home_folder()
	{
		return env_or("HOME", "/tmp");
	}
}

df::folder_path platform::known_path(const known_folder f)
{
	const auto home = home_folder();

	switch (f)
	{
	case known_folder::running_app_folder:
		{
			char buffer[PATH_MAX] = {};
			const auto len = ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
			if (len <= 0) return df::folder_path(home);
			const std::string_view exe(buffer, static_cast<size_t>(len));
			const auto slash = exe.find_last_of('/');
			return df::folder_path(slash == std::string_view::npos ? exe : exe.substr(0, slash));
		}
	case known_folder::app_data:
		return df::folder_path(std::format("{}/diffractor", env_or("XDG_DATA_HOME", home + "/.local/share")));
	case known_folder::app_cache_data:
		return df::folder_path(std::format("{}/diffractor", env_or("XDG_CACHE_HOME", home + "/.cache")));
	case known_folder::test_files_folder:
		return df::folder_path(home + "/diffractor-test-files");
	case known_folder::downloads:
		return df::folder_path(env_or("XDG_DOWNLOAD_DIR", home + "/Downloads"));
	case known_folder::pictures:
		return df::folder_path(env_or("XDG_PICTURES_DIR", home + "/Pictures"));
	case known_folder::video:
		return df::folder_path(env_or("XDG_VIDEOS_DIR", home + "/Videos"));
	case known_folder::music:
		return df::folder_path(env_or("XDG_MUSIC_DIR", home + "/Music"));
	case known_folder::documents:
		return df::folder_path(env_or("XDG_DOCUMENTS_DIR", home + "/Documents"));
	case known_folder::desktop:
		return df::folder_path(env_or("XDG_DESKTOP_DIR", home + "/Desktop"));

	// Cloud provider folders are Windows shell locations with no Linux equivalent. An empty path is
	// how the rest of the app already expresses "this location does not exist here".
	case known_folder::dropbox_photos:
	case known_folder::onedrive_pictures:
	case known_folder::onedrive_video:
	case known_folder::onedrive_music:
	case known_folder::onedrive_camera_roll:
		return {};
	}

	return {};
}

bool platform::is_writable(const df::folder_path path)
{
	return ::access(std::string(path.text()).c_str(), W_OK) == 0;
}

bool platform::exists(const df::file_path path)
{
	struct stat st = {};
	return ::stat(path.str().c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool platform::exists(const df::folder_path path)
{
	struct stat st = {};
	return ::stat(std::string(path.text()).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Calendar conversion. Timestamps are FILETIME ticks, so the epoch shift is applied on both sides.
///////////////////////////////////////////////////////////////////////////////////////////////////

df::day_t platform::to_date(const uint64_t ts)
{
	const auto seconds = static_cast<int64_t>(ts / ft_ticks_per_second) -
		static_cast<int64_t>(ft_epoch_to_unix_seconds);
	auto when = static_cast<time_t>(seconds);
	tm parts = {};
	if (::gmtime_r(&when, &parts) == nullptr) return {};

	return {
		parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday,
		parts.tm_hour, parts.tm_min, parts.tm_sec
	};
}

uint64_t platform::from_date(const df::day_t& day)
{
	tm parts = {};
	parts.tm_year = day.year - 1900;
	parts.tm_mon = day.month - 1;
	parts.tm_mday = day.day;
	parts.tm_hour = day.hour;
	parts.tm_min = day.minute;
	parts.tm_sec = day.second;
	parts.tm_isdst = 0;

	const auto seconds = ::timegm(&parts);
	if (seconds == static_cast<time_t>(-1)) return 0;

	return (static_cast<uint64_t>(seconds) + ft_epoch_to_unix_seconds) * ft_ticks_per_second;
}

df::date_t platform::now()
{
	const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	return df::date_t((static_cast<uint64_t>(seconds) + ft_epoch_to_unix_seconds) * ft_ticks_per_second);
}

std::string platform::format_date(const df::date_t d)
{
	const auto day = to_date(d._i);
	if (day.is_empty()) return {};
	return std::format("{:04}-{:02}-{:02}", day.year, day.month, day.day);
}

std::string platform::format_date_time(const df::date_t d)
{
	const auto day = to_date(d._i);
	if (day.is_empty()) return {};
	return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}", day.year, day.month, day.day, day.hour, day.minute,
	                   day.second);
}

std::string platform::number_dec_sep()
{
	const auto* const conv = ::localeconv();
	return conv != nullptr && conv->decimal_point != nullptr && *conv->decimal_point != 0
		       ? std::string(conv->decimal_point)
		       : ".";
}

std::string platform::utf8_to_a(const std::string_view utf8)
{
	// There is no "ANSI code page" on Linux; the system encoding is UTF-8, so this is the identity.
	return {utf8.begin(), utf8.end()};
}

// Not yet ported: file I/O is the next platform surface after this build runs. Returning null is
// what every caller already treats as "could not open", so nothing misreads an unported call as
// an empty file.
platform::file_ptr platform::open_file(df::file_path, file_open_mode)
{
	return {};
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Interning pool: address space is reserved up front and pages are committed as they are used, the
// same shape as the Windows MEM_RESERVE / MEM_COMMIT pair.
///////////////////////////////////////////////////////////////////////////////////////////////////

void* platform::memory_pool::alloc(const size_t size)
{
	const exclusive_lock lock(cs);

	if (base == nullptr)
	{
		auto want = reserve_size;

		while (want >= block_size)
		{
			auto* const p = ::mmap(nullptr, want, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);

			if (p != MAP_FAILED)
			{
				base = static_cast<uint8_t*>(p);
				reserved = want;
				break;
			}

			want /= 2;
		}

		if (base == nullptr) return nullptr;
	}

	const auto aligned = (size + alignment - 1) & ~(alignment - 1);
	if (used + aligned > reserved) return nullptr;

	while (used + aligned > committed)
	{
		const auto next = committed + block_size;
		if (next > reserved) return nullptr;
		if (::mprotect(base + committed, block_size, PROT_READ | PROT_WRITE) != 0) return nullptr;
		committed = next;
	}

	auto* const result = base + used;
	used += aligned;
	return result;
}

platform::file_op_result platform::move_file(const df::file_path existing, const df::file_path destination,
                                             const bool fail_if_exists)
{
	const auto from = existing.str();
	const auto to = destination.str();

	if (fail_if_exists)
	{
		struct stat st = {};
		if (::stat(to.c_str(), &st) == 0)
		{
			return {file_op_result_code::ALREADY_EXISTS};
		}
	}

	if (::rename(from.c_str(), to.c_str()) != 0)
	{
		return {file_op_result_code::FAILED, std::string(::strerror(errno))};
	}

	return {file_op_result_code::OK};
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Locks, randomness and memory
///////////////////////////////////////////////////////////////////////////////////////////////////

// Provisional: a spin-then-yield reader/writer lock held in the same uintptr_t the Windows SRWLOCK
// occupies, so `mutex` keeps the constant-initialised, destructor-free shape platform.h documents.
// It is correct but never blocks in the kernel; the real port replaces it with a futex-backed lock.
namespace
{
	constexpr uintptr_t lock_writer_bit = 1;
	constexpr uintptr_t lock_reader_step = 2;

	std::atomic_ref<uintptr_t> lock_state(const uintptr_t& cs)
	{
		return std::atomic_ref<uintptr_t>(const_cast<uintptr_t&>(cs));
	}
}

void platform::mutex::ex_lock() const
{
	auto state = lock_state(_cs);
	uintptr_t expected = 0;

	while (!state.compare_exchange_weak(expected, lock_writer_bit, std::memory_order_acquire,
	                                    std::memory_order_relaxed))
	{
		expected = 0;
		::sched_yield();
	}
}

void platform::mutex::ex_unlock() const
{
	lock_state(_cs).store(0, std::memory_order_release);
}

void platform::mutex::sh_lock() const
{
	auto state = lock_state(_cs);

	for (;;)
	{
		auto current = state.load(std::memory_order_relaxed);

		if ((current & lock_writer_bit) != 0)
		{
			::sched_yield();
			continue;
		}

		if (state.compare_exchange_weak(current, current + lock_reader_step, std::memory_order_acquire,
		                                std::memory_order_relaxed))
		{
			return;
		}
	}
}

void platform::mutex::sh_unlock() const
{
	lock_state(_cs).fetch_sub(lock_reader_step, std::memory_order_release);
}

bool platform::crc32_supported = false;
bool platform::arm_crc32_supported = false;

bool platform::has_avx2()
{
#if defined(__x86_64__) || defined(__i386__)
	// The build targets an SSE2 baseline, so the AVX2 paths in render_surface.cpp are dispatched
	// at runtime exactly as they are on Windows.
	static const bool supported = __builtin_cpu_supports("avx2");
	return supported;
#else
	return false;
#endif
}

void platform::secure_zero(void* ptr, const size_t len)
{
	::explicit_bzero(ptr, len);
}

bool platform::generate_random_bytes(uint8_t* buffer, const size_t len)
{
	size_t filled = 0;

	while (filled < len)
	{
		const auto n = ::getrandom(buffer + filled, len - filled, 0);

		if (n < 0)
		{
			if (errno == EINTR) continue;
			return false;
		}

		filled += static_cast<size_t>(n);
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Entry point
///////////////////////////////////////////////////////////////////////////////////////////////////

int main(const int argc, const char* const argv[])
{
	printf("Diffractor %s (linux stage-one build)\n", df::format_version(false).c_str());

	// Exercise the portable core so a successful run proves linkage, not only a successful build.
	const auto sample = "Diffractor"sv;
	const std::vector<uint8_t> sample_bytes(sample.begin(), sample.end());

	printf("  str::to_lower       %s\n", str::to_lower(sample).c_str());
	printf("  str::icmp           %d\n", str::icmp(sample, "DIFFRACTOR"sv));
	printf("  crypto::fnv1a_i     %u\n", crypto::fnv1a_i(sample));
	printf("  str::to_string i64  %s\n", str::to_string(static_cast<int64_t>(-1234567890123LL)).c_str());
	printf("  str::to_string dbl  %s\n", str::to_string(3.14159, 3).c_str());
	printf("  base64 round trip   %s\n", base64_decode(base64_encode(sample)) == sample_bytes ? "ok" : "FAILED");
	printf("  known app_data      %s\n",
	       std::string(platform::known_path(platform::known_folder::app_data).text()).c_str());
	printf("  known pictures      %s\n",
	       std::string(platform::known_path(platform::known_folder::pictures).text()).c_str());

	uint8_t random[8] = {};
	printf("  random bytes        %s\n", platform::generate_random_bytes(random, sizeof(random)) ? "ok" : "FAILED");

	platform::mutex m;
	{
		const platform::exclusive_lock lock(m);
		printf("  exclusive lock      ok\n");
	}

	for (auto i = 1; i < argc; ++i)
	{
		printf("  arg[%d] %s\n", i, argv[i]);
	}

	return 0;
}
