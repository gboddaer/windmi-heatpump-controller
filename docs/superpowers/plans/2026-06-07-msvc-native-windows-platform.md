# MSVC / Native Windows Platform Support Plan

## Goal

Add native Windows/MSVC support while preserving the existing Linux and MinGW builds.

This plan specifically chooses **Option B: PIMPL** for thread, mutex, and condition variable storage so public headers no longer expose pthread or Windows native types.

## Scope

### In scope

- Build with MSVC on `windows-latest` using Visual Studio generator.
- Preserve current Linux behavior.
- Preserve current MinGW cross-build behavior.
- Replace public pthread-backed storage in `Platform.hpp` with PIMPL.
- Implement native Windows threading primitives:
  - `Mutex` using `CRITICAL_SECTION` or `SRWLOCK`.
  - `ConditionVariable` using Windows `CONDITION_VARIABLE`.
  - `Thread` using `_beginthreadex`.
- Keep `ConditionVariable` separate from `Thread`.
- Keep invariant: no `<thread>`, `<mutex>`, `<condition_variable>` outside platform abstraction.
- Audit C/C++ boundaries for MSVC compatibility.
- Add MSVC CI job.

### Out of scope

- Visual Studio project files outside CMake.
- Full production validation on physical Windows serial hardware.
- Replacing MinGW support.
- Replacing the existing Modbus TCP/RTU behavior beyond what is needed for MSVC compilation.

## Current State

The project currently supports Linux and MinGW. The threading abstraction is functionally platform-independent for the current target, but public headers still expose pthread types:

- `pthread_mutex_t` in `Mutex`
- `pthread_t` in `Thread`
- `pthread_cond_t` in `ConditionVariable`
- `<pthread.h>` included from `Platform.hpp`

This is acceptable for MinGW POSIX-thread-model builds, but not for MSVC/native Windows threading.

## Design Choice: Option B, PIMPL

Use opaque implementation objects in `Platform.hpp`:

```cpp
class Mutex {
public:
    Mutex();
    ~Mutex();
    void lock();
    void unlock();
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

private:
    struct Impl;
    Impl* impl_;
    friend class ConditionVariable;
};
```

Apply the same approach to:

- `Mutex`
- `Thread`
- `ConditionVariable`

`UniqueLock` can continue to hold `Mutex*` and `bool owns_`.

### Why PIMPL

- Removes pthread and Windows native types from the public header.
- Avoids exposing `<windows.h>` or `<pthread.h>` to consumers.
- Makes MSVC/native Windows implementation possible without changing users of the abstraction.
- Keeps future platform implementation changes localized to `Platform.cpp`.

### Tradeoffs

- Each primitive allocates an implementation object.
- Move semantics for `Thread` and possibly other classes must transfer `Impl*` safely.
- Requires careful destructor ownership.

The allocation overhead is acceptable for this project because these primitives are not created in high-frequency inner loops.

## Implementation Plan

### Phase 1: Header PIMPL conversion

1. Update `include/utils/Platform.hpp`:
   - Remove `#include <pthread.h>`.
   - Add forward `struct Impl; Impl* impl_;` members for:
     - `Mutex`
     - `Thread`
     - `ConditionVariable`
   - Keep public method signatures unchanged where possible.
   - Keep `LockGuard` and `UniqueLock` unchanged except for any internal access needed by `ConditionVariable`.
   - Do not expose native handles publicly.

2. Ensure `Platform.hpp` includes only portable standard headers:
   - `<cstddef>`
   - `<cstdint>`
   - `<csignal>`
   - `<functional>`
   - `<string>`

3. Add static assertions or tests indirectly by compiling representative consumers that include `Platform.hpp` without OS headers.

### Phase 2: POSIX/MinGW pthread implementation behind PIMPL

1. In `src/utils/Platform.cpp`, define POSIX implementation structs under the non-native-Windows path:

```cpp
struct Mutex::Impl {
    pthread_mutex_t mutex;
};

struct ConditionVariable::Impl {
    pthread_cond_t cond;
};

struct Thread::Impl {
    pthread_t thread{};
    bool joined = false;
    bool detached = false;
};
```

2. Move all pthread access into `Platform.cpp`.

3. Preserve current behavior:
   - `ConditionVariable` uses `CLOCK_MONOTONIC` via `pthread_condattr_setclock`.
   - `wait_for(ms)` calculates monotonic absolute deadlines.
   - `Thread` uses `pthread_create`, `pthread_join`, and `pthread_detach`.

4. Verify Linux tests still pass.

5. Verify MinGW cross-build still passes.

### Phase 3: Native Windows implementation

Add `_WIN32` implementation that is selected only when building with MSVC/native Windows threading.

Recommended platform switch:

```cpp
#if defined(_WIN32) && !defined(__MINGW32__)
#define WINDMI_NATIVE_WINDOWS_THREADS 1
#endif
```

This preserves current MinGW pthread behavior while enabling native Windows under MSVC.

#### Mutex

Use `CRITICAL_SECTION`:

```cpp
struct Mutex::Impl {
    CRITICAL_SECTION cs;
};
```

Implement:

- constructor: `InitializeCriticalSection`
- destructor: `DeleteCriticalSection`
- `lock`: `EnterCriticalSection`
- `unlock`: `LeaveCriticalSection`

#### ConditionVariable

Use Windows `CONDITION_VARIABLE`:

```cpp
struct ConditionVariable::Impl {
    CONDITION_VARIABLE cv;
};
```

Implement:

- constructor: `InitializeConditionVariable`
- destructor: no-op
- `notify_one`: `WakeConditionVariable`
- `notify_all`: `WakeAllConditionVariable`
- `wait`: `SleepConditionVariableCS(&cv, &mutex.cs, INFINITE)`
- `wait_for`: `SleepConditionVariableCS(&cv, &mutex.cs, ms)`

Return behavior for `wait_for`:

- return `true` if woken
- return `false` if `GetLastError() == ERROR_TIMEOUT`
- log or return false for unexpected errors

#### Thread

Use `_beginthreadex`, not `CreateThread`, for C runtime safety.

```cpp
struct Thread::Impl {
    HANDLE handle = nullptr;
    unsigned int thread_id = 0;
    bool joined = false;
    bool detached = false;
};
```

Implementation details:

- Heap-allocate `std::function<void()>` for thread entry.
- Entry point catches exceptions, logs if possible, deletes callable.
- `join()` waits with `WaitForSingleObject`, closes handle, marks joined.
- `detach()` closes handle without waiting, marks detached.
- Destructor detaches any joinable thread to preserve current behavior.
- Move constructor/assignment transfer `Impl*` ownership or transfer native handle state.

### Phase 4: SerialPort native Windows validation

The project already has a Windows serial implementation using:

- `CreateFile`
- `DCB`
- `COMMTIMEOUTS`
- `ReadFile`
- `WriteFile`
- `PurgeComm`
- `CloseHandle`

Review under MSVC for:

1. Header include ordering.
2. Type conversions:
   - `size_t` to `DWORD`
   - `DWORD` to `int`
3. COM port path normalization:
   - `COM1` to `COM9` normally work.
   - `COM10+` should use `\\.\COM10`.
4. Error reporting with `GetLastError`.

Do not change serial protocol behavior unless MSVC compile or runtime safety requires it.

### Phase 5: C/C++ source compatibility audit

MSVC is stricter about C files including C++ headers.

Audit these files:

- `src/modbus_client.c`
- `src/modbus/modbus_serial_client.c`
- `src/selftest.c`
- any `.c` file including `.hpp`

Actions:

1. Prefer compiling files that include C++ headers as C++ by renaming to `.cpp` if safe.
2. Alternatively replace C++ header includes with C-compatible headers.
3. Ensure C-facing APIs stay in `.h` headers with `extern "C"` guards where needed.
4. Keep `PlatformC.h` as the C bridge for platform functionality.

Expected likely follow-up:

- Rename `src/modbus_client.c` to `.cpp` if it depends on C++ logging headers.
- Rename `src/modbus/modbus_serial_client.c` to `.cpp` if it depends on C++ logging headers.
- Or create C-compatible logging tag headers.

Choose the smallest safe change after MSVC compile errors identify exact blockers.

### Phase 6: CMake MSVC support

1. Audit compiler flags:
   - GCC/Clang flags should only apply to non-MSVC compilers.
   - MSVC flags should use `/W4` or equivalent.

2. Ensure required Windows libraries are linked:
   - `ws2_32` for Winsock.
   - Additional Windows libraries only if MSVC build requires them.

3. Ensure no MinGW-specific assumptions apply to MSVC:
   - no `-pthread`
   - no MinGW-only paths
   - no cross-toolchain assumptions

4. Verify with:

```powershell
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -DWINDMI_BUILD_TESTS=ON
cmake --build build-msvc --config Release
ctest --test-dir build-msvc -C Release --output-on-failure
```

### Phase 7: CI

Add a separate MSVC CI job on `windows-latest`.

Suggested workflow step:

```yaml
- name: Configure MSVC
  run: cmake -S . -B build-msvc -G "Visual Studio 17 2022" -DWINDMI_BUILD_TESTS=ON

- name: Build MSVC
  run: cmake --build build-msvc --config Release

- name: Test MSVC
  run: ctest --test-dir build-msvc -C Release --output-on-failure
```

Keep existing jobs:

- Linux build/tests
- MinGW cross-build

CI should prove all three remain healthy.

## Testing Plan

### Required local verification

Run after each major phase:

```bash
rm -rf build-linux
mkdir build-linux
cd build-linux
cmake .. -DCMAKE_BUILD_TYPE=Debug -DWINDMI_BUILD_TESTS=ON
make -j4
ctest --output-on-failure
```

Run MinGW cross-build:

```bash
rm -rf build-win64
mkdir build-win64
cd build-win64
cmake .. -G 'Unix Makefiles' -DCMAKE_TOOLCHAIN_FILE=../mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Debug
make -j4
```

Run package build before final PR if Debian files are touched:

```bash
dpkg-buildpackage -us -uc -b
```

### Required CI verification

- Linux tests pass.
- MinGW cross-build passes.
- MSVC build passes.
- MSVC tests pass if tests are enabled and supported.

### Invariant checks

Run before final commit:

```bash
grep -rn '#include.*<thread>\|#include.*<mutex>\|#include.*<condition_variable>' src include tests
```

Expected: no matches outside `Platform.*` or acceptable comments.

```bash
grep -rn '#include.*<pthread.h>\|pthread_' include
```

Expected: no public header pthread dependency after PIMPL conversion.

```bash
grep -rn '#include.*<windows.h>' include
```

Expected: no public header Windows dependency unless explicitly justified.

## Risks

1. **PIMPL ownership mistakes**
   - Risk: leaks or double frees in move/destruct paths.
   - Mitigation: add tests for move construction/assignment of `Thread`; use clear ownership transfer.

2. **ConditionVariable lock integration**
   - Risk: Windows `SleepConditionVariableCS` requires the exact `CRITICAL_SECTION` backing the locked mutex.
   - Mitigation: `ConditionVariable` accesses `Mutex::Impl` via friendship only.

3. **Thread destructor semantics**
   - Risk: Native Windows handles must be closed exactly once.
   - Mitigation: centralize handle close in `join()`/`detach()` and make destructor call `detach()` if joinable.

4. **C/C++ boundary churn**
   - Risk: renaming `.c` to `.cpp` may affect tests or exported symbols.
   - Mitigation: use `extern "C"` in public C headers; make one file change at a time and verify.

5. **MSVC compiler warnings/errors reveal unrelated issues**
   - Risk: signed/unsigned conversions, unsafe CRT warnings, missing includes.
   - Mitigation: fix only confirmed blockers, avoid broad rewrites.

## Acceptance Criteria

- `Platform.hpp` no longer includes `<pthread.h>` or `<windows.h>`.
- Public platform headers expose no pthread or native Windows threading types.
- Linux build and tests pass.
- MinGW cross-build passes.
- MSVC build passes in CI.
- MSVC tests pass if supported by the test setup.
- No standard-library threading primitives are used outside `Platform.*`.
- Existing serial Modbus and TCP Modbus behavior is preserved.

## Proposed Branch

Use branch:

```text
feature/msvc-native-windows-platform
```

This branch starts from current `main` and is separate from the deleted previous branch.
