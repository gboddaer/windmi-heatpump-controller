#!/usr/bin/env python3
"""Hardware test: put the Windmi into Heating+DHW (write 0x002C = 2) and prove it.

Checks: FC 06 echo, FC 03 read-back, running-status watch, and a before/after
register diff. Then confirm on the display controller that it shows Heating+DHW.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from windmi_mode import run_mode_test  # noqa: E402

if __name__ == "__main__":
    sys.exit(run_mode_test(2, "Heating + DHW"))
