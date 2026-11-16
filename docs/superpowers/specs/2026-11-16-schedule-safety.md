# Schedule safety and UI repairs

Approved: backend safety first, then UI; manual mode or temperature changes pause scheduling until explicit Resume. Preserve daily local-time operation and gaps holding the last settings, never implicitly turning the unit off.

## Isolation

Work in `.worktrees/schedule-safety`, branch `feature/schedule-safety`. Do not restart, signal, reconfigure, or probe the running controller. No live schedule POST requests. Tests use fake clients and isolated HTTP servers only. No deployment/push/merge without authorization.

## Backend

- Validate the whole candidate before changing anything: strict JSON shape/types, finite heating 25–63°C and DHW 40–63°C, modes 0–3, integer time bounds, no zero-length or overlaps including midnight wrapping. Empty disabled schedules are valid; enabling an empty schedule is rejected.
- Scheduler owns synchronized state and returns copies, never references into mutable vectors. State changes carry a revision. HTTP mutations are serialized transactions: persist a validated candidate first; publish it only on success. Invalid input or failed persistence leaves enabled/ranges/pause unchanged.
- Save enabled/ranges/paused to `~/.windmi/schedule.json` by same-directory temporary file and atomic replacement. Restore paused state on startup. All HTTP schedule changes wake the controller only after commit.
- Manual running-mode/heating/DHW commands pause an enabled schedule before actuation. A pause survives range changes, disable/re-enable and restart until Resume. If saving the manual pause fails, manual control still wins: pause in memory, log and expose a persistence warning rather than continuing automation.
- The web thread remains the command queue's only producer. The control thread applies schedule intent directly, never pushes its own commands onto the SPSC queue.
- Reapply after edits, resume/re-enable or re-entry from a gap, including temperature-only changes. Mark a range applied only after successful target writes; retry failures. Let the existing arbiter select the native device mode so DHW priority is preserved. Defer scheduled device writes during defrost/anti-freeze.
- Active schedule windows, pending application, enabled/paused state and persistence warning are reported distinctly. No range means hold current working mode/targets.

## UI

- Edits/deletes are drafts until the server accepts and persists them. On rejection retain both the authoritative range list and the editable draft; show the error.
- Deleting the last range submits an empty, disabled schedule. Enable on empty list shows a clear error.
- HTML time input 00:00 as an end means end-of-day 1440; show 1440 as 00:00 in the editor, but 24:00 in the list. Other end-before-start values represent overnight ranges. Start==end remains invalid except 00:00→24:00.
- Validate inputs before sending, but keep backend validation authoritative.
- Show explicit Resume when paused; enabling alone does not resume. Show daily/local-time and gap behavior, active vs outside-range vs pending, and persistence warnings.

## Tests

Observe failing tests before production changes. Include wrap overlap/extreme/nonfinite temperatures, empty schedules, strict parsing, durable pause and failed atomic save, coherent concurrent snapshots, HTTP transaction failures and queue emptiness, control-loop manual pause/resume/reapply/protection, UI rollback/delete/midnight/resume/states. Full build/CTest, existing UI reference checks and offline headless browser; confirm original live PID and disabled schedule unchanged.
