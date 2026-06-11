/**
 * @file probe_registers.c
 * @brief Standalone tool to probe which Modbus registers exist on the device.
 *
 * Sends raw Modbus RTU read frames (FC 0x03) over TCP to the Waveshare
 * gateway and reports OK/FAIL for every register in the config.
 *
 * Usage:
 *   ./probe_registers [ip [port]]
 *   Default: 192.168.123.10 8899
 *
 * Build:
 *   gcc -o probe_registers probe_registers.c crc16.c -lpthread
 *   (or use the CMake build below)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
#include <fcntl.h>

/* CRC16 Modbus (same as crc16.c/cpp) */
static uint16_t crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    size_t i, j;
    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

static void build_read_frame(uint8_t *frame, uint8_t slave, uint16_t addr) {
    frame[0] = slave;
    frame[1] = 0x03;
    frame[2] = (addr >> 8) & 0xFF;
    frame[3] = addr & 0xFF;
    frame[4] = 0x00;
    frame[5] = 0x01;
    uint16_t crc = crc16(frame, 6);
    frame[6] = crc & 0xFF;
    frame[7] = (crc >> 8) & 0xFF;
}

typedef struct {
    const char *name;
    uint16_t address;
} RegisterDef;

/* All registers from config.h, sorted by address */
static const RegisterDef registers[] = {
    { "OUTDOOR_TEMP",          0x0001 },
    { "INDOOR_TEMP",           0x0002 },
    { "ENTERING_WATER_TEMP",   0x0003 },
    { "LEAVING_WATER_TEMP",    0x0004 },
    { "COMPRESSOR_FREQ",       0x0017 },
    { "RUNNING_MODE",          0x002C },
    { "RUNNING_STATUS",        0x002D },
    { "OCCUPANCY_MODE",        0x0029 },
    { "WATER_CONTROL_POINT",   0x0033 },
    { "DHW_TANK_TEMP",         0x00CE },
    { "DHW_MODE_STATUS",       0x00C9 },
    { "COMPRESSOR_RUNTIME",    0x0174 },
    { "PUMP_RUNTIME",          0x0176 },
    { "HEATING_TARGET",        0x0191 },
    { "DHW_TARGET",            0x0194 },
    { "DHW_PRIORITY",          0x02BF },
    { "ACTUAL_CAPACITY_OUTPUT", 0x1004 },
    { "UNIT_CAPACITY",         0x1006 },
    { "AC_CURRENT",            0x1014 },
    { "DC_CURRENT",            0x1015 },
    { "AC_VOLTAGE",            0x1016 },
    { "DC_VOLTAGE",            0x1017 },
    { "ODU_INPUT_STATUS",      0x101F },
    { "WATER_FLOW",            0x102A },
    { "DHW_VALVE_STATUS",      0x00D2 },
};
static const int NUM_REGISTERS = sizeof(registers) / sizeof(registers[0]);

static int flush_recv(int sockfd) {
    uint8_t dummy[128];
    fd_set fds;
    struct timeval tv = {0, 0};
    int flushed = 0;
    FD_ZERO(&fds);
    FD_SET(sockfd, &fds);
    while (select(sockfd + 1, &fds, NULL, NULL, &tv) > 0) {
        int n = (int)recv(sockfd, dummy, sizeof(dummy), MSG_DONTWAIT);
        if (n <= 0) break;
        flushed += n;
    }
    return flushed;
}

static int probe_register(int sockfd, uint8_t slave, const RegisterDef *reg) {
    uint8_t frame[8];
    build_read_frame(frame, slave, reg->address);

    // Flush stale data before sending (Waveshare gateway may leave bytes in buffer)
    flush_recv(sockfd);

    if (write(sockfd, frame, 8) != 8) return -2;

    /* Read response header: [slave][func][byte_count] */
    uint8_t header[3];
    int got = 0;
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;  // 100ms timeout per register
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (got < 3) {
        int n = read(sockfd, header + got, 3 - got);
        if (n <= 0) return -2;  // timeout or error
        got += n;
    }

    /* Drain rest of response */
    if (header[1] & 0x80) {
        /* Exception response: 2 more bytes (exc code + CRC) */
        uint8_t rest[3];
        int n = read(sockfd, rest, 3);
        (void)n;
        printf("  FAIL  %-28s 0x%04X  exc=%d\n", reg->name, reg->address, header[2]);
        return -1;
    } else {
        /* Normal response: byte_count data + 2 CRC */
        int data_len = header[2];
        uint8_t buf[260];
        int total = 0;
        while (total < data_len + 2) {
            int n = read(sockfd, buf + total, data_len + 2 - total);
            if (n <= 0) break;
            total += n;
        }
        int16_t value = (buf[0] << 8) | buf[1];
        printf("  OK    %-28s 0x%04X  value=%d (%.1f)\n",
               reg->name, reg->address, value, value / 10.0);
        return 0;
    }
}

int main(int argc, char *argv[]) {
    const char *ip = "192.168.123.10";
    int port = 8899;
    uint8_t slave = 11;

    if (argc >= 2) ip = argv[1];
    if (argc >= 3) port = atoi(argv[2]);

    printf("Modbus Register Probe\n");
    printf("=====================\n");
    printf("Target: %s:%d  slave=%d  registers=%d\n\n", ip, port, slave, NUM_REGISTERS);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    /* Non-blocking connect with timeout */
    fcntl(sockfd, F_SETFL, O_NONBLOCK);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    int ret = connect(sockfd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0 && errno != EINPROGRESS) {
        perror("connect");
        close(sockfd);
        return 1;
    }

    /* Wait for connect to complete */
    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(sockfd, &wfds);
    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    int nfds = select(sockfd + 1, NULL, &wfds, NULL, &tv);
    if (nfds <= 0) {
        fprintf(stderr, "Connection timed out\n");
        close(sockfd);
        return 1;
    }

    /* Back to blocking for reads */
    fcntl(sockfd, F_SETFL, 0);

    printf("Connected.\n\n");

    int ok = 0, fail = 0, timeout = 0;

    for (int i = 0; i < NUM_REGISTERS; i++) {
        /* Small delay between probes to avoid flooding */
        usleep(200000);  // 200ms

        int r = probe_register(sockfd, slave, &registers[i]);
        if (r == 0) ok++;
        else if (r == -1) fail++;
        else timeout++;
    }

    printf("\n=====================\n");
    printf("Results: %d OK, %d FAIL (illegal addr), %d timeout/error\n", ok, fail, timeout);
    printf("Pass rate: %.0f%%\n", (double)ok / NUM_REGISTERS * 100.0);

    close(sockfd);
    return (fail > 0) ? 1 : 0;
}
