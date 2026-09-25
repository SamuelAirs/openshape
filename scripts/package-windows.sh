#!/usr/bin/env bash
# Builds a self-contained OpenShape folder for Windows from an MSYS2 UCRT64
# build: the executable, every DLL it needs (Qt, OCCT, PlaneGCS, runtime),
# Qt plugins and QML modules, plus license information.
#
# Usage (MSYS2 UCRT64 shell, repository root):
#   scripts/package-windows.sh [build-dir] [output-dir]
# Defaults: build/msys2-ucrt64  dist/OpenShape
set -euo pipefail

BUILD_DIR="${1:-build/msys2-ucrt64}"
OUT_DIR="${2:-dist/OpenShape}"
EXE="$BUILD_DIR/bin/OpenShape.exe"

command -v windeployqt6 >/dev/null || { echo "windeployqt6 not found (install mingw-w64-ucrt-x86_64-qt6-base)"; exit 1; }
command -v ntldd >/dev/null || { echo "ntldd not found (pacman -S mingw-w64-ucrt-x86_64-ntldd)"; exit 1; }
[ -f "$EXE" ] || { echo "missing $EXE - build first"; exit 1; }

rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"
cp "$EXE" "$BUILD_DIR/bin/libplanegcs.dll" "$OUT_DIR/"
# Development builds carry debug info; ship stripped copies.
strip --strip-debug "$OUT_DIR/OpenShape.exe" "$OUT_DIR/libplanegcs.dll"

# Qt libraries, plugins (platform, styles, image formats) and QML imports.
windeployqt6 --qmldir src/ui/qml --no-translations --compiler-runtime "$OUT_DIR/OpenShape.exe" >/dev/null 2>&1

# Trim what OpenShape does not use (each pulls in large codec/TLS libraries):
# only the SVG image plugin is needed (window icon), only the Basic controls style.
rm -rf "$OUT_DIR"/{qmltooling,tls,networkinformation,generic}
find "$OUT_DIR/imageformats" -iname '*.dll' ! -iname 'qsvg*.dll' -delete
for style in Imagine Material Universal Fusion FluentWinUI3 Windows; do
    rm -rf "$OUT_DIR/qml/QtQuick/Controls/$style"
    rm -f "$OUT_DIR"/Qt6QuickControls2"$style"*.dll
done

# Qt resolves plugins/QML relative to its install prefix unless told otherwise.
cat > "$OUT_DIR/qt.conf" <<'CONF'
[Paths]
Prefix = .
Plugins = .
QmlImports = qml
CONF

# Everything else the binaries load from the MSYS2 prefix (OCCT, TBB,
# FreeType, libzip, ...). Scan the exe and every DLL/plugin copied so far.
prefix="$(cygpath -m "$(dirname "$(command -v ntldd)")")"
find "$OUT_DIR" -iname '*.dll' -o -iname '*.exe' | while read -r binary; do
    ntldd -R "$binary" 2>/dev/null | awk '{print $3}'
done | tr '\\' '/' | grep -i "^${prefix}" | sort -u | while read -r dll; do
    target="$OUT_DIR/$(basename "$dll")"
    [ -f "$target" ] || cp "$(cygpath -u "$dll")" "$target"
done

# Licenses: our pending-license notice, the dependency list, and the LGPL
# text for the vendored solver.
cp LICENSE_PENDING.md THIRD_PARTY.md README.md "$OUT_DIR/"
cp third_party/planegcs/COPYING.LIB "$OUT_DIR/PlaneGCS-COPYING.LIB.txt"

count=$(find "$OUT_DIR" -type f | wc -l)
size=$(du -sh "$OUT_DIR" | cut -f1)
echo "Packaged $OUT_DIR ($count files, $size)"
