#!/usr/bin/env python3
"""
Experiment C: is 0x02BF latched only when the mode is re-arbitrated?

Sequence, no waiting in between:

  1. write 0x02BF = 1 (DHW priority), verify by echo + read-back
  2. immediately re-write 0x002C = 2 (Heating+DHW) so the unit re-reads its
     settings, verify the same way
  3. tell the operator to look at the display controller
  4. watch priority / running status / DHW valve / tank temperature, printing a
     line every poll so the log can be lined up with what the panel says
  5. snapshot and diff against the last full sweep

Writes only 0x02BF and 0x002C. Refuses to run while windmi-control is active.
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import windmi_mode as wm  # noqa: E402
import windmi_regdump as rd  # noqa: E402

PRIORITY = [(wm.REG_DHW_PRIORITY, wm.PRIORITIES.get), (wm.REG_RUNNING_STATUS, rd.RUNNING_STATUS.get),
            (wm.REG_DHW_VALVE, rd.DHW_VALVE_STATUS.get), (wm.REG_DHW_TANK, rd.temp)]


def watch_heartbeat(gateway, seconds, watch_set, interval=5.0):
    addresses = [address for address, _decoder in watch_set]
    decoders = dict(watch_set)
    history = []
    started = time.time()
    while True:
        values = wm.poll(gateway, addresses)
        print("  %5.0fs  %s" % (time.time() - started, wm.format_values(values, decoders)))
        history.append({"t": round(time.time() - started, 1),
                        **{"0x%04X" % a: v for a, v in values.items()}})
        if time.time() - started >= seconds:
            return history
        time.sleep(interval)


def main(argv=None):
    import argparse

    parser = argparse.ArgumentParser(description="Priority write followed by mode re-write")
    parser.add_argument("--host", default=os.environ.get("WINDMI_HOST", rd.DEFAULT_HOST))
    parser.add_argument("--port", type=int, default=int(os.environ.get("WINDMI_PORT", rd.DEFAULT_PORT)))
    parser.add_argument("--slave", type=int, default=int(os.environ.get("WINDMI_SLAVE", rd.DEFAULT_SLAVE)))
    parser.add_argument("--timeout", type=float, default=1.5)
    parser.add_argument("--watch", type=float, default=120.0)
    parser.add_argument("--out-dir", default="regdumps")
    parser.add_argument("--baseline", help="snapshot to diff against (default: newest full sweep)")
    parser.add_argument("--mode", type=int, default=2, help="mode to re-write after the priority")
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args(argv)

    pid = wm.controller_is_running()
    if pid and not args.force:
        print("ABORT: windmi-control is running (pid %s); stop it first or use --force." % pid)
        return 3

    baseline = args.baseline or wm.default_watch_file()
    targets = wm.watch_targets(baseline) if baseline else rd.build_targets(scan=False)
    gateway = wm.GatewayRW(args.host, args.port, args.slave, timeout=args.timeout, retries=1)
    try:
        gateway.connect()
    except OSError as exc:
        print("ABORT: cannot reach %s:%d -- %s" % (args.host, args.port, exc))
        return 2
    try:
        print("baseline diff set: %d registers (%s)" % (len(targets), baseline or "curated"))
        before_path = wm.take_snapshot(gateway, targets, "latch-before", args.out_dir)

        print("\n-- back-to-back writes --")
        for address, value, label in ((wm.REG_DHW_PRIORITY, 1, "DHW priority"),
                                      (wm.REG_SETTING_MODE, args.mode, "re-write mode")):
            try:
                gateway.write_register(address, value)
            except wm.WriteError as exc:
                print("  %-14s WRITE FAILED %#06x: %s" % (label, address, exc))
                return 1
            held = gateway.read_registers(address, 1)[0]
            if held != value:
                print("  %-14s VERIFY FAILED %#06x holds %d, expected %d" % (label, address, held, value))
                return 1
            print("  %-14s %#06x = %d verified" % (label, address, value))

        print("\n  === GO AND LOOK AT THE DISPLAY CONTROLLER NOW ===\n")
        history = watch_heartbeat(gateway, args.watch, PRIORITY)

        after_path = wm.take_snapshot(gateway, targets, "latch-after", args.out_dir)
        print("\n-- what it changed --")
        changed = rd.do_diff(before_path, after_path)
        key = "0x%04X" % wm.REG_RUNNING_STATUS
        status_values = sorted({entry[key] for entry in history if isinstance(entry[key], int)})
        print("\n== RESULT ==")
        print("  writes           : 0x02BF=1 and 0x002C=%d verified" % args.mode)
        print("  status values    : %s" % ", ".join("%s (%s)" % (v, rd.RUNNING_STATUS.get(v, "?"))
                                                    for v in status_values))
        print("  registers moved  : %d" % changed)
        print("  snapshots        : %s\n                     %s" % (before_path, after_path))
        return 0
    finally:
        gateway.close()


if __name__ == "__main__":
    sys.exit(main())
