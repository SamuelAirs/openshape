# Licensing

OpenShape is licensed under the **Mozilla Public License 2.0** (`LICENSE`),
chosen by the owner on 2026-09-25.

## What that means in practice

- Everything outside `third_party/` is OpenShape's own code under MPL-2.0.
  Every source file starts with the MPL notice (Exhibit A of the license);
  new files get it too — copy it from any existing file.
- MPL-2.0 is a file-level copyleft: whoever distributes OpenShape, changed or
  not, must keep OpenShape's files available under the MPL. OpenShape may be
  combined with code under other licenses (including closed code) in a
  larger work.
- Distributing binaries (the Windows package, store builds, an iPad app)
  requires telling recipients where they can get the source of OpenShape's
  files (section 3.2): the About box and the release notes link the public
  repository (public since 2026-09-25); a release names its tag.
- Contributions are accepted under MPL-2.0 (inbound = outbound).

## Dependencies

| Dependency | License | Effect |
|---|---|---|
| OpenCASCADE | LGPL-2.1-or-later + OCCT exception | Fine when dynamically linked (as now). Windows releases use OpenShape's own build without FFmpeg/FreeImage (below). |
| Qt 6 | LGPL-3.0 (or commercial) | Fine when dynamically linked; users must be able to replace the Qt libraries. |
| PlaneGCS (vendored, `third_party/planegcs`) | LGPL-2.1-or-later | Built as its own shared library so it stays replaceable. |
| Eigen | MPL-2.0 | Header-only; same license as OpenShape. |
| libzip, GoogleTest | BSD-3-Clause | No constraint. |
| nlohmann/json | MIT | No constraint. |

Versions and the full list: `THIRD_PARTY.md`. SolveSpace's libslvs
(GPL-3.0) is not an option: it would make distributed binaries GPL-3.0,
which conflicts with the App Store goal below.

## The owner's distribution goals (2026-09-25)

- Platforms: Windows desktop, and an iPad app (built on the owner's Mac).
  The main goal is to use OpenShape on their own iPad.
- Selling is not a priority; at most small fees (e.g. a Microsoft Store
  build, a paid iPad app) to cover costs such as Apple's developer fee.
  Avoid anything that needs costly commercial licenses (e.g. a commercial
  Qt license).

What that needs:

- **App Store:** MPL-2.0 allows it. The executable may be distributed under
  other terms as long as they don't limit recipients' rights to the source
  of OpenShape's files (section 3.2(b)).
- **iOS linking:** iOS apps link statically, and the LGPL dependencies (Qt,
  OCCT, PlaneGCS) then require giving recipients a way to relink them with
  modified versions (e.g. shipping the object files). Check this before the
  first iPad release.
- **Windows:** distribution is possible since 2026-09-25 (TD-17 resolved):
  see "The Windows release" below.

## The Windows release

MSYS2's OpenCASCADE package links its visualization toolkit (needed by the
STEP translator) against FFmpeg built with `--enable-gpl` (plus x264, x265,
xvidcore) and FreeImage, whose libtiff pulls in jbigkit (GPL-2.0). A package
built from it contains GPL libraries OpenShape never uses. Therefore:

- **Release builds use OpenShape's own OCCT build**
  (`scripts/windows/build-occt.sh`): the same version and MSYS2 patches,
  only the toolkits OpenShape links, without FFmpeg, FreeImage, TBB, VTK,
  Tcl/Tk, OpenGL and the Draw harness (FreeType stays: FTL). The dev build
  and CI keep MSYS2's package; only the release preset
  (`msys2-ucrt64-release`) is distributable.
- **License gate** (`scripts/windows/license-gate.sh`, run by the package
  and installer scripts and in `release.yml`): every bundled executable and
  DLL is traced by content to OpenShape's build, the own OCCT build or an
  MSYS2 package, whose license field must not be GPL-only; packages that
  mix GPL with other terms for their tools (Qt, xz, gettext-runtime, GMP,
  libiconv) are reviewed and listed in the script; the GCC runtime
  (GPL-3.0 with the GCC Runtime Library Exception: `libgcc_s_seh-1`,
  `libstdc++-6`, `libwinpthread-1`) is allowed by name; known GPL library
  names fail anywhere. A release build that fails the gate is not packaged.

What the release contains (2026-09-25): OpenShape (MPL-2.0), OCCT (own
build), PlaneGCS, and 24 MSYS2 packages — Qt (LGPL-3.0), ICU, FreeType (FTL),
HarfBuzz, GLib, Graphite2, libintl and libiconv (LGPL-2.1), libzip, zlib,
zstd, xz, bzip2, brotli, libpng, PCRE2, double-conversion, md4c, libb2 (all
permissive) and the GCC runtime; the full list with licenses is the gate's
report and THIRD_PARTY_LICENSES.txt.

Obligations for the LGPL parts (OCCT, Qt, PlaneGCS, GLib, Graphite2,
libintl, libiconv) and how the release meets them:

- **Replaceable libraries:** all are separate DLLs next to `OpenShape.exe`
  (no static linking), so users can swap in modified versions.
- **License texts:** `LICENSE.txt` (MPL-2.0), `THIRD_PARTY_LICENSES.txt`
  (every bundled library's license text, generated from the MSYS2 packages
  and OCCT's source tree) and `PlaneGCS-COPYING.LIB.txt` ship in the
  package; the About box points to them.
- **Corresponding source:** `THIRD_PARTY_LICENSES.txt` names, for each
  library, where its exact source is: OCCT's upstream tag
  (github.com/Open-Cascade-SAS/OCCT, tag `V7_9_3`) plus the MSYS2 patches
  and build script that were applied; each MSYS2 package's source archive
  (`https://repo.msys2.org/mingw/sources/<base>-<version>.src.tar.zst`,
  which includes MSYS2's recipe and patches); PlaneGCS is in this
  repository (`third_party/planegcs`). The release notes say the same. If
  MSYS2 ever removes an old source archive, we must keep a copy for the
  versions we shipped (the LGPL asks for the source to stay available for
  as long as we distribute the binaries): download the listed archives
  when publishing a release.
- **OpenShape itself (MPL-2.0):** the About box links the repository; the
  release notes also name the release's tag. The installer shows the MPL.

The installer is not code-signed yet (Windows SmartScreen shows "More info
→ Run anyway"); signing needs a certificate (a product decision: cost).
