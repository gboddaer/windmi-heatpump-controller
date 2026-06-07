# Windows Platform Support Investigation Plan

> **For agentic workers:** Use superpowers:subagent-driven-development to implement this plan task-by-task.

**Goal:** Investigate and document the current state of Windows platform support for the Windmi Heat Pump Controller, including build requirements, dependencies, and any platform-specific considerations.

**Architecture:** This investigation will examine:
- CMake build system configuration for cross-platform compatibility
- Third-party library dependencies (Mongoose, Modbus RTU)
- Platform-specific code paths (serial/RS485 vs TCP gateway)
- Testing strategy for Windows builds

**Tech Stack:** C++17, CMake, Google Test, Mongoose, Modbus RTU

---

## Current Codebase Analysis

### Task 1: Examine CMakeLists.txt for cross-platform configuration

**Files:**
- Read: `CMakeLists.txt`
- Read: `cmake/FindMongoose.cmake` (if exists)
- Read: `cmake/FindModbus.cmake` (if exists)

**Investigation Steps:**

- [ ] **Step 1: Read CMakeLists.txt**

```bash
cat CMakeLists.txt
```

**Expected findings:**
- Check if `CMAKE_SYSTEM_NAME` is checked for platform-specific settings
- Verify library find_module usage
- Check for platform-specific compiler flags

- [ ] **Step 2: Document platform-specific code patterns**

Search for:
```bash
grep -r "WIN32\|_WIN32\|__WIN32\|windows" src/ include/ tests/ --include="*.cpp" --include="*.hpp" --include="*.c" --include="*.h"
```

**Expected findings:**
- Any Windows-specific `#ifdef` blocks
- Platform-specific file paths
- System calls that differ on Windows

---

## Dependency Analysis

### Task 2: Investigate Mongoose library

**Files:**
- Check: `cmake/FindMongoose.cmake`
- Check: `src/web/WebServer.cpp`
- Check: `include/web/IWebServer.hpp`

**Investigation Steps:**

- [ ] **Step 1: Check Mongoose integration**

```bash
grep -r "mongoose\|MG_" src/ include/ tests/ --include="*.cpp" --include="*.hpp" --include="*.c" --include="*.h" | head -20
```

**Expected findings:**
- Mongoose is used for HTTP server (cross-platform ✅)
- Verify no platform-specific assumptions

- [ ] **Step 2: Verify Mongoose portability**

Check Mongoose documentation claims:
- Cross-platform network library
- Works on Windows, Linux, macOS
- No POSIX-specific calls (uses BSD sockets API on Windows)

---

### Task 3: Investigate Modbus RTU implementation

**Files:**
- Read: `src/modbus_client.c`
- Read: `include/modbus/IModbusClient.hpp`
- Read: `src/modbus/modbus_rtu_frames.c` (if exists)

**Investigation Steps:**

- [ ] **Step 1: Check Modbus transport layer**

```bash
grep -r "socket\|connect\|read\|write" src/modbus_client.c | head -30
```

**Expected findings:**
- TCP gateway connection (Waveshare transparent gateway)
- Socket-based communication (cross-platform ✅)
- No serial/RS485 direct access in current implementation

- [ ] **Step 2: Identify any serial/RS485 dependencies**

```bash
grep -r "serial\|RS485\|COM\|/dev/" src/ include/ --include="*.cpp" --include="*.hpp" --include="*.c" --include="*.h"
```

**Expected findings:**
- Current implementation uses TCP gateway only
- No native serial port access

---

## Platform-Specific Code Review

### Task 4: Review main entry point

**Files:**
- Read: `src/main.cpp`

**Investigation Steps:**

- [ ] **Step 1: Check for platform-specific includes**

```bash
head -50 src/main.cpp
```

**Expected findings:**
- Standard C++ headers (cross-platform ✅)
- No Windows-only headers

- [ ] **Step 2: Check signal handling**

```bash
grep -n "signal\|SIGINT\|signal(" src/main.cpp
```

**Expected findings:**
- POSIX signal handling (needs Windows compatibility layer)
- Windows uses `SetConsoleCtrlHandler` instead of `signal()`

---

### Task 5: Review threading implementation

**Files:**
- Read: `src/core/ControlLoop.cpp`
- Read: `include/core/ControlLoop.hpp`

**Investigation Steps:**

- [ ] **Step 1: Check threading headers**

```bash
grep -n "#include.*thread\|#include.*pthread" src/core/ControlLoop.cpp
```

**Expected findings:**
- C++11 `std::thread` (cross-platform ✅)
- No pthread-specific calls

- [ ] **Step 2: Check mutex/atomic usage**

```bash
grep -n "std::mutex\|std::atomic\|std::condition_variable" src/core/ControlLoop.cpp | head -20
```

**Expected findings:**
- Standard C++ synchronization primitives (cross-platform ✅)

---

## Build System Investigation

### Task 6: Check for compiler-specific code

**Files:**
- Read: `CMakeLists.txt`

**Investigation Steps:**

- [ ] **Step 1: Check compiler flags**

```bash
grep -n "CMAKE_CXX_FLAGS\|MSVC\|gcc\|clang" CMakeLists.txt
```

**Expected findings:**
- Standard C++17 flags
- Any MSVC-specific warnings disabled

- [ ] **Step 2: Check C standard**

```bash
grep -n "CMAKE_C_FLAGS\|C_STANDARD" CMakeLists.txt
```

**Expected findings:**
- C11 or C99 standard
- No POSIX-specific C extensions

---

## Testing on Windows

### Task 7: Review test infrastructure

**Files:**
- Read: `tests/CMakeLists.txt`
- Read: `tests/core/test_control_loop.cpp`

**Investigation Steps:**

- [ ] **Step 1: Check test dependencies**

```bash
cat tests/CMakeLists.txt
```

**Expected findings:**
- Google Test (cross-platform ✅)
- No platform-specific test libraries

- [ ] **Step 2: Check test code patterns**

```bash
grep -n "TEST\|EXPECT" tests/core/test_control_loop.cpp | head -30
```

**Expected findings:**
- Standard Google Test macros (cross-platform ✅)

---

## Summary Report

### Task 8: Compile findings

**Deliverable:** `docs/windows-platform-support.md`

**Required sections:**

1. **Current State Assessment**
   - CMake cross-platform compatibility
   - Dependencies (Mongoose, Modbus, Google Test)
   - Platform-specific code identified

2. **Build Requirements on Windows**
   - Compiler: MSVC (Visual Studio 2019+) or MinGW-w64
   - CMake version requirements
   - Dependency installation (vcpkg or manual)

3. **Known Issues**
   - Signal handling (POSIX vs Windows)
   - File paths (forward vs backward slash)
   - Time functions (if any)

4. **Recommendations**
   - Minimal changes needed for Windows support
   - Testing strategy
   - CI/CD considerations

---

## Verification

### Task 9: Final check

- [ ] **Step 1: Review all findings**
- [ ] **Step 2: Confirm no blockers**
- [ ] **Step 3: Save summary report**

**Summary report should answer:**
- Can this project compile on Windows? Yes/No
- What dependencies need to be installed?
- What code changes are required?
- What is the build process on Windows?

---

## Execution Handoff

**Plan complete and saved to `docs/superpowers/plans/2026-06-06-windows-platform-support-investigation.md`.**

**Two execution options:**

**1. Subagent-Driven (recommended)** - Dispatch a fresh subagent per task, review between tasks, fast iteration

**2. Inline Execution** - Execute tasks in this session using executing-plans, batch execution with checkpoints for review

**Which approach?**
