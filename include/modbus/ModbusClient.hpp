/**
 * @file modbus/ModbusClient.hpp
 * @brief C++ wrapper for Modbus client
 */

#ifndef WINDMI_MODBUS_MODBUS_CLIENT_HPP_
#define WINDMI_MODBUS_MODBUS_CLIENT_HPP_

#include <cstdint>
#include <string>
#include <memory>
#include <stdexcept>

// Include the C Modbus client header
extern "C" {
#include "modbus_client.h"
}

#include "modbus/IModbusClient.hpp"

namespace windmi {

/**
 * @brief Modbus client class
 *
 * C++ wrapper around the C Modbus client implementation.
 * Provides RAII pattern and better error handling.
 */
class ModbusClient : public IModbusClient
{
public:
  /**
   * @brief Constructor
   * @param host Hostname or IP address
   * @param port Port number
   * @param slave_id Modbus slave ID
   */
  ModbusClient(const std::string& host, int port, uint8_t slave_id);

  /**
   * @brief Destructor
   */
  ~ModbusClient();

  /**
   * @brief Copy constructor (deleted)
   */
  ModbusClient(const ModbusClient&) = delete;

  /**
   * @brief Assignment operator (deleted)
   */
  ModbusClient& operator=(const ModbusClient&) = delete;

  // IModbusClient interface
  bool connect() override;
  void disconnect() override;
  bool isConnected() const override;

  int16_t readRegister(uint16_t address) override;
  void writeRegister(uint16_t address, uint16_t value) override;

  void flushBuffer() override;
  std::string getLastError() const override;

private:
  struct Impl;
  std::unique_ptr<Impl> mImpl;
};

} // namespace windmi

#endif // WINDMI_MODBUS_MODBUS_CLIENT_HPP
