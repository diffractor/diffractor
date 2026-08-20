# Diffractor
[![CI](https://github.com/diffractor/diffractor/actions/workflows/windows.yml/badge.svg)](https://github.com/diffractor/diffractor/actions/workflows/windows.yml)

Free, high-performance photo and video organizer for Windows. Optimized for speed and local file control—no cloud storage or subscriptions required.

![Diffractor screenshot](screenshot.webp)

## Features

| Category | Capabilities |
|----------|-------------|
| **Viewing** | Unified photo/video/audio playback; native support for most formats including RAW |
| **Zoom** | One scale-and-center model across mouse, keyboard, wheel, and touch; hold to inspect, an overview navigator, and linked side-by-side magnification |
| **Search** | Instant metadata-based search (tags, location, camera data) via local indexing |
| **Places** | Photos and videos are placed from GPS alone, searchable by place, city, state, country, or distance, with a map and a timeline of derived visits |
| **Metadata** | Read/write XMP, IPTC, EXIF, ID3; add tags, ratings, location |
| **Organization** | Side-by-side comparison, and duplicate detection ("Presence") that recognizes a re-encoded, resized, or rotated copy as well as an identical one |
| **Related items** | Finds possible copies, the same album, the same series, and the closest capture times and places, each as its own group |
| **File operations** | Rename, Import, and Sync preview every file before they run, and state how each name collision will be resolved |
| **Sync** | Bidirectional sync to NAS/network drives for backup and collaboration |
| **Editing** | Resize, rotate, crop, perspective, temperature and tint, color adjustment, and automatic color, straighten, and document correction |
| **Languages** | English plus 13 fully translated languages |

## Quick Start

1. Download from [diffractor.com](https://diffractor.com/) and install
2. Point to your media folders—Diffractor indexes automatically
3. Browse via sidebar (date, location, folder, file type)
4. Apply ratings/tags via keyboard shortcuts or UI; select two like items to compare them for burst sorting
5. Sync to network drives; use metadata tools for maintenance

**Why Diffractor?** Fast (handles large libraries without lag), private (local-only), lightweight, free.

## Deployment Options

| Distribution | Description |
|--------------|-------------|
| **Windows Desktop** | Traditional installer (`diffractor-setup.exe`) with auto-update support |
| **Windows Store** | MSIX package for Microsoft Store distribution (updates handled by Store) |
| **Portable** | ZIP archive for standalone use without installation |

Both Desktop and Store builds share the same codebase. The `WINSTORE` preprocessor define controls Store-specific behavior (disables built-in auto-update since the Store handles updates).

## Building

```bash
git clone --recursive https://github.com/diffractor/diffractor.git
```

The build is CMake on both platforms. Submodules: [FFmpeg](https://github.com/diffractor/FFmpeg), [XMP-SDK](https://github.com/diffractor/XMP-Toolkit-SDK).

Everything goes through the `dd` gateway, which finds Visual Studio, configures with Ninja and puts the binary in `exe/`:

```powershell
.\dd.ps1 test                                            # build, lint, test, check translations
python tools/dd.py build --config Release --arch x86     # the 32-bit desktop binary
python tools/dd.py build --winstore                      # the Store binary
```

For debugging and profiling in the IDE, generate projects on demand — they are not checked in:

```powershell
cmake -S . -B tmp/vs -G "Visual Studio 18 2026" -A x64
```

### Build Script

Use `dd.ps1` from a Developer PowerShell:

| Command | Description |
|---------|-------------|
| `.\dd.ps1` | Show usage information and current version |
| `.\dd.ps1 build` | Build x64 Debug and Release, auto-increments build number |
| `.\dd.ps1 test` | Run the unit tests and validate the translation catalogs |
| `.\dd.ps1 bean` | Run the tests with file-I/O scratch on an SMB share |
| `.\dd.ps1 desktop` | Build desktop versions (Win32 + x64), auto-increments build number |
| `.\dd.ps1 store` | Build Windows Store MSIX package, auto-increments build number |
| `.\dd.ps1 run` | Run the recently built diffractor64.exe |
| `.\dd.ps1 cpu` | Run diffractor64.exe with CPU software rendering |
| `.\dd.ps1 bump-build` | Manually increment build number without building |
| `.\dd.ps1 bump-ver` | Increment minor version (e.g., 1.26.2 -> 1.26.3) |

**Prerequisites for release builds:** NSIS, 7-Zip, Windows SDK, code signing certificate.

## Documentation

| Document | Subject |
|----------|---------|
| [docs/design.md](docs/design.md) | Durable product behavior and the scope/contents/target/effect model |
| [docs/implementation.md](docs/implementation.md) | Architecture, data flow, threading, and validation |
| [docs/collections.md](docs/collections.md) | The collection: membership, what it earns, and its edge |
| [docs/locations.md](docs/locations.md) | Places, location search, distance, and visits |
| [docs/zoom.md](docs/zoom.md) | The zoom model, behavior contract, and how it is judged |
| [docs/selection-controls.md](docs/selection-controls.md) | The selection panel: form, content, ordering, and density |
| [docs/file-io.md](docs/file-io.md) | Reading, writing, and change response for media files |
| [docs/metadata.md](docs/metadata.md) | Property-to-tag mapping across XMP, EXIF, IPTC, and container tags |
| [docs/rendering.md](docs/rendering.md) | Surfaces, color, and the draw backends |
| [docs/third-party.md](docs/third-party.md) | Vendored dependencies and how they are updated |
| [docs/v-1.27.2.md](docs/v-1.27.2.md) | What the current release changed, and how it was verified |
| [docs/v-next.md](docs/v-next.md) | Deferred work, open issues, and constraints that must not be lost |

## Contributing

Contributions welcome via [issues](https://github.com/diffractor/diffractor/issues).

**Translations:** Edit PO files with [poedit](https://poedit.net/). Files in [exe/languages](https://github.com/diffractor/diffractor/tree/master/exe/languages). Use German as template for new languages. Test by copying to `%LOCALAPPDATA%\Diffractor\languages`.

**Tests:** Run the embedded suite with `diffractor64.exe /test` or `diffractor64.exe /test:*filter*`; the process exits with `0` when all selected tests pass and `1` when any test fails. `/run-tests` remains an alias for a complete command-line run.

**Diagnostics (Debug builds only):** The former test-view diagnostics are available as startup options. `-test-reset-graphics` resets graphics resources, `-test-new-version` simulates the new-version UI, `-test-send-crash-report` submits the test crash report, and `-test-crash` deliberately crashes the process. The non-crashing options leave Diffractor open for inspection.

**Documentation screenshots (Debug builds only):** Use `-screenshot:<scene>` with a media file or folder. Diffractor opens the requested scene in a 1600 x 1000 window, waits 10 seconds, captures and scales the window to 800 x 500, replaces `screenshot.webp` beside the source tree, and exits. The optional `-screenshot-output:<absolute-path>` selects a different image file and format.

```powershell
.\exe\diffractor64-d.exe -screenshot:items "C:\Users\zacwalker\Pictures\favs\2021-01-05 18.45.43.jpg"
```

Supported scenes: `items`, `fullscreen`, `edit`, `edit-preview`, `rename`, `adjust-date`, `convert`, `metadata`, `import`, `sync`, and `locate`.

