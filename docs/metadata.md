# Metadata

This document owns the mapping between Diffractor's properties and the metadata standards and container tags they are read from and written to. What a metadata command means to the user belongs to [design.md](design.md); how a write reaches disk — staging, patching, backups, sidecars, rollback, and the failure contract — belongs to [file-io.md](file-io.md); the vendored XMP toolkit and its fork belong to [third-party.md](third-party.md).

Metadata is information related to a media file. It may be embedded in the file or held
in a separate associated file.

Diffractor can add or update metadata such as tags, rating, description, or location, and
reads metadata written by cameras and other applications. Values are cached in the index
for fast searching, but the file remains authoritative — the cache is a derived copy.

This document is maintained by hand. The mappings it describes are spread across
[metadata_xmp.cpp](../src/metadata_xmp.cpp), [metadata_exif.cpp](../src/metadata_exif.cpp),
[metadata_iptc.cpp](../src/metadata_iptc.cpp) and `parse_metadata_ffmpeg_kv` in
[files_core.cpp](../src/files_core.cpp), so no single table in code can be generated from.
The companion "Formats and Codecs" page *is* generated, from the live file type and codec
tables, by `diffractor64.exe /gen-docs`.

## Where metadata is stored

| Container | Read from | Written to |
| --- | --- | --- |
| JPEG, TIFF, PNG, WebP, HEIC | EXIF, IPTC, XMP | EXIF, IPTC, XMP |
| Raw photos | EXIF, embedded XMP | Sidecar `.xmp` file, as Adobe products do |
| MP4, MOV, M4A | iTunes/QuickTime atoms, XMP | XMP, movie header dates, ISO copyright |
| MP3 | ID3v2, XMP | XMP and ID3v2 |
| AVI, WAV | RIFF `INFO` chunks, XMP | XMP and RIFF `INFO` |
| ASF, WMV, WMA | Windows Media attributes | XMP and Windows Media attributes |
| MKV, WebM | Matroska tags | *not supported* |

Reading and writing are not symmetric. Diffractor only ever asks the XMP toolkit to store an
XMP packet, but the toolkit's format handlers *reconcile* on save: when the packet is written
they also rewrite the container's own legacy tags from it. So updating a rating on an MP3 or a
title on a WAV does update the ID3v2 frame or `INFO` chunk that other players read. Matroska
has no handler, so nothing is written for `.mkv` or `.webm` — a rating or tag shown for one of
those files cannot be changed.

### What reconciliation writes back

These are the legacy fields the toolkit rewrites when Diffractor saves metadata. Anything not
listed is left untouched, so container tags Diffractor reads but cannot express in XMP survive
a save unchanged.

| Container | Legacy fields written from XMP |
| --- | --- |
| MP3 | `TIT2`, `TPE1`, `TPE2`, `TALB`, `TRCK`, `TPOS`, `TCON`, `TCOM`, `TCOP`, `TPE4`, `COMM`, `USLT`, `TCMP`, `WCOP`, `POPM`, and the date frames `TYER`/`TDAT`/`TIME`/`TDRC` |
| AVI | `INAM`, `IART`, `ICMT`, `ICOP`, `ICRD`, `IENG`, `IGNR`, `ISFT`, `IMED` |
| WAV | the same `INFO` set as AVI, plus `ISRC` |
| ASF, WMV, WMA | Title, Author, Copyright, Description, CopyrightURL, creation date, `WM/Category`, `WM/SharedUserRating` |
| MP4, MOV | movie header creation and modification dates, and ISO copyright only — no iTunes atoms |

`POPM`, `TPE2`, `WM/Category` and `WM/SharedUserRating` are reconciliations added in
Diffractor's fork of the toolkit; the rest are stock Adobe behaviour.

## Dates

Dates are the one subject where a metadata mistake is visible in ordinary browsing: an item
groups under the wrong month, and the day it was filed under no longer finds it. This section
owns which tag feeds which date, how several disagreeing tags resolve to one answer, and how
the result is stored. What the user does with the answer — grouping, sorting, the timeline —
belongs to [design.md](design.md).

### Three dates, and no more

A media file can carry a dozen date tags. A user holds three ideas, and every tag maps onto
one of them.

| Date | Answers | For a scanned 1980 photograph |
|---|---|---|
| **Original** | when the *content* was made | 1980, when the shutter fired |
| **Created** | when *this file* was made | the day it was scanned |
| **Modified** | when the file last changed | the last save |

The words are deliberately not photographic. A collection holds scanned paintings, contracts,
sheet music and screen recordings as readily as it holds photographs, and "taken" describes
only the last of those. **Original** works for a shutter, a brush and a typewriter alike.

**Created** covers both digitisation and encoding, which are two mechanisms for one idea:
this file came into being at some point after its content did. EXIF calls that
`DateTimeDigitized` and MP4 calls it a movie header creation time, but a user asking when a
file was created does not distinguish them, so neither does Diffractor.

There is no fourth date. Tags that answer none of these three questions — an ICC profile's
build date, an IPTC embargo date, a metadata-record timestamp — are not dates *of the media*
and are never read as one.

### A date is a wall clock reading

The same instant appears in two incompatible shapes in a single file:

```
EXIF  DateTimeOriginal : 2025:08:16 18:11:56             naive, no zone, whole seconds
XMP   xmp:CreateDate   : 2025-08-16T18:11:56.368+01:00   instant, +01:00, milliseconds
```

Diffractor stores the **wall clock reading** — 18:11:56 on 16 August — together with the UTC
offset when the source supplied one, and treats two dates as the same when their wall clock
readings agree **to the second**. Three consequences, all of them load-bearing:

- The two lines above are one date, not two, so a file carrying six date tags usually stores
  one or two values.
- Grouping never converts, so an item cannot be filed under a day adjacent to the one its
  metadata states. Comparing a UTC value against a local one is the defect class that made
  "click to open finds nothing" intermittent and timezone-dependent.
- Sub-second precision is kept in the stored value and ignored when comparing, so a burst of
  frames still sorts in capture order while remaining one group.

Sources that are genuinely instants — a container creation time, a filesystem timestamp — are
recorded as instants and converted to local wall clock **when the pack resolves**, never when it
is filled. Converting on the way in would bake the scanning machine's timezone into the index, so
a database copied to a machine in another zone would file every video under the wrong day.

XMP needs one extra step, because `parse_xml_date` reads the digits and drops the zone. A packet
value ending in `Z` is a UTC instant and one ending in `+01:00` is already a wall clock reading,
and the two are indistinguishable once parsed. [metadata_xmp.cpp](../src/metadata_xmp.cpp) recovers
the suffix and passes it with the value. Without that step an `xmp:CreateDate` written as `Z` lands
an hour or more from the container date it is meant to agree with, and one date reads as two.

### Where each date comes from

Authority runs highest first; the first populated source wins its concept outright. "Read
today" distinguishes what the scanner maps from what the format tables reserve, so the list can
grow without implying a claim it does not yet meet.

| Source | Date | Authority | Read today |
|---|---|---|---|
| EXIF `DATE_TIME_ORIGINAL` (0x9003) | Original | 1 | yes |
| XMP `exif:DateTimeOriginal` | Original | 2 | yes |
| IPTC `DATE_CREATED` (2:55) + `TIME_CREATED` (2:60) | Original | 3 | yes |
| ID3 `TDOR` | Original | 4 | yes |
| XMP `photoshop:DateCreated` | Original | 5 | yes |
| EXIF `DATE_TIME_DIGITIZED` (0x9004) | Created | 1 | yes |
| XMP `exif:DateTimeDigitized` | Created | 2 | yes |
| IPTC `DIGITAL_CREATION_DATE` (2:62) + `DIGITAL_CREATION_TIME` (2:63) | Created | 3 | yes |
| XMP `xmp:CreateDate` | Created | 4 | yes |
| Container creation — `creation_time`, `com.apple.quicktime.creationdate`, `©day`, `TDRC`, `ICRD`, Matroska `DateUTC`, PNG `tIME` | Created | 5 | yes |
| DV timecode, RAW embedded date | Created | 6 | yes |
| `date-eng`, `Rip date` | Created | 7 | yes |
| Windows shell property store | Created | 8 | yes |
| Filesystem created | Created | 9 | yes |
| EXIF `GPSDateStamp` (0x001d) + `GPSTimeStamp` (0x0007) | reference | — | yes |
| Filesystem modified | Modified | 1 | yes |
| EXIF `DATE_TIME` (0x0132) | Modified | 2 | yes |
| XMP `xmp:ModifyDate` | Modified | 3 | yes |

### The zone and the fraction a reading was written with

Three tags qualify an EXIF date rather than being one, and each is stored with the reading it
qualifies:

| Qualifies | Zone | Fraction |
|---|---|---|
| `DATE_TIME` | `OFFSET_TIME` (0x9010) | `SUB_SEC_TIME` (0x9290) |
| `DATE_TIME_ORIGINAL` | `OFFSET_TIME_ORIGINAL` (0x9011) | `SUB_SEC_TIME_ORIGINAL` (0x9291) |
| `DATE_TIME_DIGITIZED` | `OFFSET_TIME_DIGITIZED` (0x9012) | `SUB_SEC_TIME_DIGITIZED` (0x9292) |

**None of them can be applied where it is read.** A qualifier may appear before or after the date
it qualifies, and `OFFSET_TIME` sits in the Exif SubIFD while `DATE_TIME` sits in IFD0, which is
walked first. So the readings are collected during the directory walk and combined once it ends.
The same is true of IPTC, which splits every instant across two datasets — `CCYYMMDD` and
`HHMMSS±HHMM` — that may arrive in either order.

**The fraction is digits after an implied point**, so `SubSecTimeOriginal` of `12` is 0.12 s and
not twelve of anything. It is what keeps two frames of a burst apart; without it they collapse to
one instant and order by name.

**The zone changes nothing a user sees today.** An EXIF date is already a wall-clock reading, so
recording its offset adds provenance rather than shifting a value. It is stored now because it can
only be filled by re-reading every file, and because `GPSDateStamp` plus `GPSTimeStamp` — a true
UTC instant for the same moment — is the other half of recovering a zone a file never stated.

One rule settles the whole table rather than one row at a time:

> **A concept is answered by whatever is authoritative for the thing the concept describes.**

Original and Created describe the *content*, and the filesystem knows nothing about content, so
metadata outranks it and the filesystem stamp is the last resort. Modified describes the *file*,
and there the filesystem is not a fallback but the only thing that tracks an actual edit — a
metadata modify tag records when some tool last wrote the metadata, which a copy preserves and
which an editor that does not maintain the tag never touches. The two tags therefore rank below
the stamp and answer only where no usable stamp exists, such as a remote item or an unhydrated
placeholder.

The asymmetry between Created and Modified is the point rather than an inconsistency:
**copying a file destroys its creation stamp and preserves its modification stamp.** So the
container outranks the filesystem for Created, and the filesystem outranks every tag for Modified.

Three further rows carry reasoning the rest of the table depends on:

- **`photoshop:DateCreated` is the lowest-authority Original**, below every other source and
  below tags that nominally mirror it. It is nominally a copy of `DateTimeOriginal`, but it is
  the one an image editor rewrites on save — an edited file arrives carrying a
  `photoshop:DateCreated` months after its capture time. Ranking it last keeps it useful for
  files that carry nothing else while denying it the power to move a photograph to the day it
  was retouched.
- **EXIF `DATE_TIME` is Modified, not Original.** 0x0132 is defined as the time the file was
  last changed and lives in IFD0, which is walked before the Exif SubIFD holding
  `DateTimeOriginal`. Reading it as a creation date is why an edited photograph grouped under
  the day of its edit. It is Modified's *second* source, not its first, for the reason above.

`GPSDateStamp` is a true UTC reference for the moment of capture and is listed for one purpose
only: recovering an unknown UTC offset. It is date-and-time in UTC with no zone information of
its own, so it is never displayed and never resolves a concept.

### Resolving to one answer

Each of the three dates resolves independently, by authority, across every source the file
supplied. Where an item shows a single date it is:

```
Original, else Created, else Modified
```

so a scan that carries no `DateTimeOriginal` still files under the day it was scanned rather
than under nothing. **This ladder is implemented once.** Grouping, sorting, the timeline and
heat map, `created:` and `original:` searches, and the properties panel all call the same
resolver; four hand-written ladders that disagreed about the same file is the defect this
replaces.

### The date pack

The resolved dates and the sources behind them are stored as one property in the metadata
blob, grouped by value so that agreeing sources cost nothing:

```
u8   version
u8   group_count
  per group:
    u32  sources          bitmask naming every source carrying this value
    i64  wall_clock       100 ns; sorted ascending, delta-encoded after the first
    i16  utc_offset_mins  INT16_MIN when the source gave no offset
```

The bitmask is what makes provenance answerable: the properties panel can state that Original
came from `DateTimeOriginal` and not from an editor's mirror, which turns "the date is wrong"
into a question with a visible answer. It is also what keeps the format open — a new source is
a new bit and a new row in the table above, not a new field, a new database column and four
more ladder edits.

Groups are capped, and sources whose value did not fit are recorded as present-but-unstored
rather than dropped silently. Because the cap only ever evicts the lowest-authority distinct
values, no resolution can change as a result.

The pack does not save space against the three date fields it replaces; it costs about the
same and buys provenance and an open source list. It saves a great deal against storing thirty
sources as thirty properties, which is the alternative it is measured against.

### Writing a date

A write names the date it means. `Original` is written to `DATE_TIME_ORIGINAL` and to the
`photoshop:DateCreated` mirror; `Created` to `DATE_TIME_DIGITIZED` and `xmp:CreateDate`. Two
rules matter more than the mapping:

- **A resolved date is never written back.** Writing the answer of the ladder into
  `DateTimeOriginal` destroys the capture time of any file whose Original was inferred from a
  lower-authority source, and it does so silently, on a file the user only meant to rate.
- **An edit to one date leaves the others alone.** Correcting when a photograph was taken says
  nothing about when it was scanned.
- **Original is written to `exif:DateTimeOriginal` as well as the `photoshop:DateCreated` mirror.**
  The mirror alone is the lowest-authority capture source, so a file carrying its own
  `DateTimeOriginal` would outrank the edit and the correction would appear to do nothing. The XMP
  toolkit reconciles `exif:DateTimeOriginal` into the embedded EXIF on save, which is what makes
  the edit reach the tag that decides.

Two dates are **not** stable across a write, by design rather than by defect: the toolkit updates
`xmp:ModifyDate` on every save, and on a file that carried no XMP it can add an `xmp:CreateDate`.
Only Original is invariant, and that is the property worth asserting — a write that moves it has
destroyed when the picture was made.

### Files that carry no metadata

A plain text file, an archive or a tracker module has no EXIF, XMP or IPTC to read. Its pack is
synthesised from the filesystem timestamps the index already holds, with no file read at all.
Format capability is answered by the file type's traits, not by attempting a parse and failing.

## Photo metadata

EXIF names are `libexif` tag constants; IPTC names are IIM dataset constants. Where several
sources supply one property they are listed in precedence order.

| Property | XMP | EXIF | IPTC |
| --- | --- | --- | --- |
| album | `xmpDM:album` | | |
| album.artist | `xmpDM:albumArtist` | | |
| aperture | | `APERTURE_VALUE` | |
| artist | `xmpDM:artist`, `dc:creator` | `ARTIST`, `XP_AUTHOR` | `BYLINE` |
| camera.manufacturer | `tiff:Make` | `MAKE` | |
| camera.model | `tiff:Model` | `MODEL` | |
| city | `photoshop:City` | | `CITY` |
| comment | `exif:UserComment`, `xmpDM:logComment` | `USER_COMMENT`, `USER_COMMENT_XP` | |
| copyright | `dc:rights` | `COPYRIGHT` | `COPYRIGHT_NOTICE` |
| copyright.url | `xmpRights:WebStatement` | | |
| country | `photoshop:Country` | | `COUNTRY_NAME` |
| created | `xmp:CreateDate`, `exif:DateTimeDigitized` | `DATE_TIME_DIGITIZED` | `DIGITAL_CREATION_DATE` |
| credit | `photoshop:Credit` | | `CREDIT` |
| description | `dc:description` | `IMAGE_DESCRIPTION`, `XP_SUBJECT` | `CAPTION` |
| exposure | `exif:ExposureTime` | `EXPOSURE_TIME` | |
| fnumber | `exif:FNumber` | `FNUMBER` | |
| focal.length | `exif:FocalLength`, `exif:FocalLengthIn35mmFilm` | `FOCAL_LENGTH` | |
| iso | | `ISO_SPEED_RATINGS` | |
| label | `xmp:Label` | | |
| latitude | `exif:GPSLatitude` | | |
| lens | `aux:Lens` | `LENS_MODEL`, `MNOTE_CANON_TAG_LENS` | |
| longitude | `exif:GPSLongitude` | | |
| modified | `xmp:ModifyDate` | `DATE_TIME` | |
| orientation | `tiff:Orientation` | `ORIENTATION` | |
| original | `exif:DateTimeOriginal`, `photoshop:DateCreated` | `DATE_TIME_ORIGINAL` | `DATE_CREATED` |
| rating | `xmp:Rating`, `MicrosoftPhoto:Rating` | `IMAGE_RATING`, `IMAGE_RATING_PERCENT` | |
| source | `photoshop:Source` | | `SOURCE` |
| state | `photoshop:State` | | `STATE` |
| synopsis | `xmpDM:synopsis` | | |
| tag | `dc:subject` | `XP_KEYWORDS` | `KEYWORDS` |
| title | `dc:title` | `XP_TITLE` | `OBJECT_NAME` |
| year | `xmp:CreateDate` | | |

`xmp:Rating` holds a 0-5 star value and `MicrosoftPhoto:Rating` the equivalent percentage;
both are written so that Windows Explorer and Adobe applications agree.

### Maker notes

The EXIF `MakerNote` tag holds a vendor's own directory in a private format. Diffractor decodes it
for display only — Canon, Nikon, Fuji, Olympus, Sanyo, Epson, Pentax and Casio, through the vendored
libexif — and the [verbose metadata pane](selection-controls.md#verbose-metadata-blocks) owns how it
is presented. No maker note value feeds a property in the table above, so none is indexed, searchable
or written back, and a make that is not decoded keeps only its binary entry. The one exception
predates this and is listed above: `MNOTE_CANON_TAG_LENS` supplies `lens` when no other source does.

The three date rows list their sources in precedence order like every other row, but dates
alone resolve across all three standards at once rather than within a column. [Dates](#dates)
owns that ordering and is the authority where the two disagree.

## Video and audio metadata

This table describes **reading**. Diffractor does not parse container tags itself: FFmpeg
normalises each container's tags to generic lower-case keys before Diffractor sees them, so
the columns below name the tag in the file and the row names the Diffractor property the
normalised key feeds. Key matching is case-insensitive, which is what allows Matroska's
upper-case convention to work. For what goes back on save, see
[what reconciliation writes back](#what-reconciliation-writes-back) above.

| Property | MP4 / MOV atom | ID3v2 | RIFF `INFO` | Matroska | Windows Media |
| --- | --- | --- | --- | --- | --- |
| album | `©alb` | `TALB` | `IPRD` | | |
| album.artist | `aART` | `TPE2` | | | |
| artist | `©ART`, `©aut` | `TPE1` | `IART` | `ARTIST` | |
| camera.manufacturer | `manu`, `©mak`, `com.apple.quicktime.make` | | `maker` (AVI) | | |
| camera.model | `modl`, `©mod`, `com.apple.quicktime.model` | | | | |
| comment | `©cmt`, `©inf` | `COMM` | `ICMT`, `COMM` | `COMMENT` | |
| composer | `©wrt`, `©com` | `TCOM` | | | |
| copyright | `cprt`, `©cpy` | `TCOP` | `ICOP` | | |
| created, year | `©day` | `TYER`, `TDRC`, `TDRL` | `ICRD` | `DateUTC` | creation date |
| description | `desc` | | | | |
| disk | `disk` | `TPOS` | | | |
| encoder | `©enc`, `©swr`, `©too` | `TSSE`, `TENC` | `ISFT`, `ITCH` | `MuxingApp` | |
| episode | `tves` | | | | |
| genre | `gnre`, `©gen` | `TCON` | `IGNR` | `GENRE` | |
| latitude, longitude | `©xyz`, `loci`, `com.apple.quicktime.location.ISO6709` | | | | |
| media.category | | | | | `WM/Category` |
| rating | | `POPM` | | `RATING` | `WM/SharedUserRating` |
| season | `tvsn` | | | | |
| show | `tvsh` | | | | |
| synopsis | `ldes` | | | | |
| tag | `keyw` | | | `KEYWORDS` | |
| title | `©nam` | `TIT2` | `INAM`, `TITL` | `TITLE` | |
| track | `trkn`, `©trk` | `TRCK` | `IPRT`, `ITRK` | | |

Ratings use different scales per container and are normalised to 0-5 stars on read:
`POPM` and `WM/SharedUserRating` are rescaled from their native ranges, and Matroska
`RATING` is clamped. MP4 and MOV have no star rating of their own — the `rtng` atom is
iTunes' content-advisory flag, so a rating on those files comes from XMP alone.

## Known limitations

These are current defects rather than design decisions, recorded so the tables above are not
mistaken for a statement of intent. Each entry states which side it affects, because reading
and writing take different code paths — reads come from FFmpeg, writes from the XMP toolkit.

**Reading**

- **`POPM` is only honoured from Windows Media Player.** The frame surfaces as
  `id3v2_priv.<owner>` and only the owner `Windows Media Player 9 Series` is matched, so
  ratings written by other taggers are ignored. Writing is unaffected — the toolkit
  reconciles `POPM` from `xmp:Rating` whatever the owner.
- **MP4 `stik` is ignored.** FFmpeg maps it to `media_type`, for which there is no handler,
  so media category is read only from `WM/Category`.

**Writing**

- **Matroska cannot be written at all.** There is no XMP handler for the container, so a
  rating or tag shown for an `.mkv` or `.webm` file cannot be changed.
- **MP4 and MOV keep their iTunes atoms.** Reconciliation covers only the movie header dates
  and ISO copyright, so a rating or tag set in Diffractor lands in the XMP packet but leaves
  `©nam`, `©ART` and the rest untouched. Players that read only iTunes atoms will still show
  the old values.
- **Reads are wider than writes.** Every container above is read through FFmpeg but written
  through the XMP toolkit, and the toolkit reconciles a smaller set. A property Diffractor
  displays is not necessarily one it can store back to the same tag.

## Panorama

A file is a panorama because it says so, never because of its shape. The scan reads Google's
`GPano` namespace — which is what a phone panorama mode and most stitchers write, and which the
XMP toolkit does not know, so it is registered at startup — and stores one value per indexed
image: the projection the file declares, or `none`.

| Read | Stored as |
|---|---|
| `GPano:ProjectionType` = `equirectangular` | `equirectangular` |
| `GPano:ProjectionType` = `cylindrical` | `cylindrical` |
| `GPano:ProjectionType` naming anything else | `unspecified` |
| No `ProjectionType`, but `GPano:UsePanoramaViewer`, `FullPanoWidthPixels` or `CroppedAreaImageWidthPixels` | `unspecified` |
| No `GPano` properties | `none` |

Three consequences follow from storing the projection rather than a bare yes/no, and all three
are the reason it is worth a byte:

- **A writer that omits `ProjectionType` still declares a panorama**, and reading it as an
  ordinary photograph would lose the file the feature exists for. The other `GPano` properties
  answer the question the flag is for even when the projection does not.
- **A stored number this build does not recognise still means panorama.** It is read back as
  `unspecified` rather than discarded to `none`, so a value written by a later build does not
  silently un-declare a file for an earlier one.
- **The value is persisted, so its meaning is fixed once assigned.** Retire one by leaving its
  number unused; never reuse it. Honouring equirectangular projection is a renderer rather than
  a flag and is [not in this release](v-next.md) — but the file's own answer is recorded now, so
  the release that does honour it needs no second re-index.

Aspect ratio is deliberately **not** part of this. A 2:1 crop of a landscape is not a panorama,
and the index stores what the file claims; anything wanting to guess from shape can do so from
the dimensions the index already holds, at query time, without a stored field that would then
disagree with the file.

`@panorama`, and its short spelling `@pano`, ask for it. Both canonicalize to `@panorama`, so
there is one vocabulary to learn — the same rule [locations](locations.md) applies to `@remote`.

## Where this lives

| Standard or stage | Source |
|---|---|
| The date pack, its sources and the one resolution | [model_dates.h](../src/model_dates.h) — `date_source`, `date_sources`, `date_pack` |
| The panorama declaration | [model_property.h](../src/model_property.h) — `panorama_projection`; read in [metadata_xmp.cpp](../src/metadata_xmp.cpp) |
| EXIF read and write | [metadata_exif.h](../src/metadata_exif.h), [metadata_exif.cpp](../src/metadata_exif.cpp) |
| IPTC read | [metadata_iptc.h](../src/metadata_iptc.h), [metadata_iptc.cpp](../src/metadata_iptc.cpp) |
| XMP read and write, sidecars | [metadata_xmp.h](../src/metadata_xmp.h), [metadata_xmp.cpp](../src/metadata_xmp.cpp) |
| ICC profile interpretation | [metadata_icc.h](../src/metadata_icc.h), [metadata_icc.cpp](../src/metadata_icc.cpp) |
| Container tags read through FFmpeg | [av_format.cpp](../src/av_format.cpp) |
| Which properties exist, and how each is named and formatted | [model_property.h](../src/model_property.h), [model_property.cpp](../src/model_property.cpp) |
| How the pack is stored and read back | [model_db_pack.h](../src/model_db_pack.h), [model_db.cpp](../src/model_db.cpp) |
| Keyword tag sets | [model_tags.h](../src/model_tags.h), [model_tags.cpp](../src/model_tags.cpp) — `tag_set` |
| Per-format scanning and dispatch | [files_core.cpp](../src/files_core.cpp), [files_scan_photo.cpp](../src/files_scan_photo.cpp), and the `files_*` decoder for the format |
| How a write reaches disk | [file-io.md](file-io.md), [app_util.cpp](../src/app_util.cpp) |

The mapping table above is the authority for *which tag*; the source is the authority for the exact
tag constant. [app_gen_docs.cpp](../src/app_gen_docs.cpp) generates the published format and codec
list from the live dispatch tables, so that page cannot drift from the code — this document's
mapping is maintained by hand and can, which is why `/test:*metadata*` and `/test:*tag*` matter here
more than elsewhere.
