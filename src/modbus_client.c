#include "modbus_client.h"
#include "modbus/modbus_rtu_frame.h"
#include "crc16.h"
#include "utils/LogTags.hpp"
#include "utils/LoggerC.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <time.h>

#define MODBUS_MAX_FRAME 256
#define MODBUS_RECV_TIMEOUT_MS 2000  // 2 second timeout for receives
#define MODBUS_WRITE_MAX_RETRIES 3   // Max retry attempts for writes
#define MODBUS_RETRY_DELAY_MS 100    // Delay between retries in ms

// Modbus RTU response frame layout:
//   Normal:  [slave(1)][func(1)][byte_count(1)][data(N)][crc_lo(1)][crc_hi(1)]
//   Exception: [slave(1)][func|0x80(1)][exception_code(1)][crc_lo(1)][crc_hi(1)]
//
// Normal read header is 3 bytes (slave + func + byte_count).
// We read the 3-byte header first, then read byte_count data + 2 CRC bytes.

#define MODBUS_READ_HEADER_LEN 3

struct modbus_client {
    char ip[16];
    int port;
    uint8_t slave_id;
    int socket_fd;
    bool connected;
};

static uint16_t bytes_to_uint16(const uint8_t *bytes) {
    return (uint16_t)((bytes[0] << 8) | bytes[1]);
}

modbus_client_t *modbus_client_create(const char *ip, int port, uint8_t slave_id) {
    modbus_client_t *client = (modbus_client_t *)malloc(sizeof(modbus_client_t));
    if (!client) {
        return NULL;
    }
    
    strncpy(client->ip, ip, sizeof(client->ip) - 1);
    client->ip[sizeof(client->ip) - 1] = '\0';
    client->port = port;
    client->slave_id = slave_id;
    client->socket_fd = -1;
    client->connected = false;
    
    return client;
}

void modbus_client_destroy(modbus_client_t *client) {
    if (client) {
        if (client->connected) {
            modbus_client_disconnect(client);
        }
        free(client);
    }
}

bool modbus_client_connect(modbus_client_t *client) {
    if (!client || client->connected) {
        return false;
    }
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "socket failed: %s", strerror(errno));
        return false;
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(client->port);
    
    if (inet_pton(AF_INET, client->ip, &server_addr.sin_addr) <= 0) {
        WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "Invalid address");
        close(sock);
        return false;
    }
    
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "connect failed: %s", strerror(errno));
        close(sock);
        return false;
    }
    
    client->socket_fd = sock;
    client->connected = true;
    
    return true;
}

void modbus_client_disconnect(modbus_client_t *client) {
    if (client && client->connected && client->socket_fd >= 0) {
        close(client->socket_fd);
        client->socket_fd = -1;
        client->connected = false;
    }
}

bool modbus_client_is_connected(modbus_client_t *client) {
    return client && client->connected;
}

// Flush any pending data in the socket read buffer
// Returns number of bytes flushed
static int flush_read_buffer(modbus_client_t *client) {
    if (!client || client->socket_fd < 0) return 0;
    
    uint8_t dummy[128];
    fd_set fds;
    struct timeval tv = {0, 0};
    int flushed = 0;
    
    while (1) {
        FD_ZERO(&fds);
        FD_SET(client->socket_fd, &fds);
        int ready = select(client->socket_fd + 1, &fds, NULL, NULL, &tv);
        if (ready <= 0) break;
        ssize_t n = recv(client->socket_fd, dummy, sizeof(dummy), MSG_DONTWAIT);
        if (n <= 0) break;
        flushed += n;
    }
    
    if (flushed > 0) {
        WINDMI_C_LOG(WINDMI_LOG_DEBUG, LOG_TAG_MODBUS, "Flushed %d bytes of stale data from socket", flushed);
    }
    return flushed;
}

// Public API wrapper - same implementation, just void return
void modbus_client_flush_buffer(modbus_client_t *client) {
    flush_read_buffer(client);
}

static int send_frame(modbus_client_t *client, const uint8_t *frame, size_t len) {
    // Flush any stale data before sending
    flush_read_buffer(client);
    
    ssize_t sent = send(client->socket_fd, frame, len, 0);
    return (sent == (ssize_t)len) ? 0 : -1;
}

static int receive_exact(modbus_client_t *client, uint8_t *buffer, size_t expected_len) {
    size_t total_received = 0;
    
    while (total_received < expected_len) {
        // Use select() with timeout to avoid blocking forever
        fd_set fds;
        struct timeval tv;
        FD_ZERO(&fds);
        FD_SET(client->socket_fd, &fds);
        tv.tv_sec = MODBUS_RECV_TIMEOUT_MS / 1000;
        tv.tv_usec = (MODBUS_RECV_TIMEOUT_MS % 1000) * 1000;
        
        int ready = select(client->socket_fd + 1, &fds, NULL, NULL, &tv);
        if (ready <= 0) {
            if (ready == 0) {
                WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "Receive timeout (got %zu/%zu bytes)",
                        total_received, expected_len);
            }
            return -1;
        }
        
        ssize_t received = recv(client->socket_fd, buffer + total_received, 
                                expected_len - total_received, 0);
        
        if (received <= 0) {
            return -1;
        }
        
        total_received += received;
    }
    
    return 0;
}

static int modbus_read_write_registers_internal(modbus_client_t *client, 
                                                 uint16_t address, 
                                                 int16_t *values, 
                                                 uint16_t count,
                                                 bool single) {
    if (!client || !client->connected || !values || count <= 0) {
        return -1;
    }
    
    uint8_t frame[MODBUS_MAX_FRAME];
    uint16_t read_count = single ? 1 : count;
    modbus_rtu_build_read_frame(frame, client->slave_id, address, read_count);
    
    if (send_frame(client, frame, 8) != 0) {
        client->connected = false;
        return -1;
    }
    
    // Step 1: Read 3-byte header: [slave][func][byte_count]
    uint8_t header[MODBUS_READ_HEADER_LEN];
    if (receive_exact(client, header, MODBUS_READ_HEADER_LEN) != 0) {
        client->connected = false;
        return -1;
    }
    
    if (header[0] != client->slave_id) {
        return -1;
    }
    
    // Check for exception response (func | 0x80)
    if (header[1] & 0x80) {
        // Exception frame: [slave][func|0x80][exception_code][crc_lo][crc_hi]
        // We already read 3 bytes: slave, func|0x80, and what we thought was byte_count
        // is actually the exception code. Read the remaining 2 CRC bytes.
        uint8_t exc_crc[2];
        receive_exact(client, exc_crc, 2);
        WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "Modbus exception: slave=%d func=0x%02X code=%d",
                header[0], header[1], header[2]);
        return -1;
    }
    
    uint8_t byte_count = header[2];
    if (byte_count != read_count * 2) {
        WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "Unexpected byte count: %d (expected %d)",
                byte_count, read_count * 2);
        return -1;
    }
    
    // Step 2: Read data + CRC (byte_count data bytes + 2 CRC bytes)
    uint8_t data_and_crc[MODBUS_MAX_FRAME];
    size_t remaining = byte_count + 2;
    if (receive_exact(client, data_and_crc, remaining) != 0) {
        client->connected = false;
        return -1;
    }
    
    // Step 3: Verify CRC over the full frame (header + data + crc)
    uint8_t full_frame[MODBUS_MAX_FRAME];
    size_t total_len = MODBUS_READ_HEADER_LEN + remaining;
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
    
    // Step 4: Extract values as signed 16-bit
    for (uint16_t i = 0; i < read_count; i++) {
        uint16_t raw_value = bytes_to_uint16(&data_and_crc[i * 2]);
        values[i] = (int16_t)raw_value;
    }
    
    return 0;
}

int modbus_read_register(modbus_client_t *client, uint16_t address, int16_t *value) {
    if (!value) {
        return -1;
    }
    
    return modbus_read_write_registers_internal(client, address, value, 1, true);
}

int modbus_write_register(modbus_client_t *client, uint16_t address, uint16_t value) {
    if (!client || !client->connected) {
        return -1;
    }
    
    int retry;
    for (retry = 0; retry < MODBUS_WRITE_MAX_RETRIES; retry++) {
        if (retry > 0) {
            WINDMI_C_LOG(WINDMI_LOG_DEBUG, LOG_TAG_MODBUS, "Write retry %d/%d for address 0x%04X",
                   retry, MODBUS_WRITE_MAX_RETRIES, address);
            usleep(MODBUS_RETRY_DELAY_MS * 1000);
        }
        
        uint8_t frame[MODBUS_MAX_FRAME];
        modbus_rtu_build_write_frame(frame, client->slave_id, address, value);
        
        if (send_frame(client, frame, 8) != 0) {
            continue; // Retry on send failure
        }
        
        uint8_t response[8];
        if (receive_exact(client, response, 8) != 0) {
            continue; // Retry on receive failure
        }
        
        // Check for exception response first (func code with MSB set)
        if (response[1] & 0x80) {
            WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "Modbus exception on write: slave=%d func=0x%02X exception_code=%d (attempt %d/%d)", 
                    response[0], response[1] & 0x7F, response[2], retry + 1, MODBUS_WRITE_MAX_RETRIES);
            continue; // Retry on exception
        }
        
        // Check for normal response mismatch
        if (response[0] != client->slave_id || response[1] != 0x06) {
            WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "Write response mismatch: slave=%d func=0x%02X (attempt %d/%d)",
                    response[0], response[1], retry + 1, MODBUS_WRITE_MAX_RETRIES);
            continue; // Retry on response mismatch
        }
        
        uint16_t crc_received = response[6] | (response[7] << 8);
        uint16_t crc_calculated = crc16(response, 6);
        if (crc_received != crc_calculated) {
            WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "Modbus CRC error on write (attempt %d/%d)", 
                    retry + 1, MODBUS_WRITE_MAX_RETRIES);
            continue; // Retry on CRC error
        }
        
        int16_t read_value;
        if (modbus_read_register(client, address, &read_value) != 0) {
            continue; // Retry on read-back failure
        }
        
        if ((uint16_t)read_value != value) {
            WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "Modbus write verify failed: wrote %u, read %d (attempt %d/%d)",
                    value, read_value, retry + 1, MODBUS_WRITE_MAX_RETRIES);
            continue; // Retry on verify failure
        }
        
        // Success
        if (retry > 0) {
            WINDMI_C_LOG(WINDMI_LOG_DEBUG, LOG_TAG_MODBUS, "Write succeeded after %d retry(ies)", retry);
        }
        return 0;
    }
    
    // All retries exhausted
    WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "Write failed after %d attempts", MODBUS_WRITE_MAX_RETRIES);
    return -1;
}

int modbus_read_registers(modbus_client_t *client, uint16_t address,
                          int16_t *values, uint16_t count) {
    if (!values || count <= 0) {
        return -1;
    }
    
    return modbus_read_write_registers_internal(client, address, values, count, false);
}
