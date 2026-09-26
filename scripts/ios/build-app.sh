#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Generates the Xcode project for the iPad app and builds an (unsigned)
# archive for devices: <build-dir>/OpenShape.xcarchive. Signing happens
# when the archive is exported (scripts/ios/testflight.sh), or open the
# project in Xcode to run it on a connected iPad.
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

"$QT/bin/qt-cmake" -S . -B "$BUILD" -G Xcode \
    -DQT_HOST_PATH="$HOST_QT" \
    -DOPENSHAPE_BUILD_TESTS=OFF \
    -DCMAKE_PREFIX_PATH="$DEPS" -DCMAKE_FIND_ROOT_PATH="$DEPS" \
    -DCMAKE_XCODE_GENERATE_SCHEME=ON \
    ${OPENSHAPE_BUILD_NUMBER:+-DOPENSHAPE_BUILD_NUMBER="$OPENSHAPE_BUILD_NUMBER"}

rm -rf "$BUILD/OpenShape.xcarchive"
xcodebuild -project "$BUILD/OpenShape.xcodeproj" -scheme openshape -configuration Release \
    -destination 'generic/platform=iOS' -archivePath "$BUILD/OpenShape.xcarchive" \
    -quiet CODE_SIGNING_ALLOWED=NO archive

APP="$BUILD/OpenShape.xcarchive/Products/Applications/OpenShape.app"
test -d "$APP" || { echo "error: the archive has no app (Products/Applications/OpenShape.app)"; exit 1; }
du -sh "$APP"
ls "$APP"
lipo -info "$APP/OpenShape"
plutil -p "$APP/Info.plist"
for f in Assets.car PrivacyInfo.xcprivacy; do
    test -e "$APP/$f" || { echo "error: the app bundle lacks $f"; exit 1; }
done
