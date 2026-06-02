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

### 3.1 Unit Tests (test_modbus_serial.cpp)
Location: `tests/test_modbus_serial.cpp`

```cpp
// CRC16 Calculation Tests
TEST(ModbusSerialClientTest, CRC16_Calculator) {
    // Test with known Modbus RTU CRC values
    // Example: 0x01 0x03 0x00 0x00 0x00 0x01 should have CRC 0x68 0x3B
    uint8_t frame[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01};
    uint16_t crc = ModbusSerialClient::calculateCRC16(frame, 6);
    EXPECT_EQ(crc, 0x3B68);  // Low byte first: 0x68, 0x3B
}

TEST(ModbusSerialClientTest, CRC16_EmptyBuffer) {
    uint8_t frame[] = {};
    uint16_t crc = ModbusSerialClient::calculateCRC16(frame, 0);
    EXPECT_EQ(crc, 0xFFFF);  // Initial CRC value
}

TEST(ModbusSerialClientTest, CRC16_SingleByte) {
    uint8_t frame[] = {0x01};
    uint16_t crc = ModbusSerialClient::calculateCRC16(frame, 1);
    // Verify CRC is calculated correctly
}

// Frame Encoding Tests
TEST(ModbusSerialClientTest, EncodeReadRegister) {
    // Encode: Slave 1, Read Register 0x02BF (DHW Target)
    uint8_t frame[8];
    size_t len = ModbusSerialClient::encodeReadRegister(1, 0x02BF, frame);
    EXPECT_EQ(len, 6);
    // Verify frame: [0x01][0x03][0x02][0xBF][CRC_H][CRC_L]
}

TEST(ModbusSerialClientTest, EncodeWriteRegister) {
    // Encode: Slave 1, Write 0x01 to Register 0x02BF
    uint8_t frame[8];
    size_t len = ModbusSerialClient::encodeWriteRegister(1, 0x02BF, 0x01, frame);
    EXPECT_EQ(len, 8);
    // Verify frame format
}

// Frame Decoding Tests
TEST(ModbusSerialClientTest, DecodeReadResponse) {
    // Simulate valid response: [0x01][0x03][0x02][0x00][0x2E][CRC_H][CRC_L]
    uint8_t frame[] = {0x01, 0x03, 0x02, 0x00, 0x2E, 0x9F, 0x3D};
    uint16_t value;
    bool ok = ModbusSerialClient::decodeReadResponse(frame, 7, value);
    EXPECT_TRUE(ok);
    EXPECT_EQ(value, 0x002E);
}

TEST(ModbusSerialClientTest, DecodeResponse_InvalidCRC) {
    // Response with corrupted CRC
    uint8_t frame[] = {0x01, 0x03, 0x02, 0x00, 0x2E, 0x00, 0x00};
    uint16_t value;
    bool ok = ModbusSerialClient::decodeReadResponse(frame, 7, value);
    EXPECT_FALSE(ok);
}

TEST(ModbusSerialClientTest, DecodeErrorResponse) {
    // Modbus error response: [0x01][0x83][0x02][CRC_H][CRC_L]
    uint8_t frame[] = {0x01, 0x83, 0x02, 0x5E, 0x49};
    bool ok = ModbusSerialClient::isErrorResponse(frame, 5);
    EXPECT_TRUE(ok);
}

// Error Handling Tests
TEST(ModbusSerialClientTest, TimeoutDetection) {
    // Test that timeout exceptions are thrown correctly
    // when no response is received within expected time
}

TEST(ModbusSerialClientTest, InvalidSlaveAddress) {
    // Test handling of invalid slave address (0 or > 247)
}

TEST(ModbusSerialClientTest, RegisterAddressBounds) {
    // Test handling of register addresses outside valid range
}
```

### 3.2 Integration Tests
```bash
# Create loopback for testing
socat -d -d pty,raw,echo=0 pty,raw,echo=0

# Or connect to real device
./windmi-control --device /dev/ttyUSB0 --baud 9600
```

### 3.3 Test Scenarios
1. Valid serial connection
2. Invalid device path
3. Invalid baud rate
4. Connection timeout
5. CRC mismatch (corrupted data)
6. Timeout waiting for response
7. Partial/buffer overflow responses
8. Multiple sequential requests
9. Rapid consecutive requests
10. Error recovery after CRC failure

### 3.4 Manual Testing
```bash
# Connect to real device
./windmi-control --device /dev/ttyUSB0 --baud 9600

# Test different baud rates
./windmi-control --device /dev/ttyUSB0 --baud 19200

# Test with parity
./windmi-control --device /dev/ttyUSB0 --baud 9600 --parity even
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
