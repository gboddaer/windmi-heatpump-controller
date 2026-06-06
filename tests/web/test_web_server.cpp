/**
 * @file tests/web/test_web_server.cpp
 * @brief Web Server unit tests
 */

#include <gtest/gtest.h>
#include "web/WebServer.hpp"
#include "core/ControlLoop.hpp"
#include "config.h"

using namespace windmi;

// ---- Local helper functions matching WebServer.cpp logic ----

namespace {

static const char* mode_to_string(int mode) {
    switch (mode) {
        case MODE_SET_OFF:      return "off";
        case MODE_SET_COOL_DHW: return "cool+dhw";
        case MODE_SET_HEAT_DHW: return "heat+dhw";
        default:                return "unknown";
    }
}

static const char* status_to_string(int status) {
    switch (status) {
        case MODE_STATUS_OFF:        return "off";
        case MODE_STATUS_COOL:       return "cool";
        case MODE_STATUS_HEAT:       return "heat";
        case MODE_STATUS_DHW:        return "dhw";
        case MODE_STATUS_DEFROST:    return "defrost";
        case MODE_STATUS_ANTIFREEZE: return "antifreeze";
        default:                     return "unknown";
    }
}

} // anonymous namespace

// ---- WebServer construction tests ----

TEST(WebServerTest, CreateServerWithQueues) {
    CmdQueue cmd_queue;
    StatusQueue status_queue;
    EXPECT_NO_THROW({
        WebServer server(12345, "static", &cmd_queue, &status_queue);
    });
}

TEST(WebServerTest, IsShuttingDownInitiallyFalse) {
    CmdQueue cmd_queue;
    StatusQueue status_queue;
    WebServer server(12346, "static", &cmd_queue, &status_queue);
    EXPECT_FALSE(server.isShuttingDown());
}

TEST(WebServerTest, StopSetsShuttingDown) {
    CmdQueue cmd_queue;
    StatusQueue status_queue;
    WebServer server(12347, "static", &cmd_queue, &status_queue);
    server.stop();
    EXPECT_TRUE(server.isShuttingDown());
}

// ---- JSON field mapping tests ----

TEST(WebServerJsonTest, StatusSnapshotMatchesJsonFields) {
    StatusSnapshot snap{};
    snap.dhw_tank_temp = 1.0f;
    snap.dhw_target = 2.0f;
    snap.leaving_water_temp = 3.0f;
    snap.heating_target = 4.0f;
    snap.outdoor_temp = 5.0f;
    snap.running_mode = 2;
    snap.running_status = 2;
    snap.dhw_priority = true;
    snap.is_running = true;
    snap.device_online = true;
    snap.ac_current = 6.0f;
    snap.dc_current = 7.0f;
    snap.ac_voltage = 8.0f;
    snap.dc_voltage = 9.0f;
    snap.ac_power_va = 10.0f;
    snap.ac_power_w = 10.0f * ESTIMATED_POWER_FACTOR;
    snap.power_valid = true;
    snap.working_mode = 3;

    EXPECT_FLOAT_EQ(snap.dhw_tank_temp, 1.0f);
    EXPECT_FLOAT_EQ(snap.dhw_target, 2.0f);
    EXPECT_EQ(snap.running_mode, 2);
    EXPECT_TRUE(snap.dhw_priority);
    EXPECT_TRUE(snap.is_running);
    EXPECT_FLOAT_EQ(snap.ac_power_w, 10.0f * ESTIMATED_POWER_FACTOR);
    EXPECT_EQ(snap.working_mode, 3);
}

// ---- mode_to_string tests ----

TEST(WebServerJsonTest, ModeToStringValues) {
    EXPECT_STREQ(mode_to_string(MODE_SET_OFF), "off");
    EXPECT_STREQ(mode_to_string(MODE_SET_COOL_DHW), "cool+dhw");
    EXPECT_STREQ(mode_to_string(MODE_SET_HEAT_DHW), "heat+dhw");
}

// ---- status_to_string tests ----

TEST(WebServerJsonTest, StatusToStringValues) {
    EXPECT_STREQ(status_to_string(MODE_STATUS_OFF), "off");
    EXPECT_STREQ(status_to_string(MODE_STATUS_COOL), "cool");
    EXPECT_STREQ(status_to_string(MODE_STATUS_HEAT), "heat");
    EXPECT_STREQ(status_to_string(MODE_STATUS_DHW), "dhw");
    EXPECT_STREQ(status_to_string(MODE_STATUS_DEFROST), "defrost");
    EXPECT_STREQ(status_to_string(MODE_STATUS_ANTIFREEZE), "antifreeze");
}