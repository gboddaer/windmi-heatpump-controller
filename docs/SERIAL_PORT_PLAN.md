# Serial Port Support Implementation Plan

## Overview

Add support for direct serial port communication (e.g., `/dev/ttyUSB0`, `/dev/ttyACM0`) as an alternative to the existing TCP socket path. This enables connecting to the Windmi heat pump via USB-to-RS485 adapters (FTDI, CP210x, CH340, etc.) without requiring a Waveshare Modbus TCP gateway.

## Current State

### Existing Architecture

The system already uses **Modbus RTU framing over TCP** (Waveshare gateway in transparent mode):

1. **C layer** (`src/modbus_client.c`)
   - Builds standard Modbus RTU frames with CRC16
   - Sends them over a TCP socket to the Waveshare gateway
   - `modbus_client_t` stores: `ip`, `port`, `slave_id`, `socket_fd`, `connected`
   - Already has: `build_read_frame()`, `build_write_frame()`, `crc16()`
   - Frame handling: `send_frame()`, `receive_exact()`, flush, timeout via `select()`

2. **C++ layer** (`src/modbus/ModbusClient.cpp`)
   - `ModbusClient` wraps the C client, implements `IModbusClient`
   - Constructor takes `(host, port, slave_id)` and stores them
   - `connect()` / `disconnect()` / `readRegister()` / `writeRegister()` / `flushBuffer()`

3. **Interface** (`include/modbus/IModbusClient.hpp`)
   - `IModbusClient` — abstract base with `connect()`, `disconnect()`, `isConnected()`, `readRegister()`, `writeRegister()`, `flushBuffer()`, `getLastError()`
   - `ModbusException` — exception class for all Modbus errors
   - Implemented by: `ModbusClient` (TCP), `SimulatedModbusClient` (demo)

4. **CLI** (`src/main.cpp`)
   - `-i, --ip <addr>` — Modbus gateway IP (default: `MODBUS_GATEWAY_IP`)
   - `-p, --port <port>` — Modbus gateway port (default: `MODBUS_GATEWAY_PORT = 8899`)
   - **`-d, --demo` — demo mode (`-d` is already taken!)**
   - `-w, --web <port>` — web server port
   - `-l, --log-level <lvl>` — log level
   - `-s, --selftest` — run self-test and exit
   - `-f, --force` — force start

5. **Shutdown** — writes OFF mode via a **separate** `ModbusClient` instance created with the same host/port.

6. **Self-test** (`src/selftest.c`) — uses `modbus_client_t*` (C struct) directly, not the C++ interface.

### CRC16

Already implemented in `src/crc16.cpp` / `include/crc16.h`. Existing tests in `tests/utils/test_crc16.cpp`. The Modbus CRC16 algorithm is standard CRC-16/MODBUS (polynomial 0xA001, init 0xFFFF).

## Key Insight

The existing code already generates and parses complete Modbus RTU frames — it just transports them over TCP. Adding serial support means:

1. **Reuse the existing RTU frame building/parsing logic** from `modbus_client.c` (`build_read_frame`, `build_write_frame`, CRC16)
2. **Replace the TCP transport** with serial port I/O
3. **Handle Modbus RTU timing requirements** (inter-frame delays)

The `ModbusSerialClient` does NOT need its own `encodeReadRegister()` / `decodeReadResponse()` / `calculateCRC16()` — those already exist.

## Requirements

### Communication Modes

| Mode | Transport | Framing | CLI trigger |
|------|-----------|---------|-------------|
| **TCP** (existing) | TCP socket to Waveshare gateway | RTU frames over TCP | default / `-i` + `-p` |
| **Serial** (new) | Serial port (RS485) | RTU frames over serial | `--serial <device>` |
| **Demo** (existing) | In-memory simulation | N/A | `-d` / `--demo` |

### CLI Changes

```
windmi-control [CONNECTION] [OPTIONS]

Connection (mutually exclusive):
  -i, --ip <addr>         Modbus gateway IP (default: 192.168.123.10)
  -p, --port <port>       Modbus gateway TCP port (default: 8899)
      --serial <device>    Serial device path (e.g. /dev/ttyUSB0)

Serial options (only with --serial):
      --baud <rate>        Baud rate (default: 9600)
      --parity <type>      Parity: none, even, odd (default: none)

Other options:
  -w, --web <port>         Web server HTTP port (default: 8080, demo: 10000)
  -l, --log-level <lvl>    Log level (default: INFO)
  -o, --log-file <path>    Log to file
  -t, --static-dir <dir>   Static files directory
  -s, --selftest           Run self-test and exit
  -d, --demo               Demo mode with simulated device
  -f, --force              Force start (override stale lock)
  -h, --help               Show help
```

**Why not `-d` for `--serial`?** — `-d` is already used for `--demo`. Use long option `--serial` only.

**Why not `--device`?** — Avoid confusion with block device terminology. `--serial` is unambiguous and self-documenting.

**Why no `--data-bits` / `--stop-bits`?** — Modbus RTU is standardized on 8 data bits and 1 stop bit. Exposing these adds complexity with no practical benefit. If an exotic configuration is ever needed, it can be added later.

### Usage Examples

```bash
# TCP via Waveshare gateway (default, existing)
./windmi-control

# TCP with custom gateway
./windmi-control -i 192.168.1.100 -p 502

# Serial via USB-to-RS485 adapter (new)
./windmi-control --serial /dev/ttyUSB0
./windmi-control --serial /dev/ttyUSB0 --baud 19200
./windmi-control --serial /dev/ttyACM0 --parity even

# Demo mode (unchanged)
./windmi-control -d
```

## Implementation Plan

### Phase 1: C Serial Transport Layer
**Goal:** Add serial port I/O to the C Modbus client

The existing C client (`modbus_client.c`) is tightly coupled to TCP sockets. Rather than refactoring it (high risk), create a **parallel** C serial client that reuses the frame-building functions.

1. **Create `modbus_serial_client.h`** — C header for serial client
   - Location: `include/modbus_serial_client.h`
   - Same API shape as `modbus_client.h`:
     ```c
     typedef struct modbus_serial_client modbus_serial_client_t;

     modbus_serial_client_t *modbus_serial_client_create(
         const char *device, int baud, char parity, uint8_t slave_id);
     void modbus_serial_client_destroy(modbus_serial_client_t *client);
     bool modbus_serial_client_connect(modbus_serial_client_t *client);
     void modbus_serial_client_disconnect(modbus_serial_client_t *client);
     bool modbus_serial_client_is_connected(modbus_serial_client_t *client);
     void modbus_serial_client_flush_buffer(modbus_serial_client_t *client);
     int modbus_serial_read_register(modbus_serial_client_t *client, uint16_t address, int16_t *value);
     int modbus_serial_write_register(modbus_serial_client_t *client, uint16_t address, uint16_t value);
     ```

2. **Create `modbus_serial_client.c`** — C implementation
   - Location: `src/modbus_serial_client.c`
   - **Reuse** `build_read_frame()`, `build_write_frame()`, CRC16 from existing code (make them non-static or move to shared header)
   - Serial transport:
     - `open(device, O_RDWR | O_NOCTTY)` — blocking mode (NOT `O_NONBLOCK`)
     - Configure termios: baud rate, 8N1 (or 8E1/8O1), raw mode via `cfmakeraw()`
     - Read timeout: `VTIME = 1` (100ms inter-character), `VMIN = 0`
     - Inter-frame delay: `tcdrain()` after each write, then wait ≥ 3.5 character times before sending next frame
   - Send/receive: `write()` / `read()` with `select()` + timeout (same pattern as socket version)
   - Frame parsing: identical to TCP version (same CRC verification, same header-then-data protocol)

3. **Refactor shared frame functions** — Currently `build_read_frame()` and `build_write_frame()` are `static` in `modbus_client.c`
   - Option A: Make them `STATIC_FOR_TEST` (already exists as a pattern) and declare in a shared header
   - Option B: Extract into `modbus_rtu_frame.c` / `modbus_rtu_frame.h`
   - **Recommendation: Option A** — minimal change, consistent with existing test pattern

### Phase 2: C++ ModbusSerialClient Wrapper
**Goal:** Create C++ wrapper implementing IModbusClient

1. **Create `include/modbus/ModbusSerialClient.hpp`**
   ```cpp
   class ModbusSerialClient : public IModbusClient {
   public:
       ModbusSerialClient(const std::string& device, int baud,
                          char parity, uint8_t slave_id);
       ~ModbusSerialClient();

       // IModbusClient interface
       bool connect() override;
       void disconnect() override;
       bool isConnected() const override;
       int16_t readRegister(uint16_t address) override;
       void writeRegister(uint16_t address, uint16_t value) override;
       void flushBuffer() override;
       std::string getLastError() const override;

       void* getCClient() const;  // For selftest compatibility

   private:
       struct Impl;
       std::unique_ptr<Impl> impl_;
   };
   ```

2. **Create `src/modbus/ModbusSerialClient.cpp`**
   - Same pimpl pattern as `ModbusClient`
   - Wraps `modbus_serial_client_t` C struct
   - Translates C return codes to `ModbusException` throws

3. **Update `src/modbus/CMakeLists.txt`**
   - Add `ModbusSerialClient.cpp` and `${CMAKE_CURRENT_SOURCE_DIR}/../../src/modbus_serial_client.c` to `windmi_modbus` library

### Phase 3: Main Application Changes
**Goal:** Integrate serial support into CLI and runtime

1. **Update CLI parsing** (`src/main.cpp`)
   - Add `--serial <device>`, `--baud <rate>`, `--parity <type>` options
   - Validation rules:
     - `--serial` is mutually exclusive with `-i`/`-p`
     - `--baud` and `--parity` require `--serial`
     - Valid baud rates: 9600, 19200, 38400, 57600, 115200
     - Valid parity values: `none`, `even`, `odd`
   - Default baud: 9600 (Windmi standard)
   - Default parity: `none`

2. **Update Modbus client instantiation** (`src/main.cpp`)
   - After CLI parsing, create the appropriate client:
     ```cpp
     std::unique_ptr<windmi::IModbusClient> modbus_client;
     if (demo_mode) {
         modbus_client = std::make_unique<windmi::SimulatedModbusClient>();
     } else if (!serial_device.empty()) {
         modbus_client = std::make_unique<windmi::ModbusSerialClient>(
             serial_device, baud_rate, parity, MODBUS_SLAVE_ID);
     } else {
         modbus_client = std::make_unique<windmi::ModbusClient>(
             modbus_ip, modbus_port, MODBUS_SLAVE_ID);
     }
     ```

3. **Update shutdown client** (`src/main.cpp`)
   - Current code creates a separate `ModbusClient(modbus_ip, modbus_port, ...)` for writing OFF mode
   - Must also create a `ModbusSerialClient` equivalent when in serial mode:
     ```cpp
     if (!serial_device.empty()) {
         windmi::ModbusSerialClient shutdown_client(serial_device, baud_rate, parity, MODBUS_SLAVE_ID);
         // same retry logic, using shutdown_client instead
     } else {
         windmi::ModbusClient shutdown_client(modbus_ip, modbus_port, MODBUS_SLAVE_ID);
         // existing code
     }
     ```

4. **Update selftest** (`src/main.cpp`)
   - Self-test currently uses `modbus_client_t*` (C struct) via `getCClient()`
   - Add `ModbusSerialClient::getCClient()` returning `modbus_serial_client_t*`
   - Create a C adapter: `selftest_run_serial(modbus_serial_client_t* client)` OR
     refactored `selftest_run()` that accepts a generic interface
   - **Simplest approach:** Add `selftest_run_serial()` that wraps the existing `selftest_run()` logic with the serial C client

5. **Update logging**
   - Add log message: `"Serial device: /dev/ttyUSB0 @ 9600 baud, 8N1"`
   - Add serial-specific error messages:
     - `"Failed to open serial device /dev/ttyUSB0: Permission denied (are you in the 'dialout' group?)"`
     - `"Serial device /dev/ttyUSB0: No such file or directory"`
     - `"Invalid baud rate: 12345"`

### Phase 4: Serial Port Implementation Details
**Goal:** Correct Modbus RTU serial communication

1. **Termios Configuration**
   ```c
   struct termios tty;
   tcgetattr(fd, &tty);
   cfmakeraw(&tty);

   // Baud rate
   cfsetispeed(&tty, B9600);   // or B19200, B38400, B57600, B115200
   cfsetspeed(&tty, B9600);

   // 8 data bits (standard for Modbus RTU)
   tty.c_cflag &= ~CSIZE;
   tty.c_cflag |= CS8;

   // Parity
   tty.c_cflag &= ~(PARENB | PARODD);
   if (parity == 'E') {
       tty.c_cflag |= PARENB;
   } else if (parity == 'O') {
       tty.c_cflag |= PARENB | PARODD;
   }

   // 1 stop bit (standard for Modbus RTU)
   tty.c_cflag &= ~CSTOPB;

   // Enable receiver, ignore modem control lines
   tty.c_cflag |= CLOCAL | CREAD;

   // Read timeout: VTIME=2 (200ms), VMIN=0
   // 200ms is generous; 3.5 char times at 9600 baud = ~4ms
   tty.c_cc[VTIME] = 2;
   tty.c_cc[VMIN] = 0;

   tcsetattr(fd, TCSANOW, &tty);
   tcflush(fd, TCIOFLUSH);  // Clear any stale data
   ```

2. **Inter-Frame Delay (3.5 Character Times)**
   - At 9600 baud: 1 char = 11 bits (start + 8 data + parity? + stop) ÷ 9600 = ~1.15ms → 3.5 chars = ~4ms
   - At 19200 baud: ~2ms
   - At 115200 baud: ~0.3ms
   - Implementation: after `tcdrain()`, `usleep(inter_frame_delay_us)` before sending next frame
   - Minimum: 4ms regardless of baud rate (conservative, per Modbus spec)

3. **RS485 Half-Duplex Considerations**
   - Most USB-to-RS485 adapters handle direction control automatically via RTS
   - Linux provides `TIOCSRS485` ioctl for RS485 transceiver control:
     ```c
     struct serial_rs485 rs485;
     rs485.flags = SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND;
     ioctl(fd, TIOCSRS485, &rs485);
     ```
   - **Not all adapters support `TIOCSRS485`** — FTDI and CH340 typically handle this in hardware
   - Default: try `TIOCSRS485`, ignore failure (hardware auto-direction is common)
   - Add `--rs485` CLI flag to explicitly enable kernel RS485 mode if needed

4. **Error Recovery**
   - `EINTR` from `read()`/`write()`: retry the operation
   - Serial port disappears (`/dev/ttyUSB0` unplugged): `read()`/`write()` returns -1 with specific errno
     → mark `connected = false`, log error, let ControlLoop reconnect on next cycle
   - Garbage data on bus: CRC mismatch → discard frame, retry
   - Timeout: same as TCP — return error, let caller handle

### Phase 5: Build System Changes

1. **Update `CMakeLists.txt`**
   - Add `src/modbus_serial_client.c` to `windmi_modbus` library
   - Add `ModbusSerialClient.cpp` to `windmi_modbus` library
   - No new external dependencies (termios is part of glibc)

2. **Update `include/modbus/CMakeLists.txt`** (if exists)
   - Add `ModbusSerialClient.hpp` to install headers

3. **Build test:** ensure both `-DWINDMI_BUILD_TESTS=ON` and `OFF` compile cleanly

### Phase 6: Testing

#### 6.1 Unit Tests (test_modbus_serial.cpp)

Location: `tests/modbus/test_modbus_serial.cpp`

**Note:** CRC16 tests already exist in `tests/utils/test_crc16.cpp` and RTU frame building is already tested indirectly via `tests/modbus/test_modbus_client.cpp`. Do NOT duplicate CRC16 or frame-building tests. Focus on serial-specific concerns.

```cpp
// Serial Port Configuration Tests
TEST(ModbusSerialClientTest, ConnectInvalidDevice) {
    ModbusSerialClient client("/dev/nonexistent", 9600, 'N', 1);
    EXPECT_FALSE(client.connect());
}

TEST(ModbusSerialClientTest, ConnectPermissionDenied) {
    // /dev/ttyS0 typically requires dialout group
    ModbusSerialClient client("/dev/ttyS0", 9600, 'N', 1);
    // May fail with permission denied or may succeed if user has access
    // Just verify it doesn't crash
}

TEST(ModbusSerialClientTest, InvalidBaudRate) {
    // Constructor should reject or connect() should fail
    ModbusSerialClient client("/dev/ttyUSB0", 12345, 'N', 1);
    EXPECT_FALSE(client.connect());
}

TEST(ModbusSerialClientTest, CreateAndDestroy) {
    ModbusSerialClient client("/dev/ttyUSB0", 9600, 'N', 1);
    // Should not crash, not connected
    EXPECT_FALSE(client.isConnected());
}

// Termios Configuration Verification
TEST(ModbusSerialClientTest, ValidParityValues) {
    // Verify none/even/odd parity strings are accepted
    ModbusSerialClient c1("/dev/ttyUSB0", 9600, 'N', 1);
    ModbusSerialClient c2("/dev/ttyUSB0", 9600, 'E', 1);
    ModbusSerialClient c3("/dev/ttyUSB0", 9600, 'O', 1);
}

TEST(ModbusSerialClientTest, InvalidParityValue) {
    // Invalid parity should throw or fail at construction
    EXPECT_THROW(ModbusSerialClient("/dev/ttyUSB0", 9600, 'X', 1),
                 ModbusException);
}

// Baud Rate Validation
TEST(ModbusSerialClientTest, Baud9600)  { /* valid */ }
TEST(ModbusSerialClientTest, Baud19200) { /* valid */ }
TEST(ModbusSerialClientTest, Baud38400) { /* valid */ }
TEST(ModbusSerialClientTest, Baud57600) { /* valid */ }
TEST(ModbusSerialClientTest, Baud115200){ /* valid */ }
TEST(ModbusSerialClientTest, Baud1200)  { /* invalid - throw */ }
TEST(ModbusSerialClientTest, Baud99999) { /* invalid - throw */ }

// C Client API Parity (verify C wrapper matches C API)
TEST(ModbusSerialClientTest, CClientCreateDestroy) {
    modbus_serial_client_t *c = modbus_serial_client_create(
        "/dev/ttyUSB0", 9600, 'N', 1);
    ASSERT_NE(c, nullptr);
    modbus_serial_client_destroy(c);
}

TEST(ModbusSerialClientTest, CClientIsNotConnected) {
    modbus_serial_client_t *c = modbus_serial_client_create(
        "/dev/ttyUSB0", 9600, 'N', 1);
    EXPECT_FALSE(modbus_serial_client_is_connected(c));
    modbus_serial_client_destroy(c);
}
```

#### 6.2 Integration Testing with socat + pymodbus

The socat loopback by itself doesn't work — there's no Modbus slave on the other end to respond. A proper integration test requires a **mock Modbus RTU slave**.

```bash
# Step 1: Create virtual serial port pair
socat -d -d pty,raw,echo=0,link=/tmp/ttyV0 pty,raw,echo=0,link=/tmp/ttyV1 &

# Step 2: Start a mock Modbus RTU slave on /tmp/ttyV1
python3 -m pip install pymodbus
python3 << 'EOF' &
from pymodbus.server import StartSerialServer
from pymodbus.datastore import ModbusSlaveContext, ModbusServerIdentifiers
# Create a slave context with some register values
store = ModbusSlaveContext(zero_mode=True)
# Set some registers to match Windmi expectations
store.setValues(3, 0x1006, [8])      # Device type = 8
store.setValues(3, 0x0001, [120])    # Outdoor temp = 12.0°C
StartSerialServer(store, port='/tmp/ttyV1', baudrate=9600)
EOF

# Step 3: Run our controller against /tmp/ttyV0
./build/windmi-control --serial /tmp/ttyV0 --baud 9600 -w 10000

# Step 4: Test API
curl http://localhost:10000/api/status
curl -X POST http://localhost:10000/api/set-mode -d '{"mode": 2}'
```

#### 6.3 Test Scenarios

| # | Scenario | Expected Result |
|---|----------|-----------------|
| 1 | Valid serial connection | Connects, reads registers successfully |
| 2 | Device path does not exist | Clear error: "Serial device not found" |
| 3 | Permission denied | Error with hint: "add user to 'dialout' group" |
| 4 | Invalid baud rate | Rejected at CLI parse / construction |
| 5 | Invalid parity | Rejected at CLI parse / construction |
| 6 | Response timeout | `ModbusException` thrown, ControlLoop retries |
| 7 | CRC mismatch on response | Discard frame, log error, retry |
| 8 | Serial device unplugged at runtime | `connected = false`, ControlLoop reconnect on next cycle |
| 9 | Multiple sequential requests | Each gets proper response |
| 10 | Inter-frame timing | ≥4ms gap between consecutive frames |
| 11 | Selftest via serial | Same register verification as TCP |
| 12 | Shutdown OFF write via serial | OFF mode written before exit |
| 13 | `--serial` + `-i` specified | Error: mutually exclusive options |
| 14 | `--baud` without `--serial` | Error: `--baud` requires `--serial` |
| 15 | `--serial` with `--demo` | Error: mutually exclusive options |
| 16 | RS485 adapter auto-direction | Works without `TIOCSRS485` |

#### 6.4 Hardware Test Matrix

| Adapter | Chipset | Notes |
|---------|---------|-------|
| FTDI USB-RS485 | FT232RL | Auto direction via RTS, most common |
| Waveshare USB-RS485 | CH340 | Cheap, auto direction |
| Startech ICUSBRS485 | CP210x | Supports `TIOCSRS485` |
| Generic USB-Serial | PL2303 | May need `--rs485` flag |

## Files to Create/Modify

### New Files
| File | Description |
|------|-------------|
| `include/modbus_serial_client.h` | C serial client header (mirrors `modbus_client.h`) |
| `src/modbus_serial_client.c` | C serial client implementation |
| `include/modbus/ModbusSerialClient.hpp` | C++ wrapper header |
| `src/modbus/ModbusSerialClient.cpp` | C++ wrapper implementation |
| `tests/modbus/test_modbus_serial.cpp` | Unit tests for serial client |

### Modified Files
| File | Changes |
|------|---------|
| `include/config.h` | Add `SERIAL_DEFAULT_BAUD 9600`, `SERIAL_DEFAULT_PARITY 'N'`, `SERIAL_INTER_FRAME_DELAY_MS 4`, `MODBUS_SERIAL_TIMEOUT_MS 2000` |
| `src/main.cpp` | Add `--serial`, `--baud`, `--parity` CLI parsing; update client creation; update shutdown client; update selftest |
| `src/modbus/CMakeLists.txt` | Add new source files to `windmi_modbus` |
| `tests/modbus/CMakeLists.txt` | Add `test_modbus_serial` |
| `CMakeLists.txt` | (no change needed if subdirectory CMakeLists are updated) |
| `README.md` | Add "Serial Port Connection" section |

### Potentially Modified (for shared frame functions)
| File | Changes |
|------|---------|
| `src/modbus_client.c` | Make `build_read_frame` / `build_write_frame` non-static (or `STATIC_FOR_TEST`) for reuse |
| `include/modbus_rtu_frame.h` | New shared header for frame functions (if extracted) |

## Design Decisions

1. **New C client vs. extending existing one**
   - Decision: Create separate `modbus_serial_client.c`
   - Reason: The existing `modbus_client_t` struct has `ip`/`port`/`socket_fd` fields that don't apply to serial. Extending it would require union types or void pointers, making the code harder to read and more error-prone.

2. **Reuse frame functions**
   - Decision: Make `build_read_frame()` / `build_write_frame()` accessible from both clients
   - Reason: DRY — the RTU frame format is identical for TCP-transparent and serial. No point duplicating this logic.

3. **`IModbusClient` interface unchanged**
   - Decision: `ModbusSerialClient` implements the existing `IModbusClient` interface as-is
   - Reason: `ControlLoop` already depends on `IModbusClient`, not concrete types. No changes needed in `ControlLoop.cpp` or `WebServer.cpp`.

4. **Constructor takes connection parameters (not `connect()`)**
   - Consistent with existing `ModbusClient(host, port, slave_id)` pattern. The `connect()` method uses stored parameters.

5. **Blocking I/O with `select()` timeout**
   - Decision: Use blocking `open()` with `select()` for timeouts (same pattern as TCP client)
   - Reason: Simpler than non-blocking I/O with poll. `O_NONBLOCK` adds complexity with no benefit for a single-device point-to-point serial link.

6. **RS485 direction: auto-detect, with manual override**
   - Most USB-RS485 adapters handle direction automatically. Try `TIOCSRS485`, ignore failure. Add `--rs485` flag for adapters that need explicit kernel RS485 support.

7. **No UUCP lock files**
   - The application already uses `flock()` on `/tmp/windmi-controller.lock` for single-instance protection. Adding UUCP lock files (`/var/lock/LCK..ttyUSB0`) would be redundant for a single-instance daemon. If multi-process serial sharing is ever needed, this can be added later.

8. **Serial port permissions**
   - Document that the user must be in the `dialout` group: `sudo usermod -aG dialout $USER`
   - Provide a helpful error message on `EACCES` with the fix

## Out of Scope (Future Work)

These items are intentionally excluded from this plan:

- **Modbus ASCII mode** — RTU is the standard for RS485; ASCII is rarely used
- **Multi-drop bus (multiple slaves)** — Windmi uses a single slave; multi-drop adds addressing complexity
- **Serial port hotplug monitoring (udev)** — The reconnect logic in ControlLoop handles this; no need for inotify/udev
- **Configurable data bits / stop bits** — Modbus RTU is always 8 data bits; 1 stop bit (with parity) or 2 stop bits (without parity). Not worth CLI options.
- **libmodbus dependency** — Writing our own client keeps dependencies minimal and matches the existing architecture

## References

- Modbus over Serial Line Specification and Implementation Guide V1.02 (modbus.org)
- Linux `termios(3)` man page
- Linux Serial HOWTO: https://tldp.org/HOWTO/Serial-HOWTO
- `TIOCSRS485` ioctl: Linux kernel documentation (`Documentation/driver-api/serial/serial-rs485.rst`)
- Windmi Modbus register map (see `include/config.h`)