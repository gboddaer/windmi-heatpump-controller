#!/usr/bin/env python3
"""Hardware test: write any device mode to 0x002C and prove the unit took it.

The mode register is the only lever this controller has for demand, and the
vendor manual marks modes 3, 4 and 5 "only in GCHV" without saying whether a
given firmware accepts them. Answer that question for any mode here: FC 06
echo, FC 03 read-back, then a watch of running status, valve and temperatures.

Read the register map first if you are unsure what you are looking at:
tools/windmi_regdump.py. This script writes.

  ./tools/test_set_mode.py --mode 4 --watch 10
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from windmi_mode import run_mode_test  # noqa: E402

DESCRIPTIONS = {
    0: "Off",
    1: "Cool + DHW",
    2: "Heat + DHW",
    3: "Cool only (GCHV single function)",
    4: "Heat only (GCHV single function)",
    5: "DHW only (GCHV single function)",
}


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Write 0x002C = <mode> and verify the unit took it")
    parser.add_argument("--mode", type=int, required=True, choices=sorted(DESCRIPTIONS),
                        help="device mode to write: " +
                             ", ".join("%d=%s" % (m, DESCRIPTIONS[m]) for m in sorted(DESCRIPTIONS)))
    args, driver_args = parser.parse_known_args(argv)

    description = "%s (mode %d)" % (DESCRIPTIONS[args.mode], args.mode)
    # Everything else (--host --port --slave --watch --out-dir --force) belongs to
    # the driver in windmi_mode.py, which owns the Modbus conversation.
    return run_mode_test(args.mode, description, argv=driver_args)


if __name__ == "__main__":
    sys.exit(main())
