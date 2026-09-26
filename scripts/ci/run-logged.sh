#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Runs one CI step, keeps its output in $LOGS/<name>.log and, if it fails,
# reports the error lines and the end of the output as GitHub annotations.
# GitHub shows job logs only to signed-in users, but the annotations of a
# public repository can be read by anyone:
#   curl https://api.github.com/repos/SamuelAirs/openshape/check-runs/<job id>/annotations
#
# Usage: scripts/ci/run-logged.sh <name> <command> [args...]
set -uo pipefail

name=${1:?usage: run-logged.sh <name> <command> [args...]}
shift
logs=${LOGS:-ci-logs}
mkdir -p "$logs"
log="$logs/$name.log"

"$@" 2>&1 | tee "$log"
status=${PIPESTATUS[0]}
[ "$status" -eq 0 ] && exit 0

# Workflow-command escaping: one line, % and line breaks encoded.
annotate() { # <title> <text>
    local text=$2
    text=${text//'%'/'%25'}
    text=${text//$'\r'/}
    text=${text//$'\n'/'%0A'}
    echo "::error title=$1::$text"
}
clean() { LC_ALL=C sed -e $'s/\e\\[[0-9;]*[A-Za-z]//g' | cut -c1-500; } # no colors, short lines

# The error lines with some context, in chunks (GitHub keeps 10 error
# annotations per step), then the end of the output.
clean < "$log" | grep -n -E -B2 -A3 \
    'error:|Error:|error [A-Z]+[0-9]+|CMake Error|Undefined symbols|ld: |FAILED|Failed|Failure|\[FAIL\]|fatal|No such file|not found' \
    | head -n 490 > "$log.errors" || true
chunks=$(( ($(wc -l < "$log.errors") + 69) / 70 ))
for ((i = 0; i < chunks; i++)); do
    annotate "$name failed: error lines ($((i + 1))/$chunks)" "$(sed -n "$((i * 70 + 1)),$((i * 70 + 70))p" "$log.errors")"
done
annotate "$name failed (exit $status): last lines" "$(clean < "$log" | tail -n 60)"
exit "$status"
