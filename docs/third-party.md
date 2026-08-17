# Third-Party Dependencies

Diffractor relies on third-party libraries vendor-copied into `third-party/`.

## Directory Conventions

- Vendor code lives under `third-party/`.
- Do not edit files inside `third-party/` directly unless necessary for project integration or stripping unused test/build artifacts. Dependency folders are completely replaced during upgrades.

## Dependencies

No package manager is used. Source code for each library is copied into `third-party/` (or `Include/` for header-only libs) and built by an owned CMake module under `cmake/vendored/`. To update a dependency: download/clone the new source, replace the files in the corresponding folder, adjust the module's source list if files were added or removed, and verify the build on both architectures.

| Library | Version | Folder | Type | Update Source |
|---------|---------|--------|------|---------------|
| [brotli](https://github.com/google/brotli) | 1.2.0 | `third-party/brotli` | Source copy | Download release from [google/brotli](https://github.com/google/brotli/releases), replace `c/` sources |
| [bzip2](https://github.com/libarchive/bzip2) | 1.0.8 | `third-party/bzip2` | Source copy | Download from [libarchive/bzip2](https://github.com/libarchive/bzip2/releases) |
| [dav1d](https://code.videolan.org/videolan/dav1d) | 1.5.4 | `third-party/dav1d` | Source copy | Clone from [videolan/dav1d](https://code.videolan.org/videolan/dav1d), copy `include/` + `src/`, then reapply the Diffractor-specific bits (see [dav1d](#dav1d) below) |
| [dng-sdk](https://github.com/niclaswue/dng_sdk) | 1.7.1 | `third-party/dng` | Source copy | Download from [Adobe DNG SDK](https://helpx.adobe.com/camera-raw/digital-negative.html), mirror at [niclaswue/dng_sdk](https://github.com/niclaswue/dng_sdk) |
| [expat](https://github.com/libexpat/libexpat) | 2.8.2 | `third-party/expat` | Source copy | Download release from [libexpat/libexpat](https://github.com/libexpat/libexpat/releases), replace `lib/` sources |
| [ffmpeg](https://github.com/diffractor/FFmpeg) | main | `third-party/FFmpeg` | **Fork** (submodule) | Rebase [diffractor/FFmpeg](https://github.com/diffractor/FFmpeg) on upstream [FFmpeg/FFmpeg](https://github.com/FFmpeg/FFmpeg). Fork adds custom `ffmpeg.vcxproj` for MSVC |
| [highway](https://github.com/google/highway) | 1.4.0 | `third-party/highway` | Source copy | Download release from [google/highway](https://github.com/google/highway/releases) |
| [hunspell](https://github.com/hunspell/hunspell) | 1.7.3 | `third-party/hunspell` | Source copy | Download release from [hunspell/hunspell](https://github.com/hunspell/hunspell/releases), replace `src/hunspell/` sources |
| [libarchive](https://github.com/libarchive/libarchive) | 3.8.8 | `third-party/libarchive` | Source copy | Download release from [libarchive/libarchive](https://github.com/libarchive/libarchive/releases) |
| [libde265](https://github.com/strukturag/libde265) | 1.1.1 | `third-party/libde265` | Source copy | Download release from [strukturag/libde265](https://github.com/strukturag/libde265/releases), update `de265-version.h` |
| [libebml](https://github.com/Matroska-Org/libebml) | 1.4.6 | `third-party/libebml` | Source copy | Download release from [Matroska-Org/libebml](https://github.com/Matroska-Org/libebml/releases), copy `ebml/` + `src/`; keep the hand-written `ebml_export.h` shim (see [libebml / libmatroska](#libebml--libmatroska) below) |
| [libexif](https://github.com/libexif/libexif) | 0.6.26 | `third-party/libexif` | Source copy | Download release from [libexif/libexif](https://github.com/libexif/libexif/releases) |
| [libheif](https://github.com/strukturag/libheif) | 1.23.1 | `third-party/libheif` | Source copy | Download release from [strukturag/libheif](https://github.com/strukturag/libheif/releases), update `heif_version.h` |
| [libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo) | 3.2.0 | `third-party/LibJpeg` | Source copy | Download release from [libjpeg-turbo/libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo/releases), place sources under `src/` (upstream layout), regenerate `src/jconfig.h` for MSVC |
| [libjxl](https://github.com/libjxl/libjxl) | 0.12.0 | `third-party/libjx` | Source copy | Download release from [libjxl/libjxl](https://github.com/libjxl/libjxl/releases), take the source lists from `lib/jxl_lists.cmake`, update `jxl/version.h` |
| [liblzma](https://github.com/tukaani-project/xz) | 5.8.3 | `third-party/liblzma` | Source copy | Download release from [tukaani-project/xz](https://github.com/tukaani-project/xz/releases), copy `src/liblzma/` sources |
| [libmatroska](https://github.com/Matroska-Org/libmatroska) | 1.7.2 | `third-party/libmatroska` | Source copy | Download release from [Matroska-Org/libmatroska](https://github.com/Matroska-Org/libmatroska/releases), copy `matroska/` + `src/`; keep the hand-written `matroska_export.h` shim (see [libebml / libmatroska](#libebml--libmatroska) below) |
| [libopenmpt](https://github.com/OpenMPT/openmpt) | 0.8.7 | `third-party/libopenmpt` | Source copy | Download the `.autotools` source release from [lib.openmpt.org](https://lib.openmpt.org/libopenmpt/download/), mirror at [OpenMPT/openmpt](https://github.com/OpenMPT/openmpt); copy `common/`, `libopenmpt/`, `sounddsp/`, `soundlib/`, `src/` (see [libopenmpt](#libopenmpt) below) |
| [libpng](https://github.com/pnggroup/libpng) | 1.6.58 | `third-party/libpng` | Source copy | Download release from [pnggroup/libpng](https://github.com/pnggroup/libpng/releases) |
| [LibRaw](https://github.com/LibRaw/LibRaw) | 0.22.2 | `third-party/LibRaw` | Source copy | Download release from [LibRaw/LibRaw](https://github.com/LibRaw/LibRaw/releases), copy `src/`, `libraw/`, `internal/` |
| [fluentui-system-icons](https://github.com/microsoft/fluentui-system-icons) | main | `src/Res/FluentSystemIcons-Resizable.ttf` | Asset (MIT) | Download `fonts/FluentSystemIcons-Resizable.ttf` **and** the matching `.json` from [microsoft/fluentui-system-icons](https://github.com/microsoft/fluentui-system-icons). The two go together: code points are assigned sequentially by the font build and move between releases, so after replacing the font re-run `python tools/fluent_icons.py --json <the .json> --write` to regenerate `src/app_icons.h`. Never hand-edit a code point. This replaced `segmdl2.ttf`, a Windows system font that was being embedded in the shipped binary and is not redistributable |
| [libwebp](https://github.com/webmproject/libwebp) | 1.6.0 | `third-party/webp` | Source copy | Download release from [webmproject/libwebp](https://github.com/webmproject/libwebp/releases) |
| [minizip-ng](https://github.com/zlib-ng/minizip-ng) | 4.2.2 | `third-party/minizip` | Source copy | Download release from [zlib-ng/minizip-ng](https://github.com/zlib-ng/minizip-ng/releases); the zlib-style compat API moved to `compat/` (zip.c/unzip.c/ioapi.c); regenerate `mz_config.h` for MSVC |
| [rapidjson](https://github.com/Tencent/rapidjson) | main | `third-party/rapidjson` | Header-only | Copy headers from [Tencent/rapidjson](https://github.com/Tencent/rapidjson) `include/rapidjson/` |
| [skcms](https://github.com/niclaswue/skcms) | main | `third-party/skcms` | Source copy | Copy from [skia.googlesource.com/skcms](https://skia.googlesource.com/skcms), mirror at [niclaswue/skcms](https://github.com/niclaswue/skcms) |
| [sqlite](https://github.com/niclaswue/sqlite) | 3.53.3 | `third-party/sqlite` | Source copy | Download amalgamation from [sqlite.org](https://www.sqlite.org/download.html), mirror at [niclaswue/sqlite](https://github.com/niclaswue/sqlite) |
| [utf-cpp](https://github.com/nemtrif/utfcpp) | 4.1.1 | `Include/utf8-cpp` | Header-only | Copy headers from [nemtrif/utfcpp](https://github.com/nemtrif/utfcpp/releases) into `Include/utf8-cpp/` |
| [xmp-sdk](https://github.com/diffractor/XMP-Toolkit-SDK) | 6.0.0 | `third-party/xmp` | **Fork** (submodule) | Rebase [diffractor/XMP-Toolkit-SDK](https://github.com/diffractor/XMP-Toolkit-SDK) on upstream [adobe/XMP-Toolkit-SDK](https://github.com/adobe/XMP-Toolkit-SDK). Fork adds: POPM/TPE2 reconciliation for MP3, Windows tag support, C++17 fixes, WebP support from Exempi |
| [zlib-ng](https://github.com/zlib-ng/zlib-ng) | 2.3.3 | `third-party/ZLib` | Source copy | Download release from [zlib-ng/zlib-ng](https://github.com/zlib-ng/zlib-ng/releases), configure for zlib-compat mode |


## Managing Third-Party Updates

When updating or re-vendor copying third-party libraries (e.g. `libjx`, `highway`, `FFmpeg`, `LibJpeg`), follow these guidelines:

1. **Remove Test & Build System Artifacts**
   - Third-party libraries often include test files, benchmarks, or build tool integrations (e.g., Bazel, CMake, GoogleTest).
   - Ensure test files containing static initializers or build system runner dependencies are removed if they are not needed for Diffractor.
   - Example: In `third-party/libjx/lib/jxl/`, `test_utils.cc` and `test_utils.h` instantiate Bazel `Runfiles::Create("")` statically on startup. These test files must be deleted when updating `libjx` to prevent `failed to find bazel workspace` stderr output.

2. **Build Description Integration**
   - Each library is described by `cmake/vendored/<name>.cmake`, with anything that description cannot carry on its own — a generated header, a flag one compiler needs, an architecture-specific source list — in `cmake/vendored/<name>.local.cmake` beside it.
   - Ensure newly added or removed source files in `third-party/` are reflected in the module's source list. The lists are explicit rather than globbed, so a file cannot join the build by being dropped in a folder.
   - These modules began as imports from `.vcxproj` files that no longer exist; see [retiring MSBuild](linux.md#retiring-msbuild) and [tools/build-divergence.txt](../tools/build-divergence.txt) before changing anything in them that looks arbitrary. Two libraries build unoptimised in Release on purpose.

3. **Verification**
   - Always run `.\dd.ps1 test` after a vendor library update.
   - **Build Win32 as well as x64.** An incremental x64 build can reuse stale objects and silently
     skip recompiling replaced third-party sources, so a green x64 build (and passing tests) can hide
     compile errors and clobbered integration patches. `.\dd.ps1 clean` or a fresh build directory
     forces a full compile. A 32-bit build is also the only thing that exercises the i386 assembly
     paths, which are a separate source list from the x86-64 ones.

     ```powershell
     python tools/dd.py build --config Release --arch x86
     ```

## Library-specific upgrade notes

These libraries are vendored with owned CMake modules and small Diffractor-specific
shims. Upstream ships CMake/meson/autotools that generate some of these files; because Diffractor
does not use those generators, a naive "delete folder and unzip" loses the shims. Copy the upstream
source folders **over** the existing tree (do not delete first) and re-verify the items below.

### dav1d

- **Preserve the bitdepth-template wrappers.** `src/tmpl8.c`, `src/tmpl16.c`, `src/tmpl8b.c`,
  `src/tmpl16b.c` are Diffractor-authored (not upstream). They `#define BITDEPTH` and `#include`
  the upstream `*_tmpl.c` files so the templated DSP is compiled once per bitdepth without meson's
  per-bitdepth build rules. Copying `src/` over the tree keeps them (upstream has no same-named
  files). If upstream adds/removes a `*_tmpl.c`, update the include lists in these four wrappers
  (cross-check `src/meson.build`).
- **Do not regenerate `config.h` / `config.asm`.** These are hand-customized to be multi-arch in a
  single file (`#ifdef _M_AMD64` in `config.h`, `%ifidn __OUTPUT_FORMAT__,win32/win64` in
  `config.asm`) rather than the single-arch files meson emits. Only add new `HAVE_*` macros that
  new upstream source references — set them to `0` on Windows unless a matching Win32 API exists.
  (1.5.4 added `HAVE_MEMALIGN`, `HAVE_ALIGNED_ALLOC`, `HAVE_SIGACTION`, `HAVE_GETAUXVAL`,
  `HAVE_ELF_AUX_INFO`, `HAVE_PTHREAD_NP_H`, `HAVE_PTHREAD_SETNAME_NP`, `HAVE_PTHREAD_SET_NAME_NP`,
  all `0` here.)
- **`vcs_version.h` vs `include/dav1d/version.h`.** As of 1.5.x the API-version macros
  (`DAV1D_API_VERSION_*`) live in the committed `include/dav1d/version.h`. Keep the hand-written
  `vcs_version.h` to the `DAV1D_VERSION` string only — leaving the old `DAV1D_API_VERSION_*` lines
  there too causes `C4005` redefinitions.
- **`C4703` under `/sdl`.** New 1.5.x C code trips MSVC's conservative "potentially uninitialized
  local pointer" analysis (false positives upstream builds never see because meson does not use
  `/sdl`). The vendored `dav1d.vcxproj` adds `<DisableSpecificWarnings>4703</DisableSpecificWarnings>`
  under each config's `<SDLCheck>true</SDLCheck>` so `/sdl` stays on for everything else. Re-check
  after upgrades in case the affected files change.
- New per-arch source in 1.5.4 was LoongArch/RISC-V only, which is not built on Windows x86 — no
  `.vcxproj` change was needed.

### libebml / libmatroska

- Upstream generates `ebml_export.h` (libebml) and `matroska_export.h` (libmatroska) from CMake to
  define the DLL import/export attribute macros. Diffractor keeps **hand-written shims** at the
  folder root (`third-party/libebml/ebml_export.h`, `third-party/libmatroska/matroska_export.h`)
  that just `#define EBML_DLL_API` / `#define MATROSKA_DLL_API` to empty (static linkage). They are
  included by `ebml/EbmlConfig.h` and `matroska/KaxConfig.h`. Copy the upstream `ebml/`+`src/` (or
  `matroska/`+`src/`) folders over the tree and **never delete or overwrite these root shims** —
  the upstream tarballs do not contain them.
- **libebml carries three MSVC/`/sdl` source patches that a folder replace silently clobbers.**
  They do not fail the x64 build if stale `.obj`s are reused, so **always force a clean rebuild of
  libebml (delete its `intermediate/*/*/libebml` folders) and build Win32 too** after upgrading —
  Win32 compiles the sources fresh and surfaces them. Re-apply after every libebml upgrade:
  1. `src/EbmlUnicodeString.cpp`: upstream `#include <utf8/checked.h>` → Diffractor
     `#include "utf8-cpp/utf8/checked.h"` (utfcpp is vendored under `Include/utf8-cpp/`, not `utf8/`).
     Alternatively add `$(SolutionDir)include\utf8-cpp` to the `.vcxproj` include dirs.
  2. `src/EbmlSInteger.cpp`: `EBML_PRETTYLONGINT(-0x80000000)` → `EBML_PRETTYLONGINT(-0x80000000LL)`.
     `EBML_PRETTYLONGINT` only appends `ll` under `__GNUC__`; on MSVC the `0x80000000` literal is
     `unsigned int`, so without the `LL` the unary minus is unsigned (C4146 **and** a real
     sign-boundary bug in the size calc).
  3. `src/platform/win32/WinIOCallback.cpp`: comment out the `if ((LONG)GetVersion() >= 0) { … }
     else { …CreateFileA… }` branch and always use `CreateFileW`. `GetVersion()` is deprecated
     (C4996 under `/sdl`) and the ANSI fallback only mattered for Win9x.


### libopenmpt

- Use the **`.autotools` source release** (`libopenmpt-x.y.z+release.autotools.tar.gz`). Copy
  `common/`, `libopenmpt/`, `sounddsp/`, `soundlib/`, `src/` over the tree.
- The autotools tarball omits the Windows plugin/test files that the vendored tree carries
  (`in_openmpt`, `xmp-openmpt`, `plugin-common`, `libopenmpt_test`, `Doxyfile`,
  `libopenmpt/libopenmpt_version.rc`). Only `libopenmpt/libopenmpt_version.rc` is referenced by the
  `.vcxproj` (a `ResourceCompile` item); copying over the tree preserves it. The others are unused
  and harmless.
- Keep the Diffractor static shim `third-party/libopenmpt/svn_version.h`.

### FFmpeg

FFmpeg is a **git submodule** (`third-party/FFmpeg`) tracking the Diffractor fork
[diffractor/FFmpeg](https://github.com/diffractor/FFmpeg). The fork carries a small set of
Diffractor-specific patches plus hand-maintained MSVC build files. Upgrading = rebasing those
patches onto a newer upstream tag and regenerating the config/project files. Diffractor is a
mostly read-only viewer, so the config enables **decoders/demuxers/parsers and disables
encoders/muxers**, keeps `dxva2` hardware video decode, and disables genuinely rare components.

**Rebase / squash workflow**

- The fork branch is a single squashed commit ("Diffractor specific") on top of the upstream tag.
  When rebasing, cherry-pick the functional patches onto the new upstream, then `commit --amend`
  the regenerated config/project files into that one commit. Keep it to **one commit** so the next
  rebase is a clean cherry-pick.
- **Beware functional hunks folded into the generated commit.** Some `.c`/`.h` shims (e.g. `riff.c`,
  `id3v2.c`, `swresample.c`) were historically squashed together with the big generated
  `config.*`/`*_list.c` commit. Regenerating from scratch silently drops them. Before finishing,
  diff the old fork tip against the merge-base for real source edits and re-apply:
  `git diff <merge-base> <old-fork-tip> -- '*.c' '*.h'`.

**Build environment (WSL)**

- Configure/make must run from an **LF checkout** — the Windows CRLF tree breaks POSIX `sh`. Make an
  LF clone once: `git -c core.autocrlf=false clone --no-local /mnt/c/code/diffractor/third-party/FFmpeg ~/ffsrc`
  and check out the rebase commit. Build into **native-fs** dirs (`~/ffb-x64`, `~/ffb-x86`) — far
  faster than `/mnt/c`.
- WSL packages: `mingw-w64` (gives `x86_64-w64-mingw32-gcc` and `i686-w64-mingw32-gcc`), `nasm`,
  `yasm`, `make`, `pkg-config`, and `libz-mingw-w64-dev` (so `--enable-zlib` is detected by the
  cross toolchain).
- **Run complex WSL commands from a script file** in `tmp/` (`wsl -d Ubuntu -- bash /mnt/c/.../tmp/x.sh`);
  passing complex bash inline through PowerShell mangles quoting.

**Configure command**

- We cross-compile with mingw so the target OS is Windows (not Linux). The x64 line (swap the
  arch/cross-prefix bits for the other arch):

  ```
  ./configure --target-os=mingw32 --arch=x86_64 --cross-prefix=x86_64-w64-mingw32- --enable-cross-compile \
    --enable-runtime-cpudetect --enable-static --disable-shared --enable-small \
    --enable-x86asm --disable-inline-asm --enable-w32threads --disable-pthreads \
    --enable-zlib --disable-programs --disable-doc --disable-avfilter --disable-network \
    --disable-encoders --disable-muxers --enable-muxer=avif --disable-devices --disable-filters \
    --disable-protocols --enable-protocol=file
  ```
  - x86: `--arch=x86 --cross-prefix=i686-w64-mingw32-`.
  - **No `--enable-gpl`.** Diffractor is LGPL 2.1-or-later, so FFmpeg is configured LGPL to match;
    the Linux build has never passed it either. It costs exactly eleven components, all gated on the
    fork's `lgpl_gpl` marker — eight game-console ADPCM decoders (N64, PSXC, Circus, IMA
    Escape/HVQM2/HVQM4/Magix/PDA) and the CRI AHX decoder, parser and `ahx_to_mp2` bsf. Note the
    fork's own comment on that gate: these files *are* marked LGPL, and are withheld by preference
    rather than by licence. No video codec, image decoder, demuxer or hwaccel is affected.
    If a regenerated config brings `CONFIG_GPL 1` back, the eleven return with it and must be
    zeroed in `config_components.h` and dropped from `{codec,parser,bsf}_list.c`.
  - **`--disable-inline-asm` is the key unblocker.** It matches MSVC (no GCC inline asm): it zeroes
    the inline-asm `HAVE_*` flags AND drops inline-asm-only `.c` (e.g. `hscale_fast_bilinear_simd.c`)
    from the source list so the `.vcxproj` stays correct. Without it you get `mathops.h` C2143 errors.
  - Diffractor is a broad-support reader: **do not** add `--disable-decoder=...`, `--disable-dxva2`
    or `--disable-hwaccels` — new decoders are welcome and dxva2 hardware video decode is used.
  - `--enable-muxer=avif` is the one intentional muxer (writing AVIF); it pulls in `mov_muxer`.
  - Note `--disable-postproc` was **removed in 8.0** — don't pass it.
  - `configure` alone does not emit `*_list.c` / `ffversion.h`; a `make` (even a dry-run target) does.

**Multi-arch config layout**

- Diffractor dispatches to per-arch files: `config.h`/`config.asm` `#include` the correct
  `config-{x64,x86,arm64}.{h,asm}` based on `_M_X64` / `_M_IX86` / `_M_ARM64`. **Never overwrite the
  dispatcher `config.h`/`config.asm`** — restore them (plus `ffmpeg.vcxproj*` and `.gitignore`) from
  `origin/diffractor`, then drop each arch's generated `config.h`→`config-x64.h` etc.
- Generated **shared** files (identical across arches, copy once from the x64 build):
  `config_components.{h,asm}`, `libavcodec/{codec,parser,bsf}_list.c`,
  `libavformat/{demuxer,muxer,protocol}_list.c`, `libavdevice/{indev,outdev}_list.c`,
  `libavutil/avconfig.h`, `libavutil/ffversion.h`.
- Some backends need HOSTCC-generated includes committed like normal sources
  (e.g. `libswscale/x86/uops_macros.gen.asm`). `.gitignore` un-ignores `*.gen.asm` for this.

**`HAVE_*` reconciliation for MSVC (the `mathops.h` C2143 trap)**

The mingw-gcc config has GNU compiler/system capability macros that are **wrong for the MSVC build**.
After copying each `config-<arch>.h`, post-process:
- **Rule A** — set every `HAVE_*_INLINE 1` → `0` (MSVC has no GCC inline-asm SIMD). Leave
  `HAVE_INTRINSICS_SSE2` and the `HAVE_*_EXTERNAL` nasm flags at `1`.
- **Rule B** — copy the compiler/system-determined values verbatim from the OLD
  `origin/diffractor` `config-<arch>.h` (they don't change with the FFmpeg version), notably
  `HAVE_LIBC_MSVCRT 1`, all `HAVE_INLINE_ASM*`, `HAVE_EBP/EBX_AVAILABLE`, `HAVE_MM_EMPTY`,
  `HAVE_SYMVER*`, and the libc probes (`HAVE_DIRENT_H`, `HAVE_GETTIMEOFDAY`, `HAVE_GMTIME_R`,
  `HAVE_LOCALTIME_R`, `HAVE_MKSTEMP`, `HAVE_USLEEP`, `HAVE_MPROTECT`).
- **Rule C** — force new GNU-isms off: `HAVE_INT128 0`, `HAVE_TEMPNAM 0` (no MSVC equivalent).
- Any brand-new `HAVE_*` a new source references: `0` unless a matching Win32 API exists.

**External libraries and removed components**

- The cross toolchain only auto-detects some of Diffractor's external libs. `--enable-zlib` works
  (with `libz-mingw-w64-dev`); `mediafoundation`/`schannel` are auto-detected on mingw. The rest are
  **enabled manually** in the generated `config.h` to match the old fork: set `CONFIG_BZLIB 1`,
  `CONFIG_LZMA 1`, `CONFIG_LIBOPENMPT 1`, `HAVE_ZLIB_GZIP 1`. `bzlib`/`lzma` add no components (just
  internal decompress paths); `zlib` enables ~22 decoders (png/apng/exr/flashsv/zmbv/…).
- `libopenmpt` needs its demuxer wired by hand: `CONFIG_LIBOPENMPT_DEMUXER 1` in
  `config_components.h` **and** insert `&ff_libopenmpt_demuxer` alphabetically into
  `libavformat/demuxer_list.c` (after `ff_lc3_demuxer`).
- **Don't re-add upstream-removed components.** 8.0 dropped the `sonic`, `ayuv`, `v308`, `v408`,
  `v410` decoders and the `mp3_header_decompress` bsf; they were `1` in the old config but are gone
  from `allcodecs.c`. Passing `--enable-decoder=...` for them is a silent no-op.

**Rebuilding the source / asm lists**

- Derive the `.vcxproj` source and nasm lists from the FFmpeg makefiles with **`make -Bn`**
  (always-make dry run). Plain `make -n` skips already-built objects and yields an **incomplete**
  list — this once misclassified `emms.asm` as x86-only and broke the x64 link (`LNK2001
  ff_emms_asm`).
- `ffmpeg.vcxproj` is a **union superset** that serves x64, x86 AND ARM64: it lists encoder and
  aarch64 sources that compile to nothing on other targets via `#if` guards. So only **add** files
  the make-list references but the vcxproj lacks, and **remove** only files that no longer exist on
  disk (missing files cause C1083). Paths use backslashes; make-list paths use forward slashes.
- Two nasm element forms exist in the vcxproj: self-closing `<NASM Include="…" />` and
  `<nasm Include="…">…children…</nasm>`. Match the `<nasm>…</nasm>` form when adding.
- Arch-specific nasm gets per-platform `ExcludedFromBuild` in the `.vcxproj` (x64-only asm excluded
  from Win32/ARM64, etc.). Only touch **newly-added** arch-specific files; leave pre-existing
  exclusions alone.

**Do not answer link errors with hand-written stubs**

`src/av_stubs.cpp` used to carry ~14,900 lines of empty `ff_*` definitions to satisfy the MSVC
linker. It was deleted: x64 Debug, Win32 Debug and Release x64 all link with **zero** of them.
The file was a fossil of an FFmpeg era that dispatched with runtime `if (ARCH_ARM)`, which MSVC
`/Od` does not fold away, so every foreign-arch init leaked a relocation. Current FFmpeg dispatches
with `#if ARCH_ARM`, so nothing is emitted and the stubs never resolved anything; most of the
symbols they defined (`ff_add_bytes_mmx`, `ff_vc1dsp_init_mmx`, …) no longer exist upstream at all.

Stubbing is also unsafe as a policy. An empty `ff_<kernel>_<simd>` silently wins over the real
implementation whenever the symbol *does* still exist and the object file is linked first — the
codec then runs a no-op kernel and produces corrupt output, with a clean build as evidence of
nothing. Prefer, in order:

1. Add the missing `.asm`/`.S` to the vcxproj — a genuinely missing kernel is a build-list gap
   (see `make -Bn` above), not a symbol to fake.
2. Exclude the foreign-architecture `libav*/<arch>/*.c` from that platform, which is what FFmpeg's
   own configure does. A wildcard `Update` item group expresses the whole rule at once, e.g.
   `<ClCompile Update="libavcodec\x86\*.c" Condition="'$(Platform)'=='ARM64'"><ExcludedFromBuild>true</ExcludedFromBuild></ClCompile>`.
3. Only if neither applies, and with a comment naming the upstream symbol and why it cannot be
   built.

The union-superset vcxproj relies on `#if ARCH_*` to make foreign-arch sources compile to nothing.
That holds today for x64/Win32. If a future ARM64 build (which needs `config-arm64.{h,asm}` first)
reports unresolved x86 kernels, the fix is rule 2, not a new stub file.

**Verification**

- Build **both x64 and Win32** Debug (a stale incremental x64 build hides dropped integration
  hunks), plus Release x64, then run `.\dd.ps1 test` (must be 504/504).
- Watch for upstream metadata-key changes surfacing in the app layer. FFmpeg 8.0's id3v2 reader
  emits COMM frames with a language as `comment-eng` (was `comment`); `files_core.cpp`
  `parse_metadata_ffmpeg_kv` handles this (plain > UI-language > any priority). Several demuxers
  can append a `-<iso639>` language suffix to metadata keys — see
  [issue #238](https://github.com/diffractor/diffractor/issues/238) for the full picture and the
  Matroska gap.
- The fork also renames demuxer metadata keys where upstream's choice is wrong for us. `mov.c`
  maps the `rtng` atom to `itunes_advisory` rather than upstream's `rating`, because it is the
  iTunes content-advisory flag and would otherwise show as a spurious star rating. `riff.c` and
  `id3v2.c` carry similar key patches. A rebase that drops these reintroduces the defect silently,
  because nothing fails to build — see [metadata](metadata.md).
- `h264_slice.c` applies the active SPS to `AVCodecContext` on the `skip_frame >= AVDISCARD_ALL`
  path in `ff_h264_queue_decode_slice`, which upstream reaches only *below* the skip, in
  `h264_field_start`. This is what codecs advertising `FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM` do;
  H.264 does not carry that capability because `has_decode_delay_been_guessed` (`demux.c`) makes it
  the one codec `avformat_find_stream_info` keeps entropy-decoding after the stream is already
  characterised. The index scan sets `skip_frame=all` per stream so a metadata probe never runs
  CABAC (see `av_format_decoder::open`), and this hunk is what keeps width, height, pixel format
  and frame rate. A rebase that drops it builds and runs, but every indexed H.264 file loses its
  pixel format — "Should scan av metadata with a bounded probe" is the test that catches it.

> **ARM64:** `config-arm64.{h,asm}` are **not** currently generated (no aarch64-mingw toolchain was
> used). The dispatcher still references them, so an ARM64 build would fail until they are produced;
> x64/x86 are unaffected.

## Where this lives

Vendored source is under `third-party/` and is **never edited**. Everything Diffractor owns about a
dependency lives in one of these:

| Concern | Location |
|---|---|
| Build configuration and integration patches | `cmake/vendored`, [DiffractorDependency.cmake](../cmake/DiffractorDependency.cmake) |
| Compiler policy applied to vendored targets | [DiffractorCompilerPolicy.cmake](../cmake/DiffractorCompilerPolicy.cmake) |
| The owned wrapper over each library | the `files_*` decoder, [av_format.cpp](../src/av_format.cpp), [metadata_xmp.cpp](../src/metadata_xmp.cpp), [util_spell.cpp](../src/util_spell.cpp), [util_zip.cpp](../src/util_zip.cpp) |
| FFmpeg configuration comparison | `tools/compare_ffmpeg_config.py` |
| The stubs used when a dependency is absent | `src/platform_linux_*_stubs.cpp` — alternatives to the real implementation, never built alongside it |

An upgrade is proven by the wrapper's tests, not by the library's: `/test:*metadata*`,
`/test:*video*`, `/test:*audio*` and the format tests in [test_files.cpp](../src/test_files.cpp) are
what catch a rebase that drops an integration patch. Several patches above exist precisely because
they are invisible until a specific test fails.
