# DHW demand arbiter: native modes 5 and 4, and the end of the priority register

**Date:** 2026-11-11
**Unit:** Windmi 8 kW heat pump, 150 L DHW tank, behind a Modbus RTU-over-TCP gateway at `192.168.123.10:8899`, slave 11, firmware current as of this date. The heat pump heats part of the house only; a wood burner and an air-air heat pump also contribute, and the indoor temperature probe (`0x0002`) sits in an area they influence.
**Evidence:** `tools/windmi_regdump.py` (read-only sweeps), `tools/watch_changes.py` (panel-driven discovery), `tools/windmi_mode.py` + `tools/test_set_*.py` (verified single-register writes), snapshots in `regdumps/`.

## Problem

The app exposes four working modes — 0 Off, 1 DHW-only, 2 Heating-only, 3 DHW+Heating — and maps **all** of 1, 2 and 3 onto device mode `2` (`applyControlLogic`, `src/core/ControlLoop.cpp:636-644`; command handler `:560-615`). The difference between them is faked by overwriting the unit's setpoints: DHW-only pins the heating target to its 25.0 °C floor, Heating-only pins the DHW target to its 40.0 °C floor — and by writing `0x02BF` (DHW priority), which the previous author believed the firmware rejected.

The consequences are not theoretical. A pinned 25.0 °C heating target is later read back and adopted as the user's setpoint (fixed separately in `0f23b22`), and a tank that the unit will not serve while heating calls costs the household its hot water: on the evening this spec was written the tank fell from 44.7 °C to 15.2 °C and the unit declined to recharge it for three hours while the loop sat satisfied at 46.1 °C against a 45.0 °C target.

## Hardware findings (measured 2026-11-11, not inferred)

| # | Finding | How it was established |
|---|---|---|
| F1 | `0x002C` accepts and **executes** `5` = DHW-only: `0x002D 2→4`, `0x00D2 0→1` (valve closed, compressor to tank), flow `0.84→1.15 m³/h`, within 2 s | `test_set_dhw_only.py`; `regdumps/mode5-*` |
| F2 | The display controller writes `5` itself when the user picks DHW-only, passing through `4` on the way, so both GCHV modes are first-class on this unit | `watch_changes.py` during a panel walk; `0x002C 2→4→5` |
| F3 | Writing `2` returns the compressor to heating in ~7 s, valve re-opened | `test_set_heating_dhw.py`; `regdumps/mode2-*` |
| F4 | Mode `2` is heating-priority by construction: tank at 44.0 °C against a 50.0 °C setpoint, compressor stayed on space heating for the whole 153 s observation | `test_priority_latch.py`; `regdumps/latch-*` |
| F5 | `0x02BF` is readable **and writable in both directions** (FC 06 echo + FC 03 read-back), contradicting `docs/dhw_priority_report.md`, which claims `exception 0x02` | `test_set_priority_dhw.py`, `test_set_priority_heating.py` |
| F6 | Writing `0x02BF = 1` changes nothing observable: panel still said heating priority, `0x002D` stayed `2`, valve stayed open, tank flat for 153 s — including when `0x002C` was re-written immediately afterwards to force re-arbitration | `test_priority_latch.py` |
| F7 | An 8-minute panel walk moving through modes and settings changed **no** register other than `0x002C`, `0x002D`, `0x00D2`. There is no priority register behind the panel's priority indication | `watch_changes.py`; 312 change lines, `regdumps/panelwatch-end-*` |
| F8 | In mode `5` the unit charges the 150 L tank at **0.79 → 0.45 °C/min** as it warms (15.2 → 42 °C, compressor 28 → 68 Hz) — 8.3 kW at the top end, its rated output. One degree of tank = 0.174 kWh; a full 15 → 50 °C recovery is 6.1 kWh ≈ 46 min | `regdumps/tanklog`, `land`, 21:31-22:31 |
| F9 | The tank probe sits at the top of the tank and moves fast: a draw at 22:16 pulled it **41.6 → 27.2 °C in 4.5 min (3.2 °C/min)** while the compressor ran at ~50 Hz, and the earlier bath took it 44.7 → 15.2 °C | `regdumps/land-*`, `bath-check-*` |

**Conclusion.** Priority is a property of the device mode, not a setting. There is nothing to ask the unit for: the app either takes the mode register or it does not have DHW priority.

## The arbiter

`dhw_enabled` and `heating_enabled` are demands, and the device mode follows from them and the state of the tank:

| DHW | Heating | Device mode | Arbiter behaviour |
|---|---|---|---|
| on | on | `5` while the tank calls, `2` while it is satisfied | switches both ways |
| on | off | `5` | written once, then left alone |
| off | on | `4` | written once, then left alone |
| off | off | `0` | written once, then left alone |

The unit ends its own charge when the target is reached, so the arbiter never commands a stop; in the DHW-only row it has nothing further to do at all.

**Tank state** is an explicit hysteresis state machine over `dhw_tank_temp` and `dhw_target`, because a single reading at the top of the tank is not trustworthy (F9):

- `Satisfied` when `tank >= target − 0.5 °C` on **two consecutive ticks** (60 s);
- `Calling` when `tank < target − hysteresis_c`;
- otherwise the previous state is kept — that persistence *is* the hysteresis that gives heating a real window;
- `Unknown` when the register cannot be read, which holds the current device mode.

**Transitions** in the both-on row: `Satisfied` releases to `2` immediately; `Calling` grants `5`, immediately when the tank is falling fast enough to be a draw and otherwise no sooner than `min_switch_gap_s` after the last successful mode write.

### Settings (`settings.ini`, `[arbitration]`)

| Key | Default | Floor | Rationale, from the measurements |
|---|---|---|---|
| `hysteresis_c` | `5.0` | `5.0` | Width of the window in which heating may run. 5 °C of 150 L is 0.87 kWh ≈ 7 min at the F8 rate — real heating time, and wide enough that top-of-tank noise (F9) cannot cross it |
| `min_switch_gap_s` | `120` | `60` | Debounce between mode writes. A grant is a diverter-valve move, not a compressor cycle — tonight's `2→5` left the compressor running and ramping (28 → 68 Hz) — so this protects against Modbus retries and probe chatter, nothing more |
| `draw_override_c_per_min` | `1.0` | `0.5` | A tank falling at or above this rate is a hot-water draw (measured 3.2 °C/min) and re-grants regardless of the gap, for the comfort of whoever is drawing. Stratification sag is ~0.1-0.2 °C/min, an order of magnitude slower |

A value below its floor is clamped and logged, never silently honoured. The control tick stays 30 s, so the floor on comfort response is one tick, not the gap.

### Guardrails, in force in every state

1. No mode write while `0x002D` reads defrost (`7`) or anti-freeze (`20`); the decision is deferred, not lost.
2. At most one `0x002C` write per tick.
3. Every mode write is verified by reading `0x002C` back. A failed or unverified write leaves arbiter state untouched and is retried next tick.
4. `tank_temp <= 0` or an unreadable register means Unknown, which holds the mode; three consecutive read failures are flagged.
5. A mode the display controller set behind our back is logged before being corrected.
6. The release dwell never gates a release *to* heating — waiting would only punish the house.

### Sensor-fault detection, flag only

DHW priority is absolute, so a tank sensor stuck low means the house does not get its share — and that is accepted. The job is to *see* it, not to flinch. Evaluate only while genuinely charging — mode `5`, `running_status == 4`, compressor above 30 Hz, more than 5 min since the grant, not defrosting:

- tank rising ≥ 0.15 °C per 5 min → healthy (measured 0.79 → 0.45 °C/min, so a wide margin);
- tank **falling** → someone is drawing hot water: reset the timer, no fault (F9);
- tank flat for 15 min with the compressor working and nobody drawing → set `dhw_sensor_fault_suspected`, log it, expose it. The arbiter keeps priority.

There is no DHW flow register — `0x102A` is the heating loop — so the temperature-drop signature is the only draw detector available.

## Rulings

| # | Subject | Ruling |
|---|---|---|
| 1 | Charge end | The unit stops itself at target; the app only sets the target and never commands a stop. In DHW+heating it switches to `2` at `target − 0.5 °C` |
| 2 | Band | 5.0 °C, floor 5.0 |
| 3 | Switch gap | 120 s debounce, floor 60 s, overridden by a draw |
| 4 | Shutdown | Always writes device mode `2` |
| 5 | Heating-only | Native `4`, no dedicated hardware test; the new read-back verification checks it on first use |
| 6 | Both demands off | Device mode `0`; the unit keeps its own anti-freeze logic |
| 7 | Unreadable tank temp | Hold the mode; flag after three consecutive failures |
| 8 | Sensor fault | Alarm only; priority continues; house heating loss is accepted |
| 9 | Wire format | Working modes `0..3` unchanged in API, scheduler and `settings.ini`; the UI may present two enables later |
| 10 | Debug path | No `policy` setting; a `--dry-run` flag runs the whole loop with every write suppressed |
| 11 | If the app dies | No watchdog, no systemd unit — accepted risk |
| 12 | Anti-legionella / eco | Left to the unit; observe `0x00C9` (currently `2` = regular) and document |

## Non-goals

Cooling (`MODE_SET_COOL_DHW`), schedule-engine changes, and any web UI redesign. The scheduler continues to set working modes; the arbiter only decides which device mode a working mode implies.

## Review focus

1. The user changes the mode on the panel: log the mismatch, correct at most once per tick, never oscillate.
2. A real hot-water draw must re-grant within one tick, whatever the debounce says.
3. Stratification sag after a release must not start the compressor for a tank that is fine.
4. Defrost or anti-freeze mid-decision defers the write; it must not lose it or queue a stale one.
5. A write that throws or fails read-back leaves arbiter state unadvanced and retries.
6. An unreadable tank register holds the mode; it must never read as "freezing, charge now".
7. In DHW-only the app writes `5` and then stays out of the way, correcting only a panel override.

## Accepted risks

- No app process, no arbiter: priority silently reverts to the unit's heating-first behaviour. The operator override is the panel's DHW-only setting, which works because mode `5` is native (F1, F2).
- A stuck-low tank sensor starves space heating while the fault flag sits in the log and the status payload.
- Tonight's "no indoor drift during 33 minutes of diverted heating" is *not* a controlled measurement: the wood burner and the air-air unit were also running, which is precisely why indoor temperature is not an arbiter input.
