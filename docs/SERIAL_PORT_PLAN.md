# Serial Port Support Implementation Plan

## Overview
Add support for serial port communication (e.g., `/dev/ttyUSB0`, `/dev/ttyACM0`) in addition to the existing socket-based Modbus TCP communication. This will allow connecting to devices via USB-to-Serial adapters (FTDI, CP210x, etc.).

## Current State

### Existing Communication
- **Socket-based Modbus TCP** (`src/modbus/ModbusClient.cpp`)
  - Uses `ModbusClient::connect(host, port)` with socket operations
  - CLI flags: `-p <port>` (default: 502), `-h <host>` (default: localhost)
  - Configuration: `include/config.h` defines port constants

### CLI Interface (Current)
```
windmi-control [-h host] [-p port] [-w webport] [-i interval] [-d] [--log-level level]
```

## Requirements

### New Communication Modes
1. **Socket (Modbus TCP)** - Existing, unchanged
2. **Serial (Modbus RTU over RS485)** - NEW

### CLI Changes
```
windmi-control [SOCKET-OPTIONS | SERIAL-OPTIONS] [-w webport] [-i interval] [-d] [--log-level level]

Socket Options:
  -h, --host <host>     Modbus TCP host (default: localhost)
  -p, --port <port>     Modbus TCP port (default: 502)

Serial Options:
  -d, --device <path>   Serial device path (e.g., /dev/ttyUSB0)
  -b, --baud <rate>     Serial baud rate (default: 9600)
  --parity <type>       Parity: none, even, odd (default: none)
  --data-bits <bits>    Data bits: 7, 8 (default: 8)
  --stop-bits <bits>    Stop bits: 1, 2 (default: 1)
```

### Usage Examples
```bash
# Socket communication (existing behavior)
./windmi-control -h 192.168.1.100 -p 502

# Serial communication (new)
./windmi-control --device /dev/ttyUSB0 --baud 9600

# Demo mode (unchanged)
./windmi-control -d
```

## Implementation Plan

### Phase 1: Modbus Client Architecture
**Goal:** Support both socket and serial communication through a unified interface

1. **Create `ModbusSerialClient` class**
   - Location: `src/modbus/ModbusSerialClient.h/cpp`
   - Inherits from `ModbusClient` base class (or implements same interface)
   - Methods:
     - `connect(const std::string& device, int baud, Parity parity, int dataBits, int stopBits)`
     - `disconnect()`
     - `readRegister(uint16_t address) -> uint16_t`
     - `writeRegister(uint16_t address, uint16_t value)`

2. **Add Serial Port Configuration**
   - Location: `include/config.h`
   ```cpp
   // Serial port defaults
   #define SERIAL_DEFAULT_BAUD 9600
   #define SERIAL_DEFAULT_PARITY 'N'  // none
   #define SERIAL_DEFAULT_DATA_BITS 8
   #define SERIAL_DEFAULT_STOP_BITS 1
   ```

3. **Add Modbus RTU Parameters**
   - Location: `include/modbus/ModbusSerialClient.h`
   ```cpp
   enum class Parity {
       NONE = 0,
       EVEN = 1,
       ODD = 2
   };
   ```

### Phase 2: Serial Communication Implementation
**Goal:** Implement Modbus RTU over serial using termios

1. **Serial Port Setup (termios)**
   - Open serial device with `open(device, O_RDWR | O_NOCTTY | O_NONBLOCK)`
   - Configure termios settings:
     - Baud rate (use `cfsetspeed()` or manual setup)
     - 8N1 or configurable (data bits, parity, stop bits)
     - Raw mode (`cfmakeraw()` or manual setup)
     - Read timeout (VTIME, VMIN)

2. **Modbus RTU Frame Handling**
   - Calculate CRC16 for each frame
   - Add CRC to outgoing frames (low byte first, then high byte)
   - Verify CRC on incoming frames
   - Handle response timeout (typically 1-3 character times)

3. **Threading Considerations**
   - Serial port is blocking by default
   - Consider using non-blocking I/O with select/poll
   - Or use a dedicated read thread with timeout

### Phase 3: Main Application Changes
**Goal:** Integrate serial support into main application

1. **Update CLI Argument Parsing**
   - Location: `src/main.cpp`
   - Add `--device`, `--baud`, `--parity`, `--data-bits`, `--stop-bits` options
   - Add validation: either socket options OR serial device must be specified
   - Add mutual exclusion: `-h`/`-p` vs `--device` are mutually exclusive

2. **Update Modbus Client Instantiation**
   - Location: `src/main.cpp` and `src/core/ControlLoop.cpp`
   - If `--device` is specified: create `ModbusSerialClient`
   - Else: create `ModbusClient` (socket)
   - Pass appropriate connection parameters

3. **Update Demo Mode**
   - Demo mode should continue to use `SimulatedModbusClient`
   - Serial mode should NOT be available in demo mode

### Phase 4: Build System Changes
**Goal:** Add serial port support to CMake build

1. **Add termios Check**
   - Location: `CMakeLists.txt`
   - termios is POSIX standard, available on Linux
   - No external dependencies needed

2. **Update Documentation**
   - Location: `README.md`
   - Add section: "Serial Port Connection"
   - Add examples for USB-to-Serial adapters

### Phase 5: Testing
**Goal:** Verify serial communication works correctly

1. **Unit Tests**
   - Add `test_modbus_serial.cpp`
   - Test CRC16 calculation
   - Test frame encoding/decoding
   - Test error handling (timeout, CRC mismatch)

2. **Integration Tests**
   - Connect to actual device or use serial loopback
   - Test read/write operations
   - Test error recovery

3. **Test Scenarios**
   - Valid serial connection
   - Invalid device path
   - Invalid baud rate
   - Connection timeout
   - CRC mismatch (corrupted data)

## Files to Create/Modify

### New Files
- `src/modbus/ModbusSerialClient.h`
- `src/modbus/ModbusSerialClient.cpp`
- `tests/test_modbus_serial.cpp`

### Modified Files
- `include/config.h` - Add serial configuration constants
- `include/modbus/ModbusClient.h` - Add base class or interface
- `src/main.cpp` - Update CLI parsing and client creation
- `src/core/ControlLoop.cpp` - Update to work with any Modbus client
- `CMakeLists.txt` - Add serial client to build
- `README.md` - Add serial connection documentation

## Design Decisions

1. **Inheritance vs Interface**
   - Decision: Keep current `ModbusClient` as-is, add `ModbusSerialClient` as separate class
   - Reason: Minimal changes to existing code, socket and serial have different connection models

2. **Error Handling**
   - Serial errors: device not found, permission denied, invalid baud rate
   - Modbus RTU errors: timeout, CRC mismatch, unexpected response
   - Return error codes or throw exceptions (consistent with existing `ModbusException`)

3. **Timeout Configuration**
   - Default: 1 second for socket, 2 seconds for serial (slower due to baud rate)
   - Make configurable via CLI or config.h

4. **Baud Rate Support**
   - Common rates: 9600, 19200, 38400, 57600, 115200
   - Windmi default: 9600 (verify from documentation)
   - Validate baud rate against supported values

5. **Parity Support**
   - None (most common)
   - Even (some industrial devices)
   - Odd (rare)
   - Default to none for simplicity

## Testing Strategy

1. **Unit Tests**
   ```cpp
   TEST(ModbusSerialClient, CalculateCRC16) {
       // Test CRC calculation with known values
   }
   
   TEST(ModbusSerialClient, FrameEncoding) {
       // Test Modbus RTU frame format
   }
   ```

2. **Integration Tests**
   - Use USB-to-Serial adapter connected to real Windmi
   - Or use serial loopback with socat
   ```bash
   # Create loopback for testing
   socat -d -d pty,raw,echo=0 pty,raw,echo=0
   ```

3. **Manual Testing**
   ```bash
   # Connect to real device
   ./windmi-control --device /dev/ttyUSB0 --baud 9600
   ```

## Rollout Strategy

1. Implement and test serial support
2. Merge to `main` branch
3. Update documentation
4. Create release with serial support
5. Monitor for serial-specific issues

## References

- Modbus RTU specification
- termios documentation: `man 3 termios`
- Windmi Modbus register map
