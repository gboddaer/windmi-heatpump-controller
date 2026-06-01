# Test Plan & Fix Implementation Plan

## Overview

The C++ conversion has **broken functionality**. The C implementation (`src/main.c`, `src/control_loop.c`) is production-ready with proper Modbus communication, control logic, and shutdown sequence. The C++ version (`src/main.cpp`, `src/core/ControlLoop.cpp`) is incomplete and non-functional.

## Root Causes Identified

### 1. Missing Register Definitions in `include/config.h`

The C++ config is missing many register definitions present in master branch:

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

**Missing Mode Constants**:
- `MODE_SET_OFF` (0), `MODE_SET_COOL_DHW` (1), `MODE_SET_HEAT_DHW` (2)
- `MODE_STATUS_OFF` (0), `MODE_STATUS_COOL` (1), `MODE_STATUS_HEAT` (2)
- `MODE_STATUS_DHW` (4), `MODE_STATUS_DEFROST` (7), `MODE_STATUS_ANTIFREEZE` (20)

**Missing Configuration**:
- `SELFTEST_DHW_TARGET_TEMP` (45.0f)

### 2. Incomplete Control Loop Implementation

**Current C++ Status**:
- `readStatus()` only reads 2 registers (DHW temp, heating temp) instead of 10+ registers
- No `applyControlLogic()` implementation (no actual control decisions)
- No command queue processing (commands are just logged, not sent to Modbus)
- No priority switching logic (DHW vs Heating priority)
- No target temperature management (no saved/restored targets)
- No hysteresis-based mode switching
- No working mode state tracking

**Required C++ Implementation**:
- Read all required Modbus registers
- Process commands from queue and write to Modbus
- Implement priority-based control logic
- Track saved targets for mode switching
- Apply hysteresis to prevent rapid mode switching
- Handle DHW priority mode (set heating to minimum)
- Handle heating priority mode (set DHW to minimum)

### 3. Missing Self-Test Integration

**Current C++ Status**:
- `selftest_run()` is in C (`src/selftest.c`) but not integrated into C++ build
- `selftest_report_t` and `selftest_result_t` structures missing
- C++ self-test only verifies CRC16 and ModbusClient connect/disconnect
- No integration of full register read/write/verify tests

**Required C++ Implementation**:
- Include `selftest.c` in build
- Add `selftest_report_t` and `selftest_result_t` to C++ namespace or keep in C
- Integrate self-test into `run_selftest()` in main.cpp

### 4. Missing Shutdown Sequence

**Current C++ Status**:
- Main.cpp has shutdown code but may not work correctly
- No verified dedicated shutdown Modbus client pattern
- May not wait for control loop to fully stop before writing OFF mode

**Required C++ Implementation**:
- Follow master branch shutdown sequence exactly:
  1. Stop control loop
  2. Wait 150ms drain period
  3. Create dedicated shutdown Modbus client
  4. Write OFF mode (register 0x002C = 0) with retry logic
  5. Destroy shutdown client
  6. Stop web server
  7. Exit

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

---

### Test 2: Modbus Register Read/Write

**Objective**: Verify all required Modbus registers can be read and written.

**Registers to Test**:

| Register | Address | Access | Description |
|----------|---------|--------|-------------|
| REG_DEVICE_TYPE | 0x1006 | R | Device type identifier |
| REG_OUTDOOR_TEMP | 0x0001 | R | Outdoor temperature (0.1°C) |
| REG_DHW_TEMP | 0x0012 | R | DHW temperature (0.1°C) |
| REG_HEATING_TEMP | 0x0014 | R | Heating temperature (0.1°C) |
| REG_DHW_TANK_TEMP | 0x00CE | R | DHW tank temperature (0.1°C) |
| REG_LEAVING_WATER_TEMP | 0x0004 | R | Leaving water temperature (0.1°C) |
| REG_RUNNING_STATUS | 0x002D | R | Current running status |
| REG_AC_CURRENT | 0x1014 | R | AC current (Display * 2 = Actual Amps) |
| REG_DC_CURRENT | 0x1015 | R | DC current (Display * 4 = Actual Amps) |
| REG_AC_VOLTAGE | 0x1016 | R | AC voltage (Display = Actual Volts) |
| REG_DC_VOLTAGE | 0x1017 | R | DC voltage (Display / 2 = Actual Volts) |
| REG_AC_POWER | 0x1018 | R | AC Power (W) = AC Voltage × AC Current × 2 |
| REG_RUNNING_MODE | 0x002C | W | Set mode: 0=Off, 1=Cool+DHW, 2=Heat+DHW |
| REG_HEATING_TARGET | 0x0191 | RW | Heating target temperature (0.1°C) |
| REG_DHW_TARGET | 0x0194 | RW | DHW target temperature (0.1°C) |

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

**Pass Criteria**:
- All registers return valid values
- Write operations succeed
- No timeout errors
- Values are in expected ranges

---

### Test 3: Self-Test Execution

**Objective**: Run the full self-test and verify all tests pass.

**Steps**:
1. Run `./windmi-control -s` (selftest mode)
2. Verify self-test runs all 6 tests
3. Verify read/write/verify for each register
4. Verify original values are restored after write tests

**Expected Test Output**:
```
[SelfTest] Starting self-test...
[SelfTest] Testing CRC16...
[SelfTest] CRC16 result: 0x0616
[SelfTest] Testing ModbusClient...
[SelfTest] Device type = 8
[SelfTest] Heating setpoint = 45.0 C
[SelfTest] DHW setpoint = 46.0 C
[SelfTest] Outdoor temp = 25.0 C
[SelfTest] DHW tank temp = 48.0 C
[SelfTest] Write verify passed (heating setpoint = 45.0 C)
[SelfTest] Restored heating setpoint to 45.0 C
[SelfTest] All tests passed!
Self-test: 6/6 registers passed
Self-test PASSED
```

**Pass Criteria**:
- All 6 tests pass
- Read OK, Write OK, Verify OK for all registers
- Original values restored after write tests
- No errors in output

---

### Test 4: Web API Endpoints

**Objective**: Verify all web API endpoints respond correctly.

**Endpoints to Test**:

| Endpoint | Method | Description | Expected Response |
|----------|--------|-------------|-------------------|
| `/api/status` | GET | Returns current status as JSON | 200 OK, JSON body |
| `/api/setDhwTemperature` | POST | Sets DHW target temperature | 202 Accepted, JSON body |
| `/api/setHeatingTemperature` | POST | Sets heating target temperature | 202 Accepted, JSON body |
| `/api/setMode` | POST | Sets running mode (0/1/2) | 202 Accepted, JSON body |
| `/api/setPriority` | POST | Sets priority (0=Auto, 1=DHW) | 202 Accepted, JSON body |
| `/api/shutdown` | GET | Initiates graceful shutdown | 200 OK, JSON body |

**Test Steps**:
1. Start application
2. Test each endpoint with valid inputs
3. Test each endpoint with invalid inputs (out of range, missing parameters)
4. Test shutdown endpoint and verify application exits

**Pass Criteria**:
- All endpoints return appropriate status codes (200, 202, 400, 422, 503)
- JSON responses match expected format
- Commands are queued and processed by control loop
- Invalid inputs return appropriate error responses

---

### Test 5: Control Loop Logic

**Objective**: Verify control loop applies correct logic.

**Scenarios to Test**:

**Scenario 5a: DHW Priority Mode**
- Set priority to DHW (priority=1)
- Verify heating target is set to minimum (25°C) when DHW priority is active
- Verify DHW temperature is maintained at target

**Scenario 5b: Heating Priority Mode**
- Set priority to Heating (priority=0)
- Verify DHW target is set to minimum (40°C) when heating priority is active
- Verify heating temperature is maintained at target

**Scenario 5c: Mode Switching**
- Set working mode to OFF, then to DHW+Heating
- Verify saved targets are restored (not default values)

**Scenario 5d: Hysteresis**
- Set heating target to 45°C
- Verify heating doesn't switch on/off rapidly when temperature is near target
- Hysteresis should be 1°C for heating, 3°C for DHW

**Pass Criteria**:
- Priority switching works correctly
- Saved targets are maintained across mode changes
- Hysteresis prevents rapid switching
- Control logic produces expected temperature setpoints

---

### Test 6: Signal Handling

**Objective**: Verify graceful shutdown on SIGINT/SIGTERM.

**Steps**:
1. Start application
2. Send SIGINT (`kill -INT <pid>`)
3. Verify control loop stops (log message appears)
4. Verify web server stops (log message appears)
5. Verify OFF mode written (log message appears)
6. Verify clean exit with exit code 0

**Alternative Test**: Send SIGTERM (`kill -TERM <pid>`)

**Pass Criteria**:
- No hangs during shutdown
- OFF mode written before exit
- No core dumps
- Exit code 0

---

### Test 7: Multiple Concurrent Requests

**Objective**: Verify web server handles concurrent requests without data corruption.

**Steps**:
1. Start application
2. Send 10 concurrent GET `/api/status` requests using `curl -&`
3. Verify all responses complete and contain valid JSON
4. Send 5 concurrent POST requests to set temperatures rapidly
5. Verify commands are queued in order

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
| Invalid temperature (e.g., 100°C, above max 63°C) | Return 422 Unprocessable Entity |
| Temperature below minimum (e.g., 20°C, below min 40°C) | Return 422 Unprocessable Entity |
| Missing temperature parameter | Return 400 Bad Request |
| Empty POST body | Return 400 Bad Request |
| Invalid JSON in POST body | Return 400 Bad Request |
| Network disconnect during request | Clean connection close, no crash |
| Modbus timeout | Retry up to 3 times, then return error |

**Pass Criteria**:
- Appropriate error codes returned (400, 422, 500)
- No crashes or memory leaks
- Error messages logged appropriately

---

## Implementation Plan

### Phase 1: Fix Configuration (Critical)

**File**: `include/config.h`

**Changes Required**:
1. Add all missing register definitions from master branch
2. Add mode status constants (MODE_STATUS_*)
3. Add self-test configuration
4. Ensure C++ compatible (keep `#define` macros)

**Files to Reference**:
- `src/config.h` from master branch

---

### Phase 2: Integrate Self-Test (Critical)

**Files**: `src/selftest.c`, `src/selftest.h`

**Changes Required**:
1. Add `src/selftest.c` to CMakeLists.txt in src/ or create separate target
2. Add `#include "selftest.h"` to main.cpp
3. Update `run_selftest()` to call `selftest_run()` and `selftest_print_report()`
4. Add selftest structures to C++ namespace or keep in C

---

### Phase 3: Fix ControlLoop Implementation (Critical)

**File**: `src/core/ControlLoop.cpp`

**Changes Required**:

1. **Update `readStatus()` to read all registers**:
   ```cpp
   bool ControlLoop::readStatus(StatusSnapshot& snapshot) {
       // Read all 15+ registers from master branch
       // Temperature registers (divide by 10.0f)
       // Current/Voltage/Power registers
       // Running mode and status
       // Priority and working mode
   }
   ```

2. **Implement command queue processing**:
   ```cpp
   // Process commands from queue before applying control logic
   // Check if queue has pending commands
   // Write to Modbus based on command type
   ```

3. **Implement `applyControlLogic()`**:
   - Check DHW priority mode
   - Set heating target to minimum if DHW priority
   - Set DHW target to minimum if heating priority
   - Apply hysteresis for stable mode switching
   - Track and save targets for mode switching

4. **Add state tracking**:
   - Current device mode
   - Desired working mode
   - Saved DHW target
   - Saved heating target
   - Priority mode

---

### Phase 4: Verify Shutdown Sequence (Critical)

**File**: `src/main.cpp`

**Changes Required**:
1. Ensure shutdown client pattern matches master branch exactly
2. Verify OFF mode write succeeds
3. Ensure proper ordering: control loop stop → drain → shutdown client → web server stop

**Verify**:
- Shutdown client creates fresh Modbus connection
- OFF mode (register 0x002C = 0) is written
- Retry logic (up to 3 attempts) is implemented
- Drain period (150ms) is observed

---

### Phase 5: Update Tests (High Priority)

**Files**: `tests/`

**Changes Required**:
1. Update test_modbus_client.cpp to test register read/write
2. Update test_control_loop.cpp to test command processing
3. Add test for control loop logic (priority switching, hysteresis)
4. Add test for shutdown sequence

---

## Immediate Verification Steps

### Step 1: Build and Verify No Compile Errors

```bash
cd build
cmake ..
make clean
make
```

**Expected**: No warnings or errors

---

### Step 2: Run Self-Test

```bash
cd build
./windmi-control -s
```

**Expected**: 6/6 tests pass

---

### Step 3: Start Application and Check Status

```bash
cd build
./windmi-control &
sleep 2
curl http://localhost:8080/api/status
kill %1
```

**Expected**: Valid JSON with populated temperature values

---

### Step 4: Full Startup/Shutdown Test

```bash
cd build
timeout 10 ./windmi-control || true
```

**Expected**: Clean shutdown with OFF mode written

---

## Files Requiring Changes

| File | Change Type | Priority |
|------|-------------|----------|
| `include/config.h` | Add missing constants | Critical |
| `src/selftest.c` | Already exists, integrate into build | Critical |
| `src/selftest.h` | Already exists, integrate into build | Critical |
| `src/core/ControlLoop.cpp` | Rewrite readStatus(), implement command processing | Critical |
| `src/main.cpp` | Verify shutdown sequence | Critical |
| `src/modbus/ModbusClient.cpp` | Verify read/write methods | High |
| `tests/modbus/test_modbus_client.cpp` | Add register read/write tests | High |
| `tests/core/test_control_loop.cpp` | Add control logic tests | High |

---

## Success Criteria

The C++ conversion is complete when:

1. [ ] All register definitions match master branch config.h
2. [ ] Self-test runs and passes all 6 tests
3. [ ] Startup/shutdown works with control loop running
4. [ ] All Modbus registers can be read and written
5. [ ] Control loop processes commands and applies logic
6. [ ] Priority switching works correctly
7. [ ] Hysteresis prevents rapid mode switching
8. [ ] Saved targets are maintained across mode changes
9. [ ] Web API endpoints return correct responses
10. [ ] All tests pass with `ctest`

---

## Notes

- Master branch (`src/main.c`, `src/control_loop.c`) is the source of truth for functionality
- All C++ implementation must match C behavior exactly
- Modbus register addresses are in hexadecimal (0x prefix)
- Temperature values are in 0.1°C units (divide by 10.0f)
- Current values require scaling (AC * 2, DC * 4)
- The self-test writes test values and restores originals
