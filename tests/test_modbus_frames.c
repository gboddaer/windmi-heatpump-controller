#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../include/crc16.h"

static int test_count = 0;
static int pass_count = 0;

#define TEST(name) do { test_count++; printf("  TEST: %s ...", #name); } while(0)
#define PASS() do { pass_count++; printf(" PASS\n"); } while(0)
#define FAIL(msg) do { printf(" FAIL: %s\n", msg); } while(0)

extern void build_read_frame(uint8_t *frame, uint8_t slave_id, uint16_t address, uint16_t count);
extern void build_write_frame(uint8_t *frame, uint8_t slave_id, uint16_t address, uint16_t value);

static void test_read_frame_outdoor_temp(void) {
    TEST(read_frame_outdoor_temp);
    uint8_t frame[8];
    build_read_frame(frame, 0x0B, 0x0001, 0x0001);
    uint8_t expected[] = {0x0B, 0x03, 0x00, 0x01, 0x00, 0x01, 0xD5, 0x60};
    if (memcmp(frame, expected, 8) == 0) { PASS(); }
    else {
        FAIL("frame mismatch");
        printf("    got:      "); for (int i = 0; i < 8; i++) printf("%02X ", frame[i]);
        printf("\n    expected: "); for (int i = 0; i < 8; i++) printf("%02X ", expected[i]);
        printf("\n");
    }
}

static void test_read_frame_running_mode(void) {
    TEST(read_frame_running_mode);
    uint8_t frame[8];
    build_read_frame(frame, 0x0B, 0x002D, 0x0001);
    uint8_t expected[] = {0x0B, 0x03, 0x00, 0x2D, 0x00, 0x01, 0x14, 0xA9};
    if (memcmp(frame, expected, 8) == 0) { PASS(); }
    else {
        FAIL("frame mismatch");
        printf("    got:      "); for (int i = 0; i < 8; i++) printf("%02X ", frame[i]);
        printf("\n    expected: "); for (int i = 0; i < 8; i++) printf("%02X ", expected[i]);
        printf("\n");
    }
}

static void test_write_frame_structure(void) {
    TEST(write_frame_dhw_setpoint_500);
    uint8_t frame[8];
    build_write_frame(frame, 0x0B, 0x0194, 500);
    if (frame[0] == 0x0B && frame[1] == 0x06 && frame[2] == 0x01 && frame[3] == 0x94 && frame[4] == 0x01 && frame[5] == 0xF4) {
        uint16_t crc = crc16_modbus(frame, 6);
        uint16_t frame_crc = (uint16_t)frame[6] | ((uint16_t)frame[7] << 8);
        if (crc == frame_crc) { PASS(); }
        else { FAIL("CRC mismatch"); }
    } else { FAIL("structure wrong"); }
}

static void test_read_frame_crc_consistent(void) {
    TEST(read_frame_crc_consistency);
    uint8_t frame[8];
    build_read_frame(frame, 0x0B, 0x0001, 0x0001);
    uint16_t calculated = crc16_modbus(frame, 6);
    uint16_t in_frame = (uint16_t)frame[6] | ((uint16_t)frame[7] << 8);
    if (calculated == in_frame) { PASS(); }
    else { FAIL("CRC not consistent"); }
}

int main(void) {
    printf("=== Modbus Frame Building Tests ===\n\n");
    test_read_frame_outdoor_temp();
    test_read_frame_running_mode();
    test_write_frame_structure();
    test_read_frame_crc_consistent();
    printf("\nResults: %d/%d passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}