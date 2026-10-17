#!/usr/bin/env zsh
# Notarize the bundled app. Needs a Developer ID identity and a notarytool keychain profile:
#   xcrun notarytool store-credentials chq-notary --apple-id you@example.com --team-id TEAMID
#   CHQ_SIGN_IDENTITY="Developer ID Application: ..." scripts/notarize.zsh
set -euo pipefail
ROOT=${0:A:h:h}
PROFILE=${CHQ_NOTARY_PROFILE:-chq-notary}
[[ -n ${CHQ_SIGN_IDENTITY:-} ]] || { print -u2 "set CHQ_SIGN_IDENTITY to a Developer ID Application identity"; exit 1; }

$ROOT/scripts/bundle.zsh

# Submit the DMG to Apple Notary Service
xcrun notarytool submit "$ROOT/dist/Command HQ.dmg" --keychain-profile "$PROFILE" --wait

# Staple notarization tickets to the app bundle and disk image
xcrun stapler staple "$ROOT/build/macos/engine/Command HQ.app"
xcrun stapler staple "$ROOT/dist/Command HQ.app"
xcrun stapler staple "$ROOT/dist/Command HQ.dmg"

# Update zip with the stapled app
ditto -c -k --keepParent "$ROOT/dist/Command HQ.app" "$ROOT/dist/Command HQ.zip"

print "Notarized and stapled DMG, app bundle, and zip archive."
