/**
 * @file modbus_serial_client.c
 * @brief Modbus RTU over serial (RS-485) client
 */

#include "modbus/modbus_serial_client.h"
#include "modbus/modbus_rtu_frame.h"
#include "crc16.h"
#include "utils/LogTags.hpp"
#include "utils/LoggerC.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <linux/serial.h>
#include <termios.h>

#define MODBUS_SERIAL_WRITE_MAX_RETRIES 3

// Inter-frame delay in microseconds (≥4ms for safety at 9600 baud)
// This ensures compliance with Modbus RTU timing requirements
#define MODBUS_SERIAL_INTER_FRAME_DELAY_US 4000

/**
 * Modbus serial client structure
 */
struct modbus_serial_client {
    char device[256];           // Device path
    int baud;                   // Baud rate
    char parity;                // Parity: 'N', 'E', 'O'
    int stop_bits;              // Stop bits: 1 or 2
    bool rs485_enabled;         // RS-485 direction control enabled
    uint8_t slave_id;           // Modbus slave ID
    int fd;                     // File descriptor for serial port
    bool connected;             // Connection state
};

// Forward declarations
static bool configure_serial_port(int fd, int baud, char parity, int stop_bits);
static int serial_send_frame(int fd, const uint8_t *frame, size_t len);
static int serial_receive_exact(int fd, uint8_t *buffer, size_t expected_len, int timeout_ms);
static int serial_flush_buffer(int fd);

/**
 * Create a new Modbus serial client
 */
modbus_serial_client_t *modbus_serial_client_create(
    const char *device,
    int baud,
    char parity,
    int stop_bits,
    bool rs485_enabled,
    uint8_t slave_id
) {
    // Validate parameters
    if (!device) {
        WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "Serial client: device path is NULL");
        return NULL;
    }
    
    if (strlen(device) >= sizeof(((modbus_serial_client_t*)0)->device)) {
        WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "Serial client: device path too long");
        return NULL;
    }
    
    // Validate baud rate
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
    
    // Validate parity (accept both uppercase and lowercase)
    if ((parity != 'N' && parity != 'E' && parity != 'O') &&
        (parity != 'n' && parity != 'e' && parity != 'o')) {
        WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "Serial client: invalid parity '%c'", parity);
        return NULL;
    }
    // Normalize to uppercase
    parity = toupper((unsigned char)parity);
    
    // Validate stop bits
    if (stop_bits != 1 && stop_bits != 2) {
        WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "Serial client: invalid stop bits %d (must be 1 or 2)", stop_bits);
        return NULL;
    }
    
    modbus_serial_client_t *client = (modbus_serial_client_t *)malloc(sizeof(modbus_serial_client_t));
    if (!client) {
        WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "Serial client: malloc failed: %s", strerror(errno));
        return NULL;
    }
    
    strncpy(client->device, device, sizeof(client->device) - 1);
    client->device[sizeof(client->device) - 1] = '\0';
    client->baud = baud;
    client->parity = parity;
    client->stop_bits = stop_bits;
    client->rs485_enabled = rs485_enabled;
    client->slave_id = slave_id;
    client->fd = -1;
    client->connected = false;
    
    WINDMI_C_LOG(WINDMI_LOG_INFO, LOG_TAG_MODBUS,
        "Serial client created: device=%s, baud=%d, parity=%c, stop_bits=%d, rs485=%s, slave_id=%d",
        client->device, client->baud, client->parity, client->stop_bits,
        client->rs485_enabled ? "enabled" : "disabled", client->slave_id);
    
    return client;
}

/**
 * Destroy a Modbus serial client
 */
void modbus_serial_client_destroy(modbus_serial_client_t *client) {
    if (client) {
        if (client->connected) {
            modbus_serial_client_disconnect(client);
        }
        free(client);
    }
}

/**
 * Configure serial port settings
 */
static bool configure_serial_port(int fd, int baud, char parity, int stop_bits) {
    struct termios tty;
    
    // Get current terminal attributes
    if (tcgetattr(fd, &tty) != 0) {
        WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "tcgetattr failed: %s", strerror(errno));
        return false;
    }
    
    // Set raw mode
    cfmakeraw(&tty);
    
    // Set baud rate
    speed_t speed;
    switch (baud) {
        case 9600:   speed = B9600;   break;
        case 19200:  speed = B19200;  break;
        case 38400:  speed = B38400;  break;
        case 57600:  speed = B57600;  break;
        case 115200: speed = B115200; break;
        default:
            WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "Unsupported baud rate: %d", baud);
            return false;
    }
    
    if (cfsetspeed(&tty, speed) != 0) {
        WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "cfsetspeed failed: %s", strerror(errno));
        return false;
    }
    
    // Set character size (8 data bits)
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    
    // Set stop bits
    if (stop_bits == 2) {
        tty.c_cflag |= CSTOPB;  // 2 stop bits
    } else {
        tty.c_cflag &= ~CSTOPB; // 1 stop bit
    }
    
    // Set parity
    tty.c_cflag &= ~PARENB;  // Clear parity enable
    tty.c_cflag &= ~PARODD;  // Clear parity odd/even
    
    switch (parity) {
        case 'N':  // None
            tty.c_cflag &= ~PARENB;
            break;
        case 'E':  // Even
            tty.c_cflag |= PARENB;
            tty.c_cflag &= ~PARODD;
            break;
        case 'O':  // Odd
            tty.c_cflag |= PARENB;
            tty.c_cflag |= PARODD;
            break;
        default:
            WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "Unknown parity: %c", parity);
            return false;
    }
    
    // Disable hardware flow control
    tty.c_cflag &= ~CRTSCTS;
    
    // Disable software flow control
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    
    // Set read behavior: minimum characters = 1, timeout = 0 (blocking with select)
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;
    
    // Apply settings
    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "tcsetattr failed: %s", strerror(errno));
        return false;
    }
    
    return true;
}

/**
 * Setup RS-485 direction control
 */
static bool setup_rs485(int fd) {
    struct serial_rs485 rs485;
    
    // Get current RS-485 settings
    if (ioctl(fd, TIOCGRS485, &rs485) != 0) {
        WINDMI_C_LOG(WINDMI_LOG_WARN, LOG_TAG_MODBUS,
            "TIOCGRS485 failed (may not be supported): %s", strerror(errno));
        return false;
    }
    
    // Enable RS-485 mode
    rs485.flags |= SER_RS485_ENABLED;
    
    // RTS goes high before transmission, low after
    rs485.flags |= SER_RS485_RTS_ON_SEND;
    rs485.flags &= ~SER_RS485_RTS_AFTER_SEND;
    
    // Use default delays (usually fine for USB-RS485 adapters)
    // rs485.delay_rts_before_send = 0;
    // rs485.delay_rts_after_send = 0;
    
    // Apply RS-485 settings
    if (ioctl(fd, TIOCSRS485, &rs485) != 0) {
        WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS,
            "TIOCSRS485 failed: %s", strerror(errno));
        return false;
    }
    
    WINDMI_C_LOG(WINDMI_LOG_INFO, LOG_TAG_MODBUS, "RS-485 enabled on serial port");
    return true;
}

/**
 * Connect to the serial device
 */
bool modbus_serial_client_connect(modbus_serial_client_t *client) {
    if (!client || client->connected) {
        return false;
    }
    
    // Open device in read-write mode (non-blocking to avoid hanging on open)
    int fd = open(client->device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS,
            "Failed to open serial device %s: %s", client->device, strerror(errno));
        
        // Provide helpful error for permission denied
        if (errno == EACCES) {
            WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS,
                "Permission denied. User may need to be added to 'dialout' group:");
            WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS,
                "  sudo usermod -aG dialout $USER");
            WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS,
                "  Then log out and log back in, or run 'newgrp dialout'");
        }
        
        return false;
    }
    
    // Configure serial port
    if (!configure_serial_port(fd, client->baud, client->parity, client->stop_bits)) {
        close(fd);
        return false;
    }
    
    // Setup RS-485 if enabled
    if (client->rs485_enabled) {
        setup_rs485(fd);
    }
    
    // Clear any stale data
    tcflush(fd, TCIOFLUSH);
    
    // Clear non-blocking flag for normal operation
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    }
    
    client->fd = fd;
    client->connected = true;
    
    WINDMI_C_LOG(WINDMI_LOG_INFO, LOG_TAG_MODBUS,
        "Serial client connected: %s @ %d %d%c%d",
        client->device, client->baud, 8, client->parity, client->stop_bits);
    
    return true;
}

/**
 * Disconnect from the serial device
 */
void modbus_serial_client_disconnect(modbus_serial_client_t *client) {
    if (client && client->connected && client->fd >= 0) {
        close(client->fd);
        client->fd = -1;
        client->connected = false;
        WINDMI_C_LOG(WINDMI_LOG_INFO, LOG_TAG_MODBUS, "Serial client disconnected");
    }
}

/**
 * Check if client is connected
 */
bool modbus_serial_client_is_connected(modbus_serial_client_t *client) {
    return client && client->connected;
}

/**
 * Flush any pending data in the serial read buffer
 * Uses tcflush() to discard data received but not read
 */
static int serial_flush_buffer(int fd) {
    if (fd < 0) return 0;
    
    // tcflush discards data received but not read
    int rc = tcflush(fd, TCIFLUSH);
    if (rc != 0) {
        WINDMI_C_LOG(WINDMI_LOG_DEBUG, LOG_TAG_MODBUS, "tcflush failed: %s", strerror(errno));
        return 0;
    }
    
    WINDMI_C_LOG(WINDMI_LOG_DEBUG, LOG_TAG_MODBUS, "Flushed serial input buffer with tcflush");
    return 0;
}

/**
 * Send a Modbus frame over serial
 * 
 * Note: This function does NOT include inter-frame delay.
 * The caller must add delay before the next frame.
 */
static int serial_send_frame(int fd, const uint8_t *frame, size_t len) {
    if (fd < 0 || !frame || len == 0) {
        return -1;
    }
    
    // Flush buffer before sending
    serial_flush_buffer(fd);
    
    size_t total_sent = 0;
    while (total_sent < len) {
        ssize_t sent = write(fd, frame + total_sent, len - total_sent);
        
        if (sent < 0) {
            if (errno == EINTR) {
                continue;  // Interrupted, retry
            }
            WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "serial_send_frame write failed: %s", strerror(errno));
            return -1;
        }
        
        if (sent == 0) {
            WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "serial_send_frame: zero bytes written");
            return -1;
        }
        
        total_sent += sent;
    }
    
    // Wait for all output to be transmitted
    if (tcdrain(fd) != 0) {
        WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "tcdrain failed: %s", strerror(errno));
        return -1;
    }
    
    return 0;
}

/**
 * Receive exact number of bytes from serial port
 */
static int serial_receive_exact(int fd, uint8_t *buffer, size_t expected_len, int timeout_ms) {
    if (fd < 0 || !buffer || expected_len == 0) {
        return -1;
    }
    
    size_t total_received = 0;
    
    while (total_received < expected_len) {
        fd_set fds;
        struct timeval tv;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        
        int ready;
        do {
            ready = select(fd + 1, &fds, NULL, NULL, &tv);
            if (ready < 0 && errno == EINTR) {
                continue;  // Interrupted by signal, retry
            }
            break;
        } while (1);
        
        if (ready <= 0) {
            if (ready == 0) {
                WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS,
                    "Receive timeout (got %zu/%zu bytes)", total_received, expected_len);
            } else {
                WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "select failed: %s", strerror(errno));
            }
            return -1;
        }
        
        ssize_t received;
        do {
            received = read(fd, buffer + total_received, expected_len - total_received);
        } while (received < 0 && errno == EINTR);  // Retry on EINTR
        
        if (received < 0) {
            return -1;
        }
        
        if (received == 0) {
            // EOF - device disconnected
            return -1;
        }
        
        total_received += received;
    }
    
    return 0;
}

/**
 * Read a single holding register
 */
int modbus_serial_read_register(modbus_serial_client_t *client, uint16_t address, int16_t *value) {
    if (!client || !client->connected || !value) {
        return -1;
    }
    
    return modbus_serial_read_registers(client, address, value, 1);
}

/**
 * Write a single register with read-back verification
 */
int modbus_serial_write_register(modbus_serial_client_t *client, uint16_t address, uint16_t value) {
    if (!client || !client->connected) {
        return -1;
    }
    
    int retry;
    for (retry = 0; retry < MODBUS_SERIAL_WRITE_MAX_RETRIES; retry++) {
        if (retry > 0) {
            WINDMI_C_LOG(WINDMI_LOG_DEBUG, LOG_TAG_MODBUS,
                "Write retry %d/%d for address 0x%04X",
                retry, MODBUS_SERIAL_WRITE_MAX_RETRIES, address);
            usleep(MODBUS_SERIAL_RETRY_DELAY_MS * 1000);
        }
        
        // Build and send write frame
        uint8_t frame[8];
        modbus_rtu_build_write_frame(frame, client->slave_id, address, value);
        
        if (serial_send_frame(client->fd, frame, 8) != 0) {
            client->connected = false;
            continue;
        }
        
        // Receive response
        uint8_t response[8];
        if (serial_receive_exact(client->fd, response, 8, MODBUS_SERIAL_RECV_TIMEOUT_MS) != 0) {
            client->connected = false;
            continue;
        }
        
        // Check for exception response
        if (response[1] & 0x80) {
            WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS,
                "Modbus exception on write: slave=%d func=0x%02X exception_code=%d",
                response[0], response[1] & 0x7F, response[2]);
            continue;
        }
        
        // Check response mismatch
        if (response[0] != client->slave_id || response[1] != 0x06) {
            WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS,
                "Write response mismatch: slave=%d func=0x%02X",
                response[0], response[1]);
            continue;
        }
        
        // Verify CRC
        uint16_t crc_received = response[6] | (response[7] << 8);
        uint16_t crc_calculated = crc16(response, 6);
        if (crc_received != crc_calculated) {
            WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "Modbus CRC error on write");
            continue;
        }
        
        // Inter-frame delay before read-back (≥4ms for safety at 9600 baud)
        // Modbus RTU requires ≥3.5 character times between frames
        usleep(MODBUS_SERIAL_INTER_FRAME_DELAY_US);
        
        // Read-back verification
        int16_t read_value;
        if (modbus_serial_read_register(client, address, &read_value) != 0) {
            continue;
        }
        
        if ((uint16_t)read_value != value) {
            WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS,
                "Modbus write verify failed: wrote %u, read %d",
                value, read_value);
            continue;
        }
        
        // Success
        if (retry > 0) {
            WINDMI_C_LOG(WINDMI_LOG_DEBUG, LOG_TAG_MODBUS, "Write succeeded after %d retry(ies)", retry);
        }
        return 0;
    }
    
    WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS,
        "Write failed after %d attempts", MODBUS_SERIAL_WRITE_MAX_RETRIES);
    return -1;
}

/**
 * Read multiple holding registers
 */
int modbus_serial_read_registers(modbus_serial_client_t *client, uint16_t address,
                                  int16_t *values, uint16_t count) {
    if (!client || !client->connected || !values || count == 0) {
        return -1;
    }
    
    // Build read frame
    uint8_t frame[8];
    modbus_rtu_build_read_frame(frame, client->slave_id, address, count);
    
    // Send request
    if (serial_send_frame(client->fd, frame, 8) != 0) {
        client->connected = false;
        return -1;
    }
    
    // Step 1: Read 3-byte header
    uint8_t header[3];
    if (serial_receive_exact(client->fd, header, 3, MODBUS_SERIAL_RECV_TIMEOUT_MS) != 0) {
        client->connected = false;
        return -1;
    }
    
    if (header[0] != client->slave_id) {
        return -1;
    }
    
    // Check for exception response
    if (header[1] & 0x80) {
        uint8_t exc_crc[2];
        serial_receive_exact(client->fd, exc_crc, 2, MODBUS_SERIAL_RECV_TIMEOUT_MS);
        WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS,
            "Modbus exception: slave=%d func=0x%02X code=%d",
            header[0], header[1], header[2]);
        return -1;
    }
    
    uint8_t byte_count = header[2];
    if (byte_count != count * 2) {
        WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS,
            "Unexpected byte count: %d (expected %d)", byte_count, count * 2);
        return -1;
    }
    
    // Step 2: Read data + CRC
    uint8_t data_and_crc[256];
    size_t remaining = byte_count + 2;
    if (serial_receive_exact(client->fd, data_and_crc, remaining, MODBUS_SERIAL_RECV_TIMEOUT_MS) != 0) {
        client->connected = false;
        return -1;
    }
    
    // Step 3: Verify CRC
    uint8_t full_frame[256];
    size_t total_len = 3 + remaining;
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
    
    // Step 4: Extract values (Modbus is big-endian)
    for (uint16_t i = 0; i < count; i++) {
        uint16_t raw_value = ((uint16_t)data_and_crc[i * 2] << 8) |
                            (uint16_t)data_and_crc[i * 2 + 1];
        values[i] = (int16_t)raw_value;
    }
    
    return 0;
}

/**
 * Flush any pending data in the serial read buffer
 */
void modbus_serial_flush_buffer(modbus_serial_client_t *client) {
    if (client && client->fd >= 0) {
        serial_flush_buffer(client->fd);
    }
}
