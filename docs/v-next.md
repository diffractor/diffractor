# Post-1.27.0 Engineering Context

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

## 2. Invariants that must survive a re-sync or refactor

### 2.1 XMP toolkit fork divergence

The vendored Adobe XMP toolkit is its own git repository at `third-party/xmp`, on the `diffractor` branch. **Re-syncing with upstream Adobe silently re-introduces defects upstream still ships.** [File I/O](file-io.md) §11 records which of these the write pipeline depends on, and owns the toolkit options Diffractor does and does not pass.

Fixes that must be carried forward:

- **WebP chunk order.** Upstream `WEBP_Handler`/`WEBP_Support` rebuild the RIFF container by chunk *category* — VP8X, ICCP, EXIF, XMP, image data — and truncate to the new length, so any file whose physical order differs has its image or alpha data overwritten by metadata. The fork rewrites chunks in their original file order, which is why `Container` keeps an `ordered` list alongside the category map. Guarded by `Should preserve webp chunks on metadata save` in `test_media_edit.cpp`.
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
