/**
 * @file modbus/IModbusClient.hpp
 * @brief Abstract interface for Modbus client
 *
 * Both real and simulated Modbus clients implement this interface.
 * ControlLoop depends on this interface, not concrete types.
 */

#ifndef WINDMI_MODBUS_IMODBUSCLIENT_HPP_
#define WINDMI_MODBUS_IMODBUSCLIENT_HPP_

#include <cstdint>
#include <stdexcept>
#include <string>

namespace windmi {

/**
 * @brief Exception thrown for Modbus communication errors
 */
class ModbusException : public std::runtime_error
{
public:
  explicit ModbusException(const std::string& msg) : std::runtime_error(msg)
  {}
};

/**
 * @brief Abstract interface for Modbus client operations
 *
 * Both real (socket-based) and simulated (in-memory) clients implement this.
 * ControlLoop depends on this interface, not concrete types.
 */
class IModbusClient
{
public:
  virtual ~IModbusClient() = default;

  virtual bool connect() = 0;
  virtual void disconnect() = 0;
  virtual bool isConnected() const = 0;

  virtual int16_t readRegister(uint16_t address) = 0;
  virtual void writeRegister(uint16_t address, uint16_t value) = 0;

  virtual void flushBuffer() = 0;
  virtual std::string getLastError() const = 0;
};

} // namespace windmi

#endif // WINDMI_MODBUS_IMODBUSCLIENT_HPP
