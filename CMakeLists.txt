cmake_minimum_required(VERSION 3.16)
project(WindmiController
    VERSION 1.0.0
    DESCRIPTION "Windmi Heat Pump Controller"
    LANGUAGES C CXX
)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-Wall -Wextra)
endif()

# Options
option(WINDMI_BUILD_TESTS "Build unit tests" ON)
option(WINDMI_BUILD_DOCS "Build documentation" OFF)

# Find pthreads
find_package(Threads REQUIRED)

# Include Google Test
if(WINDMI_BUILD_TESTS)
    include(ExternalProject)
    ExternalProject_Add(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG release-1.12.1
        SOURCE_DIR "${CMAKE_BINARY_DIR}/googletest-src"
        BINARY_DIR "${CMAKE_BINARY_DIR}/googletest-build"
        INSTALL_COMMAND ""
        TEST_COMMAND ""
    )
    ExternalProject_Get_property(googletest source_dir binary_dir)
    include_directories(${source_dir}/googletest/include)
    include_directories(${source_dir}/googlemock/include)
    link_directories(${binary_dir}/googlemock/gtest)
    enable_testing()
endif()

# Include mongoose
include_directories(mongoose)

# Add mongoose library
add_library(mongoose STATIC mongoose/mongoose.c)
target_include_directories(mongoose PUBLIC mongoose)
target_compile_definitions(mongoose PRIVATE MG_ENABLE_LINES=1)

# Utils library must be defined first (Logger dependency used by all other libraries)
add_subdirectory(src/utils)
# Add crc16 to utils library
target_sources(windmi_utils PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/crc16.cpp)

# Core library sources
add_subdirectory(src/core)

# Modbus library sources
add_subdirectory(src/modbus)

# Web library sources
add_subdirectory(src/web)

# Self-test library (C code)
add_library(windmi_selftest STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/selftest.c
)
target_include_directories(windmi_selftest PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(windmi_selftest PUBLIC windmi_modbus windmi_utils)

# Main executable
add_executable(windmi-control src/main.cpp)
target_include_directories(windmi-control PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(windmi-control PRIVATE
    windmi_core
    windmi_modbus
    windmi_web
    windmi_utils
    windmi_selftest
    mongoose
    Threads::Threads
)

# Unit tests
if(WINDMI_BUILD_TESTS)
    add_subdirectory(tests/core)
    add_subdirectory(tests/modbus)
    add_subdirectory(tests/web)
    add_subdirectory(tests/utils)
    
    # Add CRC16 to utils tests
    target_link_libraries(test_utils PRIVATE windmi_utils GTest::gtest_main)
endif()

# Documentation
if(WINDMI_BUILD_DOCS)
    find_package(Doxygen REQUIRED)
    doxygen_add_docs(docs
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/docs
        COMMENT "Generate documentation"
    )
endif()

# Install rules
install(TARGETS windmi-control DESTINATION bin)
install(DIRECTORY include/ DESTINATION include)
