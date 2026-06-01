# Test Plan & Fix Implementation Plan

## Review Notes (2026-05-29)

This document was reviewed against the actual master branch source code.
Several **critical issues** were found that were **not identified in the original plan**,
and some claims in the plan are **incorrect or incomplete**.

---

## Overview

The C++ conversion has **broken functionality**. The C implementation (`src/main.c`,
`src/control_loop.c`) is production-ready with proper Modbus communication, control
logic, and shutdown sequence. The C++ version (`src/main.cpp`, `src/core/ControlLoop.cpp`)
is incomplete and non-functional.

---

## Root Causes Identified

### 1. Missing Register Definitions in `include/config.h`

The C++ config is missing many register definitions present in master branch.

**Missing Register Definitions**:
- `REG_DEVICE_TYPE` (0x1006) - Device type identifier
- `REG_OUTDOOR_TEMP` (0x0001) - Outdoor temperature (0.1°C)
- `REG_INDOOR_TEMP` (0x0002) - Indoor temperature (0.1°C)
- `REG_ENTERING_WATER_TEMP` (0x0003) - Entering water temperature (0.1°C)
- `REG_LEAVING_WATER_TEMP` (0x0004) - Leaving water temperature (0.1°C)
- `REG_WATER_CONTROL_POINT` (0x0033) - Water control point status
- `REG_DHW_MODE_STATUS` (0x00C9) - DHW mode: 0=Eco, 1=Anti Legionella, 2=Regular
- `REG_DHW_PRIORITY` (0x02BF) - DHW priority flag (0=Automatic, 1=DHW priority)
- `REG_OCCUPANCY_MODE` (0x0029) - Occupancy mode: 0=Away, 1=Sleep, 2=Home
- `REG_DHW_TANK_TEMP` (0x00CE) - DHW tank temperature (0.1°C)
- `REG_HEATING_TARGET` (0x0191) - Heating target temperature (0.1°C)
- `REG_DHW_TARGET` (0x0194) - DHW target temperature (0.1°C)

**⚠ NEW — Fabricated Register Definitions in C++ config.h**:
The C++ config.h defines `REG_DHW_TEMP` (0x0012) and `REG_HEATING_TEMP` (0x0014)
which **do not exist** in the master branch. These are **fabricated** register addresses
that don't correspond to the real device protocol. The correct registers are:
- `REG_DHW_TANK_TEMP` (0x00CE) — DHW tank temperature (read-only)
- `REG_DHW_TARGET` (0x0194) — DHW target temperature (read/write)
- `REG_HEATING_TARGET` (0x0191) — Heating target temperature (read/write)

**Missing Mode Constants**:
- `MODE_SET_OFF` (0), `MODE_SET_COOL_DHW` (1), `MODE_SET_HEAT_DHW` (2)
- `MODE_STATUS_OFF` (0), `MODE_STATUS_COOL` (1), `MODE_STATUS_HEAT` (2)
- `MODE_STATUS_DHW` (4), `MODE_STATUS_DEFROST` (7), `MODE_STATUS_ANTIFREEZE` (20)

**⚠ NEW — Wrong Mode Constants in C++ config.h**:
The C++ config.h defines:
```cpp
#define MODE_DHW_ONLY     1
#define MODE_HEATING_ONLY 2
#define MODE_DHW_HEATING  3
```
These do not match the device protocol. The device only supports modes 0, 1, 2:
- 0 = Off
- 1 = Cool+DHW
- 2 = Heat+DHW

The C++ "working mode" values (0=Off, 1=DHW-only, 2=Heating-only, 3=DHW+Heating) are
**application-level** abstractions that map to device modes via the control loop logic.
The master branch correctly separates `MODE_SET_*` (device-level) from
`desired_working_mode` (application-level). The C++ config conflates them and
uses wrong values (1,2,3) that overlap with device mode values but mean different things.

**Missing Configuration**:
- `SELFTEST_DHW_TARGET_TEMP` (45.0f)
- `MODBUS_GATEWAY_IP` ("192.168.123.10")
- `MODBUS_GATEWAY_PORT` (8899)
- `MODBUS_SLAVE_ID` (11)
- `MODBUS_TIMEOUT_MS` (1000)
- `MODBUS_MAX_RETRIES` (3)
- `MODBUS_RECONNECT_INTERVAL_S` (10)
- `MODBUS_POLL_INTERVAL_S` (30)
- `CONTROL_LOOP_INTERVAL_S` (30)
- `DHW_HYSTERESIS_C` (3.0f)
- `HEATING_HYSTERESIS_C` (1.0f)
- `WEB_SERVER_PORT` (8080)

### 2. Incomplete Control Loop Implementation

**Current C++ Status**:
- `readStatus()` only reads 9 registers vs master's 14 register reads
- **⚠ NEW — `readStatus()` reads WRONG registers**: Uses fabricated addresses
  0x0012 and 0x0014 instead of real device addresses 0x00CE, 0x0191, 0x0194
- **⚠ NEW — `readStatus()` does not read DHW target or heating target registers**
  (master reads REG_DHW_TARGET 0x0194 and REG_HEATING_TARGET 0x0191)
- No `applyControlLogic()` implementation matching master's logic (see §2a below)
- No command queue processing (commands are just logged, not sent to Modbus)
- No priority switching logic (DHW vs Heating priority)
- No target temperature management (no saved/restored targets)
- No hysteresis-based mode switching
- No working mode state tracking
- **⚠ NEW — `applyControlLogic()` writes to WRONG registers**: Writes DHW target
  to 0x0012 and heating target to 0x0014, which are **read-only temperature registers**,
  not the writable target registers (0x0194 and 0x0191). This could corrupt device state.
- **⚠ NEW — Wrong poll interval**: C++ uses 500ms sleep, master uses 30-second
  interval (CONTROL_LOOP_INTERVAL_S=30). The 500ms interval would hammer the device
  with 60x more Modbus requests than intended.
- **⚠ NEW — No reconnection logic**: Master reconnects on connection failure with
  MODBUS_MAX_RETRIES (3) and MODBUS_RECONNECT_INTERVAL_S (10s). C++ has none.
- **⚠ NEW — No kick/wake mechanism**: Master uses pthread condvar to wake the control
  loop immediately when a command arrives. C++ uses a fixed 500ms sleep, so commands
  may take up to 500ms to be processed.

**2a. Master `applyControlLogic()` vs C++ Implementation**:

Master's `applyControlLogic()` does the following (C++ does none of this correctly):
1. Maps `desired_working_mode` to correct device mode (only 0, 1, 2 are valid)
2. Corrects device mode if it drifted from desired mode
3. Enforces target temperature overrides per working mode:
   - Mode 1 (DHW-only): keeps heating target at HEATING_TARGET_MIN (25°C)
   - Mode 2 (Heating-only): keeps DHW target at DHW_TARGET_MIN (40°C)
   - Mode 3 (DHW+Heating): restores saved user targets if drifted > 0.5°C
4. Enforces priority per working mode:
   - Modes 1,3: sets DHW priority (register 0x02BF = 1)
   - Mode 2: clears DHW priority (register 0x02BF = 0)
5. Uses `saved_targets_initialized` flag to avoid overwriting user targets on first read
6. Updates `saved_dhw_target` and `saved_heating_target` only on explicit user commands

**2b. Master `process_commands()` is completely missing from C++**:

Master processes 4 command types:
- `CMD_SET_DHW_TEMP`: writes REG_DHW_TARGET (0x0194), saves to `saved_dhw_target`
- `CMD_SET_HEATING_TEMP`: writes REG_HEATING_TARGET (0x0191), saves to `saved_heating_target`
- `CMD_SET_PRIORITY`: writes REG_DHW_PRIORITY (0x02BF), updates `current_priority`
- `CMD_SET_RUNNING_MODE`: maps working mode to device mode, writes REG_RUNNING_MODE (0x002C),
  then adjusts targets and priority based on the working mode

C++ `enqueueCommand()` just prints and returns true — **no actual processing**.

### 3. Missing Self-Test Integration

**Current C++ Status**:
- `selftest_run()` is in C (`src/selftest.c`) but not integrated into C++ build
- `selftest_report_t` and `selftest_result_t` structures missing from C++ tests
- C++ self-test only verifies CRC16 and ModbusClient connect/disconnect
- No integration of full register read/write/verify tests

**Required C++ Implementation**:
- Include `src/selftest.c` in build
- Make `selftest.h` available (with `extern "C"` guard)
- Integrate self-test into `run_selftest()` in main.cpp
- Update CLI: master uses `--selftest`, C++ uses `-s` (incompatible)

### 4. Missing Shutdown Sequence

**Current C++ Status**:
- Main.cpp has shutdown code but may not work correctly
- No verified dedicated shutdown Modbus client pattern
- May not wait for control loop to fully stop before writing OFF mode

**⚠ NEW — Shutdown ordering differs**:
Master: `web_server_run()` blocks → signal sets stop flags → web_server_stop() returns →
`control_loop_stop()` → drain 150ms → shutdown client writes OFF → `modbus_client_destroy()` → `release_lock()`

C++: `isRunning()` poll loop → signal sets stop flags → `g_control_loop->stop()` →
drain 150ms → shutdown client writes OFF → `delete g_web_server` → `delete g_control_loop`

The C++ version stops the control loop **before** the web server finishes serving,
which means API requests after SIGINT but before web server shutdown can't queue
commands. In master, the web server loop exits first (via `web_server_stop()` in
signal handler), then control loop stops.

### 5. ⚠ NEW — Missing Instance Lock

Master uses `flock()` on `/tmp/windmi-controller.lock` to prevent multiple instances
from running simultaneously. C++ has **no lock mechanism at all**. Running two
instances would cause concurrent Modbus writes to the device.

### 6. ⚠ NEW — Web Server Non-Functional

The C++ `WebServer.cpp` is essentially a stub. It:
- Creates an `mg_http_listen()` but passes **no event handler** (3rd arg is `nullptr`)
- Has **no HTTP request routing** — no `/api/status`, `/api/set-dhw`, etc.
- Has **no static file serving** — the `static_dir` parameter is ignored
- Has **no command queue integration** — no way to queue commands
- Has **no status queue integration** — no way to read status
- Has **no CORS headers** — web UI won't work
- The `start()` method just sets a flag — `mg_mgr_poll()` is **never called**
- The `isRunning()` check in main.cpp's while loop will exit immediately since
  `mg_mgr_poll()` is never called and the shutdown mechanism is broken

Master's `web_server.c` has full routing for 5 API endpoints plus static file serving,
and runs `mg_mgr_poll()` in a blocking loop.

### 7. ⚠ NEW — StatusSnapshot Structure Mismatch

| C `status_snapshot_t` | C++ `StatusSnapshot` | Match? |
|---|---|---|
| `outdoor_temp` | `outdoor_temp` | ✅ |
| `indoor_temp` | ❌ **missing** | ❌ |
| `leaving_water_temp` | `leaving_water_temp` | ✅ |
| `dhw_tank_temp` | `dhw_tank_temp` | ✅ |
| `dhw_target` | ❌ **missing** | ❌ |
| `heating_target` | ❌ **missing** | ❌ |
| `running_mode` (int) | `mode` (int) | ❌ **rename** |
| `running_status` (int) | `running_status` (int) | ✅ |
| `dhw_priority` (bool) | `priority` (int) | ❌ **type change** |
| `is_running` (bool) | `status` (int) | ❌ **type+rename** |
| `device_online` (bool) | `device_online` (bool) | ✅ |
| `ac_current` | `ac_current` | ✅ |
| `dc_current` | `dc_current` | ✅ |
| `ac_voltage` | `ac_voltage` | ✅ |
| `dc_voltage` | `dc_voltage` | ✅ |
| `ac_power` | `ac_power` | ✅ |
| `working_mode` | `working_mode` | ✅ |
| ❌ | `heating_temperature` | ❌ **extra field** |

Missing fields (`dhw_target`, `heating_target`, `indoor_temp`) mean the web API
cannot report these values. The `priority` type change (bool→int) and `is_running`
→ `status` rename will break JSON serialization and web UI compatibility.

### 8. ⚠ NEW — Modbus Power Scaling Wrong in C++

Master's `read_status()` applies these scaling factors:
```c
status->ac_current = ac_current_raw * 2.0f;      // Display * 2 = Actual Amps
status->dc_current = dc_current_raw * 4.0f;      // Display * 4 = Actual Amps
status->ac_voltage = (float)ac_voltage_raw;      // Display = Actual Volts
status->dc_voltage = dc_voltage_raw / 2.0f;      // Display / 2 = Actual Volts
status->ac_power = status->ac_voltage * status->ac_current;  // V × A = Watts
```

C++ `readStatus()` applies:
```cpp
snapshot.ac_current = mb->readRegister(0x1014) / 2.0f;   // WRONG: should be *2
snapshot.dc_current = mb->readRegister(0x1015) / 4.0f;   // WRONG: should be *4
snapshot.ac_voltage = mb->readRegister(0x1016) / 1.0f;   // OK
snapshot.dc_voltage = mb->readRegister(0x1017) / 2.0f;   // OK
snapshot.ac_power = 0.0f;                                // WRONG: should be V×A
```

AC/DC current scaling is **inverted** (dividing instead of multiplying).
`ac_power` is hardcoded to 0 instead of calculated.

### 9. ⚠ NEW — CLI Argument Parsing Incompatible

Master supports: `--ip`, `--port`, `--web`, `--selftest`, `--help`
C++ supports: `-w`, `-s` (getopt short options only)

This is an incompatible change. Scripts or documentation referencing `--ip`, `--port`,
`--selftest` will break.

### 10. ⚠ NEW — C++ ControlLoop Does Not Receive ModbusClient

In `main.cpp`, the `ControlLoop` is created with only a status callback:
```cpp
g_control_loop = new windmi::ControlLoop([&status_monitor](const windmi::StatusSnapshot& snap) {
    status_monitor.update(snap);
});
```

But `setModbusClient()` is **never called**. Therefore:
- `modbus_client_` remains `nullptr` in the control loop
- `readStatus()` always returns `false` (early return on null check)
- `applyControlLogic()` is never called (guarded by null check)
- The control loop thread just sleeps in 500ms intervals doing nothing

The master branch passes the Modbus client to `control_loop_start()` which stores
it as `thread_client` and uses it for all register reads/writes.

### 11. ⚠ NEW — Command Queue Disconnected

Master uses SPSC queues to communicate between web server and control loop:
- `spsc_cmd_t_queue_t *cmd_queue` — web server pushes, control loop pops
- `spsc_status_snapshot_t_queue_t *status_queue` — control loop pushes, web server pops

In C++:
- The `SpscQueue` class exists but is **never instantiated** in `main.cpp`
- WebServer has no reference to any command queue
- ControlLoop has no reference to any status queue
- There is **no communication channel** between web server and control loop

Even if the web server had API handlers, queued commands would go nowhere.

---

## Test Plan

### Test 1: Startup and Shutdown (Control Loop Running)

**Objective**: Verify the application starts, control loop runs, and shuts down cleanly.

**Steps**:
1. Start application with `./windmi-control`
2. Verify Modbus connection is established
3. Verify control loop thread starts and begins reading status
4. Verify web server starts on port 8080
5. Verify `/api/status` endpoint returns valid JSON with temperature values
6. Send Ctrl+C (SIGINT)
7. Verify control loop stops
8. Verify web server stops
9. Verify OFF mode is written to Modbus register 0x002C
10. Verify application exits cleanly

**Expected Output**:
```
[Main] Rotenso Windmi Controller
[Main] Modbus gateway: 192.168.123.10:8899 (slave=11)
[Main] Control loop started
[Main] Web server: 0.0.0.0:8080
[Main] Server started. Press Ctrl+C to stop.
[Shutdown] Control loop stopped
[Shutdown] Drain period complete
[Shutdown] Writing OFF mode via dedicated client...
[Shutdown] OFF write OK (attempt 1)
[Main] Goodbye!
```

**Pass Criteria**:
- All Modbus operations succeed (no timeouts)
- Status readings are populated (not all zeros)
- Shutdown writes OFF mode (register 0x002C = 0)
- No crashes or hangs
- No resource leaks
- **⚠ NEW — Only one instance can run (lock file check)**
- **⚠ NEW — Control loop polls at 30-second interval, not 500ms**

---

### Test 2: Modbus Register Read/Write

**Objective**: Verify all required Modbus registers can be read and written.

**Registers to Test**:

| Register | Address | Access | Description |
|----------|---------|--------|-------------|
| REG_DEVICE_TYPE | 0x1006 | R | Device type identifier |
| REG_OUTDOOR_TEMP | 0x0001 | R | Outdoor temperature (0.1°C) |
| REG_DHW_TANK_TEMP | 0x00CE | R | DHW tank temperature (0.1°C) |
| REG_LEAVING_WATER_TEMP | 0x0004 | R | Leaving water temperature (0.1°C) |
| REG_RUNNING_STATUS | 0x002D | R | Current running status |
| REG_DHW_TARGET | 0x0194 | RW | DHW target temperature (0.1°C) |
| REG_HEATING_TARGET | 0x0191 | RW | Heating target temperature (0.1°C) |
| REG_RUNNING_MODE | 0x002C | RW | Set mode: 0=Off, 1=Cool+DHW, 2=Heat+DHW |
| REG_DHW_PRIORITY | 0x02BF | RW | DHW priority flag |
| REG_AC_CURRENT | 0x1014 | R | AC current (Display × 2 = Actual Amps) |
| REG_DC_CURRENT | 0x1015 | R | DC current (Display × 4 = Actual Amps) |
| REG_AC_VOLTAGE | 0x1016 | R | AC voltage (Display = Actual Volts) |
| REG_DC_VOLTAGE | 0x1017 | R | DC voltage (Display / 2 = Actual Volts) |

**⚠ UPDATED — Removed fabricated registers REG_DHW_TEMP (0x0012) and
REG_HEATING_TEMP (0x0014) from test table. These don't exist on the device.
Added correct registers REG_DHW_TANK_TEMP (0x00CE), REG_DHW_TARGET (0x0194),
REG_HEATING_TARGET (0x0191).**

**Test Steps**:
1. Start application
2. Read all registers via `/api/status` endpoint
3. Verify values are in expected ranges:
   - Temperature values: 0-100°C
   - Current values: 0-50A
   - Voltage values: 0-300V
4. Write test values to heating/DHW targets
5. Verify values persist via read
6. Restore original values

**⚠ NEW — Power scaling verification**:
- AC current: verify raw × 2 = displayed amps (not raw / 2)
- DC current: verify raw × 4 = displayed amps (not raw / 4)
- AC power: verify calculated as ac_voltage × ac_current (not hardcoded 0)

**Pass Criteria**:
- All registers return valid values
- Write operations succeed
- No timeout errors
- Values are in expected ranges
- Power scaling factors are correct (multiply, not divide)

---

### Test 3: Self-Test Execution

**Objective**: Run the full self-test and verify all tests pass.

**Steps**:
1. Run `./windmi-control --selftest` (**⚠ UPDATED — was `-s`**)
2. Verify self-test runs all 6 tests
3. Verify read/write/verify for each register
4. Verify original values are restored after write tests

**Expected Test Output**:
```
[Main] Rotenso Windmi Controller
[Main] Modbus gateway: 192.168.123.10:8899 (slave=11)
========== Self-Test Report ==========
Test Results:
Test                   Address    Read  Write  Verify      Value
----------------------------------------------------------------
Device Type            0x1006     OK     OK     OK          8
Heating Setpoint      0x0191     OK     OK     OK         450
DHW Setpoint          0x0194     OK     OK     OK         460
Outdoor Temp          0x0001     OK     OK     OK         250
DHW Tank Temp         0x00CE     OK     OK     OK         480
Write Verify Test     0x0191     OK     OK     OK         450
----------------------------------------------------------------
Summary:
  Total tests: 6
  Passed: 6
  Failed: 0
  All critical passed: YES
========================================
Self-test PASSED
```

**⚠ UPDATED — Expected output now matches master branch format, not the
simplified C++ version.**

**Pass Criteria**:
- All 6 tests pass
- Read OK, Write OK, Verify OK for all registers
- Original values restored after write tests
- No errors in output
- CLI uses `--selftest` flag (compatible with master)

---

### Test 4: Web API Endpoints

**Objective**: Verify all web API endpoints respond correctly.

**Endpoints to Test**:

| Endpoint | Method | Description | Expected Response |
|----------|--------|-------------|-------------------|
| `/api/status` | GET | Returns current status as JSON | 200 OK, JSON body |
| `/api/set-dhw` | POST | Sets DHW target temperature | 202 Accepted, JSON body |
| `/api/set-heating` | POST | Sets heating target temperature | 202 Accepted, JSON body |
| `/api/set-priority` | POST | Sets priority (dhw/heating) | 202 Accepted, JSON body |
| `/api/set-mode` | POST | Sets running mode (0-3) | 202 Accepted, JSON body |

**⚠ UPDATED — Corrected endpoint paths to match master branch:
- `/api/set-dhw` (not `/api/setDhwTemperature`)
- `/api/set-heating` (not `/api/setHeatingTemperature`)
- `/api/set-priority` (not `/api/setPriority`)
- `/api/set-mode` (not `/api/setMode`)
- Removed `/api/shutdown` (doesn't exist in master — shutdown is via SIGINT only)**

Also: master serves static files from `static/` directory for the web UI. This **must
work** for the frontend to be accessible.

**Test Steps**:
1. Start application
2. Test each endpoint with valid inputs
3. Test each endpoint with invalid inputs (out of range, missing parameters)
4. Verify static file serving (`GET /` returns `static/index.html`)

**⚠ NEW — Shutdown endpoint test removed**: Master does not have `/api/shutdown`.
Shutdown is only via SIGINT/SIGTERM signal.

**⚠ NEW — Static file serving test added**: The web UI must be accessible.

**Pass Criteria**:
- All endpoints return appropriate status codes (200, 202, 400, 422, 405, 503)
- JSON responses match master's format exactly (field names, types)
- Commands are queued and processed by control loop
- Invalid inputs return appropriate error responses
- Static files are served correctly
- CORS headers present (`Access-Control-Allow-Origin: *`)
- 503 returned during shutdown (`web_server_is_shutting_down()`)

---

### Test 5: Control Loop Logic

**Objective**: Verify control loop applies correct logic.

**Scenarios to Test**:

**Scenario 5a: DHW Priority Mode**
- Set working mode to DHW+Heating (3)
- Verify DHW priority register (0x02BF) is set to 1
- Verify both saved targets are written to device

**⚠ UPDATED — Original plan said "heating target set to minimum when DHW priority
is active". This is wrong. In master, DHW priority does NOT lower heating target.
Only DHW-only mode (working mode 1) lowers heating target to minimum.**

**Scenario 5b: Heating-only Mode**
- Set working mode to Heating-only (2)
- Verify DHW target register (0x0194) is set to DHW_TARGET_MIN (40°C / raw 400)
- Verify DHW priority register (0x02BF) is set to 0 (heating priority)
- Verify device mode register (0x002C) is set to 2 (Heat+DHW)

**⚠ UPDATED — Original plan said "heating priority mode". Master doesn't use
that term. It's "Heating-only working mode" which clears DHW priority and sets
DHW target to minimum.**

**Scenario 5c: DHW-only Mode**
- Set working mode to DHW-only (1)
- Verify heating target register (0x0191) is set to HEATING_TARGET_MIN (25°C / raw 250)
- Verify DHW priority register (0x02BF) is set to 1 (DHW priority)
- Verify device mode register (0x002C) is set to 2 (Heat+DHW)

**⚠ NEW — This scenario was missing from the original plan.**

**Scenario 5d: Mode Switching with Target Persistence**
- Set DHW target to 50°C, heating target to 40°C
- Switch to OFF mode (0), then back to DHW+Heating (3)
- Verify saved targets (50°C and 40°C) are restored, not defaults
- Verify `saved_targets_initialized` prevents overwriting on first read

**⚠ UPDATED — Original plan was vague about saved targets. Now specifies
exact expected behavior matching master.**

**Scenario 5e: Hysteresis / Target Drift Correction**
- In DHW+Heating mode, if read-back drifts > 0.5°C from saved target,
  control loop should re-write the saved target
- In DHW-only mode, if heating target drifts above HEATING_TARGET_MIN + 0.5°C,
  control loop should re-write minimum
- In Heating-only mode, if DHW target drifts above DHW_TARGET_MIN + 0.5°C,
  control loop should re-write minimum

**⚠ NEW — This scenario was missing from the original plan. Master implements
drift correction with 0.5°C tolerance.**

**Pass Criteria**:
- Device mode correctly mapped (only 0, 1, 2 written to 0x002C)
- Priority enforced per working mode
- Targets adjusted per working mode
- Saved targets maintained across mode changes
- Drift correction applied within 30-second poll interval
- No writing to read-only registers

---

### Test 6: Signal Handling

**Objective**: Verify graceful shutdown on SIGINT/SIGTERM.

**Steps**:
1. Start application
2. Verify lock file exists at `/tmp/windmi-controller.lock`
3. Send SIGINT (`kill -INT <pid>`)
4. Verify control loop stops (log message appears)
5. Verify web server stops (log message appears)
6. Verify OFF mode written (log message appears)
7. Verify lock file released
8. Verify clean exit with exit code 0

**⚠ NEW — Lock file verification added.**

**Pass Criteria**:
- No hangs during shutdown
- OFF mode written before exit
- Lock file released on exit
- No core dumps
- Exit code 0
- Second instance blocked from starting (lock file test)

---

### Test 7: Multiple Concurrent Requests

**Objective**: Verify web server handles concurrent requests without data corruption.

**Steps**:
1. Start application
2. Send 10 concurrent GET `/api/status` requests using `curl -&`
3. Verify all responses complete and contain valid JSON
4. Send 5 concurrent POST requests to set temperatures rapidly
5. Verify commands are queued in order (SPSC queue guarantees FIFO)

**Pass Criteria**:
- All responses complete successfully
- No JSON parsing errors
- Commands processed in order (FIFO)
- No data corruption in responses

---

### Test 8: Error Handling

**Objective**: Verify error handling for invalid inputs and edge cases.

**Scenarios**:

| Scenario | Expected Behavior |
|----------|-------------------|
| DHW temp above max (e.g., 100°C) | Return 422 Unprocessable Entity |
| DHW temp below min (e.g., 20°C) | Return 422 Unprocessable Entity |
| Heating temp above max (e.g., 100°C) | Return 422 Unprocessable Entity |
| Heating temp below min (e.g., 10°C) | Return 422 Unprocessable Entity |
| Missing temperature parameter | Return 400 Bad Request |
| Empty POST body | Return 400 Bad Request |
| Invalid JSON in POST body | Return 400 Bad Request |
| Invalid priority value | Return 422 Unprocessable Entity |
| Wrong HTTP method on API endpoint | Return 405 Method Not Allowed |
| Request during shutdown | Return 503 Service Unavailable |
| Network disconnect during request | Clean connection close, no crash |
| Modbus timeout during register read | Control loop logs error, publishes offline status |
| Modbus reconnection | Control loop retries up to 3 times with 10s interval |

**⚠ UPDATED — Expanded error scenarios to match master branch behavior:
- Added wrong HTTP method (405) per master's `mg_strcasecmp` method checks
- Added shutdown rejection (503) per master's `web_server_is_shutting_down()` check
- Added Modbus timeout and reconnection behavior
- Split temperature validation by endpoint type**

**Pass Criteria**:
- Appropriate error codes returned (400, 405, 422, 503)
- No crashes or memory leaks
- Error messages logged appropriately
- Modbus reconnection attempts made on failure

---

## Implementation Plan

### Phase 0: Fix C++ Config (Critical — prerequisite for all other phases)

**File**: `include/config.h`

**Changes Required**:
1. **Remove fabricated registers**: Delete `REG_DHW_TEMP` (0x0012) and
   `REG_HEATING_TEMP` (0x0014) — these don't exist on the device
2. Add all missing register definitions from master branch `src/config.h`:
   - `REG_DEVICE_TYPE` (0x1006)
   - `REG_OUTDOOR_TEMP` (0x0001)
   - `REG_INDOOR_TEMP` (0x0002)
   - `REG_ENTERING_WATER_TEMP` (0x0003)
   - `REG_LEAVING_WATER_TEMP` (0x0004)
   - `REG_DHW_TANK_TEMP` (0x00CE)
   - `REG_HEATING_TARGET` (0x0191) — writable heating target
   - `REG_DHW_TARGET` (0x0194) — writable DHW target
   - `REG_WATER_CONTROL_POINT` (0x0033)
   - `REG_DHW_MODE_STATUS` (0x00C9)
   - `REG_OCCUPANCY_MODE` (0x0029)
3. **Replace mode constants**: Remove `MODE_DHW_ONLY=1`, `MODE_HEATING_ONLY=2`,
   `MODE_DHW_HEATING=3`, `MODE_OFF=0`, `PRIORITY_AUTO=0`, `PRIORITY_DHW=1`
   and replace with master's two-level scheme:
   - `MODE_SET_OFF=0`, `MODE_SET_COOL_DHW=1`, `MODE_SET_HEAT_DHW=2` (device-level)
   - `MODE_STATUS_OFF=0`, `MODE_STATUS_COOL=1`, `MODE_STATUS_HEAT=2`,
     `MODE_STATUS_DHW=4`, `MODE_STATUS_DEFROST=7`, `MODE_STATUS_ANTIFREEZE=20`
   - Working mode values (0-3) are application-level, not config defines
4. Add all missing configuration constants:
   - `MODBUS_GATEWAY_IP`, `MODBUS_GATEWAY_PORT`, `MODBUS_SLAVE_ID`
   - `MODBUS_TIMEOUT_MS`, `MODBUS_MAX_RETRIES`, `MODBUS_RECONNECT_INTERVAL_S`
   - `MODBUS_POLL_INTERVAL_S`, `CONTROL_LOOP_INTERVAL_S`
   - `DHW_HYSTERESIS_C`, `HEATING_HYSTERESIS_C`
   - `WEB_SERVER_PORT`, `SELFTEST_DHW_TARGET_TEMP`
5. Add C++ compatibility guard: `#ifdef __cplusplus extern "C" { #endif`

**⚠ NEW — Original plan didn't mention removing fabricated registers or fixing
the mode constant scheme. This is critical because wrong registers will corrupt
device state or cause timeouts.**

---

### Phase 1: Fix StatusSnapshot Structure (Critical)

**File**: `include/core/ControlLoop.hpp`

**Changes Required**:
1. Add missing fields: `dhw_target`, `heating_target`, `indoor_temp`
2. Remove fabricated field `heating_temperature` (not in master)
3. Fix type mismatches:
   - `running_mode` (was `mode`) — match master's field name
   - `dhw_priority` (bool, was `priority` int) — match master's type
   - `is_running` (bool, was `status` int) — match master's type and name
4. These fields are serialized to JSON by the web server, so field names and
   types must match master exactly for web UI compatibility

**⚠ NEW — Original plan didn't address the StatusSnapshot structure mismatch.
This is needed before any register reading or JSON API can work correctly.**

---

### Phase 2: Integrate Self-Test (Critical)

**Files**: `src/selftest.c`, `src/selftest.h`

**Changes Required**:
1. Move `src/selftest.h` to `include/selftest.h` with `extern "C"` guard
2. Add `src/selftest.c` to CMake build (add to a library target or main)
3. Update `main.cpp` `run_selftest()` to call `selftest_run()` and
   `selftest_print_report()` — matching master's behavior
4. Fix CLI: use `--selftest` instead of `-s`

---

### Phase 3: Fix ControlLoop Implementation (Critical)

**File**: `src/core/ControlLoop.cpp`, `include/core/ControlLoop.hpp`

**Changes Required**:

1. **Pass ModbusClient to ControlLoop**: Constructor or `start()` must accept
   a `ModbusClient` pointer. Currently `setModbusClient()` is never called in
   `main.cpp`, so the control loop does nothing.

2. **Add SPSC queue references**: ControlLoop must hold references to:
   - Command queue (to receive commands from web server)
   - Status queue (to publish snapshots for web server)
   These replace the current `StatusCallback` pattern which doesn't provide
   a communication channel for commands.

3. **Rewrite `readStatus()` to read correct registers**:
   ```
   Read: REG_OUTDOOR_TEMP, REG_INDOOR_TEMP, REG_LEAVING_WATER_TEMP,
         REG_DHW_TANK_TEMP, REG_RUNNING_MODE, REG_RUNNING_STATUS,
         REG_DHW_TARGET, REG_HEATING_TARGET, REG_DHW_PRIORITY,
         REG_AC_CURRENT, REG_DC_CURRENT, REG_AC_VOLTAGE, REG_DC_VOLTAGE
   ```
   With correct scaling factors:
   - `ac_current = raw * 2.0f` (not `/ 2.0f`)
   - `dc_current = raw * 4.0f` (not `/ 4.0f`)
   - `ac_power = ac_voltage * ac_current` (not `0.0f`)

4. **Implement command processing matching master's `process_commands()`**:
   - `CMD_SET_DHW_TEMP`: write REG_DHW_TARGET, save to `saved_dhw_target`
   - `CMD_SET_HEATING_TEMP`: write REG_HEATING_TARGET, save to `saved_heating_target`
   - `CMD_SET_PRIORITY`: write REG_DHW_PRIORITY, update `current_priority`
   - `CMD_SET_RUNNING_MODE`: map working mode→device mode, write REG_RUNNING_MODE,
     adjust targets and priority

5. **Rewrite `applyControlLogic()` matching master**:
   - Map `desired_working_mode` to device mode (only 0, 1, 2)
   - Correct device mode if drifted
   - Enforce target overrides per mode
   - Enforce priority per mode
   - Restore saved targets on drift (0.5°C tolerance)

6. **Add state tracking**:
   - `current_mode` — actual device mode
   - `desired_working_mode` — application-level mode (0-3)
   - `current_priority` — PRIORITY_DHW or PRIORITY_HEATING
   - `saved_dhw_target`, `saved_heating_target`
   - `saved_targets_initialized`

7. **Fix poll interval**: Change from 500ms to 30 seconds (CONTROL_LOOP_INTERVAL_S)

8. **Add kick/wake mechanism**: When a command is queued, wake the control loop
   immediately instead of waiting for the next poll interval. Master uses
   pthread condvar for this; C++ should use `std::condition_variable`.

9. **Add reconnection logic**: On connection failure, retry up to
   MODBUS_MAX_RETRIES times with MODBUS_RECONNECT_INTERVAL_S delay.

**⚠ NEW — Original plan was too vague. Now specifies exact register addresses,
scaling factors, command processing, state tracking, and timing requirements.**

---

### Phase 4: Rewrite WebServer (Critical)

**File**: `src/web/WebServer.cpp`, `include/web/WebServer.hpp`

**The current C++ WebServer is a stub with no functionality. It needs a complete
rewrite to match master's `web_server.c` functionality.**

**Changes Required**:

1. **Add HTTP event handler**: Register a proper `mg_http_listen` callback
   that routes requests to API handlers or static file serving

2. **Add API route handlers**:
   - `GET /api/status` → read latest snapshot from status queue, return JSON
   - `POST /api/set-dhw` → parse temperature, validate range, push to cmd queue
   - `POST /api/set-heating` → parse temperature, validate range, push to cmd queue
   - `POST /api/set-priority` → parse priority string, push to cmd queue
   - `POST /api/set-mode` → parse mode int, push to cmd queue
   - Static files → `mg_http_serve_dir()` with `static/` root

3. **Add queue references**: WebServer constructor must accept:
   - Command queue (to push commands)
   - Status queue (to read snapshots for /api/status)

4. **Add shutdown rejection**: Implement `isShuttingDown()` check.
   During shutdown, API endpoints return 503.

5. **Add `mg_mgr_poll()` loop**: The `start()` method must run the mongoose
   poll loop. Master uses `web_server_run()` as a blocking call.

6. **Add CORS headers**: `Access-Control-Allow-Origin: *` on all JSON responses.

7. **Add method checking**: Return 405 for wrong HTTP methods on API endpoints.

8. **JSON format must match master exactly**: Same field names, same types,
   same formatting. The web UI depends on this.

**⚠ NEW — Original plan only said "verify shutdown sequence". The web server
is actually completely non-functional and needs a full rewrite.**

---

### Phase 5: Fix Main Entry Point (Critical)

**File**: `src/main.cpp`

**Changes Required**:

1. **Add instance lock**: Implement `acquire_lock()` / `release_lock()` with
   `flock()` on `/tmp/windmi-controller.lock`, matching master

2. **Fix CLI argument parsing**: Use `--ip`, `--port`, `--web`, `--selftest`,
   `--help` instead of `-w`, `-s` (compatible with master)

3. **Create SPSC queues** and pass them to both ControlLoop and WebServer

4. **Pass ModbusClient to ControlLoop**: Currently not done — control loop
   has no Modbus access and does nothing

5. **Fix startup sequence**:
   1. Acquire lock
   2. Install signal handlers
   3. Create Modbus client and connect
   4. Create cmd/status queues
   5. Start control loop (pass client + queues)
   6. Start web server (pass queues, serve static files)
   7. Run web server poll loop (blocking)

6. **Fix shutdown sequence** (match master ordering):
   1. Signal handler calls `web_server_stop()` (which exits poll loop)
   2. `control_loop_stop()` + join
   3. Drain period (150ms)
   4. Dedicated shutdown client writes OFF mode (3 retries)
   5. Destroy clients
   6. Release lock

7. **Remove hardcoded constants**: Use config.h defines for IP, port, slave ID

**⚠ NEW — Original plan only said "verify shutdown sequence". Main.cpp needs
significant changes for lock, CLI, queues, and startup/shutdown ordering.**

---

### Phase 6: Integrate Self-Test (Critical)

Same as Phase 2 above (integrate `src/selftest.c` into build and main.cpp).

---

### Phase 7: Update Unit Tests (High Priority)

**Files**: `tests/`

**Changes Required**:
1. Add register read/write tests for ModbusClient (using mock or real device)
2. Add command processing tests for ControlLoop
3. Add control logic tests (priority switching, mode mapping, hysteresis)
4. Add power scaling verification tests (AC/DC current multiply, not divide)
5. Fix StatusSnapshot structure tests (match master's field names and types)
6. Add WebServer API tests (route handling, JSON format, error codes)
7. Add config register address tests (verify all addresses match master)

**⚠ NEW — Original plan didn't mention power scaling tests, config address
verification, or web API tests.**

---

## Files Requiring Changes

| File | Change Type | Priority |
|------|-------------|----------|
| `include/config.h` | Remove fabricated regs, add missing, fix modes | Critical |
| `include/core/ControlLoop.hpp` | Fix StatusSnapshot fields | Critical |
| `src/core/ControlLoop.cpp` | Complete rewrite (reads, commands, logic) | Critical |
| `src/web/WebServer.cpp` | **Complete rewrite** (API, static, queues) | Critical |
| `include/web/WebServer.hpp` | Add queue refs, API interface | Critical |
| `src/main.cpp` | Add lock, fix CLI, add queues, fix sequencing | Critical |
| `include/selftest.h` | Add from master with extern "C" | Critical |
| `src/selftest.c` | Integrate into CMake build | Critical |
| `src/modbus/ModbusClient.cpp` | Verify read/write, add reconnection | High |
| `include/modbus/ModbusClient.hpp` | Add reconnect(), isConnecting() | High |
| `include/utils/SpscQueue.hpp` | Add `latest()` method for status | High |
| `tests/` | Update to match new structures and config | High |

**⚠ NEW — The original plan listed 8 files. After review, 12 files need changes,
and several need complete rewrites rather than just "fixes".**

---

## Success Criteria

The C++ conversion is complete when:

1. [ ] All register definitions in `include/config.h` match master branch exactly
2. [ ] No fabricated register addresses (0x0012, 0x0014) remain
3. [ ] StatusSnapshot fields match master's `status_snapshot_t`
4. [ ] Self-test runs and passes all 6 tests with `--selftest` flag
5. [ ] Startup/shutdown works with control loop running and polling correctly
6. [ ] Instance lock prevents multiple instances
7. [ ] Control loop reads all 13+ registers with correct addresses and scaling
8. [ ] Control loop processes commands (DHW, heating, priority, mode)
9. [ ] Control loop applies logic matching master (mode mapping, targets, priority)
10. [ ] Control loop polls at 30-second interval (not 500ms)
11. [ ] Control loop reconnects on connection failure (3 retries, 10s interval)
12. [ ] Commands wake control loop immediately (not wait for next poll)
13. [ ] Web server serves API endpoints matching master (routes, JSON format)
14. [ ] Web server serves static files
15. [ ] Web server pushes commands to queue, reads status from queue
16. [ ] Power scaling is correct (AC ×2, DC ×4, power = V × A)
17. [ ] CLI arguments compatible with master (`--ip`, `--port`, etc.)
18. [ ] Shutdown writes OFF mode with 3 retries
19. [ ] Lock file released on exit
20. [ ] All tests pass with `ctest`

---

## Notes

- Master branch (`src/main.c`, `src/control_loop.c`, `src/web_server.c`) is the
  source of truth for functionality
- All C++ implementation must match C behavior exactly
- Modbus register addresses are in hexadecimal (0x prefix)
- Temperature values are in 0.1°C units (divide by 10.0f)
- Current values require scaling (AC × 2, DC × 4) — **multiply, not divide**
- The self-test writes test values and restores originals
- The web server API paths use hyphens (`/api/set-dhw`) not camelCase
- Working mode (0-3) is application-level, not written to device directly
- Device mode (0-2) is the only thing written to REG_RUNNING_MODE (0x002C)