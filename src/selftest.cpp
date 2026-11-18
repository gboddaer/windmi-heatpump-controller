/**
 * @file src/selftest.cpp
 * @brief Self-test implementation using IModbusClient interface
 */

#include "selftest.hpp"
#include "modbus/IModbusClient.hpp"
#include "config.h"
#include "utils/Logger.hpp"
#include "utils/LogTags.hpp"

#include <cstdio>

namespace windmi {

static inline int16_t temp_to_raw(float temp)
{
  return static_cast<int16_t>(temp * 10.0f);
}

static inline float raw_to_temp(int16_t raw)
{
  return raw / 10.0f;
}

SelftestReport selftest_run(IModbusClient* client)
{
  SelftestReport report;

  // Guard against null client
  if (!client)
  {
    report.total = 6;
    report.failed = 6;
    report.all_critical_passed = false;
    return report;
  }

  // Zero-initialize the results array
  for (int i = 0; i < 6; i++)
  {
    report.results[i] = SelftestResult{};
  }

  report.total = 6;
  report.all_critical_passed = true;

  int test_idx = 0;

  // Test 1: Read device type (should be Rotenso Windmi 8kW = 8)
  {
    SelftestResult& r = report.results[test_idx++];
    r.name = "Device Type";
    r.address = REG_DEVICE_TYPE;
    r.read_ok = false;
    r.write_ok = true;
    r.verify_ok = true;

    try
    {
      int16_t device_type = client->readRegister(REG_DEVICE_TYPE);
      r.read_ok = true;
      r.read_value = device_type;
      WINDMI_LOG_DEBUG(LOG_TAG_SELFTEST, "Device type = %d", device_type);
      if (device_type != 8)
      {
        WINDMI_LOG_ERROR(LOG_TAG_SELFTEST, "Unexpected device type: %d (expected 8 for Windmi 8kW)",
                         device_type);
        report.all_critical_passed = false;
      }
    } catch (const ModbusException&)
    {
      WINDMI_LOG_ERROR(LOG_TAG_SELFTEST, "Failed to read device type");
      report.all_critical_passed = false;
    }
  }

  // Test 2: Read heating setpoint
  {
    SelftestResult& r = report.results[test_idx++];
    r.name = "Heating Setpoint";
    r.address = REG_HEATING_TARGET;
    r.read_ok = false;
    r.write_ok = true;
    r.verify_ok = true;

    try
    {
      int16_t heating_setpoint = client->readRegister(REG_HEATING_TARGET);
      r.read_ok = true;
      r.read_value = heating_setpoint;
      WINDMI_LOG_DEBUG(LOG_TAG_SELFTEST, "Heating setpoint = %.1f C",
                       raw_to_temp(heating_setpoint));
    } catch (const ModbusException&)
    {
      WINDMI_LOG_ERROR(LOG_TAG_SELFTEST, "Failed to read heating setpoint");
      report.all_critical_passed = false;
    }
  }

  // Test 3: Read DHW setpoint
  {
    SelftestResult& r = report.results[test_idx++];
    r.name = "DHW Setpoint";
    r.address = REG_DHW_TARGET;
    r.read_ok = false;
    r.write_ok = true;
    r.verify_ok = true;

    try
    {
      int16_t dhw_setpoint = client->readRegister(REG_DHW_TARGET);
      r.read_ok = true;
      r.read_value = dhw_setpoint;
      WINDMI_LOG_DEBUG(LOG_TAG_SELFTEST, "DHW setpoint = %.1f C", raw_to_temp(dhw_setpoint));
    } catch (const ModbusException&)
    {
      WINDMI_LOG_ERROR(LOG_TAG_SELFTEST, "Failed to read DHW setpoint");
      report.all_critical_passed = false;
    }
  }

  // Test 4: Read outdoor temperature
  {
    SelftestResult& r = report.results[test_idx++];
    r.name = "Outdoor Temp";
    r.address = REG_OUTDOOR_TEMP;
    r.read_ok = false;
    r.write_ok = true;
    r.verify_ok = true;

    try
    {
      int16_t outdoor_temp = client->readRegister(REG_OUTDOOR_TEMP);
      r.read_ok = true;
      r.read_value = outdoor_temp;
      WINDMI_LOG_DEBUG(LOG_TAG_SELFTEST, "Outdoor temp = %.1f C", raw_to_temp(outdoor_temp));
    } catch (const ModbusException&)
    {
      WINDMI_LOG_ERROR(LOG_TAG_SELFTEST, "Failed to read outdoor temp");
      report.all_critical_passed = false;
    }
  }

  // Test 5: Read DHW temperature (tank temp)
  {
    SelftestResult& r = report.results[test_idx++];
    r.name = "DHW Tank Temp";
    r.address = REG_DHW_TANK_TEMP;
    r.read_ok = false;
    r.write_ok = true;
    r.verify_ok = true;

    try
    {
      int16_t dhw_temp = client->readRegister(REG_DHW_TANK_TEMP);
      r.read_ok = true;
      r.read_value = dhw_temp;
      WINDMI_LOG_DEBUG(LOG_TAG_SELFTEST, "DHW tank temp = %.1f C", raw_to_temp(dhw_temp));
    } catch (const ModbusException&)
    {
      WINDMI_LOG_ERROR(LOG_TAG_SELFTEST, "Failed to read DHW tank temp");
      report.all_critical_passed = false;
    }
  }

  // Test 6: Write-then-verify test for heating setpoint
  {
    SelftestResult& r = report.results[test_idx++];
    r.name = "Write Verify Test";
    r.address = REG_HEATING_TARGET;
    r.read_ok = true;
    r.write_ok = false;
    r.verify_ok = false;
    r.read_value = -1; // Will be set after successful read

    // Save original heating setpoint
    int16_t original_heating = 0;
    bool read_original_ok = false;
    try
    {
      original_heating = client->readRegister(REG_HEATING_TARGET);
      read_original_ok = true;
    } catch (const ModbusException&)
    {
      WINDMI_LOG_ERROR(LOG_TAG_SELFTEST, "Failed to read original heating setpoint");
      report.all_critical_passed = false;
    }

    if (read_original_ok)
    {
      r.read_value = original_heating; // Store original value for report

      // Write test value (45 C)
      int16_t test_value = temp_to_raw(SELFTEST_DHW_TARGET_TEMP);
      try
      {
        client->writeRegister(REG_HEATING_TARGET, static_cast<uint16_t>(test_value));
        r.write_ok = true;

        // Verify by reading back
        try
        {
          int16_t verify_value = client->readRegister(REG_HEATING_TARGET);
          if (verify_value == test_value)
          {
            r.verify_ok = true;
            WINDMI_LOG_DEBUG(LOG_TAG_SELFTEST, "Write verify passed (heating setpoint = %.1f C)",
                             raw_to_temp(verify_value));
          } else
          {
            WINDMI_LOG_ERROR(LOG_TAG_SELFTEST, "Write verify failed (wrote %.1f, read %.1f)",
                             raw_to_temp(test_value), raw_to_temp(verify_value));
            report.all_critical_passed = false;
          }
        } catch (const ModbusException&)
        {
          WINDMI_LOG_ERROR(LOG_TAG_SELFTEST, "Failed to verify heating setpoint");
          report.all_critical_passed = false;
        }

        // Restore original heating setpoint
        try
        {
          client->writeRegister(REG_HEATING_TARGET, static_cast<uint16_t>(original_heating));
          WINDMI_LOG_DEBUG(LOG_TAG_SELFTEST, "Restored heating setpoint to %.1f C",
                           raw_to_temp(original_heating));
        } catch (const ModbusException&)
        {
          WINDMI_LOG_ERROR(LOG_TAG_SELFTEST, "Failed to restore original heating setpoint");
          report.all_critical_passed = false;
        }
      } catch (const ModbusException&)
      {
        WINDMI_LOG_ERROR(LOG_TAG_SELFTEST, "Failed to write heating setpoint");
        report.all_critical_passed = false;
      }
    }
  }

  // Count passed tests
  for (int i = 0; i < report.total; i++)
  {
    SelftestResult& r = report.results[i];
    if (r.read_ok && r.write_ok && r.verify_ok)
    {
      report.passed++;
    } else
    {
      report.failed++;
    }
  }

  return report;
}

void selftest_print_report(const SelftestReport& report)
{
  printf("\n========== Self-Test Report ==========\n");
  printf("\nTest Results:\n");
  printf("%-20s  %8s  %6s  %6s  %6s  %10s\n", "Test", "Address", "Read", "Write", "Verify",
         "Value");
  printf("----------------------------------------------------------------\n");

  for (int i = 0; i < report.total; i++)
  {
    const SelftestResult& r = report.results[i];
    printf("%-20s  0x%04X  %6s  %6s  %6s  %10d\n", r.name.c_str(), r.address,
           r.read_ok ? "OK" : "FAIL", r.write_ok ? "OK" : "FAIL", r.verify_ok ? "OK" : "FAIL",
           r.read_value);
  }

  printf("----------------------------------------------------------------\n");
  printf("\nSummary:\n");
  printf("  Total tests: %d\n", report.total);
  printf("  Passed: %d\n", report.passed);
  printf("  Failed: %d\n", report.failed);
  printf("  All critical passed: %s\n", report.all_critical_passed ? "YES" : "NO");
  printf("\n========================================\n");
}

} // namespace windmi
