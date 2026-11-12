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

int decideDeviceMode(const ArbitrationConfig& cfg, const ArbiterInput& in) {
    // Defrost and anti-freeze are the unit's own business; a mode change in the
    // middle of one is a write it will not honour. The decision is not lost -
    // next tick asks the same question again.
    if (in.blocked) {
        return in.current_mode;
    }

    // Only "both enabled" has anything to arbitrate: the single-demand rows are
    // one native mode each.
    if (in.working_mode != 3) {
        return deviceModeForWorkingMode(in.working_mode);
    }

    switch (in.tank) {
        case TankState::Satisfied:
            // Release immediately. Delaying here punishes the house for a
            // debounce that exists to protect the compressor and the valve,
            // and this transition serves both.
            return MODE_SET_HEAT_DHW;

        case TankState::Calling:
            if (in.tank_drawing || in.seconds_since_last_write >= cfg.min_switch_gap_s) {
                return MODE_SET_DHW_ONLY;
            }
            return in.current_mode;

        case TankState::SatisfiedCandidate:
        case TankState::Unknown:
        default:
            // One tick at target is not satisfaction (the probe sits at the top of
            // the tank), and no reading at all is not a demand either.
            return in.current_mode;
    }
}

bool tankIsDrawing(float previous_c, float current_c, double elapsed_s,
                   float override_c_per_min) {
    if (previous_c <= 0.0f || current_c <= 0.0f) {
        return false;          // no previous sample, or a reading we cannot use
    }
    if (elapsed_s < kMinDrawSampleS) {
        return false;          // too short an interval to speak of a rate
    }
    const double fall_c = static_cast<double>(previous_c) - current_c;
    if (fall_c <= 0.0) {
        return false;          // rising or steady
    }
    return (fall_c / (elapsed_s / 60.0)) >= override_c_per_min;
}

} // namespace windmi
