#include "modbus/modbus_rtu_frame.h"
#include "crc16.h"

/**
 * Build a Modbus RTU Read Holding Registers request frame
 *
 * Function code: 0x03 (Read Holding Registers)
 * Frame: [slave_id][func][addr_high][addr_low][count_high][count_low][crc_lo][crc_hi]
 */
void modbus_rtu_build_read_frame(uint8_t *frame, uint8_t slave_id, uint16_t address, uint16_t count) {
    frame[0] = slave_id;
    frame[1] = 0x03;  // Read Holding Registers
    frame[2] = (address >> 8) & 0xFF;
    frame[3] = address & 0xFF;
    frame[4] = (count >> 8) & 0xFF;
    frame[5] = count & 0xFF;

    uint16_t crc = crc16(frame, 6);
    frame[6] = crc & 0xFF;
    frame[7] = (crc >> 8) & 0xFF;
}

/**
 * Build a Modbus RTU Write Single Register request frame
 *
 * Function code: 0x06 (Write Single Register)
 * Frame: [slave_id][func][addr_high][addr_low][value_high][value_low][crc_lo][crc_hi]
 */
void modbus_rtu_build_write_frame(uint8_t *frame, uint8_t slave_id, uint16_t address, uint16_t value) {
    frame[0] = slave_id;
    frame[1] = 0x06;  // Write Single Register
    frame[2] = (address >> 8) & 0xFF;
    frame[3] = address & 0xFF;
    frame[4] = (value >> 8) & 0xFF;
    frame[5] = value & 0xFF;

    uint16_t crc = crc16(frame, 6);
    frame[6] = crc & 0xFF;
    frame[7] = (crc >> 8) & 0xFF;
}
