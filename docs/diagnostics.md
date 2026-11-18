# Read-only diagnostics

The Diagnostics tab consumes the same status refresh as Overview. It does not issue
commands, create a Modbus connection, or add HTTP polling. It compares the requested,
set and actual operating modes without treating normal transitions as device faults.

## What is shown

- Device connection and controller snapshot progression (not temperature changes).
- DHW demand/state, actuation, mode-write age, target temperatures, schedule/occupancy.
- Compressor register, requested compressor frequency, pump command, actual/required
  fan registers, DHW routing, four-way valve/SV1/SV2 output commands and EXV command.
- Flow, supply/return temperatures, signed delta-T, electrical/heat estimates and last
  completed heating/DHW COP. Existing current-scaling/PF assumptions still apply.
- Four hardware alarm bitmaps, set-bit descriptions, IPM reason register, pressure
  switch input bits, reported runtime counters and an expandable raw register table.

## Missing, stale and uncertain values

Missing reads are null, not zero. Genuine zero is displayed. Diagnostic live values
are invalidated on offline status or a failed HTTP refresh. An unchanged snapshot
sequence for over 90 seconds is marked stale. Browser-side observation can establish
progress only after the page has opened, not how old its first snapshot already was.

Existing register readings are recorded during their normal reads; there are no duplicate
reads. At most two optional reads are added per normal 30-second tick: one rotating
alarm bank and one rotating auxiliary register. UI command kicks do not accelerate
this polling. Alarm banks refresh every 120 seconds; the fourteen auxiliaries every 420
seconds (including the read-only backup-heater review settings). Read failures do not trigger retries in the diagnostics layer, change online
status, or change arbitration. The underlying Modbus client's normal timeout policy
still applies; this is a read-count limit, not a hard wall-clock deadline.

Each register exposes an individual sample age. Alarm samples older than 180 seconds,
auxiliary samples older than 480 seconds, and existing samples older than 90 seconds
are stale. Time since the last snapshot advance is added by the browser. "No alarm
bits set" requires all four banks to have usable, fresh samples, and refers only to
those samples—not a guarantee the device is fault-free. Set reserved/unknown bits
remain visible. These banks are sampled separately, not as an atomic fault snapshot.

Actual compressor 0x0017 has contradictory manual units, so its raw value is not called
Hz. Requested compressor 0x100F is explicitly raw/10 Hz. Fan/EXV register scales are
ambiguous and remain raw. Valve outputs are commands, not physical position feedback;
pressure-switch bit polarity is not verified. The IPM reason register may be retained
or contextual and is not labelled as an active fault on its own. Runtime counters are
reported hours, not verified lifetime totals.

Older backends still display existing values where useful, qualified with "validity
not reported". Fields needing new reads and hardware alarm coverage remain unavailable.
A new backend read failure never falls back to an older zero-filled API field.

## Backup water heater validation

See [backup-heater.md](backup-heater.md) for the paired before/after temperature
estimate, GET-only CSV logger, parameter evidence and frost-safety limitations.
This is explicitly unvalidated and does not assert activation or count runtime.

## DHW progress watchdog

The progress warning concerns one unsatisfied DHW demand, not time spent in native
mode 5. Confirmed satisfaction resets its baseline/runtime and clears the warning,
even if DHW-only mode remains selected. Count only observed compressor operation
in DHW running status; pump-only circulation, heating and protection do not count.
A prolonged charge with low net tank rise can reflect hot-water consumption as well
as a sensor or charging problem: it is not proof of a faulty sensor. The legacy API
flag remains `dhwSensorFaultSuspected` for compatibility. This watchdog only reports;
it does not hand the compressor to space heating.

## Energy price

The tab holds exactly one editable control: **cost per kWh in euro cents**, in the
*Energy price* card. It is a display setting, not a telemetry reading and not a
control action.

- It turns today's estimated input kWh into the cost shown on the Overview.
- It is saved in `~/.windmi/settings.ini` (`ui.price_per_kwh_cents`), so every
  browser shows the same estimate; the default is 30 cents when it was never set.
- `POST /api/price` accepts 0–500 and returns `202` once the change is queued. A
  failed save is logged by the controller, the accepted value stays in effect for
  the session, and the browser keeps showing the last confirmed value.
- The energy total itself is an estimate built from the uncalibrated AC-power
  hypothesis (raw current / 2, power factor 0.90). Neither figure is a meter
  reading, and no billing or accuracy claim follows from them.

## Register evidence

Original installation manual, English printed pp. 50–54:
https://thermosilesia.pl/assets/default/system_object_files/43e99485889db21827eed29418c8fbec.pdf

| Register | Purpose |
| --- | --- |
| 0x0055 | Pump speed command, reported range 0–100 |
| 0x1009–0x100C | Alarm bitmaps 1–4 |
| 0x100E | ODU output bits: 0/1 fan H/L, 2 compressor heater, 3 chassis heater, 4 PTC, 5 SV1, 6 four-way valve, 7 SV2 |
| 0x100F | Required compressor frequency ×10 |
| 0x101A/B | Required upper/lower fan readings, raw |
| 0x101C | Required EXV opening, raw |
| 0x101D/E | Actual upper/lower fan readings, raw |
| 0x101F | Low/high pressure switch input bits 0/1 (polarity unknown) |
| 0x1022 | IPM protection reason |

These optional readings were verified against the document and fake-client/HTTP tests,
not probed on the running unit. Firmware support must not be inferred from the manual.
The dashboard provides no reset, test-mode, speed-setting or valve-actuation controls.
