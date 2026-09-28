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
| OpenCASCADE | LGPL-2.1-only + OCCT exception | Windows: dynamically linked; releases use OpenShape's own build without FFmpeg/FreeImage (below). iOS: statically linked (below). |
| Qt 6 | LGPL-3.0 (or commercial) | Windows: dynamically linked, users can replace the Qt libraries. iOS: statically linked (below). |
| PlaneGCS (vendored, `third_party/planegcs`) | LGPL-2.1-or-later | Windows: its own shared library, so it stays replaceable. iOS: statically linked (below). |
| FreeType | FTL (or GPL-2.0; used under the FTL) | A credit in the documentation (the Licenses view). |
| Eigen | MPL-2.0 (a few files BSD / MINPACK / Apache-2.0) | Header-only; same license as OpenShape. |
| libzip, GoogleTest | BSD-3-Clause | Notice with binaries (GoogleTest is not distributed). |
| nlohmann/json | MIT | Notice with binaries. |
| Noto Sans | OFL-1.1 | The license goes along; the fonts are not sold on their own. |

OpenCASCADE's files say "GNU Lesser General Public License version 2.1"
without "or later" (e.g. `Standard_Transient.hxx`), so its SPDX id is
`LGPL-2.1-only WITH OCCT-exception-1.0` (THIRD_PARTY.md said "or-later"
until 2026-09-27). Versions and the full list: `THIRD_PARTY.md`; every
license text is also in the app (About → Licenses). SolveSpace's libslvs
(GPL-3.0) is not an option: it would make distributed binaries GPL-3.0,
which conflicts with the App Store goal below.

## The owner's distribution goals (2026-09-25)

- Platforms: Windows desktop, and an iPad app (built on the owner's Mac).
  The main goal is to use OpenShape on their own iPad.
- Selling is not a priority; at most small fees (e.g. a Microsoft Store
  build, a paid iPad app) to cover costs such as Apple's developer fee.
  Avoid anything that needs costly commercial licenses (e.g. a commercial
  Qt license).
- **Update 2026-09-27:** the universal iPhone/iPad app will be sold on the
  App Store, after a public TestFlight beta, around or before 2026-10-23
  (the foldable iPhone Duo's launch). Still no commercial Qt license.

What that needs:

- **App Store:** MPL-2.0 allows it. The executable may be distributed under
  other terms as long as they don't limit recipients' rights to the source
  of OpenShape's files (section 3.2(b)).
- **iOS linking:** iOS apps link statically, and the LGPL libraries (Qt,
  OCCT, PlaneGCS) then require giving recipients the means to relink them
  with modified versions. Checked on 2026-09-27 and implemented: see "The
  iOS app and the App Store" below.
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
  and installer scripts and in `release.yml`): every bundled file
  (executables, DLLs, QML modules, data) is traced by content to
  OpenShape's build, OpenShape's own texts (LICENSE, README, THIRD_PARTY,
  PlaneGCS's COPYING.LIB, compared with the repository; the generated
  THIRD_PARTY_LICENSES.txt and qt.conf), the own OCCT build or an MSYS2
  package, whose license field must not be GPL-only; packages that
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

Each release's notes say whether it is code-signed; for an unsigned one
Windows SmartScreen shows "More info → Run anyway". Signing through
SignPath Foundation (free for open-source projects) is prepared in
`release.yml` and switches on once the project is accepted and set up: see
[CODE_SIGNING.md](CODE_SIGNING.md). SignPath Foundation's
rules let us sign only OpenShape's own files (`OpenShape.exe`, the
installer); the bundled libraries of other projects stay unsigned.

## The iOS app and the App Store

Checked and implemented on 2026-09-27 for the paid App Store release of the
universal iPhone/iPad app (and the public TestFlight beta before it).
**This is an engineering analysis of the license texts and public
statements, not legal advice.** Where a lawyer's review would add
certainty is said at the end.

### In short

- **Selling the app is allowed.** Neither the MPL-2.0 nor the LGPL
  forbids charging for the app; they forbid charging for (or withholding)
  the source code and restricting what recipients may do with the LGPL
  libraries.
- **Static linking of Qt (LGPL-3.0), Open CASCADE (LGPL-2.1 with the OCCT
  exception) and PlaneGCS (LGPL-2.1-or-later) is allowed** when recipients
  get what they need to rebuild the app with modified versions of those
  libraries. OpenShape's complete source (MPL-2.0), its build scripts and
  the exact source of every library are public and free of charge at each
  release tag, so rebuilding from source is the relinking route; nothing
  proprietary is linked in.
- **The app shows every license text and notice** (About → Licenses),
  including the GPL-3.0 text the LGPL-3.0 asks for, the Open CASCADE
  notice, the FreeType credit and the third-party code inside Qt.
- **Every App Store version is a git tag** whose GitHub release carries
  the iOS libraries' source (`OpenShape-<version>-ios-sources.tar`): only
  builds of such tags are submitted for App Store review. Public beta
  builds of `main` name their commit, whose source and library pins are
  public; the written offer covers their library sources, and CI warns
  until a release keeps copies of new pins.
- **Apple's standard EULA** is very likely compatible (it excepts what the
  open-source licenses permit), but a **custom EULA** that says so
  explicitly costs nothing and removes the doubt: proposed in
  [EULA.md](EULA.md), to be set in App Store Connect before the first
  public release (the owner's decision; a lawyer's review recommended).
- **Residual risk:** the FSF's view that App Store terms conflict with the
  GPL, if someone applied it to the LGPL, and Apple's DRM on the delivered
  copy. The realistic consequence would be a takedown request to Apple,
  not damages; the VLC precedent (below) points the other way.

### What the iOS app contains

Built by `scripts/ios/build-deps.sh` (libraries), `scripts/ios/install-qt.sh`
(Qt) and `scripts/ios/build-app.sh` (the app) in `.github/workflows/ipad.yml`.
Every source archive is pinned by URL and SHA-256 in
`scripts/ios/sources.txt` (build-deps.sh refuses any other).

| Component | Version | License | How it is in the app | Exact source |
|---|---|---|---|---|
| OpenShape | the app's | MPL-2.0 | the app | this repository at the release tag |
| Qt | 6.11.2 | LGPL-3.0-only (Qt's own code) | static libraries from The Qt Company's Qt for iOS (installed by aqtinstall from download.qt.io) | `qtbase`, `qtdeclarative`, `qtsvg` source archives (download.qt.io/archive, SHA-256 in sources.txt) |
| Third-party code inside Qt | various | MIT, BSD-2/3, Zlib, FTL, IJG, libpng, Unicode-3.0, CC0, Apache-2.0, MPL-2.0 (PSL data), HPND, ... | compiled into Qt's libraries | the same Qt archives (their `qt_attribution.json` files); 41 parts listed in the app |
| Qt Shader Tools | 6.11.2 | tool: GPL-3.0-only WITH Qt-GPL-exception-1.0 (library: LGPL-3.0) | **not linked**: the `qsb` tool compiles OpenShape's own shaders at build time; the exception's "Exception 1" lets a larger work contain the tool's output under any terms | `qtshadertools` archive (mirrored anyway) |
| Open CASCADE Technology | 7.9.3 | LGPL-2.1-only WITH OCCT-exception-1.0 | static libraries (the toolkits `src/geometry` links) | GitHub tag `V7_9_3` archive |
| PlaneGCS (FreeCAD) | commit 8a1bdba1 | LGPL-2.1-or-later | static library `libplanegcs.a` | `third_party/planegcs` in this repository |
| FreeType | 2.14.3 | FTL (with MIT for BDF/PCF, Zlib for its gzip module) | static library (OCCT's font reader for the Text tool) | GitHub tag `VER-2-14-3` archive |
| libzip | 1.11.4 | BSD-3-Clause | static library | the 1.11.4 release archive |
| nlohmann/json | 3.12.0 | MIT | header-only | GitHub tag `v3.12.0` archive |
| Eigen | 5.0.1 | MPL-2.0 (some files BSD, MINPACK, Apache-2.0) | header-only (PlaneGCS) | GitLab tag `5.0.1` archive |
| Noto Sans Regular/Bold | 2.013 | OFL-1.1 | built into the executable (Qt resource) | `resources/fonts/` (hashes in THIRD_PARTY.md) |
| App icon, launch color, privacy manifest | - | OpenShape's own (MPL-2.0) | asset catalog | `resources/icons/openshape.svg`, `src/app/ios/` |
| zlib, libc++, UIKit, Metal, ... | iOS SDK | Apple's | system libraries (not in the app) | - |

The Qt modules linked include, from the QML import scan of CI run
36314342613 (build 36, 2026-09-27): Qt Core, Gui, QML (with Models and
WorkerScript), Quick, Quick Controls (Basic style only; the other styles
are unlinked, TD-36), Quick Templates, Quick Dialogs, Quick Effects, Quick
Layouts, Quick Shapes and Qt.labs.folderlistmodel, with the iOS platform
plugin and image format plugins (and whatever C++ modules these need, such
as Qt Network). iOS has no system HarfBuzz, PCRE2, libpng or libjpeg, so
Qt's own copies (listed among its third-party parts) are in the app. From
the next build on, every build's license gate lists exactly what the
linker used (below).

### What the licenses require

**MPL-2.0 (OpenShape).** Distributing the executable is allowed under
other terms (Apple's EULA) as long as they don't limit recipients' rights
in the source code form (section 3.2(b)), and recipients must be told how
to get the source (3.2(a)): the About card and the Licenses view link it.
MPL-2.0 files without Exhibit B may be combined with (L)GPL code in a
larger work (3.3), so even the reading that the whole statically linked
app must be available under the LGPL is satisfiable.

**LGPL-3.0 (Qt), section 4 "Combined Works".** A Combined Work may be
conveyed "under terms of your choice" that do not restrict modification of
the library portions and reverse engineering for debugging such
modifications, if we also:
- (a) give prominent notice that the Library is used and is covered by the
  LGPL, (b) accompany the app with the GPL-3.0 and LGPL-3.0 texts, (c) show
  the Library's copyright notice among the app's notices with a reference
  to those texts: the Licenses view (Qt's page: copyright line, notice,
  both texts);
- (d)(0) convey the Minimal Corresponding Source (Qt's source) and the
  Corresponding Application Code ("the object code and/or source code for
  the Application ... needed for reproducing the Combined Work") in a form
  that lets the user relink, "in the manner specified by section 6 of the
  GNU GPL". OpenShape's source and build scripts are the Corresponding
  Application Code; GPL-3.0 section 6(d) lets the object code be offered
  from one place (the App Store) and the source from another server "with
  clear directions next to the object code saying where to find" it, at no
  further charge and for as long as needed: the Licenses view, the About
  card and the App Store description point to the release page;
- (e) provide Installation Information **only** if GPL-3.0 section 6
  requires it. It does only when the object code is conveyed "in, or with,
  or specifically for use in, a User Product" **and** "as part of a
  transaction in which the right of possession and use of the User Product
  is transferred to the recipient". An iPhone is a User Product, but we do
  not sell iPhones: the app is conveyed on its own. The FSF's GPL FAQ says
  the same about signing keys: they would be due only for software conveyed
  "inside a User Product" whose hardware checks signatures (#GiveUpKeys).
  So this is not required. It is also not a practical obstacle: anyone can
  sign and install their own build on their own iPhone or iPad with Xcode
  and an Apple Account (a free account installs for 7 days at a time), and
  BUILDING.md documents it; we hold no key they need.

**LGPL-2.1 (Open CASCADE; PlaneGCS), section 6.** A work containing
portions of the Library may be distributed "under terms of your choice,
provided that the terms permit modification of the work for the
customer's own use and reverse engineering for debugging such
modifications", with prominent notice, a copy of the license, the
Library's copyright notice among the app's notices, and one of (a)-(e):
(a) accompany the work with the Library's source and the "work that uses
the Library" as object and/or source code (an app cannot carry 150 MB of
source), (c) a written offer valid for at least three years, or (d) if the
work is offered from a designated place, equivalent access to the
materials "from the same place" (the App Store cannot host them). So the
app carries a **written offer** (Licenses → "Your rights to the LGPL
libraries": for at least three years and for as long as we distribute that
version, on request, for no more than the cost of providing it) and the
materials are also downloadable free of charge from the release page.
PlaneGCS is "or later", so the LGPL-3.0 route above also applies to it.

**The OCCT exception** (OCCT_LGPL_EXCEPTION.txt) lets object code that
incorporates material from OCCT's headers (larger inline functions and
templates) be distributed under our terms "provided that you give
prominent notice in supporting documentation to this code that it makes
use of or is based on facilities provided by the Open CASCADE Technology
software": the Licenses view says exactly that (and the App Store
description may say it too). It does not change the LGPL-2.1 section 6
duties for OCCT's own object code, which is linked statically.

**No further restrictions vs. Apple's terms.** LGPL-2.1 section 10 and
GPL-3.0 section 10 (which the LGPL-3.0 builds on) forbid imposing further
restrictions on the rights the licenses grant. Two sets of Apple terms
reach the user:
- *Apple's standard EULA* (Licensed Application End User License
  Agreement) forbids copying, reverse engineering, modifying and
  redistributing the app, but excepts what "may be permitted by the
  licensing terms governing use of any open-sourced components" in the
  app. Restricting the transfer and redistribution of the combined app is
  allowed by the LGPL (terms of our choice); restricting modification and reverse engineering for debugging
  is not, and the exception arguably covers it. "Arguably" is the gap: the
  LGPL-2.1 asks the terms to permit modification of *the work*, and the
  exception speaks of the open-source *components*. A custom EULA closes
  it (EULA.md: it grants those permissions expressly and says the
  open-source licenses prevail for their components). App Store Connect
  accepts a plain-text custom EULA (App Information → License Agreement)
  that must contain Apple's ten minimum terms, which EULA.md does.
- *Apple Media Services Terms* ("Usage Rules", last updated 2026-09-14):
  device limits, no redistribution, no tampering with Apple's security
  technology, no scraping or analysis of Content. They apply
  whatever the app's EULA says, which is why the FSF considers the App
  Store incompatible with the GPL (GNU Go, 2010: GPLv2 section 6). For the
  LGPL the question is narrower: the combined app may be distributed under
  restrictive terms, as long as the user can modify the libraries and
  relink. Here the user never needs the App Store copy for that: the
  complete source of exactly that version is available under the MPL/LGPL,
  outside Apple's terms, and builds the same combined work. Apple's
  FairPlay encryption of the delivered copy is Apple's measure, not a term
  of ours; the LGPL-3.0 (through GPL-3.0 section 3) makes us waive any
  power to forbid circumvention, which the custom EULA repeats.
- *Precedent:* VLC for iOS (an MPL-2.0 app on the LGPL-2.1 libVLC; in
  2013 iOS apps could only link libraries statically) returned to the App
  Store in July 2013 and is still there, after VLC was withdrawn in 2011
  over the GPL and its engine was relicensed to the LGPL
  "to achieve better license compatibility, for instance with the Apple
  App Store" (Wikipedia, VLC media player). The Qt Company warns that app
  stores "may have rules that are in conflict with LGPL" and recommends,
  for static linking, "either link dynamically, or provide the application
  source code to the user under LGPL"; OpenShape provides the complete
  source (MPL-2.0, LGPL-compatible).

**Permissive licenses** (FreeType's FTL, BSD, MIT, zlib, libpng, IJG, OFL,
Unicode, ...) ask for their notices and license texts in the
documentation of binary distributions: the Licenses view has each one's
text and copyright (FreeType's credit line: "Portions of this software are
copyright © 2026 The FreeType Project").

### What is implemented

- **Licenses view** (every platform; About → Licenses,
  `src/ui/qml/LicensesOverlay.qml`, `src/ui/Licenses.*`): "Your rights to
  the LGPL libraries" (the relinking notice and written offer, naming this
  build's version, build number, commit and release tag with links to its
  source and its iOS source archive; `resources/licenses/source-offer.txt`),
  OpenShape (MPL-2.0), the 8 libraries and the 41 third-party parts inside
  Qt, each with copyright, notice, where its exact source is and the full
  license texts (51 files, 277 KiB, built into the app). Acceptance
  scenario `licenses`; GTests `Licenses.*`.
- **Generated from the pinned sources:** `scripts/licenses/licenses.py
  generate` reads the archives of `scripts/ios/sources.txt` (SHA-256
  checked), `scripts/licenses/components.json` (the libraries; which Qt
  third-party parts apply to iOS, each decision with a reason) and Qt's
  `qt_attribution.json` files, and writes `resources/licenses/`. A Qt
  update with a new third-party part fails until it is decided. `check`
  (ctest `licenses_check`, and ipad.yml on every run) fails when the
  committed texts do not match the pins, `components.json`, the
  repository's own license files or ipad.yml's Qt version.
- **License gate** (`scripts/ios/build-app.sh`, `licenses.py gate`): the
  linker's map of the app is checked file by file: everything must come
  from Qt (with its source module pinned), the iOS libraries, this build or
  Apple's SDK, and be in the Licenses view. It warns on ordinary builds and
  fails release-tag builds; each build's notice lists what was linked.
- **Relinking route** (BUILDING.md, "Rebuilding the iOS app with modified
  libraries"): the same scripts CI uses, with the unpacked library sources
  kept in a work folder so a modified library is rebuilt from there, a
  self-built Qt accepted in place of The Qt Company's, and
  `OPENSHAPE_BUNDLE_ID` for the rebuilder's own bundle identifier.
- **Source mirror** (`scripts/ios/mirror-sources.sh`): downloads every
  pinned archive, checks its SHA-256 and writes `SOURCES-SHA256SUMS.txt`
  and a README; `release.yml` runs it on every run and attaches
  `OpenShape-<version>-ios-sources.tar` to each release (OpenShape's own
  source is GitHub's "Source code" archive of the tag).
- **App Store versions are tags** (`ipad.yml`): a tag `v<version>[-suffix]`
  must match CMakeLists.txt, passes the tag into the app and requires the
  license gate; its run's notice names the build to submit for review.
  Builds of `main` also upload with `testFlightInternalTestingOnly` off
  (the owner's public TestFlight beta, docs/IPAD.md 1b), so "only tag
  builds go to review" is a rule of the release steps below, not enforced
  by Apple; each `main` run warns when no release tag has its library pins
  yet (`scripts/ios/released-pins.sh`).
- **Custom EULA** proposal: [EULA.md](EULA.md).

### Releasing an App Store (or public beta) version

1. Before the first public release, once: set the custom EULA in App Store
   Connect (EULA.md; the owner fills in name, address, contact), and add to
   the App Store description: "OpenShape is free software (Mozilla Public
   License 2.0). It uses Qt, Open CASCADE Technology and PlaneGCS under the
   GNU LGPL; source code and license texts: https://github.com/SamuelAirs/openshape
   (see Licenses in the app)."
2. Tag the version as for Windows (BUILDING.md, "Before tagging a release";
   a beta can be `v<version>-beta1`). The tag's `release.yml` run
   publishes the GitHub release with the iOS sources; its `ipad.yml` run
   uploads the build that may be submitted (the notice names its build
   number).
3. In App Store Connect submit **only** that build for App Store review.
   Keep the release and its files published for at least three years after
   that version was last offered.

**Public beta builds of `main`** (TestFlight, internal and external
testers; docs/IPAD.md 1b) are distributed too. Their Licenses notice
names their commit, whose source is public and whose
`scripts/ios/sources.txt` pins the exact library archives; the written
offer covers those for three years. So that the offer never depends on
upstream keeping an archive (TD-86), a release (a `-beta` tag is enough)
must carry every pinned set that went to testers: `ipad.yml` warns
("iOS sources: no release tag has these library pins") until one does.
A build that changes the pins should get a beta tag within days.

### Settled, judgement calls, residual risk

- **Settled by the license texts:** selling is allowed; the source must be
  free of charge; the notices and texts above must be shown (done); static
  linking needs the relinking materials, and source code of the whole app
  satisfies both LGPL versions ("object code and/or source code"); GPL-3.0
  Installation Information is not triggered by selling an app.
- **Judgement calls:** that Apple's standard EULA exception is enough (we
  recommend the custom EULA anyway); that Apple's Usage Rules and DRM,
  which we cannot change, are not "further restrictions" we impose on the
  LGPL libraries, given that the full source is available outside the App
  Store (the VLC precedent supports it; the FSF's GPL position is the
  counter-argument); that the written offer plus online source satisfies
  LGPL-2.1 section 6 for an App Store app; that listing Qt third-party
  parts which may not all be compiled into Qt's iOS libraries (over-
  inclusion) is harmless.
- **Residual risk:** a copyright holder of Qt, OCCT or FreeCAD could ask
  Apple to remove the app (as a VLC developer did in 2011 over the GPL).
  The likely remedy would be a discussion and an update, not damages. The
  risk that Apple's own terms change is outside our control; the gate,
  pins and tag process keep our side verifiable.
- **Where a lawyer adds certainty:** the custom EULA (consumer law in the
  owner's country, liability limits, Apple's minimum terms), whether the
  written offer's contact (the GitHub issue tracker) should be an e-mail
  or postal address, and the "OpenShape" name (the MPL grants no trademark
  rights, section 2.3; others may legally rebuild and distribute the app).

### Sources

- LGPL-3.0: <https://www.gnu.org/licenses/lgpl-3.0.txt> (sections 0 and 4);
  GPL-3.0: <https://www.gnu.org/licenses/gpl-3.0.txt> (sections 1, 3, 6, 10);
  LGPL-2.1: `third_party/planegcs/COPYING.LIB` (sections 5, 6, 10);
  OCCT exception: `OCCT_LGPL_EXCEPTION.txt` in the OCCT 7.9.3 archive.
- FSF, GPL FAQ: <https://www.gnu.org/licenses/gpl-faq.html>
  (#LGPLStaticVsDynamic, #GiveUpKeys, #v3VotingMachine); FSF on the App
  Store (2010): <https://www.fsf.org/blogs/licensing/more-about-the-app-store-gpl-enforcement>.
- The Qt Company, "Obligations of the GPL and LGPL":
  <https://www.qt.io/development/open-source-lgpl-obligations>; Qt FAQ:
  <https://www.qt.io/faq/qt-open-source-licensing>.
- Apple: Licensed Application EULA
  <https://www.apple.com/legal/internet-services/itunes/dev/stdeula/>;
  Minimum Terms of Developer's EULA
  <https://www.apple.com/legal/internet-services/itunes/dev/minterms/>;
  custom license agreement in App Store Connect
  <https://developer.apple.com/help/app-store-connect/manage-app-information/provide-a-custom-license-agreement>;
  Apple Media Services Terms (Usage Rules)
  <https://www.apple.com/legal/internet-services/itunes/us/terms.html>.
- VLC: <https://en.wikipedia.org/wiki/VLC_media_player> (App Store history);
  <https://www.videolan.org/press/lgpl-libvlc.html> (relicensing, 2011).
