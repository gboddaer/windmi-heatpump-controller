# Rotenso Windmi Heat Pump Controller

[![License: GPL v2](https://img.shields.io/badge/License-GPL_v2-blue.svg)](https://www.gnu.org/licenses/old-licenses/gpl-2.0)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-17-orange.svg)](https://en.cppreference.com/w/cpp/language/history)
[![C Standard](https://img.shields.io/badge/C-99-blue.svg)](https://en.cppreference.com/w/c/language/history)

Embedded web server for controlling Rotenso Windmi heat pumps via Modbus TCP.

## Overview

This project provides a web-based interface for monitoring and controlling Rotenso Windmi heat pumps. The controller connects to the Waveshare Modbus gateway (or compatible Modbus TCP devices) and exposes a REST API for reading status and setting parameters.

### Features

- **Web Interface**: Built-in HTTP server with static HTML page for local control
- **REST API**: JSON-based endpoints for status, mode control, and temperature setpoints
- **Modbus TCP**: Direct TCP communication with Waveshare gateway (transparent mode)
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
└── googletest/                 # Google Test (submodule)
```

## Prerequisites

- **Build Tools**: CMake 3.16+, GCC with C99/C++17 support, or Clang
- **Dependencies**:
  - Mongoose (embedded web server) - included as submodule
  - Google Test - included as submodule
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

### Command-Line Options

| Option | Long Form | Description | Default |
|--------|-----------|-------------|---------|
| `-i <addr>` | `--ip <addr>` | Modbus gateway IP address | 192.168.123.10 |
| `-p <port>` | `--port <port>` | Modbus gateway TCP port | 8899 |
| `-w <port>` | `--web <port>` | HTTP server port | 8080 |
| `-d` | `--demo` | Demo mode (simulated device) | disabled |
| `-l <level>` | `--log-level <level>` | Logging level (TRACE/DEBUG/INFO/WARN/ERROR/FATAL) | INFO |
| `-o <file>` | `--log-file <file>` | Log file path (default: console only) | none |
| `-t` | `--selftest` | Run self-test suite | disabled |
| `-h` | `--help` | Show help message | - |

## REST API

### Endpoints

#### GET `/api/status`

Get current heat pump status.

```json
{
  "mode": 2,
  "runningStatus": 2,
  "dhwTemp": 45.5,
  "heatingTemp": 38.2,
  "outsideTemp": 12.3,
  "targetDhw": 48.0,
  "targetHeating": 38.0,
  "dhwPriority": 1,
  "errors": [],
  "timestamp": "2026-06-01T14:32:05Z"
}
```

**Mode values:**
- `0`: OFF
- `1`: DHW only
- `2`: Heating only
- `3`: DHW + Heating

**Running status values:**
- `0`: Off
- `1`: Cool
- `2`: Heat
- `4`: DHW
- `7`: Defrost
- `20`: Anti-freeze

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
  -d '{"target": 48.0}' http://localhost:8080/api/set-dhw
```

#### POST `/api/set-heating`

Set heating target temperature.

```bash
curl -X POST -H "Content-Type: application/json" \
  -d '{"target": 36.0}' http://localhost:8080/api/set-heating
```

#### POST `/api/set-priority`

Set DHW priority.

```bash
curl -X POST -H "Content-Type: application/json" \
  -d '{"priority": 1}' http://localhost:8080/api/set-priority
```

## Working Modes Strategy

The controller exposes 4 UI working modes that map to the device's native capabilities:

| UI Mode | Mode Register | Heating Target | DHW Target | DHW Priority |
|---------|---------------|----------------|------------|--------------|
| OFF (`0`) | 0 (Off) | - | - | - |
| DHW only (`1`) | 2 (Heat+DHW) | 25°C (min) | user setpoint | 1 |
| Heating only (`2`) | 2 (Heat+DHW) | user setpoint | 40°C (min) | 0 |
| DHW+Heating (`3`) | 2 (Heat+DHW) | user setpoint | user setpoint | 1 |

**Note:** The device's mode register (`0x002C`) only supports "Off" and "Heat+DHW" natively. The controller emulates "DHW only" and "Heating only" by setting minimum target temperatures to suppress demand.

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

- **ControlLoop**: State machine for working modes, target temperature management
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
./tests/utils/test_utils --gtest_filter='LoggerTest.*'

# Specific test
./tests/modbus/test_modbus --gtest_filter='ModbusClientTest.ReadWrite'
```

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

Developed for Rotenso Windmi heat pump integration.

---

*For technical support or questions, please open an issue on GitHub.*
