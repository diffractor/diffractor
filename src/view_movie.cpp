// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Implements the Movie task view -- the timeline strip, the composed preview and its
// transport, the two-halved settings panel, and the source frames both the strip and the preview
// draw. Every rule about the document lives in model_movie; this only presents it.

#include "pch.h"
#include "view_movie.h"

#include "app_text.h"
#include "av_format.h"
#include "files.h"
#include "ui_elements.h"

namespace
{
	constexpr int timeline_tile_cx = 96;
	constexpr int timeline_tile_cy = 72;
	// Frames are cached at whole steps so a scrub reuses them instead of decoding one per pixel.
	constexpr double frame_step_seconds = 0.04;
	constexpr size_t max_cached_frames = 12;

	double quantize(const double time)
	{
		return std::round(time / frame_step_seconds) * frame_step_seconds;
	}

	std::string format_clock(const double seconds)
	{
		const auto total = static_cast<int>(std::max(0.0, seconds));
		return std::format("{}:{:02}", total / 60, total % 60);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// The source frames.
//
// One decode at a time, off the UI thread, latest wins. Everything published comes back through
// queue_ui and is checked against the generation it was asked under, so a frame decoded for a
// superseded scrub is dropped rather than drawn over the settled one.
///////////////////////////////////////////////////////////////////////////////////////////////////

struct movie_frame_key
{
	df::file_path path;
	int time_ms = 0;
	int max_dim = 0;

	bool operator==(const movie_frame_key& other) const
	{
		return time_ms == other.time_ms && max_dim == other.max_dim && path == other.path;
	}
};

// Every input the decoded frame depends on is in the key: a key that omitted the size would serve
// the timeline's thumbnail to the preview.
struct movie_frame_key_hash
{
	size_t operator()(const movie_frame_key& k) const noexcept
	{
		return df::ihash{}(k.path) ^ (static_cast<size_t>(k.time_ms) * 2654435761u) ^ (k.max_dim * 40503u);
	}
};

class movie_source_cache final : public std::enable_shared_from_this<movie_source_cache>
{
public:
	explicit movie_source_cache(view_state& s) : _state(s)
	{
	}

	// Answers from the cache when it can, and asks for the frame when it cannot. A miss returns
	// null and the caller draws nothing there until the answer arrives and invalidates the view.
	ui::const_surface_ptr fetch(const df::file_path path, const bool is_photo, const double time, const int max_dim)
	{
		if (path.is_empty() || max_dim <= 0) return {};

		const movie_frame_key key{path, is_photo ? 0 : static_cast<int>(std::lround(quantize(time) * 1000)), max_dim};

		if (const auto found = _cache.find(key); found != _cache.end())
		{
			touch(key);
			return found->second;
		}

		request(key, is_photo);
		return {};
	}

	void clear()
	{
		++_generation;
		_cache.clear();
		_order.clear();
		_pending.reset();
		_in_flight = false;
	}

	// Bumped whenever what is being asked for changes, so results for the previous question are
	// discarded instead of overwriting the current one.
	void supersede() { ++_generation; }

private:
	void touch(const movie_frame_key& key)
	{
		std::erase(_order, key);
		_order.emplace_back(key);
	}

	void publish(const movie_frame_key& key, ui::const_surface_ptr surface, const uint64_t generation)
	{
		_in_flight = false;

		if (generation == _generation && is_valid(surface))
		{
			_cache.insert_or_assign(key, std::move(surface));
			touch(key);

			while (_order.size() > max_cached_frames)
			{
				_cache.erase(_order.front());
				_order.erase(_order.begin());
			}

			_state.invalidate_view(view_invalid::view_redraw);
		}

		if (_pending)
		{
			const auto next = *_pending;
			_pending.reset();
			request(next.first, next.second);
		}
	}

	void request(const movie_frame_key& key, const bool is_photo)
	{
		if (_in_flight)
		{
			// Latest wins: the frame the user is asking for now matters, the one they scrubbed past
			// does not.
			_pending = std::make_pair(key, is_photo);
			return;
		}

		_in_flight = true;

		const auto weak = weak_from_this();
		const auto generation = _generation;

		_state.queue_async(async_queue::render, [weak, key, is_photo, generation, &s = _state]
		{
			auto surface = decode(key, is_photo);

			s.queue_ui([weak, key, generation, surface = std::move(surface)]() mutable
			{
				// The weak pointer is a lifetime token only; it is not locked until the work is
				// back on the thread that owns the cache.
				if (const auto self = weak.lock()) self->publish(key, std::move(surface), generation);
			});
		});
	}

	static ui::const_surface_ptr decode(const movie_frame_key& key, const bool is_photo)
	{
		const sizei max_dim{key.max_dim, key.max_dim};

		if (is_photo)
		{
			const auto loaded = files{}.load(key.path, true);
			return loaded.success ? loaded.to_surface(max_dim) : nullptr;
		}

		av_format_decoder decoder;
		if (!decoder.open(key.path, media_intent::thumbnail)) return {};

		const auto duration = decoder.end_time() - decoder.start_time();
		if (duration <= 0) return {};

		ui::surface_ptr surface;
		const auto wanted = std::clamp(key.time_ms / 1000.0, 0.0, duration);

		return decoder.extract_thumbnail(surface, max_dim, wanted, duration, true, 0.0) ? surface : nullptr;
	}

	view_state& _state;
	df::hash_map<movie_frame_key, ui::const_surface_ptr, movie_frame_key_hash> _cache;
	std::vector<movie_frame_key> _order;
	std::optional<std::pair<movie_frame_key, bool>> _pending;
	uint64_t _generation = 0;
	bool _in_flight = false;
};

///////////////////////////////////////////////////////////////////////////////////////////////////
// The timeline.
//
// This is the document, not a view of the selection: the order is meaningful, a source may appear
// twice, and removing a tile removes it from the movie. That is why it is not the selector strip.
///////////////////////////////////////////////////////////////////////////////////////////////////

class movie_timeline_element final : public view_element, public std::enable_shared_from_this<movie_timeline_element>
{
public:
	movie_timeline_element(view_state& s, movie_view* view) : view_element(view_element_style::can_invoke),
	                                                          _state(s), _view(view)
	{
		padding = {4, 4};
	}

	sizei measure(ui::measure_context& mc, const int cx) const override
	{
		return {cx, df::round(timeline_tile_cy * mc.scale_factor) + mc.padding2 * 3 + mc.text_line_height(
			        ui::style::font_face::dialog)};
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		const auto& project = _view->project();
		const auto r = bounds.offset(element_offset);

		dc.draw_rect(r, ui::color(ui::style::color::group_background, dc.colors.alpha));

		const auto tile_cx = df::round(timeline_tile_cx * dc.scale_factor);
		const auto tile_cy = df::round(timeline_tile_cy * dc.scale_factor);
		const auto text_cy = dc.text_line_height(ui::style::font_face::dialog);
		const auto playing_index = clip_at_playhead();

		auto x = r.left + dc.padding2 - _scroll;

		for (size_t i = 0; i < project.size(); ++i)
		{
			const recti tile{x, r.top + dc.padding2, x + tile_cx, r.top + dc.padding2 + tile_cy};
			x += tile_cx + dc.padding2;

			if (tile.right < r.left || tile.left > r.right) continue;

			const auto& clip = project.clips()[i];
			const auto is_current = i == project.current();

			dc.draw_rect(tile, ui::color(is_current
				                             ? ui::style::color::dialog_selected_background
				                             : ui::style::color::dialog_background, dc.colors.alpha));

			draw_thumbnail(dc, clip, tile.inflate(-dc.padding1));

			// Current and playing are different facts, so they are drawn differently: the current
			// clip is what the panel describes, the playing one is where the movie has got to.
			if (static_cast<int>(i) == playing_index)
			{
				dc.draw_border(tile.inflate(-1), tile, ui::color(ui::style::color::important_background,
				                                                dc.colors.alpha),
				               ui::color(ui::style::color::important_background, 0.0f));
			}

			const recti label{tile.left, tile.bottom, tile.right, tile.bottom + text_cy};
			const auto text = clip.is_trimmed()
				                  ? std::format("{} *", format_clock(clip.duration()))
				                  : format_clock(clip.duration());

			dc.draw_text(text, label, ui::style::font_face::dialog, ui::style::text_style::single_line_center,
			             ui::color(dc.colors.foreground, dc.colors.alpha), {});
		}
	}

	// Which tile the point is over, or -1.
	int hit(const pointi loc, const float scale_factor, const int padding2) const
	{
		if (!bounds.contains(loc)) return -1;

		const auto tile_cx = df::round(timeline_tile_cx * scale_factor);
		const auto offset = loc.x - (bounds.left + padding2) + _scroll;

		if (offset < 0) return -1;

		const auto index = offset / std::max(1, tile_cx + padding2);
		return index < static_cast<int>(_view->project().size()) ? index : -1;
	}

	void scroll_by(const int dx, const float scale_factor, const int padding2)
	{
		const auto tile_cx = df::round(timeline_tile_cx * scale_factor);
		const auto total = static_cast<int>(_view->project().size()) * (tile_cx + padding2);
		_scroll = std::clamp(_scroll + dx, 0, std::max(0, total - bounds.width() + padding2 * 2));
	}

	void reveal(const size_t index, const float scale_factor, const int padding2)
	{
		const auto tile_cx = df::round(timeline_tile_cx * scale_factor);
		const auto left = static_cast<int>(index) * (tile_cx + padding2);
		const auto right = left + tile_cx;

		if (left < _scroll) _scroll = std::max(0, left - padding2);
		else if (right - _scroll > bounds.width()) _scroll = right - bounds.width() + padding2 * 2;
	}

	void dispatch_event(const view_element_event& event) override
	{
		if (event.type == view_element_event_type::free_graphics_resources) _textures.clear();
	}

	int drop_index(const pointi loc, const float scale_factor, const int padding2) const
	{
		const auto tile_cx = std::max(1, df::round(timeline_tile_cx * scale_factor) + padding2);
		const auto offset = loc.x - (bounds.left + padding2) + _scroll;
		return std::clamp((offset + tile_cx / 2) / tile_cx, 0, static_cast<int>(_view->project().size()));
	}

	// Set while a tile is being dragged, so the drop point can be drawn between tiles rather than
	// on one: dropping onto a clip has no meaning in a single-track timeline.
	int _drag_from = -1;
	int _drag_to = -1;

private:
	int clip_at_playhead() const
	{
		const auto& project = _view->project();
		const auto frame = calc_movie_frame(project.clips(), project.settings(), _view->movie_state().playhead);
		return frame.a.index;
	}

	void draw_thumbnail(ui::draw_context& dc, const movie_clip& clip, const recti target) const
	{
		const auto surface = _view->sources()->fetch(clip.path, clip.is_photo, clip.start,
		                                             std::max(target.width(), target.height()));

		if (!is_valid(surface))
		{
			dc.draw_text(clip.path.name().sv(), target, ui::style::font_face::dialog,
			             ui::style::text_style::multiline_center,
			             ui::color(dc.colors.foreground, dc.colors.alpha * 0.5f), {});
			return;
		}

		auto& entry = _textures[surface.get()];

		if (!entry)
		{
			auto t = dc.create_texture();
			if (t && t->update(surface) != ui::texture_update_result::failed) entry = t;
		}

		if (entry)
		{
			// Fitted, never stretched: the movie letterboxes mixed aspect ratios and so does the
			// strip that stands for it.
			const auto fitted = ui::scale_dimensions(surface->dimensions(), target.extent(), false);
			dc.draw_texture(entry, recti(fitted).offset(target.center() - recti(fitted).center()), dc.colors.alpha);
		}
	}

	view_state& _state;
	movie_view* _view;
	int _scroll = 0;
	mutable df::hash_map<const void*, ui::texture_ptr> _textures;
};

///////////////////////////////////////////////////////////////////////////////////////////////////
// The trim control: one track, two handles.
///////////////////////////////////////////////////////////////////////////////////////////////////

class movie_trim_control final : public view_element, public std::enable_shared_from_this<movie_trim_control>
{
public:
	movie_trim_control(movie_view* view) : view_element(view_element_style::can_invoke), _view(view)
	{
		padding = {2, 6};
	}

	double _start = 0;
	double _end = 0;
	double _limit = 0;
	// Which handle is captured, so the preview can be scrubbed to that handle's frame while it
	// moves and returned to the playhead when it is released.
	int _tracking = -1;

	sizei measure(ui::measure_context& mc, const int cx) const override
	{
		return {cx, mc.text_line_height(ui::style::font_face::dialog) * 2};
	}

	void render(ui::draw_context& dc, const pointi element_offset) const override
	{
		const auto r = bounds.offset(element_offset);
		const auto track = track_bounds(r, dc.padding1);
		const auto clr = ui::color(dc.colors.foreground, dc.colors.alpha);

		dc.draw_rect(track, ui::color(ui::style::color::dialog_background, dc.colors.alpha));

		if (_limit <= 0) return;

		const recti selected{to_x(track, _start), track.top, to_x(track, _end), track.bottom};
		dc.draw_rect(selected, ui::color(ui::style::color::dialog_selected_background, dc.colors.alpha));

		draw_handle(dc, track, _start, clr);
		draw_handle(dc, track, _end, clr);

		const recti label{r.left, track.bottom + dc.padding1, r.right, r.bottom};
		dc.draw_text(std::format("{} - {}", format_clock(_start), format_clock(_end)), label,
		             ui::style::font_face::dialog, ui::style::text_style::single_line_center, clr, {});
	}

	// Which handle the point is nearest, when it is near enough to grab one.
	int hit_handle(const pointi loc, const int padding1) const
	{
		if (_limit <= 0 || !bounds.contains(loc)) return -1;

		const auto track = track_bounds(bounds, padding1);
		const auto grab = std::max(6, padding1 * 3);
		const auto dx_start = std::abs(loc.x - to_x(track, _start));
		const auto dx_end = std::abs(loc.x - to_x(track, _end));

		if (std::min(dx_start, dx_end) > grab) return -1;
		return dx_start <= dx_end ? 0 : 1;
	}

	double time_at(const pointi loc, const int padding1) const
	{
		const auto track = track_bounds(bounds, padding1);
		if (track.width() <= 0 || _limit <= 0) return 0;
		return std::clamp(static_cast<double>(loc.x - track.left) / track.width() * _limit, 0.0, _limit);
	}

private:
	static recti track_bounds(const recti r, const int padding1)
	{
		const auto height = std::max(4, padding1 * 2);
		return {r.left + padding1 * 2, r.top + padding1, r.right - padding1 * 2, r.top + padding1 + height};
	}

	int to_x(const recti track, const double time) const
	{
		if (_limit <= 0) return track.left;
		return track.left + df::round(track.width() * std::clamp(time / _limit, 0.0, 1.0));
	}

	void draw_handle(ui::draw_context& dc, const recti track, const double time, const ui::color clr) const
	{
		const auto x = to_x(track, time);
		const auto half = std::max(3, dc.padding1);
		dc.draw_rounded_rect({x - half, track.top - half, x + half, track.bottom + half}, clr, half);
	}

	movie_view* _view;
};

///////////////////////////////////////////////////////////////////////////////////////////////////
// Controllers
///////////////////////////////////////////////////////////////////////////////////////////////////

namespace
{
	class timeline_controller final : public view_controller
	{
	public:
		timeline_controller(view_host_ptr host, const recti bounds, movie_view* view,
		                    std::shared_ptr<movie_timeline_element> strip, const int index) :
			view_controller(std::move(host), bounds), _view(view), _strip(std::move(strip)), _index(index)
		{
		}

		ui::style::cursor cursor() const override { return ui::style::cursor::link; }

		void on_mouse_left_button_down(const pointi loc, const ui::key_state keys) override
		{
			view_controller::on_mouse_left_button_down(loc, keys);
			if (_index >= 0) _view->set_current(static_cast<size_t>(_index));
		}

		void on_mouse_move(const pointi loc) override
		{
			if (_index < 0) return;
			if (std::abs(loc.x - _start_loc.x) < 8) return;

			_strip->_drag_from = _index;
			_strip->_drag_to = _strip->drop_index(loc, 1.0f, 6);
			_host->frame()->invalidate();
		}

		void on_mouse_left_button_up(const pointi loc, const ui::key_state keys) override
		{
			if (_strip->_drag_from >= 0 && _strip->_drag_to >= 0)
			{
				_view->move_clip(static_cast<size_t>(_strip->_drag_from), static_cast<size_t>(_strip->_drag_to));
			}

			_strip->_drag_from = _strip->_drag_to = -1;
		}

		bool escape() override
		{
			if (_strip->_drag_from < 0) return false;
			_strip->_drag_from = _strip->_drag_to = -1;
			return true;
		}

	private:
		movie_view* _view;
		std::shared_ptr<movie_timeline_element> _strip;
		int _index;
	};

	class scrubber_controller final : public view_controller
	{
	public:
		scrubber_controller(view_host_ptr host, const recti bounds, movie_view* view) :
			view_controller(std::move(host), bounds), _view(view)
		{
		}

		ui::style::cursor cursor() const override { return ui::style::cursor::left_right; }

		void on_mouse_left_button_down(const pointi loc, const ui::key_state keys) override
		{
			view_controller::on_mouse_left_button_down(loc, keys);
			seek(loc);
		}

		void on_mouse_move(const pointi loc) override { seek(loc); }

	private:
		void seek(const pointi loc) const
		{
			if (_bounds.width() <= 0) return;
			const auto fraction = std::clamp(static_cast<double>(loc.x - _bounds.left) / _bounds.width(), 0.0, 1.0);
			_view->seek(fraction * _view->duration());
		}

		movie_view* _view;
	};

	class trim_controller final : public view_controller
	{
	public:
		trim_controller(view_host_ptr host, const recti bounds, movie_view* view,
		                std::shared_ptr<movie_trim_control> trim, const int handle) :
			view_controller(std::move(host), bounds), _view(view), _trim(std::move(trim)), _handle(handle)
		{
			_trim->_tracking = handle;
		}

		~trim_controller() override
		{
			_trim->_tracking = -1;
		}

		ui::style::cursor cursor() const override { return ui::style::cursor::left_right; }

		void on_mouse_left_button_down(const pointi loc, const ui::key_state keys) override
		{
			view_controller::on_mouse_left_button_down(loc, keys);
			move(loc);
		}

		void on_mouse_move(const pointi loc) override { move(loc); }

		void on_mouse_left_button_up(const pointi loc, const ui::key_state keys) override
		{
			_trim->_tracking = -1;
			_view->trim_current(_trim->_start, _trim->_end);
		}

	private:
		void move(const pointi loc) const
		{
			const auto time = _trim->time_at(loc, 3);

			// The handles cannot cross, and a clip cannot be trimmed to nothing.
			if (_handle == 0) _trim->_start = std::min(time, _trim->_end - 0.1);
			else _trim->_end = std::max(time, _trim->_start + 0.1);

			_view->seek_preview_only(_handle == 0 ? _trim->_start : _trim->_end);
		}

		movie_view* _view;
		std::shared_ptr<movie_trim_control> _trim;
		int _handle;
	};
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// The document's bound settings
///////////////////////////////////////////////////////////////////////////////////////////////////

void movie_view_state::read_from_project()
{
	const auto& s = project.settings();

	crossfade = s.transition == movie_transition::crossfade;
	cut = !crossfade;
	transition_tenths = std::clamp(df::round(s.transition_seconds * 10), 0, 100);
	fade_in = s.fade_in;
	fade_out = s.fade_out;
	photo_tenths = std::clamp(df::round(s.photo_seconds * 10), 1, 600);

	if (const auto* const clip = project.current_clip(); clip && clip->is_photo)
	{
		clip_hold_tenths = std::clamp(df::round(clip->duration() * 10), 1, 600);
		clip_hold_is_default = clip->photo_duration_is_default;
	}
}

movie_settings movie_view_state::to_settings() const
{
	movie_settings result = project.settings();

	result.transition = crossfade ? movie_transition::crossfade : movie_transition::cut;
	result.transition_seconds = transition_tenths / 10.0;
	result.fade_in = fade_in;
	result.fade_out = fade_out;
	result.photo_seconds = std::max(0.1, photo_tenths / 10.0);

	return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// The controls panel
///////////////////////////////////////////////////////////////////////////////////////////////////

void movie_view_controls::create_controls()
{
	auto changed = [this] { _view->settings_changed(); };

	_info = create_view_info_element(tt.movie_info);

	_movie_title = std::make_shared<ui::title_control>(icon_index::video, tt.movie_settings_title);
	_crossfade_check = std::make_shared<ui::check_control>(_dlg, tt.movie_crossfade, _movie_state.crossfade, true,
	                                                       false, [this](bool) { _view->settings_changed(); });
	_cut_check = std::make_shared<ui::check_control>(_dlg, tt.movie_cut, _movie_state.cut, true, false,
	                                                 [this](bool) { _view->settings_changed(); });
	_transition_slider = std::make_shared<ui::slider_control>(_dlg, tt.movie_transition_length,
	                                                          _movie_state.transition_tenths, 0, 50, changed);
	_fade_in_check = std::make_shared<ui::check_control>(_dlg, tt.movie_fade_in, _movie_state.fade_in, false, false,
	                                                     [this](bool) { _view->settings_changed(); });
	_fade_out_check = std::make_shared<ui::check_control>(_dlg, tt.movie_fade_out, _movie_state.fade_out, false,
	                                                      false, [this](bool) { _view->settings_changed(); });
	_photo_slider = std::make_shared<ui::slider_control>(_dlg, tt.movie_photo_duration, _movie_state.photo_tenths, 1,
	                                                     300, changed);
	_output_text = std::make_shared<text_element>(std::string_view{}, ui::style::text_style::multiline);

	_clip_divider = std::make_shared<divider_element>();
	_clip_title = std::make_shared<ui::title_control>(icon_index::edit, tt.movie_clip_title);
	_clip_text = std::make_shared<text_element>(std::string_view{}, ui::style::text_style::multiline);
	_trim = std::make_shared<movie_trim_control>(_view.get());
	_clip_hold_slider = std::make_shared<ui::slider_control>(_dlg, tt.movie_clip_duration,
	                                                         _movie_state.clip_hold_tenths, 1, 300,
	                                                         [this] { _view->hold_changed(); });
	_clip_hold_default_check = std::make_shared<ui::check_control>(_dlg, tt.movie_use_default,
	                                                               _movie_state.clip_hold_is_default, false, false,
	                                                               [this](bool) { _view->hold_changed(); });

	_controls = {
		_info,
		_movie_title,
		_crossfade_check,
		_cut_check,
		_transition_slider,
		_fade_in_check,
		_fade_out_check,
		_photo_slider,
		_output_text,
		_clip_divider,
		_clip_title,
		_clip_text,
		_trim,
		_clip_hold_slider,
		_clip_hold_default_check,
	};

	_clr = ui::color(ui::style::color::dialog_text);
	update_for_document();
}

void movie_view_controls::update_for_document()
{
	if (_controls.empty()) return;

	const auto& project = _movie_state.project;
	const auto* const clip = project.current_clip();
	const auto output = project.output();
	const auto timing = project.timing();

	_output_text->text(str_format(tt.movie_output_fmt.sv(), output.extent.cx, output.extent.cy, output.frame_rate,
	                              format_clock(timing.duration)));

	_transition_slider->is_visible(_movie_state.crossfade);

	const auto has_clip = clip != nullptr;
	const auto is_photo = has_clip && clip->is_photo;

	_clip_divider->is_visible(has_clip);
	_clip_title->is_visible(has_clip);
	_clip_text->is_visible(has_clip);
	_trim->is_visible(has_clip && !is_photo);
	_clip_hold_slider->is_visible(is_photo && !_movie_state.clip_hold_is_default);
	_clip_hold_default_check->is_visible(is_photo);

	if (has_clip)
	{
		_clip_text->text(str_format(tt.movie_clip_fmt.sv(), clip->path.name().sv(),
		                            static_cast<int>(project.current()) + 1, static_cast<int>(project.size()),
		                            format_clock(timing.starts[project.current()])));

		_trim->_start = clip->start;
		_trim->_end = clip->end;
		_trim->_limit = clip->source_duration > 0 ? clip->source_duration : clip->end;
	}
}

void movie_view_controls::layout_controls(ui::measure_context& mc)
{
	if (!_controls.empty())
	{
		update_for_document();
		view_controls_host::layout_controls(mc);
	}
}

void movie_view_controls::options_changed()
{
	view_controls_host::options_changed();

	if (!_controls.empty())
	{
		_info->text(tt.movie_info);
		_movie_title->text(tt.movie_settings_title);
		_transition_slider->label(tt.movie_transition_length);
		_photo_slider->label(tt.movie_photo_duration);
		_clip_title->text(tt.movie_clip_title);
		_clip_hold_slider->label(tt.movie_clip_duration);
		update_for_document();
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// The view
///////////////////////////////////////////////////////////////////////////////////////////////////

movie_view::movie_view(view_state& s, view_host_ptr host, movie_view_state& ms) :
	_state(s), _host(std::move(host)), _movie_state(ms)
{
	_sources = std::make_shared<movie_source_cache>(s);
}

movie_view::~movie_view() = default;

view_controls_host_ptr movie_view::controls(const ui::control_frame_ptr& owner)
{
	if (!_controls)
	{
		_controls = std::make_shared<movie_view_controls>(_state, _movie_state);
		_controls->_view = shared_from_this();
		_controls->_frame = _controls->_dlg = owner->create_dlg(_controls, false);
	}

	return _controls;
}

void movie_view::activate(const sizei extent)
{
	_extent = extent;

	if (!_timeline)
	{
		_timeline = std::make_shared<movie_timeline_element>(_state, this);
	}

	if (_controls->_controls.empty())
	{
		_controls->create_controls();
	}

	_state.stop();
	_movie_state.is_playing = false;

	// Entering with a selection seeds an empty timeline. An existing one is left alone: the
	// document is the thing being edited, and re-entering must not silently rewrite it.
	if (_movie_state.project.is_empty())
	{
		add_items(_state.selected_items());
	}
	else
	{
		probe_clips();
	}

	_movie_state.read_from_project();
	changed();
}

void movie_view::deactivate()
{
	_movie_state.is_playing = false;
	_sources->clear();
	_texture_a.reset();
	_texture_b.reset();
	_drawn_a.reset();
	_drawn_b.reset();
}

void movie_view::refresh()
{
	_host->frame()->invalidate();
}

void movie_view::broadcast_event(const view_element_event& event) const
{
	if (event.type == view_element_event_type::free_graphics_resources)
	{
		_texture_a.reset();
		_texture_b.reset();
		_drawn_a.reset();
		_drawn_b.reset();
	}

	if (_timeline) _timeline->dispatch_event(event);
}

std::string_view movie_view::title()
{
	_title = std::format("{}: {}", s_app_name, tt.movie_title);
	return _title;
}

double movie_view::duration() const
{
	return _movie_state.project.timing().duration;
}

bool movie_view::can_render() const
{
	return !_movie_state.project.is_empty() && platform::can_write_movies();
}

void movie_view::layout(ui::measure_context& mc, const sizei extent)
{
	_extent = extent;

	const auto outer = recti(extent).inflate(-mc.padding2);
	const auto strip_cy = _timeline ? _timeline->measure(mc, outer.width()).cy : 0;
	const auto transport_cy = mc.text_line_height(ui::style::font_face::dialog) + mc.padding2 * 2;

	const auto strip_top = std::max(outer.top, outer.bottom - strip_cy);
	const recti strip{outer.left, strip_top, outer.right, outer.bottom};

	const auto transport_top = std::max(outer.top, strip.top - mc.padding2 - transport_cy);
	_transport_bounds = {outer.left, transport_top, outer.right, strip.top - mc.padding2};

	const auto play_cx = mc.icon_cxy * 2;
	_play_bounds = {
		_transport_bounds.left, _transport_bounds.top, _transport_bounds.left + play_cx, _transport_bounds.bottom
	};

	const auto clock_cx = mc.measure_text("000:00 / 000:00", ui::style::font_face::dialog,
	                                      ui::style::text_style::single_line, _transport_bounds.width()).cx;

	_scrubber_bounds = {
		_play_bounds.right + mc.padding2, _transport_bounds.top,
		std::max(_play_bounds.right + mc.padding2, _transport_bounds.right - clock_cx - mc.padding2),
		_transport_bounds.bottom
	};

	_preview_bounds = {outer.left, outer.top, outer.right, std::max(outer.top, _transport_bounds.top - mc.padding2)};

	if (_timeline)
	{
		ui::control_layouts positions;
		_timeline->layout(mc, strip, positions);
	}
}

recti movie_view::calc_preview_target(const sizei source) const
{
	if (source.cx <= 0 || source.cy <= 0) return {};

	// Fitted, never stretched. Mixed aspect ratios letterbox on black, which is what the render
	// does, and the preview exists to be believed.
	const auto fitted = ui::scale_dimensions(source, _preview_bounds.extent(), false);
	return recti(fitted).offset(_preview_bounds.center() - recti(fitted).center());
}

void movie_view::render(ui::draw_context& dc, view_controller_ptr controller)
{
	const auto& project = _movie_state.project;

	dc.draw_rect(_preview_bounds, ui::color(0, dc.colors.alpha));

	if (project.is_empty())
	{
		dc.draw_text(tt.movie_empty, _preview_bounds, ui::style::font_face::dialog,
		             ui::style::text_style::multiline_center,
		             ui::color(dc.colors.foreground, dc.colors.alpha), {});
	}
	else
	{
		auto frame = calc_movie_frame(project.clips(), project.settings(), _movie_state.playhead);

		// While a trim handle is captured the preview shows that handle's frame, so the user sets
		// the trim by looking at it rather than by reading a number.
		if (_preview_override)
		{
			frame = {};
			frame.a.index = static_cast<int>(project.current());
			frame.a.source_time = *_preview_override;
			frame.a.weight = 1.0;
		}

		const auto max_dim = std::max(_preview_bounds.width(), _preview_bounds.height());

		const auto draw_source = [&](const movie_frame_source& source, ui::texture_ptr& texture,
		                             ui::const_surface_ptr& drawn, const float weight)
		{
			if (source.index < 0 || weight <= 0.0f) return;

			const auto& clip = project.clips()[source.index];
			const auto surface = _sources->fetch(clip.path, clip.is_photo, source.source_time, max_dim);
			if (!is_valid(surface)) return;

			if (!texture || drawn != surface)
			{
				auto t = dc.create_texture();
				if (!t || t->update(surface) == ui::texture_update_result::failed) return;
				texture = t;
				drawn = surface;
			}

			dc.draw_texture(texture, calc_preview_target(surface->dimensions()), dc.colors.alpha * weight);
		};

		draw_source(frame.a, _texture_a, _drawn_a, static_cast<float>(frame.a.weight));
		draw_source(frame.b, _texture_b, _drawn_b, static_cast<float>(frame.b.weight));

		if (frame.fade_to_black > 0)
		{
			dc.draw_rect(_preview_bounds,
			             ui::color(0, dc.colors.alpha * static_cast<float>(frame.fade_to_black)));
		}
	}

	const auto clr = ui::color(dc.colors.foreground, dc.colors.alpha);
	const auto total = duration();

	xdraw_icon(dc, _movie_state.is_playing ? icon_index::pause : icon_index::play, _play_bounds, clr, {});

	dc.draw_rect(_scrubber_bounds, ui::color(ui::style::color::dialog_background, dc.colors.alpha));

	if (total > 0)
	{
		const auto x = _scrubber_bounds.left + df::round(
			_scrubber_bounds.width() * std::clamp(_movie_state.playhead / total, 0.0, 1.0));
		dc.draw_rect({_scrubber_bounds.left, _scrubber_bounds.top, x, _scrubber_bounds.bottom},
		             ui::color(ui::style::color::dialog_selected_background, dc.colors.alpha));
	}

	const recti clock{_scrubber_bounds.right + dc.padding2, _transport_bounds.top, _transport_bounds.right,
	                  _transport_bounds.bottom};
	dc.draw_text(std::format("{} / {}", format_clock(_movie_state.playhead), format_clock(total)), clock,
	             ui::style::font_face::dialog, ui::style::text_style::single_line_center, clr, {});

	if (_timeline) _timeline->render(dc, {});
}

view_controller_ptr movie_view::controller_from_location(const view_host_ptr& host, const pointi loc)
{
	if (_scrubber_bounds.contains(loc) && duration() > 0)
	{
		return std::make_shared<scrubber_controller>(host, _scrubber_bounds, this);
	}

	if (_timeline && _timeline->bounds.contains(loc))
	{
		const auto index = _timeline->hit(loc, 1.0f, 6);
		return std::make_shared<timeline_controller>(host, _timeline->bounds, this, _timeline, index);
	}

	return nullptr;
}

bool movie_view::key_down(const char32_t key, const ui::key_state keys)
{
	// The key constants are runtime values, so this cannot be a switch.
	if (key == ' ')
	{
		toggle_play();
		return true;
	}

	if (key == keys::DEL)
	{
		remove_current();
		return true;
	}

	if (key == keys::LEFT)
	{
		step_clip(false);
		return true;
	}

	if (key == keys::RIGHT)
	{
		step_clip(true);
		return true;
	}

	if (key == 'Z' && keys.control)
	{
		undo();
		return true;
	}

	return false;
}

bool movie_view::escape()
{
	if (!_movie_state.is_playing) return false;
	_movie_state.is_playing = false;
	_state.invalidate_view(view_invalid::view_redraw);
	return true;
}

bool movie_view::can_exit()
{
	return true;
}

void movie_view::exit()
{
	_state.view_mode(view_type::items);
}

void movie_view::tick()
{
	if (!_movie_state.is_playing) return;

	const auto now = platform::tick_count();
	const auto elapsed = _last_tick == 0 ? 0.0 : (now - _last_tick) / 1000.0;
	_last_tick = now;

	const auto total = duration();
	_movie_state.playhead += elapsed;

	if (_movie_state.playhead >= total)
	{
		_movie_state.playhead = total;
		_movie_state.is_playing = false;
	}

	_state.invalidate_view(view_invalid::view_redraw);
}

void movie_view::toggle_play()
{
	if (_movie_state.project.is_empty()) return;

	_movie_state.is_playing = !_movie_state.is_playing;
	_last_tick = platform::tick_count();

	if (_movie_state.is_playing && _movie_state.playhead >= duration()) _movie_state.playhead = 0;

	_state.invalidate_view(view_invalid::view_redraw | view_invalid::command_state);
}

void movie_view::seek(const double time)
{
	_movie_state.playhead = std::clamp(time, 0.0, duration());
	_sources->supersede();
	_state.invalidate_view(view_invalid::view_redraw);
}

void movie_view::seek_preview_only(const double source_time)
{
	_preview_override = source_time;
	_state.invalidate_view(view_invalid::view_redraw);
}

void movie_view::step_clip(const bool forward)
{
	const auto& project = _movie_state.project;
	if (project.is_empty()) return;

	const auto timing = project.timing();
	const auto current = calc_movie_frame(project.clips(), project.settings(), _movie_state.playhead).a.index;
	const auto next = std::clamp(current + (forward ? 1 : -1), 0, static_cast<int>(project.size()) - 1);

	set_current(static_cast<size_t>(next));
	seek(timing.starts[next]);
}

void movie_view::set_current(const size_t index)
{
	_movie_state.project.current(index);
	_movie_state.read_from_project();
	changed(false);
}

void movie_view::changed(const bool relayout)
{
	if (_controls)
	{
		_controls->update_for_document();
		_controls->populate();
	}

	_sources->supersede();
	_movie_state.playhead = std::clamp(_movie_state.playhead, 0.0, duration());

	_state.invalidate_view(view_invalid::view_redraw | view_invalid::command_state |
		(relayout ? view_invalid::view_layout : view_invalid::none));
}

void movie_view::settings_changed()
{
	_movie_state.project.settings(_movie_state.to_settings());
	changed();
}

void movie_view::hold_changed()
{
	auto& project = _movie_state.project;
	const auto* const clip = project.current_clip();
	if (!clip || !clip->is_photo) return;

	if (_movie_state.clip_hold_is_default)
	{
		auto updated = *clip;
		updated.photo_duration_is_default = true;
		project.replace(project.current(), updated);
		project.apply_photo_duration();
	}
	else
	{
		project.trim(project.current(), 0, _movie_state.clip_hold_tenths / 10.0);
	}

	changed();
}

void movie_view::trim_current(const double start, const double end)
{
	_preview_override.reset();
	_movie_state.project.trim(_movie_state.project.current(), start, end);
	changed();
}

void movie_view::add_items(const df::item_set& items)
{
	std::vector<df::file_path> paths;

	for (const auto& i : items.items())
	{
		const auto* const mt = i->file_type();
		if (!mt || (mt->group != file_group::photo && mt->group != file_group::video)) continue;
		paths.emplace_back(i->path());
	}

	add_paths(paths);
}

void movie_view::add_paths(const std::vector<df::file_path>& paths)
{
	if (paths.empty()) return;

	for (const auto& path : paths)
	{
		_movie_state.project.append(make_movie_clip(path, _movie_state.project.settings()));
	}

	probe_clips();
	_movie_state.read_from_project();
	changed();
}

void movie_view::remove_current()
{
	auto& project = _movie_state.project;
	if (project.is_empty()) return;

	project.remove(project.current());
	_movie_state.read_from_project();
	changed();
}

void movie_view::move_clip(const size_t from, const size_t to)
{
	_movie_state.project.move(from, to);
	changed();
}

void movie_view::undo()
{
	if (!_movie_state.project.can_undo()) return;

	_movie_state.project.undo();
	_movie_state.read_from_project();
	changed();
}

// Probing is I/O, so it runs on a worker and comes back as detached values matched to clips by
// path. A clip whose source has moved keeps its stored times and stays marked missing.
void movie_view::probe_clips()
{
	std::vector<df::file_path> wanted;

	for (const auto& clip : _movie_state.project.clips())
	{
		if (!clip.is_missing) continue;
		if (std::find(wanted.begin(), wanted.end(), clip.path) == wanted.end()) wanted.emplace_back(clip.path);
	}

	if (wanted.empty()) return;

	const auto weak = weak_from_this();

	_state.queue_async(async_queue::load, [weak, wanted, &s = _state]
	{
		std::vector<movie_probe_result> results;
		results.reserve(wanted.size());

		for (const auto& path : wanted)
		{
			movie_probe_result probe;
			probe.path = path;

			const auto* const mt = files::file_type_from_name(path);
			probe.is_photo = mt && mt->group == file_group::photo;

			if (probe.is_photo)
			{
				const auto loaded = files{}.load(path, true);
				probe.found = loaded.success;
				probe.extent = loaded.dimensions();
			}
			else
			{
				av_format_decoder decoder;

				if (decoder.open(path, media_intent::metadata))
				{
					const auto info = decoder.info();
					probe.found = info.has_video;
					probe.extent = info.display_dimensions;
					probe.duration = std::max(0.0, info.end - info.start);
					probe.frame_rate = info.video_frame_rate;
				}
			}

			results.emplace_back(probe);
		}

		s.queue_ui([weak, results = std::move(results)]
		{
			if (const auto self = weak.lock()) self->apply_probe(results);
		});
	});
}

void movie_view::apply_probe(const std::vector<movie_probe_result>& results)
{
	auto& project = _movie_state.project;
	auto changed_any = false;

	for (size_t i = 0; i < project.size(); ++i)
	{
		const auto& clip = project.clips()[i];
		if (!clip.is_missing) continue;

		const auto found = std::find_if(results.begin(), results.end(),
		                                [&](const movie_probe_result& r) { return r.path == clip.path; });

		if (found == results.end() || !found->found) continue;

		auto updated = clip;
		updated.is_missing = false;
		updated.is_photo = found->is_photo;
		updated.extent = found->extent;
		updated.frame_rate = found->frame_rate;

		if (found->is_photo)
		{
			updated.start = 0;
			if (updated.duration() <= 0) updated.end = project.settings().photo_seconds;
		}
		else
		{
			updated.source_duration = found->duration;
			// A project read from disk already carries a trim; only a freshly added clip takes the
			// whole source.
			if (updated.duration() <= 0) updated.end = found->duration;
			updated.end = std::min(updated.end, found->duration);
			updated.start = std::clamp(updated.start, 0.0, updated.end);
		}

		project.replace_quietly(i, updated);
		changed_any = true;
	}

	if (changed_any)
	{
		_movie_state.read_from_project();
		changed();
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Project files
///////////////////////////////////////////////////////////////////////////////////////////////////

namespace
{
	std::vector<platform::file_dialog_filter> project_filters()
	{
		return {
			{std::string(tt.movie_project_files.sv()), "*.otio"},
			{std::string(tt.movie_maker_files.sv()), "*.wlmp"},
		};
	}

	std::string read_text_file(const df::file_path path)
	{
		const auto file = platform::open_file(path, platform::file_open_mode::read);
		if (!file) return {};

		const auto size = file->size();

		// A project file is a document, not a stream. A refusal here is better than reading a
		// gigabyte of something that was never a project.
		constexpr uint64_t max_project_bytes = 32 * 1024 * 1024;
		if (size == 0 || size > max_project_bytes) return {};

		std::string result(static_cast<size_t>(size), '\0');
		const auto read = file->read(std::bit_cast<uint8_t*>(result.data()), size);
		result.resize(static_cast<size_t>(read));
		return result;
	}
}

void movie_view::load_project(const df::file_path path, const bool is_wlmp)
{
	const auto text = read_text_file(path);

	if (text.empty())
	{
		make_dlg(_host->owner())->show_message(icon_index::error, tt.movie_title,
		                                       str_format(tt.movie_open_failed_fmt.sv(), path.name().sv()));
		return;
	}

	const auto loaded = is_wlmp ? read_wlmp(text) : read_otio(text, path.folder());

	if (!loaded)
	{
		make_dlg(_host->owner())->show_message(icon_index::error, tt.movie_title,
		                                       str_format(tt.movie_open_failed_fmt.sv(), path.name().sv()));
		return;
	}

	// A Movie Maker project is imported, not opened: saving it must write an .otio beside it rather
	// than overwrite a file Diffractor cannot produce.
	_movie_state.project.reset(loaded.clips, loaded.settings, is_wlmp ? df::file_path{} : path);
	_movie_state.read_from_project();
	probe_clips();
	seek(0);
	changed();
}

void movie_view::open_project()
{
	std::vector<df::file_path> paths;
	if (!platform::prompt_for_open_paths(paths, project_filters(), false) || paths.empty()) return;

	load_project(paths.front(), str::icmp(paths.front().extension(), ".wlmp") == 0);
}

void movie_view::import_project()
{
	std::vector<df::file_path> paths;

	const std::vector<platform::file_dialog_filter> filters{
		{std::string(tt.movie_maker_files.sv()), "*.wlmp"}
	};

	if (!platform::prompt_for_open_paths(paths, filters, false) || paths.empty()) return;

	load_project(paths.front(), true);
}

void movie_view::save_project()
{
	if (_movie_state.project.is_empty()) return;

	auto path = _movie_state.project.path();

	if (path.is_empty())
	{
		path = df::folder_path(platform::known_path(platform::known_folder::video)).combine_file_ext(
			std::string(tt.movie_title.sv()), ".otio");
	}

	const std::vector<platform::file_dialog_filter> filters{
		{std::string(tt.movie_project_files.sv()), "*.otio"}
	};

	if (!platform::prompt_for_save_path(path, filters)) return;

	const auto json = write_otio(_movie_state.project.clips(), _movie_state.project.settings(), path.folder());

	if (df::blob_save_to_file({std::bit_cast<const uint8_t*>(json.data()), json.size()}, path))
	{
		_movie_state.project.mark_saved(path);
		changed(false);
	}
	else
	{
		make_dlg(_host->owner())->show_message(icon_index::error, tt.movie_title,
		                                       str_format(tt.movie_save_failed_fmt.sv(), path.name().sv()));
	}
}

void movie_view::add_files()
{
	std::vector<df::file_path> paths;

	const std::vector<platform::file_dialog_filter> filters{
		{std::string(tt.movie_media_files.sv()), "*.mp4;*.mov;*.avi;*.mkv;*.m4v;*.jpg;*.jpeg;*.png;*.heic;*.webp"}
	};

	if (!platform::prompt_for_open_paths(paths, filters, true)) return;

	add_paths(paths);
}
