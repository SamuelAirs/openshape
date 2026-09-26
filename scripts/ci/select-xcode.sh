#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Selects the newest released Xcode 26 on a GitHub-hosted Mac (App Store
# Connect requires apps built with the iOS 26 SDK and refuses betas); keeps
# the default otherwise.
set -euo pipefail

ls -d /Applications/Xcode*.app
xcode=$(ls -d /Applications/Xcode_26*.app 2>/dev/null | grep -E 'Xcode_26(\.[0-9]+)*\.app$' | sort -V | tail -n 1 || true)
if [ -n "$xcode" ]; then
    sudo xcode-select -s "$xcode"
fi
xcodebuild -version
echo "::notice title=Xcode::$(xcodebuild -version | tr '\n' ' ')($(xcode-select -p))"
