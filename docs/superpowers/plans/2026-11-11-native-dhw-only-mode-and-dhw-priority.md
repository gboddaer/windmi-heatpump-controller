# DHW demand arbiter — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the "everything is device mode 2 plus pinned setpoints" scheme with a demand arbiter that writes the unit's native modes — `5` DHW-only, `4` heating-only, `2` when both are enabled and the tank is satisfied — and delete the inert `0x02BF` priority writes.

**Architecture:** A new `src/core/Arbitration.cpp` holds the decision as pure functions (tank-state hysteresis, mode choice, charge watchdog) with no I/O, so every rule is unit-testable. `ControlLoop` keeps the state those functions consume, does the Modbus writes with read-back verification, and publishes the arbiter's view in the status snapshot. `main.cpp` supplies `[arbitration]` settings and the `--dry-run` flag. `config.h` and a shared `ModeNames.hpp` carry the widened mode vocabulary.

**Tech Stack:** C++17, GoogleTest (FetchContent), CMake, existing `IModbusClient` seam with `FakeModbusClient` in `tests/core/test_control_loop.cpp`.

**Spec:** `docs/superpowers/specs/2026-11-11-native-dhw-only-mode-and-dhw-priority.md` — read it first. It carries findings F1–F9, the twelve rulings, and the measured numbers every constant below comes from.

## Global Constraints

- C++17 only; no new third-party dependencies (GoogleTest already arrives via FetchContent).
- Control tick stays `CONTROL_LOOP_INTERVAL_S` = 30 s in production; device access only through `IModbusClient`. Multi-tick tests are driven by `ControlLoop::kick()`, which is public and wakes the loop out of its wait (`src/core/ControlLoop.cpp:189`); `TickedLoop` in `tests/core/test_control_loop.cpp` wraps start/tick/stop. Filter the client's write log by register address in assertions.
- Every `0x002C` write is verified by reading `0x002C` back; an unverified write is a failure and must not advance arbiter state.
- At most one `0x002C` write per tick; no write while `0x002D` reads `MODE_STATUS_DEFROST` (7) or `MODE_STATUS_ANTIFREEZE` (20).
- Constants and floors, verbatim from the spec: `hysteresis_c` 5.0 (floor 5.0), `min_switch_gap_s` 120 (floor 60), `draw_override_c_per_min` 1.0 (floor 0.5), release threshold `target − 0.5 °C` confirmed on two consecutive ticks.
- `--dry-run` runs the entire loop and suppresses **all** writes, not only mode writes.
- Logging style `WINDMI_LOG_INFO/WARN/ERROR(LOG_TAG_CONTROLLOOP, ...)`.
- Build/test: `cmake --build build -j8 && ctest --test-dir build --output-on-failure`, expected `100% tests passed, 0 tests failed`. Per-suite: `./build/tests/core/test_core --gtest_filter='Suite.*'`, `./build/tests/core/test_arbitration --gtest_filter='Suite.*'`.
- Never write to the real unit from these tasks. Hardware verification afterwards uses `tools/test_set_*.py`.

## Review Focus

Each line is pinned by the named test in the named task:

1. Panel changes the mode behind our back → logged, corrected at most once per tick, no oscillation [Task 3 `ArbiterMappingTest.PanelOverrideCorrectedOnce`, Task 8 `ArbitrationExecutionTest.OneWritePerTickEvenWhenTwoRulesFire`].
2. A real hot-water draw re-grants within one tick regardless of the debounce [Task 7 `ArbitrationDecisionTest.DrawOverridesSwitchGap`].
3. Stratification sag after a release must not start the compressor for a healthy tank [Task 6 `TankStateTest.KeepsSatisfiedThroughSlowSag`, Task 7 `ArbitrationDecisionTest.SwitchGapBlocksGrant`].
4. Defrost or anti-freeze defers the write without losing or queueing it [Task 7 `ArbitrationDecisionTest.BlockedHoldsCurrentMode`, Task 8 `ArbitrationExecutionTest.WriteDeferredDuringDefrostIsRetriedAfterwards`].
5. A write that throws or fails read-back leaves arbiter state unadvanced and retries [Task 2 `ModeWriteVerificationTest.ReadbackMismatchIsAFailure`, Task 8 `ArbitrationExecutionTest.FailedWriteDoesNotAdvanceState`].
6. An unreadable tank register holds the mode and never reads as "freezing, charge now" [Task 6 `TankStateTest.UnreadableReadingIsUnknown`, Task 8 `ArbitrationExecutionTest.UnknownTankHoldsMode`].
7. In DHW-only the app writes `5` once and then stays out of the way [Task 3 `ArbiterMappingTest.DhwOnlyWritesFiveOnce`].

---

### Task 1: Widen the mode vocabulary, share the names, teach the simulator

**Files:**
- Modify: `include/config.h:54-70` and the `REG_RUNNING_MODE` comment at `:87`
- Create: `include/core/ModeNames.hpp`
- Modify: `src/web/WebServer.cpp:26-33` (drop the private static `mode_to_string`)
- Modify: `src/modbus/SimulatedModbusClient.cpp:23,148-170`
- Create: `tests/core/test_mode_names.cpp`; register in `tests/core/CMakeLists.txt`

**Interfaces:**
- Produces: `MODE_SET_COOL_ONLY` = 3, `MODE_SET_HEAT_ONLY` = 4, `MODE_SET_DHW_ONLY` = 5; `const char* windmi::mode_to_string(int mode)` → `"off"`, `"cool+dhw"`, `"heat+dhw"`, `"cool"`, `"heat"`, `"dhw_only"`, `"unknown"`.

- [ ] **Step 1: Write the failing test** `tests/core/test_mode_names.cpp`: `ModeNamesTest.NativeGchwModesHaveNames` asserting `"dhw_only"`, `"heat"`, `"cool"` for 5, 4, 3; `ModeNamesTest.ExistingNamesUnchanged` asserting `"off"`, `"cool+dhw"`, `"heat+dhw"` for 0, 1, 2 and `"unknown"` for 9.
- [ ] **Step 2: Run it, expect failure** — `cmake --build build -j8 && ./build/tests/core/test_core --gtest_filter='ModeNamesTest.*'`; expect `mode_to_string` undeclared.
- [ ] **Step 3: Implement.** Three defines beside the existing `MODE_SET_*` block; replace the comment `Only modes 0, 1, 2 should be SET` with the measured fact (spec F2: the display controller writes 3/4/5 on this unit). `ModeNames.hpp` holds `inline const char* windmi::mode_to_string(int)` over all seven cases. `WebServer.cpp` includes it and deletes its static copy. In `SimulatedModbusClient`, `MODE_SET_DHW_ONLY` drives leaving water toward `REG_DHW_TARGET` and sets `REG_RUNNING_STATUS = MODE_STATUS_DHW`; `MODE_SET_HEAT_ONLY` drives toward `REG_HEATING_TARGET` and sets `MODE_STATUS_HEAT`; the `// Only 0, 1, 2 allowed` comment at `:23` goes.
- [ ] **Step 4: Run the full suite** — expect green.
- [ ] **Step 5: Commit** — `git commit -m "Add native GCHV device modes 3/4/5 to the mode vocabulary"`

---

### Task 2: Pin the mode-write path and build the tick-driven test harness

**Files:**
- Modify: `src/core/ControlLoop.cpp:230-247` (`setRunningMode`)
- Test: `tests/core/test_control_loop.cpp`

**Interfaces:**
- Consumes: `MODE_SET_*` from Task 1.
- Produces: `int ControlLoop::setRunningMode(int mode)` — returns 0 **only** when the value written to `0x002C` reads back equal; returns -1 for out-of-range (`< 0 || > 5`), on exception, and on read-back mismatch.

`setRunningMode` is private and there is no public mode getter, so these tests are black-box through the command queue and the client's write log: the observable consequence of an unverified write is that the loop still believes the mode is unset and **tries again on the next tick**.

- [ ] **Step 1: Write the failing tests** — extend `ModeSwitchFixture` (Task 3 renames it; for now add a sibling runner) with a `runTicks(int working_mode, int ticks, IModbusClient&)` variant, then:
  - `ModeWriteVerificationTest.HonestClientWritesModeOnce` — a normal `FakeModbusClient`, two ticks, `countWrites(client, REG_RUNNING_MODE) == 1` (no retry storm).
  - `ModeWriteVerificationTest.LyingClientIsRetried` — a `LyingModeClient : FakeModbusClient` that records writes but always returns `MODE_SET_HEAT_DHW` from `readRegister(REG_RUNNING_MODE)` while mode 5 is requested; over three driven ticks expect `countWrites(client, REG_RUNNING_MODE) >= 2`, i.e. the mismatch did not count as success. Ticks are driven with `CMD_SET_OCCUPANCY_MODE` as described in Global Constraints.
  - `ModeWriteVerificationTest.ThrowingClientDoesNotEscape` — a client whose `writeRegister(REG_RUNNING_MODE, ...)` throws `ModbusException`; the loop survives the tick and the suite continues (assert on a following successful write of another register, not on an exception).
  The `< 0 || > 5` range guard is defence in depth and is not reachable through the public API (working modes are 0..3); it is pinned by the free-function mapping tests in Task 3 rather than by a direct call here.
- [ ] **Step 2: Run** — expect failure: the current range check rejects 5 and there is no read-back.
- [ ] **Step 3: Implement** — nothing in `src/` for this task: the deliverable is `TickedLoop`, `LyingModeClient` and `ThrowingModeClient` in the test file plus the three pins. Task 3 does the production change.
- [ ] **Step 4: Run** — expect green.
- [ ] **Step 5: Commit** — `git commit -am "Verify mode writes by reading 0x002C back and accept native modes"`

---

### Task 3: Map working modes onto native device modes

**Files:**
- Modify: `include/core/Arbitration.hpp` (new header, `deviceModeForWorkingMode`), `src/core/Arbitration.cpp` (new), `src/core/ControlLoop.cpp:560-615` (command handler), `:636-690` (`applyControlLogic`)
- Create: `tests/core/test_arbitration.cpp`, registered in `tests/core/CMakeLists.txt`
- Test: also `tests/core/test_control_loop.cpp`

**Interfaces:**
- Consumes: `MODE_SET_OFF/HEAT_DHW/HEAT_ONLY/DHW_ONLY`.
- Produces: `int deviceModeForWorkingMode(int working_mode)` — `0→MODE_SET_OFF`, `1→MODE_SET_DHW_ONLY`, `2→MODE_SET_HEAT_ONLY`, `3→MODE_SET_HEAT_DHW`, default `MODE_SET_HEAT_DHW`. The both-on refinement (tank satisfied or calling) arrives in Task 7; until then mode 3 means `2`.

- [ ] **Step 1: Write the failing tests** — in `test_arbitration.cpp`: `ArbitrationMappingTest.WorkingModesMapToNativeDeviceModes`, four `EXPECT_EQ` rows over `deviceModeForWorkingMode`, including `default` behaviour for 7. In `test_control_loop.cpp`, rename `SavedTargetFixture` to `ModeSwitchFixture` (same body; update its three existing users), add helpers `wrote(client, address)` and `countWrites(client, address)` beside the existing `wroteValue`, then write `ArbiterMappingTest.DhwOnlyWritesFiveOnce` — working mode 1 with `REG_RUNNING_MODE = 2` and `REG_HEATING_TARGET = 450` expects `wroteValue(client, REG_RUNNING_MODE, MODE_SET_DHW_ONLY)`, and `EXPECT_FALSE(wrote(client, REG_HEATING_TARGET))` with the message *"native mode 5 must not overwrite the user's heating setpoint"* — and `ArbiterMappingTest.HeatingOnlyWritesFourAndStopsPinning` — working mode 2 expects `MODE_SET_HEAT_ONLY` and no write of `REG_DHW_TARGET` at 400 — and `ArbiterMappingTest.PanelOverrideCorrectedOnce` — `REG_RUNNING_MODE` reading `MODE_SET_HEAT_DHW` while working mode 1 is requested expects `countWrites(client, REG_RUNNING_MODE) == 1`: one corrective write, no fight with the panel.
- [ ] **Step 2: Run** — expect failure (`deviceModeForWorkingMode` missing; mode 2 written; setpoints pinned).
- [ ] **Step 3: Implement** — add the function; route the command handler and `applyControlLogic` through it; delete the `case 1` heating-target pin and the `case 2` DHW-target pin in both the handler and the enforcement switch. Keep the `case 3` restore-saved-targets branch untouched. Log a panel mismatch (`current_mode_` differing from the device) at `WINDMI_LOG_WARN` before correcting, and correct at most once per tick.
- [ ] **Step 4: Run** — `ctest --test-dir build --output-on-failure`; green, including the three `SavedTargetTest` cases.
- [ ] **Step 5: Commit** — `git commit -am "Arbitrate working modes onto native device modes 5, 4 and 2"`

---

### Task 4: Delete the 0x02BF writes; derive the priority field

**Files:**
- Modify: `src/core/ControlLoop.cpp` — writes at `:536`, `:587`, `:599`, `:612`, `:695`, `:707`; the read at `:348-349`
- Modify: `include/config.h:84`
- Test: `tests/core/test_control_loop.cpp`

**Interfaces:**
- Produces: `StatusSnapshot::dhw_priority` == `(running_mode == MODE_SET_DHW_ONLY)`. `PriorityMode` and the API `priority` field survive as internal/UI state only.

- [ ] **Step 1: Write the failing tests** — `PriorityFieldTest.ReportsDhwPriorityFromDeviceMode5` (registers with `REG_RUNNING_MODE = MODE_SET_DHW_ONLY` and `REG_DHW_PRIORITY = 0`; expects `dhw_priority == true`, message *"priority is a property of the mode (F4/F7), not of 0x02BF"*) and `PriorityFieldTest.NeverWritesDhwPriorityRegister` (working mode 3; `EXPECT_FALSE(wrote(client, REG_DHW_PRIORITY))`). Add the one-line `readStatus(registers)` helper: start a `ControlLoop` on `FakeModbusClient(registers)`, take one snapshot, stop, return it.
- [ ] **Step 2: Run** — expect both to fail.
- [ ] **Step 3: Implement** — remove the six writes and their try/catch blocks; in `readStatus` replace the `0x02BF` read with the mode comparison and drop the register from the polled set. Update the `REG_DHW_PRIORITY` comment: readable and writable, no observed effect on arbitration (F5/F6), neither written nor polled.
- [ ] **Step 4: Run** — expect green.
- [ ] **Step 5: Commit** — `git commit -am "Stop writing the inert DHW priority register; derive priority from device mode"`

---

### Task 5: Pin the shutdown mode

**Files:**
- Modify: `include/core/ControlLoop.hpp:208-255`, `src/core/ControlLoop.cpp`, `src/main.cpp:354`
- Test: `tests/core/test_control_loop.cpp`

**Interfaces:**
- Produces: `static int ControlLoop::shutdownDeviceMode()` → `MODE_SET_HEAT_DHW`, used by `main.cpp` in place of the literal.

- [ ] **Step 1: Write the failing test** — `ShutdownModeTest.ShutdownAlwaysLeavesHeatingAndDhwAvailable`: `EXPECT_EQ(MODE_SET_HEAT_DHW, ...)` plus `EXPECT_NE(MODE_SET_OFF, ...)` (mode 0 leaves the unit unheated) and `EXPECT_NE(MODE_SET_DHW_ONLY, ...)` (mode 5 switches space heating off).
- [ ] **Step 2: Run** — expect compile failure.
- [ ] **Step 3: Implement** — one-line accessor citing ruling 4; `main.cpp` calls it.
- [ ] **Step 4: Run** — expect green.
- [ ] **Step 5: Commit** — `git commit -am "Pin the shutdown device mode behind a tested accessor"`

---

### Task 6: Tank state as an explicit hysteresis machine

**Files:**
- Modify: `include/core/Arbitration.hpp`, `src/core/Arbitration.cpp`
- Test: `tests/core/test_arbitration.cpp`

**Interfaces:**
- Produces:

```cpp
enum class TankState : uint8_t { Unknown, Calling, SatisfiedCandidate, Satisfied };
struct ArbitrationConfig {
    float hysteresis_c = 5.0f;            // floor 5.0
    int min_switch_gap_s = 120;           // floor 60
    float draw_override_c_per_min = 1.0f; // floor 0.5
    void applyFloors();
};
struct TankReading {
    float temp_c;
    float target_c;
    bool valid;            // false when the register could not be read
    int ticks_at_target;   // consecutive ticks reading >= target - 0.5
};
static TankState advanceTankState(TankState prev, const ArbitrationConfig& cfg,
                                  const TankReading& r);
```

`SatisfiedCandidate` means the reading has reached the release threshold but not yet for two consecutive ticks; it is not satisfied and Task 7 must not act on it.

- [ ] **Step 1: Write the failing tests** — target 50.0 throughout, `cfg()` returning a default-constructed `ArbitrationConfig`: `TankStateTest.DeepDropIsCalling` (44.9, `prev = Satisfied` → `Calling`), `TankStateTest.ReachingTargetOnceIsOnlyACandidate` (49.6, `ticks_at_target = 1`, `prev = Calling` → `SatisfiedCandidate`), `TankStateTest.TwoTicksAtTargetSatisfy` (49.6, `ticks_at_target = 2` → `Satisfied`), `TankStateTest.InsideTheBandKeepsPreviousState` (47.0 with `prev = Satisfied` → `Satisfied`; 47.0 with `prev = Calling` → `Calling`), `TankStateTest.KeepsSatisfiedThroughSlowSag` (48.4, `prev = Satisfied`, `ticks_at_target = 0` → `Satisfied` — a sag below 50.0 must not undo satisfaction), `TankStateTest.UnreadableReadingIsUnknown` (`valid = false`, `prev = Satisfied` → `Unknown`), `TankStateTest.NonPositiveTemperatureIsUnknown` (`temp_c = 0.0f` with `valid = true` → `Unknown`), `TankStateTest.FloorsClampUndershootingConfig` (`{2.0f, 10, 0.1f}` through `applyFloors()` → 5.0, 60, 0.5).
- [ ] **Step 2: Run** — `cmake --build build -j8 && ./build/tests/core/test_arbitration --gtest_filter='TankStateTest.*'`; expect compile failure.
- [ ] **Step 3: Implement** — the order inside `advanceTankState` is: invalid or non-positive → `Unknown`; `temp >= target − 0.5` → `ticks_at_target >= 2 ? Satisfied : SatisfiedCandidate`; `temp < target − hysteresis_c` → `Calling`; else `prev`. Add `SatisfiedCandidate` to the enum and note in the header comment that a candidate is not yet satisfied.
- [ ] **Step 4: Run** — expect green.
- [ ] **Step 5: Commit** — `git commit -am "Add the tank hysteresis state machine with a two-tick satisfaction rule"`

---

### Task 7: The mode decision, as a pure function

**Files:**
- Modify: `include/core/Arbitration.hpp`, `src/core/Arbitration.cpp`
- Test: `tests/core/test_arbitration.cpp`

**Interfaces:**
- Consumes: `TankState`, `ArbitrationConfig`, `deviceModeForWorkingMode` (Task 3).
- Produces:

```cpp
struct ArbiterInput {
    int working_mode;            // 0..3
    TankState tank;
    int current_mode;            // device mode we believe is active
    bool tank_drawing;           // falling at/above draw_override_c_per_min
    bool blocked;                // defrost or anti-freeze
    int seconds_since_last_write;
};
static int decideDeviceMode(const ArbitrationConfig& cfg, const ArbiterInput& in);
```

- [ ] **Step 1: Write the failing tests** — `ArbitrationDecisionTest`: `StaticRowsIgnoreTankState` (working 1 → 5 and working 2 → 4 with tank `Calling`, `Satisfied` and `Unknown` alike), `BothOnSatisfiedTankUsesHeatingMode` (tank `Satisfied` → 2), `BothOnCallingTankUsesDhwMode` (tank `Calling`, gap elapsed → 5), `BothOnKeepsCurrentModeWhileCandidate` (tank `SatisfiedCandidate` → `current_mode`), `SwitchGapBlocksGrant` (tank `Calling`, 60 s since last write, not drawing → unchanged `current_mode`), `SwitchGapDoesNotBlockRelease` (current 5, tank `Satisfied`, 0 s since last write → 2), `DrawOverridesSwitchGap` (tank `Calling`, 0 s, `tank_drawing` → 5), `BlockedHoldsCurrentMode` (`blocked`, tank `Calling`, gap elapsed → `current_mode`), `UnknownTankHoldsMode` (tank `Unknown` → `current_mode`), `WorkingModeOffMapsToOff` (0 → 0).
- [ ] **Step 2: Run** — expect compile failure.
- [ ] **Step 3: Implement** — order: `blocked` → `current_mode`; working 0/1/2 → `deviceModeForWorkingMode`; working 3 with tank `Unknown` or `SatisfiedCandidate` → `current_mode`; tank `Satisfied` → `MODE_SET_HEAT_DHW`; tank `Calling` → `MODE_SET_DHW_ONLY` when `tank_drawing || seconds_since_last_write >= min_switch_gap_s`, else `current_mode`.
- [ ] **Step 4: Run** — expect green.
- [ ] **Step 5: Commit** — `git commit -am "Add the pure device-mode decision with band, gap and draw override"`

---

### Task 8: Run the arbiter in the loop, plumb settings, add --dry-run and status

**Files:**
- Modify: `include/core/ControlLoop.hpp`, `src/core/ControlLoop.cpp`, `include/core/ControlLoop.hpp` (`StatusSnapshot`), `src/web/WebServer.cpp` (status JSON), `src/main.cpp` (settings + flag; arg parsing near `:109-130`)
- Test: `tests/core/test_control_loop.cpp`, plus `tests/web/test_web_server_api.cpp` if the status payload is asserted there

**Interfaces:**
- Consumes: `decideDeviceMode`, `advanceTankState`, `ArbitrationConfig`, `setRunningMode` (Task 2).
- Produces: `void ControlLoop::setArbitration(const ArbitrationConfig&)` (calls `applyFloors()` and logs the effective values), `void ControlLoop::setDryRun(bool)`; new `StatusSnapshot` fields `bool dhw_calling`, `bool dhw_state_known`, `int requested_device_mode`, `int seconds_since_mode_write`, `bool dhw_sensor_fault_suspected`, `int tank_read_failures`; private state `ArbitrationConfig arbitration_; TankState tank_state_ = TankState::Unknown; int ticks_at_target_ = 0; int tank_read_failures_ = 0; uint64_t last_mode_write_ms_ = 0; bool dry_run_ = false;`.

- [ ] **Step 1: Write the failing tests** — `ArbitrationExecutionTest`: `BothOnCyclesWithTheTank` (drive ticks with `REG_DHW_TANK_TEMP` at 44.0 then 49.6 twice then 44.0 → writes `5`, then `2`, then `5`), `OneWritePerTickEvenWhenTwoRulesFire`, `UnknownTankHoldsMode` (make `REG_DHW_TANK_TEMP` unreadable → no mode write, `tank_read_failures` increments), `FailedWriteDoesNotAdvanceState` (a client whose `writeRegister(REG_RUNNING_MODE, 5)` throws once: `current_mode_` unchanged, `last_mode_write_ms_` not stamped, the same decision retried next tick), `WriteDeferredDuringDefrostIsRetriedAfterwards` (`REG_RUNNING_STATUS = 7` for two ticks then `2`: no write during defrost, one write afterwards), `DryRunSuppressesEveryWrite` (`setDryRun(true)`; `client.writes()` empty while status still reports `requested_device_mode == 5`), `DrawOverridesSwitchGapInLoop`, `StatusExposesArbitration` (snapshot fields populated; if `test_web_server_api.cpp` asserts on the payload, add `"dhw_calling"` there).
- [ ] **Step 2: Run** — expect failure.
- [ ] **Step 3: Implement.** Once per tick, before the write: derive `tank_drawing` from the change since the previous tick against `draw_override_c_per_min`; advance `tank_state_` (incrementing `ticks_at_target_` when the reading is at/above `target − 0.5`, resetting it otherwise, and counting consecutive read failures into `tank_read_failures_`, which resets on success); call `decideDeviceMode`; if the answer differs from `current_mode_`, log at `WINDMI_LOG_INFO` the tank temp, target, state, the reason (`draw override`, `gap`, `blocked`, `panel mismatch`) and the seconds since the last write, then write through `setRunningMode` unless `dry_run_`. Stamp `last_mode_write_ms_` **only** on a verified success, and stamp it for every other mode write too (command handler, shutdown) so the gap always measures reality. Populate the new `StatusSnapshot` fields in `readStatus`, serialise them in the web status payload, read `[arbitration]` keys in `main.cpp` with the spec defaults, and wire `--dry-run` into the existing argument parser plus the `--help` text.
- [ ] **Step 4: Run** — `ctest --test-dir build --output-on-failure`; expect green.
- [ ] **Step 5: Commit** — `git commit -am "Run the DHW arbiter every tick with settings, dry-run and status"`

---

### Task 9: The charge watchdog, as a flag only

**Files:**
- Modify: `include/core/Arbitration.hpp`, `src/core/Arbitration.cpp`, `src/core/ControlLoop.cpp`
- Test: `tests/core/test_arbitration.cpp`, `tests/core/test_control_loop.cpp`

**Interfaces:**
- Consumes: mode, `running_status`, compressor frequency `0x0017`, tank temperature history, `tank_drawing`.
- Produces:

```cpp
enum class ChargeVerdict : uint8_t { Idle, Healthy, Drawing, Suspect };
static ChargeVerdict advanceChargeWatchdog(ChargeVerdict prev, bool charging,
                                           bool blocked, float compressor_hz,
                                           int seconds_since_grant,
                                           float delta_c_per_5min);
```

`ControlLoop` keeps `ChargeVerdict charge_watchdog_ = ChargeVerdict::Idle;` and publishes it as `StatusSnapshot::dhw_sensor_fault_suspected = (charge_watchdog_ == ChargeVerdict::Suspect)`. It changes **no** mode decision.

- [ ] **Step 1: Write the failing tests** — `ChargeWatchdogTest.NotChargingIsIdle` (mode 2 → `Idle`), `ChargeWatchdogTest.FirstFiveMinutesAreNotJudged` (`seconds_since_grant = 240` → `Idle`), `ChargeWatchdogTest.RisingIsHealthy` (`delta = +0.6`), `ChargeWatchdogTest.FallingIsDrawingNotFault` (`delta = -15.0`, the F9 bath case → `Drawing`), `ChargeWatchdogTest.FlatWhileCompressorWorksIsSuspect` (`charging`, 600 s, 45 Hz, `delta = 0.0` → `Suspect`), `ChargeWatchdogTest.LowCompressorFrequencyIsNotJudged` (20 Hz → `Idle`), `ChargeWatchdogTest.DefrostIsNotJudged` (`blocked` → `Idle`), `ChargeWatchdogTest.RecoveryClearsSuspect` (`prev = Suspect`, `delta = +0.6` → `Healthy`). Plus `ArbitrationExecutionTest.SuspectSensorDoesNotChangeMode`: with a tank register pinned flat and the compressor high, the loop keeps requesting `5` and sets the flag.
- [ ] **Step 2: Run** — expect compile failure.
- [ ] **Step 3: Implement** — return `Idle` unless `charging && !blocked && compressor_hz >= 30.0 && seconds_since_grant >= 300`; then `delta < -0.3` → `Drawing`, `delta >= +0.15` → `Healthy`, else `prev == Suspect ? Suspect : (seconds_since_grant >= 900 ? Suspect : Idle)`. Log the transition into and out of `Suspect` at `WINDMI_LOG_WARN`, stating that DHW priority is deliberately maintained.
- [ ] **Step 4: Run** — expect green.
- [ ] **Step 5: Commit** — `git commit -am "Flag a DHW sensor that will not respond, without giving up priority"`

---

### Task 10: Documentation, README, and the mode-4 probe tool

**Files:**
- Create: `tools/test_set_mode.py`
- Modify: `docs/dhw_priority_report.md`, `docs/learned.md`, `README.md`

**Interfaces:**
- Consumes: `run_reg_test` from `tools/windmi_mode.py`.
- Produces: `tools/test_set_mode.py --mode N` (0..5) for any future hardware question, and documentation that matches the code.

- [ ] **Step 1: Write the tool** — thin wrapper mirroring `tools/test_set_dhw_only.py`: `run_reg_test(REG_SETTING_MODE, mode, description, watch_set_name="mode", tag="mode%d" % mode)`, `--mode` restricted to 0..5 with descriptions from `MODES`.
- [ ] **Step 2: Verify it** — `python3 -m py_compile tools/test_set_mode.py && ./tools/test_set_mode.py --help`; then against the simulated gateway (`FAKE_DENIED=0x0209 python3 /tmp/fake_gateway.py /tmp/map.json 18899`) run `--mode 4 --watch 2` and expect exit 0 with a verified read-back. No real hardware.
- [ ] **Step 3: Correct the priority docs** — in `docs/dhw_priority_report.md`, replace "writes fail with `exception 0x02`" with the measured result (writes succeed and are ignored, F5/F6; priority is a property of the mode, F4/F7; the workaround was setpoint pinning) and point to the arbiter spec. In `docs/learned.md`, retire the stale claims that `0x02BF` and `0x0209` are unreadable — both read on the current firmware — and name `tools/windmi_regdump.py`, `tools/watch_changes.py` and `tools/test_set_*.py` as the way to re-derive any of it.
- [ ] **Step 4: Align the README** — status payload key is `"temperature"`, not `"target"`; self-test flag is `-s`/`--selftest`, not `-t`; demo gateway listens on 10000; GoogleTest comes via FetchContent, not a submodule; document the working-mode table with native device modes 5/4/2/0, the `[arbitration]` settings with their defaults and floors, `--dry-run`, `dhw_sensor_fault_suspected`, and the operator note that when the app is not running the unit reverts to heating-first and the panel's DHW-only is the manual override.
- [ ] **Step 5: Verify and commit** — `ctest --test-dir build --output-on-failure` green, then `git add tools/test_set_mode.py docs README.md && git commit -m "Document the arbiter and align the README with the code"`
