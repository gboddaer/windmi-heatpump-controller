/**
 * @file tests/core/test_status_monitor.cpp
 * @brief Status Monitor unit tests
 */

#include "gtest/gtest.h"
#include "core/StatusMonitor.hpp"

using namespace windmi;

TEST(StatusMonitorTest, DefaultState) {
    StatusMonitor monitor;
    EXPECT_FALSE(monitor.isValid());
    
    StatusSnapshot snapshot{};
    monitor.get(snapshot);
    EXPECT_EQ(snapshot.dhw_tank_temp, 0.0f);
}

TEST(StatusMonitorTest, UpdateSnapshot) {
    StatusMonitor monitor;
    
    StatusSnapshot snapshot{};
    snapshot.dhw_tank_temp = 45.5f;
    snapshot.dhw_target = 46.0f;
    snapshot.device_online = true;
    
    monitor.update(snapshot);
    
    StatusSnapshot read{};
    EXPECT_TRUE(monitor.get(read));
    EXPECT_EQ(read.dhw_tank_temp, 45.5f);
    EXPECT_EQ(read.dhw_target, 46.0f);
    EXPECT_TRUE(read.device_online);
}

TEST(StatusMonitorTest, MultipleUpdates) {
    StatusMonitor monitor;
    
    StatusSnapshot s1{};
    s1.dhw_tank_temp = 40.0f;
    monitor.update(s1);
    
    StatusSnapshot s2{};
    s2.dhw_tank_temp = 45.0f;
    monitor.update(s2);
    
    StatusSnapshot read{};
    monitor.get(read);
    EXPECT_EQ(read.dhw_tank_temp, 45.0f);
}

TEST(StatusMonitorTest, ThreadSafety) {
    StatusMonitor monitor;
    
    // Update from main thread
    StatusSnapshot snapshot{};
    snapshot.dhw_tank_temp = 50.0f;
    monitor.update(snapshot);
    
    // Read should get the updated value
    StatusSnapshot read{};
    monitor.get(read);
    EXPECT_EQ(read.dhw_tank_temp, 50.0f);
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
