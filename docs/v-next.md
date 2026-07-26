# Post-1.27.0 Engineering Context

GitHub issues are authoritative for backlog, status, discussion, and reporter follow-up: https://github.com/diffractor/diffractor/issues

This document records only durable context that spans issues or would otherwise be lost between releases. It is not an issue list or release plan.

## Deferred live-language edge cases

Language changes rebuild the visible presentation, active tool controls, metadata panel, group titles, and toolbar text. Two stateful cases remain deliberately snapshot-based: progress and completion messages created before a switch retain the language in which the operation began, and an already-open native dialog retains the labels copied into its Win32 controls. The normal language command is unavailable while a modal dialog is open, but an external settings-change path could still expose the latter. Resolving either case needs explicit language-change semantics for in-flight operation history and native dialog ownership rather than references to mutable global text.

## Search matching reverse index

`inverted_index`, `postings_union`, and `postings_difference` are implemented and unit-tested but are not wired into query execution.

Diffractor search supports substring, wildcard, range, and GPS matching, while the reverse index has exact-token semantics. Search also uses conservative item category-presence masks and folder OR summaries as prefilters. The reverse index must not replace the exact matcher without evidence that candidate generation remains complete.

Before integrating it:

- Profile representative queries on a real, large library.
- Use the `update_summary` vocabulary probe to measure distinct terms and occurrences.
- Preserve exact matching semantics and prove no false negatives.
- Remove probe logging after the decision.

## Video metadata follow-up

The 1.27.0 interoperability work uses XMP plus native Windows MP4/MOV and ASF fields. Native Matroska `SimpleTag` read/write remains a separate enhancement and is not part of completed #123 work. [Metadata](metadata.md) owns the current read and write limitations, including the Matroska gap.

## XMP toolkit fork divergence

The vendored Adobe XMP toolkit lives in `third-party/xmp` as its own git repository on the `diffractor` branch. Re-syncing it with upstream Adobe will silently re-introduce defects that upstream still ships; the following deliberately diverge and must be carried forward.

Upstream `WEBP_Handler` / `WEBP_Support` rebuild the RIFF container by chunk category (VP8X, then ICCP, then EXIF, then XMP, then image data) and truncate to the new length. Any file whose physical chunk order differs from that category order has its image or alpha data overwritten by metadata. The fork rewrites chunks in their original file order, which is why `Container` keeps an `ordered` list alongside the category map. Upstream also lacks `WriteTempFile`, reads the XMP packet as the packet plus a run of NULs, leaks a `Container` per `CacheFileData`, and casts a 64-bit chunk size through `XMP_Int32`. `Should preserve webp chunks on metadata save` in `test_media_edit.cpp` guards the ordering contract.

Upstream `MPEG4_Handler::ExportXtraTags` marks the box tree changed even when the rebuilt `Xtra` box is byte-identical, which forces a `moov` relocation and a whole-file rewrite of a large movie on every save. The fork returns early on an identical box.

The fork's own additions also carry constraints. `ExportTIFF_WindowsEncodedString` must not delete an XP* tag merely because the property is absent from XMP, because only `exif:XPKeywords` is maintained by `metadata_edits::apply`; clearing keywords is therefore expressed as an empty `exif:XPKeywords` value rather than a deleted property. The ASF Extended Content Description rewrite emits 16-bit name, value, and descriptor-count fields, so oversized values are dropped rather than written with a truncated length.

[File I/O](file-io.md) §11 records which of these divergences the write pipeline depends on, and owns the toolkit options Diffractor does and does not pass when writing.

## Deferred file-write optimisation

[File I/O](file-io.md) owns the shipped state. This section owns the work that was scoped out and the measurements that justify it, so neither is re-derived from scratch.

The shipped gate is a container allowlist. Every format that reaches the in-place branch has a bounded or staged fallback, and the one remaining exposure — no rollback if a bounded patch is interrupted — is documented rather than half-mitigated. The next step would size the rollback to the write rather than to the file, in three tiers: stage and replace for small files and every excluded format; patch in place behind a rollback for provably bounded writes; stage and replace for everything else. Bounded means either the new packet fits the old length, testable from public API via `SerializeToBuffer` with `kXMP_ExactPacketLength`, or the container is ISO base media.

**Tier 2 must journal the writes, not copy the packet.** An earlier draft proposed retaining the old packet bytes and offset and restoring them on failure. That is unsound and must not be implemented, because the handler does not confine its writes to the packet. On an MP4 rating, `ExportXtraTags` dirties `moov`, and `MPEG4_MetaHandler::UpdateFile` then checks `moovMgr.IsChanged()` independently of its in-place-XMP branch — so even when the packet is written back at its original offset, the same call rewrites the `moov` subtree, which may truncate, insert a `free` box, relocate, or append at EOF. A 4-byte timecode sample and `OptimizeFileLayout` also land outside the packet. Restoring a stale packet over an already-relocated `moov` converts a recoverable partial write into a reliably corrupt one. The sound form is a write journal: interpose a Diffractor `XMP_IO`, record the pre-image of every range before each `Write` and the discarded tail before each `Truncate`, and replay backwards on failure. It must implement `DeriveTemp`/`AbsorbTemp`/`DeleteTemp` for real, because ASF takes the temp route on fallback.

Client I/O is available without a fork — `XMPFiles::OpenFile(XMP_IO*, ...)` under `XMP_StaticBuild`, which `src/metadata_xmp.cpp` already defines. It is not needed for correctness, since a genuine in-place patch is already coherent, but it would supply an authoritative modified time and remove the last by-name re-open. Three constraints: the format must be passed explicitly because `DoOpenFile` asserts the client path is empty; progress tracking is disabled; and the temp trio needs a real implementation only for handlers that can take an out-of-place branch.

### What Windows Explorer actually does, measured

Explorer's rating is not a cheaper version of ours; it is a smaller write. Windows does not use the XMP toolkit. It writes `WM/SharedUserRating` into the Microsoft `Xtra` box through the Windows Property System and nothing else — no XMP packet, no reconciliation, no second representation to keep consistent. Diffractor writes both representations by choice, for interop, and pays for the second one.

Measured 2026-08-03 on x64 with `tmp/propsys_experiment.py` (`IPropertyStore` against copies of `exe/test` media, diffed byte-for-byte) and `tmp/propsys_scale.py`:

| File | Operation | Time | Size Δ | Bytes changed |
| --- | --- | --- | --- | --- |
| `indy.mp4` 50.4 MB, no `Xtra` | first rating | 359 ms¹ | +53 | 2 box-size fields; 53-byte `Xtra` appended at EOF |
| | change 3★→5★ | 6 ms | 0 | **1** |
| | same rating again | 6 ms | 0 | **0**, no write at all |
| `gizmo.mp4` 387 KB, has `Xtra` | clear | 9 ms | −45 | 978 in 45 ranges, trailing boxes shifted |
| `ipod.mov` 3.3 MB, `Xtra` + `XMP_` | clear | 29 ms | −45 | 964, `XMP_` relocated but intact |

¹ First-call COM initialisation. Steady state is ~6 ms independent of file size: 5.9 ms at 50.4 MB, 5.8 ms at 260 MB, 7.3 ms at 1.10 GB, 6.8 ms at 3.72 GB, with growth a constant 53 bytes. An earlier run appeared to show the first write scaling with size; that was an artefact of the script flushing its own dirty pages, and any future timing of a large-file write needs the same control.

So Windows is fast because the write is O(1) in the file, not because it takes a risk we decline, and it is idempotent — matching the guard already in `ExportXtraTags`. Its growth case is bounded only by luck of layout.

**Windows and XMP do not talk to each other.** `ipod.mov`'s `XMP_` box survived every operation byte-identical, and rating a file from Explorer left an injected XMP packet untouched. A file rated in Explorer can therefore carry two ratings that disagree, and `ImportXtraTags` only fills values XMP does not already carry, so Diffractor reports the stale one. That is a real interop gap and the strongest argument for continuing to write both.

**Explorer's JPEG rating is not non-destructive.** The same probe against `Test.jpg` (86,831 B) shrank Exif `APP1` from 22,194 to 13,060 B and XMP `APP1` from 3,271 to 1,743 B, leaving the entropy-coded scan byte-identical. Pixels are safe; the loss is the embedded Exif thumbnail, re-encoded from 10,610 to 3,388 B. `MakerNote` is preserved. A second rating change costs 16 bytes, so JPEG has the same expensive-first-write shape for Explorer as for the toolkit. When a user asks why Explorer rates a photo instantly and we stage a copy, we are not merely slower — we are preserving a thumbnail Explorer silently re-encodes.

**What this implies for our own tiers.** The `WM/SharedUserRating` entry is 45 bytes, confirmed by every clear-rating operation removing exactly 45, so *changing* a rating on a file that already carries one produces an identical-length `Xtra`, an unchanged `moov` size, and the same-size in-place branch. A journal is not what makes re-rating safe; re-rating is already same-extent. It is needed for the size-changing cases, which is the boundary the inject trait already draws.

Adobe is a weaker comparison than it looks: Lightroom and Premiere mostly do not write the file at all, keeping the rating in the catalog until an explicit save, so their fast path is fast by not being a file write. Bridge does write through the XMP SDK and is not fast. Deferring the write is available to us too, but it trades away the property the write path exists to protect — that the user's rating is in the user's file and not only in our database.

## Deferred zoom work

[zoom](zoom.md) §16 separates what shipped, what is outstanding for 1.27.0, and what is deferred past it. Only §16.2 is post-release; §16.1 is release work.

The four deferrals are independent of each other and none is blocked by the zoom model itself; each waits on a capability that lives outside it.

Viewport-region decode needs a source-origin-bearing tile representation before the retained whole-image decode can be replaced, so it is a loading change ([file-io.md](file-io.md)), not a zoom change. Touch hold inspect zoom and a distinct pen barrel-drag need pointer-contact timing that the current Windows gesture path does not surface. The §12 automation properties and settled-state announcements need a UI Automation provider on the custom Win32 frame, which nothing else in the product has asked for yet. Source-pixel coordinate and colour readout plus the high-magnification pixel grid are optional Study additions and were scoped out rather than blocked.

## Deferred touch work

1.27.0 ships a deliberately narrow touch surface and stops there. What works is tap to select, drag to rubber-band select, double-tap to toggle `Fit` and `100%` on a photo with the ordinary double-click as the fallback everywhere else, pinch to step the zoom ladder, and one-finger pan on a magnified image. The system gestures — press-and-hold and press-and-tap — keep their normal context-menu meaning rather than acting as a second entry into zoom mode. Everything else the release review found was either removed or documented rather than half-implemented.

The main gap is that **the item list cannot be scrolled by touch**. `items_view` leaves `pan_start`/`pan`/`pan_end` empty, so every `GID_PAN` over the list is discarded regardless of finger count: one-finger and two-finger pan are the same gesture and both do nothing, and three-finger swipes never reach the application because the Windows shell claims them. Only Fullscreen and the map consume pan. Touchpad two-finger scroll is unaffected because it arrives as `WM_MOUSEWHEEL` and the list handles wheel input correctly, so the defect is specific to touch screens. This is pre-existing rather than a 1.27.0 regression, which is why it was documented instead of fixed under release pressure.

Fixing it is not just a matter of filling in the empty overrides. A touch drag currently produces a promoted `WM_LBUTTONDOWN`/`WM_MOUSEMOVE` sequence that starts a rubber-band selection, so pan-to-scroll and marquee-select would fire simultaneously. The work needs touch-originated mouse messages suppressed — `GetMessageExtraInfo()` carries the `0xFF515700` signature — plus a separate tap-to-select path so that suppressing the promoted stream does not also remove selection. That is the same pointer-contact plumbing that the deferred zoom work above needs for touch hold inspect, so the two should be taken together rather than solved twice.

Two smaller deviations are known and deliberate. Pinch steps the discrete zoom ladder instead of scaling continuously, which departs from [zoom](zoom.md) §10 and will feel wrong to anyone who expects direct manipulation. And the gesture configuration names only the single-finger pan flags in `dwWant`, relying on the system default to leave `GC_PAN` enabled; naming it explicitly would make the intent legible and remove a dependency on default behaviour.

Before extending touch further, validate the current surface on real hardware. None of the 1.27.0 touch behaviour has hardware coverage in automated tests, and the gesture path is not reachable from the test runner.

## Deferred selection-panel work

[Selection controls](selection-controls.md) specifies the whole panel; its release-scope section names the five changes taken in 1.27.0 and the remainder deferred here. The deferred items are not independent polish, so take them in this order.

The presentation model comes first. Until classification, content selection, and element construction are separated, every other item edits the same branching builder and the changes conflict. Once the model exists, the flex rebuild of region grammar and spacing, Viewing/Actions separation, one shared grammar for singular non-media and folders, and compact density for the non-singular forms are all filters or containers over it rather than new code paths.

Two items are blocked on something outside the panel. Summary common/`Mixed` state needs a bounded aggregation over the selected set, which is an index-side or published-summary question, not a layout one; a selection of tens of thousands of items must not be walked while building a panel. Title ellipsis needs a trimming sign in the Direct2D text layout and `DT_END_ELLIPSIS` in the software path, so it is a draw-backend change in both `platform_win_font.cpp` and `platform_win_ui.cpp`.

Extending comparison eligibility beyond the panel is deliberately last. The same predicate should eventually decide the side-by-side media surface, comparison hit testing, zoom linking, and pair CRC/pixel work, all of which read `is_two()` today. Until then two selected files still display side by side while the panel summarizes, which is honest because side-by-side is a viewing arrangement and the table is the comparability claim.

Also open: neutral difference emphasis replacing rank colour, and whether presence deserves a permanent row when browsing inside the collection, where its value is always `In collection`.

## Deferred locations work

[locations](locations.md) retains implemented model support for the deferred Items presentations alongside the other items listed at the end of that document.

**Items distance and visit presentations.** The distance slider and visit timeline were removed from the 1.27 Items control bar after user testing. Their control implementations, radius-bearing location terms, visit derivation, and focused tests remain, so v-next work can revise and re-enable the presentations without rebuilding the underlying search model. The named-place location breakdown remains visible in 1.27.

**Altitude-derived classes.** Airborne, high-altitude, and underwater presentation, search terms, counts, and visit-flight segmentation were removed from 1.27. Raw GPS altitude and speed extraction and indexing remain. Before reviving the feature, validate real camera and phone metadata, define how coordinate edits affect vertical metadata, and require evidence beyond negative altitude for underwater classification: EXIF's below-sea-level reference does not prove submersion. [Locations](locations.md#28-deferred-altitude-classes) owns the durable constraints.

**A named place count over the index.** There is no way to ask how many indexed items a place holds without running a search. That single gap is what defers §3.4's autocomplete counts, §3.7's ambiguous-alternatives list and its leading no-location count, §2.4's levelless stored places as first-class identities, §4.2's live count while dragging the distance slider, and the remaining per-item gazetteer lookup in the matcher behind baseline defect 6. Build the aggregation once and all five follow. It belongs on the index side, and §4.2 additionally needs the debounced in-flight search of §4.3.

**Visit-feature polish.** §6.3's representative thumbnail and the map-bubble visit summary of §5.4 and §6.6. The derivation in `model_visits` deliberately keeps no item handle and is keyed by result set rather than by place, so both need a small addition to what it retains, not a new subsystem.

**Water bodies, which are data-blocked rather than scheduled.** §2.6 and the `@offshore` class cannot ship because GeoNames publishes marine names and points but no extents. Without a land/water test, "no place within 300 km" cannot distinguish the mid-Atlantic from the Sahara. The revival path is a coarse global land bitmask, around 32 KB at half a degree, evaluated before any marine naming is attempted. `@offshore` is not accepted by the parser today, so nothing shipped advertises it.

## Withdrawn Remove Metadata tool

The Tools command `tool_remove_metadata` was withdrawn from the UI before 1.27.0 because testers found it confusing. It was not a Diffractor operation at all: it handed the selection to the Windows shell `CLSID_RemovePropertiesDropTarget`, which opens the Explorer "Remove Properties" dialog. That dialog offers to *create a copy* with properties removed, so the command could silently produce new files in the folder, reported nothing back, wrote through a path that bypasses [file I/O](file-io.md) entirely, and stripped whatever Windows considers a property rather than the fields Diffractor shows.

Removed with it: the command enum value, its menu and toolbar entries, its enable and disabled-reason wiring, `platform::remove_metadata`, and feature bit 46. `tt.remove_metadata_title` is deliberately retained so the existing translations in `exe/languages/*.po` survive the gap.

Reinstating it means writing it as a real Diffractor task rather than a shell hand-off: a selected-set operation with preview before run, an explicit statement of which fields it clears, collision and retained-original rules matching the other destructive tools, and writes through the normal metadata write pipeline. [Metadata](metadata.md) owns which fields are writable, so the field set it can honestly claim to clear is bounded by that document.

## Deferred metadata work

A correctness pass over `metadata_exif`, `metadata_iptc`, `metadata_xmp`, and `metadata_icc` fixed encoding, clobbering, validation, and display defects. The items below were found by the same pass and deliberately left, because each changes data users already see rather than correcting something that is wrong.

**Unmapped IPTC datasets.** `IPTC_TAG_HEADLINE`, `SUBLOCATION`, `DATE_CREATED`/`TIME_CREATED`, `SPECIAL_INSTRUCTIONS`, and `WRITER_EDITOR` are parsed and shown in the properties panel but are not mapped into `prop::item_metadata`. Mapping them is not additive: Headline would compete with the existing title source, and DateCreated with `created_exif`, so the parse order in `file_scan_result::to_props` — Exif, then Iptc, then ffmpeg, then Xmp — decides whose value wins. Any mapping needs that precedence stated first, and needs to preserve the rule that a later source with no value never erases an earlier one.

**IPTC record 1 is never surfaced.** Both `parse` and `to_info` resync by scanning for the `0x1c 0x02` marker, so only application record 2 is read. Record 1 carries the envelope, including the 1:90 coded character set escape that would remove the need for the Latin-1 heuristic in `decode_iptc_text`. Reading the envelope properly would let encoding be determined rather than guessed, but it means restructuring both loops to walk records generically, which is a larger change than the heuristic it would replace.

**Sidecar packet wrapper.** `metadata_xmp::update` writes sidecars with `kXMP_OmitPacketWrapper`, so the file has no `<?xpacket?>` header or trailer. Adobe writes the wrapper, and some readers scan for it. Adding it changes the bytes of every sidecar Diffractor has already written, so it needs a compatibility check against the readers that matter before it is worth doing.

**Inconsistent exception handling in the XMP writers.** `metadata_xmp::update(std::string&, ...)` catches only `XMP_Error` where its path-taking sibling also catches `std::exception`. Both propagate rather than swallow, so this is a consistency question, not a defect.

**`dc:creator` round trip is lossy for multiple names.** `xmp_load_array` joins array items with a space, so a two-creator packet presents as one string in the editor, and writing it back with `str::is_artist_separator` collapses it to a single creator. Preserving multiple creators needs the editor to model the field as a list rather than as joined text.

## Deferred colour management

ICC profiles are read, preserved byte-for-byte on save, and parsed for the properties panel by `metadata_icc::to_info`. They are never applied. skcms is vendored, built, and linked into every configuration, but nothing in `src/` calls it, so a wide-gamut file is displayed as though its numbers were sRGB — an Adobe RGB or ProPhoto photo renders *desaturated*, not oversaturated, because larger primaries are being interpreted against smaller ones. [Metadata](metadata.md) owns profile parsing and what the panel shows; [rendering](rendering.md) owns the display pipeline. This section owns only the constraints that span them.

**Three implementations must agree, and the divergence is the real cost.** Colour reaches the screen through the HLSL shaders in `src/shaders/` driven by `platform_win_d3d11.cpp`, through the CPU preview in `platform_win_software.cpp`, and through `ui::surface::transform` in `render_surface.cpp` for what is written to disk. A transform added to one and not the others makes the software renderer disagree with the GPU renderer and makes the saved file disagree with both. Any pipeline-level change is therefore three changes plus the evidence that they match.

`ui::color_space` in `ui.h` is not the extension point, despite the name. It selects the YUV→RGB matrix and signal range for NV12/P010 and nothing else. Source primaries and transfer function are a separate concept that the surface does not currently carry, and adding them is the first structural step.

**Matrix-shaper profiles are GPU-friendly; A2B profiles are not.** sRGB, Adobe RGB, Display P3, ProPhoto and Rec.2020 — which is very nearly all display-referred content — decompose into a parametric source transfer function, a 3×3 matrix to a connection space, and a destination transfer function. That is one matrix multiply and two curve evaluations, evaluable analytically in a shader with no LUT texture at all, since `skcms_TransferFunction` is the standard seven-parameter form. LUT-based A2B profiles, which is where CMYK, scanner and printer profiles land, need a 3D LUT and should fall back to a CPU skcms transform at decode time rather than growing a second shader path. `skcms_Parse` distinguishes the two, so the split is decidable at load.

**Where the transform must happen is not a free choice.** Display and preview can defer to the render pipeline, which makes zoom and pan free. Editing and saving cannot: the written file has to be correct regardless of what the display did, so those need a CPU transform at decode and encode time. `file_load_result::to_surface` already defaults `can_use_yuv = false`, so the edit and save path takes the full-chroma RGB decode and is the correct hook. Thumbnails cannot defer either — cached thumbnail pixels carry no profile, so an unconverted thumbnail is permanently wrong and inconsistent with its own preview.

That last point has a migration cost that must be planned rather than discovered. Thumbnails are persisted in SQLite by `model_db`, so baking a transform into them invalidates every cached thumbnail for a wide-gamut file. Whether that is handled by a thumbnail-cache version bump or by leaving existing thumbnails stale until re-scanned is a decision to take before the work starts, not after.

**Cost is unmeasured and must not be assumed.** Parsing a profile is negligible and already happens. Transforming pixels is the exposure, and its magnitude for this codebase has not been measured — skcms ships AVX2 and AVX-512 specialisations, but the only number that matters is a benchmark of the real decode path before and after, at full resolution and at thumbnail scale, not a figure carried over from another project. The mitigating structure is already present: `files_jpeg.cpp` decodes scaled via `scale_num`/`scale_denom`, so cost tracks output pixels rather than source pixels, and the exposure is confined to full-resolution viewing and editing. Per-row-band work on the existing queues is the obvious lever if it is needed, but only after the measurement says it is.

**The destination is a platform question.** Without the system display profile the only defensible destination is sRGB, which is wrong on any wide-gamut or HDR monitor. Windows exposes it through `WcsGetDefaultColorProfile` and `GetICMProfile` per monitor, and `IDXGIOutput6::GetDesc1` for advanced-colour characteristics. Per [AGENTS.md](../AGENTS.md) that code lives only in `platform*.*` and reaches the rest of the product through an abstraction. It also has to be re-read when the window moves between monitors, which is behaviour rather than plumbing.

**Order matters if this is taken in stages.** Converting to sRGB at decode time for matrix-shaper profiles only, leaving LUT profiles untouched, is small, testable with the banded-fixture approach already used by `should_decode_bands` in `test_media_edit.cpp`, and fixes the visible defect on its own. Moving the transform into the shader and software renderers is second, keeping the CPU path for edit, save and thumbnails. Reading the system profile and targeting it instead of assuming sRGB is third, and is the only stage that needs new platform surface.

### HEIF depth reduction and NCLX

HEIF states its colour in either of two ways, and Diffractor currently drops both. `files_heif.cpp` extracts a `prof`/`rICC` profile into `result.icc`, where it joins the never-applied path above. It also detects `heif_color_profile_type_nclx` and names it in the properties panel as "nclx (coded primaries and transfer)" — and then does nothing with it. NCLX is not a lesser signal: it carries coded primaries, transfer characteristics and matrix coefficients, and it is what a Rec.2020 or PQ HEIF from a phone actually ships instead of an ICC profile. A file with NCLX and no ICC therefore gets no colour handling from either mechanism. Whatever design the ICC work settles on has to accept both sources, or HEIF gets fixed twice.

Separately, both `heif_decode_image` calls — the thumbnail and the full image — request `heif_colorspace_RGB` with `heif_chroma_interleaved_RGBA`, which is 8-bit. A 10-bit HEIF is therefore reduced to 8 bits inside libheif, under whatever policy that version happens to implement, with no rounding or dithering choice exposed and no test asserting the result. The panel meanwhile reports the true luma and chroma depth from `heif_image_handle_get_luma_bits_per_pixel`, so it can honestly say "10 bits" about pixels that were delivered as 8. That inconsistency is the visible symptom; the underlying question is whether the decode should request a wide interleaved format and do its own reduction, as `files_jpeg.cpp` now does for 12- and 16-bit JPEG, which would also be the precondition for ever displaying HEIF at more than 8 bits.

## Deferred audio clock re-anchoring

The master clock for a session with audio is the device, and `av_player.h` anchors it **once** per buffer generation: `base_time` is taken from `audio_buffer::start_time()` when the generation changes, and every later reading is `base_time + ds->time()`, where `ds->time()` is a played-sample count. Video frames are then matched against that. Everything the 1.27 timestamp work fixed — one common time origin for both streams, `pkt_timebase` so FFmpeg can compensate encoder delay, `best_effort_timestamp` — corrects the timestamps *entering* that clock, not the clock itself.

The residual defect is a genuine discontinuity in the audio stream. If a file has a gap in its audio PTS with no samples behind it, the device plays fewer samples than the timeline advanced, so `base_time + ds->time()` lags the real content by the size of the gap and video is held back by that much for the rest of the file. `audio_buffer::append` re-derives `time` from each incoming frame's timestamp, so `start_time()`/`end_time()` do follow the discontinuity — but `base_time` was sampled once and does not, which is also why `_audio_data_end` (captured at EOF from `end_time()`) can drift out of reach of `_audio_clock` and leave end-of-clip detection on its two-second fallback.

The sound form is what ffplay does: re-anchor on every audio frame handed to the device, from that frame's own timestamp minus the amount of it not yet played out. That needs a "seconds still queued in the device" accessor on `av_audio_device`, which is platform surface (`platform_win_sound.cpp` and the `wasapi_sound` ring) rather than a change inside `av_player.h`, and it changes the clock every session uses. It was scoped out of the timestamp work deliberately: the rest of that work is verifiable against the decoder's own output, and this is not — it needs files with real audio discontinuities to test against, and the corpus in `exe/test` has none.

Two constraints carry forward. Re-anchoring must not fight the priming: the device is deliberately not started until `playback_buffer.seconds() >= 0.3`, so an anchor taken before the first `start()` is meaningless and the first valid anchor is the first write after the device is running. And the anchor must stay monotonic across the silence pad appended at EOF, or `has_ended` regains the early-end behaviour the pad exists to prevent.

