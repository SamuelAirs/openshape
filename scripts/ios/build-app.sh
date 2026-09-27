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
# QT_HOST_PATH the Qt for macOS whose tools the build runs (default: the
# "macos" folder beside the iOS one, as the installers lay them out).
set -euo pipefail

QT=${1:?usage: build-app.sh <qt-ios-dir> <deps-prefix> [build-dir]}
DEPS=${2:?usage: build-app.sh <qt-ios-dir> <deps-prefix> [build-dir]}
BUILD=${3:-build/ios}
DEPS=$(cd "$DEPS" && pwd)
HOST_QT=${QT_HOST_PATH:-$(cd "$QT/.." && pwd)/macos}
mkdir -p "$BUILD"

"$QT/bin/qt-cmake" -S . -B "$BUILD" -G Xcode \
    -DQT_HOST_PATH="$HOST_QT" \
    -DOPENSHAPE_BUILD_TESTS=OFF \
    -DCMAKE_PREFIX_PATH="$DEPS" -DCMAKE_FIND_ROOT_PATH="$DEPS" \
    -DCMAKE_XCODE_GENERATE_SCHEME=ON \
    ${OPENSHAPE_BUILD_NUMBER:+-DOPENSHAPE_BUILD_NUMBER="$OPENSHAPE_BUILD_NUMBER"} \
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

rm -rf "$BUILD/OpenShape.xcarchive"
# Release with debug information (same optimization) and a dSYM, so crash
# reports from TestFlight testers symbolicate (the top-level CMakeLists.txt
# sets the same for Xcode; given here too, for every target).
xcodebuild -project "$BUILD/OpenShape.xcodeproj" -scheme openshape -configuration Release \
    -destination 'generic/platform=iOS' -archivePath "$BUILD/OpenShape.xcarchive" \
    -quiet CODE_SIGNING_ALLOWED=NO \
    DEBUG_INFORMATION_FORMAT=dwarf-with-dsym GCC_GENERATE_DEBUGGING_SYMBOLS=YES archive

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
if [ -z "$(ls -A "$DSYMS" 2>/dev/null)" ]; then
    echo "error: the archive's dSYMs folder is empty or missing ($DSYMS): crash reports would not symbolicate"
    exit 1
fi
ls -la "$DSYMS"
DSYM="$DSYMS/OpenShape.app.dSYM"
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
