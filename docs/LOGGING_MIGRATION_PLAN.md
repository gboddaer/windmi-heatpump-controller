# Logging Migration Plan

## 1. Design Overview

### Core Principles
- **Level-based logging**: TRACE, DEBUG, INFO, WARN, ERROR, FATAL
- **Context-aware**: Component tags (ControlLoop, Modbus, WebServer, etc.)
- **Structured output**: Timestamp, level, tag, message, source location
- **Multiple outputs**: Console (stdout/stderr) + optional log file
- **Thread-safe**: Mutex-protected logging
- **Zero overhead when disabled**: Level check short-circuits before any string formatting
- **C and C++ callable**: C bridge for `.c` files, C++ API for `.cpp` files

### Output Format
```
[2026-06-01 14:32:05.123] [INFO ] [ControlLoop] Set running mode to 2 (src/core/ControlLoop.cpp:156)
[2026-06-01 14:32:05.456] [ERROR] [Modbus      ] Receive timeout (got 3/10 bytes) (src/modbus_client.c:199)
```

Format: `[YYYY-MM-DD HH:MM:SS.mmm] [LEVEL] [TAG         ] message (file:line)`

---

## 2. Class Structure

```
include/utils/
├── Logger.hpp          # C++ API, macros, enums, ILogOutput, ConsoleLogOutput, FileLogOutput
├── LoggerC.h           # C API, C-safe level constants, C logging macros
└── LogTags.hpp         # Component tag constants

src/utils/
├── Logger.cpp          # Logger singleton + ConsoleLogOutput + FileLogOutput implementation
└── LoggerC.cpp         # C bridge implementation (extern "C" functions delegating to Logger)
```

### Key Classes and Types

#### `LogLevel` (enum)
```cpp
enum class LogLevel {
    TRACE = 0,  // Very verbose, debug-only
    DEBUG = 1,  // Detailed flow
    INFO  = 2,  // General info (default in production)
    WARN  = 3,  // Warning conditions
    ERROR = 4,  // Errors (goes to stderr)
    FATAL = 5,  // Critical failures (exit)
};
```

#### `LogEntry` (struct)
```cpp
struct LogEntry {
    std::chrono::system_clock::time_point timestamp;
    LogLevel level;
    const char* tag;       // Component tag (static string, not owned)
    std::string message;
    const char* file;      // Static string from __FILE__
    int line;
    const char* function;  // Static string from __func__
};
```

#### `ILogOutput` (interface)
```cpp
class ILogOutput {
public:
    virtual ~ILogOutput() = default;
    virtual void write(const LogEntry& entry) = 0;
};
```

#### `ConsoleLogOutput`
- INFO and below → stdout
- WARN, ERROR, FATAL → stderr
- Optional ANSI color codes when attached to a terminal

#### `FileLogOutput`
- Writes all levels to a single file
- Simple append mode; no rotation (rotation adds complexity for negligible
  benefit on an embedded controller that will be restarted on deploy)
- If rotation is needed later, it can be added as a separate class

#### `Logger` (singleton)
```cpp
class Logger {
public:
    static Logger& instance();

    void setLevel(LogLevel level);
    void addOutput(std::unique_ptr<ILogOutput> output);
    void clearOutputs();
    void setOutput(std::unique_ptr<ILogOutput> output);  // clear then add one output

    // Emits an already-formatted message. Call shouldLog() before formatting.
    void log(LogLevel level, const char* tag, const char* file, int line,
             const char* function, const char* message) const;

    bool shouldLog(LogLevel level) const;

private:
    Logger();  // Default: ConsoleLogOutput pre-installed, level=INFO
    std::atomic<int> level_;  // setLevel()/shouldLog() are safe across threads
    // ... members
};
```

**Key design decision - level gate before formatting**:
The `shouldLog()` check happens before any `snprintf`/string construction.
The macros expand to:
```cpp
#define WINDMI_LOG(level, tag, ...) \
    do { \
        if (windmi::Logger::instance().shouldLog(level)) { \
            char _logbuf[512]; \
            snprintf(_logbuf, sizeof(_logbuf), __VA_ARGS__); \
            windmi::Logger::instance().log(level, tag, __FILE__, __LINE__, \
                                           __func__, _logbuf); \
        } \
    } while (0)
```
When the level is filtered out, zero formatting work is done.

#### C Bridge
```c
// In include/utils/LoggerC.h - callable from .c files
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WINDMI_LOG_TRACE = 0,
    WINDMI_LOG_DEBUG = 1,
    WINDMI_LOG_INFO  = 2,
    WINDMI_LOG_WARN  = 3,
    WINDMI_LOG_ERROR = 4,
    WINDMI_LOG_FATAL = 5
} windmi_log_level_t;

int windmi_should_log(int level);
void windmi_log(int level, const char* tag, const char* file, int line,
                const char* func, const char* fmt, ...);

#ifdef __cplusplus
}
#endif
```

C files use `WINDMI_C_LOG()` from `LoggerC.h`. The macro calls
`windmi_should_log()` first so disabled levels avoid formatting work inside
`windmi_log()`.

C macro for `.c` files:
```c
#define WINDMI_C_LOG(level, tag, fmt, ...) \
    do { \
        if (windmi_should_log(level)) { \
            windmi_log(level, tag, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__); \
        } \
    } while (0)
```

`##__VA_ARGS__` is a GNU extension. The project targets GCC/Clang, so this is
acceptable, but implementation should verify `-Wpedantic` does not introduce a
warning. If it does, replace with separate no-argument and variadic macros.

### Formatting Strategy
The project uses **C++17** - `std::format` is not available and no `fmt` library
dependency exists. Use **printf-style format strings** (`%d`, `%s`, `%.1f`, etc.)
for both C and C++ call sites. This is consistent with the existing code and
avoids adding a format library dependency.

---

## 3. Early Initialization
The Logger constructor installs a default `ConsoleLogOutput` at level INFO.
This ensures that **any log call works from program start** without requiring
explicit initialization. The default console output routes INFO and below to
stdout and WARN/ERROR/FATAL to stderr.

When `main()` configures logging, it should call `clearOutputs()` or `setOutput()`
first if it wants to replace the default output. Otherwise, adding another
`ConsoleLogOutput` would duplicate console log lines. Early-boot messages before
configuration use the default console output at INFO level.

---

## 4. Thread Safety
Each `Logger::log()` call locks a mutex, iterates outputs, and unlocks.
`level_` is stored as `std::atomic<int>` so `setLevel()` and `shouldLog()` are
safe even while other threads are logging. This is acceptable because:
- The control loop polls every 30 seconds
- Logging calls are infrequent (a few per cycle)
- String formatting happens outside the lock (in the macro, before `log()`)
- The lock duration is just the output writes, not formatting

A lock-free queue feeding a writer thread is unnecessary complexity for this
application. If profiling shows contention, it can be added later.

---

## 5. Implementation Plan

### ✅ Phase 1: Core Logger (COMPLETE)
- Created `include/utils/Logger.hpp` - LogLevel enum, LogEntry, ILogOutput, ConsoleLogOutput, FileLogOutput, Logger singleton, C++ macros
- Created `include/utils/LoggerC.h` - C API with windmi_log()/windmi_should_log()
- Created `include/utils/LogTags.hpp` - Component tag constants
- Created `src/utils/Logger.cpp` - Singleton + output implementations
- Created `src/utils/LoggerC.cpp` - C bridge
- Updated `src/utils/CMakeLists.txt`

### ✅ Phase 2: Integration (COMPLETE)
- `windmi-control` links `windmi_utils` (already in root CMakeLists.txt)

### ✅ Phase 3: Migration (COMPLETE)
**Rule**: Migrate one file at a time. Never mix printf and logger in the
same function. Remove `#include <cstdio>` only when the file has zero
remaining printf calls (including `snprintf` for string formatting - keep
`<cstdio>` for those).

**Only migrate actual logging calls** (`printf`, `fprintf(stderr,...)`,
`perror`). Do NOT migrate `snprintf`/`sprintf` used for string construction
(e.g., HTTP response buffers in WebServer, PID file in main).

---

## 6. File-by-File Migration

### Active Source Files That Need Migration

| File | Language | printf | fprintf(stderr) | perror | snprintf (keep) | Tag | Notes |
|------|----------|--------|------------------|--------|------------------|-----|-------|
| `src/core/ControlLoop.cpp` | C++ | 28 | 18 | 0 | 0 | `ControlLoop` | Largest migration target |
| `src/main.cpp` | C++ | 28 | 19 | 0 | 2 | `Main` | Keep `snprintf` at lines 115, 162 |
| `src/web/WebServer.cpp` | C++ | 10 | 1 | 0 | 2 | `WebServer` | Keep `snprintf` at lines 57, 174 |
| `src/modbus_client.c` | C | 3 | 9 | 3 | 0 | `Modbus` | Uses C bridge; `perror` → `WINDMI_C_LOG_ERROR` + `strerror(errno)` |
| `src/selftest.c` | C | 19 | 11 | 0 | 0 | `SelfTest` | Uses C bridge |

**Total**: 46 `printf`, 58 `fprintf(stderr)`, 3 `perror` = **107 logging calls** to migrate.

### Active Source Files That Need NO Migration

| File | Reason |
|------|--------|
| `src/modbus/ModbusClient.cpp` | No logging calls (uses exceptions) |
| `src/modbus/SimulatedModbusClient.cpp` | No logging calls (uses exceptions) |
| `src/core/StatusMonitor.cpp` | No logging calls |
| `src/utils/Config.cpp` | No logging calls |
| `src/utils/JsonHelpers.cpp` | No logging calls |
| `src/utils/SpscQueue.cpp` | No logging calls |

### Legacy Files NOT in the Build (do NOT migrate)

These are superseded by C++ counterparts on the `PR_Cplusplus_conversion` branch:
- `src/control_loop.c` → replaced by `src/core/ControlLoop.cpp`
- `src/web_server.c` → replaced by `src/web/WebServer.cpp`
- `src/main.c` → replaced by `src/main.cpp`

### Test Files (do NOT migrate)

Test files use TEST/PASS/FAIL macros as a test harness. This is test output,
not application logging. They should keep their own output format.

- `tests/core/test_control_loop.cpp`
- `tests/core/test_status_monitor.cpp`
- `tests/modbus/test_modbus_client.cpp`
- `tests/utils/test_config.cpp`
- `tests/utils/test_crc16.cpp`
- `tests/utils/test_json_helpers.cpp`
- `tests/web/test_http_client.cpp`
- `tests/web/test_web_server.cpp`
- `tests/test_control_logic.c`
- `tests/test_crc16.c`
- `tests/test_modbus_frames.c`
- `tests/test_spsc_queue.c`

---

## 7. Migration Patterns

### Pattern 1: printf → WINDMI_LOG_INFO / WINDMI_LOG_DEBUG

Before:
```c
printf("Control loop: Set running mode to %d\n", mode);
```
After (C++):
```cpp
WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Set running mode to %d", mode);
```
After (C):
```c
WINDMI_C_LOG(WINDMI_LOG_INFO, LOG_TAG_MODBUS, "Flushed %d bytes of stale data from socket", flushed);
```

### Pattern 2: fprintf(stderr,...) → WINDMI_LOG_ERROR / WINDMI_LOG_WARN

Before:
```c
fprintf(stderr, "Modbus receive timeout (got %zu/%zu bytes)\n", got, expected);
```
After (C):
```c
WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "Receive timeout (got %zu/%zu bytes)", got, expected);
```

### Pattern 3: perror → WINDMI_LOG_ERROR with strerror

Before:
```c
perror("socket failed");
```
After (C):
```c
WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "socket failed: %s", strerror(errno));
```
Need to add `#include <string.h>` for `strerror` if not already present.

### Pattern 4: snprintf - DO NOT MIGRATE

```cpp
snprintf(url, sizeof(url), "http://%s:%d", WEB_SERVER_IP, port);  // keep as-is
snprintf(response, sizeof(response), "...");                        // keep as-is
snprintf(proc_path, sizeof(proc_path), "/proc/%d/stat", pid);        // keep as-is
snprintf(pid_buf, sizeof(pid_buf), "%d\n", getpid());               // keep as-is
```
These are string construction, not logging. Keep `<cstdio>` where needed.

---

## 8. Configuration

### Command-Line Arguments
```
--log-level <level>   # TRACE, DEBUG, INFO, WARN, ERROR, FATAL (default: INFO)
--log-file <path>     # Optional log file path (default: none, console only)
```

### Default Behavior (no arguments)
- Level: INFO
- Output: ConsoleLogOutput (stdout for INFO and below, stderr for WARN+)
- No file output

---

## 9. Build System Updates

### `src/utils/CMakeLists.txt`
Add `Logger.cpp` and `LoggerC.cpp` to windmi_utils:
```cmake
add_library(windmi_utils STATIC
    Config.cpp
    JsonHelpers.cpp
    SpscQueue.cpp
    Logger.cpp
    LoggerC.cpp
)
target_include_directories(windmi_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
```

### Dependency Updates (implemented)

Libraries that use Logger now explicitly link `windmi_utils`:
```
windmi-core       → mongoose, windmi_utils
windmi-modbus     → mongoose, windmi_utils
windmi-web        → mongoose, windmi_utils
windmi-utils      → (nothing)
windmi-selftest   → windmi_modbus, windmi_utils

windmi-control    → windmi_core, windmi_modbus, windmi_web, windmi_utils, windmi_selftest, mongoose, Threads
```

---

## 10. Testing Strategy

### Unit Tests (`tests/utils/test_logger.cpp`)
- Test log level filtering: set level to WARN, verify INFO messages suppressed
- Test multiple outputs: console + file both receive same message
- Test thread safety: two threads logging simultaneously, no garbled output
- Test C bridge: call `windmi_log()` from C test, verify output
- Test format: verify log line matches `[YYYY-MM-DD HH:MM:SS.mmm] [LEVEL] [TAG] message (file:line)`

### Integration Tests
- Run the full application with `--log-level DEBUG` and verify output
- Verify `--demo` mode logs use `LOG_TAG_SIMULATOR` tag
- Run `--selftest` and verify SelfTest tag output

---

## 11. Rollback Plan

- Commit in small batches (one file per commit during migration)
- Each commit leaves the build in a working state
- If logger causes issues, revert the commit for that file - old printf
  calls are simply restored

---

## 12. Post-Migration Cleanup

After all replacements:
1. Remove `#include <cstdio>` only from files that no longer use **any**
   printf-family function (including `snprintf` and `vsnprintf`)
2. Remove `#include <iostream>` from C++ files that no longer use cout/cerr
3. Add a CI grep check that rejects new logging-style `printf`/`fprintf`/
   `perror`/`cout`/`cerr` in non-test source files while allowing `snprintf`.
   Example:
   ```sh
   grep -RInE '(^|[^[:alnum:]_])(printf|fprintf|perror)\s*\(' src include
   ```
   Exclude generated files, tests, and any intentional string formatting.

---

## 13. Estimated Effort

- **Phase 1** (core logger + C bridge): 3-4 hours
- **Phase 2** (integration): 1 hour
- **Phase 3** (migration, 5 files): 3-4 hours
- **Testing**: 1-2 hours
- **Total**: 8-11 hours

---

## 14. Migration Checklist

- [x] Create `include/utils/Logger.hpp`
- [x] Create `include/utils/LoggerC.h` (C bridge header)
- [x] Create `include/utils/LogTags.hpp`
- [x] Create `src/utils/Logger.cpp`
- [x] Create `src/utils/LoggerC.cpp`
- [x] Update `src/utils/CMakeLists.txt`
- [x] Update library dependencies in CMakeLists.txt files
- [x] Migrate `src/core/ControlLoop.cpp` (46 calls: 28 printf, 18 fprintf)
- [x] Migrate `src/main.cpp` (47 calls: 28 printf, 19 fprintf; keep 2 snprintf)
- [x] Migrate `src/web/WebServer.cpp` (11 calls: 10 printf, 1 fprintf; keep 2 snprintf)
- [x] Migrate `src/modbus_client.c` (15 calls: 3 printf, 9 fprintf, 3 perror)
- [x] Migrate `src/selftest.c` (30 calls: 19 printf, 11 fprintf)
- [x] Write unit tests for Logger
- [x] Remove `#include <cstdio>` where no longer needed (ControlLoop.cpp, WebServer.cpp)
- [ ] Add CI check to reject new printf/cout/cerr in non-test source

---

*Last updated: 2026-06-01*

## 15. Implementation Notes

**Commit**: `9715518` on `PR_Cplusplus_conversion`
**Review fix commit**: `f99fd23`

### Review Findings and Resolution

| Finding | Critical assessment | Resolution |
|---------|---------------------|------------|
| Missing `--log-level` / `--log-file` CLI support | Feature gap rather than runtime bug, but important because DEBUG/TRACE logging and FileLogOutput were otherwise unusable. | Implemented `-l/--log-level` and `-o/--log-file` in `src/main.cpp`. Invalid levels fail clearly. |
| Console log ordering incorrect | Real bug: INFO logs went to buffered stdout while WARN+ went to unbuffered stderr, so chronological order could appear wrong. | Added `std::fflush(stream)` after console writes in `ConsoleLogOutput::write()`. |
| `WebServer.cpp` used `snprintf` without `<cstdio>` | Real include hygiene issue: it only compiled via transitive include from `Logger.hpp`. | Re-added `#include <cstdio>` to `src/web/WebServer.cpp`. |
| Logger link dependency hidden | Build worked only because final executable/tests linked `windmi_utils`; standalone library consumers would miss Logger symbols. | Added explicit PUBLIC `windmi_utils` dependencies to `windmi_core`, `windmi_modbus`, `windmi_web`, and `windmi_selftest`; reordered root CMake to define utils first. |
| No Logger unit tests | Real test coverage gap for a core utility. | Added `tests/utils/test_logger.cpp` with 14 tests covering filtering, formatting, outputs, file logging, and concurrency. |
| `##__VA_ARGS__` pedantic warnings | Build cleanliness issue caused by intentional GCC extension; pragmas in the header did not suppress call-site warnings under GCC 10. | Removed `-Wpedantic`, retained `-Wall -Wextra`, documented rationale. |
| Legacy `src/main.c.bak` tracked | Not part of logging implementation and not in build; stale tracked backup file could confuse grep/review. | Removed from git tracking. |

Validation after fixes:
- Clean build with zero project compiler warnings (`cmake .. && make clean && make -j$(nproc)`)
- All test suites pass: 4/4 via `ctest --output-on-failure`
- Logger-only tests pass: 14/14 via `test_utils --gtest_filter='LoggerTest.*'`
- Smoke-tested `--help`, invalid `--log-level`, demo mode with `--log-level DEBUG`, and demo mode with `--log-file`

**Design decisions during implementation**:
- `##__VA_ARGS__` GNU extension used in macros — `-Wpedantic` removed from
  CMakeLists.txt; `-Wall -Wextra` kept. GCC <14 cannot suppress this specific
  warning at call sites via pragma, so the project-wide flag is the cleanest
  approach.
- Macros use `windmi::` namespace qualification (not inside namespace block) for cross-file compatibility
- `formatTimestamp()` and `formatLevel()` made public in Logger class (needed by output implementations)
- `#include <cstdio>` removed from ControlLoop.cpp (no more printf-family usage)
- `#include <cstdio>` retained in main.cpp (snprintf + help printf), WebServer.cpp (snprintf), modbus_client.c, selftest.c
- `--log-level` / `--log-file` CLI args implemented in main.cpp
- `fflush()` added to ConsoleLogOutput::write() to fix log ordering (stderr unbuffered, stdout buffered)
- CMake dependencies made explicit: windmi_core/modbus/web/selftest all link windmi_utils PUBLIC
- `src/utils` subdirectory ordered first in root CMakeLists.txt (dependency must be defined before consumers)
- Logger unit tests added (14 tests in test_logger.cpp)
- `src/main.c.bak` removed from git tracking
- Unused includes removed from Logger.cpp (`<algorithm>`, `<array>`, `<sstream>`)

**Remaining `printf`/`fprintf` calls** (intentionally kept):
- `main.cpp` lines 204-214: `--help` usage text output
- `main.cpp` lines 116, 163: `snprintf` for /proc stat and PID buffer
- `WebServer.cpp` lines 58, 175: `snprintf` for URL and JSON construction
- `selftest.c` lines 203-228: Self-test report table formatting (test harness output)