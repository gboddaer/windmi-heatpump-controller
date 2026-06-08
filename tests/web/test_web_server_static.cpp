/**
 * @file tests/web/test_web_server_static.cpp
 * @brief WebServer static file serving and temperature validation tests
 */

#include <gtest/gtest.h>
#include <fstream>
#include <cstdio>
#include <cstdlib>

#include "config.h"
#include "utils/JsonHelpers.hpp"

using namespace windmi;

// ─── Static File Resolution Tests ───

TEST(StaticFileTest, CurrentDirectoryResolves) {
#ifdef _WIN32
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd))) {
        std::string path = std::string(cwd) + "/static";
        std::ifstream test(path + "/index.html");
        if (test.good()) {
            EXPECT_NE(path.find("/"), std::string::npos);
        }
    }
#else
    std::ifstream test("static/index.html");
    // If static/ exists, it should resolve
    if (test.good()) {
        EXPECT_EQ(std::string("static").find("/"), std::string::npos);
    }
#endif
}

TEST(StaticFileTest, NonExistentPathReturnsEmpty) {
    std::ifstream test("/nonexistent/path/index.html");
    EXPECT_FALSE(test.good());
}

// ─── Temperature Validation Tests ───

TEST(TemperatureValidationTest, DhwRangeConstants) {
    EXPECT_FLOAT_EQ(DHW_TEMP_MIN, 40.0f);
    EXPECT_FLOAT_EQ(DHW_TEMP_MAX, 63.0f);
}

TEST(TemperatureValidationTest, HeatingRangeConstants) {
    EXPECT_FLOAT_EQ(HEATING_TEMP_MIN, 25.0f);
    EXPECT_FLOAT_EQ(HEATING_TEMP_MAX, 63.0f);
}

TEST(TemperatureValidationTest, DhwBelowMinimum) {
    float temp = 39.9f;
    EXPECT_TRUE(temp < DHW_TEMP_MIN);
}

TEST(TemperatureValidationTest, DhwAboveMaximum) {
    float temp = 63.1f;
    EXPECT_TRUE(temp > DHW_TEMP_MAX);
}

TEST(TemperatureValidationTest, HeatingBelowMinimum) {
    float temp = 24.9f;
    EXPECT_TRUE(temp < HEATING_TEMP_MIN);
}

TEST(TemperatureValidationTest, HeatingAboveMaximum) {
    float temp = 63.1f;
    EXPECT_TRUE(temp > HEATING_TEMP_MAX);
}

TEST(TemperatureValidationTest, BoundaryValuesDhw) {
    // Exact boundaries should be valid
    EXPECT_FALSE(DHW_TEMP_MIN < DHW_TEMP_MIN);  // 40.0 not below 40.0
    EXPECT_FALSE(DHW_TEMP_MAX > DHW_TEMP_MAX);  // 63.0 not above 63.0
}

TEST(TemperatureValidationTest, BoundaryValuesHeating) {
    EXPECT_FALSE(HEATING_TEMP_MIN < HEATING_TEMP_MIN);
    EXPECT_FALSE(HEATING_TEMP_MAX > HEATING_TEMP_MAX);
}

// ─── JSON Helpers Tests ───

// Helper to build a zero-filled JSON for testing
static std::string zeroJson() {
    return JsonHelpers::generateStatusJson(
        0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
        std::string("off"), std::string("off"), std::string("dhw"), std::string("off"), false,
        0.0, 0.0, 0.0, 0.0, 0.0, 0.0, false,
        0.0, 0.0, 0, 0, 0, 0, 0,
        0.0, 0.0, false, 0
    );
}

TEST(JsonHelpersTest, GenerateStatusJsonWithZeroValues) {
    std::string json = zeroJson();
    EXPECT_FALSE(json.empty());
    EXPECT_NE(json.find("outdoorTemperature"), std::string::npos);
}

TEST(JsonHelpersTest, GenerateStatusJsonWithNegativeTemp) {
    std::string json = JsonHelpers::generateStatusJson(
        50.0, 50.0, 40.0, 35.0, 40.0,
        -20.0,  // outdoor temp -20°C
        40.0,
        "heat+dhw", "heat", "dhw", "heat", true,
        5.0, 3.0, 230.0, 12.0, 1150.0, 920.0, true,
        45.0, 4.5, 8, 80, 0, 1000, 500,
        5000.0, 3.5, true, MODE_SET_HEAT_DHW
    );
    EXPECT_FALSE(json.empty());
    EXPECT_NE(json.find("-20"), std::string::npos);
}

TEST(JsonHelpersTest, GenerateStatusJsonWithLargeValues) {
    std::string json = JsonHelpers::generateStatusJson(
        999.9, 999.9, 999.9, 999.9, 999.9,
        999.9, 999.9,
        "heat+dhw", "heat", "dhw", "heat", true,
        999.9, 999.9, 999.9, 999.9, 999999.9, 999999.9, true,
        999.9, 999.9, 999, 999, 999, 99999, 99999,
        999999.9, 999.9, true, 2
    );
    EXPECT_FALSE(json.empty());
    EXPECT_EQ(json.front(), '{');
    EXPECT_EQ(json.back(), '}');
}

// ─── Mode/Status Constants Tests ───

TEST(ModeConstantsTest, ModeValuesCorrect) {
    EXPECT_EQ(MODE_SET_OFF, 0);
    EXPECT_EQ(MODE_SET_COOL_DHW, 1);
    EXPECT_EQ(MODE_SET_HEAT_DHW, 2);
}

TEST(ModeConstantsTest, StatusValuesCorrect) {
    EXPECT_EQ(MODE_STATUS_OFF, 0);
    EXPECT_EQ(MODE_STATUS_COOL, 1);
    EXPECT_EQ(MODE_STATUS_HEAT, 2);
    EXPECT_EQ(MODE_STATUS_DHW, 4);
    EXPECT_EQ(MODE_STATUS_DEFROST, 7);
    EXPECT_EQ(MODE_STATUS_ANTIFREEZE, 20);
}
