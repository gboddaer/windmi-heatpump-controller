/**
 * @file src/modbus/ModbusClient.cpp
 * @brief Modbus client C++ wrapper implementation
 */

#include "modbus/ModbusClient.hpp"
#include "config.h"

#include <cstdio>

extern "C" {
#include "modbus_client.h"
}

namespace windmi {

struct ModbusClient::Impl
{
  modbus_client_t* client;

  Impl() : client{nullptr}
  {}

  ~Impl()
  {
    disconnect();
  }

  void disconnect()
  {
    if (client)
    {
      modbus_client_disconnect(client);
      modbus_client_destroy(client);
      client = nullptr;
    }
  }
};

ModbusClient::ModbusClient(const std::string& host, int port, uint8_t slave_id)
{
  mImpl = std::make_unique<Impl>();

  mImpl->client = modbus_client_create(host.c_str(), port, slave_id);
  if (!mImpl->client)
  {
    throw ModbusException("Failed to create Modbus client");
  }
}

ModbusClient::~ModbusClient()
{
  disconnect();
}

bool ModbusClient::connect()
{
  if (!mImpl || !mImpl->client)
    return false;
  return modbus_client_connect(mImpl->client);
}

void ModbusClient::disconnect()
{
  if (mImpl)
  {
    mImpl->disconnect();
  }
}

bool ModbusClient::isConnected() const
{
  if (!mImpl || !mImpl->client)
    return false;
  return modbus_client_is_connected(mImpl->client);
}

int16_t ModbusClient::readRegister(uint16_t address)
{
  if (!mImpl || !mImpl->client)
  {
    throw ModbusException("Not connected");
  }

  int16_t value;
  if (modbus_read_register(mImpl->client, address, &value) != 0)
  {
    throw ModbusException("Failed to read register");
  }
  return value;
}

void ModbusClient::writeRegister(uint16_t address, uint16_t value)
{
  if (!mImpl || !mImpl->client)
  {
    throw ModbusException("Not connected");
  }

  // modbus_write_register returns 0 on success, -1 on error
  if (modbus_write_register(mImpl->client, address, value) != 0)
  {
    throw ModbusException("Failed to write register");
  }
}

void ModbusClient::flushBuffer()
{
  if (mImpl && mImpl->client)
  {
    modbus_client_flush_buffer(mImpl->client);
  }
}

std::string ModbusClient::getLastError() const
{
  return "No error";
}

} // namespace windmi
