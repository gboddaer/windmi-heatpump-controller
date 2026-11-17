#include <gtest/gtest.h>
#include "core/Scheduler.hpp"
#include <atomic>
#include <limits>
#include <thread>

using namespace windmi;
namespace {
std::vector<ScheduleRange> day(float heat = 40) { return {{0, 1440, 3, heat, 50}}; }
}
TEST(ScheduleSafetyTest, OvernightOverlapIsRejectedButAdjacentWrappingIsValid) {
    EXPECT_FALSE(Scheduler::validateRanges({{0,420,3,40,50},{1380,360,1,40,50}}));
    EXPECT_FALSE(Scheduler::validateRanges({{60,180,1,40,50},{1380,120,3,40,50}}));
    EXPECT_TRUE(Scheduler::validateRanges({{360,1380,3,40,50},{1380,360,1,40,50}}));
    EXPECT_TRUE(Scheduler::validateRanges({{0,360,3,40,50},{1380,0,1,40,50}}));
}
TEST(ScheduleSafetyTest, RejectsUnsafeAndNonfiniteTemperaturesInEveryMode) {
    for (int mode = 0; mode <= 3; ++mode) {
        for (float heat : {24.9f, 63.1f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
            EXPECT_FALSE(Scheduler::validateRanges({{0,1440,mode,heat,50}}));
        for (float dhw : {39.9f, 63.1f, -200.f, std::numeric_limits<float>::quiet_NaN()})
            EXPECT_FALSE(Scheduler::validateRanges({{0,1440,mode,40,dhw}}));
    }
    EXPECT_TRUE(Scheduler::validateRanges({{0,1440,3,25,40}}));
    EXPECT_TRUE(Scheduler::validateRanges({{0,1440,3,63,63}}));
}
TEST(ScheduleSafetyTest, EmptyScheduleCanRunItsValidatedDefault) {
    Scheduler s;
    EXPECT_TRUE(Scheduler::validateRanges({}));
    EXPECT_TRUE(s.replace(false, {}, {1,35,50}));
    EXPECT_TRUE(s.setEnabled(true));
    EXPECT_TRUE(s.replace(true, {}));
    EXPECT_TRUE(s.isEnabled());
    EXPECT_EQ(Scheduler::select(s.snapshot(),600).settings.mode,1);
    EXPECT_TRUE(s.replace(true, day()));
    EXPECT_TRUE(s.replace(false, {}));
    EXPECT_TRUE(s.getRanges().empty());
}
TEST(ScheduleSafetyTest, SnapshotIsACopyAndFailedCommitDoesNotPublishAnything) {
    Scheduler s;
    ASSERT_TRUE(s.replace(true, day()));
    auto copy = s.snapshot();
    copy.ranges.clear();
    EXPECT_EQ(s.getRanges().size(), 1u);
    const auto before = s.snapshot();
    int saves = 0;
    s.setPersistenceHandler([&](const ScheduleState& candidate) {
        ++saves; EXPECT_FALSE(candidate.enabled); EXPECT_EQ(candidate.ranges[0].heatingTemp, 45); return false;
    });
    EXPECT_FALSE(s.replace(false, day(45)));
    EXPECT_EQ(saves, 1);
    const auto after = s.snapshot();
    EXPECT_EQ(after.revision, before.revision);
    EXPECT_TRUE(after.enabled);
    EXPECT_EQ(after.ranges[0].heatingTemp, 40);
}
TEST(ScheduleSafetyTest, InvalidUpdateDoesNotCallPersistenceOrChangeEnabled) {
    Scheduler s;
    s.setRanges(day());
    int saves = 0;
    s.setPersistenceHandler([&](const ScheduleState&) { ++saves; return true; });
    EXPECT_FALSE(s.replace(true, {{0,0,3,40,50}}));
    EXPECT_FALSE(s.isEnabled());
    EXPECT_EQ(saves, 0);
}
TEST(ScheduleSafetyTest, PauseSurvivesEditsAndDisableEnableUntilExplicitResume) {
    Scheduler s;
    s.replace(true, day());
    ScheduleState saved;
    s.setPersistenceHandler([&](const ScheduleState& candidate) { saved = candidate; return true; });
    ASSERT_TRUE(s.pause());
    EXPECT_TRUE(saved.paused);
    const auto pausedRevision = s.snapshot().revision;
    s.replace(true, day(45));
    s.setEnabled(false);
    s.setEnabled(true);
    EXPECT_TRUE(s.isPaused());
    EXPECT_TRUE(saved.paused);
    EXPECT_GT(s.snapshot().revision, pausedRevision);
    ASSERT_TRUE(s.resume());
    EXPECT_FALSE(s.isPaused());
    EXPECT_FALSE(saved.paused);
}
TEST(ScheduleSafetyTest, FailedManualPauseSaveStillStopsAutomationButFailedResumeDoesNotResume) {
    Scheduler s;
    s.replace(true, day());
    s.setPersistenceHandler([](const ScheduleState&) { return false; });
    EXPECT_FALSE(s.pause());
    EXPECT_TRUE(s.isPaused());
    EXPECT_TRUE(s.snapshot().persistenceError);
    EXPECT_FALSE(s.resume());
    EXPECT_TRUE(s.isPaused());
    ScheduleRange range;
    EXPECT_FALSE(s.evaluate(600, range));
}
TEST(ScheduleSafetyTest, RuntimeIdentityIsNotRestoredFromAnOldProcess) {
    Scheduler first, second;
    const auto identity = second.snapshot().instanceId;
    EXPECT_FALSE(identity.empty());
    EXPECT_NE(identity, first.snapshot().instanceId);
    ASSERT_TRUE(second.restore(first.snapshot()));
    EXPECT_EQ(second.snapshot().instanceId, identity);
}
TEST(ScheduleSafetyTest, ConcurrentReadersGetCoherentOwnedSnapshots) {
    Scheduler s;
    s.replace(true, day());
    std::atomic<bool> valid{true};
    std::thread writer([&] {
        for (int i = 0; i < 1000; ++i) if (!s.replace(true, day(i % 2 ? 40 : 41))) valid = false;
    });
    for (int i = 0; i < 1000; ++i) {
        auto state = s.snapshot();
        if (!state.enabled || state.ranges.size() != 1 ||
            (state.ranges[0].heatingTemp != 40 && state.ranges[0].heatingTemp != 41)) valid = false;
    }
    writer.join();
    EXPECT_TRUE(valid.load());
}
