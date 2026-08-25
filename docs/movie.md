# Movie mixer

**Status: implemented, and attached to no release.** The document, the project file, the Movie Maker
reader, the frame composition rule, the view, the Windows encoder, the render worker and the
preview's own sound all exist. What §12 still holds is the consequences of the last of those rather
than a missing piece of it.

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

The view is named **Movie mixer**. It is a task view, so it takes a row in
[design](design.md#naming-views-modes-and-presentation-choices)'s view table and a `view_type`
member; it is never a mode, and "movie mode" names nothing.

It is entered from a command on the selection information panel — the panel that describes the set
it will act on, per [selection controls](selection-controls.md) — and from the same command in the
Items context menu. The command is offered when the selection holds at least one photo or video,
and also whenever the timeline already holds clips: a timeline outlives the view, so clearing the
selection must not lock the user out of the movie they are part way through. A project file is
opened from inside the view, and supplies the timeline in place of the selection.

Re-entering with a different selection is the case both obvious answers get wrong. Always reseeding
throws away a movie the user spent an hour on; never reseeding means selecting new clips, choosing
Movie mixer, and being shown the old ones — which reads as the command having done nothing. The rule
is therefore about what the timeline *is*:

> A timeline that is still exactly what a selection produced is a **view of that selection**, and a
> different selection replaces it. A timeline the user has edited, or opened from a project file, is
> a **document**, and is left alone.

So the document remembers the selection it was seeded from, and seeding is not itself counted as an
edit. One trim, one reorder, one added file, and the timeline stops being replaceable.

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

The strip carries three facts at once, and they are drawn three different ways because they are
three different things:

| Fact | What it means | How it is drawn |
|---|---|---|
| Selection | The set a command acts on. One or many. | Filled background |
| Focus | The single clip the controls panel describes. Always exactly one. | Outline |
| Playing | Where the playhead has reached. | Bar along the tile's foot |

A fourth is drawn only when it is true: a clip whose source the probe could not find shows an error
marker over its file name instead of a frame. It is not decoded, and it is not asked for again — a
tile that retried a missing file would start a decode on every paint for the rest of the session.
That marker states "looked for and gone", which is a different fact from "not looked for yet": a
freshly opened project holds nothing but unresolved clips until the probe answers, and marking those
would accuse every project of being broken for as long as it took to open.

Focus is always exactly one clip, and it is always the clip the user last clicked, whether that
click selected, extended or toggled.

- **Plain click** selects one clip, focuses it, and anchors the range.
- **Ctrl+click** adds or removes one clip and moves focus to it. The last selected clip cannot be
  toggled away: a command that acts on the selection would then have nothing to act on while the
  strip still shows a focused clip.
- **Shift+click** selects from the anchor to the clicked clip.
- **Dragging** moves the whole selection to the drop point, keeping the clips' order relative to
  each other and carrying focus with them. A press inside an existing multi-selection does not
  collapse it until release, because collapsing on press would make dragging a block impossible.
  The drop point is shown between tiles, never on one — dropping *onto* a clip has no meaning in a
  single-track timeline — and a block dropped where it already is costs no undo step.
- **Right-click** offers Send to the end, Remove from movie, Select all, and Add files. Clicking
  outside the selection moves the selection first, so the menu acts on what was just pointed at.
- **Dropping files** or items from Items onto the strip appends them, or inserts at the drop point.
  A drop anywhere else in the view appends, because nothing outside the strip points at a position.
  Only photos and videos are taken; anything else in the same drop is ignored rather than refusing
  the whole drop.
- **Remove** takes the selection out. It is undoable for the session — the timeline is a document,
  and a document that cannot undo a mis-click is not one.
- **The wheel** scrolls the strip while the pointer is over it, per
  [zoom](zoom.md#111-the-wheel): the strip is one row that runs off the side, so either axis moves
  it along and there is nothing else for the wheel to do there.

Every drag here — a block of tiles, the movie scrubber, a trim handle — begins on the press and ends
on the release. A controller is built as soon as the pointer is *over* something, so anything that
acts on pointer movement alone reorders the strip, or seeks the movie, under a pointer that was only
crossing it on the way somewhere else.

That same fact decides how far a controller reaches. The framework rebuilds one only once the pointer
leaves the bounds it claimed, so a controller must claim **the thing it answers for** — the tile, the
handle, the button — and never the panel it sits in. A controller that claimed the whole strip went
on answering for whichever tile the pointer first crossed, so every click after that selected the
wrong clip while looking as though the strip had stopped responding.

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
- Transport controls are previous clip, play/pause, next clip, and they are clickable as well as
  reachable from the keyboard. There is no frame-step in v1: stepping is by clip, and a step lands
  the playhead on that clip's start and focuses it, so the transport and the strip never disagree
  about which clip is current.
- Every frame is **fitted, never stretched**, onto the output rectangle, letterboxed on black. This
  is the rule [rendering](rendering.md#a-stand-in-is-fitted-never-stretched) already states for
  stand-ins, and a movie assembled from mixed aspect ratios is the case where breaking it is most
  tempting and most visible.

### 4.1 A preview decodes forward; it does not sample

A clip that is playing is asked for a frame every fortieth of a second, and each of those frames is
a few frames on from the last. Reopening the file, seeking, and decoding from the nearest key frame
for each one costs a hundred times what walking forward from the previous frame costs, and it does
not merely make the preview slow — it makes it **empty**, because the playhead moves on wall clock
and never returns to a position whose decode has finally landed.

So the preview slots hold their decoders open. Each contributing clip keeps one, and a request for a
position ahead of where its decoder already sits decodes forward to it; only a position behind, or
far enough ahead that walking would cost more than a seek, seeks.

A slot is an `av_session`, opened video-only through `av_player::open_frames`, which is what makes
the reading genuinely *ahead*: a session demuxes and decodes on the player's own threads, so the
frame the playhead is about to want is usually already decoded when it is asked for. The player
services two of these beside the session it is playing, and Movie asks it for a seek only when the
position it wants is further from where the session has reached than the forward walk can absorb.

The alternative was evaluated and rejected. A session cannot run itself — something has to call
`process_io` and `process_video` — and the only two candidates were the player's existing threads or
a second pump inside Movie. A second pump is a second set of threads doing the same work, competing
with playback for the same cores, and the threading rules forbid Movie owning them. So the player
gained the slots rather than Movie gaining a player.

A photo has no stream to read ahead, so it stays on the frame cache, and the cache also stands in for
a video while its session is still opening: the preview never blanks waiting for a decoder.

Two rules keep a transition from costing an open, and both are easy to undo by accident:

- **A slot is bound to a clip, not to the frame's a or b position.** At the end of a crossfade the
  incoming clip moves from `b` to `a`, so mapping `a` to the first slot tears down the warm session
  that had just filled and reopens the same file in the other one. That reopen is a black gap in the
  middle of playback.
- **The clip a transition is about to bring in is opened before the transition starts.** A session
  that opens when the crossfade opens spends its first moments contributing nothing, so the composite
  is one picture at declining weight over black — which reads as a flicker rather than as a fade. It
  is opened at its in point and left paused until it contributes.

A slot is also kept once its clip stops contributing, rather than closed at the first frame that does
not need it: closing it is how the next transition ends up paying for an open it could have avoided.
Slots are released when the view is left.

Two consequences follow, and both are deliberate:

- **Latest wins, per slot.** A slot's pending request is replaced rather than queued. A backlog would
  be played out behind the playhead, which is worse than a dropped frame.
- **A slot holds its last frame.** While a decode is in flight the slot draws the frame it already
  has, so a preview waiting on a decoder holds the picture instead of blinking to black. It drops
  that frame the moment the slot moves to a different clip, because at a cut the stale frame is the
  wrong source rather than an old one.

The strip is the other case and is decoded the other way: one frame of one clip, wanted once and
then kept, so it opens the file, takes its frame and closes it.

### 4.1.1 The preview supplies its own sound

The preview is the render, and §9.2 says each clip keeps its own audio, so a silent preview is not
the render. The obstacle is that the application player owns **one** audio device and plays **one**
session on it, and the clip player in §5 already borrows that session. A crossfade needs two sources
audible at once, which one session cannot be.

So the movie preview mixes its own. Each contributing clip's audio is decoded once, whole, into a
buffer of interleaved 16-bit stereo at the endpoint's own rate — `av_format_decoder::extract_audio_pcm`
— lazily, off the queue the pictures do not use, and bounded, because 48 kHz stereo costs 192 KB a
second and two are held at once. Playback is a position in those buffers, mixed at the weights
`calc_movie_frame` gives for the same instant the picture is composed from, and written to an
endpoint this view owns. Decoding at the endpoint's rate is what keeps the mixer free of a
resampler: only the sample format and the channel count are left to translate.

The mixed sound is kept a fraction of a second ahead of the playhead — far enough that a paint or a
layout cannot starve the device, near enough that a seek does not play the position the user has
just left. A queue that has fallen behind the playhead is discarded and rebuilt rather than played
out, because sound that lags the picture for the rest of the movie is worse than a gap.

The consequence, taken deliberately: **the two hear a file through different code.** The clip goes
through the player's resampler and device; the movie through these buffers and this mixer. They are
never audible at once, so this is an asymmetry rather than a bug — but it is one somebody would
otherwise find and assume was a mistake.

### 4.2 A playing preview runs at the display's rate

The application's idle timer ticks five times a second. That is right for a window that is only
waiting, and it is what a playing movie must not be driven by: a preview advanced five times a
second does not look slow, it looks like it is stepping. So while the movie plays, Movie asks for the
same cadence video playback already gets — the frame preparation drops to the display's refresh
interval, the playhead advances there, and the view is invalidated per frame.

Two things follow from that rate. Frames are cached on whole steps rather than per pixel of playhead,
so most refreshes reuse the frame already on screen instead of asking for a new decode. And each
preview slot keeps **one** texture and updates it in place; allocating a texture per frame is a GPU
allocation dozens of times a second, which is a thing no video path does.

### 4.3 A parked preview answers for the focused clip

A playing preview answers "what does the movie look like now". A parked one is answering a different
question — "what is this clip I just picked" — and answering the first question is a wrong answer to
the second in two specific places:

- **At the movie's first instant the render is black.** With fade-in on, the honest composite at
  playhead zero is a fully opaque black rectangle. That is correct for a render and useless for a
  preview, so the fade is drawn only while the movie is being played through, and suppressed while
  the preview is parked.
- **At a crossfade one instant belongs to two clips.** The composite exactly at a clip's start is the
  *previous* clip at full weight and the new one at zero, so seeking to a clip's start and showing
  the render there shows everything except the clip that was clicked.

So picking a clip — by click, by keyboard, or by the previous/next transport buttons — puts the
playhead on that clip's start, so the scrubber and the strip agree with each other, and **parks the
preview on that clip's first kept frame** rather than on the composite at that instant. The parking
ends the moment the user says where to look instead: a scrub, a play, or a trim handle. Stepping by
clip counts from the focus rather than from the playhead, for the same reason a crossfade breaks the
other direction: one playhead position maps to two clips, and only the focus is unambiguous.

The consequence the user sees is that **both pictures always show something**. There is always a
focused clip, so the preview always has a frame to park on, and the clip control shows that same
clip's in-point frame whenever its own player is not running. Playing the movie and playing the clip
remain exclusive, but only one of them is *moving* at a time — neither goes blank because the other
started.

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
| Output | Read-only. States the frame size, frame rate, length and estimated file size the render will produce. |

**Clip settings** (lower), for the current clip:

For a **video**, a two-handle range scrubber over the clip's full duration, with a numeric in-point
and out-point beside it. Dragging either handle scrubs the preview to that handle's frame, so the
user sets the trim by looking at it rather than by reading a number. Releasing a handle returns the
preview to the playhead. A Reset restores the untrimmed clip; it appears only once the clip has
actually been trimmed, because a control that undoes nothing is a control the user has to think
about for no reason.

The scrubber carries three things beyond its handles, because a trim set from two numbers is a trim
set blind:

- **The frame the handle in hand is selecting**, above the track. One frame, not two: while a handle
  is moving, the only frame that matters is that handle's, and showing both halved the size of each
  to answer a question nobody was asking. Releasing a handle leaves its frame on screen rather than
  snapping to the other end. The out-point frame is the step *before* the out point, since the out
  point is the first instant not kept and showing it would show a frame the movie will not contain.
  Each end keeps its own decoder, because the two are distant positions in one file and one decoder
  would seek between them on every drag.
- **The audio level across the source**, drawn on the track itself. Trims are usually aimed at the
  start or end of someone talking, and that is invisible in a picture. Levels inside the trim are
  drawn at full strength and levels outside it dimmed, so what a handle would have to move to
  include is visible rather than hidden. The levels are peak, not mean, because a mean over a bucket
  flattens exactly the thing being looked for; they are measured once per source, off the queue the
  preview uses, so measuring a long stream never delays the frame the user is dragging toward.
- **A play button for the kept region.** The clip control is its own small player: it runs from the
  in point to the out point and stops there, and it plays *in the frame above the track*, with the
  position marked on the track it was set on. Checking a trim by playing the whole movie from
  wherever the playhead happens to be is not checking it, and the question being asked is always
  "what is left".

The movie and the clip are two players over the same document, and **only one of them ever runs**.
Starting either stops the other. Two pictures moving at once, to two different times, is not a
preview of anything.

A photo has none of the three: it holds one frame and carries no sound. It keeps the frame, though —
the control shrinks to the picture alone. The rule that both pictures always show something is not
met by a panel that describes a photo and shows nothing of it, and a photo is exactly the clip whose
file name says least about what it is.

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
- **Add files**, **Remove from movie**.
- **Relink missing files** — offered only when the probe has found a clip whose source has gone. See
  §7.
- **Open project** and **Save project**.
- **Import Movie Maker project** — reads a `.wlmp`.

Render is **absent, not dimmed**, on a system with no encoder, per the capability rule in
[design](design.md#command-availability). See §9. Relink is absent for the same reason and a
different one: there is nothing to repair, and a repair offered against nothing reads as a repair
that is broken.

The view is entered from **Movie mixer** in the Tools menu, which is reached from the Tools button
in the shared top bar and from the Items and Fullscreen context menus.

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
project whose sources have moved opens with the clips it could not find marked, keeping their trims;
it does not silently drop them, and it does not refuse to open. **Relink missing files** is the
repair: the user picks a folder, and every clip that was not found and whose file name is in that
folder is re-pointed at it. The trim is the work and the path is only where the work was aimed, so a
relink replaces the second and keeps the first, and the re-pointed clip is probed again before its
trim is clamped to the new source. It is one undo step. A relink that matched nothing says so,
because it is the only outcome that leaves the timeline exactly as it was.

## 8. Importing Windows Movie Maker projects

`.wlmp` is plain XML, undocumented but stable and simple: media items carry an id and a file path,
and extents reference those ids with a position and an in/out time in seconds. Expat is already
vendored, so no dependency is added.

Movie takes video clips, image extents and their trims. It ignores titles, captions, credits,
effects, animations, pan-and-zoom, audio clips, narration and the music track. **The explainer says
how many elements were left out**, because a project that imports as a silently different movie is
exactly the hidden state the product promise forbids. It is a line of text and not a dialog: there
is nothing to decide and nothing to dismiss. Windows Movie Maker's `.MSWMM` — the older Windows
Movie Maker 2 format — is not in scope.

Two practical points that will otherwise dominate the bug reports:

- Paths in a `.wlmp` are absolute and frequently stale, since the file long outlives the folder
  layout. Import must run the same relink offer as §7 rather than failing.
- Movie Maker durations are seconds as doubles and its trims are relative to the source, which maps
  onto the clip model directly. No frame-rate conversion is involved.

Import is offered wherever a project is opened. A `.wlmp` is not a media file, so it does not
appear in the collection and there is no Open with Movie command on one: `.otio` and `.wlmp` are
reached from Open project and Import Movie Maker project in §6's toolbar, and nowhere else.

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

The fades at the movie's ends are fades of the **movie**, not of its picture: a frame going to black
over a soundtrack still at full level is not what anyone means by "fade out", so the same envelope
is applied to both.

The sound is mixed in lockstep with the picture — one chunk per output frame, at that frame's
weights — so the two are computed from the same instant and cannot drift apart. Chunk boundaries
are counted in samples rather than derived from the frame index, so a sample rate that does not
divide the frame rate cannot accumulate an offset over a long movie.

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

The worker is the preview's own decision run at output resolution. It walks the movie in order,
calls `calc_movie_frame` at each output instant, and keeps one forward-walking decoder per clip a
crossfade can hold open — two, because the half-clip ceiling in `movie_overlap` stops a third
reaching across a short clip. Walking in order is what makes those decoders worth keeping: after the
first frame of a clip, every request is a step forward and costs no seek.

The canvas starts black and each contributor is **added** at its weight, because a frame's weights
sum to one. A single contributor therefore lands unchanged, and the letterbox stays black because
black is what it was added to. The fade is applied to the finished composite rather than drawn over
it, so what reaches the encoder is the pixels the preview showed.

### 9.6 Writing the file

The render writes to a temporary file in the destination folder and moves it into place on success,
the staging rule [file I/O](file-io.md) already owns. A cancelled or failed render leaves no partial
file behind and no source touched. Collisions are resolved by the existing policy rather than a new
one.

Progress is reported as a percentage of movie duration with a running elapsed time, and Cancel is
offered throughout and takes effect within a frame or two. It is coalesced to whole percent: a
message to the UI thread per output frame is tens a second of work with nothing to show for it.

While a render runs, Movie is a view with a task in it, so it behaves like every other one — Cancel
appears in the toolbar, navigating away is refused with the reason, and Render is dimmed rather than
offered twice.

## 10. Threading and ownership

- The timeline, the project, the controls and every `view_element` in Movie are UI-thread-owned.
- Preview decoding and compositing run on a worker and publish finished surfaces through
  `queue_ui`, checked against a generation counter — a settled scrub must never be overwritten by a
  frame from the seek before it. A successful `weak_ptr` lock is not that check.
- The frame cache's decoders are the one piece of Movie that is worker-owned. They are reached
  through a `shared_ptr`, but only ever from inside a task on the render queue, which one thread
  serves: tasks there run one after another, so they are single-context state and carry no lock.
  Closing them is itself a task on that queue rather than a call from the UI thread.
- The preview's two frame sources are `av_session`s owned by the player, not by Movie. Movie holds
  a `shared_ptr` to each and asks the player to open, seek, play and close them; the demuxing and
  decoding happen on the player's threads. A session that finishes opening after the slot has moved
  on is closed rather than adopted, checked on path and clip index — a successful lock is not that
  check.
- The preview's audio endpoint and its mixed buffer are UI-thread-owned, fed from `tick`. The clip
  buffers behind them are decoded on the load queue and published as whole immutable values through
  `queue_ui`, matched to a slot by path.
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

1. **The preview's clock is the wall clock, and its sound is not.** §4.1.1 mixes the sound itself
   and writes it to an endpoint Movie owns; the playhead advances on the wall clock, as it did when
   the preview was silent. The two are within a few milliseconds over any movie worth previewing, so
   nothing is wrong today — but the render is timed from sample counts and the preview is not, which
   means "the preview is the render" is true of the pixels and only nearly true of the timing.
   Re-basing the playhead on the endpoint's own clock is the fix; what it needs first is an answer
   for a machine with no endpoint at all, which must still play.

2. **The preview's sound stops when the window does.** The mixer is fed from the view's tick, which
   runs at the display's rate while the movie plays and stops when nothing is being drawn. A
   minimised or fully occluded window therefore starves the endpoint. That is tolerable for a
   preview — nobody previews a movie they cannot see — but it is a real difference from the
   application player, whose audio thread runs regardless, and it is the reason a music track (§11)
   could not simply reuse this mixer.

### 12.1 Decisions taken

Kept here as the reason, not as the behaviour — each is owned by the section named beside it.

- **How the movie preview hears two clips at once** → §4.1.1. The player owns one endpoint and plays
  one session on it, and the clip player already borrows that session, so the movie mixes whole-clip
  buffers itself and writes them to an endpoint of its own. The asymmetry that follows — the clip
  heard through the player, the movie through Movie — is stated there rather than discovered.
- **How the preview gets frames in time** → §4.1. `av_player` gained two frame-source slots rather
  than Movie gaining a second player: a session cannot run itself, and the only alternative pump was
  a set of threads Movie is not allowed to own.
- **The clip player's exclusivity** → §5. Borrowing the player's one session slot is what forces it,
  so every path that retires the clip player — starting the movie, moving focus, clicking the strip,
  leaving the view — closes the session, and a session that arrives after the focus has moved is
  closed rather than adopted. The two session behaviours it needed are owned by
  [rendering](rendering.md#a-session-that-is-not-the-user-watching-a-file).
- **Silent WLMP dropping** → §8. The explainer states the count from
  `movie_load_result::ignored_elements`, and the count of clips the probe could not find. A line of
  text, not a dialog: there is nothing to decide, only something the user has to know.
- **Relinking a moved source** → §7. A folder, matched by file name, keeping the trim, one undo
  step. It is what forced "not looked for yet" and "looked for and gone" to stop being one state:
  they look identical in the document and mean the opposite thing to the user.
- **Undo depth** → confirmed as it stands: session-scoped, 64 entries, each a copy of the whole clip
  list. A timeline is tens of clips of a hundred bytes, so 64 copies is tens of kilobytes. A diff
  would buy nothing measurable and would have to be correct about settings, selection, focus and the
  anchor as well as the clips. Revisit only if a music track or per-clip parameters make an entry
  large.
- **An unsaved timeline leaving the view** → it survives, and **Close asks nothing**. Leaving the
  view is not losing the movie, and a prompt no other task view has would be its own surprise; the
  explainer says the movie is kept, so nothing about it is hidden. Quitting Diffractor is the one
  point at which the document is actually lost, so it is the one point that asks — Save project,
  Close without saving, or Cancel — and a save the user backs out of is not consent to lose it.

## 13. Checks

The suite owes these, and [testing](testing.md) owes a row for each file when they land. Those
marked done are in [test_movie.cpp](../src/test_movie.cpp) and
[test_platform_win.cpp](../src/test_platform_win.cpp) now.

- Done: the derived output geometry, over mixed-aspect, mixed-rate and photo-only timelines,
  including the photo-does-not-drive-frame-size rule and the even-dimension rounding.
- Done: the timeline as a document -- append, insert, reorder, remove, the same source twice, undo.
- Done: strip selection -- plain, extend and toggle; the last selected clip surviving a toggle;
  a block move keeping its own order and carrying focus; a no-op drop costing no undo step;
  removing a multi-selection.
- Done: trim arithmetic and total duration under crossfades, including a transition longer than the
  clip it joins.
- Done: which clips a frame draws and at what weight, across a transition, at the movie's last
  instant, and through the fades.
- Done: OTIO round trip -- write, read, compare; and reading a file containing constructs Movie
  ignores.
- Done: WLMP import, including an entity-escaped path and elements Movie cannot represent.
- Done: the encoder capability probe, and the writer refusing an impossible request without leaving
  a partial file.
- Done: the forward walk in §4.1 -- successive positions decode forward and never go backwards, and
  a position behind the decoder seeks rather than answering with wherever it was left.
- Done: the level measurement in §5 -- one value per bucket, varying across a stream that has
  content, and empty rather than a row of zeros for a file with no audio.
- Done: the difference between a seeded timeline and an edited one in §2 -- seeding is not an edit
  and leaves nothing to undo, an edit makes the timeline a document, and a project file is never a
  seed.
- Done: the difference between a clip nobody has probed and one the probe could not find, and that
  re-pointing a lost clip keeps its trim, costs one undo step, and leaves the new source to be
  measured before the trim is clamped to it.
- Done: the whole-clip audio buffer in §4.1.1 -- interleaved stereo at the rate asked for, stopping
  exactly at its cap, carrying signal rather than silence, and empty for a file with no audio.
- Done: the two session behaviours §4.1 and §5 depend on -- a video-only open that keeps delivering
  frames to the end of the stream rather than stalling on an undrained audio queue, and playback
  that leaves the user's resume position alone. Both are in [test_av.cpp](../src/test_av.cpp).
- Outstanding: Render being absent rather than dimmed, which needs the view.
- Outstanding: the preview's frame sources and its mixer. Opening a frame session goes through the
  player's queue, which only the running app's threads drain, and the mixer writes to a real audio
  endpoint. Both are checked by hand.
- Outstanding: the render worker's composite -- adding weighted sources onto black, the letterbox
  staying black, the fade applied to the finished frame, and the audio chunking that must not drift.
  It lives in an anonymous namespace inside the view and encodes a real file, so it is checked by
  hand until the compositing step is lifted out of the view.
- Outstanding: the transport, the drop, the trim reset and the clip player are pointer work in a
  window, and no test here may open one. The clip player is further out of reach: opening a session
  goes through the application player's queue, which only the running app's threads drain. It is
  checked by hand.

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
two-handle trim, its end frames and its level track in §5, `movie_view_controls` the panel, and
`movie_source_cache` the one decode at a time that the strip, the preview and the trim all draw
from. `movie_slot_decoders` beside it holds the decoders §4.1 keeps open, and is the only state in
Movie that lives on a worker. `movie_view_state` lives in
[model.h](../src/model.h) beside `edit_view_state`, because the application holds it so a timeline
survives leaving Movie and returning. `app_frame::selector_strip_for_view` in
[app.cpp](../src/app.cpp) answers `none` for Movie, which is where the strip's independence from the
selector is enforced, and `app_frame::prepare_frame` in the same file is where a playing movie asks
for the display's frame rate rather than the idle one, per §4.2.

The clip player borrows a session through `av_player::open_detached` in
[av_player.h](../src/av_player.h), which exists because Movie has a path rather than an item and
because its playback is not the user viewing that file. `av_player::open_frames` and
`close_frames` in the same file are the preview's two frame sources in §4.1: sessions the player
demuxes and decodes for on its own threads but never plays, and never puts on the audio device.

The preview's own sound is `movie_view::pump_preview_audio` and `preview_pcm` in
[view_movie.cpp](../src/view_movie.cpp), writing to an `av_audio_device` from
[av_sound.h](../src/av_sound.h) that this view owns.

The decoding §4.1 and §5 rely on is [av_format.h](../src/av_format.h) and
[av_format.cpp](../src/av_format.cpp): `av_format_decoder::extract_frame_at` is the forward walk,
`extract_audio_peaks` measures the level track, and `extract_audio_pcm` is the whole-clip buffer
§4.1.1 mixes from.

A drop reaches the timeline through `app_frame::drag_over` and `drag_drop` in
[app.h](../src/app.h), which answer Movie before any of the folder-and-items questions, and
`app_frame::movie_drop` in [app.cpp](../src/app.cpp), which turns the frame point into a view point
so the clips land where the marker said. `platform::clipboard_data::drop_paths` in
[platform.h](../src/platform.h) is what makes every dropped path reachable rather than only the
first.

The encoder is `platform::movie_writer` in [platform.h](../src/platform.h), implemented for Windows
in [platform_win_encode.cpp](../src/platform_win_encode.cpp) and answered false on Linux in
[platform_linux_desktop.cpp](../src/platform_linux_desktop.cpp). `platform::can_write_movies` is
the run-time capability question §6 and §9.4 depend on.

The render worker is `movie_view::render_movie` and the file-scope `render_movie_to_file` beside it
in [view_movie.cpp](../src/view_movie.cpp), with `movie_render_sources` holding the two forward
walking decoders and two audio buffers §9.5 describes. `movie_clip::is_lost` in
[model_movie.h](../src/model_movie.h) is the "looked for and gone" the strip marks and the relink
in §7 acts on, and `movie_view::relink_missing` is the repair.

Tests are [test_movie.cpp](../src/test_movie.cpp), plus the writer's refusal and probe in
[test_platform_win.cpp](../src/test_platform_win.cpp).

The render worker in §9.5 does not exist yet, and is the reason Render is not among the commands in
[app_toolbar.cpp](../src/app_toolbar.cpp).
