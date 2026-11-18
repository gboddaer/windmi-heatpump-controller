# Main integration and authorized deployment

User authorization: merge all local branches into main and deploy the app. No remote push requested.

- [x] Verify and commit pending read-only backup-heater validation work, retaining unvalidated labels and no configuration writes.
- [x] Create local main in the existing isolated Schedule worktree. Merge heater, older coding-guideline/refactor and Windows design tips; confirm every local branch tip is an ancestor. Preserve all newer control/schedule/diagnostic behavior when resolving older mechanical refactor conflicts.
- [x] Build/test the integrated tree, run UI/reference/static checks and offline real-browser checks where applicable. Record limitations (Linux only; no independent reviewer tool).
- [x] Capture latest live settings, schedule, status, process arguments and rollback commit; prepare matching backend/assets. Stop old controller gracefully, verify shutdown to mode 5 from its log, and ensure no second Modbus client.
- [x] Move the main checkout to tested main only while controller is stopped, build matching binary, start one controller with existing settings. Verify API online, default migration, schedule/current intent, new heater telemetry, diagnostics/UI, and no startup/protection regressions. Re-establish GET-only monitoring.
- [x] Record deployment evidence and final branch/working-tree state. No remote push or speculative heater parameter changes.

Safety: main checkout serves live static assets; do not change its branch before shutdown. Never start a hardware-backed test app beside the controller. Preserve current 06:30–09:00 combined 45/50 period and user pause/enabled state. Legacy defaults migrate from latest saved UI values rather than guessing an earlier baseline. Retain source worktrees/branches unless explicitly asked to delete them.

## Integration and verification ledger

- Heater branch: verified 19/19 suites, then committed `4babb7a`. Main combines it with Schedule defaults. Kept both sets of loop/DOM tests in overlap conflicts.
- Refactor branch: 17 conflicts with newer work. Compared C/C++ tokens against the common base to distinguish mechanical member naming/formatting/guards from behavior. Applied the merged coding conventions consistently while retaining newer control implementation. Compared 94 C/C++ files to the pre-refactor feature union; remaining differences were header-guard changes, equivalent string concatenation and using-declaration ordering. Retained the newer power/COP/Diagnostics UI rather than resurrecting obsolete UI removals.
- Windows branch: retained corrected shared design counts/include references. Fetched origin without pushing; every local and remote-tracking branch tip is contained in main.
- Initial combined verification: 20/20 suites passed, including three consecutive runs. Address/UndefinedBehavior sanitizers found a pre-existing serial-constructor exception leak (440 bytes across 11 rejected constructions). Kept Impl locally RAII-owned until construction succeeds; the same sanitizer regression then passed.
- Real production-main simulation (isolated HOME, isolated test lock and forced --demo only) exposed non-durable legacy defaults: an automatic period could rewrite ui.* before defaults were stored. Added permanent real-process tests observing RED for both lost migration durability and unsafe continuation on migration save failure. Startup now explicitly commits the migration before automation; on failure enabled scheduling stays paused until Resume. The adapter's load remains read-only. Both new process tests observed GREEN.
- Test-only production-main entry refuses non-demo calls; it cannot create a hardware Modbus client. Browser checks use its actual HTTP/static backend on port 19460 or offline mocked fixtures, never production mutations.
- Final verification: 21/21 suites passed three consecutive runs; fresh final 21/21 passed normally and under AddressSanitizer + UndefinedBehaviorSanitizer with leak detection. Child production-main simulator exit codes are checked, so sanitizer failures cannot be hidden by the Python harness. UI reference/JS syntax/whitespace checks pass; monitor selftest verifies 23 rules.
- Real production-main smoke: 12 checks pass, including legacy persistence, default-only edits, same-mode manual pause, manual target independence, process restart, Resume, and an actual wall-clock period-end transition back to both default targets and mode. The real HTTP/static UI passes 7 checks at 1280px and 390px, no errors/overflow. Evidence: `/var/tmp/wpomp-main-integration-20261118/`.
- Independent reviewer/subagent tool unavailable; review is local. Linux builds and runtime tested; no Windows runtime claim.

## Deployment evidence

- Tested code commit `55982fc` activated in the main checkout. Copied the identical tested binary rather than rebuilding during downtime; runtime `/proc/<pid>/exe` SHA256 matches the tested artifact. Existing source worktrees/branches retained.
- Backup/rollback evidence: `/var/tmp/wpomp-main-integration-20261118/deployment-20261118T083327Z/` (private directory), including previous binary/assets, before/final-before settings and schedule, status, shutdown log and metadata.
- Old PID 396043 stopped gracefully; application log confirms shutdown native mode 5 written and read-back verified. Main checkout switched only after exit, so no new UI was exposed to the old API. New PID 482724 is the only controller; kernel socket inspection shows one connection to `192.168.123.10:8899` owned by it.
- Current period retained: enabled, unpaused, daily 06:30–09:00 combined at 45/50°C. Independent default migrated and persisted as combined, heating 45°C / DHW 50°C. No heater configuration writes or manual policy changes were made.
- Deployed real UI: 9 checks each at desktop 1280px and mobile 390px, zero JavaScript errors/overflow, zero attempted mutations (browser fetch restricted to GET). Reload existing browser tabs for the new UI/controller instance.
- Live post-observation: 240 seconds, 9 distinct snapshots, max observed publish gap 30.0 seconds, max API latency 1.7 ms, no errors/offline/persistence mismatches. All 41 diagnostic registers fresh; four alarm banks read zero. Native DHW operation observed under combined/default intent and DHW priority; not a claim of a completed heating/DHW cycle or proven anti-freeze activation.
- Fresh read-only heater configuration: type raw7, warmup raw60 (units unverified), deficit raw100, OAT raw65396 (signed -140), heating minimum OAT raw65276 (signed -260). Added-water-heat estimate remains unvalidated and is not proof of heater ON, electrical use or anti-freeze-only capability.
- Left 24-hour GET-only behaviour monitor PID 484269 and heater logger PID 484270 running, covering the next morning's period. CSVs `/var/tmp/wpomp_samples.csv` and `/var/tmp/wpomp_backup_heater_validation.csv`. No second Modbus client, remote push or service installation.
