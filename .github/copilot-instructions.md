# Diffractor Codebase

This is the Diffractor application codebase. Diffractor is the fastest photo and video organizer for Windows. It indexes your files to allow duplicate detection and fast search.

For build instructions, dependencies, and contribution guidelines, see the main [README.md](../README.md).
For detailed implementation documentation, see [implementation.md](../implementation.md).

Place temporary files in a tmp/ folder (test output etc.)

Always build using df.sln

## Quick Reference

- **Language**: C++20 for Windows
- **UI Framework**: Custom Direct2D/Direct3D 11 rendering
- **Database**: SQLite for metadata persistence
- **Media Decoding**: FFmpeg, LibRaw, libheif, libjpeg-turbo, libpng, libwebp
- **Metadata**: libexif, XMP SDK, IPTC support

## Code Architecture

The source code in `src/` is organized into modules identified by filename prefixes. Files with the same prefix before the underscore belong to the same logical module.

### Module Overview

| Prefix | Module | Purpose |
|--------|--------|---------|
| `app*` | Application | Main app logic, commands, settings, text/localization, sidebar, toolbar |
| `av*` | Audio/Video | FFmpeg integration, media playback, audio output, visualizer |
| `crypto*` | Cryptography | Hashing (MD5, SHA, CRC32), AES encryption, HMAC |
| `files*` | File Formats | Format-specific loaders/savers (JPEG, PNG, RAW, HEIF, WebP, PSD) |
| `metadata*` | Metadata | EXIF, IPTC, XMP, ICC profile parsing and writing |
| `model*` | Data Model | Index, database, items, search, tags, locations, properties |
| `platform*` | Platform | Windows API abstraction (UI, D3D11, fonts, sound, networking, WIC) |
| `render*` | Rendering | Image surfaces, color adjustments, pixel operations |
| `ui*` | User Interface | Controls, dialogs, views, layout elements, controllers |
| `util*` | Utilities | Strings, geometry, dates, ZIP, base64, spell check, helpers |
| `view*` | Views | Specific UI views (items grid, edit, import, rename, sync, test) |
| `test*` | Tests | Unit and integration tests |


## Third-Party Dependencies

Files in the `third-party` folder are external dependencies included in the repository for convenience. **Do not edit these files** as they are overwritten when libraries are upgraded.

Key dependencies include:
- **FFmpeg** - Audio/video decoding
- **SQLite** - Database storage
- **libexif, XMP SDK** - Metadata handling
- **libjpeg-turbo, libpng, libwebp, libheif, LibRaw** - Image formats
- **zlib-ng, minizip-ng** - Compression
- **Hunspell** - Spell checking

See [README.md](../README.md) for the complete list with versions, folder paths, dependency types, and update source URLs.

### Upgrading a Source-Copy Dependency

No package manager is used. Source code is manually copied and built via custom `.vcxproj` files.

1. **Download** the new release source from the GitHub repo listed in the README dependencies table.
2. **Replace** the source files in the corresponding `third-party/<folder>` (or `Include/<folder>` for header-only libs), preserving the existing `.vcxproj` and `.vcxproj.filters` files.
3. **Update version headers** if the library requires it (e.g., `de265-version.h`, `heif_version.h`, `jconfig.h`, `vcs_version.h`).
4. **Diff the `.vcxproj`** against the new source file list. If `.c`/`.cpp`/`.h` files were added or removed upstream, update the `<ClCompile>` and `<ClInclude>` items in the `.vcxproj` accordingly. Do not regenerate the vcxproj from scratch.
5. **Build** the solution (`df.sln`) for both Win32 and x64 Debug/Release to verify compilation.
6. **Run tests** via the built-in test runner (toolbar checkmark button) to verify nothing is broken.
7. **Update the version** in the README dependencies table.

### Upgrading a Fork Dependency (FFmpeg, XMP SDK)

FFmpeg and XMP SDK are maintained as Diffractor forks and included as git submodules.

- **FFmpeg** fork: [diffractor/FFmpeg](https://github.com/diffractor/FFmpeg), upstream: [FFmpeg/FFmpeg](https://github.com/FFmpeg/FFmpeg)
  - Fork adds a custom `ffmpeg.vcxproj` for MSVC builds.
  - To update: rebase the fork on latest upstream master, resolve conflicts in the vcxproj, then update the submodule pointer in this repo.

- **XMP SDK** fork: [diffractor/XMP-Toolkit-SDK](https://github.com/diffractor/XMP-Toolkit-SDK), upstream: [adobe/XMP-Toolkit-SDK](https://github.com/adobe/XMP-Toolkit-SDK)
  - Fork adds: POPM/TPE2 reconciliation for MP3, Windows tag support, C++17 fixes, WebP support from Exempi, and a custom `xmp.vcxproj`.
  - To update: rebase the fork on latest upstream, resolve conflicts preserving the Diffractor-specific changes, then update the submodule pointer.

### Upgrading a Header-Only Dependency

For `parallel-hashmap` (in `Include/parallel_hashmap/`) and `utf-cpp` (in `Include/utf8-cpp/`): download the new release and replace the header files in the corresponding `Include/` subfolder. No vcxproj changes needed.

## Source File Comments

Each `.h` and `.cpp` file in the `src` folder contains a concise comment at the top explaining what the file does. These comments appear after the copyright header and before the `#pragma once` directive (for headers) or includes (for source files). **Keep these comments up to date** when modifying files to ensure they accurately describe the file's purpose. These comments should have a `// Purpose:` marker to identify them and be updated when files change.

Example format for `.cpp` files:
```cpp
// This file is part of the Diffractor photo and video organizer
// Copyright 2026  Zac Walker
// ... (rest of license header)

// Purpose: Brief description of what this file implements.

#include "pch.h"
```

## Building

1. Recursively clone the repository (includes FFmpeg and XMP SDK submodules)
2. Open `df.sln` in Visual Studio
3. Build the solution

### Test Runner

Run tests after changes from the command line:

```
diffractor64.exe /test
```

A wildcard filter can be used to run a subset of tests by name:

```
diffractor64.exe /test:wildcard
```

The filter uses case-insensitive wildcard matching (`*` and `?`). Examples:
- `/test` or `/test:*` — run all tests (default)
- `/test:*scan*` — run tests with "scan" in the name