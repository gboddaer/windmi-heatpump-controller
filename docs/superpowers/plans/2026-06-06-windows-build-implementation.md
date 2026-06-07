# Windows Build Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Windmi Controller compile and run on Windows by extracting all POSIX-specific code from `src/main.cpp` into a platform abstraction layer (`Platform.hpp`/`Platform.cpp`), then providing Windows implementations behind `#ifdef _WIN32`.

**Architecture:** Create `windmi::platform` namespace in `include/utils/Platform.hpp` + `src/utils/Platform.cpp`. The header is pure interface (no `#ifdef`). The .cpp has `#ifdef _WIN32` blocks for Windows vs POSIX. `src/main.cpp` is refactored to call platform functions instead of directly using `flock`, `/proc`, `readlink`, `usleep`, `signal`, etc. This removes all `#ifdef` from main.cpp — platform differences live only in Platform.cpp.

**Tech Stack:** C++17, CMake 3.16+, Google Test, Mongoose

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `include/utils/Platform.hpp` | Create | Platform-abstracted function declarations |
| `src/utils/Platform.cpp` | Create | POSIX + Windows implementations |
| `tests/utils/test_platform.cpp` | Create | Unit tests for platform layer |
| `src/main.cpp` | Modify | Remove POSIX-only code, call `windmi::platform::*` |
| `src/utils/CMakeLists.txt` | Modify | Add `Platform.cpp` to utils library |
| `tests/utils/CMakeLists.txt` | Modify | Add `test_platform.cpp` |
| `CMakeLists.txt` | Modify | Add Windows system libs |

---

### POSIX-only code in current `src/main.cpp` that must be abstracted

| Lines | Code | POSIX API | Windows Equivalent |
|-------|------|-----------|-------------------|
| 22 | `#include <unistd.h>` | `readlink`, `getpid`, `usleep` | `<process.h>`, `<io.h>`, `GetModuleFileName` |
| 23 | `#include <fcntl.h>` | `open`, `O_CREAT\|O_RDWR` | `<io.h>`, `_open` |
| 24 | `#include <sys/file.h>` | `flock`, `LOCK_EX\|LOCK_NB` | Named mutex (`CreateMutexA`) |
| 25-26 | `#include <sys/stat.h>`, `<sys/types.h>` | `stat`, `S_ISDIR` | `<sys/stat.h>` works on MSVC too |
| 44 | `#define LOCK_FILE "/tmp/..."` | `/tmp` exists only on POSIX | `%TEMP%` or `%LOCALAPPDATA%` |
| 62-86 | `resolve_static_dir()` | `readlink("/proc/self/exe")`, `realpath` | `GetModuleFileName`, `_fullpath` |
| 105-110 | `signal_handler()` | `signal(SIGINT, ...)` | `SetConsoleCtrlHandler` |
| 112-118 | `is_pid_alive()` | `/proc/<pid>/stat` | `OpenProcess` + `GetExitCodeProcess` |
| 120-165 | `acquire_lock()` / `release_lock()` | `flock`, `open`, `fcntl` | `CreateMutexA`, `ReleaseMutex`, `CloseHandle` |
| 283-286 | Signal setup | `signal(SIGINT/SIGTERM/SIGPIPE)` | `SetConsoleCtrlHandler` |
| 397 | `usleep(150000)` | `usleep` | `Sleep(ms)` |
| 421 | `usleep(100000)` | `usleep` | `Sleep(ms)` |

---

## Task 1: Create Platform.hpp Interface

**Files:**
- Create: `include/utils/Platform.hpp`

- [ ] **Step 1: Create the header file**

```cpp
#pragma once

#include <csignal>
#include <string>

namespace windmi::platform {

/**
 * Install platform-specific signal handlers for graceful shutdown.
 * On POSIX: catches SIGINT, SIGTERM, ignores SIGPIPE.
 * On Windows: installs SetConsoleCtrlHandler for Ctrl+C/Break/Close.
 * When triggered, sets *running_flag = 0.
 */
void install_signal_handlers(volatile sig_atomic_t* running_flag);

/**
 * Acquire exclusive instance lock to prevent multiple running instances.
 * On POSIX: uses flock() on /tmp/windmi-controller.lock.
 * On Windows: uses named mutex Global\\windmi-controller.
 * @param force  If true, remove stale lock before attempting (POSIX only).
 * @return true if lock acquired, false if another instance is running.
 */
bool acquire_instance_lock(bool force = false);

/**
 * Release the instance lock acquired by acquire_instance_lock().
 * Safe to call even if no lock is held (no-op).
 */
void release_instance_lock();

/**
 * Check if a process with the given PID is alive.
 * On POSIX: checks /proc/<pid>/stat.
 * On Windows: uses OpenProcess + GetExitCodeProcess.
 */
bool is_pid_alive(int pid);

/**
 * Resolve the static files directory.
 * If @p dir exists as-is, returns its absolute path.
 * Otherwise tries relative to the executable, then one level up.
 * Returns @p dir as-is if nothing is found.
 */
std::string resolve_static_dir(const std::string& dir);

/**
 * Sleep for the specified number of milliseconds.
 * On POSIX: calls usleep(ms * 1000).
 * On Windows: calls Sleep(ms).
 */
void sleep_ms(unsigned int ms);

}  // namespace windmi::platform
```

- [ ] **Step 2: Verify header compiles standalone**

```bash
echo '#include "utils/Platform.hpp"' | g++ -x c++ -std=c++17 -I include -fsyntax-only -
```

Expected: No errors.

- [ ] **Step 3: Commit**

```bash
git add include/utils/Platform.hpp
git commit -m "feat: add Platform.hpp cross-platform abstraction interface"
```

---

## Task 2: Write Failing Tests for Platform

**Files:**
- Create: `tests/utils/test_platform.cpp`
- Modify: `tests/utils/CMakeLists.txt`

- [ ] **Step 1: Add test file to CMakeLists.txt**

Append to `tests/utils/CMakeLists.txt` (add `test_platform.cpp` to the `add_executable` sources):

```cmake
add_executable(test_utils
    test_config.cpp
    test_json_helpers.cpp
    test_crc16.cpp
    test_logger.cpp
    test_platform.cpp
)
```

- [ ] **Step 2: Create test file**

```cpp
#include <gtest/gtest.h>
#include <csignal>
#include <string>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include "utils/Platform.hpp"

TEST(PlatformTest, InstallSignalHandlersDoesNotCrash) {
    volatile sig_atomic_t running = 1;
    windmi::platform::install_signal_handlers(&running);
    // Just verify it doesn't crash; running should still be 1
    EXPECT_EQ(running, 1);
}

TEST(PlatformTest, IsPidAliveCurrentProcess) {
#ifdef _WIN32
    int pid = _getpid();
#else
    int pid = getpid();
#endif
    EXPECT_TRUE(windmi::platform::is_pid_alive(pid));
}

TEST(PlatformTest, IsPidAliveInvalidPid) {
    EXPECT_FALSE(windmi::platform::is_pid_alive(999999));
}

TEST(PlatformTest, AcquireAndReleaseLock) {
    EXPECT_TRUE(windmi::platform::acquire_instance_lock());
    windmi::platform::release_instance_lock();
    // After release, should be able to acquire again
    EXPECT_TRUE(windmi::platform::acquire_instance_lock());
    windmi::platform::release_instance_lock();
}

TEST(PlatformTest, ReleaseLockWhenNotHeldIsNoop) {
    // Should not crash when releasing without having acquired
    windmi::platform::release_instance_lock();
}

TEST(PlatformTest, ResolveStaticDirDot) {
    std::string result = windmi::platform::resolve_static_dir(".");
    EXPECT_FALSE(result.empty());
}

TEST(PlatformTest, ResolveStaticDirNonExistent) {
    std::string result = windmi::platform::resolve_static_dir("/nonexistent/path/xyz");
    EXPECT_EQ(result, "/nonexistent/path/xyz");
}

TEST(PlatformTest, SleepMsDoesNotCrash) {
    windmi::platform::sleep_ms(10);  // 10ms smoke test
}
```

- [ ] **Step 3: Verify tests fail (linker error — Platform.cpp not yet built)**

```bash
cmake --build build 2>&1 | grep -c "undefined reference"
```

Expected: Multiple "undefined reference" errors for `windmi::platform::*` functions.

- [ ] **Step 4: Commit**

```bash
git add tests/utils/test_platform.cpp tests/utils/CMakeLists.txt
git commit -m "test: add failing Platform abstraction tests"
```

---

## Task 3: Implement Platform.cpp (POSIX)

**Files:**
- Create: `src/utils/Platform.cpp`
- Modify: `src/utils/CMakeLists.txt`

This is the POSIX implementation. Windows `#ifdef _WIN32` blocks will be added in Task 6.

- [ ] **Step 1: Add Platform.cpp to utils CMakeLists.txt**

Modify `src/utils/CMakeLists.txt` — add `Platform.cpp` to the source list:

```cmake
add_library(windmi_utils STATIC
    Config.cpp
    JsonHelpers.cpp
    SpscQueue.cpp
    Logger.cpp
    LoggerC.cpp
    Platform.cpp
)
target_include_directories(windmi_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
```

- [ ] **Step 2: Create Platform.cpp with POSIX implementation**

```cpp
#include "utils/Platform.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>

#ifdef _WIN32
// Windows implementation will be added in a later commit
#else
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <limits.h>
#endif

#include "utils/Logger.hpp"
#include "utils/LogTags.hpp"

namespace {

volatile sig_atomic_t* g_running_flag = nullptr;

#ifdef _WIN32
static HANDLE g_lock_mutex = NULL;
static const char* LOCK_MUTEX_NAME = "Global\\windmi-controller";
#else
static int g_lock_fd = -1;
static const char* LOCK_FILE_PATH = "/tmp/windmi-controller.lock";
#endif

}  // anonymous namespace

namespace windmi::platform {

void install_signal_handlers(volatile sig_atomic_t* running_flag) {
    g_running_flag = running_flag;
#ifdef _WIN32
    // Windows implementation added in Task 6
#else
    struct sigaction sa;
    sa.sa_handler = [](int) {
        if (g_running_flag) *g_running_flag = 0;
    };
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    signal(SIGPIPE, SIG_IGN);
#endif
}

bool is_pid_alive(int pid) {
#ifdef _WIN32
    // Windows implementation added in Task 6
    (void)pid;
    return false;
#else
    char proc_path[64];
    snprintf(proc_path, sizeof(proc_path), "/proc/%d/stat", pid);
    struct stat st;
    return (stat(proc_path, &st) == 0);
#endif
}

bool acquire_instance_lock(bool force) {
#ifdef _WIN32
    // Windows implementation added in Task 6
    (void)force;
    return false;
#else
    if (force) {
        struct stat st;
        if (stat(LOCK_FILE_PATH, &st) == 0) {
            WINDMI_LOG_INFO(LOG_TAG_MAIN, "--force: removing stale lock file %s", LOCK_FILE_PATH);
            unlink(LOCK_FILE_PATH);
        }
    }

    g_lock_fd = open(LOCK_FILE_PATH, O_CREAT | O_RDWR, 0644);
    if (g_lock_fd < 0) {
        WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Failed to open lock file %s: %s",
               LOCK_FILE_PATH, strerror(errno));
        return false;
    }

    // Set close-on-exec so child processes don't inherit the lock
    int flags = fcntl(g_lock_fd, F_GETFD);
    if (flags >= 0) {
        fcntl(g_lock_fd, F_SETFD, flags | FD_CLOEXEC);
    }

    if (flock(g_lock_fd, LOCK_EX | LOCK_NB) < 0) {
        if (errno == EWOULDBLOCK) {
            char pid_buf[32] = {};
            lseek(g_lock_fd, 0, SEEK_SET);
            ssize_t n = read(g_lock_fd, pid_buf, sizeof(pid_buf) - 1);
            (void)n;
            int existing_pid = atoi(pid_buf);
            if (existing_pid > 0 && is_pid_alive(existing_pid)) {
                WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Another instance is already running (PID %d)",
                       existing_pid);
            } else if (existing_pid > 0) {
                WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Lock held by stale process %d (not running)",
                       existing_pid);
                WINDMI_LOG_WARN(LOG_TAG_MAIN, "Remove %s or use --force to override", LOCK_FILE_PATH);
            } else {
                WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Another instance is already running");
            }
        } else {
            WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Failed to acquire lock: %s", strerror(errno));
        }
        close(g_lock_fd);
        g_lock_fd = -1;
        return false;
    }

    // Write PID to lock file for diagnostics
    char pid_buf[32];
    int len = snprintf(pid_buf, sizeof(pid_buf), "%d\n", getpid());
    if (len > 0) {
        lseek(g_lock_fd, 0, SEEK_SET);
        write(g_lock_fd, pid_buf, static_cast<size_t>(len));
        ftruncate(g_lock_fd, len);
    }

    WINDMI_LOG_INFO(LOG_TAG_MAIN, "Lock acquired (PID: %d)", getpid());
    return true;
#endif
}

void release_instance_lock() {
#ifdef _WIN32
    // Windows implementation added in Task 6
#else
    if (g_lock_fd >= 0) {
        flock(g_lock_fd, LOCK_UN);
        close(g_lock_fd);
        g_lock_fd = -1;
        WINDMI_LOG_INFO(LOG_TAG_MAIN, "Lock released");
    }
#endif
}

std::string resolve_static_dir(const std::string& dir) {
#ifdef _WIN32
    // Windows implementation added in Task 6
    return dir;
#else
    struct stat st;
    if (stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        char resolved[PATH_MAX];
        if (realpath(dir.c_str(), resolved)) {
            return resolved;
        }
        return dir;
    }

    // Try relative to the executable's directory
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len > 0) {
        exe_path[len] = '\0';
        char* last_slash = strrchr(exe_path, '/');
        if (last_slash) {
            *last_slash = '\0';
            std::string candidate = std::string(exe_path) + "/" + dir;
            if (stat(candidate.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                char resolved[PATH_MAX];
                if (realpath(candidate.c_str(), resolved)) {
                    return resolved;
                }
                return candidate;
            }
            // Try one more level up (e.g. build/ -> project root)
            std::string candidate2 = std::string(exe_path) + "/../" + dir;
            if (stat(candidate2.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                char resolved[PATH_MAX];
                if (realpath(candidate2.c_str(), resolved)) {
                    return resolved;
                }
                return candidate2;
            }
        }
    }

    WINDMI_LOG_WARN(LOG_TAG_MAIN, "Static directory not found: %s", dir.c_str());
    return dir;
#endif
}

void sleep_ms(unsigned int ms) {
#ifdef _WIN32
    // Windows implementation added in Task 6
#else
    usleep(ms * 1000);
#endif
}

}  // namespace windmi::platform
```

- [ ] **Step 3: Build and run tests**

```bash
cmake --build build --clean-first
cd build && ctest --output-on-failure
```

Expected: All tests pass (4 existing + new platform tests).

- [ ] **Step 4: Verify existing binary still works**

```bash
pkill windmi-control 2>/dev/null; sleep 1
cd build && ./windmi-control --ip 192.168.123.10 --web 10000 &
sleep 3
curl -s http://localhost:10000/api/status | grep -o '"deviceOnline":[^,]*'
pkill windmi-control
```

Expected: `"deviceOnline":true` or similar valid response.

- [ ] **Step 5: Commit**

```bash
git add src/utils/Platform.cpp src/utils/CMakeLists.txt
git commit -m "feat: implement Platform abstraction (POSIX)

- install_signal_handlers: sigaction for SIGINT/SIGTERM, ignore SIGPIPE
- acquire_instance_lock: flock on /tmp lock file with PID diagnostics
- is_pid_alive: /proc/<pid>/stat check
- resolve_static_dir: readlink /proc/self/exe + realpath fallback
- sleep_ms: usleep wrapper
- Windows stubs return false/dir (filled in next task)"
```

---

## Task 4: Refactor main.cpp to Use Platform

**Files:**
- Modify: `src/main.cpp`

Remove all POSIX-only code and replace with `windmi::platform::*` calls.

- [ ] **Step 1: Replace headers**

Replace lines 19-28 of `src/main.cpp`:

```cpp
// BEFORE:
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <climits>

// AFTER:
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <climits>

#include "utils/Platform.hpp"
```

Note: `<csignal>` stays because `volatile sig_atomic_t g_running` uses it.

- [ ] **Step 2: Remove LOCK_FILE define and g_lock_fd**

Delete line 44:

```cpp
// DELETE this line:
#define LOCK_FILE "/tmp/windmi-controller.lock"
```

Delete line 47:

```cpp
// DELETE this line:
static int g_lock_fd = -1;
```

- [ ] **Step 3: Keep g_running, remove signal_handler/is_pid_alive/acquire_lock/release_lock/atexit_release_lock**

Delete the following functions entirely (lines ~62-165):
- `resolve_static_dir()` — replaced by `windmi::platform::resolve_static_dir()`
- `signal_handler()` — replaced by `windmi::platform::install_signal_handlers()`
- `is_pid_alive()` — replaced by `windmi::platform::is_pid_alive()`
- `acquire_lock()` — replaced by `windmi::platform::acquire_instance_lock()`
- `release_lock()` — replaced by `windmi::platform::release_instance_lock()`
- `atexit_release_lock()` — rewritten to call Platform

Keep only:
```cpp
static volatile sig_atomic_t g_running = 1;
```

Add simplified atexit handler:
```cpp
static void atexit_release_lock() {
    windmi::platform::release_instance_lock();
}
```

- [ ] **Step 4: Update main() — replace acquire_lock call**

Replace:
```cpp
    if (force_lock) {
        struct stat st;
        if (stat(LOCK_FILE, &st) == 0) {
            WINDMI_LOG_INFO(LOG_TAG_MAIN, "--force: removing stale lock file %s", LOCK_FILE);
            unlink(LOCK_FILE);
        }
    }
    if (acquire_lock() != 0) {
        return 1;
    }
```

With:
```cpp
    if (!windmi::platform::acquire_instance_lock(force_lock)) {
        return 1;
    }
```

- [ ] **Step 5: Replace signal setup**

Replace:
```cpp
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);  // Ignore SIGPIPE (client disconnects)
```

With:
```cpp
    windmi::platform::install_signal_handlers(&g_running);
```

- [ ] **Step 6: Replace resolve_static_dir call**

Replace:
```cpp
    std::string resolved_static_dir = resolve_static_dir(static_dir);
```

With:
```cpp
    std::string resolved_static_dir = windmi::platform::resolve_static_dir(static_dir);
```

- [ ] **Step 7: Replace usleep calls**

Replace:
```cpp
    usleep(150000);  // 150ms
```

With:
```cpp
    windmi::platform::sleep_ms(150);  // 150ms
```

Replace:
```cpp
                usleep(100000);  // 100ms retry delay
```

With:
```cpp
                windmi::platform::sleep_ms(100);  // 100ms retry delay
```

- [ ] **Step 8: Replace release_lock calls**

Replace all `release_lock();` calls with `windmi::platform::release_instance_lock();`

There should be several in the error paths and the shutdown sequence.

- [ ] **Step 9: Build and run all tests**

```bash
cmake --build build --clean-first
cd build && ctest --output-on-failure
```

Expected: All tests pass.

- [ ] **Step 10: Verify binary still works**

```bash
pkill windmi-control 2>/dev/null; sleep 1
cd build && ./windmi-control --ip 192.168.123.10 --web 10000 &
sleep 3
curl -s http://localhost:10000/api/status | grep -o '"deviceOnline":[^,]*'
pkill windmi-control
```

Expected: `"deviceOnline":true`

- [ ] **Step 11: Commit**

```bash
git add src/main.cpp
git commit -m "refactor: replace POSIX calls in main.cpp with Platform abstraction

- Remove direct flock/open/fcntl/SIGINT/readlink/usleep calls
- Use windmi::platform::install_signal_handlers()
- Use windmi::platform::acquire_instance_lock/release_instance_lock()
- Use windmi::platform::resolve_static_dir()
- Use windmi::platform::sleep_ms()
- main.cpp now has zero #ifdef or POSIX-only headers"
```

---

## Task 5: Add Windows Implementation to Platform.cpp

**Files:**
- Modify: `src/utils/Platform.cpp`

Fill in all the `#ifdef _WIN32` stub blocks with real Windows implementations.

- [ ] **Step 1: Add Windows headers at top of Platform.cpp**

In the `#ifdef _WIN32` block at the top, replace the comment with:

```cpp
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <process.h>
#include <direct.h>
#else
```

- [ ] **Step 2: Implement install_signal_handlers for Windows**

Replace the Windows stub in `install_signal_handlers` with:

```cpp
    // Windows: SetConsoleCtrlHandler for Ctrl+C, Ctrl+Break, window close
    SetConsoleCtrlHandler(
        [](DWORD dwCtrlType) -> BOOL {
            if (dwCtrlType == CTRL_C_EVENT ||
                dwCtrlType == CTRL_BREAK_EVENT ||
                dwCtrlType == CTRL_CLOSE_EVENT) {
                if (g_running_flag) *g_running_flag = 0;
                return TRUE;
            }
            return FALSE;
        },
        TRUE
    );
```

- [ ] **Step 3: Implement is_pid_alive for Windows**

Replace the Windows stub in `is_pid_alive` with:

```cpp
    (void)pid;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                 static_cast<DWORD>(pid));
    if (process == NULL) {
        return false;
    }
    DWORD exit_code = 0;
    if (!GetExitCodeProcess(process, &exit_code)) {
        CloseHandle(process);
        return false;
    }
    CloseHandle(process);
    return (exit_code == STILL_ACTIVE);
```

- [ ] **Step 4: Implement acquire_instance_lock for Windows**

Replace the Windows stub in `acquire_instance_lock` with:

```cpp
    (void)force;
    g_lock_mutex = CreateMutexA(NULL, TRUE, LOCK_MUTEX_NAME);
    if (g_lock_mutex == NULL) {
        DWORD err = GetLastError();
        if (err == ERROR_ALREADY_EXISTS) {
            WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Another instance is already running");
        } else {
            WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Failed to create lock mutex: error %lu", err);
        }
        return false;
    }
    WINDMI_LOG_INFO(LOG_TAG_MAIN, "Lock acquired (PID: %d)", _getpid());
    return true;
```

- [ ] **Step 5: Implement release_instance_lock for Windows**

Replace the Windows stub in `release_instance_lock` with:

```cpp
    if (g_lock_mutex != NULL) {
        ReleaseMutex(g_lock_mutex);
        CloseHandle(g_lock_mutex);
        g_lock_mutex = NULL;
        WINDMI_LOG_INFO(LOG_TAG_MAIN, "Lock released");
    }
```

- [ ] **Step 6: Implement resolve_static_dir for Windows**

Replace the Windows stub in `resolve_static_dir` with:

```cpp
    DWORD attrs = GetFileAttributesA(dir.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES &&
        (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        char resolved[_MAX_PATH];
        if (_fullpath(resolved, dir.c_str(), _MAX_PATH)) {
            return resolved;
        }
        return dir;
    }

    // Try relative to executable directory
    char exe_path[_MAX_PATH];
    if (GetModuleFileNameA(NULL, exe_path, _MAX_PATH) > 0) {
        char* last_slash = strrchr(exe_path, '\\');
        if (!last_slash) last_slash = strrchr(exe_path, '/');
        if (last_slash) {
            *last_slash = '\0';
            std::string candidate = std::string(exe_path) + "\\" + dir;
            attrs = GetFileAttributesA(candidate.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES &&
                (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                char resolved[_MAX_PATH];
                if (_fullpath(resolved, candidate.c_str(), _MAX_PATH)) {
                    return resolved;
                }
                return candidate;
            }
            // Try one more level up
            std::string candidate2 = std::string(exe_path) + "\\..\\" + dir;
            attrs = GetFileAttributesA(candidate2.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES &&
                (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                char resolved[_MAX_PATH];
                if (_fullpath(resolved, candidate2.c_str(), _MAX_PATH)) {
                    return resolved;
                }
                return candidate2;
            }
        }
    }

    WINDMI_LOG_WARN(LOG_TAG_MAIN, "Static directory not found: %s", dir.c_str());
    return dir;
```

- [ ] **Step 7: Implement sleep_ms for Windows**

Replace the Windows stub in `sleep_ms` with:

```cpp
    Sleep(ms);
```

- [ ] **Step 8: Build and verify on Linux (no regressions)**

```bash
cmake --build build --clean-first
cd build && ctest --output-on-failure
```

Expected: All tests pass — POSIX code paths are unchanged, Windows blocks are not compiled.

- [ ] **Step 9: Commit**

```bash
git add src/utils/Platform.cpp
git commit -m "feat: add Windows implementation to Platform abstraction

- SetConsoleCtrlHandler for signal handling
- Named mutex for instance locking
- OpenProcess + GetExitCodeProcess for PID detection
- GetModuleFileName + _fullpath for static dir resolution
- Sleep() for millisecond delay"
```

---

## Task 6: Update CMakeLists.txt for Windows

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add Windows system libraries**

After the `find_package(Threads REQUIRED)` line, add:

```cmake
# Windows-specific system libraries
if(WIN32)
    find_package(Threads REQUIRED)
    # Mongoose links ws2_32 automatically, but we need it for Platform.cpp too
endif()
```

After the `add_executable(windmi-control ...)` section, add Windows link libraries:

```cmake
if(WIN32)
    target_link_libraries(windmi-control PRIVATE ws2_32)
    target_link_libraries(windmi_utils PUBLIC ws2_32)
endif()
```

- [ ] **Step 2: Suppress MSVC-specific warnings**

Inside the existing compiler warning block, add:

```cmake
# Compiler warnings
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-Wall -Wextra)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    add_compile_options(/W4 /permissive-)
    # Disable POSIX deprecation warnings on MSVC
    add_compile_definitions(_CRT_NONSTDC_NO_WARNINGS)
endif()
```

- [ ] **Step 3: Build and verify on Linux (no regressions)**

```bash
cmake --build build --clean-first
cd build && ctest --output-on-failure
```

Expected: All tests pass.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: add Windows support to CMakeLists.txt

- Link ws2_32 on Windows for sockets
- Add MSVC compiler flags (/W4, /permissive-)
- Define _CRT_NONSTDC_NO_WARNINGS for MSVC"
```

---

## Task 7: Update Documentation

**Files:**
- Modify: `docs/windows-platform-support.md`

- [ ] **Step 1: Update the implementation status section**

Replace the existing "Code Changes Required" section in `docs/windows-platform-support.md` with:

```markdown
## Code Changes Implemented

A platform abstraction layer (`windmi::platform` namespace) has been added:

| File | Purpose |
|------|---------|
| `include/utils/Platform.hpp` | Platform-independent interface |
| `src/utils/Platform.cpp` | POSIX + Windows implementations via `#ifdef _WIN32` |

### Functions abstracted

| Function | POSIX | Windows |
|----------|-------|---------|
| `install_signal_handlers()` | `sigaction(SIGINT/SIGTERM)`, ignore SIGPIPE | `SetConsoleCtrlHandler` |
| `acquire_instance_lock()` | `flock()` on `/tmp/windmi-controller.lock` | `CreateMutexA` named mutex |
| `release_instance_lock()` | `flock(LOCK_UN)`, close | `ReleaseMutex`, `CloseHandle` |
| `is_pid_alive()` | `/proc/<pid>/stat` | `OpenProcess + GetExitCodeProcess` |
| `resolve_static_dir()` | `readlink("/proc/self/exe")`, `realpath` | `GetModuleFileName`, `_fullpath` |
| `sleep_ms()` | `usleep(ms * 1000)` | `Sleep(ms)` |

### main.cpp refactored

All POSIX-only headers and direct system calls have been removed from `src/main.cpp`.
It now includes only `utils/Platform.hpp` and calls `windmi::platform::*` functions.
```

- [ ] **Step 2: Commit**

```bash
git add docs/windows-platform-support.md
git commit -m "docs: update Windows platform support with implementation details"
```

---

## Task 8: Final Verification

- [ ] **Step 1: Clean build on Linux**

```bash
cmake --build build --clean-first
```

Expected: No errors, no warnings.

- [ ] **Step 2: Run all tests**

```bash
cd build && ctest --output-on-failure
```

Expected: All tests pass (4 existing + Platform tests).

- [ ] **Step 3: Verify binary runs correctly**

```bash
pkill windmi-control 2>/dev/null; sleep 1
cd build && ./windmi-control --ip 192.168.123.10 --web 10000 &
sleep 3
curl -s http://localhost:10000/api/status | grep -o '"deviceOnline":[^,]*'
pkill windmi-control
```

Expected: `"deviceOnline":true`, Ctrl+C shuts down cleanly.

- [ ] **Step 4: Verify no POSIX-only code remains in main.cpp**

```bash
grep -n "flock\|readlink\|usleep\|SIGPIPE\|/proc/\|/tmp/" src/main.cpp
```

Expected: No matches — all POSIX-specific code is now in Platform.cpp.

- [ ] **Step 5: Push to remote**

```bash
git push origin feature/windows-support
```

- [ ] **Step 6: Commit**

```bash
git commit --allow-empty -m "verify: Windows build plan — all tasks verified on Linux"
```

---

## Execution Handoff

**Plan saved to `docs/superpowers/plans/2026-06-06-windows-build-implementation.md`.**

**Two execution options:**

**1. Subagent-Driven (recommended)** - I dispatch a fresh subagent per task, review between tasks, fast iteration

**2. Inline Execution** - Execute tasks in this session using executing-plans, batch execution with checkpoints for review

Which approach?