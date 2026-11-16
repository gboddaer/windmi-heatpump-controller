# Daily scheduling

The Schedule tab defines repeating **daily** ranges using the controller PC's local
time zone (including that system's daylight-saving transitions), not a weekly calendar.
Uncovered periods **retain the last working mode and targets**; a gap does not mean Off.
Use an explicit Off range if that is intended. Native DHW priority and protection
handling remain the control loop's responsibility.

## Manual control and Resume

Changing working mode, heating target or DHW target manually pauses an enabled
schedule. It stays paused across range edits, disable/re-enable and application
restart. Only **Resume schedule** clears the pause. Enabling a paused schedule does
not itself resume it. Occupancy changes are not scheduled, so they do not pause it.

If saving a manual pause fails, automation still pauses in memory and the UI warns
that a restart may lose the override. Resume/configuration changes that cannot be
saved fail without publishing the candidate. Fix the storage problem before relying
on persistence. The file is replaced atomically; this does not claim immunity to
storage failure or sudden power loss.

## Ranges and editor

- Heating 25–63°C, DHW 40–63°C; finite numbers only. Modes are Off, DHW only,
  Heating only, and Heating + DHW. Temperatures are validated even in Off ranges.
- Ranges are start-inclusive/end-exclusive and may cross midnight. All overlaps,
  including overlaps with the next day's early hours, are rejected.
- In the editor, end **00:00 means 24:00/end of day**. Thus 00:00–00:00 is a full day,
  23:00–00:00 is the final hour, and 23:00–06:00 crosses midnight. Lists show 24:00
  explicitly. Other identical start/end values are rejected.
- Edits/deletes remain drafts until confirmed by the server. Rejected requests do
  not alter the confirmed list, and failed edits retain their input values.
- Deleting the last range submits an empty, disabled schedule. An empty schedule
  cannot be enabled.

The status distinguishes disabled, paused, active, pending device updates, waiting
for the unit, and gaps holding the last settings. Changes/re-enabling/Resume reapply
the current range, including temperature-only edits. New target writes precede enabling
a changed service. Failed target writes remain pending and retry on later ticks;
a scheduled Off is not blocked by an unrelated target-write failure. Scheduled writes
and automatic target restoration wait for defrost/anti-freeze to finish.

## Storage and concurrency

`~/.windmi/schedule.json` stores enabled, paused and ranges. A validated candidate
is written to a same-directory temporary file and atomically replaces the prior file
before it becomes live scheduler state. Runtime revisions and instance IDs are not
persisted. Older valid files without paused restore as unpaused; omitted temperatures
use the documented safe defaults (40°C heating / 50°C DHW), but malformed supplied
temperatures are rejected.

A mutex protects scheduler state; readers receive copies. The web thread remains the
command queue's sole producer. The control thread applies schedule intent directly,
never feeds commands into its own SPSC queue, and leaves native-mode selection to
the existing arbiter. A published configuration can be applied on the next control
iteration; already-running hardware I/O is not cancellable or an atomic multi-register
transaction.

HTTP schedule updates return the authoritative enabled/paused/ranges plus revision,
instanceId and persistenceError. Bad candidates return 422; storage failures return
500. Toggle accepts only a JSON boolean or numeric 0/1. Schedule editor saves do not
clear a pause. The explicit Resume endpoint is the only API operation that does so.

The browser ignores older responses from the same controller instance. After an
application restart it asks you to reload before editing, rather than comparing new
process-local revisions with a previous process. The API uses last-writer-wins for
valid configuration edits; there is no multi-client edit-conflict resolution.
