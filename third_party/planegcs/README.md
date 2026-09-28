# PlaneGCS (vendored)

FreeCAD's planar geometric constraint solver.

- Upstream: https://github.com/FreeCAD/FreeCAD/tree/main/src/Mod/Sketcher/App/planegcs
- Commit: 8a1bdba1dd2f15dc3bb774fbf97caa3e65f8ccd0 (2026-09-22)
- License: LGPL-2.1-or-later (see COPYING.LIB)
- Files in `Sketcher/App/planegcs/` are **unmodified** copies of upstream.

Integration is done entirely with shims so the upstream files can be replaced
by newer versions without merging:

| Shim | Replaces | Why |
|---|---|---|
| `Sketcher/SketcherGlobal.h` | FreeCAD export macro | exports via `WINDOWS_EXPORT_ALL_SYMBOLS` |
| `shim/FCConfig.h`, `shim/Base/Tools.h` | FreeCAD headers | included but unused |
| `shim/Base/Console.h` | FreeCAD logging | stderr when `OPENSHAPE_PLANEGCS_LOG` is set |
| `shim/boost_graph_adjacency_list.hpp` (+ `boost/graph/*`) | Boost.Graph | only `connected_components` is used; replaced by union-find with identical component numbering |
| `shim-public/boost/math/constants/constants.hpp` | Boost.Math | included (by public `Geo.h`) but unused |

FreeCAD's precompiled header (standard headers the sources rely on) is
reproduced with `target_precompile_headers`.

The library is built as a shared library (`libplanegcs.dll`) and linked
dynamically, satisfying the LGPL's replaceability requirement regardless of
OpenShape's own license. The iOS app cannot ship dynamic libraries, so
there it is linked statically; recipients relink it by rebuilding the app
from its public source code (docs/LICENSING.md, "The iOS app and the App
Store"). Only `src/sketch/` may include its headers.
