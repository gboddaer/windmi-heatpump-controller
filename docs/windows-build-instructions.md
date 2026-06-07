# Windows Build Instructions

## Prerequisites

To build Windmi Controller for Windows, you need:

1. **CMake 3.16+**
2. **MinGW-w64 with POSIX threads** (for cross-compilation on Linux or native builds)
3. **Git**

## Cross-Compilation on Linux (Recommended for MinGW)

The project includes a toolchain file for cross-compiling from Linux to Windows using MinGW-w64 with POSIX threading support.

### Install Dependencies (Debian/Ubuntu)

```bash
sudo apt-get install g++-mingw-w64-x86-64-posix gcc-mingw-w64-x86-64-posix cmake
```

> **Important:** Use the `-posix` variant (`x86_64-w64-mingw32-g++-posix`). The default `x86_64-w64-mingw32-g++` uses the `win32` threading model which lacks `<thread>`, `<mutex>`, and `<condition_variable>` support.

### Build Steps

```bash
cd windmi-heatpump-controller

# Clean build
rm -rf build-win64 && mkdir build-win64 && cd build-win64

# Configure (uses provided toolchain file)
cmake .. -G "Unix Makefiles" \
    -DCMAKE_TOOLCHAIN_FILE=../mingw-w64-x86_64.cmake \
    -DCMAKE_BUILD_TYPE=Release

# Build
make -j$(nproc)

# The executable is build-win64/windmi-control.exe
```

### Running on Linux (with Wine)

```bash
wine build-win64/windmi-control.exe --help
```

## Threading Model

The project uses a platform abstraction layer (`include/utils/Platform.hpp`) that wraps threading primitives:
- **POSIX/Linux:** `pthread_mutex_t`, `pthread_cond_t`, `pthread_t`
- **Windows (MinGW):** Same POSIX APIs via MinGW's `winpthreads` library (requires `-posix` toolchain)

Google's `gtest` also requires POSIX threading (it uses `std::mutex` internally), so the `-posix` toolchain is mandatory.

## Windows-Specific Code Paths

The codebase uses these Windows APIs (conditioned on `_WIN32`):

- **Signal handling:** `SetConsoleCtrlHandler()` instead of `signal()`
- **Instance locking:** `CreateMutexA()` instead of `flock()`
- **PID checking:** `OpenProcess()` + `GetExitCodeProcess()` instead of `/proc/<pid>/stat`
- **Path resolution:** `GetModuleFileNameA()` + `_fullpath()` instead of `readlink("/proc/self/exe")`
- **Sleep:** `Sleep(ms)` instead of `usleep()`
- **Timestamps:** `gmtime_s()` instead of `gmtime_r()`
- **Sockets:** Winsock2 with `ws2_32` — see `windmi_recv`, `windmi_send`, `windmi_select` macros in `modbus_client.c`
- **Mongoose:** `#undef poll` after `#include <mongoose.h>` to prevent macro conflict with `WSAPoll`

## Known Limitations

1. **`mingw_gettimeofday`:** Used instead of `clock_gettime(CLOCK_MONOTONIC)` for `ConditionVariable::wait_for` timeouts
2. **`FD_SET` on Windows:** The `nfds` parameter to `select()` is ignored (Windows uses SOCKET-based FD_SET)
3. **`MSG_DONTWAIT`:** Not available on Windows; defined as `0` for MinGW builds

## CI/CD

Windows cross-builds are automated via GitHub Actions on push to `feature/windows-support`.

See `.github/workflows/windows-build.yml` for details.