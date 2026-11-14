# Read-only diagnostics implementation plan

Spec: ../specs/2026-11-14-read-only-diagnostics.md

- [x] Isolate worktree; initialize local dependencies; baseline build and 15/15 CTest suites.
- [x] Read the existing tab, status API, acquisition paths and original offline manual. Record approved scope and safety constraints.
- [x] Core RED: missing DiagnosticRegisters.hpp. Implemented raw capture and two-read budget, checked zero/null/high bits, ages, failed rereads, no catch-up burst and no writes. Five core tests GREEN, including real ControlLoop integration.
- [x] Production HTTP RED: diagnostics JSON absent. Added nested response and two actual Mongoose HTTP tests (including full payload). Caught and fixed the existing response's trailing newline boundary during integration; independent JSON parsing identified the malformed append before the fix. Both HTTP tests GREEN.
- [x] DOM RED: missing diagnostics.js. Added groups, four alarm decoders, conservative legacy fallback, per-register age, read-only output rendering and failure invalidation. Overview and diagnostics tests GREEN. Additional RED/GREEN checks prevent calling the first observed snapshot proof of progression and ensure electrical units use the canonical backend decoding rather than duplicating calibration in JavaScript.
- [x] Isolated build and all 16 CTest suites passed; UI reference check and whitespace check passed. Local review: validity/fallback rules, polling budget, age arithmetic, bitmaps, physical-feedback distinction, portable headers and actual HTTP response were checked. No independent reviewer tool is installed. Headless Chrome GET-only preview rendered with no JavaScript errors; POST rejected with HTTP 405. Original PID 109637 remained running; sequence advanced 57→60, and the live API still has no new diagnostics object.
- [x] Delivery remains isolated at .worktrees/read-only-diagnostics (feature/read-only-diagnostics). Temporary GET-only preview at http://localhost:8081; /tmp/wpomp-diagnostics-preview.pid holds its PID. New backend telemetry is not activated. No controller restart, device probe or device write performed.

Ruling: use nine auxiliary registers, including both required fan readings, rather than the initial approximate eight — this makes the auxiliary rotation 270 s and stale threshold 330 s. The dashboard and diagnostics.md state the exact cadence. The limit is two optional read calls per normal tick, not a hard wall-clock bound on the underlying Modbus client's existing timeout policy.

Ruling: leave the live UI and controller entirely unchanged, and provide a separate GET-only preview — this most directly satisfies the user's explicit non-disturbance requirement. The preview consumes only the existing status and schedule GET endpoints; every write method is rejected locally. Backend activation and live deployment await authorization.
