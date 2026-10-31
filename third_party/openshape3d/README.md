# OpenShape3D (adapted code)

Parts of OpenShape's geometry code, its tests and the view cube are adapted from OpenShape3D,
an iOS CAD app by Laan Labs.

- Upstream: https://github.com/laanlabs/openshape3d
- Commit: be3f2fc8eea7a5a42d4821c560a0bdbd2a560385 (2026-09-19)
- License: MIT (`LICENSE` in this folder, a byte-for-byte copy of theirs;
  its closing note about OpenCASCADE concerns their app, OpenShape's own
  OpenCASCADE terms are in docs/LICENSING.md)
- Copyright (c) 2026 Laan Labs and the openshape3d contributors

No upstream file is copied whole. The functions below were rewritten in
OpenShape's style (C++20, our Result/Status errors, the kernel lock, our own
comments and messages) from the upstream functions named. Each of our files
that contains adapted code keeps the MPL-2.0 notice first, followed by the
OpenShape3D copyright line and a pointer to this license; each adapted
function names the function it comes from. OpenShape3D's name, logo,
screenshots, sample models and tutorials are not used.

## Adapted from

`openshape3d/Kernel/OCCT/OCCTBridge.mm` at the commit above:

| Upstream (lines) | What | In OpenShape |
|---|---|---|
| `OS3DFiniteBounds` (280-291) | refuse shapes with non-finite bounds | `src/geometry/internal/BrepScreen.cpp` `finiteBounds` |
| `OS3DHasTriangulation`, `OS3DWithoutMesh`, `OS3DIsValid` (293-331) | judge validity without the display mesh | `src/geometry/Modeling.cpp` `validGeometry` |
| `OS3DHealAndValidate`, `OS3DMaxTolerance` (333-350) and the tolerance cap of the boolean path (2007-2034) | one ShapeFix repair, accepted only under a tolerance cap | `src/geometry/Modeling.cpp` `finishSolid` |
| `OS3DDeadlineProgress` (376-401) | a progress indicator that stops runaway kernel algorithms | `src/geometry/internal/Deadline.h` `KernelDeadline` |
| `OS3DUnified` (408-418) and the boolean result path (1953-1996) | same-domain merge on a copy, unmerged result kept when the merge breaks it | `src/geometry/Modeling.cpp` `unifyCopy` |
| fuzzy boolean ladder (1891-1942) | fuzzy value from the operands' size, one retry at 10x | `src/geometry/Modeling.cpp` `runBoolean` |
| `OS3DBlendableEdges` (2320-2363), `OS3DEdgeBlended` and `OS3DFinishBlend` (2372-2419) | screen fillet/chamfer edges, refuse partial blends | `src/geometry/Modeling.cpp` `blendableEdge`, `edgeBlended`, `tryFillet`, `tryChamfer` |
| radius search when the tiny size fails (3023-3040) | halve down from the failed size | `src/geometry/Modeling.cpp` `detail::largestWorkingSize` |
| `OS3DVolume` (3110-3121) | adaptive Gauss-Kronrod volume | `src/geometry/Modeling.cpp` `exactVolume` |
| `OS3DSplitAtC0`, `OS3DSetSameRangeOnNewEdges`, `OS3DImagesThrough` (3216-3271) and the shell retries (3436-3509) | shell retries: split C0 faces, then intersection joins | `src/geometry/Modeling.cpp` `tryShell` |
| context statuses of the health check (3844-3877) | validity statuses stored against a parent shape | `src/geometry/Modeling.cpp` `describeCheckFailures` |
| `OS3DPlausibleSectionCounts` (3986-4119) | screen untrusted BRep text before OCCT reads it | `src/geometry/internal/BrepScreen.cpp` `plausibleBrepText` |

Tests (ideas and mutation classes):

| Upstream | In OpenShape |
|---|---|
| `openshape3dTests/OpsLeaveInputsUntouchedTests.swift` (serialize inputs before and after, without the flag lines) | `tests/test_ops_inputs_untouched.cpp` |
| `openshape3dTests/OCCTFuzzTests.swift` (inflated counts, references out of range, non-finite numbers, non-text bytes, truncation) | `tests/test_brep_fuzz.cpp` |

## Our files with adapted code

- `src/geometry/Modeling.cpp`
- `src/geometry/internal/Deadline.h`
- `src/geometry/internal/BrepScreen.h`, `src/geometry/internal/BrepScreen.cpp`
- `tests/test_ops_inputs_untouched.cpp`
- `tests/test_brep_fuzz.cpp`
- `src/interaction/ViewCube.h`, `src/interaction/ViewCube.cpp`

## The view cube

`openshape3d/Rendering/OrientationCube.swift` and `openshape3d/UI/ViewportView.swift` at the commit above:

| OpenShape file | What it adapts | From (OpenShape3D, commit be3f2fc) |
| --- | --- | --- |
| `src/interaction/ViewCube.h`, `src/interaction/ViewCube.cpp` | The view cube's tap ray and slab intersection (`hitPose`, `intersectCube`), the corner fraction (0.55), and the face labels' visibility and fade (`faceLabels`: named when facing > 0.25); here with Z up and OpenShape's camera | `openshape3d/Rendering/OrientationCube.swift` |
| `src/ui/qml/AxisTriad.qml` | The idea only (no code): a drag that starts on the cube orbits the view while a tool is armed | `openshape3d/UI/ViewportView.swift` (`gestureDragBegan`) |

The app's About → Licenses view shows the MIT text with every binary
("OpenShape3D (Laan Labs)", the `openshape3d` entry of
`scripts/licenses/components.json`); `licenses_check` fails for a
`third_party/` folder that no component lists.
