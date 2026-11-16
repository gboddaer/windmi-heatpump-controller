# Schedule Safety Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Repair unsafe schedule validation, persistence, concurrency, override semantics and editor behavior without disturbing the live unit.

**Architecture:** A synchronized Scheduler publishes copied, revisioned state and persists validated candidates before publication. The web thread is the sole command producer; the control thread applies schedule intent directly through its existing arbiter. Shared strict JSON parsing and atomic file replacement serve both HTTP and startup persistence; the UI uses server-confirmed state and draft edits.

**Tech Stack:** C++17, existing Mongoose JSON/HTTP, platform mutexes, GoogleTest, plain JavaScript/DOM tests, headless Chrome.

Spec: ../specs/2026-11-16-schedule-safety.md

## File boundaries

- `include/core/Scheduler.hpp`, `src/core/Scheduler.cpp`: range validation, locked snapshots/revisions, transactional updates, persistence callback, explicit pause/resume.
- `include/utils/ScheduleConfig.hpp`, `src/utils/ScheduleConfig.cpp`: shared strict JSON conversion, durable enabled/ranges/paused state and atomic save. Reuse existing Mongoose, not a new JSON dependency.
- `include/utils/Platform.hpp`, `src/utils/Platform.cpp`: platform-correct atomic file replacement if necessary.
- `src/main.cpp`: load paused state; configure persistence callback before accepting requests.
- `src/web/WebServer.cpp`: validated transactional handlers and authoritative schedule responses, wake on successful changes; status schedule metadata.
- `include/core/ControlLoop.hpp`, `src/core/ControlLoop.cpp`: manual pause, direct schedule application, revision/range change detection, deferred protection handling and pending status.
- `static/index.html`, `static/app.js`, `static/style.css`: draft edits, valid midnight input, explicit Resume, status/error handling.
- `tests/core/test_scheduler.cpp`, `tests/core/test_control_loop.cpp`, new `tests/web/test_schedule_http.cpp`, new `tests/tools/test_ui_schedule.cjs` and CMake registrations: regressions at the actual boundaries, not copied production logic.

## Task 1 — Validated, synchronized scheduler

- [x] Isolated build configured; baseline 16/16 CTest suites passed.
- [x] Added failing scheduler safety tests; observed missing transactional/snapshot API RED. Existing offline audit had already reproduced overlap/extreme-value acceptance.
- [x] Implemented locked owned snapshots and persist-before-publish transactions. Empty schedules may be stored disabled but not enabled; existing fixtures now create valid ranges before enabling. All scheduler/config tests pass.

## Task 2 — Strict JSON and durable state

- [x] Observed parser/config API RED, then added explicit RED/GREEN checks for near-boundary float rounding and strict toggles.
- [x] Replaced hand parsing with shared Mongoose tokenization plus strict number/trailing-comma/duplicate/type validation. Added platform atomic replacement, paused restore and production persistence callback; parser/file tests GREEN.

## Task 3 — Real HTTP transactions

- [x] All six initial real HTTP schedule transaction tests failed against the previous handlers. Added production status metadata coverage as a separate observed RED.
- [x] Shared parsing and transactional handlers now pass seven real HTTP tests, including save failure rollback, pause retention, clear-last, wake count and empty command queue.

## Task 4 — Control-loop ownership and overrides

- [x] Observed integration API RED, plus runtime RED for protection-time drift restoration and missing offline metadata. Eight fake-client loop tests now cover pause/resume, edits/re-enable, gaps, failures, Off and protection.
- [x] Removed all control-thread command pushes. Schedule target writes occur directly, with native mode chosen by the existing arbiter. Added pending/configuration metadata and deferral during protection; focused and full core tests GREEN.

## Task 5 — UI regressions and repair

- [x] Observed failed-delete draft mutation RED. Additional observed RED/GREEN cases cover late GET overwriting a confirmed save and controller-instance changes.
- [x] Added draft-only edits, authoritative replies, explicit Resume, midnight mapping, validation and status/error handling. Overview, diagnostics and schedule DOM tests pass.

## Task 6 — Review and verification

- [x] Fresh isolated build, 17/17 CTest suites, UI references and diff/whitespace checks passed. Offline Chrome exercised real editor/rollback/Resume behavior with no JavaScript errors or real network writes.
- [x] Local review checked scheduler locking/ownership, callback lock ordering, two-phase file publication, manual-pause failure policy, queue ownership, protection, retry/Off semantics, strict numeric parsing, UI rollback and stale-response handling. No independent reviewer tool is available; this is not independent review.
- [x] Original PID 131132 is unchanged and online (sequence 5919), mode 1 and targets 50/45 unchanged; live schedule still disabled/unpaused/empty. Main workspace clean. At isolated completion, no commit, merge, push or activation had been performed. The user subsequently authorized committing and running the new code (not pushing).

## Ledger

- User selected manual pause until explicit Resume. This includes retaining a pause across configuration changes and restart, except an explicitly surfaced disk failure where memory pause still protects manual control.
- End-of-day input uses 00:00→1440, avoiding a new time-widget dependency.
- Ruling: automatic target drift restoration must also defer during protection — a new failing fake-client test showed three unintended writes during defrost; the protection guard now covers this existing path.
- Ruling: a target-write failure must not prevent a scheduled Off. Other service changes wait for their target writes, and failed applications remain pending for retry.
- Ruling: use a runtime controller-instance identifier alongside process-local revisions. Late same-instance GETs/status cannot undo confirmed saves; a different instance asks for a reload before editing. These runtime fields are excluded from the file.
- Ruling: reject numeric tokens over 64 characters before std::regex matching, avoiding unbounded regex recursion on malicious numeric input. Validate double bounds before float conversion so values just outside physical bounds cannot round into acceptance.
- Browser evidence: offline real HTML/JS with mocked fetch rendered end input 00:00 and submitted 1440, saved heating 44, retained a failed delete/draft, and explicitly resumed. Three mocked POSTs; no real API requests and no browser JavaScript errors.
- Additional user-reported watchdog warning investigated before deployment/reset. Captured the live flag and historical record: mode 1 selected 2026-11-14 17:45 UTC, 1,828 subsequent satisfied samples before a 2026-11-15 05:40 UTC alarm accumulated 120 minutes from a 49.2°C baseline. Mode 5 never ended, so the old watchdog combined separate charges and latched indefinitely.
- Four new watchdog regressions observed RED, then GREEN: separate satisfied charges, warning clearance while mode 1 persists, pump-only circulation, and defrost exclusion. Existing genuine-stall test still raises the warning and never relieves DHW; fixture now explicitly reports DHW running status. A separate RED/GREEN DOM test removes the diagnostic implication that low net rise proves a sensor problem. Final fresh suite remains 17/17 passing.
- Deployment preflight found no ~/.windmi/settings.ini despite current mode 1 / targets 50,45. Seed these captured live UI settings before restart so startup defaults cannot briefly enable combined heating. Keep the disabled/empty schedule unchanged.
