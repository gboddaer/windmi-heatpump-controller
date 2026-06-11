# Rotenso Windmi Series — Manual Summary

This document summarizes the key sections from the Rotenso Windmi Series manuals and cross-references them to the downloaded PDF files and online sources.

## Document Inventory

| Document | Location | Pages | Language | Source |
|----------|----------|-------|----------|--------|
| Installation & User Manual | [ManualsLib](https://www.manualslib.com/manual/4051642/Rotenso-Windmi-Series.html) | 124 | PL + EN | Online HTML only |
| Controller Manual (ORIS) | `rotenso_windmi_controller_manual.pdf` | 20 | PL + EN | thermosilesia.pl |
| Product Card | `rotenso_windmi_product_card.pdf` | — | EN | thermosilesia.pl |
| Catalog Card | `rotenso_windmi_catalog_card.pdf` | — | EN | thermosilesia.pl |
| Performance Table | `rotenso_windmi_performance_table.pdf` | — | EN | thermosilesia.pl |

**Note:** The Installation & User Manual (124 pages) is only available as HTML on ManualsLib. The PDFs stored in this directory (`rotenso_windmi_manual.pdf`, `rotenso_windmi_installation_manual.pdf`) are HTML redirect pages, not actual PDFs. Use the ManualsLib link above for the full manual.

**Models covered:** WIM40X1, WIM60X1, WIM80X1, WIM100X1, WIM120X3, WIM140X3, WIM160X3

---

## 1. General Introduction (Installation Manual §1, p. 1-3 / p. 69-71 EN)

- Air-source monoblock heat pump (air-to-water)
- R290 (propane) refrigerant
- Available in single-phase (4-10 kW) and three-phase (12-16 kW) models
- Supports heating, cooling, and domestic hot water (DHW) modes

---

## 2. Safety Precautions (Installation Manual §1, p. 4-5 / p. 69-70 EN)

- **DANGER**: High voltage, refrigerant handling requires certified technician
- **WARNING**: Anti-freeze protection — water circuit must be drained if unit is left unpowered in freezing conditions
- **CAUTION**: Lifting weights, electrical connection requirements
- R290 refrigerant quantity limits per room volume (flammability)

---

## 3. Technical Parameters & Modbus Registers (Installation Manual §14, p. 49-52 PL / p. 66-67 EN)

### Modbus Communication Settings
| Parameter | Value |
|-----------|-------|
| Default baud rate | 9600 bps (configurable) |
| Default slave address | 11 (configurable) |
| Protocol | Modbus RTU |
| Frame format | N,8,1 (configurable) |

### Key Modbus Registers (from p. 49-52 PL table)

| Nr. | Read Address | Write Address | FC Read | FC Write | Description | Min | Max | Default | Conversion |
|-----|-------------|---------------|---------|----------|-------------|-----|-----|---------|------------|
| 1 | 002CH | 002CH / 002DH | 0x04/0x03 | 0x10/0x06 | System mode (0=Off, 1=Cool+DHW, 2=Heat+DHW, 3=Cool only GCHV, 4=Heat only GCHV, 5=DHW only GCHV) | 0 | 5 | — | — |
| 2 | 0209H | — | 0x04/0x03 | RO | Running mode | 0 | 2 | — | — |
| 3 | 0209H | — | 0x04/0x03 | RO | Running status | — | — | — | — |
| 4 | 0029H | 0029H | 0x04/0x03 | 0x10/0x06 | Heating mode setting | 0 | 2 | — | — |
| 5 | 0067H | — | 0x04/0x03 | RO | — | — | — | — | — |
| 6 | 0001H | — | 0x04/0x03 | RO | Outdoor temp (T4) | -40 | — | — | ×10 (°C) |
| 7 | 0002H | — | 0x04/0x03 | RO | EWT Tw_in | -40 | — | — | ×10 (°C) |
| 8 | 0003H | — | 0x04/0x03 | RO | LWT T1 | -40 | — | — | ×10 (°C) |
| 9 | 0004H | — | 0x04/0x03 | RO | Gas refrigerant temp T2B | -40 | — | — | ×10 (°C) |
| 10 | 0005H | — | 0x04/0x03 | RO | Discharge temp | -40 | — | — | ×10 (°C) |
| 11 | 000AH | — | 0x04/0x03 | RO | Air coil temp T3 | -40 | — | — | ×10 (°C) |
| 12 | 000BH | — | 0x04/0x03 | RO | DHW tank temp T5 | -40 | — | — | ×10 (°C) |

**Note:** Register `0x02BF` (DHW Priority) is documented in some third-party references but was found to return `Illegal Data Address` on the target firmware. See `docs/dhw_priority_report.md` for details.

**Note:** Register `0x025F` (DHW Hysteresis) was tested and found to return invalid/unreliable data. Removed from the controller codebase.

---

## 4. Operating Modes (Installation Manual §9.5 / p. 99 EN)

### System Mode (Register 002CH)
| Value | Mode | Description |
|-------|------|-------------|
| 0 | Off | Unit stopped |
| 1 | Cool + DHW | Cooling with domestic hot water |
| 2 | Heat + DHW | Heating with domestic hot water |
| 3 | Cool only | Cooling only (GCHV models) |
| 4 | Heat only | Heating only (GCHV models) |
| 5 | DHW only | Domestic hot water only (GCHV models) |

### Running Mode
| Value | Mode |
|-------|------|
| 0 | Off |
| 1 | Cooling |
| 2 | Heating |
| 4 | DHW |

---

## 5. Domestic Hot Water Mode (Installation Manual §10.1 / p. 103 EN)

### DHW Diverting Valve
- The unit drives a **3-way diverting valve** (SV3) to manage DHW storage tank
- On DHW request, the valve directs hot water to the storage tank
- Valve specs: spring return, 2-wire control, Kvs=16, Max Temp=150°C, CHAR:LT3

### DHW Temperature Sensor
- **5 KΩ NTC** thermistor, cable length 4m
- Temperature range: -30°C to +65°C
- Default DHW target: 45°C (range 30-65°C)

### DHW Operation Logic
1. When tank temperature drops below setpoint, DHW mode activates
2. Diverting valve switches water flow to DHW tank circuit
3. Heat pump runs at heating mode until tank reaches target temperature
4. Valve returns to normal heating/cooling circuit when DHW cycle completes

---

## 6. Wired Controller (ORIS) — Controller Manual (p. 1-20)

### Display Pages (Controller Manual p. 14-15)

| Page | Display | Description |
|------|---------|-------------|
| 1 | Ts1 | Setpoint during standby/cool/heat |
| 2 | Ts2 | Setpoint during DHW mode |
| 3 | Ts3 | Air setpoint control |
| 4 | Capacity | HP×10 (e.g., 10 = 1HP) |
| 5 | Target frequency | Target compressor frequency |
| 6 | Running frequency | Actual compressor frequency |
| 7 | Water flow rate | m³/h from inverter pump |
| 8 | Capacity output | = 1.163 × (flow rate) × (Tw_out - Tw_in) in kW |
| 9 | T3 | ODU coil temp |
| 10 | T4 | Outdoor air temp (OAT) |
| 11 | TP | Discharge temp |
| 12 | T7 | Refrigerant temp for PCB cool |
| 13 | EVX opening | Expansion valve degree |
| 14 | ODU fan speed | Fan motor RPM |
| 15 | AC current | — |
| 16 | AC voltage | — |
| 17 | IPM temp T9 | Compressor module temp |
| 18-19 | Compressor freq. limitation | Bitmask of limitation reasons |
| 20 | Compressor freq. limitation (cont.) | EWT↔LWT differential limitation |
| 20 | Tw_in | EWT (entering water temp) |
| 21 | Tw_out | LWT of BPHE |
| 22 | T1 | LWT of unit (after EHs) |
| 23 | T6 | IAT (indoor air temp from wired controller) |
| 24 | T5 | DHW tank temperature |
| 25 | Tw-2 | Second zone EWT (reserved) |
| 26 | T1B | External heat source LWT |
| 27 | Capacity demand | — |
| 28 | Inv. Pump speed | — |
| 29 | Last alarm | — |
| 30 | Penult alarm | — |
| 31 | Antepenultimate alarm | — |
| 32 | Current protection | P0-P3 |
| 33 | P6 alarm detail | L0-LF sub-codes |
| 34 | SV2 status | 2-way valve (cool/heat water loop) |
| 35 | SV3 status | DHW 3-way valve (OFF=0, ON=1) |
| 36 | Main water loop EHs | Electric heaters status |
| 37 | DHW EHs | DHW electric heaters (OFF=0, ON=1) |
| 38 | External heat source | AHS status |
| 39 | P_m | External main water pump |
| 40 | P_p | Second zone pump |
| 41 | P_o | First zone pump |
| 42 | Anti-frozen heater | Status |
| 43 | Chassis heater | Status |
| 44 | Crank heater | Status |
| 45 | SV2 refrigerant | FCU water loop valve |

---

## 7. Error Codes (Controller Manual §5, p. 16-17)

| Code | Description |
|------|-------------|
| E0 | Water flow switch fault |
| E1 | Communication fault IDU↔ODU board |
| E2 | LWT unit sensor (T1) fault |
| E3 | Gas refrigerant temp sensor (T2) fault (reserved) |
| E4 | Liquid refrigerant temp sensor (T2B) fault (reserved) |
| E5 | ODU (module part) alarm — check IDU PCB |
| E6 | DHW sensor (T5) fault |
| E7 | EWT sensor (T_in) fault |
| E8 | LWT of BPHE sensor (T_out) fault |
| E9 | Communication fault wired controller ↔ function board |
| EA | Second zone LWT sensor fault (reserved) |
| Eb | External heat source LWT sensor fault |
| Ec | Water pump fault |
| P0 | EEPROM fault |
| P1 | Protection: large EWT↔LWT differential |
| P2 | Protection: lack of water |
| P3 | Protection: abnormal EWT↔LWT differential |
| P6 | Protection: standard electrical heater overheat |

**Note:** P0-P3 only display after occurring 3 times in 1 hour and require power cycle to reset.

### P6 Alarm Sub-codes (Controller Manual p. 17)

| Code | Description |
|------|-------------|
| L0 | IPM or IGBT over current |
| L1 | Lack of phase |
| L2 | Compressor losing speed fault |
| L3 | DC voltage too low |
| L4 | Fan motor over current |
| L5 | Fan motor lack of phase |
| L6 | Fan motor zero speed fault |
| L7 | PFC fault |
| L8 | DC voltage too high |
| L9 | Compressor zero speed fault |
| LA | PWM synchronization fault |
| Lb | MCE fault |
| Lc | Compressor over current |
| Ld | EEPROM data wrong |
| LE | Compressor fail to start |
| LF | Fan motor losing speed fault |

---

## 8. Compressor Frequency Limitation Reasons (Controller Manual p. 16)

Bitmask display — values sum when multiple limitations active:

| Value | Reason |
|-------|--------|
| 0 | No limitation |
| 1 | T3B temp limitation (reserved) |
| 2 | OAT (outdoor air temp) limitation |
| 4 | Discharge temp limitation |
| 8 | Voltage limitation |
| 16 | Current limitation |
| 32 | IPM temp limitation |
| 64 | Night mode limitation |
| 128 | LWT limitation |

---

## 9. Installation & Wiring (Installation Manual §9, p. 34-35 PL / p. 94-95 EN)

### Standard Installation with DHW
- Unit → DHW tank via 3-way valve (SV3)
- Variable speed pump in hydraulic kit (standard)
- Optional secondary water pump for extended circuits
- External heat source (AHS/boiler) connection available

### Electrical Connection
- Terminal block on unit for customer wiring
- Phase requirements: 1-phase (4-10kW) or 3-phase (12-16kW)
- Dedicated circuit breaker required

---

## 10. Pump Configuration (Installation Manual §10.4, p. 104 EN)

- Variable speed pump controlled by entering/leaving water temperature differential
- Adjustable speed settings via controller or Modbus
- Built-in pump in hydraulic kit

---

## 11. Electric Heaters (Installation Manual §10.5, p. 105 EN)

- Backup electric heaters (EHs) available
- Configurable as backup or primary heating source
- DHW electric heaters separate from main loop heaters

---

## 12. Technical Specifications (Product Card)

| Model | Capacity | Phase | Refrigerant |
|-------|----------|-------|-------------|
| WIM40X1 | 4.0 kW | 1-phase | R290 |
| WIM60X1 | 6.0 kW | 1-phase | R290 |
| WIM80X1 | 8.0 kW | 1-phase | R290 |
| WIM100X1 | 10.0 kW | 1-phase | R290 |
| WIM120X3 | 12.0 kW | 3-phase | R290 |
| WIM140X3 | 14.0 kW | 3-phase | R290 |
| WIM160X3 | 16.0 kW | 3-phase | R290 |

---

## 13. Relevant Registers for Controller Project

Based on the probe tool results and manual cross-reference, here are the registers used by our controller:

| Address | Name | R/W | Status | Notes |
|---------|------|-----|--------|-------|
| 0001H | Outdoor Temp (T4) | RO | ✅ Works | Intermittent timeout (gateway buffering) |
| 0002H | EWT (Tw_in) | RO | ✅ Works | — |
| 0003H | LWT (T1) | RO | ✅ Works | — |
| 0004H | Gas refrigerant temp | RO | ✅ Works | — |
| 0005H | Discharge temp | RO | ✅ Works | — |
| 000AH | Air coil temp (T3) | RO | ✅ Works | — |
| 000BH | DHW tank temp (T5) | RO | ✅ Works | — |
| 000CH | IAT (T6) | RO | ✅ Works | — |
| 000DH | LWT BPHE (Tw_out) | RO | ✅ Works | — |
| 000EH | LWT unit after EH (T1) | RO | ✅ Works | — |
| 002CH | System Mode | RW | ✅ Works | 0=Off, 1=Cool+DHW, 2=Heat+DHW |
| 0029H | Heating mode setting | RW | ✅ Works | — |
| 002AH | DHW target temp | RW | ✅ Works | — |
| 002BH | Heating target temp | RW | ✅ Works | — |
| 00D2H | DHW Valve Status | RO | ✅ Works | 0=OFF, 1=ON |
| 0209H | Running mode | RO | ✅ Works | — |
| 1004H | Capacity Output (kW) | RO | ✅ Works | — |
| 102AH | Water Flow Rate (m³/h×100) | RO | ✅ Works | — |
| 1802H | DC Current | RO | ✅ Works | Kept in JSON (internal COP calc) |
| 1803H | DC Voltage | RO | ✅ Works | Kept in JSON (internal COP calc) |
| 1804H | AC Current | RO | ✅ Works | Kept in JSON (internal COP calc) |
| 1805H | AC Voltage | RO | ✅ Works | Kept in JSON (internal COP calc) |
| 02BFH | DHW Priority | RW | ❌ Fails | Illegal Data Address on target firmware |
| 025FH | DHW Hysteresis | RW | ❌ Removed | Returns invalid/unreliable data |

---

## Appendix: Online Sources

- **Installation & User Manual (124 pages):** https://www.manualslib.com/manual/4051642/Rotenso-Windmi-Series.html
- **Modbus Register Table (Polish):** p. 49-52 of above
- **Modbus Register Table (English):** p. 66-67 of above
- **Controller Manual (ORIS):** `rotenso_windmi_controller_manual.pdf` — downloaded from thermosilesia.pl
- **Product Card:** `rotenso_windmi_product_card.pdf` — downloaded from thermosilesia.pl
- **Catalog Card:** `rotenso_windmi_catalog_card.pdf` — downloaded from thermosilesia.pl
- **Performance Table:** `rotenso_windmi_performance_table.pdf` — downloaded from thermosilesia.pl
- **Distributor website:** https://thermosilesia.pl
- **Manufacturer website:** https://www.rotenso.com
