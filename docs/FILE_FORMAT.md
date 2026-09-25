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
- The first feature of every body must be a base feature (`Box` today).
- UUIDs must be unique across the document.
- `surface` / `curve` are enum ordinals (`geom::SurfaceKind`, `geom::CurveKind`).
  They must never be renumbered; new kinds are appended.
- Feature types: `Box`, `PushPull`, `Fillet`, `Chamfer`. Unknown types make the
  file unreadable with a "newer version" message (never silently dropped).

## Versioning

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
