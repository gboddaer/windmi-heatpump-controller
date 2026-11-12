/**
 * @file tests/core/test_arbitration.cpp
 * @brief DHW demand arbiter tests (pure decision functions, no I/O)
 */

#include <gtest/gtest.h>
#include "core/Arbitration.hpp"
#include "config.h"

using namespace windmi;

TEST(ArbitrationMappingTest, WorkingModesMapToNativeDeviceModes) {
    EXPECT_EQ(MODE_SET_OFF, deviceModeForWorkingMode(0));
    EXPECT_EQ(MODE_SET_DHW_ONLY, deviceModeForWorkingMode(1));
    EXPECT_EQ(MODE_SET_HEAT_ONLY, deviceModeForWorkingMode(2));
    EXPECT_EQ(MODE_SET_HEAT_DHW, deviceModeForWorkingMode(3));
}

TEST(ArbitrationMappingTest, UnknownWorkingModeFallsBackToHeatDhw) {
    EXPECT_EQ(MODE_SET_HEAT_DHW, deviceModeForWorkingMode(7));
    EXPECT_EQ(MODE_SET_HEAT_DHW, deviceModeForWorkingMode(-1));
}
