# Linux port

This document owns the Linux port: whether it is realistic, what the platform boundary
actually costs to re-implement, which Windows assumptions still sit in portable code, and
the order in which the work can be done so that each stage is provable on its own.

It does not restate architecture, threading, or ownership — those belong to
[implementation.md](implementation.md). It does not restate the backend parity contract —
that belongs to [rendering.md](rendering.md). User-facing behavior belongs to
[design.md](design.md); where a Linux difference would change what a user can predict,
this document names the decision and defers the answer there.

## Verdict

The port is realistic and the boundary is genuinely clean, but "swap in a Linux set of
files" understates the work by roughly half.

Three findings drive that:

1. **The boundary holds.** `platform_win.h` is included by `platform_win*.cpp` and
   `test_platform_win.cpp` and by nothing else. A search of `src/` for Win32 types
   (`HWND`, `HRESULT`, `windows.h`, `IUnknown`, `WINAPI`, `_WIN32`) finds essentially
   nothing outside `platform_win*` — the exceptions are listed in
   [Windows assumptions still in portable code](#windows-assumptions-still-in-portable-code)
   and are all small. This is the expensive property to establish and it is already there.

2. **The split is favourable but the platform layer is dense.** `platform_win*` is
   ~20,500 lines against ~117,400 lines elsewhere: about 15% of `src/`. But it is not 15%
   of the effort, because a large part of it is a Windows *desktop integration* surface
   (shell, WIC, DirectWrite, WASAPI, D3D11) where the Linux replacement is a different
   library with a different model, not a renamed call.

3. **The build system is the underestimated cost.** 27 libraries are vendored in
   `third-party/` and every one of them is built from a hand-written `.vcxproj`; upstream
   build files were deliberately stripped (only `libarchive`, `libde265`, `libheif`,
   `LibJpeg` and `xmp` still carry any CMake, and only `dav1d` carries meson). None of
   that builds on Linux today. See [third-party.md](third-party.md) for the vendoring policy
   this collides with.

The realistic shape is: a portable core that is close to ready, a renderer that has an
unusually good migration path because the CPU backend already exists, a text and window
layer that is new code, and a long tail of Windows shell features that need product
decisions rather than ports.

## What each layer costs

The platform layer, largest first, with what replaces it:

| File | LOC | What it is | Linux replacement | Difficulty |
|---|---|---|---|---|
| `platform_win_ui.cpp` | 7,678 | Window class, message loop, native controls, dialogs, menus, clipboard, drag/drop, DPI, monitors, `wWinMain` | Window/input/clipboard/DnD toolkit + own control implementations | **High** — the single biggest item |
| `platform_win_d3d11.cpp` | 2,976 | GPU backend, batching, glyph atlas, video textures | Vulkan or OpenGL 3.3, plus 12 HLSL shaders → GLSL/SPIR-V | High |
| `platform_win_files.cpp` | 2,896 | File I/O, attributes, enumeration, shell copy/move/delete, thumbnails, shell tags | POSIX I/O + XDG desktop portals for trash/open | Medium, with feature gaps |
| `platform_win_software.cpp` | 2,094 | CPU rasterizer + GDI present | Mostly portable already; only the present path is Windows | **Low** — see below |
| `platform_win.cpp` | 1,678 | Locks, events, known folders, dates, number/locale format, drives, eject | pthreads/futex, XDG base dirs, `std::chrono`, ICU or `std::locale` | Low–medium |
| `platform_win_font.cpp` | 831 | DirectWrite faces, fallback, metrics | FreeType + HarfBuzz + fontconfig | Medium–high |
| `platform_win_sound.cpp` | 598 | WASAPI render client | PipeWire (or PulseAudio/ALSA) | Medium |
| `platform_win_wic.cpp` | 572 | WIC encode/decode for clipboard and save | Existing vendored codecs already cover the formats | Low |
| `platform_win_settings.cpp` | 452 | Registry and INI stores | INI store already exists and is portable in shape | **Low** |
| `platform_win_web.cpp` | 330 | WinInet HTTP client | libcurl | Low |

Two of these are much cheaper than their size suggests:

- **The software backend is the shortest route to pixels.** It is a self-contained CPU
  rasterizer that renders the same retained scene as the GPU backend and honours the same
  parity contract. Only its final blit is Windows. Splitting the rasterizer from its
  present path — a pure `Internal` refactor doable today on Windows, validated by the
  existing `probe_software_tiling` — turns "write a Linux renderer" into "write a Linux
  window that owns a BGRA buffer."
- **Settings already have a portable backend.** `create_ini_file_settings()` exists
  alongside the registry store and is selected at runtime, so Linux persistence is a
  configuration decision, not new code.

One item is cheaper still: `ui::web_window` and `ui::web_events` are declared in
[ui.h](../src/ui.h) but have no implementation and no callers. There is no embedded
browser to port.

## Windows assumptions still in portable code

These are the real blockers, and every one of them can be fixed on Windows before any
Linux file exists. They are `Internal` changes under the [pre-flight gate](../AGENTS.md#mandatory-pre-flight-validation):
no observable behavior changes, and `.\dd.ps1 test` proves it.

**Path semantics are Windows semantics, in portable code.**
[util_path.h](../src/util_path.h) is not a platform file, but it encodes drive letters and
UNC prefixes in `is_path`, the Windows illegal-character set in `is_illegal_name`, and —
most consequentially — case-insensitive comparison in `folder_path::compare` and
`file_path::icmp`. Case-insensitivity is load-bearing: it reaches the index, the search
postings, deduplication, and every "is this the same file" decision. On a case-sensitive
filesystem, `Photo.JPG` and `photo.jpg` are two files, and the index must agree with the
filesystem or it will silently merge or lose items. This needs a deliberate answer, not a
compile fix, and it is the highest-risk item in the whole port.

**`wchar_t` is assumed to be UTF-16.** It is 16-bit on Windows and 32-bit on Linux, so
this miscompiles or silently mis-decodes rather than failing to build:
- [metadata_exif.cpp](../src/metadata_exif.cpp) `bit_cast`s raw EXIF bytes to `const wchar_t*`
  and copies into `std::wstring` by `sizeof(wchar_t)`.
- [metadata_icc.cpp](../src/metadata_icc.cpp) assembles UTF-16BE code units into a
  `std::wstring`.
- [app_sidebar.h](../src/app_sidebar.h) and [files_core.cpp](../src/files_core.cpp) build
  icon strings from `wchar_t` code points.
- Several `str::split` predicates take `wchar_t` where they only ever see ASCII.

The fix is `char16_t` for genuine UTF-16 data and `char32_t` for code points, which is
strictly better on Windows too.

**`std::wstring` is in the platform interface.** `platform::to_file_system_path`,
`to_shell_path`, `create_segoe_md2_icon(wchar_t)` and the `probe_drag_data_object` result
put a Windows string type in [platform.h](../src/platform.h). These should become
platform-private, with the cross-platform interface speaking UTF-8 `df::file_path` only.

**D3D11VA leaks into the media layer.** [av_format.cpp](../src/av_format.cpp) selects
`AV_HWDEVICE_TYPE_D3D11VA` directly. That is platform-specific code outside a `platform*`
file — a boundary violation today, and the natural place for the VAAPI branch tomorrow. It
should become a platform-supplied hardware-decode descriptor.

**Small residue.** `__declspec(selectany)` in [util_date.h](../src/util_date.h), and the
`error_atl_direct3d` string id in [app_text.h](../src/app_text.h) that names a Windows
technology in user-visible text.

## Features with no Linux equivalent

These are product decisions, not ports. Each needs an answer before the corresponding
platform function can be written, and several touch [design.md](design.md) because they
define what recovery means.

- **Recycle Bin.** `delete_items(allow_undo)` and `can_recycle` are the backbone of
  recoverable deletion. The XDG trash specification is close but not identical (per-volume
  trash directories, no equivalent of the "this path bypasses the bin" query). Recovery is
  a design promise, so this is a `User-visible behavior` decision.
- **Shell metadata and thumbnails.** `read_shell_metadata`, `write_shell_tags`,
  `get_cached_file_properties` and `get_shell_thumbnail` depend on Windows property
  handlers, including the cloud-placeholder path that fetches a thumbnail without
  hydrating a file. There is no Linux counterpart; the app's own metadata and thumbnail
  pipeline would have to cover these cases outright.
- **Cloud placeholders.** OneDrive Files On-Demand offline detection has no direct
  analogue. The `test_offline_predicate` seam survives; the real detection does not.
- **Shell verbs.** `set_desktop_wallpaper`, `show_file_properties`, `print`, `has_burner` /
  `burn_to_cd`, `scan` (WIA), `assoc_handlers`, `show_in_file_browser`, `resolve_link`
  (`.lnk`), `eject`, `scan_drives`. Some map to XDG desktop portals, some to nothing.
- **Registry and crash guards.** `create_registry_settings` and the crash-guard markers
  move to the INI store; the guard semantics themselves are portable.
- **Minidumps.** DbgHelp crash capture has no equivalent; a Linux port needs a different
  crash-report format and a different reader for
  [crash-dump analysis](../src/util_crash_files_db.h).
- **Segoe MDL2 icons.** `create_segoe_md2_icon` renders from a Windows-only font. Linux
  needs a bundled icon font or vector icon set, which is a visual-design decision.
- **Packaging.** The MSIX/WinStore configuration and the NSIS installer have no Linux
  counterpart; Flatpak or AppImage is a separate piece of work.

## Build system

There is no CMake or meson build for `src/`, and the vendored libraries have had their
upstream build files removed. A Linux build therefore needs, in order:

1. A build description for `src/` itself — straightforward, one target, ~130 files.
2. Build descriptions for 27 vendored libraries, including the two forks
   (`diffractor/FFmpeg` and `diffractor/XMP-Toolkit-SDK`) whose value is precisely that
   they carry Diffractor-specific patches, so they cannot be replaced with distro packages
   without losing those patches. FFmpeg and XMP both have usable upstream build systems on
   Linux; that is the path of least resistance for those two.
3. Regenerated configuration headers. `jconfig.h`, `mz_config.h` and similar were generated
   for MSVC and are checked in; Linux needs its own, kept separate rather than overwritten.

The pragmatic choice is CMake for the whole tree, generated for both platforms, replacing
the `.vcxproj` files over time rather than in one step — but note that this contradicts the
current [third-party policy](third-party.md), which assumes hand-maintained `.vcxproj`
files. That policy would need updating as part of the port.

Compiler portability is a smaller but real cost: the code is C++20 on MSVC and uses SSE2
intrinsics directly in [util_simd.h](../src/util_simd.h) (already guarded, with an ARM NEON
path), MSVC-specific SAL annotations (`_Guarded_by_`, `_Acquires_exclusive_lock_`) that
need no-op definitions, and `#pragma`s that Clang and GCC will not recognise.

## Toolkit choice

The app draws its own UI. Only six control types are OS widgets — `edit`, `toolbar`,
`trackbar`, `button`, `date_time_control` and the unused `web_window` — and the section on
[controls](#controls) below argues they should stop being OS widgets on both platforms.
That means the toolkit needs to supply a window, an input stream, a pixel target, a
clipboard, drag and drop, and text input with IME — not a widget set.

Two credible options:

- **SDL3.** Covers window, input, clipboard, drag/drop, audio and GPU context in one
  dependency, on both X11 and Wayland. Weakest on IME, accessibility and native file
  dialogs; those come from XDG desktop portals separately.
- **GTK4.** Strongest on IME, portals, file dialogs and accessibility, and gives a native
  feel for the six widget types. Heavier dependency, and its rendering model wants to own
  the frame loop, which fits the app's own `WM_PAINT`-driven lifecycle less naturally.

The recommendation is SDL3 for the window and input layer with XDG portals for dialogs,
trash and file-manager integration, and the app's own drawn controls — because it keeps the
existing frame lifecycle intact and avoids re-implementing the control-paint contract
(`probe_buffered_control_paint`) that only exists to make native Windows controls behave.

Text is the exception: FreeType, HarfBuzz and fontconfig are needed regardless of toolkit,
and [platform.h](../src/platform.h) already states the contracts the port must satisfy —
`probe_glyph_fallback` and `probe_font_cache` are executable specifications for glyph
fallback and face caching.

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

### Recommendation: retire the native controls, on Windows first

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
6. **Accessibility.** Native controls expose themselves to screen readers through UI
   Automation for free. Drawn controls expose nothing unless UIA is implemented on Windows
   and AT-SPI on Linux. This is a genuine regression and a product decision, not an
   implementation detail — it belongs in [design.md](design.md) before the edit conversion,
   not after.
7. **Mnemonics and accelerators** (`&Cancel` and friends in
   [app_text.h](../src/app_text.h)), currently resolved by the dialog manager.

Items 1, 4, 5 and 7 are bounded UI work. Item 2 is the schedule risk. Item 6 is the one
that can make the answer "no".

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

Each stage ends in something falsifiable. No stage depends on a later one.

**Stage 0 — Make the boundary provable (Windows only).**
Fix everything in [Windows assumptions still in portable code](#windows-assumptions-still-in-portable-code):
`char16_t`/`char32_t` for UTF-16 and code points, UTF-8-only platform interface, the
D3D11VA descriptor, and a decision on path case sensitivity. Split the CPU rasterizer from
its GDI present path. *Gate:* `.\dd.ps1 test` green on Windows, and a search of `src/` for
`wchar_t`, `std::wstring` and Win32 types finds hits only under `platform_win*`.

**Stage 0b — Retire the native controls (Windows only).**
[Controls](#controls), in the order given there, each step shipped on Windows. Runs in
parallel with Stage 1 and gates Stage 4. *Gate:* per step, `.\dd.ps1 test` green and the
converted control judged against [design.md](design.md); on completion,
`probe_buffered_control_paint` and `control_base::handle()` are gone.

**Stage 1 — Headless core on Linux.**
Build the portable core plus a minimal `platform_linux*` implementing files, settings,
dates, locks, events and threads, and run the test suite. This is the highest-value
milestone: it proves that indexing, search, metadata, and the decode ladder are portable,
and it needs no UI at all. It also forces the third-party build system to exist.
*Gate:* the `/test:` suites that do not require a window pass on Linux.

**Stage 2 — Pixels.**
A Linux window that owns a BGRA buffer, presenting the already-portable CPU rasterizer.
*Gate:* the same scene rendered on Windows software and Linux software matches, using the
capture/compare approach [rendering.md](rendering.md) describes for backend parity.

**Stage 3 — Text.**
FreeType/HarfBuzz/fontconfig behind `measure_context` and `text_layout`.
*Gate:* `probe_glyph_fallback` and `probe_font_cache` pass on Linux with the same contract
they assert on Windows.

**Stage 4 — Input, clipboard, drag and drop, dialogs.**
The drawn controls from Stage 0b wired to Linux input, including text input and IME, plus
portal-backed file dialogs and trash. *Gate:* the view and command tests pass; targeting
and recovery behave as [design.md](design.md) requires.

**Stage 5 — GPU backend.**
Vulkan or OpenGL 3.3, with the 12 HLSL shaders ported. The colour and YUV maths must match
exactly; the parity list in [rendering.md](rendering.md) is the checklist.
*Gate:* GPU and software backends match on Linux to the same tolerance they match on
Windows.

**Stage 6 — Audio, then hardware video decode.**
PipeWire first; VAAPI last, because the renderer interop for NV12 textures is the hardest
single piece of the port and software decode is a working fallback until it lands.

## Risks

- **Path case sensitivity** is the one that can corrupt data rather than merely fail. It
  reaches the index, dedup, and collision handling. Resolve it in Stage 0, on Windows,
  where the existing tests can judge the answer.
- **Backend parity across three renderers** (Windows GPU, portable CPU, Linux GPU) triples
  the surface the parity contract has to hold over. Keeping the CPU rasterizer as the
  shared reference limits this.
- **Feature parity is not achievable** for the shell-integration list, so the port implies
  a Linux build that does less. That is a product decision, and it should be made
  explicitly rather than discovered at Stage 4.
- **Two build systems in parallel** will drift. Every third-party upgrade becomes two
  pieces of work until the `.vcxproj` files are retired.
- **Losing UI Automation** when the native controls go is the change most likely to be
  noticed by someone who cannot see it happen. It is reversible only by implementing
  accessibility directly, on both platforms.
- **IME and complex text input** is the single deepest piece of new UI work, and it has no
  partial credit: an edit that cannot compose Japanese or Korean is not shippable in a
  product that already ships those translations.
- **`platform_win_ui.cpp` at 7,678 lines** is one file doing window management, controls,
  dialogs, menus, clipboard, drag/drop and process entry. Its Linux counterpart should not
  reproduce that shape; splitting it by concern is worth doing before it is mirrored.
  Stage 0b removes a large part of it outright.

## Open decisions

These need answers before the stages they block:

1. Is the Linux build case-sensitive in its path model, or does it preserve the current
   case-insensitive matching? (Blocks Stage 0.)
2. Does deletion on Linux use XDG trash, and does "recoverable" mean the same thing to a
   user as it does on Windows? (Blocks Stage 4; belongs to [design.md](design.md).)
3. Is CMake adopted for the whole tree, or only for the Linux build? (Blocks Stage 1.)
4. Which of the shell-integration features are dropped on Linux versus reimplemented?
5. Are the native controls retired in favour of drawn ones, and if so what is the
   accessibility commitment that replaces UI Automation? (Blocks Stage 0b; belongs to
   [design.md](design.md).)
6. Which display server is targeted first — Wayland, X11, or both through SDL3?
7. What replaces the Segoe MDL2 icon font, and does the visual design change with it?
