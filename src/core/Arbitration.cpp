/**
 * @file core/Arbitration.cpp
 * @brief DHW demand arbiter implementation
 */

#include "core/Arbitration.hpp"
#include "utils/Logger.hpp"
#include "utils/LogTags.hpp"

namespace windmi {

int deviceModeForWorkingMode(int working_mode) {
    switch (working_mode) {
        case 0: return MODE_SET_OFF;         // Off
        case 1: return MODE_SET_DHW_ONLY;    // DHW only - the unit's native mode 5
        case 2: return MODE_SET_HEAT_ONLY;   // Heating only - the unit's native mode 4
        case 3: return MODE_SET_HEAT_DHW;    // Both; refined to 5 while the tank calls
        default: return MODE_SET_HEAT_DHW;   // Unknown: keep both functions available
    }
}

void ArbitrationConfig::applyFloors() {
    if (hysteresis_c < kMinHysteresisC) {
        WINDMI_LOG_WARN(LOG_TAG_CONTROLLOOP,
                        "arbitration hysteresis_c %.1f clamped to the floor %.1f degC: a narrower band "
                        "re-arms on top-of-tank probe noise and starves space heating",
                        hysteresis_c, kMinHysteresisC);
        hysteresis_c = kMinHysteresisC;
    }
    if (min_switch_gap_s < kMinSwitchGapS) {
        WINDMI_LOG_WARN(LOG_TAG_CONTROLLOOP, "arbitration min_switch_gap_s %d clamped to %d s",
                        min_switch_gap_s, kMinSwitchGapS);
        min_switch_gap_s = kMinSwitchGapS;
    }
    if (draw_override_c_per_min < kMinDrawOverrideCPerMin) {
        WINDMI_LOG_WARN(LOG_TAG_CONTROLLOOP, "arbitration draw_override_c_per_min %.2f clamped to %.2f",
                        draw_override_c_per_min, kMinDrawOverrideCPerMin);
        draw_override_c_per_min = kMinDrawOverrideCPerMin;
    }
}

TankState advanceTankState(TankState previous, const ArbitrationConfig& cfg,
                           const TankReading& reading) {
    if (!reading.valid || reading.temp_c <= 0.0f || reading.target_c <= 0.0f) {
        return TankState::Unknown;
    }

    if (reading.temp_c >= reading.target_c - ArbitrationConfig::kReleaseMarginC) {
        return (reading.ticks_at_target >= 2) ? TankState::Satisfied : TankState::SatisfiedCandidate;
    }

    if (reading.temp_c < reading.target_c - cfg.hysteresis_c) {
        return TankState::Calling;
    }

    // Inside the band: hold. With no history to hold - a restart with the tank
    // mid-band - assume domestic hot water is calling, because under
    // "DHW has priority" guessing satisfied is the mistake that costs hot water.
    return (previous == TankState::Unknown) ? TankState::Calling : previous;
}

} // namespace windmi
