#!/usr/bin/env python3
"""Hardware test: give domestic hot water priority (write 0x02BF = 1).

Mode stays Heating+DHW (0x002C = 2); only the priority changes. The vendor
manual lists 02BFH as RW 0..1 "DHW Priority". Verified with FC 06 echo, FC 03
read-back, a watch on priority/running status/DHW valve/tank temperature, and a
before/after register diff. docs/dhw_priority_report.md claims writes are
rejected on this firmware; this script settles it either way.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from windmi_mode import REG_DHW_PRIORITY, run_reg_test  # noqa: E402

if __name__ == "__main__":
    sys.exit(run_reg_test(REG_DHW_PRIORITY, 1, "DHW priority (hot water first)",
                          watch_set_name="priority", tag="prio1"))
