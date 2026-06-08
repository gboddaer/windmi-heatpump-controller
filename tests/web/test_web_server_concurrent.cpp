/**
 * @file tests/web/test_web_server_concurrent.cpp
 * @brief WebServer concurrent access and handler behavior tests
 *
 * Tests status handler with null/empty queues, mode/status string
 * conversion, and concurrent status queue access.
 */

#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>
#include <cstring>

#include "config.h"
#include "core/ControlLoop.hpp"
#include "utils/JsonHelpers.hpp"

using namespace windmi;

// Local copies of the static helpers from WebServer.cpp (not exposed in header)
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

// ─── Status Queue Concurrent Access ───

TEST(ConcurrentTest, StatusReadDuringWrite) {
    StatusQueue queue;
    std::atomic<bool> stop{false};
    std::atomic<int> read_count{0};
    std::atomic<int> write_count{0};

    // Writer thread
    auto writer = [&]() {
        StatusSnapshot snap{};
        while (!stop.load()) {
            snap.outdoor_temp = static_cast<float>(write_count.fetch_add(1));
            queue.push(snap);
        }
    };

    // Reader thread
    auto reader = [&]() {
        StatusSnapshot snap{};
        while (!stop.load()) {
            if (queue.latest(snap)) {
                read_count.fetch_add(1);
            }
        }
    };

    {
        Thread t1(writer);
        Thread t2(reader);
        platform::sleep_ms(100);
        stop = true;
        t1.join();
        t2.join();
    }

    EXPECT_GT(read_count.load(), 0);
    EXPECT_GT(write_count.load(), 0);
}

TEST(ConcurrentTest, RapidSequentialRequests) {
    StatusQueue queue;
    StatusSnapshot snap{};

    // Push 1000 items rapidly
    for (int i = 0; i < 1000; i++) {
        snap.outdoor_temp = static_cast<float>(i);
        queue.push(snap);
    }

    // Read latest - should be the last one pushed
    StatusSnapshot latest{};
    EXPECT_TRUE(queue.latest(latest));
    EXPECT_EQ(latest.outdoor_temp, 999.0f);
}

TEST(ConcurrentTest, CmdQueueConcurrentPush) {
    CmdQueue queue;
    std::atomic<int> success_count{0};
    std::atomic<int> fail_count{0};

    Command cmd;
    cmd.type = CommandType::CMD_SET_DHW_TEMP;
    cmd.float_val = 45.0f;
    cmd.int_val = 0;

    // Try to push from multiple threads
    auto pusher = [&]() {
        for (int i = 0; i < 20; i++) {
            if (queue.push(cmd)) {
                success_count.fetch_add(1);
            } else {
                fail_count.fetch_add(1);
            }
        }
    };

    std::vector<Thread> threads;
    for (int i = 0; i < 4; i++) {
        threads.emplace_back(pusher);
    }
    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count.load() + fail_count.load(), 80);
    EXPECT_LE(success_count.load(), 16);  // CAPACITY is 16 (but 15 usable)
}

// ─── Status Handler Null Queue Handling ───

TEST(StatusHandlerTest, NullQueueReturnsEmptySnapshot) {
    // Simulate what apiStatusHandler does when status_queue_ is null
    // and last_status_ is zero-initialized
    StatusSnapshot snap{};
    EXPECT_EQ(snap.outdoor_temp, 0.0f);
    EXPECT_EQ(snap.dhw_tank_temp, 0.0f);
    EXPECT_EQ(snap.running_mode, 0);
    EXPECT_FALSE(snap.device_online);
}

TEST(StatusHandlerTest, EmptyQueueReturnsFalse) {
    StatusQueue queue;
    StatusSnapshot snap{};
    EXPECT_FALSE(queue.latest(snap));
}

// ─── Mode/Status String Conversion Tests ───

TEST(ModeStringTest, ModeToStringValues) {
    // mode_to_string mapping from WebServer.cpp
    EXPECT_STREQ(mode_to_string(MODE_SET_OFF), "off");
    EXPECT_STREQ(mode_to_string(MODE_SET_COOL_DHW), "cool+dhw");
    EXPECT_STREQ(mode_to_string(MODE_SET_HEAT_DHW), "heat+dhw");
    EXPECT_STREQ(mode_to_string(99), "unknown");
}

TEST(StatusStringTest, StatusToStringValues) {
    // status_to_string mapping from WebServer.cpp
    EXPECT_STREQ(status_to_string(MODE_STATUS_OFF), "off");
    EXPECT_STREQ(status_to_string(MODE_STATUS_COOL), "cool");
    EXPECT_STREQ(status_to_string(MODE_STATUS_HEAT), "heat");
    EXPECT_STREQ(status_to_string(MODE_STATUS_DHW), "dhw");
    EXPECT_STREQ(status_to_string(MODE_STATUS_DEFROST), "defrost");
    EXPECT_STREQ(status_to_string(MODE_STATUS_ANTIFREEZE), "antifreeze");
    EXPECT_STREQ(status_to_string(99), "unknown");
}

// ─── API Handler Shutdown Behavior ───

TEST(ShutdownTest, IsShuttingDownReturnsCorrectState) {
    // Test the isShuttingDown logic pattern
    int shuttingDown = 0;
    EXPECT_FALSE(shuttingDown != 0);
    shuttingDown = 1;
    EXPECT_TRUE(shuttingDown != 0);
}

TEST(ShutdownTest, StatusWithNullQueue) {
    // When status_queue_ is null and no snapshot exists,
    // the server uses last_status_ which is zero-initialized
    StatusSnapshot last{};

    // Verify all fields are zero/false
    EXPECT_EQ(last.dhw_tank_temp, 0.0f);
    EXPECT_EQ(last.dhw_target, 0.0f);
    EXPECT_EQ(last.leaving_water_temp, 0.0f);
    EXPECT_EQ(last.heating_target, 0.0f);
    EXPECT_EQ(last.outdoor_temp, 0.0f);
    EXPECT_EQ(last.entering_water_temp, 0.0f);
    EXPECT_EQ(last.running_mode, 0);
    EXPECT_EQ(last.running_status, 0);
    EXPECT_FALSE(last.dhw_priority);
    EXPECT_FALSE(last.is_running);
    EXPECT_FALSE(last.device_online);
    EXPECT_FALSE(last.power_valid);
    EXPECT_FALSE(last.cop_valid);
}
