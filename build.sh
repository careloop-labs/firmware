#!/usr/bin/env bash
set -euo pipefail

# Simple wrapper around west build so we do not keep stale build trees around.

BOARD=${BOARD:-nrf52840dk_nrf52840}
BUILD_DIR=${BUILD_DIR:-build}

west build -b "$BOARD" -d "$BUILD_DIR" -p auto "$@" .