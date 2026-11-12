# Working Modes Strategy

Application **working modes** (what the user picks) and **device modes** (what is written to
the unit) are not the same thing. The device has native modes for every demand the
application can express, so the controller picks a device mode per tick instead of
faking one with setpoints.

## Application working modes

- `0 = OFF`
- `1 = DHW only`
- `2 = Heating only`
- `3 = DHW + Heating`

## Device mode register `0x002C` (`REG_RUNNING_MODE`, RW)

| Value | Mode | Confirmed |
|---|---|---|
| 0 | Off | firmware |
| 1 | Cool + DHW | firmware |
| 2 | Heat + DHW | firmware |
| 3 | Cool only | firmware |
| 4 | Heat only | write verified 2026-11-11 (echo + read-back) |
| 5 | **DHW only** | write verified 2026-11-11: `0x002D` went to `4` (dhw), valve closed, space heating stopped, within 2 s |

Mode 5 is what the display controller writes when you select domestic hot water on the
panel. Every mode write is verified by read-back; see `docs/dhw_priority_report.md` for
how this was established and why the previous "no native DHW-only mode" claim was wrong.

## Status register `0x002D` (`REG_RUNNING_STATUS`, R)

`0 = Off`, `1 = Cool`, `2 = Heat`, `4 = DHW`, `7 = Defrost`, `20 = Anti-freeze`

The web API reports both:

- `mode` — derived from `0x002C` (what is set)
- `runningStatus` — derived from `0x002D` (what the unit is actually doing)

## Which device mode a working mode becomes

| Working mode | Device mode |
|---|---|
| 0 Off | `0` |
| 1 DHW only | `5` |
| 2 Heating only | `4` |
| 3 DHW + Heating | `5` while the tank is demanding, `2` once it is satisfied |

Only "both enabled" has anything to arbitrate. Domestic hot water has priority — that is
how this installation is heated, so the compressor goes to the tank the moment the tank
asks and back to the house when it stops asking.

### The tank's side of the bargain

- **Demand** — tank below `target - hysteresis` (default 5 °C, floor 5 °C). On 150 L that
  band is 0.87 kWh, about seven minutes of this unit, which is wider than the noise of a
  probe sitting at the top of the tank.
- **Satisfaction** — at `target - 0.5 °C` for two consecutive 30 s ticks. One tick is not
  enough: with the valve closing and the compressor still spinning, the top probe reads
  high for a minute (measured 2026-11-11: 41.4 °C and rising while the tank as a whole was
  far from full).
- **A sag inside the band is not a demand.** Stratification relaxes at roughly
  0.1-0.2 °C/min after a release; re-arming on that would thrash the diverter valve.
- **Switch gap** — a grant (2 → 5) waits 120 s (floor 60 s) after the last mode write. A
  release (5 → 2) never waits: making the house pay for a debounce that exists to protect
  the compressor is the wrong trade.
- **A draw overrides the gap.** A tank falling at or above 1.0 °C/min is somebody using
  hot water, and a bath measured 3.2 °C/min while the compressor ran at 50 Hz. Comfort
  waiting for a person beats a debounce.
- **No usable tank reading is not a demand.** It holds whatever mode the unit is in rather
  than starting a charge on every Modbus hiccup.
- **Nothing is written during defrost (7) or anti-freeze (20).** The decision is deferred,
  not lost: the next tick asks the same question.

The whole rule is `decideDeviceMode()` in `src/core/Arbitration.cpp`, which reads sensors
and returns a number. It does not write, sleep, or know about Modbus; the control loop
applies what it says. The tunables are margins only, and are clamped to the floors above:

```ini
[arbitration]
dhw_hysteresis_c = 5.0
dhw_min_switch_gap_s = 120
dhw_charge_watchdog_min = 120
dhw_draw_override_c_per_min = 1.0
```

Whether hot water has priority is not a setting.

## Charge watchdog

A DHW charge that runs its full period while the tank rises less than 0.25 °C/min — half
the slowest charge this unit has actually delivered — sets `dhwSensorFaultSuspected` and
logs one ERROR. It does not hand the compressor back to the house: a probe reading stuck
low is a fault to be found with a multimeter, and quietly deprioritising hot water would
turn a broken sensor into a cold house with no indication of why.

## Setpoints belong to the user

The controller does not write `0x0191` (heating target) or `0x0194` (DHW target) to
suppress a demand. That was the old trick — 25 °C to fake "DHW only", 40 °C to fake
"heating only" — and it overwrote user setpoints and leaked suppression floors back in as
saved targets. Native modes 4 and 5 replaced it.

## Shutdown

On exit the loop leaves the unit in `Heat + DHW` (`0x002C = 2`), never Off and never
DHW-only. Off would mean coming home to a cold house; DHW-only with no arbiter running
means the house never gets the compressor back. Whatever the unit was doing is also
recorded so it can be reported after the fact.
