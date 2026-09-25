# Technical debt register

Intentional shortcuts, with the milestone by which each should be resolved.

| ID | Where | Debt | Why accepted | Resolve by |
|---|---|---|---|---|
| TD-1 | `interaction/SceneCache`, `Operation::setValue` | Tessellation and preview booleans run synchronously on the GUI thread. Measured (RelWithDebInfo, i5-13500) on a 120×120 plate with 100 holes: full recompute 186 ms, tessellation 104 ms (19k triangles), profile detection 51 ms, pick 1.9 ms. Fine for typical maker parts; noticeable while dragging on heavy parts. | Simplicity while the interaction model settles. | M3 |
| TD-2 | `selection/Picking.cpp` | Brute-force ray/triangle and edge-segment picking, O(triangles) per hover. | Correct and simple; models are small. | M3 (BVH) |
| TD-3 | `geometry/TopoSignature` | Signature-based topological naming only; can mis-resolve on symmetric parts after large upstream edits. | Awareness now, provenance naming when history editing ships. | M4 |
| TD-4 | `geometry/Tessellation.cpp` | `BRepMesh_IncrementalMesh` stores triangulation inside the shared TShape, so tessellating is a hidden mutation of an "immutable" `Shape`; not safe to tessellate one shape from two threads. | Single-threaded today. | Before TD-1 |
| TD-5 | `render/` | QRhi via `Qt6::GuiPrivate` ties binaries to the Qt minor version. | Only way to use QRhi; widely done. | Review per Qt upgrade |
| TD-6 | packaging | `scripts/package-windows.sh` produces a verified self-contained folder, but: no installer/zip/signing; ~290 MB because MSYS2's OCCT links its visualization toolkit (and through it ffmpeg/AV1 codecs) into the STEP translator; the full transitive license list is not generated yet. | First step done; size needs an OCCT build without TKService/ffmpeg. | Before first release |
| TD-16 | `geometry/Exchange.cpp` | OCCT's STEP writer prints transfer statistics to stdout instead of our log. | Harmless in the GUI. | M3 |
| TD-7 | `interaction/InteractionController::deleteSelectedBodies` | Deleting several bodies creates one undo step per body. | Rare; needs a macro command. | M3 |
| TD-8 | `io/ProjectFile.cpp` | The BRep geometry cache is written but never read. | Recompute is fast at current sizes. | When load times matter |
| TD-9 | `ui/qml/Main.qml` | Text-only buttons, no icon set. | Avoids fake polish; icons need a consistent original set. | M3 |
| TD-10 | `geometry/` | Built against OCCT 7.9.3; upstream is 8.0.1. | MSYS2 package availability. | When MSYS2/vcpkg ship 8.x |
| TD-11 | `AppController::saveProject` | No thumbnail in project files. | Needs an offscreen render path. | M3 |
| TD-12 | `sketch/SketchSolver.cpp` | A new PlaneGCS system is built for every solve, including every drag step. | Sketches are small; simplest correct approach. | When sketches get large |
| TD-13 | `geometry/Profiles.cpp` | Profile regions are recomputed (General Fuse + tessellation) after every sketch edit, on the GUI thread. | A few ms for typical sketches. | With TD-1 |
| ~~TD-14~~ | `sketch/` | ~~Sketches on faces did not follow the face.~~ **Resolved**: sketches carry an `Attachment` (body, feature, face signature); the plane is re-resolved during recompute and synced after every change. | | |
| TD-15 | `geometry/Profiles.cpp` `cleanFace` | A region whose *outer* wire carries a dangling edge keeps it (the splitter face is used as is). | Rare; extrusion still succeeds in tests. | M3 |
| TD-17 | packaging (**blocks distribution**) | MSYS2's OpenCASCADE links its visualization toolkit, which pulls in an FFmpeg built with `--enable-gpl` plus x264/x265 (GPL). `dist/OpenShape` therefore contains GPL-licensed DLLs OpenShape never uses. Shipping them imposes GPL obligations and conflicts with a non-GPL or App Store release. | Found 2026-09-25 while reviewing selling options. | Before any public download: build OCCT from source without FFmpeg/FreeImage (`USE_FFMPEG=OFF`, and without TKService/TKV3d if possible), then re-verify the package's DLL list and licenses. Also shrinks the package (TD-6). |
