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

#include "model_display.h"
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
	bool changed_during_probe = false;
	bool is_photo = false;
	sizei extent;
	double duration = 0;
	double frame_rate = 0;
};

struct movie_probe_retry_updates
{
	std::vector<df::file_path> retry_paths;
	std::vector<df::file_path> missing_paths;
};

// Why a render stopped, as a reason rather than a sentence. The worker cannot word it: the text a
// user reads is translated, and translation belongs to the UI thread that owns app_text.
enum class movie_render_error
{
	none,
	no_length,
	no_encoder,
	no_memory,
	undecodable,
	encoder_failed,
	output_is_source,
};

// A render's cancel flag and its one-shot publication claim, shared by the view and its worker.
// The claim is what makes Cancel and the final replace decide each other: whichever reaches it
// first wins, so a cancel arriving while the encoder closes cannot leave a rendered file in place,
// and a cancel arriving after it cannot claim one was discarded. The flag is a separate word
// because df::cancel_token's version constructor increments the value it is handed, which a claim
// read as "still running at zero" cannot afford.
struct movie_render_control
{
	std::atomic_bool cancelled = false;
	// 0 while the render runs, 1 once cancelled, 2 once the output is claimed for publication.
	std::atomic_int claim = 0;

	void cancel()
	{
		cancelled.store(true, std::memory_order_relaxed);
		auto expected = 0;
		claim.compare_exchange_strong(expected, 1);
	}

	bool claim_publication()
	{
		auto expected = 0;
		return claim.compare_exchange_strong(expected, 2);
	}
};

struct movie_frame_cache_completion_decision
{
	bool accept = false;
	bool clear_in_flight = false;
	bool dispatch_next = false;
};

inline movie_frame_cache_completion_decision decide_movie_frame_cache_completion(const uint64_t current_generation,
                                                                                 const uint64_t result_generation,
                                                                                 const bool has_queued_request)
{
	if (current_generation != result_generation) return {};
	return {true, true, has_queued_request};
}

struct movie_peak_completion_decision
{
	bool accept = false;
	bool clear_pending_path = false;
};

inline movie_peak_completion_decision decide_movie_peak_completion(const uint64_t current_generation,
                                                                   const uint64_t result_generation)
{
	if (current_generation != result_generation) return {};
	return {true, true};
}

enum class movie_probe_changed_action
{
	retry,
	mark_missing,
};

inline movie_probe_changed_action decide_movie_changed_probe_action(const int retry_count_after_increment,
                                                                    const int max_retries)
{
	return retry_count_after_increment <= max_retries
		       ? movie_probe_changed_action::retry
		       : movie_probe_changed_action::mark_missing;
}

inline bool movie_path_list_contains(const std::vector<df::file_path>& paths, const df::file_path& path)
{
	return std::find(paths.begin(), paths.end(), path) != paths.end();
}

inline movie_probe_retry_updates decide_movie_changed_probe_retries(const std::vector<movie_probe_result>& results,
                                                                    df::hash_map<df::file_path, int, df::ihash,
	                                                                    df::ieq>& retries,
                                                                    const int max_retries)
{
	movie_probe_retry_updates updates;

	for (const auto& result : results)
	{
		if (!result.changed_during_probe || movie_path_list_contains(updates.retry_paths, result.path) ||
			movie_path_list_contains(updates.missing_paths, result.path))
		{
			continue;
		}

		const auto count = ++retries[result.path];

		if (decide_movie_changed_probe_action(count, max_retries) == movie_probe_changed_action::retry)
			updates.retry_paths.emplace_back(result.path);
		else
			updates.missing_paths.emplace_back(result.path);
	}

	return updates;
}

inline bool should_clear_movie_probe_retries_after_document_replace(const bool document_replaced)
{
	return document_replaced;
}

inline bool should_erase_movie_probe_retry_after_missing(const bool marked_missing_after_budget)
{
	return marked_missing_after_budget;
}

inline bool should_retire_movie_clip_session_for_stop(const movie_view_state::playing_t playing)
{
	return playing != movie_view_state::playing_t::nothing;
}

inline bool should_retire_movie_clip_session_for_document_focus_change(const bool document_operation_can_change_focus)
{
	return document_operation_can_change_focus;
}

inline bool should_rewind_movie_clip_after_document_focus_change(const bool document_operation_can_change_focus)
{
	return document_operation_can_change_focus;
}

inline double rewound_movie_clip_playhead(const movie_clip* const clip)
{
	return clip ? clip->start : 0.0;
}

inline bool should_retire_movie_sources_after_project_change(const bool project_changed_successfully)
{
	return project_changed_successfully;
}

inline bool should_clear_movie_preview_audio_after_source_retire(const bool project_changed_successfully)
{
	return project_changed_successfully;
}

inline int next_movie_preview_audio_source_generation(const int current_generation)
{
	return current_generation + 1;
}

inline bool should_accept_movie_preview_pcm_loaded(const int current_generation, const int request_generation)
{
	return current_generation == request_generation;
}

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

	// The controls bind to movie_view_state, but a native control shows what it was last given, so
	// a value the document changed -- an undo, an opened project, another clip in focus -- has to be
	// shown again. Neither raises a change: an echo of the view's own write is not an edit.
	void show_document_values() const;
	void show_clip_values() const;
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
	recti _prev_bounds;
	recti _next_bounds;

	// Shed on device loss from the const broadcast; render rebuilds whenever they are absent, so
	// clearing them is the whole recovery.
	mutable ui::texture_ptr _texture_a;
	mutable ui::texture_ptr _texture_b;
	mutable ui::const_surface_ptr _drawn_a;
	mutable ui::const_surface_ptr _drawn_b;

	// The preview's picture sources: one per clip a crossfade can mix. Each is a video-only session
	// the player decodes for, because a preview asked for the next frame every fortieth of a second
	// needs a decoder that is already reading ahead rather than one that seeks per request. A photo
	// has no stream to read ahead, so it still comes from the frame cache.
	struct preview_source
	{
		std::shared_ptr<av_session> session;
		df::file_path path;
		// The clip this slot is serving. A session that arrives after the slot has moved on is
		// closed rather than adopted: it is alive, but it is no longer current.
		int index = -1;
		bool opening = false;
		// Where the session was last told to be, so a position it is already walking towards does
		// not become a seek on every frame.
		double sought = -1;
		mutable ui::texture_ptr texture;
	};

	std::array<preview_source, av_max_frame_sessions> _preview_sources;

	// The preview's own sound. The player owns one audio device and plays one session on it, and
	// the clip player already borrows that session -- so a crossfade, which needs two sources
	// audible at once, is mixed here from bounded source-time windows and written to a device this
	// view owns. The clip is heard through the player's resampler and device; the movie uses this
	// mixer, and the two never play at once.
	//
	// One decoded window of a clip's sound, at the endpoint's rate. Held by source window rather
	// than by contributor, so the clip a crossfade hands from b to a keeps the sound already read
	// for it, and a window read ahead of the playhead is there when the playhead arrives.
	struct preview_audio
	{
		df::file_path path;
		double start = 0;
		double end = 0;
		int sample_rate = 0;
		// Null while the window is being read; it is asked for once, not on every pump.
		std::shared_ptr<const std::vector<int16_t>> pcm;
		// When it was last wanted. The least recently wanted window is the one a new one replaces.
		uint64_t wanted = 0;
		int source_generation = 0;
	};

	// Two contributors now, and the two the playhead reaches at each of two points ahead of it.
	static constexpr size_t preview_audio_windows = 6;

	std::array<preview_audio, preview_audio_windows> _preview_audio;
	uint64_t _preview_audio_wanted = 0;
	av_audio_device_ptr _preview_device;
	audio_buffer _preview_audio_buffer;
	// Movie time up to which samples have been queued to the device, and the epoch that tells a
	// buffer built before a seek from one built after it.
	double _preview_audio_time = 0;
	int _preview_audio_generation = 0;
	// The endpoint's rate. Clip buffers are decoded at it, so the mixer never resamples: only the
	// sample format and the channel count are left to translate.
	int _preview_audio_rate = 0;
	int _preview_audio_source_generation = 0;
	// Set when the sound restarts and cleared once the device has been started on enough of it.
	// Until then the playhead waits, so the picture does not run ahead of a soundtrack that has not
	// begun -- for a moment at most: past that the movie plays on and the sound joins it.
	bool _preview_audio_priming = false;
	int64_t _preview_audio_priming_since = 0;

	std::string _title;
	int64_t _last_tick = 0;

	// The clip player's session. Borrowed: it takes the application player's single session slot,
	// which it can only do because it never runs at the same time as the movie preview. docs/movie.md
	// owns that exclusivity; without it two things would be fighting over one audio device.
	std::shared_ptr<av_session> _clip_session;
	df::file_path _clip_session_path;
	// Shed on device loss from the const broadcast, as the preview's textures are.
	mutable ui::texture_ptr _clip_texture;
	// Set while a trim handle is captured: the preview shows that handle's frame instead of the
	// playhead, and returns to the playhead when the handle is released.
	std::optional<double> _preview_override;

	// Set when the user picks a clip, cleared as soon as they say where to look instead. While it is
	// set the preview shows the focused clip's first kept frame rather than the movie instant,
	// because at a transition that instant is mostly the *outgoing* clip and at the movie's start it
	// is black -- neither of which shows the clip that was just clicked.
	bool _show_focus_frame = false;

	// The render. Its worker consumes an immutable snapshot of the document taken when Render was
	// pressed, so editing the timeline while it runs changes the next render, not the running one.
	// The generation is what tells a result from a render the user has since cancelled and replaced.
	progress_state _progress;
	std::shared_ptr<movie_render_control> _render_control;
	size_t _render_generation = 0;
	std::string _status;
	bool _project_io_active = false;
	size_t _project_io_generation = 0;
	size_t _probe_generation = 0;
	df::hash_map<df::file_path, int, df::ihash, df::ieq> _probe_retries;
	std::function<void(bool)> _project_save_complete;

public:
	movie_view(view_state& s, view_host_ptr host, movie_view_state& ms);
	~movie_view() override;

	view_controls_host_ptr controls(const ui::control_frame_ptr& owner);

	void activate(sizei extent) override;
	void deactivate() override;
	void refresh() override;
	void layout(ui::measure_context& mc, sizei extent) override;
	void render(ui::draw_context& dc, view_controller_ptr controller) override;
	view_controller_ptr controller_from_location(const view_host_ptr& host, pointi loc, hit_test_context& ctx) override;
	bool mouse_wheel(pointi loc, ui::wheel_notch notch) override;
	bool key_down(char32_t key, ui::key_state keys) override;
	bool escape() override;
	bool can_exit() override;
	bool confirm_exit() override;
	void exit() override;
	void cancel_operation() override;
	std::string_view operation_name() const override;
	std::string_view status() override;
	progress_state progress() const override;
	void tick();

	// Document edits. Each one re-reads what the panel shows, so the controls cannot describe a
	// clip that is no longer current.
	void add_items(const df::item_set& items);
	void add_paths(const std::vector<df::file_path>& paths);
	// Inserts at a point in the timeline rather than appending, for a drop between two tiles.
	void add_paths_at(const std::vector<df::file_path>& paths, size_t at);
	// A drop from outside. `loc` is in view coordinates: over the strip it inserts at the tile
	// boundary under the pointer, anywhere else it appends. Answers false when nothing dropped was a
	// photo or a video.
	bool drop_paths(const std::vector<df::file_path>& paths, pointi loc) override;
	void add_files();
	void remove_current();
	// One click's worth of strip selection. Focus always lands on `index`.
	void select_clip(size_t index, bool extend, bool toggle);
	// Moves focus without disturbing the selection, for a press inside a multi-selection that may
	// turn out to be the start of a drag.
	void set_focus(size_t index);
	void select_all();
	// Parks the preview on the focused clip's first kept frame and puts the playhead at its start, so
	// the scrubber and the strip agree with what is on screen.
	void show_focused_clip();
	void move_selection(size_t to);
	void send_selection_to_end();
	void undo();
	void settings_changed();
	void hold_changed();
	void trim_current(double start, double end);
	// Restores the whole source, which is the only way back from a trim that went wrong.
	void reset_trim();
	void apply_probe(const std::vector<movie_probe_result>& results);

	void open_project();
	void save_project();
	void import_project();
	// Re-points clips whose source has moved at a folder the user picks. The trim is the work and
	// the path is only where the work was aimed, so a relink keeps the first and replaces the second.
	void relink_missing();
	// Writes the movie. Asks for a destination, then runs on a worker over a snapshot of the document.
	void render_movie();
	void render_progress(size_t generation, int percent);
	void render_finished(size_t generation, bool ok, bool cancelled, movie_render_error reason,
	                     const std::string& message, str::cached name);

	bool has_clips() const { return !_movie_state.project.is_empty(); }
	// Clips the user has changed since the timeline was last saved, seeded or opened: what leaving,
	// opening another project, or quitting has to ask about.
	bool has_unsaved_changes() const;
	bool can_save_project() const;
	bool has_missing_clips() const;
	bool can_undo() const { return _movie_state.project.can_undo(); }
	bool can_render() const;
	bool project_io_active() const { return _project_io_active; }
	bool is_playing() const { return _movie_state.is_playing(); }
	// Where the clip player has reached, or nothing when it is not the one running. The trim control
	// asks so its frame and its marker follow the player instead of the handles.
	std::optional<double> clip_position() const
	{
		return _movie_state.playing == movie_view_state::playing_t::clip
			       ? std::optional{_movie_state.clip_playhead}
			       : std::nullopt;
	}

	// The clip player's current frame, uploaded on demand. Null unless it is the one running and it
	// has a picture: the trim control falls back to its own decoded frame then.
	ui::texture_ptr clip_texture(ui::draw_context& dc);

	void toggle_play();
	// Stops whichever player is running. Picking a clip on the strip does this: playing on under a
	// panel that now describes a different clip is the surprise the design forbids.
	void stop_playback();
	// The clip control is its own small player over the kept region, so a trim can be checked without
	// watching the rest of the movie. Only one of the two ever runs.
	void toggle_play_clip();
	void seek(double time);
	void seek_preview_only(double source_time);
	void end_preview_override();
	void step_clip(bool forward);
	double duration() const;
	menu_type context_menu(pointi loc) override;

	const movie_project& project() const { return _movie_state.project; }
	movie_view_state& movie_state() const { return _movie_state; }
	const std::shared_ptr<movie_source_cache>& sources() const { return _sources; }

	void changed(bool relayout = true);
	// The controls panel is a separate window with its own frame, so a view invalidation does not
	// reach it.
	void invalidate_controls() const;

	void broadcast_event(const view_element_event& event) const override;

	std::string_view title() override;

	void options_changed() const
	{
		if (_controls) _controls->options_changed();
	}

private:
	void probe_clips();
	// Which slot serves one contributor, or av_max_frame_sessions when none can. The mapping is by
	// clip rather than by the frame's a/b position, which is what stops a transition tearing down
	// the session it just filled.
	size_t preview_slot_for(const movie_frame_source& source, const std::vector<movie_clip>& clips, size_t avoid);
	// Opens the clip the playhead is about to reach in the free slot, so a crossfade begins with two
	// pictures rather than one over black.
	void preopen_preview_source(size_t used_slot);
	// Points one slot at one clip. Answers whether it has a session yet; opening is a request.
	bool ensure_preview_source(size_t slot, int clip_index, const movie_clip& clip, double source_time);
	// Ensures the slot, then keeps its session running and near the position the movie wants.
	void update_preview_source(size_t slot, int clip_index, const movie_clip& clip, double source_time);
	void preview_source_opened(size_t slot, int clip_index, df::file_path path, std::shared_ptr<av_session> ses);
	void close_preview_source(size_t slot);
	void close_preview_sources();
	// The preview's sound. Opened when the movie starts playing, fed a little ahead of the playhead
	// on every tick, and restarted whenever the playhead moves somewhere it was not going.
	void start_preview_audio();
	void stop_preview_audio();
	void reset_preview_audio();
	void pump_preview_audio();
	// Lets go of an endpoint that has gone away. The preview carries on silent until the next Play,
	// which opens whatever the default endpoint is by then.
	void drop_preview_device();
	// Asks for the windows the playhead will reach next -- the clip's next stretch and the clips
	// after it -- while there is still time to read them.
	void prefetch_preview_audio();
	// The window of `clip`'s sound holding `source_time`, or null while it is read. Asking is what
	// reads it, on a queue the preview's pictures do not use, so reading never delays a frame.
	std::shared_ptr<const std::vector<int16_t>> preview_pcm(const movie_clip& clip, double source_time,
	                                                        double& window_start);
	void preview_pcm_loaded(df::file_path path, double start, double end, int sample_rate, int source_generation,
	                        std::vector<int16_t> pcm);
	// Retires the clip player and puts its position back at the focused clip's in point.
	void rewind_clip_player();
	// Adopts a session the player opened, if the focus has not moved on since it was asked for.
	void open_clip_session(df::file_path path, double at);
	void clip_session_opened(df::file_path path, std::shared_ptr<av_session> ses);
	void close_clip_session();
	void retire_source_state();
	// Asks before a running render is abandoned. Answers whether the caller may go on.
	bool confirm_render_cancel();
	bool confirm_save_or_discard(std::function<void()> after_save = {});
	void save_project(std::function<void(bool)> complete);
	// Replaces the timeline with the clips a selection produces. Only ever reached for a timeline
	// holding no unsaved work; leaving Movie is where unsaved work is settled.
	void seed_from(const std::vector<df::file_path>& paths);
	// What Discard on leaving does: the timeline goes, the movie settings stay.
	void discard_timeline();
	// Re-reads the bound panel values from the document and shows them, for a change the panel did
	// not make itself. The narrower form leaves the movie settings alone, so text being typed into
	// one of them is not rewritten under the user.
	void read_document();
	void read_focused_clip();
	void load_project(df::file_path path, bool is_wlmp);
	recti calc_preview_target(sizei source) const;
};
