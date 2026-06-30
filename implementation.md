# Diffractor Implementation Guide

This document provides detailed implementation information for the Diffractor photo and video organizer. For general usage and build instructions, see [README.md](README.md).

## Technology Stack

| Component | Technology |
|-----------|------------|
| Language | C++20 for Windows |
| UI Framework | Custom Direct2D/Direct3D 11 rendering |
| Database | SQLite for metadata persistence |
| Media Decoding | FFmpeg, LibRaw, libheif, libjpeg-turbo, libpng, libwebp |
| Metadata | libexif, XMP SDK, IPTC support |

## Architecture Overview

Source code in `src/` is organized by filename prefix. Files sharing a prefix belong to the same module:

| Prefix | Module | Purpose |
|--------|--------|---------|
| `app*` | Application | Commands, settings, UI framework |
| `av*` | Audio/Video | FFmpeg integration |
| `crypto*` | Cryptography | Hashing, encryption |
| `files*` | File Formats | Image/video loaders |
| `metadata*` | Metadata | EXIF, IPTC, XMP, ICC parsing |
| `model*` | Data Model | Indexing, database, items |
| `platform*` | Platform | Windows abstraction |
| `render*` | Rendering | Image/color processing |
| `ui*` | UI Framework | Controls, layout |
| `util*` | Utilities | Common functions, types |
| `view*` | Views | Grid, edit, import, etc. |

## Build Configurations

Diffractor supports multiple deployment targets from the same codebase:

| Configuration | Output | Description |
|---------------|--------|-------------|
| `Release\|Win32` | `diffractor32.exe` | 32-bit desktop build |
| `Release\|x64` | `diffractor64.exe` | 64-bit desktop build |
| `WinStore\|x64` | `diffractor.exe` | Windows Store (MSIX) build |

### `WINSTORE` Preprocessor Define

The `WINSTORE` define controls features that differ between Desktop and Store builds:

```cpp
#ifndef WINSTORE
    // Desktop-only features
    bool check_for_updates = true;   // Check online for newer versions
    bool is_tester = false;          // Beta tester flag
#endif
```

**Features disabled for Windows Store builds:**
- **Update checking** (`check_for_updates`) – Store handles updates
- **Beta tester mode** (`is_tester`) – Not applicable for Store distribution
- **Manual update flow** – The desktop build checks the `/ver` endpoint for a
  newer version and surfaces it via the lightbulb toolbar button and a
  "Check for updates" menu item. When an update is available the user can click
  "Install now" to download the installer to the Downloads folder and run it
  (the installer closes any running instance). There is no silent/automatic
  installation.

**Rationale:** Windows Store apps receive updates through the Microsoft Store infrastructure. Including built-in update functionality would conflict with Store policies and create a confusing user experience.

### Build Script (`build.cmd`)

The build script supports two modes:

| Command | Description |
|---------|-------------|
| `build.cmd build` | Build binaries only (fast iteration during development) |
| `build.cmd release` | Full release build with installers and signing |

**Release build steps:**
1. Builds `Release|Win32` and `Release|x64` for desktop installer
2. Builds with `WINSTORE` define for Store package
3. Signs all executables
4. Creates NSIS installer (`diffractor-setup.exe`)
5. Creates MSIX package for Store submission
6. Signs installer and MSIX package
7. Adds symbols to symbol store
8. Creates portable ZIP distribution

## Module Reference

### Application Layer (`app_`)

Core application logic, command handling, and user interface coordination.

| File | Purpose |
|------|---------|
| `app.cpp` | Main application implementation. Handles app initialization, window management, command processing, toolbar/menu creation, and coordinates all background worker threads. |
| `app.h` | Main application frame and view management. Contains the app_frame class which orchestrates the UI, handles commands, manages views, and coordinates background tasks. |
| `app_commands.cpp` | Command handlers for all user actions. Implements file operations, editing, sharing, and navigation commands. |
| `app_commands.h` | Command identifier enumeration. Defines all available application commands as strongly-typed enum values. |
| `app_command_line.h` | Command line argument parsing. Handles startup options like folder paths, search queries, and debug flags. |
| `app_command_status.h` | Progress dialog and status tracking for long-running operations. Displays progress, handles cancellation, and reports results. |
| `app_icons.h` | Icon definitions using Segoe UI Symbol font codepoints. Maps icon_index enum values to Unicode glyphs. |
| `app_match.h` | Auto-complete matching logic for search. Implements folder and text matching with highlight support for the search box dropdown suggestions. |
| `app_settings.cpp` | Application settings persistence. Reads and writes all user preferences to the registry including display options, import/sync settings, and collection configuration. |
| `app_settings.h` | Application settings and configuration. Defines all persistent user preferences including display options, collection folders, import/sync settings, and feature flags. |
| `app_sidebar.h` | Sidebar navigation panel. Contains collection overview charts, folder list, favorite searches, tags, ratings, labels, and drive information displays. |
| `app_text.cpp` | Localization and internationalization support. Loads language files (PO format) and provides text translation services for the entire application. |
| `app_text.h` | Localized text strings and translations. Contains all user-visible text for internationalization support with PO file loading and plural text formatting. |
| `app_util.cpp` | Utility functions for file operations including import, sync, and batch processing. |
| `app_util.h` | Utility functions for file operations. Includes rename templating, import/sync analysis, collection management, and file operation helpers. |

### Audio/Video Processing (`av_`)

FFmpeg integration for media playback, decoding, and visualization.

| File | Purpose |
|------|---------|
| `av_format.cpp` | FFmpeg media decoder implementation. Provides video/audio decoding, frame scaling, audio resampling, metadata extraction, and hardware acceleration support. |
| `av_format.h` | FFmpeg-based media format decoder. Handles video/audio stream parsing, codec management, frame extraction, seeking, and format conversion using libavformat and libavcodec. |
| `av_player.h` | Media playback session and player management. Coordinates audio/video decoding threads, handles play/pause/seek operations, and synchronizes audio-video timing. |
| `av_sound.h` | Audio output interface and buffer management. Defines audio format structures, sample buffering, and abstract audio device interface for playback. |
| `av_stubs.cpp` | FFmpeg stub functions for unsupported CPU architectures (x86, ARM, PowerPC). |
| `av_visualizer.h` | Audio spectrum visualization using FFT. Transforms audio samples into frequency data and renders animated bar visualizations synchronized with playback. |

### Cryptography (`crypto_`)

Hashing algorithms and encryption for file integrity and authentication.

| File | Purpose |
|------|---------|
| `crypto.cpp` | Cryptographic utilities including HMAC-SHA1, AES encryption/decryption, CRC32 checksums, and FNV-1a hashing for file integrity and authentication. |
| `crypto.h` | Cryptographic utilities facade. Provides HMAC-SHA1, CRC32C, FNV-1a hashing, and encryption/decryption functions used throughout the application. |
| `crypto_aes256.cpp` | AES-256 encryption and decryption implementation. Provides block cipher operations for secure data encryption with 256-bit keys. |
| `crypto_aes256.h` | AES-256 encryption and decryption implementation. Provides block cipher operations for secure data encryption with streaming support. |
| `crypto_md5.cpp` | MD5 hash algorithm implementation. Computes 128-bit message digests for data integrity verification. |
| `crypto_md5.h` | MD5 hash algorithm implementation. Computes 128-bit message digests for data integrity verification and identifier generation. |
| `crypto_sha.cpp` | SHA-1 and SHA-256 hash algorithm implementations. Computes secure message digests for authentication and data integrity. |
| `crypto_sha.h` | SHA-1 and SHA-256 hash algorithm implementations. Computes secure cryptographic digests for data integrity and authentication. |

### File Format Handlers (`files_`)

Format-specific loaders and savers for images and media files.

| File | Purpose |
|------|---------|
| `files.h` | Core file operations and format handling. Coordinates format detection, metadata extraction, image loading/saving, and file type classification for photos, videos, and archives. |
| `files_core.cpp` | Core file handling operations. Manages file loading, saving, and format detection for images and media files. Coordinates metadata extraction across different file types. |
| `files_commodore.cpp` | Commodore 64 disk image (D64, D81, T64, CRT) parser. Reads and lists contents of retro computing disk and cartridge formats. |
| `files_heif.cpp` | HEIF/HEIC image format support. Decodes High Efficiency Image Format files using libheif, extracts metadata, and handles HDR content. |
| `files_jpeg.cpp` | JPEG image processing. Handles loading, saving, lossless rotation, and metadata preservation using libjpeg-turbo. |
| `files_jpeg.h` | JPEG processing declarations. Defines interfaces for JPEG loading, saving, and lossless transformations. |
| `files_png.cpp` | PNG image format support. Loads and saves PNG files using libpng, handles ICC profiles, EXIF metadata, and XMP data. |
| `files_psd.cpp` | Adobe Photoshop (PSD) file format parser. Decodes layered images, extracts metadata (IPTC, XMP, EXIF, ICC), and converts color modes. |
| `files_raw.cpp` | Camera RAW image format support. Decodes RAW files from various cameras using LibRaw and Adobe DNG SDK, extracts metadata and thumbnails. |
| `files_scan_photo.cpp` | Image file scanning and metadata extraction. Detects image formats, extracts embedded thumbnails, and parses header information. |
| `files_structs.h` | Binary file format structure definitions. Contains packed structs for AVI, WAV, BMP headers and other multimedia container formats. |
| `files_webp.cpp` | WebP image format support. Loads and saves WebP files using libwebp, handles animation, ICC profiles, EXIF, and XMP metadata. |

### Metadata Handling (`metadata_`)

Parsing and writing of image metadata standards.

| File | Purpose |
|------|---------|
| `metadata_exif.cpp` | EXIF metadata extraction and writing. Parses camera settings, dates, GPS coordinates, and other EXIF tags from JPEG and other image formats. |
| `metadata_exif.h` | EXIF metadata parsing and writing. Extracts camera information, GPS coordinates, timestamps, and orientation from image files using libexif. |
| `metadata_icc.cpp` | ICC color profile parsing. Reads and interprets ICC profile data to extract color space information and profile metadata. |
| `metadata_icc.h` | ICC color profile parsing. Extracts color space information from embedded ICC profiles in image files. |
| `metadata_iptc.cpp` | IPTC metadata parsing. Extracts news industry metadata including captions, keywords, copyright, and geographic information. |
| `metadata_iptc.h` | IPTC metadata parsing. Extracts news and editorial metadata including captions, keywords, and copyright information from image files. |
| `metadata_xmp.cpp` | XMP (Extensible Metadata Platform) support. Reads and writes Adobe XMP metadata using the XMP Toolkit SDK for comprehensive metadata handling. |
| `metadata_xmp.h` | XMP metadata parsing and writing using Adobe XMP SDK. Handles extensible metadata including ratings, labels, keywords, and editing history. |

### Data Model (`model_`)

Indexing engine, database layer, and item representation.

| File | Purpose |
|------|---------|
| `model.cpp` | Application view state management. Coordinates display state, media playback, selection handling, navigation history, and view mode transitions. |
| `model.h` | View state coordination and navigation. Manages display state, media playback, item selection, history, grouping, filtering, and synchronization between views. |
| `model_db.cpp` | SQLite database layer. Manages persistent storage of indexed file metadata, thumbnails, and application state using SQLite database. |
| `model_db.h` | SQLite database layer for persistent storage. Manages item thumbnails, metadata cache, import history, and web service cache with efficient batch writes. |
| `model_db_pack.h` | Metadata serialization for database storage. Packs and unpacks item metadata into compact binary format for efficient SQLite storage. |
| `model_index.cpp` | File indexing engine. Scans folders, extracts metadata, builds searchable index, detects duplicates, and manages the in-memory item collection. |
| `model_index.h` | File indexing engine and duplicate detection. Scans folders, maintains item index, calculates summaries, and identifies duplicate files by name, date, and CRC. |
| `model_items.cpp` | File item representation. Defines item_element class for individual files, manages thumbnail caching, metadata display, and selection state. |
| `model_items.h` | Item representation and selection. Defines item_element for files/folders, item_set for collections, item_group for grouping, and thumbnail management. |
| `model_location.h` | GPS coordinate and location structures. Defines gps_coordinate for geographic positioning and location metadata. |
| `model_locations.cpp` | Geographic location database. Manages country and city lookups for GPS coordinates, provides reverse geocoding from latitude/longitude. |
| `model_locations.h` | Geographic location cache and reverse geocoding. Uses KD-tree for efficient nearest-location lookups and provides place name auto-complete from city database. |
| `model_property.cpp` | Property definitions and metadata types. Defines all displayable and searchable properties with formatting rules. |
| `model_property.h` | Property definitions and metadata formatting. Defines all item property types (dimensions, dates, camera info, etc.) and provides formatting functions for display. |
| `model_search.cpp` | Search query execution. Matches items against search criteria, filters by properties, and ranks results. |
| `model_search.h` | Search query parsing and execution. Parses user search input into structured queries and matches items against criteria. |
| `model_tags.cpp` | Tag management implementation. Handles parsing, merging, and comparison of keyword tags used for organization. |
| `model_tags.h` | Tag management utilities. Provides tag_set class for managing keyword collections with add/remove operations and string formatting. |
| `model_tokenizer.h` | Search query tokenizer. Parses search input text into structured parts with modifiers, scopes, and terms for the search engine. |

### Platform Abstraction (`platform_`)

Windows-specific implementations and OS integration.

| File | Purpose |
|------|---------|
| `platform.h` | Platform abstraction interface. Defines cross-platform APIs for file system, threading, networking, clipboard, and system services. |
| `platform_win.cpp` | Windows platform abstraction layer. Provides OS-specific implementations for file system, threading, networking, and shell integration. |
| `platform_win.h` | Windows platform implementation. Implements file system, shell integration, registry access, and Windows-specific functionality. |
| `platform_win_d3d11.cpp` | Direct3D 11 rendering backend. Implements GPU-accelerated texture rendering, shader management, and hardware video decoding support. |
| `platform_win_font.cpp` | DirectWrite font rendering. Handles text layout, font loading, glyph rendering, and text measurement using DirectWrite API. |
| `platform_win_res.h` | Windows resource identifiers. Defines resource IDs for icons, dialogs, cursors, shaders, and other Windows resources used by the application. |
| `platform_win_sound.cpp` | Windows audio output using WASAPI. Manages audio device selection, buffer playback, and audio stream synchronization. |
| `platform_win_ui.cpp` | Windows UI framework. Implements window management, message handling, input processing, clipboard, drag-drop, and system integration. |
| `platform_win_visual.h` | Windows visual styles and theming. Handles DWM composition, visual style detection, and Windows appearance settings. |
| `platform_win_web.cpp` | HTTP client using WinInet. Handles web requests, downloads, form uploads, and network connectivity checks. |
| `platform_win_wic.cpp` | Windows Imaging Component integration. Provides fallback image decoding for formats not handled by specialized decoders. |

### Rendering (`render_`)

Image processing and color manipulation.

| File | Purpose |
|------|---------|
| `render_color.cpp` | Color adjustment algorithms. Implements spline-based tone curves, saturation, vibrance, contrast, and brightness adjustments for image editing. |
| `render_image.cpp` | Image surface operations. Implements pixel-level transformations including RGB/BGR swapping and pixel difference calculations. |
| `render_surface.cpp` | Image surface management. Handles bitmap memory allocation, format conversion, and surface operations. |

### User Interface (`ui_`)

UI framework, controls, and layout management.

| File | Purpose |
|------|---------|
| `ui.cpp` | UI element layout and rendering. Implements view controllers, image layout algorithms, and comparison view controls. |
| `ui.h` | Core UI framework and rendering abstractions. Defines colors, surfaces, textures, drawing contexts, controls, frames, and platform-independent UI primitives. |
| `ui_controllers.h` | UI input controllers. Handles mouse interactions for clickable elements, selection movement, handle dragging, and other user input behaviors. |
| `ui_controls.h` | Custom UI controls and visual elements. Implements rating controls, photo viewers, video controls, scrubbers, hex displays, and other interactive UI components. |
| `ui_dialog.h` | Dialog management and layout. Provides dialog construction helpers, standard dialog layouts, and modal dialog handling. |
| `ui_elements.h` | Base view elements and building blocks. Defines text, icons, links, dividers, thumbnails, tables, and other fundamental UI components. |
| `ui_map.h` | Map view and location display using OpenStreetMap tiles. Handles tile fetching, caching, panning, zooming, and GPS coordinate display. |
| `ui_text_view.h` | Text viewer for documents. Displays text files with scrolling and selection support. |
| `ui_view.h` | View framework and element hosting. Defines view_element base class, view_controller for interactions, and view hosting infrastructure. |

### Utilities (`util_`)

Common utility functions, data types, and helper classes.

| File | Purpose |
|------|---------|
| `util.cpp` | Core utility functions and data types. Implements file paths, dates, blobs, logging, and other foundational types used throughout the application. |
| `util.h` | Core utility types and functions. Defines fundamental types (file_size, date_t, blob), memory helpers, logging, and common utility functions used throughout the application. |
| `util_base64.cpp` | Base64 encoding and decoding. Provides conversion between binary data and base64 text representation for web APIs and data serialization. |
| `util_base64.h` | Base64 encoding and decoding. Provides functions for converting binary data to/from base64 text representation. |
| `util_crash_files_db.h` | Crash database management. Tracks recent files for crash recovery and stores application state to restore after unexpected termination. |
| `util_date.h` | Date and time utilities. Defines date_t class for timestamps, date calculations, timezone conversion, and date formatting. |
| `util_file.h` | File I/O helpers. Provides memory-mapped file reading, stream utilities, and buffered I/O operations. |
| `util_geometry.cpp` | Geometry calculations. Implements quadrilateral transformations, rotation, cropping bounds, and affine transform calculations. |
| `util_geometry.h` | Geometry primitives and math. Defines point, size, rectangle, and quad types with transformation, intersection, and scaling operations. |
| `util_interfaces.h` | Common interfaces and abstract types. Defines shared interfaces used across modules for decoupling and extensibility. |
| `util_json.h` | JSON parsing and generation. Provides simple JSON document reading and writing for configuration and API responses. |
| `util_kdtree.h` | KD-tree spatial index for nearest-neighbor searches. Used by location cache to efficiently find closest cities to GPS coordinates. |
| `util_map.h` | Hash map and set templates. Provides custom hash containers optimized for file paths, strings, and other common key types. |
| `util_path.h` | File path handling. Defines file_path and folder_path classes for type-safe path manipulation, comparison, and iteration. |
| `util_selector.h` | File selection patterns. Implements item_selector for matching files by path patterns with recursive folder support. |
| `util_simd.h` | SIMD intrinsics wrappers. Provides SSE/AVX/NEON optimized implementations for image processing and CRC calculations. |
| `util_spell.cpp` | Spell checking integration. Uses Hunspell library for spell checking, suggestions, and custom dictionary management. |
| `util_spell.h` | Spell checking integration. Wraps Hunspell library for spell checking user input in metadata fields. |
| `util_strings.cpp` | String manipulation utilities. Implements UTF-8/UTF-16 conversion, string formatting, parsing, splitting, and comparison functions. |
| `util_strings.h` | String manipulation utilities. Provides UTF-8/UTF-16 conversion, string comparison, splitting, formatting, and caching. |
| `util_text.h` | Text formatting and natural language processing. Handles pluralization, localized text formatting, and text templates. |
| `util_top.h` | Top-N tracking container. Maintains a sorted list of the N largest or most frequent items for statistical summaries. |
| `util_zip.cpp` | ZIP archive handling. Creates and extracts ZIP files, lists archive contents, and provides zlib compression/decompression. |
| `util_zip.h` | ZIP archive handling using minizip. Provides archive creation, extraction, and listing for backup and export features. |

### Application Views (`view_`)

Specialized views for different application modes.

| File | Purpose |
|------|---------|
| `view_edit.cpp` | Photo editing view. Implements image adjustments including crop, rotate, straighten, color correction, and provides editing UI controls. |
| `view_edit.h` | Photo editing view. Implements crop, rotate, color adjustments, and other image editing operations with live preview. |
| `view_import.cpp` | File import workflow. Scans source folders, displays import preview, handles file copying/moving with rename templates and duplicate detection. |
| `view_import.h` | File import workflow. Manages importing files from cameras, devices, and folders with duplicate detection and organization. |
| `view_items.cpp` | Items grid/list view. Displays thumbnails in grid layout, handles selection, keyboard navigation, context menus, and item interactions. |
| `view_items.h` | Main thumbnail grid and list view. Displays file collections with thumbnail rendering, selection, and navigation. |
| `view_list.h` | List view display mode. Renders items in a compact list format with columns for metadata details. |
| `view_media.h` | Media viewing and playback. Displays photos and videos with playback controls, comparison view, and metadata display. |
| `view_rename.cpp` | Batch file rename view. Provides template-based renaming with preview, sequential numbering, and metadata-driven naming patterns. |
| `view_rename.h` | Batch rename workflow. Implements rename templates with metadata substitution, preview, and sequential numbering. |
| `view_sync.cpp` | Folder synchronization view. Compares source and destination folders, shows differences, and performs bidirectional sync operations. |
| `view_sync.h` | Folder synchronization view. Compares folders and manages file synchronization with conflict resolution. |
| `view_test.cpp` | Test runner UI view. Displays test results, runs unit tests, and provides test management interface for development. |
| `view_test.h` | Built-in test runner UI. Displays and runs unit tests from the embedded test suite with result reporting. |

### Standalone Files

| File | Purpose |
|------|---------|
| `pch.cpp` | Precompiled header source file. Includes pch.h to generate the precompiled header for faster compilation. |
| `pch.h` | Precompiled header file. Includes common system headers and core utility headers that are used throughout the application to improve build performance. |
| `secrets.h` | Encrypted API keys for map services. Contains obfuscated credentials for Google Maps and Azure Maps API access. |
| `tests.cpp` | Unit tests and validation. Contains test functions for verifying metadata parsing, crypto, search, and other core functionality. |

## Key Data Flows

| Flow | Pipeline |
|------|----------|
| **File Indexing** | `model_index` → `files_core` → `metadata_*` → `model_db` → `model_items` |
| **Media Playback** | `av_format` → `av_player` → `av_sound` (WASAPI) + `platform_win_d3d11` (GPU) |
| **Image Editing** | `view_edit` → `render_color` → `files_jpeg`/`files_png` → `metadata_xmp` |
| **Search** | `model_tokenizer` → `model_search` → `view_items` |

## Async Execution Model

Diffractor uses specialized task queues and worker threads for responsive UI during heavy background operations.

### Core Interface: `async_strategy`

Defined in `model.h`, provides the central interface for all async operations:

```cpp
class async_strategy {
    void queue_ui(std::function<void()> f);                      // Execute on UI thread
    void queue_async(async_queue q, std::function<void()> f);    // Execute on background thread
    void queue_location(std::function<void(location_cache&)> f); // Access location cache
    void queue_database(std::function<void(database&)> f);       // Access SQLite database
    void queue_media_preview(std::function<void(media_preview_state&)> f); // Video previews
};
```

### Queue Types (`async_queue` enum)

| Queue | Purpose |
|-------|---------|
| `scan_folder` | File system directory scanning |
| `scan_modified_items` | Re-scanning changed files |
| `scan_displayed_items` | Loading metadata for visible items |
| `load` | General image/media loading |
| `load_raw` | RAW photo decoding (single-threaded, memory-intensive) |
| `crc` | File hash computation for duplicate detection |
| `index` | Search index building and updates |
| `index_predictions` | Auto-complete suggestions |
| `index_summary` | Collection statistics |
| `index_presence` | Duplicate presence checking |
| `query` | Search query execution |
| `auto_complete` | Search box suggestions |
| `web` | HTTP requests |
| `cloud` | Cloud service operations |
| `map_tile` | Map tile downloading |
| `work` | General background work |
| `sidebar` | Sidebar UI updates |
| `render` | Image rendering operations |

### Usage Pattern

The typical pattern for async operations:

```cpp
// From any thread, queue background work
queue_async(async_queue::work, [this] {
    // Expensive operation runs on background thread
    auto result = process_files();
    
    // Marshal result back to UI thread for safe UI updates
    queue_ui([this, result] {
        update_display(result);
        invalidate_view(view_invalid::view_redraw);
    });
});
```

### Specialized Queue Methods

| Method | Purpose |
|--------|---------|
| `queue_location(f)` | Exclusive access to `location_cache` for reverse geocoding (KD-tree spatial queries) |
| `queue_database(f)` | SQLite access for thumbnails, metadata, web cache; batched writes |
| `queue_media_preview(f)` | Video seek preview; maintains decoder state between calls |

### Implementation (`app_frame` in `app.cpp`)

Worker threads start at launch, each monitoring a `platform::task_queue`. Task queues use `platform::thread_event` for wake-up signaling. UI queue processed in main message loop. All queue operations are mutex-protected.

### View Invalidation

Background operations signal UI updates through `invalidate_view(view_invalid flags)`:

```cpp
enum class view_invalid {
    view_redraw,        // Repaint the view
    view_layout,        // Recalculate layout
    group_layout,       // Rebuild item groups
    media_elements,     // Update media controls
    command_state,      // Update toolbar state
    sidebar,            // Refresh sidebar
    // ... and more
};
```

Multiple flags can be combined. The UI thread processes these flags in `complete_pending_events()`, coalescing redundant updates.

## Strategy Interfaces for Decoupling

Abstract strategy interfaces decouple the model/data layer from the UI layer for modularity, testability, and separation of concerns.

### `state_strategy` - UI Coordination Interface

Defined in `model.h`, abstracts UI-level coordination callbacks:

```cpp
struct state_strategy {
    // Window management
    virtual void toggle_full_screen() = 0;

    // Navigation and search
    virtual bool can_open_search(const df::search_t& path) = 0;
    virtual void search_complete(const df::search_t& path, bool path_changed) = 0;

    // Selection and focus
    virtual void item_focus_changed(const df::item_element_ptr& focus, 
                                    const df::item_element_ptr& previous) = 0;
    virtual void make_visible(const df::item_element_ptr& i) = 0;

    // View state changes
    virtual void display_changed() = 0;
    virtual void view_changed(view_type m) = 0;
    virtual void play_state_changed(bool play) = 0;

    // Command handling
    virtual void invoke(commands id) = 0;
    virtual bool is_command_checked(commands cmd) = 0;

    // UI interactions
    virtual void track_menu(const ui::frame_ptr& parent, recti bounds,
                           const std::vector<ui::command_ptr>& commands) = 0;
    virtual void element_broadcast(const view_element_event& event) = 0;

    // Resource management
    virtual void invalidate_view(view_invalid invalid) = 0;
    virtual void free_graphics_resources(bool items_only, bool offscreen_only) = 0;
};
```

**Callback Categories:** View Lifecycle (`display_changed`, `view_changed`, `search_complete`), User Interaction (`invoke`, `track_menu`, `toggle_full_screen`), Item Management (`item_focus_changed`, `make_visible`), Rendering (`invalidate_view`, `free_graphics_resources`).

### `async_strategy` - Threading Coordination Interface

```cpp
class async_strategy {
    void queue_ui(std::function<void()> f);                      // Execute on UI thread
    void queue_async(async_queue q, std::function<void()> f);    // Background thread pool
    void queue_location(std::function<void(location_cache&)>);   // Location cache access
    void queue_database(std::function<void(database&)> f);       // Database access
    void queue_media_preview(std::function<void(media_preview_state&)>); // Video preview
};
```

### Implementation Pattern

The `app_frame` class implements both `state_strategy` and `async_strategy`, bridging the model layer to the actual UI components:

```cpp
class app_frame : public state_strategy, public async_strategy {
    // state_strategy implementation
    void display_changed() override {
        _view->display_changed();
        invalidate_view(view_invalid::view_layout);
    }
    
    void invoke(commands id) override {
        // Route command to appropriate handler
        handle_command(id);
    }
    
    // async_strategy implementation
    void queue_ui(std::function<void()> f) override {
        _ui_queue.enqueue(std::move(f));
    }
    
    void queue_async(async_queue q, std::function<void()> f) override {
        _task_queues[q].enqueue(std::move(f));
    }
};
```

### Testing Support

`tests.cpp` provides `null_state_strategy` and `null_async_strategy` for testing `view_state` in isolation. Null strategies execute callbacks immediately (synchronous) or ignore them.

**Related Interfaces:** `view_host` (per-view interactions, `ui_view.h`), `df::async_i` (low-level async, `util_interfaces.h`), `av_host` (A/V callbacks, `av_player.h`).

## File Indexing Pipeline

The `index_state` class (`model_index.cpp`) maintains an in-memory index with folder scanning, metadata extraction, thumbnail generation, duplicate detection, and bloom filter search indexing.

### Thread Safety Model

```cpp
class index_state {
    index_items _items;              // Thread-safe folder/file index with internal R/W lock
    platform::mutex _summary_rw;      // Protects _summary aggregate data
    item_writes_t _db_writes;         // Lock-free queue for database writes
};
```

- **`index_items`**: `dense_hash_map` of folder paths → `index_folder_item_ptr`, R/W lock for concurrent reads
- **`_summary_rw`**: Guards tag counts, file type histograms, distinct word lists
- **`_db_writes`**: Lock-free queue; any thread enqueues writes for database thread

### Pipeline Stages

| Stage | Method | Thread | Actions |
|-------|--------|--------|---------|
| 1. Configure | `index_roots()` | UI | Stores folders, exclusion patterns |
| 2. Discover | `index_folders()` | `index_task_queue` | Recursive scan, `validate_folder()`, sets `_folders_indexed` |
| 3. Extract | `scan_uncached()` | `index_task_queue` | Metadata scan via `scan_item()`, queues DB writes, sets `_fully_loaded` |

### Core Scanning Methods

**`validate_folder(folder_path, refresh, timestamp)`** - Folder synchronization: queries filesystem via `platform::iterate_file_items()`, performs three-way merge (new files → create, deleted → remove, existing → preserve if timestamps match), associates sidecars, updates bloom filter.

**`scan_item(folder, path, load_thumb, scan_if_offline, item, ft)`** - Per-file scanner: checks `needs_scan_impl()`, calls `files::scan_file()`, updates index, queues DB write, triggers async location lookup.

**`scan_items(roots, recursive, scan_if_offline, token)`** - Batch folder scan: iterates roots, validates folders, scans files recursively.

**`scan_items(items, load_thumbs, refresh, only_if_needed, scan_if_offline, token)`** - Batch item scan: groups by folder, validates once per folder, optionally loads thumbnails.

### Async Queue Integration

| Method | Queue | Purpose |
|--------|-------|---------|
| `queue_scan_folder(path)` | `scan_folder` | Single folder scan |
| `queue_scan_folders(paths)` | `scan_folder` | Multiple folder scan |
| `queue_scan_listed_items(items)` | `scan_folder` | Scan listed items |
| `queue_scan_modified_items(items)` | `scan_modified_items` | Re-scan changed files |
| `queue_scan_displayed_items(items)` | `scan_displayed_items` | Load visible thumbnails |
| `queue_update_presence(items)` | `index_presence_single` | Check collection presence |
| `queue_update_predictions()` | `index_predictions_single` | Update duplicate groups |
| `queue_update_summary()` | `index_summary_single` | Rebuild statistics |

The `*_single` queues use `reset_and_enqueue()` to cancel pending work.

### Duplicate Detection (`update_predictions`)

Algorithm: (1) Build hash index using filename (FNV-1a), metadata/file created date, CRC32C or size; (2) Sort by hash; (3) Group matches via `is_dup_match()` (CRC equality, same name+date, same name+size for A/V); (4) Assign group IDs; (5) Coalesce overlapping groups.

### Database Persistence

Batched writes via lock-free queue: any thread enqueues `item_db_write` records, database thread periodically dequeues all, groups by type, executes in transaction. Startup loads via `merge_folder()`.

### State Flags

| Flag | Set By | Meaning |
|------|--------|---------|
| `_cache_items_loaded` | `cache_load_complete()`, `merge_folder()` | DB cache loaded |
| `_folders_indexed` | `index_folders()` | Folder discovery complete |
| `_fully_loaded` | `scan_uncached()` | All metadata extracted |

`is_init_complete()` returns true when `_cache_items_loaded` and `_folders_indexed` are set.

## String Interning System (`str::cached`)

Global string interning optimizes memory and enables O(1) equality comparisons via pointer comparison. Thread-safe for concurrent reads/writes from indexing threads.

### Architecture

The string interning system has two components:

1. **`parallel_flat_hash_map<u8string_view, chached_string_storage_t*>`** - Hash map with CRC32C hashing, 4 shards with independent locks for concurrency
2. **`platform::memory_pool`** - Fixed-size block allocator; no deallocation (strings persist for app lifetime)

#### `chached_string_storage_t` - Storage Format

Uses C "flexible array member" pattern (allocation size = `sizeof(struct) + len + 1`):

```cpp
struct chached_string_storage_t {
    uint32_t len;      // String length in bytes
    char8_t sz[1];     // Variable-length UTF-8 data (null-terminated)
};
```

#### `str::cached` - Handle Type

8-byte pointer wrapper, trivially copyable, thread-safe reads:

```cpp
struct cached {
    const chached_string_storage_t* storage = nullptr;
    operator std::u8string_view() const;                          // Implicit conversion
    bool operator==(const cached other) const { return storage == other.storage; }  // O(1)
};
```

#### `string_index_t` - Global Intern Pool

Uses `lazy_emplace_l()` for atomic lookup-or-insert:

```cpp
str::cached find_or_insert(const std::u8string_view sv) {
    if (sv.empty() || sv.size() > block_size) return {};
    chached_string_storage_t* result = nullptr;
    _storage.lazy_emplace_l(sv,
        [&result](auto& kv) { result = kv.second; },           // Found existing
        [this, sv, &result](auto& ctor) {                       // Create new
            result = make_entry(sv);
            ctor(std::u8string_view(result->sz, result->len), result);
        });
    return {result};
}
```

### Usage

```cpp
str::cached name = str::cache(u8"example.jpg"sv);           // Cache string
str::cached trimmed = str::trim_and_cache(u8"  text  "sv);  // Trim and cache
str::cached literal = u8"constant"_c;                        // Literal suffix
process(item.name);                                          // Implicit string_view conversion
```

### Benefits

| Use Case | Without Interning | With Interning |
|----------|-------------------|----------------|
| 10K files, 20-char names | ~200 KB + heap overhead | ~200 KB shared + 80 KB pointers |
| Tag "vacation" on 5K files | 40 KB | 8 bytes + 40 KB pointers |
| String equality | O(n) character compare | O(1) pointer compare |

### Thread Safety

- **Writes**: Sharded locking allows concurrent inserts with different hashes
- **Reads**: Immutable once interned; safe concurrent access
- **Lifetime**: Never deallocated (bounded by collection size, better allocation density)

**Related Types:** `db_item_t::path`, `key_val`, `index_file_item::name`, `prop::item_metadata` fields.

## Third-Party Dependencies

Files in `third-party/` are external dependencies—do not edit. Key integrations:

| Library | Integration File | Purpose |
|---------|------------------|---------|
| FFmpeg | `av_format.cpp` | Video/audio decoding |
| SQLite | `model_db.cpp` | Database storage |
| libexif | `metadata_exif.cpp` | EXIF parsing |
| XMP SDK | `metadata_xmp.cpp` | XMP metadata |
| libjpeg-turbo | `files_jpeg.cpp` | JPEG processing |
| LibRaw | `files_raw.cpp` | RAW decoding |
| libheif | `files_heif.cpp` | HEIF/HEIC support |
| Hunspell | `util_spell.cpp` | Spell checking |

See [README.md](README.md#dependencies-3rd-party-libraries) for complete list with versions.

## Testing

Built-in test runner accessible from toolbar (checkmark icon) when running from Visual Studio. Tests defined in `tests.cpp`, displayed via `view_test.cpp`.

## Source File Comments

Each source file contains a `// Purpose:` comment after the copyright header. Keep these updated when modifying files.
