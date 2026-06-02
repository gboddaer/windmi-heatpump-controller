add_executable(test_modbus_client
    test_modbus_client.cpp
)
target_link_libraries(test_modbus_client PRIVATE windmi_modbus windmi_utils GTest::gtest_main)
target_include_directories(test_modbus_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)

add_executable(test_modbus_rtu_frame
    test_modbus_rtu_frame.cpp
)
target_link_libraries(test_modbus_rtu_frame PRIVATE windmi_modbus windmi_utils GTest::gtest_main)
target_include_directories(test_modbus_rtu_frame PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)

add_executable(test_modbus_serial_client
    test_modbus_serial_client.cpp
)
target_link_libraries(test_modbus_serial_client PRIVATE windmi_modbus windmi_utils GTest::gtest_main)
target_include_directories(test_modbus_serial_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
