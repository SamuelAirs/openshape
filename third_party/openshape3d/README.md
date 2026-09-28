# OpenShape3D (MIT)

OpenShape3D (<https://github.com/laanlabs/openshape3d>) is an open-source CAD
app for iPhone and iPad by Laan Labs, under the MIT License (`LICENSE` here, a
byte-for-byte copy of theirs at commit be3f2fc). Nothing of it is vendored or
linked: a few of OpenShape's files adapt ideas and small pieces of its code,
and say so in their headers, after the MPL-2.0 notice:

> Portions adapted from OpenShape3D (https://github.com/laanlabs/openshape3d,
> commit be3f2fc), Copyright (c) 2026 Laan Labs and the openshape3d
> contributors, MIT License (third_party/openshape3d/LICENSE)

The MIT License asks that its copyright and permission notice be kept with
copies of substantial portions of the software; this folder and the file
headers do that in the source.

| OpenShape file | What it adapts | From (OpenShape3D, commit be3f2fc) |
| --- | --- | --- |
| `src/interaction/ViewCube.h`, `src/interaction/ViewCube.cpp` | The view cube's tap ray and slab intersection (`hitPose`, `intersectCube`), the corner fraction (0.55), and the face labels' visibility and fade (`faceLabels`: named when facing > 0.25); here with Z up and OpenShape's camera | `openshape3d/Rendering/OrientationCube.swift` |
| `src/ui/qml/AxisTriad.qml` | The idea only (no code): a drag that starts on the cube orbits the view while a tool is armed | `openshape3d/UI/ViewportView.swift` (`gestureDragBegan`) |
