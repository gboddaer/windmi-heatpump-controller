# Design: Refactoring & Restructuring to Adhere to Coding Guidelines

**Date:** 2026-06-07
**Branch:** `feature/refactor-coding-guidelines`
**Status:** Approved

## Objective

Refactor the entire codebase to match `docs/coding-style.md` (inspired by Google C++ Style Guide), and remove the "Power Monitoring" section from the Web UI.

## Scope

### 1. Remove "Power Monitoring" from Web UI (feature change)

**Affected files:** `static/index.html`, `static/app.js`, `static/style.css`

- Remove the `<h2>Power Monitoring</h2>` section and its status-row div from `index.html`
- Remove the 5 element references (acCurrentValue, dcCurrentValue, acVoltageValue, dcVoltageValue, acPowerValue) and their update code from `app.js`
- Remove `.status-row-power` and `.power-value` CSS rules from `style.css` if no longer referenced
- **Important:** The power fields remain in `StatusSnapshot` struct **and in the JSON status response**. They are still used internally for COP calculation and may be useful for API consumers. Only the UI display section is removed.
- `src/web/WebServer.cpp` is **not** changed — the JSON response keeps `acCurrent`, `dcCurrent`, `acVoltage`, `dcVoltage`, `acPowerVA`, `acPowerW`, and `powerValid` fields.

### 2. Include guard standardization (22 headers)

Headers currently use inconsistent guard styles:
- `#pragma once` (Platform.hpp)
- Simple `#ifndef MODBUS_CLIENT_H` (modbus_client.h, modbus_rtu_frame.h)
- `WINDMI_*` prefix with varied separators (most .hpp files)
- Missing entirely (Logger.hpp, ModbusSerialClient.hpp, modbus_serial_client.h)
- `src/selftest.h` and `src/spsc_queue.h` are internal headers in `src/` not `include/`

Standardize all to the `WINDMI_<PATH>_<FILE>_H_` (for .h) or `WINDMI_<PATH>_<FILE>_HPP_` (for .hpp) format, matching what the majority of existing guards already use. Use single underscores between segments (matching the existing convention) rather than the triple-underscore Google style:

| File | Guard |
|------|-------|
| `include/config.h` | `WINDMI_CONFIG_H_` (already correct) |
| `include/crc16.h` | `WINDMI_CRC16_H_` (already correct) |
| `include/modbus_client.h` | `WINDMI_MODBUS_CLIENT_H_` |
| `include/modbus/modbus_rtu_frame.h` | `WINDMI_MODBUS_RTU_FRAME_H_` |
| `include/modbus/modbus_serial_client.h` | `WINDMI_MODBUS_SERIAL_CLIENT_H_` |
| `include/selftest.hpp` | `WINDMI_SELFTEST_HPP_` |
| `include/core/ControlLoop.hpp` | `WINDMI_CORE_CONTROL_LOOP_HPP_` |
| `include/core/StatusMonitor.hpp` | `WINDMI_CORE_STATUS_MONITOR_HPP_` |
| `include/utils/Config.hpp` | `WINDMI_UTILS_CONFIG_HPP_` |
| `include/utils/JsonHelpers.hpp` | `WINDMI_UTILS_JSON_HELPERS_HPP_` |
| `include/utils/Logger.hpp` | `WINDMI_UTILS_LOGGER_HPP_` (add missing guard) |
| `include/utils/LoggerC.h` | `WINDMI_UTILS_LOGGER_C_H_` |
| `include/utils/LogTags.hpp` | `WINDMI_UTILS_LOG_TAGS_HPP_` |
| `include/utils/Platform.hpp` | `WINDMI_UTILS_PLATFORM_HPP_` (replace `#pragma once`) |
| `include/utils/PlatformC.h` | `WINDMI_UTILS_PLATFORM_C_H_` |
| `include/utils/SpscQueue.hpp` | `WINDMI_UTILS_SPSC_QUEUE_HPP_` |
| `include/modbus/IModbusClient.hpp` | `WINDMI_MODBUS_IMODBUSCLIENT_HPP_` |
| `include/modbus/ModbusClient.hpp` | `WINDMI_MODBUS_MODBUS_CLIENT_HPP_` |
| `include/modbus/ModbusSerialClient.hpp` | `WINDMI_MODBUS_MODBUS_SERIAL_CLIENT_HPP_` (add missing guard) |
| `include/modbus/SimulatedModbusClient.hpp` | `WINDMI_MODBUS_SIMULATED_MODBUS_CLIENT_HPP_` |
| `include/web/WebServer.hpp` | `WINDMI_WEB_WEB_SERVER_HPP_` |
| `src/selftest.h` | `WINDMI_SELFTEST_C_H_` |
| `src/spsc_queue.h` | `WINDMI_SPSC_QUEUE_H_` |
| `docs/header_example.hpp` | Update to demonstrate new guard style |

### 3. Include ordering fix (~30 source/header files)

Ensure consistent order with blank lines between groups:
1. Matching project header
2. C system headers
3. C++ standard library headers
4. Third-party headers
5. Other project headers

Alphabetical within each group.

### 4. Trailing whitespace cleanup (~117 lines)

Remove trailing whitespace from all source files.

### 5. Tab → spaces (selftest.c)

Convert tab-indented sections to spaces.

### 6. C-style cast cleanup (few instances)

Replace `(int)`, `(uint16_t)` etc. with `static_cast<type>`.

### 7. Minor pointer alignment and doc example

- Ensure `char* c` not `char *c` where applicable.
- Update `docs/header_example.hpp` to demonstrate the project's include guard style.

## Order of Execution

1. **Web UI changes** (remove Power Monitoring) — feature change
2. **Include guard standardization** — mechanical, broadest impact
3. **Include ordering** — mechanical
4. **Trailing whitespace, tabs, casts** — trivial cleanup

## Risks & Mitigations

- Include guard changes: purely mechanical, no logic change. Verified by running `ctest`.
- Web UI changes: remove DOM elements and JS references only. The C++ side and JSON response keep all power data.
- COP calculation uses `power_valid` and `ac_power_w` — those fields stay in the struct and JSON.
- `src/selftest.h` and `src/spsc_queue.h` are included from `src/selftest.c` only (internal headers), so their guard changes are low-risk.

## Verification

Run `ctest --test-dir build -C Release --output-on-failure` after changes.
