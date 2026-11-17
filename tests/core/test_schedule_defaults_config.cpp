#include <gtest/gtest.h>
#include "utils/ScheduleConfig.hpp"
#include <filesystem>
#include <fstream>
using namespace windmi;
TEST(ScheduleDefaultsConfigTest, DefaultsRoundTripSeparatelyFromPeriodsAndPause) {
    const std::string json = R"({"enabled":true,"paused":true,"defaults":{"mode":1,"heatingTemp":35,"dhwTemp":48},"ranges":[{"start":390,"end":540,"mode":3,"heatingTemp":45,"dhwTemp":52}]})";
    ScheduleState state; std::string error; bool explicitDefaults = false;
    ASSERT_TRUE(ScheduleConfig::parseJson(json,state,error,&explicitDefaults));
    EXPECT_TRUE(explicitDefaults); EXPECT_TRUE(state.paused);
    EXPECT_EQ(state.defaults.mode,1); EXPECT_EQ(state.defaults.dhwTemp,48);
    ScheduleState again;
    ASSERT_TRUE(ScheduleConfig::parseJson(ScheduleConfig::toJson(state),again,error));
    EXPECT_EQ(again.defaults.mode,1); EXPECT_EQ(again.ranges[0].mode,3);
}
TEST(ScheduleDefaultsConfigTest, ExplicitDefaultIsCompleteStrictAndBounded) {
    for (const std::string& object : std::vector<std::string>{
        R"({"mode":1})", R"({"mode":"1","heatingTemp":35,"dhwTemp":50})",
        R"({"mode":9,"heatingTemp":35,"dhwTemp":50})", R"({"mode":1,"heatingTemp":24,"dhwTemp":50})",
        R"({"mode":1,"heatingTemp":35,"dhwTemp":39.999999})",
        R"({"mode":1,"mode":3,"heatingTemp":35,"dhwTemp":50})", "null"}) {
        ScheduleState state; state.defaults={1,35,48}; std::string error;
        EXPECT_FALSE(ScheduleConfig::parseJson("{\"enabled\":true,\"ranges\":[],\"defaults\":"+object+"}",state,error)) << object;
        EXPECT_EQ(state.defaults.dhwTemp,48);
    }
    ScheduleState state; std::string error;
    EXPECT_TRUE(ScheduleConfig::parseJson(R"({"enabled":true,"ranges":[],"defaults":{"mode":1,"heatingTemp":35,"dhwTemp":50}})",state,error));
}
TEST(ScheduleDefaultsConfigTest, LegacyFileUsesStartupUiValuesWithoutRewritingItOrClearingPause) {
    const auto path = std::filesystem::temp_directory_path()/"wpomp-defaults-legacy.json";
    const std::string legacy=R"({"enabled":true,"paused":true,"ranges":[{"start":390,"end":540,"mode":3,"heatingTemp":45,"dhwTemp":50}]})";
    {std::ofstream file(path);file<<legacy;}
    ScheduleConfig config; ASSERT_TRUE(config.loadFromFile(path.string()));
    EXPECT_FALSE(config.hasDefaults());
    auto state = config.stateWithLegacyDefaults({1,38,52});
    EXPECT_EQ(state.defaults.mode,1); EXPECT_EQ(state.defaults.heatingTemp,38); EXPECT_EQ(state.defaults.dhwTemp,52);
    EXPECT_TRUE(state.paused); EXPECT_EQ(state.ranges[0].mode,3);
    {std::ifstream file(path);std::string contents((std::istreambuf_iterator<char>(file)),{});EXPECT_EQ(contents,legacy);}
    ScheduleConfig saved; ASSERT_TRUE(saved.setState(state)); ASSERT_TRUE(saved.saveToFile(path.string()));
    ScheduleConfig restored; ASSERT_TRUE(restored.loadFromFile(path.string()));
    EXPECT_TRUE(restored.hasDefaults()); EXPECT_EQ(restored.state().defaults.mode,1); EXPECT_TRUE(restored.isPaused());
    EXPECT_EQ(restored.stateWithLegacyDefaults({3,45,50}).defaults.mode,1);
    std::filesystem::remove(path);
}
