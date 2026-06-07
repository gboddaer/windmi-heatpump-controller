# Platform Abstraction Completion Plan

> **Goal:** Create clean, platform-independent abstraction classes for `Thread`, `Mutex`, `LockGuard`, `UniqueLock`, and `ConditionVariable` so that no `<thread>`, `<mutex>`, or `<condition_variable>` includes remain outside `Platform.hpp`/`Platform.cpp`. This enables Windows builds with MinGW (which lacks these C++17 headers).

---

## Current State

### What's done ✅
- `windmi::Mutex` and `windmi::LockGuard` in `Platform.hpp` (using `CRITICAL_SECTION` on Win32, `pthread_mutex_t` on POSIX)
- `windmi::platform::*` functions (signals, locks, paths, sleep, PID checks)
- `LogLevel` enum renamed to CamelCase (avoids Windows macro conflicts)
- `src/main.cpp` fully uses Platform abstraction (no direct POSIX calls)
- All tests pass on Linux (34/34)

### What's incomplete ❌
- `ControlLoop.hpp` still uses `std::thread`, `std::mutex`, `std::condition_variable`
- `StatusMonitor.hpp` still uses `std::mutex` and `std::lock_guard`
- `SimulatedModbusClient.hpp` still uses `std::mutex` and `std::lock_guard`
- `ControlLoop.cpp` uses `std::make_unique<std::thread>`, `std::unique_lock<std::mutex>`, `std::lock_guard<std::mutex>`
- `StatusMonitor.cpp` uses `std::lock_guard<std::mutex>`
- `SimulatedModbusClient.cpp` uses `std::lock_guard<std::mutex>`
- `test_logger.cpp` uses `std::thread`
- `test_control_loop.cpp` uses `std::thread`
- Windows build fails because MinGW lacks `<thread>`, `<mutex>`, `<condition_variable>`

### Files to modify

| File | Change |
|------|--------|
| `include/utils/Platform.hpp` | Add `Thread`, `UniqueLock`, `ConditionVariable` classes |
| `src/utils/Platform.cpp` | Implement `Thread`, `UniqueLock`, `ConditionVariable` |
| `include/core/ControlLoop.hpp` | Replace `std::thread`→`Thread`, `std::mutex`→`Mutex`, `std::condition_variable`→`ConditionVariable`, `std::unique_lock`→`UniqueLock`, `std::lock_guard`→`LockGuard` |
| `src/core/ControlLoop.cpp` | Same replacements as header + include changes |
| `include/core/StatusMonitor.hpp` | Replace `std::mutex`→`Mutex`, `std::lock_guard`→`LockGuard` |
| `src/core/StatusMonitor.cpp` | Same replacements as header |
| `include/modbus/SimulatedModbusClient.hpp` | Replace `std::mutex`→`Mutex`, `std::lock_guard`→`LockGuard` |
| `src/modbus/SimulatedModbusClient.cpp` | Same replacements as header |
| `tests/utils/test_logger.cpp` | Replace `std::thread`→`windmi::Thread` (or use `std::thread` conditionally) |
| `tests/core/test_control_loop.cpp` | Same |
| `src/utils/Platform.cpp` | Fix `acquire_instance_lock` / `release_instance_lock` (currently has no-op on POSIX and incomplete on Windows — lock fd leaks) |
| `tests/utils/test_platform.cpp` | Add Thread, ConditionVariable, UniqueLock tests |

---

## Design: Platform Abstraction Classes

### Thread

```cpp
namespace windmi {

class Thread {
public:
    Thread() = default;
    ~Thread();

    // Prevent copying
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    // Move semantics
    Thread(Thread&& other) noexcept;
    Thread& operator=(Thread&& other) noexcept;

    template<typename Func, typename... Args>
    explicit Thread(Func&& func, Args&&... args);

    bool joinable() const;
    void join();
    void detach();

private:
#ifdef _WIN32
    HANDLE handle_ = nullptr;
    unsigned int thread_id_ = 0;
#else
    pthread_t thread_{};
    bool joined_ = false;
    bool detached_ = false;
#endif
};

} // namespace windmi
```

**Implementation notes:**
- On POSIX: wraps `pthread_create`/`pthread_join`/`pthread_detach`
- On Windows: wraps `_beginthreadex`/`WaitForSingleObject`/`CloseHandle`
- Template constructor uses `std::function<void()>` internally to store the callable
- Move semantics transfer ownership of the underlying handle

### ConditionVariable

```cpp
namespace windmi {

class ConditionVariable {
public:
    ConditionVariable();
    ~ConditionVariable();

    void notify_one();
    void notify_all();

    // Wait until notified
    void wait(UniqueLock& lock);

    // Wait with predicate
    template<typename Predicate>
    bool wait(UniqueLock& lock, Predicate pred);

    // Wait for duration, returns true if notified, false on timeout
    bool wait_for(UniqueLock& lock, unsigned int ms);

    // Wait for duration with predicate
    template<typename Predicate>
    bool wait_for(UniqueLock& lock, unsigned int ms, Predicate pred);

    ConditionVariable(const ConditionVariable&) = delete;
    ConditionVariable& operator=(const ConditionVariable&) = delete;

private:
#ifdef _WIN32
    CONDITION_VARIABLE cv_;
#else
    pthread_cond_t cond_;
#endif
};

} // namespace windmi
```

**Implementation notes:**
- On POSIX: wraps `pthread_cond_init`/`pthread_cond_signal`/`pthread_cond_broadcast`/`pthread_cond_timedwait`
- On Windows: wraps `InitializeConditionVariable`/`WakeConditionVariable`/`WakeAllConditionVariable`/`SleepConditionVariableCS`
- `wait_for` takes milliseconds, not `std::chrono` — avoids `<chrono>` dependency issues on MinGW
- Windows `CONDITION_VARIABLE` is a lightweight struct available since Vista, no dynamic allocation

### UniqueLock (needed for ConditionVariable::wait)

```cpp
namespace windmi {

class UniqueLock {
public:
    explicit UniqueLock(Mutex& mutex);
    ~UniqueLock();

    void lock();
    void unlock();
    Mutex* mutex() const;
    bool owns_lock() const;

    UniqueLock(const UniqueLock&) = delete;
    UniqueLock& operator=(const UniqueLock&) = delete;

private:
    Mutex* mutex_;
    bool owns_;
};

} // namespace windmi
```

**Implementation notes:**
- Necessary because `ConditionVariable::wait` needs a lock/unlock interface
- Simpler than `std::unique_lock` — no deferred locking, no swap, etc.
- On POSIX, `pthread_cond_wait` requires `pthread_mutex_t*`, which our `Mutex` already wraps
- On Windows, `SleepConditionVariableCS` requires `CRITICAL_SECTION*`, which our `Mutex` already wraps

---

## Tasks

### Task 1: Add Thread, UniqueLock, ConditionVariable to Platform.hpp

**File:** `include/utils/Platform.hpp`

- [ ] **Step 1:** Add class declarations for `Thread`, `UniqueLock`, `ConditionVariable`
  - `Thread`: constructor takes `std::function<void()>`, stores `HANDLE` (Win) or `pthread_t` (POSIX)
  - `UniqueLock`: RAII wrapper for `Mutex`, provides `lock()`/`unlock()`/`mutex()`/`owns_lock()`
  - `ConditionVariable`: wraps `CONDITION_VARIABLE` (Win) or `pthread_cond_t` (POSIX)
  - `wait_for()` takes `unsigned int ms` (avoids `<chrono>` dependency issues)
  - All classes are non-copyable; `Thread` supports move semantics
- [ ] **Step 2:** Verify header compiles on Linux:
  ```bash
  echo '#include "utils/Platform.hpp"' | g++ -x c++ -std=c++17 -I include -fsyntax-only -
  ```
- [ ] **Step 3:** Commit

### Task 2: Implement Thread, UniqueLock, ConditionVariable in Platform.cpp

**File:** `src/utils/Platform.cpp`

- [ ] **Step 1:** Implement `Thread` class:
  - POSIX: `pthread_create`, `pthread_join`, `pthread_detach`
  - Windows: `_beginthreadex`, `WaitForSingleObject`, `CloseHandle`
  - Template constructor stores callable in `std::function<void()>` on heap, passes to thread entry
  - Move constructor/assignment transfers handle ownership
  - Destructor: joins if joinable (safe default)
- [ ] **Step 2:** Implement `UniqueLock` class:
  - Constructor locks the mutex, destructor unlocks
  - `lock()`/`unlock()` forward to `Mutex::lock()`/`Mutex::unlock()`
  - `mutex()` returns stored pointer
  - `owns_lock()` returns whether lock is held
- [ ] **Step 3:** Implement `ConditionVariable` class:
  - POSIX: `pthread_cond_init`, `pthread_cond_destroy`, `pthread_cond_signal`, `pthread_cond_broadcast`, `pthread_cond_timedwait`
  - Windows: `InitializeConditionVariable` (no init needed — it's a static init struct), `WakeConditionVariable`, `WakeAllConditionVariable`, `SleepConditionVariableCS`
  - `wait()` calls `pthread_cond_wait` or `SleepConditionVariableCS`
  - `wait_for(ms)` computes absolute timeout for `pthread_cond_timedwait` using `clock_gettime(CLOCK_MONOTONIC)`, or uses `SleepConditionVariableCS` with timeout
- [ ] **Step 4:** Build and run Platform tests
- [ ] **Step 5:** Commit

### Task 3: Write tests for new Platform classes

**File:** `tests/utils/test_platform.cpp`

- [ ] **Step 1:** Add `Thread` tests:
  - Create thread, run lambda, join
  - Verify thread function executes
  - Test move semantics
  - Test `joinable()` before and after join
- [ ] **Step 2:** Add `UniqueLock` tests:
  - Lock/unlock lifecycle
  - `owns_lock()` state tracking
  - RAII automatic unlock on destruction
- [ ] **Step 3:** Add `ConditionVariable` tests:
  - `notify_one` wakes a waiting thread
  - `notify_all` wakes multiple waiting threads
  - `wait_for` with timeout returns false on timeout
  - `wait_for` with predicate returns true when predicate satisfied
- [ ] **Step 4:** Run all tests
- [ ] **Step 5:** Commit

### Task 4: Refactor ControlLoop.hpp to use Platform classes

**File:** `include/core/ControlLoop.hpp`

- [ ] **Step 1:** Replace includes:
  ```cpp
  // Remove:
  #include <thread>
  #include <mutex>
  #include <condition_variable>
  
  // Add:
  #include "utils/Platform.hpp"
  ```
- [ ] **Step 2:** Replace member types:
  ```cpp
  // Before:
  std::unique_ptr<std::thread> thread_;
  std::mutex kick_mutex_;
  std::condition_variable kick_cond_;
  
  // After:
  std::unique_ptr<Thread> thread_;
  Mutex kick_mutex_;
  ConditionVariable kick_cond_;
  ```
- [ ] **Step 3:** Commit

### Task 5: Refactor ControlLoop.cpp to use Platform classes

**File:** `src/core/ControlLoop.cpp`

- [ ] **Step 1:** Replace includes:
  ```cpp
  // Remove:
  #include <thread>
  
  // No new include needed (Platform.hpp via ControlLoop.hpp)
  ```
- [ ] **Step 2:** Replace thread creation:
  ```cpp
  // Before:
  thread_ = std::make_unique<std::thread>(&ControlLoop::threadFunc, this);
  
  // After:
  thread_ = std::make_unique<Thread>([this]() { threadFunc(); });
  ```
- [ ] **Step 3:** Replace lock/condition usage:
  ```cpp
  // Before:
  std::lock_guard<std::mutex> lock(kick_mutex_);
  // After:
  LockGuard lock(kick_mutex_);
  
  // Before:
  std::unique_lock<std::mutex> lock(kick_mutex_);
  // After:
  UniqueLock lock(kick_mutex_);
  
  // Before:
  kick_cond_.wait_for(lock, std::chrono::seconds(...), predicate);
  // After:
  kick_cond_.wait_for(lock, ms, predicate);
  ```
- [ ] **Step 4:** Convert all `std::chrono` durations to milliseconds:
  - `std::chrono::seconds(MODBUS_RECONNECT_INTERVAL_S)` → `MODBUS_RECONNECT_INTERVAL_S * 1000`
  - `std::chrono::milliseconds(sleep_ms)` → `sleep_ms`
- [ ] **Step 5:** Build and test
- [ ] **Step 6:** Commit

### Task 6: Refactor StatusMonitor to use Platform Mutex

**Files:** `include/core/StatusMonitor.hpp`, `src/core/StatusMonitor.cpp`

- [ ] **Step 1:** Replace includes:
  ```cpp
  // Remove:
  #include <mutex>
  
  // Add:
  #include "utils/Platform.hpp"
  ```
- [ ] **Step 2:** Replace member type:
  ```cpp
  // Before:
  mutable std::mutex mutex_;
  
  // After:
  mutable Mutex mutex_;
  ```
- [ ] **Step 3:** Replace lock usage:
  ```cpp
  // Before:
  std::lock_guard<std::mutex> lock(mutex_);
  
  // After:
  LockGuard lock(mutex_);
  ```
- [ ] **Step 4:** Build and test
- [ ] **Step 5:** Commit

### Task 7: Refactor SimulatedModbusClient to use Platform Mutex

**Files:** `include/modbus/SimulatedModbusClient.hpp`, `src/modbus/SimulatedModbusClient.cpp`

- [ ] **Step 1:** Replace includes:
  ```cpp
  // Remove:
  #include <mutex>
  
  // Add:
  #include "utils/Platform.hpp"
  ```
- [ ] **Step 2:** Replace member type:
  ```cpp
  // Before:
  mutable std::mutex mutex_;
  
  // After:
  mutable Mutex mutex_;
  ```
- [ ] **Step 3:** Replace lock usage:
  ```cpp
  // Before:
  std::lock_guard<std::mutex> lock(mutex_);
  
  // After:
  LockGuard lock(mutex_);
  ```
- [ ] **Step 4:** Build and test
- [ ] **Step 5:** Commit

### Task 8: Fix instance lock fd leak and Windows implementation

**File:** `src/utils/Platform.cpp`

Current POSIX `acquire_instance_lock()` opens a file descriptor but never stores it for later release. Windows implementation doesn't store the mutex handle either. Both `release_instance_lock()` are no-ops.

- [ ] **Step 1:** Add static storage for the lock file descriptor / mutex handle:
  ```cpp
  // POSIX:
  static int g_lock_fd = -1;
  
  // Windows:
  static HANDLE g_lock_handle = nullptr;
  ```
- [ ] **Step 2:** POSIX: store fd in `g_lock_fd` on successful `flock()`, close fd in `release_instance_lock()`
- [ ] **Step 3:** Windows: store `HANDLE` in `g_lock_handle`, close in `release_instance_lock()`
- [ ] **Step 4:** Build and test
- [ ] **Step 5:** Commit

### Task 9: Verify no `<thread>`, `<mutex>`, `<condition_variable>` remains outside Platform.hpp

- [ ] **Step 1:** Search for remaining direct includes:
  ```bash
  grep -rn '#include.*<thread>\|#include.*<mutex>\|#include.*<condition_variable>' --include="*.hpp" --include="*.cpp" --include="*.h" --include="*.c" | grep -v build | grep -v ".git" | grep -v "Platform.hpp" | grep -v "Platform.cpp" | grep -v "mongoose/"
  ```
  Expected: no results (all test files should also use Platform)
- [ ] **Step 2:** Search for remaining direct usage:
  ```bash
  grep -rn 'std::thread\|std::mutex\|std::condition_variable\|std::lock_guard\|std::unique_lock\|pthread_' --include="*.hpp" --include="*.cpp" | grep -v build | grep -v ".git" | grep -v "Platform."
  ```
  Expected: no results outside `Platform.*`
- [ ] **Step 3:** Clean Linux build:
  ```bash
  rm -rf build && mkdir build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug && make -j4 && ctest --output-on-failure
  ```
  Expected: all tests pass
- [ ] **Step 4:** Commit final verification

### Task 10: Update Windows build documentation and test cross-build

- [ ] **Step 1:** Update `docs/windows-platform-support.md` to reflect the complete platform abstraction
- [ ] **Step 2:** Update `docs/windows-build-instructions.md` with any new requirements
- [ ] **Step 3:** Test Windows cross-build with MinGW:
  ```bash
  rm -rf build-win64 && mkdir build-win64 && cd build-win64
  cmake .. -G "Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE=../mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release
  make windmi-control
  ```
  Expected: clean build with no errors
- [ ] **Step 4:** Commit documentation updates
- [ ] **Step 5:** Push and verify CI

---

## Implementation Order

1. **Task 1** → Platform.hpp additions (declarations only)
2. **Task 2** → Platform.cpp implementations
3. **Task 3** → Tests for new classes
4. **Tasks 4-7** → Refactor all consumers (ControlLoop, StatusMonitor, SimulatedModbusClient)
5. **Task 8** → Fix lock fd leak (independent, but do it now)
6. **Task 9** → Verification sweep
7. **Task 10** → Documentation and cross-build test

Tasks 4-7 are independent of each other and could be done in parallel, but must be sequential after Tasks 1-3.

---

## Key Design Decisions

1. **`Thread` uses `std::function<void()>` internally** — avoids template specialization in `Platform.cpp`. The callable is type-erased at the call site.

2. **`wait_for` takes `unsigned int ms`** — avoids `<chrono>` dependency issues on MinGW. This is a simpler, more portable API.

3. **`UniqueLock` is minimal** — only provides what `ConditionVariable::wait` needs. No deferred locking, no swap, no release/reacquire. If needed later, it can be extended.

4. **`Thread` move semantics** — follows `std::thread` semantics: move transfers ownership, source becomes non-joinable.

5. **Lock fd leak fix** — the current POSIX `acquire_instance_lock()` leaks the file descriptor because it never stores it. This is a real bug, not just a Windows issue.

6. **No `<thread>`, `<mutex>`, `<condition_variable>` outside `Platform.*`** — this is the invariant. All thread/mutex/condvar usage goes through `windmi::Thread`, `windmi::Mutex`, `windmi::ConditionVariable`.