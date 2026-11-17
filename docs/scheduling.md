# Daily scheduling

The **Schedule** tab owns an independent **default working mode, heating target and
DHW target**. Daily periods temporarily override all three. Outside periods, the
saved default applies; gaps never imply Off. Select Off explicitly if intended.
Periods use the controller PC's local time zone, including daylight-saving changes.
Native DHW priority and protection handling remain the control loop's responsibility.

An enabled policy with no periods uses its default all day. Deleting the last period
keeps the enabled flag and defaults. Disabling scheduling stops policy application
and retains the effective settings; it does not command Off or apply a new default.

## Overview manual control and Resume

Overview shows effective settings and their source: **Schedule default**, **Scheduled
period**, or **Manual override/control**. A pending label means requested settings
have not finished applying. Offline state is unconfirmed, not proof of actuation.

Any Overview working-mode selection—including the already-selected mode—or manual
heating/DHW target change pauses an enabled schedule. Overrides never rewrite Schedule
defaults. The pause survives edits, disable/re-enable and application restart.
Only **Resume schedule** clears it, immediately selecting the current period or the
default. Saving a default or period does not Resume. Occupancy changes are not
scheduled and do not pause it. An unchanged mode press pauses without forcing a
needless native-mode round trip.

If saving a manual pause fails, automation still pauses in memory and the UI warns
that restart may lose it. Resume/configuration changes that cannot be saved leave
the previous state intact. Fix storage before relying on persistence. Atomic file
replacement does not claim immunity to storage failure or sudden power loss.

## Default and period editors

- Modes: Off, DHW only, Heating only, Heating + DHW (DHW first).
- Heating 25–63°C, DHW 40–63°C; finite numbers only, even when selecting Off.
- **Save default** edits only the default. It cannot overwrite another client's
  period list, enabled flag or manual pause. Unsaved inputs survive status polling,
  unrelated period edits and rejected saves. No hardware change occurs from typing.
- Periods are start-inclusive/end-exclusive and may cross midnight. All overlaps,
  including with the following day's early hours, are rejected.
- End **00:00 means 24:00/end of day**: 00:00–00:00 is a full day, 23:00–00:00
  the final hour, 23:00–06:00 crosses midnight. Other equal start/end times are invalid.
- Period edits/deletions remain drafts until the server confirms them; rejected edits
  preserve the confirmed list and input values. Period edits do not replace defaults.

Startup, Resume, re-enable, relevant edits and period/default boundaries select the
current policy. Target writes precede enabling a changed service. Failed target
writes remain pending and retry; explicit Off is not blocked by an unrelated target
failure. Scheduled/default writes and automatic target restoration wait for native
defrost/anti-freeze to finish. Equal effective settings across a boundary change the
reported source without redundant target writes.

## Storage and migration

`~/.windmi/schedule.json` stores enabled, paused, defaults and ranges, for example:

```json
{"enabled":true,"paused":false,"defaults":{"mode":1,"heatingTemp":40,"dhwTemp":50},"ranges":[{"start":390,"end":540,"mode":3,"heatingTemp":45,"dhwTemp":50}]}
```

Legacy files without defaults are seeded **in memory from the existing startup UI
working mode/targets**, not a guessed historical manual baseline. The old scheduler
may already have overwritten those UI values. Review the new Default editor and
explicitly save the desired normal settings. Invalid legacy fallback values use the
validated factory settings (combined, heating 40°C/DHW 50°C). Loading does not rewrite
the file; the next normal persisted change stores the new schema. Existing periods,
enabled and paused are preserved. Older files without paused restore unpaused;
omitted period temperatures retain their legacy 40°C/50°C defaults.

Candidates are validated and atomically saved using a same-directory temporary file
before publication. Runtime revisions/instance IDs are not persisted. A mutex protects
copied state. The web thread remains the SPSC command queue's sole producer; the
control loop applies schedule intent directly and keeps the existing arbiter as its
automatic native-mode selector. Already-running hardware I/O is not cancellable or
an atomic multi-register transaction.

## API

- `GET /api/schedule`: authoritative enabled, paused, defaults, ranges, revision,
  instanceId and persistenceError.
- `POST /api/schedule`: whole enabled/ranges replacement, optionally defaults. Omitted
  defaults preserve the current default under the scheduler lock. Supplied defaults
  require all three typed/bounded fields. This full replacement is last-writer-wins.
- `POST /api/schedule/defaults`: only `{"mode":1,"heatingTemp":40,"dhwTemp":50}`;
  updates defaults atomically without resubmitting cached periods/enable/pause.
- Toggle accepts only a JSON boolean or numeric 0/1. Resume alone clears manual pause.
- Mutation replies contain the authoritative state; invalid candidates return 422,
  persistence failures 500. A default save cannot implicitly Resume.
- `/api/status` includes `controlSource` (manual/default/schedule/pending),
  `scheduleDefaults`, configuration metadata and pending state. `scheduleActive`
  still means a matching period, not default application.

The browser ignores older replies from the same controller instance. After controller
restart it requires reload before editing. Simultaneous edits to the same field still
use last-writer-wins; there is no general multi-client edit-conflict resolution.
