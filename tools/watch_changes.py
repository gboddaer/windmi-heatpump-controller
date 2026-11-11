#!/usr/bin/env python3
"""
Experiment A: find a register by changing something on the display controller.

Strictly READ-ONLY: function code 0x03 only. It writes nothing, anywhere.

The display controller writes to the mainboard when you change a setting, so the
reliable way to find the register behind a control is to watch while you operate
the panel and see which address moves on its own. This is how mode 5 in 0x002C
was identified.

Fast loop on the curated registers so changes appear within seconds; a full pass
over the address set the unit answers (from the last sweep) at the end, so slow
or one-shot effects are caught too.

  python3 tools/watch_changes.py --minutes 5     # then go operate the panel
"""

import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import windmi_mode as wm  # noqa: E402  (read-only helpers; this script never writes)
import windmi_regdump as rd  # noqa: E402


def main(argv=None):
    parser = argparse.ArgumentParser(description="Watch registers for changes while you use the panel")
    parser.add_argument("--host", default=os.environ.get("WINDMI_HOST", rd.DEFAULT_HOST))
    parser.add_argument("--port", type=int, default=int(os.environ.get("WINDMI_PORT", rd.DEFAULT_PORT)))
    parser.add_argument("--slave", type=int, default=int(os.environ.get("WINDMI_SLAVE", rd.DEFAULT_SLAVE)))
    parser.add_argument("--timeout", type=float, default=1.5)
    parser.add_argument("--minutes", type=float, default=5.0)
    parser.add_argument("--interval", type=float, default=3.0)
    parser.add_argument("--out-dir", default="regdumps")
    parser.add_argument("--baseline", help="snapshot for the final full diff (default: newest sweep)")
    parser.add_argument("--tag", default="panelwatch")
    args = parser.parse_args(argv)

    fast = rd.build_targets(scan=False)
    curated = {address: (label, decoder) for address, label, decoder in fast}
    gateway = rd.Gateway(args.host, args.port, args.slave, timeout=args.timeout, retries=1)
    try:
        gateway.connect()
    except OSError as exc:
        print("ABORT: cannot reach %s:%d -- %s" % (args.host, args.port, exc))
        return 2

    print("watching %d registers for %.1f min (read-only, FC 03 only). Changes print immediately."
          % (len(fast), args.minutes))
    print("start %s" % time.strftime("%Y-%m-%d %H:%M:%S"))
    previous = {}
    events = []
    started = time.time()
    try:
        while time.time() - started < args.minutes * 60:
            values = {}
            for address, _label, _decoder in fast:
                try:
                    values[address] = gateway.read_registers(address, 1)[0]
                except rd.ReadError:
                    pass
            elapsed = time.time() - started
            for address in sorted(values):
                if address in previous and values[address] != previous[address]:
                    label, decoder = curated[address]
                    text = " (%s -> %s)" % (decoder(previous[address]), decoder(values[address])) \
                        if isinstance(previous[address], int) and isinstance(values[address], int) else ""
                    line = "  %5.0fs  %#06x %-45s %s -> %s%s" % (
                        elapsed, address, label, previous[address], values[address], text)
                    print(line, flush=True)
                    events.append(line)
                previous[address] = values[address]
            time.sleep(args.interval)
    except KeyboardInterrupt:
        print("\nstopped early")

    print("\n-- full pass over the known address set --")
    full = wm.watch_targets(args.baseline or wm.default_watch_file()) if (args.baseline or wm.default_watch_file()) else fast
    before_path = None
    if args.baseline or wm.default_watch_file():
        before_path = args.baseline or wm.default_watch_file()
    after_path = wm.take_snapshot(gateway, full, args.tag + "-end", args.out_dir)
    print("\n-- did anything move relative to the baseline --")
    if before_path and os.path.exists(before_path):
        rd.do_diff(before_path, after_path)
    print("\n== SUMMARY ==")
    print("  changes seen : %d line(s) during %.1f min" % (len(events), (time.time() - started) / 60))
    print("  end snapshot : %s" % after_path)
    gateway.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
