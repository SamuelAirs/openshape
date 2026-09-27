#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Builds a self-contained OpenShape folder for Windows from an MSYS2 UCRT64
# build: the executable, every DLL it needs (Qt, OCCT, PlaneGCS, runtime),
# Qt plugins and QML modules, plus license information.
#
# Usage (MSYS2 UCRT64 shell, repository root):
#   scripts/package-windows.sh [build-dir] [output-dir]
# Defaults: build/msys2-ucrt64  dist/OpenShape
#
# A release build (preset msys2-ucrt64-release) links OpenShape's own
# OpenCASCADE (scripts/windows/build-occt.sh, found through the build's
# CMake cache); its package must pass the license gate
# (scripts/windows/license-gate.sh) or this script fails. A development
# build links MSYS2's OCCT, which pulls in GPL FFmpeg: that package is for
# local use only, and the gate's result is a warning.
set -euo pipefail

BUILD_DIR="${1:-build/msys2-ucrt64}"
OUT_DIR="${2:-dist/OpenShape}"
EXE="$BUILD_DIR/bin/OpenShape.exe"
CACHE="$BUILD_DIR/CMakeCache.txt"

command -v windeployqt6 >/dev/null || { echo "windeployqt6 not found (install mingw-w64-ucrt-x86_64-qt6-base)"; exit 1; }
command -v ntldd >/dev/null || { echo "ntldd not found (pacman -S mingw-w64-ucrt-x86_64-ntldd)"; exit 1; }
[ -f "$EXE" ] || { echo "missing $EXE - build first"; exit 1; }
[ -f "$CACHE" ] || { echo "missing $CACHE"; exit 1; }
start=$SECONDS

# Which OpenCASCADE the build links.
own_occt=""
if grep -q '^OPENSHAPE_OWN_OCCT:BOOL=ON' "$CACHE"; then
    own_occt=$(sed -n 's/^OPENSHAPE_OCCT_PREFIX:PATH=//p' "$CACHE")
    [ -f "$own_occt/BUILD-INFO.txt" ] || { echo "OpenShape's OCCT build not found in '$own_occt' (scripts/windows/build-occt.sh)"; exit 1; }
    echo "Release build: OpenCASCADE from $own_occt"
else
    echo "Development build (MSYS2's OpenCASCADE): the package is for local use only"
fi

# Refuse while the packaged app is running: on Windows, MSYS2's rm can delete
# the files of a running program, which breaks it (e.g. its file dialogs).
if [ -d "$OUT_DIR" ]; then
    out_win=$(cygpath -w "$(cd "$OUT_DIR" && pwd)")
    running=$(powershell.exe -NoProfile -Command "@(Get-Process -ErrorAction SilentlyContinue | Where-Object { \$_.Path -and \$_.Path.StartsWith('$out_win\\', 'OrdinalIgnoreCase') }).Count" | tr -d '\r')
    case "$running" in
        0) ;;
        ''|*[!0-9]*) echo "Could not check whether OpenShape is running from $OUT_DIR ($running)."; exit 1 ;;
        *) echo "OpenShape is running from $OUT_DIR: close it, then package again."; exit 1 ;;
    esac
fi

rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"
cp "$EXE" "$BUILD_DIR/bin/libplanegcs.dll" "$OUT_DIR/"
# Development builds carry debug info; ship stripped copies.
strip --strip-debug "$OUT_DIR/OpenShape.exe" "$OUT_DIR/libplanegcs.dll"

# Qt libraries, plugins (platform, styles, image formats) and QML imports.
windeployqt6 --qmldir src/ui/qml --no-translations --compiler-runtime "$OUT_DIR/OpenShape.exe" >/dev/null 2>&1

# Trim what OpenShape does not use (each pulls in large codec/TLS libraries):
# only the SVG image plugin is needed (window icon), only the Basic controls
# style (NativeStyle serves the native Windows/macOS styles; checked by the
# full acceptance run of the packaged app).
rm -rf "$OUT_DIR"/{qmltooling,tls,networkinformation,generic} "$OUT_DIR/qml/QtQuick/NativeStyle"
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

# Everything else the binaries load: OCCT (own build or MSYS2's), FreeType,
# libzip, the runtime, ... Only the entry points are scanned (the exe, Qt's
# libraries, plugins and QML modules), in one recursive ntldd call: the
# other DLLs are their dependencies (TD-34: scanning all ~360 files one by
# one took minutes).
prefix="$(cygpath -m "$(dirname "$(command -v ntldd)")")"
if [ -n "$own_occt" ]; then
    # ntldd looks next to the scanned file first, then in its own folder
    # (MSYS2's bin, with MSYS2's OCCT), then on PATH: so our OCCT goes into
    # the package before the scan; toolkits nothing loads are removed after it.
    own_bin=$(cygpath -m "$own_occt/bin")
    cp "$own_bin"/libTK*.dll "$OUT_DIR/"
fi
mapfile -t roots < <(find "$OUT_DIR" -type f \( -iname '*.exe' -o -iname '*.dll' \) ! -iname 'libTK*.dll' | sort)
ntldd -R "${roots[@]}" 2>/dev/null | awk 'NF >= 3 { print $3 }' | tr '\\' '/' | sort -u > "$OUT_DIR/.deps"
while read -r dll; do
    target="$OUT_DIR/$(basename "$dll")"
    [ -f "$target" ] || cp "$(cygpath -u "$dll")" "$target"
done < <(grep -iE "^${prefix}/" "$OUT_DIR/.deps")
if [ -n "$own_occt" ]; then
    for tk in "$OUT_DIR"/libTK*.dll; do
        name=$(basename "$tk")
        grep -qiF "/$name" "$OUT_DIR/.deps" || { rm "$tk"; continue; } # e.g. TKFeat: linked, no symbol used
        # Every OCCT toolkit must be our build (MSYS2's links GPL FFmpeg).
        cmp -s "$tk" "$own_bin/$name" || { echo "$name is not from $own_bin"; exit 1; }
    done
fi
rm -f "$OUT_DIR/.deps"

# Licenses: OpenShape's (MPL-2.0), the dependency list, and the LGPL text
# for the vendored solver.
cp LICENSE "$OUT_DIR/LICENSE.txt"
cp THIRD_PARTY.md README.md "$OUT_DIR/"
cp third_party/planegcs/COPYING.LIB "$OUT_DIR/PlaneGCS-COPYING.LIB.txt"
# Noto Sans (SIL Open Font License 1.1) is built into OpenShape.exe for the
# Text tool (resources/fonts/); its license goes along.
if [ -f resources/fonts/OFL.txt ]; then
    cp resources/fonts/OFL.txt "$OUT_DIR/NotoSans-OFL.txt"
else
    echo "warning: resources/fonts/OFL.txt missing (the Noto Sans font's license); the Text tool has no font in this build"
fi

# The license texts of every bundled library, from the MSYS2 packages they
# came from (plugins and QML modules belong to the Qt packages found here),
# with where their exact source is; OpenCASCADE's own build first.
if command -v pacman >/dev/null; then
    owned=()
    for dll in "$OUT_DIR"/*.dll; do
        name=$(basename "$dll")
        [ -n "$own_occt" ] && [ -f "$own_bin/$name" ] && continue
        owned+=("$prefix/$name")
    done
    # Batched queries (pacman scans its database once per call) and no
    # pacman inside a `while read` loop: it would read the loop's input.
    mapfile -t packages < <({ pacman -Qqo "${owned[@]}" 2>/dev/null || true; } | sort -u)
    info="$(pacman -Qi "${packages[@]}" </dev/null)"
    licenseFiles="$(pacman -Ql "${packages[@]}" </dev/null | grep '/share/licenses/.' || true)"
    msysRoot=${prefix%/*}
    msysRoot=${msysRoot%/*} # package paths are relative to it
    {
        echo "Libraries bundled with OpenShape and their licenses. OpenShape's own"
        echo "license is in LICENSE.txt; PlaneGCS's (LGPL-2.1) in PlaneGCS-COPYING.LIB.txt."
        echo "Where to get the source code of each is given with it (\"Source code\")."
        if [ -n "$own_occt" ]; then
            echo
            echo "================================================================================"
            echo "Open CASCADE Technology (libTK*.dll)"
            echo "Licenses: LGPL-2.1-or-later WITH OCCT-exception-1.0"
            echo "Source:   https://dev.opencascade.org"
            echo "Source code: https://github.com/Open-Cascade-SAS/OCCT/tree/$(sed -n 's|^Source: .*/tags/\(V[0-9_]*\)\.tar\.gz$|\1|p' "$own_occt/BUILD-INFO.txt"),"
            echo "  built with scripts/windows/build-occt.sh in OpenShape's repository:"
            sed 's/^/  /' "$own_occt/BUILD-INFO.txt"
            echo "================================================================================"
            for file in "$own_occt"/share/licenses/opencascade/*; do
                echo "--- ${file##*/}"
                cat "$file"
            done
        fi
        echo
        echo "The following ${#packages[@]} MSYS2 packages (https://www.msys2.org) provide the"
        echo "other libraries. Each package's exact source code, including MSYS2's build"
        echo "recipe and patches, is at https://repo.msys2.org/mingw/sources/<base>-<version>.src.tar.zst."
        for pkg in "${packages[@]}"; do
            echo
            echo "================================================================================"
            awk -v p="$pkg" '/^Name *:/ { show = ($3 == p) } show && /^(Name|Version|Licenses|URL) *:/' <<<"$info" \
                | sed -e 's/^Name *: /Package:  /' -e 's/^Version *: /Version:  /' \
                      -e 's/^Licenses *: /Licenses: /' -e 's/^URL *: /Source:   /'
            version=$(awk -v p="$pkg" '/^Name *:/ { show = ($3 == p) } show && /^Version *:/ { print $3 }' <<<"$info")
            base=$(sed -n '/^%BASE%$/{n;p;q}' "$msysRoot/var/lib/pacman/local/$pkg-$version/desc" 2>/dev/null || true)
            [ -n "$base" ] && echo "Source code: https://repo.msys2.org/mingw/sources/$base-$version.src.tar.zst"
            echo "================================================================================"
            mapfile -t files < <(grep "^$pkg " <<<"$licenseFiles" | cut -d' ' -f2-)
            for file in "${files[@]}"; do
                path="${msysRoot%/}$file"
                [ -f "$path" ] || continue
                echo "--- ${file##*/}"
                cat "$path"
            done
        done
    } > "$OUT_DIR/THIRD_PARTY_LICENSES.txt"
else
    echo "warning: pacman not found; THIRD_PARTY_LICENSES.txt not generated"
fi

count=$(find "$OUT_DIR" -type f | wc -l)
size=$(du -sh "$OUT_DIR" | cut -f1)
echo "Packaged $OUT_DIR ($count files, $size) in $((SECONDS - start)) s"

# No GPL code in what we distribute (scripts/windows/license-gate.sh).
if [ -n "$own_occt" ]; then
    "$(dirname "$0")/windows/license-gate.sh" "$OUT_DIR" "$own_occt"
else
    "$(dirname "$0")/windows/license-gate.sh" "$OUT_DIR" > "$BUILD_DIR/license-gate.txt" \
        && echo "License gate: PASS" \
        || echo "License gate: FAIL - not distributable (MSYS2's OCCT links GPL FFmpeg; details in $BUILD_DIR/license-gate.txt). Package a release build to distribute."
fi
