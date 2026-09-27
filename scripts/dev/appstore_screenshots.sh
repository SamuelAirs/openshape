#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Makes the App Store screenshots (docs/APP_STORE.md) from the real app:
# six scenes (src/app/StoreScenes.cpp: a project box, text on a key tag, a
# sketch, a construction plane, the Model panel, Home) for
#
#   iPhone 6.9" (iPhone 16/17/18 Pro Max class): 440 x 956 points at 3x = 1320 x 2868
#       pixels, portrait, safe areas 62 (Dynamic Island) and 34 (home indicator);
#   iPad 13" (iPad Pro 13-inch M4 class): 1376 x 1032 points at 2x = 2752 x 2064
#       pixels, landscape, safe areas 24 (status bar) and 20 (home indicator),
#
# in the touch layout, projects saved into an app folder as on the devices.
# Then checks every file (scripts/dev/check_appstore_screenshots.py): the
# exact pixel size Apple accepts, no alpha channel, not blank.
#
#   bash scripts/dev/appstore_screenshots.sh [only-these-names...]
#
# e.g. `... iphone-1-project-box`. Run from the repository root after
# building (BUILDING.md). Each shot opens a frameless window as large as the
# picture (larger than most screens; --store-screenshot) and takes
# OpenShape's automation lock, so shots wait for other automated runs and
# run one after another (about 10 s each, Home about 20 s). The mouse
# pointer is moved off the window. Look at every image afterwards.
set -euo pipefail

exe=${OPENSHAPE_EXE:-build/msys2-ucrt64/bin/OpenShape.exe}
# The MSYS2 build finds its DLLs there (BUILDING.md).
if [ -d "$HOME/msys64/ucrt64/bin" ]; then
    export PATH="$HOME/msys64/ucrt64/bin:$PATH"
fi
python=${PYTHON:-python}
out=${OUT:-docs/appstore/screenshots}
work=build/appstore-screenshots
mkdir -p "$out" "$work"

# device | window size (points) | pixels per point | safe area top,right,bottom,left
devices=(
    "iphone|440x956|3|62,0,34,0"
    "ipad|1376x1032|2|24,0,20,0"
)
# file name | scene (App Store order: the first ones show in search results)
shots=(
    "1-project-box|store-enclosure"
    "2-key-tag|store-text"
    "3-sketch|store-sketch"
    "4-construction-plane|store-planes"
    "5-model-panel|store-history"
    "6-home|store-home"
)

for device_entry in "${devices[@]}"; do
    IFS='|' read -r device size dpr safe <<< "$device_entry"
    for shot_entry in "${shots[@]}"; do
        IFS='|' read -r name scene <<< "$shot_entry"
        file="$device-$name"
        if [ $# -gt 0 ] && [[ " $* " != *" $file "* ]]; then
            continue
        fi
        run="$work/$file"
        rm -rf "$run"
        mkdir -p "$run"
        echo "== $file ($scene, $size at ${dpr}x)"
        "$exe" --demo "$scene" --touch --size "$size" --dpr "$dpr" --safe-area "$safe" --store-screenshot \
            --app-folder "$run/OpenShape" --data-dir "$run/data" --screenshot "$out/$file.png" \
            > "$run/log.txt" 2>&1 || { tail -20 "$run/log.txt"; echo "FAILED: $file"; exit 1; }
        # A scene that could not do what it shows says so in the log.
        if grep -q "store scene:" "$run/log.txt"; then
            grep "store scene:" "$run/log.txt"
            echo "FAILED: $file (the scene went wrong; see $run/log.txt)"
            exit 1
        fi
    done
done

"$python" scripts/dev/check_appstore_screenshots.py "$out"
