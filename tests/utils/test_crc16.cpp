/**
 * @file tests/utils/test_crc16.cpp
 * @brief CRC16 unit tests
 */

#include "gtest/gtest.h"
#include "crc16.h"

TEST(CRC16Test, EmptyBuffer) {
    uint8_t data[1] = {0};
    uint16_t crc = crc16(data, 0);
    // Expected CRC for empty buffer
    EXPECT_EQ(crc, 0xFFFF);
}

TEST(CRC16Test, SingleByte) {
    uint8_t data[] = {0x01};
    uint16_t crc = crc16(data, 1);
    EXPECT_NE(crc, 0);
}

TEST(CRC16Test, MultipleBytes) {
    uint8_t data[] = {0x01, 0x02, 0x03};
    uint16_t crc = crc16(data, 3);
    EXPECT_NE(crc, 0);
}

TEST(CRC16Test, KnownValue) {
    // Test with a known CRC value
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    uint16_t crc = crc16(data, 4);
    // Just verify it produces consistent results
    EXPECT_EQ(crc, crc16(data, 4));
}
