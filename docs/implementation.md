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

`state_strategy` abstracts model-to-UI coordination. `async_strategy` abstracts UI, database, location, media-preview, and named worker queues. Tests use null strategies to exercise model behavior without a window or workers.

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

Duplicate prediction narrows candidates with names, dates, sizes, and CRC values, then applies exact rules. Presence compares outside items with indexed candidates. Neither is an authoritative deletion decision.

### Thumbnail pipeline

A thumbnail passes through four representations, each cheaper to rebuild than the one before it: metadata dimensions drive layout; an encoded image (JPEG, bounded to 256 KB and to `thumbnail_max_dimension`) is the durable form held per item and written to SQLite; a decoded surface is staged only for the near-viewport working set; and a GPU texture is created lazily by `render` on the UI thread. Resource cleanup drops the last two for off-screen items and Items restages visible ones without rescanning files or database rows.

Acquisition is ordered cheapest-first and each stage is gated so the next one is not started needlessly. SQLite is consulted first, per visible item, batched into one database hop. Only items the query did not resolve are queued as a scan. A scan reuses an embedded EXIF thumbnail when it is already within the size limit; otherwise it decodes the source, scales, and re-encodes. Cloud-only placeholders are never scanned, because that would hydrate the file; they ask the shell for the provider's thumbnail instead, only while visible, and only after the database query has run. Prefetching the previous and next displayed image skips cloud-only neighbours for the same reason: prefetch must never download a file the user has not chosen to display.

Two bounds keep cloud thumbnail work proportional to what the user is looking at. `index_state::_offline_thumbnail_batch` is written only on the UI thread when a visible batch is queued and read by the `async_queue::cloud` worker; when it no longer matches the batch's own value, a newer visible set exists and the remaining requests stop issuing network fetches. Its invariant is that abandonment still reports every remaining request, so the `shell_pending` claim is cleared and items still on screen are re-armed for the retry pass. It is atomic because the worker cannot read the UI-owned `is_visible` flag that the decision otherwise depends on. Separately, a provider that only ever returns its generic icon (common for video) is retried a bounded number of times per item; after that the item keeps its file-type placeholder, and hydration resets the count.

Every step crosses a thread boundary, so the per-item state that sequences them is a single UI-owned word, `df::thumbnail_state`. Its flags, their owners, and the invariants they carry are documented at the enum in `src/model_items.h`; that declaration is the authority for their meaning. Two properties matter to anyone changing this area:

- The `loading` claim is taken on the UI thread before a scan batch is queued and released by that batch's completion hop, so it straddles a queue boundary. A batch that is dropped rather than executed leaks the claim and the affected items never request a thumbnail again, silently and with nothing logged. `async_queue::scan_displayed_items` therefore uses `enqueue`, not `reset_and_enqueue`; superseded batches run, observe their cancel token, and release. `items_view::retry_visible_thumbnails` re-requests work that cancellation abandoned once the visible set settles.
- Publication and staging are separate hops with separate generations. An item that receives a new encoded image keeps drawing its previous surface until the replacement is staged, which is what makes video scrubbing continuous. Because a stage in flight is invalidated by that generation bump and only reschedules itself when `staging_requested` was set, replacing an image without requesting staging in the same hop leaves the item permanently unstaged.

Items whose thumbnail was decoded for them stage as soon as it is published rather than at the end of their scan batch; deferring staging to batch completion made a whole screenful appear at once instead of filling in progressively. Bulk background scans keep batched publication, because publishing per item reintroduces a locked index lookup on the UI thread for every item.

Because the pipeline is tuned against a 50k-item collection whose scroll churn generates and cancels batches faster than they drain, `df::thumbnail_perf` records the ratios that decide whether the current design holds: scan requests abandoned to cancellation, and decoded surfaces discarded as stale. It is part of the session diagnostics described under "Async execution".

Measurement settled two questions that the code shape alone answers misleadingly; the readings are recorded at the `thumbnail_state` declaration. Superseded scan batches are the overwhelming majority, but each breaks on its first cancel-token check, so the `enqueue` asymmetry above costs a queue hop rather than a decode and is not worth trading the loading claim to remove. Conversely the "already staging" branch is a hot path rather than an edge case, which is why a staging request carries a latched `invalidate_on_stage` flag instead of a completion callback: a callback arriving mid-stage was discarded along with the request that coalesced it.

## Files and metadata

Scans detect format, extract properties/thumbnails, and reconcile embedded and sidecar metadata. Writes update the media file where supported or an XMP sidecar, then refresh the index.

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