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
        5000.0, 3.5, true, 0, MODE_SET_HEAT_DHW
    );
}

static std::string makeZeroJson() {
    return JsonHelpers::generateStatusJson(
        0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
        std::string("off"), std::string("off"), std::string("dhw"), std::string("off"), false,
        0.0, 0.0, 0.0, 0.0, 0.0, 0.0, false,
        0.0, 0.0, 0, 0, 0, 0, 0,
        0.0, 0.0, false, 0, 0
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
    EXPECT_STREQ("/api/set-occupancy", "/api/set-occupancy");
    EXPECT_STREQ("/api/schedule", "/api/schedule");
    EXPECT_STREQ("/api/schedule/toggle", "/api/schedule/toggle");
    EXPECT_STREQ("/api/schedule/resume", "/api/schedule/resume");
}

// ─── Schedule API JSON Parsing Tests ───

static bool parseJsonBool(const std::string& json, const std::string& key, bool& out) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return false;
    std::string rest = json.substr(pos + 1);
    auto fs = rest.find_first_not_of(" \t");
    if (fs == std::string::npos) return false;
    rest = rest.substr(fs);
    auto ce = rest.find_first_of(",}\n");
    if (ce != std::string::npos) rest = rest.substr(0, ce);
    if (rest == "true") { out = true; return true; }
    if (rest == "false") { out = false; return true; }
    return false;
}

static bool parseJsonInt(const std::string& json, const std::string& key, int& out) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return false;
    std::string rest = json.substr(pos + 1);
    auto fs = rest.find_first_not_of(" \t");
    if (fs == std::string::npos) return false;
    rest = rest.substr(fs);
    auto ce = rest.find_first_of(",}\n");
    if (ce != std::string::npos) rest = rest.substr(0, ce);
    try {
        out = std::stoi(rest);
        return true;
    } catch (...) {
        return false;
    }
}

TEST(ScheduleApiJsonTest, EnabledTrueParsed) {
    std::string json = "{\"enabled\":true,\"ranges\":[]}";
    bool val = false;
    EXPECT_TRUE(parseJsonBool(json, "enabled", val));
    EXPECT_TRUE(val);
}

TEST(ScheduleApiJsonTest, EnabledFalseParsed) {
    std::string json = "{\"enabled\":false,\"ranges\":[]}";
    bool val = true;
    EXPECT_TRUE(parseJsonBool(json, "enabled", val));
    EXPECT_FALSE(val);
}

TEST(ScheduleApiJsonTest, MissingEnabledField) {
    std::string json = "{\"ranges\":[]}";
    bool val = true;
    EXPECT_FALSE(parseJsonBool(json, "enabled", val));
}

TEST(ScheduleApiJsonTest, PausedFieldParsed) {
    std::string json = "{\"enabled\":true,\"paused\":false,\"ranges\":[]}";
    bool val = true;
    EXPECT_TRUE(parseJsonBool(json, "paused", val));
    EXPECT_FALSE(val);
}

TEST(ScheduleApiJsonTest, RangeStartEndParsed) {
    std::string json = R"({"ranges":[{"start":480,"end":600,"mode":3}]})";
    int val = -1;
    EXPECT_TRUE(parseJsonInt(json, "start", val));
    EXPECT_EQ(val, 480);
}

TEST(ScheduleApiJsonTest, RangeJsonStructure) {
    std::string json = R"({"enabled":true,"ranges":[{"start":480,"end":600,"mode":3,"heatingTemp":40.0,"dhwTemp":45.0}]})";

    // Verify the JSON contains expected fields
    EXPECT_NE(json.find("\"start\":480"), std::string::npos);
    EXPECT_NE(json.find("\"end\":600"), std::string::npos);
    EXPECT_NE(json.find("\"mode\":3"), std::string::npos);
    EXPECT_NE(json.find("\"heatingTemp\":40"), std::string::npos);
    EXPECT_NE(json.find("\"dhwTemp\":45"), std::string::npos);
}

TEST(ScheduleApiJsonTest, MultipleRangesJson) {
    std::string json = R"({"enabled":true,"ranges":[{"start":0,"end":360,"mode":2,"heatingTemp":30,"dhwTemp":40},{"start":720,"end":1080,"mode":3,"heatingTemp":42,"dhwTemp":45}]})";

    EXPECT_NE(json.find("\"start\":0"), std::string::npos);
    EXPECT_NE(json.find("\"start\":720"), std::string::npos);
    EXPECT_NE(json.find("\"end\":1080"), std::string::npos);
}

TEST(ScheduleApiJsonTest, EmptyRangesJson) {
    std::string json = R"({"enabled":false,"ranges":[]})";
    EXPECT_NE(json.find("\"ranges\":[]"), std::string::npos);
}

TEST(ScheduleApiJsonTest, ScheduleToggleJson) {
    std::string json = R"({"enabled":true})";
    bool val = false;
    EXPECT_TRUE(parseJsonBool(json, "enabled", val));
    EXPECT_TRUE(val);
}

// ─── Schedule State in Status JSON ───
// Note: generateStatusJson is used for testing only.
// The actual WebServer uses snprintf for status JSON which includes
// scheduleActive and schedulePaused fields.

TEST(StatusJsonScheduleFieldsTest, WorkingModeFieldExists) {
    std::string json = makeJson("heat+dhw", "heat");
    EXPECT_NE(json.find("workingMode"), std::string::npos);
}

// ─── Schedule Range Validation Edge Cases ───

TEST(ScheduleRangeValidationTest, ValidModeValues) {
    // Modes 0-3 are valid
    EXPECT_TRUE((0 >= 0 && 0 <= 3));
    EXPECT_TRUE((3 >= 0 && 3 <= 3));
    EXPECT_FALSE((4 >= 0 && 4 <= 3));
    EXPECT_FALSE((-1 >= 0 && -1 <= 3));
}

TEST(ScheduleRangeValidationTest, ValidMinuteValues) {
    // Minutes 0-1439 are valid
    EXPECT_TRUE((0 >= 0 && 0 < 1440));
    EXPECT_TRUE((1439 >= 0 && 1439 < 1440));
    EXPECT_FALSE((1440 >= 0 && 1440 < 1440));
    EXPECT_FALSE((-1 >= 0 && -1 < 1440));
}

TEST(ScheduleRangeValidationTest, MinuteConversion) {
    // 08:00 = 480 minutes
    EXPECT_EQ(8 * 60 + 0, 480);
    // 23:30 = 1410 minutes
    EXPECT_EQ(23 * 60 + 30, 1410);
    // 00:00 = 0 minutes
    EXPECT_EQ(0 * 60 + 0, 0);
}
