/**
 * @file core/ControlLoop.hpp
 * @brief Control loop for heat pump management
 *
 * StatusSnapshot matches master branch status_snapshot_t exactly.
 * Control loop logic matches master branch control_loop.c.
 */

#ifndef WINDMI_CORE_CONTROL_LOOP_HPP_
#define WINDMI_CORE_CONTROL_LOOP_HPP_

#include <cstdint>
#include <memory>
#include <atomic>
#include <functional>
#include <string>
#include "utils/Platform.hpp"
#include "core/Arbitration.hpp"
#include "core/CycleCop.hpp"
#include "core/DiagnosticRegisters.hpp"
#include "config.h"
#include "core/Scheduler.hpp"

namespace windmi {

// Forward declarations
class IModbusClient;

/**
 * @brief Occupancy mode values for register 0x0029 (Occupancy Mode)
 *
 * Verified live on device:
 *   0 = Away  (DHW mode = Regular/2)
 *   1 = Sleep (DHW mode = Eco/0)
 *   2 = Home  (DHW mode = Regular/2)
 *
 * Register 0x0067 (Normal/Eco Switch) is read-only and reflects the
 * current state derived from occupancy mode.
 */
enum class OccupancyMode : int {
  Away = 0,
  Sleep = 1,
  Home = 2,
};

/**
 * @brief Command types for the control loop
 *
 * These match the C cmd_type_t enum from spsc_queue.h.
 */
enum class CommandType : uint8_t {
  CMD_SET_DHW_TEMP = 0,
  CMD_SET_HEATING_TEMP = 1,
  CMD_SET_PRIORITY = 2,
  CMD_SET_RUNNING_MODE = 3,
  CMD_SET_OCCUPANCY_MODE = 4,
};

/**
 * @brief Command structure matching C cmd_t from spsc_queue.h
 */
struct Command
{
  CommandType type;
  float float_val = 0.0f;
  int int_val = 0;
};

/**
 * @brief Status snapshot structure matching C status_snapshot_t from spsc_queue.h
 *
 * Field names and types match master branch exactly.
 * - running_mode: device register value (0=Off, 1=Cool+DHW, 2=Heat+DHW)
 * - running_status: device register value (0=Off, 1=Cool, 2=Heat, 4=DHW, 7=Defrost, 20=Antifreeze)
 * - dhw_priority: true=DHW priority, false=heating priority
 * - is_running: true if device is active (running_status != 0)
 * - working_mode: application-level (0=Off, 1=DHW-only, 2=Heating-only, 3=DHW+Heating)
 * - occupancy_mode: 0=Away, 1=Sleep, 2=Home (from REG_OCCUPANCY_MODE 0x0029)
 */
struct StatusSnapshot
{
  float outdoor_temp = 0.0f;
  float indoor_temp = 0.0f;
  float leaving_water_temp = 0.0f;
  float entering_water_temp = 0.0f; // From REG_ENTERING_WATER_TEMP (0x0003, 0.1°C)
  float dhw_tank_temp = 0.0f;
  float dhw_target = 0.0f;
  float heating_target = 0.0f;
  int running_mode = 0;   // From REG_RUNNING_MODE (0x002C)
  int running_status = 0; // From REG_RUNNING_STATUS (0x002D)
  bool dhw_priority = false;
  // DHW arbiter view: what the unit is doing for the tank and why.
  bool dhw_calling = false;                ///< tank below target - hysteresis
  bool dhw_state_known = false;            ///< tank state derived from a usable reading
  int requested_device_mode = 0;           ///< the mode the arbiter last decided on
  int seconds_since_mode_write = 0;        ///< since the last verified 0x002C write
  bool dhw_sensor_fault_suspected = false; ///< charge ran its length without a rise
  int tank_read_failures = 0;              ///< consecutive ticks with no usable tank reading
  // What the arbiter made of all that, in the terms an operator reads.
  std::string dhw_state = "unknown"; ///< calling / candidate / satisfied / unknown
  bool unit_actuating = false;       ///< compressor or flow is moving heat
  float dhw_eco_target = 0.0f;       ///< 0x0196; 0 = the unit does not answer it
  float water_control_point = 0.0f;  ///< 0x0033, informational only
  float effective_dhw_target = 0.0f; ///< the setpoint the unit will charge to
  /**
   * Increments on every published snapshot, offline ones included.
   *
   * Without it, a stalled control loop and a unit with nothing to say are the same
   * page: the registers stop moving in both cases. Measured 2026-11-13 while
   * watching for misbehaviour - publish gaps of 30, 60 and 120 s were all normal
   * for a stable tank, so liveness could not be told from calm.
   */
  uint64_t status_sequence = 0;
  bool is_running = false;
  bool device_online = false;
  // Power monitoring
  float ac_current = 0.0f;  // AC current in Amps (raw / 2, unverified hypothesis)
  float dc_current = 0.0f;  // DC current in Amps (raw * 4)
  float ac_voltage = 0.0f;  // AC voltage in Volts (raw)
  float dc_voltage = 0.0f;  // DC voltage in Volts (raw * 2, Polish manual)
  float ac_power_va = 0.0f; // AC apparent power in VA (ac_voltage * ac_current)
  float ac_power_w = 0.0f;  // AC real power in Watts (estimated: VA * power_factor)
  bool power_valid = false; // True if both AC current and AC voltage were read

  DiagnosticSnapshot diagnostics;

  // Diagnostic registers
  float compressor_freq = 0.0f;   // Actual compressor speed register, scale unverified
  float water_flow = 0.0f;        // Water flow in m³/h (from 0x102A, raw/100)
  int unit_capacity_kw = 0;       // Unit capacity in kW (4/6/8/10/12/14/16)
  int actual_capacity_output = 0; // Actual capacity output (from 0x1004)
  int dhw_valve_status = -1;      // DHW valve status (0=Opened, 1=Closed, -1=unknown)
  int dhw_mode_status = -1;       // DHW mode status (0=Eco, 1=Anti-Legionella, 2=Regular)
  int odu_input_status = 0;       // Outdoor unit input status bit flags (from 0x101F)
  int compressor_runtime_h = 0;   // Compressor runtime in hours (from 0x0174)
  int pump_runtime_h = 0;         // Pump runtime in hours (from 0x0176)

  // COP estimation (calculated from water flow + delta-T + power)
  float heat_output_w = 0.0f; // Estimated heat output in Watts
  float cop = 0.0f;           // Coefficient of Performance (heat_out / power_in)
  bool cop_valid = false;     // True if COP calculation had valid inputs
  bool heat_output_valid = false;
  bool compressor_freq_valid = false;
  double heating_cycle_cop = 0;
  double dhw_cycle_cop = 0;
  bool heating_cycle_cop_valid = false;
  bool dhw_cycle_cop_valid = false;

  // Working mode (0=Off, 1=DHW-only, 2=Heating-only, 3=DHW+Heating)
  int working_mode = 0;

  // Occupancy mode (0=Away, 1=Sleep, 2=Home)
  int occupancy_mode = 2;

  // Schedule state; "active" means a matching period, not default application.
  ScheduleDefaults schedule_defaults;
  std::string control_source = "manual";
  bool schedule_active = false;
  bool schedule_paused = false;
  bool schedule_enabled = false;
  bool schedule_pending = false;
  bool schedule_persistence_error = false;
  uint64_t schedule_revision = 0;
  std::string schedule_instance_id;
};

/**
 * @brief Priority mode enum (internal, not a register value)
 *
 */
enum class PriorityMode : uint8_t {
  Dhw,    // DHW priority
  Heating // Heating priority
};

/**
 * @brief SPSC command queue interface
 *
 * Used by WebServer to push commands and by ControlLoop to consume them.
 * The web server is the single producer; the control loop is the single consumer.
 */
class CmdQueue
{
public:
  static constexpr size_t CAPACITY = 16;

  CmdQueue();
  bool push(const Command& cmd);
  bool pop(Command& cmd);
  bool empty() const;

private:
  Command mBuf[CAPACITY];
  alignas(64) std::atomic<uint32_t> mHead;
  alignas(64) std::atomic<uint32_t> mTail;
};

/**
 * @brief SPSC status queue with overwrite (ring buffer)
 *
 * Used by ControlLoop to publish status snapshots and by WebServer to consume them.
 * The control loop is the single producer; the web server is the single consumer.
 *
 * When full, push() overwrites the oldest entry (ring buffer behavior) to prevent
 * "queue full" warnings when the consumer is idle. This is appropriate for status
 * monitoring where only the latest snapshot matters.
 */
class StatusQueue
{
public:
  static constexpr size_t CAPACITY = 32;

  StatusQueue();

  /**
   * @brief Push a status snapshot (overwrites oldest if full)
   * @param item Snapshot to push
   * @return Always true (never fails due to full queue)
   */
  bool push(const StatusSnapshot& item);

  /**
   * @brief Pop the oldest snapshot
   * @param item Output parameter
   * @return true if snapshot was available, false if queue was empty
   */
  bool pop(StatusSnapshot& item);

  /**
   * @brief Get the latest snapshot without consuming it
   * @param item Output parameter
   * @return true if snapshot was available, false if queue was empty
   */
  bool latest(StatusSnapshot& item);

private:
  StatusSnapshot mBuf[CAPACITY];
  alignas(64) std::atomic<uint32_t> mHead;
  alignas(64) std::atomic<uint32_t> mTail;
  alignas(64) std::atomic<uint32_t> mWriteIndex; // For ring buffer overwrite
};

/**
 * @brief Control loop class for heat pump management
 *
 * Manages the control loop thread, processes commands from the command queue,
 * reads device status via Modbus, applies control logic, and publishes
 * status snapshots to the status queue.
 *
 * Matches master branch control_loop.c functionality exactly.
 */
class ControlLoop
{
public:
  using SettingsCallback = std::function<void(int working_mode, const std::string& priority,
                                              float dhw_target, float heating_target)>;

  ControlLoop();
  ~ControlLoop();

  /**
   * @brief Set initial settings from persistent config
   * @param working_mode Initial working mode (0-3)
   * @param priority Priority string ("dhw" or "heating")
   * @param dhw_target DHW target temperature
   * @param heating_target Heating target temperature
   */
  void setInitialSettings(int working_mode, const std::string& priority, float dhw_target,
                          float heating_target);

  /**
   * @brief Set callback to fire on settings changes
   */
  void setSettingsCallback(SettingsCallback callback);

  /**
   * @brief Start the control loop thread
   * @param client IModbusClient to use for device communication
   * @param cmd_queue Command queue to consume
   * @param status_queue Status queue to publish to
   * @return true if successful, false otherwise
   */
  bool start(IModbusClient* client, CmdQueue* cmd_queue, StatusQueue* status_queue);

  void stop();
  bool isRunning() const;

  /**
   * @brief Get reference to the internal scheduler
   */
  Scheduler& scheduler();
  void setScheduleMinuteForTesting(int minute)
  {
    mScheduleMinuteForTesting.store(minute);
  }

  /**
   * @brief Kick the control loop to wake it immediately
   *
   * Called when a command is pushed to the cmd_queue so the
   * control loop processes it without waiting for the next poll.
   */
  void kick();

  /**
   * @brief Device mode written to 0x002C when the application exits
   *
   * Always MODE_SET_DHW_ONLY. Domestic hot water is the one demand nothing else
   * in the house can cover - space heating has a wood burner and an air-to-air
   * pump - and with the arbiter stopped, mode 2 is heating-first and the tank
   * has no advocate, so hot water runs out quietly. Mode 0 is never right: it
   * stops both.
   *
   * Accepted consequence: while the application is not running there is no
   * central heating, and the way to ask for it is the display controller. This
   * overruled the original shutdown ruling in the arbiter plan on 2026-11-13.
   */
  static int shutdownDeviceMode()
  {
    return MODE_SET_DHW_ONLY;
  }

  /**
   * @brief Tunables for the DHW demand arbiter
   *
   * Values below a floor are clamped and logged, because the floors are the
   * compressor's and the diverter valve's, not a preference.
   */
  void setArbitration(const ArbitrationConfig& arbitration);

  /**
   * @brief Run the whole control loop without writing anything to the unit
   *
   * Decisions are evaluated and logged, and the status payload says what the
   * arbiter would have done, so the logic can be watched against a live unit
   * before it is allowed to touch it.
   */
  void setDryRun(bool enabled)
  {
    mDryRun = enabled;
  }
  bool dryRun() const
  {
    return mDryRun;
  }

  /**
   * @brief Move the loop's notion of now, for tests only
   *
   * The charge watchdog runs on minutes and the tank state on minutes of
   * persistence; a test that waited for either would be a test that takes two
   * hours to say nothing. Nothing in production calls this.
   */
  void advanceClockForTesting(int64_t ms)
  {
    mClockOffsetMs += ms;
  }

private:
  void threadFunc();
  bool readStatus(StatusSnapshot& status);
  void processCommands();
  void applyControlLogic(StatusSnapshot& status);
  void updateTankState(const StatusSnapshot& status);
  uint64_t nowMs() const;
  void updateChargeWatchdog(const StatusSnapshot& status);
  void updateCycleCop(StatusSnapshot& status);
  void updateSchedule(StatusSnapshot& status, int minute);

  /**
   * Publish a snapshot that says the unit is not answering.
   *
   * The retry path used to loop without publishing, so /api/status kept serving the
   * last good snapshot with deviceOnline true: numbers from before the failure,
   * presented as now. Say it, and say it again each attempt, so the age of the
   * statement is knowable from outside.
   */
  void publishOfflineStatus();
  int setRunningMode(int mode);
  int setDhwTarget(float temp);
  int setHeatingTarget(float temp);

  // Modbus and queue pointers (not owned)
  IModbusClient* mModbusClient;
  CmdQueue* mCmdQueue;
  StatusQueue* mStatusQueue;

  // Thread management
  std::unique_ptr<windmi::Thread> mThread;
  std::atomic<bool> mRunning;
  std::atomic<bool> mStopRequested;

  // Kick mechanism
  windmi::Mutex mKickMutex;
  windmi::ConditionVariable mKickCond;
  uint64_t mKickGeneration;

  // Control state (matches master branch)
  PriorityMode mCurrentPriority;
  int mCurrentMode;        // Device mode we last set (0, 1, or 2)
  int mDesiredWorkingMode; // Application-level (0-3)
  float mLastHeatingTarget;
  float mSavedDhwTarget;
  // DHW demand arbiter state
  ArbitrationConfig mArbitration;
  TankState mTankState = TankState::Unknown;
  bool mTankDrawing = false;
  int mTicksAtTarget = 0;
  int mTankReadFailures = 0;
  float mLastTankTempC = -1.0f;
  uint64_t mLastTankSampleMs = 0;
  uint64_t mLastModeWriteMs = 0;
  int mRequestedDeviceMode = MODE_SET_HEAT_DHW;
  uint64_t mChargeStartedMs = 0;
  float mChargeStartC = -1.0f;
  int mDhwRunningS = 0; ///< compressor seconds spent on a DHW charge
  bool mUnitActuating = false;
  uint64_t mLastActuatingMs = 0;
  uint64_t mParkedAtMs = 0;     ///< last release of a declined DHW charge
  uint64_t mStatusSequence = 0; ///< published snapshots, for liveness
  CycleCop mCycleCop;
  DiagnosticMonitor mDiagnosticMonitor;
  float mEffectiveDhwTargetC = 0.0f;
  uint64_t mChargeSampleMs = 0; ///< end of the last actuating charge sample
  bool mChargeWasCompressing = false;
  int mDhwDriftTicks = 0; ///< consecutive ticks of a divergent setpoint
  int mHeatingDriftTicks = 0;
  bool mDhwSensorFaultSuspected = false;
  int64_t mClockOffsetMs = 0;
  bool mDryRun = false;
  float mSavedHeatingTarget;
  bool mSavedTargetsInitialized;

  void fireSettingsCallback();

  // Settings persistence
  SettingsCallback mSettingsCallback;
  float mMDhwTarget = 45.0f;
  float mMHeatingTarget = 40.0f;

  // Schedule
  Scheduler mScheduler;
  ScheduleDefaults mLastScheduleSettings;
  uint64_t mLastScheduleRevision = 0;
  bool mScheduleWasApplying = false;
  std::atomic<int> mScheduleMinuteForTesting{-1};
};

} // namespace windmi

#endif // WINDMI_CORE_CONTROL_LOOP_HPP_