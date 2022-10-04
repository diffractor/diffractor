// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2022  Zac Walker
//
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

#pragma once

#include "model_propery.h"
#include "model_items.h"
#include "model_locations.h"
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


class location_cache;
class database;
class av_player;
class av_format_decoder;
class av_format_decoder;
class av_format_decoder;
class scrubber_element;
class display_state_t;

using display_state_ptr = std::shared_ptr<display_state_t>;

ui::texture_sampler calc_sampler(sizei draw_extent, sizei texture_extent, const ui::orientation& orientation);
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

class async_strategy : public av_host, public df::async_i
{
public:
	~async_strategy() override = default;

	void queue_ui(std::function<void()> f) override = 0;
	void queue_async(async_queue q, std::function<void()> f) override = 0;
	virtual void queue_location(std::function<void(location_cache&)>) = 0;
	virtual void queue_database(std::function<void(database&)> f) = 0;

	virtual void queue_media_preview(std::function<void(media_preview_state&)>) = 0;

	virtual void web_service_cache(std::u8string key, std::function<void(const std::u8string&)> f) = 0;
	virtual void web_service_cache(std::u8string key, std::u8string value) = 0;
};

struct state_strategy
{
	virtual ~state_strategy() = default;

	virtual void toggle_full_screen() = 0;
	virtual bool can_open_search(const df::search_t& path) = 0;
	virtual void item_focus_changed(const df::item_element_ptr& focus, const df::item_element_ptr& previous) = 0;
	virtual void display_changed() = 0;
	virtual void view_changed(view_type m) = 0;
	virtual void play_state_changed(bool play) = 0;
	virtual void search_complete(const df::search_t& path, bool path_changed) = 0;
	virtual void invoke(commands id) = 0;
	virtual void track_menu(const ui::frame_ptr& parent, recti bounds,
	                        const std::vector<ui::command_ptr>& commands) = 0;
	virtual void make_visible(const df::item_element_ptr& i) = 0;
	virtual void command_hover(const ui::command_ptr& c, recti window_bounds) = 0;
	virtual bool is_command_checked(commands cmd) = 0;
	virtual void element_broadcast(const view_element_event& event) = 0;
	virtual void focus_view() = 0;
	virtual void free_graphics_resources(bool items_only, bool offscreen_only) = 0;
	virtual void delete_items(const df::item_set& items) = 0;

	virtual void invalidate_view(view_invalid invalid) = 0;
};

class recent_state : public df::no_copy
{
public:
	const static int max_size = 64;
	std::vector<std::u8string> _items;

public:
	recent_state() = default;

	const std::vector<std::u8string>& items() const
	{
		return _items;
	}

	void count_strings(df::string_counts& results, const int weight, const std::u8string& prefix = {})
	{
		for (const auto& i : _items)
		{
			if (prefix.empty())
			{
				results[i] += weight;
			}
			else
			{
				results[str::cache(prefix + i)] += weight;
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

	void add(const std::u8string_view v)
	{
		for (auto i = _items.cbegin(); i != _items.cend(); ++i)
		{
			if (compare(*i, v) == 0)
			{
				_items.erase(i);
				break;
			}
		}

		if (_items.size() > max_size)
		{
			_items.erase(_items.cbegin());
		}

		_items.emplace_back(v);
	}

	static void add_list(std::vector<std::u8string>& results, const std::vector<std::u8string_view>& adds)
	{
		for (const auto& a : adds)
		{
			results.emplace_back(a);
		}
	}

	static void add_list(std::vector<std::u8string>& results, const std::vector<std::u8string>& adds)
	{
		for (const auto& a : adds)
		{
			results.emplace_back(a);
		}
	}

	static void add_list(std::vector<df::folder_path>& results, const std::vector<std::u8string_view>& adds)
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

	std::u8string combine(const std::vector<std::u8string>& strings) const
	{
		return str::combine(strings);
	}

	std::u8string combine(const std::vector<df::folder_path>& strings) const
	{
		return combine_paths(strings);
	}

	int compare(const std::u8string_view l, const std::u8string_view r) const
	{
		return str::icmp(l, r);
	}

	static int compare(const df::folder_path l, const df::folder_path r)
	{
		return l.compare(r);
	}

	void read(const std::u8string_view section, const std::u8string_view key,
	          const platform::setting_file_ptr& properties)
	{
		std::u8string str;
		properties->read(section, key, str);
		add_items(str::split(str, true));
	}

	void write(const std::u8string_view section, const std::u8string_view key,
	           const platform::setting_file_ptr& properties)
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
	int _pos = -1;

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

	bool move_history_pos(const int n, history_entry& result)
	{
		const auto i = _pos + n;

		if (i >= static_cast<int>(_history.size()) || i < 0)
		{
			return false;
		}

		_pos = i;
		result = _history[i];
		return true;
	}

	void history_add(const df::search_t& link, df::paths selected)
	{
		if (_pos != -1)
		{
			_history[_pos].selected = std::move(selected);

			if (_history[_pos].search == link)
			{
				return;
			}

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
	ui::texture_ptr _tex;
	ui::texture_ptr _vid_tex;
	async_strategy& _async;

	friend class display_state_t;

private:
	df::file_path _path;
	file_load_result _loaded;
	ui::const_surface_ptr _staged_surface;

	df::date_t _photo_timestamp;
	sizei _loading_scale_hint;

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
	df::date_t _zoom_timestamp;

public:
	bool _is_video_tex = false;
	bool _tex_invalid = true;
	bool _is_photo = false;
	bool _is_raw = false;
	bool _photo_loaded = false;

	sizei _display_dimensions;
	ui::orientation _display_orientation = ui::orientation::top_left;

	texture_state(async_strategy& async, const df::file_item_ptr& i);

	void load_image(const df::file_item_ptr& i);
	void load_raw();

	void refresh(const df::file_item_ptr& i);
	void draw(ui::draw_context& rc, pointi offset, int compare_pos, bool first_texture);
	void layout(ui::measure_context& mc, recti bounds, const df::file_item_ptr& i);
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

	const df::date_t photo_timestamp() const
	{
		return _photo_timestamp;
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

	float display_alpha_animation_val() const
	{
		return _display_alpha_animation.val();
	}

	bool is_preview() const
	{
		return !_loaded.is_empty() && _loaded.is_preview;
	}

	bool is_preview_rendering() const
	{
		return _preview_rendering != 0;
	}
};

struct common_display_state_t
{
	bool _is_playing = false;
};

using texture_state_ptr = std::shared_ptr<texture_state>;

class display_state_t final : public std::enable_shared_from_this<display_state_t>
{
public:
	async_strategy& _async;
	common_display_state_t& _common;

	bool _zoom = false;
	bool _comparing = false;

	size_t _item_pos = 0;
	size_t _break_count = 0;
	size_t _total_count = 0;

	std::shared_ptr<av_session> _session;

	df::blob _selected_item_data;
	std::vector<archive_item> _archive_items;
	av_media_info _player_media_info;

	ui::vertices_ptr _verts;
	texture_state_ptr _selected_texture1;
	texture_state_ptr _selected_texture2;

	df::file_item_ptr _item1;
	df::file_item_ptr _item2;

	int _next_photo_tick = 0;

	mutable recti _scrubber_bounds;
	int _hover_scrubber_pos = -1;
	mutable int _time_width = 0;

	ui::pixel_difference_result _pixel_difference;

	mutable bool _preview_changed = false;

	int _last_scrubber_pos = -1;
	int _last_duration = -1;
	int _last_seconds = -1;

	std::u8string _duration;
	std::u8string _time;

	ui::const_surface_ptr _hover_surface;

	mutable ui::vertices_ptr _audio_verts;
	mutable pointi _audio_element_offset;
	mutable recti _audio_element_bounds;
	mutable float _audio_element_alpha = 1.0f;

	int _compare_hover_loc = 0;
	pointi _image_offset;
	recti compare_control_bounds;
	recti _compare_bounds;
	recti _compare_limits;
	bool _can_compare = false;
	bool _is_compare_video = false;
	bool _is_one = false;
	bool _is_two = false;
	bool _is_multi = false;

	std::vector<ui::const_image_ptr> _images;
	std::vector<ui::const_surface_ptr> _surfaces;
	mutable std::vector<ui::texture_ptr> _textures;
	std::vector<recti> _surface_bounds;
	const size_t max_surfaces = 6;
	pointi media_offset;

	ui::animate_alpha _loading_alpha_animation;

	explicit display_state_t(async_strategy& async, common_display_state_t& common)
		: _async(async), _common(common)
	{
	}

	void populate(view_state& state);

	bool is_compare_video_preview(const pointi loc) const
	{
		if (_is_compare_video)
		{
			return _selected_texture1->display_bounds().contains(loc) ||
				_selected_texture2->display_bounds().contains(loc);
		}

		return false;
	}

	recti compare_video_preview_bounds(const pointi loc) const
	{
		if (_selected_texture1 && _selected_texture2)
		{
			const auto bounds1 = _selected_texture1->display_bounds();

			if (bounds1.contains(loc))
			{
				return bounds1;
			}

			const auto bounds2 = _selected_texture2->display_bounds();

			if (bounds2.contains(loc))
			{
				return bounds2;
			}
		}

		return {};
	}

	bool is_one() const
	{
		return _is_one;
	}

	bool is_two() const
	{
		return _is_two;
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

	bool zoom() const
	{
		return _zoom;
	}

	void zoom(const bool zoom)
	{
		if (_zoom != zoom)
		{
			stop_slideshow();
			_zoom = zoom;
			_async.invalidate_view(view_invalid::view_layout | view_invalid::tooltip | view_invalid::controller);
		}
	}

	bool player_has_audio() const
	{
		return _player_media_info.has_audio;
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
		return _common._is_playing || is_playing_media();
	}

	bool is_playing_slideshow() const
	{
		return _common._is_playing && (!_session || !_session->is_playing());
	}

	void stop_slideshow()
	{
		if (_common._is_playing)
		{
			_common._is_playing = false;
			_next_photo_tick = 0;
			_async.invalidate_view(view_invalid::view_redraw);
		}
	}

	bool can_zoom() const
	{
		return _is_one && _item1 && _item1->file_type()->has_trait(file_type_traits::zoom);
	}

	bool display_item_has_trait(const file_type_traits t) const
	{
		return _is_one && _item1 && _item1->file_type()->has_trait(t);
	}

	int slideshow_pos() const
	{
		return df::mul_div(_next_photo_tick, 1000, setting.slideshow_delay * ui::default_ticks_per_second);
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
			const auto pos = static_cast<int>(((media_pos - start) * _scrubber_bounds.width()) / std::max(1.0, len));
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

	void load_compare_preview(int pos_numerator, int pos_denominator);
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
	void calc_pixel_difference();
};

struct group_and_item
{
	df::item_group_ptr group;
	df::item_element_ptr item;
};

class filter_t
{
private:
	std::unordered_set<file_group_ref> _groups;
	std::u8string _text;

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

	bool match_text(const str::cached text) const
	{
		if (_text.empty()) return true;
		if (_text.length() == 1 && _text[0] != '*' && _text[0] != '?') return starts(text, _text);
		return wildcard_icmp(text, _text);
	}

	bool match(const df::file_item_ptr& i) const
	{
		return match_text(i->name()) && match_group(i->file_type()->group);
	}

	bool match(const df::folder_item_ptr& f) const
	{
		return match_text(f->name()) && match_group(f->file_type()->group);
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
	}

	void wildcard(const std::u8string_view text)
	{
		if (text.length() > 1)
		{
			_text = u8"*";
			_text += text;
			_text += u8"*";
		}
		else
		{
			_text = text;
		}
	}

	[[nodiscard]] std::u8string text() const
	{
		return _text;
	}

	bool is_empty()
	{
		return _text.empty() && _groups.empty();
	}
};

class view_state
{
public:
	state_strategy& _events;
	async_strategy& _async;
	index_state& item_index;

	std::shared_ptr<av_player> _player;

private:
	df::item_set _search_items;
	df::item_set _display_items;
	df::item_set _selected;

	df::item_element_ptr _focus;

	df::item_groups _item_groups;

	df::file_group_histogram _summary_shown;
	df::file_group_histogram _summary_total;

	df::search_t _search;
	filter_t _filter;
	common_display_state_t _common_display_state;

	bool _search_is_favorite = false;

	group_by _group_order = group_by::file_type;
	sort_by _sort_order = sort_by::def;
	view_type _view_mode = view_type::None;


	view_state(const view_state& other) = delete;
	const view_state& operator=(const view_state& other) = delete;

public:
	df::file_item_ptr _edit_item;
	df::item_element_ptr _pin_item;
	display_state_ptr _display;

	view_state(state_strategy& ev, async_strategy& ac, index_state& item_index, std::shared_ptr<av_player> player);
	~view_state();

	const df::item_element_ptr& focus_item() const
	{
		return _focus;
	}

	/*bool is_photo() const
	{
		return _display_item && _display_item->file_type()->is_photo;
	}

	bool is_audio() const
	{
		return _display_item && _display_item->file_type()->is_audio;
	}*/

	bool search_is_favorite() const
	{
		return _search_is_favorite;
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
	void toggle_full_screen() { _events.toggle_full_screen(); }
	void make_visible(const df::item_element_ptr& i) { _events.make_visible(i); }
	void invalidate_view(const view_invalid invalid) { _async.invalidate_view(invalid); }

	bool has_pin() const
	{
		return _pin_item != nullptr;
	}

	view_elements_ptr create_selection_controls();

	void command_hover(const ui::command_ptr& c, const recti window_bounds)
	{
		_events.command_hover(c, window_bounds);
	}

	bool is_command_checked(const commands cmd)
	{
		return _events.is_command_checked(cmd);
	}

	void track_menu(const ui::frame_ptr& parent, const recti recti, const std::vector<ui::command_ptr>& commands)
	{
		_events.track_menu(parent, recti, commands);
	}

	df::file_item_ptr command_item() const
	{
		const auto d = _display;

		if (d && d->is_one())
		{
			return d->_item1;
		}

		return std::dynamic_pointer_cast<df::file_item>(_focus);
	}

	uint32_t count_total(file_group_ref fg) const;
	uint32_t count_shown(file_group_ref fg) const;
	uint32_t display_item_count() const;

	const df::item_set& display_items() const
	{
		return _display_items;
	}

	void reset();
	void update_search_is_favorite();
	void update_pixel_difference();


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

	df::folder_counts known_folders() const;

	df::item_element_ptr end_item(bool forward) const;
	df::item_element_ptr next_item(bool forward, bool extend) const;
	df::item_element_ptr next_group_item(bool forward) const;
	df::item_element_ptr next_unselected_item() const;

	df::folder_path save_path() const;
	icon_index displayed_item_icon() const;
	ui::const_image_ptr first_selected_thumb() const;
	void capture_display(std::function<void(file_load_result)> f) const;

	df::string_counts selected_tags() const;

	group_by group_order() const
	{
		return _group_order;
	}

	sort_by sort_order() const
	{
		return _sort_order;
	}

	std::u8string next_path(bool forward) const;
	void open_next_path(const view_host_base_ptr& view, bool forward);

	view_type view_mode() const
	{
		return _view_mode;
	}

	bool has_status() const
	{
		return has_selection();
	}

	bool is_editing() const
	{
		return _view_mode == view_type::edit;
	}

	bool is_showing_media_or_items() const
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

	bool all_items_filtered_out() const
	{
		return !_search_items.empty() &&
			_display_items.empty();
	}

	void clear_filters()
	{
		_filter.clear();
		invalidate_view(view_invalid::command_state | view_invalid::group_layout | view_invalid::filters);
	}

	bool can_play() const
	{
		const auto d = _display;
		return d && !d->is_playing() && !is_editing();
	}

	bool can_edit_media() const
	{
		const auto d = _display;

		if (d && d->is_one() && _selected.size() == 1)
		{
			const auto i = d->_item1;
			return i && i->is_media();
		}

		return false;
	}

	bool should_show_overlays() const
	{
		if (!_selected.folders().empty())
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

		if (!_selected.items()[0]->file_type()->has_trait(file_type_traits::hide_overlays))
		{
			return true;
		}

		return ui::ticks_since_last_user_action < (ui::default_ticks_per_second * 5) || df::command_active;
	}

	const df::search_t& search() const
	{
		return _search;
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
				platform::open(str::print(u8"https://www.google.com/maps/place/%f,%f", coordinate.latitude(),
				                          coordinate.longitude()));
				return;
			}
		}
	}

	void toggle_selected_item_tags(const view_host_base_ptr& view, const df::results_ptr& results,
	                               std::u8string_view tag);

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
	void close();


	df::search_parent parent_search() const;

	bool has_parent_search() const
	{
		return !parent_search().parent.is_empty();
	}

	void open(const view_host_base_ptr& view, std::u8string_view text);
	void open(const view_host_base_ptr& view, df::file_path path);
	bool open(const view_host_base_ptr& view, const df::search_t& path, const df::unique_paths& selection);
	void open(const view_host_base_ptr& view, const df::item_element_ptr& i);
	void load_display_state();

	void change_tracks(int video_track, int audio_track);
	void change_audio_device(const std::u8string& id);
	void play(const view_host_base_ptr& view);

	df::unique_items existing_items() const;

	void append_items(const view_host_base_ptr& view, df::item_set items, const df::unique_paths& selection,
	                  bool is_first, bool is_complete);

	df::process_result can_process_selection_and_mark_errors(const view_host_base_ptr& view,
	                                                         df::process_items_type file_types) const;
	bool can_process_selection(const view_host_base_ptr& view, df::process_items_type file_types) const;

	size_t selected_count() const
	{
		return _selected.size();
	}

	void select_all(const view_host_base_ptr& view);
	void select_nothing(const view_host_base_ptr& view);
	void select_inverse(const view_host_base_ptr& view);
	void select_end(const view_host_base_ptr& view, bool forward, bool toggle, bool extend);
	void select_next(const view_host_base_ptr& view, bool forward, bool toggle, bool extend);
	bool select(const view_host_base_ptr& view, std::u8string_view file_name, bool toggle);
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
	df::item_element_ptr find_displayed_item_by_name(std::u8string_view file_name) const;

	void update_item_groups();
	void toggle_group_order();
	void group_order(std::optional<group_by> group, std::optional<sort_by> order);
	void stop();
	void tick(const view_host_base_ptr& view, double time_now);
	void view_mode(view_type m);
	void toggle_rating(const df::results_ptr& results, const df::file_items& items, int r,
	                   const view_host_base_ptr& view);
	int displayed_rating() const;

	void modify_items(const df::results_ptr& dlg, icon_index icon, std::u8string_view title,
	                  const df::file_items& items_to_modify, const metadata_edits& edits,
	                  const view_host_base_ptr& view);
	void modify_items(const ui::control_frame_ptr& frame, icon_index icon, std::u8string_view title,
	                  const df::file_items& items_to_modify, const metadata_edits& edits,
	                  const view_host_base_ptr& view);

	static quadd orientate(recti bounds, recti clip, const ui::orientation orientation)
	{
		const auto t = to_simple_transform(orientation);

		if (flips_xy(orientation))
		{
			std::swap(clip.left, clip.top);
			std::swap(clip.right, clip.bottom);
			std::swap(bounds.left, bounds.top);
			std::swap(bounds.right, bounds.bottom);
		}

		if (t == simple_transform::flip_h || t == simple_transform::rot_180 || t == simple_transform::transverse || t ==
			simple_transform::rot_270)
		{
			const auto w = clip.width();
			clip.left = bounds.right - clip.right;
			clip.right = clip.left + w;
		}

		if (t == simple_transform::rot_90 || t == simple_transform::transverse || t == simple_transform::rot_180 || t ==
			simple_transform::flip_v)
		{
			const auto h = clip.height();
			clip.top = bounds.bottom - clip.bottom;
			clip.bottom = clip.top + h;
		}

		return quadd(clip).transform(t);
	}

	void invoke(commands id) const
	{
		_events.invoke(id);
	}

	void stop_slideshow()
	{
		const auto d = _display;

		if (d)
		{
			d->stop_slideshow();
		}
	}

	void load_hover_thumb(const df::item_element_ptr& drawable_item, double pos_numerator, double pos_denominator);
	void clear_hover_codec() const;
};


class detach_file_handles
{
	view_state& _state;

	df::file_item_ptr _i;
	df::item_set _selected;
	bool _is_playable = false;
	bool _is_playing = false;
	int _video_track = -1;
	int _audio_track = -1;

public:
	detach_file_handles(view_state& s, bool should_close = false);
	~detach_file_handles();

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

	std::u8string _item_title;
	std::u8string _item_tags;
	std::u8string _item_description;
	std::u8string _item_comment;
	std::u8string _item_artist;
	std::u8string _item_album;
	std::u8string _item_album_artist;
	std::u8string _item_genre;
	std::u8string _item_show;
	df::date_t _item_created;

	int _item_season = 0;
	df::xy8 _item_episode;
	int _item_year = 0;
	int _item_rating = 0;

	df::xy8 _item_track_number = {0, 0};
	df::xy8 _item_disk_number = {0, 0};

	double _straighten = 0.0;
	double _vibrance = 0.0;
	double _darks = 0.0;
	double _midtones = 0.0;
	double _lights = 0.0;
	double _contrast = 0.0;
	double _brightness = 0.0;
	double _saturation = 0.0;

	image_edits _edits;

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
	}

	void reset(const prop::item_metadata_ptr& md, sizei dimensions);
	metadata_edits build_metadata_edits(const prop::item_metadata_ptr& md) const;

	static double calc_straighten(double a)
	{
		auto s = fmod(a, 90);
		if (s > 45) s -= 90;
		if (s < -45) s += 90;
		return s;
	}

	void changed(const sizei extent, bool straighten_tracking)
	{
		if (_state._edit_item && _state._edit_item->file_type()->has_trait(file_type_traits::bitmap))
		{
			const auto selection = _edits.crop_bounds();
			const auto current = selection.angle();
			const auto current_straighten = calc_straighten(current);

			_edits.crop_bounds(selection.rotate(_straighten - current_straighten, selection.center_point()));
			grid_alpha_animation.target(straighten_tracking ? 0.6f : 0.0f);

			_edits.vibrance(_vibrance);
			_edits.darks(_darks);
			_edits.midtones(_midtones);
			_edits.lights(_lights);
			_edits.contrast(_contrast);
			_edits.brightness(_brightness);
			_edits.saturation(_saturation);
		}
	}
};
