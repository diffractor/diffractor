# Diffractor Implementation

This document owns durable architecture and engineering constraints. Source code is authoritative for exact APIs, enums, and file lists. Product behavior belongs in [design.md](design.md); build prerequisites and dependencies belong in [README.md](../README.md).

## Primary design drivers

[design.md](design.md#primary-design-drivers) owns the two co-equal drivers. Architecture and implementation must preserve them:

1. **Fast, lightweight performance.** Keep startup, navigation, search, and viewing responsive; bound memory and background work; avoid unnecessary allocation, I/O, decoding, database access, and network use; publish progressive results; and measure changes on representative large collections when performance could move.
2. **A clear user mental model.** Represent scope, contents, focus, selection, command target, effect, recovery, and operation state consistently across views and command entry points. Do not use implementation shortcuts that introduce hidden targets, stale state, ambiguous ownership, or behavior that depends on an undisclosed context.

Performance work must preserve exact behavior, and behavior changes must account for latency and resource cost. When the drivers conflict, document the tradeoff and choose the simplest design that keeps both understandable and responsive.

## System shape

Diffractor is a C++20 Windows application with a custom Direct2D/Direct3D 11 UI. SQLite persists indexed metadata and thumbnails. FFmpeg handles audio/video; specialized libraries decode images; EXIF, IPTC, XMP, and container handlers read and write metadata. The database is a rebuildable cache, not the authority for media or user metadata.

## Source organization

| Prefix | Responsibility |
|---|---|
| `app*` | Frame, commands, settings, workers, text, sidebar, toolbar. |
| `av*` | Audio/video decoding, playback, sound, visualization. |
| `files*` | Format detection, scanning, loading, saving, codecs. |
| `metadata*` | EXIF, IPTC, XMP, ICC, metadata reconciliation. See [metadata](metadata.md) for the tag mappings. |
| `model*` | Items, search, index, database, properties, tags, locations. |
| `platform*` | Windows UI, filesystem, graphics, audio, settings, networking. |
| `render*` | Surfaces, color processing, image transformations. |
| `ui*` | Platform-independent controls, layout, drawing, controllers. |
| `view*` | Items, media, edit, import, locate, rename, sync, batch, tags, plus the shared list, map, and selector renderers. |
| `util*` | Strings, paths, dates, geometry, containers, compression, helpers. |
| `test*` | Unit and regression tests. |

Prefer the owning module over new cross-cutting helpers. Keep source `// Purpose:` comments accurate.

## State, views, and items

`view_state` owns scope/query, raw and displayed results, groups, selection, focus, display state, navigation history, playback, and the current view. `view_state::view_mode` is the accessor for `view_type`; the user-facing names of those views are closed by [design.md](design.md#naming-views-modes-and-presentation-choices). `app_frame` coordinates commands, windows, workers, and invalidation.

Items is the only windowed browser view. It owns the thumbnail/detail browser, media preview, splitter, and independent scroller state. Fullscreen is drawn by the persistent internal Media view renderer, selected by `app_frame::view_changed` only while fullscreen is active; leaving fullscreen restores Items without reconstructing logical browsing state. Graphics resources may be released independently.

Every windowed view shares app-owned native top chrome. The measured app logo and title are at the left. Items navigation and address controls occupy the middle with application window controls at the right. In task views, a right-aligned task toolbar replaces those window controls and ends with a separated Maximize/Restore and task Close group; task Close returns to Items rather than exiting the application. Fullscreen hides this chrome. Edit, Locate, Tags, and the metadata batch task place their selector renderer below the primary renderer; the task controls panel remains beside both. `app_frame::selector_strip_for_view` is the single test for whether a view shows the strip, which items it offers, and what a click on it does, so layout, filtering, and selection cannot disagree. The strip is a peer frame, so it invalidates its own paint and controller rather than relying on the main view frame, and it releases its items when deactivated so item visibility and the thumbnail queue belong to the view that is on screen. Persistent task status and progress render inside the shared primary renderer, while its centered transient status overlay remains reserved for drag/drop and similar immediate feedback. Items owns only its lower browser filter/tool strip, not the window address or window controls.

UI uses `view_element` objects. One `view_controller` owns an active pointer gesture until completion or cancellation. Background work publishes small results and requests coalesced UI invalidation.

A host's window is not a proxy for the host. A `view_host` exists before it is attached and after `on_window_destroy`, and it is populated, counted, ticked and invalidated across both windows, so `view_host::frame()` never returns null: an unattached or destroyed host answers `ui::no_frame()`, a stateless stand-in whose operations are no-ops and whose answers all mean "less work" — occluded, invisible, unfocused, empty bounds, cursor outside every client rect. `view_host` and `view_scroller` dereference `frame()` unconditionally, and so may any caller. The rule is mechanical: a raw `_frame->` outside a `frame()` accessor is the defect, and a member guarded at some call sites and dereferenced at others is that defect already.

The sidebar is the reason. It borrows the Items view frame through `sidebar_host::attach_embedded`, and that attachment is unconditional: `setting.show_sidebar` decides only whether `sidebar_host::layout()` and `invalidate()` ask the shared frame for work, not whether the sidebar has a handle at all. Binding the handle to visibility instead is what shipped a startup crash in 1.27.0 — the hidden sidebar kept populating, counting and enumerating drives against a frame that was never created. `view_controls_host` holds the same shape for its `_dlg`, which is a `ui::control_frame_ptr` and therefore has no stand-in; that one stays explicitly guarded.

The view a frame draws follows the same rule from the other direction. `view_frame::_view` is assigned in the `app_frame` constructor and its setter ignores a null, so it is non-null for the whole life of the frame; `init` creates both frame windows before the first `view_changed` runs, and most of `view_frame`'s handlers dereference `_view` without testing it. `app_frame::_view` cannot be pre-assigned the same way — `view_changed` compares against it to decide whether to activate — so the one handler that a timer can deliver into that gap, `app_frame::tick`, tests it instead.

Search results are filtered, grouped, sorted, and laid out. Group rebuilds reuse `item_group` objects by `group_key`, then update members, labels, and ordering. Items maintains a near-viewport working set, stages available thumbnails, batches SQLite reads, and queues missing local or offline thumbnails. Visibility lets obsolete cloud work stop after scrolling away.

## View invalidation

`view_invalid` is a coalesced, cross-thread request to make derived application state current; it is not an event log. Producers atomically add flags, and `app_frame::complete_pending_events` consumes them on the UI thread. Handlers must therefore be idempotent and must read current state rather than assume that every intermediate transition is observed.

Invalidation layers are disjoint responsibilities. Let $S$ be Source-work functions and $P$ be Paint-layer functions. The invariant is $S \cap P = \varnothing$. Source work may publish a small result and request downstream invalidation, but Paint may only consume current model state and geometry to invalidate a frame or bounds. A function that belongs to or touches the Paint layer must never access SQLite, call a SQLite-access helper, scan files, or call a file-scanning helper. If paint reveals stale source data, it must request the owning Source-work flag; it must not refresh that data itself.

Invalidation has four layers:

1. **Source work** refreshes authoritative or cached inputs: index roots, search items, listed-item metadata, presence, predictions, and index summaries.
2. **Derived state** rebuilds groups, selection summaries, media elements, command state, address text, sidebar contents, tooltips, and comparison state.
3. **Geometry and interaction** lays out the application or active view, restores focus visibility, and rebuilds the pointer controller after geometry changes.
4. **Paint** invalidates only the affected frame or bounds when model state and geometry are already current.

A producer requests the highest layer made stale, using the narrowest flag that fully describes that stale state. The handler that rebuilds a layer owns its unconditional downstream dependencies: for example, media-element changes request view layout, redraw, and controller rebuild; group changes update selection and view elements before requesting view layout and controller rebuild. Conditional siblings remain explicit at the call site, such as command state, presence, or sidebar summaries.

The drain order is a dependency order, not an arbitrary list. Source and derived work run before geometry, controller rebuild runs after layout, and paint observes current state. A handler or worker may add flags while a pass is running; those flags may be consumed later in the same pass or on the next idle pass, so correctness must not depend on synchronous completion. Expensive work stays on its owning queue and publishes only small current-generation results before invalidating their UI consumers.

Review invalidation changes by checking both failure directions: omitting an upstream flag can leave visible state stale, while requesting a broader source-work flag can restart scans, searches, or index work unnecessarily. New flags require a single stated owner, a defined layer, explicit downstream dependencies, and a focused test where practical.

## Async execution

Filesystem, database, decoding, indexing, hashing, querying, thumbnails, maps, web, and other expensive work runs on its owning queue. Results cross threads as moved values or immutable snapshots and apply on the UI thread.

Execution contexts are mathematically disjoint. Let $U$ be work executed on the UI thread and let $E = I/O \cup Decoding \cup DatabaseAccess \cup NetworkUse$. The invariant is $U \cap E = \varnothing$. UI-thread code may schedule work in $E$ and may apply a moved value or immutable snapshot after that work completes; it may not execute any member of $E$ directly. No convenience path, synchronous fallback, callback, or invalidation handler may weaken this boundary.

- Never perform I/O, decoding, callbacks, sorting, or large allocations while holding an index lock.
- Prefer cancellation and replacement of stale work over accumulation.
- Keep SQLite access on its queue and batch writes in transactions.
- Use dedicated queues for stateful or memory-heavy decoders.
- Make user-operation cancellation operation-scoped and always publish progress and final state.

`state_strategy` abstracts model-to-UI coordination. `async_strategy` abstracts UI, database, tile-database, location, media-preview, and named worker queues. Tests use null strategies to exercise model behavior without a window or workers.

### SQLite connection ownership

The vendored SQLite is built `SQLITE_THREADSAFE=2` (multi-thread). Global state is serialized; a *connection* is not. Each connection therefore belongs to exactly one thread for its whole life, and there are two: the index database on the database thread (`queue_database`), and the map tile store on the tile-database thread (`queue_tile_db`). Both stamp their owning thread on open and assert it on every entry point, which is a debug-build tripwire rather than a runtime guarantee — the real protection is that neither type is reachable except through its queue. Do not add a third connection without a thread of its own, and do not call a database method from any other context.

The two stores share the app cache-data folder but are separate files so that neither can condemn the other: `diffractor-cache.db` holds the index, thumbnails, import history and web cache, while `map-tiles-cache.db` holds nothing that cannot be downloaded again and is replaced outright when it cannot be read.

### Synchronized-type registry

Mutable objects have one owning execution context by default. The following types are the approved exceptions because they implement execution infrastructure or coordinate state that is inherently consumed concurrently:

- Execution infrastructure: `platform::mutex`, `platform::thread_event`, `platform::queue<T>`, `platform::task_queue`, `platform::threads`, `platform::memory_pool`, `df::cancel_token`, and `df::scope_locked_inc`.
- Application coordination: `app_frame`, `command_status`, `view_command_status`, and `search_auto_complete`.
- Index and shared caches: `index_items`, `index_state`, `df::index_file_item`, `df::index_folder_item`, `location_cache`, `string_index_t`, `spell_check`, and `crash_files_db`.
- Media and device coordination: `av_queue<T>`, `av_session`, `av_player`, `av_visualizer`, `wasapi_sound`, and the D3D shared-texture handoff.
- Immutable worker publication: `map_engine::cache_entry` and the `map_engine` worker-lifetime token.

Atomic or interlocked COM reference counts synchronize lifetime only and do not make the referenced object's mutable payload safe for concurrent access. File-scope cancellation generations and bounded activity counters are coordination channels, not permission to share adjacent mutable state.

`texture_state` remains UI-owned. A display decode request carries one detached atomic cancellation token; the UI may only mark it canceled, and the render worker may only read that mark before beginning expensive decode work. No texture, surface, geometry, phase, or generation state is read through the token, and decoded results still return through `queue_ui` for generation-checked publication.

`df::item_element` is entirely UI-owned. Its metadata is a plain `shared_ptr<const item_metadata>` snapshot and its playback position is a plain UI scalar; neither may be read or mutated on a worker. Player open requests carry detached path, file type, and starting position, while explicit close persists the session's final position through the index write queue. Query, CRC, folder-summary, batch-date, thumbnail, scan, save, rotate, render, and presence workers consume detached values or immutable requests and publish results through `queue_ui`. UI publication verifies lifetime plus the relevant path, generation, request, size, status, or CRC currency before applying one bounded update. Do not add or reintroduce synchronized fields in `item_element`.

`df::index_file_item` is a separate synchronized index record. Its atomic metadata pointer publishes complete payload snapshots: folder validation, scans, and reverse geocoding clone when necessary, complete all fields, and atomically replace the pointer. Published metadata payloads are never mutated in place, and new payloads are initialized before publication.

`df::index_file_item::file_modified` is an atomic scalar for the same reason as `metadata_scanned`. Its owning contexts are the scan queue (folder enumeration in `validate_folder`) and the work queue (`index_state::apply_scan_now`, when a coherent post-write scan carries the file's new modified time); it is read from the UI, database, and search contexts. Single-context ownership is insufficient because `df::index_folder_item::files` is `const`: the file list changes only by publishing a new folder node, so a write that advances one file's modified time would otherwise have to rebuild the whole folder's record vector per written file. The protected invariant is that the record's modified time never lags the file it describes, because `should_load_thumbnail`, `needs_scan_impl`, and the item database write all compare against it.

`media_preview_superseded` is a file-scope coordination flag for the hover preview worker, reaching the decode through `media_preview_state::abandon_token()`. It is set by whichever context queues preview or teardown work and cleared by the preview worker as it takes the task it is about to run, both inside `media_preview_mutex`; the decode reads it without the lock, once per demuxed packet. Single-context ownership is insufficient because the signal has to reach a walk that is already running on the worker. The protected invariant is that a request queued after a task started is visible to that task, which the shared lock on both transitions provides. Nothing is read through the flag: it only ends the walk at the nearest frame already decoded, and that frame still returns through the same generation-checked UI publication as a completed one.

Deferred async tests keep worker and UI queues separate so lifetime and currency behavior can be exercised by completing requests out of order. New ownership migrations should add a stale-result test before removing their synchronization fields.

Adding a type to this registry requires documenting its owning threads, the protected invariant, and why moved results or immutable snapshot publication on one owning context is insufficient.

### Session diagnostics

The counters in `src/util.h` (`df::ui_perf`, `df::db_perf`, `df::query_perf`, `df::file_perf`, `df::thumbnail_perf`, and one `df::queue_counters` slot per worker queue) aggregate where a session spent its time. They are a deliberate exception to single-context ownership and a deliberate non-exception to the synchronized-type registry: nothing branches on a counter, nothing is published through one, and increments are `memory_order_relaxed`, so they are write-only totals rather than shared state. They are read once, from `app_frame::final_exit`, after the worker queues have drained.

Instrumentation sits at choke points that already cross a boundary, so the added cost is a relaxed add and a performance-counter read against work that was already going to queue, decode, or hit SQLite. The generic worker loop accounts every queue through `df::register_queue`, which returns one slot per queue name rather than per thread, so queues drained by several workers still report a single row. Each timed area records a total and a maximum, because an average hides the single long occurrence that produces a visible stall.

The summary is one grouped block of `perf ...` lines, suppressed entirely when the app did no measurable work. Counts are printed exactly, with digit grouping rather than rounding, because cross-checking one counter against another is how a measurement defect gets caught - two were caught that way while this instrumentation was being built.

## Index, search, and database

Startup loads cached items, validates roots, reconciles filesystem entries, scans stale metadata/thumbnails, queues database writes, and rebuilds predictions, presence, summaries, and search candidates. Cached results can publish before full extraction completes.

The item index and aggregate summary have separate short-lived locks. Database writes are queued and committed by the database worker.

Item `search_presence_mask` values and folder OR summaries are conservative search prefilters over semantic categories, not probabilistic Bloom filters. Required bits are derived only when every exact result must contain them. Negation adds no required bits and OR disables the prefilter. False positives are acceptable because exact matching follows; false negatives are not.

Search supports substring, wildcard, range, location, negation, and Boolean semantics. Query generations prevent stale results replacing newer searches. Candidate-generation optimizations must preserve exact matching and prove completeness.

A search with a folder selector re-reads that folder from the filesystem as it iterates, and those selector folders are also the only ones live-watched, up to a handle budget. A search with no selector — related items, duplicates, a tag, a date — answers from the cached index and has nothing watching it. An in-app operation that adds, removes or moves files therefore reports the folders it touched to the index rather than relying on a watch that may not exist; otherwise the view keeps listing a file the operation just deleted.

Reporting a change has two halves, and both are required: correct the index, and ask for the search to be run again. `queue_validate_changed_folders` re-reads the named folders only, and `queue_scan_folders` also scans their files and recurses into their subtrees; each requests `refresh_items` once per batch, and only when a folder actually differed. The recursive single-folder path deliberately does not, because a request per folder walked would re-open the search repeatedly while a subtree was still being scanned. An operation that empties a folder must name that folder as well as its destination — a move reports both ends.

Duplicate prediction narrows candidates with names, dates, sizes, and CRC values, then applies exact rules. Where those rules fail but two candidates share an exact capture time, a 64-bit DCT perceptual hash of each picture decides, within a small Hamming distance. That hash is never computed on the index walk: the walk notes a bounded batch of pairs it could not judge, a worker decodes those files at the hash's own extent and stores the result, and the pass runs again, so a large collection converges over several rounds. Every attempt is recorded, including a refusal and a file that could not be read, or the next pass would ask for it again forever. Presence compares outside items with indexed candidates. Neither is an authoritative deletion decision.

The perceptual result feeds the one duplicate-group computation rather than a path of its own. `@duplicates`, presence, and a related search all read `duplicate_info`, so they cannot disagree about what is a copy.

A related search scores every collection item against one anchor and keeps only the closest on each relation axis. Scoring happens on the search worker inside the one index walk; results are held in a fixed-capacity per-axis heap keyed on distance and tie-broken on path, so the surviving set does not depend on folder iteration order. Counting and displaying a related search share that walk and that collector, so a count cannot disagree with the set shown. The axes are evaluated in priority order and yield at most one relation per item, and the anchor is scored ahead of every match so a full axis can never evict it. A related search written as text carries only a path, so every field the axes compare is rebuilt from the index before matching starts; `df::related_info::load` and `resolve_related` are that pair and must stay in step.

### Thumbnail pipeline

A thumbnail passes through four representations, each cheaper to rebuild than the one before it: metadata dimensions drive layout; an encoded image (lossy WebP, bounded to 256 KB and to `thumbnail_max_dimension`) is the durable form held per item and written to SQLite; a decoded surface is staged only for the near-viewport working set; and a GPU texture is created lazily by `render` on the UI thread. Resource cleanup drops the last two for off-screen items and Items restages visible ones without rescanning files or database rows.

`files::surface_to_thumbnail` is the only encoder for that durable form, and it exists so the choice is made once rather than at each site that produces a thumbnail — the metadata scan, the offline and shell fetches, cover art, the container scan, and the hover video frame. It is deliberately not `surface_to_image`: a thumbnail is a rebuildable cache entry, so it is encoded for bytes at a quality the browser cannot show the difference of, with a cheap encoder search rather than the quality-first configuration a file the user asked to save gets. WebP at `thumbnail_webp_quality` is around a third smaller than the JPEG it replaced at equal measured quality, and several times smaller than the PNG that transparent thumbnails used to cost; libwebp drops the alpha plane itself when a surface turns out to be opaque, so nothing upstream has to classify one. The stored blob is self-describing, so thumbnails an older build wrote are still read without a migration.

The encoded form is bounded too. `df::trim_thumbnail_blobs` holds the result set's thumbnails to `df::max_thumbnail_bytes` (32 MiB, fixed rather than scaled to the machine — what the retention buys is scroll-back without a database hop, and a screen of scroll-back is the same amount of work everywhere). Items are ranked by distance from the viewport measured in viewport heights, not by age, because scrolling is what decides which thumbnail is about to be needed again. Bands are kept outward from the viewport while they fit; the viewport's own band is kept whatever it costs, since a budget that blanks what is on screen is worse than the memory it saves. Eviction re-arms `db_query_pending`, which is what makes the return trip a batched SQLite read rather than a file scan, and deliberately leaves `_layout_dims` alone — the same thumbnail comes back, so recomputing geometry would reflow the row for nothing. It runs on the same scroll hysteresis as texture eviction, so neither fires until the viewport has moved half a screen.

Acquisition is ordered cheapest-first and each stage is gated so the next one is not started needlessly. Indexing produces no visual at all: a metadata scan decodes, scales, stores and publishes neither thumbnail nor cover art, so a newly indexed item shows its shaped placeholder until it is first displayed. That is deliberate. Generating a thumbnail during the index pass stored it with no scan timestamp, which made it provisional — the visible-item pass replaced it on first display anyway — so the collection-wide decode, scale and re-encode bought a first paint and was then thrown away. Acquisition therefore begins when an item becomes visible: SQLite is consulted first, per visible item, batched into one database hop, and only items the query did not resolve are queued as a scan. A scan reuses an embedded EXIF thumbnail when it is already within the size limit; otherwise it decodes the source, scales, and re-encodes. Cloud-only placeholders are never scanned, because that would hydrate the file; they ask the shell for the provider's thumbnail instead, only while visible, and only after the database query has run. No image is read ahead of the one being displayed, so nothing outside the user's own navigation can download a cloud-only file.

Two bounds keep cloud thumbnail work proportional to what the user is looking at. `index_state::_offline_thumbnail_batch` is written only on the UI thread when a visible batch is queued and read by the `async_queue::cloud` worker; when it no longer matches the batch's own value, a newer visible set exists and the remaining requests stop issuing network fetches. Its invariant is that abandonment still reports every remaining request, so the `shell_pending` claim is cleared and items still on screen are re-armed for the retry pass. It is atomic because the worker cannot read the UI-owned `is_visible` flag that the decision otherwise depends on. Separately, a provider that only ever returns its generic icon (common for video) is retried a bounded number of times per item; after that the item keeps its file-type placeholder, and hydration resets the count.

Every step crosses a thread boundary, so the per-item state that sequences them is a single UI-owned word, `df::thumbnail_state`. Its flags, their owners, and the invariants they carry are documented at the enum in `src/model_items.h`; that declaration is the authority for their meaning. Two properties matter to anyone changing this area:

- The `loading` claim is taken on the UI thread before a scan batch is queued and released by that batch's completion hop, so it straddles a queue boundary. A batch that is dropped rather than executed leaks the claim and the affected items never request a thumbnail again, silently and with nothing logged. `async_queue::scan_displayed_items` therefore uses `enqueue`, not `reset_and_enqueue`; superseded batches run, observe their cancel token, and release. `items_view::retry_visible_thumbnails` re-requests work that cancellation abandoned once the visible set settles.
- Publication and staging are separate hops with separate generations. An item that receives a new encoded image keeps drawing its previous surface until the replacement is staged, which is what makes video scrubbing continuous. Because a stage in flight is invalidated by that generation bump and only reschedules itself when `staging_requested` was set, replacing an image without requesting staging in the same hop leaves the item permanently unstaged.

Items whose thumbnail was decoded for them stage as soon as it is published rather than at the end of their scan batch; deferring staging to batch completion made a whole screenful appear at once instead of filling in progressively. Bulk background scans keep batched publication, because publishing per item reintroduces a locked index lookup on the UI thread for every item.

Because the pipeline is tuned against a 50k-item collection whose scroll churn generates and cancels batches faster than they drain, `df::thumbnail_perf` records the ratios that decide whether the current design holds: scan requests abandoned to cancellation, and decoded surfaces discarded as stale. It is part of the session diagnostics described under "Async execution".

Measurement settled two questions that the code shape alone answers misleadingly; the readings are recorded at the `thumbnail_state` declaration. Superseded scan batches are the overwhelming majority, but each breaks on its first cancel-token check, so the `enqueue` asymmetry above costs a queue hop rather than a decode and is not worth trading the loading claim to remove. Conversely the "already staging" branch is a hot path rather than an edge case, which is why a staging request carries a latched `invalidate_on_stage` flag instead of a completion callback: a callback arriving mid-stage was discarded along with the request that coalesced it.

## Files and metadata

Scans detect format, extract properties/thumbnails, and reconcile embedded and sidecar metadata. Writes update the media file where supported or an XMP sidecar, then refresh the index.

A metadata-only AV scan bounds FFmpeg's stream probe. Left unbounded, `avformat_find_stream_info` entropy-decodes 7 to 20 H.264 frames per file purely to guess the reorder delay, which nothing Diffractor reports uses; on a first index that was the largest single consumer of CPU in the process, and the indexing thread was CPU-bound rather than disk-bound because of it. `av_format_decoder::open` therefore takes a required `media_intent` — `metadata`, `thumbnail`, or `playback` — so every caller states what it will do with the container rather than inheriting a default. The bound applies to `metadata` only. It is a `probesize` byte budget, because probe cost is roughly linear in bytes decoded, and it is applied after `avformat_open_input` and only when the container header already named every stream — so demuxers that spend `probesize` in their own header read, and streams whose parameters genuinely have to be discovered by reading, are unaffected. The probe still runs: it is what estimates duration and bit rate, and it is what resolves the pixel format that MOV and MP4 headers do not carry.

Container metadata is not at risk from that bound, and the reason is structural rather than incidental. Every demuxer gathers its tags in `read_header`, inside `avformat_open_input`, which the bound is applied after. This is what makes a trailing XMP packet safe: MOV walks all top-level atoms to end of file, and the ASF reader carries a Diffractor patch that seeks past the Data object for the packet the Adobe SDK writes last. Three test fixtures — `gizmo.mp4`, `ipod.mov`, `Byzantium.avi` — hold their XMP in the final 1% of the file, `ipod.mov` megabytes beyond the probe budget, and the scan equivalence test asserts the packet round-trips byte for byte and that exactly three fixtures still carry one, so the assertion cannot pass vacuously.

[File I/O](file-io.md) owns the write pipeline: staging and swap, the choice between a staged replace and an in-place patch, per-format write costs, backups, sidecars, rollback, and the failure contract.

Long operations execute immutable reviewed plans. Each item records success, failure, skip, or cancellation; cancellation stops future work without claiming to undo completed changes. Windows Shell may perform associations, drag/drop, clipboard, Recycle Bin, and copy/move, but Diffractor still owns target/effect disclosure and results.

## Media and rendering

Display state selects image, audio, video, archive, text/binary, comparison, or selection controls. FFmpeg sessions decode and synchronize off the UI thread; callbacks marshal changes back to UI.

The displayed image is built up in phases, from a correctly shaped placeholder to the full-resolution decode, and the same ladder serves zoom and resize. [file-io.md](file-io.md) owns that behavior, including why `async_queue::render_display` is separate from `async_queue::render`.

GPU and software renderers implement the same visible behavior. Font, DPI, device, theme, and graphics resets invalidate dependent glyph, texture, layout, and control caches coherently. Graphics resources may be released without discarding logical browsing state. [Rendering](rendering.md) owns the backends and their parity contract, device and swap-chain lifetime, the frame and resize lifecycle, batching, text, and the hardware video pipeline.

## Persistence and network

Preferences use the Windows settings store. SQLite stores rebuildable index data, thumbnails, import history, and web cache. Authoritative user metadata belongs in media or sidecars.

Updates, crash reporting, dictionaries, maps, and location are separate network capabilities. Keep transport in the platform/web layer and settings in the application layer; disclosure is defined in [design.md](design.md).

Feature use is a single process-wide `std::atomic<uint64_t>` in `app_settings.cpp`, reached only through `record_feature_use`, `features_used_since_last_report`, `load_feature_use`, and `clear_reported_feature_use`. It is a synchronized value rather than a `settings_t` member for two reasons. Contributors span threads: the UI thread records views, display groups, and slideshow; the query worker records search-term kinds from `index_state::query_items`; file workers record burn; and the web worker reads and clears it after a report. And `settings_t` is value-copied by the options dialog, so a member would roll the mask back to whatever it held when that dialog opened. Like the session counters, nothing branches on the mask and increments are `memory_order_relaxed` - it is a write-only accumulation, not shared state, so it stays out of the synchronized-type registry. Reporting captures the mask once, sends that value, and on success clears only those bits with `fetch_and`, so features recorded while the request is in flight survive to the next report.

Every bit in `features` must have a live recording site. Bits for retired capabilities are removed rather than left declared, since a permanently clear bit is indistinguishable from a feature nobody uses. One bit per `view_type` is recorded in `view_state::view_mode`, the single funnel for view activation; the per-tool bits only record a completed run, so without them an opened-then-abandoned view looks identical to one that was never opened.

## Diagnostic log

`df::log` writes `diffractor.log` beside the executable, or under app data when the install folder is read-only. The previous session's file is kept as `diffractor.previous.log`; both are attached to crash reports and to the support upload in `send_info`, so the log is treated as content that leaves the machine.

Three constraints hold, all enforced in `df::log` rather than at the call sites:

- **Bounded.** Writing stops at a 4 MB cap, so a failure that repeats once per scanned item cannot consume the disk. Worst case on disk is two rotations, 8 MB.
- **Not personally identifying.** The account segment of any `\Users\<name>\` or `/users/<name>/` path is replaced with `%user%` before the line is written. Scrubbing at the choke point covers OS, SQLite, and third-party messages that embed a path, which no call-site rule would. The rest of a path is kept: folder shape is what makes a report reproducible. Content files are identified by name only where that name is needed to reproduce a decode failure, and by extension alone in the crash and recovery handlers.
- **Worth reading.** End-of-session summary contexts (`perf*` and `main`) are exempt from the cap, because `df::log_perf_summary` and `log_file_op_summary` are written last and carry the most diagnostic value. Each is emitted once per session, so the exemption is bounded by construction.

A new call site that would log once per item or per frame belongs in `df::trace`, which is compiled out of release, or behind a de-duplicating counter like `record_xmp_error`. A per-item message that reaches the file is a defect: it displaces the summaries and inflates the upload.

## Crash-loop protection

Three independent mechanisms stop a fault from repeating on every launch.

`platform::set_crash_guard` covers risky GPU work: a durable marker is raised before the operation and cleared after it succeeds, so a marker still raised at the next launch attributes the crash and disables only that capability. See [rendering.md](rendering.md#resilience).

`crash_files_db` covers media files. `record_open_path` registers each file `index_state::scan_item` is about to read; `app_frame::crash` and `recover_callback` write what was still open into `diffractor-files-that-crash.txt`, and the next launch skips those files. The list has five properties, all in `util_crash_files_db.h`:

- **Attributed.** Several workers decode at once, so most open files are bystanders. The flush records only what the thread running the handler had open, and falls back to recording all of them when nothing matches — which is the recovery callback, running on its own thread.
- **Bounded.** 512 lines total, counting lines left by other releases. Entries already listed are not appended again, so a crash that repeats does not grow the file.
- **Recoverable.** Each line is `<release>\t<path>` and only the running release line's entries skip a file. A decoder fix ships in an update, and without the tag a file blacklisted once would never be scanned again on that install. Lines from an older release line are ignored, which costs at most one repeat crash after an upgrade.
- **Coarse.** The tag is `crash_files_db::release_tag`, the major component of the app version, not the build number or the point release. A build number changes on every compile, so a finer tag discarded everything the list had earned after almost any update and handed the user the same crash again. A crash costs far more than the missing thumbnail a skipped file costs, so the list is retried once per release line rather than per build.
- **Stated.** `app_frame::init` logs how many files the list is skipping, because a skipped file is otherwise an unexplained absence of metadata and a thumbnail.

Within the handler the flush is deliberately first and unconditional. It runs before the report, whether or not `create_dump` produced a minidump and whether or not `send_crash_dumps` is set, because protection from the next crash is worth more than the report of this one. In packaged builds, where `SetUnhandledExceptionFilter` is not installed at all, `recover_callback` is the only path that reaches it.

Two limits are deliberate. The list is written from the crash handler, so a fault that bypasses it — stack overflow, a hard kill, power loss — records nothing and the loop repeats; the guard-marker pattern above is what covers that case, and applying it per file would mean a durable write per scanned item.

Only unattended work consults the list, which is why `record_open_path` wraps `scan_item` and not the media-load path. A user who opens a file has asked for it, so the open is attempted; see [design.md](design.md#system-states-and-consistency). Nothing re-opens that file by itself afterwards: `format_restart_cmd_line` carries only `-no-gpu` and `-no-indexing`, and `open_default_folder` restores the saved search with an empty selection, so a restart resumes scanning the folder — which is guarded — without reopening the item.

A third limit is not deliberate. Both the flush and the report log, and `df::log` and `df::close_log` take a non-recursive lock, so a fault raised while that lock is held — heap corruption inside a logging call, say — hangs the handler instead of reporting. The process is then ended as a hang rather than a crash, and the next launch has nothing but the previous log to go on.

The safe start is the backstop for everything the other two do not attribute. `app_frame::load_settings` reads the persisted `unsettled_starts` count and writes it back incremented, and `app_frame::mark_startup_settled` resets it to zero on the first idle pass with a visible window. It has to be `load_settings` and not `init`: the platform creates the graphics factories from `setting.use_gpu` between those two calls, so a decision taken in `init` would leave the device creation it exists to avoid already done. `init` re-applies it, because `load_options` re-runs `setting.read()` over the reverted values. `should_start_safe` (app_settings.h) opens at two, because one unsettled start is also what an ordinary kill or power loss during launch looks like. A safe start calls `settings_t::reset_presentation`, which restores every setting that decides what the window puts on screen and additionally forces `use_gpu`, `use_d3d11va` and `use_yuv` off — not their defaults, which are on, because the user cannot turn them off from a window that never appears. Nothing the user cannot re-reach from a working window is touched: collection roots, recent lists, favorite tags, language and the task fields all survive, and the run says what it did rather than silently changing their layout.

This is deliberately the coarsest of the three. It attributes nothing and needs no cooperation from the failing code, so it is the only one that covers a fault in code that has not been written yet.

Its placement is bounded on both sides. Later than `load_settings` and the graphics device is already created; earlier and there is nothing to remedy, because `setting.read()` has not run, presentation is still at defaults, and no window or device exists yet — a crash before that point would be counted but the reset could not change it, and the notification would never be reached to explain it. `wWinMain` handles that earlier window differently: each step from `WSAStartup` to `InitCommonControlsEx` tests its own result and calls `show_fatal_error`, so those failures report rather than crash.

## Build and validation

Always build `df.sln`. Normal validation is:

```powershell
.\dd.ps1 test
```

For a focused run:

```powershell
diffractor64-d.exe /test:*search* | Out-Host
```

Add regression tests for search semantics, navigation restoration, operation targets, snapshot revalidation, partial failure, and cancellation. Performance changes validate throughput and UI latency under concurrent indexing and navigation.

Desktop and Store packages share the codebase; Store builds omit application-owned updates. Packaging, signing, dependencies, and prerequisites are maintained in [README.md](../README.md) and build scripts. Do not edit vendored `third-party/` source.