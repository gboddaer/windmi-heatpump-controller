add_executable(test_core
    test_control_loop.cpp
    test_control_loop_errors.cpp
    test_status_monitor.cpp
    test_selftest.cpp
)
target_link_libraries(test_core PRIVATE windmi_core windmi_modbus windmi_utils GTest::gtest_main)
target_include_directories(test_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)

target_sources(test_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src/selftest.cpp)

target_include_directories(test_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

add_executable(test_main_args
    test_main_args.cpp
)
target_link_libraries(test_main_args PRIVATE windmi_utils GTest::gtest_main)
target_include_directories(test_main_args PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)

add_executable(test_main_signal
    test_main_signal.cpp
)
target_link_libraries(test_main_signal PRIVATE windmi_utils GTest::gtest_main)
target_include_directories(test_main_signal PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
