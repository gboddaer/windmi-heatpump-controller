# Design: Refactoring & Restructuring to Adhere to Coding Guidelines

**Date:** 2026-06-07
**Branch:** `feature/refactor-coding-guidelines`
**Status:** Approved

## Objective

Refactor the entire codebase to match `docs/coding-style.md` (inspired by Google C++ Style Guide), and remove the "Power Monitoring" section from the Web UI.

## Scope

### 1. Remove "Power Monitoring" from Web UI (feature change)

**Affected files:** `static/index.html`, `static/app.js`, `static/style.css`, `src/web/WebServer.cpp`

- Remove the `<h2>Power Monitoring</h2>` section and its status-row div from `index.html`
- Remove the 6 element references (acCurrentValue, dcCurrentValue, acVoltageValue, dcVoltageValue, acPowerValue) and their update code from `app.js`
- Remove `.status-row-power` and `.power-value` CSS rules from `style.css` if no longer referenced
- Remove power fields from JSON status response string in `WebServer.cpp`
- **Important:** The power fields remain in `StatusSnapshot` struct and in the JSON — they're still used internally for COP calculation. Only the UI display is removed.

### 2. Include guard standardization (~20 headers)

Headers currently use inconsistent guard styles:
- `#pragma once` (Platform.hpp)
- Simple `#ifndef MODBUS_CLIENT_H` (modbus_client.h)
- Missing entirely (most .hpp files use `/** @file */` comments instead)

Replace all with `_<PROJECT>___<PATH>___<FILE>__H_` format:
- `WINDMI_UTILS_CONFIG_HPP_H_` for `include/utils/Config.hpp`
- `WINDMI_UTILS_LOGGER_HPP_H_` for `include/utils/Logger.hpp`
- `WINDMI_CONFIG_H_H_` for `include/config.h`
- `WINDMI_MODBUS_CLIENT_H_H_` for `include/modbus_client.h`
- etc.

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

### 7. Minor pointer alignment

Ensure `char* c` not `char *c` where applicable.

## Order of Execution

1. **Web UI changes** (remove Power Monitoring) — feature change
2. **Include guard standardization** — mechanical, broadest impact
3. **Include ordering** — mechanical
4. **Trailing whitespace, tabs, casts** — trivial cleanup

## Risks & Mitigations

- Include guard changes: purely mechanical, no logic change. Verified by running `ctest`.
- Web UI changes: remove DOM elements and JS references. The C++ side keeps all data — just doesn't display it.
- COP calculation uses `power_valid` and `ac_power_w` — those fields stay in the struct and JSON.

## Verification

Run `ctest --test-dir build -C Release --output-on-failure` after changes.
