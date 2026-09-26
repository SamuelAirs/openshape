# The .openshape project format (version 1)

A ZIP archive (deflate) with these entries:

| Entry | Required | Content |
|---|---|---|
| `document.json` | yes | The parametric document. **Source of truth.** |
| `metadata.json` | no | `{ "format", "version", "application", "applicationVersion" }`; `applicationVersion` is the version of the OpenShape that wrote the file (CMake's project version, e.g. `"0.1.0"`); informational, not used when reading. |
| `imports/<feature-uuid>.brep` | when named | The exact geometry of an imported body (OCCT BRep text, no triangulation), named by its `Imported` step. **Part of the model**, like document.json: written on every save, recovery copies included. |
| `geometry/<body-uuid>.brep` | no | OCCT BRep text of each body's current shape. A cache for external tools and future fast-open; ignored on load (bodies are recomputed from features). |
| `thumbnail.png` | no | Preview of the model, written by every Save (not by recovery copies): 256 x 256 PNG with alpha, the visible bodies from the isometric direction framed to fill it, on a transparent background. Readers treat it as untrusted: at most 4 MiB, PNG signature checked; `io::readProjectThumbnail` reads only this entry. |

## document.json

```json
{
  "format": "OpenShape",
  "version": 1,
  "lengthUnit": "mm",
  "angleUnit": "rad",
  "displayUnit": "mm",
  "id": "5f0c…",
  "sketches": [
    {
      "id": "c21d…", "name": "Sketch 1", "visible": true, "hostBody": null, "attachment": null, "nextId": 18,
      "plane": { "origin": [0,0,0], "xAxis": [1,0,0], "yAxis": [0,1,0] },
      "points": [ { "id": 1, "x": 0, "y": 0, "fixed": true }, { "id": 2, "x": 60, "y": 0, "fixed": false } ],
      "lines": [ { "id": 6, "start": 1, "end": 2, "construction": false } ],
      "circles": [ { "id": 12, "center": 11, "radius": 3.0, "construction": false } ],
      "arcs": [ { "id": 17, "center": 14, "start": 15, "end": 16, "construction": false } ],
      "constraints": [ { "id": 10, "type": "HorizontalDistance", "a": 1, "b": 2, "value": 60.0 } ]
    }
  ],
  "bodies": [
    {
      "id": "8a1e…", "name": "Body 1", "visible": true,
      "features": [
        { "id": "…", "type": "Box", "name": "", "suppressed": false,
          "params": { "origin": [-10, -10, 0], "size": [20, 20, 20] } },
        { "id": "…", "type": "PushPull", "name": "", "suppressed": false,
          "params": { "distance": 15.0,
                      "face": { "indexHint": 5, "surface": 0,
                                "normal": [0,0,1], "centroid": [0,0,20], "area": 400.0 } } },
        { "id": "…", "type": "Fillet", "name": "", "suppressed": false,
          "params": { "size": 3.0,
                      "edges": [ { "indexHint": 1, "curve": 0, "midpoint": [10,-10,17.5],
                                   "tangent": [0,0,1], "length": 35.0 } ] } }
      ]
    }
  ]
}
```

Rules:

- **All stored lengths are millimeters** and all angles radians, regardless of
  `displayUnit` (which is only the UI's default input/display unit). A reader
  must reject any other `lengthUnit`.
- The first feature of every body must be a base feature (`Box`,
  `SplitPiece`, `Copy`, `Imported`, or `Extrude` / `Revolve` with mode
  `NewBody`).
- Sketch entity ids are integers unique within their sketch; id 1 is always
  the fixed origin point. `nextId` is the next unused id. Sketch coordinates
  are millimeters in the plane's (xAxis, yAxis) frame.
- `arcs` (optional; older files have none): `{ "id", "center", "start",
  "end", "construction" }` — point ids; the arc runs counterclockwise from
  start to end around center.
- Constraint types (entity ids in `a`, `b`; dimensions in `value`):
  `Coincident`, `Horizontal`, `Vertical`, `Distance`, `HorizontalDistance`
  and `VerticalDistance` (signed: b − a), `Diameter` (circle), `Radius` (arc,
  `value` > 0), `Parallel`, `Perpendicular`, `Equal` (two lines or two
  circles/arcs), `Tangent`, `Concentric`, `PointOnLine` (point `a` on line
  `b`), `Midpoint` (point `a` at the middle of line `b`), `PointOnCircle`
  (point `a` on the circle of circle or arc `b`), `Symmetric` (points `a`
  and `b` mirror images across line `c`; neither may be an end of `c`),
  `Angle` (lines `a`, `b`; `value` = the signed angle in radians,
  counterclockwise, from a's direction (start → end) to b's: the UI shows
  it as the angle between the lines at their corner).
  Only constraints with a third entity write `"c"` (an entity id); readers
  treat a missing `"c"` as none and refuse it on any other type. A `Tangent`
  between a line or arc and an arc sharing an end point means a smooth join
  there. Unknown constraint types make the file unreadable with a "newer
  version" message.
- `Extrude` params: `{ "sketch": uuid, "profiles": [{ "point": [x, y], "area" }],
  "distance", "mode": "NewBody" | "Join" | "Cut" }`. Profiles are referenced by
  a point inside the region (sketch coordinates) plus its area.
- `Extrude` may carry `"throughAll": true` (cuts only): the cut extends
  through the whole body in the direction of `distance`; and
  `"symmetric": true`: centered on the sketch plane, `|distance|` being the
  total thickness; and `"draft": { "angle": radians, "distance" }` (absent
  = no draft; |angle| <= 89 degrees): the side walls lean in by that angle
  as they go away from the sketch (both ways when symmetric; negative leans
  out), corners staying sharp. A drafted extrusion writes its `distance`
  **only** inside `draft` (both places: refused), so builds that predate
  drafts refuse the file ("invalid extrusion") instead of extruding
  straight walls. A draft with `throughAll` is refused.
- `PushPull` params: `{ "face": faceRef, "distance" }` plus optional
  `"keepEdges": true` (fillets and chamfers around the face move with it
  where possible; steps without it are the plain prism + boolean).
- A sketch placed on a face has `"attachment": { "body", "feature",
  "faceHint", "normal", "centroid", "area" }` identifying the face (on the
  output of `feature`); its `plane` is then derived from that face and the
  stored plane is the last resolved one.
- Sketches are stored before bodies and loaded first, because extrusions
  look them up during the initial recompute.
- UUIDs must be unique across the document.
- `surface` / `curve` are enum ordinals (`geom::SurfaceKind`, `geom::CurveKind`).
  They must never be renumbered; new kinds are appended.
- `Shell` params: `{ "faces": [faceRef…], "thickness" }`. `Move` params:
  `{ "translation": [x, y, z] }`, plus an optional `"rotation": { "center":
  [x, y, z], "axis": [x, y, z], "angle": radians }` applied before the
  translation (Rotate and Align steps; Align steps are named "Align"). A
  rotation without a valid axis or finite angle makes the file unreadable.
  `Combine` params: `{ "tool": body uuid,
  "mode": "Union" | "Subtract" | "Intersect" }` — the tool body is usually
  hidden (consumed) but stays in the document.
- `Revolve` params: like `Extrude` (sketch, profiles, mode) plus `"axis": "X" |
  "Y"` (the sketch's own axes through its origin) and `"angle"` in radians
  (0, 2π].
- `Mirror` params: `{ "origin": [x, y, z], "normal": [x, y, z] }` — the body
  plus its mirror image across that plane, joined.
- `Pattern` params: `{ "layout": "Linear", "count", "direction": [x, y, z],
  "spacing" }` or `{ "layout": "Circular", "count", "axisOrigin": [x, y, z],
  "axis": [x, y, z], "angle" }` (radians; 2π spaces copies evenly). `count`
  includes the original (1–500); copies are joined.
- `DeleteFaces` params: `{ "faces": [faceRef…] }` — removed and healed.
  `OffsetFace` params: `{ "face": faceRef, "distance" }` (positive: the body
  grows along the face's outward normal).
- `Hole` params: `{ "rim": edgeRef, "diameter", "depth", "preset", "type"?,
  "angle"? }` — made at a circular rim edge, into the material (the
  direction comes from the flat face next to the rim); `preset` is an
  informational label such as "M3 heat-set insert" or "M3". `type` is
  absent (or `"Plain"`) for a cylinder of `diameter` x `depth` (heat-set
  insert pilot holes; files from before counterbores compute exactly as
  before); `"Counterbore"`: the same cylinder as a screw head's seat on the
  existing hole (refused when not wider than the hole or reaching through
  the part); `"Countersink"`: a cone of `diameter` at the surface with the
  included `angle` (radians, 90 degrees for metric screws) down to the hole,
  written **without** `depth`, so builds that predate countersinks refuse
  the file instead of drilling a plain hole of the countersink's diameter.
  Unknown `type` values are refused.
- `Split` params: `{ "pieces": [solid…] }` (at least two), where a solid is
  `{ "volume", "centroid": [x, y, z], "min": [x, y, z], "max": [x, y, z] }`
  (volume > 0, center of mass, bounding box) as the pieces were when the body
  was split into bodies. The body keeps `pieces[0]`, plus any piece its input
  gained since; the other recorded pieces are the first steps of other
  bodies. Pieces are found again by nearest signature (`geom::matchSolids`).
- `SplitPiece` params: `{ "body": uuid, "split": uuid, "piece" }` — a base
  feature: piece `piece` (≥ 1) of the Split step `split` of body `body`,
  taken from that body's shape just before the step. It fails (with a
  message) when the piece no longer exists or is no longer separate, when the
  split step is suppressed or deleted, or when the body is gone.
- `Copy` params (a base feature: Mirror / Pattern with "Separate bodies"):
  `{ "body": uuid, "mirror": { "origin": [x, y, z], "normal": [x, y, z] } }`
  — the mirror image of that body's current shape — or `{ "body": uuid,
  "translation": [x, y, z] }` plus an optional `"rotation"` like `Move`'s
  (applied before the translation) — the body's shape moved. It follows
  every change of that body and fails with a message when the body is gone.
- `Holes` params: `{ "face": faceRef, "positions": [[x, y], ...],
  "diameter", "throughAll", "depth"?, "head"?, "headDiameter"?,
  "headDepth"?, "headAngle"?, "preset" }` — round holes drilled into a flat
  face (the Hole tool; one step for the set, 1 to 1000 positions). The
  positions are in the face's frame: on its plane, origin the world origin
  projected onto it, x axis horizontal (world X on floors; the same frame
  as a sketch started on the face), so the holes follow the face when an
  upstream step moves it; a position no longer on the face fails the step.
  `depth` only when `throughAll` is false. `head` is `"Counterbore"`
  (with `headDiameter`, `headDepth`) or `"Countersink"` (with
  `headDiameter`, `headAngle` in radians); absent: no head. `preset` is an
  informational label such as "M3 normal fit".
- `Imported` params (a base feature: a body imported from a STEP file):
  `{ "geometry": "imports/<feature uuid>.brep", "hash", "volume", "source" }`
  — the archive entry holding the exact geometry (millimeters, placed as in
  the file it came from); `hash`, 16 lowercase hex digits, the FNV-1a (64
  bit) of that entry's bytes; `volume` (> 0, mm³) of the solid; `source`,
  the imported file's name (informational, at most 1024 bytes). The loader
  reads the entry, compares its hash before parsing it, and refuses the
  project ("damaged") when the entry is missing, does not match, is not a
  valid solid or no longer has the recorded volume (within 1e-6). Writers
  name the entry after the step's own id; readers accept any `imports/`
  entry the step names, but each entry for one step only (two steps naming
  one entry make the file damaged: a crafted file must not make the loader
  read and keep one entry many times). Size limits: at most 256 MiB per
  entry and 512 MiB of imported geometry per project
  (`doc::kMaxImportedBodyBytes`, `doc::kMaxImportedGeometryBytes`). The
  loader reads no more than that; the writer refuses to save more (a plain
  message) rather than write a file it could not open, and the importer
  refuses parts that would not fit.
- Feature types: `Box`, `PushPull`, `Fillet`, `Chamfer`, `Extrude`, `Shell`,
  `Move`, `Combine`, `Revolve`, `Hole`, `Mirror`, `Pattern`, `DeleteFaces`,
  `OffsetFace`, `Split`, `SplitPiece`, `Copy`, `Holes`, `Imported`. Unknown
  types make the file unreadable with a "newer version" message (never
  silently dropped).

## Versioning

Version 1 is not frozen until the first public release; until then it may
gain fields (as it did for sketches) without a version bump.

- `version` is an integer. Readers refuse files with a higher version than they
  support ("created by a newer version of OpenShape").
- Older versions are upgraded in memory by a chain of migrations
  (`io/ProjectFile.cpp`, `migrations()`), one per version step. Files are only
  written in the current version.
- Any change to the meaning of existing fields requires a version bump and a
  migration. Adding optional fields with safe defaults does not.

## Security

Project files are untrusted input:

- file ≤ 1 GiB, entries ≤ 256 MiB, ≤ 10 000 entries;
- every entry name is validated (no absolute paths, drive letters, backslashes,
  `.`/`..` components) and the file is rejected otherwise;
- nothing is extracted to disk — entries are read into memory only;
- `document.json` may nest at most 256 levels (it uses about 8): deeper
  nesting is refused before parsing, since copying such a value would
  overflow the stack;
- JSON is parsed without exceptions and validated field by field (wrong
  types, e.g. a `"fixed": "yes"`, are format errors, never defaults);
  sketch entity ids and `nextId` must be at most 2^30;
- references that do not resolve (a missing sketch, tool body or face)
  load as failed steps with a message; dependency cycles between bodies
  stop after a bounded number of recomputes;
- a step that changes nothing (a cut beside the body or a subtraction of a
  body that does not touch, which older versions could save) loads as a
  step with a warning, and the steps after it still build;
- nothing in a project file is ever executed.

## Recovery copies (not user files)

While a document has unsaved changes, the app keeps a recovery copy in its
own data folder (`<AppLocalData>/recovery/`, see `io/Recovery.h`); the
user's file changes only on Save. Per running app instance (a *session*,
named by a lowercase UUID):

| File | Content |
|---|---|
| `<session>.openshape` | A normal project file as above, written without the `geometry/` cache (with `imports/`: imported geometry is part of the model). |
| `<session>.json` | Sidecar: `{ "format": "OpenShapeRecovery", "version": 1, "originalPath", "title", "savedAt" (Unix ms), "appVersion" }`. `originalPath` is the user's file (UTF-8, empty if never saved). |
| `<session>.lock` | A `QLockFile`: the session's app is running. A lock whose process is gone marks a crashed session, whose copy is offered for restoring. |

Both files are written atomically (temp file + rename), the copy first. A
copy without a readable sidecar is still offered (as "Untitled"). The copy
stays after the app ends if the document still had unsaved changes the user
did not discard; the next start offers it. Restoring moves the copy to the
new session and writes that session's sidecar anew. Sidecars
are untrusted: strict types, length limits, unknown fields ignored.
