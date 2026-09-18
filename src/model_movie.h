// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: The Movie document. An ordered list of trimmed clips, the settings applied across them,
// the derived output geometry, and the project file read and written for both. docs/movie.md owns
// the behaviour; nothing here decodes, encodes, draws or touches the UI.

#pragma once

enum class movie_transition
{
	cut,
	crossfade,
};

// One entry in the timeline. A photo has no source duration of its own, so `end` is the length it
// is held for and `start` is always zero; a video is trimmed to [start, end) of its source.
struct movie_clip
{
	df::file_path path;
	bool is_photo = false;
	// True until the source has been probed and found. The project readers do no I/O, so a freshly
	// read project starts with every clip missing and the view resolves them; a clip that stays
	// missing keeps its stored times, so the trim survives a relink.
	bool is_missing = true;
	// Whether the probe has answered for this clip at all. Not saved: it is the difference between
	// "we have not looked yet" and "we looked and it is gone", and only the second is a thing to
	// tell the user about or to offer to repair.
	bool is_probed = false;
	double source_duration = 0;
	double start = 0;
	double end = 0;
	bool photo_duration_is_default = true;
	sizei extent;
	double frame_rate = 0;

	double duration() const
	{
		return std::max(0.0, end - start);
	}

	// Probed, and not there. Distinct from not yet probed, which looks identical in the document and
	// means the opposite to the user.
	bool is_lost() const
	{
		return is_probed && is_missing;
	}

	bool is_trimmed() const
	{
		return !is_photo && (start > 0.0001 || (source_duration > 0 && end < source_duration - 0.0001));
	}
};

struct movie_settings
{
	movie_transition transition = movie_transition::crossfade;
	double transition_seconds = 1.0;
	bool fade_in = true;
	bool fade_out = true;
	double photo_seconds = 4.0;
};

// Everything the render is told, all of it derived. docs/movie.md#7-rendering states the rules; this
// is only their result, and the view displays it rather than offering it.
struct movie_output
{
	sizei extent;
	int frame_rate = 30;
	int video_bitrate = 0;
	int audio_bitrate = 192000;
	int sample_rate = 48000;
	int channels = 2;

	bool operator==(const movie_output&) const = default;
};

// Ceilings the derivation clamps to. A photo-only timeline must not produce an 8000-pixel movie
// from a 45-megapixel source, and no codec here accepts an odd dimension.
inline constexpr int movie_max_width = 3840;
inline constexpr int movie_max_height = 2160;
inline constexpr int movie_max_frame_rate = 60;
inline constexpr int movie_default_frame_rate = 30;

// Bits per pixel per frame. 1920x1080x30 lands at ~6.2 Mbit/s, which is what a consumer H.264
// encoder wants for camera footage.
inline constexpr double movie_bits_per_pixel = 0.10;
inline constexpr int movie_min_bitrate = 1000000;
inline constexpr int movie_max_bitrate = 60000000;

movie_output derive_movie_output(const std::vector<movie_clip>& clips);

// Seconds two adjacent clips overlap. Half of the shorter clip is the ceiling, so a transition
// longer than the clip it joins shortens rather than consuming it, and no clip can be eaten
// entirely by the transitions on both of its sides.
double movie_overlap(const movie_clip& left, const movie_clip& right, const movie_settings& settings);

// Start time of each clip on the finished movie's timeline, plus the total. Both answers come from
// one walk because the caller that draws the timeline needs both and they must not disagree.
struct movie_timing
{
	std::vector<double> starts;
	double duration = 0;
};

movie_timing calc_movie_timing(const std::vector<movie_clip>& clips, const movie_settings& settings);

// One clip's contribution to one frame.
struct movie_frame_source
{
	int index = -1;
	// Seconds into the source file, already offset by the clip's in point. A photo answers 0.
	double source_time = 0;
	double weight = 0;
};

// What the compositor draws at one instant. At most two clips contribute: a crossfade joins exactly
// two, and the half-clip ceiling in movie_overlap stops a third reaching across a short clip.
//
// This is the decision; drawing it is separate, so the preview and the render cannot disagree about
// what a frame contains and neither needs a window to be tested.
struct movie_frame
{
	movie_frame_source a;
	movie_frame_source b;
	// How far the movie's own fade to black has gone: 0 draws the clips untouched, 1 is black.
	double fade_to_black = 0;

	bool is_empty() const { return a.index < 0; }
};

movie_frame calc_movie_frame(const std::vector<movie_clip>& clips, const movie_settings& settings, double time);
movie_frame calc_movie_frame(const std::vector<movie_clip>& clips, const movie_settings& settings,
	const movie_timing& timing, double time);

// Builds a clip from a path. The duration and extent come from the caller because probing is I/O;
// a photo is given the settings' default hold.
movie_clip make_movie_clip(df::file_path path, const movie_settings& settings);

enum class movie_load_status
{
	ok,
	unreadable,
	empty,
};

struct movie_load_result
{
	movie_load_status status = movie_load_status::unreadable;
	std::vector<movie_clip> clips;
	movie_settings settings;
	// Named for the report, not for a dialog: the count of elements the file carried that Movie
	// has no answer for. docs/movie.md#6-project-files owns what becomes of them.
	int ignored_elements = 0;

	explicit operator bool() const { return status == movie_load_status::ok; }
};

// The native project format is OpenTimelineIO. Paths are written relative to the project folder
// when they share a root, so a project and its sources move together.
std::string write_otio(const std::vector<movie_clip>& clips, const movie_settings& settings,
                       df::folder_path project_folder);
movie_load_result read_otio(std::string_view json, df::folder_path project_folder);

// Windows Live Movie Maker. The format is undocumented, so the reader scrapes rather than
// validates: see the note on the implementation.
movie_load_result read_wlmp(std::string_view xml);

// The timeline as a document: ordered, editable, and undoable. UI-thread-owned; the render takes a
// detached copy of `clips()` and `settings()` and never reads this object again.
class movie_project
{
public:
	// Deep enough that an accidental multi-select delete is recoverable, bounded because each entry
	// is a copy of the whole clip list.
	static constexpr size_t max_undo = 64;

	const std::vector<movie_clip>& clips() const { return _clips; }
	const movie_settings& settings() const { return _settings; }
	df::file_path path() const { return _path; }
	bool is_modified() const { return _modified; }
	uint64_t revision() const { return _revision; }
	size_t size() const { return _clips.size(); }
	bool is_empty() const { return _clips.empty(); }
	bool is_ready_to_render() const;

	// Index of the clip the controls panel describes and the strip marks. Always in range while the
	// timeline is not empty, so the panel never has to test it. Focus is one clip; the selection is
	// a set, and the two are different facts.
	size_t current() const { return _current; }
	void current(size_t i);
	const movie_clip* current_clip() const;

	// Sorted, and empty only when the timeline is. A command that acts on "the clips" acts on this.
	const std::vector<size_t>& selected() const { return _selected; }
	bool is_selected(size_t i) const;
	// One click's worth of selection: plain replaces, `extend` runs from the anchor, `toggle` adds
	// or removes one. Focus always lands on `index` whichever it was.
	void select(size_t index, bool extend, bool toggle);
	// Moves focus without changing the selected set. Used while a press inside an existing
	// multi-selection may become a block drag.
	void focus(size_t index);
	void select_all();

	void append(const movie_clip& clip);
	void insert(size_t at, const movie_clip& clip);
	void remove(size_t at);
	void move(size_t from, size_t to);
	// Moves every selected clip to the drop point, keeping their order relative to each other and
	// carrying focus with them. `to` counts positions in the list as it stands before the move.
	void move_selection(size_t to);
	void remove_selection();
	void replace(size_t at, const movie_clip& clip);
	// Replaces several clips as one document edit and therefore one undo step.
	void replace_many(const std::vector<std::pair<size_t, movie_clip>>& replacements);
	// For a change the user did not make and cannot want to undo: the probe filling in a clip's
	// duration and extent. An undo entry here would make Ctrl+Z appear to do nothing.
	void replace_quietly(size_t at, const movie_clip& clip);

	void settings(const movie_settings& s);
	// Retrims every photo still holding the default. A photo the user set explicitly keeps its own
	// length, which is what makes the movie setting a default rather than an override.
	void apply_photo_duration();

	void trim(size_t at, double start, double end);

	void reset(std::vector<movie_clip> clips, const movie_settings& s, df::file_path path);
	void mark_saved(df::file_path path, uint64_t revision);

	// The selection a timeline was built from, and empty when it came from a project file or has
	// been edited since. Re-entering Movie with a different selection replaces a timeline that is
	// still exactly what that selection produced, and leaves an edited one alone: the first is a
	// view of a selection, the second is a document.
	const std::vector<df::file_path>& seeded_from() const { return _seed; }
	void mark_seeded(std::vector<df::file_path> seed);

	bool can_undo() const { return !_undo.empty(); }
	void undo();

	movie_timing timing() const { return calc_movie_timing(_clips, _settings); }
	movie_output output() const { return derive_movie_output(_clips); }

private:
	struct undo_entry
	{
		std::vector<movie_clip> clips;
		movie_settings settings;
		std::vector<size_t> selected;
		size_t current = 0;
		size_t anchor = 0;
	};

	void push_undo();
	void select_only(size_t index);

	std::vector<movie_clip> _clips;
	movie_settings _settings;
	std::vector<undo_entry> _undo;
	df::file_path _path;
	std::vector<df::file_path> _seed;
	std::vector<size_t> _selected;
	size_t _current = 0;
	size_t _anchor = 0;
	bool _modified = false;
	uint64_t _revision = 0;
};
