# Diffractor Collections

This document owns the collection: what it is, how membership is decided and changed, what membership earns, and where the collection's edge is reported to the user. It states durable behavior, not class structure. Cross-cutting interaction rules live in [design.md](design.md); indexing architecture, threading, and data flow live in [implementation.md](implementation.md).

Both [primary design drivers](design.md#primary-design-drivers) apply without exception. The collection is the feature most able to violate them: it is the largest thing the application knows, it is built by walking storage the user controls, and it is the source of nearly every aggregate the user navigates by.

## 1. Why a collection exists

Media tools usually pick one of two shapes:

- A **viewer** opens whatever it is pointed at. It is fast and honest but knows nothing, so every question larger than one file — how many, where, when, do I already have this — is unanswerable.
- A **library** takes custody. It imports files into a managed store, and from then on the application's database is the truth and the filesystem is an implementation detail. It answers large questions, but it owns the user's files, is expensive to leave, and disagrees with the folder tree the user already maintains.

Diffractor is neither. It is a viewer that also knows the answers, because it **indexes folders the user already keeps, in place**. The collection is the mechanism: a declaration of which folders count, from which everything else — search, aggregates, duplicates, presence, sync — is derived.

This is the product's central bet. Everything below exists to keep it true.

## 2. What a collection is

**A collection is a declaration, not an accumulation.** It is a short list of included root folders plus a list of exclusions. It is not a record of per-file decisions, and nothing a user does while browsing quietly adds to it.

Three consequences follow, and they are the whole theory:

1. **Membership belongs to the folder, not to the file.** A file is in the collection because it sits under an included root that is not excluded. Moving a file into such a folder joins the collection; moving it out leaves. There is no "add to library" step and no per-file membership state that could disagree with where the file actually is. The filesystem remains the user's organizing tool.

   Import is not an exception. Import is a file operation — it copies or moves files into a folder structure built from metadata — and never a membership operation. Its results join the collection when, and only when, the destination folder is already a member, and importing to a folder outside the collection adds nothing to the collection. Membership still follows the folder; Import merely decides which folder a file ends up in.
2. **The declaration is small enough to read and reverse.** A user can see the whole collection definition at once, understand why any folder is or is not in it, and change it by editing a line. A definition that could only be inspected by querying it is not a mental model.
3. **Everything derived is rebuildable.** The index is a cache over files that remain the authority. Deleting it costs time, not information. This is what makes membership changes safe: they change what is *known*, never what *exists*.

## 3. Deciding membership

### 3.1 Includes

Roots come from two places, and they combine into one set:

- **Well-known folders** offered as switches: Pictures, Videos, Music, Dropbox photos, and the OneDrive Pictures, Video, and Music folders. A switch for a folder the system does not have contributes nothing rather than failing.
- **Other folders**, one entry per line, for anything else.

An include is recursive: naming a folder names its subtree. Naming both a folder and its descendant is not an error and does not double-count.

A "More folders" line that is not an existing path is not discarded, because the most common reason a path does not exist is that the storage is not attached right now. Such a line is resolved as a **device or volume label**, or as a **server name**. Labels matter for exactly the case where paths are worst: removable and network storage whose drive letter changes between sessions. A collection defined by label survives re-plugging; one defined by `E:\` does not.

### 3.2 Excludes

An entry prefixed with `-` excludes rather than includes, in three forms:

| Form | Example | Meaning |
|---|---|---|
| Full path | `-c:\photos\secret` | That folder and its subtree. |
| Folder name | `-secret` | Any folder with that name, anywhere in the collection. |
| Wildcard | `-proxy*` | Any folder whose name matches, anywhere in the collection. |

Excludes are evaluated during the walk, so an excluded folder is never entered and its subtree never joins. Name and wildcard excludes are how a user removes a recurring by-product folder — caches, proxies, hidden dot-folders — without listing every instance.

A full-path exclude is recorded **whether or not the path currently exists**. An exclude that silently disappeared while a drive was detached would let the next indexing pass pull in exactly the folder the user removed, so an exclude is a standing instruction rather than an observation.

### 3.3 The resulting rule

A folder is in the collection when it is an included root, or is reached from one by recursive walk without crossing an exclude. A file is in the collection when its folder is. Nothing else confers membership, and membership is never inferred from having viewed, searched, edited, or imported something.

The walk is bounded. A collection definition that would enumerate an unreasonable number of folders stops rather than growing without limit, because an unbounded walk trades the first driver for nothing the user asked for.

### 3.4 Folders that are known but not members

Browsing an external folder makes Diffractor aware of it, and that awareness is kept so navigation and completion stay useful. **Awareness is not membership.** A folder that has merely been visited contributes nothing to collection aggregates, is not a duplicate-prediction candidate, and does not make its files report as being in the collection. The only way into the collection is the declaration.

This separation is what keeps the collection meaningful. If browsing joined, the collection would drift into "everywhere I have ever looked", which answers no question at all.

## 4. Changing a collection

Changing the declaration changes what is known. It never moves, copies, renames, deletes, or writes to a file. Adding a folder makes its contents findable; removing one makes them unfindable from collection scope while leaving them exactly where they are and still reachable by browsing.

Because the change is a re-derivation rather than a migration, it has no failure mode that loses user data and needs no confirmation on those grounds. It does have a cost — a new subtree must be walked and scanned — so it is progressive rather than modal: browsing stays usable while the change takes effect, and results improve as it does.

Removing a root does not delete a folder that was named twice by different means, and re-adding a previously indexed folder is cheap, because cached knowledge for it is reconciled against the filesystem rather than rebuilt from nothing.

## 5. What membership earns

Membership is not a label; it is the precondition for every whole-collection capability. Each of these is described in full by its owning document; what matters here is that each is defined over collection members and nothing else.

| Capability | What membership provides |
|---|---|
| Search | Terms resolve against the index across every member folder at once, instead of against one folder's directory listing. |
| Navigation aggregates | Dates, tags, ratings, labels, locations, media types, folders, and drives are counted over members, and the sidebar navigates by those counts. |
| Duplicate prediction | Candidate grouping by name, date, size, and CRC runs over members only, so a duplicate claim always has a defined population. |
| [Presence](design.md#collection-presence) | An outside file can be compared against members to answer "do I already have this?". |
| Import and Sync | Both name a collection side, so their direction and effect are stated against a defined set rather than against an ad-hoc folder pair. |
| Totals | "The collection contains N items" is meaningful because N has a definition the user can inspect. |

The corollary is that a scope outside the collection cannot honestly offer these. When the current scope is not in the collection, that fact is reported plainly — the items shown are not from the collection. Where the report explains an empty or partial result, it also carries the action that would widen the collection, so the explanation and its remedy are the same surface. Silently returning empty or partial aggregates for a non-member scope would read as a broken product rather than as a scope distinction.

## 6. The collection's edge

The collection has an inside and an outside, and the interesting behavior is at the boundary. Two separate reports live there and must not be confused:

- **Membership** is a fact about a folder: this file is in the collection, or it is not. It is cheap, exact, and never probabilistic.
- **[Presence](design.md#collection-presence)** is an assessment about content: for a file outside, are there likely copies inside, and are they newer or older? It is evidence-based and states its confidence as *possible*.

Membership never implies uniqueness — a file being in the collection says nothing about whether three copies of it are also in the collection. Presence never implies a recommendation — it supports an import decision and is not a deletion verdict, does not choose a keeper, and never enlarges a command's target.

The honesty rule at this edge is absolute: **an incomplete collection may not publish an absence.** While indexing is incomplete the answer is "checking", not "not found". A confident wrong negative here is worse than a slow answer, because it is the exact input to a decision to delete or re-import.

## 7. Truth, staleness, and honesty

The collection is a cache over storage the user can change behind Diffractor's back. Three rules keep the cache from becoming a second, competing truth:

1. **Files are authoritative; the index is derived.** Where they disagree, the file wins and the index is corrected. Metadata written to a file or sidecar is the record; the index merely remembers it.
2. **Knowledge is retained when storage is not reachable.** A member folder on a detached or offline drive stays a member, and what is known about it stays known and searchable, marked as offline rather than deleted. Forgetting a collection because a drive was unplugged would make the collection less trustworthy than the folder tree it describes.
3. **Incompleteness is disclosed, not hidden.** Indexing improves results progressively while browsing remains usable, and a surface whose results may be incomplete says so. Completed work replaces provisional results without requiring navigation or restart.

## 8. What a collection is not

Stated explicitly because each has been a plausible-sounding wrong turn:

- **Not an album or a curated set.** Membership carries no judgement about quality, order, or intent. Curation is expressed by folders, tags, ratings, labels, and saved searches.
- **Not custody.** Diffractor does not own, relocate, or restructure member files, and leaving Diffractor costs nothing because there is nothing to export.
- **Not a mode.** The collection is one [scope](design.md#user-mental-model) among several. Browsing outside it is a first-class activity, not a degraded state.
- **Not per-file state.** There is no "in collection" flag on a file to get out of sync with where the file is.
- **Not a uniqueness guarantee, and not a deletion authority.** See §6.
- **Not a remote service.** The collection is built and held locally from storage the user controls; any network feature discloses its own trigger, data, recipient, and purpose independently.

## 9. Design review

- The collection definition is fully visible, and any folder's membership is explainable from it.
- Nothing joins the collection as a side effect of browsing, viewing, searching, or editing.
- Changing the definition changes only what is known, and says so.
- A result limited by the collection's edge says so and offers the action that widens it.
- Every whole-collection number states the population it was computed over.
- Membership and presence are reported as different kinds of claim, and neither is presented as a deletion recommendation.
- Offline members remain members; incomplete indexing never publishes an absence.

Presence wording and confidence values belong to [design.md](design.md#collection-presence). Place resolution over collection items belongs to [locations.md](locations.md). Index, summary, and scanning architecture belongs to [implementation.md](implementation.md#index-search-and-database). How a metadata write reaches disk belongs to [file-io.md](file-io.md). GitHub issues own design gaps and planned changes.
