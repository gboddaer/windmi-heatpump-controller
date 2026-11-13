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

// ---- Effective target: the setpoint the unit will actually charge to ----

TEST(EffectiveTargetTest, ASingleSetpointIsItsOwnTarget) {
    EXPECT_FLOAT_EQ(50.0f, effectiveDhwTargetC(50.0f, 0.0f));
}

TEST(EffectiveTargetTest, AHigherEconomicSetpointIsTheLiveTarget) {
    // Measured 2026-11-13: with the unit in its economic DHW sub-mode, the charge
    // stopped at 0x0196 (45.0) while 0x0194 said 50.0; raising 0x0196 to 52.0
    // started the compressor within two minutes.
    EXPECT_FLOAT_EQ(52.0f, effectiveDhwTargetC(50.0f, 52.0f));
}

TEST(EffectiveTargetTest, ALowerEconomicSetpointDoesNotLowerTheDemand) {
    EXPECT_FLOAT_EQ(50.0f, effectiveDhwTargetC(50.0f, 45.0f));
}

TEST(EffectiveTargetTest, AnUnreadSetpointIsSimplyAbsent) {
    EXPECT_FLOAT_EQ(48.0f, effectiveDhwTargetC(0.0f, 48.0f));
    EXPECT_FLOAT_EQ(0.0f, effectiveDhwTargetC(0.0f, 0.0f))
        << "no target at all is not a target of zero";
}

// ---- Park rule: a grant the unit does not act on is not a demand being served ----

TEST(ParkRuleTest, GrantedAndIdlePastTheTimeoutIsParked) {
    ArbitrationConfig c = cfg();
    c.park_timeout_s = 300;   // stated, so the test does not drift with the default
    EXPECT_TRUE(chargeIsParked(c, true, false, false, 301));
}

TEST(ParkRuleTest, IdleShorterThanTheTimeoutIsNotParked) {
    ArbitrationConfig c = cfg();
    c.park_timeout_s = 300;
    EXPECT_FALSE(chargeIsParked(c, true, false, false, 299));
}

TEST(ParkRuleTest, AnActuatingUnitIsNeverParked) {
    EXPECT_FALSE(chargeIsParked(cfg(), true, true, false, 3600));
}

TEST(ParkRuleTest, AFallingTankIsNotParked) {
    EXPECT_FALSE(chargeIsParked(cfg(), true, false, true, 3600))
        << "somebody is at the tap: the answer is to charge, not to give up";
}

TEST(ParkRuleTest, NoGrantNoOpinion) {
    EXPECT_FALSE(chargeIsParked(cfg(), false, false, false, 3600))
        << "the heating loop idling is normal, not a declined charge";
}

TEST(ParkRuleTest, ParkTimeoutFloor) {
    ArbitrationConfig tight;
    tight.park_timeout_s = 30;
    tight.applyFloors();
    EXPECT_EQ(180, tight.park_timeout_s)
        << "shorter than three minutes punishes every defrost and anti-cycle pause";
}

TEST(ParkRuleTest, ADeclinedChargeReleasesTheCompressorToTheHouse) {
    ArbitrationConfig c = cfg();
    c.park_timeout_s = 300;
    ArbiterInput in = input(3, TankState::Calling, MODE_SET_DHW_ONLY);
    in.dhw_granted = true;
    in.unit_actuating = false;
    in.seconds_since_actuating = 400;
    EXPECT_EQ(MODE_SET_HEAT_DHW, decideDeviceMode(c, in))
        << "the tank is parked with the unit idle: the house gets the compressor";
}

TEST(ParkRuleTest, ParkingDoesNotPreemptDefrost) {
    ArbiterInput in = input(3, TankState::Calling, MODE_SET_DHW_ONLY);
    in.dhw_granted = true;
    in.unit_actuating = false;
    in.seconds_since_actuating = 400;
    in.blocked = true;
    EXPECT_EQ(MODE_SET_DHW_ONLY, decideDeviceMode(cfg(), in));
}

TEST(ParkRuleTest, SingleDemandRowsAreNotArbitrated) {
    ArbiterInput in = input(1, TankState::Calling, MODE_SET_DHW_ONLY);
    in.dhw_granted = true;
    in.unit_actuating = false;
    in.seconds_since_actuating = 400;
    EXPECT_EQ(MODE_SET_DHW_ONLY, decideDeviceMode(cfg(), in))
        << "DHW-only asked for by the user stays DHW-only even when the unit is idle";
}

// ---- Charge watchdog: compressor minutes, not wall clock ----

TEST(ChargeWatchdogTest, StalledChargeWhileRunningIsFlagged) {
    // 121 minutes of compressor time for DHW and the tank moved four tenths.
    EXPECT_TRUE(chargeIsStalled(cfg(), 121 * 60, 30.0f, 30.4f));
}

TEST(ChargeWatchdogTest, FallingTankWhileRunningIsFlagged) {
    EXPECT_TRUE(chargeIsStalled(cfg(), 121 * 60, 50.0f, 48.0f));
}

TEST(ChargeWatchdogTest, ARealChargeIsNotFlagged) {
    EXPECT_FALSE(chargeIsStalled(cfg(), 121 * 60, 15.0f, 50.0f));
}

TEST(ChargeWatchdogTest, TooFewRunningMinutesIsNotFlagged) {
    EXPECT_FALSE(chargeIsStalled(cfg(), 30 * 60, 30.0f, 30.0f));
}

TEST(ChargeWatchdogTest, AUnitThatNeverRanIsNotBlamedOnTheSensor) {
    EXPECT_FALSE(chargeIsStalled(cfg(), 0, 45.0f, 45.0f))
        << "an idle unit is the park rule's business; counting granted minutes as "
           "running minutes would call every parked tank a broken sensor";
}

TEST(ChargeWatchdogTest, FloorKeepsTheWatchdogFromBarking) {
    ArbitrationConfig tight;
    tight.charge_watchdog_min = 5;      // below the floor
    tight.applyFloors();
    EXPECT_EQ(60, tight.charge_watchdog_min);
}

namespace {
ArbiterInput bothDemands(TankState tank, int current_mode) {
    ArbiterInput in{};
    in.working_mode = 3;
    in.tank = tank;
    in.current_mode = current_mode;
    in.seconds_since_last_write = 3600;
    in.dhw_granted = (current_mode == MODE_SET_DHW_ONLY);
    in.unit_actuating = true;
    in.seconds_since_park = ArbitrationConfig::kNoPark;
    return in;
}
} // namespace

// ---- A park must not become a metronome ----
// A unit that will not charge to a register's value (measured 2026-11-13: parked at
// 45.0 C for 80 minutes on a grant to 50.0) stays parked when asked again. So the
// release helps the house, but re-asking every switch-gap would write the mode twice
// a quarter-hour forever. Once parked, the next attempt waits for the cooldown, or
// for a hot-water draw: that is a demand we have to serve whatever it cost last time.

TEST(ParkHysteresisTest, ReGrantingAfterAParkWaitsForTheCooldown) {
    ArbitrationConfig cfg;
    ArbiterInput in = bothDemands(TankState::Calling, MODE_SET_HEAT_DHW);
    in.seconds_since_last_write = 600;   // the ordinary switch gap is long gone
    in.seconds_since_park = 300;
    EXPECT_EQ(decideDeviceMode(cfg, in), MODE_SET_HEAT_DHW);
}

TEST(ParkHysteresisTest, AFinishedCooldownGrantsTheChargeAgain) {
    ArbitrationConfig cfg;
    ArbiterInput in = bothDemands(TankState::Calling, MODE_SET_HEAT_DHW);
    in.seconds_since_last_write = 600;
    in.seconds_since_park = cfg.park_cooldown_s;
    EXPECT_EQ(decideDeviceMode(cfg, in), MODE_SET_DHW_ONLY);
}

TEST(ParkHysteresisTest, AHotWaterDrawIsServedThroughTheCooldown) {
    ArbitrationConfig cfg;
    ArbiterInput in = bothDemands(TankState::Calling, MODE_SET_HEAT_DHW);
    in.seconds_since_last_write = 600;
    in.seconds_since_park = 0;
    in.tank_drawing = true;
    EXPECT_EQ(decideDeviceMode(cfg, in), MODE_SET_DHW_ONLY);
}

TEST(ArbitrationConfigTest, ParkDefaultsAndFloorsAreEnforced) {
    ArbitrationConfig fresh;
    EXPECT_EQ(fresh.park_timeout_s, 600);
    EXPECT_EQ(fresh.park_cooldown_s, 900);

    ArbitrationConfig cfg;
    cfg.park_timeout_s = 10;
    cfg.park_cooldown_s = 5;
    cfg.applyFloors();
    EXPECT_EQ(cfg.park_timeout_s, ArbitrationConfig::kMinParkTimeoutS);
    EXPECT_EQ(cfg.park_cooldown_s, ArbitrationConfig::kMinParkCooldownS);
}
