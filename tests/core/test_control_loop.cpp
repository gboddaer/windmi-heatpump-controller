/**
 * @file tests/core/test_control_loop.cpp
 * @brief Control Loop unit tests
 */

#include "gtest/gtest.h"
#include "core/ControlLoop.hpp"
#include "core/StatusMonitor.hpp"

using namespace windmi;

// Mock Modbus client for testing
class MockModbusClient {
public:
    int16_t readRegister(uint16_t) { return 450; }  // Return 45.0°C
    void writeRegister(uint16_t, uint16_t) {}
    void flushBuffer() {}
};

TEST(ControlLoopTest, CreateControlLoop) {
    ControlLoop loop;
    // Loop should be created
}

TEST(ControlLoopTest, StartAndStop) {
    ControlLoop loop;
    EXPECT_TRUE(loop.start());
    EXPECT_TRUE(loop.isRunning());
    loop.stop();
    EXPECT_FALSE(loop.isRunning());
}

TEST(ControlLoopTest, EnqueueCommand) {
    ControlLoop loop;
    EXPECT_TRUE(loop.enqueueCommand(CommandType::CMD_SET_DHW_TEMP, 45.0f, 0));
}

TEST(ControlLoopTest, GetStatus) {
    ControlLoop loop;
    StatusSnapshot snapshot{};
    EXPECT_TRUE(loop.getStatus(snapshot));
}

// Test StatusMonitor
TEST(StatusMonitorTest, CreateMonitor) {
    StatusMonitor monitor;
    EXPECT_FALSE(monitor.isValid());
}

TEST(StatusMonitorTest, UpdateAndRead) {
    StatusMonitor monitor;
    
    StatusSnapshot snapshot{};
    snapshot.dhw_tank_temp = 45.0f;
    snapshot.device_online = true;
    
    monitor.update(snapshot);
    EXPECT_TRUE(monitor.isValid());
    
    StatusSnapshot read_snapshot{};
    EXPECT_TRUE(monitor.get(read_snapshot));
    EXPECT_EQ(read_snapshot.dhw_tank_temp, 45.0f);
}

TEST(StatusMonitorTest, CopyGet) {
    StatusMonitor monitor;
    
    StatusSnapshot snapshot{};
    snapshot.dhw_tank_temp = 50.0f;
    
    monitor.update(snapshot);
    
    StatusSnapshot copy = monitor.get();
    EXPECT_EQ(copy.dhw_tank_temp, 50.0f);
}
