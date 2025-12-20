#!/usr/bin/env python3
"""PPG serial reader (minimal)

Behavior:
- Open a serial port (auto-detect /dev/ttyACM* or /dev/ttyUSB* if --port not given)
- Read lines, extract a float after 'val=' if present, else the first float on the line
- Print the numeric value to stdout (one per line), flush immediately
"""

import argparse
import re
import sys
import os

try:
    import serial
except Exception:
    print("Missing dependency 'pyserial' (pip install pyserial)")
    raise

VAL_RE = re.compile(r"val=([-+]?[0-9]*\.?[0-9]+)")
FLOAT_RE = re.compile(r"([-+]?[0-9]*\.?[0-9]+)")


def find_port():
    for prefix in ("/dev/ttyACM", "/dev/ttyUSB"):
        for i in range(0, 8):
            p = f"{prefix}{i}"
            if os.path.exists(p):
                return p
    return None


def get_raw_ppg_value(s: str):
    """Extract a raw PPG numeric value from a line of text.

    Returns a float if found, else None.
    """
    m = VAL_RE.search(s)
    if m:
        try:
            return float(m.group(1))
        except Exception:
            return None
    m2 = FLOAT_RE.search(s)
    if m2:
        try:
            return float(m2.group(1))
        except Exception:
            return None
    return None


# Exported API (no main):

# Public API
__all__ = ["find_port", "get_raw_ppg_value"]

# Notes for usage:
# - Import this module and call `find_port()` to auto-detect a serial device.
# - Use `get_raw_ppg_value(line)` to extract a numeric PPG value from a line of text.
# - Opening/reading the serial port is intentionally left to the caller to keep
#   this module minimal and easy to compose into experiments or larger scripts.