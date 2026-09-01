#!/usr/bin/env bash
# Shared settings for the build/flash/monitor scripts.
# Override any of these from the environment, e.g. PORT=/dev/cu.usbmodem2101 ./tools/flash.sh

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SKETCH_DIR="${REPO_ROOT}/firmware/atomic_note"
BUILD_DIR="${REPO_ROOT}/.build"

# ESP32-S3 with 8MB flash and OPI PSRAM.
#   PSRAM is required — the framebuffer and audio buffers live there.
#   CDCOnBoot=cdc gives us Serial over the native USB port (no UART bridge).
FQBN="esp32:esp32:esp32s3:\
PSRAM=opi,\
FlashSize=8M,\
PartitionScheme=default_8MB,\
CDCOnBoot=cdc,\
USBMode=hwcdc,\
CPUFreq=240,\
DebugLevel=none"

BAUD="${BAUD:-115200}"

# Resolve the board's serial port. Only flash/monitor need this — compiling
# does not, so this is called on demand rather than at source time.
require_port() {
  if [[ -n "${PORT:-}" ]]; then
    echo "${PORT}"
    return
  fi
  local found
  found="$(ls /dev/cu.usbmodem* 2>/dev/null | head -1 || true)"
  if [[ -z "${found}" ]]; then
    echo "No /dev/cu.usbmodem* found." >&2
    echo "  - If the device is asleep, press either button to wake it." >&2
    echo "  - Otherwise force download mode: hold BOOT, tap RESET, release BOOT." >&2
    exit 1
  fi
  echo "${found}"
}
