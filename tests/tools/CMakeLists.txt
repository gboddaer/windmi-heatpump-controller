find_program(NODE_EXECUTABLE node)
if(NODE_EXECUTABLE)
    add_test(NAME test_ui_power
        COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_power.cjs)
    add_test(NAME test_ui_diagnostics
        COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_diagnostics.cjs)
    add_test(NAME test_ui_schedule
        COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_schedule.cjs)
    add_test(NAME test_ui_schedule_defaults
        COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_schedule_defaults.cjs)
    add_test(NAME test_ui_backup_heater
        COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_backup_heater.cjs)
    add_test(NAME test_ui_daily_energy
        COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_daily_energy.cjs)
endif()

find_program(PYTHON_EXECUTABLE NAMES python3 python)
if(PYTHON_EXECUTABLE)
    add_test(NAME test_log_backup_heater
        COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_log_backup_heater.py)
endif()

# probe_registers uses POSIX sockets (arpa/inet.h, unistd.h) — Linux/macOS only
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    add_executable(probe_registers probe_registers.c)
    target_link_libraries(probe_registers PRIVATE windmi_utils)
    target_compile_options(probe_registers PRIVATE -Wno-unused-result)
    if(PYTHON_EXECUTABLE)
        add_executable(test_demo_main demo_main_entry.cpp)
        target_include_directories(test_demo_main PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/mongoose)
        target_link_libraries(test_demo_main PRIVATE windmi_core windmi_modbus windmi_web windmi_utils windmi_selftest mongoose)
        add_test(NAME test_main_schedule_startup
            COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_main_schedule_startup.py $<TARGET_FILE:test_demo_main>)
        set_tests_properties(test_main_schedule_startup PROPERTIES TIMEOUT 90)
    endif()
endif()
