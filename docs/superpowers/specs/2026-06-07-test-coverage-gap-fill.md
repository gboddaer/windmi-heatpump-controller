# Test Coverage Gap Fill - Implementation Plan

**Date:** 2026-06-07
**Target:** 90%+ code coverage
**Status:** In Progress

## Current Coverage Estimate: ~55-65%

### High-Priority Gaps (Must-Have)

#### 1. Platform.cpp (1109 lines) — Target: +15 tests
**Current:** 6 tests
**Missing:**
- Thread join/detach functionality (positive and negative paths)
- ConditionVariable wait_for edge cases (timeout, spurious wakeups)
- Serial port open/close/read/write (error handling)
- Instance locking (force parameter, error paths)
- Signal handler installation and un-installation

#### 2. WebServer.cpp (447 lines) — Target: +15 tests
**Current:** 6 tests
**Missing:**
- Static file serving (valid file, 404, directory traversal prevention)
- JSON status response validation (all fields, edge values)
- API error handling (invalid JSON, missing fields, type mismatches)
- Concurrent request handling
- URL parameter parsing edge cases

#### 3. ControlLoop.cpp edge cases — Target: +10 tests
**Current:** 30 tests (good overall)
**Missing:**
- Error paths (Modbus read failures, partial reads)
- Timeout handling
- Invalid register values (NaN, overflow)
- COP calculation edge cases (division by zero)

#### 4. main.cpp integration tests — Target: +5 tests
**Current:** 0 tests
**Missing:**
- Argument parsing (all flags, invalid values, defaults)
- Signal handling (Ctrl+C, SIGTERM)
- Demo mode startup
- Error handling (port binding failure, Modbus connect failure)

#### 5. selftest.c + selftest.cpp — Target: +5 tests
**Current:** 0 tests
**Missing:**
- Self-test execution (success case)
- Register verification (read/write/verify)
- Critical vs. non-critical register handling

#### 6. modbus_client.c — Target: +5 tests
**Current:** 0 direct tests (indirect only)
**Missing:**
- Connection error paths (timeout, refused, DNS failure)
- CRC calculation and validation
- Socket cleanup on error

#### 7. JsonHelpers.cpp — Target: +3 tests
**Current:** 4 tests
**Missing:**
- Escape character handling
- Nested object serialization
- Buffer overflow prevention

#### 8. LoggerC.cpp — Target: +2 tests
**Current:** 0 tests
**Missing:**
- C API wrapper functions (WINDMI_C_LOG)

### Medium-Priority Gaps (Nice-to-Have)

#### 9. SpscQueue.cpp — Target: +3 tests
**Current:** 0 dedicated (tested indirectly via CmdQueue)
**Missing:**
- C version tests (spsc_queue.h)
- Edge cases (single item, max capacity, wrap-around)

#### 10. SimulatedModbusClient.cpp — Target: +2 tests
**Current:** 0 direct tests
**Missing:**
- Ambient temperature drift simulation
- Connection state transitions

### Implementation Approach

**Phase 1: Core Platform Tests (Highest Priority)**
- test_platform_thread.cpp — Thread join/detach, ConditionVariable
- test_platform_serial.cpp — Serial port open/close/read/write
- test_platform_lock.cpp — Instance locking edge cases

**Phase 2: WebServer Tests**
- test_web_server_static.cpp — Static file serving
- test_web_server_json.cpp — JSON response validation
- test_web_server_api.cpp — API error handling
- test_web_server_concurrent.cpp — Concurrent requests

**Phase 3: ControlLoop Edge Cases**
- test_control_loop_errors.cpp — Modbus errors, timeouts
- test_control_loop_cop.cpp — COP calculation edge cases

**Phase 4: Integration Tests**
- test_main_integration.cpp — main.cpp argument parsing, signals
- test_selftest_integration.cpp — Self-test execution

**Phase 5: Wrapper Tests**
- test_modbus_client_c.cpp — C API wrapper
- test_json_helpers_edge.cpp — Escape, nesting, overflow
- test_logger_c_api.cpp — C API

**Phase 6: C Library Tests**
- test_spsc_queue_c.cpp — C version of SpscQueue
- test_simulated_modbus.cpp — Simulated client

### Test File Naming Convention
- `test_<module>_<feature>.cpp` (e.g., `test_platform_thread.cpp`)
- Test cases use `TEST_F` for fixtures, `TEST` for standalone

### Coverage Targets
- Platform.cpp: 90%
- WebServer.cpp: 85%
- ControlLoop.cpp: 95%
- main.cpp: 70%
- selftest: 80%
- modbus_client.c: 80%
- JsonHelpers.cpp: 90%
- LoggerC.cpp: 80%
