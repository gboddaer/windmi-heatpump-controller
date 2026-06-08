add_executable(test_utils
    test_config.cpp
    test_json_helpers.cpp
    test_crc16.cpp
    test_logger.cpp
    test_platform.cpp
    test_platform_thread.cpp
)
target_link_libraries(test_utils PRIVATE windmi_utils mongoose GTest::gtest_main)
target_include_directories(test_utils PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)

add_executable(test_web_server_static
    ../web/test_web_server_static.cpp
)
target_link_libraries(test_web_server_static PRIVATE windmi_core windmi_modbus windmi_utils GTest::gtest_main)
target_include_directories(test_web_server_static PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)

add_executable(test_web_server_api
    ../web/test_web_server_api.cpp
)
target_link_libraries(test_web_server_api PRIVATE windmi_core windmi_modbus windmi_utils GTest::gtest_main)
target_include_directories(test_web_server_api PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
