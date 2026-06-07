# Windows Build Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement Windows platform support by adding a signal handling wrapper that allows the project to build and run on Windows while maintaining POSIX compatibility.

**Architecture:** Implement a cross-platform signal handling system using conditional compilation. The wrapper will use `SetConsoleCtrlHandler` on Windows and `signal()` on POSIX systems, both triggering the same shutdown logic.

**Tech Stack:** C++17, CMake, Mongoose, Google Test

---

## Current State

**Baseline:** Clean build, all tests passing on Linux. Windows support investigation complete.

**Blocking Issue:** Signal handling in `src/main.cpp` uses POSIX `signal()` which is not available on Windows.

**Solution:** Add platform-specific signal handling with `#ifdef _WIN32` blocks.

---

## Task 1: Add Cross-Platform Signal Handling

**Files:**
- Modify: `src/main.cpp:105-120` (signal_handler function)
- Modify: `src/main.cpp:283-286` (signal setup in main)
- Add: Windows-compatible header include

### Task 1a: Add Windows-compatible header include

**Files:**
- Modify: `src/main.cpp:10-15`

- [ ] **Step 1: Add conditional signal headers**

Find the include section (around line 13-15) and modify to:

```cpp
#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#endif
```

- [ ] **Step 2: Verify compilation on Linux**

```bash
cmake --build build --clean-first
```

Expected: Build succeeds with no warnings.

- [ ] **Step 3: Commit**

```bash
git add src/main.cpp
git commit -m "feat: add cross-platform signal handling headers"
```

---

### Task 1b: Modify signal_handler function signature

**Files:**
- Modify: `src/main.cpp:105-120`

- [ ] **Step 1: Update signal_handler for Windows compatibility**

On Windows, console control handler uses `BOOL handler(DWORD)` signature.
On POSIX, signal handler uses `void handler(int)` signature.

Modify the function to support both:

```cpp
#ifdef _WIN32
static BOOL WINAPI signal_handler(DWORD dwCtrlType)
#else
static void signal_handler(int sig)
#endif
{
    // Async-signal-safe: only update sig_atomic_t state here.
    switch (dwCtrlType) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
#ifdef _WIN32
            g_shutdown_requested = true;
            return TRUE;
#else
            g_shutdown_requested = true;
#endif
        default:
#ifdef _WIN32
            return FALSE;
#else
            return;
#endif
    }
}
```

- [ ] **Step 2: Update shutdown_requested declaration**

Find `static std::atomic<bool> g_shutdown_requested(false);` and add Windows-compatible version:

```cpp
#ifdef _WIN32
static volatile BOOL g_shutdown_requested = FALSE;
#else
static std::atomic<bool> g_shutdown_requested(false);
#endif
```

- [ ] **Step 3: Verify compilation on Linux**

```bash
cmake --build build
```

Expected: Build succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "feat: implement cross-platform signal_handler"
```

---

### Task 1c: Update signal setup in main()

**Files:**
- Modify: `src/main.cpp:283-286`

- [ ] **Step 1: Replace signal() setup with platform-specific code**

Replace the signal setup section with:

```cpp
// Set up signal handlers
#ifdef _WIN32
if (!SetConsoleCtrlHandler(signal_handler, TRUE)) {
    WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Failed to set console control handler");
}
#else
if (signal(SIGINT, signal_handler) == SIG_ERR) {
    WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Failed to set SIGINT handler");
}
if (signal(SIGTERM, signal_handler) == SIG_ERR) {
    WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Failed to set SIGTERM handler");
}
signal(SIGPIPE, SIG_IGN);  // Ignore SIGPIPE (client disconnects)
#endif
```

- [ ] **Step 2: Update shutdown check in main loop**

Find the loop that checks `g_shutdown_requested` and update for Windows:

```cpp
// Poll web server until a signal requests shutdown
#ifdef _WIN32
while (!g_shutdown_requested)
#else
while (!g_shutdown_requested.load())
#endif
{
    // ... existing loop body ...
}
```

- [ ] **Step 3: Verify compilation on Linux**

```bash
cmake --build build
```

Expected: Build succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "feat: implement platform-specific signal setup and check"
```

---

## Task 2: Add Windows-Specific Code for Shutdown Cleanup

**Files:**
- Modify: `src/main.cpp:369-371` (shutdown cleanup)

- [ ] **Step 1: Update shutdown cleanup for Windows**

The shutdown cleanup section needs to handle Windows socket cleanup:

```cpp
// Cleanup on shutdown
#ifdef _WIN32
// Cleanup Windows Sockets
WSACleanup();
#endif
if (g_control_loop) {
    g_control_loop->stop();
    delete g_control_loop;
    g_control_loop = nullptr;
}
```

- [ ] **Step 2: Verify compilation on Linux**

```bash
cmake --build build
```

Expected: Build succeeds.

- [ ] **Step 3: Commit**

```bash
git add src/main.cpp
git commit -m "feat: add Windows socket cleanup on shutdown"
```

---

## Task 3: Update Windows Platform Support Report

**Files:**
- Modify: `docs/windows-platform-support.md`

- [ ] **Step 1: Add implementation status section**

Append to the report:

```markdown
## Implementation Status

**Status:** In progress

### Completed
- ✅ Cross-platform build system verified
- ✅ Dependencies confirmed cross-platform
- ✅ Threading verified (C++ std::thread)
- ✅ Networking verified (BSD sockets)
- ⏳ Signal handling - In progress (Task 1-2)

### Remaining
- [ ] Test build on Windows
- [ ] Verify Ctrl+C shutdown behavior
- [ ] Document Windows build instructions in README

### Known Issues
- None identified yet (pending Windows testing)
```

- [ ] **Step 2: Update conclusion**

Modify the conclusion section to reflect the implementation status.

- [ ] **Step 3: Commit**

```bash
git add docs/windows-platform-support.md
git commit -m "docs: update Windows platform support status"
```

---

## Task 4: Final Verification

- [ ] **Step 1: Clean build**

```bash
cmake --build build --clean-first
```

Expected: No errors, no warnings.

- [ ] **Step 2: Run all tests**

```bash
ctest --output-on-failure
```

Expected: 4/4 tests pass.

- [ ] **Step 3: Push to remote**

```bash
git push origin feature/windows-support
```

- [ ] **Step 4: Update plan checklist**

Mark all tasks as complete in the plan document.

- [ ] **Step 5: Final commit**

```bash
git add docs/superpowers/plans/2026-06-06-windows-build-implementation.md
git commit -m "docs: update Windows build implementation plan status"
```

---

## Execution Handoff

**Plan complete and saved to `docs/superpowers/plans/2026-06-06-windows-build-implementation.md`.**

**Two execution options:**

**1. Subagent-Driven (recommended)** - I dispatch a fresh subagent per task, review between tasks, fast iteration

**2. Inline Execution** - Execute tasks in this session using executing-plans, batch execution with checkpoints

**Which approach?**

**If Subagent-Driven chosen:**
- **REQUIRED SUB-SKILL:** Use superpowers:subagent-driven-development
- Fresh subagent per task + two-stage review

**If Inline Execution chosen:**
- **REQUIRED SUB-SKILL:** Use superpowers:executing-plans
- Batch execution with checkpoints for review
