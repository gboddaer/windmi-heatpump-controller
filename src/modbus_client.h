#ifndef MODBUS_CLIENT_H
#define MODBUS_CLIENT_H

#include <stdint.h>
#include <stdbool.h>

typedef struct modbus_client modbus_client_t;

modbus_client_t *modbus_client_create(const char *ip, int port, uint8_t slave_id);
void modbus_client_destroy(modbus_client_t *client);
bool modbus_client_connect(modbus_client_t *client);
void modbus_client_disconnect(modbus_client_t *client);
bool modbus_client_is_connected(modbus_client_t *client);

// Flush any pending data in the socket read buffer
void modbus_client_flush_buffer(modbus_client_t *client);

// Read a single holding register. Returns 0 on success, -1 on error.
// Value is signed (int16_t) to handle negative temperatures.
int modbus_read_register(modbus_client_t *client, uint16_t address, int16_t *value);

// Write a single register with read-back verification.
// Returns 0 on success and verified, -1 on error or verification failure.
int modbus_write_register(modbus_client_t *client, uint16_t address, uint16_t value);

// Read multiple holding registers. Returns 0 on success, -1 on error.
int modbus_read_registers(modbus_client_t *client, uint16_t address,
                          int16_t *values, uint16_t count);

#endif // MODBUS_CLIENT_H
