# Selection Controls

This document owns the selection information panel's form classification, content, ordering, density, spacing, and responsive behavior. It also inventories the controls currently created for selected items. Product concepts and command targeting remain owned by [design.md](design.md); exact APIs remain owned by source. The panel is built by `view_state::create_selection_controls` and related presentation code.

## Goals

The panel should answer, in order:

1. What is selected?
2. What is its collection presence?
3. How can I view it?
4. What can I do to it?
5. What useful facts and user metadata describe it?

The answer should use a consistent hierarchy without forcing unlike tasks into identical controls. It should pack content at its natural height, omit low-value empty sections, preserve identity under width pressure, and keep commands visibly attached to the target they affect.

## User tasks

The selection determines the user's task and therefore the panel's organization:

| Form | Primary user task | Panel emphasis |
|---|---|---|
| Singular | View, play, inspect, rate, and act on one item | Identity, viewing controls, actions, then properties |
| Comparison | Decide how two comparable items differ | Stable A/B identity and a compact table of high-value differences |
| Selection summary | Understand and act on a complete selected set | Thumbnails, target summary, then applicable batch actions |

Consistency means predictable grouping, spacing, icon treatment, empty states, and command targeting. It does not mean showing single-item navigation in a batch panel or file-changing commands in a comparison table.

## Current implementation

The controls are created in [model.cpp](../src/model.cpp) and consumed in two presentations:

- **Items preview:** `items_view::update_media_elements` calls the non-compact builder. For one item it may append the Description section, stream details, and raw metadata below the selection panel. The selection panel ends the primary block; a Show/Hide verbose metadata affordance closes the optional detail below it, so the Description section is always read as detail rather than as primary content. The affordance appears only when the item actually has stream or raw metadata to reveal, and when verbose metadata is closed and nothing else would sit below the primary block it joins that block, so a lone toggle never turns a centred media pane into a scrolling one.
- **Fullscreen:** `media_view::update_media_elements` calls the compact builder. The panel is hidden below a minimum usable width. On wide displays, a bounded panel holding the item's leading prose field may appear beside it. Zoom and compare states overlay the panel on the media; otherwise the panel reserves space below the media.

The current top-level classification is not the intended three-form model:

| Current branch | Condition | Presentation |
|---|---|---|
| One file | Exactly one selected item and no folders | Media property stack or non-media table |
| Two comparable files | Exactly two selected items, no folders, and a like-with-like trait profile | Comparison renderer and three-column property table |
| Multiple files | At least three selected items and no folders | Thumbnail collage, count titles, and file-group summary |
| Folder, mixed, or incomparable pair | Any selection containing a folder, or two files that are not like with like | Count titles and file-group summary; no collage flag |

`can_compare_file_types` in [files.h](../src/files.h) decides comparison from traits; `display_state_t::is_comparison` combines it with the two-item cardinality fact. `_can_compare` and `_is_compare_video` still separately drive comparison interaction on the media surface and have not yet moved to that predicate, so a pair the panel summarizes may still be displayed side by side. Compact density only reduces the one-media branch. The full comparison table and selected-set summary are currently unchanged in fullscreen.

### One media file

The non-compact panel creates these regions in order:

1. AV transport: Play and scrubber for audio/video. Withheld when the decoder could not open the file, because the pane below is then a hex dump and a scrubber over it claims a position in something there is no way to play.
2. Identity: metadata Title when present, otherwise filename without extension.
3. Identity: metadata Title when present, otherwise filename without extension, followed by any count badges. Badges are read live so a late result appears without a rebuild; the bubble lists the copies and states the current collection presence.
4. Viewing: playback options for playable media, Slideshow, Pin, orientation state, preview state, Scale up, and Fullscreen. Playback options follow the transport and are withheld on the same condition.
5. Actions: reject/label, rating where editable, rotate, Edit, Open, and Tools.
6. File facts: containing folder when it is not already the scope, filename, dates, and size.
7. Technical and descriptive facts when present: dimensions, resolution, codecs, bitrate, audio format, camera, album, artist, retro system, and copyright.
8. Location: current location, or the Add Location command when empty.
9. Tags: current tags capped at six plus a `+N` remainder, or the Edit Tags command when empty.
10. Description: a Description label, or the Edit Metadata command when empty. The description text itself is rendered later by Items or beside the fullscreen panel.

Compact density reduces region 6 to the capture date alone and removes the encoding facts within region 7 — codecs, pixel format, bitrate, and audio format. It keeps dimensions, camera, album, artist, retro system, and copyright, because fullscreen is where the picture is being read and those facts describe the subject rather than the container. The date stays for the same reason: when a photograph was taken is a fact about the subject, and dropping it with the folder, filename, and size left fullscreen unable to answer "when was this?". Long-form prose is not a compact concern; fullscreen renders the leading field in its own bounded panel beside the controls.

### The Description section

An item can carry several prose fields — `description`, `synopsis`, and `comment`. They read as one section under one header, not as a stack of headers, because a per-field header costs more height than the text it introduces.

`prop::descriptive_fields` in [model_property.cpp](../src/model_property.cpp) is the single source of the section's content. It returns the populated fields in the order Description, Synopsis, Comment — Description leads because it is the field the app can write and the one most items carry — and marks any field repeating text an earlier field already holds, which sidecar round trips make common.

The section has one header and one body:

- **Header.** Named for the content: the field's own name while there is exactly one, the section name Description once there is a list. One button cluster serves every field in the section: open link, copy, then `tool_edit_description`. Copy takes the whole section, labelling each field only when there is more than one.
- **Link button.** Links are gathered across all fields. One link opens directly; several offer a menu, because there is then no single obvious destination.
- **Body, one field.** The text alone. No field label, no tree, no expander.
- **Body, several fields.** The verbose-metadata tree, one row per field. The leading field opens by default; the rest collapse to a one-line preview and open on click, with the choice remembered per field. A field marked as a repeat shows the word Duplicate rather than its preview, so the user can see that both fields really are populated without reading the same passage twice.
- **Cover art.** Shown to the left of the body when the item has it and it fits within half the available width.

Comparison keeps one row per field instead. A table exists to align like against like, so folding three fields into one row would hide exactly the difference the form is for.

Of the three, `description` is the field this section's edit command writes: the batch metadata task carries comment and synopsis fields too, but one command cannot mean three.

Presence is stated in the identity bubble in singular and comparison, alongside the list of copies and separately from it.

### One non-media file

This uses a different grammar:

- One row holds name, Pin, the label/reject controls, then Open and Tools at its end. Open is the way out to an application that can render what Diffractor cannot; Tools is the same route to less-common actions the media panel offers, because renaming, deleting, copying, and emailing a file do not require decoding it.
- Further rows show folder, size, created date, and modified date.
- It does not show navigation, rotate, Edit, or Tag, none of which have a meaning for a file the viewer cannot decode. It also does not show rating, tags, Description state, or type-specific properties.
- Nothing in the panel is held above the fold. Hex, Commodore, and archive views contribute no priority region, so the panel and the listing below it scroll as one stack.
- Compact density does not reduce it.

### Two comparable files

The comparison branch creates:

- Per-item label/reject, rating where editable, Pin, Delete, and Unselect controls. There is no shared navigation/action row, because Previous, Next, Rotate, Edit, Tag, Open, and Tools have no unambiguous target while two items are displayed.
- Rows for name, presence, folder, size, created date, and modified date.
- Optional rows for duration, dimensions and codecs, audio, camera, album, artist, retro system, location, copyright, tags, Description, Comment, and Synopsis, rendered from whichever metadata snapshot has arrived.
- A separate identical/not-identical result when CRC or pixel comparison has resolved.

Larger size, later created date, later modified date, and larger pixel area receive rank colour even though “larger” or “later” is not necessarily better. Rows are omitted when both sides have no value.

### Other selections

The summary branch creates, in this order:

- A thumbnail collage for three or more files, capped at 24 cells with an overflow cell. The pinned item is always the first cell.
- Separate folder and file count titles beside the stack glyph.
- A file-group histogram with icon, count, type name, and total size.
- One centred command row holding navigation and the selected-set actions, followed by a Pin control and the held item's name when an item is pinned.

It does not report common user metadata, mixed values, total selected size as one headline fact, or selection-wide eligibility for primary actions. Compact density does not reduce it.

## Required form model

Classification is performed once and presentation code consumes the result. Cardinality alone must not imply comparability.

### 1. Singular

Use when exactly one visible item is selected.

- A file shows identity, current state, actions, user metadata, file facts, and applicable media facts.
- A folder uses the same region order but substitutes folder identity and folder summary facts. It never presents media controls or pretends to have file metadata.

### 2. Comparison

Use when exactly two visible files are selected **and both belong to one supported comparison profile**.

Initial supported profiles are:

- Image with image: side-by-side visual and property comparison; pixel equality may be reported when known.
- Previewable video with previewable video: side-by-side visual and property comparison; aligned preview timelines may be offered when available.

Eligibility depends on stable file traits, not metadata arrival, thumbnail completion, online state, or panel width. An unavailable preview is represented inside the chosen form rather than changing form. Image/video, audio/audio, archive/archive, unrelated file pairs, and any pair containing a folder use Selection summary until an explicit comparison profile is designed for them.

### 3. Selection summary

Use for every other non-empty selection: incompatible pairs, three or more files, multiple folders, and mixed file/folder selections.

This is a selected-set view, not a degraded comparison. It emphasizes count, composition, total size, common state, and available batch actions. A collage is optional supporting content and must not determine the information layout.

An empty selection creates no panel.

## Singular controls

The singular panel is one compact wrapping block with the following groups. The order is fixed; inapplicable controls or empty optional property groups consume no space. Title, Viewing, and Actions share a horizontal line when their natural widths fit, then wrap as complete groups under width pressure.

### 1. Title

- The title is always the first item and always exactly one line high.
- Prefer authored Title for media, otherwise use filename. Trim long text to the row and expose the full value in the tooltip; never wrap the title or increase the row height. Trimming currently clips at a character boundary; an ellipsis sign is a presentation improvement in both draw backends, not a layout requirement.
- A count badge may follow: a sidecar count, then a copy count when the item has copies. Both use the same text, colour, and shape as the badges on a thumbnail and in a detail row, so a count is recognized identically wherever it appears. The badges are part of the title control, not separate controls: the title is one target whose action is to show the items related to this one, which is what a count is for.
- The bubble carries the identity detail the badges summarize: the sidecar files, the copies with their names, dates and sizes, and then the collection presence in the complete status vocabulary from [design.md](design.md#collection-presence), including Checking presence. Redundancy and presence are stated as separate claims rather than folded together, because they answer different questions ([collections.md](collections.md#6-the-collections-edge)).
- Presence is informational and does not add possible copies to the target. Omit it where the concept does not apply, such as a folder.

### 2. Viewing

This group contains only controls that change how the item is presented, or the sequence mode running over it. Nothing in it writes to the file:

- Play immediately followed by the scrubber for playable media, then playback options.
- Slideshow for every media type, because a running slideshow must be visible and stoppable whatever it has landed on. It is the mode over the item sequence, so it reports the mode and not the item: it shows checked with a pause icon while running, and fills along its base as the current photo's delay elapses. Video and audio report their own position on the scrubber, so the toggle carries no fill for them. Play is transport within one item and never reports slideshow state, because invoking it ends the slideshow.
- Pin. Pin holds this item selected so the following item joins it instead of replacing it, which is the only way a user reaches Comparison deliberately; without it, comparison is something that happens to a selection rather than something the user asks for. It changes what is on screen, not the file.
- Show rotated/original orientation when orientation metadata is present.
- RAW/processed preview when both representations exist.
- Scale up and Fullscreen when supported.

Previous and Next are not in the panel in any form. The panel describes one target and offers what acts on it; a control that replaces the target belongs to the surfaces that own moving between items — the fullscreen arrows, the item list, and the keyboard. Offering them here also produced the contradiction of a Next beside a multiple selection that has no single item to advance from.

Use icon controls with tooltips. Keep the group on one row when possible and wrap it as a unit before removing any command. File rotation does not belong here because it changes the item.

### 4. Actions

This group reports and changes item state or invokes a file action:

- Reject/flag, colour label, and rating first, with current values visible in the controls.
- Rotate anticlockwise and clockwise when supported.
- Edit, Open, and Tools in that order.

The boundary between Viewing and Actions is the file: if invoking a control could change bytes on disk or metadata written for the item, it is an Action. Rating and label are Actions although they look like state, because they are written. Scale up and Fullscreen are Viewing although they are commands, because they are not.

Tools is the stable route to less-common actions. Unsupported commands dim in place when the command set is otherwise relevant; controls that have no meaning for the file type are omitted.

### 5. Properties

Properties are compact icon-led lines, ordered by user value rather than metadata schema order:

1. File facts: filename when different from Title, containing folder when it adds scope, size, captured/created date, and modified date.
2. Primary media facts: dimensions and megapixels for images; duration, dimensions, and codec for video; duration, codec, channels, and sample rate for audio; entry count for archives when known.
3. Authored or capture facts: camera and lens followed by exposure, aperture, ISO, and focal length; album and artist for music; system and game for retro media.
4. Secondary technical facts: pixel format, bitrate, audio sample type, copyright, and similar details. These may move behind verbose metadata when space is constrained.

Search-matched values retain emphasis. Optional lines are omitted when every value is absent. Folder counts and sizes use already-published summaries only and never trigger UI-thread filesystem work.

### 6. Editable metadata

Location, Tags, and Description are the final three lines, in that order, so the information block ends with clear opportunities to improve the item:

- Location and Tags lead with their own blue edit command icon, followed by whatever values exist: `[edit] [0 or more values]`. The icon is the same whether the field is empty or full, so the line does not change shape as the item gains a value.
- A neutral, non-clickable field icon is never drawn in front of an editable field; the clickable command icon replaces it rather than sitting beside it.
- A populated location or tag value remains a search link. Opening the edit task is the leading icon's job, not the value's.
- Each icon opens that field's own task for the singular item: `tool_locate`, `tool_tag`, or `tool_edit_description`. Routing through the shared batch Metadata task is not required.
- Description has no line in this block when it is populated: its own section below the panel carries both the text and the same `tool_edit_description` icon. When it is empty the block ends with that icon alone, unlabelled.
- Tags show at most six values followed by `+N` in both densities; activating the remainder opens the Tags task rather than expanding the panel without bound.

## Comparison controls

Comparison is an inspection task. The controls block is a compact table, not a second action toolbar.

### Structure

- The header has stable A and B markers aligned with the media panes. Each title is single-line and trimmed independently, and each carries its own count badges, so comparison spends no row or column on restating them.
- Equality status is the first table row and changes from Checking to the resolved result without changing table geometry.
- One per-item control group sits at the head of each value column and targets only that column's item: reject/flag, colour label, rating where editable, Pin, Unselect, and Delete. Column attachment is what makes the target unambiguous, and discarding the weaker of two candidates is the main reason to compare in the first place.
- Pin, Unselect, and Delete are the three ways a comparison ends, and they belong together in that order: keep this one, drop this one, destroy this one. Exactly one item is pinned at a time, so the two Pin controls read as a pair — the held column is filled and the other is not — and pinning the other column moves the hold instead of adding a second. Holding one column is how a comparison continues into the next candidate rather than restarting from a single selection, so a user comparing one photo against several never has to reselect it.
- The table contains no Rotate, Edit, Open, Tools, Previous, or Next commands, because those are navigation or selected-set commands with no per-column meaning. Visual comparison controls belong on the media surface.

### High-value rows

The default table shows the following rows when either item has a value:

1. File size.
2. Captured/created date, then modified date.
3. Dimensions and megapixels for images, or duration and dimensions for video.
4. Camera and lens for images; codec for video.
5. Exposure, aperture, ISO, and focal length for images when they differ.
6. Rating, reject/flag, and colour label.
7. Location.
8. Tags.
9. Description as a bounded one-line excerpt or `No description`.

Filename belongs in the A/B headers and folder appears only when the folders differ. Pixel format, bitrate, audio sample details, copyright, Comment, Synopsis, and raw metadata are secondary and belong behind Details in regular Items; they are absent from compact fullscreen.

Core rows remain visible when equal because they establish the basis of comparison. Equal secondary rows are omitted. Differences use neutral emphasis; larger, later, or higher is not styled as better. A missing value uses an em dash so A/B alignment is stable. Metadata from either available snapshot is shown without waiting for both snapshots.

## Selection summary controls

A Selection summary is a batch task. Its vertical order is preview, target, then actions.

### 1. Thumbnails

- Show the bounded collage first, including an explicit `+N` overflow cell.
- The collage supports recognition of the target but does not determine control layout or consume unbounded height.
- Folder-only selections may omit the collage; mixed selections may show file thumbnails without implying that folders are excluded from the target.

### 2. Selection information

- Lead with one single-line headline such as `12 items selected` or `8 files and 4 folders selected`.
- Follow with one total size and a compact file-type breakdown. Do not repeat totals in separate titles.
- Show rating, reject/flag, colour label, location, and tags only as common values, `Mixed`, or `None`. Do not enumerate every selected value.
- The information describes the complete visibly selected set, including items not represented by a collage thumbnail.

### 3. Batch actions

Place actions directly below the selection information, never above it:

- Open and Copy first when meaningful for the complete set.
- Reject/flag, colour label, Rating, Rotate, Tags, and Metadata next when supported by the complete set.
- Tools last as the stable route to Move, Convert, Rename, Delete, and other less-common selected-set operations.

Commands act on the complete visibly selected set. Commands that are meaningful for this form but fail whole-selection eligibility remain visible and dimmed. Do not show Previous, Next, Play, scrubber, orientation preview, RAW preview, Scale up, or Fullscreen in Selection summary.

The actions form one centred wrapping row rather than stacked rows, so the batch commands read as a single band under the selection information. The Pin control and the held item's name join the end of that row when an item is pinned, because releasing the hold is one of the actions available on this selection.

## Grading controls

Reject, the colour labels, and the star rating are one culling vocabulary. They appear in the Singular panel, at the head of each comparison column, in the Selection summary batch row, and as the zoom-mode grading row, and they must read the same way in all of them.

### One control, one badge

The six marks are one vocabulary reached through one control. The control is a single badge that shows the mark the item currently carries — a colour label, or Reject when it wins — and a dimmed flag when the item carries neither. Clicking it opens a menu listing the whole vocabulary in this order, which is also the sidebar filter order and the key order:

| Position | Mark | Colour | Icon | Key |
| --- | --- | --- | --- | --- |
| 1 | Reject | Reject red | Cancel | `M`, `Alt+Delete` |
| 2 | Select | Red | Flag | `6` |
| 3 | Second | Yellow | Bullet | `7` |
| 4 | Approved | Green | Check | `8` |
| 5 | Review | Blue | Question | `9` |
| 6 | To Do | Violet | Clock | `P` |

Every mark carries its own icon as well as its own colour, so it remains readable when colours are hard to tell apart and keeps its identity in the badge, the thumbnail, the detail row, the sidebar filter, and the menu. Colour alone is never the only signal. One definition draws the mark on all of those surfaces; a mark is learned once and recognized everywhere.

The menu names every mark, shows its icon, colour, and accelerator, and checks the one currently set, so the whole vocabulary stays visible without the panel spending six icons of width on it. When the item cannot be edited the badge dims, the menu does not open, and the reason is stated in the bubble.

### Exclusivity

- The colour labels are mutually exclusive: choosing one replaces the current one.
- Reject and the star rating are the same stored value, so each replaces the other. A rejected item has no stars, and giving a rejected item a star clears Reject.
- A colour label and a star rating are independent and coexist.

### Setting and clearing

Pointer and keyboard behave identically. Choosing a mark that is already set clears it; choosing any other mark replaces the exclusive value it conflicts with. For the stars, `0` clears, so the stars do not additionally toggle. A keyboard mark applies to the complete visibly selected set and clears only when every selected item already carries that exact value.

### The bubble

The bubble is the only place the grading rules are stated, so it must state them rather than repeat the control's own name. It contains, in order:

1. The current mark's icon and the control's name.
2. Both current values, Label and Rating, as a two-column table. Both appear on both controls, because the two controls share the Reject/rating field and a user who cannot see that will not understand why a star cleared their Reject mark.
3. The exclusive values a menu choice would replace, and only when a choice would actually replace something.
4. The reason editing is unavailable, when it is.

Accelerators are shown on the menu items themselves, beside the mark they apply to, rather than repeated in the bubble.

## Shared content rules

- Identity is never hidden to make room for commands.
- Commands within a group have a stable order and wrap with their group; individual trailing commands do not disappear under width pressure.
- Filled Location, Tags, and Description show their values without a trailing action. Their empty states show clickable calls to add metadata.
- Derived values use already-published model summaries and must not introduce synchronous file or database work.
- Panel-level commands act on the complete visibly selected set. A control that targets only one member must be visibly attached to that member, such as a comparison column head or a media pane, and never sits in a shared command row where its target is ambiguous.

### Equality and availability

- Equality status appears directly below comparison identity and uses `Identical files`, `Same pixels; files differ`, `Different`, or `Checking` as appropriate.
- Unknown is not presented as different.
- A delayed metadata snapshot fills existing rows without moving the panel to another form.
- Offline or failed media shows a bounded availability status in the media surface; property controls remain usable where their data is known.

### Verbose metadata blocks

Verbose metadata is a block inspector, not a curated summary. It reports what the file actually contains so a reader can tell one embedded block from another.

- Each embedded block (media, EXIF, IPTC, XMP, ICC, raw) is a separate section headed by the standard's name, its byte extent, its value count, and whether it was understood. A block that could not be parsed is distinguishable from a block that is absent.
- A further `File structure` block reports how the container itself is assembled, as read by the format scanner rather than by a metadata parser. It is not a metadata standard, so it carries a plain descriptive heading, and it survives having the metadata stripped.
	- For JPEG it lists every marker segment in file order with its offset, length and APP identifier; the frame's encoding process, sample precision, component sampling factors and the chroma subsampling they imply; each quantisation table with its coefficient grid and the quality that table implies; each Huffman table classified as the standard Annex K example table or as one the encoder fitted to the image; the restart interval; any comment segments; and whether the end-of-image marker is present and whether anything follows it.
	- For WebP it lists the RIFF chunks with their offsets, sizes and roles, and whether the image data is lossy or lossless.
	- For HEIF and AVIF it reports the declared brands, the number of top-level images, the primary item, the stored and displayed sizes, bit depth, alpha, colour profile kind, thumbnail, depth and auxiliary image counts, and the metadata items with their types and sizes.
	- These are observations, not verdicts. The block states what the file contains and leaves the reading to the reader; where a fact is conventionally significant, such as camera firmware writing the Annex K tables while software encoders fit their own, the convention is stated next to the fact rather than turned into a conclusion.
- Each block is presented in its own native shape: EXIF grouped by the IFD each entry was read from, XMP as a tree grouped by schema namespace with arrays, structs and qualifiers retained, ICC as a labelled summary, header, and tag directory in file order, and raw grouped by subject (identification, lens, shooting, exposure, environment, GPS, sizes, thumbnails, colour, and the maker-specific groups the file actually has).
- A file's XMP is presented as an ordinary XMP block whether it is embedded in the file or held in a sidecar beside it, and whether the file is a still image, a raw capture, or a playing video. Only one XMP block is shown; which source wins follows the same rule the scanner and the writer use for that format.
- A shape column carries the block's own typing: EXIF format and component count with the tag number, XMP array or struct kind, ICC tag type and size.
- Decoded values are shown alongside their raw form, never instead of it. Derived facts such as an ICC profile's approximate gamut or tone response are annotations on the block, not replacements for the tags they came from.
- Nothing is silently dropped. Entries the parser could not render appear as binary rows with their size, and bytes the parser skipped are accounted for as unread.
- Content too long to read inline is not truncated. It moves behind an expander and is shown as a hex-and-text dump in the code font.
- Duplication between a decoded summary and the raw block is deliberate and labelled, so the reader knows which reading they are looking at.
- Nesting is drawn, not lettered. Each parent runs a connector line down to its children in the manner of the archive contents listing, so structure is read from the shape of the listing rather than from indent glyphs or disclosure arrows.
- Sections expand and collapse in place by clicking the row itself. Small sections open by default; large ones stay closed until asked for, except where a block names a section as the one readers came for — the EXIF `Exif` directory opens however many tags it holds. The posture is remembered for the session and survives changing selection, so a reader who opens a block keeps it open while moving through files, and a section closed by hand stays closed.
- Expansion reveals content already parsed. Opening a section never reads the file or the index again.

## Density and responsive rules

Compact and regular presentations share region order and command placement. Compactness may cap detail; it must not switch to a different grammar.

### Regular Items preview

- Pack panel regions at natural height with normal inter-region spacing. No child grows merely to distribute unused vertical space.
- When media plus primary properties is shorter than the pane, centre that combined block as specified by product design; blank space is outside the block, not inserted between its rows.
- Long-form Description, Comment, Synopsis, and verbose stream/raw metadata follow the primary panel. They are not duplicated inside it. The prose fields share one Description section rather than a header each.
- The verbose toggle follows primary content. Showing verbose metadata must not change the ordering or width of primary regions.
- The toggle is omitted for items with no stream or raw metadata, because an affordance that reveals nothing is noise.
- When Description, Comment, Synopsis, or other detail follows the primary block, the toggle trails that whole run at the base of the stack.
- When no such detail exists and verbose metadata is closed, the toggle ends the primary block so a lone affordance does not force the pane to scroll. Opening verbose metadata returns the toggle to the base of the stack: the pane already scrolls, and the toggle is low-value chrome that should not hold primary space. This is the one sanctioned exception to the toggle keeping a fixed position.

### Compact fullscreen

- Apply compact budgets to all three forms, not only singular media.
- Singular keeps the one-line Title, Presence, Viewing, Actions, one primary media-facts line, Location, Tags, and Description groups. Omit file path, raw dates, capture details, and secondary technical facts.
- Singular shows at most six tag values followed by `+N`.
- Comparison shows its headers, per-column controls, Presence, equality status, and default high-value rows. Details and selected-set command rows stay out of the transient panel.
- Selection summary shows the headline count, common state, applicable batch actions, and at most four type-breakdown rows followed by `+N types`; it never introduces single-item viewing controls.
- The panel is content-height, bottom aligned, and bounded by the existing minimum-width and description-height rules. It never reserves blank rows for omitted content.

### Width adaptation

- Regions wrap as units; individual trailing commands do not disappear.
- Singular Title remains one line, trims, and stays left aligned. Presence, Viewing, and Actions form a tightly packed right-aligned cluster on the same line when they fit, then wrap as complete groups.
- A comparison row is a label plus two equal value columns that share the remaining width and stop shrinking at a minimum usable width. Below that width the row stacks as label, A, then B, keeping each item's values contiguous; it never collapses into an unrelated sequential property list.
- Selection summary keeps its headline to one line and moves composition or breakdown detail below it when needed.
- Below the existing minimum usable fullscreen width, the complete transient panel yields to media.

## Spacing rules

- Use one panel inset and one inter-region gap in every form.
- Use dividers only between semantic groups, never as leading/trailing decoration.
- Do not reserve height for an omitted row or an empty optional group.
- Explicit empty values are limited to one-sided comparison cells; empty Location, Tags, and Description show only their blue edit command icons.
- Tables measure from visible rows only. Summary and collage elements use content-driven height with a stated maximum, not a form-specific minimum chosen to fill the pane.
- A loading or checking state replaces the value in its row; it does not add a second temporary row.

## Implementation direction

The current builder combines classification, content selection, and element construction. Implementation should first produce a small presentation model, then render it in regular or compact density:

- `selection_panel_form`: singular, comparison, or summary.
- Ordered regions with stable identity and command groups.
- Property rows containing label, optional A/B values, value state, and importance.
- Density policy containing tag, comparison-row, summary-row, and long-text budgets.

The model is built from UI-owned item snapshots and published summaries. Rendering remains free of I/O, decoding, database access, and file scanning. Compact density filters the same model instead of taking separate code paths.

Comparison eligibility should become an explicit predicate or profile on display state. `is_two()` remains a cardinality fact and must not be used as the comparison decision. Changing eligibility also changes which pair gets a side-by-side media surface, comparison hit testing, zoom linking, and pair CRC/pixel work, so the eligibility predicate is the single source those call sites read.

### Layout primitives

Rendering uses the existing flex layout and adds controls only where no primitive exists:

- Regions are nested `view_elements` containers. A column container with one `gap` and one `padding` supplies the panel inset and inter-region spacing; children do not grow vertically, so regions pack at natural height.
- Command groups are row containers. Group cohesion under width pressure uses the existing keep-with-next grouping, so a group wraps whole rather than dropping its trailing command.
- Comparison rows are flex, not fixed tables: a row container holding a label and two value columns with equal grow, equal shrink, zero basis, and a minimum usable width. The existing wrap then produces the stacked narrow form.
- Presence is a live element that reads the item's current related count when it measures and its current presence when its bubble opens, because presence resolution invalidates layout rather than rebuilding the panel.
- Existing per-item Delete and Unselect controls are reused unchanged for comparison columns.

## Release scope

The form model above is the target. 1.27.0 takes only the part that removes a wrong answer or a hidden answer without a layout rewrite, a presentation-model refactor, or a new translatable string. Everything else is post-release work in [v-next](v-next.md).

### In 1.27.0

1. **Comparison eligibility.** An explicit profile predicate over file traits decides whether a pair receives comparison controls: image with image, or previewable video with previewable video. Every other pair, including any pair containing a folder, receives Selection summary. Eligibility reads stable traits only, never online status, metadata arrival, or panel width.
2. **Presence as its own badge.** Singular and comparison show the familiar compare/count badge separately from the title. Its bubble uses the existing status vocabulary and lists related items; the badge reads the current count when it measures, so a late result appears without rebuilding the panel.
3. **Editable metadata affordances.** Location and Tags are the last lines of the singular panel and each leads with the icon that opens `tool_locate` or `tool_tag` for that item, followed by whatever values exist. Description has no line here when populated, because its own section below the panel carries the text and the `tool_edit_description` icon; when empty, that icon closes the panel on its own.
4. **Bounded tags in both densities.** The compact six-plus-`+N` cap applies to regular density as well, so one heavily tagged item cannot dominate the panel.
5. **Comparison values from either snapshot.** Optional comparison rows render from whichever metadata snapshot has arrived rather than waiting for both, so the table is not blank while one side loads.
6. **Pin where it is used.** Pin appears in the singular viewing group and at the head of each comparison column, so the control that starts and continues a comparison is present in the two forms that can compare. It previously existed only in the singular non-media table, where comparison is impossible. The pinned item is also marked in the items list, so the held item is distinguishable from the merely selected one.
7. **Pin is recognisable and releasable everywhere.** One orange pin badge is drawn for the held item on every surface that shows it: the item tile, the detail row's leading icon, and the first cell of the selection collage. That badge is itself the release affordance, so a display without a Pin control still lets the user end the hold with one click. The selection summary panel additionally names the held item beside a Pin control, because a 24-cell collage of a 500-item selection cannot show which item is held by position alone.

The first five change what the panel claims and what it reveals. They do not move regions, change spacing, or add strings. The sixth adds one existing control to two existing command groups and adds no strings.

### Deferred to v-next

The presentation-model refactor, the flex rebuild of region grammar and spacing, Viewing/Actions separation, one shared grammar for singular non-media and folders, compact density for the non-singular forms, responsive stacked comparison rows, whole-line activation for an empty metadata line, summary common/`Mixed` state with reordered batch actions, neutral difference emphasis replacing rank colour, title ellipsis in both draw backends, and extending eligibility to the side-by-side media surface, comparison hit testing, zoom linking, and pair CRC work.

Until the media surface reads the same predicate, two selected files still display side by side. That is a viewing arrangement, not a claim of comparability; the comparison table is the claim, and 1.27.0 makes only the table honest.

## Acceptance scenarios

1. Select one titled photo with presence, rating, label, tags, Description, dimensions, camera, and location. Title remains one line; the Presence badge is visibly separate and its bubble states the detailed status; Viewing precedes Actions; properties follow; Location, Tags, and Description are last.
2. Select one photo with no location, tags, or Description. Each line shows only its blue edit command icon, without a neutral field icon. Other absent property groups consume no height.
3. Select one video. Play is immediately left of the scrubber; title remains one line at narrow widths; playback and presentation controls remain separate from file-changing actions.
4. Select one audio file, archive, ordinary document, and folder in turn. Each uses the singular hierarchy and only applicable facts; compact density is bounded for all four.
5. Select two images with different dimensions, dates, tags, and ratings. A/B identity matches the media panes, missing values retain alignment, and differences are emphasized without implying that larger or later is better. The table contains no Rotate, Edit, Open, Tools, Previous, or Next commands.
6. Compare two near-duplicate photos and discard the weaker one. Reject, colour label, rating, Pin, Unselect, and Delete each appear at the head of one column and act only on that column's item; after Delete or Unselect the survivor becomes the Singular panel without an intermediate empty state. Pinning the keeper and then choosing a third photo compares the keeper against it rather than starting over, and pinning the other column moves the hold instead of holding both.
7. Select two identical images before and after CRC/pixel checks complete. The Presence bubble changes from Checking to the resolved result and its badge count updates without a panel rebuild or form change.
8. Select two previewable videos. The pair uses the video comparison profile even while one preview is unavailable.
9. Select an image and a video, two audio files, two folders, and a file plus folder. Each uses Selection summary, not the comparison table.
10. Select a large mixed set. Thumbnails appear first; the panel then shows one total count, composition, total size, common or Mixed state, and a bounded breakdown; applicable batch actions follow the information and do not include Previous or Next.
11. Resize Items and fullscreen from wide to the minimum supported width. Titles remain one line, groups wrap without overlap, no command vanishes selectively, and omitted content leaves no blank rows.

Scenarios 2, 6, 7, 8, 9, and the presence half of 1 verify the 1.27.0 scope. The rest verify deferred work and are not release gates.

## Known gaps to resolve during implementation

- Singular media and non-media branches need one shared region grammar.
- Singular Viewing and Actions need distinct command groups, with orientation/preview controls separated from file rotation.
- Compact density must be applied to non-media, comparison, folder, and summary forms.
- Rank colour should be replaced by neutral difference emphasis except where a beneficial direction is explicitly defined.
- Summary needs common/Mixed user state, one headline total size, actions below the information, and no Previous/Next controls.
- Folder facts must use existing published summaries only; no layout or paint path may scan the filesystem.
- The side-by-side media surface, comparison hit testing, zoom linking, and pair CRC work still key off cardinality and must move to the eligibility predicate.