# Linux Port

This document owns the current Linux support boundary: what builds, which platform services exist,
and which product surfaces are unavailable. [Implementation](implementation.md) owns architecture;
[Rendering](rendering.md) owns backend parity; [Design](design.md) owns cross-platform user behavior.

## Current Support

The portable core and vendored dependencies build with CMake and GCC on Linux, and the embedded test
suite runs headlessly in Debug and Release. Indexing, search, metadata parsing, image and media decode,
collections, locations, and core file operations use the same model and tests as Windows.

Linux does not yet produce the interactive Diffractor application. There is no native window or
message loop, text stack, renderer present target, audio output, or complete desktop integration.
The Linux target is therefore a portability and headless-test target, not a distributable product.

## Platform Boundary

Portable application code depends on [platform.h](../src/platform.h). Operating-system headers,
handles, and direct system calls remain in `platform*` files. Repository lint enforces the parts of
this boundary visible from source text.

The Linux implementation currently provides:

| Area | Implementation |
| --- | --- |
| Process, synchronization, locale, known folders | `platform_linux.cpp` |
| Files, mapping, enumeration, paths | `platform_linux_files.cpp` |
| Copy, move, delete, and shell stand-ins | `platform_linux_desktop.cpp` |
| INI settings | `platform_linux_settings.cpp` |
| System theme colours, key codes, and UI-thread identity | `platform_linux_ui.cpp` |

Path identity follows the filesystem: case-insensitive on Windows and case-sensitive on Linux.
Case-folded comparisons remain appropriate for media types, tags, and other vocabulary, but not for
path-keyed containers.

## Unsupported Surfaces

The current Linux platform has no implementation for:

- native windows, input dispatch, dialogs, menus, clipboard, drag/drop, or monitor/DPI integration;
- font discovery, shaping, fallback, and glyph rasterization;
- a GPU backend or a native present target for the portable software rasterizer;
- audio output and hardware video presentation;
- Windows Shell features such as associations, properties, Explorer thumbnails, device eject, the
  share sheet, and taskbar progress;
- update, installer, Store, and desktop packaging workflows.

Portable code must express the intended operation rather than mirror a Win32 API shape. A Linux
stand-in may support headless tests, but it must not report success for a user-visible operation it
did not perform.

## Rendering Path

The platform-independent software rasterizer already produces bounded BGRA tiles. A Linux UI needs a
window-owned buffer and present target plus the text stack described above. Rendering parity remains
defined by [rendering.md](rendering.md); Linux does not get a reduced visual contract merely because
its first backend is software.

## Build

CMake is the only application build description. Linux builds vendored dependencies by default and
uses the FFmpeg and XMP forks as required dependencies. Runtime fixtures, languages, dictionaries,
and location data are staged beside the test binary.

Typical commands are:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/diffractor /test
```

CI definitions remain authoritative for compiler versions and installed system prerequisites.

### Retiring MSBuild

The application solution and project files were removed after CMake reached parity. Repository lint
rejects tracked `.sln` and application `.vcxproj` files so a second build description cannot drift
back in. Vendored forks may retain their own upstream project files; Diffractor's CMake modules decide
how they join the application build.

## Where this lives

- [platform.h](../src/platform.h): abstraction required from each platform.
- [platform_linux.cpp](../src/platform_linux.cpp),
  [platform_linux_files.cpp](../src/platform_linux_files.cpp),
  [platform_linux_settings.cpp](../src/platform_linux_settings.cpp),
  [platform_linux_desktop.cpp](../src/platform_linux_desktop.cpp), and
  [platform_linux_ui.cpp](../src/platform_linux_ui.cpp): current Linux implementation.
- [platform_compat.h](../src/platform_compat.h): cross-platform compatibility helpers.
- [render_software.h](../src/render_software.h) and
  [render_software.cpp](../src/render_software.cpp): portable CPU rasterizer.
- [CMakeLists.txt](../CMakeLists.txt), `cmake/`, [dd.ps1](../dd.ps1), and
  [tools/dd.py](../tools/dd.py): build description and drivers.