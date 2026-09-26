# Code signing policy

This page describes how OpenShape's Windows releases are code-signed
through [SignPath Foundation](https://signpath.org/), which gives
open-source projects free code signing. Signed releases carry this
attribution:

**Free code signing provided by [SignPath.io](https://about.signpath.io/),
certificate by [SignPath Foundation](https://signpath.org/).**

Status: the signing pipeline is ready in `.github/workflows/release.yml`
and switches on once the owner's application to SignPath Foundation is
accepted and set up (see [For the maintainer](#for-the-maintainer)). Until
then releases are unsigned, and Windows SmartScreen may warn ("More info →
Run anyway"). Each release's notes say whether it is signed.

## What is signed

| File | Signed | Why |
|---|---|---|
| `OpenShape.exe` (in the installer and in the portable zip) | yes | OpenShape's own program, built from this repository |
| `OpenShape-<version>-windows-x64-setup.exe` (the installer) | yes | built from this repository (`packaging/windows/openshape.nsi`) |
| The bundled DLLs: Qt, Open CASCADE Technology, PlaneGCS, the MSYS2 libraries and the GCC runtime | no | other projects' code. SignPath Foundation lets a project sign only binaries built from its own source; libraries of other open-source projects may be bundled unsigned. This includes `libplanegcs.dll`: OpenShape builds it, but from FreeCAD's unmodified PlaneGCS source (`third_party/planegcs`). Each bundled file is listed with its license in `THIRD_PARTY_LICENSES.txt`, and the license gate (`scripts/windows/license-gate.sh`) traces every file of a release to its origin. |
| `Uninstall.exe` | no, for now | NSIS writes it on the user's computer while installing, so no file of the release contains it to sign ([TD-44](TECHNICAL_DEBT.md)). |
| The portable `.zip` | no | a zip file cannot carry an Authenticode signature; `OpenShape.exe` inside it is signed, and `SHA256SUMS.txt` lists the zip's checksum |

Signatures are Authenticode (SHA-256). SignPath checks that each signed
file's version information names the product **OpenShape** and the
release's version (`.signpath/artifact-configurations/`).

The iPhone and iPad app is signed by Apple's App Store / TestFlight process
(docs/IPAD.md); there is no macOS or Linux download yet.

## How signed releases are built

Only the GitHub Actions workflow `.github/workflows/release.yml`, running on
GitHub-hosted runners, produces signed files, always from the public
repository at https://github.com/SamuelAirs/openshape. Nobody signs on a
personal computer, and nobody holds the signing key: it is kept in
SignPath's hardware security module.

For a release tag `v<version>` the workflow:

1. checks out the tag and builds Open CASCADE Technology from its upstream
   tag with `scripts/windows/build-occt.sh`. A release that is signed builds
   it in the same run, not from an earlier run's cache, so everything the
   signed program is built from comes from this run;
2. builds OpenShape (release preset, warnings as errors) and runs the
   headless tests;
3. packages it and runs the license gate (no GPL code may ship), and tries
   the check of step 5 on made-up signed copies of the packaged program
   (it must accept only the copy that differs by a signature alone);
4. uploads `OpenShape.exe` as a workflow artifact and submits it to
   SignPath. SignPath verifies with GitHub that the artifact was built by
   this workflow run from this repository and tag on GitHub-hosted runners,
   checks the file's product name and version, and waits for an approver;
5. checks the signed `OpenShape.exe` it gets back
   (`scripts/windows/use-signed.sh`): byte for byte the file it sent plus a
   signature and nothing else (`scripts/windows/pe-signature.py`), and a
   signature Windows reports as valid;
6. makes the installer and the zip with the signed program, then has the
   installer signed and checked the same way, and writes `SHA256SUMS.txt`
   for the signed files;
7. installs, upgrades and uninstalls the signed installer silently as a
   test, and publishes the GitHub Release.

Once signing is on, manual runs and pushes to `main` that change the
packaging go through the same steps with SignPath's test certificate, which
Windows does not trust (and reuse the cached OpenCASCADE build); those
files only become a workflow artifact and are never published.

## Team roles

OpenShape currently has one maintainer.

- **Committers and reviewers:** [@SamuelAirs](https://github.com/SamuelAirs)
  (the owner). Changes from anyone else (pull requests) are reviewed by the
  owner before they are merged. Changes prepared with AI coding assistants
  on the owner's computer reach the repository only when the owner pushes
  them or approves the push; the owner answers for them like for any other
  change.
- **Approvers:** [@SamuelAirs](https://github.com/SamuelAirs). Every
  release-signing request must be approved by hand in SignPath.

Everyone in these roles uses multi-factor authentication for GitHub and for
SignPath. Build scripts and the CI configuration (`.github/`, `scripts/`,
`packaging/`, `CMakeLists.txt`, `cmake/`) are reviewed like the code, since
they decide what gets built and signed.

## Privacy policy

This program will not transfer any information to other networked systems
unless specifically requested by the user or the person installing or
operating it.

In practice OpenShape has no telemetry, no usage statistics, no automatic
crash reports, no update check and no account. Projects, settings, recovery
copies and the log stay on your computer. The only network access is a web
link you click (the user guide and the license and source links in Help and
About), which opens in your browser. The bundled libraries do not contact
the network in OpenShape. (Windows' own error reporting, if a program
crashes, follows your Windows settings.)

## Checking a signed release

- **Explorer:** right-click the installer or `OpenShape.exe` →
  *Properties* → *Digital Signatures*. The signer is **SignPath
  Foundation** (the certificate is issued to SignPath Foundation, which
  vouches that the file was built from this repository); *Details* says
  "This digital signature is OK".
- **PowerShell:** `Get-AuthenticodeSignature .\OpenShape-<version>-windows-x64-setup.exe | Format-List`
  shows `Status : Valid` and the signer certificate.
- **Checksums:** `Get-FileHash <file>` must match the line in the release's
  `SHA256SUMS.txt`.

Anything else (no signature, another signer, an invalid signature) means
the file did not come from an OpenShape release: please report it at
https://github.com/SamuelAirs/openshape/issues. A file signed with a
SignPath Foundation certificate that violates SignPath Foundation's rules
can be reported to support@signpath.io.

## For the maintainer

What to do once, to switch signing on. Until step 4 the release workflow
keeps producing unsigned releases exactly as before; step 7 brings the
documentation up to date after the first signed release.

### 1. Before applying

- Turn on two-factor authentication for your GitHub account (*Settings →
  Password and authentication*). SignPath Foundation requires it, and later
  for your SignPath login too.
- Make sure a release is published: SignPath Foundation only signs a
  project that is already released in the form to be signed (the Windows
  installer and zip, e.g. 0.1.0 on the Releases page).
- This page must be reachable from the home page and the release pages
  under the words "Code signing policy": README.md and the release notes
  (`packaging/windows/release-notes.md`) link it.

### 2. Apply

Fill in the form at https://signpath.org/apply. Answers that fit:

| Field | Answer |
|---|---|
| Project / repository | OpenShape, https://github.com/SamuelAirs/openshape |
| Homepage | https://github.com/SamuelAirs/openshape |
| Download page | https://github.com/SamuelAirs/openshape/releases |
| Code signing policy, privacy policy | https://github.com/SamuelAirs/openshape/blob/main/docs/CODE_SIGNING.md |
| License | MPL-2.0 (OSI-approved; bundled libraries LGPL, see THIRD_PARTY.md) |
| Description | Free, open-source direct-modeling CAD for makers and 3D printing (Windows desktop app: C++, Qt 6, OpenCASCADE) |
| Build system | GitHub Actions on GitHub-hosted runners, `.github/workflows/release.yml` |
| What to sign | `OpenShape.exe` and the NSIS installer (Authenticode), two signing requests per release |
| Reputation | what can be checked: stars, downloads, links or videos by others |

SignPath Foundation reviews applications by hand; applicants report one to
two weeks. Be prepared for a "not yet": for downloadable programs SignPath
Foundation asks for a reputation it can verify, and OpenShape has only been
public since 2026-09-25. Their terms: https://signpath.org/terms.

### 3. Set up SignPath (after acceptance)

In SignPath (https://app.signpath.io), with multi-factor authentication on
for your login:

1. **Trusted build system:** add the predefined *GitHub.com* trusted build
   system to the organization and link it to the project.
2. **Project** (if SignPath has not created it): slug `openshape`,
   repository URL `https://github.com/SamuelAirs/openshape`. Another slug
   works too: then set the variable `SIGNPATH_PROJECT_SLUG` (section 4).
3. **Artifact configurations:** two, with exactly these slugs, their XML
   copied from this repository:
   - `program` ← `.signpath/artifact-configurations/program.xml`
   - `installer` ← `.signpath/artifact-configurations/installer.xml`
4. **CI user and API token:** create a CI user (*Users → Add CI user*)
   and its API token. Copy the token; it is shown once.
5. **Signing policies** (exactly these slugs), both with trusted build
   system verification on (SignPath requires it for open-source projects)
   and the CI user as *Submitter*:
   - `test-signing`: the test certificate, no approval. Used by manual
     runs and packaging changes on `main`.
   - `release-signing`: the SignPath Foundation certificate; origin
     verification on; approval process on, 1 approval, approver: you.
     *Allowed branch names:* the release tags, `refs/tags/v*` (a signing
     request's page shows the ref SignPath saw; adjust the pattern if it
     names tags differently).
6. Note the **organization ID** (click your organization's name at the
   upper right).

On GitHub, install the **SignPath GitHub App** on the repository:
https://github.com/apps/signpath (*Install → Only select repositories →
openshape*). Version 3 of SignPath's GitHub action requires it.

### 4. GitHub secret and variables

Repository → *Settings → Secrets and variables → Actions*:

| Kind | Name | Value |
|---|---|---|
| Secret | `SIGNPATH_API_TOKEN` | the CI user's API token |
| Variable | `SIGNPATH_ORGANIZATION_ID` | the organization ID |
| Variable (optional) | `SIGNPATH_PROJECT_SLUG` | only if the project's slug is not `openshape` |

Signing is on as soon as the secret and `SIGNPATH_ORGANIZATION_ID` are set;
deleting the variable switches it off again (releases are then unsigned,
with the SmartScreen note).

### 5. First test (test certificate, no approval)

GitHub → *Actions → Release → Run workflow* (branch `main`). The run's
notices show `Code signing: Signed: OpenShape.exe (test-signing): ...` and
the same for the installer; the files are in the run's artifact. They show
an untrusted signature on your PC: that is the test certificate.

### 6. A signed release

Push the tag as usual (`git tag v<version> && git push origin v<version>`).
After the builds and tests (about an hour: a signed release builds
OpenCASCADE itself instead of using the cache; the first, uncached release
run took 54 minutes in all) SignPath e-mails you twice: first for
`OpenShape.exe`, a few minutes after your approval for the installer.
Approve each in SignPath within 60 minutes (the workflow waits that long,
then fails without publishing anything). The release notes then say the
files are signed, and the notices show
`Signed: ... (release-signing): SignPath Foundation; Valid`. Check the
downloaded installer as in [Checking a signed release](#checking-a-signed-release).

### 7. After the first signed release

The install instructions already cover both cases (README.md, "Download and
install"; docs/USER_GUIDE.md, "Install" and "Troubleshooting": the
SmartScreen advice is for unsigned releases), and each release's own notes
say whether it is signed. What still describes signing as "not yet" and
needs updating then (or ask the AI assistant to do it):

- the *Status* paragraph at the top of this page;
- docs/TECHNICAL_DEBT.md: close TD-44 (keep the unsigned `Uninstall.exe` as
  its own item if it is still unsigned);
- docs/LICENSING.md: the paragraph on code signing ("switches on once the
  project is accepted");
- CHANGELOG.md: note that Windows releases are now code-signed;
- PROJECT_STATUS.md and ROADMAP.md, where they list code signing as open;
- docs/MANUAL_TESTS.md needs no change (it covers both), but do its
  "Install" check once with the signed installer.

### If something goes wrong

- *"Failed to retrieve GitHub App token"*: the SignPath GitHub App is not
  installed on the repository (section 3).
- *Metadata mismatch* on a signing request: the file's product name or
  version differs from what the artifact configuration expects
  (`OpenShape`, CMakeLists.txt's version). SignPath's page shows the value
  it read; fix the version resource (`src/app/openshape.rc.in`,
  `packaging/windows/openshape.nsi`) or the configuration, not by loosening
  it for good.
- *Branch not allowed*: adjust *Allowed branch names* of `release-signing`
  to the ref shown on the request's page.
- *Timeout*: an approval was not given within 60 minutes. Re-run the
  failed run (it builds everything again, about an hour).
- *Too many re-runs*: SignPath accepts at most 3 re-runs of one workflow
  run. Start a new run on the tag instead: *Actions → Release → Run
  workflow → Use workflow from → Tags → v<version>*.
- `use-signed.sh` refuses the returned file: SignPath returned something
  other than the file it was sent plus a signature, or Windows does not
  accept the signature. Do not publish; look at the signing request in
  SignPath.

References: SignPath's GitHub integration
(https://docs.signpath.io/trusted-build-systems/github), artifact
configurations (https://docs.signpath.io/artifact-configuration), projects
and signing policies (https://docs.signpath.io/projects).
