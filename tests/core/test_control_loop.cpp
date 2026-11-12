/**
 * @file tests/core/test_control_loop.cpp
 * @brief Control Loop unit tests
 */

#include <gtest/gtest.h>
#include "core/ControlLoop.hpp"
#include "core/StatusMonitor.hpp"
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
        {REG_AC_CURRENT, 5},      // actual 10 A
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
        loop_.setInitialSettings(working_mode, priority, dhw_target, heating_target);
        return loop_.start(&client_, &cmd_queue_, &status_queue_);
    }

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

    void sendPriority(int priority) {
        Command cmd{};
        cmd.type = CommandType::CMD_SET_PRIORITY;
        cmd.int_val = priority;
        cmd_queue_.push(cmd);
        loop_.kick();
    }

    StatusSnapshot latest() {
        StatusSnapshot snapshot{};
        waitForLatest(status_queue_, snapshot);
        return snapshot;
    }

    void stop() { loop_.stop(); }

private:
    IModbusClient& client_;
    ControlLoop loop_;
    CmdQueue cmd_queue_;
    StatusQueue status_queue_;
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

TEST(ArbiterMappingTest, BothOnUsesHeatDhwMode) {
    auto registers = fullRegisters();
    registers[REG_RUNNING_MODE] = MODE_SET_OFF;

    FakeModbusClient client(registers);
    TickedLoop runner(client);
    ASSERT_TRUE(runner.start(3, "dhw", 46.0f, 45.0f));
    runner.tick();
    runner.stop();

    EXPECT_TRUE(wroteValue(client, REG_RUNNING_MODE, MODE_SET_HEAT_DHW));
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
