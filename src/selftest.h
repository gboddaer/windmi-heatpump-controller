#ifndef SELFTEST_H
#define SELFTEST_H

#include "modbus_client.h"
#include <stdbool.h>

typedef struct {
    const char *name;
    uint16_t address;
    bool read_ok;
    bool write_ok;
    bool verify_ok;
    int16_t read_value;
} selftest_result_t;

typedef struct {
    selftest_result_t results[8]; // 8 registers tested
    int total;
    int passed;
    int failed;
    bool all_critical_passed;
} selftest_report_t;

// Run self-test against the heat pump. Returns report with per-register results.
// This is unintrusive: write registers are written back with their current value.
selftest_report_t selftest_run(modbus_client_t *client);

// Print self-test report to stdout
void selftest_print_report(const selftest_report_t *report);

#endif // SELFTEST_H
