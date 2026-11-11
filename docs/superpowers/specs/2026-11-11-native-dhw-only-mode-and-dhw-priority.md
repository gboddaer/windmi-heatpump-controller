# Native DHW-only mode (0x002C = 5) and a real DHW-priority policy

**Date:** 2026-11-11
**Unit:** Windmi / Rotenso heat pump behind a Modbus RTU-over-TCP gateway at `192.168.123.10:8899`, slave 11, firmware current as of this date.
**Evidence:** `tools/windmi_regdump.py` (read-only sweeps), `tools/windmi_mode.py` + `tools/test_set_*.py` (single-register writes with verification), snapshots in `regdumps/`.

## Problem

`windmi-control` offers four working modes (0 Off, 1 DHW-only, 2 Heating-only, 3 DHW+Heating) but maps **all** of 1, 2 and 3 onto device mode `2` (`applyControlLogic`, `src/core/ControlLoop.cpp:636-644`, command handler `:560-615`). The difference between them is faked by overwriting the unit's setpoints — DHW-only pins the heating target to its 25.0 °C floor, Heating-only pins the DHW target to its 40.0 °C floor — and by writing `0x02BF` (DHW priority), which the previous author believed was blocked by the firmware.

That design damages user settings (a pinned 25.0 °C heating target is later read back and adopted as the user's setpoint — bug already fixed separately), and it does not deliver DHW when the heating circuit keeps calling.

## Hardware findings (measured 2026-11-11, not inferred)

| # | Finding | How it was established |
|---|---|---|
| F1 | `0x002C` accepts and **executes** `5` = DHW-only. `0x002D 2→4 (dhw)`, `0x00D2 0→1` (valve closed, compressor to tank), flow `0.84→1.15 m³/h`, within 2 s | `test_set_dhw_only.py`; `regdumps/mode5-*` |
| F2 | The display controller writes `5` itself when the user selects DHW-only, passing through `4` on the way | `tools/watch_changes.py` during a panel walk; `0x002C 2→4→5` at t=30s/38s |
| F3 | Writing `2` returns control to heating in ~7 s: `0x002D 4→2`, valve re-opened | `test_set_heating_dhw.py`; `regdumps/mode2-*` |
| F4 | In mode `2` the unit has **heating priority by construction**: with the tank at 44.0 °C against a 50.0 °C setpoint the compressor stayed on space heating for the whole 153 s observation | `test_priority_latch.py`; `regdumps/latch-*` |
| F5 | `0x02BF` (DHW priority) is **readable and writable both directions** (FC 06 echo + FC 03 read-back), contradicting `docs/dhw_priority_report.md`, which claims `exception 0x02` | `test_set_priority_dhw.py`, `test_set_priority_heating.py` |
| F6 | Writing `0x02BF = 1` has **no observable effect**: panel still reported heating priority, `0x002D` stayed `2`, valve stayed open, tank flat for 153 s — including when `0x002C` was re-written immediately afterwards to force re-arbitration | `test_priority_latch.py` |
| F7 | An 8-minute panel walk changing mode and settings moved **no** register other than `0x002C`, `0x002D`, `0x00D2`: there is no priority register behind the panel's priority indication | `tools/watch_changes.py`; 312 change lines, `regdumps/panelwatch-end-*` |

**Conclusion.** Priority is a property of the mode, not a setting: mode `5` serves the tank, mode `2` serves heating and lets the tank wait. A DHW-priority behaviour therefore has to be produced by **switching modes**, which is safe to do because both transitions are verified and fast (F1, F3).

## Decisions

- **D1 — DHW-only becomes native mode 5.** Working mode 1 writes `0x002C = 5` and stops pinning `0x0191` to 25.0 °C. The pin overwrites a user setpoint and is unnecessary once the mode exists.
- **D2 — Heating-only stays as it is** (device mode `2` + DHW target pinned to 40.0 °C) until mode `4` is validated on hardware the same way. Unknowns: whether mode `4` suppresses the unit's own tank logic. Groundwork for that test is a generic `tools/test_set_mode.py`.
- **D3 — DHW-priority becomes an opt-in policy on working mode 3**, implemented as mode switching with hysteresis and dwell, **defaulting to shadow mode** (evaluate and log, never write) until real logs set the timing constants.
- **D4 — Shutdown writes device mode `2`** (heat + DHW), never `0` and never `5`. `0` leaves the unit with no heating at 7 °C outdoor; `5` switches space heating off. Already implemented in `src/main.cpp`; this spec pins it with a test.
- **D5 — All writes to `0x02BF` are removed, and so is its read.** The register is accepted but inert (F5, F6), so polling it tells us nothing we act on. The `dhw_priority` field in the status snapshot and API becomes `running_mode == MODE_SET_DHW_ONLY`, which is the honest statement of who is winning right now.

## Policy definition (D3)

State lives in `ControlLoop`, evaluated once per control tick (30 s) and only while working mode is `3`:

- **Enter DHW priority (write `5`) when** `dhw_tank_temp < dhw_target - hysteresis_c`.
- **Leave DHW priority (write `2`) when** `dhw_tank_temp >= dhw_target`, or when `max_dhw_window_s` has elapsed in mode 5.
- **Otherwise hold** the current device mode.

Settings (`settings.ini`, `[dhw_priority]` section):

| Key | Default | Meaning |
|---|---|---|
| `policy` | `1` | `0` off, `1` shadow (log the decision, write nothing), `2` active |
| `hysteresis_c` | `1.0` | Threshold gap below target for entering DHW priority |
| `min_dwell_s` | `1800` | Minimum time in a device mode before another transition |
| `max_dhw_window_s` | `5400` | Maximum continuous time in mode 5, so heating is never starved |

Guardrails, in force in every policy state:

1. No transition while `0x002D` reads defrost (`7`) or anti-freeze (`20`).
2. At most one mode write per tick, and none inside `min_dwell_s` of the previous successful mode write.
3. A failed or unverified write does **not** advance policy state; the transition is retried on the next tick.
4. A missing or non-positive tank temperature is treated as unknown: hold current mode, log it.
5. A mode the display controller set behind our back is logged before being corrected.

## Non-goals

- Mode `3` (cool-only) and mode `4` (heat-only) support. Mode `4` replaces D2's pin only after a hardware test.
- Any change to the schedule engine, which sets working modes and is unaffected by which device mode a working mode resolves to.
- Web UI changes beyond the existing `priority` field becoming a read-only mirror.

## Review focus

Behaviours a user will hit that no single task's happy-path test covers:

1. **Fighting the display controller** — the user selects a mode on the panel; the app must log the mismatch, correct at most once per tick, and never oscillate.
2. **Compressor short-cycling** — tank temperature hovering around the threshold must not produce mode writes closer together than `min_dwell_s`.
3. **Defrost / anti-freeze overlap** — a policy decision during `0x002D` 7 or 20 must be deferred, not executed.
4. **Write failure mid-policy** — device offline or exception mid-transition must leave policy state unadvanced and retry, not record a transition that never happened.
5. **Unknown tank temperature** — a failed register read yielding 0 °C must not be read as "tank is freezing, charge it now".
