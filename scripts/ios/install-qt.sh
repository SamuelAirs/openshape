#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Installs Qt for iOS and the matching Qt for macOS (the iOS build runs its
# tools, e.g. the shader compiler, on the Mac) with aqtinstall, from the
# official Qt download servers. (The Qt Online Installer with the "macOS"
# and "iOS" components of one version gives the same result.)
#
# Usage (macOS): scripts/ios/install-qt.sh <qt-version> <install-dir>
# Result: <install-dir>/<qt-version>/ios and <install-dir>/<qt-version>/macos
set -euo pipefail

VERSION=${1:?usage: install-qt.sh <qt-version> <install-dir>}
DIR=${2:?usage: install-qt.sh <qt-version> <install-dir>}
VENV=${AQT_VENV:-${TMPDIR:-/tmp}/aqt-venv}

python3 -m venv "$VENV"
"$VENV/bin/pip" install --quiet --upgrade aqtinstall
AQT="$VENV/bin/aqt"
"$AQT" version

"$AQT" install-qt mac desktop "$VERSION" clang_64 -m qtshadertools -O "$DIR"
# Newer Qt versions list iOS under the "all_os" host.
"$AQT" install-qt mac ios "$VERSION" ios -m qtshadertools -O "$DIR" \
    || "$AQT" install-qt all_os ios "$VERSION" ios -m qtshadertools -O "$DIR"

ls "$DIR/$VERSION"
