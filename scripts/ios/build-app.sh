#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Generates the Xcode project for the iOS app (universal: iPhone and iPad)
# and builds an (unsigned) archive for devices: <build-dir>/OpenShape.xcarchive.
# Signing happens when the archive is exported (scripts/ios/testflight.sh),
# or open the project in Xcode to run it on a connected iPhone or iPad.
#
# Usage (macOS, repository root):
#   scripts/ios/build-app.sh <qt-ios-dir> <deps-prefix> [build-dir]
#   e.g. scripts/ios/build-app.sh ~/Qt/6.11.2/ios ~/openshape-ios-deps
# OPENSHAPE_BUILD_NUMBER (optional) sets the bundle's build number;
# OPENSHAPE_RELEASE_TAG (optional, v<version>...) marks an App Store or
# public beta build made from that tag (About -> Licenses then names the
# tag's source); OPENSHAPE_BUNDLE_ID (optional) another bundle identifier,
# e.g. for rebuilding the app for your own devices (BUILDING.md);
# QT_HOST_PATH the Qt for macOS whose tools the build runs (default: the
# "macos" folder beside the iOS one, as the installers lay them out).
#
# The license gate (docs/LICENSING.md) checks the linker's map of the app:
# every file linked in must come from Qt, the iOS libraries, this build or
# Apple's SDK, with its source pinned (sources.txt) and its license in the
# Licenses view (scripts/licenses/licenses.py gate; report in
# <build-dir>/license-gate.txt). OPENSHAPE_LICENSE_GATE=required (release
# tags in ipad.yml) makes a failure fail the build; otherwise it warns.
set -euo pipefail

QT=${1:?usage: build-app.sh <qt-ios-dir> <deps-prefix> [build-dir]}
DEPS=${2:?usage: build-app.sh <qt-ios-dir> <deps-prefix> [build-dir]}
BUILD=${3:-build/ios}
QT=$(cd "$QT" && pwd)
DEPS=$(cd "$DEPS" && pwd)
HOST_QT=${QT_HOST_PATH:-$(cd "$QT/.." && pwd)/macos}
mkdir -p "$BUILD"
BUILD=$(cd "$BUILD" && pwd)
case "${OPENSHAPE_LICENSE_GATE:-warn}" in
    required|warn) ;;
    *) echo "error: OPENSHAPE_LICENSE_GATE must be 'required' or 'warn'"; exit 1 ;;
esac

"$QT/bin/qt-cmake" -S . -B "$BUILD" -G Xcode \
    -DQT_HOST_PATH="$HOST_QT" \
    -DOPENSHAPE_BUILD_TESTS=OFF \
    -DCMAKE_PREFIX_PATH="$DEPS" -DCMAKE_FIND_ROOT_PATH="$DEPS" \
    -DCMAKE_XCODE_GENERATE_SCHEME=ON \
    ${OPENSHAPE_BUILD_NUMBER:+-DOPENSHAPE_BUILD_NUMBER="$OPENSHAPE_BUILD_NUMBER"} \
    -DOPENSHAPE_RELEASE_TAG="${OPENSHAPE_RELEASE_TAG:-}" \
    -DOPENSHAPE_SOURCE_COMMIT="$(git rev-parse HEAD 2>/dev/null || true)" \
    ${OPENSHAPE_BUNDLE_ID:+-DOPENSHAPE_BUNDLE_ID="$OPENSHAPE_BUNDLE_ID"} \
    2>&1 | tee "$BUILD/configure.log"

# Qt is linked statically: Qt's CMake scans the QML files and links the
# plugin of every module they import. One it cannot link would stop the app
# at startup on the device, so that fails the build here.
imports="$BUILD/src/app/.qt/qml_imports/openshape_conf.cmake"
if [ ! -f "$imports" ]; then
    echo "error: Qt did not scan the QML imports ($imports missing): the app would start without its QML plugins"
    ls -la "$BUILD/src/app/.qt" 2>&1 || true
    grep -E "QmlTools_DIR|ShaderToolsTools_DIR|QT_HOST_PATH|qmlimportscanner" "$BUILD/CMakeCache.txt" || true
    ls "$HOST_QT/lib/cmake" | grep -i -E "qml|tools" || true
    grep -i "qmlimportscanner\|QmlTools" "$BUILD/configure.log" || true
    exit 1
fi
if grep -q 'qml_import_scanner_imports_count 0)' "$imports"; then
    echo "error: Qt's QML import scan found no imports (the app would start without its QML plugins)"
    head -c 2000 "$imports"
    exit 1
fi
# Entries look like "CLASSNAME;QtQuick2Plugin;NAME;QtQuick;PATH;...".
# The unused Controls styles are unlinked after the scan (TD-36,
# src/app/CMakeLists.txt), which leaves a list of what it dropped.
dropped="$BUILD/src/app/.qt/qml_imports/openshape_dropped_styles.txt"
[ -f "$dropped" ] || { echo "error: the unused Controls styles were not unlinked ($dropped missing)"; exit 1; }
unused_styles='^QtQuick\.Controls\.(Material|Fusion|Universal|Imagine|FluentWinUI3|iOS|macOS|Windows)(\.impl)?$'
sed -nE 's/.*[";]NAME;([^;"]*).*/\1/p' "$imports" | sort -u | grep -vE "$unused_styles" > "$BUILD/qml-modules.txt"
echo "QML modules linked: $(tr '\n' ' ' < "$BUILD/qml-modules.txt")"
echo "Controls style plugins not linked: $(cat "$dropped")"
if grep -q "will not be linked" "$BUILD/configure.log"; then
    echo "error: a QML plugin will not be linked (see the configure output above)"
    exit 1
fi

rm -rf "$BUILD/OpenShape.xcarchive" "$BUILD/OpenShape-LinkMap.txt"
# Release with debug information (same optimization) and a dSYM, so crash
# reports from TestFlight testers symbolicate (the top-level CMakeLists.txt
# sets the same for Xcode; given here too, for every target). Intermediate
# files go inside the build folder (not ~/Library/Developer), so the license
# gate knows every object file linked from there is this build's.
xcodebuild -project "$BUILD/OpenShape.xcodeproj" -scheme openshape -configuration Release \
    -destination 'generic/platform=iOS' -archivePath "$BUILD/OpenShape.xcarchive" \
    -derivedDataPath "$BUILD/DerivedData" \
    -quiet CODE_SIGNING_ALLOWED=NO \
    DEBUG_INFORMATION_FORMAT=dwarf-with-dsym GCC_GENERATE_DEBUGGING_SYMBOLS=YES archive

# The license gate (see the top). The link map is written by the linker
# (LD_GENERATE_MAP_FILE, src/app/CMakeLists.txt).
gate=0
python3 scripts/licenses/licenses.py gate --link-map "$BUILD/OpenShape-LinkMap.txt" \
    --qt-dir "$QT" --deps-dir "$DEPS" --build-dir "$BUILD" --report "$BUILD/license-gate.txt" || gate=$?
if [ "$gate" -ne 0 ]; then
    if [ "${OPENSHAPE_LICENSE_GATE:-warn}" = required ]; then
        echo "error: the license gate failed ($BUILD/license-gate.txt): a file linked into the app has no known source or license"
        exit 1
    fi
    echo "warning: the license gate failed ($BUILD/license-gate.txt); release tags require it to pass"
fi

APP="$BUILD/OpenShape.xcarchive/Products/Applications/OpenShape.app"
test -d "$APP" || { echo "error: the archive has no app (Products/Applications/OpenShape.app)"; exit 1; }
du -sh "$APP"
ls "$APP"
lipo -info "$APP/OpenShape"
plutil -p "$APP/Info.plist"
for f in Assets.car PrivacyInfo.xcprivacy; do
    test -e "$APP/$f" || { echo "error: the app bundle lacks $f"; exit 1; }
done
# One universal app: Xcode writes TARGETED_DEVICE_FAMILY into UIDeviceFamily
# (1 = iPhone, 2 = iPad).
families=$(plutil -extract UIDeviceFamily json -o - "$APP/Info.plist" 2>/dev/null || echo none)
echo "Device families: $families"
case "$families" in
    *1*2*) ;;
    *) echo "error: the app is not universal (UIDeviceFamily $families; expected 1 and 2)"; exit 1 ;;
esac
# "Open in OpenShape": the document types other apps may hand over.
doctypes=$(plutil -extract CFBundleDocumentTypes json -o - "$APP/Info.plist" 2>/dev/null || echo none)
echo "Document types: $doctypes"
case "$doctypes" in
    *io.github.samuelairs.openshape.project*org.iso.step*) ;;
    *) echo "error: the app does not declare projects and STEP files as document types (CFBundleDocumentTypes)"; exit 1 ;;
esac

# The archive carries the app's dSYM (testflight.sh uploads it with the
# build): without it no crash report from a tester can be read.
DSYMS="$BUILD/OpenShape.xcarchive/dSYMs"
DSYM="$DSYMS/OpenShape.app.dSYM"
if [ ! -d "$DSYM" ]; then
    # Xcode's archive step left the dSYM out (ipad.yml run 37: an empty
    # dSYMs folder), likely because CMake's Xcode project gives each target
    # its own output folder (CONFIGURATION_BUILD_DIR), where dsymutil writes
    # it beside the app. Put it in; the UUID check below makes sure it is
    # this build's.
    built=$(find "$BUILD" "$HOME/Library/Developer/Xcode/DerivedData" -name OpenShape.app.dSYM -type d \
                -not -path "*.xcarchive/*" 2>/dev/null | head -n 1 || true)
    echo "The archive has no dSYM of its own; the build's: ${built:-none}"
    if [ -n "$built" ]; then
        mkdir -p "$DSYMS"
        cp -R "$built" "$DSYMS/"
    fi
fi
if [ -z "$(ls -A "$DSYMS" 2>/dev/null)" ]; then
    echo "dSYMs anywhere in the build: $(find "$BUILD" "$HOME/Library/Developer/Xcode/DerivedData" -name '*.dSYM' -type d 2>/dev/null | head -n 10 | tr '\n' ' ')"
    xcodebuild -project "$BUILD/OpenShape.xcodeproj" -target openshape -configuration Release -showBuildSettings 2>/dev/null \
        | grep -E ' (DEBUG_INFORMATION_FORMAT|GCC_GENERATE_DEBUGGING_SYMBOLS|DWARF_DSYM_FOLDER_PATH|CONFIGURATION_BUILD_DIR|BUILT_PRODUCTS_DIR) =' || true
    echo "error: the archive's dSYMs folder is empty or missing ($DSYMS): crash reports would not symbolicate"
    exit 1
fi
ls -la "$DSYMS"
DWARF="$DSYM/Contents/Resources/DWARF/OpenShape"
test -f "$DWARF" || { echo "error: no dSYM for the app ($DWARF missing)"; exit 1; }
app_uuid=$(dwarfdump --uuid "$APP/OpenShape" | awk '{print $2}')
dsym_uuid=$(dwarfdump --uuid "$DSYM" | awk '{print $2}')
echo "App binary UUID: $app_uuid; dSYM UUID: $dsym_uuid; dSYM size: $(du -sh "$DSYM" | cut -f1)"
if [ -z "$app_uuid" ] || [ "$app_uuid" != "$dsym_uuid" ]; then
    echo "error: the dSYM does not belong to the app binary (UUIDs differ)"
    exit 1
fi
# OpenShape's own code has debug information in it (a function found by name).
found=$(dwarfdump --name=shareFile "$DWARF" 2>/dev/null | head -c 4000 || true)
case "$found" in
    *shareFile*) echo "dSYM: OpenShape's code has debug information (AppController::shareFile found)" ;;
    *) echo "error: the dSYM has no debug information for OpenShape's code (AppController::shareFile not found)"; exit 1 ;;
esac
