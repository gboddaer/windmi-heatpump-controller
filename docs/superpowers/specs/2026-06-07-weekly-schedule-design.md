# Weekly Schedule Design

## Overview

App-managed weekly heating and DHW schedule. Replaces manual mode/temperature changes with time-based automation. Stored and evaluated entirely in the application — no Modbus schedule registers used.

## Decisions

- **Structure**: List of time ranges (start–end), each with mode + temperature(s)
- **Scope**: One schedule repeating daily (no day groups, no per-day override)
- **UI**: Mobile-first vertical card list on Schedule tab (tab 2)
- **Granularity**: Times in minutes (00:00–23:59), no fixed 30-min grid

## Data Model

```
ScheduleRange {
    startTime: int    // minutes from midnight (0-1439)
    endTime: int      // minutes from midnight (1-1440, 1440 = midnight)
    mode: int         // 0=OFF, 1=DHW Only, 2=Heating Only, 3=Heating+DHW
    heatingTemp: float
    dhwTemp: float
}
```

Ranges must not overlap and must cover 24 hours (00:00–24:00). Gaps are not allowed.

## Storage

Saved to `~/.windmi/schedule.json` (separate from `settings.ini`):

```json
{
    "enabled": true,
    "ranges": [
        { "start": 0, "end": 360, "mode": 0 },
        { "start": 360, "end": 480, "mode": 2, "heatingTemp": 38.0 },
        { "start": 480, "end": 720, "mode": 3, "heatingTemp": 40.0, "dhwTemp": 50.0 },
        { "start": 720, "end": 1020, "mode": 2, "heatingTemp": 35.0 },
        { "start": 1020, "end": 1320, "mode": 3, "heatingTemp": 42.0, "dhwTemp": 50.0 },
        { "start": 1320, "end": 1440, "mode": 0 }
    ]
}
```

## Architecture

### New Files

- `include/core/Scheduler.hpp` / `src/core/Scheduler.cpp` — Schedule evaluation engine
- `include/utils/ScheduleConfig.hpp` / `src/utils/ScheduleConfig.cpp` — JSON load/save

### Modified Files

- `include/core/ControlLoop.hpp` / `src/core/ControlLoop.cpp` — Scheduler integration in control loop
- `include/web/WebServer.hpp` / `src/web/WebServer.cpp` — Schedule CRUD API endpoints
- `include/utils/JsonHelpers.hpp` / `src/utils/JsonHelpers.cpp` — Schedule JSON serialization
- `static/index.html` — Schedule tab UI (already exists as empty placeholder)
- `static/app.js` — Schedule tab JS (render, edit, save)
- `static/style.css` — Schedule card styles
- `src/main.cpp` — Load schedule on startup, pass to ControlLoop
- `CMakeLists.txt` — New source files

### Scheduler Engine

The `Scheduler` class:

- Holds the list of ranges and an enabled flag
- `evaluate()` takes current time (minutes from midnight), returns the active `ScheduleRange`
- The control loop calls `evaluate()` each tick; if the active range changed from the previous tick, it pushes the appropriate commands (mode + temperatures) to the CmdQueue
- A "schedule active" flag in the Overview tab shows when schedule automation is controlling the device
- Manual mode/temperature changes from Overview **pause** the schedule (user override). Schedule resumes after next range boundary or explicit "Resume schedule" button.

### API Endpoints

| Method | Path | Body | Description |
|--------|------|------|-------------|
| GET | `/api/schedule` | — | Return current schedule JSON |
| POST | `/api/schedule` | `{...}` | Save full schedule (replace) |
| POST | `/api/schedule/toggle` | `{ "enabled": true }` | Enable/disable schedule |
| POST | `/api/schedule/resume` | — | Resume schedule after manual override |

### Control Loop Integration

In `ControlLoop::threadFunc()`, after `readStatus()` and before `applyControlLogic()`:

1. Call `scheduler.evaluate(currentMinutes)`
2. If result differs from last active range, push `CMD_SET_RUNNING_MODE` + `CMD_SET_DHW_TEMP` / `CMD_SET_HEATING_TEMP` to CmdQueue
3. Set `status.scheduleActive = true` when schedule is driving changes

### User Override

- Any manual change from Overview tab (mode button, temp slider) sets a `schedulePaused` flag
- While paused, `evaluate()` still runs but doesn't push commands
- "Resume schedule" button on Schedule tab clears the flag
- Schedule also auto-resumes at the next range boundary (optional — see below)

## Testing

- `test_scheduler.cpp`: Unit tests for range evaluation, edge cases (midnight wrap, gaps, overlaps)
- `test_schedule_config.cpp`: JSON load/save, validation
- Web server tests for schedule API endpoints

## Out of Scope (for now)

- Day groups (weekday/weekend patterns)
- Per-day overrides
- Holiday/exception handling
- Geofencing or presence detection
- UI visual tweaks (deferred to after implementation)
