#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Downloads the source code of the LGPL libraries a Windows package ships,
# so a release can offer it next to the binaries for as long as the release
# is offered (TD-46): MSYS2 deletes old source archives from its server.
#   - every MSYS2 source archive THIRD_PARTY_LICENSES.txt names
#     ("Source code: https://repo.msys2.org/mingw/sources/...");
#   - OpenCASCADE's source and the MSYS2 patches build-occt.sh applies,
#     checked against the SHA-256 values pinned there.
# Writes <out-dir>/SOURCES-SHA256SUMS.txt last.
#
# Usage: scripts/ci/mirror-sources.sh <THIRD_PARTY_LICENSES.txt> <out-dir> [build-occt.sh]
set -euo pipefail

licenses=${1:?usage: mirror-sources.sh <THIRD_PARTY_LICENSES.txt> <out-dir> [build-occt.sh]}
out=${2:?usage: mirror-sources.sh <THIRD_PARTY_LICENSES.txt> <out-dir> [build-occt.sh]}
occt_script=${3:-scripts/windows/build-occt.sh}
mkdir -p "$out"

fetch() { # <url> <file>
    # Retry any error (a reset connection too); give up on a stalled transfer.
    curl -fsSL --retry 5 --retry-delay 5 --retry-all-errors --speed-limit 10000 --speed-time 60 -o "$2.part" "$1"
    mv "$2.part" "$2"
}

[ -f "$licenses" ] || { echo "mirror-sources.sh: $licenses not found"; exit 1; }
urls=$(grep -oE 'https://repo\.msys2\.org/mingw/sources/[^ <>,]+\.src\.tar\.zst' "$licenses" | sort -u || true)
[ -n "$urls" ] || { echo "mirror-sources.sh: no MSYS2 source archives named in $licenses"; exit 1; }
count=0
for url in $urls; do
    echo "== $url"
    fetch "$url" "$out/${url##*/}"
    count=$((count + 1))
done

# OpenCASCADE: the same source and patches as build-occt.sh, verified.
value() { sed -n "s/^$1=\"\{0,1\}\([^\"]*\)\"\{0,1\}$/\1/p" "$occt_script" | head -n 1; }
occt_tag=$(value OCCT_TAG)
occt_sha=$(value OCCT_SHA256)
msys2_commit=$(value MSYS2_COMMIT)
[ -n "$occt_tag" ] && [ -n "$occt_sha" ] && [ -n "$msys2_commit" ] || { echo "mirror-sources.sh: OCCT_TAG, OCCT_SHA256 or MSYS2_COMMIT not found in $occt_script"; exit 1; }
occt_file="$out/opencascade-$occt_tag.tar.gz"
echo "== OpenCASCADE $occt_tag"
fetch "https://github.com/Open-Cascade-SAS/OCCT/archive/refs/tags/$occt_tag.tar.gz" "$occt_file"
echo "$occt_sha  $occt_file" | sha256sum -c --status || { echo "SHA-256 mismatch for $occt_file"; exit 1; }

patch_dir=$(mktemp -d)
trap 'rm -rf "$patch_dir"' EXIT
mkdir -p "$patch_dir/opencascade-$occt_tag-patches"
patches=0
while read -r name sha; do
    file="$patch_dir/opencascade-$occt_tag-patches/$name"
    fetch "https://raw.githubusercontent.com/msys2/MINGW-packages/$msys2_commit/mingw-w64-opencascade/$name" "$file"
    echo "$sha  $file" | sha256sum -c --status || { echo "SHA-256 mismatch for $name"; exit 1; }
    patches=$((patches + 1))
done < <(sed -n '/^PATCHES=(/,/^)/s/^ *"\([^ ]*\.patch\) \([0-9a-f]\{64\}\)"$/\1 \2/p' "$occt_script")
[ "$patches" -gt 0 ] || { echo "mirror-sources.sh: no patches found in $occt_script"; exit 1; }
cp "$occt_script" "$patch_dir/opencascade-$occt_tag-patches/build-occt.sh"
tar -czf "$out/opencascade-$occt_tag-patches.tar.gz" -C "$patch_dir" "opencascade-$occt_tag-patches"

(cd "$out" && sha256sum -- *.tar.* > SOURCES-SHA256SUMS.txt)
echo "mirror-sources.sh: $count MSYS2 source archives, OpenCASCADE $occt_tag and $patches patches in $out"
