/**
 * @file modbus/SimulatedModbusClient.hpp
 * @brief Simulated Modbus client for demo mode
 *
 * This client stores register values in memory and simulates heat pump behavior.
 * It never opens a socket and is used when --demo flag is specified.
 */

#ifndef WINDMI_MODBUS_SIMULATED_MODBUS_CLIENT_HPP_
#define WINDMI_MODBUS_SIMULATED_MODBUS_CLIENT_HPP_

#include <cstdint>
#include <unordered_map>
#include <chrono>
#include <string>

#include "modbus/IModbusClient.hpp"
#include "utils/Platform.hpp"

namespace windmi {

/**
 * @brief Simulated Modbus client implementation
 *
 * Stores register values in memory and simulates basic heat pump behavior:
 * - Temperature drift toward ambient values when OFF
 * - Leaving water approaches heating target when heating is active
 * - DHW tank approaches target when DHW is active
 */
class SimulatedModbusClient : public IModbusClient
{
public:
  SimulatedModbusClient();

  // IModbusClient interface
  bool connect() override;
  void disconnect() override;
  bool isConnected() const override;

  int16_t readRegister(uint16_t address) override;
  void writeRegister(uint16_t address, uint16_t value) override;

  void flushBuffer() override;
  std::string getLastError() const override;

private:
  void updateSimulationLocked();

  // Register storage
  std::unordered_map<uint16_t, int16_t> registers_;

  // Connection state
  bool connected_;

  // Simulation state
  mutable windmi::Mutex mutex_;
  std::chrono::steady_clock::time_point last_update_;

  // Ambient temperature for drift simulation (degrees C * 10)
  int16_t ambient_temp_;

  // Last error message
  mutable std::string last_error_;
};

} // namespace windmi

#endif // WINDMI_MODBUS_SIMULATED_MODBUS_CLIENT_HPP
