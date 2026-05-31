# Rotenso Windmi Heat Pump Controller

Embedded web server for controlling Rotenso Windmi heat pumps via Modbus TCP.

## Prerequisites

- GCC with C99 support
- Make
- Mongoose library (see below)

## Setup

```bash
# Download Mongoose
make setup

# Build
make

# Run with defaults (gateway 192.168.123.10:8899)
./windmi-control

# Run with custom gateway IP
./windmi-control --ip 192.168.1.50

# Run with custom gateway IP and port
./windmi-control --ip 192.168.1.50 --port 4196

# Run with custom web server port
./windmi-control --web 3000

# Show help
./windmi-control --help
```

## Configuration

Defaults can be overridden via command-line arguments:
- `--ip <address>`: Modbus gateway IP (default: 192.168.123.10)
- `--port <port>`: Modbus gateway port (default: 8899)
- `--web <port>`: Web server HTTP port (default: 8080)

Edit `src/config.h` to change compiled-in defaults.

## Architecture

Two threads communicating via lock-free SPSC queues:
1. Main thread: Mongoose HTTP server, static files, REST API
2. Modbus thread: TCP connection, register polling, control logic, write verification

## Waveshare Gateway

The Waveshare gateway operates in transparent mode (TCP:8899).
Full Modbus RTU frames (including CRC) are sent/received over the TCP socket.
No MBAP header. No virtual COM port needed.

## Working mode strategy

See `docs/working-modes.md` for details about:
- UI working modes (`off`, `dhw`, `heat`, `both`)
- mode register (`0x002C`) vs running status register (`0x002D`)
- DHW/heating target override behavior used to emulate single-purpose modes
