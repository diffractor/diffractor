// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2024  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

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
	extern size_t static_memory_usage;

	std::u8string OS();

	extern bool sse2_supported;
	extern bool crc32_supported;
	extern bool avx2_supported;
	extern bool avx512_supported;
	extern bool neon_supported;

	void trace(std::u8string_view message);
	void trace(const std::u8string& message);
	inline void trace(const std::string_view message) { trace(str::utf8_cast(message)); };

	df::unique_folders known_folders();
	local_folders_result local_folders();

	void set_desktop_wallpaper(df::file_path file_path);
	void show_file_properties(const std::vector<df::file_path>& files, const std::vector<df::folder_path>& folders);

	bool has_burner();
	void burn_to_cd(const std::vector<df::file_path>& files, const std::vector<df::folder_path>& folders);
	void print(const std::vector<df::file_path>& files, const std::vector<df::folder_path>& folders);
	void remove_metadata(const std::vector<df::file_path>& files, const std::vector<df::folder_path>& folders);

	std::u8string format_number(const std::u8string& num_text);
	std::u8string number_dec_sep();
	bool is_valid_file_name(const std::u8string_view name);

	struct file_attributes_t
	{
		bool is_readonly = false;
		bool is_offline = false;
		bool is_hidden = false;
		uint64_t modified = 0;
		uint64_t created = 0;
		uint64_t size = 0;
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
		std::u8string name;
		std::u8string vol_name;
		std::u8string file_system;

		df::file_size used;
		df::file_size free;
		df::file_size capacity;
	};

	using drives = std::vector<drive_t>;
	drives scan_drives(bool scan_contents);

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

	enum class file_open_mode
	{
		read,
		write,
		create,
		sequential_scan,
		read_write,
	};

	std::wstring to_file_system_path(df::file_path path);
	std::wstring to_file_system_path(df::folder_path path);

	enum class known_folder
	{
		running_app_folder,
		test_files_folder,
		app_data,
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
	ui::const_surface_ptr create_segoe_md2_icon(wchar_t ch);
	bool eject(df::folder_path path);

	enum class file_op_result_code
	{
		OK,
		CANCELLED,
		FAILED
	};

	struct file_op_result
	{
		file_op_result_code code = file_op_result_code::FAILED;
		std::u8string error_message;
		df::paths created_files;

		bool success() const
		{
			return code == file_op_result_code::OK;
		}

		bool failed() const
		{
			return code != file_op_result_code::OK;
		}

		std::u8string format_error(std::u8string_view text = {}, std::u8string_view more_text = {}) const;
	};

	file_op_result delete_items(const std::vector<df::file_path>& files, const std::vector<df::folder_path>& folders,
		bool allow_undo);
	file_op_result move_or_copy(const std::vector<df::file_path>& files, const std::vector<df::folder_path>& folders,
		df::folder_path target, bool is_move);

	file_op_result delete_file(df::file_path path);
	file_attributes_t file_attributes(df::file_path path);
	file_attributes_t file_attributes(df::folder_path path);
	file_op_result copy_file(df::file_path existing, df::file_path destination, bool fail_if_exists,
		bool can_create_folder);
	file_op_result move_file(df::file_path existing, df::file_path destination, bool fail_if_exists);
	file_op_result move_file(df::folder_path existing, df::folder_path destination);
	file_op_result replace_file(df::file_path destination, df::file_path existing, bool create_originals = false);
	bool exists(df::file_path path);
	bool exists(df::folder_path path);
	file_op_result create_folder(df::folder_path path);
	bool open(df::file_path path);
	bool open(std::u8string_view path);
	bool run(std::u8string_view cmd);
	void show_text_in_notepad(std::u8string_view s);
	void show_in_file_browser(df::file_path path);
	void show_in_file_browser(df::folder_path path);
	bool working_set(int64_t& current, int64_t& peak);
	df::folder_path temp_folder();
	int display_frequency();
	/*uint32_t file_attributes(const df::file_path path);
	uint32_t file_attributes(const df::folder_path path);*/
	df::file_path resolve_link(df::file_path path);
	bool created_date(df::file_path path, df::date_t dt);
	bool set_files_dates(df::file_path path, uint64_t dt_created, uint64_t dt_modified);

	struct metadata_result
	{
		std::optional<std::u8string> title;
		std::vector<std::u8string> tags;
		std::optional<int> rating;
	};

	df::blob from_file(df::file_path path);
	bool save_to_file(df::file_path path, df::cspan data);

	df::count_and_size calc_folder_summary(df::folder_path folder, bool show_hidden, const df::cancel_token& token);
	folder_contents iterate_file_items(df::folder_path folder, bool show_hidden);
	std::vector<folder_info> select_folders(const df::item_selector& selector, bool show_hidden);
	std::vector<file_info> select_files(const df::item_selector& selector, bool show_hidden);

	struct scan_result
	{
		bool success = false;
		df::file_path saved_file_path;
		std::u8string error_message;
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
	get_cached_file_properties_response get_shell_thumbnail(df::file_path path, ui::const_image_ptr& thumbnail_out);

	std::u8string user_name();
	std::u8string last_os_error();
	void set_thread_description(std::u8string_view name);
	df::file_path temp_file(std::u8string_view ext = u8".tmp"sv, df::folder_path folder = {});

	bool browse_for_folder(df::folder_path& path);
	bool prompt_for_save_path(df::file_path& path);

	std::u8string utf16_to_utf8(std::wstring_view text);
	std::wstring utf8_to_utf16(std::u8string_view text);

	std::string utf8_to_a(std::u8string_view utf8);

	void download_and_verify(bool test_version, const std::function<void(df::file_path)>& complete);
	file_op_result install(df::file_path installer_path, df::folder_path destination_folder, bool silent,
		bool run_app_after_install);


	// dates
	df::day_t to_date(uint64_t uint64);
	uint64_t from_date(const df::day_t& day);
	uint64_t utc_to_local(uint64_t ts);
	uint64_t local_to_utc(uint64_t ts);
	df::date_t dos_date_to_ts(uint16_t dos_date, uint16_t dos_time);
	std::u8string format_date_time(df::date_t d);
	std::u8string format_date(df::date_t d);
	std::u8string format_time(df::date_t d);
	df::date_t now();

	std::u8string user_language();


	struct open_with_entry
	{
		std::u8string name;
		std::function<bool(const std::vector<df::file_path>& files, const std::vector<df::folder_path>& folders)> invoke;
		int weight = 0;
	};

	std::vector<open_with_entry> assoc_handlers(std::u8string_view ext);

	enum class resource_item
	{
		logo,
		logo15,
		logo30,
		title,
		sql,
		map_html,
		map_png
	};

	df::blob load_resource(resource_item i);
	std::u8string resource_url(resource_item map_html);
	df::file_path running_app_path();
	bool is_server(std::u8string_view path);
	size_t calc_optimal_read_size(df::file_path path);

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
			std::u8string first_name;
			drop_effect preferred_drop_effect = drop_effect::none;
			int count = 0;
			bool has_readonly = false;
		};

		virtual bool has_drop_files() const = 0;
		virtual bool has_bitmap() const = 0;
		virtual description files_description() const = 0;
		virtual df::file_path first_path() const = 0;

		virtual file_op_result drop_files(df::folder_path save_path, drop_effect effect) = 0;
		virtual file_op_result save_bitmap(df::folder_path save_path, std::u8string_view name, bool as_png) = 0;
	};

	drop_effect perform_drag(std::any frame_handle, const std::vector<df::file_path>& files,
		const std::vector<df::folder_path>& folders);


	using clipboard_data_ptr = std::shared_ptr<clipboard_data>;

	clipboard_data_ptr clipboard();
	bool clipboard_has_files_or_image();

	void set_clipboard(const std::vector<df::file_path>& files, const std::vector<df::folder_path>& folders,
		const file_load_result& loaded, bool is_move);
	void set_clipboard(std::u8string_view text);

	class setting_file
	{
	public:
		virtual bool root_created() const = 0;

		virtual bool write(std::u8string_view section, std::u8string_view name, uint32_t v) = 0;
		virtual bool write(std::u8string_view section, std::u8string_view name, uint64_t v) = 0;
		virtual bool write(std::u8string_view section, std::u8string_view name, std::u8string_view v) = 0;
		virtual bool write(std::u8string_view section, std::u8string_view name, df::cspan cs) = 0;

		virtual bool read(std::u8string_view section, std::u8string_view name, uint32_t& v) const = 0;
		virtual bool read(std::u8string_view section, std::u8string_view name, uint64_t& v) const = 0;
		virtual bool read(std::u8string_view section, std::u8string_view name, std::u8string& v) const = 0;
		virtual bool read(std::u8string_view section, std::u8string_view name, uint8_t* data, size_t& len) const = 0;
	};

	using setting_file_ptr = std::shared_ptr<setting_file>;

	setting_file_ptr create_registry_settings();

	extern size_t static_memory_usage;

	class mutex : public df::no_copy
	{
		mutable uintptr_t _cs = 0;

	public:
		mutex();
		~mutex();

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

	class shared_lock : public df::no_copy
	{
		const mutex& _rw;
		bool _locked = false;

	public:
		shared_lock(const mutex& rw) : _rw(rw)
		{
			lock();
		}

		~shared_lock()
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

	class exclusive_lock : public df::no_copy
	{
		const mutex& _rw;

	public:
		exclusive_lock(const mutex& rw) : _rw(rw)
		{
			_rw.ex_lock();
		}

		~exclusive_lock()
		{
			_rw.ex_unlock();
		}
	};

	class thread_event
	{
	public:
		std::any _h;

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

		void enqueue(T f)
		{
			exclusive_lock lock_dec(_rw);
			_storage.emplace_back(std::move(f));
		}

		void reset_and_enqueue(T f)
		{
			exclusive_lock lock_dec(_rw);
			_storage.clear();
			_storage.emplace_back(std::move(f));
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
	};

	class task_queue
	{
	public:
		using task_t = std::function<void()>;
		queue<task_t> _q;
		thread_event _event;

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
			_q.enqueue(std::move(f));
			_event.set();
		}

		void reset_and_enqueue(task_t f)
		{
			_q.reset_and_enqueue(std::move(f));
			_event.set();
		}
	};

	class threads
	{
	private:
		mutex _rw;
		std::vector<std::thread> _threads;

	public:
		~threads()
		{
			clear();
		}

		template <typename F>
		void start(F&& f)
		{
			exclusive_lock lock_dec(_rw);
			_threads.emplace_back(std::thread(f));
		}

		void clear()
		{
			std::vector<std::thread> threads;

			{
				exclusive_lock lock_dec(_rw);
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

	extern uint32_t wait_for_timeout;
	uint32_t wait_for(const std::vector<std::reference_wrapper<thread_event>>& events, uint32_t timeout_ms,
		bool wait_all);
	using attachments_t = std::vector<std::pair<std::u8string, df::file_path>>;
	bool mapi_send(std::u8string_view to, std::u8string_view subject, std::u8string_view text,
		const attachments_t& attachments);
	uint32_t tick_count();
	uint32_t current_thread_id();
	extern thread_event event_exit;

	struct memory_pool
	{
		mutex cs;
		uint8_t* next_free = nullptr;
		uint8_t* block_limit = nullptr;

		constexpr static size_t block_size = 1024_z * 1024_z;
		constexpr static size_t alignment = 4;

		void* alloc(size_t size);
	};

	template <typename T>
	class pool_allocator
	{
	private:
		memory_pool _pool;

	public:
		void deallocate(T* const p, const size_t count)
		{
			// no-op
		}

		T* allocate(const size_t count)
		{
			return static_cast<T*>(_pool.alloc(sizeof(T) * count));
		}
	};


	////////////////////////////////
	/// Network

	bool is_online();

	using web_params = std::vector<std::pair<std::u8string, std::u8string>>;

	enum class web_request_verb
	{
		POST,
		GET
	};

	struct web_request
	{
		std::u8string host;
		std::u8string command;
		std::u8string path;

		web_params query;
		web_params headers;
		web_params form_data;

		std::u8string file_form_data_name;
		std::u8string file_name;
		df::file_path file_path;

		df::file_path download_file_path;

		web_request_verb verb = web_request_verb::GET;
		bool secure = true;
		int port = 0;
	};

	struct web_response
	{
		std::u8string headers;
		std::u8string body;
		std::u8string content_type;
		int status_code = 0;
	};

	web_response send_request(const web_request& req);
	ui::surface_ptr image_to_surface(df::cspan image_buffer_in, sizei target_extent);
}
