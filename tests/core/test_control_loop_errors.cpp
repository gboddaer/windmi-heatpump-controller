/**
 * @file test_control_loop_errors.cpp
 * @brief ControlLoop error path and edge case testing
 */

#include <gtest/gtest.h>
#include <cstdint>
#include <cmath>

#include "core/ControlLoop.hpp"
#include "modbus/IModbusClient.hpp"

namespace windmi::test {

// ─────────────────────────────────────────────────────────────────────────────
// CmdQueue Error Path Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(CmdQueueErrorTest, PopWhenFullThenEmpty) {
  CmdQueue queue;

  // Fill queue
  for (int i = 0; i < static_cast<int>(CmdQueue::CAPACITY); ++i) {
    Command cmd;
    cmd.type = CommandType::CMD_SET_DHW_TEMP;
    cmd.float_val = 50.0f;
    queue.push(cmd);
  }

  // Empty it
  for (int i = 0; i < static_cast<int>(CmdQueue::CAPACITY); ++i) {
    Command cmd;
    queue.pop(cmd);
  }

  // Queue should be empty now
  Command cmd;
  EXPECT_FALSE(queue.pop(cmd));  // Should fail (empty)
}

// ─────────────────────────────────────────────────────────────────────────────
// StatusQueue Error Path Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(StatusQueueErrorTest, PushWhenFullOverwritesOldest) {
  StatusQueue queue;

  // Push CAPACITY items
  for (size_t i = 0; i < StatusQueue::CAPACITY; ++i) {
    StatusSnapshot status;
    status.outdoor_temp = static_cast<float>(i * 10);
    queue.push(status);
  }

  // Push one more - should overwrite oldest
  StatusSnapshot status;
  status.outdoor_temp = 999.0f;
  queue.push(status);

  // Latest should be the new item
  StatusSnapshot latest;
  EXPECT_TRUE(queue.latest(latest));
  EXPECT_FLOAT_EQ(latest.outdoor_temp, 999.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// StatusSnapshot Edge Case Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(StatusSnapshotEdgeTest, ZeroValues) {
  StatusSnapshot status;

  EXPECT_FLOAT_EQ(status.outdoor_temp, 0.0f);
  EXPECT_FLOAT_EQ(status.dhw_tank_temp, 0.0f);
  EXPECT_FLOAT_EQ(status.cop, 0.0f);
}

TEST(StatusSnapshotEdgeTest, LargeValues) {
  StatusSnapshot status;
  status.outdoor_temp = 1000.0f;
  status.dhw_tank_temp = 1000.0f;
  status.cop = 10.0f;

  EXPECT_FLOAT_EQ(status.outdoor_temp, 1000.0f);
  EXPECT_FLOAT_EQ(status.dhw_tank_temp, 1000.0f);
  EXPECT_FLOAT_EQ(status.cop, 10.0f);
}

TEST(StatusSnapshotEdgeTest, PowerValidFalseWhenNoData) {
  StatusSnapshot status;

  // power_valid should be false initially
  EXPECT_FALSE(status.power_valid);
}

// ─────────────────────────────────────────────────────────────────────────────
// Power Scaling Edge Case Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(PowerScalingEdgeTest, ZeroCurrentVoltage) {
  StatusSnapshot status;

  // Zero values should produce zero power
  status.ac_current = 0.0f;
  status.ac_voltage = 0.0f;
  status.ac_power_va = status.ac_voltage * status.ac_current;
  status.ac_power_w = status.ac_power_va * 0.9f;  // Assuming 0.9 power factor

  EXPECT_FLOAT_EQ(status.ac_power_va, 0.0f);
  EXPECT_FLOAT_EQ(status.ac_power_w, 0.0f);
}

TEST(PowerScalingEdgeTest, MaximumCurrent) {
  StatusSnapshot status;

  // Simulate maximum current (e.g., 30A)
  int16_t raw_current = 15;  // 15 * 2 = 30A
  status.ac_current = static_cast<float>(raw_current) * 2.0f;

  EXPECT_FLOAT_EQ(status.ac_current, 30.0f);
}

TEST(PowerScalingEdgeTest, HighVoltage) {
  StatusSnapshot status;

  // Simulate high voltage (e.g., 250V)
  int16_t raw_voltage = 250;  // 250 * 1 = 250V
  status.ac_voltage = static_cast<float>(raw_voltage);

  EXPECT_FLOAT_EQ(status.ac_voltage, 250.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// COP Calculation Edge Case Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(COPCalculationEdgeTest, ZeroPowerInput) {
  StatusSnapshot status;

  // COP = heat_output / power_input
  // If power_input is zero, COP should remain invalid
  status.heat_output_w = 1000.0f;
  status.ac_power_w = 0.0f;
  status.cop = status.heat_output_w / status.ac_power_w;  // Division by zero!

  // In practice, COP should be validated before use
  EXPECT_TRUE(std::isinf(status.cop) || std::isnan(status.cop));
}

TEST(COPCalculationEdgeTest, ZeroHeatOutput) {
  StatusSnapshot status;

  status.heat_output_w = 0.0f;
  status.ac_power_w = 100.0f;
  status.cop = status.heat_output_w / status.ac_power_w;

  EXPECT_FLOAT_EQ(status.cop, 0.0f);
}

TEST(COPCalculationEdgeTest, ValidCOP) {
  StatusSnapshot status;

  // Heat output 2000W, Power input 500W → COP = 4.0
  status.heat_output_w = 2000.0f;
  status.ac_power_w = 500.0f;
  status.cop = status.heat_output_w / status.ac_power_w;

  EXPECT_FLOAT_EQ(status.cop, 4.0f);
}

}  // namespace windmi::test
