#!/usr/bin/env python3
"""
Read-only Modbus register readout for the Rotenso Windmi heat pump.

Talks Modbus RTU over TCP (raw RTU frames with CRC16, no MBAP header) to the
Waveshare gateway in transparent mode, exactly like src/modbus_client.c does.

SAFETY: this tool only ever sends function code 0x03 (Read Holding Registers).
It contains no write path at all; the frame builder asserts on every request.

Usage
-----
  ./tools/windmi_regdump.py --tag before                 # snapshot, default gateway
  ./tools/windmi_regdump.py --tag after
  ./tools/windmi_regdump.py --diff regdumps/before-*.json regdumps/after-*.json
  ./tools/windmi_regdump.py --host 192.168.123.10 --port 8899 --slave 11
  ./tools/windmi_regdump.py --no-scan                    # curated list only
  ./tools/windmi_regdump.py --registers 0x002C,0x002D,0x0194

Why the block scans exist: the display controller's "DHW only" setting is not a
device mode (0x002C only takes 0/1/2), so it has to be produced by changing
other registers. Diffing two full sweeps is how those registers are found.
"""

import argparse
import json
import os
import socket
import struct
import sys
import time

DEFAULT_HOST = "192.168.123.10"
DEFAULT_PORT = 8899
DEFAULT_SLAVE = 11

EXCEPTION_NAMES = {
    0x01: "Illegal Function",
    0x02: "Illegal Data Address",
    0x03: "Illegal Data Value",
    0x04: "Slave Device Failure",
    0x06: "Slave Device Busy",
}

# 3/4/5 are documented as "only in GCHV". Verified live on our unit 2026-11-11:
# the display controller's DHW-only setting writes 5 to 0x002C.
RUNNING_MODE = {0: "off", 1: "cool+dhw", 2: "heat+dhw",
                3: "cool (GCHV)", 4: "heat (GCHV)", 5: "DHW ONLY (GCHV)"}
RUNNING_STATUS = {0: "off", 1: "cool", 2: "heat", 4: "dhw", 7: "defrost", 20: "antifreeze"}
OCCUPANCY = {0: "away", 1: "sleep", 2: "home"}
DHW_MODE_STATUS = {0: "eco", 1: "anti-legionella", 2: "regular"}
DHW_VALVE_STATUS = {0: "opened", 1: "closed"}
WORKING_MODE = {0: "off", 1: "dhw-only", 2: "heating-only", 3: "dhw+heating"}


# ---- decoders ----------------------------------------------------------------

def temp(raw):
    """Signed 0.1 degC; the manual uses -40.0 degC as 'invalid'."""
    value = raw / 10.0
    return "invalid(-40)" if abs(value + 40.0) < 0.05 else value


def scaled(factor):
    def decode(raw):
        return round(raw * factor, 3)
    return decode


def enum(table):
    def decode(raw):
        return table.get(raw, "unknown(%d)" % raw)
    return decode


def raw_value(raw):
    return raw


# (address, label, decoder) — every register the controller code touches, plus
# the setpoint/status registers from the official Windmi manual.
CURATED = [
    (0x0001, "outdoor air temp", temp),
    (0x0002, "indoor air temp", temp),
    (0x0003, "entering water temp (EWT)", temp),
    (0x0004, "leaving water temp (LWT)", temp),
    (0x0005, "gas refrigerant temp", temp),
    (0x000A, "air coil temp T3", temp),
    (0x000B, "DHW tank temp T5", temp),
    (0x0017, "compressor frequency", scaled(1)),
    (0x0029, "occupancy mode", enum(OCCUPANCY)),
    (0x002C, "SETTING MODE (5 = DHW only, native)", enum(RUNNING_MODE)),
    (0x002D, "running status", enum(RUNNING_STATUS)),
    (0x0033, "water control point", temp),
    (0x0055, "pump speed", raw_value),
    (0x0067, "normal/eco switch", {0: "normal", 1: "eco"}.get),
    (0x00C9, "DHW mode status", enum(DHW_MODE_STATUS)),
    (0x00CE, "DHW tank temp", temp),
    (0x00D2, "DHW valve status", enum(DHW_VALVE_STATUS)),
    (0x0174, "compressor runtime h", raw_value),
    (0x0176, "pump runtime h", raw_value),
    (0x0191, "heating target (NOT in vendor table)", temp),
    (0x0192, "heating water setpoint offset (unoccupied)", temp),
    (0x0193, "heating water setpoint offset (economic)", temp),
    (0x0194, "DHW setpoint (normal)", temp),
    (0x0195, "anti-legionella setpoint", temp),
    (0x0196, "DHW setpoint (economic)", temp),
    (0x0197, "cooling water setpoint", temp),
    (0x019C, "heating curve max offset", temp),
    (0x019D, "heating curve min offset", temp),
    (0x01A5, "occupied heating air setpoint", temp),
    (0x0202, "heating min OAT", temp),
    (0x0206, "reserve 0x0206", raw_value),
    (0x0207, "reserve 0x0207", raw_value),
    (0x0209, "user interface type", raw_value),
    (0x0239, "water delta T setpoint", temp),
    (0x0259, "backup heater type", raw_value),
    (0x025A, "warmup time min", raw_value),
    (0x025B, "booster delta T", temp),
    (0x025C, "booster OAT threshold", temp),
    (0x02BF, "DHW priority (readable on current fw)", raw_value),
    (0x1004, "actual capacity output", raw_value),
    (0x1006, "unit capacity kW", raw_value),
    (0x1014, "AC current (x2 A)", scaled(2)),
    (0x1015, "DC current (x4 A)", scaled(4)),
    (0x1016, "AC voltage (V)", scaled(1)),
    (0x1017, "DC voltage (/2 V)", scaled(0.5)),
    (0x101F, "ODU input status bitfield", raw_value),
    (0x102A, "water flow (m3/h x100)", scaled(0.01)),
]

# Contiguous sweeps for discovering what the panel actually changes.
SCAN_RANGES = [
    (0x0000, 0x006F, "status/temps"),
    (0x00C0, 0x00DF, "DHW status block"),
    (0x0170, 0x01BF, "runtimes"),
    (0x0190, 0x01BF, "setpoints"),
    (0x0200, 0x02BF, "config/curves"),
    (0x1000, 0x103F, "energy block"),
]


# ---- Modbus RTU over TCP -----------------------------------------------------

def crc16(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc & 0xFFFF


class Gateway:
    """Read-only Modbus RTU over TCP client. FC 03 and nothing else."""

    def __init__(self, host, port, slave, timeout=2.0, retries=2):
        self.host = host
        self.port = port
        self.slave = slave
        self.timeout = timeout
        self.retries = retries
        self.sock = None
        self.requests = 0

    def connect(self):
        self.sock = socket.create_connection((self.host, self.port), self.timeout)
        self.sock.settimeout(self.timeout)

    def close(self):
        if self.sock:
            self.sock.close()
            self.sock = None

    def _drain(self):
        self.sock.settimeout(0.0)
        try:
            while self.sock.recv(4096):
                pass
        except (BlockingIOError, socket.timeout):
            pass
        finally:
            self.sock.settimeout(self.timeout)

    def _recv_exact(self, count):
        buf = b""
        while len(buf) < count:
            chunk = self.sock.recv(count - len(buf))
            if not chunk:
                raise IOError("connection closed by gateway")
            buf += chunk
        return buf

    def read_registers(self, address, count):
        """Return a list of `count` signed 16-bit values, or raise ReadError."""
        pdu = struct.pack(">BBHH", self.slave, 0x03, address, count)
        assert pdu[1] == 0x03, "read-only tool: refusing to send function %#04x" % pdu[1]
        frame = pdu + struct.pack("<H", crc16(pdu))

        last_error = "not attempted"
        for _ in range(self.retries + 1):
            try:
                self._drain()
                self.sock.sendall(frame)
                self.requests += 1
                header = self._recv_exact(3)
                if header[0] != self.slave:
                    raise IOError("slave mismatch: got %d" % header[0])
                function = header[1]
                if function & 0x80:
                    code = header[2]  # exception code came in the 3-byte header
                    self._recv_exact(2)  # CRC, not verified: we only report the code
                    raise ReadError("exception 0x%02X %s" % (code, EXCEPTION_NAMES.get(code, "?")),
                                    exception=code)
                byte_count = header[2]
                if byte_count != count * 2:
                    raise IOError("byte count %d, expected %d" % (byte_count, count * 2))
                payload = self._recv_exact(byte_count + 2)
                body, received_crc = payload[:-2], struct.unpack("<H", payload[-2:])[0]
                if crc16(header + body) != received_crc:
                    raise IOError("CRC mismatch")
                return list(struct.unpack(">%dh" % count, body))
            except ReadError:
                raise
            except (IOError, socket.timeout, struct.error) as exc:
                last_error = str(exc)
                self._reconnect()
        raise ReadError(last_error)

    def _reconnect(self):
        self.close()
        try:
            self.connect()
        except OSError:
            pass


class ReadError(Exception):
    def __init__(self, message, exception=None):
        super().__init__(message)
        self.exception = exception


# ---- snapshot ----------------------------------------------------------------

def collect(gateway, targets, block=20, quiet=False, progress=None):
    """Read `targets` = list of (address, label, decoder).

    Contiguous addresses are read with one multi-register request. If that
    fails, the rest of that run is read one register at a time: a single
    Illegal Data Address aborts an entire block on this firmware, so the
    per-register fallback is what keeps a sweep from losing 20 registers.
    """
    readings = {}
    by_address = {address: (label, decoder) for address, label, decoder in targets}

    runs = []
    for address in sorted(by_address):
        if runs and address == runs[-1][0] + runs[-1][1]:
            runs[-1][1] += 1
        else:
            runs.append([address, 1])

    for run_index, (start, length) in enumerate(runs):
        address, end = start, start + length
        degraded = False
        while address < end:
            count = 1 if degraded else min(block, end - address)
            try:
                values = gateway.read_registers(address, count)
                for offset, value in enumerate(values):
                    addr = address + offset
                    if addr in by_address:
                        label, decoder = by_address[addr]
                        readings[addr] = {"raw": value, "value": decode(decoder, value),
                                          "label": label, "status": "ok"}
                address += count
            except ReadError as exc:
                if count > 1:
                    degraded = True  # one bad address in the block: go one by one
                    continue
                label, decoder = by_address[address]
                readings[address] = {"raw": None, "value": None, "label": label,
                                     "status": str(exc)}
                if not quiet:
                    print("  %#06x %-42s READ FAIL: %s" % (address, label, exc), file=sys.stderr)
                address += 1
            if progress:
                progress(len(readings), len(by_address), run_index, len(runs))
    return readings


def decode(decoder, raw):
    try:
        value = decoder(raw)
    except Exception:  # a decoder must never lose a reading
        value = raw
    if isinstance(value, float):
        value = round(value, 3)
    return value


def build_targets(scan=True, registers=None):
    if registers:
        return [(address, "user-supplied", raw_value) for address in registers]
    targets = list(CURATED)
    if scan:
        seen = {address for address, _, _ in targets}
        for start, end, _region in SCAN_RANGES:
            for address in range(start, end + 1):
                if address not in seen:
                    targets.append((address, "scan", raw_value))
                    seen.add(address)
    return targets


def print_table(readings):
    width = max(len(r["label"]) for r in readings.values()) if readings else 10
    print("%-8s %-*s %-14s %s" % ("ADDR", width, "LABEL", "RAW", "DECODED / ERROR"))
    for address in sorted(readings):
        item = readings[address]
        if item["status"] == "ok":
            print("%#06x %-*s %-14s %s" % (address, width, item["label"], item["raw"], item["value"]))
        else:
            print("%#06x %-*s %-14s %s" % (address, width, item["label"], "-", item["status"]))


def do_diff(path_a, path_b):
    with open(path_a) as handle_a, open(path_b) as handle_b:
        snap_a, snap_b = json.load(handle_a), json.load(handle_b)
    regs_a, regs_b = snap_a["registers"], snap_b["registers"]

    def key(address):
        return int(address, 16)

    changed, appeared, vanished = [], [], []
    for address in sorted(set(regs_a) | set(regs_b), key=key):
        a, b = regs_a.get(address), regs_b.get(address)
        if a is None:
            appeared.append((address, b))
        elif b is None:
            vanished.append((address, a))
        elif a.get("raw") != b.get("raw") or a.get("status") != b.get("status"):
            changed.append((address, a, b))

    print("A: %s (%s)" % (path_a, snap_a["timestamp"]))
    print("B: %s (%s)" % (path_b, snap_b["timestamp"]))
    print("\n== CHANGED (%d) ==" % len(changed))
    for address, a, b in changed:
        label = b.get("label") or a.get("label") or ""
        print("  %s %-44s %s -> %s   | %s -> %s" %
              (address, label, a.get("raw"), b.get("raw"), a.get("value"), b.get("value")))
    print("\n== NEWLY READABLE IN B (%d) ==" % len(appeared))
    for address, b in appeared:
        print("  %s %-44s %s | %s" % (address, b.get("label"), b.get("raw"), b.get("value")))
    print("\n== NO LONGER READABLE IN B (%d) ==" % len(vanished))
    for address, a in vanished:
        print("  %s %-44s was %s" % (address, a.get("label"), a.get("raw")))
    return len(changed)


def parse_registers(text):
    return [int(part, 0) for part in text.split(",") if part.strip()]


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default=os.environ.get("WINDMI_HOST", DEFAULT_HOST))
    parser.add_argument("--port", type=int, default=int(os.environ.get("WINDMI_PORT", DEFAULT_PORT)))
    parser.add_argument("--slave", type=int, default=int(os.environ.get("WINDMI_SLAVE", DEFAULT_SLAVE)))
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--retries", type=int, default=2, help="retries per read before giving up")
    parser.add_argument("--block", type=int, default=20, help="max registers per request")
    parser.add_argument("--tag", default="snap", help="label for the snapshot filename")
    parser.add_argument("--out-dir", default="regdumps")
    parser.add_argument("--registers", help="comma-separated addresses, skips the standard list")
    parser.add_argument("--no-scan", action="store_true", help="curated list only, no block sweeps")
    parser.add_argument("--quiet", action="store_true")
    parser.add_argument("--diff", nargs=2, metavar=("A.json", "B.json"), help="compare two snapshots")
    args = parser.parse_args()

    if args.diff:
        return 1 if do_diff(args.diff[0], args.diff[1]) else 0

    targets = build_targets(scan=not args.no_scan,
                            registers=parse_registers(args.registers) if args.registers else None)
    if not args.quiet:
        print("gateway %s:%d slave %d -- %d registers, read-only (FC 0x03)"
              % (args.host, args.port, args.slave, len(targets)), file=sys.stderr)

    gateway = Gateway(args.host, args.port, args.slave, timeout=args.timeout, retries=args.retries)
    started = time.time()
    try:
        gateway.connect()
    except OSError as exc:
        print("cannot reach gateway %s:%d -- %s" % (args.host, args.port, exc), file=sys.stderr)
        return 2
    try:
        def progress(done, total, run_index, run_count):
            if not args.quiet and done % 50 == 0:
                print("  ... %d/%d registers, run %d/%d" % (done, total, run_index + 1, run_count),
                      file=sys.stderr)

        readings = collect(gateway, targets, block=args.block, quiet=args.quiet,
                           progress=progress)
    finally:
        gateway.close()

    ok = sum(1 for r in readings.values() if r["status"] == "ok")
    snapshot = {
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "host": args.host,
        "port": args.port,
        "slave": args.slave,
        "tag": args.tag,
        "requests": gateway.requests,
        "seconds": round(time.time() - started, 2),
        "readable": ok,
        "attempted": len(targets),
        "registers": {"0x%04X" % address: reading for address, reading in sorted(readings.items())},
    }

    print_table(readings)
    print("\n%d/%d registers readable, %d requests, %.1fs"
          % (ok, len(targets), gateway.requests, snapshot["seconds"]))

    os.makedirs(args.out_dir, exist_ok=True)
    stem = "%s-%s" % (args.tag, time.strftime("%Y%m%d-%H%M%S"))
    json_path = os.path.join(args.out_dir, stem + ".json")
    with open(json_path, "w") as handle:
        json.dump(snapshot, handle, indent=2)
    print("wrote %s" % json_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
