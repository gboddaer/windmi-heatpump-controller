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

// ---- The device-mode decision ----

namespace {

ArbiterInput input(int working_mode, TankState tank, int current_mode,
                   int seconds_since_last_write = 3600, bool tank_drawing = false,
                   bool blocked = false) {
    ArbiterInput in{};
    in.working_mode = working_mode;
    in.tank = tank;
    in.current_mode = current_mode;
    in.tank_drawing = tank_drawing;
    in.blocked = blocked;
    in.seconds_since_last_write = seconds_since_last_write;
    return in;
}

} // namespace

TEST(ArbitrationDecisionTest, StaticRowsIgnoreTankState) {
    const ArbitrationConfig c = cfg();
    for (TankState tank : {TankState::Calling, TankState::Satisfied, TankState::Unknown}) {
        EXPECT_EQ(MODE_SET_DHW_ONLY, decideDeviceMode(c, input(1, tank, MODE_SET_HEAT_DHW)));
        EXPECT_EQ(MODE_SET_HEAT_ONLY, decideDeviceMode(c, input(2, tank, MODE_SET_HEAT_DHW)));
    }
}

TEST(ArbitrationDecisionTest, WorkingModeOffMapsToOff) {
    EXPECT_EQ(MODE_SET_OFF, decideDeviceMode(cfg(), input(0, TankState::Calling, MODE_SET_HEAT_DHW)));
}

TEST(ArbitrationDecisionTest, BothOnSatisfiedTankUsesHeatingMode) {
    EXPECT_EQ(MODE_SET_HEAT_DHW,
              decideDeviceMode(cfg(), input(3, TankState::Satisfied, MODE_SET_DHW_ONLY)));
}

TEST(ArbitrationDecisionTest, BothOnCallingTankUsesDhwMode) {
    EXPECT_EQ(MODE_SET_DHW_ONLY,
              decideDeviceMode(cfg(), input(3, TankState::Calling, MODE_SET_HEAT_DHW)));
}

TEST(ArbitrationDecisionTest, BothOnKeepsCurrentModeWhileCandidate) {
    EXPECT_EQ(MODE_SET_DHW_ONLY,
              decideDeviceMode(cfg(), input(3, TankState::SatisfiedCandidate, MODE_SET_DHW_ONLY)));
    EXPECT_EQ(MODE_SET_HEAT_DHW,
              decideDeviceMode(cfg(), input(3, TankState::SatisfiedCandidate, MODE_SET_HEAT_DHW)));
}

TEST(ArbitrationDecisionTest, SwitchGapBlocksGrant) {
    EXPECT_EQ(MODE_SET_HEAT_DHW,
              decideDeviceMode(cfg(), input(3, TankState::Calling, MODE_SET_HEAT_DHW, 60)));
}

TEST(ArbitrationDecisionTest, SwitchGapDoesNotBlockRelease) {
    EXPECT_EQ(MODE_SET_HEAT_DHW,
              decideDeviceMode(cfg(), input(3, TankState::Satisfied, MODE_SET_DHW_ONLY, 0)))
        << "giving the compressor back to the house is never worth delaying";
}

TEST(ArbitrationDecisionTest, DrawOverridesSwitchGap) {
    EXPECT_EQ(MODE_SET_DHW_ONLY,
              decideDeviceMode(cfg(), input(3, TankState::Calling, MODE_SET_HEAT_DHW, 0, true)))
        << "somebody is at the tap: comfort wins over the debounce";
}

TEST(ArbitrationDecisionTest, BlockedHoldsCurrentMode) {
    EXPECT_EQ(MODE_SET_HEAT_DHW,
              decideDeviceMode(cfg(), input(3, TankState::Calling, MODE_SET_HEAT_DHW, 3600, false, true)));
    EXPECT_EQ(MODE_SET_DHW_ONLY,
              decideDeviceMode(cfg(), input(3, TankState::Satisfied, MODE_SET_DHW_ONLY, 3600, false, true)));
    EXPECT_EQ(MODE_SET_HEAT_DHW,
              decideDeviceMode(cfg(), input(1, TankState::Calling, MODE_SET_HEAT_DHW, 3600, false, true)))
        << "no writes during defrost or anti-freeze; the decision is deferred, not lost";
}

TEST(ArbitrationDecisionTest, UnknownTankHoldsMode) {
    EXPECT_EQ(MODE_SET_DHW_ONLY,
              decideDeviceMode(cfg(), input(3, TankState::Unknown, MODE_SET_DHW_ONLY)));
    EXPECT_EQ(MODE_SET_HEAT_DHW,
              decideDeviceMode(cfg(), input(3, TankState::Unknown, MODE_SET_HEAT_DHW)))
        << "an unreadable tank probe must never be read as freezing and start a charge";
}

// ---- Draw detection: rate, not direction ----

TEST(TankDrawRateTest, FastFallIsADraw) {
    // Measured 2026-11-11: a bath pulled the top probe 41.6 -> 27.2 C in 4.5 min.
    EXPECT_TRUE(tankIsDrawing(41.6f, 27.2f, 270.0, 1.0f));
}

TEST(TankDrawRateTest, SlowSagIsNotADraw) {
    // Stratification relaxing after a release: a couple of degrees over ten minutes.
    EXPECT_FALSE(tankIsDrawing(49.8f, 48.4f, 600.0, 1.0f));
}

TEST(TankDrawRateTest, AFirstSampleHasNoRate) {
    EXPECT_FALSE(tankIsDrawing(-1.0f, 40.0f, 0.0, 1.0f));
}

TEST(TankDrawRateTest, TooShortAnIntervalIsNotADraw) {
    EXPECT_FALSE(tankIsDrawing(49.0f, 48.0f, 2.0, 1.0f));
}

TEST(TankDrawRateTest, RisingIsNeverADraw) {
    EXPECT_FALSE(tankIsDrawing(20.0f, 25.0f, 60.0, 1.0f));
}
