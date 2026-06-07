/**
 * @file crc16.h
 * @brief CRC16 checksum utilities
 */

#ifndef WINDMI_CRC16_H_
#define WINDMI_CRC16_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Calculate CRC16 checksum
 * @param data Data buffer
 * @param len Length of data
 * @return CRC16 checksum
 */
uint16_t crc16(const uint8_t* data, size_t len);

/**
 * @brief Backward-compatible Modbus CRC16 API used by legacy C tests.
 * @param data Data buffer
 * @param len Length of data
 * @return CRC16 checksum
 */
uint16_t crc16_modbus(const uint8_t* data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* WINDMI_CRC16_H */
