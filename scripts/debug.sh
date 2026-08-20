#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 CareLoop Labs
#
# J-Link GDB server for the CareLoop wearable, on port 2331.
#
#   ./scripts/debug.sh                  start the server
#   ./scripts/debug.sh -select USB=<sn> pick a probe when two are attached
#
# Connect to it from a debugger with:
#   target remote localhost:2331
#
# Nothing here is version-pinned on purpose. This used to hardcode
# /opt/SEGGER/JLink_V966 - a Linux path, and a version that is not the one
# installed - so it failed outright on macOS and would have rotted on Linux at
# the next J-Link update. The binary is found on PATH instead, and everything
# else is derived from where that turns out to live.

set -euo pipefail

# /usr/local/bin/JLinkGDBServerCL is a symlink SEGGER's installer repoints on
# every upgrade, which is why it is preferred over any versioned directory.
JLINK_GDB="${JLINK_GDB:-$(command -v JLinkGDBServerCL || true)}"
[ -n "$JLINK_GDB" ] || JLINK_GDB=/usr/local/bin/JLinkGDBServerCL

if [ ! -x "$JLINK_GDB" ]; then
    echo "debug.sh: no J-Link GDB server found." >&2
    echo "  Looked for JLinkGDBServerCL on PATH and at /usr/local/bin." >&2
    echo "  Set JLINK_GDB=/path/to/JLinkGDBServerCLExe to override." >&2
    exit 1
fi

# Resolve the symlink chain to locate the install, which is where the RTOS
# plugins sit. macOS readlink has no -f, hence the loop rather than one call.
resolved="$JLINK_GDB"
while [ -L "$resolved" ]; do
    link="$(readlink "$resolved")"
    case "$link" in
        /*) resolved="$link" ;;
        *)  resolved="$(dirname "$resolved")/$link" ;;
    esac
done
JLINK_DIR="$(cd "$(dirname "$resolved")" && pwd)"

# Zephyr thread awareness: the plugin makes the debugger list Zephyr threads
# instead of one anonymous stack. .dylib on macOS, .so on Linux - and note that
# -rtos with a path that does not exist makes the server abort, so a missing
# plugin has to mean "start without it", not "pass it anyway".
RTOS_PLUGIN=""
for candidate in "$JLINK_DIR/GDBServer/RTOSPlugin_Zephyr.dylib" \
                 "$JLINK_DIR/GDBServer/RTOSPlugin_Zephyr.so"; do
    if [ -f "$candidate" ]; then
        RTOS_PLUGIN="$candidate"
        break
    fi
done

if [ -z "$RTOS_PLUGIN" ]; then
    echo "debug.sh: no Zephyr RTOS plugin under $JLINK_DIR/GDBServer -" >&2
    echo "  starting without thread awareness." >&2
fi

# Only one host process may hold the J-Link. A stale holder wedges the probe
# into "out of sync" and every later command fails confusingly. Note that
# rtt_console.py has to be matched by name: its command line contains no
# "jlink", so the usual pkill pattern misses it entirely.
pkill -9 -f rtt_console.py 2>/dev/null || true
pkill -9 -f -i jlink 2>/dev/null || true
sleep 1

echo "debug.sh: $JLINK_GDB"
echo "debug.sh: listening on localhost:2331 - connect with 'target remote localhost:2331'"

exec "$JLINK_GDB" \
    -device nRF52840_xxAA \
    -if SWD \
    -speed 4000 \
    -endian little \
    -port 2331 \
    ${RTOS_PLUGIN:+-rtos "$RTOS_PLUGIN"} \
    -nogui \
    "$@"
