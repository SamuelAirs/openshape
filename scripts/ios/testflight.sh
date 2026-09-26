#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Signs an OpenShape archive for the App Store and uploads it to TestFlight
# (internal testers only). Xcode creates the distribution certificate and
# the provisioning profile itself ("cloud signing"), authenticated with an
# App Store Connect API key with the Admin role; the app record must exist
# in App Store Connect (docs/IPAD.md).
#
# Usage (macOS): scripts/ios/testflight.sh <archive> <work-dir>
# Environment: APPLE_TEAM_ID, ASC_KEY_ID, ASC_ISSUER_ID, ASC_KEY_PATH (.p8)
set -euo pipefail

ARCHIVE=${1:?usage: testflight.sh <archive> <work-dir>}
WORK=${2:?usage: testflight.sh <archive> <work-dir>}
: "${APPLE_TEAM_ID:?}" "${ASC_KEY_ID:?}" "${ASC_ISSUER_ID:?}" "${ASC_KEY_PATH:?}"
mkdir -p "$WORK"

cat > "$WORK/ExportOptions.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>method</key>
    <string>app-store-connect</string>
    <key>destination</key>
    <string>upload</string>
    <key>teamID</key>
    <string>$APPLE_TEAM_ID</string>
    <key>signingStyle</key>
    <string>automatic</string>
    <key>testFlightInternalTestingOnly</key>
    <true/>
    <key>manageAppVersionAndBuildNumber</key>
    <false/>
    <key>uploadSymbols</key>
    <true/>
</dict>
</plist>
EOF

xcodebuild -exportArchive -archivePath "$ARCHIVE" \
    -exportOptionsPlist "$WORK/ExportOptions.plist" -exportPath "$WORK/export" \
    -allowProvisioningUpdates -authenticationKeyPath "$ASC_KEY_PATH" \
    -authenticationKeyID "$ASC_KEY_ID" -authenticationKeyIssuerID "$ASC_ISSUER_ID"
