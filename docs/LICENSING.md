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
  files (section 3.2), e.g. a link to a public repository in the app's About
  box and store listing. The GitHub repository is private today.
- Contributions are accepted under MPL-2.0 (inbound = outbound).

## Dependencies

| Dependency | License | Effect |
|---|---|---|
| OpenCASCADE | LGPL-2.1-or-later + OCCT exception | Fine when dynamically linked (as now). |
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
- **Windows:** before any public download, fix TD-17 (GPL FFmpeg DLLs pulled
  in by MSYS2's OCCT), generate the transitive license list (TD-6) and add
  an About box with the license notices and the source link (TD-32).
