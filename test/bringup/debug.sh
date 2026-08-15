#!/usr/bin/env bash
set -euo pipefail

# J-Link GDB server for careloop_v2. Leave this running in its own terminal.
#
# Ports:
#   2331  - GDB remote protocol
#   19021 - RTT terminal: nc localhost 19021
#
# Usage, from a test app directory such as boot/:
#   arm-zephyr-eabi-gdb build/zephyr/zephyr.elf
#   (gdb) target extended-remote localhost:2331
#   (gdb) monitor reset
#   (gdb) load                 # flashes, so ELF and chip cannot drift apart
#   (gdb) monitor reset
#   (gdb) break main
#   (gdb) continue
#
# "detach" leaves the core HALTED, which looks exactly like a dead board.
# Issue "monitor go" first to leave the target running standalone.
#
# The J-Link accepts exactly one host connection at a time. If this fails to
# attach, something else still holds the probe (JLinkExe, JLinkRTTLogger).
# sudo is not needed - the udev rules already allow normal-user access.

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
