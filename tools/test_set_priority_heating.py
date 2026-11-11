#!/usr/bin/env python3
"""Hardware test: restore heating priority (write 0x02BF = 0).

Companion to test_set_priority_dhw.py: proves the register is writable both
ways and puts the unit back the way you had it.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from windmi_mode import REG_DHW_PRIORITY, run_reg_test  # noqa: E402

if __name__ == "__main__":
    sys.exit(run_reg_test(REG_DHW_PRIORITY, 0, "heating priority (restore)",
                          watch_set_name="priority", tag="prio0"))
