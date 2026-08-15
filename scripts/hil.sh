#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 CareLoop Labs
#
# Hardware-in-the-loop entry point for CareLoop bring-up.
#
#   ./scripts/hil smoke      build, flash and run the bring-up suite
#   ./scripts/hil run NAME   build, flash and watch one standalone diagnostic
#   ./scripts/hil log [WHAT]  show the last run's output (handler|device|build)
#   ./scripts/hil unlock     one-time per board: open UICR.APPROTECT
#   ./scripts/hil console    stream RTT from an already-flashed board
#   ./scripts/hil probes     list attached J-Links
#
# Twister does the real work. This wrapper exists only to absorb three
# things that are specific to this board and easy to get wrong:
#
#   1. The NCS toolchain is not on PATH and ZEPHYR_BASE is unset.
#   2. The board tree lives in this repo, so BOARD_ROOT/DTS_ROOT must be
#      passed explicitly.
#   3. A board whose UICR.APPROTECT has never been written will have its
#      flash mass-erased by J-Link on every connect - see `unlock`.

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NCS="${NCS:-/opt/nordic/ncs}"
NCS_VERSION="${NCS_VERSION:-v3.4.0}"
TOOLCHAIN="${TOOLCHAIN:-$NCS/toolchains/ccc010f809}"
BOARD="${BOARD:-careloop_v2/nrf52840}"
PROBE="${PROBE:-}"
JLINK_DEVICE="${JLINK_DEVICE:-nRF52840_xxAA}"

export ZEPHYR_BASE="${ZEPHYR_BASE:-$NCS/$NCS_VERSION/zephyr}"
export ZEPHYR_SDK_INSTALL_DIR="${ZEPHYR_SDK_INSTALL_DIR:-$TOOLCHAIN/opt/zephyr-sdk}"
export ZEPHYR_TOOLCHAIN_VARIANT="${ZEPHYR_TOOLCHAIN_VARIANT:-zephyr}"
export PATH="$TOOLCHAIN/bin:$TOOLCHAIN/opt/zephyr-sdk/gnu/arm-zephyr-eabi/bin:$PATH"

PYTHON="$TOOLCHAIN/opt/python@3.12/bin/python3.12"
[ -x "$PYTHON" ] || PYTHON=python3

die() { echo "hil: $*" >&2; exit 1; }

# Only one host process may hold the J-Link. A stale one wedges the probe
# into "out of sync" and every later command fails confusingly.
release_probe() {
    pkill -9 -f -i jlink 2>/dev/null || true
    sleep 1
}

cmd_probes() {
    printf 'ShowEmuList\nq\n' | JLinkExe -nogui 1 2>&1 |
        grep -E "Serial number|ProductName" || echo "no J-Link found"
}

# Late nRF52840 revisions treat an erased UICR.APPROTECT as protected, so
# J-Link mass-erases to gain access and the freshly flashed image vanishes.
# Writing 0x5A opens it permanently, so this runs once per board.
cmd_unlock() {
    local script
    script="$(mktemp)"
    release_probe
    cat > "$script" <<EOF
connect
r
h
w4 0x10001208, 0xFFFFFF5A
r
q
EOF
    JLinkExe -nogui 1 -device "$JLINK_DEVICE" -if SWD -speed 4000 \
        -CommanderScript "$script" >/dev/null 2>&1 || true
    rm -f "$script"

    local val
    val="$(printf 'connect\nmem32 0x10001208 1\nq\n' |
        JLinkExe -nogui 1 -device "$JLINK_DEVICE" -if SWD -speed 4000 2>&1 |
        awk '/^10001208/{print $3}')"
    echo "UICR.APPROTECT = 0x${val:-????}"
    case "${val:-}" in
        *5A|*5a) echo "unlocked - west flash will now persist" ;;
        *) die "APPROTECT still 0x${val:-????}; flashing will be erased on connect" ;;
    esac
}

# Show the output of the last `smoke` run.
#
#   handler  what the board printed - every printk and ztest line  (default)
#   device   flashing and runner output
#   build    compiler output
#   twister  Twister's own log
#
# Twister buries these several directories deep under a path that includes
# the absolute source path, so find them rather than spelling it out.
cmd_log() {
    local which="${1:-handler}"
    local out="$REPO/build/twister-out"
    [ -d "$out" ] || die "no run found at $out - run: $(basename "$0") smoke"

    local f
    f="$(find "$out" -name "${which}.log" -print -quit 2>/dev/null)"
    [ -n "$f" ] || die "no ${which}.log under $out (try: handler|device|build|twister)"

    # Strip the ANSI colouring ztest emits, which is noise in a pager or a diff.
    sed -e $'s/\033\\[[0-9;]*m//g' "$f"
}

cmd_console() {
    release_probe
    exec "$PYTHON" "$REPO/scripts/rtt_console.py" \
        --device "$JLINK_DEVICE" ${PROBE:+--probe "$PROBE"}
}

# Build, flash and watch one of the standalone diagnostics under
# test/bringup (boot, i2cscan, tmp117stress). These are plain Zephyr apps
# rather than ztest suites, so Twister ignores them - they exist to answer a
# single question interactively, not to pass or fail.
#
# Flash first, then attach: only one process may hold the J-Link.
cmd_run() {
    local name="${1:-}"
    [ -n "$name" ] || die "usage: hil run <boot|i2cscan|tmp117stress> [-- extra west build args]"
    local dir="$REPO/test/bringup/$name"
    [ -d "$dir" ] || die "no such test: $dir"
    shift

    local out="$REPO/build/$name"
    release_probe
    west build --no-sysbuild -b "$BOARD" -d "$out" -p always "$dir" "$@" || die "build failed"
    west flash -d "$out" --runner jlink || die "flash failed - has this board been unlocked? see: hil unlock"

    echo "--- RTT (ctrl-c to stop) ---"
    release_probe
    exec "$PYTHON" "$REPO/scripts/rtt_console.py" \
        --device "$JLINK_DEVICE" ${PROBE:+--probe "$PROBE"}
}

# All generated output goes under build/, which .gitignore already covers.
# --clobber-output deletes the previous run rather than renaming it to
# twister-out.N; the default renaming policy silently accumulates a ~20 MB
# copy per run.
cmd_smoke() {
    [ -f "$REPO/hardware-map.yml" ] || die "hardware-map.yml not found"
    release_probe
    cd "$REPO"
    # Two different roots, deliberately. Twister's --board-root must *include*
    # the boards/ directory - it internally takes dirname() of what you pass -
    # while CMake's BOARD_ROOT must not. Passing $REPO to both silently finds
    # no platform and reports "unrecognized platform".
    exec west twister \
        -T test/bringup \
        -p "$BOARD" \
        --board-root "$REPO/boards" \
        --device-testing \
        --hardware-map hardware-map.yml \
        --west-flash \
        -x=BOARD_ROOT="$REPO" \
        -x=DTS_ROOT="$REPO" \
        --outdir "$REPO/build/twister-out" \
        --clobber-output \
        -v "$@"
}

case "${1:-smoke}" in
    smoke)   shift || true; cmd_smoke "$@" ;;
    run)     shift; cmd_run "$@" ;;
    log)     shift || true; cmd_log "$@" ;;
    unlock)  cmd_unlock ;;
    console) cmd_console ;;
    probes)  cmd_probes ;;
    *)       die "unknown command '$1' (smoke|run|log|unlock|console|probes)" ;;
esac
