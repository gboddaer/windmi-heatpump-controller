# Main integration and authorized deployment

User authorization: merge all local branches into main and deploy the app. No remote push requested.

- [ ] Verify and commit pending read-only backup-heater validation work, retaining unvalidated labels and no configuration writes.
- [ ] Create local main in the existing isolated Schedule worktree. Merge heater, older coding-guideline/refactor and Windows design tips; confirm every local branch tip is an ancestor. Preserve all newer control/schedule/diagnostic behavior when resolving older mechanical refactor conflicts.
- [ ] Build/test the integrated tree, run UI/reference/static checks and offline real-browser checks where applicable. Record limitations (Linux only; no independent reviewer tool).
- [ ] Capture latest live settings, schedule, status, process arguments and rollback commit; prepare matching backend/assets. Stop old controller gracefully, verify shutdown to mode 5 from its log, and ensure no second Modbus client.
- [ ] Move the main checkout to tested main only while controller is stopped, build matching binary, start one controller with existing settings. Verify API online, default migration, schedule/current intent, new heater telemetry, diagnostics/UI, and no startup/protection regressions. Re-establish GET-only monitoring.
- [ ] Record deployment evidence and final branch/working-tree state. No remote push or speculative heater parameter changes.

Safety: main checkout serves live static assets; do not change its branch before shutdown. Never start a hardware-backed test app beside the controller. Preserve current 06:30–09:00 combined 45/50 period and user pause/enabled state. Legacy defaults migrate from latest saved UI values rather than guessing an earlier baseline. Retain source worktrees/branches unless explicitly asked to delete them.
