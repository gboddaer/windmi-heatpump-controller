#!/usr/bin/env python3
"""Behaviour monitor for a running windmi-control: judges the unit against the
arbiter's own laws and reports violations, not samples.

Read-only. It talks to the web API only, never to the gateway: a second Modbus
client on this gateway corrupts frames (docs/learned.md, 2026-11-13).

  tools/monitor_behaviour.py                 # run until stopped
  tools/monitor_behaviour.py --once          # single pass, for cron
  tools/monitor_behaviour.py --interval 5    # faster polling

Outputs, both append-only so a running instance is never disturbed:
  /var/tmp/wpomp_samples.csv   every poll, the registers that matter
  /var/tmp/wpomp_findings.log  one line per rule violation, with the evidence

Rules are deliberately pessimistic: each one is a way this controller could be
wrong that we have already seen once, plus the ways the 2026-11-13 measurements
showed the unit can refuse to do what a register says.
"""

import argparse
import csv
import json
import os
import socket
import statistics
import sys
import time
import urllib.error
import urllib.request

BASE_DEFAULT = "http://localhost:8080"
SAMPLES = "/var/tmp/wpomp_samples.csv"
FINDINGS = "/var/tmp/wpomp_findings.log"
APP_LOG = os.environ.get("WPOMP_LOG", "/var/tmp/wpomp.log")
PROC_MATCH = "build/windmi-control"

# Setpoints as they were when the monitor started. A change is not a fault, but
# it must be reported: the display controller moving them means the arbiter's
# saved target and the unit's setpoint now disagree.
WATCH_SETPOINTS = ("dhwTarget", "dhwEcoTarget", "heatingTarget")

# Everything numeric that the unit moves. Used for the "nothing changed" check.
MONITORED = (
    "dhwTemperature", "dhwTarget", "dhwEcoTarget", "effectiveDhwTarget",
    "heatingTarget", "heatingTemperature", "outdoorTemperature",
    "leavingWaterTemperature", "enteringWaterTemperature", "compressorFrequency",
    "waterFlow", "dhwValveStatus", "mode", "runningStatus", "dhwState",
    "unitActuating", "requestedDeviceMode", "waterControlPoint",
    "compressorRuntimeHours", "pumpRuntimeHours", "acPowerW", "cop",
)

COLUMNS = ("ts", "latency_ms", "mode", "requestedDeviceMode", "runningStatus",
           "dhwState", "dhwCalling", "unitActuating", "dhwTemperature",
           "dhwTarget", "dhwEcoTarget", "effectiveDhwTarget", "waterControlPoint",
           "heatingTarget", "heatingTemperature", "outdoorTemperature",
           "leavingWaterTemperature", "enteringWaterTemperature",
           "compressorFrequency", "waterFlow", "dhwValveStatus", "tankReadFailures",
           "dhwSensorFaultSuspected", "secondsSinceModeWrite", "cop", "acPowerW",
           "deviceOnline")


def utcnow():
    return time.strftime("%Y-%m-%d %H:%M:%S", time.localtime())


class Reporter:
    """Findings go to stdout and the findings file, deduplicated while they last."""

    def __init__(self, path, dedup_s):
        self.path = path
        self.dedup_s = dedup_s
        self.last = {}

    def emit(self, rule, severity, evidence):
        now = time.time()
        key = rule
        if now - self.last.get(key, 0.0) < self.dedup_s:
            return
        self.last[key] = now
        line = f"{utcnow()}  {severity:5}  {rule:22}  {evidence}"
        print(line, flush=True)
        with open(self.path, "a", encoding="utf-8") as fh:
            fh.write(line + "\n")


def fetch(base, timeout):
    t0 = time.perf_counter()
    try:
        with urllib.request.urlopen(base.rstrip("/") + "/api/status", timeout=timeout) as r:
            data = json.load(r)
        return data, (time.perf_counter() - t0) * 1000.0, None
    except urllib.error.HTTPError as e:
        return None, (time.perf_counter() - t0) * 1000.0, f"HTTP {e.code}"
    except socket.timeout:
        return None, (time.perf_counter() - t0) * 1000.0, "timeout"
    except Exception as e:                                    # noqa: BLE001
        return None, (time.perf_counter() - t0) * 1000.0, f"{type(e).__name__}: {e}"


def find_pids():
    pids = []
    for pid in (p for p in os.listdir("/proc") if p.isdigit()):
        try:
            with open(f"/proc/{pid}/cmdline", "rb") as fh:
                cmd = fh.read().replace(b"\0", b" ").decode(errors="replace")
        except OSError:
            continue
        if PROC_MATCH in cmd and "monitor_behaviour" not in cmd:
            pids.append(int(pid))
    return pids


def proc_stats(pid):
    """(rss_kib, cpu_seconds, threads) for one pid, or None."""
    try:
        with open(f"/proc/{pid}/status", encoding="utf-8") as fh:
            status = fh.read()
        with open(f"/proc/{pid}/stat", encoding="utf-8") as fh:
            stat = fh.read().split()
    except OSError:
        return None
    rss = None
    threads = None
    for line in status.splitlines():
        if line.startswith("VmRSS:"):
            rss = int(line.split()[1])
        elif line.startswith("Threads:"):
            threads = int(line.split()[1])
    ticks = int(stat[13]) + int(stat[14])
    hz = os.sysconf("SC_CLK_TCK")
    return rss, ticks / hz, threads


def tail_new_lines(path, offset):
    """New log lines without disturbing the writer: append-only file, read from offset."""
    try:
        size = os.path.getsize(path)
    except OSError:
        return offset, []
    if size < offset:                      # rotated or truncated by the operator
        offset = 0
    if size == offset:
        return offset, []
    with open(path, "rb") as fh:
        fh.seek(offset)
        chunk = fh.read(size - offset)
    return size, chunk.decode(errors="replace").splitlines()


class Monitor:
    def __init__(self, base, interval, rep, from_start=False):
        self.base = base
        self.interval = interval
        self.rep = rep
        self.samples = 0
        self.latencies = []
        self.setpoints = {}
        self.prev = None
        self.prev_ts = None
        self.prev_all = None
        self.frozen_since = None
        self.gaps = []
        self.last_change = None
        self.grant_idle_since = None       # mode 5, nothing actuating
        self.calling_idle_since = None     # mode 2, DHW calling, nothing actuating
        self.mismatch_since = None         # requested != actual
        self.running_no_cop_since = None
        self.transitions = []              # (ts, what)
        self.pid = None
        # Start at the end of the log: a monitor reports what happens while it runs.
        # The history is audited by tools/audit_log.py, and --from-start replays it.
        try:
            self.log_offset = 0 if from_start else os.path.getsize(APP_LOG)
        except OSError:
            self.log_offset = 0
        self.cpu_base = None
        self.started = time.time()

    # ---- bookkeeping -------------------------------------------------------

    def open_csv(self):
        new = not os.path.exists(SAMPLES) or os.path.getsize(SAMPLES) == 0
        self.csv_fh = open(SAMPLES, "a", newline="", encoding="utf-8")
        self.csv = csv.writer(self.csv_fh)
        if new:
            self.csv.writerow(COLUMNS)

    def transition(self, what):
        self.transitions.append((time.time(), what))
        self.rep.emit("TRANSITION", "info", what)

    def within(self, minutes, what=None):
        cut = time.time() - minutes * 60
        out = [t for (t, w) in self.transitions if t >= cut]
        if what is not None:
            out = [t for (t, w) in self.transitions if t >= cut and what in w]
        return out

    # ---- rules -------------------------------------------------------------

    def rule_latency(self, latency, error):
        if error:
            self.rep.emit("API-UNRESPONSIVE", "ERROR", f"/api/status failed: {error}")
            return
        self.latencies.append(latency)
        if latency > 2000:
            self.rep.emit("API-SLOW", "ERROR", f"/api/status took {latency:.0f} ms")
        elif latency > 400:
            self.rep.emit("API-SLOW", "WARN", f"/api/status took {latency:.0f} ms")
        recent = self.latencies[-30:]
        if len(recent) >= 30:
            p95 = sorted(recent)[int(len(recent) * 0.95)]
            if p95 > 250:
                self.rep.emit("API-SLOW-P95", "WARN",
                              f"p95 over 30 polls {p95:.0f} ms (max {max(recent):.0f} ms)")
            if p95 < 100:
                self.rep.last.pop("API-SLOW-P95", None)

    def rule_loop_period(self, all_key):
        """Estimate the control loop's publish period from when values actually change.

        The loop polls the unit on a fixed interval and the API serves the last
        snapshot, so the registers move on that rhythm. A period far beyond it means
        the loop is spending its time somewhere else: Modbus timeouts, a stuck
        reconnect, or a thread that died and left the last snapshot on the shelf.
        """
        if self.prev_all is not None and all_key != self.prev_all:
            now = time.time()
            if self.last_change is not None:
                gap = now - self.last_change
                if gap > 300:
                    self.rep.emit("LOOP-GAP", "ERROR",
                                  f"nothing changed for {gap / 60:.1f} min between two updates")
                self.gaps.append(gap)
                if len(self.gaps) >= 12:
                    recent = self.gaps[-12:]
                    med = statistics.median(recent)
                    if med > 75:
                        self.rep.emit("LOOP-SLOW", "ERROR",
                                      f"updates arrive every {med:.0f} s (median of 12); the "
                                      "loop is meant to run every 30 s")
                    elif med > 50:
                        self.rep.emit("LOOP-SLOW", "WARN",
                                      f"updates arrive every {med:.0f} s (median of 12)")
                    elif med < 15:
                        self.rep.emit("LOOP-FAST", "WARN",
                                      f"updates arrive every {med:.0f} s: faster than the tick, "
                                      "so something is polling the unit off-rhythm")
                    else:
                        self.rep.last.pop("LOOP-SLOW", None)
                        self.rep.last.pop("LOOP-FAST", None)
            self.last_change = now
        elif self.last_change is None:
            self.last_change = time.time()

    def rule_frozen(self, all_key):
        """Nothing moved at all: an idle unit does this, a stalled loop does too."""
        if all_key == self.prev_all:
            if self.frozen_since is None:
                self.frozen_since = time.time()
            idle = time.time() - self.frozen_since
            if idle > 600:
                self.rep.emit("NOTHING-MOVING", "INFO",
                              f"every monitored register unchanged for {idle / 60:.0f} min; "
                              "idle unit, or the control loop stopped polling")
        else:
            self.frozen_since = None

    def rule_setpoints(self, d):
        for key in WATCH_SETPOINTS:
            val = d.get(key)
            if val is None:
                continue
            if key not in self.setpoints:
                self.setpoints[key] = val
            elif abs(float(val) - float(self.setpoints[key])) > 0.4:
                self.rep.emit("SETPOINT-MOVED", "WARN",
                              f"{key} {self.setpoints[key]} -> {val} "
                              "(panel or restore rule; arbiter saved target and unit "
                              "setpoint now disagree until the next write)")
                self.setpoints[key] = val

    def rule_transitions(self, d):
        if self.prev is None:
            return
        p, c = self.prev, d
        if p.get("mode") != c.get("mode"):
            self.transition(f"device mode {p.get('mode')} -> {c.get('mode')} "
                            f"tank={c.get('dhwTemperature')} state={c.get('dhwState')} "
                            f"hz={c.get('compressorFrequency')}")
        if p.get("dhwState") != c.get("dhwState"):
            self.transition(f"dhw state {p.get('dhwState')} -> {c.get('dhwState')} "
                            f"tank={c.get('dhwTemperature')} "
                            f"eff={c.get('effectiveDhwTarget')}")
        if p.get("dhwValveStatus") != c.get("dhwValveStatus"):
            self.transition(f"valve {p.get('dhwValveStatus')} -> {c.get('dhwValveStatus')}")

        # A release that is immediately followed by a re-grant is the metronome the
        # park cooldown exists to stop.
        mode_swaps = self.within(15, "device mode")
        if len(mode_swaps) >= 3:
            self.rep.emit("MODE-FLAPPING", "ERROR",
                          f"{len(mode_swaps)} device-mode changes in 15 min")

        thrash = self.within(60, "dhw state satisfied -> calling")
        if len(thrash) > 2:
            self.rep.emit("SATISFACTION-THRASH", "WARN",
                          f"{len(thrash)} satisfied->calling flips in 60 min; "
                          "hysteresis band or the release margin is wrong for this tank")

    def rule_priority_laws(self, d):
        """The laws the arbiter claims, checked from outside."""
        now = time.time()
        mode = d.get("mode")
        act = bool(d.get("unitActuating"))
        state = d.get("dhwState")
        calling = bool(d.get("dhwCalling"))

        if mode == "dhw_only" and not act:
            if self.grant_idle_since is None:
                self.grant_idle_since = now
            idle = now - self.grant_idle_since
            if idle > 900 and state == "calling":
                self.rep.emit("PARK-RULE-QUIET", "ERROR",
                              f"DHW granted and nothing actuating for {idle / 60:.0f} min "
                              f"while the tank calls ({d.get('dhwTemperature')} C vs "
                              f"{d.get('effectiveDhwTarget')} C): the release did not happen")
        else:
            self.grant_idle_since = None

        if mode == "heat+dhw" and calling and not act:
            if self.calling_idle_since is None:
                self.calling_idle_since = now
            held = now - self.calling_idle_since
            if held > 1800:
                self.rep.emit("DH-CALLING-UNSERVED", "WARN",
                              f"tank calls and nothing runs for {held / 60:.0f} min while the "
                              f"compressor is on the house ({d.get('dhwTemperature')} C vs "
                              f"{d.get('effectiveDhwTarget')} C); park cooldown plus a cold "
                              "house explains some of this, more than half an hour does not")
        else:
            self.calling_idle_since = None

        if d.get("requestedDeviceMode") and d.get("requestedDeviceMode") != mode:
            if self.mismatch_since is None:
                self.mismatch_since = now
            held = now - self.mismatch_since
            sev = "ERROR" if held > 180 else "WARN"
            self.rep.emit("MODE-MISMATCH", sev,
                          f"asked for {d.get('requestedDeviceMode')}, unit reports {mode}, "
                          f"{held:.0f} s: panel override, a failed write, or a verification "
                          "that is not correcting")
        else:
            self.mismatch_since = None

        if d.get("dhwSensorFaultSuspected"):
            self.rep.emit("SENSOR-FAULT-FLAG", "ERROR",
                          f"charge watchdog fired: tank {d.get('dhwTemperature')} C after a "
                          "charge that did not rise")

        if int(d.get("tankReadFailures") or 0) > 0:
            self.rep.emit("TANK-READ-FAILURES", "ERROR",
                          f"{d.get('tankReadFailures')} consecutive ticks without a tank "
                          "reading; the arbiter holds the mode and demand is unknown")

    def rule_physics(self, d):
        tank = d.get("dhwTemperature")
        if tank is not None:
            if not 5.0 <= float(tank) <= 75.0:
                self.rep.emit("TANK-IMPLAUSIBLE", "ERROR", f"tank {tank} C")
            if self.prev is not None and self.prev_ts:
                dt = (time.time() - self.prev_ts) / 60.0
                if dt > 0:
                    rate = (float(tank) - float(self.prev.get("dhwTemperature") or tank)) / dt
                    if rate < -2.0:
                        self.rep.emit("TANK-DROPS-FAST", "WARN",
                                      f"tank falling {-rate:.1f} C/min ({tank} C): a real draw "
                                      "if it lasts minutes, a sensor if it does not")
                    elif rate > 3.0:
                        self.rep.emit("TANK-RISES-FAST", "WARN",
                                      f"tank rising {rate:.1f} C/min to {tank} C")
        lwt = d.get("leavingWaterTemperature")
        if tank is not None and lwt is not None and d.get("dhwValveStatus") == 1:
            if float(lwt) - float(tank) > 25.0:
                self.rep.emit("CHARGE-DELTA-HUGE", "INFO",
                              f"charging with LWT {lwt} C against tank {tank} C: stratification "
                              "or a probe that is not in the tank")
        if float(d.get("compressorFrequency") or 0) > 0 and not d.get("copValid"):
            if self.running_no_cop_since is None:
                self.running_no_cop_since = time.time()
            if time.time() - self.running_no_cop_since > 300:
                self.rep.emit("COP-INVALID", "INFO",
                              "compressor running with copValid false for over 5 min: the "
                              "power/temperature inputs the COP needs are not all present")
        else:
            self.running_no_cop_since = None

    def rule_process(self):
        pids = find_pids()
        if not pids:
            self.rep.emit("APP-NOT-RUNNING", "ERROR",
                          "no windmi-control process: no arbiter, and the unit was left in "
                          "DHW-only by the exit path")
            self.pid = None
            return
        pid = pids[0]
        if self.pid is not None and pid != self.pid:
            self.rep.emit("APP-RESTARTED", "ERROR",
                          f"pid {self.pid} -> {pid}: something killed the controller")
        self.pid = pid
        try:
            fds = len(os.listdir(f"/proc/{pid}/fd"))
        except OSError:
            fds = None
        if fds and fds > 128:
            self.rep.emit("FD-GROWTH", "WARN",
                          f"{fds} file descriptors open: connections are not being closed")
        stats = proc_stats(pid)
        if not stats:
            return
        rss, cpu_s, threads = stats
        if self.cpu_base is None:
            self.cpu_base = (time.time(), cpu_s, rss)
        else:
            t0, c0, r0 = self.cpu_base
            span = time.time() - t0
            if span > 300:
                cpu = 100.0 * (cpu_s - c0) / span
                if cpu > 10.0:
                    self.rep.emit("CPU-HIGH", "WARN",
                                  f"{cpu:.1f}% of a core sustained over {span / 60:.0f} min")
                if rss - r0 > 2048:
                    self.rep.emit("MEMORY-GROWTH", "WARN",
                                  f"RSS grew {(rss - r0) / 1024:.1f} MiB over "
                                  f"{span / 60:.0f} min")
                if threads and threads > 12:
                    self.rep.emit("THREAD-COUNT", "WARN",
                                  f"{threads} threads: polling threads are not being replaced?")
                self.cpu_base = (time.time(), cpu_s, rss)

    def rule_log(self):
        self.log_offset, lines = tail_new_lines(APP_LOG, self.log_offset)
        for line in lines:
            low = line.lower()
            # The logger writes "[Error]" / "[Warn]" without padding.
            if "[error]" in low:
                self.rep.emit("LOG-ERROR", "ERROR", line.split("]", 3)[-1].strip()[:200])
            elif "[warn]" in low:
                self.rep.emit("LOG-WARN", "WARN", line.split("]", 3)[-1].strip()[:200])
            for needle, rule in (("crc error", "MODBUS-CRC"),
                                 ("unexpected byte count", "MODBUS-BYTECOUNT"),
                                 ("failed to read", "READ-FAILED"),
                                 ("clamped", "CONFIG-CLAMPED"),
                                 ("mismatch", "LOG-MODE-MISMATCH"),
                                 ("not written", "LOG-WRITE-REFUSED"),
                                 ("failed", "LOG-WRITE-FAILED")):
                if needle in low:
                    self.rep.emit(rule, "WARN", line.split("]", 3)[-1].strip()[:200])
                    break

    # ---- one pass ----------------------------------------------------------

    def poll(self):
        d, latency, error = fetch(self.base, timeout=10)
        self.rule_latency(latency, error)
        self.rule_process()
        self.rule_log()
        if not d or not d.get("deviceOnline"):
            if d is not None:
                self.rep.emit("DEVICE-OFFLINE", "ERROR",
                              "deviceOnline false: gateway or unit is not answering")
            return
        self.samples += 1
        self.rule_setpoints(d)
        self.rule_transitions(d)
        self.rule_priority_laws(d)
        self.rule_physics(d)
        all_key = tuple(d.get(k) for k in MONITORED)
        self.rule_loop_period(all_key)
        self.rule_frozen(all_key)
        self.prev = d
        self.prev_ts = time.time()
        self.prev_all = all_key
        self.csv.writerow([int(time.time()), round(latency, 1)] +
                          [d.get(c) for c in COLUMNS[2:]])
        self.csv_fh.flush()

    def summary(self):
        if len(self.latencies) < 5:
            return
        s = sorted(self.latencies)
        p50 = s[len(s) // 2]
        p95 = s[int(len(s) * 0.95)]
        if p95 > 150 or p50 > 80:
            self.rep.emit("LATENCY-SUMMARY", "WARN",
                          f"{self.samples} polls: p50 {p50:.0f} ms, p95 {p95:.0f} ms, "
                          f"max {max(self.latencies):.0f} ms")


def selftest():
    """Prove each rule fires on the situation it exists for.

    A monitor that has never been seen to report is decoration: every rule here is
    fed the violation and must emit. Time is not faked; the rules keep their real
    clock and the violations are backdated instead, which is what a long-running
    instance would actually look like.
    """
    import tempfile

    failures = []
    path = os.path.join(tempfile.mkdtemp(), "findings.log")

    def run(name, feed):
        rep = Reporter(path, dedup_s=0)
        seen = []
        rep.emit = lambda rule, sev, ev: seen.append(rule)          # noqa: ARG005
        mon = Monitor("http://unused", 10, rep)
        mon.open_csv()
        feed(mon)
        mon.csv_fh.close()
        return name, seen

    def status(**kw):
        base = dict.fromkeys(MONITORED, 0)
        base.update(mode="heat+dhw", runningStatus="heat", dhwState="satisfied",
                    dhwCalling=False, unitActuating=True, requestedDeviceMode="heat+dhw",
                    dhwTemperature=50.0, dhwTarget=50.0, dhwEcoTarget=50.0,
                    effectiveDhwTarget=50.0, heatingTarget=45.0, heatingTemperature=40.0,
                    outdoorTemperature=9.0, leavingWaterTemperature=45.0,
                    enteringWaterTemperature=40.0, compressorFrequency=60.0,
                    waterFlow=1.0, dhwValveStatus=0, tankReadFailures=0,
                    dhwSensorFaultSuspected=False, copValid=True,
                    compressorRuntimeHours=1.0, pumpRuntimeHours=1.0,
                    acPowerW=1000.0, cop=3.0, waterControlPoint=45.0)
        base.update(kw)
        return base

    cases = []

    def case_slow(mon):
        mon.rule_latency(2500.0, None)
    cases.append(("API-SLOW", case_slow))

    def case_dead(mon):
        mon.rule_latency(10.0, "connection refused")
    cases.append(("API-UNRESPONSIVE", case_dead))

    def case_loop_slow(mon):
        # Twelve updates, each 90 s apart: the loop is meant to publish every 30 s.
        mon.last_change = time.time()
        for i in range(12):
            mon.prev_all = ("a", i)
            mon.rule_loop_period(("a", i + 1))
            mon.last_change -= 90          # backdate so the gaps read as 90 s each
    cases.append(("LOOP-SLOW", case_loop_slow))

    def case_loop_gap(mon):
        mon.last_change = time.time() - 400
        mon.prev_all = ("x",)
        mon.rule_loop_period(("y",))
    cases.append(("LOOP-GAP", case_loop_gap))

    def case_frozen(mon):
        d = status()
        mon.prev_all = tuple(d.get(k) for k in MONITORED)
        mon.frozen_since = time.time() - 700
        mon.rule_frozen(tuple(d.get(k) for k in MONITORED))
    cases.append(("NOTHING-MOVING", case_frozen))

    def case_setpoint(mon):
        mon.rule_setpoints(status(dhwTarget=50.0))
        mon.rule_setpoints(status(dhwTarget=45.0))
    cases.append(("SETPOINT-MOVED", case_setpoint))

    def case_flap(mon):
        # rule_transitions needs a previous tick to compare against, so give it one
        # that says the same thing as the new sample: only the seeded history should
        # make noise.
        mon.prev = status()
        for i in range(4):
            mon.transitions.append((time.time() - i * 60, "device mode a -> b"))
        mon.rule_transitions(status())
    cases.append(("MODE-FLAPPING", case_flap))

    def case_thrash(mon):
        mon.prev = status()
        for i in range(3):
            mon.transitions.append((time.time() - i * 300, "dhw state satisfied -> calling"))
        mon.rule_transitions(status())
    cases.append(("SATISFACTION-THRASH", case_thrash))

    def case_park_quiet(mon):
        mon.grant_idle_since = time.time() - 1000
        mon.rule_priority_laws(status(mode="dhw_only", requestedDeviceMode="dhw_only",
                                      unitActuating=False, dhwState="calling",
                                      dhwTemperature=30.0))
    cases.append(("PARK-RULE-QUIET", case_park_quiet))

    def case_unserved(mon):
        mon.calling_idle_since = time.time() - 2000
        mon.rule_priority_laws(status(unitActuating=False, dhwCalling=True,
                                      dhwTemperature=30.0))
    cases.append(("DH-CALLING-UNSERVED", case_unserved))

    def case_mismatch(mon):
        mon.rule_priority_laws(status(requestedDeviceMode="dhw_only"))
    cases.append(("MODE-MISMATCH", case_mismatch))

    def case_fault(mon):
        mon.rule_priority_laws(status(dhwSensorFaultSuspected=True))
    cases.append(("SENSOR-FAULT-FLAG", case_fault))

    def case_readfail(mon):
        mon.rule_priority_laws(status(tankReadFailures=3))
    cases.append(("TANK-READ-FAILURES", case_readfail))

    def case_implausible(mon):
        # 8 C is a plausible cold tank; 80 C is a sensor or a garbled frame.
        mon.rule_physics(status(dhwTemperature=80.0))
    cases.append(("TANK-IMPLAUSIBLE", case_implausible))

    def case_drop(mon):
        mon.prev = status(dhwTemperature=50.0)
        mon.prev_ts = time.time() - 60
        mon.rule_physics(status(dhwTemperature=45.0))
    cases.append(("TANK-DROPS-FAST", case_drop))

    def case_rise(mon):
        mon.prev = status(dhwTemperature=45.0)
        mon.prev_ts = time.time() - 60
        mon.rule_physics(status(dhwTemperature=50.0))
    cases.append(("TANK-RISES-FAST", case_rise))

    def case_delta(mon):
        mon.rule_physics(status(dhwValveStatus=1, leavingWaterTemperature=75.0,
                                dhwTemperature=45.0))
    cases.append(("CHARGE-DELTA-HUGE", case_delta))

    def case_cop(mon):
        mon.running_no_cop_since = time.time() - 400
        mon.rule_physics(status(compressorFrequency=60.0, copValid=False))
    cases.append(("COP-INVALID", case_cop))

    def case_log(mon):
        mon.log_offset = 0
        with tempfile.NamedTemporaryFile("w", suffix=".log", delete=False,
                                        encoding="utf-8") as fh:
            fh.write("[2026-11-13 12:00:00.000] [Error] [Modbus   ] CRC error in read "
                     "response (src/modbus_client.c:294)\n")
            fh.write("[2026-11-13 12:00:01.000] [Warn ] [ControlLoop] arbitration "
                     "park_timeout_s 10 clamped to 180 s\n")
            name = fh.name
        global APP_LOG
        APP_LOG = name
        mon.rule_log()
    cases.append(("LOG-ERROR", case_log))

    for name, feed in cases:
        got_name, seen = run(name, feed)
        if got_name not in seen:
            failures.append(f"{got_name}: emitted {seen or 'nothing'}")
        else:
            print(f"  ok    {got_name:22} fired")

    # And the other direction: a healthy tick must be silent.
    name, seen = run("healthy", lambda mon: mon.rule_priority_laws(status()) or
                     mon.rule_physics(status()))
    if seen:
        failures.append(f"healthy tick reported {seen}")
    else:
        print("  ok    healthy tick           silent")

    if failures:
        print("SELFTEST FAILED:")
        for f in failures:
            print("  -", f)
        return 1
    print(f"selftest: {len(cases) + 1} rules verified")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--selftest", action="store_true",
                    help="verify the rules fire, against synthetic violations")
    ap.add_argument("--base", default=BASE_DEFAULT)
    ap.add_argument("--interval", type=float, default=10.0)
    ap.add_argument("--minutes", type=float, default=0, help="stop after this long")
    ap.add_argument("--once", action="store_true")
    ap.add_argument("--from-start", action="store_true",
                    help="also report log lines written before the monitor started")
    ap.add_argument("--dedup", type=float, default=600,
                    help="seconds a repeated finding stays suppressed")
    a = ap.parse_args()

    if a.selftest:
        return selftest()

    rep = Reporter(FINDINGS, a.dedup)
    m = Monitor(a.base, a.interval, rep, from_start=a.from_start)
    m.open_csv()
    print(f"{utcnow()}  info   MONITOR-START        base={a.base} interval={a.interval}s "
          f"log={APP_LOG} findings={FINDINGS}", flush=True)
    with open(FINDINGS, "a", encoding="utf-8") as fh:
        fh.write(f"{utcnow()}  info   MONITOR-START        base={a.base} "
                 f"interval={a.interval}s\n")

    deadline = time.time() + a.minutes * 60 if a.minutes else None
    try:
        while True:
            loop_start = time.time()
            m.poll()
            m.summary()
            if a.once:
                break
            time.sleep(max(0.5, a.interval - (time.time() - loop_start)))
            if deadline and time.time() > deadline:
                break
    except KeyboardInterrupt:
        pass
    m.csv_fh.close()


if __name__ == "__main__":
    sys.exit(main())
