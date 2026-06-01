/**
 * @file tests/modbus/test_modbus_client.cpp
 * @brief Modbus Client unit tests
 */

#include <gtest/gtest.h>
#include "modbus/ModbusClient.hpp"
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