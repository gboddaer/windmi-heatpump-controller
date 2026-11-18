/**
 * @file src/modbus/ModbusSerialClient.cpp
 * @brief Modbus RTU over serial (RS-485) C++ wrapper implementation
 */

#include "modbus/ModbusSerialClient.hpp"
#include "utils/LogTags.hpp"
#include "utils/Logger.hpp"
#include "config.h"
#include <cstring>
#include <sstream>

namespace windmi {

struct ModbusSerialClient::Impl
{
  modbus_serial_client_t* c_client;
  std::string last_error;

  ~Impl()
  {
    if (c_client)
    {
      modbus_serial_client_destroy(c_client);
      c_client = nullptr;
    }
  }
};

ModbusSerialClient::ModbusSerialClient(const std::string& device, int baud, char parity,
                                       int stop_bits, bool rs485_enabled, uint8_t slave_id)
    : mImpl(new Impl())
{

  // Validate baud rate
  if (baud != 9600 && baud != 19200 && baud != 38400 && baud != 57600 && baud != 115200)
  {
    std::ostringstream oss;
    oss << "Invalid baud rate: " << baud;
    throw ModbusException(oss.str());
  }

  // Validate parity (accept both uppercase and lowercase)
  if ((parity != 'N' && parity != 'E' && parity != 'O') &&
      (parity != 'n' && parity != 'e' && parity != 'o'))
  {
    std::ostringstream oss;
    oss << "Invalid parity: '" << parity << "' (must be N, E, or O)";
    throw ModbusException(oss.str());
  }
  // Normalize to uppercase
  parity = toupper((unsigned char)parity);

  // Validate stop bits
  if (stop_bits != 1 && stop_bits != 2)
  {
    std::ostringstream oss;
    oss << "Invalid stop bits: " << stop_bits << " (must be 1 or 2)";
    throw ModbusException(oss.str());
  }

  mImpl->c_client =
      modbus_serial_client_create(device.c_str(), baud, parity, stop_bits, rs485_enabled, slave_id);

  if (!mImpl->c_client)
  {
    throw ModbusException("Failed to create Modbus serial client");
  }
}

ModbusSerialClient::~ModbusSerialClient()
{
  if (mImpl)
  {
    delete mImpl;
    mImpl = nullptr;
  }
}

bool ModbusSerialClient::connect()
{
  if (!mImpl || !mImpl->c_client)
  {
    mImpl->last_error = "Client is not initialized";
    return false;
  }

  bool result = modbus_serial_client_connect(mImpl->c_client);
  if (!result)
  {
    mImpl->last_error = "Failed to connect to serial port";
    WINDMI_LOG_ERROR(LOG_TAG_MODBUS, "%s", mImpl->last_error.c_str());
  }
  return result;
}

void ModbusSerialClient::disconnect()
{
  if (mImpl && mImpl->c_client)
  {
    modbus_serial_client_disconnect(mImpl->c_client);
  }
}

bool ModbusSerialClient::isConnected() const
{
  return mImpl && mImpl->c_client && modbus_serial_client_is_connected(mImpl->c_client);
}

int16_t ModbusSerialClient::readRegister(uint16_t address)
{
  if (!mImpl || !mImpl->c_client)
  {
    throw ModbusException("Client is not initialized");
  }

  int16_t value;
  int result = modbus_serial_read_register(mImpl->c_client, address, &value);

  if (result != 0)
  {
    mImpl->last_error = "Failed to read register";
    WINDMI_LOG_ERROR(LOG_TAG_MODBUS, "readRegister(0x%04X) failed", address);
    throw ModbusException(mImpl->last_error);
  }

  return value;
}

void ModbusSerialClient::writeRegister(uint16_t address, uint16_t value)
{
  if (!mImpl || !mImpl->c_client)
  {
    throw ModbusException("Client is not initialized");
  }

  int result = modbus_serial_write_register(mImpl->c_client, address, value);

  if (result != 0)
  {
    mImpl->last_error = "Failed to write register";
    WINDMI_LOG_ERROR(LOG_TAG_MODBUS, "writeRegister(0x%04X, %u) failed", address, value);
    throw ModbusException(mImpl->last_error);
  }
}

void ModbusSerialClient::flushBuffer()
{
  if (mImpl && mImpl->c_client)
  {
    modbus_serial_flush_buffer(mImpl->c_client);
  }
}

std::string ModbusSerialClient::getLastError() const
{
  if (mImpl)
  {
    return mImpl->last_error;
  }
  return "Client is not initialized";
}

} // namespace windmi
