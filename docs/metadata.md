# Metadata

This document owns the mapping between Diffractor properties and media metadata standards.
[Design](design.md) owns what metadata commands mean; [File I/O](file-io.md) owns staging, sidecars,
backups, and failure; [Third-party](third-party.md) owns the XMP toolkit fork.

Media files remain authoritative. The index stores derived property snapshots for search and display.

## Containers

| Container | Read | Write |
| --- | --- | --- |
| JPEG, TIFF | EXIF, IPTC, XMP | XMP reconciled to EXIF/IPTC where supported |
| PNG, WebP | EXIF/XMP where present | XMP |
| RAW photos | EXIF and embedded/sidecar XMP | Sidecar XMP |
| MP4, MOV, M4A | QuickTime/iTunes tags and XMP | XMP, movie header dates, ISO copyright |
| MP3 | ID3v2 and XMP | XMP reconciled to ID3v2 |
| AVI, WAV | RIFF INFO and XMP | XMP reconciled to RIFF INFO |
| ASF, WMV, WMA | Windows Media attributes and XMP | XMP reconciled to Windows Media attributes |
| HEIC, AVIF | EXIF and XMP | Not editable |
| MKV, WebM | Matroska tags | Not editable |

Read and write support are intentionally asymmetric. FFmpeg exposes more container tags than the XMP
toolkit can reconcile, so displaying a property does not prove it can be stored back into that
container. Per-format edit traits gate commands before the write path runs.

## Property Mapping

| Diffractor property | XMP | Photo metadata | Audio/video metadata |
| --- | --- | --- | --- |
| Title | Dublin Core title | IPTC Object Name | ID3 title, RIFF title, QuickTime title |
| Description | Dublin Core description | EXIF ImageDescription, IPTC Caption | ID3 comment, RIFF comment, container description |
| Comment and synopsis | XMP comment/description fields | EXIF UserComment where present | ID3 comment, long description |
| Tags | Dublin Core subject | IPTC keywords, Windows XP keywords | ID3 genre/keyword fields, RIFF keywords |
| Rating | `xmp:Rating` | EXIF/Windows rating fields | ID3 POPM, Windows shared rating, Matroska rating on read |
| Label | `xmp:Label` | XMP only | XMP only |
| Artist/creator | Dublin Core creator | EXIF Artist, IPTC By-line | ID3 artist, QuickTime artist, RIFF artist |
| Album/track/disc | Dynamic Media fields | n/a | ID3 and QuickTime album/track/disc fields |
| Copyright | Dublin Core rights | EXIF Copyright, IPTC CopyrightNotice | ID3, RIFF, ASF, and ISO copyright fields |
| Location text | IPTC Core location fields | IPTC/XMP city, state, country | XMP where the container supports it |
| Coordinate | EXIF GPS and XMP location | EXIF GPS latitude/longitude/altitude | ISO6709 location where exposed |

String alternatives are normalized into one property snapshot. XMP generally has precedence over
legacy fields because it is the representation Diffractor writes; format-specific readers retain
legacy values when no higher-priority value exists. Ratings are normalized to zero through five.
The QuickTime `rtng` atom is a content-advisory flag, not a star rating.

## Dates

Diffractor exposes three date concepts:

| Date | Meaning | Typical sources |
| --- | --- | --- |
| Original | When the depicted or recorded content was made | EXIF DateTimeOriginal, XMP CreateDate, GPS date/time |
| Created | When this media file or encoding was made | EXIF DateTimeDigitized, container creation time, filesystem creation |
| Modified | When the file last changed | XMP ModifyDate, container modification time, filesystem modification |

The scanner retains candidate values with source identity and precision, then resolves one answer per
concept using the precedence in `model_dates.h`. A timezone-qualified instant outranks an otherwise
equivalent floating local value. Invalid, placeholder, and out-of-range dates are ignored.

The date pack is self-describing: readers consume fields they understand and skip later additions.
Source-bit meanings are never reused. Compatibility fields permit older builds to recover useful
dates when they encounter a newer cache row; the cache remains disposable and may be rescanned.

Writes update the standard matching the property and container. File-system dates are not silently
substituted for authored metadata during a metadata save.

## Photo Metadata

EXIF supplies camera, lens, exposure, dimensions, orientation, resolution, color space, GPS, and
capture dates. IPTC supplies editorial title, caption, creator, copyright, location text, and
keywords. XMP can override or extend both and carries Diffractor's editable rating, label, tags,
description, and location values.

Orientation affects presentation and is therefore not treated as display-neutral metadata. Embedded
thumbnails and ICC profiles are parsed with the image and invalidated when their source metadata
changes.

Unknown tags are not rewritten merely because Diffractor does not display them. XMP updates begin
from the existing packet and reconcile only fields managed by the toolkit and Diffractor's fork.

## Audio And Video Metadata

FFmpeg container dictionaries are normalized into shared properties. Language-suffixed comment and
description keys are selected by plain value, UI language, then any available value. Container
duration, streams, codecs, bitrate, frame rate, and pixel/audio format are technical properties and
are not written through the user metadata editor.

A video's shape also comes from the container rather than from its frames: the sample aspect ratio
gives the displayed dimensions for anamorphic video, and the display matrix gives the orientation.
Both are read without decoding, so a scan that never opens a frame describes the same shape the
player does.

The XMP toolkit reconciles a bounded legacy subset. In particular, MP4/MOV writes do not rewrite the
full iTunes atom set, so software that ignores XMP may continue to show an older atom value.
Matroska metadata is read-only.

## Current Limitations

- HEIC, AVIF, JXL, MKV, and WebM do not expose editable metadata traits.
- MP4/MOV legacy iTunes atoms are broader on read than on write.
- ID3 POPM ratings are recognized only for supported owner conventions.
- A fixed number of date candidates is retained; excess sources remain marked present but may not
  participate in resolution.

## Panorama

An image is a panorama only when GPano metadata declares it. `ProjectionType` maps to
equirectangular, cylindrical, unspecified panorama, or none. Other GPano fields can establish an
unspecified panorama when projection type is absent. Aspect ratio alone never declares one.

Full panorama dimensions and crop offsets are read when the item is displayed. The crop must agree
with decoded dimensions and remain inside the declared full panorama; otherwise projection geometry
is discarded. These values feed [zoom.md](zoom.md#panorama) and are not written as derived metadata.

`@panorama` and `@pano` search the stored projection classification and canonicalize to one term.

## Where this lives

- [model_dates.h](../src/model_dates.h): date sources, retained candidates, and resolution.
- [metadata_exif.cpp](../src/metadata_exif.cpp), [metadata_iptc.cpp](../src/metadata_iptc.cpp), and
  [metadata_xmp.cpp](../src/metadata_xmp.cpp): standard-specific mapping and reconciliation.
- [av_format.cpp](../src/av_format.cpp): audio/video container metadata.
- [model_property.cpp](../src/model_property.cpp): shared property identity and formatting.
- [model_db_pack.h](../src/model_db_pack.h) and [model_db.cpp](../src/model_db.cpp): cached metadata
  serialization.
- [files_core.cpp](../src/files_core.cpp) and [files_scan_photo.cpp](../src/files_scan_photo.cpp):
  format dispatch and scan publication.