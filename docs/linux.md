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
  counterpart; see [Distributions and delivery](#distributions-and-delivery).

## Build system

CMake now describes the whole tree and generates for both platforms: one target for `src/`, and
one module per vendored library under `cmake/vendored/`, imported from the `.vcxproj` files by
`tools/import_vcxproj.py`. Anything the Windows project could not express — a header its build
generates, a flag a different compiler needs — lives beside it in `<name>.local.cmake`, which
re-importing does not discard. All 23 vendored libraries build under GCC. This supersedes the
hand-maintained `.vcxproj` assumption in the [third-party policy](third-party.md).

The two forks are the exception to "one module per library". `diffractor/XMP-Toolkit-SDK` builds
as a module like the rest; `diffractor/FFmpeg` does not, because restating which codecs exist
would be a second source of truth for it. On Windows it compiles from `ffmpeg.vcxproj` against a
`config-x64.h` that is checked in precisely because `configure` needs a shell; on Linux it is an
`ExternalProject` running the fork's own `configure` and `make`.

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
the 27 vendored libraries are distribution-invariant by construction; that is exactly the
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
  home, a case-folded directory, and a mounted NTFS volume. That rules out a build-time
  answer in [util_path.h](../src/util_path.h) — the comparison rule has to be a property of
  the path's location, or the index will disagree with the filesystem on one of them.
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
[toolkit recommendation](#toolkit-choice) assumes, so the sandbox enforces a constraint the
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
