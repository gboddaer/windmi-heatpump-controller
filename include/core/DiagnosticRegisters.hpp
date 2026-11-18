#ifndef WINDMI_CORE_DIAGNOSTIC_REGISTERS_HPP
#define WINDMI_CORE_DIAGNOSTIC_REGISTERS_HPP

#include "config.h"
#include "modbus/IModbusClient.hpp"
#include <array>
#include <cstddef>
#include <optional>

namespace windmi {

enum class DiagnosticGroup { Existing, Alarm, Auxiliary };
struct DiagnosticRegisterSpec
{
  const char* key;
  uint16_t address;
  DiagnosticGroup group;
};

// Original installation manual, English printed pp. 50–54. Raw is deliberately
// retained: compressor/fan/EXV scaling and pressure-switch polarity are not proven.
inline constexpr DiagnosticRegisterSpec kDiagnosticRegisters[] = {
    {"compressorSpeed", REG_COMPRESSOR_FREQ, DiagnosticGroup::Existing},
    {"waterFlow", REG_WATER_FLOW, DiagnosticGroup::Existing},
    {"dhwValve", REG_DHW_VALVE_STATUS, DiagnosticGroup::Existing},
    {"dhwProfile", REG_DHW_MODE_STATUS, DiagnosticGroup::Existing},
    {"oduInputs", REG_ODU_INPUT_STATUS, DiagnosticGroup::Existing},
    {"compressorRuntime", REG_COMPRESSOR_RUNTIME, DiagnosticGroup::Existing},
    {"pumpRuntime", REG_PUMP_RUNTIME, DiagnosticGroup::Existing},
    {"capacityOutput", REG_ACTUAL_CAPACITY_OUTPUT, DiagnosticGroup::Existing},
    {"unitCapacity", REG_UNIT_CAPACITY, DiagnosticGroup::Existing},
    {"leavingWater", REG_LEAVING_WATER_TEMP, DiagnosticGroup::Existing},
    {"beforeHeater", REG_BEFORE_BACKUP_HEATER_TEMP, DiagnosticGroup::Existing},
    {"enteringWater", REG_ENTERING_WATER_TEMP, DiagnosticGroup::Existing},
    {"tankTemperature", REG_DHW_TANK_TEMP, DiagnosticGroup::Existing},
    {"dhwTarget", REG_DHW_TARGET, DiagnosticGroup::Existing},
    {"ecoDhwTarget", REG_DHW_TARGET_ECO, DiagnosticGroup::Existing},
    {"heatingTarget", REG_HEATING_TARGET, DiagnosticGroup::Existing},
    {"waterControlPoint", REG_WATER_CONTROL_POINT, DiagnosticGroup::Existing},
    {"acCurrent", REG_AC_CURRENT, DiagnosticGroup::Existing},
    {"dcCurrent", REG_DC_CURRENT, DiagnosticGroup::Existing},
    {"acVoltage", REG_AC_VOLTAGE, DiagnosticGroup::Existing},
    {"dcVoltage", REG_DC_VOLTAGE, DiagnosticGroup::Existing},
    {"setMode", REG_RUNNING_MODE, DiagnosticGroup::Existing},
    {"operation", REG_RUNNING_STATUS, DiagnosticGroup::Existing},
    {"alarm1", 0x1009, DiagnosticGroup::Alarm},
    {"alarm2", 0x100A, DiagnosticGroup::Alarm},
    {"alarm3", 0x100B, DiagnosticGroup::Alarm},
    {"alarm4", 0x100C, DiagnosticGroup::Alarm},
    {"pumpCommand", 0x0055, DiagnosticGroup::Auxiliary},
    {"oduOutputs", 0x100E, DiagnosticGroup::Auxiliary},
    {"compressorTarget", 0x100F, DiagnosticGroup::Auxiliary},
    {"fanUpperTarget", 0x101A, DiagnosticGroup::Auxiliary},
    {"fanLowerTarget", 0x101B, DiagnosticGroup::Auxiliary},
    {"exvCommand", 0x101C, DiagnosticGroup::Auxiliary},
    {"fanUpper", 0x101D, DiagnosticGroup::Auxiliary},
    {"fanLower", 0x101E, DiagnosticGroup::Auxiliary},
    {"ipmReason", 0x1022, DiagnosticGroup::Auxiliary},
    // Configuration, not current heater state. Read only here; no setters.
    {"backupType", 0x0259, DiagnosticGroup::Auxiliary},
    {"backupWarmup", 0x025A, DiagnosticGroup::Auxiliary},
    {"backupDelta", 0x025B, DiagnosticGroup::Auxiliary},
    {"backupOatThreshold", 0x025C, DiagnosticGroup::Auxiliary},
    {"minHeatingOat", 0x0202, DiagnosticGroup::Auxiliary},
};
inline constexpr size_t kDiagnosticRegisterCount =
    sizeof(kDiagnosticRegisters) / sizeof(kDiagnosticRegisters[0]);
inline constexpr size_t diagnosticGroupCount(DiagnosticGroup group)
{
  size_t count = 0;
  for (const auto& spec : kDiagnosticRegisters)
    if (spec.group == group)
      ++count;
  return count;
}
inline constexpr int diagnosticMaxAgeSeconds(DiagnosticGroup group)
{
  return group == DiagnosticGroup::Existing
             ? 90
             : static_cast<int>(diagnosticGroupCount(group) * 30 + 60);
}
inline size_t diagnosticIndex(uint16_t address)
{
  for (size_t i = 0; i < kDiagnosticRegisterCount; ++i)
    if (kDiagnosticRegisters[i].address == address)
      return i;
  return kDiagnosticRegisterCount;
}
struct DiagnosticReading
{
  std::optional<uint16_t> raw;
  uint64_t sampled_at_ms = 0;
};
struct DiagnosticSnapshot
{
  std::array<DiagnosticReading, kDiagnosticRegisterCount> readings{};
  uint64_t observed_at_ms = 0;
};

class DiagnosticMonitor
{
public:
  void beginSample()
  {
    for (size_t i = 0; i < kDiagnosticRegisterCount; ++i)
      if (kDiagnosticRegisters[i].group == DiagnosticGroup::Existing)
        mState.readings[i] = {};
  }
  int16_t readRegister(IModbusClient& client, uint16_t address, uint64_t now_ms)
  {
    const size_t index = diagnosticIndex(address);
    if (index < kDiagnosticRegisterCount)
      mState.readings[index] = {{}, now_ms};
    const int16_t value = client.readRegister(address); // failure stays null
    if (index < kDiagnosticRegisterCount)
      mState.readings[index].raw = static_cast<uint16_t>(value);
    return value;
  }
  void sampleOptional(IModbusClient& client, uint64_t now_ms)
  {
    // Keep a stable deadline: register-read jitter must not turn a normal
    // 30-second tick into a skipped poll. Skip missed periods without a burst.
    if (!mPolled)
    {
      mNextPollMs = now_ms;
      mPolled = true;
    }
    if (now_ms < mNextPollMs)
      return;
    mNextPollMs += ((now_ms - mNextPollMs) / 30000 + 1) * 30000;
    sampleNext(client, DiagnosticGroup::Alarm, mAlarmCursor, now_ms);
    sampleNext(client, DiagnosticGroup::Auxiliary, mAuxiliaryCursor, now_ms);
  }
  DiagnosticSnapshot snapshot(uint64_t now_ms) const
  {
    auto result = mState;
    result.observed_at_ms = now_ms;
    return result;
  }

private:
  void sampleNext(IModbusClient& client, DiagnosticGroup group, size_t& cursor, uint64_t now_ms)
  {
    for (size_t n = 0; n < kDiagnosticRegisterCount; ++n)
    {
      const size_t i = (cursor + n) % kDiagnosticRegisterCount;
      if (kDiagnosticRegisters[i].group != group)
        continue;
      cursor = (i + 1) % kDiagnosticRegisterCount;
      try
      {
        readRegister(client, kDiagnosticRegisters[i].address, now_ms);
      } catch (const ModbusException&)
      { /* Unsupported is not a device fault. */
      }
      return;
    }
  }
  DiagnosticSnapshot mState;
  size_t mAlarmCursor = 0, mAuxiliaryCursor = 0;
  bool mPolled = false;
  uint64_t mNextPollMs = 0;
};
} // namespace windmi
#endif
