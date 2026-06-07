/**
 * @file src/core/ControlLoop.cpp
 * @brief Control loop implementation matching master branch control_loop.c
 */

#include "core/ControlLoop.hpp"
#include "modbus/IModbusClient.hpp"
#include "utils/Logger.hpp"
#include "utils/LogTags.hpp"
#include "config.h"

#include <cstring>
#include <cmath>
#include <chrono>

namespace windmi {

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

  // Validate: only 0, 1, 2 are valid device modes
  if (mode < 0 || mode > 2)
  {
    WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP,
                     "Invalid device mode %d: only 0 (Off), 1 (Cool+DHW), 2 (Heat+DHW) supported",
                     mode);
    return -1;
  }

  try
  {
    mModbusClient->writeRegister(REG_RUNNING_MODE, static_cast<uint16_t>(mode));
    WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Set running mode to %d", mode);
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

  int16_t raw_temp = temp_to_raw(temp);
  try
  {
    mModbusClient->writeRegister(REG_DHW_TARGET, static_cast<uint16_t>(raw_temp));
    WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Set DHW target to %.1f C", temp);
    return 0;
  } catch (const ModbusException& e)
  {
    WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "Failed to set DHW target to %.1f: %s", temp, e.what());
    return -1;
  }
}

int ControlLoop::setHeatingTarget(float temp)
{
  if (!mModbusClient)
    return -1;

  int16_t raw_temp = temp_to_raw(temp);
  try
  {
    mModbusClient->writeRegister(REG_HEATING_TARGET, static_cast<uint16_t>(raw_temp));
    WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Set heating target to %.1f C", temp);
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
  int16_t raw;

  // Read outdoor temp (0x0001)
  try
  {
    raw = mModbusClient->readRegister(REG_OUTDOOR_TEMP);
    status.outdoor_temp = raw_to_temp(raw);
  } catch (const ModbusException&)
  {
    ok = false;
  }

  // Read indoor temp (0x0002)
  try
  {
    raw = mModbusClient->readRegister(REG_INDOOR_TEMP);
    status.indoor_temp = raw_to_temp(raw);
  } catch (const ModbusException&)
  {
    // Non-critical, ignore
  }

  // Read leaving water temp (0x0004)
  try
  {
    raw = mModbusClient->readRegister(REG_LEAVING_WATER_TEMP);
    status.leaving_water_temp = raw_to_temp(raw);
  } catch (const ModbusException&)
  {
    ok = false;
  }

  // Read entering water temp (0x0003)
  try
  {
    raw = mModbusClient->readRegister(REG_ENTERING_WATER_TEMP);
    status.entering_water_temp = raw_to_temp(raw);
  } catch (const ModbusException&)
  {
    // Non-critical, leave as 0
  }

  // Read DHW tank temp (0x00CE)
  try
  {
    raw = mModbusClient->readRegister(REG_DHW_TANK_TEMP);
    status.dhw_tank_temp = raw_to_temp(raw);
  } catch (const ModbusException&)
  {
    WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "Failed to read DHW tank temp");
    ok = false;
  }

  // Read running mode (0x002C)
  try
  {
    raw = mModbusClient->readRegister(REG_RUNNING_MODE);
    status.running_mode = raw;
    // Update current_mode from the device (may have been changed externally)
    if (raw == 0 || raw == 1 || raw == 2)
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
    raw = mModbusClient->readRegister(REG_RUNNING_STATUS);
    status.running_status = raw;
    status.is_running = (raw != MODE_STATUS_OFF);
  } catch (const ModbusException&)
  {
    ok = false;
  }

  // Read DHW target (0x0194)
  try
  {
    raw = mModbusClient->readRegister(REG_DHW_TARGET);
    status.dhw_target = raw_to_temp(raw);
    if (!mSavedTargetsInitialized)
    {
      mSavedDhwTarget = status.dhw_target;
    }
  } catch (const ModbusException&)
  {
    ok = false;
  }

  // Read heating target (0x0191)
  try
  {
    raw = mModbusClient->readRegister(REG_HEATING_TARGET);
    status.heating_target = raw_to_temp(raw);
    mLastHeatingTarget = status.heating_target;
    if (!mSavedTargetsInitialized)
    {
      mSavedHeatingTarget = status.heating_target;
    }
  } catch (const ModbusException&)
  {
    status.heating_target = mLastHeatingTarget;
  }

  // Read DHW priority (0x02BF) - only for status reporting, not for control
  try
  {
    raw = mModbusClient->readRegister(REG_DHW_PRIORITY);
    status.dhw_priority = (raw != 0);
    // Note: mCurrentPriority is managed by applyControlLogic(), not read from device
    // to prevent external changes from overriding the desired working mode priority
  } catch (const ModbusException&)
  {
    ok = false;
  }

  // Read power monitoring registers (individual reads — one failure does not zero the others)
  {
    int16_t raw;
    float ac_current = 0.0f, ac_voltage = 0.0f;
    bool got_current = false, got_voltage = false;

    try
    {
      raw = mModbusClient->readRegister(REG_AC_CURRENT);
      status.ac_current = static_cast<float>(raw) * 2.0f; // Manual: Actual = Display × 2
      ac_current = status.ac_current;
      got_current = true;
    } catch (const ModbusException&)
    {
      status.ac_current = 0.0f;
    }

    try
    {
      raw = mModbusClient->readRegister(REG_DC_CURRENT);
      status.dc_current = static_cast<float>(raw) * 4.0f; // Manual: Actual = Display × 4
    } catch (const ModbusException&)
    {
      status.dc_current = 0.0f;
    }

    try
    {
      raw = mModbusClient->readRegister(REG_AC_VOLTAGE);
      status.ac_voltage = static_cast<float>(raw); // Manual: Actual = Display
      ac_voltage = status.ac_voltage;
      got_voltage = true;
    } catch (const ModbusException&)
    {
      status.ac_voltage = 0.0f;
    }

    try
    {
      raw = mModbusClient->readRegister(REG_DC_VOLTAGE);
      status.dc_voltage = static_cast<float>(raw) / 2.0f; // Manual: Actual = Display / 2
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
    raw = mModbusClient->readRegister(REG_UNIT_CAPACITY);
    status.unit_capacity_kw = raw; // Raw = kW rating (4,6,8,10,12,14,16)
  } catch (const ModbusException&)
  {
    status.unit_capacity_kw = 0;
  }

  try
  {
    raw = mModbusClient->readRegister(REG_COMPRESSOR_FREQ);
    status.compressor_freq = static_cast<float>(raw); // 1 Hz units
  } catch (const ModbusException&)
  {
    status.compressor_freq = 0.0f;
  }

  try
  {
    raw = mModbusClient->readRegister(REG_WATER_FLOW);
    status.water_flow = static_cast<float>(raw) / 100.0f; // m³/h × 100
  } catch (const ModbusException&)
  {
    status.water_flow = 0.0f;
  }

  try
  {
    raw = mModbusClient->readRegister(REG_ACTUAL_CAPACITY_OUTPUT);
    status.actual_capacity_output = raw;
  } catch (const ModbusException&)
  {
    status.actual_capacity_output = 0;
  }

  try
  {
    raw = mModbusClient->readRegister(REG_ODU_INPUT_STATUS);
    status.odu_input_status = raw;
  } catch (const ModbusException&)
  {
    status.odu_input_status = 0;
  }

  try
  {
    raw = mModbusClient->readRegister(REG_COMPRESSOR_RUNTIME);
    status.compressor_runtime_h = raw;
  } catch (const ModbusException&)
  {
    status.compressor_runtime_h = 0;
  }

  try
  {
    raw = mModbusClient->readRegister(REG_PUMP_RUNTIME);
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

  if (status.water_flow > 0.01f && status.power_valid && status.leaving_water_temp > 0.0f &&
      status.entering_water_temp > -50.0f)
  {
    float delta_t = status.leaving_water_temp - status.entering_water_temp;
    if (delta_t > 0.1f)
    {
      // flow_m3h / 3600 = m³/s → × 1000 = kg/s, × 4186 J/(kg·K) = W/K
      float flow_kg_s = (status.water_flow * 1000.0f) / 3600.0f;
      status.heat_output_w = flow_kg_s * 4186.0f * delta_t;
      if (status.ac_power_w > 0.0f)
      {
        status.cop = status.heat_output_w / status.ac_power_w;
        status.cop_valid = true;
      }
    }
  }

  status.device_online = ok;
  if (ok)
  {
    mSavedTargetsInitialized = true;
  }
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

    case CommandType::CMD_SET_PRIORITY:
      WINDMI_LOG_DEBUG(LOG_TAG_CONTROLLOOP, "CMD_SET_PRIORITY, pri_val=%d", cmd.int_val);
      try
      {
        mModbusClient->writeRegister(REG_DHW_PRIORITY, static_cast<uint16_t>(cmd.int_val));
        mCurrentPriority = (cmd.int_val == 1) ? PriorityMode::Dhw : PriorityMode::Heating;
        WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Priority set to %s",
                        cmd.int_val == 1 ? "DHW" : "Heating");
      } catch (const ModbusException& e)
      {
        WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "Failed to set priority register: %s", e.what());
      }
      break;

    case CommandType::CMD_SET_RUNNING_MODE:
      WINDMI_LOG_DEBUG(LOG_TAG_CONTROLLOOP, "CMD_SET_RUNNING_MODE, mode=%d", cmd.int_val);
      mDesiredWorkingMode = cmd.int_val;

      {
        int target_device_mode;
        switch (cmd.int_val)
        {
        case 0:
          target_device_mode = MODE_SET_OFF;
          break;
        case 1:
          target_device_mode = MODE_SET_HEAT_DHW;
          break;
        case 2:
          target_device_mode = MODE_SET_HEAT_DHW;
          break;
        case 3:
          target_device_mode = MODE_SET_HEAT_DHW;
          break;
        default:
          target_device_mode = MODE_SET_HEAT_DHW;
          break;
        }

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
          WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP,
                          "DHW-only mode, setting heating target to min (%.1f)",
                          static_cast<float>(HEATING_TARGET_MIN));
          setHeatingTarget(HEATING_TARGET_MIN);
          try
          {
            mModbusClient->writeRegister(REG_DHW_PRIORITY, 1);
            mCurrentPriority = PriorityMode::Dhw;
            WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "DHW-only mode, set DHW priority");
          } catch (const ModbusException& e)
          {
            WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "Failed to set DHW priority: %s", e.what());
          }
          break;
        case 2: // Heating-only
          WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP,
                          "Heating-only mode, setting DHW target to min (%.1f)",
                          static_cast<float>(DHW_TARGET_MIN));
          setDhwTarget(DHW_TARGET_MIN);
          try
          {
            mModbusClient->writeRegister(REG_DHW_PRIORITY, 0);
            mCurrentPriority = PriorityMode::Heating;
            WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Heating-only mode, cleared DHW priority");
          } catch (const ModbusException& e)
          {
            WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "Failed to clear DHW priority: %s", e.what());
          }
          break;
        case 3: // DHW+Heating
          WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP,
                          "DHW+Heating mode, restoring targets (DHW=%.1f, Heating=%.1f)",
                          mSavedDhwTarget, mSavedHeatingTarget);
          setDhwTarget(mSavedDhwTarget);
          setHeatingTarget(mSavedHeatingTarget);
          try
          {
            mModbusClient->writeRegister(REG_DHW_PRIORITY, 1);
            mCurrentPriority = PriorityMode::Dhw;
            WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "DHW+Heating mode, set DHW priority");
          } catch (const ModbusException& e)
          {
            WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "Failed to set DHW priority: %s", e.what());
          }
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

void ControlLoop::applyControlLogic(StatusSnapshot& status)
{
  // Determine what device mode we should be in
  int desired_device_mode;
  switch (mDesiredWorkingMode)
  {
  case 0:
    desired_device_mode = MODE_SET_OFF;
    break;
  case 1:
    desired_device_mode = MODE_SET_HEAT_DHW;
    break;
  case 2:
    desired_device_mode = MODE_SET_HEAT_DHW;
    break;
  case 3:
    desired_device_mode = MODE_SET_HEAT_DHW;
    break;
  default:
    desired_device_mode = MODE_SET_HEAT_DHW;
    break;
  }

  // Only change mode if it doesn't match
  if (mCurrentMode != desired_device_mode)
  {
    WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP,
                    "Correcting device mode from %d to %d (desired working mode=%d)", mCurrentMode,
                    desired_device_mode, mDesiredWorkingMode);
    if (setRunningMode(desired_device_mode) == 0)
    {
      mCurrentMode = desired_device_mode;
    }
  }

  // Enforce target temperature overrides based on working mode
  switch (mDesiredWorkingMode)
  {
  case 1: // DHW-only: keep heating target at minimum
    if (status.heating_target > HEATING_TARGET_MIN + 0.5f)
    {
      WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "DHW-only enforcing heating target min (%.1f, was %.1f)",
                      static_cast<float>(HEATING_TARGET_MIN), status.heating_target);
      setHeatingTarget(HEATING_TARGET_MIN);
    }
    break;
  case 2: // Heating-only: keep DHW target at minimum
    if (status.dhw_target > DHW_TARGET_MIN + 0.5f)
    {
      WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Heating-only enforcing DHW target min (%.1f, was %.1f)",
                      static_cast<float>(DHW_TARGET_MIN), status.dhw_target);
      setDhwTarget(DHW_TARGET_MIN);
    }
    break;
  case 3: // DHW+Heating: ensure user's saved targets are active
    if (fabsf(status.dhw_target - mSavedDhwTarget) > 0.5f)
    {
      WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Restoring DHW target (%.1f, was %.1f)",
                      mSavedDhwTarget, status.dhw_target);
      setDhwTarget(mSavedDhwTarget);
    }
    if (fabsf(status.heating_target - mSavedHeatingTarget) > 0.5f)
    {
      WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Restoring heating target (%.1f, was %.1f)",
                      mSavedHeatingTarget, status.heating_target);
      setHeatingTarget(mSavedHeatingTarget);
    }
    break;
  }

  // Enforce priority based on working mode
  WINDMI_LOG_DEBUG(
      LOG_TAG_CONTROLLOOP, "Enforcing priority for working_mode=%d, current_priority=%s",
      mDesiredWorkingMode, mCurrentPriority == PriorityMode::Dhw ? "Dhw" : "Heating");
  switch (mDesiredWorkingMode)
  {
  case 1: // DHW-only: must have DHW priority
  case 3: // DHW+Heating: must have DHW priority
    if (mCurrentPriority != PriorityMode::Dhw)
    {
      WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Mode %d enforcing DHW priority", mDesiredWorkingMode);
      try
      {
        mModbusClient->writeRegister(REG_DHW_PRIORITY, 1);
        mCurrentPriority = PriorityMode::Dhw;
        WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Priority now set to DHW");
      } catch (const ModbusException& e)
      {
        WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "Failed to set DHW priority: %s", e.what());
      }
    }
    break;
  case 2: // Heating-only: must have no DHW priority
    if (mCurrentPriority != PriorityMode::Heating)
    {
      WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Heating-only mode clearing DHW priority");
      try
      {
        mModbusClient->writeRegister(REG_DHW_PRIORITY, 0);
        mCurrentPriority = PriorityMode::Heating;
        WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Priority now set to Heating");
      } catch (const ModbusException& e)
      {
        WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "Failed to clear DHW priority: %s", e.what());
      }
    }
    break;
  }
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

      int retries = 0;
      while (!mStopRequested.load() && retries < MODBUS_MAX_RETRIES)
      {
        if (mModbusClient->connect())
        {
          WINDMI_LOG_INFO(LOG_TAG_CONTROLLOOP, "Reconnected successfully");
          break;
        }
        retries++;
        windmi::UniqueLock lock(mKickMutex);
        mKickCond.wait_for(lock, MODBUS_RECONNECT_INTERVAL_S * 1000,
                            [this]() { return mStopRequested.load(); });
      }

      if (retries >= MODBUS_MAX_RETRIES)
      {
        WINDMI_LOG_ERROR(LOG_TAG_CONTROLLOOP, "Failed to reconnect after %d attempts", retries);
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
      // Apply control logic
      applyControlLogic(status);

      // Set working mode for status reporting
      status.working_mode = mDesiredWorkingMode;

      // Publish status to queue (always succeeds with ring buffer)
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

} // namespace windmi