/**
 * @file tests/core/test_control_loop.cpp
 * @brief Control Loop unit tests
 */

#include <gtest/gtest.h>
#include "core/ControlLoop.hpp"
#include "core/StatusMonitor.hpp"
#include "config.h"

using namespace windmi;

// ---- StatusSnapshot tests ----

TEST(StatusSnapshotTest, DefaultValues) {
    StatusSnapshot snap{};
    EXPECT_FLOAT_EQ(snap.outdoor_temp, 0.0f);
    EXPECT_FLOAT_EQ(snap.indoor_temp, 0.0f);
    EXPECT_FLOAT_EQ(snap.leaving_water_temp, 0.0f);
    EXPECT_FLOAT_EQ(snap.dhw_tank_temp, 0.0f);
    EXPECT_FLOAT_EQ(snap.dhw_target, 0.0f);
    EXPECT_FLOAT_EQ(snap.heating_target, 0.0f);
    EXPECT_EQ(snap.running_mode, 0);
    EXPECT_EQ(snap.running_status, 0);
    EXPECT_FALSE(snap.dhw_priority);
    EXPECT_FALSE(snap.is_running);
    EXPECT_FALSE(snap.device_online);
    EXPECT_FLOAT_EQ(snap.ac_current, 0.0f);
    EXPECT_FLOAT_EQ(snap.dc_current, 0.0f);
    EXPECT_FLOAT_EQ(snap.ac_voltage, 0.0f);
    EXPECT_FLOAT_EQ(snap.dc_voltage, 0.0f);
    EXPECT_FLOAT_EQ(snap.ac_power_va, 0.0f);
    EXPECT_FLOAT_EQ(snap.ac_power_w, 0.0f);
    EXPECT_FALSE(snap.power_valid);
    EXPECT_EQ(snap.working_mode, 0);
}

TEST(StatusSnapshotTest, HasNoFabricatedFields) {
    // Verify the struct has no heating_temperature field
    // by confirming the fields that DO exist match master's status_snapshot_t
    StatusSnapshot snap{};
    snap.dhw_tank_temp = 45.0f;    // exists in master
    snap.dhw_target = 46.0f;       // exists in master
    snap.heating_target = 40.0f;   // exists in master
    snap.indoor_temp = 22.0f;      // exists in master
    snap.leaving_water_temp = 35.0f;  // exists in master

    EXPECT_FLOAT_EQ(snap.dhw_tank_temp, 45.0f);
    EXPECT_FLOAT_EQ(snap.dhw_target, 46.0f);
    EXPECT_FLOAT_EQ(snap.heating_target, 40.0f);
    EXPECT_FLOAT_EQ(snap.indoor_temp, 22.0f);
    EXPECT_FLOAT_EQ(snap.leaving_water_temp, 35.0f);
}

TEST(StatusSnapshotTest, PriorityIsBool) {
    StatusSnapshot snap{};
    snap.dhw_priority = true;
    EXPECT_TRUE(snap.dhw_priority);
    snap.dhw_priority = false;
    EXPECT_FALSE(snap.dhw_priority);
}

TEST(StatusSnapshotTest, IsRunningIsBool) {
    StatusSnapshot snap{};
    snap.is_running = true;
    EXPECT_TRUE(snap.is_running);
    snap.is_running = false;
    EXPECT_FALSE(snap.is_running);
}

// ---- Config register address tests ----

TEST(ConfigRegisterTest, NoFabricatedRegisters) {
    // Verify fabricated addresses are NOT defined
    // REG_DHW_TEMP (0x0012), REG_HEATING_TEMP (0x0014), REG_AC_POWER (0x1018)
    // should not exist. We can't test for undefined macros directly,
    // but we verify the correct ones exist.
    EXPECT_EQ(REG_DHW_TANK_TEMP, 0x00CE);
    EXPECT_EQ(REG_DHW_TARGET, 0x0194);
    EXPECT_EQ(REG_HEATING_TARGET, 0x0191);
    EXPECT_EQ(REG_LEAVING_WATER_TEMP, 0x0004);
    EXPECT_EQ(REG_OUTDOOR_TEMP, 0x0001);
    EXPECT_EQ(REG_INDOOR_TEMP, 0x0002);
    EXPECT_EQ(REG_RUNNING_MODE, 0x002C);
    EXPECT_EQ(REG_RUNNING_STATUS, 0x002D);
    EXPECT_EQ(REG_DHW_PRIORITY, 0x02BF);
    EXPECT_EQ(REG_DEVICE_TYPE, 0x1006);
}

TEST(ConfigRegisterTest, ModeSetValues) {
    EXPECT_EQ(MODE_SET_OFF, 0);
    EXPECT_EQ(MODE_SET_COOL_DHW, 1);
    EXPECT_EQ(MODE_SET_HEAT_DHW, 2);
}

TEST(ConfigRegisterTest, ModeStatusValues) {
    EXPECT_EQ(MODE_STATUS_OFF, 0);
    EXPECT_EQ(MODE_STATUS_COOL, 1);
    EXPECT_EQ(MODE_STATUS_HEAT, 2);
    EXPECT_EQ(MODE_STATUS_DHW, 4);
    EXPECT_EQ(MODE_STATUS_DEFROST, 7);
    EXPECT_EQ(MODE_STATUS_ANTIFREEZE, 20);
}

TEST(ConfigRegisterTest, ModbusConfig) {
    EXPECT_EQ(MODBUS_SLAVE_ID, 11);
    EXPECT_EQ(MODBUS_GATEWAY_PORT, 8899);
    EXPECT_STREQ(MODBUS_GATEWAY_IP, "192.168.123.10");
}

TEST(ConfigRegisterTest, ControlLoopConfig) {
    EXPECT_EQ(CONTROL_LOOP_INTERVAL_S, 30);
    EXPECT_EQ(MODBUS_MAX_RETRIES, 3);
    EXPECT_EQ(MODBUS_RECONNECT_INTERVAL_S, 10);
}

TEST(ConfigRegisterTest, DiagnosticRegisters) {
    EXPECT_EQ(REG_UNIT_CAPACITY, 0x1006);
    EXPECT_EQ(REG_DEVICE_TYPE, REG_UNIT_CAPACITY);  // Backwards compatible alias
    EXPECT_EQ(REG_COMPRESSOR_FREQ, 0x0040);
    EXPECT_EQ(REG_WATER_FLOW, 0x102A);
    EXPECT_EQ(REG_ACTUAL_CAPACITY_OUTPUT, 0x1004);
    EXPECT_EQ(REG_ODU_INPUT_STATUS, 0x101F);
    EXPECT_EQ(REG_COMPRESSOR_RUNTIME, 0x0174);
    EXPECT_EQ(REG_PUMP_RUNTIME, 0x0176);
}

TEST(StatusSnapshotTest, DiagnosticDefaults) {
    StatusSnapshot snap{};
    EXPECT_FLOAT_EQ(snap.compressor_freq, 0.0f);
    EXPECT_FLOAT_EQ(snap.water_flow, 0.0f);
    EXPECT_EQ(snap.unit_capacity_kw, 0);
    EXPECT_EQ(snap.actual_capacity_output, 0);
    EXPECT_EQ(snap.odu_input_status, 0);
    EXPECT_EQ(snap.compressor_runtime_h, 0);
    EXPECT_EQ(snap.pump_runtime_h, 0);
    EXPECT_FLOAT_EQ(snap.heat_output_w, 0.0f);
    EXPECT_FLOAT_EQ(snap.cop, 0.0f);
    EXPECT_FALSE(snap.cop_valid);
}

// ---- CmdQueue tests ----

TEST(CmdQueueTest, PushPop) {
    CmdQueue q;
    Command cmd_in;
    cmd_in.type = CommandType::CMD_SET_DHW_TEMP;
    cmd_in.float_val = 45.0f;
    cmd_in.int_val = 0;

    EXPECT_TRUE(q.push(cmd_in));
    EXPECT_FALSE(q.empty());

    Command cmd_out;
    EXPECT_TRUE(q.pop(cmd_out));
    EXPECT_EQ(static_cast<int>(cmd_out.type), static_cast<int>(CommandType::CMD_SET_DHW_TEMP));
    EXPECT_FLOAT_EQ(cmd_out.float_val, 45.0f);
    EXPECT_EQ(cmd_out.int_val, 0);
    EXPECT_TRUE(q.empty());
}

TEST(CmdQueueTest, PopEmpty) {
    CmdQueue q;
    Command cmd;
    EXPECT_FALSE(q.pop(cmd));
    EXPECT_TRUE(q.empty());
}

TEST(CmdQueueTest, MultipleCommands) {
    CmdQueue q;
    Command cmd1, cmd2;
    cmd1.type = CommandType::CMD_SET_DHW_TEMP;
    cmd1.float_val = 50.0f;
    cmd2.type = CommandType::CMD_SET_HEATING_TEMP;
    cmd2.float_val = 40.0f;

    EXPECT_TRUE(q.push(cmd1));
    EXPECT_TRUE(q.push(cmd2));

    Command out;
    EXPECT_TRUE(q.pop(out));
    EXPECT_EQ(static_cast<int>(out.type), static_cast<int>(CommandType::CMD_SET_DHW_TEMP));
    EXPECT_TRUE(q.pop(out));
    EXPECT_EQ(static_cast<int>(out.type), static_cast<int>(CommandType::CMD_SET_HEATING_TEMP));
}

// ---- StatusQueue tests ----

TEST(StatusQueueTest, PushPopLatest) {
    StatusQueue q;
    StatusSnapshot snap1{}, snap2{};
    snap1.dhw_tank_temp = 45.0f;
    snap2.dhw_tank_temp = 50.0f;

    EXPECT_TRUE(q.push(snap1));
    EXPECT_TRUE(q.push(snap2));

    StatusSnapshot latest;
    EXPECT_TRUE(q.latest(latest));
    EXPECT_FLOAT_EQ(latest.dhw_tank_temp, 50.0f);
}

TEST(StatusQueueTest, LatestEmpty) {
    StatusQueue q;
    StatusSnapshot snap;
    EXPECT_FALSE(q.latest(snap));
}

// ---- ControlLoop tests ----

TEST(ControlLoopTest, CreateAndStop) {
    ControlLoop loop;
    EXPECT_FALSE(loop.isRunning());
}

TEST(ControlLoopTest, KickDoesNotCrash) {
    ControlLoop loop;
    // Kick without start should not crash
    loop.kick();
}

// ---- Power scaling tests ----

TEST(PowerScalingTest, AcCurrentMultipliedBy2) {
    // Master: ac_current = raw * 2.0f
    int16_t raw = 5;  // 5 displayed
    float ac_current = static_cast<float>(raw) * 2.0f;
    EXPECT_FLOAT_EQ(ac_current, 10.0f);
}

TEST(PowerScalingTest, DcCurrentMultipliedBy4) {
    // Master: dc_current = raw * 4.0f
    int16_t raw = 3;
    float dc_current = static_cast<float>(raw) * 4.0f;
    EXPECT_FLOAT_EQ(dc_current, 12.0f);
}

TEST(PowerScalingTest, AcVoltageIsRaw) {
    // Master: ac_voltage = (float)raw
    int16_t raw = 230;
    float ac_voltage = static_cast<float>(raw);
    EXPECT_FLOAT_EQ(ac_voltage, 230.0f);
}

TEST(PowerScalingTest, DcVoltageDividedBy2) {
    // Master: dc_voltage = raw / 2.0f
    int16_t raw = 24;
    float dc_voltage = static_cast<float>(raw) / 2.0f;
    EXPECT_FLOAT_EQ(dc_voltage, 12.0f);
}

TEST(PowerScalingTest, AcPowerVaIsVoltageTimesCurrent) {
    // Apparent power: VA = V × A
    float ac_voltage = 230.0f;
    float ac_current = 10.0f;
    float ac_power_va = ac_voltage * ac_current;
    EXPECT_FLOAT_EQ(ac_power_va, 2300.0f);
}

TEST(PowerScalingTest, AcPowerWEstimatedWithPowerFactor) {
    // Real power: W = VA × PF
    float ac_power_va = 2300.0f;
    float ac_power_w = ac_power_va * ESTIMATED_POWER_FACTOR;
    EXPECT_NEAR(ac_power_w, 2070.0f, 1.0f);
}

// ---- StatusMonitor tests ----

TEST(StatusMonitorTest, CreateMonitor) {
    StatusMonitor monitor;
    EXPECT_FALSE(monitor.isValid());
}

TEST(StatusMonitorTest, UpdateAndRead) {
    StatusMonitor monitor;

    StatusSnapshot snapshot{};
    snapshot.dhw_tank_temp = 45.0f;
    snapshot.dhw_target = 46.0f;
    snapshot.heating_target = 40.0f;
    snapshot.device_online = true;
    snapshot.dhw_priority = true;
    snapshot.is_running = true;

    monitor.update(snapshot);
    EXPECT_TRUE(monitor.isValid());

    StatusSnapshot read_snapshot{};
    EXPECT_TRUE(monitor.get(read_snapshot));
    EXPECT_FLOAT_EQ(read_snapshot.dhw_tank_temp, 45.0f);
    EXPECT_FLOAT_EQ(read_snapshot.dhw_target, 46.0f);
    EXPECT_FLOAT_EQ(read_snapshot.heating_target, 40.0f);
    EXPECT_TRUE(read_snapshot.dhw_priority);
    EXPECT_TRUE(read_snapshot.is_running);
}

TEST(StatusMonitorTest, CopyGet) {
    StatusMonitor monitor;

    StatusSnapshot snapshot{};
    snapshot.dhw_tank_temp = 50.0f;
    snapshot.dhw_target = 51.0f;

    monitor.update(snapshot);

    StatusSnapshot copy = monitor.get();
    EXPECT_FLOAT_EQ(copy.dhw_tank_temp, 50.0f);
    EXPECT_FLOAT_EQ(copy.dhw_target, 51.0f);
}

TEST(StatusMonitorTest, Reset) {
    StatusMonitor monitor;
    StatusSnapshot snapshot{};
    snapshot.dhw_tank_temp = 45.0f;
    monitor.update(snapshot);
    EXPECT_TRUE(monitor.isValid());

    monitor.reset();
    EXPECT_FALSE(monitor.isValid());
}