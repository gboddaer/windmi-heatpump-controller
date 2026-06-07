/**
 * @file modbus_serial_client.c
 * @brief Modbus RTU over serial client protocol implementation
 *
 * Serial I/O is delegated to windmi::platform::SerialPort via PlatformC.h.
 */

#include "modbus/modbus_serial_client.h"
#include "modbus/modbus_rtu_frame.h"
#include "crc16.h"
#include "utils/LogTags.hpp"
#include "utils/LoggerC.h"
#include "utils/PlatformC.h"
#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MODBUS_SERIAL_WRITE_MAX_RETRIES 3
#define MODBUS_SERIAL_RECV_TIMEOUT_MS 2000
#define MODBUS_SERIAL_RETRY_DELAY_MS 100
#define MODBUS_SERIAL_INTER_FRAME_DELAY_MS 4
#define MODBUS_SERIAL_MAX_FRAME 256
#define MODBUS_SERIAL_READ_HEADER_LEN 3

struct modbus_serial_client {
    char device[256];
    int baud;
    char parity;
    int stop_bits;
    bool rs485_enabled;
    uint8_t slave_id;
    WindmiSerialPort *port;
    bool connected;
};

static uint16_t bytes_to_uint16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static int serial_send_frame(modbus_serial_client_t *client, const uint8_t *frame, size_t len);
static int serial_receive_exact(modbus_serial_client_t *client, uint8_t *buffer, size_t expected_len, int timeout_ms);
static int modbus_serial_read_write_registers_internal(modbus_serial_client_t *client,
                                                        uint16_t address,
                                                        int16_t *values,
                                                        uint16_t count,
                                                        bool single);

modbus_serial_client_t *modbus_serial_client_create(
    const char *device,
    int baud,
    char parity,
    int stop_bits,
    bool rs485_enabled,
    uint8_t slave_id
) {
    if (!device) {
        WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "Serial client: device path is NULL");
        return NULL;
    }

    if (strlen(device) >= sizeof(((modbus_serial_client_t*)0)->device)) {
        WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "Serial client: device path too long");
        return NULL;
    }

    int valid_bauds[] = {9600, 19200, 38400, 57600, 115200};
    bool baud_valid = false;
    for (size_t i = 0; i < sizeof(valid_bauds) / sizeof(valid_bauds[0]); i++) {
        if (baud == valid_bauds[i]) {
            baud_valid = true;
            break;
        }
    }
    if (!baud_valid) {
        WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "Serial client: invalid baud rate %d", baud);
        return NULL;
    }

    if ((parity != 'N' && parity != 'E' && parity != 'O') &&
        (parity != 'n' && parity != 'e' && parity != 'o')) {
        WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "Serial client: invalid parity '%c'", parity);
        return NULL;
    }
    parity = (char)toupper((unsigned char)parity);

    if (stop_bits != 1 && stop_bits != 2) {
        WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS,
            "Serial client: invalid stop bits %d (must be 1 or 2)", stop_bits);
        return NULL;
    }

    modbus_serial_client_t *client = (modbus_serial_client_t *)malloc(sizeof(modbus_serial_client_t));
    if (!client) {
        WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "Serial client: malloc failed");
        return NULL;
    }

    strncpy(client->device, device, sizeof(client->device) - 1);
    client->device[sizeof(client->device) - 1] = '\0';
    client->baud = baud;
    client->parity = parity;
    client->stop_bits = stop_bits;
    client->rs485_enabled = rs485_enabled;
    client->slave_id = slave_id;
    client->port = NULL;
    client->connected = false;

    WINDMI_C_LOG(WINDMI_LOG_INFO, LOG_TAG_MODBUS,
        "Serial client created: device=%s, baud=%d, parity=%c, stop_bits=%d, rs485=%s, slave_id=%d",
        client->device, client->baud, client->parity, client->stop_bits,
        client->rs485_enabled ? "enabled" : "disabled", client->slave_id);

    return client;
}

void modbus_serial_client_destroy(modbus_serial_client_t *client) {
    if (client) {
        if (client->connected) {
            modbus_serial_client_disconnect(client);
        } else if (client->port) {
            windmi_serial_close(client->port);
            client->port = NULL;
        }
        free(client);
    }
}

bool modbus_serial_client_connect(modbus_serial_client_t *client) {
    if (!client || client->connected) return false;

    client->port = windmi_serial_open(client->device, client->baud, client->parity,
                                      client->stop_bits, client->rs485_enabled);
    if (!client->port) {
        WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS,
            "Failed to open serial device %s", client->device);
        return false;
    }

    windmi_serial_flush(client->port);
    client->connected = true;

    WINDMI_C_LOG(WINDMI_LOG_INFO, LOG_TAG_MODBUS,
        "Serial client connected: %s @ %d %d%c%d",
        client->device, client->baud, 8, client->parity, client->stop_bits);
    return true;
}

void modbus_serial_client_disconnect(modbus_serial_client_t *client) {
    if (client && client->port) {
        windmi_serial_close(client->port);
        client->port = NULL;
        client->connected = false;
        WINDMI_C_LOG(WINDMI_LOG_INFO, LOG_TAG_MODBUS, "Serial client disconnected");
    }
}

bool modbus_serial_client_is_connected(modbus_serial_client_t *client) {
    return client && client->connected;
}

void modbus_serial_flush_buffer(modbus_serial_client_t *client) {
    if (client && client->port) {
        windmi_serial_flush(client->port);
    }
}

static int serial_send_frame(modbus_serial_client_t *client, const uint8_t *frame, size_t len) {
    if (!client || !client->port || !frame || len == 0) return -1;
    windmi_serial_flush(client->port);

    size_t total_sent = 0;
    while (total_sent < len) {
        int sent = windmi_serial_write(client->port, frame + total_sent, len - total_sent);
        if (sent <= 0) return -1;
        total_sent += (size_t)sent;
    }
    return 0;
}

static int serial_receive_exact(modbus_serial_client_t *client, uint8_t *buffer, size_t expected_len, int timeout_ms) {
    if (!client || !client->port || !buffer || expected_len == 0) return -1;

    size_t total_received = 0;
    while (total_received < expected_len) {
        int received = windmi_serial_read(client->port, buffer + total_received,
                                          expected_len - total_received,
                                          (unsigned int)timeout_ms);
        if (received <= 0) {
            if (received == 0) {
                WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS,
                    "Receive timeout (got %zu/%zu bytes)", total_received, expected_len);
            }
            return -1;
        }
        total_received += (size_t)received;
    }
    return 0;
}

static int modbus_serial_read_write_registers_internal(modbus_serial_client_t *client,
                                                        uint16_t address,
                                                        int16_t *values,
                                                        uint16_t count,
                                                        bool single) {
    if (!client || !client->connected || !values || count == 0) return -1;

    uint8_t frame[8];
    uint16_t read_count = single ? 1 : count;
    modbus_rtu_build_read_frame(frame, client->slave_id, address, read_count);

    if (serial_send_frame(client, frame, sizeof(frame)) != 0) {
        client->connected = false;
        return -1;
    }

    windmi_sleep_ms(MODBUS_SERIAL_INTER_FRAME_DELAY_MS);

    uint8_t header[MODBUS_SERIAL_READ_HEADER_LEN];
    if (serial_receive_exact(client, header, MODBUS_SERIAL_READ_HEADER_LEN, MODBUS_SERIAL_RECV_TIMEOUT_MS) != 0) {
        client->connected = false;
        return -1;
    }

    if (header[0] != client->slave_id) return -1;

    if (header[1] & 0x80) {
        uint8_t exc_crc[2];
        serial_receive_exact(client, exc_crc, 2, MODBUS_SERIAL_RECV_TIMEOUT_MS);
        WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS,
            "Modbus exception: slave=%d func=0x%02X code=%d", header[0], header[1], header[2]);
        return -1;
    }

    uint8_t byte_count = header[2];
    if (byte_count != read_count * 2) {
        WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS,
            "Unexpected byte count: %d (expected %d)", byte_count, read_count * 2);
        return -1;
    }

    uint8_t data_and_crc[MODBUS_SERIAL_MAX_FRAME];
    size_t remaining = (size_t)byte_count + 2;
    if (serial_receive_exact(client, data_and_crc, remaining, MODBUS_SERIAL_RECV_TIMEOUT_MS) != 0) {
        client->connected = false;
        return -1;
    }

    uint8_t full_frame[MODBUS_SERIAL_MAX_FRAME];
    size_t total_len = MODBUS_SERIAL_READ_HEADER_LEN + remaining;
    full_frame[0] = header[0];
    full_frame[1] = header[1];
    full_frame[2] = header[2];
    memcpy(full_frame + 3, data_and_crc, remaining);

    uint16_t crc_received = (uint16_t)full_frame[total_len - 2] |
                           ((uint16_t)full_frame[total_len - 1] << 8);
    uint16_t crc_calculated = crc16(full_frame, total_len - 2);
    if (crc_received != crc_calculated) {
        WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "CRC error in read response");
        return -1;
    }

    for (uint16_t i = 0; i < read_count; i++) {
        values[i] = (int16_t)bytes_to_uint16(&data_and_crc[i * 2]);
    }
    return 0;
}

int modbus_serial_read_register(modbus_serial_client_t *client, uint16_t address, int16_t *value) {
    if (!value) return -1;
    return modbus_serial_read_write_registers_internal(client, address, value, 1, true);
}

int modbus_serial_write_register(modbus_serial_client_t *client, uint16_t address, uint16_t value) {
    if (!client || !client->connected) return -1;

    for (int retry = 0; retry < MODBUS_SERIAL_WRITE_MAX_RETRIES; retry++) {
        if (retry > 0) {
            WINDMI_C_LOG(WINDMI_LOG_DEBUG, LOG_TAG_MODBUS,
                "Write retry %d/%d for address 0x%04X", retry, MODBUS_SERIAL_WRITE_MAX_RETRIES, address);
            windmi_sleep_ms(MODBUS_SERIAL_RETRY_DELAY_MS);
        }

        uint8_t frame[8];
        modbus_rtu_build_write_frame(frame, client->slave_id, address, value);
        if (serial_send_frame(client, frame, sizeof(frame)) != 0) {
            client->connected = false;
            continue;
        }

        windmi_sleep_ms(MODBUS_SERIAL_INTER_FRAME_DELAY_MS);

        uint8_t response[8];
        if (serial_receive_exact(client, response, sizeof(response), MODBUS_SERIAL_RECV_TIMEOUT_MS) != 0) {
            client->connected = false;
            continue;
        }

        if (response[1] & 0x80) {
            WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS,
                "Modbus exception on write: slave=%d func=0x%02X exception_code=%d (attempt %d/%d)",
                response[0], response[1] & 0x7F, response[2], retry + 1, MODBUS_SERIAL_WRITE_MAX_RETRIES);
            continue;
        }

        if (response[0] != client->slave_id || response[1] != 0x06) {
            WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS,
                "Write response mismatch: slave=%d func=0x%02X (attempt %d/%d)",
                response[0], response[1], retry + 1, MODBUS_SERIAL_WRITE_MAX_RETRIES);
            continue;
        }

        uint16_t crc_received = response[6] | (response[7] << 8);
        uint16_t crc_calculated = crc16(response, 6);
        if (crc_received != crc_calculated) {
            WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS,
                "Modbus CRC error on write (attempt %d/%d)", retry + 1, MODBUS_SERIAL_WRITE_MAX_RETRIES);
            continue;
        }

        windmi_sleep_ms(MODBUS_SERIAL_INTER_FRAME_DELAY_MS);

        int16_t read_value;
        if (modbus_serial_read_register(client, address, &read_value) != 0) continue;
        if ((uint16_t)read_value != value) {
            WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS,
                "Modbus write verify failed: wrote %u, read %d (attempt %d/%d)",
                value, read_value, retry + 1, MODBUS_SERIAL_WRITE_MAX_RETRIES);
            continue;
        }

        if (retry > 0) {
            WINDMI_C_LOG(WINDMI_LOG_DEBUG, LOG_TAG_MODBUS, "Write succeeded after %d retry(ies)", retry);
        }
        return 0;
    }

    WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS,
        "Write failed after %d attempts", MODBUS_SERIAL_WRITE_MAX_RETRIES);
    return -1;
}

int modbus_serial_read_registers(modbus_serial_client_t *client, uint16_t address,
                                 int16_t *values, uint16_t count) {
    if (!values || count == 0) return -1;
    return modbus_serial_read_write_registers_internal(client, address, values, count, false);
}
