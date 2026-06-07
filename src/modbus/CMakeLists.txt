add_library(windmi_modbus STATIC
    ModbusClient.cpp
    SimulatedModbusClient.cpp
    ModbusSerialClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/modbus_client.c
    ${CMAKE_CURRENT_SOURCE_DIR}/modbus_rtu_frame.c
    ${CMAKE_CURRENT_SOURCE_DIR}/modbus_serial_client.c
)
target_include_directories(windmi_modbus PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
target_link_libraries(windmi_modbus PUBLIC mongoose windmi_utils)
if(WIN32)
    target_link_libraries(windmi_modbus PUBLIC ws2_32)
endif()
