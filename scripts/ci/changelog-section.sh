#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Prints the body of one version's section of CHANGELOG.md (the lines after
# its "## <version> ..." heading, up to the next "## " heading), followed by
# an empty line; nothing if the version has no section. Used for the GitHub
# release notes (.github/workflows/release.yml).
#
# Usage: scripts/ci/changelog-section.sh <version> [changelog]
set -euo pipefail

version=${1:?usage: changelog-section.sh <version> [changelog]}
changelog=${2:-CHANGELOG.md}

awk -v version="$version" '
    /^## / { inside = ($2 == version); if (inside) found = 1; next }
    inside { print }
    END { if (found) print "" }
' "$changelog"
