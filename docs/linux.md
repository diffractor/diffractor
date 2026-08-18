# Linux port

This document owns the Linux port: whether it is realistic, what the platform boundary
actually costs to re-implement, which Windows assumptions still sit in portable code, and
the order in which the work can be done so that each stage is provable on its own.

It does not restate architecture, threading, or ownership — those belong to
[implementation.md](implementation.md). It does not restate the backend parity contract —
that belongs to [rendering.md](rendering.md). User-facing behavior belongs to
[design.md](design.md); where a Linux difference would change what a user can predict,
this document names the decision and defers the answer there.

## Current state

The portable core builds and passes its full test suite on Linux, headless, in both Debug and
Release. What exists is the build, the core, and the file and settings half of the platform
layer. What does not exist is a window, a renderer surface, text shaping, or any desktop
integration — everything from [Stage 2](#staged-plan) onward.

| | Windows | Linux |
|---|---|---|
| Tests | 754 Debug (CMake/Ninja), 760 Release (MSBuild) | **736, Debug and Release** |
| Build | CMake + Ninja, MSVC | CMake + Ninja, GCC 13 |
| Vendored libraries | 26 | **26, all building under GCC** |
| Release binary | 40 MB | 94 MB |

The test-count difference is not a coverage gap. Six tests are guarded `#ifndef _DEBUG`, and
`_DEBUG` is an MSVC macro that GCC never defines, so Linux runs them in *both* configurations
while the Windows Debug build skips them. Those six include the five decoder-robustness tests,
which feed malformed input through the shared decode path.

The Linux platform layer is about 2,400 lines across five files, against `platform_win*`'s
25,180:

| File | What it covers |
|---|---|
| `platform_linux.cpp` | Process entry, locks, events, known folders (XDG), dates, locale, SIMD detection |
| `platform_linux_files.cpp` | I/O, attributes, enumeration, copy/move/delete, temp files, mapping, NFC normalisation |
| `platform_linux_desktop.cpp` | Delete, move and copy as real operations; the rest of the shell surface stands in |
| `platform_linux_settings.cpp` | The INI backend, which is the only one here |
| `platform_linux_ui.cpp` | Style palette, key table, UI-thread identity — no window, no message loop |

The FFmpeg and XMP forks once had stand-in translation units for a build configured without them.
Both forks build under GCC, and an application that reports every media file and every XMP
property as absent is not Diffractor, so they are now required dependencies: configuration fails
when either is missing rather than producing a degraded binary.

**What is done beyond the build:** indexing, search, metadata, the decode ladder, ratings,
renaming, collections and the location index all pass on Linux; delete, move and copy are real
operations with collision renaming; and `platform::image_to_surface` — the WIC entry point — is
gone from both platforms, replaced by `av_decode_still` in [av_format.cpp](../src/av_format.cpp),
so there is now one image decoder rather than one per platform.

**What is unchanged:** part of
[Windows assumptions still in portable code](#windows-assumptions-still-in-portable-code) is
still open. The port fixed the specific defects those assumptions caused — a mis-decoded EXIF
string, a truncated coordinate — without always removing the assumption itself. What remains is
the last of the `std::wstring` in the platform interface and the small residue listed there. Path
identity is no longer among them: it now follows the filesystem, case-sensitive on Linux and
unchanged on Windows. The [decisions](#decisions) that blocked this work are answered, so what is
left is implementation rather than deliberation.

## Verdict

The port is realistic and the boundary is genuinely clean. Two of the three findings that
originally drove that assessment still hold; the third has been resolved.

1. **The boundary holds.** `platform_win.h` is included by `platform_win*.cpp` and
   `test_platform_win.cpp` and by nothing else. A search of `src/` for Win32 types
   (`HWND`, `HRESULT`, `windows.h`, `IUnknown`, `WINAPI`, `_WIN32`) finds essentially
   nothing outside `platform_win*` — the exceptions are listed in
   [Windows assumptions still in portable code](#windows-assumptions-still-in-portable-code)
   and are all small. This is the expensive property to establish and it was already there;
   the headless Linux build is the proof.

2. **The split is favourable but the platform layer is dense.** `platform_win*` is
   ~25,200 lines against ~147,700 lines elsewhere: about 15% of `src/`. But it is not 15%
   of the effort, because a large part of it is a Windows *desktop integration* surface
   (shell, DirectWrite, WASAPI, D3D11) where the Linux replacement is a different
   library with a different model, not a renamed call. The 2,400 lines of `platform_linux*`
   written so far cover the cheap half.

3. ~~**The build system is the underestimated cost.**~~ **Resolved.** It was the largest single
   piece of work in the port, and it is done: CMake describes the whole tree and generates for
   both platforms, and all 26 vendored libraries build under GCC. See
   [build system](#build-system). The [third-party policy](third-party.md) has been updated to
   match.

The realistic shape of what remains is: a renderer that has an unusually good migration path
because the CPU backend already exists, a text and window layer that is new code, and a long
tail of Windows shell features that need product decisions rather than ports.

## What each layer costs

The platform layer, largest first, with what replaces it:

| File | LOC | What it is | Linux replacement | Difficulty |
|---|---|---|---|---|
| `platform_win_ui.cpp` | 7,678 | Window class, message loop, native controls, dialogs, menus, clipboard, drag/drop, DPI, monitors, `wWinMain` | SDL3 for window/input/clipboard/DnD/file dialogs, plus the drawn controls from [Stage 0b](#staged-plan) | **High** — the single biggest item |
| `platform_win_d3d11.cpp` | 2,976 | GPU backend, batching, glyph atlas, video textures | Vulkan or OpenGL 3.3, plus 12 HLSL shaders → GLSL/SPIR-V | High |
| `platform_win_files.cpp` | 2,896 | File I/O, attributes, enumeration, shell copy/move/delete, thumbnails, shell tags | POSIX I/O + XDG trash, with the portal variant for Flatpak | Medium, with feature gaps |
| `platform_win_software.cpp` | 1,596 | GDI present, DirectWrite text, the retained scene | The rasterizer already moved out; what is left is the present target and text | **Low** — see below |
| `platform_win.cpp` | 1,678 | Locks, events, known folders, dates, number/locale format, drives, eject | pthreads/futex, XDG base dirs, `std::chrono`, ICU or `std::locale` | Low–medium |
| `platform_win_font.cpp` | 831 | DirectWrite faces, fallback, metrics | FreeType + HarfBuzz + fontconfig | Medium–high |
| `platform_win_sound.cpp` | 598 | WASAPI render client | PipeWire (or PulseAudio/ALSA) | Medium |
| `platform_win_wic.cpp` | 523 | WIC encode for clipboard and save | Decoding already left: `image_to_surface` is gone and both platforms use `av_decode_still` | **Done, for decode** |
| `platform_win_settings.cpp` | 452 | Registry and INI stores | INI store already exists and is portable in shape | **Low** |
| `platform_win_web.cpp` | 330 | WinInet HTTP client | libcurl | Low |

Two of these are much cheaper than their size suggests:

- **The software backend is the shortest route to pixels, and the rasterizer is now portable
  code.** It renders the same retained scene as the GPU backend and honours the same parity
  contract; only its final blit was ever Windows. `software_canvas` and the pixel helpers live in
  [render_software.h](../src/render_software.h) and name nothing of any platform — including of a
  font, since `blend_glyph` takes an 8-bit coverage bitmap and leaves what produced it to the text
  layer. `ui::software_present_target` is the split: the rasterizer asks for a BGRA buffer, walks
  its tile across the damaged region and hands each finished tile back, and `gdi_present_target`
  in [platform_win_software.cpp](../src/platform_win_software.cpp) is the only part of the CPU
  backend that names a `HWND`, a `HDC` or a DIB section. `create_memory_present_target` presents
  nothing, which is what a render with no window targets. What remains between here and Stage 2 is
  a Linux window that owns a BGRA buffer, and text: `ui::draw_context_device` is portable now — its
  `render()` answers a `ui::present_result` carrying a backend code rather than an `HRESULT` — so
  the last thing holding `software_draw_context` on Windows is DirectWrite.
- **Settings already have a portable backend.** `create_ini_file_settings()` exists
  alongside the registry store and is selected at runtime, so Linux persistence is a
  configuration decision, not new code.

One item is cheaper still: `ui::web_window` and `ui::web_events` are declared in
[ui.h](../src/ui.h) but have no implementation and no callers. There is no embedded
browser to port.

## Windows assumptions still in portable code

These are the real blockers. The headless Linux build works around the specific defects they
caused rather than removing the assumption, so each is still owed. All are `Internal` changes
under the [pre-flight gate](../AGENTS.md#mandatory-pre-flight-validation): no observable behavior
changes, and `.\dd.ps1 test` proves it on Windows.

**Path identity follows the filesystem. Done.**
[util_path.h](../src/util_path.h) is not a platform file, but it encodes path spelling and path
identity for both platforms. Spelling was already conditional: `windows_path_semantics` selects the
separator, and `is_path` and `is_root` branch on it so a drive letter means nothing off Windows.
`case_insensitive_path_identity` now joins it, and `is_illegal_name` applies the Win32 reserved
character set only where it is reserved — off Windows every byte but the separator is a legal name,
and refusing one would decline to name files that already exist beside it.

Case-insensitivity was load-bearing, and deeper than `folder_path::compare` and `file_path::icmp`
suggested, because it was baked into the **hash** rather than only the comparison.
[util_strings.h](../src/util_strings.h) stores a case-folded `ihash` inside each interned string
record, computed once at intern time; [df::ihash](../src/util_map.h) read that value and `df::ieq`
compared with `icmp`. Every index structure is keyed on that pair — `items_by_folder_t`,
`index_item_info_map` and `index_folder_info_map` in [model_items.h](../src/model_items.h), the
term map in [model_postings.h](../src/model_postings.h), the collision and dedup sets in
[app_util.cpp](../src/app_util.cpp), and the crash database in
[util_crash_files_db.h](../src/util_crash_files_db.h).

That ruled out a per-mount rule, which is otherwise the theoretically correct answer. A hash
functor receives only the key, so making the rule depend on the path's location would mean
consulting the filesystem inside every hash probe — on paths that run on the UI thread and under
the index lock.

**The answer taken is a compile-time property:** case-sensitive comparison and hashing on Linux,
unchanged on Windows. The decisive argument was that no existing data is at risk in either
direction — Linux has no index yet, and Windows keeps its current rule — so unlike a runtime rule
this one never has to be right about a database that already exists. It is also what a Linux user
expects, since every other tool on the system reports `Photo.JPG` and `photo.jpg` as two files, and
an index that merges them targets the wrong file on a rename or a delete.

The stored hash did not have to change, and no second hash had to be stored beside it. Interning is
exact, so one id is one spelling: where identity is case-sensitive it *is* id equality, which makes
the id its own perfect hash and makes the comparison free. `path_text_hash` and `compare_path_text`
are that pair, and `df::ihash` and `df::ieq` reach them for their `file_path` and `folder_path`
overloads only — their text overloads still fold case, because they answer questions about type and
vocabulary rather than about which file this is. Ordering still folds case on both platforms; the
exact comparison only breaks a tie between two spellings of one name, so a case-sensitive listing
stays in the order a reader expects rather than putting every capital ahead of every lower-case
name.

Two things made the residual risk much smaller than it looked:

- **Identity and type are different questions, and only identity follows the filesystem.**
  "Is this the same file?" must match the filesystem; "is this a JPEG?" must stay
  case-insensitive on both platforms. `file_type_by_extension` in
  [av_format.h](../src/av_format.h) and `file_group_by_name` in [files.h](../src/files.h) are
  keyed on `std::string_view` rather than on a path, so they were correct as they stood and were
  deliberately not swept along. Sidecar matching is the same question and stays folded too.
- **A case-sensitive index over a case-insensitive mount can only disagree where a path is
  constructed rather than enumerated**, because both spellings of one entry can never be
  enumerated. That bounds the exposure to sidecar and extension lookups, user-typed paths, and
  paths persisted in collections and saved searches.

The one place a mount's own rule is authoritative is a write, and collision checks ask the
filesystem directly rather than the index — which is correct on both platforms anyway, since the
answer has to hold at the moment of the write.

A path spelled as a string rather than parsed needed the same rule, and this is where it was
easiest to miss: the rename planner's `vacated`, `wanted` and duplicate-destination maps, the
parked-path map the run uses to free a destination, sync's relative-folder maps, and
`check_overwrite`'s enumeration of the destination folder were all `df::iless`, which folds
unconditionally. They are `df::path_key_less` now. Two behaviors follow from it directly: a
case-only rename vacates the name it renames away from on Linux and does not on Windows, and the
temporary-file dance `move_rename_path` performs for a case-only rename is Windows-only, because
elsewhere the two names are two paths and a plain move is correct.

**`wchar_t` is assumed to be UTF-16.** It is 16-bit on Windows and 32-bit on Linux, so this
miscompiles or silently mis-decodes rather than failing to build. The sweep is done. Icon code
points are `char32_t` and reach the drawing layer as UTF-8 through `icon_to_utf8`;
[metadata_exif.cpp](../src/metadata_exif.cpp) and [metadata_icc.cpp](../src/metadata_icc.cpp) read
their genuinely UTF-16 payloads as `char16_t`; and the character predicates behind `str::split` —
`is_quote`, `is_separator`, `is_artist_separator`, `is_genre_separator`, `is_white_space`,
`is_slash` and `df::is_path_sep` — take `char`, which is what a UTF-8 `string_view` was always
handing them. That last one was not cosmetic: promoting a `char` to `wchar_t` turns byte 0xC3 into
65475 on Windows and −61 on Linux, and `is_range_separator` was passing that to `iswpunct`, where a
negative argument collides with `WEOF`.

What the gate finds now is 9 files rather than 28, and none of them is a defect.
[util_strings.h](../src/util_strings.h), [util_strings.cpp](../src/util_strings.cpp),
[test.h](../src/test.h) and [test_util.cpp](../src/test_util.cpp) are the UTF-16 boundary itself and
the tests that pin it; [test_platform_win.cpp](../src/test_platform_win.cpp) is the Windows-only
test file; [platform.h](../src/platform.h) declares `native_path`, which *is* the native type, and
the OS's own conversion pair, which nothing in the app calls and which exists so a test can check
ours against an implementation we did not write; and the last two are a comment and the libarchive
entry point `native_path` already resolves.

What is gone since: the wide `s_app_name_l`, which had three Win32 call sites and now converts the
narrow constant at each; and the `wstring_view` conveniences on `folder_path`, `file_path` and
`df::ieq`. Those last had one live caller between them and a dozen implicit ones, all in
`platform_win*`, so removing them turned an invisible conversion into `to_file_path` and
`to_folder_path` in [platform_win.h](../src/platform_win.h) — named once, on the side of the
boundary with a reason to know about it.

**`std::wstring` is out of the platform interface.** `to_file_system_path` now answers
`platform::native_path` — UTF-16 with the `\\?\` prefix on Windows, the UTF-8 bytes elsewhere —
which portable callers pass straight to a native API without inspecting. That removed the `#ifdef`
from [files_raw.cpp](../src/files_raw.cpp) and made the Linux implementation honest, where it had
been converting UTF-8 into a 32-bit `wstring` nothing wanted. `to_utf8_file_system_path` covers the
third-party libraries that take a byte path everywhere, replacing a UTF-8 → UTF-16 → UTF-8 round
trip in [metadata_xmp.cpp](../src/metadata_xmp.cpp). `to_shell_path` and the drag-data-object probe
are now declared in [platform_win.h](../src/platform_win.h), where they belong: neither has a
cross-platform meaning, and neither had a caller outside `platform_win*`.

One caller needed a per-platform answer rather than a type change, and it is settled:
[files_core.cpp](../src/files_core.cpp) opens an archive through libarchive, whose two entry points
differ in more than their character type. The wide one is what carries the `\\?\` prefix a long
Windows path needs; a POSIX path is bytes all the way down and has no wide form worth converting
to. Overloading a file-local helper on `platform::native_path` picks the right one with no
conditional at the call site — which matters because `native_path` had already made the old
unconditional `archive_read_open_filename_w` fail to compile off Windows.

**D3D11VA no longer leaks into the media layer.** [av_format.cpp](../src/av_format.cpp) asks
`av_platform_hw_decode_target()` for the device type and pixel format the renderer can present,
and installs a hwaccel only for that exact pair; [platform_win_d3d11.cpp](../src/platform_win_d3d11.cpp)
answers D3D11VA, and [platform_linux.cpp](../src/platform_linux.cpp) answers none until there is
a backend that can import an NV12 surface. That is where the VAAPI branch goes. The tests for
whether a decoded frame is a hardware frame now ask `hw_frames_ctx` rather than naming a pixel
format, which is both platform-free and what FFmpeg means by the question. One reference remains:
`av_get_d3d_info` hands a `ID3D11Texture2D*` to the D3D renderer, which is what the function is
for, and it cannot move to `platform_win_d3d11.cpp` while `av_frame` is defined in the .cpp.

**Small residue.** The `error_atl_direct3d` string id in [app_text.h](../src/app_text.h) names a
Windows technology in user-visible text. The `__declspec(selectany)` that was here is gone.

### Two that compile clean on both and are wrong on one

Neither of these fails to build, and neither is visible in a diff. They are recorded because
each was found only by a failing test, and each had already been written more than once.

**`abs()` on a floating-point value is `int abs(int)` under GCC.** MSVC puts the floating
overloads in the global namespace, so unqualified `abs` on a `float` or `double` is silently
correct there and truncates here. It has bitten three times: a GPS coordinate of 40.71417
became 40; a colour distance summed as integers; and an alpha fade of 0.667 became 0, compared
under its 0.001 completion threshold, so every fade on Linux finished in one step. Qualify it
`std::abs`. `df::round` returns `int32_t`, so `abs(df::round(x))` is not an instance.

**A clock is only as useful as the timestamps it is compared against.** `platform::now()`
truncated to whole seconds while file times carry the nanoseconds ext4 records, so a file
written during the current second was later than the scan that had just read it and
`needs_scan` said yes forever. Anything producing a `df::date_t` must carry the same 100ns tick
the file times do.

## Features with no Linux equivalent

These are product decisions, not ports. The governing answer is now settled: **anything without
an obvious Linux equivalent is dropped on Linux and continues unchanged on Windows**, and a
dropped feature waits for a user to ask for it rather than being guessed at. Two consequences
follow, and both belong to [design.md](design.md):

- **Capability queries become runtime rather than compile-time.** Whether trash, wallpaper or
  show-in-file-browser works depends on the portal backend present at run time, not on what was
  linked. The `can_recycle`-style queries the app already has are the right shape for this.
- **The UI hides an unavailable affordance rather than failing it.** A command that is present
  and errors is a defect report; a command that is absent is a feature request, which is the
  feedback this policy is trying to collect.

Feature by feature:

- **Recoverable deletion uses the XDG trash specification.** `delete_items(allow_undo)` and
  `can_recycle` are the backbone of recoverable deletion, and the answer is that a Linux user
  gets what every other Linux application gives them: Delete moves to the trash, Shift+Delete
  deletes permanently after confirmation, and the file appears in the desktop's own Trash and
  restores to its original location from there. That last part is why the specification's
  `.trashinfo` record — original path and deletion time, written alongside the file — is not
  optional; without it the file is merely in a folder. Because trashing is a rename, it is
  confined to one filesystem, so every other volume has its own `.Trash-$uid` at its mount root
  and a cross-volume delete offers permanent deletion rather than silently copying. `can_recycle`
  therefore becomes a real runtime query, answering false for read-only mounts, some network
  mounts, and any volume with no writable trash directory. Two implementations sit behind one
  platform function: the specification directly, which needs no desktop session, and
  `org.freedesktop.portal.Trash`, which is the only route available inside the recommended
  Flatpak.
- **Shell metadata and thumbnails.** `read_shell_metadata`, `write_shell_tags`,
  `get_cached_file_properties` and `get_shell_thumbnail` depend on Windows property
  handlers, including the cloud-placeholder path that fetches a thumbnail without
  hydrating a file. There is no Linux counterpart, so these are dropped: the app's own metadata
  and thumbnail pipeline covers these cases on Linux outright.
- **Cloud placeholders.** OneDrive Files On-Demand offline detection has no direct
  analogue and is dropped. The `test_offline_predicate` seam survives; the real detection does
  not.
- **Shell verbs.** `set_desktop_wallpaper`, `show_file_properties`, `print`, `has_burner` /
  `burn_to_cd`, `scan` (WIA), `assoc_handlers`, `show_in_file_browser`, `resolve_link`
  (`.lnk`), `eject`, `scan_drives`. All dropped on Linux for now. A few have obvious portal
  equivalents and may return on request; `resolve_link` is meaningless off Windows and will not.
- **Registry and crash guards.** `create_registry_settings` and the crash-guard markers
  move to the INI store; the guard semantics themselves are portable.
- **Minidumps.** DbgHelp crash capture has no equivalent; a Linux port needs a different
  crash-report format and a different reader for
  [crash-dump analysis](../src/util_crash_files_db.h).
- **Segoe MDL2 icons — replaced on both platforms by Fluent UI System Icons. Done.** Segoe MDL2
  Assets and its Windows 11 successor Segoe Fluent Icons are Windows system fonts whose licence
  does not permit redistribution to another platform, so this could never have been a Linux build
  option. It was also not a Windows one: `segmdl2.ttf` was checked in and embedded as `IDF_ICONS`
  in the shipped binary, which is the same problem a year earlier. Microsoft's own
  [Fluent UI System Icons](https://github.com/microsoft/fluentui-system-icons) are MIT-licensed
  and share the design language, so they change the picture least while being redistributable.

  The swap was cheaper than expected because the bundling already existed: the font is loaded from
  a resource through a custom DirectWrite collection in
  [platform_win_font.cpp](../src/platform_win_font.cpp), so only the file, the family name and the
  code points changed. One gap had to be closed — `create_segoe_md2_icon` laid out its glyph
  against the *system* collection, which worked only because the old font was a system font; it now
  takes the bundled collection through `create_icon_font_collection`.

  The 148 code points in [app_icons.h](../src/app_icons.h) are generated by
  [tools/fluent_icons.py](../tools/fluent_icons.py) from the font's own JSON. Names are the
  reviewable artifact; a code point is never hand-edited, because the font build assigns them
  sequentially and they move between releases.

  Five of the old entries were deleted rather than mapped, all with no call site anywhere:
  `facebook`, `flickr` and `twitter`, because Fluent carries no third-party brand marks, and
  `tape` and `move_to`. Two substitutions are judgement calls — `disk` becomes `storage` because
  Fluent has no cassette or platter, and `sdcard` becomes `sim`. Two are strictly better than what
  the old font could express: `rotate_anticlockwise` was the clockwise glyph with a `| 0x10000`
  flag meaning "draw it mirrored", and `edit_cut` and `edit_copy` shared a code point, so cut and
  copy drew identically.

  Fluent has three speaker levels where Segoe had four, so one volume pair has to collapse. It
  collapses at the loud end, because telling silent from quiet carries more than telling loud from
  louder. `cancel` and `close` also share a glyph now; both were already near-identical crosses.
  Those two are the only icons that lost a distinction, and both were checked by diffing the old
  and new enums for pairs that became identical — which is how three real collisions were caught
  and fixed before they shipped: `audio` against `volume3`, `volume0` against `volume1`, and
  `move_to_folder` against `next_folder`, the last of which would have drawn a file move exactly
  like folder navigation.

  Still owed: subsetting the font, which carries about 4,000 glyphs where 150 are used and costs
  roughly 1.5 MB against a 200 KB predecessor.

  Two things fell out of the swap. Fluent draws smaller within the em than Segoe did — across the
  icons this app actually uses, 6.8% shorter and 9.4% narrower — so the icon em is raised by 11/10,
  which is the most that is safe: it makes the tallest Fluent glyph exactly as tall as the tallest
  Segoe one was, so nothing can overflow a box that did not already overflow. And the mirroring
  path is gone: `icon_is_mirrored`, `ui::draw_context::draw_text_mirrored` and the
  `_horizontal_mirror` plumbing in both backends existed only to draw `rotate_clockwise` backwards,
  and the bundled font has a real counterclockwise glyph.
- **Packaging.** The MSIX/WinStore configuration and the NSIS installer have no Linux
  counterpart; see [Distributions and delivery](#distributions-and-delivery).

## Build system

CMake describes the whole tree and generates for both platforms: one target for `src/`, and one
module per vendored library under `cmake/vendored/`. Anything a module needs that its origin could
not express — a header the build generates, a flag a different compiler needs, an architecture the
import never saw — lives beside it in `<name>.local.cmake`. All 26 vendored libraries build under
GCC, and a fully vendored build is the default: `DIFFRACTOR_PREFER_SYSTEM` is `OFF`, so a system
package is a way to bring a new platform up rather than something a release picks up by accident.

This is now the only description of the tree. The modules began as imports from the `.vcxproj`
files, which [retiring MSBuild](#retiring-msbuild) deleted in 1.27.2; the [third-party
policy](third-party.md) is written against CMake accordingly.

Two modules had never been built off Windows and only failed once vendoring was forced: sqlite
states `SQLITE_WIN32_MALLOC` unconditionally, and off Windows the allocator behind it compiles to
nothing, leaving `sqlite3MemSetDefault` undefined; expat had `HAVE_GETRANDOM` defined but not
`random_getrandom.c` compiled, so the define moved the call site without supplying the callee.
Both are `.local.cmake` deltas.

The build also stages the runtime layout beside the binary. Fixtures, translation catalogs,
dictionaries and the gazetteer are all found relative to
`platform::known_folder::running_app_folder`, which is the install layout and `exe/` in this tree.
On Windows the binary is linked into `exe/` directly, under the name the installer and the package
expect. Elsewhere it stays in the build directory and those entries are symlinked beside it —
symlinked rather than copied, because `exe/test` alone is around 100 MB and a copy would go stale
the moment a fixture changed, and not on Windows because a directory symlink there needs a
privilege an ordinary build does not have.

The two forks are the exception to "one module per library". `diffractor/XMP-Toolkit-SDK` builds
as a module like the rest; `diffractor/FFmpeg` does not, because restating which codecs exist
would be a second source of truth for it. On Windows it compiles from a source list imported from
the fork's own project file, against a `config-x64.h` or `config-x86.h` that is checked in
precisely because `configure` needs a shell; on Linux it is an `ExternalProject` running the
fork's own `configure` and `make`.

### Retiring MSBuild

**Done, in 1.27.2.** `df.sln`, `src/app.vcxproj` and the 26 vendored `.vcxproj` files are deleted,
and `tools/import_vcxproj.py` with them, because its input is gone. CMake is the only description
of the tree on both platforms. `tools/lint_repo.ps1` fails a tracked `.sln` or `.vcxproj`, so a
second description cannot come back quietly; the two forks are excepted, since their project files
belong to their own repositories.

The Linux release is the reason for the timing. A build system carrying a shipped product for the
first time on the release that also introduces a window would put two new things in one failure
path; Windows exercises it for a release first.

Four things had to be told to CMake, and each was a real difference rather than a formality:

- **Binary identity.** MSBuild linked into `exe/` as `diffractor64-d`, `diffractor64`,
  `diffractor32` and, for the Store, `diffractor`. `dd.ps1`, [diff.nsi](../installer/diff.nsi),
  the MSIX staging and the symbol store all name those files rather than asking the build what it
  produced, so CMake produces them under exactly those names in exactly that directory.
- **The Store configuration** is Release plus `WINSTORE`, which disables application-owned
  updates. It is `-DDIFFRACTOR_WINSTORE=ON` here rather than a third configuration type, because
  every other setting in it was identical to Release.
- **The target architecture.** `CMAKE_SYSTEM_PROCESSOR` answers for the *host* on Windows, so a
  Win32 build on an x64 machine read as AMD64 and an ARM64 one would too. `DIFFRACTOR_TARGET_ARCH`
  is derived from the generator platform or the MSVC architecture id, and everything
  architecture-dependent — nasm, the object format, the SSE2 switch, the libjpeg SIMD directory —
  asks that instead.
- **Two libraries build unoptimised on purpose.** highway and jxl set `Optimization` to `Disabled`
  in Release as well as Debug. That is not a formality: with the Release policy applied to them, a
  JPEG XL file decodes to an empty image, and `Should scan jxl metadata` is what caught it.

What the migration found is recorded in
[tools/build-divergence.txt](../tools/build-divergence.txt), and it is worth reading before
"tidying" anything above. It is a record rather than a check — there is no longer a second build to
compare against — but at the point it was written the two builds compiled the same 88 translation
units and agreed on every define, with the differences listed there and explained one by one.

Three defects surfaced that were nothing to do with the build system, and would not have been
found without it:

- **The Linux CI matrix had been building Debug twice.** `tools/dd.py` took its trailing arguments
  with `argparse.REMAINDER`, which swallows the script's own options, so `dd build --config Release`
  silently built Debug. Both legs of [linux.yml](../.github/workflows/linux.yml) were therefore the
  same build, and the Release figures recorded against them were not Release figures.
- **The Store configuration did not compile at `HEAD`.** A `std::wstring_view` overload of
  `df::folder_path` was removed during [Stage 0](#staged-plan), and the one call site behind
  `#ifdef WINSTORE` went with nothing compiling it. Nothing had built that configuration since.
- **Win32 was only ever described for x64.** The vendored modules were imported from the
  `Release|x64` configuration, so the 32-bit build assembled the x86-64 libjpeg SIMD with `-f
  win32`, pre-included FFmpeg's x64 nasm configuration, and omitted the `PREFIX` define that gives
  every 32-bit symbol the leading underscore MSVC expects. Each was invisible until something
  built it.

The Visual Studio generator remains supported, because debugging and profiling are why anyone
opens the IDE; `cmake -G "Visual Studio 18 2026" -A x64` generates projects on demand. The
developer and CI path is Ninja, which `tools/dd.py` drives after capturing the `vcvarsall`
environment, so both hosts use one generator and a failure on one is the same kind of failure as a
failure on the other.

### FFmpeg is configured twice, and the two answers differ

Nothing keeps those two configurations in step, so a format can be supported on one platform and
silently absent on the other. Comparing the enabled `CONFIG_*` switches in the two configurations
is the only way to see it, and it should be run after any FFmpeg change. It found four things:

- `--disable-autodetect` had declined zlib, and with it the thirty-odd decoders that depend on
  it: APNG, EXR, TSCC, ZMBV and the screen-capture family.
- libopenmpt was never asked for, so a tracked module scanned as no title, no encoder and no
  sample rate at all.
- This build carried RTP, RTSP, SAP and SDP demuxers that the Windows build has never had.
  Nothing in a local media organizer opens a socket, so it is now `--disable-network`.
- Running the comparison the other way after the first three were fixed showed the real one: the
  **entire encoder and muxer set** was being compiled in here. Diffractor is a broad-support
  reader and the Windows configure line has always said `--disable-encoders --disable-muxers
  --enable-muxer=avif --disable-protocols --enable-protocol=file`; this had simply never been
  matched. Doing so took 65 MB off the binary.

What remains is 27 switches on the Windows side, all of them platform — the DXVA2 and D3D11VA
hardware accelerators, Media Foundation, SChannel — and one on the Linux side, `CONFIG_ICONV`,
which is part of glibc rather than a dependency. Decoder and demuxer coverage is identical.

The four documentation page switches are excluded from the comparison rather than accounted in it.
Both configure lines pass `--disable-doc`, so nothing they control is built on either platform, but
they are autodetected rather than chosen: FFmpeg makes `doc` depend on the pages instead of the
pages on `doc`, so each follows whether `perl`, `pod2man` and `texi2html` happen to be installed
where configure ran. The Windows side is a checked-in artifact generated on one machine and the
Linux side is generated on whichever machine builds, so comparing them reports a package list as
though it were a difference in the product. This was found by CI and first answered by naming the
pages off in the Linux configure line, which only moved the difference to the other side.

[tools/compare_ffmpeg_config.py](../tools/compare_ffmpeg_config.py) is that comparison, run by
CI on the Release leg. It diffs the enabled `CONFIG_*` switches against a checked-in record of the
divergences that are platform by nature, and fails on any change — in either direction, because a
divergence that has silently disappeared makes the record a lie as surely as a new one does.
`--update` re-records it once someone has looked.

It has to read **two** headers per side, and reading only the first would miss the whole point:
FFmpeg puts every decoder, demuxer, encoder and muxer switch in `config_components.h`, and
`config.h` carries only the library-level ones. Windows checks in one `config-x64.h` per
architecture beside a shared `config_components.h`; Linux generates both into the build staging
directory, which also holds a copy of the Windows headers that must not be read as if they were
its own.

The recorded set is [third-party/ffmpeg-config-divergence.txt](../third-party/ffmpeg-config-divergence.txt):
27 Windows switches and one Linux one, with no decoder, demuxer, encoder or muxer among them.
One entry deserves a second look whenever it is re-recorded. `CONFIG_AVDEVICE` is there because
Linux passes `--disable-avdevice` while the Windows configuration builds it for Media Foundation;
that is defensible, but it is a decision rather than a fact about the platform, and it is the one
line in that file that could equally well be an oversight.

### Feeding configure a vendored static library

Every compression and music library FFmpeg links is the same copy the application links, which
takes two different mechanisms because configure offers two. zlib and libopenmpt are found through
pkg-config, so the build generates a `.pc` for each describing the archive it has just produced.
bzlib and lzma have no pkg-config path at all — configure probes them with a hard coded `-lbz2`
and `-llzma` — so those archives are staged under exactly those names and reached with
`--extra-cflags`/`--extra-ldflags`. Naming the system copies instead would put a second bzip2 and
a second lzma into a binary that already links the vendored ones; the result is verifiable, and
worth verifying, with `nm`:

    inflate defined 1 time(s)
    BZ2_bzDecompress defined 1 time(s)
    lzma_code defined 1 time(s)

Two traps, both of which cost a build cycle. `configure` sorts a probe's arguments into compiler
flags and libraries by looking for a `-l` prefix, so a library named by its archive path is placed
ahead of the object it has to satisfy and `ld`, reading once, discards it: name it
`-L<dir> -l<name>`. And `$<LINK_GROUP:RESCAN,...>` covers only the items written into it, while an
interface target's own link libraries are emitted after the group has closed — which left
`libavformat.a` behind `libopenmpt.a` on the final line. The FFmpeg archives are therefore
published through a global property that the application appends into the group.

A last one worth knowing: the vendored zlib is **zlib-ng**, not zlib. Before this, configure was
probing the system zlib's headers while the link resolved against zlib-ng beside it — two
implementations agreeing only by ABI convention.

Compiler portability is a smaller but real cost: the code is C++20 on MSVC and uses SSE2
intrinsics directly in [util_simd.h](../src/util_simd.h) (already guarded, with an ARM NEON
path), MSVC-specific SAL annotations (`_Guarded_by_`, `_Acquires_exclusive_lock_`) that
need no-op definitions, and `#pragma`s that Clang and GCC will not recognise.

## Distributions and delivery

The distribution is not the variable it appears to be. Building from source removes the
packaging difference and almost none of the real ones, because what varies between two
Linux machines is not the compiler or the package manager — it is the set of runtime
services the platform layer will newly depend on, and those cut across distributions rather
than along them.

Everything in `third-party/` is already built from source and pinned by the superproject, so
the 26 vendored libraries are distribution-invariant by construction; that is exactly the
property the [third-party policy](third-party.md) exists to protect. The variance is in the
*new* system dependencies the port acquires: SDL3, FreeType, HarfBuzz, fontconfig, libcurl,
an audio server, a GPU driver stack, and an XDG portal implementation.

### The axes that actually vary

| Axis | Values | What it decides |
|---|---|---|
| Display server | X11, Wayland | Clipboard, drag and drop, DPI and fractional scaling, and whether a window may restore its own position at all |
| Portal backend | `xdg-desktop-portal-gtk`, `-kde`, `-wlr`, none | Which [shell verbs](#features-with-no-linux-equivalent) exist: file chooser, trash, open-with, wallpaper, show-in-file-browser |
| Audio server | PipeWire, PulseAudio, bare ALSA | The replacement for `platform_win_sound.cpp`, and whether low-latency exclusive output exists |
| GPU stack | Mesa, NVIDIA proprietary | Vulkan/GL level, and VAAPI versus NVDEC for the hardware-decode descriptor that replaces the D3D11VA selection in [av_format.cpp](../src/av_format.cpp) |
| libc | glibc, musl | Locale and `iconv` behavior, backtrace capture for crash reports, `dlopen` semantics |
| C++ runtime | libstdc++, libc++ | The ABI of a shipped binary; irrelevant to a source build, decisive for a distributed one |
| Dependency age | rolling, frozen stable | Whether the minimum version of each system dependency is met at all |
| Filesystem | ext4/btrfs/xfs, case-folded ext4, mounted NTFS/exFAT, SMB/NFS | Case sensitivity, and therefore index identity |

Only two of those rows correlate with the distribution family: libc (musl means Alpine,
Void, Chimera) and dependency age (Debian stable and RHEL freeze; Arch and Fedora do not).
Display server, portal backend, audio server and GPU stack are choices a user makes *inside*
any distribution, and each can differ between two machines running the same release of the
same distribution. So "Debian-based versus Arch-based" is the wrong partition: specify the
port against these axes, and verify it on a small matrix that covers them, rather than
enumerating distributions.

Two of the axes are sharper than the table suggests:

- **Case sensitivity is per-mount, not per-system.** The
  [case-insensitivity problem](#windows-assumptions-still-in-portable-code) does not become a
  per-distribution constant on Linux; a single collection can span a case-sensitive ext4
  home, a case-folded directory, and a mounted NTFS volume. The build-time answer is
  nevertheless the one taken, because a per-mount rule would have to reach the filesystem from
  inside a hash probe. What absorbs the difference instead is that the index is case-sensitive
  while file-type matching is not, and that collision checks ask the filesystem at the moment of
  the write rather than asking the index.
- **Feature availability is a runtime query.** Whether trash, wallpaper or show-in-file-browser
  works depends on the portal backend present at run time, not on what was linked. The
  `can_recycle`-style capability queries the app already has are the right shape for this;
  the answers just stop being compile-time.

### Delivery decides everything the source build does not

**Distribution packages** (`.deb`, `.rpm`, a PKGBUILD) hand the build to each distribution's
toolchain and policy. Nearly all of those policies forbid vendored copies of libraries the
distribution already ships, and will unbundle `third-party/` on sight. That is fatal rather
than inconvenient here: `diffractor/FFmpeg` and `diffractor/XMP-Toolkit-SDK` are forks whose
entire value is Diffractor-specific patches, and unbundling silently discards them. Per-distribution
packaging also multiplies every axis above by the number of targets.

**Flatpak** pins a Freedesktop SDK runtime, which collapses the libc, C++ runtime, ABI and
dependency-age rows to one known set and keeps `third-party/` vendored as-is. It also makes
portals the only interface to the host — which is already what the
[toolkit choice](#toolkit-choice) assumes, so the sandbox enforces a constraint the
port wants rather than adding one. Display server and GPU stack remain host-supplied, as they
must.

**AppImage** is weaker: it inherits the build machine's glibc floor, has no portal contract,
and pushes the ABI problem onto whoever built it.

### The recommendation

- **Ship one artifact: a Flatpak on Flathub**, built against `org.freedesktop.Platform`
  rather than the GNOME or KDE runtime. The app draws its own UI, so it needs the base
  runtime's glibc, libstdc++, FreeType, HarfBuzz, fontconfig, libcurl, SDL3, PipeWire client
  and Mesa userspace, and nothing above them. That single choice pins six of the eight rows
  in the table above.
- **Develop on Fedora Workstation.** It defaults to Wayland, PipeWire, Mesa and
  `xdg-desktop-portal-gtk`, and its toolchain and library versions track close enough to the
  Freedesktop runtime that a host build and a Flatpak build fail in the same places. Arch is
  an equally good development host; the point is a current stack, not a specific distribution.
- **Cover the axes in CI, not the distributions.** Four configurations are enough:
  Fedora/Wayland/GNOME portal/Mesa as primary; Ubuntu LTS/X11/Mesa for the X11 path and the
  version floor; a KDE image for `xdg-desktop-portal-kde`; and one NVIDIA-proprietary machine
  for the GPU backend and hardware decode. Adding more distributions to that list adds cost
  without adding coverage.
- **Record a minimum version per system dependency** and let the Ubuntu LTS leg enforce it.
  Without a stated floor the requirement silently becomes "whatever the developer's machine
  had".
- **Do not produce `.deb`, `.rpm` or AUR packages ourselves.** They cannot be maintained
  without either abandoning the vendored forks or fighting every distribution's unbundling
  policy. If maintainers package it downstream, the fork question above is the first thing to
  answer for them.

### WSL as the early development target

WSL2 is a real Linux kernel with a real glibc userspace, so it covers the first and largest
part of the port at zero setup cost — and the part it covers is precisely the part that is
cheapest to get wrong on Windows. Use it, but know where its answers stop being true.

| Covered by WSL | Not covered by WSL |
|---|---|
| Building `src/` and `third-party/` with GCC and Clang, which is where the MSVC-isms surface: SAL annotations, unrecognised `#pragma`s, `__declspec(selectany)` | GPU backend validation — Mesa's D3D12 driver over `/dev/dxg` means any Vulkan or GL result is about that driver, not about Mesa or NVIDIA |
| Running the existing suite headless (`/test`), which is the single highest-value early milestone: a passing portable core before any window exists | Hardware decode — VAAPI is absent or unrepresentative, so the D3D11VA replacement cannot be judged here |
| `wchar_t` being 32-bit, which makes the [UTF-16 assumptions](#windows-assumptions-still-in-portable-code) fail loudly instead of silently | XDG portals — there is no desktop session and no portal backend by default, so trash, open-with, wallpaper and file chooser cannot be exercised |
| Case sensitivity from both sides at once: the ext4 root is case-sensitive while `/mnt/c` is not, which is the per-mount problem reproduced on one machine | Drag and drop — WSLg does not bridge it |
| The software renderer and a Wayland or X11 window through WSLg, plus clipboard, which WSLg does bridge | DPI, fractional scaling and multi-monitor — WSLg presents one virtual output |
| Audio through the PulseAudio server WSLg provides, enough to prove the output path | Audio latency and exclusive output, and PipeWire's native API |
| Crash capture via core dumps, POSIX I/O, threading, locale, libcurl | Any performance number, GPU or CPU — it is a VM |

The practical division is that WSL proves *correctness of the portable core and the build*,
and proves nothing about *integration with a desktop*. That maps almost exactly onto the two
halves of this document: everything in
[Windows assumptions still in portable code](#windows-assumptions-still-in-portable-code) and
most of the [build system](#build-system) work can be done and validated in WSL, while
everything in [features with no Linux equivalent](#features-with-no-linux-equivalent) needs a
real session on real hardware.

Install Ubuntu LTS there rather than a rolling distribution: it is the version-floor leg of
the CI matrix anyway, so the development machine enforces the floor for free.

One trap if the source is mirrored into the Linux filesystem rather than built over `/mnt/c`
(worth doing — an ext4 mirror builds several times faster): `rsync -a` without `--delete`
leaves files that have been deleted on the Windows side sitting in the mirror. A file that no
longer exists can then still be read, or still be compiled if anything names it. Verify a
deletion against `git ls-files`, not against the mirror.

## Toolkit choice

The app draws its own UI. Only six control types are OS widgets — `edit`, `toolbar`,
`trackbar`, `button`, `date_time_control` and the unused `web_window` — and the section on
[controls](#controls) below records the decision that they stop being OS widgets on both
platforms. That means the toolkit needs to supply a window, an input stream, a pixel target, a
clipboard, drag and drop, and text input with IME — not a widget set.

**Decided: SDL3, on Linux only.** The alternative was GTK4, which is stronger on IME, portals
and accessibility and gives a native feel for the six widget types — but it is a heavier
dependency whose rendering model wants to own the frame loop, which fits the app's own
`WM_PAINT`-driven lifecycle badly, and its advantage in native widgets is worthless once the
[controls](#controls) are drawn. SDL3 keeps the existing frame lifecycle intact and avoids
re-implementing the control-paint contract (`probe_buffered_control_paint`) that only exists to
make native Windows controls behave.

What SDL3 supplies, on both X11 and Wayland from one code path, is more of Stage 4 than the
toolkit comparison above suggested: window, input, clipboard, drag-and-drop file events, audio,
text input with IME including the candidate-window area, and — new in SDL3 — native file dialogs,
which resolve to the desktop portal on Linux. Its licence is zlib, so vendoring it raises nothing
against the LGPL application.

Two constraints come with that choice:

- **Linux only.** The Windows platform layer works, and routing it through SDL would be risk
  with no user-visible gain. A later macOS port would get the window and input layer nearly free
  and nothing else; that is not a reason to change Windows now.
- **SDL is not the whole platform.** Text shaping, trash, and the surviving shell verbs remain
  ours.

One bonus worth weighing at Stage 5 rather than now: SDL_GPU abstracts Vulkan, Metal and D3D12
behind a single shader pipeline, which would answer the "Vulkan or OpenGL 3.3" question and
collapse two shader sources into one. It has no D3D11 backend, so it does not replace
[platform_win_d3d11.cpp](../src/platform_win_d3d11.cpp) — adopting it for Linux alone means three
backends, not two.

Text is the exception, and it is Linux-only for the same reason SDL3 is: FreeType, HarfBuzz and
fontconfig are needed regardless of toolkit, and Windows keeps DirectWrite. See
[the text stack](#the-text-stack). [platform.h](../src/platform.h) already states the contracts the
port must satisfy — `probe_glyph_fallback` and `probe_font_cache` are executable specifications for
glyph fallback and face caching.

## The text stack

"Replace DirectWrite" sounds like one decision and is three, because a text stack does three
separable jobs:

| Job | Windows | Linux |
|---|---|---|
| Find a face, and a fallback face for a glyph the face lacks | DirectWrite over the system font stack | fontconfig |
| Shape: Unicode to positioned glyph ids | DirectWrite | HarfBuzz |
| Rasterise: a glyph id to a coverage bitmap | DirectWrite | FreeType |

**Decided: FreeType, HarfBuzz and fontconfig on Linux only.** The first job is inherently a
system service and could never have been shared — fontconfig on Windows would not resolve the
user's chosen UI font and would not do font linking for CJK — so "one text stack everywhere" was
never on the table. At most the second and third could be shared, and sharing those alone still
leaves the two platforms selecting different *faces*, which is where most of the visible difference
lives. Against that, replacing DirectWrite on Windows changes hinting, gamma and subpixel
antialiasing on a platform that has no problem: a `User-visible behavior` change under the
[pre-flight gate](../AGENTS.md#mandatory-pre-flight-validation), for no user benefit.

The seam is already in the right place. `software_canvas::blend_glyph` takes an 8-bit coverage
bitmap, so the rasterizer does not care which of the three produced it, and `ui::text_layout` and
`measure_context` are the portable interfaces a second implementation fills.

**The cost is not pixels, it is layout.** Two shapers means two sets of advances and two line
breakers, so the same string can wrap differently: a dialog that fits on Windows may not fit on
Linux. That is why the cross-machine parity scene in
[rendering.md](rendering.md#the-software-tile) deliberately contains no text, and why any
fixed-size text container is a portability risk that only a real Linux session will find. Prefer
measured layout over fixed extents wherever a string is translated.

## Controls

The edit box, toolbars, sliders and buttons are the part of the port that looks worst and
is actually in the best shape, because most of them are already drawn by Diffractor rather
than by Windows.

### What the six controls are today

| Control | Win32 class | Who draws the pixels | What Windows really supplies |
|---|---|---|---|
| `toolbar` | `TOOLBARCLASSNAME` | **Diffractor**, via `NM_CUSTOMDRAW` at `CDDS_PREERASE` and `CDDS_ITEMPREPAINT` | Button layout and wrapping, hit testing, tooltips, dropdown notification, keyboard traversal |
| `button` | `BUTTON` | **Diffractor**, via `NM_CUSTOMDRAW` | Hit testing, focus, radio grouping, default-button handling |
| `edit` | `EDIT` | Windows | Text layout, caret, selection, IME, `EM_SETCUEBANNER`, `IAutoComplete2` including filesystem completion |
| `trackbar` | `TRACKBAR_CLASS` | Windows | Thumb rendering, drag behavior, keyboard steps |
| `date_time_control` | `DATETIMEPICK_CLASS` ×2 | Windows | Field parsing, calendar drop-down, locale format |
| `web_window` | — | — | Nothing; it is declared with no implementation and no callers |

So for the two controls that dominate the UI — toolbars and buttons — the appearance is
already Diffractor's. Windows is supplying an `HWND`, a hit-test and a message pump, and
charging for it in `probe_buffered_control_paint`, the GDI object ownership rules and the
resize behavior that [rendering.md](rendering.md) has to document.

### The in-tree precedent

The self-drawn versions already exist and already ship:

- [ui_text_edit.h](../src/ui_text_edit.h) holds `ui::single_line_edit_model`: window-free
  editing state with UTF-8 boundary handling, word motion, selection and undo/redo. It has
  no platform dependency at all.
- [ui_controls.h](../src/ui_controls.h) holds `edit_element` and `edit_element_controller`:
  a rendered single-line edit with caret blink, cue text, a leading icon, an overridable
  background, and hit testing derived from metrics built during layout rather than during
  paint. It is in use as the filter edit in [view_items.h](../src/view_items.h).
- The same file holds `slider_element`, a rendered slider, in use in
  [view_items.cpp](../src/view_items.cpp).

Two of the hard inputs are already Diffractor's rather than the OS's:

- **Spell checking** is [util_spell.h](../src/util_spell.h) over vendored hunspell, not a
  Windows service. Squiggles, suggestions and "add word" port unchanged.
- **Autocomplete candidates** are supplied by the app through
  `edit_styles::auto_complete_list` and `edit::auto_completes`. Only the drop-down UI and
  the `file_system_auto_complete` enumeration come from the shell.

The only control with no in-tree precedent is `date_time_control`.

### Decided: retire the native controls, on Windows first

Convert each `control_base` subclass to a `view_element` drawn into the same scene as the
rest of the UI, and do it on Windows before any Linux file exists. This is the highest
leverage decision in the port:

- It converts a porting problem into a refactor the existing view tests and `.\dd.ps1 test`
  can judge today, on a platform where the current behavior is available for comparison.
- It deletes real Windows-only complexity rather than mirroring it: the double-buffered
  control paint, the shared GDI object ownership, the resize dance for child windows, and
  the `std::any handle()` escape hatch on `control_base`.
- It gives one appearance and one interaction model on both platforms, which is what the
  parity contract already demands of the renderer.
- Every step is independently shippable on Windows, so the port never carries a long-lived
  broken branch.

A sensible order, cheapest and least risky first:

1. **`button`** — already owner-drawn; needs focus, default-button and radio grouping.
2. **`toolbar`** — already owner-drawn; needs layout and wrapping (which
   `measure_toolbar` already computes), hit testing, tooltips and dropdown menus.
3. **`trackbar`** — `slider_element` already does this; mostly an adapter.
4. **`edit`** — `edit_element` already does the single-line case. The dialog uses in
   [ui_dialog.h](../src/ui_dialog.h) additionally need multi-line, password, numeric and
   vertical-scroll variants.
5. **`date_time_control`** — genuinely new. The cheapest honest answer is three numeric
   edits plus the existing calendar affordances rather than a bespoke picker.

Note the classification: this is `User-visible behavior` under the
[pre-flight gate](../AGENTS.md#mandatory-pre-flight-validation), not `Internal`. Appearance
and interaction change, so each step names its scope, target and effect and is judged
against [design.md](design.md).

### What you must then build yourself

This is the real cost, and it is owed on Windows as much as on Linux:

1. **Focus and tab order.** Today this is the Win32 dialog manager (`WS_TABSTOP` plus
   `IsDialogMessage`). A `control_frame` will need an explicit focus ring that honours
   `focus_first()`, wraps, skips disabled and hidden controls, and survives layout changes.
2. **Text input and IME.** The one genuinely hard item. Composition, preedit display and
   candidate-window positioning have to be handled explicitly — `WM_IME_*` on Windows,
   `SDL_StartTextInput` and preedit events on Linux. This is the main reason not to do the
   edit conversion casually, and the reason to do `button` and `toolbar` first.
3. **Tooltips**, currently the toolbar's own tooltip window.
4. **Autocomplete drop-down**, plus a platform call for filesystem completion to replace
   `IAutoComplete2`.
5. **Context menus.** The edit's cut/copy/paste/spelling menu and `frame::track_menu` are
   native popup menus today. They are command lists, so they can be drawn in-scene, but
   that is another piece of new UI.
6. **Accessibility — explicitly not replaced.** Native controls expose themselves to screen
   readers through UI Automation for free, and drawn controls expose nothing unless UIA is
   implemented on Windows and AT-SPI on Linux. Neither will be: the decision is to accept the
   loss rather than take on an accessibility stack, on both platforms. It is recorded here
   because it is a real regression on Windows for anyone using a screen reader, and because it
   is reversible only by implementing accessibility directly.
7. **Mnemonics and accelerators** (`&Cancel` and friends in
   [app_text.h](../src/app_text.h)), currently resolved by the dialog manager.

Items 1, 4, 5 and 7 are bounded UI work. Item 2 is the schedule risk. Item 6 was the one that
could have made the answer "no", and has been answered instead.

### The alternative, and why it is worse

The other route is to keep `control_base` as a native-widget abstraction and implement the
six subclasses with GTK4 widgets on Linux. It starts faster and gets IME and accessibility
for free, but it buys a recurring cost:

- Two visual languages to keep in sync, forever, against a renderer whose whole point is
  that it produces the same picture everywhere.
- The child-window compositing problem returns in a new form. Native widgets must be
  embedded in a window whose client area the app paints itself; that is exactly what the
  double-buffered custom draw in [platform_win_ui.cpp](../src/platform_win_ui.cpp) exists
  to solve, and it would have to be solved again against a toolkit that wants to own the
  frame loop.
- It does not reduce the Windows platform layer at all, so it is pure addition.

If a native widget is kept anywhere, `date_time_control` is the candidate: it is a lot of
UI for a small number of dialog fields, and it is the one control with no in-tree drawn
equivalent.

## Staged plan

Each stage ends in something falsifiable. No stage depends on a later one. Stage 1 is complete;
Stage 0 is partly done, and what remains of it is listed under
[Windows assumptions still in portable code](#windows-assumptions-still-in-portable-code).

**Stage 0 — Make the boundary provable (Windows only). Done.**
The `char16_t` and `char32_t` conversions, the `char` predicates behind `str::split`, the
hardware-decode descriptor that replaced the D3D11VA selection, `native_path` and the move of
`to_shell_path` and the drag probe into `platform_win.h`, the icon font, path identity as a
compile-time property with file-type matching kept separate from it, the present-target seam that
takes GDI out of the CPU rasterizer, the backend interface losing its `HRESULT`, and the
cross-machine scene comparison the Stage 2 gate needs.
*Gate:* `.\dd.ps1 test` green on Windows, and a search of `src/` for `wchar_t`, `std::wstring` and
Win32 types finds hits only under `platform_win*` — that search now finds 9 files, down from 28,
and every one of them is the UTF-16 boundary, its tests, or a native type named where it belongs.

**Stage 0b — Retire the native controls (Windows only). Not started.**
[Controls](#controls), in the order given there, each step shipped on Windows. Runs in
parallel with Stage 1 and gates Stage 4. *Gate:* per step, `.\dd.ps1 test` green and the
converted control judged against [design.md](design.md); on completion,
`probe_buffered_control_paint` and `control_base::handle()` are gone.

**Stage 1 — Headless core on Linux. Done, and past its gate.**
The gate asked only for the suites that do not require a window. What passes is the *entire*
suite — 736 of 736, in Debug and Release — so indexing, search, metadata and the decode ladder
are proven portable, and the third-party build exists for all 26 libraries. The platform layer
written to get there is the five files listed under [current state](#current-state).

**Stage 2 — Pixels. Not started, but its gate is already executable and green.**
A Linux window that owns a BGRA buffer, presenting the already-portable CPU rasterizer. The
interface it has to satisfy is `ui::software_present_target`, and `create_memory_present_target`
is a working implementation that presents nothing, so what is left is the window rather than the
renderer.
*Gate:* the same scene rendered on Windows software and Linux software matches.
`Should rasterise one scene to one answer on every platform` is that gate: it drives the
rasterizer with no window and no font, and compares an even spread of the resulting pixels
against a recorded set. It passes today under MSVC Debug, MSVC Release and GCC 13, which is the
first time anything has compared the two platforms' rendering rather than asserting they agree.
The comparison carries a tolerance deliberately — see [rendering.md](rendering.md), where the
reason an exact hash cannot be the artifact is recorded.

**Stage 3 — Text (Linux only).**
FreeType, HarfBuzz and fontconfig behind `measure_context` and `text_layout`. Windows keeps
DirectWrite; see [the text stack](#the-text-stack) for why this is the one layer with no shared
implementation. This is the last thing holding `software_draw_context` on Windows, so it is what
Stage 2 waits on.
*Gate:* `probe_glyph_fallback` and `probe_font_cache` pass on Linux with the same contract
they assert on Windows.

**Stage 4 — Input, clipboard, drag and drop, dialogs.**
The drawn controls from Stage 0b wired to Linux input, including text input and IME, plus
portal-backed file dialogs and trash. *Gate:* the view and command tests pass; targeting
and recovery behave as [design.md](design.md) requires.

**Stage 5 — GPU backend.**
Vulkan or OpenGL 3.3, with the 12 HLSL shaders ported — or SDL_GPU, which would supply one
shader pipeline instead of two at the cost of a third backend. The colour and YUV maths must
match exactly; the parity list in [rendering.md](rendering.md) is the checklist.
*Gate:* GPU and software backends match on Linux to the same tolerance they match on
Windows.

**Stage 6 — Audio, then hardware video decode.**
PipeWire first; VAAPI last, because the renderer interop for NV12 textures is the hardest
single piece of the port and software decode is a working fallback until it lands.

## Risks

- **Path case sensitivity** was the one that could corrupt data rather than merely fail, and it is
  now landed rather than pending. It reaches the index, dedup and collision handling, and it was
  baked into the interned string hash rather than only into a comparison function. What is left of
  the risk is regression: `df::iless` and `str::icmp` still exist and still fold unconditionally,
  so a new path-keyed container reaching for either re-creates the defect silently. Path keys take
  `df::path_key_less`, `df::ihash`/`df::ieq` or the `folder_path` and `file_path` comparisons.
- **Nothing guarded Stage 1 until recently.** CI was Windows and MSBuild only, so no commit built
  CMake or Linux and the suite could rot silently between one deliberate check and the next.
  [linux.yml](../.github/workflows/linux.yml) now builds Debug and Release on Ubuntu LTS, runs the
  suite, and runs the configuration comparison below. That gate was itself partly vacuous until the
  build learned to stage `exe/` beside the binary: fixtures, catalogs and dictionaries are found
  relative to the running binary, and a CMake build puts the binary somewhere none of them are, so
  every fixture test failed on a missing file rather than on its subject. The last hole — CMake on
  Windows, configured by hand and built by no commit — is closed by
  [windows.yml](../.github/workflows/windows.yml), which builds Debug and Release x64, Release
  Win32 and the Store variant, and runs the suite on each.
- **Backend parity across three renderers** (Windows GPU, portable CPU, Linux GPU) triples
  the surface the parity contract has to hold over. Keeping the CPU rasterizer as the
  shared reference limits this.
- **Feature parity is not achievable** for the shell-integration list, so the port implies
  a Linux build that does less. That is settled policy now rather than an open risk; what
  remains is making sure an unavailable command is hidden rather than shown and failing.
- ~~**Two build systems in parallel**~~ **Closed.** They drifted twice: the FFmpeg configurations
  diverged on four separate switches, each silently changing which formats decode; and the
  hand-derived Windows source list was compiling two `#include`-only templates and two `HOSTPROGS`
  table generators, one of which put a `main()` into a static library. Neither was visible without
  deliberately comparing the two descriptions. There is now one, and a third-party upgrade is one
  piece of work; see [retiring MSBuild](#retiring-msbuild).
- **Losing UI Automation** when the native controls go is the change most likely to be
  noticed by someone who cannot see it happen. This is now an accepted cost rather than an open
  question, and it applies to Windows as much as to Linux.
- **IME and complex text input** is the single deepest piece of new UI work, and it has no
  partial credit: an edit that cannot compose Japanese or Korean is not shippable in a
  product that already ships those translations.
- **`platform_win_ui.cpp` at 7,678 lines** is one file doing window management, controls,
  dialogs, menus, clipboard, drag/drop and process entry. Its Linux counterpart should not
  reproduce that shape; splitting it by concern is worth doing before it is mirrored.
  Stage 0b removes a large part of it outright.

## Decisions

Every question that blocked a stage has an answer. What follows is the record; the reasoning
lives in the section each one links to.

1. ~~Is the Linux build case-sensitive in its path model, or does it preserve the current
   case-insensitive matching?~~ **Answered and landed: case-sensitive on Linux, unchanged on
   Windows, as `case_insensitive_path_identity` beside `windows_path_semantics`.** Per-mount is the
   theoretically correct answer and is not affordable, because the case-folded hash is stored in the
   interned string record and a hash functor cannot consult the filesystem. Identity follows the
   filesystem; file-type, extension and sidecar matching stay case-insensitive on both platforms.
   See [path identity](#windows-assumptions-still-in-portable-code).
2. ~~Does deletion on Linux use XDG trash, and does "recoverable" mean the same thing to a
   user as it does on Windows?~~ **Answered: yes, the XDG trash specification, including the
   `.trashinfo` record that makes restore work from the user's own file manager.** Cross-volume
   deletes offer permanent deletion rather than copying, and `can_recycle` becomes a runtime
   query. See [features with no Linux equivalent](#features-with-no-linux-equivalent). Blocks
   Stage 4; the user-facing half belongs to [design.md](design.md).
3. ~~Is CMake adopted for the whole tree, or only for the Linux build?~~ **Answered and landed: the
   whole tree, generating for both platforms, and since 1.27.2 it is the only description.** The
   `.vcxproj` files the vendored modules were imported from are deleted, and a lint rule keeps them
   from returning. Windows moved onto CMake a release before Linux needs it, so that the build is
   not a new variable on the release that also introduces a window. See
   [retiring MSBuild](#retiring-msbuild).
4. ~~Which of the shell-integration features are dropped on Linux versus reimplemented?~~
   **Answered: anything without an obvious Linux equivalent is dropped, and waits for a user to
   ask.** Windows is unchanged. An unavailable command is hidden, not shown and failing. See
   [features with no Linux equivalent](#features-with-no-linux-equivalent).
5. ~~Are the native controls retired in favour of drawn ones, and if so what is the
   accessibility commitment that replaces UI Automation?~~ **Answered: retired in favour of
   drawn controls, with no accessibility stack replacing UI Automation on either platform.**
   The loss is accepted deliberately. See [controls](#controls). Blocks Stage 0b.
6. ~~Which display server is targeted first — Wayland, X11, or both through SDL3?~~
   **Answered: SDL3, on Linux only, which makes Wayland and X11 a runtime backend choice rather
   than two code paths.** Windows keeps its own platform layer. See
   [toolkit choice](#toolkit-choice).
7. ~~What replaces the Segoe MDL2 icon font, and does the visual design change with it?~~
   **Answered: Fluent UI System Icons, bundled, on both platforms.** Segoe MDL2 cannot be
   redistributed off Windows at all. The visual design changes slightly on Windows too, which is
   the point — one icon set everywhere. See
   [features with no Linux equivalent](#features-with-no-linux-equivalent).
8. ~~Is the FFmpeg configuration comparison a release step?~~ **Answered: yes, and mechanized.**
   It has found four divergences so far, each silent, and nothing else detects them, so it is
   [tools/compare_ffmpeg_config.py](../tools/compare_ffmpeg_config.py) run by CI rather than a
   manual step that would lapse. The guard is not "no divergence" but "no divergence that has not
   been accounted for": the platform switches are recorded beside the fork, and any change to that
   set fails until someone looks. See
   [FFmpeg is configured twice](#ffmpeg-is-configured-twice-and-the-two-answers-differ).
9. ~~Does the FreeType/HarfBuzz/fontconfig text stack replace DirectWrite on Windows too, or is it
   Linux only?~~ **Answered: Linux only.** Face selection and fallback are a system service that
   could never have been shared, so the choice was only ever about shaping and rasterisation, and
   taking those from Windows changes how text looks there for no user benefit. The accepted cost is
   that text can wrap differently on the two platforms, which is a layout risk rather than a
   cosmetic one. See [the text stack](#the-text-stack). Blocks Stage 3, and through it Stage 2.

The work these unblock is ordered under the [staged plan](#staged-plan). The one item there that
was owed to no decision — the cross-machine scene comparison that makes the Stage 2 gate
executable — is done, and green.

## Where this lives

| Port subject | Source |
|---|---|
| The abstraction every port must satisfy | [platform.h](../src/platform.h) |
| What exists today: entry point, files, settings, desktop, UI | [platform_linux.cpp](../src/platform_linux.cpp), [platform_linux_files.cpp](../src/platform_linux_files.cpp), [platform_linux_settings.cpp](../src/platform_linux_settings.cpp), [platform_linux_desktop.cpp](../src/platform_linux_desktop.cpp), [platform_linux_ui.cpp](../src/platform_linux_ui.cpp) |
| Cross-platform compatibility shims | [platform_compat.h](../src/platform_compat.h) |
| The CPU rasterizer, the seam a platform fills to show it, and the parity gate | [render_software.h](../src/render_software.h), [render_software.cpp](../src/render_software.cpp) |
| The Windows implementation each of these mirrors | the `platform_win*` files |
| The build | [CMakeLists.txt](../CMakeLists.txt), `cmake/`, [dd.ps1](../dd.ps1), [tools/dd.py](../tools/dd.py), `.github/workflows/` |
| What the MSBuild build did that CMake had to be told | [tools/build-divergence.txt](../tools/build-divergence.txt) |

The debt catalogued above is measurable: `tools/lint_repo.ps1` fails when a system header or Windows
handle type appears outside `platform*`, which is the boundary the port depends on. It cannot see a
`platform.h` entry point that is shaped around a Win32 call rather than an intention — that remains
the reviewer's job, and it is the failure mode that costs most later.
