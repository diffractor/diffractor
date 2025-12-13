# Diffractor
[![CI](https://github.com/diffractor/diffractor/actions/workflows/msbuild.yml/badge.svg)](https://github.com/diffractor/diffractor/actions/workflows/msbuild.yml)

Free, high-performance photo and video organizer for Windows. Optimized for speed and local file control—no cloud storage or subscriptions required.

## Features

| Category | Capabilities |
|----------|-------------|
| **Viewing** | Unified photo/video/audio playback; native support for most formats including RAW |
| **Search** | Instant metadata-based search (tags, location, camera data) via local indexing |
| **Metadata** | Read/write XMP, IPTC, EXIF, ID3; add tags, ratings, location |
| **Organization** | Side-by-side comparison, duplicate detection ("Presence" feature) |
| **Sync** | Bidirectional sync to NAS/network drives for backup and collaboration |
| **Editing** | Resize, rotate, crop, color adjustment |

## Quick Start

1. Download from [diffractor.com](https://diffractor.com/) and install
2. Point to your media folders—Diffractor indexes automatically
3. Browse via sidebar (date, location, folder, file type)
4. Apply ratings/tags via keyboard shortcuts or UI; use Compare view for burst sorting
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

Open `df.sln` in Visual Studio 2022. Submodules: [FFmpeg](https://github.com/diffractor/FFmpeg), [XMP-SDK](https://github.com/diffractor/XMP-Toolkit-SDK).

### Build Script

Use `dd.ps1` from a Developer PowerShell:

| Command | Description |
|---------|-------------|
| `.\dd.ps1` | Show usage information and current version |
| `.\dd.ps1 desktop` | Build desktop versions (Win32 + x64), auto-increments build number |
| `.\dd.ps1 store` | Build Windows Store MSIX package, auto-increments build number |
| `.\dd.ps1 run` | Run the recently built diffractor64.exe |
| `.\dd.ps1 bump-build` | Manually increment build number without building |
| `.\dd.ps1 bump-ver` | Increment minor version (e.g., 1.26.2 ? 1.26.3) |

**Prerequisites for release builds:** NSIS, 7-Zip, Windows SDK, code signing certificate.

See [implementation.md](implementation.md) for architecture details.

## Contributing

Contributions welcome via [issues](https://github.com/diffractor/diffractor/issues).

**Translations:** Edit PO files with [poedit](https://poedit.net/). Files in [exe/languages](https://github.com/diffractor/diffractor/tree/master/exe/languages). Use German as template for new languages. Test by copying to `%LOCALAPPDATA%\Diffractor\languages`.

**Tests:** Built-in runner accessible via toolbar checkmark button when launched from Visual Studio. Press Escape to exit test mode.

## Dependencies

| Library | Version | Library | Version |
|---------|---------|---------|---------|
| [brotli](https://github.com/google/brotli) | 1.1.0 | [libjxl](https://github.com/libjxl/libjxl) | 0.10.3 |
| [bzip2](https://sourceware.org/bzip2/) | 1.0.8 | [liblzma](https://github.com/tukaani-project/xz) | 5.4.6 |
| [dav1d](https://code.videolan.org/videolan/dav1d) | 1.5.1 | [libmatroska](https://github.com/Matroska-Org/libmatroska) | 1.7.1 |
| [dng-sdk](https://helpx.adobe.com/camera-raw/digital-negative.html) | 1.7.1 | [libopenmpt](https://lib.openmpt.org) | 0.8.3 |
| [expat](https://libexpat.github.io/) | 2.7.1 | [libpng](http://www.libpng.org/pub/png/libpng.html) | 1.6.50 |
| [ffmpeg](https://ffmpeg.org/) | main | [LibRaw](https://www.libraw.org) | 0.21.2 |
| [highway](https://github.com/google/highway) | 1.3.0 | [libwebp](https://github.com/webmproject/libwebp) | 1.4.0 |
| [hunspell](https://github.com/hunspell/hunspell) | 1.7.2 | [minizip-ng](https://github.com/zlib-ng/minizip-ng) | 4.0.5 |
| [libarchive](https://github.com/libarchive/libarchive) | 3.8.1 | [parallel-hashmap](https://github.com/greg7mdp/parallel-hashmap) | 2.0.0 |
| [libde265](https://github.com/strukturag/libde265) | 1.0.15 | [rapidjson](https://github.com/Tencent/rapidjson) | main |
| [libebml](https://github.com/Matroska-Org/libebml) | 1.4.5 | [skcms](https://skia.googlesource.com/skcms) | main |
| [libexif](https://github.com/libexif/libexif) | 0.6.25 | [sqlite](https://www.sqlite.org/) | 3.50.4 |
| [libheif](https://github.com/strukturag/libheif) | 1.18.0 | [utf-cpp](https://github.com/nemtrif/utfcpp) | 4.0.6 |
| [libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo) | 3.1.2 | [zlib-ng](https://github.com/zlib-ng/zlib-ng) | 2.2.5 |
