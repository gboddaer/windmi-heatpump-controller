add_executable(test_modbus
    test_modbus_client.cpp
    test_modbus_rtu_frame.cpp
    test_modbus_serial_client.cpp
)
target_link_libraries(test_modbus PRIVATE windmi_modbus windmi_utils GTest::gtest_main)
target_include_directories(test_modbus PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)

# Register tests
add_test(NAME test_modbus COMMAND test_modbus)
