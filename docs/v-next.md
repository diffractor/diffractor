# Post-1.27.1 Engineering Context

GitHub issues are authoritative for backlog, status, discussion, and reporter follow-up: https://github.com/diffractor/diffractor/issues

This document records durable context that spans issues or would otherwise be lost between releases: what was deliberately deferred, why, and what unblocks it. It is not an issue list or a release plan. Each entry links to the document that owns the shipped behaviour.

---

## 1. Carried forward from 1.27.0

### 1.1 Deferred work

Scoped, understood, and deliberately not taken before 1.27.0 shipped.

- **Zoom navigator on/off switch.** [zoom](zoom.md) §16.1's last outstanding item: the setting that hides the corner navigator, and its non-fading behaviour in zoom mode. Everything else in §16.1 landed — the zoom-mode command gate, the single `Ctrl+Space` entry, the `Escape` peel and page-key sequencing are all in `app_frame::key_down` and `view_state::escape`.
- **Nested modals parented to a running modal.** Found by the 1.27.0 modal review and deferred; the other sixteen defects in that pass were closed. A modal opened from inside a running modal has its lifetime tied to a frame that can close underneath it.
- **Photo edit still decodes on the UI thread on entry and resize.** The three automatic adjustments moved to `async_queue::render` for 1.27.0; entry and resize predate the release and were left as they were. The entry call is `edit_view::display_changed`, which reads and decodes the full-resolution bitmap inline. Making it async is not a patch: `_edit_state.reset` and `update_media_elements` both consume `_loaded`, so it needs a placeholder state and a second publication hop — a redesign of edit-view entry.
- **Folder autocomplete enumerates the filesystem on the UI thread.** `folder_auto_complete::search` asserts the UI thread and then calls `platform::select_folders`, a synchronous `FindFirstFileEx` walk with no cancellation or timeout, whenever the typed text parses as a path — so once per keystroke. An unreachable UNC or mapped drive stalls the modal for the filesystem timeout. Deferred because there is no cheap mitigation: a reachability probe blocks exactly as the enumeration does, so the fix is a real async hop through the `complete` callback the interface already takes, landing in a modal workflow.
- **Distance slider and visit timeline.** See [§4.12](#412-locations). Controls, radius-bearing terms, visit derivation and tests all remain; only the Items presentations were withdrawn.

### 1.2 Validation not run before release

Specified but not all executed. Recorded so the coverage gap is visible rather than assumed closed. Run each against the shipped build.

- Zoom on both backends (GPU and `-no-gpu`) at 100%, 150% and 200% scaling across portrait, landscape, tiny, huge and mixed-aspect images: the two magnified states, single entry and peeled exit, grading from inside zoom mode, inert commands outside its vocabulary, page-key sequencing, ladder reversibility, pan limits by every input, navigator behaviour, settled-quality upgrade, comparison pane identity and flipping, pointer-capture release, and touch.
- Pin end to end in Items and fullscreen, in both detail and thumbnail presentations.
- Selection panel against scenarios 2, 6, 7, 8 and 9 in [selection controls](selection-controls.md#acceptance-scenarios).
- Rename, Import and Sync end to end, including a source or destination changed between Analyze and Run, and Import's Replace count.
- The Copy and Move collision prompt against a populated destination, including a folder collision and a network share.
- Photo edit end to end on a mixed folder, including Save+Next across non-editable items and saving pixel edits to RAW or HEIC.
- Perceptual duplicate detection and related search on a real library, with the `perf phash` log lines read afterwards.
- The Play and Slideshow split on a mixed folder.
- Sustained scrolling on a large collection after the thumbnail store moved to WebP. The decode is measured at roughly 4x the JPEG it replaced (0.13-0.21 ms to 0.53-0.92 ms per thumbnail at `thumbnail_max_dimension`), which is off the UI thread on `async_queue::render` and paid once per item per session, so it is expected to be invisible. That is a prediction, not a measurement: read `df::thumbnail_perf.stage_decodes` and the `render` queue counters after scrolling a 50k-item collection hard, and compare against a build with `surface_to_thumbnail` emitting JPEG.
- Metadata writes on SMB, OneDrive and a locked file (#180, #207, #103, #231), and container round trips (#3, #22, #123, #230).
- Fresh install, update and uninstall (#173, #215); #189 with GPU and `-no-gpu`; #232 without Calibri installed; locations on a library with GPS-only items.

### 1.3 Issues awaiting reporter confirmation

Evidence is strong enough to ship, but closure should wait for the reporter's own files, storage, language, hardware or install history. Ask each to retest and report the exact build number.

| Issue | Ask the reporter to |
| --- | --- |
| #61 video locations missing from the map | Rescan a video that previously failed and confirm the map area is highlighted. |
| #65 binary Samsung JPEG comments | Confirm on original SM-G900F photos that no binary text appears and real comments survive. |
| #78 videos with the wrong aspect ratio | Retest the affected videos in thumbnails, preview, fullscreen and playback. |
| #103 OneDrive reverts metadata | Edit tags and ratings under both Files On-Demand modes, sync, restart, and confirm nothing reverted. |
| #117 multiple genres | Save `Rock; Jazz` on their own audio files and confirm both values round trip and search independently. |
| #173 desktop shortcut reappears | Delete the shortcut, launch repeatedly, then update or reinstall over the top. |
| #175 history chart omits older years | Set History chart years to cover the oldest collection year and confirm old items appear without reindexing. |
| #180 locked-file write hangs | Repeat the MP3-open-in-VLC tagging steps and confirm prompt failure, then success once VLC releases the file. |
| #184 DateTimeOriginal grouping | Confirm the edited holiday photo groups and sorts at its DateTimeOriginal. |
| #189 mixed glyph sizes | Repeat the toggle sequence, including returning to the original size after a long session, in details, thumbnails, preview, sidebar and hover popups. |
| #207 network-drive writes | Rate and label files on the original NAS, confirm they survive a restart, and confirm no `diffractor_` files remain. |
| #215 Explorer opens every folder | Confirm folder double-click still opens Explorer while the Diffractor context command remains. |
| #219 Korean tags fail | Retest the original tags and supply the exact text and failing operation if any case remains. |
| #232 missing Calibri floods the log | Run normal browsing and Large Font toggles without Calibri and report log growth and fallback readability. |

### 1.4 Issues still open

**Ruled out on source evidence — no manual time needed.**

- **#137 hide Presence when the count is one.** The duplicate badge is formatted with `show_zero = true` and has no suppression for a count of one, in the item list or the title overlay.
- **#139 quote completed paths containing spaces.** `auto_quote_search_input` quotes a whole input only; the completion path concatenates lead and folder text with a space and never quotes.
- **#157 tag autocompletion.** The completion strategy exists only for the main search box; the tag editor is a plain multi-line edit with click-only suggestion toolbars.

**Implemented but unconfirmed against a real file, so not claimed in 1.27.0.** Each needs one manual test to rule in or out.

- **#47 RAW/cloud preview indication.** Partial: `can_preview()` drives a RAW preview badge and tooltip, but the cloud half is an offline presence icon, not a statement that the displayed image stands in for the original.
- **#81 hide the left area.** `view_show_sidebar` (F4) hides the left-anchored pane and persists the choice; test from Items, since the pane is also suppressed in zoom and compare.
- **#134 emoji filenames break rating/labels.** Covered by `Issue #134: Should update rating and label for emoji filename`; needs confirmation with keyboard and mouse, and a restart.
- **#138 rating/label/tags in fullscreen.** The fullscreen overlay is built from the same selection controls as the browser and includes all three.
- **#148 clickable URL metadata.** `df::url_extract` feeds a link element wired to Description, Comment and Synopsis only; a URL in any other property is inert.
- **#172 MKV/MP4 embedded cover art.** Any `AV_DISPOSITION_ATTACHED_PIC` stream is preferred over a frame grab, and the vendored Matroska demuxer promotes image attachments; no test extracts cover art from a real file.
- **#182 Description in Preview.** The Items preview pane renders a Description title and full multi-line text outside the editor; no test asserts it.
- **#224 group recursive thumbnails by subfolder.** The folder group key is the containing folder rather than the browsed root, so it composes with a recursive search; `group_by::folder` has no test.

---

## 1A. Found by the 1.27.1 pre-release review, not taken

Defects confirmed in the source before 1.27.1 and deliberately left, each with the reason. What was fixed instead is in [1.27.1](v-1.27.1.md).

- **`create_dlg` failure is never checked, and `dialog::_frame` is guarded at some call sites and dereferenced at others.** `platform_win_ui.cpp` models the `CreateWindowEx` failure and returns null; `dialog::create_frame` and every view's `controls()` use the result unchecked. Per the absent-handle rule that mix is already the bug. Left because the only trigger is desktop-heap or GDI handle exhaustion, and the correct fix - making `_frame` safe by construction so the inconsistent `if (_frame)` tests can be deleted - touches six views and is not a patch.
- **`app_frame` and `win32_app` hold each other by `shared_ptr`, so `~app_frame` never runs.** Its teardown ordering, `_state.close()` and the `ui_owned_ptr` hand-back are all dead code; the objects leak at exit instead. **Do not simply break the cycle.** The leak is currently load-bearing: it is what stops `ui::animations`, a global map holding strong references to `_view_media`, `_view_frame`, `_app_logo` and `item_element`s, from running UI and graphics destructors during static destruction - after `factories::destroy()`, `OleUninitialize` and `CoUninitialize`. Breaking it without also clearing `ui::animations` in `on_window_destroy` converts a leak into a shutdown crash.
- **`search_complete` probes and watches folders on the UI thread.** `folder_path::exists()` per selector, then `FindFirstChangeNotification` per folder, both inside a `queue_ui` dispatch. Against an unreachable share each blocks for the SMB timeout, and `open_default_folder` restores the last location at startup - so the worst case is a freeze before the first interactive frame. Same shape as the folder auto-complete item in [§1.1](#11-deferred-work): the fix is a worker hop plus publication of the surviving folder list, with the wait set staying UI-owned.
- **An audio endpoint whose mix format is not 16/32-bit PCM or 32-bit float freezes the playback clock.** `create_av_audio_device` returns the device even when the format maps to `prop::audio_sample_t::none`, so `audio_buffer::init` sizes to zero, `_pending_time_sync` is never cleared, and `av_session::pos()` returns `_last_seek` forever. Because the device *was* created, `_audio_unavailable` stays false and the wall-clock fallback that exists for exactly this case never engages. Treat an unmappable format as device-creation failure. Not taken because shared-mode mix formats are almost always float32 and no reachable test host reproduces it.
- **A non-device-loss `ResizeBuffers` failure permanently under-sizes the back buffer.** `handle_device_loss` returns immediately for anything outside the four device-loss codes, and `_buffer_extent` keeps the stale value, so the region beyond it shows the swap-chain background until a later resize succeeds. The realistic cause is `DXGI_ERROR_INVALID_CALL` from a surviving back-buffer reference.
- **Owning handles to UI-owned objects cross worker queues.** `view_selector.cpp` captures a `df::item_element_ptr` and `view_edit.cpp` captures a `dialog_ptr` into `queue_async` lambdas. Neither is dereferenced on the worker, but if the UI continuation is ever dropped rather than run the last reference is released there - and `~dialog` calls `DestroyWindow`. `model_items.cpp`'s `stage_thumbnail_surface` shows the correct shape: carry a weak handle or a detached identity and re-resolve on the UI thread.
- **The query worker's `validate_folder` is not held off until the database cache has loaded.** The index bring-up waits on `cache_loaded_event` precisely because `merge_folder` and `validate_folder` mutate the same folder nodes; `view_state::open` on `async_queue::query` takes no such wait, and `app_frame::init` restores the saved search within a few hundred milliseconds of a load measured in seconds. The symptom is the one the wait was added to prevent: the folder the user starts in re-read from disk on every launch.
- **`validate_folder` files every unchanged collection folder into `_distinct_other_folders`,** and `auto_complete_folders` walks the three folder sets without de-duplicating, so an ordinary collection folder is offered twice in the address bar.
- **`index_state::stats` is still an unsynchronized cross-thread struct**, and `index_items::replace` still copies the parent's child vector under the exclusive index lock.
- **`utf8_to_a` sizes its buffer from `string_view::size()` and then passes `-1` to `MultiByteToWideChar`.** Every current caller passes a NUL-terminated temporary, so it is correct today and an out-of-bounds read the moment one does not.
- **A stale checksum survives in the database after the file's bytes change.** `validate_folder` now clears the in-memory `crc32c` when the modified time or size differs, so the displayed-item worker recomputes it - but `perform_writes` upserts with `coalesce(excluded.crc, item_properties.crc)`, so a zero cannot clear the stored value and the stale one returns on the next launch. It self-heals only for items the user displays. Clearing it properly needs a deliberate "write null" path through the item write. The same argument applies to `phash`, which nothing invalidates on a content change at all.

---

## 2. Invariants that must survive a re-sync or refactor

### 2.0 The spelling dictionary writes to the per-user folder, never the install folder

Fixed 2026-08-11. `spell_check` used to pick `<install>\dictionaries` whenever that folder existed — and it always does, because `en_US.aff`/`.dic` ship there (verified in `Diffractor_1.26.3.1207_x64.msix`). The writable `%LOCALAPPDATA%` fallback was therefore unreachable. On a Store install that folder is read-only, so *Add to dictionary* and every dictionary download failed silently: the word survived the session in the in-memory Hunspell and was gone at restart, leaving only a log line. Desktop installs were unaffected because `diff.nsi` installs under `$LOCALAPPDATA` with `RequestExecutionLevel user`, which is why it went unnoticed.

The rule now is a split, and it must stay split: **reads resolve from either folder, preferring the per-user copy so a downloaded dictionary supersedes a shipped one; every write goes to `%LOCALAPPDATA%\Diffractor\dictionaries`.** `find_dictionary` and `ensure_user_folder` own the two halves. The user folder is created on demand, so a user who never adds a word gets no empty folder.

Guarded by `Should keep the custom dictionary where it can be written` (`test_text.cpp`), which asserts the custom dictionary path is under the per-user folder and not the install folder. Verified discriminating: pointing `_custom_dic_path` back at the shipped folder fails it. `Should check spelling` covers the other half — it passes only because the read still falls back to the shipped `en_US`.

### 2.1 XMP toolkit fork divergence

The vendored Adobe XMP toolkit is its own git repository at `third-party/xmp`, on the `diffractor` branch. **Re-syncing with upstream Adobe silently re-introduces defects upstream still ships.** [File I/O](file-io.md) §11 records which of these the write pipeline depends on, and owns the toolkit options Diffractor does and does not pass.

Fixes that must be carried forward:

- **WebP chunk order.** Upstream `WEBP_Handler`/`WEBP_Support` rebuild the RIFF container by chunk *category* — VP8X, ICCP, EXIF, XMP, image data — and truncate to the new length, so any file whose physical order differs has its image or alpha data overwritten by metadata. The fork rewrites chunks in their original file order, which is why `Container` keeps an `ordered` list alongside the category map. Guarded by `Should preserve webp chunks on metadata save` in `test_files.cpp`.
- **Other upstream WebP defects:** no `WriteTempFile`; the XMP packet is read as the packet plus a run of NULs; a `Container` is leaked per `CacheFileData`; a 64-bit chunk size is cast through `XMP_Int32`.
- **MP4 identical-box guard.** Upstream `MPEG4_Handler::ExportXtraTags` marks the box tree changed even when the rebuilt `Xtra` box is byte-identical, forcing a `moov` relocation and a whole-file rewrite of a large movie on every save. The fork returns early.

Constraints on the fork's own additions:

- `ExportTIFF_WindowsEncodedString` must not delete an XP* tag merely because the property is absent from XMP: only `exif:XPKeywords` is maintained by `metadata_edits::apply`, so clearing keywords is expressed as an empty value, not a deleted property.
- The ASF Extended Content Description rewrite emits 16-bit name, value and descriptor-count fields, so an oversized value is dropped rather than written with a truncated length.

### 2.2 What Windows Explorer actually does when it rates a file

Measured 2026-08-03 on x64 with `tmp/propsys_experiment.py` (`IPropertyStore` against copies of `exe/test` media, diffed byte for byte) and `tmp/propsys_scale.py`. This exists so the "why is Explorer instant?" question is answered from data rather than re-derived.

| File | Operation | Time | Size Δ | Bytes changed |
| --- | --- | --- | --- | --- |
| `indy.mp4` 50.4 MB, no `Xtra` | first rating | 359 ms¹ | +53 | 2 box-size fields; 53-byte `Xtra` appended at EOF |
| | change 3★→5★ | 6 ms | 0 | **1** |
| | same rating again | 6 ms | 0 | **0**, no write at all |
| `gizmo.mp4` 387 KB, has `Xtra` | clear | 9 ms | −45 | 978 in 45 ranges, trailing boxes shifted |
| `ipod.mov` 3.3 MB, `Xtra` + `XMP_` | clear | 29 ms | −45 | 964, `XMP_` relocated but intact |

¹ First-call COM initialisation. Steady state is ~6 ms independent of file size — 5.9 ms at 50.4 MB, 5.8 ms at 260 MB, 7.3 ms at 1.10 GB, 6.8 ms at 3.72 GB — with growth a constant 53 bytes. An earlier run appeared to show the first write scaling with size; that was the script flushing its own dirty pages. Any future large-file timing needs the same control.

- **Explorer's write is smaller, not braver.** Windows does not use the XMP toolkit. It writes `WM/SharedUserRating` into the Microsoft `Xtra` box through the Windows Property System and nothing else — no XMP packet, no reconciliation, no second representation to keep consistent. It is O(1) in the file and idempotent, matching the guard already in `ExportXtraTags`. Its growth case is bounded only by luck of layout. Diffractor writes both representations by choice, for interop, and pays for the second one.
- **Windows and XMP do not talk to each other.** `ipod.mov`'s `XMP_` box survived every operation byte-identical, and rating from Explorer left an injected XMP packet untouched. A file rated in Explorer can therefore carry two ratings that disagree, and `ImportXtraTags` only fills values XMP does not already carry, so Diffractor reports the stale one. This is the strongest argument for continuing to write both.
- **Explorer's JPEG rating is not non-destructive.** The same probe against `Test.jpg` (86,831 B) shrank Exif `APP1` from 22,194 to 13,060 B and XMP `APP1` from 3,271 to 1,743 B. The entropy-coded scan is byte-identical and `MakerNote` survives; the loss is the embedded Exif thumbnail, re-encoded from 10,610 to 3,388 B. A second rating change costs 16 bytes, so JPEG has the same expensive-first-write shape for Explorer as for the toolkit. We are not merely slower than Explorer here — we are preserving a thumbnail it silently re-encodes.
- **Adobe is a weaker comparison than it looks.** Lightroom and Premiere mostly do not write the file at all, keeping the rating in the catalog until an explicit save, so their fast path is fast by not being a file write. Bridge does write through the XMP SDK and is not fast. Deferring the write is available to us too, but it trades away the property the write path exists to protect: that the user's rating is in the user's file and not only in our database.

### 2.3 Absent windows, and why the guards were removed rather than added to

1.27.0 shipped a startup crash: `sidebar_host` borrowed the Items view frame only while `setting.show_sidebar` was set, but a hidden sidebar still populates, counts rows, enumerates drives and invalidates. The released fix added six null checks. That was the wrong shape of fix, and re-adding checks is the regression to watch for.

- **Guard duty cannot be distributed.** `view_host` and `view_scroller` dereference `frame()` in `update_controller`, `update_cursor`, `escape_controller`, `on_mouse_left_button_up` and `scroll_offset`. Base-class code cannot know whether a particular subclass has a window, so a subclass that answers null is a Liskov violation, not a call site that forgot a check. `frame()` is therefore total: `ui::no_frame()` when there is no window. Do not re-introduce a nullable `frame()`, and do not "helpfully" add `if (_frame)` around a call that already goes through the accessor.
- **The null object's answers are chosen, not arbitrary.** Occluded, invisible, unfocused, not enabled, empty bounds, cursor at `{-1,-1}`. Every one makes a caller that acts on it do *less*. Adding a member that answers optimistically would turn a no-op into a wrong result.
- **`ui::control_frame_ptr` deliberately has no stand-in.** Its interface is factories (`create_edit`, `create_dlg`, `create_bubble`), so a null object would have to hand back nulls and move the problem one level down. `view_controls_host::_dlg` and `dialog::_frame` stay guarded or safe-by-construction instead. If a future refactor gives `control_frame` a non-factory core, that is the point at which this decision should be revisited.
- **The test is discriminating and cheap.** `Should survive a host with no window` (test_view.cpp) drives the whole `view_host` pointer surface plus `view_scroller` against a host that answers `ui::no_frame()`. Changing that host to return `nullptr` exits `0xC0000005`; verified 2026-08-09. Run that negative case again before trusting any change to the contract.
- **Not covered.** No test constructs a real `sidebar_host`, which needs a `view_state`. The unconditional `attach_embedded` in `items_view::layout` is verified manually: launch with `show_nav_bar = 0` (REG_DWORD under `HKCU\Software\Diffractor` — writing it as `REG_SZ` makes the app fall back to the default and silently tests nothing), let it index, close cleanly.

### 2.4 Display memory: what may be dropped, and when

1.27.1 replaced read-ahead and flat byte caps with two policies that are easy to undo by accident. [file I/O](file-io.md#47-lifetime) owns the shipped behaviour; these are the parts a refactor must not quietly reverse.

- **No *image* is read ahead, and that is a measurement, not an oversight.** `texture_state::prefetch` and the full-size neighbour loop in `display_state_t::populate` were removed. Prefetch decoded the previous and next image at a hardcoded `{2560, 2560}` regardless of window size, so in a 1000 px window the two images the user was *not* looking at cost more than ten times the one they were. Re-adding image read-ahead needs a measurement of a display-size decode first, not an appeal to smoothness. What `populate` does stage ahead is the neighbours' *thumbnail surfaces* — two 320-pixel decodes, the same work the browser does for a whole screenful — and that is load-bearing, not an optimisation: without it the media view has no stand-in and shows the grey rectangle on every step.
- **There is no item-to-item fade, and the hazard that removed it is worth remembering.** `clone_fade_out` copied the *outgoing* item's `_last_draw_tex` into the incoming `texture_state`, which made the incoming image's first frame depend on a `texture_state` the recent-texture cache is free to demote. It was also already inert: it set the outgoing alpha from `_display_alpha_animation.val()`, which is zero on a freshly constructed texture, so the fade ran 0 to 0. It was removed rather than repaired, because a new item's thumbnail is available immediately and the transition bought nothing. Re-introducing any cross-item fade re-introduces the coupling.
- **"Available immediately" is an obligation, not an observation.** Removing the cross-item fade removed the thing that hid phase 1's latency, so any delay before the first texture is now a visible flash of `group_background`, a flat mid-grey. Six things keep phase 1 immediate and all six are load-bearing: `seed_placeholder` adopts the surface the browser already staged; `get_tex` applies it to cached entries as well as new ones because demotion drops their textures; `refresh` re-seeds on every layout because a requested thumbnail arrives after the texture state was built; the decode publish guard adopts a superseded placeholder when nothing is drawn or staged; `populate` stages the displayed item and its neighbours and asks the index for a thumbnail where there is none, because only `items_view` otherwise does; and a view switch clears offscreen items only. Removing any one of them restores the flash without failing a test.
- **`calc_flex_layout` reports the container height, not the content height, for a centred line.** `line_fills_main` is set whenever a line spends free space to position itself — `center`, `end`, or `space_between` — because reporting only the content would let an ancestor centre it a second time. That is correct and it is a trap for any caller asking "where did this block end?" so it can put something after it: the answer is the bottom of the container, and the something lands a full free-space gap too low. Ask a top-aligned layout instead, or compute the offset without going through the returned extent.
- **`_loaded.s` is not a cache, it is the only copy.** `files::load` returns encoded bytes for JPEG, PNG, WebP and a RAW preview, but a decoded surface for PSD, HEIF, JXL, TIFF, GIF, BMP and a developed RAW. `release_decoded_surfaces` deliberately keeps `_loaded` whole. Dropping `_loaded.s` alongside the derived surfaces would force a full native-size re-decode, and for a developed RAW it would also discard the phase 4 decision, because `refresh` reloads through `load(path, /*can_load_preview*/ true)`.
- **Thumbnail eviction must re-arm `db_query_pending` and must not touch layout.** That flag is set at construction and cleared once by `begin_db_thumbnail_query()`; without re-arming it, `update_visible_items_list` routes the returning item to a file scan instead of the batched SQLite read that makes eviction worth doing. And `refresh_layout_dims` must not be called: for an item whose aspect came from its thumbnail, recomputing would clear `_layout_aspect_known` and reflow the row on scroll. The same thumbnail returns, so stale dimensions are the correct answer.
- **Not covered by tests.** No fade can be pinned — the suite has no draw context, so nothing observes `_last_draw_tex` or the alpha animation. That is not theoretical: 1.27.1 shipped `texture_state::draw` reading `_display_alpha_animation.val()` at the top of the function, before the upload that calls `fade_out` and restarts it, which made every upgrade flash the new resolution opaque for one frame and then fade it up from the background. Nothing failed. The ordering is now stated in [file I/O](file-io.md#49-the-resolution-dissolve) because the code cannot defend it. Neither budget has ever fired in a real session either: `df::max_thumbnail_bytes` needs roughly two thousand thumbnails, and the recent-texture byte budget needs a HEIF or developed RAW plus navigation. Verify by hand on a large library: scroll hard and watch for a thumbnail that blanks and re-fades, or a burst of database work while scrolling; step between items and confirm each appears at once; watch one item sharpen and confirm the higher resolution dissolves in without the picture dimming; zoom in and confirm the same; develop a RAW, step away and back. `setting.show_debug_info` names the rung on screen, its alpha, and whether an outgoing texture is underneath, which is what makes those judgements checkable rather than impressionistic.

### 2.5 The thumbnail codec, and the four things that made it worth changing

[implementation](implementation.md#thumbnail-pipeline) owns the shipped pipeline. These are the parts that look like incidental detail and are not.

- **`files::surface_to_thumbnail` is the only encoder for the durable form.** The metadata scan, the offline and shell fetches, cover art, the container scan and the hover video frame all produce one, and each used to pick its own format through `surface_to_image(..., image_format::Unknown)`. That is precisely what let `get_shell_thumbnail` store every cloud thumbnail as PNG — the shell returns 32-bit BGRA even for opaque photos, `Unknown` reads the surface tag, and a 293x256 thumbnail cost 110 KB instead of 19 KB for as long as nobody looked. Re-introducing a per-site format choice re-introduces that class of bug, not just that instance.
- **`decode_webp_nv12` must keep cropping an odd axis.** A thumbnail scaled to fit a box is odd on one axis about half the time, and before the crop those fell back to a 4-byte RGB surface instead of 1.5-byte NV12, plus a CPU colour conversion. `decode_jpeg` has always cropped with `& ~1`; the WebP path simply did not, and the failure is silent — the picture is correct and only the memory and the time are wrong. Cropping needs the advanced decoder (`use_cropping` with `MODE_YUV` and external memory) because `WebPDecodeYUVInto` has no crop. `Should decode odd sized webp as nv12` pins it.
- **Do not classify opacity upstream of the encoder.** An explicit per-pixel alpha scan was written, shipped for one iteration, and then removed as redundant: libwebp's `alpha_enc.c` calls `WebPPictureHasTransparency` itself, and an opaque surface tagged `ARGB` encodes byte-for-byte identically to the same pixels tagged `RGB`. The scan cost a full pass over every thumbnail to reach the answer the codec already had.
- **`webp_fast` is not a micro-optimisation.** `save_webp`'s default is quality-first — method 6 plus sharp YUV — and measures 17.8 ms against 3.0 ms for the same image at the same size, because sharp YUV buys chroma accuracy rather than bytes. That is the right trade for a file the user asked to save and the wrong one for output only the browser reads. Giving thumbnails the file-save configuration would cost roughly six times the encode for nothing visible.
- **`df::assert_true` discards its argument in Release.** `apply_scan_result` asserted `thumbnail_image->data().size()` before checking `is_valid(thumbnail_image)`, so a failed encode was a null dereference that only Debug could reach, and reached as a `__debugbreak` rather than an assertion message. Anything an assertion dereferences must already have been guarded.

### 2.6 A run is never indexed by what the list is showing

Fixed 2026-08-12, after a review found the same root cause behind two separate reports. The guided task views — Convert, Edit metadata, Adjust date, Rename, Tags, Import, Sync — let the user sort the reviewed rows by clicking a column header, and that is offered for the whole of the review because `controller_from_location` only refuses while `_progress.active`. The run itself stays in plan order. Anything that pairs the two by position is therefore wrong the moment the user sorts, and silently right until then.

- **`row_element::_work_index` is the row's identity, and display position is never a substitute.** `batch_tool_view::show_run_results` indexed `statuses` and `_row_names` by loop position, so a sorted list reported every row's success or failure against a different file. Rename and Tags were the same defect wearing different clothes: they set `_order` as a *sequence* while `processing_exact_order_item` read it as the *class tag* Import and Sync use, so exactly one row ever matched and progress froze at 1 of N. `_order` now means only "original display order, and the key `sort()` restores by"; `_work_index` is the run's index and nothing else.
- **The two index spaces are deliberate and must not be unified.** The batch views set `_work_index` to the *plan* index, including for rows the run skips, because `statuses` is plan-sized and a skipped row owns a slot in it. Import and Sync set a *dense* counter over participating rows only, because their workers count `start_item` calls and never report a skipped row. A new view that copies the batch convention while its worker consumes a filtered list re-creates the misattribution, and `Should resolve list rows by work index` will not catch it — that test pins the resolver, not the assignment.
- **`_active_row` is held, not indexed.** It is a `row_element_ptr`. Storing an `int` again is the obvious tidy-up and restores the bug: `sort()` reorders `_rows` and a refresh replaces them, so a stored position clears whichever row has since moved into that slot and strands the highlight on the one it left behind. Guarded by `Should track the active row across a reorder` (`test_view.cpp`), verified discriminating by reversing `_rows` mid-run — the old code then leaves two rows highlighted at once.
- **The mid-run sort is reachable, which is why this is not theoretical.** `begin_processing` does not raise `view_invalid::controller`, and `view_host::update_controller` only re-tests when the pointer leaves the controller bounds, so a header controller created during review survives into the run. Raising that flag would remove the reorder itself; the identity fix is what makes the reorder harmless either way.
- **`row_for_work_index` answers from the identity position first.** Not a micro-optimisation: the batch views queue one progress callback per item with no coalescing, unlike the four views behind `view_command_status`, so a plain scan is quadratic in the selection on the UI thread. The fast path is an exact match, so it can never answer differently from the scan.
- **Tags refuses a run over its own results, and the refusal must stay explainable.** `can_run()` tests `showing_results()`, which only `begin_processing` and `tags_view::refresh` clear — so `refresh` clearing it is load-bearing, and without that the gate would dim Run permanently. Tags has no Refresh button, so `commands::tags_run` carries `tt.run_needs_refresh` as its `disabled_reason`; a dimmed Run that names nothing is the thing `app_toolbar.cpp` already warns against.

### 2.7 A staged path is claimed before it can hold bytes, not after

Fixed 2026-08-12. `temp_file_created` and its siblings mean *"this path may have bytes at it"*, not *"the operation succeeded"* — `df::blob_save_to_file` and `CopyFile` both create the destination before they can fail on a short write or a full disk. `temp_file_created = result.success()` reads as the obviously-correct form and is exactly what a future cleanup would restore; it leaves a truncated `diffractor_<hex>` file beside the user's photo, where the indexer finds it and the browser shows it as a broken image.

- The staged sidecar is cleaned up by its own deterministic path, `path_temp.extension(".xmp")`, not by the returned `xmp_result.xmp_path`: `metadata_xmp::update` creates the file and *then* throws on a short write, leaving the result unassigned.
- **`rollback_file_created` is the exception and must not be hoisted the same way.** It also gates restoring the user's destination from that copy, so claiming it early could restore from a partial file — strictly worse than the leak. Its cleanup is unconditional instead, guarded only by `rollback_holds_sole_original`, which is the one case where the temp file is the user's last surviving original and must survive under its meaningless name.
- `jpeg_encoder::setup` opens with `jpeg_abort_compress` for the same reason `jpeg_decoder_x::read_header` opens with `jpeg_abort_decompress`: `files` holds one long-lived encoder, `handle_error_exit` throws out of libjpeg, and an encode abandoned mid-scan leaves `global_state` at `CSTATE_SCANNING`, after which every later encode fails for the process lifetime — taking thumbnails and Convert down together. Guarded by `Should reuse jpeg encoder after abandoned encode`, whose negative case fails with `Improper call to JPEG library in state 101`.


---

## 3. Withdrawn features

### 3.1 Remove Metadata tool

`tool_remove_metadata` was withdrawn from the UI before 1.27.0 because testers found it confusing. It was not a Diffractor operation at all: it handed the selection to the Windows shell `CLSID_RemovePropertiesDropTarget`, which opens Explorer's "Remove Properties" dialog. That dialog offers to *create a copy* with properties removed, so the command could silently produce new files in the folder, reported nothing back, bypassed [file I/O](file-io.md) entirely, and stripped whatever Windows considers a property rather than the fields Diffractor shows.

Removed with it: the command enum value, its menu and toolbar entries, its enable and disabled-reason wiring, `platform::remove_metadata`, and feature bit 46. `tt.remove_metadata_title` is deliberately retained so the existing translations in `exe/languages/*.po` survive the gap.

**To reinstate:** write it as a real Diffractor task — a selected-set operation with preview before run, an explicit statement of which fields it clears, collision and retained-original rules matching the other destructive tools, and writes through the normal metadata pipeline. [Metadata](metadata.md) owns which fields are writable, so it bounds the field set the tool can honestly claim to clear.

---

## 4. Deferred work by area

### 4.1 File writes: sizing rollback to the write

[File I/O](file-io.md) owns the shipped state: a container allowlist, where every format reaching the in-place branch has a bounded or staged fallback. The one remaining exposure — no rollback if a bounded patch is interrupted — is documented rather than half-mitigated.

The next step sizes the rollback to the write rather than to the file, in three tiers:

1. Stage and replace for small files and every excluded format.
2. Patch in place behind a rollback for provably bounded writes. Bounded means the new packet fits the old length — testable from public API via `SerializeToBuffer` with `kXMP_ExactPacketLength` — or the container is ISO base media.
3. Stage and replace for everything else.

**Tier 2 must journal the writes, not copy the packet.** Retaining the old packet bytes and offset and restoring them on failure is unsound and must not be implemented: the handler does not confine its writes to the packet. On an MP4 rating, `ExportXtraTags` dirties `moov`, and `MPEG4_MetaHandler::UpdateFile` checks `moovMgr.IsChanged()` independently of its in-place-XMP branch — so even when the packet is written back at its original offset, the same call rewrites the `moov` subtree, which may truncate, insert a `free` box, relocate, or append at EOF. A 4-byte timecode sample and `OptimizeFileLayout` also land outside the packet. Restoring a stale packet over an already-relocated `moov` converts a recoverable partial write into a reliably corrupt one.

The sound form is a write journal: interpose a Diffractor `XMP_IO`, record the pre-image of every range before each `Write` and the discarded tail before each `Truncate`, and replay backwards on failure. It must implement `DeriveTemp`/`AbsorbTemp`/`DeleteTemp` for real, because ASF takes the temp route on fallback.

**Scope note from [§2.2](#22-what-windows-explorer-actually-does-when-it-rates-a-file):** the `WM/SharedUserRating` entry is 45 bytes, confirmed by every clear-rating operation removing exactly 45, so *changing* a rating on a file that already carries one produces an identical-length `Xtra`, an unchanged `moov` size, and the same-size in-place branch. A journal is not what makes re-rating safe — re-rating is already same-extent. It is needed for the size-changing cases, which is the boundary the inject trait already draws.

**Client I/O is available without a fork:** `XMPFiles::OpenFile(XMP_IO*, ...)` under `XMP_StaticBuild`, which `src/metadata_xmp.cpp` already defines. Not needed for correctness, since a genuine in-place patch is already coherent, but it would supply an authoritative modified time and remove the last by-name re-open. Three constraints: the format must be passed explicitly, because `DoOpenFile` asserts the client path is empty; progress tracking is disabled; and the temp trio needs a real implementation only for handlers that can take an out-of-place branch.

### 4.2 Collision and destruction gaps

Copy and Move now name their collisions and ask (see [design](design.md)), but three paths still resolve or destroy without saying so. None loses data silently today; each is a stated-behaviour gap rather than a safety hole.

- **Paste and drag-drop** run through `perform_hdrop2` with `FOF_RENAMEONCOLLISION` fixed on, so a collision becomes a second copy with no prompt. Extending the prompt needs the collision test to run *before* the drop is performed — a different call path from the menu commands, with no destination folder resolved until the drop lands.
- **Sync deletes bypass the Recycle Bin even for local files.** `platform::delete_file` is `DeleteFile`; the confirmation states the permanence honestly, but a local collection delete could be recoverable. Moving to `platform::delete_items` with `allow_undo` means adopting its batch failure model and per-row status, which the per-row sync results contract would have to absorb.
- **Import Replace does not check sidecar destinations.** Only the primary destination carries a reviewed snapshot. A sidecar is guarded by `fail_if_exists` under every other policy, and under Replace it is overwritten unchecked. Closing it means threading per-sidecar snapshots through `import_analysis_item`, which currently holds one `destination_fi`.

**Use `check_overwrite` in `app_util.cpp`** — the shared collision test — rather than adding per-item existence queries. It was dead code until 1.27.0 wired it into Copy and Move.

### 4.3 Colour management

ICC profiles are read, preserved byte for byte on save, and parsed for the properties panel by `metadata_icc::to_info`. **They are never applied.** skcms is vendored, built and linked into every configuration, but nothing in `src/` calls it, so a wide-gamut file is displayed as though its numbers were sRGB — an Adobe RGB or ProPhoto photo renders *desaturated*, not oversaturated, because larger primaries are being interpreted against smaller ones.

[Metadata](metadata.md) owns profile parsing and what the panel shows; [rendering](rendering.md) owns the display pipeline. The constraints that span them:

- **Three implementations must agree, and the divergence is the real cost.** Colour reaches the screen through the HLSL shaders in `src/shaders/` driven by `platform_win_d3d11.cpp`, through the CPU preview in `platform_win_software.cpp`, and through `ui::surface::transform` in `render_surface.cpp` for what is written to disk. A transform added to one and not the others makes the software renderer disagree with the GPU renderer and the saved file disagree with both. Any pipeline change is three changes plus the evidence that they match.
- **`ui::color_space` is not the extension point**, despite the name. It selects the YUV→RGB matrix and signal range for NV12/P010 and nothing else. Source primaries and transfer function are a separate concept the surface does not carry; adding them is the first structural step.
- **Matrix-shaper profiles are GPU-friendly; A2B profiles are not.** sRGB, Adobe RGB, Display P3, ProPhoto and Rec.2020 — very nearly all display-referred content — decompose into a parametric source transfer function, a 3×3 matrix to a connection space, and a destination transfer function: one matrix multiply and two curve evaluations, evaluable analytically in a shader with no LUT texture, since `skcms_TransferFunction` is the standard seven-parameter form. LUT-based A2B profiles, where CMYK, scanner and printer profiles land, need a 3D LUT and should fall back to a CPU skcms transform at decode time rather than growing a second shader path. `skcms_Parse` distinguishes the two, so the split is decidable at load.
- **Where the transform happens is not a free choice.** Display and preview can defer to the render pipeline, which makes zoom and pan free. Editing and saving cannot: the written file must be correct regardless of what the display did. `file_load_result::to_surface` already defaults `can_use_yuv = false`, so the edit and save path takes the full-chroma RGB decode and is the correct hook. Thumbnails cannot defer either — cached thumbnail pixels carry no profile, so an unconverted thumbnail is permanently wrong and inconsistent with its own preview.
- **The thumbnail migration must be planned, not discovered.** Thumbnails are persisted in SQLite by `model_db`, so baking a transform into them invalidates every cached thumbnail for a wide-gamut file. Whether that is a cache version bump or stale-until-rescanned is a decision to take before the work starts.
- **Cost is unmeasured and must not be assumed.** Parsing a profile is negligible and already happens; transforming pixels is the exposure, and its magnitude here has not been measured. skcms ships AVX2 and AVX-512 specialisations, but the only number that matters is a benchmark of the real decode path before and after, at full resolution and at thumbnail scale. `files_jpeg.cpp` decodes scaled via `scale_num`/`scale_denom`, so cost tracks output pixels rather than source pixels and the exposure is confined to full-resolution viewing and editing. Per-row-band work on the existing queues is the obvious lever, but only after measurement says it is needed.
- **The destination is a platform question.** Without the system display profile the only defensible destination is sRGB, which is wrong on any wide-gamut or HDR monitor. Windows exposes it through `WcsGetDefaultColorProfile`, `GetICMProfile` per monitor, and `IDXGIOutput6::GetDesc1`. Per [AGENTS.md](../AGENTS.md) that code lives only in `platform*.*`. It must also be re-read when the window moves between monitors, which is behaviour rather than plumbing.

**Order if taken in stages.** (1) Convert to sRGB at decode time for matrix-shaper profiles only — small, testable with the banded-fixture approach `should_decode_bands` already uses, and it fixes the visible defect on its own. (2) Move the transform into the shader and software renderers, keeping the CPU path for edit, save and thumbnails. (3) Read the system profile and target it instead of assuming sRGB; this is the only stage needing new platform surface.

### 4.4 HEIF colour, depth and the missing YUV path

**HEIF states its colour two ways and Diffractor drops both.** `files_heif.cpp` extracts a `prof`/`rICC` profile into `result.icc`, where it joins the never-applied path above. It also detects `heif_color_profile_type_nclx`, names it in the properties panel as "nclx (coded primaries and transfer)", and does nothing with it. NCLX is not a lesser signal — it carries coded primaries, transfer characteristics and matrix coefficients, and it is what a Rec.2020 or PQ HEIF from a phone ships instead of an ICC profile. A file with NCLX and no ICC gets no colour handling from either mechanism. Whatever design [§4.3](#43-colour-management) settles on must accept both sources, or HEIF gets fixed twice.

**10-bit HEIF is silently reduced to 8.** Both `heif_decode_image` calls — thumbnail and full image — request `heif_colorspace_RGB` with `heif_chroma_interleaved_RGBA`, so libheif reduces under whatever policy that version implements, with no rounding or dithering choice exposed and no test asserting the result. The panel meanwhile reports true depth from `heif_image_handle_get_luma_bits_per_pixel`, so it can honestly say "10 bits" about pixels delivered as 8. The real question is whether the decode should request a wide interleaved format and do its own reduction, as `files_jpeg.cpp` now does for 12- and 16-bit JPEG — which is also the precondition for ever displaying HEIF above 8 bits.

**There is no NV12/P010 path, and the reason is structural.** JPEG and WebP reach the GPU YUV sampler because `files::load` keeps the *encoded* bytes in `file_load_result::i` and defers the decode to `files::image_to_surface`, which is where `can_use_yuv` is answered — so one file can yield an NV12 surface for the display and a full-chroma RGB surface for edit and save. `load_heif` decodes eagerly into `file_load_result::s` and `to_surface` returns it verbatim, so `can_use_yuv` never reaches the decoder and could not be honoured safely if it did: one eagerly decoded surface cannot be YUV for the display and RGB for the editor at once. The prize is real — HEVC is natively 4:2:0, so the current path pays a CPU YUV→RGB conversion plus a whole-surface `swap_rb` to produce 4 bytes per pixel where NV12 would be 1.5, and `_pixel_shader_yuv_bicubic` already upsamples chroma better than libheif does.

`load_webp` is the worked example of the shape HEIF needs — gate on `can_use_yuv` plus lossy-VP8, no alpha, no animation and even dimensions; decode with `WebPDecodeYUVInto`; interleave Cb/Cr into the NV12 chroma plane; return a surface the caller must not rescale on the CPU — and also of why HEIF cannot copy it. `decode_webp_nv12` hard-codes `ui::color_space::rec601_limited` because VP8 defines it. HEVC does not: the answer is in the NCLX matrix coefficients and full-range flag currently discarded, and guessing BT.601 for a BT.709 phone photo is a visible hue shift across the whole image. **NCLX parsing is a prerequisite for the HEIF YUV path, not a parallel task.**

Remaining blockers: `ui::image_format` models only JPEG, PNG and WebP, so HEIF has no encoded-bytes form to defer to and the work starts by giving it one or threading `can_use_yuv` through `files::load` into `load_heif`; libheif emits planar I420, so the same interleave pass is needed; 10-bit wants P010 rather than NV12, tying this to the depth question; alpha, odd dimensions and monochrome all need the RGB fallback. One constraint carries over from the orientation fix: `irot`/`imir` are per-item properties libheif applies during decode, so the surface's `orientation` and the swapped extent it implies must be established the same way on whichever path is taken — `has_orientation_transform` is the single answer for both scan and load, and a YUV path must not reintroduce a second one. NV12 also requires even dimensions, and unlike WebP the crop enforcing that has to happen in the *decoded* frame, after any transform has swapped the axes.

### 4.5 Metadata mapping and encoding

A correctness pass over `metadata_exif`, `metadata_iptc`, `metadata_xmp` and `metadata_icc` fixed encoding, clobbering, validation and display defects. These were found by the same pass and deliberately left, because each changes data users already see rather than correcting something wrong.

- **Unmapped IPTC datasets.** `IPTC_TAG_HEADLINE`, `SUBLOCATION`, `DATE_CREATED`/`TIME_CREATED`, `SPECIAL_INSTRUCTIONS` and `WRITER_EDITOR` are parsed and shown in the properties panel but not mapped into `prop::item_metadata`. Mapping them is not additive: Headline would compete with the existing title source and DateCreated with `created_exif`, so the parse order in `file_scan_result::to_props` — Exif, Iptc, ffmpeg, Xmp — decides whose value wins. Any mapping must state that precedence first, and preserve the rule that a later source with no value never erases an earlier one.
- **IPTC record 1 is never surfaced.** Both `parse` and `to_info` resync by scanning for the `0x1c 0x02` marker, so only application record 2 is read. Record 1 carries the envelope, including the 1:90 coded character set escape that would remove the need for the Latin-1 heuristic in `decode_iptc_text`. Reading it properly means restructuring both loops to walk records generically — a larger change than the heuristic it would replace.
- **Sidecar packet wrapper.** `metadata_xmp::update` writes sidecars with `kXMP_OmitPacketWrapper`, so there is no `<?xpacket?>` header or trailer. Adobe writes the wrapper and some readers scan for it, but adding it changes the bytes of every sidecar Diffractor has already written, so it needs a compatibility check against the readers that matter.
- **`dc:creator` round trip is lossy for multiple names.** `xmp_load_array` joins array items with a space, so a two-creator packet presents as one string in the editor, and writing it back with `str::is_artist_separator` collapses it to a single creator. Preserving multiple creators means modelling the field as a list rather than as joined text.
- **Inconsistent exception handling in the XMP writers.** `metadata_xmp::update(std::string&, ...)` catches only `XMP_Error` where its path-taking sibling also catches `std::exception`. Both propagate rather than swallow, so this is consistency, not a defect.
- **Native Matroska `SimpleTag` read/write.** The 1.27.0 interoperability work uses XMP plus native Windows MP4/MOV and ASF fields. Matroska is a separate enhancement and is not part of completed #123 work; [metadata](metadata.md) owns the current limitation.

### 4.6 Search: reverse index integration

`inverted_index`, `postings_union` and `postings_difference` are implemented and unit-tested but are not wired into query execution.

Diffractor search supports substring, wildcard, range and GPS matching; the reverse index has exact-token semantics. Search also uses conservative item category-presence masks and folder OR summaries as prefilters. **The reverse index must not replace the exact matcher without evidence that candidate generation remains complete.**

Before integrating:

- Profile representative queries on a real, large library.
- Use the `update_summary` vocabulary probe to measure distinct terms and occurrences.
- Preserve exact matching semantics and prove no false negatives.
- Remove probe logging after the decision.

### 4.7 Rendering: software gradient optimisation

[Rendering](rendering.md) owns the parity contract this work must not break.

`clear` and `draw_rect` were both `fill_rect_gradient(bounds, c, c.emphasize())` on both backends, so every background in the client was a centre vignette. `emphasize` is `(f - 0.5) * 0.9 + 0.5`, which on dark chrome moves a corner about 9/255 from the centre — subtle enough to read as flat while costing a full per-pixel gradient. Both are now flat fills, and a caller wanting the vignette asks for `draw_rect_gradient` explicitly, so `fill_rect_gradient` survives only for callers that opted in, plus `draw_vertices` and `fill_border_gradient`.

A first-index CPU profile put `fill_rect_gradient` at 35.93% of the whole process and `blend_over` at 27.36% self. **What remains of that cost after the flattening is the number to re-measure before spending anything below.**

Two independent wins remain for the gradient that is still drawn, in this order:

1. **Scalar, no parity risk.** `blend_over`'s general path divides each channel by `out_a`. When `_opaque` is set — every non-layered window — `da` is 1.0, so `out_a` is identically 1.0 and the three divides per pixel cannot change the answer; `/fp:fast` does not remove them because the compiler cannot prove `da == 1`. The constant-colour middle span also recomputes `c*sa` and `1-sa` per pixel when alpha is below 1. How much these recover decides whether the rest is worth doing.
2. **SIMD.** The row splits into three spans with closed forms: outside the middle band `max(tx, ty)` collapses to `tx`, which is affine in x, so both ramps are straight lerps, and the middle span is one constant colour. Unit stride, no per-pixel branches, no gathers. x86 baseline is already SSE2 (`EnableEnhancedInstructionSet`), so no runtime dispatch is needed — unlike `ui::surface::swap_rb`, which gates on `platform::ssse3_supported` for `_mm_shuffle_epi8`. Recompute the ramp colour per 4-pixel block from x rather than accumulating a fixed-point delta, which drifts across a wide row.

Two costs that are not footnotes: 8-bit fixed point differs from the float path by up to ±1 LSB, so the parity contract needs checking and possibly a tolerance; and there is no NEON path anywhere in this codebase — `util_simd.h` uses `COMPILE_ARM_INTRINSIC` only for `<arm_acle.h>` CRC — so ARM64 is new, unvalidated ground and the scalar fallback must stay correct.

### 4.8 Audio clock re-anchoring

The master clock for a session with audio is the device, and `av_player.h` anchors it **once** per buffer generation: `base_time` is taken from `audio_buffer::start_time()` when the generation changes, and every later reading is `base_time + ds->time()`, a played-sample count. Video frames are matched against that. Everything the 1.27 timestamp work fixed — one common time origin for both streams, `pkt_timebase` so FFmpeg can compensate encoder delay, `best_effort_timestamp` — corrects the timestamps *entering* the clock, not the clock itself.

**The residual defect is a genuine discontinuity in the audio stream.** If a file has a gap in its audio PTS with no samples behind it, the device plays fewer samples than the timeline advanced, so `base_time + ds->time()` lags the real content by the size of the gap and video is held back by that much for the rest of the file. `audio_buffer::append` re-derives `time` from each incoming frame's timestamp, so `start_time()`/`end_time()` do follow the discontinuity — but `base_time` was sampled once and does not, which is also why `_audio_data_end` (captured at EOF from `end_time()`) can drift out of reach of `_audio_clock` and leave end-of-clip detection on its two-second fallback.

The sound form is what ffplay does: re-anchor on every audio frame handed to the device, from that frame's own timestamp minus the amount not yet played out. That needs a "seconds still queued in the device" accessor on `av_audio_device`, which is platform surface (`platform_win_sound.cpp` and the `wasapi_sound` ring) rather than a change inside `av_player.h`, and it changes the clock every session uses. It was scoped out deliberately: the rest of the timestamp work is verifiable against the decoder's own output and this is not — it needs files with real audio discontinuities, and the corpus in `exe/test` has none.

Two constraints carry forward. Re-anchoring must not fight the priming: the device is deliberately not started until `playback_buffer.seconds() >= 0.3`, so an anchor taken before the first `start()` is meaningless and the first valid anchor is the first write after the device is running. And the anchor must stay monotonic across the silence pad appended at EOF, or `has_ended` regains the early-end behaviour the pad exists to prevent.

### 4.9 Zoom

[zoom](zoom.md) §16 separates what shipped, what was outstanding for 1.27.0 (§16.1, see [§1.1](#11-deferred-work)) and what is deferred past it (§16.2). The four §16.2 deferrals are independent of each other; none is blocked by the zoom model, and each waits on a capability outside it.

- **Viewport-region decode** needs a source-origin-bearing tile representation before the retained whole-image decode can be replaced, so it is a loading change ([file I/O](file-io.md)), not a zoom change.
- **Touch hold inspect zoom and a distinct pen barrel-drag** need pointer-contact timing the current Windows gesture path does not surface — the same plumbing [§4.10](#410-touch) needs.
- **§12 automation properties and settled-state announcements** need a UI Automation provider on the custom Win32 frame, which nothing else in the product has asked for.
- **Source-pixel coordinate and colour readout, and the high-magnification pixel grid**, are optional Study additions that were scoped out rather than blocked.

### 4.10 Touch

1.27.0 ships a deliberately narrow touch surface: tap to select, drag to rubber-band select, double-tap to toggle `Fit` and `100%` on a photo with the ordinary double-click as the fallback everywhere else, pinch to step the zoom ladder, and one-finger pan on a magnified image. The system press-and-hold and press-and-tap gestures keep their normal context-menu meaning rather than acting as a second entry into zoom mode.

**The main gap: the item list cannot be scrolled by touch.** `items_view` leaves `pan_start`/`pan`/`pan_end` empty, so every `GID_PAN` over the list is discarded regardless of finger count, and three-finger swipes never reach the application because the Windows shell claims them. Only Fullscreen and the map consume pan. Touchpad two-finger scroll is unaffected because it arrives as `WM_MOUSEWHEEL`, so the defect is specific to touch screens. It is pre-existing rather than a 1.27.0 regression, which is why it was documented instead of fixed under release pressure.

Filling in the empty overrides is not enough. A touch drag currently produces a promoted `WM_LBUTTONDOWN`/`WM_MOUSEMOVE` sequence that starts a rubber-band selection, so pan-to-scroll and marquee-select would fire simultaneously. The work needs touch-originated mouse messages suppressed — `GetMessageExtraInfo()` carries the `0xFF515700` signature — plus a separate tap-to-select path so suppression does not also remove selection. **That is the same pointer-contact plumbing the deferred touch-hold inspect zoom needs, so take the two together.**

Two deliberate deviations: pinch steps the discrete zoom ladder instead of scaling continuously, which departs from [zoom](zoom.md) §10 and will feel wrong to anyone expecting direct manipulation; and the gesture configuration names only the single-finger pan flags in `dwWant`, relying on the system default to leave `GC_PAN` enabled, where naming it explicitly would remove a dependency on default behaviour.

**Validate the current surface on real hardware before extending it.** None of the 1.27.0 touch behaviour has automated coverage, and the gesture path is not reachable from the test runner.

### 4.11 Selection panel

[Selection controls](selection-controls.md) specifies the whole panel; its release-scope section names the five changes taken in 1.27.0 and defers the rest here. These are not independent polish — take them in order.

1. **The presentation model comes first.** Until classification, content selection and element construction are separated, every other item edits the same branching builder and the changes conflict.
2. Once the model exists, the flex rebuild of region grammar and spacing, Viewing/Actions separation, one shared grammar for singular non-media and folders, and compact density for the non-singular forms are all filters or containers over it rather than new code paths.

Two items are blocked outside the panel: **summary common/`Mixed` state** needs a bounded aggregation over the selected set, which is an index-side or published-summary question — a selection of tens of thousands of items must not be walked while building a panel; and **title ellipsis** needs a trimming sign in the Direct2D text layout and `DT_END_ELLIPSIS` in the software path, so it is a draw-backend change in both `platform_win_font.cpp` and `platform_win_ui.cpp`.

**Extending comparison eligibility beyond the panel is deliberately last.** The same predicate should eventually decide the side-by-side media surface, comparison hit testing, zoom linking, and pair CRC/pixel work, all of which read `is_two()` today. Until then two selected files still display side by side while the panel summarizes, which is honest: side-by-side is a viewing arrangement, and the table is the comparability claim.

Also open: neutral difference emphasis replacing rank colour, and whether presence deserves a permanent row when browsing inside the collection, where its value is always `In collection`.

### 4.12 Locations

[locations](locations.md) retains implemented model support for the deferred Items presentations alongside the other items listed at the end of that document.

- **Items distance and visit presentations.** The distance slider and visit timeline were removed from the 1.27 Items control bar after user testing. Their control implementations, radius-bearing location terms, visit derivation and focused tests remain, so the presentations can be revised and re-enabled without rebuilding the search model. The named-place location breakdown remains visible in 1.27.
- **Altitude-derived classes.** Airborne, high-altitude and underwater presentation, search terms, counts and visit-flight segmentation were removed from 1.27; raw GPS altitude and speed extraction and indexing remain. Before reviving: validate real camera and phone metadata, define how coordinate edits affect vertical metadata, and require evidence beyond negative altitude for underwater classification — EXIF's below-sea-level reference does not prove submersion. [Locations](locations.md#28-deferred-altitude-classes) owns the durable constraints.
- **A named place count over the index.** There is no way to ask how many indexed items a place holds without running a search. That single gap defers §3.4's autocomplete counts, §3.7's ambiguous-alternatives list and its leading no-location count, §2.4's levelless stored places as first-class identities, §4.2's live count while dragging the distance slider, and the remaining per-item gazetteer lookup behind baseline defect 6. **Build the aggregation once and all five follow.** It belongs on the index side; §4.2 additionally needs the debounced in-flight search of §4.3.
- **Visit-feature polish.** §6.3's representative thumbnail and the map-bubble visit summary of §5.4 and §6.6. The derivation in `model_visits` deliberately keeps no item handle and is keyed by result set rather than by place, so both need a small addition to what it retains, not a new subsystem.
- **Water bodies — data-blocked rather than scheduled.** §2.6 and the `@offshore` class cannot ship because GeoNames publishes marine names and points but no extents. Without a land/water test, "no place within 300 km" cannot distinguish the mid-Atlantic from the Sahara. The revival path is a coarse global land bitmask, around 32 KB at half a degree, evaluated before any marine naming is attempted. `@offshore` is not accepted by the parser today, so nothing shipped advertises it.

### 4.13 Live language changes

A language change rebuilds the visible presentation, active tool controls, metadata panel, group titles and toolbar text. Two stateful cases remain deliberately snapshot-based:

- Progress and completion messages created before a switch retain the language the operation began in.
- An already-open native dialog retains the labels copied into its Win32 controls. The normal language command is unavailable while a modal is open, but an external settings-change path could still expose this.

Resolving either needs explicit language-change semantics for in-flight operation history and native dialog ownership, rather than references to mutable global text.

### 4.14 Crash reporting

Two limits in the crash handler are known and unresolved. Both are recorded in [implementation](implementation.md#crash-loop-protection); neither has a cheap fix, and both would need to be validated against a real fault rather than a `/test:` crash.

- **The handler can hang instead of reporting.** `df::log` and `df::close_log` take a non-recursive lock, so a fault raised while it is held — heap corruption inside a logging call is the plausible case — deadlocks the flush and the report. A lock-free or try-lock logging path for the handler is the obvious answer, and would change how every logging call behaves under contention, so it is not a change to make blind.
- **The upload has no timeout.** `connect_to_host` and `send_request` use the WinINet defaults, so a crash report on an unreachable network can hold the faulting thread for a minute or more with the window already hidden. Setting timeouts is a one-line change on the shared web path, which is also what makes it worth measuring first: the same call is used for update checks, map tiles and the support upload.
