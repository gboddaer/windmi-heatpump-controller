# C++ Conversion Plan for Windmi Controller

## Overview
Convert the embedded C project to a cleaner, more maintainable C++ codebase with:
- Unit tests (Google Test)
- Doxygen documentation
- Modern C++ (C++17)
- Clean separation of concerns
- Better error handling

## Phase 1: Infrastructure Setup

### 1.1 Directory Structure
```
src/
  core/           # Core controller logic (C++)
  modbus/         # Modbus client (C++)
  web/            # Web server (C++)
  utils/          # Utilities (C++)
  main.cpp        # Entry point (minimal C wrapper)
include/
  core/           # Public headers
  modbus/
  web/
  utils/
tests/
  core/
  modbus/
  web/
  utils/
docs/
  doxygen/        # Doxygen config and generated docs
```

### 1.2 Build System
- CMake as build system
- Google Test for unit tests
- Doxygen for documentation

### 1.3 Dependencies
- Mongoose (already included, keep as-is)
- Google Test (add as submodule/subdirectory)
- Doxygen (generate docs during build)

## Phase 2: Core Classes

### 2.1 Modbus Client
- `ModbusClient` class (C++ wrapper around C implementation)
- RAII pattern for connection management
- Error handling with exceptions or error codes
- Thread-safe operations

### 2.2 Status Monitor
- `StatusMonitor` class
- Thread-safe status updates
- Snapshot creation for API

### 2.3 Command Queue
- Thread-safe command queue (SPSC)
- Template-based or type-erased commands
- Proper synchronization

### 2.4 Control Loop
- `ControlLoop` class
- State machine for working modes
- Target temperature management
- Priority logic

## Phase 3: Web Server

### 3.1 HTTP Handler
- HTTP handler class
- Route registration
- JSON request/response helpers

### 3.2 API Endpoints
- `/api/status` - GET
- `/api/set-heating` - POST
- `/api/set-dhw` - POST
- `/api/set-priority` - POST
- `/api/set-mode` - POST

## Phase 4: Configuration

### 4.1 Config Class
- `Config` class for application settings
- Load from file/command line
- Type-safe configuration access

## Phase 5: Unit Tests

### 5.1 Modbus Client Tests
- Connection tests
- Read/write tests
- Error handling tests

### 5.2 Control Loop Tests
- Mode switching tests
- Target temperature tests
- Priority logic tests

### 5.3 Web Server Tests
- Route handler tests
- Request parsing tests
- Response generation tests

## Phase 6: Documentation

### 6.1 Doxygen Setup
- Configure Doxygen for C++ project
- Generate HTML/PDF documentation
- Include class diagrams

### 6.2 Code Comments
- Document public API
- Explain design decisions
- Add usage examples

## Implementation Tasks

### Task 1: Create CMakeLists.txt
- Project setup
- Source groups
- Test configuration
- Documentation generation

### Task 2: Refactor Modbus Client to C++
- Wrap C implementation
- Add C++ convenience methods
- Thread-safe access

### Task 3: Refactor Web Server to C++
- C++ handler classes
- Route table
- JSON helpers

### Task 4: Create Control Loop C++ Class
- State machine
- Thread management
- Command processing

### Task 5: Create Main C++ Wrapper
- Minimal C wrapper for entry
- C++ object initialization

### Task 6: Add Unit Tests
- Google Test integration
- Test fixtures
- Mock objects where needed

### Task 7: Add Doxygen Documentation
- Class documentation
- Module documentation
- API reference

## Notes
- Keep C-compatible interfaces where needed (for Mongoose, pthreads)
- Use RAII for resource management
- Prefer smart pointers
- Use constexpr for configuration
- Implement proper error handling
