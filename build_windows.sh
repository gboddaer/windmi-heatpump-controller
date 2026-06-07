#!/bin/bash
set -e

echo "=== Building Windmi Controller for Windows (x86_64) ==="

# Create build directory
mkdir -p build-win64
cd build-win64

# Configure with MinGW toolchain
cmake .. \
    -G "Unix Makefiles" \
    -DCMAKE_TOOLCHAIN_FILE=../mingw-w64-x86_64.cmake \
    -DCMAKE_BUILD_TYPE=Release

# Build
make

echo "=== Build complete ==="
echo "Executable: windmi-control.exe"
ls -la windmi-control.exe 2>/dev/null || echo "windmi-control.exe not found (may need linking adjustments)"
