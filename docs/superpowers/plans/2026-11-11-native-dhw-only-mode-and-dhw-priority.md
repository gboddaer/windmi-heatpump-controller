# Native DHW-only mode (0x002C = 5) and DHW-priority policy — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make working mode 1 use the unit's native DHW-only mode `0x002C = 5` instead of pinning the heating setpoint, remove the inert `0x02BF` priority writes, and add a shadow-by-default policy that switches between mode 2 and mode 5 to give domestic hot water priority.

**Architecture:** All changes live in the control layer. `include/config.h` gains the device-mode vocabulary; a new `include/core/ModeNames.hpp` makes mode names unit-testable; `ControlLoop` gains a pure decision function (`decidePriorityAction`) plus a small amount of transition state, and `main.cpp` supplies policy settings from `settings.ini`. The Modbus layer is untouched apart from accepting the wider mode range.

**Tech Stack:** C++17, GoogleTest (FetchContent), CMake, existing `IModbusClient` seam with `FakeModbusClient` in `tests/core/test_control_loop.cpp`.

**Spec:** `docs/superpowers/specs/2026-11-11-native-dhw-only-mode-and-dhw-priority.md` — read it first; it carries the hardware measurements (F1–F7) and decisions (D1–D5) that every number below comes from.

## Global Constraints

- C++17 only, no new third-party dependencies (GoogleTest already comes in via FetchContent).
- Control tick stays `CONTROL_LOOP_INTERVAL_S` = 30 s; device access only through `IModbusClient`.
- Every device mode write must be verified by reading `0x002C` back; an unverified write is a failure, not a success.
- At most one `0x002C` write per tick; no transition while `0x002D` reads `MODE_STATUS_DEFROST` (7) or `MODE_STATUS_ANTIFREEZE` (20).
- Policy default is `policy = 1` (shadow): evaluate and log, **write nothing**.
- Logging style: `WINDMI_LOG_INFO/WARN/ERROR(LOG_TAG_CONTROLLOOP, ...)`.
- Build and test loop: `cmake --build build -j8 && ctest --test-dir build --output-on-failure`, expected tail `100% tests passed, 0 tests failed out of 14` (suite count grows if a task adds a suite). Single suite: `./build/tests/core/test_core --gtest_filter='Suite.*'`.
- Branch: `feature/native-mode5-dhw-priority`. Never write to the real unit from these tasks; hardware validation happens afterwards with `tools/test_set_*.py`.

## Review Focus

Each line is pinned by a named test in the task named in brackets:

1. The user changes the mode on the display controller: the app logs the mismatch and corrects at most once per tick, never oscillating [Task 2 `DhwOnlyModeTest.CorrectsForeignDeviceModeExactlyOnce`, Task 6 `PolicyExecutionTest.PanelOverrideCausesAtMostOneWritePerTick`].
2. Tank temperature hovering on the threshold must not produce mode writes closer than `min_dwell_s` [Task 5 `PriorityPolicyTest.DwellBlocksReEntry`, Task 6 `PolicyExecutionTest.DwellBlocksSecondWriteInSameWindow`].
3. Defrost (`0x002D = 7`) and anti-freeze (`20`) block a transition that the temperature alone would call for [Task 5 `PriorityPolicyTest.DefrostAndAntifreezeBlockTransition`].
4. A write that throws or fails read-back must leave policy state unadvanced and be retried next tick [Task 6 `PolicyExecutionTest.FailedWriteDoesNotRecordTransition`].
5. A failed tank-temperature read (0 °C / unknown) must hold the current mode instead of starting a charge [Task 5 `PriorityPolicyTest.UnknownTankTempHoldsMode`].

---

### Task 1: Device-mode vocabulary, names and simulator support

**Files:**
- Modify: `include/config.h:54-70` (mode defines and the stale comment above them, plus the `REG_RUNNING_MODE` comment at `:87`)
- Create: `include/core/ModeNames.hpp`
- Modify: `src/web/WebServer.cpp:26-33` (delete the local `mode_to_string`, use the shared one)
- Modify: `src/modbus/SimulatedModbusClient.cpp:153-170` (mode 5 behaves like DHW)
- Create: `tests/core/test_mode_names.cpp`, register it in `tests/core/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `MODE_SET_COOL_ONLY` = 3, `MODE_SET_HEAT_ONLY` = 4, `MODE_SET_DHW_ONLY` = 5; `const char* windmi::mode_to_string(int mode)` in `include/core/ModeNames.hpp`, returning `"off"`, `"cool+dhw"`, `"heat+dhw"`, `"cool"`, `"heat"`, `"dhw_only"`, `"unknown"`.

- [ ] **Step 1: Write the failing test** — `tests/core/test_mode_names.cpp`, following the include style of `tests/core/test_control_loop.cpp`:

```cpp
TEST(ModeNamesTest, NativeGchwModesHaveNames) {
    EXPECT_STREQ("dhw_only", windmi::mode_to_string(MODE_SET_DHW_ONLY));
    EXPECT_STREQ("heat",     windmi::mode_to_string(MODE_SET_HEAT_ONLY));
    EXPECT_STREQ("cool",     windmi::mode_to_string(MODE_SET_COOL_ONLY));
}
TEST(ModeNamesTest, ExistingNamesUnchanged) {
    EXPECT_STREQ("off",      windmi::mode_to_string(MODE_SET_OFF));
    EXPECT_STREQ("cool+dhw", windmi::mode_to_string(MODE_SET_COOL_DHW));
    EXPECT_STREQ("heat+dhw", windmi::mode_to_string(MODE_SET_HEAT_DHW));
    EXPECT_STREQ("unknown",  windmi::mode_to_string(9));
}
```

- [ ] **Step 2: Run it and watch it fail to compile or run** — `cmake --build build -j8 && ./build/tests/core/test_core --gtest_filter='ModeNamesTest.*'`; expect failure on `windmi::mode_to_string` being undeclared.
- [ ] **Step 3: Implement.** Add the three defines next to the existing `MODE_SET_*` block and replace the comment above it (`Only modes 0, 1, 2 should be SET`) with one naming the measured facts: modes 3, 4, 5 are marked "GCHV only" in the vendor manual and are written by the display controller on this unit (spec F2). `ModeNames.hpp` holds an `inline const char* mode_to_string(int mode)` switch covering all seven cases above, in namespace `windmi`. `WebServer.cpp` deletes its static copy and includes the header. In `SimulatedModbusClient`, mode `MODE_SET_DHW_ONLY` sets `REG_RUNNING_STATUS = MODE_STATUS_DHW` and drives leaving water toward `REG_DHW_TARGET`; the existing comment `// Only 0, 1, 2 allowed` at `:23` and the range check in the mode setter go with it.
- [ ] **Step 4: Run the tests** — `cmake --build build -j8 && ctest --test-dir build --output-on-failure`; expect `100% tests passed, 0 tests failed`.
- [ ] **Step 5: Commit** — `git add include/config.h include/core/ModeNames.hpp src/web/WebServer.cpp src/modbus/SimulatedModbusClient.cpp tests/core/ && git commit -m "Add native GCHV device modes 3/4/5 to the mode vocabulary"`

---

### Task 2: Working mode 1 uses native DHW-only mode 5

**Files:**
- Modify: `src/core/ControlLoop.cpp:230-247` (`setRunningMode` range check and verification), `:560-590` (command handler), `:636-690` (`applyControlLogic`)
- Test: `tests/core/test_control_loop.cpp`

**Interfaces:**
- Consumes: `MODE_SET_DHW_ONLY` from Task 1.
- Produces: working mode 1 resolves to device mode 5. `int setRunningMode(int mode)` now returns 0 only when the written value reads back unchanged. Private helper `static int deviceModeForWorkingMode(int working_mode, bool dhw_priority_active)` mapping 0→`MODE_SET_OFF`, 1→`MODE_SET_DHW_ONLY`, 2→`MODE_SET_HEAT_DHW`, 3→`device_mode_param`, default→`MODE_SET_HEAT_DHW`, where `device_mode_param` is `MODE_SET_DHW_ONLY` when the policy is active and `MODE_SET_HEAT_DHW` otherwise (Task 6 passes it).

- [ ] **Step 1: Write the failing tests** — reuse `fullRegisters()`, `SavedTargetFixture` and `wroteValue(client, REG, raw)` from `tests/core/test_control_loop.cpp`:

```cpp
TEST(DhwOnlyModeTest, WritesNativeMode5AndNeverPinsHeatingTarget) {
    auto registers = fullRegisters();
    registers[REG_RUNNING_MODE] = 2;              // unit sits in heat+dhw
    registers[REG_HEATING_TARGET] = 450;          // 45.0 C, the user's value
    ModeSwitchFixture f(registers);
    f.run(1, "dhw", 50.0f, 45.0f);                // working mode 1 = DHW-only
    EXPECT_TRUE(wroteValue(f.client, REG_RUNNING_MODE, MODE_SET_DHW_ONLY));
    EXPECT_FALSE(wroteValue(f.client, REG_HEATING_TARGET, 250))
        << "native mode 5 must not overwrite the user's heating setpoint";
    EXPECT_FALSE(wrote(f.client, REG_DHW_PRIORITY)) << "spec D5";
}

TEST(DhwOnlyModeTest, CorrectsForeignDeviceModeExactlyOnce) {
    auto registers = fullRegisters();
    registers[REG_RUNNING_MODE] = MODE_SET_HEAT_DHW;   // panel set heat+dhw
    ModeSwitchFixture f(registers);
    f.run(1, "dhw", 50.0f, 45.0f);
    EXPECT_EQ(countWrites(f.client, REG_RUNNING_MODE), 1u)
        << "one corrective write per tick at most, never a fight with the panel";
}
```

`ModeSwitchFixture` is `SavedTargetFixture` renamed and kept general (same body); rename it and update its three existing `SavedTargetTest` uses in the same commit. `countWrites(client, address)` is a new helper next to `wroteValue`, returning how many recorded writes hit that address.
- [ ] **Step 2: Run** — `cmake --build build -j8 && ./build/tests/core/test_core --gtest_filter='DhwOnlyModeTest.*'`; expect failure: mode 2 is written and `0x0191` is pinned to 250.
- [ ] **Step 3: Implement.** In `setRunningMode`, accept `mode` 0..5 and reject anything else; after `writeRegister`, read `REG_RUNNING_MODE` and return -1 with a `WINDMI_LOG_ERROR` when it differs from what was written (spec guardrail: an unverified write is a failure). In the command handler, case 1 sets `target_device_mode = MODE_SET_DHW_ONLY` and its override block reduces to logging — no `setHeatingTarget(HEATING_TARGET_MIN)`, no priority write. In `applyControlLogic`, resolve the desired device mode through `deviceModeForWorkingMode(desired_working_mode_, false)` and delete the `case 1` pinning branch that forces `HEATING_TARGET_MIN`. Keep the `case 2` (heating-only) pin exactly as is — spec D2 defers mode 4 to a hardware test.
- [ ] **Step 4: Run** — `ctest --test-dir build --output-on-failure`; expect the full suite green, including the three `SavedTargetTest` cases.
- [ ] **Step 5: Commit** — `git add src/core/ControlLoop.cpp tests/core/test_control_loop.cpp && git commit -m "Use native DHW-only mode 5 instead of pinning the heating setpoint"`

---

### Task 3: Remove the inert 0x02BF writes; report priority from device mode

**Files:**
- Modify: `src/core/ControlLoop.cpp` — writes at `:536`, `:587`, `:599`, `:612`, `:695`, `:707`; the read at `:348-349`; `readStatus` polling
- Modify: `include/config.h:84` (`REG_DHW_PRIORITY` comment)
- Test: `tests/core/test_control_loop.cpp`

**Interfaces:**
- Consumes: `MODE_SET_DHW_ONLY` from Task 1.
- Produces: `StatusSnapshot::dhw_priority` is `true` when `running_mode == MODE_SET_DHW_ONLY`. `PriorityMode` and the `priority` field of the API and `setInitialSettings` remain, but drive no device write.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST(PriorityFieldTest, ReportsDhwPriorityFromDeviceMode5) {
    auto registers = fullRegisters();
    registers[REG_RUNNING_MODE] = MODE_SET_DHW_ONLY;
    registers[REG_DHW_PRIORITY] = 0;              // the device's inert flag says otherwise
    EXPECT_TRUE(readStatus(registers).dhw_priority)
        << "priority is a property of the mode (spec F4/F7), not of 0x02BF";
}
TEST(PriorityFieldTest, NeverWritesDhwPriorityRegister) {
    ModeSwitchFixture f(fullRegisters());
    f.run(3, "dhw", 50.0f, 45.0f);
    EXPECT_FALSE(wrote(f.client, REG_DHW_PRIORITY)) << "writable but inert (spec F5/F6)";
}
```

`readStatus(registers)` is a new one-line helper in the test file: build a `ControlLoop`, `start()` it on a `FakeModbusClient(registers)`, take one snapshot from the status queue, `stop()`, return the snapshot. `wrote(client, address)` is a sibling of the existing `wroteValue`.
- [ ] **Step 2: Run** — `./build/tests/core/test_core --gtest_filter='PriorityFieldTest.*'`; expect both to fail (register written; priority read from `0x02BF`).
- [ ] **Step 3: Implement.** Delete the six `writeRegister(REG_DHW_PRIORITY, ...)` calls and their try/catch blocks; in `readStatus` replace the `0x02BF` read with `status.dhw_priority = (status.running_mode == MODE_SET_DHW_ONLY)` and drop the register from the polled set. `current_priority_` stays as internal state for the API's `priority` field. Update the `REG_DHW_PRIORITY` comment in `config.h` to: readable and writable on this firmware, no observed effect on arbitration (spec F5/F6), not written and not polled.
- [ ] **Step 4: Run** — `ctest --test-dir build --output-on-failure`; expect green.
- [ ] **Step 5: Commit** — `git commit -am "Stop writing the DHW priority register; report priority from device mode"`

---

### Task 4: Pin the shutdown mode

**Files:**
- Modify: `include/core/ControlLoop.hpp:208-255` (public section), `src/core/ControlLoop.cpp`, `src/main.cpp:354`
- Test: `tests/core/test_control_loop.cpp`

**Interfaces:**
- Consumes: `MODE_SET_*` from Task 1.
- Produces: `static int ControlLoop::shutdownDeviceMode()`, returning the device mode written on shutdown. Used by `main.cpp` in place of the literal `MODE_SET_HEAT_DHW`.

- [ ] **Step 1: Write the failing test**

```cpp
TEST(ShutdownModeTest, ShutdownKeepsHeatingAndDhwAvailable) {
    EXPECT_EQ(MODE_SET_HEAT_DHW, ControlLoop::shutdownDeviceMode());
    EXPECT_NE(MODE_SET_OFF, ControlLoop::shutdownDeviceMode())
        << "mode 0 leaves the unit unheated (spec D4)";
    EXPECT_NE(MODE_SET_DHW_ONLY, ControlLoop::shutdownDeviceMode())
        << "mode 5 switches space heating off";
}
```

- [ ] **Step 2: Run** — expect compile failure on `shutdownDeviceMode` not being a member.
- [ ] **Step 3: Implement** — `static int shutdownDeviceMode() { return MODE_SET_HEAT_DHW; }` in the public section with a one-line comment citing spec D4; in `src/main.cpp` the shutdown write becomes `shutdown_client->writeRegister(REG_RUNNING_MODE, windmi::ControlLoop::shutdownDeviceMode())`.
- [ ] **Step 4: Run** — `ctest --test-dir build --output-on-failure`; expect green.
- [ ] **Step 5: Commit** — `git commit -am "Pin shutdown device mode behind a tested accessor"`

---

### Task 5: The policy decision, as a pure function

**Files:**
- Modify: `include/core/ControlLoop.hpp` (add `DhwPriorityPolicy`, `PriorityAction`, static `decidePriorityAction`)
- Modify: `src/core/ControlLoop.cpp` (implementation)
- Test: `tests/core/test_priority_policy.cpp`, registered in `tests/core/CMakeLists.txt`

**Interfaces:**
- Consumes: `MODE_SET_DHW_ONLY`, `MODE_SET_HEAT_DHW`, `MODE_STATUS_DEFROST`, `MODE_STATUS_ANTIFREEZE`.
- Produces:

```cpp
struct DhwPriorityPolicy {
    int policy = 1;            // 0 off, 1 shadow, 2 active
    float hysteresis_c = 1.0f;
    int min_dwell_s = 1800;
    int max_dhw_window_s = 5400;
    bool enabled() const { return policy != 0; }
    bool active() const { return policy == 2; }
};
enum class PriorityAction : uint8_t { Hold, EnterDhwOnly, LeaveDhwOnly };
static PriorityAction decidePriorityAction(const DhwPriorityPolicy& cfg,
                                           int device_mode, float tank_temp_c,
                                           float dhw_target_c, int seconds_in_mode,
                                           int running_status);
```

`seconds_in_mode` is time since the last successful mode write; `tank_temp_c <= 0` means unknown. Task 6 owns the state that feeds these arguments.

- [ ] **Step 1: Write the failing tests** — one per rule, exact numbers from the spec table:

```cpp
using A = ControlLoop::PriorityAction;
static ControlLoop::DhwPriorityPolicy cfg() { return {}; }

TEST(PriorityPolicyTest, ColdTankEntersDhwOnly)      // mode 2, tank 48.9 < 50.0-1.0
TEST(PriorityPolicyTest, SatisfiedTankLeavesDhwOnly) // mode 5, tank 50.0 >= target -> LeaveDhwOnly
TEST(PriorityPolicyTest, InsideHysteresisHolds)      // mode 2, tank 49.5 -> Hold
TEST(PriorityPolicyTest, DwellBlocksReEntry)         // mode 2, tank 40.0, seconds_in_mode 1799 -> Hold
TEST(PriorityPolicyTest, MaxWindowEndsDhwCharge)     // mode 5, tank 45.0, seconds_in_mode 5400 -> LeaveDhwOnly
TEST(PriorityPolicyTest, DefrostAndAntifreezeBlockTransition)  // tank 40.0, status 7 and 20 -> Hold
TEST(PriorityPolicyTest, UnknownTankTempHoldsMode)   // tank 0.0 -> Hold in both modes
TEST(PriorityPolicyTest, DisabledPolicyAlwaysHolds)  // policy = 0, tank 30.0 -> Hold
```

Each is a two-to-four line `EXPECT_EQ` over `decidePriorityAction`, written out in full, not table-driven.
- [ ] **Step 2: Run** — `cmake --build build -j8 && ./build/tests/core/test_priority_policy --gtest_filter='PriorityPolicyTest.*'`; expect compile failure.
- [ ] **Step 3: Implement** — order of checks in `decidePriorityAction`: disabled → unknown tank temp → defrost/anti-freeze → dwell → mode-specific threshold and max window. Hold is the fallthrough, so an unrecognised device mode cannot trigger a write.
- [ ] **Step 4: Run** — expect the eight tests to pass; `ctest --test-dir build --output-on-failure` green.
- [ ] **Step 5: Commit** — `git commit -am "Add pure DHW-priority transition decision with dwell, window and safety blocks"`

---

### Task 6: Execute or shadow the decision, and plumb the settings

**Files:**
- Modify: `include/core/ControlLoop.hpp` (setter, state members)
- Modify: `src/core/ControlLoop.cpp` (`applyControlLogic`, constructor initialisation)
- Modify: `src/main.cpp:267-277` (read `[dhw_priority]` keys, call the setter)
- Test: `tests/core/test_control_loop.cpp`

**Interfaces:**
- Consumes: `decidePriorityAction`, `DhwPriorityPolicy`, `PriorityAction`, `deviceModeForWorkingMode` (Task 2).
- Produces: `void setPriorityPolicy(const DhwPriorityPolicy&)`; private state `DhwPriorityPolicy priority_policy_; uint64_t last_mode_change_ms_ = 0;`. `deviceModeForWorkingMode(desired_working_mode_, in_dhw_priority)` is called with `in_dhw_priority = (current_mode_ == MODE_SET_DHW_ONLY)`.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST(PolicyExecutionTest, ShadowPolicyLogsButNeverWritesMode)   // policy=1, tank 35.0, working mode 3
TEST(PolicyExecutionTest, ActivePolicyEntersDhwOnlyOnce)        // policy=2, tank 35.0 -> exactly one write of 5
TEST(PolicyExecutionTest, DwellBlocksSecondWriteInSameWindow)   // policy=2, two ticks 30 s apart -> still one write
TEST(PolicyExecutionTest, FailedWriteDoesNotRecordTransition)   // throwing client: state unadvanced, retried next tick
TEST(PolicyExecutionTest, PanelOverrideCausesAtMostOneWritePerTick)
```

Build them on `ModeSwitchFixture` extended with a `run(ticks, policy, working_mode, tank_temp, ...)` that drives N control ticks and returns `client.writes()`. `FailingModeClient` is a `FakeModbusClient` subclass whose `writeRegister(REG_RUNNING_MODE, ...)` throws `ModbusException("device offline")` on the first call only.
- [ ] **Step 2: Run** — expect failure: no setter, no writes.
- [ ] **Step 3: Implement.** In `applyControlLogic`, after the setpoint-enforcement switch and while `desired_working_mode_ == 3`: compute `seconds_in_mode` from `last_mode_change_ms_`, call `decidePriorityAction`, then — for `EnterDhwOnly` / `LeaveDhwOnly` — log the decision and the numbers behind it in both shadow and active mode, and write only when `priority_policy_.active()`. A write returning non-zero leaves `current_mode_` and `last_mode_change_ms_` untouched; success sets `current_mode_`, stamps `last_mode_change_ms_`, and logs `"DHW priority %s: tank %.1f C vs target %.1f C, %d s in mode %d"`. Reset `last_mode_change_ms_` whenever a mode write happens for any other reason, so the dwell always measures the real gap. In `main.cpp`, read `dhw_priority.policy`, `dhw_priority.hysteresis_c`, `dhw_priority.min_dwell_s`, `dhw_priority.max_dhw_window_s` through `windmi::Config` with the spec defaults and call `setPriorityPolicy` before `start()`.
- [ ] **Step 4: Run** — `ctest --test-dir build --output-on-failure`; expect green.
- [ ] **Step 5: Commit** — `git commit -am "Evaluate the DHW-priority policy every tick, shadow by default"`

---

### Task 7: Documentation, README, and groundwork for the mode-4 test

**Files:**
- Create: `tools/test_set_mode.py`
- Modify: `docs/dhw_priority_report.md`, `docs/learned.md`, `README.md`

**Interfaces:**
- Consumes: `run_reg_test` from `tools/windmi_mode.py`.
- Produces: `tools/test_set_mode.py --mode N` for hardware probing of device modes 3 and 4 later (spec D2), and documentation that matches the code.

- [ ] **Step 1: Write the tool** — thin wrapper, mirroring `tools/test_set_dhw_only.py`, calling `run_reg_test(REG_SETTING_MODE, mode, description, watch_set_name="mode", tag="mode%d" % mode)` with a `--mode` argument restricted to 0..5 and a description table matching `MODES` in `windmi_mode.py`.
- [ ] **Step 2: Verify it** — `python3 -m py_compile tools/test_set_mode.py && ./tools/test_set_mode.py --help`; expect the usage block listing `--mode`. Then against the simulated gateway used earlier (`FAKE_DENIED=0x0209 python3 /tmp/fake_gateway.py …`), run `--mode 4 --watch 2` and expect exit 0 with a verified read-back. No real hardware.
- [ ] **Step 3: Correct the docs.** In `docs/dhw_priority_report.md`, replace the claim that writes to `0x02BF` fail with `exception 0x02` by the measured result: writes succeed and are ignored (spec F5/F6), priority is a property of the mode (F4/F7), and the workaround was the setpoint pin. In `docs/learned.md`, retire the stale statements that `0x02BF` and `0x0209` return exceptions — both read on the current firmware. Add a section naming `tools/windmi_regdump.py`, `tools/watch_changes.py` and the `test_set_*` scripts as the way to re-derive any of this.
- [ ] **Step 4: Align the README** with the code: status payload key is `"temperature"`, not `"target"`; the self-test flag is `-s`/`--selftest`, not `-t`; the demo gateway listens on 10000; GoogleTest arrives via FetchContent, not a submodule; document working mode 1 as native device mode 5, the `[dhw_priority]` settings block with `policy` defaulting to shadow, and `priority` in the API as a read-only mirror of the device mode.
- [ ] **Step 5: Verify and commit** — `ctest --test-dir build --output-on-failure` green, then `git add tools/test_set_mode.py docs README.md && git commit -m "Correct the priority documentation and align README with the code"`.
