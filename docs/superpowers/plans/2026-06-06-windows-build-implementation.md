# Windows Build Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Windmi Controller compile on Windows by removing POSIX-only assumptions from built code while preserving current Linux behavior.

**Architecture:** Add a small `windmi::platform` abstraction for process/signal/lock/path/sleep behavior used by `src/main.cpp`. Port the built C Modbus TCP client (`src/modbus_client.c`) to use Winsock on Windows while keeping the current POSIX socket path on Linux. Keep platform conditionals localized in `src/utils/Platform.cpp` and `src/modbus_client.c` instead of spreading `#ifdef _WIN32` through application logic.

**Tech Stack:** C++17, C99, CMake 3.16+, Google Test, Mongoose, Winsock2 on Windows

---

## Scope and Non-Goals

### In scope

Built code that currently blocks Windows compilation:

| File | Why it matters |
|------|----------------|
| `src/main.cpp` | Built executable; uses POSIX signal, lock, `/proc`, `readlink`, `realpath`, `usleep` |
| `src/modbus_client.c` | Built into `windmi_modbus`; uses POSIX sockets and `usleep` |
| `CMakeLists.txt` / `src/modbus/CMakeLists.txt` | Need Windows socket libraries |
| `src/utils/CMakeLists.txt` / `tests/utils/CMakeLists.txt` | Need platform abstraction and tests |

### Out of scope

These files are POSIX-heavy but are not built by current CMake targets:

| File | Reason excluded |
|------|-----------------|
| `src/main.c` | Not referenced by `CMakeLists.txt` |
| `src/main.c.bak` | Backup file, not built |
| `src/control_loop.c` | Not built; C++ `src/core/ControlLoop.cpp` is used |
| `src/web_server.c` | Not built; C++ `src/web/WebServer.cpp` is used |

Do not port unbuilt legacy C files in this PR.

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `include/utils/Platform.hpp` | Create | Platform-independent C++ interface |
| `src/utils/Platform.cpp` | Create | POSIX + Windows implementations behind `#ifdef _WIN32` |
| `tests/utils/test_platform.cpp` | Create | Linux-run tests for platform abstraction; Windows CI later validates Windows code path |
| `src/main.cpp` | Modify | Remove direct POSIX APIs and call `windmi::platform::*` |
| `src/modbus_client.c` | Modify | Add Winsock-compatible socket/sleep wrappers |
| `src/utils/CMakeLists.txt` | Modify | Add `Platform.cpp` to `windmi_utils` |
| `tests/utils/CMakeLists.txt` | Modify | Add `test_platform.cpp` |
| `src/modbus/CMakeLists.txt` | Modify | Link `ws2_32` for `windmi_modbus` on Windows |
| `CMakeLists.txt` | Modify | Link `ws2_32` for `mongoose` on Windows; add MSVC flags |
| `docs/windows-platform-support.md` | Modify | Update status and exact implementation notes |

---

## Known Windows blockers to fix

| Code | Current API | Windows fix |
|------|-------------|-------------|
| `src/main.cpp` signal handling | `signal(SIGINT/SIGTERM/SIGPIPE)` | `SetConsoleCtrlHandler`; ignore SIGPIPE only on POSIX |
| `src/main.cpp` instance lock | `/tmp` + `open` + `flock` + `fcntl` | Named mutex via `CreateMutexA`; check `GetLastError()==ERROR_ALREADY_EXISTS` after non-null handle |
| `src/main.cpp` process check | `/proc/<pid>/stat` | `OpenProcess` + `GetExitCodeProcess` |
| `src/main.cpp` executable path | `readlink("/proc/self/exe")`, `realpath` | `GetModuleFileNameA`, `_fullpath` |
| `src/main.cpp` sleeps | `usleep` | `Sleep(ms)` |
| `src/modbus_client.c` sockets | POSIX headers, `int fd`, `close`, `MSG_DONTWAIT`, `errno` | Winsock headers, `SOCKET`, `closesocket`, `WSAGetLastError`, `WSAStartup/WSACleanup` |
| CMake | no Windows socket libs | link `ws2_32` to targets that use sockets |

---

## Task 1: Add Platform Abstraction Interface

**Files:**
- Create: `include/utils/Platform.hpp`

- [ ] **Step 1: Create `include/utils/Platform.hpp`**

```cpp
#pragma once

#include <csignal>
#include <string>

namespace windmi::platform {

/** Install Ctrl+C / termination handlers and set *running_flag = 0 on shutdown request. */
void install_signal_handlers(volatile sig_atomic_t* running_flag);

/** Acquire exclusive instance lock. Returns false when another instance is running. */
bool acquire_instance_lock(bool force = false);

/** Release instance lock if currently held. Safe to call more than once. */
void release_instance_lock();

/** Check whether a PID appears alive on the current platform. */
bool is_pid_alive(int pid);

/** Resolve static directory as-is, relative to executable, or one level above executable. */
std::string resolve_static_dir(const std::string& dir);

/** Cross-platform millisecond sleep. */
void sleep_ms(unsigned int ms);

/** Test hook: override lock path/name so tests do not use the production lock. */
void set_instance_lock_name_for_test(const std::string& lock_name);

/** Test hook: clear lock override. */
void clear_instance_lock_name_for_test();

}  // namespace windmi::platform
```

- [ ] **Step 2: Verify header syntax**

Run:

```bash
echo '#include "utils/Platform.hpp"' | g++ -x c++ -std=c++17 -I include -fsyntax-only -
```

Expected: command exits 0.

- [ ] **Step 3: Commit**

```bash
git add include/utils/Platform.hpp
git commit -m "feat: add cross-platform Platform interface"
```

---

## Task 2: Add Failing Platform Tests

**Files:**
- Create: `tests/utils/test_platform.cpp`
- Modify: `tests/utils/CMakeLists.txt`

- [ ] **Step 1: Add `test_platform.cpp` to `tests/utils/CMakeLists.txt`**

Change the `add_executable(test_utils ...)` source list to:

```cmake
add_executable(test_utils
    test_config.cpp
    test_json_helpers.cpp
    test_crc16.cpp
    test_logger.cpp
    test_platform.cpp
)
```

- [ ] **Step 2: Create `tests/utils/test_platform.cpp`**

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

namespace {

int current_pid() {
#ifdef _WIN32
    return _getpid();
#else
    return getpid();
#endif
}

}  // namespace

class PlatformTest : public ::testing::Test {
protected:
    void SetUp() override {
        windmi::platform::set_instance_lock_name_for_test("windmi-controller-test-lock");
    }

    void TearDown() override {
        windmi::platform::release_instance_lock();
        windmi::platform::clear_instance_lock_name_for_test();
    }
};

TEST_F(PlatformTest, InstallSignalHandlersDoesNotChangeFlagImmediately) {
    volatile sig_atomic_t running = 1;
    windmi::platform::install_signal_handlers(&running);
    EXPECT_EQ(running, 1);
}

TEST_F(PlatformTest, IsPidAliveCurrentProcess) {
    EXPECT_TRUE(windmi::platform::is_pid_alive(current_pid()));
}

TEST_F(PlatformTest, IsPidAliveRejectsInvalidPid) {
    EXPECT_FALSE(windmi::platform::is_pid_alive(-1));
}

TEST_F(PlatformTest, AcquireReleaseAndReacquireLock) {
    EXPECT_TRUE(windmi::platform::acquire_instance_lock(false));
    windmi::platform::release_instance_lock();
    EXPECT_TRUE(windmi::platform::acquire_instance_lock(false));
}

TEST_F(PlatformTest, ReleaseLockWithoutAcquireIsNoop) {
    windmi::platform::release_instance_lock();
    SUCCEED();
}

TEST_F(PlatformTest, ResolveStaticDirDotReturnsNonEmptyPath) {
    std::string resolved = windmi::platform::resolve_static_dir(".");
    EXPECT_FALSE(resolved.empty());
}

TEST_F(PlatformTest, ResolveStaticDirMissingPathReturnsInput) {
    const std::string missing = "/nonexistent/windmi/path/xyz";
    EXPECT_EQ(windmi::platform::resolve_static_dir(missing), missing);
}

TEST_F(PlatformTest, SleepMsReturns) {
    windmi::platform::sleep_ms(1);
    SUCCEED();
}
```

- [ ] **Step 3: Run build and verify expected failure**

Run:

```bash
cmake --build build 2>&1 | tee /tmp/windmi-platform-test-build.log
grep -E "undefined reference|unresolved external" /tmp/windmi-platform-test-build.log | head
```

Expected: linker errors for missing `windmi::platform::*` functions.

- [ ] **Step 4: Commit failing tests**

```bash
git add tests/utils/test_platform.cpp tests/utils/CMakeLists.txt
git commit -m "test: add Platform abstraction tests"
```

---

## Task 3: Implement Platform.cpp and Wire Into Utils

**Files:**
- Create: `src/utils/Platform.cpp`
- Modify: `src/utils/CMakeLists.txt`

- [ ] **Step 1: Add `Platform.cpp` to `src/utils/CMakeLists.txt`**

Change the source list to:

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

- [ ] **Step 2: Create `src/utils/Platform.cpp`**

```cpp
#include "utils/Platform.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <process.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "utils/Logger.hpp"
#include "utils/LogTags.hpp"

namespace {

volatile sig_atomic_t* g_running_flag = nullptr;
std::string g_test_lock_name;

#ifdef _WIN32
static HANDLE g_lock_mutex = NULL;

static std::string lock_name() {
    return g_test_lock_name.empty() ? std::string("Global\\windmi-controller") : g_test_lock_name;
}

static BOOL WINAPI console_ctrl_handler(DWORD dwCtrlType) {
    if (dwCtrlType == CTRL_C_EVENT ||
        dwCtrlType == CTRL_BREAK_EVENT ||
        dwCtrlType == CTRL_CLOSE_EVENT) {
        if (g_running_flag) *g_running_flag = 0;
        return TRUE;
    }
    return FALSE;
}

#else
static int g_lock_fd = -1;

static std::string lock_file_path() {
    return g_test_lock_name.empty() ? std::string("/tmp/windmi-controller.lock") : g_test_lock_name;
}

static void posix_signal_handler(int) {
    if (g_running_flag) *g_running_flag = 0;
}
#endif

}  // anonymous namespace

namespace windmi::platform {

void set_instance_lock_name_for_test(const std::string& lock_name) {
    g_test_lock_name = lock_name;
}

void clear_instance_lock_name_for_test() {
    g_test_lock_name.clear();
}

void install_signal_handlers(volatile sig_atomic_t* running_flag) {
    g_running_flag = running_flag;
#ifdef _WIN32
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
#else
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = posix_signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    signal(SIGPIPE, SIG_IGN);
#endif
}

bool is_pid_alive(int pid) {
    if (pid <= 0) return false;
#ifdef _WIN32
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (process == NULL) return false;
    DWORD exit_code = 0;
    bool alive = GetExitCodeProcess(process, &exit_code) && exit_code == STILL_ACTIVE;
    CloseHandle(process);
    return alive;
#else
    char proc_path[64];
    snprintf(proc_path, sizeof(proc_path), "/proc/%d/stat", pid);
    struct stat st;
    return (stat(proc_path, &st) == 0);
#endif
}

bool acquire_instance_lock(bool force) {
#ifdef _WIN32
    (void)force;
    const std::string name = lock_name();
    g_lock_mutex = CreateMutexA(NULL, TRUE, name.c_str());
    if (g_lock_mutex == NULL) {
        WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Failed to create lock mutex: error %lu", GetLastError());
        return false;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Another instance is already running");
        CloseHandle(g_lock_mutex);
        g_lock_mutex = NULL;
        return false;
    }
    WINDMI_LOG_INFO(LOG_TAG_MAIN, "Lock acquired (PID: %d)", _getpid());
    return true;
#else
    const std::string path = lock_file_path();
    if (force) {
        struct stat st;
        if (stat(path.c_str(), &st) == 0) {
            WINDMI_LOG_INFO(LOG_TAG_MAIN, "--force: removing stale lock file %s", path.c_str());
            unlink(path.c_str());
        }
    }

    g_lock_fd = open(path.c_str(), O_CREAT | O_RDWR, 0644);
    if (g_lock_fd < 0) {
        WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Failed to open lock file %s: %s", path.c_str(), strerror(errno));
        return false;
    }

    int flags = fcntl(g_lock_fd, F_GETFD);
    if (flags >= 0) fcntl(g_lock_fd, F_SETFD, flags | FD_CLOEXEC);

    if (flock(g_lock_fd, LOCK_EX | LOCK_NB) < 0) {
        if (errno == EWOULDBLOCK) {
            char pid_buf[32] = {};
            lseek(g_lock_fd, 0, SEEK_SET);
            ssize_t n = read(g_lock_fd, pid_buf, sizeof(pid_buf) - 1);
            (void)n;
            int existing_pid = atoi(pid_buf);
            if (existing_pid > 0 && is_pid_alive(existing_pid)) {
                WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Another instance is already running (PID %d)", existing_pid);
            } else if (existing_pid > 0) {
                WINDMI_LOG_ERROR(LOG_TAG_MAIN, "Lock held by stale process %d (not running)", existing_pid);
                WINDMI_LOG_WARN(LOG_TAG_MAIN, "Remove %s or use --force to override", path.c_str());
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
    if (g_lock_mutex != NULL) {
        ReleaseMutex(g_lock_mutex);
        CloseHandle(g_lock_mutex);
        g_lock_mutex = NULL;
        WINDMI_LOG_INFO(LOG_TAG_MAIN, "Lock released");
    }
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
    DWORD attrs = GetFileAttributesA(dir.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        char resolved[_MAX_PATH];
        if (_fullpath(resolved, dir.c_str(), _MAX_PATH)) return resolved;
        return dir;
    }

    char exe_path[_MAX_PATH];
    if (GetModuleFileNameA(NULL, exe_path, _MAX_PATH) > 0) {
        char* last_slash = strrchr(exe_path, '\\');
        if (!last_slash) last_slash = strrchr(exe_path, '/');
        if (last_slash) {
            *last_slash = '\0';
            std::string candidate = std::string(exe_path) + "\\" + dir;
            attrs = GetFileAttributesA(candidate.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                char resolved[_MAX_PATH];
                if (_fullpath(resolved, candidate.c_str(), _MAX_PATH)) return resolved;
                return candidate;
            }
            std::string candidate2 = std::string(exe_path) + "\\..\\" + dir;
            attrs = GetFileAttributesA(candidate2.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                char resolved[_MAX_PATH];
                if (_fullpath(resolved, candidate2.c_str(), _MAX_PATH)) return resolved;
                return candidate2;
            }
        }
    }
#else
    struct stat st;
    if (stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        char resolved[PATH_MAX];
        if (realpath(dir.c_str(), resolved)) return resolved;
        return dir;
    }

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
                if (realpath(candidate.c_str(), resolved)) return resolved;
                return candidate;
            }
            std::string candidate2 = std::string(exe_path) + "/../" + dir;
            if (stat(candidate2.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                char resolved[PATH_MAX];
                if (realpath(candidate2.c_str(), resolved)) return resolved;
                return candidate2;
            }
        }
    }
#endif

    WINDMI_LOG_WARN(LOG_TAG_MAIN, "Static directory not found: %s", dir.c_str());
    return dir;
}

void sleep_ms(unsigned int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

}  // namespace windmi::platform
```

- [ ] **Step 3: Run tests**

```bash
cmake --build build --clean-first
cd build && ctest --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/utils/Platform.cpp src/utils/CMakeLists.txt
git commit -m "feat: implement cross-platform Platform abstraction"
```

---

## Task 4: Refactor main.cpp to Use Platform

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Replace POSIX headers with Platform include**

In `src/main.cpp`, keep standard headers and add `utils/Platform.hpp`:

```cpp
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <climits>

#include "utils/Platform.hpp"
```

Delete these POSIX-only includes from `src/main.cpp`:

```cpp
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
```

- [ ] **Step 2: Delete main.cpp-local POSIX helpers**

Delete these from `src/main.cpp`:

```cpp
#define LOCK_FILE "/tmp/windmi-controller.lock"
static int g_lock_fd = -1;
static std::string resolve_static_dir(const std::string& dir) { ... }
static void signal_handler(int sig) { ... }
static bool is_pid_alive(pid_t pid) { ... }
static int acquire_lock() { ... }
static void release_lock() { ... }
```

Keep:

```cpp
static volatile sig_atomic_t g_running = 1;
```

Replace the old `atexit_release_lock()` body with:

```cpp
static void atexit_release_lock() {
    windmi::platform::release_instance_lock();
}
```

- [ ] **Step 3: Replace lock acquisition and force handling**

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

- [ ] **Step 4: Replace signal setup**

Replace:

```cpp
signal(SIGINT, signal_handler);
signal(SIGTERM, signal_handler);
signal(SIGPIPE, SIG_IGN);
```

With:

```cpp
windmi::platform::install_signal_handlers(&g_running);
```

- [ ] **Step 5: Replace static-dir resolution**

Replace:

```cpp
std::string resolved_static_dir = resolve_static_dir(static_dir);
```

With:

```cpp
std::string resolved_static_dir = windmi::platform::resolve_static_dir(static_dir);
```

- [ ] **Step 6: Replace lock release calls**

Replace every remaining:

```cpp
release_lock();
```

With:

```cpp
windmi::platform::release_instance_lock();
```

- [ ] **Step 7: Replace `usleep` calls**

Replace:

```cpp
usleep(150000);
```

With:

```cpp
windmi::platform::sleep_ms(150);
```

Replace:

```cpp
usleep(100000);
```

With:

```cpp
windmi::platform::sleep_ms(100);
```

- [ ] **Step 8: Verify no POSIX-only symbols remain in `src/main.cpp`**

Run:

```bash
grep -nE 'flock|readlink|realpath|usleep|SIGPIPE|SIGTERM|SIGINT|/proc/|/tmp/|<unistd.h>|<fcntl.h>|<sys/file.h>' src/main.cpp || true
```

Expected: no matches, except comments if any were intentionally retained. Remove matching comments if they imply stale implementation details.

- [ ] **Step 9: Build and test**

```bash
cmake --build build --clean-first
cd build && ctest --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 10: Commit**

```bash
git add src/main.cpp
git commit -m "refactor: move main platform calls behind Platform abstraction"
```

---

## Task 5: Port Built C Modbus Client to Winsock

**Files:**
- Modify: `src/modbus_client.c`

This file is built into `windmi_modbus`, so Windows support is incomplete until this task is done.

- [ ] **Step 1: Add platform socket headers and wrappers**

Replace the current POSIX socket include block near the top:

```c
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
```

With:

```c
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET windmi_socket_t;
#define WINDMI_INVALID_SOCKET INVALID_SOCKET
#define windmi_close_socket closesocket
#define windmi_socket_error WSAGetLastError()
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0
#endif
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
typedef int windmi_socket_t;
#define WINDMI_INVALID_SOCKET (-1)
#define windmi_close_socket close
#define windmi_socket_error errno
#endif
```

- [ ] **Step 2: Add sleep wrapper**

Near constants/macros, add:

```c
static void windmi_sleep_ms(int ms) {
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    usleep((useconds_t)ms * 1000);
#endif
}
```

- [ ] **Step 3: Update socket field type and Windows init flag**

Change struct field:

```c
int socket_fd;
```

To:

```c
windmi_socket_t socket_fd;
#ifdef _WIN32
bool wsa_started;
#endif
```

In `modbus_client_create`, replace:

```c
client->socket_fd = -1;
```

With:

```c
client->socket_fd = WINDMI_INVALID_SOCKET;
#ifdef _WIN32
client->wsa_started = false;
#endif
```

- [ ] **Step 4: Initialize Winsock in `modbus_client_connect`**

At the start of `modbus_client_connect`, before `socket(...)`, add:

```c
#ifdef _WIN32
    if (!client->wsa_started) {
        WSADATA wsa_data;
        int wsa_ret = WSAStartup(MAKEWORD(2, 2), &wsa_data);
        if (wsa_ret != 0) {
            WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "WSAStartup failed: %d", wsa_ret);
            return false;
        }
        client->wsa_started = true;
    }
#endif
```

- [ ] **Step 5: Replace socket create/error checks**

Replace:

```c
int sock = socket(AF_INET, SOCK_STREAM, 0);
if (sock < 0) {
    WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "socket failed: %s", strerror(errno));
    return false;
}
```

With:

```c
windmi_socket_t sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
if (sock == WINDMI_INVALID_SOCKET) {
#ifdef _WIN32
    WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "socket failed: %d", windmi_socket_error);
    if (client->wsa_started) {
        WSACleanup();
        client->wsa_started = false;
    }
#else
    WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "socket failed: %s", strerror(errno));
#endif
    return false;
}
```

- [ ] **Step 6: Replace `inet_addr` with `inet_pton` compatible block**

Replace:

```c
server_addr.sin_addr.s_addr = inet_addr(client->ip);
```

With:

```c
if (inet_pton(AF_INET, client->ip, &server_addr.sin_addr) != 1) {
    WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "invalid IP address: %s", client->ip);
    windmi_close_socket(sock);
#ifdef _WIN32
    if (client->wsa_started) {
        WSACleanup();
        client->wsa_started = false;
    }
#endif
    return false;
}
```

- [ ] **Step 7: Replace `close` and connect error handling**

Replace `close(sock)` with `windmi_close_socket(sock)`.

Replace connect error logging:

```c
WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "connect failed: %s", strerror(errno));
```

With:

```c
#ifdef _WIN32
WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "connect failed: %d", windmi_socket_error);
#else
WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "connect failed: %s", strerror(errno));
#endif
```

- [ ] **Step 8: Update disconnect and connect-failure cleanup**

Replace the whole `modbus_client_disconnect` function with:

```c
void modbus_client_disconnect(modbus_client_t *client) {
    if (!client) return;

    if (client->socket_fd != WINDMI_INVALID_SOCKET) {
        windmi_close_socket(client->socket_fd);
        client->socket_fd = WINDMI_INVALID_SOCKET;
    }

    client->connected = false;

#ifdef _WIN32
    if (client->wsa_started) {
        WSACleanup();
        client->wsa_started = false;
    }
#endif
}
```

For the connect failure block, replace the whole block:

```c
if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "connect failed: %s", strerror(errno));
    close(sock);
    return false;
}
```

With:

```c
if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
#ifdef _WIN32
    WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "connect failed: %d", windmi_socket_error);
#else
    WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "connect failed: %s", strerror(errno));
#endif
    windmi_close_socket(sock);
#ifdef _WIN32
    if (client->wsa_started) {
        WSACleanup();
        client->wsa_started = false;
    }
#endif
    return false;
}
```

This prevents failed Windows connection attempts from leaking a `WSAStartup` reference.

- [ ] **Step 9: Update socket validity checks and select nfds**

Replace:

```c
if (!client || client->socket_fd < 0) return 0;
```

With:

```c
if (!client || client->socket_fd == WINDMI_INVALID_SOCKET) return 0;
```

For `select`, use:

```c
int nfds = 0;
#ifndef _WIN32
nfds = client->socket_fd + 1;
#endif
int ready = select(nfds, &fds, NULL, NULL, &tv);
```

Apply this pattern in both `modbus_client_flush_buffer` and the receive loop.

- [ ] **Step 10: Fix send/recv types**

Replace `ssize_t sent` with:

```c
int sent = send(client->socket_fd, (const char *)frame, (int)len, 0);
```

Replace `ssize_t received` with:

```c
int received = recv(client->socket_fd, (char *)buffer + total_received,
                    (int)(expected_len - total_received), 0);
```

Keep existing logic that checks `sent < 0` and `received <= 0`.

- [ ] **Step 11: Replace retry sleep**

Replace:

```c
usleep(MODBUS_RETRY_DELAY_MS * 1000);
```

With:

```c
windmi_sleep_ms(MODBUS_RETRY_DELAY_MS);
```

- [ ] **Step 12: Build and test on Linux**

```bash
cmake --build build --clean-first
cd build && ctest --output-on-failure
```

Expected: all tests pass; Linux socket behavior unchanged.

- [ ] **Step 13: Commit**

```bash
git add src/modbus_client.c
git commit -m "feat: make C Modbus TCP client portable to Winsock"
```

---

## Task 6: Update CMake for Windows Libraries and MSVC

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `src/modbus/CMakeLists.txt`

- [ ] **Step 1: Add MSVC compiler flags**

Replace the compiler warning block in root `CMakeLists.txt` with:

```cmake
# Compiler warnings
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-Wall -Wextra)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    add_compile_options(/W4 /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
endif()
```

- [ ] **Step 2: Link Winsock to Mongoose on Windows**

After the `mongoose` target is created in root `CMakeLists.txt`, add:

```cmake
if(WIN32)
    target_link_libraries(mongoose PUBLIC ws2_32)
endif()
```

- [ ] **Step 3: Link Winsock to `windmi_modbus`**

Append to `src/modbus/CMakeLists.txt`:

```cmake
if(WIN32)
    target_link_libraries(windmi_modbus PUBLIC ws2_32)
endif()
```

- [ ] **Step 4: Build and test on Linux**

```bash
cmake --build build --clean-first
cd build && ctest --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/modbus/CMakeLists.txt
git commit -m "build: add Windows socket libraries and MSVC flags"
```

---

## Task 7: Update Windows Support Documentation

**Files:**
- Modify: `docs/windows-platform-support.md`

- [ ] **Step 1: Replace the old “only signal handling” conclusion**

Update the report to say Windows support requires and implements two areas:

```markdown
## Implementation Plan Status

Windows support is not only signal handling. The built code contains two portability areas:

1. `src/main.cpp` platform APIs: signals, instance lock, PID check, executable path resolution, sleep.
2. `src/modbus_client.c` sockets: POSIX sockets must become Winsock-compatible.

The implementation plan adds:

| Area | Solution |
|------|----------|
| Main platform APIs | `include/utils/Platform.hpp` + `src/utils/Platform.cpp` |
| C Modbus sockets | Winsock wrappers inside `src/modbus_client.c` |
| Build system | Link `ws2_32` for Mongoose and Modbus on Windows |
```

- [ ] **Step 2: Commit**

```bash
git add docs/windows-platform-support.md
git commit -m "docs: correct Windows support report for Modbus socket portability"
```

---

## Task 8: Final Verification

- [ ] **Step 1: Clean Linux build**

```bash
cmake --build build --clean-first
```

Expected: exits 0.

- [ ] **Step 2: Run tests**

```bash
cd build && ctest --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 3: Verify POSIX-only code is localized**

Run:

```bash
grep -RIn --include='*.c' --include='*.cpp' --include='*.h' --include='*.hpp' \
  -E '#include <(unistd|sys/socket|arpa/inet|netinet/in|sys/file|fcntl|pthread|signal)|flock|readlink|realpath|usleep|SIGPIPE|/proc/|/tmp/' \
  src include tests | grep -v 'src/utils/Platform.cpp' | grep -v 'src/modbus_client.c' | grep -v 'src/main.c' | grep -v 'src/control_loop.c' | grep -v 'src/web_server.c' || true
```

Expected: no matches in currently built modern C++ entrypoint code outside the intended platform-specific files. `src/modbus_client.c` may still contain POSIX code under `#ifndef _WIN32`.

- [ ] **Step 4: Push branch**

```bash
git push origin feature/windows-support
```

---

## Windows Validation Checklist

Run on a Windows machine or CI runner after implementation:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Expected:
- Build succeeds.
- `test_utils` includes and passes `PlatformTest` tests.
- Link succeeds without unresolved Winsock symbols.

---

## Execution Handoff

Plan saved to `docs/superpowers/plans/2026-06-06-windows-build-implementation.md`.

Two execution options:

1. **Subagent-Driven (recommended)** — fresh subagent per task, review between tasks.
2. **Inline Execution** — execute in this session with checkpoints.

Which approach?
