# Third-party software

OpenShape itself is licensed under MPL-2.0 (`LICENSE`); the components below
keep their own licenses (see also docs/LICENSING.md). Versions are those used
for the verified Windows build (MSYS2 UCRT64, 2026-09-25).

| Component | Version | License | Used for | Linked |
|---|---|---|---|---|
| Open CASCADE Technology | 7.9.3 | LGPL-2.1-or-later WITH OCCT-exception-1.0 | Exact B-rep kernel, STEP/STL | dynamic (releases: own build, `scripts/windows/build-occt.sh`) |
| Qt (Core, Gui, Qml, Quick, QuickControls2, ShaderTools) | 6.11.2 | LGPL-3.0-only (GPL exceptions for tools) | UI, QRhi rendering, shader compilation (build time) | dynamic |
| libzip | 1.11.4 | BSD-3-Clause | .openshape containers | dynamic |
| PlaneGCS (from FreeCAD, commit 8a1bdba1) | vendored | LGPL-2.1-or-later | 2D constraint solving | dynamic (`libplanegcs.dll`) |
| Eigen | 5.0.1 | MPL-2.0 | Linear algebra for PlaneGCS | header-only |
| nlohmann/json | 3.12.0 | MIT | JSON | header-only |
| GoogleTest | 1.18.0 | BSD-3-Clause | Tests only (not shipped) | – |
| GCC / MinGW-w64 runtime | 16.2.0 | GPL-3.0 with GCC Runtime Library Exception | Compiler and runtime | dynamic |
| FreeType | 2.14.3 | FTL (or GPL-2.0; used under FTL) | Fonts (OCCT's TKService, Qt) | dynamic |
| NSIS | 3.12 | zlib/libpng; its LZMA module CPL-1.0 with a linking exception | Windows installer (`packaging/windows/openshape.nsi`) | build tool; its installer stub is part of the setup .exe |

**Windows releases** (`msys2-ucrt64-release`) link OpenShape's own
OpenCASCADE build (`scripts/windows/build-occt.sh`: OCCT 7.9.3 from the
upstream tag with MSYS2's source patches, without FFmpeg, FreeImage, TBB,
VTK, Tcl/Tk, OpenGL and the Draw harness). A package from the development
build (MSYS2's OCCT package) would also contain FFmpeg built with
`--enable-gpl`, x264/x265/xvidcore and jbigkit (GPL) and must not be
distributed; `scripts/windows/license-gate.sh` refuses it (docs/LICENSING.md).

Transitive runtime dependencies of Qt and OCCT from MSYS2 (FreeType, HarfBuzz,
ICU, zlib, libpng, PCRE2, GLib, ...) are listed with their license texts and
the location of their exact source in `THIRD_PARTY_LICENSES.txt`, which
`scripts/package-windows.sh` generates for every package.

Vendored code: `third_party/planegcs` (unmodified upstream files plus
OpenShape shims; see its README). It is built as a separate shared library
so it stays replaceable, as the LGPL requires.
