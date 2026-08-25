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
import signal
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

    # Bound the control block search to RAM.
    #
    # Without this the search never finds the block and rtt_read() returns
    # nothing forever - silently, with no error: the target fills its 16 KB
    # up-buffer, blocks mid-print because the mode is BLOCK_IF_FIFO_FULL, and
    # Twister waits out its timeout while every test has actually passed.
    # An empty handler.log with a green run inside the buffer is this bug.
    jlink.exec_command(f"SetRTTSearchRanges 0x{RAM_START:X} 0x{RAM_SIZE:X}")

    return jlink


def _write_chunk(jlink: pylink.JLink, addr: int, words: list[int], attempts: int = 4) -> None:
    """Write one chunk, retrying a probe that is not ready yet.

    bringup.sh reaches this within a second of `west flash`, and its release()
    kills the previous J-Link holder with SIGKILL - which does not close the
    USB handle, it only drops the process holding it. Until the kernel reaps
    that handle the probe answers open() and connect() but can still fail
    part way through a long write burst, which pylink reports as the entirely
    unhelpful "Unspecified error".

    Retrying costs nothing on a healthy probe and covers the window. The CPU
    is re-halted first because a write that failed may have left the target
    running, and a running image writes to the RAM being wiped.
    """
    for attempt in range(attempts):
        try:
            jlink.memory_write32(addr, words)
            return
        except Exception:
            if attempt == attempts - 1:
                raise
            try:
                if not jlink.halted():
                    jlink.reset(halt=True)
            except Exception:
                pass
            time.sleep(0.25 * (attempt + 1))


def clear_stale_control_blocks(jlink: pylink.JLink) -> bool:
    """Wipe RAM, then reset, so only the running image's RTT block exists.

    The control block lives in a no-init section and `west flash` does not
    clear RAM, so a block written by a *previous* image survives the flash.
    J-Link's search scans upwards and locks onto the first valid "SEGGER RTT"
    signature it meets - which is the stale one whenever the old image placed
    it at a lower address than the new one.

    That is not a cosmetic problem. It replays the previous run's output as
    though it were current: the bring-up suite showed a clock diagnostic's
    banner, timestamp and all, while Twister reported every testcase blocked.
    Observed with the suite's block at 0x20004010 and a stale one at
    0x20001010.

    Zeroing then resetting is safe precisely because the reset follows: the
    image re-initialises its own block from a known-clean RAM.

    Returns True if the wipe completed. A failure here is reported and
    tolerated rather than fatal: this is hygiene, not correctness. Aborting
    costs the whole run - the console is what carries the test output, and
    Twister has already flashed by this point - while skipping the wipe only
    risks replaying a previous image's output, which is visible in the log
    and recoverable by rerunning. Trading a certain total loss for a possible
    cosmetic one is the wrong way round.
    """
    jlink.reset(halt=True)

    wiped = True
    words = [0] * 1024  # 4 KB per transaction
    for addr in range(RAM_START, RAM_START + RAM_SIZE, 4096):
        try:
            _write_chunk(jlink, addr, words)
        except Exception as exc:
            print(
                f"rtt_console: could not wipe RAM at 0x{addr:08X} ({exc}).\n"
                "rtt_console: continuing without it - if the output below looks like a "
                "previous run (wrong banner, stale timestamps), reset the board and "
                "rerun.",
                file=sys.stderr,
            )
            wiped = False
            break

    # Unconditional, and the reason this is not inside the loop's success
    # path: the target is halted from the reset above, so returning early
    # without this leaves it stopped. The console would then wait out its
    # 30 s control-block deadline and stream nothing, which looks like a dead
    # image rather than a failed wipe.
    jlink.reset(halt=False)

    # Let the image re-create its control block before the search runs.
    # Without this the search can latch onto the freshly zeroed RAM and then
    # stream it as content - 9.7 MB of NUL bytes in one session here.
    time.sleep(1.0)

    return wiped


def _terminate(signum, frame):  # noqa: ARG001 - signal handler signature
    """Turn SIGTERM into the same clean exit path as ctrl-c.

    Python's default SIGTERM action kills the process outright, skipping the
    finally block that calls jlink.close(). The probe is then held until the
    kernel reaps the USB handle, and the next flash fails with a bare
    "Timeout during flashing" that names neither this process nor the reason.
    Raising here reuses the KeyboardInterrupt path, which does release it.
    """
    raise KeyboardInterrupt


def main() -> int:
    signal.signal(signal.SIGTERM, _terminate)

    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--device", default="nRF52840_xxAA")
    ap.add_argument("--probe", default=None, help="J-Link serial number")
    ap.add_argument("--speed", type=int, default=4000)
    ap.add_argument(
        "--no-reset-cb",
        action="store_true",
        help="skip wiping stale RTT control blocks (see clear_stale_control_blocks)",
    )
    args = ap.parse_args()

    try:
        jlink = open_jlink(args.device, args.probe, args.speed)
    except Exception as exc:  # pylink raises a wide range of errors
        print(f"rtt_console: cannot open J-Link: {exc}", file=sys.stderr)
        return 1

    if not args.no_reset_cb:
        # Return value is deliberately not checked: a failed wipe has already
        # explained itself on stderr and is not a reason to abandon the run.
        clear_stale_control_blocks(jlink)

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
                # Strip NULs: the target never emits them, so any that arrive
                # are uninitialised RAM being mistaken for content. Passing
                # them through buries the real log and breaks Twister's
                # line-oriented parsing.
                chunk = bytes(data).replace(b"\x00", b"")
                if chunk:
                    sys.stdout.write(chunk.decode("utf-8", "replace"))
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
