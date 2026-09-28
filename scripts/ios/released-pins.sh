#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Which release tag carries the iOS library pins of this checkout: prints
# the newest tag v* whose scripts/ios/sources.txt is the same as the
# working tree's, or nothing (exit 1) when no tag has them.
#
# TestFlight builds of main go to testers (the public beta), and the
# written offer in About -> Licenses promises their library sources for
# three years. A release keeps checked copies of one pinned set
# (release.yml: OpenShape-<version>-ios-sources.tar), so ipad.yml warns
# while no release has this build's pins (docs/LICENSING.md, "The iOS app
# and the App Store"). Tags without sources.txt (before 2026-09-27) never
# match. Needs the tags' commits (ipad.yml fetches them).
#
# Usage: scripts/ios/released-pins.sh [repository-dir]
set -euo pipefail

repo=${1:-.}
pins="$repo/scripts/ios/sources.txt"
[ -f "$pins" ] || { echo "released-pins.sh: $pins not found" >&2; exit 2; }
for tag in $(git -C "$repo" tag --list 'v*' --sort=-creatordate); do
    if git -C "$repo" show "$tag:scripts/ios/sources.txt" 2>/dev/null | cmp -s - "$pins"; then
        echo "$tag"
        exit 0
    fi
done
exit 1
