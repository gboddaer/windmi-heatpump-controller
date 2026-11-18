/**
 * @file src/core/ControlLoop.cpp
 * @brief Control loop implementation matching master branch control_loop.c
 */

#include "core/ControlLoop.hpp"
#include "core/Arbitration.hpp"
#include "core/ModeNames.hpp"
#include "modbus/IModbusClient.hpp"
#include "utils/Logger.hpp"
#include "utils/LogTags.hpp"
#include "config.h"

#include <cstring>
#include <cmath>
#include <chrono>
#include <ctime>

namespace windmi {
static void copyScheduleMetadata(StatusSnapshot& status, const ScheduleState& state)
{
  status.schedule_enabled = state.enabled;
  status.schedule_defaults = state.defaults;
  status.control_source = state.enabled && !state.paused ? "pending" : "manual";
  status.schedule_pending = state.enabled && !state.paused;
  status.schedule_paused = state.paused;
  status.schedule_revision = state.revision;
  status.schedule_instance_id = state.instanceId;
  status.schedule_persistence_error = state.persistenceError;
}

namespace {
/** How many consecutive ticks must agree that a setpoint moved before we write ours back. */
constexpr int kSetpointDriftConfirmTicks = 2;
} // namespace

// ---- CmdQueue implementation ----

CmdQueue::CmdQueue() : mHead(0), mTail(0)
{}

bool CmdQueue::push(const Command& cmd)
{
  const uint32_t current_tail = mTail.load(std::memory_order_relaxed);
  const uint32_t next_tail = (current_tail + 1) % CAPACITY;
  if (next_tail == mHead.load(std::memory_order_acquire))
    return false; // Full
  mBuf[current_tail] = cmd;
  mTail.store(next_tail, std::memory_order_release);
  return true;
}

bool CmdQueue::pop(Command& cmd)
{
  const uint32_t current_head = mHead.load(std::memory_order_relaxed);
  if (current_head == mTail.load(std::memory_order_acquire))
    return false; // Empty
  cmd = mBuf[current_head];
  mHead.store((current_head + 1) % CAPACITY, std::memory_order_release);
  return true;
}

bool CmdQueue::empty() const
{
  return mHead.load(std::memory_order_acquire) == mTail.load(std::memory_order_acquire);
}

// ---- StatusQueue implementation (ring buffer with overwrite) ----

StatusQueue::StatusQueue() : mHead(0), mTail(0), mWriteIndex(0)
{}

bool StatusQueue::push(const StatusSnapshot& item)
{
  // Ring buffer: always write, overwrite oldest if full
  const uint32_t write_idx = mWriteIndex.fetch_add(1, std::memory_order_relaxed);
  const uint32_t buf_idx = write_idx % CAPACITY;

  mBuf[buf_idx] = item;

  // Update tail to point past this write
  mTail.store(write_idx + 1, std::memory_order_release);

  // If we overwrote old data, advance head to maintain at least one slot
  // This prevents reading stale data that's been overwritten
  uint32_t current_head = mHead.load(std::memory_order_acquire);
  uint32_t current_tail = write_idx + 1;

  // Ensure head doesn't lag too far behind (keep at most CAPACITY-1 items)
  if (current_tail - current_head > CAPACITY - 1)
  {
    mHead.store(current_tail - (CAPACITY - 1), std::memory_order_release);
  }

  return true; // Always succeeds
}

bool StatusQueue::pop(StatusSnapshot& item)
{
  const uint32_t current_head = mHead.load(std::memory_order_relaxed);
  const uint32_t current_tail = mTail.load(std::memory_order_acquire);

  if (current_head >= current_tail)
    return false; // Empty

  const uint32_t buf_idx = current_head % CAPACITY;
  item = mBuf[buf_idx];
  mHead.store(current_head + 1, std::memory_order_release);
  return true;
}

bool StatusQueue::latest(StatusSnapshot& item)
{
  const uint32_t current_tail = mTail.load(std::memory_order_acquire);
  const uint32_t current_head = mHead.load(std::memory_order_acquire);

  if (current_head >= current_tail)
    return false; // Empty

  // Get the last written item (tail - 1)
  const uint32_t last_idx = current_tail - 1;
  const uint32_t buf_idx = last_idx % CAPACITY;
  item = mBuf[buf_idx];
  return true;
}

// ---- Helper functions ----

static inline float raw_to_temp(int16_t raw)
{
  return static_cast<float>(raw) / 10.0f;
}

static inline int16_t temp_to_raw(float temp)
{
  return static_cast<int16_t>(temp * 10.0f);
}

// ---- ControlLoop implementation ----

ControlLoop::ControlLoop()
    : mModbusClient(nullptr), mCmdQueue(nullptr), mStatusQueue(nullptr), mRunning(false),
      mStopRequested(false), mKickGeneration(0), mCurrentPriority(PriorityMode::Dhw),
      mCurrentMode(MODE_SET_HEAT_DHW), mDesiredWorkingMode(3), mLastHeatingTarget(45.0f),
      mSavedDhwTarget(46.0f), mSavedHeatingTarget(45.0f), mSavedTargetsInitialized(false)
{}

void ControlLoop::setInitialSettings(int working_mode, const std::string& priority,
                                     float dhw_target, float heating_target)
{
  mDesiredWorkingMode = working_mode;
  mCurrentPriority = (priority == "heating") ? PriorityMode::Heating : PriorityMode::Dhw;
  mMDhwTarget = dhw_target;
  mMHeatingTarget = heating_target;
}

void ControlLoop::setSettingsCallback(SettingsCallback callback)
{
  mSettingsCallback = callback;
}

static inline std::string priorityToString(PriorityMode priority)
{
  return priority == PriorityMode::Heating ? "heating" : "dhw";
}

void ControlLoop::fireSettingsCallback()
{
  if (mSettingsCallback)
  {
    mSettingsCallback(mDesiredWorkingMode, priorityToString(mCurrentPriority), mMDhwTarget,
                      mMHeatingTarget);
  }
}

ControlLoop::~ControlLoop()
{
  stop();
}

bool ControlLoop::start(IModbusClient* client, CmdQueue* cmd_queue, StatusQueue* status_queue)
{
  if (mRunning.load())
    return true;

  mModbusClient = client;
  mCmdQueue = cmd_queue;
  mStatusQueue = status_queue;
  mStopRequested.store(false);
  mRunning.store(true);

  mThread = std::make_unique<windmi::Thread>([this]() { threadFunc(); });
  return true;
}

void ControlLoop::stop()
{
  if (!mRunning.load())
    return;

  mStopRequested.store(true);
  mRunning.store(false);

  // Wake the thread if it's sleeping
  kick();

  if (mThread && mThread->joinable())
  {
    mThread->join();
  }
}

bool ControlLoop::isRunning() const
{
  return mRunning.load();
}

Scheduler& ControlLoop::scheduler()
{
  return mScheduler;
}

void ControlLoop::kick()
{
  windmi::LockGuard lock(mKickMutex);
  ++mKickGeneration;
  mKickCond.notify_one();
}

int ControlLoop::setRunningMode(int mode)
{
  if (!mModbusClient)
    return -1;

  if (mDryRun)
  {
    WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "[dry-run] would write device mode 0x002C = %d (%s)", mode,
                    mode_to_string(mode));
    return 0;
  }

  // 0..5: the vendor modes 0-2 plus the GCHV modes 3-4-5 this unit accepts and
  // the display controller writes itself (spec F1, F2).
  if (mode < 0 || mode > 5)
  {
    WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "Invalid device mode %d: supported are 0..5", mode);
    return -1;
  }

  try
  {
    mModbusClient->writeRegister(REG_RUNNING_MODE, static_cast<uint16_t>(mode));

    // A device that does not hold the value has not accepted it. Reading it
    // back here means a refused mode is reported as the failure it is, on this
    // tick, instead of looking like a success until the next poll.
    const int16_t held = mModbusClient->readRegister(REG_RUNNING_MODE);
    if (held != mode)
    {
      WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "Device did not accept mode %d: 0x002C reads %d", mode,
                       static_cast<int>(held));
      return -1;
    }

    mLastModeWriteMs = nowMs();
    WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Set running mode to %d (verified by read-back)", mode);
    return 0;
  } catch (const ModbusException& e)
  {
    WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "Failed to set running mode to %d: %s", mode, e.what());
    return -1;
  }
}

int ControlLoop::setDhwTarget(float temp)
{
  if (!mModbusClient)
    return -1;

  if (mDryRun)
  {
    WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "[dry-run] would write DHW target %.1f C", temp);
    mMDhwTarget = temp;
    return 0;
  }

  int16_t raw_temp = temp_to_raw(temp);
  try
  {
    mModbusClient->writeRegister(REG_DHW_TARGET, static_cast<uint16_t>(raw_temp));
    mMDhwTarget = temp;
  } catch (const ModbusException& e)
  {
    WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "Failed to set DHW target to %.1f: %s", temp, e.what());
    return -1;
  }

  // And the economic sub-mode's copy. The unit charges to whichever DHW setpoint
  // its active sub-mode uses and does not expose which one that is (measured
  // 2026-11-13: parked at 0x0196 = 45.0 while 0x0194 said 50.0), so both carry the
  // user's wish. Firmware without the register is a warning, not a failed command.
  try
  {
    mModbusClient->writeRegister(REG_DHW_TARGET_ECO, static_cast<uint16_t>(raw_temp));
    WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Set DHW target to %.1f C (normal and economic setpoints)",
                    temp);
  } catch (const ModbusException& e)
  {
    WINDMI_LOG_WARN(LOG_TAG_CONTROLLOOP,
                    "DHW economic setpoint not written (%s): the unit will reach %.1f C only "
                    "in its normal DHW sub-mode",
                    e.what(), temp);
  }

  fireSettingsCallback();
  return 0;
}

int ControlLoop::setHeatingTarget(float temp)
{
  if (!mModbusClient)
    return -1;

  if (mDryRun)
  {
    WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "[dry-run] would write heating target %.1f C", temp);
    mMHeatingTarget = temp;
    return 0;
  }

  int16_t raw_temp = temp_to_raw(temp);
  try
  {
    mModbusClient->writeRegister(REG_HEATING_TARGET, static_cast<uint16_t>(raw_temp));
    WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Set heating target to %.1f C", temp);
    mMHeatingTarget = temp;
    fireSettingsCallback();
    return 0;
  } catch (const ModbusException& e)
  {
    WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "Failed to set heating target to %.1f: %s", temp,
                     e.what());
    return -1;
  }
}

bool ControlLoop::readStatus(StatusSnapshot& status)
{
  if (!mModbusClient)
    return false;

  bool ok = true;
  bool got_entering_water = false;
  bool got_water_flow = false;
  int16_t raw;
  mDiagnosticMonitor.beginSample();
  const auto readRegister = [this](uint16_t address) {
    return mDiagnosticMonitor.readRegister(*mModbusClient, address, nowMs());
  };

  // Read outdoor temp (0x0001)
  try
  {
    raw = readRegister(REG_OUTDOOR_TEMP);
    status.outdoor_temp = raw_to_temp(raw);
  } catch (const ModbusException&)
  {
    ok = false;
  }

  // Read indoor temp (0x0002)
  try
  {
    raw = readRegister(REG_INDOOR_TEMP);
    status.indoor_temp = raw_to_temp(raw);
  } catch (const ModbusException&)
  {
    // Non-critical, ignore
  }

  // Read leaving water temp (0x0004)
  try
  {
    raw = readRegister(REG_LEAVING_WATER_TEMP);
    status.leaving_water_temp = raw_to_temp(raw);
  } catch (const ModbusException&)
  {
    ok = false;
  }

  // Pair T1 (after EH) with Tw-out (after BPHE, before EH). Keep adjacent
  // and non-critical: an unsupported validation sensor must not affect control.
  try
  {
    (void)readRegister(REG_BEFORE_BACKUP_HEATER_TEMP);
  } catch (const ModbusException&)
  {
    // Diagnostic monitor records null; never substitute a synthetic zero.
  }
  // Read entering water temp (0x0003)
  try
  {
    raw = readRegister(REG_ENTERING_WATER_TEMP);
    status.entering_water_temp = raw_to_temp(raw);
    got_entering_water = true;
  } catch (const ModbusException&)
  {
    // Non-critical, leave as 0
  }

  // Read DHW tank temp (0x00CE)
  try
  {
    raw = readRegister(REG_DHW_TANK_TEMP);
    status.dhw_tank_temp = raw_to_temp(raw);
  } catch (const ModbusException&)
  {
    WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "Failed to read DHW tank temp");
    ok = false;
  }

  // Read running mode (0x002C)
  try
  {
    raw = readRegister(REG_RUNNING_MODE);
    status.running_mode = raw;
    // Update current_mode from the device (may have been changed externally).
    // Modes 3, 4 and 5 count too: they are what the display controller writes
    // for DHW-only and heating-only (spec F2).
    if (raw >= 0 && raw <= 5)
    {
      mCurrentMode = raw;
    }
  } catch (const ModbusException&)
  {
    ok = false;
  }

  // Read running status (0x002D)
  try
  {
    raw = readRegister(REG_RUNNING_STATUS);
    status.running_status = raw;
    status.is_running = (raw != MODE_STATUS_OFF);
  } catch (const ModbusException&)
  {
    ok = false;
  }

  // Read DHW target (0x0194)
  try
  {
    raw = readRegister(REG_DHW_TARGET);
    status.dhw_target = raw_to_temp(raw);
    if (!mSavedTargetsInitialized)
    {
      // A target sitting on its minimum is our own demand suppression
      // (heating-only pins DHW to DHW_TARGET_MIN), not a user setpoint,
      // and a value outside the writable range is a garbled frame rather
      // than a setpoint either (measured 2026-11-13: a colliding Modbus
      // exchange produced 52.0 and it became the saved target). Seed from
      // settings.ini in both cases.
      const bool suppressed = (status.dhw_target <= DHW_TARGET_MIN + 0.5f);
      const bool implausible =
          (status.dhw_target < DHW_TEMP_MIN || status.dhw_target > DHW_TEMP_MAX);
      mSavedDhwTarget =
          ((suppressed || implausible) && mMDhwTarget > 0.0f) ? mMDhwTarget : status.dhw_target;
    }
  } catch (const ModbusException&)
  {
    ok = false;
  }

  // Read the DHW setpoint of the economic sub-mode (0x0196). Optional: firmware
  // that does not answer it simply has one DHW setpoint, and 0x0194 is the whole
  // story. Not a critical read, so a missing register is not a dead device.
  try
  {
    raw = readRegister(REG_DHW_TARGET_ECO);
    status.dhw_eco_target = raw_to_temp(raw);
  } catch (const ModbusException&)
  {
    status.dhw_eco_target = 0.0f;
  }

  // Water control point (0x0033). Informational: through the 2026-11-13
  // experiments it did not follow either DHW setpoint or the heating setpoint, so
  // nothing is decided from it, but an operator wants to see what the unit thinks
  // its target is.
  try
  {
    raw = readRegister(REG_WATER_CONTROL_POINT);
    status.water_control_point = raw_to_temp(raw);
  } catch (const ModbusException&)
  {
    status.water_control_point = 0.0f;
  }

  // Read heating target (0x0191)
  try
  {
    raw = readRegister(REG_HEATING_TARGET);
    status.heating_target = raw_to_temp(raw);
    mLastHeatingTarget = status.heating_target;
    if (!mSavedTargetsInitialized)
    {
      // Same as above: DHW-only pins the heating target to
      // HEATING_TARGET_MIN, and the exit path leaves the unit in DHW
      // mode, so a floor value must not become the saved setpoint.
      const bool suppressed = (status.heating_target <= HEATING_TARGET_MIN + 0.5f);
      mSavedHeatingTarget =
          (suppressed && mMHeatingTarget > 0.0f) ? mMHeatingTarget : status.heating_target;
    }
  } catch (const ModbusException&)
  {
    status.heating_target = mLastHeatingTarget;
  }

  // Who actually has the compressor follows from the device mode. The 0x02BF
  // register is accepted by this firmware and ignored (spec F5, F6), so reading
  // it would report a preference rather than a fact - and a read fault there
  // used to mark the whole status unreadable.
  status.dhw_priority = (status.running_mode == MODE_SET_DHW_ONLY);

  // Read occupancy mode (0x0029) - 0=Away, 1=Sleep, 2=Home
  try
  {
    raw = readRegister(REG_OCCUPANCY_MODE);
    status.occupancy_mode = raw;
  } catch (const ModbusException&)
  {
    // Non-critical, leave as last known value
  }

  // Read power monitoring registers (individual reads — one failure does not zero the others)
  {
    int16_t raw;
    float ac_current = 0.0f, ac_voltage = 0.0f;
    bool got_current = false, got_voltage = false;

    try
    {
      raw = readRegister(REG_AC_CURRENT);
      // UNVERIFIED, user-approved hypothesis: raw / 2 A.
      // Contradicts the manual's *2; needs independent power-meter validation.
      // This quarters estimated AC power and quadruples COP versus the old decoder.
      status.ac_current = static_cast<float>(raw) / 2.0f;
      ac_current = status.ac_current;
      got_current = true;
    } catch (const ModbusException&)
    {
      status.ac_current = 0.0f;
    }

    try
    {
      raw = readRegister(REG_DC_CURRENT);
      status.dc_current = static_cast<float>(raw) * 4.0f; // Manual: Actual = Display × 4
    } catch (const ModbusException&)
    {
      status.dc_current = 0.0f;
    }

    try
    {
      raw = readRegister(REG_AC_VOLTAGE);
      status.ac_voltage = static_cast<float>(raw); // Manual: Actual = Display
      ac_voltage = status.ac_voltage;
      got_voltage = true;
    } catch (const ModbusException&)
    {
      status.ac_voltage = 0.0f;
    }

    try
    {
      raw = readRegister(REG_DC_VOLTAGE);
      // Polish manual p. 52: actual = displayed * 2; English p. 51
      // incorrectly says / 2. Recorded raw ~150 fits a ~300 V DC bus.
      status.dc_voltage = static_cast<float>(raw) * 2.0f;
    } catch (const ModbusException&)
    {
      status.dc_voltage = 0.0f;
    }

    // Calculate power only if both V and I were obtained
    if (got_current && got_voltage)
    {
      status.ac_power_va = ac_voltage * ac_current;                    // Apparent power (VA)
      status.ac_power_w = status.ac_power_va * ESTIMATED_POWER_FACTOR; // Estimated real power (W)
      status.power_valid = true;
    } else
    {
      status.ac_power_va = 0.0f;
      status.ac_power_w = 0.0f;
      status.power_valid = false;
    }
  }

  // Read diagnostic registers (non-critical — failures don't affect ok flag)
  try
  {
    raw = readRegister(REG_UNIT_CAPACITY);
    status.unit_capacity_kw = raw; // Raw = kW rating (4,6,8,10,12,14,16)
  } catch (const ModbusException&)
  {
    status.unit_capacity_kw = 0;
  }

  try
  {
    raw = readRegister(REG_COMPRESSOR_FREQ);
    status.compressor_freq = static_cast<float>(raw); // Raw speed, scale unverified
    status.compressor_freq_valid = true;
  } catch (const ModbusException&)
  {
    status.compressor_freq = 0.0f;
  }

  try
  {
    raw = readRegister(REG_WATER_FLOW);
    status.water_flow = static_cast<float>(raw) / 100.0f; // m³/h × 100
    got_water_flow = true;
  } catch (const ModbusException&)
  {
    status.water_flow = 0.0f;
  }

  try
  {
    raw = readRegister(REG_ACTUAL_CAPACITY_OUTPUT);
    status.actual_capacity_output = raw;
  } catch (const ModbusException&)
  {
    status.actual_capacity_output = 0;
  }

  // Read DHW valve status (0x00D2)
  try
  {
    raw = readRegister(REG_DHW_VALVE_STATUS);
    status.dhw_valve_status = raw;
  } catch (const ModbusException&)
  {
    status.dhw_valve_status = -1;
  }

  // Read DHW mode status (0x00C9) - 0=Eco, 1=Anti-Legionella, 2=Regular
  try
  {
    raw = readRegister(REG_DHW_MODE_STATUS);
    status.dhw_mode_status = raw;
  } catch (const ModbusException&)
  {
    status.dhw_mode_status = -1;
  }

  try
  {
    raw = readRegister(REG_ODU_INPUT_STATUS);
    status.odu_input_status = raw;
  } catch (const ModbusException&)
  {
    status.odu_input_status = 0;
  }

  try
  {
    raw = readRegister(REG_COMPRESSOR_RUNTIME);
    status.compressor_runtime_h = raw;
  } catch (const ModbusException&)
  {
    status.compressor_runtime_h = 0;
  }

  try
  {
    raw = readRegister(REG_PUMP_RUNTIME);
    status.pump_runtime_h = raw;
  } catch (const ModbusException&)
  {
    status.pump_runtime_h = 0;
  }

  // Calculate COP using water flow and delta-T after reading water_flow.
  // COP = heat_output / electrical_input
  // heat_output = flow × 1000/3600 × 4186 × (T_leaving - T_entering)
  // where flow is in m³/h, T in °C, result in Watts
  status.heat_output_w = 0.0f;
  status.cop = 0.0f;
  status.cop_valid = false;

  status.heat_output_valid = ok && got_entering_water && got_water_flow &&
                             status.water_flow >= 0.0f && status.leaving_water_temp > -50.0f &&
                             status.entering_water_temp > -50.0f;
  if (status.heat_output_valid)
  {
    const float delta_t = status.leaving_water_temp - status.entering_water_temp;
    // Signed heat is essential: defrost removes energy from the water circuit.
    status.heat_output_w = (status.water_flow * 1000.0f / 3600.0f) * 4186.0f * delta_t;
    if (status.power_valid && status.ac_power_w > 0.0f && delta_t > 0.1f)
    {
      status.cop = status.heat_output_w / status.ac_power_w;
      status.cop_valid = true;
    }
  }

  status.device_online = ok;
  if (ok)
  {
    mSavedTargetsInitialized = true;
    mDiagnosticMonitor.sampleOptional(*mModbusClient, nowMs());
  }
  status.diagnostics = mDiagnosticMonitor.snapshot(nowMs());
  copyScheduleMetadata(status, mScheduler.snapshot());
  return ok;
}

void ControlLoop::processCommands()
{
  if (!mCmdQueue)
    return;

  Command cmd;
  int cmd_count = 0;
  while (mCmdQueue->pop(cmd))
  {
    cmd_count++;
    WINDMI_LOG_DEBUG(LOG_TAG_CONTROLLOOP, "Processing command #%d - type=%d", cmd_count,
                     static_cast<int>(cmd.type));

    // The web thread is the sole queue producer. Scheduled intents are
    // applied directly below and therefore can never pause themselves.
    if (cmd.type == CommandType::CMD_SET_DHW_TEMP ||
        cmd.type == CommandType::CMD_SET_HEATING_TEMP ||
        cmd.type == CommandType::CMD_SET_RUNNING_MODE)
      mScheduler.pause();
    switch (cmd.type)
    {
    case CommandType::CMD_SET_DHW_TEMP:
      WINDMI_LOG_DEBUG(LOG_TAG_CONTROLLOOP, "CMD_SET_DHW_TEMP, temp=%.1f C", cmd.float_val);
      setDhwTarget(cmd.float_val);
      mSavedDhwTarget = cmd.float_val;
      break;

    case CommandType::CMD_SET_HEATING_TEMP:
      WINDMI_LOG_DEBUG(LOG_TAG_CONTROLLOOP, "CMD_SET_HEATING_TEMP, temp=%.1f C", cmd.float_val);
      setHeatingTarget(cmd.float_val);
      mLastHeatingTarget = cmd.float_val;
      mSavedHeatingTarget = cmd.float_val;
      break;

    case CommandType::CMD_SET_PRICE:
    {
      // A tariff is a display setting, so it is deliberately neither a scheduler
      // pause nor a settings-callback change: ui.working_mode and the targets stay put.
      const double cents = std::clamp(static_cast<double>(cmd.float_val),
                                      static_cast<double>(PRICE_CENTS_MIN),
                                      static_cast<double>(PRICE_CENTS_MAX));
      mPriceCentsPerKwh = cents;
      WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Tariff set to %.2f eurocents/kWh (estimate only)",
                      cents);
      if (mPricePersistCallback && !mPricePersistCallback(cents))
      {
        WINDMI_LOG_WARN(LOG_TAG_CONTROLLOOP,
                        "Tariff could not be saved; it stays in effect until the next restart");
      }
      break;
    }

    case CommandType::CMD_SET_PRIORITY:
      WINDMI_LOG_DEBUG(LOG_TAG_CONTROLLOOP, "CMD_SET_PRIORITY, pri_val=%d", cmd.int_val);
      mCurrentPriority = (cmd.int_val == 1) ? PriorityMode::Dhw : PriorityMode::Heating;
      WINDMI_LOG_WARN(
          LOG_TAG_CONTROLLOOP,
          "Priority %s noted, not written: 0x02BF has no effect on this firmware (spec F5/F6). "
          "Domestic hot water gets the compressor through device mode %d.",
          cmd.int_val == 1 ? "DHW" : "heating", MODE_SET_DHW_ONLY);
      fireSettingsCallback();
      break;

    case CommandType::CMD_SET_OCCUPANCY_MODE:
      WINDMI_LOG_DEBUG(LOG_TAG_CONTROLLOOP, "CMD_SET_OCCUPANCY_MODE, mode=%d", cmd.int_val);
      try
      {
        mModbusClient->writeRegister(REG_OCCUPANCY_MODE, static_cast<uint16_t>(cmd.int_val));
        WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Occupancy mode set to %d (0=Away, 1=Sleep, 2=Home)",
                        cmd.int_val);
      } catch (const ModbusException& e)
      {
        WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "Failed to set occupancy mode: %s", e.what());
      }
      break;

    case CommandType::CMD_SET_RUNNING_MODE:
      WINDMI_LOG_DEBUG(LOG_TAG_CONTROLLOOP, "CMD_SET_RUNNING_MODE, mode=%d", cmd.int_val);
      // The explicit command already paused scheduling above. An
      // unchanged working-mode intent must not force a native-mode
      // round trip (e.g. combined then back to DHW-priority mode 5).
      if (mDesiredWorkingMode == cmd.int_val)
      {
        fireSettingsCallback();
        break;
      }
      mDesiredWorkingMode = cmd.int_val;
      fireSettingsCallback();

      {
        const int target_device_mode = deviceModeForWorkingMode(cmd.int_val);

        if (setRunningMode(target_device_mode) == 0)
        {
          mCurrentMode = target_device_mode;
          WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Working mode set to %d (device mode=%d)",
                          cmd.int_val, target_device_mode);
        } else
        {
          WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "Failed to set running mode");
          break;
        }

        // Override target temperatures and priority based on working mode
        switch (cmd.int_val)
        {
        case 1: // DHW-only
          WINDMI_LOG_INFO(
              LOG_TAG_CONTROLLOOP,
              "DHW-only mode: native device mode %d, setpoints left as the user set them",
              MODE_SET_DHW_ONLY);
          break;
        case 2: // Heating-only
          WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP,
                          "Heating-only mode: native device mode %d, DHW setpoint left alone",
                          MODE_SET_HEAT_ONLY);
          break;
        case 3: // DHW+Heating
          WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP,
                          "DHW+Heating mode, restoring targets (DHW=%.1f, Heating=%.1f)",
                          mSavedDhwTarget, mSavedHeatingTarget);
          setDhwTarget(mSavedDhwTarget);
          setHeatingTarget(mSavedHeatingTarget);
          break;
        case 0: // Off: no target or priority changes needed
          break;
        }
      }
      break;

    default:
      WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "Unknown command type: %d", static_cast<int>(cmd.type));
      break;
    }
  }
  if (cmd_count > 0)
  {
    WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Batch complete - processed %d command(s)", cmd_count);
  }
}

void ControlLoop::updateSchedule(StatusSnapshot& status, int minute)
{
  const auto state = mScheduler.snapshot();
  copyScheduleMetadata(status, state);
  const auto selection = Scheduler::select(state, minute);
  status.schedule_active = selection.source == ScheduleSource::Period;
  status.control_source = selection.source == ScheduleSource::Period    ? "schedule"
                          : selection.source == ScheduleSource::Default ? "default"
                                                                        : "manual";
  status.schedule_pending = false;
  if (selection.source == ScheduleSource::Manual)
  {
    mScheduleWasApplying = false;
    return;
  }
  const auto& settings = selection.settings;
  const bool changed = !mScheduleWasApplying || state.revision != mLastScheduleRevision ||
                       settings.mode != mLastScheduleSettings.mode ||
                       settings.heatingTemp != mLastScheduleSettings.heatingTemp ||
                       settings.dhwTemp != mLastScheduleSettings.dhwTemp;
  if (!changed)
    return;
  status.schedule_pending = true;
  if (status.running_status == MODE_STATUS_DEFROST ||
      status.running_status == MODE_STATUS_ANTIFREEZE)
    return;
  // Never feed commands back into the SPSC queue from its consumer thread.
  // Targets precede enabling a different service; Off must not depend on an
  // unrelated target write succeeding. The arbiter remains the only automatic
  // native-mode selector, preserving DHW demand priority.
  if (settings.mode == 0)
    mDesiredWorkingMode = 0;
  const bool dhw_ok = setDhwTarget(settings.dhwTemp) == 0;
  if (dhw_ok)
  {
    mSavedDhwTarget = settings.dhwTemp;
    status.dhw_target = settings.dhwTemp;
  }
  const bool heat_ok = setHeatingTarget(settings.heatingTemp) == 0;
  if (heat_ok)
  {
    mSavedHeatingTarget = settings.heatingTemp;
    mLastHeatingTarget = settings.heatingTemp;
    status.heating_target = settings.heatingTemp;
  }
  if (!dhw_ok || !heat_ok)
    return; // Retry the same revision next normal tick.
  mDesiredWorkingMode = settings.mode;
  fireSettingsCallback();
  mLastScheduleSettings = settings;
  mScheduleWasApplying = true;
  mLastScheduleRevision = state.revision;
  status.schedule_pending = false;
  WINDMI_LOG_INFO(LOG_TAG_SCHEDULER, "Applied %s revision %llu: mode=%d, heat=%.1f, dhw=%.1f",
                  status.control_source.c_str(), static_cast<unsigned long long>(state.revision),
                  settings.mode, settings.heatingTemp, settings.dhwTemp);
}

void ControlLoop::applyControlLogic(StatusSnapshot& status)
{
  // Determine what device mode we should be in
  // ---- Demand arbiter: domestic hot water first, heating in the gaps ----
  const bool blocked = (status.running_status == MODE_STATUS_DEFROST ||
                        status.running_status == MODE_STATUS_ANTIFREEZE);
  updateTankState(status);

  ArbiterInput in;
  in.working_mode = mDesiredWorkingMode;
  in.tank = mTankState;
  in.current_mode = mCurrentMode;
  in.tank_drawing = mTankDrawing;
  in.blocked = blocked;
  mUnitActuating = (status.compressor_freq > 0.0f || status.water_flow > 0.0f);
  if (mUnitActuating)
  {
    mLastActuatingMs = nowMs();
  } else if (mLastActuatingMs == 0)
  {
    // First observation of a unit that is already idle: the idle clock starts
    // here, so a grant the unit never acts on does eventually park.
    mLastActuatingMs = nowMs();
  }
  in.dhw_granted = (mCurrentMode == MODE_SET_DHW_ONLY);
  in.unit_actuating = mUnitActuating;
  // Nothing has actuated since startup is not "idle for hours": treat an unseen
  // sample as just actuated so the first tick never releases on a stale clock.
  in.seconds_since_actuating =
      (mLastActuatingMs == 0) ? 0 : static_cast<int>((nowMs() - mLastActuatingMs) / 1000u);
  in.seconds_since_park = (mParkedAtMs == 0) ? ArbitrationConfig::kNoPark
                                             : static_cast<int>((nowMs() - mParkedAtMs) / 1000u);
  // No mode write since startup means no churn to debounce against, so the gap
  // counts as satisfied rather than as a very large or very small elapsed time.
  const bool wrote_mode_before = (mLastModeWriteMs != 0);
  in.seconds_since_last_write = wrote_mode_before
                                    ? static_cast<int>((nowMs() - mLastModeWriteMs) / 1000u)
                                    : mArbitration.min_switch_gap_s;

  // When this tick releases a declined charge, the cooldown for asking again starts
  // now. Marking it here rather than in the decision keeps decideDeviceMode() pure.
  if (chargeIsParked(mArbitration, in.dhw_granted, in.unit_actuating, in.tank_drawing,
                     in.seconds_since_actuating))
  {
    mParkedAtMs = nowMs();
  }

  const int wanted_mode = decideDeviceMode(mArbitration, in);
  // A device mode that is neither what we asked for nor what we are about to
  // ask for was set by somebody else, normally the display controller.
  const bool set_elsewhere = (mCurrentMode != mRequestedDeviceMode);
  mRequestedDeviceMode = wanted_mode;

  if (wanted_mode != mCurrentMode)
  {
    if (set_elsewhere)
    {
      WINDMI_LOG_WARN(LOG_TAG_CONTROLLOOP,
                      "Device mode %d was set elsewhere (we last asked for %d)", mCurrentMode,
                      mRequestedDeviceMode);
    }
    WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP,
                    "DHW arbiter: device mode %d -> %d (tank %.1f C vs target %.1f C, state %s, "
                    "draw %s, blocked %s, gap %d/%d s%s)",
                    mCurrentMode, wanted_mode, status.dhw_tank_temp, status.dhw_target,
                    tankStateName(mTankState), mTankDrawing ? "yes" : "no", blocked ? "yes" : "no",
                    in.seconds_since_last_write, mArbitration.min_switch_gap_s,
                    wrote_mode_before ? "" : ", none since startup");
    if (setRunningMode(wanted_mode) == 0)
    {
      mCurrentMode = wanted_mode;
    }
  }

  status.dhw_calling = (mTankState == TankState::Calling);
  status.dhw_state_known = (mTankState != TankState::Unknown);
  status.requested_device_mode = mRequestedDeviceMode;
  status.seconds_since_mode_write =
      wrote_mode_before ? static_cast<int>((nowMs() - mLastModeWriteMs) / 1000u) : -1;

  updateChargeWatchdog(status);
  status.dhw_sensor_fault_suspected = mDhwSensorFaultSuspected;
  status.tank_read_failures = mTankReadFailures;
  status.dhw_state = tankStateName(mTankState);
  status.unit_actuating = mUnitActuating;
  status.effective_dhw_target = mEffectiveDhwTargetC;

  // Protection also blocks automatic target restoration, not only mode writes.
  if (blocked)
    return;
  // Enforce target temperature overrides based on working mode
  switch (mDesiredWorkingMode)
  {
  case 1: // DHW-only: native mode 5 does the work; the user's setpoints stay
  case 2: // Heating-only: native mode 4 does the work. No setpoint pinning -
          // a pinned floor used to be read back as the user's own target.
    break;
  case 3: // DHW+Heating: ensure user's saved targets are active
  {
    // A divergent setpoint normally means the display controller changed it,
    // and we put ours back. One reading is not enough to conclude that: two
    // clients on this gateway can garble a frame, and on 2026-11-13 a garbled
    // DHW read became a real write of 52.0 C that nobody had asked for. So
    // require a plausible value, seen twice in a row.
    const bool dhw_plausible =
        (status.dhw_target >= DHW_TEMP_MIN && status.dhw_target <= DHW_TEMP_MAX);
    if (dhw_plausible && fabsf(status.dhw_target - mSavedDhwTarget) > 0.5f)
    {
      if (++mDhwDriftTicks >= kSetpointDriftConfirmTicks)
      {
        WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Restoring DHW target (%.1f, was %.1f after %d ticks)",
                        mSavedDhwTarget, status.dhw_target, mDhwDriftTicks);
        setDhwTarget(mSavedDhwTarget);
        mDhwDriftTicks = 0;
      }
    } else
    {
      mDhwDriftTicks = 0;
    }

    const bool heating_plausible =
        (status.heating_target >= HEATING_TEMP_MIN && status.heating_target <= HEATING_TEMP_MAX);
    if (heating_plausible && fabsf(status.heating_target - mSavedHeatingTarget) > 0.5f)
    {
      if (++mHeatingDriftTicks >= kSetpointDriftConfirmTicks)
      {
        WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP,
                        "Restoring heating target (%.1f, was %.1f after %d ticks)",
                        mSavedHeatingTarget, status.heating_target, mHeatingDriftTicks);
        setHeatingTarget(mSavedHeatingTarget);
        mHeatingDriftTicks = 0;
      }
    } else
    {
      mHeatingDriftTicks = 0;
    }
    break;
  }
  }

  // No priority enforcement: 0x02BF is accepted and ignored on this firmware
  // (spec F5, F6, and F7: no register moves for it on the panel). Domestic hot
  // water gets the compressor by the arbiter choosing device mode 5, and the
  // status field dhw_priority reports exactly that.
}

void ControlLoop::threadFunc()
{
  WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Thread started");

  // Publish initial status snapshot immediately so web server has data
  {
    StatusSnapshot initial_status{};
    if (readStatus(initial_status))
    {
      initial_status.device_online = true;
      initial_status.working_mode = mDesiredWorkingMode;
      updateDailyEnergy(initial_status);
      if (mStatusQueue)
      {
        if (!mStatusQueue->push(initial_status))
        {
          WINDMI_LOG_WARN(LOG_TAG_CONTROLLOOP, "Status queue full, dropping snapshot");
        }
      }
    }
  }

  // Main loop
  while (!mStopRequested.load())
  {
    auto start_time = std::chrono::steady_clock::now();

    // Check connection and reconnect if needed
    if (!mModbusClient || !mModbusClient->isConnected())
    {
      WINDMI_LOG_WARN(LOG_TAG_CONTROLLOOP, "Not connected, attempting to reconnect...");
      publishOfflineStatus();

      int retries = 0;
      while (!mStopRequested.load() && retries < MODBUS_MAX_RETRIES)
      {
        if (mModbusClient->connect())
        {
          WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Reconnected successfully");
          break;
        }
        retries++;
        publishOfflineStatus();
        windmi::UniqueLock lock(mKickMutex);
        mKickCond.wait_for(lock, MODBUS_RECONNECT_INTERVAL_S * 1000,
                           [this]() { return mStopRequested.load(); });
      }

      if (retries >= MODBUS_MAX_RETRIES)
      {
        WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "Failed to reconnect after %d attempts", retries);
        publishOfflineStatus();
        windmi::UniqueLock lock(mKickMutex);
        mKickCond.wait_for(lock, MODBUS_RECONNECT_INTERVAL_S * 1000,
                           [this]() { return mStopRequested.load(); });
        continue;
      }
    }

    // Process any pending commands
    processCommands();

    // Read current status
    StatusSnapshot status{};
    if (readStatus(status))
    {
      std::time_t now = std::time(nullptr);
      struct tm tmBuf;
#if defined(_WIN32) || defined(_WIN64)
      localtime_s(&tmBuf, &now);
#else
      localtime_r(&now, &tmBuf);
#endif
      const int test_minute = mScheduleMinuteForTesting.load();
      updateSchedule(status, test_minute >= 0 ? test_minute : tmBuf.tm_hour * 60 + tmBuf.tm_min);

      // Integrate the observed machine state, before any arbiter writes.
      updateCycleCop(status);
      // Apply control logic
      applyControlLogic(status);
      if (status.schedule_enabled && !status.schedule_paused &&
          mCurrentMode != mRequestedDeviceMode)
        status.schedule_pending = true;

      // Set working mode for status reporting
      status.working_mode = mDesiredWorkingMode;

      updateDailyEnergy(status);

      // Publish status to queue (always succeeds with ring buffer)
      status.status_sequence = ++mStatusSequence;
      if (mStatusQueue)
      {
        mStatusQueue->push(status);
      }
    } else
    {
      WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "Failed to read status");
      // Publish offline status (always succeeds with ring buffer)
      status.device_online = false;
      status.working_mode = mDesiredWorkingMode;
      updateCycleCop(status);
      updateDailyEnergy(status);
      // Offline is a fresh statement, not an old one: it advances the sequence
      // so a monitor can tell a quiet unit from a loop that stopped publishing.
      status.status_sequence = ++mStatusSequence;
      if (mStatusQueue)
      {
        mStatusQueue->push(status);
      }
    }

    // Calculate sleep time to maintain interval
    auto end_time = std::chrono::steady_clock::now();
    auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    auto sleep_ms = static_cast<long>(CONTROL_LOOP_INTERVAL_S) * 1000 - elapsed_ms;

    if (sleep_ms > 0)
    {
      windmi::UniqueLock lock(mKickMutex);
      const uint64_t observed_generation = mKickGeneration;
      mKickCond.wait_for(lock, static_cast<unsigned int>(sleep_ms), [this, observed_generation]() {
        return mStopRequested.load() || mKickGeneration != observed_generation ||
               (mCmdQueue && !mCmdQueue->empty());
      });
    }
  }

  // Disconnect
  if (mModbusClient)
  {
    mModbusClient->disconnect();
  }

  WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Thread stopped");
  mRunning.store(false);
}

void ControlLoop::setArbitration(const ArbitrationConfig& arbitration)
{
  mArbitration = arbitration;
  mArbitration.applyFloors();
  WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP,
                  "DHW arbiter: hysteresis %.1f C, switch gap %d s, draw override %.2f C/min%s",
                  mArbitration.hysteresis_c, mArbitration.min_switch_gap_s,
                  mArbitration.draw_override_c_per_min, mDryRun ? " [dry-run]" : "");
}

uint64_t ControlLoop::nowMs() const
{
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count();
  return static_cast<uint64_t>(static_cast<int64_t>(ms) + mClockOffsetMs);
}

int64_t ControlLoop::wallNowMs() const
{
  const int64_t frozen = mWallClockFrozenMs.load();
  if (frozen >= 0)
  {
    return frozen;
  }
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

void ControlLoop::updateDailyEnergy(StatusSnapshot& status)
{
  const int64_t wallNow = wallNowMs();

  // Only a fresh, valid power sample is evidence of consumption. An offline or
  // failed read says nothing about the interval, so the counter advances without
  // adding energy rather than inventing it.
  if (status.device_online && status.power_valid)
  {
    mDailyEnergy.observe(wallNow, status.ac_power_w);
  } else
  {
    mDailyEnergy.advance(wallNow);
  }

  status.daily_energy_kwh = mDailyEnergy.kwhToday();
  status.daily_energy_valid = mDailyEnergy.hasToday();
  status.price_cents_per_kwh = mPriceCentsPerKwh;

  if (!mDailyEnergy.persistDue(wallNow))
  {
    return;
  }

  const double kwh = mDailyEnergy.kwhToday();
  const std::string dayKey = mDailyEnergy.storedDayKey();
  if (mEnergyPersistCallback && mEnergyPersistCallback(kwh, dayKey))
  {
    WINDMI_LOG_DEBUG(LOG_TAG_CONTROLLOOP, "Saved daily energy estimate %.3f kWh for %s", kwh,
                     dayKey.c_str());
  } else
  {
    WINDMI_LOG_WARN(LOG_TAG_CONTROLLOOP,
                    "Daily energy estimate could not be saved; counting continues in memory");
  }
  // Marked either way: a failed save retries in an hour, not on every tick, so a
  // read-only settings file cannot turn into a warning every 30 seconds.
  mDailyEnergy.markPersisted(wallNow);
}

void ControlLoop::publishOfflineStatus()
{
  if (!mStatusQueue)
  {
    return;
  }
  // Deliberately empty apart from the two facts: a snapshot of zeros with
  // deviceOnline false is honest, one that repeats the last good temperatures is not.
  StatusSnapshot status{};
  status.device_online = false;
  status.working_mode = mDesiredWorkingMode;
  updateCycleCop(status);
  copyScheduleMetadata(status, mScheduler.snapshot());
  updateDailyEnergy(status);
  status.status_sequence = ++mStatusSequence;
  mStatusQueue->push(status);
}

void ControlLoop::updateCycleCop(StatusSnapshot& status)
{
  CyclePowerSample sample;
  if (status.running_status == MODE_STATUS_HEAT)
    sample.service = CopService::Heating;
  if (status.running_status == MODE_STATUS_DHW)
    sample.service = CopService::Dhw;
  sample.protection = status.running_status == MODE_STATUS_DEFROST ||
                      status.running_status == MODE_STATUS_ANTIFREEZE;
  sample.compressor_running = status.compressor_freq > 0;
  sample.valid = status.device_online && status.power_valid && status.heat_output_valid &&
                 status.compressor_freq_valid;
  sample.input_w = status.ac_power_w;
  sample.output_w = status.heat_output_w;
  mCycleCop.update(nowMs(), sample);
  status.heating_cycle_cop = mCycleCop.heating().cop;
  status.heating_cycle_cop_valid = mCycleCop.heating().available;
  status.dhw_cycle_cop = mCycleCop.dhw().cop;
  status.dhw_cycle_cop_valid = mCycleCop.dhw().available;
}

void ControlLoop::updateChargeWatchdog(const StatusSnapshot& status)
{
  const bool granted =
      (mCurrentMode == MODE_SET_DHW_ONLY || mRequestedDeviceMode == MODE_SET_DHW_ONLY);
  const uint64_t now = nowMs();

  const bool demanding =
      mTankState == TankState::Calling || mTankState == TankState::SatisfiedCandidate;
  if (!granted || !demanding || status.dhw_tank_temp <= 0.0f)
  {
    if (mDhwSensorFaultSuspected)
    {
      WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP,
                      "DHW charge watchdog cleared: charge satisfied or no longer granted");
    }
    mChargeStartedMs = 0;
    mChargeStartC = -1.0f;
    mDhwRunningS = 0;
    mChargeSampleMs = 0;
    mChargeWasCompressing = false;
    mDhwSensorFaultSuspected = false;
    return;
  }

  if (mChargeStartedMs == 0)
  {
    mChargeStartedMs = now;
    mChargeStartC = status.dhw_tank_temp;
  }

  // One unsatisfied charge, not the lifetime of mode 5. Pump circulation,
  // heating and defrost are not compressor minutes spent charging the tank.
  const bool compressing_dhw = status.compressor_freq_valid && status.compressor_freq > 0 &&
                               status.running_status == MODE_STATUS_DHW;
  if (compressing_dhw && mChargeWasCompressing && mChargeSampleMs != 0)
  {
    mDhwRunningS += static_cast<int>((now - mChargeSampleMs) / 1000u);
  }
  mChargeSampleMs = now;
  mChargeWasCompressing = compressing_dhw;

  if (mDhwSensorFaultSuspected)
  {
    return; // said it once; saying it every thirty seconds is noise
  }

  if (chargeIsStalled(mArbitration, mDhwRunningS, mChargeStartC, status.dhw_tank_temp))
  {
    mDhwSensorFaultSuspected = true;
    WINDMI_LOG_ERROR(
        LOG_TAG_CONTROLLOOP,
        "DHW charge watchdog: observed %d compressor min in one unsatisfied DHW charge; "
        "the tank went %.1f -> %.1f C. Low net rise may reflect hot-water use, "
        "a sensor or a charging problem. Space heating is NOT being relieved.",
        mDhwRunningS / 60, mChargeStartC, status.dhw_tank_temp);
  }
}

void ControlLoop::updateTankState(const StatusSnapshot& status)
{
  const uint64_t now = nowMs();

  // The temperature the unit is willing to charge to, which is not necessarily the
  // one the user set: see effectiveDhwTargetC().
  const float target = effectiveDhwTargetC(status.dhw_target, status.dhw_eco_target);
  mEffectiveDhwTargetC = target;

  if (status.dhw_tank_temp <= 0.0f || target <= 0.0f)
  {
    // A failed read leaves the field at zero. That is not a freezing tank, and
    // treating it as one would start a charge on every Modbus hiccup.
    if (mTankState != TankState::Unknown)
    {
      WINDMI_LOG_WARN(LOG_TAG_CONTROLLOOP, "DHW tank reading unusable; holding device mode %d",
                      mCurrentMode);
    }
    ++mTankReadFailures;
    if (mTankReadFailures == 3)
    {
      WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "DHW tank temperature unreadable for %d ticks in a row",
                       mTankReadFailures);
    }
    mTankState = TankState::Unknown;
    mTankDrawing = false;
    mTicksAtTarget = 0;
    return;
  }
  mTankReadFailures = 0;

  const double elapsed_s =
      (mLastTankSampleMs == 0) ? 0.0 : static_cast<double>(now - mLastTankSampleMs) / 1000.0;
  mTankDrawing = tankIsDrawing(mLastTankTempC, status.dhw_tank_temp, elapsed_s,
                               mArbitration.draw_override_c_per_min);
  if (mTankDrawing)
  {
    WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "DHW draw: tank %.1f -> %.1f C in %.0f s", mLastTankTempC,
                    status.dhw_tank_temp, elapsed_s);
  }

  if (status.dhw_tank_temp >= target - ArbitrationConfig::kReleaseMarginC)
  {
    ++mTicksAtTarget;
  } else
  {
    mTicksAtTarget = 0;
  }

  const TankReading reading{status.dhw_tank_temp, target, true, mTicksAtTarget};
  mTankState = advanceTankState(mTankState, mArbitration, reading);

  mLastTankTempC = status.dhw_tank_temp;
  mLastTankSampleMs = now;
}

} // namespace windmi