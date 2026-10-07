# Rendering Stack

This document owns render-backend parity, device and frame lifetime, resource budgets, text, image
and video presentation, and graphics recovery. [Design](design.md) owns visible behavior;
[Zoom](zoom.md) owns scale and center; [File I/O](file-io.md) owns decoded-image acquisition; and
[Implementation](implementation.md) owns thread publication.

## Backends

Every window draws through `ui::draw_context_device` using one of two backends:

- Direct3D 11 is the primary backend for the main window.
- The CPU software backend draws dialogs and popups and is the fallback when hardware setup fails or
  GPU use is disabled.

The window layer selects the backend and falls back to software after cleaning partial D3D state.
The software path is the fallback renderer, not D3D WARP.

## Backend Parity

Both backends consume the same scene and must agree on clipping, opacity, flat and gradient fills,
borders, shadows, rounded geometry, image transforms, text placement, and panorama output. A damage
rectangle limits required output but cannot affect pixels inside it. Layered windows preserve
transparency; ordinary targets begin from the same neutral clear color.

The GPU may dither solid output slightly to avoid banding. This is the only intentional visible
backend difference. Alpha animation is disabled through `ui::animations_enabled` in software mode;
callers use `ui::animate_alpha` rather than bypassing that gate.

## Device And Frame Lifetime

Shared D3D and DXGI factories live for the process. Each hardware window owns its swap chain and
size-dependent targets. Device creation enables BGRA support and multithread protection because the
video decoder and UI renderer can touch D3D resources from different owning contexts.

A frame records draw commands, submits the retained scene, and presents. Resize keeps the old target
until a new one is ready, then replaces size-dependent resources together. Occluded windows avoid
unnecessary presentation. Device loss releases GPU objects and requests recreation without dropping
logical view, selection, zoom, or playback state.

Resources are keyed by every input that changes their pixels or geometry. DPI, font, theme, device,
and graphics resets invalidate the dependent caches. View-owned graphics objects are released when
their device generation changes, whether or not the view is currently visible.

## Image Budgets

Decoded surfaces and textures are bounded separately from encoded thumbnails and source bytes.
Visible content has priority; off-screen GPU textures and reproducible decoded surfaces are released
before irreproducible or currently displayed data. A budget eviction must leave a cheaper path back,
such as an encoded image or SQLite thumbnail, and must preserve layout dimensions.

Texture upload occurs lazily on the UI thread because the draw context owns device resources. Decode
and scaling remain on workers. Publication checks item identity and generation before installing a
surface or texture.

FFmpeg still-image fallback constrains stream creation during probing so every probe decoder it opens
inherits the application pixel ceiling. General AV container probing still supports formats that
create streams while reading packets; those streams are checked again before explicit decoder setup,
but their probe decoder may use FFmpeg's default ceiling until the demuxer exposes the stream without
a vendored FFmpeg patch.

## The Software Tile

The CPU backend rasterizes the damaged region in bounded BGRA tiles and hands completed tiles to a
platform present target. The rasterizer is platform-independent; only the final presentation target
names a native window. Tiles clip every primitive and use the same premultiplied-alpha convention as
the GPU path.

Text arrives as positioned glyph coverage, so the software rasterizer does not own font discovery or
shaping. A memory present target supports headless rendering without a window.

## The Sidebar Globe

The globe is rasterized into a software surface and uploaded like any other image. The globe model,
projection, selection, and hit testing are shared across backends, so software and hardware output
use identical geometry and colors. Tile and globe cache identities include projection, dimensions,
theme, and source revision.

## Text And Glyphs

The platform font layer resolves faces, fallback, shaping, metrics, and glyph coverage. Draw
backends consume positioned glyphs. Hardware rendering stores coverage in glyph atlases keyed by
font and device generation; software rendering blends the same coverage masks directly.

Text measurement and drawing use the same shaping result. A font, DPI, locale, or device change
invalidates both measurement and glyph resources so layout cannot describe different text from the
pixels drawn.

## Images And Pixel Formats

Surfaces carry explicit color and alpha semantics. Upload maps supported RGB, YUV, and grayscale
formats to backend textures without silently changing range, matrix, orientation, or premultiplication.
Unsupported combinations convert through the shared rendering utilities before upload.

Color transforms and image edits are applied in the shared render layer. Backends perform sampling,
composition, and presentation; they do not reinterpret metadata.

A destination is shaped by what the item is, not by what has arrived to draw into it: a stand-in
staged before the decode need not match the item's shape, and stretching it would distort the picture
until the decode landed. Video is the exception, because a decoded frame can carry non-square pixels.
There the stored frame is not the shape to fill, so the destination takes the container's declared
display dimensions and the frame is stretched into them.

## Hardware Video

Video decoding runs off the UI thread. Hardware decode uses a dedicated D3D11 video device and
shares completed textures with the render device through keyed synchronization. The decoder publishes
an immutable frame description; the UI opens or copies it into render-owned resources and verifies
session generation before display.

When hardware decoding, shared textures, or YUV presentation are unavailable, playback falls back to
software-decoded surfaces. The fallback preserves timing, orientation, color intent, and playback
state. Closing or superseding a session releases decoder resources on their owning context before
the render-side reference is discarded.

## Resilience

Risky graphics capabilities are guarded by durable crash markers. A marker raised at the next launch
disables only the implicated capability, such as GPU rendering, hardware video decode, or direct YUV
presentation. Successful initialization clears its marker.

Runtime device removal tears down dependent resources, falls back to software where possible, and
invalidates affected views. Repeated failure must not loop device creation or leave a partially
initialized backend selected. Logical application state remains independent from device state.

## Where this lives

- [platform_win_d3d11.cpp](../src/platform_win_d3d11.cpp): hardware drawing, swap-chain resources,
  texture upload, glyph atlases, and shared video textures.
- [platform_win_software.cpp](../src/platform_win_software.cpp): CPU backend and Windows present
  target.
- [platform_win_font.cpp](../src/platform_win_font.cpp): font resolution, shaping, and glyph masks.
- [render_surface.cpp](../src/render_surface.cpp) and [render_software.h](../src/render_software.h):
  shared surfaces and software rasterization.
- [av_format.cpp](../src/av_format.cpp): video decode and hardware-frame publication.
- [ui.h](../src/ui.h): backend-neutral draw interfaces and presentation results.