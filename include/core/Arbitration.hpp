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
  Unknown,            ///< no usable reading; hold whatever the unit is doing
  Calling,            ///< below target - hysteresis, domestic hot water wants the compressor
  SatisfiedCandidate, ///< at or above target, but not yet confirmed on two ticks
  Satisfied,          ///< confirmed at target; heating may have the compressor
};

/** Tunables, with the values and floors measured on the unit (spec settings table). */
struct ArbitrationConfig
{
  float hysteresis_c = 5.0f;  ///< width of the window heating may run in
  int min_switch_gap_s = 120; ///< debounce between device-mode writes
  float draw_override_c_per_min = 1.0f;
  int charge_watchdog_min = 120;
  /* A DHW grant the unit does not act on is not a demand being served: measured
   * 2026-11-13, the unit sat idle for 80 min on a grant with the tank parked at its
   * economic setpoint, which locked space heating out for nothing. */
  int park_timeout_s = 600;
  /* After releasing a declined charge, the next attempt waits this long. A unit that
   * will not charge to a register's value will not change its mind either, so
   * re-asking every switch gap would be a metronome writing the mode twice a
   * quarter-hour forever. */
  int park_cooldown_s = 900;
  static constexpr int kMinChargeWatchdogMin = 60;
  static constexpr int kMinParkTimeoutS = 180;
  static constexpr int kMinParkCooldownS = 300;
  /** ArbiterInput::seconds_since_park for "no park has happened". */
  static constexpr int kNoPark = 0x7fffffff; ///< a fall this fast is somebody using hot water

  /// How far below the target counts as "at temperature", in degC.
  static constexpr float kReleaseMarginC = 0.5f;

  /// Floor for each tunable; anything below it is clamped and logged.
  static constexpr float kMinHysteresisC = 5.0f;
  static constexpr int kMinSwitchGapS = 60;
  static constexpr float kMinDrawOverrideCPerMin = 0.5f;

  void applyFloors();
};

/** One tank temperature observation, plus the caller's run counter at target. */
struct TankReading
{
  float temp_c;
  float target_c;
  bool valid;          ///< false when the register could not be read
  int ticks_at_target; ///< consecutive observations at or above target - margin
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

/** Everything the decision needs, evaluated once per control tick. */
struct ArbiterInput
{
  int working_mode = 3; ///< application working mode, 0..3
  TankState tank = TankState::Unknown;
  int current_mode = MODE_SET_HEAT_DHW; ///< device mode we believe is active
  bool tank_drawing = false;            ///< falling at or above draw_override_c_per_min
  bool blocked = false;                 ///< defrost or anti-freeze in progress
  int seconds_since_last_write = 0;     ///< since the last verified device-mode write
  bool dhw_granted = false;             ///< DHW-only is what we asked for and the unit says
  bool unit_actuating = true;           ///< compressor or flow is actually moving heat
  int seconds_since_actuating = 0;      ///< since either did, while DHW was granted
  int seconds_since_park = ArbitrationConfig::kNoPark; ///< since a release; never = kNoPark
};

/**
 * The device mode the unit should be in, or @p current_mode to leave it alone.
 *
 * Nothing here writes; ControlLoop applies the answer. Holding the current mode
 * is the fallthrough, so an unrecognised state can never trigger a write.
 */
int decideDeviceMode(const ArbitrationConfig& cfg, const ArbiterInput& in);

/// Shortest interval over which a fall in tank temperature can be judged at all.
constexpr double kMinDrawSampleS = 10.0;

/**
 * Is the tank falling because somebody is using hot water?
 *
 * Rate, not direction: a hot-water draw measured 3.2 degC/min, while
 * stratification relaxing after a release is around 0.1-0.2 degC/min. Only a
 * draw may override the switch gap, because only a draw has a person waiting.
 */
/// A 150 L tank on this unit measured 0.45-0.79 degC/min while charging. Half
/// the slowest rate ever seen is the line between a slow charge and a sensor
/// that is not telling the truth.
constexpr float kMinExpectedChargeRiseCPerMin = 0.25f;

/**
 * Has a DHW charge run for the watchdog period without the tank rising?
 *
 * This raises a flag and nothing else. A tank that will not rise while the
 * compressor is committed to it is either a sensor that has failed low or a
 * unit that cannot make heat; in both cases the answer is a person with a
 * multimeter, not handing the compressor to the house.
 */
/**
 * The DHW temperature the unit will actually charge to.
 *
 * The register the app writes (0x0194) is not the only DHW setpoint: with the unit in
 * its economic DHW sub-mode the charge runs to 0x0196 and stops there whatever 0x0194
 * says (measured 2026-11-13: parked at 45.0 for 80 minutes while 0x0194 read 50.0;
 * raising 0x0196 to 52.0 started the compressor inside two minutes). Neither register
 * declares which one the firmware is obeying, so demand is measured against the higher
 * of the two: that is the temperature the unit is willing to reach.
 */
float effectiveDhwTargetC(float user_target_c, float eco_target_c);

/**
 * Has the unit declined a DHW charge?
 *
 * A grant with the compressor and the flow both idle for the park timeout, on a tank
 * that is not falling, means the unit has stopped trying. Releasing the compressor to
 * the house is the only useful answer; holding it buys a tank that is not charging and
 * rooms that are not heating.
 */
bool chargeIsParked(const ArbitrationConfig& cfg, bool dhw_granted, bool unit_actuating,
                    bool tank_falling, int seconds_since_actuating);

bool chargeIsStalled(const ArbitrationConfig& cfg, int running_s, float start_c, float now_c);

bool tankIsDrawing(float previous_c, float current_c, double elapsed_s, float override_c_per_min);

/// Name of a tank state, for logs and the status payload.
inline const char* tankStateName(TankState state)
{
  switch (state)
  {
  case TankState::Unknown:
    return "unknown";
  case TankState::Calling:
    return "calling";
  case TankState::SatisfiedCandidate:
    return "candidate";
  case TankState::Satisfied:
    return "satisfied";
  }
  return "unknown";
}

} // namespace windmi

#endif // WINDMI_CORE_ARBITRATION_HPP
