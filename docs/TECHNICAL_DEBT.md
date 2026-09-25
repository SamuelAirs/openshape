# Technical debt register

Intentional shortcuts, with the milestone by which each should be resolved.

| ID | Where | Debt | Why accepted | Resolve by |
|---|---|---|---|---|
| TD-1 | `interaction/SceneCache`, `Operation::setValue` | Tessellation and preview booleans run synchronously on the GUI thread. Fine for M0-size parts (a few ms); will stutter on large models. | Simplicity while the interaction model settles. | M3 |
| TD-2 | `selection/Picking.cpp` | Brute-force ray/triangle and edge-segment picking, O(triangles) per hover. | Correct and simple; models are small. | M3 (BVH) |
| TD-3 | `geometry/TopoSignature` | Signature-based topological naming only; can mis-resolve on symmetric parts after large upstream edits. | Awareness now, provenance naming when history editing ships. | M4 |
| TD-4 | `geometry/Tessellation.cpp` | `BRepMesh_IncrementalMesh` stores triangulation inside the shared TShape, so tessellating is a hidden mutation of an "immutable" `Shape`; not safe to tessellate one shape from two threads. | Single-threaded today. | Before TD-1 |
| TD-5 | `render/` | QRhi via `Qt6::GuiPrivate` ties binaries to the Qt minor version. | Only way to use QRhi; widely done. | Review per Qt upgrade |
| TD-6 | packaging | No deployment: running outside the MSYS2 environment needs Qt/OCCT DLLs and Qt plugins next to the exe (windeployqt), plus a license bundle. | Development builds only so far. | Before first release |
| TD-7 | `interaction/InteractionController::deleteSelectedBodies` | Deleting several bodies creates one undo step per body. | Rare; needs a macro command. | M3 |
| TD-8 | `io/ProjectFile.cpp` | The BRep geometry cache is written but never read. | Recompute is fast at current sizes. | When load times matter |
| TD-9 | `ui/qml/Main.qml` | Text-only buttons, no icon set. | Avoids fake polish; icons need a consistent original set. | M3 |
| TD-10 | `geometry/` | Built against OCCT 7.9.3; upstream is 8.0.1. | MSYS2 package availability. | When MSYS2/vcpkg ship 8.x |
| TD-11 | `AppController::saveProject` | No thumbnail in project files. | Needs an offscreen render path. | M3 |
