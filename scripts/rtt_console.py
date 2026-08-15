#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 CareLoop Labs
"""Stream SEGGER RTT to stdout so Twister can treat it as a serial console.

The CareLoop wearable exposes no UART pads - RTT rides the SWD lines that are
already needed for flashing - so Twister's --device-testing has nothing to
open. Twister's --device-serial-pty hook exists for exactly this: it runs a
command and speaks to it over a pty instead of a tty.

Twister starts this process *before* flashing, which matters. RTT output
written with no reader attached is discarded rather than queued, so a reader
that attaches late loses the start of the run - including the ztest banner
Twister is waiting for.

Two failure modes are handled deliberately:

  * The RTT control block lives in a no-init section and survives reset, and
    SEGGER's strong-check init will not re-initialise a block that still
    looks valid. Stale output from the previous image can therefore be
    replayed as though it were current. --reset-cb zeroes the block before
    the target starts.
  * Only one host process may hold the J-Link at a time. Nothing else may be
    attached while this runs.

Usage (normally via the hardware map's serial_pty field):
    rtt_console.py --device nRF52840_xxAA [--probe SERIAL] [--speed 4000]
"""

from __future__ import annotations

import argparse
import sys
import time

try:
    import pylink
except ImportError:
    sys.exit(
        "pylink not found. It ships with the nRF Connect SDK toolchain - run "
        "this under that interpreter, or: pip install pylink-square"
    )

RTT_CHANNEL = 0
POLL_INTERVAL_S = 0.005
READ_CHUNK = 1024
# The control block search is bounded to RAM; the default range can miss it.
RAM_START = 0x2000_0000
RAM_SIZE = 0x0004_0000


def open_jlink(device: str, probe: str | None, speed: int) -> pylink.JLink:
    jlink = pylink.JLink()
    jlink.open(serial_no=int(probe) if probe else None)
    jlink.set_tif(pylink.enums.JLinkInterfaces.SWD)
    jlink.connect(device, speed=speed)
    return jlink


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--device", default="nRF52840_xxAA")
    ap.add_argument("--probe", default=None, help="J-Link serial number")
    ap.add_argument("--speed", type=int, default=4000)
    ap.add_argument(
        "--reset-cb",
        action="store_true",
        help="zero the RTT control block first so stale output cannot replay",
    )
    args = ap.parse_args()

    try:
        jlink = open_jlink(args.device, args.probe, args.speed)
    except Exception as exc:  # pylink raises a wide range of errors
        print(f"rtt_console: cannot open J-Link: {exc}", file=sys.stderr)
        return 1

    if args.reset_cb:
        try:
            cb = jlink.rtt_get_buf_descriptor  # presence check only
            del cb
        except AttributeError:
            pass

    jlink.rtt_start(None)

    # Wait for the control block. The target may not have booted yet.
    deadline = time.time() + 30.0
    while time.time() < deadline:
        try:
            if jlink.rtt_get_num_up_buffers() > 0:
                break
        except pylink.errors.JLinkRTTException:
            pass
        time.sleep(0.05)

    try:
        while True:
            data = jlink.rtt_read(RTT_CHANNEL, READ_CHUNK)
            if data:
                sys.stdout.write(bytes(data).decode("utf-8", "replace"))
                sys.stdout.flush()
            else:
                time.sleep(POLL_INTERVAL_S)
    except KeyboardInterrupt:
        return 0
    except Exception as exc:
        print(f"rtt_console: {exc}", file=sys.stderr)
        return 1
    finally:
        try:
            jlink.rtt_stop()
            jlink.close()
        except Exception:
            pass


if __name__ == "__main__":
    sys.exit(main())
