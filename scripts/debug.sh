#!/usr/bin/env bash
set -euo pipefail

JLINK_DIR=${JLINK_DIR:-/opt/SEGGER/JLink_V966}

exec "${JLINK_DIR}/JLinkGDBServerCLExe" \
    -device nRF52840_xxAA \
    -if SWD \
    -speed 4000 \
    -endian little \
    -port 2331 \
    -rtos "${JLINK_DIR}/GDBServer/RTOSPlugin_Zephyr.so" \
    -nogui \
    "$@"
