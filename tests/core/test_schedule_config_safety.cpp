#include <gtest/gtest.h>
#include "utils/ScheduleConfig.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
using namespace windmi;
namespace {
const char* valid = R"({"enabled":true,"ranges":[{"start":0,"end":1440,"mode":3,"heatingTemp":40,"dhwTemp":50}]})";
struct FileFixture : testing::Test {
    std::filesystem::path directory = std::filesystem::temp_directory_path() / "wpomp-schedule-safety-test";
    void SetUp() override { std::filesystem::remove_all(directory); std::filesystem::create_directories(directory); }
    void TearDown() override { std::filesystem::remove_all(directory); }
};
}
TEST(ScheduleJsonSafetyTest, StrictShapeTypesAndCompleteDocument) {
    ScheduleState state; std::string error;
    ASSERT_TRUE(ScheduleConfig::parseJson(valid, state, error));
    EXPECT_TRUE(state.enabled);
    for (const std::string& bad : std::vector<std::string>{
        "not json", "{}", "[]", std::string(valid) + " garbage",
        R"({"enabled":true,"enabled":false,"ranges":[]})",
        R"({"enabled":false,"ranges":[],})",
        R"({"enabled":false,"ranges":[{},]})",
        R"({"enabled":false,"ranges":[{"start":0,"end":1440}]})",
        R"({"enabled":false,"ranges":[{"start":"0","end":1440,"mode":3}]})",
        R"({"enabled":false,"ranges":[{"start":0.5,"end":1440,"mode":3}]})",
        R"({"enabled":false,"ranges":[{"start":01,"end":1440,"mode":3}]})",
        R"({"enabled":false,"ranges":[{"start":0,"end":1440,"mode":3,"dhwTemp":null}]})",
        R"({"enabled":false,"ranges":[{"start":0,"end":1440,"mode":3,"heatingTemp":1e400}]})",
        R"({"enabled":false,"ranges":[{"start":0,"end":1440,"mode":3,"dhwTemp":39.999999}]})",
        R"({"enabled":false,"ranges":[{"start":0,"end":1440,"mode":3,"heatingTemp":63.000001}]})",
        std::string("{\"enabled\":false,\"ranges\":[],\"paused\":") + std::string(10000,'1') + "}",
        R"({"enabled":false,"ranges":[{"start":0,"end":1440,"mode":3},{"start":10}]})",
        R"({"enabled":true,"ranges":[]})",
        R"({"enabled":false,"ranges":[],"paused":0})"}) {
        EXPECT_FALSE(ScheduleConfig::parseJson(bad, state, error)) << bad;
        EXPECT_FALSE(error.empty());
    }
}
TEST(ScheduleJsonSafetyTest, EmptyDisabledAndLegacySafeDefaultsAreAccepted) {
    ScheduleState state; std::string error;
    EXPECT_TRUE(ScheduleConfig::parseJson(R"({"enabled":false,"ranges":[]})", state, error));
    EXPECT_TRUE(state.ranges.empty());
    EXPECT_TRUE(ScheduleConfig::parseJson(R"({"enabled":1,"ranges":[{"start":0,"end":1440,"mode":3}]})", state, error));
    EXPECT_EQ(state.ranges[0].heatingTemp, 40);
    EXPECT_EQ(state.ranges[0].dhwTemp, 50);
}
TEST(ScheduleJsonSafetyTest, ToggleRequiresOneStrictBooleanOrZeroOneField) {
    bool enabled = false;
    EXPECT_TRUE(ScheduleConfig::parseEnabledJson(R"({"enabled":true})", enabled));
    EXPECT_TRUE(enabled);
    EXPECT_TRUE(ScheduleConfig::parseEnabledJson(R"({"enabled":0})", enabled));
    EXPECT_FALSE(enabled);
    for (const char* bad : {R"({"enabled":2})", R"({"enabled":"true"})",
                           R"({"enabled":true,"enabled":false})", R"({"enabled":true,})",
                           R"({"enabled":true} junk)", R"({})"})
        EXPECT_FALSE(ScheduleConfig::parseEnabledJson(bad, enabled));
}
TEST_F(FileFixture, PausedStateRoundTripsAndRestoresWithoutResuming) {
    ScheduleConfig config;
    ScheduleState state; state.enabled = true; state.paused = true; state.ranges = {{0,1440,3,40,50}};
    ASSERT_TRUE(config.setState(state));
    const auto file = directory / "nested" / "schedule.json";
    ASSERT_TRUE(config.saveToFile(file.string()));
    ScheduleConfig loaded;
    ASSERT_TRUE(loaded.loadFromFile(file.string()));
    EXPECT_TRUE(loaded.isPaused());
    Scheduler scheduler;
    ASSERT_TRUE(scheduler.restore(loaded.state()));
    EXPECT_TRUE(scheduler.isPaused());
    ScheduleRange range;
    EXPECT_FALSE(scheduler.evaluate(500, range));
}
TEST_F(FileFixture, FailedReplacementLeavesPreviousFileAndStateIntact) {
    const auto file = directory / "schedule.json";
    { std::ofstream out(file); out << valid; }
    std::filesystem::create_directory(file.string() + ".tmp");
    ScheduleConfig config;
    ASSERT_TRUE(config.loadFromFile(file.string()));
    config.setEnabled(false);
    EXPECT_FALSE(config.saveToFile(file.string()));
    ScheduleConfig previous;
    ASSERT_TRUE(previous.loadFromFile(file.string()));
    EXPECT_TRUE(previous.isEnabled());
    { std::ofstream out(file); out << "invalid json"; }
    EXPECT_FALSE(previous.loadFromFile(file.string()));
    EXPECT_TRUE(previous.isEnabled());
}
