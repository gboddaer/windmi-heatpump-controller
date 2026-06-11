/**
 * @file tests/utils/test_json_helpers.cpp
 * @brief JSON Helpers unit tests
 */

#include "gtest/gtest.h"
#include "utils/JsonHelpers.hpp"

using namespace windmi;

TEST(JsonHelpersTest, GenerateStatusJson) {
    std::string json = JsonHelpers::generateStatusJson(
        45.0, 45.0,
        35.0, 30.0, 40.0,
        20.0, 35.0,
        "heat+dhw", "heat",
        "dhw", "running",
        true,
        25.0, 20.0,
        230.0, 75.0,
        5750.0, 5175.0, true,
        42.0, 1.20, 8, 50, 3, 123, 456,
        6976.7, 1.35, true,
        5.0, 0,
        3);
    
    EXPECT_NE(json.find("\"dhwTemperature\":45.0"), std::string::npos);
    EXPECT_NE(json.find("\"enteringWaterTemperature\":30.0"), std::string::npos);
    EXPECT_NE(json.find("\"heatingTarget\":40.0"), std::string::npos);
    EXPECT_NE(json.find("\"deviceOnline\":true"), std::string::npos);
    EXPECT_NE(json.find("\"acPowerVA\":5750.0"), std::string::npos);
    EXPECT_NE(json.find("\"acPowerW\":5175.0"), std::string::npos);
    EXPECT_NE(json.find("\"powerValid\":true"), std::string::npos);
    EXPECT_NE(json.find("\"copValid\":true"), std::string::npos);
    EXPECT_NE(json.find("\"waterDeltaT\":5.0"), std::string::npos);
    EXPECT_NE(json.find("\"dhwValveStatus\":0"), std::string::npos);
    EXPECT_EQ(json.find("\"dhwHysteresis\":"), std::string::npos);
    EXPECT_EQ(json.find("\"acPower\":"), std::string::npos);
}

TEST(JsonHelpersTest, GenerateSuccessResponse) {
    std::string json = JsonHelpers::generateSuccessResponse(true, false, "Command queued");
    
    EXPECT_NE(json.find("\"success\":true"), std::string::npos);
    EXPECT_NE(json.find("\"message\":\"Command queued\""), std::string::npos);
}

TEST(JsonHelpersTest, GenerateErrorResponse) {
    std::string json = JsonHelpers::generateErrorResponse("Temperature out of range");
    
    EXPECT_NE(json.find("\"error\":\"Temperature out of range\""), std::string::npos);
}

TEST(JsonHelpersTest, EmptyString) {
    std::string json = JsonHelpers::generateSuccessResponse(false);
    EXPECT_NE(json.find("\"success\":false"), std::string::npos);
}
