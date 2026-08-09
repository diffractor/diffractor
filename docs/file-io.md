# File I/O

This document owns every path between a media file on disk and what the user sees: how bytes are
read and turned into a displayed image, how bytes are changed, how a change is discovered, and what
each of those guarantees on failure.

It replaces the former `loading.md` and `file-writes.md`, because reading and writing turned out not
to be separable. The write path exists to be read back, the read path exists to be invalidated by
writes, and the bug that motivated most of §3 and §10 was a reader and a writer holding the same
file. Splitting them put the interesting half of each subject in the other document.

Boundaries with the other documents:

- [Design](design.md) owns what the user is promised and how it is worded.
- [Implementation](implementation.md) owns overall architecture, threading and the index.
- [Metadata](metadata.md) owns which standard each container is read from and written to, and the
  per-property tag mappings.
- [Rendering](rendering.md) owns backends, samplers and texture upload.
- [Zoom](zoom.md) owns scale and centre, and §10 there states the image-quality promise this
  document keeps.
- [Third-party](third-party.md) owns vendored library provenance.
- [v-next](v-next.md) owns deferred optimisation work beyond §13 and the measured Explorer
  comparison.

The subjects in source are `texture_state` and `display_state_t` in [../src/model.h](../src/model.h)
and [../src/model.cpp](../src/model.cpp) for reading, `files::update` in
[../src/files_core.cpp](../src/files_core.cpp) for writing, and `index_state` in
[../src/model_index.cpp](../src/model_index.cpp) for discovery and scheduling.

Two terms are used throughout and are easy to confuse with zoom's vocabulary. The **phase ladder**
is the sequence of progressively better representations of one image (§4); zoom's *ladder* is the
fixed ladder of zoom percentages, which is a different thing. **Provenance** means whether a change
to a file was made by Diffractor or by something else (§2.3).

## 1. Goals

### 1.1 Reading

The user should never wait in front of an empty display, and should never be shown a hard cut *within
an item*. Every transition from one representation of an item to a better one is dissolved; moving to
a different item is immediate, because its thumbnail is already available. Two rules follow, and they
are how [zoom.md](zoom.md) L11 and L12 are met:

1. **There is always something to draw.** At worst a correctly shaped grey rectangle, at best the
   full-resolution image. Whatever the best available representation is, it is drawn immediately,
   scaled to the display bounds. No zoom or pan input ever waits on a decode.
2. **Quality only ever increases for a given item.** A better representation replaces a worse one,
   never the reverse, regardless of the order work completes in. A provisional representation is
   therefore always identifiable, which is what L12 needs.

### 1.2 Writing

Every write obeys five rules, in priority order.

1. **A failed write leaves the user no worse off.** Either the change lands completely or the prior
   bytes survive. There is no state where the media file is half-written and unrecoverable.
2. **A requested original backup must exist before the destination changes.** If the backup cannot
   be created, the operation fails and nothing is replaced.
3. **A media file and its sidecar are one logical update.** They roll back together where the
   platform allows it.
4. **What Diffractor reads back after a write is what it just wrote.** The index, thumbnail and
   displayed properties refresh from the bytes that landed, never from a stale cache. On SMB and NAS
   shares this does not follow from the write having succeeded — see §6.
5. **Cost is proportional to the change where the format allows it.** Rating a 141 MB video must not
   cost 141 MB of I/O when the format can absorb a bounded write.

Rules 1 to 4 outrank rule 5. An optimisation that cannot roll back, or that leaves the app reading a
stale copy of the file it just changed, is not taken.

The driving workflow is a photographer importing a card of video and stills, then culling and grading
them quickly. Ratings dominate, they arrive in bursts, and the collection frequently lives on a NAS.
That is what makes rule 5 matter: staging cost scales with the link, so a full copy that is merely
slow locally is punishing over SMB, where the file crosses the wire twice.

**Backups are a per-call request, not a global safety net,** and this is easy to over-read.
`setting.create_originals` defaults to **on**, but only the photo editor save path in
`src/view_edit.cpp` consults it. The metadata-only callers — the rating, label and tag paths in
`src/app_util.cpp` and `src/app_commands.cpp` — pass `create_original = false` unconditionally. So
the workflow this document exists to serve takes **no backup at all**, whatever the user has
configured. Rule 2 constrains what happens when a backup is asked for; it does not make writes
recoverable in general.

## 2. Concepts

### 2.1 Representations, not files

Nothing the user sees is "the file". It is one of a series of representations of the file, each
better than the last, each derived from the one before where possible. `_loaded` — the parsed
container — is retained for the life of a `texture_state`, and every later phase derives from it.
This is why a zoom change costs no disk I/O, and why device loss rebuilds a texture without
reopening the file.

### 2.2 A change is classified by what it invalidates

The useful description of a change is what a holder of a retained representation must throw away —
not which bytes on disk moved. Four disjoint answers, widening:

| Class | Retained pixels | Drawn result | Response |
| --- | --- | --- | --- |
| `metadata` | Valid | Unchanged | Publish the new metadata. Reload nothing. |
| `presentation` | Valid | Differs | Redraw. No decode, no file read. |
| `pixels` | Stale | Differs | Re-enter the phase ladder. |
| `identity` | Stale | Item moved or gone | Re-resolve the path, then treat as `pixels`. |

`presentation` is a real class and not a refinement worth skipping: EXIF orientation is metadata, and
changing it changes what is drawn without changing a single pixel of the decoded image. An embedded
thumbnail rewrite and an ICC profile change belong here too. Classifying by storage location rather
than by effect would put orientation in `metadata` and get it wrong.

`files::update` already computes most of this: it derives `has_photo_edits` and `path_change` to
choose between an in-place patch, a sidecar write, and staging plus replace. §10 describes how much
of the classification is currently exploited and by whom.

### 2.3 Provenance decides whether the classification can be trusted

- **Self-inflicted** — we wrote the file, so we know both that it changed and exactly what changed.
  The classification is authoritative.
- **Foreign** — an external process wrote it. We know nothing beyond "the folder changed", so the
  only safe classification is `identity`, and timestamp polling is the right mechanism.

This is *not* "first load versus subsequent load". An item displayed for an hour is still read cold
from disk after a cache eviction, after a zoom past the retained resolution, or when
`is_provisional()` reports the held representation is too small. Those are ordinary reads and must
not be routed through the write path. The axis is provenance, and provenance is known at the moment
the change is discovered.

### 2.4 Identity change, not the write, is what goes stale

A staged replace makes a name refer to a *different file object*, and a network redirector's cached
view of that name can outlive the swap. An in-place patch changes bytes within the same file object,
through the same client, so the client's own write invalidates its own cached view. §6 builds on
this; it is easy to get backwards, because the intuition is that writing more is riskier when in
fact writing *elsewhere* is.

### 2.5 In-place patch versus staged replace

A metadata-only edit to a container whose handler is known to perform a bounded write may patch the
live file directly. Everything else copies to a stage in the destination folder, edits the stage, and
atomically swaps it in. The first is rule 5; the second is rule 1. §7 is the gate that decides which
one a given write gets, and why that gate is an allowlist of containers rather than a capability
test.

## 3. The I/O model

### 3.1 Nothing that touches a file runs on the UI thread

No filesystem, decoding, database or network work happens on the UI thread, and no I/O runs under an
index lock. The UI thread builds requests, publishes results, and uploads textures — that last one
because only the draw context can create a texture, so it has nowhere else to go.

### 3.2 Which queue matters, because they are plain FIFOs

- **`async_queue::load`** — container reads for phases 2 and 3.
- **`async_queue::load_raw`** — phase 4, kept off the `load` queue so a multi-second development
  never delays the next image. Uses `reset_and_enqueue`, so only the most recent RAW development
  survives; superseded ones are dropped rather than run.
- **`async_queue::render`** — phase 1 placeholder decodes, alongside browser thumbnail staging.
  Sub-millisecond work.
- **`async_queue::render_display`** — phase 2 to 4 decodes for the displayed item. Tens of
  milliseconds each.
- **`async_queue::work`** — in-place batch writes, metadata and pixels alike.
  `view_state::modify_items` queues here and `files::update` runs here. One thread, so two writes
  never overlap each other.
- **`async_queue::scan_modified_items`**, **`scan_displayed_items`**, **`scan_folder`** — the
  readers that re-derive index records and thumbnails after a change.

`render` and `render_display` are separate because a placeholder queued behind the full-size decode
it exists to cover for is worse than no placeholder at all: it arrives after the thing it was
standing in for. Measurement while stepping quickly through a folder found the combined queue over
two hundred tasks deep, which is why the display went grey during the walk and stayed grey for about
a second after it stopped.

Because the queues complete independently, a phase 1 result that arrives after a later phase has
been staged is discarded rather than applied. Rule 1.1(2) is enforced where results are applied, not
assumed from queue ordering.

### 3.3 Write claims keep a reader off a file being written

A write opens read-write with no sharing; a scan opens for read with `FILE_SHARE_READ`.
Because the readers are triggered by the write, a fast sequence of edits had the rescan of edit N
holding the file when the write of edit N+1 arrived, and the write failed with a sharing violation.

`index_state` therefore holds the paths currently claimed by a writer, counted rather than as a plain
set. Two batches can overlap on one path — the writes serialize on `async_queue::work`, but a claim
spans from the UI thread that queues the write to the UI thread that completes it — and with a plain
set the first batch to finish would erase the second batch's claim while its write was still queued,
re-opening the sharing violation the claim exists to close. The count is incremented per claim and
the entry erased only when it reaches zero.
`view_state::modify_items` claims them before it queues the write and releases them in the completion
that already runs on the UI thread. The table is UI-thread-owned and needs no lock, because every
consumer queues from the UI thread. Paths rather than items cross the worker queue, so no owning
reference to a UI-owned object goes with the work. The release is reached even when a write fails,
because `files::update` converts every exception into a failed result rather than propagating one.

- `queue_scan_modified_items` defers claimed items into a pending set and re-queues them, forced, on
  release.
- `queue_scan_displayed_items` filters claimed items out *before* the request batch is built, so no
  loading claim is made for them and `items_view::retry_visible_thumbnails` simply re-offers them
  after its throttle.
- `queue_scan_folder` is deliberately not gated. It takes a folder rather than items, and the
  per-file open happens later on a worker, so gating it correctly would need a synchronized claim
  set — which is exactly what this design avoids. It remains a possible racer, but it is a
  background sweep rather than something a write triggers.

A dedicated writer thread was considered and is not required for exclusion. Serializing writes onto
one queue is what `async_queue::work` already does; the claim is what provides exclusion against
*readers*.

### 3.4 Results carry the modified time they were derived from

The claim closes the window for work queued after it is taken. It does not recall work already in
flight — a `scan_displayed_items` batch may reach the item after the claim exists. Publication
therefore checks currency as well as lifetime: a result derived from a modified time older than the
item's current stamp is discarded rather than applied.

The modified time is the currency token because it is already carried and already compared. This is
the same discipline as `texture_state::_decode_generation` and `display_state_t::_av_generation`, and
it is the enforcement point for the quality-only-increases rule.

Results return to the UI as detached values. The coherent handle from §6 is consumed inside
`files::update` by the re-scan that runs there, so it never reaches a caller. The one exception is
the AV display handle, which `update` moves out on request; it travels to the UI as a value, is moved
into the reopening session, and is cleared unconditionally on release so a superseded reopen leaves
nothing open.

### 3.5 Playback holds a handle for its whole duration

`media_reading` keeps the file open for as long as an audio or video session is playing, and ffmpeg
cannot be handed bytes in place of a seekable handle. A write must therefore detach the session,
because a metadata write opens read-write with no sharing and cannot coexist with the player's read
handle. Teardown closes the session and clears the display's pointer to it, so nothing on the present
or seek path can dereference a closed session.

Letting the in-place patch open with `FILE_SHARE_READ` so the session need not be detached at all was
considered and rejected. An in-place patch rewrites a bounded region, but the region can sit anywhere
in the container, and a reader with no coordination can observe it torn. The cost being avoided is
one session cycle; the risk is a decode fault during playback.

Overlapping operations share one detachment window. The first guard captures the playback state;
intermediate completions neither reopen the player nor queue a modified-item scan, because either
could open the file while a later write still owns it. The final guard restores playback once and
queues the final scan. The guard's destructor can run on a worker, because the guard is handed to
callers through completion callbacks, so it marshals the release to the UI thread — everything the
release touches, including the non-atomic nesting count, is UI-owned.

The reopen is queued, so it can land after a later teardown or navigation has already taken the
display. `display_state_t` carries an AV generation that every open captures and every publication
re-checks. A superseded reopen is closed rather than dropped: dropping it would leave the file open
across exactly the rename, replace or delete the detach was for.

## 4. Reading: the display phase ladder

Acquisition of *browser* thumbnails is a different pipeline, owned by
[implementation.md](implementation.md). This section starts from the point where an item is selected
for display.

Both pipelines obey one memory rule. Pixels are expensive and encoded bytes are not, so **only what
is on screen exists decoded**; everything else is held in its compressed form, and anything held
compressed is under a budget with a cheaper place to reload from. That gives four statements the
whole read path has to keep true:

- **Decode for the screen, not for the file.** Every decode is asked for at the size the display will
  use. Nothing is read ahead of what is displayed, and nothing is decoded at native size on the way
  to a smaller one when the codec can avoid it.
- **At rest, hold the cheapest form that exists.** An item keeps a bounded WebP thumbnail, and a
  displayed image keeps its encoded source file — *when it has one*. The codecs that decode straight
  to a surface have no compressed form at all, so for those the surface is the cheapest form and is
  what is kept. Nothing keeps a surface it is neither drawing from nor unable to rebuild.
- **Retention is budgeted, and evicts to something cheaper.** Decoded surfaces are bounded by
  `df::max_texture_bytes`, thumbnails by `df::max_thumbnail_bytes`, and the recent-texture cache by
  both a count and a byte budget over its irreproducible remainder. Each evicts to a cheaper source
  than the original work — SQLite for a thumbnail, the retained encoded image for a surface — and
  where no cheaper source exists, the expensive form is retained instead; see [4.7](#47-lifetime).
- **Textures are built just in time.** A GPU texture is created by the frame that first needs it and
  released with the rest of the graphics resources. It is never the thing that keeps pixels alive.

The two size-reduction mechanisms have to agree for that first statement to hold. libjpeg can scale
in the DCT domain by any N/8, but `ui::calc_scale_down_factor` uses only 1/2, 1/4 and 1/8, picking
the largest whose result still covers the target; `files_jpeg` passes it as `scale_denom` and the
app's own resampler closes the remaining gap. The direction is the invariant: the codec step must
land at or above the target so the resampler only ever downscales. Every other codec decodes at
native size and goes straight to `scale_if_needed`, which is why `estimate_decode_bytes` treats JPEG
and the rest differently.

Each phase is a better representation of the same item. Phases are skipped when unavailable or
unnecessary, and the display sits at the best phase reached so far.

| Phase | Representation | Source | Applies to | Reached |
| --- | --- | --- | --- | --- |
| 0 | Grey rectangle at the correct size and orientation | Index metadata | Everything | Automatically, free |
| 1 | Item thumbnail, scaled up | Already in memory or SQLite | Everything | Automatically, sub-millisecond |
| 2 | Embedded preview | JPEG inside the RAW file | RAW only | Automatically, fast |
| 3 | Source image decoded to fit the display | The file | Non-RAW only | Automatically, tens of milliseconds |
| 4 | Full RAW development | The file | RAW only | On request, slowest |

Phase 1 is the normal entry point, because the browser has usually already obtained a thumbnail; the
`texture_state` constructor seeds `_loaded.i` with it before any work is queued. Phase 0 covers the
case where it has not — a newly indexed item, or a cloud placeholder — and is what makes "no
thumbnail yet" a shaped grey box rather than a blank area or a stall.

Phase 1 is only *sub-millisecond* where it needs no worker at all, and `seed_placeholder` is what
buys that. The browser stages a decoded thumbnail surface for every visible item, so the item usually
holds the very pixels phase 1 wants; `get_tex` hands that surface straight to `_staged_surface` and
the next draw uploads it. Without that step phase 1 costs a render-worker hop plus a UI hop before
anything at all is on screen, and phase 0's grey rectangle stands in for the whole of it — which is
visible as a flash on every selection, and is what the ladder exists to avoid. The seed is applied to
cached entries as well as new ones, because §4.7 demotion gives up everything an entry had decoded.

That leaves the question of where the surface comes from when there is no browser. The media view has
no grid staging thumbnails behind it, and switching views clears the cached surface of every item
outside the browser's viewport, so the display keeps its own supply: `display_state_t::populate`
stages the thumbnail surface of the displayed item and the one either side of it. Two extra
320-pixel decodes — this is not the image read-ahead §4.7 rejected, which decoded whole neighbouring
images at display size. A view switch is correspondingly narrowed to offscreen items only: what the
user was just looking at is exactly what the media view is about to step through.

Staging is not enough on its own, because an item can reach the display with **no encoded thumbnail
at all**. A metadata-only index pass stores none, `trim_thumbnail_blobs` gives them up beyond the
browser's viewport, and only `items_view` ever asks the database for one. With `_loaded.i` empty
there is nothing for phase 1 to decode and `draw` queues nothing, so the media area holds phase 0 for
the entire file load and full-size decode — the longest and most visible version of the flash. So
`populate` also calls `index_state::queue_load_thumbnail` for any of those three items that has none,
which reads the database and falls back to a source scan. That result arrives long after the
`texture_state` was built, so `refresh` re-seeds on every layout rather than only at assembly.

### 4.1 The ladder branches by file type after phase 1

Phases 2 and 3 are alternatives, not successive steps. `files::load` is called with previews
permitted, and what comes back decides the branch:

- **Non-RAW** — JPEG, PNG, HEIF and the rest — decodes to phase 3 and stops. Phase 3 is the full
  image, so the ladder completes on its own with no user involvement.
- **RAW** — returns the camera's embedded JPEG with `is_preview` set, which is phase 2. That is where
  a RAW normally stays.

So "the full image loads automatically if it is needed" is true for ordinary images and deliberately
not true for RAW.

### 4.2 Phase 4 is a user decision, not a quality step

Developing a RAW is not simply *more* quality. The embedded preview is the camera manufacturer's
rendering of the shot; a developed RAW is Diffractor's. They differ in colour, tone and sharpening,
and the photographer may reasonably prefer either. Developing also costs seconds rather than
milliseconds. Because the result is a choice rather than an improvement, it is not made on the user's
behalf.

The choice is offered by `preview_control`, a button in the display info bar shown whenever
`can_preview()` — that is, for any RAW. It highlights while a development is running
(`is_preview_rendering`) and offers three options:

- **Show this RAW now** — `load_raw()` for the current item only; the setting is unchanged and the
  next RAW again stops at phase 2.
- **Always show RAW** — clears `setting.raw_preview` and loads immediately. From then on
  `texture_state::update` sees `_loaded.is_preview && !setting.raw_preview` and enters phase 4
  automatically for every RAW.
- **Always show preview** — restores `setting.raw_preview`, the default.

The setting is therefore the whole of the automatic behaviour: with it on (default) phase 4 is
per-image and explicit; with it off phase 4 becomes an ordinary automatic step and RAW behaves like
everything else, at its own cost.

### 4.3 Phase 0 has to be the right shape

A grey rectangle is only unobtrusive if it occupies exactly the space the image will. If its aspect
is wrong, the later phases visibly resize and relayout, which is more jarring than the blank it
replaced. Phase 0 therefore uses the item's metadata dimensions and orientation — both known from the
index without touching the file — and `calc_display_dimensions` applies the orientation swap. Where
metadata dimensions are absent, the thumbnail's own dimensions stand in.

A stand-in is not an answer, and `_display_geometry_known` must stay false while one is in use. The
thumbnail was scaled to fit a bounding box, so it carries the shape only to within a rounding error;
the first load replaces it. Marking the stand-in as known instead froze that rounded aspect, and the
correction then landed at whatever later moment happened to clear the flag — a resize with no visible
cause, long after the image had settled.

### 4.4 The pipeline

Reaching phase 3 takes three worker hops and three UI hops, which is why perceived latency exceeds
decode time:

1. **UI** — `refresh` decides more pixels are needed and calls `load_image`.
2. **`async_queue::load`** — `files::load` reads and parses the container. For most formats this
   yields a still-compressed image in `_loaded.i`, not pixels. Cheap: single-digit milliseconds.
3. **UI** — `texture_state::update` swaps in the new `_loaded`, clears `_is_placeholder` and sets
   `_tex_invalid`.
4. **`async_queue::render_display`** — `draw` observes `_tex_invalid` and queues `to_surface`, the
   actual pixel decode and scale. This is the expensive step, tens of milliseconds.
5. **UI** — the decoded surface is applied to `_staged_surface` and a redraw requested.
6. **UI** — the next `draw` uploads the surface to a GPU texture and calls `fade_out`.

Phases 1 and 2 use the same steps 4 to 6; phase 1 skips steps 1 to 3 because its representation is
already in hand, and skips steps 4 and 5 as well wherever the browser already staged the surface.
Step 6 happens on the UI thread by design: only the draw context can create textures. Decoding never
does.

Steps 2 and 4 race, and phase 1 loses that race often enough to matter: `files::load` is single-digit
milliseconds on a queue of its own, while the phase 1 decode shares `async_queue::render` with
browser thumbnail staging. The publish guard therefore has one exception. A superseded placeholder
result is still adopted when nothing is drawn or staged, because the alternative is phase 0 for the
full duration of the phase 3 decode. It is staged alone: the retained surface and the navigator
downsample belong to the phase that superseded it, and a thumbnail-derived surface in
`_retained_surface` would be reused as though it were the source.

### 4.5 Resolution changes re-enter the ladder

Phases are not a startup-only sequence. Zooming in, resizing the window, and entering fullscreen all
mean "we now need more pixels than we hold", and they re-enter at the same point: keep drawing what
we have, scaled, and request something better.

`calc_scale_hint` derives a target size from `_display_bounds` — the full scaled image rectangle,
which grows as the user zooms in — and `draw` re-requests whenever `_loading_scale_hint` no longer
matches. `refresh` applies the equivalent test to the file load
(`_display_bounds.width() > _loaded.dimensions().cx`).

The hint is the source reduced by an integer factor, and only where the display bounds are less than
half the source; otherwise it is full source resolution. It never asks for more than the source has,
so zooming past 100% stops generating new decodes and magnifies what is held.

Decoding to display size rather than full resolution is deliberate: it bounds decode time and memory,
and it keeps minification below roughly 2x so the Catmull-Rom sampler described in
[rendering.md](rendering.md) stays well sampled. `calc_sampler` depends on this and would alias
without it.

The seeded phase 1 is the one representation that is *not* pre-scaled to display size — it is the
browser's thumbnail at thumbnail resolution — so it reaches `calc_sampler` at four or five times
magnification, past the threshold where a real image is drawn nearest-neighbour. It is passed as
provisional so it is magnified smoothly; [zoom.md](zoom.md#3-the-laws) L12 owns why.

A RAW held at phase 2 is the exception. Zooming past the embedded preview's resolution upscales it
rather than quietly starting a development; the phase 4 decision stays with the user however far they
zoom.

### 4.6 What is retained

`_loaded` — the result of reading and parsing the file — is retained for the life of the
`texture_state`, and every later phase is derived from it. That is what lets
`free_graphics_resources` rebuild a texture after device loss, and what keeps a zoom change off the
`load` queue and away from the disk.

The largest decoded surface reached for the current phase is retained. A smaller scale hint is
downsampled from it on the render worker rather than decoded from the source again, and the navigator
is derived from the same surface. A later request replaces it only when it produces a larger
representation. One retained surface is capped by `df::max_texture_bytes` — at most 32 million pixels
(roughly 128 MiB at four bytes per pixel), and less on a machine whose graphics or system memory
cannot carry that; see [rendering.md](rendering.md#image-budgets). Reaching that ceiling leaves the
quality mark visible when the displayed image needs more source pixels.

### 4.7 Lifetime

`display_state_t::populate` carries a `texture_state` forward when the same item remains displayed
after a selection change. Stepping to another image can reuse the bounded recent texture cache;
otherwise it builds a fresh `texture_state` and restarts at phase 1. No image is loaded ahead of the
one being displayed: a decode at the size the display asks for is fast enough that reading files the
user has not opened costs more memory than it saves time, and most navigation passes straight over
an image without zooming into it.

The cache retains **by form, not by recency**. `release_undisplayed` demotes every entry that is no
longer displayed: it gives up each representation that entry decoded — staged surface, retained
surface, navigator downsample and GPU textures — and keeps only `_loaded`. For the codecs that end at
`_loaded.i` that leaves the encoded file, which the next draw re-decodes at display size for less
than the surface cost to hold. For the codecs that end at `_loaded.s` — PSD, HEIF, JXL, TIFF, GIF,
BMP and a developed RAW — the surface *is* the only representation, so it survives. Returning to a
demoted entry therefore has no texture, and would show phase 0 for the length of a phase 3 decode if
`get_tex` did not re-seed phase 1 from the item's staged thumbnail. That is
deliberate: those already paid for a whole-frame native-size decode and have nothing compressed to
fall back on, and a developed RAW additionally carries a phase 4 decision the user made explicitly.

Demotion runs from `view_state::load_display_state` once the new display is live. Nothing reads the
outgoing entry after that point: with no item-to-item fade, the incoming image no longer needs the
outgoing one's `_last_draw_tex`.

What is left after that demotion is therefore only the irreproducible remainder, and that is what the
byte budget bounds — `df::max_texture_bytes`, about what the displayed image itself is allowed, so
the cache can carry one expensive decode forward rather than several. The cache also holds at most
`max_recent_textures` entries; two of those are structural, since `populate` finds the outgoing
display's `texture_state` through this same map and would otherwise restart a still-displayed item at
phase 1. `free_graphics_resources` clears the cache outright: nothing in it is on screen, and the
displayed textures are held by the display state itself.

`free_graphics_resources` drops every GPU object and sets `_tex_invalid` but keeps `_loaded`. Device
loss, a theme change, or a DPI change therefore rebuilds the texture from the retained representation
without returning to the file.

### 4.8 Video and audio

`refresh` is gated on `_is_photo`, so video and audio never enter the file-load phases. They still
get phase 1: the constructor seeds the thumbnail, `draw` stages it, and it fills the display area
until the first decoded frame arrives.

Once playing, `_vid_tex` takes precedence over `_tex` in `draw` and the player updates it directly
through `av_session::update_texture`. `texture_state::update(surface)` is the media-preview entry
point and clears `_vid_tex` when a still frame is staged instead.

A few extensions are claimed by both a container and a widely used text format: `.ts` is an MPEG-2
transport stream and a TypeScript file, and the same holds for `.m2t`, `.m2ts` and `.mts`. For those,
`av_format_decoder::open` reads the first kilobyte and applies `files::media_header_matches` before
ffmpeg sees the file, so a source file is refused outright rather than part-opening as a stream with
nothing in it. The rule is positive and specific — a transport stream is a run of packets each
opening with the `0x47` sync byte at a 188, 192 or 204 stride — so extensions with no entry are
decided by the decoder exactly as before. The run is searched for rather than assumed to start at
offset zero, because a partial capture or a PVR dump can begin mid-packet or behind a prefix, and
ffmpeg's own probe would find it; four aligned packet starts are required, which text cannot supply
by accident. A refused open reaches the panel as `display_state_t::_av_open_failed`, which suppresses
both the media element and the transport row and leaves the hex dump.

A header rule is only worth writing where the format is worth keeping. Where the sole media claim on
a contested extension is an obscure or output-only ffmpeg format, the entry is dropped from the type
table instead, and the file is simply not media. `.raw` is the counter-case and stays: it is a real
Panasonic and Leica raw extension, so dropping it would hide photographs, which fails the wrong way.

### 4.9 The resolution dissolve

One animation runs per `texture_state`. `_display_alpha_animation` fades an incoming representation in
over the outgoing one, which is held underneath at full opacity and released once the incoming
reaches full alpha. Drawing the old image opaque beneath the new one rather than fading both means
the composite never dips toward the background mid-transition.

`fade_out` captures the last drawn texture and restarts the animation, so **the only dissolve is
between resolutions of one item**: thumbnail to decoded image, embedded preview to development, still
to first video frame. Where there is no last drawn texture there is nothing to dissolve from, so the
first image of an item appears at once rather than fading up out of the background.

Two orderings inside `draw` decide whether that actually holds. The alpha must be read **after** the
texture upload, because the upload is what calls `fade_out` and restarts the animation: read before
it, the value is the *previous* fade's finished 1.0, which draws the incoming texture opaque for one
frame, releases the outgoing texture as though the fade were complete, and leaves every subsequent
frame fading up from the background — the failure this section exists to prevent, wearing the costume
of a working fade. And the outgoing texture is drawn at the incoming image's *current* bounds, not
the bounds it was last drawn at, because a zoom moves those every frame and a frozen copy drifts out
of register for the length of the dissolve.

There is deliberately no item-to-item fade. A new item's thumbnail is on screen at its first draw, so
stepping shows it straight away; carrying the previous item's texture across only to dissolve it away
added a transition the user cannot act on, and made the outgoing `texture_state` load-bearing for the
incoming one's first frame. That trade only holds while phase 1 really is immediate: with no
outgoing image underneath, every gap before the first texture is a visible flash rather than a
hidden one, which is why `seed_placeholder` and the placeholder publish exception in §4.4 are part of
the same decision. The within-item upgrade fade remains the "no flash" requirement of
[zoom.md](zoom.md) §10 and is not optional.

### 4.10 What re-enters the ladder

- **Content changed.** `refresh` compares `_photo_timestamp` against the item's `file_modified` and
  `thumbnail_timestamp`. This is the correct answer for a foreign change and the wrong answer for one
  we made ourselves — see §10.
- **More pixels needed.** Zoom, resize, fullscreen, as in §4.5.
- **Graphics resources released.** Rebuilds from `_loaded` at the phase already reached.

### 4.11 When an image cannot be shown

The ladder can run out of room, and the media area says so rather than leaving an empty rectangle.
`texture_state::_display_problem` records which of two things happened, and it is cleared whenever
fresh content is adopted, so the message never outlives the item it describes.

| State | Cause | Message |
|---|---|---|
| `too_large` | The estimated decode exceeds `df::max_decode_bytes`. The request is never queued. | `Too large to display` |
| `failed` | A queued decode returned nothing, or the texture upload failed. | `This image could not be displayed` |

Both are drawn centred in the media bounds with the source dimensions beneath, so a user who sees
`Too large to display / 32768 x 20000` can tell it is the file's size and not a corrupt file or a bug.
The pixel dimensions also stay visible in the properties strip, because they come from the header
scan and never needed a decode.

The estimate is JPEG-aware: libjpeg reduces by up to 1/8 while decoding, so a JPEG never materialises
its full frame and is judged on what it will actually allocate. Every other deferred codec decodes at
native size first and is judged on the whole thing. The budgets themselves are owned by
[rendering.md](rendering.md#image-budgets).

PSD, HEIF, JXL and the platform-decoded types (GIF, BMP, TIFF and the rest of the WIC set) decode
during `files::load` rather than on demand, so they cannot be caught by the check above — the
allocation would already have happened. Each refuses at the point it first knows the geometry and
before it allocates anything:

| Format | Where it refuses |
|---|---|
| PSD | `load_psd`, after the 26-byte header |
| JXL | `load_jxl`, on `JXL_DEC_BASIC_INFO` |
| HEIF | `load_heif`, on the primary image handle |
| GIF, BMP, TIFF, other WIC types | `files::load`, from the `scan_photo` header geometry, with `platform::image_to_surface` re-checking after `IWICBitmapSource::GetSize` |

`reject_over_budget_source` performs all four checks and fills a `load_diagnostic`, which
`files::load` turns into `file_load_result::reason` and `source_dimensions`. That is what lets a
54-byte BMP whose header claims 40000 x 30000 report its real size without a byte of it being
decoded. A refusal is never retried, and `complete_load` drops the item thumbnail with it, because
leaving the thumbnail on screen would imply the image itself is what is displayed.

## 5. Writing: how a write runs

`files::update` in `src/files_core.cpp` is the single entry point for changing an existing media
file. It runs on `async_queue::work`, never on the UI thread.

```
no change at all                      -> return OK, touch nothing
metadata-only, in-place eligible (§7) -> patch the destination, return
metadata-only, no embedded XMP        -> stage and swap the sidecar only, return
otherwise                             -> stage -> edit the stage -> back up -> swap -> swap sidecar
```

### 5.1 What each format costs

"Measured" marks behaviour observed directly rather than read from source. Rating cost is the
interesting case because it is the smallest possible edit: where it is O(file), the format cannot
express a small change cheaply.

| Format | Metadata goes to | Write path | Rating cost | Interrupted write recoverable? | Windows | Adobe |
|---|---|---|---|---|---|---|
| MP4 / MOV / M4A / 3GP / CRM | Embedded XMP + `Xtra` | **In-place, including the first write** | **O(1)**, measured | **No** on growth or relocation | R/W, measured | R/W |
| ASF / WMV / WMA | Embedded XMP + `WM/*` | In-place *only if* the packet fits, else staged | O(1) or O(file) | Yes, either branch | R/W | R/W |
| MP3 | Embedded XMP + ID3 `POPM` | Staged | O(file) | Yes | R/W | R/W |
| WAV / AVI | Embedded XMP | Staged; the handler would `XIO::Move` a live file | O(file) | Yes | Partial | R/W |
| JPEG | Embedded XMP + Exif `0x4746`/`0x4749` + IPTC | Staged; a rating always dirties Exif | O(file), small | Yes | R/W, measured | R/W |
| TIFF | Embedded XMP + Exif | Staged | O(file) | Yes | R/W, measured | R/W |
| PNG / WebP / GIF / PSD | Embedded XMP | Staged | O(file) | Yes | Read only | R/W |
| DNG | Embedded XMP | Staged | O(file) | Yes | No write | R/W |
| RAW (CR2, NEF, ARW, …) | **Sidecar `.xmp`** | Sidecar only; the media file is untouched | O(sidecar) | Yes | **Cannot write**, measured | R/W |
| HEIC / AVIF | Embedded XMP | **No `edit` trait — cannot be edited** | n/a | n/a | R/W with extension | R/W |
| JXL | Embedded XMP | **No `edit` trait** | n/a | n/a | No | Partial |
| MKV / WebM | — | **No traits — no metadata write at all** | n/a | n/a | Partial | No |

Three things follow. MP4 and MOV are the only formats where the cost rule is fully satisfied, and
they are also the only ones carrying an unrecoverable interruption window (§7.1). RAW is the cheapest
case of all because nothing touches the media file. And the bottom three rows are gaps rather than
decisions: HEIC in particular means iPhone photos cannot be rated.

Sidecars are invisible to Windows in both directions, measured: Explorer reports a file's *embedded*
values and ignores a sidecar entirely, and writes to the media file rather than the sidecar. Since
Windows cannot open a writable property store on RAW at all, there is no conflict to manage there —
the sidecar is Adobe territory exclusively, and `<name>.xmp` is the Camera Raw and Bridge convention.

### 5.2 The sidecar-only branch

The third branch is what makes the RAW row true. A container that cannot hold an embedded packet
keeps its metadata entirely in `<name>.xmp`, so a rating leaves the media file's bytes identical —
and copying a 60 MB raw to a stage only to swap back the same bytes would charge O(file) for an
O(sidecar) change.

The branch is taken only when the write is metadata-only, the path does not change, and no
`.original` backup was requested; a requested backup still stages, because rule 2 requires the prior
bytes to be kept before anything is replaced. The sidecar itself is still staged and then atomically
replaced, so rule 1 holds. No coherent handle is produced, because the media file was never opened
for replacement — the re-scan inside `files::update` opens by name and reports itself not coherent
(§6).

### 5.3 The staged path in full

1. **Produce the stage.** A pixel edit encodes a new image into a temp file in the destination
   folder. A metadata-only edit copies the source to that temp file. Either way the live file is
   untouched at this point.
2. **Apply metadata to the stage.** `metadata_xmp::update` reads the existing packet from the
   *source*, applies `metadata_edits`, and writes to the *stage*. Reading from the source matters:
   applying edits to an empty packet would discard every property the file already holds.
3. **Capture a rollback copy.** Only when a sidecar swap will follow, because that is the only case
   with two commits to keep consistent.
4. **Swap the media file.** `platform::replace_file` moves the stage into place and returns a
   cache-coherent handle (§6).
5. **Swap the sidecar.** If this fails, the media swap is rolled back from the copy taken in step 3.
6. **Re-scan through the handle.** `files::update` runs the caller's `rescan_spec` here, while the
   handle is still valid, and releases it (§6).

Temp paths come from `platform::temp_file`, which only produces a name. Nothing exists on disk until
written, so an early return leaks nothing.

`platform::replace_file` in `src/platform_win_files.cpp` is a clean-room reimplementation of
`ReplaceFileW` rather than a call to it, for the read-back reason in §6 and because the replacement
must be flushed to the volume before the swap, so network write-behind caching cannot leave a
swapped-in file whose contents have not landed. Creation time is preserved from the destination.
Where the handle path cannot run — a filesystem that rejects rename-by-handle, a read-only target, a
reparse point — it falls back to `MoveFileEx`, which returns no coherent handle.

When an original backup is requested, `<name>.original.<ext>` is created *before* the replacement is
opened or renamed, so a failure leaves both the destination and the stage untouched. An existing
`.original` is never overwritten; the name is uniquified up to 1000 attempts, and exhausting them
fails the operation rather than silently producing no new recovery point.

### 5.4 What a write reads before it decides

`files::update` has to know whether the pixels will be rewritten before it can pick a branch, and for
a bitmap that question reads the source: `image_edits` states a crop as a quad in source pixels, so
only the frame size can say whether the quad crops anything.

That read is skipped where it cannot change the answer. `image_edits::is_empty` is the
frame-independent form of `has_changes`, with the crop test widened from "crops this frame" to
"carries a crop quad at all", so when it holds `has_changes` is false for every possible frame and
there is nothing to scan for. Every metadata-only write takes that path. The saving is not a header
peek: `scan_photo` parses EXIF, IPTC, XMP and ICC and copies out the embedded thumbnail, and its only
other consumer is `save_metadata` on the re-encode branch, which such a write never reaches. So
rating and labelling no longer pay a full parse before the branch that §7 or §5.2 then takes.

Rotate reads the source as well, in `batch_edit_spec::make_edits`, because its crop quad is built from
the frame size. It takes `scan_photo` over a `file_read_stream` rather than `files::load`:
`load_image_file` derives both the dimensions and the stored orientation from that same scan, so the
whole-file read behind `load` was only ever producing bytes rotate discarded. Reading the header
rather than the item's indexed record is deliberate — the crop is baked into the user's pixels, so it
has to come from the bytes being rewritten, not from a record guarded by a timestamp heuristic.

## 6. Read-back coherence on network shares

This is the constraint that shapes the whole write path, and it is why `replace_file` is a
reimplementation rather than a call.

Every successful write is followed immediately by a re-scan, because the index, thumbnail and
displayed properties must reflect the new bytes. On a local NTFS volume a fresh open by name returns
those bytes. On an SMB share it may not: the redirector can serve the pre-swap contents from its
by-name data cache, so the re-scan reads the *old* file, writes the old metadata back into the index,
and the user watches the edit silently revert (#207). Flushing and retrying do not fix this. The
write did land — it is the reader's cached view that is stale, and nothing in a by-name open
invalidates it. The only reliable fix is to never take a second look by name at all.

So the swap is performed *through* a handle that outlives it. The stage is flushed to the volume,
opened with `GENERIC_READ | DELETE | FILE_WRITE_ATTRIBUTES` while sharing read, write and delete,
stamped with the destination's preserved creation time, and then renamed by handle via
`SetFileInformationByHandle(FileRenameInfo)`. The handle does not track the *name* — it continues to
refer to the same file object, which is now the destination — so no by-name lookup happens and there
is nothing stale to serve. The authoritative modified time is read back through that same handle.

Two SMB-specific details are load-bearing. The rename target must be in extended-length form, so UNC
paths are rewritten to `\\?\UNC\server\share\...`; a plain UNC target is rejected with
`ERROR_INVALID_NAME`. And a just-written destination on a share commonly returns a sharing, lock or
access violation while an oplock or lease break completes, so the rename retries with a rising
backoff.

`replace_file` returns the handle as `file_op_result::coherent_handle`, and `files::update` consumes
it before returning: the re-scan happens inside the write, where the handle is still valid, so no
caller ever holds it and no caller ever reopens the file by name. `rescan_spec` says what that
re-scan should produce and `file_update_result` carries it back.

Three readers want the new bytes, and one scan behind the write serves all three.

- **The index** takes `file_update_result::scan` and applies it through `index_state::apply_scan_now`.
  Two details in the consumer matter as much as the handle: it reuses the cached folder node and
  deliberately does **not** refresh the folder from the filesystem first, because a by-name directory
  refresh is the same stale window in a different place; and when the scan was coherent, the modified
  time read back through the handle is stamped as both the file's modified time and the scan and
  thumbnail version, so the later background re-scan finds no work and never opens the file by name.
- **The photo display** takes `file_update_result::loaded`. The scan of a directly displayable format
  already materialises the whole file and wraps it, so surfacing it as `file_scan_result::full_image`
  costs a reference count. `view_state::publish_written_image` adopts it for the matching texture and
  bumps the load generation, so a by-name load already in flight cannot land on top of it. Two items
  can be on screen at once, so `displayed_photo_paths` reports both panes and the write carries one
  written-image record per displayed path; a write to either pane hands over its bytes rather than
  re-reading them by name.
- **The AV display** takes `file_update_result::display_handle`, asked for by `rescan_spec::want_handle`.
  A container can be gigabytes, so it gets the handle rather than the bytes. This is the only way a
  handle leaves `update`; it is moved so exactly one owner holds it, and `view_state` clears it on
  every detach and every release so a superseded reopen cannot hold a file open across the next
  rename. `av_format_decoder::open` seeks a handed-over handle to zero, because the scan left the
  position wherever it finished.

**Coverage boundary.** The scan behind the write covers the primary media stream only. The RAW branch
(`scan_raw`) and the XMP sidecar are still read by path, so a RAW-plus-sidecar edit retains a narrow
stale-read window on a share. Where there is no handle at all — the `MoveFileEx` fallback, or an
in-place patch — the scan still runs inside the write but opens by name, and
`file_update_result::coherent` reports which it was. Callers whose scan failed outright re-scan by
name with `force` set. Forcing solves a different problem, the modified-time tie heuristic missing a
rapid second edit; it does not make a by-name read coherent. That residual window is accepted because
the alternative is refusing to write.

**An in-place patch is coherent without a handle**, for the reason in §2.4: the staleness is caused
by the identity change, not by the write. This reasoning holds only for handlers that truly patch —
JPEG's rewrite path ends in a three-way rename, which is exactly the identity-change condition, and
is one reason it is excluded in §7.

Two residual differences from the staged path remain, neither a correctness bug, both to be
re-checked if the in-place gate widens. The in-place path relies on the handler's close rather than
an explicit flush. And with no handle there is no authoritative modified time, so the forced re-scan
stamps `metadata_scanned` from the *client* clock while `file_modified` later refreshes from the
*server* clock; `needs_scan_impl` compares the two, so a share whose clock runs far enough ahead
would re-scan on every folder validation. Measured skew to the reference NAS is 17 ms, far inside the
window; a poorly synchronised share is the exposure.

## 7. The in-place patch

A metadata-only edit may patch the destination directly, skipping stage and swap entirely. This is
the optimisation behind rule 5 and it is gated, because it trades rule 1's guarantee for speed.

**Eligible when all hold:** no path change, no pixel edits, no backup requested, the format carries
`file_traits::in_place_metadata`, and — unless it also carries `file_traits::in_place_metadata_inject`
— `metadata_xmp::has_embedded_xmp` reports an existing embedded packet.

Each exclusion is load-bearing. A save-as or a pixel edit must produce a second file. A backup needs
the prior bytes kept.

The two traits exist because this branch does not itself perform a bounded write: it hands the *live*
file to the toolkit and lets the handler choose. "A packet exists" is a proxy for "the handler will
patch rather than rewrite", and the two come apart badly, so the gate is an allowlist of containers
whose handler behaviour has been read and tested. `in_place_metadata` means the handler never
destroys the live file; `in_place_metadata_inject` means a first packet can be absorbed boundedly too,
so no existing packet is required. §7.2 records what each admitted container does and why the rest
are excluded.

`metadata_xmp::update` deliberately does not pass `kXMPFiles_UpdateSafely` to `CloseFile`, because
`XMPFiles::CloseFile` takes the `DeriveTemp` branch for any handler that does not own the file, so
safe update *always* copies the whole file — exactly the cost an in-place caller is avoiding, and
redundant on the staged path. Choosing between a patch and an atomic replace belongs to
`files::update`, not to the metadata layer.

`has_embedded_xmp` is cheap enough to run per write — measured warm in Release, 294 µs on an MP4,
384 µs on a MOV, 761 µs on a JPEG, and 0 µs when the format trait excludes it outright, because the
smart handler seeks structure rather than reading the file. It is deliberately **not** cached:
`metadata_parts::xmp` is also populated from sidecars, so a non-empty packet does not mean an
*embedded* packet, and a stale value would select the wrong branch.

A patch that loses a race with a reader is retried once. Probing for write access and then writing
was rejected, because the probe closes its handle and a reader can take the file in between. Instead
the patch is retried after it actually fails, and only when `platform::wait_for_unlocked_write`
confirms the file is merely locked — five probes with escalating sleeps, about 500 ms worst case,
giving up immediately on any error other than a sharing or lock violation, so a read-only file or a
permissions problem still fails at once. If the retry also fails the exception propagates and the
failure is reported as before.

### 7.1 Rollback gap

`has_embedded_xmp` proves a packet *exists*, not that the new one *fits*. When it does not fit, the
handler takes a fallback that Diffractor cannot roll back, because `files::update` staged nothing to
roll back to. What the fallback costs depends on the container, and the allowlist is what keeps that
cost survivable:

- ISO base media falls back within the container, and every fallback in §7.3 is bounded.
- ASF falls back to `SafeWriteFile` — `DeriveTemp` + `WriteTempFile` + `AbsorbTemp`. The live file
  survives intact until a three-way rename, so no bytes are at risk, but the file's identity changes
  outside Diffractor's control and no backup is taken.

In practice the fallbacks are rare: the toolkit serializes with `kXMP_ExactPacketLength` against the
old length, so ratings, labels and tags fit by construction, and a rating is a fixed-size ASF legacy
field so `legacyGrows` stays false. Only metadata *growth* past the original packet reaches a
fallback.

Closing the gap means sizing the rollback to the write rather than to the file. Retaining the old
packet bytes is **not** a sufficient way to do that — the write is not confined to the packet, and
[v-next](v-next.md) records why a packet copy is unsound and what a write journal would have to do
instead.

### 7.2 What the gate admits

`file_traits::embedded_xmp` is carried by MP3, WAV, AVI, ASF, JPEG and many more, so before the
allowlist all of them reached the in-place branch once they held a packet. What the handler then does
to the live file differs completely, which is why the trait could not be the gate:

| Format | Handler behaviour on the live file | Admitted |
| --- | --- | --- |
| MP4 / MOV / M4A / CRM | Bounded box patch, no relocation, bounded even with no packet (§7.3) | Yes, with inject |
| ASF / WMV / WMA | Patches an existing packet that fits; otherwise `SafeWriteFile` stages a temp and swaps | Yes, packet required |
| JPEG, edit dirties EXIF or PSIR | `DeriveTemp` + `WriteTempFile` + `SwapData` three-way rename | No |
| MP3 | `XIO::Move` shifts the entire audio payload within the file, no temp | No |
| WAV / AVI | `XIO::Move` likewise; `WriteTempFile` throws | No |

The MP3 and RIFF cases are why the allowlist exists. `MP3_MetaHandler::UpdateFile` throws on safe
update, so it has no temp-file mode at all: when the tag grows it moves the whole payload inside the
file it was given. Interrupt that — a dropped SMB connection, a power loss — and the media is
destroyed with no recovery. Excluded from the branch, the identical `XIO::Move` runs against a stage,
so the live file is untouched until the atomic swap.

JPEG is excluded because the branch costs safety for nothing. A rating always dirties EXIF and a tag
always dirties the PSIR (§7.4), so the handler always rewrites; handing it the live file merely moves
that rewrite off a stage and onto the user's file, and takes Diffractor's backup and rollback out of
the picture.

ASF is admitted but is not MP4-class, and the distinction is the reason it does not carry the inject
trait. `ASF_MetaHandler::UpdateFile` patches only when the new packet fits the existing one and the
legacy fields do not grow; otherwise it takes the same `DeriveTemp`/`AbsorbTemp` route as JPEG. That
route is safe — the live file is intact until the rename — and a rating is a fixed-size legacy field,
so ratings patch in practice. With no packet at all the fallback is certain, so requiring one keeps
the common case bounded and sends the first write to the staged path.

### 7.3 Why MP4 and MOV are always bounded

`MPEG4_MetaHandler::UpdateTopLevelBox` has six branches and **none relocates `mdat`**: same-size
in-place write; box already at EOF so write and truncate; shrink by 8 or more and cover the remainder
with a `free` box; absorb an adjacent `free` box; use any sufficient `free` space elsewhere in the
file; otherwise append at EOF and wipe the old box to `free`. Cost is proportional to the `moov`
subtree plus the packet, never to the file, and there is no `DeriveTemp` on this path.

Because this holds even with no existing packet, MP4/MOV carries `in_place_metadata_inject`: the
first rating on a camera video patches in place rather than staging a full copy. Appending grows the
file by the `moov` size and de-optimises faststart layout, but this is self-limiting — the vacated
`moov` becomes a `free` box that a later write reuses. `CheckFinalBox` can throw on a final box
larger than 4 GB without a preceding `wide` atom, which falls back to staging.

The branches are layout-dependent, so this needs testing per layout rather than once. Faststart
files, where `moov` sits ahead of `mdat`, are the awkward case, and §12 names the two corpus files
that cover both layouts.

### 7.4 Why JPEG always stages

`JPEG_MetaHandler::UpdateFile` patches in place only when the file already had a packet, the new
packet fits the old length, there is no extended XMP, and neither the EXIF nor the PSIR block
changed. APP1 sits at the front of the file, so injecting a first packet must shift everything after
it — the first write is unavoidably a rewrite. After that rewrite the toolkit's 2048-byte pad usually
satisfies the size condition, so the legacy conditions decide: a rating dirties EXIF and tags, title
or caption dirty the PSIR. Only XMP-only fields such as label avoid both, and extended XMP blocks the
path permanently because the merged packet cannot fit the old length.

**A fork patch here was considered and rejected.** The gate starts from `fileHadXMP`, so a camera
JPEG's first rating rewrites regardless, and on that same file the rating tag does not exist yet, so
inserting it grows the IFD. The patch would turn N rating edits on one file into one rewrite plus
N−1 patches — nothing for the import-and-cull pass that dominates the cost, in exchange for changing
the write path of the most common format in the collection. Measured on an untouched Pixel photo,
both routes are shut: the EXIF APP1 is 100% occupied with zero slack and both rating tags absent, and
the XMP packet has no `<?xpacket?>` wrapper and declares 68 KB of extended XMP. Phone photos of this
shape cost a full rewrite per rating and cannot be optimised app-side.

### 7.5 Why MP3 and RIFF are excluded

`MP3_Handler` sets `mustShift` and calls `XIO::Move(file, 0, file, newTagSize, filesize)`, moving the
entire file, because ID3v2 lives at the front. It also shifts when metadata *shrinks* by more than
8 KB. `RIFF_Handler` likewise uses `XIO::Move`, and its `WriteTempFile` throws outright. Neither
declares `kXMPFiles_CanRewrite`, and `MP3_MetaHandler::UpdateFile` throws on safe update.

These must only ever run against a stage, which is why the §7 allowlist excludes them.

### 7.6 Orientation is the one EXIF value that could be patched directly

§7.4 rejects a *toolkit* fork patch for JPEG, and that rejection turns on growth: the rating tags are
absent on an untouched camera JPEG, so writing one grows IFD0, and IFD0 sits inside APP1 at the front
of the file, so everything after it shifts. Orientation does not have that problem, and it is the edit
the culling pass performs most.

EXIF orientation is a `SHORT` with a count of one, so its value lives inline in the 12-byte IFD entry
rather than in the data area after it. Changing it is a two-byte overwrite at a fixed offset: nothing
grows, nothing relocates, no packet has to fit. The offset is already derived while reading —
`files_scan_photo.cpp` takes the value at entry offset `+8`, and `file_scan_result::exif_file_offset`
records where the EXIF block starts in a JPEG — so a patch needs the scan to *retain* that position,
not to discover anything new. It would also be a Diffractor-owned bounded write rather than a live
file handed to a toolkit handler, so §7.1's rollback gap does not arise: there is no fallback branch
to take, and a same-size two-byte write inside one sector has no partial outcome to roll back to.

Two conditions gate it, and the second is why this is not merely an optimisation.

- **The XMP packet must not carry `tiff:Orientation`.** `file_scan_result::to_props` parses EXIF, then
  IPTC, then XMP, so an XMP orientation overrides the EXIF value. Patching EXIF underneath one would
  change what is drawn — `scan_photo` reads the EXIF value — while the reported property stayed stale,
  and the two would disagree permanently. The write is only sound where EXIF is the sole holder.
- **It changes what rotation means.** Today rotate rewrites the pixels and normalises the tag to
  `top_left`; because `quadd::transform` reorders a quad's corners rather than moving them,
  `is_no_loss` holds and a JPEG takes the lossless DCT transform, so the current cost is a whole-file
  rewrite, not lost quality. A tag-only rotate would leave the pixels in their stored arrangement and
  rely on every other reader honouring the tag. That is a product decision about what a rotated file
  is, and it belongs in [design](design.md) before it belongs here.

Rating cannot use this route: §7.4 measured the tag as absent on a camera JPEG, so writing one grows
the IFD, and a rating is normally an XMP property in any case. Label is XMP-only and never had an EXIF
value to patch.

## 8. Sidecars

Sidecar writes follow the same staging discipline. `metadata_xmp::update` writes the sidecar beside
the file being written — the stage, not the live destination — and the live sidecar is replaced only
after the media swap succeeds.

The sidecar path is `<name>.xmp` unless an explicit name is supplied. Formats without the
`embedded_xmp` trait always take this path; the workflow states which destination applies. When such
a format is edited without a pixel change, path change or backup, the sidecar is the *only* thing
written — see §5.2.

Across the sidecar swap the media file's coherent handle and modified time are preserved explicitly,
because `replace_file` returns its own result for the sidecar and would otherwise clobber the handle
the re-scan in §6 needs.

## 9. Failure and rollback

| Failure point | Outcome |
| --- | --- |
| Stage cannot be produced | Nothing touched; stage deleted |
| Metadata write to stage fails | Nothing touched; stage deleted |
| Backup cannot be created | Nothing touched; operation fails |
| Media swap fails | Destination unchanged; stage deleted |
| Sidecar swap fails | Media restored from the rollback copy, or deleted if it did not previously exist |
| In-place patch fails | Retried once while merely locked (§7); **no rollback** thereafter, with the allowlist bounding the damage to a bounded box write or a temp-and-swap — see §7.1 and §7.2 |

Every exit deletes the stage and the rollback copy. Errors are reported concretely: a locked or
read-only file, a format that cannot store the metadata, and a format with no handler are
distinguished rather than collapsed into one opaque failure (#231).

`files::update` converts every exception into a failed result rather than propagating one, which is
what makes the claim release in §3.3 unconditional.

Cancellation stops future work. It never claims to undo completed changes. A check that cancellation
interrupted proves nothing, so the row it was checking is reported as canceled rather than as a file
that changed.

### 9.1 Guided operations revalidate a row, not a plan

Import and Sync execute a plan the user reviewed. Between Review and Run the files can move on, so
each row is held to the files it named, checked immediately before that row acts:

| What the row does | What it must prove |
| --- | --- |
| Reads a source | The source still matches the reviewed size and modified time |
| Writes where the review found nothing | Nothing — the write itself is `fail_if_exists`, so the file system decides atomically and there is no window in which a file could appear |
| Writes over a file the review found | That file still matches the reviewed size and modified time |
| Deletes | The target still matches the reviewed size and modified time, and still has the reviewed CRC |

A failed query is never read as absence (see `platform::file_presence`): denied, offline and
unreachable all count as changed, because a run must not write over or delete a file it could not
look at.

A row that fails its check writes nothing and is reported as failed; the run states that files
changed after analysis alongside its counts, and the rows that still match still run. The alternative
— re-deriving the whole plan and discarding the run if anything anywhere differs — costs a second
full scan of both sides on every Run, which is the dominant cost against a network share or a camera
card, and turns one changed file into a wasted run.

Rename keeps its own pre-pass over the reviewed rows and its whole-run abort.

## 10. Change response

A change to a file has to reach three consumers: the index record, the thumbnail, and the displayed
image. How it reaches them depends on provenance (§2.3).

### 10.1 The problem this solves

Every consumer used to discover a change independently, by polling a timestamp, and the folder
watcher responded by re-running the whole search. Three problems followed.

1. **The signal was too coarse.** "The modified time moved" was the only fact available, so every
   consumer had to assume the worst. A rating change rewrites one XMP packet and leaves every pixel
   identical, yet it invalidated the thumbnail and the full-size image and caused both to be decoded
   again.
2. **Nothing distinguished our own writes from anyone else's.** The app could not suppress the
   refresh it caused, because it could not tell it apart from a change made by a sync client.
3. **The consumers raced the writer.** The rescan of edit N held the file when the write of edit N+1
   arrived, and the write failed with a sharing violation.

The first problem caused the second and third: fixing the signal removed most of the work that raced.

### 10.2 Self-inflicted metadata writes suppress the reload

The general design is that a write returns its change class and the UI applies one bounded batch —
publish the new metadata snapshot, then restamp `_thumbnail_timestamp` and `_photo_timestamp` to the
new modified time for a `metadata` class, leave the stamps behind for a `pixels` class so the ladder
re-enters. Restamping is the whole mechanism: both existing polls stay exactly as they are, which
keeps them correct for foreign changes, and after a self-inflicted `metadata` write the stamps
already agree with disk so the polls find nothing to do.

What is implemented is a narrower form of that. The **caller** derives the class from the edits it is
about to apply, rather than reading it back off the write, because it can do so before the write
starts and without waiting for it. It has to: the per-item edits of a batch are produced on the
worker, so the decision is stated up front as `batch_edit_spec::changes_presentation`. Rotate sets it
unconditionally; the metadata overload of `view_state::modify_items` derives it from
`metadata_edits::changes_presentation`, which is sound there because that overload never applies
photo edits and never changes the path. When the flag is clear, before the write starts, on the UI
thread, `modify_items`:

- arms `df::item_element::retain_thumbnail_across_next_write` on every item, and
- calls `texture_state::mark_visuals_current` on the displayed textures.

Both are latches, not stamps, and for the same reason: the value they would have to stamp does not
exist yet. The new modified time is produced by the write, and the only clock available before it
starts is the client's, which is neither the file's clock nor — on a share — the same machine's. Each
latch is therefore armed empty and consumed by the first update that carries the write's own modified
time. `item_element::update` consumes its flag only on the update that actually carries that time, so
an unrelated republish landing first cannot disarm it and let the reload happen anyway;
`texture_state::refresh` consumes its flag by adopting the item's own stamp as `_photo_timestamp`
instead of reloading. When the write hands back its image,
`texture_state::publish_written_image` stamps `_photo_timestamp` from the modified time
`replace_file` read back through the handle, so every comparison stays inside the file's clock domain.
Ordinary loads stay inside it too: `load_image` stamps from the item's own file version
through `item_version_stamp`, the same expression `refresh` compares against. Stamping the client
clock there would make every load on a share whose server clock leads the client look instantly stale,
reloading the texture on every tick until the client caught up.

The result is that a rating change costs no thumbnail decode and no image decode. When the write does
change what is drawn — a rotation, or any edit that changes orientation — the reload is unavoidable,
but it costs no read: the write is asked for its image
(`rescan_spec::want_image`) and the display adopts it. On a playing video it costs exactly one
session cycle: tear down, write, reopen at the retained position — over the handle the write hands
back — with the metadata beneath the video updating from the published record.

### 10.3 Foreign changes are detected by comparison, not by refresh

Nothing about the timestamp polls is removed, because they are the correct mechanism for a change we
know nothing about.

The folder watcher cannot be filtered by provenance: `FindFirstChangeNotification` signals that
*something* in the folder changed and carries no path, so a notification cannot be attributed to a
file we wrote. Suppression by attribution is not available. Suppression by *outcome* is, and it falls
out of publishing the write into the index.

The watch is per folder, so although the notification carries no path the *folder* is known. It is
retained beside the handle and passed to `app_frame::folder_changed`, which accumulates the folders
that signalled into a set and stamps the time. `tick` waits one second for the burst to settle — sync
clients and batch tools report many changes in quick succession — then flushes the whole set once to
`index_state::queue_validate_changed_folders`, which runs `validate_folder` over them on the
`scan_folder` queue and raises `refresh_items` only if something actually differed.

`validate_folder` compares names, modified times, sizes and cloud status against the index from a
plain `iterate_file_items` enumeration. It opens no files, so this response cannot itself collide
with a writer. Additions, removals and subfolder changes are all covered by the same comparison, so
nothing that used to be detected stops being detected. Because the writer has already published the
new modified time and size, a change we made ourselves costs one enumeration and nothing more.

One benign race remains: if the comparison runs between a write reaching disk and its new modified
time being published, it finds a difference and refreshes redundantly. That is wasted work, not wrong
behaviour, and the settle window makes it unlikely.

### 10.4 The layers that removed the sharing violation

In order of how much each removed:

1. Metadata-only writes stop causing reads at all (§10.2), which eliminated the dominant source.
2. The write claim stops new reads being queued for a file being written (§3.3).
3. In-flight reads and genuinely concurrent external access remain possible, so the in-place patch
   retries once when it loses the race (§7).

The retry belongs on the **write** side rather than the read side: a failed read leaves the display
briefly stale, whereas a failed write loses what the user asked for and is the failure actually
reported. It runs on a worker; the UI thread never waits.

## 11. Toolkit constraints

The vendored XMP toolkit is a fork (`third-party/xmp`, `diffractor` branch).
[Third-party](third-party.md) owns provenance and [v-next](v-next.md) owns the full divergence list.
Only the write-relevant constraints belong here:

- Upstream `MPEG4_Handler::ExportXtraTags` marks the box tree changed even when the rebuilt `Xtra`
  box is byte-identical, forcing a `moov` relocation and a whole-file rewrite of a large movie on
  every save. The fork returns early on an identical box. Without this, §7.3's bounded-write claim
  does not hold in practice.
- Upstream `WEBP_Handler` rebuilds the RIFF container by chunk *category* and truncates, overwriting
  image or alpha data in any file whose physical chunk order differs. The fork preserves original
  order. `Should preserve webp chunks on metadata save` guards it.
- `ExportTIFF_WindowsEncodedString` must not delete an XP* tag merely because the property is absent
  from XMP. Clearing keywords is expressed as an empty `exif:XPKeywords` value, not a deleted
  property.
- The ASF Extended Content Description rewrite emits 16-bit length fields, so oversized values are
  dropped rather than written with a truncated length.

## 12. Validation

Write behaviour is covered by `src/test_media_edit.cpp`. The load-bearing cases:

- An unchanged file is not rewritten.
- A metadata edit on an allowlisted container patches in place: not staged, no staged files left
  behind, unchanged file size, and the value round-trips through a re-scan. `file_update_result::staged`
  is the assertion that proves no copy happened — it reports whether `replace_file` ran, which is the
  only thing that stages and swaps.
- A first edit on ISO base media with no packet still patches in place, which is what the inject
  trait claims. Covered for both top-level layouts, because the handler branch differs between them:
  `indy.mp4` for `moov` last, and `anamorphic.mp4` for faststart with no absorbable free space
  (§7.3).
- A metadata edit on MP3, WAV, AVI or JPEG stages: `staged` *is* set. This is the negative of the
  same assertion and is what keeps the §7.2 hazards off the live file.
- A metadata edit on a raw file writes the sidecar and nothing else. The raw's size, modified time
  and bytes are all asserted unchanged, and `staged` is false. Size alone would not catch a
  same-size copy-and-swap, which is exactly what the §5.2 branch exists to remove.
- A staged replace returns a coherent handle and reading it back yields the bytes just written
  (`src/test_utils.cpp`).
- WebP chunk order survives a metadata save.
- Sidecar collision handling under the guided-operation grammar.

A change to write strategy must add a test that asserts the *cost* — file size unchanged, or whether
the write staged and swapped — not merely that the value round-trips. Round-tripping passes on
every strategy and so proves nothing about which one ran.

A test that reads back what a write produced must take the write's own scan, via `ff_scan_after_update`
and `ff_inspect_rescan` in `src/test_utils.cpp`. Re-opening the destination by name is the stale read
§6 exists to remove, so a test that does it is not testing what the app does — and over SMB it fails
intermittently, which reads as a lost edit rather than as a test that asked the wrong question.

The detach/reopen currency rule in §3.5 is covered separately by `Should reject superseded av
session` in `src/test_index.cpp`, against `display_state_t` rather than the player: `av_player` has
no thread of its own, so a test that drove `open`/`close` could only wait on an unpumped queue. The
shared detachment window is covered by `Should keep file handles detached until last operation`.

A per-run summary of file operations is logged at exit, broken down by file type and by operation —
reads, in-place patches, replaces, sidecar writes and write failures. It is the cheapest way to
confirm that a change to §7 actually changed which branch runs:

```
mp4        inject    reads=35      in-place=7      replace=7      sidecar=0      write-failed=0
```

Everything in this document was last validated on x64 Debug with `.\dd.ps1 test` and, because the
write path is in scope, `.\dd.ps1 bean` against a real SMB share — both green.

**Network validation is mandatory for write-path changes.** Read-back coherence (§6) cannot be proven
on a local volume: NTFS returns fresh bytes from a by-name open, so a broken coherent-handle path
still passes every local test. `dd bean` runs the whole suite with
`/test-temp:\\bean.local\home\tmp`, placing every file-I/O test's scratch work on a real SMB share.
A change to §5, §6, §7 or §8 is not validated by `dd test` alone.

`/test-temp:<path>` is the underlying switch and accepts a filter alongside it, so a single group can
be targeted while iterating:

```
.\exe\diffractor64-d.exe "/test-temp:\\bean.local\home\tmp" "/test:*in place*"
```

## 13. Deferred ideas

### 13.1 Writing

- **Seed `_loaded.i` from writer-supplied bytes for `pixels` writes.** When a write produced a whole
  file, the reload could decode from memory and never open the file. `file_load_result` already
  retains the encoded image alongside the decoded surface, so this is the representation the pipeline
  expects. It affects the photo-editor save path only, which is why it was not needed to close the
  reported bug; it is a cost optimisation, not a correctness fix.
- **Return the change class from `files::update`** rather than having the caller derive it (§10.2).
  Required for `pixels` and `identity` writes, and blocked on the in-place path having no coherent
  handle to compose a snapshot from.
- **Rating cost is O(file) for every format except MP4, MOV and the sidecar-only formats.** Inherent
  for JPEG (§7.4), but merely unimplemented for large AVI and WAV.
- **A two-byte in-place EXIF orientation patch would make rotation O(1) on JPEG** (§7.6). Gated on the
  XMP packet holding no `tiff:Orientation`, and blocked on the design question of whether a rotated
  file keeps its stored pixel arrangement.
- **The last three rows of §5.1 cannot be written at all**, of which HEIC matters most because it
  means iPhone photos cannot be rated.
- **A write journal** would size rollback to the write rather than to the file and close §7.1.
  [v-next](v-next.md) owns why a packet-copy rollback would be unsound.

### 13.2 Reading

Deferred beyond version 1.27. The implemented viewport-bounded rendering, cancellation and
cache behaviour is sufficient for that release; these do not block it.

- **Native decode still tracks the source.** Paint submits only the visible destination and both
  renderers sample only the clipped viewport, so frame cost is viewport-bounded. The retained decode
  is still a whole-image representation, however. Baseline JPEG decoding can be interrupted between
  scanline chunks, but it does not yet use `jpeg_crop_scanline` and `jpeg_skip_scanlines`; other
  codecs retain their full-decode fallback. A correct JPEG tile path must carry the tile's source
  origin through `surface`, texture upload, both backends, fallback drawing, navigator ownership,
  cancellation, and cache accounting.
- **Codec cancellation is capability-based.** JPEG checks the detached cancellation token between
  scanline chunks and aborts publication. Codecs without an owned incremental callback cannot be
  interrupted inside their library call; their stale result is still rejected by request generation.
- **Nothing is read ahead.** Each displayed image is loaded and decoded on demand at the size the
  display asks for, and the recent-texture cache described in [4.7](#47-lifetime) avoids immediate
  repeat work when stepping back. It retains by form rather than by recency, so what remains
  budgeted is only the decoded pixels of formats with no compressed representation. Eviction within
  that remainder is still by recency and does not yet rank candidates by viewport distance.
