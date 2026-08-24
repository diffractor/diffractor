// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: The Movie task view. Assembles photos and videos into one video: the timeline strip,
// the composed preview and its transport, and the settings panel. docs/movie.md owns the
// behaviour; model_movie.h owns the document and every rule about it.

#pragma once

#include "model.h"
#include "model_movie.h"
#include "ui_view.h"
#include "ui_dialog.h"
#include "view_list.h"

class movie_view;
class movie_view_controls;
class movie_timeline_element;
class movie_trim_control;
class movie_source_cache;

// What probing one source found. A detached value, produced on a worker and applied on the UI
// thread by matching on path, so nothing the view owns crosses a queue.
struct movie_probe_result
{
	df::file_path path;
	bool found = false;
	bool is_photo = false;
	sizei extent;
	double duration = 0;
	double frame_rate = 0;
};

class movie_view_controls final : public view_controls_host
{
public:
	movie_view_state& _movie_state;
	std::shared_ptr<movie_view> _view;

	std::shared_ptr<text_element> _info;
	std::shared_ptr<ui::title_control> _movie_title;
	std::shared_ptr<ui::check_control> _crossfade_check;
	std::shared_ptr<ui::check_control> _cut_check;
	std::shared_ptr<ui::slider_control> _transition_slider;
	std::shared_ptr<ui::check_control> _fade_in_check;
	std::shared_ptr<ui::check_control> _fade_out_check;
	std::shared_ptr<ui::slider_control> _photo_slider;
	std::shared_ptr<text_element> _output_text;

	std::shared_ptr<divider_element> _clip_divider;
	std::shared_ptr<ui::title_control> _clip_title;
	std::shared_ptr<text_element> _clip_text;
	std::shared_ptr<movie_trim_control> _trim;
	std::shared_ptr<ui::slider_control> _clip_hold_slider;
	std::shared_ptr<ui::check_control> _clip_hold_default_check;

	movie_view_controls(view_state& s, movie_view_state& ms) : view_controls_host(s), _movie_state(ms)
	{
		_scroller._scroll_child_controls = true;
	}

	void create_controls();
	void layout_controls(ui::measure_context& mc) override;
	void options_changed() override;

	// Refreshes the text and the visibility that depend on the document rather than on a control.
	void update_for_document();
};

class movie_view final : public view_base, public std::enable_shared_from_this<movie_view>
{
	view_state& _state;
	view_host_ptr _host;
	movie_view_state& _movie_state;

	std::shared_ptr<movie_view_controls> _controls;
	std::shared_ptr<movie_timeline_element> _timeline;
	std::shared_ptr<movie_source_cache> _sources;

	sizei _extent;
	recti _preview_bounds;
	recti _transport_bounds;
	recti _scrubber_bounds;
	recti _play_bounds;

	// Shed on device loss from the const broadcast; render rebuilds whenever they are absent, so
	// clearing them is the whole recovery.
	mutable ui::texture_ptr _texture_a;
	mutable ui::texture_ptr _texture_b;
	mutable ui::const_surface_ptr _drawn_a;
	mutable ui::const_surface_ptr _drawn_b;

	std::string _title;
	int64_t _last_tick = 0;
	// Set while a trim handle is captured: the preview shows that handle's frame instead of the
	// playhead, and returns to the playhead when the handle is released.
	std::optional<double> _preview_override;

public:
	movie_view(view_state& s, view_host_ptr host, movie_view_state& ms);
	~movie_view() override;

	view_controls_host_ptr controls(const ui::control_frame_ptr& owner);

	void activate(sizei extent) override;
	void deactivate() override;
	void refresh() override;
	void layout(ui::measure_context& mc, sizei extent) override;
	void render(ui::draw_context& dc, view_controller_ptr controller) override;
	view_controller_ptr controller_from_location(const view_host_ptr& host, pointi loc) override;
	bool key_down(char32_t key, ui::key_state keys) override;
	bool escape() override;
	bool can_exit() override;
	void exit() override;
	void tick();

	// Document edits. Each one re-reads what the panel shows, so the controls cannot describe a
	// clip that is no longer current.
	void add_items(const df::item_set& items);
	void add_paths(const std::vector<df::file_path>& paths);
	void add_files();
	void remove_current();
	void move_clip(size_t from, size_t to);
	void set_current(size_t index);
	void undo();
	void settings_changed();
	void hold_changed();
	void trim_current(double start, double end);
	void apply_probe(const std::vector<movie_probe_result>& results);

	void open_project();
	void save_project();
	void import_project();

	bool has_clips() const { return !_movie_state.project.is_empty(); }
	bool can_undo() const { return _movie_state.project.can_undo(); }
	bool can_render() const;

	void toggle_play();
	void seek(double time);
	void seek_preview_only(double source_time);
	void step_clip(bool forward);
	double duration() const;

	const movie_project& project() const { return _movie_state.project; }
	movie_view_state& movie_state() const { return _movie_state; }
	const std::shared_ptr<movie_source_cache>& sources() const { return _sources; }

	void changed(bool relayout = true);

	void broadcast_event(const view_element_event& event) const override;

	std::string_view title() override;

	void options_changed() const
	{
		if (_controls) _controls->options_changed();
	}

private:
	void probe_clips();
	void load_project(df::file_path path, bool is_wlmp);
	recti calc_preview_target(sizei source) const;
};
