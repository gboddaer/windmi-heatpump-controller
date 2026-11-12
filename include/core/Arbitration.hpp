/**
 * @file core/Arbitration.hpp
 * @brief DHW demand arbiter: which device mode the unit should be in
 */

#ifndef WINDMI_CORE_ARBITRATION_HPP
#define WINDMI_CORE_ARBITRATION_HPP

#include "config.h"

#include <cstdint>

namespace windmi {

/**
 * Device mode (register 0x002C) implied by an application working mode.
 *
 * Working modes are the application-level pair of enables the API and the
 * scheduler use: 0 Off, 1 DHW-only, 2 Heating-only, 3 DHW+Heating. The device
 * modes below are measured behaviour, not interpretation: mode 5 is what the
 * display controller writes itself when the user selects DHW-only, and DHW
 * priority is a property of that mode - the priority register 0x02BF is
 * accepted and ignored (spec findings F1, F4 and F7).
 *
 * Working mode 3 resolves to MODE_SET_HEAT_DHW here, which is the heating-priority
 * mode the unit then serves the tank in only when heating is not calling. The
 * arbiter refines it to MODE_SET_DHW_ONLY while the tank is calling; see
 * decideDeviceMode().
 */
int deviceModeForWorkingMode(int working_mode);

/**
 * Where the tank is in the hysteresis band the arbiter works in.
 *
 * A single reading is not enough to act on: the probe sits at the top of the
 * tank and can read high while the bulk is cold, or plunge during a draw
 * (spec F9).
 */
enum class TankState : uint8_t {
    Unknown,             ///< no usable reading; hold whatever the unit is doing
    Calling,             ///< below target - hysteresis, domestic hot water wants the compressor
    SatisfiedCandidate,  ///< at or above target, but not yet confirmed on two ticks
    Satisfied,           ///< confirmed at target; heating may have the compressor
};

/** Tunables, with the values and floors measured on the unit (spec settings table). */
struct ArbitrationConfig {
    float hysteresis_c = 5.0f;             ///< width of the window heating may run in
    int min_switch_gap_s = 120;            ///< debounce between device-mode writes
    float draw_override_c_per_min = 1.0f;  ///< a fall this fast is somebody using hot water

    /// How far below the target counts as "at temperature", in degC.
    static constexpr float kReleaseMarginC = 0.5f;

    /// Floor for each tunable; anything below it is clamped and logged.
    static constexpr float kMinHysteresisC = 5.0f;
    static constexpr int kMinSwitchGapS = 60;
    static constexpr float kMinDrawOverrideCPerMin = 0.5f;

    void applyFloors();
};

/** One tank temperature observation, plus the caller's run counter at target. */
struct TankReading {
    float temp_c;
    float target_c;
    bool valid;            ///< false when the register could not be read
    int ticks_at_target;   ///< consecutive observations at or above target - margin
};

/**
 * Move the tank state one observation forward.
 *
 * The persistence in the middle of the band is the hysteresis: inside the band
 * nothing changes, so a sag caused by stratification relaxing after a release
 * cannot undo satisfaction, and only a fall through the whole band re-arms a
 * DHW episode.
 */
TankState advanceTankState(TankState previous, const ArbitrationConfig& cfg,
                           const TankReading& reading);

} // namespace windmi

#endif // WINDMI_CORE_ARBITRATION_HPP
