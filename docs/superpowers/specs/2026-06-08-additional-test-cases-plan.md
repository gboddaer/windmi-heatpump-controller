# Additional Test Cases Plan

**Date:** 2026-06-08
**Branch:** `feature/test-coverage-gap-fill`
**Status:** Planning phase

## Goal
Increase code coverage from ~60-65% to ~90% by writing targeted test cases for:
- Platform.cpp (15-20 tests)
- WebServer.cpp (15-20 tests)
- main.cpp (5-10 tests)
- selftest (5 tests)
- modbus_client.c (5 tests)

---

## 1. Platform.cpp Tests (15-20 tests)

### Current Coverage: ~6 tests
### Target: ~15-20 tests

#### Thread Tests (5-7 tests)
```
TEST(ThreadTest, CreateWithCallable) - Verify thread starts and executes
TEST(ThreadTest, JoinAfterStart) - Verify join blocks until completion
TEST(ThreadTest, DetachMakesNotJoinable) - Verify detach transitions state
TEST(ThreadTest, MultipleThreadsConcurrent) - Verify concurrent execution
TEST(ThreadTest, ThreadExceptionHandler) - Verify exception handling in thread
TEST(ThreadTest, MoveSemanticsPreservesExecution) - Verify move doesn't lose task
TEST(ThreadTest, JoinableTransitionsCorrectly) - Verify joinable state machine
```

#### Mutex Tests (3-4 tests)
```
TEST(MutexTest, RecursiveLockDeadlock) - Verify deadlock on recursive lock
TEST(MutexTest, LockGuardRAII) - Verify RAII unlock on scope exit
TEST(MutexTest, ConcurrentCounterIncrement) - Verify atomic increment with lock
TEST(MutexTest, TryLockBehavior) - Verify try_lock returns false when locked
```

#### ConditionVariable Tests (3-4 tests)
```
TEST(ConditionVariableTest, WaitTimeoutReturnsFalse) - Verify timeout handling
TEST(ConditionVariableTest, SpuriousWakeupHandled) - Verify predicate loop handles spurious wakeups
TEST(ConditionVariableTest, NotifyAllWakesAllWaiters) - Verify all threads notified
TEST(ConditionVariableTest, WaitWithPredicate) - Verify wait(pred) pattern
```

#### Signal Handler Tests (2-3 tests)
```
TEST(SignalHandlerTest, InstallInstallsHandler) - Verify handler is registered
TEST(SignalHandlerTest, HandlerSetsRunningFlag) - Verify signal sets flag to 0
TEST(SignalHandlerTest, MultipleSignalTypes) - Verify multiple signals handled
```

#### Instance Lock Tests (2 tests)
```
TEST(InstanceLockTest, ForceAcquireWhenLocked) - Verify force bypasses lock
TEST(InstanceLockTest, StandardAcquireWhenFree) - Verify normal acquisition works
```

---

## 2. WebServer.cpp Tests (15-20 tests)

### Current Coverage: ~6 tests
### Target: ~15-20 tests

#### Static File Serving Tests (4-5 tests)
```
TEST(StaticFileTest, ServeExistingFile) - Verify file content returned
TEST(StaticFileTest, Serve404ForMissing) - Verify 404 for non-existent file
TEST(StaticFileTest, PreventDirectoryTraversal) - Verify ../ is blocked
TEST(StaticFileTest, ServeFileWithCorrectContentType) - Verify MIME types
TEST(StaticFileTest, ServeNestedDirectoryFile) - Verify subdirectory serving
```

#### API Handler Tests (5-6 tests)
```
TEST(ApiStatusHandlerTest, GeneratesValidJson) - Verify JSON structure
TEST(ApiStatusHandlerTest, AllRequiredFieldsPresent) - Verify all fields
TEST(ApiStatusHandlerTest, HandlesNullQueue) - Verify graceful handling
TEST(ApiSetDhwHandlerTest, ValidatesTemperatureRange) - Verify 40-63°C
TEST(ApiSetDhwHandlerTest, RejectsInvalidJson) - Verify error response
TEST(ApiSetHeatingHandlerTest, TemperatureValidation) - Verify 25-63°C
```

#### Error Path Tests (3-4 tests)
```
TEST(ErrorHandlingTest, InvalidPortBinding) - Verify error handling
TEST(ErrorHandlingTest, MalformedJsonRequest) - Verify 400 response
TEST(ErrorHandlingTest, MissingRequiredField) - Verify validation
TEST(ErrorHandlingTest, ConnectionDrain) - Verify connection cleanup
```

#### Concurrent Request Tests (2-3 tests)
```
TEST(ConcurrentTest, MultipleSimultaneousRequests) - Verify thread safety
TEST(ConcurrentTest, RapidSequentialRequests) - Verify no race conditions
TEST(ConcurrentTest, StatusReadDuringWrite) - Verify consistency
```

---

## 3. main.cpp Tests (5-10 tests)

### Current Coverage: 0 tests
### Target: ~5-10 tests

#### Argument Parsing Tests (4-5 tests)
```
TEST(ArgsTest, ParsePort) - Verify --port flag
TEST(ArgsTest, ParseHost) - Verify --host flag
TEST(ArgsTest, ParseDemoMode) - Verify --demo flag
TEST(ArgsTest, ParseSlaveId) - Verify --slave-id flag
TEST(ArgsTest, ParseSerialPort) - Verify --serial flag
TEST(ArgsTest, ParseLogVerbosity) - Verify --verbose flag
```

#### Error Handling Tests (2-3 tests)
```
TEST(ErrorTest, InvalidPortNumber) - Verify error message
TEST(ErrorTest, InvalidHostAddress) - Verify DNS/valid IP validation
TEST(ErrorTest, InvalidSlaveId) - Verify range validation (1-247)
```

#### Signal Handler Integration (1-2 tests)
```
TEST(SignalTest, CtrlCSetsFlag) - Verify signal handler integration
TEST(SignalTest, SIGTERMGracefulShutdown) - Verify graceful shutdown
```

---

## 4. selftest Tests (5 tests)

### Current Coverage: 0 tests
### Target: ~5 tests

#### Selftest Execution Tests (3 tests)
```
TEST(SelftestTest, RunPassesValidClient) - Verify execution with mock
TEST(SelftestTest, ReportContainsAllTests) - Verify report structure
TEST(SelftestTest, ReportHasPassFailCounts) - Verify statistics
```

#### Register Verification Tests (2 tests)
```
TEST(SelftestTest, CriticalRegisterRead) - Verify critical register reads
TEST(SelftestTest, NonCriticalRegisterRead) - Verify non-critical register reads
```

---

## 5. modbus_client.c Tests (5 tests)

### Current Coverage: 0 direct tests
### Target: ~5 tests

#### Connection Tests (2 tests)
```
TEST(ModbusClientCTest, ConnectSuccess) - Verify successful connection
TEST(ModbusClientCTest, ConnectFailureTimeout) - Verify connection timeout
```

#### CRC Tests (2 tests)
```
TEST(CRCTest, ValidCRC16Computation) - Verify CRC calculation
TEST(CRCTest, InvalidCRCRejected) - Verify CRC validation
```

#### Read/Write Tests (1 test)
```
TEST(ModbusReadWriteTest, ReadRegisterRoundtrip) - Verify read/write cycle
```

---

## Implementation Strategy

### Phase 1: High-impact, low-effort (Week 1)
1. modbus_client.c tests (5 tests) - 2-3 hours
2. Selftest tests (5 tests) - 2-3 hours
3. Argument parsing tests (3 tests) - 1 hour

**Expected coverage increase:** +5-7%

### Phase 2: Medium-impact, medium-effort (Week 2)
1. WebServer static file tests (4 tests) - 2-3 hours
2. WebServer error path tests (3 tests) - 2 hours
3. Platform thread tests (3 tests) - 2-3 hours
4. Platform condition variable tests (2 tests) - 2 hours

**Expected coverage increase:** +8-10%

### Phase 3: High-effort, high-impact (Week 3)
1. Platform mutex tests (4 tests) - 3 hours
2. Platform signal handler tests (3 tests) - 2 hours
3. WebServer concurrent tests (3 tests) - 3 hours
4. main.cpp signal integration (2 tests) - 2 hours

**Expected coverage increase:** +5-7%

---

## Notes

- Use GoogleTest fixtures where appropriate
- Use mock IModbusClient for WebServer tests
- Use simulated Modbus client for most tests
- Platform tests may need to be platform-specific (Windows/POSIX)
- Consider adding a coverage target with `-fprofile-arcs -ftest-coverage`
