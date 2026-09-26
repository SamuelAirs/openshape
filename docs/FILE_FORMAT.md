# The .openshape project format (version 1)

A ZIP archive (deflate) with these entries:

| Entry | Required | Content |
|---|---|---|
| `document.json` | yes | The parametric document. **Source of truth.** |
| `metadata.json` | no | `{ "format", "version", "application", "applicationVersion" }` |
| `geometry/<body-uuid>.brep` | no | OCCT BRep text of each body's current shape. A cache for external tools and future fast-open; ignored on load (bodies are recomputed from features). |
| `thumbnail.png` | no | Preview image (not written yet). |

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
  `SplitPiece`, or `Extrude` / `Revolve` with mode `NewBody`).
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
  (point `a` on the circle of circle or arc `b`). Unknown constraint types
  make the file unreadable with a "newer version" message.
- `Extrude` params: `{ "sketch": uuid, "profiles": [{ "point": [x, y], "area" }],
  "distance", "mode": "NewBody" | "Join" | "Cut" }`. Profiles are referenced by
  a point inside the region (sketch coordinates) plus its area.
- `Extrude` may carry `"throughAll": true` (cuts only): the cut extends
  through the whole body in the direction of `distance`; and
  `"symmetric": true`: centered on the sketch plane, `|distance|` being the
  total thickness.
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
- `Hole` params: `{ "rim": edgeRef, "diameter", "depth", "preset" }` — a
  cylindrical hole centered on a circular rim edge, drilled into the
  material (the direction comes from the flat face next to the rim);
  `preset` is an informational label such as "M3 heat-set insert".
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
- Feature types: `Box`, `PushPull`, `Fillet`, `Chamfer`, `Extrude`, `Shell`,
  `Move`, `Combine`, `Revolve`, `Hole`, `Mirror`, `Pattern`, `DeleteFaces`,
  `OffsetFace`, `Split`, `SplitPiece`. Unknown
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
- JSON is parsed without exceptions and validated field by field;
- nothing in a project file is ever executed.
