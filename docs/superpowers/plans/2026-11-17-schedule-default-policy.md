# Schedule Default Policy Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make schedule periods temporary overrides of explicit Schedule defaults, with persistent Overview manual override until Resume.

**Architecture:** Add independently persisted defaults to copied/locked scheduler state and a pure policy resolver. The existing control loop applies resolved settings through its arbiter and publishes the control source; the UI edits defaults on Schedule and treats Overview interactions as manual intent.

**Tech Stack:** C++17, existing Mongoose/atomic JSON storage, GoogleTest, plain JavaScript/DOM tests and offline Chrome.

Spec: ../specs/2026-11-17-schedule-default-policy.md

## Files

- Scheduler header/source: default settings, validation/transaction overload and pure selection.
- ScheduleConfig header/source and main: strict schema, legacy default detection/migration and existing persistence callback.
- ControlLoop header/source: default/range selection, source/pending metadata and same-mode manual pause.
- WebServer: transactional defaults and default/source status fields.
- app.js/index.html/style.css: default editor, effective/manual Overview labels and source, revised empty/gap behavior.
- Existing scheduler/config/HTTP/loop/DOM tests plus focused new default tests and build registrations as needed.
- docs/scheduling.md and README: new policy, migration, manual Resume and restart behavior.

## Tasks

- [x] Build isolated workspace and verify baseline; record live PID/configuration.
- [x] Add failing core tests for defaults, range/gap/empty resolution, invalid transactions and pause immutability. Observe RED; implement and verify GREEN.
- [x] Add failing strict JSON/legacy round-trip and real HTTP defaults tests. Observe RED; implement migration and API storage without default loss on older clients. Verify GREEN.
- [x] Add failing fake-loop tests for 06:30–09:00 then default, all target restoration, manual same-mode pause, paused edits/restart and resume-inside/outside, protection/retries. Observe RED; implement using the existing arbiter. Update superseded gap-policy tests explicitly, not by weakening assertions.
- [x] Add failing DOM tests for default edits/rollback, same-mode intent, source/status labels and deleting the last period to default-only policy. Observe RED; implement Schedule default form and Overview source. Preserve existing overview/diagnostic/schedule regressions.
- [x] Document, inspect safety/transaction boundaries and run full build/CTest/UI checks/offline browser. No independent reviewer tool is installed; record local review limitation.
- [x] Verify original live controller and saved policy unchanged. Report isolated implementation and pending activation; no merge, push or restart.

## Ledger

- Approved: defaults live on Schedule, not on Overview. Manual overrides persist until explicit Resume and never change defaults.
- Ruling: enabled with zero ranges uses the default at all times; deleting the last period retains enabled state. This is the empty-set case of the approved gap fallback and avoids silently abandoning the policy.
- Ruling: migration copies current persisted UI settings, not a guessed historical default that may have been overwritten by old scheduling. User can explicitly change it on the new Schedule editor; normal running code and files remain untouched during development.
- Ruling: keep this work independent of the pending backup-heater branch to avoid unintentionally activating new hardware reads.
- Local review finding: sending the default form as a whole cached policy could overwrite a second client's newer period/enabled state. Reproduced the payload coupling, then observed RED HTTP/UI isolation tests. Added a defaults-only locked transaction and strict POST `/api/schedule/defaults`; verified GREEN, retained legacy whole-policy compatibility.
- Review limitation: no independent reviewer/subagent tool is installed. Reviewed persistence transactions, migration, manual intent, source/pending/offline semantics, target retries and arbiter/protection ownership locally.
- Verification: isolated baseline 17/17 suites; final fresh build and 18/18 CTest suites passed, UI reference checker, JavaScript syntax and diff whitespace checks passed. New core tests cover defaults and restart/protection/retry/Off; real production HTTP defaults/metadata and DOM draft/rollback/stale-source tests pass.
- Browser: 13 checks each in offline Chrome at desktop 1280px and emulated mobile 390px, no JavaScript errors or horizontal overflow. Every browser request was mocked, including manual/Resume interactions. Artifacts and full final build/test log: `/var/tmp/wpomp-schedule-default-browser-20261117/`.
- Live verification: PID 396043 unchanged and online; existing enabled/unpaused 06:30–09:00 combined 45/50 schedule unchanged; no defaults field on the live API. Main workspace remains clean. No deployment, hardware probing, remote push or live mutation performed.
- Activation caution: deploy backend and static assets together. The live app serves the main checkout's static files, so merging there alone could expose new UI to the old API. Obtain coordinated activation authorization and preserve latest user settings before integration/restart.
