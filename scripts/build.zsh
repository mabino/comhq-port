#!/usr/bin/env zsh
# Recompile CHQ.EXE (if found) and build the native macOS (Apple Silicon) app.
set -euo pipefail
setopt extendedglob
ROOT=${0:A:h:h}
cd $ROOT
export CHQ_DATA=${CHQ_DATA:-${ROOT:h}}
if [[ -n ${CHQ_DATA}(#qN) ]] && ls $CHQ_DATA | grep -qi '^chq.exe$'; then
  $ROOT/scripts/recomp.zsh
else
  print -u2 "CHQ.EXE not found in $CHQ_DATA: building the app without the game"
fi
mise exec -- cmake --preset macos
mise exec -- cmake --build --preset macos
print "Built: $ROOT/build/macos/engine/Command HQ.app"
