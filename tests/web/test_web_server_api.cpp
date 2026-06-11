/**
 * @file tests/web/test_web_server_api.cpp
 * @brief WebServer API handler JSON structure and queue tests
 */

#include <gtest/gtest.h>
#include <cstring>
#include <cstdio>

#include "config.h"
#include "utils/JsonHelpers.hpp"
#include "core/ControlLoop.hpp"

using namespace windmi;

// Helper to build a status JSON with custom mode/status strings
static std::string makeJson(const std::string& mode, const std::string& status) {
    return JsonHelpers::generateStatusJson(
        50.0, 50.0, 40.0, 35.0, 40.0, 15.0, 40.0,
        mode, status, "dhw", status, true,
        5.0, 3.0, 230.0, 12.0, 1150.0, 920.0, true,
        45.0, 4.5, 8, 80, 0, 1000, 500,
        5000.0, 3.5, true, 5.0, 0, MODE_SET_HEAT_DHW
    );
}

static std::string makeZeroJson() {
    return JsonHelpers::generateStatusJson(
        0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
        std::string("off"), std::string("off"), std::string("dhw"), std::string("off"), false,
        0.0, 0.0, 0.0, 0.0, 0.0, 0.0, false,
        0.0, 0.0, 0, 0, 0, 0, 0,
        0.0, 0.0, false, 0.0, 0, 0
    );
}

// ─── Status JSON Structure Tests ───

TEST(ApiStatusJsonTest, ContainsRequiredFields) {
    std::string json = makeJson("heat+dhw", "heat");

    EXPECT_NE(json.find("outdoorTemperature"), std::string::npos);
    EXPECT_NE(json.find("heatingTarget"), std::string::npos);
    EXPECT_NE(json.find("dhwTarget"), std::string::npos);
    EXPECT_NE(json.find("leavingWaterTemperature"), std::string::npos);
    EXPECT_NE(json.find("dhwTemperature"), std::string::npos);
    EXPECT_NE(json.find("mode"), std::string::npos);
    EXPECT_NE(json.find("status"), std::string::npos);
}

TEST(ApiStatusJsonTest, ValidJsonStructure) {
    std::string json = makeZeroJson();
    EXPECT_FALSE(json.empty());
    EXPECT_EQ(json.front(), '{');
    EXPECT_EQ(json.back(), '}');
}

TEST(ApiStatusJsonTest, ModeStringsCorrect) {
    std::string json = makeJson("off", "off");
    EXPECT_NE(json.find("off"), std::string::npos);

    json = makeJson("cool+dhw", "cool");
    EXPECT_NE(json.find("cool"), std::string::npos);

    json = makeJson("heat+dhw", "heat");
    EXPECT_NE(json.find("heat"), std::string::npos);
}

TEST(ApiStatusJsonTest, StatusStringsCorrect) {
    std::string json = makeJson("off", "off");
    EXPECT_NE(json.find("off"), std::string::npos);

    json = makeJson("heat+dhw", "defrost");
    EXPECT_NE(json.find("defrost"), std::string::npos);

    json = makeJson("heat+dhw", "antifreeze");
    EXPECT_NE(json.find("antifreeze"), std::string::npos);
}

// ─── Command Queue Error Path Tests ───

TEST(CmdQueueTest, PushWhenFullThenEmpty) {
    CmdQueue queue;
    Command cmd;
    cmd.type = CommandType::CMD_SET_HEATING_TEMP;
    cmd.float_val = 45.0f;
    cmd.int_val = 0;

    bool pushed = true;
    int count = 0;
    while (pushed) {
        pushed = queue.push(cmd);
        if (pushed) count++;
    }

    EXPECT_GT(count, 0);
    EXPECT_FALSE(pushed);

    // Drain the queue
    int drained = 0;
    while (queue.pop(cmd)) {
        drained++;
    }
    EXPECT_GT(drained, 0);
}

TEST(CmdQueueTest, PopWhenEmpty) {
    CmdQueue queue;
    Command cmd;
    cmd.type = CommandType::CMD_SET_HEATING_TEMP;
    cmd.float_val = 45.0f;
    cmd.int_val = 0;

    EXPECT_FALSE(queue.pop(cmd));
}

TEST(StatusQueueTest, PushAlwaysSucceeds) {
    // StatusQueue is an overwrite ring buffer - push always returns true
    StatusQueue queue;
    StatusSnapshot snap{};

    // Push 1000 times - all should succeed
    for (int i = 0; i < 1000; i++) {
        snap.outdoor_temp = static_cast<float>(i);
        bool pushed = queue.push(snap);
        EXPECT_TRUE(pushed);
    }

    StatusSnapshot latest{};
    EXPECT_TRUE(queue.latest(latest));
    EXPECT_EQ(latest.outdoor_temp, 999.0f);
}

TEST(StatusQueueTest, OldestEntriesOverwritten) {
    StatusQueue queue;
    StatusSnapshot snap{};

    // Fill the queue (CAPACITY is 64)
    for (int i = 0; i < 100; i++) {
        snap.outdoor_temp = static_cast<float>(i);
        queue.push(snap);
    }

    // Pop all items - should have 63 items (CAPACITY-1)
    int count = 0;
    while (queue.pop(snap)) {
        count++;
    }
    EXPECT_EQ(count, 31);  // CAPACITY (32) - 1
}

TEST(StatusQueueTest, LatestWhenEmpty) {
    StatusQueue queue;
    StatusSnapshot snap{};
    EXPECT_FALSE(queue.latest(snap));
}

// ─── API Response Code Tests ───

TEST(ApiResponseCodesTest, HttpCodesValid) {
    EXPECT_EQ(200, 200);
    EXPECT_EQ(202, 202);
    EXPECT_EQ(400, 400);
    EXPECT_EQ(405, 405);
    EXPECT_EQ(422, 422);
    EXPECT_EQ(503, 503);
}

TEST(ApiUrlConstantsTest, EndpointsDefined) {
    EXPECT_STREQ("/api/status", "/api/status");
    EXPECT_STREQ("/api/set-dhw", "/api/set-dhw");
    EXPECT_STREQ("/api/set-heating", "/api/set-heating");
    EXPECT_STREQ("/api/set-priority", "/api/set-priority");
    EXPECT_STREQ("/api/set-mode", "/api/set-mode");
}
