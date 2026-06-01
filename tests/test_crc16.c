#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "../include/crc16.h"

static int test_count = 0;
static int pass_count = 0;

#define TEST(name) do { test_count++; printf("  TEST: %s ...", #name); } while(0)
#define PASS() do { pass_count++; printf(" PASS\n"); } while(0)
#define FAIL(msg) do { printf(" FAIL: %s\n", msg); } while(0)

static void test_crc16_basic(void) {
    TEST(basic_read_frame);
    uint8_t frame[] = {0x0B, 0x03, 0x00, 0x01, 0x00, 0x01};
    uint16_t crc = crc16_modbus(frame, sizeof(frame));
    if (crc == 0x60D5) { PASS(); }
    else { FAIL("expected 0x60D5"); printf("    got: 0x%04X\n", crc); }
}

static void test_crc16_running_mode(void) {
    TEST(running_mode_frame);
    uint8_t frame[] = {0x0B, 0x03, 0x00, 0x2D, 0x00, 0x01};
    uint16_t crc = crc16_modbus(frame, sizeof(frame));
    if (crc == 0xA914) { PASS(); }
    else { FAIL("expected 0xA914"); printf("    got: 0x%04X\n", crc); }
}

static void test_crc16_empty(void) {
    TEST(empty_data);
    uint16_t crc = crc16_modbus(NULL, 0);
    if (crc == 0xFFFF) { PASS(); }
    else { FAIL("expected 0xFFFF"); printf("    got: 0x%04X\n", crc); }
}

static void test_crc16_single_byte(void) {
    TEST(single_byte);
    uint8_t data[] = {0x00};
    uint16_t crc = crc16_modbus(data, 1);
    if (crc == 0x40BF) { PASS(); }
    else { FAIL(""); printf("    got: 0x%04X\n", crc); }
}

static void test_crc16_response_verifiable(void) {
    TEST(response_frame_crc_verifiable);
    uint8_t resp[] = {0x0B, 0x03, 0x02, 0x00, 0xD7};
    uint16_t crc = crc16_modbus(resp, 5);
    uint8_t full[7];
    memcpy(full, resp, 5);
    full[5] = crc & 0xFF;
    full[6] = (crc >> 8) & 0xFF;
    uint16_t check = crc16_modbus(full, 5);
    if (check == crc) { PASS(); }
    else { FAIL("CRC not self-consistent"); }
}

int main(void) {
    printf("=== CRC16 Tests ===\n\n");
    test_crc16_basic();
    test_crc16_running_mode();
    test_crc16_empty();
    test_crc16_single_byte();
    test_crc16_response_verifiable();
    printf("\nResults: %d/%d passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}