/**
 * @file tests/core/test_daily_energy.cpp
 * @brief Daily energy accumulator: wall-clock integration, midnight roll, seeding, persistence cadence.
 */

#include "core/DailyEnergy.hpp"

#include "TestClock.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <regex>
#include <string>

namespace {

using windmi::DailyEnergyAccumulator;
using windmi_test::localNoonMs;

constexpr int64_t kDayMs = 86400000;
constexpr double kWhPerWattMs = 1.0 / 3.6e9;

// Local noon, so every window below stays inside one calendar day in any timezone.
const int64_t kBase = localNoonMs(2026, 11, 18);

TEST(DailyEnergyAccumulatorTest, DayKeyForFormatsLocalDate)
{
  const std::string key = DailyEnergyAccumulator::dayKeyFor(kBase);
  EXPECT_TRUE(std::regex_match(key, std::regex(R"(\d{4}-\d{2}-\d{2})"))) << key;
  EXPECT_NE(key, DailyEnergyAccumulator::dayKeyFor(kBase + 30 * kDayMs));
  EXPECT_EQ("", DailyEnergyAccumulator::dayKeyFor(0));
}

TEST(DailyEnergyAccumulatorTest, ObserveIntegratesBetweenFreshSamples)
{
  DailyEnergyAccumulator accumulator;

  accumulator.observe(kBase, 1000.0);
  EXPECT_DOUBLE_EQ(0.0, accumulator.kwhToday()) << "the first sample of a day has nothing to pair with";
  EXPECT_TRUE(accumulator.hasToday());

  accumulator.observe(kBase + 30000, 1000.0);
  EXPECT_NEAR(1000.0 * 30000.0 * kWhPerWattMs, accumulator.kwhToday(), 1e-12);
}

TEST(DailyEnergyAccumulatorTest, ObserveResetsAtDayRoll)
{
  DailyEnergyAccumulator accumulator;
  accumulator.observe(kBase, 1000.0);
  accumulator.observe(kBase + 30000, 1000.0);
  const double dayOne = accumulator.kwhToday();
  ASSERT_GT(dayOne, 0.0);

  accumulator.observe(kBase + kDayMs + 20000, 2000.0);
  EXPECT_DOUBLE_EQ(0.0, accumulator.kwhToday()) << "yesterday's total must not follow into the new day";
  EXPECT_EQ(DailyEnergyAccumulator::dayKeyFor(kBase + kDayMs), accumulator.storedDayKey());
}

TEST(DailyEnergyAccumulatorTest, LongOutageIntegratesNothing)
{
  DailyEnergyAccumulator accumulator;
  accumulator.observe(kBase, 500.0);

  // Ten minutes without a sample: the gap is not evidence that 500 W was drawn throughout.
  accumulator.observe(kBase + 600000, 500.0);
  EXPECT_DOUBLE_EQ(0.0, accumulator.kwhToday());

  // A normal 30-second window afterwards integrates again.
  accumulator.observe(kBase + 630000, 500.0);
  EXPECT_NEAR(500.0 * 30000.0 * kWhPerWattMs, accumulator.kwhToday(), 1e-12);
}

TEST(DailyEnergyAccumulatorTest, SeedOnlyAcceptsTodaysDate)
{
  DailyEnergyAccumulator yesterday;
  yesterday.seed(kBase, 1.5, DailyEnergyAccumulator::dayKeyFor(kBase - kDayMs));
  EXPECT_DOUBLE_EQ(0.0, yesterday.kwhToday()) << "a saved total for another day must not be shown as today's";
  EXPECT_FALSE(yesterday.hasToday());

  DailyEnergyAccumulator today;
  today.seed(kBase, 1.5, DailyEnergyAccumulator::dayKeyFor(kBase));
  EXPECT_DOUBLE_EQ(1.5, today.kwhToday());
  EXPECT_TRUE(today.hasToday());

  today.advance(kBase + kDayMs);
  EXPECT_DOUBLE_EQ(0.0, today.kwhToday()) << "midnight must clear a seeded total too";
  EXPECT_FALSE(today.hasToday());
}

TEST(DailyEnergyAccumulatorTest, PersistDueAndMarkPersisted)
{
  DailyEnergyAccumulator accumulator;
  EXPECT_TRUE(accumulator.persistDue(kBase)) << "nothing saved yet, so a save is owed";

  accumulator.markPersisted(kBase);
  EXPECT_FALSE(accumulator.persistDue(kBase + 3599999));
  EXPECT_TRUE(accumulator.persistDue(kBase + 3600000));
}

} // namespace
