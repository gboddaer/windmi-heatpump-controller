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
- A failed energy save logs a warning, keeps counting in memory, and the next attempt is one
  hour later (the hourly cadence is kept even on failure, so an unwritable settings file does
  not warn on every 30-second tick). It never affects control or scheduling.
- Price: persisted setting `ui.price_per_kwh_cents` (euro cents, decimals allowed),
  default 30 when never set. New endpoint `POST /api/price` accepts
  `{"priceCentsPerKwh": <0–500>}`, returns 422 on invalid input, 400 on a missing field or
  empty body, and 202 `{"success":true,…}` once the change is queued — the same
  queue-and-verify pattern as every other set endpoint. Persistence happens in the control
  loop's settings callback; if that save fails the loop logs a warning, keeps the previous
  price, and the UI keeps showing the last confirmed value (the browser's pending value is
  cleared only when `/api/status` confirms it). The server never returns 500 for a deferred
  save failure, because the response is already sent by then.
- `/api/status` adds `dailyEnergyKwh`, `dailyEnergyValid` and `pricePerKwhCents`.
  `dailyEnergyValid` means "the controller holds a counter for today" (seeded from a saved
  value or integrated from a sample) — it is **not** a statement that the device is online,
  so an offline snapshot keeps showing today's total instead of dropping to zero.
  `state.energy_today_date` is persisted but not exposed. The UI computes
  `cost = dailyEnergyKwh × price / 100`.
- Diagnostics keeps its read-only character; the price field is the **only** editable
  control on that tab, in a small "Energy price" card.

## Left to the builder

- Exact member names, where in the control-loop tick the integration happens, DOM ids,
  CSS classes, test file placement — follow existing conventions and TDD.
- The precise hourly-save trigger (elapsed wall time ≥ 60 min since last save).
- Settings-save wiring for the price (reuse the existing settings persistence callback).
- The read-only Diagnostics guard lives in `tests/tools/test_ui_diagnostics.cjs` as an
  assertion that the tab has no write controls. The price card is the one agreed exception:
  update that assertion into an explicit allowlist (strip the price card, assert the rest
  stays read-only) rather than deleting it.
