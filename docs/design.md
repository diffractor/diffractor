# Diffractor Product Design

This document owns Diffractor's durable user concepts and behavior. It describes what the product should mean and do, not the class structure or backlog. Differences between code and this document are design issues to resolve.

## Primary design drivers

Diffractor has two co-equal primary design drivers:

1. **Fast, lightweight performance.** The application starts quickly, responds immediately to common interactions, scales to large collections, works progressively during indexing, and avoids unnecessary CPU, memory, storage, and network use. Performance is part of the user experience, not an implementation detail.
2. **A clear user mental model.** The application uses consistent concepts so users can predict the current scope and contents, the target of every command, the resulting effect, and whether it is recoverable. Hidden targets, silent state changes, and context-dependent surprises violate this driver.

Features and workflows must satisfy both drivers. A faster interaction that obscures its target or effect is not acceptable, and clearer behavior should not require avoidable delay or resource cost.

## Product promise

Diffractor is a fast, private Windows organizer for photos, videos, and audio. It helps people find, inspect, compare, describe, edit, and safely manage media on storage they control. It should feel like one persistent collection viewed through changing scopes, not unrelated tools.

## User mental model

Every interaction makes four facts clear:

1. **Scope: where results come from.** The indexed collection, one folder, a recursive folder, an external folder, or a search. Changing scope is navigation and does not modify files.
2. **Contents: what is visible.** The query, photo/video/audio filters, grouping, and sorting determine presentation, not files. Show visible and hidden counts when filters apply.
3. **Target: what a command affects.** Singular commands act on the focused or singular displayed item; batch commands act on the complete, visibly selected set. Focus, viewing, comparison, and pin state never create hidden targets.
4. **Effect: what changes.** Consequential commands identify the action, count, source, destination, collisions, retained originals, and recovery before execution.

Every workflow must support this sentence:

> In this scope, this command affects this target and produces this recoverable or permanent effect.

The following JSON Schema is the normative shape of the user mental model. All four fields are required, unknown fields are forbidden, and each value must describe the current interaction rather than an implicit or future state.

```json
{
	"$schema": "https://json-schema.org/draft/2020-12/schema",
	"$id": "diffractor.user-mental-model.schema.json",
	"title": "UserMentalModel",
	"type": "object",
	"additionalProperties": false,
	"required": ["Scope", "Contents", "Target", "Effect"],
	"properties": {
		"Scope": {
			"type": "string",
			"enum": ["Indexed collection", "Folder", "Recursive folder", "External folder", "Search", "All scopes"]
		},
		"Contents": {
			"type": "string",
			"minLength": 1,
			"description": "The query, media filters, grouping, sorting, and visible/hidden counts that determine presentation."
		},
		"Target": {
			"type": "string",
			"enum": ["Focused item", "Singular displayed item", "Complete visibly selected set", "Visible items"]
		},
		"Effect": {
			"type": "string",
			"minLength": 1,
			"description": "The action, count, source, destination when applicable, collision policy when applicable, retained originals, and recovery class."
		}
	}
}
```

These names and values form a closed ontology. An implementation or design proposal must use them exactly and must not infer additional scope or target states. When a proposed interaction cannot be represented by this schema, product clarification is required before implementation.

The schema describes user-visible interactions. Work that changes no observable behavior — rendering backends, threading, indexing internals, codecs, build configuration, tests, refactors — has no scope and no target, and must not be described with these values. Such work is governed by [implementation.md](implementation.md) and the change classes in [AGENTS.md](../AGENTS.md); borrowing a scope or target for it is a violation, not a fallback.

### Command availability

Whether a command is offered answers two questions and no others: **is its effect visible on the current surface**, and **does its target qualify**. Nothing else dims a command, and a command that passes both is never silently inert.

- A command is available on the surfaces where its effect can be seen. Commands that change the item listing or the sidebar — filters, recursion, grouping, sorting, detail and thumbnail size, sidebar visibility, large-item highlighting — belong to Items. Commands that change what is rendered of an item — scale, rotation, RAW preview, verbose metadata, zoom, playback — belong to Items and Fullscreen alike. Commands that belong to a task view are offered only in that task view, and window, help, language, and eject commands answer from everywhere because they do not depend on what is being browsed.
- A command that acts on items is available when its target qualifies, and when it does not it says why rather than dimming without explanation. The eligibility test names the reason: nothing selected, not a local file, not writable, not a photo.
- A submenu answers the same test as the entries it opens. A parent that opens onto a menu of uniformly disabled entries promises what its contents refuse, so the parent carries the union of their tests.
- While a command is running, availability describes the view, not the running command. The re-entrancy guard that suppresses target-acting commands during a run never inverts into making an unrelated command available.

## Vocabulary

| Term | Meaning |
|---|---|
| Collection | Persistent folders indexed by Diffractor. Membership, its declaration, and its edge are owned by [collections.md](collections.md). |
| Scope | The collection, folder, recursive folder, external folder, or search being viewed. Shared presentation infrastructure may use All scopes only when its behavior is provably identical in every concrete scope, stated with its reason; it is never a default for work that fits one concrete scope or no scope. |
| Query | Search criteria within a scope. |
| Results | Items matching the scope and query. |
| Visible items | Results remaining after visibility filters. Viewport-driven loading and cache work may operate on the currently presented subset without making it an implicit command target. |
| Focus | Keyboard cursor and range-selection anchor. |
| Selection | Complete visible target set for batch commands. |
| Displayed item | Singular item shown in a preview or media surface. |
| Pin | A visibly selected item held while focus moves for comparison. It carries an orange badge wherever it appears, leads the selection preview, and is released by clicking that badge. |
| Preview | A non-committing media display or proposed operation. |
| Analyze | Compute an operation from a specific snapshot. |
| Run | Execute an analyzed operation after revalidating its snapshot. |
| Presence | A file's direct membership in the collection and, for an outside file, whether possible copies are known in the collection. |
| Duplicate | Items considered identical with stated confidence. |

Qualify overloaded terms: use **navigation history**, **recent searches**, **import history**, and **date chart**, not simply "history."

### Naming views, modes, and presentation choices

Names are closed in the same way scope and target are. One concept has one name, used in the product, in these documents, and in source.

**View.** A named top-level presentation that occupies the main surface; exactly one is current. The set is closed:

| View | Kind | Source `view_type` |
|---|---|---|
| Items | Browser | `items` |
| Fullscreen | Presentation | `media` |
| Edit | Task view | `edit` |
| Tags | Task view | `tags` |
| Locate | Task view | `locate` |
| Rename | Task view | `rename` |
| Convert | Task view | `batch` |
| Metadata | Task view | `batch` |
| Date | Task view | `batch` |
| Import | Task view | `import` |
| Sync | Task view | `sync` |

Write the name capitalized and alone; the word "view" may follow it in lowercase where the sentence needs a noun, as in "the Edit view". A view is never a mode, so "edit mode", "tag mode", and "items mode" name nothing. **Fullscreen** is one word — never "full screen" or "full-screen" — and is the only user-facing name for that view; **Media view** names the internal renderer that draws it and belongs to [implementation.md](implementation.md).

**Mode.** A durable state the user enters and must leave, which changes what commands do or which are offered. There are exactly two: **zoom mode**, owned by [zoom.md](zoom.md), and a running **Slideshow**. A state that lasts exactly as long as one gesture is not a mode and carries its own name, such as **inspect zoom**.

**Presentation choice.** A user-owned setting that changes how the current view draws without changing scope, contents, or target: thumbnails or details, detail and thumbnail size, grouping, sorting, regular or compact density, verbose metadata, and navigator display. Each is named directly and none is called a mode.

**Panel form.** The selection information panel's classification — Singular, Comparison, or Selection summary — determined by the selection rather than entered by the user. Owned by [selection-controls.md](selection-controls.md).

"Mode" keeps its ordinary engineering meaning for states no user enters, such as a failure mode or the software rendering mode.

## Application structure

The search/address box and sidebar navigate folders, dates, locations, media types, ratings, labels, tags, duplicates, drives, and saved searches.

- **Items** is the windowed browser. It shows grouped thumbnails or details alongside an optional media/metadata preview.
- **Fullscreen** gives media the whole display for immersive photo, video, audio, comparison, or selection presentation.
- **Task views** cover Edit, Tags, Locate, Rename, Convert, Metadata, Date, Import, and Sync.

Items is the only windowed browsing presentation. Its browser and preview use a resizable divider and scroll independently. Relayout from resizing, toolbar wrapping, or presentation changes preserves orientation in each pane: the focused visible item remains at its prior screen position when possible, otherwise the content nearest the viewport center is retained, with proportional scroll position as a fallback when that content no longer exists. When a command names the items it has just produced — a paste, or opening a specific file — the listing scrolls that new selection into view, because the command selected it in order to show it. Zoom or comparison may temporarily give the content area to media; leaving restores the browser without changing scope or target. Fullscreen is temporary presentation state and preserves scope, query, filters, grouping, sorting, focus, selection, splitter position, and Items scroll position.

The main render surface owns every affordance that acts on something visible in it. The right controls panel exists only for task views whose parameters have no visible referent. Commands therefore sit on the surface they change:

- The commands that act on selected items sit in the information panel that describes them, so a command and the complete visibly selected target are read together. [Selection controls](selection-controls.md) owns this panel's form classification, region order, content priorities, state and user-metadata presentation, command placement, comparison and summary rules, regular and compact density, responsive behavior, spacing, long-form Description placement, and relationship to verbose metadata in Items and Fullscreen.
- The item list carries the commands that decide its own contents and presentation — media-type filters, recursion, grouping, sorting, detail and thumbnail size, and the filter box. They sit at the head of the list and scroll with it, so a long list is never permanently shortened by a fixed band. Focusing the filter box brings the list back to the top so the box being typed into is always visible.
- Reporting and control are separate affordances. The totals affordance reports the visible item count and total size and discloses its per-type breakdown on hover; it carries no menu. Changing grouping and sorting belongs to its own control, labelled with the current grouping, so what the list is doing and how to change it are never the same button.
- The control area may carry additional optional rows below the filter row when the current scope makes them meaningful — a parameter control for the active query, a breakdown strip offering drill-down within the current results, or a derived-cluster strip. Each row appears only when it would act on something present, keeps a stable height while shown, and is presentation and navigation only: no such row changes grouping, sorting, focus, or selection. Location's rows are specified in [locations.md](locations.md#7-the-items-control-bar).
- Optional control rows share a height budget measured against available content height, because the user came to see items. When applicable rows would exceed it they collapse to one-line headers from the bottom of a defined priority order, never while one of their controls is captured, and a row the user expands explicitly stays expanded for the session.
- A control the user is actively manipulating keeps ownership of its value across result refreshes. Refreshes triggered by that control coalesce, discard superseded results, and must not recreate, reposition, or write back to the captured control; the settled value commits once.
- When the item list can scroll, the base of its scrollbar provides a back-to-top action. It changes only the item-list scroll position; focus and selection remain unchanged.
- When filters hide some items, the bottom of the visible items states the hidden count and offers to clear them. When filters hide every item, this action appears directly below the filter controls, covering the common recovery case without leaving an apparently empty list.

Every task view opens its controls panel with one explainer stating what the task does, what it acts on, and what it keeps or writes. The explainer is visually distinct from the controls below it, so it reads as a description of the task rather than as a parameter; being distinct, it needs no divider under it. Where a task can preview before running, the explainer says so. Below the explainer the panel names the target set and its count before it offers parameters, so the target is read before the parameters that act on it.

A toolbar button names the operation the current view will actually perform. Where one toolbar serves several tasks, its run button is relabelled per task, so the button never promises an operation the view cannot do.

Task views preserve their originating context so Cancel or completion returns to an understandable place.

Edit, Locate, Tags, and the metadata batch task place their selector strip below the primary renderer. The selector remains part of the task context while keeping the main work surface directly below the shared top bar; the task controls continue beside both regions.

The strip offers only the items the task can act on, so what it shows is what a run would write. Where the task acts on one item, a click moves the task to that item. Where it acts on a set, a plain click selects one item and anchors the range, Ctrl adds or removes one item, and Shift selects from the anchor to the clicked item; the clicked item takes focus in every case. Entering a task reveals the focused item, and a task view without a strip holds no items.

In a task view, the view-specific toolbar replaces the Items window-control group and is right-aligned in the shared top bar. Its final group is separated from task actions and contains the current Maximize or Restore command followed by a text-bearing Close command. This Close exits the task and returns to Items; it never exits the application. Items retains the application Minimize, Maximize/Restore, and Close controls.

Because that toolbar lives in the shared top bar, a task view is never fullscreen. Fullscreen is refused from a task view, and starting a task from fullscreen leaves fullscreen and keeps the selection the task targets, so the task always opens with its own controls visible rather than being started into a presentation that hides them.

## Navigation and search

Search uses the local index and supports text, phrases, tags, media types, metadata, dates, locations, ratings, negation, comparisons, and Boolean expressions. The address box also accepts folders and offers relevant completions.

- Focusing the address box begins an editing session and snapshots the committed address. Typing maintains a separate draft and refreshes completions for that draft.
- Up and Down highlight completions and preview the highlighted text in the address box without changing the draft or regenerating the completion list. Clicking a completion commits it and returns focus to the content view.
- While previewing a completion, Escape restores the latest typed draft, keeps the popup open, and highlights its first completion. A subsequent Escape, or Escape when no completion is being previewed, restores the address captured when editing began, closes the popup, and returns focus to the content view.
- Enter commits the highlighted completion when one is selected, otherwise it commits the visible address. Plain Tab accepts the highlighted completion into the draft without running it, refreshes completions, keeps the popup open, and retains address focus. Shift+Tab performs normal reverse focus traversal.
- Moving focus elsewhere closes the completion popup without redirecting focus back to the content view.
- Parsed input retains the user's visible spelling and quoting.
- Sidebar actions disclose whether they replace or refine the current query.
- A history entry owns at least its query and selection. Back and Forward restore the destination without overwriting it; navigating after Back removes the forward branch.
- Parent means **broaden this scope** and must not depend surprisingly on incidental selection. It drops one narrowing at a time - date, then media type, then the remaining terms, then a folder level - so the step is the same size whether or not a folder is in the query. A scope with nothing wider to show, such as a drive root, reports no parent and Parent is unavailable rather than repeating the query.
- Next and Previous folder move between siblings of the current folder. At the first or last sibling they do nothing and are unavailable; they never change level.
- Navigating from a visualization, summary, breakdown, or group header changes the query only. Grouping, sorting, and filters are user-owned presentation and are never reassigned as a side effect of navigation.
- Filters change what is visible, not what exists. An item a filter hides leaves the selection, so every command still targets only visible items; removing the filter shows the item again but does not reselect it. Grouping and sorting reorganize visible items without modifying files. Shuffle is visibly exclusive with deterministic sorting.
- Place names, distances, and location queries follow [locations.md](locations.md).

## Selection and viewing

- Plain click selects one item, except for a visibly pinned comparison item.
- Ctrl toggles, Shift extends from focus, and Ctrl+Shift combines both.
- Dragging empty space creates a rectangular selection; Select All targets visible items.
- Empty space is inert: a click that hits no item, and a drag whose rectangle covers no item, leave the selection unchanged rather than clearing it.
- Right-clicking an unselected item selects it before a batch-oriented menu opens.

Selection, focus, pin, hover, and error states are visually distinct. The preview represents selection: one file previews, two compatible files compare, and larger selections summarize. Folder selection does not masquerade as media.

Opening media preserves selection. Fullscreen is presentation state, not a different target model.

Play and Slideshow are separate named behaviors and neither one changes a durable preference.

- **Play** is transport for the displayed video or audio only. It is unavailable when the displayed item cannot play, and it also stops a running slideshow so the key that started a sequence always ends it.
- **Slideshow** presents the visible items in order starting from the focused displayed item. Photos are held for the slideshow delay, which is at least one second; video and audio play to their end. Only photos, video, and audio take part, so a folder, document, or archive is stepped over rather than stalling the sequence, and a slideshow that can no longer reach a playable item stops instead of appearing to keep playing. A video or audio item reached by a slideshow starts playing even when autoplay is off.
- **Continue with the next item** governs whether finishing one item hands over to the next while browsing normally. A slideshow always continues, because continuing is what the mode means.
- **Repeat** governs only what happens at the ends of the sequence: repeat one holds on the current item, repeat all returns to the first item after the last, and no repeat stops there.

Playback advances through visible media according to these clearly named behaviors. When the displayed media has multiple audio tracks, playback visibly names the active track and offers the alternatives as one exclusive choice, separate from the audio output device. Track names prefer authored title, language, role, and channel layout, with a one-based `Audio track N` fallback; "track" identifies the selectable stream while "channels" describes its mono, stereo, or surround layout. Temporary and latched zoom are distinguishable; comparison always uses two visibly selected items.

Hovering across a video thumbnail previews frames by pointer position. Preview requests are latest-wins; only a successfully decoded frame is shown or retained, and an obsolete result is not shown after the item's path or modified date changes. The final successfully previewed frame becomes that video's rebuildable SQLite thumbnail and survives restart. Every durable thumbnail records the source file's modified timestamp as its version key: exact equality reuses it without opening the source, while any forward or backward change makes it stale and allows one replacement decode. Previewing never hydrates an offline file and never changes a media file.

Resume from the last played position is an optional playback preference and is enabled by default. Closing media, including navigation to another item, saves the last presented position to the rebuildable index; if a seek or resume is still synchronizing, it saves the accepted target instead, so closing before the first resumed frame arrives does not replace that position with zero. Resume applies to media longer than ten seconds only when the saved position is more than two seconds from the start and five seconds from the end, avoiding a surprising resume for barely started or effectively completed media. Disabling resume starts playback normally but does not modify the media file.

## Collection presence

Presence is a file-only browsing aid that separates direct collection membership from possible-copy assessment:

- **In collection** means the file itself is in a folder that belongs to the indexed collection. It does not mean the file is unique.
- For a file outside the collection, **Possible copy**, **Possible newer copy**, and **Possible older copy** mean that current evidence identifies one or more likely collection copies. **Possible** states confidence rather than identity proof.
- When candidates imply different relationships, report the result most useful to an import decision: a possible newer collection copy takes precedence; otherwise a possible same version takes precedence over a possible older copy.
- **Outside collection; no possible copy identified** means no likely copy was found from the collection information currently available. It does not prove that no copy exists.
- Show **Checking presence** while the comparison is incomplete. Do not publish an absence result from incomplete collection information.

Presence refreshes when the outside file changes, indexed evidence changes, or collection membership changes. Results may improve progressively during indexing, but completed work must replace provisional results without requiring navigation or restart.

Presence supports browsing and import decisions. It is not a deletion recommendation, does not choose a keeper, and never adds possible copies to the command target. Duplicate inspection or resolution must independently establish its confidence, target, effects, and recovery.

## Metadata, editing, and files

Diffractor reads and writes common EXIF, IPTC, XMP, ID3, and container metadata, including ratings, labels, tags, descriptions, dates, and locations. Metadata persists in the media file or an XMP sidecar; the workflow states which. The index is a cache, not the authority. Mixed selections either pass eligibility as a whole or explicitly show unsupported items as Skipped. [File I/O](file-io.md) owns how a write reaches disk and what it guarantees on failure. [Metadata](metadata.md) owns which standard each container is read from and written to, the per-property tag mappings, and the current read and write limitations.

Single-item Edit contains only photo pixel adjustments and keeps them as a draft until Save. Save updates the file; Save As creates and opens another file; leaving a changed draft offers Save, Don't Save, and Cancel. Reset affects the draft, not a saved file.

Metadata changes use the Metadata task for the complete visibly selected set. Each non-tag field is independently optional and changes only when its checkbox is selected. Tag changes use the Tags task, so metadata and photo editing do not duplicate tag controls.

Adjust Date and Time is one shift applied to the complete visibly selected set. The new starting date replaces the earliest known date in the selection and every other item keeps its distance from that date; items with no known date all receive the new starting date. The offered starting date is the earliest date in the selection, so an untouched task leaves every date as it is. The picker never shows a date the task would not use, and the offered date comes from the current selection rather than from a previous use. Review states the original and resulting date for each item. Run writes the capture metadata and the Windows created time in place, creates no original backup, and cannot be undone. A starting date that cannot be a capture date, such as a future date, refuses Run and states why.

Direct rotation is an in-place selected-set command. Convert or Resize creates new output while retaining sources. Rename, Copy, Move, Paste, and Delete follow the same target/effect rules. Primary files and sidecars are one logical item. Destination commands require an unambiguous folder.

Every collision uses one explicit policy: Replace, Skip, Auto-rename, or Block Run. A destination claimed twice by one plan is a collision on the same terms as a destination that already exists on disk, so two sources can never resolve to one file without the policy being stated in Review.

Delete distinguishes **Recycle**, recoverable from the Windows Recycle Bin, from **Permanent delete**, which cannot be recovered and always requires count-bearing confirmation. Use direct wording: "Can be recovered from the Recycle Bin," "An original backup will be created," "The source file will remain unchanged," or "Cannot be undone." Diffractor has no unified application-wide undo.

The following enumerations are closed. Agents and implementations must not add aliases, fallback values, or inferred states:

```json
{
	"CollisionPolicy": ["Replace", "Skip", "Auto-rename", "Block Run"],
	"DeleteOperation": ["Recycle", "Permanent delete"],
	"PresenceConfidence": [
		"In collection",
		"Possible copy",
		"Possible newer copy",
		"Possible older copy",
		"Outside collection; no possible copy identified"
	]
}
```

`Checking presence` is an incomplete evaluation status, not a `PresenceConfidence` value. A confidence value may be published only after the presence evaluation is complete.

## Guided operations

Import, Sync, Convert, batch Metadata, Date adjustment, batch Rename, and future duplicate resolution use one grammar:

1. **Scope**: identify sources and destinations.
2. **Target**: capture and show the item snapshot and count.
3. **Options**: choose transformations, direction, collisions, and recovery.
4. **Analyze**: compute a proposed plan.
5. **Review**: show actions, overwrites, skips, conflicts, and permanent effects.
6. **Run**: revalidate and execute the approved snapshot.
7. **Results**: retain Succeeded, Failed, Skipped, and Canceled details.

Changing options, target, destination, or relevant files invalidates analysis. Cancellation belongs to one operation, stops future work, and reports partial completion without claiming rollback.

This grammar is a strict state machine:

```json
{
	"GuidedOperationState": ["Scope", "Target", "Options", "Analyze", "Review", "Run", "Results"],
	"initialState": "Scope",
	"terminalState": "Results",
	"forwardTransitions": {
		"Scope": ["Target"],
		"Target": ["Options"],
		"Options": ["Analyze"],
		"Analyze": ["Review"],
		"Review": ["Run"],
		"Run": ["Results"],
		"Results": []
	},
	"runPreconditions": {
		"currentState": "Review",
		"analyzeObjectExists": true,
		"analyzeObjectValidated": true,
		"approvedSnapshotMatchesCurrentInputs": true
	}
}
```

`Run` is logically dependent on a validated `Analyze` object for the reviewed snapshot. `Options -> Run` is forbidden, as is any direct execution path that bypasses `Analyze` or `Review`. Changing scope, target, options, destination, or relevant files invalidates the `Analyze` object; the operation must return to the changed state and traverse `Analyze -> Review` again before `Run` becomes eligible.

Import distinguishes new imports, import-history records, existing destinations, overwrites, and skips; history or path collision is not proof of duplicate content. Sync names both ends and direction and separates copies, replacements, conflicts, and deletions on each side.

Duplicate resolution states confidence, requires an explicit keeper, and lists every removal and recovery class. Related items never become targets automatically.

## System states and consistency

Surfaces distinguish Loading, Searching, Empty Folder, No Results, Filtered Results, Unavailable Scope, Indexing, Offline, Download Required, Unsupported, and Decode Failed. Each names the scope and offers a next action; blank content is not an error state.

Long work shows operation, current item, completed and total counts, and Cancel. Results and actionable errors remain afterward. Indexing progressively improves results while browsing remains usable and discloses when results may be incomplete.

Visible state converges automatically after its source changes. Results, filters, groups, selection, focus, command availability, address, sidebar summaries, presence, previews, layout, and paint must not remain stale until unrelated navigation or input. Progressive background results apply only to the scope and generation that requested them; superseded work must not overwrite newer state.

Changes refresh the smallest sufficient surface while preserving every dependent invariant. Paint-only changes must not restart searches or scans, while data changes must refresh all affected derived state before it is presented as current. The implementation-level invalidation contract is defined in [implementation.md](implementation.md#view-invalidation).

Fades and other alpha transitions are decoration, never a carrier of meaning, so they are dropped whenever they would cost more than they convey. When the application runs on the CPU software renderer, or the system asks for reduced client-area animation, every alpha transition completes immediately: thumbnails, photos, loading indicators, and the edit grid appear at their final appearance instead of fading in or out. The result is identical, only sooner; no state, target, or availability differs between the animated and immediate presentation.

Persist durable preferences, not transient focus, selection, pin, zoom, playback, or operation progress. Settings either apply immediately and say so or use Apply/Cancel consistently.

Network features independently disclose trigger, transmitted data, recipient, purpose, and disable control. Keyboard, toolbar, menu, context-menu, and accessibility paths share availability, targeting, confirmation, and validation. Escape unwinds the most local temporary state first.

## Feature map

| Area | Capabilities |
|---|---|
| Collection and find | Indexed/external folders, metadata search, filters, groups, sorting, saved searches. Collection membership owned by [collections.md](collections.md). |
| Locations | Qualified place names, place and distance search, map areas, visit timeline. Owned by [locations.md](locations.md). |
| View and organize | Photos, RAW, video, audio, archives, slideshow, zoom, compare, tags, ratings, dates, locations. |
| Edit and files | Crop, rotate, color, optional metadata fields, tags, convert, email, rename, copy, move, recycle, permanent delete. |
| Operations | Import, sync, duplicate inspection, progress, cancellation, results. |
| Preferences | Collection, display, playback, language, privacy, and performance. |

## Design review

- Scope, contents, target, and effect are unambiguous.
- Consequential actions show counts, destinations, collisions, and recovery.
- Previewed operations execute only the validated snapshot.
- Cancellation and partial failure leave accurate results.
- Empty, loading, offline, unsupported, and error states offer a next action.
- All command entry points behave alike and invariants have regression tests.

Collection membership belongs in [collections.md](collections.md). Location behavior belongs in [locations.md](locations.md). Zoom behavior belongs in [zoom.md](zoom.md). Implementation structure belongs in [implementation.md](implementation.md). GitHub issues own design gaps and planned changes.