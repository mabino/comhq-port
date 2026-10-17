#!/usr/bin/env zsh
# Run headless Ghidra analysis of CHQ.EXE in the comhq-analysis container.
# Usage: scripts/analyze.zsh [--decompile] [--rebuild] [--project NAME]
set -euo pipefail
setopt extendedglob

ROOT=${0:A:h:h}
DATA=${CHQ_DATA:-${ROOT:h}}
DATA=${DATA:A}
IMAGE=comhq-analysis
EXTRA=()

while (( $# )); do
  arg=$1; shift
  case $arg in
    --project) EXTRA+=(--project-name "$1"); shift ;;
    --rebuild) docker rmi -f $IMAGE >/dev/null 2>&1 || true ;;
    --decompile) EXTRA+=(--decompile) ;;
    *) print -u2 "unknown option: $arg"; exit 2 ;;
  esac
done

EXE=( $DATA/(#i)chq.exe(N) )
(( ${#EXE} )) || { print -u2 "CHQ.EXE not found in $DATA (set CHQ_DATA)"; exit 1; }

if ! docker image inspect $IMAGE >/dev/null 2>&1; then
  docker build -f $ROOT/docker/analysis.Dockerfile -t $IMAGE $ROOT/docker
fi

mkdir -p $ROOT/out/ghidra $ROOT/ghidra-projects
docker run --rm \
  -v "$DATA:/data:ro" \
  -v "$ROOT/tools:/work/tools:ro" \
  -v "$ROOT/out:/work/out" \
  -v "$ROOT/ghidra-projects:/work/ghidra-projects" \
  $IMAGE python /work/tools/ghidra/analyze.py \
    --binary "/data/${EXE[1]:t}" --out /work/out/ghidra "${EXTRA[@]}"
