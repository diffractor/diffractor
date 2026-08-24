# Movie

**Status: partly implemented, and attached to no release.** The document, the project file, the
Movie Maker reader, the frame composition rule, the view and the Windows encoder exist. The render
worker in §9.5 does not, so Render is not yet offered as a command and §6's toolbar is short by one.

Movie assembles a sequence of photos and videos into one video file. It is deliberately the
smallest editor that is still worth having: an ordered list of clips, a trim on each, one
transition, and a render. Everything a timeline editor normally offers — tracks, titles, effects,
speed, a separate music bed — is out, and stays out until the small version has shipped and been
used.

The reference point is Windows Movie Maker, because that is the shape of the tool people ask for
and because Diffractor can read its project files.

## 1. What it is

```json
{
	"Change class": "User-visible behavior",
	"Scope": "All scopes",
	"Contents": "The photos and videos in the complete visibly selected set, in the order the listing presents them. Audio files are not offered. The set is the starting point only: once Movie is open, the timeline is its own ordered list and the user adds to it, removes from it and reorders it there.",
	"Target": "Complete visibly selected set",
	"Effect": "Writes one new video file to a folder the user names, and optionally one project file. No source file is read for anything but its pixels and samples, none is modified, moved or deleted, and recovery is deleting the file that was written."
}
```

`All scopes` is earned here: Movie reads decoded frames from file paths and nothing else. It asks
no question of the index, the query, the grouping or the sort beyond the order the listing already
presents, so a folder, a search and the collection produce the same behaviour from the same
selection.

Movie is the only task view that **creates** a file that is not a version of an item it was given.
Rename, Convert, Metadata and Date each transform items one for one; Import and Sync copy. Movie
consumes many and emits one, and that is why it owns a project file — there is a document here to
lose, which is true of no other task.

## 2. The view

The view is named **Movie**, following the one-word noun rule in
[design](design.md#naming-views-modes-and-presentation-choices). It is a task view, so it takes a
row in that document's view table and a `view_type` member when it ships; it is never a mode, and
"movie mode" names nothing.

It is entered from a command on the selection information panel — the panel that describes the set
it will act on, per [selection controls](selection-controls.md) — and from the same command in the
Items context menu. The command is offered when the selection holds at least one photo or video.
It is also entered with no selection at all by opening a project file, in which case the project
supplies the timeline.

Movie departs from the other task views in one structural way, and the departure has to be
deliberate rather than accidental:

> Every other task view's selector strip is a **view** of the target set. Movie's strip is the
> **document**. It is ordered, the order is meaningful, an item may appear in it more than once, and
> removing an item from it removes it from the movie rather than from a selection.

So Movie's strip is called the **timeline**, not the selector strip, and `selector_strip_for_view`
answers `none` for it. Reusing the selector would make a click mean "select" in eight views and
"seek" in the ninth, which is exactly the context-dependent surprise the design driver forbids.

Layout, top to bottom:

- The shared top bar, carrying Movie's own toolbar right-aligned, ending in Maximize/Restore and a
  text-bearing Close, as every task view does.
- The **preview**, occupying the main surface.
- The **transport**: a play/pause button, the elapsed and total time of the whole movie, and the
  movie scrubber.
- The **timeline** across the full width beneath them.

The **controls panel** sits at the right for the full height, as in Edit. Its explainer states what
Movie does, what it acts on and what it writes, and says that the preview is the render.

## 3. The timeline

One tile per clip, left to right, in playback order. Each tile shows the source thumbnail, the
clip's length after trimming, and a badge when the clip is trimmed. Tile width is fixed, not
proportional to duration: a proportional timeline turns a 3-second clip into an unclickable sliver
next to a 20-minute one, and a filmstrip of equal tiles is the thing the user asked for.

- Exactly one clip is **current**. The controls panel's lower half describes the current clip, and
  clicking a tile makes it current and moves the playhead to that clip's start.
- Dragging a tile reorders. The drop point is shown between tiles, never on one, because dropping
  *onto* a clip has no meaning in a single-track timeline.
- Dropping files or items from Items onto the timeline appends them, or inserts at the drop point.
- Remove takes the current clip out. It is undoable for the session — the timeline is a document,
  and a document that cannot undo a mis-click is not one.
- The clip under the playhead is marked, so the timeline reports where playback is even when the
  current clip is elsewhere. Current and playing are different facts and are drawn differently.

The timeline scrolls horizontally when it overflows and never grows past a fixed height.

## 4. The preview

**The preview is the render, run at display rate.** The same compositor produces the pixels in the
preview rect and the pixels handed to the encoder; only the working resolution and the timing
differ. This is not an optimisation, it is the whole reason the preview can be trusted, and it is
the same promise the guided operations make in [design](design.md#guided-operations): what is
previewed is what runs.

- The movie scrubber addresses the whole movie, not the current clip. Dragging it seeks the clip
  under the playhead and shows the nearest decodable frame — the behaviour the existing scrubber
  preview already has via `decode_nearest_frame` — then settles exactly when released.
- During a crossfade the compositor holds two clips open at once and mixes them. The read-ahead
  budget in [av_format.h](../src/av_format.h) applies to each, which is why the preview composites
  at the preview rect's size rather than at output size.
- Transport controls are play/pause, previous clip, next clip. There is no frame-step in v1.
- Every frame is **fitted, never stretched**, onto the output rectangle, letterboxed on black. This
  is the rule [rendering](rendering.md#a-stand-in-is-fitted-never-stretched) already states for
  stand-ins, and a movie assembled from mixed aspect ratios is the case where breaking it is most
  tempting and most visible.

## 5. The controls panel

Two halves, divided, exactly as asked. The upper half describes the movie; the lower half describes
the current clip. Both name what they act on before offering a parameter, per
[design](design.md#application-structure).

**Movie settings** (upper):

| Control | Effect |
|---|---|
| Transition | Cut or Crossfade. One choice, applied between every pair of clips. |
| Transition length | Seconds. Enabled only for Crossfade. |
| Fade in from black / Fade out to black | Two checks, applied at the movie's ends. |
| Photo duration | Seconds each photo is held. This is the default for photos added later, and changing it retrims every photo that is still at the default. |
| Output | Read-only. States the frame size, frame rate and estimated file size the render will produce, and how they were derived. |

**Clip settings** (lower), for the current clip:

For a **video**, a two-handle range scrubber over the clip's full duration, with a numeric in-point
and out-point beside it. Dragging either handle scrubs the preview to that handle's frame, so the
user sets the trim by looking at it rather than by reading a number. Releasing a handle returns the
preview to the playhead. A Reset restores the untrimmed clip.

For a **photo**, a duration in seconds, plus a checkbox to use the movie default.

For both: the source file name, its dimensions, and its position in the movie as "clip 3 of 11,
starting at 00:42".

The range scrubber is a new control. It is a two-handle sibling of the existing slider, and it must
follow the rule in [design](design.md#application-structure) that a control the user is actively
manipulating keeps ownership of its value across refreshes: the preview frames arriving from the
decoder must never write back to a captured handle.

## 6. The toolbar

Right-aligned in the shared top bar, before the window group:

- **Render** — writes the video. Its label is the operation, not a generic Run.
- **Save project** and **Open project**.
- **Import project** — reads a `.wlmp`.
- **Add items** — a file picker, for sources not in the current selection.
- **Remove clip** — takes the current clip out of the timeline.

Render is **absent, not dimmed**, on a system with no encoder, per the capability rule in
[design](design.md#command-availability). See §9.

## 7. The project file

The native format is **OpenTimelineIO** (`.otio`). There is no widely adopted lightweight standard
for this; OTIO is the closest thing, it is JSON, its data model is precisely "a track of clips with
source ranges and transitions", and it is readable by DaVinci Resolve and others, so a user who
outgrows Movie can leave with their work. Playlist formats — M3U, XSPF — cannot express a trim and
are not candidates.

Diffractor writes and reads the subset it uses directly with RapidJSON, via
[util_json.h](../src/util_json.h). The OTIO library is not vendored: it would bring Python-adjacent
tooling for a schema that is a few hundred lines of JSON, and
[third-party](third-party.md) is clear about what a dependency has to earn.

The subset is a single `Timeline` holding one video `Track`, whose children are `Clip` elements with
an `ExternalReference` target URL, a `source_range` in `RationalTime`, and `Transition` elements
between them. A photo is a clip whose reference is the image file and whose source range is the
duration it is held for — OTIO has no photo concept, and inventing one would make the file
unreadable elsewhere.

Reading is defensive: a file from another tool will contain tracks, effects, markers and metadata
Movie has no answer for. Movie takes the first video track, takes the clips and transitions it
understands, and refuses the file if that leaves nothing.

Source paths are stored **relative to the project file when possible**, absolute otherwise. A
project whose sources have moved opens with the clips it could not find marked, and offers to
relink from a folder the user picks; it does not silently drop them, and it does not refuse to open.

## 8. Importing Windows Movie Maker projects

`.wlmp` is plain XML, undocumented but stable and simple: media items carry an id and a file path,
and extents reference those ids with a position and an in/out time in seconds. Expat is already
vendored, so no dependency is added.

Movie takes video clips, image extents and their trims. It ignores titles, captions, credits,
effects, animations, pan-and-zoom, audio clips, narration and the music track. Windows Movie
Maker's `.MSWMM` — the older Windows Movie Maker 2 format — is not in scope.

Two practical points that will otherwise dominate the bug reports:

- Paths in a `.wlmp` are absolute and frequently stale, since the file long outlives the folder
  layout. Import must run the same relink offer as §7 rather than failing.
- Movie Maker durations are seconds as doubles and its trims are relative to the source, which maps
  onto the clip model directly. No frame-rate conversion is involved.

Import is offered wherever a project is opened; a `.wlmp` in the collection also gets an Open with
Movie command.

## 9. Rendering the video

### 9.1 What the output is, and why it is not asked

Movie asks the user for no output settings. The parameters are derived, and the derived values are
displayed:

- **Frame size** is the display size, after rotation, of the largest-area source in the timeline. If
  any video is present, only videos are considered — a 45-megapixel photo must not force a 8000-pixel
  movie. Clamped to 3840×2160, and rounded down to even in both axes because every codec here
  requires it.
- **Frame rate** is the highest source frame rate, clamped to 60, and 30 when the timeline holds no
  video.
- **Audio** is 48 kHz stereo.
- **Bitrate** is derived from frame size and rate.

Every clip is fitted into that frame, never stretched and never cropped.

### 9.2 Audio

Each video clip keeps its own audio, trimmed with the clip and mixed with the same crossfade
envelope as the picture. Photos contribute silence. A clip with no audio stream contributes
silence. There is no separate music track in v1 — that was the deferred question, and this is only
the answer to the other one.

### 9.3 The encoder, and the licensing problem

Two distinct problems are usually conflated here, and only one is about FFmpeg's configure line.

**Copyleft.** Diffractor is LGPL-2.1-or-later and the vendored FFmpeg is deliberately configured
without `--enable-gpl` ([third-party](third-party.md) records why). FFmpeg's *native* encoders are
all LGPL, so enabling them would cost nothing. What is GPL is a short list of *external* encoder
libraries — libx264, libx265, libxvid, libvidstab — any of which forces `--enable-gpl`, relicenses
Diffractor, and separately ends Microsoft Store distribution. Those four are permanently out.

**Patents.** Independent of the code's licence, patent pools charge per unit for *encoders*. AVC
(Via LA), HEVC (three pools) and AAC (Via LA) are all live. AV1, VP9, VP8, Opus, FLAC and PCM are
royalty-free; MJPEG, FFV1, MPEG-2 and MPEG-4 Part 2 are expired or unencumbered. The awkward part
is that FFmpeg has **no native encoder for any modern royalty-free codec** — AV1 and VP9 both need
an external library — so "just enable an encoder in the existing build" produces MJPEG or FFV1, a
multi-gigabyte file for a five-minute movie that nothing plays well. That is not a shippable Render
button.

**So v1 does not encode. The operating system does.** Windows ships an H.264 encoder MFT and an AAC
encoder MFT, and the Media Foundation sink writer muxes them into MP4; the hardware paths behind the
same interface are Intel QSV, NVENC and AMF. The patent licence is carried by Microsoft and the GPU
vendor for use on that machine, which is how Windows applications ship H.264 output without joining
a pool. This adds no dependency, changes no FFmpeg configuration, and is the fastest option because
it runs on the GPU.

HEVC is **not** offered: Windows has no HEVC encoder by default, only the paid Store extension.

This is an engineering position and not legal advice. It is chosen partly because it means the
patent question never has to be answered in order to ship.

### 9.4 The platform boundary

The encoder is therefore a platform capability, and every Media Foundation call lives in a
`platform_win_*` file behind an entry point in [platform.h](../src/platform.h). Per the platform
rules, that entry point is an *intention* — "encode this sequence of frames and samples to this
path" — not a wrapper over `IMFSinkWriter`, so a Linux implementation can be honest.

Capability is answered at run time by attempting to create a sink writer once, not fixed at build
time. When the answer is no, Render is absent from the toolbar. Linux gets an explicit stub that
answers no, which is what makes the Movie view buildable there before the encoder exists, and
[linux.md](linux.md) records it as debt. The portable second path — SVT-AV1 or libvpx in MKV, both
royalty-free — is the eventual Linux answer and belongs in
[post-release context](v-next.md) until someone needs it.

### 9.5 The render must not touch the display device

The compositor runs entirely on CPU surfaces and never uses the Direct3D device that draws the
window. A device loss mid-render — a driver update, a TDR, an RDP transition — would otherwise
destroy an hour of work, and [rendering](rendering.md#device-and-swap-chain) is clear that a lost
device is a thing that happens rather than a thing that is prevented. The encoder's own hardware
path is internal to Media Foundation and is not the display device.

`av_scaler::scale_frame` fits each decoded frame; the crossfade is a per-pixel mix that
`util_simd.h` can accelerate.

### 9.6 Writing the file

The render writes to a temporary file in the destination folder and moves it into place on success,
the staging rule [file I/O](file-io.md) already owns. A cancelled or failed render leaves no partial
file behind and no source touched. Collisions are resolved by the existing policy rather than a new
one.

Progress is reported as a percentage of movie duration with a running elapsed time, and Cancel is
offered throughout and takes effect within a frame or two.

## 10. Threading and ownership

- The timeline, the project, the controls and every `view_element` in Movie are UI-thread-owned.
- Preview decoding and compositing run on a worker and publish finished surfaces through
  `queue_ui`, checked against a generation counter — a settled scrub must never be overwritten by a
  frame from the seek before it. A successful `weak_ptr` lock is not that check.
- The render runs on one owning worker consuming an immutable snapshot of the project taken when
  Render was pressed. Editing the timeline during a render changes the next render, not the running
  one. The snapshot is a detached value, not a pointer into UI-owned state.
- No filesystem, decode or encode work happens on the UI thread, and no paint function opens a file.

## 11. What v1 does not do

Named so that each is a decision rather than an omission: multiple tracks; a music or narration
track; titles, captions and credits; visual effects and colour grading; pan-and-zoom on photos;
speed changes; per-clip volume; transitions other than crossfade; frame-accurate stepping;
output settings; HEVC or AV1 output; and rendering on Linux.

## 12. Open decisions

1. **The view name.** This document uses **Movie**. The feature was requested as "video maker";
   one-word nouns are the rule, and "Video" collides with the media type. Confirm before code.
2. **Silent WLMP dropping.** The chosen behaviour is to import clips and trims and ignore titles,
   effects and music without saying so. That conflicts with the product promise that the user is
   never surprised by hidden state — a project that imports as a silently different movie is exactly
   that surprise. The minimum reconciliation is a line in the explainer stating what was dropped,
   with no dialog and nothing to dismiss. This document does not resolve it.
3. **Undo depth on the timeline.** Session-scoped and unbounded, or a fixed depth.
4. **Whether an unsaved timeline survives leaving the view.** Every other task view discards its
   parameters on Close. A document should not, but Movie having a Save prompt when no other view
   does is its own inconsistency.

## 13. Checks

The suite owes these, and [testing](testing.md) owes a row for each file when they land. Those
marked done are in [test_movie.cpp](../src/test_movie.cpp) and
[test_platform_win.cpp](../src/test_platform_win.cpp) now.

- Done: the derived output geometry, over mixed-aspect, mixed-rate and photo-only timelines,
  including the photo-does-not-drive-frame-size rule and the even-dimension rounding.
- Done: the timeline as a document -- append, insert, reorder, remove, the same source twice, undo.
- Done: trim arithmetic and total duration under crossfades, including a transition longer than the
  clip it joins.
- Done: which clips a frame draws and at what weight, across a transition, at the movie's last
  instant, and through the fades.
- Done: OTIO round trip -- write, read, compare; and reading a file containing constructs Movie
  ignores.
- Done: WLMP import, including an entity-escaped path and elements Movie cannot represent.
- Done: the encoder capability probe, and the writer refusing an impossible request without leaving
  a partial file.
- Outstanding: Render being absent rather than dimmed, which needs the view.

None may open a window, encode a real file, or depend on hardware. That last constraint is the
reason nothing here proves the encoder produces a playable file; that is verified by hand.

## Where this lives

The document, its arithmetic and its project files are [model_movie.h](../src/model_movie.h) and
[model_movie.cpp](../src/model_movie.cpp): `movie_project` is the timeline document,
`derive_movie_output` the geometry rules in §9.1, `movie_overlap` and `calc_movie_timing` the
transition arithmetic in §3, `calc_movie_frame` the composition decision in §4, and `write_otio`,
`read_otio` and `read_wlmp` the files in §7 and §8. Nothing in it decodes, draws or touches the UI.

The view is [view_movie.h](../src/view_movie.h) and [view_movie.cpp](../src/view_movie.cpp):
`movie_view` is the surface, `movie_timeline_element` the strip in §3, `movie_trim_control` the
two-handle trim in §5, `movie_view_controls` the panel, and `movie_source_cache` the one decode at
a time that both the strip and the preview draw from. `movie_view_state` lives in
[model.h](../src/model.h) beside `edit_view_state`, because the application holds it so a timeline
survives leaving Movie and returning. `app_frame::selector_strip_for_view` in
[app.cpp](../src/app.cpp) answers `none` for Movie, which is where the strip's independence from the
selector is enforced.

The encoder is `platform::movie_writer` in [platform.h](../src/platform.h), implemented for Windows
in [platform_win_encode.cpp](../src/platform_win_encode.cpp) and answered false on Linux in
[platform_linux_desktop.cpp](../src/platform_linux_desktop.cpp). `platform::can_write_movies` is
the run-time capability question §6 and §9.4 depend on.

Tests are [test_movie.cpp](../src/test_movie.cpp), plus the writer's refusal and probe in
[test_platform_win.cpp](../src/test_platform_win.cpp).

The render worker in §9.5 does not exist yet, and is the reason Render is not among the commands in
[app_toolbar.cpp](../src/app_toolbar.cpp).
