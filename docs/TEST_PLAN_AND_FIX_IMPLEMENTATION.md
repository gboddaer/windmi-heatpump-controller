# Test Plan & Fix Implementation Plan

Verified against master branch C source code (2026-05-29).

---

## Overview

The C++ conversion has **broken functionality**. The C implementation (`src/main.c`,
`src/control_loop.c`, `src/web_server.c`) is production-ready. The C++ version
is incomplete and non-functional.

---

## Root Causes

### 1. Fabricated and Missing Register Definitions

**`include/config.h`** defines registers that don't exist on the device and is
missing registers that do.

**Fabricated (must remove)**:
- `REG_DHW_TEMP` (0x0012) — does not exist in master. Not a real device register.
- `REG_HEATING_TEMP` (0x0014) — does not exist in master. Not a real device register.
- `REG_AC_POWER` (0x1018) — does not exist in master config. Power is calculated
  as `ac_voltage × ac_current`, not read from a register.

**Missing (must add from master `src/config.h`)**:

| Define | Value | Access | Description |
|--------|-------|--------|-------------|
| `REG_DEVICE_TYPE` | 0x1006 | R | Device type identifier |
| `REG_OUTDOOR_TEMP` | 0x0001 | R | Outdoor temperature (0.1°C) |
| `REG_INDOOR_TEMP` | 0x0002 | R | Indoor temperature (0.1°C) |
| `REG_ENTERING_WATER_TEMP` | 0x0003 | R | Entering water temperature (0.1°C) |
| `REG_LEAVING_WATER_TEMP` | 0x0004 | R | Leaving water temperature (0.1°C) |
| `REG_WATER_CONTROL_POINT` | 0x0033 | R | Water control point status |
| `REG_DHW_MODE_STATUS` | 0x00C9 | R | DHW mode (0=Eco, 1=Anti Legionella, 2=Regular) |
| `REG_DHW_PRIORITY` | 0x02BF | R/W | DHW priority flag (0=Automatic, 1=DHW priority) |
| `REG_DHW_TANK_TEMP` | 0x00CE | R | DHW tank temperature (0.1°C) |
| `REG_HEATING_TARGET` | 0x0191 | R/W | Heating target temperature (0.1°C) |
| `REG_DHW_TARGET` | 0x0194 | R/W | DHW target temperature (0.1°C) |
| `REG_OCCUPANCY_MODE` | 0x0029 | R/W | Occupancy mode (0=Away, 1=Sleep, 2=Home) |

Note: `REG_DHW_PRIORITY` is commented as R in master's config.h, but master's
`control_loop.c` writes to it successfully. The config comment is wrong; the
register is actually R/W. Follow the working C code.

**Missing mode constants** (master defines two separate sets):

Device-level (values written to `REG_RUNNING_MODE` 0x002C):
- `MODE_SET_OFF` = 0
- `MODE_SET_COOL_DHW` = 1
- `MODE_SET_HEAT_DHW` = 2

Status-level (values read from `REG_RUNNING_STATUS` 0x002D):
- `MODE_STATUS_OFF` = 0
- `MODE_STATUS_COOL` = 1
- `MODE_STATUS_HEAT` = 2
- `MODE_STATUS_DHW` = 4
- `MODE_STATUS_DEFROST` = 7
- `MODE_STATUS_ANTIFREEZE` = 20

C++ config conflates these with application-level working modes:
```cpp
#define MODE_DHW_ONLY     1   // overlaps MODE_SET_COOL_DHW — wrong
#define MODE_HEATING_ONLY 2  // overlaps MODE_SET_HEAT_DHW — wrong
#define MODE_DHW_HEATING  3   // no corresponding device mode
```
The working mode values (0-3) are internal application state, not config
constants. They should NOT be #defines. Master stores them in a variable
`desired_working_mode`, not as config macros.

**Missing config constants from master**:

| Define | Value | Description |
|--------|-------|-------------|
| `MODBUS_GATEWAY_IP` | "192.168.123.10" | Default Modbus gateway IP |
| `MODBUS_GATEWAY_PORT` | 8899 | Default Modbus gateway port |
| `MODBUS_SLAVE_ID` | 11 | Default Modbus slave ID |
| `MODBUS_TIMEOUT_MS` | 1000 | Modbus response timeout |
| `MODBUS_MAX_RETRIES` | 3 | Max reconnection attempts |
| `MODBUS_RECONNECT_INTERVAL_S` | 10 | Seconds between reconnection attempts |
| `MODBUS_POLL_INTERVAL_S` | 30 | Unused alias for CONTROL_LOOP_INTERVAL_S |
| `CONTROL_LOOP_INTERVAL_S` | 30 | Control loop poll interval (seconds) |
| `DHW_HYSTERESIS_C` | 3.0f | DHW hysteresis (unused in current code) |
| `HEATING_HYSTERESIS_C` | 1.0f | Heating hysteresis (unused in current code) |
| `WEB_SERVER_PORT` | 8080 | Web server default port |
| `SELFTEST_DHW_TARGET_TEMP` | 45.0f | Self-test DHW target temperature |

Note: `DHW_HYSTERESIS_C` and `HEATING_HYSTERESIS_C` are defined in master config
but not used in `control_loop.c`. Master uses 0.5°C drift tolerance for target
correction instead. Keep the defines but note they are unused.

**Also: master has duplicate register defines** (REG_AC_CURRENT/DC_CURRENT/
AC_VOLTAGE/DC_VOLTAGE defined twice). The C++ config should NOT duplicate them.

### 2. C++ ControlLoop Writes to Wrong Registers

`src/core/ControlLoop.cpp` `applyControlLogic()` writes DHW and heating targets
to fabricated addresses 0x0012 and 0x0014. These are **not writable target registers**.

Correct addresses from master:
- Write DHW target → `REG_DHW_TARGET` (0x0194)
- Write heating target → `REG_HEATING_TARGET` (0x0191)

### 3. C++ ControlLoop Reads Wrong Registers

`readStatus()` reads from fabricated addresses 0x0012 and 0x0014.
Master reads from:
- `REG_DHW_TANK_TEMP` (0x00CE) for DHW tank temperature
- `REG_DHW_TARGET` (0x0194) for DHW target temperature
- `REG_HEATING_TARGET` (0x0191) for heating target temperature
- `REG_LEAVING_WATER_TEMP` (0x0004) for leaving water temperature

### 4. Inverted Power Scaling Factors

Master's scaling:
```c
ac_current = raw * 2.0f;     // Display × 2 = Actual Amps
dc_current = raw * 4.0f;     // Display × 4 = Actual Amps
ac_voltage = (float)raw;     // Display = Actual Volts
dc_voltage = raw / 2.0f;    // Display / 2 = Actual Volts
ac_power  = ac_voltage * ac_current;  // V × A = Watts
```

C++ scaling (WRONG):
```cpp
ac_current = raw / 2.0f;    // DIVIDES instead of MULTIPLIES
dc_current = raw / 4.0f;    // DIVIDES instead of MULTIPLIES
ac_power  = 0.0f;           // HARDCODED instead of calculated
```

### 5. Wrong Poll Interval

C++ uses `sleep_for(500ms)`. Master uses `CONTROL_LOOP_INTERVAL_S = 30` seconds.
The 500ms interval sends 60× more Modbus requests than intended.

### 6. No Reconnection Logic

Master reconnects on connection failure: up to `MODBUS_MAX_RETRIES` (3) attempts
with `MODBUS_RECONNECT_INTERVAL_S` (10s) delay. C++ has none — if the connection
drops, the control loop silently fails forever.

### 7. No Kick/Wake Mechanism

Master uses `pthread_cond_timedwait()` with a kick from the command processor.
When a command arrives, the control loop wakes immediately instead of waiting for
the next poll. C++ uses `sleep_for(500ms)` and cannot be woken early.

### 8. No Command Processing

Master's `process_commands()` handles 4 command types from the SPSC queue:
- `CMD_SET_DHW_TEMP`: writes `REG_DHW_TARGET`, saves to `saved_dhw_target`
- `CMD_SET_HEATING_TEMP`: writes `REG_HEATING_TARGET`, saves to `saved_heating_target`
- `CMD_SET_PRIORITY`: writes `REG_DHW_PRIORITY`, updates `current_priority`
- `CMD_SET_RUNNING_MODE`: maps working mode to device mode, writes
  `REG_RUNNING_MODE`, then adjusts targets and priority based on the mode

C++ `enqueueCommand()` only prints a debug message. No queue. No Modbus writes.

### 9. No Control Logic Implementation

Master's `apply_control_logic()` does:
1. Map `desired_working_mode` (0-3) to correct device mode (0-2 only)
2. Correct device mode if it drifted from desired mode
3. Enforce target temperature overrides per working mode:
   - Mode 1 (DHW-only): keep `heating_target` at `HEATING_TARGET_MIN` (25°C)
   - Mode 2 (Heating-only): keep `dhw_target` at `DHW_TARGET_MIN` (40°C)  
   - Mode 3 (DHW+Heating): restore saved targets if drifted > 0.5°C
4. Enforce priority per working mode:
   - Modes 1, 3: set DHW priority (REG_DHW_PRIORITY = 1)
   - Mode 2: clear DHW priority (REG_DHW_PRIORITY = 0)

C++ `applyControlLogic()` exists but writes to wrong registers and doesn't match
master's logic.

### 10. ModbusClient Never Passed to ControlLoop

`main.cpp` creates a `ControlLoop` but never calls `setModbusClient()`. Therefore
`modbus_client_` is always `nullptr`, `readStatus()` always returns false, and the
control loop thread does nothing except sleep.

### 11. Web Server Is a Complete Stub

`src/web/WebServer.cpp` has no functionality:
- `mg_http_listen()` called with `nullptr` handler — no HTTP request routing
- No API endpoint handlers (`/api/status`, `/api/set-dhw`, etc.)
- No static file serving (`static/` directory ignored)
- No command queue integration
- No status queue integration
- No CORS headers
- `mg_mgr_poll()` is never called — the server never processes requests
- `isRunning()` returns `true` falsely since no poll loop runs
- No shutdown rejection (503 during shutdown)

Master's `web_server.c` is 290 lines with 5 API routes and static file serving.

### 12. StatusSnapshot Structure Mismatch

| C `status_snapshot_t` | C++ `StatusSnapshot` | Match? |
|---|---|---|
| `outdoor_temp` | `outdoor_temp` | ✅ |
| `indoor_temp` | ❌ missing | ❌ |
| `leaving_water_temp` | `leaving_water_temp` | ✅ |
| `dhw_tank_temp` | `dhw_tank_temp` | ✅ |
| `dhw_target` | ❌ missing | ❌ |
| `heating_target` | ❌ missing | ❌ |
| `running_mode` | `mode` | ❌ renamed |
| `running_status` | `running_status` | ✅ |
| `dhw_priority` (bool) | `priority` (int) | ❌ type+name |
| `is_running` (bool) | `status` (int) | ❌ type+name |
| `device_online` (bool) | `device_online` (bool) | ✅ |
| `ac_current` | `ac_current` | ✅ |
| `dc_current` | `dc_current` | ✅ |
| `ac_voltage` | `ac_voltage` | ✅ |
| `dc_voltage` | `dc_voltage` | ✅ |
| `ac_power` | `ac_power` | ✅ |
| `working_mode` | `working_mode` | ✅ |
| ❌ | `heating_temperature` | ❌ extra |

Missing `dhw_target` and `heating_target` means the web API can't report target
temperatures. Missing `indoor_temp` means indoor temp can't be read. The `priority`
type change breaks JSON output (`"priority":"dhw"` vs `3`). The `is_running`→
`status` rename breaks JSON (`"status":"running"` vs `0`).

### 13. JSON API Field Names Must Match Master

Master's `/api/status` JSON mapping (verified from `web_server.c`):
```
"dhwTemperature"         ← dhw_tank_temp
"dhwTarget"              ← dhw_target
"heatingTemperature"     ← leaving_water_temp (NOT a separate field!)
"heatingTarget"          ← heating_target
"outdoorTemperature"     ← outdoor_temp
"leavingWaterTemperature"← leaving_water_temp (same as heatingTemperature)
"mode"                   ← mode_to_string(running_mode): "off"/"cool+dhw"/"heat+dhw"
"runningStatus"          ← status_to_string(running_status): "off"/"cool"/"heat"/"dhw"/"defrost"/"antifreeze"
"priority"               ← dhw_priority ? "dhw" : "heating"
"status"                 ← is_running ? "running" : "stopped"
"deviceOnline"           ← device_online (true/false)
"acCurrent"              ← ac_current
"dcCurrent"              ← dc_current
"acVoltage"              ← ac_voltage
"dcVoltage"              ← dc_voltage
"acPower"                ← ac_power
"workingMode"            ← working_mode
```

Note: `heatingTemperature` and `leavingWaterTemperature` both map to
`leaving_water_temp`. There is NO separate "heating temperature" field in the
C struct. The C++ StatusSnapshot has a fabricated `heating_temperature` field
that doesn't correspond to anything in the C code.

### 14. Missing Instance Lock

Master uses `flock()` on `/tmp/windmi-controller.lock` to prevent multiple
instances. C++ has no lock mechanism. Two instances would cause concurrent
Modbus writes to the device.

### 15. Wrong Modbus Slave ID

C++ `main.cpp` hardcodes `MODBUS_SLAVE_ID = 1`. Master uses `MODBUS_SLAVE_ID = 11`.

### 16. CLI Argument Parsing Incompatible

Master: `--ip`, `--port`, `--web`, `--selftest`, `--help`
C++: `-w`, `-s` (getopt short options only)

C++ also doesn't support `--ip` or `--port` for Modbus configuration.

### 17. Self-Test Not Integrated

`src/selftest.c` and `src/selftest.h` are not included in the C++ build.
C++ self-test only tests CRC16 and Modbus connect/disconnect, missing the 6
real register tests. CLI also changed from `--selftest` to `-s`.

---

## Test Plan

### Test 1: Startup and Shutdown (Control Loop Running)

**Objective**: Verify the application starts, control loop runs, and shuts down cleanly.

**Steps**:
1. Start application with `./windmi-control`
2. Verify lock file created at `/tmp/windmi-controller.lock`
3. Verify Modbus connection is established (log message)
4. Verify control loop thread starts and reads status (30-second interval)
5. Verify web server starts on port 8080
6. Hit `/api/status` and verify JSON response with real values
7. Verify second instance is blocked (lock file)
8. Send SIGINT (Ctrl+C)
9. Verify control loop stops
10. Verify OFF mode written to REG_RUNNING_MODE (0x002C = 0)
11. Verify lock file released
12. Verify clean exit (exit code 0)

**Expected log output** (matching master):
```
[Main] Lock acquired (PID: <pid>)
[Main] Rotenso Windmi Controller
[Main] Modbus gateway: 192.168.123.10:8899
[Main] Web server: 0.0.0.0:8080
[Main] Server started. Press Ctrl+C to stop.
[Main] Shutting down...
[Shutdown] Control loop stopped
[Shutdown] Drain period complete
[Shutdown] Writing OFF mode via dedicated client...
[Shutdown] OFF write OK (attempt 1)
[Main] Goodbye!
[Main] Lock released
```

**Pass Criteria**:
- Modbus connection succeeds
- Status readings populated (not all zeros)
- Control loop polls at 30-second interval
- Only one instance can run (lock prevents duplicates)
- Shutdown writes OFF mode
- Lock released on exit
- No crashes or hangs

---

### Test 2: Register Read/Write

**Objective**: Verify all Modbus registers read and write correctly.

**Registers read by master's `read_status()`** (must all work):

| Register | Address | C++ readStatus reads it? |
|----------|---------|--------------------------|
| REG_OUTDOOR_TEMP | 0x0001 | ❌ |
| REG_INDOOR_TEMP | 0x0002 | ❌ |
| REG_LEAVING_WATER_TEMP | 0x0004 | ❌ (hardcoded to heating_temp) |
| REG_DHW_TANK_TEMP | 0x00CE | ❌ (uses 0x0012 instead) |
| REG_RUNNING_MODE | 0x002C | ✅ |
| REG_RUNNING_STATUS | 0x002D | ✅ |
| REG_DHW_TARGET | 0x0194 | ❌ |
| REG_HEATING_TARGET | 0x0191 | ❌ |
| REG_DHW_PRIORITY | 0x02BF | ✅ |
| REG_AC_CURRENT | 0x1014 | ✅ (but wrong scaling) |
| REG_DC_CURRENT | 0x1015 | ✅ (but wrong scaling) |
| REG_AC_VOLTAGE | 0x1016 | ✅ |
| REG_DC_VOLTAGE | 0x1017 | ✅ |

**Registers written by master** (must all work):

| Register | Address | Written by |
|----------|---------|------------|
| REG_RUNNING_MODE | 0x002C | set_running_mode(), process_commands() |
| REG_DHW_TARGET | 0x0194 | set_dhw_target(), process_commands() |
| REG_HEATING_TARGET | 0x0191 | set_heating_target(), process_commands() |
| REG_DHW_PRIORITY | 0x02BF | process_commands(), apply_control_logic() |

**Power scaling verification**:
- `ac_current` = raw × 2.0 (not raw / 2.0)
- `dc_current` = raw × 4.0 (not raw / 4.0)
- `ac_voltage` = raw × 1.0
- `dc_voltage` = raw / 2.0
- `ac_power` = `ac_voltage × ac_current` (not 0.0)

**Pass Criteria**:
- All 13 register reads succeed
- All 4 register writes succeed
- Power scaling factors correct
- No fabricated addresses (0x0012, 0x0014) referenced

---

### Test 3: Self-Test Execution

**Objective**: Run the full self-test matching master's implementation.

**Steps**:
1. Run `./windmi-control --selftest`
2. Verify all 6 tests execute:
   - Test 1: Read REG_DEVICE_TYPE (0x1006) — expect value 8
   - Test 2: Read REG_HEATING_TARGET (0x0191)
   - Test 3: Read REG_DHW_TARGET (0x0194)
   - Test 4: Read REG_OUTDOOR_TEMP (0x0001)
   - Test 5: Read REG_DHW_TANK_TEMP (0x00CE)
   - Test 6: Write-then-verify REG_HEATING_TARGET (0x0191)
3. Verify write-verify restores original value
4. Verify formatted report output
5. Verify exit code 0 on pass, 1 on fail

**Expected output** (matching master):
```
========== Self-Test Report ==========
Test                   Address    Read  Write  Verify      Value
----------------------------------------------------------------
Device Type            0x1006     OK     OK     OK          8
Heating Setpoint      0x0191     OK     OK     OK        <value>
DHW Setpoint          0x0194     OK     OK     OK        <value>
Outdoor Temp          0x0001     OK     OK     OK        <value>
DHW Tank Temp         0x00CE     OK     OK     OK        <value>
Write Verify Test     0x0191     OK     OK     OK        <value>
----------------------------------------------------------------
Summary:
  Total tests: 6
  Passed: 6
  Failed: 0
  All critical passed: YES
========================================
Self-test: 6/6 registers passed
Self-test PASSED
```

**Pass Criteria**:
- All 6 tests pass with Read OK, Write OK, Verify OK
- Original heating target restored after write test
- CLI uses `--selftest` (not `-s`)
- Exit code 0

---

### Test 4: Web API Endpoints

**Objective**: Verify all web API endpoints match master's behavior.

**Endpoints** (verified from master's `http_handler`):

| Endpoint | Method | Request Body | Success | Validation Error |
|----------|--------|-------------|---------|------------------|
| `/api/status` | GET | none | 200, JSON | — |
| `/api/set-dhw` | POST | `{"temperature":<float>}` | 202 | 400/422 |
| `/api/set-heating` | POST | `{"temperature":<float>}` | 202 | 400/422 |
| `/api/set-priority` | POST | `{"priority":"dhw"\|"heating"}` | 202 | 400/422 |
| `/api/set-mode` | POST | `{"mode":<int>}` | 202 | 400 only when body is empty or mode is missing |
| `/*` | GET | none | Static file | — |

**`/api/status` JSON format** (verified from master):
```json
{
  "dhwTemperature": 48.0,
  "dhwTarget": 46.0,
  "heatingTemperature": 35.2,
  "heatingTarget": 45.0,
  "outdoorTemperature": 25.0,
  "leavingWaterTemperature": 35.2,
  "mode": "heat+dhw",
  "runningStatus": "heat",
  "priority": "dhw",
  "status": "running",
  "deviceOnline": true,
  "acCurrent": 3.40,
  "dcCurrent": 0.50,
  "acVoltage": 230.0,
  "dcVoltage": 12.0,
  "acPower": 782.0,
  "workingMode": 3
}
```

**Validation rules** (from master):
- `/api/set-dhw`: temperature must be in [DHW_TEMP_MIN, DHW_TEMP_MAX] = [40, 63]
- `/api/set-heating`: temperature must be in [HEATING_TEMP_MIN, HEATING_TEMP_MAX] = [25, 63]
- `/api/set-priority`: must be "dhw" or "heating" string
- `/api/set-mode`: master expects working mode values 0, 1, 2, or 3, but only validates that `mode` is present. Out-of-range values are still queued and later handled by the control loop default branch.
- Empty body → 400
- During shutdown → 503

**Method checking** (from master):
- GET on `/api/status` → 200
- POST on `/api/set-*` → process
- Wrong method on any API → 405

**Static files** (from master):
- All non-API paths served from `static/` directory
- Used by the web UI (`static/index.html`, `static/app.js`, `static/style.css`)

**CORS headers** (from master):
- `Access-Control-Allow-Origin: *` on all JSON responses

**Pass Criteria**:
- All endpoints return correct status codes
- JSON response format matches master exactly (same field names, same types)
- Commands queued to cmd queue (not just printed)
- Static file serving works for web UI
- CORS headers present
- Wrong methods → 405
- During shutdown → 503
- Empty body → 400
- Temperature out-of-range → 422

---

### Test 5: Control Loop Logic

**Objective**: Verify control loop logic matches master.

**Scenario 5a: Set DHW temperature**
- POST `{"temperature":50.0}` to `/api/set-dhw`
- Command queued as `CMD_SET_DHW_TEMP`
- Control loop writes REG_DHW_TARGET (0x0194) = 500
- `saved_dhw_target` updated to 50.0
- Verify via `/api/status` → `"dhwTarget":50.0`

**Scenario 5b: Set heating temperature**
- POST `{"temperature":40.0}` to `/api/set-heating`
- Command queued as `CMD_SET_HEATING_TEMP`
- Control loop writes REG_HEATING_TARGET (0x0191) = 400
- `saved_heating_target` updated to 40.0
- Verify via `/api/status` → `"heatingTarget":40.0`

**Scenario 5c: Set DHW-only mode (working mode 1)**
- POST `{"mode":1}` to `/api/set-mode`
- Command queued as `CMD_SET_RUNNING_MODE (int_val=1)`
- Control loop:
  - Writes REG_RUNNING_MODE (0x002C) = 2 (MODE_SET_HEAT_DHW)
  - Writes REG_HEATING_TARGET (0x0191) = 250 (25°C minimum)
  - Writes REG_DHW_PRIORITY (0x02BF) = 1
- On subsequent polls, enforces heating_target stays at 25°C

**Scenario 5d: Set Heating-only mode (working mode 2)**
- POST `{"mode":2}` to `/api/set-mode`
- Command queued as `CMD_SET_RUNNING_MODE (int_val=2)`
- Control loop:
  - Writes REG_RUNNING_MODE (0x002C) = 2 (MODE_SET_HEAT_DHW)
  - Writes REG_DHW_TARGET (0x0194) = 400 (40°C minimum)
  - Writes REG_DHW_PRIORITY (0x02BF) = 0
- On subsequent polls, enforces dhw_target stays at 40°C

**Scenario 5e: Set DHW+Heating mode (working mode 3)**
- POST `{"mode":3}` to `/api/set-mode`
- Command queued as `CMD_SET_RUNNING_MODE (int_val=3)`
- Control loop:
  - Writes REG_RUNNING_MODE (0x002C) = 2 (MODE_SET_HEAT_DHW)
  - Restores saved_dhw_target and saved_heating_target
  - Writes REG_DHW_PRIORITY (0x02BF) = 1
- On subsequent polls, restores targets if drifted > 0.5°C

**Scenario 5f: Set OFF mode (working mode 0)**
- POST `{"mode":0}` to `/api/set-mode`
- Command queued as `CMD_SET_RUNNING_MODE (int_val=0)`
- Control loop writes REG_RUNNING_MODE (0x002C) = 0 (MODE_SET_OFF)
- No target or priority changes

**Scenario 5g: Priority change**
- POST `{"priority":"heating"}` to `/api/set-priority`
- Command queued as `CMD_SET_PRIORITY (int_val=0)`
- Control loop writes REG_DHW_PRIORITY (0x02BF) = 0

**Scenario 5h: Mode drift correction**
- In DHW+Heating mode, if read-back shows targets drifted > 0.5°C,
  control loop re-writes saved targets on next poll
- In DHW-only mode, if heating_target > HEATING_TARGET_MIN + 0.5,
  control loop re-writes 25°C
- In Heating-only mode, if dhw_target > DHW_TARGET_MIN + 0.5,
  control loop re-writes 40°C

**Pass Criteria**:
- Device mode register only written with values 0, 1, 2
- Targets adjusted per working mode
- Priority enforced per working mode
- Saved targets restored on mode switch to 3
- Drift correction applied within 30-second poll interval
- Commands processed immediately (kick mechanism)

---

### Test 6: Signal Handling and Shutdown

**Objective**: Verify clean shutdown on SIGINT/SIGTERM.

**Steps**:
1. Start application
2. Verify lock file exists
3. Send SIGINT (`kill -INT <pid>`)
4. Verify offline mode written: REG_RUNNING_MODE = 0
5. Verify lock file released
6. Verify clean exit (code 0)

**Shutdown sequence** (from master):
1. Signal handler calls `web_server_stop()` → sets `g_running=false`
2. `web_server_run()` loop exits → returns to `main()`
3. `control_loop_stop()` → sets `stop_requested=true`, joins thread
4. Drain period: `usleep(150000)` (150ms)
5. Create dedicated shutdown Modbus client
6. Write REG_RUNNING_MODE = 0 with up to 3 retries (100ms between)
7. Destroy shutdown client
8. Destroy main Modbus client
9. `release_lock()`
10. Exit

**Pass Criteria**:
- Shutdown completes within 5 seconds
- OFF mode written before exit
- Lock file released
- No crashes or hangs
- Exit code 0

---

### Test 7: Concurrent Requests

**Objective**: Verify web server handles concurrent requests.

**Steps**:
1. Start application
2. Send 10 concurrent `GET /api/status` requests
3. Verify all complete with valid JSON
4. Send 5 concurrent POST requests
5. Verify commands queued in FIFO order

**Pass Criteria**:
- All responses complete
- No JSON parsing errors
- Commands queued in order (SPSC FIFO guarantee)

---

### Test 8: Error Handling

**Objective**: Verify error handling for invalid inputs.

| Scenario | Expected |
|----------|----------|
| DHW temp 100°C (above max 63) | 422 |
| DHW temp 20°C (below min 40) | 422 |
| Heating temp 100°C (above max 63) | 422 |
| Heating temp 10°C (below min 25) | 422 |
| Missing temperature param | 400 |
| Empty POST body | 400 |
| Invalid JSON | 400 |
| Invalid priority string | 422 |
| Wrong HTTP method on API | 405 |
| Request during shutdown | 503 |
| Modbus timeout | Log error, publish offline status |
| Modbus disconnect | Reconnect (3 retries, 10s interval) |
| Second instance launch | Refused (lock file) |

**Pass Criteria**:
- Correct HTTP status codes returned
- No crashes
- Error messages logged
- Reconnection attempted on Modbus failure

---

## Implementation Plan

### Phase 1: Fix `include/config.h`

1. Remove fabricated defines: `REG_DHW_TEMP`, `REG_HEATING_TEMP`, `REG_AC_POWER`
2. Remove application-level mode defines: `MODE_OFF`, `MODE_DHW_ONLY`,
   `MODE_HEATING_ONLY`, `MODE_DHW_HEATING` (these are not config constants)
3. Add all missing register defines from master (see Root Cause §1)
4. Add `MODE_SET_*` and `MODE_STATUS_*` defines from master
5. Add all missing configuration constants from master
6. Remove duplicate defines (master has duplicate REG_AC_CURRENT etc.; don't copy that bug)
7. Keep `#ifdef __cplusplus extern "C" { #endif` guard for C++ compatibility

### Phase 2: Fix `StatusSnapshot` Structure

File: `include/core/ControlLoop.hpp`

1. Add missing fields: `dhw_target`, `heating_target`, `indoor_temp`
2. Remove fabricated field `heating_temperature`
3. Rename `mode` → `running_mode` (match master)
4. Rename `priority` (int) → `dhw_priority` (bool) (match master)
5. Rename `status` (int) → `is_running` (bool) (match master)

### Phase 3: Fix `ControlLoop` Implementation

Files: `src/core/ControlLoop.cpp`, `include/core/ControlLoop.hpp`

1. Constructor must accept `ModbusClient*` (or `start()` must accept it)
2. Add SPSC queue references (cmd queue for reading, status queue for writing)
3. Replace `StatusCallback` with queue-based communication
4. Rewrite `readStatus()` to read all 13 registers from master with correct
   addresses and scaling
5. Implement `process_commands()` matching master's 4 command types
6. Rewrite `applyControlLogic()` matching master's target/priority enforcement
7. Add state tracking: `current_mode`, `desired_working_mode`, `current_priority`,
   `saved_dhw_target`, `saved_heating_target`, `saved_targets_initialized`
8. Change poll interval from 500ms to 30 seconds
9. Add kick mechanism (`std::condition_variable`) for immediate command processing
10. Add reconnection logic (3 retries, 10s interval)

### Phase 4: Rewrite Web Server

Files: `src/web/WebServer.cpp`, `include/web/WebServer.hpp`

Complete rewrite. Current implementation is a stub with no functionality.

1. Accept cmd queue and status queue in constructor
2. Register mongoose event handler with `mg_http_listen()`
3. Implement `http_handler()` with routing for all 5 API endpoints + static files
4. Implement `api_status_handler()` with exact JSON format from master
5. Implement `api_set_dhw_handler()`, `api_set_heating_handler()`,
   `api_set_priority_handler()`, `api_set_mode_handler()` with validation
6. Add method checking (405 for wrong methods)
7. Add shutdown rejection (503 during shutdown)
8. Add CORS headers
9. Run `mg_mgr_poll()` in blocking loop
10. Implement `web_server_stop()` to exit poll loop

### Phase 5: Fix `main.cpp`

1. Add instance lock (`flock()` on `/tmp/windmi-controller.lock`)
2. Fix CLI parsing: `--ip`, `--port`, `--web`, `--selftest`, `--help`
3. Fix `MODBUS_SLAVE_ID`: use config define (11), not hardcoded 1
4. Create SPSC queues and pass to ControlLoop and WebServer
5. Pass ModbusClient to ControlLoop
6. Fix startup sequence matching master
7. Fix shutdown sequence matching master (web server stops first, then control loop)
8. Integrate `selftest_run()` and `selftest_print_report()` for `--selftest`
9. Use config defines for all constants

### Phase 6: Integrate Self-Test

1. Add `src/selftest.h` to include path with `extern "C"` guard
2. Add `src/selftest.c` to CMake build
3. Call `selftest_run()` and `selftest_print_report()` from main on `--selftest`

### Phase 7: Update Unit Tests

1. Fix StatusSnapshot structure in tests
2. Fix config register addresses in tests
3. Add power scaling test (×2, ×4, not /2, /4)
4. Add control loop command processing test
5. Add web server API routing test
6. Add config address consistency test (C++ addresses match master)

---

## Files Requiring Changes

| File | Change | Priority |
|------|--------|----------|
| `include/config.h` | Remove fabricated, add missing regs/modes/config | Critical |
| `include/core/ControlLoop.hpp` | Fix StatusSnapshot, add ModbusClient/queue refs | Critical |
| `src/core/ControlLoop.cpp` | Complete rewrite (reads, writes, commands, logic) | Critical |
| `src/web/WebServer.cpp` | Complete rewrite (API, static, queues, poll) | Critical |
| `include/web/WebServer.hpp` | Add queue/refs interface | Critical |
| `src/main.cpp` | Add lock, CLI, queues, startup/shutdown sequence | Critical |
| `include/selftest.h` | Add extern "C" guard | Critical |
| `src/selftest.c` | Integrate into CMake build | Critical |
| `src/modbus/ModbusClient.cpp` | Verify read/write correctness | High |
| `include/utils/SpscQueue.hpp` | Add `latest()` method for status drain | High |
| `tests/` | Update to match new structures | High |

---

## Success Criteria

1. [ ] `include/config.h` matches master's register addresses and mode constants
2. [ ] No fabricated register addresses remain (0x0012, 0x0014, 0x1018)
3. [ ] StatusSnapshot fields match master's `status_snapshot_t`
4. [ ] `--selftest` runs all 6 tests and passes
5. [ ] Startup creates lock, connects Modbus, starts control loop and web server
6. [ ] Control loop reads 13 registers at correct addresses
7. [ ] Control loop processes 4 command types with correct register writes
8. [ ] Control loop enforces targets and priority per working mode
9. [ ] Control loop polls at 30-second interval
10. [ ] Control loop reconnects on connection failure (3 retries, 10s)
11. [ ] Commands wake control loop immediately
12. [ ] Web server serves 5 API endpoints + static files
13. [ ] JSON response format matches master exactly
14. [ ] Power scaling correct (AC×2, DC×4, power=V×A)
15. [ ] Instance lock prevents duplicate processes
16. [ ] Shutdown writes OFF mode with 3 retries
17. [ ] Lock released on exit
18. [ ] CLI args compatible with master (`--ip`, `--port`, `--selftest`)
19. [ ] MODBUS_SLAVE_ID = 11 (not 1)
20. [ ] All `ctest` tests pass