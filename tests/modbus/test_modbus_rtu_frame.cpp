/**
 * @file tests/modbus/test_modbus_rtu_frame.cpp
 * @brief Modbus RTU frame building tests
 */

#include <gtest/gtest.h>
#include "modbus/modbus_rtu_frame.h"
#include "crc16.h"

/**
 * CRC-16/MODBUS test vector:
 * Input:  {0x01, 0x03, 0x00, 0x00, 0x00, 0x01} (slave=1, func=3, addr=0, count=1)
 * CRC:    0x0A84 (low byte 0x84, high byte 0x0A)
 * Full frame: {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0A}
 */
TEST(ModbusRtuFrameTest, ReadFrameStandardVector) {
    uint8_t frame[8];
    modbus_rtu_build_read_frame(frame, 1, 0, 1);

    // Slave ID
    EXPECT_EQ(frame[0], 0x01);
    // Function code
    EXPECT_EQ(frame[1], 0x03);
    // Address (high byte)
    EXPECT_EQ(frame[2], 0x00);
    // Address (low byte)
    EXPECT_EQ(frame[3], 0x00);
    // Count (high byte)
    EXPECT_EQ(frame[4], 0x00);
    // Count (low byte)
    EXPECT_EQ(frame[5], 0x01);
    // CRC low byte
    EXPECT_EQ(frame[6], 0x84);
    // CRC high byte
    EXPECT_EQ(frame[7], 0x0A);
}

/**
 * CRC-16/MODBUS test vector for write:
 * Input:  {0x01, 0x06, 0x00, 0x00, 0x00, 0x01} (slave=1, func=6, addr=0, value=1)
 * CRC:    0x0A48 (low byte 0x48, high byte 0x0A)
 * Full frame: {0x01, 0x06, 0x00, 0x00, 0x00, 0x01, 0x48, 0x0A}
 */
TEST(ModbusRtuFrameTest, WriteFrameStandardVector) {
    uint8_t frame[8];
    modbus_rtu_build_write_frame(frame, 1, 0, 1);

    // Slave ID
    EXPECT_EQ(frame[0], 0x01);
    // Function code
    EXPECT_EQ(frame[1], 0x06);
    // Address (high byte)
    EXPECT_EQ(frame[2], 0x00);
    // Address (low byte)
    EXPECT_EQ(frame[3], 0x00);
    // Value (high byte)
    EXPECT_EQ(frame[4], 0x00);
    // Value (low byte)
    EXPECT_EQ(frame[5], 0x01);
    // CRC low byte
    EXPECT_EQ(frame[6], 0x48);
    // CRC high byte
    EXPECT_EQ(frame[7], 0x0A);
}

/**
 * Test with different slave ID (Windmi uses 11 = 0x0B)
 */
TEST(ModbusRtuFrameTest, ReadFrameSlaveId) {
    uint8_t frame[8];
    modbus_rtu_build_read_frame(frame, 11, 100, 1);  // Windmi slave ID

    EXPECT_EQ(frame[0], 0x0B);  // Slave ID 11
    EXPECT_EQ(frame[1], 0x03);  // Read function
    EXPECT_EQ(frame[2], 0x00);  // Address high
    EXPECT_EQ(frame[3], 0x64);  // Address low (100)
    EXPECT_EQ(frame[4], 0x00);  // Count high
    EXPECT_EQ(frame[5], 0x01);  // Count low
}

/**
 * Test with max register address (65535)
 */
TEST(ModbusRtuFrameTest, ReadFrameMaxAddress) {
    uint8_t frame[8];
    modbus_rtu_build_read_frame(frame, 1, 65535, 1);

    EXPECT_EQ(frame[0], 0x01);
    EXPECT_EQ(frame[1], 0x03);
    EXPECT_EQ(frame[2], 0xFF);  // Address high
    EXPECT_EQ(frame[3], 0xFF);  // Address low
}

/**
 * Test with max count (125 registers - Modbus spec limit)
 */
TEST(ModbusRtuFrameTest, ReadFrameMaxCount) {
    uint8_t frame[8];
    modbus_rtu_build_read_frame(frame, 1, 0, 125);

    EXPECT_EQ(frame[0], 0x01);
    EXPECT_EQ(frame[1], 0x03);
    EXPECT_EQ(frame[2], 0x00);
    EXPECT_EQ(frame[3], 0x00);
    EXPECT_EQ(frame[4], 0x00);
    EXPECT_EQ(frame[5], 0x7D);  // 125 = 0x7D
}

/**
 * Test with max write value (65535)
 */
TEST(ModbusRtuFrameTest, WriteFrameMaxValue) {
    uint8_t frame[8];
    modbus_rtu_build_write_frame(frame, 1, 0, 65535);

    EXPECT_EQ(frame[0], 0x01);
    EXPECT_EQ(frame[1], 0x06);
    EXPECT_EQ(frame[2], 0x00);
    EXPECT_EQ(frame[3], 0x00);
    EXPECT_EQ(frame[4], 0xFF);  // Value high
    EXPECT_EQ(frame[5], 0xFF);  // Value low
}

/**
 * Verify CRC calculation is correct for different inputs
 */
TEST(ModbusRtuFrameTest, CRCVerification) {
    uint8_t frame[8];

    // Test 1: slave=1, addr=0, count=1
    modbus_rtu_build_read_frame(frame, 1, 0, 1);
    uint16_t crc1 = crc16(frame, 6);
    EXPECT_EQ(frame[6], crc1 & 0xFF);
    EXPECT_EQ(frame[7], (crc1 >> 8) & 0xFF);

    // Test 2: slave=11, addr=256, count=10
    modbus_rtu_build_read_frame(frame, 11, 256, 10);
    uint16_t crc2 = crc16(frame, 6);
    EXPECT_EQ(frame[6], crc2 & 0xFF);
    EXPECT_EQ(frame[7], (crc2 >> 8) & 0xFF);

    // Test 3: slave=1, addr=1000, value=500
    modbus_rtu_build_write_frame(frame, 1, 1000, 500);
    uint16_t crc3 = crc16(frame, 6);
    EXPECT_EQ(frame[6], crc3 & 0xFF);
    EXPECT_EQ(frame[7], (crc3 >> 8) & 0xFF);
}

/**
 * Test with slave ID = 255 (maximum valid)
 */
TEST(ModbusRtuFrameTest, ReadFrameMaxSlaveId) {
    uint8_t frame[8];
    modbus_rtu_build_read_frame(frame, 255, 0, 1);

    EXPECT_EQ(frame[0], 0xFF);
    EXPECT_EQ(frame[1], 0x03);
}

/**
 * Test with slave ID = 0 (edge case)
 */
TEST(ModbusRtuFrameTest, ReadFrameZeroSlaveId) {
    uint8_t frame[8];
    modbus_rtu_build_read_frame(frame, 0, 0, 1);

    EXPECT_EQ(frame[0], 0x00);
    EXPECT_EQ(frame[1], 0x03);
}

/**
 * Test with multiple register count
 */
TEST(ModbusRtuFrameTest, ReadFrameMultipleRegisters) {
    uint8_t frame[8];
    modbus_rtu_build_read_frame(frame, 1, 0, 10);

    EXPECT_EQ(frame[0], 0x01);
    EXPECT_EQ(frame[1], 0x03);
    EXPECT_EQ(frame[4], 0x00);
    EXPECT_EQ(frame[5], 0x0A);  // 10 registers
}
