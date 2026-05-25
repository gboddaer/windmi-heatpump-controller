# Rotenso Windmi Heat Pump Web Controller

**Date:** 2026-05-24  
**Version:** 0.2 (Draft)  
**Status:** Design Phase

## Overview

A single-process embedded web server that provides a user-friendly interface for controlling a Rotenso Windmi 8kW heat pump. The system uses the Mongoose embedded webserver library and communicates with the heat pump via Modbus TCP/RTU through a Waveshare gateway.

## Requirements

### Functional Requirements

1. **Display Current Status**
   - Show DHW tank temperature
   - Show leaving water temperature
   - Show outdoor temperature
   - Show current operating mode (heating/cooling/DHW)
   - Show system status (running/stopped)

2. **Control DHW Temperature**
   - Set DHW target temperature (range: 40.0-63.0°C, raw 400-630)
   - Immediate application of new setpoint
   - Read-back verification after write to confirm setpoint was accepted
   - Visual feedback when setting is applied (success/failure)

3. **Control Heating Temperature**
   - Set heating water setpoint (range: 25.0-63.0°C, raw 250-630)
   - Immediate application of new setpoint
   - Read-back verification after write to confirm setpoint was accepted
   - Visual feedback when setting is applied (success/failure)

4. **Priority Control**
   - Toggle between DHW priority and Heating priority
   - When DHW priority is active and DHW needs heating:
     - Pause space heating compressor
     - Focus on heating DHW tank
   - When heating priority is active:
     - Continuous space heating
     - DHW on schedule or manual boost

5. **REST API**
   - `GET /api/status` - Return current system status
   - `POST /api/set-dhw` - Set DHW temperature (with read-back verification)
   - `POST /api/set-heating` - Set heating temperature (with read-back verification)
   - `POST /api/set-priority` - Set priority mode (with read-back verification)
   - All endpoints return JSON

6. **Write Verification**
   - Every Modbus write operation must be immediately followed by a read-back of the same register
   - If the read-back value matches the written value, report success
   - If the read-back value differs, report failure to the caller
   - This applies to: RunningMode, DhwTempNormalSetpoint, OccupiedHeatingWaterSetpoint, DhwPriority

### Non-Functional Requirements

1. **Performance**
   - Web server response time < 100ms
   - Modbus polling interval: 30 seconds
   - Control loop execution: every 30 seconds

2. **Reliability**
   - Automatic reconnection on Modbus disconnect
   - Retry failed Modbus requests (max 3 attempts)
   - Graceful error handling

3. **Resource Usage**
   - Memory footprint < 10MB
   - CPU usage < 5% on typical embedded hardware

4. **Security**
   - Input validation on all user inputs
   - Rate limiting (1 request/second per client)
   - No authentication for v1 (local network assumption)

## Architecture

### System Components

```
+------------------+                   +-------------------+
|   Web Server     |  status_queue ---> |   Control Loop    |
|   (mongoose)     |                   |   (priority mgmt) |
|   main thread    |  <--- cmd_queue   |   modbus thread    |
+------------------+                   +-------------------+
         |                                      |
         v                                      v
+------------------+                   +------------------+
|   Static Files   |                   |  Modbus Client   |
|   (HTML/CSS/JS)  |                   |  (TCP/RTU)       |
+------------------+                   +------------------+
                                              |
                                              v
                                       +------------------+
                                       |  Waveshare GW    |
                                       |  (TCP:8899)      |
                                       |  (transparent)   |
                                       +------------------+
                                              |
                                              v
                                       +------------------+
                                       |  Rotenso Heat    |
                                       |  Pump (Modbus)   |
                                       +------------------+
```

### Thread Model & Communication

The application uses two threads:

1. **Main thread** — runs the Mongoose event loop (`mg_mgr_poll`), handles HTTP requests and serves static files. Never blocks on Modbus I/O.
2. **Modbus thread** — manages the TCP connection to the Waveshare gateway, runs the control loop, processes read/write requests.

Communication between threads uses two lock-free Single-Producer Single-Consumer (SPSC) queues:

- **`cmd_queue`** (main → modbus): Commands from the web server (set temperature, set priority). The web server enqueues a command and immediately returns `202 Accepted` to the client. The modbus thread dequeues and executes.
- **`status_queue`** (modbus → main): The modbus thread enqueues status snapshots after each poll cycle. The main thread dequeues on each `GET /api/status` request.

This avoids hiccups in the web server: no mutex contention, no blocking on slow Modbus round-trips, and the UI remains responsive at all times.

### Module Specifications

#### 1. Web Server Module (`web_server.c`)

**Responsibilities:**
- Initialize and run mongoose HTTP server
- Route incoming HTTP requests
- Serve static files (HTML, CSS, JS)
- Handle REST API endpoints
- JSON request/response parsing

**API Endpoints:**

| Method | Path | Description | Request Body | Response |
|--------|------|-------------|--------------|----------|
| GET | `/` | Main UI page | - | HTML |
| GET | `/api/status` | Current system status | - | JSON |
| POST | `/api/set-dhw` | Set DHW temperature | `{"temperature": 50}` | `{"success":true,"verified":true}` or `{"success":false,"error":"readback mismatch"}` |
| POST | `/api/set-heating` | Set heating temp | `{"temperature": 45}` | `{"success":true,"verified":true}` or `{"success":false,"error":"readback mismatch"}` |
| POST | `/api/set-priority` | Set priority | `{"priority": "dhw"}` | `{"success":true,"verified":true}` or `{"success":false,"error":"readback mismatch"}` |

**JSON Schema - Status Response:**
```json
{
  "dhwTemperature": 45.2,
  "dhwTarget": 50.0,
  "heatingTemperature": 42.0,
  "heatingTarget": 45.0,
  "outdoorTemperature": 8.5,
  "leavingWaterTemperature": 43.0,
  "mode": "heating",
  "priority": "dhw",
  "status": "running",
  "deviceOnline": true
}
```

**Error Handling:**
- Invalid JSON → `400 Bad Request`
- Invalid temperature value → `422 Unprocessable Entity`
- Modbus communication error → `503 Service Unavailable`

#### 2. Modbus Client Module (`modbus_client.c`)

**Responsibilities:**
- Maintain TCP connection to Waveshare gateway (transparent mode)
- Build and send Modbus RTU frames over raw TCP socket
- Parse responses with CRC16 verification
- Implement CRC16 calculation
- Automatic reconnection on disconnect
- Read-back verification after every write
- Proper framed reads (read header first, then remaining bytes)

**Configuration:**
```c
// MODBUS_GATEWAY_IP can be overridden via --ip command-line argument
#define MODBUS_GATEWAY_IP      "192.168.123.10"
#define MODBUS_GATEWAY_PORT    8899       // Waveshare transparent mode port
#define MODBUS_SLAVE_ID      11
#define MODBUS_TIMEOUT_MS    1000
#define MODBUS_MAX_RETRIES   3
```

**Important:** The Waveshare gateway operates in transparent mode (raw TCP ↔ RS485). There is no MBAP header — full Modbus RTU frames including CRC are sent and received over the TCP socket. Only function code 0x03 (read holding registers) works for reads; function code 0x04 returns exception 04 on this firmware.

**Register Map (Key Registers):**

| Register | Address | Name | Access | Scale | Min Raw | Max Raw |
|----------|---------|------|--------|-------|---------|---------|
| Outdoor Temp | 0x0001 | OutdoorAirTemperature | R | temp×10 (signed) | — | — |
| Indoor Temp | 0x0002 | IndoorAirTemperature | R | temp×10 (signed) | — | — |
| Leaving Water | 0x0004 | LeavingWaterTemperatureT1 | R | temp×10 (signed) | — | — |
| DHW Tank Temp | 0x1C5B | DhwTankTemperature | R | temp×10 (signed) | — | — |
| Running Mode | 0x002D | RunningMode | R | enum | 0 | 20 |
| DHW Target | 0x0194 | DhwTempNormalSetpoint | R/W | temp×10 | 400 | 630 |
| Heating Target | 0x0191 | OccupiedHeatingWaterSetpoint | R/W | temp×10 | 250 | 630 |
| DHW Priority | 0x028F | DhwPriority | R/W | bool (0/1) | 0 | 1 |

**Running Mode Values (verified from hardware):**

| Value | Mode |
|-------|------|
| 0 | Off |
| 1 | Cool |
| 2 | Heat |
| 4 | DHW |
| 7 | Defrost |
| 20 | Home Anti-Freeze |

**Note:** Values 1=Cool and 2=Heat differ from typical Modbus conventions. There is no mode value 3.

**Temperature Encoding:**
- All temperatures are stored as signed 16-bit integers (int16_t), scaled by ×10
- For example: -5.0°C → raw 0xFFEC (−12 × 10 as int16), 21.5°C → raw 0x00D7 (215)
- Unsigned types must NOT be used for temperature registers; outdoor temperatures can be negative

**Frame Format (Modbus RTU over TCP, transparent mode):**
```
Request: [SlaveID][FuncCode][RegAddrHi][RegAddrLo][RegCountHi][RegCountLo][CRC_Lo][CRC_Hi]
Example: 0B 03 00 01 00 01 D5 60 (Read outdoor temp)

Response: [SlaveID][FuncCode][ByteCount][DataHi][DataLo][CRC_Lo][CRC_Hi]
Example: 0B 03 02 00 D7 D3 45 (Value: 0x00D7 = 215 → 21.5°C)

Exception: [SlaveID][FuncCode|0x80][ExceptionCode][CRC_Lo][CRC_Hi]
Example: 0B 83 04 XX XX (Exception code 4: Slave Device Failure)
```

**Framed Read Protocol:**
1. Write request frame to socket
2. Read response: first read minimum header (5 bytes for error, or up to receiving byte count), then read remaining data + 2 CRC bytes
3. Verify CRC on every received frame; discard and retry on mismatch
4. On exception response (function code & 0x80), parse exception code and return error

**Write-then-Verify Protocol:**
1. Write register with function code 0x06 (write single register)
2. Read echo response (8 bytes), verify CRC, compare echoed address and value
3. Immediately read back the same register with function code 0x03
4. Compare read-back value with the value that was written
5. Return success only if values match

**CRC16 Algorithm:**
- Polynomial: 0x8005
- Initial value: 0xFFFF
- RefIn: true, RefOut: true
- XorOut: 0x0000

#### 3. Control Loop Module (`control_loop.c`)

**Responsibilities:**
- Execute every 30 seconds on the modbus thread
- Read current temperatures and mode
- Evaluate priority logic
- Adjust setpoints if needed (with write-then-verify)
- Monitor target achievement
- Enqueue status snapshots to `status_queue` after each poll

**Priority Logic:**

```c
void control_loop(void) {
    Status status = read_all_registers();
    
    if (config.priority == DHW_PRIORITY) {
        if (needs_dhw_heating(status)) {
            // tank_temp < dhw_target - 3°C: switch to DHW mode
            if (status.mode == MODE_HEAT) {
                set_running_mode_with_verify(MODE_DHW);
            }
        } else if (dhw_target_reached(status)) {
            // tank_temp >= dhw_target: resume heating if needed
            if (needs_space_heating(status)) {
                set_running_mode_with_verify(MODE_HEAT);
            }
        }
    }
    // Heating priority: continuous heating, DHW on schedule
}
```

**Decision Criteria:**
- `needs_dhw_heating()`: tank_temp < dhw_target - 3°C (switches to DHW priority mode)
- `dhw_target_reached()`: tank_temp >= dhw_target (switches back to heating mode)
- The 3°C hysteresis band below setpoint prevents short-cycling: DHW kicks in early (3°C below target) and continues heating until the target is reached
- `needs_space_heating()`: true when leaving_water_temp < heating_target - 1°C (uses leaving water temperature, not indoor temp, since indoor temp register may not always be available)

**State Machine:**
```
           +----------+
           |  IDLE    |
           +----+-----+
                |
    needs_dhw + | - needs_heating
                v
           +----+-----+     needs_heating     +----------+
           |   DHW    |<----------------------+  HEATING |
           +----+-----+                       +----+-----+
                |                                   |
                +-----------------------------------+
                        dhw_target_reached
```

#### 4. Configuration Module (`config.h`)

```c
// Network Configuration
#define WEB_SERVER_PORT        8080
#define WEB_SERVER_IP          "0.0.0.0"  // Listen on all interfaces

// Modbus Configuration (IP can be overridden via --ip command-line argument)
#define MODBUS_GATEWAY_IP      "192.168.123.10"
#define MODBUS_GATEWAY_PORT    8899       // Waveshare transparent mode
#define MODBUS_SLAVE_ID        11
#define MODBUS_POLL_INTERVAL_S 30

// Note: Only function code 0x03 (read holding registers) and 0x06 (write
// single register) are supported. Function code 0x04 returns exception 04
// on this firmware.

// Temperature Ranges (from Rotenso register specification)
#define DHW_TEMP_MIN           40.0f      // raw 400 = 40.0°C
#define DHW_TEMP_MAX           63.0f      // raw 630 = 63.0°C
#define HEATING_TEMP_MIN       25.0f      // raw 250 = 25.0°C
#define HEATING_TEMP_MAX       63.0f      // raw 630 = 63.0°C

// Control Loop
#define CONTROL_LOOP_INTERVAL_S 30
#define DHW_HYSTERESIS_C       3.0f
#define HEATING_HYSTERESIS_C   1.0f

// Timeouts and Retries
#define MODBUS_TIMEOUT_MS      1000
#define MODBUS_MAX_RETRIES     3
#define MODBUS_RECONNECT_INTERVAL_S 10

// SPSC Queue sizes
#define CMD_QUEUE_SIZE         16
#define STATUS_QUEUE_SIZE      4
```

#### 5. SPSC Queue Module (`spsc_queue.h`)

Lock-free single-producer single-consumer queue for thread communication.

**`cmd_queue` (main → modbus):** Commands from the web server. Each command carries an operation type and value:
```c
typedef enum {
    CMD_SET_DHW_TEMP,
    CMD_SET_HEATING_TEMP,
    CMD_SET_PRIORITY,
} cmd_type_t;

typedef struct {
    cmd_type_t type;
    float float_val;
    int int_val;
} cmd_t;
```

**`status_queue` (modbus → main):** Status snapshots consumed by `GET /api/status`:
```c
typedef struct {
    float outdoor_temp;
    float indoor_temp;
    float leaving_water_temp;
    float dhw_tank_temp;
    float dhw_target;
    float heating_target;
    int running_mode;       // RunningMode enum values
    bool dhw_priority;
    bool is_running;
    bool device_online;
} status_snapshot_t;
```

**Queue semantics:**
- Non-blocking enqueue returns `false` if queue is full (caller handles discard)
- The web server's `GET /api/status` dequeues the latest snapshot; if the queue is empty, it returns the last-known snapshot (never blocks)
- The modbus thread enqueues one snapshot after each poll cycle

#### 6. Startup Self-Test Module (`selftest.c`)

**Responsibilities:**
- Verify all register addresses and command sequences against the actual heat pump
- Run unintrusively: read current values, write-back same values, verify round-trips
- Report success/failure per register
- Block control loop from running until self-test completes

**Test Sequence:**
```
For each readable register:
  1. Send read request (FC 0x03)
  2. Verify response has no exception and valid CRC
  3. Record value and pass/fail

For each writable register:
  1. Read current value (FC 0x03)
  2. Write back the same value (FC 0x06) — no-op, no setting change
  3. Read back to verify (FC 0x03)
  4. Compare: must match the value written
  5. Record pass/fail

Report summary:
  - Total registers tested
  - Pass/fail per register
  - Overall: proceed if all critical registers pass
```

**Registers tested:**

| Register | Address | Test Type | Critical |
|----------|---------|-----------|----------|
| Outdoor Temp | 0x0001 | Read only | Yes |
| Indoor Temp | 0x0002 | Read only | No |
| Leaving Water Temp | 0x0004 | Read only | Yes |
| Running Mode | 0x002D | Read + write-back | Yes |
| DHW Target | 0x0194 | Read + write-back | Yes |
| Heating Target | 0x0191 | Read + write-back | Yes |
| DHW Priority | 0x028F | Read + write-back | Yes |
| DHW Tank Temp | 0x1C5B | Read only | Yes |

#### 7. UI Module (static/index.html)

**Based on existing `windmi-control.html` prototype.** The UI is evolved from the existing
mockup, preserving its visual design (priority buttons, status indicators, gradient theme)
while adding live data binding via the REST API.

**Features:**
- Responsive design (based on existing windmi-control.html styling)
- Real-time status display (auto-refresh every 10 seconds via `GET /api/status`)
- Temperature sliders with visual feedback
- Priority buttons (DHW Priority / Heating Priority) — mutually exclusive, not a toggle
- Offline/error state display ("Device Offline" shown when 3+ consecutive Modbus failures)
- Write verification feedback (show success/failure after setpoint changes)

**JavaScript Behavior:**
```javascript
// Auto-refresh status every 10 seconds
setInterval(fetchStatus, 10000);

// Temperature slider change (with write-verify feedback)
dhwSlider.oninput = function() {
    updateDisplay(this.value);
    debouncedSetDhw(this.value, function(success, verified) {
        if (!success || !verified) revertSlider();
        showFeedback(success && verified);
    });
};

// Priority buttons (mutually exclusive)
dhwPriorityBtn.onclick = function() { setPriority('dhw'); };
heatPriorityBtn.onclick = function() { setPriority('heating'); };
```

## Error Handling Strategy

### Modbus Errors

| Error | Action |
|-------|--------|
| Connection refused | Retry with exponential backoff (1s, 2s, 4s, ...) |
| Timeout | Retry up to 3 times, then mark device offline |
| Exception response (0x84) | Log error, skip this register, continue |
| CRC error | Discard frame, retry up to 3 times |
| Read-back mismatch | Report failure to caller; do not update local state |

### Write Verification Errors

| Error | Action |
|-------|--------|
| Write succeeded but read-back differs | Report `{"success": false, "error": "readback mismatch"}` to client |
| Write succeeded and read-back matches | Report `{"success": true, "verified": true}` to client |

### Web Server Errors

| Error | Response |
|-------|----------|
| Invalid JSON | 400 Bad Request |
| Missing field | 400 Bad Request |
| Out of range value | 422 Unprocessable Entity |
| Modbus unavailable | 503 Service Unavailable |

### Recovery Behavior

1. **Device Offline Detection**
   - After 3 consecutive failures, mark device offline
   - Enqueue offline status to `status_queue` for web server to display
   - Continue control loop cycle (skip priority logic while offline)

2. **Reconnection**
   - Modbus thread attempts reconnection every 10 seconds
   - On success, resume normal poll/control cycle
   - Enqueue restored-online status to `status_queue`
   - Log reconnection event

3. **Thread Failure Isolation**
   - If the modbus thread crashes, the web server remains responsive (serves stale status)
   - If the web server has issues, modbus thread continues control logic independently

## Testing Strategy

### Unit Tests

- CRC16 calculation (verify against known frame: `{0x0B, 0x03, 0x00, 0x01, 0x00, 0x01}` → CRC 0x60D5)
- Modbus frame building and parsing
- Temperature scaling (raw ↔ °C) including negative temperatures (int16_t)
- Priority logic decisions
- Read-back verification logic
- SPSC queue enqueue/dequeue

### Integration Tests

- Read register values from heat pump
- Write setpoint values
- Verify control loop behavior
- Test error recovery

### Manual Testing

- UI interaction (sliders, toggles)
- Temperature setpoint changes
- Priority switching
- Disconnection/reconnection

## Build Instructions

### Dependencies

- Mongoose library (v7.x or later)
- GCC (C99 support)
- make or cmake

### Compilation

```bash
# Clone mongoose
git clone https://github.com/cesanta/mongoose.git
cd mongoose && make -f Makefile.mongoose

# Build heat pump controller
gcc -o windmi-control main.c web_server.c modbus_client.c control_loop.c \
    spsc_queue.c crc16.c -lmongoose -lpthread -Wall -O2
```

### Running

```bash
./windmi-control
```

## Future Enhancements

1. **Authentication** - Basic auth or token-based
2. **HTTPS** - TLS support for secure communication
3. **Scheduled DHW** - Programmable DHW schedules
4. **Energy Monitoring** - Track energy consumption
5. **Remote Access** - VPN or secure tunnel for remote access
6. **Multi-zone Support** - Control multiple heating zones
7. **Weather Integration** - Adjust setpoints based on forecast

## Appendix: Register Map Reference

Complete register map from `docs/header_example.hpp`:

```c
// Readable Registers (use function code 0x03 only; 0x04 returns exception 04)
OutdoorAirTemperature       = 0x0001  // int16_t, °C = raw / 10 (can be negative)
IndoorAirTemperature        = 0x0002  // int16_t, °C = raw / 10
EnteringWaterTemperature    = 0x0003  // int16_t, °C = raw / 10
LeavingWaterTemperature     = 0x0004  // int16_t, °C = raw / 10
DhwTankTemperature          = 0x1C5B  // int16_t, °C = raw / 10
RunningMode                 = 0x002D  // enum: 0=Off,1=Cool,2=Heat,4=DHW,7=Defrost,20=AntiFreeze

// Writable Registers (function code 0x06, with read-back verification)
RunningMode                 = 0x002D  // enum (same values)
OccupiedHeatingWaterSetpoint = 0x0191  // uint16_t, raw=°C×10, range 250..630 (25.0°C..63.0°C)
DhwTempNormalSetpoint        = 0x0194  // uint16_t, raw=°C×10, range 400..630 (40.0°C..63.0°C)
DhwPriority                 = 0x028F  // uint16_t, 0=heating priority, 1=DHW priority
```

**Verified behavior (from `docs/learned.md`):**
- Waveshare gateway acts as transparent TCP ↔ RS485 bridge (raw Modbus RTU frames, no MBAP header)
- Default port: 8899 (not 502)
- Only function code 0x03 works for reads; 0x04 returns exception 04
- Function code 0x06 for write single register
- Temperature registers are signed (int16_t); outdoor temps can be negative
- CRC16 must be verified on all received frames
