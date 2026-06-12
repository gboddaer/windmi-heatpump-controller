# Weekly Schedule Implementation Plan

## Phase 1: Backend — Scheduler Engine

### Task 1.1: Create Scheduler class
- **Files**: `include/core/Scheduler.hpp`, `src/core/Scheduler.cpp`
- **Work**: 
  - Define `ScheduleRange` struct (start, end, mode, heatingTemp, dhwTemp)
  - `Scheduler` class: `load()`, `evaluate(minutes)`, `isEnabled()`, `setEnabled()`, `pause()`, `resume()`, `isPaused()`
  - Range validation: no overlaps, must cover 0-1440, sort by start time
- **Tests**: `tests/core/test_scheduler.cpp` — evaluate at various times, edge cases (midnight, gaps, overlaps), pause/resume

### Task 1.2: Create ScheduleConfig for JSON persistence
- **Files**: `include/utils/ScheduleConfig.hpp`, `src/utils/ScheduleConfig.cpp`
- **Work**:
  - Load/save `~/.windmi/schedule.json`
  - JSON schema: `{ enabled, ranges: [...] }`
  - Validation on load (reject invalid ranges)
- **Tests**: `tests/utils/test_schedule_config.cpp` — valid/invalid JSON, missing fields, round-trip

### Task 1.3: Integrate Scheduler into ControlLoop
- **Files**: `include/core/ControlLoop.hpp`, `src/core/ControlLoop.cpp`
- **Work**:
  - Add `Scheduler` member to `ControlLoop`
  - In `threadFunc()`: after readStatus, call `scheduler.evaluate()` 
  - If active range changed, push CMD_SET_RUNNING_MODE + CMD_SET_DHW_TEMP / CMD_SET_HEATING_TEMP to CmdQueue
  - Add `scheduleActive` and `schedulePaused` to `StatusSnapshot`
  - Manual mode/temp changes set `schedulePaused = true`
- **Tests**: Update existing control loop tests

### Task 1.4: Build and test
- **Work**: Compile, run all tests, verify scheduler integration

## Phase 2: API — Schedule Endpoints

### Task 2.1: Add schedule API endpoints
- **Files**: `include/web/WebServer.hpp`, `src/web/WebServer.cpp`
- **Work**:
  - `GET /api/schedule` — return current schedule JSON
  - `POST /api/schedule` — save full schedule (replace)
  - `POST /api/schedule/toggle` — enable/disable
  - `POST /api/schedule/resume` — resume after manual override
  - Wire Scheduler instance into WebServer

### Task 2.2: Add schedule to status JSON
- **Files**: `include/utils/JsonHelpers.hpp`, `src/utils/JsonHelpers.cpp`
- **Work**:
  - Add `scheduleActive` and `schedulePaused` fields to status JSON response

### Task 2.3: Wire up in main.cpp
- **Files**: `src/main.cpp`
- **Work**:
  - Load schedule from file on startup
  - Pass Scheduler reference to ControlLoop and WebServer

### Task 2.4: Build and test
- **Work**: Compile, run all tests, test API endpoints with curl

## Phase 3: UI — Schedule Tab

### Task 3.1: Schedule tab HTML
- **Files**: `static/index.html`
- **Work**:
  - Replace Schedule tab placeholder with card list
  - Each card: time range, mode badge, temperature(s)
  - "+ Add" button, Save/Reset buttons
  - Enable/disable toggle

### Task 3.2: Schedule tab CSS
- **Files**: `static/style.css`
- **Work**:
  - Mobile-first card styles
  - Mode badge colors (matching Overview)
  - Responsive layout

### Task 3.3: Schedule tab JS
- **Files**: `static/app.js`
- **Work**:
  - Fetch schedule on load, render cards
  - Edit dialog for each range (mode selector, temp inputs)
  - Add/remove ranges
  - Save to API, show confirmation
  - Enable/disable toggle
  - Resume schedule button (when paused)

### Task 3.4: Overview indicator
- **Files**: `static/index.html`, `static/app.js`, `static/style.css`
- **Work**:
  - Show "Schedule active" indicator in Overview tab when schedule is driving changes
  - "Schedule paused" state when user overrides manually

### Task 3.5: Build and test
- **Work**: Full integration test, verify schedule drives mode/temp changes

## Phase 4: Tests

### Task 4.1: Scheduler unit tests
- **Files**: `tests/core/test_scheduler.cpp`
- **Tests**: 10+ tests covering evaluation, validation, pause/resume

### Task 4.2: ScheduleConfig tests
- **Files**: `tests/utils/test_schedule_config.cpp`
- **Tests**: 5+ tests covering load/save/validation

### Task 4.3: API tests
- **Files**: `tests/web/test_web_server_api.cpp`
- **Tests**: 4+ tests for schedule CRUD endpoints

### Task 4.4: Final verification
- **Work**: All 13+ test suites pass, clean build, no warnings
