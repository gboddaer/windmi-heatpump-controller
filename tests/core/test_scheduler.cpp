/**
 * @file tests/core/test_scheduler.cpp
 * @brief Tests for Scheduler and ScheduleConfig
 */

#include <gtest/gtest.h>
#include "core/Scheduler.hpp"
#include "utils/ScheduleConfig.hpp"
#include <fstream>
#include <filesystem>

using namespace windmi;

// ---- Scheduler Tests ----

TEST(SchedulerTest, DefaultStateIsDisabled) {
    Scheduler scheduler;
    EXPECT_FALSE(scheduler.isEnabled());
    EXPECT_FALSE(scheduler.isPaused());
    EXPECT_TRUE(scheduler.getRanges().empty());
}

TEST(SchedulerTest, EnableDisableToggles) {
    Scheduler scheduler;
    scheduler.setEnabled(true);
    EXPECT_TRUE(scheduler.isEnabled());
    scheduler.setEnabled(false);
    EXPECT_FALSE(scheduler.isEnabled());
}

TEST(SchedulerTest, PauseResume) {
    Scheduler scheduler;
    scheduler.setEnabled(true);
    EXPECT_FALSE(scheduler.isPaused());
    scheduler.pause();
    EXPECT_TRUE(scheduler.isPaused());
    scheduler.resume();
    EXPECT_FALSE(scheduler.isPaused());
}

TEST(SchedulerTest, EvaluateDisabledReturnsFalse) {
    Scheduler scheduler;
    ScheduleRange range{};
    EXPECT_FALSE(scheduler.evaluate(600, range));
}

TEST(SchedulerTest, EvaluatePausedReturnsFalse) {
    Scheduler scheduler;
    scheduler.setEnabled(true);
    scheduler.pause();
    ScheduleRange range{};
    EXPECT_FALSE(scheduler.evaluate(600, range));
}

TEST(SchedulerTest, EvaluateInRange) {
    Scheduler scheduler;
    scheduler.setEnabled(true);
    scheduler.setRanges({{60, 600, 3, 40.0f, 45.0f}});
    
    ScheduleRange range{};
    EXPECT_TRUE(scheduler.evaluate(300, range));
    EXPECT_EQ(range.startMinutes, 60);
    EXPECT_EQ(range.endMinutes, 600);
    EXPECT_EQ(range.mode, 3);
}

TEST(SchedulerTest, EvaluateOutOfRange) {
    Scheduler scheduler;
    scheduler.setEnabled(true);
    scheduler.setRanges({{60, 600, 3, 40.0f, 45.0f}});
    
    ScheduleRange range{};
    EXPECT_FALSE(scheduler.evaluate(0, range));
    EXPECT_FALSE(scheduler.evaluate(1440, range));
}

TEST(SchedulerTest, EvaluateAdjacentRanges) {
    Scheduler scheduler;
    scheduler.setEnabled(true);
    scheduler.setRanges({
        {0, 600, 3, 40.0f, 45.0f},
        {600, 900, 2, 35.0f, 42.0f}
    });
    
    ScheduleRange range{};
    EXPECT_TRUE(scheduler.evaluate(400, range));
    EXPECT_EQ(range.startMinutes, 0);
    
    EXPECT_TRUE(scheduler.evaluate(700, range));
    EXPECT_EQ(range.startMinutes, 600);
}

TEST(SchedulerTest, EvaluateWrappedRange) {
    Scheduler scheduler;
    scheduler.setEnabled(true);
    // Range that wraps midnight (e.g., 23:00 to 06:00)
    scheduler.setRanges({{1380, 360, 2, 30.0f, 40.0f}});
    
    ScheduleRange range{};
    EXPECT_TRUE(scheduler.evaluate(0, range));     // Midnight
    EXPECT_TRUE(scheduler.evaluate(300, range));   // 5:00
    EXPECT_TRUE(scheduler.evaluate(1380, range));  // 23:00
    EXPECT_FALSE(scheduler.evaluate(600, range));  // 10:00
}

TEST(SchedulerTest, EvaluateMultipleRanges) {
    Scheduler scheduler;
    scheduler.setEnabled(true);
    scheduler.setRanges({
        {0, 120, 3, 40.0f, 45.0f},       // 00:00-02:00
        {480, 600, 3, 38.0f, 45.0f},     // 08:00-10:00
        {1080, 1200, 3, 42.0f, 45.0f}    // 18:00-20:00
    });
    
    ScheduleRange range{};
    EXPECT_TRUE(scheduler.evaluate(60, range));
    EXPECT_EQ(range.startMinutes, 0);
    
    EXPECT_TRUE(scheduler.evaluate(540, range));
    EXPECT_EQ(range.startMinutes, 480);
    
    EXPECT_TRUE(scheduler.evaluate(1140, range));
    EXPECT_EQ(range.startMinutes, 1080);
    
    EXPECT_FALSE(scheduler.evaluate(240, range));  // Gap between ranges
}

TEST(SchedulerTest, EmptyRangesNeverMatch) {
    Scheduler scheduler;
    scheduler.setEnabled(true);
    scheduler.setRanges({});
    
    ScheduleRange range{};
    EXPECT_FALSE(scheduler.evaluate(720, range));
}

// ---- Range Validation Tests ----

TEST(SchedulerValidationTest, ValidRanges) {
    EXPECT_TRUE(Scheduler::validateRanges({{0, 1440, 3, 40.0f, 45.0f}}));
    EXPECT_TRUE(Scheduler::validateRanges({{0, 600, 3, 40.0f, 45.0f}, {720, 1200, 3, 40.0f, 45.0f}}));
}

TEST(SchedulerValidationTest, OverlappingRangesFail) {
    EXPECT_FALSE(Scheduler::validateRanges({{0, 600, 3, 40.0f, 45.0f}, {300, 900, 3, 40.0f, 45.0f}}));
}

TEST(SchedulerValidationTest, OutOfOrderRangesFail) {
    EXPECT_FALSE(Scheduler::validateRanges({{600, 900, 3, 40.0f, 45.0f}, {0, 300, 3, 40.0f, 45.0f}}));
}

TEST(SchedulerValidationTest, InvalidMinutesFail) {
    EXPECT_FALSE(Scheduler::validateRanges({{0, 1500, 3, 40.0f, 45.0f}}));
    EXPECT_FALSE(Scheduler::validateRanges({{-1, 600, 3, 40.0f, 45.0f}}));
}

TEST(SchedulerValidationTest, IdenticalRangesFail) {
    EXPECT_FALSE(Scheduler::validateRanges({{0, 600, 3, 40.0f, 45.0f}, {0, 600, 3, 40.0f, 45.0f}}));
}

// ---- ScheduleConfig Tests ----

TEST(ScheduleConfigTest, LoadNonExistentFileReturnsFalse) {
    ScheduleConfig config;
    EXPECT_FALSE(config.loadFromFile("/tmp/does_not_exist_schedule.json"));
    EXPECT_FALSE(config.isEnabled());
}

TEST(ScheduleConfigTest, SaveAndLoad) {
    std::string path = "/tmp/test_schedule_config.json";
    
    // Clean up
    std::filesystem::remove(path);
    
    ScheduleConfig config;
    config.setEnabled(true);
    config.setRanges({{0, 600, 3, 40.0f, 45.0f}, {720, 1200, 2, 35.0f, 42.0f}});
    
    EXPECT_TRUE(config.saveToFile(path));
    
    ScheduleConfig loaded;
    EXPECT_TRUE(loaded.loadFromFile(path));
    EXPECT_TRUE(loaded.isEnabled());
    EXPECT_EQ(loaded.getRanges().size(), 2u);
    EXPECT_EQ(loaded.getRanges()[0].startMinutes, 0);
    EXPECT_EQ(loaded.getRanges()[0].endMinutes, 600);
    EXPECT_EQ(loaded.getRanges()[1].startMinutes, 720);
    EXPECT_EQ(loaded.getRanges()[1].mode, 2);
    
    std::filesystem::remove(path);
}

TEST(ScheduleConfigTest, SaveInvalidFileReturnsFalse) {
    ScheduleConfig config;
    EXPECT_FALSE(config.saveToFile("/nonexistent_dir/schedule.json"));
}

TEST(ScheduleConfigTest, LoadInvalidJson) {
    std::string path = "/tmp/test_schedule_invalid.json";
    {
        std::ofstream file(path);
        file << "not valid json {{{";
    }
    
    ScheduleConfig config;
    EXPECT_FALSE(config.loadFromFile(path));
    
    std::filesystem::remove(path);
}

TEST(ScheduleConfigTest, LoadInvalidRanges) {
    std::string path = "/tmp/test_schedule_bad_ranges.json";
    {
        std::ofstream file(path);
        file << "{\"enabled\":true,\"ranges\":[{\"start\":600,\"end\":900,\"mode\":0},{\"start\":0,\"end\":300,\"mode\":0}]}";
    }
    
    ScheduleConfig config;
    EXPECT_FALSE(config.loadFromFile(path));
    
    std::filesystem::remove(path);
}

TEST(ScheduleConfigTest, SetInvalidRanges) {
    ScheduleConfig config;
    EXPECT_FALSE(config.setRanges({{600, 900, 3, 40.0f, 45.0f}, {0, 300, 3, 40.0f, 45.0f}}));
}
