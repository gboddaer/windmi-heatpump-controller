# Settings Persistence & Temperature Display Cleanup

## Date
2026-06-07

## Goals
1. Remove redundant DHW Target / Heating Target from Temperatures section (already in Controls)
2. Persist working mode, priority, and temperature targets across restarts
3. Eliminate the "DHW Only vs DHW+Heating" ambiguity on restart

## Design

### A. Temperature Display Cleanup

**Remove from Temperatures section:**
- `DHW Target` — redundant with DHW slider/input in Controls
- `Heating Target` — redundant with Heating slider/input in Controls

**Keep in Temperatures section:**
- `DHW Tank` — actual sensor reading
- `Heating (LWT)` — actual sensor reading

### B. Persistent Settings

**File location:** `~/.windmi/settings.ini` (default), overridable with `--config` CLI flag.

**Format:** INI-style (reuses existing `Config` class key=value parser).

**Keys:**
| Key | Type | Default | Description |
|---|---|---|---|
| `ui.working_mode` | int (0-3) | 3 | 0=Off, 1=DHW-only, 2=Heating-only, 3=DHW+Heating |
| `ui.priority` | string | dhw | DHW or Heating priority |
| `ui.dhw_target` | float | 45.0 | DHW target temperature (°C) |
| `ui.heating_target` | float | 40.0 | Heating target temperature (°C) |

**Flow:**
1. `main.cpp` creates `Config`, loads from file (if exists, ignore missing file)
2. ControlLoop reads `desired_working_mode_` from Config instead of hardcoding 3
3. On every mode/target change (via WebServer API), a `save_settings()` callback writes Config back to disk
4. WebServer passes Config reference to ControlLoop for initial values

**Changes to Config class:**
- Add `saveToFile(const std::string& filename)` method
- Write all keys in `values_` map as `key = value\n`

**Changes to main.cpp:**
- Add `--config <path>` CLI argument (default: `~/.windmi/settings.ini`)
- Load Config before ControlLoop creation
- Pass working mode, priority, targets from Config to ControlLoop
- Register save callback so settings persist on every change

**Changes to ControlLoop:**
- Accept initial working mode, priority, dhw target, heating target in constructor
- Add `onSettingsChanged` callback type and member
- Fire callback when mode/target/priority changes

**Changes to WebServer:**
- Accept save callback from main
- Fire save callback after successful mode/target/priority changes

### C. Testing
- Unit test for `Config::saveToFile` / round-trip load/save
- Verify settings file is created on first write
- Verify missing settings file is not an error on load
