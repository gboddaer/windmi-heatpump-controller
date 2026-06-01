/**
 * @file tests/modbus/test_modbus_client.cpp
 * @brief Modbus Client unit tests
 */

#include <gtest/gtest.h>
#include "modbus/ModbusClient.hpp"
#include "modbus/SimulatedModbusClient.hpp"
#include "config.h"

using namespace windmi;

TEST(ModbusClientTest, CreateClient) {
    ModbusClient client(MODBUS_GATEWAY_IP, MODBUS_GATEWAY_PORT, MODBUS_SLAVE_ID);
    // Client should be created but not connected
}

TEST(ModbusClientTest, NotConnectedInitially) {
    ModbusClient client(MODBUS_GATEWAY_IP, MODBUS_GATEWAY_PORT, MODBUS_SLAVE_ID);
    EXPECT_FALSE(client.isConnected());
}

TEST(ModbusClientTest, ExceptionOnReadWhenNotConnected) {
    ModbusClient client(MODBUS_GATEWAY_IP, MODBUS_GATEWAY_PORT, MODBUS_SLAVE_ID);
    EXPECT_THROW(client.readRegister(REG_RUNNING_MODE), ModbusException);
}

TEST(ModbusClientTest, ExceptionOnWriteWhenNotConnected) {
    ModbusClient client(MODBUS_GATEWAY_IP, MODBUS_GATEWAY_PORT, MODBUS_SLAVE_ID);
    EXPECT_THROW(client.writeRegister(REG_RUNNING_MODE, 0), ModbusException);
}

TEST(ModbusClientTest, DestructorDoesNotCrash) {
    {
        ModbusClient client(MODBUS_GATEWAY_IP, MODBUS_GATEWAY_PORT, MODBUS_SLAVE_ID);
    }
    // Should not crash
}

TEST(ModbusClientTest, GetCClient) {
    ModbusClient client(MODBUS_GATEWAY_IP, MODBUS_GATEWAY_PORT, MODBUS_SLAVE_ID);
    void* c_client = client.getCClient();
    // Should return non-null even when not connected (client struct exists)
    EXPECT_NE(c_client, nullptr);
}

// Simulator tests
TEST(SimulatedModbusClientTest, Connect) {
    SimulatedModbusClient client;
    EXPECT_TRUE(client.connect());
    EXPECT_TRUE(client.isConnected());
}

TEST(SimulatedModbusClientTest, InitialValues) {
    SimulatedModbusClient client;
    client.connect();
    
    // Check some initial register values
    EXPECT_EQ(client.readRegister(REG_DEVICE_TYPE), 8);  // Rotenso Windmi 8kW
    EXPECT_EQ(client.readRegister(REG_OUTDOOR_TEMP), 120);  // 12.0 C
    EXPECT_EQ(client.readRegister(REG_RUNNING_MODE), MODE_SET_HEAT_DHW);
}

TEST(SimulatedModbusClientTest, WriteRead) {
    SimulatedModbusClient client;
    client.connect();
    
    client.writeRegister(REG_HEATING_TARGET, 500);  // 50.0 C
    int16_t value = client.readRegister(REG_HEATING_TARGET);
    EXPECT_EQ(value, 500);
}

TEST(SimulatedModbusClientTest, ExceptionWhenNotConnected) {
    SimulatedModbusClient client;
    EXPECT_FALSE(client.isConnected());
    EXPECT_THROW(client.readRegister(REG_RUNNING_MODE), ModbusException);
}