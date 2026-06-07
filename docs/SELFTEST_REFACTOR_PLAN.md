# Selftest Refactor: Eliminate getCClient() Dependency

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rewrite the selftest module in C++ using `IModbusClient*` so it works with all three transports (TCP, serial, demo), then remove the type-unsafe `getCClient()` escape hatch.

**Architecture:** Replace the C selftest (`selftest.c`/`selftest.h`) with C++ equivalents (`selftest.hpp`/`selftest.cpp`) that use the `IModbusClient` virtual interface instead of the raw C `modbus_client_t*`. This eliminates the need for `getCClient()` on both `ModbusClient` and `ModbusSerialClient`.

**Tech Stack:** C++17, Google Test, CMake

---

## File Structure

| Action | File | Responsibility |
|--------|------|----------------|
| Create | `include/selftest.hpp` | C++ header: `SelftestResult`, `SelftestReport`, `selftest_run(IModbusClient*)`, `selftest_print_report()` |
| Create | `src/selftest.cpp` | C++ implementation: 6-test selftest using `IModbusClient*` |
| Modify | `CMakeLists.txt` | Change `windmi_selftest` from `.c` to `.cpp`, add `include/` path |
| Modify | `src/main.cpp` | Replace `#include "selftest.h"` + `dynamic_cast`/`getCClient()` block with `IModbusClient*` call |
| Modify | `include/modbus/ModbusClient.hpp` | Remove `getCClient()` declaration (line 69) |
| Modify | `src/modbus/ModbusClient.cpp` | Remove `getCClient()` implementation (lines 95-99) |
| Modify | `include/modbus/ModbusSerialClient.hpp` | Remove `getCClient()` declaration (lines 73-76) |
| Modify | `src/modbus/ModbusSerialClient.cpp` | Remove `getCClient()` implementation (lines 138-144) |
| Modify | `tests/modbus/test_modbus_client.cpp` | Remove `GetCClient` test (lines 40-43) |
| Keep | `src/selftest.h` | Legacy C selftest header used by the legacy `Makefile` path |
| Keep | `src/selftest.c` | Legacy C selftest implementation used by the legacy `Makefile` path |

---

### Task 1: Create `include/selftest.hpp`

**Files:**
- Create: `include/selftest.hpp`

- [ ] **Step 1: Write the header file**

```cpp
/**
 * @file selftest.hpp
 * @brief Self-test for Windmi heat pump controller
 *
 * Runs a sequence of register read/write/verify tests against the
 * heat pump using any IModbusClient transport (TCP, serial, or demo).
 */

#ifndef WINDMI_SELFTEST_HPP
#define WINDMI_SELFTEST_HPP

#include <cstdint>
#include <string>

namespace windmi {

class IModbusClient;  // forward declaration

/// Result of a single self-test step
struct SelftestResult {
    std::string name;
    uint16_t address = 0;
    bool read_ok = false;
    bool write_ok = false;
    bool verify_ok = false;
    int16_t read_value = 0;
};

/// Aggregate report from a self-test run
struct SelftestReport {
    int total = 0;
    int passed = 0;
    int failed = 0;
    bool all_critical_passed = false;
    SelftestResult results[6];
};

/**
 * Run a self-test against the heat pump.
 *
 * @param client Connected IModbusClient (any transport)
 * @return Report with per-register results
 */
SelftestReport selftest_run(IModbusClient* client);

/**
 * Print self-test report to stdout.
 *
 * @param report Report to print
 */
void selftest_print_report(const SelftestReport& report);

} // namespace windmi

#endif // WINDMI_SELFTEST_HPP
```

- [ ] **Step 2: Commit**

```bash
git add include/selftest.hpp
git commit -m "feat: Add selftest.hpp header with IModbusClient-based API"
```

---

### Task 2: Create `src/selftest.cpp`

**Files:**
- Create: `src/selftest.cpp`

- [ ] **Step 1: Write the implementation**

Port all 6 tests from `src/selftest.c`. Each test replaces `if (modbus_read_register(client, ...) == 0)` with `try/catch` around `client->readRegister(...)`.

```cpp
/**
 * @file src/selftest.cpp
 * @brief Self-test implementation using IModbusClient interface
 */

#include "selftest.hpp"
#include "modbus/IModbusClient.hpp"
#include "config.h"
#include "utils/Logger.hpp"
#include "utils/LogTags.hpp"

#include <cstdio>

namespace windmi {

static inline int16_t temp_to_raw(float temp) {
    return static_cast<int16_t>(temp * 10.0f);
}

static inline float raw_to_temp(int16_t raw) {
    return raw / 10.0f;
}

SelftestReport selftest_run(IModbusClient* client) {
    SelftestReport report;
    // Zero-initialize the results array
    for (int i = 0; i < 6; i++) {
        report.results[i] = SelftestResult{};
    }

    report.total = 6;
    report.all_critical_passed = true;

    int test_idx = 0;

    // Test 1: Read device type (should be Rotenso Windmi 8kW = 8)
    {
        SelftestResult& r = report.results[test_idx++];
        r.name = "Device Type";
        r.address = REG_DEVICE_TYPE;
        r.read_ok = false;
        r.write_ok = true;
        r.verify_ok = true;

        try {
            int16_t device_type = client->readRegister(REG_DEVICE_TYPE);
            r.read_ok = true;
            r.read_value = device_type;
            WINDMI_LOG_DEBUG(LOG_TAG_SELFTEST, "Device type = %d", device_type);
            if (device_type != 8) {
                WINDMI_LOG_ERROR(LOG_TAG_SELFTEST,
                    "Unexpected device type: %d (expected 8 for Windmi 8kW)", device_type);
                report.all_critical_passed = false;
            }
        } catch (const ModbusException&) {
            WINDMI_LOG_ERROR(LOG_TAG_SELFTEST, "Failed to read device type");
            report.all_critical_passed = false;
        }
    }

    // Test 2: Read heating setpoint
    {
        SelftestResult& r = report.results[test_idx++];
        r.name = "Heating Setpoint";
        r.address = REG_HEATING_TARGET;
        r.read_ok = false;
        r.write_ok = true;
        r.verify_ok = true;

        try {
            int16_t heating_setpoint = client->readRegister(REG_HEATING_TARGET);
            r.read_ok = true;
            r.read_value = heating_setpoint;
            WINDMI_LOG_DEBUG(LOG_TAG_SELFTEST, "Heating setpoint = %.1f C",
                raw_to_temp(heating_setpoint));
        } catch (const ModbusException&) {
            WINDMI_LOG_ERROR(LOG_TAG_SELFTEST, "Failed to read heating setpoint");
            report.all_critical_passed = false;
        }
    }

    // Test 3: Read DHW setpoint
    {
        SelftestResult& r = report.results[test_idx++];
        r.name = "DHW Setpoint";
        r.address = REG_DHW_TARGET;
        r.read_ok = false;
        r.write_ok = true;
        r.verify_ok = true;

        try {
            int16_t dhw_setpoint = client->readRegister(REG_DHW_TARGET);
            r.read_ok = true;
            r.read_value = dhw_setpoint;
            WINDMI_LOG_DEBUG(LOG_TAG_SELFTEST, "DHW setpoint = %.1f C",
                raw_to_temp(dhw_setpoint));
        } catch (const ModbusException&) {
            WINDMI_LOG_ERROR(LOG_TAG_SELFTEST, "Failed to read DHW setpoint");
            report.all_critical_passed = false;
        }
    }

    // Test 4: Read outdoor temperature
    {
        SelftestResult& r = report.results[test_idx++];
        r.name = "Outdoor Temp";
        r.address = REG_OUTDOOR_TEMP;
        r.read_ok = false;
        r.write_ok = true;
        r.verify_ok = true;

        try {
            int16_t outdoor_temp = client->readRegister(REG_OUTDOOR_TEMP);
            r.read_ok = true;
            r.read_value = outdoor_temp;
            WINDMI_LOG_DEBUG(LOG_TAG_SELFTEST, "Outdoor temp = %.1f C",
                raw_to_temp(outdoor_temp));
        } catch (const ModbusException&) {
            WINDMI_LOG_ERROR(LOG_TAG_SELFTEST, "Failed to read outdoor temp");
            report.all_critical_passed = false;
        }
    }

    // Test 5: Read DHW temperature (tank temp)
    {
        SelftestResult& r = report.results[test_idx++];
        r.name = "DHW Tank Temp";
        r.address = REG_DHW_TANK_TEMP;
        r.read_ok = false;
        r.write_ok = true;
        r.verify_ok = true;

        try {
            int16_t dhw_temp = client->readRegister(REG_DHW_TANK_TEMP);
            r.read_ok = true;
            r.read_value = dhw_temp;
            WINDMI_LOG_DEBUG(LOG_TAG_SELFTEST, "DHW tank temp = %.1f C",
                raw_to_temp(dhw_temp));
        } catch (const ModbusException&) {
            WINDMI_LOG_ERROR(LOG_TAG_SELFTEST, "Failed to read DHW tank temp");
            report.all_critical_passed = false;
        }
    }

    // Test 6: Write-then-verify test for heating setpoint
    {
        SelftestResult& r = report.results[test_idx++];
        r.name = "Write Verify Test";
        r.address = REG_HEATING_TARGET;
        r.read_ok = true;
        r.write_ok = false;
        r.verify_ok = false;
        r.read_value = 0;

        // Save original heating setpoint
        int16_t original_heating = 0;
        bool read_original_ok = false;
        try {
            original_heating = client->readRegister(REG_HEATING_TARGET);
            read_original_ok = true;
        } catch (const ModbusException&) {
            WINDMI_LOG_ERROR(LOG_TAG_SELFTEST, "Failed to read original heating setpoint");
            report.all_critical_passed = false;
        }

        if (read_original_ok) {
            // Write test value (45 C)
            int16_t test_value = temp_to_raw(SELFTEST_DHW_TARGET_TEMP);
            try {
                client->writeRegister(REG_HEATING_TARGET, static_cast<uint16_t>(test_value));
                r.write_ok = true;

                // Verify by reading back
                try {
                    int16_t verify_value = client->readRegister(REG_HEATING_TARGET);
                    if (verify_value == test_value) {
                        r.verify_ok = true;
                        WINDMI_LOG_DEBUG(LOG_TAG_SELFTEST,
                            "Write verify passed (heating setpoint = %.1f C)",
                            raw_to_temp(verify_value));
                    } else {
                        WINDMI_LOG_ERROR(LOG_TAG_SELFTEST,
                            "Write verify failed (wrote %.1f, read %.1f)",
                            raw_to_temp(test_value), raw_to_temp(verify_value));
                        report.all_critical_passed = false;
                    }
                } catch (const ModbusException&) {
                    WINDMI_LOG_ERROR(LOG_TAG_SELFTEST, "Failed to verify heating setpoint");
                    report.all_critical_passed = false;
                }

                // Restore original heating setpoint
                try {
                    client->writeRegister(REG_HEATING_TARGET,
                        static_cast<uint16_t>(original_heating));
                    WINDMI_LOG_DEBUG(LOG_TAG_SELFTEST,
                        "Restored heating setpoint to %.1f C", raw_to_temp(original_heating));
                } catch (const ModbusException&) {
                    WINDMI_LOG_ERROR(LOG_TAG_SELFTEST,
                        "Failed to restore original heating setpoint");
                    report.all_critical_passed = false;
                }
            } catch (const ModbusException&) {
                WINDMI_LOG_ERROR(LOG_TAG_SELFTEST, "Failed to write heating setpoint");
                report.all_critical_passed = false;
            }
        }
    }

    // Count passed tests
    for (int i = 0; i < report.total; i++) {
        SelftestResult& r = report.results[i];
        if (r.read_ok && r.write_ok && r.verify_ok) {
            report.passed++;
        } else {
            report.failed++;
        }
    }

    return report;
}

void selftest_print_report(const SelftestReport& report) {
    printf("\n========== Self-Test Report ==========\n");
    printf("\nTest Results:\n");
    printf("%-20s  %8s  %6s  %6s  %6s  %10s\n",
           "Test", "Address", "Read", "Write", "Verify", "Value");
    printf("----------------------------------------------------------------\n");

    for (int i = 0; i < report.total; i++) {
        const SelftestResult& r = report.results[i];
        printf("%-20s  0x%04X  %6s  %6s  %6s  %10d\n",
               r.name.c_str(),
               r.address,
               r.read_ok ? "OK" : "FAIL",
               r.write_ok ? "OK" : "FAIL",
               r.verify_ok ? "OK" : "FAIL",
               r.read_value);
    }

    printf("----------------------------------------------------------------\n");
    printf("\nSummary:\n");
    printf("  Total tests: %d\n", report.total);
    printf("  Passed: %d\n", report.passed);
    printf("  Failed: %d\n", report.failed);
    printf("  All critical passed: %s\n", report.all_critical_passed ? "YES" : "NO");
    printf("\n========================================\n");
}

} // namespace windmi
```

- [ ] **Step 2: Compile the new file directly to verify it is self-contained**

`selftest.cpp` is not part of the CMake build until Task 3, so use a direct compile check here:

```bash
cd /home/gbo/develop/wpomp
c++ -std=c++17 -Wall -Wextra -Werror -Iinclude -c src/selftest.cpp -o /tmp/selftest.o
```

Expected: Command exits with code 0.

- [ ] **Step 3: Commit**

```bash
git add src/selftest.cpp
git commit -m "feat: Add selftest.cpp implementation using IModbusClient interface"
```

---

### Task 3: Update `CMakeLists.txt`

**Files:**
- Modify: `CMakeLists.txt` (lines 59-67)

- [ ] **Step 1: Update the selftest library to use the C++ source**

Change line 60-61 from:
```cmake
add_library(windmi_selftest STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/selftest.c
)
```
To:
```cmake
add_library(windmi_selftest STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/selftest.cpp
)
```

Change line 64-65 from:
```cmake
target_include_directories(windmi_selftest PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
```
To:
```cmake
target_include_directories(windmi_selftest PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
```

The `src` include was needed for `selftest.h`; the `include` directory already has `config.h` and all `modbus/` headers. After moving the header to `include/selftest.hpp`, only `include/` is needed.

- [ ] **Step 2: Build to verify**

```bash
cmake --build build -j4 2>&1 | tail -5
```

Expected: Build succeeds with the new source file.

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: Switch selftest from C to C++ source in CMakeLists"
```

---

### Task 4: Update `src/main.cpp` to use new selftest API

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Replace the C include with C++ include**

Change line 38 from:
```cpp
#include "selftest.h"
```
To:
```cpp
#include "selftest.hpp"
```

- [ ] **Step 2: Replace the selftest block (lines ~404-425)**

This is the critical change. The entire `if (run_selftest)` block currently does:
1. `dynamic_cast<ModbusClient*>` (TCP-only)
2. `getCClient()` to extract C struct
3. Call C `selftest_run(modbus_client_t*)`
4. Call C `selftest_print_report()`

Replace the entire block from `if (run_selftest) {` through the closing `}` before the connect section. The new block uses `IModbusClient*` directly:

```cpp
    if (run_selftest) {
        // Works with ANY IModbusClient transport — TCP, serial, or demo
        windmi::SelftestReport report = windmi::selftest_run(modbus_client.get());
        windmi::selftest_print_report(report);
        WINDMI_LOG_INFO(LOG_TAG_SELFTEST, "%d/%d registers passed", report.passed, report.total);
        if (report.all_critical_passed) {
            WINDMI_LOG_INFO(LOG_TAG_SELFTEST, "Self-test PASSED");
        } else {
            WINDMI_LOG_WARN(LOG_TAG_SELFTEST, "Self-test FAILED");
        }
        release_lock();
        return report.all_critical_passed ? 0 : 1;
    }
```

Note: The `modbus_client` is already connected at this point (connect happens below this block in the original code). **But wait** — in the current code the selftest runs *before* the main `connect()`. The selftest connects a *dedicated* TCP client. Let me verify:

Looking at current `main.cpp` lines 404-425, the selftest:
1. Creates a *new* `ModbusClient` via `dynamic_cast` — no, it `dynamic_cast`s the existing `modbus_client.get()`
2. Calls `real_client->connect()` — connects it
3. Calls `selftest_run(c_client)` — runs tests
4. Calls `real_client->disconnect()` — disconnects it
5. Returns

The main `modbus_client->connect()` happens at line ~430, *after* the selftest block. So selftest does its own connect/disconnect.

For the new version, we need to keep this pattern: selftest connects, runs, disconnects, then returns. But we use the same `modbus_client` that was already created earlier (TCP, serial, or demo).

Update: The connect+disconnect should happen via the `IModbusClient` interface:

```cpp
    if (run_selftest) {
        if (!modbus_client->connect()) {
            WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Failed to connect for self-test");
            release_lock();
            return 1;
        }

        windmi::SelftestReport report = windmi::selftest_run(modbus_client.get());
        windmi::selftest_print_report(report);
        WINDMI_LOG_INFO(LOG_TAG_SELFTEST, "%d/%d registers passed", report.passed, report.total);
        if (report.all_critical_passed) {
            WINDMI_LOG_INFO(LOG_TAG_SELFTEST, "Self-test PASSED");
        } else {
            WINDMI_LOG_WARN(LOG_TAG_SELFTEST, "Self-test FAILED");
        }

        modbus_client->disconnect();
        release_lock();
        return report.all_critical_passed ? 0 : 1;
    }
```

- [ ] **Step 3: Remove serial/demo selftest blocks**

Remove these two validation blocks since selftest now works with all transports:

```cpp
        if (run_selftest) {
            WINDMI_LOG_ERROR(LOG_TAG_MAIN, "--selftest is not supported in serial mode");
            release_lock();
            return 1;
        }
```

And:

```cpp
        if (run_selftest) {
            WINDMI_LOG_ERROR(LOG_TAG_MAIN, "--selftest is not supported in demo mode");
            release_lock();
            return 1;
        }
```

- [ ] **Step 4: Build and verify**

```bash
cmake --build build -j4 2>&1 | tail -5
```

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "refactor: Use IModbusClient in selftest, remove transport-specific blocks"
```

---

### Task 5: Remove `getCClient()` from wrapper classes

**Files:**
- Modify: `include/modbus/ModbusClient.hpp`
- Modify: `src/modbus/ModbusClient.cpp`
- Modify: `include/modbus/ModbusSerialClient.hpp`
- Modify: `src/modbus/ModbusSerialClient.cpp`

- [ ] **Step 1: Remove from ModbusClient**

In `include/modbus/ModbusClient.hpp`, delete lines 69-70:
```cpp
    void* getCClient() const;
```

In `src/modbus/ModbusClient.cpp`, delete lines 96-99:
```cpp
void* ModbusClient::getCClient() const {
    return impl_ ? impl_->client : nullptr;
}
```

- [ ] **Step 2: Remove from ModbusSerialClient**

In `include/modbus/ModbusSerialClient.hpp`, delete lines 72-76:
```cpp
    /**
     * @brief Get underlying C client pointer (for selftest)
     * @return Pointer to C modbus_serial_client structure
     */
    void* getCClient() const;
```

In `src/modbus/ModbusSerialClient.cpp`, delete lines 138-144:
```cpp
void* ModbusSerialClient::getCClient() const {
    if (impl_ && impl_->c_client) {
        return impl_->c_client;
    }
    return nullptr;
}
```

- [ ] **Step 3: Remove GetCClient test from `tests/modbus/test_modbus_client.cpp`**

Delete lines 40-43:
```cpp
TEST(ModbusClientTest, GetCClient) {
    ModbusClient client(MODBUS_GATEWAY_IP, MODBUS_GATEWAY_PORT, MODBUS_SLAVE_ID);
    void* c_client = client.getCClient();
    // Should return non-null even when not connected (client struct exists)
    EXPECT_NE(c_client, nullptr);
}
```

- [ ] **Step 4: Build and verify**

```bash
cmake --build build -j4 2>&1 | tail -5
cd build && ctest --output-on-failure
```

Expected: All tests pass. No compilation errors from missing `getCClient()`.

- [ ] **Step 5: Commit**

```bash
git add include/modbus/ModbusClient.hpp src/modbus/ModbusClient.cpp
git add include/modbus/ModbusSerialClient.hpp src/modbus/ModbusSerialClient.cpp
git add tests/modbus/test_modbus_client.cpp
git commit -m "refactor: Remove getCClient() from ModbusClient and ModbusSerialClient"
```

---

### Task 6: Preserve legacy C selftest path

**Files:**
- Keep: `src/selftest.h`
- Keep: `src/selftest.c`
- Keep: `src/main.c`
- Keep: `src/control_loop.c`
- Keep: `src/control_loop.h`

The repository still advertises the legacy `Makefile` path in `README.md`, and `Makefile` currently compiles `src/main.c`, `src/control_loop.c`, and `src/selftest.c`. Do **not** delete those files as part of this refactor. This task verifies that the CMake/C++ path no longer depends on `getCClient()`, while the legacy C path remains available for follow-up cleanup if desired.

- [ ] **Step 1: Verify CMake uses the new C++ selftest**

```bash
grep -n 'selftest' CMakeLists.txt
```

Expected: Shows `src/selftest.cpp` in the `windmi_selftest` target and does not show `src/selftest.c`.

- [ ] **Step 2: Verify legacy Makefile still references C files**

```bash
grep -n 'main\.c\|control_loop\.c\|selftest\.c' Makefile
```

Expected: Shows the legacy C sources. This is intentional for now.

- [ ] **Step 3: Verify no C++ code includes the old C selftest header**

```bash
grep -rn '#include.*"selftest\.h"' src include tests --include='*.cpp' --include='*.hpp'
```

Expected: No matches.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "chore: Preserve legacy C selftest while CMake uses C++ selftest"
```

---

### Task 7: Verify end-to-end

- [ ] **Step 1: Full clean build with warnings-as-errors**

```bash
cd /home/gbo/develop/wpomp
rm -rf build
cmake -S . -B build -DWINDMI_BUILD_TESTS=ON \
    -DCMAKE_C_FLAGS="-Wall -Wextra -Werror" \
    -DCMAKE_CXX_FLAGS="-Wall -Wextra -Werror"
cmake --build build -j4
cd build && ctest --output-on-failure
```

Expected: Clean build, all tests pass.

- [ ] **Step 2: Verify `--selftest` CLI still works with demo mode**

```bash
timeout 5 ./build/windmi-control --demo --selftest 2>&1 | tail -20
```

Expected: Selftest runs with simulated device, reports results, exits.

- [ ] **Step 3: Verify `--selftest --serial` is no longer blocked**

```bash
timeout 5 ./build/windmi-control --serial /dev/ttyUSB0 --selftest 2>&1 | grep -i "selftest\|serial\|error\|connect"
```

Expected: Selftest attempts to connect to serial device (fails without hardware, but is not blocked at CLI level).

- [ ] **Step 4: Verify `getCClient()` no longer exists**

```bash
grep -rn 'getCClient' src/ include/ tests/
```

Expected: No matches.

- [ ] **Step 5: Verify legacy C selftest files still exist for Makefile compatibility**

```bash
ls src/selftest.h src/selftest.c
```

Expected: Both files exist.

- [ ] **Step 6: Verify C++ path uses the new header only**

```bash
grep -rn '#include.*"selftest\.h"' src include tests --include='*.cpp' --include='*.hpp'
```

Expected: No matches.

- [ ] **Step 7: Final commit**

```bash
git add -A
git commit -m "feat: Selftest refactor complete — IModbusClient interface, no getCClient()"
```

---

## Self-Review Checklist

- [x] **Spec coverage**: Every requirement from the original plan is implemented
- [x] **Placeholder scan**: No TBD, TODO, or "implement later" — all code is complete
- [x] **Type consistency**: `SelftestReport` and `SelftestResult` in `selftest.hpp` match usage in `selftest.cpp` and `main.cpp`
- [x] **Legacy compatibility**: `selftest.h`, `selftest.c`, `src/main.c`, and `src/control_loop.c/h` are preserved for the legacy Makefile path
- [x] **No C++ orphan references**: active C++ files include `selftest.hpp`, not legacy `selftest.h`
- [x] **CMake consistency**: `windmi_selftest` source updated from `.c` to `.cpp` in Task 3
- [x] **Connect/disconnect**: Selftest manages its own connect/disconnect in Task 4, matching original behavior
- [x] **Demo mode**: Selftest works with `SimulatedModbusClient` (throws `ModbusException` on unknown registers — same as existing behavior)