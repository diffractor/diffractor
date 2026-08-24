// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// 
// This program is free software; you can redistribute it and / or modify it
// under the terms of the LGPL License either version 2.1 or later.
// License details are available at https://www.gnu.org/licenses/lgpl-2.1.html
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY

// Purpose: Tests for the Movie document (model_movie) -- the derived output geometry, transition
// and duration arithmetic, timeline edits and undo, the OpenTimelineIO project file, and the
// Windows Live Movie Maker reader. docs/movie.md owns the behaviour.

#include "pch.h"
#include "test.h"
#include "model.h"
#include "model_movie.h"
#include "test_runner.h"

static df::folder_path movie_test_folder()
{
	return df::folder_path(df::windows_path_semantics ? "c:\\movie" : "/movie");
}

static df::file_path movie_test_path(const std::string_view name)
{
	return movie_test_folder().combine_file(name);
}

static movie_clip make_video(const std::string_view name, const double duration, const sizei extent,
                             const double rate)
{
	movie_clip c;
	c.path = movie_test_path(name);
	c.is_photo = false;
	c.source_duration = duration;
	c.start = 0;
	c.end = duration;
	c.extent = extent;
	c.frame_rate = rate;
	return c;
}

static movie_clip make_photo(const std::string_view name, const double hold, const sizei extent)
{
	movie_clip c;
	c.path = movie_test_path(name);
	c.is_photo = true;
	c.end = hold;
	c.extent = extent;
	return c;
}

//
// Output geometry
//

static void should_derive_movie_output()
{
	assert_equal(1920, derive_movie_output({}).extent.cx, "an empty timeline falls back to 1080p");
	assert_equal(movie_default_frame_rate, derive_movie_output({}).frame_rate, "and to the default rate");

	const auto mixed = derive_movie_output({
		make_video("a.mp4", 10, {1280, 720}, 60),
		make_video("b.mp4", 10, {1920, 1080}, 30)
	});

	assert_equal(1920, mixed.extent.cx, "the largest video decides the width");
	assert_equal(1080, mixed.extent.cy, "the largest video decides the height");
	assert_equal(60, mixed.frame_rate, "the highest source rate decides the rate");
	assert_equal(12441600, mixed.video_bitrate, "bitrate follows frame size and rate");

	// The rule that matters most: a 48-megapixel photo beside a 720p clip must not produce an
	// 8000-pixel movie. Removing the have_video test makes this line fail with 8000.
	const auto with_photo = derive_movie_output({
		make_photo("big.jpg", 4, {8000, 6000}),
		make_video("small.mp4", 10, {1280, 720}, 25)
	});

	assert_equal(1280, with_photo.extent.cx, "a photo never drives the frame size when a video is present");
	assert_equal(25, with_photo.frame_rate, "the only video decides the rate");

	const auto photos_only = derive_movie_output({make_photo("big.jpg", 4, {8000, 6000})});

	assert_equal(2880, photos_only.extent.cx, "a photo-only timeline is clamped inside 4K");
	assert_equal(2160, photos_only.extent.cy, "and keeps its aspect ratio while clamped");
	assert_equal(movie_default_frame_rate, photos_only.frame_rate, "with no video there is no source rate");

	const auto odd = derive_movie_output({make_video("odd.mp4", 10, {1921, 1081}, 30)});

	assert_equal(1920, odd.extent.cx, "an odd width is rounded down");
	assert_equal(1080, odd.extent.cy, "an odd height is rounded down");

	const auto fast = derive_movie_output({make_video("fast.mp4", 10, {1920, 1080}, 240)});
	assert_equal(movie_max_frame_rate, fast.frame_rate, "an extreme source rate is clamped");
}

//
// Timing
//

static void should_time_movie_transitions()
{
	const std::vector clips{
		make_video("a.mp4", 10, {1920, 1080}, 30),
		make_video("b.mp4", 10, {1920, 1080}, 30),
		make_video("c.mp4", 10, {1920, 1080}, 30)
	};

	movie_settings cut;
	cut.transition = movie_transition::cut;

	const auto cut_timing = calc_movie_timing(clips, cut);
	assert_equal(30.0, cut_timing.duration, "cuts do not shorten the movie");
	assert_equal(10.0, cut_timing.starts[1], "a cut starts the next clip where the last ended");
	assert_equal(20.0, cut_timing.starts[2], "and so on");

	movie_settings fade;
	fade.transition = movie_transition::crossfade;
	fade.transition_seconds = 1.0;

	const auto fade_timing = calc_movie_timing(clips, fade);
	assert_equal(28.0, fade_timing.duration, "each crossfade shortens the movie by its length");
	assert_equal(0.0, fade_timing.starts[0], "the first clip starts at zero");
	assert_equal(9.0, fade_timing.starts[1], "the second clip starts one transition early");
	assert_equal(18.0, fade_timing.starts[2], "and the third two");

	// A transition longer than the clip it joins must shorten rather than consume it. Dropping the
	// half-clip ceiling in movie_overlap makes this 18, and the middle clip disappears.
	const std::vector short_middle{
		make_video("a.mp4", 10, {1920, 1080}, 30),
		make_video("b.mp4", 2, {1920, 1080}, 30),
		make_video("c.mp4", 10, {1920, 1080}, 30)
	};

	movie_settings long_fade;
	long_fade.transition_seconds = 4.0;

	const auto clamped = calc_movie_timing(short_middle, long_fade);
	assert_equal(1.0, movie_overlap(short_middle[0], short_middle[1], long_fade),
	             "the overlap is capped at half the shorter clip");
	assert_equal(20.0, clamped.duration, "a two second clip survives a four second transition");
	assert_equal(10.0, clamped.starts[2], "and still separates its neighbours");

	assert_equal(0.0, calc_movie_timing({}, fade).duration, "an empty timeline has no duration");
	assert_equal(10.0, calc_movie_timing({clips[0]}, fade).duration, "one clip has no transition to apply");
}

static void should_compose_a_movie_frame()
{
	const std::vector clips{
		make_video("a.mp4", 10, {1920, 1080}, 30),
		make_video("b.mp4", 10, {1920, 1080}, 30)
	};

	movie_settings settings;
	settings.transition = movie_transition::crossfade;
	settings.transition_seconds = 2.0;
	settings.fade_in = false;
	settings.fade_out = false;

	// The movie is 18s: 0-8 is a alone, 8-10 is the crossfade, 10-18 is b alone.
	const auto early = calc_movie_frame(clips, settings, 4.0);
	assert_equal(0, early.a.index, "one clip carries the frame outside a transition");
	assert_equal(4.0, early.a.source_time, "at its own position in the source");
	assert_equal(1.0, early.a.weight, "at full weight");
	assert_equal(-1, early.b.index, "with nothing mixed into it");

	const auto middle = calc_movie_frame(clips, settings, 9.0);
	assert_equal(0, middle.a.index, "the outgoing clip is first");
	assert_equal(1, middle.b.index, "the incoming clip is second");
	assert_equal(0.5, middle.a.weight, "halfway through the transition they are equal");
	assert_equal(0.5, middle.b.weight, "on both sides");
	assert_equal(9.0, middle.a.source_time, "the outgoing clip is near its end");
	assert_equal(1.0, middle.b.source_time, "and the incoming one just past its start");

	const auto late = calc_movie_frame(clips, settings, 14.0);
	assert_equal(1, late.a.index, "after the transition only the incoming clip is left");
	assert_equal(6.0, late.a.source_time, "at its own position");
	assert_equal(-1, late.b.index, "with nothing mixed into it");

	// The final instant must still draw. An exclusive end test here leaves the last frame empty,
	// which is one black frame at the end of every movie.
	const auto last = calc_movie_frame(clips, settings, 18.0);
	assert_equal(1, last.a.index, "the final instant still draws a clip");
	assert_equal(true, calc_movie_frame({}, settings, 0).is_empty(), "an empty timeline draws nothing");

	// A trimmed clip reports positions in its source, not on the timeline.
	auto trimmed_clips = clips;
	trimmed_clips[0].start = 3;
	trimmed_clips[0].end = 9;
	assert_equal(5.0, calc_movie_frame(trimmed_clips, settings, 2.0).a.source_time,
	             "a trim offsets the position asked of the source");

	assert_equal(0.0, calc_movie_frame({make_photo("p.jpg", 5, {4000, 3000})}, settings, 3.0).a.source_time,
	             "a photo has one frame however long it is held");

	settings.fade_in = true;
	settings.fade_out = true;

	assert_equal(1.0, calc_movie_frame(clips, settings, 0.0).fade_to_black, "the movie opens black");
	assert_equal(0.5, calc_movie_frame(clips, settings, 1.0).fade_to_black, "and fades up");
	assert_equal(0.0, calc_movie_frame(clips, settings, 5.0).fade_to_black, "and is clear in the middle");
	assert_equal(0.5, calc_movie_frame(clips, settings, 17.0).fade_to_black, "then fades down");
	assert_equal(1.0, calc_movie_frame(clips, settings, 18.0).fade_to_black, "and closes black");
}

//
// The timeline as a document
//

static void should_edit_the_movie_timeline()
{
	movie_project project;

	assert_equal(true, project.is_empty(), "a new project holds nothing");
	assert_equal(false, project.can_undo(), "and has nothing to undo");
	assert_equal(false, project.is_modified(), "and is unmodified");

	project.append(make_video("a.mp4", 10, {1920, 1080}, 30));
	project.append(make_video("b.mp4", 10, {1920, 1080}, 30));
	project.append(make_photo("c.jpg", 4, {4000, 3000}));

	assert_equal(3, static_cast<int>(project.size()), "three clips were appended");
	assert_equal(2, static_cast<int>(project.current()), "the last append is current");
	assert_equal(true, project.is_modified(), "editing marks the project modified");

	// The same source twice is the case a set-based selector could not express, and the reason the
	// timeline is a document rather than a view of the selection.
	project.append(make_video("a.mp4", 10, {1920, 1080}, 30));
	assert_equal(4, static_cast<int>(project.size()), "the same source can appear twice");
	assert_equal("a.mp4", project.clips()[3].path.name().sv(), "and it is the same source");

	project.insert(0, make_video("first.mp4", 5, {1920, 1080}, 30));
	assert_equal("first.mp4", project.clips()[0].path.name().sv(), "insert places the clip at the index");
	assert_equal(0, static_cast<int>(project.current()), "and makes it current");

	project.move(0, 2);
	assert_equal("first.mp4", project.clips()[2].path.name().sv(), "move relocates the clip");
	assert_equal("a.mp4", project.clips()[0].path.name().sv(), "and closes the gap behind it");

	project.undo();
	assert_equal("first.mp4", project.clips()[0].path.name().sv(), "undo restores the order");

	project.current(1);
	project.remove(1);
	assert_equal(4, static_cast<int>(project.size()), "remove takes one clip out");
	assert_equal(1, static_cast<int>(project.current()), "and current stays in range");

	project.remove(3);
	assert_equal(1, static_cast<int>(project.current()), "removing after current leaves current alone");
	project.current(2);
	project.remove(0);
	assert_equal(1, static_cast<int>(project.current()), "removing before current shifts current back");

	project.remove(99);
	assert_equal(2, static_cast<int>(project.size()), "an out of range remove does nothing");

	while (project.can_undo()) project.undo();
	assert_equal(true, project.is_empty(), "undo unwinds every edit");
}

static void should_trim_movie_clips()
{
	movie_project project;
	project.append(make_video("a.mp4", 10, {1920, 1080}, 30));

	project.trim(0, 2, 8);
	assert_equal(2.0, project.clips()[0].start, "the in point is taken");
	assert_equal(8.0, project.clips()[0].end, "the out point is taken");
	assert_equal(6.0, project.clips()[0].duration(), "and the clip is that long");
	assert_equal(true, project.clips()[0].is_trimmed(), "a trimmed clip says so");

	project.trim(0, -5, 100);
	assert_equal(0.0, project.clips()[0].start, "a trim below zero is clamped");
	assert_equal(10.0, project.clips()[0].end, "a trim past the source is clamped");
	assert_equal(false, project.clips()[0].is_trimmed(), "and a full-length clip is untrimmed");

	movie_project photos;
	movie_settings settings;
	settings.photo_seconds = 4.0;
	photos.reset({}, settings, {});
	photos.append(make_photo("a.jpg", 4, {4000, 3000}));
	photos.append(make_photo("b.jpg", 4, {4000, 3000}));

	photos.trim(1, 0, 9);
	assert_equal(9.0, photos.clips()[1].end, "a photo takes the duration it is given");
	assert_equal(false, photos.clips()[1].photo_duration_is_default, "and stops following the default");

	settings.photo_seconds = 6.0;
	photos.settings(settings);

	assert_equal(6.0, photos.clips()[0].end, "a photo still on the default follows it");
	assert_equal(9.0, photos.clips()[1].end, "a photo the user set keeps its own length");
}

//
// The project file
//

static void should_round_trip_a_movie_project()
{
	std::vector<movie_clip> clips;

	auto video = make_video("a.mp4", 12, {1920, 1080}, 30);
	video.start = 1.5;
	video.end = 8.25;
	clips.emplace_back(video);

	auto photo = make_photo("b.jpg", 5, {4000, 3000});
	photo.photo_duration_is_default = false;
	clips.emplace_back(photo);

	movie_settings settings;
	settings.transition = movie_transition::crossfade;
	settings.transition_seconds = 1.5;
	settings.fade_in = false;
	settings.fade_out = true;
	settings.photo_seconds = 7.0;

	const auto json = write_otio(clips, settings, movie_test_folder());

	assert_equal(true, str::contains(json, "\"OTIO_SCHEMA\":\"Timeline.1\""), "the file declares its schema");
	assert_equal(true, str::contains(json, "\"target_url\":\"a.mp4\""),
	             "a source beside the project is stored relative to it");
	assert_equal(true, str::contains(json, "SMPTE_Dissolve"), "the crossfade is written where other tools read it");

	const auto read = read_otio(json, movie_test_folder());

	assert_equal(true, static_cast<bool>(read), "the project reads back");
	assert_equal(2, static_cast<int>(read.clips.size()), "with both clips");
	assert_equal(0, read.ignored_elements, "and nothing dropped");

	assert_equal(video.path.pack(), read.clips[0].path.pack(), "the relative path resolves back");
	assert_equal(1.5, read.clips[0].start, "the in point survives");
	assert_equal(8.25, read.clips[0].end, "the out point survives");
	assert_equal(12.0, read.clips[0].source_duration, "the source length survives");
	assert_equal(false, read.clips[0].is_photo, "a video is still a video");

	assert_equal(true, read.clips[1].is_photo, "a photo is still a photo");
	assert_equal(5.0, read.clips[1].end, "its hold survives");
	assert_equal(false, read.clips[1].photo_duration_is_default, "and so does its override");

	assert_equal(true, read.settings.transition == movie_transition::crossfade, "the transition survives");
	assert_equal(1.5, read.settings.transition_seconds, "its length survives");
	assert_equal(false, read.settings.fade_in, "fade in survives");
	assert_equal(true, read.settings.fade_out, "fade out survives");
	assert_equal(7.0, read.settings.photo_seconds, "the photo default survives");

	// An absolute path is written whole when the source is nowhere near the project, and must still
	// resolve when the project is read from a different folder. It is forward-slashed so the JSON
	// carries no escaped separators.
	const auto elsewhere = write_otio(clips, settings, {});
	assert_equal(true, str::contains(elsewhere, str::replace(video.path.pack(), "\\", "/")),
	             "a distant source is stored absolute");
	assert_equal(false, str::contains(elsewhere, "\\\\"), "and with no escaped separators");
	assert_equal(video.path.pack(), read_otio(elsewhere, {}).clips[0].path.pack(), "and resolves without a root");
}

static void should_read_a_movie_project_it_did_not_write()
{
	// One video track carrying a Gap, beside an audio track. Movie takes the clips it understands
	// and counts the rest; refusing the file over an audio track would make interchange useless.
	const auto path = movie_test_path("a.mp4").pack();
	auto url = path;
	for (auto&& c : url) if (c == '\\') c = '/';

	const auto json = std::format(R"({{
		"OTIO_SCHEMA": "Timeline.1",
		"tracks": {{
			"OTIO_SCHEMA": "Stack.1",
			"children": [
				{{
					"OTIO_SCHEMA": "Track.1", "kind": "Video",
					"children": [
						{{
							"OTIO_SCHEMA": "Clip.1", "name": "a.mp4",
							"source_range": {{
								"OTIO_SCHEMA": "TimeRange.1",
								"start_time": {{ "OTIO_SCHEMA": "RationalTime.1", "rate": 24.0, "value": 24.0 }},
								"duration": {{ "OTIO_SCHEMA": "RationalTime.1", "rate": 24.0, "value": 48.0 }}
							}},
							"media_reference": {{
								"OTIO_SCHEMA": "ExternalReference.1", "target_url": "{}"
							}}
						}},
						{{ "OTIO_SCHEMA": "Gap.1", "name": "gap" }},
						{{
							"OTIO_SCHEMA": "Transition.1", "transition_type": "SMPTE_Dissolve",
							"in_offset": {{ "OTIO_SCHEMA": "RationalTime.1", "rate": 24.0, "value": 12.0 }},
							"out_offset": {{ "OTIO_SCHEMA": "RationalTime.1", "rate": 24.0, "value": 12.0 }}
						}}
					]
				}},
				{{ "OTIO_SCHEMA": "Track.1", "kind": "Audio", "children": [] }}
			]
		}}
	}})", url);

	const auto read = read_otio(json, movie_test_folder());

	assert_equal(true, static_cast<bool>(read), "a foreign project reads");
	assert_equal(1, static_cast<int>(read.clips.size()), "with the clip it understands");
	assert_equal(1.0, read.clips[0].start, "at the frame rate the file used");
	assert_equal(3.0, read.clips[0].end, "and for the duration it stated");
	assert_equal(2, read.ignored_elements, "and the audio track and the gap counted, not silently lost");
	assert_equal(1.0, read.settings.transition_seconds, "a foreign transition supplies the crossfade length");

	assert_equal(false, static_cast<bool>(read_otio("not json at all", {})), "junk is refused");
	assert_equal(false, static_cast<bool>(read_otio("{}", {})), "an empty object is refused");
	assert_equal(true, read_otio("{}", {}).status == movie_load_status::empty, "and says it held nothing");
}

static void should_import_a_movie_maker_project()
{
	const auto video = movie_test_path("a.mp4").pack();
	const auto image = movie_test_path("b&c.jpg").pack();
	const auto music = movie_test_path("track.mp3").pack();

	// The ampersand is here to prove the attribute decoder: a raw & would leave the name as
	// "b&amp;c.jpg" and the clip would point at a file that cannot exist.
	const auto image_attribute = str::replace(image, "&", "&amp;");

	const auto xml = std::format(R"(<?xml version="1.0" encoding="utf-8"?>
<Project version="9.0">
	<MediaItems>
		<MediaItem id="1" filePath="{}" />
		<MediaItem id="2" filePath="{}" />
		<MediaItem id="3" filePath="{}" />
	</MediaItems>
	<Extents>
		<VideoClip extentID="10" mediaItemID="1" inTime="2" outTime="7.5" />
		<ImageExtent extentID="11" mediaItemID="2" duration="6" />
		<AudioClip extentID="12" mediaItemID="3" inTime="0" outTime="60" />
		<TitleClip extentID="13" mediaItemID="2" duration="3" />
		<VideoClip extentID="14" mediaItemID="99" inTime="0" outTime="5" />
	</Extents>
</Project>)", video, image_attribute, music);

	const auto read = read_wlmp(xml);

	assert_equal(true, static_cast<bool>(read), "the project imports");
	assert_equal(2, static_cast<int>(read.clips.size()), "with the video and the image only");

	assert_equal(video, read.clips[0].path.pack(), "the video path resolves");
	assert_equal(false, read.clips[0].is_photo, "a video clip is a video");
	assert_equal(2.0, read.clips[0].start, "its in point is taken");
	assert_equal(7.5, read.clips[0].end, "and its out point");

	assert_equal(image, read.clips[1].path.pack(), "the entity in the image path is decoded");
	assert_equal(true, read.clips[1].is_photo, "an image extent is a photo");
	assert_equal(6.0, read.clips[1].end, "and is held for its stated duration");
	assert_equal(false, read.clips[1].photo_duration_is_default, "an imported hold is not the default");

	// The music, the title, and a clip whose media item is not in the file.
	assert_equal(3, read.ignored_elements, "everything it cannot represent is counted");

	assert_equal(false, static_cast<bool>(read_wlmp("")), "an empty file is refused");
	assert_equal(false, static_cast<bool>(read_wlmp("<Project><Extents /></Project>")),
	             "a project with no media is refused");
}

void register_movie_tests(view_state& state, test_registry& tests)
{
	//
	// Output
	//
	tests.add("Should derive movie output geometry"s, should_derive_movie_output);

	//
	// Timing
	//
	tests.add("Should time movie transitions"s, should_time_movie_transitions);
	tests.add("Should compose a movie frame"s, should_compose_a_movie_frame);

	//
	// The timeline document
	//
	tests.add("Should edit the movie timeline"s, should_edit_the_movie_timeline);
	tests.add("Should trim movie clips"s, should_trim_movie_clips);

	//
	// The project file
	//
	tests.add("Should round trip a movie project"s, should_round_trip_a_movie_project);
	tests.add("Should read a movie project it did not write"s, should_read_a_movie_project_it_did_not_write);
	tests.add("Should import a movie maker project"s, should_import_a_movie_maker_project);
}
