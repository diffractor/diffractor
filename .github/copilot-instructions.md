# Diffractor Codebase

This is the Diffractor application codebase. Diffractor is the fastest photo and video organizer for Windows. It indexes your files to allow duplicate detection and fast search.

For build instructions, dependencies, and contribution guidelines, see the main [README.md](../README.md).
For detailed implementation documentation, see [implementation.md](../implementation.md).

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


## Third-Party Dependencies

Files in the `third-party` folder are external dependencies included in the repository for convenience. **Do not edit these files** as they are overwritten when libraries are upgraded.

Key dependencies include:
- **FFmpeg** - Audio/video decoding
- **SQLite** - Database storage
- **libexif, XMP SDK** - Metadata handling
- **libjpeg-turbo, libpng, libwebp, libheif, LibRaw** - Image formats
- **zlib-ng, minizip-ng** - Compression
- **Hunspell** - Spell checking

See [README.md](../README.md) for the complete list with versions.

## Source File Comments

Each `.h` and `.cpp` file in the `src` folder contains a concise comment at the top explaining what the file does. These comments appear after the copyright header and before the `#pragma once` directive (for headers) or includes (for source files). **Keep these comments up to date** when modifying files to ensure they accurately describe the file's purpose. These comments should have a `// Purpose:` marker to identify them and be updated when files change.

Example format for `.cpp` files:
```cpp
// This file is part of the Diffractor photo and video organizer
// Copyright(C) 2025  Zac Walker
// ... (rest of license header)

// Purpose: Brief description of what this file implements.

#include "pch.h"
```

## Building

1. Recursively clone the repository (includes FFmpeg and XMP SDK submodules)
2. Open `df.sln` in Visual Studio
3. Build the solution

## Testing

Diffractor has a built-in test runner accessible from the toolbar when running from Visual Studio. Tests are defined in `tests.cpp` and displayed via `view_test.cpp`.
