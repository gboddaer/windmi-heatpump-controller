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
   - Write-then-verify: `modbus_write_register()` writes, then reads back the register and compares

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
   - `-d, --demo` — demo mode (**`-d` is already taken — cannot use for `--serial`**)
   - `-w, --web <port>` — web server port
   - `-l, --log-level <lvl>` — log level
   - `-s, --selftest` — run self-test and exit
   - `-f, --force` — force start

5. **Shutdown** (`src/main.cpp`) — writes OFF mode via a **separate** `ModbusClient` instance created with the same host/port. Uses 3-attempt retry loop.

6. **Self-test** (`src/selftest.c`) — uses `modbus_client_t*` (C struct) directly, not the C++ interface. Calls `modbus_read_register()` and `modbus_write_register()` directly.

7. **Reconnect** (`src/core/ControlLoop.cpp`) — on connection loss, calls `modbus_client_->connect()` (via `IModbusClient` pointer) with up to `MODBUS_MAX_RETRIES` attempts at `MODBUS_RECONNECT_INTERVAL_S` intervals. For the serial client, `connect()` must close the stale fd and re-open/re-configure the serial port.

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
      --stop-bits <bits>    Stop bits: 1 or 2 (default: 1; Windmi default is 8N1)
      --rs485               Enable kernel RS485 transceiver mode (TIOCSRS485)

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

**Why no `--data-bits`?** — Windmi serial communication uses 8 data bits. `--stop-bits` is exposed because the Windmi default is confirmed as **9600 8N1**, while strict Modbus RTU no-parity setups are often 8N2.

### Usage Examples

```bash
# TCP via Waveshare gateway (default, existing)
./windmi-control

# TCP with custom gateway
./windmi-control -i 192.168.1.100 -p 502

# Serial via USB-to-RS485 adapter (new)
./windmi-control --serial /dev/ttyUSB0
./windmi-control --serial /dev/ttyUSB0 --baud 19200
./windmi-control --serial /dev/ttyACM0 --parity even --stop-bits 1
./windmi-control --serial /dev/ttyUSB0 --rs485  # adapter needs kernel RS485

# Demo mode (unchanged)
./windmi-control -d
```

## Implementation Plan

### Phase 1: Extract and Share RTU Frame Functions
**Goal:** Make frame-building functions reusable by both TCP and serial clients

Currently `build_read_frame()`, `build_write_frame()` are `static` in `modbus_client.c`. The serial client needs them too.

1. **Create `include/modbus_rtu_frame.h`** — shared header for RTU frame functions
   ```c
   #ifndef MODBUS_RTU_FRAME_H
   #define MODBUS_RTU_FRAME_H

   #include <stdint.h>
   #include <stddef.h>

   // Build a Modbus RTU Read Holding Registers frame (function code 0x03).
   // Frame buffer must be at least 8 bytes.
   // Returns frame length including CRC (always 8).
   size_t modbus_rtu_build_read_frame(uint8_t *frame, uint8_t slave_id,
                                       uint16_t address, uint16_t count);

   // Build a Modbus RTU Write Single Register frame (function code 0x06).
   // Frame buffer must be at least 8 bytes.
   // Returns frame length including CRC (always 8).
   size_t modbus_rtu_build_write_frame(uint8_t *frame, uint8_t slave_id,
                                        uint16_t address, uint16_t value);

   #endif
   ```

2. **Create `src/modbus_rtu_frame.c`** — implementation (moved from `modbus_client.c`)
   - Move the body of `build_read_frame()` and `build_write_frame()` here
   - Rename to `modbus_rtu_build_read_frame()` and `modbus_rtu_build_write_frame()`
   - These call `crc16()` from `crc16.h` (already available)

3. **Update `src/modbus_client.c`**
   - Remove the `static` `build_read_frame()` / `build_write_frame()` functions
   - `#include "modbus_rtu_frame.h"` and call the new names
   - All existing behavior unchanged — just calls renamed functions

4. **Update `CMakeLists.txt`** or `src/CMakeLists.txt` (wherever `modbus_client.c` is built)
   - Add `src/modbus_rtu_frame.c` to the build
   - Both `windmi_modbus` library and any test targets that reference the old static functions need updating

5. **Update existing tests** (`tests/modbus/test_modbus_client.cpp`)
   - If any test directly calls `build_read_frame` / `build_write_frame` (via `STATIC_FOR_TEST`), update to new names

### Phase 2: C Serial Transport Layer
**Goal:** Add serial port I/O as a parallel C client

1. **Create `include/modbus_serial_client.h`** — C header for serial client
   - Location: `include/modbus_serial_client.h` (same directory as `modbus_client.h`)
   - Mirrors the `modbus_client.h` API shape:
     ```c
     #include <stdint.h>
     #include <stdbool.h>

     typedef struct modbus_serial_client modbus_serial_client_t;

     modbus_serial_client_t *modbus_serial_client_create(
         const char *device, int baud, char parity, int stop_bits,
         bool rs485_enabled, uint8_t slave_id);
     void modbus_serial_client_destroy(modbus_serial_client_t *client);
     bool modbus_serial_client_connect(modbus_serial_client_t *client);
     void modbus_serial_client_disconnect(modbus_serial_client_t *client);
     bool modbus_serial_client_is_connected(modbus_serial_client_t *client);
     void modbus_serial_client_flush_buffer(modbus_serial_client_t *client);
     int modbus_serial_read_register(modbus_serial_client_t *client,
                                      uint16_t address, int16_t *value);
     int modbus_serial_write_register(modbus_serial_client_t *client,
                                       uint16_t address, uint16_t value);
     int modbus_serial_read_registers(modbus_serial_client_t *client,
                                       uint16_t address, int16_t *values,
                                       uint16_t count);
     ```
   - Note: includes `modbus_serial_read_registers()` (plural) for feature parity with TCP client

2. **Create `src/modbus_serial_client.c`** — C implementation
   - Location: `src/modbus_serial_client.c`
   - Calls `modbus_rtu_build_read_frame()` / `modbus_rtu_build_write_frame()` from Phase 1
   - Calls `crc16()` from `crc16.h` for CRC verification
   - Serial transport (detailed in Phase 4):
     - `open(device, O_RDWR | O_NOCTTY)` — blocking mode
     - Configure termios: baud, parity, stop bits, raw mode
     - Apply `TIOCSRS485` only when `rs485_enabled == true`
     - `write()` / `read()` with `select()` + timeout
     - `tcdrain()` after write + inter-frame delay before next frame
     - `tcflush()` for buffer flush instead of the socket `select()`+`recv()` approach
   - Frame parsing: identical to TCP version (same header-then-data protocol, same CRC verification)
   - Write-then-verify: `modbus_serial_write_register()` must follow the same write → read-back → compare pattern as the TCP client
   - Reconnect: `modbus_serial_client_connect()` must close stale fd (if any) and re-open/re-configure the serial port, so ControlLoop's existing reconnect loop works

### Phase 3: C++ ModbusSerialClient Wrapper
**Goal:** Create C++ wrapper implementing IModbusClient

1. **Create `include/modbus/ModbusSerialClient.hpp`**
   ```cpp
   namespace windmi {

   class ModbusSerialClient : public IModbusClient {
   public:
       ModbusSerialClient(const std::string& device, int baud,
                          char parity, int stop_bits,
                          bool rs485_enabled, uint8_t slave_id);
       ~ModbusSerialClient();

       // IModbusClient interface
       bool connect() override;
       void disconnect() override;
       bool isConnected() const override;
       int16_t readRegister(uint16_t address) override;
       void writeRegister(uint16_t address, uint16_t value) override;
       void flushBuffer() override;
       std::string getLastError() const override;

   private:
       struct Impl;
       std::unique_ptr<Impl> impl_;
   };

   } // namespace windmi
   ```
   - **No `getCClient()` method** — selftest will be refactored to use `IModbusClient` (see Phase 5)
   - Constructor stores connection parameters (consistent with `ModbusClient` pattern)
   - `connect()` uses stored parameters

2. **Create `src/modbus/ModbusSerialClient.cpp`**
   - Same pimpl pattern as `ModbusClient`
   - Wraps `modbus_serial_client_t` C struct
   - Translates C return codes to `ModbusException` throws

3. **Update `src/modbus/CMakeLists.txt`**
   - Add `ModbusSerialClient.cpp` to `windmi_modbus` sources
   - Add `${CMAKE_CURRENT_SOURCE_DIR}/../../src/modbus_serial_client.c` to `windmi_modbus` sources
   - Add `${CMAKE_CURRENT_SOURCE_DIR}/../../src/modbus_rtu_frame.c` to `windmi_modbus` sources (if not already there from Phase 1)

### Phase 4: Serial Port Implementation Details
**Goal:** Correct Modbus RTU serial communication

1. **Termios Configuration — Windmi Default 9600 8N1**

   The Windmi default serial configuration is confirmed as **9600 8N1**:
   - 9600 baud
   - 8 data bits
   - no parity
   - 1 stop bit

   This differs from strict Modbus RTU's common no-parity recommendation of 8N2, but 8N1 is the required default for this device. The plan therefore exposes `--stop-bits` so users can select 8N2 if needed for other compatible devices.

   **Stop bits are explicit, not inferred from parity:**
   ```c
   struct termios tty;
   tcgetattr(fd, &tty);
   cfmakeraw(&tty);

   // Baud rate
   cfsetispeed(&tty, B9600);
   cfsetospeed(&tty, B9600);

   // 8 data bits (always, per Modbus RTU)
   tty.c_cflag &= ~CSIZE;
   tty.c_cflag |= CS8;

   // Parity
   tty.c_cflag &= ~(PARENB | PARODD);
   if (parity == 'E') {
       tty.c_cflag |= PARENB;            // Even parity
   } else if (parity == 'O') {
       tty.c_cflag |= PARENB | PARODD;   // Odd parity
   }

   // Stop bits: Windmi default is 1 stop bit (8N1)
   tty.c_cflag &= ~CSTOPB;
   if (stop_bits == 2) {
       tty.c_cflag |= CSTOPB;
   }

   // Enable receiver, ignore modem control lines
   tty.c_cflag |= CLOCAL | CREAD;

   // Read timeout: VTIME=2 (200ms inter-character timeout), VMIN=0
   // 200ms is generous; actual inter-character timeout at 9600 baud
   // would be ~1ms, but VTIME resolution is 100ms minimum
   tty.c_cc[VTIME] = 2;
   tty.c_cc[VMIN] = 0;

   tcsetattr(fd, TCSANOW, &tty);
   tcflush(fd, TCIOFLUSH);  // Clear any stale data
   ```

2. **Baud Rate Mapping**

   The C `termios` API uses symbolic constants (`B9600`, `B19200`, etc.), not raw integers. The client must map the integer baud rate to the correct constant:
   ```c
   static speed_t baud_to_constant(int baud) {
       switch (baud) {
           case 9600:   return B9600;
           case 19200:  return B19200;
           case 38400:  return B38400;
           case 57600:  return B57600;
           case 115200: return B115200;
           default:     return B0;  // invalid
       }
   }
   ```
   If `B0` is returned, `modbus_serial_client_create()` returns `NULL` (invalid baud rate).

3. **Inter-Frame Delay (3.5 Character Times)**

   Per Modbus spec, a silent period of ≥3.5 character times is required between frames:
   - At 9600 baud: 1 char = 11 bits ÷ 9600 = ~1.15ms → 3.5 chars = ~4ms
   - At 19200 baud: ~2ms
   - At 115200 baud: ~0.3ms
   - Minimum: 4ms regardless of baud rate (conservative)

   Implementation:
   ```c
   // After writing a frame:
   tcdrain(fd);  // Wait for all output to be transmitted
   // Before sending the next request frame:
   usleep(inter_frame_delay_us);  // ≥ 4000us (4ms)
   ```
   Note: do **not** delay between writing a request and reading its response. The silent interval is required before sending the next request frame. `tcdrain()` ensures the request has been physically transmitted before the response read begins.

4. **RS485 Half-Duplex Considerations**

   Most USB-to-RS485 adapters handle direction control automatically via hardware RTS toggling. For others, Linux provides `TIOCSRS485`:
   ```c
   struct serial_rs485 rs485;
   memset(&rs485, 0, sizeof(rs485));
   rs485.flags = SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND;
   if (ioctl(fd, TIOCSRS485, &rs485) < 0) {
       // Not all adapters support this — ignore failure
       // Hardware auto-direction is common (FTDI, CH340)
   }
   ```
   CLI flag `--rs485` explicitly enables this. Without the flag, the ioctl is not called at all (hardware auto-direction is the default assumption).

5. **Buffer Flush**

   The TCP client flushes stale data using `select()` + `recv(MSG_DONTWAIT)` in a loop. For serial, use `tcflush()` which is simpler and more reliable:
   ```c
   void modbus_serial_client_flush_buffer(modbus_serial_client_t *client) {
       if (client && client->connected && client->fd >= 0) {
           tcflush(client->fd, TCIFLUSH);  // Flush received data only
       }
   }
   ```

6. **Send and Receive**

   Same `select()` + timeout pattern as TCP, but using `write()`/`read()` instead of `send()`/`recv()`:

   ```c
   // Send a complete RTU frame, handling EINTR and partial writes
   static int serial_send_frame(modbus_serial_client_t *client,
                                 const uint8_t *frame, size_t len) {
       tcflush(client->fd, TCIFLUSH);  // Flush stale input before sending

       size_t total = 0;
       while (total < len) {
           ssize_t sent = write(client->fd, frame + total, len - total);
           if (sent < 0) {
               if (errno == EINTR) continue;
               return -1;
           }
           if (sent == 0) return -1;
           total += (size_t)sent;
       }

       if (tcdrain(client->fd) != 0) return -1;
       return 0;
   }

   // Receive exactly N bytes with timeout (same logic as TCP receive_exact)
   static int serial_receive_exact(modbus_serial_client_t *client,
                                    uint8_t *buffer, size_t expected_len) {
       size_t total = 0;
       while (total < expected_len) {
           fd_set fds;
           struct timeval tv;
           FD_ZERO(&fds);
           FD_SET(client->fd, &fds);
           tv.tv_sec = MODBUS_SERIAL_TIMEOUT_MS / 1000;
           tv.tv_usec = (MODBUS_SERIAL_TIMEOUT_MS % 1000) * 1000;

           int ready = select(client->fd + 1, &fds, NULL, NULL, &tv);
           if (ready <= 0) return -1;  // Timeout or error

           ssize_t received = read(client->fd, buffer + total,
                                    expected_len - total);
           if (received < 0) {
               if (errno == EINTR) continue;
               return -1;
           }
           if (received == 0) return -1;

           total += (size_t)received;
       }
       return 0;
   }
   ```

7. **Write-Then-Verify**

   The existing TCP client's `modbus_write_register()` writes a value, then reads it back and compares. The serial client must follow the same pattern:
   ```c
   int modbus_serial_write_register(modbus_serial_client_t *client,
                                     uint16_t address, uint16_t value) {
       // Same retry structure as TCP version:
       // 1. Build write frame
       // 2. Send via serial_send_frame()
       // 3. Receive echo/response via serial_receive_exact()
       // 4. Verify CRC and response content
       // 5. Read back register via modbus_serial_read_register()
       // 6. Compare read value with written value
       // 7. Retry up to MODBUS_WRITE_MAX_RETRIES on failure
   }
   ```

8. **Error Recovery**

   - `EINTR` from `read()`/`write()`: retry the operation
   - Serial port disappears (USB unplugged): `read()`/`write()` returns -1 → mark `connected = false`, ControlLoop reconnects on next cycle by calling `connect()` which re-opens and re-configures the port
   - Garbage data on bus: CRC mismatch → discard frame, retry
   - Timeout: same as TCP — return error, let caller handle
   - **Device re-enumeration**: if `/dev/ttyUSB0` disappears and reappears (USB replug), `connect()` will re-open it. If the device gets a different node name (`/dev/ttyUSB1`), the user must restart with the correct path. This is documented in README.

9. **Reconnection**

   The ControlLoop already handles reconnection by calling `modbus_client_->connect()` (the `IModbusClient` pointer). For the serial client, `connect()` must:
   1. If already open, close the existing fd
   2. Re-open the serial device path
   3. Re-configure termios
   4. Set `connected = true`
   
   This way, the existing ControlLoop reconnect logic works without changes.

### Phase 5: Main Application Changes
**Goal:** Integrate serial support into CLI and runtime

1. **Update CLI parsing** (`src/main.cpp`)
   - Add variables:
     ```cpp
     std::string serial_device;   // empty = no serial
     int serial_baud = SERIAL_DEFAULT_BAUD;
     char serial_parity = SERIAL_DEFAULT_PARITY;
     int serial_stop_bits = SERIAL_DEFAULT_STOP_BITS;
     bool serial_rs485 = false;

     bool ip_specified = false;
     bool port_specified = false;
     ```
   - Add `--serial <device>`, `--baud <rate>`, `--parity <type>`, `--stop-bits <bits>`, `--rs485` option parsing
   - Track `ip_specified` and `port_specified`; defaults alone must not make TCP and serial look mutually exclusive.
   - Validation rules:
     - `--serial` is mutually exclusive with explicitly provided `-i`/`--ip` or `-p`/`--port`, and with `-d`/`--demo`
     - `--baud`, `--parity`, `--stop-bits`, `--rs485` require `--serial`
     - Valid baud rates: 9600, 19200, 38400, 57600, 115200
     - Valid parity values: `none`, `even`, `odd`
     - Valid stop bits: `1`, `2`
     - Invalid baud, parity, or stop bits → print error and exit
   - Print an error and exit on mutual exclusion violations

2. **Update Modbus client instantiation** (`src/main.cpp`)
   - After CLI parsing, create the appropriate client:
     ```cpp
     std::unique_ptr<windmi::IModbusClient> modbus_client;
     if (demo_mode) {
         modbus_client = std::make_unique<windmi::SimulatedModbusClient>();
     } else if (!serial_device.empty()) {
         modbus_client = std::make_unique<windmi::ModbusSerialClient>(
             serial_device, serial_baud, serial_parity, serial_stop_bits,
             serial_rs485, MODBUS_SLAVE_ID);
     } else {
         modbus_client = std::make_unique<windmi::ModbusClient>(
             modbus_ip, modbus_port, MODBUS_SLAVE_ID);
     }
     ```

3. **Update shutdown client** (`src/main.cpp`)
   - Current code creates a separate `ModbusClient(modbus_ip, modbus_port, ...)` for writing OFF mode
   - Must create the matching client type for serial mode:
     ```cpp
     if (!serial_device.empty()) {
         windmi::ModbusSerialClient shutdown_client(
             serial_device, serial_baud, serial_parity, serial_stop_bits,
             serial_rs485, MODBUS_SLAVE_ID);
         // same retry logic using shutdown_client
     } else {
         windmi::ModbusClient shutdown_client(modbus_ip, modbus_port, MODBUS_SLAVE_ID);
         // existing code
     }
     ```

4. **Refactor selftest to use `IModbusClient`** (`src/selftest.c` → `src/selftest.cpp`, move header to `include/selftest.hpp`)

   The current selftest calls `modbus_read_register()` / `modbus_write_register()` directly on the C struct. This ties selftest to the TCP C client. Three options:

   **Option A (recommended): Rewrite selftest to use `IModbusClient` C++ interface**
   - Rewrite `src/selftest.c` as `src/selftest.cpp`
   - Move `src/selftest.h` to `include/selftest.hpp`
   - Replace `modbus_client_t*` parameter with `windmi::IModbusClient*`
   - Replace `modbus_read_register(c, addr, &val)` → `client->readRegister(addr)`
   - Replace `modbus_write_register(c, addr, val)` → `client->writeRegister(addr, val)`
   - This makes selftest work with **any** `IModbusClient` implementation
   - Update `main.cpp` include from `#include "selftest.h"` to `#include "selftest.hpp"`
   - Update `main.cpp` selftest call: remove `getCClient()` cast, pass `modbus_client.get()` directly
   - Remove `ModbusClient::getCClient()` (no longer needed)

   **Option B: Duplicate selftest logic** — create `selftest_run_serial()` alongside `selftest_run()`
   - Bad: code duplication, must update both when registers change

   **Option C: Create a C function pointer abstraction** — complex, over-engineered

   Choosing **Option A** — it's clean, eliminates `getCClient()`, and makes selftest future-proof.

5. **Update logging** (`src/main.cpp`)
   - Add serial-specific log messages:
     ```
     [INFO ] Serial device: /dev/ttyUSB0 @ 9600 baud, 8N1
     [ERROR] Failed to open serial device /dev/ttyUSB0: No such file or directory
     [ERROR] Failed to open serial device /dev/ttyUSB0: Permission denied
            (are you in the 'dialout' group? sudo usermod -aG dialout $USER)
     [ERROR] Invalid baud rate: 12345 (supported: 9600, 19200, 38400, 57600, 115200)
     [ERROR] Invalid stop bits: 3 (supported: 1 or 2)
     [ERROR] --serial and --demo are mutually exclusive
     [ERROR] --baud requires --serial
     ```

6. **Update help text** (`src/main.cpp`)
   - Add `--serial`, `--baud`, `--parity`, `--stop-bits`, `--rs485` to the `--help` output

### Phase 6: Build System Changes

1. **Update `src/modbus/CMakeLists.txt`**
   - Add `ModbusSerialClient.cpp` to `windmi_modbus` sources
   - Add `${CMAKE_CURRENT_SOURCE_DIR}/../../src/modbus_serial_client.c` to `windmi_modbus` sources
   - Add `${CMAKE_CURRENT_SOURCE_DIR}/../../src/modbus_rtu_frame.c` to `windmi_modbus` sources
   - Do **not** add selftest here; selftest has its own top-level `windmi_selftest` target

2. **Update top-level `CMakeLists.txt` for selftest**
   - Current target builds `src/selftest.c`
   - Change it to build `src/selftest.cpp`
   - Link it against `windmi_modbus` and `windmi_utils` as before

3. **Update `tests/modbus/CMakeLists.txt`**
   - Add `test_modbus_serial` executable and test registration
   - Add `test_modbus_rtu_frame` if frame function tests are extracted

4. **Header installation** (if `CMakeLists.txt` has `install(DIRECTORY include/)`)
   - New headers `include/modbus_serial_client.h`, `include/modbus_rtu_frame.h`, `include/modbus/ModbusSerialClient.hpp`, and `include/selftest.hpp` will be automatically included

5. **Build verification:** ensure both `-DWINDMI_BUILD_TESTS=ON` and `OFF` compile cleanly

6. **CI update** (`.github/workflows/ci.yml`)
   - Serial port tests that require real hardware should be marked `DISABLED_` or use a CI-only guard (e.g., `if (getenv("WINDMI_CI_HARDWARE"))`) to avoid failures in GitHub Actions where no serial ports exist

### Phase 7: Testing

#### 7.1 Unit Tests — Frame Functions (test_modbus_rtu_frame.cpp)

Test the extracted shared frame functions with known CRC values:

```cpp
#include "modbus_rtu_frame.h"
#include "crc16.h"

// Verify known CRC values (verified against Python crc16_modbus implementation)
TEST(ModbusRtuFrameTest, BuildReadFrame_CRC) {
    uint8_t frame[8];
    size_t len = modbus_rtu_build_read_frame(frame, 1, 0x0000, 1);
    ASSERT_EQ(len, 8);
    // Frame: [0x01][0x03][0x00][0x00][0x00][0x01][CRC_lo][CRC_hi]
    // Known CRC for {0x01,0x03,0x00,0x00,0x00,0x01} = 0x0A84
    // Wire order (low byte first): 0x84, 0x0A
    EXPECT_EQ(frame[6], 0x84);
    EXPECT_EQ(frame[7], 0x0A);
}

TEST(ModbusRtuFrameTest, BuildWriteFrame_CRC) {
    uint8_t frame[8];
    size_t len = modbus_rtu_build_write_frame(frame, 1, 0x002C, 0x0002);
    ASSERT_EQ(len, 8);
    // Verify CRC independently
    uint16_t crc = crc16(frame, 6);
    uint16_t frame_crc = (uint16_t)frame[6] | ((uint16_t)frame[7] << 8);
    EXPECT_EQ(frame_crc, crc);
}

TEST(ModbusRtuFrameTest, ReadFrameSlaveId) {
    uint8_t frame[8];
    modbus_rtu_build_read_frame(frame, 11, 0x0001, 1);
    EXPECT_EQ(frame[0], 11);  // Slave ID
}

TEST(ModbusRtuFrameTest, ReadFrameFunctionCode) {
    uint8_t frame[8];
    modbus_rtu_build_read_frame(frame, 1, 0x0001, 1);
    EXPECT_EQ(frame[1], 0x03);  // Read Holding Registers
}

TEST(ModbusRtuFrameTest, WriteFrameFunctionCode) {
    uint8_t frame[8];
    modbus_rtu_build_write_frame(frame, 1, 0x002C, 0x0002);
    EXPECT_EQ(frame[1], 0x06);  // Write Single Register
}
```

#### 7.2 Unit Tests — Serial Client (test_modbus_serial.cpp)

Location: `tests/modbus/test_modbus_serial.cpp`

**Note:** CRC16 and RTU frame building tests now exist in `test_modbus_rtu_frame.cpp`. Do NOT duplicate them here. Focus on serial-specific concerns.

```cpp
#include <gtest/gtest.h>
#include <unistd.h>
#include "modbus/ModbusSerialClient.hpp"
extern "C" {
#include "modbus_serial_client.h"
}

using namespace windmi;

// Construction and lifecycle (no real hardware needed)
TEST(ModbusSerialClientTest, CreateAndDestroy) {
    ModbusSerialClient client("/dev/ttyUSB0", 9600, 'N', 1, false, 1);
    EXPECT_FALSE(client.isConnected());
}

TEST(ModbusSerialClientTest, CreateInvalidDevice) {
    // Should not crash, but connect() will fail
    ModbusSerialClient client("/dev/nonexistent", 9600, 'N', 1, false, 1);
    EXPECT_FALSE(client.connect());
}

TEST(ModbusSerialClientTest, DestructorDisconnects) {
    {
        ModbusSerialClient client("/dev/ttyUSB0", 9600, 'N', 1, false, 1);
        // Should not crash on scope exit
    }
}

// Baud rate validation (constructor should throw on invalid)
TEST(ModbusSerialClientTest, ValidBaudRates) {
    // None of these should throw at construction
    EXPECT_NO_THROW(ModbusSerialClient("/dev/ttyUSB0", 9600,   'N', 1, false, 1));
    EXPECT_NO_THROW(ModbusSerialClient("/dev/ttyUSB0", 19200,  'N', 1, false, 1));
    EXPECT_NO_THROW(ModbusSerialClient("/dev/ttyUSB0", 38400,  'N', 1, false, 1));
    EXPECT_NO_THROW(ModbusSerialClient("/dev/ttyUSB0", 57600,  'N', 1, false, 1));
    EXPECT_NO_THROW(ModbusSerialClient("/dev/ttyUSB0", 115200, 'N', 1, false, 1));
}

TEST(ModbusSerialClientTest, InvalidBaudRate) {
    EXPECT_THROW(ModbusSerialClient("/dev/ttyUSB0", 1200,   'N', 1, false, 1), ModbusException);
    EXPECT_THROW(ModbusSerialClient("/dev/ttyUSB0", 99999,  'N', 1, false, 1), ModbusException);
    EXPECT_THROW(ModbusSerialClient("/dev/ttyUSB0", 0,      'N', 1, false, 1), ModbusException);
    EXPECT_THROW(ModbusSerialClient("/dev/ttyUSB0", -1,     'N', 1, false, 1), ModbusException);
}

// Stop-bit validation
TEST(ModbusSerialClientTest, ValidStopBits) {
    EXPECT_NO_THROW(ModbusSerialClient("/dev/ttyUSB0", 9600, 'N', 1, false, 1));
    EXPECT_NO_THROW(ModbusSerialClient("/dev/ttyUSB0", 9600, 'N', 2, false, 1));
}

TEST(ModbusSerialClientTest, InvalidStopBits) {
    EXPECT_THROW(ModbusSerialClient("/dev/ttyUSB0", 9600, 'N', 0, false, 1), ModbusException);
    EXPECT_THROW(ModbusSerialClient("/dev/ttyUSB0", 9600, 'N', 3, false, 1), ModbusException);
}

// Parity validation
TEST(ModbusSerialClientTest, ValidParityValues) {
    EXPECT_NO_THROW(ModbusSerialClient("/dev/ttyUSB0", 9600, 'N', 1, false, 1));
    EXPECT_NO_THROW(ModbusSerialClient("/dev/ttyUSB0", 9600, 'E', 1, false, 1));
    EXPECT_NO_THROW(ModbusSerialClient("/dev/ttyUSB0", 9600, 'O', 1, false, 1));
}

TEST(ModbusSerialClientTest, InvalidParityValue) {
    EXPECT_THROW(ModbusSerialClient("/dev/ttyUSB0", 9600, 'X', 1, false, 1), ModbusException);
    EXPECT_THROW(ModbusSerialClient("/dev/ttyUSB0", 9600, 'M', 1, false, 1), ModbusException);
    EXPECT_THROW(ModbusSerialClient("/dev/ttyUSB0", 9600, 'S', 1, false, 1), ModbusException);
}

// Operations when not connected
TEST(ModbusSerialClientTest, ReadWhenNotConnected) {
    ModbusSerialClient client("/dev/ttyUSB0", 9600, 'N', 1, false, 1);
    EXPECT_THROW(client.readRegister(0x0001), ModbusException);
}

TEST(ModbusSerialClientTest, WriteWhenNotConnected) {
    ModbusSerialClient client("/dev/ttyUSB0", 9600, 'N', 1, false, 1);
    EXPECT_THROW(client.writeRegister(0x002C, 0), ModbusException);
}

// Permission denied test (guarded — only runs if device exists but is inaccessible)
TEST(ModbusSerialClientTest, PermissionDenied) {
    // /dev/ttyS0 typically requires 'dialout' group on Linux
    // This test is informational — it may pass or fail depending on user setup
    // Guard with environment check so CI doesn't fail
    if (access("/dev/ttyS0", F_OK) != 0) {
        GTEST_SKIP() << "/dev/ttyS0 not available on this system";
    }
    ModbusSerialClient client("/dev/ttyS0", 9600, 'N', 1, false, 1);
    // Connect may fail with permission denied — verify it doesn't crash
    client.connect();  // ignore result
}

// C Client API
TEST(ModbusSerialClientTest, CClientCreateDestroy) {
    modbus_serial_client_t *c = modbus_serial_client_create(
        "/dev/ttyUSB0", 9600, 'N', 1, false, 1);
    ASSERT_NE(c, nullptr);
    EXPECT_FALSE(modbus_serial_client_is_connected(c));
    modbus_serial_client_destroy(c);
}

TEST(ModbusSerialClientTest, CClientInvalidBaud) {
    modbus_serial_client_t *c = modbus_serial_client_create(
        "/dev/ttyUSB0", 12345, 'N', 1, false, 1);
    EXPECT_EQ(c, nullptr);  // Should fail at creation
}

TEST(ModbusSerialClientTest, CClientInvalidParity) {
    modbus_serial_client_t *c = modbus_serial_client_create(
        "/dev/ttyUSB0", 9600, 'X', 1, false, 1);
    EXPECT_EQ(c, nullptr);  // Should fail at creation
}

TEST(ModbusSerialClientTest, CClientInvalidStopBits) {
    modbus_serial_client_t *c = modbus_serial_client_create(
        "/dev/ttyUSB0", 9600, 'N', 0, false, 1);
    EXPECT_EQ(c, nullptr);  // Should fail at creation
    modbus_serial_client_t *c2 = modbus_serial_client_create(
        "/dev/ttyUSB0", 9600, 'N', 3, false, 1);
    EXPECT_EQ(c2, nullptr);
}
```

#### 7.3 Integration Testing with socat + pymodbus

The socat loopback by itself doesn't work — there's no Modbus slave on the other end to respond. A proper integration test requires a **mock Modbus RTU slave**.

```bash
# Step 1: Create virtual serial port pair
socat -d -d pty,raw,echo=0,link=/tmp/ttyV0 pty,raw,echo=0,link=/tmp/ttyV1 &

# Step 2: Start a mock Modbus RTU slave on /tmp/ttyV1
# Example script; pymodbus APIs vary by version.
# Pin a known version during implementation and adjust imports if needed.
python3 -m pip install "pymodbus>=3.0"
python3 << 'EOF' &
from pymodbus.server import StartSerialServer
from pymodbus.datastore import ModbusSlaveContext, ModbusServerContext

# Create a slave context with register values matching the Windmi
slave = ModbusSlaveContext(zero_mode=True)
slave.setValues(3, 0x1006, [8])      # Device type = 8 (Windmi 8kW)
slave.setValues(3, 0x0001, [120])    # Outdoor temp = 12.0°C
slave.setValues(3, 0x0004, [450])   # Leaving water temp = 45.0°C
slave.setValues(3, 0x00CE, [420])   # DHW tank temp = 42.0°C
slave.setValues(3, 0x002D, [2])     # Running status = Heating
slave.setValues(3, 0x0191, [450])   # Heating target = 45.0°C
slave.setValues(3, 0x0194, [460])   # DHW target = 46.0°C

# Controller default MODBUS_SLAVE_ID is 11
context = ModbusServerContext(slaves={11: slave})
StartSerialServer(context=context, port='/tmp/ttyV1', baudrate=9600)
EOF

# Step 3: Run our controller against /tmp/ttyV0
./build/windmi-control --serial /tmp/ttyV0 --baud 9600 -w 10000

# Step 4: Test API
curl http://localhost:10000/api/status
curl -X POST http://localhost:10000/api/set-mode -d '{"mode": 2}'
```

#### 7.4 Test Scenarios

| # | Scenario | Expected Result |
|---|----------|-----------------|
| 1 | Valid serial connection (socat + pymodbus) | Connects, reads registers successfully |
| 2 | Device path does not exist | Clear error: "Serial device /dev/XXX: No such file or directory" |
| 3 | Permission denied | Error with hint: "add user to 'dialout' group" |
| 4 | Invalid baud rate at construction | `ModbusException` thrown |
| 5 | Invalid parity at construction | `ModbusException` thrown |
| 6 | Response timeout | `ModbusException` thrown, ControlLoop retries next cycle |
| 7 | CRC mismatch on response | Discard frame, log error, retry |
| 8 | Serial device unplugged at runtime | `connected = false`, ControlLoop reconnect on next cycle |
| 9 | Serial device replugged at runtime | `connect()` re-opens and re-configures, reconnect succeeds |
| 10 | Multiple sequential register reads | Each gets proper response |
| 11 | Write register with read-back verify | Written value matches read-back value |
| 12 | Inter-frame timing | ≥4ms gap between consecutive frames |
| 13 | Selftest via serial (IModbusClient) | Same register verification as TCP |
| 14 | Shutdown OFF write via serial | OFF mode written before exit |
| 15 | `--serial` + `-i` specified | Error: "mutually exclusive options" |
| 16 | `--baud` without `--serial` | Error: "--baud requires --serial" |
| 17 | `--serial` with `--demo` | Error: "mutually exclusive options" |
| 18 | RS485 adapter auto-direction (no `--rs485`) | Works without `TIOCSRS485` ioctl |
| 19 | RS485 adapter with `--rs485` flag | `TIOCSRS485` ioctl called |
| 20 | Windmi default 8N1 (`--parity none --stop-bits 1`) | `CSTOPB` cleared in termios |
| 21 | Optional 8N2 (`--parity none --stop-bits 2`) | `CSTOPB` set in termios |
| 22 | Device re-enumerates as different path | `connect()` fails, logged clearly |

#### 7.5 Hardware Test Matrix

| Adapter | Chipset | Notes |
|---------|---------|-------|
| FTDI USB-RS485 | FT232RL | Auto direction via RTS, most common |
| Waveshare USB-RS485 | CH340 | Cheap, auto direction |
| Startech ICUSBRS485 | CP210x | Supports `TIOCSRS485`, use `--rs485` |
| Generic USB-Serial | PL2303 | May need `--rs485` flag |

## Files to Create/Modify

### New Files
| File | Description |
|------|-------------|
| `include/modbus_rtu_frame.h` | Shared RTU frame-building functions header |
| `src/modbus/modbus_rtu_frame.c` | Shared RTU frame-building implementation |
| `include/modbus_serial_client.h` | C serial client header (mirrors `modbus_client.h`) |
| `src/modbus/modbus_serial_client.c` | C serial client implementation |
| `include/modbus/ModbusSerialClient.hpp` | C++ wrapper header |
| `src/modbus/ModbusSerialClient.cpp` | C++ wrapper implementation |
| `tests/modbus/test_modbus_rtu_frame.cpp` | Unit tests for shared frame functions |
| `tests/modbus/test_modbus_serial_client.cpp` | Unit tests for serial client |

**Note:** `tests/test_modbus_frames.c` was removed - it was a stale legacy test file referencing extracted functions.

### Modified Files
| File | Changes |
|------|---------|
| `include/config.h` | Add `SERIAL_DEFAULT_BAUD`, `SERIAL_DEFAULT_PARITY`, `SERIAL_DEFAULT_STOP_BITS`, `MODBUS_SERIAL_TIMEOUT_MS` |
| `src/modbus_client.c` | Remove `STATIC_FOR_TEST` macro (dead code); remove `build_read_frame`/`build_write_frame` (extracted); use `modbus_rtu_build_*` |
| `src/main.cpp` | Add `--serial`, `--baud`, `--parity`, `--stop-bits`, `--rs485` CLI; update client creation; update shutdown client |
| `src/selftest.c` → `src/selftest.cpp` | Rewrite to use `windmi::IModbusClient*` instead of `modbus_client_t*`; eliminate `getCClient()` dependency |
| `src/selftest.h` → `include/selftest.hpp` | Move selftest header out of `src/`; project headers belong in `include/` |
| `src/modbus/CMakeLists.txt` | Add `ModbusSerialClient.cpp`, `modbus_rtu_frame.c`, `modbus_serial_client.c` |
| `tests/modbus/CMakeLists.txt` | Add `test_modbus_rtu_frame.cpp`, `test_modbus_serial_client.cpp` |
| `CMakeLists.txt` | Update the existing top-level `windmi_selftest` target from `src/selftest.c` to `src/selftest.cpp` |
| `README.md` | Add "Serial Port Connection" section |

**Removed:**
- `tests/test_modbus_frames.c` — stale legacy test file referencing extracted functions

### Removed
| File | Reason |
|------|--------|
| (none as standalone deletion) | `src/selftest.h` is moved to `include/selftest.hpp`; `ModbusClient::getCClient()` can be removed after selftest refactor, but leaving it is harmless |

## Design Decisions

1. **New C client vs. extending existing one**
   - Decision: Create separate `modbus_serial_client.c`
   - Reason: The existing `modbus_client_t` struct has `ip`/`port`/`socket_fd` fields that don't apply to serial. Extending it would require union types or void pointers, making the code harder to read and more error-prone.

2. **Shared frame functions in separate files**
   - Decision: Extract `build_read_frame()` / `build_write_frame()` into `modbus_rtu_frame.c` / `.h`
   - Reason: Both TCP and serial clients need identical frame building. Duplicating this code would be a maintenance hazard. A separate compilation unit is cleaner than sharing `STATIC_FOR_TEST` visibility.

3. **`IModbusClient` interface unchanged**
   - Decision: `ModbusSerialClient` implements the existing `IModbusClient` interface as-is
   - Reason: `ControlLoop` already depends on `IModbusClient`, not concrete types. No changes needed in `ControlLoop.cpp` or `WebServer.cpp`.

4. **Constructor takes connection parameters (not `connect()`)**
   - Consistent with existing `ModbusClient(host, port, slave_id)` pattern. The `connect()` method uses stored parameters, enabling reconnection.

5. **Blocking I/O with `select()` timeout**
   - Decision: Use blocking `open()` with `select()` for timeouts (same pattern as TCP client)
   - Reason: Simpler than non-blocking I/O with poll. `O_NONBLOCK` adds complexity with no benefit for a single-device point-to-point serial link.

6. **Stop bits explicit, default 1 for Windmi 8N1**
   - Decision: expose `--stop-bits` with default `1`
   - Reason: Windmi default is confirmed as 9600 8N1. Strict Modbus RTU no-parity setups may use 8N2, so `--stop-bits 2` remains available for compatible devices.

7. **RS485 direction: default off, explicit `--rs485` flag**
   - Decision: Do NOT call `TIOCSRS485` by default. Only when `--rs485` is specified.
   - Reason: Most USB-RS485 adapters handle direction automatically in hardware. Calling `TIOCSRS485` on unsupported adapters can cause undefined behavior. Opt-in is safer.

8. **Selftest rewrite to use `IModbusClient*`**
   - Decision: Rewrite selftest in C++ using `IModbusClient*` instead of `modbus_client_t*`
   - Reason: Eliminates `getCClient()`, works with any client type, no code duplication, future-proof.

9. **No UUCP lock files**
   - The application already uses `flock()` on `/tmp/windmi-controller.lock` for single-instance protection. Adding UUCP lock files (`/var/lock/LCK..ttyUSB0`) would be redundant for a single-instance daemon.

10. **Serial port permissions**
    - Document that the user must be in the `dialout` group: `sudo usermod -aG dialout $USER`
    - Provide a helpful error message on `EACCES` with the fix

## Out of Scope (Future Work)

- **Modbus ASCII mode** — RTU is the standard for RS485; ASCII is rarely used
- **Multi-drop bus (multiple slaves)** — Windmi uses a single slave; multi-drop adds addressing complexity
- **Serial port hotplug monitoring (udev/inotify)** — ControlLoop's reconnect logic handles this; the user must restart with a new device path if USB re-enumerates differently
- **Configurable data bits** — Windmi uses 8 data bits; `--stop-bits` is included because Windmi defaults to 8N1 while some Modbus RTU devices use 8N2
- **libmodbus dependency** — Writing our own client keeps dependencies minimal and matches the existing architecture
- **Modbus RTU over TCP/UDP bridge mode** — the existing TCP client already provides this via Waveshare gateway

## References

- Modbus over Serial Line Specification and Implementation Guide V1.02 (modbus.org)
- Linux `termios(3)` man page
- Linux Serial HOWTO: https://tldp.org/HOWTO/Serial-HOWTO
- `TIOCSRS485` ioctl: Linux kernel documentation (`Documentation/driver-api/serial/serial-rs485.rst`)
- Windmi Modbus register map (see `include/config.h`)