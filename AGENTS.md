# Diffractor Codebase

Diffractor is a C++20 Windows media organizer with a custom Direct2D/Direct3D UI, SQLite index, and local file/metadata processing.

## Primary design drivers

1. **Fast, lightweight performance:** start and respond quickly, scale to large collections, bound resource use, and keep expensive work off the UI thread.
2. **A clear user mental model:** make scope, contents, target, effect, and recovery predictable; avoid hidden state and context-dependent surprises.

Both are product requirements. Make tradeoffs explicit and validate behavior and performance where affected.

## Information ownership

GitHub issues own work, status, discussion, and follow-up. Source owns exact APIs, enums, and file lists. Every other subject has exactly one owning document:

| Document | Owns |
|---|---|
| [design](docs/design.md) | Durable user concepts and behavior; the scope/contents/target/effect ontology; view, mode, and presentation naming |
| [implementation](docs/implementation.md) | Architecture, data flow, threading, ownership, invalidation, index/search internals |
| [collections](docs/collections.md) | The collection: membership, what it earns, and its edge |
| [locations](docs/locations.md) | Places, location search, distance, map-driven queries, visits |
| [metadata](docs/metadata.md) | Property-to-tag mapping across XMP, EXIF, IPTC, and container tags |
| [file I/O](docs/file-io.md) | Read and write paths: decode ladder, staging, patching, sidecars, backups, rollback, failure contract |
| [rendering](docs/rendering.md) | Backends and parity, device/swap-chain, frame and resize lifecycle, batching, text, video pipeline |
| [zoom](docs/zoom.md) | The zoom model, laws, rendering tiers, and how zoom is judged |
| [selection controls](docs/selection-controls.md) | The selection panel: form classification, content, ordering, density, responsive behavior |
| [third-party](docs/third-party.md) | Vendored dependencies, upgrade procedure, integration patches |
| [Linux port](docs/linux.md) | Portability assessment, platform-boundary debt, staging and open decisions for a Linux build |
| [README](README.md) | Product overview, build prerequisites, command line |
| [release notes](docs/v-1.27.0.md) | What the current release changed, as user-facing features and fixed issues, plus its verification record |
| [post-release context](docs/v-next.md) | Deferred work and why, validation not run, open issues, and invariants that must survive a re-sync or refactor |

Move information to its owner; link rather than duplicate volatile detail.

The two version documents are not work logs. The release notes state what a user can now do and which reported issues are closed; anything scoped out, unfinished, unvalidated, or merely explanatory belongs in post-release context. Neither records the sequence of attempts that produced a change.

## Source directory map

Start from the concrete failing behavior, symbol, test, or file, then route to the owning prefix in `src/`. Do not scan unrelated modules to build general context.

- `app*`: application coordination; `view*`/`ui*`: views, controls, layout, drawing.
- `model*`: state, index, search, SQLite, properties; `files*`/`metadata*`: I/O, codecs, EXIF/IPTC/XMP/ICC.
- `av*`: audio/video; `render*`: surfaces/color/transforms; `platform_win_d3d11.cpp`/`platform_win_software.cpp`: draw backends.
- `platform*`: Windows/system integration; `util*`: shared utilities; `test*`: tests.

## Working rules

- Build with `df.sln`; normal validation is `.\dd.ps1 test`. App source belongs in `src/`; temporary output belongs in `tmp/`.
- System, OS, Windows API, and other platform-specific implementation code MUST exist only in `platform*.*` files. Other modules use platform abstractions.
- Keep `// Purpose:` comments accurate in modified `src/*.h` and `src/*.cpp` files.
- Preserve [product design](docs/design.md), especially targeting, recovery, navigation restoration, and preview-before-run.

## Mandatory pre-flight validation

Before generating or modifying C++ code, an AI agent MUST declare a change class and output exactly the fields that class requires. Placeholders are invalid. Do not output both schemas.

**Change class `User-visible behavior`** — the change alters what a user can observe: scope, contents, target, effect, presentation, or recovery. Populate from the closed [design ontology](docs/design.md#user-mental-model).

```json
{
	"Change class": "User-visible behavior",
	"Scope": "Indexed collection | Folder | Recursive folder | External folder | Search | All scopes",
	"Contents": "What visible or derived contents the change reads or changes",
	"Target": "Focused item | Singular displayed item | Complete visibly selected set | Visible items",
	"Effect": "The exact behavior, affected count or class, destinations/collisions when applicable, retained originals, and recovery"
}
```

`All scopes` is earned, not default: use it only when the behavior is provably identical in every concrete scope, and say why in the `Contents` field. If the change belongs to one concrete scope, name that scope.

**Change class `Internal`** — the change alters no observable behavior: rendering backends, threading and ownership, indexing internals, codecs, build configuration, tests, or refactors. Naming a user scope or target here is a gate violation.

```json
{
	"Change class": "Internal",
	"Component": "The owning src/ prefix and the specific files or subsystem",
	"Invariant": "The correctness, ownership, or performance property the change must preserve or restore",
	"Effect": "The exact implementation change and its blast radius",
	"Observable behavior": "Unchanged, plus the check that proves it"
}
```

If an `Internal` change turns out to shift observable behavior, stop and restate it as `User-visible behavior` before continuing.

Before editing, explicitly confirm both [implementation boundaries](docs/implementation.md):

1. UI-thread execution remains disjoint from I/O, decoding, database access, and network use.
2. Paint invalidation remains disjoint from Source work; no Paint-layer function contains or calls SQLite access or file scanning.

This is a gate. If a `User-visible behavior` change cannot map unambiguously to the schema and enums, HALT and ask the user; never invent, alias, or infer a state, and never reclassify it as `Internal` to avoid the question.

## Iteration and loop protocol

1. After each substantive edit, run the narrowest relevant check before editing again: prefer `.\exe\diffractor64-d.exe /test:*module* | Out-Host`, otherwise a focused compile/lint.
2. Cap one failing check/hypothesis at three correction attempts. After the third failure, HALT and report the hypothesis, attempts, exact failure, and request guidance; cosmetic changes do not reset the count.
3. If an edit breaks unrelated modules, remove only the agent's causal changes before changing approach. Preserve user work; never use blanket `git checkout`, `git reset`, or file replacement.
4. Once the owner, falsifiable hypothesis, and discriminating check are known, edit and validate; stop broad exploration.

## Strict code anti-patterns

- **Threads:** no application `std::thread`; use `async_strategy` or an existing queue. Direct threads are limited to platform queue/thread implementation and focused concurrency tests.
- **Locks:** no I/O, SQLite, decoding, callbacks, sorting, or large/unbounded allocations under an index lock.
- **UI thread:** no filesystem, database, decoding, indexing, hashing, querying, thumbnail, map, or network work.
- **Invalidation:** Paint functions never access SQLite or scan files, directly or indirectly; request Source work.
- **Vendors:** never edit `third-party/`; use owned wrappers or build configuration.
- **Cache keys:** a key must carry every input the cached value depends on. Invalidation is only as good as the identity it is keyed on, and a key that omits an input silently serves one caller's value to another.
- **Animation:** alpha fades are disabled in CPU software rendering mode (`ui::animations_enabled`, mirroring `setting.can_animate`); never bypass the gate or animate alpha outside `ui::animate_alpha`.
- **Exceptions:** every `catch` propagates, logs, returns a bounded error, or implements a documented bounded fallback; no unexplained swallowing.

## Thread ownership and result publication

- Every mutable object has one owning execution context unless its qualified type is documented as a synchronized type in [implementation](docs/implementation.md).
- Views, controls, hosts, controllers, and `view_element` instances are UI-thread-owned. Their mutable state is read and written only on the UI thread.
- Background callbacks must not capture `this`, raw pointers, references, or owning `shared_ptr`s to UI-owned objects. A `weak_ptr` may cross a worker queue only as an opaque lifetime token: do not lock or dereference it until execution has returned to `queue_ui`.
- Workers consume moved values or immutable snapshots and produce detached result values. Apply results on the owning context in one bounded batch; do not publish partially initialized objects.
- UI publication checks both lifetime and currency. A successful `weak_ptr::lock()` does not replace a generation, request-ID, scope, path, or current-identity check.
- Do not use `const_pointer_cast` to publish worker results or make a mutable object appear safe to share.
- `df::item_element` is entirely UI-owned, including its immutable metadata snapshot and playback position. Never read or mutate an item on a worker. Queue detached values or immutable requests, then publish through path/generation-checked UI results; do not reintroduce per-field atomics.
- `df::index_file_item` remains a synchronized index record. Its atomic metadata pointer publishes complete snapshots only: clone an existing payload when needed, finish every mutation, then replace the pointer. Never mutate a published metadata payload in place or publish a new payload before initialization is complete.
- Do not add an atomic, mutex, event, lock, or synchronized mutable field to a new type unless the implementation documentation names the owning threads, protected invariant, and reason single-context ownership is insufficient.

## Post-flight checklist

Before completion, verify every applicable item:

- [ ] Narrowest relevant tests pass; run `.\dd.ps1 test` for shared behavior or release completion.
- [ ] Modified source retains accurate `// Purpose:` comments.
- [ ] The diff is task-only and preserves user work.
- [ ] The final response lists checks, outcomes, and unavailable/skipped checks accurately.