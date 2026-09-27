#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Gets one pinned source archive of the iOS app's libraries (sources.txt)
# into a folder as <name>-<version>.<ext> and checks its SHA-256; prints
# the file's path. An archive already there with the right SHA-256 is kept.
# With OPENSHAPE_SOURCES_DIR set (e.g. the unpacked iOS source archive of a
# GitHub release), the archive is copied from there instead of downloaded.
# Used by build-deps.sh and mirror-sources.sh.
#
# Usage: scripts/ios/fetch-source.sh <name> <dir>
# Environment: OPENSHAPE_IOS_SOURCES (default: sources.txt beside this script)
set -euo pipefail

name=${1:?usage: fetch-source.sh <name> <dir>}
dir=${2:?usage: fetch-source.sh <name> <dir>}
here=$(cd "$(dirname "$0")" && pwd)
sources=${OPENSHAPE_IOS_SOURCES:-$here/sources.txt}

line=$(awk -v n="$name" '$1 == n && $1 !~ /^#/ { print $2, $3, $4; exit }' "$sources")
[ -n "$line" ] || { echo "fetch-source.sh: '$name' is not pinned in $sources" >&2; exit 1; }
read -r version sha url <<<"$line"
case "$sha" in
    [0-9a-f][0-9a-f][0-9a-f][0-9a-f]*) [ ${#sha} -eq 64 ] || { echo "fetch-source.sh: bad SHA-256 for $name in $sources" >&2; exit 1; } ;;
    *) echo "fetch-source.sh: bad SHA-256 for $name in $sources" >&2; exit 1 ;;
esac
case "$url" in
    *.tar.gz) ext=tar.gz ;;
    *.tar.xz) ext=tar.xz ;;
    *.tar.bz2) ext=tar.bz2 ;;
    *) echo "fetch-source.sh: $name: unsupported archive type ($url)" >&2; exit 1 ;;
esac

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1"; else shasum -a 256 "$1"; fi | awk '{ print $1 }'
}

mkdir -p "$dir"
file="$dir/$name-$version.$ext"
if [ -f "$file" ] && [ "$(sha256 "$file")" = "$sha" ]; then
    echo "$file"
    exit 0
fi
if [ -n "${OPENSHAPE_SOURCES_DIR:-}" ] && [ -f "$OPENSHAPE_SOURCES_DIR/$name-$version.$ext" ]; then
    echo "== $name $version: copied from $OPENSHAPE_SOURCES_DIR" >&2
    cp "$OPENSHAPE_SOURCES_DIR/$name-$version.$ext" "$file.part"
else
    echo "== $name $version: downloading $url" >&2
    # Retry any error (a reset connection too); give up on a stalled transfer.
    curl -fsSL --retry 5 --retry-delay 5 --retry-all-errors --speed-limit 10000 --speed-time 60 \
        -o "$file.part" "$url"
fi
actual=$(sha256 "$file.part")
if [ "$actual" != "$sha" ]; then
    rm -f "$file.part"
    echo "fetch-source.sh: SHA-256 mismatch for $name $version ($url): expected $sha, got $actual" >&2
    exit 1
fi
mv "$file.part" "$file"
echo "$file"
