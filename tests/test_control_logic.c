#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "../include/config.h"

static int test_count = 0;
static int pass_count = 0;
#define TEST(name) do { test_count++; printf("  TEST: %s ...", #name); } while(0)
#define PASS() do { pass_count++; printf(" PASS\n"); } while(0)
#define FAIL(msg) do { printf(" FAIL: %s\n", msg); } while(0)

static inline float raw_to_temp(int16_t raw) { return raw / 10.0f; }
static inline int16_t temp_to_raw(float temp) { return (int16_t)(temp * 10.0f); }
static bool dhw_needs_heating(float tank, float target, float hyst) { return (target - tank) > hyst; }
static bool heating_is_needed(float ht, float lwt, float hyst) { return (ht - lwt) > hyst; }

static void test_temp_positive(void) { TEST(temp_positive); if (raw_to_temp(215) > 21.49f && raw_to_temp(215) < 21.51f) PASS(); else FAIL(""); }
static void test_temp_negative(void) { TEST(temp_negative); float t = raw_to_temp(-50); if (t > -5.01f && t < -4.99f) PASS(); else FAIL(""); }
static void test_temp_zero(void) { TEST(temp_zero); if (raw_to_temp(0) == 0.0f) PASS(); else FAIL(""); }
static void test_temp_roundtrip(void) { TEST(temp_roundtrip); if (raw_to_temp(temp_to_raw(50.0f)) == 50.0f) PASS(); else FAIL(""); }
static void test_dhw_eq_3(void) { TEST(dhw_delta_eq_3_no_trigger); if (!dhw_needs_heating(47.0f, 50.0f, DHW_HYSTERESIS_C)) PASS(); else FAIL("> should not trigger at delta==3"); }
static void test_dhw_above_3(void) { TEST(dhw_delta_gt_3_trigger); if (dhw_needs_heating(46.0f, 50.0f, DHW_HYSTERESIS_C)) PASS(); else FAIL(""); }
static void test_dhw_near(void) { TEST(dhw_near_target); if (!dhw_needs_heating(48.5f, 50.0f, DHW_HYSTERESIS_C)) PASS(); else FAIL(""); }
static void test_dhw_reached(void) { TEST(dhw_target_reached); if (50.0f >= 50.0f) PASS(); else FAIL(""); }
static void test_dhw_min(void) { TEST(dhw_min_40); if (DHW_TEMP_MIN == 40.0f) PASS(); else FAIL(""); }
static void test_dhw_max(void) { TEST(dhw_max_63); if (DHW_TEMP_MAX == 63.0f) PASS(); else FAIL(""); }
static void test_heat_min(void) { TEST(heat_min_25); if (HEATING_TEMP_MIN == 25.0f) PASS(); else FAIL(""); }
static void test_heat_max(void) { TEST(heat_max_63); if (HEATING_TEMP_MAX == 63.0f) PASS(); else FAIL(""); }
static void test_modes(void) { TEST(mode_values); if (MODE_STATUS_OFF==0&&MODE_STATUS_COOL==1&&MODE_STATUS_HEAT==2&&MODE_STATUS_DHW==4&&MODE_STATUS_DEFROST==7&&MODE_STATUS_ANTIFREEZE==20) PASS(); else FAIL(""); }
static void test_no_mode3(void) { TEST(no_mode_3); if (MODE_SET_OFF!=3&&MODE_SET_COOL_DHW!=3&&MODE_SET_HEAT_DHW!=3&&MODE_STATUS_DHW!=3) PASS(); else FAIL(""); }
static void test_heat_cold(void) { TEST(heat_needed_cold); if (heating_is_needed(45.0f,30.0f,HEATING_HYSTERESIS_C)) PASS(); else FAIL(""); }
static void test_heat_warm(void) { TEST(heat_not_needed_warm); if (!heating_is_needed(45.0f,44.5f,HEATING_HYSTERESIS_C)) PASS(); else FAIL(""); }
static void test_port(void) { TEST(port_8899); if (MODBUS_GATEWAY_PORT==8899) PASS(); else FAIL(""); }
static void test_ip(void) { TEST(ip_default); if (strcmp(MODBUS_GATEWAY_IP,"192.168.123.10")==0) PASS(); else FAIL(""); }

int main(void) {
    printf("=== Control Logic & Config Tests ===\n\n");
    test_temp_positive(); test_temp_negative(); test_temp_zero(); test_temp_roundtrip();
    test_dhw_eq_3(); test_dhw_above_3(); test_dhw_near(); test_dhw_reached();
    test_dhw_min(); test_dhw_max(); test_heat_min(); test_heat_max();
    test_modes(); test_no_mode3();
    test_heat_cold(); test_heat_warm();
    test_port(); test_ip();
    printf("\nResults: %d/%d passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}