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

**Key design decision — level gate before formatting**:
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
// In include/utils/LoggerC.h — callable from .c files
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
The project uses **C++17** — `std::format` is not available and no `fmt` library
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

### Phase 1: Core Logger (2-3 hours)
1. Create `include/utils/Logger.hpp`
   - `LogLevel` enum
   - `LogEntry` struct
   - `ILogOutput` interface
   - `ConsoleLogOutput` class declaration
   - `FileLogOutput` class declaration
   - `Logger` singleton class declaration
   - C++ macros: `WINDMI_LOG_TRACE`, `WINDMI_LOG_DEBUG`, `WINDMI_LOG_INFO`,
     `WINDMI_LOG_WARN`, `WINDMI_LOG_ERROR`, `WINDMI_LOG_FATAL`
   - Each macro takes `(tag, fmt, ...)`

2. Create `src/utils/Logger.cpp`
   - `Logger` singleton implementation
   - `ConsoleLogOutput::write()` implementation
   - `FileLogOutput` class + `write()` implementation
   - `shouldLog()` and `log()` methods

3. Create `src/utils/LoggerC.cpp`
   - `windmi_log()` C-bridge function with `va_list` + `vsnprintf`
   - C macro header (exposed via a new `include/utils/LoggerC.h`)

4. Create `include/utils/LogTags.hpp`
   - `#define LOG_TAG_CONTROLLOOP "ControlLoop"`
   - `#define LOG_TAG_MODBUS      "Modbus"`
   - `#define LOG_TAG_WEBSERVER   "WebServer"`
   - `#define LOG_TAG_MAIN        "Main"`
   - `#define LOG_TAG_SELFTEST    "SelfTest"`

5. Update `src/utils/CMakeLists.txt` to add `Logger.cpp` and `LoggerC.cpp`

### Phase 2: Integration (1 hour)
1. In `main.cpp`:
   - `#include "utils/Logger.hpp"`
   - After argument parsing, configure logger:
     - `Logger::instance().setLevel(LogLevel::INFO);`
     - `Logger::instance().setOutput(make_unique<ConsoleLogOutput>());` (replace default console)
     - Optionally: `Logger::instance().addOutput(make_unique<FileLogOutput>(path));`
   - Add `--log-level` and `--log-file` CLI arguments

2. Ensure `windmi_utils` (which will contain Logger) is linked by all
   libraries that need it (`windmi_core`, `windmi_modbus`, `windmi_web`,
   `windmi_selftest`)

### Phase 3: Migration (4-6 hours)
**Rule**: Migrate one file at a time. Never mix printf and logger in the
same function. Remove `#include <cstdio>` only when the file has zero
remaining printf calls (including `snprintf` for string formatting — keep
`<cstdio>` for those).

**Only migrate actual logging calls** (`printf`, `fprintf(stderr,...)`,
`perror`). Do NOT migrate `snprintf`/`sprintf` used for string construction
(e.g., HTTP response buffers in WebServer).

---

## 6. File-by-File Migration

### Files That Need Migration (actually have logging calls and are in the build)

| File | Language | Logging Calls | Tag | Notes |
|------|----------|---------------|-----|-------|
| `src/core/ControlLoop.cpp` | C++ | 46 | `ControlLoop` | printf + fprintf(stderr,...) |
| `src/web/WebServer.cpp` | C++ | 11 | `WebServer` | printf + fprintf(stderr,...); skip `snprintf` on lines 57, 174 |
| `src/main.cpp` | C++ | 43 | `Main` | printf + fprintf(stderr,...); skip `snprintf` on line 113 and other string-formatting uses |
| `src/modbus_client.c` | C | 15 | `Modbus` | printf + fprintf(stderr,...) + 3× perror |
| `src/selftest.c` | C | 30 | `SelfTest` | printf + fprintf(stderr,...) |

### Files That Need NO Migration (in the build but have no logging calls)

- `src/modbus/ModbusClient.cpp` — no logging
- `src/core/StatusMonitor.cpp` — no logging
- `src/utils/Config.cpp` — no logging
- `src/utils/JsonHelpers.cpp` — no logging
- `src/utils/SpscQueue.cpp` — no logging

### Legacy Files NOT in the Build (do NOT migrate)
These are superseded by C++ counterparts on the `PR_Cplusplus_conversion` branch:
- `src/control_loop.c` → replaced by `src/core/ControlLoop.cpp`
- `src/web_server.c` → replaced by `src/web/WebServer.cpp`
- `src/main.c` → replaced by `src/main.cpp`

### Test Files (do NOT migrate)
Test files (`tests/test_crc16.c`, `tests/test_control_logic.c`,
`tests/test_modbus_frames.c`, `tests/test_spsc_queue.c`) use TEST/PASS/FAIL
macros as a test harness. This is test output, not application logging.
They should keep their own output format.

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

### Pattern 4: snprintf — DO NOT MIGRATE

```cpp
snprintf(url, sizeof(url), "http://%s:%d", WEB_SERVER_IP, port);  // keep as-is
snprintf(response, sizeof(response), "...");                        // keep as-is
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

### Dependency Updates
`windmi_utils` now contains Logger. Libraries that need logging must link it:
- `windmi_core` — add `windmi_utils`
- `windmi_modbus` — add `windmi_utils`
- `windmi_web` — add `windmi_utils`
- `windmi_selftest` — add `windmi_utils`

---

## 10. Testing Strategy

### Unit Tests
- Test log level filtering: set level to WARN, verify INFO messages suppressed
- Test multiple outputs: console + file both receive same message
- Test thread safety: two threads logging simultaneously, no garbled output
- Test C bridge: call `windmi_log()` from C test, verify output

### Integration Tests
- Verify log line format matches spec
- Verify level-based stdout/stderr routing in ConsoleLogOutput
- Run the full application with `--log-level DEBUG` and verify output

---

## 11. Rollback Plan

- Commit in small batches (one file per commit during migration)
- Each commit leaves the build in a working state
- If logger causes issues, revert the commit for that file — old printf
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

- [ ] Create `include/utils/Logger.hpp`
- [ ] Create `include/utils/LoggerC.h` (C bridge header)
- [ ] Create `include/utils/LogTags.hpp`
- [ ] Create `src/utils/Logger.cpp`
- [ ] Create `src/utils/LoggerC.cpp`
- [ ] Update `src/utils/CMakeLists.txt`
- [ ] Update library dependencies in CMakeLists.txt files
- [ ] Initialize logger in `main.cpp` + add `--log-level` / `--log-file` args
- [ ] Migrate `src/core/ControlLoop.cpp` (46 calls)
- [ ] Migrate `src/web/WebServer.cpp` (11 calls, skip snprintf)
- [ ] Migrate `src/main.cpp` (43 calls, skip snprintf/string formatting)
- [ ] Migrate `src/modbus_client.c` (15 calls including 3 perror)
- [ ] Migrate `src/selftest.c` (30 calls)
- [ ] Write unit tests for Logger
- [ ] Remove unnecessary `#include <cstdio>` / `#include <iostream>`
- [ ] Add CI check to reject new printf/cout/cerr in non-test source

---

*Last updated: 2026-06-01*