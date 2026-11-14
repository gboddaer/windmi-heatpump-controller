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
this polling. Alarm banks refresh every 120 seconds; the nine auxiliaries every 270
seconds. Read failures do not trigger retries in the diagnostics layer, change online
status, or change arbitration. The underlying Modbus client's normal timeout policy
still applies; this is a read-count limit, not a hard wall-clock deadline.

Each register exposes an individual sample age. Alarm samples older than 180 seconds,
auxiliary samples older than 330 seconds, and existing samples older than 90 seconds
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
