#!/usr/bin/env zsh
# Segment map, overlay/sound drivers, C strings, and FONTS.CHQ into out/.
set -euo pipefail
ROOT=${0:A:h:h}
export CHQ_DATA=${CHQ_DATA:-${ROOT:h}}
for cmd in inventory strings font; do
  out=$ROOT/out/$cmd.json
  [[ $cmd == font ]] && out=$ROOT/out/fonts-chq.json
  mise exec -- uv --directory $ROOT/tools/chqre run chqre $cmd --out $out
done
