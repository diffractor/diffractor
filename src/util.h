// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Core utility types and functions. Defines fundamental types (file_size, date_t, blob),
// memory helpers, logging, session-aggregate performance diagnostics, and common utility functions
// used throughout the application.

#pragma once

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#define COMPILE_SIMD_INTRINSIC
#endif

#if defined(_MSC_VER) && defined(_M_ARM64)
#define COMPILE_ARM_INTRINSIC
#endif

using namespace std::literals;

constexpr std::size_t operator "" _z(const unsigned long long n)
{
	return static_cast<std::size_t>(n);
}

// Move-semantics contracts. std::is_move_constructible_v is also satisfied by a copy constructor,
// so it proves nothing; declare the role a type plays instead.
//
//   df_assert_pod       passed and stored by value, no move required.
//   df_assert_movable   relocation is noexcept, so container growth moves instead of copying.
//   df_assert_move_only as movable, and a missing std::move fails to compile rather than
//                       silently deep-copying a decoded buffer or scan result.
//
// df_assert_movable cannot be used on df::hash_map / df::hash_set or anything containing one:
// the MSVC unordered containers do not declare a noexcept move constructor.
#define df_assert_pod(T) \
	static_assert(std::is_trivially_copyable_v<T>, #T " must remain trivially copyable")

#define df_assert_movable(T) \
	static_assert(std::is_nothrow_move_constructible_v<T>, #T " must be nothrow move constructible"); \
	static_assert(std::is_nothrow_move_assignable_v<T>, #T " must be nothrow move assignable")

#define df_assert_move_only(T) \
	df_assert_movable(T); \
	static_assert(!std::is_copy_constructible_v<T>, #T " must not be copy constructible"); \
	static_assert(!std::is_copy_assignable_v<T>, #T " must not be copy assignable")

// The message is stored rather than passed to a base constructor: std::exception(const char*)
// is an MSVC extension and does not exist in libstdc++.
class app_exception final : public std::exception
{
public:
	explicit app_exception(std::string message) : _message(std::move(message))
	{
	}

	explicit app_exception(const char* message) : _message(message)
	{
	}

	const char* what() const noexcept override
	{
		return _message.c_str();
	}

private:
	std::string _message;
};

struct text_t
{
	text_t() = default;

	text_t(const std::string_view t) : text(t)
	{
	}

	// A string literal needs its own constructor: reaching text_t through std::string_view is a
	// second user-defined conversion, which MSVC accepts and conforming compilers reject.
	text_t(const char* t) : text(t)
	{
	}

	operator std::string_view() const
	{
		return sv();
	}

	std::string_view sv() const
	{
		return trans.empty() ? text : trans;
	}

	void clear()
	{
		trans.clear();
	}

	std::string_view text;
	std::string trans;
};

namespace df
{
	constexpr uint32_t sixty_four_k = 1024 * 64;
	constexpr uint32_t two_fifty_six_k = 1024 * 256;
	constexpr uint32_t one_meg = 1024 * 1024;
	constexpr uint32_t one_mega_pixel = 1024 * 1024;
	constexpr double golden_ratio = 1.61803399;
	constexpr uint64_t max_file_load_size = 1024ull * 1024ull * 16ull;
	constexpr uint32_t max_thumbnails_to_display = 5u;

	// Encoded thumbnails held across the whole result set. Fixed rather than scaled to the machine:
	// what it buys is scroll-back without a database hop, and one screen of scroll-back is the same
	// amount of work everywhere. At the 320x256 thumbnail size this is a few thousand items.
	constexpr size_t max_thumbnail_bytes = 32ull * 1024ull * 1024ull;

	class folder_path;
	class file_path;
	class date_t;
	class file_size;

	// The macro prevents release builds from evaluating assertion expressions.
#ifdef _DEBUG
	constexpr void assert_true_impl(const bool should_be_true)
	{
		if (!should_be_true)
		{
			__debugbreak();
		}
	}
#define assert_true(should_be_true) assert_true_impl(should_be_true)
#else
	constexpr void assert_true_impl()
	{
	}

#define assert_true(should_be_true) assert_true_impl()
#endif //_DEBUG

	struct free_delete
	{
		void operator()(void* x) const { _aligned_free(x); }
	};

	template <typename T>
	using unique_alloc_ptr = std::unique_ptr<T, free_delete>;

	template <typename T>
	unique_alloc_ptr<T> unique_alloc(const size_t alloc_size)
	{
		if (alloc_size > std::numeric_limits<size_t>::max() - 16) return {};
		return std::unique_ptr<T, free_delete>(static_cast<T*>(_aligned_malloc(alloc_size + 16, 16)));
	}

	template <typename T>
	class releaser
	{
	public:
		releaser(T* ptr, std::function<void(T*)> destroy_func)
			: ptr_(ptr), destroy_func_(std::move(destroy_func))
		{
		} // Move the function for efficiency

		~releaser() { destroy_func_(ptr_); }

		T* get() const { return ptr_; }

		// Disable copying and moving (same as before)
		releaser(const releaser&) = delete;
		releaser& operator=(const releaser&) = delete;
		releaser(releaser&&) = delete;
		releaser& operator=(releaser&&) = delete;

	private:
		T* ptr_;
		std::function<void(T*)> destroy_func_;
	};

	// Zero-overhead scope guard: no std::function, no allocation, inlinable.
	template <typename F>
	class scope_exit
	{
	public:
		explicit scope_exit(F f) : _f(std::move(f))
		{
		}

		~scope_exit() { _f(); }

		scope_exit(const scope_exit&) = delete;
		scope_exit& operator=(const scope_exit&) = delete;
		scope_exit(scope_exit&&) = delete;
		scope_exit& operator=(scope_exit&&) = delete;

	private:
		F _f;
	};

	extern std::atomic_bool is_closing;
	extern std::atomic_int file_handles_detached;
	extern std::atomic_int jobs_running;
	extern std::atomic_int loading_media;
	extern std::atomic_int command_active;
	extern std::atomic_int dragging_items;
	extern std::atomic_int handling_crash;
	// Written by whichever thread is inside the draw backend, read by the UI debug panel and by the
	// crash handler on its own thread, so the name of the last function entered is a published value.
	extern std::atomic<const char*> rendering_func;
	extern std::string gpu_desc;
	extern std::string gpu_id;
	extern std::string d3d_info;
	// Budgets for a decoded image, published once by the draw backend when it creates its device and
	// read only on the UI thread when a decode is sized. Defaults suit a feature level 11 device.
	extern int max_texture_dimension;
	extern int64_t max_texture_bytes;
	extern int64_t max_decode_bytes;
	extern date_t start_time;
	extern file_path last_loaded_path;
	extern file_path previous_log_path;
	extern file_path log_path;

	void log(std::string_view context, std::string_view message);
	// For messages a codec can emit once per scanned item. Each distinct message reaches the log once.
	void log_once(std::string_view context, std::string_view message);
	void trace(std::string_view message);
	file_path close_log();
	std::string format_version(bool short_text);

	double now();
	int64_t now_ms();
	int64_t now_us();

	// Session-aggregate performance diagnostics. Counters only: they publish no state, nothing
	// branches on their value, and they are read once at exit after the worker queues have drained.
	// Increments are relaxed and arrive from every execution context, so no single-context owner
	// exists and no ordering guarantee is needed - only the totals matter.

	inline void bump(std::atomic_uint64_t& counter, const uint64_t n = 1)
	{
		counter.fetch_add(n, std::memory_order_relaxed);
	}

	inline void bump(std::atomic_uint32_t& counter)
	{
		counter.fetch_add(1, std::memory_order_relaxed);
	}

	inline void record_peak(std::atomic_uint32_t& peak, const uint32_t value)
	{
		auto current = peak.load(std::memory_order_relaxed);
		while (value > current && !peak.compare_exchange_weak(current, value, std::memory_order_relaxed))
		{
		}
	}

	// Totals answer "where did the session spend its time"; the paired maximum answers "did any
	// single occurrence stall a thread", which an average hides.
	class perf_timer
	{
		std::atomic_uint64_t& _total_us;
		std::atomic_uint32_t* const _max_us;
		const int64_t _start = now_us();

	public:
		explicit perf_timer(std::atomic_uint64_t& total_us, std::atomic_uint32_t* max_us = nullptr) noexcept
			: _total_us(total_us), _max_us(max_us)
		{
		}

		~perf_timer()
		{
			const auto elapsed = static_cast<uint64_t>(now_us() - _start);
			bump(_total_us, elapsed);
			if (_max_us) record_peak(*_max_us, static_cast<uint32_t>(std::min<uint64_t>(elapsed, UINT32_MAX)));
		}

		perf_timer(const perf_timer&) = delete;
		perf_timer& operator=(const perf_timer&) = delete;
		perf_timer(perf_timer&&) = delete;
		perf_timer& operator=(perf_timer&&) = delete;
	};

	struct thumbnail_counters
	{
		std::atomic_uint32_t scan_batches = 0;
		std::atomic_uint32_t scan_batches_cancelled = 0;
		std::atomic_uint32_t scan_batches_queued = 0;
		std::atomic_uint32_t scan_batches_pending = 0;
		std::atomic_uint32_t scan_batches_pending_peak = 0;
		std::atomic_uint64_t scan_thumbs_requested = 0;
		std::atomic_uint64_t scan_thumbs_scanned = 0;
		std::atomic_uint64_t scan_completions_stale = 0;
		std::atomic_uint64_t stage_requests = 0;
		std::atomic_uint64_t stage_skipped = 0;
		std::atomic_uint64_t stage_coalesced = 0;
		std::atomic_uint64_t stage_decodes = 0;
		std::atomic_uint64_t stage_discarded = 0;
		std::atomic_uint64_t published_db = 0;
		std::atomic_uint64_t published_shell = 0;
		std::atomic_uint64_t shell_retries = 0;
		std::atomic_uint64_t load_failures = 0;
	};

	// UI-thread cost. Everything here competes with input handling, so the maxima matter more than
	// the totals: one long drain or paint is a visible stutter.
	struct ui_counters
	{
		std::atomic_uint64_t idle_drains = 0;
		std::atomic_uint64_t idle_tasks = 0;
		std::atomic_uint64_t idle_us = 0;
		std::atomic_uint32_t idle_max_us = 0;
		std::atomic_uint32_t idle_batch_peak = 0;
		std::atomic_uint64_t paints = 0;
		std::atomic_uint64_t paint_us = 0;
		std::atomic_uint32_t paint_max_us = 0;
		// on_render only: walking the view tree and issuing draw calls, with no GPU work in it. Paint
		// time is this plus submit plus present, so measuring it directly says which of the three to
		// attack rather than leaving it to subtraction.
		std::atomic_uint64_t scene_build_us = 0;
		std::atomic_uint32_t scene_build_max_us = 0;
		std::atomic_uint64_t texture_uploads = 0;
		// What asked for a frame. prepares is the display-frequency timer, invalidates is anything
		// that dirtied a window and so will rebuild the scene, and redraws is the re-present path that
		// replays the last scene instead. Paints far above prepares means frames are being driven by
		// something other than the pacing.
		std::atomic_uint64_t frame_prepares = 0;
		std::atomic_uint64_t invalidates = 0;
		std::atomic_uint64_t redraws = 0;
		// Which source kept asking for the next frame. An idle app should leave all three at zero;
		// anything else is an animation that never settles.
		std::atomic_uint64_t prepares_registered_anim = 0;
		std::atomic_uint64_t prepares_display_anim = 0;
		std::atomic_uint64_t prepares_display_invalid = 0;
		std::atomic_uint32_t animations_peak = 0;
	};

	// Direct3D frame cost, all bumped from the UI thread by the hardware backend. A user's log is
	// the only place this is ever observable - a profiler cannot be attached to their machine - so
	// it records what a frame cost, how well it batched, and what it had to build to draw it.
	struct gpu_counters
	{
		std::atomic_uint64_t frames = 0; // draw_scene calls; a redraw replays a scene without a paint
		std::atomic_uint64_t submit_us = 0;
		std::atomic_uint32_t submit_max_us = 0;
		std::atomic_uint64_t present_us = 0;
		std::atomic_uint32_t present_max_us = 0;

		// draws vs merged is the batching ratio: every merge is a draw call and a state block that
		// building the scene managed to avoid.
		std::atomic_uint64_t draws = 0;
		std::atomic_uint64_t merged = 0;
		std::atomic_uint32_t draws_peak = 0;
		std::atomic_uint64_t geometry_bytes = 0;

		std::atomic_uint64_t shader_binds = 0;
		std::atomic_uint64_t view_binds = 0;
		std::atomic_uint64_t sampler_binds = 0;
		std::atomic_uint64_t cbuffer_uploads = 0;

		// Driver object creation. These should flatten out early in a session; totals that keep pace
		// with the frame count are the signature of something being rebuilt every frame.
		std::atomic_uint64_t views_created = 0;
		std::atomic_uint64_t targets_created = 0;
		std::atomic_uint64_t textures_created = 0;
		std::atomic_uint64_t buffers_created = 0;

		// Gauge, sampled periodically: what the adapter reports this process is using locally.
		std::atomic_uint32_t vram_mb = 0;
		std::atomic_uint32_t vram_peak_mb = 0;
	};

	struct db_counters
	{
		std::atomic_uint64_t read_batches = 0;
		std::atomic_uint64_t thumbnails_read = 0;
		std::atomic_uint64_t read_us = 0;
		std::atomic_uint32_t read_max_us = 0;
		std::atomic_uint64_t write_batches = 0;
		std::atomic_uint64_t items_written = 0;
		std::atomic_uint64_t thumbs_written = 0;
		std::atomic_uint64_t write_us = 0;
		std::atomic_uint32_t write_max_us = 0;
	};

	// Query work is split because the two halves run on different threads: matching on a worker,
	// materializing into item_elements on the UI thread where its cost is felt directly.
	struct query_counters
	{
		std::atomic_uint64_t queries = 0;
		std::atomic_uint64_t query_us = 0;
		std::atomic_uint32_t query_max_us = 0;
		std::atomic_uint64_t query_items = 0;
		std::atomic_uint64_t materializations = 0;
		std::atomic_uint64_t materialize_us = 0;
		std::atomic_uint32_t materialize_max_us = 0;
		std::atomic_uint64_t materialize_items = 0;
		std::atomic_uint64_t counts = 0;
		std::atomic_uint64_t count_us = 0;
	};

	struct file_counters
	{
		std::atomic_uint64_t scans = 0;
		std::atomic_uint64_t scan_us = 0;
		std::atomic_uint32_t scan_max_us = 0;
		// Full-image loads for display, one per step through a folder, and the slowest thing the load
		// queue does - a backlog here is what leaves the viewer blank.
		std::atomic_uint64_t loads = 0;
		std::atomic_uint64_t load_us = 0;
		std::atomic_uint32_t load_max_us = 0;
		std::atomic_uint64_t decodes = 0;
		std::atomic_uint64_t decode_us = 0;
		std::atomic_uint32_t decode_max_us = 0;
		std::atomic_uint64_t decode_bytes = 0;
		// Metadata parse failures on per-item paths, counted rather than logged so a bad batch of
		// files cannot flood the log with one line each.
		std::atomic_uint64_t metadata_errors = 0;
	};

	// Content hashing for duplicate detection. A CRC costs a full read and a perceptual hash costs a
	// read and a decode, so what these answer is how much of that work was avoidable: a hash computed
	// for a path that already carried one, or held by a picture that could never have matched.
	struct index_counters
	{
		std::atomic_uint64_t crc_computed = 0;
		std::atomic_uint64_t crc_failed = 0;
		std::atomic_uint64_t crc_bytes = 0;
		std::atomic_uint64_t crc_us = 0;
		std::atomic_uint32_t crc_max_us = 0;

		std::atomic_uint64_t phash_computed = 0;
		std::atomic_uint64_t phash_usable = 0;
		std::atomic_uint64_t phash_declined = 0;
		std::atomic_uint64_t phash_unreadable = 0;
		// Hashed, but nothing retained the result - no index item at that path, or a database update
		// that matched no row - so the next pass over the same file pays for the decode again.
		std::atomic_uint64_t phash_unpersisted = 0;
		std::atomic_uint64_t phash_unwritten = 0;
		std::atomic_uint64_t phash_presence = 0;
		std::atomic_uint64_t phash_bytes = 0;
		std::atomic_uint64_t phash_us = 0;
		std::atomic_uint32_t phash_max_us = 0;

		// Gauges, not totals: each holds what the most recent predictions pass saw. Summing passes
		// would count one picture once per pass and hide whether the candidate rule is holding.
		std::atomic_uint32_t pass_files = 0;
		std::atomic_uint32_t pass_crc_held = 0;
		std::atomic_uint32_t pass_dup_groups = 0;
		std::atomic_uint32_t pass_pictures = 0;
		std::atomic_uint32_t pass_buckets = 0;
		std::atomic_uint32_t pass_candidates = 0;
		std::atomic_uint32_t pass_wanted = 0;
		std::atomic_uint32_t pass_usable_held = 0;
		std::atomic_uint32_t pass_declined_held = 0;
		std::atomic_uint32_t pass_uninvited = 0;
		std::atomic_uint32_t pass_matched = 0;
		std::atomic_uint32_t pass_crowded = 0;

		// The shape narrowing applied alongside the shared capture time. Solo counts pictures refused
		// because nothing under their timestamp shares their shape; the swap variant is the one the gate
		// actually applies, because a quarter turn transposes the stored extent.
		std::atomic_uint32_t pass_dims_unknown = 0;
		std::atomic_uint32_t pass_aspect_solo = 0;
		std::atomic_uint32_t pass_aspect_solo_swap = 0;
		std::atomic_uint32_t pass_matched_cross_aspect = 0;
	};

	inline void set_gauge(std::atomic_uint32_t& gauge, const uint32_t value)
	{
		gauge.store(value, std::memory_order_relaxed);
	}

	// One slot per worker queue, claimed once by its worker thread at startup so the dispatch loop
	// can account tasks without knowing which queue it is draining.
	// Aligned because the slots are an array whose elements have different owners: unpadded, two
	// queues share a line and every worker's per-task bump invalidates the other's copy.
	struct alignas(std::hardware_destructive_interference_size) queue_counters
	{
		std::string_view name;
		std::atomic_uint64_t tasks = 0;
		std::atomic_uint64_t busy_us = 0;
		std::atomic_uint32_t task_max_us = 0;
		std::atomic_uint32_t batches = 0;
		std::atomic_uint32_t batch_peak = 0;
	};

	// Never null: extra queues share an overflow slot rather than forcing a null check into the
	// dispatch loop. Name must outlive the process (a literal).
	queue_counters* register_queue(std::string_view name);

	extern thumbnail_counters thumbnail_perf;
	extern ui_counters ui_perf;
	extern gpu_counters gpu_perf;
	extern db_counters db_perf;
	extern query_counters query_perf;
	extern file_counters file_perf;
	extern index_counters index_perf;

	// Writes the whole-session summary as one grouped block, or nothing at all when the app did no
	// measurable work, so an idle run adds no log noise.
	void log_perf_summary();

	class measure_ms
	{
		int& _ms;
		int64_t _start = 0;

	public:
		measure_ms(int& ms) : _ms(ms), _start(now_ms())
		{
		}

		~measure_ms()
		{
			const auto t = now_ms() - _start;
			// average if existing value
			_ms = static_cast<int>(_ms == 0 ? t : (_ms + t) / 2);
		}
	};

	constexpr uint32_t byte_clamp(const int n)
	{
		return n > 255 ? 255u : n < 0 ? 0u : static_cast<uint32_t>(n);
	}

	constexpr uint32_t byte_clamp(const uint32_t n)
	{
		return n > 255u ? 255u : n;
	}

	constexpr int round_up(const int i, const int d)
	{
		return i % d ? i / d + 1 : i / d;
	}

	inline int round_up(const float d)
	{
		if (!std::isnormal(d)) return 0;
		return static_cast<int>(d < 0.0f ? std::floor(d) : std::ceil(d));
	}

	// Round half away from zero. The hand-rolled form this replaced inverted negatives:
	// round(-100.6) gave -100 and round(-100.4) gave -101.
	inline int32_t round(const double d)
	{
		if (!std::isnormal(d)) return 0;
		return static_cast<int32_t>(std::round(d));
	}

	inline int64_t round64(const double d)
	{
		if (!std::isnormal(d)) return 0;
		return static_cast<int64_t>(std::round(d));
	}

	inline int round(const float d)
	{
		if (!std::isnormal(d)) return 0;
		return static_cast<int>(std::round(d));
	}

	constexpr int round(const int i, const int d)
	{
		return (i + d / 2) / d;
	}

	constexpr uint64_t round64(const uint64_t i, const uint64_t d)
	{
		return (i + d / 2) / d;
	}

	constexpr int64_t round64(const int64_t i, const int64_t d)
	{
		return (i + d / 2) / d;
	}

	constexpr bool in_range(const uint8_t* section, const size_t section_size, const uint8_t* limit,
	                        const size_t limit_size)
	{
		return section >= limit && section + section_size <= limit + limit_size;
	}

	// Returns true if x is in range [low..high], else false 
	constexpr bool in_range(const int low, const int high, const int x)
	{
		return low <= x && high >= x;
	}

	constexpr int64_t mul_div(const int64_t n64, const int64_t num64, const int64_t den64)
	{
		return den64 ? (n64 * num64 + den64 / 2) / den64 : -1;
	}

	constexpr int mul_div(const int32_t n, const int32_t num, const int32_t den)
	{
		if (in_range(INT16_MIN, INT16_MAX, n) &&
			in_range(INT16_MIN, INT16_MAX, num))
		{
			return den ? (n * num + den / 2) / den : -1;
		}

		const int64_t n64 = n;
		const int64_t num64 = num;
		const int64_t den64 = den;

		return static_cast<int>(mul_div(n64, num64, den64));
	}

	template <typename FloatingPoint>
	constexpr FloatingPoint fabs(FloatingPoint x)
	{
		return x >= 0 ? x : x < 0 ? -x : 0;
	}

	constexpr bool equiv(const double x, const double y, const double epsilon = std::numeric_limits<double>::epsilon())
	{
		return fabs(x - y) <= epsilon;
	}

	constexpr bool equiv(const float x, const float y)
	{
		return fabs(x - y) <= std::numeric_limits<float>::epsilon();
	}

	constexpr bool is_zero(const float x)
	{
		return fabs(x) <= std::numeric_limits<float>::epsilon();
	}

	constexpr bool is_zero(const double x)
	{
		return fabs(x) <= std::numeric_limits<double>::epsilon();
	}

	struct no_copy
	{
		virtual ~no_copy() = default;
		no_copy(const no_copy&) = delete;
		no_copy& operator=(const no_copy&) = delete;
		no_copy() = default;
	};

	static constexpr int max_blob_size = 1024 * 1024 * 100;

	// Skips value-initialization. Blob buffers are sized then immediately overwritten by a
	// read or decode, so the implicit zero-fill is a wasted pass over every byte.
	// Any blob sized this way MUST be fully written, or trimmed to what was written.
	template <typename T>
	struct default_init_allocator : std::allocator<T>
	{
		using std::allocator<T>::allocator;

		template <typename U>
		struct rebind
		{
			using other = default_init_allocator<U>;
		};

		template <typename U>
		void construct(U* p) noexcept(std::is_nothrow_default_constructible_v<U>)
		{
			::new(static_cast<void*>(p)) U;
		}

		template <typename U, typename... Args>
		void construct(U* p, Args&&... args)
		{
			std::allocator_traits<std::allocator<T>>::construct(
				static_cast<std::allocator<T>&>(*this), p, std::forward<Args>(args)...);
		}
	};

	// A move-only byte buffer. Blobs hold decoded images, file contents and metadata blocks, so an
	// accidental copy is an unbounded allocation plus a full memcpy on a hot path. Copying is spelled
	// clone() so it cannot happen silently; everything else forwards to the underlying vector.
	class blob
	{
	public:
		using storage = std::vector<uint8_t, default_init_allocator<uint8_t>>;
		using value_type = uint8_t;
		using size_type = storage::size_type;
		using iterator = storage::iterator;
		using const_iterator = storage::const_iterator;

		blob() noexcept = default;
		~blob() noexcept = default;

		blob(blob&&) noexcept = default;
		blob& operator=(blob&&) noexcept = default;

		blob(const blob&) = delete;
		blob& operator=(const blob&) = delete;

		explicit blob(const size_type n) : _v(n)
		{
		}

		blob(const size_type n, const uint8_t fill) : _v(n, fill)
		{
		}

		blob(const std::initializer_list<uint8_t> il) : _v(il)
		{
		}

		template <typename Iter>
		blob(Iter first, Iter last) : _v(first, last)
		{
		}

		blob clone() const
		{
			blob result;
			result._v = _v;
			return result;
		}

		uint8_t* data() noexcept { return _v.data(); }
		const uint8_t* data() const noexcept { return _v.data(); }
		size_type size() const noexcept { return _v.size(); }
		size_type capacity() const noexcept { return _v.capacity(); }
		bool empty() const noexcept { return _v.empty(); }

		iterator begin() noexcept { return _v.begin(); }
		iterator end() noexcept { return _v.end(); }
		const_iterator begin() const noexcept { return _v.begin(); }
		const_iterator end() const noexcept { return _v.end(); }
		const_iterator cbegin() const noexcept { return _v.cbegin(); }
		const_iterator cend() const noexcept { return _v.cend(); }

		uint8_t& operator[](const size_type i) noexcept { return _v[i]; }
		const uint8_t& operator[](const size_type i) const noexcept { return _v[i]; }
		uint8_t& front() noexcept { return _v.front(); }
		const uint8_t& front() const noexcept { return _v.front(); }
		uint8_t& back() noexcept { return _v.back(); }
		const uint8_t& back() const noexcept { return _v.back(); }

		void clear() noexcept { _v.clear(); }
		void resize(const size_type n) { _v.resize(n); }
		void resize(const size_type n, const uint8_t fill) { _v.resize(n, fill); }
		void reserve(const size_type n) { _v.reserve(n); }
		void shrink_to_fit() { _v.shrink_to_fit(); }
		void push_back(const uint8_t v) { _v.push_back(v); }
		void pop_back() { _v.pop_back(); }
		void swap(blob& other) noexcept { _v.swap(other._v); }

		template <typename... Args>
		auto insert(Args&&... args) { return _v.insert(std::forward<Args>(args)...); }

		template <typename... Args>
		auto erase(Args&&... args) { return _v.erase(std::forward<Args>(args)...); }

		template <typename... Args>
		void assign(Args&&... args) { _v.assign(std::forward<Args>(args)...); }

		friend bool operator==(const blob& a, const blob& b) { return a._v == b._v; }
		friend bool operator!=(const blob& a, const blob& b) { return a._v != b._v; }

	private:
		storage _v;
	};

	struct cspan
	{
		const uint8_t* data = nullptr;
		size_t size = 0;

		cspan() noexcept = default;
		~cspan() noexcept = default;
		cspan(const cspan&) noexcept = default;
		cspan& operator=(const cspan&) noexcept = default;
		cspan(cspan&&) noexcept = default;
		cspan& operator=(cspan&&) noexcept = default;

		cspan(const uint8_t* d, const size_t s) noexcept : data(d), size(s)
		{
		}

		cspan(const blob& b) noexcept : data(b.data()), size(b.size())
		{
		}

		bool operator>(const size_t other) const
		{
			return size > other;
		}

		bool operator>(const int other) const
		{
			return static_cast<int>(size) > other;
		}

		const uint8_t* begin() const
		{
			return data;
		}

		const uint8_t* end() const
		{
			return data + size;
		}

		cspan sub(const size_t pos) const
		{
			// Clamped: an over-long pos would otherwise wrap size and answer a span past the end.
			const auto n = pos < size ? pos : size;
			cspan result;
			result.data = data + n;
			result.size = size - n;
			return result;
		}

		bool empty() const
		{
			return size == 0 || data == nullptr;
		}
	};

	blob blob_from_file(file_path path, size_t max_load = max_blob_size);
	bool blob_save_to_file(cspan data, file_path path);

	struct span
	{
		uint8_t* data = nullptr;
		size_t size = 0;

		span() noexcept = default;
		~span() noexcept = default;
		span(const span&) noexcept = default;
		span& operator=(const span&) noexcept = default;
		span(span&&) noexcept = default;
		span& operator=(span&&) noexcept = default;

		span(uint8_t* d, const size_t s) noexcept : data(d), size(s)
		{
		}

		span(blob& b) noexcept : data(b.data()), size(b.size())
		{
		}

		bool operator>(const size_t other) const
		{
			return size > other;
		}

		bool operator>(const int other) const
		{
			return static_cast<int>(size) > other;
		}

		span sub(const size_t pos) const
		{
			// Clamped: an over-long pos would otherwise wrap size and answer a span past the end.
			const auto n = pos < size ? pos : size;
			span result;
			result.data = data + n;
			result.size = size - n;
			return result;
		}

		operator bool() const
		{
			return size > 0 && data;
		}

		operator cspan() const
		{
			return {data, size};
		}

	private:
		span operator+(int) const;
	};

#pragma pack(push, 1)

	struct xy8
	{
		int16_t x, y;

		xy8() noexcept = default;
		constexpr xy8& operator=(const xy8&) noexcept = default;
		constexpr xy8& operator=(xy8&&) noexcept = default;
		constexpr xy8(const xy8&) noexcept = default;
		constexpr xy8(xy8&&) noexcept = default;

		constexpr xy8(const uint8_t xx, const uint8_t yy) : x(xx), y(yy)
		{
		}

		constexpr static xy8 make(const uint8_t xx, const uint8_t yy)
		{
			const xy8 result = {xx, yy};
			return result;
		}

		constexpr bool operator ==(const xy8 other) const
		{
			return x == other.x && y == other.y;
		}

		constexpr bool operator !=(const xy8 other) const
		{
			return x != other.x || y != other.y;
		}

		static xy8 parse(std::string_view r);
		std::string str() const;
	};


#pragma pack(pop)

	struct xy16
	{
		int16_t x, y;

		xy16() noexcept = default;
		constexpr xy16& operator=(const xy16&) noexcept = default;
		constexpr xy16& operator=(xy16&&) noexcept = default;
		constexpr xy16(const xy16&) noexcept = default;
		constexpr xy16(xy16&&) noexcept = default;

		constexpr xy16(const int16_t xx, const int16_t yy) : x(xx), y(yy)
		{
		}

		constexpr static xy16 make(const int16_t xx, const int16_t yy) noexcept
		{
			const xy16 result = {xx, yy};
			return result;
		}

		friend bool operator==(const xy16& lhs, const xy16& rhs)
		{
			return lhs.x == rhs.x
				&& lhs.y == rhs.y;
		}

		friend bool operator!=(const xy16& lhs, const xy16& rhs)
		{
			return !(lhs == rhs);
		}

		friend bool operator<(const xy16& lhs, const xy16& rhs)
		{
			if (lhs.x < rhs.x)
				return true;
			if (rhs.x < lhs.x)
				return false;
			return lhs.y < rhs.y;
		}

		friend bool operator<=(const xy16& lhs, const xy16& rhs)
		{
			return !(rhs < lhs);
		}

		friend bool operator>(const xy16& lhs, const xy16& rhs)
		{
			return rhs < lhs;
		}

		friend bool operator>=(const xy16& lhs, const xy16& rhs)
		{
			return !(lhs < rhs);
		}
	};

	struct xy32
	{
		int32_t x, y;
		static xy32 null;

		xy32() noexcept = default;
		constexpr xy32& operator=(const xy32&) noexcept = default;
		constexpr xy32& operator=(xy32&&) noexcept = default;
		constexpr xy32(const xy32&) noexcept = default;
		constexpr xy32(xy32&&) noexcept = default;

		constexpr xy32(const int32_t xx, const int32_t yy) : x(xx), y(yy)
		{
		}

		constexpr static xy32 make(const int32_t xx, const int32_t yy)
		{
			const xy32 result = {xx, yy};
			return result;
		}

		constexpr static xy32 make(const uint32_t xx, const uint32_t yy)
		{
			const xy32 result = {static_cast<int32_t>(xx), static_cast<int32_t>(yy)};
			return result;
		}

		static xy32 parse(std::string_view r);

		constexpr operator xy16() const
		{
			return xy16::make(static_cast<int16_t>(x), static_cast<int16_t>(y));
		}

		constexpr bool is_empty() const { return x == 0 && y == 0; }

		std::string str() const;

		friend bool operator==(const xy32& lhs, const xy32& rhs)
		{
			return lhs.x == rhs.x
				&& lhs.y == rhs.y;
		}

		friend bool operator!=(const xy32& lhs, const xy32& rhs)
		{
			return !(lhs == rhs);
		}

		friend bool operator<(const xy32& lhs, const xy32& rhs)
		{
			if (lhs.x < rhs.x)
				return true;
			if (rhs.x < lhs.x)
				return false;
			return lhs.y < rhs.y;
		}

		friend bool operator<=(const xy32& lhs, const xy32& rhs)
		{
			return !(rhs < lhs);
		}

		friend bool operator>(const xy32& lhs, const xy32& rhs)
		{
			return rhs < lhs;
		}

		friend bool operator>=(const xy32& lhs, const xy32& rhs)
		{
			return !(lhs < rhs);
		}
	};

	class file_size
	{
	public:
		uint64_t _i = 0;

		constexpr file_size() noexcept = default;

		constexpr explicit file_size(const uint32_t d) noexcept : _i(d)
		{
		}

		constexpr explicit file_size(const uint64_t i) noexcept : _i(i)
		{
		}

		constexpr explicit file_size(const int64_t i) noexcept : _i(i)
		{
		}

		constexpr explicit file_size(const int32_t i) noexcept : _i(static_cast<uint64_t>(i))
		{
		}

		constexpr explicit file_size(const float f) noexcept : _i(static_cast<uint64_t>(f))
		{
		}

		constexpr explicit file_size(const double d) noexcept : _i(static_cast<uint64_t>(d))
		{
		}

#if !DF_LONG_IS_INT64
		constexpr explicit file_size(const long i) noexcept : _i(static_cast<uint64_t>(i))
		{
		}

		constexpr explicit file_size(const unsigned long i) noexcept : _i(static_cast<uint64_t>(i))
		{
		}
#endif

		constexpr file_size(const file_size&) noexcept = default;
		constexpr file_size& operator=(const file_size&) noexcept = default;
		constexpr file_size(file_size&&) noexcept = default;
		constexpr file_size& operator=(file_size&&) noexcept = default;

		constexpr file_size& operator=(const uint32_t s) noexcept
		{
			_i = s;
			return *this;
		}

		constexpr file_size& operator=(const uint64_t s) noexcept
		{
			_i = s;
			return *this;
		}

		constexpr file_size operator+(const file_size other) const noexcept
		{
			return file_size(_i + other._i);
		}

		constexpr file_size operator-(const file_size other) const noexcept
		{
			return file_size(_i - other._i);
		}

		constexpr file_size operator/(const size_t other) const noexcept
		{
			return file_size(_i / other);
		}

		constexpr file_size& operator+=(const file_size other) noexcept
		{
			_i += other._i;
			return *this;
		}

		constexpr void clear()
		{
			_i = 0;
		}

		constexpr bool is_empty() const
		{
			return _i == 0;
		}

		constexpr bool is_valid() const
		{
			return _i != 0;
		}

		friend bool operator==(const file_size& lhs, const file_size& rhs)
		{
			return lhs._i == rhs._i;
		}

		friend bool operator!=(const file_size& lhs, const file_size& rhs)
		{
			return !(lhs == rhs);
		}

		friend bool operator<(const file_size& lhs, const file_size& rhs)
		{
			return lhs._i < rhs._i;
		}

		friend bool operator<=(const file_size& lhs, const file_size& rhs)
		{
			return !(rhs < lhs);
		}

		friend bool operator>(const file_size& lhs, const file_size& rhs)
		{
			return rhs < lhs;
		}

		friend bool operator>=(const file_size& lhs, const file_size& rhs)
		{
			return !(lhs < rhs);
		}

		constexpr int to_int() const
		{
			return static_cast<int>(_i);
		}

		constexpr uint64_t to_int64() const
		{
			return _i;
		}

		constexpr float to_float() const
		{
			return static_cast<float>(_i);
		}

		std::string str() const;

		static file_size null;
	};

	struct count_and_size
	{
		uint64_t count = 0;
		file_size size;

		count_and_size operator+(const count_and_size other) const
		{
			return {count + other.count, size + other.size};
		}

		count_and_size operator+=(const count_and_size other)
		{
			count += other.count;
			size += other.size;
			return *this;
		}

		void add(const file_size& s)
		{
			++count;
			size += s;
		}

		friend bool operator==(const count_and_size& lhs, const count_and_size& rhs)
		{
			return lhs.count == rhs.count
				&& lhs.size == rhs.size;
		}

		friend bool operator!=(const count_and_size& lhs, const count_and_size& rhs)
		{
			return !(lhs == rhs);
		}
	};

	// In-flight gauges answer "is anything of this kind running". Nothing reads the count itself, so
	// entering needs no ordering; leaving still releases, and because every leave is an RMW they form
	// one release sequence, so a reader that sees zero has synchronized with all of them. That is the
	// guarantee seq_cst was providing here, kept at the price ARM64 charges for it rather than double.
	inline void gauge_enter(std::atomic_int& gauge) noexcept
	{
		gauge.fetch_add(1, std::memory_order_relaxed);
	}

	inline void gauge_leave(std::atomic_int& gauge) noexcept
	{
		gauge.fetch_sub(1, std::memory_order_release);
	}

	class scope_locked_inc final : public no_copy
	{
		std::atomic_int& _i;

	public:
		scope_locked_inc(std::atomic_int& i) : _i(i)
		{
			gauge_enter(_i);
		}

		~scope_locked_inc() override
		{
			gauge_leave(_i);
		}
	};

	class scope_rendering_func final : public no_copy
	{
		const char* _prev = "";

	public:
		scope_rendering_func(const char* f) : _prev(rendering_func.load(std::memory_order_relaxed))
		{
			rendering_func.store(f, std::memory_order_relaxed);
		}

		~scope_rendering_func() override
		{
			rendering_func.store(_prev, std::memory_order_relaxed);
		}
	};

	struct version
	{
		int major = 0;
		int minor = 0;

		version(std::string_view version);

		friend bool operator==(const version& lhs, const version& rhs)
		{
			return lhs.major == rhs.major
				&& lhs.minor == rhs.minor;
		}

		friend bool operator!=(const version& lhs, const version& rhs)
		{
			return !(lhs == rhs);
		}

		friend bool operator<(const version& lhs, const version& rhs)
		{
			if (lhs.major < rhs.major)
				return true;
			if (rhs.major < lhs.major)
				return false;
			return lhs.minor < rhs.minor;
		}

		friend bool operator<=(const version& lhs, const version& rhs)
		{
			return !(rhs < lhs);
		}

		friend bool operator>(const version& lhs, const version& rhs)
		{
			return rhs < lhs;
		}

		friend bool operator>=(const version& lhs, const version& rhs)
		{
			return !(lhs < rhs);
		}

		version operator +(const int i) const
		{
			version result = *this;
			result.major += i;
			return result;
		}

		friend std::ostringstream& operator <<(std::ostringstream& stream, const version& ver)
		{
			stream << ver.major;
			stream << '.';
			stream << ver.minor;
			return stream;
		}

		std::string to_string() const
		{
			std::ostringstream s;
			s << *this;
			return s.str();
		}
	};

	struct int_counter
	{
		int i = 0;

		void operator++()
		{
			i++;
		}

		void operator+=(const int n)
		{
			i += n;
		}

		int_counter& operator=(const int n)
		{
			i = n;
			return *this;
		}

		operator int() const
		{
			return i;
		}
	};

	class cancel_token
	{
		static std::atomic_int empty;
		std::atomic_int* version = nullptr;
		std::atomic_bool* flag = nullptr;
		int job_version = 0;

	public:
		bool is_cancelled() const
		{
			return is_closing || (flag && flag->load(std::memory_order_relaxed)) ||
				(version && job_version != version->load(std::memory_order_relaxed));
		}


		~cancel_token() noexcept = default;
		cancel_token& operator=(const cancel_token& other) noexcept = delete;
		cancel_token& operator=(cancel_token&& other) noexcept = delete;
		cancel_token(const cancel_token& other) noexcept = default;
		cancel_token(cancel_token&& other) noexcept = default;

		cancel_token() noexcept : version(&empty)
		{
			// empty version
		}

		cancel_token(std::atomic_int& v) : version(&v)
		{
			job_version = version->fetch_add(1, std::memory_order_relaxed) + 1;
		}

		cancel_token(std::atomic_bool& f) : flag(&f)
		{
		}
	};

	inline uint32_t byteswap32(const uint32_t n)
	{
		return _byteswap_ulong(n);
	}

	// Callers pass an offset into a file buffer, so the address carries no alignment guarantee.
	inline uint32_t byteswap32(const uint8_t* addr)
	{
		uint32_t n;
		std::memcpy(&n, addr, sizeof(n));
		return _byteswap_ulong(n);
	}

	inline uint16_t byteswap16(const uint16_t n)
	{
		return _byteswap_ushort(n);
	}

	inline uint16_t byteswap16(const uint8_t* addr)
	{
		uint16_t n;
		std::memcpy(&n, addr, sizeof(n));
		return _byteswap_ushort(n);
	}

	std::string url_extract(std::string_view text);

	// Every distinct link in source order, so a caller can offer a choice rather than the first hit.
	std::vector<std::string> url_extract_all(std::string_view text);

	inline std::string url_encode(const std::string_view url)
	{
		std::string result;

		for (const auto c : url)
		{
			if ((48 <= c && c <= 57) || //0-9
				(65 <= c && c <= 90) || //ABC...XYZ
				(97 <= c && c <= 122) || //abc...xyz
				c == '~' || c == '-' || c == '_' || c == '.'
			)
			{
				result += c;
			}
			else
			{
				static constexpr auto chars = "0123456789ABCDEF";

				result += '%';
				result += chars[(c & 0xF0) >> 4];
				result += chars[c & 0x0F];
			}
		}

		return result;
	}
} // namespace

template <>
struct std::formatter<text_t, char> : std::formatter<std::string_view, char>
{
	auto format(const text_t& t, std::format_context& ctx) const
	{
		return std::formatter<std::string_view, char>::format(t.sv(), ctx);
	}
};

// Helper for formatting with runtime format strings (e.g. text_t).
// Takes args by value so temporaries can be passed to std::make_format_args.
template <class... Args>
std::string str_format(const std::string_view fmt, Args... args)
{
	return std::vformat(fmt, std::make_format_args(args...));
}

template <>
struct std::formatter<df::file_size, char> : std::formatter<std::string_view, char>
{
	auto format(const df::file_size& s, std::format_context& ctx) const
	{
		return std::formatter<std::string_view, char>::format(s.str(), ctx);
	}
};
