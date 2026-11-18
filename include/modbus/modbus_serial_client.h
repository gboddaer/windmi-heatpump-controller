/**
 * @file modbus_serial_client.h
 * @brief Modbus RTU over serial (RS-485) client
 *
 * This provides a Modbus client implementation that communicates over a serial port
 * (e.g., /dev/ttyUSB0, /dev/ttyS0) using Modbus RTU protocol.
 *
 * Default configuration: 9600 8N1 (Windmi default)
 * Other common configurations: 9600 8N2, 19200 8N1, etc.
 *
 * Note: RS-485 direction control requires explicit --rs485 flag.
 * Most USB-RS485 adapters handle direction automatically in hardware.
 */

#ifndef WINDMI_MODBUS_SERIAL_CLIENT_H_
#define WINDMI_MODBUS_SERIAL_CLIENT_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Modbus serial client handle
 */
typedef struct modbus_serial_client modbus_serial_client_t;

/**
 * Parity configuration
 */
typedef enum {
  MODBUS_SERIAL_PARITY_NONE = 'N', // 8N1 - Windmi default
  MODBUS_SERIAL_PARITY_EVEN = 'E', // 8E1
  MODBUS_SERIAL_PARITY_ODD = 'O'   // 8O1
} modbus_serial_parity_t;

/**
 * Create a new Modbus serial client
 *
 * @param device Serial device path (e.g., "/dev/ttyUSB0")
 * @param baud Baud rate (e.g., 9600, 19200, 38400, 57600, 115200)
 * @param parity Parity: 'N' (none), 'E' (even), 'O' (odd)
 * @param stop_bits Stop bits: 1 or 2 (default: 1)
 * @param rs485_enabled Enable RS-485 direction control (default: false)
 * @param slave_id Modbus slave ID (0-255)
 * @return Client handle, or NULL on error (invalid params, device open fails)
 */
modbus_serial_client_t* modbus_serial_client_create(const char* device, int baud, char parity,
                                                    int stop_bits, bool rs485_enabled,
                                                    uint8_t slave_id);

/**
 * Destroy a Modbus serial client
 *
 * @param client Client handle (may be NULL)
 */
void modbus_serial_client_destroy(modbus_serial_client_t* client);

/**
 * Connect to the serial device
 *
 * @param client Client handle
 * @return true on success, false on failure
 */
bool modbus_serial_client_connect(modbus_serial_client_t* client);

/**
 * Disconnect from the serial device
 *
 * @param client Client handle
 */
void modbus_serial_client_disconnect(modbus_serial_client_t* client);

/**
 * Check if client is connected
 *
 * @param client Client handle
 * @return true if connected, false otherwise
 */
bool modbus_serial_client_is_connected(modbus_serial_client_t* client);

/**
 * Read a single holding register
 *
 * @param client Client handle
 * @param address Register address (0-65535)
 * @param value Pointer to store read value (signed 16-bit)
 * @return 0 on success, -1 on error
 */
int modbus_serial_read_register(modbus_serial_client_t* client, uint16_t address, int16_t* value);

/**
 * Write a single register with read-back verification
 *
 * @param client Client handle
 * @param address Register address (0-65535)
 * @param value Value to write (0-65535)
 * @return 0 on success and verified, -1 on error or verification failure
 */
int modbus_serial_write_register(modbus_serial_client_t* client, uint16_t address, uint16_t value);

/**
 * Read multiple holding registers
 *
 * @param client Client handle
 * @param address Starting register address (0-65535)
 * @param values Pointer to store read values (array of signed 16-bit)
 * @param count Number of registers to read (1-125)
 * @return 0 on success, -1 on error
 */
int modbus_serial_read_registers(modbus_serial_client_t* client, uint16_t address, int16_t* values,
                                 uint16_t count);

/**
 * Flush any pending data in the serial read buffer
 *
 * @param client Client handle
 */
void modbus_serial_flush_buffer(modbus_serial_client_t* client);

#ifdef __cplusplus
}
#endif

#endif /* MODBUS_SERIAL_CLIENT_H */
