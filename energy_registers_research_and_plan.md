# Energy Registers Research & Implementation Plan

## Revised: 2026-06-06 (v2) — Corrected after finding official Rotenso Windmi manual

## ✅ KEY CORRECTION: Register Addresses and Scaling Factors ARE Documented

The previous version of this report incorrectly stated that registers 0x1015–0x1017 and scaling factors were "undocumented." This is **wrong**. The official Rotenso Windmi Installation & User Manual (page 120) explicitly documents all four power registers with their scaling conversions.

---

## 1. Official Rotenso Windmi Register Documentation

### Source: Rotenso Windmi Installation & User Manual, Page 120 (GCHV Modbus Table)

| No. | GCHV Addr | Hex Addr | Dec Addr | Name | Conversion | Unit |
|-----|-----------|----------|----------|------|-----------|------|
| 114 | **1014H** | 0x1014 | 4116 | **AC current** | **Actual value = Display value × 2** | A |
| 115 | **1015H** | 0x1015 | 4117 | **DC current** | **Actual value = Display value × 4** | A |
| 116 | **1016H** | 0x1016 | 4118 | **AC voltage** | **Actual value = Display value** | V |
| 117 | **1017H** | 0x1017 | 4119 | **DC voltage** | **Actual value = Display value / 2** | V |

### Also Confirmed on Same Page (Page 120):

| No. | GCHV Addr | Hex Addr | Name | Conversion |
|-----|-----------|----------|------|------------|
| 107 | 1001H | 0x1001 | IDU side capacity demand | — |
| 108 | 1002H | 0x1002 | Capacity demand after ODU rectify | — |
| 109 | 1004H | 0x1004 | Actual capacity output | — |
| 110 | 1005H | 0x1005 | Fan speed | — |
| 111 | 1008H | 0x1008 | LWT after BPHE inside unit (Tw-out) | 1/10°C, Data=Temp×10 |
| 112 | 1012H | 0x1012 | EXV opening degree | — |
| 113 | 1013H | 0x1013 | IPM refrigerant cool pipe temp. | 1/10°C, Data=Temp×10 |

### Page 123 (GCHV Modbus Table continued):

| No. | GCHV Addr | Hex Addr | Name | Conversion |
|-----|-----------|----------|------|------------|
| 139 | 1006H | 0x1006 | Capacity of the unit | 4/6/8/10/12/14/16 = kW |
| 142 | 100FH | 0x100F | Required compressor frequency | 1/10 Hz, Data=Freq×10 |
| 143 | 101AH | 0x101A | Required fan speed upper motor | RPM/10 |
| 149 | 102AH | 0x102A | Water flow feedback | m³/h × 100 |

### User Manual Page 46 (Wired Controller Display):

| No. | Definition | Description |
|-----|-----------|-------------|
| 14 | ODU fan motor speed | — |
| 15 | **AC current** | Display value (before multiplication) |
| 16 | **AC voltage** | Display value (before conversion) |
| 17 | IPM temp. (T9) | Compressor module temp. |

---

## 2. Current Code Validation

### Our `config.h` register definitions — ALL CORRECT ✅

```c
#define REG_AC_CURRENT            0x1014  // R  - AC current (Display * 2 = Actual Amps)     ✅
#define REG_DC_CURRENT            0x1015  // R  - DC current (Display * 4 = Actual Amps)       ✅
#define REG_AC_VOLTAGE            0x1016  // R  - AC voltage (Display = Actual Volts)           ✅
#define REG_DC_VOLTAGE            0x1017  // R  - DC voltage (Display / 2 = Actual Volts)       ✅
```

### Our `ControlLoop.cpp` scaling — ALL CORRECT ✅

```cpp
status.ac_current = static_cast<float>(ac_current_raw) * 2.0f;   // ✅ Matches manual
status.dc_current = static_cast<float>(dc_current_raw) * 4.0f;   // ✅ Matches manual
status.ac_voltage = static_cast<float>(ac_voltage_raw);           // ✅ Matches manual
status.dc_voltage = static_cast<float>(dc_voltage_raw) / 2.0f;   // ✅ Matches manual
```

### Power Calculation — NEEDS IMPROVEMENT ⚠️

```cpp
status.ac_power = status.ac_voltage * status.ac_current;  // ⚠️ Approximate
```

**This is an approximation.** AC power = V × I only for purely resistive loads (power factor = 1.0). For inverter-driven compressors, the power factor varies (typically 0.85–0.95). This gives **apparent power (VA)**, not **real power (W)**.

---

## 3. Additional Registers Worth Adding

From the official manual and hvdb gist, these registers are valuable for energy monitoring and COP:

| Hex Addr | Dec Addr | Name | Source | Why Important |
|----------|----------|------|--------|---------------|
| 0x1004 | 4100 | Actual capacity output | Manual p120 + hvdb | Current heating/cooling capacity |
| 0x1006 | 4102 | Unit capacity (kW) | Manual p123 + hvdb | Identifies unit size (8=8kW) |
| 0x100F | 4111 | Required compressor freq | Manual p123 + hvdb | Compressor load indicator |
| 0x1008 | 4104 | LWT after BPHE (Tw-out) | Manual p120 + hvdb | Heat output calculation |
| 0x1013 | 4115 | IPM refrigerant pipe temp | hvdb (0.1°C) | Diagnostic |
| 0x1012 | 4130 | EXV opening degree | Manual p120 | Diagnostic |
| 0x102A | 4146 | Water flow feedback | Manual p123 (m³/h × 100) | **CRITICAL for COP calc** |
| 0x0003 | 3 | Current consumption value | Aerona TSV (100W units) | **Alternative direct power** |
| 0x0174 | 372 | Compressor runtime | hvdb (hours) | Uptime tracking |
| 0x0176 | 374 | Pump runtime | hvdb (hours) | Uptime tracking |
| 0x1001 | 4097 | IDU side capacity demand | hvdb | Compressor load |
| 0x1002 | 4098 | Capacity demand after ODU rectify | hvdb | Compressor load |
| 0x101F | 4127 | Outdoor unit input status | Manual p123 (bit flags) | Includes current limitation flag |

---

## 4. Power Calculation Improvements

### Current Approach (V × I = Apparent Power)

```cpp
// Current: AC voltage × AC current = apparent power (VA)
status.ac_power = status.ac_voltage * status.ac_current;
```

**Problem**: This gives Volt-Amperes (VA), not Watts (W). For accurate power measurement, we need to account for the power factor.

### Improvement Option A: Power Factor Estimation

The Rotenso Windmi manual page 122 register 0x1019H mentions "8-Voltage limitation; 16-Current limitation" in the ODU output status, suggesting the inverter imposes current limits. The power factor for inverter-driven compressors typically ranges from 0.85 to 0.95.

```cpp
// Estimated real power with assumed power factor
constexpr float DEFAULT_POWER_FACTOR = 0.90f;  // Typical for inverter compressors
status.ac_power_va = status.ac_voltage * status.ac_current;  // Apparent power (VA)
status.ac_power_w = status.ac_power_va * DEFAULT_POWER_FACTOR; // Estimated real power (W)
```

### Improvement Option B: Register 0x0003 (Direct Power in 100W)

From the Aerona/Chofu TSV (Midea-family register map), register 0x0003 reports "Current consumption value" in 100W units. If this register works on the Windmi (needs verification), it would give **direct real power** without V×I calculation.

```cpp
// Try reading register 0x0003 for direct power consumption
int16_t consumption_raw;
if (modbus_client_->readRegister(0x0003, &consumption_raw) == 0) {
    status.direct_power_w = static_cast<float>(consumption_raw) * 100.0f;
} else {
    // Fallback to V×I calculation
    status.ac_power_w = status.ac_voltage * status.ac_current * DEFAULT_POWER_FACTOR;
}
```

### Improvement Option C: External Power Meter (Gold Standard)

As used by CNC-Buddy/R290_heatpump:
- Shelly 3EM or HomeWizard for electrical energy monitoring
- kamstrup Multical 303 for heat energy monitoring
- COP = heat_output / electrical_input (both measured externally)

### Improvement Option D: Heat Output Estimation from Water Flow

If register 0x102A (water flow feedback) works:

```cpp
// Heat output from water flow and delta-T
// Q = ṁ × cₚ × ΔT = (flow_m3h / 3.6) × 4186 × (T_leaving - T_entering)
// Where:
//   flow_m3h = water_flow_raw / 100.0  (from register 0x102A)
//   cₚ = 4186 J/(kg·K) for water
//   T_leaving from register 0x0004
//   T_entering from register 0x0003
float flow_m3s = (water_flow_raw / 100.0f) / 3600.0f;
float delta_t = status.leaving_water_temp - status.entering_water_temp;
float heat_output_w = flow_m3s * 1000.0f * 4186.0f * delta_t;
float cop = heat_output_w / status.ac_power_w;
```

---

## 5. COP Calculation Strategy

### COP = Heat Output / Electrical Input

| Method | Heat Output | Electrical Input | Accuracy |
|--------|-------------|-----------------|----------|
| **A: External meters** | kamstrup heat meter | Shelly 3EM | ★★★★★ Best |
| **B: Water flow + ΔT** | Register 0x102A + temps | AC V × I × PF | ★★★ Good |
| **C: Register 0x0003** | Not available | Register 0x0003 × 100W | ★★★ Good for input only |
| **D: V×I only** | Not available | AC V × I × 0.90 | ★★ Poor (no heat output) |

**Recommended**: Implement Method B (water flow + delta-T) for heating COP, plus Method C (register 0x0003) as a cross-check for electrical input.

---

## 6. hvdb Gist: Complete Rotenso Windmi Power Register Usage

The hvdb Home Assistant configuration reads these power-related registers (confirmed working on an actual Rotenso Windmi):

```yaml
# Directly relevant to power/energy
- name: Rotenso AC current
  address: 4116          # 0x1014 - READ ONLY (no scaling specified in HA config)
  slave: 11
  unique_id: rotenso_ac_current

- name: Rotenso Unit capacity
  address: 4102          # 0x1006 - Unit capacity (4/6/8 kW)
  slave: 11
  unique_id: rotenso_unit_capacity
  unit_of_measurement: KW

- name: Rotenso Required compressor frequency
  address: 4111          # 0x100F
  slave: 11
  unique_id: rotenso_req_comp_freq
  unit_of_measurement: HZ
```

**Note**: The hvdb config does NOT specify a scaling factor for AC current. This is because Home Assistant's modbus integration reads the raw value, and the user is expected to apply the scaling manually (e.g., via template sensor). The Rotenso manual specifies `Actual = Display × 2` for this register.

**Critically missing from hvdb**: DC current (0x1015), AC voltage (0x1016), DC voltage (0x1017) — the hvdb config only reads AC current.

---

## 7. Implementation Priority

### ✅ Phase 0: Validation (Already Correct)

The current register addresses and scaling factors in `config.h` and `ControlLoop.cpp` match the official Rotenso Windmi manual exactly. No corrections needed.

### Phase 1: Add Missing Power/Energy Registers

Add these registers to `config.h`:

```c
// Energy/COP monitoring registers (from official manual)
#define REG_IDU_CAPACITY_DEMAND    0x1001  // R  - IDU side capacity demand
#define REG_ODU_CAPACITY_DEMAND    0x1002  // R  - Capacity demand after ODU rectify
#define REG_ACTUAL_CAPACITY_OUTPUT 0x1004  // R  - Actual capacity output
#define REG_FAN_SPEED              0x1005  // R  - Fan speed
#define REG_LWT_AFTER_BPHE         0x1008  // R  - LWT after BPHE inside unit (0.1°C)
#define REG_UNIT_CAPACITY          0x1006  // R  - Unit capacity (4/6/8/10/12/14/16 = kW)
#define REG_EXV_OPENING            0x1012  // R  - EXV opening degree
#define REG_IPM_PIPE_TEMP          0x1013  // R  - IPM refrigerant cool pipe temp (0.1°C)
#define REG_WATER_FLOW             0x102A  // R  - Water flow feedback (m³/h × 100)
#define REG_COMPRESSOR_RUNTIME     0x0174  // R  - Compressor runtime (hours)
#define REG_PUMP_RUNTIME           0x0176  // R  - Pump runtime (hours)

// Alternative direct power register (Aerona/Chofu - needs verification)
#define REG_CURRENT_CONSUMPTION    0x0003  // R  - Current consumption value (100W units)
```

### Phase 2: Improve Power Calculation

```cpp
// In ControlLoop::readStatus():
// 1. Read V and I registers (already implemented)
// 2. Calculate apparent power (VA)
status.ac_power_va = status.ac_voltage * status.ac_current;
// 3. Estimate real power with power factor
constexpr float ESTIMATED_PF = 0.90f;
status.ac_power_w = status.ac_power_va * ESTIMATED_PF;
// 4. Try direct power register as cross-check
int16_t consumption_raw;
if (modbus_client_->readRegister(REG_CURRENT_CONSUMPTION, &consumption_raw) == 0) {
    status.direct_power_w = static_cast<float>(consumption_raw) * 100.0f;
}
```

### Phase 3: Add Per-Mode Energy Tracking

```cpp
struct EnergyTracker {
    float heating_kwh = 0.0f;
    float dhw_kwh = 0.0f;
    float total_kwh = 0.0f;
    float cooling_kwh = 0.0f;
    std::chrono::steady_clock::time_point last_update;

    void accumulate(float power_w, int running_status) {
        auto now = std::chrono::steady_clock::now();
        float elapsed_h = std::chrono::duration<float>(now - last_update).count() / 3600.0f;
        last_update = now;
        float kwh = (power_w / 1000.0f) * elapsed_h;
        total_kwh += kwh;
        if (running_status == 2) heating_kwh += kwh;  // MODE_STATUS_HEAT
        else if (running_status == 4) dhw_kwh += kwh;  // MODE_STATUS_DHW
        else if (running_status == 1) cooling_kwh += kwh; // MODE_STATUS_COOL
    }
};
```

### Phase 4: COP Calculation

```cpp
// Heat output estimation using water flow + delta-T
int16_t water_flow_raw = modbus_client_->readRegister(REG_WATER_FLOW);
float flow_m3h = static_cast<float>(water_flow_raw) / 100.0f;  // m³/h × 100
float flow_kg_s = (flow_m3h * 1000.0f) / 3600.0f;  // kg/s
float delta_t = leaving_water_temp - entering_water_temp;  // °C

if (flow_kg_s > 0.01f && delta_t > 0.1f) {
    float heat_output_w = flow_kg_s * 4186.0f * delta_t;
    float cop = heat_output_w / status.ac_power_w;
    status.heat_output_w = heat_output_w;
    status.cop = cop;
}
```

### Phase 5: Add HTTP API Endpoints

```
GET /api/energy  → { power_w, power_va, heating_kwh, dhw_kwh, total_kwh, cop, heat_output_w }
GET /api/diag    → { water_flow, exv_opening, ipm_temp, comp_freq, unit_capacity }
```

---

## 8. Summary of Sources

| Source | What it Confirms |
|--------|-----------------|
| **Rotenso Windmi Manual (p120)** | ✅ Registers 0x1014-0x1017 addresses AND scaling factors |
| **Rotenso Windmi Manual (p123)** | ✅ Registers 0x1006, 0x100F, 0x101A, 0x102A |
| **Rotenso Windmi User Manual (p46)** | ✅ AC current and AC voltage displayed on wired controller |
| **hvdb HA Gist** | ✅ Register 0x1014 (AC current) confirmed working on real Windmi |
| **Aerona/Chofu TSV** | Register 0x0003 (100W power) — **needs verification** on Windmi |
| **Mosibi/Midea ESPHome** | Alternative (Midea) protocol — **NOT compatible** with Windmi |
| **CNC-Buddy/R290** | External meter approach for COP — **recommended architecture** |

---

## 9. References

1. Rotenso Windmi Installation & User Manual, pages 118-123: https://www.manualslib.com/manual/4051642/Rotenso-Windmi-Series.html
2. Rotenso Windmi User Manual, page 46: https://www.manualslib.com/manual/2986210/Rotenso-Windmi-Series.html
3. hvdb HA config: https://gist.github.com/hvdb/a6a6fdc889573084ac2bdd53e71303c7
4. Aerona/Chofu TSV: https://github.com/aerona-chofu-ashp/modbus
5. Mosibi/Midea-heat-pump-ESPHome: https://github.com/Mosibi/Midea-heat-pump-ESPHome
6. CNC-Buddy/R290: https://github.com/CNC-Buddy/R290_heatpump
7. TapHome Midea integration: https://taphome.com/en/compatibility/midea-heat-pump/