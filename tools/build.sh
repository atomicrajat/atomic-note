#!/usr/bin/env bash
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
echo "Building ${SKETCH_DIR}"
arduino-cli compile \
  --fqbn "${FQBN}" \
  --build-path "${BUILD_DIR}" \
  --warnings default \
  "$@" \
  "${SKETCH_DIR}"
