# Items View and Media View — Behavioural Specification

This document describes the runtime behaviour of Diffractor's two primary content
views — the **items view** (a grouped thumbnail browser with an attached
preview pane) and the **media view** (a full-screen single-item viewer/player) —
in enough detail for a separate agent to re-implement them. It is derived from
the current C++ implementation in [src/view_items.h](src/view_items.h),
[src/view_items.cpp](src/view_items.cpp), [src/view_media.h](src/view_media.h),
and the supporting model/UI infrastructure
([src/model.h](src/model.h), [src/model_items.h](src/model_items.h),
[src/ui_view.h](src/ui_view.h)).

---

## 1. Shared concepts

### 1.1 View modes
`view_type` (in [src/util_interfaces.h](src/util_interfaces.h)) enumerates all
top-level view modes. Only two are covered here:

- `view_type::items` — grouped thumbnail browser plus side preview pane.
- `view_type::media` — full-screen single-item viewer/player.

Other modes (`edit`, `import`, `rename`, `sync`, `locate`, `test`) replace the
content area entirely and are out of scope.

The current view mode is owned by `view_state` (see
[src/model.h](src/model.h)). Setting it raises a `view_changed(view_type)`
notification on the `state_strategy`/`app_frame`, which:

1. Calls `deactivate()` on the outgoing view.
2. Swaps `view_frame::_view` to point at the new `view_base`.
3. Calls `activate(extent)` on the new view.

Selection, scroll position, focus item, search, grouping, sort and slideshow
state all live in `view_state` and therefore survive the swap; only per-view
layout caches (visible items, scrollers, controllers) are torn down.

### 1.2 view_base, view_element, view_controller
All views derive from `view_base` ([src/ui_view.h](src/ui_view.h)). The
contract a view must provide is roughly:

- `activate(sizei extent)` / `deactivate()` — lifetime hooks.
- `layout(measure_context&, sizei extent)` — recompute element bounds.
- `render(draw_context&)` — paint the view.
- `controller_from_location(loc)` — return the `view_controller` that should
  receive mouse events at a point.
- `context_menu(loc)` — return a `menu_type` for right-click.
- `tick()` — periodic update (animations, slideshow advance, lazy loads).

Content is composed from `view_element` instances. Each element has
`bounds : recti` (in logical coordinates, i.e. before scroll offset is
applied), a set of style bits (`view_element_style`: `hover`, `highlight`,
`selected`, `focus`, `background`, `shrink`, `center`, `disabled`,
`important`, `error`, …) and `render(dc, offset)` / `layout(mc, bounds)` /
`measure()` methods.

A `view_controller` represents an in-flight mouse interaction (drag, splitter,
scroll, drag-to-select, drag-and-drop). The host stores at most one active
controller; mouse events are dispatched to it until the user releases the
button or hits `Escape`. Controllers also draw transient overlays (e.g. the
selection rectangle).

### 1.3 view_state and async pipeline
`view_state` ([src/model.h](src/model.h), [src/model_items.h](src/model_items.h))
owns the model that both views render:

- `_search` — current `search_t`.
- `_search_items` — raw matches.
- `_display_items` — filtered/sorted matches.
- `_item_groups` (`groups()`) — vector of `item_group_ptr`, the visible
  hierarchy.
- `_selected` — currently selected `df::item_element_ptr`s.
- `focus_item()` — keyboard focus item.
- `display_state()` — texture/player state for the currently-displayed item(s).
- `view_mode()` — current `view_type`.
- `is_full_screen` — full-screen toggle.

The async system (`_async`) marshals work between UI, database and decoder
threads:

- `queue_database(...)` — bulk thumbnail / metadata reads from SQLite.
- `queue_ui(...)` — runs a callback on the UI thread.
- `queue_media_preview(...)` — RAW/HEIF preview decode.
- `invalidate_view(invalid_flags)` — coalesces redraw / re-layout requests.

Invalidation flags (see `ui_view.h`):
`view_layout`, `view_redraw`, `group_layout`, `media_elements`, `controller`,
`scroll`, `tooltip`. Multiple flags can be ORed; the next tick processes them.

---

## 2. Items view

Class `items_view` in [src/view_items.h](src/view_items.h) and
[src/view_items.cpp](src/view_items.cpp).

### 2.1 Purpose
The items view is the application's main browsing surface. It shows a grouped,
scrollable grid (or detail list) of file thumbnails on the left and an
optional preview/metadata pane for the current selection on the right. It is
the source of all selection, drag-and-drop and bulk command operations.

### 2.2 Layout
The client area is divided by a vertical splitter:

```
+--------------------------+  +--------------------+
|         group A title    |  |  media element     |
|  [thumb][thumb][thumb]   |  |  (photo / video /  |
|  [thumb][thumb][thumb]   |  |   audio / hex /    |
|         group B title    |  |   archive list)    |
|  [thumb][thumb]          |  +--------------------+
|                          |  |  toolbar           |
|                          |  +--------------------+
|                          |  |  metadata sections |
+--------------------------+  +--------------------+
       items scroller            media scroller
              (splitter — draggable)
```

Splitter geometry:

- `setting.item_splitter_pos` is stored on a 0–10000 scale.
- `splitter_pos()` converts to a pixel X, clamped so that each side is at
  least `96` px wide and at most half the client width.
- A hit zone of a few pixels wide either side hosts a `splitter_controller`.

When the display is in **zoom** or **comparison** mode
(`display_state_t::zoom()` / `comparing()`), the items list and splitter are
hidden and the media element fills the entire client area; the side scroller,
toolbar and metadata are also suppressed.

Two scrollers live on the view:

- `_items_scroller` — vertical scroll for the items grid.
- `_media_scroller` — vertical scroll for the media pane (used when the
  metadata column overflows).

Each scroller exposes `offset()`, `bounds()`, `draw_scroll(dc)` and a
`changed_func` callback that the view uses to recompute the visible items
window.

### 2.3 Data sources
- `_state.groups()` — ordered `df::item_groups` (`std::vector<item_group_ptr>`).
- Each `item_group` (see [src/model_items.h](src/model_items.h)) carries:
  - `_items : item_elements` — its `item_element_ptr`s.
  - `_display : item_group_display` — `icons` (thumbnail grid) or `detail`
    (row list with columns).
  - `bounds : recti` — logical bounds after layout.
  - `_key : group_key` — identity for stable reuse across regrouping.
  - `scroll_text`, `icon` — used to label the scrollbar quick-jump popup.
  - `sort(group_by, sort_by, group_by_dups)` — orders items inside the group.
- Each `item_element` exposes `name()`, `extension()`, `file_type()`,
  `metadata()`, `file_size()`, `file_modified()`, `media_created()`,
  `duplicates()`, `is_folder()`, `has_thumb()`, `random()`, `bounds`,
  `row_layout_valid` and the `view_element_style` bitset.

### 2.4 Grouping and sorting
Grouping is recomputed by `view_state::update_item_groups()` whenever the
search, filter or `group_by` setting changes. The previous group map is reused
keyed by `group_key` so that scrollbar position and group expansion are stable.

`group_by` modes (`model_items.h`):
`file_type`, `shuffle`, `size`, `extension`, `location`, `rating_label`,
`date_created`, `date_modified`, `camera`, `resolution`, `album_show`,
`presence`, `folder`. Each mode supplies a static keying function (e.g.
`date_key`, `camera_key`, `extension_key`) that maps an item to a `group_key`.

`sort_by` modes: `def`, `name`, `size`, `date_modified`. The sort runs inside
each group via `item_group::sort`. Concrete sorters: `name_sorter`,
`size_sorter`, `modified_sorter`, `created_sorter`, `pixel_sorter`,
`group_sorter`. The date direction is reversed when
`!setting.sort_dates_descending`. When `group_by_dups` is set on the search,
duplicate runs are coloured by alternating `item_element::alt_background` for
visual separation.

For each group, `items_view::items_changed(path_changed)` builds:

- a title `view_element` (group label, item count, optional sort-direction
  toggle, optional "Searching…" / "All items filtered out" indicators),
- the contained items as a child element list,
- and an optional inline UI (e.g. clickable "Clear filters" link in the empty
  state).

If `path_changed` is true, the items scroller is reset to the top; otherwise
layout tries to anchor the current focus or centre item to avoid scroll
jumps.

### 2.5 Selection model
- Multi-select is the default. `_state.selected()` is the canonical set;
  `focus_item()` is the keyboard cursor.
- `_state.select(item, toggle, extend)` is the single entry point:
  - plain click → `select(item, false, false)` (replace selection),
  - Ctrl+click → `select(item, true, false)` (toggle this item only),
  - Shift+click → `select(item, false, true)` (extend from anchor),
  - Ctrl+Shift+click → `select(item, true, true)`.
- `_state.select(rect, toggle)` is used by drag-to-select.
- `_state.select_all()` selects every visible item.
- `_state.clear_selection()` clears it.
- The "anchor" for shift-extend is the previous focus item.

Selected items render with the `selected` style bit; the focus item adds the
`focus` bit; hovered items add `hover`; drag-to-select preview adds
`highlight`. The colours come from `dc.colors` and `view_handle_color()` in
[src/ui_view.h](src/ui_view.h).

### 2.6 Mouse interactions
The view installs three controllers depending on hit-test:

#### 2.6.1 `item_select_controller` (drag-to-select / click)
- **Left down**: stores `_start_loc`, `_first_tic` (for double-click timing),
  sets `_tracking = true`, marks the candidate item as `highlight`.
- **Move**: if movement exceeds an 8 px threshold, switches to `_selecting`
  mode and applies `highlight` to all items inside the rubber-band rectangle.
  For items whose `file_type()` carries `file_traits::preview_video`, the
  cursor X within the thumbnail is forwarded to
  `_state.load_hover_thumb(x_offset, width)` to scrub a preview frame.
- Auto-scroll: while `_selecting`, if the cursor is within the top/bottom
  32 px of the items area, the scroller advances by a fixed delta on each
  tick.
- **Left up**:
  - If `_selecting`: `_state.select(rect, ctrl_down)`.
  - Else: `_state.select(item, ctrl_down, shift_down)`.
- **Double-click** (within the system double-click time and within a few px
  of `_start_loc`):
  - `_state.open(host, item)`. For a media item this also calls
    `_state.toggle_full_screen()` and switches to the media view; any active
    slideshow stops.
- **Escape**: cancels rubber-band selection and reverts highlights.
- **Draw**: paints a translucent blue selection rectangle while `_selecting`.

#### 2.6.2 `item_drag_controller` (drag-and-drop source)
- Activated when `_tracking` is true and the cursor leaves the original item
  bounds before a click is committed.
- Sets the global `df::dragging_items` flag (suppresses background updates),
  detaches file handles via a RAII `detach_file_handles` guard, then calls
  `platform::perform_drag(paths, effects)` with the selected files/folders.
- Does **not** itself receive the drop; the OS does. On return the flag is
  cleared and the view is invalidated.

#### 2.6.3 `splitter_controller`
- Hit zone is a few pixels around `splitter_pos()`.
- Drag updates `setting.item_splitter_pos` (clamped) and re-lays out both
  panels. While dragging, the splitter is drawn at the live position rather
  than the saved one and a wait-cursor is suppressed.

#### 2.6.4 Other mouse behaviour
- **Mouse wheel over items**: scrolls `_items_scroller`.
- **Ctrl+wheel over items**: increments/decrements `setting.item_scale`
  (thumbnail size) and forces a re-layout.
- **Wheel over media pane**: scrolls `_media_scroller` if the metadata
  overflows; otherwise it has no effect.
- **Right-click**: `context_menu(loc)` returns `menu_type::items` if the
  click is on the items side and `menu_type::media` if on the media side.
  If the right-clicked item is not selected it is selected first.
- **Hover**: the hovered element gets the `hover` style bit; the previous
  hovered element is invalidated. Tooltips are shown via the standard
  `view_host` tooltip mechanism. The scrollbar shows a quick-jump popup
  with the current group's `scroll_text` and icon.

### 2.7 Keyboard interactions
The items view exposes navigation primitives that are bound to commands in
[src/app_commands.cpp](src/app_commands.cpp):

- **Arrow Up / Down**: `line_up()` / `line_down()` perform a vertical scan
  (8 px steps, up to ~100 steps) from the focus item to find the closest
  item on the previous/next visual row. If nothing is found, fall back to
  `_state.select_next(forward)` which crosses group boundaries.
- **Arrow Left / Right**: `_state.select_next(forward)`.
- **Page Up / Page Down**: scroll one viewport height and move focus.
- **Home / End**: jump to first / last visible item.
- **Enter**: `_state.open(focus_item)` — same as double-click.
- **Space**: toggles selection of the focus item.
- **Ctrl+A**: `_state.select_all()`.
- **Esc**: cancels active controller (rubber-band, splitter drag, slideshow).
- **Delete**: invokes the delete command on the current selection.
- **Type-to-search**: passed through to the search box (see app shell);
  the items view itself does not own the typing buffer.

All other shortcuts (rate, label, rotate, copy, paste, F2 rename, etc.) are
plain command bindings and operate on `_state.selected()`.

### 2.8 Rendering
`items_view::render` (called by the host once per frame for the dirty
region):

1. If `display->zoom()` or `display->comparing()`: render only
   `_media_element` filling the client area, then return.
2. Otherwise:
   - Iterate `_visible_items` (the cached visible window) and draw in this
     Z-order so focus/hover float on top:
     1. Backgrounds for non-focus, non-hover items.
     2. Thumbnails for non-focus, non-hover items.
     3. Background + thumbnail of the focus item.
     4. Background + thumbnail of the hover item.
   - Group titles and inline group UI are drawn at their logical bounds.
   - Detail-mode groups call `update_detail_row_layout()` first.
   - The active controller's `draw(dc)` is invoked last (rubber-band,
     splitter ghost).
   - `_items_scroller.draw_scroll(dc)` and `_media_scroller.draw_scroll(dc)`
     paint scrollbars unless they are being actively tracked.
   - `draw_splitter(dc)` paints the splitter handle.
3. The media pane, if present, is rendered through its own element tree —
   see §2.10.

Items themselves are rendered by `item_element::render_bg(dc, offset)` and
`item_element::render(dc, offset)`. Thumbnails are drawn as textured quads
with optional badges (rating, label, sidecar, video, raw, archive, error)
and an optional file-name caption beneath. Items missing a thumbnail fall
back to a file-type icon placeholder.

### 2.9 Scrolling and visible-item virtualisation
- `update_visible_items_list()` is called whenever
  `_items_scroller.offset` changes (registered as the scroller's
  `changed_func` in the constructor).
- It computes the visible logical rectangle plus a ±50 % vertical
  overscan and walks groups to collect the `item_and_group` pairs that
  intersect that band into `_visible_items`.
- It also gathers items whose thumbnails are present in the database but
  not yet in memory (`db_thumbnail_pending`) and posts them as a single
  batch to `_async.queue_database()` (calls `db.load_thumbnails(...)`).
- Items with no thumbnail at all are forwarded to
  `item_index.queue_scan_displayed_items()` for decode.
- Items with `file_traits::preview_available` (RAW/HEIF) post a
  `queue_media_preview()` request; when the preview arrives the view is
  invalidated and the item re-renders.
- The scrollbar's quick-jump popup is built from `view_scroller_section`s
  derived from `(scroll_text, icon)` runs across groups.

### 2.10 Side media pane
`update_media_elements()` runs whenever the active selection or display
state changes. It rebuilds the right-hand element stack:

1. A media control sized to the pane width:
   - Bitmap photo → `photo_control`.
   - Video → `video_control` (with scrubber, multi-track support).
   - Audio → `audio_control` (with visualiser).
   - Archive → `file_list_control` listing contained entries.
   - Commodore disk image → `commodore_disk_control`.
   - Other binary → `hex_control` (hex dump, truncated at 1 MB with a
     warning row).
2. A small spacer.
3. Selection-scoped controls created via
   `_state.create_selection_controls(...)` (play/pause, volume, seek,
   rotate, etc.).
4. Metadata sections — EXIF, IPTC, XMP, ICC, file properties, embedded
   cover art (`cover_art_control`), GPS map preview, etc., each preceded by
   a `create_text_title()` section header.

`stack_elements()` lays the stack out vertically with consistent padding.
The media element itself is centred horizontally if narrower than the pane,
or stretched when its element style includes `shrink`. The
`_media_scroller` covers the metadata overflow; its bounds are
`recti(split_x ± control_padding, cy/2, cy)`.

### 2.11 Empty / error / transitional states
- Empty result set with active filters → group-title element shows
  "All items filtered out" with a clickable "Clear filters" link that
  invokes the corresponding command.
- Background scan in progress
  (`_state.item_index.searching > 0`) → "Searching…" label.
- Items with decode errors get `set_style_bit(error, true)`; they render
  with an error tint. `_state.clear_error_items()` clears the flag (e.g.
  after the user retries an operation).
- During drag-and-drop (`df::dragging_items`) the view suppresses
  background re-scans and async refreshes to avoid invalidating the
  source data mid-drag.

---

## 3. Media view

Class `media_view` in [src/view_media.h](src/view_media.h).

### 3.1 Purpose
A single-item, full-bleed viewer/player that replaces the items view when
the user opens a media file (photo, video, audio, animated image, RAW with
preview). It owns no selection model of its own — it always renders
`view_state::display_state()` and uses `select_next()` to navigate.

### 3.2 Layout
The media view fills the client area
(`calc_media_bounds() = {0, 0, cx, cy}`) and overlays UI on top of the
content:

- **Media element** (`_media_element`) — same control classes as the items
  view side pane (`photo_control`, `video_control`, `audio_control`, …).
  Centred when smaller than the bounds, scaled to fit when larger,
  preserving aspect ratio.
- **Previous / next chevrons** — left and right edge buttons, roughly
  `cx/17` wide and `cy/7` tall, vertically centred. Bound to
  `commands::browse_previous_item` and `commands::browse_next_item`.
- **Bottom controls bar** — overlaid on the bottom of the bounds, centred
  horizontally. Hosts play/pause, volume, mute, repeat, track-cycle,
  seek bar (for video/audio), zoom controls, rotate, fullscreen, slideshow
  toggle, etc.
- **Slideshow progress bar** — drawn at the bottom-left while
  `is_playing_slideshow()` is true; fills based on
  `(now - _next_photo_tick) / setting.slideshow_delay`.
- **Overlay alpha** — `overlay_alpha` interpolates towards
  `overlay_alpha_target` per `tick()`. Mouse movement over the view raises
  the target to 1.0; after an inactivity timeout it falls back to 0.0
  (full-screen video/slideshow) or to a low resting value (regular use).
  The overlay background is drawn with `overlay_alpha * 0.55f`.

When `display->zoom()` or `display->comparing()` is true the overlay (arrows,
controls bar, progress bar) is suppressed entirely so only the media is
visible.

### 3.3 Display state
`display_state_t` ([src/model.h](src/model.h)) carries everything the media
view needs:

- `_session : av_session*` — FFmpeg-based decoder/player for video/audio.
- `_selected_texture1 / _selected_texture2 : texture_state` — current and
  comparison item textures (GPU surfaces with fade-in animations and
  `_tex_invalid` dirty bit).
- `_item1 / _item2 : item_element` — current and comparison items.
- `_player_media_info : av_media_info` — track counts, duration, codec
  metadata.
- `_comparing : bool` — side-by-side comparison mode (two textures shown).
- `_zoom : bool` — image zoom/pan mode (arrow keys pan instead of nav).
- `_item_pos : size_t` — index of the current item in the displayed
  sequence (used for "n of m" display and progress indicators).
- `is_playing()` — slideshow OR media playback is running.
- `is_playing_media()` — `_session && _session->is_playing()`.
- `is_playing_slideshow()` — global "playing" flag without media playback.

### 3.4 Mouse interactions
- **Wheel**: `_state.select_next(zDelta > 0, ctrl_down, shift_down)`. The
  wheel always navigates regardless of zoom (zoom level is preserved across
  navigation if both items support it).
- **Move**: raises `overlay_alpha_target` and resets the inactivity timer.
- **Left click**:
  - On left/right chevron → previous/next command.
  - On controls bar → forwarded to the underlying control (play, seek,
    volume slider, etc.).
  - Elsewhere on a video/animation → toggle play/pause via the bound
    command.
- **Double click**: toggles full-screen.
- **Right click**: `context_menu(loc)` returns `menu_type::media`.
- **Drag** in zoom mode: pans the zoomed image (handled by `photo_control`
  via its own controller).

### 3.5 Keyboard interactions
All key handling routes through commands; the relevant defaults (subject to
the user's keymap in [src/app_commands.cpp](src/app_commands.cpp)) are:

- **Esc / Backspace**: leave media view → `view_mode(view_type::items)`.
- **Space**: play/pause (slideshow when on a still image, media when on
  video/audio).
- **Left / Right**:
  - In zoom mode → pan the zoomed area.
  - Otherwise → previous / next item.
- **Up / Down**: previous / next item across rows (mirrors items view).
- **Home / End**: first / last item in the current sequence.
- **+ / -**: zoom in / out, when `display->can_zoom()` is true.
- **0**: reset zoom to fit.
- **R / Shift+R**: rotate clockwise / counter-clockwise (image only).
- **C**: toggle comparison mode (requires two items selected before
  entering the media view).
- **F / F11**: toggle full-screen.
- **V**: cycle video track.
- **A**: cycle audio track.
- **M**: mute / unmute.
- **0–9**: set volume to 0 / 10 / … / 90 % (mapping is configurable).
- **Shift+F**: cycle repeat mode (`repeat_none → repeat_all → repeat_one`).
- **Delete**: delete current item and advance.
- **Enter**: enter edit view for the current item.

### 3.6 Rendering
`media_view::render`:

1. Render `_media_element` at offset `{0, 0}` over the full bounds.
2. If not zoomed and not comparing:
   - Multiply the draw context's overlay colour by `overlay_alpha`.
   - Paint a translucent darkening pass over the bottom controls strip
     (`alpha = 0.55f * overlay_alpha`).
   - Render previous/next chevrons.
   - Render the controls element.
   - If `display->is_playing_slideshow()`, render the slideshow progress
     bar.
3. The active controller (if any) draws on top.

The texture for the current item is uploaded by `texture_state` on demand;
if `_tex_invalid` is true a fade-in animation runs as the new texture
arrives. When transitioning between items, the outgoing texture briefly
cross-fades with the incoming one.

### 3.7 Async behaviour
- Opening an item triggers `_state.display_state()->load(...)` which queues
  a decoder request (image, RAW preview, HEIF, video first frame, etc.).
- Video/audio playback is driven by `_session` on its own thread; the view
  schedules redraws via `host->invalidate(...)` from the session callback.
- During playback the view requests `tick()` at the platform frame rate;
  outside of playback `tick()` only services `overlay_alpha` and the
  slideshow advance.
- Memory pressure or backgrounding can cause
  `display_state_t::free_graphics_resources()` to release textures; the
  next render will re-decode lazily.

---

## 4. Transitions between the views

### 4.1 Items → Media
1. User double-clicks an item, presses **Enter** on a focused media item,
   or invokes a command such as `commands::play`.
2. `_state.open(host, item)` is called.
3. If the item's `file_type()` is media:
   - `_state.display_state()` is updated to that item.
   - `_state.view_mode(view_type::media)` is set.
   - If `!is_full_screen` and the item is a media file,
     `_state.toggle_full_screen()` is also called.
4. `app_frame::view_changed(view_type::media)` deactivates `items_view`,
   attaches `media_view` to `view_frame`, and calls `activate(extent)`.
5. The media view begins rendering the existing `display_state`.

### 4.2 Media → Items
1. **Esc**, the close-view command, or the toolbar's "back to items" button.
2. `_state.view_mode(view_type::items)` is set.
3. `app_frame::view_changed(view_type::items)` swaps the view back.
4. The items view re-activates with its previous scroller offset and focus
   item; selection is unchanged because it lives on `view_state`.
5. Slideshow playback, if any, is stopped on exit.

### 4.3 In-media navigation
`browse_previous_item` / `browse_next_item` /
`_state.select_next(forward, toggle, extend)` walk the `_display_items`
sequence (skipping non-media items where appropriate) and replace the
current `display_state` without leaving the media view.

---

## 5. Commands and toolbar

The toolbar host (`app_frame::_items_toolbar`, `_media_toolbar`,
`_media_edit_commands`) is rebuilt by `app_commands.cpp` based on the
current view and selection. View-specific commands include:

- View switching:
  `commands::view_items`, `commands::view_media`, `commands::view_edit`.
- Navigation:
  `browse_previous_item`, `browse_next_item`, `browse_previous_group`,
  `browse_next_group`, `browse_first`, `browse_last`.
- Grouping/sorting:
  `group_by_*`, `sort_by_*`, `sort_dates_ascending`,
  `sort_dates_descending`.
- View options:
  `show_thumbnails`, `show_details`, `item_scale_*`, `toggle_sidebar`.
- Playback:
  `play`, `pause`, `mute`, `repeat_*`, `next_audio_track`,
  `next_video_track`, `volume_*`.
- Display:
  `zoom_in`, `zoom_out`, `zoom_reset`, `rotate_*`, `compare`, `full_screen`,
  `slideshow`.
- Selection:
  `select_all`, `select_none`, `select_invert`.

Each command's enabled/visible/checked state is recomputed from the
current `view_state` (selection, view mode, display state, full-screen
flag, slideshow flag, zoom flag, etc.). The same command set is reused
across both views — only the toolbar's visible subset changes.

---

## 6. Implementation checklist for a re-implementation

A re-implementation should provide, in roughly this order:

1. **Model surface**: a `view_state` analogue exposing `groups()`,
   `selected()`, `focus_item()`, `display_state()`, `view_mode()`,
   `select(...)`, `open(...)`, `select_next(...)`, `select_all()`,
   `clear_selection()`, `toggle_full_screen()`, `invalidate_view(flags)`,
   `create_selection_controls(...)`, plus the async hooks
   `queue_database`, `queue_ui`, `queue_media_preview` and
   `item_index.queue_scan_displayed_items`.
2. **Element/controller framework**: `view_element`, `view_controller`,
   `view_scroller`, `view_host`, `draw_context`, `measure_context`,
   `view_element_style` bits, `view_handle_color`, invalidation flags.
3. **Item model**: `item_element` (with style bits, bounds, badges,
   thumbnail texture, hover-frame support) and `item_group` (with
   keying, sorting and detail-vs-icon layout).
4. **Items view** with: splitter, two scrollers, grouping/sorting,
   selection model, three controllers (select, drag, splitter),
   visible-item virtualisation, async thumbnail batching, side media
   pane construction (`update_media_elements`), empty/error states,
   keyboard navigation primitives (`line_up`/`line_down`).
5. **Media view** with: full-bleed media element, overlay chevrons and
   controls bar with fade animation, slideshow progress bar, zoom and
   comparison rendering modes, wheel-to-navigate, command-driven
   keyboard handling.
6. **View swap**: `view_changed(view_type)` in the app frame that
   activates/deactivates the right view while preserving model state.
7. **Command bindings**: connect navigation, grouping, sorting,
   playback, zoom, selection and view-switch commands to both views.
