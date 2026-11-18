# Schedule defaults and persistent Overview override

Approved design: Schedule owns normal/default mode and heating/DHW targets. A defined period temporarily overrides them; gaps restore the defaults. Overview shows effective settings and source, and any explicit mode press (including the already-active mode) or target change pauses scheduling until Resume. Manual changes do not rewrite Schedule defaults. Pausing, edits and restart must preserve manual control until explicit Resume.

## Safety and delivery

Implement in `.worktrees/schedule-default-policy`, independently of the unactivated backup-heater worktree. No live schedule POST, controller restart, extra Modbus client, merge/push/activation without authorization. Existing DHW priority, protection guards, verified writes and schedule persistence guarantees remain unchanged.

## Model

- Add validated `ScheduleDefaults { mode, heatingTemp, dhwTemp }` to synchronized ScheduleState.
- Resolve enabled/unpaused policy as a matching range or the defaults. Disabled or paused means manual: retain effective working mode/targets.
- Off is only an explicitly selected mode, never the consequence of a missing period.
- An enabled policy with no ranges runs its valid default continuously. Deleting the last period retains enabled state and restores the default, rather than silently disabling automation. This follows the approved gap/default rule; UI must explain it.
- Apply on startup, resume/re-enable, range/default transition or relevant edit; restore both targets and mode. Use the existing arbiter, pending/retry/protection handling, and never feed the command queue from the consumer.
- Preserve independent defaults during all manual/scheduled actuation. Avoid redundant target writes at boundaries where effective settings do not change.
- Clicking the already-active Overview mode must still pause; avoid needless native mode writes for that unchanged manual intent.

## Persistence/API

- Persist defaults together with ranges, enabled and paused in schedule.json, using existing atomic transactions.
- Legacy files without defaults seed from existing startup UI working mode/targets; validate and use safe existing factory values if corrupt. Do not guess an earlier pre-schedule mode. The adapter loads without writing; controller startup explicitly commits the seeded defaults before automation can overwrite UI settings. A failed migration save pauses enabled scheduling until explicit Resume. This startup durability correction was identified by real-process restart tests during main integration.
- Legacy API updates omitting defaults preserve existing defaults; explicit defaults must contain all three correctly typed and bounded fields. Invalid defaults reject the entire transaction.
- Default editor uses a dedicated defaults-only POST so it cannot resubmit stale periods/enabled/pause fields from another client. Whole-policy updates remain supported.
- Expose `controlSource` (manual/default/schedule/pending) plus default values in live status, including offline metadata. Keep `scheduleActive` meaning a matching period, not default application.
- Keep runtime instance/revision handling; no client-side guessing of fallback state.

## UI

- Schedule tab has Default working mode, Default heating target, Default DHW target and Save default. Edits use drafts and authoritative replies, retain pause, and cannot silently edit ranges.
- Overview buttons remain effective/manual controls, not default selectors. Show source and explain that any mode/target change pauses scheduling.
- Resume evaluates now immediately: current period or default. No need to wait for another boundary.
- Render enabled default-only, scheduled, pending, paused and disabled distinctly. Explicitly state gap behavior. Rejected edits retain drafts and committed defaults.

## Tests

TDD: bounds/empty/default resolution, legacy migration and pause persistence, real HTTP atomic defaults/replies, fake-loop boundary/targets/restart/resume/manual same-mode/default immutability, protection/retry/Off, DOM editor/rollback/same-mode POST/source and stale-response handling. Full isolated build/CTest plus offline real-browser checks. Keep live PID and schedule unchanged.
