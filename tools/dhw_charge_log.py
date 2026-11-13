#!/usr/bin/env python3
"""Log the registers that decide whether the tank is actually being charged.

The controller's demand logic asks "is the tank below target?". That is not the
same question as "is the unit putting heat into the tank?", and the difference
only shows up over minutes: the unit can sit in DHW status with the compressor
at 0 Hz forever while the app insists the tank is demanding.

This is read-only. It only ever sends function code 0x03, through the same
Gateway class as windmi_regdump.py, and asserts on every request.

  ./tools/dhw_charge_log.py --minutes 10 --interval 20
  ./tools/dhw_charge_log.py --minutes 3 --interval 10 --out /var/tmp/dhw.csv
"""
import argparse
import csv
import datetime
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import windmi_regdump as rd  # noqa: E402

# What the unit is doing, what it is doing it to, and whether any of it is moving heat.
COLUMNS = [
    (0x0001, "outdoor_c", "temp"),
    (0x0003, "ewt_c", "temp"),
    (0x0004, "lwt_c", "temp"),
    (0x000A, "coil_c", "temp"),
    (0x000B, "tank_t5_c", "temp"),
    (0x0017, "hz", "raw"),
    (0x002C, "mode", "raw"),
    (0x002D, "status", "raw"),
    (0x0033, "control_point_c", "temp"),
    (0x0055, "pump_speed", "raw"),
    (0x00C9, "dhw_mode", "raw"),
    (0x00CE, "tank_c", "temp"),
    (0x00D2, "valve", "raw"),
    (0x0174, "comp_h", "raw"),
    (0x0176, "pump_h", "raw"),
    (0x0194, "dhw_set_c", "temp"),
    (0x0195, "legionella_set_c", "temp"),
    (0x0196, "dhw_set_eco_c", "temp"),
    (0x101F, "odu_bits", "raw"),
    (0x102A, "flow", "raw"),
]


def poll(gateway, addrs, block, names):
    targets = [(a, names[a], rd.raw_value) for a in addrs]
    readings = rd.collect(gateway, targets, block=block, quiet=True)
    # A reading of 0 is a real reading (compressor stopped, pump off); only None is missing.
    return {a: (readings.get(a) or {}).get("raw") for a in addrs}


def decode_for(kind, raw):
    if raw is None:
        return ""
    if kind == "temp":
        return round(rd.temp(raw), 1)
    return raw


def main():
    parser = argparse.ArgumentParser(description="Read-only DHW charge logger")
    parser.add_argument("--host", default=os.environ.get("WINDMI_HOST", rd.DEFAULT_HOST))
    parser.add_argument("--port", type=int, default=int(os.environ.get("WINDMI_PORT", rd.DEFAULT_PORT)))
    parser.add_argument("--slave", type=int, default=int(os.environ.get("WINDMI_SLAVE", rd.DEFAULT_SLAVE)))
    parser.add_argument("--minutes", type=float, default=10.0)
    parser.add_argument("--interval", type=float, default=20.0)
    parser.add_argument("--block", type=int, default=8)
    parser.add_argument("--out", help="append CSV here as well as printing")
    args = parser.parse_args()

    addrs = [a for a, _, _ in COLUMNS]
    kinds = {a: k for a, _, k in COLUMNS}
    names = {a: n for a, n, _ in COLUMNS}

    gateway = rd.Gateway(args.host, args.port, args.slave, timeout=1.5, retries=2)
    gateway.connect()

    out_handle = open(args.out, "a", newline="") if args.out else None
    writer = None
    if out_handle:
        if out_handle.tell() == 0:
            writer = csv.writer(out_handle)
            writer.writerow(["time"] + [n for _, n, _ in COLUMNS])
        else:
            writer = csv.writer(out_handle)

    print("%-9s %6s %6s %6s %5s %5s %5s %4s %5s %6s %6s %6s %5s %5s %5s %6s" %
          ("time", "tank", "tankT5", "ctrlpt", "hz", "pump", "flow", "mode", "stat", "valve",
           "dhwset", "legion", "lwt", "ewt", "outdr", "odu"))
    samples = []
    deadline = time.time() + args.minutes * 60.0
    try:
        while time.time() < deadline:
            raw_values = poll(gateway, addrs, args.block, names)
            values = {a: decode_for(kinds[a], raw_values.get(a, "")) for a in addrs}
            stamps = datetime.datetime.now().strftime("%H:%M:%S")
            samples.append((time.time(), values))
            print("%-9s %6s %6s %6s %5s %5s %6s %4s %5s %6s %6s %6s %5s %5s %5s %6s" % (
                stamps, values[0x00CE], values[0x000B], values[0x0033], values[0x0017],
                values[0x0055], values[0x102A], values[0x002C], values[0x002D], values[0x00D2],
                values[0x0194], values[0x0195], values[0x0004], values[0x0003], values[0x0001],
                values[0x101F]), flush=True)
            if writer:
                writer.writerow([datetime.datetime.now().isoformat(timespec="seconds")] +
                                [values[a] for a in addrs])
                out_handle.flush()
            time.sleep(args.interval)
    finally:
        gateway.close()
        if out_handle:
            out_handle.close()

    if len(samples) >= 2:
        span_min = (samples[-1][0] - samples[0][0]) / 60.0
        first, last = samples[0][1], samples[-1][1]
        running = sum(1 for _, v in samples if isinstance(v[0x0017], int) and v[0x0017] > 0)
        flowing = sum(1 for _, v in samples if isinstance(v[0x102A], int) and v[0x102A] > 0)
        pumping = sum(1 for _, v in samples if isinstance(v[0x0055], int) and v[0x0055] > 0)
        tank_first, tank_last = first[0x00CE], last[0x00CE]
        rise = ""
        if isinstance(tank_first, float) and isinstance(tank_last, float) and span_min > 0:
            rise = "%+.2f C (%+.3f C/min)" % (tank_last - tank_first,
                                              (tank_last - tank_first) / span_min)
        print("\n== SUMMARY over %.1f min, %d samples ==" % (span_min, len(samples)))
        print("  compressor running : %d/%d samples" % (running, len(samples)))
        print("  water flowing      : %d/%d samples" % (flowing, len(samples)))
        print("  pump commanded     : %d/%d samples" % (pumping, len(samples)))
        print("  tank 0x00CE        : %s -> %s %s" % (tank_first, tank_last, rise))
        print("  tank 0x000B (T5)   : %s -> %s" % (first[0x000B], last[0x000B]))
        print("  DHW setpoint       : %s C (legionella %s, eco %s)"
              % (first[0x0194], first[0x0195], first[0x0196]))
        print("  mode / status      : %s / %s -> %s / %s"
              % (first[0x002C], first[0x002D], last[0x002C], last[0x002D]))


if __name__ == "__main__":
    main()
