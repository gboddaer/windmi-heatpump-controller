#include "selftest.h"
#include "crc16.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

// Register test entries
typedef struct {
    const char *name;
    uint16_t address;
    bool read_only;
    bool critical;
} selftest_entry_t;

// Test table: 8 registers from design doc
static const selftest_entry_t test_table[] = {
    {"Outdoor Temp", 0x0001, true, true},
    {"Indoor Temp", 0x0002, true, false},
    {"Leaving Water Temp", 0x0004, true, true},
    {"Running Mode", 0x002D, false, true},
    {"DHW Target", 0x0194, false, true},
    {"Heating Target", 0x0191, false, true},
    {"DHW Priority", 0x028F, false, true},
    {"DHW Tank Temp", 0x1C5B, true, true}
};

#define NUM_TESTS (sizeof(test_table) / sizeof(test_table[0]))

// Internal: Read a single register using public API
static int read_register_internal(modbus_client_t *client, uint16_t address, int16_t *value) {
    return modbus_read_register(client, address, value);
}

// Internal: Write a single register using public API
static int write_register_internal(modbus_client_t *client, uint16_t address, uint16_t value) {
    return modbus_write_register(client, address, value);
}

selftest_report_t selftest_run(modbus_client_t *client) {
    selftest_report_t report;
    memset(&report, 0, sizeof(report));
    
    report.total = (int)NUM_TESTS;
    report.all_critical_passed = true;
    
    for (size_t i = 0; i < NUM_TESTS; i++) {
        const selftest_entry_t *entry = &test_table[i];
        selftest_result_t *result = &report.results[i];
        
        result->name = entry->name;
        result->address = entry->address;
        result->read_ok = false;
        result->write_ok = false;
        result->verify_ok = false;
        result->read_value = 0;
        
        // Step 1: Read current value
        int16_t current_value;
        if (read_register_internal(client, entry->address, &current_value) == 0) {
            result->read_ok = true;
            result->read_value = current_value;
            
            if (entry->read_only) {
                // Read-only register: pass if read succeeded
                result->write_ok = true;
                result->verify_ok = true;
            } else {
                // Read-write register: write back the same value (unintrusive)
                if (write_register_internal(client, entry->address, (uint16_t)current_value) == 0) {
                    result->write_ok = true;
                    
                    // Step 2: Verify by reading back
                    int16_t verify_value;
                    if (read_register_internal(client, entry->address, &verify_value) == 0) {
                        if (verify_value == current_value) {
                            result->verify_ok = true;
                        }
                    }
                }
            }
        }
        
        // Update report counters
        if (result->read_ok && result->write_ok && result->verify_ok) {
            report.passed++;
        } else {
            report.failed++;
            if (entry->critical) {
                report.all_critical_passed = false;
            }
        }
    }
    
    return report;
}

void selftest_print_report(const selftest_report_t *report) {
    if (!report) {
        return;
    }
    
    printf("\n========== Self-Test Report ==========\n");
    printf("\nRegister Results:\n");
    printf("%-20s  %8s  %6s  %6s  %6s  %8s\n", 
           "Name", "Address", "Read", "Write", "Verify", "Value");
    printf("----------------------------------------------------------------\n");
    
    for (int i = 0; i < report->total; i++) {
        const selftest_result_t *r = &report->results[i];
        printf("%-20s  0x%04X  %6s  %6s  %6s  %8d\n",
               r->name,
               r->address,
               r->read_ok ? "OK" : "FAIL",
               r->write_ok ? "OK" : "FAIL",
               r->verify_ok ? "OK" : "FAIL",
               r->read_value);
    }
    
    printf("----------------------------------------------------------------\n");
    printf("\nSummary:\n");
    printf("  Total registers tested: %d\n", report->total);
    printf("  Passed: %d\n", report->passed);
    printf("  Failed: %d\n", report->failed);
    printf("  All critical passed: %s\n", report->all_critical_passed ? "YES" : "NO");
    printf("\n========================================\n");
}
