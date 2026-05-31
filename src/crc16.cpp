/**
 * @file src/crc16.cpp
 * @brief CRC16 checksum implementation
 */

#include "crc16.h"

uint16_t crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    size_t i, j;
    
    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc = crc >> 1;
            }
        }
    }
    
    return crc;
}
