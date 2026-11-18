"""Real production-main schedule migration/restart tests; simulated device only."""
import contextlib
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import unittest
import urllib.error
import urllib.request

BINARY = str(Path(sys.argv.pop(1)).resolve())
ROOT = Path(__file__).resolve().parents[2]
DEFAULT = {"mode": 1, "heatingTemp": 35, "dhwTemp": 48}
PERIOD = {"start": 0, "end": 1440, "mode": 3, "heatingTemp": 44, "dhwTemp": 52}


class MainScheduleStartupTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="wpomp-main-schedule-")
        self.addCleanup(self.temp.cleanup)
        self.home = Path(self.temp.name)
        self.directory = self.home / ".windmi"
        self.directory.mkdir()
        self.addCleanup(lambda: self.directory.chmod(0o700))
        self.schedule = self.directory / "schedule.json"
        self.schedule.write_text(json.dumps({"enabled": True, "paused": False, "ranges": [PERIOD]}))
        self.settings = self.home / "settings.ini"
        self.settings.write_text("ui.working_mode=1\nui.priority=dhw\nui.heating_target=35\nui.dhw_target=48\n")
        self.starts = 0

    def request(self, path, body=None):
        data = None if body is None else json.dumps(body).encode()
        request = urllib.request.Request(self.base + path, data=data, headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(request, timeout=2) as reply:
            return json.load(reply)

    def wait(self, predicate, timeout=8):
        end = time.monotonic() + timeout
        latest = None
        while time.monotonic() < end:
            if self.process.poll() is not None:
                raise AssertionError("Simulated production main exited unexpectedly")
            try:
                latest = self.request("/api/status")
                if predicate(latest):
                    return latest
            except (urllib.error.URLError, TimeoutError):
                pass
            time.sleep(0.03)
        self.fail("Timed out waiting for simulated status: " + repr(latest))

    @contextlib.contextmanager
    def running(self):
        self.starts += 1
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        self.base = "http://127.0.0.1:" + str(port)
        environment = dict(os.environ, HOME=str(self.home),
                           WINDMI_TEST_INSTANCE_LOCK=str(self.home / "isolated.lock"))
        with (self.home / ("process-" + str(self.starts) + ".log")).open("w") as log:
            self.process = subprocess.Popen(
                [BINARY, "--demo", "--web", str(port), "--config", str(self.settings),
                 "--static-dir", str(ROOT / "static")],
                env=environment, stdout=log, stderr=subprocess.STDOUT)
            try:
                self.wait(lambda status: status["deviceOnline"] and status["statusSequence"] > 0)
                yield
            finally:
                if self.process.poll() is None:
                    self.process.terminate()
                try:
                    self.process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait()
                    self.fail("Simulated production main did not shut down")
                log.flush()
                self.assertEqual(self.process.returncode, 0, Path(log.name).read_text()[-2500:])

    def test_migration_persists_before_period_rewrites_ui_and_survives_restart(self):
        with self.running():
            self.wait(lambda status: status["controlSource"] == "schedule" and status["dhwTarget"] == 52)
            self.assertEqual(self.request("/api/schedule")["defaults"], DEFAULT)
            self.assertEqual(json.loads(self.schedule.read_text()).get("defaults"), DEFAULT,
                             "Persist legacy default before automatic periods can rewrite startup UI settings")
        self.assertIn("ui.working_mode = 3", self.settings.read_text())
        timestamp = self.schedule.stat().st_mtime_ns
        with self.running():
            state = self.request("/api/schedule")
            self.assertEqual(state["defaults"], DEFAULT)
            self.assertEqual(state["ranges"], [PERIOD])
            self.assertFalse(state["paused"])
            self.assertEqual(self.schedule.stat().st_mtime_ns, timestamp,
                             "Explicit defaults must not be rewritten on every startup")
            self.request("/api/set-mode", {"mode": 3})
            self.wait(lambda status: status["schedulePaused"] and status["controlSource"] == "manual")
        with self.running():
            state = self.request("/api/schedule")
            self.assertTrue(state["paused"])
            self.assertEqual(state["defaults"], DEFAULT)
            self.assertEqual(self.request("/api/status")["controlSource"], "manual")

    @unittest.skipIf(os.geteuid() == 0, "root bypasses read-only directory permissions")
    def test_failed_startup_migration_pauses_automation_until_explicit_resume(self):
        self.directory.chmod(0o555)
        with self.running():
            state = self.request("/api/schedule")
            self.assertTrue(state["paused"], "Do not run automatic periods with an unpersisted legacy default")
            self.assertTrue(state["persistenceError"])
            self.assertEqual(state["defaults"], DEFAULT)
            self.assertEqual(self.request("/api/status")["controlSource"], "manual")
            self.assertNotIn("defaults", json.loads(self.schedule.read_text()))
            self.directory.chmod(0o700)
            self.request("/api/schedule/resume", {})
            self.wait(lambda status: status["controlSource"] == "schedule" and status["workingMode"] == 3 and status["dhwTarget"] == 52)
            self.assertEqual(json.loads(self.schedule.read_text())["defaults"], DEFAULT)


    def test_daily_energy_seed_price_default_and_price_persistence(self):
        today = time.strftime("%Y-%m-%d")
        self.settings.write_text(
            "ui.working_mode=1\nui.priority=dhw\nui.heating_target=35\nui.dhw_target=48\n"
            "state.energy_today_kwh=1.2\nstate.energy_today_date=" + today + "\n")
        with self.running():
            status = self.request("/api/status")
            self.assertEqual(30.0, status["pricePerKwhCents"], "never set: the default tariff applies")
            self.assertTrue(status["dailyEnergyValid"])
            self.assertGreaterEqual(status["dailyEnergyKwh"], 1.19, "today's saved total is restored")
            self.request("/api/price", {"priceCentsPerKwh": 25})
            self.wait(lambda s: s["pricePerKwhCents"] == 25.0)
        saved = self.settings.read_text()
        self.assertRegex(saved, r"(?m)^ui\.price_per_kwh_cents\s*=\s*25(\.0+)?$")
        with self.running():
            self.assertEqual(25.0, self.request("/api/status")["pricePerKwhCents"],
                             "the tariff survives a restart")

if __name__ == "__main__":
    unittest.main()
