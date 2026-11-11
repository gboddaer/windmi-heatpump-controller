#!/usr/bin/env python3
"""
Shared driver for the Windmi hardware write tests.

Each test writes exactly ONE register with function code 0x06 and verifies it
three ways:

  1. the FC 06 echo (slave, function, address, value, CRC)
  2. an FC 03 read-back of that register
  3. watching the behavioural registers for a while to see the unit actually move

Before and after, it snapshots the address set the device is known to answer
(from the last full sweep) and prints the diff, so side effects are observed
rather than assumed. A refused write is reported as a failure, never as success.

Registers written by the tests: 0x002C (setting mode) and 0x02BF (DHW priority).
Nothing else.
"""

import glob
import json
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import windmi_regdump as rd  # noqa: E402  (read-only client, decoders, CRC)

REG_SETTING_MODE = 0x002C
REG_RUNNING_STATUS = 0x002D
REG_DHW_PRIORITY = 0x02BF
REG_DHW_VALVE = 0x00D2
REG_DHW_TANK = 0x00CE

# Vendor manual values; 3/4/5 are marked "only in GCHV". Confirmed live on our
# unit 2026-11-11: mode 5 is what the display controller uses for DHW-only.
MODES = {0: "off", 1: "cool+dhw", 2: "heat+dhw", 3: "cool (GCHV)", 4: "heat (GCHV)",
         5: "DHW only (GCHV)"}
PRIORITIES = {0: "heating/auto", 1: "DHW priority"}

# Which registers to poll while waiting for the unit to react.
WATCH_SETS = {
    "mode": [(REG_SETTING_MODE, MODES.get), (REG_RUNNING_STATUS, rd.RUNNING_STATUS.get)],
    "priority": [(REG_DHW_PRIORITY, PRIORITIES.get), (REG_RUNNING_STATUS, rd.RUNNING_STATUS.get),
                 (REG_DHW_VALVE, rd.DHW_VALVE_STATUS.get), (REG_DHW_TANK, rd.temp)],
}


class WriteError(Exception):
    pass


class GatewayRW(rd.Gateway):
    """Adds a verified FC 06 write on top of the read-only client."""

    def write_register(self, address, value):
        pdu = struct.pack(">BBHH", self.slave, 0x06, address, value)
        frame = pdu + struct.pack("<H", rd.crc16(pdu))
        self._drain()
        self.sock.sendall(frame)
        self.requests += 1
        # A normal FC 06 reply is an 8-byte echo; an exception reply is only
        # 5 bytes, so the function byte decides how much more to read.
        header = self._recv_exact(3)
        if header[1] & 0x80:
            self._recv_exact(2)  # CRC of the exception frame
            code = header[2]
            raise WriteError("exception 0x%02X %s" % (code, rd.EXCEPTION_NAMES.get(code, "?")))
        response = header + self._recv_exact(5)  # address, value, CRC
        if rd.crc16(response[:6]) != struct.unpack("<H", response[6:8])[0]:
            raise WriteError("CRC mismatch on write response")
        _slave, _func, echo_address, echo_value = struct.unpack(">BBHH", response[:6])
        if (echo_address, echo_value) != (address, value):
            raise WriteError("echo mismatch: wrote %#06x=%d, device echoed %#06x=%d"
                             % (address, value, echo_address, echo_value))
        return True


def controller_is_running():
    """The controller writes 0x002C/0x02BF every 30 s and would fight these tests."""
    for entry in glob.glob("/proc/[0-9]*/comm"):
        try:
            with open(entry) as handle:
                if handle.read().strip() == "windmi-control":
                    return entry.split("/")[2]
        except OSError:
            continue
    return None


def default_watch_file():
    files = sorted(glob.glob(os.path.join("regdumps", "full*-*.json")), key=os.path.getmtime)
    return files[-1] if files else None


def watch_targets(path):
    """(address, label, decoder) triples for every address the device answered
    in a previous full sweep, so the before/after diff is quick and complete."""
    with open(path) as handle:
        snapshot = json.load(handle)
    curated = {address: (label, decoder) for address, label, decoder in rd.CURATED}
    targets = []
    for key, item in snapshot["registers"].items():
        if item.get("status") != "ok":
            continue
        address = int(key, 16)
        label, decoder = curated.get(address, (item.get("label") or "known-live", rd.raw_value))
        targets.append((address, label, decoder))
    targets.sort()
    return targets


def take_snapshot(gateway, targets, tag, out_dir, quiet=False):
    readings = rd.collect(gateway, targets, block=20, quiet=True)
    ok = sum(1 for item in readings.values() if item["status"] == "ok")
    snapshot = {
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "host": gateway.host, "port": gateway.port, "slave": gateway.slave,
        "tag": tag,
        "requests": gateway.requests,
        "readable": ok, "attempted": len(targets),
        "registers": {"0x%04X" % address: reading for address, reading in sorted(readings.items())},
    }
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, "%s-%s.json" % (tag, time.strftime("%Y%m%d-%H%M%S")))
    with open(path, "w") as handle:
        json.dump(snapshot, handle, indent=2)
    if not quiet:
        print("  snapshot %-14s %3d/%d readable -> %s" % (tag, ok, len(targets), path))
    return path


def poll(gateway, addresses):
    values = {}
    for address in addresses:
        try:
            values[address] = gateway.read_registers(address, 1)[0]
        except rd.ReadError as exc:
            values[address] = str(exc)
    return values


def format_values(values, decoders):
    return "  ".join("%#06x=%-4s %s" % (address, values[address],
                                        decoders.get(address, lambda v: "")(values[address])
                                        if isinstance(values[address], int) else "")
                     for address in sorted(values))


def watch(gateway, seconds, watch_set, interval=2.0):
    print("  watching for %.0fs:" % seconds)
    addresses = [address for address, _decoder in watch_set]
    decoders = dict(watch_set)
    history = []
    last = None
    started = time.time()
    while True:
        values = poll(gateway, addresses)
        stamp = time.time() - started
        if values != last:
            print("    %5.0fs  %s" % (stamp, format_values(values, decoders)))
            last = dict(values)
        history.append({"t": round(stamp, 1), **{"0x%04X" % a: v for a, v in values.items()}})
        if stamp >= seconds:
            break
        time.sleep(interval)
    return history


def run_reg_test(register, value, description, watch_set_name="mode", tag=None, argv=None):
    import argparse

    parser = argparse.ArgumentParser(
        description="Write %#06x = %d (%s) and verify" % (register, value, description))
    parser.add_argument("--host", default=os.environ.get("WINDMI_HOST", rd.DEFAULT_HOST))
    parser.add_argument("--port", type=int, default=int(os.environ.get("WINDMI_PORT", rd.DEFAULT_PORT)))
    parser.add_argument("--slave", type=int, default=int(os.environ.get("WINDMI_SLAVE", rd.DEFAULT_SLAVE)))
    parser.add_argument("--timeout", type=float, default=1.5)
    parser.add_argument("--watch", type=float, default=40.0, help="seconds to watch the unit react")
    parser.add_argument("--out-dir", default="regdumps")
    parser.add_argument("--watch-file", help="snapshot defining the register set to diff")
    parser.add_argument("--force", action="store_true", help="run even if windmi-control is running")
    args = parser.parse_args(argv)

    tag = tag or "w%04X_%d" % (register, value)
    print("== %s: writing %#06x = %d ==" % (description, register, value))

    pid = controller_is_running()
    if pid and not args.force:
        print("ABORT: windmi-control is running (pid %s) and rewrites the same registers"
              " every 30 s." % pid)
        print("       Stop it first, or pass --force.")
        return 3

    watch_file = args.watch_file or default_watch_file()
    if watch_file:
        targets = watch_targets(watch_file)
        print("  diffing against the %d registers the device answers (%s)"
              % (len(targets), watch_file))
    else:
        targets = rd.build_targets(scan=False)
        print("  no previous full sweep found; using the %d curated registers" % len(targets))

    gateway = GatewayRW(args.host, args.port, args.slave, timeout=args.timeout, retries=1)
    try:
        gateway.connect()
    except OSError as exc:
        print("ABORT: cannot reach %s:%d -- %s" % (args.host, args.port, exc))
        return 2

    try:
        print("\n-- before --")
        before_path = take_snapshot(gateway, targets, tag + "-before", args.out_dir)
        print("  %-22s %s" % ("before", format_values(poll(gateway, [register]), {})))

        print("\n-- write --")
        try:
            gateway.write_register(register, value)
            print("  FC 06 echo OK (address and value confirmed by the device)")
        except WriteError as exc:
            print("  WRITE FAILED: %s" % exc)
            return 1

        try:
            echoed = gateway.read_registers(register, 1)[0]
        except rd.ReadError as exc:
            print("  READ-BACK FAILED: %s" % exc)
            return 1
        if echoed != value:
            print("  VERIFY FAILED: device holds %d, expected %d" % (echoed, value))
            return 1
        print("  read-back confirms %#06x = %d" % (register, value))

        print("\n-- device response --")
        history = watch(gateway, args.watch, WATCH_SETS[watch_set_name])

        print("\n-- after --")
        after_path = take_snapshot(gateway, targets, tag + "-after", args.out_dir)

        print("\n-- what the change did --")
        changed = rd.do_diff(before_path, after_path)

        seen = sorted({entry["0x%04X" % addr] for entry in history
                       for addr in dict(WATCH_SETS[watch_set_name])
                       if isinstance(entry["0x%04X" % addr], int)})
        print("\n== RESULT ==")
        print("  register write   : verified (echo + read-back), %#06x = %d" % (register, value))
        print("  values seen      : %s" % ", ".join(str(v) for v in seen))
        print("  registers moved  : %d (see diff above)" % changed)
        print("  snapshots        : %s\n                     %s" % (before_path, after_path))
        return 0
    finally:
        gateway.close()


def run_mode_test(mode_value, description, argv=None):
    """Backwards-compatible entry point for the two mode scripts."""
    return run_reg_test(REG_SETTING_MODE, mode_value, description,
                        watch_set_name="mode", tag="mode%d" % mode_value, argv=argv)
