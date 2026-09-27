OpenShape @VERSION@ for Windows (64-bit, Windows 10 or 11).

## Install

- **Installer:** download `OpenShape-@APP_VERSION@-windows-x64-setup.exe` and run it.
  It installs for your user only (no administrator rights) into
  `%LOCALAPPDATA%\Programs\OpenShape`, adds OpenShape to the Start menu
  (optionally to the desktop) and opens `.openshape` files. Remove it from
  *Settings > Apps*; your projects and settings stay.
- **Portable:** download `OpenShape-@APP_VERSION@-windows-x64.zip`, extract it anywhere
  and run `OpenShape.exe` inside.

<!-- if unsigned -->
The installer and the program are not code-signed yet, so Windows SmartScreen
may say "Windows protected your PC": click **More info**, then **Run anyway**.
<!-- end -->
<!-- if signed -->
The installer and `OpenShape.exe` are code-signed: Windows shows the
verified publisher **SignPath Foundation**. Free code signing provided by
SignPath.io, certificate by SignPath Foundation. To check a file, right-click
it: *Properties > Digital Signatures*.
<!-- end -->
`SHA256SUMS.txt` lists the files' SHA-256 checksums
(PowerShell: `Get-FileHash <file>`). Code signing policy:
[docs/CODE_SIGNING.md](https://github.com/SamuelAirs/openshape/blob/@TAG@/docs/CODE_SIGNING.md).

## License and source code

OpenShape is free software under the Mozilla Public License 2.0; its source
code is at https://github.com/SamuelAirs/openshape (this release: tag
`@TAG@`). The bundled libraries keep their own licenses, listed with their
full texts and where to get their source code in `THIRD_PARTY_LICENSES.txt`
next to `OpenShape.exe`: among them Open CASCADE Technology
(LGPL-2.1 with the OCCT exception; built from the upstream tag with
`scripts/windows/build-occt.sh`), Qt 6 (LGPL-3.0) and PlaneGCS (LGPL-2.1,
in the repository under `third_party/planegcs`). They are separate DLLs,
so they can be replaced with modified versions. No GPL-licensed code is
included.
