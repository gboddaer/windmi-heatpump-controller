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

// ---- Tank state: the hysteresis the arbiter is built on ----

namespace {

ArbitrationConfig cfg() { return ArbitrationConfig{}; }

TankReading reading(float temp_c, float target_c = 50.0f, int ticks_at_target = 0,
                    bool valid = true) {
    return TankReading{temp_c, target_c, valid, ticks_at_target};
}

} // namespace

TEST(TankStateTest, DeepDropIsCalling) {
    EXPECT_EQ(TankState::Calling,
              advanceTankState(TankState::Satisfied, cfg(), reading(44.9f)));
}

TEST(TankStateTest, ReachingTargetOnceIsOnlyACandidate) {
    EXPECT_EQ(TankState::SatisfiedCandidate,
              advanceTankState(TankState::Calling, cfg(), reading(49.6f, 50.0f, 1)));
}

TEST(TankStateTest, TwoTicksAtTargetSatisfy) {
    EXPECT_EQ(TankState::Satisfied,
              advanceTankState(TankState::SatisfiedCandidate, cfg(), reading(49.6f, 50.0f, 2)));
}

TEST(TankStateTest, InsideTheBandKeepsPreviousState) {
    EXPECT_EQ(TankState::Satisfied,
              advanceTankState(TankState::Satisfied, cfg(), reading(47.0f)));
    EXPECT_EQ(TankState::Calling,
              advanceTankState(TankState::Calling, cfg(), reading(47.0f)));
}

TEST(TankStateTest, KeepsSatisfiedThroughSlowSag) {
    // Stratification sag below the target must not undo satisfaction (spec F9):
    // only crossing the whole band re-arms a DHW episode.
    EXPECT_EQ(TankState::Satisfied,
              advanceTankState(TankState::Satisfied, cfg(), reading(48.4f, 50.0f, 0)));
}

TEST(TankStateTest, UnreadableReadingIsUnknown) {
    EXPECT_EQ(TankState::Unknown,
              advanceTankState(TankState::Satisfied, cfg(), reading(45.0f, 50.0f, 0, false)));
}

TEST(TankStateTest, NonPositiveTemperatureIsUnknown) {
    EXPECT_EQ(TankState::Unknown,
              advanceTankState(TankState::Calling, cfg(), reading(0.0f)));
}

TEST(TankStateTest, UnknownTargetIsUnknown) {
    EXPECT_EQ(TankState::Unknown,
              advanceTankState(TankState::Satisfied, cfg(), reading(44.0f, 0.0f)));
}

TEST(TankStateTest, FloorsClampUndershootingConfig) {
    ArbitrationConfig undershoot{2.0f, 10, 0.1f};
    undershoot.applyFloors();
    EXPECT_FLOAT_EQ(5.0f, undershoot.hysteresis_c);
    EXPECT_EQ(60, undershoot.min_switch_gap_s);
    EXPECT_FLOAT_EQ(0.5f, undershoot.draw_override_c_per_min);
}

TEST(TankStateTest, DefaultsComeFromTheMeasuredUnit) {
    ArbitrationConfig defaults{};
    EXPECT_FLOAT_EQ(5.0f, defaults.hysteresis_c);
    EXPECT_EQ(120, defaults.min_switch_gap_s);
    EXPECT_FLOAT_EQ(1.0f, defaults.draw_override_c_per_min);
}

TEST(TankStateTest, FreshReadingInsideBandCountsAsCalling) {
    // At start-up there is no history to fall back on. Under "domestic hot water
    // has priority", an in-band tank with no history is treated as calling, so a
    // restart cannot leave the tank starved for hours - which is the failure this
    // whole change exists to fix (spec F4).
    EXPECT_EQ(TankState::Calling,
              advanceTankState(TankState::Unknown, cfg(), reading(47.0f)));
}
