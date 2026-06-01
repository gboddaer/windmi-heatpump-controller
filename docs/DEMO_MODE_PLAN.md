# Demo Mode Plan

## 1. Goal

Add a demo/simulation mode where the controller can run without connecting to
the physical Windmi heat pump or Modbus TCP gateway. In demo mode:

- No socket is opened to the real gateway.
- The web UI and control loop run normally.
- Reads and writes go to an in-process simulated Modbus device.
- UI commands visibly affect simulated state.
- Shutdown does not write OFF mode to the real device.

Primary use cases:
- Frontend/web UI demos
- Local development without hardware
- Regression testing control-loop behavior
- Safe development of command handling and logging

---

## 2. Current Architecture Summary

Current runtime path:

```text
main.cpp
  └── windmi::ModbusClient
        └── src/modbus_client.c
              └── TCP socket to Modbus gateway

ControlLoop
  └── ModbusClient* modbus_client_
        ├── connect()
        ├── isConnected()
        ├── readRegister()
        ├── writeRegister()
        └── disconnect()
```

Important details:
- `ControlLoop::start()` currently accepts a concrete `ModbusClient*`.
- `ModbusClient` methods are not virtual.
- `main.cpp` creates a real `ModbusClient` unconditionally.
- Shutdown creates a second real `ModbusClient` to write OFF mode.
- `selftest.c` takes a raw `modbus_client_t*`, so it is tied to the C Modbus client.

Because of this, demo mode should **not** be bolted into `modbus_client.c` with
conditionals. The cleaner approach is an abstraction layer above the real
transport.

---

## 3. Design Decision

Use an interface-based design:

```cpp
class ModbusException : public std::runtime_error {
public:
    explicit ModbusException(const std::string& msg)
        : std::runtime_error(msg) {}
};

class IModbusClient {
public:
    virtual ~IModbusClient() = default;
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    virtual int16_t readRegister(uint16_t address) = 0;
    virtual void writeRegister(uint16_t address, uint16_t value) = 0;
    virtual void flushBuffer() = 0;
    virtual std::string getLastError() const = 0;
};
```

`ModbusException` should move out of `ModbusClient.hpp` into this shared API.
That avoids making `ControlLoop.cpp` include the concrete socket client just to
catch exceptions.

Then provide two implementations:

```text
RealModbusClient / existing ModbusClient
  └── wraps current C socket implementation

SimulatedModbusClient
  └── stores register values in memory and simulates device behavior
```

`ControlLoop` should depend on `IModbusClient*`, not `ModbusClient*`.

Why this approach:
- Keeps socket code unchanged.
- Keeps simulation deterministic and testable.
- Avoids hidden behavior switches inside low-level Modbus code.
- Allows future test doubles beyond demo mode.

---

## 4. Files to Add

### `include/modbus/IModbusClient.hpp`
Defines the abstract interface used by `ControlLoop`.

This header should also define the shared `ModbusException` type, or include a
new `modbus/ModbusException.hpp`. Recommended: define `ModbusException` in
`IModbusClient.hpp` initially to minimize file churn. Both the real and
simulated clients must throw this same exception type so existing
`ControlLoop.cpp` catch blocks continue to work unchanged.

Minimum includes for this header:
```cpp
#include <cstdint>
#include <stdexcept>
#include <string>
```

### `include/modbus/SimulatedModbusClient.hpp`
Declares the simulator implementation.

### `src/modbus/SimulatedModbusClient.cpp`
Implements simulated register storage and basic heat-pump behavior.

---

## 5. Files to Modify

### `include/modbus/ModbusClient.hpp`
- Include `modbus/IModbusClient.hpp`.
- Remove the local `ModbusException` definition after it is moved to the shared API.
- Make `ModbusClient` inherit from `IModbusClient`.
- Mark overridden methods with `override`.
- Keep `getCClient()` as a concrete-only helper for selftest compatibility.

### `include/core/ControlLoop.hpp`
- Forward declare `IModbusClient` instead of `ModbusClient`.
- Change:
  ```cpp
  bool start(ModbusClient* client, CmdQueue* cmd_queue, StatusQueue* status_queue);
  ModbusClient* modbus_client_;
  ```
  to:
  ```cpp
  bool start(IModbusClient* client, CmdQueue* cmd_queue, StatusQueue* status_queue);
  IModbusClient* modbus_client_;
  ```

### `src/core/ControlLoop.cpp`
- Include `modbus/IModbusClient.hpp` instead of `modbus/ModbusClient.hpp`.
- No behavior changes should be required beyond the type change.
- Continue catching `ModbusException`; the simulator must throw the same shared type.

### `src/main.cpp`
- Add CLI flag:
  ```text
  --demo              Run without connecting to real Windmi device
  ```
- Replace the current stack-allocated concrete client:
  ```cpp
  windmi::ModbusClient modbus_client(modbus_ip, modbus_port, MODBUS_SLAVE_ID);
  ```
  with an owning interface pointer:
  ```cpp
  std::unique_ptr<windmi::IModbusClient> modbus_client;

  if (demo_mode) {
      modbus_client = std::make_unique<windmi::SimulatedModbusClient>();
  } else {
      modbus_client = std::make_unique<windmi::ModbusClient>(modbus_ip, modbus_port, MODBUS_SLAVE_ID);
  }
  ```
- Update calls from `modbus_client.connect()` to `modbus_client->connect()` etc.
- Self-test remains real-client only because `selftest.c` requires `modbus_client_t*`:
  ```cpp
  if (run_selftest && demo_mode) {
      fprintf(stderr, "--selftest is not supported in demo mode\n");
      release_lock();
      return 1;
  }

  if (run_selftest) {
      auto* real_client = dynamic_cast<windmi::ModbusClient*>(modbus_client.get());
      // real_client is guaranteed non-null because demo+selftest was rejected.
      modbus_client_t* c_client = static_cast<modbus_client_t*>(real_client->getCClient());
      ...
  }
  ```
- In demo mode, skip the real shutdown OFF write via dedicated client with an explicit guard:
  ```cpp
  if (!demo_mode) {
      // existing dedicated shutdown ModbusClient OFF write block
  } else {
      printf("[Shutdown] DEMO MODE: skipping real OFF write\n");
  }
  ```

### `src/modbus/CMakeLists.txt`
- Add `SimulatedModbusClient.cpp` to `windmi_modbus`.

---

## 6. Simulator Behavior

### Register Storage
Use an in-memory map initially:

```cpp
std::unordered_map<uint16_t, int16_t> registers_;
```

A fixed struct with named fields would be more type-safe, but the register set is
small and demo mode benefits from permissive, easy-to-extend storage. Use the
map for the first implementation; consider a stricter fixed representation only
if simulator behavior becomes complex.

Initialize all registers used by the application:

| Register | Macro | Initial Demo Value |
|---|---|---:|
| Device type | `REG_DEVICE_TYPE` | `8` |
| Outdoor temp | `REG_OUTDOOR_TEMP` | `120` (12.0 C) |
| Indoor temp | `REG_INDOOR_TEMP` | `210` (21.0 C) |
| Entering water temp | `REG_ENTERING_WATER_TEMP` | `320` (32.0 C) |
| Leaving water temp | `REG_LEAVING_WATER_TEMP` | `350` (35.0 C) |
| Running mode | `REG_RUNNING_MODE` | `MODE_SET_HEAT_DHW` |
| Running status | `REG_RUNNING_STATUS` | `MODE_STATUS_HEAT` |
| DHW target | `REG_DHW_TARGET` | `460` (46.0 C) |
| Heating target | `REG_HEATING_TARGET` | `450` (45.0 C) |
| DHW tank temp | `REG_DHW_TANK_TEMP` | `420` (42.0 C) |
| DHW priority | `REG_DHW_PRIORITY` | `1` |
| AC current | `REG_AC_CURRENT` | `3` |
| DC current | `REG_DC_CURRENT` | `2` |
| AC voltage | `REG_AC_VOLTAGE` | `230` |
| DC voltage | `REG_DC_VOLTAGE` | `700` |

Remember the existing scaling rules:
- Temperatures are raw 0.1 C.
- AC current is raw × 2.
- DC current is raw × 4.
- DC voltage is raw / 2.

### Connection Semantics
- `connect()` sets `connected_ = true` and returns true. It never opens a socket.
- `disconnect()` sets `connected_ = false`.
- `isConnected()` returns `connected_`.
- `flushBuffer()` is a no-op.
- `readRegister()` and `writeRegister()` throw `ModbusException` if not connected.

### Write Semantics
`writeRegister()` updates writable registers:
- `REG_RUNNING_MODE`
- `REG_DHW_TARGET`
- `REG_HEATING_TARGET`
- `REG_DHW_PRIORITY`
- `REG_OCCUPANCY_MODE` if needed later

For unknown or read-only registers, choose one policy:
1. Strict mode: throw `ModbusException`
2. Permissive demo mode: store the value and continue

Recommended default: permissive for smoother demos, with optional strict mode for tests.

### Dynamic Simulation
On each read/write, update simulated values based on wall-clock elapsed time:

- If running mode is OFF:
  - `REG_RUNNING_STATUS = MODE_STATUS_OFF`
  - Temperatures drift slowly toward ambient values.

- If heating is active:
  - `REG_RUNNING_STATUS = MODE_STATUS_HEAT`
  - Leaving water temperature approaches heating target.
  - AC current/power become non-zero.

- If DHW priority is enabled and DHW tank temp is below target:
  - `REG_RUNNING_STATUS = MODE_STATUS_DHW`
  - DHW tank temp slowly approaches DHW target.

- If targets are reached:
  - Device may remain in heat/DHW status or drop to idle/off-like state depending on desired realism.

Keep the simulation simple and deterministic. The point is not thermodynamic accuracy; the point is believable UI behavior.

Suggested implementation:
```cpp
void SimulatedModbusClient::updateSimulationLocked();
std::chrono::steady_clock::time_point last_update_;
```
called at the beginning of `readRegister()` and `writeRegister()`.

Important: do **not** advance the simulation by a fixed amount per register read.
`ControlLoop::readStatus()` reads many registers in one cycle, so a per-call
fixed step would make the simulated device evolve too quickly. Compute elapsed
seconds from `last_update_` using `std::chrono::steady_clock`, update once based
on that delta, then store the new `last_update_`.

Use a mutex around register state because the control loop and web commands may run on different threads.

---

## 7. CLI Behavior

Add help text:

```text
-d, --demo          Run in demo mode with simulated Windmi device
```

Startup output should clearly indicate demo mode:

```text
[Main] DEMO MODE: using simulated Windmi device, no Modbus socket will be opened
```

Rules:
- `--demo` ignores `--ip` and `--port` for device communication.
- `--demo --selftest` should initially be rejected with a clear message:
  ```text
  --selftest is not supported in demo mode
  ```
  A simulator-specific selftest can be added later.
- Shutdown OFF write through a dedicated real client must be skipped in demo mode.

---

## 8. Web UI Behavior

No web UI changes should be required for the initial implementation.
The web server already talks to the control loop via queues and status snapshots.
If the simulator updates status values correctly, the UI should display changing values normally.

Optional later enhancement:
- Add a visual "DEMO MODE" banner in the web UI.
- Expose `/api/demo` endpoint with simulator state and controls.
- Add buttons to simulate errors, disconnects, low DHW temperature, defrost, etc.

---

## 9. Failure Simulation (Optional Phase 2)

After basic demo mode works, add optional controls:

```text
--demo-fail-connect       Simulate connect() failure
--demo-fail-read-rate N   Fail 1 in N reads
--demo-fail-write-rate N  Fail 1 in N writes
--demo-latency-ms N       Add artificial Modbus latency
```

This is useful for testing error handling and reconnect behavior.

Implementation idea:
```cpp
struct SimulatedModbusOptions {
    bool fail_connect = false;
    int fail_read_rate = 0;
    int fail_write_rate = 0;
    int latency_ms = 0;
    bool strict_registers = false;
};
```

---

## 10. Testing Strategy

### Unit Tests
Add tests under `tests/modbus/`:

- `SimulatedModbusClient.connect_disconnect`
- `SimulatedModbusClient.initial_register_values`
- `SimulatedModbusClient.write_then_read_target_register`
- `SimulatedModbusClient.off_mode_sets_running_status_off`
- `SimulatedModbusClient.throws_when_not_connected`

### Integration Smoke Test
Run:

```sh
./windmi-control --demo --web 8080 --static-dir static
```

Verify:
- Process starts without real gateway reachable.
- Web UI loads.
- `/api/status` returns simulated values.
- Setting DHW/heating/mode changes status within the next control loop cycle.
- Ctrl+C shuts down without attempting real Modbus OFF write.

---

## 11. Implementation Phases

### Phase 1: Interface Extraction
- Add `IModbusClient.hpp` and move/share `ModbusException` there.
- Make `ModbusClient` implement it.
- Change `ControlLoop` to depend on `IModbusClient*`.
- Confirm normal non-demo behavior still works.

This phase should be a separate commit and should be tested before adding the
simulator. It changes the type boundary but should not change runtime behavior.

### Phase 2: Basic Simulator
- Add `SimulatedModbusClient`.
- Implement register map, connect/disconnect, read/write, no-op flush.
- Add CMake entry.
- Add unit tests.

### Phase 3: CLI Integration
- Add `--demo` flag.
- Instantiate simulator in demo mode.
- Skip selftest and shutdown real OFF write in demo mode.
- Print clear startup message.

### Phase 4: Dynamic Behavior
- Add temperature drift/update simulation.
- Add power/current simulation.
- Verify web UI values change over time.

### Phase 5: Optional Failure Simulation
- Add failure/latency options only if needed.

---

## 12. Estimated Effort

- Phase 1: 1-2 hours
- Phase 2: 2-3 hours
- Phase 3: 1 hour
- Phase 4: 1-2 hours
- Tests: 1-2 hours
- Total: 6-10 hours

---

## 13. Migration Checklist

- [ ] Create `include/modbus/IModbusClient.hpp`
- [ ] Move/share `ModbusException` from `ModbusClient.hpp` into `IModbusClient.hpp`
- [ ] Update `include/modbus/ModbusClient.hpp` to inherit from `IModbusClient`
- [ ] Mark `ModbusClient` overrides with `override` in the header
- [ ] Update `include/core/ControlLoop.hpp` to use `IModbusClient*`
- [ ] Update `src/core/ControlLoop.cpp` to include/use `IModbusClient.hpp`
- [ ] Build and smoke-test normal mode after interface extraction, before adding simulator
- [ ] Create `include/modbus/SimulatedModbusClient.hpp`
- [ ] Create `src/modbus/SimulatedModbusClient.cpp`
- [ ] Update `src/modbus/CMakeLists.txt`
- [ ] Add `--demo` CLI flag in `src/main.cpp`
- [ ] Replace stack `ModbusClient` in `main.cpp` with `std::unique_ptr<IModbusClient>`
- [ ] Instantiate `SimulatedModbusClient` in demo mode
- [ ] Reject `--demo --selftest` with a clear error
- [ ] Preserve real-client `getCClient()` path for non-demo selftest
- [ ] Guard dedicated shutdown OFF write with `if (!demo_mode)`
- [ ] Add simulator unit tests
- [ ] Run normal mode smoke test
- [ ] Run demo mode smoke test

---

*Last updated: 2026-06-01*
