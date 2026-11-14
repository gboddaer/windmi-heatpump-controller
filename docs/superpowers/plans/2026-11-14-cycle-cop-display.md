# Cycle COP and power display

## Approved design

Use the user-approved, unverified AC-current raw/2 hypothesis. Show estimated input W, output W, latest completed heating-cycle COP and latest completed DHW-cycle COP after Outdoor. Always show the effective DHW setpoint and move it between DHW Tank and Heating (LWT).

A cycle is a compressor run serving heating or DHW. Integrate heat and electricity over time; never average instantaneous COP. Defrost stays in the preceding cycle and includes signed thermal energy. Discard incomplete startup runs, cycles with missing data or sampling gaps >90 s, and runs <60 s. Retain the last valid result separately for each service until restart. An idle or service-change sample completes a run. Before a complete valid run show "Awaiting cycle", not zero. Mark estimates as hypothesis-based. No historical backfill or persistence in this change.

## Files

- `include/core/CycleCop.hpp`: independently testable accumulator.
- `tests/core/test_cycle_cop.cpp`, CMake: energy-weighting, boundaries, defrost, missing data, startup/gaps.
- ControlLoop header/source: measured sample validity, signed heat, cycle integration and snapshot results.
- WebServer: nullable cycle COP fields and hypothesis metadata.
- Static HTML/JS: setpoint placement and values, input/output W, cycle COP labels.
- UI tests: mocked status rendering and DOM contracts; live HTTP/demo smoke check.

## Tasks

- [x] Write failing accumulator tests; implement and verify.
- [x] Write failing loop integration tests; wire snapshots/API and verify.
- [x] Write failing UI tests; implement layout/rendering and verify.
- [x] Full build/ctest (15/15), UI checks, live API/browser verification and review diff. Used the authorized live restart instead of a second/demo controller.
- [x] Deploy with a controlled single-client restart, preserving mode 3 and targets 50/45. Shutdown mode 5 verified, new API and Chrome render healthy. Archived old-calibration CSV; restarted API-only monitor. Completed-cycle fields await the first fully observed runs.
