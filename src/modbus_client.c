#include "modbus_client.h"
#include "crc16.h"
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

#define MODBUS_MAX_FRAME 256
#define MODBUS_EXCEPTION_OFFSET 5
#define MODBUS_RESPONSE_HEADER_LEN 5

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

static void build_read_frame(uint8_t *frame, uint8_t slave_id, uint16_t address, uint16_t count) {
    frame[0] = slave_id;
    frame[1] = 0x03;  // Read Holding Registers
    frame[2] = (address >> 8) & 0xFF;
    frame[3] = address & 0xFF;
    frame[4] = (count >> 8) & 0xFF;
    frame[5] = count & 0xFF;
    
    uint16_t crc = crc16_modbus(frame, 6);
    frame[6] = crc & 0xFF;
    frame[7] = (crc >> 8) & 0xFF;
}

static void build_write_frame(uint8_t *frame, uint8_t slave_id, uint16_t address, uint16_t value) {
    frame[0] = slave_id;
    frame[1] = 0x06;  // Write Single Register
    frame[2] = (address >> 8) & 0xFF;
    frame[3] = address & 0xFF;
    frame[4] = (value >> 8) & 0xFF;
    frame[5] = value & 0xFF;
    
    uint16_t crc = crc16_modbus(frame, 6);
    frame[6] = crc & 0xFF;
    frame[7] = (crc >> 8) & 0xFF;
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
        perror("socket failed");
        return false;
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(client->port);
    
    if (inet_pton(AF_INET, client->ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid address");
        close(sock);
        return false;
    }
    
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect failed");
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

static int send_frame(modbus_client_t *client, const uint8_t *frame, size_t len) {
    ssize_t sent = send(client->socket_fd, frame, len, 0);
    return (sent == (ssize_t)len) ? 0 : -1;
}

static int receive_exact(modbus_client_t *client, uint8_t *buffer, size_t expected_len) {
    size_t total_received = 0;
    
    while (total_received < expected_len) {
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
    build_read_frame(frame, client->slave_id, address, read_count);
    
    if (send_frame(client, frame, 8) != 0) {
        return -1;
    }
    
    uint8_t response[MODBUS_MAX_FRAME];
    
    if (receive_exact(client, response, MODBUS_RESPONSE_HEADER_LEN) != 0) {
        return -1;
    }
    
    if (response[0] != client->slave_id) {
        return -1;
    }
    
    if (response[1] == 0x83) {
        return -1;
    }
    
    uint8_t byte_count = response[2];
    uint16_t expected_response_len = MODBUS_RESPONSE_HEADER_LEN + 1 + byte_count + 2;
    
    if (byte_count != read_count * 2) {
        return -1;
    }
    
    if (receive_exact(client, response + MODBUS_RESPONSE_HEADER_LEN, 
                      expected_response_len - MODBUS_RESPONSE_HEADER_LEN) != 0) {
        return -1;
    }
    
    uint16_t crc_received = response[expected_response_len - 2] | 
                           (response[expected_response_len - 1] << 8);
    uint16_t crc_calculated = crc16_modbus(response, expected_response_len - 2);
    
    if (crc_received != crc_calculated) {
        return -1;
    }
    
    for (uint16_t i = 0; i < read_count; i++) {
        uint16_t raw_value = bytes_to_uint16(&response[3 + i * 2]);
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
    
    uint8_t frame[MODBUS_MAX_FRAME];
    build_write_frame(frame, client->slave_id, address, value);
    
    if (send_frame(client, frame, 8) != 0) {
        return -1;
    }
    
    uint8_t response[8];
    if (receive_exact(client, response, 8) != 0) {
        return -1;
    }
    
    if (response[0] != client->slave_id || response[1] != 0x06) {
        return -1;
    }
    
    uint16_t crc_received = response[6] | (response[7] << 8);
    uint16_t crc_calculated = crc16_modbus(response, 6);
    if (crc_received != crc_calculated) {
        return -1;
    }
    
    int16_t read_value;
    if (modbus_read_register(client, address, &read_value) != 0) {
        return -1;
    }
    
    if ((uint16_t)read_value != value) {
        return -1;
    }
    
    return 0;
}

int modbus_read_registers(modbus_client_t *client, uint16_t address,
                          int16_t *values, uint16_t count) {
    if (!values || count <= 0) {
        return -1;
    }
    
    return modbus_read_write_registers_internal(client, address, values, count, false);
}
