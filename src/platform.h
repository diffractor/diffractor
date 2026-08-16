// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Platform abstraction interface. Defines cross-platform APIs for file system,
// threading, networking, clipboard, and system services.

#pragma once

#include "util_map.h"

class image_edits;
class metadata_edits;
struct file_load_result;

namespace ui
{
	class image;
	class surface;
	using surface_ptr = std::shared_ptr<surface>;
	using const_surface_ptr = std::shared_ptr<const surface>;
	using image_ptr = std::shared_ptr<image>;
	using const_image_ptr = std::shared_ptr<const image>;
};

namespace df
{
	struct progess_i;
	class item_selector;
	class item_set;
	class date_t;
	struct day_t;
	struct index_roots;
	struct count_and_size;
};

namespace prop
{
	struct item_metadata;
}

struct local_folders_result
{
	df::folder_path pictures;
	df::folder_path video;
	df::folder_path music;
	df::folder_path desktop;
	df::folder_path downloads;
	df::folder_path dropbox_photos;
	df::folder_path onedrive_pictures;
	df::folder_path onedrive_video;
	df::folder_path onedrive_music;
};

namespace platform
{
	// Written under memory_pool::cs, read from the UI thread for the about box.
	extern std::atomic<size_t> static_memory_usage;

	std::string OS();

	extern bool sse2_supported;
	extern bool ssse3_supported;
	extern bool crc32_supported;
	extern bool avx2_supported;
	extern bool avx512_supported;
	extern bool neon_supported;
	extern bool arm_crc32_supported;

	void secure_zero(void* ptr, size_t len);

	// False leaves the buffer zeroed; callers must not fall back to its contents.
	bool generate_random_bytes(uint8_t* buffer, size_t len);

	void trace(std::string_view message);
	void trace(const std::string& message);

	df::unique_folders known_folders();
	local_folders_result local_folders();

	void set_desktop_wallpaper(df::file_path file_path);
	void show_file_properties(const std::vector<df::file_path>& files, const std::vector<df::folder_path>& folders);

	bool has_burner();
	bool burn_to_cd(const std::vector<df::file_path>& files, const std::vector<df::folder_path>& folders);
	void print(const std::vector<df::file_path>& files, const std::vector<df::folder_path>& folders);

	std::string format_number(const std::string& num_text);
	std::string number_dec_sep();
	bool is_valid_file_name(std::string_view name);

	// A failed query is not proof of absence. Access denied, an offline volume or a dropped network
	// share all fail the same call as a deleted file, so they stay `unknown` and a caller that would
	// delete or overwrite on "not there" cannot act on a guess.
	enum class file_presence
	{
		unknown,
		not_found,
		found,
	};

	struct file_attributes_t
	{
		file_presence presence = file_presence::unknown;
		bool is_readonly = false;
		bool is_offline = false;
		bool is_hidden = false;
		uint64_t modified = 0;
		uint64_t created = 0;
		uint64_t size = 0;

		bool exists() const { return presence == file_presence::found; }
		bool confirmed_missing() const { return presence == file_presence::not_found; }
	};

	struct file_info
	{
		df::folder_path folder = {};
		str::cached name = {};
		file_attributes_t attributes = {};
	};

	struct folder_info
	{
		str::cached name = {};
		file_attributes_t attributes = {};
	};

	struct folder_contents
	{
		std::vector<folder_info> folders;
		std::vector<file_info> files;
		bool success = false;

		folder_contents() noexcept = default;
		folder_contents(const folder_contents&) = delete;
		folder_contents& operator=(const folder_contents&) = delete;
		folder_contents(folder_contents&&) noexcept = default;
		folder_contents& operator=(folder_contents&&) noexcept = default;
	};

	enum class drive_type
	{
		unknown,
		removable,
		fixed,
		remote,
		cdrom,
		device
	};

	struct drive_t
	{
		drive_type type = drive_type::unknown;
		std::string name;
		std::string vol_name;
		std::string file_system;

		df::file_size used;
		df::file_size free;
		df::file_size capacity;
	};

	using drives = std::vector<drive_t>;
	drives scan_drives();

	class file
	{
	public:
		virtual ~file() = default;

		enum class whence
		{
			begin,
			current,
			end
		};

		virtual uint64_t size() const = 0;
		virtual uint64_t read(uint8_t* buf, uint64_t buf_size) const = 0;
		virtual uint64_t write(const uint8_t* data, uint64_t size) = 0;
		virtual uint64_t seek(uint64_t pos, whence w) const = 0;
		virtual uint64_t pos() const = 0;
		virtual bool trunc(uint64_t pos) const = 0;
		virtual df::date_t get_created() = 0;
		virtual void set_created(df::date_t date) = 0;
		virtual df::date_t get_modified() = 0;
		virtual void set_modified(df::date_t date) = 0;
		virtual df::file_path path() const = 0;
	};

	using file_ptr = std::shared_ptr<file>;

	enum class map_mode
	{
		// One view over the whole file. data() is the file; touching all of it makes all of it
		// resident, so this suits random sampling rather than a full scan.
		whole_file,
		// No view until set_window is called. Lets a full scan keep only a bounded window
		// resident, which is both smaller and faster than scanning a whole-file view.
		windowed,
	};

	// A read-only view of a file's bytes backed by the file itself rather than by heap.
	//
	// The pages are clean and shared, so they cost no commit charge and do not appear in the
	// private working set that Task Manager reports as "Memory". They are also evictable without
	// a pagefile write, because the file on disk is already their backing store. That makes this
	// the cheapest way to read a large data file that is scanned once and then sampled at random.
	//
	// Only touched pages become resident, so mapping a whole file and reading a few records out of
	// it costs a few pages rather than the file size.
	class mapped_file : public df::no_copy
	{
	public:
		virtual ~mapped_file() = default;

		virtual uint64_t file_size() const = 0;

		// The current view. Empty for a windowed mapping until set_window succeeds.
		virtual df::cspan data() const = 0;

		// Re-points the view at [offset, offset + len), clamped to the file. The returned span
		// starts exactly at offset even though the underlying view is granularity-aligned. The
		// previous view is invalidated, so callers must not retain spans across a call.
		//
		// Concurrency: data() and file_size() are safe to call from any number of threads, so a
		// whole-file mapping can be shared by readers. set_window mutates the view and is not, so a
		// shared mapping must not be re-windowed while readers hold it.
		virtual df::cspan set_window(uint64_t offset, uint64_t len) = 0;

		// Drops the mapped pages from the working set without unmapping. The data stays valid and
		// faults back in on next touch, trading a soft fault for a smaller reported figure.
		virtual void release_working_set() = 0;
	};

	using mapped_file_ptr = std::shared_ptr<mapped_file>;

	// Null when the file is missing, empty or cannot be mapped.
	mapped_file_ptr map_file(df::file_path path, map_mode mode = map_mode::whole_file);

	enum class file_open_mode
	{
		read,
		write,
		create,
		sequential_scan,
		read_write,
	};

	// The path form the OS and native third-party libraries take. Windows needs UTF-16, with the
	// \\?\ prefix once the path is long; elsewhere the file system takes the UTF-8 bytes as they
	// are. Portable callers pass it straight through and never inspect it.
#ifdef _WIN32
	using native_path = std::wstring;
#else
	using native_path = std::string;
#endif

	native_path to_file_system_path(df::file_path path);
	native_path to_file_system_path(df::folder_path path);

	// The same path a native API would be given, as UTF-8, for the third-party libraries that take a
	// byte path on every platform. On Windows this still carries the \\?\ prefix for a long path, so
	// it is not interchangeable with df::file_path::str().
	std::string to_utf8_file_system_path(df::file_path path);

	// std::fstream accepts a std::filesystem::path everywhere, but only MSVC accepts a std::wstring.
	// Going through this keeps stream call sites free of any assumption about the native encoding.
	std::filesystem::path to_stream_path(df::file_path path);
	std::filesystem::path to_stream_path(df::folder_path path);

	enum class known_folder
	{
		running_app_folder,
		test_files_folder,
		app_data,
		app_cache_data,
		downloads,
		pictures,
		video,
		music,
		documents,
		desktop,
		dropbox_photos,
		onedrive_pictures,
		onedrive_video,
		onedrive_music,
		onedrive_camera_roll,
	};

	df::folder_path known_path(known_folder f);
	file_ptr open_file(df::file_path path, file_open_mode mode);
	uint32_t file_crc32(df::file_path path);
	uint32_t file_crc32(df::file_path path, const df::cancel_token& token);
	// Rasterises one glyph of the bundled icon font to a surface, for the places that need an image
	// rather than drawn text. Takes a code point, so it does not depend on the size of wchar_t.
	ui::const_surface_ptr create_icon_surface(char32_t ch);
	bool eject(df::folder_path path);

	enum class file_op_result_code
	{
		OK,
		CANCELLED,
		FAILED,
		// A fail-if-exists write refused because the destination is there. That is the answer to the
		// question the caller asked, not a fault, so a run can report it as a file that changed after
		// review rather than as a disk error.
		ALREADY_EXISTS
	};

	struct file_op_result
	{
		file_op_result_code code = file_op_result_code::FAILED;
		std::string error_message;
		df::paths created_files;

		// Set only by replace_file when it renames the replacement into place through a
		// retained handle. This is that same still-open, cache-coherent handle, positioned at
		// the start of the freshly-swapped file, so the file can be re-scanned immediately
		// without a stale by-name reopen. Null when replace_file fell back to a plain move.
		// When set, `modified` holds the file's modified time (in df::date_t ticks, i.e. the
		// value df::date_t wraps) read back from the handle. Stored as a raw tick count rather
		// than a df::date_t because date_t is only forward-declared here (see pch.h include order).
		file_ptr coherent_handle;
		uint64_t modified = 0;

		bool success() const
		{
			return code == file_op_result_code::OK;
		}

		bool failed() const
		{
			return code != file_op_result_code::OK;
		}

		std::string format_error(std::string_view text = {}, std::string_view more_text = {}) const;
	};

#ifdef _DEBUG
	ui::surface_ptr capture_window_surface(const std::any& window_handle);
#endif

	file_op_result delete_items(const std::vector<df::file_path>& files, const std::vector<df::folder_path>& folders,
	                            bool allow_undo);
	// Returns true if deleting the given paths would move them to the Recycle Bin.
	// Returns false when any path is on a location that bypasses the Recycle Bin
	// (for example a UNC network path), causing a permanent, unrecoverable delete.
	bool can_recycle(const std::vector<df::file_path>& files, const std::vector<df::folder_path>& folders);
	// Copies or moves through the shell. Collisions are auto-renamed unless replace_existing is set,
	// which the caller may only do having named the colliding files and had the overwrite confirmed.
	file_op_result move_or_copy(const std::vector<df::file_path>& files, const std::vector<df::folder_path>& folders,
	                            df::folder_path target, bool is_move, bool replace_existing = false);

	file_op_result delete_file(df::file_path path);
	file_attributes_t file_attributes(df::file_path path);
	file_attributes_t file_attributes(df::folder_path path);
	file_op_result copy_file(df::file_path existing, df::file_path destination, bool fail_if_exists,
	                         bool can_create_folder);
	file_op_result move_file(df::file_path existing, df::file_path destination, bool fail_if_exists);
	file_op_result move_file(df::folder_path existing, df::folder_path destination);
	file_op_result replacement_flush_result(bool flushed, std::string error_message = {});
	file_op_result replace_file(df::file_path destination, df::file_path existing, bool create_originals = false);
	bool exists(df::file_path path);
	bool exists(df::folder_path path);
	file_op_result create_folder(df::folder_path path);
	// True when a new file can be created in the folder. Store package folders are read-only and
	// per-machine installs are read-only without elevation, so writable data must live elsewhere.
	bool is_writable(df::folder_path path);
	bool open(df::file_path path);
	bool open(std::string_view path);
	bool run(std::string_view cmd);
	// Preferred over run(cmd): naming the image removes any ambiguity about which prefix of the
	// command line is the executable (CWE-428).
	bool run(df::file_path exe, std::string_view cmd);
	void show_in_file_browser(df::file_path path);
	void show_in_file_browser(df::folder_path path);

	// Total working set counts clean file-backed pages that are shared with every other process
	// mapping the same file, so a memory-mapped data file inflates it while costing the process no
	// private memory at all. Task Manager's Memory column reports the private working set, so that
	// is the figure to show a user who is comparing the two.
	struct memory_usage_t
	{
		int64_t private_working_set = 0;
		int64_t shared_working_set = 0;
		int64_t working_set = 0;
		int64_t peak_working_set = 0;
		int64_t commit = 0;
	};

	bool memory_usage(memory_usage_t& result);
	df::folder_path temp_folder();
	int display_frequency();

	// The app is compiled for an SSE2 baseline, so anything wider has to be selected at run time.
	bool has_avx2();

	df::file_path resolve_link(df::file_path path);
	bool created_date(df::file_path path, df::date_t dt);


	struct metadata_result
	{
		std::optional<std::string> title;
		std::vector<std::string> tags;
		std::optional<int> rating;
	};

	// Fails for containers Windows has no property handler for, and transiently on a UNC path
	// under load, so the result names the step that failed rather than collapsing to a bool.
	file_op_result write_shell_tags(df::file_path path, const std::vector<std::string>& tags);
	metadata_result read_shell_metadata(df::file_path path);

	bool save_to_file(df::file_path path, df::cspan data);

	df::count_and_size calc_folder_summary(df::folder_path folder, bool show_hidden, const df::cancel_token& token);
	folder_contents iterate_file_items(df::folder_path folder, bool show_hidden);
	std::vector<folder_info> select_folders(const df::item_selector& selector, bool show_hidden);
	std::vector<file_info> select_files(const df::item_selector& selector, bool show_hidden);

	// Test seam: when set, this predicate overrides cloud-placeholder (offline) detection so
	// unit tests can simulate OneDrive Files On-Demand online-only files without real cloud
	// storage. Returning true marks the given path as offline during folder enumeration.
	extern std::function<bool(const df::file_path&)> test_offline_predicate;

	struct scan_result
	{
		bool success = false;
		df::file_path saved_file_path;
		std::string error_message;
	};

	scan_result scan(df::folder_path save_path);

	enum class get_cached_file_properties_response
	{
		ok,
		fail,
		pending
	};

	get_cached_file_properties_response get_cached_file_properties(df::file_path path,
	                                                               prop::item_metadata& properties_out,
	                                                               ui::const_image_ptr& thumbnail_out);
	// Fetch a thumbnail via the Windows Shell (IShellItemImageFactory) -- the same path Explorer
	// uses. For a cloud-only placeholder this returns the provider's thumbnail WITHOUT hydrating.
	// allow_network=false restricts to the local thumbnail cache; true permits a cloud fetch.
	get_cached_file_properties_response get_shell_thumbnail(df::file_path path, sizei requested_extent,
	                                                        bool allow_network, ui::const_image_ptr& thumbnail_out);

	std::string user_name();
	std::string last_os_error();

	// Last-resort feedback for a failure that happens before there is a window or a message loop to
	// show the app's own dialog. Blocks until the user dismisses it.
	void show_startup_failure(std::string_view message);

	// Returns a human-readable reason why the file cannot currently be opened for writing
	// (e.g. locked by another process, read-only, access denied), or an empty string if it
	// can be opened. Used to turn opaque metadata-write failures into an actionable message.
	std::string file_write_error(df::file_path path);
	// Bounded wait for a transient lock on an existing file to clear. True when it became writable.
	bool wait_for_unlocked_write(df::file_path path);
	void set_thread_description(std::string_view name);
	df::file_path temp_file(std::string_view ext = ".tmp", df::folder_path folder = {});

	bool browse_for_folder(df::folder_path& path);
	bool prompt_for_save_path(df::file_path& path);

	std::string utf16_to_utf8(std::wstring_view text);
	std::wstring utf8_to_utf16(std::string_view text);

	std::string utf8_to_a(std::string_view utf8);

	// Canonical Unicode composition (NFC). Used to make text that differs only in
	// normalization form (e.g. Korean Hangul as precomposed syllables vs decomposed
	// conjoining jamo) compare and search as equal. ASCII input is returned as-is.
	std::string normalize_nfc(std::string_view text);

	// What the system font stack knows about one character in two named font families.
	// Exposed so tests can assert the glyph-fallback contract (issue #219) without
	// reaching into the platform text API themselves.
	struct glyph_fallback_probe
	{
		// False when the query could not run at all (no text engine, or a family is
		// not installed on this machine). Callers must treat the rest as meaningless.
		bool available = false;
		// Glyph index the primary family maps the character to; 0 means "not covered",
		// which is what makes fallback mandatory.
		uint16_t primary_glyph = 0;
		// Glyph index in the fallback family. Non-zero when that family covers it.
		uint16_t fallback_glyph = 0;
		// True when metrics for fallback_glyph read from the fallback face succeeded.
		bool fallback_metrics_ok = false;
		// True when reading metrics for fallback_glyph from the PRIMARY face either
		// fails or returns different metrics -- i.e. the face confusion is real.
		bool primary_metrics_differ = false;
	};

	glyph_fallback_probe probe_glyph_fallback(std::string_view primary_family, std::string_view fallback_family,
	                                          char32_t code_point);

	// What the font-face cache does with repeated requests. Exposed so tests can assert the
	// caching contract without reaching into the renderer: a repeat request must be served
	// from the cache rather than re-running the family lookup and re-logging its failures
	// (issue #232), and a font-size change must produce a distinct face so glyphs cached for
	// the old size cannot be reused (issue #189).
	struct font_cache_probe
	{
		// False when no text engine could be created; the rest is then meaningless.
		bool available = false;
		// Entries held after the first request. One request must add exactly one entry.
		int entries_after_first = 0;
		// True when the identical request returned the same face and added no entry.
		bool same_request_is_cached = false;
		// True when the same face at a different size returned a different instance.
		bool size_change_is_distinct = false;
		// True when a different face type at the same size returned a different instance.
		bool face_change_is_distinct = false;
		// True when resetting the fonts emptied the cache, so a settings change cannot
		// leave stale faces behind.
		bool reset_clears_cache = false;
	};

	font_cache_probe probe_font_cache(int base_font_size);

#ifndef WINSTORE
	void download_and_verify(const std::function<void(df::file_path)>& complete);
	file_op_result install(df::file_path installer_path, df::folder_path destination_folder, bool silent,
	                       bool run_app_after_install);
#endif


	// dates
	df::day_t to_date(uint64_t uint64);
	uint64_t from_date(const df::day_t& day);
	uint64_t utc_to_local(uint64_t ts);
	uint64_t local_to_utc(uint64_t ts);
	df::date_t dos_date_to_ts(uint16_t dos_date, uint16_t dos_time);
	std::string format_date_time(df::date_t d);
	std::string format_date(df::date_t d);
	std::string format_time(df::date_t d);
	df::date_t now();

	std::string user_language();


	struct open_with_entry
	{
		std::string name;
		std::function<bool(const std::vector<df::file_path>& files, const std::vector<df::folder_path>& folders)>
		invoke;
		int weight = 0;
	};

	std::vector<open_with_entry> assoc_handlers(std::string_view ext);

	enum class resource_item
	{
		sql,
		map_png
	};

	df::blob load_resource(resource_item i);
	df::file_path running_app_path();
	bool is_server(std::string_view path);

	enum class drop_effect
	{
		none,
		copy,
		move,
		link
	};

	class clipboard_data
	{
	public:
		struct description
		{
			std::string first_name;
			drop_effect preferred_drop_effect = drop_effect::none;
			int count = 0;
			bool has_readonly = false;
		};

		virtual bool has_drop_files() const = 0;
		virtual bool has_bitmap() const = 0;
		virtual description files_description() const = 0;
		virtual df::file_path first_path() const = 0;

		virtual file_op_result drop_files(df::folder_path save_path, drop_effect effect) = 0;
		virtual file_op_result save_bitmap(df::folder_path save_path, std::string_view name, bool as_png) = 0;
	};

	drop_effect perform_drag(const std::any& frame_handle, const std::vector<df::file_path>& files,
	                         const std::vector<df::folder_path>& folders);

	// Test-only probe of the drag/clipboard IDataObject. Lets tests inspect which formats
	// are advertised (and in what order) and confirm that each file-bearing format
	// independently resolves to the cached items exactly once. Used to reason about
	// duplicate-import behaviour in third-party drop targets (e.g. Adobe Premiere).

	// Test-only probe of the double-buffered paint applied to native common controls. The buffering
	// relies on the control drawing itself on demand into a supplied device context. If a control
	// class ignored that request the control would blit an empty buffer and appear blank, so the
	// probe reports what each class actually drew into a pre-filled buffer.
	struct control_paint_probe
	{
		int trackbar_painted_pixels = 0; // pixels the trackbar changed from the pre-fill
		int trackbar_colors = 0; // distinct colors the trackbar left in the buffer
		int toolbar_painted_pixels = 0;
		int toolbar_colors = 0;
		int button_painted_pixels = 0;
		int button_colors = 0;
	};

	control_paint_probe probe_buffered_control_paint();

	// Test-only probe of the software renderer's tiled rasterisation. The backend replays the
	// retained scene once per fixed scratch tile, which is only sound while every primitive derives
	// its colour, coverage and source mapping from its own bounds and treats the clip purely as a
	// write mask. A primitive that read the clip instead would seam at tile edges, so the probe
	// draws one representative scene whole and again tile by tile and compares the two.
	struct software_tiling_probe
	{
		int painted_pixels = 0; // pixels the scene changed from the initial fill
		int mismatched_pixels = 0; // pixels where the tiled result differs from the untiled one
		int tiles = 0; // tiles the scene was rasterised in
		// The scratch tile stops being reallocated once it reaches its final size, so growing the
		// client no longer reallocates anything. These record how much of a grown client the canvas
		// will actually accept writes for, summed over the tiles the real loop walks, against the
		// buffer that stayed behind - which must not have grown with the window.
		int grown_client_pixels = 0;
		int grown_writable_pixels = 0;
		int grown_buffer_pixels = 0;
	};

	software_tiling_probe probe_software_tiling();


	using clipboard_data_ptr = std::shared_ptr<clipboard_data>;

	clipboard_data_ptr clipboard();

	void set_clipboard(const std::vector<df::file_path>& files, const std::vector<df::folder_path>& folders,
	                   const file_load_result& loaded, bool is_move);
	void set_clipboard(std::string_view text);
	std::string clipboard_text();

	class setting_file
	{
	public:
		virtual bool root_created() const = 0;

		virtual bool write(std::string_view section, std::string_view name, uint32_t v) = 0;
		virtual bool write(std::string_view section, std::string_view name, uint64_t v) = 0;
		virtual bool write(std::string_view section, std::string_view name, std::string_view v) = 0;
		virtual bool write(std::string_view section, std::string_view name, df::cspan cs) = 0;

		virtual bool read(std::string_view section, std::string_view name, uint32_t& v) const = 0;
		virtual bool read(std::string_view section, std::string_view name, uint64_t& v) const = 0;
		virtual bool read(std::string_view section, std::string_view name, std::string& v) const = 0;
		virtual bool read(std::string_view section, std::string_view name, uint8_t* data, size_t& len) const = 0;
	};

	using setting_file_ptr = std::shared_ptr<setting_file>;

	// The single, process-wide settings store: the backend (registry vs INI) is resolved once
	// and all access is synchronised. This is the store all persistence should use.
	setting_file_ptr settings();

	// Low-level settings backends. Prefer settings() above; these are exposed only so tests can
	// exercise each backend directly.
	setting_file_ptr create_registry_settings();
	setting_file_ptr create_ini_file_settings();
	setting_file_ptr create_ini_file_settings(df::folder_path folder);

	// Crash watchdog: a persisted marker recording that a graphics subsystem was active.
	// Set at safe times (subsystem start), cleared on clean shutdown; if a flag is still
	// set at the next launch the previous run crashed while that subsystem was in use, so
	// the app falls back (see apply_gpu_crash_guard). gpu_render disables GPU UI rendering;
	// hw_video_decode disables only hardware video decoding, leaving GPU rendering on.
	enum class crash_guard
	{
		gpu_render,
		hw_video_decode,
	};

	void set_crash_guard(crash_guard g, bool active);
	bool read_crash_guard(crash_guard g);
	void fail_crash_guard(crash_guard g);
	bool crash_guard_failed(crash_guard g);
	void suppress_crash_guard(crash_guard g, bool suppress);
	bool crash_guard_suppressed(crash_guard g);

	class mutex final : public df::no_copy
	{
		// Opaque OS lock (a Win32 SRWLOCK). Zero is its documented initialised state, so the type
		// is constant-initialised and needs neither a constructor nor a destructor.
		mutable uintptr_t _cs = 0;

	public:
		_Acquires_exclusive_lock_(this)
		void ex_lock() const;

		_Releases_exclusive_lock_(this)
		void ex_unlock() const;

		_Acquires_shared_lock_(this)
		void sh_lock() const;

		_Releases_shared_lock_(this)
		void sh_unlock() const;

		void unlock() const
		{
			ex_unlock();
		}

		void lock() const
		{
			ex_lock();
		}

		friend class shared_lock;
		friend class exclusive_lock;
	};

	class shared_lock final : public df::no_copy
	{
		const mutex& _rw;
		bool _locked = false;

	public:
		shared_lock(const mutex& rw) : _rw(rw)
		{
			lock();
		}

		~shared_lock() override
		{
			unlock();
		}

		void unlock()
		{
			if (_locked)
			{
				_rw.sh_unlock();
				_locked = false;
			}
		}

		void lock()
		{
			if (!_locked)
			{
				_rw.sh_lock();
				_locked = true;
			}
		}
	};

	class exclusive_lock final : public df::no_copy
	{
		const mutex& _rw;

	public:
		exclusive_lock(const mutex& rw) : _rw(rw)
		{
			_rw.ex_lock();
		}

		~exclusive_lock() override
		{
			_rw.ex_unlock();
		}
	};

	class thread_event
	{
	public:
		// Opaque OS event handle (a Win32 HANDLE), null until create() succeeds. Held as void*
		// rather than std::any so the noexcept signalling path can neither throw nor type-check.
		void* _h = nullptr;

		thread_event() = default;
		~thread_event();
		thread_event(bool manual_reset, bool initial_state);
		void create(bool manual_reset, bool initial_state);

		thread_event(const thread_event& other) = delete;
		void operator=(const thread_event& other) = delete;

		void reset() const noexcept;
		void set() const noexcept;
	};

	template <typename T>
	struct queue
	{
		mutex _rw;
		_Guarded_by_(_rw) std::deque<T> _storage;

		bool dequeue(T& result)
		{
			exclusive_lock lock_dec(_rw);
			if (_storage.empty()) return false;
			result = std::move(_storage.front());
			_storage.pop_front();
			return true;
		}

		bool enqueue(T f)
		{
			exclusive_lock lock_dec(_rw);
			const auto was_empty = _storage.empty();
			_storage.emplace_back(std::move(f));
			return was_empty;
		}

		// One lock and one empty-to-nonempty answer for a whole group, so a producer holding a batch
		// signals its consumer once rather than once per element.
		template <typename C>
		bool enqueue_all(C&& items)
		{
			exclusive_lock lock_dec(_rw);
			const auto was_empty = _storage.empty();
			for (auto&& i : items) _storage.emplace_back(std::move(i));
			return was_empty && !_storage.empty();
		}

		void reset_and_enqueue(T f)
		{
			// Superseded tasks own captured surfaces, item lists and shared state, so they are swapped
			// out and destroyed after the lock is released rather than run down inside it.
			std::deque<T> discarded;

			{
				exclusive_lock lock_dec(_rw);
				std::swap(discarded, _storage);
				_storage.emplace_back(std::move(f));
			}
		}

		std::deque<T> dequeue_all()
		{
			std::deque<T> result;

			{
				exclusive_lock lock_dec(_rw);
				std::swap(result, _storage);
			}

			return result;
		}

		bool empty()
		{
			shared_lock lock_dec(_rw);
			return _storage.empty();
		}
	};

	class task_queue
	{
	public:
		using task_t = std::function<void()>;
		queue<task_t> _q;
		thread_event _event;

		// Returned by delay_before_ready_ms when there is nothing to run, so the worker blocks on its
		// events rather than on a duration.
		static constexpr uint32_t wait_indefinitely = ~0u;

		task_queue() : _event(false, false)
		{
		}

		bool dequeue(task_t& result)
		{
			return _q.dequeue(result);
		}

		std::deque<task_t> dequeue_all()
		{
			return _q.dequeue_all();
		}

		void enqueue(task_t f)
		{
			// Signal only the empty-to-nonempty transition; the worker drains the complete batch.
			if (_q.enqueue(std::move(f)))
			{
				_event.set();
			}
		}

		void reset_and_enqueue(task_t f)
		{
			_q.reset_and_enqueue(std::move(f));
			_event.set();
		}

		// Trailing-edge debounce. A newer request pushes the deadline out, so a burst settles once, and
		// the worker spends the delay in its event wait rather than a task spending it asleep on the
		// thread - which is what stopped these queues sharing a worker with anything else.
		// The deadline belongs to the queue rather than the task, so only supersede-on-enqueue queues
		// should use this.
		void enqueue_after(const uint32_t delay_ms, task_t f)
		{
			_run_after_ms.store(df::now_ms() + delay_ms, std::memory_order_relaxed);
			_q.reset_and_enqueue(std::move(f));
			_event.set();
		}

		// 0 means drain now. Every queue that never calls enqueue_after answers 0 whenever it holds
		// anything, which is the behaviour it had before deadlines existed.
		uint32_t delay_before_ready_ms()
		{
			if (_q.empty()) return wait_indefinitely;

			const auto due = _run_after_ms.load(std::memory_order_relaxed);
			const auto now = df::now_ms();
			return due > now ? static_cast<uint32_t>(due - now) : 0;
		}

	private:
		// A df::now_ms stamp; zero leaves the queue ready as soon as it is signalled.
		std::atomic<int64_t> _run_after_ms = 0;
	};

	class threads
	{
		mutex _rw;
		std::vector<std::thread> _threads;
		// Latched by clear(), so a thread started on demand can never be created after the join.
		bool _stopped = false;

	public:
		~threads()
		{
			clear();
		}

		// Refuses to start once clear() has run. That refusal is what makes an on-demand start safe from
		// any thread: the caller learns no worker will service its queue, rather than leaking a thread
		// that outlives the objects it captured. There is deliberately no unchecked form.
		template <typename F>
		bool start_if_running(F&& f)
		{
			exclusive_lock lock_dec(_rw);
			if (_stopped) return false;
			_threads.emplace_back(std::thread(f));
			return true;
		}

		void clear()
		{
			std::vector<std::thread> threads;

			{
				exclusive_lock lock_dec(_rw);
				_stopped = true;
				std::swap(threads, _threads);
			}

			for (auto&& t : threads) t.join();
		}
	};

	class thread_init
	{
		uint32_t _hr = 0;

	public:
		thread_init();
		~thread_init();
	};

	// RAII helper that registers the current thread with the Multimedia Class
	// Scheduler Service (MMCSS) as a "Pro Audio" task so it is not starved by
	// heavier CPU work (notably video decoding) - starvation of the audio thread
	// lets the WASAPI ring underrun and replay its stale contents.
	class media_thread_priority
	{
		void* _task = nullptr;

	public:
		media_thread_priority();
		~media_thread_priority();
	};

	extern uint32_t wait_for_timeout;
	uint32_t wait_for(const std::vector<std::reference_wrapper<thread_event>>& events, uint32_t timeout_ms,
	                  bool wait_all);
	using attachments_t = std::vector<std::pair<std::string, df::file_path>>;

	enum class mapi_send_result
	{
		sent,
		canceled,
		failed
	};

	mapi_send_result classify_mapi_send_result(uint32_t result_code);
	mapi_send_result mapi_send(std::string_view to, std::string_view subject, std::string_view text,
	                           const attachments_t& attachments);
	void sync_app_window_enabled();
	uint32_t tick_count();
	uint32_t caret_blink_time();
	uint32_t current_thread_id();
	extern thread_event event_exit;

	// Claimed while this launch has not yet reached the user, and released once it has. Answers false
	// when another instance already holds it, which is a concurrent launch rather than a repeat of one.
	bool claim_startup_scope();
	void release_startup_scope();

	// Append-only arena for the interned string records. One reservation of virtual address space is
	// committed a block at a time, so every record keeps a stable offset from a base that never moves -
	// which is what lets str::cached be a 32-bit handle rather than a pointer.
	struct memory_pool
	{
		mutex cs;
		uint8_t* base = nullptr;
		size_t reserved = 0;
		size_t committed = 0;
		size_t used = 0;

		constexpr static size_t block_size = 1024_z * 1024_z; // commit granularity, and the largest record
		constexpr static size_t alignment = 4;

		// Address space only; pages are committed on demand, and alloc halves this until a reservation
		// succeeds. A 32-bit build gets 2GB in total (the exe is not large-address-aware), so it takes a
		// sixteenth rather than the half a gigabyte a 64-bit build can ignore; at roughly 30 bytes per
		// interned name that still covers a collection far larger than a 32-bit process can index.
		constexpr static size_t reserve_size = sizeof(void*) == 8
			                                      ? 1024_z * 1024_z * 1024_z
			                                      : 128_z * 1024_z * 1024_z;

		void* alloc(size_t size);
	};

	template <typename T>
	class pool_allocator
	{
		memory_pool _pool;
	};


	////////////////////////////////
	/// Network

	bool is_online();

	using web_params = std::vector<std::pair<std::string, std::string>>;

	enum class web_request_verb
	{
		POST,
		GET
	};

	struct web_request
	{
		std::string command;
		std::string path;

		web_params query;
		web_params headers;
		web_params form_data;

		std::string file_form_data_name;
		std::string file_name;
		df::file_path file_path;

		df::file_path download_file_path;

		web_request_verb verb = web_request_verb::GET;
	};

	struct web_response
	{
		std::string headers;
		std::string body;
		std::string content_type;
		int status_code = 0;
	};

	struct web_host;
	using web_host_ptr = std::shared_ptr<web_host>;

	web_host_ptr connect_to_host(std::string_view host, bool secure = true, int port = 0);
	web_response send_request(const web_host_ptr& host, const web_request& req);
}
