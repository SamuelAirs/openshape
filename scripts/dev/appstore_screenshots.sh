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
#
# Interrupting it is safe. The pictures are rendered into
# build/appstore-screenshots/ and copied into docs/appstore/screenshots/
# only after all of them were made and pass the check, so the folder never
# holds a half-written or half-updated set. Ctrl+C, closing the terminal or
# killing the script (SIGINT, SIGTERM, SIGHUP) closes the window of the
# shot in progress and starts no further one. Only one copy of the script
# runs at a time. Should the script itself be killed without a chance to
# clean up (TerminateProcess, `kill -9`), the shot in progress still ends
# by itself: it saves its one picture into build/ and exits (at the latest
# after 3 minutes once it has the lock, --store-screenshot's watchdog
# thread, which also ends a scene that hangs).
set -euo pipefail

exe=${OPENSHAPE_EXE:-build/msys2-ucrt64/bin/OpenShape.exe}
# The MSYS2 build finds its DLLs there (BUILDING.md).
if [ -d "$HOME/msys64/ucrt64/bin" ]; then
    export PATH="$HOME/msys64/ucrt64/bin:$PATH"
fi
python=${PYTHON:-python}
out=${OUT:-docs/appstore/screenshots}
work=build/appstore-screenshots
rendered="$work/rendered"
mkdir -p "$out" "$work"

# One copy at a time: a second one would launch windows alongside the first.
guard="$work/.running"
if ! mkdir "$guard" 2> /dev/null; then
    other=$(cat "$guard/pid" 2> /dev/null || true)
    if [ -n "$other" ] && kill -0 "$other" 2> /dev/null; then
        echo "appstore_screenshots.sh is already running (pid $other); wait for it or stop it first." >&2
        exit 1
    fi
    rm -rf "$guard" # left by a copy that was killed
    mkdir "$guard"
fi
echo $$ > "$guard/pid"

child=""
stop_child() {
    if [ -n "$child" ] && kill -0 "$child" 2> /dev/null; then
        kill "$child" 2> /dev/null || true
        wait "$child" 2> /dev/null || true
    fi
    child=""
}
cleanup() {
    stop_child
    rm -rf "$guard"
}
interrupted() {
    trap - INT TERM HUP
    echo "interrupted: no further shots; $out is unchanged" >&2
    cleanup
    exit 130
}
trap cleanup EXIT
trap interrupted INT TERM HUP

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

rm -rf "$rendered"
mkdir -p "$rendered"
made=()
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
        # In the background, so a signal reaches the trap at once (bash runs
        # traps only between commands) and the trap can close the window.
        # timeout: the app waits up to 15 minutes for the automation lock.
        timeout --kill-after=10 1200 "$exe" --demo "$scene" --touch --size "$size" --dpr "$dpr" --safe-area "$safe" \
            --store-screenshot --app-folder "$run/OpenShape" --data-dir "$run/data" --screenshot "$rendered/$file.png" \
            > "$run/log.txt" 2>&1 &
        child=$!
        status=0
        wait "$child" || status=$?
        child=""
        if [ "$status" -ne 0 ] || [ ! -s "$rendered/$file.png" ]; then
            tail -20 "$run/log.txt"
            echo "FAILED: $file (exit status $status)"
            exit 1
        fi
        # A scene that could not do what it shows says so in the log.
        if grep -q "store scene:" "$run/log.txt"; then
            grep "store scene:" "$run/log.txt"
            echo "FAILED: $file (the scene went wrong; see $run/log.txt)"
            exit 1
        fi
        made+=("$file")
    done
done
if [ ${#made[@]} -eq 0 ]; then
    echo "no screenshot names matched: $*" >&2
    exit 1
fi

# Check the set as it will be (the new pictures with the ones kept), then
# put the new ones in place.
staged="$work/staged"
rm -rf "$staged"
mkdir -p "$staged"
cp "$out"/*.png "$staged"/ 2> /dev/null || true
cp "$rendered"/*.png "$staged"/
"$python" scripts/dev/check_appstore_screenshots.py "$staged"
for file in "${made[@]}"; do
    cp "$rendered/$file.png" "$out/$file.png.tmp"
    mv -f "$out/$file.png.tmp" "$out/$file.png"
done
echo "${#made[@]} screenshot(s) updated in $out"
