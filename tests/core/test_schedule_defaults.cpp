#include <gtest/gtest.h>
#include "core/Scheduler.hpp"
#include <limits>
using namespace windmi;
TEST(ScheduleDefaultsTest, PeriodOverridesDefaultAndEndRestoresAllDefaultValues)
{
  Scheduler scheduler;
  ASSERT_TRUE(scheduler.replace(true, {{390, 540, 3, 45, 52}}, {1, 35, 48}));
  const auto state = scheduler.snapshot();
  for (int minute : {0, 389, 540, 1439})
  {
    const auto selection = Scheduler::select(state, minute);
    EXPECT_EQ(selection.source, ScheduleSource::Default);
    EXPECT_EQ(selection.settings.mode, 1);
    EXPECT_EQ(selection.settings.heatingTemp, 35);
    EXPECT_EQ(selection.settings.dhwTemp, 48);
  }
  for (int minute : {390, 539})
  {
    const auto selection = Scheduler::select(state, minute);
    EXPECT_EQ(selection.source, ScheduleSource::Period);
    EXPECT_EQ(selection.settings.mode, 3);
    EXPECT_EQ(selection.settings.heatingTemp, 45);
    EXPECT_EQ(selection.settings.dhwTemp, 52);
  }
}
TEST(ScheduleDefaultsTest, EmptyPolicyUsesDefaultAndOffMustBeExplicit)
{
  Scheduler scheduler;
  ASSERT_TRUE(scheduler.replace(true, {}, {1, 35, 50}));
  EXPECT_EQ(Scheduler::select(scheduler.snapshot(), 600).settings.mode, 1);
  EXPECT_EQ(Scheduler::select(scheduler.snapshot(), 600).source, ScheduleSource::Default);
  ASSERT_TRUE(scheduler.replace(true, {}, {0, 35, 50}));
  EXPECT_EQ(Scheduler::select(scheduler.snapshot(), 600).settings.mode, 0);
}
TEST(ScheduleDefaultsTest, PausedAndDisabledNeverApplyADefault)
{
  Scheduler scheduler;
  scheduler.replace(true, {{390, 540, 3, 45, 52}}, {1, 35, 48});
  scheduler.pause();
  EXPECT_EQ(Scheduler::select(scheduler.snapshot(), 400).source, ScheduleSource::Manual);
  EXPECT_EQ(Scheduler::select(scheduler.snapshot(), 600).source, ScheduleSource::Manual);
  scheduler.replace(true, {}, {2, 40, 50});
  EXPECT_TRUE(scheduler.isPaused());
  EXPECT_EQ(Scheduler::select(scheduler.snapshot(), 600).source, ScheduleSource::Manual);
  scheduler.resume();
  EXPECT_EQ(Scheduler::select(scheduler.snapshot(), 600).source, ScheduleSource::Default);
  scheduler.setEnabled(false);
  EXPECT_EQ(Scheduler::select(scheduler.snapshot(), 600).source, ScheduleSource::Manual);
}
TEST(ScheduleDefaultsTest, InvalidDefaultsRejectTheEntireTransactionBeforePersistence)
{
  Scheduler scheduler;
  scheduler.replace(true, {}, {1, 35, 48});
  const auto before = scheduler.snapshot();
  int saves = 0;
  scheduler.setPersistenceHandler([&](const ScheduleState&) {
    ++saves;
    return true;
  });
  for (ScheduleDefaults bad : {ScheduleDefaults{7, 35, 48},
                               {1, 24.9f, 48},
                               {1, 35, 63.1f},
                               {1, std::numeric_limits<float>::quiet_NaN(), 48}})
  {
    EXPECT_FALSE(scheduler.replace(false, {{0, 60, 3, 45, 50}}, bad));
  }
  EXPECT_EQ(saves, 0);
  EXPECT_EQ(scheduler.snapshot().revision, before.revision);
  EXPECT_EQ(scheduler.snapshot().defaults.mode, 1);
  EXPECT_TRUE(scheduler.isEnabled());
}
TEST(ScheduleDefaultsTest, LegacyRangeEditsPreserveIndependentDefaultsAndFailedSaveRetainsThem)
{
  Scheduler scheduler;
  scheduler.replace(true, {}, {1, 35, 48});
  scheduler.replace(true, {{390, 540, 3, 45, 52}}); // old client omits defaults
  EXPECT_EQ(scheduler.snapshot().defaults.mode, 1);
  EXPECT_EQ(scheduler.snapshot().defaults.dhwTemp, 48);
  scheduler.setPersistenceHandler([](const ScheduleState&) { return false; });
  EXPECT_FALSE(scheduler.replace(true, {}, {2, 40, 50}));
  EXPECT_EQ(scheduler.snapshot().defaults.mode, 1);
  EXPECT_EQ(scheduler.snapshot().ranges.size(), 1u);
}
