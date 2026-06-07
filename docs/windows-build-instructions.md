# Windows Build Instructions

## Prerequisites

To build Windmi Controller for Windows, you need:

1. **Windows 10/11**
2. **Visual Studio 2022** or **MinGW-w64** (64-bit)
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
ctest --build-and-test . . --build-generator "Visual Studio 17 2022" -A x64 --build-config Release
```

### Build Output

The executable will be at:
```
build\Release\windmi-control.exe
```

## Option 2: MinGW-w64

### Install MinGW-w64

Download from [mingw-w64.org](https://www.mingw-w64.org/) or use MSYS2:

```bash
# Using MSYS2
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake
```

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

### Build Output

The executable will be at:
```
build\windmi-control.exe
```

## Cross-Compilation on Linux (for maintainers)

If you have MinGW installed on Linux:

```bash
# Install MinGW-w64 (Debian/Ubuntu)
sudo apt-get install g++-mingw-w64-x86-64 gcc-mingw-w64-x86-64

# Use the provided build script
cd windmi-heatpump-controller
./build_windows.sh
```

Or manually:

```bash
mkdir -p build-win64
cd build-win64

cmake .. \
  -G "Unix Makefiles" \
  -DCMAKE_TOOLCHAIN_FILE=../mingw-w64-x86_64.cmake \
  -DCMAKE_BUILD_TYPE=Release

make
```

## Known Windows-Specific Code Paths

The codebase uses these Windows APIs:

- **Signal handling**: `SetConsoleCtrlHandler()` instead of `signal()`
- **Instance locking**: `CreateMutexA()` instead of `flock()`
- **PID checking**: `OpenProcess()` + `GetExitCodeProcess()` instead of `/proc/<pid>/stat`
- **Path resolution**: `GetModuleFileNameA()` + `_fullpath()` instead of `readlink("/proc/self/exe")`
- **Sockets**: Winsock2 (`ws2_32.lib`) instead of POSIX sockets
- **Sleep**: `Sleep(ms)` instead of `usleep()`

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

## Debugging Windows Builds

Common issues:

1. **Linker errors**: Ensure `ws2_32.lib` is linked (CMake handles this)
2. **Missing DLLs**: MinGW builds may need `libwinpthread-1.dll`
3. **Firewall prompts**: Windows Defender may block the server - allow access

## CI/CD

Windows builds are automated via GitHub Actions on push to `feature/windows-support`.

See `.github/workflows/windows-build.yml` for details.
