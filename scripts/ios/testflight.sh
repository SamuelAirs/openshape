#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Signs an OpenShape archive for the App Store and uploads it to TestFlight
# with its dSYM (testers' crash reports symbolicate). Builds may go to
# external testers as well (the public beta, decided 2026-09-27; docs/IPAD.md
# says what to set up in App Store Connect) and to App Store review; only
# builds of release tags are submitted for review (docs/LICENSING.md, "The
# iOS app and the App Store"). TESTFLIGHT_INTERNAL_ONLY=true keeps a build
# to internal testers. Xcode creates the distribution certificate and the
# provisioning profile itself ("cloud signing"), authenticated with an App
# Store Connect API key with the Admin role; the app record must exist in
# App Store Connect (docs/IPAD.md).
#
# Usage (macOS): scripts/ios/testflight.sh <archive> <work-dir>
# Environment: APPLE_TEAM_ID, ASC_KEY_ID, ASC_ISSUER_ID, ASC_KEY_PATH (.p8),
# TESTFLIGHT_INTERNAL_ONLY (true or false; default false)
set -euo pipefail

ARCHIVE=${1:?usage: testflight.sh <archive> <work-dir>}
WORK=${2:?usage: testflight.sh <archive> <work-dir>}
: "${APPLE_TEAM_ID:?}" "${ASC_KEY_ID:?}" "${ASC_ISSUER_ID:?}" "${ASC_KEY_PATH:?}"
INTERNAL_ONLY=${TESTFLIGHT_INTERNAL_ONLY:-false}
case "$INTERNAL_ONLY" in
    true|false) ;;
    *) echo "error: TESTFLIGHT_INTERNAL_ONLY must be true or false, not '$INTERNAL_ONLY'"; exit 1 ;;
esac
mkdir -p "$WORK"
# uploadSymbols (below) sends the archive's dSYMs; scripts/ios/build-app.sh checks them.
if [ -z "$(ls -A "$ARCHIVE/dSYMs" 2>/dev/null)" ]; then
    echo "error: $ARCHIVE has no dSYMs: crash reports from testers would not symbolicate"
    exit 1
fi

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
    <$INTERNAL_ONLY/>
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
