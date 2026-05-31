/**
 * @file tests/modbus/test_modbus_client.cpp
 * @brief Modbus Client unit tests
 */

#include "gtest/gtest.h"
#include "modbus/ModbusClient.hpp"

using namespace windmi;

TEST(ModbusClientTest, CreateClient) {
    ModbusClient client("192.168.123.10", 8899, 1);
    // Client should be created but not connected
}

TEST(ModbusClientTest, NotConnectedInitially) {
    ModbusClient client("192.168.123.10", 8899, 1);
    EXPECT_FALSE(client.isConnected());
}

TEST(ModbusClientTest, ExceptionOnReadWhenNotConnected) {
    ModbusClient client("192.168.123.10", 8899, 1);
    EXPECT_THROW(client.readRegister(0x0012), ModbusException);
}

TEST(ModbusClientTest, ExceptionOnWriteWhenNotConnected) {
    ModbusClient client("192.168.123.10", 8899, 1);
    EXPECT_THROW(client.writeRegister(0x0012, 100), ModbusException);
}

TEST(ModbusClientTest, DestructorDoesNotCrash) {
    {
        ModbusClient client("192.168.123.10", 8899, 1);
    }
    // Should not crash
}
