add_executable(test_core
    test_control_loop.cpp
    test_status_monitor.cpp
)
target_link_libraries(test_core PRIVATE windmi_core windmi_modbus windmi_utils GTest::gtest_main)
target_include_directories(test_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
