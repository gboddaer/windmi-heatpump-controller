#!/usr/bin/env python3
"""Hardware test: put the Windmi into DHW only (write 0x002C = 5) and prove it.

This is the mode the display controller sets natively ("only in GCHV"); our
firmware-level equivalent is unverified until this test passes. Checks: FC 06
echo, FC 03 read-back, running-status watch, before/after register diff.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from windmi_mode import run_mode_test  # noqa: E402

if __name__ == "__main__":
    sys.exit(run_mode_test(5, "DHW only (GCHV mode 5)"))
