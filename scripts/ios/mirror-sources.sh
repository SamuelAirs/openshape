#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Downloads the source code of every library in the iOS app (iPhone and
# iPad), exactly as sources.txt pins it (each archive's SHA-256 is checked),
# so a release can offer it next to the app for as long as the app is
# offered, as the LGPL asks for Qt, OpenCASCADE and PlaneGCS
# (docs/LICENSING.md, "The iOS app and the App Store"). release.yml packs the
# folder into OpenShape-<version>-ios-sources.tar on the GitHub release.
#   - the Qt modules (Qt for iOS is The Qt Company's build of that source),
#     OpenCASCADE, FreeType, libzip, nlohmann/json and Eigen, named
#     <name>-<version>.<ext>;
#   - README.txt (what this is, how to rebuild the app with it) and
#     sources.txt (the pins);
#   - SOURCES-SHA256SUMS.txt, written last.
# OpenShape's own source and PlaneGCS (third_party/planegcs) are the
# repository at the release's tag (GitHub's "Source code" archives).
#
# Usage: scripts/ios/mirror-sources.sh <out-dir> [tag]
# Environment: OPENSHAPE_IOS_SOURCES (default: sources.txt beside this
# script), OPENSHAPE_SOURCES_DIR (copy from there instead of downloading).
set -euo pipefail

out=${1:?usage: mirror-sources.sh <out-dir> [tag]}
tag=${2:-}
here=$(cd "$(dirname "$0")" && pwd)
sources=${OPENSHAPE_IOS_SOURCES:-$here/sources.txt}
[ -f "$sources" ] || { echo "mirror-sources.sh: $sources not found"; exit 1; }
mkdir -p "$out"

names=$(awk '$1 !~ /^#/ && NF >= 4 { print $1 }' "$sources")
[ -n "$names" ] || { echo "mirror-sources.sh: no sources pinned in $sources"; exit 1; }
count=0
for name in $names; do
    OPENSHAPE_IOS_SOURCES="$sources" bash "$here/fetch-source.sh" "$name" "$out" >/dev/null
    count=$((count + 1))
done

cp "$sources" "$out/sources.txt"
if [ -n "$tag" ]; then
    where="https://github.com/SamuelAirs/openshape/tree/$tag"
    guide="https://github.com/SamuelAirs/openshape/blob/$tag/BUILDING.md"
else
    where="the OpenShape repository (https://github.com/SamuelAirs/openshape), at the release's tag"
    guide="BUILDING.md in the repository, at the release's tag"
fi
cat > "$out/README.txt" <<EOF
Source code of the libraries in OpenShape's iOS app (iPhone and iPad)
${tag:+for $tag}

These are the exact source archives the app is built from, as pinned in
sources.txt (name, version, SHA-256, where it was downloaded from):
Qt (LGPL-3.0; Qt for iOS is The Qt Company's build of this source),
Open CASCADE Technology (LGPL-2.1 with the OCCT exception), FreeType (FTL),
libzip (BSD-3-Clause), nlohmann/json (MIT) and Eigen (MPL-2.0).
SOURCES-SHA256SUMS.txt lists their SHA-256.

OpenShape's own source (MPL-2.0), its build scripts and PlaneGCS
(LGPL-2.1-or-later, third_party/planegcs) are in $where
(also attached to the release as "Source code").

To rebuild the app, also with modified versions of these libraries, follow
"Rebuilding the iOS app with modified libraries" in
$guide
(set OPENSHAPE_SOURCES_DIR to this folder to build from these archives).
EOF

(cd "$out" && for f in *.tar.*; do
    if command -v sha256sum >/dev/null 2>&1; then sha256sum -- "$f"; else shasum -a 256 -- "$f"; fi
done > SOURCES-SHA256SUMS.txt)
[ "$(wc -l < "$out/SOURCES-SHA256SUMS.txt")" -eq "$count" ] \
    || { echo "mirror-sources.sh: $out holds other archives than the $count pinned ones"; exit 1; }
echo "mirror-sources.sh: $count iOS source archives in $out ($(du -sh "$out" | cut -f1))"
