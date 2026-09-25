# Third-party software

Versions are those used for the verified Windows build (MSYS2 UCRT64, 2026-09-25).

| Component | Version | License | Used for | Linked |
|---|---|---|---|---|
| Open CASCADE Technology | 7.9.3 | LGPL-2.1-or-later WITH OCCT-exception-1.0 | Exact B-rep kernel, STEP/STL | dynamic |
| Qt (Core, Gui, Qml, Quick, QuickControls2, ShaderTools) | 6.11.2 | LGPL-3.0-only (GPL exceptions for tools) | UI, QRhi rendering, shader compilation (build time) | dynamic |
| libzip | 1.11.4 | BSD-3-Clause | .openshape containers | dynamic |
| nlohmann/json | 3.12.0 | MIT | JSON | header-only |
| GoogleTest | 1.18.0 | BSD-3-Clause | Tests only (not shipped) | – |
| GCC / MinGW-w64 runtime | 16.2.0 | GPL-3.0 with GCC Runtime Library Exception | Compiler and runtime | dynamic |

Transitive runtime dependencies of Qt and OCCT from MSYS2 (FreeType, zlib,
libpng, HarfBuzz, ICU, TBB etc.) must be listed with their licenses in any
binary distribution. Producing that list is part of the packaging task in
docs/TECHNICAL_DEBT.md.

No third-party code is vendored into this repository.
