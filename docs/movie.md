# Movie mixer

Movie mixer assembles an ordered timeline of photos and videos into one MP4 video. It provides clip
ordering, video trims, photo durations, one transition style, end fades, preview, project files and
rendering. It does not modify, move or delete source media.

## 1. Behaviour contract

Movie receives photos and videos from the complete visibly selected set in listing order. The
timeline then becomes an independent ordered document. Rendering writes one MP4 destination and Save
project writes one OTIO document; source media remains unchanged. Failed or cancelled writes preserve
an existing destination, and timeline edits are undoable during the session.

## 2. Entry and document lifecycle

**Movie mixer** is a task view. Its command is offered when the visible selection contains at least
one photo or video, or when an existing Movie timeline can be resumed.

Entering Movie with a selection of photos and videos that differs from the one its timeline was built
from, in its items or their listing order, builds a new timeline from it in listing order. Entering
with the same selection, or with none, returns to the timeline left behind, including edits that were
saved; a project opened in Movie answers to the selection the timeline it replaced was built from.

Leaving Movie settles unsaved edits. When the timeline has changed since it was built, opened or
saved, Close, Items and Escape offer Save project, Discard or Cancel. An edited timeline that has
had every clip removed is still unsaved work; a pristine empty timeline is not. Save writes a valid
project, including one with no clips, and leaves once the project is written; Discard throws the
timeline away and keeps the movie settings; Cancel stays with the draft and its undo history. A
cancelled or failed save never counts as permission to discard the timeline. A running render is
asked about first. Open, Import and quitting while Movie is open ask the same question before the
timeline is replaced or lost.

Only a change is an edit. Undoing back to the state last built, opened or saved leaves nothing to
ask about, and a handle pressed and released in place, or a setting chosen again, changes nothing.

Project Open, Save and Relink perform file access on workers. Save writes a sibling temporary file
and replaces the destination only after the complete project has been written. Edits made while a
save is running remain marked as unsaved.

## 3. View and timeline

The view contains the shared top bar, a fitted preview, movie transport and scrubber, a one-row
horizontal timeline, and a right-hand controls panel.

Timeline tiles have equal width and play left to right. A tile shows its thumbnail, retained duration,
trim state, missing-source state and playback position. The timeline scrolls horizontally without
growing vertically: by its scroll bar, by either wheel axis or a touchpad, and by holding a dragged
block near either end.

Selection, focus and playback are separate:

- Filled tiles are selected and receive timeline commands.
- One outlined tile is focused and supplies the clip controls.
- A bar at the tile foot shows the playhead.

Plain click selects and focuses one clip. Ctrl+click toggles membership without allowing an empty
selection. Shift+click selects from the anchor. Pressing an already selected tile moves focus without
collapsing the selected block, so dragging moves the complete selection. A block keeps its internal
order and focus, and a no-op drop creates no undo entry.

Files dropped on the timeline insert at the displayed boundary; files dropped elsewhere append.
Photos and videos are accepted and other files are ignored. Remove, reorder, send to end, trim, add and
relink are undoable. One Add, drop or Relink is one undo step regardless of how many files it carries.

## 4. Preview and clip controls

The preview and renderer use the same timeline timing and composition decision. Frames are fitted,
never stretched or cropped, and unused canvas area is black. A crossfade holds both contributing clips
and mixes them with the same weights used for audio.

The movie transport provides previous clip, play/pause, next clip, elapsed time, total time and a
whole-movie scrubber. Selecting a clip parks the preview on its first retained frame and stops movie
playback; while the movie plays, previous and next clip instead carry playback on from that clip's
start. Starting movie playback or clip playback stops the other.

Video controls provide in and out points, a two-handle range scrubber, retained-region playback,
endpoint frames, an audio-level track and Reset. The out-point preview shows the last retained frame.
Photo controls provide a duration and an option to follow the movie default; turning the default off
keeps the photo's current length. The controls always show the document's values, including after
Undo, Open or a change of focus.

Playing preview video uses two persistent frame sessions, bound to clip identity and opened ahead of a
crossfade. Photo and parked frames use the bounded frame cache. Preview audio is decoded off the UI
thread in bounded windows of each contributing clip's retained interval, held by source window so a
crossfade's incoming clip keeps its sound, and read ahead of the playhead. It is mixed at the endpoint
rate and the user's media volume into an endpoint ring long enough to outlast the UI tick that
refills it. When playback starts or jumps, the picture waits briefly for its sound so the two begin
together; a window still unread after that plays as silence rather than stalling, and sound that
falls behind the picture restarts at the playhead. A seek discards queued sound from the old position.

## 5. Movie settings

Movie settings are:

- Cut or one uniform crossfade between adjacent clips.
- Transition length. This one number is both the crossfade duration and the length of the end fades,
  because a second number meaning nearly the same thing is harder to predict than one. It is offered
  whenever either use is switched on, and is labelled for the one the current settings make.
- Fade in from black and fade out to black, applied to picture and sound.
- Default photo duration. Changing it updates photos that still follow the default.

Output is read-only and reports the derived dimensions, frame rate and duration.

## 6. Project files

The native project format is OpenTimelineIO (`.otio`). Movie writes one video track containing clips,
source ranges and uniform transitions. A saved project may have no clips. Source paths are relative
to the project folder when possible. Photos are represented as image references with retained
durations.

Movie reads the first video track and reports constructs it cannot represent. A foreign OTIO project
defaults to cuts and no end fades. A foreign dissolve is adopted only when every clip boundary carries
the same representable dissolve; mixed or unsupported transitions are reported as ignored rather than
silently applied to other boundaries.

Windows Live Movie Maker (`.wlmp`) import accepts video and image extents, orders them by timeline
position, and preserves trims and photo durations. Titles, captions, effects, animations, narration,
music and other unsupported elements are omitted and counted in the view.

Newly opened clips are unresolved until an asynchronous probe supplies type, dimensions, duration and
frame rate. A source confirmed missing remains in the timeline with its trim and an error marker.
Relink missing files checks a chosen folder for matching names, updates every match together, and
probes the replacements before clamping trims.

## 7. Rendering

Render is shown only when the running platform provides an encoder. It is enabled only when every clip
has been probed, exists, has a positive retained duration and has decodable dimensions.

Output is derived without asking for codec settings:

- Dimensions come from the largest displayed video, or the largest photo for a photo-only movie.
- Dimensions are fitted within 3840 x 2160 and rounded down to even values.
- Frame rate is the highest video rate, limited to 60 fps; photo-only movies use 30 fps.
- Audio is 48 kHz stereo, and video bitrate follows output size and rate. An audio track is written
  whenever the timeline holds a video, whether or not any of those videos carries sound.

Windows writes H.264 video and AAC audio in MP4 through Media Foundation. The codec is the system's
to supply rather than Diffractor's to ship: H.264 and AAC carry patent licences that a vendored
encoder would oblige Diffractor to hold, and the outbound licence in [third-party](third-party.md)
is chosen on the assumption it does not. This is why `platform::can_write_movies` exists at all, why
the FFmpeg build is configured without encoders on both platforms, and why Render is absent rather
than dimmed where the platform has no writer.

The render worker receives an
immutable project snapshot, precomputes clip timing once, walks frames in order and uses CPU surfaces
independent of the display device: a driver update or a device-removed reset mid-render would
otherwise destroy an hour of work. It retains at most the two frame decoders and two bounded audio
chunks needed by a crossfade.

The output is staged in the destination folder. Any missing or undecodable contributing frame fails
the render instead of writing black. Cancel abandons the writer and is checked again after encoder
finalization; the staged file replaces the destination only after successful completion. A failure
is reported as a translated reason; the encoder's own diagnostic text goes to the log.

## 8. Ownership and limits

Timeline and view state are UI-thread-owned. Workers consume detached paths, settings and clip
snapshots and publish complete results through the UI queue with generation, revision, path and trim
currency checks. Paint performs no file, database, decode or network work.

Movie intentionally has one video track, no title or effects track, no music or narration track, no
speed control, no per-clip volume, no pan-and-zoom, no color grading, no output settings, no redo and
no HEVC or AV1 output. Rendering is unavailable where the platform reports no movie writer.

## Where this lives

The timeline, timing, output derivation, project readers and the decision entering Movie makes about
the timeline it holds are in [model_movie.h](../src/model_movie.h) and [model_movie.cpp](../src/model_movie.cpp).

The task view, timeline controls, preview, project workflows and render worker are in
[view_movie.h](../src/view_movie.h) and [view_movie.cpp](../src/view_movie.cpp). Persistent Movie state
is in [model.h](../src/model.h); application entry, shutdown and command availability are in
[app.cpp](../src/app.cpp), [app_commands.cpp](../src/app_commands.cpp) and
[app_toolbar.cpp](../src/app_toolbar.cpp).

Frame and audio extraction are in [av_format.h](../src/av_format.h),
[av_format.cpp](../src/av_format.cpp) and [av_player.h](../src/av_player.h). The audio endpoint the
preview mixes into is [av_sound.h](../src/av_sound.h), with the Windows implementation in
[platform_win_sound.cpp](../src/platform_win_sound.cpp). The platform writer contract
is in [platform.h](../src/platform.h), with Windows implementation in
[platform_win_encode.cpp](../src/platform_win_encode.cpp) and the unavailable-platform answer in
[platform_linux_desktop.cpp](../src/platform_linux_desktop.cpp).

Tests are in [test_movie.cpp](../src/test_movie.cpp), [test_av.cpp](../src/test_av.cpp) and
[test_platform_win.cpp](../src/test_platform_win.cpp).