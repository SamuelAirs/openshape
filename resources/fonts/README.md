# Fonts built into OpenShape

The Text tool (emboss / deboss) makes its letters from **Noto Sans**, which is
built into the executable (`src/app/CMakeLists.txt` embeds the files below as
Qt resources; `ui/AppController` registers them with the geometry layer at
startup). Steps store the font by id (`NotoSans-Regular`, `NotoSans-Bold`),
never the font itself.

Files here (re-run CMake's configure step after replacing them):

| File | What |
|---|---|
| `NotoSans-Regular.ttf` | Noto Sans Regular, static unhinted TTF (required for the Text tool) |
| `NotoSans-Bold.ttf` | Noto Sans Bold, static unhinted TTF (the tool's Bold switch) |
| `OFL.txt` | the SIL Open Font License 1.1 as shipped with the release (required whenever a font is here; `scripts/package-windows.sh` copies it next to OpenShape.exe as `NotoSans-OFL.txt`) |

Source: the official Noto Sans release of the Noto project,
<https://github.com/notofonts/latin-greek-cyrillic/releases> (tag
`NotoSans-v2.013`, zip `NotoSans-v2.013.zip`: `NotoSans/unhinted/ttf/NotoSans-Regular.ttf`,
`.../NotoSans-Bold.ttf`, and `OFL.txt`), added unmodified on 2026-09-26. The
SHA-256 of each file (and of the zip) is in `THIRD_PARTY.md`: record the new
ones there when updating them. `.gitattributes` keeps `*.ttf` binary, so a
checkout has exactly these bytes.

A checkout without `NotoSans-Regular.ttf` still builds (CMake warns) and the
Text tool says that text is not available. For development only, the
environment variable `OPENSHAPE_TEXT_FONT=<path to a .ttf>` makes such a
build use that font in its place; files saved that way still name
`NotoSans-Regular`. The tests read these files from the source tree
(`tests/TestFonts.h`).
