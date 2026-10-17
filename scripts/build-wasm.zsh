#!/usr/bin/env zsh
# Build the WebAssembly port in the Emscripten container and run the unit tests under node.
# Output: build/wasm/engine/comhq.{html,js,wasm}. Serve with: scripts/build-wasm.zsh --serve
set -euo pipefail
ROOT=${0:A:h:h}
IMAGE=comhq-wasm
TAG=4.0.15
[[ $(uname -m) == arm64 ]] && TAG=4.0.15-arm64
if ! docker image inspect $IMAGE >/dev/null 2>&1; then
  docker build -q --build-arg EMSDK_TAG=$TAG -f $ROOT/docker/wasm.Dockerfile -t $IMAGE $ROOT/docker >/dev/null
fi
DATA_ARGS=()
PRE_BUILD=""
if [[ -n "${CHQ_DATA:-}" && -d "${CHQ_DATA}" ]]; then
  DATA_DIR=$(cd "$CHQ_DATA" && pwd)
  DATA_ARGS=(-v "$DATA_DIR:/raw_data:ro" -e CHQ_DATA=/tmp/chq_data)
  PRE_BUILD="mkdir -p /tmp/chq_data && find /raw_data -maxdepth 1 -type f -exec cp {} /tmp/chq_data/ \;"
fi
docker run --rm -v "$ROOT:/src" "${DATA_ARGS[@]}" -w /src -u "$(id -u):$(id -g)" -e HOME=/tmp $IMAGE sh -c '
  set -e
  '"$PRE_BUILD"'
  cmake --preset wasm
  cmake --build --preset wasm -- -j 2
  node build/wasm/engine/chq_tests.js'
print "Built: $ROOT/build/wasm/engine/comhq.html"
if [[ ${1:-} == --serve ]]; then
  exec python3 -m http.server -d $ROOT/build/wasm/engine 8080
fi
