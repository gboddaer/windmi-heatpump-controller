/**
 * @file tests/core/test_mode_names.cpp
 * @brief Device-mode name tests, including the native GCHV modes 3/4/5
 */

#include <gtest/gtest.h>
#include "core/ModeNames.hpp"
#include "config.h"

using namespace windmi;

TEST(ModeNamesTest, NativeGchwModesHaveNames)
{
  EXPECT_STREQ("dhw_only", mode_to_string(MODE_SET_DHW_ONLY));
  EXPECT_STREQ("heat", mode_to_string(MODE_SET_HEAT_ONLY));
  EXPECT_STREQ("cool", mode_to_string(MODE_SET_COOL_ONLY));
}

TEST(ModeNamesTest, ExistingNamesUnchanged)
{
  EXPECT_STREQ("off", mode_to_string(MODE_SET_OFF));
  EXPECT_STREQ("cool+dhw", mode_to_string(MODE_SET_COOL_DHW));
  EXPECT_STREQ("heat+dhw", mode_to_string(MODE_SET_HEAT_DHW));
  EXPECT_STREQ("unknown", mode_to_string(9));
}

TEST(ModeNamesTest, NativeModeValuesMatchTheVendorTable)
{
  // Measured on the unit 2026-11-11: the display controller writes 5 for
  // DHW-only and passes through 4 on the way (spec F2).
  EXPECT_EQ(3, MODE_SET_COOL_ONLY);
  EXPECT_EQ(4, MODE_SET_HEAT_ONLY);
  EXPECT_EQ(5, MODE_SET_DHW_ONLY);
}
