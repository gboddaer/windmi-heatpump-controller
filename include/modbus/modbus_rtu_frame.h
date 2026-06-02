/**
 * @file modbus_rtu_frame.h
 * @brief Modbus RTU frame building utilities
 * 
 * Shared between TCP and serial implementations.
 * Both use identical Modbus RTU frame format:
 *   [slave(1)][func(1)][addr(2)][count(2)][crc_lo(1)][crc_hi(1)]
 */

#ifndef MODBUS_RTU_FRAME_H
#define MODBUS_RTU_FRAME_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Build a Modbus RTU Read Holding Registers request frame
 * 
 * Frame format: [slave_id][func][addr_high][addr_low][count_high][count_low][crc_lo][crc_hi]
 * 
 * @param frame Buffer to write frame (minimum 8 bytes)
 * @param slave_id Modbus slave ID (0-255)
 * @param address Starting register address (0-65535)
 * @param count Number of registers to read (1-125)
 */
void modbus_rtu_build_read_frame(uint8_t *frame, uint8_t slave_id, uint16_t address, uint16_t count);

/**
 * Build a Modbus RTU Write Single Register request frame
 * 
 * Frame format: [slave_id][func][addr_high][addr_low][value_high][value_low][crc_lo][crc_hi]
 * 
 * @param frame Buffer to write frame (minimum 8 bytes)
 * @param slave_id Modbus slave ID (0-255)
 * @param address Starting register address (0-65535)
 * @param value Value to write (0-65535)
 */
void modbus_rtu_build_write_frame(uint8_t *frame, uint8_t slave_id, uint16_t address, uint16_t value);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_RTU_FRAME_H */
