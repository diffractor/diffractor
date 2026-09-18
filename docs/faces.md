# Diffractor Faces

This document owns the face foundation: the face vector, alignment, resemblance grouping, the sample
face that identifies a group, the way a face is written down, and the form a face set is stored in.

**Nothing in this document is reachable by a user in this release.** There is no detector, no
embedding model, no background pass, no search term and no user interface. What ships is the
computation that those things would be built on, kept because it is settled, tested and expensive to
re-derive. The feature that uses it lives on the `face-search` branch and is expected in one of the
next few releases. Until it lands, no face is detected, no vector is computed, and the `item_faces`
table stays empty.

Collection membership is owned by [collections](collections.md), background execution and publication
by [implementation](implementation.md), media decoding by [file I/O](file-io.md), and dependency
provenance by [third-party](third-party.md).

## 1. What a face is here

A face is derived data. Everything below is computed from a picture and rebuildable by reading it
again; none of it is user input, and none of it is written to a media file or a sidecar.

| Data | Meaning |
|---|---|
| Detection | One face in one file: bounds, five landmarks, and a detector score |
| Face vector | The normalized 128-value embedding of an aligned detection |
| Group | Faces resembling one sample face |

A group is an appearance cluster, not a person. There is no name, no label, and no identity claim: a
resemblance search cannot support one, and half a collection labelled looks like a job abandoned
rather than a job not started.

Derived data carries the joint detector, alignment-template and embedding-model version, so a change
to any one of them makes every stored vector detectably stale rather than silently wrong.

## 2. Geometry

Stored geometry is **upright**: the space the picture is displayed in, not the grid it was decoded
on. Most formats decode in the file's own orientation, so a portrait photograph detects on a
landscape surface. Only what is written down is turned. Without this a box would be on its side the
moment anything drew one, and a face locator would mean a different part of the picture depending on
the format it arrived in. This is the same space a region the user draws is already kept in, and the
inverse of `edit_view_state::crop_from_displayed_rect`.

The faces in one file are held in a **stable order** — upright left to right, then top to bottom,
then larger first. Detector output order is not that: it is a function of how a model happened to
scan a frame, so a saved reference would move to a different face after a re-detection. Because the
order is part of what a reference means, it is part of the stored model version.

## 3. Alignment

The five landmarks — both eye centres, the nose tip, then both mouth corners — are fitted by least
squares to a 112x112 reference template. The transform permits rotation, uniform scale and
translation, but no shear: a general affine has the freedom to shear a face into the template, which
destroys exactly the geometry an embedding reads. Least squares over all five pairs means one
misplaced landmark does not decide the result.

An alignment that would sample substantially outside the decoded surface is refused rather than
padded. A half-face at a frame edge embeds to something confidently similar to other half-faces at
frame edges — a group that is real, stable, and about nothing.

## 4. Comparison and grouping

Similarity is cosine similarity over normalized vectors. Vectors are normalized once when produced,
which makes every later comparison a dot product.

Grouping happens in two steps, and the second is what makes a group answerable.

Clusters are proposed first. Candidates are led in a deterministic quality order — detector
confidence, then crop size, then pose — and each one either joins the nearest existing cluster it is
within the threshold of, or anchors a new one. Anchors receive one mean-based refinement and then
stay fixed while membership is evaluated. A face outside every refined anchor gets a reference of its
own, so refinement cannot make it unsearchable.

Each cluster then adopts a **sample face** and takes that face's own vector as its anchor, and
membership is measured again against it. A mean vector is nobody's face: it could not be shown, it
could not be spelled, and a count taken against it would not equal what a search for the sample
returns. Adopting a real face makes the tile, the identity and the query the same thing. Two clusters
that settle on one sample face are one group.

Re-anchoring can strand a face, and a stranded face gets a group of its own. Refinement only promises
that every member is within the threshold of its cluster's mean, so two members either side of that
mean can be twice as far apart as the threshold allows. A face in no group can be reached by nothing,
which is why neither step is allowed to leave one behind.

Published groups are overlapping resemblance searches, not an exclusive partition. Every member must
directly meet the sample's threshold, so A resembling B and B resembling C does not make A a match
for C. The threshold trades missed matches against lookalikes; it does not separate identities.

## 5. The sample face

The sample is chosen to be worth showing and worth searching for: square-on rather than in profile,
large enough not to be an upscale, and typical of the group rather than its most photogenic outlier.
A face turned too far from the camera is passed over entirely while any member is square-on, because
a profile makes a poor tile and a worse reference. Crop size counts in absolute pixels, because as a
fraction of the frame it barely varies and a small blurred face would win from a large sharp one.
Ties break on path and face index, so the choice does not reshuffle between sessions.

Where two members are equally good, a sample whose file name is unique in the collection is
preferred, because a face is spelled as a file name and a name shared by two files names both faces.

## 6. How a face is written down

A face is spelled as the file's own name and the 1-based position of the face in that file's stable
order, with `:1` left off because most pictures hold one face and `IMG_1234.jpg` is the whole of what
there is to say about them. The folder is deliberately absent: a reference has to be readable and
typeable, so two files of one name spell one reference and it means both faces.

This spelling outlives grouping. Nothing in it refers to a cluster, an id or a label that a later
pass is free to change: it names a picture and a position, both of which mean the same thing a year
later.

## 7. Storage

A file's faces encode to one blob, stamped with the joint model version. A row from another version,
a malformed row, or a row older than its source file decodes to nothing and counts as unscanned.

The `item_faces` table exists in the schema and is currently written by nothing. It is declared now
so that the release which turns detection on needs no migration over a large database. Groups are
never stored: they are what a grouping pass proposes over the vectors on hand, so a database clean or
a model-version change costs the pass again and nothing else.

## Where this lives

| Concern | Source |
|---|---|
| Face values, alignment, similarity, grouping, sample selection, the face token, and storage encoding | [model_faces.h](../src/model_faces.h), [model_faces.cpp](../src/model_faces.cpp) |
| The reserved table | `src/Res/create_tables.sql` |
| Tests | [test_faces.cpp](../src/test_faces.cpp) |
