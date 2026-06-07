/**
 * @file src/modbus/SimulatedModbusClient.cpp
 * @brief Simulated Modbus client for demo mode
 */

#include "modbus/SimulatedModbusClient.hpp"
#include "config.h"

#include <cstdio>

namespace windmi {

SimulatedModbusClient::SimulatedModbusClient()
    : mConnected(false), mAmbientTemp(200) // 20.0 C
      ,
      mLastError("No error")
{
  // Initialize with known register values matching real device
  mRegisters[REG_DEVICE_TYPE] = 8;                   // Rotenso Windmi 8kW
  mRegisters[REG_OUTDOOR_TEMP] = 120;                // 12.0 C
  mRegisters[REG_INDOOR_TEMP] = 210;                 // 21.0 C
  mRegisters[REG_ENTERING_WATER_TEMP] = 320;         // 32.0 C
  mRegisters[REG_LEAVING_WATER_TEMP] = 350;          // 35.0 C
  mRegisters[REG_RUNNING_MODE] = MODE_SET_HEAT_DHW;  // Heat+DHW mode
  mRegisters[REG_RUNNING_STATUS] = MODE_STATUS_HEAT; // Heating
  mRegisters[REG_DHW_TARGET] = 460;                  // 46.0 C
  mRegisters[REG_HEATING_TARGET] = 450;              // 45.0 C
  mRegisters[REG_DHW_TANK_TEMP] = 420;               // 42.0 C
  mRegisters[REG_DHW_PRIORITY] = 1;                  // DHW priority
  mRegisters[REG_AC_CURRENT] = 3;                    // 1.5 A (raw / 2)
  mRegisters[REG_DC_CURRENT] = 2;                    // 0.5 A (raw / 4)
  mRegisters[REG_AC_VOLTAGE] = 230;                  // 230 V
  mRegisters[REG_DC_VOLTAGE] = 700;                  // 350 V (raw * 2)

  mLastUpdate = std::chrono::steady_clock::now();
}

bool SimulatedModbusClient::connect()
{
  windmi::LockGuard lock(mMutex);
  mConnected = true;
  mLastError = "No error";
  return true;
}

void SimulatedModbusClient::disconnect()
{
  windmi::LockGuard lock(mMutex);
  mConnected = false;
  mLastError = "Not connected";
}

bool SimulatedModbusClient::isConnected() const
{
  windmi::LockGuard lock(mMutex);
  return mConnected;
}

int16_t SimulatedModbusClient::readRegister(uint16_t address)
{
  windmi::LockGuard lock(mMutex);

  if (!mConnected)
  {
    mLastError = "Not connected";
    throw ModbusException("Not connected");
  }

  updateSimulationLocked();

  auto it = mRegisters.find(address);
  if (it == mRegisters.end())
  {
    mLastError = "Unknown register";
    throw ModbusException("Unknown register: " + std::to_string(address));
  }

  mLastError = "No error";
  return it->second;
}

void SimulatedModbusClient::writeRegister(uint16_t address, uint16_t value)
{
  windmi::LockGuard lock(mMutex);

  if (!mConnected)
  {
    mLastError = "Not connected";
    throw ModbusException("Not connected");
  }

  updateSimulationLocked();

  // Store the value (demo is permissive — accept all register writes)
  mRegisters[address] = static_cast<int16_t>(value);

  mLastError = "No error";
}

void SimulatedModbusClient::flushBuffer()
{
  // No-op for simulated client
}

std::string SimulatedModbusClient::getLastError() const
{
  windmi::LockGuard lock(mMutex);
  return mLastError;
}

void SimulatedModbusClient::updateSimulationLocked()
{
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration<double>(now - mLastUpdate).count();
  mLastUpdate = now;

  if (elapsed < 0.1)
    return; // Don't update too frequently

  // Read current mode
  int running_mode = mRegisters[REG_RUNNING_MODE];

  // Simulate based on running mode
  if (running_mode == MODE_SET_OFF)
  {
    // OFF mode: temperatures drift toward ambient
    int16_t leaving = mRegisters[REG_LEAVING_WATER_TEMP];
    int16_t dhw = mRegisters[REG_DHW_TANK_TEMP];
    int16_t indoor = mRegisters[REG_INDOOR_TEMP];
    int16_t outdoor = mRegisters[REG_OUTDOOR_TEMP];

    // Slow drift toward ambient (20 C = 200)
    int16_t drift = static_cast<int16_t>(elapsed * 20); // ~20 units per second

    if (leaving > mAmbientTemp)
    {
      leaving = (leaving - drift < mAmbientTemp) ? mAmbientTemp : (leaving - drift);
    } else
    {
      leaving = (leaving + drift > mAmbientTemp) ? mAmbientTemp : (leaving + drift);
    }

    if (dhw > mAmbientTemp)
    {
      dhw = (dhw - drift < mAmbientTemp) ? mAmbientTemp : (dhw - drift);
    } else
    {
      dhw = (dhw + drift > mAmbientTemp) ? mAmbientTemp : (dhw + drift);
    }

    if (indoor > mAmbientTemp)
    {
      indoor = (indoor - drift < mAmbientTemp) ? mAmbientTemp : (indoor - drift);
    } else
    {
      indoor = (indoor + drift > mAmbientTemp) ? mAmbientTemp : (indoor + drift);
    }

    // Outdoor temp drifts slightly slower
    if (outdoor > mAmbientTemp)
    {
      outdoor = (outdoor - static_cast<int16_t>(drift / 2) < mAmbientTemp)
                    ? mAmbientTemp
                    : outdoor - static_cast<int16_t>(drift / 2);
    } else
    {
      outdoor = (outdoor + static_cast<int16_t>(drift / 2) > mAmbientTemp)
                    ? mAmbientTemp
                    : outdoor + static_cast<int16_t>(drift / 2);
    }

    mRegisters[REG_LEAVING_WATER_TEMP] = leaving;
    mRegisters[REG_DHW_TANK_TEMP] = dhw;
    mRegisters[REG_INDOOR_TEMP] = indoor;
    mRegisters[REG_OUTDOOR_TEMP] = outdoor;

    mRegisters[REG_RUNNING_STATUS] = MODE_STATUS_OFF;
  } else if (running_mode == MODE_SET_HEAT_DHW || running_mode == MODE_SET_COOL_DHW)
  {
    // Heating or cooling+DHW mode: leaving water approaches target
    int16_t leaving = mRegisters[REG_LEAVING_WATER_TEMP];
    int16_t target = mRegisters[REG_HEATING_TARGET];

    // Move 20% of the way toward target per second
    int16_t diff = target - leaving;
    int16_t step = static_cast<int16_t>(elapsed * diff * 0.2);

    leaving += step;
    mRegisters[REG_LEAVING_WATER_TEMP] = leaving;

    // Set status based on mode
    if (running_mode == MODE_SET_HEAT_DHW)
    {
      mRegisters[REG_RUNNING_STATUS] = MODE_STATUS_HEAT;
    } else
    {
      mRegisters[REG_RUNNING_STATUS] = MODE_STATUS_COOL;
    }
  }

  // Update power readings based on current status
  int running_status = mRegisters[REG_RUNNING_STATUS];
  if (running_status == MODE_STATUS_OFF)
  {
    mRegisters[REG_AC_CURRENT] = 0;
    mRegisters[REG_DC_CURRENT] = 0;
  }
}

} // namespace windmi
