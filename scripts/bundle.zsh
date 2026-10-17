#!/usr/bin/env zsh
# Package the app into dist/ as a signed (ad-hoc unless CHQ_SIGN_IDENTITY is set)
# application bundle, zip archive, and drag-and-drop disk image (DMG).
set -euo pipefail
ROOT=${0:A:h:h}

$ROOT/scripts/build.zsh

APP="$ROOT/build/macos/engine/Command HQ.app"
ENTITLEMENTS="$ROOT/engine/resources/comhq.entitlements"
IDENTITY=${CHQ_SIGN_IDENTITY:--}

DATA_SRC=""
if [[ -n "${CHQ_DATA:-}" && -f "$CHQ_DATA/CHQ.EXE" ]]; then
  DATA_SRC="$CHQ_DATA"
elif [[ -f "$ROOT/../CHQ.EXE" ]]; then
  DATA_SRC="$ROOT/.."
fi

if [[ -n "$DATA_SRC" ]]; then
  rm -rf "$APP/Contents/Resources/data"
  mkdir -p "$APP/Contents/Resources/data"
  for f in "$DATA_SRC"/*; do
    if [[ -f "$f" ]]; then
      base=${f:t}
      if [[ "$base" != *copy* && "$base" != *working* && "$base" != *test* && "$base" != .* ]]; then
        cp -p "$f" "$APP/Contents/Resources/data/"
      fi
    fi
  done
  chmod -R u+w "$APP/Contents/Resources/data"
fi

SIGN_ARGS=(--force --deep --options runtime)
if [[ -f "$ENTITLEMENTS" ]]; then
  SIGN_ARGS+=(--entitlements "$ENTITLEMENTS")
fi

if [[ "$IDENTITY" == "-" ]]; then
  SIGN_ARGS+=(--timestamp=none)
else
  SIGN_ARGS+=(--timestamp)
fi

xattr -cr "$APP"
codesign "${SIGN_ARGS[@]}" --sign "$IDENTITY" "$APP"

mkdir -p "$ROOT/dist"
rm -rf "$ROOT/dist/Command HQ.app" "$ROOT/dist/Command HQ.zip" "$ROOT/dist/Command HQ.dmg"

cp -a "$APP" "$ROOT/dist/Command HQ.app"
ditto -c -k --keepParent "$APP" "$ROOT/dist/Command HQ.zip"

# Create drag-and-drop disk image (DMG)
DMG_STAGING=$(mktemp -d "${TMPDIR:-/tmp}/comhq-dmg.XXXXXX")
trap 'rm -rf "$DMG_STAGING"' EXIT INT TERM

cp -a "$APP" "$DMG_STAGING/Command HQ.app"
ln -s /Applications "$DMG_STAGING/Applications"

if [[ -f "$APP/Contents/Resources/AppIcon.icns" ]]; then
  cp "$APP/Contents/Resources/AppIcon.icns" "$DMG_STAGING/.VolumeIcon.icns"
  if command -v SetFile &>/dev/null; then
    SetFile -a C "$DMG_STAGING"
  fi
fi

hdiutil create -volname "Command HQ" \
  -srcfolder "$DMG_STAGING" \
  -ov -format UDZO \
  "$ROOT/dist/Command HQ.dmg" >/dev/null

if [[ "$IDENTITY" != "-" ]]; then
  codesign --force --sign "$IDENTITY" --timestamp "$ROOT/dist/Command HQ.dmg"
fi

print "Packaged: $ROOT/dist/Command HQ.app, $ROOT/dist/Command HQ.zip, and $ROOT/dist/Command HQ.dmg (signed with: $IDENTITY)"
