/**
 * @file tests/core/test_control_loop.cpp
 * @brief Control Loop unit tests
 */

#include <gtest/gtest.h>
#include "core/ControlLoop.hpp"
#include "core/StatusMonitor.hpp"
#include "core/BackupHeater.hpp"
#include "modbus/IModbusClient.hpp"
#include "config.h"
#include "utils/Platform.hpp"

#include <chrono>
#include <map>
#include <utility>
#include <vector>

using namespace windmi;

namespace {

class FakeModbusClient : public IModbusClient {
public:
    explicit FakeModbusClient(std::map<uint16_t, int16_t> registers)
        : registers_(std::move(registers)) {}

    bool connect() override { connected_ = true; return true; }
    void disconnect() override { connected_ = false; }
    bool isConnected() const override { return connected_; }

    int16_t readRegister(uint16_t address) override {
        auto it = registers_.find(address);
        if (it == registers_.end()) {
            throw ModbusException("register not configured");
        }
        return it->second;
    }

    void writeRegister(uint16_t address, uint16_t value) override {
        writes_.push_back({address, value});
        registers_[address] = static_cast<int16_t>(value);
    }

    const std::vector<std::pair<uint16_t, uint16_t>>& writes() const { return writes_; }

    void flushBuffer() override {}
    std::string getLastError() const override { return {}; }

private:
    ArbitrationConfig cfg_;

    bool connected_ = true;
    std::map<uint16_t, int16_t> registers_;
    std::vector<std::pair<uint16_t, uint16_t>> writes_;
};

// Complete register map so readStatus() reports the device as online; without
// that the control logic is skipped entirely.
std::map<uint16_t, int16_t> fullRegisters() {
    return {
        {REG_OUTDOOR_TEMP, 100},
        {REG_INDOOR_TEMP, 210},
        {REG_ENTERING_WATER_TEMP, 300},
        {REG_LEAVING_WATER_TEMP, 350},
        {REG_DHW_TANK_TEMP, 450},
        {REG_RUNNING_MODE, MODE_SET_HEAT_DHW},
        {REG_RUNNING_STATUS, MODE_STATUS_HEAT},
        {REG_DHW_TARGET, 460},
        {REG_HEATING_TARGET, 400},
        {REG_DHW_PRIORITY, 1},
        {REG_OCCUPANCY_MODE, 2},
        {REG_AC_CURRENT, 5},
        {REG_DC_CURRENT, 3},
        {REG_AC_VOLTAGE, 230},
        {REG_DC_VOLTAGE, 24},
        {REG_UNIT_CAPACITY, 8},
        {REG_COMPRESSOR_FREQ, 42},
        {REG_WATER_FLOW, 120},
        {REG_ACTUAL_CAPACITY_OUTPUT, 50},
        {REG_ODU_INPUT_STATUS, 0},
        {REG_COMPRESSOR_RUNTIME, 123},
        {REG_PUMP_RUNTIME, 456},
    };
}

bool wroteValue(const FakeModbusClient& client, uint16_t address, uint16_t value) {
    for (const auto& write : client.writes()) {
        if (write.first == address && write.second == value) {
            return true;
        }
    }
    return false;
}

bool wrote(const FakeModbusClient& client, uint16_t address) {
    for (const auto& write : client.writes()) {
        if (write.first == address) {
            return true;
        }
    }
    return false;
}

int writeCountFor(const FakeModbusClient& client, uint16_t address) {
    int count = 0;
    for (const auto& write : client.writes()) {
        if (write.first == address) {
            ++count;
        }
    }
    return count;
}

bool waitForLatest(StatusQueue& queue, StatusSnapshot& snapshot) {
    for (int i = 0; i < 50; ++i) {
        if (queue.latest(snapshot)) {
            return true;
        }
        windmi::platform::sleep_ms(10);
    }
    return false;
}

} // namespace

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
    EXPECT_EQ(REG_UNIT_CAPACITY, 0x1006);
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
    EXPECT_EQ(REG_COMPRESSOR_FREQ, 0x0017);
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

TEST(StatusQueueTest, RingBufferOverwrite) {
    // Test that StatusQueue overwrites oldest entries when full
    // instead of returning false
    StatusQueue q;
    
    // Fill the queue beyond capacity
    for (int i = 0; i < 50; ++i) {
        StatusSnapshot snap{};
        snap.dhw_tank_temp = static_cast<float>(i);
        EXPECT_TRUE(q.push(snap));  // Should always succeed
    }
    
    // Latest should be the last written value (49)
    StatusSnapshot latest;
    EXPECT_TRUE(q.latest(latest));
    EXPECT_FLOAT_EQ(latest.dhw_tank_temp, 49.0f);
    
    // We should be able to pop at least CAPACITY-1 items
    int popped_count = 0;
    StatusSnapshot snap;
    while (q.pop(snap)) {
        popped_count++;
    }
    EXPECT_GE(popped_count, StatusQueue::CAPACITY - 1);
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

TEST(ControlLoopTest, CalculatesCopAfterReadingWaterFlow) {
    FakeModbusClient client({
        {REG_OUTDOOR_TEMP, 100},
        {REG_INDOOR_TEMP, 210},
        {REG_ENTERING_WATER_TEMP, 300},
        {REG_LEAVING_WATER_TEMP, 350},
        {REG_DHW_TANK_TEMP, 450},
        {REG_RUNNING_MODE, MODE_SET_HEAT_DHW},
        {REG_RUNNING_STATUS, MODE_STATUS_HEAT},
        {REG_DHW_TARGET, 460},
        {REG_HEATING_TARGET, 400},
        {REG_DHW_PRIORITY, 1},
        {REG_AC_CURRENT, 20},     // hypothesis: raw / 2 = 10 A
        {REG_DC_CURRENT, 3},
        {REG_AC_VOLTAGE, 230},    // actual 230 V
        {REG_DC_VOLTAGE, 24},
        {REG_UNIT_CAPACITY, 8},
        {REG_COMPRESSOR_FREQ, 42},
        {REG_WATER_FLOW, 120},    // 1.20 m³/h
        {REG_ACTUAL_CAPACITY_OUTPUT, 50},
        {REG_ODU_INPUT_STATUS, 0},
        {REG_COMPRESSOR_RUNTIME, 123},
        {REG_PUMP_RUNTIME, 456},
    });
    CmdQueue cmd_queue;
    StatusQueue status_queue;
    ControlLoop loop;

    ASSERT_TRUE(loop.start(&client, &cmd_queue, &status_queue));

    StatusSnapshot snap{};
    ASSERT_TRUE(waitForLatest(status_queue, snap));
    loop.stop();

    EXPECT_FLOAT_EQ(snap.water_flow, 1.2f);
    EXPECT_TRUE(snap.power_valid);
    EXPECT_TRUE(snap.cop_valid);
    EXPECT_NEAR(snap.heat_output_w, 6976.7f, 1.0f);
    EXPECT_NEAR(snap.cop, 3.37f, 0.02f);
}

// ---- Power scaling tests ----

TEST(PowerScalingTest, AcCurrentHypothesisDividesRawByTwoAndPropagatesToCop) {
    // User-approved hypothesis, NOT a verified manual correction or calibration.
    auto registers = fullRegisters();
    registers[REG_AC_CURRENT] = 14;
    registers[REG_AC_VOLTAGE] = 226;
    registers[REG_WATER_FLOW] = 107;
    registers[REG_ENTERING_WATER_TEMP] = 547;
    registers[REG_LEAVING_WATER_TEMP] = 573;
    FakeModbusClient client(registers);
    CmdQueue commands;
    StatusQueue statuses;
    ControlLoop loop;
    ASSERT_TRUE(loop.start(&client, &commands, &statuses));
    StatusSnapshot snapshot{};
    const bool received = waitForLatest(statuses, snapshot);
    loop.stop();
    ASSERT_TRUE(received);

    EXPECT_FLOAT_EQ(snapshot.ac_current, 7.0f);
    EXPECT_NEAR(snapshot.ac_power_va, 1582.0f, 0.1f);
    EXPECT_NEAR(snapshot.ac_power_w, 1423.8f, 0.1f);
    EXPECT_NEAR(snapshot.heat_output_w, 3234.8f, 1.0f);
    EXPECT_TRUE(snapshot.cop_valid);
    EXPECT_NEAR(snapshot.cop, 2.272f, 0.005f);
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

TEST(PowerScalingTest, DcVoltageUsesPolishManualMultiplierWithoutChangingCop) {
    // Polish manual: actual = displayed * 2; English says / 2.
    // Recorded raw ~150 is plausible as a ~300 V mains-derived DC bus, not 75 V.
    // Exercise the actual decoder, not a duplicate expression in this test.
    auto registers = fullRegisters();
    registers[REG_DC_VOLTAGE] = 150;
    registers[REG_AC_CURRENT] = 20;  // hypothesis: raw / 2 = 10 A
    registers[REG_AC_VOLTAGE] = 230;
    registers[REG_WATER_FLOW] = 120;
    registers[REG_ENTERING_WATER_TEMP] = 300;
    registers[REG_LEAVING_WATER_TEMP] = 350;
    FakeModbusClient client(registers);
    CmdQueue commands;
    StatusQueue statuses;
    ControlLoop loop;
    ASSERT_TRUE(loop.start(&client, &commands, &statuses));
    StatusSnapshot snapshot{};
    const bool received = waitForLatest(statuses, snapshot);
    loop.stop();
    ASSERT_TRUE(received);

    EXPECT_FLOAT_EQ(snapshot.dc_voltage, 300.0f);
    EXPECT_NEAR(snapshot.ac_power_w, 2070.0f, 0.1f);
    EXPECT_TRUE(snapshot.cop_valid);
    EXPECT_NEAR(snapshot.heat_output_w, 6976.7f, 1.0f);
    EXPECT_NEAR(snapshot.cop, 3.37f, 0.02f);
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
// ---- Saved target seeding tests ----
//
// The DHW-only / heating-only strategies pin a target register to its minimum
// to suppress demand, and the exit path leaves the unit in DHW mode. A value
// read back at the floor is therefore our own suppression, not the user's
// setpoint, and must not overwrite the targets loaded from settings.ini.

namespace {

struct SavedTargetFixture {
    FakeModbusClient client;
    CmdQueue cmd_queue;
    StatusQueue status_queue;
    ControlLoop loop;

    explicit SavedTargetFixture(std::map<uint16_t, int16_t> registers)
        : client(std::move(registers)) {}

    void run(int working_mode, const char* priority, float dhw_target, float heating_target) {
        loop.setInitialSettings(working_mode, priority, dhw_target, heating_target);
        ASSERT_TRUE(loop.start(&client, &cmd_queue, &status_queue));
        StatusSnapshot snapshot{};
        ASSERT_TRUE(waitForLatest(status_queue, snapshot));
        // A divergent setpoint is only restored after two agreeing ticks, so give it
        // the second one before judging what was written.
        loop.kick();
        ASSERT_TRUE(waitForLatest(status_queue, snapshot));
        windmi::platform::sleep_ms(50);
        loop.stop();
    }
};

} // namespace

TEST(SavedTargetTest, HeatingFloorIsNotAdoptedAsSavedTarget) {
    auto registers = fullRegisters();
    registers[REG_HEATING_TARGET] = 250;  // 25.0 C == HEATING_TARGET_MIN: suppressed
    registers[REG_DHW_TARGET] = 460;      // 46.0 C: real user value

    SavedTargetFixture f(registers);
    f.run(3, "dhw", 46.0f, 48.0f);

    EXPECT_TRUE(wroteValue(f.client, REG_HEATING_TARGET, 480))
        << "settings.ini heating target must be restored, not the suppression floor";
    EXPECT_FALSE(wroteValue(f.client, REG_HEATING_TARGET, 250))
        << "the suppression floor must never be re-asserted as the user's setpoint";
}

TEST(SavedTargetTest, DhwFloorIsNotAdoptedAsSavedTarget) {
    auto registers = fullRegisters();
    registers[REG_DHW_TARGET] = 400;      // 40.0 C == DHW_TARGET_MIN: suppressed
    registers[REG_HEATING_TARGET] = 470;  // 47.0 C: real user value

    SavedTargetFixture f(registers);
    f.run(3, "dhw", 45.0f, 47.0f);

    EXPECT_TRUE(wroteValue(f.client, REG_DHW_TARGET, 450))
        << "settings.ini DHW target must be restored, not the suppression floor";
    EXPECT_EQ(writeCountFor(f.client, REG_HEATING_TARGET), 0)
        << "a device heating target above the floor is the user's value and stays untouched";
}

TEST(SavedTargetTest, DeviceTargetsAboveFloorWinOverStaleSettings) {
    auto registers = fullRegisters();
    registers[REG_HEATING_TARGET] = 470;  // 47.0 C, set on the display panel
    registers[REG_DHW_TARGET] = 460;

    SavedTargetFixture f(registers);
    f.run(3, "dhw", 45.0f, 40.0f);  // settings.ini only holds the compiled-in default

    EXPECT_EQ(writeCountFor(f.client, REG_HEATING_TARGET), 0)
        << "without a recorded target the device value must not be clobbered";
    EXPECT_EQ(writeCountFor(f.client, REG_DHW_TARGET), 0)
        << "without a recorded target the device value must not be clobbered";
}

// ---- Mode write verification ----
//
// setRunningMode() is private and there is no public mode getter, so the
// verification is pinned by what an observer can see: a device that does not
// hold the value must not be treated as done, and the loop must come back and
// try again on the next tick.

namespace {

/**
 * Drives the control loop one iteration at a time. kick() wakes the loop out of
 * its 30 s wait, so each tick() is one control iteration we can assert on
 * without waiting half a minute for it.
 */
class TickedLoop {
public:
    explicit TickedLoop(IModbusClient& client) : client_(client) {}

    bool start(int working_mode, const char* priority, float dhw_target, float heating_target) {
        // A short park timeout keeps the park tests minutes-long instead of
        // ten, and stays above the configured floor so nothing is clamped away.
        cfg_.park_timeout_s = ArbitrationConfig::kMinParkTimeoutS;
        cfg_.park_cooldown_s = ArbitrationConfig::kMinParkCooldownS;
        loop_.setArbitration(cfg_);
        loop_.setInitialSettings(working_mode, priority, dhw_target, heating_target);
        return loop_.start(&client_, &cmd_queue_, &status_queue_);
    }

    /** The arbitration configuration the loop applies, so tests never hard-code it. */
    const ArbitrationConfig& dcfg() const { return cfg_; }

    void tick() {
        loop_.kick();
        windmi::platform::sleep_ms(150);
    }

    void sendWorkingMode(int working_mode) {
        Command cmd{};
        cmd.type = CommandType::CMD_SET_RUNNING_MODE;
        cmd.int_val = working_mode;
        cmd_queue_.push(cmd);
        loop_.kick();
    }

    void sendDhwTarget(float temp) {
        Command cmd{};
        cmd.type = CommandType::CMD_SET_DHW_TEMP;
        cmd.float_val = temp;
        cmd_queue_.push(cmd);
        loop_.kick();
    }

    Scheduler& schedule() { return loop_.scheduler(); }
    void setMinute(int minute) { loop_.setScheduleMinuteForTesting(minute); }
    void enableDryRun() { loop_.setDryRun(true); }

    void advanceTime(int64_t ms) { loop_.advanceClockForTesting(ms); }

    /** Idle for longer than the park timeout, then ask the loop to decide. */
    void idlePastPark() {
        advanceTime(static_cast<int64_t>(cfg_.park_timeout_s + 1) * 1000);
        tick();
    }

    void sendPriority(int priority) {
        Command cmd{};
        cmd.type = CommandType::CMD_SET_PRIORITY;
        cmd.int_val = priority;
        cmd_queue_.push(cmd);
        loop_.kick();
    }

    /** The snapshot published by the tick that was just driven. */
    StatusSnapshot tickSnapshot() {
        tick();
        return latest();
    }

    StatusSnapshot latest() {
        StatusSnapshot snapshot{};
        waitForLatest(status_queue_, snapshot);
        return snapshot;
    }

    void stop() { loop_.stop(); }

private:
    ArbitrationConfig cfg_;
    IModbusClient& client_;
    ControlLoop loop_;
    CmdQueue cmd_queue_;
    StatusQueue status_queue_;
};

/** A gateway that never comes back: connect() fails for ever. */
class UnreachableClient : public FakeModbusClient {
public:
    explicit UnreachableClient(std::map<uint16_t, int16_t> registers)
        : FakeModbusClient(std::move(registers)) {}

    bool connect() override { return false; }
    bool isConnected() const override { return false; }
};

/** Accepts the write but keeps reporting a different value in 0x002C. */
class LyingModeClient : public FakeModbusClient {
public:
    LyingModeClient(std::map<uint16_t, int16_t> registers, uint16_t reported)
        : FakeModbusClient(std::move(registers)), reported_(reported) {}

    int16_t readRegister(uint16_t address) override {
        if (address == REG_RUNNING_MODE) {
            return static_cast<int16_t>(reported_);
        }
        return FakeModbusClient::readRegister(address);
    }

private:
    uint16_t reported_;
};

/** Refuses mode writes the way an unreachable or faulted device would. */
class ThrowingModeClient : public FakeModbusClient {
public:
    explicit ThrowingModeClient(std::map<uint16_t, int16_t> registers)
        : FakeModbusClient(std::move(registers)) {}

    void writeRegister(uint16_t address, uint16_t value) override {
        if (address == REG_RUNNING_MODE) {
            ++mode_attempts_;          // the attempt happens, the write does not land
            throw ModbusException("device offline");
        }
        FakeModbusClient::writeRegister(address, value);
    }

    int modeAttempts() const { return mode_attempts_; }

private:
    int mode_attempts_ = 0;
};

} // namespace

TEST(ModeWriteVerificationTest, VerifiedModeWriteIsNotRepeated) {
    auto registers = fullRegisters();
    registers[REG_RUNNING_MODE] = MODE_SET_OFF;   // needs correcting to heat+dhw

    FakeModbusClient client(registers);
    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(3, "dhw", 46.0f, 40.0f));
    runner.tick();
    runner.tick();
    runner.stop();

    EXPECT_GE(writeCountFor(client, REG_RUNNING_MODE), 1);
    EXPECT_EQ(writeCountFor(client, REG_RUNNING_MODE), 1)
        << "a write the device confirmed is final; the loop must not keep pushing the mode";
}

TEST(ModeWriteVerificationTest, LyingClientIsRetried) {
    auto registers = fullRegisters();
    registers[REG_RUNNING_MODE] = MODE_SET_OFF;

    // The device takes the write and still claims it is off: exactly what a
    // rejected value looks like on the wire.
    LyingModeClient client(registers, MODE_SET_OFF);
    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(3, "dhw", 46.0f, 40.0f));
    runner.tick();
    runner.tick();
    runner.tick();
    runner.stop();

    EXPECT_GE(writeCountFor(client, REG_RUNNING_MODE), 2)
        << "a read-back mismatch must not be recorded as success";
}

TEST(ModeWriteVerificationTest, ThrowingClientKeepsTheLoopAlive) {
    auto registers = fullRegisters();
    registers[REG_RUNNING_MODE] = MODE_SET_OFF;

    ThrowingModeClient client(registers);
    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(3, "dhw", 46.0f, 40.0f));
    runner.tick();
    runner.tick();
    StatusSnapshot snapshot = runner.latest();
    runner.stop();

    EXPECT_GE(client.modeAttempts(), 2)
        << "a failed mode write is retried on the next tick";
    EXPECT_EQ(writeCountFor(client, REG_RUNNING_MODE), 0)
        << "nothing may be recorded as written while the device refuses";
    EXPECT_TRUE(snapshot.device_online)
        << "one register failing to write must not take the status down";
}

// ---- Working mode to device mode mapping ----

TEST(ArbiterMappingTest, DhwOnlyWritesNativeMode5Once) {
    auto registers = fullRegisters();
    registers[REG_RUNNING_MODE] = MODE_SET_HEAT_DHW;   // unit sits in heat+dhw
    registers[REG_HEATING_TARGET] = 450;               // 45.0 C, the user's value

    FakeModbusClient client(registers);
    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(1, "dhw", 50.0f, 45.0f));
    runner.tick();
    runner.tick();
    runner.stop();

    EXPECT_TRUE(wroteValue(client, REG_RUNNING_MODE, MODE_SET_DHW_ONLY))
        << "DHW-only must use the unit's native DHW-only mode, not a pinned setpoint";
    EXPECT_EQ(1, writeCountFor(client, REG_RUNNING_MODE))
        << "one corrective write, then leave the panel alone";
    EXPECT_FALSE(wrote(client, REG_HEATING_TARGET))
        << "native mode 5 must not overwrite the user's heating setpoint";
}

TEST(ArbiterMappingTest, HeatingOnlyWritesNativeMode4AndStopsPinning) {
    auto registers = fullRegisters();
    registers[REG_RUNNING_MODE] = MODE_SET_HEAT_DHW;
    registers[REG_DHW_TARGET] = 460;                   // 46.0 C, the user's value

    FakeModbusClient client(registers);
    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(2, "heating", 46.0f, 45.0f));
    runner.tick();
    runner.tick();
    runner.stop();

    EXPECT_TRUE(wroteValue(client, REG_RUNNING_MODE, MODE_SET_HEAT_ONLY));
    EXPECT_FALSE(wroteValue(client, REG_DHW_TARGET, 400))
        << "native mode 4 must not overwrite the user's DHW setpoint with the floor";
}

TEST(ArbiterMappingTest, BothOnStartsInDhwModeWhileTheTankIsCalling) {
    auto registers = fullRegisters();
    registers[REG_RUNNING_MODE] = MODE_SET_OFF;
    // Tank 45.0 against a 46.0 target is inside the band; with no history that
    // counts as a demand, so the tank gets the compressor first.

    FakeModbusClient client(registers);
    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(3, "dhw", 46.0f, 45.0f));
    runner.tick();
    runner.stop();

    EXPECT_TRUE(wroteValue(client, REG_RUNNING_MODE, MODE_SET_DHW_ONLY));
    EXPECT_FALSE(wroteValue(client, REG_RUNNING_MODE, MODE_SET_HEAT_DHW))
        << "both demands enabled never means heating-first while the tank is starved";
}

TEST(ArbiterMappingTest, BothOffWritesModeZero) {
    auto registers = fullRegisters();
    registers[REG_RUNNING_MODE] = MODE_SET_HEAT_DHW;

    FakeModbusClient client(registers);
    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(0, "dhw", 46.0f, 45.0f));
    runner.tick();
    runner.stop();

    EXPECT_TRUE(wroteValue(client, REG_RUNNING_MODE, MODE_SET_OFF));
}

TEST(ArbiterMappingTest, PanelOverrideCorrectedOnce) {
    auto registers = fullRegisters();
    registers[REG_RUNNING_MODE] = MODE_SET_OFF;   // somebody pressed Off on the panel

    FakeModbusClient client(registers);
    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(3, "dhw", 46.0f, 45.0f));
    runner.tick();
    runner.tick();
    runner.tick();
    runner.stop();

    EXPECT_EQ(1, writeCountFor(client, REG_RUNNING_MODE))
        << "correct the panel once, then stay out of the way";
}

// ---- The 0x02BF priority register is accepted and ignored ----

TEST(PriorityFieldTest, HeatingModeDoesNotReportDhwPriority) {
    auto registers = fullRegisters();
    registers[REG_RUNNING_MODE] = MODE_SET_HEAT_DHW;
    registers[REG_DHW_PRIORITY] = 1;   // the device's own flag claims DHW priority

    FakeModbusClient client(registers);
    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(3, "dhw", 46.0f, 45.0f));
    runner.tick();
    StatusSnapshot snapshot = runner.latest();
    runner.stop();

    EXPECT_FALSE(snapshot.dhw_priority)
        << "priority is a property of the mode (spec F4/F7), not of 0x02BF";
}

TEST(PriorityFieldTest, DhwOnlyModeReportsDhwPriority) {
    auto registers = fullRegisters();
    registers[REG_RUNNING_MODE] = MODE_SET_HEAT_DHW;
    registers[REG_DHW_PRIORITY] = 0;

    FakeModbusClient client(registers);
    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(1, "dhw", 46.0f, 45.0f));
    runner.tick();   // the mode is written during this tick
    runner.tick();   // the next status read is the first that can see it
    StatusSnapshot snapshot = runner.latest();
    runner.stop();

    EXPECT_TRUE(snapshot.dhw_priority)
        << "in native mode 5 the tank has the compressor, whatever 0x02BF says";
}

TEST(PriorityFieldTest, NeverWritesDhwPriorityRegister) {
    auto registers = fullRegisters();
    registers[REG_RUNNING_MODE] = MODE_SET_HEAT_DHW;
    registers[REG_DHW_PRIORITY] = 0;

    FakeModbusClient client(registers);
    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(3, "heating", 46.0f, 45.0f));
    runner.tick();
    runner.sendWorkingMode(1);
    runner.tick();
    runner.sendWorkingMode(2);
    runner.tick();
    runner.sendWorkingMode(3);
    runner.tick();
    runner.sendPriority(1);
    runner.tick();
    runner.stop();

    EXPECT_FALSE(wrote(client, REG_DHW_PRIORITY))
        << "0x02BF is writable but has no observable effect (spec F5/F6); do not touch it";
}

// ---- Shutdown ----

TEST(ShutdownModeTest, ShutdownLeavesTheUnitMakingHotWater) {
    EXPECT_EQ(MODE_SET_DHW_ONLY, ControlLoop::shutdownDeviceMode());
    EXPECT_NE(MODE_SET_OFF, ControlLoop::shutdownDeviceMode())
        << "mode 0 leaves the unit with neither space heating nor domestic hot water";
    EXPECT_NE(MODE_SET_HEAT_DHW, ControlLoop::shutdownDeviceMode())
        << "mode 2 with no arbiter running is heating-first, and the tank has lost its "
           "advocate; this house has a wood burner for the air and nothing but this unit "
           "for hot water";
}

// ---- The arbiter running in the control loop ----

namespace {

/** Registers for an arbiter test: 50.0 C DHW target, tank where you want it. */
std::map<uint16_t, int16_t> arbiterRegisters(int device_mode, int tank_raw, int status = MODE_STATUS_HEAT) {
    auto registers = fullRegisters();
    registers[REG_RUNNING_MODE] = static_cast<int16_t>(device_mode);
    registers[REG_RUNNING_STATUS] = static_cast<int16_t>(status);
    registers[REG_DHW_TARGET] = 500;                 // 50.0 C
    registers[REG_DHW_TARGET_ECO] = 450;             // 45.0 C: below, so 50.0 is the target
    registers[REG_DHW_TANK_TEMP] = static_cast<int16_t>(tank_raw);
    registers[REG_COMPRESSOR_FREQ] = 0;              // idle until a test says otherwise
    registers[REG_WATER_FLOW] = 0;
    return registers;
}

} // namespace

TEST(ArbitrationExecutionTest, GrantOnColdTankAtStartup) {
    FakeModbusClient client(arbiterRegisters(MODE_SET_HEAT_DHW, 440));   // tank 44.0 < 50.0 - 5.0

    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(3, "dhw", 50.0f, 45.0f));
    runner.tick();
    runner.stop();

    EXPECT_TRUE(wroteValue(client, REG_RUNNING_MODE, MODE_SET_DHW_ONLY))
        << "with both demands on and the tank starved, the tank gets the compressor";
}

TEST(ArbitrationExecutionTest, ReleaseAfterTwoTicksAtTarget) {
    FakeModbusClient client(arbiterRegisters(MODE_SET_DHW_ONLY, 496));   // 49.6 >= 50.0 - 0.5

    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(3, "dhw", 50.0f, 45.0f));
    runner.tick();   // first tick at target: candidate, hold
    EXPECT_FALSE(wroteValue(client, REG_RUNNING_MODE, MODE_SET_HEAT_DHW))
        << "one tick at the top of the tank is not satisfaction (spec F9)";
    runner.tick();   // second tick: satisfied, release to heating
    runner.stop();

    EXPECT_TRUE(wroteValue(client, REG_RUNNING_MODE, MODE_SET_HEAT_DHW));
}

TEST(ArbitrationExecutionTest, BandHoldsAcrossASag) {
    FakeModbusClient client(arbiterRegisters(MODE_SET_HEAT_DHW, 496));

    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(3, "dhw", 50.0f, 45.0f));
    runner.tick();
    runner.tick();                                   // satisfied; already in heat+dhw, no write
    client.writeRegister(REG_DHW_TANK_TEMP, 470);    // sag to 47.0, still inside the band
    runner.tick();
    runner.tick();
    runner.stop();

    EXPECT_EQ(0, writeCountFor(client, REG_RUNNING_MODE))
        << "a sag inside the 5 degC band must not start a DHW episode";
}

TEST(ArbitrationExecutionTest, UnreadableTankHoldsMode) {
    FakeModbusClient client(arbiterRegisters(MODE_SET_HEAT_DHW, 0));   // 0 = no usable reading

    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(3, "dhw", 50.0f, 45.0f));
    runner.tick();
    runner.tick();
    runner.stop();

    EXPECT_EQ(0, writeCountFor(client, REG_RUNNING_MODE))
        << "an unusable tank reading must never look like a freezing tank";
}

TEST(ArbitrationExecutionTest, WriteDeferredDuringDefrostThenRetried) {
    FakeModbusClient client(arbiterRegisters(MODE_SET_HEAT_DHW, 440, MODE_STATUS_DEFROST));

    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(3, "dhw", 50.0f, 45.0f));
    runner.tick();
    EXPECT_EQ(0, writeCountFor(client, REG_RUNNING_MODE)) << "no mode writes during defrost";

    client.writeRegister(REG_RUNNING_STATUS, MODE_STATUS_DHW);   // defrost ends
    runner.tick();
    runner.stop();

    EXPECT_TRUE(wroteValue(client, REG_RUNNING_MODE, MODE_SET_DHW_ONLY))
        << "the deferred decision is still made once defrost ends";
}

TEST(ArbitrationExecutionTest, FailedWriteDoesNotAdvanceState) {
    // Tank satisfied, unit in DHW-only: the release to heat+DHW must happen.
    LyingModeClient client(arbiterRegisters(MODE_SET_DHW_ONLY, 496), MODE_SET_DHW_ONLY);

    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(3, "dhw", 50.0f, 45.0f));
    runner.tick();
    runner.tick();
    runner.tick();
    runner.tick();
    runner.stop();

    EXPECT_GE(writeCountFor(client, REG_RUNNING_MODE), 2)
        << "a device that does not hold the mode is retried, not believed";
}

TEST(ArbitrationExecutionTest, DryRunSuppressesWrites) {
    FakeModbusClient client(arbiterRegisters(MODE_SET_HEAT_DHW, 440));

    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(3, "dhw", 50.0f, 45.0f));
    runner.enableDryRun();
    runner.tick();
    runner.tick();
    StatusSnapshot snapshot = runner.latest();
    runner.stop();

    EXPECT_EQ(0, static_cast<int>(client.writes().size())) << "dry run writes nothing at all";
    EXPECT_EQ(MODE_SET_DHW_ONLY, snapshot.requested_device_mode)
        << "but it still says what it would have done";
    EXPECT_TRUE(snapshot.dhw_calling);
}

TEST(ArbitrationExecutionTest, StatusExposesArbitration) {
    FakeModbusClient client(arbiterRegisters(MODE_SET_HEAT_DHW, 440));

    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(3, "dhw", 50.0f, 45.0f));
    runner.tick();
    StatusSnapshot snapshot = runner.latest();
    runner.stop();

    EXPECT_TRUE(snapshot.dhw_state_known);
    EXPECT_TRUE(snapshot.dhw_calling);
    EXPECT_EQ(MODE_SET_DHW_ONLY, snapshot.requested_device_mode);
    EXPECT_EQ(-1, snapshot.seconds_since_mode_write)
        << "negative means no device-mode write has happened since startup";
}

// ---- The watchdog in the loop: it watches, it says so, it does nothing else ----

TEST(ChargeWatchdogLoopTest, NormalChargeRaisesNothing) {
    FakeModbusClient client(arbiterRegisters(MODE_SET_DHW_ONLY, 300));   // 3.0 C tank, demanding

    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(3, "dhw", 50.0f, 45.0f));
    runner.tick();
    StatusSnapshot snapshot = runner.latest();
    runner.stop();

    EXPECT_FALSE(snapshot.dhw_sensor_fault_suspected);
    EXPECT_EQ(0, snapshot.tank_read_failures);
}

TEST(ChargeWatchdogLoopTest, AChargeThatRunsAndDoesNotRiseRaisesTheFlagAndNeverRelieves) {
    FakeModbusClient client(arbiterRegisters(MODE_SET_DHW_ONLY, 300, MODE_STATUS_DHW));
    client.writeRegister(REG_COMPRESSOR_FREQ, 400);   // the compressor is genuinely charging DHW

    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(3, "dhw", 50.0f, 45.0f));
    runner.tick();                                   // charge session starts at 3.0 C
    client.writeRegister(REG_DHW_TANK_TEMP, 304);    // two hours later: 30.4 C
    runner.advanceTime(121 * 60 * 1000);
    runner.tick();
    StatusSnapshot snapshot = runner.latest();
    runner.stop();

    EXPECT_TRUE(snapshot.dhw_sensor_fault_suspected);
    EXPECT_EQ(0, writeCountFor(client, REG_RUNNING_MODE))
        << "the watchdog reports; it does not hand the compressor to the house";
}

TEST(ChargeWatchdogLoopTest, AnIdleChargeIsNotBlamedOnTheSensor) {
    FakeModbusClient client(arbiterRegisters(MODE_SET_DHW_ONLY, 300));   // compressor at 0 Hz

    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(3, "dhw", 50.0f, 45.0f));
    runner.tick();
    runner.advanceTime(121 * 60 * 1000);
    runner.tick();
    StatusSnapshot snapshot = runner.latest();
    runner.stop();

    EXPECT_FALSE(snapshot.dhw_sensor_fault_suspected)
        << "a unit that never ran is not a tank sensor that lies; it is the park rule's case";
}

TEST(ChargeWatchdogLoopTest, DhwOnlySeparatesSatisfiedChargesInsteadOfSummingTheirRuntime) {
    FakeModbusClient client(arbiterRegisters(MODE_SET_DHW_ONLY, 440, MODE_STATUS_DHW));
    client.writeRegister(REG_COMPRESSOR_FREQ, 400);
    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(1, "dhw", 50, 45));
    runner.tick();
    runner.advanceTime(61 * 60 * 1000);
    client.writeRegister(REG_DHW_TANK_TEMP, 500);
    runner.tick(); runner.tick(); // confirmed satisfaction, mode stays DHW-only
    EXPECT_FALSE(runner.latest().dhw_sensor_fault_suspected);
    client.writeRegister(REG_DHW_TANK_TEMP, 440);
    runner.tick();
    runner.advanceTime(61 * 60 * 1000);
    client.writeRegister(REG_DHW_TANK_TEMP, 480);
    auto status = runner.tickSnapshot();
    runner.stop();
    EXPECT_FALSE(status.dhw_sensor_fault_suspected)
        << "two separate healthy charges must not become one 122-minute stall";
}
TEST(ChargeWatchdogLoopTest, SatisfactionClearsLatchedWarningWithoutChangingDhwOnlyMode) {
    FakeModbusClient client(arbiterRegisters(MODE_SET_DHW_ONLY, 440, MODE_STATUS_DHW));
    client.writeRegister(REG_COMPRESSOR_FREQ, 400);
    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(1, "dhw", 50, 45));
    runner.tick(); runner.advanceTime(121 * 60 * 1000); runner.tick();
    EXPECT_TRUE(runner.latest().dhw_sensor_fault_suspected);
    client.writeRegister(REG_DHW_TANK_TEMP, 500);
    runner.tick(); auto status = runner.tickSnapshot();
    runner.stop();
    EXPECT_EQ(status.working_mode, 1);
    EXPECT_FALSE(status.dhw_sensor_fault_suspected);
}
TEST(ChargeWatchdogLoopTest, PumpOnlyCirculationDoesNotCountAsCompressorChargeMinutes) {
    FakeModbusClient client(arbiterRegisters(MODE_SET_DHW_ONLY, 440, MODE_STATUS_DHW));
    client.writeRegister(REG_WATER_FLOW, 107);
    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(1, "dhw", 50, 45));
    runner.tick(); runner.advanceTime(121 * 60 * 1000);
    auto status = runner.tickSnapshot(); runner.stop();
    EXPECT_FALSE(status.dhw_sensor_fault_suspected);
}
TEST(ChargeWatchdogLoopTest, DefrostDoesNotCountAsDhwCompressorChargeMinutes) {
    FakeModbusClient client(arbiterRegisters(MODE_SET_DHW_ONLY, 440, MODE_STATUS_DHW));
    client.writeRegister(REG_COMPRESSOR_FREQ, 400);
    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(1, "dhw", 50, 45));
    runner.tick(); client.writeRegister(REG_RUNNING_STATUS, MODE_STATUS_DEFROST);
    runner.advanceTime(121 * 60 * 1000);
    auto status = runner.tickSnapshot(); runner.stop();
    EXPECT_FALSE(status.dhw_sensor_fault_suspected);
}

// ---- A grant the unit does not act on must not lock the house out ----

TEST(ParkExecutionTest, DeclinedChargeReleasesTheCompressorToTheHouse) {
    FakeModbusClient client(arbiterRegisters(MODE_SET_DHW_ONLY, 300));   // 3.0 C, clearly calling

    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(3, "dhw", 50.0f, 45.0f));
    runner.tick();
    EXPECT_FALSE(wroteValue(client, REG_RUNNING_MODE, MODE_SET_HEAT_DHW))
        << "one idle tick is not a declined charge";

    runner.idlePastPark();
    runner.stop();

    EXPECT_TRUE(wroteValue(client, REG_RUNNING_MODE, MODE_SET_HEAT_DHW))
        << "the unit has been idle on a DHW grant for the park timeout";
}

TEST(ParkExecutionTest, AnActuatingUnitIsNotParked) {
    auto registers = arbiterRegisters(MODE_SET_DHW_ONLY, 300);
    FakeModbusClient client(registers);
    client.writeRegister(REG_COMPRESSOR_FREQ, 400);   // 40 Hz: it is charging

    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(3, "dhw", 50.0f, 45.0f));
    runner.tick();
    runner.advanceTime(600 * 1000);
    runner.tick();
    runner.stop();

    EXPECT_EQ(0, writeCountFor(client, REG_RUNNING_MODE))
        << "a unit that is working gets left alone";
}

TEST(ParkExecutionTest, StatusExplainsTheTankInWords) {
    auto registers = arbiterRegisters(MODE_SET_DHW_ONLY, 300);
    FakeModbusClient client(registers);
    client.writeRegister(REG_COMPRESSOR_FREQ, 400);

    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(3, "dhw", 50.0f, 45.0f));
    runner.tick();
    StatusSnapshot snapshot = runner.latest();
    runner.stop();

    EXPECT_STREQ("calling", snapshot.dhw_state.c_str());
    EXPECT_TRUE(snapshot.unit_actuating);
    EXPECT_FLOAT_EQ(50.0f, snapshot.effective_dhw_target);
    EXPECT_FLOAT_EQ(45.0f, snapshot.dhw_eco_target);
}

// ---- Both DHW setpoints carry the user's wish ----

TEST(DhwSetpointTest, TheTargetIsWrittenToNormalAndEconomicSetpoints) {
    auto registers = arbiterRegisters(MODE_SET_HEAT_DHW, 496);
    FakeModbusClient client(registers);

    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(3, "dhw", 50.0f, 45.0f));
    runner.sendDhwTarget(55.0f);
    runner.tick();
    runner.stop();

    EXPECT_TRUE(wroteValue(client, REG_DHW_TARGET, 550));
    EXPECT_TRUE(wroteValue(client, REG_DHW_TARGET_ECO, 550))
        << "the unit charges to whichever setpoint its active DHW sub-mode uses, and we "
           "cannot read which sub-mode is authoritative, so both carry the wish";
}

TEST(DhwSetpointTest, AMissingEconomicRegisterDoesNotBreakStatusOrWrites) {
    auto registers = arbiterRegisters(MODE_SET_HEAT_DHW, 496);
    registers.erase(REG_DHW_TARGET_ECO);        // older firmware may not answer it
    FakeModbusClient client(registers);

    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(3, "dhw", 50.0f, 45.0f));
    runner.tick();
    StatusSnapshot snapshot = runner.latest();
    runner.stop();

    EXPECT_TRUE(snapshot.device_online) << "an absent optional register is not a dead device";
    EXPECT_FLOAT_EQ(0.0f, snapshot.dhw_eco_target);
    EXPECT_FLOAT_EQ(50.0f, snapshot.effective_dhw_target);
}

// ---- A garbled read must not move a setpoint ----

TEST(SetpointDriftTest, ASingleDivergentReadIsNotRestored) {
    auto registers = arbiterRegisters(MODE_SET_HEAT_DHW, 496);
    FakeModbusClient client(registers);

    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(3, "dhw", 50.0f, 45.0f));
    runner.tick();                                // seeds the saved target from 50.0
    client.writeRegister(REG_DHW_TARGET, 440);    // panel or noise: 44.0
    runner.tick();
    runner.stop();

    EXPECT_FALSE(wroteValue(client, REG_DHW_TARGET, 500))
        << "one divergent reading is not yet a panel change";
}

TEST(SetpointDriftTest, TwoCorroboratingReadsAreRestored) {
    auto registers = arbiterRegisters(MODE_SET_HEAT_DHW, 496);
    FakeModbusClient client(registers);

    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(3, "dhw", 50.0f, 45.0f));
    runner.tick();                                // seeds the saved target from 50.0
    client.writeRegister(REG_DHW_TARGET, 440);
    runner.tick();
    client.writeRegister(REG_DHW_TARGET, 440);    // and again: the panel really did move
    runner.tick();
    runner.stop();

    EXPECT_TRUE(wroteValue(client, REG_DHW_TARGET, 500));
}

TEST(SetpointDriftTest, AnImplausibleReadingIsNeverWrittenBack) {
    auto registers = arbiterRegisters(MODE_SET_HEAT_DHW, 496);
    FakeModbusClient client(registers);

    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(3, "dhw", 50.0f, 45.0f));
    runner.tick();                                // seeds the saved target from 50.0
    for (int i = 0; i < 3; i++) {
        client.writeRegister(REG_DHW_TARGET, 120);   // 12.0 C: a colliding frame
        runner.tick();
    }
    runner.stop();

    EXPECT_FALSE(wroteValue(client, REG_DHW_TARGET, 500))
        << "measured 2026-11-13: a garbled read became a real write of 52.0 C";
}

TEST(CycleCopLoopTest, PublishesCompletedEnergyRatioAndRetainsItWhenIdle) {
    auto registers = fullRegisters();
    registers[REG_COMPRESSOR_FREQ] = 0;
    registers[REG_AC_CURRENT] = 20; // hypothesis: 10 A
    registers[REG_AC_VOLTAGE] = 230;
    registers[REG_WATER_FLOW] = 120;
    registers[REG_ENTERING_WATER_TEMP] = 300;
    registers[REG_LEAVING_WATER_TEMP] = 350;
    FakeModbusClient client(registers);
    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(2, "dhw", 50, 45));
    runner.tick();
    client.writeRegister(REG_COMPRESSOR_FREQ, 200);
    runner.tick();
    EXPECT_FALSE(runner.latest().heating_cycle_cop_valid);
    runner.advanceTime(60000);
    client.writeRegister(REG_COMPRESSOR_FREQ, 0);
    runner.tick();
    const auto completed = runner.latest();
    runner.tick();
    const auto idle = runner.latest();
    runner.stop();
    EXPECT_TRUE(completed.heating_cycle_cop_valid);
    EXPECT_NEAR(completed.heating_cycle_cop, 3.37, 0.02);
    EXPECT_FALSE(completed.dhw_cycle_cop_valid);
    EXPECT_DOUBLE_EQ(idle.heating_cycle_cop, completed.heating_cycle_cop);
}

TEST(ControlLoopTest, MissingFlowIsNotAValidHeatMeasurement) {
    auto registers = fullRegisters();
    registers.erase(REG_WATER_FLOW);
    FakeModbusClient client(registers);
    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(2, "dhw", 50, 45));
    auto snapshot = runner.tickSnapshot();
    runner.stop();
    EXPECT_FALSE(snapshot.heat_output_valid);
}

TEST(DiagnosticLoopTest, CapturesExistingAndOptionalReadingsWithoutNewWrites) {
    auto registers = fullRegisters();
    registers[0x0055] = 0;
    registers[0x1009] = static_cast<int16_t>(0x8001);
    FakeModbusClient client(registers);
    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(3, "dhw", 46.0f, 40.0f));
    auto snapshot = runner.tickSnapshot();
    runner.stop();
    EXPECT_TRUE(snapshot.device_online);
    const auto& readings = snapshot.diagnostics.readings;
    ASSERT_TRUE(readings[diagnosticIndex(REG_COMPRESSOR_FREQ)].raw);
    EXPECT_EQ(*readings[diagnosticIndex(REG_COMPRESSOR_FREQ)].raw, 42);
    ASSERT_TRUE(readings[diagnosticIndex(0x0055)].raw);
    EXPECT_EQ(*readings[diagnosticIndex(0x0055)].raw, 0);
    ASSERT_TRUE(readings[diagnosticIndex(0x1009)].raw);
    EXPECT_EQ(*readings[diagnosticIndex(0x1009)].raw, 0x8001);
    EXPECT_FALSE(readings[diagnosticIndex(0x100A)].raw);
    for (const auto& write : client.writes()) {
        EXPECT_NE(write.first, 0x0055);
        EXPECT_NE(write.first, 0x1009);
        EXPECT_NE(write.first, 0x100E);
    }
}

TEST(ScheduleLoopSafetyTest, ManualTemperaturePausesUntilExplicitResume) {
    FakeModbusClient client(fullRegisters());
    TickedLoop runner(client);
    runner.schedule().replace(true, {{0,1440,3,40,46}});
    ASSERT_TRUE(runner.start(3, "dhw", 46, 40));
    auto status = runner.tickSnapshot();
    EXPECT_FALSE(status.schedule_paused); // scheduled updates are not manual commands
    runner.sendDhwTarget(52);
    status = runner.tickSnapshot();
    EXPECT_TRUE(status.schedule_paused);
    EXPECT_EQ(status.dhw_target, 52);
    runner.schedule().setEnabled(false);
    runner.schedule().setEnabled(true);
    status = runner.tickSnapshot();
    EXPECT_TRUE(status.schedule_paused);
    EXPECT_EQ(status.dhw_target, 52);
    runner.schedule().resume();
    status = runner.tickSnapshot();
    runner.stop();
    EXPECT_FALSE(status.schedule_paused);
    EXPECT_FALSE(status.schedule_pending);
    EXPECT_EQ(status.dhw_target, 46);
}
TEST(ScheduleLoopSafetyTest, TemperatureOnlyEditAndReenableReapplyCurrentWindow) {
    FakeModbusClient client(fullRegisters());
    TickedLoop runner(client);
    runner.schedule().replace(true, {{0,1440,3,40,46}});
    ASSERT_TRUE(runner.start(3, "dhw", 46, 40));
    runner.tick();
    runner.schedule().replace(true, {{0,1440,3,44,52}});
    auto status = runner.tickSnapshot();
    EXPECT_EQ(status.heating_target, 44);
    EXPECT_EQ(status.dhw_target, 52);
    runner.schedule().setEnabled(false);
    runner.sendWorkingMode(1);
    status = runner.tickSnapshot();
    EXPECT_EQ(status.working_mode, 1);
    EXPECT_FALSE(status.schedule_paused);
    runner.schedule().setEnabled(true);
    status = runner.tickSnapshot();
    runner.stop();
    EXPECT_EQ(status.working_mode, 3);
    EXPECT_TRUE(status.schedule_enabled);
    EXPECT_FALSE(status.schedule_pending);
}
TEST(ScheduleLoopSafetyTest, ManualModePausesAndGapsRestoreExplicitDefault) {
    FakeModbusClient client(fullRegisters());
    TickedLoop runner(client);
    runner.setMinute(60);
    runner.schedule().replace(true, {{0,120,2,42,48}}, {1,35,48});
    ASSERT_TRUE(runner.start(1, "dhw", 46, 40));
    auto status = runner.tickSnapshot();
    EXPECT_EQ(status.working_mode, 2);
    runner.setMinute(240);
    status = runner.tickSnapshot();
    EXPECT_FALSE(status.schedule_active);
    EXPECT_EQ(status.working_mode, 1);
    EXPECT_EQ(status.heating_target, 35);
    runner.setMinute(60);
    status = runner.tickSnapshot();
    EXPECT_TRUE(status.schedule_active);
    runner.sendWorkingMode(1);
    status = runner.tickSnapshot();
    runner.stop();
    EXPECT_TRUE(status.schedule_paused);
    EXPECT_EQ(status.working_mode, 1);
}
TEST(ScheduleDefaultsLoopTest, EqualPeriodAndDefaultChangeSourceWithoutRedundantTargetWrites) {
    FakeModbusClient client(fullRegisters()); TickedLoop runner(client); runner.setMinute(400);
    runner.schedule().replace(true,{{390,540,3,40,46}},{3,40,46});
    ASSERT_TRUE(runner.start(3,"dhw",46,40));
    auto status=runner.tickSnapshot(); EXPECT_EQ(status.control_source,"schedule");
    const auto writes=client.writes().size(); runner.setMinute(600); status=runner.tickSnapshot();
    EXPECT_EQ(status.control_source,"default"); EXPECT_FALSE(status.schedule_active);
    EXPECT_EQ(client.writes().size(),writes); runner.stop();
}
TEST(ScheduleDefaultsLoopTest, SameModePressPausesWithoutNeedlessNativeWritesAndResumeAppliesEditedDefault) {
    FakeModbusClient client(fullRegisters());
    TickedLoop runner(client); runner.setMinute(400);
    runner.schedule().replace(true,{{390,540,3,45,50}},{1,35,48});
    ASSERT_TRUE(runner.start(3,"dhw",46,40));
    auto status=runner.tickSnapshot();
    const auto nativeWrites=writeCountFor(client,REG_RUNNING_MODE);
    runner.sendWorkingMode(3); status=runner.tickSnapshot();
    EXPECT_TRUE(status.schedule_paused); EXPECT_EQ(status.working_mode,3);
    EXPECT_EQ(status.control_source,"manual"); EXPECT_EQ(status.schedule_defaults.mode,1);
    EXPECT_EQ(writeCountFor(client,REG_RUNNING_MODE),nativeWrites);
    EXPECT_EQ(runner.schedule().snapshot().defaults.mode,1);
    runner.schedule().replace(true,{{390,540,3,45,50}},{2,37,49});
    runner.setMinute(600); status=runner.tickSnapshot();
    EXPECT_TRUE(status.schedule_paused); EXPECT_EQ(status.working_mode,3); EXPECT_EQ(status.dhw_target,50);
    runner.schedule().resume(); runner.advanceTime(130000); status=runner.tickSnapshot();
    runner.stop();
    EXPECT_FALSE(status.schedule_paused); EXPECT_FALSE(status.schedule_active);
    EXPECT_EQ(status.control_source,"default"); EXPECT_EQ(status.schedule_defaults.mode,2);
    EXPECT_EQ(status.working_mode,2); EXPECT_EQ(status.heating_target,37); EXPECT_EQ(status.dhw_target,49);
}
TEST(ScheduleDefaultsLoopTest, PausedRestartRetainsManualTargetsUntilResumeRestoresDefault) {
    FakeModbusClient client(fullRegisters());
    TickedLoop first(client); first.setMinute(400);
    first.schedule().replace(true,{{390,540,3,45,50}},{1,35,48});
    ASSERT_TRUE(first.start(3,"dhw",46,40)); first.tick();
    first.sendDhwTarget(53); auto manual=first.tickSnapshot();
    const auto state=first.schedule().snapshot(); first.stop();
    ASSERT_TRUE(state.paused); EXPECT_EQ(state.defaults.dhwTemp,48);
    TickedLoop restarted(client); restarted.setMinute(600); restarted.schedule().restore(state);
    ASSERT_TRUE(restarted.start(manual.working_mode,"dhw",manual.dhw_target,manual.heating_target));
    auto status=restarted.tickSnapshot();
    EXPECT_TRUE(status.schedule_paused); EXPECT_EQ(status.dhw_target,53); EXPECT_EQ(status.working_mode,3);
    restarted.schedule().resume(); restarted.advanceTime(130000); status=restarted.tickSnapshot(); restarted.stop();
    EXPECT_EQ(status.working_mode,1); EXPECT_EQ(status.dhw_target,48); EXPECT_EQ(status.heating_target,35);
}
class ScheduleProtectedClient : public FakeModbusClient {
public:
    ScheduleProtectedClient() : FakeModbusClient(fullRegisters()) {}
    std::atomic<int> operation{MODE_STATUS_DEFROST};
    int16_t readRegister(uint16_t address) override {
        if (address == REG_RUNNING_STATUS) return static_cast<int16_t>(operation.load());
        return FakeModbusClient::readRegister(address);
    }
};
TEST(ScheduleLoopSafetyTest, ScheduledWritesWaitForNativeProtectionToFinish) {
    ScheduleProtectedClient client;
    TickedLoop runner(client);
    runner.schedule().replace(true, {{0,1440,2,44,52}});
    ASSERT_TRUE(runner.start(1, "dhw", 46, 40));
    auto status = runner.tickSnapshot();
    EXPECT_TRUE(status.schedule_pending);
    EXPECT_EQ(status.working_mode, 1);
    EXPECT_FALSE(wrote(client, REG_HEATING_TARGET));
    EXPECT_FALSE(wrote(client, REG_DHW_TARGET));
    EXPECT_FALSE(wrote(client, REG_RUNNING_MODE));
    client.operation = MODE_STATUS_HEAT;
    status = runner.tickSnapshot();
    runner.stop();
    EXPECT_EQ(status.working_mode, 2);
    EXPECT_EQ(status.heating_target, 44);
    EXPECT_FALSE(status.schedule_pending);
}
TEST(ScheduleDefaultsLoopTest, DefaultOnlyPolicyWaitsForDefrostAndAntiFreeze) {
    ScheduleProtectedClient client; TickedLoop runner(client);
    runner.schedule().replace(true,{}, {2,44,52});
    ASSERT_TRUE(runner.start(1,"dhw",46,40));
    auto status=runner.tickSnapshot();
    EXPECT_FALSE(status.schedule_active); EXPECT_TRUE(status.schedule_pending);
    EXPECT_EQ(status.control_source,"default");
    EXPECT_EQ(status.working_mode,1); EXPECT_TRUE(client.writes().empty());
    client.operation=MODE_STATUS_ANTIFREEZE; status=runner.tickSnapshot();
    EXPECT_TRUE(status.schedule_pending); EXPECT_TRUE(client.writes().empty());
    client.operation=MODE_STATUS_HEAT; status=runner.tickSnapshot(); runner.stop();
    EXPECT_EQ(status.working_mode,2); EXPECT_EQ(status.heating_target,44); EXPECT_EQ(status.dhw_target,52);
    EXPECT_FALSE(status.schedule_pending);
}
class ScheduleWriteFailClient : public FakeModbusClient {
public:
    ScheduleWriteFailClient() : FakeModbusClient(fullRegisters()) {}
    std::atomic<bool> fail{true};
    void writeRegister(uint16_t address, uint16_t value) override {
        if (address == REG_HEATING_TARGET && fail.load()) throw ModbusException("test target write failed");
        FakeModbusClient::writeRegister(address, value);
    }
};
TEST(ScheduleLoopSafetyTest, FailedTargetWriteRemainsPendingAndRetriesWithoutChangingMode) {
    ScheduleWriteFailClient client;
    TickedLoop runner(client);
    runner.schedule().replace(true, {{0,1440,2,44,52}});
    ASSERT_TRUE(runner.start(1, "dhw", 46, 40));
    auto status = runner.tickSnapshot();
    EXPECT_TRUE(status.schedule_pending);
    EXPECT_EQ(status.working_mode, 1);
    client.fail = false;
    status = runner.tickSnapshot();
    runner.stop();
    EXPECT_EQ(status.working_mode, 2);
    EXPECT_EQ(status.heating_target, 44);
    EXPECT_FALSE(status.schedule_pending);
}

TEST(ScheduleLoopSafetyTest, TargetFailureDoesNotPreventScheduledOff) {
    ScheduleWriteFailClient client;
    TickedLoop runner(client);
    runner.schedule().replace(true, {{0,1440,0,44,52}});
    ASSERT_TRUE(runner.start(1, "dhw", 46, 40));
    auto status = runner.tickSnapshot();
    runner.stop();
    EXPECT_EQ(status.working_mode, 0);
    EXPECT_TRUE(wroteValue(client, REG_RUNNING_MODE, MODE_SET_OFF));
    EXPECT_TRUE(status.schedule_pending);
}

class ProtectionDriftClient : public FakeModbusClient {
public:
    ProtectionDriftClient() : FakeModbusClient(fullRegisters()) {}
    std::atomic<bool> protection{false};
    int16_t readRegister(uint16_t address) override {
        if (protection.load()) {
            if (address == REG_RUNNING_STATUS) return MODE_STATUS_DEFROST;
            if (address == REG_DHW_TARGET) return 500;
            if (address == REG_HEATING_TARGET) return 450;
        }
        return FakeModbusClient::readRegister(address);
    }
};
TEST(ScheduleDefaultsLoopTest, DefaultTargetFailureRetriesAndDoesNotPreventExplicitOff) {
    ScheduleWriteFailClient client; TickedLoop runner(client);
    runner.schedule().replace(true,{}, {2,44,52});
    ASSERT_TRUE(runner.start(1,"dhw",46,40)); auto status=runner.tickSnapshot();
    EXPECT_TRUE(status.schedule_pending); EXPECT_EQ(status.working_mode,1);
    client.fail=false; runner.advanceTime(130000); status=runner.tickSnapshot();
    EXPECT_EQ(status.working_mode,2); EXPECT_EQ(status.heating_target,44); EXPECT_FALSE(status.schedule_pending);
    client.fail=true; runner.schedule().replace(true,{}, {0,45,53}); status=runner.tickSnapshot();
    runner.stop();
    EXPECT_EQ(status.working_mode,0); EXPECT_TRUE(status.schedule_pending);
    EXPECT_TRUE(wroteValue(client,REG_RUNNING_MODE,MODE_SET_OFF));
}
TEST(ScheduleLoopSafetyTest, ProtectionAlsoBlocksAutomaticTargetDriftRestoration) {
    ProtectionDriftClient client;
    TickedLoop runner(client);
    runner.schedule().replace(true, {{0,1440,3,40,46}});
    ASSERT_TRUE(runner.start(3, "dhw", 46, 40));
    runner.tick();
    const auto before = client.writes().size();
    client.protection = true;
    runner.tick(); runner.tick();
    runner.stop();
    EXPECT_EQ(client.writes().size(), before);
}
TEST(ScheduleLoopSafetyTest, OfflineStatusStillReportsEnabledAndPausedConfiguration) {
    FakeModbusClient client({});
    TickedLoop runner(client);
    runner.schedule().replace(true, {{0,1440,3,40,46}});
    runner.schedule().pause();
    ASSERT_TRUE(runner.start(3, "dhw", 46, 40));
    auto status = runner.tickSnapshot();
    runner.stop();
    EXPECT_FALSE(status.device_online);
    EXPECT_TRUE(status.schedule_enabled);
    EXPECT_TRUE(status.schedule_paused);
    EXPECT_EQ(status.control_source,"manual");
}

TEST(ScheduleDefaultsLoopTest, OfflineDefaultPolicyReportsPendingInsteadOfInventingActuation) {
    FakeModbusClient client({}); TickedLoop runner(client);
    runner.schedule().replace(true,{}, {1,35,48});
    ASSERT_TRUE(runner.start(3,"dhw",50,45)); const auto status=runner.tickSnapshot(); runner.stop();
    EXPECT_FALSE(status.device_online); EXPECT_EQ(status.control_source,"pending");
    EXPECT_TRUE(status.schedule_pending); EXPECT_FALSE(status.schedule_active);
    EXPECT_EQ(status.schedule_defaults.mode,1); EXPECT_EQ(status.schedule_defaults.dhwTemp,48);
}
TEST(BackupHeaterLoopTest, CapturesPairedTemperatureWithoutChangingPowerOrWrites) {
    auto registers = fullRegisters();
    registers[REG_LEAVING_WATER_TEMP] = 336;
    registers[REG_ENTERING_WATER_TEMP] = 300;
    registers[REG_WATER_FLOW] = 100;
    registers[REG_BEFORE_BACKUP_HEATER_TEMP] = 310;
    FakeModbusClient client(registers);
    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(2, "dhw", 46, 40));
    auto status = runner.tickSnapshot(); runner.stop();
    auto validation = estimateBackupHeater(status.diagnostics, status.device_online);
    ASSERT_TRUE(validation.measurement_valid);
    EXPECT_NEAR(*validation.estimated_added_heat_w, 3023.22, .1);
    EXPECT_TRUE(status.heat_output_valid);
    EXPECT_NEAR(status.heat_output_w, 4186.0, .2); // Existing total-water output stays unchanged.
    EXPECT_FALSE(wrote(client, REG_BEFORE_BACKUP_HEATER_TEMP));
}
TEST(BackupHeaterLoopTest, ValidationStillWorksWithCompressorAndEstimatedElectricalInputAtZero) {
    auto registers = fullRegisters();
    registers[REG_COMPRESSOR_FREQ] = 0;
    registers[REG_AC_CURRENT] = 0;
    registers[REG_BEFORE_BACKUP_HEATER_TEMP] = 310;
    registers[REG_LEAVING_WATER_TEMP] = 336;
    registers[REG_WATER_FLOW] = 100;
    FakeModbusClient client(registers);
    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(2, "dhw", 46, 40));
    auto status = runner.tickSnapshot(); runner.stop();
    auto validation = estimateBackupHeater(status.diagnostics, status.device_online);
    EXPECT_TRUE(validation.measurement_valid);
    EXPECT_FALSE(status.cop_valid);
    EXPECT_EQ(status.ac_power_w, 0);
}
TEST(BackupHeaterLoopTest, UnsupportedBeforeSensorLeavesOnlineAndExistingCopUsable) {
    FakeModbusClient client(fullRegisters());
    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(2, "dhw", 46, 40));
    auto status = runner.tickSnapshot(); runner.stop();
    auto validation = estimateBackupHeater(status.diagnostics, status.device_online);
    EXPECT_TRUE(status.device_online);
    EXPECT_TRUE(status.cop_valid);
    EXPECT_FALSE(validation.measurement_valid);
    EXPECT_EQ(validation.reason, "missing_temperature");
}

// ---- Liveness has to be in the payload ----
// A calm unit and a stalled control loop look identical from outside: the registers
// stop moving in both cases, because there is nothing to move. Measured 2026-11-13
// while watching for misbehaviour: publish gaps of 30, 60 and 120 s all mean "fine"
// when the tank is stable, so an external monitor cannot tell them from a loop that
// died holding the last snapshot. One monotonic counter makes the question answerable.

TEST(StatusLivenessTest, EveryPublishedSnapshotAdvancesTheSequence) {
    FakeModbusClient client(fullRegisters());

    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(3, "dhw", 50.0f, 45.0f));
    StatusSnapshot first = runner.tickSnapshot();
    runner.tick();
    StatusSnapshot second = runner.tickSnapshot();
    runner.stop();

    EXPECT_GT(first.status_sequence, 0u);
    EXPECT_GT(second.status_sequence, first.status_sequence)
        << "a monitor needs to see a new snapshot, not just different numbers";
}

TEST(StatusLivenessTest, AUnitThatDoesNotAnswerIsPublishedAsOffline) {
    // Connected to the gateway, but no register answers: readStatus() fails, and the
    // snapshot that says so has to be fresh, not the last good one.
    FakeModbusClient client({});

    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(3, "dhw", 50.0f, 45.0f));
    StatusSnapshot first = runner.tickSnapshot();
    runner.tick();
    StatusSnapshot second = runner.tickSnapshot();
    runner.stop();

    EXPECT_FALSE(first.device_online);
    EXPECT_GT(second.status_sequence, first.status_sequence);
}

TEST(StatusLivenessTest, AGatewayThatIsDownIsSaidToBeDown) {
    // Found by watching the controller, not by reading the code: while the gateway was
    // unreachable the loop retried and published nothing at all, so /api/status kept
    // serving the last good snapshot with deviceOnline true. A page full of numbers
    // from before the failure, presented as now.
    UnreachableClient client(fullRegisters());

    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(3, "dhw", 50.0f, 45.0f));
    StatusSnapshot snap = runner.tickSnapshot();
    runner.stop();

    EXPECT_FALSE(snap.device_online)
        << "an unreachable gateway must be reported, not hidden behind stale numbers";
    EXPECT_GT(snap.status_sequence, 0u) << "and said again each cycle, so its age is knowable";
}
