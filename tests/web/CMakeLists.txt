add_executable(test_web
    test_http_client.cpp
    test_web_server.cpp
)
target_link_libraries(test_web PRIVATE windmi_web windmi_utils GTest::gtest_main)
target_include_directories(test_web PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)

# Register tests
add_test(NAME test_web COMMAND test_web)
