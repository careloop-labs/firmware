#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 CareLoop Labs
#
# Bring-up runner for the CareLoop wearable.
#
#   ./scripts/bringup.sh <name>   build, flash and watch one diagnostic
#   ./scripts/bringup.sh suite    run the ztest bring-up suite via Twister
#
# <name> is any directory under test/bringup that has a CMakeLists.txt. That
# list is derived at run time, never written down here - a hardcoded one is
# exactly what rotted in the predecessor, which offered a `tmp117stress` that
# had been deleted and hid four tests that existed.
#
# Extra arguments after the name go to `west build`, e.g.
#   ./scripts/bringup.sh clocks -- -DCONFIG_CLOCK_CONTROL_NRF_K32SRC_RC=y
#
# Two things are deliberately not commands here, because each is needed rarely
# and folding them in would double this file:
#
#   Open UICR.APPROTECT - once per board. Late nRF52840 revisions read an
#   erased APPROTECT as *protected*, so J-Link mass-erases on every connect and
#   a freshly flashed board looks dead. This is the single biggest time sink on
#   this hardware; the write persists.
#     printf 'connect\nr\nh\nw4 0x10001208, 0xFFFFFF5A\nr\nq\n' |
#       JLinkExe -nogui 1 -device nRF52840_xxAA -if SWD -speed 4000
#
#   List attached probes - needed whenever a second board is plugged in, to
#   find the serial number for PROBE below.
#     printf 'ShowEmuList\nq\n' | JLinkExe -nogui 1
#

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NCS="${NCS:-/opt/nordic/ncs}"
NCS_VERSION="${NCS_VERSION:-v3.4.0}"
TOOLCHAIN="${TOOLCHAIN:-$NCS/toolchains/ccc010f809}"
BOARD="${BOARD:-careloop/nrf52840}"   # resolves to revision 2.0.0 via board.yml
PROBE="${PROBE:-}"                    # J-Link serial; required with two boards

export ZEPHYR_BASE="${ZEPHYR_BASE:-$NCS/$NCS_VERSION/zephyr}"
export ZEPHYR_SDK_INSTALL_DIR="${ZEPHYR_SDK_INSTALL_DIR:-$TOOLCHAIN/opt/zephyr-sdk}"
export ZEPHYR_TOOLCHAIN_VARIANT="${ZEPHYR_TOOLCHAIN_VARIANT:-zephyr}"
export PATH="$TOOLCHAIN/bin:$TOOLCHAIN/opt/zephyr-sdk/gnu/arm-zephyr-eabi/bin:$PATH"

PYTHON="$TOOLCHAIN/opt/python@3.12/bin/python3.12"
[ -x "$PYTHON" ] || PYTHON=python3

# Only one host process may hold the J-Link. A stale one wedges the probe into
# "out of sync" and every later command fails confusingly.
release() { pkill -9 -f -i jlink 2>/dev/null || true; sleep 1; }

names() {
    for d in "$REPO"/test/bringup/*/; do
        [ -f "$d/CMakeLists.txt" ] && basename "$d"
    done
}

case "${1:-}" in
suite)
    shift
    release
    cd "$REPO"

    exec west twister -T test/bringup -p "$BOARD" \
        --board-root "$REPO/boards" \
        --device-testing --hardware-map hardware-map.yml --west-flash \
        -x=BOARD_ROOT="$REPO" -x=DTS_ROOT="$REPO" \
        --outdir "$REPO/build/twister-out" --clobber-output \
        -i --report-summary 0 -v "$@"
    ;;
"" | -h | --help)
    echo "usage: $(basename "$0") <$(names | tr '\n' '|' | sed 's/|$//')|suite> [-- west build args]" >&2
    exit 1
    ;;
*)
    name="$1"
    shift
    dir="$REPO/test/bringup/$name"
    [ -f "$dir/CMakeLists.txt" ] ||
        { echo "no such test: $name (have: $(names | tr '\n' ' '))" >&2; exit 1; }

    release
    west build --no-sysbuild -b "$BOARD" -d "$REPO/build/$name" -p always "$dir" "$@"
    # --dev-id matters as soon as a second J-Link is attached - the normal state
    # when comparing against a DK. Without it the runner picks a probe itself
    # and will happily flash the other board, which then looks like a firmware
    # change that did nothing.
    west flash -d "$REPO/build/$name" --runner jlink ${PROBE:+--dev-id "$PROBE"} ||
        { echo "flash failed - has this board had its APPROTECT opened? see top of this file" >&2; exit 1; }

    echo "--- RTT (ctrl-c to stop) ---"
    release
    exec "$PYTHON" "$REPO/scripts/rtt_console.py" ${PROBE:+--probe "$PROBE"}
    ;;
esac
