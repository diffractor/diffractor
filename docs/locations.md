# Diffractor Locations

This document owns Diffractor's location concepts: how a place is identified and named, how location search works, how distance participates in a query, how the map produces searches, and how location and time combine into visit nodes. It states durable behavior, not class structure. Cross-cutting rules that are not specific to locations live in [design.md](design.md); architecture, threading, and data-flow rules live in [implementation.md](implementation.md).

Both [primary design drivers](design.md#primary-design-drivers) apply without exception. Location work reads a large gazetteer and correlates it with the whole result set, so every feature here must stay off the UI thread and must state its scope, contents, target, and effect as clearly as any other command.

## 1. Current behavior (baseline)

Recorded so that changes below are deltas, not rewrites.

**Gazetteer.** `location-places.txt` is a tab-separated GeoNames extract loaded by `location_cache`. Columns are `id, latitude, longitude, stateCode, countryCode, population, langmask, name[, localized names...]` (`Cols::GeoNamesCols` in `src/model_locations.cpp`). The name column is followed by a variable number of localized names, one per set bit in `langmask`, in bit order. `location-countries.txt` and `location-states.txt` supply country names, country alternate names, and admin1 names. The generator is `tools/generate_locations.py`; the shipped file is Git LFS content and is packaged by `dd.ps1` and `installer/diff.nsi`.

**What the generator refuses to ship.** GeoNames records the same place more than once, and a duplicate is not a harmless extra row: it splits one place's photos across two identities, and where two records land on one written coordinate the k-d tree picks between them arbitrarily. The generator therefore collapses a record into another only when the two cannot describe different places — the same name in one country within 10 km, the same non-trivial population in one country within 10 km carried by two spellings of one name, or one written coordinate where one record's population dwarfs the other's, which is a district and its city. Every collapsed spelling survives as an alternate name, so nothing becomes unsearchable. Names differing only by accent or case are dropped because search folds both; a place keeps at most six untagged alternate names, since alternate names are half the file and each one is another way for a query to land on the wrong place. A parenthetical suffix is moved out of the display name into the alternates, and an admin1 code of `00` — GeoNames for "no region", which has no name to render — is blanked. Excluding whole feature classes was tried and rejected: gating out sections and localities (`PPLX`/`PPLL`) removes thousands of neighbourhoods that carry a genuine sub-population and label a photo better than the enclosing city, and measurement showed almost none of them outrank a real town.

**Lookup.** `location_cache` builds a KD-tree over coordinates plus an n-gram index over every name column. It supports `find_closest`, `find_by_id`, `find_largest` (highest population inside a lat/long box), `find_country`, and `auto_complete`. Display names are localized per UI language; the country **grouping key** deliberately stays canonical English because it doubles as a search term.

**Item location data.** An item may carry `location_place`, `location_state`, `location_country` text and/or a `coordinate`. Any of these may be absent. When place text is missing but coordinates exist, search reverse-geocodes on demand.

**Search.** Three overlapping vocabularies exist:

- `loc:` — either `loc:<±lat><±lon><±km>` (parsed by `split_location`, which requires `+`/`-` prefixes and treats the third value as kilometres), or `loc:"<text>"`. The text form matches by **case-insensitive equality** against the item's place, state, country, or the composed `place, state, country` string.
- `area:` — a map heat-map cell rectangle (`location_cell`, `location_cell_span`) matched against `location_heat_map::calc_map_loc(coordinate)`.
- `place:` (alias `city:`), `state:`, `country:` (alias `countries:`), `latitude:`/`x:`, `longitude:`/`y:` — ordinary **property** terms registered by `prop::from_prefix`. They match the item's stored metadata field only, participate in `with:`/`without:` presence queries and comparisons, and perform no gazetteer resolution and no reverse geocoding.

**Grouping.** `group_by::location` keys groups on country/state/place text; the group header links each part to a `prop::location_country` / `location_state` / `location_place` search.

**Map.** The sidebar map paints a heat map and folds cells into `map_location_area` buckets. An area resolves its label with `find_largest` over the photo bounds and, on click, opens an `area:` search **and forces `group_by::location`**.

### Baseline defects this document resolves

1. A bare name is ambiguous ("London" is a place in the UK and in Canada) and there is no way to express which one; the composed-string form is undiscoverable and locale-fragile.
2. Distance is available only for raw coordinates, only in kilometres, and only through a syntax that requires signed numbers.
3. `area:` is an internal geometry concept leaking into the user's address bar; users reason about "near a place", not about heat-map cells.
4. Clicking the map silently changes the user's grouping, violating [the rule that presentation is user-owned](design.md#navigation-and-search).
5. The items summary affordance conflates totals with grouping and sorting.
6. Location text matching runs a reverse geocode per item per term with no memoization.
7. A location group header links to `place:` / `state:` / `country:` property terms, so clicking the header of a group formed by reverse geocoding returns fewer items than the group contained. A header must reproduce its own group.
8. `loc:` currently matches place **or** state **or** country by equality, which silently duplicates the property terms while behaving differently from them. Two vocabularies that look alike and match differently are worse than either alone.
9. **The most guessable spelling is the most misleading one.** `place:London` is a stored-field query, and most photos carry coordinates but no IPTC/XMP place field. A user with hundreds of London photos types the obvious term and gets nothing, which reads as a broken product rather than as a subtle distinction between a field and a location.
10. **Attribution is unbounded.** `find_closest` returns the nearest gazetteer record at any distance and `find_country` takes that record's country, so a photo taken at sea or from a plane is confidently labelled with a city it is nowhere near. There is no vocabulary for "away from any place".

Defects 1 through 10 are resolved. Defect 6 is closed by three memos rather than one: reverse-geocoded places are memoized per grouping pass and per item panel, identity matching resolves a term's name once per search rather than once per item, and the matcher's own attribution is memoized per coordinate cell as §3.3 requires. The per-place counts of §3.4 remain **deferred to a future release**; they need index-side aggregation rather than a memo.

## 2. Identifying a place

### 2.1 Qualification level

Every gazetteer record gains a `flags` column. Bits 0–1 hold the **qualification level**, the smallest name form that uniquely identifies the place to a user:

| Level | Bits | Display form | Example |
|---|---|---|---|
| 0 | `00` | Name | `Reykjavík` |
| 1 | `01` | Name, country | `London, United Kingdom` · `London, Canada` |
| 2 | `10` | Name, region, country | `Springfield, Illinois, United States` |
| — | `11` | Reserved. A loader that sees it treats the record as level 2. | |

Bit 2 marks an **extent feature**: a record matched by its bounding box rather than by proximity to a point, and which therefore has no meaningful centroid. Water bodies (§2.6) are extent features. Extent features are excluded from `find_closest`, `find_largest`, and radius search. Bits 3–31 are reserved and MUST be written as zero and ignored on read.

The column is fixed-width and MUST be inserted **before** the name column, because the name column is followed by a variable-length run of localized names. The resulting column order is `id, latitude, longitude, stateCode, countryCode, population, langmask, flags, name[, localized...]`. Loader and generator change together; the shipped `location-places.txt` must be regenerated in the same change. A file without the column is a stale file, not a supported variant: the loader treats a record with fewer than the expected fixed columns as level 2 and logs once, so a mismatched install degrades to over-qualified names rather than wrong names.

### 2.2 How the generator assigns levels

Computed over the set of records actually emitted, using the same normalization search uses (case-insensitive, accent- and punctuation-folded):

1. Group records by normalized default name. A name held by exactly one record is level 0.
2. Otherwise group the collision set by country. A name unique within its country is level 1.
3. Otherwise the record is level 2.
4. If a name is not unique even at level 2, the highest-population record keeps level 2 and is the **canonical** record for that name triple; the others also stay at level 2 and are separated only by identity, never by label. Grouping keeps them apart; a text query that resolves ambiguously prefers the canonical record and says so in autocomplete.

Levels are computed from **default** names only. Localized names reuse the level of their record, so `München, Deutschland` and `Munich, Germany` qualify identically. This keeps the level stable across UI languages, which matters because it feeds a search term that may be saved.

Determinism is required: the generator sorts before assigning so regeneration from the same GeoNames snapshot produces byte-identical output.

### 2.3 Qualified display name

`qualified_name(location)` composes the level's parts with `", "`, using the current display language for each part and the country's **short common** form (`United Kingdom`, not `United Kingdom of Great Britain and Northern Ireland`). It is the single function used by group headers, map bubbles, timeline nodes, autocomplete, and canonical term formatting. Nothing else composes place labels.

Where the level would repeat a part (a city-state, or a place whose name equals its region) the repeated part is dropped, and the level is treated as satisfied.

### 2.4 Places that come only from file metadata

**Deferred to a future release.** The stored-text-wins precedence of §2.5 step 1 is implemented, so such an item already displays and groups by its stored text and is never overwritten by a gazetteer guess. What is deferred is giving a levelless stored place its own identity in grouping and completion, which needs the item-count work of §3.4 to be worth anything.

An item may carry place text with no coordinates and no gazetteer match. Such a place has no level. It displays exactly as stored, groups under its stored text, and is never silently promoted to a gazetteer place. When both a gazetteer match and stored text exist, stored text wins for display and gazetteer identity wins for geometry — the same precedence search already uses.

### 2.5 Coordinates with no nearby place

`find_closest` is currently unbounded: it returns the nearest gazetteer record at any distance, and `find_country` derives the country from that record. A photo taken mid-Atlantic is therefore attributed today to a real city hundreds of kilometres away, and a photo over the Sahara is attributed to a village that the photographer never saw. The label is stated with the same confidence as a correct one, which is the worst possible failure: a wrong fact presented as a fact.

Attribution becomes **bounded**. A place may label an item only within an attribution radius scaled to its significance, because a large city is a reasonable answer from far away and a hamlet is not:

| Population | Attribution radius |
|---|---|
| ≥ 1,000,000 | 100 km |
| ≥ 100,000 | 50 km |
| ≥ 10,000 | 25 km |
| ≥ 1,000 | 15 km |
| below 1,000 or unknown | 10 km |

Resolution then follows one ladder, first match wins:

1. **Stored text.** Unchanged precedence.
2. **At a place** — a gazetteer place within its attribution radius. Displays as its qualified name.
3. **Near a place** — a gazetteer place within three times its attribution radius, on land. Displays as `Near Aviemore, Scotland, United Kingdom` and **groups under that place**, so rural photography is not shattered into singletons. It still matches `place:Aviemore`; the `Near` prefix is display honesty, not a different identity.
4. **A water body** — §2.6. Deferred; this step currently never matches.
5. **Remote** — nothing within three radii. Displays as the country when one can be determined, otherwise `Remote area`. Until step 4 exists this also covers open water, which is why it reports no country there.

An item that reaches steps 3–5 is still located. It never appears in `without:location`, which means *no coordinates and no text* and nothing else.

### 2.6 Water bodies, offshore, and in flight

**Deferred, and step 4 of the §2.5 ladder is currently unreachable.** This section originally specified a `location-waters.txt` of roughly eighty bounding boxes for oceans, seas, gulfs, bays, straits, and channels, so that a mid-Adriatic photo would read `Adriatic Sea`. It is not implemented, and it is recorded here as deferred rather than pending because the reason is a data problem rather than a scheduling one.

Naming a water body requires two things Diffractor does not have:

- **A land/water test.** Without one, "no populated place within 300 km" cannot distinguish the mid-Atlantic from the interior of the Sahara, the Amazon, Greenland, or the Australian outback. Calling any of those `Offshore` would be a confident wrong fact — exactly the defect §2.5 exists to remove.
- **Extents.** GeoNames, the gazetteer this project ships, publishes marine *names and points* (feature class `H`, plus `no-country.zip` for features belonging to no country) but no bounding boxes for them; its only boundary export, `shapes_simplified_low`, covers countries. Bounding boxes exist only behind its rate-limited web service, which cannot be a build step. Hand-authoring eighty boxes would be unverifiable geometry maintained by nobody.

So there is no `Offshore` class, no `@offshore` or `@sea` term, and no `location-waters.txt` in the packaging lists. A coordinate over open water resolves through §2.5 step 5 as `Remote area` with no country, which is true, and §2.7 gives it the descriptor that actually answers the question: `Remote area · 410 km NW of Lisbon, Portugal`.

The cheapest honest way to revive this is a low-resolution global land mask — a 0.5° bitmask is about 32 KB — which would make `Offshore` a truthful class on its own, before any named body is added. Named bodies should follow only with a real motivating example, not eighty speculative rows.

### 2.7 The bearing descriptor

Every item resolved at step 3, 4, or 5 also gets a secondary descriptor: `410 km NW of Lisbon, Portugal`, computed from the nearest place regardless of radius. It appears in the item information panel, in map and timeline bubbles, and in the location group header's subtitle.

It is deliberately **never a group key and never a search term**. Per-item bearings would shatter grouping into singletons and would fill the address box with values no user would type. It exists to answer "where *was* that?", which for a photo taken from a plane over open water is a better answer than any single place name.

The compass is eight points, not sixteen. A bearing taken to the nearest gazetteer record is not precise enough to justify `NNW`, and the extra points would read as confidence the underlying answer does not have.

Resolution reads the gazetteer file, so it runs on the location worker and is published back to the UI, which only ever reads an answer it already holds. A display-language change clears the memo and drops any result still in flight.

### 2.8 Deferred altitude classes

Airborne, high-altitude, and underwater classification is deferred beyond 1.27. Diffractor continues to extract and index GPS altitude and speed, but this release does not derive a class, display an altitude claim, segment flights, or accept `@flying`, `@altitude`, or `@underwater` search terms.

The earlier threshold design was not reliable enough to ship. In particular, EXIF `GPSAltitudeRef=1` means **below sea level**, not underwater, so a negative altitude alone would confidently misclassify valid land photography. A future underwater feature needs independent evidence of submersion. Airborne and high-altitude classification likewise need validation against real camera and phone metadata, including missing values, stale coordinates, and metadata rewritten by location editors, before thresholds become product behavior.

### 2.9 Inferred locations are visibly inferred

Diffractor writes metadata to files, so a user who sees `Near Aviemore` on a photo they never tagged has an entirely reasonable fear: that the application has edited their file, or is about to. Everything in §2.5 through §2.8 is derived at read time and held only in the index. **Nothing in this document writes a location into a file.**

Being true is not enough; it has to be visible:

- The item information panel separates **stored** location fields from **derived** ones. A derived value is labelled as derived and names its basis — the gazetteer record and the distance used — so the user can see a lookup rather than a change to their file.
- A derived value never populates an editing affordance as though it were the field's current content. Where a location field is editable, an empty field reads as empty. Scanning does not write a derived place into the item's stored location fields either: a non-empty `location_place`, `location_state`, or `location_country` in the index always came from the file, which is what lets the matcher treat stored text as the user's own answer and lets `without:place` mean what §3.6 says it means.
- That invariant has to hold for indexes an earlier build wrote, and those rows are irreducibly ambiguous — a derived `Prague` is indistinguishable from an IPTC `Prague`. So the index carries a cached-metadata version, and opening an index stamped with an older one drops the cached metadata and clears the scan state, letting the next scan restore each item's stored fields from the file itself. Only the metadata is dropped: thumbnails, cover art, hashes, playback positions, and import history survive, so the upgrade costs a metadata rescan rather than a full re-index.
- Writing a derived location into a file happens only through an explicit, previewed metadata command against the user's [target](design.md#user-mental-model), never as a side effect of viewing, searching, grouping, or indexing.
- Even then, only the coordinate is written. The Add location task writes the map centre and nothing else: place, state, and country are derived from that coordinate at read time by anything that cares, so writing them would freeze one gazetteer's answer — and one moment's §2.5 attribution — into the user's file forever. The screen still names and qualifies the centre, and states its §2.7 bearing when the answer is not `At`; that description stays a description.
- Add location can hide items that already carry a coordinate from its selector strip, so a long list can be walked down to nothing left to place. The strip is a navigator: hiding an item removes it from what the user can reach, not from what a run would write.
- Group headers, map bubbles, and timeline nodes may mix stored and derived items freely. The distinction matters at the item, which is where "what is actually in my file?" is asked.

This is also what makes the §3.5 widening safe to ship: `place:London` matching a GPS-only photo is a broader **search**, not a broader claim about that photo's contents.

## 3. Location search

### 3.1 Grammar

```
loc: <place-query>
place-query := <name> [ "," <region> ] [ "," <country> ] [ "," <distance> ]
             | <lat> "," <lon> [ "," <distance> ] # canonical coordinate form
             | <±lat><±lon>[<±km>]                 # retained legacy form
distance    := <number> ( "m" | "km" | "mi" | "mile" | "miles" )
@<class>                                  # built-in class, closed set, no argument
```

`near:` is a synonym of `loc:`. `place:`, `city:`, `state:`, `country:`, and `countries:` take the same place-query and constrain it to one level; the `@` classes are enumerated in §3.5.

Quoting follows normal term rules; `loc:"London, United Kingdom, 10km"` and `loc:London,UK,10km` are the same term. Country accepts the canonical name, any alternate name, and ISO codes (`UK`, `GB`, `USA`) via the existing country alias normalization. Distance is recognized only in the final position and only when it parses completely as a number plus a unit; a place literally named like a distance therefore still resolves as a place when quoted.

Quotes are never required. After a location scope the parser keeps absorbing the following words into the place query, and stops at the first word that is not recognizably part of a place. A word is absorbed when it is a distance (`5km`), an ISO country code or well-known alias (`UK`, `GB`, `USA`), or the user separated it with a comma. So `loc: London UK 5km`, `loc: London, UK, 5km`, and `loc:"London, UK, 5km"` are the same term, `loc: London, Ontario, 5km` works because the commas state the intent, and `loc: London sunset` stays a London location plus a separate `sunset` text term. A multi-word country with no comma (`loc: London United Kingdom`) still needs a comma or quotes, because the parser only recognizes country *codes* without them - the country name table belongs to the gazetteer, which is not loaded on the UI thread where parsing runs.

Canonical formatting (what the address box shows after commit, and what a saved search stores) is `loc:<qualified name>[, <distance>]` for a resolved place. A coordinate fallback is `loc:<lat>,<lon>[,<distance>]`; it uses bounded decimal precision and an explicit `m` or `km` unit, never scientific notation. The user's typed spelling is preserved while editing, per [design.md](design.md#navigation-and-search).

### 3.2 Matching

A location term matches an item when either rule holds:

- **Identity match** (no distance given). The item's place, region, or country — stored text first, reverse-geocoded gazetteer values otherwise — equals the query at the query's specificity. A bare `loc:London` therefore matches every London. `loc:"London, Canada"` matches only that place's identity. A region-only or country-only query matches everything inside it.
- **Radius match** (distance given). The item has coordinates and lies within the radius of the resolved place's position, by great-circle distance. Items without coordinates never satisfy a radius match, even when their stored text names the place; the results footer states how many items were excluded for having no coordinates so the omission is never silent.

An identity match compares **names as parts, not as one string**, because §2.3 labels a record at its own qualification level while the index may know the item at another. `loc:"London, United Kingdom"` — the term a completion commits — has to find an item the gazetteer names London, England, United Kingdom. Every comma-separated part of the query must name one of the item's three fields, which is what still keeps `loc:"London, Ontario"` off a photo taken in England.

An identity match also **reaches down**, because the record nearest a coordinate is frequently not a settlement at all. The gazetteer carries unpopulated sub-city records — a photo in central London is named `Bread Street` — so comparing the derived name alone would answer `loc:London` with nothing, which is the §3.5 failure this document exists to remove. A more significant place therefore also matches an item when it stands within the §2.5 reach of the record that actually named that item: London (reach 100 km) answers for a photo named by Bread Street (reach 10 km) 2 km away, and does **not** answer for a photo named by Windsor (reach 25 km) 35 km away. A town near a metropolis keeps its own identity; a street inside one does not have a competing one.

A term with a distance and an ambiguous name resolves the name first (canonical record, or the completion the user committed) and reports the resolved place in the address box, so the searched centre is always visible.

Negation, Boolean composition, and the `search_result_type::match_location` highlight behave as they do today.

### 3.3 Resolution and cost

Name resolution happens once per search, not once per item: the term holds the resolved place id, position, and radius after parsing. Reverse geocoding for identity matches is memoized per search generation keyed on the rounded coordinate cell, so a result set with clustered coordinates performs a bounded number of KD-tree lookups. All of it runs on the search worker; none of it may run on the UI thread ([implementation.md](implementation.md#index-search-and-database)).

### 3.4 Autocomplete

**Deferred to a future release.** Completions already offer qualified names, but the collection item count beside each candidate needs a per-place count over the index that nothing currently computes. Shipping the list without the counts would not solve the problem the section exists to solve — telling the two Londons apart — so the counts and the ordering that depends on them wait together.

Location completions offer qualified names. When a typed prefix collides, the completion list shows every colliding place with its qualified name and a population or item-count hint, ordered by relevance to the collection first and population second — the user picks the London they mean rather than discovering later that they got the wrong one. Committing a completion writes the canonical term.

### 3.5 One location vocabulary

`place:`, `city:`, `state:`, `country:`, `countries:`, `loc:`, and `near:` all **resolve locations**. None of them is a raw metadata-field query.

This is the correction of the most damaging baseline behavior. `place:` is currently a property term over the stored IPTC/XMP place field, and most photos never carry that field — they carry coordinates. A user who types the obvious thing gets zero results for a place they have hundreds of photos of, and concludes the product is broken. A search vocabulary whose most guessable spelling is also its most misleading one is not defensible under either [primary design driver](design.md#primary-design-drivers).

The resolved vocabulary:

| Term | Meaning | Distance |
|---|---|---|
| `loc:` / `near:` | The item is at the resolved place, at any level | Optional |
| `place:` / `city:` | The item is at the resolved **place** | Optional |
| `state:` | The item is in the resolved **region** | No |
| `country:` / `countries:` | The item is in the resolved **country** | No |

Every one of them uses the same resolution rule as §3.2: stored text when present, reverse-geocoded gazetteer identity otherwise. `place:London` therefore matches a GPS-tagged photo with no place field, which is what the user meant. The level-constrained spellings remain worth having because they disambiguate — `country:Georgia` and `state:Georgia` are different questions, and `place:` is the natural thing to type.

`loc:` is the canonical form. The level-constrained spellings parse to the same term with a fixed level and are preserved in the address box as typed; anything the application generates writes `loc:`.

**Built-in classes use `@`.** A few locations are not gazetteer names at all but classifications Diffractor derives on its own. These form a **closed** set — enumerable, argument-free, and unable to take a distance — which is exactly the shape `@` already carries in this grammar for `@photo`, `@video`, `@audio`, and `@duplicates`.

| Term | Matches | Synonyms |
|---|---|---|
| `@offshore` | Any water body (§2.6) | `@sea` — deferred with §2.6; not accepted |
| `@remote` | Remote land (§2.5) | — |

The division is structural rather than cosmetic: `loc:` takes an open set of names loaded from a data file and accepts a radius, while `@` takes a closed set of built-in classifications and accepts nothing. Typing `@` on its own lists everything the application can classify without the user having tagged anything, grouped by kind, so the entire set is discoverable in one keystroke. The deferred `@offshore` term is not accepted.

### 3.6 Asking about the stored field

Metadata integrity work still needs the raw field, so it gets an explicit, unambiguous grammar rather than an overloaded prefix:

- `with:place`, `without:place`, `with:country`, `without:country` — the **stored field** is present or absent. This is the existing presence grammar; its wording already says it is about a property, and it is the only place a raw location field is addressable.
- `without:location` — no location can be determined at all: no stored text and no coordinates. This is the query a user actually wants when asking "what have I got that isn't placed?", and it is distinct from `without:place`.

`latitude:` and `longitude:` are unchanged numeric property terms. They are genuinely field-based, but the field is coordinates, which most photos do have, so they do not carry the empty-result trap.

### 3.7 A location query that finds nothing must explain itself

Even correct behavior returns nothing sometimes, and that moment is where trust is won or lost. When a location term yields no results, the No Results state names the resolved centre, not just the typed text, and offers the next action:

- `Nothing within 2 km of London, United Kingdom.` with **Widen to 10 km** — the single most likely fix, one click.
- When the name resolved to an unintended place, **Did you mean London, Canada?** listing the other candidates with their collection item counts.
- When the query was qualified, **Search all places named London** to drop the qualification.
- Always: `N items in this scope have no location.` linking to `without:location`, so a user never mistakes "not placed" for "not present".

Autocomplete carries the same protection earlier: place completions show the item count in the collection, so a place with zero items is visibly zero **before** the user commits to it.

The widening action, the qualification-dropping action, and the `without:location` link are implemented. Two parts are **deferred to a future release** with §3.4, because both need per-place collection counts: the **Did you mean** candidate list, and the leading `N` in the no-location line — which currently reads as a plain link rather than a count. Naming the centre uses the committed term text; resolving it to its canonical qualified name in the message waits for the same work.

### 3.8 Compatibility

Existing saved searches and history entries containing `place:` / `city:` / `state:` / `country:` keep working and now match a superset of what they matched before. That widening is deliberate and is the point of the change; it is disclosed once, in release notes, as "location searches now find photos located by GPS, not only photos with a place field written into them." No saved search is rewritten on load. A user who genuinely wanted the old field-only behavior expresses it as `place:London with:place`.

Two further rules complete the vocabulary:

- **The application generates resolved location terms.** Map clicks, timeline nodes, and the distance slider emit `loc:`, because only a resolved location term reproduces a set formed around a point. Location group headers and breakdown-strip chips emit the level-scoped form — `place:` / `state:` / `country:` — of the place key they were grouped by, because those name a place rather than a circle. Either form matches reverse-geocoded identity, which is what resolves baseline defect 7.
- **A location term matches at its own level.** Under §3.2 a query matches at the specificity it names instead of testing place, then region, then country in turn. A country-level query still matches everything in the country because the country is the resolved place, not because the term fell through to another field. This resolves baseline defect 8.



## 4. The distance filter

**Deferred to a future release.** Radius-bearing location terms and distance stepping remain implemented and tested, but the Items slider is not presented in 1.27.

### 4.1 When it appears

A **Distance** slider sits in the item-list control bar, on its own row directly below the existing filter row, whenever the current search contains exactly one location term that resolves to a place (including a term produced by the map). It does not appear for coordinate-only terms typed by the user, for multiple location terms, or when the scope has no location context — an always-present, usually-inert control would be noise.

### 4.2 Behavior

- Range 100 m to 100 km on a logarithmic scale, with detents at 100 m, 250 m, 500 m, 1 km, 2 km, 5 km, 10 km, 25 km, 50 km, 100 km. Keyboard steps move between detents; dragging is continuous and snaps to a detent within a small tolerance.
- The label reads `Distance: 10 km` and, while dragging, `Distance: 10 km — 1,240 items`, so the cost of the change is visible before release. The live count is **deferred to a future release**: it needs §4.3's debounced in-flight search to produce a number for a value the user has not committed to, and a count that lags the thumb is worse than no count.
- Moving the slider rewrites the distance of the single location term and re-runs the search. The address box updates to the canonical term. This is a query change and participates in navigation history as one entry per settled value, not per pixel.
- Removing the term (clearing the search) removes the slider. Dragging to the maximum does not remove the term.

### 4.3 Interaction with refresh

A search re-run rebuilds the item list, which is where a naive implementation loses the drag. The rule is:

- The slider owns its value while it is captured. Search results, item refreshes, and control-bar rebuilds must not write the value back, reposition, or recreate the captured control.
- Search re-runs are debounced while captured (coalesce to at most one in-flight search plus one pending value) and issued with the current generation; a result from a superseded value is discarded, per [design.md](design.md#system-states-and-consistency).
- On release, the settled value commits: the term is canonicalized, one history entry is written, and the control resumes ownership from state.
- The control-bar row keeps a stable height across refreshes so the pointer never leaves the control because the layout moved.

## 5. The map

### 5.1 Areas become places with a radius

Map areas stop producing `area:` searches. When an area resolves, it resolves to a **place plus radius**:

- The place is `find_largest` over the area's photo bounds, falling back to `find_closest` on the area centroid — the existing rule, which is already population-weighted and therefore already picks the name a user recognizes. When neither yields a place within its attribution radius (§2.5), the area is labelled `Remote area` and clicking it opens the area's coordinates rather than borrowing a distant city's name; naming a water body instead is deferred with §2.6.
- The radius is the distance from the resolved place's position to the furthest corner of the area's photo bounds, rounded **up** to the next slider detent and clamped to 100 km. When the bounds are degenerate the radius is the smallest detent that retains every item in the area.
- Clicking the area opens `loc:"<qualified name>, <radius>"`.

Areas may then overlap, and a photo may satisfy two neighbouring area clicks. That is the correct trade: "within 100 km of London" is a concept users hold, while "the photos GeoNames happened to attribute to London" is not. Counts shown on the map remain cell-based (they describe the map), while counts shown after the click are search results (they describe the query); when they differ, the item list states the searched centre and radius so the difference is explained rather than surprising.

### 5.2 Compatibility

`area:` remains parseable so existing saved searches and history entries keep working. On load it resolves through `find_map_location` exactly as today and is then rewritten to the equivalent `loc:` term when the user commits any change. No new `area:` terms are created.

### 5.3 Clicking never changes grouping

Map click opens a search. It does not set `group_by::location`. See §7 and [design.md](design.md#navigation-and-search).

### 5.4 Bubble content

The hover bubble keeps its icon, qualified place name, representative thumbnail, and count, and adds the visit summary from §6.6 when one exists. Its action line states the search it will run, including the radius.

The gazetteer answers on the location worker, so the first bubble shown for an area usually has no name yet. Until the name arrives the bubble states the count alone and offers no action line: a sentence that reads "close to " or "within 25 km of " with a hole in it is worse than a short true one. Clicking an unnamed area still works and searches its coordinates, exactly as `Remote area` does.

The qualified name, the count, and the radius-bearing action line are implemented. The visit summary is **deferred to a future release**: §6's derivation produces the nodes it needs, but the map bubble is built from a place rather than from the result set, so feeding it needs a per-place visit lookup that does not exist yet.

### 5.5 The map inside advanced search

Advanced search builds a location term in place rather than through a second modal dialog. The map fills the right of the dialog and is always live; the left column ends with a **Located within** check box and one line describing what was picked.

- The map opens framed on what is worth clicking. With no remembered pick it fits the box that holds the collection's geotagged photos; with a remembered pick it fits that place and the distance it covers. Framing happens once, at the first layout that has an extent to fit into, so later panning and zooming are never undone. A default coordinate at street level is not a starting point: it shows one road, no markers, and demands ten zoom steps before the gesture below can begin.
- The map shows the same clustered collection markers as the sidebar map, so the user can see where photos actually are before choosing an area. Markers are rebuilt for the current zoom level on the query worker and published back to the map on the UI thread with a generation check; a hovered cluster shows the same thumbnail-and-count bubble as the map view. A cluster resolves to the highest-population place whose §2.5 `At` radius contains its centre, so a recognizable city wins over a nearby minor feature. Its caption reads, for example, `117 items close to Singapore` only when no other visible bubble resolves to Singapore at the current zoom; otherwise it keeps the unambiguous count-only caption.
- Panning and zooming until a wanted hot spot is visible, then clicking it, is the whole gesture. There is no place-search box: the collection's own hot spots are the thing worth searching, and a name box would offer places the collection has no photos of.
- The map has no centre crosshair here, because the centre is not the answer. Panning moves the view and nothing else, and a drag is never read as a click. Contrast §5.6, where the centre *is* the answer and the crosshair is therefore mandatory.
- Clicking a cluster picks it without moving the map, ticks the check box, and marks the picked bubble with a halo. The location worker resolves the bubble to a nearby named place; when that name is suitable, the place centre and radius replace the raw bubble coordinate so the search can read as `loc:Tokyo, 100km` without excluding the picked area. Otherwise the exact bubble remains selected.
- Clicking a cluster also switches the scope to the collection. The markers are built from the whole indexed collection, so a pick made while the folder scope is selected would promise items the search then refuses to look for — the user sees a hot spot with a hundred photos in it and gets nothing back.
- The radius comes from the bubble, not from a slider. A cluster stands for a fixed cell of ground at the current zoom, so the radius is that cell rounded up to the next §4.2 detent — rounding up because §5.1 must never drop an item the visible area contained. Zooming in before picking therefore narrows the search, which is the same gesture the user already made to find the hot spot.
- The line under the check box reads as the search it will run: the radius and resolved place name, or the picked latitude and longitude when no suitable name exists. It is blank until something is picked.
- The check box remains the single gate. Picking ticks it, and unticking leaves the pick visible but out of the search.
- OK produces the same single `loc:` term as any other route to a location search. A resolved bubble uses the recognizable place form; the coordinate fallback uses readable decimal coordinates and an explicit metre or kilometre distance.

### 5.6 The locate view: where the centre *is* the answer

The locate view is the one map that writes rather than searches, and its contract is the deliberate opposite of §5.5. The user is choosing a single coordinate to stamp onto the selected files, so the centre of the map is the answer, and everything on screen must say so.

- **The crosshair is the answer.** It sits at the centre and is drawn in the warning/important colour rather than the accent colour the cluster bubbles use. Sharing a colour with the bubbles was the original defect: users read the crosshair as one more cluster and never learned that the centre was what would be written.
- **The crosshair is a plain cross.** Two unbroken semi-transparent lines, full length, no ring and no centre disc. Any enclosed shape at the centre reads as a small bubble, and any filled shape hides the bubble the user has just moved under it; the bare intersection of two lines does neither.
- **Clicking a cluster moves the map so the cluster lands under the crosshair.** In §5.5 a click picks without moving, because there the centre means nothing; here a click that left the map still would be picking something the crosshair does not point at. A drag is still a drag: movement beyond a few pixels is a pan, never a pick.
- **The explainer names all three gestures and the button.** Drag, click a bubble, or search by name; wheel to zoom; nothing is written until **Add location** is pressed. The button name is spelled out in the explainer text rather than composed at run time, so *if the Add location command is ever renamed, `map_instructions` in `app_text.h` must be renamed with it* — and every `.po` entry re-translated. This is the one accepted drift risk in this section.
- **A location the user can already see is never hidden from them.** The **Show items with no location** filter thins the selector strip to items still needing placement, which is useful when walking a long list. But entering the view with a selected or focused item that already carries a location, while that filter is on, hides the very item being placed and the strip reads as empty. Entering the view in that state therefore turns the filter off. The filter is a convenience; being unable to see what you are about to change is not a convenience.

### 5.7 Downloaded tiles live in one database

The OpenStreetMap tile usage policy requires clients to keep what they download so that a repeat view costs no traffic. Diffractor keeps every tile in a single `map-tiles-cache.db`, in the same app cache-data folder that holds `diffractor-cache.db` — both are rebuildable caches, so they live together. One file rather than thousands of loose PNGs: a cache the user can see the size of, back up, and delete in one action, instead of a directory that looks like a leak.

The store is bounded rather than trusted to stay small. A tile is evicted when it has gone unused for thirty days, and when the file exceeds its size cap the least recently used tiles go first — but nothing fetched within the last seven days is ever evicted, whatever the cache is costing, because that window is the policy obligation. Pruning is incremental and bounded per pass, and returns the freed pages to the filesystem rather than leaving a file that only grows.

A tile database that cannot be read is replaced, not repaired: every row in it can be downloaded again. If it cannot be opened at all the map still works and simply fetches each session. Threading and connection ownership are [implementation](implementation.md#sqlite-connection-ownership).

## 6. Location × time: visits and the timeline

**Derivation implemented; presentation deferred to a future release.** The derivation lives in `src/model_visits.*` and runs on the location worker from a detached snapshot of the results. The Items strip remains implemented but is not constructed in 1.27. The representative thumbnail in the node bubble (§6.3) and reciprocal map-bubble summary (§6.6) also remain deferred.

The goal is a small, honest strip that answers "when was I here, and which trip do you mean?" It must never appear when it would only restate that a user lives somewhere.

### 6.1 Concepts

| Term | Meaning |
|---|---|
| Cluster | A set of result items close together in space, keyed by qualified place where available and by a coarse spatial bucket otherwise. |
| Visit | A maximal run of a cluster's items in time whose internal gaps are all below the gap threshold. A candidate timeline node. |
| Era | A long, dense residence-like run at one cluster. Not a visit; usually suppressed. |
| Node | A visit or era selected for display. |
| Timeline | The ordered strip of nodes for the current result set. |

### 6.2 Deriving nodes

Computed on a worker from index data for the **current result set only**, never for the whole collection:

1. **Filter.** Keep items with a usable created date and either coordinates or gazetteer-resolvable place text. Items lacking either are counted and excluded.
2. **Cluster.** Assign each item to its qualified place when one resolves within 25 km, otherwise to a coarse spatial bucket (the heat-map cell at a span chosen so a bucket is roughly 50 km). Merge clusters whose centres are within 25 km and share a region. Items resolved to a water body cluster by that body rather than by bucket, because a ship or aircraft moves continuously and would otherwise produce a trail of buckets instead of one journey.
3. **Segment.** Sort each cluster's items by date and split at gaps larger than `G = clamp(3 × median gap, 14 days, 180 days)`. Each run is a candidate.
4. **Score.** `score = w1·log(items) + w2·log(1 + days) + w3·density + w4·separation + w5·rarity`, where density is items per active day, separation is the smaller adjacent gap normalized by `G`, and rarity is the inverse share of the cluster in the whole collection. Weights are tuned constants recorded next to the implementation, not per-user settings.
5. **Classify eras.** A candidate is an era when its span exceeds 18 months **and** its items occupy more than 25% of the months in that span **and** its cluster holds more than 15% of all located items in the collection. Eras are suppressed by default: a decade of photos from where the user lives is not a trip.
6. **Reveal eras on intent.** When the current query names the era's place — a `loc:` term, a location group drill-down, or a map click resolving to that cluster — the era becomes eligible and is labelled by its stable bounds, `London 2000–2010`. It is offered only when its first and last quartile boundaries are stable under a ±10% change of `G`; an era whose extent depends on the threshold is not reported as a fact.
7. **Select.** Keep the top ten nodes by score, then restore chronological order. Merge adjacent nodes of the same cluster separated by less than `G`. Drop nodes holding fewer than five items or less than 1% of results, unless fewer than three nodes survive, in which case the strongest are kept so the strip is either useful or absent.
8. **Publish.** Show the timeline only when at least two nodes survive and they cover at least 40% of the located results. Otherwise show nothing.

The whole computation is bounded by the result count, cached by search generation plus grouping-independent inputs, and discarded when the generation changes. It is Source work and never runs during paint ([implementation.md](implementation.md#view-invalidation)).

### 6.3 Presentation

A single-row strip directly under the control bar, above the first group, scrolling with the item list like the rest of the control area:

```
──2008──────2012────Mar 2020──Dec 2020────────2023──
  London    Australia  London    London        Japan
```

- A thin baseline spans the result set's date range, compressed non-linearly so dense periods do not squeeze sparse ones out of existence; ticks carry the coarsest label that stays unambiguous (`2012`, `Mar 2020`, `2000–2010`).
- Each node is a small selectable box holding the qualified place name and item count. Boxes never overlap; when space runs out, lower-scoring boxes collapse to a tick with a count and are reachable by hover.
- Hovering a node shows a bubble with the exact date range, item count, a representative thumbnail, and the search it will run.
- Clicking a node refines the current search with that node's place, radius, and date range. The address box shows the resulting query, so the refinement is inspectable and undoable by Back like any other navigation. A selected node is visibly latched and offers Clear.
- The strip is presentation, not selection: it never changes focus or selection, and it never changes grouping or sorting.

### 6.4 Why this delights

- It answers the question users actually have. "Australia" returns eleven years of photos; "Australia, 2012" returns the trip they remember. The strip turns a place into a set of memories without requiring them to know dates.
- It is discovered, not configured. Nodes appear from the data; there is no trip editor, no manual album, and nothing to maintain.
- It is honest about scatter. Not showing a node for the city the user lives in is what makes the nodes that do appear meaningful.
- It composes. Every node is a normal query, so it can be saved, shared as text, refined further, or reached from the address box.
- It reciprocates with the map, so the two visualizations explain each other rather than competing.

### 6.5 Guardrails

- Ten nodes maximum. The strip is a starting point for drilling down, not a summary of a life.
- No node is shown for a range the user cannot re-derive: clicking a node always produces a query that reproduces exactly the node's items.
- Items with dates but no location, or locations but no dates, are never invented into a node; their counts are available in the strip's overflow so the user knows what the strip does not cover.
- Time zones use the same rule the rest of the product uses for created dates; a visit boundary is never presented with more precision than the underlying dates support.

### 6.6 Reciprocal summaries

The same node computation feeds both visualizations:

- **Map bubble** — after the place name: `Visits: 2008, 2012` or `2000 → 2010`, up to four entries plus `+n more`.
- **Timeline bubble** — after the date range: the qualified places inside that range, `July 2025 — Japan`.
- **Location group header** — when grouping by location, the header may carry the same compact visit summary for that place, which is the cheapest place to show it because the grouping already did the clustering.

## 7. The items control bar

**Partially implemented.** §7.1 splits the totals from the grouping button, resolving baseline defect 5. §7.2's breakdown strip and class chips remain presented and are fed by the visit derivation. The distance-slider and timeline rows are deferred from 1.27; their supporting controls and §7.3 height-budget code remain dormant.

Location work exposed two general problems; the general rules belong in [design.md](design.md#application-structure) and are summarized here for the location-specific parts.

### 7.1 Totals and grouping are separate affordances

- The **totals** affordance reads `{item count}|{total size}` only. Hovering it shows the existing breakdown of items by type. It has no menu.
- A separate **Grouped by {current grouping}** button opens the grouping and sorting menu that the totals affordance carries today. When a non-default sort is active it reads `Grouped by {grouping} · {sort}`.

### 7.2 Location breakdown strip

When the current scope has location context — a `loc:` term, a map click, or a location drill-down — and the current grouping is **not** `group_by::location`, the control bar shows a single row of the top places inside the result set: qualified name and count separated by `|`, ordered by count, capped at what fits with an overflow affordance. Clicking one refines the search to that place. This gives the user the drill-down that location grouping would provide without taking their grouping away from them.

A chip states a count and then runs a search, and those are one promise: the search returns exactly what the chip counted. The only way to keep that promise is to count with the same function the search matches with, so the breakdown is a **partition by place key** — precisely the key location grouping uses:

- An item's place is its stored place, region, and country, with any field it left empty filled by attribution (§3.2). Attribution is a function from a coordinate to one place, so every item lands in exactly one entry and the entries never overlap.
- A chip runs the level-scoped terms it was keyed by — the same query the location group header for that place would run. It carries no radius, because a radius describes a circle rather than a place: a circle wide enough to cover a small place also covers whatever larger neighbour happens to fall inside it, and the chip would then return items it never counted.
- An entry that leaves a field empty emits no term for it, so its query already returns everything an entry that only differs by naming that field counted. The two cannot both keep the promise, so the vaguer entry **absorbs** the sharper one and states the count its own click returns. This is what makes one Singapore rather than three: attribution leaves the region empty where the gazetteer has no region for it, one photo carries `Singapore` and another the name GeoNames has since retired, and all three describe the same city.
- A chip is then labelled at the **smallest name form that is unique within the strip**, which is §2.1 applied to what is on screen rather than to the gazetteer. Most chips are their place name alone; two that would read the same are both promoted until they differ, so a result set holding both Londons shows `London, United Kingdom` and `London, Canada`. Two chips that read identically are one affordance the user cannot choose from, and the hover text is not a substitute for a label.

Counts are of the whole result set, including items with no date, because the chip's query carries no date either.

The strip is hidden when grouping is already by location, because the group headers already provide it. It never appears for scopes with no located items.

### 7.3 Ordering of the control area

In 1.27 the filter row (filter box, media type filters, thumbnail size, grouped-by, totals) is followed only by the location breakdown strip when §7.2 applies. The deferred target order adds the distance slider before the breakdown strip and the timeline after it. Each optional row keeps a stable height while present, so rows appearing or disappearing never move a control the user is holding.

Four rows at once is too many, and all four can apply simultaneously. The user came to see photographs, so the control area carries a **height budget**: the optional rows together may occupy no more than roughly a quarter of the content height. When they would exceed it, rows collapse from the bottom of the priority order until they fit, each becoming a one-line header carrying its title and count and expanding on click.

Priority is by how directly a row serves what the user is doing now: the distance slider first, because a slider that collapses while being dragged is indefensible; then the timeline when the query names a place; then the breakdown strip. A row the user expands stays expanded for the session, because an explicit expansion outranks the budget's guess. The budget is evaluated against available height, so a maximized window on a large display simply shows everything and a short window degrades gracefully rather than burying the items. Collapse and expansion never occur while a control in that row is captured (§4.2).

## 8. Mental model mapping

Every new interaction resolves against the closed ontology in [design.md](design.md#user-mental-model).

| Interaction | Scope | Contents | Target | Effect |
|---|---|---|---|---|
| Commit `loc:"London, UK, 10km"` | Search | Items within 10 km of London, United Kingdom, current filters and grouping unchanged | Visible items | Navigation only; no file changes; Back restores the previous query |
| Drag the distance slider | Search | The single location term's radius, with live visible/hidden counts | Visible items | Navigation only; one history entry per settled value |
| Click a map area | Search | Items within the resolved radius of the resolved place | Visible items | Navigation only; grouping and sorting are preserved |
| Click a timeline node | Search | Items at that place within that date range | Visible items | Navigation only; Clear restores the unrefined query |
| Click a breakdown-strip place | Search | Items at that place, found by the same place terms its location group header would use | Visible items | Navigation only |

## 9. Other metadata that could carry the same treatment

The visit engine is really "segment a result set on one axis and show the honest clusters". The same strip, with the same suppression rules, could carry:

- **Camera and lens** — a strip of bodies or lenses present in the result set. Delightful when a user is looking for "the film-look ones" and remembers the camera, not the date.
- **People and tags** — the top co-occurring tags for the current results, which turns a location query into "Japan, with Anna" in one click.
- **Time of day** — golden hour, blue hour, night, computed locally from coordinates plus created time. Strong on a location search, where "Prague at night" is a real intent.
- **Season and On this day** — same month across years, which pairs naturally with the timeline because it is the orthogonal cut through the same data.
- **Rating and label** — a compact keeper strip so a trip can be narrowed to its best frames without leaving the query.
- **Altitude and terrain** — after the evidence requirements in §2.8 are resolved, a height band strip could separate coastal, valley, and mountain photography within one trip.
- **Trip facts in the node bubble** — number of distinct places, span in days, and total distance between consecutive places. It reads like a memory rather than metadata, and every input is already loaded.
- **Countries and places counters** in the collection summary — "you have photographed 41 places in 14 countries" — which is a genuinely pleasant fact and costs one pass over data the index already holds.

Everything listed is computed locally from indexed metadata. Nothing here introduces a network dependency, and any future feature that would must disclose it under the network rules in [design.md](design.md#system-states-and-consistency).

## 10. Verification

A change implementing this document is complete when:

- Qualification levels round-trip: the generator emits levels, the loader reads them, and `qualified_name` returns `London, United Kingdom` and `London, Canada` for the two Londons and `Reykjavík` for a unique name.
- `loc:London` matches both Londons; `loc:"London, Canada"` matches one; `loc:"London, UK, 10km"` matches only items with coordinates inside the radius and reports the excluded no-coordinate count.
- `place:`, `city:`, `state:`, `country:`, and `near:` resolve locations at their level and match a GPS-only item with no stored place field; `with:place` / `without:place` still address the stored field, and `without:location` matches items with neither text nor coordinates. `latitude:` and `longitude:` are unchanged.
- A location query with no results names the place and radius it searched, offers a widening action to the next detent, offers to drop a name qualifier, and offers the items that have no location at all. Listing ambiguous alternatives with their item counts is deferred with §3.4.
- Attribution is bounded: a coordinate far from any place resolves to `Near <place>` or `Remote area`, and never to a distant city. A mid-ocean coordinate resolves to `Remote area`, reports no country, and carries a bearing descriptor. Naming the sea itself is deferred with §2.6.
- Items resolved remote are excluded from `without:location`, group under their country when one is known, and carry a bearing descriptor that is never used as a group key or search term.
- The deferred `@flying`, `@altitude`, and `@underwater` terms are not accepted, and no altitude-derived class appears in item details, location breakdowns, grouping, or visit derivation.
- A map click opens a `loc:` term carrying the area's qualified place name and a radius rounded up to a detent, never an `area:` term, and never changes grouping. An area with no place inside its attribution radius opens its coordinates instead of borrowing a distant city's name. The distance slider appears for the resulting search.
- A location group header link reproduces its own group: clicking the place carries its region, so the other place of the same name is not returned.
- Resolution writes nothing to any file: after indexing, searching, grouping, and viewing a scope of derived locations, file metadata is byte-identical, derived values are labelled as derived in the item panel, and an unpopulated editable location field still reads as empty.
- Existing saved `area:` searches still load and resolve.

Deferred to a future release, with their sections: §2.4, §2.6, §3.4 and the completion counts, the ambiguous-alternatives list and in-scope no-location count of §3.7, §4.2's live drag count, §5.4's and §6.6's map-bubble visit summary, and the representative thumbnail in §6.3's node bubble.
- A location group header, a timeline node, and a breakdown-strip entry each produce a query whose results equal the set they were rendered from, including items that reached the group by reverse geocoding.
- Distance parsing accepts `m`, `km`, `mi`, and rejects a trailing token that is not a complete distance; canonical formatting round-trips through parse.
- The coordinate form `loc:-30.515+151.66+5` continues to parse and match unchanged.
- A map click produces a `loc:` term with a radius, preserves the user's grouping and sorting, and an existing `area:` search still resolves.
- The distance slider retains capture across a search refresh, coalesces in-flight searches, and writes exactly one history entry per settled value.
- Timeline node derivation is deterministic for a fixed result set, suppresses a residence-scale cluster, reveals it when the query names it, caps at ten nodes, and produces for each node a query that reproduces exactly that node's items.
- No location resolution, gazetteer read, or node computation runs on the UI thread, and none runs inside a paint path.

Regression coverage extends the existing location tests in `src/test_search.cpp` (`should_find_location`, the map-area tests) and the map regression in `src/test_regressions.cpp`.
