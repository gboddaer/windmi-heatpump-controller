# DHW Hysteresis Rollback and New Register Additions

## Context

DHW Hysteresis register `0x025F` returns invalid values (450 instead of 0-10°C range) on the actual device firmware. It's from the generic GCHV protocol but doesn't map correctly on this Rotenso Windmi firmware. Removing it entirely.

Three new read-only registers are needed for better monitoring visibility in the Web UI:
- `0x0239` — Water Delta T Setpoint (confirmed in hvdb gist and official manual)
- `0x1004` — Actual Capacity Output (already tracked in code, needs JSON + UI)
- `0x00D2` — DHW Valve Status (confirmed in official manual GCHV No. 83)

## Changes

### Remove DHW Hysteresis
| File | Change |
|------|--------|
| `include/config.h` | Remove `REG_DHW_HYSTERESIS`, `DHW_HYSTERESIS_MIN/MAX/DEFAULT` |
| `include/core/ControlLoop.hpp` | Remove `dhw_hysteresis` from `StatusSnapshot`, `CMD_SET_DHW_HYSTERESIS` from enum |
| `src/core/ControlLoop.cpp` | Remove read of `REG_DHW_HYSTERESIS`, remove `CMD_SET_DHW_HYSTERESIS` case |
| `include/utils/JsonHelpers.hpp` | Remove `dhw_hysteresis` param from `generateStatusJson` |
| `src/utils/JsonHelpers.cpp` | Remove `dhwHysteresis` from JSON output |
| `include/web/WebServer.hpp` | Remove `handle_set_dhw_hysteresis` declaration |
| `src/web/WebServer.cpp` | Remove `/api/set-dhw-hysteresis` endpoint handler |
| `src/main.cpp` | Remove `--dhw-hysteresis` CLI arg |
| `include/selftest.hpp` | Update test count from 7 to 6 |
| `src/selftest.cpp` | Remove hysteresis test |
| `static/index.html` | Remove DHW Hysteresis controls |
| `static/app.js` | Remove hysteresis UI logic |
| `tests/tools/probe_registers.c` | Remove from probe list |
| All test files | Update `generateStatusJson` calls |

### Add Water Delta T Setpoint (`0x0239`)
- RO, unit: °C (raw × 1 = °C), range 0-10°C
- Add to `StatusSnapshot`, read in `readStatus()`, JSON field `waterDeltaT`, Web UI read-only display

### Add Actual Capacity Output (`0x1004`)
- Already in `StatusSnapshot` and read in `readStatus()`
- JSON field `actualCapacityOutput`, Web UI read-only display with unit indicator

### Add DHW Valve Status (`0x00D2`)
- RO, 0=Opened, 1=Closed
- Add to `StatusSnapshot`, read in `readStatus()`, JSON field `dhwValveStatus`, Web UI read-only display
