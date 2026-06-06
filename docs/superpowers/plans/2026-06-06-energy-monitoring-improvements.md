# Energy Monitoring Improvements Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix power monitoring issues: rename VA→W mislabel, add entering water temp, add COP/energy fields, harden power reads, add diagnostic registers, rename REG_DEVICE_TYPE→REG_UNIT_CAPACITY.

**Architecture:** Extends `StatusSnapshot` with new fields, adds individual try/catch per power register, adds powerVA/powerW distinction with assumed power factor, adds entering water temp read, adds water flow + compressor frequency reads. No new classes — keeps changes in existing `ControlLoop`, `config.h`, `JsonHelpers`, and `WebServer`.

**Tech Stack:** C++17, CMake, Google Test, Mongoose web server, Modbus RTU over TCP

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `include/config.h` | Modify | Add new register defines, rename REG_DEVICE_TYPE |
| `include/core/ControlLoop.hpp` | Modify | Add new fields to StatusSnapshot |
| `src/core/ControlLoop.cpp` | Modify | Read new registers, fix power calc, individual try/catch |
| `include/utils/JsonHelpers.hpp` | Modify | Add new parameters to generateStatusJson |
| `src/utils/JsonHelpers.cpp` | Modify | Add new JSON fields |
| `src/web/WebServer.cpp` | Modify | Add new fields to JSON response |
| `tests/core/test_control_loop.cpp` | Modify | Add tests for new fields and scaling |

---

### Task 1: Rename ac_power to ac_power_va, add ac_power_w with power factor

**Files:**
- Modify: `include/core/ControlLoop.hpp`
- Modify: `tests/core/test_control_loop.cpp`

- [ ] **Step 1: Add ac_power_va and ac_power_w fields to StatusSnapshot, add POWER_FACTOR constant**

In `include/core/ControlLoop.hpp`, change the power monitoring section of `StatusSnapshot` from:

```cpp
    // Power monitoring
    float ac_current = 0.0f;     // AC current in Amps (raw * 2)
    float dc_current = 0.0f;     // DC current in Amps (raw * 4)
    float ac_voltage = 0.0f;     // AC voltage in Volts (raw)
    float dc_voltage = 0.0f;     // DC voltage in Volts (raw / 2)
    float ac_power = 0.0f;       // AC power in Watts (ac_voltage * ac_current)
```

to:

```cpp
    // Power monitoring
    float ac_current = 0.0f;     // AC current in Amps (raw * 2)
    float dc_current = 0.0f;     // DC current in Amps (raw * 4)
    float ac_voltage = 0.0f;     // AC voltage in Volts (raw)
    float dc_voltage = 0.0f;     // DC voltage in Volts (raw / 2)
    float ac_power_va = 0.0f;    // AC apparent power in VA (ac_voltage * ac_current)
    float ac_power_w = 0.0f;    // AC real power in Watts (estimated: VA * power_factor)
    bool power_valid = false;    // True if at least one power register read succeeded
```

Also add this constant below `#define DHW_HYSTERESIS_C`:

```cpp
// Power factor assumption for inverter-driven compressor
// Typical range: 0.85-0.95 for inverter heat pumps
#define ESTIMATED_POWER_FACTOR  0.90f
```

- [ ] **Step 2: Update tests to match new field names**

In `tests/core/test_control_loop.cpp`, change the `DefaultValues` test:

```cpp
    EXPECT_FLOAT_EQ(snap.ac_power_va, 0.0f);
    EXPECT_FLOAT_EQ(snap.ac_power_w, 0.0f);
    EXPECT_FALSE(snap.power_valid);
```

Replace the `AcPowerIsVoltageTimesCurrent` test:

```cpp
TEST(PowerScalingTest, AcPowerVaIsVoltageTimesCurrent) {
    // Apparent power: VA = V × A
    float ac_voltage = 230.0f;
    float ac_current = 10.0f;
    float ac_power_va = ac_voltage * ac_current;
    EXPECT_FLOAT_EQ(ac_power_va, 2300.0f);
}

TEST(PowerScalingTest, AcPowerWEstimatedWithPowerFactor) {
    // Real power: W = VA × PF
    float ac_power_va = 2300.0f;
    float ac_power_w = ac_power_va * ESTIMATED_POWER_FACTOR;
    EXPECT_NEAR(ac_power_w, 2070.0f, 1.0f);
}
```

- [ ] **Step 3: Build and run tests**

Run: `cd /home/gbo/develop/wpomp && cmake --build build --target test_control_loop 2>&1 | tail -5`

Then: `cd /home/gbo/develop/wpomp/build && ./tests/core/test_control_loop`

Expected: All tests PASS. The old `ac_power` field no longer exists, replaced by `ac_power_va` and `ac_power_w`.

- [ ] **Step 4: Commit**

```bash
git add include/core/ControlLoop.hpp tests/core/test_control_loop.cpp
git commit -m "refactor: rename ac_power to ac_power_va, add ac_power_w with power factor"
```

---

### Task 2: Fix power register reads — individual try/catch per register

**Files:**
- Modify: `src/core/ControlLoop.cpp`

- [ ] **Step 1: Replace the all-or-nothing power read block with individual try/catch**

In `src/core/ControlLoop.cpp`, replace the entire power monitoring section in `readStatus()` from:

```cpp
    // Read power monitoring registers
    try {
        int16_t ac_current_raw = modbus_client_->readRegister(REG_AC_CURRENT);
        int16_t dc_current_raw = modbus_client_->readRegister(REG_DC_CURRENT);
        int16_t ac_voltage_raw = modbus_client_->readRegister(REG_AC_VOLTAGE);
        int16_t dc_voltage_raw = modbus_client_->readRegister(REG_DC_VOLTAGE);

        // Apply scaling factors per device spec
        status.ac_current = static_cast<float>(ac_current_raw) * 2.0f;   // Actual = Display * 2
        status.dc_current = static_cast<float>(dc_current_raw) * 4.0f;   // Actual = Display * 4
        status.ac_voltage = static_cast<float>(ac_voltage_raw);           // Actual = Display
        status.dc_voltage = static_cast<float>(dc_voltage_raw) / 2.0f;   // Actual = Display / 2
        status.ac_power = status.ac_voltage * status.ac_current;          // Power in Watts (AC)
    } catch (const ModbusException&) {
        status.ac_current = 0.0f;
        status.dc_current = 0.0f;
        status.ac_voltage = 0.0f;
        status.dc_voltage = 0.0f;
        status.ac_power = 0.0f;
    }
```

to:

```cpp
    // Read power monitoring registers (individual reads — one failure does not zero the others)
    {
        int16_t raw;
        float ac_current = 0.0f, ac_voltage = 0.0f;
        bool got_current = false, got_voltage = false;

        try {
            raw = modbus_client_->readRegister(REG_AC_CURRENT);
            status.ac_current = static_cast<float>(raw) * 2.0f;   // Manual: Actual = Display × 2
            ac_current = status.ac_current;
            got_current = true;
        } catch (const ModbusException&) {
            status.ac_current = 0.0f;
        }

        try {
            raw = modbus_client_->readRegister(REG_DC_CURRENT);
            status.dc_current = static_cast<float>(raw) * 4.0f;   // Manual: Actual = Display × 4
        } catch (const ModbusException&) {
            status.dc_current = 0.0f;
        }

        try {
            raw = modbus_client_->readRegister(REG_AC_VOLTAGE);
            status.ac_voltage = static_cast<float>(raw);           // Manual: Actual = Display
            ac_voltage = status.ac_voltage;
            got_voltage = true;
        } catch (const ModbusException&) {
            status.ac_voltage = 0.0f;
        }

        try {
            raw = modbus_client_->readRegister(REG_DC_VOLTAGE);
            status.dc_voltage = static_cast<float>(raw) / 2.0f;   // Manual: Actual = Display / 2
        } catch (const ModbusException&) {
            status.dc_voltage = 0.0f;
        }

        // Calculate power only if both V and I were obtained
        if (got_current && got_voltage) {
            status.ac_power_va = ac_voltage * ac_current;                    // Apparent power (VA)
            status.ac_power_w = status.ac_power_va * ESTIMATED_POWER_FACTOR; // Estimated real power (W)
            status.power_valid = true;
        } else {
            status.ac_power_va = 0.0f;
            status.ac_power_w = 0.0f;
            status.power_valid = false;
        }
    }
```

- [ ] **Step 2: Build and verify compilation**

Run: `cd /home/gbo/develop/wpomp && cmake --build build 2>&1 | tail -10`

Expected: Build succeeds with no errors.

- [ ] **Step 3: Commit**

```bash
git add src/core/ControlLoop.cpp
git commit -m "fix: individual try/catch per power register, compute VA and estimated W"
```

---

### Task 3: Add entering water temperature (REG_ENTERING_WATER_TEMP)

**Files:**
- Modify: `include/core/ControlLoop.hpp`
- Modify: `src/core/ControlLoop.cpp`

- [ ] **Step 1: Add entering_water_temp field to StatusSnapshot**

In `include/core/ControlLoop.hpp`, after `float leaving_water_temp = 0.0f;` add:

```cpp
    float entering_water_temp = 0.0f; // From REG_ENTERING_WATER_TEMP (0x0003, 0.1°C)
```

- [ ] **Step 2: Read entering water temp in readStatus()**

In `src/core/ControlLoop.cpp`, after the leaving water temp read block, add:

```cpp
    // Read entering water temp (0x0003)
    try {
        raw = modbus_client_->readRegister(REG_ENTERING_WATER_TEMP);
        status.entering_water_temp = raw_to_temp(raw);
    } catch (const ModbusException&) {
        // Non-critical, leave as 0
    }
```

- [ ] **Step 3: Commit**

```bash
git add include/core/ControlLoop.hpp src/core/ControlLoop.cpp
git commit -m "feat: add entering water temperature field and register read"
```

---

### Task 4: Add diagnostic registers (unit capacity, compressor freq, water flow, ODU input status)

**Files:**
- Modify: `include/config.h`
- Modify: `include/core/ControlLoop.hpp`
- Modify: `src/core/ControlLoop.cpp`

- [ ] **Step 1: Add new register defines in config.h**

After the existing `// === POWER MONITORING REGISTERS` section in `include/config.h`, add:

```cpp
//
// === DIAGNOSTIC REGISTERS (READ-ONLY) ===
#define REG_UNIT_CAPACITY          0x1006  // R  - Unit capacity (4/6/8/10/12/14/16 = kW)
#define REG_COMPRESSOR_FREQ       0x0040  // R  - Actual compressor frequency (1 Hz)
#define REQ_COMPRESSOR_FREQ_TARGET 0x100F // R  - Required compressor frequency (1/10 Hz, Data=Freq*10)
#define REG_WATER_FLOW             0x102A  // R  - Water flow feedback (m3/h × 100)
#define REG_ACTUAL_CAPACITY_OUTPUT 0x1004  // R  - Actual capacity output
#define REG_ODU_INPUT_STATUS       0x101F  // R  - Outdoor unit input status (bit flags)
#define REG_COMPRESSOR_RUNTIME     0x0174  // R  - Compressor runtime (hours)
#define REG_PUMP_RUNTIME           0x0176  // R  - Pump runtime (hours)
```

Also **rename** the existing line:

```cpp
#define REG_DEVICE_TYPE           0x1006  // R  - Device type identifier
```

to:

```cpp
#define REG_UNIT_CAPACITY         0x1006  // R  - Unit capacity (4/6/8/10/12/14/16 = kW)
// Note: This was previously named REG_DEVICE_TYPE. The Rotenso Windmi manual (p123)
// documents 0x1006 as "Capacity of the unit" with values 4/6/8/10/12/14/16 = kW.
// REG_DEVICE_TYPE is kept as alias for backwards compatibility.
#define REG_DEVICE_TYPE           REG_UNIT_CAPACITY  // Alias: same register, old name
```

- [ ] **Step 2: Add diagnostic fields to StatusSnapshot**

In `include/core/ControlLoop.hpp`, add after the power monitoring section:

```cpp
    // Diagnostic registers
    float compressor_freq = 0.0f;       // Actual compressor frequency in Hz
    float water_flow = 0.0f;            // Water flow in m³/h (from 0x102A, raw/100)
    int unit_capacity_kw = 0;            // Unit capacity in kW (4/6/8/10/12/14/16)
    int actual_capacity_output = 0;      // Actual capacity output (from 0x1004)
    int odu_input_status = 0;           // Outdoor unit input status bit flags (from 0x101F)
    int compressor_runtime_h = 0;        // Compressor runtime in hours (from 0x0174)
    int pump_runtime_h = 0;              // Pump runtime in hours (from 0x0176)
```

- [ ] **Step 3: Read diagnostic registers in readStatus()**

In `src/core/ControlLoop.cpp`, add at the end of `readStatus()` (before `status.device_online = ok;`):

```cpp
    // Read diagnostic registers (non-critical — failures don't affect ok flag)
    try {
        raw = modbus_client_->readRegister(REG_UNIT_CAPACITY);
        status.unit_capacity_kw = raw;  // Raw = kW rating (4,6,8,10,12,14,16)
    } catch (const ModbusException&) {
        status.unit_capacity_kw = 0;
    }

    try {
        raw = modbus_client_->readRegister(REG_COMPRESSOR_FREQ);
        status.compressor_freq = static_cast<float>(raw);  // 1 Hz units
    } catch (const ModbusException&) {
        status.compressor_freq = 0.0f;
    }

    try {
        raw = modbus_client_->readRegister(REG_WATER_FLOW);
        status.water_flow = static_cast<float>(raw) / 100.0f;  // m³/h × 100
    } catch (const ModbusException&) {
        status.water_flow = 0.0f;
    }

    try {
        raw = modbus_client_->readRegister(REG_ACTUAL_CAPACITY_OUTPUT);
        status.actual_capacity_output = raw;
    } catch (const ModbusException&) {
        status.actual_capacity_output = 0;
    }

    try {
        raw = modbus_client_->readRegister(REG_ODU_INPUT_STATUS);
        status.odu_input_status = raw;
    } catch (const ModbusException&) {
        status.odu_input_status = 0;
    }

    try {
        raw = modbus_client_->readRegister(REG_COMPRESSOR_RUNTIME);
        status.compressor_runtime_h = raw;
    } catch (const ModbusException&) {
        status.compressor_runtime_h = 0;
    }

    try {
        raw = modbus_client_->readRegister(REG_PUMP_RUNTIME);
        status.pump_runtime_h = raw;
    } catch (const ModbusException&) {
        status.pump_runtime_h = 0;
    }
```

- [ ] **Step 4: Add tests for new register defines**

In `tests/core/test_control_loop.cpp`, add to `ConfigRegisterTest`:

```cpp
    EXPECT_EQ(REG_UNIT_CAPACITY, 0x1006);
    EXPECT_EQ(REG_DEVICE_TYPE, REG_UNIT_CAPACITY);  // Backwards compatibility alias
    EXPECT_EQ(REG_COMPRESSOR_FREQ, 0x0040);
    EXPECT_EQ(REG_WATER_FLOW, 0x102A);
    EXPECT_EQ(REG_ACTUAL_CAPACITY_OUTPUT, 0x1004);
    EXPECT_EQ(REG_ODU_INPUT_STATUS, 0x101F);
    EXPECT_EQ(REG_COMPRESSOR_RUNTIME, 0x0174);
    EXPECT_EQ(REG_PUMP_RUNTIME, 0x0176);
```

Add new test for diagnostic field defaults:

```cpp
TEST(StatusSnapshotTest, DiagnosticDefaults) {
    StatusSnapshot snap{};
    EXPECT_FLOAT_EQ(snap.compressor_freq, 0.0f);
    EXPECT_FLOAT_EQ(snap.water_flow, 0.0f);
    EXPECT_EQ(snap.unit_capacity_kw, 0);
    EXPECT_EQ(snap.actual_capacity_output, 0);
    EXPECT_EQ(snap.odu_input_status, 0);
    EXPECT_EQ(snap.compressor_runtime_h, 0);
    EXPECT_EQ(snap.pump_runtime_h, 0);
}
```

- [ ] **Step 5: Build and run tests**

Run: `cd /home/gbo/develop/wpomp && cmake --build build --target test_control_loop 2>&1 | tail -5`

Then: `cd /home/gbo/develop/wpomp/build && ./tests/core/test_control_loop`

Expected: All tests PASS.

- [ ] **Step 6: Commit**

```bash
git add include/config.h include/core/ControlLoop.hpp src/core/ControlLoop.cpp tests/core/test_control_loop.cpp
git commit -m "feat: add diagnostic registers (capacity, compressor freq, water flow, runtime)

- Rename REG_DEVICE_TYPE to REG_UNIT_CAPACITY (0x1006)
- Add REG_COMPRESSOR_FREQ (0x0040), REG_WATER_FLOW (0x102A)
- Add REG_ACTUAL_CAPACITY_OUTPUT (0x1004), REG_ODU_INPUT_STATUS (0x101F)
- Add REG_COMPRESSOR_RUNTIME (0x0174), REG_PUMP_RUNTIME (0x0176)
- Add diagnostic fields to StatusSnapshot
- Individual try/catch per register (non-critical)
- All verified against Rotenso Windmi manual pages 120-123"
```

---

### Task 5: Add COP and heat output estimation fields

**Files:**
- Modify: `include/core/ControlLoop.hpp`
- Modify: `src/core/ControlLoop.cpp`

- [ ] **Step 1: Add COP fields to StatusSnapshot**

In `include/core/ControlLoop.hpp`, after the diagnostic section added in Task 4, add:

```cpp
    // COP estimation (calculated from water flow + delta-T + power)
    float heat_output_w = 0.0f;         // Estimated heat output in Watts
    float cop = 0.0f;                    // Coefficient of Performance (heat_out / power_in)
    bool cop_valid = false;             // True if COP calculation had valid inputs
```

- [ ] **Step 2: Calculate COP in readStatus()**

In `src/core/ControlLoop.cpp`, after the diagnostic register reads and before `status.device_online = ok;`, add:

```cpp
    // Calculate COP using water flow and delta-T (only if we have valid data)
    // COP = heat_output / electrical_input
    // heat_output = flow × 1000/3600 × 4186 × (T_leaving - T_entering)
    // where flow is in m³/h, T in °C, result in Watts
    status.heat_output_w = 0.0f;
    status.cop = 0.0f;
    status.cop_valid = false;

    if (status.water_flow > 0.01f && status.power_valid &&
        status.leaving_water_temp > 0.0f && status.entering_water_temp > -50.0f) {
        float delta_t = status.leaving_water_temp - status.entering_water_temp;
        if (delta_t > 0.1f) {
            // flow_m3h / 3600 = m³/s → × 1000 = kg/s, × 4186 J/(kg·K) = W/K
            float flow_kg_s = (status.water_flow * 1000.0f) / 3600.0f;
            status.heat_output_w = flow_kg_s * 4186.0f * delta_t;
            if (status.ac_power_w > 0.0f) {
                status.cop = status.heat_output_w / status.ac_power_w;
                status.cop_valid = true;
            }
        }
    }
```

- [ ] **Step 3: Add test**

In `tests/core/test_control_loop.cpp`, add:

```cpp
TEST(COPEstimationTest, HeatOutputCalculation) {
    // 0.5 m³/h flow, delta-T of 5°C
    float flow_m3h = 0.5f;
    float flow_kg_s = (flow_m3h * 1000.0f) / 3600.0f;  // 0.1389 kg/s
    float delta_t = 5.0f;
    float heat_output_w = flow_kg_s * 4186.0f * delta_t;
    // 0.1389 × 4186 × 5 = 2910 W
    EXPECT_NEAR(heat_output_w, 2910.0f, 10.0f);
}

TEST(COPEstimationTest, COPWithKnownValues) {
    float heat_output_w = 2900.0f;
    float ac_power_w = 1000.0f;
    float cop = heat_output_w / ac_power_w;
    EXPECT_NEAR(cop, 2.9f, 0.01f);
}
```

- [ ] **Step 4: Build and run tests**

Run: `cd /home/gbo/develop/wpomp && cmake --build build --target test_control_loop 2>&1 | tail -5`

Then: `cd /home/gbo/develop/wpomp/build && ./tests/core/test_control_loop`

Expected: All tests PASS.

- [ ] **Step 5: Commit**

```bash
git add include/core/ControlLoop.hpp src/core/ControlLoop.cpp tests/core/test_control_loop.cpp
git commit -m "feat: add COP estimation using water flow and delta-T"
```

---

### Task 6: Update JSON API output to include all new fields

**Files:**
- Modify: `include/utils/JsonHelpers.hpp`
- Modify: `src/utils/JsonHelpers.cpp`
- Modify: `src/web/WebServer.cpp`

- [ ] **Step 1: Update JsonHelpers.hpp signature**

In `include/utils/JsonHelpers.hpp`, add the new parameters to `generateStatusJson`:

Change the signature from:

```cpp
    static std::string generateStatusJson(
        double dhw_temp, double dhw_target,
        double heating_temp, double heating_target,
        double outdoor_temp, double leaving_water_temp,
        const std::string& mode, const std::string& running_status,
        const std::string& priority, const std::string& status,
        bool device_online,
        double ac_current, double dc_current,
        double ac_voltage, double dc_voltage,
        double ac_power, int working_mode);
```

to:

```cpp
    static std::string generateStatusJson(
        double dhw_temp, double dhw_target,
        double heating_temp, double heating_target,
        double outdoor_temp, double leaving_water_temp,
        double entering_water_temp,
        const std::string& mode, const std::string& running_status,
        const std::string& priority, const std::string& status,
        bool device_online,
        double ac_current, double dc_current,
        double ac_voltage, double dc_voltage,
        double ac_power_va, double ac_power_w,
        bool power_valid,
        double compressor_freq, double water_flow,
        int unit_capacity_kw, int actual_capacity_output,
        int odu_input_status, int compressor_runtime_h, int pump_runtime_h,
        double heat_output_w, double cop, bool cop_valid,
        int working_mode);
```

- [ ] **Step 2: Update JsonHelpers.cpp implementation**

In `src/utils/JsonHelpers.cpp`, update the `generateStatusJson` function body:

```cpp
std::string JsonHelpers::generateStatusJson(
    double dhw_temp, double dhw_target,
    double heating_temp, double heating_target,
    double outdoor_temp, double leaving_water_temp,
    double entering_water_temp,
    const std::string& mode, const std::string& running_status,
    const std::string& priority, const std::string& status,
    bool device_online,
    double ac_current, double dc_current,
    double ac_voltage, double dc_voltage,
    double ac_power_va, double ac_power_w,
    bool power_valid,
    double compressor_freq, double water_flow,
    int unit_capacity_kw, int actual_capacity_output,
    int odu_input_status, int compressor_runtime_h, int pump_runtime_h,
    double heat_output_w, double cop, bool cop_valid,
    int working_mode) {

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1);
    oss << "{"
        << "\"dhwTemperature\":" << dhw_temp << ","
        << "\"dhwTarget\":" << dhw_target << ","
        << "\"heatingTemperature\":" << heating_temp << ","
        << "\"heatingTarget\":" << heating_target << ","
        << "\"outdoorTemperature\":" << outdoor_temp << ","
        << "\"leavingWaterTemperature\":" << leaving_water_temp << ","
        << "\"enteringWaterTemperature\":" << entering_water_temp << ","
        << "\"mode\":\"" << mode << "\","
        << "\"runningStatus\":\"" << running_status << "\","
        << "\"priority\":\"" << priority << "\","
        << "\"status\":\"" << status << "\","
        << "\"deviceOnline\":" << (device_online ? "true" : "false") << ","
        << "\"acCurrent\":" << ac_current << ","
        << "\"dcCurrent\":" << dc_current << ","
        << "\"acVoltage\":" << ac_voltage << ","
        << "\"dcVoltage\":" << dc_voltage << ","
        << "\"acPowerVA\":" << ac_power_va << ","
        << "\"acPowerW\":" << ac_power_w << ","
        << "\"powerValid\":" << (power_valid ? "true" : "false") << ","
        << std::setprecision(1)
        << "\"compressorFrequency\":" << compressor_freq << ","
        << "\"waterFlow\":" << water_flow << ","
        << "\"unitCapacityKw\":" << unit_capacity_kw << ","
        << "\"actualCapacityOutput\":" << actual_capacity_output << ","
        << "\"oduInputStatus\":" << odu_input_status << ","
        << "\"compressorRuntimeHours\":" << compressor_runtime_h << ","
        << "\"pumpRuntimeHours\":" << pump_runtime_h << ","
        << "\"heatOutputW\":" << heat_output_w << ","
        << "\"cop\":" << cop << ","
        << "\"copValid\":" << (cop_valid ? "true" : "false") << ","
        << "\"workingMode\":" << working_mode
        << "}";
    return oss.str();
}
```

- [ ] **Step 3: Update WebServer.cpp call sites**

In `src/web/WebServer.cpp`, update the `generateStatusJson` call to include all new fields. Find the existing call and replace it with:

```cpp
        last_status_.dhw_tank_temp,
        last_status_.dhw_target,
        last_status_.leaving_water_temp,
        last_status_.heating_target,
        last_status_.outdoor_temp,
        last_status_.leaving_water_temp,
        last_status_.entering_water_temp,
        mode_to_string(last_status_.running_mode),
        status_to_string(last_status_.running_status),
        last_status_.dhw_priority ? "dhw" : "heating",
        last_status_.is_running ? "running" : "stopped",
        last_status_.device_online ? "true" : "false",
        last_status_.ac_current,
        last_status_.dc_current,
        last_status_.ac_voltage,
        last_status_.dc_voltage,
        last_status_.ac_power_va,
        last_status_.ac_power_w,
        last_status_.power_valid,
        last_status_.compressor_freq,
        last_status_.water_flow,
        last_status_.unit_capacity_kw,
        last_status_.actual_capacity_output,
        last_status_.odu_input_status,
        last_status_.compressor_runtime_h,
        last_status_.pump_runtime_h,
        last_status_.heat_output_w,
        last_status_.cop,
        last_status_.cop_valid,
        last_status_.working_mode
```

Also update the JSON format string — remove the old `snprintf` or `oss` format and use the `generateStatusJson` helper (it may already be using it, in which case just verify the parameter match).

Also update the plain-JSON fallback format string in the status handler if there is one. Search for `"acPower"` in WebServer.cpp and change to:

```
"\"acPowerVA\":%.1f,"
"\"acPowerW\":%.1f,"
"\"powerValid\":%s,"
"\"compressorFrequency\":%.1f,"
"\"waterFlow\":%.2f,"
"\"unitCapacityKw\":%d,"
"\"enteringWaterTemperature\":%.1f,"
...
```

Make sure the format arguments match the new fields exactly.

- [ ] **Step 4: Build and verify**

Run: `cd /home/gbo/develop/wpomp && cmake --build build 2>&1 | tail -10`

Expected: Build succeeds.

- [ ] **Step 5: Commit**

```bash
git add include/utils/JsonHelpers.hpp src/utils/JsonHelpers.cpp src/web/WebServer.cpp
git commit -m "feat: add new fields to JSON API (powerVA/powerW, enteringWater, COP, diagnostics)"
```

---

### Task 7: Rename REG_DEVICE_TYPE and update tests

**Files:**
- Modify: `include/config.h` (already done in Task 4 step 1)
- Modify: `tests/core/test_control_loop.cpp`

- [ ] **Step 1: Verify REG_DEVICE_TYPE alias works in tests**

In `tests/core/test_control_loop.cpp`, the test should already reference the new name from Task 4. Verify:

```cpp
    EXPECT_EQ(REG_UNIT_CAPACITY, 0x1006);
    EXPECT_EQ(REG_DEVICE_TYPE, REG_UNIT_CAPACITY);  // Backwards compatible alias
```

If the old test still references `REG_DEVICE_TYPE` directly with `0x1006`, update it to use `REG_UNIT_CAPACITY`.

- [ ] **Step 2: Build and run full test suite**

Run: `cd /home/gbo/develop/wpomp && cmake --build build && cd build && ctest --output-on-failure`

Expected: All tests PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/core/test_control_loop.cpp
git commit -m "test: update tests for REG_UNIT_CAPACITY rename and new diagnostic fields"
```

---

### Task 8: Final integration build and test

**Files:**
- All modified files from Tasks 1-7

- [ ] **Step 1: Clean rebuild**

Run: `cd /home/gbo/develop/wpomp && cmake --build build --clean-first 2>&1 | tail -15`

Expected: Clean build with no warnings or errors.

- [ ] **Step 2: Run all tests**

Run: `cd /home/gbo/develop/wpomp/build && ctest --output-on-failure`

Expected: All tests PASS.

- [ ] **Step 3: Verify JSON output includes all new fields**

Run a quick manual test if the binary can be executed, or inspect the `generateStatusJson` function to confirm all fields are present:

```cpp
// Expected JSON fields:
// enteringWaterTemperature, acPowerVA, acPowerW, powerValid,
// compressorFrequency, waterFlow, unitCapacityKw, actualCapacityOutput,
// oduInputStatus, compressorRuntimeHours, pumpRuntimeHours,
// heatOutputW, cop, copValid
```

- [ ] **Step 4: Final commit**

```bash
git add -A
git commit -m "chore: integration verify — all energy monitoring improvements complete"
```

---

## Self-Review Checklist

| Spec Item | Covered by Task |
|-----------|----------------|
| Issue 1: Rename VA→W | Task 1 (ac_power_va + ac_power_w + PF constant) |
| Issue 2: Add entering water temp | Task 3 (REG_ENTERING_WATER_TEMP read + field) |
| Issue 5: COP calculation | Task 5 (heat_output_w + cop + cop_valid fields) |
| Issue 6: Individual try/catch | Task 2 (per-register error handling) |
| Issue 7: Add diagnostic registers | Task 4 (capacity, freq, flow, status, runtime) |
| Issue 8: REG_DEVICE_TYPE→REG_UNIT_CAPACITY | Task 4 step 1 + Task 7 |
| JSON API updated | Task 6 |
| Tests updated | Tasks 1, 4, 5, 7 |