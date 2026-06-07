/**
 * @file src/modbus/SimulatedModbusClient.cpp
 * @brief Simulated Modbus client for demo mode
 */

#include "modbus/SimulatedModbusClient.hpp"
#include "config.h"

#include <cstdio>

namespace windmi {

SimulatedModbusClient::SimulatedModbusClient()
    : connected_(false), ambient_temp_(200) // 20.0 C
      ,
      last_error_("No error")
{
  // Initialize with known register values matching real device
  registers_[REG_DEVICE_TYPE] = 8;                   // Rotenso Windmi 8kW
  registers_[REG_OUTDOOR_TEMP] = 120;                // 12.0 C
  registers_[REG_INDOOR_TEMP] = 210;                 // 21.0 C
  registers_[REG_ENTERING_WATER_TEMP] = 320;         // 32.0 C
  registers_[REG_LEAVING_WATER_TEMP] = 350;          // 35.0 C
  registers_[REG_RUNNING_MODE] = MODE_SET_HEAT_DHW;  // Heat+DHW mode
  registers_[REG_RUNNING_STATUS] = MODE_STATUS_HEAT; // Heating
  registers_[REG_DHW_TARGET] = 460;                  // 46.0 C
  registers_[REG_HEATING_TARGET] = 450;              // 45.0 C
  registers_[REG_DHW_TANK_TEMP] = 420;               // 42.0 C
  registers_[REG_DHW_PRIORITY] = 1;                  // DHW priority
  registers_[REG_AC_CURRENT] = 3;                    // 1.5 A (raw / 2)
  registers_[REG_DC_CURRENT] = 2;                    // 0.5 A (raw / 4)
  registers_[REG_AC_VOLTAGE] = 230;                  // 230 V
  registers_[REG_DC_VOLTAGE] = 700;                  // 350 V (raw * 2)

  last_update_ = std::chrono::steady_clock::now();
}

bool SimulatedModbusClient::connect()
{
  windmi::LockGuard lock(mutex_);
  connected_ = true;
  last_error_ = "No error";
  return true;
}

void SimulatedModbusClient::disconnect()
{
  windmi::LockGuard lock(mutex_);
  connected_ = false;
  last_error_ = "Not connected";
}

bool SimulatedModbusClient::isConnected() const
{
  windmi::LockGuard lock(mutex_);
  return connected_;
}

int16_t SimulatedModbusClient::readRegister(uint16_t address)
{
  windmi::LockGuard lock(mutex_);

  if (!connected_)
  {
    last_error_ = "Not connected";
    throw ModbusException("Not connected");
  }

  updateSimulationLocked();

  auto it = registers_.find(address);
  if (it == registers_.end())
  {
    last_error_ = "Unknown register";
    throw ModbusException("Unknown register: " + std::to_string(address));
  }

  last_error_ = "No error";
  return it->second;
}

void SimulatedModbusClient::writeRegister(uint16_t address, uint16_t value)
{
  windmi::LockGuard lock(mutex_);

  if (!connected_)
  {
    last_error_ = "Not connected";
    throw ModbusException("Not connected");
  }

  updateSimulationLocked();

  // Store the value (demo is permissive — accept all register writes)
  registers_[address] = static_cast<int16_t>(value);

  last_error_ = "No error";
}

void SimulatedModbusClient::flushBuffer()
{
  // No-op for simulated client
}

std::string SimulatedModbusClient::getLastError() const
{
  windmi::LockGuard lock(mutex_);
  return last_error_;
}

void SimulatedModbusClient::updateSimulationLocked()
{
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration<double>(now - last_update_).count();
  last_update_ = now;

  if (elapsed < 0.1)
    return; // Don't update too frequently

  // Read current mode
  int running_mode = registers_[REG_RUNNING_MODE];

  // Simulate based on running mode
  if (running_mode == MODE_SET_OFF)
  {
    // OFF mode: temperatures drift toward ambient
    int16_t leaving = registers_[REG_LEAVING_WATER_TEMP];
    int16_t dhw = registers_[REG_DHW_TANK_TEMP];
    int16_t indoor = registers_[REG_INDOOR_TEMP];
    int16_t outdoor = registers_[REG_OUTDOOR_TEMP];

    // Slow drift toward ambient (20 C = 200)
    int16_t drift = static_cast<int16_t>(elapsed * 20); // ~20 units per second

    if (leaving > ambient_temp_)
    {
      leaving = (leaving - drift < ambient_temp_) ? ambient_temp_ : (leaving - drift);
    } else
    {
      leaving = (leaving + drift > ambient_temp_) ? ambient_temp_ : (leaving + drift);
    }

    if (dhw > ambient_temp_)
    {
      dhw = (dhw - drift < ambient_temp_) ? ambient_temp_ : (dhw - drift);
    } else
    {
      dhw = (dhw + drift > ambient_temp_) ? ambient_temp_ : (dhw + drift);
    }

    if (indoor > ambient_temp_)
    {
      indoor = (indoor - drift < ambient_temp_) ? ambient_temp_ : (indoor - drift);
    } else
    {
      indoor = (indoor + drift > ambient_temp_) ? ambient_temp_ : (indoor + drift);
    }

    // Outdoor temp drifts slightly slower
    if (outdoor > ambient_temp_)
    {
      outdoor = (outdoor - static_cast<int16_t>(drift / 2) < ambient_temp_)
                    ? ambient_temp_
                    : outdoor - static_cast<int16_t>(drift / 2);
    } else
    {
      outdoor = (outdoor + static_cast<int16_t>(drift / 2) > ambient_temp_)
                    ? ambient_temp_
                    : outdoor + static_cast<int16_t>(drift / 2);
    }

    registers_[REG_LEAVING_WATER_TEMP] = leaving;
    registers_[REG_DHW_TANK_TEMP] = dhw;
    registers_[REG_INDOOR_TEMP] = indoor;
    registers_[REG_OUTDOOR_TEMP] = outdoor;

    registers_[REG_RUNNING_STATUS] = MODE_STATUS_OFF;
  } else if (running_mode == MODE_SET_HEAT_DHW || running_mode == MODE_SET_COOL_DHW)
  {
    // Heating or cooling+DHW mode: leaving water approaches target
    int16_t leaving = registers_[REG_LEAVING_WATER_TEMP];
    int16_t target = registers_[REG_HEATING_TARGET];

    // Move 20% of the way toward target per second
    int16_t diff = target - leaving;
    int16_t step = static_cast<int16_t>(elapsed * diff * 0.2);

    leaving += step;
    registers_[REG_LEAVING_WATER_TEMP] = leaving;

    // Set status based on mode
    if (running_mode == MODE_SET_HEAT_DHW)
    {
      registers_[REG_RUNNING_STATUS] = MODE_STATUS_HEAT;
    } else
    {
      registers_[REG_RUNNING_STATUS] = MODE_STATUS_COOL;
    }
  }

  // Update power readings based on current status
  int running_status = registers_[REG_RUNNING_STATUS];
  if (running_status == MODE_STATUS_OFF)
  {
    registers_[REG_AC_CURRENT] = 0;
    registers_[REG_DC_CURRENT] = 0;
  }
}

} // namespace windmi
