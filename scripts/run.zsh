#!/usr/bin/env zsh
# Build (if needed) and run the port against the original CHQ.EXE.
set -euo pipefail
ROOT=${0:A:h:h}
export CHQ_DATA=${CHQ_DATA:-${ROOT:h}}
APP="$ROOT/build/macos/engine/Command HQ.app"
[[ -d $APP ]] || $ROOT/scripts/build.zsh
exec "$APP/Contents/MacOS/Command HQ" "$@"
