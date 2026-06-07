# Windows Build Instructions

## Prerequisites

To build Windmi Controller for Windows, you need:

1. **Windows 10/11**
2. **Visual Studio 2022** (recommended) or **MinGW-w64** (64-bit) with pthread support
3. **CMake 3.16+**
4. **Git**

## Option 1: Visual Studio (Recommended)

### Install Visual Studio Build Tools

Download and install from [visualstudio.microsoft.com](https://visualstudio.microsoft.com/):

- **Visual Studio 2022 Community** (free) or
- **Build Tools for Visual Studio**

Ensure these workloads are selected:
- Desktop development with C++
- CMake tools for Windows development

### Build Steps

```powershell
# Clone the repository
git clone https://github.com/your-username/windmi-heatpump-controller.git
cd windmi-heatpump-controller

# Create build directory
mkdir build
cd build

# Configure with Visual Studio generator
cmake .. -G "Visual Studio 17 2022" -A x64

# Build
cmake --build . --config Release

# Run tests
ctest --test-dir build --output-on-failure
```

## Option 2: MinGW-w64 (Limited Support)

### Install MinGW-w64 with pthreads

Note: The MinGW version 10-win32 lacks full pthread support:
- `clock_gettime` and `CLOCK_MONOTONIC` may not be available
- Some POSIX APIs are missing or incomplete

For production builds, **use Visual Studio 2022 instead**. MinGW is only suitable for basic builds with known limitations.

You need a MinGW-w64 with **pthreads-win32** support. Options:

1. **MSYS2** (recommended for MinGW builds):
```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake
```

2. **Win64 Seh** build from [mingw-w64.org](https://www.mingw-w64.org/)

**Warning:** MinGW builds may require additional patches for `clock_gettime` and other POSIX APIs. The CI/CD uses Visual Studio for Windows builds.

### Build Steps

```bash
# Clone the repository
git clone https://github.com/your-username/windmi-heatpump-controller.git
cd windmi-heatpump-controller

# Create build directory
mkdir build
cd build

# Configure with MinGW generator
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release

# Build
make

# Run tests
ctest
```

## Cross-Compilation on Linux (for maintainers)

If you have MinGW installed on Linux with pthread support:

```bash
# Install MinGW-w64 with pthreads (Debian/Ubuntu)
sudo apt-get install g++-mingw-w64-x86-64 gcc-mingw-w64-x86-64

# Use the provided build script
cd windmi-heatpump-controller
./build_windows.sh
```

**Note:** MinGW cross-compilation on Linux has the same limitations as native MinGW builds. For production Windows builds, use Visual Studio or GitHub Actions with Windows runners.

Or manually with proper toolchain:

```bash
mkdir -p build-win64
cd build-win64

# Create toolchain file with pthread support
cat > toolchain.cmake << 'EOF'
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
# Ensure pthread is available
add_compile_definitions(_REENTRANT)
EOF

cmake .. -G "Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake -DCMAKE_BUILD_TYPE=Release
make
```

## Known Windows-Specific Code Paths

The codebase uses these Windows APIs:

- **Signal handling**: `SetConsoleCtrlHandler()` instead of `signal()`
- **Instance locking**: `CreateMutexA()` instead of `flock()`
- **PID checking**: `OpenProcess()` + `GetExitCodeProcess()` instead of `/proc/<pid>/stat`
- **Path resolution**: `GetModuleFileNameA()` + `_fullpath()` instead of `readlink("/proc/self/exe")`
- **Sleep**: `Sleep(ms)` instead of `usleep()`
- **Mutex/Threading**: Platform abstraction using `CRITICAL_SECTION` (Windows) or `pthread_mutex_t` (MinGW)

## Testing on Windows

Run the test suite:

```powershell
cd build
ctest --output-on-failure
```

Run the executable:

```powershell
.\Release\windmi-control.exe --help
```

## CI/CD

Windows builds are automated via GitHub Actions on push to `feature/windows-support`.

See `.github/workflows/windows-build.yml` for details.
