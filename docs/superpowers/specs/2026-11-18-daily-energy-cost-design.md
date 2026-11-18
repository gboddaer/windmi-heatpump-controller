# Daily estimated energy and cost — design (small change)

- Date: 2026-11-18
- Status: approach picked in conversation; doc for approval
- Branch: `feature/daily-energy-cost` (worktree `.worktrees/daily-energy-cost`, from `main` `ab980b7`)

## Intent and why

The Overview already shows an estimated input power (W). The owner wants running-cost
context: how much energy the unit has consumed today (input side) and what that costs,
per day. Because the power decoding is an uncalibrated hypothesis, both new figures are
explicitly estimates.

## Goals

- Overview → Current Status shows **Energy today (est.)** (kWh) and **Cost today (est.)**
  (€/day) next to the existing power items.
- Price is entered in euro cents per kWh on the Diagnostics tab; default **30** cents.
- Cost is recalculated in the browser whenever price or kWh changes.

## Non-goals

- No calibrated metering or accuracy claim (the existing `powerCalibration` hypothesis note stays).
- No monthly/yearly sums, no time-of-use tariffs, no export/reporting.
- No control-behavior changes; no writes to `schedule.json`; no new Modbus reads or writes.

## Approaches considered

- **Daily accumulator:** (A, picked) in-memory integration in the control loop with hourly
  persistence of today's kWh — restart loses at most the last hour. (B) fully persisted
  counter with frequent writes — rejected as heavier for an estimate; (C) compute in the
  browser from power telemetry — rejected: only covers open tabs, no hourly history.
- **Cost computation:** in the browser as kWh × price (picked, owner request). Server never
  computes cost.
- **Price storage:** server-side in `~/.windmi/settings.ini` (picked) so every device shows
  the same €/day; browser-only localStorage was rejected (per-device drift).

## Decided

- The control loop integrates estimated AC input power (`acPowerW`, current hypothesis
  PF 0.90) over wall-clock time between fresh, valid samples; counts only while
  `deviceOnline` with a fresh `powerValid` sample so outages do not add energy.
- Resets at **local midnight** (same system clock the schedule uses).
- Every hour (at least once per hour while running) the controller saves
  `state.energy_today_kwh` and `state.energy_today_date` to `settings.ini`.
  On startup, the accumulator is seeded from that pair only if the date still matches
  today, otherwise it starts at 0.
- A failed energy save logs an error and keeps counting in memory; it never affects
  control or scheduling.
- Price: persisted setting `ui.price_per_kwh_cents` (euro cents, decimals allowed),
  default 30 when never set. New endpoint `POST /api/price` accepts
  `{"priceCentsPerKwh": <0–500>}`, returns 422 on invalid input, 500 with retained UI
  state on persistence failure (same patterns as the schedule endpoints).
- `/api/status` adds `dailyEnergyKwh`, `dailyEnergyValid`, `dailyEnergySavedDate` is not
  exposed, and `pricePerKwhCents`. The UI computes `cost = dailyEnergyKwh × price / 100`.
- Diagnostics keeps its read-only character; the price field is the **only** editable
  control on that tab, in a small "Energy price" card.

## Left to the builder

- Exact member names, where in the control-loop tick the integration happens, DOM ids,
  CSS classes, test file placement — follow existing conventions and TDD.
- The precise hourly-save trigger (elapsed wall time ≥ 60 min since last save).
- Settings-save wiring for the price (reuse the existing settings persistence callback).
