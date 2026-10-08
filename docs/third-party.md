# Third-Party Dependencies

This document owns the dependency inventory, upstream sources, licensing, vendoring policy, and
integration constraints that an upgrade must preserve. The inventory records the imported releases
and snapshots; vendored version headers and submodule pins are authoritative, and CMake owns the
exact compiled source lists. Wrapper behavior belongs to the corresponding subject document.

## Licensing

Diffractor source is LGPL-2.1-or-later. Released binaries are LGPL-3.0-or-later because they
statically link libheif and libde265, which are LGPL-3.0-or-later. The repository root contains the
LGPL and incorporated GPL texts, and every release package - the installer, the portable zip and the
Store package - carries both beside the executable, as the LGPL requires of each copy.

FFmpeg is configured without GPL, version-3, or LGPL-v3-only components, so it remains usable under
LGPL 2.1. Enabling a GPL component changes the outbound license of the combined binary and is not an
ordinary build option.

A dependency or model with non-commercial terms, no redistribution grant, or incompatible
additional restrictions cannot ship. Every vendored component must retain the license and notices
required by its exact upstream version. Its folder's license file is the authority for those terms.

## Vendoring

Dependencies are source copies under `third-party/`, header-only copies under `Include/`, assets in
their consuming tree, or the FFmpeg/XMP git submodules. Owned CMake modules under `cmake/vendored/`
select sources, definitions, generated configuration, and compiler policy.

Treat vendored source as upstream material. Keep Diffractor changes in wrappers, CMake modules, or a
maintained fork whenever possible. A required generated header or source-level integration patch that
must remain inside the vendor tree is part of the upgrade checklist and must be reapplied explicitly.

## Dependency Inventory

Update this table with each import. A snapshot is an exact upstream commit, not a moving `main`
branch or the API version left in its headers. Source-copy locations are relative to the repository
root; FFmpeg and XMP are maintained forks, not interchangeable upstream release archives.

| Library | Imported release or upstream snapshot | Location | Type | Upstream / update source |
| --- | --- | --- | --- | --- |
| Brotli | 1.2.0 | `third-party/brotli` | Source copy | [google/brotli releases](https://github.com/google/brotli/releases); import `c/` |
| bzip2 | 1.0.8 | `third-party/bzip2` | Source copy | [Sourceware](https://sourceware.org/bzip2/downloads.html) |
| dav1d | 1.5.4 | `third-party/dav1d` | Source copy | [VideoLAN](https://code.videolan.org/videolan/dav1d); import `include/` and `src/` |
| Adobe DNG SDK | 1.7.1 build 2724 | `third-party/dng` | Source copy | [Adobe source archive](https://download.adobe.com/pub/adobe/dng/dng_sdk_1_7_1_2724_20260908.zip); import only `dng_sdk/source/` and the SDK license |
| Expat | 2.8.5 | `third-party/expat` | Source copy | [libexpat releases](https://github.com/libexpat/libexpat/releases); import `expat/lib/` |
| FFmpeg | `7366b2f552c929337a7cf14c3ed18b55ddfdbab3` plus Diffractor patches | `third-party/FFmpeg` | Fork / submodule | [Diffractor fork](https://github.com/diffractor/FFmpeg), based on [upstream FFmpeg](https://github.com/FFmpeg/FFmpeg) |
| Highway | 1.4.0 | `third-party/highway` | Source copy | [google/highway releases](https://github.com/google/highway/releases) |
| Hunspell | 1.7.4 | `third-party/hunspell` | Source copy | [Hunspell releases](https://github.com/hunspell/hunspell/releases); import `src/hunspell/` |
| libarchive | 3.8.9 | `third-party/libarchive` | Source copy | [libarchive releases](https://github.com/libarchive/libarchive/releases) |
| libde265 | 1.1.3 | `third-party/libde265` | Source copy | [strukturag/libde265 releases](https://github.com/strukturag/libde265/releases) |
| libebml | 1.4.7 | `third-party/libebml` | Source copy | [Matroska-Org/libebml tags](https://github.com/Matroska-Org/libebml/tags); import `ebml/` and `src/` |
| libexif | 0.6.26 | `third-party/libexif` | Source copy | [libexif releases](https://github.com/libexif/libexif/releases) |
| libheif | 1.23.5 | `third-party/libheif` | Source copy | [strukturag/libheif releases](https://github.com/strukturag/libheif/releases); import the library subtree |
| libjpeg-turbo | 3.2.0 | `third-party/LibJpeg` | Source copy | [libjpeg-turbo releases](https://github.com/libjpeg-turbo/libjpeg-turbo/releases) |
| libjxl | 0.12.0 | `third-party/libjx` | Source copy | [libjxl releases](https://github.com/libjxl/libjxl/releases) |
| XZ / liblzma | 5.8.4 | `third-party/liblzma` | Source copy | [XZ releases](https://github.com/tukaani-project/xz/releases); import `src/liblzma/` and `src/common/`, not the command-line tools |
| libmatroska | 1.7.2 | `third-party/libmatroska` | Source copy | [Matroska-Org/libmatroska tags](https://github.com/Matroska-Org/libmatroska/tags); import `matroska/` and `src/` |
| libopenmpt | 0.8.9 | `third-party/libopenmpt` | Source copy | [libopenmpt downloads](https://lib.openmpt.org/libopenmpt/download/); use the `.autotools` source release |
| libpng | 1.6.59 | `third-party/libpng` | Source copy | [libpng](https://www.libpng.org/pub/png/libpng.html), [upstream tags](https://github.com/pnggroup/libpng/tags) |
| LibRaw | 0.22.2 | `third-party/LibRaw` | Source copy | [LibRaw releases](https://github.com/LibRaw/LibRaw/releases); import `src/`, `libraw/` and `internal/` |
| libwebp | 1.6.0 | `third-party/webp` | Source copy | [libwebp tags](https://github.com/webmproject/libwebp/tags) |
| minizip-ng | 4.2.2 | `third-party/minizip` | Source copy | [minizip-ng releases](https://github.com/zlib-ng/minizip-ng/releases); include the `compat/` API |
| RapidJSON | `24b5e7a8b27f42fa16b96fc70aade9106cf7102f` | `third-party/rapidjson` | Header-only snapshot | [Tencent/rapidjson](https://github.com/Tencent/rapidjson); import `include/rapidjson/` and `license.txt` |
| skcms | `7e572ada698a60ff50bd9038fd6e01e44f40868a` | `third-party/skcms` | Source snapshot | [Skia upstream](https://skia.googlesource.com/skcms/); import the library sources and `LICENSE` |
| SQLite | 3.53.4 | `third-party/sqlite` | Amalgamation | [sqlite.org](https://www.sqlite.org/download.html) |
| utf8cpp | 4.2.1 | `Include/utf8-cpp` | Header-only | [utfcpp releases](https://github.com/nemtrif/utfcpp/releases); import `source/` and `LICENSE` |
| Adobe XMP Toolkit | v2025.03 plus later upstream and Diffractor fixes; fork `a6cb9ee25449ba58c1c2cef94e88836a973ea868` | `third-party/xmp` | Fork / submodule | [Diffractor fork](https://github.com/diffractor/XMP-Toolkit-SDK), based on [Adobe upstream](https://github.com/adobe/XMP-Toolkit-SDK) |
| zlib-ng | 2.3.3, zlib-compatible API | `third-party/ZLib` | Source copy | [zlib-ng releases](https://github.com/zlib-ng/zlib-ng/releases), not stock zlib |

Version traps matter during an audit: XMP's `6.0.0` is an API version, RapidJSON's `1.1.0` does not
identify a post-release snapshot, and zlib-ng's `1.3.1.zlib-ng` describes its compatibility API.
Use the SDK build number for DNG, the public `hunversion.h` for Hunspell, and `ZLIBNG_VERSION`
in the generated `zlib.h` for zlib-ng rather than stale package strings or input templates.

### Bundled Build Tools

| Tool | Version | Location | Upstream / retained notice |
| --- | --- | --- | --- |
| NASM | 3.02 | `tools/nasm.exe` | [NASM Windows archives](https://www.nasm.us/pub/nasm/releasebuilds/3.02/); `tools/nasm-LICENSE.txt` |
| 7-Zip Extra | 26.03 | `tools/7za.exe`, `tools/7za.dll`, `tools/7zxa.dll` | [7-Zip release](https://github.com/ip7z/7zip/releases/tag/26.03); `tools/7zip-LICENSE.txt` |

Keep the 7-Zip executable and both DLLs from the same architecture and release; the bundled set is
x86. These are build and packaging tools, not additional media decoders. Installed Visual Studio,
CMake, Ninja, Windows SDK and NSIS prerequisites are described in [README](../README.md#building).

## Upgrade Procedure

1. Identify the exact upstream release or commit and verify its license.
2. Compare the old vendored subset with its old upstream release before importing the new one.
   Copy the required sources over the existing tree, retaining the upstream license, generated
   configuration and integration shims. Remove only identified obsolete upstream files; never
   delete a vendor folder wholesale.
3. Reapply the integration constraints below, update the owning CMake module, and update the
   inventory. Record snapshot commit IDs and DNG build numbers, not just a branch or API version.
4. Configure from a clean build directory so removed sources cannot survive as stale objects.
5. Build Debug and Release x64 plus Release Win32 where the dependency has architecture-specific
   code, then run `.\dd.ps1 test` and its focused wrapper tests.

Do not import upstream tests, benchmarks, examples, or build systems unless the application build
requires them. CMake source lists are explicit; dropping a file into a vendor folder does not make it
part of Diffractor.

## XMP Toolkit

`third-party/xmp` is the Diffractor fork of Adobe XMP Toolkit SDK. Its integration differences are
observable metadata behavior and must survive a rebase:

- WebP metadata rewrites preserve physical chunk order and payload boundaries.
- MP3 POPM/TPE2, Windows keyword/rating fields, RIFF, and ASF reconciliation match the properties
  described in [metadata.md](metadata.md).
- MP4 avoids marking an unchanged Xtra box dirty and preserves bounded metadata updates.
- Container handlers preserve unknown metadata and do not delete a legacy field merely because an
  unrelated XMP property is absent.

The write path and rollback boundary are owned by [file-io.md](file-io.md). A successful toolkit
build is insufficient; metadata and format round-trip tests prove these patches remain effective.

## FFmpeg

`third-party/FFmpeg` is the Diffractor fork. The fork's configuration controls available demuxers,
decoders, parsers, hardware acceleration, and metadata keys. It builds no end-user programs and does
not enable network protocols for application media access. It is configured for decode speed rather
than size, which also keeps codec profile names; it decodes software AV1 through libdav1d, built
against the vendored dav1d; D3D11VA is its only hardware acceleration; and it has no muxers or
devices.

Rebasing must preserve metadata-key normalization and the bounded metadata-probe behavior consumed by
[av_format.cpp](../src/av_format.cpp). Architecture dispatch must use real compiled kernels or exclude
foreign-architecture sources; unresolved SIMD symbols must never be answered by no-op stubs.

Windows configuration remains architecture-specific. Linux uses the fork's configure/build system
rather than restating its codec inventory in application CMake.

### FFmpeg Configuration And Rebase Checklist

- Inventory the functional difference from the old upstream base before merging or rebasing.
  Preserve ASF/AVI/FLV/WAV embedded XMP extraction, repeated ASF categories, RIFF metadata keys,
  MP4 Windows Xtra tags and ratings, and the distinction between iTunes content advisory and star
  rating. Preserve H.264 stream characterization during discard-all probing and the audio
  reallocation guards. These changes can be folded into a commit that otherwise looks generated.
- Configure an LF-only source export in a separate build directory. For a Windows cross-configure,
  use MinGW with the matching x86 or x64 target, runtime CPU detection, external x86 assembly,
  no inline assembly, Windows threads, and zlib. Disable programs, documentation, network,
  encoders, muxers, devices and libavdevice, DXVA2, filters and general protocols; retain the
  `file` protocol. Enable libdav1d and do not pass `--enable-small`, which strips profile names
  and trades decode speed for size. Do not enable GPL, version-3 or nonfree components.
- Keep the `config.h` and `config.asm` dispatchers. Regenerate `config-x64.h`, `config-x86.h` and
  their assembly equivalents independently. MinGW compiler and libc probes are not MSVC probes:
  preserve the MSVC capability overrides, disable GNU inline assembly, and keep
  `HAVE_ALIGNED_STACK` disabled on Win32. MSVC does not supply `unistd.h`; do not accidentally
  select the application compatibility header instead of FFmpeg's native Windows I/O support.
- Reconcile the already-linked bzip2, liblzma and libopenmpt backends when the cross-configure
  cannot detect them. libopenmpt needs both its component switch and its entry in the generated
  demuxer list. A switch without a compiled implementation is not support.
- Generate the shared component headers and codec, parser, bitstream-filter, demuxer, muxer,
  protocol and device lists. Verify the shared files agree between architectures. Generate
  `libswscale/x86/uops_macros.gen.asm` with the upstream host compiler on x64; it is an include,
  not a standalone assembly unit. Stamp `libavutil/ffversion.h` with the upstream revision.
- Derive source changes from complete `make -Bn` dry runs for both architectures, not an
  incremental `make -n`. Update [ffmpeg_msvc.cmake](../cmake/vendored/ffmpeg_msvc.cmake), remove
  deleted source names, and retain assembly include paths, unique object names and Win32
  exclusions. The atomics compatibility layer needs both `compat/atomics` and
  `compat/atomics/win32` on the MSVC include path.
- Compare the Windows/Linux component sets with `tools/compare_ffmpeg_config.py`. Explain any
  intentional platform difference in `third-party/ffmpeg-config-divergence.txt`; do not simply
  accept a changed baseline. A working Windows build does not verify the Linux configuration.
- Prove metadata reconciliation and bounded probing through the application wrapper tests.
  Run the metadata, video, audio and format tests, then the complete suite. Do not replace a
  missing assembly implementation with an empty symbol or drop a decoder to make the link pass.

## Image And Archive Libraries

Image decoders, encoders, compression libraries, SQLite, and archive readers are built through their
owned CMake modules. Keep generated configuration multi-architecture where one source tree serves
x86 and x64. Preserve application wrappers as the sole boundary for errors, allocation limits,
pixel formats, and cancellation; vendor callbacks must not escape those contracts.

An upgrade that changes a file format's metadata, orientation, color, alpha, or malformed-input
behavior requires the corresponding format tests, not only a compile.

### Source Subsets And Generated Files

| Dependency | Integration that an import must preserve |
| --- | --- |
| libpng | Keep `pnglibconf.h`. Refresh the relevant SIMD subdirectories as well as the root sources; a new version string alone does not update those implementations. |
| Expat | Keep the root `expat_config.h`. Windows builds the `rand_s` entropy unit; the Linux module selects `getrandom` and its source. Do not import `xmlwf` or upstream build files as new application sources. |
| libheif / libde265 | Regenerate version headers from the matching upstream templates. Keep static linkage, the selected decoder backends and libde265's platform configuration; do not enable additional encoder/plugin dependencies implicitly. Keep libheif's FFmpeg decoder plugin: [files_heif.cpp](../src/files_heif.cpp) names it for HEVC images, which it decodes faster than libde265 and, unlike libde265's 32-bit build, bit-exactly. Update both copies of the libde265 version header. |
| libjpeg-turbo / libjxl | Keep architecture-correct generated configuration and version headers. Use the upstream source lists to identify added or removed units; preserve libjpeg-turbo's high-bit-depth API and JPEG XL's Highway/Brotli dependencies. |
| liblzma | Keep `config.h` and the Linux POSIX-thread override. Import the library/common subset, not the differently licensed command-line scripts and tools. |
| libarchive / minizip-ng | Keep the checked-in configuration. minizip-ng's zlib-style compatibility API lives in `compat/`; retain it and the platform-specific crypto selection. |
| Hunspell | Keep the static-library configuration and import the public version header. The source list includes `hunspelltrace.cxx`; dropping it leaves the trace hooks unresolved. |
| LibRaw / DNG SDK | Preserve the LibRaw wrapper boundary and DNG's `RawEnvironment.h`; document-history support remains disabled because the linked XMP subset has no `SXMPDocOps`. Import only the DNG source subset, not Adobe's bundled copies of libraries already owned here. |
| skcms | Keep the baseline, Haswell and Skylake transform units and their compiler-specific instruction-set flags. Import the public/internal headers under `src/` together with the root headers. Validate ICC transforms, not only compilation. |
| RapidJSON / utf8cpp | Import the complete header subset and its notices. RapidJSON's upstream `bin/jsonchecker/` has a different, restrictive license and is not part of the header-only import. |

### dav1d

Keep the four bitdepth-template wrappers that compile the upstream template sources for the
supported bitdepths. Reconcile their include lists with upstream's source inventory. The checked-in
`config.h` and `config.asm` serve both Windows architectures; replacing them with a single-arch
Meson output breaks the other build. Keep the public API version in `include/dav1d/version.h` and
the release string in `vcs_version.h`, without duplicate API macros. Linux assembly configuration
is selected by [dav1d.local.cmake](../cmake/vendored/dav1d.local.cmake).

### libebml And libmatroska

Preserve the root `ebml_export.h` and `matroska_export.h` static-link shims. libebml also carries
three source-level integration differences that must be retained or moved into owned build glue:
the prefixed utf8cpp include, the signed 32-bit lower-bound literal in `EbmlSInteger.cpp`, and
the Unicode constructor's unconditional `CreateFileW` path instead of the obsolete Windows-version
check. The Linux long-integer compatibility include belongs to
[ebml.local.cmake](../cmake/vendored/ebml.local.cmake). Build Win32 as well as x64 after an import.

### libopenmpt

Use the `.autotools` source release and import `common/`, `libopenmpt/`, `sounddsp/`, `soundlib/`
and `src/`. Retain the static `svn_version.h` shim. Do not import player plugins or test programs
into the library build. Its FFmpeg demuxer must remain present on both platforms.

## Assets

The Fluent UI icon font and generated code-point table are one versioned asset. Updating the font
requires the matching upstream JSON and regeneration through `tools/fluent_icons.py`; code points are
not edited by hand.

| Asset | Location | Upstream / update rule |
| --- | --- | --- |
| Fluent UI System Icons | `src/Res/FluentSystemIcons-Resizable.ttf` | [Microsoft upstream](https://github.com/microsoft/fluentui-system-icons); take the font and JSON from one revision and regenerate [app_icons.h](../src/app_icons.h). The font's internal `1.0` is not a release identifier. |
| Location data | `exe/location-countries.txt`, `exe/location-states.txt`, `exe/location-places.txt` | [GeoNames dumps](https://download.geonames.org/export/dump/) and the country-name source used by `tools/generate_locations.py`; regenerate as a set. |
| Spelling dictionaries | `exe/dictionaries` | Keep each dictionary's upstream provenance, encoding, paired affix file and redistribution notices when replacing it. |

Location data and dictionaries retain their own upstream notices and generation procedures. They are
packaged data, not source dependencies, but the same redistribution rule applies.

## Where this lives

- `third-party/` and `Include/`: vendored sources, headers, and upstream license files.
- `cmake/vendored/`: owned source selection, definitions, and generated configuration.
- [DiffractorDependency.cmake](../cmake/DiffractorDependency.cmake) and
  [DiffractorCompilerPolicy.cmake](../cmake/DiffractorCompilerPolicy.cmake): common dependency and
  compiler policy.
- [files_core.cpp](../src/files_core.cpp), the `files_*` decoders,
  [av_format.cpp](../src/av_format.cpp), [metadata_xmp.cpp](../src/metadata_xmp.cpp),
  [util_spell.cpp](../src/util_spell.cpp), and [util_zip.cpp](../src/util_zip.cpp): application-owned
  wrappers.
- `tools/compare_ffmpeg_config.py` and `tools/fluent_icons.py`:
  reproducible integration checks and generators.
- [ffmpeg_msvc.cmake](../cmake/vendored/ffmpeg_msvc.cmake) and
  [ffmpeg_msvc.local.cmake](../cmake/vendored/ffmpeg_msvc.local.cmake): Windows source inventory,
  assembly configuration and architecture exclusions.
- [test_files.cpp](../src/test_files.cpp), [test_metadata.cpp](../src/test_metadata.cpp),
  [test_av.cpp](../src/test_av.cpp), [test_text.cpp](../src/test_text.cpp) and
  [test_util.cpp](../src/test_util.cpp): application-level format, metadata, media and text checks.
- `dd.ps1` (`Copy-LicenceTexts`) and `installer/diff.nsi`: the licence texts each release package
  carries.
