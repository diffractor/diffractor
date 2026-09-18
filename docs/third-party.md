# Third-Party Dependencies

This document owns dependency licensing, vendoring policy, and integration constraints that an
upgrade must preserve. CMake and the vendored source own the exact version and source inventory;
wrapper behavior belongs to the corresponding subject document.

## Licensing

Diffractor source is LGPL-2.1-or-later. Released binaries are LGPL-3.0-or-later because they
statically link libheif and libde265, which are LGPL-3.0-or-later. The repository root contains the
LGPL and incorporated GPL texts.

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

## Upgrade Procedure

1. Identify the exact upstream release or commit and verify its license.
2. Replace only the vendored subset, retaining the upstream license and required generated files.
3. Reapply the integration constraints below and update the owning CMake module.
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
not enable network protocols for application media access.

Rebasing must preserve metadata-key normalization and the bounded metadata-probe behavior consumed by
[av_format.cpp](../src/av_format.cpp). Architecture dispatch must use real compiled kernels or exclude
foreign-architecture sources; unresolved SIMD symbols must never be answered by no-op stubs.

Windows configuration remains architecture-specific. Linux uses the fork's configure/build system
rather than restating its codec inventory in application CMake.

## Image And Archive Libraries

Image decoders, encoders, compression libraries, SQLite, and archive readers are built through their
owned CMake modules. Keep generated configuration multi-architecture where one source tree serves
x86 and x64. Preserve application wrappers as the sole boundary for errors, allocation limits,
pixel formats, and cancellation; vendor callbacks must not escape those contracts.

An upgrade that changes a file format's metadata, orientation, color, alpha, or malformed-input
behavior requires the corresponding format tests, not only a compile.

## Face Libraries

None are vendored in this release. libfacedetection, ncnn and the MobileFaceNet weights live on the
`face-search` branch with the feature that uses them.

The weights are the reason this is more than a scheduling decision. Their immediate source is GPLv3,
but the weights' own redistribution grant is unresolved: the graph identifies an InsightFace-derived
MobileFaceNet model, and neither source records a separate commercial redistribution grant. Shipping
the embedded pair therefore requires that grant to be established, or the files to be replaced by a
model with compatible terms, before the feature can be released. See [faces](faces.md) for what the
release does keep.

## Assets

The Fluent UI icon font and generated code-point table are one versioned asset. Updating the font
requires the matching upstream JSON and regeneration through `tools/fluent_icons.py`; code points are
not edited by hand.

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
