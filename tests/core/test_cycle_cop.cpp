#include <gtest/gtest.h>
#include "core/CycleCop.hpp"

using namespace windmi;
namespace {
CyclePowerSample idle()
{
  return {};
}
CyclePowerSample heating(double input = 1000, double output = 4000)
{
  return {CopService::Heating, true, false, true, input, output};
}
CyclePowerSample dhw()
{
  return {CopService::Dhw, true, false, true, 2000, 5000};
}
} // namespace

TEST(CycleCopTest, IntegratesEnergyRatherThanAveragingInstantaneousCop)
{
  CycleCop c;
  c.update(0, idle());
  c.update(1000, heating());
  c.update(61000, heating(2000, 4000));
  EXPECT_FALSE(c.heating().available);
  c.update(121000, idle());
  ASSERT_TRUE(c.heating().available);
  EXPECT_NEAR(c.heating().cop, 8.0 / 3.0, 1e-6);
  EXPECT_NEAR(c.heating().electric_wh, 50.0, 1e-6);
  EXPECT_NEAR(c.heating().heat_wh, 133.333333, 1e-5);
  EXPECT_FALSE(c.dhw().available);
}

TEST(CycleCopTest, ServiceChangeFinishesOneCycleAndKeepsSeparateResults)
{
  CycleCop c;
  c.update(0, idle());
  c.update(1000, heating());
  c.update(61000, dhw());
  EXPECT_DOUBLE_EQ(c.heating().cop, 4.0);
  EXPECT_FALSE(c.dhw().available);
  c.update(121000, idle());
  EXPECT_DOUBLE_EQ(c.dhw().cop, 2.5);
  EXPECT_DOUBLE_EQ(c.heating().cop, 4.0);
}

TEST(CycleCopTest, DefrostIsPartOfTheRunAndSubtractsHeat)
{
  CycleCop c;
  c.update(0, idle());
  c.update(1000, heating(2000, 6000));
  auto defrost = heating(1000, -2000);
  defrost.protection = true;
  defrost.compressor_running = false; // a protection pause is not cycle completion
  c.update(61000, defrost);
  EXPECT_FALSE(c.heating().available);
  c.update(121000, idle());
  EXPECT_NEAR(c.heating().cop, 4.0 / 3.0, 1e-6);
}

TEST(CycleCopTest, DiscardsPartialRunAtStartup)
{
  CycleCop c;
  c.update(1000, heating());
  c.update(61000, idle());
  EXPECT_FALSE(c.heating().available);
  c.update(62000, heating());
  c.update(122000, idle());
  EXPECT_TRUE(c.heating().available);
}

TEST(CycleCopTest, MissingDataOrLongGapsDoNotCreateAValidCycle)
{
  for (bool gap : {false, true})
  {
    CycleCop c;
    c.update(0, idle());
    c.update(1000, heating());
    auto bad = heating();
    bad.valid = gap;
    c.update(gap ? 121000 : 61000, bad);
    c.update(gap ? 181000 : 121000, idle());
    EXPECT_FALSE(c.heating().available);
  }
}

TEST(CycleCopTest, BadOrVeryShortRunDoesNotOverwriteLastGoodCycle)
{
  CycleCop c;
  c.update(0, idle());
  c.update(1000, heating());
  c.update(61000, idle());
  c.update(62000, heating(2000, 2000));
  c.update(63000, idle());
  EXPECT_DOUBLE_EQ(c.heating().cop, 4.0);
}
