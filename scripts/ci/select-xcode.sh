#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Selects the newest released Xcode of a major version on a GitHub-hosted
# Mac; keeps the default when there is none. Usage: select-xcode.sh [major]
# (default 26). App Store Connect refuses betas, so betas and release
# candidates are skipped, also behind the version-named symlinks the runner
# images add (Xcode_27.2.app -> Xcode_27.2_Release_Candidate.app).
# ipad.yml asks for 27: the iOS 27.1 SDK makes the app fill the iPhone Duo's
# inner display (apps built with older SDKs run there at a smaller,
# phone-like size; docs/IPAD.md).
set -euo pipefail

major=${1:-26}
ls -d /Applications/Xcode*.app
xcode=""
for candidate in $(ls -d /Applications/Xcode_"$major"*.app 2>/dev/null | grep -E "Xcode_$major(\.[0-9]+)*\.app$" | sort -V); do
    real=$(cd "$candidate" && pwd -P)
    case "$real" in
        *[Bb]eta*|*Release_Candidate*|*_RC*) continue ;;
    esac
    xcode=$candidate
done
if [ -n "$xcode" ]; then
    sudo xcode-select -s "$xcode"
else
    echo "::warning title=Xcode::no released Xcode $major on this runner; keeping the default"
fi
xcodebuild -version
echo "::notice title=Xcode::$(xcodebuild -version | tr '\n' ' ')($(xcode-select -p)), iOS SDK $(xcrun --sdk iphoneos --show-sdk-version 2>/dev/null || echo none)"
