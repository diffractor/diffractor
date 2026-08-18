# Rendering stack

This document owns how Diffractor draws its UI, images, and video on Windows: the two
backends and their parity contract, device and swap-chain lifetime, the frame and
resize lifecycle, batching, textures, text, the hardware video pipeline, and recovery
from device loss. What the user sees and can predict belongs to
[design.md](design.md); overall architecture, threading, and ownership belong to
[implementation.md](implementation.md); the zoom behavior a backend must deliver
belongs to [zoom.md](zoom.md); the phased image ladder that feeds it belongs to
[file-io.md](file-io.md).

Rendering is held to the two [primary design drivers](design.md#primary-design-drivers).
In practice that means keeping the UI thread responsive, doing expensive work (decode,
scaling, indexing) off it, and using GPU resources deliberately.

## Backends

Diffractor has two interchangeable draw backends behind a common
`ui::draw_context_device` interface ([../src/ui.h](../src/ui.h)), which names no graphics API and
no window type - `render()` answers a `ui::present_result` carrying the backend's own code, and
only the window layer knows how to read it:

- **Direct3D 11 (GPU)** — the primary backend, in [../src/platform_win_d3d11.cpp](../src/platform_win_d3d11.cpp).
  Used for the main window when a hardware D3D11 device is available.
- **Software (CPU)** — in [../src/platform_win_software.cpp](../src/platform_win_software.cpp).
  Used for dialogs and bubble/popup windows, and as the fallback when Direct3D
  hardware is unavailable or the D3D draw context fails to initialise.

The window layer ([../src/platform_win_ui.cpp](../src/platform_win_ui.cpp))
selects a backend per window in `frame_base::create_draw_context`. If the D3D path
is requested but swap-chain or device-resource setup fails, it resets any partial
state and falls back to software rendering so the window still draws instead of
staying blank.

### Backend parity

Because either backend can end up driving the main window, they have to produce the
same picture. The treatments that are easy to miss, and that both backends must
reproduce, are:

- **Render-target clear.** Every frame starts by clearing to `scene_clear_shade`
  (see [../src/platform_win.h](../src/platform_win.h)) so anything the scene does
  not explicitly paint is a neutral grey rather than black. Layered windows are the
  exception: they stay transparent where nothing is drawn. The software backend
  skips this clear when the scene's own opening `clear` is opaque and covers the
  region being painted, because nothing could show through it.
- **The damage rect is a hint, not a contract.** `begin_draw` receives the window's
  update region, and a backend may repaint more than it asks for - but the pixels
  inside the damage rect must not depend on how much was repainted. The software
  backend honours it (base clip, bounded pre-clear, per-tile `BitBlt`); the Direct3D
  backend ignores it, because `DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL` rotates back
  buffers so the region it did not draw holds two-frames-ago content. Partial
  repaint needs no retained pixels: every pixel of the damaged region is written
  each frame, either by the scene's own opening opaque `clear` or by the neutral
  pre-clear, and only that region is presented. A layered window is the exception -
  `UpdateLayeredWindow` presents the whole surface, so it always takes full damage.
- **`clear` and `draw_rect`** are the same operation - a flat fill of the requested
  colour. Neither derives a second colour; a rect that wants a centre gradient must
  ask for one with `draw_rect_gradient`, which interpolates `c_centre` in the middle
  to `c_corner` at the four corners.
- **`draw_rounded_rect`** inflates the bounds, fills with `emphasize()` and fades
  the edge out; the visible edge is at `0.833 * (radius + 2)`.
- **`draw_border`** is a mitred gradient from `c_outside` at the outer rectangle to
  `c_inside` at the inner one, not a flat fill.
- **`draw_vertices`** (the audio visualizer) draws a drop shadow behind each bar
  that is taller than one pixel, then the bar as a centre gradient with the plain
  colour at the corners - the inverse of `draw_rect_gradient`.
- **`draw_shadow`** must honour its `width` argument, and `draw_edge_shadows` sizes
  its inset as `min(cx / 2, cy / 2, 96)`.
- **The shadow nine-slice split must match on both backends.** The shadow artwork is
  a centred radial blob, so each *quadrant* of the source is a corner tile, and each
  edge stretches the two-pixel band that straddles the centre line
  (`half - 1` to `half + 1`). The GPU path builds this in `build_shadow_vertices`
  and the CPU path in `stretch_shadow`; any change to one is a change to both.

The one deliberate difference is the small (<= 0.01) dither the solid and circle
pixel shaders add to hide banding; there is no CPU equivalent and it is well below
the visible threshold.

`tmp/capture.ps1` and `tmp/compare.ps1` are throwaway helpers for checking this: run
the app with and without `-no-gpu`, screenshot the window, and diff the two images.

## Device and swap chain

The shared D3D/DXGI objects live in a `factories` struct (`_f`) and are created
once in [../src/platform_win_d3d11.cpp](../src/platform_win_d3d11.cpp):

- `D3D11CreateDevice` with `D3D_DRIVER_TYPE_HARDWARE` and `D3D11_CREATE_DEVICE_BGRA_SUPPORT`
  (plus `D3D11_CREATE_DEVICE_DEBUG` in debug builds). It tries feature levels
  11_1 → 10_0, then retries without 11_1 on `E_INVALIDARG`.
- If the device cannot be created (or GPU is disabled), the app runs the **CPU
  software** backend rather than the WARP rasterizer; the D3D/DXGI objects stay
  null and `software_mode` is set.
- `setting.can_animate` records whether animation is affordable and permitted: it
  starts from `SPI_GETCLIENTAREAANIMATION` (falling back to "not a remote session")
  and is cleared whenever the CPU software backend takes over, including a runtime
  `downgrade_to_software`. It is published to `ui::animations_enabled`, which
  `ui::animate_alpha` consults; when the gate is off every alpha jumps straight to
  its target, so thumbnail fade-in, photo cross-fade, loading fade and the edit-view
  grid fade do not play. This also keeps CPU frames on the opaque blit fast paths,
  which mid-fade alpha would otherwise defeat.
- When the gate is on, a fade closes a fraction of its remaining distance each frame,
  and that fraction is recomputed once per `prepare_frame` from the real elapsed time
  (`ui::animation_step_factor`, decaying at `ui::alpha_fade_rate`). It is a rate rather
  than a fixed step because frames are paced to the display: a fixed step made a fade
  last a fixed number of frames, so the same fade ran at twice the speed on a 120Hz
  display as on a 60Hz one. The rate is chosen to reproduce the previous fixed step
  exactly at 60Hz, so the feel is unchanged on the refresh rate almost every display
  reports. The delta is clamped, so a stall or a breakpoint cannot snap a fade to its
  target.
- `IDXGIDevice1::SetMaximumFrameLatency(1)` keeps latency low.
- **Multithread protection** is enabled via `ID3D10Multithread::SetMultithreadProtected(TRUE)`.
  This matters because video decode touches D3D resources from a worker thread
  while the UI renders on the UI thread.

The per-window swap chain is created in `frame_base::create_draw_context`
([../src/platform_win_ui.cpp](../src/platform_win_ui.cpp)) using
`IDXGIFactory2::CreateSwapChainForHwnd` with:

- Format `DXGI_FORMAT_B8G8R8A8_UNORM`, `BufferCount = 2`,
- `DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL`, `DXGI_ALPHA_MODE_IGNORE`,
- `DXGI_SCALING_NONE`, so a pane whose swap chain has not yet presented at its new
  size pins its previous frame instead of rubber-banding it across the new
  rectangle — only the newly exposed edge is briefly undrawn. It also lets the
  buffer be larger than the client area, which is what Resize below relies on.

There is no legacy `IDXGIFactory::CreateSwapChain` fallback: it cannot produce a
flip-model chain and its blt-model output does not match the BGRA device. If
`IDXGIFactory2` or `CreateSwapChainForHwnd` fails, the window falls through to the
software backend instead.

## Frame lifecycle

Rendering is driven by `WM_PAINT`. The flow is:

1. `WM_PAINT` → `frame_base::handle_render` ([../src/platform_win_ui.cpp](../src/platform_win_ui.cpp)).
2. `ctx->begin_draw(...)` resets per-frame state.
3. `on_render(ctx)` — the view builds its scene by issuing draw calls into the
   draw context.
4. `ctx->render()` — the accumulated scene is flushed to the GPU. It returns an
   `HRESULT` so a device-loss result from `GetBuffer` / `CreateRenderTargetView`
   reaches the window layer instead of being swallowed; a failed render skips the
   present and routes to `handle_device_loss`.
5. `frame_base::present()` calls `IDXGISwapChain::Present(0, 0)`.

A successful `Present` is also the signal that GPU rendering actually works: the
first one clears the GPU crash guard (see Resilience below). A failed `Present`
routes to `handle_device_loss`.

### Redraw without rebuilding the scene

`frame_impl::redraw` calls `render()` + `present()` **without** `begin_draw` /
`on_render`. Video and audio playback use it to show a new frame: only the texture
contents changed, so the scene atoms and the vertex/index buffers built by the last
`WM_PAINT` are re-issued as-is. `build_index_and_vertex_buffers` therefore keeps the
existing buffers when nothing has been staged — clearing them would present a blank
frame on every playback tick.

### Resize

Back buffers are allocated on a `back_buffer_quantum` (256 px) grid
(`quantise_back_buffer_extent` in [../src/platform_win_ui.cpp](../src/platform_win_ui.cpp)),
not at the exact client size. `frame_base::handle_resize` therefore calls
`IDXGISwapChain::ResizeBuffers` only when the client outgrows the allocation, or when
it has halved in both dimensions. Growing immediately is required — the area beyond
the buffer is filled with the swap chain background colour — while the halving
threshold is hysteresis, so a drag sitting on a quantum boundary cannot reallocate on
alternate steps.

Nothing else changes with the buffer size. `DXGI_SCALING_NONE` pins the top-left
corner and crops to the window, so the client-sized region of an oversized buffer is
presented pixel-exact with no scaling call, and the draw context is driven entirely by
`_client_extent` (viewport, scissor, projection and clip) rather than by the
allocation.

`IDXGISwapChain2::SetSourceSize` is **not** used, despite being documented for exactly
this purpose. With `DXGI_SCALING_NONE` it scales the source region up to the full
buffer and then crops, which magnifies the frame; it only behaves as documented under
`DXGI_SCALING_STRETCH`, which would cost the pin-on-grow behaviour above. Since
`SCALING_NONE` already crops for free, there is nothing left for it to do.

Why this matters, measured on an RX 7800 XT with the app's swap chain description
(`tmp/resize_bench.cpp`, 400-step drag):

| Pane | Per-step size change | Reallocations | Whole drag |
|---|---|---|---|
| 1200x800, exact size | mean 682 us, p95 1516 us, max 3791 us | 400 | 348 ms |
| 1200x800, quantised | mean 1.9 us, p95 0.1 us | 1 | 56 ms |
| 3800x2100, exact size | mean 671 us, p95 1120 us, max 3046 us | 400 | 1028 ms |
| 3800x2100, quantised | mean 1.6 us, p95 0.1 us | 1 | 59 ms |

`ResizeBuffers` is only half of it: it also hands back fresh buffers, so the following
`GetBuffer` / `CreateRenderTargetView` / clear / `Present` cost 1898 us per step at
3800x2100 against 146 us when the buffers survive.

Before any `ResizeBuffers`, `handle_resize` calls
`draw_context_device::release_back_buffer_references()` (a no-op on the software
backend) to unbind the render target view left bound by the previous frame and to drop
the cached view itself. `ResizeBuffers` fails with `DXGI_ERROR_INVALID_CALL` while any
reference to a back buffer is outstanding, so both are required, not an optimisation.
`draw_scene` also unbinds the render target at the end of each frame for the same
reason, and rebuilds the view on the next frame that finds it missing.

The cost is video memory: the allocation is at most one quantum larger than the client
in each dimension, or up to twice it in each dimension while the shrink threshold has
not been reached. Only the view frame is hardware accelerated, so this is one swap
chain per window, bounded by the desktop size.

### The software tile

The software backend does not allocate a window-sized buffer at all. It rasterises
through a single fixed scratch tile of `software_tile_extent` (512) square - 1 MiB,
created once and never resized - which `render()` walks across the damaged region,
replaying the retained scene and blitting each step
([../src/platform_win_software.cpp](../src/platform_win_software.cpp)). This matters
because the main window is software rendered (only `view_frame` sets
`hardware_accelerated`), so at 4K the old full-client DIB was a permanently resident
33 MB for a window whose centre is covered by the view frame's child HWND.

Tiling is sound only because **every primitive derives its colour, coverage and source
mapping from its own bounds and treats the clip purely as a write mask** - so a tiled
frame is pixel-identical to an untiled one. Any new primitive must keep that property:
`clamp_to_clip` gives the iteration range, never the geometry. `probe_software_rasterizer`
([../src/render_software.cpp](../src/render_software.cpp)) guards it by drawing one
representative scene whole and again tile by tile and comparing, and it is the only
content-independent proof of no seams (a screenshot diff of a live window is dominated by
loading content).

That probe is also the **cross-machine** parity artifact, because it takes no window and no
font: it returns an even spread of pixels from the reference rendering, and
`Should rasterise one scene to one answer on every platform` compares them against a recorded
set. Two things about how that comparison is made are load-bearing:

- **Within a tolerance, not hashed.** A hash of this scene is identical between MSVC and GCC
  but *not* between MSVC Debug and Release: the rasterizer is float arithmetic an optimiser
  may reassociate. An exact artifact therefore fails for a reason this code does not own. A
  structural break - a transposed, mirrored or channel-swapped blit, a blend against the wrong
  background - moves a sample much further than a rounding step, which is what the tolerance is
  sized against and what mirroring one blit was used to confirm. The recorded samples hold
  unchanged across all four builds: MSVC and GCC, Debug and Release.
- **The scene contains no transcendental.** The rotated blit is a parallelogram written out
  rather than produced by `quadd::rotate`, because `sin` and `cos` are the C library's answer
  and would put its rounding inside a check about this rasterizer.

Two consequences are easy to trip over:

- **The canvas is bounded by the allocation, not the window.** `software_canvas`
  holds `_buffer_extent` plus the tile's window-space `_origin`, and `clamp_to_clip`
  intersects with those. It deliberately holds no copy of the client size: when it
  did, growing the window stopped widening the clamp - the tile had already reached
  its final size, so nothing reallocated and nothing refreshed the copy - and
  everything outside the original window silently vanished.
- **A clip-dependent fast path becomes a clip-dependent *result*.** `draw_texture_impl`
  chooses the swscale path only for a wholly visible destination; that test is taken
  against the recorded damage, not the live clip, or it would never fire under tiling
  and every image would drop to the scalar resample.

Layered windows cannot be tiled - `UpdateLayeredWindow` presents the whole surface -
so they keep a full-client buffer and run as a single tile.

**Where the buffer comes from and where a tile goes is not the rasterizer's business.**
Those two answers sit behind `ui::software_present_target`
([../src/render_software.h](../src/render_software.h)): acquire a BGRA buffer, begin a
present, present each tile, end it. `gdi_present_target` is the Windows answer and is the
only part of the CPU backend that names a `HWND`, a `HDC` or a DIB section - a DIB being
simply a BGRA buffer GDI will also blit from, which is why presenting costs no copy. The
seam exists because the rasterizer is the half worth carrying to another platform and the
present is the half that is never portable; `create_memory_present_target` presents
nothing at all, which is what a render with no window targets.

`software_canvas` itself lives in that same portable header and names nothing of any
platform, including of a font: `blend_glyph` takes an 8-bit coverage bitmap, because what
produced the coverage belongs to the text layer, which is still per-platform. What remains
Windows-shaped in the CPU backend is the present target and the DirectWrite text renderer.

### Native controls during resize

The controls panel, dialogs and toolbars host real comctl32 child windows
(`TRACKBAR_CLASS`, `TOOLBARCLASSNAME`, `BUTTON`) alongside the scene. Those classes
reach the screen in stages: they erase the client area with the parent background
and then draw the channel, thumb, separators, buttons or text over it. Every child
is moved and resized by a window resize or a splitter drag — and `apply_layout`
adds `SWP_NOCOPYBITS` when a size changed, so the whole client is invalidated —
which made each control visible mid-repaint and read as flicker across the panel.

`buffered_control_paint` (`platform_win_ui.cpp`) subclasses those controls: it
swallows `WM_ERASEBKGND`, and on `WM_PAINT` fills a cached memory bitmap with the
brush the parent returns from `WM_CTLCOLORSTATIC`, asks the control to draw into
that bitmap with `WM_PRINTCLIENT`, then blits the update region once. The
intermediate state never reaches the screen. This depends on the control rendering
into a device context it is handed; `platform::probe_buffered_control_paint` holds
comctl32 to that contract for each buffered class (test: "Should render common
controls into a buffer"). Edits are excluded — `edit_impl` already owns a paint
subclass and draws its whole frame in the non-client area.

### One paint pass per layout

`control_host_impl::apply_layout` positions the whole panel in a single
`DeferWindowPos` batch. `SetWindowPos` paints synchronously — it copies a moved
window's pixels to their new position and repaints a resized window's frame — so
without care each control reaches the screen as the batch walks past it, while its
siblings are still at their old geometry. That is most obvious on an edit, whose
entire appearance (border, padding, background, icon) is drawn in `WM_NCPAINT`: the
border arrived a composition frame ahead of everything around it, so it read as
flashing in the wrong place.

This applies to moves as much as resizes. A fixed-width control in a resizing panel —
the numeric edit beside each slider in the Edit view is right-aligned and never
changes width — only ever moves, so move must be deferred too or that case is missed.

A changed control therefore gets `SWP_NOREDRAW`, which suppresses the pixel copy, the
frame paint and the parent repaint alike. This includes a nested frame: moving its
left edge temporarily moves every child in screen coordinates before its `WM_SIZE`
layout counter-moves right-aligned children, so allowing redraw across that sequence
exposes the intermediate positions. `pending_move::invalidate_after_move` re-arms the
paint (`RDW_INVALIDATE | RDW_ERASE | RDW_FRAME`) once every control in the batch is in
place, and the host invalidates the union of the rects the controls vacated, since
`SWP_NOREDRAW` suppressed that too. `frame_base::handle_resize` then renders the host
surface and flushes all of those pending paints with `RDW_ALLCHILDREN | RDW_UPDATENOW`,
so the surface and every control land in the same composition frame. Nested frames do
not get this additional surface invalidation: they render themselves from `WM_SIZE`,
and re-invalidating them would cost a second full render per drag step. Their direct
child HWNDs are invalidated after the move, however, because a fixed-position child
such as a checkbox is skipped by the nested layout and otherwise retains stale pixels.

For the same reason `edit_impl::on_window_pos_changed` marks its frame invalid but
does not paint it: an out-of-band `RDW_UPDATENOW` there would reintroduce the split
between border and interior.

### Measurement must not paint

Layout measurement must not resize or otherwise mutate a live child HWND. The tags
panel exposed this through its wrapped `recommended_words_control` toolbars. The old
`toolbar_impl::measure_toolbar(cx)` temporarily called `SetWindowPos` with the proposed
width and a height of 500, then sent `TB_AUTOSIZE`. Each measurement therefore resized
a visible toolbar before the deferred layout batch began, invalidating the shared
parent repeatedly and exposing the multiline tag edit's non-client border between
updates. Metadata panels did not show the problem because they contain no wrapped
toolbars.

Wrapped toolbars are measured with `TB_GETIDEALSIZE` instead. It returns the height for
the proposed width without changing the toolbar window, so measurement remains a pure
input to `apply_layout` and all HWND geometry changes stay inside its deferred batch.

### APIs not used for resize flicker

`LockWindowUpdate` is not a redraw transaction. Drawing to the locked window is
discarded, only one window in the desktop session can be locked, and unlocking causes
the accumulated region to repaint. It can replace resize flicker with stale content
and a final flash, so it must not be used here.

`WM_SETREDRAW` is also too broad for this hierarchy: redraw state is per HWND, does not
automatically cover child windows, and native controls may have their own redraw-side
effects. `SWP_NOREDRAW` on the exact geometry operations, followed by bounded
invalidation after the batch, preserves the one-pass ordering without hidden redraw
state. `WS_EX_COMPOSITED` is likewise unsuitable for control hosts because it conflicts
with their software `GetDC`/`BitBlt` presentation path and adds composition latency.

### Ownership of GDI objects shared with native controls

Native controls do not own the GDI objects they are given, so every such object has
exactly one owner and a defined point at which every user of it stops referencing it.

- **Fonts.** `owner_context` owns the six `HFONT`s. A DPI change or a large-font
  toggle deletes and recreates all six, so it also records every window it has
  fonted and re-fonts each one from the new generation, and detaches them all on
  destruction. GDI recycles handle values, so a window still holding a deleted font
  can select an arbitrary live object into its DC. Font a window through
  `owner_context::set_window_font`, never through a raw `SetFont` with a context
  font, and always with the window's configured face rather than `dialog`. A popup
  host owns its own context and must be fonted from that one.
- **Class background brushes.** A window class is registered once and never
  unregistered, so `hbrBackground` outlives every context. Classes whose background
  colour is context- or theme-owned register with a null brush and paint in
  `WM_ERASEBKGND` instead.
- **Image lists.** A toolbar does not own its image list. It must be released both
  in `destroy()` and in the destructor, because control panels are rebuilt on view
  changes and simply drop their controls without calling `destroy()`.

### Reaching every window with a scale change

A DPI change and a large-font toggle both change the scale, and each window that
draws holds derived state that must be refreshed: its draw context's font size and
the metrics `frame_base::update_dpi_metrics` sets — `scale_factor`, `icon_cxy`, the
paddings, the resize-handle size and the scroll width. That state is otherwise set
once, at draw-context creation, so a window the change does not reach keeps drawing
at the old scale while the controls inside it are re-fonted from the shared owner
context, which shows as mixed scaling inside one panel.

Only a top-level window receives `WM_DPICHANGED`, so the refresh is delivered down
the tree rather than assumed:

- A **popup host** owns its own `owner_context` and handles `WM_DPICHANGED` itself.
- A **child host** shares its parent's context and is neither a control nor a frame,
  so the parent tracks it and hands it the change; it must not rebuild the shared
  fonts, which the parent has already done.
- A **bubble** shares the app's context and has no DPI message of its own, so it
  refreshes both its font size and its metrics when it is shown.

## Draw context and batching

The D3D draw context (`d3d11_draw_context_impl`) accumulates geometry into a list
of `scene_atom`s rather than issuing an immediate draw per primitive. Each atom
records a texture binding, a pixel shader, sampler/format, and a vertex/index range.
`draw_scene` then:

- binds the swap-chain back buffer as the render target (`OMSetRenderTargets`), sets
  the viewport and scissor rect,
- binds the shared vertex shader, input layout, blend state, rasterizer state, and
- walks the atoms, binding the right pixel shader + shader-resource view(s) per
  atom and issuing `DrawIndexed`,
- unbinds the render target and the sampled textures before returning, so nothing
  holds the back buffer between frames and no video surface is still bound as an
  input when the next decoded frame is copied into it.

There is no per-frame `ClearState`. This backend owns the device context outright -
nothing else on it touches pipeline state - and the per-atom binding cache starts each
frame with every slot null, so the first atom that needs a shader, sampler, constant
buffer or vertex buffer binds it regardless of what the previous frame left behind.

The render target view is built once per back buffer, not once per frame. Only
`ResizeBuffers` replaces the underlying surface, and `release_back_buffer_references`
runs before it.

Consecutive atoms are merged when every piece of per-atom state matches (texture,
shader, sampler, pixel format, colour space, transform and clip) and the merged
vertex count still fits the 16-bit index range.

### Texture bindings

Creating a shader-resource view is a driver object allocation, so it must not happen
per texture per frame. Rather than cache views in a side table keyed by texture, each
texture **owns** the views it is sampled through, as a `texture_binding`, and an atom
carries that binding instead of a pointer to look one up.

This is what makes the lifetime rules disappear rather than needing to be enforced:

- A view holds a reference to its own resource, so an atom that carries a binding keeps
  its texture alive from the moment it is staged until the scene is rendered. There is
  no separate keep-alive list.
- There is no key, so there is no question of a raw `ID3D11Texture2D*` being recycled by
  a different texture once the original is released.
- There is no cache to prune, so a texture is released as soon as the scene that drew it
  is, rather than lingering for an extra frame.

`d3d11_texture::binding()` and `d3d11_text_renderer::atlas_binding()` build on demand and
rebuild when the texture behind them is replaced. Both detect replacement by comparing the
current texture against the one the binding was built from — safe for the same reason as
above, since the binding holds that texture and its address therefore cannot be reused.
No call site has to remember to invalidate anything. A failed build leaves the binding
empty and is retried on the next use.

Pixel shaders cover the distinct material types:

- `_pixel_shader_solid` — solid fills,
- `_pixel_shader_rgb` / `_pixel_shader_rgb_bicubic` — RGB images (point and bicubic),
- `_pixel_shader_yuv` / `_pixel_shader_yuv_bicubic` — NV12/P010 video sampled
  directly as YUV, converted in-shader using a color-space constant buffer
  (`_yuv_cbuffer`),
- `_pixel_shader_font` — text glyph atlas,
- `_pixel_shader_circle` — anti-aliased circles.

Samplers are point and bilinear; a `_texture_transform_cbuffer` carries per-atom
texture transforms.

## Textures and pixel formats

`ui::texture` is implemented by `d3d11_texture` (GPU) and `software_texture` (CPU).
Textures support several formats (`ui::texture_format`): `RGB`/`ARGB`, `NV12`,
`P010`, and the font atlas. Format capability is probed once at device creation via
`CheckFormatSupport` (`_supports_nv12`, `_supports_p010`).

Video frames are the interesting case: they arrive as YUV (NV12 or 10-bit P010)
and are sampled directly by the YUV pixel shaders, avoiding a CPU-side RGB
conversion. When a texture upload path is unavailable, frames fall back to CPU
scaling (`av_scaler` / `sws_scale`) into an RGB surface.

### Image budgets

`publish_image_budgets` runs once when the backend creates its device and publishes three
numbers that the decode path reads on the UI thread:

| Value | Derived from | Purpose |
|---|---|---|
| `df::max_texture_dimension` | The achieved feature level: 16384 at 11_0 and above, 8192 at 10_x, 4096 at 9_3 | A hard runtime limit — `CreateTexture2D` fails outright above it |
| `df::max_texture_bytes` | `min(128 MiB, dedicated VRAM / 8, physical RAM / 16)`, floor 64 MiB | What one displayed image may occupy, given the compared image, its fade-out, thumbnails, the glyph atlas and map tiles are live at the same time |
| `df::max_decode_bytes` | `min(2 GiB, physical RAM / 8)`, floor 64 MiB | The transient full-resolution frame a codec builds before anything can be scaled down |

The dedicated figure is `DXGI_ADAPTER_DESC::DedicatedVideoMemory`, falling back to
`SharedSystemMemory` for integrated parts that report no dedicated pool. Total rather than available
memory is used, so the same file behaves the same way twice. Both byte budgets only ever tighten the
fixed ceilings the app shipped with, so a large machine behaves exactly as before.

`clamp_to_texture_budget` fits every decode target inside the first two, preserving aspect ratio.
This is what lets a high-aspect-ratio panorama display at all: a 40000 x 2000 source in a 1600 x 1000
pane reduces to 20000 x 1000 by the integer ladder, which no device will accept, and the clamp brings
it to 16384 x 819. Without it the upload failed and the pane drew nothing.

`files::exceeds_decode_budget` guards the third, in `files::image_to_surface` so every deferred
decode — display, index thumbnails, the edit view — is covered, and in each of the loaders that
decode during `files::load`. The media view checks it up front as well, so it can say
[`Too large to display`](file-io.md#411-when-an-image-cannot-be-shown) rather than reporting a
generic failure after the fact.

### The sidebar globe

The [sidebar globe](locations.md#50-the-sidebar-globe) is rasterised entirely in software, into a
surface the element uploads as a texture. That is what makes it backend-independent for free: the
pixels are produced before either backend sees them, so there is nothing to keep at parity.

The view has two degrees of freedom, so a per-pixel lookup table is not available — a vertical drag
would invalidate every entry on every frame. Instead each destination row is walked in segments of
sixteen pixels, the exact sphere inverse is evaluated only at segment endpoints, and the source
coordinates are interpolated between them. That is a sixteenth of the transcendentals a per-pixel
inverse would cost, which is what makes resampling on every frame of a drag affordable. Two
consequences are load-bearing rather than incidental:

- A segment that straddles the date line has its endpoint longitudes unwrapped before interpolation.
  Without that the row sweeps the whole map backwards and shreds. `Should render the globe` covers
  this by reading the sampled position back out of the pixels: a shading-invariant channel ratio
  encodes where each pixel came from, and the largest step along a row is 1.4 with the unwrap and 32
  without it.
- A segment whose endpoints come within ten degrees of a pole is evaluated per pixel instead.
  Longitude swings arbitrarily fast there and a straight line between two endpoints is simply wrong.
  **This one has no test**: near a pole the correct rendering is itself heavily compressed, so no
  cheap assertion separates the two, and it is verified by eye at a polar view.

Reduced copies of the source, halved until they are small, cover the compression toward the limb,
where one screen pixel can span dozens of source texels; the level is chosen per segment from how far
the source coordinates moved across it. The source is the map with the heat overlay already composited
into it, so the reduced copies are rebuilt whenever that is, and the resample runs only when the view
or the element's size actually changed — a repaint that moved neither redraws the texture it has.

## Text

Text uses **DirectWrite** (`DWriteCreateFactory`). The D3D backend renders glyphs
through `d3d11_text_renderer` per font face into a glyph atlas sampled by
`_pixel_shader_font`; the software backend has its own `software_text_renderer`
implementing `IDWriteTextRenderer`.

`DWRITE_GLYPH_RUN::glyphIndices` holds font-specific glyph IDs, not characters, so
whether a glyph contributes geometry is decided from its atlas result (an empty
raster, as produced by a space) and never by comparing an ID against a character
literal. The staged vertices are flushed after the run's loop, because the last
glyph in a run may itself contribute nothing.

For the same reason, **a glyph cache must be keyed by the face the index belongs
to, not by the index alone.** Font fallback means one renderer sees several faces
within a single layout, and index 42 in a fallback face is a different glyph from
index 42 in the primary face. Both backends key through `glyph_face_keys`
([../src/platform_win_visual.h](../src/platform_win_visual.h)), which assigns each
face a small id and holds a reference to it, so a released face cannot be
reallocated at the same address and silently alias another face's entries.

**The run's em size is part of the same key.** A face carries no size - the size
lives on the glyph run - so a renderer that ever sees two sizes would otherwise
serve a raster made at the earlier size to text drawn at the later one, mixing
glyph sizes inside a single string. A renderer is normally single-size, but a text
layout built before a font-size change and drawn after it is not, so the key does
not depend on that.

A draw context therefore learns its font size before anything measures through it:
measuring builds and caches the element text layouts, so `update_font_size` runs
first (`bubble_impl::show`), and the software backend routes `begin_draw`'s size
through `update_font_size` rather than recording it, which would leave the text
renderers built at the previous size while making the later call believe it had
nothing to do.

The D3D glyph atlas grows on demand and is capped; a glyph too large for the cap is
rendered without being cached rather than being allowed to grow the atlas without
bound.

## Hardware video pipeline

Hardware-accelerated video decode is integrated in
[../src/av_format.cpp](../src/av_format.cpp) and presented in
[../src/platform_win_d3d11.cpp](../src/platform_win_d3d11.cpp). See
[third-party.md](third-party.md) for the FFmpeg build configuration.

- **Decoder setup** (`av_format_decoder::init_streams`): when GPU video is enabled
  (`setting.use_d3d11va`), the decoder scans the codec's advertised hardware
  configs and selects the **D3D11VA** one (`pix_fmt == AV_PIX_FMT_D3D11`,
  `device_type == AV_HWDEVICE_TYPE_D3D11VA`). Only that path is wired into the
  renderer, so other hwaccels (e.g. dxva2) are skipped rather than installed and
  then rejected. It creates an FFmpeg-owned D3D11 device via
  `av_hwdevice_ctx_create`, installs `get_hw_format`, and sets `extra_hw_frames`.
- **Decoded frames** arrive as `AV_PIX_FMT_D3D11` NV12/P010 array textures on the
  FFmpeg device. `av_get_d3d_info` exposes the texture, array index, orientation,
  and color space.
- **Presentation** (`d3d11_texture::update(const av_frame_ptr&)`): because the
  decoder runs on FFmpeg's own D3D11 device and the renderer on the app device, a
  frame is bridged across devices through a **keyed-mutex shared texture**:
  1. the decoder's array-slice is copied into a shared texture on the video device
     (`CopySubresourceRegion` under the hwcontext lock),
  2. the shared texture is opened once on the render device via a shared NT handle,
     and its opened view + both keyed mutexes are **cached** (recreated only on a
     resolution/format/decode-device change — opening a shared handle per frame is a
     heavyweight kernel operation and must not sit in the render loop; the decode
     device is part of the key because each decoder owns its own FFmpeg device and a
     producer copy cannot be written from a different one),
  3. the render device copies the shared texture into a shader-readable texture and
     the YUV shaders sample it.

  `IDXGIKeyedMutex::AcquireSync` returns `WAIT_TIMEOUT` and `WAIT_ABANDONED` as
  *success* HRESULTs, so both acquisitions are tested against `S_OK`. Both keys are
  owned by this one function, so an uncontended acquire returns immediately and the
  wait is capped at a few milliseconds — this code runs on the UI thread, so a wait
  long enough to be noticed as a stall is itself the failure. If either side cannot
  take the mutex, or the consumer cannot release it, the shared chain is dropped and
  rebuilt on the next frame, so a mutex left in a bad state cannot stall video
  permanently.

  The FFmpeg hwcontext lock covers the producer-side copy **and** the render-device
  work that opens and copies from the shared texture, because that work reads state
  owned by the decode device. It is released on every exit path.

If HW decode is unavailable, video falls back to software decode + `av_scaler`.

### Read-ahead and the surface pool

Read-ahead is budgeted in **bytes**, not frames (`av_read_ahead_bytes` in
[../src/av_format.h](../src/av_format.h)). Counting frames alone made read-ahead cost
whatever the resolution cost: sixteen queued 1920x816 frames measure 63 MB, and 4K is
four times the frame. `av_queue::should_receive` stops at the budget but never below
`av_read_ahead_min_frames`, so a very large frame shortens the queue rather than
emptying it, and `av_queued_payload_bytes` charges a frame what it actually occupies —
its own buffers for software frames, the pool surface it pins for hardware ones.

The D3D11VA pool is **one texture array allocated in full when the stream opens**, so
every surface is paid for whether or not it is used. FFmpeg provisions the decoder's own
reference frames (`ff_dxva2_common_frame_params`: one base surface plus sixteen for
H.264/HEVC) and clamps the total to 64; `extra_hw_frames` is only what the app itself
holds. It is therefore derived from the same budget — `av_read_ahead_frames` plus a
margin for the frame on screen, the two `update_for_present` holds while it settles, and
one the decoder emits after the queue stopped asking. **The queue limit and
`extra_hw_frames` must stay in step**: hold more surfaces than the pool has and
`d3d11va_pool_alloc` fails that frame with "Static surface pool size exceeded". Frame
threading is off on this path because it costs one more pool surface per thread and buys
almost nothing once the GPU is decoding. `d3d11_texture::update` logs the pool's real
size the first time it bridges a stream.

### Hover previews

The scrubber tooltip and the hovered item thumbnail scrub a video through a **second decoder**,
owned by the `media_preview` worker in [../src/app_workers.cpp](../src/app_workers.cpp) and kept
away from playback entirely. It is software-only and video-only, one file at a time, and closes
itself after five idle seconds. Requests coalesce: only the newest survives, because the pointer
has usually moved on by the time one finishes.

A seek lands on the key frame at or before the target, so `decode_nearest_frame` decodes forward
to the frame the player would actually jump to. That walk is the whole cost of a preview — 30 to 50
frames for a typical GOP — so it must stay cheap: **only a reference to the nearest frame is kept
and exactly one scale runs**, at the end. Scaling every improving frame instead cost a bicubic pass
and a fresh surface per frame of the GOP and threw all but the last away.

Landing at or before the target is what makes the preview match where the player jumps, and it is
also what makes the walk long: seeking to the key frame *after* the target, as the pre-1.27.0 code
did, answered from the first frame decoded. Responsiveness is bought back by abandoning instead.
**A walk stops as soon as a newer hover is queued behind it**, answering with the nearest frame it
has reached — but never before it has one, so every hover still shows something. A pointer in
motion therefore pays roughly the cost of a single frame, and the request the pointer stops on is
the one that runs to completion and lands exactly.

The decoder is reused across hovers, so **every request seeks**, including one near the start.
Skipping the seek for a small position leaves the walk beginning wherever the previous hover
stopped, where every frame is already past the requested time and the first one seen is returned.

The two hovers do not want the same thing. The scrubber promises the frame playback will jump to,
so it walks to the exact one. A hovered thumbnail only says what the video contains, so it passes a
tolerance — a fraction of the duration, currently 2% — and the walk stops at the first frame inside
it, which is normally the key frame the seek already landed on. That is one decode rather than a
GOP. The hovered thumbnail also costs a PNG encode and a re-decode on `async_queue::render`,
because the frame is persisted as the item's thumbnail; that is a few milliseconds of work but it
is two hops behind whatever else that queue is carrying.

## Resilience

Rendering is defended by several mechanisms, all aimed at "never leave the user
with a blank or crashing window":

- **Crash guards** ([../src/platform_win.h](../src/platform_win.h) /
  `platform::set_crash_guard`): a durable marker is raised before risky GPU work
  (device creation, HW decode) so that a crash is attributed on the next launch and
  only the offending capability is disabled — GPU rendering and HW video decode are
  guarded independently. The first successful `Present` clears the GPU render guard.
- **Device loss**: a device-loss `HRESULT` from `render`, `Present` or
  `ResizeBuffers` routes to `frame_base::handle_device_loss`. It marks the GPU
  render crash guard (so the next launch starts in software mode) and posts a
  private `WM_DIFF_DEVICE_LOST` message. Recovery runs from the message loop, not
  inside the render call stack, because it destroys the draw contexts and the
  device that are still live on the stack. `handle_graphics_device_lost` then:
  1. raises `ui::os_event_type::graphics_device_lost`, which makes the app release
     every GPU-backed resource (`app_frame::free_graphics_resources`),
  2. calls `factories::downgrade_to_software`, releasing the D3D device and setting
     `software_mode`, and
  3. calls `reset_graphics()` on the main window, which rebuilds its own and every
     child frame's draw context — now on the CPU backend.

  The session continues rendering in software; it does not close the window or
  attempt to recreate a GPU device that has just failed.
- **YUV texture SEH fallback**: some drivers fault inside `CreateTexture2D` for
  NV12/P010. A narrowly-scoped `__try/__except` (`yuv_upload_seh_filter`) catches
  only that access violation, disables YUV textures durably (`setting.use_yuv =
  false`), and falls back to RGB video/JPEG rendering. All other exceptions
  propagate to the global handler so real bugs are not hidden.

  `setting.use_yuv` is the single switch this fallback shares with the Advanced
  option and with safe start, so it is read where the format is chosen — in
  `files::decode_jpeg` and `load_webp`, which then decode to RGB, and in
  `d3d11_texture::update(const_surface_ptr)`, which converts any NV12 surface
  decoded before the fault rather than asking the driver for another YUV texture.
  A producer that ignores the setting leaves all three with no effect and the
  faulting driver with no way out; `Should honor the yuv texture setting`
  (test_files.cpp) is the guard.
- **Software fallback**: any failure to build a D3D draw context downgrades that
  window to CPU rendering, and a runtime device loss downgrades the whole process
  (see Device loss above). The software backend type-checks textures handed to it,
  so textures created by the lost GPU backend are ignored rather than misused.

## Design discussion: single vs. two D3D11 devices for video

The hardware video pipeline currently uses **two** D3D11 devices — FFmpeg's own
decode device and the app's render device — bridged per frame by a keyed-mutex
shared texture. A frequently-raised alternative is to use a **single** device:
pass the app's render device into FFmpeg (`av_hwdevice_ctx_alloc` + set
`AVD3D11VADeviceContext.device`, then `_init`) so decoded textures already live on
the render device. This section records the tradeoffs so the choice is explicit.

### Pros of a single device

- **Fewer copies.** The producer copy, the shared-handle bridge, and the
  keyed-mutex handoff disappear. Best case drops from two GPU copies + a
  cross-device sync per frame to a single copy — or even a direct shader-resource
  view over the decoder's array slice (true zero-copy) if the frames pool is
  allocated with `D3D11_BIND_SHADER_RESOURCE`.
- **Less VRAM.** No duplicate shared texture / render-side copy per active video.
- **Simpler, less fragile code.** The entire shared-handle / keyed-mutex path — a
  historically crash-prone area with a dedicated crash guard — goes away.
- **No cross-device driver quirks.** Sharing NT handles between two devices is
  exactly where flaky drivers misbehave.

### Cons of a single device

- **Immediate-context contention (the big one).** Decode runs on a worker thread
  and rendering on the UI thread. With one device they share one immediate
  `ID3D11DeviceContext`. Multithread protection (already enabled) makes that *safe*
  but *serialized* — decode submissions and UI rendering can block each other,
  risking UI hitches. The two-device design deliberately isolates decode from the
  render context to protect UI responsiveness (a primary driver).
- **Device-loss blast radius.** A decode-triggered device reset (TDR) would take
  down rendering too, instead of being contained to the decoder's device.
- **Extra requirements on the render device.** It must be created with
  `D3D11_CREATE_DEVICE_VIDEO_SUPPORT`, and decode profile/format support must be
  validated or you fall back anyway.
- **Adapter mismatch edge cases.** On multi-GPU / hybrid systems the best decode
  adapter may not be the render adapter; forcing one device can pick a worse
  decoder.

### How to decide well

Make it evidence-based, not a guess:

1. **Define the metric that matters** — frame present latency and UI-thread stall
   time during playback, not raw decode throughput. UI smoothness is the point of
   the two-device split.
2. **Instrument the current path first.** Time `update()` (producer copy, cached
   handle open, consumer copy) and measure UI-thread frame intervals during 4K/60
   playback and scrubbing. That is the baseline.
3. **Prototype behind a setting.** Add the single-device path as opt-in (mirroring
   `use_d3d11va`) so both run on the same build/hardware. Never replace outright.
4. **A/B on representative hardware** — old iGPU, modern iGPU, discrete, and a
   hybrid laptop. Stress with 4K/HEVC/10-bit, multiple simultaneous videos
   (thumbnails), scrubbing, and window resize.
5. **Watch the worst case, not the average.** Compare 99th-percentile UI frame
   time and dropped frames. A single device that is faster on average but stutters
   while scrubbing is a regression under the "responsive UI" driver.
6. **Have a fallback rule.** If single-device is enabled but the device lacks
   `VIDEO_SUPPORT`, decode profile support is missing, or the decode adapter differs
   from the render adapter, silently fall back to the two-device bridge.

### Current recommendation

Keep the two-device bridge as the default: it protects UI responsiveness, and the
per-frame cost is small now that the shared-handle open is cached. Treat
single-device as an opt-in experiment — it is the bigger theoretical win (and much
simpler code), but the only thing that justifies changing the default is measured
latency/stall data showing it does not hurt UI smoothness on weak/hybrid GPUs. If
the numbers hold across that hardware matrix, flip the default and delete the
bridge.

## Where this lives

| Rendering subject | Source |
|---|---|
| The portable drawing abstraction: colours, surfaces, textures, contexts | [ui.h](../src/ui.h), [ui.cpp](../src/ui.cpp) |
| The hardware backend, shaders, shared-texture handoff, device loss | [platform_win_d3d11.cpp](../src/platform_win_d3d11.cpp) |
| The CPU backend and its fixed 512-square tile walk | [platform_win_software.cpp](../src/platform_win_software.cpp) |
| The rasterizer itself, the present seam, and the parity probe | [render_software.h](../src/render_software.h), [render_software.cpp](../src/render_software.cpp) |
| Text layout, glyph rasterisation, measurement | [platform_win_font.cpp](../src/platform_win_font.cpp) |
| Surface allocation, scaling, cropping, format conversion, the mark | [render_surface.cpp](../src/render_surface.cpp) |
| Tone curves, saturation, vibrance, contrast, brightness | [render_color.cpp](../src/render_color.cpp) |
| Pixel-level transforms and differences | [render_image.cpp](../src/render_image.cpp) |
| The sidebar globe's software resample | [render_globe.cpp](../src/render_globe.cpp), [ui_globe.h](../src/ui_globe.h) |
| SSE/AVX/NEON paths behind the above | [util_simd.h](../src/util_simd.h) |
| Demux, decode, scaling, resampling, hardware acceleration | [av_format.h](../src/av_format.h), [av_format.cpp](../src/av_format.cpp) |
| Playback session, A/V sync, transport | [av_player.h](../src/av_player.h) |
| Audio output and buffering | [av_sound.h](../src/av_sound.h), [platform_win_sound.cpp](../src/platform_win_sound.cpp) |
| The spectrum visualizer | [av_visualizer.h](../src/av_visualizer.h) |
| Visual styles, DWM composition, theming | [platform_win_visual.h](../src/platform_win_visual.h) |
| Session GPU counters | [util.h](../src/util.h) — `df::gpu_perf`; see [implementation](implementation.md#session-diagnostics) |

The two backends must agree. `/test:*surface*` and `/test:*colour*` cover the software paths
directly — `Should area downscale packed surfaces` compares the AVX2 and SSE2 results byte for byte,
which is what proves the AVX2 path is live at all — but nothing in the suite drives a real device,
so a change to [platform_win_d3d11.cpp](../src/platform_win_d3d11.cpp) is verified by running both
`.\dd.ps1 run` and `.\dd.ps1 cpu` and comparing.
