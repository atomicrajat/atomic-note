#!/usr/bin/env bash
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
PORT="$(require_port)"
echo "Flashing to ${PORT}"
arduino-cli compile \
  --fqbn "${FQBN}" \
  --build-path "${BUILD_DIR}" \
  --upload --port "${PORT}" \
  "${SKETCH_DIR}"
