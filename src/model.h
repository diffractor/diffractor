// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: View state coordination and navigation. Manages display state, media playback,
// item selection, history, grouping, filtering, and synchronization between views.

#pragma once

#include "model_zoom.h"

#include "model_property.h"
#include "model_items.h"
#include "model_locations.h"
#include "model_visits.h"
#include "av_format.h"
#include "av_player.h"

constexpr ui::color32 color_for_delete = ui::bgr(0xaa2211);
constexpr ui::color32 color_for_action = ui::bgr(0xCC6611);
constexpr ui::color32 color_rate_rejected = ui::bgr(0xE01C2B);
constexpr ui::color32 color_label_approved = ui::bgr(0x6FC958);
constexpr ui::color32 color_label_to_do = ui::bgr(0xD08CE0);
constexpr ui::color32 color_label_select = ui::bgr(0xFD6460);
constexpr ui::color32 color_label_review = ui::bgr(0x52B7F2);
constexpr ui::color32 color_label_second = ui::bgr(0xF2CC51);

// The grading vocabulary. One definition drives the grading control, the thumbnail badge, the
// sidebar filters, and the Rate or Label menu, so a mark is learned once and recognized everywhere.
// Reject is a rating of -1 rather than a colour label, so it leads the row with an empty key.
// Each mark has its own glyph as well as its own colour so the row stays readable without colour.
struct rate_label_def
{
	std::string_view key;
	ui::color32 clr;
	icon_index icon;
};

inline constexpr rate_label_def rate_label_defs[] = {
	{{}, color_rate_rejected, icon_index::cancel},
	{label_select_text, color_label_select, icon_index::flag},
	{label_second_text, color_label_second, icon_index::bullet},
	{label_approved_text, color_label_approved, icon_index::check},
	{label_review_text, color_label_review, icon_index::question},
	{label_to_do_text, color_label_to_do, icon_index::time},
};

inline constexpr auto& rate_label_reject = rate_label_defs[0];
inline constexpr auto& rate_label_select = rate_label_defs[1];
inline constexpr auto& rate_label_second = rate_label_defs[2];
inline constexpr auto& rate_label_approved = rate_label_defs[3];
inline constexpr auto& rate_label_review = rate_label_defs[4];
inline constexpr auto& rate_label_to_do = rate_label_defs[5];

inline const rate_label_def* find_rate_label_def(const std::string_view label)
{
	for (const auto& d : rate_label_defs)
	{
		if (!d.key.empty() && str::icmp(d.key, label) == 0) return &d;
	}

	return nullptr;
}

// One badge definition for every surface: the thumbnail flag, the detail row, and the selection
// panel control all draw the same mark for the same state.
void draw_rate_label_badge(ui::draw_context& dc, std::string_view label, int rating, recti logical_bounds, float alpha);

// The pin mark is the same orange badge wherever it appears, so the held item reads the same in the
// item grid, the selection collage and the panel control.
void draw_pin_badge(ui::draw_context& dc, recti logical_bounds, float alpha);


class location_cache;
class database;
class tile_cache_db;
class av_player;
class av_format_decoder;
class scrubber_element;
class display_state_t;

using display_state_ptr = std::shared_ptr<display_state_t>;

ui::texture_sampler calc_sampler(sizei draw_extent, sizei texture_extent, const ui::orientation& orientation,
                                 bool interactive = false);
void draw_texture_info(ui::draw_context& rc, recti media_bounds, const ui::texture_ptr& tex,
                       ui::orientation orientation, ui::texture_sampler sampler, float alpha);
df::unique_paths make_unique_paths(df::paths selection);

struct media_preview_state
{
	std::shared_ptr<av_format_decoder> decoder1;
	std::shared_ptr<av_format_decoder> decoder2;

	void close();
	bool open1(df::file_path file_path);
	bool open2(df::file_path file_path);
};

// async_strategy - Core async execution and thread coordination interface.
//
// Provides thread-safe context switching between different execution contexts in the application.
// All background work is dispatched through specialized queues, each running on dedicated threads.
// Results are marshalled back to the UI thread via queue_ui() for safe UI updates.
//
// Queue methods:
// - queue_ui(f)        : Execute f on the UI thread. Use for all UI updates from background threads.
// - queue_async(q, f)  : Execute f on a background thread pool based on queue type q.
// - queue_location(f)  : Execute f with access to the location_cache (reverse geocoding, city lookups).
// - queue_database(f)  : Execute f with access to the SQLite database (thumbnails, metadata cache).
// - queue_tile_db(f)   : Execute f with access to the map tile store. Its own connection on its own
//   thread, so a tile lookup never queues behind index or thumbnail work.
// - queue_media_preview(f, must_run) : Execute f for video seek preview generation. Preview requests
//   coalesce so only the newest survives; must_run marks teardown, which is never superseded.
//
// Typical pattern:
//   queue_async(async_queue::work, [this] {
//       auto result = expensive_computation();
//       queue_ui([result] { update_ui_with(result); });
//   });
//
// The async_queue enum defines specialized queues for different workloads:
// - scan_folder, scan_modified_items : File system scanning
// - load, load_raw                   : Image/media loading
// - crc                              : File hash computation  
// - index, index_predictions         : Search index operations
// - web, cloud, map_tile             : Network operations
// - query, auto_complete             : Search and suggestions
//
// See app.cpp for the implementation using platform::task_queue and worker threads.
class async_strategy : public av_host, public df::async_i
{
public:
	~async_strategy() override = default;

	void queue_ui(std::function<void()> f) override = 0;
	void queue_async(async_queue q, std::function<void()> f) override = 0;
	// Runs f no sooner than delay_ms from now, superseding anything already waiting on that queue. The
	// wait happens in the worker's event wait, so it costs no thread. Only the coalescing queues
	// support it - the deadline belongs to the queue, not to f.
	virtual void queue_async_after(async_queue q, uint32_t delay_ms, std::function<void()> f) = 0;
	virtual void queue_location(std::function<void(location_cache&)>) = 0;
	virtual void queue_database(std::function<void(database&)> f) = 0;
	virtual void queue_tile_db(std::function<void(tile_cache_db&)> f) = 0;

	virtual void queue_media_preview(std::function<void(media_preview_state&)>, bool must_run = false) = 0;

	virtual void web_service_cache(std::string key, std::function<void(const std::string&)> f) = 0;
	virtual void web_service_cache(std::string key, std::string value) = 0;
};

// Carries a UI-owned object across a worker hop. A worker must never run these destructors: they
// release D3D textures and cross-device keyed mutexes, tear down av_session decoder threads, and
// every worker runs in its own single-threaded apartment. Capturing a plain shared_ptr lets that
// happen whenever the worker drops the final reference - an early return, a thrown task, or the
// queue truncation each worker performs as it exits. This guard hands that final reference back to
// queue_ui instead, so teardown always runs on the owning context. app_frame joins every worker
// before draining _ui_queue, which is what makes the shutdown hand-back land on the UI thread.
template <typename T>
class ui_owned_ptr
{
public:
	ui_owned_ptr(async_strategy& async, std::shared_ptr<T> p) noexcept : _async(&async), _p(std::move(p))
	{
	}

	ui_owned_ptr(const ui_owned_ptr&) = default;
	ui_owned_ptr& operator=(const ui_owned_ptr&) = default;
	ui_owned_ptr(ui_owned_ptr&&) noexcept = default;
	ui_owned_ptr& operator=(ui_owned_ptr&&) noexcept = default;

	~ui_owned_ptr()
	{
		// use_count only filters out the common case where another reference survives this one, so an
		// over-estimate costs a redundant hop. It cannot under-estimate: once this is the sole owner no
		// other thread has a shared_ptr to copy, and every weak_ptr to these types is locked on the UI
		// thread, which would only push the count up and keep the object alive.
		if (_p && _p.use_count() == 1 && !ui::is_ui_thread())
		{
			_async->queue_ui([p = std::move(_p)]
			{
			});
		}
	}

	T* operator->() const noexcept { return _p.get(); }
	T& operator*() const noexcept { return *_p; }
	T* get() const noexcept { return _p.get(); }
	explicit operator bool() const noexcept { return static_cast<bool>(_p); }

	const std::shared_ptr<T>& shared() const noexcept { return _p; }

private:
	async_strategy* _async;
	std::shared_ptr<T> _p;
};

template <typename T>
ui_owned_ptr<T> ui_owned(async_strategy& async, std::shared_ptr<T> p)
{
	return ui_owned_ptr<T>(async, std::move(p));
}

// state_strategy - Abstract interface for application-level UI coordination and state management.
//
// This interface decouples the view_state (model/data layer) from the application frame (app_frame).
// By using this abstraction, view_state can notify the UI of state changes without depending on
// concrete UI implementation details. This enables:
//
// 1. **Loose Coupling**: view_state communicates through abstract callbacks rather than direct
//    method calls to app_frame, making the codebase more modular and testable.
//
// 2. **Testability**: Unit tests can provide a null implementation (see null_state_strategy in
//    tests.cpp) that ignores all callbacks, allowing view_state to be tested in isolation.
//
// 3. **Separation of Concerns**: The model layer (view_state) focuses on data and business logic,
//    while the app layer handles UI coordination, window management, and command routing.
//
// Key callback categories:
// - **View Lifecycle**: display_changed(), view_changed(), search_complete()
// - **User Interaction**: invoke(), track_menu(), command_hover(), toggle_full_screen()
// - **Item Management**: item_focus_changed(), make_visible(), delete_items()
// - **Rendering**: invalidate_view(), free_graphics_resources(), element_broadcast()
//
// The app_frame class implements this interface to bridge view_state notifications to the
// actual UI components (toolbars, sidebar, views, etc.).
//
// See also: async_strategy for threading/async decoupling, view_host for per-view interactions.
struct state_strategy
{
	virtual ~state_strategy() = default;

	// Window management
	virtual void toggle_full_screen() = 0;

	// Navigation and search
	virtual bool can_open_search(const df::search_t& path) = 0;
	virtual void search_complete(const df::search_t& path, bool path_changed) = 0;

	// Called when a scope could not be opened because one of its locations cannot be read.
	virtual void report_scope_unavailable(const df::search_t& path) = 0;

	// Selection and focus
	virtual void item_focus_changed(const df::item_element_ptr& focus, const df::item_element_ptr& previous) = 0;
	virtual void make_visible(const df::item_element_ptr& i) = 0;

	// View state changes
	virtual void display_changed() = 0;
	virtual void view_changed(view_type m) = 0;
	virtual void play_state_changed(bool play) = 0;

	// Command handling
	virtual void invoke(commands id) = 0;
	virtual bool is_command_checked(commands cmd) = 0;
	virtual ui::command_ptr find_command(commands id) const = 0;
	virtual void command_hover(const ui::command_ptr& c, recti window_bounds) = 0;

	// UI interactions
	virtual void track_menu(const ui::frame_ptr& parent, recti bounds,
	                        const std::vector<ui::command_ptr>& commands) = 0;
	virtual void element_broadcast(const view_element_event& event) = 0;
	virtual void focus_view() = 0;

	// Resource management
	virtual void free_graphics_resources(bool items_only, bool offscreen_only) = 0;
	virtual void delete_items(const df::item_set& items) = 0;

	// Rendering invalidation
	virtual void invalidate_view(view_invalid invalid) = 0;
};

class recent_state final : public df::no_copy
{
public:
	static constexpr int max_size = 64;
	std::vector<std::string> _items;

	recent_state() = default;

	const std::vector<std::string>& items() const
	{
		return _items;
	}

	void count_strings(df::string_counts& results, const int weight, const std::string_view prefix = {}) const
	{
		// Keys are views, so the text must be interned: _items is rewritten whenever a search or
		// folder is used again, which would otherwise leave earlier counts dangling.
		for (const auto& i : _items)
		{
			if (prefix.empty())
			{
				results[str::cache(i)] += weight;
			}
			else
			{
				results[str::cache(std::format("{}{}", prefix, i))] += weight;
			}
		}
	}

	void count(df::folder_counts& results, const int weight) const
	{
		for (const auto& i : _items)
		{
			if (df::is_path(i))
			{
				results[df::folder_path(i)] += weight;
			}
		}
	}

	void add(const std::string_view v)
	{
		for (auto i = _items.cbegin(); i != _items.cend(); ++i)
		{
			if (compare(*i, v) == 0)
			{
				_items.erase(i);
				break;
			}
		}

		if (_items.size() >= max_size)
		{
			_items.erase(_items.cbegin());
		}

		_items.emplace_back(v);
	}

	static void add_list(std::vector<std::string>& results, const std::vector<std::string_view>& adds)
	{
		for (const auto& a : adds)
		{
			results.emplace_back(a);
		}
	}

	static void add_list(std::vector<std::string>& results, const std::vector<std::string>& adds)
	{
		for (const auto& a : adds)
		{
			results.emplace_back(a);
		}
	}

	static void add_list(std::vector<df::folder_path>& results, const std::vector<std::string_view>& adds)
	{
		for (const auto& a : adds)
		{
			results.emplace_back(a);
		}
	}

	template <class t>
	void add_items(t&& v)
	{
		add_list(_items, v);
	}

	static std::string combine(const std::vector<std::string>& strings)
	{
		return str::combine(strings);
	}

	static std::string combine(const std::vector<df::folder_path>& strings)
	{
		return combine_paths(strings);
	}

	static int compare(const std::string_view l, const std::string_view r)
	{
		return str::icmp(l, r);
	}

	static int compare(const df::folder_path l, const df::folder_path r)
	{
		return l.compare(r);
	}

	void read(const std::string_view section, const std::string_view key,
	          const platform::setting_file_ptr& properties)
	{
		std::string str;
		properties->read(section, key, str);
		add_items(str::split(str, true));
	}

	void write(const std::string_view section, const std::string_view key,
	           const platform::setting_file_ptr& properties) const
	{
		properties->write(section, key, combine(_items));
	}
};

class history_state final : public df::no_copy
{
public:
	struct history_entry
	{
		df::search_t search;
		df::paths selected;
	};

	static constexpr int max_history_size = 32;
	std::vector<history_entry> _history;
	ptrdiff_t _pos = -1;

	history_state() = default;

	void count_strings(df::string_counts& results, const int weight) const
	{
		for (const auto& h : _history)
		{
			for (const auto& s : h.search.selectors())
			{
				results[str::cache(s.str())] += weight;
				results[s.folder().text()] += weight;
			}

			for (const auto& t : h.search.terms())
			{
				results[str::cache(format_term(t))] += weight;
			}
		}
	}

	void count_folders(df::folder_counts& results, int weight) const
	{
		if (weight == 0)
		{
			for (const auto& result : results)
			{
				weight = std::max(weight, static_cast<int>(result.second));
			}
		}

		for (const auto& h : _history)
		{
			for (const auto& s : h.search.selectors())
			{
				if (s.folder().exists())
				{
					results[s.folder()] += weight;
				}
			}
		}
	}

	bool can_browse_forward() const
	{
		return _pos + 1 < static_cast<int>(_history.size());
	}

	bool can_browse_back() const
	{
		return _pos > 0;
	}

	const history_entry& back_entry() const
	{
		return _history[_pos - 1];
	}

	const history_entry& forward_entry() const
	{
		return _history[_pos + 1];
	}

	bool move_history_pos(const int n, df::paths selected, history_entry& result)
	{
		const auto i = _pos + n;

		if (i >= static_cast<int>(_history.size()) || i < 0)
		{
			return false;
		}

		_history[_pos].selected = std::move(selected);
		_pos = i;
		result = _history[i];
		return true;
	}

	void replace_current_search(const df::search_t& expected, const df::search_t& replacement)
	{
		if (_pos >= 0 && _pos < static_cast<int>(_history.size()) && _history[_pos].search == expected)
		{
			_history[_pos].search = replacement;
		}
	}

	void history_add(const df::search_t& link, df::paths selected)
	{
		if (_pos != -1)
		{
			if (_history[_pos].search == link)
			{
				return;
			}

			_history[_pos].selected = std::move(selected);
			_history.resize(_pos + 1);
		}

		_history.emplace_back(link, df::paths{});

		if (_history.size() > max_history_size)
		{
			_history.erase(_history.cbegin());
		}

		_pos = static_cast<int>(_history.size()) - 1;
	}
};


class texture_state final : public std::enable_shared_from_this<texture_state>
{
public:
	// Why the media area has nothing to draw, so it can say so instead of showing an empty frame.
	enum class display_problem
	{
		none,
		too_large,
		failed
	};

	ui::texture_ptr _tex;
	ui::texture_ptr _vid_tex;
	async_strategy& _async;

	friend class display_state_t;

private:
	df::file_path _path;
	file_load_result _loaded;
	ui::const_surface_ptr _staged_surface;
	ui::const_surface_ptr _retained_surface;
	std::shared_ptr<std::atomic_bool> _decode_cancel;

	df::date_t _photo_timestamp;
	sizei _loading_scale_hint;
	uint64_t _decode_generation = 0;
	uint64_t _load_generation = 0;
	int _load_retry_count = 0;

	ui::texture_ptr _last_draw_tex;
	recti _last_draw_rect;
	recti _last_draw_source_rect;
	ui::texture_sampler _last_drawn_sampler = ui::texture_sampler::point;

	ui::texture_ptr _fade_out_tex;
	recti _fade_out_rect;
	recti _fade_out_source_rect;
	ui::texture_sampler _fade_out_sampler = ui::texture_sampler::point;
	ui::animate_alpha _fade_out_alpha_animation;

	ui::animate_alpha _display_alpha_animation;
	std::atomic_int _preview_rendering = 0;

	recti _display_bounds;

	ui::texture_ptr _zoom_texture;
	ui::const_surface_ptr _zoom_staged_surface;
	df::date_t _zoom_timestamp;

public:
	bool _is_video_tex = false;
	bool _tex_invalid = true;
	bool _is_photo = false;
	bool _is_raw = false;
	bool _photo_loaded = false;
	// True while _loaded still holds the item thumbnail standing in for the full-size image.
	bool _is_placeholder = true;
	bool _display_geometry_known = false;
	bool _load_retry_pending = false;
	// Armed before a write that changes the modified time without changing what is drawn; consumed by
	// the first refresh that sees that write's stamps.
	bool _retain_visuals_on_modify = false;

	ui::orientation _display_orientation = ui::orientation::top_left;
	sizei _display_dimensions;
	display_problem _display_problem = display_problem::none;

	texture_state(async_strategy& async, const df::item_element_ptr& i);

	void load_image(const df::item_element_ptr& i);
	void prefetch(const df::item_element_ptr& i);
	void load_raw();

	void refresh(const df::item_element_ptr& i);
	// Declares what is held to be current across the next write, for a write that changes the file's
	// modified time without changing what the file draws.
	void mark_visuals_current();
	// Adopts the image a write just produced, for the path it was written to. Supersedes any load
	// already in flight so a slower by-name read cannot land on top of it. The modified time is the
	// file's own, so the stamp stays in the same clock domain as the item's timestamps.
	void publish_written_image(df::file_path path, file_load_result loaded, df::date_t modified);
	void draw(ui::draw_context& rc, pointi offset, int compare_pos, bool first_texture, bool interactive = false);
	void layout(ui::measure_context& mc, recti bounds, const df::item_element_ptr& i);
	sizei calc_display_dimensions() const;
	void clear();
	bool is_empty() const;
	void clone_fade_out(const std::shared_ptr<texture_state>& other);
	void fade_out();
	void display_dimensions(sizei dims);

	sizei display_dimensions() const
	{
		return _display_dimensions;
	}

	ui::orientation display_orientation() const
	{
		return _display_orientation;
	}

	const file_load_result& loaded() const
	{
		return _loaded;
	}

	bool step()
	{
		auto result = _display_alpha_animation.step();
		result |= _fade_out_alpha_animation.step();
		return result;
	}

	void free_graphics_resources();
	void update(const ui::const_surface_ptr& staged_surface);
	void update(file_load_result loaded);
	void complete_load(file_load_result loaded, uint64_t generation, bool raw);
	void cancel_pending_decode();
	sizei calc_scale_hint() const;

	bool can_preview() const
	{
		return _is_raw;
	}

	ui::texture_ptr zoom_texture(ui::draw_context& rc, sizei extent);

	recti display_bounds() const
	{
		return _display_bounds;
	}

	bool is_preview() const
	{
		return !_loaded.is_empty() && _loaded.is_preview;
	}

	bool is_preview_rendering() const
	{
		return _preview_rendering != 0;
	}

	bool is_provisional() const
	{
		if (_is_placeholder || !_tex || !_tex->is_valid()) return true;

		auto texture_dims = _tex->source_extent();
		auto loaded_dims = _loaded.dimensions();
		if (setting.show_rotated && flips_xy(_tex->_orientation)) std::swap(texture_dims.cx, texture_dims.cy);
		if (setting.show_rotated && flips_xy(_loaded.orientation())) std::swap(loaded_dims.cx, loaded_dims.cy);
		if (_loaded.is_preview)
			return _display_bounds.width() > texture_dims.cx || _display_bounds.height() > texture_dims.cy;
		const auto required_width = std::min(_display_bounds.width(), loaded_dims.cx);
		const auto required_height = std::min(_display_bounds.height(), loaded_dims.cy);
		return required_width > texture_dims.cx || required_height > texture_dims.cy;
	}

	size_t retained_surface_bytes() const noexcept
	{
		return _retained_surface ? _retained_surface->size() : 0;
	}
};

using texture_state_ptr = std::shared_ptr<texture_state>;

struct common_display_state_t
{
	// True only while the slideshow mode is running. Media transport state lives on the av_session.
	bool _is_slideshow = false;
	df::zoom_view_state _zoom;
	std::vector<std::pair<df::file_path, texture_state_ptr>> _recent_textures;

	void retain_texture(const df::file_path path, const texture_state_ptr& texture)
	{
		if (!texture) return;
		std::erase_if(_recent_textures, [&path](const auto& entry) { return entry.first == path; });
		_recent_textures.emplace(_recent_textures.begin(), path, texture);
		constexpr auto max_count = 5u;
		constexpr auto max_bytes = 256ull * 1024ull * 1024ull;
		auto bytes = size_t{};
		for (const auto& entry : _recent_textures) bytes += entry.second->retained_surface_bytes();
		while (_recent_textures.size() > max_count || bytes > max_bytes)
		{
			bytes -= _recent_textures.back().second->retained_surface_bytes();
			_recent_textures.pop_back();
		}
	}
};

class display_state_t final : public std::enable_shared_from_this<display_state_t>
{
public:
	async_strategy& _async;
	common_display_state_t& _common;

	bool _comparing = false;
	df::comparison_zoom_state _comparison_zoom;

	std::shared_ptr<av_session> _session;
	// Bumped whenever the session is torn down or superseded. An open that completes after a bump has
	// lost the display and must close what it was handed, not publish it. UI thread only.
	uint32_t _av_generation = 0;

	df::blob _selected_item_data;
	std::vector<archive_item> _archive_items;
	av_media_info _player_media_info;
	// Set once the full-file metadata scan (or player open) has completed for this display item.
	// Reading the whole file finishes hydrating a cloud-only placeholder, so this is the point at
	// which a previously offline item can be safely re-indexed to pick up its real metadata.
	bool _full_metadata_loaded = false;

	// The decoder could not make sense of this file, so there is nothing to play or show. The panel
	// falls back to a hex dump of the bytes, which at least says what the file really is.
	bool _av_open_failed = false;

	ui::vertices_ptr _verts;
	texture_state_ptr _selected_texture1;
	texture_state_ptr _selected_texture2;

	df::item_element_ptr _item1;
	df::item_element_ptr _item2;

	// Painted track width, used only to quantise scrubber redraws to whole pixels.
	mutable int _scrubber_width = 0;

	int _hover_scrubber_pos = -1;
	mutable int _time_width = 0;
	int _next_photo_tick = 0;
	// Latches the end of the current clip until the queued pause/seek has actually taken effect.
	bool _media_end_handled = false;

	ui::pixel_difference_result _pixel_difference = ui::pixel_difference_result::unknown;
	int _last_scrubber_pos = -1;
	int _last_duration = -1;
	int _last_seconds = -1;

	std::string _duration;
	std::string _time;

	ui::const_surface_ptr _hover_surface;

	mutable ui::vertices_ptr _audio_verts;
	mutable pointi _audio_element_offset;
	mutable recti _audio_element_bounds;
	mutable float _audio_element_alpha = 1.0f;

	int _compare_hover_loc = 0;
	int _compare_video_pos = 0;
	bool _temporary_zoom = false;
	uint64_t _zoom_activity_generation = 0;
	double _zoom_activity_time = 0.0;
	float _zoom_overlay_alpha = 1.0f;
	recti compare_control_bounds;
	recti _compare_bounds;
	recti _compare_limits;
	recti _compare_video_control_bounds;
	recti _compare_video_scrubber_bounds;
	// Clickable A and B pane markers. Two entries while both images show; only the first while magnified.
	mutable std::array<recti, 2> _pane_marker_bounds;
	bool _can_compare = false;
	bool _is_compare_video = false;
	bool _comparison_eligible = false;
	bool _is_one = false;
	bool _is_two = false;
	bool _is_multi = false;
	bool _can_zoom = false;

	struct zoom_layout_state
	{
		sized source_extent;
		sized viewport_extent;
		pointd viewport_origin;
		pointd pending_source_anchor;
		pointd pending_client_anchor;
		bool pending_reanchor = false;
	};

	std::array<zoom_layout_state, 2> _zoom_layouts;
	mutable bool _preview_changed = false;

	std::vector<ui::const_image_ptr> _images;
	std::vector<ui::const_surface_ptr> _surfaces;
	// UI-owned, index-aligned with _images/_surfaces so a collage cell can name the item it shows.
	df::item_elements _collage_source_items;
	mutable std::vector<ui::texture_ptr> _textures;
	std::vector<recti> _surface_bounds;
	size_t _selection_item_count = 0;
	size_t _collage_image_count = 0;
	size_t _selection_overflow_count = 0;
	static constexpr size_t max_surfaces = 24;
	pointi media_offset;

	ui::animate_alpha _loading_alpha_animation;

	explicit display_state_t(async_strategy& async, common_display_state_t& common)
		: _async(async), _common(common)
	{
	}

	void populate(const view_state& state);

	bool is_one() const
	{
		return _is_one;
	}

	bool is_two() const
	{
		return _is_two;
	}

	// Two selected files is cardinality; comparison is the separate claim that the pair is like with like.
	bool is_comparison() const
	{
		return _is_two && _comparison_eligible;
	}

	render_valid update_for_present(double time_now) const;

	double media_pos() const
	{
		return _session ? _session->last_frame_time() : 0.0;
	}

	double media_start() const
	{
		return _player_media_info.start;
	}

	double media_end() const
	{
		return _player_media_info.end;
	}

	bool comparing() const
	{
		return _comparing;
	}

	static constexpr size_t zoom_pane_index(const df::zoom_pane pane) noexcept
	{
		return pane == df::zoom_pane::primary ? 0 : 1;
	}

	df::zoom_pane active_zoom_pane() const noexcept
	{
		return _is_two ? _comparison_zoom.active() : df::zoom_pane::primary;
	}

	void active_zoom_pane(const df::zoom_pane pane)
	{
		if (_is_two && _comparison_zoom.active() != pane)
		{
			// The pane you switch to always adopts the scale and center you were just looking at.
			_comparison_zoom.active(pane);
			mark_zoom_activity();
			_async.invalidate_view(view_invalid::view_layout | view_invalid::view_redraw |
				view_invalid::command_state | view_invalid::controller);
		}
	}

	void flip_zoom_pane()
	{
		if (!_is_two) return;
		active_zoom_pane(df::comparison_zoom_state::other(_comparison_zoom.active()));
	}

	void active_zoom_pane_at(const pointd location)
	{
		// Magnified there is no left or right to hit test, so the pointer never chooses the pane.
		if (!_is_two || is_zoom_mode()) return;
		for (const auto pane : {df::zoom_pane::primary, df::zoom_pane::secondary})
		{
			const auto& layout = _zoom_layouts[zoom_pane_index(pane)];
			const rectd pane_bounds(layout.viewport_origin, layout.viewport_extent);
			if (pane_bounds.contains(location))
			{
				active_zoom_pane(pane);
				return;
			}
		}
	}

	const df::zoom_view_state& current_zoom_state() const noexcept
	{
		return _is_two ? _comparison_zoom.active_state() : _common._zoom;
	}

	zoom_layout_state& current_zoom_layout() noexcept
	{
		return _zoom_layouts[zoom_pane_index(active_zoom_pane())];
	}

	const zoom_layout_state& current_zoom_layout() const noexcept
	{
		return _zoom_layouts[zoom_pane_index(active_zoom_pane())];
	}

	pointd zoom_anchor_at(const pointd location) const noexcept
	{
		return location - current_zoom_layout().viewport_origin;
	}

	template <class Mutator>
	void mutate_zoom(Mutator&& mutator)
	{
		if (_is_two) _comparison_zoom.mutate(std::forward<Mutator>(mutator));
		else mutator(_common._zoom);
	}

	bool zoom() const
	{
		return _can_zoom && current_zoom_state().is_magnified(zoom_fit_scale());
	}

	bool is_zoom_mode() const
	{
		return _can_zoom && (_temporary_zoom || !current_zoom_state().is_fit());
	}

	uint64_t zoom_activity_generation() const noexcept
	{
		return _zoom_activity_generation;
	}

	void mark_zoom_activity() noexcept
	{
		++_zoom_activity_generation;
		_zoom_activity_time = df::now();
	}

	bool zoom_interactive(const double time_now) const noexcept
	{
		return time_now - _zoom_activity_time < 0.15;
	}

	void zoom(const bool zoom)
	{
		const auto state_changes = zoom ? !this->zoom() : !current_zoom_state().is_fit();
		if (state_changes)
		{
			stop_slideshow();
			mutate_zoom([zoom](df::zoom_view_state& state)
			{
				if (zoom) state.set_explicit(1.0);
				else state.fit();
			});
			mark_zoom_activity();
			_async.invalidate_view(view_invalid::app_layout | view_invalid::view_layout | view_invalid::tooltip |
				view_invalid::controller);
		}
	}

	int zoom_scale_percent() const
	{
		return df::round(current_zoom_state().effective_scale(zoom_fit_scale()) * 100.0);
	}

	void adjust_zoom_scale(const int direction, const pointd anchor)
	{
		if (direction == 0) return;
		auto& layout = current_zoom_layout();
		const auto source_anchor = current_zoom_state().source_point_at(
			layout.source_extent, layout.viewport_extent, zoom_fit_scale(), anchor);
		mutate_zoom([&](df::zoom_view_state& state)
		{
			state.step(direction, zoom_fit_scale(), layout.source_extent, layout.viewport_extent, anchor);
		});
		mark_zoom_activity();
		if (!current_zoom_state().is_fit())
		{
			layout.pending_source_anchor = source_anchor;
			layout.pending_client_anchor = layout.viewport_origin + anchor;
			layout.pending_reanchor = true;
		}
		_async.invalidate_view(view_invalid::app_layout | view_invalid::view_layout | view_invalid::view_redraw |
			view_invalid::controller);
	}

	void adjust_zoom_scale(const int direction)
	{
		if (direction == 0) return;
		const auto& layout = current_zoom_layout();
		const pointd anchor{layout.viewport_extent.Width / 2.0, layout.viewport_extent.Height / 2.0};
		mutate_zoom([&](df::zoom_view_state& state)
		{
			state.step(direction, zoom_fit_scale(), layout.source_extent, layout.viewport_extent, anchor);
		});
		mark_zoom_activity();
		_async.invalidate_view(view_invalid::app_layout | view_invalid::view_layout | view_invalid::view_redraw |
			view_invalid::controller);
	}

	void zoom_100()
	{
		const auto& layout = current_zoom_layout();
		zoom_100({layout.viewport_extent.Width / 2.0, layout.viewport_extent.Height / 2.0});
	}

	void zoom_100(const pointd anchor)
	{
		auto& layout = current_zoom_layout();
		const auto source_anchor = current_zoom_state().source_point_at(
			layout.source_extent, layout.viewport_extent, zoom_fit_scale(), anchor);
		const auto old_scale = current_zoom_state().effective_scale(zoom_fit_scale());
		mutate_zoom([&](df::zoom_view_state& state)
		{
			state.set_anchored(1.0, old_scale, layout.source_extent, layout.viewport_extent, anchor);
		});
		mark_zoom_activity();
		layout.pending_source_anchor = source_anchor;
		layout.pending_client_anchor = layout.viewport_origin + anchor;
		layout.pending_reanchor = true;
		_async.invalidate_view(view_invalid::app_layout | view_invalid::view_layout | view_invalid::view_redraw |
			view_invalid::controller);
	}

	void zoom_fit_variant(const df::zoom_scale_mode mode)
	{
		const auto& layout = current_zoom_layout();
		mutate_zoom([&](df::zoom_view_state& state)
		{
			if (mode == df::zoom_scale_mode::fit_width)
				state.fit_width(layout.source_extent, layout.viewport_extent);
			else if (mode == df::zoom_scale_mode::fill)
				state.fill(layout.source_extent, layout.viewport_extent);
			else
				state.fit();
		});
		mark_zoom_activity();
		_async.invalidate_view(view_invalid::app_layout | view_invalid::view_layout | view_invalid::view_redraw |
			view_invalid::controller);
	}

	void zoom_scale(const double scale)
	{
		const auto& layout = current_zoom_layout();
		const pointd anchor{layout.viewport_extent.Width / 2.0, layout.viewport_extent.Height / 2.0};
		const auto old_scale = current_zoom_state().effective_scale(zoom_fit_scale());
		mutate_zoom([&](df::zoom_view_state& state)
		{
			state.set_anchored(scale, old_scale, layout.source_extent, layout.viewport_extent, anchor);
		});
		mark_zoom_activity();
		_async.invalidate_view(view_invalid::app_layout | view_invalid::view_layout | view_invalid::view_redraw |
			view_invalid::controller);
	}

	double zoom_fit_scale() const
	{
		const auto& layout = current_zoom_layout();
		return df::zoom_view_state::fit_scale(layout.source_extent, layout.viewport_extent, setting.scale_up);
	}

	double zoom_fit_scale(const df::zoom_pane pane) const
	{
		const auto& layout = _zoom_layouts[zoom_pane_index(pane)];
		return df::zoom_view_state::fit_scale(layout.source_extent, layout.viewport_extent, setting.scale_up);
	}

	void zoom_layout(const sized source, const sized viewport, const pointd viewport_origin,
	                 const df::zoom_pane pane = df::zoom_pane::primary)
	{
		auto& layout = _zoom_layouts[zoom_pane_index(pane)];
		layout.source_extent = source;
		layout.viewport_extent = viewport;
		layout.viewport_origin = viewport_origin;
		auto& state = _is_two ? _comparison_zoom.state(pane) : _common._zoom;
		state.update_source(source, zoom_fit_scale(pane));
		state.update_fit_variant(source, viewport, setting.scale_up);
		if (layout.pending_reanchor)
		{
			state.center_source_point_at(layout.pending_source_anchor, source, viewport, zoom_fit_scale(pane),
			                             layout.pending_client_anchor - viewport_origin);
			layout.pending_reanchor = false;
		}
	}

	const df::zoom_view_state& zoom_state() const noexcept
	{
		return current_zoom_state();
	}

	const df::zoom_view_state& zoom_state(const df::zoom_pane pane) const noexcept
	{
		return _is_two ? _comparison_zoom.state(pane) : _common._zoom;
	}

	void restore_zoom_state(const df::zoom_view_state& state)
	{
		mutate_zoom([&](df::zoom_view_state& current) { current = state; });
		_temporary_zoom = false;
		mark_zoom_activity();
		_async.invalidate_view(view_invalid::app_layout | view_invalid::view_layout | view_invalid::view_redraw |
			view_invalid::controller);
	}

	void inspect_at_100(const pointd anchor)
	{
		auto& layout = current_zoom_layout();
		const auto source_anchor = current_zoom_state().source_point_at(
			layout.source_extent, layout.viewport_extent, zoom_fit_scale(), anchor);
		const auto old_scale = current_zoom_state().effective_scale(zoom_fit_scale());
		mutate_zoom([&](df::zoom_view_state& state)
		{
			state.set_anchored(1.0, old_scale, layout.source_extent, layout.viewport_extent, anchor);
		});
		mark_zoom_activity();
		layout.pending_source_anchor = source_anchor;
		layout.pending_client_anchor = layout.viewport_origin + anchor;
		layout.pending_reanchor = true;
		_temporary_zoom = true;
		_async.invalidate_view(view_invalid::app_layout | view_invalid::view_layout | view_invalid::view_redraw |
			view_invalid::controller);
	}

	void commit_inspect()
	{
		if (_temporary_zoom)
		{
			_temporary_zoom = false;
			mark_zoom_activity();
			_async.invalidate_view(view_invalid::view_redraw | view_invalid::controller);
		}
	}

	bool is_temporary_zoom() const noexcept
	{
		return _temporary_zoom;
	}

	void zoom_center(const pointd center)
	{
		const auto scale = current_zoom_state().effective_scale(zoom_fit_scale());
		mutate_zoom([&](df::zoom_view_state& state) { state.set_explicit(scale, center); });
		mark_zoom_activity();
		_async.invalidate_view(view_invalid::view_layout | view_invalid::view_redraw);
	}

	void pan_zoom(const pointd client_delta, const df::zoom_view_state& start)
	{
		const auto& layout = current_zoom_layout();
		mutate_zoom([&](df::zoom_view_state& state)
		{
			state = start;
			const auto scale = state.effective_scale(zoom_fit_scale());
			state.pan_source({-client_delta.X / scale, -client_delta.Y / scale}, layout.source_extent,
			                 layout.viewport_extent, zoom_fit_scale());
		});
		mark_zoom_activity();
		_async.invalidate_view(view_invalid::view_layout | view_invalid::view_redraw);
	}

	void pan_zoom_by(const pointd client_delta)
	{
		const auto& layout = current_zoom_layout();
		mutate_zoom([&](df::zoom_view_state& state)
		{
			const auto scale = state.effective_scale(zoom_fit_scale());
			state.pan_source({client_delta.X / scale, client_delta.Y / scale}, layout.source_extent,
			                 layout.viewport_extent, zoom_fit_scale());
		});
		mark_zoom_activity();
		_async.invalidate_view(view_invalid::view_layout | view_invalid::view_redraw);
	}

	void pan_zoom_to_horizontal_edge(const bool last)
	{
		auto& layout = current_zoom_layout();
		mutate_zoom([&](df::zoom_view_state& state)
		{
			state.pan_source({last ? layout.source_extent.Width : -layout.source_extent.Width, 0.0},
			                 layout.source_extent, layout.viewport_extent, zoom_fit_scale());
		});
		mark_zoom_activity();
		_async.invalidate_view(view_invalid::view_layout | view_invalid::view_redraw);
	}

	void zoom_region(const rectd& region)
	{
		const auto& layout = current_zoom_layout();
		mutate_zoom([&](df::zoom_view_state& state)
		{
			state.zoom_region(region, layout.source_extent, layout.viewport_extent, zoom_fit_scale());
		});
		mark_zoom_activity();
		_async.invalidate_view(view_invalid::app_layout | view_invalid::view_layout | view_invalid::view_redraw |
			view_invalid::controller);
	}

	bool can_zoom() const
	{
		return _can_zoom;
	}

	void toggle_zoom()
	{
		if (current_zoom_state().is_fit()) zoom(true);
		else zoom(false);
	}

	void toggle_zoom_fit()
	{
		mutate_zoom([](df::zoom_view_state& state) { state.toggle_fit(); });
		mark_zoom_activity();
		_async.invalidate_view(view_invalid::app_layout | view_invalid::view_layout | view_invalid::view_redraw |
			view_invalid::controller);
	}

	bool player_has_video() const
	{
		return _player_media_info.has_video;
	}

	bool can_play_media() const
	{
		return (_player_media_info.has_video || _player_media_info.has_audio) && _session;
	}

	bool is_playing_media() const
	{
		return (_player_media_info.has_video || _player_media_info.has_audio) && _session && _session->is_playing();
	}

	bool is_playing() const
	{
		return _common._is_slideshow || is_playing_media();
	}

	bool is_slideshow() const
	{
		return _common._is_slideshow;
	}

	bool is_playing_slideshow() const
	{
		return _common._is_slideshow && (!_session || !_session->is_playing());
	}

	void stop_slideshow()
	{
		if (_common._is_slideshow)
		{
			_common._is_slideshow = false;
			_next_photo_tick = 0;
			// command_state too: the slideshow toggle and its related commands read this flag.
			// screen_saver because a photo slideshow is the only thing holding sleep off.
			_async.invalidate_view(view_invalid::view_redraw | view_invalid::command_state |
				view_invalid::screen_saver);
		}
	}

	bool display_item_has_trait(const file_traits t) const
	{
		return _is_one && _item1 && _item1->file_type()->has_trait(t);
	}

	int slideshow_pos() const
	{
		return df::mul_div(_next_photo_tick, 1000,
		                   std::max(1, setting.slideshow_delay) * ui::default_ticks_per_second);
	}

	void update_scrubber()
	{
		const auto& info = _player_media_info;

		if (info.has_video || info.has_audio)
		{
			auto invalid = false;
			const auto start = info.start;
			const auto end = info.end;
			const auto len = end - start;
			const auto media_pos = _session ? _session->last_frame_time() : 0.0;
			const auto pos = static_cast<int>((media_pos - start) * _scrubber_width / std::max(1.0, len));
			const auto endi = df::round(end);

			if (endi != _last_duration)
			{
				_duration = str::format_seconds(endi);
			}

			if (pos != _last_scrubber_pos)
			{
				_last_scrubber_pos = pos;
				invalid = true;
			}

			const auto position = df::round(media_pos);

			if (_last_seconds != position)
			{
				_last_seconds = position;
				_time = str::format_seconds(position);
				invalid = true;
			}

			if (invalid)
			{
				_async.invalidate_view(view_invalid::view_redraw);
			}
		}
	}

	void load_compare_preview(int elapsed_numerator, int elapsed_denominator);
	void load_seek_preview(int pos_numerator, int pos_denominator, std::function<void()> callback);

	void preview_loaded()
	{
		update_scrubber();
		_async.invalidate_view(view_invalid::view_redraw | view_invalid::tooltip);
	}


	bool is_multi() const
	{
		return _is_multi;
	}

	bool step()
	{
		bool invalid = false;
		invalid |= _loading_alpha_animation.step();
		invalid |= _selected_texture1 && _selected_texture1->step();
		invalid |= _selected_texture2 && _selected_texture2->step();
		return invalid;
	}

	void update_av_session(const std::shared_ptr<av_session>& ses);

	// Reads the head of the display item into _selected_item_data for the hex view.
	void load_selected_item_data();
	// False when a teardown or a newer open has taken the display since; the caller then owns closing
	// the session it was handed.
	bool publish_av_session(const std::shared_ptr<av_session>& ses, uint32_t generation);
	// A player session only reads the container, so a sidecar packet is fetched separately.
	void load_xmp_sidecar();
	void calc_pixel_difference();
};

struct group_and_item
{
	df::item_group_ptr group;
	df::item_element_ptr item;
};

class filter_t
{
	std::unordered_set<file_group_ref> _groups;
	std::string _text;
	std::string _input_text;

public:
	filter_t() = default;

	bool match_group(const file_group_ref group) const
	{
		return _groups.empty() || _groups.contains(group);
	}

	bool has_group(const file_group_ref group) const
	{
		return _groups.contains(group);
	}

	const std::unordered_set<file_group_ref>& groups() const
	{
		return _groups;
	}

	void add_group(const file_group_ref group)
	{
		_groups.emplace(group);
	}

	bool match_text(const str::cached text) const
	{
		if (_text.empty()) return true;
		if (_text.length() == 1 && _text[0] != '*' && _text[0] != '?') return starts(text, _text);
		return wildcard_icmp(text, _text);
	}

	bool match(const df::item_element_ptr& i) const
	{
		return match_text(i->name()) && match_group(i->file_type()->group);
	}

	void toggle(const file_group_ref g)
	{
		if (_groups.contains(g))
		{
			_groups.erase(g);
		}
		else
		{
			_groups.emplace(g);
		}
	}

	void clear()
	{
		_groups.clear();
		_text.clear();
		_input_text.clear();
	}

	void wildcard(const std::string_view text)
	{
		_input_text = text;
		if (text.length() > 1)
		{
			_text = "*";
			_text += text;
			_text += "*";
		}
		else
		{
			_text = text;
		}
	}

	[[nodiscard]] std::string text() const
	{
		return _input_text;
	}

	bool is_empty() const
	{
		return _text.empty() && _groups.empty();
	}
};

// Serialize the media-type groups of a filter to a stable comma-separated string
// (e.g. "photo,video") so the selection can be persisted between sessions.
inline std::string media_filter_to_string(const filter_t& filter)
{
	std::string result;

	for (const auto* const group : filter.groups())
	{
		if (!result.empty()) result += ",";
		result += group->name;
	}

	return result;
}

// Rebuild the media-type groups of a filter from a comma-separated string
// produced by media_filter_to_string. Unknown group names are ignored.
inline filter_t media_filter_from_string(const std::string_view text)
{
	filter_t result;

	for (const auto part : str::split(text, false, [](const wchar_t c) { return c == ','; }))
	{
		if (const auto* const group = parse_file_group(std::string(part)))
		{
			result.add_group(group);
		}
	}

	return result;
}

// Parent broadens a scope one named term or one folder level at a time. An empty parent means the
// scope is already as wide as it gets.
df::search_parent find_parent_search(const df::search_t& search);

struct item_edits
{
	metadata_edits metadata;
	image_edits image;
};

// What a batch write needs beyond the items. The edits are produced per item, on the worker, because
// a pixel edit depends on the file it is applied to; everything behind the write is the same either way.
struct batch_edit_spec
{
	// Runs on the worker, once per item, so it must capture only detached values - never a view,
	// control, item, or anything else UI-owned. Returns nothing for an item that could not be
	// prepared, which is reported as a failed item.
	std::function<std::optional<item_edits>(df::file_path)> make_edits;
	df::process_items_type process_type = df::process_items_type::can_save_metadata;
	file_encode_params encode_params;
	// Decided on the UI thread before the write, so it cannot consult the per-item edits: the caller
	// states whether this batch can change what is drawn. A pixel edit always can.
	bool changes_presentation = false;
};

class view_state
{
public:
	state_strategy& _events;
	async_strategy& _async;
	index_state& item_index;

	std::shared_ptr<av_player> _player;

private:
	friend class detach_file_handles;

	// Overlapping operations share one detached-display window. The first guard captures playback
	// state and the last guard restores it, so an earlier completion cannot reopen a file still in use.
	int _file_handle_detach_count = 0;
	void release_detached_file_handles(bool reopen_display);
	df::item_element_ptr _detached_display_item;
	bool _detached_display_is_playable = false;
	bool _detached_display_is_playing = false;
	bool _detached_display_should_reopen = true;
	int _detached_display_video_track = -1;
	int _detached_display_audio_track = -1;
	// The handle a write handed over for the detached item, consumed by the reopen. Cleared on every
	// release so a superseded reopen cannot leave the file open across the next rename or delete.
	platform::file_ptr _detached_display_handle;

	// The folders either side of the current folder scope. Finding them enumerates the containing
	// folder, so they are resolved on a worker and published here for the UI to read. An empty path
	// means there is no folder in that direction.
	struct sibling_folders_t
	{
		df::search_t scope;
		df::folder_path next;
		df::folder_path previous;
	};

	df::item_set _search_items;
	df::item_set _display_items;
	df::item_set _selected;

	df::item_element_ptr _focus;

	// Where a Shift range starts. Set by every non-extending selection so extending is
	// predictable instead of anchoring on whichever selected item happens to be nearest.
	df::item_element_ptr _selection_anchor;

	// The one item currently carrying view_element_style::hover. Cached so hit-testing can check the
	// hover-expanded interactive bounds without scanning every item.
	df::item_element_ptr _hover;

	df::item_groups _item_groups;

	df::file_group_histogram _summary_shown;
	df::file_group_histogram _summary_total;

	df::search_t _search;

	// Derived from _search alone, but asked for on every command-state update, so it is computed
	// once when the search changes.
	df::search_parent _parent_search;
	sibling_folders_t _sibling_folders;

	df::hash_map<std::string, map_location_area, df::ihash, df::ieq> _map_locations;
	filter_t _filter;
	common_display_state_t _common_display_state;

	// Set on the UI thread when a finished item hands over to the next one, and consumed by the
	// display-open path so the successor starts playing even when autoplay is off.
	bool _play_next_on_open = false;

	bool _search_is_favorite = false;
	bool _search_is_in_collection = false;

	group_by _group_order = group_by::file_type;
	sort_by _sort_order = sort_by::def;
	view_type _view_mode = view_type::none;

	// Bumped whenever the search or grouping changes. Group titles are derived from both, so this
	// stamp lets them be reused across the group_layout passes that only reshuffle items.
	uint32_t _group_title_generation = 1;

	// Memo of reverse-geocoded place names for the active display language. find_closest is a spatial
	// search over the whole place table, and many items share a coordinate.
	std::map<attribution_cell, located_place> _resolved_places;
	int _resolved_places_language = -1;

	// Coordinates already handed to the location worker, so a repainted panel queues each one once.
	std::set<gps_coordinate> _resolving_places;

	// Counting the items that share a day has no selector, so it walks every indexed folder. The
	// dates tooltip therefore never counts inline: it reads this memo, keyed on the packed day, and
	// the query worker fills it in. Cleared whenever the index summary changes.
	df::hash_map<uint32_t, uint64_t> _day_counts;
	df::hash_set<uint32_t> _counting_days;
	uint32_t _day_counts_generation = 1;

	// Bumped whenever the memo is cleared. A worker result carrying a stale stamp is dropped rather
	// than published, because the display language it was resolved in is no longer the current one.
	uint32_t _resolved_places_generation = 1;

	// locations.md 6.2: the derived timeline for the current result set. Deriving it reads the
	// gazetteer, so it is computed on the location worker from a detached snapshot of the results
	// and published back complete. The stamp is bumped on every refresh, so a result that arrives
	// after the results changed is answering a question nobody is asking now.
	df::visit_timeline _visits;
	uint32_t _visits_generation = 1;

	// Path of the cloud-only item we already queued a post-hydration rescan for, so the per-frame
	// check triggers it only once.
	df::file_path _hydration_rescan_done;
	uint64_t _hover_thumbnail_generation = 0;

	view_state(const view_state& other) = delete;
	const view_state& operator=(const view_state& other) = delete;

	void refresh_sibling_folders();

public:
	df::item_element_ptr _edit_item;
	df::item_element_ptr _pin_item;
	display_state_ptr _display;

	view_state(state_strategy& ev, async_strategy& ac, index_state& item_index, std::shared_ptr<av_player> player);
	~view_state();

	// When a cloud-only (OneDrive) item has been hydrated by viewing it in the big window,
	// queue a rescan so the items-view thumbnail and index (online status, hash) update.
	void rescan_hydrated_display_item();

	const df::item_element_ptr& focus_item() const
	{
		return _focus;
	}

	const df::item_element_ptr& hover_item() const
	{
		return _hover;
	}

	// Marks (or unmarks) an item as hovered, keeping the cached hover item in sync with the style bit.
	void hover_item(const view_host_base_ptr& view, const df::item_element_ptr& i, bool is_hover);

	bool search_is_favorite() const
	{
		return _search_is_favorite;
	}

	bool search_is_in_collection() const
	{
		return _search_is_in_collection;
	}

	bool has_error_items() const
	{
		for (const auto& g : _item_groups)
		{
			for (const auto& i : g->items())
			{
				if (i->is_error())
				{
					return true;
				}
			}
		}

		return false;
	}

	void clear_error_items(const view_host_base_ptr& view) const
	{
		for (const auto& g : _item_groups)
		{
			for (const auto& i : g->items())
			{
				i->is_error(false, view, i);
			}
		}
	}

	bool enter(const view_host_base_ptr& view);
	void toggle_full_screen() const { _events.toggle_full_screen(); }
	void make_visible(const df::item_element_ptr& i) const { _events.make_visible(i); }
	void invalidate_view(const view_invalid invalid) const { _async.invalidate_view(invalid); }

	bool has_pin() const
	{
		return _pin_item != nullptr;
	}

	view_elements_ptr create_selection_controls(bool compact = false);
	view_element_ptr create_selection_description();

	// locations.md 2.5/2.7: the UI-thread reader of derived location attribution. Resolving reads
	// the gazetteer file, so this only ever returns an answer already published back from the
	// location worker; an unknown coordinate queues one resolve and returns null this time.
	const located_place* derived_location(const gps_coordinate& coord);

	// The number of indexed items sharing this day, or null until the query worker answers. Counting
	// scans every indexed folder, so it never runs on the UI thread.
	const uint64_t* day_item_count(df::date_t d);

	// The counts describe the index, so a scan that changed it makes them stale.
	void invalidate_day_counts();

	// locations.md 6.2 / 7.2: the published visit nodes and place breakdown for the current
	// results. Empty until the worker answers, which keeps the strip out of the first paint after
	// a search rather than letting it appear and then re-shape itself.
	const df::visit_timeline& visits() const
	{
		return _visits;
	}

	// Takes the detached snapshot on the UI thread and queues the derivation.
	void refresh_visits();

	void command_hover(const ui::command_ptr& c, const recti window_bounds) const
	{
		_events.command_hover(c, window_bounds);
	}

	bool is_command_checked(const commands cmd) const
	{
		return _events.is_command_checked(cmd);
	}

	ui::command_ptr find_command(const commands cmd) const
	{
		return _events.find_command(cmd);
	}

	void track_menu(const ui::frame_ptr& parent, const recti recti, const std::vector<ui::command_ptr>& commands) const
	{
		_events.track_menu(parent, recti, commands);
	}

	df::item_element_ptr command_item() const
	{
		const auto d = _display;

		if (d && d->is_one())
		{
			return d->_item1;
		}

		return _focus;
	}

	uint64_t count_total(file_group_ref fg) const;

	const df::item_set& display_items() const
	{
		return _display_items;
	}

	void reset();
	void update_search_is_favorite_or_collection_root();
	void update_pixel_difference() const;


	bool is_full_screen = false;

	history_state history;

	recent_state recent_folders;
	recent_state recent_searches;
	recent_state recent_apps;
	recent_state recent_tags;
	recent_state recent_locations;

	void queue_ui(std::function<void()> f) const { _async.queue_ui(std::move(f)); }
	void queue_async(const async_queue q, std::function<void()> f) const { _async.queue_async(q, std::move(f)); }
	void queue_location(std::function<void(location_cache&)> f) const { _async.queue_location(std::move(f)); }

	display_state_ptr display_state() const
	{
		return _display;
	}

	// Adopts the image a write just produced for the displayed item, so the edit that changed the
	// pixels does not have to be read back off disk to be seen.
	void publish_written_image(df::file_path path, file_load_result loaded, df::date_t modified) const;
	// Adopts the open handle a write kept over the detached item, so the playback session that is
	// about to reopen it does not have to open the path again.
	void publish_written_handle(df::file_path path, platform::file_ptr file);
	// The item whose pixels are on screen, so a write to it can ask for its image back.
	std::vector<df::file_path> displayed_photo_paths() const;
	// The detached playable item that will be reopened, so a write to it can be asked for its handle.
	df::file_path detached_display_av_path() const;

	df::folder_counts known_folders() const;

	df::item_element_ptr end_item(bool forward) const;
	df::item_element_ptr next_item(bool forward, bool extend) const;
	df::item_element_ptr next_media_item(bool forward, bool wrap) const;
	bool can_slideshow() const;
	df::item_element_ptr next_group_item(bool forward) const;
	df::item_element_ptr next_unselected_item() const;

	df::folder_path save_path() const;

	ui::const_image_ptr first_selected_thumb() const;
	void capture_display(const std::function<void(file_load_result)>& f) const;

	df::string_counts selected_tags() const;

	group_by group_order() const
	{
		return _group_order;
	}

	// What the visible groups are actually built from. A related search groups by relation whatever
	// the user last chose, and leaves that choice intact for the next ordinary search.
	group_by effective_group_order() const
	{
		return _search.has_related() ? group_by::related : _group_order;
	}

	uint32_t group_title_generation() const
	{
		return _group_title_generation;
	}

	void display_language_changed()
	{
		++_group_title_generation;
	}

	sort_by sort_order() const
	{
		return _sort_order;
	}

	std::string next_path(bool forward) const;
	bool has_next_path(bool forward) const;
	void open_next_path(const view_host_base_ptr& view, bool forward);

	view_type view_mode() const
	{
		return _view_mode;
	}

	bool is_items_or_media_view() const
	{
		return _view_mode == view_type::items || _view_mode == view_type::media;
	}

	bool has_display_items() const
	{
		return !_item_groups.empty();
	}

	bool has_selection() const
	{
		return !_selected.empty();
	}

	struct selection_status_result
	{
		bool is_playing = false;
		bool is_playing_media = false;
		bool is_slideshow = false;
		bool can_play_media = false;
		bool can_zoom = false;
		bool has_single_media_selection = false;
		bool has_single_folder_selection = false;
		bool showing_image = false;
	};

	selection_status_result selection_status() const
	{
		selection_status_result result;

		const auto d = _display;

		if (d)
		{
			result.is_playing = d->is_playing();
			result.is_playing_media = d->is_playing_media();
			result.is_slideshow = d->is_slideshow();
			result.can_play_media = d->can_play_media();
			result.can_zoom = d->can_zoom();

			if (d->is_one() && _selected.size() == 1)
			{
				const auto i = d->_item1;

				if (i)
				{
					const auto ft = i->file_type();
					result.has_single_media_selection = i->is_media();
					result.showing_image = ft->has_trait(file_traits::bitmap) || d->player_has_video();
				}
			}
		}

		result.has_single_folder_selection = _selected.size() == 1 && _selected.has_folders();

		return result;
	}

	void clear_filters()
	{
		_filter.clear();
		invalidate_view(view_invalid::command_state | view_invalid::group_layout);
	}

	bool can_edit_media() const
	{
		const auto d = _display;

		if (d && d->is_one() && _selected.size() == 1)
		{
			const auto i = d->_item1;
			return i && i->file_type()->can_edit_photo();
		}

		return false;
	}

	bool should_show_overlays() const
	{
		if (_display && _display->is_zoom_mode())
		{
			return ui::ticks_since_last_user_action < ui::default_ticks_per_second * 5 || df::command_active;
		}

		if (_selected.has_folders())
		{
			return true;
		}

		if (_selected.items().size() != 1 || _selected.size() != 1)
		{
			return true;
		}

		if (view_mode() == view_type::items)
		{
			return true;
		}

		if (!_selected.items()[0]->file_type()->has_trait(file_traits::hide_overlays))
		{
			return true;
		}

		return ui::ticks_since_last_user_action < ui::default_ticks_per_second * 5 || df::command_active;
	}

	const df::search_t& search() const
	{
		return _search;
	}

	void map_locations(const std::vector<map_location_area>& locations)
	{
		_map_locations.clear();
		for (const auto& location : locations)
		{
			if (!location.name.empty()) _map_locations[location.name] = location;
		}
	}

	const filter_t& filter() const
	{
		return _filter;
	}

	filter_t& filter()
	{
		return _filter;
	}

	const df::item_set& search_items() const
	{
		return _search_items;
	}

	const df::item_set& selected_items() const
	{
		return _selected;
	}

	bool has_gps() const
	{
		for (const auto& i : _selected.items())
		{
			const auto md = i->metadata();

			if (md && md->coordinate.is_valid())
			{
				return true;
			}
		}

		return false;
	}

	void open_gps_on_google_maps() const
	{
		for (const auto& i : _selected.items())
		{
			const auto md = i->metadata();

			if (md && md->coordinate.is_valid())
			{
				const auto coordinate = md->coordinate;
				platform::open(str::print("https://www.google.com/maps/place/%f,%f", coordinate.latitude(),
				                          coordinate.longitude()));
				return;
			}
		}
	}

	// Open the GPS coordinate of the first selected item that has one using a
	// url template from diffractor-tools.json ("maps" section). The template may
	// contain {latitude} and {longitude} tokens.
	void open_gps_on_map(const std::string_view url_template) const
	{
		for (const auto& i : _selected.items())
		{
			const auto md = i->metadata();

			if (md && md->coordinate.is_valid())
			{
				const auto coordinate = md->coordinate;
				auto substitute = [&coordinate](std::ostringstream& result, const std::string_view token)
				{
					if (token == "latitude") result << str::print("%f", coordinate.latitude());
					else if (token == "longitude") result << str::print("%f", coordinate.longitude());
				};

				platform::open(str::replace_tokens(url_template, substitute));
				return;
			}
		}
	}

	const df::file_group_histogram& summary_shown() const
	{
		return _summary_shown;
	}

	const df::item_groups& groups() const
	{
		return _item_groups;
	}

	void browse_back(const view_host_base_ptr& view);
	void browse_forward(const view_host_base_ptr& view);
	void close() const;


	df::search_parent parent_search() const
	{
		return _parent_search;
	}

	bool has_parent_search() const
	{
		return !_parent_search.parent.is_empty();
	}

	void open(const view_host_base_ptr& view, std::string_view text);
	void open(const view_host_base_ptr& view, df::file_path path);
	bool resolve_area_search(df::search_t& search) const;
	bool open(const view_host_base_ptr& view, const df::search_t& path, const df::unique_paths& selection);
	void open(const view_host_base_ptr& view, const df::item_element_ptr& i);
	void load_display_state();

	void change_tracks(int video_track, int audio_track) const;
	void change_audio_device(const std::string& id) const;
	// The only way to publish a player session onto a display; see display_state_t::_av_generation.
	// An open handle, when supplied, is moved into the session instead of opening the path again.
	void open_av_session(const std::shared_ptr<display_state_t>& d, const df::item_element_ptr& i, bool auto_play,
	                     int video_track, int audio_track, bool use_last_played_pos,
	                     platform::file_ptr file = {}) const;
	void play(const view_host_base_ptr& view);
	void toggle_slideshow(const view_host_base_ptr& view);

	df::unique_items existing_items() const;

	void append_items(const view_host_base_ptr& view, df::item_set items, const df::unique_paths& selection,
	                  bool is_first, bool is_complete);

	df::process_result can_process_selection_and_mark_errors(const view_host_base_ptr& view,
	                                                         df::process_items_type file_types) const;
	bool can_process_selection(const view_host_base_ptr& view, df::process_items_type file_types) const;
	// Same test as can_process_selection, but keeps the reason so a disabled command can explain itself.
	df::process_result selection_process_result(df::process_items_type file_types) const;

	size_t selected_count() const
	{
		return _selected.size();
	}

	void select_all(const view_host_base_ptr& view);
	void select_nothing(const view_host_base_ptr& view);
	void select_inverse(const view_host_base_ptr& view);
	void select_end(const view_host_base_ptr& view, bool forward, bool toggle, bool extend);
	void select_next(const view_host_base_ptr& view, bool forward, bool toggle, bool extend);
	void select_next_media(const view_host_base_ptr& view, bool forward);
	bool select(const view_host_base_ptr& view, std::string_view file_name, bool toggle);
	void select(const view_host_base_ptr& view, const df::item_elements& items, bool toggle);
	void select(const view_host_base_ptr& view, const df::item_element_ptr& i, bool toggle, bool extend,
	            bool continue_slideshow);
	void select(const view_host_base_ptr& view, recti selection_bounds, bool toggle);
	void unselect(const view_host_base_ptr& view, const df::item_element_ptr& i);

	group_and_item item_group(const df::item_element_ptr& ii) const
	{
		for (const auto& g : _item_groups)
		{
			for (const auto& i : g->items())
			{
				if (i == ii)
				{
					return {g, i};
				}
			}
		}

		return {};
	}

	df::item_element_ptr item_from_location(pointi loc) const;
	// Layout bounds only: grid navigation must not see the caption overhang that lets the hovered or
	// focused item win a pointer hit test, or a step off the focused item lands back on it.
	df::item_element_ptr item_from_layout_location(pointi loc) const;

	group_and_item selected_item_group() const;
	bool is_item_displayed(const df::item_element_ptr& first_selection) const;

	group_and_item first_selected() const
	{
		for (const auto& g : _item_groups)
		{
			for (const auto& i : g->items())
			{
				if (i->is_selected())
				{
					return {g, i};
				}
			}
		}

		return {};
	}

	bool escape(const view_host_base_ptr& view);
	bool update_selection();
	df::item_element_ptr find_displayed_item_by_name(std::string_view file_name) const;

	void update_item_groups();
	void toggle_group_order();
	void group_order(std::optional<group_by> group, std::optional<sort_by> order);
	void stop();
	void tick(const view_host_base_ptr& view, double time_now);
	void view_mode(view_type m);
	void toggle_rating(const df::results_ptr& results, const df::item_elements& items, int r,
	                   const view_host_base_ptr& view);
	int displayed_rating() const;

	void modify_items(const df::results_ptr& dlg, icon_index icon, std::string_view title,
	                  const df::item_elements& items_to_modify, const metadata_edits& edits,
	                  const view_host_base_ptr& view);
	void modify_items(const ui::control_frame_ptr& frame, icon_index icon, std::string_view title,
	                  const df::item_elements& items_to_modify, const metadata_edits& edits,
	                  const view_host_base_ptr& view);
	// One in-place batch write. The caller says what each item's edit is; everything behind the write -
	// the write claim, the scan taken through the coherent handle, the image and handle handed to the
	// display, and the background rescan - is the same whether the edit touches metadata, pixels, or both.
	void modify_items(const df::results_ptr& results, const df::item_elements& items_to_modify,
	                  const batch_edit_spec& spec, const view_host_base_ptr& view);

	void invoke(const commands id) const
	{
		_events.invoke(id);
	}

	void stop_slideshow() const
	{
		const auto d = _display;

		if (d)
		{
			d->stop_slideshow();
		}
	}

	void load_hover_thumb(const df::item_element_ptr& drawable_item, double pos_numerator, double pos_denominator);
	void clear_hover_codec();
};


class detach_file_handles
{
	view_state& _state;

	bool _reopen_display = true;

public:
	explicit detach_file_handles(view_state& s);
	~detach_file_handles();
	void keep_display_closed() { _reopen_display = false; }

	detach_file_handles(const detach_file_handles& other) = delete;
	detach_file_handles(detach_file_handles&& other) noexcept = delete;
	detach_file_handles& operator=(const detach_file_handles& other) = delete;
	detach_file_handles& operator=(detach_file_handles&& other) noexcept = delete;
};


class edit_view_state
{
public:
	view_state& _state;
	ui::animate_alpha grid_alpha_animation;

	double _straighten = 0.0;
	double _perspective_horizontal = 0.0;
	double _perspective_vertical = 0.0;
	double _vibrance = 0.0;
	double _darks = 0.0;
	double _midtones = 0.0;
	double _lights = 0.0;
	double _contrast = 0.0;
	double _brightness = 0.0;
	double _saturation = 0.0;
	double _temperature = 0.0;
	double _tint = 0.0;
	bool _preview_mode = false;

	image_edits _edits;
	image_edits _original_edits;

	edit_view_state(view_state& s) : _state(s)
	{
	}

	quadd selection() const
	{
		return _edits.crop_bounds();
	}

	void selection(const quadd& s)
	{
		_edits.crop_bounds(s);
	}

	void color_reset()
	{
		_vibrance = 0;
		_darks = 0;
		_midtones = 0;
		_lights = 0;
		_contrast = 0;
		_brightness = 0;
		_saturation = 0;
		_temperature = 0;
		_tint = 0;
	}

	void reset(const prop::item_metadata_const_ptr& md, sizei dimensions, ui::orientation orientation);

	bool has_pixel_changes() const
	{
		return _edits != _original_edits;
	}

	static quadd initial_crop(const sizei dimensions, const ui::orientation orientation)
	{
		return quadd(dimensions).transform(to_simple_transform_inv(orientation));
	}

	static double calc_straighten(const double a)
	{
		auto s = fmod(a, 90);
		if (s > 45) s -= 90;
		if (s < -45) s += 90;
		return s;
	}

	void changed(const sizei extent, const bool straighten_tracking)
	{
		if (_state._edit_item && _state._edit_item->file_type()->has_trait(file_traits::bitmap))
		{
			const auto selection = _edits.crop_bounds();
			const auto current = selection.angle();
			const auto current_straighten = calc_straighten(current);

			_edits.crop_bounds(selection.rotate(_straighten - current_straighten, selection.center_point()));
			grid_alpha_animation.target(straighten_tracking ? 0.6f : 0.0f);

			_edits.perspective_horizontal(_perspective_horizontal);
			_edits.perspective_vertical(_perspective_vertical);
			_edits.vibrance(_vibrance);
			_edits.darks(_darks);
			_edits.midtones(_midtones);
			_edits.lights(_lights);
			_edits.contrast(_contrast);
			_edits.brightness(_brightness);
			_edits.saturation(_saturation);
			_edits.temperature(_temperature);
			_edits.tint(_tint);
		}
	}
};
