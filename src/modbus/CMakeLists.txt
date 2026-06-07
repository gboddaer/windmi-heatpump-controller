add_library(windmi_modbus STATIC
    ModbusClient.cpp
    SimulatedModbusClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/modbus_client.c
)
target_include_directories(windmi_modbus PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
target_link_libraries(windmi_modbus PUBLIC mongoose windmi_utils)
if(WIN32)
    target_link_libraries(windmi_modbus PUBLIC ws2_32)
endif()
