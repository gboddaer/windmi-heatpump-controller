/**
 * @file selftest.hpp
 * @brief Self-test for Windmi heat pump controller
 *
 * Runs a sequence of register read/write/verify tests against the
 * heat pump using any IModbusClient transport (TCP, serial, or demo).
 */

#ifndef WINDMI_SELFTEST_HPP
#define WINDMI_SELFTEST_HPP

#include <cstdint>
#include <string>

namespace windmi {

class IModbusClient;  // forward declaration

/// Result of a single self-test step
struct SelftestResult {
    std::string name;
    uint16_t address = 0;
    bool read_ok = false;
    bool write_ok = false;
    bool verify_ok = false;
    int16_t read_value = 0;
};

/// Aggregate report from a self-test run
struct SelftestReport {
    int total = 0;
    int passed = 0;
    int failed = 0;
    bool all_critical_passed = false;
    SelftestResult results[6];
};

/**
 * Run a self-test against the heat pump.
 *
 * @param client Connected IModbusClient (any transport)
 * @return Report with per-register results
 */
SelftestReport selftest_run(IModbusClient* client);

/**
 * Print self-test report to stdout.
 *
 * @param report Report to print
 */
void selftest_print_report(const SelftestReport& report);

} // namespace windmi

#endif // WINDMI_SELFTEST_HPP
