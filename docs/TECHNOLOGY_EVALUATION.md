# Technology Evaluation

Decisions made at project start (2026-09-25). Version facts were checked
against GitHub release APIs and the MSYS2 package index on that date; the rest
is engineering judgement, recorded so it can be revisited.

## Geometry kernel: Open CASCADE Technology (OCCT) — chosen

- **Versions:** upstream 8.0.1 (2026-07-30) is current; 8.0.0 shipped
  2026-05-07. MSYS2 ships **7.9.3**, which OpenShape builds against today.
- **Why:** the only mature open-source exact B-rep kernel with booleans,
  fillets, chamfers, offsets/shelling, lofts/sweeps, STEP/IGES/STL, BRep
  serialization, tessellation and full topology traversal. Proven in FreeCAD,
  KiCad (STEP), CadQuery/build123d.
- **License:** LGPL-2.1-or-later WITH OCCT-exception-1.0 — permits dynamic
  linking from software under any license.
- **Platforms:** Windows/macOS/Linux; packaged by MSYS2, vcpkg (`opencascade`),
  Conan, Homebrew and Linux distros.
- **Costs:** large API surface, exceptions for control flow, some operations
  (fillets on complex geometry) fail in ways that need careful wrapping. This
  is why all OCCT use is confined to `src/geometry` behind `Result` APIs.
- **8.0 migration:** code avoids deprecated APIs; moving to 8.x is expected to
  be mechanical. Tracked in TECHNICAL_DEBT.md.
- **3MF:** OCCT has no 3MF writer. Plan: write 3MF (ZIP + XML mesh) ourselves
  from tessellation in Milestone 5; it is a simple, open format.

## UI framework: Qt 6 + Qt Quick (QML) — chosen

| Option | Verdict |
|---|---|
| **Qt 6 Quick/QML** | Chosen. Native on Win/macOS/Linux + iOS/Android path; GPU scene graph; first-class touch, pen and high-DPI; animation and custom controls are idiomatic; `QQuickRhiItem` embeds our own renderer in the scene so QML overlays compose over the model. LGPL-3. |
| Qt 6 Widgets | Mature but desktop-centric; touch and fluid animation are weak; tends toward the "1990s toolbar CAD" look we want to avoid. |
| Dear ImGui | Excellent for tools, not for a polished consumer UI with native text input, accessibility and touch. |
| Web UI (Electron/Tauri + WebGL) | Would force the kernel into WASM or IPC; a product-level change (native → browser). Not taken. |
| Flutter / Compose Multiplatform | Good UI, but C++ kernel integration and custom GPU viewport are far less direct than Qt. |
| Slint | Promising, lighter, but no equivalent of an embedded custom RHI item and a smaller ecosystem today. |

Qt 6.11.2 from MSYS2 is used. Minimum supported: **6.8** (LTS; needed for
current CMake policies and `QQuickRhiItem`, which appeared in 6.7).

## Renderer: own QRhi renderer inside Qt Quick — chosen

| Option | Verdict |
|---|---|
| **QRhi via `QQuickRhiItem`** | Chosen. One code path for D3D11/12, Vulkan, Metal and OpenGL; shaders written once (GLSL 440 → `.qsb`). Full control over highlighting, edges, manipulators. Picking is done on the CPU against our own meshes, so face/edge ids are ours (stable within a shape revision), not a renderer's. |
| OCCT AIS/V3d | Gives selection and highlighting for free, but is OpenGL-only, awkward to embed in Qt Quick, and its interaction model would fight our touch-first manipulators. |
| Qt Quick 3D | High level, but 1-px lines only, limited control of depth-offset edges and per-face highlight ranges. |
| Raw Vulkan/D3D | Most control, most code, per-platform backends. Unjustified now. |

Cost: QRhi headers come from `Qt6::GuiPrivate` ("limited compatibility"
API), tying binaries to the Qt minor version they were built with. Accepted.

## Dependency management: MSYS2 today, vcpkg manifest later

- No compiler, CMake, Qt or OCCT existed on the development machine and there
  are no admin rights. **MSYS2 UCRT64** installs into the user profile and
  ships prebuilt GCC 16, CMake 4.4, Ninja, Qt 6.11 (incl. shadertools), OCCT
  7.9.3, GTest, nlohmann-json and libzip — a working toolchain in minutes.
- **vcpkg** has all dependencies as ports but builds Qt and OCCT from source
  (hours) and needs MSVC. **Conan** similar. These remain the planned route for
  reproducible MSVC release builds and CI (see ROADMAP).
- The CMake project uses only `find_package`, so it is package-manager neutral.

## Constraint solver — PlaneGCS chosen (Milestone 1)

| Option | License | Notes |
|---|---|---|
| FreeCAD **PlaneGCS** | LGPL-2.1-or-later | Full constraint set incl. tangency, DOF analysis via QR. Depends on Eigen (MPL-2.0), Boost.Graph and FreeCAD `Base/` helpers, so it needs extraction/shimming (the `planegcs` WASM project did this). **Leading candidate.** |
| SolveSpace **libslvs** | GPL-3.0 | Solid, compact C API. GPL would force OpenShape to GPL — a product-level licensing decision. |
| Own solver (Newton / Levenberg–Marquardt on Eigen) | ours | Feasible for the M1 subset (coincident, horizontal, vertical, distance, radius), but the prompt rightly warns against writing a sophisticated solver ourselves. Possible stop-gap only. |

**Decision (2026-09-25): PlaneGCS.** It is compatible with every license option
(GPL, MPL, Apache) when linked dynamically, supports the full constraint set
we will need (tangency, equal, symmetric, angles), and reports degrees of
freedom and conflicting/redundant constraints. Integration cost turned out to
be small: the upstream files are vendored **unmodified**; FreeCAD-specific
headers (logging, export macros, precompiled header) and the tiny Boost.Graph
subset it uses (`connected_components`) are replaced by shims. It builds as
C++23 (upstream requirement) while OpenShape stays C++20.

## File format: own ZIP + JSON container

ZIP via **libzip** (BSD-3-Clause), JSON via **nlohmann/json** (MIT). See
docs/FILE_FORMAT.md.
