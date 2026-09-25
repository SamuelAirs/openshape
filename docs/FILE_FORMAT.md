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
      "id": "c21d…", "name": "Sketch 1", "visible": true, "hostBody": null, "attachment": null, "nextId": 14,
      "plane": { "origin": [0,0,0], "xAxis": [1,0,0], "yAxis": [0,1,0] },
      "points": [ { "id": 1, "x": 0, "y": 0, "fixed": true }, { "id": 2, "x": 60, "y": 0, "fixed": false } ],
      "lines": [ { "id": 6, "start": 1, "end": 2, "construction": false } ],
      "circles": [ { "id": 12, "center": 11, "radius": 3.0, "construction": false } ],
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
- The first feature of every body must be a base feature (`Box`, or
  `Extrude` with mode `NewBody`).
- Sketch entity ids are integers unique within their sketch; id 1 is always
  the fixed origin point. `nextId` is the next unused id. Sketch coordinates
  are millimeters in the plane's (xAxis, yAxis) frame.
- Constraint types: `Coincident`, `Horizontal`, `Vertical`, `Distance`,
  `HorizontalDistance` and `VerticalDistance` (signed: b − a), `Diameter`.
- `Extrude` params: `{ "sketch": uuid, "profiles": [{ "point": [x, y], "area" }],
  "distance", "mode": "NewBody" | "Join" | "Cut" }`. Profiles are referenced by
  a point inside the region (sketch coordinates) plus its area.
- `Extrude` may carry `"throughAll": true` (cuts only): the cut extends
  through the whole body in the direction of `distance`.
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
- Feature types: `Box`, `PushPull`, `Fillet`, `Chamfer`, `Extrude`, `Shell`,
  `Move`, `Combine`, `Revolve`. Unknown
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
