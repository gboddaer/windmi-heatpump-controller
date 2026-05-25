#include "selftest.h"
#include "crc16.h"
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// Convert temperature to raw register value (multiply by 10)
static inline int16_t temp_to_raw(float temp) {
    return (int16_t)(temp * 10.0f);
}

// Convert raw register value to temperature (divide by 10)
static inline float raw_to_temp(int16_t raw) {
    return raw / 10.0f;
}

selftest_report_t selftest_run(modbus_client_t *client) {
    selftest_report_t report;
    memset(&report, 0, sizeof(report));
    
    report.total = 6;
    report.all_critical_passed = true;
    
    int test_idx = 0;
    
    // Test 1: Read device type (should be Rotenso Windmi 8kW = 8)
    {
        selftest_result_t *r = &report.results[test_idx++];
        r->name = "Device Type";
        r->address = REG_DEVICE_TYPE;
        r->read_ok = false;
        r->write_ok = true;
        r->verify_ok = true;
        
        int16_t device_type;
        if (modbus_read_register(client, REG_DEVICE_TYPE, &device_type) == 0) {
            r->read_ok = true;
            r->read_value = device_type;
            printf("Self-test: Device type = %d\n", device_type);
            if (device_type != 8) {
                fprintf(stderr, "Self-test: Unexpected device type: %d (expected 8 for Windmi 8kW)\n", device_type);
                report.all_critical_passed = false;
            }
        } else {
            fprintf(stderr, "Self-test: Failed to read device type\n");
            report.all_critical_passed = false;
        }
    }
    
    // Test 2: Read heating setpoint
    {
        selftest_result_t *r = &report.results[test_idx++];
        r->name = "Heating Setpoint";
        r->address = REG_HEATING_TARGET;
        r->read_ok = false;
        r->write_ok = true;
        r->verify_ok = true;
        
        int16_t heating_setpoint;
        if (modbus_read_register(client, REG_HEATING_TARGET, &heating_setpoint) == 0) {
            r->read_ok = true;
            r->read_value = heating_setpoint;
            printf("Self-test: Heating setpoint = %.1f C\n", raw_to_temp(heating_setpoint));
        } else {
            fprintf(stderr, "Self-test: Failed to read heating setpoint\n");
            report.all_critical_passed = false;
        }
    }
    
    // Test 3: Read DHW setpoint
    {
        selftest_result_t *r = &report.results[test_idx++];
        r->name = "DHW Setpoint";
        r->address = REG_DHW_TARGET;
        r->read_ok = false;
        r->write_ok = true;
        r->verify_ok = true;
        
        int16_t dhw_setpoint;
        if (modbus_read_register(client, REG_DHW_TARGET, &dhw_setpoint) == 0) {
            r->read_ok = true;
            r->read_value = dhw_setpoint;
            printf("Self-test: DHW setpoint = %.1f C\n", raw_to_temp(dhw_setpoint));
        } else {
            fprintf(stderr, "Self-test: Failed to read DHW setpoint\n");
            report.all_critical_passed = false;
        }
    }
    
    // Test 4: Read outdoor temperature
    {
        selftest_result_t *r = &report.results[test_idx++];
        r->name = "Outdoor Temp";
        r->address = REG_OUTDOOR_TEMP;
        r->read_ok = false;
        r->write_ok = true;
        r->verify_ok = true;
        
        int16_t outdoor_temp;
        if (modbus_read_register(client, REG_OUTDOOR_TEMP, &outdoor_temp) == 0) {
            r->read_ok = true;
            r->read_value = outdoor_temp;
            printf("Self-test: Outdoor temp = %.1f C\n", raw_to_temp(outdoor_temp));
        } else {
            fprintf(stderr, "Self-test: Failed to read outdoor temp\n");
            report.all_critical_passed = false;
        }
    }
    
    // Test 5: Read DHW temperature (tank temp)
    {
        selftest_result_t *r = &report.results[test_idx++];
        r->name = "DHW Tank Temp";
        r->address = REG_DHW_TANK_TEMP;
        r->read_ok = false;
        r->write_ok = true;
        r->verify_ok = true;
        
        int16_t dhw_temp;
        if (modbus_read_register(client, REG_DHW_TANK_TEMP, &dhw_temp) == 0) {
            r->read_ok = true;
            r->read_value = dhw_temp;
            printf("Self-test: DHW tank temp = %.1f C\n", raw_to_temp(dhw_temp));
        } else {
            fprintf(stderr, "Self-test: Failed to read DHW tank temp\n");
            report.all_critical_passed = false;
        }
    }
    
    // Test 6: Write-then-verify test for heating setpoint
    {
        selftest_result_t *r = &report.results[test_idx++];
        r->name = "Write Verify Test";
        r->address = REG_HEATING_TARGET;
        r->read_ok = true;
        r->write_ok = false;
        r->verify_ok = false;
        r->read_value = 0;
        
        // Save original heating setpoint
        int16_t original_heating;
        if (modbus_read_register(client, REG_HEATING_TARGET, &original_heating) != 0) {
            fprintf(stderr, "Self-test: Failed to read original heating setpoint\n");
            report.all_critical_passed = false;
            r->write_ok = false;
        } else {
            // Write test value (45 C)
            int16_t test_value = temp_to_raw(SELFTEST_DHW_TARGET_TEMP);
            if (modbus_write_register(client, REG_HEATING_TARGET, (uint16_t)test_value) == 0) {
                r->write_ok = true;
                
                // Verify by reading back
                int16_t verify_value;
                if (modbus_read_register(client, REG_HEATING_TARGET, &verify_value) == 0) {
                    if (verify_value == test_value) {
                        r->verify_ok = true;
                        printf("Self-test: Write verify passed (heating setpoint = %.1f C)\n", raw_to_temp(verify_value));
                    } else {
                        fprintf(stderr, "Self-test: Write verify failed (wrote %.1f, read %.1f)\n", 
                                raw_to_temp(test_value), raw_to_temp(verify_value));
                        report.all_critical_passed = false;
                    }
                } else {
                    fprintf(stderr, "Self-test: Failed to verify heating setpoint\n");
                    report.all_critical_passed = false;
                }
                
                // Restore original heating setpoint
                if (modbus_write_register(client, REG_HEATING_TARGET, (uint16_t)original_heating) != 0) {
                    fprintf(stderr, "Self-test: Failed to restore original heating setpoint\n");
                    report.all_critical_passed = false;
                } else {
                    printf("Self-test: Restored heating setpoint to %.1f C\n", raw_to_temp(original_heating));
                }
            } else {
                fprintf(stderr, "Self-test: Failed to write heating setpoint\n");
                report.all_critical_passed = false;
            }
        }
    }
    
    // Count passed tests
    for (int i = 0; i < report.total; i++) {
        selftest_result_t *r = &report.results[i];
        if (r->read_ok && r->write_ok && r->verify_ok) {
            report.passed++;
        } else {
            report.failed++;
        }
    }
    
    return report;
}

void selftest_print_report(const selftest_report_t *report) {
    if (!report) {
        return;
    }
    
    printf("\n========== Self-Test Report ==========\n");
    printf("\nTest Results:\n");
    printf("%-20s  %8s  %6s  %6s  %6s  %10s\n", 
           "Test", "Address", "Read", "Write", "Verify", "Value");
    printf("----------------------------------------------------------------\n");
    
    for (int i = 0; i < report->total; i++) {
        const selftest_result_t *r = &report->results[i];
        printf("%-20s  0x%04X  %6s  %6s  %6s  %10d\n",
               r->name,
               r->address,
               r->read_ok ? "OK" : "FAIL",
               r->write_ok ? "OK" : "FAIL",
               r->verify_ok ? "OK" : "FAIL",
               r->read_value);
    }
    
    printf("----------------------------------------------------------------\n");
    printf("\nSummary:\n");
    printf("  Total tests: %d\n", report->total);
    printf("  Passed: %d\n", report->passed);
    printf("  Failed: %d\n", report->failed);
    printf("  All critical passed: %s\n", report->all_critical_passed ? "YES" : "NO");
    printf("\n========================================\n");
}
