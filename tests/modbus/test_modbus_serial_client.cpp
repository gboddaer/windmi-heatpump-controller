/**
 * @file tests/modbus/test_modbus_serial_client.cpp
 * @brief Modbus Serial Client unit tests
 */

#include <gtest/gtest.h>
#include "modbus/ModbusSerialClient.hpp"
#include "config.h"

using namespace windmi;

/**
 * Test creating a serial client with valid parameters
 */
TEST(ModbusSerialClientTest, CreateClient)
{
  ModbusSerialClient client("/dev/ttyUSB0", 9600, 'N', 1, false, 1);
  // Client should be created but not connected
}

/**
 * Test that client is not connected initially
 */
TEST(ModbusSerialClientTest, NotConnectedInitially)
{
  ModbusSerialClient client("/dev/ttyUSB0", 9600, 'N', 1, false, 1);
  EXPECT_FALSE(client.isConnected());
}

/**
 * Test exception on read when not connected
 */
TEST(ModbusSerialClientTest, ExceptionOnReadWhenNotConnected)
{
  ModbusSerialClient client("/dev/ttyUSB0", 9600, 'N', 1, false, 1);
  EXPECT_THROW(client.readRegister(0), ModbusException);
}

/**
 * Test exception on write when not connected
 */
TEST(ModbusSerialClientTest, ExceptionOnWriteWhenNotConnected)
{
  ModbusSerialClient client("/dev/ttyUSB0", 9600, 'N', 1, false, 1);
  EXPECT_THROW(client.writeRegister(0, 0), ModbusException);
}

/**
 * Test destructor does not crash
 */
TEST(ModbusSerialClientTest, DestructorDoesNotCrash)
{
  {
    ModbusSerialClient client("/dev/ttyUSB0", 9600, 'N', 1, false, 1);
  }
  // Should not crash
}

/**
 * Test constructor with different valid parameters
 */
TEST(ModbusSerialClientTest, ValidBaudRates)
{
  EXPECT_NO_THROW(ModbusSerialClient("/dev/ttyUSB0", 9600, 'N', 1, false, 1));
  EXPECT_NO_THROW(ModbusSerialClient("/dev/ttyUSB0", 19200, 'N', 1, false, 1));
  EXPECT_NO_THROW(ModbusSerialClient("/dev/ttyUSB0", 38400, 'N', 1, false, 1));
  EXPECT_NO_THROW(ModbusSerialClient("/dev/ttyUSB0", 57600, 'N', 1, false, 1));
  EXPECT_NO_THROW(ModbusSerialClient("/dev/ttyUSB0", 115200, 'N', 1, false, 1));
}

/**
 * Test constructor with different parity settings
 */
TEST(ModbusSerialClientTest, ValidParitySettings)
{
  EXPECT_NO_THROW(ModbusSerialClient("/dev/ttyUSB0", 9600, 'N', 1, false, 1));
  EXPECT_NO_THROW(ModbusSerialClient("/dev/ttyUSB0", 9600, 'E', 1, false, 1));
  EXPECT_NO_THROW(ModbusSerialClient("/dev/ttyUSB0", 9600, 'O', 1, false, 1));
}

/**
 * Test constructor with different stop bit settings
 */
TEST(ModbusSerialClientTest, ValidStopBits)
{
  EXPECT_NO_THROW(ModbusSerialClient("/dev/ttyUSB0", 9600, 'N', 1, false, 1));
  EXPECT_NO_THROW(ModbusSerialClient("/dev/ttyUSB0", 9600, 'N', 2, false, 1));
}

/**
 * Test constructor with RS-485 enabled
 */
TEST(ModbusSerialClientTest, RS485Enabled)
{
  EXPECT_NO_THROW(ModbusSerialClient("/dev/ttyUSB0", 9600, 'N', 1, true, 1));
}

/**
 * Test constructor with Windmi slave ID (11 = 0x0B)
 */
TEST(ModbusSerialClientTest, WindmiSlaveId)
{
  EXPECT_NO_THROW(ModbusSerialClient("/dev/ttyUSB0", 9600, 'N', 1, false, 11));
}

/**
 * Test constructor throws exception on invalid baud rate
 */
TEST(ModbusSerialClientTest, InvalidBaudRate)
{
  EXPECT_THROW(ModbusSerialClient("/dev/ttyUSB0", 0, 'N', 1, false, 1), ModbusException);
  EXPECT_THROW(ModbusSerialClient("/dev/ttyUSB0", -1, 'N', 1, false, 1), ModbusException);
  EXPECT_THROW(ModbusSerialClient("/dev/ttyUSB0", 99999, 'N', 1, false, 1), ModbusException);
  EXPECT_THROW(ModbusSerialClient("/dev/ttyUSB0", 4800, 'N', 1, false, 1), ModbusException);
  EXPECT_THROW(ModbusSerialClient("/dev/ttyUSB0", 7200, 'N', 1, false, 1), ModbusException);
}

/**
 * Test constructor throws exception on invalid parity
 */
TEST(ModbusSerialClientTest, InvalidParity)
{
  EXPECT_THROW(ModbusSerialClient("/dev/ttyUSB0", 9600, 'X', 1, false, 1), ModbusException);
  EXPECT_THROW(ModbusSerialClient("/dev/ttyUSB0", 9600, 'M', 1, false, 1), ModbusException);
  EXPECT_THROW(ModbusSerialClient("/dev/ttyUSB0", 9600, 'S', 1, false, 1), ModbusException);
}

/**
 * Test constructor throws exception on invalid stop bits
 */
TEST(ModbusSerialClientTest, InvalidStopBits)
{
  EXPECT_THROW(ModbusSerialClient("/dev/ttyUSB0", 9600, 'N', 0, false, 1), ModbusException);
  EXPECT_THROW(ModbusSerialClient("/dev/ttyUSB0", 9600, 'N', 3, false, 1), ModbusException);
  EXPECT_THROW(ModbusSerialClient("/dev/ttyUSB0", 9600, 'N', -1, false, 1), ModbusException);
}

/**
 * Test non-copyable
 */
TEST(ModbusSerialClientTest, NonCopyable)
{
  ModbusSerialClient client("/dev/ttyUSB0", 9600, 'N', 1, false, 1);
  // Test compilation - these should not compile if copyable:
  // ModbusSerialClient client2 = client;
  // ModbusSerialClient client3(client);
}

// ============= C API Tests =============

extern "C" {
#include "modbus/modbus_serial_client.h"
}

/**
 * Test C API: create and destroy serial client
 */
TEST(ModbusSerialClientTest, CClientCreateDestroy)
{
  modbus_serial_client_t* client =
      modbus_serial_client_create("/dev/ttyUSB0", 9600, 'N', 1, false, 1);
  ASSERT_NE(nullptr, client);
  EXPECT_FALSE(modbus_serial_client_is_connected(client));
  modbus_serial_client_destroy(client);
}

/**
 * Test C API: invalid baud rate returns NULL
 */
TEST(ModbusSerialClientTest, CClientInvalidBaud)
{
  EXPECT_EQ(nullptr, modbus_serial_client_create("/dev/ttyUSB0", 0, 'N', 1, false, 1));
  EXPECT_EQ(nullptr, modbus_serial_client_create("/dev/ttyUSB0", -1, 'N', 1, false, 1));
  EXPECT_EQ(nullptr, modbus_serial_client_create("/dev/ttyUSB0", 99999, 'N', 1, false, 1));
  EXPECT_EQ(nullptr, modbus_serial_client_create("/dev/ttyUSB0", 4800, 'N', 1, false, 1));
  EXPECT_EQ(nullptr, modbus_serial_client_create("/dev/ttyUSB0", 7200, 'N', 1, false, 1));
}

/**
 * Test C API: invalid parity returns NULL
 */
TEST(ModbusSerialClientTest, CClientInvalidParity)
{
  EXPECT_EQ(nullptr, modbus_serial_client_create("/dev/ttyUSB0", 9600, 'X', 1, false, 1));
  EXPECT_EQ(nullptr, modbus_serial_client_create("/dev/ttyUSB0", 9600, 'M', 1, false, 1));
  EXPECT_EQ(nullptr, modbus_serial_client_create("/dev/ttyUSB0", 9600, 'S', 1, false, 1));
}

/**
 * Test C API: invalid stop bits returns NULL
 */
TEST(ModbusSerialClientTest, CClientInvalidStopBits)
{
  EXPECT_EQ(nullptr, modbus_serial_client_create("/dev/ttyUSB0", 9600, 'N', 0, false, 1));
  EXPECT_EQ(nullptr, modbus_serial_client_create("/dev/ttyUSB0", 9600, 'N', 3, false, 1));
  EXPECT_EQ(nullptr, modbus_serial_client_create("/dev/ttyUSB0", 9600, 'N', -1, false, 1));
}
