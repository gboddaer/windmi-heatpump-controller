# Backup Heater Validation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add trustworthy read-only evidence for validating backup-water-heater use and reviewing minimization settings without compromising freeze protection.

**Architecture:** Acquire 0x1008 beside the existing after-heater sensor, preserve raw/timestamp validity, and compute only a signed water-based heat estimate. Read configuration through the existing bounded diagnostic round robin, display uncertainty explicitly, and log status changes using an API-only CSV tool. No device writes or heater-state/runtime claims in this validation phase.

**Tech Stack:** C++17, GoogleTest, production Mongoose HTTP, plain JS/DOM tests, Python stdlib logger/tests, offline Chrome.

Spec: ../specs/2026-11-16-backup-heater-validation.md

## File responsibilities

- `include/core/BackupHeater.hpp` (new): pure validation/heat estimate from timestamped diagnostic samples.
- `include/core/DiagnosticRegisters.hpp`, `src/core/ControlLoop.cpp`: paired acquisition and five read-only auxiliary parameters; derived freshness budget.
- `include/web/BackupHeaterJson.hpp` (new), `include/web/DiagnosticsJson.hpp`, `src/web/WebServer.cpp`: real API serialization, nulls and explicit validation-only status.
- `static/index.html`, `static/diagnostics.js`: read-only measurements, reasons and parameter review; no state-setting UI.
- `tools/log_backup_heater.py` (new): append-only status-sequence CSV evidence and gap records, no Modbus or POST.
- Tests: new core estimator tests plus real loop/HTTP and existing DOM fixtures; new logger unit tests and CMake registrations.
- `docs/backup-heater.md` (new): sources, contradictory parameter limits, safety constraints and validation procedure; update diagnostic cadence docs.

## Tasks

- [x] Isolated branch/worktree created; baseline 17/17 CTest suites passed. Live PID 396043, mode 3, online; no new backend telemetry activated.
- [x] Observed estimator missing-header RED and paired-read runtime RED. Pure estimator and fake-loop tests pass, including signed/zero heat, missing/sentinel/offline/flow/skew/age, unchanged COP/online and compressor/electrical-input zero. No new write operations.
- [x] Observed derived-budget API RED and production HTTP RED. Five parameters remain within the two-optional-read budget; freshness is derived. Actual Mongoose HTTP tests pass, including nulls/unvalidated status and an additional observed RED/GREEN regression preserving flow's 0.01 m³/h resolution.
- [x] Observed missing-card DOM RED. Backup, overview, diagnostic and schedule DOM tests now pass; activation remains Unknown even with a calculated estimate. No controls or extra renderer fetches.
- [x] Observed missing-logger RED; six stdlib logger tests now pass. GET-only endpoint construction, nullable/legacy/offline data, explicit gaps, per-instance sequence deduplication, first-sample progress uncertainty, safe append and labels are covered.
- [x] Added docs/backup-heater.md with sources, conflicting limits and no unverified anti-freeze-only claim. Fresh build, 19/19 CTest suites, UI references and diff checks pass. Offline Chrome at 1280px and 500px actual viewport rendered the card with no overflow or JS errors, two mocked GETs and zero POSTs. Local review covered source validity, water/zero/sign handling, acquisition isolation, raw settings, derived cadence, JSON precision, CSV gaps/schema and absence of actuator changes. No independent reviewer tool is available; this was not independent review.
- [x] PID 396043 remains online; sequence advanced 78→120 and working mode remains 3. Main workspace clean; no new backupHeater API object on the live server. Code remains uncommitted in the isolated worktree. No device writes, main-app deployment/restart or live logging started.

## Ledger

- User approved temperature validation, not a guessed definitive heater-state bit. Logged heat is a water-based estimate, not metered electricity or runtime.
- No verified anti-freeze-only setting was found. Backup Type=7 is not treated as safe merely because small craft heaters are separate.
- The controller manual lists booster OAT -10..0°C and warmup 0..120min, while the Modbus table lists -20..15°C and a contradictory warmup 0..60/temperature scale. Exact changes are deferred pending actual readback and firmware confirmation.
- Ruling: keep the new before-heater sensor non-critical and adjacent to T1, with <=5s source span and <=10s source age within the snapshot. Extra I/O consists of one normal sensor read and the unchanged two-read optional budget; transport's existing retries/timeouts are not a new retry loop.
- Ruling: this phase logs evidence only, including negative deltas, and never classifies ON/OFF or counts runtime/kWh. The next validation step needs deployed telemetry and independent panel item36/meter observations; no forced heater activation.
