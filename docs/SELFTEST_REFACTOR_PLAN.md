# Selftest Refactor Plan: Eliminate getCClient() Dependency

## Problem

The selftest module (`src/selftest.c` / `src/selftest.h`) is written in pure C and
calls the C API directly (`modbus_read_register`, `modbus_write_register`). This
forces `main.cpp` to extract the underlying C struct from the C++ wrapper via
`getCClient()` — a `void*` escape hatch that returns different struct types depending
on the transport:

| Transport | `getCClient()` returns | C API called |
|-----------|----------------------|--------------|
| TCP       | `modbus_client_t*`   | `modbus_read_register()` |
| Serial    | `modbus_serial_client_t*` | `modbus_serial_read_register()` |
| Demo      | N/A (no C struct)    | N/A |

`getCClient()` is **not** on the `IModbusClient` interface. It only exists on the
concrete `ModbusClient` and `ModbusSerialClient` classes. Today the selftest is
blocked for serial and demo modes (`--selftest` requires TCP), which is the only
reason it doesn't explode — a `modbus_serial_client_t*` would be cast to
`modbus_client_t*` and dereference completely wrong struct offsets.

## Goal

Rewrite the selftest module in C++ using `IModbusClient*`, so it works with **all
three transports** (TCP, serial, demo) and the `getCClient()` method can be removed
from both wrapper classes.

## Scope of Changes

### Files to Create

| File | Description |
|------|-------------|
| `include/selftest.hpp` | C++ header — replaces `src/selftest.h` |
| `src/selftest.cpp` | C++ implementation — replaces `src/selftest.c` |

### Files to Modify

| File | Change |
|------|--------|
| `src/main.cpp` | Remove `dynamic_cast<ModbusClient*>` + `getCClient()` hack; pass `IModbusClient*` directly to selftest; remove `--selftest` block for serial/demo |
| `CMakeLists.txt` | Change `windmi_selftest` from `src/selftest.c` to `src/selftest.cpp`; update include dirs |
| `include/modbus/ModbusClient.hpp` | Remove `getCClient()` declaration |
| `src/modbus/ModbusClient.cpp` | Remove `getCClient()` implementation |
| `include/modbus/ModbusSerialClient.hpp` | Remove `getCClient()` declaration |
| `src/modbus/ModbusSerialClient.cpp` | Remove `getCClient()` implementation |
| `tests/modbus/test_modbus_client.cpp` | Remove `GetCClient` test case |

### Files to Delete

| File | Reason |
|------|--------|
| `src/selftest.h` | Replaced by `include/selftest.hpp` |
| `src/selftest.c` | Replaced by `src/selftest.cpp` |

### Files Unchanged

- `src/modbus/modbus_client.c` — C API stays, just not called by selftest anymore
- `src/modbus/modbus_serial_client.c` — C API stays, same
- `src/modbus_client.c` — TCP C API stays (used internally by `ModbusClient`)
- `include/modbus_client.h` — stays (still used by `ModbusClient` impl)
- `include/modbus/modbus_serial_client.h` — stays (still used by `ModbusSerialClient` impl)
- All other files — no changes

## New API Design

### `include/selftest.hpp`

```cpp
#pragma once

#include <cstdint>
#include <string>

namespace windmi {

class IModbusClient;  // forward declaration

struct SelftestResult {
    std::string name;
    uint16_t address;
    bool read_ok = false;
    bool write_ok = false;    // true for read-only tests (NOP)
    bool verify_ok = false;    // true for read-only tests (NOP)
    int16_t read_value = 0;
};

struct SelftestReport {
    int total = 0;
    int passed = 0;
    int failed = 0;
    bool all_critical_passed = false;
    SelftestResult results[6];
};

// Run self-test against the heat pump using any IModbusClient transport.
SelftestReport selftest_run(IModbusClient* client);

// Print self-test report to stdout
void selftest_print_report(const SelftestReport& report);

} // namespace windmi
```

Key differences from the C version:
- Uses `IModbusClient*` instead of `modbus_client_t*`
- Struct names use C++ conventions (`SelftestResult`, `SelftestReport`)
- `name` field is `std::string` instead of `const char*`
- `selftest_run` throws `ModbusException` on I/O errors (no return code checking)
- `selftest_print_report` takes `const SelftestReport&` instead of `const selftest_report_t*`

### `src/selftest.cpp` — Implementation Sketch

The logic is identical to the C version, but uses the C++ interface:

```cpp
int16_t device_type = client->readRegister(REG_DEVICE_TYPE);   // throws on error
int16_t heating = client->readRegister(REG_HEATING_TARGET);    // throws on error
client->writeRegister(REG_HEATING_TARGET, test_value);          // throws on error
```

Error handling changes from:
```c
if (modbus_read_register(client, REG_DEVICE_TYPE, &device_type) == 0) {
    // success
} else {
    // failure
}
```

To:
```cpp
try {
    int16_t device_type = client->readRegister(REG_DEVICE_TYPE);
    result.read_ok = true;
    result.read_value = device_type;
} catch (const ModbusException&) {
    result.read_ok = false;
}
```

### `src/main.cpp` — Selftest Section (After)

```cpp
#include "selftest.hpp"

// ...

if (run_selftest) {
    // Works with ANY IModbusClient — TCP, serial, or demo
    windmi::SelftestReport report = windmi::selftest_run(modbus_client.get());
    windmi::selftest_print_report(report);
    // ...
}
```

No more `dynamic_cast`, no more `getCClient()`, no more mode-specific blocks.

## Steps

### Step 1: Create `include/selftest.hpp`

- Define `windmi::SelftestResult` and `windmi::SelftestReport` structs
- Declare `selftest_run(IModbusClient*)` and `selftest_print_report()`
- Use `std::string` for result names
- Keep 6-test layout identical to C version

### Step 2: Create `src/selftest.cpp`

- Port all 6 tests from `src/selftest.c`
- Replace `modbus_read_register(client, addr, &val) == 0` with `try/catch` around `client->readRegister(addr)`
- Replace `modbus_write_register(client, addr, val) == 0` with `try/catch` around `client->writeRegister(addr, val)`
- Replace `WINDMI_C_LOG` with `WINDMI_LOG_DEBUG`/`WINDMI_LOG_ERROR`
- Replace `printf` in `selftest_print_report` with same table format (printf is fine for tabular output)
- Keep `temp_to_raw()` / `raw_to_temp()` helpers as static inline functions

### Step 3: Update `CMakeLists.txt`

- Change `windmi_selftest` source from `src/selftest.c` to `src/selftest.cpp`
- Remove `src` from include dirs (was needed for `selftest.h`), add `include` (already there)
- Link `windmi_selftest` against `windmi_modbus` and `windmi_utils` (already done)

### Step 4: Update `src/main.cpp`

- Replace `#include "selftest.h"` with `#include "selftest.hpp"`
- Remove `dynamic_cast<ModbusClient*>` and `getCClient()` usage
- Pass `modbus_client.get()` (the `IModbusClient*`) directly to `selftest_run()`
- Remove `--selftest` blocking for serial and demo modes
- Remove `#include "modbus/ModbusClient.hpp"` from selftest path (still needed for shutdown client)
- `selftest_report_t` → `windmi::SelftestReport`, field rename `.all_critical_passed` → same name

### Step 5: Remove `getCClient()` from wrappers

- Remove `void* getCClient() const;` from `ModbusClient.hpp`
- Remove `void* getCClient() const;` from `ModbusSerialClient.hpp`
- Remove implementations from `ModbusClient.cpp` and `ModbusSerialClient.cpp`
- Remove `GetCClient` test from `tests/modbus/test_modbus_client.cpp`

### Step 6: Delete old C files

- Delete `src/selftest.c`
- Delete `src/selftest.h`

### Step 7: Build and test

- Verify clean build with `-Wall -Wextra -Werror`
- Run all unit tests
- Verify `--selftest` works with `--demo`, `--serial`, and default TCP modes

## Risk Assessment

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| Exception handling changes selftest behavior | Low — `try/catch` is equivalent to return-code checking | Compare logs carefully |
| `std::string` in struct breaks printf-style table | None — format strings unchanged, only `.c_str()` needed for `%s` | Use `r.name.c_str()` |
| Demo `SimulatedModbusClient` doesn't implement all registers | None — it already does (checked) | N/A |
| Serial selftest requires real hardware | Expected — `--selftest` with `--serial` will fail at `connect()` if no device | Same as TCP without gateway |

## Estimated Effort

| Task | Lines changed | Time |
|------|--------------|------|
| Create `selftest.hpp` | ~40 new | 10 min |
| Create `selftest.cpp` | ~200 new (1:1 port from C) | 20 min |
| Update `CMakeLists.txt` | ~3 changed | 5 min |
| Update `main.cpp` | ~15 changed (remove 10 lines, add 3) | 10 min |
| Remove `getCClient()` from 4 files | ~15 deleted | 5 min |
| Remove old C files | ~200 deleted | 2 min |
| Remove test case | ~5 deleted | 2 min |
| Build, test, verify | 0 | 10 min |
| **Total** | **~460 lines touched** | **~65 min** |

## Test Plan

1. `--selftest` with TCP mode (if gateway available): same behavior as before
2. `--selftest --demo`: should now work (was previously blocked)
3. `--selftest --serial /dev/ttyUSB0`: should now attempt to connect (was previously blocked)
4. All existing unit tests still pass
5. `getCClient()` compilation error confirms it's removed (no code uses it)