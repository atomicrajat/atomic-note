#!/usr/bin/env bash
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
PORT="$(require_port)"
echo "Monitoring ${PORT} at ${BAUD} (ctrl-c to exit)"
arduino-cli monitor --port "${PORT}" --config "baudrate=${BAUD}"
