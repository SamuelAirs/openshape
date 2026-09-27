#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Regenerates the screenshots in docs/images/ (README.md and
# docs/USER_GUIDE.md) from the app's demo scenes, then makes them smaller
# without changing a pixel (scripts/dev/shrink_png.py).
#
#   bash scripts/dev/doc_screenshots.sh [only-these-names...]
#
# Run from the repository root after building (BUILDING.md). Each shot opens
# a window (taking OpenShape's automation lock, so it waits for other
# automated runs); keep the mouse pointer away from where the window
# opens, or a tooltip under it ends up in the picture. Look at every image
# afterwards.
set -euo pipefail

exe=${OPENSHAPE_EXE:-build/msys2-ucrt64/bin/OpenShape.exe}
python=${PYTHON:-python}
out=docs/images
work=build/doc-screenshots
mkdir -p "$out" "$work"

# name | demo scene | extra arguments
shots=(
    "hero|enclosure|"
    "tablet|enclosure|--touch --size 1180x820"
    "phone|fillet|--touch --size 402x874"
    "pushpull|pushpull|"
    "fillet|fillet|"
    "sketch|constraints|"
    "combine|combine|"
    "pattern|pattern|"
    "history|history|"
)

for entry in "${shots[@]}"; do
    IFS='|' read -r name scene extra <<< "$entry"
    if [ $# -gt 0 ] && [[ " $* " != *" $name "* ]]; then
        continue
    fi
    read -r -a args <<< "$extra"
    "$exe" --demo "$scene" "${args[@]}" --screenshot "$work/$name.png"
    "$python" scripts/dev/shrink_png.py "$work/$name.png" "$out/$name.png"
done
