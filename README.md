# Windmi Heat Pump Controller

[![License: GPL v2](https://img.shields.io/badge/License-GPL_v2-blue.svg)](https://www.gnu.org/licenses/old-licenses/gpl-2.0)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-17-orange.svg)](https://en.cppreference.com/w/cpp/language/history)
[![C Standard](https://img.shields.io/badge/C-99-blue.svg)](https://en.cppreference.com/w/c/language/history)

Embedded web server for controlling Windmi heat pumps via Modbus TCP.

> **Disclaimer:** This is a hobby project not affiliated with, endorsed by, or connected to Rotenso or any of its affiliates. This software is provided "as-is" without any warranty. Users bear full responsibility for the use of this program. The author is not liable for any damages, losses, or incorrect operation resulting from the use of this software.

## Overview

This project provides a web-based interface for monitoring and controlling Rotenso Windmi heat pumps. The controller connects to the Waveshare Modbus gateway (or compatible Modbus TCP devices) and exposes a REST API for reading status and setting parameters.

### Features

- **Web Interface**: Built-in HTTP server with static HTML page for local control
- **REST API**: JSON-based endpoints for status, mode control, and temperature setpoints
- **Modbus TCP**: Direct TCP communication with Waveshare gateway (transparent mode)
- **Modbus RTU over Serial (RS-485)**: Serial port communication for devices without TCP gateway
- **Multi-threaded**: Separate threads for HTTP server and Modbus operations
- **Thread-safe**: Lock-free SPSC queues for inter-thread communication
- **Comprehensive Logging**: Structured logging with levels, timestamps, and component tags
- **Unit Tests**: Google Test framework for verified functionality
- **CMake Build System**: Cross-platform build configuration

## Architecture

### Thread Model

The application uses two threads communicating via lock-free SPSC queues:

1. **Main Thread (HTTP Server)**
   - Mongoose HTTP server
   - Static file serving
   - REST API endpoints
   - JSON request/response handling

2. **Modbus Thread**
   - TCP connection to Modbus gateway
   - Register polling (every 30 seconds)
   - Control logic and state machine
   - Write verification

```
┌─────────────────┐     ┌──────────────────┐
│  Main Thread    │     │  Modbus Thread   │
│  (HTTP Server)  │     │   (Modbus I/O)   │
├─────────────────┤     ├──────────────────┤
│  Mongoose       │     │  Modbus Client   │
│  WebServer      │────▶│  ControlLoop     │
│  REST API       │     │  StatusMonitor   │
└─────────────────┘     └──────────────────┘
```

### Directory Structure

```
.
├── CMakeLists.txt              # Main build configuration
├── Makefile                    # Legacy Make build (backward compatibility)
├── include/                    # Public headers
│   ├── core/                   # Core controller logic
│   ├── modbus/                 # Modbus client interfaces
│   ├── web/                    # Web server interfaces
│   └── utils/                  # Utility classes (Logger, Config, etc.)
├── src/                        # Source code
│   ├── core/                   # Core implementation (C++)
│   ├── modbus/                 # Modbus implementation (C++)
│   ├── web/                    # Web server implementation (C++)
│   └── utils/                  # Utility implementation (C++)
├── tests/                      # Unit tests
│   ├── core/
│   ├── modbus/
│   ├── web/
│   └── utils/
├── docs/                       # Documentation
│   ├── doxygen/                # Doxygen configuration
│   └── *.md                    # Technical documentation
├── static/                     # Web interface files
├── mongoose/                   # Mongoose library (submodule)
└── googletest/                 # Local Google Test checkout (tests use the copy CMake fetches)
```

## Prerequisites

- **Build Tools**: CMake 3.16+, GCC with C99/C++17 support, or Clang
- **Dependencies**:
  - Mongoose (embedded web server) - included as submodule
  - Google Test - fetched by CMake with `FetchContent`; only Mongoose is a submodule
  - pthreads (standard on most systems)

## Quick Start

### Build from Source

```bash
# Clone the repository with submodules
git clone --recursive https://github.com/yourusername/wpomp.git
cd wpomp

# Create build directory
mkdir build && cd build

# Configure with CMake
cmake ..

# Build
make -j$(nproc)

# Or use the Makefile (legacy)
cd ..
make setup
make
```

### Run the Controller

```bash
# Run with defaults (gateway 192.168.123.10:8899)
./build/windmi-control

# Run with custom gateway IP and port
./build/windmi-control --ip 192.168.1.50 --port 4196

# Run with custom web server port
./build/windmi-control --web 3000

# Enable demo mode (no hardware required)
./build/windmi-control --demo

# Run with debug logging
./build/windmi-control --log-level DEBUG

# Run self-test
./build/windmi-control --selftest

# Show all options
./build/windmi-control --help
```

### Serial Port (Modbus RTU over RS-485)

For devices connected via RS-485 without a TCP gateway, use the serial port mode. The controller uses **9600 8N1** as the default configuration (matching Windmi device defaults).

```bash
# Connect via serial device with default 9600 8N1
./build/windmi-control --serial /dev/ttyUSB0

# Connect with custom baud rate
./build/windmi-control --serial /dev/ttyUSB0 --baud 19200

# Connect with 8E1 configuration
./build/windmi-control --serial /dev/ttyUSB0 --parity E

# Connect with 8N2 configuration
./build/windmi-control --serial /dev/ttyUSB0 --stop-bits 2

# Enable RS-485 direction control
./build/windmi-control --serial /dev/ttyUSB0 --rs485
```

**Note:** Serial mode (`--serial`) is mutually exclusive with TCP mode (`--ip`, `--port`). You cannot specify both.

**Supported baud rates:** 9600, 19200, 38400, 57600, 115200

**Parity options:** `N` (none/8N1), `E` (even/8E1), `O` (odd/8O1)

**Stop bits:** 1 or 2

### Command-Line Options

| Option | Long Form | Description | Default |
|--------|-----------|-------------|---------|
| `-i <addr>` | `--ip <addr>` | Modbus gateway IP address | 192.168.123.10 |
| `-p <port>` | `--port <port>` | Modbus gateway TCP port | 8899 |
| `-w <port>` | `--web <port>` | HTTP server port | 8080 (10000 in demo mode) |
| `-t <dir>` | `--static-dir <dir>` | Static files directory | static |
| `-d` | `--demo` | Demo mode (simulated device) | disabled |
| `-l <level>` | `--log-level <level>` | Logging level (TRACE/DEBUG/INFO/WARN/ERROR/FATAL) | INFO |
| `-o <file>` | `--log-file <file>` | Log file path (default: console only) | none |
| `-s` | `--selftest` | Run self-test suite and exit | disabled |
| | `--dry-run` | Evaluate and log control decisions without writing any register | disabled |
| | `--config <path>` | Settings file | ~/.windmi/settings.ini |
| `--serial <device>` | - | Serial device for Modbus RTU (e.g., /dev/ttyUSB0) | none |
| `--baud <rate>` | - | Baud rate | 9600 |
| `--parity <N|E|O>` | - | Parity: N(none), E(even), O(odd) | N |
| `--stop-bits <1|2>` | - | Stop bits: 1 or 2 | 1 |
| `--rs485` | - | Enable RS-485 direction control | disabled |
| `-h` | `--help` | Show help message | - |

## REST API

### Endpoints

#### GET `/api/status`

Get current heat pump status.

```json
{
  "dhwTemperature": 45.5,
  "dhwTarget": 50.0,
  "heatingTemperature": 38.2,
  "heatingTarget": 40.0,
  "outdoorTemperature": 7.1,
  "leavingWaterTemperature": 44.8,
  "enteringWaterTemperature": 39.5,
  "mode": "heat+dhw",
  "runningStatus": "heat",
  "priority": "dhw",
  "status": "running",
  "deviceOnline": true,
  "acCurrent": 6.2,
  "dcCurrent": 4.1,
  "acVoltage": 231.0,
  "dcVoltage": 320.5,
  "acPowerVA": 1430.0,
  "acPowerW": 1320.0,
  "powerValid": true,
  "compressorFrequency": 62.0,
  "waterFlow": 12.4,
  "unitCapacityKw": 8,
  "actualCapacityOutput": 6,
  "oduInputStatus": 0,
  "compressorRuntimeHours": 4211,
  "pumpRuntimeHours": 5120,
  "heatOutputW": 4680.0,
  "cop": 3.54,
  "copValid": true,
  "dhwValveStatus": 0,
  "dhwModeStatus": 2,
  "workingMode": 3,
  "occupancyMode": 0,
  "scheduleActive": true,
  "schedulePaused": false,
  "dhwCalling": true,
  "dhwStateKnown": true,
  "requestedDeviceMode": "dhw_only",
  "secondsSinceModeWrite": 36,
  "dhwSensorFaultSuspected": false,
  "tankReadFailures": 0
}
```

`mode` is the device mode written to `0x002C`, as a name: `off`, `cool+dhw`, `heat+dhw`,
`cool`, `heat`, `dhw_only`. `workingMode` is the application working mode: 0
off, 1 DHW only, 2 heating only, 3 both. `runningStatus` is what the unit is actually
doing: `off`, `cool`, `heat`, `dhw`, `defrost`, `antifreeze`.

The four fields at the end are the DHW arbiter: whether the tank is demanding, whether
that verdict came from a usable reading, the device mode the arbiter decided on, and
seconds since the last verified mode write (`-1`: none since startup).

#### POST `/api/set-mode`

Set working mode.

```bash
curl -X POST -H "Content-Type: application/json" \
  -d '{"mode": 2}' http://localhost:8080/api/set-mode
```

#### POST `/api/set-dhw`

Set DHW target temperature.

```bash
curl -X POST -H "Content-Type: application/json" \
  -d '{"temperature": 48.0}' http://localhost:8080/api/set-dhw
```

#### POST `/api/set-heating`

Set heating target temperature.

```bash
curl -X POST -H "Content-Type: application/json" \
  -d '{"temperature": 36.0}' http://localhost:8080/api/set-heating
```

#### POST `/api/set-priority`

Accepted and ignored. Domestic hot water always has priority and the unit has no
heating-priority state to switch to, so there is nothing for this call to do; it stays so
that an old client gets a 200 instead of a 404. See `docs/dhw_priority_report.md`.

## Working Modes Strategy

The controller exposes 4 working modes. Each one is a native device mode, so no setpoint
is touched to express a demand:

| Working mode | Device mode `0x002C` |
|--------------|----------------------|
| OFF (`0`) | `0` Off |
| DHW only (`1`) | `5` DHW only |
| Heating only (`2`) | `4` Heat only |
| DHW + Heating (`3`) | `5` while the tank is demanding, `2` Heat + DHW once it is confirmed at target |

### DHW priority

Hot water has priority: this heat pump is the only thing that makes hot water, and the
house has other heat sources. With both demands enabled, the compressor goes to the tank
when the tank is cold and back to the house when the tank is full.

The rule, in words: demand is a fall more than 5 °C below target; satisfaction is
`target - 0.5 °C` held for two 30 s ticks; a sag inside the band is not a demand; a grant
waits 120 s after the last mode write unless the tank is falling at 1 °C/min or faster,
which is somebody using hot water; a release never waits; and nothing is written during
defrost or anti-freeze. Every mode write is verified by read-back.

Two of those rules exist because the unit does not do what its registers say. The tank is
measured against the **higher** of `0x0194` and `0x0196`, because with the unit in its
economic DHW sub-mode the charge runs to `0x0196` and stops there whatever `0x0194` asks,
and `setDhwTarget` writes both. And a grant the unit does not act on is not a demand being
served: compressor and flow idle for 10 minutes means the charge was declined, the house
gets the compressor back, and the next attempt waits 15 minutes — a hot-water draw is served
immediately. Without that cooldown the release was a metronome, because a unit that will not
charge to a setpoint does not change its mind when asked again.

The margins are configurable, the priority is not:

```ini
[arbitration]
dhw_hysteresis_c = 5.0            # floor 5.0
dhw_min_switch_gap_s = 120        # floor 60
dhw_charge_watchdog_min = 120     # floor 60, counted in compressor minutes
dhw_draw_override_c_per_min = 1.0 # floor 0.5
dhw_park_timeout_s = 600          # floor 180, idle on a grant before the house gets it back
dhw_park_cooldown_s = 900         # floor 300, before asking the unit to charge again
```

Values below a floor are clamped with a warning: the floors are the compressor's and the
diverter valve's, not preferences. `--dry-run` runs the whole loop, logging every decision
and publishing it in `/api/status`, without writing a single register.

On exit the application leaves the unit in **DHW only**, verified by read-back: hot water is
the demand nothing else in this house covers, and `heat+dhw` with no arbiter running means
heating-first and a tank nobody is arguing for. The flip side is that while the application
is not running there is no central heating — run it as a service, or use the display
controller.

Full detail: [working-modes.md](docs/working-modes.md); the measurements behind it:
[dhw_priority_report.md](docs/dhw_priority_report.md).

## Configuration

### Compile-time Defaults

Edit `include/config.h` to change compiled-in defaults:

```c
#define MODBUS_DEFAULT_IP "192.168.123.10"
#define MODBUS_DEFAULT_PORT 8899
#define WEB_SERVER_DEFAULT_PORT 8080
#define LOG_DEFAULT_LEVEL WINDMI_LOG_INFO
```

### Runtime Configuration

Use command-line arguments (see above) to override defaults at runtime.

## Development

### Build with Tests

```bash
mkdir build && cd build
cmake -DWINDMI_BUILD_TESTS=ON ..
make -j$(nproc)

# Run tests
ctest --output-on-failure
```

### Build with Documentation

```bash
mkdir build && cd build
cmake -DWINDMI_BUILD_DOCS=ON ..
make docs

# View documentation
firefox docs/html/index.html
```

### Code Structure

#### Core Components

- **ControlLoop**: Control tick, tank state, and the DHW demand arbiter
- **Arbitration**: Pure decision function — sensors in, device mode out; no I/O, fully unit-tested
- **StatusMonitor**: Thread-safe status data aggregation
- **ModbusClient**: Modbus TCP communication with gateway
- **WebServer**: HTTP server and REST API implementation

#### Utilities

- **Logger**: Structured logging with levels, timestamps, and component tags
- **Config**: Application configuration management
- **JsonHelpers**: JSON serialization/deserialization
- **SpscQueue**: Single-producer single-consumer lock-free queue

### Adding New Features

1. Create new source files in appropriate `src/` subdirectory
2. Add public headers to `include/` with proper namespace
3. Update `CMakeLists.txt` to include new sources
4. Add unit tests in corresponding `tests/` subdirectory
5. Update documentation as needed

## Testing

### Unit Test Categories

- **Core Tests**: ControlLoop state machine, StatusMonitor thread safety
- **Modbus Tests**: Frame encoding/decoding, CRC calculation
- **Web Tests**: HTTP routing, JSON parsing
- **Utils Tests**: Logger filtering, Config loading, Queue operations

### Running Tests

```bash
# All tests
ctest --output-on-failure

# Specific test suite
./build/tests/utils/test_utils --gtest_filter='LoggerTest.*'

# Specific test
./build/tests/modbus/test_modbus --gtest_filter='ModbusClientTest.ReadWrite'
```

The web UI has no test framework, so it has a checker instead:

```bash
./tools/check_ui_refs.py
```

It fails when `app.js` reaches for a DOM element `index.html` does not define. That is
not pedantry: a deleted button left one `addEventListener` line behind, `app.js` threw on
load, the page never polled `/api/status`, and the controller appeared to be offline and
switched off while the API said online and DHW-only. `node --check` passed, because the
file was syntactically perfect.

## Troubleshooting

### Common Issues

**Connection refused to Modbus gateway:**
- Verify gateway IP address with `--ip` option
- Check gateway is reachable with `ping`
- Verify gateway port (default 8899)

**Web server port already in use:**
- Use `--web` to specify alternative port
- Stop conflicting service or use `sudo` for port < 1024

**No logs visible:**
- Check log level with `--log-level DEBUG`
- Verify console output is not being redirected

**Build errors:**
- Ensure submodules are initialized: `git submodule update --init --recursive`
- Verify CMake version: `cmake --version`
- Check compiler supports C++17: `g++ --version`

## Contributing

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit changes (`git commit -m 'Add amazing feature'`)
4. Push branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

### Development Guidelines

- Follow existing code style (C++17, C99)
- Add unit tests for new functionality
- Update documentation as needed
- Ensure all tests pass before submitting PR

## License

This project is licensed under the GNU General Public License v2.0 - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- [Mongoose](https://github.com/cesanta/mongoose) - Embedded web server library
- [Google Test](https://github.com/google/googletest) - Unit testing framework

## See Also

- [CONVERSION_PLAN.md](docs/CONVERSION_PLAN.md) - C++ conversion plan and architecture
- [LOGGING_MIGRATION_PLAN.md](docs/LOGGING_MIGRATION_PLAN.md) - Structured logging implementation
- [TEST_PLAN_AND_FIX_IMPLEMENTATION.md](docs/TEST_PLAN_AND_FIX_IMPLEMENTATION.md) - Test coverage and fixes
- [working-modes.md](docs/working-modes.md) - Working mode strategy details

## Author

This project was developed for Windmi heat pump integration.

---

*For technical support or questions, please open an issue on GitHub.*
