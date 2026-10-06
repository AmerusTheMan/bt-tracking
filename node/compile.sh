#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="${0%/*}"

arduino-cli compile -v --fqbn esp32:esp32:waveshare_esp32_c3_zero --build-property build.partitions=no_fs --build-property upload.maximum_size=2031616 --build-path "${SCRIPT_DIR}/build" "${SCRIPT_DIR}"
