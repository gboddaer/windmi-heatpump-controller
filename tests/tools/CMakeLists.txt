find_program(NODE_EXECUTABLE node)
if(NODE_EXECUTABLE)
    add_test(NAME test_ui_power
        COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_power.cjs)
    add_test(NAME test_ui_diagnostics
        COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_diagnostics.cjs)
    add_test(NAME test_ui_schedule
        COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_schedule.cjs)
endif()

# probe_registers uses POSIX sockets (arpa/inet.h, unistd.h) — Linux/macOS only
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    add_executable(probe_registers probe_registers.c)
    target_link_libraries(probe_registers PRIVATE windmi_utils)
    target_compile_options(probe_registers PRIVATE -Wno-unused-result)
endif()
