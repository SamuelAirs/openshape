#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Prints the install and license part of a Windows release's notes from
# packaging/windows/release-notes.md (.github/workflows/release.yml): fills
# in @VERSION@ (the tag's version, e.g. 0.2.0-rc1), @APP_VERSION@ (the
# version in the file names, CMakeLists.txt's) and @TAG@, and keeps the
# block for a signed or an unsigned release. Blocks are marked by lines
#   <!-- if signed -->  or  <!-- if unsigned -->  ...  <!-- end -->
# (not nested); the marker lines themselves are dropped.
#
# Usage: scripts/ci/install-notes.sh <version> <app-version> <tag> <signed|unsigned> [template]
set -euo pipefail

version=${1:?usage: install-notes.sh <version> <app-version> <tag> <signed|unsigned> [template]}
app_version=${2:?usage: install-notes.sh <version> <app-version> <tag> <signed|unsigned> [template]}
tag=${3:?usage: install-notes.sh <version> <app-version> <tag> <signed|unsigned> [template]}
signing=${4:?usage: install-notes.sh <version> <app-version> <tag> <signed|unsigned> [template]}
template=${5:-packaging/windows/release-notes.md}

case "$signing" in
    signed) drop=unsigned ;;
    unsigned) drop=signed ;;
    *) echo "install-notes: '$signing' is neither signed nor unsigned" >&2; exit 1 ;;
esac

awk -v drop="$drop" -v version="$version" -v app_version="$app_version" -v tag="$tag" '
    function fill(line) {
        gsub(/@VERSION@/, version, line)
        gsub(/@APP_VERSION@/, app_version, line)
        gsub(/@TAG@/, tag, line)
        return line
    }
    /^<!-- if (signed|unsigned) -->$/ {
        if (block != "") { print "install-notes: nested block at line " NR > "/dev/stderr"; failed = 1; exit 1 }
        block = $3; next
    }
    /^<!-- end -->$/ {
        if (block == "") { print "install-notes: <!-- end --> without a block at line " NR > "/dev/stderr"; failed = 1; exit 1 }
        block = ""; next
    }
    block != drop { print fill($0) }
    END {
        if (!failed && block != "") { print "install-notes: block \"" block "\" is not closed" > "/dev/stderr"; exit 1 }
    }
' "$template"
