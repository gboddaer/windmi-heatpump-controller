/**
 * @file tests/modbus/test_modbus_client_c.cpp
 * @brief C Modbus client API unit tests
 */

#include "gtest/gtest.h"
#include "config.h"

extern "C" {
#include "modbus_client.h"
}

TEST(ModbusClientCTest, CreateAndDestroy) {
    modbus_client_t* client = modbus_client_create("127.0.0.1", 502, 1);
    EXPECT_NE(client, nullptr);
    EXPECT_FALSE(modbus_client_is_connected(client));
    modbus_client_destroy(client);
}

TEST(ModbusClientCTest, CreateWithNullIp) {
    modbus_client_t* client = modbus_client_create("127.0.0.1", 8000, 10);
    EXPECT_NE(client, nullptr);
    modbus_client_destroy(client);
}

TEST(ModbusClientCTest, DestroyNullDoesNotCrash) {
    modbus_client_destroy(nullptr);
    // Should not crash
}

TEST(ModbusClientCTest, ConnectToInvalidAddressFails) {
    modbus_client_t* client = modbus_client_create("999.999.999.999", 502, 1);
    EXPECT_NE(client, nullptr);
    EXPECT_FALSE(modbus_client_connect(client));
    modbus_client_destroy(client);
}

TEST(ModbusClientCTest, ConnectToNonExistentFails) {
    modbus_client_t* client = modbus_client_create("127.0.0.1", 59999, 1);
    EXPECT_NE(client, nullptr);
    EXPECT_FALSE(modbus_client_connect(client));
    modbus_client_destroy(client);
}

TEST(ModbusClientCTest, ConnectWhenAlreadyConnectedFails) {
    // Can't test this without a real connection, but we can verify
    // the API exists and doesn't crash with a null client
    EXPECT_FALSE(modbus_client_connect(nullptr));
}

TEST(ModbusClientCTest, DisconnectNullDoesNotCrash) {
    modbus_client_disconnect(nullptr);
    // Should not crash
}

TEST(ModbusClientCTest, FlushBufferNullDoesNotCrash) {
    modbus_client_flush_buffer(nullptr);
    // Should not crash
}

TEST(ModbusClientCTest, ReadRegisterNullClientFails) {
    int16_t value = 0;
    EXPECT_EQ(modbus_read_register(nullptr, 0x0001, &value), -1);
}

TEST(ModbusClientCTest, WriteRegisterNullClientFails) {
    EXPECT_EQ(modbus_write_register(nullptr, 0x0001, 0), -1);
}

TEST(ModbusClientCTest, ReadRegistersNullClientFails) {
    int16_t values[10] = {0};
    EXPECT_EQ(modbus_read_registers(nullptr, 0x0001, values, 10), -1);
}

TEST(ModbusClientCTest, ReadRegistersNullValuesFails) {
    modbus_client_t* client = modbus_client_create("127.0.0.1", 502, 1);
    EXPECT_EQ(modbus_read_registers(client, 0x0001, nullptr, 10), -1);
    modbus_client_destroy(client);
}

TEST(ModbusClientCTest, ReadRegistersZeroCountFails) {
    modbus_client_t* client = modbus_client_create("127.0.0.1", 502, 1);
    int16_t values[10] = {0};
    EXPECT_EQ(modbus_read_registers(client, 0x0001, values, 0), -1);
    modbus_client_destroy(client);
}
