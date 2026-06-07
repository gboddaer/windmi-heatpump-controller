/**
 * @file modbus/ModbusSerialClient.hpp
 * @brief Modbus RTU over serial (RS-485) C++ wrapper
 *
 * This provides a C++ wrapper around the C modbus_serial_client_t.
 * It implements the IModbusClient interface for use with ControlLoop.
 *
 * Usage:
 *   ModbusSerialClient client("/dev/ttyUSB0", 9600, 'N', 1, false, 11);
 *   client.connect();
 *   int16_t temp = client.readRegister(REG_OUTDOOR_TEMP);
 */

#ifndef WINDMI_MODBUS_MODBUSSERIALCLIENT_HPP_
#define WINDMI_MODBUS_MODBUSSERIALCLIENT_HPP_

#include "modbus/IModbusClient.hpp"
#include <cstdint>
#include <stdexcept>
#include <string>

extern "C" {
#include "modbus/modbus_serial_client.h"
}

namespace windmi {

/**
 * @brief Modbus RTU client over serial port
 *
 * Implements IModbusClient interface using serial transport (RS-485).
 * Default configuration: 9600 8N1 (Windmi default).
 *
 * @note This class is not copyable.
 */
class ModbusSerialClient : public IModbusClient
{
public:
  /**
   * @brief Create a new Modbus serial client
   *
   * @param device Serial device path (e.g., "/dev/ttyUSB0")
   * @param baud Baud rate (9600, 19200, 38400, 57600, 115200)
   * @param parity Parity character: 'N' (none), 'E' (even), 'O' (odd)
   * @param stop_bits Stop bits: 1 or 2 (default: 1)
   * @param rs485_enabled Enable RS-485 direction control (default: false)
   * @param slave_id Modbus slave ID (0-255)
   * @throws ModbusException on invalid parameters
   */
  ModbusSerialClient(const std::string& device, int baud, char parity, int stop_bits,
                     bool rs485_enabled, uint8_t slave_id);

  /**
   * @brief Destroy the Modbus serial client
   */
  ~ModbusSerialClient() override;

  // Non-copyable
  ModbusSerialClient(const ModbusSerialClient&) = delete;
  ModbusSerialClient& operator=(const ModbusSerialClient&) = delete;

  // IModbusClient interface implementation
  bool connect() override;
  void disconnect() override;
  bool isConnected() const override;

  int16_t readRegister(uint16_t address) override;
  void writeRegister(uint16_t address, uint16_t value) override;

  void flushBuffer() override;
  std::string getLastError() const override;

private:
  struct Impl;
  Impl* impl_;
};

} // namespace windmi

#endif // WINDMI_MODBUS_MODBUSSERIALCLIENT_HPP
