add_executable(test_utils
    test_config.cpp
    test_json_helpers.cpp
    test_crc16.cpp
    test_logger.cpp
    test_platform.cpp
)
target_link_libraries(test_utils PRIVATE windmi_utils mongoose GTest::gtest_main GTest::gtest Threads::Threads)
target_include_directories(test_utils PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)

# Register tests
add_test(NAME test_utils COMMAND test_utils)
