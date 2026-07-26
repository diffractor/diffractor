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
| created | `photoshop:DateCreated`, `exif:DateTimeOriginal`, `exif:DateTimeDigitized` | `DATE_TIME_ORIGINAL`, `DATE_TIME_DIGITIZED`, `DATE_TIME` | |
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
| orientation | `tiff:Orientation` | `ORIENTATION` | |
| rating | `xmp:Rating`, `MicrosoftPhoto:Rating` | `IMAGE_RATING`, `IMAGE_RATING_PERCENT` | |
| source | `photoshop:Source` | | `SOURCE` |
| state | `photoshop:State` | | `STATE` |
| tag | `dc:subject` | `XP_KEYWORDS` | `KEYWORDS` |
| title | `dc:title` | `XP_TITLE` | `OBJECT_NAME` |
| year | `xmp:CreateDate` | | |

`xmp:Rating` holds a 0-5 star value and `MicrosoftPhoto:Rating` the equivalent percentage;
both are written so that Windows Explorer and Adobe applications agree.

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
| created, year | `©day` | `TYER`, `TDRC`, `TDRL` | `ICRD` | | |
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
